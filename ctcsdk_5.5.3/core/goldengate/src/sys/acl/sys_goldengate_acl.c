/**
   @file sys_goldengate_acl.c

   @date 2012-09-19

   @version v2.0

 */

/****************************************************************************
*
* Header Files
*
****************************************************************************/
#include "ctc_error.h"
#include "ctc_const.h"
#include "ctc_common.h"
#include "ctc_macro.h"
#include "ctc_debug.h"
#include "ctc_hash.h"
#include "ctc_stats.h"
#include "ctc_parser.h"
#include "ctc_acl.h"
#include "ctc_qos.h"
#include "ctc_linklist.h"
#include "ctc_vlan.h"

#include "sys_goldengate_opf.h"
#include "sys_goldengate_chip.h"
#include "sys_goldengate_parser.h"
#include "sys_goldengate_stats.h"
#include "sys_goldengate_qos_policer.h"
#include "sys_goldengate_acl.h"
#include "sys_goldengate_queue_enq.h"
#include "sys_goldengate_nexthop_api.h"
#include "sys_goldengate_nexthop.h"
#include "sys_goldengate_internal_port.h"
#include "sys_goldengate_mirror.h"
#include "sys_goldengate_learning_aging.h"
#include "sys_goldengate_ftm.h"
#include "sys_goldengate_l2_fdb.h"
#include "sys_goldengate_common.h"
#include "sys_goldengate_vlan_mapping.h"
#include "sys_goldengate_wb_common.h"
#include "sys_goldengate_register.h"
#include "sys_goldengate_ftm.h"

#include "goldengate/include/drv_io.h"


#define SYS_ACL_L4_PORT_REF_CNT         0xFFFF
#define SYS_ACL_TCP_FALG_REF_CNT        0xFFFF
#define SYS_ACL_MAX_SESSION_NUM         4
#define SYS_ACL_PKT_LEN_NUM             14

#define SYS_ACL_ENTRY_OP_FLAG_ADD       1
#define SYS_ACL_ENTRY_OP_FLAG_DELETE    2


#define SYS_MAX_STRIP_PKT_LEN 128  /*used for strip packet*/

#define SYS_ACL_INVALID_QOS_POLICY      0

#define SYS_ACL_GROUP_HASH_BLOCK_SIZE           32
#define SYS_ACL_ENTRY_HASH_BLOCK_SIZE           32
#define SYS_ACL_ENTRY_HASH_BLOCK_SIZE_BY_KEY    1024

/* used by show only */
#define SYS_ACL_SHOW_IN_HEX   0xFFFFF

#define SYS_ACL_VLAN_ID_CHECK(vlan_id)           \
    {                                            \
        if ((vlan_id) > CTC_MAX_VLAN_ID) {       \
            return CTC_E_VLAN_INVALID_VLAN_ID; } \
    }                                            \

#define SYS_ACL_VLAN_ACTION_RESERVE_NUM    63

struct sys_acl_master_s
{
    ctc_hash_t          * group;           /* Hash table save group by gid.*/
    ctc_fpa_t           * fpa;             /* Fast priority arrangement. no array. */
    ctc_hash_t          * entry;           /* Hash table save hash|tcam entry by eid */
    ctc_hash_t          * hash_ent_key;    /* Hash table save hash entry by key */
    ctc_spool_t         * ad_spool;        /* Share pool save hash action  */
    ctc_spool_t         * vlan_edit_spool; /* Share pool save vlan edit  */
    sys_acl_block_t     block[ACL_BLOCK_ID_NUM];

    uint32              max_entry_priority;

    sys_acl_tcp_flag_t  tcp_flag[SYS_ACL_TCP_FLAG_NUM];
    sys_acl_l4port_op_t l4_port[SYS_ACL_L4_PORT_NUM];
    uint8               pkt_len_ref_cnt;
    uint8               l4_port_free_cnt;
    uint8               l4_port_range_num;
    uint8               tcp_flag_free_cnt;
    uint8               igs_block_num_max;         /* max number of block: 4 */
    uint8               egs_block_num_max;         /* max number of block: 1 */
    uint8               pbr_block_num_max;         /* max number of block: 1 */

    uint8               entry_size[CTC_ACL_KEY_NUM];
    uint8               key_size[CTC_ACL_KEY_NUM];
    uint8               hash_action_size;
    uint8               vlan_edit_size;

    uint8               ipv4_160bit_mode_en;

    uint32              key_count[ACL_ALLOC_COUNT_NUM];
 /*    uint16                      ptr_key_index[CTC_ACL_KEY_NUM]; // pointer to key index of specific keytype */

    sys_acl_register_t acl_register;
    sal_mutex_t        *mutex;
};
typedef struct sys_acl_master_s   sys_acl_master_t;
sys_acl_master_t* p_gg_acl_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};

#define SYS_ACL_DBG_OUT(level, FMT, ...) \
    CTC_DEBUG_OUT(acl, acl, ACL_SYS, level, FMT, ## __VA_ARGS__)

#define SYS_ACL_DBG_FUNC() \
    SYS_ACL_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__)

#define SYS_ACL_DBG_INFO(FMT, ...) \
    SYS_ACL_DBG_OUT(CTC_DEBUG_LEVEL_INFO, FMT, ## __VA_ARGS__)

#define SYS_ACL_DBG_PARAM(FMT, ...) \
    SYS_ACL_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, FMT, ## __VA_ARGS__)

#define SYS_ACL_DBG_ERROR(FMT, ...) \
    SYS_ACL_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, FMT, ## __VA_ARGS__)

#define SYS_ACL_DBG_DUMP(FMT, ...) \
    SYS_ACL_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, FMT, ## __VA_ARGS__)

#define SYS_ACL_INIT_CHECK(lchip)           \
    do {                                    \
        SYS_LCHIP_CHECK_ACTIVE(lchip);           \
        if (NULL == p_gg_acl_master[lchip]) {  \
            return CTC_E_NOT_INIT; }        \
    } while(0)

#define SYS_ACL_CREATE_LOCK(lchip)                          \
    do                                                      \
    {                                                       \
        sal_mutex_create(&p_gg_acl_master[lchip]->mutex);   \
        if (NULL == p_gg_acl_master[lchip]->mutex)          \
        {                                                   \
            CTC_ERROR_RETURN(CTC_E_FAIL_CREATE_MUTEX);      \
        }                                                   \
    } while (0)

#define SYS_ACL_LOCK(lchip) \
    sal_mutex_lock(p_gg_acl_master[lchip]->mutex)

#define SYS_ACL_UNLOCK(lchip) \
    sal_mutex_unlock(p_gg_acl_master[lchip]->mutex)

#define CTC_ERROR_RETURN_ACL_UNLOCK(op) \
    do \
    { \
        int32 rv = (op); \
        if (rv < 0) \
        { \
            sal_mutex_unlock(p_gg_acl_master[lchip]->mutex); \
            return rv; \
        } \
    } while (0)

#define SYS_ACL_GET_BLOCK_ID(pe, block_id)        \
{                                           \
    block_id = pe->group->group_info.block_id;  \
    if((CTC_ACL_KEY_PBR_IPV4 == pe->key.type) || (CTC_ACL_KEY_PBR_IPV6 == pe->key.type))    \
    {                                                                                       \
        block_id = ACL_IGS_BLOCK_ID_NUM + ACL_EGS_BLOCK_ID_NUM;                             \
    }                                                                                       \
    else if(CTC_EGRESS == pe->group->group_info.dir)                                             \
    {                                                                                       \
        block_id += ACL_IGS_BLOCK_ID_NUM;                                                   \
    }                                                                                       \
}

/* input rsv group id , return failed */
#define SYS_ACL_IS_RSV_GROUP(gid) \
    ((gid) > CTC_ACL_GROUP_ID_NORMAL && (gid) < CTC_ACL_GROUP_ID_MAX)

#define SYS_ACL_CHECK_RSV_GROUP_ID(gid)      \
    {                                        \
        if SYS_ACL_IS_RSV_GROUP(gid)         \
        {                                    \
            return CTC_E_ACL_GROUP_ID_RSVED; \
        }                                    \
    }

#define SYS_ACL_CHECK_GROUP_ID(gid)        \
    {                                      \
        if (gid >= CTC_ACL_GROUP_ID_MAX) { \
            return CTC_E_ACL_GROUP_ID; }   \
    }

#define SYS_ACL_CHECK_GROUP_TYPE(type)        \
    {                                         \
        if (type >= CTC_ACL_GROUP_TYPE_MAX) { \
            return CTC_E_ACL_GROUP_TYPE; }    \
    }

#define SYS_ACL_CHECK_GROUP_PRIO(dir, priority)                                      \
    {                                                                                \
        if (((CTC_INGRESS == dir) && (priority >= p_gg_acl_master[lchip]->igs_block_num_max)) || \
            ((CTC_EGRESS == dir) && (priority >= p_gg_acl_master[lchip]->egs_block_num_max)))    \
        {                                                                            \
            return CTC_E_ACL_GROUP_PRIORITY;                                         \
        }                                                                            \
    }

#define SYS_ACL_CHECK_ENTRY_ID(eid) \
    {                               \
        ;                           \
    }

#define SYS_ACL_CHECK_ENTRY_PRIO(priority)               \
    {                                                    \
        if (priority > p_gg_acl_master[lchip]->max_entry_priority) { \
            return CTC_E_ACL_GROUP_PRIORITY; }           \
    }

#define SYS_ACL_SERVICE_ID_CHECK(sid)    CTC_MAX_VALUE_CHECK(sid, 0x3FFF)

#define SYS_ACL_CHECK_GROUP_UNEXIST(pg)     \
    {                                       \
        if (!pg)                            \
        {                                   \
            SYS_ACL_UNLOCK(lchip);          \
            return CTC_E_ACL_GROUP_UNEXIST; \
        }                                   \
    }

#define _DEF_ERR

#define SYS_ACL_SET_MAC_HIGH(dest_h, src)    \
    {                                        \
        dest_h = ((src[0] << 8) | (src[1])); \
    }

#define SYS_ACL_SET_MAC_LOW(dest_l, src)                                       \
    {                                                                          \
        dest_l = ((src[2] << 24) | (src[3] << 16) | (src[4] << 8) | (src[5])); \
    }

#define ACL_ENTRY_IS_TCAM(type)                                            \
    ((CTC_ACL_KEY_HASH_MAC != type) && (CTC_ACL_KEY_HASH_IPV4 != type) &&  \
     (CTC_ACL_KEY_HASH_MPLS != type) && (CTC_ACL_KEY_HASH_IPV6 != type) && \
     (CTC_ACL_KEY_HASH_L2_L3 != type))


#define  DUMP_IPV4(pk, TYPE)                                             \
    {                                                                    \
        uint32 addr;                                                     \
        char   ip_addr[16];                                              \
        if (CTC_FLAG_ISSET(pk->flag, CTC_ACL_##TYPE##_KEY_FLAG_IP_SA))   \
        {                                                                \
            if (0xFFFFFFFF == pk->u0.ip.ip_sa_mask)                      \
            {                                                            \
                SYS_ACL_DBG_DUMP("  ip-sa host");                        \
                addr = sal_ntohl(pk->u0.ip.ip_sa);                       \
                sal_inet_ntop(AF_INET, &addr, ip_addr, sizeof(ip_addr)); \
                SYS_ACL_DBG_DUMP(" %s", ip_addr);                        \
            }                                                            \
            else if (0 == pk->u0.ip.ip_sa_mask)                          \
            {                                                            \
                SYS_ACL_DBG_DUMP("  ip-sa any");                         \
            }                                                            \
            else                                                         \
            {                                                            \
                SYS_ACL_DBG_DUMP("  ip-sa");                             \
                addr = sal_ntohl(pk->u0.ip.ip_sa);                       \
                sal_inet_ntop(AF_INET, &addr, ip_addr, sizeof(ip_addr)); \
                SYS_ACL_DBG_DUMP(" %s", ip_addr);                        \
                addr = sal_ntohl(pk->u0.ip.ip_sa_mask);                  \
                sal_inet_ntop(AF_INET, &addr, ip_addr, sizeof(ip_addr)); \
                SYS_ACL_DBG_DUMP("  %s", ip_addr);                       \
            }                                                            \
        }                                                                \
        else                                                             \
        {                                                                \
            SYS_ACL_DBG_DUMP("  ip-sa any");                             \
        }                                                                \
                                                                         \
        if (CTC_FLAG_ISSET(pk->flag, CTC_ACL_##TYPE##_KEY_FLAG_IP_SA))   \
        {                                                                \
            if (0xFFFFFFFF == pk->u0.ip.ip_da_mask)                      \
            {                                                            \
                SYS_ACL_DBG_DUMP(",  ip-da host");                       \
                addr = sal_ntohl(pk->u0.ip.ip_da);                       \
                sal_inet_ntop(AF_INET, &addr, ip_addr, sizeof(ip_addr)); \
                SYS_ACL_DBG_DUMP(" %s", ip_addr);                        \
            }                                                            \
            else if (0 == pk->u0.ip.ip_da_mask)                          \
            {                                                            \
                SYS_ACL_DBG_DUMP(",  ip-da any");                        \
            }                                                            \
            else                                                         \
            {                                                            \
                SYS_ACL_DBG_DUMP(",  ip-da");                            \
                addr = sal_ntohl(pk->u0.ip.ip_da);                       \
                sal_inet_ntop(AF_INET, &addr, ip_addr, sizeof(ip_addr)); \
                SYS_ACL_DBG_DUMP(" %s", ip_addr);                        \
                addr = sal_ntohl(pk->u0.ip.ip_da_mask);                  \
                sal_inet_ntop(AF_INET, &addr, ip_addr, sizeof(ip_addr)); \
                SYS_ACL_DBG_DUMP("  %s", ip_addr);                       \
            }                                                            \
        }                                                                \
        else                                                             \
        {                                                                \
            SYS_ACL_DBG_DUMP(",  ip-da any");                            \
        }                                                                \
        SYS_ACL_DBG_DUMP("\n");                                          \
    }

#define    DUMP_IPV6(pk, TYPE)                                                                         \
    {                                                                                                  \
        char        buf[CTC_IPV6_ADDR_STR_LEN];                                                        \
        ipv6_addr_t ipv6_addr;                                                                         \
        sal_memset(buf, 0, CTC_IPV6_ADDR_STR_LEN);                                                     \
        sal_memset(ipv6_addr, 0, sizeof(ipv6_addr_t));                                                 \
        if (CTC_FLAG_ISSET(pk->flag, CTC_ACL_##TYPE##_KEY_FLAG_IP_SA))                                 \
        {                                                                                              \
            if ((0xFFFFFFFF == pk->u0.ip.ip_sa_mask[0]) && (0xFFFFFFFF == pk->u0.ip.ip_sa_mask[1])     \
                && (0xFFFFFFFF == pk->u0.ip.ip_sa_mask[2]) && (0xFFFFFFFF == pk->u0.ip.ip_sa_mask[3])) \
            {                                                                                          \
                SYS_ACL_DBG_DUMP("  ip-sa host ");                                                     \
                                                                                                       \
                ipv6_addr[0] = sal_htonl(pk->u0.ip.ip_sa[0]);                                          \
                ipv6_addr[1] = sal_htonl(pk->u0.ip.ip_sa[1]);                                          \
                ipv6_addr[2] = sal_htonl(pk->u0.ip.ip_sa[2]);                                          \
                ipv6_addr[3] = sal_htonl(pk->u0.ip.ip_sa[3]);                                          \
                                                                                                       \
                sal_inet_ntop(AF_INET6, ipv6_addr, buf, CTC_IPV6_ADDR_STR_LEN);                        \
                SYS_ACL_DBG_DUMP("%20s", buf);                                                         \
            }                                                                                          \
            else if ((0 == pk->u0.ip.ip_sa_mask[0]) && (0 == pk->u0.ip.ip_sa_mask[1])                  \
                     && (0 == pk->u0.ip.ip_sa_mask[2]) && (0 == pk->u0.ip.ip_sa_mask[3]))              \
            {                                                                                          \
                SYS_ACL_DBG_DUMP("  ip-sa any");                                                       \
            }                                                                                          \
            else                                                                                       \
            {                                                                                          \
                SYS_ACL_DBG_DUMP("  ip-sa");                                                           \
                ipv6_addr[0] = sal_htonl(pk->u0.ip.ip_sa[0]);                                          \
                ipv6_addr[1] = sal_htonl(pk->u0.ip.ip_sa[1]);                                          \
                ipv6_addr[2] = sal_htonl(pk->u0.ip.ip_sa[2]);                                          \
                ipv6_addr[3] = sal_htonl(pk->u0.ip.ip_sa[3]);                                          \
                sal_inet_ntop(AF_INET6, ipv6_addr, buf, CTC_IPV6_ADDR_STR_LEN);                        \
                SYS_ACL_DBG_DUMP("%20s ", buf);                                                        \
                                                                                                       \
                ipv6_addr[0] = sal_htonl(pk->u0.ip.ip_sa_mask[0]);                                     \
                ipv6_addr[1] = sal_htonl(pk->u0.ip.ip_sa_mask[1]);                                     \
                ipv6_addr[2] = sal_htonl(pk->u0.ip.ip_sa_mask[2]);                                     \
                ipv6_addr[3] = sal_htonl(pk->u0.ip.ip_sa_mask[3]);                                     \
                                                                                                       \
                sal_inet_ntop(AF_INET6, ipv6_addr, buf, CTC_IPV6_ADDR_STR_LEN);                        \
                SYS_ACL_DBG_DUMP("%20s", buf);                                                         \
            }                                                                                          \
        }                                                                                              \
        else                                                                                           \
        {                                                                                              \
            SYS_ACL_DBG_DUMP("  ip-sa any");                                                           \
        }                                                                                              \
                                                                                                       \
        sal_memset(buf, 0, CTC_IPV6_ADDR_STR_LEN);                                                     \
                                                                                                       \
        if (CTC_FLAG_ISSET(pk->flag, CTC_ACL_##TYPE##_KEY_FLAG_IP_DA))                                     \
        {                                                                                              \
            if ((0xFFFFFFFF == pk->u0.ip.ip_da_mask[0]) && (0xFFFFFFFF == pk->u0.ip.ip_da_mask[1])     \
                && (0xFFFFFFFF == pk->u0.ip.ip_da_mask[2]) && (0xFFFFFFFF == pk->u0.ip.ip_da_mask[3])) \
            {                                                                                          \
                SYS_ACL_DBG_DUMP(",  ip-da host ");                                                    \
                                                                                                       \
                ipv6_addr[0] = sal_htonl(pk->u0.ip.ip_da[0]);                                          \
                ipv6_addr[1] = sal_htonl(pk->u0.ip.ip_da[1]);                                          \
                ipv6_addr[2] = sal_htonl(pk->u0.ip.ip_da[2]);                                          \
                ipv6_addr[3] = sal_htonl(pk->u0.ip.ip_da[3]);                                          \
                                                                                                       \
                sal_inet_ntop(AF_INET6, ipv6_addr, buf, CTC_IPV6_ADDR_STR_LEN);                        \
                SYS_ACL_DBG_DUMP("%20s", buf);                                                         \
            }                                                                                          \
            else if ((0 == pk->u0.ip.ip_da_mask[0]) && (0 == pk->u0.ip.ip_da_mask[1])                  \
                     && (0 == pk->u0.ip.ip_da_mask[2]) && (0 == pk->u0.ip.ip_da_mask[3]))              \
            {                                                                                          \
                SYS_ACL_DBG_DUMP(",  ip-da any");                                                      \
            }                                                                                          \
            else                                                                                       \
            {                                                                                          \
                SYS_ACL_DBG_DUMP(",  ip-da");                                                          \
                ipv6_addr[0] = sal_htonl(pk->u0.ip.ip_da[0]);                                          \
                ipv6_addr[1] = sal_htonl(pk->u0.ip.ip_da[1]);                                          \
                ipv6_addr[2] = sal_htonl(pk->u0.ip.ip_da[2]);                                          \
                ipv6_addr[3] = sal_htonl(pk->u0.ip.ip_da[3]);                                          \
                sal_inet_ntop(AF_INET6, ipv6_addr, buf, CTC_IPV6_ADDR_STR_LEN);                        \
                SYS_ACL_DBG_DUMP("%20s ", buf);                                                        \
                                                                                                       \
                ipv6_addr[0] = sal_htonl(pk->u0.ip.ip_da_mask[0]);                                     \
                ipv6_addr[1] = sal_htonl(pk->u0.ip.ip_da_mask[1]);                                     \
                ipv6_addr[2] = sal_htonl(pk->u0.ip.ip_da_mask[2]);                                     \
                ipv6_addr[3] = sal_htonl(pk->u0.ip.ip_da_mask[3]);                                     \
                                                                                                       \
                sal_inet_ntop(AF_INET6, ipv6_addr, buf, CTC_IPV6_ADDR_STR_LEN);                        \
                SYS_ACL_DBG_DUMP("%20s", buf);                                                         \
            }                                                                                          \
        }                                                                                              \
        else                                                                                           \
        {                                                                                              \
            SYS_ACL_DBG_DUMP(",  ip-da any");                                                          \
        }                                                                                              \
        SYS_ACL_DBG_DUMP("\n");                                                                        \
    }

#define    DUMP_COMMON_L2(pk, TYPE)                                                       \
    SYS_ACL_DBG_DUMP("  mac-sa");                                                         \
    if (CTC_FLAG_ISSET(pk->flag, CTC_ACL_##TYPE##_KEY_FLAG_MAC_SA))                       \
    {                                                                                     \
        if ((0xFFFF == *(uint16 *) &pk->mac_sa_mask[0]) &&                               \
            (0xFFFFFFFF == *(uint32 *) &pk->mac_sa_mask[2]))                             \
        {                                                                                 \
            SYS_ACL_DBG_DUMP(" host %02x%02x.%02x%02x.%02x%02x",                          \
                             pk->mac_sa[0], pk->mac_sa[1], pk->mac_sa[2],                 \
                             pk->mac_sa[3], pk->mac_sa[4], pk->mac_sa[5]);                \
        }                                                                                 \
        else if ((0 == *(uint16 *) &pk->mac_sa_mask[0]) &&                                \
                 (0 == *(uint32 *) &pk->mac_sa_mask[2]))                                  \
        {                                                                                 \
            SYS_ACL_DBG_DUMP(" any");                                                     \
        }                                                                                 \
        else                                                                              \
        {                                                                                 \
            SYS_ACL_DBG_DUMP(" %02x%02x.%02x%02x.%02x%02x %02x%02x.%02x%02x.%02x%02x",    \
                             pk->mac_sa[0], pk->mac_sa[1], pk->mac_sa[2],                 \
                             pk->mac_sa[3], pk->mac_sa[4], pk->mac_sa[5],                 \
                             pk->mac_sa_mask[0], pk->mac_sa_mask[1], pk->mac_sa_mask[2],  \
                             pk->mac_sa_mask[3], pk->mac_sa_mask[4], pk->mac_sa_mask[5]); \
        }                                                                                 \
    }                                                                                     \
    else                                                                                  \
    {                                                                                     \
        SYS_ACL_DBG_DUMP(" any");                                                         \
    }                                                                                     \
                                                                                          \
    SYS_ACL_DBG_DUMP(",  mac-da");                                                        \
    if (CTC_FLAG_ISSET(pk->flag, CTC_ACL_##TYPE##_KEY_FLAG_MAC_DA))                       \
    {                                                                                     \
        if ((0xFFFF == *(uint16 *) &pk->mac_da_mask[0]) &&                                \
            (0xFFFFFFFF == *(uint32 *) &pk->mac_da_mask[2]))                              \
        {                                                                                 \
            SYS_ACL_DBG_DUMP(" host %02x%02x.%02x%02x.%02x%02x",                          \
                             pk->mac_da[0], pk->mac_da[1], pk->mac_da[2],                 \
                             pk->mac_da[3], pk->mac_da[4], pk->mac_da[5]);                \
        }                                                                                 \
        else if ((0 == *(uint16 *) &pk->mac_da_mask[0]) &&                                \
                 (0 == *(uint32 *) &pk->mac_da_mask[2]))                                  \
        {                                                                                 \
            SYS_ACL_DBG_DUMP(" any");                                                     \
        }                                                                                 \
        else                                                                              \
        {                                                                                 \
            SYS_ACL_DBG_DUMP(" %02x%02x.%02x%02x.%02x%02x %02x%02x.%02x%02x.%02x%02x",    \
                             pk->mac_da[0], pk->mac_da[1], pk->mac_da[2],                 \
                             pk->mac_da[3], pk->mac_da[4], pk->mac_da[5],                 \
                             pk->mac_da_mask[0], pk->mac_da_mask[1], pk->mac_da_mask[2],  \
                             pk->mac_da_mask[3], pk->mac_da_mask[4], pk->mac_da_mask[5]); \
        }                                                                                 \
    }                                                                                     \
    else                                                                                  \
    {                                                                                     \
        SYS_ACL_DBG_DUMP(" any");                                                         \
    }                                                                                     \
    SYS_ACL_DBG_DUMP("\n");                                                               \
                                                                                          \
                                                                                          \
    if (CTC_FLAG_ISSET(pk->flag, CTC_ACL_##TYPE##_KEY_FLAG_CVLAN))                        \
    {                                                                                     \
        SYS_ACL_DBG_DUMP("  c-vlan 0x%x  mask 0x%0x,", pk->cvlan, pk->cvlan_mask);        \
    }                                                                                     \
    if (CTC_FLAG_ISSET(pk->flag, CTC_ACL_##TYPE##_KEY_FLAG_CTAG_COS))                     \
    {                                                                                     \
        SYS_ACL_DBG_DUMP("  c-cos %u  mask %u,", pk->ctag_cos, pk->ctag_cos_mask);        \
    }                                                                                     \
    if (CTC_FLAG_ISSET(pk->flag, CTC_ACL_##TYPE##_KEY_FLAG_CTAG_CFI))                     \
    {                                                                                     \
        SYS_ACL_DBG_DUMP("  c-cfi %u,", pk->ctag_cfi);                                    \
    }                                                                                     \
    if (CTC_FLAG_ISSET(pk->flag, CTC_ACL_##TYPE##_KEY_FLAG_CVLAN) +                       \
        CTC_FLAG_ISSET(pk->flag, CTC_ACL_##TYPE##_KEY_FLAG_CTAG_COS) +                    \
        CTC_FLAG_ISSET(pk->flag, CTC_ACL_##TYPE##_KEY_FLAG_CTAG_CFI))                     \
    {                                                                                     \
        SYS_ACL_DBG_DUMP("\n");                                                           \
    }                                                                                     \
    if (CTC_FLAG_ISSET(pk->flag, CTC_ACL_##TYPE##_KEY_FLAG_SVLAN))                        \
    {                                                                                     \
        SYS_ACL_DBG_DUMP("  s-vlan 0x%x  mask 0x%0x,", pk->svlan, pk->svlan_mask);        \
    }                                                                                     \
    if (CTC_FLAG_ISSET(pk->flag, CTC_ACL_##TYPE##_KEY_FLAG_STAG_COS))                     \
    {                                                                                     \
        SYS_ACL_DBG_DUMP("  s-cos %u  mask %u,", pk->stag_cos, pk->stag_cos_mask);        \
    }                                                                                     \
    if (CTC_FLAG_ISSET(pk->flag, CTC_ACL_##TYPE##_KEY_FLAG_STAG_CFI))                     \
    {                                                                                     \
        SYS_ACL_DBG_DUMP("  s-cfi %u,", pk->stag_cfi);                                    \
    }                                                                                     \
    if (CTC_FLAG_ISSET(pk->flag, CTC_ACL_##TYPE##_KEY_FLAG_SVLAN) +                       \
        CTC_FLAG_ISSET(pk->flag, CTC_ACL_##TYPE##_KEY_FLAG_STAG_COS) +                    \
        CTC_FLAG_ISSET(pk->flag, CTC_ACL_##TYPE##_KEY_FLAG_STAG_CFI))                     \
    {                                                                                     \
        SYS_ACL_DBG_DUMP("\n");                                                           \
    }                                                                                     \
    if (CTC_FLAG_ISSET(pk->flag, CTC_ACL_##TYPE##_KEY_FLAG_L2_TYPE))                      \
    {                                                                                     \
        SYS_ACL_DBG_DUMP("  l2_type %u  mask %u,", pk->l2_type, pk->l2_type_mask);        \
    }                                                                                     \
    if (CTC_FLAG_ISSET(pk->flag, CTC_ACL_##TYPE##_KEY_FLAG_CTAG_VALID))                   \
    {                                                                                     \
        SYS_ACL_DBG_DUMP("  ctag_valid %u,", pk->ctag_valid);                             \
    }                                                                                     \
    if (CTC_FLAG_ISSET(pk->flag, CTC_ACL_##TYPE##_KEY_FLAG_STAG_VALID))                   \
    {                                                                                     \
        SYS_ACL_DBG_DUMP("  stag_valid %u,", pk->stag_valid);                             \
    }                                                                                     \
    if (CTC_FLAG_ISSET(pk->flag, CTC_ACL_##TYPE##_KEY_FLAG_ETH_TYPE)                      \
        + CTC_FLAG_ISSET(pk->flag, CTC_ACL_##TYPE##_KEY_FLAG_L2_TYPE) +                   \
        + CTC_FLAG_ISSET(pk->flag, CTC_ACL_##TYPE##_KEY_FLAG_CTAG_VALID) +                \
        + CTC_FLAG_ISSET(pk->flag, CTC_ACL_##TYPE##_KEY_FLAG_STAG_VALID))                 \
    {                                                                                     \
        SYS_ACL_DBG_DUMP("\n");                                                           \
    }

#define   DUMP_COMMON_L3(pk, TYPE)                                                                                           \
    if (CTC_FLAG_ISSET(pk->flag, CTC_ACL_##TYPE##_KEY_FLAG_DSCP))                                                            \
    {                                                                                                                        \
        SYS_ACL_DBG_DUMP("  dscp %s ", CTC_DEBUG_HEX_FORMAT(str, format, pk->u0.ip.dscp, 2, U));                                \
        SYS_ACL_DBG_DUMP("  dscp-mask %s", CTC_DEBUG_HEX_FORMAT(str, format, pk->u0.ip.dscp_mask, 2, U));                       \
    }                                                                                                                        \
    if (CTC_FLAG_ISSET(pk->flag, CTC_ACL_##TYPE##_KEY_FLAG_PRECEDENCE))                                                      \
    {                                                                                                                        \
        SYS_ACL_DBG_DUMP("  precedence %s", CTC_DEBUG_HEX_FORMAT(str, format, (pk->u0.ip.dscp & 0x7 << 3), 2, U));             \
        SYS_ACL_DBG_DUMP("  precedence-mask %s", CTC_DEBUG_HEX_FORMAT(str, format, ((pk->u0.ip.dscp_mask & 0x7) << 3), 2, U));\
    }                                                                                                                        \
    if (CTC_FLAG_ISSET(pk->flag, CTC_ACL_##TYPE##_KEY_FLAG_IP_FRAG))                                                         \
    {                                                                                                                        \
        SYS_ACL_DBG_DUMP("  frag_info %s", CTC_DEBUG_HEX_FORMAT(str, format, pk->u0.ip.frag_info, 1, U));                       \
        SYS_ACL_DBG_DUMP("  frag_info-mask %s", CTC_DEBUG_HEX_FORMAT(str, format, pk->u0.ip.frag_info_mask, 1, U));             \
    }                                                                                                                        \
    if (CTC_FLAG_ISSET(pk->flag, CTC_ACL_##TYPE##_KEY_FLAG_IP_OPTION))                                                       \
    {                                                                                                                        \
        SYS_ACL_DBG_DUMP("  ip_option %u", pk->u0.ip.ip_option);                                                             \
    }                                                                                                                        \
    if (CTC_FLAG_ISSET(pk->flag, CTC_ACL_##TYPE##_KEY_FLAG_DSCP)                                                             \
        + CTC_FLAG_ISSET(pk->flag, CTC_ACL_##TYPE##_KEY_FLAG_PRECEDENCE)                                                     \
        + CTC_FLAG_ISSET(pk->flag, CTC_ACL_##TYPE##_KEY_FLAG_IP_FRAG)                                                        \
        + CTC_FLAG_ISSET(pk->flag, CTC_ACL_##TYPE##_KEY_FLAG_IP_OPTION))                                                     \
    {                                                                                                                        \
        SYS_ACL_DBG_DUMP("\n");                                                                                              \
    }                                                                                                                        \
    if (CTC_FLAG_ISSET(pk->flag, CTC_ACL_##TYPE##_KEY_FLAG_ROUTED_PACKET))                                                   \
    {                                                                                                                        \
        SYS_ACL_DBG_DUMP("  routed-packet %u,", pk->routed_packet);                                                          \
    }                                                                                                                        \
    if (CTC_FLAG_ISSET(pk->flag, CTC_ACL_##TYPE##_KEY_FLAG_ECN))                                                             \
    {                                                                                                                        \
        SYS_ACL_DBG_DUMP("  ecn  %d", pk->u0.ip.ecn);                                                                        \
        SYS_ACL_DBG_DUMP("  ecn-mask  %d", pk->u0.ip.ecn_mask);                                                              \
    }                                                                                                                        \
    if (CTC_FLAG_ISSET(pk->flag, CTC_ACL_##TYPE##_KEY_FLAG_ROUTED_PACKET)                                                    \
        + CTC_FLAG_ISSET(pk->flag, CTC_ACL_##TYPE##_KEY_FLAG_ECN))                                                           \
    {                                                                                                                        \
        SYS_ACL_DBG_DUMP("\n");                                                                                              \
    }                                                                                                                        \
    if (CTC_FLAG_ISSET(pk->flag, CTC_ACL_##TYPE##_KEY_FLAG_ETH_TYPE))                                                        \
    {                                                                                                                        \
        SYS_ACL_DBG_DUMP("  eth-type 0x%x  mask 0x%x,",                                                                      \
                         pk->u0.other.eth_type, pk->u0.other.eth_type_mask);                                                 \
    }

#define  DUMP_L4_PROTOCOL(pk, TYPE)                                                                                            \
    if (CTC_FLAG_ISSET(pk->flag, CTC_ACL_##TYPE##_KEY_FLAG_L4_PROTOCOL))                                                       \
    {                                                                                                                          \
        SYS_ACL_DBG_DUMP("  tcp_flags_idx %d  flag_tcp_flags %d",                                                             \
                         pk->u0.ip.l4.tcp_flags_idx, pk->u0.ip.l4.flag_tcp_flags);                                             \
        SYS_ACL_DBG_DUMP("  flag_l4_src_port_range %d  flag_l4_dst_port_range %d\n",                                            \
                         pk->u0.ip.l4.flag_l4_src_port_range, pk->u0.ip.l4.flag_l4_dst_port_range);                            \
        SYS_ACL_DBG_DUMP("  l4_src_port_range_idx %d  l4_dst_port_range_idx %d" ,                                             \
                         pk->u0.ip.l4.l4_src_port_range_idx,  pk->u0.ip.l4.l4_dst_port_range_idx);                             \
        SYS_ACL_DBG_DUMP("  flag_l4_type %d,  flag_l4info_mapped %d\n",                                                        \
                         pk->u0.ip.l4.flag_l4_type,  pk->u0.ip.l4.flag_l4info_mapped);                                         \
                                                                                                                               \
        SYS_ACL_DBG_DUMP("  l4_type %s", CTC_DEBUG_HEX_FORMAT(str, format, pk->u0.ip.l4.l4_type, 1, U));                          \
        SYS_ACL_DBG_DUMP("  l4_type-mask %s", CTC_DEBUG_HEX_FORMAT(str, format, pk->u0.ip.l4.l4_type_mask, 1, U));                \
        SYS_ACL_DBG_DUMP("  l4_compress_type %s", CTC_DEBUG_HEX_FORMAT(str, format, pk->u0.ip.l4.l4_compress_type, 1, U));        \
        SYS_ACL_DBG_DUMP("  l4info_mapped %s", CTC_DEBUG_HEX_FORMAT(str, format, pk->u0.ip.l4.l4info_mapped, 4, U));            \
        SYS_ACL_DBG_DUMP("  l4info_mapped-mask %s\n", CTC_DEBUG_HEX_FORMAT(str, format, pk->u0.ip.l4.l4info_mapped_mask, 1, U));    \
        SYS_ACL_DBG_DUMP("  l4_src_port %s", CTC_DEBUG_HEX_FORMAT(str, format, pk->u0.ip.l4.l4_src_port,  4, U));                 \
        SYS_ACL_DBG_DUMP("  l4_src_port-mask %s", CTC_DEBUG_HEX_FORMAT(str, format, pk->u0.ip.l4.l4_src_port_mask, 4, U));        \
        SYS_ACL_DBG_DUMP("  l4_dst_port %s", CTC_DEBUG_HEX_FORMAT(str, format, pk->u0.ip.l4.l4_dst_port,  4, U));                 \
        SYS_ACL_DBG_DUMP("  l4_dst_port-mask %s\n", CTC_DEBUG_HEX_FORMAT(str, format, pk->u0.ip.l4.l4_dst_port_mask, 4, U));      \
    }

#define   DUMP_L3_COMMON_PBR(pk, TYPE)                                                                                    \
    if (CTC_FLAG_ISSET(pk->flag, CTC_ACL_##TYPE##_KEY_FLAG_DSCP))                                                         \
    {                                                                                                                     \
        SYS_ACL_DBG_DUMP("  dscp %s", CTC_DEBUG_HEX_FORMAT(str, format, pk->u0.ip.dscp_mask, 2, U));                         \
        SYS_ACL_DBG_DUMP("  dscp-mask %s", CTC_DEBUG_HEX_FORMAT(str, format, pk->u0.ip.dscp_mask, 2, U));                    \
    }                                                                                                                     \
    if (CTC_FLAG_ISSET(pk->flag, CTC_ACL_##TYPE##_KEY_FLAG_PRECEDENCE))                                                   \
    {                                                                                                                     \
        SYS_ACL_DBG_DUMP("  precedence %s", CTC_DEBUG_HEX_FORMAT(str, format, (pk->u0.ip.dscp & 0x7) << 3, 2, U));           \
        SYS_ACL_DBG_DUMP("  precedence-mask %s", CTC_DEBUG_HEX_FORMAT(str, format, (pk->u0.ip.dscp_mask & 0x7) << 3, 2, U)); \
    }                                                                                                                     \
                                                                                                                          \
    if (CTC_FLAG_ISSET(pk->flag, CTC_ACL_##TYPE##_KEY_FLAG_IP_FRAG))                                                      \
    {                                                                                                                     \
        SYS_ACL_DBG_DUMP("  frag_info %s", CTC_DEBUG_HEX_FORMAT(str, format, pk->u0.ip.frag_info_mask, 1, U));               \
        SYS_ACL_DBG_DUMP("  frag_info-mask %s", CTC_DEBUG_HEX_FORMAT(str, format, pk->u0.ip.frag_info_mask, 1, U));          \
    }                                                                                                                     \
    if (CTC_FLAG_ISSET(pk->flag, CTC_ACL_##TYPE##_KEY_FLAG_VRFID))                                                        \
    {                                                                                                                     \
        SYS_ACL_DBG_DUMP("  vrfid %s", CTC_DEBUG_HEX_FORMAT(str, format, pk->vrfid, 3, U));                                  \
        SYS_ACL_DBG_DUMP("  vrfid-mask %s", CTC_DEBUG_HEX_FORMAT(str, format, pk->vrfid_mask, 3, U));                        \
    }                                                                                                                     \
                                                                                                                          \
    if ((CTC_FLAG_ISSET(pk->flag, CTC_ACL_##TYPE##_KEY_FLAG_DSCP))                                                        \
       || (CTC_FLAG_ISSET(pk->flag, CTC_ACL_##TYPE##_KEY_FLAG_PRECEDENCE))                                                \
       || (CTC_FLAG_ISSET(pk->flag, CTC_ACL_##TYPE##_KEY_FLAG_IP_FRAG))                                                   \
       || (CTC_FLAG_ISSET(pk->flag, CTC_ACL_##TYPE##_KEY_FLAG_VRFID)))                                                    \
    {                                                                                                                     \
        SYS_ACL_DBG_DUMP("\n");                                                                                           \
    }

#define   DUMP_IPV6_PBR_UNIQUE(pk)                                                                             \
    if (CTC_FLAG_ISSET(pk->flag, CTC_ACL_PBR_IPV6_KEY_FLAG_FLOW_LABEL))                                        \
    {                                                                                                          \
        SYS_ACL_DBG_DUMP("  flow_label %s", CTC_DEBUG_HEX_FORMAT(str, format, pk->flow_label, 8, U));             \
        SYS_ACL_DBG_DUMP("  flow_label-mask %s\n", CTC_DEBUG_HEX_FORMAT(str, format, pk->flow_label_mask, 8, U)); \
    }

#define    DUMP_MAC_UNIQUE(pk)                                                                                 \
    if (CTC_FLAG_ISSET(pk->flag, CTC_ACL_MAC_KEY_FLAG_VLAN_NUM))                                               \
    {                                                                                                          \
        SYS_ACL_DBG_DUMP("vlan_num  %d, vlan_num_mask  %d\n", pk->vlan_num, pk->vlan_num_mask);                \
    }                                                                                                          \
    if (CTC_FLAG_ISSET(pk->flag, CTC_ACL_MAC_KEY_FLAG_ETH_TYPE))                                               \
    {                                                                                                          \
        SYS_ACL_DBG_DUMP("  eth-type 0x%x  mask 0x%x,", pk->eth_type, pk->eth_type_mask);                      \
    }

#define   DUMP_IPV4_UNIQUE(pk)                                                                                                  \
    DUMP_IPV4(pk, IPV4)                                                                                                         \
    if (CTC_FLAG_ISSET(pk->flag, CTC_ACL_IPV4_KEY_FLAG_L3_TYPE)                                                                 \
        && ((pk->l3_type & pk->l3_type_mask) == (CTC_PARSER_L3_TYPE_ARP & 0xff)))                                   \
    {                                                                                                                           \
        SYS_ACL_DBG_DUMP("  arp-packet %u,", 1);                                                                                \
    }                                                                                                                           \
    if (CTC_FLAG_ISSET(pk->flag, CTC_ACL_IPV4_KEY_FLAG_IPV4_PACKET))                                                            \
    {                                                                                                                           \
        SYS_ACL_DBG_DUMP("  ipv4-packet %u,", 1);                                                                               \
    }                                                                                                                           \
    if ((CTC_FLAG_ISSET(pk->flag, CTC_ACL_IPV4_KEY_FLAG_L3_TYPE)                                                               \
        && ((pk->l3_type & pk->l3_type_mask) == (CTC_PARSER_L3_TYPE_ARP & 0xff)))                                  \
        + CTC_FLAG_ISSET(pk->flag, CTC_ACL_IPV4_KEY_FLAG_IPV4_PACKET))                                                         \
    {                                                                                                                           \
        SYS_ACL_DBG_DUMP("\n");                                                                                                 \
    }                                                                                                                           \
    if ((pk->l3_type_mask & pk->l3_type) == (CTC_PARSER_L3_TYPE_ARP & 0xff))                                        \
    {                                                                                                                           \
        SYS_ACL_DBG_DUMP("  unique_l3_type %d", pk->unique_l3_type);                                                            \
        if (CTC_FLAG_ISSET(pk->sub_flag, CTC_ACL_IPV4_KEY_SUB_FLAG_ARP_OP_CODE))                                                \
        {                                                                                                                       \
            SYS_ACL_DBG_DUMP("  arp_op_code %s", CTC_DEBUG_HEX_FORMAT(str, format, pk->u0.arp.op_code, 4, U));                     \
            SYS_ACL_DBG_DUMP("  arp_op_code-mask %s", CTC_DEBUG_HEX_FORMAT(str, format, pk->u0.arp.op_code_mask, 4, U));           \
        }                                                                                                                       \
        if (CTC_FLAG_ISSET(pk->sub_flag, CTC_ACL_IPV4_KEY_SUB_FLAG_ARP_SENDER_IP))                                              \
        {                                                                                                                       \
            SYS_ACL_DBG_DUMP("  sender_ip %s", CTC_DEBUG_HEX_FORMAT(str, format, pk->u0.arp.sender_ip, 8, U));                     \
            SYS_ACL_DBG_DUMP("  sender_ip-mask %s", CTC_DEBUG_HEX_FORMAT(str, format, pk->u0.arp.sender_ip_mask, 8, U));           \
        }                                                                                                                       \
        if (CTC_FLAG_ISSET(pk->sub_flag, CTC_ACL_IPV4_KEY_SUB_FLAG_ARP_TARGET_IP))                                              \
        {                                                                                                                       \
            SYS_ACL_DBG_DUMP("  target_ip %s", CTC_DEBUG_HEX_FORMAT(str, format, pk->u0.arp.target_ip, 8, U));                     \
            SYS_ACL_DBG_DUMP("  target_ip-mask %s", CTC_DEBUG_HEX_FORMAT(str, format, pk->u0.arp.target_ip_mask, 8, U));           \
        }                                                                                                                       \
        if (CTC_FLAG_ISSET(pk->sub_flag, CTC_ACL_IPV4_KEY_SUB_FLAG_ARP_PROTOCOL_TYPE))                                          \
        {                                                                                                                       \
            SYS_ACL_DBG_DUMP("  protocol_type %s", CTC_DEBUG_HEX_FORMAT(str, format, pk->u0.arp.protocol_type, 4, U));             \
            SYS_ACL_DBG_DUMP("  protocol_type-mask %s\n", CTC_DEBUG_HEX_FORMAT(str, format, pk->u0.arp.protocol_type_mask, 4, U)); \
        }                                                                                                                       \
        SYS_ACL_DBG_DUMP("\n");                                                                                                 \
    }                                                                                                                           \
    if (((pk->l3_type_mask & pk->l3_type) == (CTC_PARSER_L3_TYPE_MPLS & 0xff))                                      \
       || ((pk->l3_type_mask & pk->l3_type) == (CTC_PARSER_L3_TYPE_MPLS_MCAST & 0xff)))                             \
    {                                                                                                                           \
        SYS_ACL_DBG_DUMP("  unique_l3_type %d", pk->unique_l3_type);                                                            \
        if (CTC_FLAG_ISSET(pk->sub1_flag, CTC_ACL_IPV4_KEY_SUB1_FLAG_MPLS_LABEL_NUM))                                           \
        {                                                                                                                       \
            SYS_ACL_DBG_DUMP("  label_num %s", CTC_DEBUG_HEX_FORMAT(str, format, pk->u0.mpls.label_num, 2, U));                    \
            SYS_ACL_DBG_DUMP("  label_num-mask %s", CTC_DEBUG_HEX_FORMAT(str, format, pk->u0.mpls.label_num_mask, 2, U));           \
        }                                                                                                                       \
        if (CTC_FLAG_ISSET(pk->sub1_flag, CTC_ACL_IPV4_KEY_SUB1_FLAG_MPLS_LABEL0))                                              \
        {                                                                                                                       \
            SYS_ACL_DBG_DUMP("  mpls_label0 %s", CTC_DEBUG_HEX_FORMAT(str, format, pk->u0.mpls.mpls_label0, 2, U));                \
            SYS_ACL_DBG_DUMP("  mpls_label0-mask %s", CTC_DEBUG_HEX_FORMAT(str, format, pk->u0.mpls.mpls_label0_mask, 2, U));      \
        }                                                                                                                       \
        if (CTC_FLAG_ISSET(pk->sub1_flag, CTC_ACL_IPV4_KEY_SUB1_FLAG_MPLS_LABEL1))                                              \
        {                                                                                                                       \
            SYS_ACL_DBG_DUMP("  mpls_label1 %s", CTC_DEBUG_HEX_FORMAT(str, format, pk->u0.mpls.mpls_label1, 2, U));                \
            SYS_ACL_DBG_DUMP("  mpls_label1-mask %s", CTC_DEBUG_HEX_FORMAT(str, format, pk->u0.mpls.mpls_label1_mask, 2, U));      \
        }                                                                                                                       \
        if (CTC_FLAG_ISSET(pk->sub1_flag, CTC_ACL_IPV4_KEY_SUB1_FLAG_MPLS_LABEL2))                                              \
        {                                                                                                                       \
            SYS_ACL_DBG_DUMP("  mpls_label2 %s", CTC_DEBUG_HEX_FORMAT(str, format, pk->u0.mpls.mpls_label2, 2, U));                \
            SYS_ACL_DBG_DUMP("  mpls_label2-mask %s", CTC_DEBUG_HEX_FORMAT(str, format, pk->u0.mpls.mpls_label2_mask, 2, U));      \
        }                                                                                                                       \
        SYS_ACL_DBG_DUMP("\n");                                                                                                 \
    }                                                                                                                           \
    if ((pk->l3_type_mask & pk->l3_type) == (CTC_PARSER_L3_TYPE_NONE & pk->l3_type_mask))                                       \
    {                                                                                                                           \
        SYS_ACL_DBG_DUMP("  unique_l3_type %d", pk->unique_l3_type);                                                            \
        if (CTC_FLAG_ISSET(pk->flag, CTC_ACL_IPV4_KEY_FLAG_ETH_TYPE))                                                           \
        {                                                                                                                       \
            SYS_ACL_DBG_DUMP("  eth_type %s", CTC_DEBUG_HEX_FORMAT(str, format, pk->u0.other.eth_type, 4, U));                     \
            SYS_ACL_DBG_DUMP("  eth_type-mask %s", CTC_DEBUG_HEX_FORMAT(str, format, pk->u0.other.eth_type_mask, 4, U));           \
        }                                                                                                                       \
        SYS_ACL_DBG_DUMP("\n");                                                                                                 \
    }

#define   DUMP_IPV6_UNIQUE(pk)                                       \
    DUMP_IPV6(pk, IPV6)                                              \
    if (CTC_FLAG_ISSET(pk->flag, CTC_ACL_IPV6_KEY_FLAG_L3_TYPE))     \
    {                                                                \
        SYS_ACL_DBG_DUMP("  l3_type %u  mask %u,",                   \
                         pk->l3_type, pk->l3_type_mask);             \
    }                                                                \
    if (CTC_FLAG_ISSET(pk->flag, CTC_ACL_IPV6_KEY_FLAG_FLOW_LABEL))  \
    {                                                                \
        SYS_ACL_DBG_DUMP("  flow-label 0x%x flow-label 0x%x,",       \
                         pk->flow_label, pk->flow_label_mask);       \
    }                                                                \
    if (CTC_FLAG_ISSET(pk->flag, CTC_ACL_IPV6_KEY_FLAG_L3_TYPE)      \
        + CTC_FLAG_ISSET(pk->flag, CTC_ACL_IPV6_KEY_FLAG_FLOW_LABEL))\
    {                                                                \
        SYS_ACL_DBG_DUMP("\n");                                      \
    }

#define   DUMP_MPLS_UNIQUE(pk)                                                       \
    if (pk->flag.routed_packet)                                                      \
    {                                                                                \
        SYS_ACL_DBG_DUMP("  routed-packet %u,", pk->routed_packet);                  \
        SYS_ACL_DBG_DUMP("\n");                                                      \
    }                                                                                \
    if (pk->flag.mpls_label0)                                                        \
    {                                                                                \
        if (0 == pk->mpls_label0_mask)                                               \
        {                                                                            \
            SYS_ACL_DBG_DUMP("  mpls-label0 any,");                                  \
        }                                                                            \
        else                                                                         \
        {                                                                            \
            SYS_ACL_DBG_DUMP("  mpls-label0 0x%x  mask 0x%x",                        \
                             (pk->mpls_label0 >> 12), (pk->mpls_label0_mask >> 12)); \
        }                                                                            \
    }                                                                                \
    if (pk->flag.mpls_label1)                                                        \
    {                                                                                \
        if (0 == pk->mpls_label1_mask)                                               \
        {                                                                            \
            SYS_ACL_DBG_DUMP("  mpls-label1 any,");                                  \
        }                                                                            \
        else                                                                         \
        {                                                                            \
            SYS_ACL_DBG_DUMP("  mpls-label1 0x%x  mask 0x%x",                        \
                             (pk->mpls_label1 >> 12), (pk->mpls_label1_mask >> 12)); \
        }                                                                            \
    }                                                                                \
    if ((pk->flag.mpls_label0) + (pk->flag.mpls_label1))                             \
    {                                                                                \
        SYS_ACL_DBG_DUMP("\n");                                                      \
    }                                                                                \
    if (pk->flag.mpls_label2)                                                        \
    {                                                                                \
        if (0 == pk->mpls_label2_mask)                                               \
        {                                                                            \
            SYS_ACL_DBG_DUMP("  mpls-label2 any,");                                  \
        }                                                                            \
        else                                                                         \
        {                                                                            \
            SYS_ACL_DBG_DUMP("  mpls-label2 0x%x  mask 0x%x",                        \
                             (pk->mpls_label2 >> 12), (pk->mpls_label2_mask >> 12)); \
        }                                                                            \
    }                                                                                \
    if (pk->flag.mpls_label3)                                                        \
    {                                                                                \
        if (0 == pk->mpls_label3_mask)                                               \
        {                                                                            \
            SYS_ACL_DBG_DUMP("  mpls-label3 any,");                                  \
        }                                                                            \
        else                                                                         \
        {                                                                            \
            SYS_ACL_DBG_DUMP("  mpls-label3 0x%x  mask 0x%x",                        \
                             (pk->mpls_label3 >> 12), (pk->mpls_label3_mask >> 12)); \
        }                                                                            \
    }                                                                                \
    if ((pk->flag.mpls_label2) + (pk->flag.mpls_label3))                             \
    {                                                                                \
        SYS_ACL_DBG_DUMP("\n");                                                      \
    }                                                                                \

#define ACL_PRINT_ENTRY_ROW(eid)               SYS_ACL_DBG_DUMP("\n>>entry-id %u : \n", (eid))
#define ACL_PRINT_GROUP_ROW(gid)               SYS_ACL_DBG_DUMP("\n>>group-id %u (first in goes last, installed goes tail) :\n", (gid))
#define ACL_PRINT_PRIO_ROW(prio)               SYS_ACL_DBG_DUMP("\n>>group-prio %u (sort by block_idx ) :\n", (prio))
#define ACL_PRINT_TYPE_ROW(type)               SYS_ACL_DBG_DUMP("\n>>type %u (sort by block_idx ) :\n", (type))
#define ACL_PRINT_ALL_SORT_SEP_BY_ROW(type)    SYS_ACL_DBG_DUMP("\n>>Divide by %s. \n", (type) ? "group" : "priority")

#define ACL_PRINT_COUNT(n)                     SYS_ACL_DBG_DUMP("%-7d ", n)
#define SYS_ACL_ALL_KEY      CTC_ACL_KEY_NUM
#define FIB_KEY_TYPE_ACL     0x1F
#define SYS_ACL_GPORT13_TO_GPORT14(gport)      ((((gport >> 8) << 9)) | (gport & 0xFF))

#define ACL_MAX_LABEL_NUM    9 /* This means parser can support 9 label */

#define ACL_GET_TABLE_ENTYR_NUM(lchip, t, n)    CTC_ERROR_RETURN(sys_goldengate_ftm_query_table_entry_num(lchip, t, n))

struct drv_acl_group_info_s
{
    uint8  dir;

    uint32 class_id_data;
    uint32 class_id_mask;

    uint16 gport;      /* logic_port/ phy_port/ MetaData/ bitmap_64 */
    uint16 gport_mask;

    uint8  gport_type; /* logic_port/ phy_port/ MetaData/ bitmap_64 */
    uint8  gport_type_mask;
    uint8  bitmap_base;
    uint8  bitmap_base_mask;

    uint16 bitmap_16;

    uint32 bitmap_64[2];
};
typedef struct drv_acl_group_info_s   drv_acl_group_info_t;

typedef struct
{
    uint32             count;
    uint8              detail;
    uint8              rsv;
    ctc_acl_key_type_t key_type;
} _acl_cb_para_t;


#define ACL_OUTER_ENTRY(pe)    ((sys_acl_entry_t *) ((uint8 *) pe - sizeof(ctc_slistnode_t)))

#define ___________ACL_INNER_FUNCTION________________________
static int32
_sys_goldengate_acl_get_field_sel(uint8 lchip, uint8 key_type, uint32 field_sel_id, void* ds_sel);
int32 sys_goldengate_acl_wb_sync(uint8 lchip);
int32 sys_goldengate_acl_wb_restore(uint8 lchip);

#define __ACL_GET_FROM_SW__


STATIC uint32
_sys_goldengate_acl_sum(uint32 a, ...)
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
#if 0
STATIC uint32
_sys_goldengate_acl_max(uint32 a, ...)
{
    va_list list;
    uint32  v   = a;
    uint32  max = 0;
    va_start(list, a);
    while (v != -1)
    {
        if (v > max)
        {
            max = v;
        }
        v = va_arg(list, int32);
    }
    va_end(list);

    return max;
}
#endif
#define ACL_SUM(...)    _sys_goldengate_acl_sum(__VA_ARGS__, -1)
/*#define ACL_MAX(...)    _sys_goldengate_acl_max(__VA_ARGS__, -1)*/
/* only hash will call this fuction
 * pe must not NULL
 * pg can be NULL
 */
STATIC int32
_sys_goldengate_acl_get_nodes_by_key(uint8 lchip, sys_acl_entry_t*  pe_in,
                                     sys_acl_entry_t** pe,
                                     sys_acl_group_t** pg)
{
    sys_acl_entry_t* pe_lkup = NULL;
    sys_acl_group_t* pg_lkup = NULL;

    CTC_PTR_VALID_CHECK(pe_in);
    CTC_PTR_VALID_CHECK(pe);

    SYS_ACL_DBG_FUNC();

    pe_lkup = ctc_hash_lookup(p_gg_acl_master[lchip]->hash_ent_key, pe_in);
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



STATIC uint8
_sys_goldengate_acl_get_fpa_size(uint8 lchip, sys_acl_entry_t * pe, uint8* o_step)
{
    uint8 step = 0;
    uint8 fpa_size = 0;
    uint8 key_type;
    uint8 key_size;
    key_type = pe->key.type;
    key_size = pe->key.u0.ipv4_key.key_size;

    switch (key_type)
    {
    case CTC_ACL_KEY_PBR_IPV4:
        fpa_size = CTC_FPA_KEY_SIZE_160;
        step = 1;
        break;
    case CTC_ACL_KEY_PBR_IPV6:
        fpa_size = CTC_FPA_KEY_SIZE_320;
        step = 2;
        break;
    case CTC_ACL_KEY_MAC:
        if (0 == p_gg_acl_master[lchip]->ipv4_160bit_mode_en)
        {
            fpa_size = CTC_FPA_KEY_SIZE_160;
            step = 1;
        }
        else
        {
            fpa_size = CTC_FPA_KEY_SIZE_320;
            step = 2;
        }
        break;
    case   CTC_ACL_KEY_IPV4:
        if (CTC_ACL_KEY_SIZE_SINGLE == key_size)
        {
            fpa_size = CTC_FPA_KEY_SIZE_160;
            step = 1;
        }
        else
        {
            fpa_size = CTC_FPA_KEY_SIZE_320;
            step = 2;
        }
        break;
    case   CTC_ACL_KEY_IPV6:
        fpa_size = CTC_FPA_KEY_SIZE_640;
        step = 4;
        break;
    case CTC_ACL_KEY_HASH_MAC:
    case CTC_ACL_KEY_HASH_IPV4:
    case CTC_ACL_KEY_HASH_MPLS:
        step = 1;
        break;
    case CTC_ACL_KEY_HASH_L2_L3:
    case CTC_ACL_KEY_HASH_IPV6:
        step = 2;
        break;
    default:
        step = 1;
        break;
    }

    if(o_step)
    {
        *o_step = step;
    }

    return fpa_size;
}


/*
 * get sys entry node by entry id
 * pg, pb if be NULL, won't care.
 * pe cannot be NULL.
 */
STATIC int32
_sys_goldengate_acl_get_nodes_by_eid(uint8 lchip, uint32 eid,
                                     sys_acl_entry_t** pe,
                                     sys_acl_group_t** pg,
                                     sys_acl_block_t** pb)
{
    uint8 block_id = 0;
    sys_acl_entry_t sys_entry;
    sys_acl_entry_t * pe_lkup = NULL;
    sys_acl_group_t * pg_lkup = NULL;
    sys_acl_block_t * pb_lkup = NULL;

    CTC_PTR_VALID_CHECK(pe);
    SYS_ACL_DBG_FUNC();


    sal_memset(&sys_entry, 0, sizeof(sys_acl_entry_t));
    sys_entry.fpae.entry_id = eid;
    sys_entry.lchip = lchip;

    pe_lkup = ctc_hash_lookup(p_gg_acl_master[lchip]->entry, &sys_entry);

    if (pe_lkup)
    {
        pg_lkup = pe_lkup->group;
        SYS_ACL_CHECK_GROUP_UNEXIST(pg_lkup);

        /* get block */
        if (ACL_ENTRY_IS_TCAM(pe_lkup->key.type))
        {
            block_id = pg_lkup->group_info.block_id;
            if((CTC_ACL_KEY_PBR_IPV4 == pe_lkup->key.type) || (CTC_ACL_KEY_PBR_IPV6 == pe_lkup->key.type))
            {
                block_id = ACL_IGS_BLOCK_ID_NUM + ACL_EGS_BLOCK_ID_NUM;
            }
            else if(CTC_EGRESS == pg_lkup->group_info.dir)
            {
                block_id += ACL_IGS_BLOCK_ID_NUM;
            }
            pb_lkup  = &p_gg_acl_master[lchip]->block[block_id];
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
_sys_goldengate_acl_get_group_by_gid(uint8 lchip, uint32 gid, sys_acl_group_t** sys_group_out)
{
    sys_acl_group_t * p_sys_group_lkup = NULL;
    sys_acl_group_t sys_group;

    CTC_PTR_VALID_CHECK(sys_group_out);
    SYS_ACL_DBG_FUNC();

    sal_memset(&sys_group, 0, sizeof(sys_acl_group_t));
    sys_group.group_id = gid;

    p_sys_group_lkup = ctc_hash_lookup(p_gg_acl_master[lchip]->group, &sys_group);

    *sys_group_out = p_sys_group_lkup;

    return CTC_E_NONE;
}



#define ACL_SET_MAC(d, s)       sal_memcpy(d, s, sizeof(mac_addr_t))
#define ACL_SET_HW_MAC(d, s)    SYS_GOLDENGATE_SET_HW_MAC(d, s)
#define ACL_SET_HW_IP6(d, s)    SYS_GOLDENGATE_REVERT_IP6(d, s)



/*
 * below is build hw struct
 */
#define __ACL_OPERATE_ON_HW__

STATIC int32
_sys_goldengate_acl_vlan_tag_op_translate(uint8 lchip, uint8 op_in, uint8* op_out, uint8* mode_out)
{
    *mode_out = 0;

    switch (op_in)
    {
    case CTC_ACL_VLAN_TAG_OP_NONE:
        *op_out = 0;
        break;
    case CTC_ACL_VLAN_TAG_OP_REP_OR_ADD:
        *mode_out = 1;
        *op_out   = 1;
        break;
    case CTC_ACL_VLAN_TAG_OP_REP:
        *op_out = 1;
        break;
    case CTC_ACL_VLAN_TAG_OP_ADD:
        *op_out = 2;
        break;
    case CTC_ACL_VLAN_TAG_OP_DEL:
        *op_out = 3;
        break;
    default:
        break;
    }
    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_acl_build_vlan_edit(uint8 lchip, void* p_ds_edit, sys_acl_vlan_edit_t* p_vlan_edit)
{
    uint8 op = 0;
    uint8 mo;

    _sys_goldengate_acl_vlan_tag_op_translate(lchip, p_vlan_edit->stag_op, &op, &mo);
    SetDsAclVlanActionProfile(V, sTagAction_f, p_ds_edit, op);
    SetDsAclVlanActionProfile(V, stagModifyMode_f, p_ds_edit, mo);
    _sys_goldengate_acl_vlan_tag_op_translate(lchip, p_vlan_edit->ctag_op, &op, &mo);
    SetDsAclVlanActionProfile(V, cTagAction_f, p_ds_edit, op);
    SetDsAclVlanActionProfile(V, ctagModifyMode_f, p_ds_edit, mo);

    SetDsAclVlanActionProfile(V, sVlanIdAction_f, p_ds_edit, p_vlan_edit->svid_sl);
    SetDsAclVlanActionProfile(V, sCosAction_f, p_ds_edit, p_vlan_edit->scos_sl);
    SetDsAclVlanActionProfile(V, sCfiAction_f, p_ds_edit, p_vlan_edit->scfi_sl);

    SetDsAclVlanActionProfile(V, cVlanIdAction_f, p_ds_edit, p_vlan_edit->cvid_sl);
    SetDsAclVlanActionProfile(V, cCosAction_f, p_ds_edit, p_vlan_edit->ccos_sl);
    SetDsAclVlanActionProfile(V, cCfiAction_f, p_ds_edit, p_vlan_edit->ccfi_sl);
    SetDsAclVlanActionProfile(V, svlanTpidIndexEn_f, p_ds_edit, 1);
    SetDsAclVlanActionProfile(V, svlanTpidIndex_f, p_ds_edit, p_vlan_edit->tpid_index);
/*
   outerVlanStatus_f
   ctagAddMode_f
   svlanTpidIndexEn_f
   svlanTpidIndex_f
   ds_vlan_action_profile_t
 */
    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_acl_build_ds_action(uint8 lchip, sys_acl_action_t* p_sys_action, void* p_ds_acl, void* ds_edit)
{
    uint16 stats_ptr;
    uint64 flag = 0;

    CTC_PTR_VALID_CHECK(p_sys_action);
    CTC_PTR_VALID_CHECK(p_ds_acl);

    SYS_ACL_DBG_FUNC();

    flag = p_sys_action->flag;
/**
    u1_g2_adLengthAdjustType_f
    u1_g2_adCriticalPacket_f
    u1_g2_adNextHopExt_f
    u1_g2_adSendLocalPhyPort_f
    u1_g2_adApsBridgeEn_f
    u1_g2_adDestMap_f
    u1_g2_adNextHopPtr_f
    u1_g2_adCareLength_f
 **/
    if (CTC_FLAG_ISSET(flag, CTC_ACL_ACTION_FLAG_DISCARD))
    {
        SetDsAcl(V, nextHopPtrValid_f, p_ds_acl, 0);
        SetDsAcl(V, u1_g1_discardPacketType_f, p_ds_acl, 1);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_ACTION_FLAG_CANCEL_DISCARD))
    {
        SetDsAcl(V, nextHopPtrValid_f, p_ds_acl, 0);
        SetDsAcl(V, u1_g1_discardPacketType_f, p_ds_acl, 2);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_ACTION_FLAG_DENY_BRIDGE))
    {
        SetDsAcl(V, denyBridge_f, p_ds_acl, 1);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_ACTION_FLAG_DISABLE_IPFIX))
    {
        SetDsAcl(V, aclDisableIpfix_f, p_ds_acl, 1);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_ACTION_FLAG_DENY_IPFIX_LEARNING))
    {
        SetDsAcl(V, denyIpfixInsertOperation_f , p_ds_acl, 1);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_ACTION_FLAG_DENY_LEARNING))
    {
        SetDsAcl(V, denyLearning_f, p_ds_acl, 1);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_ACTION_FLAG_DENY_ROUTE))
    {
        SetDsAcl(V, denyRoute_f, p_ds_acl, 1);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_ACTION_FLAG_COPY_TO_CPU))
    {
        SetDsAcl(V, exceptionToCpu_f, p_ds_acl, 1);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_ACTION_FLAG_STATS))
    {
        stats_ptr = p_sys_action->chip.stats_ptr;
        SetDsAcl(V, statsPtr_f, p_ds_acl, stats_ptr);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_ACTION_FLAG_MICRO_FLOW_POLICER))
    {
        SetDsAcl(V, isAggFlowPolicer_f, p_ds_acl, 0);
        SetDsAcl(V, flowPolicerPtr_f, p_ds_acl, p_sys_action->chip.micro_policer_ptr);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_ACTION_FLAG_MACRO_FLOW_POLICER))
    {
        SetDsAcl(V, isAggFlowPolicer_f, p_ds_acl, 1);
        SetDsAcl(V, flowPolicerPtr_f, p_ds_acl, p_sys_action->chip.macro_policer_ptr);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_ACTION_FLAG_RANDOM_LOG))
    {
        SetDsAcl(V, randomThresholdShift_f, p_ds_acl, p_sys_action->random_threshold_shift);
        SetDsAcl(V, aclLogId_f, p_ds_acl, p_sys_action->acl_log_id);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_ACTION_FLAG_PRIORITY))
    {
        SetDsAcl(V, priorityValid_f, p_ds_acl, 1);
        SetDsAcl(V, _priority_f, p_ds_acl, p_sys_action->priority);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_ACTION_FLAG_COLOR))
    {
        SetDsAcl(V, color_f, p_ds_acl, p_sys_action->color);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_ACTION_FLAG_PRIORITY_AND_COLOR))
    {
        SetDsAcl(V, priorityValid_f, p_ds_acl, 1);
        SetDsAcl(V, _priority_f, p_ds_acl, p_sys_action->priority);
        SetDsAcl(V, color_f, p_ds_acl, p_sys_action->color);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_ACTION_FLAG_TRUST))
    {
        SetDsAcl(V, qosPolicy_f, p_ds_acl, p_sys_action->trust);
    }
    else
    {
        SetDsAcl(V, qosPolicy_f, p_ds_acl, SYS_ACL_INVALID_QOS_POLICY);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_ACTION_FLAG_REDIRECT))
    {
        if (!p_sys_action->merge_dsfwd)
        {
            if (p_sys_action->ecmp_group_id)
            {
                SetDsAcl(V, u1_g1_ecmpGroupId_f, p_ds_acl, p_sys_action->ecmp_group_id);
            }
            else
            {
                SetDsAcl(V, u1_g1_dsFwdPtr_f, p_ds_acl, p_sys_action->chip.fwdptr);
            }
        }
        else
        {
            uint8 dest_chipid = p_sys_action->dest_chipid;
            uint16 dest_id = p_sys_action->dest_id;
            uint8 adjust_len_idx = 0;

            SetDsAcl(V, nextHopPtrValid_f, p_ds_acl, 1);
            if(p_sys_action->is_ecmp_intf)
            {
                SetDsAcl(V, u1_g2_adDestMap_f, p_ds_acl, ((0x3d << 12) | (dest_id&0xFFF)));
            }
            else if( p_sys_action->is_mcast)
            {
               uint8   speed = 0;
               SetDsAcl(V, u1_g2_adDestMap_f, p_ds_acl, SYS_ENCODE_MCAST_IPE_DESTMAP(speed,dest_id));
           }
            else
            {
                SetDsAcl(V, u1_g2_adDestMap_f, p_ds_acl, SYS_ENCODE_DESTMAP(dest_chipid, dest_id));
            }
            SetDsAcl(V, u1_g2_adApsBridgeEn_f, p_ds_acl, p_sys_action->aps_en);
            SetDsAcl(V, u1_g2_adNextHopPtr_f, p_ds_acl, p_sys_action->dsnh_offset);
            if (p_sys_action->adjust_len)
            {
                sys_goldengate_lkup_adjust_len_index(lchip, p_sys_action->adjust_len, &adjust_len_idx);
                SetDsAcl(V, u1_g2_adLengthAdjustType_f, p_ds_acl, adjust_len_idx);
            }
            else
            {
                SetDsAcl(V, u1_g2_adLengthAdjustType_f, p_ds_acl, 0);
            }
            SetDsAcl(V, u1_g2_adCareLength_f, p_ds_acl, 1);

        }

        SetDsAcl(V, denyBridge_f, p_ds_acl, 1);
        SetDsAcl(V, denyRoute_f, p_ds_acl, 1);
        SetDsAcl(V, forceLearning_f, p_ds_acl, 1);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_ACTION_FLAG_FORCE_ROUTE))
    {
        SetDsAcl(V, forceRoute_f, p_ds_acl, 1);
        if (CTC_FLAG_ISSET(flag, CTC_ACL_ACTION_FLAG_REDIRECT))
        {
            SetDsAcl(V, denyRoute_f, p_ds_acl, 0);
        }
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_ACTION_FLAG_POSTCARD_EN))
    {
        SetDsAcl(V, postcardEn_f, p_ds_acl, 1);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_ACTION_FLAG_METADATA))
    {
        SetDsAcl(V, metadata_f, p_ds_acl, p_sys_action->metadata);
        SetDsAcl(V, metadataType_f, p_ds_acl, p_sys_action->metadata_type);
    }
    else if (CTC_FLAG_ISSET(flag, CTC_ACL_ACTION_FLAG_FID))
    {
        SetDsAcl(V, metadata_f, p_ds_acl, p_sys_action->fid);
        SetDsAcl(V, metadataType_f, p_ds_acl, p_sys_action->metadata_type);
    }
    else if (CTC_FLAG_ISSET(flag, CTC_ACL_ACTION_FLAG_VRFID))
    {
        SetDsAcl(V, metadata_f, p_ds_acl, p_sys_action->vrfid);
        SetDsAcl(V, metadataType_f, p_ds_acl, p_sys_action->metadata_type);
    }

    if(p_sys_action->packet_strip.start_packet_strip)
    {
        SetDsAcl(V, u1_g1_payloadOffsetStartPoint_f, p_ds_acl, p_sys_action->packet_strip.start_packet_strip);
        SetDsAcl(V, u1_g1_packetType_f, p_ds_acl, p_sys_action->packet_strip.packet_type);
        SetDsAcl(V, u1_g1_tunnelPayloadOffset_f, p_ds_acl, p_sys_action->packet_strip.strip_extra_len);
    }
    if (CTC_FLAG_ISSET(flag, CTC_ACL_ACTION_FLAG_REDIRECT_WITH_RAW_PKT))
    {
        SetDsAcl(V, payloadOffsetType_f, p_ds_acl, 1);
    }
/*
    payloadOffsetType_f
    forceBackEn_f
    forceBridge_f
    forceLearning_f
    forceRoute_f
 */
    if (CTC_FLAG_ISSET(flag, CTC_ACL_ACTION_FLAG_QOS_DOMAIN))
    {
        SetDsAcl(V, qosDomain_f, p_ds_acl, p_sys_action->qos_domain);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_ACTION_FLAG_DSCP))
    {
        SetDsAcl(V, dscpValid_f, p_ds_acl, 1);
        SetDsAcl(V, aclDscp_f, p_ds_acl, p_sys_action->dscp);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_ACTION_FLAG_VLAN_EDIT))
    {
        CTC_PTR_VALID_CHECK(p_sys_action->vlan_edit);

        SetDsAcl(V, svlanId_f, p_ds_acl, p_sys_action->svid);
        SetDsAcl(V, scos_f, p_ds_acl, p_sys_action->scos);
        SetDsAcl(V, scfi_f, p_ds_acl, p_sys_action->scfi);

        SetDsAcl(V, cvlanId_f, p_ds_acl, p_sys_action->cvid);
        SetDsAcl(V, ccos_f, p_ds_acl, p_sys_action->ccos);
        SetDsAcl(V, ccfi_f, p_ds_acl, p_sys_action->ccfi);

        SetDsAcl(V, vlanActionProfileId_f, p_ds_acl, p_sys_action->chip.profile_id);
        _sys_goldengate_acl_build_vlan_edit(lchip, ds_edit, p_sys_action->vlan_edit);
    }
    return CTC_E_NONE;
}





STATIC int32
_sys_goldengate_acl_build_pbr_ds_action(uint8 lchip, sys_acl_action_t* p_sys_action, void* p_ds)
{
    uint64 flag = 0;

    CTC_PTR_VALID_CHECK(p_sys_action);
    CTC_PTR_VALID_CHECK(p_ds);
    SYS_ACL_DBG_FUNC();

    flag = p_sys_action->flag;

    if (CTC_FLAG_ISSET(flag, CTC_ACL_ACTION_FLAG_PBR_TTL_CHECK))
    {
        SetDsIpDa(V, ttlCheckEn_f, p_ds, 1);
    }
    if (CTC_FLAG_ISSET(flag, CTC_ACL_ACTION_FLAG_PBR_DENY))
    {
        SetDsIpDa(V, u2_g1_denyPbr_f, p_ds, 1);
    }
    if (CTC_FLAG_ISSET(flag, CTC_ACL_ACTION_FLAG_PBR_CPU))
    {
        SetDsIpDa(V, ipDaExceptionEn_f, p_ds, 1);
        /*to CPU subindex refer SYS_L3PDU_PER_L3IF_ACTION_INDEX_RSV_IPDA_TO_CPU.
           and here only the lower 5bits are needed */
        SetDsIpDa(V, exceptionSubIndex_f, p_ds, 63 & 0x1F);
    }


#if 0 /* ecmp CTC_ACL_ACTION_FLAG_PBR_ECMP not support*/
    p_ds->equal_cost_path_num2_0 = p_sys_action->pbr_ecpn & 0x7;
    p_ds->equal_cost_path_num3_3 = (p_sys_action->pbr_ecpn >> 3) & 0x1;
#endif

    if (CTC_FLAG_ISSET(flag, CTC_ACL_ACTION_FLAG_PBR_ICMP_CHECK))
    {
        SetDsIpDa(V, rpfIfId_f, p_ds, p_sys_action->l3if_id & 0x3ff);
        SetDsIpDa(V, icmpCheckEn_f, p_ds, 1);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_ACTION_FLAG_PBR_FWD))
    {
        if(p_sys_action->ecmp_group_id)
        {
            SetDsIpDa(V, u1_g1_ecmpGroupId_f, p_ds, p_sys_action->ecmp_group_id);
        }
        else
        {
            SetDsIpDa(V, u1_g1_dsFwdPtr_f, p_ds, p_sys_action->chip.fwdptr);
        }

        SetDsIpDa(V, nextHopPtrValid_f, p_ds, 0);
    }

/*
   mcastRpfFailCpuEn_f
   ipmcStatsEn_f
   isatapCheckEn_f
   isDefaultRoute_f
   icmpErrMsgCheckEn_f
   l3IfType_f
   exception3CtlEn_f
   selfAddress_f
   ptEnable_f
   rpfCheckEn_f
   rpfCheckMode_f
   u1_g1_ecmpGroupId_f
   bidiPimGroupValid_f
   u1_g1_ipmcStatsPtrLowBits_f
   u1_g1_payloadSelect_f
   u1_g1_priorityPathEn_f
   u1_g1_payloadOffsetType_f
   u1_g1_tunnelPayloadOffset_f
   u1_g1_tunnelPacketType_f
   u1_g1_tunnelGreOption_f
   u1_g2_adLengthAdjustType_f
   u1_g2_adCriticalPacket_f
   u1_g2_adNextHopExt_f
   u1_g2_adSendLocalPhyPort_f
   u1_g2_adApsBridgeEn_f
   u1_g2_adDestMap_f
   u1_g2_adNextHopPtr_f
   u1_g2_adCareLength_f
 */

    return CTC_E_NONE;
}




/*
 * build driver group info based on sys group info
 */
STATIC int32
_sys_goldengate_acl_build_group_info(uint8 lchip, sys_acl_group_info_t* pg_sys, drv_acl_group_info_t* pg_drv)
{
    CTC_PTR_VALID_CHECK(pg_drv);
    CTC_PTR_VALID_CHECK(pg_sys);

    SYS_ACL_DBG_FUNC();

    pg_drv->dir = pg_sys->dir;

    switch (pg_sys->type)
    {
    case     CTC_ACL_GROUP_TYPE_HASH:   /* won't use it anyway. */
        break;
    case     CTC_ACL_GROUP_TYPE_PORT_BITMAP:

        /*
         *  example: user care port 00001111 (that is port0 port1 port2 port3).
         *  In hardware,  should be 00000000 - data
         *                          11110000 - mask.
         *  so port0 port1 port2 port3 will hit, other ports won't.
         */

        /* port_bitmap  31 ~0 */
        if (ACL_BITMAP_STATUS_64 == pg_sys->bitmap_status)
        {
            uint32 bitmap_64[2] = { 0 };
            if (pg_sys->un.port_bitmap[0] || pg_sys->un.port_bitmap[1])
            {
                bitmap_64[0]        = pg_sys->un.port_bitmap[0];
                bitmap_64[1]        = pg_sys->un.port_bitmap[1];
                pg_drv->bitmap_base = 0;
                pg_drv->bitmap_base_mask = 1;
            }
            else if (pg_sys->un.port_bitmap[2] || pg_sys->un.port_bitmap[3])
            {
                bitmap_64[0]        = pg_sys->un.port_bitmap[2];
                bitmap_64[1]        = pg_sys->un.port_bitmap[3];
                pg_drv->bitmap_base = 1;
                pg_drv->bitmap_base_mask = 1;
            }
            pg_drv->bitmap_64[0] = ~bitmap_64[0];
            pg_drv->bitmap_64[1] = ~bitmap_64[1];
        }
        else if (ACL_BITMAP_STATUS_16 == pg_sys->bitmap_status)
        {
            uint16 bitmap_16 = 0;
            uint8  i         = 0;

            for (i = 0; i < 4; i++)
            {
                if ((bitmap_16 = (pg_sys->un.port_bitmap[i] & 0xFFFF)))
                {
                    pg_drv->bitmap_base = 0 + (2 * i);
                    break;
                }
                else if ((bitmap_16 = (pg_sys->un.port_bitmap[i] >> 16)))
                {
                    pg_drv->bitmap_base = 1 + (2 * i);
                    break;
                }
            }

            pg_drv->bitmap_16 = ~bitmap_16;

            pg_drv->class_id_data = bitmap_16 & 0xFF;
            pg_drv->class_id_mask = pg_drv->bitmap_16 & 0xFF;
            pg_drv->gport      = (pg_drv->bitmap_base << 8) + (bitmap_16 >> 8);
            pg_drv->gport_mask = (0x7 << 8) | (pg_drv->bitmap_16 >> 8);
        }
        pg_drv->gport_type      = DRV_FLOWPORTTYPE_BITMAP;
        pg_drv->gport_type_mask = 3;
        break;

    case     CTC_ACL_GROUP_TYPE_GLOBAL:
    case     CTC_ACL_GROUP_TYPE_NONE:
        break;

    case     CTC_ACL_GROUP_TYPE_VLAN_CLASS:
        pg_drv->class_id_data = pg_sys->un.vlan_class_id;
        pg_drv->class_id_mask = 0xFF;
        break;

    case     CTC_ACL_GROUP_TYPE_PORT_CLASS:
        pg_drv->class_id_data = pg_sys->un.port_class_id;
        pg_drv->class_id_mask = 0xFF;
        break;

    case     CTC_ACL_GROUP_TYPE_SERVICE_ACL:
        pg_drv->gport      = pg_sys->un.service_id;
        pg_drv->gport_mask = pg_sys->un.service_id ? 0x3FFF : 0;

        pg_drv->gport_type      = DRV_FLOWPORTTYPE_LPORT;
        pg_drv->gport_type_mask = 3;
        break;

    case     CTC_ACL_GROUP_TYPE_PORT:
        pg_drv->gport      = SYS_MAP_CTC_GPORT_TO_DRV_GPORT(pg_sys->un.gport);
        pg_drv->gport_mask = 0x3FFF;

        pg_drv->gport_type      = DRV_FLOWPORTTYPE_GPORT;
        pg_drv->gport_type_mask = 3;
        break;

    case     CTC_ACL_GROUP_TYPE_PBR_CLASS:
        pg_drv->class_id_data = pg_sys->un.pbr_class_id;
        pg_drv->class_id_mask = 0x3F;
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}




STATIC int32
_sys_goldengate_acl_build_mac_key(uint8 lchip, drv_acl_group_info_t* p_info,
                                  sys_acl_sw_tcam_key_mac_t* p_key,
                                  void  * p_data,
                                  void       * p_mask)
{
    uint32     flag   = 0;
    hw_mac_addr_t hw_mac = { 0 };

    CTC_PTR_VALID_CHECK(p_info);
    CTC_PTR_VALID_CHECK(p_data);
    CTC_PTR_VALID_CHECK(p_mask);
    CTC_PTR_VALID_CHECK(p_key);

    SYS_ACL_DBG_FUNC();

    flag = p_key->flag;

    if (CTC_FLAG_ISSET(flag, CTC_ACL_MAC_KEY_FLAG_SVLAN))
    {
        SetDsAclQosMacKey160(V, svlanId_f, p_data, p_key->svlan);
        SetDsAclQosMacKey160(V, svlanId_f, p_mask, p_key->svlan_mask);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_MAC_KEY_FLAG_STAG_COS))
    {
        SetDsAclQosMacKey160(V, stagCos_f, p_data, p_key->stag_cos);
        SetDsAclQosMacKey160(V, stagCos_f, p_mask, p_key->stag_cos_mask);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_MAC_KEY_FLAG_STAG_CFI))
    {
        SetDsAclQosMacKey160(V, stagCfi_f, p_data, p_key->stag_cfi);
        SetDsAclQosMacKey160(V, stagCfi_f, p_mask, 1);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_MAC_KEY_FLAG_CVLAN))
    {
        SetDsAclQosMacKey160(V, svlanId_f, p_data, p_key->cvlan);
        SetDsAclQosMacKey160(V, svlanId_f, p_mask, p_key->cvlan_mask);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_MAC_KEY_FLAG_CTAG_COS))
    {
        SetDsAclQosMacKey160(V, stagCos_f, p_data, p_key->ctag_cos);
        SetDsAclQosMacKey160(V, stagCos_f, p_mask, p_key->ctag_cos_mask);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_MAC_KEY_FLAG_CTAG_CFI))
    {
        SetDsAclQosMacKey160(V, stagCfi_f, p_data, p_key->ctag_cfi);
        SetDsAclQosMacKey160(V, stagCfi_f, p_mask, 1);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_MAC_KEY_FLAG_STAG_VALID))
    {
        SetDsAclQosMacKey160(V, svlanIdValid_f, p_data, p_key->stag_valid);
        SetDsAclQosMacKey160(V, svlanIdValid_f, p_mask, 1);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_MAC_KEY_FLAG_CTAG_VALID))
    {
        SetDsAclQosMacKey160(V, svlanIdValid_f, p_data, p_key->ctag_valid);
        SetDsAclQosMacKey160(V, svlanIdValid_f, p_mask, 1);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_MAC_KEY_FLAG_VLAN_NUM))
    {
        SetDsAclQosMacKey160(V, layer2Type_f, p_data, p_key->vlan_num);
        SetDsAclQosMacKey160(V, layer2Type_f, p_mask, p_key->vlan_num_mask);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_MAC_KEY_FLAG_MAC_SA))
    {
        ACL_SET_HW_MAC(hw_mac, p_key->mac_sa);
        SetDsAclQosMacKey160(A, macSa_f, p_data, hw_mac);
        ACL_SET_HW_MAC(hw_mac, p_key->mac_sa_mask);
        SetDsAclQosMacKey160(A, macSa_f, p_mask, hw_mac);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_MAC_KEY_FLAG_MAC_DA))
    {
        ACL_SET_HW_MAC(hw_mac, p_key->mac_da);
        SetDsAclQosMacKey160(A, macDa_f, p_data, hw_mac);
        ACL_SET_HW_MAC(hw_mac, p_key->mac_da_mask);
        SetDsAclQosMacKey160(A, macDa_f, p_mask, hw_mac);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_MAC_KEY_FLAG_ETH_TYPE))
    {
        SetDsAclQosMacKey160(V, etherType_f, p_data, p_key->eth_type);
        SetDsAclQosMacKey160(V, etherType_f, p_mask, p_key->eth_type_mask);
    }


    if (p_key->port.valid)
    {
        SetDsAclQosMacKey160(V, aclLabel_f, p_data, p_key->port.class_id_data);
        SetDsAclQosMacKey160(V, aclLabel_f, p_mask, p_key->port.class_id_mask);
    }
    else
    {
        SetDsAclQosMacKey160(V, aclLabel_f, p_data, p_info->class_id_data);
        SetDsAclQosMacKey160(V, aclLabel_f, p_mask, p_info->class_id_mask);
    }

    SetDsAclQosMacKey160(V, isVlanKey_f , p_data, 0);
    SetDsAclQosMacKey160(V, isVlanKey_f , p_mask, 1);

    SetDsAclQosMacKey160(V, aclQosKeyType_f, p_data, DRV_TCAMKEYTYPE_MACKEY160);
    SetDsAclQosMacKey160(V, aclQosKeyType_f, p_mask, 0x3);

    SetDsAclQosMacKey160(V, sclKeyType_f, p_data, 0);
    SetDsAclQosMacKey160(V, sclKeyType_f, p_mask, 0);

    if (CTC_FLAG_ISSET(flag, CTC_ACL_MAC_KEY_FLAG_METADATA))
    {
        SetDsAclQosMacKey160(V, globalPort_f, p_data, p_key->metadata);
        SetDsAclQosMacKey160(V, globalPort_f, p_mask, p_key->metadata_mask);
        SetDsAclQosMacKey160(V, globalPortType_f, p_data, DRV_FLOWPORTTYPE_METADATA);
        SetDsAclQosMacKey160(V, globalPortType_f, p_mask, 0x3);
    }
    else
    {
        if (p_key->port.valid)
        {
            SetDsAclQosMacKey160(V, globalPort_f, p_data, p_key->port.gport);
            SetDsAclQosMacKey160(V, globalPort_f, p_mask, p_key->port.gport_mask);
            SetDsAclQosMacKey160(V, globalPortType_f, p_data, p_key->port.gport_type);
            SetDsAclQosMacKey160(V, globalPortType_f, p_mask, p_key->port.gport_type_mask);
        }
        else
        {
            SetDsAclQosMacKey160(V, globalPort_f, p_data, p_info->gport);
            SetDsAclQosMacKey160(V, globalPort_f, p_mask, p_info->gport_mask);
            SetDsAclQosMacKey160(V, globalPortType_f, p_data, p_info->gport_type);
            SetDsAclQosMacKey160(V, globalPortType_f, p_mask, p_info->gport_type_mask);
        }
    }



    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_acl_build_mac_key_double(uint8 lchip, drv_acl_group_info_t* p_info,
                                  sys_acl_sw_tcam_key_mac_t* p_key,
                                  void  * p_data,
                                  void       * p_mask)
{
    uint32     flag   = 0;
    hw_mac_addr_t hw_mac = { 0 };

    CTC_PTR_VALID_CHECK(p_info);
    CTC_PTR_VALID_CHECK(p_data);
    CTC_PTR_VALID_CHECK(p_mask);
    CTC_PTR_VALID_CHECK(p_key);

    SYS_ACL_DBG_FUNC();

    flag = p_key->flag;

    if (CTC_FLAG_ISSET(flag, CTC_ACL_MAC_KEY_FLAG_SVLAN))
    {
        SetDsAclQosL3Key320(V, svlanId_f, p_data, p_key->svlan);
        SetDsAclQosL3Key320(V, svlanId_f, p_mask, p_key->svlan_mask);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_MAC_KEY_FLAG_STAG_COS))
    {
        SetDsAclQosL3Key320(V, stagCos_f, p_data, p_key->stag_cos);
        SetDsAclQosL3Key320(V, stagCos_f, p_mask, p_key->stag_cos_mask);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_MAC_KEY_FLAG_STAG_CFI))
    {
        SetDsAclQosL3Key320(V, stagCfi_f, p_data, p_key->stag_cfi);
        SetDsAclQosL3Key320(V, stagCfi_f, p_mask, 1);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_MAC_KEY_FLAG_CVLAN))
    {
        SetDsAclQosL3Key320(V, cvlanId_f, p_data, p_key->cvlan);
        SetDsAclQosL3Key320(V, cvlanId_f, p_mask, p_key->cvlan_mask);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_MAC_KEY_FLAG_CTAG_COS))
    {
        SetDsAclQosL3Key320(V, ctagCos_f, p_data, p_key->ctag_cos);
        SetDsAclQosL3Key320(V, ctagCos_f, p_mask, p_key->ctag_cos_mask);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_MAC_KEY_FLAG_CTAG_CFI))
    {
        SetDsAclQosL3Key320(V, ctagCfi_f, p_data, p_key->ctag_cfi);
        SetDsAclQosL3Key320(V, ctagCfi_f, p_mask, 1);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_MAC_KEY_FLAG_STAG_VALID))
    {
        SetDsAclQosL3Key320(V, svlanIdValid_f, p_data, p_key->stag_valid);
        SetDsAclQosL3Key320(V, svlanIdValid_f, p_mask, 1);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_MAC_KEY_FLAG_CTAG_VALID))
    {
        SetDsAclQosL3Key320(V, cvlanIdValid_f, p_data, p_key->ctag_valid);
        SetDsAclQosL3Key320(V, cvlanIdValid_f, p_mask, 1);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_MAC_KEY_FLAG_VLAN_NUM))
    {
        SetDsAclQosL3Key320(V, layer2Type_f, p_data, p_key->vlan_num);
        SetDsAclQosL3Key320(V, layer2Type_f, p_mask, p_key->vlan_num_mask);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_MAC_KEY_FLAG_MAC_SA))
    {
        ACL_SET_HW_MAC(hw_mac, p_key->mac_sa);
        SetDsAclQosL3Key320(A, macSa_f, p_data, hw_mac);
        ACL_SET_HW_MAC(hw_mac, p_key->mac_sa_mask);
        SetDsAclQosL3Key320(A, macSa_f, p_mask, hw_mac);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_MAC_KEY_FLAG_MAC_DA))
    {
        ACL_SET_HW_MAC(hw_mac, p_key->mac_da);
        SetDsAclQosL3Key320(A, macDa_f, p_data, hw_mac);
        ACL_SET_HW_MAC(hw_mac, p_key->mac_da_mask);
        SetDsAclQosL3Key320(A, macDa_f, p_mask, hw_mac);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_MAC_KEY_FLAG_ETH_TYPE))
    {
        SetDsAclQosL3Key320(V, u1_g10_etherType_f, p_data, p_key->eth_type);
        SetDsAclQosL3Key320(V, u1_g10_etherType_f, p_mask, p_key->eth_type_mask);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_MAC_KEY_FLAG_L3_TYPE))
    {
        SetDsAclQosL3Key320(V, layer3Type_f, p_data, p_key->l3_type);
        SetDsAclQosL3Key320(V, layer3Type_f, p_mask, p_key->l3_type_mask);
    }

    if (p_key->port.valid)
    {
        SetDsAclQosL3Key320(V, aclLabel_f, p_data, p_key->port.class_id_data);
        SetDsAclQosL3Key320(V, aclLabel_f, p_mask, p_key->port.class_id_mask);
    }
    else
    {
        SetDsAclQosL3Key320(V, aclLabel_f, p_data, p_info->class_id_data);
        SetDsAclQosL3Key320(V, aclLabel_f, p_mask, p_info->class_id_mask);
    }

    SetDsAclQosL3Key320(V, aclQosKeyType0_f, p_data, DRV_TCAMKEYTYPE_MACL3KEY320);
    SetDsAclQosL3Key320(V, aclQosKeyType0_f, p_mask, 0x3);

    SetDsAclQosL3Key320(V, aclQosKeyType1_f, p_data, DRV_TCAMKEYTYPE_MACL3KEY320);
    SetDsAclQosL3Key320(V, aclQosKeyType1_f, p_mask, 0x3);

    if (p_key->port.valid)
    {
        SetDsAclQosL3Key320(V, globalPortType_f, p_data, p_key->port.gport_type);
        SetDsAclQosL3Key320(V, globalPortType_f, p_mask, p_key->port.gport_type_mask);

        SetDsAclQosL3Key320(V, globalPort_f, p_data, p_key->port.gport);
        SetDsAclQosL3Key320(V, globalPort_f, p_mask, p_key->port.gport_mask);
    }
    else
    {
        SetDsAclQosL3Key320(V, globalPortType_f, p_data, p_info->gport_type);
        SetDsAclQosL3Key320(V, globalPortType_f, p_mask, p_info->gport_type_mask);

        SetDsAclQosL3Key320(V, globalPort_f, p_data, p_info->gport);
        SetDsAclQosL3Key320(V, globalPort_f, p_mask, p_info->gport_mask);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_MAC_KEY_FLAG_METADATA))
    {
        SetDsAclQosL3Key320(V, udf_f, p_data, p_key->metadata);
        SetDsAclQosL3Key320(V, udf_f, p_mask, p_key->metadata_mask);

        SetDsAclQosL3Key320(V, sclKeyType_f, p_data, DRV_UDFTYPE_METADATA);
        SetDsAclQosL3Key320(V, sclKeyType_f, p_mask, 0x3);
    }
    else
    {
        SetDsAclQosL3Key320(V, sclKeyType_f, p_data, 0);
        SetDsAclQosL3Key320(V, sclKeyType_f, p_mask, 0);
    }


    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_acl_build_ipv4_key_single(uint8 lchip, drv_acl_group_info_t* p_info,
                                          sys_acl_sw_tcam_key_ipv4_t* p_key,
                                          void * p_data,
                                          void   * p_mask)
{
    uint32 flag;
    uint32 sub_flag;
    uint32 sub1_flag;
    /*uint32 sub2_flag;*/
    uint32 sub3_flag;

    CTC_PTR_VALID_CHECK(p_info);
    CTC_PTR_VALID_CHECK(p_data);
    CTC_PTR_VALID_CHECK(p_mask);
    CTC_PTR_VALID_CHECK(p_key);

    SYS_ACL_DBG_FUNC();

    flag      = p_key->flag;
    sub_flag  = p_key->sub_flag;
    sub1_flag = p_key->sub1_flag;
    /*sub2_flag = p_key->sub2_flag;*/
    sub3_flag = p_key->sub3_flag;

    if (p_key->port.valid)
    {
        SetDsAclQosL3Key160(V, aclLabel_f, p_data, p_key->port.class_id_data);
        SetDsAclQosL3Key160(V, aclLabel_f, p_mask, p_key->port.class_id_mask);
    }
    else
    {
        SetDsAclQosL3Key160(V, aclLabel_f, p_data, p_info->class_id_data);
        SetDsAclQosL3Key160(V, aclLabel_f, p_mask, p_info->class_id_mask);
    }

    SetDsAclQosL3Key160(V, aclQosKeyType_f, p_data, DRV_TCAMKEYTYPE_L3KEY160);
    SetDsAclQosL3Key160(V, aclQosKeyType_f, p_mask, 0x3);

    /* 1 bit is ip-header-error, another no use */
    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_IP_HDR_ERROR))
    {
        SetDsAclQosL3Key160(V, sclKeyType_f, p_data, p_key->ip_header_error);
        SetDsAclQosL3Key160(V, sclKeyType_f, p_mask, 1);
    }

    /*metadata */
    if(CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_METADATA))
    {
        SetDsAclQosL3Key160(V, globalPort_f, p_data, p_key->metadata);
        SetDsAclQosL3Key160(V, globalPort_f, p_mask, p_key->metadata_mask);
        SetDsAclQosL3Key160(V, globalPortType_f, p_data, DRV_FLOWPORTTYPE_METADATA);
        SetDsAclQosL3Key160(V, globalPortType_f, p_mask, 0x3);
    }
    else
    {
        if (p_key->port.valid)
        {
            SetDsAclQosL3Key160(V, globalPort_f, p_data, p_key->port.gport);
            SetDsAclQosL3Key160(V, globalPort_f, p_mask, p_key->port.gport_mask);
            SetDsAclQosL3Key160(V, globalPortType_f, p_data, p_key->port.gport_type);
            SetDsAclQosL3Key160(V, globalPortType_f, p_mask, p_key->port.gport_type_mask);
        }
        else
        {
            SetDsAclQosL3Key160(V, globalPort_f, p_data, p_info->gport);
            SetDsAclQosL3Key160(V, globalPort_f, p_mask, p_info->gport_mask);
            SetDsAclQosL3Key160(V, globalPortType_f, p_data, p_info->gport_type);
            SetDsAclQosL3Key160(V, globalPortType_f, p_mask, p_info->gport_type_mask);
        }
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_VLAN_NUM))
    {
        SetDsAclQosL3Key160(V, layer2Type_f, p_data, p_key->vlan_num);
        SetDsAclQosL3Key160(V, layer2Type_f, p_mask, p_key->vlan_num_mask);
    }
    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_L3_TYPE))
    {
        SetDsAclQosL3Key160(V, layer3Type_f, p_data, p_key->l3_type);
        SetDsAclQosL3Key160(V, layer3Type_f, p_mask, p_key->l3_type_mask);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_ROUTED_PACKET))
    {
        SetDsAclQosL3Key160(V, routedPacket_f, p_data, p_key->routed_packet);
        SetDsAclQosL3Key160(V, routedPacket_f, p_mask, 1);
    }

    switch (p_key->unique_l3_type)
    {
    case CTC_PARSER_L3_TYPE_IPV4:
        if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_IP_DA))
        {
            SetDsAclQosL3Key160(V, u1_g1_ipDa_f, p_data, p_key->u0.ip.ip_da);
            SetDsAclQosL3Key160(V, u1_g1_ipDa_f, p_mask, p_key->u0.ip.ip_da_mask);
        }

        if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_IP_SA))
        {
            SetDsAclQosL3Key160(V, u1_g1_ipSa_f, p_data, p_key->u0.ip.ip_sa);
            SetDsAclQosL3Key160(V, u1_g1_ipSa_f, p_mask, p_key->u0.ip.ip_sa_mask);
        }


        if ((CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_DSCP))
            || (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_PRECEDENCE)))
        {
            SetDsAclQosL3Key160(V, u1_g1_dscp_f, p_data, p_key->u0.ip.dscp);
            SetDsAclQosL3Key160(V, u1_g1_dscp_f, p_mask, p_key->u0.ip.dscp_mask);
        }

        if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_IP_FRAG))
        {
            SetDsAclQosL3Key160(V, u1_g1_fragInfo_f, p_data, p_key->u0.ip.frag_info);
            SetDsAclQosL3Key160(V, u1_g1_fragInfo_f, p_mask, p_key->u0.ip.frag_info_mask);
        }

        if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_IP_OPTION))
        {
            SetDsAclQosL3Key160(V, u1_g1_ipOptions_f, p_data, p_key->u0.ip.ip_option);
            SetDsAclQosL3Key160(V, u1_g1_ipOptions_f, p_mask, 1);
        }

        /*ipv4key.routed_packet always = 0*/
        if (p_key->u0.ip.l4.flag_l4_type)
        {
            SetDsAclQosL3Key160(V, u1_g1_layer4Type_f, p_data, p_key->u0.ip.l4.l4_type);
            SetDsAclQosL3Key160(V, u1_g1_layer4Type_f, p_mask, p_key->u0.ip.l4.l4_type_mask);
        }

        if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_ECN))
        {
            SetDsAclQosL3Key160(V, u1_g1_ecn_f, p_data, p_key->u0.ip.ecn);
            SetDsAclQosL3Key160(V, u1_g1_ecn_f, p_mask, p_key->u0.ip.ecn_mask);
        }

        if (p_key->u0.ip.l4.flag_l4info_mapped)
        {
            SetDsAclQosL3Key160(V, u1_g1_l4InfoMapped_f, p_data, p_key->u0.ip.l4.l4info_mapped);
            SetDsAclQosL3Key160(V, u1_g1_l4InfoMapped_f, p_mask, p_key->u0.ip.l4.l4info_mapped_mask);
        }

        if ((CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV4_KEY_SUB_FLAG_L4_SRC_PORT)) ||
            (CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV4_KEY_SUB_FLAG_ICMP_TYPE)) ||
            (CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV4_KEY_SUB_FLAG_ICMP_CODE)) ||
            (CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV4_KEY_SUB_FLAG_IGMP_TYPE)) ||
            (CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV4_KEY_SUB_FLAG_GRE_KEY)) ||
            (CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV4_KEY_SUB_FLAG_NVGRE_KEY)) ||
            (CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV4_KEY_SUB_FLAG_VNI)))
        {
            SetDsAclQosL3Key160(V, u1_g1_l4SourcePort_f, p_data, p_key->u0.ip.l4.l4_src_port);
            SetDsAclQosL3Key160(V, u1_g1_l4SourcePort_f, p_mask, p_key->u0.ip.l4.l4_src_port_mask);
        }

        if (CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV4_KEY_SUB_FLAG_L4_DST_PORT) ||
            (CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV4_KEY_SUB_FLAG_GRE_KEY)) ||
            (CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV4_KEY_SUB_FLAG_NVGRE_KEY)) ||
            (CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV4_KEY_SUB_FLAG_VNI)))
        {
            SetDsAclQosL3Key160(V, u1_g1_l4DestPort_f, p_data, p_key->u0.ip.l4.l4_dst_port);
            SetDsAclQosL3Key160(V, u1_g1_l4DestPort_f, p_mask, p_key->u0.ip.l4.l4_dst_port_mask);
        }
        break;
    case CTC_PARSER_L3_TYPE_ARP:
        if (CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV4_KEY_SUB_FLAG_ARP_OP_CODE))
        {
            SetDsAclQosL3Key160(V, u1_g2_arpOpCode_f, p_data, p_key->u0.arp.op_code);
            SetDsAclQosL3Key160(V, u1_g2_arpOpCode_f, p_mask, p_key->u0.arp.op_code_mask);
        }
        if (CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV4_KEY_SUB_FLAG_ARP_PROTOCOL_TYPE))
        {
            SetDsAclQosL3Key160(V, u1_g2_protocolType_f, p_data, p_key->u0.arp.protocol_type);
            SetDsAclQosL3Key160(V, u1_g2_protocolType_f, p_mask, p_key->u0.arp.protocol_type_mask);
        }
        if (CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV4_KEY_SUB_FLAG_ARP_TARGET_IP))
        {
            SetDsAclQosL3Key160(V, u1_g2_targetIp_f, p_data, p_key->u0.arp.target_ip);
            SetDsAclQosL3Key160(V, u1_g2_targetIp_f, p_mask, p_key->u0.arp.target_ip_mask);
        }
        if (CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV4_KEY_SUB_FLAG_ARP_SENDER_IP))
        {
            SetDsAclQosL3Key160(V, u1_g2_senderIp_f, p_data, p_key->u0.arp.sender_ip);
            SetDsAclQosL3Key160(V, u1_g2_senderIp_f, p_mask, p_key->u0.arp.sender_ip_mask);
        }
        break;
    case CTC_PARSER_L3_TYPE_MPLS:
    case CTC_PARSER_L3_TYPE_MPLS_MCAST:
        if (CTC_FLAG_ISSET(sub1_flag, CTC_ACL_IPV4_KEY_SUB1_FLAG_MPLS_LABEL_NUM))
        {
            SetDsAclQosL3Key160(V, u1_g3_labelNum_f, p_data, p_key->u0.mpls.label_num);
            SetDsAclQosL3Key160(V, u1_g3_labelNum_f, p_mask, p_key->u0.mpls.label_num_mask);
        }

        if (CTC_FLAG_ISSET(sub1_flag, CTC_ACL_IPV4_KEY_SUB1_FLAG_MPLS_LABEL0))
        {
            SetDsAclQosL3Key160(V, u1_g3_mplsLabel0_f, p_data, p_key->u0.mpls.mpls_label0);
            SetDsAclQosL3Key160(V, u1_g3_mplsLabel0_f, p_mask, p_key->u0.mpls.mpls_label0_mask);
        }

        if (CTC_FLAG_ISSET(sub1_flag, CTC_ACL_IPV4_KEY_SUB1_FLAG_MPLS_LABEL1))
        {
            SetDsAclQosL3Key160(V, u1_g3_mplsLabel1_f, p_data, p_key->u0.mpls.mpls_label1);
            SetDsAclQosL3Key160(V, u1_g3_mplsLabel1_f, p_mask, p_key->u0.mpls.mpls_label1_mask);
        }

        if (CTC_FLAG_ISSET(sub1_flag, CTC_ACL_IPV4_KEY_SUB1_FLAG_MPLS_LABEL2))
        {
            SetDsAclQosL3Key160(V, u1_g3_mplsLabel2_f, p_data, p_key->u0.mpls.mpls_label2);
            SetDsAclQosL3Key160(V, u1_g3_mplsLabel2_f, p_mask, p_key->u0.mpls.mpls_label2_mask);
        }

        break;
    case CTC_PARSER_L3_TYPE_NONE:
        if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_ETH_TYPE))
        {
            SetDsAclQosL3Key160(V, u1_g10_etherType_f, p_data, p_key->u0.other.eth_type);
            SetDsAclQosL3Key160(V, u1_g10_etherType_f, p_mask, p_key->u0.other.eth_type_mask);
        }
        break;

    case CTC_PARSER_L3_TYPE_ETHER_OAM:
        if (CTC_FLAG_ISSET(sub3_flag, CTC_ACL_IPV4_KEY_SUB3_FLAG_ETHOAM_LEVEL))
        {
            SetDsAclQosL3Key160(V, u1_g7_etherOamLevel_f, p_data, p_key->u0.ethoam.ethoam_level);
            SetDsAclQosL3Key160(V, u1_g7_etherOamLevel_f, p_mask, p_key->u0.ethoam.ethoam_level_mask);
        }

        if (CTC_FLAG_ISSET(sub3_flag, CTC_ACL_IPV4_KEY_SUB3_FLAG_ETHOAM_OPCODE))
        {
            SetDsAclQosL3Key160(V, u1_g7_etherOamOpCode_f, p_data, p_key->u0.ethoam.ethoam_op_code);
            SetDsAclQosL3Key160(V, u1_g7_etherOamOpCode_f, p_mask, p_key->u0.ethoam.ethoam_op_code_mask);
        }

        #if 0
        if (CTC_FLAG_ISSET(sub3_flag, CTC_ACL_IPV4_KEY_SUB3_FLAG_ETHOAM_VERSION))
        {
            SetDsAclQosL3Key160(V, u1_g7_etherOamVersion_f, p_data, p_key->u0.ethoam.ethoam_version);
            SetDsAclQosL3Key160(V, u1_g7_etherOamVersion_f, p_mask, p_key->u0.ethoam.ethoam_version_mask);
        }

        if (CTC_FLAG_ISSET(sub3_flag, CTC_ACL_IPV4_KEY_SUB3_FLAG_ETHOAM_IS_Y1731))
        {
            SetDsAclQosL3Key160(V, u1_g7_isY1731Oam_f, p_data, p_key->u0.ethoam.ethoam_is_y1731);
            SetDsAclQosL3Key160(V, u1_g7_isY1731Oam_f, p_mask, p_key->u0.ethoam.ethoam_is_y1731_mask);
        }
        #endif

        break;

    default:
        break;

#if 0
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

    return CTC_E_NONE;
}



STATIC int32
_sys_goldengate_acl_build_ipv4_key_double(uint8 lchip, drv_acl_group_info_t* p_info,
                                          sys_acl_sw_tcam_key_ipv4_t* p_key,
                                          void* p_data,
                                          void* p_mask)
{
    uint32     flag;
    uint32     sub_flag;
    uint32     sub1_flag;
    /*uint32     sub2_flag;*/
    uint32     sub3_flag;
    hw_mac_addr_t hw_mac = { 0 };

    CTC_PTR_VALID_CHECK(p_info);
    CTC_PTR_VALID_CHECK(p_data);
    CTC_PTR_VALID_CHECK(p_mask);
    CTC_PTR_VALID_CHECK(p_key);

    SYS_ACL_DBG_FUNC();

    flag      = p_key->flag;
    sub_flag  = p_key->sub_flag;
    sub1_flag = p_key->sub1_flag;
    /*sub2_flag = p_key->sub2_flag;*/
    sub3_flag = p_key->sub3_flag;

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_MAC_DA))
    {
        ACL_SET_HW_MAC(hw_mac, p_key->mac_da);
        SetDsAclQosL3Key320(A, macDa_f, p_data, hw_mac);

        ACL_SET_HW_MAC(hw_mac, p_key->mac_da_mask);
        SetDsAclQosL3Key320(A, macDa_f, p_mask, hw_mac);
    }
    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_MAC_SA))
    {
        ACL_SET_HW_MAC(hw_mac, p_key->mac_sa);
        SetDsAclQosL3Key320(A, macSa_f, p_data, hw_mac);

        ACL_SET_HW_MAC(hw_mac, p_key->mac_sa_mask);
        SetDsAclQosL3Key320(A, macSa_f, p_mask, hw_mac);
    }
    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_CVLAN))
    {
        SetDsAclQosL3Key320(V, cvlanId_f, p_data, p_key->cvlan);
        SetDsAclQosL3Key320(V, cvlanId_f, p_mask, p_key->cvlan_mask);
    }
    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_CTAG_COS))
    {
        SetDsAclQosL3Key320(V, ctagCos_f, p_data, p_key->ctag_cos);
        SetDsAclQosL3Key320(V, ctagCos_f, p_mask, p_key->ctag_cos_mask);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_ROUTED_PACKET))
    {
        SetDsAclQosL3Key320(V, ctagCfi_f, p_data, p_key->routed_packet);
        SetDsAclQosL3Key320(V, ctagCfi_f, p_mask, 1);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_SVLAN))
    {
        SetDsAclQosL3Key320(V, svlanId_f, p_data, p_key->svlan);
        SetDsAclQosL3Key320(V, svlanId_f, p_mask, p_key->svlan_mask);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_STAG_COS))
    {
        SetDsAclQosL3Key320(V, stagCos_f, p_data, p_key->stag_cos);
        SetDsAclQosL3Key320(V, stagCos_f, p_mask, p_key->stag_cos_mask);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_STAG_CFI))
    {
        SetDsAclQosL3Key320(V, stagCfi_f, p_data, p_key->stag_cfi);
        SetDsAclQosL3Key320(V, stagCfi_f, p_mask, 1);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_STAG_VALID))
    {
        SetDsAclQosL3Key320(V, svlanIdValid_f, p_data, p_key->stag_valid);
        SetDsAclQosL3Key320(V, svlanIdValid_f, p_mask, 1);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_CTAG_VALID))
    {
        SetDsAclQosL3Key320(V, cvlanIdValid_f, p_data, p_key->ctag_valid);
        SetDsAclQosL3Key320(V, cvlanIdValid_f, p_mask, 1);
    }

    if (p_key->port.valid)
    {
        SetDsAclQosL3Key320(V, globalPort_f, p_data, p_key->port.gport);
        SetDsAclQosL3Key320(V, globalPort_f, p_mask, p_key->port.gport_mask);

        SetDsAclQosL3Key320(V, aclLabel_f, p_data, p_key->port.class_id_data);
        SetDsAclQosL3Key320(V, aclLabel_f, p_mask, p_key->port.class_id_mask);

        SetDsAclQosL3Key320(V, globalPortType_f, p_data, p_key->port.gport_type);
        SetDsAclQosL3Key320(V, globalPortType_f, p_mask, p_key->port.gport_type_mask);
    }
    else
    {
        SetDsAclQosL3Key320(V, globalPort_f, p_data, p_info->gport);
        SetDsAclQosL3Key320(V, globalPort_f, p_mask, p_info->gport_mask);

        SetDsAclQosL3Key320(V, aclLabel_f, p_data, p_info->class_id_data);
        SetDsAclQosL3Key320(V, aclLabel_f, p_mask, p_info->class_id_mask);

        SetDsAclQosL3Key320(V, globalPortType_f, p_data, p_info->gport_type);
        SetDsAclQosL3Key320(V, globalPortType_f, p_mask, p_info->gport_type_mask);
    }

    SetDsAclQosL3Key320(V, aclQosKeyType0_f, p_data, DRV_TCAMKEYTYPE_MACL3KEY320);
    SetDsAclQosL3Key320(V, aclQosKeyType0_f, p_mask, 0x3);

    SetDsAclQosL3Key320(V, aclQosKeyType1_f, p_data, DRV_TCAMKEYTYPE_MACL3KEY320);
    SetDsAclQosL3Key320(V, aclQosKeyType1_f, p_mask, 0x3);

    /* metadata or udf*/
    if(CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_METADATA))
    {
        SetDsAclQosL3Key320(V, udf_f, p_data, p_key->metadata);
        SetDsAclQosL3Key320(V, udf_f, p_mask, p_key->metadata_mask);

        SetDsAclQosL3Key320(V, sclKeyType_f, p_data, DRV_UDFTYPE_METADATA);
        SetDsAclQosL3Key320(V, sclKeyType_f, p_mask, 0x3);
    }
    else if(CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_UDF))
    {
        SetDsAclQosL3Key320(V, udf_f, p_data, p_key->udf);
        SetDsAclQosL3Key320(V, udf_f, p_mask, p_key->udf_mask);
        SetDsAclQosL3Key320(V, sclKeyType_f, p_data, p_key->udf_type);
        SetDsAclQosL3Key320(V, sclKeyType_f, p_mask, 0x3);
    }
    else
    {
        SetDsAclQosL3Key320(V, sclKeyType_f, p_data, 0);
        SetDsAclQosL3Key320(V, sclKeyType_f, p_mask, 0);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_VLAN_NUM))
    {
        SetDsAclQosL3Key320(V, layer2Type_f, p_data, p_key->vlan_num);
        SetDsAclQosL3Key320(V, layer2Type_f, p_mask, p_key->vlan_num_mask);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_L3_TYPE))
    {
        SetDsAclQosL3Key320(V, layer3Type_f, p_data, p_key->l3_type);
        SetDsAclQosL3Key320(V, layer3Type_f, p_mask, p_key->l3_type_mask);
    }
    switch (p_key->unique_l3_type)
    {
    case CTC_PARSER_L3_TYPE_IPV4:
        if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_IP_DA))
        {
            SetDsAclQosL3Key320(V, u1_g1_ipDa_f, p_data, p_key->u0.ip.ip_da);
            SetDsAclQosL3Key320(V, u1_g1_ipDa_f, p_mask, p_key->u0.ip.ip_da_mask);
        }

        if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_IP_SA))
        {
            SetDsAclQosL3Key320(V, u1_g1_ipSa_f, p_data, p_key->u0.ip.ip_sa);
            SetDsAclQosL3Key320(V, u1_g1_ipSa_f, p_mask, p_key->u0.ip.ip_sa_mask);
        }


        if ((CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_DSCP))
            || (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_PRECEDENCE)))
        {
            SetDsAclQosL3Key320(V, u1_g1_dscp_f, p_data, p_key->u0.ip.dscp);
            SetDsAclQosL3Key320(V, u1_g1_dscp_f, p_mask, p_key->u0.ip.dscp_mask);
        }

        if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_IP_FRAG))
        {
            SetDsAclQosL3Key320(V, u1_g1_fragInfo_f, p_data, p_key->u0.ip.frag_info);
            SetDsAclQosL3Key320(V, u1_g1_fragInfo_f, p_mask, p_key->u0.ip.frag_info_mask);
        }

        if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_IP_OPTION))
        {
            SetDsAclQosL3Key320(V, u1_g1_ipOptions_f, p_data, p_key->u0.ip.ip_option);
            SetDsAclQosL3Key320(V, u1_g1_ipOptions_f, p_mask, 1);
        }

        if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_IP_HDR_ERROR))
        {
            SetDsAclQosL3Key320(V, u1_g1_ipHeaderError_f, p_data, p_key->ip_header_error);
            SetDsAclQosL3Key320(V, u1_g1_ipHeaderError_f, p_mask, 1);
        }

        /*ipv4key.routed_packet always = 0*/
        if (p_key->u0.ip.l4.flag_l4_type)
        {
            SetDsAclQosL3Key320(V, u1_g1_l4CompressType_f, p_data, p_key->u0.ip.l4.l4_compress_type);
            SetDsAclQosL3Key320(V, u1_g1_l4CompressType_f, p_mask, p_key->u0.ip.l4.l4_compress_type_mask);
        }

        if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_ECN))
        {
            SetDsAclQosL3Key320(V, u1_g1_ecn_f, p_data, p_key->u0.ip.ecn);
            SetDsAclQosL3Key320(V, u1_g1_ecn_f, p_mask, p_key->u0.ip.ecn_mask);
        }

        if (p_key->u0.ip.l4.flag_l4info_mapped)
        {
            SetDsAclQosL3Key320(V, u1_g1_l4InfoMapped_f, p_data, p_key->u0.ip.l4.l4info_mapped);
            SetDsAclQosL3Key320(V, u1_g1_l4InfoMapped_f, p_mask, p_key->u0.ip.l4.l4info_mapped_mask);
        }

        if ((CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV4_KEY_SUB_FLAG_L4_SRC_PORT)) ||
            (CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV4_KEY_SUB_FLAG_ICMP_TYPE)) ||
            (CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV4_KEY_SUB_FLAG_ICMP_CODE)) ||
            (CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV4_KEY_SUB_FLAG_IGMP_TYPE)) ||
            (CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV4_KEY_SUB_FLAG_GRE_KEY)) ||
            (CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV4_KEY_SUB_FLAG_NVGRE_KEY)) ||
            (CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV4_KEY_SUB_FLAG_VNI)))
        {
            SetDsAclQosL3Key320(V, u1_g1_l4SourcePort_f, p_data, p_key->u0.ip.l4.l4_src_port);
            SetDsAclQosL3Key320(V, u1_g1_l4SourcePort_f, p_mask, p_key->u0.ip.l4.l4_src_port_mask);
        }

        if (CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV4_KEY_SUB_FLAG_L4_DST_PORT) ||
            (CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV4_KEY_SUB_FLAG_GRE_KEY)) ||
            (CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV4_KEY_SUB_FLAG_NVGRE_KEY)) ||
            (CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV4_KEY_SUB_FLAG_VNI)))
        {
            SetDsAclQosL3Key320(V, u1_g1_l4DestPort_f, p_data, p_key->u0.ip.l4.l4_dst_port);
            SetDsAclQosL3Key320(V, u1_g1_l4DestPort_f, p_mask, p_key->u0.ip.l4.l4_dst_port_mask);
        }
        break;
    case CTC_PARSER_L3_TYPE_ARP:
        if (CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV4_KEY_SUB_FLAG_ARP_OP_CODE))
        {
            SetDsAclQosL3Key320(V, u1_g2_arpOpCode_f, p_data, p_key->u0.arp.op_code);
            SetDsAclQosL3Key320(V, u1_g2_arpOpCode_f, p_mask, p_key->u0.arp.op_code_mask);
        }
        if (CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV4_KEY_SUB_FLAG_ARP_PROTOCOL_TYPE))
        {
            SetDsAclQosL3Key320(V, u1_g2_protocolType_f, p_data, p_key->u0.arp.protocol_type);
            SetDsAclQosL3Key320(V, u1_g2_protocolType_f, p_mask, p_key->u0.arp.protocol_type_mask);
        }
        if (CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV4_KEY_SUB_FLAG_ARP_TARGET_IP))
        {
            SetDsAclQosL3Key320(V, u1_g2_targetIp_f, p_data, p_key->u0.arp.target_ip);
            SetDsAclQosL3Key320(V, u1_g2_targetIp_f, p_mask, p_key->u0.arp.target_ip_mask);
        }
        if (CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV4_KEY_SUB_FLAG_ARP_SENDER_IP))
        {
            SetDsAclQosL3Key320(V, u1_g2_senderIp_f, p_data, p_key->u0.arp.sender_ip);
            SetDsAclQosL3Key320(V, u1_g2_senderIp_f, p_mask, p_key->u0.arp.sender_ip_mask);
        }
        break;
    case CTC_PARSER_L3_TYPE_MPLS:
    case CTC_PARSER_L3_TYPE_MPLS_MCAST:
        if (CTC_FLAG_ISSET(sub1_flag, CTC_ACL_IPV4_KEY_SUB1_FLAG_MPLS_LABEL_NUM))
        {
            SetDsAclQosL3Key320(V, u1_g3_labelNum_f, p_data, p_key->u0.mpls.label_num);
            SetDsAclQosL3Key320(V, u1_g3_labelNum_f, p_mask, p_key->u0.mpls.label_num_mask);
        }

        if (CTC_FLAG_ISSET(sub1_flag, CTC_ACL_IPV4_KEY_SUB1_FLAG_MPLS_LABEL0))
        {
            SetDsAclQosL3Key320(V, u1_g3_mplsLabel0_f, p_data, p_key->u0.mpls.mpls_label0);
            SetDsAclQosL3Key320(V, u1_g3_mplsLabel0_f, p_mask, p_key->u0.mpls.mpls_label0_mask);
        }

        if (CTC_FLAG_ISSET(sub1_flag, CTC_ACL_IPV4_KEY_SUB1_FLAG_MPLS_LABEL1))
        {
            SetDsAclQosL3Key320(V, u1_g3_mplsLabel1_f, p_data, p_key->u0.mpls.mpls_label1);
            SetDsAclQosL3Key320(V, u1_g3_mplsLabel1_f, p_mask, p_key->u0.mpls.mpls_label1_mask);
        }

        if (CTC_FLAG_ISSET(sub1_flag, CTC_ACL_IPV4_KEY_SUB1_FLAG_MPLS_LABEL2))
        {
            SetDsAclQosL3Key320(V, u1_g3_mplsLabel2_f, p_data, p_key->u0.mpls.mpls_label2);
            SetDsAclQosL3Key320(V, u1_g3_mplsLabel2_f, p_mask, p_key->u0.mpls.mpls_label2_mask);
        }

        break;
    case CTC_PARSER_L3_TYPE_NONE:
        if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_ETH_TYPE))
        {
            SetDsAclQosL3Key320(V, u1_g10_etherType_f, p_data, p_key->u0.other.eth_type);
            SetDsAclQosL3Key320(V, u1_g10_etherType_f, p_mask, p_key->u0.other.eth_type_mask);
        }
        break;

    case CTC_PARSER_L3_TYPE_ETHER_OAM:
        if (CTC_FLAG_ISSET(sub3_flag, CTC_ACL_IPV4_KEY_SUB3_FLAG_ETHOAM_LEVEL))
        {
            SetDsAclQosL3Key320(V, u1_g7_etherOamLevel_f, p_data, p_key->u0.ethoam.ethoam_level);
            SetDsAclQosL3Key320(V, u1_g7_etherOamLevel_f, p_mask, p_key->u0.ethoam.ethoam_level_mask);
        }

        if (CTC_FLAG_ISSET(sub3_flag, CTC_ACL_IPV4_KEY_SUB3_FLAG_ETHOAM_OPCODE))
        {
            SetDsAclQosL3Key320(V, u1_g7_etherOamOpCode_f, p_data, p_key->u0.ethoam.ethoam_op_code);
            SetDsAclQosL3Key320(V, u1_g7_etherOamOpCode_f, p_mask, p_key->u0.ethoam.ethoam_op_code_mask);
        }

        #if 0
        if (CTC_FLAG_ISSET(sub3_flag, CTC_ACL_IPV4_KEY_SUB3_FLAG_ETHOAM_VERSION))
        {
            SetDsAclQosL3Key160(V, u1_g7_etherOamVersion_f, p_data, p_key->u0.ethoam.ethoam_version);
            SetDsAclQosL3Key160(V, u1_g7_etherOamVersion_f, p_mask, p_key->u0.ethoam.ethoam_version_mask);
        }

        if (CTC_FLAG_ISSET(sub3_flag, CTC_ACL_IPV4_KEY_SUB3_FLAG_ETHOAM_IS_Y1731))
        {
            SetDsAclQosL3Key160(V, u1_g7_isY1731Oam_f, p_data, p_key->u0.ethoam.ethoam_is_y1731);
            SetDsAclQosL3Key160(V, u1_g7_isY1731Oam_f, p_mask, p_key->u0.ethoam.ethoam_is_y1731_mask);
        }
        #endif

    default:
        break;
#if 0
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
   udf_f
 */
    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_acl_build_ipv6_key(uint8 lchip, drv_acl_group_info_t* p_info,
                                   sys_acl_sw_tcam_key_ipv6_t* p_key,
                                   void  * p_data,
                                   void  * p_mask)
{
    uint32      flag;
    uint32      sub_flag;
    ipv6_addr_t hw_ip6;
    hw_mac_addr_t  hw_mac = { 0 };


    CTC_PTR_VALID_CHECK(p_info);
    CTC_PTR_VALID_CHECK(p_data);
    CTC_PTR_VALID_CHECK(p_mask);
    CTC_PTR_VALID_CHECK(p_key);

    SYS_ACL_DBG_FUNC();

    flag     = p_key->flag;
    sub_flag = p_key->sub_flag;
    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_MAC_DA))
    {
        ACL_SET_HW_MAC(hw_mac, p_key->mac_da);
        SetDsAclQosIpv6Key640(A, macDa_f, p_data, hw_mac);

        ACL_SET_HW_MAC(hw_mac, p_key->mac_da_mask);
        SetDsAclQosIpv6Key640(A, macDa_f, p_mask, hw_mac);
    }
    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_MAC_SA))
    {
        ACL_SET_HW_MAC(hw_mac, p_key->mac_sa);
        SetDsAclQosIpv6Key640(A, macSa_f, p_data, hw_mac);
        ACL_SET_HW_MAC(hw_mac, p_key->mac_sa_mask);
        SetDsAclQosIpv6Key640(A, macSa_f, p_mask, hw_mac);
    }
    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_CVLAN))
    {
        SetDsAclQosIpv6Key640(V, cvlanId_f, p_data, p_key->cvlan);
        SetDsAclQosIpv6Key640(V, cvlanId_f, p_mask, p_key->cvlan_mask);
    }
    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_CTAG_COS))
    {
        SetDsAclQosIpv6Key640(V, ctagCos_f, p_data, p_key->ctag_cos);
        SetDsAclQosIpv6Key640(V, ctagCos_f, p_mask, p_key->ctag_cos_mask);
    }
    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_CTAG_CFI))
    {
        SetDsAclQosIpv6Key640(V, ctagCfi_f, p_data, p_key->ctag_cfi);
        SetDsAclQosIpv6Key640(V, ctagCfi_f, p_mask, 1);
    }
    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_SVLAN))
    {
        SetDsAclQosIpv6Key640(V, svlanId_f, p_data, p_key->svlan);
        SetDsAclQosIpv6Key640(V, svlanId_f, p_mask, p_key->svlan_mask);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_STAG_COS))
    {
        SetDsAclQosIpv6Key640(V, stagCos_f, p_data, p_key->stag_cos);
        SetDsAclQosIpv6Key640(V, stagCos_f, p_mask, p_key->stag_cos_mask);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_STAG_CFI))
    {
        SetDsAclQosIpv6Key640(V, stagCfi_f, p_data, p_key->stag_cfi);
        SetDsAclQosIpv6Key640(V, stagCfi_f, p_mask, 1);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_STAG_VALID))
    {
        SetDsAclQosIpv6Key640(V, svlanIdValid_f, p_data, p_key->stag_valid);
        SetDsAclQosIpv6Key640(V, svlanIdValid_f, p_mask, 1);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_CTAG_VALID))
    {
        SetDsAclQosIpv6Key640(V, cvlanIdValid_f, p_data, p_key->ctag_valid);
        SetDsAclQosIpv6Key640(V, cvlanIdValid_f, p_mask, 1);
    }

    if (p_key->port.valid)
    {
        SetDsAclQosIpv6Key640(V, globalPort_f, p_data, p_key->port.gport);
        SetDsAclQosIpv6Key640(V, globalPort_f, p_mask, p_key->port.gport_mask);

        SetDsAclQosIpv6Key640(V, aclLabel_f, p_data, p_key->port.class_id_data);
        SetDsAclQosIpv6Key640(V, aclLabel_f, p_mask, p_key->port.class_id_mask);

        SetDsAclQosIpv6Key640(V, globalPortType_f, p_data, p_key->port.gport_type);
        SetDsAclQosIpv6Key640(V, globalPortType_f, p_mask, p_key->port.gport_type_mask);

        SetDsAclQosIpv6Key640(A, portBitmap_f, p_mask, p_key->port.bitmap_64);

        SetDsAclQosIpv6Key640(V, portBitmapBase_f, p_data, p_key->port.bitmap_base);
        SetDsAclQosIpv6Key640(V, portBitmapBase_f, p_mask, p_key->port.bitmap_base_mask);
    }
    else
    {
        SetDsAclQosIpv6Key640(V, globalPort_f, p_data, p_info->gport);
        SetDsAclQosIpv6Key640(V, globalPort_f, p_mask, p_info->gport_mask);

        SetDsAclQosIpv6Key640(V, aclLabel_f, p_data, p_info->class_id_data);
        SetDsAclQosIpv6Key640(V, aclLabel_f, p_mask, p_info->class_id_mask);

        SetDsAclQosIpv6Key640(V, globalPortType_f, p_data, p_info->gport_type);
        SetDsAclQosIpv6Key640(V, globalPortType_f, p_mask, p_info->gport_type_mask);

        SetDsAclQosIpv6Key640(A, portBitmap_f, p_mask, p_info->bitmap_64);

        SetDsAclQosIpv6Key640(V, portBitmapBase_f, p_data, p_info->bitmap_base);
        SetDsAclQosIpv6Key640(V, portBitmapBase_f, p_mask, p_info->bitmap_base_mask);
    }

    SetDsAclQosIpv6Key640(V, aclQosKeyType0_f, p_data, DRV_TCAMKEYTYPE_IPV6KEY640);
    SetDsAclQosIpv6Key640(V, aclQosKeyType0_f, p_mask, 0x3);
    SetDsAclQosIpv6Key640(V, aclQosKeyType1_f, p_data, DRV_TCAMKEYTYPE_IPV6KEY640);
    SetDsAclQosIpv6Key640(V, aclQosKeyType1_f, p_mask, 0x3);
    SetDsAclQosIpv6Key640(V, aclQosKeyType2_f, p_data, DRV_TCAMKEYTYPE_IPV6KEY640);
    SetDsAclQosIpv6Key640(V, aclQosKeyType2_f, p_mask, 0x3);
    SetDsAclQosIpv6Key640(V, aclQosKeyType3_f, p_data, DRV_TCAMKEYTYPE_IPV6KEY640);
    SetDsAclQosIpv6Key640(V, aclQosKeyType3_f, p_mask, 0x3);

    /* metadata*/
    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_METADATA))
    {
        SetDsAclQosIpv6Key640(V, metadata_f, p_data, p_key->metadata);
        SetDsAclQosIpv6Key640(V, metadata_f, p_mask, p_key->metadata_mask);
    }

    if(CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_UDF))
    {
        SetDsAclQosIpv6Key640(V, udf_f, p_data, p_key->udf);
        SetDsAclQosIpv6Key640(V, udf_f, p_mask, p_key->udf_mask);
        SetDsAclQosIpv6Key640(V, sclKeyType_f, p_data, p_key->udf_type);
        SetDsAclQosIpv6Key640(V, sclKeyType_f, p_mask, 0x3);
    }
    else
    {
        SetDsAclQosIpv6Key640(V, sclKeyType_f, p_data, 0);
        SetDsAclQosIpv6Key640(V, sclKeyType_f, p_mask, 0);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_ROUTED_PACKET))
    {
        SetDsAclQosIpv6Key640(V, routedPacket_f, p_data, p_key->routed_packet);
        SetDsAclQosIpv6Key640(V, routedPacket_f, p_mask, 1);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_IP_DA))
    {
        ACL_SET_HW_IP6(hw_ip6, p_key->u0.ip.ip_da);
        SetDsAclQosIpv6Key640(A, ipDa_f, p_data, hw_ip6);
        ACL_SET_HW_IP6(hw_ip6, p_key->u0.ip.ip_da_mask);
        SetDsAclQosIpv6Key640(A, ipDa_f, p_mask, hw_ip6);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_IP_SA))
    {
        ACL_SET_HW_IP6(hw_ip6, p_key->u0.ip.ip_sa);
        SetDsAclQosIpv6Key640(A, ipSa_f, p_data, hw_ip6);
        ACL_SET_HW_IP6(hw_ip6, p_key->u0.ip.ip_sa_mask);
        SetDsAclQosIpv6Key640(A, ipSa_f, p_mask, hw_ip6);
    }


    if ((CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_DSCP))
        || (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_PRECEDENCE)))
    {
        SetDsAclQosIpv6Key640(V, dscp_f, p_data, p_key->u0.ip.dscp);
        SetDsAclQosIpv6Key640(V, dscp_f, p_mask, p_key->u0.ip.dscp_mask);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_IP_FRAG))
    {
        SetDsAclQosIpv6Key640(V, fragInfo_f, p_data, p_key->u0.ip.frag_info);
        SetDsAclQosIpv6Key640(V, fragInfo_f, p_mask, p_key->u0.ip.frag_info_mask);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_IP_OPTION))
    {
        SetDsAclQosIpv6Key640(V, ipOptions_f, p_data, p_key->u0.ip.ip_option);
        SetDsAclQosIpv6Key640(V, ipOptions_f, p_mask, 1);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_IP_HDR_ERROR))
    {
        SetDsAclQosIpv6Key640(V, ipHeaderError_f, p_data, p_key->ip_header_error);
        SetDsAclQosIpv6Key640(V, ipHeaderError_f, p_mask, 1);
    }

    if (p_key->u0.ip.l4.flag_l4_type)
    {
        SetDsAclQosIpv6Key640(V, layer4Type_f, p_data, p_key->u0.ip.l4.l4_type);
        SetDsAclQosIpv6Key640(V, layer4Type_f, p_mask, p_key->u0.ip.l4.l4_type_mask);
    }
    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_VLAN_NUM))
    {
        SetDsAclQosIpv6Key640(V, layer2Type_f, p_data, p_key->vlan_num);
        SetDsAclQosIpv6Key640(V, layer2Type_f, p_mask, p_key->vlan_num_mask);
    }
    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_FLOW_LABEL))
    {
        SetDsAclQosIpv6Key640(V, ipv6FlowLabel_f, p_data, p_key->flow_label);
        SetDsAclQosIpv6Key640(V, ipv6FlowLabel_f, p_mask, p_key->flow_label_mask);
    }
    /* ipHeaderError_f */

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_ECN))
    {
        SetDsAclQosIpv6Key640(V, ecn_f, p_data, p_key->u0.ip.ecn);
        SetDsAclQosIpv6Key640(V, ecn_f, p_mask, p_key->u0.ip.ecn_mask);
    }

    if (p_key->u0.ip.l4.flag_l4info_mapped)
    {
        SetDsAclQosIpv6Key640(V, l4InfoMapped_f, p_data, p_key->u0.ip.l4.l4info_mapped);
        SetDsAclQosIpv6Key640(V, l4InfoMapped_f, p_mask, p_key->u0.ip.l4.l4info_mapped_mask);
    }

    if ((CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV6_KEY_SUB_FLAG_L4_SRC_PORT)) ||
        (CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV6_KEY_SUB_FLAG_ICMP_TYPE)) ||
        (CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV6_KEY_SUB_FLAG_ICMP_CODE)) ||
        (CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV6_KEY_SUB_FLAG_GRE_KEY)) ||
        (CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV6_KEY_SUB_FLAG_NVGRE_KEY)) ||
        (CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV6_KEY_SUB_FLAG_VNI)))
    {
        SetDsAclQosIpv6Key640(V, l4SourcePort_f, p_data, p_key->u0.ip.l4.l4_src_port);
        SetDsAclQosIpv6Key640(V, l4SourcePort_f, p_mask, p_key->u0.ip.l4.l4_src_port_mask);
    }

    if (CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV6_KEY_SUB_FLAG_L4_DST_PORT) ||
        (CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV6_KEY_SUB_FLAG_GRE_KEY)) ||
        (CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV6_KEY_SUB_FLAG_NVGRE_KEY)) ||
        (CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV6_KEY_SUB_FLAG_VNI)))
    {
        SetDsAclQosIpv6Key640(V, l4DestPort_f, p_data, p_key->u0.ip.l4.l4_dst_port);
        SetDsAclQosIpv6Key640(V, l4DestPort_f, p_mask, p_key->u0.ip.l4.l4_dst_port_mask);
    }
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_acl_build_hash_ipv6_key(uint8 lchip, sys_acl_entry_t* pe,
                                        DsFlowL3Ipv6HashKey_m* p_ds_key,
                                        uint16 ad_index)
{
    sys_acl_hash_ipv6_key_t* pk = NULL;
    ipv6_addr_t            hw_ip6;
    FlowL3Ipv6HashFieldSelect_m ds_sel;

    CTC_PTR_VALID_CHECK(pe);
    CTC_PTR_VALID_CHECK(p_ds_key);

    SYS_ACL_DBG_FUNC();

    sal_memset(p_ds_key, 0, sizeof(DsFlowL3Ipv6HashKey_m));

    pk = (sys_acl_hash_ipv6_key_t *) &pe->key.u0.hash.u0.hash_ipv6_key;
    CTC_ERROR_RETURN(_sys_goldengate_acl_get_field_sel(lchip, CTC_ACL_KEY_HASH_IPV6, pk->field_sel_id, &ds_sel));

    SetDsFlowL3Ipv6HashKey(V, dsAdIndex_f, p_ds_key, ad_index);
    SetDsFlowL3Ipv6HashKey(V, hashKeyType0_f, p_ds_key, FLOWHASHTYPE_L3IPV6);
    SetDsFlowL3Ipv6HashKey(V, hashKeyType1_f, p_ds_key, FLOWHASHTYPE_L3IPV6);
    SetDsFlowL3Ipv6HashKey(V, flowFieldSel_f, p_ds_key, pk->field_sel_id);
    SetDsFlowL3Ipv6HashKey(V, dscp_f, p_ds_key, pk->dscp);
    ACL_SET_HW_IP6(hw_ip6, pk->ip_sa);
    SetDsFlowL3Ipv6HashKey(A, ipSa_f, p_ds_key, hw_ip6);
    ACL_SET_HW_IP6(hw_ip6, pk->ip_da);
    SetDsFlowL3Ipv6HashKey(A, ipDa_f, p_ds_key, hw_ip6);
    SetDsFlowL3Ipv6HashKey(V, globalSrcPort_f, p_ds_key, pk->gport);
    SetDsFlowL3Ipv6HashKey(V, layer4Type_f, p_ds_key, pk->l4_type);
    if (GetFlowL3Ipv6HashFieldSelect(V, l4SourcePortEn_f, &ds_sel)  || \
        GetFlowL3Ipv6HashFieldSelect(V, icmpTypeEn_f, &ds_sel) || \
        GetFlowL3Ipv6HashFieldSelect(V, icmpOpcodeEn_f, &ds_sel))
    {
        SetDsFlowL3Ipv6HashKey(V, uL4_gPort_l4SourcePort_f, p_ds_key, pk->l4_src_port);
    }
    if (GetFlowL3Ipv6HashFieldSelect(V, l4DestPortEn_f, &ds_sel))
    {
        SetDsFlowL3Ipv6HashKey(V, uL4_gPort_l4DestPort_f, p_ds_key, pk->l4_dst_port);
    }
    if (GetFlowL3Ipv6HashFieldSelect(V, vxlanVniEn_f, &ds_sel))
    {
        SetDsFlowL3Ipv6HashKey(V, uL4_gVxlan_vni_f, p_ds_key, pk->vni);
    }
    if (GetFlowL3Ipv6HashFieldSelect(V, greKeyEn_f, &ds_sel))
    {
        SetDsFlowL3Ipv6HashKey(V, uL4_gKey_greKey_f, p_ds_key, pk->gre_key);
    }
/*
   uL4_gKey_greKey_f
   uL4_gVxlan_vni_f
   isVxlan_f
 */
    return CTC_E_NONE;
}



STATIC int32
_sys_goldengate_acl_build_hash_mpls_key(uint8 lchip, sys_acl_entry_t* pe,
                                        DsFlowL3MplsHashKey_m* p_ds_key,
                                        uint16 ad_index)
{
    sys_acl_hash_mpls_key_t* pk = NULL;

    CTC_PTR_VALID_CHECK(pe);
    CTC_PTR_VALID_CHECK(p_ds_key);

    SYS_ACL_DBG_FUNC();

    sal_memset(p_ds_key, 0, sizeof(DsFlowL3MplsHashKey_m));

    pk = (sys_acl_hash_mpls_key_t *) &pe->key.u0.hash.u0.hash_mpls_key;

    SetDsFlowL3MplsHashKey(V, dsAdIndex_f, p_ds_key, ad_index);
    SetDsFlowL3MplsHashKey(V, hashKeyType_f, p_ds_key, FLOWHASHTYPE_L3MPLS);
    SetDsFlowL3MplsHashKey(V, flowFieldSel_f, p_ds_key, pk->field_sel_id);
    SetDsFlowL3MplsHashKey(V, globalSrcPort_f, p_ds_key, pk->gport);
    SetDsFlowL3MplsHashKey(V, metadata_f, p_ds_key, pk->metadata);
    SetDsFlowL3MplsHashKey(V, labelNum_f, p_ds_key, pk->label_num);

    SetDsFlowL3MplsHashKey(V, mplsTtl0_f, p_ds_key, pk->mpls_ttl0);
    SetDsFlowL3MplsHashKey(V, mplsSbit0_f, p_ds_key, pk->mpls_s0);
    SetDsFlowL3MplsHashKey(V, mplsExp0_f, p_ds_key, pk->mpls_exp0);
    SetDsFlowL3MplsHashKey(V, mplsLabel0_f, p_ds_key, pk->mpls_label0);

    SetDsFlowL3MplsHashKey(V, mplsTtl1_f, p_ds_key, pk->mpls_ttl1);
    SetDsFlowL3MplsHashKey(V, mplsSbit1_f, p_ds_key, pk->mpls_s1);
    SetDsFlowL3MplsHashKey(V, mplsExp1_f, p_ds_key, pk->mpls_exp1);
    SetDsFlowL3MplsHashKey(V, mplsLabel1_f, p_ds_key, pk->mpls_label1);

    SetDsFlowL3MplsHashKey(V, mplsTtl2_f, p_ds_key, pk->mpls_ttl2);
    SetDsFlowL3MplsHashKey(V, mplsSbit2_f, p_ds_key, pk->mpls_s2);
    SetDsFlowL3MplsHashKey(V, mplsExp2_f, p_ds_key, pk->mpls_exp2);
    SetDsFlowL3MplsHashKey(V, mplsLabel2_f, p_ds_key, pk->mpls_label2);

    return CTC_E_NONE;
}



STATIC int32
_sys_goldengate_acl_build_hash_l2_l3_key(uint8 lchip, sys_acl_entry_t* pe,
                                         DsFlowL2L3HashKey_m* p_ds_key,
                                         uint16 ad_index)
{
    sys_acl_hash_l2_l3_key_t* pk = NULL;
    hw_mac_addr_t              hw_mac_sa;
    hw_mac_addr_t              hw_mac_da;
    FlowL2L3HashFieldSelect_m ds_sel;

    CTC_PTR_VALID_CHECK(pe);
    CTC_PTR_VALID_CHECK(p_ds_key);

    SYS_ACL_DBG_FUNC();

    sal_memset(p_ds_key, 0, sizeof(DsFlowL2L3HashKey_m));

    pk = (sys_acl_hash_l2_l3_key_t *) &pe->key.u0.hash.u0.hash_l2_l3_key;
    CTC_ERROR_RETURN(_sys_goldengate_acl_get_field_sel(lchip, CTC_ACL_KEY_HASH_L2_L3, pk->field_sel_id, &ds_sel));

    SetDsFlowL2L3HashKey(V, dsAdIndex_f, p_ds_key, ad_index);
    SetDsFlowL2L3HashKey(V, hashKeyType0_f, p_ds_key, FLOWHASHTYPE_L2L3);
    SetDsFlowL2L3HashKey(V, hashKeyType1_f, p_ds_key, FLOWHASHTYPE_L2L3);
    SetDsFlowL2L3HashKey(V, flowFieldSel_f, p_ds_key, pk->field_sel_id);
    SetDsFlowL2L3HashKey(V, metadata_f, p_ds_key, pk->metadata);
    SetDsFlowL2L3HashKey(V, uL3_gUnknown_etherType_f, p_ds_key, pk->eth_type);

    SetDsFlowL2L3HashKey(V, cvlanIdValid_f, p_ds_key, pk->ctag_valid);
    SetDsFlowL2L3HashKey(V, ctagCos_f, p_ds_key, pk->ctag_cos);
    SetDsFlowL2L3HashKey(V, cvlanId_f, p_ds_key, pk->ctag_vid);
    SetDsFlowL2L3HashKey(V, ctagCfi_f, p_ds_key, pk->ctag_cfi);
    SetDsFlowL2L3HashKey(V, svlanIdValid_f, p_ds_key, pk->stag_valid);
    SetDsFlowL2L3HashKey(V, stagCos_f, p_ds_key, pk->stag_cos);
    SetDsFlowL2L3HashKey(V, svlanId_f, p_ds_key, pk->stag_vid);
    SetDsFlowL2L3HashKey(V, stagCfi_f, p_ds_key, pk->stag_cfi);
    ACL_SET_HW_MAC(hw_mac_sa, pk->mac_sa);
    ACL_SET_HW_MAC(hw_mac_da, pk->mac_da);
    SetDsFlowL2L3HashKey(A, macSa_f, p_ds_key, hw_mac_sa);
    SetDsFlowL2L3HashKey(A, macDa_f, p_ds_key, hw_mac_da);
    SetDsFlowL2L3HashKey(V, globalSrcPort_f, p_ds_key, pk->gport);
    SetDsFlowL2L3HashKey(V, layer4Type_f, p_ds_key, pk->l4_type);
    SetDsFlowL2L3HashKey(V, layer3Type_f, p_ds_key, pk->l3_type);
    if (GetFlowL2L3HashFieldSelect(V, l4SourcePortEn_f, &ds_sel)|| \
        GetFlowL2L3HashFieldSelect(V, icmpTypeEn_f, &ds_sel) || \
        GetFlowL2L3HashFieldSelect(V, icmpOpcodeEn_f, &ds_sel))
    {
        SetDsFlowL2L3HashKey(V, uL4_gPort_l4SourcePort_f, p_ds_key, pk->l4_src_port);
    }
    if (GetFlowL2L3HashFieldSelect(V, l4DestPortEn_f, &ds_sel))
    {
        SetDsFlowL2L3HashKey(V, uL4_gPort_l4DestPort_f, p_ds_key, pk->l4_dst_port);
    }
    if (GetFlowL2L3HashFieldSelect(V, vxlanVniEn_f, &ds_sel))
    {
        SetDsFlowL2L3HashKey(V, uL4_gVxlan_vni_f, p_ds_key, pk->vni);
    }
    if (GetFlowL2L3HashFieldSelect(V, greKeyEn_f, &ds_sel))
    {
        SetDsFlowL2L3HashKey(V, uL4_gKey_greKey_f, p_ds_key, pk->gre_key);
    }

    if (CTC_PARSER_L3_TYPE_IPV4 == pk->l3_type)
    {
        SetDsFlowL2L3HashKey(V, uL3_gIpv4_dscp_f, p_ds_key, pk->l3.ipv4.dscp);
        SetDsFlowL2L3HashKey(V, uL3_gIpv4_ecn_f, p_ds_key, pk->l3.ipv4.ecn);
        SetDsFlowL2L3HashKey(V, uL3_gIpv4_ttl_f, p_ds_key, pk->l3.ipv4.ttl);

        SetDsFlowL2L3HashKey(V, uL3_gIpv4_ipSa_f, p_ds_key, pk->l3.ipv4.ip_sa);
        SetDsFlowL2L3HashKey(V, uL3_gIpv4_ipDa_f, p_ds_key, pk->l3.ipv4.ip_da);
    }
    else if ((CTC_PARSER_L3_TYPE_MPLS == pk->l3_type) ||
             (CTC_PARSER_L3_TYPE_MPLS_MCAST == pk->l3_type))
    {
        SetDsFlowL2L3HashKey(V, uL3_gMpls_mplsLabel0_f, p_ds_key, pk->l3.mpls.mpls_label0);
        SetDsFlowL2L3HashKey(V, uL3_gMpls_mplsExp0_f, p_ds_key, pk->l3.mpls.mpls_exp0);
        SetDsFlowL2L3HashKey(V, uL3_gMpls_mplsSbit0_f, p_ds_key, pk->l3.mpls.mpls_s0);
        SetDsFlowL2L3HashKey(V, uL3_gMpls_mplsTtl0_f, p_ds_key, pk->l3.mpls.mpls_ttl0);

        SetDsFlowL2L3HashKey(V, uL3_gMpls_mplsLabel1_f, p_ds_key, pk->l3.mpls.mpls_label1);
        SetDsFlowL2L3HashKey(V, uL3_gMpls_mplsExp1_f, p_ds_key, pk->l3.mpls.mpls_exp1);
        SetDsFlowL2L3HashKey(V, uL3_gMpls_mplsSbit1_f, p_ds_key, pk->l3.mpls.mpls_s1);
        SetDsFlowL2L3HashKey(V, uL3_gMpls_mplsTtl1_f, p_ds_key, pk->l3.mpls.mpls_ttl1);

        SetDsFlowL2L3HashKey(V, uL3_gMpls_mplsLabel2_f, p_ds_key, pk->l3.mpls.mpls_label2);
        SetDsFlowL2L3HashKey(V, uL3_gMpls_mplsExp2_f, p_ds_key, pk->l3.mpls.mpls_exp2);
        SetDsFlowL2L3HashKey(V, uL3_gMpls_mplsSbit2_f, p_ds_key, pk->l3.mpls.mpls_s2);
        SetDsFlowL2L3HashKey(V, uL3_gMpls_mplsTtl2_f, p_ds_key, pk->l3.mpls.mpls_ttl2);
        SetDsFlowL2L3HashKey(V, uL3_gMpls_labelNum_f, p_ds_key, pk->l3.mpls.label_num);
    }

/*
   uL3_gIpv4_l4InfoMapped_f
   uL4_gKey_greKey_f
   uL4_gVxlan_vni_f
   layer4UserType_f
 */
    return CTC_E_NONE;
}



STATIC int32
_sys_goldengate_acl_build_hash_ipv4_key(uint8 lchip, sys_acl_entry_t* pe,
                                        DsFlowL3Ipv4HashKey_m* p_ds_key,
                                        uint16 ad_index)
{
    sys_acl_hash_ipv4_key_t* pk = NULL;
    FlowL3Ipv4HashFieldSelect_m ds_sel;

    CTC_PTR_VALID_CHECK(pe);
    CTC_PTR_VALID_CHECK(p_ds_key);

    SYS_ACL_DBG_FUNC();

    sal_memset(&ds_sel, 0, sizeof(ds_sel));
    sal_memset(p_ds_key, 0, sizeof(DsFlowL3Ipv4HashKey_m));

    pk = (sys_acl_hash_ipv4_key_t *) &pe->key.u0.hash.u0.hash_ipv4_key;
    CTC_ERROR_RETURN(_sys_goldengate_acl_get_field_sel(lchip, CTC_ACL_KEY_HASH_IPV4, pk->field_sel_id, &ds_sel));

    SetDsFlowL3Ipv4HashKey(V, dsAdIndex_f, p_ds_key, ad_index);
    SetDsFlowL3Ipv4HashKey(V, hashKeyType_f, p_ds_key, FLOWHASHTYPE_L3IPV4);
    SetDsFlowL3Ipv4HashKey(V, flowFieldSel_f, p_ds_key, pk->field_sel_id);
    SetDsFlowL3Ipv4HashKey(V, metadata_f, p_ds_key, pk->metadata);
    SetDsFlowL3Ipv4HashKey(V, dscp_f, p_ds_key, pk->dscp);
    SetDsFlowL3Ipv4HashKey(V, ecn_f, p_ds_key, pk->ecn);
    SetDsFlowL3Ipv4HashKey(V, layer3HeaderProtocol_f, p_ds_key, pk->l4_protocol);
    if (GetFlowL3Ipv4HashFieldSelect(V, l4SourcePortEn_f, &ds_sel)|| \
        GetFlowL3Ipv4HashFieldSelect(V, icmpTypeEn_f, &ds_sel) || \
        GetFlowL3Ipv4HashFieldSelect(V, icmpOpcodeEn_f, &ds_sel))
    {
        SetDsFlowL3Ipv4HashKey(V, uL4_gPort_l4SourcePort_f, p_ds_key, pk->l4_src_port);
    }
    if (GetFlowL3Ipv4HashFieldSelect(V, l4DestPortEn_f, &ds_sel))
    {
        SetDsFlowL3Ipv4HashKey(V, uL4_gPort_l4DestPort_f, p_ds_key, pk->l4_dst_port);
    }
    SetDsFlowL3Ipv4HashKey(V, ipSa_f, p_ds_key, pk->ip_sa);
    SetDsFlowL3Ipv4HashKey(V, ipDa_f, p_ds_key, pk->ip_da);
    SetDsFlowL3Ipv4HashKey(V, globalSrcPort_f, p_ds_key, pk->gport);
    if (GetFlowL3Ipv4HashFieldSelect(V, vxlanVniEn_f, &ds_sel))
    {
        SetDsFlowL3Ipv4HashKey(V, uL4_gVxlan_vni_f, p_ds_key, pk->vni);
    }
    if (GetFlowL3Ipv4HashFieldSelect(V, greKeyEn_f, &ds_sel))
    {
        SetDsFlowL3Ipv4HashKey(V, uL4_gKey_greKey_f, p_ds_key, pk->gre_key);
    }
/*
   layer4UserType_f
   uL4_gKey_greKey_f
   uL4_gVxlan_vni_f
   isRouteMac_f
 */

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_acl_build_hash_mac_key(uint8 lchip, sys_acl_entry_t* pe,
                                       DsFlowL2HashKey_m * p_ds_key,
                                       uint16 ad_index)
{
    sys_acl_hash_mac_key_t* pk = NULL;
    hw_mac_addr_t            hw_mac;

    CTC_PTR_VALID_CHECK(pe);
    CTC_PTR_VALID_CHECK(p_ds_key);

    SYS_ACL_DBG_FUNC();

    sal_memset(p_ds_key, 0, sizeof(DsFlowL2HashKey_m));

    pk = (sys_acl_hash_mac_key_t *) &pe->key.u0.hash.u0.hash_mac_key;

    SetDsFlowL2HashKey(V, dsAdIndex_f, p_ds_key, ad_index);
    SetDsFlowL2HashKey(V, hashKeyType_f, p_ds_key, FLOWHASHTYPE_L2);
    SetDsFlowL2HashKey(V, flowFieldSel_f, p_ds_key, pk->field_sel_id);
    SetDsFlowL2HashKey(V, globalSrcPort_f, p_ds_key, pk->gport);
    SetDsFlowL2HashKey(V, flowL2KeyUseCvlan_f, p_ds_key, pk->is_ctag);
    SetDsFlowL2HashKey(V, etherType_f, p_ds_key, pk->eth_type);
    SetDsFlowL2HashKey(V, svlanIdValid_f, p_ds_key, pk->tag_exist);
    SetDsFlowL2HashKey(V, stagCos_f, p_ds_key, pk->cos);
    SetDsFlowL2HashKey(V, svlanId_f, p_ds_key, pk->vlan_id);
    SetDsFlowL2HashKey(V, stagCfi_f, p_ds_key, pk->cfi);
    ACL_SET_HW_MAC(hw_mac, pk->mac_sa);
    SetDsFlowL2HashKey(A, macSa_f, p_ds_key, hw_mac);
    ACL_SET_HW_MAC(hw_mac, pk->mac_da);
    SetDsFlowL2HashKey(A, macDa_f, p_ds_key, hw_mac);

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_acl_build_pbr_ds_ipv4_key(uint8 lchip, drv_acl_group_info_t* p_info,
                                          sys_acl_pbr_ipv4_key_t* p_sys_key,
                                          void* p_ds_key,
                                          void* p_ds_mask)
{
    uint32 flag     = 0;
    uint32 sub_flag = 0;
    CTC_PTR_VALID_CHECK(p_info);
    CTC_PTR_VALID_CHECK(p_sys_key);
    CTC_PTR_VALID_CHECK(p_ds_key);
    CTC_PTR_VALID_CHECK(p_ds_mask);

    SYS_ACL_DBG_FUNC();
    flag     = p_sys_key->flag;
    sub_flag = p_sys_key->sub_flag;

    if (CTC_FLAG_ISSET(flag, CTC_ACL_PBR_IPV4_KEY_FLAG_IP_DA))
    {
        SetDsLpmTcamIpv4160Key(V, ipDa_f, p_ds_key, p_sys_key->u0.ip.ip_da);
        SetDsLpmTcamIpv4160Key(V, ipDa_f, p_ds_mask, p_sys_key->u0.ip.ip_da_mask);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_PBR_IPV4_KEY_FLAG_IP_SA))
    {
        SetDsLpmTcamIpv4160Key(V, ipSa_f, p_ds_key, p_sys_key->u0.ip.ip_sa);
        SetDsLpmTcamIpv4160Key(V, ipSa_f, p_ds_mask, p_sys_key->u0.ip.ip_sa_mask);
    }

    if (p_sys_key->u0.ip.l4.flag_l4info_mapped)
    {
        SetDsLpmTcamIpv4160Key(V, l4InfoMapped_f, p_ds_key, p_sys_key->u0.ip.l4.l4info_mapped);
        SetDsLpmTcamIpv4160Key(V, l4InfoMapped_f, p_ds_mask, p_sys_key->u0.ip.l4.l4info_mapped_mask);
    }

    if (p_sys_key->u0.ip.l4.flag_l4_type)
    {
        SetDsLpmTcamIpv4160Key(V, layer4Type_f, p_ds_key, p_sys_key->u0.ip.l4.l4_type);
        SetDsLpmTcamIpv4160Key(V, layer4Type_f, p_ds_mask, p_sys_key->u0.ip.l4.l4_type_mask);
    }

    if ((CTC_FLAG_ISSET(sub_flag, CTC_ACL_PBR_IPV4_KEY_SUB_FLAG_L4_SRC_PORT)) ||
        (CTC_FLAG_ISSET(sub_flag, CTC_ACL_PBR_IPV4_KEY_SUB_FLAG_ICMP_TYPE)) ||
        (CTC_FLAG_ISSET(sub_flag, CTC_ACL_PBR_IPV4_KEY_SUB_FLAG_ICMP_CODE)))
    {
        SetDsLpmTcamIpv4160Key(V, l4SourcePort_f, p_ds_key, p_sys_key->u0.ip.l4.l4_src_port);
        SetDsLpmTcamIpv4160Key(V, l4SourcePort_f, p_ds_mask, p_sys_key->u0.ip.l4.l4_src_port_mask);
    }

    if (CTC_FLAG_ISSET(sub_flag, CTC_ACL_PBR_IPV4_KEY_SUB_FLAG_L4_DST_PORT))
    {
        SetDsLpmTcamIpv4160Key(V, l4DestPort_f, p_ds_key, p_sys_key->u0.ip.l4.l4_dst_port);
        SetDsLpmTcamIpv4160Key(V, l4DestPort_f, p_ds_mask, p_sys_key->u0.ip.l4.l4_dst_port_mask);
    }

    if ((CTC_FLAG_ISSET(flag, CTC_ACL_PBR_IPV4_KEY_FLAG_DSCP))
        || (CTC_FLAG_ISSET(flag, CTC_ACL_PBR_IPV4_KEY_FLAG_PRECEDENCE)))
    {
        SetDsLpmTcamIpv4160Key(V, dscp_f, p_ds_key, p_sys_key->u0.ip.dscp);
        SetDsLpmTcamIpv4160Key(V, dscp_f, p_ds_mask, p_sys_key->u0.ip.dscp_mask);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_PBR_IPV4_KEY_FLAG_IP_FRAG))
    {
        SetDsLpmTcamIpv4160Key(V, fragInfo_f, p_ds_key, p_sys_key->u0.ip.frag_info);
        SetDsLpmTcamIpv4160Key(V, fragInfo_f, p_ds_mask, p_sys_key->u0.ip.frag_info_mask);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_PBR_IPV4_KEY_FLAG_VRFID))
    {
        SetDsLpmTcamIpv4160Key(V, vrfId_f, p_ds_key, p_sys_key->vrfid);
        SetDsLpmTcamIpv4160Key(V, vrfId_f, p_ds_mask, p_sys_key->vrfid_mask);
    }

    if (p_sys_key->port.valid)
    {
        SetDsLpmTcamIpv4160Key(V, pbrLabel_f, p_ds_key, p_sys_key->port.class_id_data);
        SetDsLpmTcamIpv4160Key(V, pbrLabel_f, p_ds_mask, p_sys_key->port.class_id_mask);
    }
    else
    {
        SetDsLpmTcamIpv4160Key(V, pbrLabel_f, p_ds_key, p_info->class_id_data);
        SetDsLpmTcamIpv4160Key(V, pbrLabel_f, p_ds_mask, p_info->class_id_mask);
    }

    SetDsLpmTcamIpv4160Key(V, lpmTcamKeyType_f, p_ds_key, DRV_FIBNATPBRTCAMKEYTYPE_IPV4PBR);
    SetDsLpmTcamIpv4160Key(V, lpmTcamKeyType_f, p_ds_mask, 3);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_acl_build_pbr_ds_ipv6_key(uint8 lchip, drv_acl_group_info_t* p_info,
                                          sys_acl_pbr_ipv6_key_t* p_sys_key,
                                          void* p_ds_key,
                                          void* p_ds_mask)
{
    uint32 flag     = 0;
    uint32 sub_flag = 0;
    ipv6_addr_t            hw_ip6;
    ipv6_addr_t            hw_ip6_mask;

    CTC_PTR_VALID_CHECK(p_info);
    CTC_PTR_VALID_CHECK(p_sys_key);
    CTC_PTR_VALID_CHECK(p_ds_key);
    CTC_PTR_VALID_CHECK(p_ds_mask);

    SYS_ACL_DBG_FUNC();
    flag     = p_sys_key->flag;
    sub_flag = p_sys_key->sub_flag;

    if (CTC_FLAG_ISSET(flag, CTC_ACL_PBR_IPV6_KEY_FLAG_IP_DA))
    {
        ACL_SET_HW_IP6(hw_ip6, p_sys_key->u0.ip.ip_da);
        ACL_SET_HW_IP6(hw_ip6_mask, p_sys_key->u0.ip.ip_da_mask);

        SetDsLpmTcamIpv6320Key(A, ipDa_f, p_ds_key, hw_ip6);
        SetDsLpmTcamIpv6320Key(A, ipDa_f, p_ds_mask, hw_ip6_mask);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_PBR_IPV6_KEY_FLAG_IP_SA))
    {
        ACL_SET_HW_IP6(hw_ip6, p_sys_key->u0.ip.ip_sa);
        ACL_SET_HW_IP6(hw_ip6_mask, p_sys_key->u0.ip.ip_sa_mask);

        SetDsLpmTcamIpv6320Key(A, ipSa_f, p_ds_key, hw_ip6);
        SetDsLpmTcamIpv6320Key(A, ipSa_f, p_ds_mask, hw_ip6_mask);
    }

    if (p_sys_key->u0.ip.l4.flag_l4info_mapped)
    {
        SetDsLpmTcamIpv6320Key(V, l4InfoMapped_f, p_ds_key, p_sys_key->u0.ip.l4.l4info_mapped);
        SetDsLpmTcamIpv6320Key(V, l4InfoMapped_f, p_ds_mask, p_sys_key->u0.ip.l4.l4info_mapped_mask);
    }

    if (p_sys_key->u0.ip.l4.flag_l4_type)
    {
        SetDsLpmTcamIpv6320Key(V, layer4Type_f, p_ds_key, p_sys_key->u0.ip.l4.l4_type);
        SetDsLpmTcamIpv6320Key(V, layer4Type_f, p_ds_mask, p_sys_key->u0.ip.l4.l4_type_mask);
    }

    if ((CTC_FLAG_ISSET(sub_flag, CTC_ACL_PBR_IPV6_KEY_SUB_FLAG_L4_SRC_PORT)) ||
        (CTC_FLAG_ISSET(sub_flag, CTC_ACL_PBR_IPV6_KEY_SUB_FLAG_ICMP_TYPE)) ||
        (CTC_FLAG_ISSET(sub_flag, CTC_ACL_PBR_IPV6_KEY_SUB_FLAG_ICMP_CODE)))
    {
        SetDsLpmTcamIpv6320Key(V, l4SourcePort_f, p_ds_key, p_sys_key->u0.ip.l4.l4_src_port);
        SetDsLpmTcamIpv6320Key(V, l4SourcePort_f, p_ds_mask, p_sys_key->u0.ip.l4.l4_src_port_mask);
    }

    if (CTC_FLAG_ISSET(sub_flag, CTC_ACL_PBR_IPV6_KEY_SUB_FLAG_L4_DST_PORT))
    {
        SetDsLpmTcamIpv6320Key(V, l4DestPort_f, p_ds_key, p_sys_key->u0.ip.l4.l4_dst_port);
        SetDsLpmTcamIpv6320Key(V, l4DestPort_f, p_ds_mask, p_sys_key->u0.ip.l4.l4_dst_port_mask);
    }

    if ((CTC_FLAG_ISSET(flag, CTC_ACL_PBR_IPV6_KEY_FLAG_DSCP))
        || (CTC_FLAG_ISSET(flag, CTC_ACL_PBR_IPV6_KEY_FLAG_PRECEDENCE)))
    {
        SetDsLpmTcamIpv6320Key(V, dscp_f, p_ds_key, p_sys_key->u0.ip.dscp);
        SetDsLpmTcamIpv6320Key(V, dscp_f, p_ds_mask, p_sys_key->u0.ip.dscp_mask);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_PBR_IPV6_KEY_FLAG_IP_FRAG))
    {
        SetDsLpmTcamIpv6320Key(V, fragInfo_f, p_ds_key, p_sys_key->u0.ip.frag_info);
        SetDsLpmTcamIpv6320Key(V, fragInfo_f, p_ds_mask, p_sys_key->u0.ip.frag_info_mask);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_PBR_IPV6_KEY_FLAG_FLOW_LABEL))
    {
        SetDsLpmTcamIpv6320Key(V, ipv6FlowLabel_f, p_ds_key, p_sys_key->flow_label);
        SetDsLpmTcamIpv6320Key(V, ipv6FlowLabel_f, p_ds_mask, p_sys_key->flow_label_mask);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_PBR_IPV6_KEY_FLAG_VRFID))
    {
        SetDsLpmTcamIpv6320Key(V, vrfId_f, p_ds_key, p_sys_key->vrfid);
        SetDsLpmTcamIpv6320Key(V, vrfId_f, p_ds_mask, p_sys_key->vrfid_mask);
    }

    if (p_sys_key->port.valid)
    {
        SetDsLpmTcamIpv6320Key(V, pbrLabel_f, p_ds_key, p_sys_key->port.class_id_data);
        SetDsLpmTcamIpv6320Key(V, pbrLabel_f, p_ds_mask, p_sys_key->port.class_id_mask);
    }
    else
    {
        SetDsLpmTcamIpv6320Key(V, pbrLabel_f, p_ds_key, p_info->class_id_data);
        SetDsLpmTcamIpv6320Key(V, pbrLabel_f, p_ds_mask, p_info->class_id_mask);
    }
    SetDsLpmTcamIpv6320Key(V, lpmTcamKeyType0_f, p_ds_key, DRV_FIBNATPBRTCAMKEYTYPE_IPV6PBR);
    SetDsLpmTcamIpv6320Key(V, lpmTcamKeyType0_f, p_ds_mask, 3);

    SetDsLpmTcamIpv6320Key(V, lpmTcamKeyType1_f, p_ds_key, DRV_FIBNATPBRTCAMKEYTYPE_IPV6PBR);
    SetDsLpmTcamIpv6320Key(V, lpmTcamKeyType1_f, p_ds_mask, 3);

    return CTC_E_NONE;
}



STATIC int32
_sys_goldengate_acl_get_table_id(uint8 lchip, sys_acl_entry_t* pe, uint32* key_id, uint32* act_id)
{
    uint32 _key_id  = 0;
    uint32 _act_id  = 0;
    uint8  dir      = 0;
    uint8  block_id = 0;

    CTC_PTR_VALID_CHECK(pe);
    CTC_PTR_VALID_CHECK(pe->group);
    dir      = pe->group->group_info.dir;
    block_id = pe->group->group_info.block_id;

    if ((pe->key.type == CTC_ACL_KEY_MAC) && (0 == p_gg_acl_master[lchip]->ipv4_160bit_mode_en))
    {
        _key_id = (CTC_INGRESS == dir) ? DsAclQosMacKey160Ingr0_t : DsAclQosMacKey160Egr0_t;
        _act_id = (CTC_INGRESS == dir) ? DsIngAcl0Mac160TcamAd_t : DsEgrAcl0Mac160TcamAd_t;
    }
    else if (pe->key.type == CTC_ACL_KEY_MAC)
    {
        _key_id = (CTC_INGRESS == dir) ? DsAclQosL3Key320Ingr0_t : DsAclQosL3Key320Egr0_t;
        _act_id = (CTC_INGRESS == dir) ? DsIngAcl0L3320TcamAd_t : DsEgrAcl0L3320TcamAd_t;
    }
    else if ((pe->key.type == CTC_ACL_KEY_IPV4) &&
             (CTC_ACL_KEY_SIZE_DOUBLE == pe->key.u0.ipv4_key.key_size))
    {
        _key_id = (CTC_INGRESS == dir) ? DsAclQosL3Key320Ingr0_t : DsAclQosL3Key320Egr0_t;
        _act_id = (CTC_INGRESS == dir) ? DsIngAcl0L3320TcamAd_t : DsEgrAcl0L3320TcamAd_t;
    }
    else if (pe->key.type == CTC_ACL_KEY_IPV4)
    {
        _key_id = (CTC_INGRESS == dir) ? DsAclQosL3Key160Ingr0_t : DsAclQosL3Key160Egr0_t;
        _act_id = (CTC_INGRESS == dir) ? DsIngAcl0L3160TcamAd_t : DsEgrAcl0L3160TcamAd_t;
    }
    else if (pe->key.type == CTC_ACL_KEY_IPV6)
    {
        _key_id = (CTC_INGRESS == dir) ? DsAclQosIpv6Key640Ingr0_t : DsAclQosIpv6Key640Egr0_t;
        _act_id = (CTC_INGRESS == dir) ? DsIngAcl0Ipv6640TcamAd_t : DsEgrAcl0Ipv6640TcamAd_t;
    }
    else if (pe->key.type == CTC_ACL_KEY_PBR_IPV4)
    {
           _key_id  = DsLpmTcamIpv4Pbr160Key_t;
           _act_id  = DsLpmTcamIpv4Pbr160Ad_t;
    }
    else if (pe->key.type == CTC_ACL_KEY_PBR_IPV6)
    {
           _key_id  = DsLpmTcamIpv6320Key_t;
           _act_id  = DsLpmTcamIpv6Pbr320Ad_t;
    }
    else if (pe->key.type == CTC_ACL_KEY_HASH_MAC)
    {
        _key_id = DsFlowL2HashKey_t;
        _act_id = DsAcl_t;
    }
    else if (pe->key.type == CTC_ACL_KEY_HASH_IPV4)
    {
        _key_id = DsFlowL3Ipv4HashKey_t;
        _act_id = DsAcl_t;
    }
    else if (pe->key.type == CTC_ACL_KEY_HASH_IPV6)
    {
        _key_id = DsFlowL3Ipv6HashKey_t;
        _act_id = DsAcl_t;
    }
    else if (pe->key.type == CTC_ACL_KEY_HASH_MPLS)
    {
        _key_id = DsFlowL3MplsHashKey_t;
        _act_id = DsAcl_t;
    }
    else if (pe->key.type == CTC_ACL_KEY_HASH_L2_L3)
    {
        _key_id = DsFlowL2L3HashKey_t;
        _act_id = DsAcl_t;
    }

    _key_id += block_id;
    _act_id += block_id * (DsIngAcl1Ipv6640TcamAd_t - DsIngAcl0Ipv6640TcamAd_t);

    if (key_id)
    {
        *key_id = _key_id;
    }

    if (act_id)
    {
        *act_id = _act_id;
    }

    return CTC_E_NONE;
}

/*
 * remove entry specified by entry id from hardware table
 */
STATIC int32
_sys_goldengate_acl_remove_hw(uint8 lchip, uint32 eid)
{
    uint32              index    = 0;
    uint32              key_id   = 0;
    uint8               step     = 0;

    sys_acl_group_t     * pg = NULL;
    sys_acl_entry_t     * pe = NULL;
    ds_t                 ds_hash_key;

    SYS_ACL_DBG_FUNC();
    SYS_ACL_DBG_PARAM("  %% PARAM: eid: %u \n", eid);

    /* get sys entry */
    _sys_goldengate_acl_get_nodes_by_eid(lchip, eid, &pe, &pg, NULL);
    if(!pe)
    {
        return CTC_E_ACL_ENTRY_UNEXIST;
    }
    if(!pg)
    {
        return CTC_E_ACL_GROUP_UNEXIST;
    }
    /* get block_id, index */
    index    = pe->fpae.offset_a;

    /* get group_info */

    _sys_goldengate_acl_get_table_id(lchip, pe, &key_id, NULL);

    if (!ACL_ENTRY_IS_TCAM(pe->key.type))
    {

        /* -- set hw valid bit  -- */
        drv_cpu_acc_in_t in;
        sal_memset(&in, 0, sizeof(in));
        sal_memset(&ds_hash_key, 0, sizeof(ds_hash_key));

        switch (pe->key.type)
        {
            case CTC_ACL_KEY_HASH_MAC:
                SetDsFlowL2HashKey(V, hashKeyType_f, ds_hash_key, FLOWHASHTYPE_L2);
                break;

            case CTC_ACL_KEY_HASH_IPV4:
                SetDsFlowL3Ipv4HashKey(V, hashKeyType_f, ds_hash_key, FLOWHASHTYPE_L3IPV4);
                break;

            case CTC_ACL_KEY_HASH_IPV6:
                SetDsFlowL3Ipv6HashKey(V, hashKeyType0_f, ds_hash_key, FLOWHASHTYPE_L3IPV6);
                SetDsFlowL3Ipv6HashKey(V, hashKeyType1_f, ds_hash_key, FLOWHASHTYPE_L3IPV6);
                break;

            case CTC_ACL_KEY_HASH_MPLS:
                SetDsFlowL3MplsHashKey(V, hashKeyType_f, ds_hash_key, FLOWHASHTYPE_L3MPLS);
                break;

            case CTC_ACL_KEY_HASH_L2_L3:
                SetDsFlowL2L3HashKey(V, hashKeyType0_f, ds_hash_key, FLOWHASHTYPE_L2L3);
                SetDsFlowL2L3HashKey(V, hashKeyType1_f, ds_hash_key, FLOWHASHTYPE_L2L3);
                break;

            default:
                return CTC_E_INVALID_PARAM;
        }

        in.tbl_id    = key_id;
        in.data      = ds_hash_key;
        in.key_index = index;

        CTC_ERROR_RETURN(drv_goldengate_cpu_acc(lchip, DRV_CPU_ADD_ACC_FLOW_HASH_BY_IDX, &in, NULL));

    }
    else if (pe->key.type == CTC_ACL_KEY_PBR_IPV4)
    {
        index = pe->fpae.offset_a;
        DRV_TCAM_TBL_REMOVE(lchip, key_id, index);
    }
    else if (pe->key.type == CTC_ACL_KEY_PBR_IPV6)
    {
        index = (pe->fpae.offset_a - p_gg_acl_master[lchip]->block[5].fpab.start_offset[1]) / 2;
        DRV_TCAM_TBL_REMOVE(lchip, key_id, index);
    }
    else
    {
        _sys_goldengate_acl_get_fpa_size(lchip, pe, &step);
        DRV_TCAM_TBL_REMOVE(lchip, key_id, index/step);
    }

    return CTC_E_NONE;
}


/*
 * add entry specified by entry id to hardware table
 */
STATIC int32
_sys_goldengate_acl_add_hw(uint8 lchip, uint32 eid, bool is_update)
{
    uint8                block_id  = 0;
    uint32               key_index = 0;
    uint32               ad_index  = 0;
    uint16               pbr_index = 0;
    uint32               cmd       = 0;
    uint8                step      = 0;

    uint32               key_id = 0;
    uint32               act_id = 0;

    sys_acl_group_t      * pg = NULL;
    sys_acl_entry_t      * pe = NULL;

    sys_acl_group_info_t * p_info;
    drv_acl_group_info_t drv_info;

    tbl_entry_t          tcam_key;

    ds_t                 data1;
    ds_t                 data2;

    uint32*              ds_ad      = data1;
    uint32*              ds_edit    = data2;
    uint32*              data       = data1;
    uint32*              mask       = data2;
    uint32*              ds_hash_key= data1;

    char key_name[50] = {0};
    char ad_name[50] = {0};

    sal_memset(&data1, 0, sizeof(data1));
    sal_memset(&data2, 0, sizeof(data2));

    sal_memset(&tcam_key, 0, sizeof(tcam_key));
    sal_memset(&drv_info, 0, sizeof(drv_info));

    SYS_ACL_DBG_FUNC();
    SYS_ACL_DBG_PARAM("  %% PARAM: eid: %u \n", eid);

    /* get sys entry */
    _sys_goldengate_acl_get_nodes_by_eid(lchip, eid, &pe, &pg, NULL);
    if(!pe)
    {
        return CTC_E_ACL_ENTRY_UNEXIST;
    }
    if(!pg)
    {
        return CTC_E_ACL_GROUP_UNEXIST;
    }

    /* get block_id, ad_index */
    block_id  = pg->group_info.block_id;

    _sys_goldengate_acl_get_table_id(lchip, pe, &key_id, &act_id);
    drv_goldengate_get_tbl_string_by_id(key_id, key_name);
    drv_goldengate_get_tbl_string_by_id(act_id, ad_name);

    SYS_ACL_DBG_INFO("  %% key_tbl_id:[%s]  ad_tbl_id:[%s]  \n", key_name, ad_name);
    if (!ACL_ENTRY_IS_TCAM(pe->key.type))
    {
        key_index = pe->fpae.offset_a;
        ad_index = pe->action->chip.ad_index;
    }
    else if (pe->key.type == CTC_ACL_KEY_PBR_IPV4)
    {
        key_index = pe->fpae.offset_a;
        ad_index = pe->fpae.offset_a;
        pbr_index = pe->action->chip.ad_index;
    }
    else if (pe->key.type == CTC_ACL_KEY_PBR_IPV6)
    {
        key_index = (pe->fpae.offset_a - p_gg_acl_master[lchip]->block[5].fpab.start_offset[1]) / 2;
        ad_index = key_index;
        pbr_index = pe->action->chip.ad_index;
    }
    else
    {
        _sys_goldengate_acl_get_fpa_size(lchip, pe, &step);
        key_index = pe->fpae.offset_a / step;
        ad_index = pe->fpae.offset_a;
    }

    SYS_ACL_DBG_INFO("  %% key_index:[0x%x]  ad_index:[0x%x] pbr_index:[0x%x] \n", key_index,ad_index,pbr_index);

    /* get group_info */
    p_info = &(pg->group_info);

    /* build drv group_info */
    CTC_ERROR_RETURN(_sys_goldengate_acl_build_group_info(lchip, p_info, &drv_info));


    if ((pe->key.type == CTC_ACL_KEY_PBR_IPV4) || (pe->key.type == CTC_ACL_KEY_PBR_IPV6))
    {
        CTC_ERROR_RETURN(_sys_goldengate_acl_build_pbr_ds_action(lchip, pe->action, ds_ad));
        cmd = DRV_IOW(DsIpDa_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, pbr_index, cmd, ds_ad));

        sal_memset(ds_ad, 0, sizeof(data1));
        SetDsLpmTcamAd1(V, nexthop_f, ds_ad, pbr_index);
    }
    else
    {
        CTC_ERROR_RETURN(_sys_goldengate_acl_build_ds_action(lchip, pe->action, ds_ad, ds_edit));
    }
    cmd = DRV_IOW(act_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, ad_index, cmd, ds_ad));

    if (CTC_FLAG_ISSET(pe->action->flag, CTC_ACL_ACTION_FLAG_VLAN_EDIT))
    {
        uint32 cmd_edit = DRV_IOW(DsAclVlanActionProfile_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, pe->action->chip.profile_id, cmd_edit, ds_edit));
    }

    /* update  tcam action no need to write key, but hash need to write ad index in key */
    if (is_update && ACL_ENTRY_IS_TCAM(pe->key.type))
    {
        return CTC_E_NONE;
    }

    sal_memset(&data1, 0, sizeof(data1));
    sal_memset(&data2, 0, sizeof(data2));
    /* get key, build drv_key. write asic */
    SYS_ACL_DBG_INFO("  %% key_type: %d block_id %d\n", pe->key.type, block_id);

    if (!ACL_ENTRY_IS_TCAM(pe->key.type))
    {
        drv_cpu_acc_in_t in;
        sal_memset(&in, 0, sizeof(in));

        if (CTC_ACL_KEY_HASH_MAC == pe->key.type)
        {
            _sys_goldengate_acl_build_hash_mac_key(lchip, pe, (void *) ds_hash_key, ad_index);
        }
        else if (CTC_ACL_KEY_HASH_IPV4 == pe->key.type)
        {
            _sys_goldengate_acl_build_hash_ipv4_key(lchip, pe, (void *) ds_hash_key, ad_index);
        }
        else if (CTC_ACL_KEY_HASH_IPV6 == pe->key.type)
        {
            _sys_goldengate_acl_build_hash_ipv6_key(lchip, pe, (void *) ds_hash_key, ad_index);
        }
        else if (CTC_ACL_KEY_HASH_MPLS == pe->key.type)
        {
            _sys_goldengate_acl_build_hash_mpls_key(lchip, pe, (void *) ds_hash_key, ad_index);
        }
        else if (CTC_ACL_KEY_HASH_L2_L3 == pe->key.type)
        {
            _sys_goldengate_acl_build_hash_l2_l3_key(lchip, pe, (void *) ds_hash_key, ad_index);
        }

        in.tbl_id    = key_id;
        in.data      = ds_hash_key;
        in.key_index = key_index;

        CTC_ERROR_RETURN(drv_goldengate_cpu_acc(lchip, DRV_CPU_ADD_ACC_FLOW_HASH_BY_IDX, &in, NULL));
    }
    else
    {
        if ((CTC_ACL_KEY_MAC == pe->key.type) && (0 == p_gg_acl_master[lchip]->ipv4_160bit_mode_en))
        {
            _sys_goldengate_acl_build_mac_key(lchip, &drv_info, &pe->key.u0.mac_key, data, mask);
        }
        else if (CTC_ACL_KEY_MAC == pe->key.type)
        {
            _sys_goldengate_acl_build_mac_key_double(lchip, &drv_info, &pe->key.u0.mac_key, data, mask);
        }
        else if ((pe->key.type == CTC_ACL_KEY_IPV4) &&
                 (CTC_ACL_KEY_SIZE_DOUBLE == pe->key.u0.ipv4_key.key_size))
        {
            _sys_goldengate_acl_build_ipv4_key_double(lchip, &drv_info, &pe->key.u0.ipv4_key, data, mask);
        }
        else if (pe->key.type == CTC_ACL_KEY_IPV4)
        {
            _sys_goldengate_acl_build_ipv4_key_single(lchip, &drv_info, &pe->key.u0.ipv4_key, data, mask);
        }
        else if (pe->key.type == CTC_ACL_KEY_IPV6)
        {
            _sys_goldengate_acl_build_ipv6_key(lchip, &drv_info, &pe->key.u0.ipv6_key, data, mask);
        }
        else if (pe->key.type == CTC_ACL_KEY_PBR_IPV4)
        {
            _sys_goldengate_acl_build_pbr_ds_ipv4_key(lchip, &drv_info, &pe->key.u0.pbr_ipv4_key, data, mask);
        }
        else if (pe->key.type == CTC_ACL_KEY_PBR_IPV6)
        {
            _sys_goldengate_acl_build_pbr_ds_ipv6_key(lchip, &drv_info, &pe->key.u0.pbr_ipv6_key, data, mask);
        }

        tcam_key.data_entry = data;
        tcam_key.mask_entry = mask;
        cmd = DRV_IOW(key_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, key_index, cmd, &tcam_key));
    }


    return CTC_E_NONE;
}

/*
 * install entry to hardware table
 */
STATIC int32
_sys_goldengate_acl_install_entry(uint8 lchip, uint32 eid, uint8 flag, uint8 move_sw)
{
    sys_acl_group_t* pg = NULL;
    sys_acl_entry_t* pe = NULL;

    SYS_ACL_DBG_FUNC();
    SYS_ACL_DBG_PARAM("  %% PARAM: eid %u \n", eid);

    /* get sys entry */
    _sys_goldengate_acl_get_nodes_by_eid(lchip, eid, &pe, &pg, NULL);
    if(!pe)
    {
        return CTC_E_ACL_ENTRY_UNEXIST;
    }
    if(!pg)
    {
        return CTC_E_ACL_GROUP_UNEXIST;
    }

    if (SYS_ACL_ENTRY_OP_FLAG_ADD == flag)
    {
#if 0
        if (FPA_ENTRY_FLAG_INSTALLED == pe->fpae.flag)
        {   /* already installed */
            return CTC_E_NONE;
        }
#endif         /* _if0_*/

        CTC_ERROR_RETURN(_sys_goldengate_acl_add_hw(lchip, eid, FALSE));
        pe->fpae.flag = FPA_ENTRY_FLAG_INSTALLED;

        if (move_sw)
        {
            /* move to tail */
            ctc_slist_delete_node(pg->entry_list, &(pe->head));
            ctc_slist_add_tail(pg->entry_list, &(pe->head));
        }
    }
    else if (SYS_ACL_ENTRY_OP_FLAG_DELETE == flag)
    {
#if 0
        if (FPA_ENTRY_FLAG_UNINSTALLED == pe->fpae.flag)
        {   /* already uninstalled */
            return CTC_E_NONE;
        }
#endif         /* _if0_*/

        CTC_ERROR_RETURN(_sys_goldengate_acl_remove_hw(lchip, eid));
        pe->fpae.flag = FPA_ENTRY_FLAG_UNINSTALLED;

        if (move_sw)
        {
            /* move to head */
            ctc_slist_delete_node(pg->entry_list, &(pe->head));
            ctc_slist_add_head(pg->entry_list, &(pe->head));
        }
    }

    return CTC_E_NONE;
}

#define __ACL_FPA_CALLBACK__

STATIC int32
_sys_goldengate_acl_get_block_by_pe_fpa(ctc_fpa_entry_t* pe, ctc_fpa_block_t** pb)
{
    uint8          block_id;
    sys_acl_entry_t* entry;
    uint8 lchip = 0;

    CTC_PTR_VALID_CHECK(pe);
    CTC_PTR_VALID_CHECK(pb);

    *pb = NULL;
    lchip = pe->lchip;
    entry = ACL_OUTER_ENTRY(pe);
    CTC_PTR_VALID_CHECK(entry->group);

    SYS_ACL_GET_BLOCK_ID(entry, block_id);

    *pb = &(p_gg_acl_master[lchip]->block[block_id].fpab);

    return CTC_E_NONE;
}


/*
 * move entry in hardware table to an new index.
 */
STATIC int32
_sys_goldengate_acl_entry_move_hw_fpa(ctc_fpa_entry_t* pe, int32 tcam_idx_new)
{
    uint8  lchip = 0;
    uint32 tcam_idx_old = pe->offset_a;
    uint32 eid;

    CTC_PTR_VALID_CHECK(pe);
    SYS_ACL_DBG_FUNC();

    lchip = pe->lchip;

    eid = pe->entry_id;

    /* add first */
    pe->offset_a = tcam_idx_new;
    CTC_ERROR_RETURN(_sys_goldengate_acl_add_hw(lchip, eid, FALSE));

    /* then delete */
    pe->offset_a = tcam_idx_old;
    CTC_ERROR_RETURN(_sys_goldengate_acl_remove_hw(lchip, eid));

    /* set new_index */
    pe->offset_a = tcam_idx_new;

    return CTC_E_NONE;
}

#define __ACL_L4_INFO_MAPPED__

int32
sys_goldengate_acl_show_tcp_flags(uint8 lchip)
{
    uint8             idx1;
    sys_acl_tcp_flag_t* p_tcp = NULL;
    char str[35] = {0};
    char format[10] = {0};

    SYS_ACL_INIT_CHECK(lchip);
    SYS_ACL_LOCK(lchip);
    p_tcp = p_gg_acl_master[lchip]->tcp_flag;

    SYS_ACL_DBG_FUNC();

    SYS_ACL_DBG_DUMP("%-10s%-12s%-13s%s\n", "Index", "OP", "TCP_FLAG", "REF_CNT");
    SYS_ACL_DBG_DUMP("------------------------------------------\n");

    for (idx1 = 0; idx1 < SYS_ACL_TCP_FLAG_NUM; idx1++)
    {
        if (p_tcp[idx1].ref)
        {
            SYS_ACL_DBG_DUMP("%-10u%-12s%-13s%u\n", idx1, (p_tcp[idx1].match_any) ? "match_any" : "match_all", \
                             CTC_DEBUG_HEX_FORMAT(str, format, (p_tcp[idx1].tcp_flags), 2, U), p_tcp[idx1].ref);
        }
    }
    SYS_ACL_UNLOCK(lchip);

    return CTC_E_NONE;
}

int32
sys_goldengate_acl_show_port_range(uint8 lchip)
{
    uint8              idx1;
    sys_acl_l4port_op_t* p_pop = NULL;
    SYS_ACL_INIT_CHECK(lchip);

    SYS_ACL_LOCK(lchip);
    p_pop = p_gg_acl_master[lchip]->l4_port;

    SYS_ACL_DBG_FUNC();

    SYS_ACL_DBG_DUMP("%-10s%-12s%-8s%-8s%s\n", "Index", "OP", "MIN", "MAX", "REF_CNT");
    SYS_ACL_DBG_DUMP("---------------------------------------------\n");

    for (idx1 = 0; idx1 < p_gg_acl_master[lchip]->l4_port_range_num; idx1++)
    {
        if (p_pop[idx1].ref)
        {
            SYS_ACL_DBG_DUMP("%-10u%-12s%-8u%-8u%u\n",
                              idx1, (p_pop[idx1].op_dest_port) ? "dest_port" : "src_port ",
                              p_pop[idx1].port_min, p_pop[idx1].port_max, p_pop[idx1].ref);
        }
    }
    SYS_ACL_UNLOCK(lchip);

    return CTC_E_NONE;
}

int32
sys_goldengate_acl_show_pkt_len_range(uint8 lchip)
{
    uint32 cmd = 0;
    uint32 min = 0;
    uint32 max = 0;

    SYS_ACL_INIT_CHECK(lchip);
    SYS_ACL_DBG_FUNC();
    SYS_ACL_DBG_DUMP("\n");

    SYS_ACL_LOCK(lchip);
    if(p_gg_acl_master[lchip]->pkt_len_ref_cnt)
    {
        cmd = DRV_IOR(ParserLayer3LengthOpCtl_t, ParserLayer3LengthOpCtl_array_1_length_f);
        CTC_ERROR_RETURN_ACL_UNLOCK(DRV_IOCTL(lchip, 0, cmd, &min));
        cmd = DRV_IOR(ParserLayer3LengthOpCtl_t, ParserLayer3LengthOpCtl_array_2_length_f);
        CTC_ERROR_RETURN_ACL_UNLOCK(DRV_IOCTL(lchip, 0, cmd, &max));

        SYS_ACL_DBG_DUMP("  Length Range   [%u, %u]\n", min, max - 1);
        SYS_ACL_DBG_DUMP("  %u ACE(s) use it.\n", p_gg_acl_master[lchip]->pkt_len_ref_cnt);
    }
    else
    {
        SYS_ACL_DBG_DUMP("  No Length Range Configured.\n");
    }
    SYS_ACL_UNLOCK(lchip);

    SYS_ACL_DBG_DUMP("\n");

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_acl_lkup_tcp_flag(uint8 lchip, uint8 match_any, uint8 tcp_flags,
                                  uint8* avail_idx, uint8* lkup_idx)
{
    uint8                      idx1;
    uint8                      hit     = 0;
    sys_acl_tcp_flag_t         * p_tcp = NULL;
    sys_parser_l4flag_op_ctl_t parser;

    p_tcp = p_gg_acl_master[lchip]->tcp_flag;

    CTC_PTR_VALID_CHECK(avail_idx);
    CTC_PTR_VALID_CHECK(lkup_idx);
    SYS_ACL_DBG_FUNC();

    sal_memset(&parser, 0, sizeof(sys_parser_l4flag_op_ctl_t));

    for (idx1 = 0; idx1 < SYS_ACL_TCP_FLAG_NUM; idx1++)
    {
        if ((p_tcp[idx1].ref > 0) &&
            (p_tcp[idx1].match_any == match_any)
            && (p_tcp[idx1].tcp_flags == (tcp_flags & 0x3F)))
        {   /* found exactly matched entry */
            CTC_NOT_EQUAL_CHECK(p_tcp[idx1].ref, SYS_ACL_TCP_FALG_REF_CNT);

            SYS_ACL_DBG_INFO("share tcp flag, index = %u, match_any = %u, tcp_flags = 0x%x, ref = %u\n",
                             idx1, match_any, tcp_flags, p_tcp[idx1].ref);
            break;
        }
        else if ((p_tcp[idx1].ref == 0)
                 && (0 == hit))
        {   /* first available entry */
            *avail_idx = idx1;
            hit        = 1;
        }
    }

    *lkup_idx = idx1;
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_acl_add_tcp_flag(uint8 lchip, uint8 match_any, uint8 tcp_flags,
                                 uint8 target_idx)
{
    sys_acl_tcp_flag_t         * p_tcp = NULL;
    sys_parser_l4flag_op_ctl_t parser;

    p_tcp = p_gg_acl_master[lchip]->tcp_flag;

    CTC_MAX_VALUE_CHECK(target_idx, SYS_ACL_TCP_FLAG_NUM - 1);

    SYS_ACL_DBG_FUNC();

    sal_memset(&parser, 0, sizeof(sys_parser_l4flag_op_ctl_t));

    /* write to hardware */

    parser.op_and_or = match_any;
    if (match_any)
    {   /* match any.*/
        parser.flags_mask = tcp_flags;
    }
    else
    {                                            /* match all */
        parser.flags_mask = (~tcp_flags & 0x3F); /* make 1 as care, 0 as don't care.*/
    }

    CTC_ERROR_RETURN(sys_goldengate_parser_set_layer4_flag_op_ctl(lchip, target_idx, &parser));

    /* write to software */
    p_tcp[target_idx].ref       = 1;
    p_tcp[target_idx].match_any = match_any;
    p_tcp[target_idx].tcp_flags = tcp_flags & 0x3F;

    if (p_gg_acl_master[lchip]->tcp_flag_free_cnt > 0)
    {
        p_gg_acl_master[lchip]->tcp_flag_free_cnt--;
    }

    SYS_ACL_DBG_INFO("add tcp flag, index = %u, match_any = %u, flags = 0x%x\n",
                     target_idx, match_any, tcp_flags);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_acl_remove_tcp_flag(uint8 lchip, uint8 index)
{
    sys_acl_tcp_flag_t         * p_tcp = NULL;

    sys_parser_l4flag_op_ctl_t parser;

    p_tcp = p_gg_acl_master[lchip]->tcp_flag;

    CTC_MAX_VALUE_CHECK(index, SYS_ACL_TCP_FLAG_NUM - 1);
    SYS_ACL_DBG_FUNC();

    sal_memset(&parser, 0, sizeof(sys_parser_l4flag_op_ctl_t));

    if (0 == p_tcp[index].ref)
    {
        return CTC_E_NONE;
    }

    p_tcp[index].ref--;

    if (0 == p_tcp[index].ref)
    {
        /* write to hardware */
        parser.op_and_or  = 0;
        parser.flags_mask = 0;

        CTC_ERROR_RETURN(sys_goldengate_parser_set_layer4_flag_op_ctl(lchip, index, &parser));

        /* write to software */
        p_tcp[index].match_any = 0;
        p_tcp[index].tcp_flags = 0;
    }

    if (p_gg_acl_master[lchip]->tcp_flag_free_cnt < SYS_ACL_TCP_FLAG_NUM)
    {
        p_gg_acl_master[lchip]->tcp_flag_free_cnt++;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_acl_lkup_l4_port_range(uint8 lchip, uint16 port_min, uint16 port_max,
                                       uint8 op_dest_port,
                                       uint8* avail_idx, uint8* lkup_idx)
{
    uint8              idx1;
    uint8              hit     = 0;
    sys_acl_l4port_op_t* p_pop = NULL;

    p_pop = p_gg_acl_master[lchip]->l4_port;

    CTC_PTR_VALID_CHECK(avail_idx);
    CTC_PTR_VALID_CHECK(lkup_idx);
    SYS_ACL_DBG_FUNC();

    for (idx1 = 0; idx1 < p_gg_acl_master[lchip]->l4_port_range_num; idx1++)
    {
        if ((p_pop[idx1].ref > 0) &&
            (p_pop[idx1].port_min == port_min)
            && (p_pop[idx1].port_max == port_max)
            && (p_pop[idx1].op_dest_port == op_dest_port))
        {   /* found exactly matched entry */
            CTC_NOT_EQUAL_CHECK(p_pop[idx1].ref, SYS_ACL_L4_PORT_REF_CNT);

            SYS_ACL_DBG_INFO("share l4 port entry, index = %u, op_dest_port = %u, \
                                      port_min = % d, port_max = % d, ref = % d\n",
                             idx1, op_dest_port, port_min, port_max, p_pop[idx1].ref);
            break;
        }
        else if ((p_pop[idx1].ref == 0)
                 && (0 == hit))
        {   /* first available entry */
            *avail_idx = idx1;
            hit        = 1;
        }
    }

    *lkup_idx = idx1;

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_acl_add_l4_port_range(uint8 lchip, uint16 port_min, uint16 port_max,
                                      uint8 op_dest_port, uint8 target_idx)
{
    uint8                       idx1;
    sys_parser_l4_port_op_sel_t sel;
    sys_parser_l4_port_op_ctl_t ctl;
    sys_acl_l4port_op_t         * p_pop = NULL;

    p_pop = p_gg_acl_master[lchip]->l4_port;

    CTC_MAX_VALUE_CHECK(target_idx, p_gg_acl_master[lchip]->l4_port_range_num - 1);
    SYS_ACL_DBG_FUNC();

    sal_memset(&sel, 0, sizeof(sys_parser_l4_port_op_sel_t));
    sal_memset(&ctl, 0, sizeof(sys_parser_l4_port_op_ctl_t));

    /* write to hardware */
    /* get sel from sw */
    for (idx1 = 0; idx1 < p_gg_acl_master[lchip]->l4_port_range_num; idx1++)
    {
        if (p_pop[idx1].op_dest_port)
        {
            SET_BIT(sel.op_dest_port, idx1);
        }
    }

    /* set new sel */
    if (op_dest_port)
    {
        SET_BIT(sel.op_dest_port, target_idx);
    }

    /* set new ctl */
    ctl.layer4_port_min = port_min;
    ctl.layer4_port_max = port_max;

    CTC_ERROR_RETURN(sys_goldengate_parser_set_layer4_port_op_sel(lchip, &sel));
    CTC_ERROR_RETURN(sys_goldengate_parser_set_layer4_port_op_ctl(lchip, target_idx, &ctl));

    /* write to software */
    /* set sel */
    if (op_dest_port)
    {
        p_pop[target_idx].op_dest_port = 1;
    }

    /* set ctl */
    p_pop[target_idx].ref      = 1;
    p_pop[target_idx].port_min = port_min;
    p_pop[target_idx].port_max = port_max;

    if (p_gg_acl_master[lchip]->l4_port_free_cnt > 0)
    {
        p_gg_acl_master[lchip]->l4_port_free_cnt--;
    }

    SYS_ACL_DBG_INFO("add l4 port entry, index = %u, op_dest_port = %u, \
                     port_min = % d, port_max = % d\n",
                     target_idx, op_dest_port, port_min, port_max);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_acl_remove_l4_port_range(uint8 lchip, uint8 index)
{
    uint8                       idx1;
    sys_parser_l4_port_op_sel_t sel;
    sys_parser_l4_port_op_ctl_t ctl;
    sys_acl_l4port_op_t         * p_pop = NULL;

    p_pop = p_gg_acl_master[lchip]->l4_port;

    CTC_MAX_VALUE_CHECK(index, p_gg_acl_master[lchip]->l4_port_range_num - 1);

    SYS_ACL_DBG_FUNC();

    sal_memset(&sel, 0, sizeof(sys_parser_l4_port_op_sel_t));
    sal_memset(&ctl, 0, sizeof(sys_parser_l4_port_op_ctl_t));

    if (0 == p_pop[index].ref)
    {
        return CTC_E_NONE;
    }

    p_pop[index].ref--;

    if (0 == p_pop[index].ref)
    {
        /* write to asic */
        /* get sel from sw */
        for (idx1 = 0; idx1 < p_gg_acl_master[lchip]->l4_port_range_num; idx1++)
        {
            if (p_pop[idx1].op_dest_port)
            {
                SET_BIT(sel.op_dest_port, idx1);
            }
        }

        /* set new sel */
        CLEAR_BIT(sel.op_dest_port, index);

        /* clear ctl is enough */

        /* sys_goldengate_parser_set_layer4_port_op_sel(lchip, ACL_LCHIP, &sel); */
        sys_goldengate_parser_set_layer4_port_op_ctl(lchip, index, &ctl);

        /* write to db */
        p_pop[index].port_min     = 0;
        p_pop[index].port_max     = 0;
        p_pop[index].op_dest_port = 0;
    }

    if (p_gg_acl_master[lchip]->l4_port_free_cnt < p_gg_acl_master[lchip]->l4_port_range_num)
    {
        p_gg_acl_master[lchip]->l4_port_free_cnt++;
    }

    return CTC_E_NONE;
}

/*
 * below is build sw struct
 */
#define __ACL_HASH_CALLBACK__


STATIC uint32
_sys_goldengate_acl_hash_make_group(sys_acl_group_t* pg)
{
    return pg->group_id;
}

STATIC bool
_sys_goldengate_acl_hash_compare_group(sys_acl_group_t* pg0, sys_acl_group_t* pg1)
{
    return(pg0->group_id == pg1->group_id);
}

STATIC uint32
_sys_goldengate_acl_hash_make_entry(sys_acl_entry_t* pe)
{
    return pe->fpae.entry_id;
}

STATIC bool
_sys_goldengate_acl_hash_compare_entry(sys_acl_entry_t* pe0, sys_acl_entry_t* pe1)
{
    return(pe0->fpae.entry_id == pe1->fpae.entry_id);
}

STATIC uint32
_sys_goldengate_acl_hash_make_entry_by_key(sys_acl_entry_t* pe)
{
    uint32 size;
    uint8  * k;
    uint8 lchip;
    CTC_PTR_VALID_CHECK(pe);
    lchip = pe->lchip;
    size = p_gg_acl_master[lchip]->key_size[pe->key.type];
    k    = (uint8 *) (&pe->key);
    return ctc_hash_caculate(size, k);
}
STATIC bool
_sys_goldengate_acl_hash_compare_entry_by_key(sys_acl_entry_t* pe0, sys_acl_entry_t* pe1)
{
    uint8 lchip;

    if (!pe0 || !pe1)
    {
        return FALSE;
    }
    lchip = pe0->lchip;

    if (!sal_memcmp(&pe0->key, &pe1->key, p_gg_acl_master[lchip]->key_size[pe0->key.type]))
    {
        return TRUE;
    }

    return FALSE;
}
STATIC uint32
_sys_goldengate_acl_hash_make_action(sys_acl_action_t* pa)
{
    uint32 size;
    uint8  * k;
    uint8 lchip;
    CTC_PTR_VALID_CHECK(pa);
    lchip = pa->lchip;
    size = p_gg_acl_master[lchip]->hash_action_size;  /* only care prop not related to chip */
    k    = (uint8 *) pa;
    return ctc_hash_caculate(size, k);
}

STATIC bool
_sys_goldengate_acl_hash_compare_action(sys_acl_action_t* pa0, sys_acl_action_t* pa1)
{
    uint8 lchip;

    if (!pa0 || !pa1)
    {
        return FALSE;
    }
    lchip = pa0->lchip;

    if ((pa0->chip.fwdptr == pa1->chip.fwdptr)
        && (pa0->chip.micro_policer_ptr == pa1->chip.micro_policer_ptr)
        && (pa0->chip.macro_policer_ptr == pa1->chip.macro_policer_ptr)
        && (pa0->chip.stats_ptr == pa1->chip.stats_ptr)
        && (pa0->chip.profile_id == pa1->chip.profile_id)
        && (!sal_memcmp(pa0, pa1, p_gg_acl_master[lchip]->hash_action_size)))
    {
        return TRUE;
    }

    return FALSE;
}



/* if vlan edit in bucket equals */
STATIC bool
_sys_goldengate_acl_hash_compare_vlan_edit(sys_acl_vlan_edit_t* pv0,
                                           sys_acl_vlan_edit_t* pv1)
{
    uint32 size = 0;
    uint8 lchip = 0;;

    if (!pv0 || !pv1)
    {
        return FALSE;
    }
    lchip = pv0->lchip;

    size = p_gg_acl_master[lchip]->vlan_edit_size;
    if (!sal_memcmp(pv0, pv1, size))
    {
        return TRUE;
    }

    return FALSE;
}

STATIC uint32
_sys_goldengate_acl_hash_make_vlan_edit(sys_acl_vlan_edit_t* pv)
{
    uint32 size = 0;

    uint8  * k = NULL;
    uint8 lchip;

    CTC_PTR_VALID_CHECK(pv);
    lchip = pv->lchip;

    size = p_gg_acl_master[lchip]->vlan_edit_size;
    k    = (uint8 *) pv;

    return ctc_hash_caculate(size, k);
}



#define __ACL_MAP_UNMAP__

STATIC int32
_sys_goldengate_acl_check_pbr_action(uint8 lchip, uint32 flag)
{
    uint32 unsupport_flag = 0;
    unsupport_flag = ACL_SUM(CTC_ACL_ACTION_FLAG_PBR_ECMP);
    if (flag & unsupport_flag)
    {
        return CTC_E_FEATURE_NOT_SUPPORT;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_acl_check_action(uint8 lchip, uint8 dir, uint64 flag)
{
    uint64 unsupport_flag = 0;
    unsupport_flag = CTC_ACL_ACTION_FLAG_COPY_TO_CPU_WITH_TIMESTAMP + CTC_ACL_ACTION_FLAG_AGING;


    if (CTC_EGRESS == dir)
    {
        unsupport_flag += CTC_ACL_ACTION_FLAG_PRIORITY_AND_COLOR;
        unsupport_flag += CTC_ACL_ACTION_FLAG_DENY_BRIDGE;
        unsupport_flag += CTC_ACL_ACTION_FLAG_DENY_LEARNING;
        unsupport_flag += CTC_ACL_ACTION_FLAG_DENY_ROUTE;
        unsupport_flag += CTC_ACL_ACTION_FLAG_TRUST;
        unsupport_flag += CTC_ACL_ACTION_FLAG_REDIRECT;
        unsupport_flag += CTC_ACL_ACTION_FLAG_DSCP;
        unsupport_flag += CTC_ACL_ACTION_FLAG_QOS_DOMAIN;
        unsupport_flag += CTC_ACL_ACTION_FLAG_VLAN_EDIT;
        unsupport_flag += CTC_ACL_ACTION_FLAG_PRIORITY;
        unsupport_flag += CTC_ACL_ACTION_FLAG_COLOR;
    }

    if (flag & unsupport_flag)
    {
        return CTC_E_FEATURE_NOT_SUPPORT;
    }

    /* only support one policer */
    if (CTC_FLAG_ISSET(flag, CTC_ACL_ACTION_FLAG_MICRO_FLOW_POLICER)
        && CTC_FLAG_ISSET(flag, CTC_ACL_ACTION_FLAG_MACRO_FLOW_POLICER))
    {
        return CTC_E_ACL_FLAG_COLLISION;
    }

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_acl_check_vlan_edit(uint8 lchip, ctc_acl_vlan_edit_t*   p_ctc, uint8* vlan_edit)
{
    /* check tag op */
    CTC_MAX_VALUE_CHECK(p_ctc->stag_op, CTC_ACL_VLAN_TAG_OP_MAX - 1);
    CTC_MAX_VALUE_CHECK(p_ctc->ctag_op, CTC_ACL_VLAN_TAG_OP_MAX - 1);

    if ((CTC_ACL_VLAN_TAG_OP_NONE == p_ctc->stag_op)
        && (CTC_ACL_VLAN_TAG_OP_NONE == p_ctc->ctag_op))
    {
        *vlan_edit = 0;
        return CTC_E_NONE;
    }

    *vlan_edit = 1;

    if ((CTC_ACL_VLAN_TAG_OP_ADD == p_ctc->stag_op) ||
        (CTC_ACL_VLAN_TAG_OP_REP == p_ctc->stag_op) ||
        (CTC_ACL_VLAN_TAG_OP_REP_OR_ADD == p_ctc->stag_op) )
    {
        CTC_MAX_VALUE_CHECK(p_ctc->svid_sl, CTC_ACL_VLAN_TAG_SL_MAX - 1);
        CTC_MAX_VALUE_CHECK(p_ctc->scos_sl, CTC_ACL_VLAN_TAG_SL_MAX - 1);
        CTC_MAX_VALUE_CHECK(p_ctc->scfi_sl, CTC_ACL_VLAN_TAG_SL_MAX - 1);

        if (CTC_ACL_VLAN_TAG_SL_NEW == p_ctc->svid_sl)
        {
            CTC_VLAN_RANGE_CHECK(p_ctc->svid_new);
        }
        if (CTC_ACL_VLAN_TAG_SL_NEW == p_ctc->scos_sl)
        {
            CTC_COS_RANGE_CHECK(p_ctc->scos_new);
        }
        if (CTC_ACL_VLAN_TAG_SL_NEW == p_ctc->scfi_sl)
        {
            CTC_MAX_VALUE_CHECK(p_ctc->scfi_new, 1);
        }
    }

    if ((CTC_ACL_VLAN_TAG_OP_ADD == p_ctc->ctag_op) ||
        (CTC_ACL_VLAN_TAG_OP_REP == p_ctc->ctag_op) ||
        (CTC_ACL_VLAN_TAG_OP_REP_OR_ADD == p_ctc->ctag_op) )
    {
        CTC_MAX_VALUE_CHECK(p_ctc->cvid_sl, CTC_ACL_VLAN_TAG_SL_MAX - 1);
        CTC_MAX_VALUE_CHECK(p_ctc->ccos_sl, CTC_ACL_VLAN_TAG_SL_MAX - 1);
        CTC_MAX_VALUE_CHECK(p_ctc->ccfi_sl, CTC_ACL_VLAN_TAG_SL_MAX - 1);

        if (CTC_ACL_VLAN_TAG_SL_NEW == p_ctc->cvid_sl)
        {
            CTC_VLAN_RANGE_CHECK(p_ctc->cvid_new);
        }
        if (CTC_ACL_VLAN_TAG_SL_NEW == p_ctc->ccos_sl)
        {
            CTC_COS_RANGE_CHECK(p_ctc->ccos_new);
        }
        if (CTC_ACL_VLAN_TAG_SL_NEW == p_ctc->ccfi_sl)
        {
            CTC_MAX_VALUE_CHECK(p_ctc->ccfi_new, 1);
        }
    }
    CTC_MAX_VALUE_CHECK(p_ctc->tpid_index, 3);

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_acl_map_action(uint8 lchip, ctc_acl_action_t* p_ctc_action,
                               sys_acl_action_t* p_sys_action,
                               sys_acl_vlan_edit_t* p_vlan_edit)
{
    uint16        policer_ptr;
    uint32        dsfwd_offset;
    sys_nh_info_dsnh_t nhinfo;
    uint8 is_bwp;
    uint8 triple_play;
	uint8 have_dsfwd = 0;

    CTC_PTR_VALID_CHECK(p_ctc_action);
    CTC_PTR_VALID_CHECK(p_sys_action);

    sal_memset(&nhinfo, 0, sizeof(nhinfo));

    SYS_ACL_DBG_FUNC();

    p_sys_action->flag = p_ctc_action->flag;

    if ((CTC_FLAG_ISSET(p_ctc_action->flag, CTC_ACL_ACTION_FLAG_METADATA)
         + CTC_FLAG_ISSET(p_ctc_action->flag, CTC_ACL_ACTION_FLAG_FID)
         + CTC_FLAG_ISSET(p_ctc_action->flag, CTC_ACL_ACTION_FLAG_VRFID)) > 1)
    {
        return CTC_E_ACL_FLAG_COLLISION;
    }

    if(CTC_FLAG_ISSET(p_ctc_action->flag, CTC_ACL_ACTION_FLAG_REDIRECT_WITH_RAW_PKT) && p_ctc_action->packet_strip.start_packet_strip)
    {
        /* can not config redirect with raw pkt and strip pkt */
        return CTC_E_ACL_FLAG_COLLISION;
    }

    if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_ACL_ACTION_FLAG_STATS))
    {
        CTC_ERROR_RETURN(sys_goldengate_stats_get_statsptr
                             (lchip, p_ctc_action->stats_id, CTC_STATS_STATSID_TYPE_ACL, &p_sys_action->chip.stats_ptr));

        p_sys_action->stats_id = p_ctc_action->stats_id;
    }

    /* micro flow policer */
    if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_ACL_ACTION_FLAG_MICRO_FLOW_POLICER))
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
    if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_ACL_ACTION_FLAG_MACRO_FLOW_POLICER))
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

    /* random log */
    if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_ACL_ACTION_FLAG_RANDOM_LOG))
    {
        CTC_VALUE_RANGE_CHECK(p_ctc_action->log_percent,
                              CTC_LOG_PERCENT_POWER_NEGATIVE_14, CTC_LOG_PERCENT_POWER_NEGATIVE_0);
        CTC_MAX_VALUE_CHECK(p_ctc_action->log_session_id, SYS_ACL_MAX_SESSION_NUM - 1);

        p_sys_action->acl_log_id             = p_ctc_action->log_session_id;
        p_sys_action->random_threshold_shift = p_ctc_action->log_percent;
    }

    /* priority */
    if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_ACL_ACTION_FLAG_PRIORITY))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_action->priority, SYS_QOS_CLASS_PRIORITY_MAX);
        p_sys_action->priority = p_ctc_action->priority;
    }

    /* color */
    if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_ACL_ACTION_FLAG_COLOR))
    {
        CTC_VALUE_RANGE_CHECK(p_ctc_action->color, 1, MAX_CTC_QOS_COLOR - 1);
        p_sys_action->color = p_ctc_action->color;
    }

    /* priority&color for gb usage */
    if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_ACL_ACTION_FLAG_PRIORITY_AND_COLOR))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_action->priority, SYS_QOS_CLASS_PRIORITY_MAX);
        p_sys_action->priority = p_ctc_action->priority;
        CTC_VALUE_RANGE_CHECK(p_ctc_action->color, 1, MAX_CTC_QOS_COLOR - 1);
        p_sys_action->color = p_ctc_action->color;
    }

    /* qos policy (trust) */
    if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_ACL_ACTION_FLAG_TRUST))
    {
        uint32 trust;
        /* 0 is invalid */
        CTC_MAX_VALUE_CHECK(p_ctc_action->trust, CTC_QOS_TRUST_MAX - 1);
        sys_goldengate_common_map_qos_policy(lchip, p_ctc_action->trust, &trust);
        p_sys_action->trust = trust;
    }

    if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_ACL_ACTION_FLAG_DSCP))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_action->dscp, 0x3F);
        p_sys_action->dscp = p_ctc_action->dscp;
    }

    /* qos domain */
    if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_ACL_ACTION_FLAG_QOS_DOMAIN))
    {
        if ((0 == p_ctc_action->qos_domain) || (p_ctc_action->qos_domain > 7))
        {
            return CTC_E_INVALID_PARAM;
        }

        p_sys_action->qos_domain = p_ctc_action->qos_domain;
    }

    /*strip packet*/
    if (p_ctc_action->packet_strip.start_packet_strip)
    {
        CTC_MAX_VALUE_CHECK(p_ctc_action->packet_strip.start_packet_strip, CTC_ACL_STRIP_MAX - 1);
        CTC_MAX_VALUE_CHECK(p_ctc_action->packet_strip.packet_type, PKT_TYPE_RESERVED - 1);
        CTC_MAX_VALUE_CHECK(p_ctc_action->packet_strip.strip_extra_len, SYS_MAX_STRIP_PKT_LEN-1);
        p_sys_action->packet_strip.start_packet_strip = p_ctc_action->packet_strip.start_packet_strip;
        p_sys_action->packet_strip.packet_type        = p_ctc_action->packet_strip.packet_type;
        p_sys_action->packet_strip.strip_extra_len    = p_ctc_action->packet_strip.strip_extra_len;
	 have_dsfwd = 1;
    }

    if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_ACL_ACTION_FLAG_CANCEL_DISCARD)
	  	|| CTC_FLAG_ISSET(p_ctc_action->flag, CTC_ACL_ACTION_FLAG_DISCARD))
    {
        have_dsfwd  = 1;
    }

    if(have_dsfwd && CTC_FLAG_ISSET(p_ctc_action->flag, CTC_ACL_ACTION_FLAG_ASSIGN_OUTPUT_PORT))
    {
        return CTC_E_ACL_FLAG_COLLISION;
    }
    /* ds forward */
    if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_ACL_ACTION_FLAG_REDIRECT))
    {
        p_sys_action->nh_id = p_ctc_action->nh_id;

        /* get nexthop information */
        CTC_ERROR_RETURN(sys_goldengate_nh_get_nhinfo(lchip, p_ctc_action->nh_id, &nhinfo));
	   nhinfo.merge_dsfwd = (nhinfo.merge_dsfwd == 1) && !have_dsfwd && !nhinfo.dsfwd_valid;

        if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_ACL_ACTION_FLAG_ASSIGN_OUTPUT_PORT))
        {
            p_sys_action->merge_dsfwd = 1;
            p_sys_action->dsnh_offset = nhinfo.dsnh_offset;
            p_sys_action->dest_chipid = SYS_MAP_CTC_GPORT_TO_GCHIP(p_ctc_action->assign_port);
            p_sys_action->dest_id  = SYS_MAP_CTC_GPORT_TO_DRV_LPORT(p_ctc_action->assign_port);
        }
        else if ((nhinfo.merge_dsfwd))
        {
            p_sys_action->merge_dsfwd = 1;
            p_sys_action->dsnh_offset = nhinfo.dsnh_offset;
            p_sys_action->dest_chipid = nhinfo.dest_chipid;
            p_sys_action->is_mcast = nhinfo.is_mcast;
            p_sys_action->aps_en   = nhinfo.aps_en;
            p_sys_action->adjust_len = nhinfo.adjust_len;
             if (nhinfo.is_ecmp_intf)
             {
                    p_sys_action->dest_id  = nhinfo.ecmp_group_id;
                    p_sys_action->dest_chipid = 0x3d;
                    p_sys_action->is_ecmp_intf = 1;
            }
            else
            {
                p_sys_action->dest_id  = nhinfo.dest_id;
            }
        }
        else if (nhinfo.ecmp_valid)
        {
            p_sys_action->ecmp_group_id = nhinfo.ecmp_group_id;
        }
        else
        {
            CTC_ERROR_RETURN(sys_goldengate_nh_get_dsfwd_offset(lchip, p_sys_action->nh_id, &dsfwd_offset));
               p_sys_action->chip.fwdptr = dsfwd_offset;
        }
    }

    /* metadata */
    if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_ACL_ACTION_FLAG_METADATA))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_action->metadata, 0x3FFF);
        p_sys_action->metadata = p_ctc_action->metadata;
        p_sys_action->metadata_type = CTC_METADATA_TYPE_METADATA;
    }
    else if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_ACL_ACTION_FLAG_FID))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_action->fid, 0x3FFF);
        p_sys_action->fid = p_ctc_action->fid;
        p_sys_action->metadata_type = CTC_METADATA_TYPE_FID;
    }
    else if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_ACL_ACTION_FLAG_VRFID))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_action->vrf_id, 0x3FFF);
        p_sys_action->vrfid = p_ctc_action->vrf_id;
        p_sys_action->metadata_type = CTC_METADATA_TYPE_VRFID;
    }

    {
        uint8 vlan_edit = 0;
        if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_ACL_ACTION_FLAG_VLAN_EDIT))
        {
            CTC_ERROR_RETURN(_sys_goldengate_acl_check_vlan_edit(lchip, &p_ctc_action->vlan_edit, &vlan_edit));
        }

        if (vlan_edit)
        {
            p_vlan_edit->stag_op = p_ctc_action->vlan_edit.stag_op;
            p_vlan_edit->ctag_op = p_ctc_action->vlan_edit.ctag_op;
            p_vlan_edit->svid_sl = p_ctc_action->vlan_edit.svid_sl;
            p_vlan_edit->scos_sl = p_ctc_action->vlan_edit.scos_sl;
            p_vlan_edit->scfi_sl = p_ctc_action->vlan_edit.scfi_sl;
            p_vlan_edit->cvid_sl = p_ctc_action->vlan_edit.cvid_sl;
            p_vlan_edit->ccos_sl = p_ctc_action->vlan_edit.ccos_sl;
            p_vlan_edit->ccfi_sl = p_ctc_action->vlan_edit.ccfi_sl;
            p_vlan_edit->tpid_index = p_ctc_action->vlan_edit.tpid_index;

            p_sys_action->svid = p_ctc_action->vlan_edit.svid_new;
            p_sys_action->scos = p_ctc_action->vlan_edit.scos_new;
            p_sys_action->scfi = p_ctc_action->vlan_edit.scfi_new;
            p_sys_action->cvid = p_ctc_action->vlan_edit.cvid_new;
            p_sys_action->ccos = p_ctc_action->vlan_edit.ccos_new;
            p_sys_action->ccfi = p_ctc_action->vlan_edit.ccfi_new;
        }
    }


    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_acl_map_pbr_action(uint8 lchip, ctc_acl_action_t* p_ctc_action,
                                   sys_acl_action_t* p_sys_action)
{
    uint32    dsfwd_offset;
    sys_nh_info_dsnh_t nhinfo;
    sal_memset(&nhinfo, 0, sizeof(nhinfo));

    p_sys_action->flag = p_ctc_action->flag;
    if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_ACL_ACTION_FLAG_PBR_FWD))
    {
        CTC_ERROR_RETURN(sys_goldengate_nh_get_nhinfo(lchip, p_ctc_action->nh_id, &nhinfo));

        if (nhinfo.ecmp_valid)
        {
            if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_ACL_ACTION_FLAG_PBR_ICMP_CHECK))
            {
                return CTC_E_INVALID_PARAM;
            }
            p_sys_action->ecmp_group_id = nhinfo.ecmp_group_id;
        }
        else
        {
            p_sys_action->nh_id = p_ctc_action->nh_id;
            CTC_ERROR_RETURN(sys_goldengate_nh_get_dsfwd_offset(lchip, p_ctc_action->nh_id, &dsfwd_offset));
            p_sys_action->chip.fwdptr = dsfwd_offset;
        }
    }
    else if (!CTC_FLAG_ISSET(p_ctc_action->flag, CTC_ACL_ACTION_FLAG_PBR_DENY))
    {
        return CTC_E_ACL_PBR_ENTRY_NO_NXTTHOP;
    }

    if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_ACL_ACTION_FLAG_PBR_ICMP_CHECK))
    {
        CTC_ERROR_RETURN(sys_goldengate_nh_get_l3ifid(lchip, p_ctc_action->nh_id, &p_sys_action->l3if_id));
        if (p_sys_action->l3if_id == 0)
        {
            return CTC_E_NH_INVALID_NH_TYPE;
        }
    }
    return CTC_E_NONE;
}




STATIC int32
_sys_goldengate_acl_check_mac_key(uint8 lchip, uint32 flag)
{
    uint32 obsolete_flag = 0;
    uint32 sflag         = 0;
    uint32 cflag         = 0;


    if (0 == p_gg_acl_master[lchip]->ipv4_160bit_mode_en)
    {
        sflag = ACL_SUM(CTC_ACL_MAC_KEY_FLAG_SVLAN,
                        CTC_ACL_MAC_KEY_FLAG_STAG_COS,
                        CTC_ACL_MAC_KEY_FLAG_STAG_CFI,
                        CTC_ACL_MAC_KEY_FLAG_STAG_VALID);

        cflag = ACL_SUM(CTC_ACL_MAC_KEY_FLAG_CVLAN,
                        CTC_ACL_MAC_KEY_FLAG_CTAG_COS,
                        CTC_ACL_MAC_KEY_FLAG_CTAG_CFI,
                        CTC_ACL_MAC_KEY_FLAG_CTAG_VALID);

        if ((flag & sflag) && (flag & cflag))
        {
            return CTC_E_INVALID_PARAM;
        }

        if (CTC_FLAG_ISSET(flag, CTC_ACL_MAC_KEY_FLAG_L3_TYPE))
        {
            return CTC_E_FEATURE_NOT_SUPPORT;
        }
    }

    obsolete_flag = ACL_SUM(CTC_ACL_MAC_KEY_FLAG_ARP_OP_CODE,
                            CTC_ACL_MAC_KEY_FLAG_IP_SA,
                            CTC_ACL_MAC_KEY_FLAG_IP_DA,
                            CTC_ACL_MAC_KEY_FLAG_L2_TYPE);

    if (flag & obsolete_flag)
    {
        return CTC_E_FEATURE_NOT_SUPPORT;
    }

    return CTC_E_NONE;
}



STATIC int32
_sys_goldengate_acl_check_ipv4_key(uint8 lchip, uint32 flag, uint8 key_size)
{
    uint32 obsolete_flag = 0;

    if ((CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_PRECEDENCE)
         + CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_DSCP)) > 1)
    {
        return CTC_E_ACL_FLAG_COLLISION;
    }

    obsolete_flag = ACL_SUM(CTC_ACL_IPV4_KEY_FLAG_IPV4_PACKET,
                            CTC_ACL_IPV4_KEY_FLAG_ARP_PACKET,
                            CTC_ACL_IPV4_KEY_FLAG_L2_TYPE,
                            CTC_ACL_IPV4_KEY_FLAG_CTAG_CFI);

    if (CTC_ACL_KEY_SIZE_SINGLE == key_size)
    {
        obsolete_flag += ACL_SUM(CTC_ACL_IPV4_KEY_FLAG_MAC_DA,
                                CTC_ACL_IPV4_KEY_FLAG_MAC_SA,
                                CTC_ACL_IPV4_KEY_FLAG_CVLAN,
                                CTC_ACL_IPV4_KEY_FLAG_CTAG_COS,
                                CTC_ACL_IPV4_KEY_FLAG_SVLAN,
                                CTC_ACL_IPV4_KEY_FLAG_STAG_COS,
                                CTC_ACL_IPV4_KEY_FLAG_STAG_CFI,
                                CTC_ACL_IPV4_KEY_FLAG_STAG_VALID,
                                CTC_ACL_IPV4_KEY_FLAG_CTAG_VALID,
                                CTC_ACL_IPV4_KEY_FLAG_UDF);
    }


    if (CTC_ACL_KEY_SIZE_DOUBLE == key_size)
    {
        if ((CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_METADATA)
             + CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_UDF)) > 1)
        {
            return CTC_E_ACL_FLAG_COLLISION;
        }
    }


    if (flag & obsolete_flag)
    {
        return CTC_E_FEATURE_NOT_SUPPORT;
    }

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_acl_check_ipv6_key(uint8 lchip, uint32 flag)
{
    uint32 obsolete_flag = 0;

    if ((CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_PRECEDENCE)
         + CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_DSCP)) > 1)
    {
        return CTC_E_ACL_FLAG_COLLISION;
    }

    obsolete_flag = ACL_SUM(CTC_ACL_IPV6_KEY_FLAG_ETH_TYPE,
                            CTC_ACL_IPV6_KEY_FLAG_L3_TYPE,
                            CTC_ACL_IPV6_KEY_FLAG_L2_TYPE);

    if (flag & obsolete_flag)
    {
        return CTC_E_FEATURE_NOT_SUPPORT;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_acl_check_pbr_ipv6_key(uint8 lchip, uint32 flag)
{
    uint32 obsolete_flag = 0;

    if (CTC_FLAG_ISSET(flag, CTC_ACL_PBR_IPV6_KEY_FLAG_DSCP)
        && CTC_FLAG_ISSET(flag, CTC_ACL_PBR_IPV6_KEY_FLAG_PRECEDENCE))
    {
        return CTC_E_ACL_FLAG_COLLISION;
    }

    obsolete_flag = ACL_SUM(CTC_ACL_PBR_IPV6_KEY_FLAG_MAC_DA,
                            CTC_ACL_PBR_IPV6_KEY_FLAG_MAC_SA,
                            CTC_ACL_PBR_IPV6_KEY_FLAG_CVLAN,
                            CTC_ACL_PBR_IPV6_KEY_FLAG_CTAG_COS,
                            CTC_ACL_PBR_IPV6_KEY_FLAG_SVLAN,
                            CTC_ACL_PBR_IPV6_KEY_FLAG_STAG_COS,
                            CTC_ACL_PBR_IPV6_KEY_FLAG_ETH_TYPE,
                            CTC_ACL_PBR_IPV6_KEY_FLAG_L2_TYPE,
                            CTC_ACL_PBR_IPV6_KEY_FLAG_L3_TYPE,
                            CTC_ACL_PBR_IPV6_KEY_FLAG_CTAG_CFI,
                            CTC_ACL_PBR_IPV6_KEY_FLAG_STAG_CFI);

    if (flag & obsolete_flag)
    {
        return CTC_E_FEATURE_NOT_SUPPORT;
    }

    return CTC_E_NONE;
}




STATIC int32
_sys_goldengate_acl_map_ip_frag(uint8 lchip, uint8 ctc_ip_frag, uint8* frag_info, uint8* frag_info_mask)
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
_sys_goldengate_acl_map_port_range(uint8 lchip, uint8 op_dest,
                                   uint16 min_port,
                                   uint16 max_port,
                                   sys_acl_l4_t* pc)
{
    uint8 idx_valid  = 0;
    uint8 idx_target = 0;
    uint8 idx_lkup   = 0;

    if (min_port >= max_port)
    {
        return CTC_E_INVALID_PARAM;
    }

    pc->flag_l4info_mapped = 1;

    CTC_ERROR_RETURN(_sys_goldengate_acl_lkup_l4_port_range(lchip,
                         min_port, max_port, op_dest, &idx_target, &idx_lkup));

    if (p_gg_acl_master[lchip]->l4_port_range_num != idx_lkup)
    {
        p_gg_acl_master[lchip]->l4_port[idx_lkup].ref++;

        idx_valid = idx_lkup;
    }
    else if ((p_gg_acl_master[lchip]->l4_port_range_num == idx_lkup)
             && (p_gg_acl_master[lchip]->l4_port_free_cnt > 0))
    {
        CTC_ERROR_RETURN(_sys_goldengate_acl_add_l4_port_range
                             (lchip, min_port, max_port, op_dest, idx_target));

        idx_valid = idx_target;
    }
    else
    {
        return CTC_E_ACL_L4_PORT_RANGE_EXHAUSTED;
    }

    if(p_gg_acl_master[lchip]->acl_register.pkt_len_range_en)
    {
        pc->l4info_mapped      |= 1 << (idx_valid + 4);
        pc->l4info_mapped_mask |= 1 << (idx_valid + 4);
    }
    else
    {
        pc->l4info_mapped      |= 1 << idx_valid;
        pc->l4info_mapped_mask |= 1 << idx_valid;
    }

    if (0 == op_dest)
    {
        pc->flag_l4_src_port_range = 1;
        pc->l4_src_port_range_idx  = idx_valid;
    }
    else
    {
        pc->flag_l4_dst_port_range = 1;
        pc->l4_dst_port_range_idx  = idx_valid;
    }

    return CTC_E_NONE;
}




STATIC int32
_sys_goldengate_acl_map_tcp_flags(uint8 lchip, uint8 match_any,
                                  uint8 tcp_flags,
                                  sys_acl_l4_t* pc)
{
    uint8 idx_lkup   = 0;
    uint8 idx_target = 0;
    uint8 idx_valid  = 0;

    CTC_MAX_VALUE_CHECK(match_any, 1);
    CTC_MAX_VALUE_CHECK(tcp_flags, 0x3F);

    CTC_ERROR_RETURN(_sys_goldengate_acl_lkup_tcp_flag(lchip, match_any,
                                                       tcp_flags, &idx_target, &idx_lkup));

    if (SYS_ACL_TCP_FLAG_NUM != idx_lkup)
    {   /* lkup success */
        p_gg_acl_master[lchip]->tcp_flag[idx_lkup].ref++;

        idx_valid = idx_lkup;
    }
    else if ((SYS_ACL_TCP_FLAG_NUM == idx_lkup) && (p_gg_acl_master[lchip]->tcp_flag_free_cnt > 0))
    {
        _sys_goldengate_acl_add_tcp_flag(lchip, match_any,
                                         tcp_flags, idx_target);
        idx_valid = idx_target;
    }
    else
    {
        return CTC_E_ACL_TCP_FLAG_EXHAUSTED;
    }

    pc->flag_l4info_mapped  = 1;
    pc->l4info_mapped      |= 1 << (idx_valid + 8);
    pc->l4info_mapped_mask |= 1 << (idx_valid + 8);

    pc->flag_tcp_flags = 1;
    pc->tcp_flags_idx  = idx_valid;

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_acl_map_ethertype(uint16 ethertype, uint8* l3type)
{

    CTC_PTR_VALID_CHECK(l3type);

    switch (ethertype)
    {
        case 0x0800:
            *l3type = CTC_PARSER_L3_TYPE_IPV4;
            break;

        case 0x8847:
            *l3type = CTC_PARSER_L3_TYPE_MPLS;
            break;

        case 0x8848:
            *l3type = CTC_PARSER_L3_TYPE_MPLS_MCAST;
            break;

        case 0x0806:
        case 0x8035:
            *l3type = CTC_PARSER_L3_TYPE_ARP;
            break;

        case 0x8906:
            *l3type = CTC_PARSER_L3_TYPE_FCOE;
            break;

        case 0x22F3:
            *l3type = CTC_PARSER_L3_TYPE_TRILL;
            break;

        case 0x88E7:
            *l3type = CTC_PARSER_L3_TYPE_CMAC;
            break;

        case 0x8902:
            *l3type = CTC_PARSER_L3_TYPE_ETHER_OAM;
            break;

        case 0x8809:
            *l3type = CTC_PARSER_L3_TYPE_SLOW_PROTO;
            break;

        case 0x88F7:
            *l3type = CTC_PARSER_L3_TYPE_PTP;
            break;

        default:
            *l3type = CTC_PARSER_L3_TYPE_NONE;
            break;

    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_acl_map_pkt_len_range(uint8 lchip, uint32 min, uint32 max, sys_acl_l4_t* p_l4)
{
    uint32 cmd = 0;
    uint32 old_min = 0;
    uint32 old_max = 0;
    uint32 value = 0;
    uint8  len_level = 0;

    if(0 == p_gg_acl_master[lchip]->acl_register.pkt_len_range_en)
    {
        return CTC_E_FEATURE_NOT_SUPPORT;
    }

    if((min > max) || (max > 0x3FFE))
    {
        return CTC_E_INVALID_PARAM;
    }

    if(0 == p_l4->flag_pkt_len_range)
    {
        cmd = DRV_IOR(ParserLayer3LengthOpCtl_t, ParserLayer3LengthOpCtl_array_1_length_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &old_min));
        cmd = DRV_IOR(ParserLayer3LengthOpCtl_t, ParserLayer3LengthOpCtl_array_2_length_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &old_max));

        if(0 == p_gg_acl_master[lchip]->pkt_len_ref_cnt) /*pkt-len-range not exist*/
        {
            value = min;
            cmd = DRV_IOW(ParserLayer3LengthOpCtl_t, ParserLayer3LengthOpCtl_array_1_length_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
            value = max + 1;
            cmd = DRV_IOW(ParserLayer3LengthOpCtl_t, ParserLayer3LengthOpCtl_array_2_length_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));

            p_gg_acl_master[lchip]->pkt_len_ref_cnt = 1;
        }
        else if((min == old_min) && ((max + 1) == old_max))
        {
            (p_gg_acl_master[lchip]->pkt_len_ref_cnt)++;
        }
        else
        {
            return CTC_E_NO_RESOURCE;
        }

/*
    if(    (packetLength(13,0) >= ParserLayer3LengthOpCtl.array[1].length(13,0))
        && (packetLength(13,0) < ParserLayer3LengthOpCtl.array[2].length(13,0))  )
    {
        lengthLevel(3,0) = CBit(4, 'd', "1", 1);
    }
*/
        len_level = 1;  /*only support 1 range*/

        p_l4->flag_l4info_mapped  = 1;
        p_l4->l4info_mapped      |= len_level & 0xF;
        p_l4->l4info_mapped_mask |= 0xF;

        p_l4->flag_pkt_len_range = 1;
    }

    return CTC_E_NONE;
}

/**
   tcp: 4bit tcp_flags,  4-bit port range, 4-bit port/length range
   udp: 4bit user type,  4-bit port range, 4-bit port/length range
   gre: 1bit is nvgre, 4-bit gre_option_flags, 3-bit gre version, 4-bit port/length range
   other: 8-bit l4 protocol , 4-bit length range
 */
typedef struct
{
    uint16 l4_src_port_0;
    uint16 l4_src_port_1;
    uint16 l4_dst_port_0;
    uint16 l4_dst_port_1;

    uint8  tcp_flags_match_any;
    uint8  tcp_flags;
    uint8  l4_protocol;
    uint8  l4_protocol_mask;
    uint8  igmp_type;
    uint8  igmp_type_mask;

    uint8  icmp_type;
    uint8  icmp_type_mask;
    uint8  icmp_code;
    uint8  icmp_code_mask;
    uint8  l4_src_port_use_mask;
    uint8  l4_dst_port_use_mask;
    uint8 gre_crks;
    uint8 gre_crks_mask;
    uint32 gre_key;
    uint32 gre_key_mask;
    uint32 vni;
    uint32 vni_mask;
    uint32 pkt_len_min;
    uint32 pkt_len_max;
}
sys_acl_l4_info_t;

STATIC int32
_sys_goldengate_acl_map_l4_protocol(uint8 lchip, uint8 isv6,
                                    uint8 ispbr,
                                    void* pv,
                                    sys_acl_l4_t* pc)
{
    uint32            sub_flag = 0;
    uint32            match    = 0;
    uint8             matched  = 0;
    sys_acl_l4_info_t ctc;

    uint32            l4_flag[9][2][2] =
    {
        { { CTC_ACL_IPV4_KEY_SUB_FLAG_ICMP_TYPE,   CTC_ACL_IPV6_KEY_SUB_FLAG_ICMP_TYPE   },
            { CTC_ACL_PBR_IPV4_KEY_SUB_FLAG_ICMP_TYPE, CTC_ACL_PBR_IPV6_KEY_SUB_FLAG_ICMP_TYPE } },     /* 0 */
        { { CTC_ACL_IPV4_KEY_SUB_FLAG_ICMP_CODE,   CTC_ACL_IPV6_KEY_SUB_FLAG_ICMP_CODE   },
            { CTC_ACL_PBR_IPV4_KEY_SUB_FLAG_ICMP_CODE, CTC_ACL_PBR_IPV6_KEY_SUB_FLAG_ICMP_CODE } },     /* 1 */
        { { CTC_ACL_IPV4_KEY_SUB_FLAG_TCP_FLAGS,   CTC_ACL_IPV6_KEY_SUB_FLAG_TCP_FLAGS   },
            { CTC_ACL_PBR_IPV4_KEY_SUB_FLAG_TCP_FLAGS, CTC_ACL_PBR_IPV6_KEY_SUB_FLAG_TCP_FLAGS } },     /* 2 */
        { { CTC_ACL_IPV4_KEY_SUB_FLAG_L4_SRC_PORT, CTC_ACL_IPV6_KEY_SUB_FLAG_L4_SRC_PORT },
            { CTC_ACL_PBR_IPV4_KEY_SUB_FLAG_L4_SRC_PORT, CTC_ACL_PBR_IPV6_KEY_SUB_FLAG_L4_SRC_PORT } }, /* 3 */
        { { CTC_ACL_IPV4_KEY_SUB_FLAG_L4_DST_PORT, CTC_ACL_IPV6_KEY_SUB_FLAG_L4_DST_PORT },
            { CTC_ACL_PBR_IPV4_KEY_SUB_FLAG_L4_DST_PORT, CTC_ACL_PBR_IPV6_KEY_SUB_FLAG_L4_DST_PORT } }, /* 4 */
        { { CTC_ACL_IPV4_KEY_SUB_FLAG_GRE_KEY,     CTC_ACL_IPV6_KEY_SUB_FLAG_GRE_KEY     },
            { -1, -1 } },                                                                               /* 5 */
        { { CTC_ACL_IPV4_KEY_SUB_FLAG_VNI,     CTC_ACL_IPV6_KEY_SUB_FLAG_VNI},
            { -1, -1 } },                                                                               /* 6 */
        { { CTC_ACL_IPV4_KEY_SUB_FLAG_NVGRE_KEY,   CTC_ACL_IPV6_KEY_SUB_FLAG_NVGRE_KEY   },
            { -1, -1 } },                                                                                /* 7 */
        { { CTC_ACL_IPV4_KEY_SUB_FLAG_GRE_CRKS,     CTC_ACL_IPV6_KEY_SUB_FLAG_GRE_CRKS},
            { -1, -1 } },                                                                               /* 8 */
    };

    sal_memset(&ctc, 0, sizeof(ctc));
    if (isv6 && ispbr)
    {
        ctc_acl_pbr_ipv6_key_t* pctc = (ctc_acl_pbr_ipv6_key_t *) pv;
        match    = pctc->l4_protocol & pctc->l4_protocol_mask;
        sub_flag = pctc->sub_flag;

        ctc.l4_src_port_0 = pctc->l4_src_port_0;
        ctc.l4_src_port_1 = pctc->l4_src_port_1;
        ctc.l4_dst_port_0 = pctc->l4_dst_port_0;
        ctc.l4_dst_port_1 = pctc->l4_dst_port_1;

        ctc.tcp_flags_match_any  = pctc->tcp_flags_match_any;
        ctc.tcp_flags            = pctc->tcp_flags;
        ctc.l4_protocol          = pctc->l4_protocol;
        ctc.l4_protocol_mask     = pctc->l4_protocol_mask;
        ctc.icmp_type            = pctc->icmp_type;
        ctc.icmp_type_mask       = pctc->icmp_type_mask;
        ctc.icmp_code            = pctc->icmp_code;
        ctc.icmp_code_mask       = pctc->icmp_code_mask;
        ctc.l4_src_port_use_mask = pctc->l4_src_port_use_mask;
        ctc.l4_dst_port_use_mask = pctc->l4_dst_port_use_mask;
    }
    else if (isv6 && !ispbr)
    {
        ctc_acl_ipv6_key_t* pctc = (ctc_acl_ipv6_key_t *) pv;
        match    = pctc->l4_protocol & pctc->l4_protocol_mask;
        sub_flag = pctc->sub_flag;

        if(((pctc->l4_protocol == SYS_L4_PROTOCOL_TCP)&&(pctc->l4_protocol_mask != 0xff))
          ||((pctc->l4_protocol == SYS_L4_PROTOCOL_UDP)&&(pctc->l4_protocol_mask != 0xff))
          ||((pctc->l4_protocol == SYS_L4_PROTOCOL_GRE)&&(pctc->l4_protocol_mask != 0xff)))
        {
            return CTC_E_INVALID_PARAM;
        }

        CTC_MAX_VALUE_CHECK(pctc->gre_crks, 0xF);
        CTC_MAX_VALUE_CHECK(pctc->gre_crks_mask, 0xF);

        ctc.l4_src_port_0 = pctc->l4_src_port_0;
        ctc.l4_src_port_1 = pctc->l4_src_port_1;
        ctc.l4_dst_port_0 = pctc->l4_dst_port_0;
        ctc.l4_dst_port_1 = pctc->l4_dst_port_1;

        ctc.tcp_flags_match_any  = pctc->tcp_flags_match_any;
        ctc.tcp_flags            = pctc->tcp_flags;
        ctc.l4_protocol          = pctc->l4_protocol;
        ctc.l4_protocol_mask     = pctc->l4_protocol_mask;
        ctc.icmp_type            = pctc->icmp_type;
        ctc.icmp_type_mask       = pctc->icmp_type_mask;
        ctc.icmp_code            = pctc->icmp_code;
        ctc.icmp_code_mask       = pctc->icmp_code_mask;
        ctc.l4_src_port_use_mask = pctc->l4_src_port_use_mask;
        ctc.l4_dst_port_use_mask = pctc->l4_dst_port_use_mask;
        ctc.gre_crks= pctc->gre_crks;
        ctc.gre_crks_mask= pctc->gre_crks_mask;
        ctc.gre_key              = pctc->gre_key;
        ctc.gre_key_mask         = pctc->gre_key_mask;
        ctc.vni                  = pctc->vni;
        ctc.vni_mask             = pctc->vni_mask;
        ctc.pkt_len_max          = pctc->pkt_len_max;
        ctc.pkt_len_min          = pctc->pkt_len_min;
    }
    else if (!isv6 && ispbr)
    {
        ctc_acl_pbr_ipv4_key_t* pctc = (ctc_acl_pbr_ipv4_key_t *) pv;
        match    = pctc->l4_protocol & pctc->l4_protocol_mask;
        sub_flag = pctc->sub_flag;

        ctc.l4_src_port_0 = pctc->l4_src_port_0;
        ctc.l4_src_port_1 = pctc->l4_src_port_1;
        ctc.l4_dst_port_0 = pctc->l4_dst_port_0;
        ctc.l4_dst_port_1 = pctc->l4_dst_port_1;

        ctc.tcp_flags_match_any  = pctc->tcp_flags_match_any;
        ctc.tcp_flags            = pctc->tcp_flags;
        ctc.l4_protocol          = pctc->l4_protocol;
        ctc.l4_protocol_mask     = pctc->l4_protocol_mask;
        ctc.icmp_type            = pctc->icmp_type;
        ctc.icmp_type_mask       = pctc->icmp_type_mask;
        ctc.icmp_code            = pctc->icmp_code;
        ctc.icmp_code_mask       = pctc->icmp_code_mask;
        ctc.l4_src_port_use_mask = pctc->l4_src_port_use_mask;
        ctc.l4_dst_port_use_mask = pctc->l4_dst_port_use_mask;
    }
    else if (!isv6 && !ispbr)
    {
        ctc_acl_ipv4_key_t* pctc = (ctc_acl_ipv4_key_t *) pv;
        match    = pctc->l4_protocol & pctc->l4_protocol_mask;
        sub_flag = pctc->sub_flag;

        if(((pctc->l4_protocol == SYS_L4_PROTOCOL_TCP)&&(pctc->l4_protocol_mask != 0xff))
          ||((pctc->l4_protocol == SYS_L4_PROTOCOL_UDP)&&(pctc->l4_protocol_mask != 0xff))
          ||((pctc->l4_protocol == SYS_L4_PROTOCOL_GRE)&&(pctc->l4_protocol_mask != 0xff)))
        {
            return CTC_E_INVALID_PARAM;
        }

        CTC_MAX_VALUE_CHECK(pctc->gre_crks, 0xF);
        CTC_MAX_VALUE_CHECK(pctc->gre_crks_mask, 0xF);

        ctc.l4_src_port_0 = pctc->l4_src_port_0;
        ctc.l4_src_port_1 = pctc->l4_src_port_1;
        ctc.l4_dst_port_0 = pctc->l4_dst_port_0;
        ctc.l4_dst_port_1 = pctc->l4_dst_port_1;

        ctc.tcp_flags_match_any  = pctc->tcp_flags_match_any;
        ctc.tcp_flags            = pctc->tcp_flags;
        ctc.l4_protocol          = pctc->l4_protocol;
        ctc.l4_protocol_mask     = pctc->l4_protocol_mask;
        ctc.igmp_type            = pctc->igmp_type;
        ctc.igmp_type_mask       = pctc->igmp_type_mask;
        ctc.icmp_type            = pctc->icmp_type;
        ctc.icmp_type_mask       = pctc->icmp_type_mask;
        ctc.icmp_code            = pctc->icmp_code;
        ctc.icmp_code_mask       = pctc->icmp_code_mask;
        ctc.l4_src_port_use_mask = pctc->l4_src_port_use_mask;
        ctc.l4_dst_port_use_mask = pctc->l4_dst_port_use_mask;
        ctc.gre_crks= pctc->gre_crks;
        ctc.gre_crks_mask= pctc->gre_crks_mask;
        ctc.gre_key              = pctc->gre_key;
        ctc.gre_key_mask         = pctc->gre_key_mask;
        ctc.vni                  = pctc->vni;
        ctc.vni_mask             = pctc->vni_mask;
        ctc.pkt_len_max          = pctc->pkt_len_max;
        ctc.pkt_len_min          = pctc->pkt_len_min;
    }

    if ((!isv6 && (match == (SYS_L4_PROTOCOL_IPV4_ICMP & 0xFF))) ||
        (isv6 && (match == (SYS_L4_PROTOCOL_IPV6_ICMP & 0xFF))))
    {
        pc->flag_l4info_mapped = 1;
        pc->l4info_mapped      |= ctc.l4_protocol      << 4;
        pc->l4info_mapped_mask |= ctc.l4_protocol_mask << 4;

        /* icmp type */
        if (CTC_FLAG_ISSET(sub_flag, l4_flag[0][ispbr][isv6]))
        {
            pc->l4_src_port      |= (ctc.icmp_type) << 8;
            pc->l4_src_port_mask |= (ctc.icmp_type_mask) << 8;
        }

        /* icmp code */
        if (CTC_FLAG_ISSET(sub_flag, l4_flag[1][ispbr][isv6]))
        {
            pc->l4_src_port      |= ctc.icmp_code & 0x00FF;
            pc->l4_src_port_mask |= (ctc.icmp_code_mask) & 0x00FF;
        }
        matched = 1;

        pc->flag_l4_type          = 1;
        pc->l4_compress_type      = 0;  /*NOT TCP UDP GRE;*/
        pc->l4_compress_type_mask = 3;
    }

    if ((match == (SYS_L4_PROTOCOL_TCP & 0xFF)) ||
        (match == (SYS_L4_PROTOCOL_UDP & 0xFF)))
    {
        if (match == (SYS_L4_PROTOCOL_TCP & 0xFF))
        {
            pc->flag_l4_type          = 1;
            pc->l4_type               = CTC_PARSER_L4_TYPE_TCP;
            pc->l4_type_mask          = 15;
            pc->l4_compress_type      = 2; /*CTC_PARSER_L4_TYPE_TCP;*/
            pc->l4_compress_type_mask = 3;

            if (CTC_FLAG_ISSET(sub_flag, l4_flag[2][ispbr][isv6]))
            {
                CTC_ERROR_RETURN(_sys_goldengate_acl_map_tcp_flags
                                     (lchip, ctc.tcp_flags_match_any, ctc.tcp_flags, pc));
            }
        }
        else
        {
            pc->flag_l4_type          = 1;
            pc->l4_type               = CTC_PARSER_L4_TYPE_UDP;
            pc->l4_type_mask          = 15;
            pc->l4_compress_type      = 1; /*CTC_PARSER_L4_TYPE_UDP;*/
            pc->l4_compress_type_mask = 3;
        }

        if (CTC_FLAG_ISSET(sub_flag, l4_flag[3][ispbr][isv6]))
        {
            if (ctc.l4_src_port_use_mask)
            {
                pc->l4_src_port      = ctc.l4_src_port_0;
                pc->l4_src_port_mask = ctc.l4_src_port_1;
            }
            else
            {
                CTC_ERROR_RETURN(_sys_goldengate_acl_map_port_range(lchip, 0,
                                    ctc.l4_src_port_0,
                                    ctc.l4_src_port_1,
                                    pc))
            }
        }

        if (CTC_FLAG_ISSET(sub_flag, l4_flag[4][ispbr][isv6]))
        {
            if (ctc.l4_dst_port_use_mask)
            {
                pc->l4_dst_port      = ctc.l4_dst_port_0;
                pc->l4_dst_port_mask = ctc.l4_dst_port_1;
            }
            else
            {
                CTC_ERROR_RETURN(_sys_goldengate_acl_map_port_range(lchip,
                                     1, ctc.l4_dst_port_0,
                                     ctc.l4_dst_port_1,
                                     pc));
            }
        }

        if (!ispbr && (CTC_FLAG_ISSET(sub_flag, l4_flag[6][ispbr][isv6])))
        {
            CTC_MAX_VALUE_CHECK(ctc.vni, 0xFFFFFF);
            CTC_MAX_VALUE_CHECK(ctc.vni_mask, 0xFFFFFF);

            pc->l4_dst_port       = ctc.vni & 0xFFFF;
            pc->l4_dst_port_mask = ctc.vni_mask & 0xFFFF;

            pc->l4_src_port= (ctc.vni >> 16) & 0xFF;
            pc->l4_src_port_mask = (ctc.vni_mask>> 16) & 0xFF;

            pc->flag_l4info_mapped  = 1;
            pc->l4info_mapped      |= SYS_L4_USER_UDP_TYPE_VXLAN << 8;
            pc->l4info_mapped_mask |= 0xF << 8;
        }

        matched = 1;
    }

    if (match == (SYS_L4_PROTOCOL_GRE & 0xFF))  /*ctc.l4_protocol_mask*/
    {
        pc->flag_l4_type          = 1;
        pc->l4_type               = CTC_PARSER_L4_TYPE_GRE;
        pc->l4_type_mask          = 15;
        pc->l4_compress_type      = 3;  /*CTC_PARSER_L4_TYPE_GRE;*/
        pc->l4_compress_type_mask = 3;

        if (!ispbr && (CTC_FLAG_ISSET(sub_flag, l4_flag[5][ispbr][isv6])))
        {
            pc->flag_l4info_mapped = 1;
            CTC_BIT_UNSET(pc->l4info_mapped, 11);
            CTC_BIT_SET(pc->l4info_mapped_mask, 11);

            pc->l4_src_port      = ctc.gre_key >> 16;
            pc->l4_src_port_mask = ctc.gre_key_mask >> 16;

            pc->l4_dst_port      = ctc.gre_key & 0xFFFF;
            pc->l4_dst_port_mask = ctc.gre_key_mask & 0xFFFF;
        }
        else if (!ispbr && (CTC_FLAG_ISSET(sub_flag, l4_flag[7][ispbr][isv6])))
        {
            pc->flag_l4info_mapped = 1;
            pc->l4info_mapped      |= 1 << 11;
            pc->l4info_mapped_mask |= 1 << 11;

            pc->l4_src_port        = ctc.gre_key >> 24;
            pc->l4_src_port_mask   = ctc.gre_key_mask >> 24;

            pc->l4_dst_port        = (ctc.gre_key >> 8) & 0xFFFF;
            pc->l4_dst_port_mask   = (ctc.gre_key_mask >> 8) & 0xFFFF;
        }

        if (!ispbr && (CTC_FLAG_ISSET(sub_flag, l4_flag[8][ispbr][isv6])))
        {
            pc->flag_l4info_mapped = 1;
            pc->l4info_mapped |= ctc.gre_crks<< 7;
            pc->l4info_mapped_mask |= ctc.gre_crks_mask<< 7;
        }
        matched = 1;
    }

    if (!isv6 && (match == (SYS_L4_PROTOCOL_IPV4_IGMP & 0xFF)))
    {
        pc->flag_l4info_mapped = 1;
        pc->l4info_mapped      |= ctc.l4_protocol      << 4;
        pc->l4info_mapped_mask |= ctc.l4_protocol_mask << 4;

        /* l4_src_port for flex-l4 (including igmp), it's type|..*/
        if ((CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV4_KEY_SUB_FLAG_IGMP_TYPE)) && !ispbr )
        {
            pc->l4_src_port      |= (ctc.igmp_type << 8) & 0xFF00;
            pc->l4_src_port_mask |= (ctc.igmp_type_mask << 8) & 0xFF00;
        }
        matched = 1;

        pc->flag_l4_type          = 1;
        pc->l4_type               = CTC_PARSER_L4_TYPE_IGMP;
        pc->l4_type_mask          = 15;
        pc->l4_compress_type      = 0;  /*NOT TCP UDP GRE;*/
        pc->l4_compress_type_mask = 3;
    }

    if (match == (SYS_L4_PROTOCOL_SCTP & 0xFF))  /*ctc.l4_protocol_mask*/
    {
        pc->flag_l4_type          = 1;
        pc->l4_type               = CTC_PARSER_L4_TYPE_SCTP;
        pc->l4_type_mask          = 15;
        pc->l4_compress_type      = 0; /*NOT TCP UDP GRE;*/
        pc->l4_compress_type_mask = 3;

        pc->flag_l4info_mapped = 1;
        pc->l4info_mapped      |= ctc.l4_protocol      << 4;
        pc->l4info_mapped_mask |= ctc.l4_protocol_mask << 4;

        if (CTC_FLAG_ISSET(sub_flag, l4_flag[3][ispbr][isv6]))
        {
            if (ctc.l4_src_port_use_mask)
            {
                pc->l4_src_port      = ctc.l4_src_port_0;
                pc->l4_src_port_mask = ctc.l4_src_port_1;
            }
            else
            {
                return CTC_E_INVALID_PARAM;
            }
        }

        if (CTC_FLAG_ISSET(sub_flag, l4_flag[4][ispbr][isv6]))
        {
            if (ctc.l4_dst_port_use_mask)
            {
                pc->l4_dst_port      = ctc.l4_dst_port_0;
                pc->l4_dst_port_mask = ctc.l4_dst_port_1;
            }
            else
            {
                return CTC_E_INVALID_PARAM;
            }
        }
        matched = 1;
    }

    if (!matched)
    {
        uint16 temp_type = 0;

        pc->flag_l4info_mapped  = 1;
        pc->l4info_mapped      |= ctc.l4_protocol << 4;
        pc->l4info_mapped_mask |= ctc.l4_protocol_mask << 4;

        switch(ctc.l4_protocol)
        {
            case 27:
                temp_type = CTC_PARSER_L4_TYPE_RDP;
                break;
            case 33:
                temp_type = CTC_PARSER_L4_TYPE_DCCP;
                break;
            case 4:
                temp_type = isv6 ?  CTC_PARSER_L4_TYPE_IPINV6 : CTC_PARSER_L4_TYPE_IPINIP;
                break;
            case 41:
                temp_type = isv6 ?  CTC_PARSER_L4_TYPE_V6INV6 : CTC_PARSER_L4_TYPE_V6INIP;
                break;
            default:
                temp_type = CTC_PARSER_L4_TYPE_NONE;
                break;
        }
        pc->flag_l4_type          = 1;
        pc->l4_type               = temp_type;
        pc->l4_type_mask          = 15;
        pc->l4_compress_type      = 0;
        pc->l4_compress_type_mask = 3;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_acl_map_tcam_key_port(uint8 lchip, ctc_field_port_t* p_ctc_key,
                                sys_acl_port_t* p_sys_key, uint8 is_ipv6)
{
    uint8 idx = 0;

    sal_memset(p_sys_key, 0, sizeof(sys_acl_port_t));

    switch (p_ctc_key->type)
    {
        case CTC_FIELD_PORT_TYPE_NONE:
            break;
        case CTC_FIELD_PORT_TYPE_GPORT:
            CTC_GLOBAL_PORT_CHECK(p_ctc_key->gport);
            if(!CTC_IS_LINKAGG_PORT(p_ctc_key->gport))
            {
                if(FALSE == sys_goldengate_chip_is_local(lchip, CTC_MAP_GPORT_TO_GCHIP(p_ctc_key->gport)))
                {
                    /*chip is not local*/
                    return CTC_E_CHIP_IS_REMOTE;
                }
            }
            p_sys_key->gport = SYS_MAP_CTC_GPORT_TO_DRV_GPORT(p_ctc_key->gport);
            p_sys_key->gport_mask = 0x3FFF;
            p_sys_key->gport_type  = DRV_FLOWPORTTYPE_GPORT;
            p_sys_key->gport_type_mask = 3;
            p_sys_key->valid = 1;
            break;
        case CTC_FIELD_PORT_TYPE_LOGIC_PORT:
            SYS_ACL_SERVICE_ID_CHECK(p_ctc_key->logic_port);
            p_sys_key->gport = p_ctc_key->logic_port;
            p_sys_key->gport_mask = 0x3FFF;
            p_sys_key->gport_type  = DRV_FLOWPORTTYPE_LPORT;
            p_sys_key->gport_type_mask = 3;
            p_sys_key->valid = 1;
            break;

        case CTC_FIELD_PORT_TYPE_VLAN_CLASS:
            SYS_ACL_VLAN_CLASS_ID_CHECK(p_ctc_key->vlan_class_id);
            p_sys_key->class_id_data = p_ctc_key->vlan_class_id;
            p_sys_key->class_id_mask = 0xFF;
            p_sys_key->valid = 1;
            break;

        case CTC_FIELD_PORT_TYPE_PORT_CLASS:
            SYS_ACL_PORT_CLASS_ID_CHECK(p_ctc_key->port_class_id);
            p_sys_key->class_id_data = p_ctc_key->port_class_id;
            p_sys_key->class_id_mask = 0xFF;
            p_sys_key->valid = 1;
            break;

        case CTC_FIELD_PORT_TYPE_PBR_CLASS:
            CTC_ACL_PBR_CLASS_ID_CHECK(p_ctc_key->pbr_class_id);
            p_sys_key->class_id_data = p_ctc_key->pbr_class_id;
            p_sys_key->class_id_mask = 0x3F;
            p_sys_key->valid = 1;
            break;

        case CTC_FIELD_PORT_TYPE_PORT_BITMAP:
             if (lchip != p_ctc_key->lchip)
            {
                return CTC_E_NONE;
            }

             /* 1. port_bitmap only allow to use 128 bits */
            for (idx = 4; idx < CTC_PORT_BITMAP_IN_WORD; idx++)
            {
                if (p_ctc_key->port_bitmap[idx])
                {
                    return CTC_E_ACL_PORT_BITMAP_OVERFLOW;
                }
            }

            if (is_ipv6)
            {
                if (((p_ctc_key->port_bitmap[0] || p_ctc_key->port_bitmap[1]) +
                     (p_ctc_key->port_bitmap[2] || p_ctc_key->port_bitmap[3])) > 1)
                {
                    return CTC_E_ACL_PORT_BITMAP_OVERFLOW;
                }
            }
            else
            {
                if (((p_ctc_key->port_bitmap[0] & 0xFFFF || 0) +
                     (p_ctc_key->port_bitmap[1] & 0xFFFF || 0) +
                     (p_ctc_key->port_bitmap[2] & 0xFFFF || 0) +
                     (p_ctc_key->port_bitmap[3] & 0xFFFF || 0) +
                     (p_ctc_key->port_bitmap[0] >> 16 || 0) +
                     (p_ctc_key->port_bitmap[1] >> 16 || 0) +
                     (p_ctc_key->port_bitmap[2] >> 16 || 0) +
                     (p_ctc_key->port_bitmap[3] >> 16 || 0)) > 1)
                {
                    return CTC_E_ACL_PORT_BITMAP_OVERFLOW;
                }
            }

            if (is_ipv6)
            {
                uint32 bitmap_64[2] = { 0 };
                if (p_ctc_key->port_bitmap[0] || p_ctc_key->port_bitmap[1])
                {
                    bitmap_64[0]        = p_ctc_key->port_bitmap[0];
                    bitmap_64[1]        = p_ctc_key->port_bitmap[1];
                    p_sys_key->bitmap_base = 0;
                    p_sys_key->bitmap_base_mask = 1;
                }
                else if (p_ctc_key->port_bitmap[2] || p_ctc_key->port_bitmap[3])
                {
                    bitmap_64[0]        = p_ctc_key->port_bitmap[2];
                    bitmap_64[1]        = p_ctc_key->port_bitmap[3];
                    p_sys_key->bitmap_base = 1;
                    p_sys_key->bitmap_base_mask = 1;
                }
                p_sys_key->bitmap_64[0] = ~bitmap_64[0];
                p_sys_key->bitmap_64[1] = ~bitmap_64[1];
            }
            else
            {
                uint16 bitmap_16 = 0;
                uint8  i         = 0;

                for (i = 0; i < 4; i++)
                {
                    if ((bitmap_16 = (p_ctc_key->port_bitmap[i] & 0xFFFF)))
                    {
                        p_sys_key->bitmap_base = 0 + (2 * i);
                        break;
                    }
                    else if ((bitmap_16 = (p_ctc_key->port_bitmap[i] >> 16)))
                    {
                        p_sys_key->bitmap_base = 1 + (2 * i);
                        break;
                    }
                }

                p_sys_key->bitmap_16 = ~bitmap_16;

                p_sys_key->class_id_data = bitmap_16 & 0xFF;
                p_sys_key->class_id_mask = p_sys_key->bitmap_16 & 0xFF;
                p_sys_key->gport      = (p_sys_key->bitmap_base << 8) + (bitmap_16 >> 8);
                p_sys_key->gport_mask = (0x7 << 8) | (p_sys_key->bitmap_16 >> 8);
            }
            p_sys_key->gport_type      = DRV_FLOWPORTTYPE_BITMAP;
            p_sys_key->gport_type_mask = 3;
            p_sys_key->valid = 1;
            break;
        default:
            return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_acl_map_mac_key(uint8 lchip, ctc_acl_mac_key_t* p_ctc_key,
                                sys_acl_sw_tcam_key_mac_t* p_sys_key, uint8 dir)
{
    uint32 flag = 0;
    uint8 l3_type = CTC_PARSER_L3_TYPE_NONE;
    ctc_vlan_range_info_t vrange_info;
    ctc_vlan_range_t vlan_range;
    uint16 cvid = 0;
    uint16 svid = 0;

    CTC_PTR_VALID_CHECK(p_ctc_key);
    CTC_PTR_VALID_CHECK(p_sys_key);

    SYS_ACL_DBG_FUNC();

    sal_memset(&vrange_info, 0, sizeof(ctc_vlan_range_info_t));
    sal_memset(&vlan_range, 0, sizeof(ctc_vlan_range_t));

    p_sys_key->flag = p_ctc_key->flag;
    flag            = p_ctc_key->flag;
    CTC_ERROR_RETURN(_sys_goldengate_acl_check_mac_key(lchip, flag));

    /* mac da */
    if (CTC_FLAG_ISSET(flag, CTC_ACL_MAC_KEY_FLAG_MAC_DA))
    {
        sal_memcpy(p_sys_key->mac_da, p_ctc_key->mac_da, CTC_ETH_ADDR_LEN);
        sal_memcpy(p_sys_key->mac_da_mask, p_ctc_key->mac_da_mask, CTC_ETH_ADDR_LEN);
    }

    /* mac sa */
    if (CTC_FLAG_ISSET(flag, CTC_ACL_MAC_KEY_FLAG_MAC_SA))
    {
        sal_memcpy(p_sys_key->mac_sa, p_ctc_key->mac_sa, CTC_ETH_ADDR_LEN);
        sal_memcpy(p_sys_key->mac_sa_mask, p_ctc_key->mac_sa_mask, CTC_ETH_ADDR_LEN);
    }

    /* cvlan */
    if (CTC_FLAG_ISSET(flag, CTC_ACL_MAC_KEY_FLAG_CVLAN))
    {
        SYS_ACL_VLAN_ID_CHECK(p_ctc_key->cvlan);
        SYS_ACL_VLAN_ID_CHECK(p_ctc_key->cvlan_end);

        cvid = p_ctc_key->cvlan;

        vrange_info.direction = dir;
        vrange_info.vrange_grpid = p_ctc_key->vrange_grpid;
        vlan_range.is_acl = 1;
        vlan_range.vlan_start = p_ctc_key->cvlan;
        vlan_range.vlan_end = p_ctc_key->cvlan_end;
        CTC_ERROR_RETURN(sys_goldengate_vlan_get_maxvid_from_vrange(lchip, &vrange_info, &vlan_range, FALSE, &cvid));

        p_sys_key->cvlan = cvid;
        p_sys_key->cvlan_mask = p_ctc_key->cvlan_mask & 0xFFF;
    }

    /* ctag cos */
    if (CTC_FLAG_ISSET(flag, CTC_ACL_MAC_KEY_FLAG_CTAG_COS))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->ctag_cos, 0x7);
        p_sys_key->ctag_cos      = p_ctc_key->ctag_cos;
        p_sys_key->ctag_cos_mask = (p_ctc_key->ctag_cos_mask) & 0x7;
    }

    /* ctag cfi */
    if (CTC_FLAG_ISSET(flag, CTC_ACL_MAC_KEY_FLAG_CTAG_CFI))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->ctag_cfi, 0x1);
        p_sys_key->ctag_cfi = p_ctc_key->ctag_cfi;
    }

    /* cvid_valid */
    if (CTC_FLAG_ISSET(flag, CTC_ACL_MAC_KEY_FLAG_CTAG_VALID))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->ctag_valid, 1);
        p_sys_key->ctag_valid = p_ctc_key->ctag_valid;
    }

    /* svlan */
    if (CTC_FLAG_ISSET(flag, CTC_ACL_MAC_KEY_FLAG_SVLAN))
    {
        SYS_ACL_VLAN_ID_CHECK(p_ctc_key->svlan);
        SYS_ACL_VLAN_ID_CHECK(p_ctc_key->svlan_end);

        svid = p_ctc_key->svlan;

        vrange_info.direction = dir;
        vrange_info.vrange_grpid = p_ctc_key->vrange_grpid;
        vlan_range.is_acl = 1;
        vlan_range.vlan_start = p_ctc_key->svlan;
        vlan_range.vlan_end = p_ctc_key->svlan_end;
        CTC_ERROR_RETURN(sys_goldengate_vlan_get_maxvid_from_vrange(lchip, &vrange_info, &vlan_range, TRUE, &svid));

        p_sys_key->svlan = svid;
        p_sys_key->svlan_mask = p_ctc_key->svlan_mask & 0xFFF;
    }

    /* stag cos */
    if (CTC_FLAG_ISSET(flag, CTC_ACL_MAC_KEY_FLAG_STAG_COS))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->stag_cos, 0x7);
        p_sys_key->stag_cos      = p_ctc_key->stag_cos;
        p_sys_key->stag_cos_mask = (p_ctc_key->stag_cos_mask) & 0x7;
    }

    /* stag cfi */
    if (CTC_FLAG_ISSET(flag, CTC_ACL_MAC_KEY_FLAG_STAG_CFI))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->stag_cfi, 0x1);
        p_sys_key->stag_cfi = p_ctc_key->stag_cfi;
    }
    if (CTC_FLAG_ISSET(flag, CTC_ACL_MAC_KEY_FLAG_STAG_VALID))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->stag_valid, 1);
        p_sys_key->stag_valid = p_ctc_key->stag_valid;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_MAC_KEY_FLAG_VLAN_NUM))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->vlan_num, 0x2);
        p_sys_key->vlan_num      = p_ctc_key->vlan_num;
        p_sys_key->vlan_num_mask = p_ctc_key->vlan_num_mask;
    }

    /* l3 type */
    if (CTC_FLAG_ISSET(flag, CTC_ACL_MAC_KEY_FLAG_L3_TYPE))
    {
        p_sys_key->l3_type = p_ctc_key->l3_type;
        p_sys_key->l3_type_mask = p_ctc_key->l3_type_mask;
    }

    /* eth type */
    if (CTC_FLAG_ISSET(flag, CTC_ACL_MAC_KEY_FLAG_ETH_TYPE))
    {
        if (p_gg_acl_master[lchip]->ipv4_160bit_mode_en && (0xFFFF == p_ctc_key->eth_type_mask))
        {
            /* mac key use L3 320bit, ethertype used to match unknow l3type */
            CTC_ERROR_RETURN(_sys_goldengate_acl_map_ethertype(p_ctc_key->eth_type, &l3_type));

            if(l3_type != CTC_PARSER_L3_TYPE_NONE)
            {
                CTC_UNSET_FLAG(flag, CTC_ACL_MAC_KEY_FLAG_ETH_TYPE);
                CTC_SET_FLAG(flag, CTC_ACL_MAC_KEY_FLAG_L3_TYPE);
                p_sys_key->flag = flag;
                p_sys_key->l3_type = l3_type;
                p_sys_key->l3_type_mask = 0xF;
            }
            else
            {
                p_sys_key->eth_type      = p_ctc_key->eth_type;
                p_sys_key->eth_type_mask = p_ctc_key->eth_type_mask;
            }
        }
        else
        {
            p_sys_key->eth_type      = p_ctc_key->eth_type;
            p_sys_key->eth_type_mask = p_ctc_key->eth_type_mask;
        }
    }

    /* metadata */
    if (CTC_FLAG_ISSET(flag, CTC_ACL_MAC_KEY_FLAG_METADATA))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->metadata, 0x3FFF);
        p_sys_key->metadata = p_ctc_key->metadata;
        p_sys_key->metadata_mask = p_ctc_key->metadata_mask;
    }

    return CTC_E_NONE;
}
STATIC int32
_sys_goldengate_acl_map_ipv4_key(uint8 lchip, ctc_acl_ipv4_key_t* p_ctc_key,
                                 sys_acl_sw_tcam_key_ipv4_t* p_sys_key, uint8 dir)
{
    uint32 flag          = 0;
    uint32 sub_flag      = 0;
    uint32 sub1_flag     = 0;
    /*uint32 sub2_flag     = 0;*/
    uint32 sub3_flag     = 0;
    /*uint8 udf0 = 0;*/
    uint8 udf1 = 0;
    uint8 udf2 = 0;
    uint8 udf3 = 0;
    uint8 udf0_mask = 0;
    uint8 udf1_mask = 0;
    uint8 udf2_mask = 0;
    uint8 udf3_mask = 0;
    uint8 l3_type = CTC_PARSER_L3_TYPE_NONE;
    ctc_vlan_range_info_t vrange_info;
    ctc_vlan_range_t vlan_range;
    uint16 cvid = 0;
    uint16 svid = 0;

    CTC_PTR_VALID_CHECK(p_ctc_key);
    CTC_PTR_VALID_CHECK(p_sys_key);

    SYS_ACL_DBG_FUNC();

    CTC_ERROR_RETURN(_sys_goldengate_acl_check_ipv4_key(lchip, p_ctc_key->flag, p_ctc_key->key_size));

    if(CTC_FLAG_ISSET(p_ctc_key->flag, CTC_ACL_IPV4_KEY_FLAG_L3_TYPE))
    {
        if(CTC_PARSER_L3_TYPE_IP == ((p_ctc_key->l3_type) & (p_ctc_key->l3_type_mask)))
        {
            return CTC_E_FEATURE_NOT_SUPPORT;
        }
    }

    sal_memset(&vrange_info, 0, sizeof(ctc_vlan_range_info_t));
    sal_memset(&vlan_range, 0, sizeof(ctc_vlan_range_t));

    p_sys_key->key_size  = p_ctc_key->key_size;
    p_sys_key->flag      = p_ctc_key->flag;
    p_sys_key->sub_flag  = p_ctc_key->sub_flag;
    p_sys_key->sub1_flag = p_ctc_key->sub1_flag;
    p_sys_key->sub2_flag = p_ctc_key->sub2_flag;
    p_sys_key->sub3_flag = p_ctc_key->sub3_flag;

    flag      = p_ctc_key->flag;
    sub_flag  = p_ctc_key->sub_flag;
    sub1_flag = p_ctc_key->sub1_flag;
    /*sub2_flag = p_ctc_key->sub2_flag;*/
    sub3_flag = p_ctc_key->sub3_flag;

    if ((CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV4_KEY_SUB_FLAG_L4_SRC_PORT)
         + CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV4_KEY_SUB_FLAG_GRE_KEY)
         + CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV4_KEY_SUB_FLAG_NVGRE_KEY)
         + CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV4_KEY_SUB_FLAG_VNI)) > 1)
    {
        return CTC_E_ACL_FLAG_COLLISION;
    }

    if ((CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV4_KEY_SUB_FLAG_L4_DST_PORT)
         + CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV4_KEY_SUB_FLAG_GRE_KEY)
         + CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV4_KEY_SUB_FLAG_NVGRE_KEY)
         + CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV4_KEY_SUB_FLAG_VNI)) > 1)
    {
        return CTC_E_ACL_FLAG_COLLISION;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_MAC_DA))
    {
        sal_memcpy(p_sys_key->mac_da, p_ctc_key->mac_da, CTC_ETH_ADDR_LEN);
        sal_memcpy(p_sys_key->mac_da_mask, p_ctc_key->mac_da_mask, CTC_ETH_ADDR_LEN);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_MAC_SA))
    {
        sal_memcpy(p_sys_key->mac_sa, p_ctc_key->mac_sa, CTC_ETH_ADDR_LEN);
        sal_memcpy(p_sys_key->mac_sa_mask, p_ctc_key->mac_sa_mask, CTC_ETH_ADDR_LEN);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_CVLAN))
    {
        SYS_ACL_VLAN_ID_CHECK(p_ctc_key->cvlan);
        SYS_ACL_VLAN_ID_CHECK(p_ctc_key->cvlan_end);

        cvid = p_ctc_key->cvlan;

        vrange_info.direction = dir;
        vrange_info.vrange_grpid = p_ctc_key->vrange_grpid;
        vlan_range.is_acl = 1;
        vlan_range.vlan_start = p_ctc_key->cvlan;
        vlan_range.vlan_end = p_ctc_key->cvlan_end;
        CTC_ERROR_RETURN(sys_goldengate_vlan_get_maxvid_from_vrange(lchip, &vrange_info, &vlan_range, FALSE, &cvid));

        p_sys_key->cvlan = cvid;
        p_sys_key->cvlan_mask = p_ctc_key->cvlan_mask & 0xFFF;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_CTAG_COS))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->ctag_cos, 0x7);
        p_sys_key->ctag_cos      = p_ctc_key->ctag_cos;
        p_sys_key->ctag_cos_mask = (p_ctc_key->ctag_cos_mask) & 0x7;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_SVLAN))
    {
        SYS_ACL_VLAN_ID_CHECK(p_ctc_key->svlan);
        SYS_ACL_VLAN_ID_CHECK(p_ctc_key->svlan_end);

        svid = p_ctc_key->svlan;

        vrange_info.direction = dir;
        vrange_info.vrange_grpid = p_ctc_key->vrange_grpid;
        vlan_range.is_acl = 1;
        vlan_range.vlan_start = p_ctc_key->svlan;
        vlan_range.vlan_end = p_ctc_key->svlan_end;
        CTC_ERROR_RETURN(sys_goldengate_vlan_get_maxvid_from_vrange(lchip, &vrange_info, &vlan_range, TRUE, &svid));

        p_sys_key->svlan = svid;
        p_sys_key->svlan_mask = p_ctc_key->svlan_mask & 0xFFF;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_STAG_COS))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->stag_cos, 0x7);
        p_sys_key->stag_cos      = p_ctc_key->stag_cos;
        p_sys_key->stag_cos_mask = (p_ctc_key->stag_cos_mask) & 0x7;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_STAG_CFI))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->stag_cfi, 0x1);
        p_sys_key->stag_cfi = p_ctc_key->stag_cfi;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_VLAN_NUM))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->vlan_num, 2);
        p_sys_key->vlan_num = p_ctc_key->vlan_num;
        p_sys_key->vlan_num_mask = (p_ctc_key->vlan_num_mask) & 0xF;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_STAG_VALID))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->stag_valid, 0x1);
        p_sys_key->stag_valid = p_ctc_key->stag_valid;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_CTAG_VALID))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->ctag_valid, 0x1);
        p_sys_key->ctag_valid = p_ctc_key->ctag_valid;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_ROUTED_PACKET))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->routed_packet, 1);
        p_sys_key->routed_packet = p_ctc_key->routed_packet;
    }

    if ((CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_IP_HDR_ERROR)) && (CTC_ACL_KEY_SIZE_SINGLE == p_ctc_key->key_size))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->ip_hdr_error, 1);
        p_sys_key->ip_header_error = p_ctc_key->ip_hdr_error;
    }

    /* metadata */
    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_METADATA))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->metadata, 0x3FFF);
        p_sys_key->metadata = p_ctc_key->metadata;
        p_sys_key->metadata_mask = p_ctc_key->metadata_mask;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_UDF))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->udf_type, CTC_PARSER_UDF_TYPE_NUM-1);
        /*udf0 = p_ctc_key->udf[0];*/
        udf1 = p_ctc_key->udf[1];
        udf2 = p_ctc_key->udf[2];
        udf3 = p_ctc_key->udf[3];
        p_sys_key->udf = p_ctc_key->udf[0] + (udf1 << 8) + (udf2 << 16) + (udf3 << 24);

        udf0_mask = p_ctc_key->udf_mask[0];
        udf1_mask = p_ctc_key->udf_mask[1];
        udf2_mask = p_ctc_key->udf_mask[2];
        udf3_mask = p_ctc_key->udf_mask[3];
        p_sys_key->udf_mask = udf0_mask + (udf1_mask << 8) + (udf2_mask << 16) + (udf3_mask << 24);

        if(CTC_PARSER_UDF_TYPE_L3_UDF == p_ctc_key->udf_type)
        {
            p_sys_key->udf_type = DRV_UDFTYPE_L3UDF;
        }
        else if(CTC_PARSER_UDF_TYPE_L4_UDF == p_ctc_key->udf_type)
        {
            p_sys_key->udf_type = DRV_UDFTYPE_L4UDF;
        }
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_L3_TYPE))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->l3_type, 0xF);
        p_sys_key->l3_type      = p_ctc_key->l3_type;
        p_sys_key->l3_type_mask = (p_ctc_key->l3_type_mask) & 0xF;
    }

    /* eth type */
    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_ETH_TYPE))
    {
        if (0xFFFF == p_ctc_key->eth_type_mask)
        {
            CTC_ERROR_RETURN(_sys_goldengate_acl_map_ethertype(p_ctc_key->eth_type, &l3_type));

            if(l3_type != CTC_PARSER_L3_TYPE_NONE)
            {
                CTC_UNSET_FLAG(flag, CTC_ACL_IPV4_KEY_FLAG_ETH_TYPE);
                CTC_SET_FLAG(flag, CTC_ACL_IPV4_KEY_FLAG_L3_TYPE);
                p_sys_key->flag = flag;
                p_sys_key->l3_type = l3_type;
                p_sys_key->l3_type_mask = 0xF;
            }
            else
            {
                p_sys_key->u0.other.eth_type      = p_ctc_key->eth_type;
                p_sys_key->u0.other.eth_type_mask = p_ctc_key->eth_type_mask;
            }
        }
        else
        {
            p_sys_key->u0.other.eth_type      = p_ctc_key->eth_type;
            p_sys_key->u0.other.eth_type_mask = p_ctc_key->eth_type_mask;
        }
    }

    /* l3_type is not supposed to match more than once. --> Fullfill this will make things easy.
     * But still, l3_type_mask can be any value, if user wants to only match it. --> Doing this make things complicate.
     * If further l3_info requires to be matched, only one l3_info is promised to work.
     */

    p_sys_key->unique_l3_type = CTC_PARSER_L3_TYPE_NONE;

    if(0xF == p_sys_key->l3_type_mask)
    {
        switch (p_sys_key->l3_type)
        {
            case CTC_PARSER_L3_TYPE_IPV4:
                if(CTC_FLAG_ISSET(flag,CTC_ACL_IPV4_KEY_FLAG_ETH_TYPE))
                {
                    return CTC_E_ACL_FLAG_COLLISION;
                }

                p_sys_key->unique_l3_type = CTC_PARSER_L3_TYPE_IPV4;
                if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_IP_DA))
                {
                    p_sys_key->u0.ip.ip_da      = p_ctc_key->ip_da;
                    p_sys_key->u0.ip.ip_da_mask = p_ctc_key->ip_da_mask;
                }

                if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_IP_SA))
                {
                    p_sys_key->u0.ip.ip_sa      = p_ctc_key->ip_sa;
                    p_sys_key->u0.ip.ip_sa_mask = p_ctc_key->ip_sa_mask;
                }

                if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_IP_FRAG))
                {
                    uint8 d;
                    uint8 m;
                    CTC_ERROR_RETURN(_sys_goldengate_acl_map_ip_frag(lchip, p_ctc_key->ip_frag, &d, &m));
                    p_sys_key->u0.ip.frag_info      = d;
                    p_sys_key->u0.ip.frag_info_mask = m;
                }

                if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_IP_OPTION))
                {
                    CTC_MAX_VALUE_CHECK(p_ctc_key->ip_option, 1);
                    p_sys_key->u0.ip.ip_option = p_ctc_key->ip_option;
                }

                if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_ECN))
                {
                    CTC_MAX_VALUE_CHECK(p_ctc_key->ecn, 3);
                    p_sys_key->u0.ip.ecn      = p_ctc_key->ecn;
                    p_sys_key->u0.ip.ecn_mask = p_ctc_key->ecn_mask;
                }

                if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_DSCP))
                {
                    CTC_MAX_VALUE_CHECK(p_ctc_key->dscp, 0x3F);
                    p_sys_key->u0.ip.dscp      = (p_ctc_key->dscp);
                    p_sys_key->u0.ip.dscp_mask = (p_ctc_key->dscp_mask) & 0x3F;
                }
                else if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_PRECEDENCE))
                {
                    CTC_MAX_VALUE_CHECK(p_ctc_key->dscp, 7);
                    p_sys_key->u0.ip.dscp      = (p_ctc_key->dscp) << 3;
                    p_sys_key->u0.ip.dscp_mask = (p_ctc_key->dscp_mask & 0x7) << 3;
                }

                if ((CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_IP_HDR_ERROR)) && (CTC_ACL_KEY_SIZE_DOUBLE == p_ctc_key->key_size))
                {
                    CTC_MAX_VALUE_CHECK(p_ctc_key->ip_hdr_error, 1);
                    p_sys_key->ip_header_error = p_ctc_key->ip_hdr_error;
                }
                break;

            case CTC_PARSER_L3_TYPE_ARP:
                if(CTC_FLAG_ISSET(flag,CTC_ACL_IPV4_KEY_FLAG_ETH_TYPE))
                {
                    return CTC_E_ACL_FLAG_COLLISION;
                }

                p_sys_key->unique_l3_type = CTC_PARSER_L3_TYPE_ARP;
                if (CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV4_KEY_SUB_FLAG_ARP_OP_CODE))
                {
                    p_sys_key->u0.arp.op_code      = p_ctc_key->arp_op_code;
                    p_sys_key->u0.arp.op_code_mask = p_ctc_key->arp_op_code_mask;
                }

                if (CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV4_KEY_SUB_FLAG_ARP_SENDER_IP))
                {
                    p_sys_key->u0.arp.sender_ip      = p_ctc_key->sender_ip;
                    p_sys_key->u0.arp.sender_ip_mask = p_ctc_key->sender_ip_mask;
                }

                if (CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV4_KEY_SUB_FLAG_ARP_TARGET_IP))
                {
                    p_sys_key->u0.arp.target_ip      = p_ctc_key->target_ip;
                    p_sys_key->u0.arp.target_ip_mask = p_ctc_key->target_ip_mask;
                }

                if (CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV4_KEY_SUB_FLAG_ARP_PROTOCOL_TYPE))
                {
                    p_sys_key->u0.arp.protocol_type      = p_ctc_key->arp_protocol_type;
                    p_sys_key->u0.arp.protocol_type_mask = p_ctc_key->arp_protocol_type_mask;
                }
                break;

            case CTC_PARSER_L3_TYPE_MPLS:
            case CTC_PARSER_L3_TYPE_MPLS_MCAST:
                if(CTC_FLAG_ISSET(flag,CTC_ACL_IPV4_KEY_FLAG_ETH_TYPE))
                {
                    return CTC_E_ACL_FLAG_COLLISION;
                }

                p_sys_key->unique_l3_type = CTC_PARSER_L3_TYPE_MPLS;
                if (CTC_FLAG_ISSET(sub1_flag, CTC_ACL_IPV4_KEY_SUB1_FLAG_MPLS_LABEL_NUM))
                {
                    CTC_MAX_VALUE_CHECK(p_ctc_key->mpls_label_num, ACL_MAX_LABEL_NUM);
                    p_sys_key->u0.mpls.label_num      = p_ctc_key->mpls_label_num;
                    p_sys_key->u0.mpls.label_num_mask = p_ctc_key->mpls_label_num_mask;
                }

                if (CTC_FLAG_ISSET(sub1_flag, CTC_ACL_IPV4_KEY_SUB1_FLAG_MPLS_LABEL0))
                {
                    p_sys_key->u0.mpls.mpls_label0      = p_ctc_key->mpls_label0;
                    p_sys_key->u0.mpls.mpls_label0_mask = p_ctc_key->mpls_label0_mask;
                }

                if (CTC_FLAG_ISSET(sub1_flag, CTC_ACL_IPV4_KEY_SUB1_FLAG_MPLS_LABEL1))
                {
                    p_sys_key->u0.mpls.mpls_label1      = p_ctc_key->mpls_label1;
                    p_sys_key->u0.mpls.mpls_label1_mask = p_ctc_key->mpls_label1_mask;
                }

                if (CTC_FLAG_ISSET(sub1_flag, CTC_ACL_IPV4_KEY_SUB1_FLAG_MPLS_LABEL2))
                {
                    p_sys_key->u0.mpls.mpls_label2      = p_ctc_key->mpls_label2;
                    p_sys_key->u0.mpls.mpls_label2_mask = p_ctc_key->mpls_label2_mask;
                }
                break;

            case CTC_PARSER_L3_TYPE_FCOE:
            case CTC_PARSER_L3_TYPE_TRILL:
            case CTC_PARSER_L3_TYPE_SLOW_PROTO:
            case CTC_PARSER_L3_TYPE_CMAC:
            case CTC_PARSER_L3_TYPE_PTP:
                if(CTC_FLAG_ISSET(flag,CTC_ACL_IPV4_KEY_FLAG_ETH_TYPE))
                {
                    return CTC_E_ACL_FLAG_COLLISION;
                }
                break;

            case CTC_PARSER_L3_TYPE_ETHER_OAM:
                if(CTC_FLAG_ISSET(flag,CTC_ACL_IPV4_KEY_FLAG_ETH_TYPE))
                {
                    return CTC_E_ACL_FLAG_COLLISION;
                }

                p_sys_key->unique_l3_type = CTC_PARSER_L3_TYPE_ETHER_OAM;
                if (CTC_FLAG_ISSET(sub3_flag, CTC_ACL_IPV4_KEY_SUB3_FLAG_ETHOAM_LEVEL))
                {
                    CTC_MAX_VALUE_CHECK(p_ctc_key->ethoam_level, 0x7);
                    p_sys_key->u0.ethoam.ethoam_level      = p_ctc_key->ethoam_level;
                    p_sys_key->u0.ethoam.ethoam_level_mask = p_ctc_key->ethoam_level_mask;
                }

                if (CTC_FLAG_ISSET(sub3_flag, CTC_ACL_IPV4_KEY_SUB3_FLAG_ETHOAM_OPCODE))
                {
                    p_sys_key->u0.ethoam.ethoam_op_code      = p_ctc_key->ethoam_op_code;
                    p_sys_key->u0.ethoam.ethoam_op_code_mask = p_ctc_key->ethoam_op_code_mask;
                }
                break;

            case CTC_PARSER_L3_TYPE_NONE:
            case CTC_PARSER_L3_TYPE_IP:
            case CTC_PARSER_L3_TYPE_IPV6:
            case CTC_PARSER_L3_TYPE_RSV_USER_DEFINE0:
            case CTC_PARSER_L3_TYPE_RSV_USER_DEFINE1:
            case CTC_PARSER_L3_TYPE_RSV_USER_FLEXL3:
                p_sys_key->unique_l3_type = CTC_PARSER_L3_TYPE_NONE;
                if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_ETH_TYPE))
                {
                    p_sys_key->u0.other.eth_type      = p_ctc_key->eth_type;
                    p_sys_key->u0.other.eth_type_mask = p_ctc_key->eth_type_mask;
                }
                break;

            default:
                break;

        }
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_PKT_LEN_RANGE))
    {
        CTC_ERROR_RETURN(_sys_goldengate_acl_map_pkt_len_range(lchip,
                                p_ctc_key->pkt_len_min, p_ctc_key->pkt_len_max, &p_sys_key->u0.ip.l4));
    }

    /* layer 4 information */
    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_L4_PROTOCOL))
    {
        CTC_ERROR_RETURN(_sys_goldengate_acl_map_l4_protocol(lchip, 0, 0, p_ctc_key, &p_sys_key->u0.ip.l4));
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_acl_map_ipv6_key(uint8 lchip, ctc_acl_ipv6_key_t* p_ctc_key,
                                 sys_acl_sw_tcam_key_ipv6_t* p_sys_key, uint8 dir)
{
    uint32 flag     = 0;
    uint32 sub_flag = 0;
    /*uint8 udf0 = 0;*/
    uint8 udf1 = 0;
    uint8 udf2 = 0;
    uint8 udf3 = 0;
    uint8 udf0_mask = 0;
    uint8 udf1_mask = 0;
    uint8 udf2_mask = 0;
    uint8 udf3_mask = 0;
    uint16 cvid = 0;
    uint16 svid = 0;
    ctc_vlan_range_info_t vrange_info;
    ctc_vlan_range_t vlan_range;

    CTC_PTR_VALID_CHECK(p_ctc_key);
    CTC_PTR_VALID_CHECK(p_sys_key);

    SYS_ACL_DBG_FUNC();

    CTC_ERROR_RETURN(_sys_goldengate_acl_check_ipv6_key(lchip, p_ctc_key->flag));

    sal_memset(&vrange_info, 0, sizeof(ctc_vlan_range_info_t));
    sal_memset(&vlan_range, 0, sizeof(ctc_vlan_range_t));

    flag                = p_ctc_key->flag;
    sub_flag            = p_ctc_key->sub_flag;
    p_sys_key->flag     = flag;
    p_sys_key->sub_flag = sub_flag;

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_IP_DA))
    {
        sal_memcpy(p_sys_key->u0.ip.ip_da, p_ctc_key->ip_da, sizeof(ipv6_addr_t));
        sal_memcpy(p_sys_key->u0.ip.ip_da_mask, p_ctc_key->ip_da_mask, sizeof(ipv6_addr_t));
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_IP_SA))
    {
        sal_memcpy(p_sys_key->u0.ip.ip_sa, p_ctc_key->ip_sa, sizeof(ipv6_addr_t));
        sal_memcpy(p_sys_key->u0.ip.ip_sa_mask, p_ctc_key->ip_sa_mask, sizeof(ipv6_addr_t));
    }

    /* ip fragment */
    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_IP_FRAG))
    {
        uint8 d;
        uint8 m;
        CTC_ERROR_RETURN(_sys_goldengate_acl_map_ip_frag(lchip, p_ctc_key->ip_frag, &d, &m));
        p_sys_key->u0.ip.frag_info      = d;
        p_sys_key->u0.ip.frag_info_mask = m;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_DSCP))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->dscp, 0x3F);
        p_sys_key->u0.ip.dscp      = p_ctc_key->dscp;
        p_sys_key->u0.ip.dscp_mask = p_ctc_key->dscp_mask;
    }
    else if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_PRECEDENCE))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->dscp, 7);
        p_sys_key->u0.ip.dscp      = (p_ctc_key->dscp) << 3;
        p_sys_key->u0.ip.dscp_mask = (p_ctc_key->dscp_mask & 0x7) << 3;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_IP_OPTION))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->ip_option, 1);
        p_sys_key->u0.ip.ip_option = p_ctc_key->ip_option;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_ROUTED_PACKET))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->routed_packet, 1);
        p_sys_key->routed_packet = p_ctc_key->routed_packet;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_FLOW_LABEL))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->flow_label, 0xFFFFF);
        p_sys_key->flow_label      = p_ctc_key->flow_label;
        p_sys_key->flow_label_mask = p_ctc_key->flow_label_mask & 0xFFFFF;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_MAC_DA))
    {
        sal_memcpy(p_sys_key->mac_da, p_ctc_key->mac_da, CTC_ETH_ADDR_LEN);
        sal_memcpy(p_sys_key->mac_da_mask, p_ctc_key->mac_da_mask, CTC_ETH_ADDR_LEN);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_MAC_SA))
    {
        sal_memcpy(p_sys_key->mac_sa, p_ctc_key->mac_sa, CTC_ETH_ADDR_LEN);
        sal_memcpy(p_sys_key->mac_sa_mask, p_ctc_key->mac_sa_mask, CTC_ETH_ADDR_LEN);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_CVLAN))
    {
        SYS_ACL_VLAN_ID_CHECK(p_ctc_key->cvlan);
        SYS_ACL_VLAN_ID_CHECK(p_ctc_key->cvlan_end);

        cvid = p_ctc_key->cvlan;

        vrange_info.direction = dir;
        vrange_info.vrange_grpid = p_ctc_key->vrange_grpid;
        vlan_range.is_acl = 1;
        vlan_range.vlan_start = p_ctc_key->cvlan;
        vlan_range.vlan_end = p_ctc_key->cvlan_end;
        CTC_ERROR_RETURN(sys_goldengate_vlan_get_maxvid_from_vrange(lchip, &vrange_info, &vlan_range, FALSE, &cvid));

        p_sys_key->cvlan = cvid;
        p_sys_key->cvlan_mask = p_ctc_key->cvlan_mask & 0xFFF;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_CTAG_COS))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->ctag_cos, 0x7);
        p_sys_key->ctag_cos      = p_ctc_key->ctag_cos;
        p_sys_key->ctag_cos_mask = (p_ctc_key->ctag_cos_mask) & 0x7;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_CTAG_CFI))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->ctag_cfi, 0x1);
        p_sys_key->ctag_cfi = p_ctc_key->ctag_cfi;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_CTAG_VALID))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->ctag_valid, 0x1);
        p_sys_key->ctag_valid = p_ctc_key->ctag_valid;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_STAG_VALID))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->stag_valid, 0x1);
        p_sys_key->stag_valid = p_ctc_key->stag_valid;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_ECN))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->ecn, 0x3);
        p_sys_key->u0.ip.ecn      = p_ctc_key->ecn;
        p_sys_key->u0.ip.ecn_mask = p_ctc_key->ecn_mask;
    }
    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_SVLAN))
    {
        SYS_ACL_VLAN_ID_CHECK(p_ctc_key->svlan);
        SYS_ACL_VLAN_ID_CHECK(p_ctc_key->svlan_end);

        svid = p_ctc_key->svlan;

        vrange_info.direction = dir;
        vrange_info.vrange_grpid = p_ctc_key->vrange_grpid;
        vlan_range.is_acl = 1;
        vlan_range.vlan_start = p_ctc_key->svlan;
        vlan_range.vlan_end = p_ctc_key->svlan_end;
        CTC_ERROR_RETURN(sys_goldengate_vlan_get_maxvid_from_vrange(lchip, &vrange_info, &vlan_range, TRUE, &svid));

        p_sys_key->svlan = svid;
        p_sys_key->svlan_mask = p_ctc_key->svlan_mask & 0xFFF;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_STAG_COS))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->stag_cos, 0x7);
        p_sys_key->stag_cos      = p_ctc_key->stag_cos;
        p_sys_key->stag_cos_mask = (p_ctc_key->stag_cos_mask) & 0x7;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_STAG_CFI))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->stag_cfi, 0x1);
        p_sys_key->stag_cfi = p_ctc_key->stag_cfi;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_ETH_TYPE))
    {
        p_sys_key->u0.other.eth_type      = p_ctc_key->eth_type;
        p_sys_key->u0.other.eth_type_mask = p_ctc_key->eth_type_mask;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_VLAN_NUM))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->vlan_num, 2);
        p_sys_key->vlan_num = p_ctc_key->vlan_num;
        p_sys_key->vlan_num_mask = p_ctc_key->vlan_num_mask;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_L3_TYPE))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->l3_type, 0xF);
        p_sys_key->l3_type      = p_ctc_key->l3_type;
        p_sys_key->l3_type_mask = p_ctc_key->l3_type_mask;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_PKT_LEN_RANGE))
    {
        CTC_ERROR_RETURN(_sys_goldengate_acl_map_pkt_len_range(lchip,
                                p_ctc_key->pkt_len_min, p_ctc_key->pkt_len_max, &p_sys_key->u0.ip.l4));
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_IP_HDR_ERROR))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->ip_hdr_error, 1);
        p_sys_key->ip_header_error = p_ctc_key->ip_hdr_error;
    }

    /* layer 4 information */
    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_L4_PROTOCOL))
    {
        CTC_ERROR_RETURN(_sys_goldengate_acl_map_l4_protocol(lchip, 1, 0, p_ctc_key, &p_sys_key->u0.ip.l4));
    }

    /* metadata */
    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_METADATA))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->metadata, 0x3FFF);
        p_sys_key->metadata = p_ctc_key->metadata;
        p_sys_key->metadata_mask = p_ctc_key->metadata_mask;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_UDF))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->udf_type, CTC_PARSER_UDF_TYPE_NUM-1);
        /*udf0 = p_ctc_key->udf[0];*/
        udf1 = p_ctc_key->udf[1];
        udf2 = p_ctc_key->udf[2];
        udf3 = p_ctc_key->udf[3];
        p_sys_key->udf = p_ctc_key->udf[0] + (udf1 << 8) + (udf2 << 16) + (udf3 << 24);

        udf0_mask = p_ctc_key->udf_mask[0];
        udf1_mask = p_ctc_key->udf_mask[1];
        udf2_mask = p_ctc_key->udf_mask[2];
        udf3_mask = p_ctc_key->udf_mask[3];
        p_sys_key->udf_mask = udf0_mask + (udf1_mask << 8) + (udf2_mask << 16) + (udf3_mask << 24);

        if(CTC_PARSER_UDF_TYPE_L3_UDF == p_ctc_key->udf_type)
        {
            p_sys_key->udf_type = DRV_UDFTYPE_L3UDF;
        }
        else if(CTC_PARSER_UDF_TYPE_L4_UDF == p_ctc_key->udf_type)
        {
            p_sys_key->udf_type = DRV_UDFTYPE_L4UDF;
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_acl_get_field_sel(uint8 lchip, uint8 key_type, uint32 field_sel_id, void* ds_sel)
{
    uint32 cmd = 0;

    CTC_MAX_VALUE_CHECK(field_sel_id, 0xF);
    if (CTC_ACL_KEY_HASH_MAC == key_type)
    {
        cmd = DRV_IOR(FlowL2HashFieldSelect_t, DRV_ENTRY_FLAG);
    }
    else if (CTC_ACL_KEY_HASH_IPV4 == key_type)
    {
        cmd = DRV_IOR(FlowL3Ipv4HashFieldSelect_t, DRV_ENTRY_FLAG);
    }
    else if (CTC_ACL_KEY_HASH_MPLS == key_type)
    {
        cmd = DRV_IOR(FlowL3MplsHashFieldSelect_t, DRV_ENTRY_FLAG);
    }
    else if (CTC_ACL_KEY_HASH_IPV6 == key_type)
    {
        cmd = DRV_IOR(FlowL3Ipv6HashFieldSelect_t, DRV_ENTRY_FLAG);
    }
    else if (CTC_ACL_KEY_HASH_L2_L3 == key_type)
    {
        cmd = DRV_IOR(FlowL2L3HashFieldSelect_t, DRV_ENTRY_FLAG);
    }

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, field_sel_id, cmd, ds_sel));

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_acl_map_hash_mac_key(uint8 lchip, ctc_acl_hash_mac_key_t* p_ctc_key,
                                     sys_acl_hash_mac_key_t* p_sys_key, uint8 dir)
{
    FlowL2HashFieldSelect_m ds_sel;
    ctc_vlan_range_info_t vrange_info;
    ctc_vlan_range_t vlan_range;
    uint16 vid = 0;

    CTC_PTR_VALID_CHECK(p_ctc_key);
    CTC_PTR_VALID_CHECK(p_sys_key);

    SYS_ACL_DBG_FUNC();

    sal_memset(&ds_sel, 0, sizeof(ds_sel));
    sal_memset(&vrange_info, 0, sizeof(ctc_vlan_range_info_t));
    sal_memset(&vlan_range, 0, sizeof(ctc_vlan_range_t));

    CTC_ERROR_RETURN(_sys_goldengate_acl_get_field_sel(lchip, CTC_ACL_KEY_HASH_MAC, p_ctc_key->field_sel_id, &ds_sel));

    p_sys_key->field_sel_id = p_ctc_key->field_sel_id;
    p_sys_key->is_ctag      = p_ctc_key->is_ctag;

    if (GetFlowL2HashFieldSelect(V, macDaEn_f, &ds_sel))
    {
        sal_memcpy(p_sys_key->mac_da, p_ctc_key->mac_da, CTC_ETH_ADDR_LEN);
    }

    if (GetFlowL2HashFieldSelect(V, macSaEn_f, &ds_sel))
    {
        sal_memcpy(p_sys_key->mac_sa, p_ctc_key->mac_sa, CTC_ETH_ADDR_LEN);
    }

    if (GetFlowL2HashFieldSelect(V, globalSrcPortEn_f, &ds_sel))
    {
        CTC_GLOBAL_PORT_CHECK(p_ctc_key->gport);
        if(!CTC_IS_LINKAGG_PORT(p_ctc_key->gport))
        {
            if(FALSE == sys_goldengate_chip_is_local(lchip, CTC_MAP_GPORT_TO_GCHIP(p_ctc_key->gport)))
            {       /*chip is not local*/
                return CTC_E_CHIP_IS_REMOTE;
            }
        }
        p_sys_key->gport = SYS_MAP_CTC_GPORT_TO_DRV_GPORT(p_ctc_key->gport);
    }
    else if (GetFlowL2HashFieldSelect(V, logicSrcPortEn_f, &ds_sel))
    {
        SYS_ACL_SERVICE_ID_CHECK(p_ctc_key->gport);
        p_sys_key->gport = p_ctc_key->gport;
    }
    else if (GetFlowL2HashFieldSelect(V, metadataEn_f, &ds_sel))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->gport, 0x3FFF);
        p_sys_key->gport = p_ctc_key->gport;
    }

    if (GetFlowL2HashFieldSelect(V, etherTypeEn_f, &ds_sel))
    {
        p_sys_key->eth_type = p_ctc_key->eth_type;
    }

    if (GetFlowL2HashFieldSelect(V, stagCosEn_f, &ds_sel))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->cos, 7);
        p_sys_key->cos = p_ctc_key->cos;
    }

    if (GetFlowL2HashFieldSelect(V, svlanIdEn_f, &ds_sel))
    {
        SYS_ACL_VLAN_ID_CHECK(p_ctc_key->vlan_id);
        SYS_ACL_VLAN_ID_CHECK(p_ctc_key->vlan_end);

        vid = p_ctc_key->vlan_id;

        vrange_info.direction = dir;
        vrange_info.vrange_grpid = p_ctc_key->vrange_grpid;
        vlan_range.is_acl = 1;
        vlan_range.vlan_start = p_ctc_key->vlan_id;
        vlan_range.vlan_end = p_ctc_key->vlan_end;
        CTC_ERROR_RETURN(sys_goldengate_vlan_get_maxvid_from_vrange(lchip, &vrange_info, &vlan_range, p_ctc_key->is_ctag ? FALSE : TRUE, &vid));

        p_sys_key->vlan_id = vid;
    }

    if (GetFlowL2HashFieldSelect(V, svlanIdValidEn_f, &ds_sel))
    {
        p_sys_key->tag_exist = p_ctc_key->tag_valid;
    }

    if (GetFlowL2HashFieldSelect(V, stagCfiEn_f, &ds_sel))
    {
        p_sys_key->cfi = p_ctc_key->cfi;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_acl_map_hash_ipv4_key(uint8 lchip, ctc_acl_hash_ipv4_key_t* p_ctc_key,
                                      sys_acl_hash_ipv4_key_t* p_sys_key)
{
    uint8 da_len = 0;
    uint8 sa_len = 0;
    FlowL3Ipv4HashFieldSelect_m ds_sel;

    CTC_PTR_VALID_CHECK(p_ctc_key);
    CTC_PTR_VALID_CHECK(p_sys_key);

    SYS_ACL_DBG_FUNC();

    sal_memset(&ds_sel, 0, sizeof(ds_sel));

    CTC_ERROR_RETURN(_sys_goldengate_acl_get_field_sel(lchip, CTC_ACL_KEY_HASH_IPV4, p_ctc_key->field_sel_id, &ds_sel));

    p_sys_key->field_sel_id = p_ctc_key->field_sel_id;

    da_len = GetFlowL3Ipv4HashFieldSelect(V, ipDaEn_f, &ds_sel);
    if (da_len)
    {
        p_sys_key->ip_da = (p_ctc_key->ip_da >> (31 - da_len)) << (31 - da_len);
    }

    sa_len = GetFlowL3Ipv4HashFieldSelect(V, ipSaEn_f, &ds_sel);
    if (sa_len)
    {
        p_sys_key->ip_sa = (p_ctc_key->ip_sa >> (31 - sa_len)) << (31 - sa_len);
    }

    if (GetFlowL3Ipv4HashFieldSelect(V, l4SourcePortEn_f, &ds_sel))
    {
        p_sys_key->l4_src_port = p_ctc_key->l4_src_port;
    }

    if (GetFlowL3Ipv4HashFieldSelect(V, l4DestPortEn_f, &ds_sel))
    {
        p_sys_key->l4_dst_port = p_ctc_key->l4_dst_port;
    }

    if (GetFlowL3Ipv4HashFieldSelect(V, globalSrcPortEn_f, &ds_sel))
    {
        CTC_GLOBAL_PORT_CHECK(p_ctc_key->gport);
        if(!CTC_IS_LINKAGG_PORT(p_ctc_key->gport))
        {
            if(FALSE == sys_goldengate_chip_is_local(lchip, CTC_MAP_GPORT_TO_GCHIP(p_ctc_key->gport)))
            {       /*chip is not local*/
                return CTC_E_CHIP_IS_REMOTE;
            }
        }
        p_sys_key->gport = SYS_MAP_CTC_GPORT_TO_DRV_GPORT(p_ctc_key->gport);
    }
    else if (GetFlowL3Ipv4HashFieldSelect(V, logicSrcPortEn_f, &ds_sel))
    {
        SYS_ACL_SERVICE_ID_CHECK(p_ctc_key->gport);
        p_sys_key->gport = p_ctc_key->gport;
    }

    if (GetFlowL3Ipv4HashFieldSelect(V, dscpEn_f, &ds_sel))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->dscp, 0x3F);
        p_sys_key->dscp = p_ctc_key->dscp;
    }

    if (GetFlowL3Ipv4HashFieldSelect(V, layer3HeaderProtocolEn_f, &ds_sel))
    {
        p_sys_key->l4_protocol = p_ctc_key->l4_protocol;
    }

    if (GetFlowL3Ipv4HashFieldSelect(V, metadataEn_f, &ds_sel))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->metadata, 0x3FFF);
        p_sys_key->metadata = p_ctc_key->metadata;
    }

    if (GetFlowL3Ipv4HashFieldSelect(V, ecnEn_f, &ds_sel))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->ecn, 3);
        p_sys_key->ecn = p_ctc_key->ecn;
    }

    if (GetFlowL3Ipv4HashFieldSelect(V, icmpTypeEn_f, &ds_sel))
    {
        p_sys_key->l4_src_port |= p_ctc_key->icmp_type << 8;
    }

    if (GetFlowL3Ipv4HashFieldSelect(V, icmpOpcodeEn_f, &ds_sel))
    {
        p_sys_key->l4_src_port |= p_ctc_key->icmp_code & 0xFF;
    }

    if (GetFlowL3Ipv4HashFieldSelect(V, vxlanVniEn_f, &ds_sel))
    {
        p_sys_key->vni = p_ctc_key->vni;
    }
    if (GetFlowL3Ipv4HashFieldSelect(V, greKeyEn_f, &ds_sel))
    {
        p_sys_key->gre_key = p_ctc_key->gre_key;
    }
    /*
       --- hidden --
       layer4UserTypeEn_f
       greKeyEn_f
       isRouteMacEn_f
       vxlanVniEn_f
       ignorVxlan_f
     */

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_acl_map_hash_mpls_key(uint8 lchip, ctc_acl_hash_mpls_key_t* p_ctc_key,
                                      sys_acl_hash_mpls_key_t* p_sys_key)
{
    FlowL3MplsHashFieldSelect_m ds_sel;

    CTC_PTR_VALID_CHECK(p_ctc_key);
    CTC_PTR_VALID_CHECK(p_sys_key);

    SYS_ACL_DBG_FUNC();

    sal_memset(&ds_sel, 0, sizeof(ds_sel));

    CTC_ERROR_RETURN(_sys_goldengate_acl_get_field_sel(lchip, CTC_ACL_KEY_HASH_MPLS, p_ctc_key->field_sel_id, &ds_sel));

    p_sys_key->field_sel_id = p_ctc_key->field_sel_id;

    if (GetFlowL3MplsHashFieldSelect(V, metadataEn_f, &ds_sel))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->metadata, 0x3FFF);
        p_sys_key->metadata = p_ctc_key->metadata;
    }

    if (GetFlowL3MplsHashFieldSelect(V, globalSrcPortEn_f, &ds_sel))
    {
        CTC_GLOBAL_PORT_CHECK(p_ctc_key->gport);
        if(!CTC_IS_LINKAGG_PORT(p_ctc_key->gport))
        {
            if(FALSE == sys_goldengate_chip_is_local(lchip, CTC_MAP_GPORT_TO_GCHIP(p_ctc_key->gport)))
            {       /*chip is not local*/
                return CTC_E_CHIP_IS_REMOTE;
            }
        }
        p_sys_key->gport = SYS_MAP_CTC_GPORT_TO_DRV_GPORT(p_ctc_key->gport);
    }
    else if (GetFlowL3MplsHashFieldSelect(V, logicSrcPortEn_f, &ds_sel))
    {
        SYS_ACL_SERVICE_ID_CHECK(p_ctc_key->gport);
        p_sys_key->gport = p_ctc_key->gport;
    }

    if (GetFlowL3MplsHashFieldSelect(V, labelNumEn_f, &ds_sel))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->mpls_label_num, ACL_MAX_LABEL_NUM);
        p_sys_key->label_num = p_ctc_key->mpls_label_num;
    }

    if (GetFlowL3MplsHashFieldSelect(V, mplsTtl0En_f, &ds_sel))
    {
        p_sys_key->mpls_ttl0 = p_ctc_key->mpls_label0_ttl;
    }

    if (GetFlowL3MplsHashFieldSelect(V, mplsSbit0En_f, &ds_sel))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->mpls_label0_s, 1);
        p_sys_key->mpls_s0 = p_ctc_key->mpls_label0_s;
    }

    if (GetFlowL3MplsHashFieldSelect(V, mplsExp0En_f, &ds_sel))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->mpls_label0_exp, 7);
        p_sys_key->mpls_exp0 = p_ctc_key->mpls_label0_exp;
    }

    if (GetFlowL3MplsHashFieldSelect(V, mplsLabel0En_f, &ds_sel))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->mpls_label0_label, 0xFFFFF);
        p_sys_key->mpls_label0 = p_ctc_key->mpls_label0_label;
    }

    if (GetFlowL3MplsHashFieldSelect(V, mplsTtl1En_f, &ds_sel))
    {
        p_sys_key->mpls_ttl1 = p_ctc_key->mpls_label1_ttl;
    }

    if (GetFlowL3MplsHashFieldSelect(V, mplsSbit1En_f, &ds_sel))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->mpls_label1_s, 1);
        p_sys_key->mpls_s1 = p_ctc_key->mpls_label1_s;
    }

    if (GetFlowL3MplsHashFieldSelect(V, mplsExp1En_f, &ds_sel))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->mpls_label1_exp, 7);
        p_sys_key->mpls_exp1 = p_ctc_key->mpls_label1_exp;
    }

    if (GetFlowL3MplsHashFieldSelect(V, mplsLabel1En_f, &ds_sel))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->mpls_label1_label, 0xFFFFF);
        p_sys_key->mpls_label1 = p_ctc_key->mpls_label1_label;
    }


    if (GetFlowL3MplsHashFieldSelect(V, mplsTtl2En_f, &ds_sel))
    {
        p_sys_key->mpls_ttl2 = p_ctc_key->mpls_label2_ttl;
    }

    if (GetFlowL3MplsHashFieldSelect(V, mplsSbit2En_f, &ds_sel))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->mpls_label2_s, 1);
        p_sys_key->mpls_s2 = p_ctc_key->mpls_label2_s;
    }

    if (GetFlowL3MplsHashFieldSelect(V, mplsExp2En_f, &ds_sel))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->mpls_label2_exp, 7);
        p_sys_key->mpls_exp2 = p_ctc_key->mpls_label2_exp;
    }

    if (GetFlowL3MplsHashFieldSelect(V, mplsLabel2En_f, &ds_sel))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->mpls_label2_label, 0xFFFFF);
        p_sys_key->mpls_label2 = p_ctc_key->mpls_label2_label;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_acl_map_hash_ipv6_key(uint8 lchip, ctc_acl_hash_ipv6_key_t* p_ctc_key,
                                      sys_acl_hash_ipv6_key_t* p_sys_key)
{
    uint8 da_len = 0;
    uint8 sa_len = 0;
    int8  idx = 0;
    uint8 words = 0;
    uint8 bits = 0;
    FlowL3Ipv6HashFieldSelect_m ds_sel;

    CTC_PTR_VALID_CHECK(p_ctc_key);
    CTC_PTR_VALID_CHECK(p_sys_key);

    SYS_ACL_DBG_FUNC();

    sal_memset(&ds_sel, 0, sizeof(ds_sel));

    CTC_ERROR_RETURN(_sys_goldengate_acl_get_field_sel(lchip, CTC_ACL_KEY_HASH_IPV6, p_ctc_key->field_sel_id, &ds_sel));

    p_sys_key->field_sel_id = p_ctc_key->field_sel_id;

    if (GetFlowL3Ipv6HashFieldSelect(V, metadataEn_f, &ds_sel))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->gport, 0x3FFF);
        p_sys_key->gport = p_ctc_key->gport;
    }
    else if (GetFlowL3Ipv6HashFieldSelect(V, globalSrcPortEn_f, &ds_sel))
    {
        CTC_GLOBAL_PORT_CHECK(p_ctc_key->gport);
        if(!CTC_IS_LINKAGG_PORT(p_ctc_key->gport))
        {
            if(FALSE == sys_goldengate_chip_is_local(lchip, CTC_MAP_GPORT_TO_GCHIP(p_ctc_key->gport)))
            {       /*chip is not local*/
                return CTC_E_CHIP_IS_REMOTE;
            }
        }
        p_sys_key->gport = SYS_MAP_CTC_GPORT_TO_DRV_GPORT(p_ctc_key->gport);
    }
    else if (GetFlowL3Ipv6HashFieldSelect(V, logicSrcPortEn_f, &ds_sel))
    {
        SYS_ACL_SERVICE_ID_CHECK(p_ctc_key->gport);
        p_sys_key->gport = p_ctc_key->gport;
    }

    if (GetFlowL3Ipv6HashFieldSelect(V, dscpEn_f, &ds_sel))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->dscp, 0x3F);
        p_sys_key->dscp = p_ctc_key->dscp;
    }

    da_len = GetFlowL3Ipv6HashFieldSelect(V, ipDaEn_f, &ds_sel);
    if (da_len)
    {
        words = ((da_len << 2) + 4) /32;
        bits  = ((da_len << 2) + 4) %32;
        sal_memcpy(p_sys_key->ip_da, p_ctc_key->ip_da, sizeof(ipv6_addr_t));

        for (idx = 3; idx > words; idx--)
        {
            sal_memset(p_sys_key->ip_da + idx, 0, sizeof(uint32));
        }

        if (bits)
        {
            p_sys_key->ip_da[words] = (p_sys_key->ip_da[words] >> (32 - bits) ) << (32 - bits);
        }
    }

    sa_len = GetFlowL3Ipv6HashFieldSelect(V, ipSaEn_f, &ds_sel) << 2;
    if (sa_len)
    {
        words = ((sa_len << 2) + 4) /32;
        bits  = ((sa_len << 2) + 4) %32;
        sal_memcpy(p_sys_key->ip_sa, p_ctc_key->ip_sa, sizeof(ipv6_addr_t));

        for (idx = 3; idx > words; idx--)
        {
            sal_memset(p_sys_key->ip_sa + idx, 0, sizeof(uint32));
        }

        if (bits)
        {
            p_sys_key->ip_sa[words] = (p_sys_key->ip_sa[words] >> (32 - bits) ) << (32 - bits);
        }
    }

    if (GetFlowL3Ipv6HashFieldSelect(V, l4SourcePortEn_f, &ds_sel))
    {
        p_sys_key->l4_src_port = p_ctc_key->l4_src_port;
    }

    if (GetFlowL3Ipv6HashFieldSelect(V, l4DestPortEn_f, &ds_sel))
    {
        p_sys_key->l4_dst_port = p_ctc_key->l4_dst_port;
    }

    if (GetFlowL3Ipv6HashFieldSelect(V, layer4TypeEn_f, &ds_sel))
    {
        p_sys_key->l4_type = p_ctc_key->l4_type;
    }

    if (GetFlowL3Ipv6HashFieldSelect(V, icmpTypeEn_f, &ds_sel))
    {
        p_sys_key->l4_src_port |= p_ctc_key->icmp_type << 8;
    }

    if (GetFlowL3Ipv6HashFieldSelect(V, icmpOpcodeEn_f, &ds_sel))
    {
        p_sys_key->l4_src_port |= p_ctc_key->icmp_code & 0xFF;
    }

    if (GetFlowL3Ipv6HashFieldSelect(V, vxlanVniEn_f, &ds_sel))
    {
        p_sys_key->vni = p_ctc_key->vni;
    }

    if (GetFlowL3Ipv6HashFieldSelect(V, greKeyEn_f, &ds_sel))
    {
        p_sys_key->gre_key = p_ctc_key->gre_key;
    }

/*
   --- hidden ---
   ipv6FlowLabelEn_f
   greKeyEn_f
   vxlanVniEn_f
   isVxlanEn_f
   ignorVxlan_f
 */

    return CTC_E_NONE;
}



STATIC int32
_sys_goldengate_acl_map_hash_l2_l3_key(uint8 lchip, ctc_acl_hash_l2_l3_key_t* p_ctc_key,
                                       sys_acl_hash_l2_l3_key_t* p_sys_key, uint8 dir)
{
    FlowL2L3HashFieldSelect_m ds_sel;
    ctc_vlan_range_info_t vrange_info;
    ctc_vlan_range_t vlan_range;
    uint16 cvid = 0;
    uint16 svid = 0;

    CTC_PTR_VALID_CHECK(p_ctc_key);
    CTC_PTR_VALID_CHECK(p_sys_key);

    SYS_ACL_DBG_FUNC();

    sal_memset(&ds_sel, 0, sizeof(ds_sel));
    sal_memset(&vrange_info, 0, sizeof(ctc_vlan_range_info_t));
    sal_memset(&vlan_range, 0, sizeof(ctc_vlan_range_t));

    CTC_ERROR_RETURN(_sys_goldengate_acl_get_field_sel(lchip, CTC_ACL_KEY_HASH_L2_L3, p_ctc_key->field_sel_id, &ds_sel));

    p_sys_key->field_sel_id = p_ctc_key->field_sel_id;

/* later */
 /*    if (GetFlowL2L3HashFieldSelect(V, layer3TypeEn_f, &ds_sel))*/
 /*    {*/
        CTC_MAX_VALUE_CHECK(p_ctc_key->l3_type, 0xF);
        p_sys_key->l3_type      = p_ctc_key->l3_type;
 /*    }*/

    if (GetFlowL2L3HashFieldSelect(V, metadataEn_f, &ds_sel))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->metadata, 0x3FFF);
        p_sys_key->metadata = p_ctc_key->metadata;
    }

    if (GetFlowL2L3HashFieldSelect(V, globalSrcPortEn_f, &ds_sel))
    {
        CTC_GLOBAL_PORT_CHECK(p_ctc_key->gport);
        if(!CTC_IS_LINKAGG_PORT(p_ctc_key->gport))
        {
            if(FALSE == sys_goldengate_chip_is_local(lchip, CTC_MAP_GPORT_TO_GCHIP(p_ctc_key->gport)))
            {       /*chip is not local*/
                return CTC_E_CHIP_IS_REMOTE;
            }
        }
        p_sys_key->gport = SYS_MAP_CTC_GPORT_TO_DRV_GPORT(p_ctc_key->gport);
    }
    else if (GetFlowL2L3HashFieldSelect(V, logicSrcPortEn_f, &ds_sel))
    {
        SYS_ACL_SERVICE_ID_CHECK(p_ctc_key->gport);
        p_sys_key->gport = p_ctc_key->gport;
    }

    if (GetFlowL2L3HashFieldSelect(V, macDaEn_f, &ds_sel))
    {
        sal_memcpy(p_sys_key->mac_da, p_ctc_key->mac_da, CTC_ETH_ADDR_LEN);
    }

    if (GetFlowL2L3HashFieldSelect(V, macSaEn_f, &ds_sel))
    {
        sal_memcpy(p_sys_key->mac_sa, p_ctc_key->mac_sa, CTC_ETH_ADDR_LEN);
    }

    if (GetFlowL2L3HashFieldSelect(V, stagCosEn_f, &ds_sel))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->stag_cos, 7);
        p_sys_key->stag_cos = p_ctc_key->stag_cos;
    }

    if (GetFlowL2L3HashFieldSelect(V, svlanIdEn_f, &ds_sel))
    {
        SYS_ACL_VLAN_ID_CHECK(p_ctc_key->stag_vlan);
        SYS_ACL_VLAN_ID_CHECK(p_ctc_key->svlan_end);

        svid = p_ctc_key->stag_vlan;

        vrange_info.direction = dir;
        vrange_info.vrange_grpid = p_ctc_key->vrange_grpid;
        vlan_range.is_acl = 1;
        vlan_range.vlan_start = p_ctc_key->stag_vlan;
        vlan_range.vlan_end = p_ctc_key->svlan_end;
        CTC_ERROR_RETURN(sys_goldengate_vlan_get_maxvid_from_vrange(lchip, &vrange_info, &vlan_range, TRUE, &svid));

        p_sys_key->stag_vid = svid;
    }

    if (GetFlowL2L3HashFieldSelect(V, svlanIdValidEn_f, &ds_sel))
    {
        p_sys_key->stag_valid = p_ctc_key->stag_valid;
    }

    if (GetFlowL2L3HashFieldSelect(V, stagCfiEn_f, &ds_sel))
    {
        p_sys_key->stag_cfi = p_ctc_key->stag_cfi;
    }

    if (GetFlowL2L3HashFieldSelect(V, ctagCosEn_f, &ds_sel))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->ctag_cos, 7);
        p_sys_key->ctag_cos = p_ctc_key->ctag_cos;
    }

    if (GetFlowL2L3HashFieldSelect(V, cvlanIdEn_f, &ds_sel))
    {
        SYS_ACL_VLAN_ID_CHECK(p_ctc_key->ctag_vlan);
        SYS_ACL_VLAN_ID_CHECK(p_ctc_key->cvlan_end);

        cvid = p_ctc_key->ctag_vlan;

        vrange_info.direction = dir;
        vrange_info.vrange_grpid = p_ctc_key->vrange_grpid;
        vlan_range.is_acl = 1;
        vlan_range.vlan_start = p_ctc_key->ctag_vlan;
        vlan_range.vlan_end = p_ctc_key->cvlan_end;
        CTC_ERROR_RETURN(sys_goldengate_vlan_get_maxvid_from_vrange(lchip, &vrange_info, &vlan_range, FALSE, &cvid));

        p_sys_key->ctag_vid = cvid;
    }

    if (GetFlowL2L3HashFieldSelect(V, cvlanIdValidEn_f, &ds_sel))
    {
        p_sys_key->ctag_valid = p_ctc_key->ctag_valid;
    }

    if (GetFlowL2L3HashFieldSelect(V, ctagCfiEn_f, &ds_sel))
    {
        p_sys_key->ctag_cfi = p_ctc_key->ctag_cfi;
    }

    if (GetFlowL2L3HashFieldSelect(V, l4SourcePortEn_f, &ds_sel))
    {
        p_sys_key->l4_src_port = p_ctc_key->l4_src_port;
    }

    if (GetFlowL2L3HashFieldSelect(V, l4DestPortEn_f, &ds_sel))
    {
        p_sys_key->l4_dst_port = p_ctc_key->l4_dst_port;
    }

    if (GetFlowL2L3HashFieldSelect(V, etherTypeEn_f, &ds_sel))
    {
        p_sys_key->eth_type = p_ctc_key->eth_type;
    }

    if (GetFlowL2L3HashFieldSelect(V, layer4TypeEn_f, &ds_sel))
    {
        p_sys_key->l4_type = p_ctc_key->l4_type;
    }

    if (GetFlowL2L3HashFieldSelect(V, icmpTypeEn_f, &ds_sel))
    {
        p_sys_key->l4_src_port |= p_ctc_key->icmp_type << 8;
    }

    if (GetFlowL2L3HashFieldSelect(V, icmpOpcodeEn_f, &ds_sel))
    {
        p_sys_key->l4_src_port |= p_ctc_key->icmp_code & 0xFF;
    }
    if (GetFlowL2L3HashFieldSelect(V, vxlanVniEn_f, &ds_sel))
    {
        p_sys_key->vni = p_ctc_key->vni;
    }

    if (GetFlowL2L3HashFieldSelect(V, greKeyEn_f, &ds_sel))
    {
        p_sys_key->gre_key = p_ctc_key->gre_key;
    }
    if (CTC_PARSER_L3_TYPE_IPV4 == p_sys_key->l3_type)
    {
        uint8 da_len = 0;
        uint8 sa_len = 0;
        if (GetFlowL2L3HashFieldSelect(V, dscpEn_f, &ds_sel))
        {
            CTC_MAX_VALUE_CHECK(p_ctc_key->dscp, 0x3F);
            p_sys_key->l3.ipv4.dscp = p_ctc_key->dscp;
        }

        if (GetFlowL2L3HashFieldSelect(V, ttlEn_f, &ds_sel))
        {
            p_sys_key->l3.ipv4.ttl = p_ctc_key->ttl;
        }

        if (GetFlowL2L3HashFieldSelect(V, ecnEn_f, &ds_sel))
        {
            CTC_MAX_VALUE_CHECK(p_ctc_key->ecn, 3);
            p_sys_key->l3.ipv4.ecn = p_ctc_key->ecn;
        }

        da_len = GetFlowL2L3HashFieldSelect(V, ipDaEn_f, &ds_sel);
        if (da_len)
        {
            p_sys_key->l3.ipv4.ip_da = (p_ctc_key->ip_da >> (31 - da_len)) << (31 - da_len);
        }

        sa_len = GetFlowL2L3HashFieldSelect(V, ipSaEn_f, &ds_sel);
        if (sa_len)
        {
            p_sys_key->l3.ipv4.ip_sa = (p_ctc_key->ip_sa >> (31 - sa_len)) << (31 - sa_len);
        }
    }
    else if ((CTC_PARSER_L3_TYPE_MPLS == p_sys_key->l3_type) ||
             (CTC_PARSER_L3_TYPE_MPLS_MCAST == p_sys_key->l3_type))
    {
        if (GetFlowL2L3HashFieldSelect(V, labelNumEn_f, &ds_sel))
        {
            CTC_MAX_VALUE_CHECK(p_ctc_key->mpls_label_num, ACL_MAX_LABEL_NUM);
            p_sys_key->l3.mpls.label_num = p_ctc_key->mpls_label_num;
        }

        if (GetFlowL2L3HashFieldSelect(V, mplsTtl0En_f, &ds_sel))
        {
            p_sys_key->l3.mpls.mpls_ttl0 = p_ctc_key->mpls_label0_ttl;
        }

        if (GetFlowL2L3HashFieldSelect(V, mplsSbit0En_f, &ds_sel))
        {
            CTC_MAX_VALUE_CHECK(p_ctc_key->mpls_label0_s, 0);
            p_sys_key->l3.mpls.mpls_s0 = p_ctc_key->mpls_label0_s;
        }

        if (GetFlowL2L3HashFieldSelect(V, mplsExp0En_f, &ds_sel))
        {
            CTC_MAX_VALUE_CHECK(p_ctc_key->mpls_label0_exp, 7);
            p_sys_key->l3.mpls.mpls_exp0 = p_ctc_key->mpls_label0_exp;
        }

        if (GetFlowL2L3HashFieldSelect(V, mplsLabel0En_f, &ds_sel))
        {
            CTC_MAX_VALUE_CHECK(p_ctc_key->mpls_label0_label, 0xFFFFF);
            p_sys_key->l3.mpls.mpls_label0 = p_ctc_key->mpls_label0_label;
        }

        if (GetFlowL2L3HashFieldSelect(V, mplsTtl1En_f, &ds_sel))
        {
            p_sys_key->l3.mpls.mpls_ttl1 = p_ctc_key->mpls_label1_ttl;
        }

        if (GetFlowL2L3HashFieldSelect(V, mplsSbit1En_f, &ds_sel))
        {
            CTC_MAX_VALUE_CHECK(p_ctc_key->mpls_label1_s, 1);
            p_sys_key->l3.mpls.mpls_s1 = p_ctc_key->mpls_label1_s;
        }

        if (GetFlowL2L3HashFieldSelect(V, mplsExp1En_f, &ds_sel))
        {
            CTC_MAX_VALUE_CHECK(p_ctc_key->mpls_label1_exp, 7);
            p_sys_key->l3.mpls.mpls_exp1 = p_ctc_key->mpls_label1_exp;
        }

        if (GetFlowL2L3HashFieldSelect(V, mplsLabel1En_f, &ds_sel))
        {
            CTC_MAX_VALUE_CHECK(p_ctc_key->mpls_label1_label, 0xFFFFF);
            p_sys_key->l3.mpls.mpls_label1 = p_ctc_key->mpls_label1_label;
        }


        if (GetFlowL2L3HashFieldSelect(V, mplsTtl2En_f, &ds_sel))
        {
            p_sys_key->l3.mpls.mpls_ttl2 = p_ctc_key->mpls_label2_ttl;
        }

        if (GetFlowL2L3HashFieldSelect(V, mplsSbit2En_f, &ds_sel))
        {
            CTC_MAX_VALUE_CHECK(p_ctc_key->mpls_label2_s, 1);
            p_sys_key->l3.mpls.mpls_s2 = p_ctc_key->mpls_label2_s;
        }

        if (GetFlowL2L3HashFieldSelect(V, mplsExp2En_f, &ds_sel))
        {
            CTC_MAX_VALUE_CHECK(p_ctc_key->mpls_label2_exp, 7);
            p_sys_key->l3.mpls.mpls_exp2 = p_ctc_key->mpls_label2_exp;
        }

        if (GetFlowL2L3HashFieldSelect(V, mplsLabel2En_f, &ds_sel))
        {
            CTC_MAX_VALUE_CHECK(p_ctc_key->mpls_label2_label, 0xFFFFF);
            p_sys_key->l3.mpls.mpls_label2 = p_ctc_key->mpls_label2_label;
        }

    }

/*
   --- hidden ---
   l4InfoMappedEn_f
   vxlanVniEn_f
   ignorVxlan_f
   greKeyEn_f
   layer4UserTypeEn_f
 */


    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_acl_map_pbr_ipv4_key(uint8 lchip, ctc_acl_pbr_ipv4_key_t* p_ctc_key,
                                     sys_acl_pbr_ipv4_key_t* p_sys_key)
{
    uint32 flag     = 0;

    CTC_PTR_VALID_CHECK(p_ctc_key);
    CTC_PTR_VALID_CHECK(p_sys_key);

    SYS_ACL_DBG_FUNC();

    p_sys_key->flag     = p_ctc_key->flag;
    p_sys_key->sub_flag = p_ctc_key->sub_flag;

    flag     = p_ctc_key->flag;

    if (CTC_FLAG_ISSET(flag, CTC_ACL_PBR_IPV4_KEY_FLAG_IP_DA))
    {
        p_sys_key->u0.ip.ip_da      = p_ctc_key->ip_da;
        p_sys_key->u0.ip.ip_da_mask = p_ctc_key->ip_da_mask;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_PBR_IPV4_KEY_FLAG_IP_SA))
    {
        p_sys_key->u0.ip.ip_sa      = p_ctc_key->ip_sa;
        p_sys_key->u0.ip.ip_sa_mask = p_ctc_key->ip_sa_mask;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_PBR_IPV4_KEY_FLAG_L4_PROTOCOL))
    {
        CTC_ERROR_RETURN(_sys_goldengate_acl_map_l4_protocol(lchip, 0, 1, p_ctc_key, &p_sys_key->u0.ip.l4));
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_PBR_IPV4_KEY_FLAG_IP_FRAG))
    {
        uint8 d;
        uint8 m;
        CTC_ERROR_RETURN(_sys_goldengate_acl_map_ip_frag(lchip, p_ctc_key->ip_frag, &d, &m));
        p_sys_key->u0.ip.frag_info      = d;
        p_sys_key->u0.ip.frag_info_mask = m;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_PBR_IPV4_KEY_FLAG_VRFID))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->vrfid, 0x3FF);
        p_sys_key->vrfid      = p_ctc_key->vrfid;
        p_sys_key->vrfid_mask = p_ctc_key->vrfid_mask;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_PBR_IPV4_KEY_FLAG_DSCP))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->dscp, 0x3F);
        p_sys_key->u0.ip.dscp      = p_ctc_key->dscp;
        p_sys_key->u0.ip.dscp_mask = (p_ctc_key->dscp_mask) & 0x3F;
    }
    else if (CTC_FLAG_ISSET(flag, CTC_ACL_PBR_IPV4_KEY_FLAG_PRECEDENCE))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->dscp, 7);
        p_sys_key->u0.ip.dscp      = (p_ctc_key->dscp) << 3;
        p_sys_key->u0.ip.dscp_mask = (p_ctc_key->dscp_mask & 0x7) << 3;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_acl_map_pbr_ipv6_key(uint8 lchip, ctc_acl_pbr_ipv6_key_t* p_ctc_key,
                                     sys_acl_pbr_ipv6_key_t* p_sys_key)
{
    uint32 flag     = 0;

    CTC_PTR_VALID_CHECK(p_ctc_key);
    CTC_PTR_VALID_CHECK(p_sys_key);

    SYS_ACL_DBG_FUNC();

    CTC_ERROR_RETURN(_sys_goldengate_acl_check_pbr_ipv6_key(lchip, p_ctc_key->flag));
    p_sys_key->flag     = p_ctc_key->flag;
    p_sys_key->sub_flag = p_ctc_key->sub_flag;

    flag     = p_ctc_key->flag;

    if (CTC_FLAG_ISSET(flag, CTC_ACL_PBR_IPV6_KEY_FLAG_IP_DA))
    {
        sal_memcpy(p_sys_key->u0.ip.ip_da, p_ctc_key->ip_da, sizeof(ipv6_addr_t));
        sal_memcpy(p_sys_key->u0.ip.ip_da_mask, p_ctc_key->ip_da_mask, sizeof(ipv6_addr_t));
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_PBR_IPV6_KEY_FLAG_IP_SA))
    {
        sal_memcpy(p_sys_key->u0.ip.ip_sa, p_ctc_key->ip_sa, sizeof(ipv6_addr_t));
        sal_memcpy(p_sys_key->u0.ip.ip_sa_mask, p_ctc_key->ip_sa_mask, sizeof(ipv6_addr_t));
    }

    /* layer 4 information */
    if (CTC_FLAG_ISSET(flag, CTC_ACL_PBR_IPV6_KEY_FLAG_L4_PROTOCOL))
    {
        CTC_ERROR_RETURN(_sys_goldengate_acl_map_l4_protocol(lchip, 1, 1, p_ctc_key, &p_sys_key->u0.ip.l4));
    }

    /* ip fragment */
    if (CTC_FLAG_ISSET(flag, CTC_ACL_PBR_IPV6_KEY_FLAG_IP_FRAG))
    {
        uint8 d;
        uint8 m;
        CTC_ERROR_RETURN(_sys_goldengate_acl_map_ip_frag(lchip, p_ctc_key->ip_frag, &d, &m));
        p_sys_key->u0.ip.frag_info      = d;
        p_sys_key->u0.ip.frag_info_mask = m;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_PBR_IPV6_KEY_FLAG_DSCP))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->dscp, 0x3F);
        p_sys_key->u0.ip.dscp      = p_ctc_key->dscp;
        p_sys_key->u0.ip.dscp_mask = p_ctc_key->dscp_mask;
    }
    else if (CTC_FLAG_ISSET(flag, CTC_ACL_PBR_IPV6_KEY_FLAG_PRECEDENCE))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->dscp, 7);
        p_sys_key->u0.ip.dscp      = (p_ctc_key->dscp) << 3;
        p_sys_key->u0.ip.dscp_mask = (p_ctc_key->dscp_mask & 0x7) << 3;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_PBR_IPV6_KEY_FLAG_FLOW_LABEL))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->flow_label, 0xFFFFF);
        p_sys_key->flow_label      = p_ctc_key->flow_label;
        p_sys_key->flow_label_mask = p_ctc_key->flow_label_mask & 0xFFFFF;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_PBR_IPV6_KEY_FLAG_VRFID))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->vrfid, 0x3FF);
        p_sys_key->vrfid      = p_ctc_key->vrfid;
        p_sys_key->vrfid_mask = p_ctc_key->vrfid_mask;
    }

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_acl_map_ctc_mpls_2_ipv4(uint8 lchip, ctc_acl_key_t* p_mpls,
                            ctc_acl_key_t* p_ipv4)
{
    uint32 mpls_flag = 0;
    uint32 ipv4_flag = 0;
    uint32 ipv4_sub1_flag = 0;
    CTC_PTR_VALID_CHECK(p_mpls);
    CTC_PTR_VALID_CHECK(p_ipv4);
    mpls_flag = p_mpls->u.mpls_key.flag;
    ipv4_flag = p_ipv4->u.ipv4_key.flag;
    ipv4_sub1_flag = p_ipv4->u.ipv4_key.sub1_flag;

    if (CTC_FLAG_ISSET(mpls_flag, CTC_ACL_MPLS_KEY_FLAG_MPLS_LABEL3) || CTC_FLAG_ISSET(mpls_flag, CTC_ACL_MPLS_KEY_FLAG_CTAG_CFI))
    {
        return CTC_E_INVALID_PARAM;
    }

    p_ipv4->type = CTC_ACL_KEY_IPV4;
    p_ipv4->u.ipv4_key.key_size = CTC_ACL_KEY_SIZE_DOUBLE;

    p_ipv4->u.ipv4_key.l3_type = CTC_PARSER_L3_TYPE_MPLS;
    p_ipv4->u.ipv4_key.l3_type_mask = 0xF;
    sal_memcpy(p_ipv4->u.ipv4_key.mac_da, p_mpls->u.mpls_key.mac_da,sizeof(mac_addr_t));
    sal_memcpy(p_ipv4->u.ipv4_key.mac_da_mask, p_mpls->u.mpls_key.mac_da_mask,sizeof(mac_addr_t));
    sal_memcpy(p_ipv4->u.ipv4_key.mac_sa, p_mpls->u.mpls_key.mac_sa,sizeof(mac_addr_t));
    sal_memcpy(p_ipv4->u.ipv4_key.mac_sa_mask, p_mpls->u.mpls_key.mac_sa_mask,sizeof(mac_addr_t));
    p_ipv4->u.ipv4_key.cvlan = p_mpls->u.mpls_key.cvlan;
    p_ipv4->u.ipv4_key.cvlan_end = p_mpls->u.mpls_key.cvlan_end;
    p_ipv4->u.ipv4_key.cvlan_mask = p_mpls->u.mpls_key.cvlan_mask;
    p_ipv4->u.ipv4_key.svlan = p_mpls->u.mpls_key.svlan;
    p_ipv4->u.ipv4_key.svlan_end = p_mpls->u.mpls_key.svlan_end;
    p_ipv4->u.ipv4_key.svlan_mask = p_mpls->u.mpls_key.svlan_mask;
    p_ipv4->u.ipv4_key.ctag_cos = p_mpls->u.mpls_key.ctag_cos;
    p_ipv4->u.ipv4_key.ctag_cos_mask = p_mpls->u.mpls_key.ctag_cos_mask;
    p_ipv4->u.ipv4_key.vrange_grpid = p_mpls->u.mpls_key.vrange_grpid;
    p_ipv4->u.ipv4_key.stag_cos = p_mpls->u.mpls_key.stag_cos;
    p_ipv4->u.ipv4_key.stag_cos_mask = p_mpls->u.mpls_key.stag_cos_mask;
    p_ipv4->u.ipv4_key.ctag_cfi = p_mpls->u.mpls_key.ctag_cfi;
    p_ipv4->u.ipv4_key.stag_cfi = p_mpls->u.mpls_key.stag_cfi;
    p_ipv4->u.ipv4_key.routed_packet = p_mpls->u.mpls_key.routed_packet;
    p_ipv4->u.ipv4_key.mpls_label0= p_mpls->u.mpls_key.mpls_label0;
    p_ipv4->u.ipv4_key.mpls_label0_mask = p_mpls->u.mpls_key.mpls_label0_mask;
    p_ipv4->u.ipv4_key.mpls_label1 = p_mpls->u.mpls_key.mpls_label1;
    p_ipv4->u.ipv4_key.mpls_label1_mask = p_mpls->u.mpls_key.mpls_label1_mask;
    p_ipv4->u.ipv4_key.mpls_label2 = p_mpls->u.mpls_key.mpls_label2;
    p_ipv4->u.ipv4_key.mpls_label2_mask = p_mpls->u.mpls_key.mpls_label2_mask;
    p_ipv4->u.ipv4_key.mpls_label_num = p_mpls->u.mpls_key.mpls_label_num;
    p_ipv4->u.ipv4_key.mpls_label_num_mask = p_mpls->u.mpls_key.mpls_label_num_mask;

    CTC_MAX_VALUE_CHECK(p_mpls->u.mpls_key.mpls_label_num, ACL_MAX_LABEL_NUM);

    p_ipv4->u.ipv4_key.metadata = p_mpls->u.mpls_key.metadata;
    p_ipv4->u.ipv4_key.metadata_mask = p_mpls->u.mpls_key.metadata_mask;

    CTC_SET_FLAG(ipv4_flag, CTC_ACL_IPV4_KEY_FLAG_L3_TYPE);

    if (CTC_FLAG_ISSET(mpls_flag, CTC_ACL_MPLS_KEY_FLAG_MAC_DA))
    {
        CTC_SET_FLAG(ipv4_flag, CTC_ACL_IPV4_KEY_FLAG_MAC_DA);
    }

    if (CTC_FLAG_ISSET(mpls_flag, CTC_ACL_MPLS_KEY_FLAG_MAC_SA))
    {
        CTC_SET_FLAG(ipv4_flag, CTC_ACL_IPV4_KEY_FLAG_MAC_SA);
    }

    if (CTC_FLAG_ISSET(mpls_flag, CTC_ACL_MPLS_KEY_FLAG_CVLAN))
    {
        CTC_SET_FLAG(ipv4_flag, CTC_ACL_IPV4_KEY_FLAG_CVLAN);
    }

    if (CTC_FLAG_ISSET(mpls_flag, CTC_ACL_MPLS_KEY_FLAG_CTAG_COS))
    {
        CTC_SET_FLAG(ipv4_flag, CTC_ACL_IPV4_KEY_FLAG_CTAG_COS);
    }

    if (CTC_FLAG_ISSET(mpls_flag, CTC_ACL_MPLS_KEY_FLAG_SVLAN))
    {
        CTC_SET_FLAG(ipv4_flag, CTC_ACL_IPV4_KEY_FLAG_SVLAN);
    }

    if (CTC_FLAG_ISSET(mpls_flag, CTC_ACL_MPLS_KEY_FLAG_STAG_COS))
    {
        CTC_SET_FLAG(ipv4_flag, CTC_ACL_IPV4_KEY_FLAG_STAG_COS);
    }

    if (CTC_FLAG_ISSET(mpls_flag, CTC_ACL_MPLS_KEY_FLAG_MPLS_LABEL0))
    {
        CTC_SET_FLAG(ipv4_sub1_flag, CTC_ACL_IPV4_KEY_SUB1_FLAG_MPLS_LABEL0);
    }

    if (CTC_FLAG_ISSET(mpls_flag, CTC_ACL_MPLS_KEY_FLAG_MPLS_LABEL1))
    {
        CTC_SET_FLAG(ipv4_sub1_flag, CTC_ACL_IPV4_KEY_SUB1_FLAG_MPLS_LABEL1);
    }

    if (CTC_FLAG_ISSET(mpls_flag, CTC_ACL_MPLS_KEY_FLAG_MPLS_LABEL2))
    {
        CTC_SET_FLAG(ipv4_sub1_flag, CTC_ACL_IPV4_KEY_SUB1_FLAG_MPLS_LABEL2);
    }

    if (CTC_FLAG_ISSET(mpls_flag, CTC_ACL_MPLS_KEY_FLAG_MPLS_LABEL_NUM))
    {
        CTC_SET_FLAG(ipv4_sub1_flag, CTC_ACL_IPV4_KEY_SUB1_FLAG_MPLS_LABEL_NUM);
    }

    if (CTC_FLAG_ISSET(mpls_flag, CTC_ACL_MPLS_KEY_FLAG_ROUTED_PACKET))
    {
        CTC_SET_FLAG(ipv4_flag, CTC_ACL_IPV4_KEY_FLAG_ROUTED_PACKET);
    }

    if (CTC_FLAG_ISSET(mpls_flag, CTC_ACL_MPLS_KEY_FLAG_STAG_CFI))
    {
        CTC_SET_FLAG(ipv4_flag, CTC_ACL_IPV4_KEY_FLAG_STAG_CFI);
    }

    if (CTC_FLAG_ISSET(mpls_flag, CTC_ACL_MPLS_KEY_FLAG_METADATA))
    {
        CTC_SET_FLAG(ipv4_flag, CTC_ACL_IPV4_KEY_FLAG_METADATA);
    }

    p_ipv4->u.ipv4_key.flag = ipv4_flag;
    p_ipv4->u.ipv4_key.sub1_flag = ipv4_sub1_flag;

    return CTC_E_NONE;
}


/*
 * get sys key based on ctc key
 */
STATIC int32
_sys_goldengate_acl_map_key(uint8 lchip, ctc_acl_key_t* p_ctc_key,
                            sys_acl_key_t* p_sys_key, uint8 dir)
{
    ctc_acl_key_t  tmp_ctc_key = {0};

    CTC_PTR_VALID_CHECK(p_ctc_key);
    CTC_PTR_VALID_CHECK(p_sys_key);
    SYS_ACL_DBG_FUNC();

    p_sys_key->type = p_ctc_key->type;

    switch (p_ctc_key->type)
    {
    case CTC_ACL_KEY_MAC:
        /* merge key cannot add mac key */
        #if 0 /* for add default entry to discard */
        if (p_gg_acl_master[lchip]->acl_register.merge)
        {
            return CTC_E_INVALID_PARAM;
        }
        #endif
        CTC_ERROR_RETURN(
            _sys_goldengate_acl_map_tcam_key_port(lchip, &p_ctc_key->u.mac_key.port, &p_sys_key->u0.mac_key.port, FALSE));
        CTC_ERROR_RETURN(
            _sys_goldengate_acl_map_mac_key(lchip, &p_ctc_key->u.mac_key, &p_sys_key->u0.mac_key, dir));
        break;

    case CTC_ACL_KEY_MPLS:
        p_sys_key->type = CTC_ACL_KEY_IPV4;
        CTC_ERROR_RETURN(
            _sys_goldengate_acl_map_tcam_key_port(lchip, &p_ctc_key->u.mpls_key.port, &p_sys_key->u0.ipv4_key.port, FALSE));
        CTC_ERROR_RETURN(_sys_goldengate_acl_map_ctc_mpls_2_ipv4(lchip, p_ctc_key,&tmp_ctc_key));
        CTC_ERROR_RETURN(
            _sys_goldengate_acl_map_ipv4_key(lchip, &(tmp_ctc_key.u.ipv4_key), &p_sys_key->u0.ipv4_key, dir));
        break;


    case CTC_ACL_KEY_IPV4:
        CTC_ERROR_RETURN(
            _sys_goldengate_acl_map_tcam_key_port(lchip, &p_ctc_key->u.ipv4_key.port, &p_sys_key->u0.ipv4_key.port, FALSE));
        CTC_ERROR_RETURN(
            _sys_goldengate_acl_map_ipv4_key(lchip, &p_ctc_key->u.ipv4_key, &p_sys_key->u0.ipv4_key, dir));
        break;

    case CTC_ACL_KEY_IPV6:
        CTC_ERROR_RETURN(
            _sys_goldengate_acl_map_tcam_key_port(lchip, &p_ctc_key->u.ipv6_key.port, &p_sys_key->u0.ipv6_key.port, TRUE));
        CTC_ERROR_RETURN(
            _sys_goldengate_acl_map_ipv6_key(lchip, &p_ctc_key->u.ipv6_key, &p_sys_key->u0.ipv6_key, dir));
        break;

    case CTC_ACL_KEY_HASH_MAC:
        CTC_ERROR_RETURN(
            _sys_goldengate_acl_map_hash_mac_key(lchip, &p_ctc_key->u.hash_mac_key, &p_sys_key->u0.hash.u0.hash_mac_key, dir));
        break;

    case CTC_ACL_KEY_HASH_IPV4:
        CTC_ERROR_RETURN(
            _sys_goldengate_acl_map_hash_ipv4_key(lchip, &p_ctc_key->u.hash_ipv4_key, &p_sys_key->u0.hash.u0.hash_ipv4_key));
        break;

    case CTC_ACL_KEY_HASH_L2_L3:
        CTC_ERROR_RETURN(
            _sys_goldengate_acl_map_hash_l2_l3_key(lchip, &p_ctc_key->u.hash_l2_l3_key, &p_sys_key->u0.hash.u0.hash_l2_l3_key, dir));
        break;

    case CTC_ACL_KEY_HASH_MPLS:
        CTC_ERROR_RETURN(
            _sys_goldengate_acl_map_hash_mpls_key(lchip, &p_ctc_key->u.hash_mpls_key, &p_sys_key->u0.hash.u0.hash_mpls_key));
        break;

    case CTC_ACL_KEY_HASH_IPV6:
        CTC_ERROR_RETURN(
            _sys_goldengate_acl_map_hash_ipv6_key(lchip, &p_ctc_key->u.hash_ipv6_key, &p_sys_key->u0.hash.u0.hash_ipv6_key));
        break;

    case CTC_ACL_KEY_PBR_IPV4:
        CTC_ERROR_RETURN(
            _sys_goldengate_acl_map_tcam_key_port(lchip, &p_ctc_key->u.pbr_ipv4_key.port, &p_sys_key->u0.pbr_ipv4_key.port, FALSE));
        CTC_ERROR_RETURN(
            _sys_goldengate_acl_map_pbr_ipv4_key(lchip, &p_ctc_key->u.pbr_ipv4_key, &p_sys_key->u0.pbr_ipv4_key));
        break;

    case CTC_ACL_KEY_PBR_IPV6:
        CTC_ERROR_RETURN(
            _sys_goldengate_acl_map_tcam_key_port(lchip, &p_ctc_key->u.pbr_ipv6_key.port, &p_sys_key->u0.pbr_ipv6_key.port, TRUE));
        CTC_ERROR_RETURN(
            _sys_goldengate_acl_map_pbr_ipv6_key(lchip, &p_ctc_key->u.pbr_ipv6_key, &p_sys_key->u0.pbr_ipv6_key));
        break;

    default:
        return CTC_E_ACL_INVALID_KEY_TYPE;
    }

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_acl_check_vlan_group(uint8 lchip, ctc_acl_group_info_t* pinfo)
{
    uint32  cmd                  = 0;
    uint32  ingress_vlan_acl_num = 0;
    uint32  egress_vlan_acl_num  = 0;
    uint32  egress_per_port_sel_en = 0;

    if(CTC_INGRESS == pinfo->dir)
    {
        /* ipe */
        cmd = DRV_IOR(IpeAclLookupCtl_t, IpeAclLookupCtl_l3AclNum_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ingress_vlan_acl_num));
        if(3 == ingress_vlan_acl_num)
        {
            if(0 == pinfo->priority)
            {
                return CTC_E_FEATURE_NOT_SUPPORT;
            }
        }
        else if(2 == ingress_vlan_acl_num)
        {
            if((0 == pinfo->priority)||(1 == pinfo->priority))
            {
                return CTC_E_FEATURE_NOT_SUPPORT;
            }
        }
        else if(1 == ingress_vlan_acl_num)
        {
            if((0 == pinfo->priority)||(1 == pinfo->priority)||(2 == pinfo->priority))
            {
                return CTC_E_FEATURE_NOT_SUPPORT;
            }
        }
        else if(0 == ingress_vlan_acl_num)
        {
            return CTC_E_FEATURE_NOT_SUPPORT;
        }

    }
    else if(CTC_EGRESS == pinfo->dir)
    {
        /* epe */
        cmd = DRV_IOR(EpeAclQosCtl_t, EpeAclQosCtl_l3AclNum_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &egress_vlan_acl_num));
        cmd = DRV_IOR(EpeAclQosCtl_t, EpeAclQosCtl_perPortSelectEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &egress_per_port_sel_en));
        if(0 == egress_vlan_acl_num && (!egress_per_port_sel_en))
        {
            return CTC_E_FEATURE_NOT_SUPPORT;
        }
    }

    return CTC_E_NONE;
}

/*
 * check group info if it is valid.
 */
STATIC int32
_sys_goldengate_acl_check_group_info(uint8 lchip, ctc_acl_group_info_t* pinfo, uint8 type, uint8 is_create, uint8 status)
{
    uint8 idx;

    CTC_PTR_VALID_CHECK(pinfo);
    SYS_ACL_DBG_FUNC();

    if (is_create)
    {
      /*   if ((CTC_ACL_GROUP_TYPE_PORT_BITMAP == type) || (CTC_ACL_GROUP_TYPE_PORT_CLASS == type))*/
      /*   {   // port bitmap , port class will care lchip. others ignore. */
       /*      if (pinfo->lchip != lchip)*/
        /*     {*/
        /*         return CTC_E_INVALID_PARAM;*/
        /*     }*/
        /* }*/

        SYS_ACL_CHECK_GROUP_PRIO(pinfo->dir, pinfo->priority);
    }

    CTC_MAX_VALUE_CHECK(pinfo->dir, CTC_BOTH_DIRECTION - 1);

    switch (type)
    {
    case CTC_ACL_GROUP_TYPE_PORT_BITMAP:

        /* 1. port_bitmap only allow to use 128 bits */
        for (idx = 4; idx < CTC_PORT_BITMAP_IN_WORD; idx++)
        {
            if (pinfo->un.port_bitmap[idx])
            {
                return CTC_E_ACL_PORT_BITMAP_OVERFLOW;
            }
        }

        if (ACL_BITMAP_STATUS_64 == status)
        {
            if (((pinfo->un.port_bitmap[0] || pinfo->un.port_bitmap[1]) +
                 (pinfo->un.port_bitmap[2] || pinfo->un.port_bitmap[3])) > 1)
            {
                return CTC_E_ACL_PORT_BITMAP_OVERFLOW;
            }
        }
        else if (ACL_BITMAP_STATUS_16 == status)
        {
            if (((pinfo->un.port_bitmap[0] & 0xFFFF || 0) +
                 (pinfo->un.port_bitmap[1] & 0xFFFF || 0) +
                 (pinfo->un.port_bitmap[2] & 0xFFFF || 0) +
                 (pinfo->un.port_bitmap[3] & 0xFFFF || 0) +
                 (pinfo->un.port_bitmap[0] >> 16 || 0) +
                 (pinfo->un.port_bitmap[1] >> 16 || 0) +
                 (pinfo->un.port_bitmap[2] >> 16 || 0) +
                 (pinfo->un.port_bitmap[3] >> 16 || 0)) > 1)
            {
                return CTC_E_ACL_PORT_BITMAP_OVERFLOW;
            }
        }

        break;

    case CTC_ACL_GROUP_TYPE_GLOBAL:
    case CTC_ACL_GROUP_TYPE_NONE:
        break;

    case CTC_ACL_GROUP_TYPE_VLAN_CLASS:
        if(is_create)
        {
            CTC_ERROR_RETURN(_sys_goldengate_acl_check_vlan_group(lchip, pinfo));
        }
        SYS_ACL_VLAN_CLASS_ID_CHECK(pinfo->un.vlan_class_id);
        break;

    case CTC_ACL_GROUP_TYPE_PORT_CLASS:
        SYS_ACL_PORT_CLASS_ID_CHECK(pinfo->un.port_class_id);
        break;

    case CTC_ACL_GROUP_TYPE_SERVICE_ACL:
        SYS_ACL_SERVICE_ID_CHECK(pinfo->un.service_id);
        break;
    case CTC_ACL_GROUP_TYPE_HASH: /* check nothing*/
        break;

    case CTC_ACL_GROUP_TYPE_PBR_CLASS:
        CTC_ACL_PBR_CLASS_ID_CHECK(pinfo->un.pbr_class_id);
        break;

    case CTC_ACL_GROUP_TYPE_PORT:
        CTC_GLOBAL_PORT_CHECK(pinfo->un.gport);
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

/*
 * check if install group info is right, and if it is new.
 */
STATIC bool
_sys_goldengate_acl_is_group_info_new(uint8 lchip, sys_acl_group_info_t* sys,
                                      ctc_acl_group_info_t* ctc)
{
    uint8 equal = 0;
    switch (ctc->type)
    {
    case  CTC_ACL_GROUP_TYPE_PORT_BITMAP:
        equal = !sal_memcmp(&ctc->un.port_bitmap, &sys->un.port_bitmap, sizeof(ctc_port_bitmap_t));
        break;
    case  CTC_ACL_GROUP_TYPE_GLOBAL:
    case  CTC_ACL_GROUP_TYPE_NONE:
        equal = 1;
        break;
    case  CTC_ACL_GROUP_TYPE_HASH:
        break;
    case  CTC_ACL_GROUP_TYPE_VLAN_CLASS:
        equal = (ctc->un.vlan_class_id == sys->un.vlan_class_id);
        break;
    case  CTC_ACL_GROUP_TYPE_PORT_CLASS:
        equal = (ctc->un.port_class_id == sys->un.port_class_id);
        break;
    case  CTC_ACL_GROUP_TYPE_SERVICE_ACL:
        equal = (ctc->un.service_id == sys->un.service_id);
        break;
    case  CTC_ACL_GROUP_TYPE_PBR_CLASS:
        equal = (ctc->un.pbr_class_id == sys->un.pbr_class_id);
        break;
    case  CTC_ACL_GROUP_TYPE_PORT:
        equal = (ctc->un.gport == sys->un.gport);
        break;
    default:
        return CTC_E_INVALID_PARAM;
    }

    if (CTC_ACL_GROUP_TYPE_HASH != ctc->type) /* hash group always is not new */
    {
        if ((ctc->dir != sys->dir) || !equal)
        {
            return TRUE;
        }
    }

    return FALSE;
}

/*
 * get sys group info based on ctc group info
 */
STATIC int32
_sys_goldengate_acl_map_group_info(uint8 lchip, sys_acl_group_info_t* sys, ctc_acl_group_info_t* ctc, uint8 is_create)
{
    if (is_create) /* install group cannot modify: type, priority, direction*/
    {
        sys->type     = ctc->type;
        sys->block_id = ctc->priority;
        sys->dir      = ctc->dir; /* this is new for gg. becasue ingress egress don't share memory. */

    /*
        sys->un.gprot is uint16, ctc->type.un.gport is uint32
        if use function sal_memcpy to copy union, the value of sys->un.gport will be wrong in big-endian,
        but be true in little-endian
    */
        if(CTC_ACL_GROUP_TYPE_PORT == ctc->type)
        {
            sys->un.gport = ctc->un.gport;
        }
        else
        {
            sal_memcpy(&sys->un, &ctc->un, sizeof(ctc_port_bitmap_t));
        }
    }
    else
    {
        if(CTC_ACL_GROUP_TYPE_PORT == sys->type)
        {
            sys->un.gport = ctc->un.gport;
        }
        else
        {
            sal_memcpy(&sys->un, &ctc->un, sizeof(ctc_port_bitmap_t));
        }
    }

    return CTC_E_NONE;
}


/*
 * get ctc group info based on sys group info
 */
STATIC int32
_sys_goldengate_acl_unmap_group_info(uint8 lchip, ctc_acl_group_info_t* ctc, sys_acl_group_info_t* sys)
{
    ctc->dir      = sys->dir;
    ctc->type     = sys->type;
    ctc->priority = sys->block_id;
    ctc->lchip    = lchip;

    if(CTC_ACL_GROUP_TYPE_PORT == sys->type)
    {
        ctc->un.gport = sys->un.gport;
    }
    else
    {
        sal_memcpy(&ctc->un, &sys->un, sizeof(ctc_port_bitmap_t));
    }


    return CTC_E_NONE;
}

STATIC sys_acl_l4_t*
_sys_goldengate_get_l4(uint8 lchip, sys_acl_entry_t * pe)
{
    sys_acl_l4_t* l4 = NULL;
    switch (pe->key.type)
    {
    case CTC_ACL_KEY_IPV4:
        l4 = &pe->key.u0.ipv4_key.u0.ip.l4;
        break;

    case CTC_ACL_KEY_IPV6:
        l4 = &pe->key.u0.ipv6_key.u0.ip.l4;
        break;

    case CTC_ACL_KEY_PBR_IPV4:
        l4 = &pe->key.u0.pbr_ipv4_key.u0.ip.l4;
        break;

    case CTC_ACL_KEY_PBR_IPV6:
        l4 = &pe->key.u0.pbr_ipv6_key.u0.ip.l4;
        break;

    default:
        break;
    }
    return l4;
}

STATIC int32
_sys_goldengate_acl_init_pkt_len_range(uint8 lchip)
{
    uint32 cmd = 0;
    ParserLayer3LengthOpCtl_m len_ctl;

    sal_memset(&len_ctl, 0xFF, sizeof(ParserLayer3LengthOpCtl_m));

    cmd = DRV_IOW(ParserLayer3LengthOpCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &len_ctl));

    p_gg_acl_master[lchip]->pkt_len_ref_cnt = 0;

    return CTC_E_NONE;
}

/*
 * remove accessory property
 */
STATIC int32
_sys_goldengate_acl_remove_accessory_property(uint8 lchip, sys_acl_entry_t* pe, sys_acl_action_t* pa)
{
    sys_acl_l4_t * l4 = NULL;
    CTC_PTR_VALID_CHECK(pe);
    CTC_PTR_VALID_CHECK(pa);

    /* remove tcp flags and port range */
    l4 = _sys_goldengate_get_l4(lchip, pe);

    if (l4)
    {
        if (l4->flag_tcp_flags)
        {
            CTC_ERROR_RETURN(_sys_goldengate_acl_remove_tcp_flag(lchip, l4->tcp_flags_idx));
        }

        if (l4->flag_l4_src_port_range)
        {
            CTC_ERROR_RETURN(_sys_goldengate_acl_remove_l4_port_range(lchip, l4->l4_src_port_range_idx));
        }

        if (l4->flag_l4_dst_port_range)
        {
            CTC_ERROR_RETURN(_sys_goldengate_acl_remove_l4_port_range(lchip, l4->l4_dst_port_range_idx));
        }

        if (l4->flag_pkt_len_range)
        {
            (p_gg_acl_master[lchip]->pkt_len_ref_cnt)--;
            if (0 == p_gg_acl_master[lchip]->pkt_len_ref_cnt)
            {
                CTC_ERROR_RETURN(_sys_goldengate_acl_init_pkt_len_range(lchip));
            }
        }
    }

    return CTC_E_NONE;
}

#define  __ACL_BUILD_FREE_INDEX__
/* free index of HASH ad */
STATIC int32
_sys_goldengate_acl_free_hash_ad_index(uint8 lchip, uint16 ad_index)
{
    SYS_ACL_DBG_FUNC();

    CTC_ERROR_RETURN(sys_goldengate_ftm_free_table_offset(lchip, DsAcl_t, CTC_INGRESS, 1, (uint32) ad_index));

    return CTC_E_NONE;
}



/* build index of HASH ad */
STATIC int32
_sys_goldengate_acl_build_hash_ad_index(uint8 lchip, uint32* p_ad_index)
{
    uint32 value_32 = 0;

    SYS_ACL_DBG_FUNC();

    CTC_ERROR_RETURN(sys_goldengate_ftm_alloc_table_offset(lchip, DsAcl_t, CTC_INGRESS, 1, &value_32));
    *p_ad_index = value_32 & 0xFFFF;

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_acl_asic_hash_lkup(uint8 lchip, sys_acl_entry_t* pe, uint8* conflict, uint32* index)
{
    ds_t              key;
    drv_cpu_acc_in_t  in;
    drv_cpu_acc_out_t out;
    uint8             acc_type = DRV_CPU_ALLOC_ACC_FLOW_HASH;
    uint32            key_id;

    CTC_PTR_VALID_CHECK(pe);
    SYS_ACL_DBG_FUNC();

    sal_memset(&in, 0, sizeof(in));
    sal_memset(&out, 0, sizeof(out));
    sal_memset(&key, 0, sizeof(key));

    CTC_ERROR_RETURN(_sys_goldengate_acl_get_table_id(lchip, pe, &key_id, NULL));

    switch (pe->key.type)
    {
    case CTC_ACL_KEY_HASH_MAC:
        _sys_goldengate_acl_build_hash_mac_key(lchip, pe, (DsFlowL2HashKey_m *)key, 0);
        break;

    case CTC_ACL_KEY_HASH_IPV4:
        _sys_goldengate_acl_build_hash_ipv4_key(lchip, pe, (DsFlowL3Ipv4HashKey_m*)key, 0);
        break;

    case CTC_ACL_KEY_HASH_IPV6:
        _sys_goldengate_acl_build_hash_ipv6_key(lchip, pe, (DsFlowL3Ipv6HashKey_m*)key, 0);
        break;

    case CTC_ACL_KEY_HASH_MPLS:
        _sys_goldengate_acl_build_hash_mpls_key(lchip, pe, (DsFlowL3MplsHashKey_m*)key, 0);
        break;

    case CTC_ACL_KEY_HASH_L2_L3:
        _sys_goldengate_acl_build_hash_l2_l3_key(lchip, pe, (DsFlowL2L3HashKey_m*)key, 0);
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }


    /* get index of Key */
    in.data   = key;
    in.tbl_id = key_id;
    CTC_ERROR_RETURN(drv_goldengate_cpu_acc(lchip, acc_type, &in, &out));
    *conflict = out.conflict;
    *index    = out.key_index;
    return CTC_E_NONE;
}






#define  __ACL_ESSENTIAL_API__



/*----reserv 64 patterns-----*/
STATIC int32
_sys_goldengate_acl_vlan_edit_index_build(uint8 lchip, uint16* p_profile_id)
{
    sys_goldengate_opf_t opf;
    uint32               value_32 = 0;

    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
    opf.pool_index = 0;
    opf.pool_type  = OPF_ACL_VLAN_ACTION;

    CTC_ERROR_RETURN(sys_goldengate_opf_alloc_offset(lchip, &opf, 1, &value_32));
    *p_profile_id = value_32 & 0xFFFF;

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_acl_vlan_edit_index_free(uint8 lchip, uint16 profile_id)
{
    sys_goldengate_opf_t opf;

    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
    opf.pool_index = 0;
    opf.pool_type  = OPF_ACL_VLAN_ACTION;

    CTC_ERROR_RETURN(sys_goldengate_opf_free_offset(lchip, &opf, 1, (uint32) profile_id));

    return CTC_E_NONE;
}



/* pv_out can be NULL.*/
STATIC int32
_sys_goldengate_acl_hash_vlan_edit_spool_add(uint8 lchip, sys_acl_entry_t* pe,
                                             sys_acl_vlan_edit_t* pv,
                                             sys_acl_vlan_edit_t** pv_out)
{
    sys_acl_vlan_edit_t* pv_new = NULL;
    sys_acl_vlan_edit_t* pv_get = NULL; /* get from share pool*/
    int32              ret      = 0;


    /* malloc sys action */
    MALLOC_ZERO(MEM_ACL_MODULE, pv_new, sizeof(sys_acl_vlan_edit_t));
    if (!pv_new)
    {
        return CTC_E_NO_MEMORY;
    }
    sal_memcpy(pv_new, pv, sizeof(sys_acl_vlan_edit_t));
    pv_new->lchip = lchip;

    ret = ctc_spool_add(p_gg_acl_master[lchip]->vlan_edit_spool, pv_new, NULL, &pv_get);

    if (ret < 0)
    {
        ret = CTC_E_SPOOL_ADD_UPDATE_FAILED;
        goto error_0;
    }
    else if (CTC_SPOOL_E_OPERATE_MEMORY == ret)
    {
        CTC_ERROR_GOTO(_sys_goldengate_acl_vlan_edit_index_build(lchip, &pv_get->profile_id), ret, error_1);
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

 error_1:
    ctc_spool_remove(p_gg_acl_master[lchip]->vlan_edit_spool, pv_new, NULL);
 error_0:
    mem_free(pv_new);
    return ret;
}

STATIC int32
_sys_goldengate_acl_hash_vlan_edit_spool_remove(uint8 lchip, sys_acl_vlan_edit_t* pv)

{
    uint16             index_free = 0;
    int32              ret        = 0;
    sys_acl_vlan_edit_t* pv_lkup;

    ret = ctc_spool_remove(p_gg_acl_master[lchip]->vlan_edit_spool, pv, &pv_lkup);

    if (ret < 0)
    {
        return CTC_E_SPOOL_REMOVE_FAILED;
    }
    else if (ret == CTC_SPOOL_E_OPERATE_MEMORY)
    {
        index_free = pv_lkup->profile_id;
        _sys_goldengate_acl_vlan_edit_index_free(lchip, index_free);
        mem_free(pv_lkup);
    }

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_acl_hash_ad_spool_remove(uint8 lchip, sys_acl_action_t* pa)
{
    uint16          index_free = 0;

    int32           ret = 0;
    sys_acl_action_t* pa_lkup;

    ret = ctc_spool_remove(p_gg_acl_master[lchip]->ad_spool, pa, &pa_lkup);

    if (ret < 0)
    {
        return CTC_E_SPOOL_REMOVE_FAILED;
    }
    else if (ret == CTC_SPOOL_E_OPERATE_MEMORY)
    {
        index_free = pa_lkup->chip.ad_index;

        CTC_ERROR_RETURN(_sys_goldengate_acl_free_hash_ad_index(lchip, index_free));

        SYS_ACL_DBG_INFO("  %% INFO: acl action removed: index = %d\n", index_free);
    }

    if (ret == CTC_SPOOL_E_OPERATE_MEMORY)
    {
        mem_free(pa_lkup);
    }

    return CTC_E_NONE;
}



STATIC int32
_sys_goldengate_acl_hash_ad_spool_add(uint8 lchip, sys_acl_entry_t* pe,
                                      sys_acl_action_t* pa,
                                      sys_acl_action_t** pa_out)
{
    sys_acl_action_t* pa_new = NULL;
    sys_acl_action_t* pa_get = NULL; /* get from share pool*/
    int32           ret      = 0;

    /* malloc sys action */
    MALLOC_ZERO(MEM_ACL_MODULE, pa_new, sizeof(sys_acl_action_t));
    if (!pa_new)
    {
        return CTC_E_NO_MEMORY;
    }
    sal_memcpy(pa_new, pa, sizeof(sys_acl_action_t));
    pa_new->lchip = lchip;

    /* -- set action ptr -- */
    ret = ctc_spool_add(p_gg_acl_master[lchip]->ad_spool, pa_new, NULL, &pa_get);

    if (ret < 0)
    {
        ret = CTC_E_SPOOL_ADD_UPDATE_FAILED;
        goto error_0;
    }
    else if (ret == CTC_SPOOL_E_OPERATE_MEMORY)
    {
        CTC_ERROR_GOTO(_sys_goldengate_acl_build_hash_ad_index(lchip, &pa_get->chip.ad_index), ret, error_1);
    }

    if (CTC_SPOOL_E_OPERATE_REFCNT == ret)
    {
        mem_free(pa_new);
    }

    *pa_out = pa_get;

    return CTC_E_NONE;

 error_1:
    ctc_spool_remove(p_gg_acl_master[lchip]->ad_spool, pa_get, NULL);
 error_0:
    mem_free(pa_new);
    return ret;
}



STATIC int32
_sys_goldengate_acl_alloc_hash_entry(uint8 lchip, sys_acl_entry_t* pe,
                                     sys_acl_entry_t** pe_out)
{
    uint32          block_index = 0;
    sys_acl_entry_t * pe_new    = NULL;
    uint8           conflict    = 0;
    uint32          index       = 0;


    /* cacluate asic hash index */
    /* ASIC hash lookup, check if hash conflict */
    CTC_ERROR_RETURN(_sys_goldengate_acl_asic_hash_lkup(lchip, pe, &conflict, &index));

    if (conflict)
    {
        return CTC_E_ACL_HASH_CONFLICT;
    }
    else
    {
        block_index = index;
    }

    /* malloc sys entry */
    MALLOC_ZERO(MEM_ACL_MODULE, pe_new, p_gg_acl_master[lchip]->entry_size[pe->key.type]);
    if (!pe_new)
    {
        return CTC_E_NO_MEMORY;
    }
    sal_memcpy(pe_new, pe, p_gg_acl_master[lchip]->entry_size[pe->key.type]);
    pe_new->lchip = lchip;
    pe_new->fpae.lchip = lchip;

    /* set block index */
    pe_new->fpae.offset_a = block_index;

    /* add to hash */
    ctc_hash_insert(p_gg_acl_master[lchip]->hash_ent_key, pe_new);

    *pe_out = pe_new;
    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_acl_alloc_tcam_entry(uint8 lchip, sys_acl_group_t* pg,
                                     sys_acl_entry_t* pe,
                                     sys_acl_action_t* pa,
                                     sys_acl_entry_t** pe_out,
                                     sys_acl_action_t** pa_out)
{
    uint8           fpa_size = 0;
    uint8           block_id;
    uint32          pbr_index;
    int32           ret;
    sys_acl_entry_t * pe_new    = NULL;
    sys_acl_action_t* pa_new    = NULL;
    sys_acl_block_t * pb        = NULL;
    uint32          block_index = 0;


    /* malloc sys entry */
    MALLOC_ZERO(MEM_ACL_MODULE, pe_new, p_gg_acl_master[lchip]->entry_size[pe->key.type]);
    if (!pe_new)
    {
        ret = CTC_E_NO_MEMORY;
        goto error_0;
    }
    sal_memcpy(pe_new, pe, p_gg_acl_master[lchip]->entry_size[pe->key.type]);
    pe_new->lchip = lchip;
    pe_new->fpae.lchip =lchip;

    /* malloc sys action */
    MALLOC_ZERO(MEM_ACL_MODULE, pa_new, sizeof(sys_acl_action_t));
    if (!pa_new)
    {
        ret = CTC_E_NO_MEMORY;
        goto error_1;
    }
    sal_memcpy(pa_new, pa, sizeof(sys_acl_action_t));
    pa_new->lchip = lchip;

    /* get pbr index */
    if ((pe->key.type == CTC_ACL_KEY_PBR_IPV4) || (pe->key.type == CTC_ACL_KEY_PBR_IPV6))
    {
        CTC_ERROR_GOTO(sys_goldengate_ftm_alloc_table_offset(lchip, DsIpDa_t, 0, 1, &pbr_index), ret, error_2);
        pa_new->chip.ad_index = pbr_index;
    }

    SYS_ACL_GET_BLOCK_ID(pe, block_id);
    pb = &p_gg_acl_master[lchip]->block[block_id];
    fpa_size = _sys_goldengate_acl_get_fpa_size(lchip, pe, NULL);

    CTC_ERROR_GOTO(fpa_goldengate_alloc_offset(p_gg_acl_master[lchip]->fpa, &pb->fpab, fpa_size,
                                    pe->fpae.priority, &block_index), ret, error_3);

    /* add to block */
    pb->fpab.entries[block_index] = &pe_new->fpae;

    /* set block index */
    pe_new->fpae.offset_a = block_index;

    fpa_goldengate_reorder(p_gg_acl_master[lchip]->fpa, &pb->fpab, fpa_size);

    *pe_out = pe_new;
    *pa_out = pa_new;

    return CTC_E_NONE;


error_3:
    if ((pe->key.type == CTC_ACL_KEY_PBR_IPV4) || (pe->key.type == CTC_ACL_KEY_PBR_IPV6))
    {
        sys_goldengate_ftm_free_table_offset(lchip, DsIpDa_t, 0, 1, pbr_index);
    }
error_2:
    mem_free(pa_new);
error_1:
    mem_free(pe_new);
error_0:
    return ret;
}


STATIC int32
_sys_goldengate_acl_update_key_count(uint8 lchip, sys_acl_entry_t* pe, uint8 is_add)
{
    if (is_add)
    {
        if (ACL_ENTRY_IS_TCAM(pe->key.type))
        {
            if ((CTC_ACL_KEY_MAC == pe->key.type) && (0 == p_gg_acl_master[lchip]->ipv4_160bit_mode_en))
            {
                p_gg_acl_master[lchip]->key_count[ACL_ALLOC_COUNT_TCAM_160]++;
            }
            else if (CTC_ACL_KEY_MAC == pe->key.type)
            {
                p_gg_acl_master[lchip]->key_count[ACL_ALLOC_COUNT_TCAM_320]++;
            }
            else if ((pe->key.type == CTC_ACL_KEY_IPV4) &&
                     (CTC_ACL_KEY_SIZE_DOUBLE == pe->key.u0.ipv4_key.key_size))
            {
                p_gg_acl_master[lchip]->key_count[ACL_ALLOC_COUNT_TCAM_320]++;
            }
            else if (pe->key.type == CTC_ACL_KEY_IPV4)
            {
                p_gg_acl_master[lchip]->key_count[ACL_ALLOC_COUNT_TCAM_160]++;
            }
            else if (pe->key.type == CTC_ACL_KEY_IPV6)
            {
                p_gg_acl_master[lchip]->key_count[ACL_ALLOC_COUNT_TCAM_640]++;
            }
            else if (pe->key.type == CTC_ACL_KEY_PBR_IPV4)
            {
                p_gg_acl_master[lchip]->key_count[ACL_ALLOC_COUNT_PBR_160]++;
            }
            else if (pe->key.type == CTC_ACL_KEY_PBR_IPV6)
            {
                p_gg_acl_master[lchip]->key_count[ACL_ALLOC_COUNT_PBR_320]++;
            }
        }
        else
        {
            switch (pe->key.type)
            {
                case CTC_ACL_KEY_HASH_MAC:
                case CTC_ACL_KEY_HASH_IPV4:
                case CTC_ACL_KEY_HASH_MPLS:
                    p_gg_acl_master[lchip]->key_count[ACL_ALLOC_COUNT_HASH] = p_gg_acl_master[lchip]->key_count[ACL_ALLOC_COUNT_HASH] + 1;
                    break;

                case CTC_ACL_KEY_HASH_L2_L3:
                case CTC_ACL_KEY_HASH_IPV6:
                    p_gg_acl_master[lchip]->key_count[ACL_ALLOC_COUNT_HASH] = p_gg_acl_master[lchip]->key_count[ACL_ALLOC_COUNT_HASH] + 2;
                    break;

                default:
                    break;
            }
        }
    }
    else
    {
        if (ACL_ENTRY_IS_TCAM(pe->key.type))
        {
            if ((CTC_ACL_KEY_MAC == pe->key.type) && (0 == p_gg_acl_master[lchip]->ipv4_160bit_mode_en))
            {
                p_gg_acl_master[lchip]->key_count[ACL_ALLOC_COUNT_TCAM_160]--;
            }
            else if (CTC_ACL_KEY_MAC == pe->key.type)
            {
                p_gg_acl_master[lchip]->key_count[ACL_ALLOC_COUNT_TCAM_320]--;
            }
            else if ((pe->key.type == CTC_ACL_KEY_IPV4) &&
                     (CTC_ACL_KEY_SIZE_DOUBLE == pe->key.u0.ipv4_key.key_size))
            {
                p_gg_acl_master[lchip]->key_count[ACL_ALLOC_COUNT_TCAM_320]--;
            }
            else if (pe->key.type == CTC_ACL_KEY_IPV4)
            {
                p_gg_acl_master[lchip]->key_count[ACL_ALLOC_COUNT_TCAM_160]--;
            }
            else if (pe->key.type == CTC_ACL_KEY_IPV6)
            {
                p_gg_acl_master[lchip]->key_count[ACL_ALLOC_COUNT_TCAM_640]--;
            }
            else if (pe->key.type == CTC_ACL_KEY_PBR_IPV4)
            {
                p_gg_acl_master[lchip]->key_count[ACL_ALLOC_COUNT_PBR_160]--;
            }
            else if (pe->key.type == CTC_ACL_KEY_PBR_IPV6)
            {
                p_gg_acl_master[lchip]->key_count[ACL_ALLOC_COUNT_PBR_320]--;
            }
        }
        else
        {
            switch (pe->key.type)
            {
                case CTC_ACL_KEY_HASH_MAC:
                case CTC_ACL_KEY_HASH_IPV4:
                case CTC_ACL_KEY_HASH_MPLS:
                    p_gg_acl_master[lchip]->key_count[ACL_ALLOC_COUNT_HASH] = p_gg_acl_master[lchip]->key_count[ACL_ALLOC_COUNT_HASH] - 1;
                    break;

                case CTC_ACL_KEY_HASH_L2_L3:
                case CTC_ACL_KEY_HASH_IPV6:
                    p_gg_acl_master[lchip]->key_count[ACL_ALLOC_COUNT_HASH] = p_gg_acl_master[lchip]->key_count[ACL_ALLOC_COUNT_HASH] - 2;
                    break;

                default:
                    break;
            }
        }

    }

    return CTC_E_NONE;
}

/*
 * pe, pg, pb are lookup result.
 */
STATIC int32
_sys_goldengate_acl_remove_tcam_entry(uint8 lchip, sys_acl_entry_t* pe_lkup,
                                      sys_acl_group_t* pg_lkup,
                                      sys_acl_block_t* pb_lkup)
{
    if (pe_lkup->action->vlan_edit_valid)
    {
        CTC_ERROR_RETURN(_sys_goldengate_acl_hash_vlan_edit_spool_remove(lchip, pe_lkup->action->vlan_edit));
    }

    /* remove from group */
    ctc_slist_delete_node(pg_lkup->entry_list, &(pe_lkup->head));

    /* remove from hash */
    ctc_hash_remove(p_gg_acl_master[lchip]->entry, pe_lkup);

    /* remove accessory property */
    _sys_goldengate_acl_remove_accessory_property(lchip, pe_lkup, pe_lkup->action);

    /* free index */
    fpa_goldengate_free_offset(&pb_lkup->fpab, pe_lkup->fpae.offset_a);

    if ((pe_lkup->key.type == CTC_ACL_KEY_PBR_IPV4) || (pe_lkup->key.type == CTC_ACL_KEY_PBR_IPV6))
    {
        sys_goldengate_ftm_free_table_offset(lchip, DsIpDa_t, 0, 1, (uint32)pe_lkup->action->chip.ad_index);
    }

    _sys_goldengate_acl_update_key_count(lchip, pe_lkup, 0);

    /* free action */
    mem_free(pe_lkup->action);

    /* memory free */
    mem_free(pe_lkup);

    (pg_lkup->entry_count)--;

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_acl_remove_hash_entry(uint8 lchip, sys_acl_entry_t* pe_lkup,
                                      sys_acl_group_t* pg_lkup,
                                      sys_acl_block_t* pb_lkup)
{
    uint32            key_id = 0;
    uint32            index = 0;
    drv_cpu_acc_in_t  in;

    if (pe_lkup->action->vlan_edit_valid)
    {
        CTC_ERROR_RETURN(_sys_goldengate_acl_hash_vlan_edit_spool_remove(lchip, pe_lkup->action->vlan_edit));
    }

    /* remove from group */
    ctc_slist_delete_node(pg_lkup->entry_list, &(pe_lkup->head));

    /* remove from hash */
    ctc_hash_remove(p_gg_acl_master[lchip]->entry, pe_lkup);

    /* remove from hash by key */
    ctc_hash_remove(p_gg_acl_master[lchip]->hash_ent_key, pe_lkup);

    /* remove accessory property */
    CTC_ERROR_RETURN(_sys_goldengate_acl_remove_accessory_property(lchip, pe_lkup, pe_lkup->action));

    /* remove ad share pool( mem_free ad, free ad_index) */
    CTC_ERROR_RETURN(_sys_goldengate_acl_hash_ad_spool_remove(lchip, pe_lkup->action));

    _sys_goldengate_acl_update_key_count(lchip, pe_lkup, 0);

    /* clear hw valid bit */
    index = pe_lkup->fpae.offset_a;
    _sys_goldengate_acl_get_table_id(lchip, pe_lkup, &key_id, NULL);

    sal_memset(&in, 0, sizeof(in));
    in.tbl_id    = key_id;
    in.key_index = index;
    CTC_ERROR_RETURN(drv_goldengate_cpu_acc(lchip, DRV_CPU_DEL_ACC_FLOW_HASH_BY_IDX, &in, NULL));

    /* memory free */
    mem_free(pe_lkup);

    (pg_lkup->entry_count)--;

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_acl_add_tcam_entry(uint8 lchip, sys_acl_group_t* pg,
                                   sys_acl_entry_t* pe,
                                   sys_acl_action_t* pa,
                                   sys_acl_vlan_edit_t* pv)
{
    int32              ret      = 0;
    sys_acl_entry_t    * pe_get = NULL;
    sys_acl_action_t   * pa_get = NULL;    /* get from share pool*/
    sys_acl_vlan_edit_t* pv_get = NULL;    /* get from share pool*/

    /* 1. vlan edit. error just return.*/
    if (CTC_FLAG_ISSET(pa->flag, CTC_ACL_ACTION_FLAG_VLAN_EDIT))
    {
        CTC_ERROR_GOTO(_sys_goldengate_acl_hash_vlan_edit_spool_add(lchip, pe, pv, &pv_get), ret, error_0);

        pa->vlan_edit       = pv_get;
        pa->vlan_edit_valid = 1;
        pa->chip.profile_id = pv_get->profile_id;
    }

    /* 2. key and action */
    CTC_ERROR_GOTO(_sys_goldengate_acl_alloc_tcam_entry(lchip, pg, pe, pa, &pe_get, &pa_get), ret, error_1);

    /* 3. connect key, action */
    pe_get->action = pa_get;

    /* add to hash */
    ctc_hash_insert(p_gg_acl_master[lchip]->entry, pe_get);

    /* add to group */
    ctc_slist_add_head(pg->entry_list, &(pe_get->head));

    /* mark flag */
    pe_get->fpae.flag = FPA_ENTRY_FLAG_UNINSTALLED;

    (pg->entry_count)++;

    _sys_goldengate_acl_update_key_count(lchip, pe_get, 1);

    return CTC_E_NONE;
 error_1:
    if (pv_get)
    {
        _sys_goldengate_acl_hash_vlan_edit_spool_remove(lchip, pv_get);
    }
 error_0:
    return ret;
}

STATIC int32
_sys_goldengate_acl_add_hash_entry(uint8 lchip, sys_acl_group_t* pg,
                                   sys_acl_entry_t* pe,
                                   sys_acl_action_t* pa,
                                   sys_acl_vlan_edit_t* pv)
{
    int32              ret      = 0;
    uint32            key_id = 0;
    uint32            act_id = 0;
    uint32               key_index = 0;
    sys_acl_entry_t    * pe_get = NULL;
    sys_acl_action_t   * pa_get = NULL; /* get from share pool*/
    sys_acl_vlan_edit_t* pv_get = NULL; /* get from share pool*/
    ds_t                 ds_hash_key;
    drv_cpu_acc_in_t     in;

    /* 1. vlan edit.*/
    if (CTC_FLAG_ISSET(pa->flag, CTC_ACL_ACTION_FLAG_VLAN_EDIT))
    {
        CTC_ERROR_GOTO(_sys_goldengate_acl_hash_vlan_edit_spool_add(lchip, pe, pv, &pv_get), ret, error_0);

        pa->vlan_edit       = pv_get;
        pa->vlan_edit_valid = 1;
        pa->chip.profile_id = pv_get->profile_id;
    }

    /* 2. action */
    CTC_ERROR_GOTO(_sys_goldengate_acl_hash_ad_spool_add(lchip, pe, pa, &pa_get), ret, error_1);

    /* 3. key */
    CTC_ERROR_GOTO(_sys_goldengate_acl_alloc_hash_entry(lchip, pe, &pe_get), ret, error_2);
    pe_get->action = pa_get;

    /* -- set hw valid bit  -- */
    sal_memset(&in, 0, sizeof(in));
    sal_memset(&ds_hash_key, 0, sizeof(ds_hash_key));

    switch (pe_get->key.type)
    {
        case CTC_ACL_KEY_HASH_MAC:
            SetDsFlowL2HashKey(V, hashKeyType_f, ds_hash_key, FLOWHASHTYPE_L2);
            break;

        case CTC_ACL_KEY_HASH_IPV4:
            SetDsFlowL3Ipv4HashKey(V, hashKeyType_f, ds_hash_key, FLOWHASHTYPE_L3IPV4);
            break;

        case CTC_ACL_KEY_HASH_IPV6:
            SetDsFlowL3Ipv6HashKey(V, hashKeyType0_f, ds_hash_key, FLOWHASHTYPE_L3IPV6);
            SetDsFlowL3Ipv6HashKey(V, hashKeyType1_f, ds_hash_key, FLOWHASHTYPE_L3IPV6);
            break;

        case CTC_ACL_KEY_HASH_MPLS:
            SetDsFlowL3MplsHashKey(V, hashKeyType_f, ds_hash_key, FLOWHASHTYPE_L3MPLS);
            break;

        case CTC_ACL_KEY_HASH_L2_L3:
            SetDsFlowL2L3HashKey(V, hashKeyType0_f, ds_hash_key, FLOWHASHTYPE_L2L3);
            SetDsFlowL2L3HashKey(V, hashKeyType1_f, ds_hash_key, FLOWHASHTYPE_L2L3);
            break;

        default:
            return CTC_E_INVALID_PARAM;
    }

    key_index = pe_get->fpae.offset_a;
    _sys_goldengate_acl_get_table_id(lchip, pe_get, &key_id, &act_id);

    in.tbl_id    = key_id;
    in.data      = ds_hash_key;
    in.key_index = key_index;

    CTC_ERROR_RETURN(drv_goldengate_cpu_acc(lchip, DRV_CPU_ADD_ACC_FLOW_HASH_BY_IDX, &in, NULL));

    /* add to hash */
    ctc_hash_insert(p_gg_acl_master[lchip]->entry, pe_get);

    /* add to group */
    ctc_slist_add_head(pg->entry_list, &(pe_get->head));

    /* mark flag */
    pe_get->fpae.flag = FPA_ENTRY_FLAG_UNINSTALLED;

    (pg->entry_count)++;

    _sys_goldengate_acl_update_key_count(lchip, pe_get, 1);

    return CTC_E_NONE;

 error_2:
    _sys_goldengate_acl_hash_ad_spool_remove(lchip, pa_get);
 error_1:
    if (pv_get)
    {
        _sys_goldengate_acl_hash_vlan_edit_spool_remove(lchip, pv_get);
    }
 error_0:
    return ret;
}


int32
_sys_goldengate_acl_create_group(uint8 lchip, uint32 group_id, ctc_acl_group_info_t* group_info)
{
    sys_acl_group_t* pg_new = NULL;
    int32          ret;
    struct ctc_slist_s* pslist = NULL;


    /* malloc an empty group */

    MALLOC_ZERO(MEM_ACL_MODULE, pg_new, sizeof(sys_acl_group_t));
    if (!pg_new)
    {
        return CTC_E_NO_MEMORY;
    }
    pg_new->lchip = lchip;
    if (!SYS_ACL_IS_RSV_GROUP(group_id)) /*tcam group*/
    {
        if (!group_info)
        {
            ret = CTC_E_INVALID_PARAM;
            goto cleanup;
        }
        CTC_ERROR_GOTO(_sys_goldengate_acl_check_group_info(lchip, group_info, group_info->type, 1, ACL_BITMAP_STATUS_64), ret, cleanup);
        CTC_ERROR_GOTO(_sys_goldengate_acl_map_group_info(lchip, &pg_new->group_info, group_info, 1), ret, cleanup);
    }

    pg_new->group_id   = group_id;
    pslist = ctc_slist_new();
    if(pslist)
    {
        pg_new->entry_list = pslist;
    }

    ctc_hash_insert(p_gg_acl_master[lchip]->group, pg_new);

    return CTC_E_NONE;
 cleanup:
    mem_free(pg_new);
    return ret;
}


#define __ACL_DUMP_INFO__

/*
 * show acl action
 */
STATIC int32
_sys_goldengate_acl_show_action(uint8 lchip, sys_acl_action_t* pa)
{
    char* tag_op[4]   = { "None", "Replace", "Add", "Delete" };
    char* field_op[3] = { "None", "Alternative", "New" };
    CTC_PTR_VALID_CHECK(pa);

    if (CTC_FLAG_ISSET(pa->flag, CTC_ACL_ACTION_FLAG_DISCARD))
    {
        SYS_ACL_DBG_DUMP("  =================== deny ======================");
    }
    else
    {
        SYS_ACL_DBG_DUMP("  =================== permit ====================");
    }

    /* next line */
    SYS_ACL_DBG_DUMP("\n");

    if (CTC_FLAG_ISSET(pa->flag, CTC_ACL_ACTION_FLAG_DENY_BRIDGE))
    {
        SYS_ACL_DBG_DUMP("  deny-bridge,");
    }

    if (CTC_FLAG_ISSET(pa->flag, CTC_ACL_ACTION_FLAG_DENY_LEARNING))
    {
        SYS_ACL_DBG_DUMP("  deny-learning,");
    }

    if (CTC_FLAG_ISSET(pa->flag, CTC_ACL_ACTION_FLAG_DENY_ROUTE))
    {
        SYS_ACL_DBG_DUMP("  deny-route,");
    }

    if (CTC_FLAG_ISSET(pa->flag, CTC_ACL_ACTION_FLAG_DENY_BRIDGE) +
        CTC_FLAG_ISSET(pa->flag, CTC_ACL_ACTION_FLAG_DENY_LEARNING) +
        CTC_FLAG_ISSET(pa->flag, CTC_ACL_ACTION_FLAG_DENY_ROUTE))
    {
        SYS_ACL_DBG_DUMP("\n");
    }

    if (CTC_FLAG_ISSET(pa->flag, CTC_ACL_ACTION_FLAG_STATS))
    {
        SYS_ACL_DBG_DUMP("  stats,");
    }

    if (CTC_FLAG_ISSET(pa->flag, CTC_ACL_ACTION_FLAG_COPY_TO_CPU))
    {
        SYS_ACL_DBG_DUMP("  copy-to-cpu,");
    }

    if (CTC_FLAG_ISSET(pa->flag, CTC_ACL_ACTION_FLAG_STATS) +
        CTC_FLAG_ISSET(pa->flag, CTC_ACL_ACTION_FLAG_COPY_TO_CPU) +
        CTC_FLAG_ISSET(pa->flag, CTC_ACL_ACTION_FLAG_AGING) +
        CTC_FLAG_ISSET(pa->flag, CTC_ACL_ACTION_FLAG_COPY_TO_CPU_WITH_TIMESTAMP))
    {
        SYS_ACL_DBG_DUMP("\n");
    }

    if (CTC_FLAG_ISSET(pa->flag, CTC_ACL_ACTION_FLAG_TRUST))
    {
        uint32 trust;
        sys_goldengate_common_unmap_qos_policy(lchip, pa->trust, &trust);
        SYS_ACL_DBG_DUMP("  trust %u,", trust);
    }

    if (CTC_FLAG_ISSET(pa->flag, CTC_ACL_ACTION_FLAG_PRIORITY_AND_COLOR))
    {
        SYS_ACL_DBG_DUMP("  priority %u color %u,", pa->priority, pa->color);
    }

    if (CTC_FLAG_ISSET(pa->flag, CTC_ACL_ACTION_FLAG_DSCP))
    {
        SYS_ACL_DBG_DUMP("  dscp %u,", pa->dscp);
    }

    if (CTC_FLAG_ISSET(pa->flag, CTC_ACL_ACTION_FLAG_TRUST) +
        CTC_FLAG_ISSET(pa->flag, CTC_ACL_ACTION_FLAG_PRIORITY_AND_COLOR) +
        CTC_FLAG_ISSET(pa->flag, CTC_ACL_ACTION_FLAG_DSCP))
    {
        SYS_ACL_DBG_DUMP("\n");
    }

    if (CTC_FLAG_ISSET(pa->flag, CTC_ACL_ACTION_FLAG_RANDOM_LOG))
    {
        SYS_ACL_DBG_DUMP("  random-log id %u  log-percent %u,", pa->acl_log_id, pa->random_threshold_shift);
    }

    if (CTC_FLAG_ISSET(pa->flag, CTC_ACL_ACTION_FLAG_REDIRECT))
    {
        SYS_ACL_DBG_DUMP("  redirect nhid %u,", pa->nh_id);
    }

    if (CTC_FLAG_ISSET(pa->flag, CTC_ACL_ACTION_FLAG_QOS_DOMAIN))
    {
        SYS_ACL_DBG_DUMP("  qos-domain :%u,", pa->qos_domain);
    }

    if (CTC_FLAG_ISSET(pa->flag, CTC_ACL_ACTION_FLAG_RANDOM_LOG) +
        CTC_FLAG_ISSET(pa->flag, CTC_ACL_ACTION_FLAG_REDIRECT) +
        CTC_FLAG_ISSET(pa->flag, CTC_ACL_ACTION_FLAG_QOS_DOMAIN))
    {
        SYS_ACL_DBG_DUMP("\n");
    }

    if (CTC_FLAG_ISSET(pa->flag, CTC_ACL_ACTION_FLAG_MACRO_FLOW_POLICER))
    {
        SYS_ACL_DBG_DUMP("  macro policer-id %u,", pa->macro_policer_id);
    }

    if (CTC_FLAG_ISSET(pa->flag, CTC_ACL_ACTION_FLAG_MICRO_FLOW_POLICER))
    {
        SYS_ACL_DBG_DUMP("  macro policer-id %u,", pa->micro_policer_id);
    }

    if (CTC_FLAG_ISSET(pa->flag, CTC_ACL_ACTION_FLAG_METADATA))
    {
        SYS_ACL_DBG_DUMP("  metadata: %u,", pa->metadata);
    }
    else if (CTC_FLAG_ISSET(pa->flag, CTC_ACL_ACTION_FLAG_FID))
    {
        SYS_ACL_DBG_DUMP("  fid: %u,", pa->fid);
    }
    else if (CTC_FLAG_ISSET(pa->flag, CTC_ACL_ACTION_FLAG_VRFID))
    {
        SYS_ACL_DBG_DUMP(" vrfid: %u,", pa->vrfid);
    }

    if (CTC_FLAG_ISSET(pa->flag, CTC_ACL_ACTION_FLAG_MICRO_FLOW_POLICER) +
        CTC_FLAG_ISSET(pa->flag, CTC_ACL_ACTION_FLAG_MACRO_FLOW_POLICER))
    {
        SYS_ACL_DBG_DUMP("\n");
    }

    SYS_ACL_DBG_DUMP("\n");
    if (CTC_FLAG_ISSET(pa->flag, CTC_ACL_ACTION_FLAG_VLAN_EDIT))
    {
        if (pa->vlan_edit->stag_op != CTC_ACL_VLAN_TAG_OP_NONE)
        {
            SYS_ACL_DBG_DUMP("  stag opeation:  %s\n", tag_op[pa->vlan_edit->stag_op]);
            if (pa->vlan_edit->stag_op != CTC_ACL_VLAN_TAG_OP_DEL)
            {
                SYS_ACL_DBG_DUMP("  svid opeation:  %s;  new_svid:  %d\n", field_op[pa->vlan_edit->svid_sl], pa->svid);
                SYS_ACL_DBG_DUMP("  scos opeation:  %s;  new_scos:  %d\n", field_op[pa->vlan_edit->scos_sl], pa->scos);
                SYS_ACL_DBG_DUMP("  scfi opeation:  %s;  new_scfi:  %d\n", field_op[pa->vlan_edit->scfi_sl], pa->scfi);
            }
        }
        if (pa->vlan_edit->ctag_op != CTC_ACL_VLAN_TAG_OP_NONE)
        {
            SYS_ACL_DBG_DUMP("  ctag opeation:  %s\n", tag_op[pa->vlan_edit->ctag_op]);
            if (pa->vlan_edit->ctag_op != CTC_ACL_VLAN_TAG_OP_DEL)
            {
                SYS_ACL_DBG_DUMP("  cvid opeation:  %s;  new_cvid:  %d\n", field_op[pa->vlan_edit->cvid_sl], pa->cvid);
                SYS_ACL_DBG_DUMP("  ccos opeation:  %s;  new_ccos:  %d\n", field_op[pa->vlan_edit->ccos_sl], pa->ccos);
                SYS_ACL_DBG_DUMP("  ccfi opeation:  %s;  new_ccfi:  %d\n", field_op[pa->vlan_edit->ccfi_sl], pa->ccfi);
            }
        }
    }
    return CTC_E_NONE;
}

/*
 * show acl action
 */
STATIC int32
_sys_goldengate_acl_show_pbr_action(uint8 lchip, sys_acl_action_t* pa)
{
    CTC_PTR_VALID_CHECK(pa);

    if (CTC_FLAG_ISSET(pa->flag, CTC_ACL_ACTION_FLAG_PBR_DENY))
    {
        SYS_ACL_DBG_DUMP("  =================== deny ====================");
    }
    else
    {
        SYS_ACL_DBG_DUMP("  =================== permit ====================");
    }

    /* next line */
    SYS_ACL_DBG_DUMP("\n");

    if (CTC_FLAG_ISSET(pa->flag, CTC_ACL_ACTION_FLAG_PBR_FWD))
    {
        SYS_ACL_DBG_DUMP("  forward nhid %u,", pa->nh_id);
    }

    if (CTC_FLAG_ISSET(pa->flag, CTC_ACL_ACTION_FLAG_PBR_ECMP))
    {
        SYS_ACL_DBG_DUMP("  ecmp-nexthop,");
    }

    if (CTC_FLAG_ISSET(pa->flag, CTC_ACL_ACTION_FLAG_PBR_FWD) +
        CTC_FLAG_ISSET(pa->flag, CTC_ACL_ACTION_FLAG_PBR_ECMP))
    {
        SYS_ACL_DBG_DUMP("\n");
    }

    if (CTC_FLAG_ISSET(pa->flag, CTC_ACL_ACTION_FLAG_PBR_TTL_CHECK))
    {
        SYS_ACL_DBG_DUMP("  ttl-check,");
    }

    if (CTC_FLAG_ISSET(pa->flag, CTC_ACL_ACTION_FLAG_PBR_ICMP_CHECK))
    {
        SYS_ACL_DBG_DUMP("  icmp-check,");
    }

    if (CTC_FLAG_ISSET(pa->flag, CTC_ACL_ACTION_FLAG_PBR_CPU))
    {
        SYS_ACL_DBG_DUMP("  copy-to-cpu,");
    }

    SYS_ACL_DBG_DUMP("\n");
    return CTC_E_NONE;
}

/*
 * show mac entry
 */
STATIC int32
_sys_goldengate_acl_show_mac_entry(uint8 lchip, sys_acl_entry_t* p_entry)
{
    sys_acl_action_t * pa;
    sys_acl_sw_tcam_key_mac_t* pk;

    CTC_PTR_VALID_CHECK(p_entry);

    pa = p_entry->action;
    pk = &p_entry->key.u0.mac_key;

    DUMP_COMMON_L2(pk, MAC)
    DUMP_MAC_UNIQUE(pk)

    CTC_ERROR_RETURN(_sys_goldengate_acl_show_action(lchip, pa));

    return CTC_E_NONE;
}

/*
 * show ipv4 entry
 */
STATIC int32
_sys_goldengate_acl_show_ipv4_entry(uint8 lchip, sys_acl_entry_t* p_entry)
{
    sys_acl_action_t  * pa;
    sys_acl_sw_tcam_key_ipv4_t* pk;
    char str[35] = {0};
    char format[10] = {0};

    CTC_PTR_VALID_CHECK(p_entry);

    pa = p_entry->action;
    pk = &p_entry->key.u0.ipv4_key;

    DUMP_COMMON_L2(pk, IPV4)
    DUMP_COMMON_L3(pk, IPV4)
    DUMP_IPV4_UNIQUE(pk)
    DUMP_L4_PROTOCOL(pk, IPV4)

    CTC_ERROR_RETURN(_sys_goldengate_acl_show_action(lchip, pa));

    return CTC_E_NONE;
}

/*
 * show ipv6 entry
 */
STATIC int32
_sys_goldengate_acl_show_ipv6_entry(uint8 lchip, sys_acl_entry_t* p_entry)
{
    sys_acl_action_t* pa;
    sys_acl_sw_tcam_key_ipv6_t* pk;
    char str[35] = {0};
    char format[10] = {0};

    CTC_PTR_VALID_CHECK(p_entry);

    pa = p_entry->action;
    pk = &p_entry->key.u0.ipv6_key;

    DUMP_COMMON_L2(pk, IPV6)
    DUMP_COMMON_L3(pk, IPV6)
    DUMP_IPV6_UNIQUE(pk)
    DUMP_L4_PROTOCOL(pk, IPV6)

    CTC_ERROR_RETURN(_sys_goldengate_acl_show_action(lchip, pa));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_acl_show_hash_mac_entry(uint8 lchip, sys_acl_entry_t* p_entry)
{
    sys_acl_action_t      * pa;
    sys_acl_hash_mac_key_t* pk;
    char str[35] = {0};
    char format[10] = {0};
    uint16 vlan_id = 0;

    CTC_PTR_VALID_CHECK(p_entry);

    pa = p_entry->action;
    pk = &p_entry->key.u0.hash.u0.hash_mac_key;

    SYS_ACL_DBG_DUMP("  ad_index %d", pa->chip.ad_index);
    SYS_ACL_DBG_DUMP("\n");
    SYS_ACL_DBG_DUMP("  mac-da host %02x%02x.%02x%02x.%02x%02x \n",
                     pk->mac_da[0], pk->mac_da[1], pk->mac_da[2],
                     pk->mac_da[3], pk->mac_da[4], pk->mac_da[5]);
    SYS_ACL_DBG_DUMP("  eth-type 0x%x,  gport 0x%x, field_sel_id %d\n", pk->eth_type, pk->gport, pk->field_sel_id);
    vlan_id = pk->vlan_id;
    SYS_ACL_DBG_DUMP("  tag-exist %d, is_ctag %d, vlan-id %s, cos %d, cfi %d\n",
                     pk->tag_exist, pk->is_ctag, CTC_DEBUG_HEX_FORMAT(str, format, vlan_id, 3, U), pk->cos, pk->cfi);

    CTC_ERROR_RETURN(_sys_goldengate_acl_show_action(lchip, pa));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_acl_show_hash_ipv4_entry(uint8 lchip, sys_acl_entry_t* p_entry)
{
    sys_acl_action_t*        pa;
    sys_acl_hash_ipv4_key_t* pk;
    char                     ip_addr[16];
    uint32                   addr;
    char                     str[35] = {0};
    char                     format[10] = {0};
    uint16                   gport = 0;

    CTC_PTR_VALID_CHECK(p_entry);

    pa = p_entry->action;
    pk = &p_entry->key.u0.hash.u0.hash_ipv4_key;

    SYS_ACL_DBG_DUMP("  ad_index %d", pa->chip.ad_index);
    SYS_ACL_DBG_DUMP("\n");

    SYS_ACL_DBG_DUMP("  ip-sa host");
    addr = sal_ntohl(pk->ip_sa);
    sal_inet_ntop(AF_INET, &addr, ip_addr, sizeof(ip_addr));
    SYS_ACL_DBG_DUMP(" %s", ip_addr);

    SYS_ACL_DBG_DUMP("  ip-da host");
    addr = sal_ntohl(pk->ip_da);
    sal_inet_ntop(AF_INET, &addr, ip_addr, sizeof(ip_addr));
    SYS_ACL_DBG_DUMP(" %s\n", ip_addr);

    gport = pk->gport;
    SYS_ACL_DBG_DUMP("  l4-src-port 0x%x,  l4-dst-port 0x%x\n", pk->l4_src_port, pk->l4_dst_port);
    SYS_ACL_DBG_DUMP("  gport %s,  dscp %d,  l4_protocol %d,  ecn %d\n", CTC_DEBUG_HEX_FORMAT(str, format, gport, 4, U), pk->dscp, pk->l4_protocol, pk->ecn);
    SYS_ACL_DBG_DUMP("  field_sel_id %d,  metadata %s\n", pk->field_sel_id, CTC_DEBUG_HEX_FORMAT(str, format, pk->metadata, 4, U));

    CTC_ERROR_RETURN(_sys_goldengate_acl_show_action(lchip, pa));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_acl_show_hash_l2_l3_entry(uint8 lchip, sys_acl_entry_t* p_entry)
{
    sys_acl_action_t*         pa;
    sys_acl_hash_l2_l3_key_t* pk;
    char str[35];
    char format[10];

    CTC_PTR_VALID_CHECK(p_entry);

    pa = p_entry->action;
    pk = &p_entry->key.u0.hash.u0.hash_l2_l3_key;

    SYS_ACL_DBG_DUMP("  ad_index %d\n", pa->chip.ad_index);

    SYS_ACL_DBG_DUMP("  field_sel_id %d, eth_type %s, gport %s, metadata %s\n",
                     pk->field_sel_id,
                     CTC_DEBUG_HEX_FORMAT(str, format, pk->eth_type, 4, U),
                     CTC_DEBUG_HEX_FORMAT(str, format, pk->gport, 4, U),
                     CTC_DEBUG_HEX_FORMAT(str, format, pk->metadata, 4, U));

    SYS_ACL_DBG_DUMP("  mac_da %s, mac_sa %s\n", sys_goldengate_output_mac(pk->mac_da), sys_goldengate_output_mac(pk->mac_sa));
    SYS_ACL_DBG_DUMP("  ctag_valid %d, ctag_cos %d, ctag_cfi %d, ctag_vid %s\n",
                     pk->ctag_valid, pk->ctag_cos, pk->ctag_cfi,
                     CTC_DEBUG_HEX_FORMAT(str, format, pk->ctag_vid, 3, U));

    SYS_ACL_DBG_DUMP("  stag_valid %d, stag_cos %d, stag_cfi %d, stag_vid %s\n",
                     pk->stag_valid, pk->stag_cos, pk->stag_cfi,
                     CTC_DEBUG_HEX_FORMAT(str, format, pk->stag_vid, 3, U));

    SYS_ACL_DBG_DUMP("  l4_src_port %s, l4_dst_port %s\n",
                     CTC_DEBUG_HEX_FORMAT(str, format, pk->l4_src_port, 4, U),
                     CTC_DEBUG_HEX_FORMAT(str, format, pk->l4_dst_port, 4, U));

    if (CTC_PARSER_L3_TYPE_IPV4 == pk->l3_type)
    {
        SYS_ACL_DBG_DUMP("ip_sa %s, ip_da %s\n",
                         CTC_DEBUG_HEX_FORMAT(str, format, pk->l3.ipv4.ip_sa, 8, U),
                         CTC_DEBUG_HEX_FORMAT(str, format, pk->l3.ipv4.ip_da, 8, U));

        SYS_ACL_DBG_DUMP("dscp %d, ecn %d, ttl %d\n", pk->l3.ipv4.dscp, pk->l3.ipv4.ecn, pk->l3.ipv4.ttl);
    }
    else if ((CTC_PARSER_L3_TYPE_MPLS == pk->l3_type) ||
             (CTC_PARSER_L3_TYPE_MPLS_MCAST == pk->l3_type))
    {
        SYS_ACL_DBG_DUMP("label_num %d\n", pk->l3.mpls.label_num);
        SYS_ACL_DBG_DUMP("mpls_label0 %s, mpls_exp0 %d, mpls_s0 %d, mpls_ttl0 %d\n",
                         CTC_DEBUG_HEX_FORMAT(str, format, pk->l3.mpls.mpls_label0, 7, U),
                         pk->l3.mpls.mpls_exp0, pk->l3.mpls.mpls_s0, pk->l3.mpls.mpls_ttl0);
        SYS_ACL_DBG_DUMP("mpls_label1 %s, mpls_exp1 %d, mpls_s1 %d, mpls_ttl1 %d\n",
                         CTC_DEBUG_HEX_FORMAT(str, format, pk->l3.mpls.mpls_label1, 7, U),
                         pk->l3.mpls.mpls_exp1, pk->l3.mpls.mpls_s1, pk->l3.mpls.mpls_ttl1);
        SYS_ACL_DBG_DUMP("mpls_label2 %s, mpls_exp2 %d, mpls_s2 %d, mpls_ttl2 %d\n",
                         CTC_DEBUG_HEX_FORMAT(str, format, pk->l3.mpls.mpls_label2, 7, U),
                         pk->l3.mpls.mpls_exp2, pk->l3.mpls.mpls_s2, pk->l3.mpls.mpls_ttl2);
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_acl_show_hash_mpls_entry(uint8 lchip, sys_acl_entry_t* p_entry)
{
    sys_acl_action_t*         pa;
    sys_acl_hash_mpls_key_t*  pk;
    char str[35];
    char format[10];

    CTC_PTR_VALID_CHECK(p_entry);

    pa = p_entry->action;
    pk = &p_entry->key.u0.hash.u0.hash_mpls_key;

    SYS_ACL_DBG_DUMP("  ad_index %d\n", pa->chip.ad_index);

    SYS_ACL_DBG_DUMP("  field_sel_id %d,  gport %s,  metadata %s\n",
                     pk->field_sel_id,
                     CTC_DEBUG_HEX_FORMAT(str, format, pk->gport, 4, U),
                     CTC_DEBUG_HEX_FORMAT(str, format, pk->metadata, 4, U));

    SYS_ACL_DBG_DUMP("label_num %d\n", pk->label_num);
    SYS_ACL_DBG_DUMP("mpls_label0 %s, mpls_exp0 %d, mpls_s0 %d, mpls_ttl0 %d\n",
                     CTC_DEBUG_HEX_FORMAT(str, format, pk->mpls_label0, 7, U),
                     pk->mpls_exp0, pk->mpls_s0, pk->mpls_ttl0);
    SYS_ACL_DBG_DUMP("mpls_label1 %s, mpls_exp1 %d, mpls_s1 %d, mpls_ttl1 %d\n",
                     CTC_DEBUG_HEX_FORMAT(str, format, pk->mpls_label1, 7, U),
                     pk->mpls_exp1, pk->mpls_s1, pk->mpls_ttl1);
    SYS_ACL_DBG_DUMP("mpls_label2 %s, mpls_exp2 %d, mpls_s2 %d, mpls_ttl2 %d\n",
                     CTC_DEBUG_HEX_FORMAT(str, format, pk->mpls_label2, 7, U),
                     pk->mpls_exp2, pk->mpls_s2, pk->mpls_ttl2);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_acl_show_hash_ipv6_entry(uint8 lchip, sys_acl_entry_t* p_entry)
{
    sys_acl_action_t*         pa;
    sys_acl_hash_ipv6_key_t*  pk;
    char str[35];
    char format[10];

    CTC_PTR_VALID_CHECK(p_entry);

    pa = p_entry->action;
    pk = &p_entry->key.u0.hash.u0.hash_ipv6_key;

    SYS_ACL_DBG_DUMP("  ad_index %d\n", pa->chip.ad_index);

    SYS_ACL_DBG_DUMP("  field_sel_id %d,  gport %s\n",
                     pk->field_sel_id,
                     CTC_DEBUG_HEX_FORMAT(str, format, pk->gport, 4, U));

    SYS_ACL_DBG_DUMP("  l4_src_port %s, l4_dst_port %s\n",
                     CTC_DEBUG_HEX_FORMAT(str, format, pk->l4_src_port, 4, U),
                     CTC_DEBUG_HEX_FORMAT(str, format, pk->l4_dst_port, 4, U));

    SYS_ACL_DBG_DUMP("  ip_sa %X%X%X%X  ip_da %X%X%X%xX\n",
                     pk->ip_sa[0], pk->ip_sa[1], pk->ip_sa[2], pk->ip_sa[3],
                     pk->ip_da[0], pk->ip_da[1], pk->ip_da[2], pk->ip_da[3]);

    SYS_ACL_DBG_DUMP("  dscp %s, l4_type %s\n",
                     CTC_DEBUG_HEX_FORMAT(str, format, pk->dscp, 2, U),
                     CTC_DEBUG_HEX_FORMAT(str, format, pk->l4_type, 2, U));

    return CTC_E_NONE;
}

/*
 * show pbr ipv4 entry
 */
STATIC int32
_sys_goldengate_acl_show_pbr_ipv4_entry(uint8 lchip, sys_acl_entry_t* p_entry)
{
    sys_acl_action_t      * pa;
    sys_acl_pbr_ipv4_key_t* pk;
    char str[35];
    char format[10];

    CTC_PTR_VALID_CHECK(p_entry);

    pa = p_entry->action;
    pk = &p_entry->key.u0.pbr_ipv4_key;

    DUMP_IPV4(pk, PBR_IPV4)
    DUMP_L3_COMMON_PBR(pk, PBR_IPV4)
    DUMP_L4_PROTOCOL(pk, PBR_IPV4)

    CTC_ERROR_RETURN(_sys_goldengate_acl_show_pbr_action(lchip, pa));

    return CTC_E_NONE;
}

/*
 * show pbr ipv6 entry
 */
STATIC int32
_sys_goldengate_acl_show_pbr_ipv6_entry(uint8 lchip, sys_acl_entry_t* p_entry)
{
    sys_acl_action_t      * pa;
    sys_acl_pbr_ipv6_key_t* pk;
    char str[35];
    char format[10];

    CTC_PTR_VALID_CHECK(p_entry);

    pa = p_entry->action;
    pk = &p_entry->key.u0.pbr_ipv6_key;

    DUMP_IPV6(pk, PBR_IPV6)
    DUMP_L3_COMMON_PBR(pk,PBR_IPV6 )
    DUMP_IPV6_PBR_UNIQUE(pk)
    DUMP_L4_PROTOCOL(pk, PBR_IPV6)

    CTC_ERROR_RETURN(_sys_goldengate_acl_show_pbr_action(lchip, pa));

    return CTC_E_NONE;
}

/*
 * show acl entry to a specific entry with specific key type
 */
STATIC int32
_sys_goldengate_acl_show_entry(uint8 lchip, sys_acl_entry_t* pe, uint32* p_cnt, ctc_acl_key_type_t key_type, uint8 detail)
{
    char*  type[CTC_ACL_KEY_NUM] = {"Tcam-Mac ", "Tcam-Ipv4", NULL, "Tcam-Ipv6", "Hash-Mac", "Hash-Ipv4",
                                    "Hash-L2&L3", "Hash-Mpls", "Hash-Ipv6", "Tcam-pbr-ipv4", "Tcam-pbr-ipv6"};
    sys_acl_group_t* pg;
    char   str[35];
    char   format[10];
    uint64 flag = 0;
    uint32 key_id = 0;
    uint32 act_id = 0;
    uint8  step = 0;
    char   key_name[50] = {0};
    char   ad_name[50] = {0};

    CTC_PTR_VALID_CHECK(pe);

    pg = pe->group;
    if (!pg)
    {
        return CTC_E_ACL_GROUP_UNEXIST;
    }

    if ((SYS_ACL_ALL_KEY != key_type)
        && (pe->key.type != key_type))
    {
        return CTC_E_NONE;
    }

    SYS_ACL_DBG_DUMP("%-8d", *p_cnt);
    if(SYS_ACL_SHOW_IN_HEX <= pe->fpae.entry_id)
    {
        SYS_ACL_DBG_DUMP("%-12s", CTC_DEBUG_HEX_FORMAT(str, format, pe->fpae.entry_id, 8, U));
    }
    else
    {
        SYS_ACL_DBG_DUMP("%-12u", pe->fpae.entry_id);
    }

    if (SYS_ACL_SHOW_IN_HEX <= pg->group_id)
    {
        SYS_ACL_DBG_DUMP("%-12s", CTC_DEBUG_HEX_FORMAT(str, format, pg->group_id, 8, U));
    }
    else
    {
        SYS_ACL_DBG_DUMP("%-12u", pg->group_id);
    }


    SYS_ACL_DBG_DUMP("%-4s", (pe->fpae.flag) ? "Y" : "N");
    if(SYS_ACL_SHOW_IN_HEX <= pe->fpae.priority)
    {
        SYS_ACL_DBG_DUMP("%-12s", CTC_DEBUG_HEX_FORMAT(str, format, pe->fpae.priority, 8, U));
    }
    else
    {
        SYS_ACL_DBG_DUMP("%-12u", pe->fpae.priority);
    }
    SYS_ACL_DBG_DUMP("%-8u", pg->group_info.block_id);
    SYS_ACL_DBG_DUMP("%-14s", type[pe->key.type]);
    if (ACL_ENTRY_IS_TCAM(pe->key.type))
    {
        _sys_goldengate_acl_get_fpa_size(lchip, pe, &step);
        SYS_ACL_DBG_DUMP("%-8u",  pe->fpae.offset_a/step);
    }
    else
    {
        SYS_ACL_DBG_DUMP("%-8s",  CTC_DEBUG_HEX_FORMAT(str, format, pe->fpae.offset_a, 4, U));
    }


    flag = pe->action->flag;
    if(flag==0)
    {
        SYS_ACL_DBG_DUMP("%s\n", "PERMIT");
    }
    else if(CTC_FLAG_ISSET(flag, CTC_ACL_ACTION_FLAG_DISCARD))
    {
        SYS_ACL_DBG_DUMP("%s\n", "DISCARD");
    }
    else if(CTC_FLAG_ISSET(flag, CTC_ACL_ACTION_FLAG_REDIRECT))
    {
        SYS_ACL_DBG_DUMP("%s\n", "REDIRECT");
    }
    else
    {
        SYS_ACL_DBG_DUMP("%s\n", "OTHER");
    }

    (*p_cnt)++;

    if (detail)
    {
        _sys_goldengate_acl_get_table_id(lchip, pe, &key_id, &act_id);
        drv_goldengate_get_tbl_string_by_id(key_id, key_name);
        drv_goldengate_get_tbl_string_by_id(act_id, ad_name);

        SYS_ACL_DBG_DUMP("\n");
        SYS_ACL_DBG_DUMP("Detail Entry Info\n");
        SYS_ACL_DBG_DUMP("-----------------------------------------------\n");

        if (!ACL_ENTRY_IS_TCAM(pe->key.type))
        {
            SYS_ACL_DBG_DUMP(" --%-32s :0x%-8x\n", key_name,pe->fpae.offset_a);
            SYS_ACL_DBG_DUMP(" --%-32s :0x%-8x\n", ad_name, pe->action->chip.ad_index);
        }
        else
        {
            SYS_ACL_DBG_DUMP(" --%-32s :%-8u\n", key_name,pe->fpae.offset_a/step);
            SYS_ACL_DBG_DUMP(" --%-32s :%-8u\n", ad_name, pe->fpae.offset_a);
        }


        if((CTC_ACL_KEY_IPV4 == pe->key.type)
            && (CTC_FLAG_ISSET(pe->key.u0.ipv4_key.sub_flag, CTC_ACL_IPV4_KEY_SUB_FLAG_L4_SRC_PORT)
                ||CTC_FLAG_ISSET(pe->key.u0.ipv4_key.sub_flag, CTC_ACL_IPV4_KEY_SUB_FLAG_L4_DST_PORT))
            )
        {
            if((pe->key.u0.ipv4_key.u0.ip.l4.flag_l4_src_port_range)
                ||(pe->key.u0.ipv4_key.u0.ip.l4.flag_l4_dst_port_range))
            {
                SYS_ACL_DBG_DUMP(" --%-32s :%-8u\n", "ParserLayer4PortOpSel",0);
                SYS_ACL_DBG_DUMP(" --%-32s :%-8u\n", "ParserLayer4PortOpCtl",0);
            }

            if(pe->key.u0.ipv4_key.u0.ip.l4.flag_l4_src_port_range)
            {
                SYS_ACL_DBG_DUMP("   %-32s :%-8u\n", "  --l4_src_port_range_idx",pe->key.u0.ipv4_key.u0.ip.l4.l4_src_port_range_idx);
            }

            if(pe->key.u0.ipv4_key.u0.ip.l4.flag_l4_dst_port_range)
            {
                SYS_ACL_DBG_DUMP("   %-32s :%-8u\n", "  --l4_dst_port_range_idx",pe->key.u0.ipv4_key.u0.ip.l4.l4_dst_port_range_idx);
            }
        }

        if((CTC_ACL_KEY_IPV4 == pe->key.type)
            && CTC_FLAG_ISSET(pe->key.u0.ipv4_key.sub_flag, CTC_ACL_IPV4_KEY_SUB_FLAG_TCP_FLAGS))
        {
            if(pe->key.u0.ipv4_key.u0.ip.l4.flag_tcp_flags)
            {
                SYS_ACL_DBG_DUMP(" --%-32s :%-8u\n", "ParserLayer4FlagOpCtl",0);
                SYS_ACL_DBG_DUMP("   %-32s :%-8u\n", "  --tcp_flags_idx",pe->key.u0.ipv4_key.u0.ip.l4.tcp_flags_idx);
            }
        }

        SYS_ACL_DBG_DUMP("\n");
        SYS_ACL_DBG_DUMP("Acl Action Info\n");
        SYS_ACL_DBG_DUMP("-----------------------------------------------\n");


        if (detail)
        {
            if((CTC_ACL_KEY_PBR_IPV4 == pe->key.type)||(CTC_ACL_KEY_PBR_IPV6 == pe->key.type))
            {
                SYS_ACL_DBG_DUMP(" --%-32s :0x%-16"PRIx64"\n", "flag(ctc_acl_pbr_action_flag_t)",pe->action->flag);

                if (CTC_FLAG_ISSET(flag, CTC_ACL_ACTION_FLAG_PBR_FWD))
                {
                    SYS_ACL_DBG_DUMP(" --%-32s :%-8u\n", "nh-id",pe->action->nh_id);
                }

                if (CTC_FLAG_ISSET(flag, CTC_ACL_ACTION_FLAG_PBR_TTL_CHECK))
                {
                    SYS_ACL_DBG_DUMP(" --%-32s :%-8u\n", "ttl-check",1);
                }

                if (CTC_FLAG_ISSET(flag, CTC_ACL_ACTION_FLAG_PBR_ICMP_CHECK))
                {
                    SYS_ACL_DBG_DUMP(" --%-32s :%-8u\n", "icmp-check",1);
                }

                if (CTC_FLAG_ISSET(flag, CTC_ACL_ACTION_FLAG_PBR_CPU))
                {
                    SYS_ACL_DBG_DUMP(" --%-32s :%-8u\n", "copy-to-cpu",1);
                }
            }
            else
            {
                SYS_ACL_DBG_DUMP(" --%-32s :0x%-16"PRIx64"\n", "flag(ctc_acl_action_flag_t)",pe->action->flag);

                if(CTC_FLAG_ISSET(flag, CTC_ACL_ACTION_FLAG_REDIRECT))
                {
                    SYS_ACL_DBG_DUMP(" --%-32s :%-8u\n", "nh-id",pe->action->nh_id);
                }

                if(CTC_FLAG_ISSET(flag, CTC_ACL_ACTION_FLAG_MICRO_FLOW_POLICER))
                {
                    SYS_ACL_DBG_DUMP(" --%-32s :%-8u\n", "micro-policer-id",pe->action->micro_policer_id);
                }

                if(CTC_FLAG_ISSET(flag, CTC_ACL_ACTION_FLAG_MACRO_FLOW_POLICER))
                {
                    SYS_ACL_DBG_DUMP(" --%-32s :%-8u\n", "macro-policer-id",pe->action->macro_policer_id);
                }

                if(CTC_FLAG_ISSET(flag, CTC_ACL_ACTION_FLAG_STATS))
                {
                    SYS_ACL_DBG_DUMP(" --%-32s :%-8u\n", "stats-id",pe->action->stats_id);
                }

                if(CTC_FLAG_ISSET(flag, CTC_ACL_ACTION_FLAG_RANDOM_LOG))
                {
                    SYS_ACL_DBG_DUMP(" --%-32s :%-8u\n", "acl-log-id",pe->action->acl_log_id);
                }
            }
        }
        else
        {
            /* old process, cannot go here, for clear warning */
            switch (pe->key.type)
            {
            case CTC_ACL_KEY_MAC:
                _sys_goldengate_acl_show_mac_entry(lchip, pe);
                break;

            case CTC_ACL_KEY_IPV4:
                _sys_goldengate_acl_show_ipv4_entry(lchip, pe);
                break;

            case CTC_ACL_KEY_MPLS:
                break;

            case CTC_ACL_KEY_IPV6:
                _sys_goldengate_acl_show_ipv6_entry(lchip, pe);
                break;

            case CTC_ACL_KEY_HASH_MAC:
                _sys_goldengate_acl_show_hash_mac_entry(lchip, pe);
                break;

            case CTC_ACL_KEY_HASH_IPV4:
                _sys_goldengate_acl_show_hash_ipv4_entry(lchip, pe);
                break;

            case CTC_ACL_KEY_HASH_L2_L3:
                _sys_goldengate_acl_show_hash_l2_l3_entry(lchip, pe);
                break;

            case CTC_ACL_KEY_HASH_MPLS:
                _sys_goldengate_acl_show_hash_mpls_entry(lchip, pe);
                break;

            case CTC_ACL_KEY_HASH_IPV6:
                _sys_goldengate_acl_show_hash_ipv6_entry(lchip, pe);
                break;

            case CTC_ACL_KEY_PBR_IPV4:
                _sys_goldengate_acl_show_pbr_ipv4_entry(lchip, pe);
                break;

            case CTC_ACL_KEY_PBR_IPV6:
                _sys_goldengate_acl_show_pbr_ipv6_entry(lchip, pe);
                break;

            default:
                break;

            }
        }
    }

    return CTC_E_NONE;
}

/*
 * show acl entriy by entry id
 */
STATIC int32
_sys_goldengate_acl_show_entry_by_entry_id(uint8 lchip, uint32 eid, ctc_acl_key_type_t key_type, uint8 detail)
{
    sys_acl_entry_t* pe = NULL;

    uint32         count = 1;

    SYS_ACL_LOCK(lchip);
    _sys_goldengate_acl_get_nodes_by_eid(lchip, eid, &pe, NULL, NULL);
    if (!pe)
    {
        SYS_ACL_UNLOCK(lchip);
        return CTC_E_ACL_ENTRY_UNEXIST;
    }

    /* ACL_PRINT_ENTRY_ROW(eid); */

    CTC_ERROR_RETURN_ACL_UNLOCK(_sys_goldengate_acl_show_entry(lchip, pe, &count, key_type, detail));
    SYS_ACL_UNLOCK(lchip);

    return CTC_E_NONE;
}

/*
 * show acl entries in a group with specific key type
 */
STATIC int32
_sys_goldengate_acl_show_entry_in_group(uint8 lchip, sys_acl_group_t* pg, uint32* p_cnt, ctc_acl_key_type_t key_type, uint8 detail)
{
    struct ctc_slistnode_s* pe;

    CTC_PTR_VALID_CHECK(pg);

    CTC_SLIST_LOOP(pg->entry_list, pe)
    {
        _sys_goldengate_acl_show_entry(lchip, (sys_acl_entry_t *) pe, p_cnt, key_type, detail);
    }

    return CTC_E_NONE;
}

/*
 * show acl entries by group id with specific key type
 */
STATIC int32
_sys_goldengate_acl_show_entry_by_group_id(uint8 lchip, uint32 gid, ctc_acl_key_type_t key_type, uint8 detail)
{
    uint32         count = 1;
    sys_acl_group_t* pg  = NULL;

    _sys_goldengate_acl_get_group_by_gid(lchip, gid, &pg);
    SYS_ACL_CHECK_GROUP_UNEXIST(pg);

    /* ACL_PRINT_GROUP_ROW(gid); */

    CTC_ERROR_RETURN(_sys_goldengate_acl_show_entry_in_group(lchip, pg, &count, key_type, detail));

    return CTC_E_NONE;
}

/*
 * show acl entries in a block with specific key type
 */
STATIC int32
_sys_goldengate_acl_show_entry_in_block(uint8 lchip, sys_acl_block_t* pb, uint32* p_cnt, ctc_acl_key_type_t key_type, uint8 detail)
{
    uint8 step = 0;
    ctc_fpa_entry_t* pe;
    uint16         block_idx;

    CTC_PTR_VALID_CHECK(pb);
    CTC_PTR_VALID_CHECK(p_cnt);

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
            CTC_ERROR_RETURN(_sys_goldengate_acl_show_entry(lchip, ACL_OUTER_ENTRY(pe), p_cnt, key_type, detail));
        }
    }

    return CTC_E_NONE;
}

/*
 * show acl entries by group priority
 */
STATIC int32
_sys_goldengate_acl_show_entry_by_priority(uint8 lchip, uint8 prio, ctc_acl_key_type_t key_type, uint8 detail)
{
    uint32 count = 1;
    sys_acl_block_t* pb;

    CTC_MAX_VALUE_CHECK(prio, (ACL_IGS_BLOCK_ID_NUM - 1));

    count = 1;
    SYS_ACL_LOCK(lchip);
    pb = &p_gg_acl_master[lchip]->block[prio];
    if (pb->fpab.free_count != pb->fpab.entry_count) /*not empty*/
    {
        CTC_ERROR_RETURN_ACL_UNLOCK(_sys_goldengate_acl_show_entry_in_block(lchip, pb, &count, key_type, detail));
    }

    if(prio < ACL_EGS_BLOCK_ID_NUM)
    {
        pb = &p_gg_acl_master[lchip]->block[prio + ACL_IGS_BLOCK_ID_NUM];
        if (pb->fpab.free_count != pb->fpab.entry_count) /*not empty*/
        {
            CTC_ERROR_RETURN_ACL_UNLOCK(_sys_goldengate_acl_show_entry_in_block(lchip, pb, &count, key_type, detail));
        }
    }
    SYS_ACL_UNLOCK(lchip);

    return CTC_E_NONE;
}

/*
 * callback for hash traverse. traverse group hash table, show each group's entry.
 *
 */
STATIC int32
_sys_goldengate_acl_hash_traverse_cb_show_entry(sys_acl_group_t* pg, _acl_cb_para_t* cb_para)
{
    uint8 lchip = 0;
    CTC_PTR_VALID_CHECK(pg);
    CTC_PTR_VALID_CHECK(cb_para);
    lchip = pg->lchip;
    CTC_ERROR_RETURN(_sys_goldengate_acl_show_entry_in_group(lchip, pg, &(cb_para->count), cb_para->key_type, cb_para->detail));
    return CTC_E_NONE;
}

/*
 * callback for hash traverse. traverse group hash table, show each group's group id.
 *
 */
STATIC int32
_sys_goldengate_acl_hash_traverse_cb_show_inner_gid(sys_acl_group_t* pg, _acl_cb_para_t* cb_para)
{
    CTC_PTR_VALID_CHECK(cb_para);

    if(pg->group_id>CTC_ACL_GROUP_ID_NORMAL)
    {
        cb_para->count++;
        SYS_ACL_DBG_DUMP("No.%03u  :  gid 0x%0X \n", cb_para->count, pg->group_id);
    }

    return CTC_E_NONE;
}

/*
 * callback for hash traverse. traverse group hash table, show each group's group id.
 *
 */
STATIC int32
_sys_goldengate_acl_hash_traverse_cb_show_outer_gid(sys_acl_group_t* pg, _acl_cb_para_t* cb_para)
{
    CTC_PTR_VALID_CHECK(cb_para);

    if(pg->group_id<CTC_ACL_GROUP_ID_NORMAL)
    {
        cb_para->count++;
        SYS_ACL_DBG_DUMP("No.%03u  :  gid %0d \n", cb_para->count, pg->group_id);
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_acl_hash_traverse_cb_show_group(sys_acl_group_t* pg, _acl_cb_para_t* cb_para)
{
    char str[35];
    char format[10];
    char* dir[3] = {"IGS", "EGS", "BOTH"};
    char* type[CTC_ACL_GROUP_TYPE_MAX] = {"NONE", "HASH", "PORT_BITMAP", "GLOBAL", "VLAN_CLASS", "PORT_CLASS", "SERVICE_ACL",
                                                                           "PBR_CLASS", "GPORT"};

    CTC_PTR_VALID_CHECK(cb_para);
    cb_para->count++;
    if(pg)
    {
        SYS_ACL_DBG_DUMP("%-6d", cb_para->count);

        if (SYS_ACL_SHOW_IN_HEX <= pg->group_id)
        {
            SYS_ACL_DBG_DUMP("%-12s", CTC_DEBUG_HEX_FORMAT(str, format, pg->group_id, 8, U));
        }
        else
        {
            SYS_ACL_DBG_DUMP("%-12u", pg->group_id);
        }

        SYS_ACL_DBG_DUMP("%-6s", dir[pg->group_info.dir]);
        SYS_ACL_DBG_DUMP("%-6u", pg->group_info.block_id);

        if (pg->group_id == CTC_ACL_GROUP_ID_HASH_MAC)
        {
            SYS_ACL_DBG_DUMP("%-14s", "HASH_MAC");
        }
        else if (pg->group_id == CTC_ACL_GROUP_ID_HASH_IPV4)
        {
            SYS_ACL_DBG_DUMP("%-14s", "HASH_IPV4");
        }
        else if (pg->group_id == CTC_ACL_GROUP_ID_HASH_MPLS)
        {
            SYS_ACL_DBG_DUMP("%-14s", "HASH_MPLS");
        }
        else if (pg->group_id == CTC_ACL_GROUP_ID_HASH_L2_L3)
        {
            SYS_ACL_DBG_DUMP("%-14s", "HASH_L2L3");
        }
        else if (pg->group_id == CTC_ACL_GROUP_ID_HASH_IPV6)
        {
            SYS_ACL_DBG_DUMP("%-14s", "HASH_IPV6");
        }
        else
        {
            SYS_ACL_DBG_DUMP("%-14s", type[pg->group_info.type]);
        }

        SYS_ACL_DBG_DUMP("%-10u", pg->entry_count);

        if (pg->group_info.type == CTC_ACL_GROUP_TYPE_PORT)
        {
            SYS_ACL_DBG_DUMP("0x%-6.4x\n", pg->group_info.un.gport);
        }
        else if (pg->group_info.type == CTC_ACL_GROUP_TYPE_PORT_BITMAP)
        {
            SYS_ACL_DBG_DUMP("0x%-.8x%-.8x%-.8x%-.8x\n", pg->group_info.un.port_bitmap[0], pg->group_info.un.port_bitmap[1],
                pg->group_info.un.port_bitmap[2],pg->group_info.un.port_bitmap[3]);

        }
        else if ((pg->group_info.type == CTC_ACL_GROUP_TYPE_HASH) || (pg->group_info.type == CTC_ACL_GROUP_TYPE_GLOBAL)
            || (pg->group_info.type == CTC_ACL_GROUP_TYPE_NONE))
        {
            SYS_ACL_DBG_DUMP("%-8s\n", "-");
        }
        else if (pg->group_info.type == CTC_ACL_GROUP_TYPE_PBR_CLASS)
        {
            SYS_ACL_DBG_DUMP("%-u\n", pg->group_info.un.pbr_class_id);
        }
        else
        {
            SYS_ACL_DBG_DUMP("%-u\n", pg->group_info.un.port_class_id);
        }


    }

    return CTC_E_NONE;
}

/*
 * show all acl entries separate by priority or group
 * sort_type = 0: by priority
 * sort_type = 1: by group
 */
STATIC int32
_sys_goldengate_acl_show_entry_all(uint8 lchip, uint8 sort_type, ctc_acl_key_type_t key_type, uint8 detail)
{
    uint8          prio;
    uint32         count = 1;
    uint32         gid;
    _acl_cb_para_t para;
    sys_acl_block_t* pb;

    sal_memset(&para, 0, sizeof(_acl_cb_para_t));
    para.detail   = detail;
    para.key_type = key_type;

    /* ACL_PRINT_ALL_SORT_SEP_BY_ROW(sort_type); */

    SYS_ACL_LOCK(lchip);
    if (sort_type == 0) /* separate by priority */
    {
        /* tcam entry by priority */
        count = 1;
        for (prio = 0; prio < ACL_BLOCK_ID_NUM; prio++)
        {
            pb = &p_gg_acl_master[lchip]->block[prio];
            CTC_ERROR_RETURN_ACL_UNLOCK(_sys_goldengate_acl_show_entry_in_block
                                     (lchip, pb, &count, key_type, detail));
        }

        for (gid = CTC_ACL_GROUP_ID_NORMAL + 1; gid < CTC_ACL_GROUP_ID_HASH_RSV_0; gid++)
        {
            CTC_ERROR_RETURN_ACL_UNLOCK(_sys_goldengate_acl_show_entry_by_group_id(lchip, gid, key_type, detail));
        }

        /* hash entry by group */
    }
    else /* separate by group */
    {
        ctc_hash_traverse_through(p_gg_acl_master[lchip]->group,
                                  (hash_traversal_fn) _sys_goldengate_acl_hash_traverse_cb_show_entry, &para);
    }
    SYS_ACL_UNLOCK(lchip);

    return CTC_E_NONE;
}

int32
sys_goldengate_acl_show_status(uint8 lchip)
{
    uint8          idx;
    uint8          step      = 0;
    uint8          block_id  = 0;
    uint16         block_idx = 0;
    sys_acl_block_t* pb      = NULL;
    ctc_fpa_entry_t* pe      = NULL;

    uint16         mac_cnt        = 0;
    uint16         ipv4_cnt       = 0;
    uint16         ipv6_cnt       = 0;
    uint16         pbr_ipv4_cnt   = 0;
    uint16         pbr_ipv6_cnt   = 0;
    uint16         hash_mac_cnt   = 0;
    uint16         hash_ipv4_cnt  = 0;
    uint16         hash_ipv6_cnt  = 0;
    uint16         hash_mpls_cnt  = 0;
    uint16         hash_l2_l3_cnt = 0;
    char*  acl_type[8] = {"MAC/IPv4-S", "IPv4-D", "IPv6", "MAC/IPv4-S(EGS)",
                           "IPv4-D(EGS)", "IPv6(EGS)", "PBR_IPv4", "PBR_IPv6"};

    char*  acl_type1[8] = {"IPv4-S", "MAC/IPv4-D", "IPv6", "IPv4-S(EGS)",
                           "MAC/IPv4-D(EGS)", "IPv6(EGS)", "PBR_IPv4", "PBR_IPv6"};

    sys_acl_group_t* pg;

    _acl_cb_para_t para;

    SYS_ACL_INIT_CHECK(lchip);

    SYS_ACL_DBG_DUMP("================= ACL Overall Status ==================\n");

    SYS_ACL_DBG_DUMP("\n");
    SYS_ACL_DBG_DUMP("#0 Group Status\n");
    SYS_ACL_DBG_DUMP("--------------------------------------------------------------------------------\n");

    SYS_ACL_DBG_DUMP("-----------------****Inner Acl Group****--------------- \n");
    sal_memset(&para, 0, sizeof(_acl_cb_para_t));
    para.count = 0;

    SYS_ACL_LOCK(lchip);
    ctc_hash_traverse_through(p_gg_acl_master[lchip]->group,
                              (hash_traversal_fn) _sys_goldengate_acl_hash_traverse_cb_show_inner_gid, &para);
    SYS_ACL_UNLOCK(lchip);

    SYS_ACL_DBG_DUMP("Total group count :%u \n", 5);

    SYS_ACL_DBG_DUMP("-----------------****Outer Acl Group****--------------- \n");
    sal_memset(&para, 0, sizeof(_acl_cb_para_t));
    para.count = 0;
    SYS_ACL_LOCK(lchip);
    ctc_hash_traverse_through(p_gg_acl_master[lchip]->group,
                              (hash_traversal_fn) _sys_goldengate_acl_hash_traverse_cb_show_outer_gid, &para);
    SYS_ACL_UNLOCK(lchip);

    SYS_ACL_DBG_DUMP("Total group count :%u \n", para.count);

    SYS_ACL_DBG_DUMP("\n");
    SYS_ACL_DBG_DUMP("#1 Priority Status \n");
    SYS_ACL_DBG_DUMP("--------------------------------------------------------------------------------\n");

    SYS_ACL_DBG_DUMP("%-20s %-14s %-14s %-14s %-12s\n", "Type\\Prio","Prio 0","Prio 1","Prio 2","Prio 3");
    for (idx = 0; idx < CTC_FPA_KEY_SIZE_NUM; idx++)
    {
        SYS_ACL_LOCK(lchip);
        if(0 == p_gg_acl_master[lchip]->ipv4_160bit_mode_en)
        {
            SYS_ACL_DBG_DUMP("%-17s", acl_type[idx]);
        }
        else
        {
            SYS_ACL_DBG_DUMP("%-17s", acl_type1[idx]);
        }
        for (block_id = 0; block_id < ACL_IGS_BLOCK_ID_NUM; block_id++)
        {
            SYS_ACL_DBG_DUMP("%5d of %-6d",
                             (p_gg_acl_master[lchip]->block[block_id].fpab.sub_entry_count[idx] - p_gg_acl_master[lchip]->block[block_id].fpab.sub_free_count[idx]),
                             p_gg_acl_master[lchip]->block[block_id].fpab.sub_entry_count[idx]);
        }
        SYS_ACL_UNLOCK(lchip);

        SYS_ACL_DBG_DUMP("\n");
    }
    for (idx = 0; idx < CTC_FPA_KEY_SIZE_NUM; idx++)
    {
        SYS_ACL_LOCK(lchip);
        if(0 == p_gg_acl_master[lchip]->ipv4_160bit_mode_en)
        {
            SYS_ACL_DBG_DUMP("%-17s", acl_type[idx + CTC_FPA_KEY_SIZE_NUM]);
        }
        else
        {
            SYS_ACL_DBG_DUMP("%-17s", acl_type1[idx + CTC_FPA_KEY_SIZE_NUM]);
        }
        block_id = ACL_IGS_BLOCK_ID_NUM;
        SYS_ACL_DBG_DUMP("%5d of %-6d",
                             (p_gg_acl_master[lchip]->block[block_id].fpab.sub_entry_count[idx] - p_gg_acl_master[lchip]->block[block_id].fpab.sub_free_count[idx]),
                             p_gg_acl_master[lchip]->block[block_id].fpab.sub_entry_count[idx]);
        SYS_ACL_UNLOCK(lchip);

        SYS_ACL_DBG_DUMP("\n");
    }
    for (idx = 0; idx <= CTC_FPA_KEY_SIZE_320; idx++)
    {
        SYS_ACL_LOCK(lchip);
        if(0 == p_gg_acl_master[lchip]->ipv4_160bit_mode_en)
        {
            SYS_ACL_DBG_DUMP("%-17s", acl_type[idx + 2*CTC_FPA_KEY_SIZE_NUM]);
        }
        else
        {
            SYS_ACL_DBG_DUMP("%-17s", acl_type1[idx + 2*CTC_FPA_KEY_SIZE_NUM]);
        }
        block_id = ACL_IGS_BLOCK_ID_NUM + ACL_EGS_BLOCK_ID_NUM;
        SYS_ACL_DBG_DUMP("%5d of %-6d",
                             (p_gg_acl_master[lchip]->block[block_id].fpab.sub_entry_count[idx] - p_gg_acl_master[lchip]->block[block_id].fpab.sub_free_count[idx]),
                             p_gg_acl_master[lchip]->block[block_id].fpab.sub_entry_count[idx]);
        SYS_ACL_UNLOCK(lchip);

        SYS_ACL_DBG_DUMP("\n");
    }


    SYS_ACL_DBG_DUMP("\n");
    SYS_ACL_DBG_DUMP("#2 Tcam Entry Status :\n");
    SYS_ACL_DBG_DUMP("--------------------------------------------------------------------------------\n");
    for (block_id = 0; block_id < ACL_BLOCK_ID_NUM; block_id++)
    {
        SYS_ACL_LOCK(lchip);
        pb = &p_gg_acl_master[lchip]->block[block_id];
        step = 1;
        for (idx = 0; idx < CTC_FPA_KEY_SIZE_NUM; idx++)
        {
            for (block_idx = pb->fpab.start_offset[idx]; block_idx < pb->fpab.start_offset[idx]+pb->fpab.sub_entry_count[idx]*step; block_idx = block_idx + step)
            {
                pe = pb->fpab.entries[block_idx];
                if (pe)
                {
                    switch (ACL_OUTER_ENTRY(pe)->key.type)
                    {
                    case CTC_ACL_KEY_MAC:
                        mac_cnt++;
                        break;

                    case CTC_ACL_KEY_IPV4:
                        ipv4_cnt++;
                        break;

                    case CTC_ACL_KEY_IPV6:
                        ipv6_cnt++;
                        break;

                    case CTC_ACL_KEY_PBR_IPV4:
                        pbr_ipv4_cnt++;
                        break;

                    case CTC_ACL_KEY_PBR_IPV6:
                        pbr_ipv6_cnt++;
                        break;

                    default:
                        break;
                    }
                }
            }
            step = step * 2;
        }
        SYS_ACL_UNLOCK(lchip);
    }


    SYS_ACL_DBG_DUMP("%-9s entries: %u \n", "Mac", mac_cnt);
    SYS_ACL_DBG_DUMP("%-9s entries: %u \n", "Ipv4", ipv4_cnt);
    SYS_ACL_DBG_DUMP("%-9s entries: %u \n", "Ipv6", ipv6_cnt);
    SYS_ACL_DBG_DUMP("%-9s entries: %u \n", "pbr_Ipv4", pbr_ipv4_cnt);
    SYS_ACL_DBG_DUMP("%-9s entries: %u \n", "pbr_Ipv6", pbr_ipv6_cnt);

    SYS_ACL_DBG_DUMP("\n");
    SYS_ACL_DBG_DUMP("#3 Hash Entry Status :\n");
    SYS_ACL_DBG_DUMP("--------------------------------------------------------------------------------\n");

    SYS_ACL_LOCK(lchip);
    _sys_goldengate_acl_get_group_by_gid(lchip, CTC_ACL_GROUP_ID_HASH_MAC, &pg);
    hash_mac_cnt = pg->entry_count;

    _sys_goldengate_acl_get_group_by_gid(lchip, CTC_ACL_GROUP_ID_HASH_IPV4, &pg);
    hash_ipv4_cnt = pg->entry_count;

    _sys_goldengate_acl_get_group_by_gid(lchip, CTC_ACL_GROUP_ID_HASH_IPV6, &pg);
    hash_ipv6_cnt = pg->entry_count;

    _sys_goldengate_acl_get_group_by_gid(lchip, CTC_ACL_GROUP_ID_HASH_MPLS, &pg);
    hash_mpls_cnt = pg->entry_count;

    _sys_goldengate_acl_get_group_by_gid(lchip, CTC_ACL_GROUP_ID_HASH_L2_L3, &pg);
    hash_l2_l3_cnt = pg->entry_count;
    SYS_ACL_UNLOCK(lchip);

    SYS_ACL_DBG_DUMP("%-9s entries: %u \n", "Hash-Mac", hash_mac_cnt);
    SYS_ACL_DBG_DUMP("%-9s entries: %u \n", "Hash-Ipv4", hash_ipv4_cnt);
    SYS_ACL_DBG_DUMP("%-9s entries: %u \n", "Hash-Ipv6", hash_ipv6_cnt);
    SYS_ACL_DBG_DUMP("%-9s entries: %u \n", "Hash-MPLS", hash_mpls_cnt);
    SYS_ACL_DBG_DUMP("%-9s entries: %u \n", "Hash-L2L3", hash_l2_l3_cnt);

    return CTC_E_NONE;
}

/** show acl entry **
 * type = 0 :by all
 * type = 1 :by entry
 * type = 2 :by group
 * type = 3 :by priority
 */
int32
sys_goldengate_acl_show_entry(uint8 lchip, uint8 type, uint32 param, ctc_acl_key_type_t key_type, uint8 detail)
{
    SYS_ACL_INIT_CHECK(lchip);
    /* CTC_ACL_KEY_NUM represents all type*/
    CTC_MAX_VALUE_CHECK(type, CTC_ACL_KEY_NUM);

    SYS_ACL_DBG_DUMP("%-8s%-12s%-12s%-4s%-12s%-8s%-14s%-8s%s\n", "No.", "ENTRY_ID", "GROUP_ID", "HW", "E_PRI", "G_PRI", "TYPE", "INDEX","ACTION");
    SYS_ACL_DBG_DUMP("------------------------------------------------------------------------------------\n");

    switch (type)
    {
    case 0:
        CTC_ERROR_RETURN(_sys_goldengate_acl_show_entry_all(lchip, param, key_type, 0));
        SYS_ACL_DBG_DUMP("\n");
        break;

    case 1:
        CTC_ERROR_RETURN(_sys_goldengate_acl_show_entry_by_entry_id(lchip, param, key_type, detail));
        SYS_ACL_DBG_DUMP("\n");
        break;

    case 2:
        SYS_ACL_LOCK(lchip);
        CTC_ERROR_RETURN_ACL_UNLOCK(_sys_goldengate_acl_show_entry_by_group_id(lchip, param, key_type, 0));
        SYS_ACL_UNLOCK(lchip);
        SYS_ACL_DBG_DUMP("\n");
        break;

    case 3:
        CTC_ERROR_RETURN(_sys_goldengate_acl_show_entry_by_priority(lchip, param, key_type, 0));
        SYS_ACL_DBG_DUMP("\n");
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}


/** show acl entry **
 * type = 0 :by all
 */
int32
sys_goldengate_acl_show_group(uint8 lchip, uint8 type)
{
    _acl_cb_para_t para;
    SYS_ACL_INIT_CHECK(lchip);
    SYS_ACL_DBG_DUMP("%-6s%-12s%-6s%-6s%-14s%-10s%s\n", "No.", "GROUP_ID", "DIR", "PRI", "TYPE", "ENTRY_NUM", "KEY");
    SYS_ACL_DBG_DUMP("-----------------------------------------------------------------------------------------\n");

    sal_memset(&para, 0, sizeof(_acl_cb_para_t));
    para.count = 0;

    SYS_ACL_LOCK(lchip);
    ctc_hash_traverse_through(p_gg_acl_master[lchip]->group,
                              (hash_traversal_fn) _sys_goldengate_acl_hash_traverse_cb_show_group, &para);
    SYS_ACL_UNLOCK(lchip);

    SYS_ACL_DBG_DUMP("Total group count :%u \n", para.count);

        return CTC_E_NONE;
}

int32
sys_goldengate_acl_show_tcam_alloc_status(uint8 lchip, uint8 fpa_block_id)
{
    char* dir_str[3] = {"Acl Ingress", "Acl Egress", "Pbr"};
    char* key_str[3] = {"160 bit", "320 bit", "640 bit"};
    sys_acl_block_t *pb = NULL;
    uint8 idx = 0;
    uint8 step = 1;
    uint8 dir = CTC_INGRESS;
    uint8 block_id = fpa_block_id;

    SYS_ACL_INIT_CHECK(lchip);

    CTC_MAX_VALUE_CHECK(fpa_block_id, ACL_BLOCK_ID_NUM - 1);

    SYS_ACL_LOCK(lchip);
    pb = &(p_gg_acl_master[lchip]->block[fpa_block_id]);
    if(ACL_IGS_BLOCK_ID_NUM == fpa_block_id)
    {
        dir = CTC_EGRESS;
        block_id = 0;
    }
    if((ACL_BLOCK_ID_NUM - 1) == fpa_block_id)
    {
        block_id = 0;
        dir = 2;
    }

    SYS_ACL_DBG_DUMP("\n%s %d\n", dir_str[dir], block_id);
    SYS_ACL_DBG_DUMP("(unit 160bit)  entry count: %d, used count: %d", pb->fpab.entry_count, pb->fpab.entry_count - pb->fpab.free_count);
    SYS_ACL_DBG_DUMP("\n------------------------------------------------------------------\n");
    SYS_ACL_DBG_DUMP("%-12s%-15s%-15s%-15s%-15s\n", "key size", "range", "entry count", "used count", "rsv count");

    for(idx = 0; idx < CTC_FPA_KEY_SIZE_NUM; idx++)
    {
        if(pb->fpab.sub_entry_count[idx] > 0)
        {
            SYS_ACL_DBG_DUMP("%-12s[%4d,%4d]    %-15u%-15u%-15u\n", key_str[idx], pb->fpab.start_offset[idx],
                                            pb->fpab.start_offset[idx] + pb->fpab.sub_entry_count[idx] * step - 1,
                                            pb->fpab.sub_entry_count[idx], pb->fpab.sub_entry_count[idx] - pb->fpab.sub_free_count[idx],
                                            pb->fpab.sub_rsv_count[idx]);
        }
        step = step * 2;
    }
    SYS_ACL_UNLOCK(lchip);

    SYS_ACL_DBG_DUMP("------------------------------------------------------------------\n");

    return CTC_E_NONE;
}


#define ___________ACL_OUTER_FUNCTION________________________

#define _1_ACL_GROUP_

/*
 * create an acl group.
 * For init api, group_info can be NULL.
 * For out using, group_info must not be NULL.
 *
 */
int32
sys_goldengate_acl_create_group(uint8 lchip, uint32 group_id, ctc_acl_group_info_t* group_info)
{
    sys_acl_group_t* pg     = NULL;
    int32          ret;

    SYS_ACL_INIT_CHECK(lchip);
    SYS_ACL_CHECK_GROUP_ID(group_id);
    SYS_ACL_CHECK_RSV_GROUP_ID(group_id); /* reserve group cannot be created by user */
    /* check if priority bigger than biggest priority */

    SYS_ACL_DBG_FUNC();
    SYS_ACL_DBG_PARAM("  %% PARAM: group_id: %u \n", group_id);

    /*
     *  group_id is uint32.
     *  #1 check block_num from p_p_gg_acl_master[lchip]. precedence cannot bigger than block_num.
     *  #2 malloc a sys_acl_group_t, add to hash based on group_id.
     */

    /* check if group exist */
    SYS_ACL_LOCK(lchip);
    _sys_goldengate_acl_get_group_by_gid(lchip, group_id, &pg);
    if (pg)
    {
        SYS_ACL_UNLOCK(lchip);
        return CTC_E_ACL_GROUP_EXIST;
    }

    CTC_ERROR_GOTO(_sys_goldengate_acl_create_group(lchip, group_id, group_info), ret, cleanup);

    SYS_ACL_UNLOCK(lchip);

    return CTC_E_NONE;

 cleanup:
    SYS_ACL_UNLOCK(lchip);
    return ret;
}

/*
 * destroy an empty group.
 */
int32
sys_goldengate_acl_destroy_group(uint8 lchip, uint32 group_id)
{
    sys_acl_group_t* pg = NULL;

    SYS_ACL_INIT_CHECK(lchip);
    SYS_ACL_CHECK_GROUP_ID(group_id);
    SYS_ACL_CHECK_RSV_GROUP_ID(group_id); /* reserve group cannot be destroyed */

    SYS_ACL_DBG_FUNC();
    SYS_ACL_DBG_PARAM("  %% PARAM: group_id: %u \n", group_id);

    /*
     * #1 check if entry all removed.
     * #2 remove from hash. free sys_acl_group_t.
     */

    /* check if group exist */
    SYS_ACL_LOCK(lchip);
    _sys_goldengate_acl_get_group_by_gid(lchip, group_id, &pg);
    SYS_ACL_CHECK_GROUP_UNEXIST(pg);

    /* check if all entry removed */
    if (0 != pg->entry_list->count)
    {
        SYS_ACL_UNLOCK(lchip);
        return CTC_E_ACL_GROUP_NOT_EMPTY;
    }

    ctc_hash_remove(p_gg_acl_master[lchip]->group, pg);
    SYS_ACL_UNLOCK(lchip);

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
sys_goldengate_acl_install_group(uint8 lchip, uint32 group_id, ctc_acl_group_info_t* group_info)
{
    sys_acl_group_t       * pg        = NULL;
    uint8                 install_all = 0;
    struct ctc_slistnode_s* pe;

    uint32                eid = 0;

    SYS_ACL_INIT_CHECK(lchip);
    SYS_ACL_CHECK_GROUP_ID(group_id);

    SYS_ACL_DBG_FUNC();
    SYS_ACL_DBG_PARAM("  %% PARAM: gid %u  \n", group_id);

    /* get group node */
    SYS_ACL_LOCK(lchip);
    _sys_goldengate_acl_get_group_by_gid(lchip, group_id, &pg);
    SYS_ACL_CHECK_GROUP_UNEXIST(pg);


    if (group_info) /* group_info != NULL*/
    {
        bool is_new;

        /* if group_info is new, rewrite all entries */
        is_new = _sys_goldengate_acl_is_group_info_new(lchip, &pg->group_info, group_info);
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
        CTC_ERROR_RETURN_ACL_UNLOCK(_sys_goldengate_acl_check_group_info(lchip, group_info, pg->group_info.type, 0, pg->group_info.bitmap_status));

        CTC_ERROR_RETURN_ACL_UNLOCK(_sys_goldengate_acl_map_group_info(lchip, &pg->group_info, group_info, 0));

        CTC_SLIST_LOOP(pg->entry_list, pe)
        {
            eid = ((sys_acl_entry_t *) pe)->fpae.entry_id;
            _sys_goldengate_acl_install_entry(lchip, eid, SYS_ACL_ENTRY_OP_FLAG_ADD, 0);
        }
    }
    else
    {   /* traverse, stop at first installed entry.*/
        if (pg->entry_list)
        {
            for (pe = pg->entry_list->head;
                 pe && (((sys_acl_entry_t *) pe)->fpae.flag != FPA_ENTRY_FLAG_INSTALLED);
                 pe = pe->next)
            {
                eid = ((sys_acl_entry_t *) pe)->fpae.entry_id;
                _sys_goldengate_acl_install_entry(lchip, eid, SYS_ACL_ENTRY_OP_FLAG_ADD, 0);
            }
        }
    }
    SYS_ACL_UNLOCK(lchip);

    return CTC_E_NONE;
}

/*
 * uninstall a group (empty or NOT) from hardware table
 */
int32
sys_goldengate_acl_uninstall_group(uint8 lchip, uint32 group_id)
{
    sys_acl_group_t       * pg = NULL;
    struct ctc_slistnode_s* pe = NULL;

    uint32                eid = 0;

    SYS_ACL_INIT_CHECK(lchip);
    SYS_ACL_CHECK_GROUP_ID(group_id);

    SYS_ACL_DBG_FUNC();
    SYS_ACL_DBG_PARAM("  %% PARAM: gid %u \n", group_id);

    /* get group node */
    SYS_ACL_LOCK(lchip);
    _sys_goldengate_acl_get_group_by_gid(lchip, group_id, &pg);
    SYS_ACL_CHECK_GROUP_UNEXIST(pg);

    CTC_SLIST_LOOP(pg->entry_list, pe)
    {
        eid = ((sys_acl_entry_t *) pe)->fpae.entry_id;
        CTC_ERROR_RETURN_ACL_UNLOCK(_sys_goldengate_acl_install_entry(lchip, eid, SYS_ACL_ENTRY_OP_FLAG_DELETE, 0));
    }
    SYS_ACL_UNLOCK(lchip);

    return CTC_E_NONE;
}

/*
 * get group info by group id. NOT For hash group.
 */
int32
sys_goldengate_acl_get_group_info(uint8 lchip, uint32 group_id, ctc_acl_group_info_t* group_info)
{
    sys_acl_group_info_t* pinfo = NULL;  /* sys group info */
    sys_acl_group_t     * pg    = NULL;

    SYS_ACL_INIT_CHECK(lchip);
    SYS_ACL_CHECK_GROUP_ID(group_id);
    SYS_ACL_CHECK_RSV_GROUP_ID(group_id); /* rsv group has no group_info */

    CTC_PTR_VALID_CHECK(group_info);

    SYS_ACL_DBG_FUNC();
    SYS_ACL_DBG_PARAM("  %% PARAM: gid: %u \n", group_id);

    SYS_ACL_LOCK(lchip);
    _sys_goldengate_acl_get_group_by_gid(lchip, group_id, &pg);
    SYS_ACL_CHECK_GROUP_UNEXIST(pg);

    pinfo = &(pg->group_info);

    /* get ctc group info based on pinfo (sys group info) */
    CTC_ERROR_RETURN_ACL_UNLOCK(_sys_goldengate_acl_unmap_group_info(lchip, group_info, pinfo));
    SYS_ACL_UNLOCK(lchip);

    return CTC_E_NONE;
}

#define _2_ACL_ENTRY_

/*
 * install entry to hardware table
 */
int32
sys_goldengate_acl_install_entry(uint8 lchip, uint32 eid)
{
    SYS_ACL_INIT_CHECK(lchip);
    SYS_ACL_CHECK_ENTRY_ID(eid);

    SYS_ACL_DBG_FUNC();
    SYS_ACL_DBG_PARAM("  %% PARAM: eid %u \n", eid);

    SYS_ACL_LOCK(lchip);
    CTC_ERROR_RETURN_ACL_UNLOCK(_sys_goldengate_acl_install_entry(lchip, eid, SYS_ACL_ENTRY_OP_FLAG_ADD, 1));
    SYS_ACL_UNLOCK(lchip);

    return CTC_E_NONE;
}

/*
 * uninstall entry from hardware table
 */
int32
sys_goldengate_acl_uninstall_entry(uint8 lchip, uint32 eid)
{
    SYS_ACL_INIT_CHECK(lchip);
    SYS_ACL_CHECK_ENTRY_ID(eid);

    SYS_ACL_DBG_FUNC();
    SYS_ACL_DBG_PARAM("  %% PARAM: eid %u \n", eid);

    SYS_ACL_LOCK(lchip);
    CTC_ERROR_RETURN_ACL_UNLOCK(_sys_goldengate_acl_install_entry(lchip, eid, SYS_ACL_ENTRY_OP_FLAG_DELETE, 1));
    SYS_ACL_UNLOCK(lchip);

    return CTC_E_NONE;
}

/*
 * check entry type if it is valid.
 */
STATIC int32
_sys_goldengate_acl_check_entry_type(uint8 lchip, uint32 group_id, uint8 group_type, uint8 entry_type)
{
    switch (entry_type)
    {
    case CTC_ACL_KEY_PBR_IPV4:
    case CTC_ACL_KEY_PBR_IPV6:
        if ((CTC_ACL_GROUP_TYPE_PBR_CLASS != group_type) && (CTC_ACL_GROUP_TYPE_NONE != group_type))
        {
            return CTC_E_ACLQOS_INVALID_KEY_TYPE;
        }
        break;

    case CTC_ACL_KEY_MPLS:
    case CTC_ACL_KEY_MAC:
    case CTC_ACL_KEY_IPV4:
    case CTC_ACL_KEY_IPV6:
        if ((CTC_ACL_GROUP_TYPE_PBR_CLASS == group_type) || (CTC_ACL_GROUP_TYPE_HASH == group_type))
        {
            return CTC_E_ACLQOS_INVALID_KEY_TYPE;
        }
        break;

    case CTC_ACL_KEY_HASH_MAC:
        CTC_EQUAL_CHECK(group_id, CTC_ACL_GROUP_ID_HASH_MAC);
        break;

    case CTC_ACL_KEY_HASH_IPV4:
        CTC_EQUAL_CHECK(group_id, CTC_ACL_GROUP_ID_HASH_IPV4);
        break;

    case CTC_ACL_KEY_HASH_IPV6:
        CTC_EQUAL_CHECK(group_id, CTC_ACL_GROUP_ID_HASH_IPV6);
        break;

    case CTC_ACL_KEY_HASH_MPLS:
        CTC_EQUAL_CHECK(group_id, CTC_ACL_GROUP_ID_HASH_MPLS);
        break;

    case CTC_ACL_KEY_HASH_L2_L3:
        CTC_EQUAL_CHECK(group_id, CTC_ACL_GROUP_ID_HASH_L2_L3);
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_acl_check_metadata(uint8 lchip, sys_acl_group_t * pg, ctc_acl_entry_t* acl_entry, uint8 entry_type)
{
    bool b_use_metadata = FALSE;
    CTC_PTR_VALID_CHECK(pg);
    CTC_PTR_VALID_CHECK(acl_entry);

    switch (entry_type)
    {
        case CTC_ACL_KEY_MAC:
            if(CTC_FLAG_ISSET(acl_entry->key.u.mac_key.flag,CTC_ACL_MAC_KEY_FLAG_METADATA))
            {
                b_use_metadata = TRUE;
            }
            break;

        case CTC_ACL_KEY_MPLS:
            if(CTC_FLAG_ISSET(acl_entry->key.u.mpls_key.flag,CTC_ACL_MPLS_KEY_FLAG_METADATA))
            {
                b_use_metadata = TRUE;
            }
            break;

        case CTC_ACL_KEY_IPV4:
            if(CTC_FLAG_ISSET(acl_entry->key.u.ipv4_key.flag,CTC_ACL_IPV4_KEY_FLAG_METADATA))
            {
                b_use_metadata = TRUE;
            }
            break;

        case CTC_ACL_KEY_IPV6:
            if(CTC_FLAG_ISSET(acl_entry->key.u.ipv6_key.flag,CTC_ACL_IPV6_KEY_FLAG_METADATA))
            {
                b_use_metadata = TRUE;
            }
            break;

        case CTC_ACL_KEY_HASH_MAC:
        case CTC_ACL_KEY_HASH_IPV4:
        case CTC_ACL_KEY_HASH_L2_L3:
        case CTC_ACL_KEY_HASH_MPLS:
        case CTC_ACL_KEY_HASH_IPV6:
        case CTC_ACL_KEY_PBR_IPV4:
        case CTC_ACL_KEY_PBR_IPV6:
            return CTC_E_NONE;

        default:
            return CTC_E_NONE;
    }

    if(b_use_metadata)
    {
        if((pg->group_info.type == CTC_ACL_GROUP_TYPE_GLOBAL)
            ||(pg->group_info.type == CTC_ACL_GROUP_TYPE_PORT_BITMAP)
            ||(pg->group_info.type == CTC_ACL_GROUP_TYPE_PORT_CLASS)
            ||(pg->group_info.type == CTC_ACL_GROUP_TYPE_PORT)
            ||(pg->group_info.type == CTC_ACL_GROUP_TYPE_NONE))
        {
            return CTC_E_NONE;
        }
        else
        {
            return CTC_E_INVALID_PARAM;
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_acl_check_port_field_type(uint8 lchip, sys_acl_group_t * pg, ctc_acl_entry_t* acl_entry, uint8 entry_type)
{
    uint32  cmd                  = 0;
    uint32  ingress_vlan_acl_num = 0;
    uint32  egress_vlan_acl_num  = 0;
    uint32  egress_per_port_sel_en = 0;
    CTC_PTR_VALID_CHECK(pg);
    CTC_PTR_VALID_CHECK(acl_entry);

    if (CTC_ACL_GROUP_TYPE_NONE != pg->group_info.type)
    {
        return CTC_E_NONE;
    }

    if (((CTC_ACL_KEY_PBR_IPV4 == entry_type) && (CTC_FIELD_PORT_TYPE_PBR_CLASS != acl_entry->key.u.pbr_ipv4_key.port.type)
        && (CTC_FIELD_PORT_TYPE_NONE != acl_entry->key.u.pbr_ipv4_key.port.type))
        || ((CTC_ACL_KEY_PBR_IPV6 == entry_type) && (CTC_FIELD_PORT_TYPE_PBR_CLASS != acl_entry->key.u.pbr_ipv6_key.port.type)
        && (CTC_FIELD_PORT_TYPE_NONE != acl_entry->key.u.pbr_ipv6_key.port.type)))
    {
        return CTC_E_ACLQOS_INVALID_KEY_TYPE;
    }

    if (((CTC_ACL_KEY_MAC == entry_type) && (CTC_FIELD_PORT_TYPE_PBR_CLASS == acl_entry->key.u.mac_key.port.type))
        || ((CTC_ACL_KEY_MPLS == entry_type) && (CTC_FIELD_PORT_TYPE_PBR_CLASS == acl_entry->key.u.mpls_key.port.type))
        || ((CTC_ACL_KEY_IPV4 == entry_type) && (CTC_FIELD_PORT_TYPE_PBR_CLASS == acl_entry->key.u.ipv4_key.port.type))
        || ((CTC_ACL_KEY_IPV6 == entry_type) && (CTC_FIELD_PORT_TYPE_PBR_CLASS == acl_entry->key.u.ipv6_key.port.type)))
    {
        return CTC_E_ACLQOS_INVALID_KEY_TYPE;
    }

    if (((CTC_ACL_KEY_MAC == entry_type) && (CTC_FIELD_PORT_TYPE_VLAN_CLASS == acl_entry->key.u.mac_key.port.type))
        || ((CTC_ACL_KEY_MPLS == entry_type) && (CTC_FIELD_PORT_TYPE_VLAN_CLASS == acl_entry->key.u.mpls_key.port.type))
        || ((CTC_ACL_KEY_IPV4 == entry_type) && (CTC_FIELD_PORT_TYPE_VLAN_CLASS == acl_entry->key.u.ipv4_key.port.type))
        || ((CTC_ACL_KEY_IPV6 == entry_type) && (CTC_FIELD_PORT_TYPE_VLAN_CLASS == acl_entry->key.u.ipv6_key.port.type)))
    {
        if(CTC_INGRESS == pg->group_info.dir)
        {
            /* ipe */
            cmd = DRV_IOR(IpeAclLookupCtl_t, IpeAclLookupCtl_l3AclNum_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ingress_vlan_acl_num));
            if(3 == ingress_vlan_acl_num)
            {
                if(0 == pg->group_info.block_id)
                {
                    return CTC_E_FEATURE_NOT_SUPPORT;
                }
            }
            else if(2 == ingress_vlan_acl_num)
            {
                if((0 == pg->group_info.block_id)||(1 == pg->group_info.block_id))
                {
                    return CTC_E_FEATURE_NOT_SUPPORT;
                }
            }
            else if(1 == ingress_vlan_acl_num)
            {
                if((0 == pg->group_info.block_id)||(1 == pg->group_info.block_id)||(2 == pg->group_info.block_id))
                {
                    return CTC_E_FEATURE_NOT_SUPPORT;
                }
            }
            else if(0 == ingress_vlan_acl_num)
            {
                return CTC_E_FEATURE_NOT_SUPPORT;
            }

        }
        else if(CTC_EGRESS == pg->group_info.dir)
        {
            /* epe */
            cmd = DRV_IOR(EpeAclQosCtl_t, EpeAclQosCtl_l3AclNum_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &egress_vlan_acl_num));
            cmd = DRV_IOR(EpeAclQosCtl_t, EpeAclQosCtl_perPortSelectEn_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &egress_per_port_sel_en));
            if(0 == egress_vlan_acl_num && (!egress_per_port_sel_en))
            {
                return CTC_E_FEATURE_NOT_SUPPORT;
            }
        }
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_acl_add_entry(uint8 lchip, uint32 group_id, ctc_acl_entry_t* acl_entry)
{
    sys_acl_group_t     * pg      = NULL;
    sys_acl_entry_t     * pe_lkup = NULL;

    sys_acl_entry_t     e_tmp;
    sys_acl_action_t    a_tmp;
    sys_acl_vlan_edit_t v_tmp;
    int32               ret = CTC_E_NONE;
    uint32              tcam_count = 0;
    uint8               step = 0;

    SYS_ACL_INIT_CHECK(lchip);
    SYS_ACL_CHECK_GROUP_ID(group_id);
    CTC_PTR_VALID_CHECK(acl_entry);
    SYS_ACL_CHECK_ENTRY_ID(acl_entry->entry_id);

    SYS_ACL_DBG_FUNC();
    SYS_ACL_DBG_PARAM("  %% PARAM: eid %u -- key_type %u\n",
                      acl_entry->entry_id, acl_entry->key.type);


    /* check sys group */
    SYS_ACL_LOCK(lchip);
    _sys_goldengate_acl_get_group_by_gid(lchip, group_id, &pg);
    SYS_ACL_CHECK_GROUP_UNEXIST(pg);
    CTC_ERROR_RETURN_ACL_UNLOCK(_sys_goldengate_acl_check_entry_type(lchip, group_id, pg->group_info.type, acl_entry->key.type));

    /* check metadata */
    CTC_ERROR_RETURN_ACL_UNLOCK(_sys_goldengate_acl_check_metadata(lchip, pg, acl_entry, acl_entry->key.type));

    /* check vlan class entry*/
    CTC_ERROR_RETURN_ACL_UNLOCK(_sys_goldengate_acl_check_port_field_type(lchip, pg, acl_entry, acl_entry->key.type));

    /* check sys entry */
    _sys_goldengate_acl_get_nodes_by_eid(lchip, acl_entry->entry_id, &pe_lkup, NULL, NULL);
    if (pe_lkup)
    {
        SYS_ACL_UNLOCK(lchip);
        return CTC_E_ACL_ENTRY_EXIST;
    }

    sal_memset(&e_tmp, 0, sizeof(e_tmp));
    sal_memset(&a_tmp, 0, sizeof(a_tmp));
    sal_memset(&v_tmp, 0, sizeof(v_tmp));
    a_tmp.lchip = lchip;
    e_tmp.lchip = lchip;
    v_tmp.lchip = lchip;

     e_tmp.group = pg;

    /* build sys entry action, key*/
    CTC_ERROR_RETURN_ACL_UNLOCK(_sys_goldengate_acl_map_key(lchip, &acl_entry->key, &e_tmp.key, pg->group_info.dir));

    if ((acl_entry->key.type == CTC_ACL_KEY_PBR_IPV4) || (acl_entry->key.type == CTC_ACL_KEY_PBR_IPV6))
    {
        CTC_ERROR_RETURN_ACL_UNLOCK(_sys_goldengate_acl_check_pbr_action(lchip, acl_entry->action.flag));
        CTC_ERROR_RETURN_ACL_UNLOCK(_sys_goldengate_acl_map_pbr_action(lchip, &acl_entry->action, &a_tmp));
    }
    else
    {
        CTC_ERROR_RETURN_ACL_UNLOCK(_sys_goldengate_acl_check_action(lchip, pg->group_info.dir, acl_entry->action.flag));
        CTC_ERROR_RETURN_ACL_UNLOCK(_sys_goldengate_acl_map_action(lchip, &acl_entry->action, &a_tmp, &v_tmp));
    }


    /* build sys entry inner field */
    e_tmp.fpae.entry_id = acl_entry->entry_id;
    e_tmp.head.next     = NULL;
    if (acl_entry->priority_valid)
    {
        e_tmp.fpae.priority = acl_entry->priority; /* meaningless for HASH */
    }
    else
    {
        e_tmp.fpae.priority = FPA_PRIORITY_DEFAULT; /* meaningless for HASH */
    }

    /* set lchip --> key and action */
    _sys_goldengate_acl_get_fpa_size(lchip, &e_tmp, &step);

    if (!ACL_ENTRY_IS_TCAM(acl_entry->key.type)) /* hash entry */
    {
        /* hash check +1*/
        if (!SYS_ACL_IS_RSV_GROUP(pg->group_id)) /* hash entry only in hash group */
        {
            SYS_ACL_UNLOCK(lchip);
            return CTC_E_ACL_ADD_HASH_ENTRY_TO_WRONG_GROUP;
        }

        if(p_gg_acl_master[lchip]->key_count[ACL_ALLOC_COUNT_HASH] + step > SYS_FTM_SPEC(lchip, CTC_FTM_SPEC_ACL_FLOW))
        {
            SYS_ACL_UNLOCK(lchip);
            return CTC_E_NO_RESOURCE;
        }

        /* hash check +2. */
        _sys_goldengate_acl_get_nodes_by_key(lchip, &e_tmp, &pe_lkup, NULL);
        if (pe_lkup) /* hash's key must be unique */
        {
            SYS_ACL_UNLOCK(lchip);
            return CTC_E_ACL_ENTRY_EXIST;
        }
    }
    else /* tcam entry */
    {
        /* tcam check +1 */
        if (SYS_ACL_IS_RSV_GROUP(pg->group_id)) /* tcam entry only in tcam group */
        {
            SYS_ACL_UNLOCK(lchip);
            return CTC_E_ACL_ADD_TCAM_ENTRY_TO_WRONG_GROUP;
        }

        tcam_count = (p_gg_acl_master[lchip]->key_count[ACL_ALLOC_COUNT_TCAM_160])
                    + (p_gg_acl_master[lchip]->key_count[ACL_ALLOC_COUNT_TCAM_320]*2)
                    + (p_gg_acl_master[lchip]->key_count[ACL_ALLOC_COUNT_TCAM_640]*4);

        if(tcam_count + step > SYS_FTM_SPEC(lchip, CTC_FTM_SPEC_ACL))
        {
            SYS_ACL_UNLOCK(lchip);
            return CTC_E_NO_RESOURCE;
        }
    }

    if (ACL_ENTRY_IS_TCAM(e_tmp.key.type))
    {
        CTC_ERROR_GOTO(_sys_goldengate_acl_add_tcam_entry(lchip, pg, &e_tmp, &a_tmp, &v_tmp), ret, cleanup);
    }
    else
    {
        CTC_ERROR_GOTO(_sys_goldengate_acl_add_hash_entry(lchip, pg, &e_tmp, &a_tmp, &v_tmp), ret, cleanup);
    }

    if (CTC_ACL_KEY_IPV6 != acl_entry->key.type)
    {
        pg->group_info.bitmap_status = ACL_BITMAP_STATUS_16;
    }
    SYS_ACL_UNLOCK(lchip);

    return CTC_E_NONE;

 cleanup:
    _sys_goldengate_acl_remove_accessory_property(lchip, &e_tmp, &a_tmp);
    SYS_ACL_UNLOCK(lchip);
    return ret;
}

/*
 * remove entry from software table
 */
int32
_sys_goldengate_acl_remove_entry(uint8 lchip, uint32 entry_id)
{
    sys_acl_group_t* pg = NULL;
    sys_acl_entry_t* pe = NULL;
    sys_acl_block_t* pb = NULL;

    /* check raw entry */
    _sys_goldengate_acl_get_nodes_by_eid(lchip, entry_id, &pe, &pg, &pb);
    if(!pe)
    {
        return CTC_E_ACL_ENTRY_UNEXIST;
    }
    if(!pg)
    {
        return CTC_E_ACL_GROUP_UNEXIST;
    }
    if (FPA_ENTRY_FLAG_INSTALLED == pe->fpae.flag)
    {
        return CTC_E_ACL_ENTRY_INSTALLED;
    }

    if (ACL_ENTRY_IS_TCAM(pe->key.type))
    {
        CTC_ERROR_RETURN(_sys_goldengate_acl_remove_tcam_entry(lchip, pe, pg, pb));
    }
    else
    {
        CTC_ERROR_RETURN(_sys_goldengate_acl_remove_hash_entry(lchip, pe, pg, pb));
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_acl_remove_entry(uint8 lchip, uint32 entry_id)
{
    SYS_ACL_INIT_CHECK(lchip);
    SYS_ACL_CHECK_ENTRY_ID(entry_id);

    SYS_ACL_DBG_FUNC();
    SYS_ACL_DBG_PARAM("  %% PARAM: eid %u \n", entry_id);

    SYS_ACL_LOCK(lchip);
    CTC_ERROR_RETURN_ACL_UNLOCK(_sys_goldengate_acl_remove_entry(lchip, entry_id));
    SYS_ACL_UNLOCK(lchip);

    return CTC_E_NONE;
}

/*
 * remove all entries from a group
 */
int32
sys_goldengate_acl_remove_all_entry(uint8 lchip, uint32 group_id)
{
    sys_acl_group_t       * pg      = NULL;
    struct ctc_slistnode_s* pe      = NULL;
    struct ctc_slistnode_s* pe_next = NULL;
    uint32                eid       = 0;

    SYS_ACL_INIT_CHECK(lchip);
    SYS_ACL_CHECK_GROUP_ID(group_id);

    SYS_ACL_DBG_FUNC();
    SYS_ACL_DBG_PARAM("  %% PARAM: gid %u \n", group_id);

    /* get group node */
    SYS_ACL_LOCK(lchip);
    _sys_goldengate_acl_get_group_by_gid(lchip, group_id, &pg);
    SYS_ACL_CHECK_GROUP_UNEXIST(pg);

    /* check if all uninstalled */
    CTC_SLIST_LOOP(pg->entry_list, pe)
    {
        if (FPA_ENTRY_FLAG_INSTALLED ==
            ((sys_acl_entry_t *) pe)->fpae.flag)
        {
            SYS_ACL_UNLOCK(lchip);
            return CTC_E_ACL_ENTRY_INSTALLED;
        }
    }

    CTC_SLIST_LOOP_DEL(pg->entry_list, pe, pe_next)
    {
        eid = ((sys_acl_entry_t *) pe)->fpae.entry_id;
        /* no stop to keep consitent */
        _sys_goldengate_acl_remove_entry(lchip, eid);
    }
    SYS_ACL_UNLOCK(lchip);

    return CTC_E_NONE;
}



/*
 * set priority of entry
 */
int32
_sys_goldengate_acl_set_entry_priority(uint8 lchip, uint32 entry_id, uint32 priority)
{
    sys_acl_entry_t* pe = NULL;
    sys_acl_group_t* pg = NULL;
    sys_acl_block_t* pb = NULL;
    uint8 fpa_size = 0;

    /* get sys entry */
    _sys_goldengate_acl_get_nodes_by_eid(lchip, entry_id, &pe, &pg, &pb);
    if(!pe)
    {
        return CTC_E_ACL_ENTRY_UNEXIST;
    }
    if(!pg)
    {
        return CTC_E_ACL_GROUP_UNEXIST;
    }

    if (!ACL_ENTRY_IS_TCAM(pe->key.type)) /* hash entry */
    {
        return CTC_E_ACL_HASH_ENTRY_NO_PRIORITY;
    }

    /* tcam entry check pb */
    CTC_PTR_VALID_CHECK(pb);
    fpa_size = _sys_goldengate_acl_get_fpa_size(lchip, pe, NULL);

    CTC_ERROR_RETURN(fpa_goldengate_set_entry_prio(p_gg_acl_master[lchip]->fpa, &pe->fpae, &pb->fpab, fpa_size, priority));
    return CTC_E_NONE;
}

int32
sys_goldengate_acl_set_entry_priority(uint8 lchip, uint32 entry_id, uint32 priority)
{
    SYS_ACL_INIT_CHECK(lchip);
    SYS_ACL_CHECK_ENTRY_ID(entry_id);
    SYS_ACL_CHECK_ENTRY_PRIO(priority);

    SYS_ACL_DBG_FUNC();
    SYS_ACL_DBG_PARAM("  %% PARAM: eid %u -- prio %u \n", entry_id, priority);

    SYS_ACL_LOCK(lchip);
    CTC_ERROR_RETURN_ACL_UNLOCK(_sys_goldengate_acl_set_entry_priority(lchip, entry_id, priority));
    SYS_ACL_UNLOCK(lchip);

    return CTC_E_NONE;
}
/*
 * get multiple entries
 */
int32
sys_goldengate_acl_get_multi_entry(uint8 lchip, ctc_acl_query_t* query)
{
    uint32                entry_index = 0;
    sys_acl_group_t       * pg        = NULL;
    struct ctc_slistnode_s* pe        = NULL;

    SYS_ACL_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(query);
    SYS_ACL_CHECK_GROUP_ID(query->group_id);

    SYS_ACL_DBG_FUNC();
    SYS_ACL_DBG_PARAM("  %% PARAM: gid %u -- entry_sz %u\n", query->group_id, query->entry_size);

    /* get group node */
    SYS_ACL_LOCK(lchip);
    _sys_goldengate_acl_get_group_by_gid(lchip, query->group_id, &pg);
    SYS_ACL_CHECK_GROUP_UNEXIST(pg);

    if (query->entry_size == 0)
    {
        query->entry_count = pg->entry_count;
    }
    else
    {
        uint32* p_array = query->entry_array;
        if (NULL == p_array)
        {
            SYS_ACL_UNLOCK(lchip);
            return CTC_E_INVALID_PTR;
        }

        if (query->entry_size > pg->entry_count)
        {
            query->entry_size = pg->entry_count;
        }

        CTC_SLIST_LOOP(pg->entry_list, pe)
        {
            *p_array = ((sys_acl_entry_t *) pe)->fpae.entry_id;
            p_array++;
            entry_index++;
            if (entry_index == query->entry_size)
            {
                break;
            }
        }

        query->entry_count = query->entry_size;
    }
    SYS_ACL_UNLOCK(lchip);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_acl_update_tcam_action(uint8 lchip, sys_acl_entry_t* pe,
                                       sys_acl_action_t* pa_new,
                                       sys_acl_vlan_edit_t* pv_new)
{
    sys_acl_vlan_edit_t* pv_get = NULL;
    sys_acl_action_t   * pa_old = pe->action;
    sys_acl_vlan_edit_t* pv_old = pa_old->vlan_edit;

    /* 1. vlan edit. error just return.*/
    if (CTC_FLAG_ISSET(pa_new->flag, CTC_ACL_ACTION_FLAG_VLAN_EDIT))
    {
        CTC_ERROR_RETURN(_sys_goldengate_acl_hash_vlan_edit_spool_add(lchip, pe, pv_new, &pv_get));

        pa_new->vlan_edit       = pv_get;
        pa_new->vlan_edit_valid = 1;
        pa_new->chip.profile_id = pv_get->profile_id;
    }

    pa_new->chip.ad_index = pe->action->chip.ad_index;

    /* if old action has vlan edit. delete it. */
    if (pa_old->vlan_edit_valid)
    {
        _sys_goldengate_acl_hash_vlan_edit_spool_remove(lchip, pv_old);
    }
    sal_memcpy(pe->action, pa_new, sizeof(sys_acl_action_t));

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_acl_update_hash_action(uint8 lchip, sys_acl_entry_t* pe,
                                       sys_acl_action_t* pa_new,
                                       sys_acl_vlan_edit_t* pv_new)
{
    int32              ret      = 0;
    sys_acl_vlan_edit_t* pv_get = NULL;
    sys_acl_action_t   * pa_get = NULL;
    sys_acl_action_t   * pa_old = pe->action;
    sys_acl_vlan_edit_t* pv_old = pa_old->vlan_edit;

    /* 1. vlan edit. error just return.*/
    if (CTC_FLAG_ISSET(pa_new->flag, CTC_ACL_ACTION_FLAG_VLAN_EDIT))
    {
        CTC_ERROR_RETURN(_sys_goldengate_acl_hash_vlan_edit_spool_add(lchip, pe, pv_new, &pv_get));

        pa_new->vlan_edit       = pv_get;
        pa_new->vlan_edit_valid = 1;
        pa_new->chip.profile_id = pv_get->profile_id;
    }

    /* 2. action */
    CTC_ERROR_GOTO(_sys_goldengate_acl_hash_ad_spool_add(lchip, pe, pa_new, &pa_get), ret, cleanup);


    /* if old action has vlan edit. delete it. */
    if (pa_old->vlan_edit_valid)
    {
        _sys_goldengate_acl_hash_vlan_edit_spool_remove(lchip, pv_old);
    }

    /* old action must be delete */
    _sys_goldengate_acl_hash_ad_spool_remove(lchip, pa_old);

    pe->action = pa_get;
    return CTC_E_NONE;

 cleanup:
    if (pv_get)
    {
        _sys_goldengate_acl_hash_vlan_edit_spool_remove(lchip, pv_get);
    }
    return ret;
}



/* update acl action*/
int32
sys_goldengate_acl_update_action(uint8 lchip, uint32 entry_id, ctc_acl_action_t* action)
{
    sys_acl_entry_t     * pe_old = NULL;
    sys_acl_group_t     * pg     = NULL;
    sys_acl_action_t    new_action;
    sys_acl_vlan_edit_t new_vedit;

    SYS_ACL_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(action);
    SYS_ACL_DBG_FUNC();

    SYS_ACL_LOCK(lchip);
    /* lookup */
    _sys_goldengate_acl_get_nodes_by_eid(lchip, entry_id, &pe_old, &pg, NULL);
    if(!pe_old)
    {
        SYS_ACL_UNLOCK(lchip);
        return CTC_E_ACL_ENTRY_UNEXIST;
    }
    if(!pg)
    {
        SYS_ACL_UNLOCK(lchip);
        return CTC_E_ACL_GROUP_UNEXIST;
    }

    sal_memset(&new_action, 0, sizeof(sys_acl_action_t));

    if ((pe_old->key.type == CTC_ACL_KEY_PBR_IPV4) || (pe_old->key.type == CTC_ACL_KEY_PBR_IPV6))
    {
        CTC_ERROR_RETURN_ACL_UNLOCK(_sys_goldengate_acl_check_pbr_action(lchip, action->flag));
        CTC_ERROR_RETURN_ACL_UNLOCK(_sys_goldengate_acl_map_pbr_action(lchip, action, &new_action));
    }
    else
    {
        CTC_ERROR_RETURN_ACL_UNLOCK(_sys_goldengate_acl_check_action(lchip, pg->group_info.dir, action->flag));
        CTC_ERROR_RETURN_ACL_UNLOCK(_sys_goldengate_acl_map_action(lchip, action, &new_action, &new_vedit));
    }

    if (ACL_ENTRY_IS_TCAM(pe_old->key.type))
    {
        CTC_ERROR_RETURN_ACL_UNLOCK(_sys_goldengate_acl_update_tcam_action(lchip, pe_old, &new_action, &new_vedit));
    }
    else
    {
        CTC_ERROR_RETURN_ACL_UNLOCK(_sys_goldengate_acl_update_hash_action(lchip, pe_old, &new_action, &new_vedit));
    }

    if (pe_old->fpae.flag == FPA_ENTRY_FLAG_INSTALLED)
    {
        CTC_ERROR_RETURN_ACL_UNLOCK(_sys_goldengate_acl_add_hw(lchip, entry_id, TRUE));
    }
    SYS_ACL_UNLOCK(lchip);

    return CTC_E_NONE;
}

int32
sys_goldengate_acl_copy_entry(uint8 lchip, ctc_acl_copy_entry_t* copy_entry)
{
    sys_acl_entry_t * pe_src = NULL;

    sys_acl_entry_t * pe_dst    = NULL;
    sys_acl_action_t* pa_dst    = NULL;
    sys_acl_block_t * pb_dst    = NULL;
    sys_acl_group_t * pg_dst    = NULL;
    uint32          block_index = 0;
    uint8           fpa_size    = 0;
    int32           ret         = 0;
    sys_acl_l4_t    * l4        = NULL;
    uint8           block_id    = 0;

    SYS_ACL_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(copy_entry);
    SYS_ACL_CHECK_ENTRY_ID(copy_entry->entry_id);
    SYS_ACL_DBG_FUNC();

    SYS_ACL_LOCK(lchip);
    /* check src entry */
    _sys_goldengate_acl_get_nodes_by_eid(lchip, copy_entry->src_entry_id, &pe_src, NULL, NULL);
    if (!pe_src)
    {
        SYS_ACL_UNLOCK(lchip);
        return CTC_E_ACL_ENTRY_UNEXIST;
    }

    if (!(ACL_ENTRY_IS_TCAM(pe_src->key.type))) /* hash entry not support copy */
    {
        SYS_ACL_UNLOCK(lchip);
        return CTC_E_ACL_HASH_ENTRY_UNSUPPORT_COPY;
    }

    /* check dst entry */
    _sys_goldengate_acl_get_nodes_by_eid(lchip, copy_entry->dst_entry_id, &pe_dst, NULL, NULL);
    if (pe_dst)
    {
        SYS_ACL_UNLOCK(lchip);
        return CTC_E_ACL_ENTRY_EXIST;
    }

    _sys_goldengate_acl_get_group_by_gid(lchip, copy_entry->dst_group_id, &pg_dst);
    SYS_ACL_CHECK_GROUP_UNEXIST(pg_dst);

    MALLOC_ZERO(MEM_ACL_MODULE, pe_dst, p_gg_acl_master[lchip]->entry_size[pe_src->key.type]);
    if (!pe_dst)
    {
        SYS_ACL_UNLOCK(lchip);
        return CTC_E_NO_MEMORY;
    }

    MALLOC_ZERO(MEM_ACL_MODULE, pa_dst, sizeof(sys_acl_action_t));
    if (!pa_dst)
    {
        SYS_ACL_UNLOCK(lchip);
        mem_free(pe_dst);
        return CTC_E_NO_MEMORY;
    }

    sal_memcpy(pe_dst, pe_src, p_gg_acl_master[lchip]->entry_size[pe_src->key.type]);
    sal_memcpy(pa_dst, pe_src->action, sizeof(sys_acl_action_t));
    pe_dst->action = pa_dst;

    /* flag count ++ */
    l4 = _sys_goldengate_get_l4(lchip, pe_dst);

    if (l4) /* if exist */
    {
        if (l4->flag_tcp_flags)
        {
            p_gg_acl_master[lchip]->tcp_flag[l4->tcp_flags_idx].ref++;
        }
        if (l4->flag_l4_src_port_range)
        {
            p_gg_acl_master[lchip]->l4_port[l4->l4_src_port_range_idx].ref++;
        }
        if (l4->flag_l4_dst_port_range)
        {
            p_gg_acl_master[lchip]->l4_port[l4->l4_dst_port_range_idx].ref++;
        }
    }

    pe_dst->fpae.entry_id    = copy_entry->dst_entry_id;
    pe_dst->fpae.priority = pe_src->fpae.priority;
    pe_dst->fpae.priority = pe_src->fpae.priority;
    pe_dst->group            = pg_dst;
    pe_dst->head.next        = NULL;

    SYS_ACL_GET_BLOCK_ID(pe_dst, block_id);
    pb_dst = &p_gg_acl_master[lchip]->block[block_id];
    fpa_size = _sys_goldengate_acl_get_fpa_size(lchip, pe_dst, NULL);

    if (NULL == pb_dst)
    {
        ret = CTC_E_INVALID_PTR;
        goto cleanup;
    }

    /* get block index, based on priority */
    CTC_ERROR_GOTO(fpa_goldengate_alloc_offset(p_gg_acl_master[lchip]->fpa, &(pb_dst->fpab), fpa_size,
                                    pe_src->fpae.priority, &block_index), ret, cleanup);
    pe_dst->fpae.offset_a = block_index;

    /* add to hash */
    ctc_hash_insert(p_gg_acl_master[lchip]->entry, pe_dst);

    /* add to group */
    ctc_slist_add_head(pg_dst->entry_list, &(pe_dst->head));

    /* mark flag */
    pe_dst->fpae.flag = FPA_ENTRY_FLAG_UNINSTALLED;

    /* add to block */
    pb_dst->fpab.entries[block_index] = &pe_dst->fpae;

    /* set new priority */
    CTC_ERROR_RETURN_ACL_UNLOCK(_sys_goldengate_acl_set_entry_priority(lchip, copy_entry->dst_entry_id, pe_src->fpae.priority));

    (pg_dst->entry_count)++;

    _sys_goldengate_acl_update_key_count(lchip, pe_dst, 1);
    SYS_ACL_UNLOCK(lchip);

    return CTC_E_NONE;

 cleanup:
    SYS_ACL_UNLOCK(lchip);
    mem_free(pa_dst);
    mem_free(pe_dst);
    return ret;
}

int32
sys_goldengate_acl_set_hash_field_sel(uint8 lchip, ctc_acl_hash_field_sel_t* field_sel)
{
    ds_t   ds_sel;
    uint32 cmd = 0;
    SYS_ACL_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(field_sel);
    CTC_MAX_VALUE_CHECK(field_sel->field_sel_id, 0xF);

    sal_memset(&ds_sel, 0, sizeof(ds_sel));
    if (CTC_ACL_KEY_HASH_MAC == field_sel->hash_key_type)
    {
        if((field_sel->u.mac.phy_port && field_sel->u.mac.logic_port)
            || (field_sel->u.mac.phy_port && field_sel->u.mac.metadata)
            || (field_sel->u.mac.metadata && field_sel->u.mac.logic_port))
        {
            return CTC_E_INVALID_PARAM;
        }
        SetFlowL2HashFieldSelect(V,etherTypeEn_f,   &ds_sel, !!field_sel->u.mac.eth_type);
        SetFlowL2HashFieldSelect(V,svlanIdValidEn_f,&ds_sel, !!field_sel->u.mac.tag_valid);
        SetFlowL2HashFieldSelect(V,stagCosEn_f,     &ds_sel, !!field_sel->u.mac.cos);
        SetFlowL2HashFieldSelect(V,svlanIdEn_f,     &ds_sel, !!field_sel->u.mac.vlan_id);
        SetFlowL2HashFieldSelect(V,stagCfiEn_f,     &ds_sel, !!field_sel->u.mac.cfi);
        SetFlowL2HashFieldSelect(V,macSaEn_f,       &ds_sel, !!field_sel->u.mac.mac_sa);
        SetFlowL2HashFieldSelect(V,macDaEn_f,       &ds_sel, !!field_sel->u.mac.mac_da);
        SetFlowL2HashFieldSelect(V,metadataEn_f,    &ds_sel, !!field_sel->u.mac.metadata);
        SetFlowL2HashFieldSelect(V,logicSrcPortEn_f,&ds_sel, !!field_sel->u.mac.logic_port);
        SetFlowL2HashFieldSelect(V,globalSrcPortEn_f,&ds_sel, !!field_sel->u.mac.phy_port);
        cmd = DRV_IOW(FlowL2HashFieldSelect_t, DRV_ENTRY_FLAG);
    }
    else if (CTC_ACL_KEY_HASH_IPV4 == field_sel->hash_key_type)
    {
        if(field_sel->u.ipv4.phy_port && field_sel->u.ipv4.logic_port)
        {
            return CTC_E_INVALID_PARAM;
        }
        SetFlowL3Ipv4HashFieldSelect(V,metadataEn_f,     &ds_sel, !!field_sel->u.ipv4.metadata);
        SetFlowL3Ipv4HashFieldSelect(V,logicSrcPortEn_f, &ds_sel, !!field_sel->u.ipv4.logic_port);
        SetFlowL3Ipv4HashFieldSelect(V,globalSrcPortEn_f, &ds_sel, !!field_sel->u.ipv4.phy_port);
        SetFlowL3Ipv4HashFieldSelect(V,dscpEn_f,          &ds_sel, !!field_sel->u.ipv4.dscp);
        SetFlowL3Ipv4HashFieldSelect(V, ecnEn_f          ,&ds_sel, !!field_sel->u.ipv4.ecn);
        SetFlowL3Ipv4HashFieldSelect(V, l4SourcePortEn_f ,&ds_sel, !!field_sel->u.ipv4.l4_src_port);
        SetFlowL3Ipv4HashFieldSelect(V, l4DestPortEn_f   ,&ds_sel, !!field_sel->u.ipv4.l4_dst_port);
        SetFlowL3Ipv4HashFieldSelect(V, layer3HeaderProtocolEn_f,&ds_sel, !!field_sel->u.ipv4.l4_protocol);
        SetFlowL3Ipv4HashFieldSelect(V, ipSaEn_f       ,&ds_sel, field_sel->u.ipv4.ip_sa?0x1F:0);
        SetFlowL3Ipv4HashFieldSelect(V, ipDaEn_f       ,&ds_sel, field_sel->u.ipv4.ip_da?0x1F:0);
        SetFlowL3Ipv4HashFieldSelect(V, icmpTypeEn_f   ,&ds_sel, !!field_sel->u.ipv4.icmp_type);
        SetFlowL3Ipv4HashFieldSelect(V, icmpOpcodeEn_f ,&ds_sel, !!field_sel->u.ipv4.icmp_code);
        SetFlowL3Ipv4HashFieldSelect(V, vxlanVniEn_f ,&ds_sel, !!field_sel->u.ipv4.vni);
        SetFlowL3Ipv4HashFieldSelect(V, greKeyEn_f ,&ds_sel, !!field_sel->u.ipv4.gre_key);

        cmd = DRV_IOW(FlowL3Ipv4HashFieldSelect_t, DRV_ENTRY_FLAG);
    }
    else if (CTC_ACL_KEY_HASH_MPLS == field_sel->hash_key_type)
    {
        if(field_sel->u.mpls.phy_port && field_sel->u.mpls.logic_port)
        {
            return CTC_E_INVALID_PARAM;
        }
        SetFlowL3MplsHashFieldSelect(V, metadataEn_f     , &ds_sel, !!field_sel->u.mpls.metadata);
        SetFlowL3MplsHashFieldSelect(V, mplsTtl2En_f     , &ds_sel, !!field_sel->u.mpls.mpls_label2_ttl);
        SetFlowL3MplsHashFieldSelect(V, mplsSbit2En_f    , &ds_sel, !!field_sel->u.mpls.mpls_label2_s);
        SetFlowL3MplsHashFieldSelect(V, mplsExp2En_f     , &ds_sel, !!field_sel->u.mpls.mpls_label2_exp);
        SetFlowL3MplsHashFieldSelect(V, mplsLabel2En_f   , &ds_sel, !!field_sel->u.mpls.mpls_label2_label);
        SetFlowL3MplsHashFieldSelect(V, mplsTtl1En_f     , &ds_sel, !!field_sel->u.mpls.mpls_label1_ttl);
        SetFlowL3MplsHashFieldSelect(V, mplsSbit1En_f    , &ds_sel, !!field_sel->u.mpls.mpls_label1_s);
        SetFlowL3MplsHashFieldSelect(V, mplsExp1En_f     , &ds_sel, !!field_sel->u.mpls.mpls_label1_exp);
        SetFlowL3MplsHashFieldSelect(V, mplsLabel1En_f   , &ds_sel, !!field_sel->u.mpls.mpls_label1_label);
        SetFlowL3MplsHashFieldSelect(V, mplsTtl0En_f     , &ds_sel, !!field_sel->u.mpls.mpls_label0_ttl);
        SetFlowL3MplsHashFieldSelect(V, mplsSbit0En_f    , &ds_sel, !!field_sel->u.mpls.mpls_label0_s);
        SetFlowL3MplsHashFieldSelect(V, mplsExp0En_f     , &ds_sel, !!field_sel->u.mpls.mpls_label0_exp);
        SetFlowL3MplsHashFieldSelect(V, mplsLabel0En_f   , &ds_sel, !!field_sel->u.mpls.mpls_label0_label);
        SetFlowL3MplsHashFieldSelect(V, globalSrcPortEn_f, &ds_sel, !!field_sel->u.mpls.phy_port);
        SetFlowL3MplsHashFieldSelect(V, logicSrcPortEn_f , &ds_sel, !!field_sel->u.mpls.logic_port);
        SetFlowL3MplsHashFieldSelect(V, labelNumEn_f     , &ds_sel, !!field_sel->u.mpls.mpls_label_num);
        cmd = DRV_IOW(FlowL3MplsHashFieldSelect_t, DRV_ENTRY_FLAG);
    }
    else if (CTC_ACL_KEY_HASH_IPV6 == field_sel->hash_key_type)
    {
        if((field_sel->u.ipv6.phy_port && field_sel->u.ipv6.logic_port)
            || (field_sel->u.ipv6.phy_port && field_sel->u.ipv6.metadata)
            || (field_sel->u.ipv6.metadata && field_sel->u.ipv6.logic_port))
        {
            return CTC_E_INVALID_PARAM;
        }
        SetFlowL3Ipv6HashFieldSelect(V, metadataEn_f     , &ds_sel, !!field_sel->u.ipv6.metadata);
        SetFlowL3Ipv6HashFieldSelect(V, dscpEn_f         , &ds_sel, !!field_sel->u.ipv6.dscp);
        SetFlowL3Ipv6HashFieldSelect(V, l4SourcePortEn_f , &ds_sel, !!field_sel->u.ipv6.l4_src_port);
        SetFlowL3Ipv6HashFieldSelect(V, l4DestPortEn_f   , &ds_sel, !!field_sel->u.ipv6.l4_dst_port);
        SetFlowL3Ipv6HashFieldSelect(V, ipSaEn_f         , &ds_sel, field_sel->u.ipv6.ip_sa?0x1F:0);
        SetFlowL3Ipv6HashFieldSelect(V, ipDaEn_f         , &ds_sel, field_sel->u.ipv6.ip_da?0x1F:0);
        SetFlowL3Ipv6HashFieldSelect(V, globalSrcPortEn_f, &ds_sel, !!field_sel->u.ipv6.phy_port);
        SetFlowL3Ipv6HashFieldSelect(V, layer4TypeEn_f   , &ds_sel, !!field_sel->u.ipv6.l4_type);
        SetFlowL3Ipv6HashFieldSelect(V, logicSrcPortEn_f , &ds_sel, !!field_sel->u.ipv6.logic_port);
        SetFlowL3Ipv6HashFieldSelect(V, icmpTypeEn_f     , &ds_sel, !!field_sel->u.ipv6.icmp_type);
        SetFlowL3Ipv6HashFieldSelect(V, icmpOpcodeEn_f   , &ds_sel, !!field_sel->u.ipv6.icmp_code);
        SetFlowL3Ipv6HashFieldSelect(V, vxlanVniEn_f   , &ds_sel, !!field_sel->u.ipv6.vni);
        SetFlowL3Ipv6HashFieldSelect(V, greKeyEn_f   , &ds_sel, !!field_sel->u.ipv6.gre_key);
        cmd = DRV_IOW(FlowL3Ipv6HashFieldSelect_t, DRV_ENTRY_FLAG);
    }
    else if (CTC_ACL_KEY_HASH_L2_L3 == field_sel->hash_key_type)
    {
        if(field_sel->u.l2_l3.phy_port && field_sel->u.l2_l3.logic_port)
        {
            return CTC_E_INVALID_PARAM;
        }
        SetFlowL2L3HashFieldSelect(V, ctagCfiEn_f        , &ds_sel, !!field_sel->u.l2_l3.ctag_cfi);
        SetFlowL2L3HashFieldSelect(V, ctagCosEn_f        , &ds_sel, !!field_sel->u.l2_l3.ctag_cos);
        SetFlowL2L3HashFieldSelect(V, cvlanIdEn_f        , &ds_sel, !!field_sel->u.l2_l3.ctag_vlan);
        SetFlowL2L3HashFieldSelect(V, cvlanIdValidEn_f   , &ds_sel, !!field_sel->u.l2_l3.ctag_valid);
        SetFlowL2L3HashFieldSelect(V, metadataEn_f       , &ds_sel, !!field_sel->u.l2_l3.metadata);
        SetFlowL2L3HashFieldSelect(V, etherTypeEn_f      , &ds_sel, !!field_sel->u.l2_l3.eth_type);
        SetFlowL2L3HashFieldSelect(V, svlanIdValidEn_f   , &ds_sel, !!field_sel->u.l2_l3.stag_valid);
        SetFlowL2L3HashFieldSelect(V, stagCosEn_f        , &ds_sel, !!field_sel->u.l2_l3.stag_cos);
        SetFlowL2L3HashFieldSelect(V, svlanIdEn_f        , &ds_sel, !!field_sel->u.l2_l3.stag_vlan);
        SetFlowL2L3HashFieldSelect(V, stagCfiEn_f        , &ds_sel, !!field_sel->u.l2_l3.stag_cfi);
        SetFlowL2L3HashFieldSelect(V, macSaEn_f          , &ds_sel, !!field_sel->u.l2_l3.mac_sa);
        SetFlowL2L3HashFieldSelect(V, macDaEn_f          , &ds_sel, !!field_sel->u.l2_l3.mac_da);
        SetFlowL2L3HashFieldSelect(V, layer4TypeEn_f     , &ds_sel, !!field_sel->u.l2_l3.l4_type);
        SetFlowL2L3HashFieldSelect(V, layer3TypeEn_f     , &ds_sel, !!field_sel->u.l2_l3.l3_type);
        SetFlowL2L3HashFieldSelect(V, ecnEn_f            , &ds_sel, !!field_sel->u.l2_l3.ecn);
        SetFlowL2L3HashFieldSelect(V, ttlEn_f            , &ds_sel, !!field_sel->u.l2_l3.ttl);
        SetFlowL2L3HashFieldSelect(V, dscpEn_f           , &ds_sel, !!field_sel->u.l2_l3.dscp);
        SetFlowL2L3HashFieldSelect(V, ipSaEn_f           , &ds_sel, field_sel->u.l2_l3.ip_sa?0x1F:0);
        SetFlowL2L3HashFieldSelect(V, ipDaEn_f           , &ds_sel, field_sel->u.l2_l3.ip_da?0x1F:0);
        SetFlowL2L3HashFieldSelect(V, mplsLabel0En_f     , &ds_sel, !!field_sel->u.l2_l3.mpls_label0_label);
        SetFlowL2L3HashFieldSelect(V, mplsExp0En_f       , &ds_sel, !!field_sel->u.l2_l3.mpls_label0_exp);
        SetFlowL2L3HashFieldSelect(V, mplsSbit0En_f      , &ds_sel, !!field_sel->u.l2_l3.mpls_label0_s);
        SetFlowL2L3HashFieldSelect(V, mplsTtl0En_f       , &ds_sel, !!field_sel->u.l2_l3.mpls_label0_ttl);
        SetFlowL2L3HashFieldSelect(V, mplsLabel1En_f     , &ds_sel, !!field_sel->u.l2_l3.mpls_label1_label);
        SetFlowL2L3HashFieldSelect(V, mplsExp1En_f       , &ds_sel, !!field_sel->u.l2_l3.mpls_label1_exp);
        SetFlowL2L3HashFieldSelect(V, mplsSbit1En_f      , &ds_sel, !!field_sel->u.l2_l3.mpls_label1_s);
        SetFlowL2L3HashFieldSelect(V, mplsTtl1En_f       , &ds_sel, !!field_sel->u.l2_l3.mpls_label1_ttl);
        SetFlowL2L3HashFieldSelect(V, mplsLabel2En_f     , &ds_sel, !!field_sel->u.l2_l3.mpls_label2_label);
        SetFlowL2L3HashFieldSelect(V, mplsExp2En_f       , &ds_sel, !!field_sel->u.l2_l3.mpls_label2_exp);
        SetFlowL2L3HashFieldSelect(V, mplsSbit2En_f      , &ds_sel, !!field_sel->u.l2_l3.mpls_label2_s);
        SetFlowL2L3HashFieldSelect(V, mplsTtl2En_f       , &ds_sel, !!field_sel->u.l2_l3.mpls_label2_ttl);
        SetFlowL2L3HashFieldSelect(V, l4SourcePortEn_f   , &ds_sel, !!field_sel->u.l2_l3.l4_src_port);
        SetFlowL2L3HashFieldSelect(V, l4DestPortEn_f     , &ds_sel, !!field_sel->u.l2_l3.l4_dst_port);
        SetFlowL2L3HashFieldSelect(V, logicSrcPortEn_f   , &ds_sel, !!field_sel->u.l2_l3.logic_port);
        SetFlowL2L3HashFieldSelect(V, globalSrcPortEn_f  , &ds_sel, !!field_sel->u.l2_l3.phy_port);
        SetFlowL2L3HashFieldSelect(V, icmpTypeEn_f       , &ds_sel, !!field_sel->u.l2_l3.icmp_type);
        SetFlowL2L3HashFieldSelect(V, icmpOpcodeEn_f     , &ds_sel, !!field_sel->u.l2_l3.icmp_code);
        SetFlowL2L3HashFieldSelect(V, labelNumEn_f       , &ds_sel, !!field_sel->u.l2_l3.mpls_label_num);
        SetFlowL2L3HashFieldSelect(V, vxlanVniEn_f       , &ds_sel, !!field_sel->u.l2_l3.vni);
        SetFlowL2L3HashFieldSelect(V, greKeyEn_f       , &ds_sel, !!field_sel->u.l2_l3.gre_key);

        cmd = DRV_IOW(FlowL2L3HashFieldSelect_t, DRV_ENTRY_FLAG);
    }
    else
    {
        return CTC_E_INVALID_PARAM;
    }

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, field_sel->field_sel_id, cmd, &ds_sel));

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_acl_install_vlan_edit(uint8 lchip, sys_acl_vlan_edit_t* p_vlan_edit, uint16* p_prof_idx)
{
    sys_acl_vlan_edit_t          * p_vlan_edit_new = NULL;
    uint32                       cmd               = 0;
    int32                        ret               = 0;
    DsAclVlanActionProfile_m ds_edit;

    sal_memset(&ds_edit, 0, sizeof(ds_edit));

    MALLOC_ZERO(MEM_ACL_MODULE, p_vlan_edit_new, sizeof(sys_acl_vlan_edit_t));
    if (NULL == p_vlan_edit_new)
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memcpy(p_vlan_edit_new, p_vlan_edit, sizeof(sys_acl_vlan_edit_t));
    p_vlan_edit_new->lchip = lchip;

    p_vlan_edit_new->profile_id = *p_prof_idx;
    if (ctc_spool_static_add(p_gg_acl_master[lchip]->vlan_edit_spool, p_vlan_edit_new) < 0)
    {
        ret = CTC_E_NO_MEMORY;
        goto cleanup;
    }

    _sys_goldengate_acl_build_vlan_edit(lchip, &ds_edit, p_vlan_edit);
    cmd = DRV_IOW(DsAclVlanActionProfile_t, DRV_ENTRY_FLAG);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, p_vlan_edit_new->profile_id, cmd, &ds_edit), ret, cleanup);

    *p_prof_idx = *p_prof_idx + 1;

    return CTC_E_NONE;

 cleanup:
    mem_free(p_vlan_edit_new);
    return ret;
}

/* 10 combinations */
STATIC int32
_sys_goldengate_acl_init_vlan_edit_stag(uint8 lchip, sys_acl_vlan_edit_t* p_vlan_edit, uint16* p_prof_idx)
{
    uint8 stag_op = 0;

    SYS_ACL_DBG_FUNC();

    for (stag_op = CTC_ACL_VLAN_TAG_OP_NONE; stag_op < CTC_ACL_VLAN_TAG_OP_MAX; stag_op++)
    {
        p_vlan_edit->stag_op = stag_op;

        switch (stag_op)
        {
        case CTC_ACL_VLAN_TAG_OP_REP:         /* template has no replace */
            continue;
        case CTC_ACL_VLAN_TAG_OP_REP_OR_ADD:  /* 4*/
        case CTC_ACL_VLAN_TAG_OP_ADD:         /* 4*/
        {
            p_vlan_edit->svid_sl = CTC_ACL_VLAN_TAG_SL_NEW;
            p_vlan_edit->scos_sl = CTC_ACL_VLAN_TAG_SL_NEW;
            CTC_ERROR_RETURN(_sys_goldengate_acl_install_vlan_edit(lchip, p_vlan_edit, p_prof_idx));

            p_vlan_edit->svid_sl = CTC_ACL_VLAN_TAG_SL_NEW;
            p_vlan_edit->scos_sl = CTC_ACL_VLAN_TAG_SL_NONE;
            CTC_ERROR_RETURN(_sys_goldengate_acl_install_vlan_edit(lchip, p_vlan_edit, p_prof_idx));

            p_vlan_edit->svid_sl = CTC_ACL_VLAN_TAG_SL_NONE;
            p_vlan_edit->scos_sl = CTC_ACL_VLAN_TAG_SL_NONE;
            CTC_ERROR_RETURN(_sys_goldengate_acl_install_vlan_edit(lchip, p_vlan_edit, p_prof_idx));

            p_vlan_edit->svid_sl = CTC_ACL_VLAN_TAG_SL_NONE;
            p_vlan_edit->scos_sl = CTC_ACL_VLAN_TAG_SL_NEW;
            CTC_ERROR_RETURN(_sys_goldengate_acl_install_vlan_edit(lchip, p_vlan_edit, p_prof_idx));
        }
        break;
        case CTC_ACL_VLAN_TAG_OP_NONE:      /*1*/
        case CTC_ACL_VLAN_TAG_OP_DEL:       /*1*/
            p_vlan_edit->svid_sl = CTC_ACL_VLAN_TAG_SL_NONE;
            p_vlan_edit->scos_sl = CTC_ACL_VLAN_TAG_SL_NONE;
            CTC_ERROR_RETURN(_sys_goldengate_acl_install_vlan_edit(lchip, p_vlan_edit, p_prof_idx));
            break;
        }
    }

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_acl_init_vlan_edit(uint8 lchip)
{
    uint16              prof_idx = 0;
    uint8               ctag_op  = 0;

    sys_acl_vlan_edit_t vlan_edit;
    sal_memset(&vlan_edit, 0, sizeof(vlan_edit));

    for (ctag_op = CTC_ACL_VLAN_TAG_OP_NONE; ctag_op < CTC_ACL_VLAN_TAG_OP_MAX; ctag_op++)
    {
        vlan_edit.ctag_op = ctag_op;

        switch (ctag_op)
        {
        case CTC_ACL_VLAN_TAG_OP_ADD:        /* template has no append ctag */
        case CTC_ACL_VLAN_TAG_OP_REP:        /* template has no replace */
            continue;
        case CTC_ACL_VLAN_TAG_OP_REP_OR_ADD: /* 4 */
        {
            vlan_edit.cvid_sl = CTC_ACL_VLAN_TAG_SL_NEW;
            vlan_edit.ccos_sl = CTC_ACL_VLAN_TAG_SL_NEW;
            CTC_ERROR_RETURN(_sys_goldengate_acl_init_vlan_edit_stag(lchip, &vlan_edit, &prof_idx));

            vlan_edit.cvid_sl = CTC_ACL_VLAN_TAG_SL_NEW;
            vlan_edit.ccos_sl = CTC_ACL_VLAN_TAG_SL_NONE;
            CTC_ERROR_RETURN(_sys_goldengate_acl_init_vlan_edit_stag(lchip, &vlan_edit, &prof_idx));

            vlan_edit.cvid_sl = CTC_ACL_VLAN_TAG_SL_NONE;
            vlan_edit.ccos_sl = CTC_ACL_VLAN_TAG_SL_NONE;
            CTC_ERROR_RETURN(_sys_goldengate_acl_init_vlan_edit_stag(lchip, &vlan_edit, &prof_idx));

            vlan_edit.cvid_sl = CTC_ACL_VLAN_TAG_SL_NONE;
            vlan_edit.ccos_sl = CTC_ACL_VLAN_TAG_SL_NEW;
            CTC_ERROR_RETURN(_sys_goldengate_acl_init_vlan_edit_stag(lchip, &vlan_edit, &prof_idx));
        }
        break;
        case CTC_ACL_VLAN_TAG_OP_NONE:    /* 1 */
        case CTC_ACL_VLAN_TAG_OP_DEL:     /* 1 */
            vlan_edit.cvid_sl = CTC_ACL_VLAN_TAG_SL_NONE;
            vlan_edit.ccos_sl = CTC_ACL_VLAN_TAG_SL_NONE;
            CTC_ERROR_RETURN(_sys_goldengate_acl_init_vlan_edit_stag(lchip, &vlan_edit, &prof_idx));
            break;
        }
    }
    /* 6 * 10 = 60 */

    /*for swap*/
    vlan_edit.ctag_op = CTC_ACL_VLAN_TAG_OP_REP_OR_ADD;
    vlan_edit.stag_op = CTC_ACL_VLAN_TAG_OP_REP_OR_ADD;

    vlan_edit.cvid_sl = CTC_ACL_VLAN_TAG_SL_ALTERNATIVE;
    vlan_edit.ccos_sl = CTC_ACL_VLAN_TAG_SL_ALTERNATIVE;
    vlan_edit.svid_sl = CTC_ACL_VLAN_TAG_SL_ALTERNATIVE;
    vlan_edit.scos_sl = CTC_ACL_VLAN_TAG_SL_ALTERNATIVE;
    CTC_ERROR_RETURN(_sys_goldengate_acl_install_vlan_edit(lchip, &vlan_edit, &prof_idx));

    vlan_edit.cvid_sl = CTC_ACL_VLAN_TAG_SL_ALTERNATIVE;
    vlan_edit.ccos_sl = CTC_ACL_VLAN_TAG_SL_NONE;
    vlan_edit.svid_sl = CTC_ACL_VLAN_TAG_SL_ALTERNATIVE;
    vlan_edit.scos_sl = CTC_ACL_VLAN_TAG_SL_NONE;
    CTC_ERROR_RETURN(_sys_goldengate_acl_install_vlan_edit(lchip, &vlan_edit, &prof_idx));

    vlan_edit.cvid_sl = CTC_ACL_VLAN_TAG_SL_NONE;
    vlan_edit.ccos_sl = CTC_ACL_VLAN_TAG_SL_ALTERNATIVE;
    vlan_edit.svid_sl = CTC_ACL_VLAN_TAG_SL_NONE;
    vlan_edit.scos_sl = CTC_ACL_VLAN_TAG_SL_ALTERNATIVE;
    CTC_ERROR_RETURN(_sys_goldengate_acl_install_vlan_edit(lchip, &vlan_edit, &prof_idx));

    /* 60+3 = 63 */
    return CTC_E_NONE;
}

/*
 * set acl register
 */
STATIC int32
_sys_goldengate_acl_set_register(uint8 lchip, sys_acl_register_t* p_reg)
{
    uint32                  cmd                = 0;
    uint32                  oam_obey_acl_qos      = 1;
    uint32                  field_value      = 0;
    uint32                  service_acl_en = 0;
    uint32                  ret                  = CTC_E_NONE;
    uint8                    acl_id              = 0;
    uint32 field_id[4] = {FlowTcamLookupCtl_ingressAcl0Bitmap_f,FlowTcamLookupCtl_ingressAcl1Bitmap_f,FlowTcamLookupCtl_ingressAcl2Bitmap_f,FlowTcamLookupCtl_ingressAcl3Bitmap_f};
    IpeAclLookupCtl_m    ipe;
    EpeAclQosCtl_m       epe;
    FlowTcamLookupCtl_m flow_ctl;

    CTC_PTR_VALID_CHECK(p_reg);
    CTC_MAX_VALUE_CHECK(p_reg->ingress_port_service_acl_en, 0xF);
    CTC_MAX_VALUE_CHECK(p_reg->ingress_vlan_service_acl_en, 0xF);

    CTC_MAX_VALUE_CHECK(p_reg->egress_port_service_acl_en, 1);
    CTC_MAX_VALUE_CHECK(p_reg->egress_vlan_service_acl_en, 1);
    CTC_MAX_VALUE_CHECK(p_reg->egress_per_port_sel_en, 1);

    CTC_MAX_VALUE_CHECK(p_reg->ingress_vlan_acl_num, 4);
    CTC_MAX_VALUE_CHECK(p_reg->egress_vlan_acl_num, 1);

    /* ipe */
    cmd = DRV_IOR(IpeAclLookupCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe));

    SetIpeAclLookupCtl(V, l3AclNum_f, &ipe, p_reg->ingress_vlan_acl_num);

    for (acl_id = 0; acl_id < 4; acl_id++)
    {
        cmd = DRV_IOR(FlowTcamLookupCtl_t, field_id[acl_id]);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
        if (0 != field_value)
        {
            CTC_BIT_SET(service_acl_en, acl_id);
        }
    }

    if (0 == service_acl_en)
    {
        if (p_reg->ingress_port_service_acl_en || p_reg->ingress_vlan_service_acl_en)
        {
            ret = CTC_E_INVALID_CONFIG;
        }
    }
    else
    {
        field_value = service_acl_en & p_reg->ingress_port_service_acl_en;
        field_value = (field_value & 0xf) | ((service_acl_en & p_reg->ingress_vlan_service_acl_en) << 4);
    }

    SetIpeAclLookupCtl(V, serviceAclQosEn_f, &ipe, field_value);
    SetIpeAclLookupCtl(V, oamObeyAclQos_f, &ipe, oam_obey_acl_qos);
    SetIpeAclLookupCtl(V, downstreamOamFollowAcl_f, &ipe, oam_obey_acl_qos);

    SetIpeAclLookupCtl(V, ipfixHashBackToL3V6Type_f, &ipe, 1);

    SetIpeAclLookupCtl(V, layer2TypeUsedAsVlanNum_f, &ipe, 1);
    SetIpeAclLookupCtl(V, aclVlanRangeEn_f, &ipe, 1);

    cmd = DRV_IOW(IpeAclLookupCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe));

    sal_memset(&flow_ctl, 0, sizeof(flow_ctl));
    cmd = DRV_IOR(FlowTcamLookupCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &flow_ctl));
    SetFlowTcamLookupCtl(V, l2KeyUse160_f, &flow_ctl, !p_reg->ipv4_160bit_mode_en);
    SetFlowTcamLookupCtl(V, l3MergeKey_f, &flow_ctl, p_reg->ipv4_160bit_mode_en); /* L3TYPE_NONE */
    SetFlowTcamLookupCtl(V, aclL2KeyIgnorMergeMode_f, &flow_ctl, 1);
    SetFlowTcamLookupCtl(V, aclL2L3KeyIgnorMergeMode_f, &flow_ctl, 1);
    SetFlowTcamLookupCtl(V, aclL3V6KeyIgnorMergeMode_f, &flow_ctl, 1);
    SetFlowTcamLookupCtl(V, useRoutedPacket_f, &flow_ctl, 1);
    cmd = DRV_IOW(FlowTcamLookupCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &flow_ctl));

    /* epe */
    service_acl_en = 0;
    field_value = 0;
    cmd = DRV_IOR(FlowTcamLookupCtl_t, FlowTcamLookupCtl_egressAcl0Bitmap_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    if (0 == field_value)
    {
        if (p_reg->egress_port_service_acl_en || p_reg->egress_vlan_service_acl_en)
        {
            ret = CTC_E_INVALID_CONFIG;
        }
    }
    else
    {
        if (p_reg->egress_port_service_acl_en)
        {
            CTC_BIT_SET(service_acl_en, 0);
        }

        if (p_reg->egress_vlan_service_acl_en)
        {
            CTC_BIT_SET(service_acl_en, 1);
        }
    }

    cmd = DRV_IOR(EpeAclQosCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe));

    SetEpeAclQosCtl(V, l3AclNum_f, &epe, p_reg->egress_vlan_acl_num);
    SetEpeAclQosCtl(V, serviceAclQosEn_f, &epe, service_acl_en);
    SetEpeAclQosCtl(V, perPortSelectEn_f, &epe, p_reg->egress_per_port_sel_en);
    SetEpeAclQosCtl(V, downstreamOamFollowAcl_f, &epe, oam_obey_acl_qos);
    SetEpeAclQosCtl(V, oamObeyAclQos_f, &epe, oam_obey_acl_qos);
    SetEpeAclQosCtl(V, layer2TypeUsedAsVlanNum_f, &epe, 1);
    SetEpeAclQosCtl(V, aclVlanRangeEn_f, &epe, 1);

    cmd = DRV_IOW(EpeAclQosCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe));

    return ret;
}


/*
 * init acl register
 */
STATIC int32
_sys_goldengate_acl_init_global_cfg(uint8 lchip, ctc_acl_global_cfg_t* acl_global_cfg)
{
    sys_acl_register_t acl_register;

    CTC_PTR_VALID_CHECK(acl_global_cfg);

    sal_memset(&acl_register, 0, sizeof(acl_register));


    acl_register.ingress_port_service_acl_en = acl_global_cfg->ingress_port_service_acl_en;
    acl_register.ingress_vlan_service_acl_en = acl_global_cfg->ingress_vlan_service_acl_en;
    acl_register.egress_port_service_acl_en  = acl_global_cfg->egress_port_service_acl_en;
    acl_register.egress_vlan_service_acl_en  = acl_global_cfg->egress_vlan_service_acl_en;
    acl_register.egress_per_port_sel_en      = 1;
    acl_register.ipv4_160bit_mode_en = p_gg_acl_master[lchip]->ipv4_160bit_mode_en;

    CTC_ERROR_RETURN(_sys_goldengate_acl_set_register(lchip, &acl_register));

    /* save it. not just for internal usage. like merge */
    sal_memcpy(&(p_gg_acl_master[lchip]->acl_register), &acl_register, sizeof(acl_register));

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_acl_create_rsv_group(uint8 lchip)
{
    ctc_acl_group_info_t hash_group;
    sal_memset(&hash_group, 0, sizeof(hash_group));

    hash_group.type = CTC_ACL_GROUP_TYPE_HASH;

    CTC_ERROR_RETURN(_sys_goldengate_acl_create_group(lchip, CTC_ACL_GROUP_ID_HASH_MAC, &hash_group));
    CTC_ERROR_RETURN(_sys_goldengate_acl_create_group(lchip, CTC_ACL_GROUP_ID_HASH_IPV4, &hash_group));
    CTC_ERROR_RETURN(_sys_goldengate_acl_create_group(lchip, CTC_ACL_GROUP_ID_HASH_IPV6, &hash_group));
    CTC_ERROR_RETURN(_sys_goldengate_acl_create_group(lchip, CTC_ACL_GROUP_ID_HASH_MPLS, &hash_group));
    CTC_ERROR_RETURN(_sys_goldengate_acl_create_group(lchip, CTC_ACL_GROUP_ID_HASH_L2_L3, &hash_group));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_acl_opf_init(uint8 lchip, uint8 opf_type)
{
    uint32               entry_num    = 0;
    uint32               start_offset = 0;
    sys_goldengate_opf_t opf;

    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));

    if (OPF_ACL_VLAN_ACTION == opf_type)
    {
        CTC_ERROR_RETURN(sys_goldengate_ftm_query_table_entry_num(lchip, DsAclVlanActionProfile_t, &entry_num));
        start_offset = SYS_ACL_VLAN_ACTION_RESERVE_NUM;
        entry_num   -= SYS_ACL_VLAN_ACTION_RESERVE_NUM;
    }
    else if (OPF_ACL_AD == opf_type)
    {
        CTC_ERROR_RETURN(sys_goldengate_ftm_query_table_entry_num(lchip, DsAcl_t, &entry_num));
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

int32
sys_goldengate_acl_ftm_acl_cb(uint8 lchip, sys_ftm_specs_info_t* specs_info)
{
    CTC_PTR_VALID_CHECK(specs_info);
    SYS_ACL_INIT_CHECK(lchip);

    specs_info->used_size = (p_gg_acl_master[lchip]->key_count[ACL_ALLOC_COUNT_TCAM_160])
                            + (p_gg_acl_master[lchip]->key_count[ACL_ALLOC_COUNT_TCAM_320]*2)
                            + (p_gg_acl_master[lchip]->key_count[ACL_ALLOC_COUNT_TCAM_640]*4);

    return CTC_E_NONE;
}


int32
sys_goldengate_acl_ftm_acl_flow_cb(uint8 lchip, sys_ftm_specs_info_t* specs_info)
{
    CTC_PTR_VALID_CHECK(specs_info);
    SYS_ACL_INIT_CHECK(lchip);

    specs_info->used_size = (p_gg_acl_master[lchip]->key_count[ACL_ALLOC_COUNT_HASH]);

    return CTC_E_NONE;
}

/*
 * init acl module
 */
int32
sys_goldengate_acl_init(uint8 lchip, ctc_acl_global_cfg_t* acl_global_cfg)
{
    uint8       common_size0;
    uint8       common_size1;
    uint8       common_size2;
    uint8       idx1 = 0;
    uint8       block_id = 0;
    uint16      start_offset = 0;

    uint32      size = 0;
    uint32      pb_sz[ACL_BLOCK_ID_NUM] = {0};
    uint32      entry_num[ACL_BLOCK_ID_NUM][CTC_FPA_KEY_SIZE_NUM] = {{0}};
    uint32      pbr160_num = 0;
    uint32      pbr320_num = 0;
    uint32      ad_entry_size;
    uint32      vedit_entry_size = 0;
    hash_cmp_fn cmp_action       = NULL;
    ctc_spool_t spool;

    LCHIP_CHECK(lchip);
    /* check init */
    if (p_gg_acl_master[lchip])
    {
        return CTC_E_NONE;
    }

    CTC_PTR_VALID_CHECK(acl_global_cfg);

    /* malloc master */
    MALLOC_ZERO(MEM_ACL_MODULE, p_gg_acl_master[lchip], sizeof(sys_acl_master_t));
    if (NULL == p_gg_acl_master[lchip])
    {
        return CTC_E_NO_MEMORY;
    }

    p_gg_acl_master[lchip]->group = ctc_hash_create(1,
                                        SYS_ACL_GROUP_HASH_BLOCK_SIZE,
                                        (hash_key_fn) _sys_goldengate_acl_hash_make_group,
                                        (hash_cmp_fn) _sys_goldengate_acl_hash_compare_group);

    /* init max priority */
    p_gg_acl_master[lchip]->max_entry_priority = 0xFFFFFFFF;

    /* init block_num_max */
    {
        for (block_id = 0; block_id < ACL_IGS_BLOCK_ID_NUM + ACL_EGS_BLOCK_ID_NUM; block_id++)
        {
            size = 0;
            if(block_id == ACL_IGS_BLOCK_ID_NUM)    /*egress*/
            {
                ACL_GET_TABLE_ENTYR_NUM(lchip, DsAclQosL3Key160Egr0_t, &pb_sz[block_id]);
            }
            else
            {
                ACL_GET_TABLE_ENTYR_NUM(lchip, DsAclQosL3Key160Ingr0_t + block_id, &pb_sz[block_id]);
            }
            for(idx1 = 0; idx1 < CTC_FPA_KEY_SIZE_NUM; idx1++)
            {
                /* block id refer to drv_ftm_tcam_key_type_t
                   idx1 refer to DRV_FTM_TCAM_SIZE_XXX */
                CTC_ERROR_RETURN(sys_goldengate_ftm_get_entry_num_by_size(lchip, block_id, idx1, &(entry_num[block_id][idx1])));
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

        /* use pbr key*/
        ACL_GET_TABLE_ENTYR_NUM(lchip, DsLpmTcamIpv4Pbr160Key_t, &pbr160_num);
        ACL_GET_TABLE_ENTYR_NUM(lchip, DsLpmTcamIpv6320Key_t,    &pbr320_num);

        p_gg_acl_master[lchip]->igs_block_num_max = 4;

        p_gg_acl_master[lchip]->egs_block_num_max = 1;

        p_gg_acl_master[lchip]->pbr_block_num_max = 1;
    }


    /* init sort algor / hash table /share pool */
    {
        p_gg_acl_master[lchip]->fpa = fpa_goldengate_create(_sys_goldengate_acl_get_block_by_pe_fpa,
                                     _sys_goldengate_acl_entry_move_hw_fpa,
                                     sizeof(ctc_slistnode_t));
        /* init hash table :
         *    instead of using one linklist, acl use 32 linklist.
         *    hash caculatition is pretty fast, just label_id % hash_size
         */


        p_gg_acl_master[lchip]->entry = ctc_hash_create(1,
                                            SYS_ACL_ENTRY_HASH_BLOCK_SIZE,
                                            (hash_key_fn) _sys_goldengate_acl_hash_make_entry,
                                            (hash_cmp_fn) _sys_goldengate_acl_hash_compare_entry);

        p_gg_acl_master[lchip]->hash_ent_key = ctc_hash_create(1,
                                                   SYS_ACL_ENTRY_HASH_BLOCK_SIZE,
                                                   (hash_key_fn) _sys_goldengate_acl_hash_make_entry_by_key,
                                                   (hash_cmp_fn) _sys_goldengate_acl_hash_compare_entry_by_key);

        CTC_ERROR_RETURN(sys_goldengate_ftm_query_table_entry_num(lchip, DsAcl_t, &ad_entry_size));
        CTC_ERROR_RETURN(sys_goldengate_ftm_query_table_entry_num(lchip, DsAclVlanActionProfile_t, &vedit_entry_size));

        cmp_action = (hash_cmp_fn) _sys_goldengate_acl_hash_compare_action;

        sal_memset(&spool, 0, sizeof(ctc_spool_t));
        spool.lchip = lchip;
        spool.block_num = 1;
        spool.block_size = SYS_ACL_ENTRY_HASH_BLOCK_SIZE_BY_KEY;
        spool.max_count = ad_entry_size;
        spool.user_data_size = sizeof(sys_acl_action_t);
        spool.spool_key = (hash_key_fn) _sys_goldengate_acl_hash_make_action;
        spool.spool_cmp = (hash_cmp_fn) cmp_action;
        p_gg_acl_master[lchip]->ad_spool = ctc_spool_create(&spool);

#define ACL_VLAN_ACTION_SIZE_PER_BUCKET    16

        sal_memset(&spool, 0, sizeof(ctc_spool_t));
        spool.lchip = lchip;
        spool.block_num = 1;
        spool.block_size = vedit_entry_size / ACL_VLAN_ACTION_SIZE_PER_BUCKET;
        spool.max_count = vedit_entry_size;
        spool.user_data_size = sizeof(sys_acl_vlan_edit_t);
        spool.spool_key = (hash_key_fn) _sys_goldengate_acl_hash_make_vlan_edit;
        spool.spool_cmp = (hash_cmp_fn) _sys_goldengate_acl_hash_compare_vlan_edit;
        p_gg_acl_master[lchip]->vlan_edit_spool = ctc_spool_create(&spool);

        /*ACL ingress and egress*/
        for (idx1 = 0; idx1 < ACL_BLOCK_ID_NUM - 1; idx1++)
        {
            p_gg_acl_master[lchip]->block[idx1].fpab.entry_count = pb_sz[idx1];
            p_gg_acl_master[lchip]->block[idx1].fpab.free_count  = pb_sz[idx1];
            start_offset = 0;
            p_gg_acl_master[lchip]->block[idx1].fpab.start_offset[CTC_FPA_KEY_SIZE_160]    = start_offset;
            p_gg_acl_master[lchip]->block[idx1].fpab.sub_entry_count[CTC_FPA_KEY_SIZE_160] = entry_num[idx1][CTC_FPA_KEY_SIZE_160];
            p_gg_acl_master[lchip]->block[idx1].fpab.sub_free_count[CTC_FPA_KEY_SIZE_160]  = entry_num[idx1][CTC_FPA_KEY_SIZE_160];
            p_gg_acl_master[lchip]->block[idx1].fpab.sub_rsv_count[CTC_FPA_KEY_SIZE_160]  = 0;
            start_offset += entry_num[idx1][CTC_FPA_KEY_SIZE_160];
            p_gg_acl_master[lchip]->block[idx1].fpab.start_offset[CTC_FPA_KEY_SIZE_320]    = start_offset;
            p_gg_acl_master[lchip]->block[idx1].fpab.sub_entry_count[CTC_FPA_KEY_SIZE_320] = entry_num[idx1][CTC_FPA_KEY_SIZE_320]/2;
            p_gg_acl_master[lchip]->block[idx1].fpab.sub_free_count[CTC_FPA_KEY_SIZE_320]  = entry_num[idx1][CTC_FPA_KEY_SIZE_320]/2;
            p_gg_acl_master[lchip]->block[idx1].fpab.sub_rsv_count[CTC_FPA_KEY_SIZE_320]  = 0;
            start_offset += entry_num[idx1][CTC_FPA_KEY_SIZE_320];
            p_gg_acl_master[lchip]->block[idx1].fpab.start_offset[CTC_FPA_KEY_SIZE_640]    = start_offset;
            p_gg_acl_master[lchip]->block[idx1].fpab.sub_entry_count[CTC_FPA_KEY_SIZE_640] = entry_num[idx1][CTC_FPA_KEY_SIZE_640]/4;
            p_gg_acl_master[lchip]->block[idx1].fpab.sub_free_count[CTC_FPA_KEY_SIZE_640]  = entry_num[idx1][CTC_FPA_KEY_SIZE_640]/4;
            p_gg_acl_master[lchip]->block[idx1].fpab.sub_rsv_count[CTC_FPA_KEY_SIZE_640]  = 0;

            size = sizeof(sys_acl_entry_t*) * pb_sz[idx1];
            MALLOC_ZERO(MEM_ACL_MODULE, p_gg_acl_master[lchip]->block[idx1].fpab.entries, size);
            if (NULL == p_gg_acl_master[lchip]->block[idx1].fpab.entries && size)
            {
                goto ERROR_FREE_MEM0;
            }
        }
        /*pbr block*/
        {
            idx1 = ACL_BLOCK_ID_NUM - 1;
            p_gg_acl_master[lchip]->block[idx1].fpab.entry_count = pbr160_num + pbr320_num * 2;
            p_gg_acl_master[lchip]->block[idx1].fpab.free_count  = pbr160_num + pbr320_num * 2;
            start_offset = 0;
            p_gg_acl_master[lchip]->block[idx1].fpab.start_offset[CTC_FPA_KEY_SIZE_160]    = start_offset;
            p_gg_acl_master[lchip]->block[idx1].fpab.sub_entry_count[CTC_FPA_KEY_SIZE_160] = pbr160_num;
            p_gg_acl_master[lchip]->block[idx1].fpab.sub_free_count[CTC_FPA_KEY_SIZE_160]  = pbr160_num;
            p_gg_acl_master[lchip]->block[idx1].fpab.sub_rsv_count[CTC_FPA_KEY_SIZE_160]   = pbr160_num;  /*unable to adjust resource*/
            start_offset += pbr160_num;
            p_gg_acl_master[lchip]->block[idx1].fpab.start_offset[CTC_FPA_KEY_SIZE_320]    = start_offset;
            p_gg_acl_master[lchip]->block[idx1].fpab.sub_entry_count[CTC_FPA_KEY_SIZE_320] = pbr320_num;
            p_gg_acl_master[lchip]->block[idx1].fpab.sub_free_count[CTC_FPA_KEY_SIZE_320]  = pbr320_num;
            p_gg_acl_master[lchip]->block[idx1].fpab.sub_rsv_count[CTC_FPA_KEY_SIZE_320]   = pbr320_num;
            start_offset += pbr320_num * 2;
            p_gg_acl_master[lchip]->block[idx1].fpab.start_offset[CTC_FPA_KEY_SIZE_640]    = start_offset;
            p_gg_acl_master[lchip]->block[idx1].fpab.sub_entry_count[CTC_FPA_KEY_SIZE_640] = 0;
            p_gg_acl_master[lchip]->block[idx1].fpab.sub_free_count[CTC_FPA_KEY_SIZE_640]  = 0;
            p_gg_acl_master[lchip]->block[idx1].fpab.sub_rsv_count[CTC_FPA_KEY_SIZE_640]   = 0;
            size = sizeof(sys_acl_entry_t*) * (pbr160_num + pbr320_num * 2);
            MALLOC_ZERO(MEM_ACL_MODULE, p_gg_acl_master[lchip]->block[idx1].fpab.entries, size);
            if (NULL == p_gg_acl_master[lchip]->block[idx1].fpab.entries && size)
            {
                goto ERROR_FREE_MEM0;
            }
        }

        if (!(p_gg_acl_master[lchip]->fpa &&
              p_gg_acl_master[lchip]->entry &&
              p_gg_acl_master[lchip]->hash_ent_key &&
              p_gg_acl_master[lchip]->ad_spool &&
              p_gg_acl_master[lchip]->vlan_edit_spool))
        {
            goto ERROR_FREE_MEM0;
        }
    }

    /* init l4_port_free_cnt */
    p_gg_acl_master[lchip]->l4_port_free_cnt = SYS_ACL_L4_PORT_NUM;
    p_gg_acl_master[lchip]->l4_port_range_num = SYS_ACL_L4_PORT_NUM;

    /* init tcp_flag_free_cnt */
    p_gg_acl_master[lchip]->tcp_flag_free_cnt = SYS_ACL_TCP_FLAG_NUM;

    /* default ipve key use 160 bit (single), mac key use 320 bit  */
    p_gg_acl_master[lchip]->ipv4_160bit_mode_en = 1;

    /* init packet length range */
    CTC_ERROR_RETURN(_sys_goldengate_acl_init_pkt_len_range(lchip));

    /* init entry size */
    {
        common_size0                                  = sizeof(sys_acl_entry_t) - sizeof(sys_acl_key_union_t);
        common_size1                                  = sizeof(sys_acl_hash_key_t) - sizeof(sys_acl_hash_key_union_t);
        p_gg_acl_master[lchip]->entry_size[CTC_ACL_KEY_MAC]       = common_size0 + sizeof(sys_acl_sw_tcam_key_mac_t);
        p_gg_acl_master[lchip]->entry_size[CTC_ACL_KEY_IPV4]      = common_size0 + sizeof(sys_acl_sw_tcam_key_ipv4_t);
        p_gg_acl_master[lchip]->entry_size[CTC_ACL_KEY_IPV6]      = common_size0 + sizeof(sys_acl_sw_tcam_key_ipv6_t);
        p_gg_acl_master[lchip]->entry_size[CTC_ACL_KEY_HASH_MAC]  = common_size0 + common_size1 + sizeof(sys_acl_hash_mac_key_t);
        p_gg_acl_master[lchip]->entry_size[CTC_ACL_KEY_HASH_IPV4] = common_size0 + common_size1 + sizeof(sys_acl_hash_ipv4_key_t);
        p_gg_acl_master[lchip]->entry_size[CTC_ACL_KEY_HASH_IPV6] = common_size0 + common_size1 + sizeof(sys_acl_hash_ipv6_key_t);
        p_gg_acl_master[lchip]->entry_size[CTC_ACL_KEY_HASH_MPLS] = common_size0 + common_size1 + sizeof(sys_acl_hash_mpls_key_t);
        p_gg_acl_master[lchip]->entry_size[CTC_ACL_KEY_HASH_L2_L3]= common_size0 + common_size1 + sizeof(sys_acl_hash_l2_l3_key_t);
        p_gg_acl_master[lchip]->entry_size[CTC_ACL_KEY_PBR_IPV4]  = common_size0 + sizeof(sys_acl_pbr_ipv4_key_t);
        p_gg_acl_master[lchip]->entry_size[CTC_ACL_KEY_PBR_IPV6]  = common_size0 + sizeof(sys_acl_pbr_ipv6_key_t);

        common_size2 = sizeof(sys_acl_key_t) - sizeof(sys_acl_key_union_t);

        p_gg_acl_master[lchip]->key_size[CTC_ACL_KEY_MAC]       = common_size2 + sizeof(sys_acl_sw_tcam_key_mac_t);
        p_gg_acl_master[lchip]->key_size[CTC_ACL_KEY_IPV4]      = common_size2 + sizeof(sys_acl_sw_tcam_key_ipv4_t);
        p_gg_acl_master[lchip]->key_size[CTC_ACL_KEY_IPV6]      = common_size2 + sizeof(sys_acl_sw_tcam_key_ipv6_t);
        p_gg_acl_master[lchip]->key_size[CTC_ACL_KEY_HASH_MAC]  = common_size2 + common_size1 + sizeof(sys_acl_hash_mac_key_t);
        p_gg_acl_master[lchip]->key_size[CTC_ACL_KEY_HASH_IPV4] = common_size2 + common_size1 + sizeof(sys_acl_hash_ipv4_key_t);
        p_gg_acl_master[lchip]->key_size[CTC_ACL_KEY_HASH_IPV6] = common_size2 + common_size1 + sizeof(sys_acl_hash_ipv6_key_t);
        p_gg_acl_master[lchip]->key_size[CTC_ACL_KEY_HASH_MPLS] = common_size2 + common_size1 + sizeof(sys_acl_hash_mpls_key_t);
        p_gg_acl_master[lchip]->key_size[CTC_ACL_KEY_HASH_L2_L3]= common_size2 + common_size1 + sizeof(sys_acl_hash_l2_l3_key_t);
        p_gg_acl_master[lchip]->key_size[CTC_ACL_KEY_PBR_IPV4]  = common_size2 + sizeof(sys_acl_pbr_ipv4_key_t);
        p_gg_acl_master[lchip]->key_size[CTC_ACL_KEY_PBR_IPV6]  = common_size2 + sizeof(sys_acl_pbr_ipv6_key_t);

        p_gg_acl_master[lchip]->hash_action_size = (sizeof(sys_acl_action_t) - sizeof(sys_acl_action_with_chip_t) - sizeof(sys_acl_vlan_edit_t*));
        p_gg_acl_master[lchip]->vlan_edit_size   = sizeof(sys_acl_vlan_edit_t) - sizeof(uint32);
    }

    CTC_ERROR_RETURN(_sys_goldengate_acl_init_global_cfg(lchip, acl_global_cfg));

    SYS_ACL_CREATE_LOCK(lchip);
    CTC_ERROR_RETURN(_sys_goldengate_acl_create_rsv_group(lchip));

    CTC_ERROR_RETURN(_sys_goldengate_acl_opf_init(lchip, OPF_ACL_AD));
    CTC_ERROR_RETURN(_sys_goldengate_acl_opf_init(lchip, OPF_ACL_VLAN_ACTION));

    CTC_ERROR_RETURN(_sys_goldengate_acl_init_vlan_edit(lchip));

    CTC_ERROR_RETURN(sys_goldengate_acl_set_pkt_length_range_en(lchip, 1));  /*enable pkt-len-range default*/

    sys_goldengate_ftm_register_callback(lchip, CTC_FTM_SPEC_ACL, sys_goldengate_acl_ftm_acl_cb);
    sys_goldengate_ftm_register_callback(lchip, CTC_FTM_SPEC_ACL_FLOW, sys_goldengate_acl_ftm_acl_flow_cb);

    CTC_ERROR_RETURN(sys_goldengate_wb_sync_register_cb(lchip, CTC_FEATURE_ACL, sys_goldengate_acl_wb_sync));

    if (CTC_WB_ENABLE && CTC_WB_STATUS(lchip) == CTC_WB_STATUS_RELOADING)
    {
        CTC_ERROR_RETURN(sys_goldengate_acl_wb_restore(lchip));
    }

    return CTC_E_NONE;

 ERROR_FREE_MEM0:

    return CTC_E_ACL_INIT;
}

STATIC int32
_sys_goldengate_acl_free_node_data(void* node_data, void* user_data)
{
    uint32 free_entry_hash = 0;
    sys_acl_entry_t* pe = NULL;
    sys_acl_group_t* pg = NULL;

    if (user_data)
    {
        free_entry_hash = *(uint32*)user_data;
        if (1 == free_entry_hash)
        {
            pe = (sys_acl_entry_t*)node_data;
            if (ACL_ENTRY_IS_TCAM(pe->key.type))
            {
                mem_free(pe->action);
            }
        }
        else if (2 == free_entry_hash)
        {
            pg = (sys_acl_group_t*)node_data;
            ctc_slist_free(pg->entry_list);
        }
    }

    mem_free(node_data);

    return CTC_E_NONE;
}

int32
sys_goldengate_acl_deinit(uint8 lchip)
{
    uint8       idx1;
    uint32      free_entry_hash = 1;

    LCHIP_CHECK(lchip);
    if (NULL == p_gg_acl_master[lchip])
    {
        return CTC_E_NONE;
    }

    /*ACL ingress and egress*/
    for (idx1 = 0; idx1 < ACL_BLOCK_ID_NUM; idx1++)
    {
        mem_free(p_gg_acl_master[lchip]->block[idx1].fpab.entries);
    }

    /*free vlan edit data*/
    ctc_spool_free(p_gg_acl_master[lchip]->vlan_edit_spool);

    /*free ad data*/
    ctc_spool_free(p_gg_acl_master[lchip]->ad_spool);

    /*free acl key*/
    ctc_hash_traverse(p_gg_acl_master[lchip]->entry, (hash_traversal_fn)_sys_goldengate_acl_free_node_data, &free_entry_hash);
    ctc_hash_free(p_gg_acl_master[lchip]->entry);
    ctc_hash_free(p_gg_acl_master[lchip]->hash_ent_key);

    /*free acl group*/
    free_entry_hash = 2;
    ctc_hash_traverse(p_gg_acl_master[lchip]->group, (hash_traversal_fn)_sys_goldengate_acl_free_node_data, &free_entry_hash);
    ctc_hash_free(p_gg_acl_master[lchip]->group);

    fpa_goldengate_free(p_gg_acl_master[lchip]->fpa);

    /*opf free*/
    sys_goldengate_opf_deinit(lchip, OPF_ACL_AD);
    sys_goldengate_opf_deinit(lchip, OPF_ACL_VLAN_ACTION);

    sal_mutex_destroy(p_gg_acl_master[lchip]->mutex);
    mem_free(p_gg_acl_master[lchip]);

    return CTC_E_NONE;
}

/***********************************
   Below For Internal test
 ************************************/

/* Internal function for internal cli */
int32
sys_goldengate_acl_set_global_cfg(uint8 lchip, sys_acl_global_property_t property,uint32 value)
{
    sys_acl_register_t acl_register;
    SYS_ACL_INIT_CHECK(lchip);
    /* get first */
    SYS_ACL_LOCK(lchip);
    sal_memcpy(&acl_register, &(p_gg_acl_master[lchip]->acl_register), sizeof(acl_register));
    SYS_ACL_UNLOCK(lchip);

    /* set user input */
    if (SYS_ACL_IGS_PORT_SERVICE_ACL_EN== property)
    {
        acl_register.ingress_port_service_acl_en = value;
    }
    else if (SYS_ACL_IGS_VLAN_SERVICE_ACL_EN== property)
    {
        acl_register.ingress_vlan_service_acl_en = value;
    }
    else if (SYS_ACL_IGS_VLAN_ACL_NUM== property)
    {
        acl_register.ingress_vlan_acl_num = value;
    }
    else if (SYS_ACL_EGS_PORT_SERVICE_ACL_EN== property)
    {
        acl_register.egress_port_service_acl_en = value;
    }
    else if (SYS_ACL_EGS_VLAN_SERVICE_ACL_EN== property)
    {
        acl_register.egress_vlan_service_acl_en = value;
    }
    else if (SYS_ACL_EGS_VLAN_ACL_NUM== property)
    {
        acl_register.egress_vlan_acl_num = value;
    }
    else
    {
        return CTC_E_INVALID_PARAM;
    }

    acl_register.ipv4_160bit_mode_en = p_gg_acl_master[lchip]->ipv4_160bit_mode_en;

    CTC_ERROR_RETURN(_sys_goldengate_acl_set_register(lchip, &acl_register));

    /* save it for internal usage */
    SYS_ACL_LOCK(lchip);
    sal_memcpy(&(p_gg_acl_master[lchip]->acl_register), &acl_register, sizeof(acl_register));
    SYS_ACL_UNLOCK(lchip);

    return CTC_E_NONE;
}

/*
    mode: 0-IO, 1-DMA
*/
int32
sys_goldengate_acl_set_tcam_mode(uint8 lchip, uint8 mode)
{
    uint8 value = 0;

    SYS_ACL_DBG_INFO(" Set flow tacm access mode:%d  \n", mode);

    value = mode?1:0;
    drv_goldengate_chip_set_write_tcam_mode(1, value);

    return 0;
}

/*
 * Function: sys_goldengate_acl_set_pkt_length_range_en
 *
 * Purpose:
 *     Enable IP packet length range.
 *
 * Parameters:
 *     lchip   - (IN) local chip id.
 *     enable  - (IN) 1: enable; 0: disable.
 *
 * Returns:
 *     CTC_E_INVALID_LOCAL_CHIPID  - Invalid local chip id
 *     CTC_E_NOT_INIT              - ACL not initialized.
 *     CTC_E_NONE                  - Success.
*/
int32
sys_goldengate_acl_set_pkt_length_range_en(uint8 lchip, uint8 enable)
{
    uint32 value = 0;
    uint32 cmd   = 0;

    SYS_ACL_INIT_CHECK(lchip);
    SYS_ACL_DBG_FUNC();


    CTC_MAX_VALUE_CHECK(enable, 1);

    /* enable match pkt length range, l4 port range will only support 4*/
    value = enable ? 0 : 1;
    cmd = DRV_IOW(ParserLayer4AppCtl_t, ParserLayer4AppCtl_onlyUsePortRange_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
    SYS_ACL_LOCK(lchip);
    if(enable)
    {
        p_gg_acl_master[lchip]->acl_register.pkt_len_range_en = 1;
        p_gg_acl_master[lchip]->l4_port_range_num = SYS_ACL_L4_PORT_NUM - 4;
        p_gg_acl_master[lchip]->l4_port_free_cnt = SYS_ACL_L4_PORT_NUM - 4;
    }
    else
    {
        p_gg_acl_master[lchip]->acl_register.pkt_len_range_en = 0;
        p_gg_acl_master[lchip]->l4_port_range_num = SYS_ACL_L4_PORT_NUM;
        p_gg_acl_master[lchip]->l4_port_free_cnt = SYS_ACL_L4_PORT_NUM;
    }
    SYS_ACL_UNLOCK(lchip);

    return CTC_E_NONE;
}


#define _____warmboot_____

int32
_sys_goldengate_acl_wb_mapping_group(sys_wb_acl_group_t* p_wb_acl_group, sys_acl_group_t* p_acl_group, uint8 is_sync)
{
    if (is_sync)
    {
        p_wb_acl_group->lchip = p_acl_group->lchip;
        p_wb_acl_group->group_id = p_acl_group->group_id;
        sal_memcpy(&p_wb_acl_group->group_info, &p_acl_group->group_info, sizeof(sys_acl_group_info_t));
    }
    else
    {
        p_acl_group->lchip = p_wb_acl_group->lchip;
        p_acl_group->group_id = p_wb_acl_group->group_id;
        sal_memcpy(&p_acl_group->group_info, &p_wb_acl_group->group_info, sizeof(sys_acl_group_info_t));
        p_acl_group->entry_list = ctc_slist_new();
        CTC_PTR_VALID_CHECK(p_acl_group->entry_list);
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_acl_wb_sync_group(void* bucket_data, void* user_data)
{
    uint32 max_entry_cnt = 0;
    sys_acl_group_t* p_acl_group = NULL;
    ctc_wb_data_t* p_wb_data = NULL;
    sys_wb_acl_group_t* p_wb_acl_group = NULL;

    max_entry_cnt = CTC_WB_DATA_BUFFER_LENGTH / sizeof(sys_wb_acl_group_t);
    p_acl_group = (sys_acl_group_t*)bucket_data;

    if ((p_acl_group->group_id >= CTC_ACL_GROUP_ID_HASH_MAC) && (p_acl_group->group_id <= CTC_ACL_GROUP_ID_HASH_IPV6))
    {
        return CTC_E_NONE; /* rsv group create when init */
    }

    p_wb_data = (ctc_wb_data_t*)user_data;
    p_wb_acl_group = (sys_wb_acl_group_t*)p_wb_data->buffer + p_wb_data->valid_cnt;

    _sys_goldengate_acl_wb_mapping_group(p_wb_acl_group, p_acl_group, 1);

    if(++p_wb_data->valid_cnt == max_entry_cnt)
    {
        CTC_ERROR_RETURN(ctc_wb_add_entry(p_wb_data));
        p_wb_data->valid_cnt = 0;
    }
    return CTC_E_NONE;
}

int32
_sys_goldengate_acl_wb_mapping_entry(sys_wb_acl_entry_t* p_wb_acl_entry, sys_acl_entry_t* p_acl_entry, uint8 is_sync)
{
    int32 ret = CTC_E_NONE;
    uint8 lchip = 0;
    uint32 cmd = 0;
    uint32 value = 0;
    sys_acl_group_t* p_acl_group = NULL;
    sys_acl_action_t* p_acl_action = NULL;
    sys_acl_action_t* pa_get = NULL;
    sys_acl_vlan_edit_t* p_vlan_edit = NULL;
    sys_acl_vlan_edit_t* pv_get = NULL;
    DsAclVlanActionProfile_m ds_vlan_profile;

    if(is_sync)
    {
        p_wb_acl_entry->lchip = p_acl_entry->lchip;
        sal_memcpy(&p_wb_acl_entry->fpae, &p_acl_entry->fpae, sizeof(ctc_fpa_entry_t));
        p_wb_acl_entry->group_id = p_acl_entry->group->group_id;
        sal_memcpy(&p_wb_acl_entry->action, p_acl_entry->action, sizeof(sys_acl_action_t));
        sal_memcpy(&p_wb_acl_entry->key, &p_acl_entry->key, sizeof(sys_acl_action_t));
    }
    else
    {
        p_acl_entry->lchip = p_wb_acl_entry->lchip;
        lchip = p_acl_entry->lchip;
        sal_memcpy(&p_acl_entry->fpae, &p_wb_acl_entry->fpae, sizeof(ctc_fpa_entry_t));
        _sys_goldengate_acl_get_group_by_gid(lchip, p_wb_acl_entry->group_id, &p_acl_group);
        p_acl_entry->group = p_acl_group;

        p_acl_action = mem_malloc(MEM_ACL_MODULE, sizeof(sys_acl_action_t));
        if(NULL == p_acl_action)
        {
            return CTC_E_NO_MEMORY;
        }
        sal_memset(p_acl_action, 0, sizeof(sys_acl_action_t));
        sal_memcpy(p_acl_action, &p_wb_acl_entry->action, sizeof(sys_acl_action_t));
        p_acl_entry->action = p_acl_action;

        p_vlan_edit = mem_malloc(MEM_ACL_MODULE, sizeof(sys_acl_vlan_edit_t));
        if (NULL == p_vlan_edit)
        {
            mem_free(p_acl_action);
            return CTC_E_NO_MEMORY;
        }
        sal_memset(p_vlan_edit, 0, sizeof(sys_acl_vlan_edit_t));

        sal_memset(&ds_vlan_profile, 0, sizeof(DsAclVlanActionProfile_m));
        cmd = DRV_IOR(DsAclVlanActionProfile_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_wb_acl_entry->action.chip.profile_id, cmd, &ds_vlan_profile));

        p_vlan_edit->svid_sl = GetDsAclVlanActionProfile(V, sVlanIdAction_f, &ds_vlan_profile);
        p_vlan_edit->scos_sl = GetDsAclVlanActionProfile(V, sCosAction_f, &ds_vlan_profile);
        p_vlan_edit->scfi_sl = GetDsAclVlanActionProfile(V, sCfiAction_f, &ds_vlan_profile);
        p_vlan_edit->cvid_sl = GetDsAclVlanActionProfile(V, cVlanIdAction_f, &ds_vlan_profile);
        p_vlan_edit->ccos_sl = GetDsAclVlanActionProfile(V, cCosAction_f, &ds_vlan_profile);
        p_vlan_edit->ccfi_sl = GetDsAclVlanActionProfile(V, cCfiAction_f, &ds_vlan_profile);
        value = GetDsAclVlanActionProfile(V, sTagAction_f, &ds_vlan_profile);

        if (GetDsAclVlanActionProfile(V, stagModifyMode_f, &ds_vlan_profile))
        {
            p_vlan_edit->stag_op = CTC_VLAN_TAG_OP_REP_OR_ADD;
        }
        else if (0 == value)
        {
            p_vlan_edit->stag_op = CTC_VLAN_TAG_OP_NONE;
        }
        else if (1 == value)
        {
            p_vlan_edit->stag_op = CTC_VLAN_TAG_OP_REP;
        }
        else if (2 == value)
        {
            p_vlan_edit->stag_op = CTC_VLAN_TAG_OP_ADD;
        }
        else if (3 == value)
        {
            p_vlan_edit->stag_op = CTC_VLAN_TAG_OP_DEL;
        }

        value = GetDsAclVlanActionProfile(V, cTagAction_f, &ds_vlan_profile);

        if (GetDsAclVlanActionProfile(V, ctagModifyMode_f, &ds_vlan_profile))
        {
            p_vlan_edit->ctag_op = CTC_VLAN_TAG_OP_REP_OR_ADD;
        }
        else if (0 == value)
        {
            p_vlan_edit->ctag_op = CTC_VLAN_TAG_OP_NONE;
        }
        else if (1 == value)
        {
            p_vlan_edit->ctag_op = CTC_VLAN_TAG_OP_REP;
        }
        else if (2 == value)
        {
            p_vlan_edit->ctag_op = CTC_VLAN_TAG_OP_ADD;
        }
        else if (3 == value)
        {
            p_vlan_edit->ctag_op = CTC_VLAN_TAG_OP_DEL;
        }

        p_vlan_edit->profile_id = p_wb_acl_entry->action.chip.profile_id;
        p_vlan_edit->lchip = lchip;
        sal_memcpy(&p_acl_entry->key, &p_wb_acl_entry->key, sizeof(sys_acl_action_t));

        /*restore vlan edit spool regardless of entry type*/
        if (CTC_FLAG_ISSET(p_acl_entry->action->flag, CTC_ACL_ACTION_FLAG_VLAN_EDIT))
        {
            sys_goldengate_opf_t opf;
            sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
            opf.pool_index = 0;
            opf.pool_type  = OPF_ACL_VLAN_ACTION;
            ret = ctc_spool_add(p_gg_acl_master[lchip]->vlan_edit_spool, p_vlan_edit, NULL, &pv_get);
            if (ret < 0)
            {
                ret = CTC_E_SPOOL_ADD_UPDATE_FAILED;
                goto exit;
            }
            else if (CTC_SPOOL_E_OPERATE_MEMORY == ret)
            {
                sys_goldengate_opf_alloc_offset_from_position(lchip, &opf, 1, pv_get->profile_id);
            }
            else
            {
                mem_free(p_vlan_edit);
            }

            p_acl_entry->action->vlan_edit       = pv_get;
            p_acl_entry->action->vlan_edit_valid = 1;
            p_acl_entry->action->chip.profile_id = pv_get->profile_id;
        }

        /*restore tcam entry's action spool*/
        if (ACL_ENTRY_IS_TCAM(p_acl_entry->key.type))
        {
            uint8 block_id = 0;
            SYS_ACL_GET_BLOCK_ID(p_acl_entry, block_id);
            p_gg_acl_master[lchip]->block[block_id].fpab.entries[p_acl_entry->fpae.offset_a] = &p_acl_entry->fpae;
        }
        /*restore hash entry's action spool*/
        if (!ACL_ENTRY_IS_TCAM(p_acl_entry->key.type))
        {
            uint32 opf_type = 0;
            sys_goldengate_opf_t opf_hash;
            sal_memset(&opf_hash, 0, sizeof(sys_goldengate_opf_t));
            CTC_ERROR_GOTO(sys_goldengate_ftm_get_opf_type(lchip, DsAcl_t, &opf_type), ret, exit);
            opf_hash.pool_type = OPF_FTM;
            opf_hash.pool_index = opf_type;
            opf_hash.multiple  = 0;
            opf_hash.reverse = CTC_INGRESS;

            ret = ctc_spool_add(p_gg_acl_master[lchip]->ad_spool, p_acl_action, NULL, &pa_get);
            if (ret < 0)
            {
                ret = CTC_E_SPOOL_ADD_UPDATE_FAILED;
                goto exit;
            }
            else if (CTC_SPOOL_E_OPERATE_MEMORY == ret)
            {
                sys_goldengate_opf_alloc_offset_from_position(lchip, &opf_hash, 1, pa_get->chip.ad_index);
            }
            else
            {
                mem_free(p_acl_action);
            }

            ctc_hash_insert(p_gg_acl_master[lchip]->hash_ent_key, p_acl_entry);

        }

        /* add to group */
        ctc_slist_add_head(p_acl_group->entry_list, &(p_acl_entry->head));

        (p_acl_group->entry_count)++;

    }

return ret;

exit:
    if (p_vlan_edit)
    {
        mem_free(p_vlan_edit);
    }
    if (p_acl_action)
    {
        mem_free(p_acl_action);
    }
    return ret;
}


int32
sys_goldengate_acl_wb_sync_entry(void* bucket_data, void* user_data)
{
    uint32 max_entry_cnt = 0;
    ctc_wb_data_t* p_wb_data = NULL;
    sys_acl_entry_t* p_acl_entry = NULL;
    sys_wb_acl_entry_t* p_wb_acl_entry = NULL;

    max_entry_cnt = CTC_WB_DATA_BUFFER_LENGTH / sizeof(sys_wb_acl_entry_t);
    p_wb_data = (ctc_wb_data_t*)user_data;
    p_acl_entry = (sys_acl_entry_t*)bucket_data;
    p_wb_acl_entry = (sys_wb_acl_entry_t*)p_wb_data->buffer + p_wb_data->valid_cnt;

    _sys_goldengate_acl_wb_mapping_entry(p_wb_acl_entry, p_acl_entry, 1);

    if (++p_wb_data->valid_cnt == max_entry_cnt)
    {
        CTC_ERROR_RETURN(ctc_wb_add_entry(p_wb_data));
        p_wb_data->valid_cnt = 0;
    }
    return CTC_E_NONE;
}

int32
sys_goldengate_acl_wb_sync(uint8 lchip)
{
    int32 ret = CTC_E_NONE;
    int32 loop = 0;
    ctc_wb_data_t wb_data;
    sys_wb_acl_master_t* p_wb_acl_master = NULL;
    sal_memset(&wb_data, 0, sizeof(ctc_wb_data_t));
    wb_data.buffer = mem_malloc(MEM_ACL_MODULE, CTC_WB_DATA_BUFFER_LENGTH);
    if (NULL == wb_data.buffer)
    {
        ret = CTC_E_NO_MEMORY;
        goto done;
    }
    sal_memset(wb_data.buffer, 0, CTC_WB_DATA_BUFFER_LENGTH);

    /*restore acl_master */
    p_wb_acl_master = (sys_wb_acl_master_t*)wb_data.buffer;
    p_wb_acl_master->lchip = lchip;
    for (loop = 0; loop < ACL_BLOCK_ID_NUM; loop++)
    {
        sal_memcpy(&p_wb_acl_master->block[loop], &p_gg_acl_master[lchip]->block[loop], sizeof(sys_wb_acl_block_t));
    }

    sal_memcpy(p_wb_acl_master->tcp_flag, p_gg_acl_master[lchip]->tcp_flag, SYS_ACL_TCP_FLAG_NUM * sizeof(sys_acl_tcp_flag_t));
    sal_memcpy(p_wb_acl_master->l4_port, p_gg_acl_master[lchip]->l4_port, SYS_ACL_L4_PORT_NUM * sizeof(sys_acl_l4port_op_t));

    p_wb_acl_master->l4_port_free_cnt = p_gg_acl_master[lchip]->l4_port_free_cnt;
    p_wb_acl_master->l4_port_range_num = p_gg_acl_master[lchip]->l4_port_range_num;
    p_wb_acl_master->tcp_flag_free_cnt = p_gg_acl_master[lchip]->tcp_flag_free_cnt;

    sal_memcpy(p_wb_acl_master->key_count, p_gg_acl_master[lchip]->key_count, ACL_ALLOC_COUNT_NUM * sizeof(uint32));
    sal_memcpy(&p_wb_acl_master->acl_register, &p_gg_acl_master[lchip]->acl_register, sizeof(sys_acl_register_t));

    CTC_WB_INIT_DATA_T((&wb_data), sys_wb_acl_master_t, CTC_FEATURE_ACL, SYS_WB_APPID_ACL_SUBID_MASTER);
    wb_data.valid_cnt = 1;

    CTC_ERROR_GOTO(ctc_wb_add_entry(&wb_data), ret, done);

    /*restore group*/
    CTC_WB_INIT_DATA_T((&wb_data), sys_wb_acl_group_t, CTC_FEATURE_ACL, SYS_WB_APPID_ACL_SUBID_GROUP);
    CTC_ERROR_GOTO(ctc_hash_traverse(p_gg_acl_master[lchip]->group, sys_goldengate_acl_wb_sync_group, (void*) (&wb_data)), ret, done);
    if (wb_data.valid_cnt > 0)
    {
        CTC_ERROR_GOTO(ctc_wb_add_entry(&wb_data), ret, done);
    }

    /*restore acl entry*/
    CTC_WB_INIT_DATA_T((&wb_data), sys_wb_acl_entry_t, CTC_FEATURE_ACL, SYS_WB_APPID_ACL_SUBID_ENTRY);
    CTC_ERROR_GOTO(ctc_hash_traverse(p_gg_acl_master[lchip]->entry, sys_goldengate_acl_wb_sync_entry, (void*) (&wb_data)), ret, done);
    if (wb_data.valid_cnt > 0)
    {
        CTC_ERROR_GOTO(ctc_wb_add_entry(&wb_data), ret, done);
    }

done:
    if (wb_data.buffer)
    {
        mem_free(wb_data.buffer);
    }
    return ret;
}

int32
sys_goldengate_acl_wb_restore(uint8 lchip)
{
    int32 ret = CTC_E_NONE;
    int32 loop = 0;
    uint16 entry_cnt = 0;
    ctc_wb_query_t wb_query;
    sys_wb_acl_master_t* p_wb_acl_master = NULL;
    sys_wb_acl_group_t* p_wb_acl_group = NULL;
    sys_wb_acl_entry_t* p_wb_acl_entry = NULL;
    sys_acl_group_t* p_acl_group = NULL;
    sys_acl_entry_t* p_acl_entry = NULL;
    sal_memset(&wb_query, 0, sizeof(ctc_wb_query_t));
    wb_query.buffer = mem_malloc(MEM_ACL_MODULE, CTC_WB_DATA_BUFFER_LENGTH);
    if (NULL == wb_query.buffer)
    {
        ret = CTC_E_NO_MEMORY;
        goto done;
    }
    sal_memset(wb_query.buffer, 0, CTC_WB_DATA_BUFFER_LENGTH);

    /*restore acl master*/
    CTC_WB_INIT_QUERY_T((&wb_query), sys_wb_acl_master_t, CTC_FEATURE_ACL, SYS_WB_APPID_ACL_SUBID_MASTER);
    CTC_ERROR_GOTO((ctc_wb_query_entry(&wb_query)), ret, done);
    if (1 != wb_query.valid_cnt || 1 != wb_query.is_end)
    {
        CTC_WB_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "query acl master error! valid_cnt: %d, is_end: %d.\n", wb_query.valid_cnt, wb_query.is_end);
        goto done;
    }

    p_wb_acl_master = (sys_wb_acl_master_t*)wb_query.buffer;

    for(loop = 0; loop < ACL_BLOCK_ID_NUM; loop++)
    {
        sal_memcpy(&p_gg_acl_master[lchip]->block[loop], &p_wb_acl_master->block[loop], sizeof(sys_wb_acl_block_t));
    }
    sal_memcpy(p_gg_acl_master[lchip]->tcp_flag, p_wb_acl_master->tcp_flag, SYS_ACL_TCP_FLAG_NUM * sizeof(sys_acl_tcp_flag_t));
    sal_memcpy(p_gg_acl_master[lchip]->l4_port, p_wb_acl_master->l4_port, SYS_ACL_L4_PORT_NUM * sizeof(sys_acl_l4port_op_t));

    p_gg_acl_master[lchip]->l4_port_free_cnt = p_wb_acl_master->l4_port_free_cnt;
    p_gg_acl_master[lchip]->l4_port_range_num = p_wb_acl_master->l4_port_range_num;
    p_gg_acl_master[lchip]->tcp_flag_free_cnt = p_wb_acl_master->tcp_flag_free_cnt;

    sal_memcpy(p_gg_acl_master[lchip]->key_count, p_wb_acl_master->key_count, ACL_ALLOC_COUNT_NUM * sizeof(uint32));
    sal_memcpy(&p_gg_acl_master[lchip]->acl_register, &p_wb_acl_master->acl_register, sizeof(sys_acl_register_t));

    /*restore acl group*/
    CTC_WB_INIT_QUERY_T((&wb_query), sys_wb_acl_group_t, CTC_FEATURE_ACL, SYS_WB_APPID_ACL_SUBID_GROUP);
    CTC_WB_QUERY_ENTRY_BEGIN((&wb_query));
        p_acl_group = mem_malloc(MEM_ACL_MODULE, sizeof(sys_acl_group_t));
        if (NULL == p_acl_group)
        {
            ret = CTC_E_NO_MEMORY;
            goto done;
        }
        sal_memset(p_acl_group, 0, sizeof(sys_acl_group_t));
        p_wb_acl_group = (sys_wb_acl_group_t*)wb_query.buffer + entry_cnt++;
        CTC_ERROR_GOTO(_sys_goldengate_acl_wb_mapping_group(p_wb_acl_group, p_acl_group, 0), ret, done);
        ctc_hash_insert(p_gg_acl_master[lchip]->group, (void*) p_acl_group);
    CTC_WB_QUERY_ENTRY_END((&wb_query));

    /*restore acl entry*/
    CTC_WB_INIT_QUERY_T((&wb_query), sys_wb_acl_entry_t, CTC_FEATURE_ACL, SYS_WB_APPID_ACL_SUBID_ENTRY);
    CTC_WB_QUERY_ENTRY_BEGIN((&wb_query));
        p_acl_entry = mem_malloc(MEM_ACL_MODULE, sizeof(sys_acl_entry_t));
        if (NULL == p_acl_entry)
        {
            ret = CTC_E_NO_MEMORY;
            goto done;
        }
        sal_memset(p_acl_entry, 0, sizeof(sys_acl_entry_t));
        p_wb_acl_entry = (sys_wb_acl_entry_t*)wb_query.buffer + entry_cnt++;
        CTC_ERROR_GOTO(_sys_goldengate_acl_wb_mapping_entry(p_wb_acl_entry, p_acl_entry, 0), ret, done);
        ctc_hash_insert(p_gg_acl_master[lchip]->entry, (void*) p_acl_entry);
    CTC_WB_QUERY_ENTRY_END((&wb_query));

done:
    if(wb_query.buffer)
    {
        mem_free(wb_query.buffer);
    }
    return ret;
}


/**
   @file sys_greatbelt_acl.c

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

#include "sys_greatbelt_opf.h"
#include "sys_greatbelt_chip.h"
#include "sys_greatbelt_parser.h"
#include "sys_greatbelt_stats.h"
#include "sys_greatbelt_qos_policer.h"
#include "sys_greatbelt_acl.h"
#include "sys_greatbelt_queue_enq.h"
#include "sys_greatbelt_nexthop_api.h"
#include "sys_greatbelt_nexthop.h"
#include "sys_greatbelt_internal_port.h"
#include "sys_greatbelt_mirror.h"
#include "sys_greatbelt_learning_aging.h"
#include "sys_greatbelt_ftm.h"
#include "sys_greatbelt_l2_fdb.h"

#include "greatbelt/include/drv_tbl_reg.h"
#include "greatbelt/include/drv_io.h"


#define SYS_ACL_TCP_FLAG_NUM            4
#define SYS_ACL_L4_PORT_NUM             8
#define SYS_ACL_L4_PORT_REF_CNT         0xFFFF
#define SYS_ACL_TCP_FALG_REF_CNT        0xFFFF
#define SYS_ACL_MAX_SESSION_NUM         4

#define SYS_ACL_ENTRY_OP_FLAG_ADD       1
#define SYS_ACL_ENTRY_OP_FLAG_DELETE    2

#define SYS_ACL_TCAM_ALLOC_NUM          6    /* ipv4(mac,mpls) + ipv6 + pbr_ipv4 + pbr_ipv6 + egress ipv4(mac,mpls) + egress ipv6*/
#define SYS_ACL_TCAM_RAW_NUM            7    /* mac + ipv4 + ipv6 + pbr_ipv4 + pbr_ipv6 + egress ipv4(mac,mpls) + egress ipv6 */

#define SYS_ACL_BLOCK_NUM_MAX           4    /* max block num */

#define IPV4_ICMP_PROTOCOL              1
#define IPV4_IGMP_PROTOCOL              2
#define TCP_PROTOCOL                    6
#define UDP_PROTOCOL                    17
#define IPV6_ICMP_PROTOCOL              58

#define SYS_ACL_INVALID_QOS_POLICY      7

#define SYS_ACL_GROUP_HASH_BLOCK_SIZE           32
#define SYS_ACL_ENTRY_HASH_BLOCK_SIZE           32
#define SYS_ACL_ENTRY_HASH_BLOCK_SIZE_BY_KEY    1024


#define SYS_ACL_VLAN_ID_CHECK(vlan_id)           \
    {                                            \
        if ((vlan_id) > CTC_MAX_VLAN_ID) {       \
            return CTC_E_VLAN_INVALID_VLAN_ID; } \
    }                                            \

struct sys_acl_l4port_op_s
{
    uint16 ref;
    uint8  op_dest_port;
    uint8  rsv;

    uint16 port_min;
    uint16 port_max;
};
typedef struct sys_acl_l4port_op_s   sys_acl_l4port_op_t;

struct sys_acl_tcp_flag_s
{
    uint16 ref;

    uint8  match_any; /* 0: match all , 1: match any */
    uint8  tcp_flags; /* bitmap of CTC_ACLQOS_TCP_XXX_FLAG */
};
typedef struct sys_acl_tcp_flag_s   sys_acl_tcp_flag_t;

struct sys_acl_register_s
{
    uint8  ingress_use_mapped_vlan;   /*i*/
    uint8  merge;                     /*ie*/
    uint8  trill_use_ipv6_key;        /*ie*/
    uint8  arp_use_ipv6_key;          /*ie*/

    uint8  ingress_port_service_acl_en;
    uint8  ingress_vlan_service_acl_en;
    uint8  egress_port_service_acl_en;
    uint8  egress_vlan_service_acl_en;

    uint8  non_ip_mpls_use_ipv6_key;  /*ie*/
    uint8  hash_acl_en;               /*i*/
    uint8  rsv0[2];

    uint32 hash_mac_key_flag;         /*i*/
    uint32 hash_ipv4_key_flag;        /*i*/


    uint32 priority_bitmap_of_stats; /*not regist. sw concept*/

#if 0
    uint8  stacking_disable_acl;      /*e*/
    uint8  oam_obey_acl_discard;      /*e*/
    uint8  route_obey_stp;            /*i*/
    uint8  acl_hash_lookup_en;        /*i*/
    uint8  l4_dest_port_overwrite;    /*i*/
    uint8  oam_obey_acl_qos;          /*ie*/
#endif
};
typedef struct sys_acl_register_s   sys_acl_register_t;

struct sys_acl_master_s
{
    ctc_hash_t          * group;                            /* Hash table save group by gid.*/
    ctc_fpa_t           * fpa;                              /* Fast priority arrangement. no array. */
    ctc_hash_t          * entry;                            /* Hash table save hash|tcam entry by eid */
    ctc_hash_t          * hash_ent_key;                     /* Hash table save hash entry by key */
    ctc_spool_t         * ad_spool; /* Share pool save hash action  */
    sys_acl_block_t     block[SYS_ACL_TCAM_RAW_NUM][SYS_ACL_BLOCK_NUM_MAX];

    uint32              max_entry_priority;

    sys_acl_tcp_flag_t  tcp_flag[SYS_ACL_TCP_FLAG_NUM];
    sys_acl_l4port_op_t l4_port[SYS_ACL_L4_PORT_NUM];
    uint8               l4_port_free_cnt;
    uint8               tcp_flag_free_cnt;
    uint8               alloc_id[CTC_ACL_KEY_NUM];
    uint8               block_num_max;         /* max number of block: 4 */

    uint8               tcam_alloc_num;         /* Allocation number of entry key*/
    uint8               entry_size[CTC_ACL_KEY_NUM];
    uint8               key_size[CTC_ACL_KEY_NUM];
    uint8               hash_action_size;


    uint8               hash_base;

 /*    uint16                      ptr_key_index[CTC_ACL_KEY_NUM]; // pointer to key index of specific keytype */

    sys_acl_register_t acl_register;

    uint8               white_list;
    uint8               egs_ipv4_alloc_id;
    uint8               egs_ipv6_alloc_id;
    sal_mutex_t        *mutex;
};
typedef struct sys_acl_master_s   sys_acl_master_t;
sys_acl_master_t* acl_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};

#define SYS_ACL_DBG_OUT(level, FMT, ...) \
    CTC_DEBUG_OUT(acl, acl, ACL_SYS, level, FMT, ## __VA_ARGS__)

#define SYS_ACL_DBG_FUNC() \
    SYS_ACL_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "  %% FUNC: %s()\n", __FUNCTION__)

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
        if (NULL == acl_master[lchip]) {    \
            return CTC_E_NOT_INIT; }        \
    } while(0)

#define SYS_ACL_CREATE_LOCK(lchip)                      \
    do                                                  \
    {                                                   \
        sal_mutex_create(&acl_master[lchip]->mutex);    \
        if (NULL == acl_master[lchip]->mutex)           \
        {                                               \
            CTC_ERROR_RETURN(CTC_E_FAIL_CREATE_MUTEX);  \
        }                                               \
    } while (0)

#define SYS_ACL_LOCK(lchip) \
    sal_mutex_lock(acl_master[lchip]->mutex)

#define SYS_ACL_UNLOCK(lchip) \
    sal_mutex_unlock(acl_master[lchip]->mutex)

#define CTC_ERROR_RETURN_ACL_UNLOCK(op) \
    do \
    { \
        int32 rv = (op); \
        if (rv < 0) \
        { \
            sal_mutex_unlock(acl_master[lchip]->mutex); \
            return rv; \
        } \
    } while (0)

/* input rsv group id , return failed */
#define SYS_ACL_IS_RSV_GROUP(gid) \
    ((gid) > CTC_ACL_GROUP_ID_NORMAL && (gid) < (CTC_ACL_GROUP_ID_MAX))

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

#define SYS_ACL_CHECK_GROUP_PRIO(priority)           \
    {                                                \
        if (priority >= acl_master[lchip]->block_num_max) { \
            return CTC_E_ACL_GROUP_PRIORITY; }       \
    }

#define SYS_ACL_CHECK_ENTRY_ID(eid) \
    {                               \
        ;                           \
    }

#define SYS_ACL_CHECK_ENTRY_PRIO(priority)               \
    {                                                    \
        if (priority > acl_master[lchip]->max_entry_priority) { \
            return CTC_E_ACL_GROUP_PRIORITY; }           \
    }

#define SYS_ACL_SERVICE_ID_CHECK(sid)    SYS_LOGIC_PORT_CHECK(sid)

#define SYS_ACL_CHECK_GROUP_UNEXIST(pg)     \
    {                                       \
        if (!pg)                            \
        {                                   \
            SYS_ACL_UNLOCK(lchip);          \
            return CTC_E_ACL_GROUP_UNEXIST; \
        }                                   \
    }

#define SYS_ACL_CHECK_SUPPORT_STATS(flag, block_id)                              \
    {                                                                            \
        if ((!CTC_IS_BIT_SET(acl_master[lchip]->acl_register.priority_bitmap_of_stats, block_id)) && \
            (CTC_FLAG_ISSET(flag, CTC_ACL_ACTION_FLAG_STATS)))                   \
        {                                                                        \
            SYS_ACL_UNLOCK(lchip);                                               \
            return CTC_E_ACL_STATS_NOT_SUPPORT;                                  \
        }                                                                        \
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

#define ACL_ENTRY_IS_TCAM(type)    ((CTC_ACL_KEY_HASH_MAC != type) && (CTC_ACL_KEY_HASH_IPV4 != type))


#define  DUMP_IPV4(pk)                                                   \
    {                                                                    \
        uint32 addr;                                                     \
        char   ip_addr[16];                                              \
        if (pk->flag.ip_sa)                                              \
        {                                                                \
            if (0xFFFFFFFF == pk->ip_sa_mask)                            \
            {                                                            \
                SYS_ACL_DBG_DUMP("  ip-sa host");                        \
                addr = sal_ntohl(pk->ip_sa);                             \
                sal_inet_ntop(AF_INET, &addr, ip_addr, sizeof(ip_addr)); \
                SYS_ACL_DBG_DUMP(" %s", ip_addr);                        \
            }                                                            \
            else if (0 == pk->ip_sa_mask)                                \
            {                                                            \
                SYS_ACL_DBG_DUMP("  ip-sa any");                         \
            }                                                            \
            else                                                         \
            {                                                            \
                SYS_ACL_DBG_DUMP("  ip-sa");                             \
                addr = sal_ntohl(pk->ip_sa);                             \
                sal_inet_ntop(AF_INET, &addr, ip_addr, sizeof(ip_addr)); \
                SYS_ACL_DBG_DUMP(" %s", ip_addr);                        \
                addr = sal_ntohl(pk->ip_sa_mask);                        \
                sal_inet_ntop(AF_INET, &addr, ip_addr, sizeof(ip_addr)); \
                SYS_ACL_DBG_DUMP("  %s", ip_addr);                       \
            }                                                            \
        }                                                                \
        else                                                             \
        {                                                                \
            SYS_ACL_DBG_DUMP("  ip-sa any");                             \
        }                                                                \
                                                                         \
        if (pk->flag.ip_da)                                              \
        {                                                                \
            if (0xFFFFFFFF == pk->ip_da_mask)                            \
            {                                                            \
                SYS_ACL_DBG_DUMP(",  ip-da host");                       \
                addr = sal_ntohl(pk->ip_da);                             \
                sal_inet_ntop(AF_INET, &addr, ip_addr, sizeof(ip_addr)); \
                SYS_ACL_DBG_DUMP(" %s", ip_addr);                        \
            }                                                            \
            else if (0 == pk->ip_da_mask)                                \
            {                                                            \
                SYS_ACL_DBG_DUMP(",  ip-da any");                        \
            }                                                            \
            else                                                         \
            {                                                            \
                SYS_ACL_DBG_DUMP(",  ip-da");                            \
                addr = sal_ntohl(pk->ip_da);                             \
                sal_inet_ntop(AF_INET, &addr, ip_addr, sizeof(ip_addr)); \
                SYS_ACL_DBG_DUMP(" %s", ip_addr);                        \
                addr = sal_ntohl(pk->ip_da_mask);                        \
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

#define    DUMP_IPV6(pk)                                                                   \
    {                                                                                      \
        char        buf[CTC_IPV6_ADDR_STR_LEN];                                            \
        ipv6_addr_t ipv6_addr;                                                             \
        sal_memset(buf, 0, CTC_IPV6_ADDR_STR_LEN);                                         \
        sal_memset(ipv6_addr, 0, sizeof(ipv6_addr_t));                                     \
        if (pk->flag.ip_sa)                                                                \
        {                                                                                  \
            if ((0xFFFFFFFF == pk->ip_sa_mask[0]) && (0xFFFFFFFF == pk->ip_sa_mask[1])     \
                && (0xFFFFFFFF == pk->ip_sa_mask[2]) && (0xFFFFFFFF == pk->ip_sa_mask[3])) \
            {                                                                              \
                SYS_ACL_DBG_DUMP("  ip-sa host ");                                         \
                                                                                           \
                ipv6_addr[0] = sal_htonl(pk->ip_sa[0]);                                    \
                ipv6_addr[1] = sal_htonl(pk->ip_sa[1]);                                    \
                ipv6_addr[2] = sal_htonl(pk->ip_sa[2]);                                    \
                ipv6_addr[3] = sal_htonl(pk->ip_sa[3]);                                    \
                                                                                           \
                sal_inet_ntop(AF_INET6, ipv6_addr, buf, CTC_IPV6_ADDR_STR_LEN);            \
                SYS_ACL_DBG_DUMP("%20s", buf);                                             \
            }                                                                              \
            else if ((0 == pk->ip_sa_mask[0]) && (0 == pk->ip_sa_mask[1])                  \
                     && (0 == pk->ip_sa_mask[2]) && (0 == pk->ip_sa_mask[3]))              \
            {                                                                              \
                SYS_ACL_DBG_DUMP("  ip-sa any");                                           \
            }                                                                              \
            else                                                                           \
            {                                                                              \
                SYS_ACL_DBG_DUMP("  ip-sa");                                               \
                ipv6_addr[0] = sal_htonl(pk->ip_sa[0]);                                    \
                ipv6_addr[1] = sal_htonl(pk->ip_sa[1]);                                    \
                ipv6_addr[2] = sal_htonl(pk->ip_sa[2]);                                    \
                ipv6_addr[3] = sal_htonl(pk->ip_sa[3]);                                    \
                sal_inet_ntop(AF_INET6, ipv6_addr, buf, CTC_IPV6_ADDR_STR_LEN);            \
                SYS_ACL_DBG_DUMP("%20s ", buf);                                            \
                                                                                           \
                ipv6_addr[0] = sal_htonl(pk->ip_sa_mask[0]);                               \
                ipv6_addr[1] = sal_htonl(pk->ip_sa_mask[1]);                               \
                ipv6_addr[2] = sal_htonl(pk->ip_sa_mask[2]);                               \
                ipv6_addr[3] = sal_htonl(pk->ip_sa_mask[3]);                               \
                                                                                           \
                sal_inet_ntop(AF_INET6, ipv6_addr, buf, CTC_IPV6_ADDR_STR_LEN);            \
                SYS_ACL_DBG_DUMP("%20s", buf);                                             \
            }                                                                              \
        }                                                                                  \
        else                                                                               \
        {                                                                                  \
            SYS_ACL_DBG_DUMP("  ip-sa any");                                               \
        }                                                                                  \
                                                                                           \
        sal_memset(buf, 0, CTC_IPV6_ADDR_STR_LEN);                                         \
                                                                                           \
        if (pk->flag.ip_da)                                                                \
        {                                                                                  \
            if ((0xFFFFFFFF == pk->ip_da_mask[0]) && (0xFFFFFFFF == pk->ip_da_mask[1])     \
                && (0xFFFFFFFF == pk->ip_da_mask[2]) && (0xFFFFFFFF == pk->ip_da_mask[3])) \
            {                                                                              \
                SYS_ACL_DBG_DUMP(",  ip-da host ");                                        \
                                                                                           \
                ipv6_addr[0] = sal_htonl(pk->ip_da[0]);                                    \
                ipv6_addr[1] = sal_htonl(pk->ip_da[1]);                                    \
                ipv6_addr[2] = sal_htonl(pk->ip_da[2]);                                    \
                ipv6_addr[3] = sal_htonl(pk->ip_da[3]);                                    \
                                                                                           \
                sal_inet_ntop(AF_INET6, ipv6_addr, buf, CTC_IPV6_ADDR_STR_LEN);            \
                SYS_ACL_DBG_DUMP("%20s", buf);                                             \
            }                                                                              \
            else if ((0 == pk->ip_da_mask[0]) && (0 == pk->ip_da_mask[1])                  \
                     && (0 == pk->ip_da_mask[2]) && (0 == pk->ip_da_mask[3]))              \
            {                                                                              \
                SYS_ACL_DBG_DUMP(",  ip-da any");                                          \
            }                                                                              \
            else                                                                           \
            {                                                                              \
                SYS_ACL_DBG_DUMP(",  ip-da");                                              \
                ipv6_addr[0] = sal_htonl(pk->ip_da[0]);                                    \
                ipv6_addr[1] = sal_htonl(pk->ip_da[1]);                                    \
                ipv6_addr[2] = sal_htonl(pk->ip_da[2]);                                    \
                ipv6_addr[3] = sal_htonl(pk->ip_da[3]);                                    \
                sal_inet_ntop(AF_INET6, ipv6_addr, buf, CTC_IPV6_ADDR_STR_LEN);            \
                SYS_ACL_DBG_DUMP("%20s ", buf);                                            \
                                                                                           \
                ipv6_addr[0] = sal_htonl(pk->ip_da_mask[0]);                               \
                ipv6_addr[1] = sal_htonl(pk->ip_da_mask[1]);                               \
                ipv6_addr[2] = sal_htonl(pk->ip_da_mask[2]);                               \
                ipv6_addr[3] = sal_htonl(pk->ip_da_mask[3]);                               \
                                                                                           \
                sal_inet_ntop(AF_INET6, ipv6_addr, buf, CTC_IPV6_ADDR_STR_LEN);            \
                SYS_ACL_DBG_DUMP("%20s", buf);                                             \
            }                                                                              \
        }                                                                                  \
        else                                                                               \
        {                                                                                  \
            SYS_ACL_DBG_DUMP(",  ip-da any");                                              \
        }                                                                                  \
        SYS_ACL_DBG_DUMP("\n");                                                            \
    }

#define    DUMP_L2_0(pk)                                                                  \
    SYS_ACL_DBG_DUMP("  mac-sa");                                                         \
    if (pk->flag.mac_sa)                                                                  \
    {                                                                                     \
        if ((0xFFFF == *(uint16 *) &pk->mac_sa_mask[0]) &&                                \
            (0xFFFFFFFF == *(uint32 *) &pk->mac_sa_mask[2]))                              \
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
    if (pk->flag.mac_da)                                                                  \
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
    if (pk->flag.cvlan)                                                                   \
    {                                                                                     \
        SYS_ACL_DBG_DUMP("  c-vlan 0x%x  mask 0x%0x,", pk->cvlan, pk->cvlan_mask);        \
    }                                                                                     \
    if (pk->flag.ctag_cos)                                                                \
    {                                                                                     \
        SYS_ACL_DBG_DUMP("  c-cos %u  mask %u,", pk->ctag_cos, pk->ctag_cos_mask);        \
    }                                                                                     \
    if (pk->flag.ctag_cfi)                                                                \
    {                                                                                     \
        SYS_ACL_DBG_DUMP("  c-cfi %u,", pk->ctag_cfi);                                    \
    }                                                                                     \
    if ((pk->flag.cvlan) +                                                                \
        (pk->flag.ctag_cos) +                                                             \
        (pk->flag.ctag_cfi))                                                              \
    {                                                                                     \
        SYS_ACL_DBG_DUMP("\n");                                                           \
    }                                                                                     \
                                                                                          \
                                                                                          \
    if (pk->flag.svlan)                                                                   \
    {                                                                                     \
        SYS_ACL_DBG_DUMP("  s-vlan 0x%x  mask 0x%0x,", pk->svlan, pk->svlan_mask);        \
    }                                                                                     \
    if (pk->flag.stag_cos)                                                                \
    {                                                                                     \
        SYS_ACL_DBG_DUMP("  s-cos %u  mask %u,", pk->stag_cos, pk->stag_cos_mask);        \
    }                                                                                     \
    if (pk->flag.stag_cfi)                                                                \
    {                                                                                     \
        SYS_ACL_DBG_DUMP("  s-cfi %u,", pk->stag_cfi);                                    \
    }                                                                                     \
    if ((pk->flag.svlan) +                                                                \
        (pk->flag.stag_cos) +                                                             \
        (pk->flag.stag_cfi))                                                              \
    {                                                                                     \
        SYS_ACL_DBG_DUMP("\n");                                                           \
    }                                                                                     \


#define    DUMP_L2_1(pk)                                                                  \
    if (pk->flag.l2_type)                                                                 \
    {                                                                                     \
        SYS_ACL_DBG_DUMP("  l2_type %u  mask %u,", pk->l2_type, pk->l2_type_mask);        \
    }                                                                                     \
    if (pk->flag.eth_type)                                                                \
    {                                                                                     \
        SYS_ACL_DBG_DUMP("  eth-type 0x%x  mask 0x%x,", pk->eth_type, pk->eth_type_mask); \
    }                                                                                     \
    if (pk->flag.cvlan_id_valid)                                                          \
    {                                                                                     \
        SYS_ACL_DBG_DUMP("  ctag_valid %u,", pk->cvlan_id_valid);                         \
    }                                                                                     \
    if (pk->flag.svlan_id_valid)                                                          \
    {                                                                                     \
        SYS_ACL_DBG_DUMP("  stag_valid %u,", pk->svlan_id_valid);                         \
    }                                                                                     \
    if ((pk->flag.eth_type) +                                                             \
        (pk->flag.l2_type) +                                                              \
        (pk->flag.cvlan_id_valid) +                                                       \
        (pk->flag.svlan_id_valid))                                                        \
    {                                                                                     \
        SYS_ACL_DBG_DUMP("\n");                                                           \
    }

#define    DUMP_PBR_L2_1(pk)                                                              \
    if (pk->flag.l2_type)                                                                 \
    {                                                                                     \
        SYS_ACL_DBG_DUMP("  l2_type %u  mask %u,", pk->l2_type, pk->l2_type_mask);        \
    }                                                                                     \
    if (pk->flag.eth_type)                                                                \
    {                                                                                     \
        SYS_ACL_DBG_DUMP("  eth-type 0x%x  mask 0x%x,", pk->eth_type, pk->eth_type_mask); \
    }                                                                                     \
    if ((pk->flag.eth_type) +                                                             \
        (pk->flag.l2_type))                                                               \
    {                                                                                     \
        SYS_ACL_DBG_DUMP("\n");                                                           \
    }

#define   DUMP_L3(pk)                                                     \
    if (pk->flag.dscp)                                                    \
    {                                                                     \
        SYS_ACL_DBG_DUMP("  dscp %u  mask %u,", pk->dscp, pk->dscp_mask); \
    }                                                                     \
    if (pk->flag.frag_info)                                               \
    {                                                                     \
        SYS_ACL_DBG_DUMP("  frag-info %u  mask %u,", pk->frag_info,       \
                         pk->frag_info_mask);                             \
    }                                                                     \
    if (pk->flag.ip_option)                                               \
    {                                                                     \
        SYS_ACL_DBG_DUMP("  ip-option %u,", pk->ip_option);               \
    }                                                                     \
    if ((pk->flag.dscp) +                                                 \
        (pk->flag.frag_info) +                                            \
        (pk->flag.ip_option))                                             \
    {                                                                     \
        SYS_ACL_DBG_DUMP("\n");                                           \
    }                                                                     \
                                                                          \
    if (pk->flag.ip_hdr_error)                                            \
    {                                                                     \
        SYS_ACL_DBG_DUMP("  ip-hdr-error %u,", pk->ip_hdr_error);         \
    }                                                                     \
    if (pk->flag.routed_packet)                                           \
    {                                                                     \
        SYS_ACL_DBG_DUMP("  routed-packet %u,", pk->routed_packet);       \
    }                                                                     \
    if (pk->flag.l4info_mapped)                                           \
    {                                                                     \
        SYS_ACL_DBG_DUMP("  l4info_mapped 0x%x  mask 0x%x,",              \
                         pk->l4info_mapped, pk->l4info_mapped_mask);      \
    }                                                                     \
    if ((pk->flag.ip_hdr_error) +                                         \
        (pk->flag.routed_packet) +                                        \
        (pk->flag.l4info_mapped))                                         \
    {                                                                     \
        SYS_ACL_DBG_DUMP("\n");                                           \
    }

#define   DUMP_L3_PBR(pk)                                                 \
    if (pk->flag.dscp)                                                    \
    {                                                                     \
        SYS_ACL_DBG_DUMP("  dscp %u  mask %u,", pk->dscp, pk->dscp_mask); \
    }                                                                     \
    if (pk->flag.frag_info)                                               \
    {                                                                     \
        SYS_ACL_DBG_DUMP("  frag-info %u  mask %u,", pk->frag_info,       \
                         pk->frag_info_mask);                             \
    }                                                                     \
    if ((pk->flag.dscp) +                                                 \
        (pk->flag.frag_info))                                             \
    {                                                                     \
        SYS_ACL_DBG_DUMP("\n");                                           \
    }                                                                     \
                                                                          \
    if (pk->flag.l4info_mapped)                                           \
    {                                                                     \
        SYS_ACL_DBG_DUMP("  l4info_mapped 0x%x  mask 0x%x,",              \
                         pk->l4info_mapped, pk->l4info_mapped_mask);      \
    }                                                                     \
    if (pk->flag.l4info_mapped)                                           \
    {                                                                     \
        SYS_ACL_DBG_DUMP("\n");                                           \
    }

#define   DUMP_L3_PBR_IPV6(pk)                                            \
    if (pk->flag.dscp)                                                    \
    {                                                                     \
        SYS_ACL_DBG_DUMP("  dscp %u  mask %u,", pk->dscp, pk->dscp_mask); \
    }                                                                     \
    if (pk->flag.frag_info)                                               \
    {                                                                     \
        SYS_ACL_DBG_DUMP("  frag-info %u  mask %u,", pk->frag_info,       \
                         pk->frag_info_mask);                             \
    }                                                                     \
    if ((pk->flag.dscp) +                                                 \
        (pk->flag.frag_info))                                             \
    {                                                                     \
        SYS_ACL_DBG_DUMP("\n");                                           \
    }                                                                     \
                                                                          \
    if (pk->flag.ip_hdr_error)                                            \
    {                                                                     \
        SYS_ACL_DBG_DUMP("  ip-hdr-error %u,", pk->ip_hdr_error);         \
    }                                                                     \
    if (pk->flag.l4info_mapped)                                           \
    {                                                                     \
        SYS_ACL_DBG_DUMP("  l4info_mapped 0x%x  mask 0x%x,",              \
                         pk->l4info_mapped, pk->l4info_mapped_mask);      \
    }                                                                     \
    if ((pk->flag.ip_hdr_error) +                                         \
        (pk->flag.l4info_mapped))                                         \
    {                                                                     \
        SYS_ACL_DBG_DUMP("\n");                                           \
    }

#define    DUMP_MAC_UNIQUE(pk)                                                                     \
    DUMP_IPV4(pk)                                                                                  \
    if (pk->flag.l3_type)                                                                          \
    {                                                                                              \
        SYS_ACL_DBG_DUMP("  l3_type %u  mask %u,", pk->l3_type, pk->l3_type_mask);                 \
    }                                                                                              \
    if (pk->flag.arp_op_code)                                                                      \
    {                                                                                              \
        SYS_ACL_DBG_DUMP("  arp_op_code 0x%x  mask 0x%x,", pk->arp_op_code, pk->arp_op_code_mask); \
    }                                                                                              \
    if ((pk->flag.arp_op_code) + (pk->flag.l3_type))                                               \
    {                                                                                              \
        SYS_ACL_DBG_DUMP("\n");                                                                    \
    }                                                                                              \

#define   DUMP_IPV4_UNIQUE(pk)                                  \
    DUMP_IPV4(pk)                                               \
    if (pk->flag.arp_packet)                                    \
    {                                                           \
        SYS_ACL_DBG_DUMP("  arp-packet %u,", pk->arp_packet);   \
    }                                                           \
    if (pk->flag.ipv4_packet)                                   \
    {                                                           \
        SYS_ACL_DBG_DUMP("  ipv4-packet %u,", pk->ipv4_packet); \
    }                                                           \
    if ((pk->flag.arp_packet) + (pk->flag.ipv4_packet))         \
    {                                                           \
        SYS_ACL_DBG_DUMP("\n");                                 \
    }                                                           \


#define   DUMP_IPV6_UNIQUE(pk)                                 \
    DUMP_IPV6(pk)                                              \
    if (pk->flag.l3_type)                                      \
    {                                                          \
        SYS_ACL_DBG_DUMP("  l3_type %u  mask %u,",             \
                         pk->l3_type, pk->l3_type_mask);       \
    }                                                          \
    if (pk->flag.flow_label)                                   \
    {                                                          \
        SYS_ACL_DBG_DUMP("  flow-label 0x%x flow-label 0x%x,", \
                         pk->flow_label, pk->flow_label_mask); \
    }                                                          \
    if ((pk->flag.l3_type) + (pk->flag.flow_label))            \
    {                                                          \
        SYS_ACL_DBG_DUMP("\n");                                \
    }                                                          \


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


#define SYS_ACL_ALL_ONE(num)                     ((1 << (num)) - 1)

#define SYS_ACL_AND_MASK_NUM(src, right, num)    (((src) >> (right)) & SYS_ACL_ALL_ONE(num))

#define ACL_PRINT_ENTRY_ROW(eid)                 SYS_ACL_DBG_DUMP("\n>>entry-id %u : \n", (eid))
#define ACL_PRINT_GROUP_ROW(gid)                 SYS_ACL_DBG_DUMP("\n>>group-id %u (first in goes last, installed goes tail) :\n", (gid))
#define ACL_PRINT_PRIO_ROW(prio)                 SYS_ACL_DBG_DUMP("\n>>group-prio %u (sort by block_idx ) :\n", (prio))
#define ACL_PRINT_TYPE_ROW(type)                 SYS_ACL_DBG_DUMP("\n>>type %u (sort by block_idx ) :\n", (type))
#define ACL_PRINT_ALL_SORT_SEP_BY_ROW(type)      SYS_ACL_DBG_DUMP("\n>>Divide by %s. \n", (type) ? "group" : "priority")
#define ACL_PRINT_CHIP(lchip)                    SYS_ACL_DBG_DUMP("++++++++++++++++ lchip %d ++++++++++++++++\n", lchip);


#define ACL_PRINT_COUNT(n)                       SYS_ACL_DBG_DUMP("%-7d ", n)
#define SYS_ACL_ALL_KEY     CTC_ACL_KEY_NUM
#define FIB_KEY_TYPE_ACL    0x1F
#define SYS_ACL_GPORT13_TO_GPORT14(gport)        ((((gport >> 8) << 9)) | (gport & 0xFF))


#define ACL_GET_TABLE_ENTYR_NUM(t, n)            CTC_ERROR_RETURN(sys_greatbelt_ftm_get_table_entry_num(lchip, t, n))

struct drv_acl_group_info_s
{
    uint8  dir;
    uint8  dir_mask;
    uint8  is_label;
    uint8  is_label_mask;

    uint8  use_group;
    uint8  use_group_mask;
    uint8  rsv0[2];

    uint32 label[2];
    uint32 mask[2];
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


#define __ACL_GET_FROM_SW__

/* only hash will call this fuction
 * pe must not NULL
 * pg can be NULL
 */
STATIC int32
_sys_greatbelt_acl_get_nodes_by_key(uint8 lchip, sys_acl_entry_t*  pe_in,
                                    sys_acl_entry_t** pe,
                                    sys_acl_group_t** pg)
{
    sys_acl_entry_t* pe_lkup = NULL;
    sys_acl_group_t* pg_lkup = NULL;

    CTC_PTR_VALID_CHECK(pe_in);
    CTC_PTR_VALID_CHECK(pe);

    SYS_ACL_DBG_FUNC();

    pe_lkup = ctc_hash_lookup(acl_master[lchip]->hash_ent_key, pe_in);
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

STATIC int32
_sys_greatbelt_acl_get_tcam_alloc_info(uint8 lchip, ctc_acl_key_type_t  type, sys_acl_group_info_t* group_info, uint8* alloc_id, uint32* hw_index_base)
{
    uint8 igs_alloc_id = 0;

    CTC_PTR_VALID_CHECK(group_info);
    CTC_PTR_VALID_CHECK(alloc_id);

    if (NULL != hw_index_base)
    {
        *hw_index_base = 0;
    }

    if ((CTC_EGRESS == group_info->dir) &&
            ((CTC_ACL_KEY_MAC == type) || (CTC_ACL_KEY_IPV4 == type)
            || (CTC_ACL_KEY_MPLS == type) || (CTC_ACL_KEY_IPV6 == type)))
    {
        /* alloc_id 0-5: ipv4(mac,mpls) + ipv6 + pbr_ipv4 + pbr_ipv6 + egress ipv4(mac,mpls) + egress ipv6*/
        *alloc_id = (CTC_ACL_KEY_IPV6 == type) ? acl_master[lchip]->egs_ipv6_alloc_id : acl_master[lchip]->egs_ipv4_alloc_id;

        if (0 == acl_master[lchip]->block[*alloc_id][group_info->block_id].fpab.entry_count)
        {
            *alloc_id = acl_master[lchip]->alloc_id[type];
        }
        else if (NULL != hw_index_base)
        {
            /* egs ipv4/ipv6 block hw index should add igs ipv4/ipv6 block total size */
            igs_alloc_id = acl_master[lchip]->alloc_id[type];
            *hw_index_base = acl_master[lchip]->block[igs_alloc_id][group_info->block_id].fpab.entry_count;
        }
    }
    else
    {
        /* when have egress block, both direction entry use ingress block */
        *alloc_id = acl_master[lchip]->alloc_id[type];
    }

    return CTC_E_NONE;
}

/*
 * get sys entry node by entry id
 * pg, pb if be NULL, won't care.
 * pe cannot be NULL.
 */
STATIC int32
_sys_greatbelt_acl_get_nodes_by_eid(uint8 lchip, uint32 eid,
                                    sys_acl_entry_t** pe,
                                    sys_acl_group_t** pg,
                                    sys_acl_block_t** pb)
{
    sys_acl_entry_t sys_entry;
    sys_acl_entry_t * pe_lkup = NULL;
    sys_acl_group_t * pg_lkup = NULL;
    sys_acl_block_t * pb_lkup = NULL;
    uint8           alloc_id  = 0;

    CTC_PTR_VALID_CHECK(pe);
    SYS_ACL_DBG_FUNC();


    sal_memset(&sys_entry, 0, sizeof(sys_acl_entry_t));
    sys_entry.fpae.entry_id = eid;
    sys_entry.fpae.lchip = lchip;

    pe_lkup = ctc_hash_lookup(acl_master[lchip]->entry, &sys_entry);

    if (pe_lkup)
    {
        pg_lkup = pe_lkup->group;

        /* get block */
        if (ACL_ENTRY_IS_TCAM(pe_lkup->key.type))
        {
            CTC_ERROR_RETURN(_sys_greatbelt_acl_get_tcam_alloc_info(lchip, pe_lkup->key.type, &(pg_lkup->group_info), &alloc_id, NULL));
            pb_lkup  = &acl_master[lchip]->block[alloc_id][pg_lkup->group_info.block_id];
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
_sys_greatbelt_acl_get_group_by_gid(uint8 lchip, uint32 gid, sys_acl_group_t** sys_group_out)
{
    sys_acl_group_t * p_sys_group_lkup = NULL;
    sys_acl_group_t sys_group;

    CTC_PTR_VALID_CHECK(sys_group_out);
    SYS_ACL_DBG_FUNC();

    sal_memset(&sys_group, 0, sizeof(sys_acl_group_t));
    sys_group.group_id = gid;

    p_sys_group_lkup = ctc_hash_lookup(acl_master[lchip]->group, &sys_group);

    *sys_group_out = p_sys_group_lkup;

    return CTC_E_NONE;
}

/* is_hash can be NULL */
/*static int32
_sys_greatbelt_acl_get_chip_by_pe(sys_acl_entry_t* pe,
                                  uint8* lchip,
                                  uint8* lchip_num,
                                  uint8* is_hash)
{
    uint8 lchip_stored;
    CTC_PTR_VALID_CHECK(lchip);
    CTC_PTR_VALID_CHECK(lchip_num);
    if (is_hash)
    {
        *is_hash = !(ACL_ENTRY_IS_TCAM(pe->key.type));
    }
    if (ACL_ENTRY_IS_TCAM(pe->key.type))
    {
        lchip_stored = pe->group->group_info.lchip;
    }
    else
    {
        lchip_stored = pe->key.u0.hash.lchip;
    }

    if (0xFF == lchip_stored)
    {
        *lchip     = 0;
        *lchip_num = acl_master[*lchip]->lchip_num;
    }
    else
    {
        *lchip     = lchip_stored;
        *lchip_num = (*lchip + 1);
    }

    return CTC_E_NONE;
}*/



/*
 * below is build hw struct
 */
#define __ACL_OPERATE_ON_HW__

STATIC int32
_sys_greatbelt_acl_build_ds_action(uint8 lchip, sys_acl_action_t* p_sys_action, ds_acl_t* p_ds_action)
{
    uint16 stats_ptr;
    uint16 vlan_field;

    CTC_PTR_VALID_CHECK(p_sys_action);
    CTC_PTR_VALID_CHECK(p_ds_action);

    SYS_ACL_DBG_FUNC();

    p_ds_action->discard_packet   = p_sys_action->flag.discard;
    p_ds_action->deny_learning    = p_sys_action->flag.deny_learning;
    p_ds_action->deny_bridge      = p_sys_action->flag.deny_bridge;
    p_ds_action->deny_route       = p_sys_action->flag.deny_route;
    p_ds_action->exception_to_cpu = p_sys_action->flag.copy_to_cpu;
    p_ds_action->timestamp_en     = p_sys_action->flag.copy_to_cpu_with_timestamp;

    if (p_sys_action->flag.stats)
    {
        stats_ptr = p_sys_action->chip.stats_ptr;

        p_ds_action->stats_ptr15_14 = (stats_ptr >> 14) & 3;
        p_ds_action->stats_ptr13_12 = (stats_ptr >> 12) & 3;
        p_ds_action->stats_ptr11_4  = (stats_ptr >> 4) & 0xFF;
        p_ds_action->stats_ptr3_0   = (stats_ptr & 0xF);
    }

#if 0
    if (p_sys_action->flag.flow_id)
    {   /* 0 invalid */
        p_ds_action->flow_id = p_sys_action->flow_id;
    }
#endif /* _if0_ */

    if (p_sys_action->flag.micro_flow_policer)
    {
        p_ds_action->flow_policer_ptr = p_sys_action->chip.micro_policer_ptr;
    }

    if (p_sys_action->flag.macro_flow_policer)
    {
        p_ds_action->agg_flow_policer_ptr = p_sys_action->chip.macro_policer_ptr;
    }

    if (p_sys_action->flag.random_log)
    {
        p_ds_action->random_log_en          = 1;
        p_ds_action->random_threshold_shift = p_sys_action->random_threshold_shift;
        p_ds_action->acl_log_id             = p_sys_action->acl_log_id;
    }

    if (p_sys_action->flag.priority_and_color)
    {
        p_ds_action->priority = p_sys_action->priority;
        p_ds_action->color    = p_sys_action->color;
    }

    if (p_sys_action->flag.trust)
    {
        p_ds_action->qos_policy = p_sys_action->trust;
    }
    else
    {   /* 7 is invalid */
        p_ds_action->qos_policy = SYS_ACL_INVALID_QOS_POLICY;
    }

    if (p_sys_action->flag.redirect)
    {
        p_ds_action->ds_fwd_ptr = p_sys_action->chip.offset;
    }

    if (p_sys_action->flag.qos_domain)
    {
        p_ds_action->qos_domain = p_sys_action->qos_domain;
    }

    if (p_sys_action->flag.dscp)
    {
        p_ds_action->dscp_valid = 1;
        p_ds_action->acl_dscp   = p_sys_action->dscp;
    }

    if (p_sys_action->flag.aging)
    {
        p_ds_action->aging_valid = 1;
        p_ds_action->aging_index = SYS_AGING_TIMER_INDEX_SERVICE;
    }

    if (p_sys_action->flag.vlan_edit == 1)
    {
        p_ds_action->s_tag_action     = p_sys_action->vlan_edit.stag_op;
        p_ds_action->s_vlan_id_action = p_sys_action->vlan_edit.svid_sl;
        p_ds_action->s_cos_action     = p_sys_action->vlan_edit.scos_sl;
        p_ds_action->s_cfi_action     = p_sys_action->vlan_edit.scfi_sl;

        vlan_field                = p_sys_action->vlan_edit.svid;
        p_ds_action->svlan_id11_4 = (vlan_field >> 4) & 0xFF;
        p_ds_action->svlan_id3_2  = (vlan_field >> 2) & 0x3;
        p_ds_action->svlan_id1_0  = vlan_field & 0x3;
        vlan_field                = 0;
        vlan_field                = p_sys_action->vlan_edit.scos;
        p_ds_action->scos2        = (vlan_field >> 2) & 0x1;
        p_ds_action->scos1_0      = vlan_field & 0x3;
        p_ds_action->scfi         = p_sys_action->vlan_edit.scfi;


        p_ds_action->c_tag_action     = p_sys_action->vlan_edit.ctag_op;
        p_ds_action->c_vlan_id_action = p_sys_action->vlan_edit.cvid_sl;
        p_ds_action->c_cos_action     = p_sys_action->vlan_edit.ccos_sl;
        p_ds_action->c_cfi_action     = p_sys_action->vlan_edit.ccfi_sl;

        p_ds_action->cvlan_id = p_sys_action->vlan_edit.cvid;
        p_ds_action->ccos     = p_sys_action->vlan_edit.ccos;
        p_ds_action->ccfi     = p_sys_action->vlan_edit.ccfi;
        p_ds_action->svlan_tpid_index_en = 1;
        p_ds_action->svlan_tpid_index = p_sys_action->vlan_edit.tpid_index;
    }

    p_ds_action->equal_cost_path_num = ((p_sys_action->ecpn) & 0xF);

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_acl_build_pbr_ds_action(uint8 lchip, sys_acl_action_t* p_sys_action, ds_ipv4_ucast_pbr_dual_da_tcam_t* p_ds_action)
{
    CTC_PTR_VALID_CHECK(p_sys_action);
    CTC_PTR_VALID_CHECK(p_ds_action);

    SYS_ACL_DBG_FUNC();

    if (p_sys_action->flag.pbr_ttl_check)
    {
        p_ds_action->ttl_check_en = 1;
    }

    if (p_sys_action->flag.pbr_icmp_check)
    {
        p_ds_action->icmp_check_en = 1;
    }

    if (p_sys_action->flag.pbr_cpu)
    {
        p_ds_action->ip_da_exception_en  = 1;
        p_ds_action->exception_sub_index = 63 & 0x1F; /*to CPU must be equal to 63(get the low 5bits is 31), refer to sys_greatbelt_pdu.h:SYS_L3PDU_PER_L3IF_ACTION_INDEX_RSV_IPDA_TO_CPU*/
    }

    if (p_sys_action->flag.pbr_deny)
    {
        p_ds_action->deny_pbr = 1;
    }

    p_ds_action->equal_cost_path_num2_0 = p_sys_action->ecpn & 0x7;
    p_ds_action->equal_cost_path_num3_3 = (p_sys_action->ecpn >> 3) & 0x1;

    p_ds_action->rpf_if_id  = p_sys_action->l3if_id & 0x3ff;
    p_ds_action->ds_fwd_ptr = p_sys_action->chip.offset;

    return CTC_E_NONE;
}

/*
 * build driver group info based on sys group info
 */
STATIC int32
_sys_greatbelt_acl_build_ds_group_info(uint8 lchip, sys_acl_group_info_t* pg_sys, drv_acl_group_info_t* pg_drv)
{
    CTC_PTR_VALID_CHECK(pg_drv);
    CTC_PTR_VALID_CHECK(pg_sys);

    SYS_ACL_DBG_FUNC();

    if (CTC_BOTH_DIRECTION == pg_sys->dir)
    {
        pg_drv->dir      = 0;
        pg_drv->dir_mask = 0;
    }
    else if (CTC_INGRESS == pg_sys->dir)
    {
        pg_drv->dir      = 0;
        pg_drv->dir_mask = 1;
    }
    else
    {
        pg_drv->dir      = 1;
        pg_drv->dir_mask = 1;
    }

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

        pg_drv->use_group      = 0;
        pg_drv->use_group_mask = 1;
        pg_drv->is_label       = 1;
        pg_drv->is_label_mask  = 1;
        pg_drv->label[0]       = 0;
        pg_drv->label[1]       = 0;

        /* port_bitmap  31 ~0 */
        pg_drv->mask[0] = ~(pg_sys->un.port_bitmap[0]);

        /* port_bitmap  51 ~32 */
        pg_drv->mask[1] = ~(pg_sys->un.port_bitmap[1]);

        break;

    case     CTC_ACL_GROUP_TYPE_GLOBAL:
    case     CTC_ACL_GROUP_TYPE_NONE:
        pg_drv->use_group      = 0;
        pg_drv->use_group_mask = 0;
        pg_drv->is_label       = 0;
        pg_drv->is_label_mask  = 0;
        pg_drv->mask[0]        = 0;
        pg_drv->mask[1]        = 0;
        pg_drv->label[0]       = 0;
        pg_drv->label[1]       = 0;
        break;

    case     CTC_ACL_GROUP_TYPE_VLAN_CLASS:
        pg_drv->use_group      = 1;
        pg_drv->use_group_mask = 1;
        pg_drv->is_label       = 1;
        pg_drv->is_label_mask  = 1;
        pg_drv->label[0]       = pg_sys->un.vlan_class_id << 10;
        pg_drv->label[1]       = 0;
        pg_drv->mask[0]        = 0x3FF << 10;
        pg_drv->mask[1]        = 0;
        break;

    case     CTC_ACL_GROUP_TYPE_PORT_CLASS:
        pg_drv->use_group      = 1;
        pg_drv->use_group_mask = 1;
        pg_drv->is_label       = 1;
        pg_drv->is_label_mask  = 1;
        pg_drv->label[0]       = pg_sys->un.port_class_id;
        pg_drv->label[1]       = 0;
        pg_drv->mask[0]        = 0x3FF;
        pg_drv->mask[1]        = 0;
        break;

    case     CTC_ACL_GROUP_TYPE_SERVICE_ACL:
        pg_drv->use_group      = 0;
        pg_drv->use_group_mask = 0;
        pg_drv->is_label       = 0;
        pg_drv->is_label_mask  = 1;
        pg_drv->label[0]       = pg_sys->un.service_id;
        pg_drv->label[1]       = 0;
        pg_drv->mask[0]        = pg_sys->un.service_id ? 0xFFFF : 0;
        pg_drv->mask[1]        = 0;
        break;

    case     CTC_ACL_GROUP_TYPE_PBR_CLASS:
        pg_drv->label[0] = pg_sys->un.pbr_class_id;
        pg_drv->mask[0]  = 0x3F;
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_acl_build_ds_mac_key(uint8 lchip, drv_acl_group_info_t* p_info,
                                    sys_acl_mac_key_t* p_sys_key,
                                    ds_acl_mac_key0_t* p_ds_key,
                                    ds_acl_mac_key0_t* p_ds_mask)
{
    CTC_PTR_VALID_CHECK(p_info);
    CTC_PTR_VALID_CHECK(p_sys_key);
    CTC_PTR_VALID_CHECK(p_ds_key);
    CTC_PTR_VALID_CHECK(p_ds_mask);

    SYS_ACL_DBG_FUNC();

    /* mac da */
    if (p_sys_key->flag.mac_da)
    {
        SYS_ACL_SET_MAC_HIGH(p_ds_key->mac_da47_32, p_sys_key->mac_da);
        SYS_ACL_SET_MAC_LOW(p_ds_key->mac_da31_0, p_sys_key->mac_da);

        SYS_ACL_SET_MAC_HIGH(p_ds_mask->mac_da47_32, p_sys_key->mac_da_mask);
        SYS_ACL_SET_MAC_LOW(p_ds_mask->mac_da31_0, p_sys_key->mac_da_mask);
    }

    /* mac sa */
    if (p_sys_key->flag.mac_sa)
    {
        SYS_ACL_SET_MAC_HIGH(p_ds_key->mac_sa47_32, p_sys_key->mac_sa);
        SYS_ACL_SET_MAC_LOW(p_ds_key->mac_sa31_0, p_sys_key->mac_sa);

        SYS_ACL_SET_MAC_HIGH(p_ds_mask->mac_sa47_32, p_sys_key->mac_sa_mask);
        SYS_ACL_SET_MAC_LOW(p_ds_mask->mac_sa31_0, p_sys_key->mac_sa_mask);
    }

    /* vlan ptr */
    if (p_sys_key->flag.vlan_ptr)
    {
        p_ds_key->vlan_ptr  = p_sys_key->vlan_ptr;
        p_ds_mask->vlan_ptr = 0x1FFF;
    }

#if 0 /* GB NOT SUPPORT cos */
      /* cos */
    if (p_sys_key->flag.cos)
    {
        p_ds_key->cos  = p_sys_key->cos;
        p_ds_mask->cos = 0x7;
    }
#endif /* _if0_ */

    /* cvlan id */
    if (p_sys_key->flag.cvlan)
    {
        p_ds_key->cvlan_id  = p_sys_key->cvlan;
        p_ds_mask->cvlan_id = p_sys_key->cvlan_mask;
    }

    /* ctag cos */
    if (p_sys_key->flag.ctag_cos)
    {
        p_ds_key->ctag_cos  = p_sys_key->ctag_cos;
        p_ds_mask->ctag_cos = p_sys_key->ctag_cos_mask;
    }

    /* ctag cfi */
    if (p_sys_key->flag.ctag_cfi)
    {
        p_ds_key->ctag_cfi  = p_sys_key->ctag_cfi;
        p_ds_mask->ctag_cfi = 0x1;
    }

    /* svlan id */
    if (p_sys_key->flag.svlan)
    {
        p_ds_key->svlan_id  = p_sys_key->svlan;
        p_ds_mask->svlan_id = p_sys_key->svlan_mask;
    }

    /* stag cos */
    if (p_sys_key->flag.stag_cos)
    {
        p_ds_key->stag_cos  = p_sys_key->stag_cos;
        p_ds_mask->stag_cos = p_sys_key->stag_cos_mask;
    }

    /* stag cfi */
    if (p_sys_key->flag.stag_cfi)
    {
        p_ds_key->stag_cfi  = p_sys_key->stag_cfi;
        p_ds_mask->stag_cfi = 0x1;
    }

    /* ethernet type */
    if (p_sys_key->flag.eth_type)
    {
        p_ds_key->ether_type  = p_sys_key->eth_type;
        p_ds_mask->ether_type = p_sys_key->eth_type_mask;
    }

    /* layer 2 type */
    if (p_sys_key->flag.l2_type)
    {
        p_ds_key->layer2_type  = p_sys_key->l2_type;
        p_ds_mask->layer2_type = p_sys_key->l2_type_mask;
    }

    /* layer 3 type */
    if (p_sys_key->flag.l3_type)
    {
        p_ds_key->layer3_type  = p_sys_key->l3_type;
        p_ds_mask->layer3_type = p_sys_key->l3_type_mask;
    }

    /* arp_op_code */
    if (p_sys_key->flag.arp_op_code)
    {
        p_ds_key->arp_op_code0_0   = p_sys_key->arp_op_code & 0x1;
        p_ds_key->arp_op_code15_1  = p_sys_key->arp_op_code >> 1;
        p_ds_mask->arp_op_code0_0  = p_sys_key->arp_op_code_mask & 0x1;
        p_ds_mask->arp_op_code15_1 = p_sys_key->arp_op_code_mask >> 1;
    }

    /* ip_sa */
    if (p_sys_key->flag.ip_sa)
    {
        p_ds_key->ip_sa  = p_sys_key->ip_sa;
        p_ds_mask->ip_sa = p_sys_key->ip_sa_mask;
    }

    /* ip_da */
    if (p_sys_key->flag.ip_da)
    {
        p_ds_key->ip_da  = p_sys_key->ip_da;
        p_ds_mask->ip_da = p_sys_key->ip_da_mask;
    }

    /* svlan_id_valid */
    if (p_sys_key->flag.svlan_id_valid)
    {
        p_ds_key->svlan_id_valid  = p_sys_key->svlan_id_valid;
        p_ds_mask->svlan_id_valid = 1;
    }

    /* cvlan_id_valid */
    if (p_sys_key->flag.cvlan_id_valid)
    {
        p_ds_key->cvlan_id_valid  = p_sys_key->cvlan_id_valid;
        p_ds_mask->cvlan_id_valid = 1;
    }

    /* non-property field */
    p_ds_key->is_acl_qos_key0 = 1;
    p_ds_key->is_acl_qos_key1 = 1;
    p_ds_key->is_acl_qos_key2 = 1;
    p_ds_key->is_acl_qos_key3 = 1;

    p_ds_mask->is_acl_qos_key0 = 1;
    p_ds_mask->is_acl_qos_key1 = 1;
    p_ds_mask->is_acl_qos_key2 = 1;
    p_ds_mask->is_acl_qos_key3 = 1;

    p_ds_key->sub_table_id1 = ACL_MAC_SUB_TABLEID;
    p_ds_key->sub_table_id3 = ACL_MAC_SUB_TABLEID;

    p_ds_mask->sub_table_id1 = 0x3;
    p_ds_mask->sub_table_id3 = 0x3;

    p_ds_key->direction  = p_info->dir;
    p_ds_mask->direction = p_info->dir_mask;

    if (p_sys_key->port.valid)
    {
        p_ds_key->acl_label10_0  = SYS_ACL_AND_MASK_NUM(p_sys_key->port.label[0], 0, 11);
        p_ds_key->acl_label19_11 = SYS_ACL_AND_MASK_NUM(p_sys_key->port.label[0], 11, 9);
        p_ds_key->acl_label35_20 = SYS_ACL_AND_MASK_NUM(p_sys_key->port.label[0], 20, 12)
                                   | SYS_ACL_AND_MASK_NUM(p_sys_key->port.label[1], 0, 4) << 12;
        p_ds_key->acl_label51_36 = SYS_ACL_AND_MASK_NUM(p_sys_key->port.label[1], 4, 16);
        p_ds_key->acl_label55_52 = SYS_ACL_AND_MASK_NUM(p_sys_key->port.label[1], 20, 4);

        p_ds_mask->acl_label10_0  = SYS_ACL_AND_MASK_NUM(p_sys_key->port.mask[0], 0, 11);
        p_ds_mask->acl_label19_11 = SYS_ACL_AND_MASK_NUM(p_sys_key->port.mask[0], 11, 9);
        p_ds_mask->acl_label35_20 = SYS_ACL_AND_MASK_NUM(p_sys_key->port.mask[0], 20, 12)
                                    | SYS_ACL_AND_MASK_NUM(p_sys_key->port.mask[1], 0, 4) << 12;
        p_ds_mask->acl_label51_36 = SYS_ACL_AND_MASK_NUM(p_sys_key->port.mask[1], 4, 16);
        p_ds_mask->acl_label55_52 = SYS_ACL_AND_MASK_NUM(p_sys_key->port.mask[1], 20, 4);

        p_ds_key->acl_use_label  = p_sys_key->port.use_group;
        p_ds_mask->acl_use_label = p_sys_key->port.use_group_mask;

        p_ds_key->is_label  = p_sys_key->port.is_label;
        p_ds_mask->is_label = p_sys_key->port.is_label_mask;
    }
    else
    {
        p_ds_key->acl_label10_0  = SYS_ACL_AND_MASK_NUM(p_info->label[0], 0, 11);
        p_ds_key->acl_label19_11 = SYS_ACL_AND_MASK_NUM(p_info->label[0], 11, 9);
        p_ds_key->acl_label35_20 = SYS_ACL_AND_MASK_NUM(p_info->label[0], 20, 12)
                                   | SYS_ACL_AND_MASK_NUM(p_info->label[1], 0, 4) << 12;
        p_ds_key->acl_label51_36 = SYS_ACL_AND_MASK_NUM(p_info->label[1], 4, 16);
        p_ds_key->acl_label55_52 = SYS_ACL_AND_MASK_NUM(p_info->label[1], 20, 4);

        p_ds_mask->acl_label10_0  = SYS_ACL_AND_MASK_NUM(p_info->mask[0], 0, 11);
        p_ds_mask->acl_label19_11 = SYS_ACL_AND_MASK_NUM(p_info->mask[0], 11, 9);
        p_ds_mask->acl_label35_20 = SYS_ACL_AND_MASK_NUM(p_info->mask[0], 20, 12)
                                    | SYS_ACL_AND_MASK_NUM(p_info->mask[1], 0, 4) << 12;
        p_ds_mask->acl_label51_36 = SYS_ACL_AND_MASK_NUM(p_info->mask[1], 4, 16);
        p_ds_mask->acl_label55_52 = SYS_ACL_AND_MASK_NUM(p_info->mask[1], 20, 4);

        p_ds_key->acl_use_label  = p_info->use_group;
        p_ds_mask->acl_use_label = p_info->use_group_mask;

        p_ds_key->is_label  = p_info->is_label;
        p_ds_mask->is_label = p_info->is_label_mask;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_acl_build_ds_ipv4_key(uint8 lchip, drv_acl_group_info_t* p_info,
                                     sys_acl_ipv4_key_t* p_sys_key,
                                     ds_acl_ipv4_key0_t* p_ds_key,
                                     ds_acl_ipv4_key0_t* p_ds_mask)
{
    CTC_PTR_VALID_CHECK(p_info);
    CTC_PTR_VALID_CHECK(p_sys_key);
    CTC_PTR_VALID_CHECK(p_ds_key);
    CTC_PTR_VALID_CHECK(p_ds_mask);

    SYS_ACL_DBG_FUNC();

    /* ip da */
    if (p_sys_key->flag.ip_da)
    {
        p_ds_key->ip_da  = p_sys_key->ip_da;
        p_ds_mask->ip_da = p_sys_key->ip_da_mask;
    }

    /* ip sa */
    if (p_sys_key->flag.ip_sa)
    {
        p_ds_key->ip_sa  = p_sys_key->ip_sa;
        p_ds_mask->ip_sa = p_sys_key->ip_sa_mask;
    }

    /* l4 info mapped */
    if (p_sys_key->flag.l4info_mapped)
    {
        p_ds_key->l4_info_mapped  = p_sys_key->l4info_mapped;
        p_ds_mask->l4_info_mapped = p_sys_key->l4info_mapped_mask;
    }

    /* is tcp */
    if (p_sys_key->flag.is_tcp)
    {
        p_ds_key->is_tcp  = p_sys_key->is_tcp;
        p_ds_mask->is_tcp = 1;
    }

    /* is udp */
    if (p_sys_key->flag.is_udp)
    {
        p_ds_key->is_udp  = p_sys_key->is_udp;
        p_ds_mask->is_udp = 1;
    }

    /* l4 source port */
    if (p_sys_key->flag.l4_src_port)
    {
        p_ds_key->l4_source_port  = p_sys_key->l4_src_port;
        p_ds_mask->l4_source_port = p_sys_key->l4_src_port_mask;
    }

    /* l4 destination port */
    if (p_sys_key->flag.l4_dst_port)
    {
        p_ds_key->l4_dest_port  = p_sys_key->l4_dst_port;
        p_ds_mask->l4_dest_port = p_sys_key->l4_dst_port_mask;
    }
    else if (p_sys_key->flag.eth_type)
    {    /* eth_type */
        p_ds_key->l4_dest_port  = p_sys_key->eth_type;
        p_ds_mask->l4_dest_port = p_sys_key->eth_type_mask;
    }

    /* l2 type */
    if (p_sys_key->flag.l2_type)
    {
        p_ds_key->dscp  = p_sys_key->l2_type;
        p_ds_mask->dscp = p_sys_key->l2_type_mask;
    }
    else if (p_sys_key->flag.dscp)
    {
        p_ds_key->dscp  = p_sys_key->dscp;
        p_ds_mask->dscp = p_sys_key->dscp_mask;
    }

    /* ip fragement */
    if (p_sys_key->flag.frag_info)
    {
        p_ds_key->frag_info  = p_sys_key->frag_info;
        p_ds_mask->frag_info = p_sys_key->frag_info_mask;
    }

    /* ip option */
    if (p_sys_key->flag.ip_option)
    {
        p_ds_key->ip_options  = p_sys_key->ip_option;
        p_ds_mask->ip_options = 1;
    }

    /* ip header error */
    if (p_sys_key->flag.ip_hdr_error)
    {
        p_ds_key->ip_header_error  = p_sys_key->ip_hdr_error;
        p_ds_mask->ip_header_error = 1;
    }

    /* routed packet */
    if (p_sys_key->flag.routed_packet)
    {
        p_ds_key->routed_packet  = p_sys_key->routed_packet;
        p_ds_mask->routed_packet = 1;
    }

    /* mac da */
    if (p_sys_key->flag.mac_da)
    {
        SYS_ACL_SET_MAC_HIGH(p_ds_key->mac_da47_32, p_sys_key->mac_da);
        SYS_ACL_SET_MAC_LOW(p_ds_key->mac_da31_0, p_sys_key->mac_da);

        SYS_ACL_SET_MAC_HIGH(p_ds_mask->mac_da47_32, p_sys_key->mac_da_mask);
        SYS_ACL_SET_MAC_LOW(p_ds_mask->mac_da31_0, p_sys_key->mac_da_mask);
    }

    /* mac sa */
    if (p_sys_key->flag.mac_sa)
    {
        SYS_ACL_SET_MAC_HIGH(p_ds_key->mac_sa47_32, p_sys_key->mac_sa);
        SYS_ACL_SET_MAC_LOW(p_ds_key->mac_sa31_0, p_sys_key->mac_sa);

        SYS_ACL_SET_MAC_HIGH(p_ds_mask->mac_sa47_32, p_sys_key->mac_sa_mask);
        SYS_ACL_SET_MAC_LOW(p_ds_mask->mac_sa31_0, p_sys_key->mac_sa_mask);
    }

    /* cvlan id */
    if (p_sys_key->flag.cvlan)
    {
        p_ds_key->cvlan_id  = p_sys_key->cvlan;
        p_ds_mask->cvlan_id = p_sys_key->cvlan_mask;
    }

    /* ctag cos */
    if (p_sys_key->flag.ctag_cos)
    {
        p_ds_key->ctag_cos  = p_sys_key->ctag_cos;
        p_ds_mask->ctag_cos = p_sys_key->ctag_cos_mask;
    }

    /* ctag cfi */
    if (p_sys_key->flag.ctag_cfi)
    {
        p_ds_key->ctag_cfi  = p_sys_key->ctag_cfi;
        p_ds_mask->ctag_cfi = 1;
    }

    /* svlan id */
    if (p_sys_key->flag.svlan)
    {
        p_ds_key->svlan_id  = p_sys_key->svlan;
        p_ds_mask->svlan_id = p_sys_key->svlan_mask;
    }

    /* stag cos */
    if (p_sys_key->flag.stag_cos)
    {
        p_ds_key->stag_cos  = p_sys_key->stag_cos;
        p_ds_mask->stag_cos = p_sys_key->stag_cos_mask;
    }

    /* stag cfi */
    if (p_sys_key->flag.stag_cfi)
    {
        p_ds_key->stag_cfi  = p_sys_key->stag_cfi;
        p_ds_mask->stag_cfi = 1;
    }

    /* svlan_id_valid */
    if (p_sys_key->flag.svlan_id_valid)
    {
        p_ds_key->svlan_id_valid  = p_sys_key->svlan_id_valid;
        p_ds_mask->svlan_id_valid = 1;
    }

    /* cvlan_id_valid */
    if (p_sys_key->flag.cvlan_id_valid)
    {
        p_ds_key->cvlan_id_valid  = p_sys_key->cvlan_id_valid;
        p_ds_mask->cvlan_id_valid = 1;
    }

    /* ipv4_packet */
    if (p_sys_key->flag.ipv4_packet)
    {
        p_ds_key->ipv4_packet  = p_sys_key->ipv4_packet;
        p_ds_mask->ipv4_packet = 1;
    }

    /* arp_packet */
    if (p_sys_key->flag.arp_packet)
    {
        p_ds_key->is_arp_key  = p_sys_key->arp_packet;
        p_ds_mask->is_arp_key = 1;
    }

    /* non-property field */
    p_ds_key->is_acl_qos_key0 = 1;
    p_ds_key->is_acl_qos_key1 = 1;
    p_ds_key->is_acl_qos_key2 = 1;
    p_ds_key->is_acl_qos_key3 = 1;

    p_ds_mask->is_acl_qos_key0 = 1;
    p_ds_mask->is_acl_qos_key1 = 1;
    p_ds_mask->is_acl_qos_key2 = 1;
    p_ds_mask->is_acl_qos_key3 = 1;

    p_ds_key->sub_table_id1 = ACL_IPV4_SUB_TABLEID;
    p_ds_key->sub_table_id3 = ACL_IPV4_SUB_TABLEID;

    p_ds_mask->sub_table_id1 = 0x3;
    p_ds_mask->sub_table_id3 = 0x3;

    p_ds_key->direction  = p_info->dir;
    p_ds_mask->direction = p_info->dir_mask;

    if (p_sys_key->port.valid)
    {
        p_ds_key->acl_label9_0   = SYS_ACL_AND_MASK_NUM(p_sys_key->port.label[0], 0, 10);
        p_ds_key->acl_label19_10 = SYS_ACL_AND_MASK_NUM(p_sys_key->port.label[0], 10, 10);
        p_ds_key->acl_label28_20 = SYS_ACL_AND_MASK_NUM(p_sys_key->port.label[0], 20, 9);
        p_ds_key->acl_label32_29 = SYS_ACL_AND_MASK_NUM(p_sys_key->port.label[0], 29, 3)
                                   | (SYS_ACL_AND_MASK_NUM(p_sys_key->port.label[1], 0, 1) << 3);
        p_ds_key->acl_label41_33 = SYS_ACL_AND_MASK_NUM(p_sys_key->port.label[1], 1, 9);
        p_ds_key->acl_label53_42 = SYS_ACL_AND_MASK_NUM(p_sys_key->port.label[1], 10, 12);
        p_ds_key->acl_label55_54 = SYS_ACL_AND_MASK_NUM(p_sys_key->port.label[1], 22, 2);

        p_ds_mask->acl_label9_0   = SYS_ACL_AND_MASK_NUM(p_sys_key->port.mask[0], 0, 10);
        p_ds_mask->acl_label19_10 = SYS_ACL_AND_MASK_NUM(p_sys_key->port.mask[0], 10, 10);
        p_ds_mask->acl_label28_20 = SYS_ACL_AND_MASK_NUM(p_sys_key->port.mask[0], 20, 9);
        p_ds_mask->acl_label32_29 = SYS_ACL_AND_MASK_NUM(p_sys_key->port.mask[0], 29, 3)
                                    | (SYS_ACL_AND_MASK_NUM(p_sys_key->port.mask[1], 0, 1) << 3);
        p_ds_mask->acl_label41_33 = SYS_ACL_AND_MASK_NUM(p_sys_key->port.mask[1], 1, 9);
        p_ds_mask->acl_label53_42 = SYS_ACL_AND_MASK_NUM(p_sys_key->port.mask[1], 10, 12);
        p_ds_mask->acl_label55_54 = SYS_ACL_AND_MASK_NUM(p_sys_key->port.mask[1], 22, 2);

        p_ds_key->acl_use_label  = p_sys_key->port.use_group;
        p_ds_mask->acl_use_label = p_sys_key->port.use_group_mask;

        p_ds_key->is_label  = p_sys_key->port.is_label;
        p_ds_mask->is_label = p_sys_key->port.is_label_mask;
    }
    else
    {
        p_ds_key->acl_label9_0   = SYS_ACL_AND_MASK_NUM(p_info->label[0], 0, 10);
        p_ds_key->acl_label19_10 = SYS_ACL_AND_MASK_NUM(p_info->label[0], 10, 10);
        p_ds_key->acl_label28_20 = SYS_ACL_AND_MASK_NUM(p_info->label[0], 20, 9);
        p_ds_key->acl_label32_29 = SYS_ACL_AND_MASK_NUM(p_info->label[0], 29, 3)
                                   | (SYS_ACL_AND_MASK_NUM(p_info->label[1], 0, 1) << 3);
        p_ds_key->acl_label41_33 = SYS_ACL_AND_MASK_NUM(p_info->label[1], 1, 9);
        p_ds_key->acl_label53_42 = SYS_ACL_AND_MASK_NUM(p_info->label[1], 10, 12);
        p_ds_key->acl_label55_54 = SYS_ACL_AND_MASK_NUM(p_info->label[1], 22, 2);

        p_ds_mask->acl_label9_0   = SYS_ACL_AND_MASK_NUM(p_info->mask[0], 0, 10);
        p_ds_mask->acl_label19_10 = SYS_ACL_AND_MASK_NUM(p_info->mask[0], 10, 10);
        p_ds_mask->acl_label28_20 = SYS_ACL_AND_MASK_NUM(p_info->mask[0], 20, 9);
        p_ds_mask->acl_label32_29 = SYS_ACL_AND_MASK_NUM(p_info->mask[0], 29, 3)
                                    | (SYS_ACL_AND_MASK_NUM(p_info->mask[1], 0, 1) << 3);
        p_ds_mask->acl_label41_33 = SYS_ACL_AND_MASK_NUM(p_info->mask[1], 1, 9);
        p_ds_mask->acl_label53_42 = SYS_ACL_AND_MASK_NUM(p_info->mask[1], 10, 12);
        p_ds_mask->acl_label55_54 = SYS_ACL_AND_MASK_NUM(p_info->mask[1], 22, 2);

        p_ds_key->acl_use_label  = p_info->use_group;
        p_ds_mask->acl_use_label = p_info->use_group_mask;

        p_ds_key->is_label  = p_info->is_label;
        p_ds_mask->is_label = p_info->is_label_mask;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_acl_build_ds_ipv6_key(uint8 lchip, drv_acl_group_info_t* p_info,
                                     sys_acl_ipv6_key_t* p_sys_key,
                                     ds_acl_ipv6_key0_t* p_ds_key,
                                     ds_acl_ipv6_key0_t* p_ds_mask)
{
    CTC_PTR_VALID_CHECK(p_info);
    CTC_PTR_VALID_CHECK(p_sys_key);
    CTC_PTR_VALID_CHECK(p_ds_key);
    CTC_PTR_VALID_CHECK(p_ds_mask);

    SYS_ACL_DBG_FUNC();

    /* ip da */
    if (p_sys_key->flag.ip_da)
    {
        p_ds_key->ip_da127_104 = SYS_ACL_AND_MASK_NUM(p_sys_key->ip_da[0], 8, 24);
        p_ds_key->ip_da103_72  = SYS_ACL_AND_MASK_NUM(p_sys_key->ip_da[0], 0, 8) << 24
                                | SYS_ACL_AND_MASK_NUM(p_sys_key->ip_da[1], 8, 24);
        p_ds_key->ip_da71_64 = SYS_ACL_AND_MASK_NUM(p_sys_key->ip_da[1], 0, 8);
        p_ds_key->ip_da63_32 = p_sys_key->ip_da[2];
        p_ds_key->ip_da31_0  = p_sys_key->ip_da[3];

        p_ds_mask->ip_da127_104 = SYS_ACL_AND_MASK_NUM(p_sys_key->ip_da_mask[0], 8, 24);
        p_ds_mask->ip_da103_72  = SYS_ACL_AND_MASK_NUM(p_sys_key->ip_da_mask[0], 0, 8) << 24
                                 | SYS_ACL_AND_MASK_NUM(p_sys_key->ip_da_mask[1], 8, 24);
        p_ds_mask->ip_da71_64 = SYS_ACL_AND_MASK_NUM(p_sys_key->ip_da_mask[1], 0, 8);
        p_ds_mask->ip_da63_32 = p_sys_key->ip_da_mask[2];
        p_ds_mask->ip_da31_0  = p_sys_key->ip_da_mask[3];
    }

    /* ip sa */
    if (p_sys_key->flag.ip_sa)
    {
        p_ds_key->ip_sa127_104 = SYS_ACL_AND_MASK_NUM(p_sys_key->ip_sa[0], 8, 24);
        p_ds_key->ip_sa103_72  = SYS_ACL_AND_MASK_NUM(p_sys_key->ip_sa[0], 0, 8) << 24
                                | SYS_ACL_AND_MASK_NUM(p_sys_key->ip_sa[1], 8, 24);
        p_ds_key->ip_sa71_64 = SYS_ACL_AND_MASK_NUM(p_sys_key->ip_sa[1], 0, 8);
        p_ds_key->ip_sa63_32 = p_sys_key->ip_sa[2];
        p_ds_key->ip_sa31_0  = p_sys_key->ip_sa[3];

        p_ds_mask->ip_sa127_104 = SYS_ACL_AND_MASK_NUM(p_sys_key->ip_sa_mask[0], 8, 24);
        p_ds_mask->ip_sa103_72  = SYS_ACL_AND_MASK_NUM(p_sys_key->ip_sa_mask[0], 0, 8) << 24
                                 | SYS_ACL_AND_MASK_NUM(p_sys_key->ip_sa_mask[1], 8, 24);
        p_ds_mask->ip_sa71_64 = SYS_ACL_AND_MASK_NUM(p_sys_key->ip_sa_mask[1], 0, 8);
        p_ds_mask->ip_sa63_32 = p_sys_key->ip_sa_mask[2];
        p_ds_mask->ip_sa31_0  = p_sys_key->ip_sa_mask[3];
    }

    /* l4_info_mapped */
    if (p_sys_key->flag.l4info_mapped)
    {
        p_ds_key->l4_info_mapped  = p_sys_key->l4info_mapped;
        p_ds_mask->l4_info_mapped = p_sys_key->l4info_mapped_mask;
    }

#if 0 /* gb not support */
      /* is application */
    if (p_sys_key->flag.is_application)
    {
        p_ds_key->is_application  = p_sys_key->is_application;
        p_ds_mask->is_application = 1;
    }

    /* extension header */
    if (p_sys_key->flag.ext_hdr)
    {
        p_ds_key->ipv6_extension_headers  = p_sys_key->ext_hdr;
        p_ds_mask->ipv6_extension_headers = p_sys_key->ext_hdr_mask;
    }

    /* cos */
    if (p_sys_key->flag.cos)
    {
        p_ds_key->cos  = p_sys_key->cos;
        p_ds_mask->cos = 7;
    }
#endif /* _if0_ */

    /* is tcp */
    if (p_sys_key->flag.is_tcp)
    {
        p_ds_key->is_tcp  = p_sys_key->is_tcp;
        p_ds_mask->is_tcp = 1;
    }

    /* is udp */
    if (p_sys_key->flag.is_udp)
    {
        p_ds_key->is_udp  = p_sys_key->is_udp;
        p_ds_mask->is_udp = 1;
    }

    /* l4 source port */
    if (p_sys_key->flag.l4_src_port)
    {
        p_ds_key->l4_source_port15_12 = p_sys_key->l4_src_port >> 12;
        p_ds_key->l4_source_port11_4  = (p_sys_key->l4_src_port >> 4) & 0xFF;
        p_ds_key->l4_source_port3_0   = p_sys_key->l4_src_port & 0xF;

        p_ds_mask->l4_source_port15_12 = p_sys_key->l4_src_port_mask >> 12;
        p_ds_mask->l4_source_port11_4  = (p_sys_key->l4_src_port_mask >> 4) & 0xFF;
        p_ds_mask->l4_source_port3_0   = p_sys_key->l4_src_port_mask & 0xF;
    }

    /* l4 destination port */
    if (p_sys_key->flag.l4_dst_port)
    {
        p_ds_key->l4_dest_port15_6 = p_sys_key->l4_dst_port >> 6;
        p_ds_key->l4_dest_port5_0  = p_sys_key->l4_dst_port & 0x3F;

        p_ds_mask->l4_dest_port15_6 = p_sys_key->l4_dst_port_mask >> 6;
        p_ds_mask->l4_dest_port5_0  = p_sys_key->l4_dst_port_mask & 0x3F;
    }

    /* dscp */
    if (p_sys_key->flag.dscp)
    {
        p_ds_key->dscp  = p_sys_key->dscp;
        p_ds_mask->dscp = p_sys_key->dscp_mask;
    }

    /* ip fragement */
    if (p_sys_key->flag.frag_info)
    {
        p_ds_key->frag_info  = p_sys_key->frag_info;
        p_ds_mask->frag_info = p_sys_key->frag_info_mask;
    }

    /* ip option */
    if (p_sys_key->flag.ip_option)
    {
        p_ds_key->ip_options  = p_sys_key->ip_option;
        p_ds_mask->ip_options = 1;
    }

    /* ip header error */
    if (p_sys_key->flag.ip_hdr_error)
    {
        p_ds_key->ip_header_error  = p_sys_key->ip_hdr_error;
        p_ds_mask->ip_header_error = 1;
    }

    /* routed packet */
    if (p_sys_key->flag.routed_packet)
    {
        p_ds_key->routed_packet  = p_sys_key->routed_packet;
        p_ds_mask->routed_packet = 1;
    }

    /* flow label */
    if (p_sys_key->flag.flow_label)
    {
        p_ds_key->ipv6_flow_label  = p_sys_key->flow_label;
        p_ds_mask->ipv6_flow_label = p_sys_key->flow_label_mask;
    }

    /* mac da */
    if (p_sys_key->flag.mac_da)
    {
        SYS_ACL_SET_MAC_HIGH(p_ds_key->mac_da47_32, p_sys_key->mac_da);
        SYS_ACL_SET_MAC_LOW(p_ds_key->mac_da31_0, p_sys_key->mac_da);

        SYS_ACL_SET_MAC_HIGH(p_ds_mask->mac_da47_32, p_sys_key->mac_da_mask);
        SYS_ACL_SET_MAC_LOW(p_ds_mask->mac_da31_0, p_sys_key->mac_da_mask);
    }

    /* mac sa */
    if (p_sys_key->flag.mac_sa)
    {
        SYS_ACL_SET_MAC_HIGH(p_ds_key->mac_sa47_32, p_sys_key->mac_sa);
        SYS_ACL_SET_MAC_LOW(p_ds_key->mac_sa31_0, p_sys_key->mac_sa);

        SYS_ACL_SET_MAC_HIGH(p_ds_mask->mac_sa47_32, p_sys_key->mac_sa_mask);
        SYS_ACL_SET_MAC_LOW(p_ds_mask->mac_sa31_0, p_sys_key->mac_sa_mask);
    }

    /* cvlan id */
    if (p_sys_key->flag.cvlan)
    {
        p_ds_key->cvlan_id  = p_sys_key->cvlan;
        p_ds_mask->cvlan_id = p_sys_key->cvlan_mask;
    }

    /* ctag cos */
    if (p_sys_key->flag.ctag_cos)
    {
        p_ds_key->ctag_cos  = p_sys_key->ctag_cos;
        p_ds_mask->ctag_cos = p_sys_key->ctag_cos_mask;
    }

    /* ctag cfi */
    if (p_sys_key->flag.ctag_cfi)
    {
        p_ds_key->ctag_cfi  = p_sys_key->ctag_cfi;
        p_ds_mask->ctag_cfi = 1;
    }

    /* svlan id */
    if (p_sys_key->flag.svlan)
    {
        p_ds_key->svlan_id  = p_sys_key->svlan;
        p_ds_mask->svlan_id = p_sys_key->svlan_mask;
    }

    /* stag cos */
    if (p_sys_key->flag.stag_cos)
    {
        p_ds_key->stag_cos  = p_sys_key->stag_cos;
        p_ds_mask->stag_cos = p_sys_key->stag_cos_mask;
    }

    /* stag cfi */
    if (p_sys_key->flag.stag_cfi)
    {
        p_ds_key->stag_cfi  = p_sys_key->stag_cfi;
        p_ds_mask->stag_cfi = 1;
    }

    /* l2 type */
    if (p_sys_key->flag.l2_type)
    {
        p_ds_key->layer2_type  = p_sys_key->l2_type;
        p_ds_mask->layer2_type = p_sys_key->l2_type_mask;
    }

    /* l3 type */
    if (p_sys_key->flag.l3_type)
    {
        p_ds_key->layer3_type  = p_sys_key->l3_type;
        p_ds_mask->layer3_type = p_sys_key->l3_type_mask;
    }

    /* ether type */
    if (p_sys_key->flag.eth_type)
    {
        p_ds_key->ether_type  = p_sys_key->eth_type;
        p_ds_mask->ether_type = p_sys_key->eth_type_mask;
    }

    /* svlan_id_valid */
    if (p_sys_key->flag.svlan_id_valid)
    {
        p_ds_key->svlan_id_valid  = p_sys_key->svlan_id_valid;
        p_ds_mask->svlan_id_valid = 1;
    }

    /* cvlan_id_valid */
    if (p_sys_key->flag.cvlan_id_valid)
    {
        p_ds_key->cvlan_id_valid  = p_sys_key->cvlan_id_valid;
        p_ds_mask->cvlan_id_valid = 1;
    }

    /* non-property field */
    p_ds_key->is_acl_qos_key0 = 1;
    p_ds_key->is_acl_qos_key1 = 1;
    p_ds_key->is_acl_qos_key2 = 1;
    p_ds_key->is_acl_qos_key3 = 1;
    p_ds_key->is_acl_qos_key4 = 1;
    p_ds_key->is_acl_qos_key5 = 1;
    p_ds_key->is_acl_qos_key6 = 1;
    p_ds_key->is_acl_qos_key7 = 1;

    p_ds_mask->is_acl_qos_key0 = 1;
    p_ds_mask->is_acl_qos_key1 = 1;
    p_ds_mask->is_acl_qos_key2 = 1;
    p_ds_mask->is_acl_qos_key3 = 1;
    p_ds_mask->is_acl_qos_key4 = 1;
    p_ds_mask->is_acl_qos_key5 = 1;
    p_ds_mask->is_acl_qos_key6 = 1;
    p_ds_mask->is_acl_qos_key7 = 1;

    p_ds_key->sub_table_id1 = ACL_IPV6_SUB_TABLEID;
    p_ds_key->sub_table_id3 = ACL_IPV6_SUB_TABLEID;
    p_ds_key->sub_table_id5 = ACL_IPV6_SUB_TABLEID;
    p_ds_key->sub_table_id7 = ACL_IPV6_SUB_TABLEID;

    p_ds_mask->sub_table_id1 = 0x3;
    p_ds_mask->sub_table_id3 = 0x3;
    p_ds_mask->sub_table_id5 = 0x3;
    p_ds_mask->sub_table_id7 = 0x3;

    p_ds_key->direction  = p_info->dir;
    p_ds_mask->direction = p_info->dir_mask;

    if (p_sys_key->port.valid)
    {
        p_ds_key->acl_label0     = SYS_ACL_AND_MASK_NUM(p_sys_key->port.label[0], 0, 1);
        p_ds_key->acl_label6_1   = SYS_ACL_AND_MASK_NUM(p_sys_key->port.label[0], 1, 6);
        p_ds_key->acl_label9_7   = SYS_ACL_AND_MASK_NUM(p_sys_key->port.label[0], 7, 3);
        p_ds_key->acl_label10    = SYS_ACL_AND_MASK_NUM(p_sys_key->port.label[0], 10, 1);
        p_ds_key->acl_label14_11 = SYS_ACL_AND_MASK_NUM(p_sys_key->port.label[0], 11, 4);
        p_ds_key->acl_label16_15 = SYS_ACL_AND_MASK_NUM(p_sys_key->port.label[0], 15, 2);
        p_ds_key->acl_label19_17 = SYS_ACL_AND_MASK_NUM(p_sys_key->port.label[0], 17, 3);
        p_ds_key->acl_label51_20 = SYS_ACL_AND_MASK_NUM(p_sys_key->port.label[0], 20, 12)
                                   | SYS_ACL_AND_MASK_NUM(p_sys_key->port.label[1], 0, 20) << 12;
        p_ds_key->acl_label55_52 = SYS_ACL_AND_MASK_NUM(p_sys_key->port.label[1], 20, 4);

        p_ds_mask->acl_label0     = SYS_ACL_AND_MASK_NUM(p_sys_key->port.mask[0], 0, 1);
        p_ds_mask->acl_label6_1   = SYS_ACL_AND_MASK_NUM(p_sys_key->port.mask[0], 1, 6);
        p_ds_mask->acl_label9_7   = SYS_ACL_AND_MASK_NUM(p_sys_key->port.mask[0], 7, 3);
        p_ds_mask->acl_label10    = SYS_ACL_AND_MASK_NUM(p_sys_key->port.mask[0], 10, 1);
        p_ds_mask->acl_label14_11 = SYS_ACL_AND_MASK_NUM(p_sys_key->port.mask[0], 11, 4);
        p_ds_mask->acl_label16_15 = SYS_ACL_AND_MASK_NUM(p_sys_key->port.mask[0], 15, 2);
        p_ds_mask->acl_label19_17 = SYS_ACL_AND_MASK_NUM(p_sys_key->port.mask[0], 17, 3);
        p_ds_mask->acl_label51_20 = SYS_ACL_AND_MASK_NUM(p_sys_key->port.mask[0], 20, 12)
                                    | SYS_ACL_AND_MASK_NUM(p_sys_key->port.mask[1], 0, 20) << 12;
        p_ds_mask->acl_label55_52 = SYS_ACL_AND_MASK_NUM(p_sys_key->port.mask[1], 20, 4);

        p_ds_key->acl_use_label  = p_sys_key->port.use_group;
        p_ds_mask->acl_use_label = p_sys_key->port.use_group_mask;

        p_ds_key->is_label  = p_sys_key->port.is_label;
        p_ds_mask->is_label = p_sys_key->port.is_label_mask;
    }
    else
    {
        p_ds_key->acl_label0     = SYS_ACL_AND_MASK_NUM(p_info->label[0], 0, 1);
        p_ds_key->acl_label6_1   = SYS_ACL_AND_MASK_NUM(p_info->label[0], 1, 6);
        p_ds_key->acl_label9_7   = SYS_ACL_AND_MASK_NUM(p_info->label[0], 7, 3);
        p_ds_key->acl_label10    = SYS_ACL_AND_MASK_NUM(p_info->label[0], 10, 1);
        p_ds_key->acl_label14_11 = SYS_ACL_AND_MASK_NUM(p_info->label[0], 11, 4);
        p_ds_key->acl_label16_15 = SYS_ACL_AND_MASK_NUM(p_info->label[0], 15, 2);
        p_ds_key->acl_label19_17 = SYS_ACL_AND_MASK_NUM(p_info->label[0], 17, 3);
        p_ds_key->acl_label51_20 = SYS_ACL_AND_MASK_NUM(p_info->label[0], 20, 12)
                                   | SYS_ACL_AND_MASK_NUM(p_info->label[1], 0, 20) << 12;
        p_ds_key->acl_label55_52 = SYS_ACL_AND_MASK_NUM(p_info->label[1], 20, 4);

        p_ds_mask->acl_label0     = SYS_ACL_AND_MASK_NUM(p_info->mask[0], 0, 1);
        p_ds_mask->acl_label6_1   = SYS_ACL_AND_MASK_NUM(p_info->mask[0], 1, 6);
        p_ds_mask->acl_label9_7   = SYS_ACL_AND_MASK_NUM(p_info->mask[0], 7, 3);
        p_ds_mask->acl_label10    = SYS_ACL_AND_MASK_NUM(p_info->mask[0], 10, 1);
        p_ds_mask->acl_label14_11 = SYS_ACL_AND_MASK_NUM(p_info->mask[0], 11, 4);
        p_ds_mask->acl_label16_15 = SYS_ACL_AND_MASK_NUM(p_info->mask[0], 15, 2);
        p_ds_mask->acl_label19_17 = SYS_ACL_AND_MASK_NUM(p_info->mask[0], 17, 3);
        p_ds_mask->acl_label51_20 = SYS_ACL_AND_MASK_NUM(p_info->mask[0], 20, 12)
                                    | SYS_ACL_AND_MASK_NUM(p_info->mask[1], 0, 20) << 12;
        p_ds_mask->acl_label55_52 = SYS_ACL_AND_MASK_NUM(p_info->mask[1], 20, 4);

        p_ds_key->acl_use_label  = p_info->use_group;
        p_ds_mask->acl_use_label = p_info->use_group_mask;

        p_ds_key->is_label  = p_info->is_label;
        p_ds_mask->is_label = p_info->is_label_mask;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_acl_build_ds_mpls_key(uint8 lchip, drv_acl_group_info_t* p_info,
                                     sys_acl_mpls_key_t* p_sys_key,
                                     ds_acl_mpls_key0_t* p_ds_key,
                                     ds_acl_mpls_key0_t* p_ds_mask)
{
    CTC_PTR_VALID_CHECK(p_sys_key);
    CTC_PTR_VALID_CHECK(p_ds_key);
    CTC_PTR_VALID_CHECK(p_ds_mask);

    SYS_ACL_DBG_FUNC();

    /* mac da */
    if (p_sys_key->flag.mac_da)
    {
        SYS_ACL_SET_MAC_HIGH(p_ds_key->mac_da47_32, p_sys_key->mac_da);
        SYS_ACL_SET_MAC_LOW(p_ds_key->mac_da31_0, p_sys_key->mac_da);

        SYS_ACL_SET_MAC_HIGH(p_ds_mask->mac_da47_32, p_sys_key->mac_da_mask);
        SYS_ACL_SET_MAC_LOW(p_ds_mask->mac_da31_0, p_sys_key->mac_da_mask);
    }

    /* mac sa */
    if (p_sys_key->flag.mac_sa)
    {
        SYS_ACL_SET_MAC_HIGH(p_ds_key->mac_sa47_32, p_sys_key->mac_sa);
        SYS_ACL_SET_MAC_LOW(p_ds_key->mac_sa31_0, p_sys_key->mac_sa);

        SYS_ACL_SET_MAC_HIGH(p_ds_mask->mac_sa47_32, p_sys_key->mac_sa_mask);
        SYS_ACL_SET_MAC_LOW(p_ds_mask->mac_sa31_0, p_sys_key->mac_sa_mask);
    }

#if 0 /*  gb not support */
      /* cos */
    if (p_sys_key->flag.cos)
    {
        p_ds_key->cos  = p_sys_key->cos;
        p_ds_mask->cos = 0x7;
    }

    /* layer 2 type */
    if (p_sys_key->flag.l2_type)
    {
        p_ds_key->layer2_type  = p_sys_key->l2_type;
        p_ds_mask->layer2_type = 0xF;
    }
#endif /* _if0_ */

    /* cvlan id */
    if (p_sys_key->flag.cvlan)
    {
        p_ds_key->cvlan_id  = p_sys_key->cvlan;
        p_ds_mask->cvlan_id = p_sys_key->cvlan_mask;
    }

    /* ctag cos */
    if (p_sys_key->flag.ctag_cos)
    {
        p_ds_key->ctag_cos  = p_sys_key->ctag_cos;
        p_ds_mask->ctag_cos = p_sys_key->ctag_cos_mask;
    }

    /* ctag cfi */
    if (p_sys_key->flag.ctag_cfi)
    {
        p_ds_key->ctag_cfi  = p_sys_key->ctag_cfi;
        p_ds_mask->ctag_cfi = 0x1;
    }

    /* svlan id */
    if (p_sys_key->flag.svlan)
    {
        p_ds_key->svlan_id  = p_sys_key->svlan;
        p_ds_mask->svlan_id = p_sys_key->svlan_mask;
    }

    /* stag cos */
    if (p_sys_key->flag.stag_cos)
    {
        p_ds_key->stag_cos  = p_sys_key->stag_cos;
        p_ds_mask->stag_cos = p_sys_key->stag_cos_mask;
    }

    /* stag cfi */
    if (p_sys_key->flag.stag_cfi)
    {
        p_ds_key->stag_cfi  = p_sys_key->stag_cfi;
        p_ds_mask->stag_cfi = 0x1;
    }

    /* mpls label 0 */
    if (p_sys_key->flag.mpls_label0)
    {
        p_ds_key->mpls_label0  = p_sys_key->mpls_label0;
        p_ds_mask->mpls_label0 = p_sys_key->mpls_label0_mask;
    }

    /* mpls label 1 */
    if (p_sys_key->flag.mpls_label1)
    {
        p_ds_key->mpls_label1  = p_sys_key->mpls_label1;
        p_ds_mask->mpls_label1 = p_sys_key->mpls_label1_mask;
    }

    /* mpls label 2 */
    if (p_sys_key->flag.mpls_label2)
    {
        p_ds_key->mpls_label2  = p_sys_key->mpls_label2;
        p_ds_mask->mpls_label2 = p_sys_key->mpls_label2_mask;
    }

    /* mpls label 3 */
    if (p_sys_key->flag.mpls_label3)
    {
        p_ds_key->mpls_label3  = p_sys_key->mpls_label3;
        p_ds_mask->mpls_label3 = p_sys_key->mpls_label3_mask;
    }

    /* routed packet */
    if (p_sys_key->flag.routed_packet)
    {
        p_ds_key->routed_packet  = p_sys_key->routed_packet;
        p_ds_mask->routed_packet = 1;
    }

    /* non-property field */
    p_ds_key->is_acl_qos_key0 = 1;
    p_ds_key->is_acl_qos_key1 = 1;
    p_ds_key->is_acl_qos_key2 = 1;
    p_ds_key->is_acl_qos_key3 = 1;

    p_ds_mask->is_acl_qos_key0 = 1;
    p_ds_mask->is_acl_qos_key1 = 1;
    p_ds_mask->is_acl_qos_key2 = 1;
    p_ds_mask->is_acl_qos_key3 = 1;

    p_ds_key->sub_table_id1 = ACL_MPLS_SUB_TABLEID;
    p_ds_key->sub_table_id3 = ACL_MPLS_SUB_TABLEID;

    p_ds_mask->sub_table_id1 = 0x3;
    p_ds_mask->sub_table_id3 = 0x3;

    p_ds_key->direction  = p_info->dir;
    p_ds_mask->direction = p_info->dir_mask;

    if (p_sys_key->port.valid)
    {
        p_ds_key->acl_label11_0  = SYS_ACL_AND_MASK_NUM(p_sys_key->port.label[0], 0, 12);
        p_ds_key->acl_label24_12 = SYS_ACL_AND_MASK_NUM(p_sys_key->port.label[0], 12, 13);
        p_ds_key->acl_label37_25 = SYS_ACL_AND_MASK_NUM(p_sys_key->port.label[0], 25, 7)
                                   | (SYS_ACL_AND_MASK_NUM(p_sys_key->port.label[1], 0, 6) << 7);
        p_ds_key->acl_label51_38 = SYS_ACL_AND_MASK_NUM(p_sys_key->port.label[1], 6, 14);

        p_ds_mask->acl_label11_0  = SYS_ACL_AND_MASK_NUM(p_sys_key->port.mask[0], 0, 12);
        p_ds_mask->acl_label24_12 = SYS_ACL_AND_MASK_NUM(p_sys_key->port.mask[0], 12, 13);
        p_ds_mask->acl_label37_25 = SYS_ACL_AND_MASK_NUM(p_sys_key->port.mask[0], 25, 7)
                                    | (SYS_ACL_AND_MASK_NUM(p_sys_key->port.mask[1], 0, 6) << 7);
        p_ds_mask->acl_label51_38 = SYS_ACL_AND_MASK_NUM(p_sys_key->port.mask[1], 6, 14);

        p_ds_key->acl_use_label  = p_sys_key->port.use_group;
        p_ds_mask->acl_use_label = p_sys_key->port.use_group_mask;

        p_ds_key->is_label  = p_sys_key->port.is_label;
        p_ds_mask->is_label = p_sys_key->port.is_label_mask;
    }
    else
    {
        p_ds_key->acl_label11_0  = SYS_ACL_AND_MASK_NUM(p_info->label[0], 0, 12);
        p_ds_key->acl_label24_12 = SYS_ACL_AND_MASK_NUM(p_info->label[0], 12, 13);
        p_ds_key->acl_label37_25 = SYS_ACL_AND_MASK_NUM(p_info->label[0], 25, 7)
                                   | (SYS_ACL_AND_MASK_NUM(p_info->label[1], 0, 6) << 7);
        p_ds_key->acl_label51_38 = SYS_ACL_AND_MASK_NUM(p_info->label[1], 6, 14);

        p_ds_mask->acl_label11_0  = SYS_ACL_AND_MASK_NUM(p_info->mask[0], 0, 12);
        p_ds_mask->acl_label24_12 = SYS_ACL_AND_MASK_NUM(p_info->mask[0], 12, 13);
        p_ds_mask->acl_label37_25 = SYS_ACL_AND_MASK_NUM(p_info->mask[0], 25, 7)
                                    | (SYS_ACL_AND_MASK_NUM(p_info->mask[1], 0, 6) << 7);
        p_ds_mask->acl_label51_38 = SYS_ACL_AND_MASK_NUM(p_info->mask[1], 6, 14);

        p_ds_key->acl_use_label  = p_info->use_group;
        p_ds_mask->acl_use_label = p_info->use_group_mask;

        p_ds_key->is_label  = p_info->is_label;
        p_ds_mask->is_label = p_info->is_label_mask;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_acl_build_ds_hash_ipv4_key(uint8 lchip, sys_acl_entry_t* pe,
                                          ds_acl_qos_ipv4_hash_key_t* p_ds_key,
                                          uint16 ad_index)
{
    sys_acl_hash_ipv4_key_t* pk = NULL;

    CTC_PTR_VALID_CHECK(pe);
    CTC_PTR_VALID_CHECK(p_ds_key);

    SYS_ACL_DBG_FUNC();

    sal_memset(p_ds_key, 0, sizeof(ds_acl_qos_ipv4_hash_key_t));

    pk = (sys_acl_hash_ipv4_key_t *) &pe->key.u0.hash.u0.hash_ipv4_key;

    p_ds_key->hash_type0_high = (FIB_KEY_TYPE_ACL >> 1) & 0x7;
    p_ds_key->hash_type0_low  = FIB_KEY_TYPE_ACL & 0x1;
    p_ds_key->hash_type1_high = (FIB_KEY_TYPE_ACL >> 1) & 0x7;
    p_ds_key->hash_type1_low  = FIB_KEY_TYPE_ACL & 0x1;
    p_ds_key->valid0          = 1;
    p_ds_key->valid1          = 1;
    p_ds_key->is_mac_hash0    = 0;
    p_ds_key->is_mac_hash1    = 0;

    p_ds_key->dscp = pk->dscp;

    p_ds_key->is_arp_key    = pk->arp_packet;
    p_ds_key->is_ipv4_key   = pk->ipv4_packet;
    p_ds_key->is_logic_port = pk->is_logic_port;

    p_ds_key->global_src_port13_3 = SYS_ACL_GPORT13_TO_GPORT14(pk->gport) >> 3;
    p_ds_key->global_src_port2_0  = SYS_ACL_GPORT13_TO_GPORT14(pk->gport) & 0x7;

    p_ds_key->ds_ad_index6_0  = ad_index & 0x7F;
    p_ds_key->ds_ad_index14_7 = (ad_index >> 7) & 0xFF;
    p_ds_key->ds_ad_index15   = ad_index >> 15;

    p_ds_key->layer3_header_protocol = pk->l4_protocol;

    p_ds_key->l4_dest_port15_14 = (pk->l4_dst_port >> 14) & 0x3;
    p_ds_key->l4_dest_port13    = (pk->l4_dst_port >> 13) & 0x1;
    p_ds_key->l4_dest_port12_9  = (pk->l4_dst_port >> 9) & 0xF;
    p_ds_key->l4_dest_port8_0   = pk->l4_dst_port & 0x1FF;

    p_ds_key->l4_source_port = pk->l4_src_port;

    p_ds_key->ip_da = pk->ip_da;
    p_ds_key->ip_sa = pk->ip_sa;

    return CTC_E_NONE;
}


STATIC int32
_sys_greatbelt_acl_build_ds_hash_mac_key(uint8 lchip, sys_acl_entry_t* pe,
                                         ds_acl_qos_mac_hash_key_t* p_ds_key,
                                         uint16 ad_index)
{
    sys_acl_hash_mac_key_t* pk = NULL;

    CTC_PTR_VALID_CHECK(pe);
    CTC_PTR_VALID_CHECK(p_ds_key);

    SYS_ACL_DBG_FUNC();

    sal_memset(p_ds_key, 0, sizeof(ds_acl_qos_mac_hash_key_t));

    pk = (sys_acl_hash_mac_key_t *) &pe->key.u0.hash.u0.hash_mac_key;

    p_ds_key->hash_type0_high = (FIB_KEY_TYPE_ACL >> 1) & 0x7;
    p_ds_key->hash_type0_low  = FIB_KEY_TYPE_ACL & 0x1;
    p_ds_key->hash_type1_high = (FIB_KEY_TYPE_ACL >> 1) & 0x7;
    p_ds_key->hash_type1_low  = FIB_KEY_TYPE_ACL & 0x1;

    p_ds_key->valid0       = 1;
    p_ds_key->valid1       = 1;
    p_ds_key->is_mac_hash0 = 0;
    p_ds_key->is_mac_hash1 = 0;

    p_ds_key->cos                  = pk->cos;
    p_ds_key->is_logic_port        = pk->is_logic_port;
    p_ds_key->is_ipv4_key          = pk->ipv4_packet;
    p_ds_key->ds_ad_index6_0       = ad_index & 0x7F;
    p_ds_key->ds_ad_index14_7      = (ad_index >> 7) & 0xFF;
    p_ds_key->ds_ad_index15        = ad_index >> 15;
    p_ds_key->vlan_id              = pk->vlan_id;
    p_ds_key->global_src_port10_0  = SYS_ACL_GPORT13_TO_GPORT14(pk->gport) & 0x7FF;
    p_ds_key->global_src_port13_11 = (SYS_ACL_GPORT13_TO_GPORT14(pk->gport) >> 11) & 0x7;

    SYS_ACL_SET_MAC_HIGH(p_ds_key->mac_da47_32, pk->mac_da);
    SYS_ACL_SET_MAC_LOW(p_ds_key->mac_da31_0, pk->mac_da);

    p_ds_key->ether_type11_0  = pk->eth_type & 0xFFF;
    p_ds_key->ether_type15_12 = (pk->eth_type >> 12) & 0xF;

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_acl_build_pbr_ds_ipv4_key(uint8 lchip, drv_acl_group_info_t* p_info,
                                         sys_acl_pbr_ipv4_key_t* p_sys_key,
                                         ds_ipv4_pbr_key_t* p_ds_key,
                                         ds_ipv4_pbr_key_t* p_ds_mask)
{
    CTC_PTR_VALID_CHECK(p_info);
    CTC_PTR_VALID_CHECK(p_sys_key);
    CTC_PTR_VALID_CHECK(p_ds_key);
    CTC_PTR_VALID_CHECK(p_ds_mask);

    SYS_ACL_DBG_FUNC();

    /* ip da */
    if (p_sys_key->flag.ip_da)
    {
        p_ds_key->ip_da  = p_sys_key->ip_da;
        p_ds_mask->ip_da = p_sys_key->ip_da_mask;
    }

    /* ip sa */
    if (p_sys_key->flag.ip_sa)
    {
        p_ds_key->ip_sa  = p_sys_key->ip_sa;
        p_ds_mask->ip_sa = p_sys_key->ip_sa_mask;
    }

    /* l4 info mapped */
    if (p_sys_key->flag.l4info_mapped)
    {
        p_ds_key->l4_info_mapped  = p_sys_key->l4info_mapped;
        p_ds_mask->l4_info_mapped = p_sys_key->l4info_mapped_mask;
    }

    /* is tcp */
    if (p_sys_key->flag.is_tcp)
    {
        p_ds_key->is_tcp  = p_sys_key->is_tcp;
        p_ds_mask->is_tcp = 1;
    }

    /* is udp */
    if (p_sys_key->flag.is_udp)
    {
        p_ds_key->is_udp  = p_sys_key->is_udp;
        p_ds_mask->is_udp = 1;
    }

    /* l4 source port */
    if (p_sys_key->flag.l4_src_port)
    {
        p_ds_key->l4_source_port  = p_sys_key->l4_src_port;
        p_ds_mask->l4_source_port = p_sys_key->l4_src_port_mask;
    }

    /* l4 destination port */
    if (p_sys_key->flag.l4_dst_port)
    {
        p_ds_key->l4_dest_port  = p_sys_key->l4_dst_port;
        p_ds_mask->l4_dest_port = p_sys_key->l4_dst_port_mask;
    }

    if (p_sys_key->flag.dscp)
    {
        p_ds_key->dscp  = p_sys_key->dscp;
        p_ds_mask->dscp = p_sys_key->dscp_mask;
    }

    /* ip fragement */
    if (p_sys_key->flag.frag_info)
    {
        p_ds_key->frag_info  = p_sys_key->frag_info;
        p_ds_mask->frag_info = p_sys_key->frag_info_mask;
    }

    /* vrfid */
    if (p_sys_key->flag.vrfid)
    {
        p_ds_key->vrf_id11_0   = p_sys_key->vrfid & 0xFFF;
        p_ds_key->vrf_id13_12  = (p_sys_key->vrfid >> 12) & 0x3;
        p_ds_mask->vrf_id11_0  = p_sys_key->vrfid_mask & 0xFFF;
        p_ds_mask->vrf_id13_12 = (p_sys_key->vrfid_mask >> 12) & 0x3;
    }

    /* non-property field */
    p_ds_key->table_id0      = IPV4SA_TABLEID;
    p_ds_key->table_id1      = IPV4SA_TABLEID;
    p_ds_key->sub_table_id0  = IPV4SA_PBR_SUB_TABLEID;
    p_ds_key->sub_table_id1  = IPV4SA_PBR_SUB_TABLEID;
    p_ds_mask->table_id0     = 0x7;
    p_ds_mask->table_id1     = 0x7;
    p_ds_mask->sub_table_id0 = 0x3;
    p_ds_mask->sub_table_id1 = 0x3;

    if (p_sys_key->port.valid)
    {
        p_ds_key->pbr_label  = p_sys_key->port.label[0];
        p_ds_mask->pbr_label = p_sys_key->port.mask[0];
    }
    else
    {
        p_ds_key->pbr_label  = p_info->label[0];
        p_ds_mask->pbr_label = p_info->mask[0];
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_acl_build_pbr_ds_ipv6_key(uint8 lchip, drv_acl_group_info_t* p_info,
                                         sys_acl_pbr_ipv6_key_t* p_sys_key,
                                         ds_ipv6_pbr_key_t* p_ds_key,
                                         ds_ipv6_pbr_key_t* p_ds_mask)
{
    CTC_PTR_VALID_CHECK(p_info);
    CTC_PTR_VALID_CHECK(p_sys_key);
    CTC_PTR_VALID_CHECK(p_ds_key);
    CTC_PTR_VALID_CHECK(p_ds_mask);

    SYS_ACL_DBG_FUNC();

    /* ip da */
    if (p_sys_key->flag.ip_da)
    {
        p_ds_key->ip_da127_104 = SYS_ACL_AND_MASK_NUM(p_sys_key->ip_da[0], 8, 24);
        p_ds_key->ip_da103_72  = SYS_ACL_AND_MASK_NUM(p_sys_key->ip_da[0], 0, 8) << 24
                                | SYS_ACL_AND_MASK_NUM(p_sys_key->ip_da[1], 8, 24);
        p_ds_key->ip_da71_64 = SYS_ACL_AND_MASK_NUM(p_sys_key->ip_da[1], 0, 8);
        p_ds_key->ip_da63_32 = p_sys_key->ip_da[2];
        p_ds_key->ip_da31_0  = p_sys_key->ip_da[3];

        p_ds_mask->ip_da127_104 = SYS_ACL_AND_MASK_NUM(p_sys_key->ip_da_mask[0], 8, 24);
        p_ds_mask->ip_da103_72  = SYS_ACL_AND_MASK_NUM(p_sys_key->ip_da_mask[0], 0, 8) << 24
                                 | SYS_ACL_AND_MASK_NUM(p_sys_key->ip_da_mask[1], 8, 24);
        p_ds_mask->ip_da71_64 = SYS_ACL_AND_MASK_NUM(p_sys_key->ip_da_mask[1], 0, 8);
        p_ds_mask->ip_da63_32 = p_sys_key->ip_da_mask[2];
        p_ds_mask->ip_da31_0  = p_sys_key->ip_da_mask[3];
    }

    /* ip sa */
    if (p_sys_key->flag.ip_sa)
    {
        p_ds_key->ip_sa127_104 = SYS_ACL_AND_MASK_NUM(p_sys_key->ip_sa[0], 8, 24);
        p_ds_key->ip_sa103_72  = SYS_ACL_AND_MASK_NUM(p_sys_key->ip_sa[0], 0, 8) << 24
                                | SYS_ACL_AND_MASK_NUM(p_sys_key->ip_sa[1], 8, 24);
        p_ds_key->ip_sa71_64 = SYS_ACL_AND_MASK_NUM(p_sys_key->ip_sa[1], 0, 8);
        p_ds_key->ip_sa63_32 = p_sys_key->ip_sa[2];
        p_ds_key->ip_sa31_0  = p_sys_key->ip_sa[3];

        p_ds_mask->ip_sa127_104 = SYS_ACL_AND_MASK_NUM(p_sys_key->ip_sa_mask[0], 8, 24);
        p_ds_mask->ip_sa103_72  = SYS_ACL_AND_MASK_NUM(p_sys_key->ip_sa_mask[0], 0, 8) << 24
                                 | SYS_ACL_AND_MASK_NUM(p_sys_key->ip_sa_mask[1], 8, 24);
        p_ds_mask->ip_sa71_64 = SYS_ACL_AND_MASK_NUM(p_sys_key->ip_sa_mask[1], 0, 8);
        p_ds_mask->ip_sa63_32 = p_sys_key->ip_sa_mask[2];
        p_ds_mask->ip_sa31_0  = p_sys_key->ip_sa_mask[3];
    }

    /* l4_info_mapped */
    if (p_sys_key->flag.l4info_mapped)
    {
        p_ds_key->l4_info_mapped  = p_sys_key->l4info_mapped;
        p_ds_mask->l4_info_mapped = p_sys_key->l4info_mapped_mask;
    }

#if 0 /* gb not support */
      /* is application */
    if (p_sys_key->flag.is_application)
    {
        p_ds_key->is_application  = p_sys_key->is_application;
        p_ds_mask->is_application = 1;
    }

    /* extension header */
    if (p_sys_key->flag.ext_hdr)
    {
        p_ds_key->ipv6_extension_headers  = p_sys_key->ext_hdr;
        p_ds_mask->ipv6_extension_headers = p_sys_key->ext_hdr_mask;
    }

    /* cos */
    if (p_sys_key->flag.cos)
    {
        p_ds_key->cos  = p_sys_key->cos;
        p_ds_mask->cos = 7;
    }
#endif /* _if0_ */

    /* is tcp */
    if (p_sys_key->flag.is_tcp)
    {
        p_ds_key->is_tcp  = p_sys_key->is_tcp;
        p_ds_mask->is_tcp = 1;
    }

    /* is udp */
    if (p_sys_key->flag.is_udp)
    {
        p_ds_key->is_udp  = p_sys_key->is_udp;
        p_ds_mask->is_udp = 1;
    }

    /* l4 source port */
    if (p_sys_key->flag.l4_src_port)
    {
        p_ds_key->l4_source_port  = p_sys_key->l4_src_port;
        p_ds_mask->l4_source_port = p_sys_key->l4_src_port_mask;
    }

    /* l4 destination port */
    if (p_sys_key->flag.l4_dst_port)
    {
        p_ds_key->l4_dest_port  = p_sys_key->l4_dst_port;
        p_ds_mask->l4_dest_port = p_sys_key->l4_dst_port_mask;
    }

    /* dscp */
    if (p_sys_key->flag.dscp)
    {
        p_ds_key->dscp  = p_sys_key->dscp;
        p_ds_mask->dscp = p_sys_key->dscp_mask;
    }

    /* ip fragement */
    if (p_sys_key->flag.frag_info)
    {
        p_ds_key->frag_info  = p_sys_key->frag_info;
        p_ds_mask->frag_info = p_sys_key->frag_info_mask;
    }

    /* ip header error */
    if (p_sys_key->flag.ip_hdr_error)
    {
        p_ds_key->ip_header_error  = p_sys_key->ip_hdr_error;
        p_ds_mask->ip_header_error = 1;
    }

    /* flow label */
    if (p_sys_key->flag.flow_label)
    {
        p_ds_key->ipv6_flow_label7_0  = p_sys_key->flow_label & 0xFF;
        p_ds_key->ipv6_flow_label19_8 = p_sys_key->flow_label >> 8;

        p_ds_mask->ipv6_flow_label7_0  = p_sys_key->flow_label_mask & 0xFF;
        p_ds_mask->ipv6_flow_label19_8 = p_sys_key->flow_label_mask >> 8;
    }

    /* mac da */
    if (p_sys_key->flag.mac_da)
    {
        SYS_ACL_SET_MAC_HIGH(p_ds_key->mac_da47_32, p_sys_key->mac_da);
        SYS_ACL_SET_MAC_LOW(p_ds_key->mac_da31_0, p_sys_key->mac_da);

        SYS_ACL_SET_MAC_HIGH(p_ds_mask->mac_da47_32, p_sys_key->mac_da_mask);
        SYS_ACL_SET_MAC_LOW(p_ds_mask->mac_da31_0, p_sys_key->mac_da_mask);
    }

    /* mac sa */
    if (p_sys_key->flag.mac_sa)
    {
        SYS_ACL_SET_MAC_HIGH(p_ds_key->mac_sa47_32, p_sys_key->mac_sa);
        SYS_ACL_SET_MAC_LOW(p_ds_key->mac_sa31_0, p_sys_key->mac_sa);

        SYS_ACL_SET_MAC_HIGH(p_ds_mask->mac_sa47_32, p_sys_key->mac_sa_mask);
        SYS_ACL_SET_MAC_LOW(p_ds_mask->mac_sa31_0, p_sys_key->mac_sa_mask);
    }

    /* cvlan id */
    if (p_sys_key->flag.cvlan)
    {
        p_ds_key->cvlan_id  = p_sys_key->cvlan;
        p_ds_mask->cvlan_id = p_sys_key->cvlan_mask;
    }

    /* ctag cos */
    if (p_sys_key->flag.ctag_cos)
    {
        p_ds_key->ctag_cos  = p_sys_key->ctag_cos;
        p_ds_mask->ctag_cos = p_sys_key->ctag_cos_mask;
    }

    /* ctag cfi */
    if (p_sys_key->flag.ctag_cfi)
    {
        p_ds_key->ctag_cfi  = p_sys_key->ctag_cfi;
        p_ds_mask->ctag_cfi = 1;
    }

    /* svlan id */
    if (p_sys_key->flag.svlan)
    {
        p_ds_key->svlan_id  = p_sys_key->svlan;
        p_ds_mask->svlan_id = p_sys_key->svlan_mask;
    }

    /* stag cos */
    if (p_sys_key->flag.stag_cos)
    {
        p_ds_key->stag_cos  = p_sys_key->stag_cos;
        p_ds_mask->stag_cos = p_sys_key->stag_cos_mask;
    }

    /* stag cfi */
    if (p_sys_key->flag.stag_cfi)
    {
        p_ds_key->stag_cfi  = p_sys_key->stag_cfi;
        p_ds_mask->stag_cfi = 1;
    }

    /* l2 type */
    if (p_sys_key->flag.l2_type)
    {
        p_ds_key->layer2_type  = p_sys_key->l2_type;
        p_ds_mask->layer2_type = p_sys_key->l2_type_mask;
    }

    /* l3 type */
    if (p_sys_key->flag.l3_type)
    {
        p_ds_key->layer3_type  = p_sys_key->l3_type;
        p_ds_mask->layer3_type = p_sys_key->l3_type_mask;
    }

    /* vrfid */
    if (p_sys_key->flag.vrfid)
    {
        p_ds_key->vrf_id3_0   = p_sys_key->vrfid & 0xF;
        p_ds_key->vrf_id13_4  = (p_sys_key->vrfid >> 4) & 0x3FF;
        p_ds_mask->vrf_id3_0  = p_sys_key->vrfid_mask & 0xF;
        p_ds_mask->vrf_id13_4 = (p_sys_key->vrfid_mask >> 4) & 0x3FF;
    }

    /* ether type */
    if (p_sys_key->flag.eth_type)
    {
        p_ds_key->ether_type7_0  = p_sys_key->eth_type & 0xFF;
        p_ds_key->ether_type15_8 = p_sys_key->eth_type >> 8;

        p_ds_mask->ether_type7_0  = p_sys_key->eth_type_mask & 0xFF;
        p_ds_mask->ether_type15_8 = p_sys_key->eth_type_mask >> 8;
    }

    /* non-property field */
    p_ds_key->table_id0     = IPV6SA_TABLEID;
    p_ds_key->table_id1     = IPV6SA_TABLEID;
    p_ds_key->table_id2     = IPV6SA_TABLEID;
    p_ds_key->table_id3     = IPV6SA_TABLEID;
    p_ds_key->table_id4     = IPV6SA_TABLEID;
    p_ds_key->table_id5     = IPV6SA_TABLEID;
    p_ds_key->table_id6     = IPV6SA_TABLEID;
    p_ds_key->table_id7     = IPV6SA_TABLEID;
    p_ds_key->sub_table_id1 = IPV6SA_PBR_SUB_TABLEID;
    p_ds_key->sub_table_id3 = IPV6SA_PBR_SUB_TABLEID;
    p_ds_key->sub_table_id5 = IPV6SA_PBR_SUB_TABLEID;
    p_ds_key->sub_table_id7 = IPV6SA_PBR_SUB_TABLEID;
    if (p_sys_key->port.valid)
    {
        p_ds_key->pbr_label  = p_sys_key->port.label[0];
    }
    else
    {
        p_ds_key->pbr_label  = p_info->label[0];
    }

    p_ds_mask->table_id0     = 0x7;
    p_ds_mask->table_id1     = 0x7;
    p_ds_mask->table_id2     = 0x7;
    p_ds_mask->table_id3     = 0x7;
    p_ds_mask->table_id4     = 0x7;
    p_ds_mask->table_id5     = 0x7;
    p_ds_mask->table_id6     = 0x7;
    p_ds_mask->table_id7     = 0x7;
    p_ds_mask->sub_table_id1 = 0x3;
    p_ds_mask->sub_table_id3 = 0x3;
    p_ds_mask->sub_table_id5 = 0x3;
    p_ds_mask->sub_table_id7 = 0x3;
    if (p_sys_key->port.valid)
    {
        p_ds_mask->pbr_label = p_sys_key->port.mask[0];
    }
    else
    {
        p_ds_mask->pbr_label = p_info->mask[0];
    }
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_acl_remove_cam(uint8 lchip, uint32 key_index)
{
    uint32                            cmd = 0;
    dynamic_ds_fdb_lookup_index_cam_t cam;

    SYS_ACL_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "free cam key, key_index:0x%x\n", key_index);

    sal_memset(&cam, 0, sizeof(cam));

    cmd = DRV_IOW(DynamicDsFdbLookupIndexCam_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, key_index / 2, cmd, &cam));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_acl_add_cam(uint8 lchip, uint32 key_index, void* ds_hash_key)
{
    uint32                            cmd = 0;

    SYS_ACL_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "write cam key, key_index:0x%x\n", key_index);

    cmd = DRV_IOW(DynamicDsFdbLookupIndexCam_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, key_index / 2, cmd,  ds_hash_key));

    return CTC_E_NONE;
}

int32
_sys_greatbelt_acl_set_hash_valid(uint8 lchip, uint16 key_index,uint32 key_type,uint8 valid)
{
    uint32 cmd = 0;
    uint32 field_val = 0;

    field_val = valid;

    if(key_index < acl_master[lchip]->hash_base)
    {
        cmd = DRV_IOW(DynamicDsFdbLookupIndexCam_t, DynamicDsFdbLookupIndexCam_Valid0_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, key_index / 2, cmd, &field_val));
        cmd = DRV_IOW(DynamicDsFdbLookupIndexCam_t, DynamicDsFdbLookupIndexCam_Valid1_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, key_index / 2, cmd, &field_val));
    }
    else
    {
        if(CTC_ACL_KEY_HASH_MAC == key_type)
        {
            cmd = DRV_IOW(DsAclQosMacHashKey_t, DsAclQosMacHashKey_Valid0_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, key_index - acl_master[lchip]->hash_base, cmd, &field_val));
            cmd = DRV_IOW(DsAclQosMacHashKey_t, DsAclQosMacHashKey_Valid1_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, key_index - acl_master[lchip]->hash_base, cmd, &field_val));
        }
        else
        {
            cmd = DRV_IOW(DsAclQosIpv4HashKey_t, DsAclQosIpv4HashKey_Valid0_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, key_index - acl_master[lchip]->hash_base, cmd, &field_val));
            cmd = DRV_IOW(DsAclQosIpv4HashKey_t, DsAclQosIpv4HashKey_Valid1_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, key_index - acl_master[lchip]->hash_base, cmd, &field_val));
        }
    }
    return CTC_E_NONE;
}
/*
 * remove entry specified by entry id from hardware table
 */
STATIC int32
_sys_greatbelt_acl_remove_hw(uint8 lchip, uint32 eid)
{
    uint8               block_id  = 0;
    uint8               alloc_id  = 0;
    uint32              index = 0;
    uint32              hw_index_base = 0;

    sys_acl_group_t     * pg = NULL;
    sys_acl_entry_t     * pe = NULL;

    SYS_ACL_DBG_FUNC();
    SYS_ACL_DBG_PARAM("  %% PARAM: eid: %u \n", eid);

    /* get sys entry */
    _sys_greatbelt_acl_get_nodes_by_eid(lchip, eid, &pe, &pg, NULL);
    if(!pe)
    {
        return CTC_E_ACL_ENTRY_UNEXIST;
    }
    if(!pg)
    {
        return CTC_E_ACL_GROUP_UNEXIST;
    }

    /* get block_id, index */
    block_id = pg->group_info.block_id;
    index  = pe->fpae.offset_a;

    if (ACL_ENTRY_IS_TCAM(pe->key.type))
    {
        /* egs ipv4/ipv6 block hw index should add igs ipv4/ipv6 block total size */
        CTC_ERROR_RETURN(_sys_greatbelt_acl_get_tcam_alloc_info(lchip, pe->key.type, &(pg->group_info), &alloc_id, &hw_index_base));
        index += hw_index_base;
    }

    /*Although remove hw ,but still to set hw valid bit*/
    switch (pe->key.type)
    {
    case CTC_ACL_KEY_MAC:
    {
        DRV_TCAM_TBL_REMOVE(lchip, DsAclMacKey0_t + block_id, index);
    }
    break;

    case CTC_ACL_KEY_IPV4:
    {
        DRV_TCAM_TBL_REMOVE(lchip, DsAclIpv4Key0_t + block_id, index);
    }
    break;

    case CTC_ACL_KEY_IPV6:
    {
        DRV_TCAM_TBL_REMOVE(lchip, DsAclIpv6Key0_t + block_id, index);
    }
    break;

    case CTC_ACL_KEY_MPLS:
    {
        DRV_TCAM_TBL_REMOVE(lchip, DsAclMplsKey0_t + block_id, index);
    }
    break;

    case CTC_ACL_KEY_HASH_MAC:
    {
        /* remove key */
        if (index < acl_master[lchip]->hash_base)
        {
            _sys_greatbelt_acl_remove_cam(lchip, index);
        }
        else
        {
            hash_io_by_idx_para_t by_idx;
            sal_memset(&by_idx, 0, sizeof(by_idx));
            by_idx.chip_id = lchip;
            by_idx.table_id    = DsAclQosMacHashKey_t;
            by_idx.table_index = index - acl_master[lchip]->hash_base;
            by_idx.key         = NULL;

            DRV_HASH_KEY_IOCTL(&by_idx, HASH_OP_TP_DEL_BY_INDEX, NULL);
        }
    }
    break;

    case CTC_ACL_KEY_HASH_IPV4:
    {
        /* remove key */
        if (index < acl_master[lchip]->hash_base)
        {
            _sys_greatbelt_acl_remove_cam(lchip, index);
        }
        else
        {
            hash_io_by_idx_para_t by_idx;
            sal_memset(&by_idx, 0, sizeof(by_idx));
            by_idx.chip_id     = lchip;
            by_idx.table_id    = DsAclQosIpv4HashKey_t;
            by_idx.table_index = index - acl_master[lchip]->hash_base;
            by_idx.key         = NULL;

            DRV_HASH_KEY_IOCTL(&by_idx, HASH_OP_TP_DEL_BY_INDEX, NULL);
        }
    }
    break;

    case CTC_ACL_KEY_PBR_IPV4:
    {
        DRV_TCAM_TBL_REMOVE(lchip, DsIpv4PbrKey_t, index);
    }
    break;

    case CTC_ACL_KEY_PBR_IPV6:
    {
        DRV_TCAM_TBL_REMOVE(lchip, DsIpv6PbrKey_t, index);
    }
    break;

    default:
        return CTC_E_ACL_INVALID_KEY_TYPE;
    }

    if (!ACL_ENTRY_IS_TCAM(pe->key.type)) /* hash entry */
    {
        _sys_greatbelt_acl_set_hash_valid(lchip, index,pe->key.type,1);
    }
    return CTC_E_NONE;
}


/*
 * add entry specified by entry id to hardware table
 */
STATIC int32
_sys_greatbelt_acl_add_hw(uint8 lchip, uint32 eid)
{
    uint8  block_id = 0;
    uint8  alloc_id = 0;
    uint32 p_key_index = 0;
    uint32 p_ad_index = 0;
    uint32 cmd = 0;
    uint32 dm_type = 0;
    uint32 hw_index_base = 0;

    sys_acl_group_t                  * pg = NULL;
    sys_acl_entry_t                  * pe = NULL;

    sys_acl_group_info_t             * p_info;
    drv_acl_group_info_t             drv_info;

    tbl_entry_t                      tcam_key;
    ds_acl_t                         ds_acl;
    ds_ipv4_ucast_pbr_dual_da_tcam_t ds_pbr;
    uint32                           ds_hash_key[6] = { 0 };

    SYS_ACL_DBG_FUNC();
    SYS_ACL_DBG_PARAM("  %% PARAM: eid: %u \n", eid);

    sal_memset(&ds_acl, 0, sizeof(ds_acl));
    sal_memset(&ds_pbr, 0, sizeof(ds_pbr));
    sal_memset(&drv_info, 0, sizeof(drv_info));
    sal_memset(&tcam_key, 0, sizeof(tcam_key));

    /* get sys entry */
    _sys_greatbelt_acl_get_nodes_by_eid(lchip, eid, &pe, &pg, NULL);
    if(!pe)
    {
        return CTC_E_ACL_ENTRY_UNEXIST;
    }
    if(!pg)
    {
        return CTC_E_ACL_GROUP_UNEXIST;
    }

    /* get block_id, p_ad_index */
    block_id    = pg->group_info.block_id;
    p_key_index = pe->fpae.offset_a;

    if (!ACL_ENTRY_IS_TCAM(pe->key.type))
    {
        p_ad_index = pe->action->chip.ad_index;
        dm_type = SYS_AGING_MAC_HASH;
    }
    else
    {
        /* egs ipv4/ipv6 block hw index should add igs ipv4/ipv6 block total size */
        CTC_ERROR_RETURN(_sys_greatbelt_acl_get_tcam_alloc_info(lchip, pe->key.type, &(pg->group_info), &alloc_id, &hw_index_base));
        p_key_index += hw_index_base;
        p_ad_index = p_key_index;
        dm_type = SYS_AGING_TCAM_8k;
    }

    /* get group_info */
    p_info = &(pg->group_info);

    /* build drv group_info */
    CTC_ERROR_RETURN(_sys_greatbelt_acl_build_ds_group_info(lchip, p_info, &drv_info));

    if ((pe->key.type == CTC_ACL_KEY_PBR_IPV4) || (pe->key.type == CTC_ACL_KEY_PBR_IPV6))
    {
        /* get pbr action */
        CTC_ERROR_RETURN(_sys_greatbelt_acl_build_pbr_ds_action(lchip, pe->action, &ds_pbr));

        /* build drv_action. write asic */
        if (pe->key.type == CTC_ACL_KEY_PBR_IPV4)
        {
            cmd = DRV_IOW(DsIpv4UcastPbrDualDaTcam_t, DRV_ENTRY_FLAG);
        }
        else
        {
            cmd = DRV_IOW(DsIpv6UcastPbrDualDaTcam_t, DRV_ENTRY_FLAG);
        }

        CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_ad_index, cmd, &ds_pbr));
    }
    else
    {
        /* get action */
        CTC_ERROR_RETURN(_sys_greatbelt_acl_build_ds_action(lchip, pe->action, &ds_acl));

        /* build drv_action. write asic */
        switch (pe->key.type)
        {
        case CTC_ACL_KEY_MAC:
            cmd = DRV_IOW(DsMacAcl0Tcam_t + block_id, DRV_ENTRY_FLAG);
            break;

        case CTC_ACL_KEY_IPV4:
        case CTC_ACL_KEY_MPLS:
            cmd = DRV_IOW(DsIpv4Acl0Tcam_t + block_id, DRV_ENTRY_FLAG);
            break;

        case CTC_ACL_KEY_IPV6:
            cmd = DRV_IOW(DsIpv6Acl0Tcam_t + block_id, DRV_ENTRY_FLAG);
            break;

        case CTC_ACL_KEY_HASH_MAC:
        case CTC_ACL_KEY_HASH_IPV4:
            cmd = DRV_IOW(DsAcl_t, DRV_ENTRY_FLAG);
            break;

        case CTC_ACL_KEY_PBR_IPV4:
        case CTC_ACL_KEY_PBR_IPV6:
            break;

        default:
            return CTC_E_ACL_INVALID_KEY_TYPE;
        }

        CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_ad_index, cmd, &ds_acl));
    }

    /* get key, build drv_key. write asic */
    SYS_ACL_DBG_INFO("  %% key_type: %d block_id %d\n", pe->key.type, block_id);

    switch (pe->key.type)
    {
    case CTC_ACL_KEY_MAC:
    {
        ds_acl_mac_key0_t data;
        ds_acl_mac_key0_t mask;
        sal_memset(&data, 0, sizeof(data));
        sal_memset(&mask, 0, sizeof(mask));

        _sys_greatbelt_acl_build_ds_mac_key(lchip, &drv_info,
                                            &pe->key.u0.mac_key, &data, &mask);
        tcam_key.data_entry = (uint32 *) &data;
        tcam_key.mask_entry = (uint32 *) &mask;

        cmd = DRV_IOW(DsAclMacKey0_t + block_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_key_index, cmd, &tcam_key));
    }
    break;

    case CTC_ACL_KEY_IPV4:
    {
        ds_acl_ipv4_key0_t data;
        ds_acl_ipv4_key0_t mask;
        sal_memset(&data, 0, sizeof(data));
        sal_memset(&mask, 0, sizeof(mask));

        _sys_greatbelt_acl_build_ds_ipv4_key(lchip, &drv_info,
                                             &pe->key.u0.ipv4_key, &data, &mask);
        tcam_key.data_entry = (uint32 *) &data;
        tcam_key.mask_entry = (uint32 *) &mask;

        cmd = DRV_IOW(DsAclIpv4Key0_t + block_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_key_index, cmd, &tcam_key));
    }
    break;

    case CTC_ACL_KEY_IPV6:
    {
        ds_acl_ipv6_key0_t data;
        ds_acl_ipv6_key0_t mask;
        sal_memset(&data, 0, sizeof(data));
        sal_memset(&mask, 0, sizeof(mask));

        _sys_greatbelt_acl_build_ds_ipv6_key(lchip, &drv_info,
                                             &pe->key.u0.ipv6_key, &data, &mask);
        tcam_key.data_entry = (uint32 *) &data;
        tcam_key.mask_entry = (uint32 *) &mask;

        cmd = DRV_IOW(DsAclIpv6Key0_t + block_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_key_index, cmd, &tcam_key));
    }
    break;

    case CTC_ACL_KEY_MPLS:
    {
        ds_acl_mpls_key0_t data;
        ds_acl_mpls_key0_t mask;
        sal_memset(&data, 0, sizeof(data));
        sal_memset(&mask, 0, sizeof(mask));

        _sys_greatbelt_acl_build_ds_mpls_key(lchip, &drv_info,
                                             &pe->key.u0.mpls_key, &data, &mask);
        tcam_key.data_entry = (uint32 *) &data;
        tcam_key.mask_entry = (uint32 *) &mask;

        cmd = DRV_IOW(DsAclMplsKey0_t + block_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_key_index, cmd, &tcam_key));
    }
    break;

    case CTC_ACL_KEY_HASH_MAC:
    {
        _sys_greatbelt_acl_build_ds_hash_mac_key(lchip,
            pe, (void *) ds_hash_key, p_ad_index);

        /* write key */
        if (p_key_index < acl_master[lchip]->hash_base)
        {
            _sys_greatbelt_acl_add_cam(lchip, p_key_index, ds_hash_key);
        }
        else
        {
            cmd = DRV_IOW(DsAclQosMacHashKey_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_key_index - acl_master[lchip]->hash_base, cmd, ds_hash_key));
        }
    }
    break;

    case CTC_ACL_KEY_HASH_IPV4:
    {
        _sys_greatbelt_acl_build_ds_hash_ipv4_key(lchip,
            pe, (void *) ds_hash_key, p_ad_index);

        /* write key */
        if (p_key_index < acl_master[lchip]->hash_base)
        {
            _sys_greatbelt_acl_add_cam(lchip, p_key_index, ds_hash_key);
        }
        else
        {
            cmd = DRV_IOW(DsAclQosIpv4HashKey_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_key_index - acl_master[lchip]->hash_base, cmd, ds_hash_key));
        }
    }
    break;

    case CTC_ACL_KEY_PBR_IPV4:
    {
        ds_ipv4_pbr_key_t data;
        ds_ipv4_pbr_key_t mask;
        sal_memset(&data, 0, sizeof(data));
        sal_memset(&mask, 0, sizeof(mask));

        _sys_greatbelt_acl_build_pbr_ds_ipv4_key(lchip, &drv_info,
                                                 &pe->key.u0.pbr_ipv4_key, &data, &mask);
        tcam_key.data_entry = (uint32 *) &data;
        tcam_key.mask_entry = (uint32 *) &mask;

        cmd = DRV_IOW(DsIpv4PbrKey_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_key_index, cmd, &tcam_key));
    }
    break;

    case CTC_ACL_KEY_PBR_IPV6:
    {
        ds_ipv6_pbr_key_t data;
        ds_ipv6_pbr_key_t mask;
        sal_memset(&data, 0, sizeof(data));
        sal_memset(&mask, 0, sizeof(mask));

        _sys_greatbelt_acl_build_pbr_ds_ipv6_key(lchip, &drv_info,
                                                 &pe->key.u0.pbr_ipv6_key, &data, &mask);
        tcam_key.data_entry = (uint32 *) &data;
        tcam_key.mask_entry = (uint32 *) &mask;

        cmd = DRV_IOW(DsIpv6PbrKey_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_key_index, cmd, &tcam_key));
    }
    break;

    default:
        return CTC_E_ACL_INVALID_KEY_TYPE;
    }

    if (pe->action->flag.aging)
    {
        CTC_ERROR_RETURN(sys_greatbelt_aging_set_aging_status(lchip, dm_type, p_key_index, TRUE));
    }
    else
    {
        CTC_ERROR_RETURN(sys_greatbelt_aging_set_aging_status(lchip, dm_type, p_key_index, FALSE));
    }


    return CTC_E_NONE;
}

/*
 * install entry to hardware table
 */
STATIC int32
_sys_greatbelt_acl_install_entry(uint8 lchip, uint32 eid, uint8 flag, uint8 move_sw)
{
    sys_acl_group_t* pg = NULL;
    sys_acl_entry_t* pe = NULL;

    SYS_ACL_DBG_FUNC();
    SYS_ACL_DBG_PARAM("  %% PARAM: eid %u \n", eid);

    /* get sys entry */
    _sys_greatbelt_acl_get_nodes_by_eid(lchip, eid, &pe, &pg, NULL);
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


            CTC_ERROR_RETURN(_sys_greatbelt_acl_add_hw(lchip, eid));

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

            CTC_ERROR_RETURN(_sys_greatbelt_acl_remove_hw(lchip, eid));
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
_sys_greatbelt_acl_get_block_by_pe_fpa(uint8 lchip, ctc_fpa_entry_t* pe, ctc_fpa_block_t** pb)
{
    uint8          block_id;
    uint8          ftm;
    sys_acl_entry_t* entry;

    CTC_PTR_VALID_CHECK(pe);
    CTC_PTR_VALID_CHECK(pb);

    *pb = NULL;

    entry = ACL_OUTER_ENTRY(pe);
    CTC_PTR_VALID_CHECK(entry->group);

    CTC_ERROR_RETURN(_sys_greatbelt_acl_get_tcam_alloc_info(lchip, entry->key.type, &(entry->group->group_info), &ftm, NULL));
    block_id = entry->group->group_info.block_id;


    *pb = &(acl_master[lchip]->block[ftm][block_id].fpab);

    return CTC_E_NONE;
}


/*
 * move entry in hardware table to an new index.
 */
STATIC int32
_sys_greatbelt_acl_entry_move_hw_fpa(uint8 lchip,
                                     ctc_fpa_entry_t* pe,
                                     int32 tcam_idx_new)
{
    int32 tcam_idx_old = pe->offset_a;
    uint32 eid;


    CTC_PTR_VALID_CHECK(pe);
    SYS_ACL_DBG_FUNC();

    eid = pe->entry_id;

    /* add first */
    pe->offset_a = tcam_idx_new;
    CTC_ERROR_RETURN(_sys_greatbelt_acl_add_hw(lchip, eid));

    /* then delete */
    pe->offset_a = tcam_idx_old;
    CTC_ERROR_RETURN(_sys_greatbelt_acl_remove_hw(lchip, eid));

    /* set new_index */
    pe->offset_a = tcam_idx_new;

    return CTC_E_NONE;
}

#define __ACL_L4_INFO_MAPPED__

int32
sys_greatbelt_acl_show_tcp_flags(uint8 lchip)
{
    uint8             idx1;
    sys_acl_tcp_flag_t* p_tcp = NULL;
    SYS_ACL_INIT_CHECK(lchip);

    SYS_ACL_LOCK(lchip);
    p_tcp = acl_master[lchip]->tcp_flag;

    SYS_ACL_DBG_FUNC();

    SYS_ACL_DBG_DUMP("Index     OP          TCP_FLAG     REF_CNT\n");
    SYS_ACL_DBG_DUMP("------------------------------------------\n");

    for (idx1 = 0; idx1 < SYS_ACL_TCP_FLAG_NUM; idx1++)
    {
        if (p_tcp[idx1].ref)
        {
            SYS_ACL_DBG_DUMP("%-6u %-12s   0x%02x         %-6u\n",
                              idx1, (p_tcp[idx1].match_any) ? "match_any" : "match_all", p_tcp[idx1].tcp_flags, p_tcp[idx1].ref);
        }
    }
    SYS_ACL_UNLOCK(lchip);

    return CTC_E_NONE;
}

int32
sys_greatbelt_acl_show_port_range(uint8 lchip)
{
    uint8              idx1;
    sys_acl_l4port_op_t* p_pop = NULL;
    SYS_ACL_INIT_CHECK(lchip);

    SYS_ACL_LOCK(lchip);
    p_pop = acl_master[lchip]->l4_port;

    SYS_ACL_DBG_FUNC();

    SYS_ACL_DBG_DUMP("Index     OP       MIN     MAX     REF_CNT\n");
    SYS_ACL_DBG_DUMP("------------------------------------------\n");

    for (idx1 = 0; idx1 < SYS_ACL_L4_PORT_NUM; idx1++)
    {
        if (p_pop[idx1].ref)
        {
            SYS_ACL_DBG_DUMP("%-6u %-11s %-5u   %-5u   %-4u\n",
                              idx1, (p_pop[idx1].op_dest_port) ? "dest_port" : "src_port ",
                              p_pop[idx1].port_min, p_pop[idx1].port_max, p_pop[idx1].ref);
        }
    }
    SYS_ACL_UNLOCK(lchip);

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_acl_lkup_tcp_flag(uint8 lchip, uint8 match_any, uint8 tcp_flags,
                                 uint8* avail_idx, uint8* lkup_idx)
{
    uint8                      idx1;
    uint8                      hit     = 0;
    sys_acl_tcp_flag_t         * p_tcp = NULL;
    sys_parser_l4flag_op_ctl_t parser;

    p_tcp = acl_master[lchip]->tcp_flag;

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
_sys_greatbelt_acl_add_tcp_flag(uint8 lchip, uint8 match_any, uint8 tcp_flags,
                                uint8 target_idx)
{
    sys_acl_tcp_flag_t         * p_tcp = NULL;
    sys_parser_l4flag_op_ctl_t parser;

    p_tcp = acl_master[lchip]->tcp_flag;

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
        CTC_ERROR_RETURN(sys_greatbelt_parser_set_layer4_flag_op_ctl(lchip, target_idx, &parser));

    /* write to software */
    p_tcp[target_idx].ref       = 1;
    p_tcp[target_idx].match_any = match_any;
    p_tcp[target_idx].tcp_flags = tcp_flags & 0x3F;

    if (acl_master[lchip]->tcp_flag_free_cnt > 0)
    {
        acl_master[lchip]->tcp_flag_free_cnt--;
    }

    SYS_ACL_DBG_INFO("add tcp flag, index = %u, match_any = %u, flags = 0x%x\n",
                     target_idx, match_any, tcp_flags);

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_acl_remove_tcp_flag(uint8 lchip, uint8 index)
{
    sys_acl_tcp_flag_t         * p_tcp = NULL;

    sys_parser_l4flag_op_ctl_t parser;

    p_tcp = acl_master[lchip]->tcp_flag;

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
            CTC_ERROR_RETURN(sys_greatbelt_parser_set_layer4_flag_op_ctl(lchip, index, &parser));

        /* write to software */
        p_tcp[index].match_any = 0;
        p_tcp[index].tcp_flags = 0;
    }

    if (acl_master[lchip]->tcp_flag_free_cnt < SYS_ACL_TCP_FLAG_NUM)
    {
        acl_master[lchip]->tcp_flag_free_cnt++;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_acl_lkup_l4_port_range(uint8 lchip, uint16 port_min, uint16 port_max,
                                      uint8 op_dest_port,
                                      uint8* avail_idx, uint8* lkup_idx)
{
    uint8              idx1;
    uint8              hit     = 0;
    sys_acl_l4port_op_t* p_pop = NULL;

    p_pop = acl_master[lchip]->l4_port;

#if 0 /* this can avoid using ** */
    p_pop += lchip;
#endif /* _if0_ */
    CTC_PTR_VALID_CHECK(avail_idx);
    CTC_PTR_VALID_CHECK(lkup_idx);
    SYS_ACL_DBG_FUNC();

    for (idx1 = 0; idx1 < SYS_ACL_L4_PORT_NUM; idx1++)
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
_sys_greatbelt_acl_add_l4_port_range(uint8 lchip, uint16 port_min, uint16 port_max,
                                     uint8 op_dest_port, uint8 target_idx)
{
    uint8                       idx1;
    sys_parser_l4_port_op_sel_t sel;
    sys_parser_l4_port_op_ctl_t ctl;
    sys_acl_l4port_op_t         * p_pop = NULL;

    p_pop = acl_master[lchip]->l4_port;

    CTC_MAX_VALUE_CHECK(target_idx, SYS_ACL_L4_PORT_NUM - 1);
    SYS_ACL_DBG_FUNC();

    sal_memset(&sel, 0, sizeof(sys_parser_l4_port_op_sel_t));
    sal_memset(&ctl, 0, sizeof(sys_parser_l4_port_op_ctl_t));

    /* write to hardware */
    /* get sel from sw */
    for (idx1 = 0; idx1 < SYS_ACL_L4_PORT_NUM; idx1++)
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

        CTC_ERROR_RETURN(sys_greatbelt_parser_set_layer4_port_op_sel(lchip, &sel));
        CTC_ERROR_RETURN(sys_greatbelt_parser_set_layer4_port_op_ctl(lchip, target_idx, &ctl));
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

    if (acl_master[lchip]->l4_port_free_cnt > 0)
    {
        acl_master[lchip]->l4_port_free_cnt--;
    }

    SYS_ACL_DBG_INFO("add l4 port entry, index = %u, op_dest_port = %u, \
                     port_min = % d, port_max = % d\n",
                     target_idx, op_dest_port, port_min, port_max);

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_acl_remove_l4_port_range(uint8 lchip, uint8 index)
{
    uint8                       idx1;
    sys_parser_l4_port_op_sel_t sel;
    sys_parser_l4_port_op_ctl_t ctl;
    sys_acl_l4port_op_t         * p_pop = NULL;

    p_pop = acl_master[lchip]->l4_port;

    CTC_MAX_VALUE_CHECK(index, SYS_ACL_L4_PORT_NUM - 1);

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
        for (idx1 = 0; idx1 < SYS_ACL_L4_PORT_NUM; idx1++)
        {
            if (p_pop[idx1].op_dest_port)
            {
                SET_BIT(sel.op_dest_port, idx1);
            }
        }

        /* set new sel */
        CLEAR_BIT(sel.op_dest_port, index);

        /* clear ctl is enough */

            /* sys_greatbelt_parser_set_layer4_port_op_sel(lchip, &sel); */
            sys_greatbelt_parser_set_layer4_port_op_ctl(lchip, index, &ctl);

        /* write to db */
        p_pop[index].port_min     = 0;
        p_pop[index].port_max     = 0;
        p_pop[index].op_dest_port = 0;
    }

    if (acl_master[lchip]->l4_port_free_cnt < SYS_ACL_L4_PORT_NUM)
    {
        acl_master[lchip]->l4_port_free_cnt++;
    }

    return CTC_E_NONE;
}

/*
 * below is build sw struct
 */
#define __ACL_HASH_CALLBACK__


STATIC uint32
_sys_greatbelt_acl_hash_make_group(sys_acl_group_t* pg)
{
    return pg->group_id;
}

STATIC bool
_sys_greatbelt_acl_hash_compare_group(sys_acl_group_t* pg0, sys_acl_group_t* pg1)
{
    return(pg0->group_id == pg1->group_id);
}

STATIC uint32
_sys_greatbelt_acl_hash_make_entry(sys_acl_entry_t* pe)
{
    return pe->fpae.entry_id;
}

STATIC bool
_sys_greatbelt_acl_hash_compare_entry(sys_acl_entry_t* pe0, sys_acl_entry_t* pe1)
{
    return(pe0->fpae.entry_id == pe1->fpae.entry_id);
}

STATIC uint32
_sys_greatbelt_acl_hash_make_entry_by_key(sys_acl_entry_t* pe)
{
    uint32 size;
    uint8  * k;
    uint8 lchip = 0;
    CTC_PTR_VALID_CHECK(pe);
    lchip = pe->fpae.lchip;
    size = acl_master[lchip]->key_size[pe->key.type];
    k    = (uint8 *) (&pe->key);
    return ctc_hash_caculate(size, k);
}
STATIC bool
_sys_greatbelt_acl_hash_compare_entry_by_key(sys_acl_entry_t* pe0, sys_acl_entry_t* pe1)
{
    uint8 lchip = 0;
    if (!pe0 || !pe1)
    {
        return FALSE;
    }
     lchip = pe0->fpae.lchip;
    if (!sal_memcmp(&pe0->key, &pe1->key, acl_master[lchip]->key_size[pe0->key.type]))
    {
        return TRUE;
    }

    return FALSE;
}
STATIC uint32
_sys_greatbelt_acl_hash_make_action(sys_acl_action_t* pa)
{
    uint32 size;
    uint8  * k;
    uint8 lchip = 0;
    CTC_PTR_VALID_CHECK(pa);
    lchip = pa->lchip;
    size = acl_master[lchip]->hash_action_size;  /* only care prop not related to chip */
    k    = (uint8 *) pa;
    return ctc_hash_caculate(size, k);
}

STATIC bool
_sys_greatbelt_acl_hash_compare_action(sys_acl_action_t* pa0, sys_acl_action_t* pa1)
{
    uint8 lchip = 0;
    if (!pa0 || !pa1)
    {
        return FALSE;
    }
    lchip = pa0->lchip;
    if ((pa0->chip.offset == pa1->chip.offset)
        && (pa0->chip.micro_policer_ptr == pa1->chip.micro_policer_ptr)
        && (pa0->chip.macro_policer_ptr == pa1->chip.macro_policer_ptr)
        && (pa0->chip.stats_ptr == pa1->chip.stats_ptr)
        && (!sal_memcmp(pa0, pa1, acl_master[lchip]->hash_action_size)))
    {
        return TRUE;
    }

    return FALSE;
}

#define __ACL_MAP_UNMAP__

/*
 * p_sys_action_old can be NULL.
 */
STATIC int32
_sys_greatbelt_acl_map_stats_action(uint8 lchip, sys_acl_entry_t*  pe,
                                    ctc_acl_action_t* p_ctc_action,
                                    sys_acl_action_t* p_sys_action,
                                    uint8 is_update,
                                    sys_acl_action_t* p_sys_action_old)
{
    CTC_PTR_VALID_CHECK(pe);
    CTC_PTR_VALID_CHECK(p_ctc_action);
    CTC_PTR_VALID_CHECK(p_sys_action);

    if ((0 == is_update) || (0 == p_sys_action_old->flag.stats))  /* pure add, OR update but old action has no stats*/
    {
        if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_ACL_ACTION_FLAG_STATS))
        {
                CTC_ERROR_RETURN(sys_greatbelt_stats_get_statsptr
                                (lchip, p_ctc_action->stats_id, &p_sys_action->chip.stats_ptr));

            p_sys_action->flag.stats = 1;
        }
    }
    else /* update, but old action has stats.1 -> 1, 1 -> 0 */
    {
        if (!CTC_FLAG_ISSET(p_ctc_action->flag, CTC_ACL_ACTION_FLAG_STATS))
        {
            p_sys_action->flag.stats = 0;
                p_sys_action->chip.stats_ptr = 0;
        }
        else
        {
            p_sys_action->flag.stats = 1;
                CTC_ERROR_RETURN(sys_greatbelt_stats_get_statsptr
                                (lchip, p_ctc_action->stats_id, &p_sys_action->chip.stats_ptr));
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_acl_check_vlan_edit(uint8 lchip, ctc_acl_vlan_edit_t* p_ctc, uint8* vlan_edit)
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
        (CTC_ACL_VLAN_TAG_OP_REP == p_ctc->stag_op))
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
        (CTC_ACL_VLAN_TAG_OP_REP == p_ctc->ctag_op))
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
_sys_greatbelt_acl_map_action(uint8 lchip, uint8 dir,
                              sys_acl_entry_t*  pe,
                              ctc_acl_action_t* p_ctc_action,
                              sys_acl_action_t* p_sys_action)
{
    uint16             policer_ptr;
    uint32    offset;

    CTC_PTR_VALID_CHECK(pe);
    CTC_PTR_VALID_CHECK(p_ctc_action);
    CTC_PTR_VALID_CHECK(p_sys_action);

    SYS_ACL_DBG_FUNC();

    if ((CTC_EGRESS == dir) || (CTC_BOTH_DIRECTION == dir))
    {
        uint8 egress_not_support = 0;
        egress_not_support += CTC_FLAG_ISSET(p_ctc_action->flag, CTC_ACL_ACTION_FLAG_DENY_BRIDGE);
        egress_not_support += CTC_FLAG_ISSET(p_ctc_action->flag, CTC_ACL_ACTION_FLAG_DENY_LEARNING);
        egress_not_support += CTC_FLAG_ISSET(p_ctc_action->flag, CTC_ACL_ACTION_FLAG_DENY_ROUTE);
        egress_not_support += CTC_FLAG_ISSET(p_ctc_action->flag, CTC_ACL_ACTION_FLAG_PRIORITY_AND_COLOR);
        egress_not_support += CTC_FLAG_ISSET(p_ctc_action->flag, CTC_ACL_ACTION_FLAG_TRUST);
        egress_not_support += CTC_FLAG_ISSET(p_ctc_action->flag, CTC_ACL_ACTION_FLAG_COPY_TO_CPU);
        egress_not_support += CTC_FLAG_ISSET(p_ctc_action->flag, CTC_ACL_ACTION_FLAG_REDIRECT);
        egress_not_support += CTC_FLAG_ISSET(p_ctc_action->flag, CTC_ACL_ACTION_FLAG_DSCP);
        egress_not_support += CTC_FLAG_ISSET(p_ctc_action->flag, CTC_ACL_ACTION_FLAG_COPY_TO_CPU_WITH_TIMESTAMP);
        egress_not_support += CTC_FLAG_ISSET(p_ctc_action->flag, CTC_ACL_ACTION_FLAG_QOS_DOMAIN);
        egress_not_support += CTC_FLAG_ISSET(p_ctc_action->flag, CTC_ACL_ACTION_FLAG_AGING);
        egress_not_support += CTC_FLAG_ISSET(p_ctc_action->flag, CTC_ACL_ACTION_FLAG_VLAN_EDIT);

        if (egress_not_support)
        {
            return CTC_E_ACL_INVALID_EGRESS_ACTION;
        }
    }




    /* discard */
    p_sys_action->flag.discard =
        CTC_FLAG_ISSET(p_ctc_action->flag, CTC_ACL_ACTION_FLAG_DISCARD);

    /* deny bridging */
    p_sys_action->flag.deny_bridge =
        CTC_FLAG_ISSET(p_ctc_action->flag, CTC_ACL_ACTION_FLAG_DENY_BRIDGE);

    /* deny learning */
    p_sys_action->flag.deny_learning =
        CTC_FLAG_ISSET(p_ctc_action->flag, CTC_ACL_ACTION_FLAG_DENY_LEARNING);

    /* copy to cpu */
    p_sys_action->flag.copy_to_cpu =
        CTC_FLAG_ISSET(p_ctc_action->flag, CTC_ACL_ACTION_FLAG_COPY_TO_CPU);

    /* deny route */
    p_sys_action->flag.deny_route =
        CTC_FLAG_ISSET(p_ctc_action->flag, CTC_ACL_ACTION_FLAG_DENY_ROUTE);

    /* timestamp */
    p_sys_action->flag.copy_to_cpu_with_timestamp =
        CTC_FLAG_ISSET(p_ctc_action->flag, CTC_ACL_ACTION_FLAG_COPY_TO_CPU_WITH_TIMESTAMP);

    /*------ above has no value, below has value -----*/

    /* acl white list do here */
    if(acl_master[lchip]->white_list)
    {
        if(CTC_FLAG_ISSET(p_ctc_action->flag, CTC_ACL_ACTION_FLAG_DISCARD))
        {
                CTC_ERROR_RETURN(sys_greatbelt_qos_policer_index_get(lchip, CTC_QOS_POLICER_TYPE_FLOW, SYS_QOS_POLICER_FLOW_ID_DROP, &policer_ptr));

                if ((policer_ptr > 0x1FFF) || (policer_ptr == 0))
                {
                    return CTC_E_INVALID_PARAM;
                }
                p_sys_action->chip.macro_policer_ptr = policer_ptr;

            p_sys_action->flag.macro_flow_policer = 1;
            p_sys_action->macro_policer_id        = p_ctc_action->macro_policer_id;
            p_sys_action->flag.discard = 0;
        }
        else
        {

                CTC_ERROR_RETURN(sys_greatbelt_qos_policer_index_get(lchip, CTC_QOS_POLICER_TYPE_FLOW, SYS_QOS_POLICER_FLOW_ID_RATE_MAX, &policer_ptr));

                if ((policer_ptr > 0x1FFF) || (policer_ptr == 0))
                {
                    return CTC_E_INVALID_PARAM;
                }
                p_sys_action->chip.macro_policer_ptr = policer_ptr;

            p_sys_action->flag.macro_flow_policer = 1;
            p_sys_action->macro_policer_id        = p_ctc_action->macro_policer_id;
        }
    }



    /* micro flow policer */
    if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_ACL_ACTION_FLAG_MICRO_FLOW_POLICER))
    {
        CTC_NOT_EQUAL_CHECK(p_ctc_action->micro_policer_id, 0);

            CTC_ERROR_RETURN(sys_greatbelt_qos_policer_index_get(lchip, CTC_QOS_POLICER_TYPE_FLOW, p_ctc_action->micro_policer_id, &policer_ptr));

            if ((policer_ptr > 0x1FFF) || (policer_ptr == 0))
            {
                return CTC_E_INVALID_PARAM;
            }

            p_sys_action->chip.micro_policer_ptr = policer_ptr;

        p_sys_action->flag.micro_flow_policer = 1;
        p_sys_action->micro_policer_id        = p_ctc_action->micro_policer_id;
    }

    /* macro flow policer */
    if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_ACL_ACTION_FLAG_MACRO_FLOW_POLICER))
    {
        CTC_NOT_EQUAL_CHECK(p_ctc_action->macro_policer_id, 0);
            CTC_ERROR_RETURN(sys_greatbelt_qos_policer_index_get(lchip, CTC_QOS_POLICER_TYPE_FLOW, p_ctc_action->macro_policer_id, &policer_ptr));

            if ((policer_ptr > 0x1FFF) || (policer_ptr == 0))
            {
                return CTC_E_INVALID_PARAM;
            }
            p_sys_action->chip.macro_policer_ptr = policer_ptr;

        p_sys_action->flag.macro_flow_policer = 1;
        p_sys_action->macro_policer_id        = p_ctc_action->macro_policer_id;
    }

    /* random log */
    if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_ACL_ACTION_FLAG_RANDOM_LOG))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_action->log_percent, CTC_LOG_PERCENT_MAX - 1);
        CTC_MAX_VALUE_CHECK(p_ctc_action->log_session_id, SYS_ACL_MAX_SESSION_NUM - 1);

        p_sys_action->flag.random_log        = 1;
        p_sys_action->acl_log_id             = p_ctc_action->log_session_id;
        p_sys_action->random_threshold_shift = p_ctc_action->log_percent;
    }

    /* priority + color */
    if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_ACL_ACTION_FLAG_PRIORITY_AND_COLOR))
    {
        /* valid check */
        CTC_NOT_ZERO_CHECK(p_ctc_action->color);
        CTC_MAX_VALUE_CHECK(p_ctc_action->color, MAX_CTC_QOS_COLOR - 1);
        CTC_MAX_VALUE_CHECK(p_ctc_action->priority, SYS_QOS_CLASS_PRIORITY_MAX);

        p_sys_action->flag.priority_and_color = 1;
        p_sys_action->priority                = p_ctc_action->priority;
        p_sys_action->color                   = p_ctc_action->color;
    }

    /* qos policy (trust) */
    if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_ACL_ACTION_FLAG_TRUST))
    {
        /* 7 is invalid, but that's inside the range.*/
        CTC_MAX_VALUE_CHECK(p_ctc_action->trust, CTC_QOS_TRUST_MAX - 1);
        p_sys_action->flag.trust = 1;
        p_sys_action->trust      = p_ctc_action->trust;
    }

    /* dscp */
    if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_ACL_ACTION_FLAG_DSCP))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_action->dscp, 0x3F);
        p_sys_action->flag.dscp = 1;
        p_sys_action->dscp      = p_ctc_action->dscp;
    }

    /* qos domain */
    if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_ACL_ACTION_FLAG_QOS_DOMAIN))
    {
        if ((0 == p_ctc_action->qos_domain) || (p_ctc_action->qos_domain > 7))
        {
            return CTC_E_INVALID_PARAM;
        }

        p_sys_action->flag.qos_domain = 1;
        p_sys_action->qos_domain      = p_ctc_action->qos_domain;
    }

    /* aging */
    if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_ACL_ACTION_FLAG_AGING))
    {
        p_sys_action->flag.aging = 1;
    }

    /* ds forward */
    if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_ACL_ACTION_FLAG_REDIRECT))
    {
        p_sys_action->nh_id = p_ctc_action->nh_id;

        p_sys_action->flag.redirect = 1;

        /* get nexthop information */
        {
             /*reduce kernel mode stacking size*/
            sys_nh_info_dsnh_t nhinfo;
            sal_memset(&nhinfo, 0, sizeof(nhinfo));

            CTC_ERROR_RETURN(sys_greatbelt_nh_get_nhinfo(lchip, p_ctc_action->nh_id, &nhinfo));

            /* for acl ecmp */
            if (nhinfo.ecmp_valid)
            {
                p_sys_action->ecpn  = (nhinfo.ecmp_num - 1) ;
            }
        }
        CTC_ERROR_RETURN(sys_greatbelt_nh_get_dsfwd_offset(lchip, p_sys_action->nh_id, &offset));

            CTC_NOT_ZERO_CHECK(offset);

            p_sys_action->chip.offset = offset;

        p_sys_action->flag.deny_bridge = 1;
        p_sys_action->flag.deny_route  = 1;

        SYS_ACL_DBG_INFO("p_sys_action->flag.deny_bridge = 1\n");
        SYS_ACL_DBG_INFO("p_sys_action->flag.deny_route = 1\n");
        SYS_ACL_DBG_INFO("p_sys_action->flag.fwd = 1\n");
    }

    if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_ACL_ACTION_FLAG_VLAN_EDIT))
    {
        uint8 vlan_edit = 0;
        CTC_ERROR_RETURN(_sys_greatbelt_acl_check_vlan_edit(lchip, &p_ctc_action->vlan_edit, &vlan_edit));

        if (vlan_edit)
        {
            p_sys_action->flag.vlan_edit = 1;
            p_sys_action->vlan_edit.stag_op = p_ctc_action->vlan_edit.stag_op;
            p_sys_action->vlan_edit.ctag_op = p_ctc_action->vlan_edit.ctag_op;
            p_sys_action->vlan_edit.svid_sl = p_ctc_action->vlan_edit.svid_sl;
            p_sys_action->vlan_edit.scos_sl = p_ctc_action->vlan_edit.scos_sl;
            p_sys_action->vlan_edit.scfi_sl = p_ctc_action->vlan_edit.scfi_sl;
            p_sys_action->vlan_edit.cvid_sl = p_ctc_action->vlan_edit.cvid_sl;
            p_sys_action->vlan_edit.ccos_sl = p_ctc_action->vlan_edit.ccos_sl;
            p_sys_action->vlan_edit.ccfi_sl = p_ctc_action->vlan_edit.ccfi_sl;

            p_sys_action->vlan_edit.svid = p_ctc_action->vlan_edit.svid_new;
            p_sys_action->vlan_edit.scos = p_ctc_action->vlan_edit.scos_new;
            p_sys_action->vlan_edit.scfi = p_ctc_action->vlan_edit.scfi_new;
            p_sys_action->vlan_edit.cvid = p_ctc_action->vlan_edit.cvid_new;
            p_sys_action->vlan_edit.ccos = p_ctc_action->vlan_edit.ccos_new;
            p_sys_action->vlan_edit.ccfi = p_ctc_action->vlan_edit.ccfi_new;
            p_sys_action->vlan_edit.tpid_index = p_ctc_action->vlan_edit.tpid_index;
        }
    }

    /* pbr fwd */
    if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_ACL_ACTION_FLAG_PBR_FWD))
    {
        p_sys_action->flag.pbr_fwd = 1;
        p_sys_action->nh_id        = p_ctc_action->nh_id;
        CTC_ERROR_RETURN(sys_greatbelt_nh_get_dsfwd_offset(lchip, p_ctc_action->nh_id, &offset));

            CTC_NOT_ZERO_CHECK(offset);

            p_sys_action->chip.offset = offset;

        if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_ACL_ACTION_FLAG_PBR_ECMP)) /* pbr ecmp */
        {
            if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_ACL_ACTION_FLAG_PBR_ICMP_CHECK))
            {
                return CTC_E_INVALID_PARAM;
            }
            p_sys_action->flag.pbr_ecmp = 1;

            {
                /*reduce kernel mode stacking size*/
                sys_nh_info_dsnh_t nhinfo;
                sal_memset(&nhinfo, 0, sizeof(nhinfo));
                CTC_ERROR_RETURN(sys_greatbelt_nh_get_nhinfo(lchip, p_ctc_action->nh_id, &nhinfo));
                if (nhinfo.ecmp_valid)
                {
                    p_sys_action->ecpn = (nhinfo.ecmp_num - 1);
                }
            }
        }
    }
    else if ((!CTC_FLAG_ISSET(p_ctc_action->flag, CTC_ACL_ACTION_FLAG_PBR_DENY))
             && ((CTC_ACL_KEY_PBR_IPV4 == pe->key.type)
                 || (CTC_ACL_KEY_PBR_IPV6 == pe->key.type)))
    {
        return CTC_E_ACL_PBR_ENTRY_NO_NXTTHOP;
    }

    /* pbr ttl-check */
    if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_ACL_ACTION_FLAG_PBR_TTL_CHECK))
    {
        p_sys_action->flag.pbr_ttl_check = 1;
    }

    /* pbr icmp-check */
    if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_ACL_ACTION_FLAG_PBR_ICMP_CHECK))
    {
        p_sys_action->flag.pbr_icmp_check = 1;
        CTC_ERROR_RETURN(sys_greatbelt_nh_get_l3ifid(lchip, p_ctc_action->nh_id, &p_sys_action->l3if_id));
        if (p_sys_action->l3if_id == 0)
        {
            return CTC_E_NH_INVALID_NH_TYPE;
        }
    }

    /* pbr copy-to-cpu */
    if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_ACL_ACTION_FLAG_PBR_CPU))
    {
        p_sys_action->flag.pbr_cpu = 1;
    }

    /* pbr deny */
    if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_ACL_ACTION_FLAG_PBR_DENY))
    {
        p_sys_action->flag.pbr_deny = 1;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_acl_map_tcam_key_port(uint8 lchip, ctc_field_port_t* p_ctc_key,
                                sys_acl_port_t* p_sys_key, uint8 is_mpls_key)
{
    uint8 idx = 0;
    uint8 bitmap_num = 0;

    sal_memset(p_sys_key, 0, sizeof(sys_acl_port_t));

    switch (p_ctc_key->type)
    {
        case CTC_FIELD_PORT_TYPE_NONE:
            break;
        case CTC_FIELD_PORT_TYPE_GPORT:
            return CTC_E_INVALID_PARAM;

        case CTC_FIELD_PORT_TYPE_LOGIC_PORT:
            SYS_ACL_SERVICE_ID_CHECK(p_ctc_key->logic_port);
            p_sys_key->use_group      = 0;
            p_sys_key->use_group_mask = 0;
            p_sys_key->is_label       = 0;
            p_sys_key->is_label_mask  = 1;
            p_sys_key->label[0]       = p_ctc_key->logic_port;
            p_sys_key->label[1]       = 0;
            p_sys_key->mask[0]        = p_ctc_key->logic_port ? 0xFFFF : 0;
            p_sys_key->mask[1]        = 0;
            p_sys_key->valid = 1;
            break;

        case CTC_FIELD_PORT_TYPE_VLAN_CLASS:
            SYS_ACL_VLAN_CLASS_ID_CHECK(p_ctc_key->vlan_class_id);
            p_sys_key->use_group      = 1;
            p_sys_key->use_group_mask = 1;
            p_sys_key->is_label       = 1;
            p_sys_key->is_label_mask  = 1;
            p_sys_key->label[0]       = p_ctc_key->vlan_class_id << 10;
            p_sys_key->label[1]       = 0;
            p_sys_key->mask[0]        = 0x3FF << 10;
            p_sys_key->mask[1]        = 0;
            p_sys_key->valid = 1;
            break;

        case CTC_FIELD_PORT_TYPE_PORT_CLASS:
            SYS_ACL_PORT_CLASS_ID_CHECK(p_ctc_key->port_class_id);
            p_sys_key->use_group      = 1;
            p_sys_key->use_group_mask = 1;
            p_sys_key->is_label       = 1;
            p_sys_key->is_label_mask  = 1;
            p_sys_key->label[0]       = p_ctc_key->port_class_id;
            p_sys_key->label[1]       = 0;
            p_sys_key->mask[0]        = 0x3FF;
            p_sys_key->mask[1]        = 0;
            p_sys_key->valid = 1;
            break;

        case CTC_FIELD_PORT_TYPE_PBR_CLASS:
            CTC_ACL_PBR_CLASS_ID_CHECK(p_ctc_key->pbr_class_id);
            p_sys_key->label[0] = p_ctc_key->pbr_class_id;
            p_sys_key->mask[0]  = 0x3F;
            p_sys_key->valid = 1;
            break;

        case CTC_FIELD_PORT_TYPE_PORT_BITMAP:
            if (lchip != p_ctc_key->lchip)
            {
                return CTC_E_NONE;
            }

            bitmap_num = is_mpls_key ? 52 : 56;

            /* port_bitmap only allow to use 52 bits use mpls key, others is 56 bits */
            for (idx = 1; idx < CTC_PORT_BITMAP_IN_WORD; idx++)
            {
                if ((1 == idx) && ((p_ctc_key->port_bitmap[1]) >> (bitmap_num - 32)))
                {
                    return CTC_E_ACL_PORT_BITMAP_OVERFLOW;
                }
                else if ((1 < idx) && (p_ctc_key->port_bitmap[idx]))
                {
                    return CTC_E_ACL_PORT_BITMAP_OVERFLOW;
                }
            }
             /*
             *  example: user care port 00001111 (that is port0 port1 port2 port3).
             *  In hardware,  should be 00000000 - data
             *                          11110000 - mask.
             *  so port0 port1 port2 port3 will hit, other ports won't.
             */

            p_sys_key->use_group      = 0;
            p_sys_key->use_group_mask = 1;
            p_sys_key->is_label       = 1;
            p_sys_key->is_label_mask  = 1;
            p_sys_key->label[0]       = 0;
            p_sys_key->label[1]       = 0;

            /* port_bitmap  31 ~0 */
            p_sys_key->mask[0] = ~(p_ctc_key->port_bitmap[0]);

            /* port_bitmap  51 ~32 */
            p_sys_key->mask[1] = ~(p_ctc_key->port_bitmap[1]);
            p_sys_key->valid = 1;
            break;
        default:
            return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_acl_map_mac_key(uint8 lchip, ctc_acl_mac_key_t* p_ctc_key,
                               sys_acl_mac_key_t* p_sys_key)
{
    uint32 flag = 0;

    CTC_PTR_VALID_CHECK(p_ctc_key);
    CTC_PTR_VALID_CHECK(p_sys_key);

    SYS_ACL_DBG_FUNC();

    flag = p_ctc_key->flag;

    /* mac da */
    if (CTC_FLAG_ISSET(flag, CTC_ACL_MAC_KEY_FLAG_MAC_DA))
    {
        p_sys_key->flag.mac_da = 1;
        sal_memcpy(p_sys_key->mac_da, p_ctc_key->mac_da, CTC_ETH_ADDR_LEN);
        sal_memcpy(p_sys_key->mac_da_mask, p_ctc_key->mac_da_mask, CTC_ETH_ADDR_LEN);
    }

    /* mac sa */
    if (CTC_FLAG_ISSET(flag, CTC_ACL_MAC_KEY_FLAG_MAC_SA))
    {
        p_sys_key->flag.mac_sa = 1;
        sal_memcpy(p_sys_key->mac_sa, p_ctc_key->mac_sa, CTC_ETH_ADDR_LEN);
        sal_memcpy(p_sys_key->mac_sa_mask, p_ctc_key->mac_sa_mask, CTC_ETH_ADDR_LEN);
    }

    /* cvlan */
    if (CTC_FLAG_ISSET(flag, CTC_ACL_MAC_KEY_FLAG_CVLAN))
    {
        SYS_ACL_VLAN_ID_CHECK(p_ctc_key->cvlan);
        p_sys_key->flag.cvlan = 1;
        p_sys_key->cvlan      = p_ctc_key->cvlan;
        p_sys_key->cvlan_mask = p_ctc_key->cvlan_mask & 0xFFF;
    }

    /* ctag cos */
    if (CTC_FLAG_ISSET(flag, CTC_ACL_MAC_KEY_FLAG_CTAG_COS))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->ctag_cos, 0x7);
        p_sys_key->flag.ctag_cos = 1;
        p_sys_key->ctag_cos      = p_ctc_key->ctag_cos;
        p_sys_key->ctag_cos_mask = (p_ctc_key->ctag_cos_mask) & 0x7;
    }

    /* ctag cfi */
    if (CTC_FLAG_ISSET(flag, CTC_ACL_MAC_KEY_FLAG_CTAG_CFI))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->ctag_cfi, 0x1);
        p_sys_key->flag.ctag_cfi = 1;
        p_sys_key->ctag_cfi      = p_ctc_key->ctag_cfi;
    }

    /* svlan */
    if (CTC_FLAG_ISSET(flag, CTC_ACL_MAC_KEY_FLAG_SVLAN))
    {
        SYS_ACL_VLAN_ID_CHECK(p_ctc_key->svlan);
        p_sys_key->flag.svlan = 1;
        p_sys_key->svlan      = p_ctc_key->svlan;
        p_sys_key->svlan_mask = p_ctc_key->svlan_mask & 0xFFF;
    }

    /* stag cos */
    if (CTC_FLAG_ISSET(flag, CTC_ACL_MAC_KEY_FLAG_STAG_COS))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->stag_cos, 0x7);
        p_sys_key->flag.stag_cos = 1;
        p_sys_key->stag_cos      = p_ctc_key->stag_cos;
        p_sys_key->stag_cos_mask = (p_ctc_key->stag_cos_mask) & 0x7;
    }

    /* stag cfi */
    if (CTC_FLAG_ISSET(flag, CTC_ACL_MAC_KEY_FLAG_STAG_CFI))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->stag_cfi, 0x1);
        p_sys_key->flag.stag_cfi = 1;
        p_sys_key->stag_cfi      = p_ctc_key->stag_cfi;
    }

    /* eth type */
    if (CTC_FLAG_ISSET(flag, CTC_ACL_MAC_KEY_FLAG_ETH_TYPE))
    {
        /* Ether_type no need check */
        p_sys_key->flag.eth_type = 1;
        p_sys_key->eth_type      = p_ctc_key->eth_type;
        p_sys_key->eth_type_mask = p_ctc_key->eth_type_mask;
    }

    /* l2 type */
    if (CTC_FLAG_ISSET(flag, CTC_ACL_MAC_KEY_FLAG_L2_TYPE))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->l2_type, 0xF);
        p_sys_key->flag.l2_type = 1;
        p_sys_key->l2_type      = p_ctc_key->l2_type;
        p_sys_key->l2_type_mask = p_ctc_key->l2_type_mask;
    }

    /* l3 type */
    if (CTC_FLAG_ISSET(flag, CTC_ACL_MAC_KEY_FLAG_L3_TYPE))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->l3_type, 0xF);
        p_sys_key->flag.l3_type = 1;
        p_sys_key->l3_type      = p_ctc_key->l3_type;
        p_sys_key->l3_type_mask = p_ctc_key->l3_type_mask;
    }

    /* op code */
    if (CTC_FLAG_ISSET(flag, CTC_ACL_MAC_KEY_FLAG_ARP_OP_CODE))
    {
        /* arp op code no need check */
        p_sys_key->flag.arp_op_code = 1;
        p_sys_key->arp_op_code      = p_ctc_key->arp_op_code;
        p_sys_key->arp_op_code_mask = p_ctc_key->arp_op_code_mask;
    }

    /* ip sa */
    if (CTC_FLAG_ISSET(flag, CTC_ACL_MAC_KEY_FLAG_IP_SA))
    {
        /* ip sa no check */
        p_sys_key->flag.ip_sa = 1;
        p_sys_key->ip_sa      = p_ctc_key->ip_sa;
        p_sys_key->ip_sa_mask = p_ctc_key->ip_sa_mask;
    }

    /* ip da */
    if (CTC_FLAG_ISSET(flag, CTC_ACL_MAC_KEY_FLAG_IP_DA))
    {
        /* ip da no check */
        p_sys_key->flag.ip_da = 1;
        p_sys_key->ip_da      = p_ctc_key->ip_da;
        p_sys_key->ip_da_mask = p_ctc_key->ip_da_mask;
    }

    /* svid_valid */
    if (CTC_FLAG_ISSET(flag, CTC_ACL_MAC_KEY_FLAG_STAG_VALID))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->stag_valid, 1);
        p_sys_key->flag.svlan_id_valid = 1;
        p_sys_key->svlan_id_valid      = p_ctc_key->stag_valid;
    }

    /* cvid_valid */
    if (CTC_FLAG_ISSET(flag, CTC_ACL_MAC_KEY_FLAG_CTAG_VALID))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->ctag_valid, 1);
        p_sys_key->flag.cvlan_id_valid = 1;
        p_sys_key->cvlan_id_valid      = p_ctc_key->ctag_valid;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_acl_map_ipv4_key(uint8 lchip, ctc_acl_ipv4_key_t* p_ctc_key,
                                sys_acl_ipv4_key_t* p_sys_key)
{
    int32  ret        = 0;
    uint32 flag       = 0;
    uint32 sub_flag   = 0;
    uint32 sub3_flag   = 0;
    uint8  idx_lkup   = 0;
    uint8  idx_valid  = 0;
    uint8  idx_target = 0;

    uint16 port0 = 0;
    uint16 port1 = 0;

    uint8  op_dest               = 0; /* loop 2 times, for saving code.*/
    uint8  l4_src_port_use_range = 0; /* src port use range */
    uint8  l4_dst_port_use_range = 0; /* dst port use range */

    CTC_PTR_VALID_CHECK(p_ctc_key);
    CTC_PTR_VALID_CHECK(p_sys_key);

    SYS_ACL_DBG_FUNC();

    flag     = p_ctc_key->flag;
    sub_flag = p_ctc_key->sub_flag;
    sub3_flag = p_ctc_key->sub3_flag;

    if ((CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_L2_TYPE)
         + CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_PRECEDENCE)
         + CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_DSCP)) > 1)
    {
        return CTC_E_ACL_FLAG_COLLISION;
    }

    if ((CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_ETH_TYPE)
         + (CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV4_KEY_SUB_FLAG_L4_DST_PORT)
            && p_ctc_key->l4_dst_port_use_mask)) > 1)
    {
        return CTC_E_ACL_FLAG_COLLISION;
    }

    /*  feature not support: l3_type * cos */
    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_L3_TYPE))
    {
        return CTC_E_FEATURE_NOT_SUPPORT;
    }

    if (!acl_master[lchip]->acl_register.merge) /* no merge_key, some field will not support */
    {
        if ((CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_IPV4_PACKET))
            || (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_ARP_PACKET))
            || (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_ETH_TYPE))
            || (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_L2_TYPE)))
        {
            return CTC_E_INVALID_PARAM;
        }
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_IP_DA) && (CTC_FLAG_ISSET(sub3_flag, CTC_ACL_IPV4_KEY_SUB3_FLAG_ETHOAM_LEVEL)
         || CTC_FLAG_ISSET(sub3_flag, CTC_ACL_IPV4_KEY_SUB3_FLAG_ETHOAM_OPCODE)))
    {
        return CTC_E_ACL_FLAG_COLLISION;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_IP_DA))
    {
        p_sys_key->flag.ip_da = 1;
        p_sys_key->ip_da      = p_ctc_key->ip_da;
        p_sys_key->ip_da_mask = p_ctc_key->ip_da_mask;

        p_sys_key->flag.arp_packet = 1;
        p_sys_key->arp_packet      = 0;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_IP_SA))
    {
        p_sys_key->flag.ip_sa = 1;
        p_sys_key->ip_sa      = p_ctc_key->ip_sa;
        p_sys_key->ip_sa_mask = p_ctc_key->ip_sa_mask;

        p_sys_key->flag.arp_packet = 1;
        p_sys_key->arp_packet      = 0;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_ETH_TYPE))
    {
        /* no check */
        p_sys_key->flag.eth_type = 1;
        p_sys_key->eth_type      = p_ctc_key->eth_type;
        p_sys_key->eth_type_mask = p_ctc_key->eth_type_mask;
    }

    if (CTC_FLAG_ISSET(sub3_flag, CTC_ACL_IPV4_KEY_SUB3_FLAG_ETHOAM_LEVEL))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->ethoam_level, 0x7);
        CTC_MAX_VALUE_CHECK(p_ctc_key->ethoam_level_mask, 0x7);
        p_sys_key->flag.ip_da = 1;
        p_sys_key->ip_da      |= p_ctc_key->ethoam_level << 13;
        p_sys_key->ip_da_mask |= p_ctc_key->ethoam_level_mask << 13;

        p_sys_key->flag.arp_packet = 1;
        p_sys_key->arp_packet      = 0;
    }

    if (CTC_FLAG_ISSET(sub3_flag, CTC_ACL_IPV4_KEY_SUB3_FLAG_ETHOAM_OPCODE))
    {
        p_sys_key->flag.ip_da = 1;
        p_sys_key->ip_da      |= p_ctc_key->ethoam_op_code;
        p_sys_key->ip_da_mask |= p_ctc_key->ethoam_op_code_mask;

        p_sys_key->flag.arp_packet = 1;
        p_sys_key->arp_packet      = 0;
    }

    /* layer 4 information */
    if ((CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_L4_PROTOCOL))
        &&(p_ctc_key->l4_protocol_mask != 0))
    {
        if ((0xFF == p_ctc_key->l4_protocol_mask) &&
            (IPV4_ICMP_PROTOCOL == p_ctc_key->l4_protocol))        /* ICMP */
        {
            p_sys_key->flag.l4info_mapped = 1;
            /* l4_info_mapped for non-tcp non-udp packet, it's l4_type | l3_header_protocol */
            p_sys_key->l4info_mapped      = p_ctc_key->l4_protocol;
            p_sys_key->l4info_mapped_mask = p_ctc_key->l4_protocol_mask;

            p_sys_key->flag.is_tcp = 1;
            p_sys_key->is_tcp      = 0;

            p_sys_key->flag.is_udp = 1;
            p_sys_key->is_udp      = 0;

            /* l4_src_port for flex-l4 (including icmp), it's type|code */

            /* icmp type */
            if (CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV4_KEY_SUB_FLAG_ICMP_TYPE))
            {
                p_sys_key->flag.l4_src_port  = 1;
                p_sys_key->l4_src_port      |= (p_ctc_key->icmp_type) << 8;
                p_sys_key->l4_src_port_mask |= (p_ctc_key->icmp_type_mask) << 8;
            }

            /* icmp code */
            if (CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV4_KEY_SUB_FLAG_ICMP_CODE))
            {
                p_sys_key->flag.l4_src_port  = 1;
                p_sys_key->l4_src_port      |= p_ctc_key->icmp_code & 0x00FF;
                p_sys_key->l4_src_port_mask |= (p_ctc_key->icmp_code_mask) & 0x00FF;
            }
        }
        else if ((0xFF == p_ctc_key->l4_protocol_mask)
                 && (IPV4_IGMP_PROTOCOL == p_ctc_key->l4_protocol)) /* IGMP */
        {
            /* l4_info_mapped for non-tcp non-udp packet, it's l4_type | l3_header_protocol */
            p_sys_key->flag.l4info_mapped = 1;
            p_sys_key->l4info_mapped      = p_ctc_key->l4_protocol;
            p_sys_key->l4info_mapped_mask = p_ctc_key->l4_protocol_mask;

            p_sys_key->flag.is_tcp = 1;
            p_sys_key->is_tcp      = 0;

            p_sys_key->flag.is_udp = 1;
            p_sys_key->is_udp      = 0;

            /* l4_src_port for flex-l4 (including igmp), it's type|..*/
            if (CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV4_KEY_SUB_FLAG_IGMP_TYPE))
            {
                p_sys_key->flag.l4_src_port  = 1;
                p_sys_key->l4_src_port      |= (p_ctc_key->igmp_type << 8) & 0xFF00;
                p_sys_key->l4_src_port_mask |= (p_ctc_key->igmp_type_mask << 8) & 0xFF00;
            }
        }
        else if ((0xFF == p_ctc_key->l4_protocol_mask)
                 && ((TCP_PROTOCOL == p_ctc_key->l4_protocol)
                     || (UDP_PROTOCOL == p_ctc_key->l4_protocol)))   /* TCP or UDP */
        {
            if (TCP_PROTOCOL == p_ctc_key->l4_protocol)
            {   /* TCP */
                p_sys_key->flag.is_tcp = 1;
                p_sys_key->is_tcp      = 1;
            }
            else
            {   /* UDP */
                p_sys_key->flag.is_udp = 1;
                p_sys_key->is_udp      = 1;
            }

            /* caculate use how many range count */
#if 0
            range_cnt = ((CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV4_KEY_SUB_FLAG_L4_SRC_PORT)) &&
                         (!p_ctc_key->l4_src_port_use_mask))
                        + ((CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV4_KEY_SUB_FLAG_L4_DST_PORT)) &&
                           (!p_ctc_key->l4_dst_port_use_mask))
#endif      /* _if0_ */
            if (CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV4_KEY_SUB_FLAG_L4_SRC_PORT))
            { /* op_dest_port = 0 */
                if (p_ctc_key->l4_src_port_use_mask)
                {   /*  use mask.
                     *  l4_src_port_0 is data.
                     *  l4_src_port_1 is mask.
                     */
                    p_sys_key->flag.l4_src_port = 1;
                    p_sys_key->l4_src_port      = p_ctc_key->l4_src_port_0;
                    p_sys_key->l4_src_port_mask = p_ctc_key->l4_src_port_1;
                }
                else
                {   /*  use range.
                     *  l4_src_port_0 is min.
                     *  l4_src_port_1 is max.
                     */
                    if (p_ctc_key->l4_src_port_0 >= p_ctc_key->l4_src_port_1)
                    {
                        return CTC_E_INVALID_PARAM;
                    }

                    l4_src_port_use_range = 1;
                }
            }

            if (CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV4_KEY_SUB_FLAG_L4_DST_PORT))
            {     /* op_dest_port =1 */
                if (p_ctc_key->l4_dst_port_use_mask)
                { /*  use mask.
                   *  l4_dst_port_0 is data.
                   *  l4_dst_port_1 is mask.
                   */
                    p_sys_key->flag.l4_dst_port = 1;
                    p_sys_key->l4_dst_port      = p_ctc_key->l4_dst_port_0;
                    p_sys_key->l4_dst_port_mask = p_ctc_key->l4_dst_port_1;
                }
                else
                {   /*  use range.
                     *  l4_dst_port_0 is min.
                     *  l4_dst_port_1 is max.
                     */
                    if (p_ctc_key->l4_dst_port_0 >= p_ctc_key->l4_dst_port_1)
                    {
                        return CTC_E_INVALID_PARAM;
                    }

                    l4_dst_port_use_range = 1;
                }
            }

            /* op_dest, use loop to save code.
             * op_dest = 0 -->src_port
             * op_dest = 1 -->dst_port
             */
            for (op_dest = 0; op_dest <= 1; op_dest++)
            {
                if (0 == op_dest)
                {
                    if (!l4_src_port_use_range)
                    {
                        continue;
                    }

                    port0 = p_ctc_key->l4_src_port_0;
                    port1 = p_ctc_key->l4_src_port_1;
                }
                else
                {
                    if (!l4_dst_port_use_range)
                    {
                        continue;
                    }

                    port0 = p_ctc_key->l4_dst_port_0;
                    port1 = p_ctc_key->l4_dst_port_1;
                }

                p_sys_key->flag.l4info_mapped = 1;

                CTC_ERROR_RETURN(_sys_greatbelt_acl_lkup_l4_port_range(lchip, port0,
                                                                       port1, op_dest, &idx_target, &idx_lkup));

                if (SYS_ACL_L4_PORT_NUM != idx_lkup)
                {   /* lkup success */
                    acl_master[lchip]->l4_port[idx_lkup].ref++;

                    idx_valid = idx_lkup;
                }
                else if ((SYS_ACL_L4_PORT_NUM == idx_lkup)
                         && (acl_master[lchip]->l4_port_free_cnt > 0))
                {
                    CTC_ERROR_RETURN(_sys_greatbelt_acl_add_l4_port_range
                                         (lchip, port0, port1, op_dest, idx_target));

                    idx_valid = idx_target;
                }
                else
                {
                    ret = CTC_E_ACL_L4_PORT_RANGE_EXHAUSTED;
                    goto error;
                }

                p_sys_key->l4info_mapped      |= 1 << idx_valid;
                p_sys_key->l4info_mapped_mask |= 1 << idx_valid;

                if (0 == op_dest)
                {
                    p_sys_key->flag.l4_src_port_range = 1;
                    p_sys_key->l4_src_port_range_idx  = idx_valid;
                }
                else
                {
                    p_sys_key->flag.l4_dst_port_range = 1;
                    p_sys_key->l4_dst_port_range_idx  = idx_valid;
                }
            }
        }
        else    /* other layer 4 protocol type */
        {
            p_sys_key->flag.l4info_mapped  = 1;
            p_sys_key->l4info_mapped      |= p_ctc_key->l4_protocol;
            p_sys_key->l4info_mapped_mask |= p_ctc_key->l4_protocol_mask;

            p_sys_key->flag.is_tcp = 1;
            p_sys_key->is_tcp      = 0;

            p_sys_key->flag.is_udp = 1;
            p_sys_key->is_udp      = 0;
        }
    }

    /* ip fragment */
    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_IP_FRAG))
    {
        p_sys_key->flag.frag_info = 1;

        CTC_MAX_VALUE_CHECK(p_ctc_key->ip_frag, CTC_IP_FRAG_MAX - 1);

        switch (p_ctc_key->ip_frag)
        {
        case CTC_IP_FRAG_NON:
            /* Non fragmented */
            p_sys_key->frag_info      = 0;
            p_sys_key->frag_info_mask = 3;
            break;

        case CTC_IP_FRAG_FIRST:
            /* Fragmented, and first fragment */
            p_sys_key->frag_info      = 1;
            p_sys_key->frag_info_mask = 3;
            break;

        case CTC_IP_FRAG_NON_OR_FIRST:
            /* Non fragmented OR Fragmented and first fragment */
            p_sys_key->frag_info      = 0;
            p_sys_key->frag_info_mask = 2;     /* mask frag_info as 0x */
            break;

        case CTC_IP_FRAG_SMALL:
            /* Small fragment */
            p_sys_key->frag_info      = 2;
            p_sys_key->frag_info_mask = 3;
            break;

        case CTC_IP_FRAG_NOT_FIRST:
            /* Not first fragment (Fragmented of cause) */
            p_sys_key->frag_info      = 3;
            p_sys_key->frag_info_mask = 3;
            break;

        default:
            return CTC_E_INVALID_PARAM;
        }
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_IP_OPTION))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->ip_option, 1);
        p_sys_key->flag.ip_option = 1;
        p_sys_key->ip_option      = p_ctc_key->ip_option;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_ROUTED_PACKET))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->routed_packet, 1);
        p_sys_key->flag.routed_packet = 1;
        p_sys_key->routed_packet      = p_ctc_key->routed_packet;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_MAC_DA))
    {
        /* no check */
        p_sys_key->flag.mac_da = 1;
        sal_memcpy(p_sys_key->mac_da, p_ctc_key->mac_da, CTC_ETH_ADDR_LEN);
        sal_memcpy(p_sys_key->mac_da_mask, p_ctc_key->mac_da_mask, CTC_ETH_ADDR_LEN);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_MAC_SA))
    {
        /* no check */
        p_sys_key->flag.mac_sa = 1;
        sal_memcpy(p_sys_key->mac_sa, p_ctc_key->mac_sa, CTC_ETH_ADDR_LEN);
        sal_memcpy(p_sys_key->mac_sa_mask, p_ctc_key->mac_sa_mask, CTC_ETH_ADDR_LEN);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_CVLAN))
    {
        SYS_ACL_VLAN_ID_CHECK(p_ctc_key->cvlan);
        p_sys_key->flag.cvlan = 1;
        p_sys_key->cvlan      = p_ctc_key->cvlan;
        p_sys_key->cvlan_mask = p_ctc_key->cvlan_mask & 0xFFF;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_CTAG_COS))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->ctag_cos, 0x7);
        p_sys_key->flag.ctag_cos = 1;
        p_sys_key->ctag_cos      = p_ctc_key->ctag_cos;
        p_sys_key->ctag_cos_mask = (p_ctc_key->ctag_cos_mask) & 0x7;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_CTAG_CFI))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->ctag_cfi, 0x1);
        p_sys_key->flag.ctag_cfi = 1;
        p_sys_key->ctag_cfi      = p_ctc_key->ctag_cfi;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_SVLAN))
    {
        SYS_ACL_VLAN_ID_CHECK(p_ctc_key->svlan);
        p_sys_key->flag.svlan = 1;
        p_sys_key->svlan      = p_ctc_key->svlan;
        p_sys_key->svlan_mask = p_ctc_key->svlan_mask & 0xFFF;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_STAG_COS))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->stag_cos, 0x7);
        p_sys_key->flag.stag_cos = 1;
        p_sys_key->stag_cos      = p_ctc_key->stag_cos;
        p_sys_key->stag_cos_mask = (p_ctc_key->stag_cos_mask) & 0x7;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_STAG_CFI))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->stag_cfi, 0x1);
        p_sys_key->flag.stag_cfi = 1;
        p_sys_key->stag_cfi      = p_ctc_key->stag_cfi;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_L2_TYPE))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->l2_type, 0xF);
        p_sys_key->flag.l2_type = 1;
        p_sys_key->l2_type      = p_ctc_key->l2_type;
        p_sys_key->l2_type_mask = (p_ctc_key->l2_type_mask) & 0xF;
    }
    else if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_DSCP))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->dscp, 0x3F);
        p_sys_key->flag.dscp = 1;
        p_sys_key->dscp      = p_ctc_key->dscp;
        p_sys_key->dscp_mask = (p_ctc_key->dscp_mask) & 0x3F;
    }
    else if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_PRECEDENCE))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->dscp, 7);

        p_sys_key->flag.dscp = 1;
        p_sys_key->dscp      = (p_ctc_key->dscp) << 3;
        p_sys_key->dscp_mask = (p_ctc_key->dscp_mask & 0x7) << 3;
#if 0
        p_sys_key->dscp_mask = 0x38; /* mask = 111000, that is 0x38 */
#endif /* _if0_ */
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_STAG_VALID))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->stag_valid, 1);
        p_sys_key->flag.svlan_id_valid = 1;
        p_sys_key->svlan_id_valid      = p_ctc_key->stag_valid;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_CTAG_VALID))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->ctag_valid, 1);
        p_sys_key->flag.cvlan_id_valid = 1;
        p_sys_key->cvlan_id_valid      = p_ctc_key->ctag_valid;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_IPV4_PACKET))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->ipv4_packet, 1);
        p_sys_key->flag.ipv4_packet = 1;
        p_sys_key->ipv4_packet      = p_ctc_key->ipv4_packet;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV4_KEY_FLAG_ARP_PACKET))
    {
        if (p_sys_key->flag.arp_packet)
        {
            return CTC_E_INVALID_PARAM;
        }

        p_sys_key->flag.arp_packet = 1;
        p_sys_key->arp_packet      = p_ctc_key->arp_packet;

        if (CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV4_KEY_SUB_FLAG_ARP_OP_CODE))
        {
            if (p_sys_key->flag.l4_src_port)
            {
                return CTC_E_ACL_FLAG_COLLISION;
            }

            p_sys_key->flag.l4_src_port = 1;
            p_sys_key->l4_src_port      = p_ctc_key->arp_op_code;
            p_sys_key->l4_src_port_mask = p_ctc_key->arp_op_code_mask;
        }

        if (CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV4_KEY_SUB_FLAG_ARP_SENDER_IP))
        {
            if (p_sys_key->flag.ip_sa)
            {
                return CTC_E_ACL_FLAG_COLLISION;
            }

            p_sys_key->flag.ip_sa = 1;
            p_sys_key->ip_sa      = p_ctc_key->sender_ip;
            p_sys_key->ip_sa_mask = p_ctc_key->sender_ip_mask;
        }

        if (CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV4_KEY_SUB_FLAG_ARP_TARGET_IP))
        {
            if (p_sys_key->flag.ip_da)
            {
                return CTC_E_ACL_FLAG_COLLISION;
            }

            p_sys_key->flag.ip_da = 1;
            p_sys_key->ip_da      = p_ctc_key->target_ip;
            p_sys_key->ip_da_mask = p_ctc_key->target_ip_mask;
        }
    }

    /* tcp flag */
    if (p_sys_key->flag.is_tcp
        && p_sys_key->is_tcp
        && CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV4_KEY_SUB_FLAG_TCP_FLAGS))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->tcp_flags_match_any, 1);
        CTC_MAX_VALUE_CHECK(p_ctc_key->tcp_flags, 0x3F);

        CTC_ERROR_RETURN(_sys_greatbelt_acl_lkup_tcp_flag(lchip, p_ctc_key->tcp_flags_match_any,
                                                          p_ctc_key->tcp_flags, &idx_target, &idx_lkup));

        if (SYS_ACL_TCP_FLAG_NUM != idx_lkup)
        {   /* lkup success */
            acl_master[lchip]->tcp_flag[idx_lkup].ref++;

            idx_valid = idx_lkup;
        }
        else if ((SYS_ACL_TCP_FLAG_NUM == idx_lkup) && (acl_master[lchip]->tcp_flag_free_cnt > 0))
        {
            _sys_greatbelt_acl_add_tcp_flag(lchip, p_ctc_key->tcp_flags_match_any,
                                            p_ctc_key->tcp_flags, idx_target);
            idx_valid = idx_target;
        }
        else
        {
            ret = CTC_E_ACL_TCP_FLAG_EXHAUSTED;
            goto error;
        }

        p_sys_key->flag.l4info_mapped  = 1;
        p_sys_key->l4info_mapped      |= 1 << (idx_valid + 8);
        p_sys_key->l4info_mapped_mask |= 1 << (idx_valid + 8);

        p_sys_key->flag.tcp_flags = 1;
        p_sys_key->tcp_flags_idx  = idx_valid;
    }

    return CTC_E_NONE;


 error:
    return ret;
}

STATIC int32
_sys_greatbelt_acl_map_mpls_key(uint8 lchip, ctc_acl_mpls_key_t* p_ctc_key,
                                sys_acl_mpls_key_t* p_sys_key)
{
    uint32 flag = 0;

    CTC_PTR_VALID_CHECK(p_ctc_key);
    CTC_PTR_VALID_CHECK(p_sys_key);

    SYS_ACL_DBG_FUNC();

    flag = p_ctc_key->flag;

    if (CTC_FLAG_ISSET(flag, CTC_ACL_MPLS_KEY_FLAG_MAC_DA))
    {
        p_sys_key->flag.mac_da = 1;
        sal_memcpy(p_sys_key->mac_da, p_ctc_key->mac_da, CTC_ETH_ADDR_LEN);
        sal_memcpy(p_sys_key->mac_da_mask, p_ctc_key->mac_da_mask, CTC_ETH_ADDR_LEN);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_MPLS_KEY_FLAG_MAC_SA))
    {
        p_sys_key->flag.mac_sa = 1;
        sal_memcpy(p_sys_key->mac_sa, p_ctc_key->mac_sa, CTC_ETH_ADDR_LEN);
        sal_memcpy(p_sys_key->mac_sa_mask, p_ctc_key->mac_sa_mask, CTC_ETH_ADDR_LEN);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_MPLS_KEY_FLAG_CVLAN))
    {
        SYS_ACL_VLAN_ID_CHECK(p_ctc_key->cvlan);
        p_sys_key->flag.cvlan = 1;
        p_sys_key->cvlan      = p_ctc_key->cvlan;
        p_sys_key->cvlan_mask = p_ctc_key->cvlan_mask & 0xFFF;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_MPLS_KEY_FLAG_CTAG_COS))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->ctag_cos, 0x7);
        p_sys_key->flag.ctag_cos = 1;
        p_sys_key->ctag_cos      = p_ctc_key->ctag_cos;
        p_sys_key->ctag_cos_mask = (p_ctc_key->ctag_cos_mask) & 0x7;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_MPLS_KEY_FLAG_CTAG_CFI))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->ctag_cfi, 0x1);
        p_sys_key->flag.ctag_cfi = 1;
        p_sys_key->ctag_cfi      = p_ctc_key->ctag_cfi;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_MPLS_KEY_FLAG_SVLAN))
    {
        SYS_ACL_VLAN_ID_CHECK(p_ctc_key->svlan);
        p_sys_key->flag.svlan = 1;
        p_sys_key->svlan      = p_ctc_key->svlan;
        p_sys_key->svlan_mask = p_ctc_key->svlan_mask & 0xFFF;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_MPLS_KEY_FLAG_STAG_COS))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->stag_cos, 0x7);
        p_sys_key->flag.stag_cos = 1;
        p_sys_key->stag_cos      = p_ctc_key->stag_cos;
        p_sys_key->stag_cos_mask = (p_ctc_key->stag_cos_mask) & 0x7;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_MPLS_KEY_FLAG_STAG_CFI))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->stag_cfi, 0x1);
        p_sys_key->flag.stag_cfi = 1;
        p_sys_key->stag_cfi      = p_ctc_key->stag_cfi;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_MPLS_KEY_FLAG_ROUTED_PACKET))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->routed_packet, 1);
        p_sys_key->flag.routed_packet = 1;
        p_sys_key->routed_packet      = p_ctc_key->routed_packet;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_MPLS_KEY_FLAG_MPLS_LABEL0))
    {
        /* no check */
        p_sys_key->flag.mpls_label0 = 1;
        p_sys_key->mpls_label0      = p_ctc_key->mpls_label0;
        p_sys_key->mpls_label0_mask = p_ctc_key->mpls_label0_mask;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_MPLS_KEY_FLAG_MPLS_LABEL1))
    {
        /* no check */
        p_sys_key->flag.mpls_label1 = 1;
        p_sys_key->mpls_label1      = p_ctc_key->mpls_label1;
        p_sys_key->mpls_label1_mask = p_ctc_key->mpls_label1_mask;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_MPLS_KEY_FLAG_MPLS_LABEL2))
    {
        /* no check */
        p_sys_key->flag.mpls_label2 = 1;
        p_sys_key->mpls_label2      = p_ctc_key->mpls_label2;
        p_sys_key->mpls_label2_mask = p_ctc_key->mpls_label2_mask;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_MPLS_KEY_FLAG_MPLS_LABEL3))
    {
        /* no check */
        p_sys_key->flag.mpls_label3 = 1;
        p_sys_key->mpls_label3      = p_ctc_key->mpls_label3;
        p_sys_key->mpls_label3_mask = p_ctc_key->mpls_label3_mask;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_acl_map_ipv6_key(uint8 lchip, ctc_acl_ipv6_key_t* p_ctc_key,
                                sys_acl_ipv6_key_t* p_sys_key)
{
    uint32 flag       = 0;
    uint32 sub_flag   = 0;
    int32  ret        = 0;
    uint8  idx_lkup   = 0;
    uint8  idx_target = 0;
    uint8  idx_valid  = 0;

    uint16 port0                 = 0;
    uint16 port1                 = 0;
    uint8  op_dest               = 0; /* loop 2 times, for saving code.*/
    uint8  l4_src_port_use_range = 0; /* src port use range */
    uint8  l4_dst_port_use_range = 0; /* dst port use range */

    CTC_PTR_VALID_CHECK(p_ctc_key);
    CTC_PTR_VALID_CHECK(p_sys_key);

    SYS_ACL_DBG_FUNC();

    flag     = p_ctc_key->flag;
    sub_flag = p_ctc_key->sub_flag;

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_DSCP)
        && CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_PRECEDENCE))
    {
        return CTC_E_ACL_FLAG_COLLISION;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_IP_DA))
    {
        p_sys_key->flag.ip_da = 1;
        sal_memcpy(p_sys_key->ip_da, p_ctc_key->ip_da, sizeof(ipv6_addr_t));
        sal_memcpy(p_sys_key->ip_da_mask, p_ctc_key->ip_da_mask, sizeof(ipv6_addr_t));
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_IP_SA))
    {
        p_sys_key->flag.ip_sa = 1;
        sal_memcpy(p_sys_key->ip_sa, p_ctc_key->ip_sa, sizeof(ipv6_addr_t));
        sal_memcpy(p_sys_key->ip_sa_mask, p_ctc_key->ip_sa_mask, sizeof(ipv6_addr_t));
    }

    /* layer 4 information */
    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_L4_PROTOCOL))
    {
        if ((0xFF == p_ctc_key->l4_protocol_mask)
            && (IPV6_ICMP_PROTOCOL == p_ctc_key->l4_protocol))        /* ICMP */
        {
            p_sys_key->flag.l4info_mapped = 1;
            p_sys_key->l4info_mapped      = p_ctc_key->l4_protocol;
            p_sys_key->l4info_mapped_mask = p_ctc_key->l4_protocol_mask;

            p_sys_key->flag.is_tcp = 1;
            p_sys_key->is_tcp      = 0;

            p_sys_key->flag.is_udp = 1;
            p_sys_key->is_udp      = 0;

            /* icmp type */
            if (CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV6_KEY_SUB_FLAG_ICMP_TYPE))
            {
                p_sys_key->flag.l4_src_port  = 1;
                p_sys_key->l4_src_port       = (p_ctc_key->icmp_type << 8) & 0xFF00;
                p_sys_key->l4_src_port_mask |= (p_ctc_key->icmp_type_mask << 8) & 0xFF00;
            }

            /* icmp code */
            if (CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV6_KEY_SUB_FLAG_ICMP_CODE))
            {
                p_sys_key->flag.l4_src_port  = 1;
                p_sys_key->l4_src_port      |= p_ctc_key->icmp_code & 0x00FF;
                p_sys_key->l4_src_port_mask |= p_ctc_key->icmp_code_mask & 0x00FF;
            }
        }
        else if ((0xFF == p_ctc_key->l4_protocol_mask)
                 && ((TCP_PROTOCOL == p_ctc_key->l4_protocol)
                     || (UDP_PROTOCOL == p_ctc_key->l4_protocol))) /* TCP or UDP */
        {
            if (TCP_PROTOCOL == p_ctc_key->l4_protocol)
            {   /* TCP */
                p_sys_key->flag.is_tcp = 1;
                p_sys_key->is_tcp      = 1;
            }
            else
            {   /* UDP */
                p_sys_key->flag.is_udp = 1;
                p_sys_key->is_udp      = 1;
            }

            if (CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV6_KEY_SUB_FLAG_L4_SRC_PORT))
            {
                if (p_ctc_key->l4_src_port_use_mask)
                {   /*  use mask.
                     *  l4_src_port_0 is data.
                     *  l4_src_port_1 is mask.
                     */
                    p_sys_key->flag.l4_src_port = 1;
                    p_sys_key->l4_src_port      = p_ctc_key->l4_src_port_0;
                    p_sys_key->l4_src_port_mask = p_ctc_key->l4_src_port_1;
                }
                else
                {   /*  use range.
                     *  l4_src_port_0 is min.
                     *  l4_src_port_1 is max.
                     */
                    if (p_ctc_key->l4_src_port_0 >= p_ctc_key->l4_src_port_1)
                    {
                        return CTC_E_INVALID_PARAM;
                    }

                    l4_src_port_use_range = 1;
                }
            }

            if (CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV6_KEY_SUB_FLAG_L4_DST_PORT))
            {
                if (p_ctc_key->l4_dst_port_use_mask)
                {   /*  use mask.
                     *  l4_dst_port_0 is data.
                     *  l4_dst_port_1 is mask.
                     */
                    p_sys_key->flag.l4_dst_port = 1;
                    p_sys_key->l4_dst_port      = p_ctc_key->l4_dst_port_0;
                    p_sys_key->l4_dst_port_mask = p_ctc_key->l4_dst_port_1;
                }
                else
                {   /*  use range.
                     *  l4_dst_port_0 is min.
                     *  l4_dst_port_1 is max.
                     */
                    if (p_ctc_key->l4_dst_port_0 >= p_ctc_key->l4_dst_port_1)
                    {
                        return CTC_E_INVALID_PARAM;
                    }

                    l4_dst_port_use_range = 1;
                }
            }

            /* op_dest, use loop to save code.
             * op_dest = 0 -->src_port
             * op_dest = 1 -->dst_port
             */
            for (op_dest = 0; op_dest <= 1; op_dest++)
            {
                if (0 == op_dest)
                {
                    if (!l4_src_port_use_range)
                    {
                        continue;
                    }

                    port0 = p_ctc_key->l4_src_port_0;
                    port1 = p_ctc_key->l4_src_port_1;
                }
                else
                {
                    if (!l4_dst_port_use_range)
                    {
                        continue;
                    }

                    port0 = p_ctc_key->l4_dst_port_0;
                    port1 = p_ctc_key->l4_dst_port_1;
                }

                p_sys_key->flag.l4info_mapped = 1;

                CTC_ERROR_RETURN(_sys_greatbelt_acl_lkup_l4_port_range(lchip, port0,
                                                                       port1, op_dest, &idx_target, &idx_lkup));

                if (SYS_ACL_L4_PORT_NUM != idx_lkup)
                {   /* lkup success */
                    acl_master[lchip]->l4_port[idx_lkup].ref++;

                    idx_valid = idx_lkup;
                }
                else if ((SYS_ACL_L4_PORT_NUM == idx_lkup) && (acl_master[lchip]->l4_port_free_cnt > 0))
                {
                    CTC_ERROR_RETURN(_sys_greatbelt_acl_add_l4_port_range
                                         (lchip, port0, port1, op_dest, idx_target));

                    idx_valid = idx_target;
                }
                else
                {
                    ret = CTC_E_ACL_L4_PORT_RANGE_EXHAUSTED;
                    goto error;
                }

                p_sys_key->l4info_mapped      |= 1 << idx_valid;
                p_sys_key->l4info_mapped_mask |= 1 << idx_valid;

                if (0 == op_dest)
                {
                    p_sys_key->flag.l4_src_port_range = 1;
                    p_sys_key->l4_src_port_range_idx  = idx_valid;
                }
                else
                {
                    p_sys_key->flag.l4_dst_port_range = 1;
                    p_sys_key->l4_dst_port_range_idx  = idx_valid;
                }
            }
        }
        else    /* other layer 4 protocol type */
        {
            p_sys_key->flag.l4info_mapped = 1;
            p_sys_key->l4info_mapped      = p_ctc_key->l4_protocol;
            p_sys_key->l4info_mapped_mask = p_ctc_key->l4_protocol_mask;

            p_sys_key->flag.is_tcp = 1;
            p_sys_key->is_tcp      = 0;

            p_sys_key->flag.is_udp = 1;
            p_sys_key->is_udp      = 0;
        }
    }

    /* ip fragment */
    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_IP_FRAG))
    {
        p_sys_key->flag.frag_info = 1;

        CTC_MAX_VALUE_CHECK(p_ctc_key->ip_frag, CTC_IP_FRAG_MAX - 1);

        switch (p_ctc_key->ip_frag)
        {
        case CTC_IP_FRAG_NON:
            /* Non fragmented */
            p_sys_key->frag_info      = 0;
            p_sys_key->frag_info_mask = 3;
            break;

        case CTC_IP_FRAG_FIRST:
            /* Fragmented, and first fragment */
            p_sys_key->frag_info      = 1;
            p_sys_key->frag_info_mask = 3;
            break;

        case CTC_IP_FRAG_NON_OR_FIRST:
            /* Non fragmented OR Fragmented and first fragment */
            p_sys_key->frag_info      = 0;
            p_sys_key->frag_info_mask = 2;     /* mask frag_info as 0x */
            break;

        case CTC_IP_FRAG_SMALL:
            /* Small fragment */
            p_sys_key->frag_info      = 2;
            p_sys_key->frag_info_mask = 3;
            break;

        case CTC_IP_FRAG_NOT_FIRST:
            /* Not first fragment (Fragmented of cause) */
            p_sys_key->frag_info      = 3;
            p_sys_key->frag_info_mask = 3;
            break;

        default:
            return CTC_E_INVALID_PARAM;
        }
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_DSCP))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->dscp, 0x3F);
        p_sys_key->flag.dscp = 1;
        p_sys_key->dscp      = p_ctc_key->dscp;
        p_sys_key->dscp_mask = p_ctc_key->dscp_mask;
    }
    else if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_PRECEDENCE))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->dscp, 7);

        p_sys_key->flag.dscp = 1;
        p_sys_key->dscp      = (p_ctc_key->dscp) << 3;
        p_sys_key->dscp_mask = (p_ctc_key->dscp_mask & 0x7) << 3;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_IP_OPTION))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->ip_option, 1);
        p_sys_key->flag.ip_option = 1;
        p_sys_key->ip_option      = p_ctc_key->ip_option;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_ROUTED_PACKET))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->routed_packet, 1);
        p_sys_key->flag.routed_packet = 1;
        p_sys_key->routed_packet      = p_ctc_key->routed_packet;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_FLOW_LABEL))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->flow_label, 0xFFFFF);
        p_sys_key->flag.flow_label = 1;
        p_sys_key->flow_label      = p_ctc_key->flow_label;
        p_sys_key->flow_label_mask = p_ctc_key->flow_label_mask & 0xFFFFF;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_MAC_DA))
    {
        p_sys_key->flag.mac_da = 1;
        sal_memcpy(p_sys_key->mac_da, p_ctc_key->mac_da, CTC_ETH_ADDR_LEN);
        sal_memcpy(p_sys_key->mac_da_mask, p_ctc_key->mac_da_mask, CTC_ETH_ADDR_LEN);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_MAC_SA))
    {
        p_sys_key->flag.mac_sa = 1;
        sal_memcpy(p_sys_key->mac_sa, p_ctc_key->mac_sa, CTC_ETH_ADDR_LEN);
        sal_memcpy(p_sys_key->mac_sa_mask, p_ctc_key->mac_sa_mask, CTC_ETH_ADDR_LEN);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_CVLAN))
    {
        SYS_ACL_VLAN_ID_CHECK(p_ctc_key->cvlan);
        p_sys_key->flag.cvlan = 1;
        p_sys_key->cvlan      = p_ctc_key->cvlan;
        p_sys_key->cvlan_mask = p_ctc_key->cvlan_mask & 0xFFF;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_CTAG_COS))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->ctag_cos, 0x7);
        p_sys_key->flag.ctag_cos = 1;
        p_sys_key->ctag_cos      = p_ctc_key->ctag_cos;
        p_sys_key->ctag_cos_mask = (p_ctc_key->ctag_cos_mask) & 0x7;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_CTAG_CFI))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->ctag_cfi, 0x1);
        p_sys_key->flag.ctag_cfi = 1;
        p_sys_key->ctag_cfi      = p_ctc_key->ctag_cfi;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_SVLAN))
    {
        SYS_ACL_VLAN_ID_CHECK(p_ctc_key->svlan);
        p_sys_key->flag.svlan = 1;
        p_sys_key->svlan      = p_ctc_key->svlan;
        p_sys_key->svlan_mask = p_ctc_key->svlan_mask & 0xFFF;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_STAG_COS))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->stag_cos, 0x7);
        p_sys_key->flag.stag_cos = 1;
        p_sys_key->stag_cos      = p_ctc_key->stag_cos;
        p_sys_key->stag_cos_mask = (p_ctc_key->stag_cos_mask) & 0x7;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_STAG_CFI))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->stag_cfi, 0x1);
        p_sys_key->flag.stag_cfi = 1;
        p_sys_key->stag_cfi      = p_ctc_key->stag_cfi;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_ETH_TYPE))
    {
        p_sys_key->flag.eth_type = 1;
        p_sys_key->eth_type      = p_ctc_key->eth_type;
        p_sys_key->eth_type_mask = p_ctc_key->eth_type_mask;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_L2_TYPE))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->l2_type, 0xF);
        p_sys_key->flag.l2_type = 1;
        p_sys_key->l2_type      = p_ctc_key->l2_type;
        p_sys_key->l2_type_mask = p_ctc_key->l2_type_mask;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_L3_TYPE))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->l3_type, 0xF);
        p_sys_key->flag.l3_type = 1;
        p_sys_key->l3_type      = p_ctc_key->l3_type;
        p_sys_key->l3_type_mask = p_ctc_key->l3_type_mask;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_STAG_VALID))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->stag_valid, 1);
        p_sys_key->flag.svlan_id_valid = 1;
        p_sys_key->svlan_id_valid      = p_ctc_key->stag_valid;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_IPV6_KEY_FLAG_CTAG_VALID))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->ctag_valid, 1);
        p_sys_key->flag.cvlan_id_valid = 1;
        p_sys_key->cvlan_id_valid      = p_ctc_key->ctag_valid;
    }

    if (p_sys_key->flag.is_tcp
        && p_sys_key->is_tcp
        && CTC_FLAG_ISSET(sub_flag, CTC_ACL_IPV6_KEY_SUB_FLAG_TCP_FLAGS))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->tcp_flags_match_any, 1);
        CTC_MAX_VALUE_CHECK(p_ctc_key->tcp_flags, 0x3F);

        CTC_ERROR_RETURN(_sys_greatbelt_acl_lkup_tcp_flag(lchip, p_ctc_key->tcp_flags_match_any,
                                                          p_ctc_key->tcp_flags, &idx_target, &idx_lkup));

        if (SYS_ACL_TCP_FLAG_NUM != idx_lkup)
        {   /* lkup success */
            acl_master[lchip]->tcp_flag[idx_lkup].ref++;

            idx_valid = idx_lkup;
        }
        else if ((SYS_ACL_TCP_FLAG_NUM == idx_lkup) && (acl_master[lchip]->tcp_flag_free_cnt > 0))
        {
            _sys_greatbelt_acl_add_tcp_flag(lchip, p_ctc_key->tcp_flags_match_any,
                                            p_ctc_key->tcp_flags, idx_target);
            idx_valid = idx_target;
        }
        else
        {
            ret = CTC_E_ACL_TCP_FLAG_EXHAUSTED;
            goto error;
        }

        p_sys_key->flag.l4info_mapped  = 1;
        p_sys_key->l4info_mapped      |= 1 << (idx_valid + 8);
        p_sys_key->l4info_mapped_mask |= 1 << (idx_valid + 8);

        p_sys_key->flag.tcp_flags = 1;
        p_sys_key->tcp_flags_idx  = idx_valid;
    }

    return CTC_E_NONE;

 error:
    return ret;
}


STATIC int32
_sys_greatbelt_acl_map_hash_mac_key(uint8 lchip, ctc_acl_hash_mac_key_t* p_ctc_key,
                                    sys_acl_hash_mac_key_t* p_sys_key)
{
    CTC_PTR_VALID_CHECK(p_ctc_key);
    CTC_PTR_VALID_CHECK(p_sys_key);

    SYS_ACL_DBG_FUNC();

    /* mac da */
    if (CTC_FLAG_ISSET(acl_master[lchip]->acl_register.hash_mac_key_flag,
                       CTC_ACL_HASH_MAC_KEY_FLAG_MAC_DA))
    {
        sal_memcpy(p_sys_key->mac_da, p_ctc_key->mac_da, CTC_ETH_ADDR_LEN);
    }

    /* eth type */
    if (CTC_FLAG_ISSET(acl_master[lchip]->acl_register.hash_mac_key_flag,
                       CTC_ACL_HASH_MAC_KEY_FLAG_ETH_TYPE))
    {
        p_sys_key->eth_type = p_ctc_key->eth_type;
    }

    /* is logic port */
    CTC_MAX_VALUE_CHECK(p_ctc_key->gport_is_logic_port, 1);
    p_sys_key->is_logic_port = p_ctc_key->gport_is_logic_port;

    /* gport */
    if (p_sys_key->is_logic_port) /* logic port */
    {
        if (CTC_FLAG_ISSET(acl_master[lchip]->acl_register.hash_mac_key_flag,
                           CTC_ACL_HASH_MAC_KEY_FLAG_LOGIC_PORT))
        {
            SYS_LOGIC_PORT_CHECK(p_ctc_key->gport);
            p_sys_key->gport = p_ctc_key->gport;
        }
    }
    else    /* phy port */
    {
        if (CTC_FLAG_ISSET(acl_master[lchip]->acl_register.hash_mac_key_flag,
                           CTC_ACL_HASH_MAC_KEY_FLAG_PHY_PORT))
        {
            CTC_GLOBAL_PORT_CHECK(p_ctc_key->gport);
            p_sys_key->gport = p_ctc_key->gport;
        }
    }

    /* cos */
    if (CTC_FLAG_ISSET(acl_master[lchip]->acl_register.hash_mac_key_flag,
                       CTC_ACL_HASH_MAC_KEY_FLAG_COS))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->cos, 7);
        p_sys_key->cos = p_ctc_key->cos;
    }

    /* ipv4 packet  */
    p_sys_key->ipv4_packet = 0;

    if (CTC_FLAG_ISSET(acl_master[lchip]->acl_register.hash_mac_key_flag,
                       CTC_ACL_HASH_MAC_KEY_FLAG_VLAN_ID))
    {
        /* vlan id  */
        SYS_ACL_VLAN_ID_CHECK(p_ctc_key->vlan_id);
        p_sys_key->vlan_id = p_ctc_key->vlan_id;
    }

    return CTC_E_NONE;
}


STATIC int32
_sys_greatbelt_acl_map_hash_ipv4_key(uint8 lchip, ctc_acl_hash_ipv4_key_t* p_ctc_key,
                                     sys_acl_hash_ipv4_key_t* p_sys_key)
{
    CTC_PTR_VALID_CHECK(p_ctc_key);
    CTC_PTR_VALID_CHECK(p_sys_key);

    SYS_ACL_DBG_FUNC();


    /* ip_sa */
    if (CTC_FLAG_ISSET(acl_master[lchip]->acl_register.hash_ipv4_key_flag,
                       CTC_ACL_HASH_IPV4_KEY_FLAG_IP_DA))
    {
        p_sys_key->ip_da = p_ctc_key->ip_da;
    }

    /* ip_da */
    if (CTC_FLAG_ISSET(acl_master[lchip]->acl_register.hash_ipv4_key_flag,
                       CTC_ACL_HASH_IPV4_KEY_FLAG_IP_SA))
    {
        p_sys_key->ip_sa = p_ctc_key->ip_sa;
    }

    /* l4_src_port */
    if (CTC_FLAG_ISSET(acl_master[lchip]->acl_register.hash_ipv4_key_flag,
                       CTC_ACL_HASH_IPV4_KEY_FLAG_L4_SRC_PORT))
    {
        p_sys_key->l4_src_port = p_ctc_key->l4_src_port;
    }

    /* l4_dst_port */
    if (CTC_FLAG_ISSET(acl_master[lchip]->acl_register.hash_ipv4_key_flag,
                       CTC_ACL_HASH_IPV4_KEY_FLAG_L4_DST_PORT))
    {
        p_sys_key->l4_dst_port = p_ctc_key->l4_dst_port;
    }

    /* is_logic_port  */
    CTC_MAX_VALUE_CHECK(p_ctc_key->gport_is_logic_port, 1);
    p_sys_key->is_logic_port = p_ctc_key->gport_is_logic_port;

    /* gport */
    if (p_ctc_key->gport_is_logic_port) /* logic port */
    {
        if (CTC_FLAG_ISSET(acl_master[lchip]->acl_register.hash_ipv4_key_flag,
                           CTC_ACL_HASH_IPV4_KEY_FLAG_LOGIC_PORT))
        {
            SYS_LOGIC_PORT_CHECK(p_ctc_key->gport);
            p_sys_key->gport = p_ctc_key->gport;
        }
    }
    else     /* phy port */
    {
        if (CTC_FLAG_ISSET(acl_master[lchip]->acl_register.hash_ipv4_key_flag,
                           CTC_ACL_HASH_IPV4_KEY_FLAG_PHY_PORT))
        {
            CTC_GLOBAL_PORT_CHECK(p_ctc_key->gport);
            p_sys_key->gport = p_ctc_key->gport;
        }
    }

    /* dscp */
    if (CTC_FLAG_ISSET(acl_master[lchip]->acl_register.hash_ipv4_key_flag,
                       CTC_ACL_HASH_IPV4_KEY_FLAG_DSCP))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->dscp, 0x3F);
        p_sys_key->dscp = p_ctc_key->dscp;
    }

    /* l4_protocol */
    if (CTC_FLAG_ISSET(acl_master[lchip]->acl_register.hash_ipv4_key_flag,
                       CTC_ACL_HASH_IPV4_KEY_FLAG_L4_PROTOCOL))
    {
        p_sys_key->l4_protocol = p_ctc_key->l4_protocol;
    }

    /* ipv4_packet */
    p_sys_key->ipv4_packet = 1;

    /* arp_packet */
    if (CTC_FLAG_ISSET(acl_master[lchip]->acl_register.hash_ipv4_key_flag,
                       CTC_ACL_HASH_IPV4_KEY_FLAG_ARP_PACKET))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->arp_packet, 1);
        p_sys_key->arp_packet = p_ctc_key->arp_packet;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_acl_map_pbr_ipv4_key(uint8 lchip, ctc_acl_pbr_ipv4_key_t* p_ctc_key,
                                    sys_acl_pbr_ipv4_key_t* p_sys_key)
{
    int32  ret        = 0;
    uint32 flag       = 0;
    uint32 sub_flag   = 0;
    uint8  idx_lkup   = 0;
    uint8  idx_valid  = 0;
    uint8  idx_target = 0;

    uint16 port0 = 0;
    uint16 port1 = 0;

    uint8  op_dest               = 0; /* loop 2 times, for saving code.*/
    uint8  l4_src_port_use_range = 0; /* src port use range */
    uint8  l4_dst_port_use_range = 0; /* dst port use range */

    CTC_PTR_VALID_CHECK(p_ctc_key);
    CTC_PTR_VALID_CHECK(p_sys_key);

    SYS_ACL_DBG_FUNC();

    flag     = p_ctc_key->flag;
    sub_flag = p_ctc_key->sub_flag;

    if (CTC_FLAG_ISSET(flag, CTC_ACL_PBR_IPV4_KEY_FLAG_IP_DA))
    {
        p_sys_key->flag.ip_da = 1;
        p_sys_key->ip_da      = p_ctc_key->ip_da;
        p_sys_key->ip_da_mask = p_ctc_key->ip_da_mask;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_PBR_IPV4_KEY_FLAG_IP_SA))
    {
        p_sys_key->flag.ip_sa = 1;
        p_sys_key->ip_sa      = p_ctc_key->ip_sa;
        p_sys_key->ip_sa_mask = p_ctc_key->ip_sa_mask;
    }

    /* layer 4 information */
    if (CTC_FLAG_ISSET(flag, CTC_ACL_PBR_IPV4_KEY_FLAG_L4_PROTOCOL))
    {
        if ((0xFF == p_ctc_key->l4_protocol_mask) &&
            (IPV4_ICMP_PROTOCOL == p_ctc_key->l4_protocol))        /* ICMP */
        {
            p_sys_key->flag.l4info_mapped = 1;
            /* l4_info_mapped for non-tcp non-udp packet, it's l4_type | l3_header_protocol */
            p_sys_key->l4info_mapped      = p_ctc_key->l4_protocol;
            p_sys_key->l4info_mapped_mask = p_ctc_key->l4_protocol_mask;

            p_sys_key->flag.is_tcp = 1;
            p_sys_key->is_tcp      = 0;

            p_sys_key->flag.is_udp = 1;
            p_sys_key->is_udp      = 0;

            /* l4_src_port for flex-l4 (including icmp), it's type|code */

            /* icmp type */
            if (CTC_FLAG_ISSET(sub_flag, CTC_ACL_PBR_IPV4_KEY_SUB_FLAG_ICMP_TYPE))
            {
                p_sys_key->flag.l4_src_port  = 1;
                p_sys_key->l4_src_port      |= (p_ctc_key->icmp_type) << 8;
                p_sys_key->l4_src_port_mask |= (p_ctc_key->icmp_type_mask) << 8;
            }

            /* icmp code */
            if (CTC_FLAG_ISSET(sub_flag, CTC_ACL_PBR_IPV4_KEY_SUB_FLAG_ICMP_CODE))
            {
                p_sys_key->flag.l4_src_port  = 1;
                p_sys_key->l4_src_port      |= p_ctc_key->icmp_code & 0x00FF;
                p_sys_key->l4_src_port_mask |= (p_ctc_key->icmp_code_mask) & 0x00FF;
            }
        }
        else if ((0xFF == p_ctc_key->l4_protocol_mask)
                 && ((TCP_PROTOCOL == p_ctc_key->l4_protocol)
                     || (UDP_PROTOCOL == p_ctc_key->l4_protocol)))   /* TCP or UDP */
        {
            if (TCP_PROTOCOL == p_ctc_key->l4_protocol)
            {   /* TCP */
                p_sys_key->flag.is_tcp = 1;
                p_sys_key->is_tcp      = 1;
            }
            else
            {   /* UDP */
                p_sys_key->flag.is_udp = 1;
                p_sys_key->is_udp      = 1;
            }

            if (CTC_FLAG_ISSET(sub_flag, CTC_ACL_PBR_IPV4_KEY_SUB_FLAG_L4_SRC_PORT))
            {     /* op_dest_port = 0 */
                if (p_ctc_key->l4_src_port_use_mask)
                { /*  use mask.
                   *  l4_src_port_0 is data.
                   *  l4_src_port_1 is mask.
                   */
                    p_sys_key->flag.l4_src_port = 1;
                    p_sys_key->l4_src_port      = p_ctc_key->l4_src_port_0;
                    p_sys_key->l4_src_port_mask = p_ctc_key->l4_src_port_1;
                }
                else
                {   /*  use range.
                     *  l4_src_port_0 is min.
                     *  l4_src_port_1 is max.
                     */
                    if (p_ctc_key->l4_src_port_0 >= p_ctc_key->l4_src_port_1)
                    {
                        return CTC_E_INVALID_PARAM;
                    }

                    l4_src_port_use_range = 1;
                }
            }

            if (CTC_FLAG_ISSET(sub_flag, CTC_ACL_PBR_IPV4_KEY_SUB_FLAG_L4_DST_PORT))
            {     /* op_dest_port =1 */
                if (p_ctc_key->l4_dst_port_use_mask)
                { /*  use mask.
                   *  l4_dst_port_0 is data.
                   *  l4_dst_port_1 is mask.
                   */
                    p_sys_key->flag.l4_dst_port = 1;
                    p_sys_key->l4_dst_port      = p_ctc_key->l4_dst_port_0;
                    p_sys_key->l4_dst_port_mask = p_ctc_key->l4_dst_port_1;
                }
                else
                {   /*  use range.
                     *  l4_dst_port_0 is min.
                     *  l4_dst_port_1 is max.
                     */
                    if (p_ctc_key->l4_dst_port_0 >= p_ctc_key->l4_dst_port_1)
                    {
                        return CTC_E_INVALID_PARAM;
                    }

                    l4_dst_port_use_range = 1;
                }
            }

            /* op_dest, use loop to save code.
             * op_dest = 0 -->src_port
             * op_dest = 1 -->dst_port
             */
            for (op_dest = 0; op_dest <= 1; op_dest++)
            {
                if (0 == op_dest)
                {
                    if (!l4_src_port_use_range)
                    {
                        continue;
                    }

                    port0 = p_ctc_key->l4_src_port_0;
                    port1 = p_ctc_key->l4_src_port_1;
                }
                else
                {
                    if (!l4_dst_port_use_range)
                    {
                        continue;
                    }

                    port0 = p_ctc_key->l4_dst_port_0;
                    port1 = p_ctc_key->l4_dst_port_1;
                }

                p_sys_key->flag.l4info_mapped = 1;

                CTC_ERROR_RETURN(_sys_greatbelt_acl_lkup_l4_port_range(lchip, port0,
                                                                       port1, op_dest, &idx_target, &idx_lkup));

                if (SYS_ACL_L4_PORT_NUM != idx_lkup)
                {   /* lkup success */
                    acl_master[lchip]->l4_port[idx_lkup].ref++;

                    idx_valid = idx_lkup;
                }
                else if ((SYS_ACL_L4_PORT_NUM == idx_lkup)
                         && (acl_master[lchip]->l4_port_free_cnt > 0))
                {
                    CTC_ERROR_RETURN(_sys_greatbelt_acl_add_l4_port_range
                                         (lchip, port0, port1, op_dest, idx_target));

                    idx_valid = idx_target;
                }
                else
                {
                    ret = CTC_E_ACL_L4_PORT_RANGE_EXHAUSTED;
                    goto error;
                }

                p_sys_key->l4info_mapped      |= 1 << idx_valid;
                p_sys_key->l4info_mapped_mask |= 1 << idx_valid;

                if (0 == op_dest)
                {
                    p_sys_key->flag.l4_src_port_range = 1;
                    p_sys_key->l4_src_port_range_idx  = idx_valid;
                }
                else
                {
                    p_sys_key->flag.l4_dst_port_range = 1;
                    p_sys_key->l4_dst_port_range_idx  = idx_valid;
                }
            }
        }
        else    /* other layer 4 protocol type */
        {
            p_sys_key->flag.l4info_mapped  = 1;
            p_sys_key->l4info_mapped      |= p_ctc_key->l4_protocol;
            p_sys_key->l4info_mapped_mask |= p_ctc_key->l4_protocol_mask;

            p_sys_key->flag.is_tcp = 1;
            p_sys_key->is_tcp      = 0;

            p_sys_key->flag.is_udp = 1;
            p_sys_key->is_udp      = 0;
        }
    }

    /* ip fragment */
    if (CTC_FLAG_ISSET(flag, CTC_ACL_PBR_IPV4_KEY_FLAG_IP_FRAG))
    {
        p_sys_key->flag.frag_info = 1;

        CTC_MAX_VALUE_CHECK(p_ctc_key->ip_frag, CTC_IP_FRAG_MAX - 1);

        switch (p_ctc_key->ip_frag)
        {
        case CTC_IP_FRAG_NON:
            /* Non fragmented */
            p_sys_key->frag_info      = 0;
            p_sys_key->frag_info_mask = 3;
            break;

        case CTC_IP_FRAG_FIRST:
            /* Fragmented, and first fragment */
            p_sys_key->frag_info      = 1;
            p_sys_key->frag_info_mask = 3;
            break;

        case CTC_IP_FRAG_NON_OR_FIRST:
            /* Non fragmented OR Fragmented and first fragment */
            p_sys_key->frag_info      = 0;
            p_sys_key->frag_info_mask = 2;     /* mask frag_info as 0x */
            break;

        case CTC_IP_FRAG_SMALL:
            /* Small fragment */
            p_sys_key->frag_info      = 2;
            p_sys_key->frag_info_mask = 3;
            break;

        case CTC_IP_FRAG_NOT_FIRST:
            /* Not first fragment (Fragmented of cause) */
            p_sys_key->frag_info      = 3;
            p_sys_key->frag_info_mask = 3;
            break;

        default:
            return CTC_E_INVALID_PARAM;
        }
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_PBR_IPV4_KEY_FLAG_VRFID))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->vrfid, 0x3FFF);
        p_sys_key->flag.vrfid = 1;
        p_sys_key->vrfid      = p_ctc_key->vrfid;
        p_sys_key->vrfid_mask = p_ctc_key->vrfid_mask;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_PBR_IPV4_KEY_FLAG_DSCP))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->dscp, 0x3F);
        p_sys_key->flag.dscp = 1;
        p_sys_key->dscp      = p_ctc_key->dscp;
        p_sys_key->dscp_mask = (p_ctc_key->dscp_mask) & 0x3F;
    }
    else if (CTC_FLAG_ISSET(flag, CTC_ACL_PBR_IPV4_KEY_FLAG_PRECEDENCE))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->dscp, 7);

        p_sys_key->flag.dscp = 1;
        p_sys_key->dscp      = (p_ctc_key->dscp) << 3;
        p_sys_key->dscp_mask = (p_ctc_key->dscp_mask & 0x7) << 3;
    }

    /* tcp flag */
    if (p_sys_key->flag.is_tcp
        && p_sys_key->is_tcp
        && CTC_FLAG_ISSET(sub_flag, CTC_ACL_PBR_IPV4_KEY_SUB_FLAG_TCP_FLAGS))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->tcp_flags_match_any, 1);
        CTC_MAX_VALUE_CHECK(p_ctc_key->tcp_flags, 0x3F);

        CTC_ERROR_RETURN(_sys_greatbelt_acl_lkup_tcp_flag(lchip, p_ctc_key->tcp_flags_match_any,
                                                          p_ctc_key->tcp_flags, &idx_target, &idx_lkup));

        if (SYS_ACL_TCP_FLAG_NUM != idx_lkup)
        {   /* lkup success */
            acl_master[lchip]->tcp_flag[idx_lkup].ref++;

            idx_valid = idx_lkup;
        }
        else if ((SYS_ACL_TCP_FLAG_NUM == idx_lkup) && (acl_master[lchip]->tcp_flag_free_cnt > 0))
        {
            _sys_greatbelt_acl_add_tcp_flag(lchip, p_ctc_key->tcp_flags_match_any,
                                            p_ctc_key->tcp_flags, idx_target);
            idx_valid = idx_target;
        }
        else
        {
            ret = CTC_E_ACL_TCP_FLAG_EXHAUSTED;
            goto error;
        }

        p_sys_key->flag.l4info_mapped  = 1;
        p_sys_key->l4info_mapped      |= 1 << (idx_valid + 8);
        p_sys_key->l4info_mapped_mask |= 1 << (idx_valid + 8);

        p_sys_key->flag.tcp_flags = 1;
        p_sys_key->tcp_flags_idx  = idx_valid;
    }

    return CTC_E_NONE;


 error:
    return ret;
}

STATIC int32
_sys_greatbelt_acl_map_pbr_ipv6_key(uint8 lchip, ctc_acl_pbr_ipv6_key_t* p_ctc_key,
                                    sys_acl_pbr_ipv6_key_t* p_sys_key)
{
    uint32 flag       = 0;
    uint32 sub_flag   = 0;
    int32  ret        = 0;
    uint8  idx_lkup   = 0;
    uint8  idx_target = 0;
    uint8  idx_valid  = 0;

    uint16 port0                 = 0;
    uint16 port1                 = 0;
    uint8  op_dest               = 0; /* loop 2 times, for saving code.*/
    uint8  l4_src_port_use_range = 0; /* src port use range */
    uint8  l4_dst_port_use_range = 0; /* dst port use range */

    CTC_PTR_VALID_CHECK(p_ctc_key);
    CTC_PTR_VALID_CHECK(p_sys_key);

    SYS_ACL_DBG_FUNC();

    flag     = p_ctc_key->flag;
    sub_flag = p_ctc_key->sub_flag;

    if (CTC_FLAG_ISSET(flag, CTC_ACL_PBR_IPV6_KEY_FLAG_DSCP)
        && CTC_FLAG_ISSET(flag, CTC_ACL_PBR_IPV6_KEY_FLAG_PRECEDENCE))
    {
        return CTC_E_ACL_FLAG_COLLISION;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_PBR_IPV6_KEY_FLAG_IP_DA))
    {
        p_sys_key->flag.ip_da = 1;
        sal_memcpy(p_sys_key->ip_da, p_ctc_key->ip_da, sizeof(ipv6_addr_t));
        sal_memcpy(p_sys_key->ip_da_mask, p_ctc_key->ip_da_mask, sizeof(ipv6_addr_t));
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_PBR_IPV6_KEY_FLAG_IP_SA))
    {
        p_sys_key->flag.ip_sa = 1;
        sal_memcpy(p_sys_key->ip_sa, p_ctc_key->ip_sa, sizeof(ipv6_addr_t));
        sal_memcpy(p_sys_key->ip_sa_mask, p_ctc_key->ip_sa_mask, sizeof(ipv6_addr_t));
    }

    /* layer 4 information */
    if (CTC_FLAG_ISSET(flag, CTC_ACL_PBR_IPV6_KEY_FLAG_L4_PROTOCOL))
    {
        if ((0xFF == p_ctc_key->l4_protocol_mask)
            && (IPV6_ICMP_PROTOCOL == p_ctc_key->l4_protocol))        /* ICMP */
        {
            p_sys_key->flag.l4info_mapped = 1;
            p_sys_key->l4info_mapped      = p_ctc_key->l4_protocol;
            p_sys_key->l4info_mapped_mask = p_ctc_key->l4_protocol_mask;

            p_sys_key->flag.is_tcp = 1;
            p_sys_key->is_tcp      = 0;

            p_sys_key->flag.is_udp = 1;
            p_sys_key->is_udp      = 0;

            /* icmp type */
            if (CTC_FLAG_ISSET(sub_flag, CTC_ACL_PBR_IPV6_KEY_SUB_FLAG_ICMP_TYPE))
            {
                p_sys_key->flag.l4_src_port  = 1;
                p_sys_key->l4_src_port       = (p_ctc_key->icmp_type << 8) & 0xFF00;
                p_sys_key->l4_src_port_mask |= (p_ctc_key->icmp_type_mask << 8) & 0xFF00;
            }

            /* icmp code */
            if (CTC_FLAG_ISSET(sub_flag, CTC_ACL_PBR_IPV6_KEY_SUB_FLAG_ICMP_CODE))
            {
                p_sys_key->flag.l4_src_port  = 1;
                p_sys_key->l4_src_port      |= p_ctc_key->icmp_code & 0x00FF;
                p_sys_key->l4_src_port_mask |= p_ctc_key->icmp_code_mask & 0x00FF;
            }
        }
        else if ((0xFF == p_ctc_key->l4_protocol_mask)
                 && ((TCP_PROTOCOL == p_ctc_key->l4_protocol)
                     || (UDP_PROTOCOL == p_ctc_key->l4_protocol))) /* TCP or UDP */
        {
            if (TCP_PROTOCOL == p_ctc_key->l4_protocol)
            {   /* TCP */
                p_sys_key->flag.is_tcp = 1;
                p_sys_key->is_tcp      = 1;
            }
            else
            {   /* UDP */
                p_sys_key->flag.is_udp = 1;
                p_sys_key->is_udp      = 1;
            }

            if (CTC_FLAG_ISSET(sub_flag, CTC_ACL_PBR_IPV6_KEY_SUB_FLAG_L4_SRC_PORT))
            {
                if (p_ctc_key->l4_src_port_use_mask)
                {   /*  use mask.
                     *  l4_src_port_0 is data.
                     *  l4_src_port_1 is mask.
                     */
                    p_sys_key->flag.l4_src_port = 1;
                    p_sys_key->l4_src_port      = p_ctc_key->l4_src_port_0;
                    p_sys_key->l4_src_port_mask = p_ctc_key->l4_src_port_1;
                }
                else
                {   /*  use range.
                     *  l4_src_port_0 is min.
                     *  l4_src_port_1 is max.
                     */
                    if (p_ctc_key->l4_src_port_0 >= p_ctc_key->l4_src_port_1)
                    {
                        return CTC_E_INVALID_PARAM;
                    }

                    l4_src_port_use_range = 1;
                }
            }

            if (CTC_FLAG_ISSET(sub_flag, CTC_ACL_PBR_IPV6_KEY_SUB_FLAG_L4_DST_PORT))
            {
                if (p_ctc_key->l4_dst_port_use_mask)
                {   /*  use mask.
                     *  l4_dst_port_0 is data.
                     *  l4_dst_port_1 is mask.
                     */
                    p_sys_key->flag.l4_dst_port = 1;
                    p_sys_key->l4_dst_port      = p_ctc_key->l4_dst_port_0;
                    p_sys_key->l4_dst_port_mask = p_ctc_key->l4_dst_port_1;
                }
                else
                {   /*  use range.
                     *  l4_dst_port_0 is min.
                     *  l4_dst_port_1 is max.
                     */
                    if (p_ctc_key->l4_dst_port_0 >= p_ctc_key->l4_dst_port_1)
                    {
                        return CTC_E_INVALID_PARAM;
                    }

                    l4_dst_port_use_range = 1;
                }
            }

            /* op_dest, use loop to save code.
             * op_dest = 0 -->src_port
             * op_dest = 1 -->dst_port
             */
            for (op_dest = 0; op_dest <= 1; op_dest++)
            {
                if (0 == op_dest)
                {
                    if (!l4_src_port_use_range)
                    {
                        continue;
                    }

                    port0 = p_ctc_key->l4_src_port_0;
                    port1 = p_ctc_key->l4_src_port_1;
                }
                else
                {
                    if (!l4_dst_port_use_range)
                    {
                        continue;
                    }

                    port0 = p_ctc_key->l4_dst_port_0;
                    port1 = p_ctc_key->l4_dst_port_1;
                }

                p_sys_key->flag.l4info_mapped = 1;

                CTC_ERROR_RETURN(_sys_greatbelt_acl_lkup_l4_port_range(lchip, port0,
                                                                       port1, op_dest, &idx_target, &idx_lkup));

                if (SYS_ACL_L4_PORT_NUM != idx_lkup)
                {   /* lkup success */
                    acl_master[lchip]->l4_port[idx_lkup].ref++;

                    idx_valid = idx_lkup;
                }
                else if ((SYS_ACL_L4_PORT_NUM == idx_lkup) && (acl_master[lchip]->l4_port_free_cnt > 0))
                {
                    CTC_ERROR_RETURN(_sys_greatbelt_acl_add_l4_port_range
                                         (lchip, port0, port1, op_dest, idx_target));

                    idx_valid = idx_target;
                }
                else
                {
                    ret = CTC_E_ACL_L4_PORT_RANGE_EXHAUSTED;
                    goto error;
                }

                p_sys_key->l4info_mapped      |= 1 << idx_valid;
                p_sys_key->l4info_mapped_mask |= 1 << idx_valid;

                if (0 == op_dest)
                {
                    p_sys_key->flag.l4_src_port_range = 1;
                    p_sys_key->l4_src_port_range_idx  = idx_valid;
                }
                else
                {
                    p_sys_key->flag.l4_dst_port_range = 1;
                    p_sys_key->l4_dst_port_range_idx  = idx_valid;
                }
            }
        }
        else    /* other layer 4 protocol type */
        {
            p_sys_key->flag.l4info_mapped = 1;
            p_sys_key->l4info_mapped      = p_ctc_key->l4_protocol;
            p_sys_key->l4info_mapped_mask = p_ctc_key->l4_protocol_mask;

            p_sys_key->flag.is_tcp = 1;
            p_sys_key->is_tcp      = 0;

            p_sys_key->flag.is_udp = 1;
            p_sys_key->is_udp      = 0;
        }
    }

    /* ip fragment */
    if (CTC_FLAG_ISSET(flag, CTC_ACL_PBR_IPV6_KEY_FLAG_IP_FRAG))
    {
        p_sys_key->flag.frag_info = 1;

        CTC_MAX_VALUE_CHECK(p_ctc_key->ip_frag, CTC_IP_FRAG_MAX - 1);

        switch (p_ctc_key->ip_frag)
        {
        case CTC_IP_FRAG_NON:
            /* Non fragmented */
            p_sys_key->frag_info      = 0;
            p_sys_key->frag_info_mask = 3;
            break;

        case CTC_IP_FRAG_FIRST:
            /* Fragmented, and first fragment */
            p_sys_key->frag_info      = 1;
            p_sys_key->frag_info_mask = 3;
            break;

        case CTC_IP_FRAG_NON_OR_FIRST:
            /* Non fragmented OR Fragmented and first fragment */
            p_sys_key->frag_info      = 0;
            p_sys_key->frag_info_mask = 2;     /* mask frag_info as 0x */
            break;

        case CTC_IP_FRAG_SMALL:
            /* Small fragment */
            p_sys_key->frag_info      = 2;
            p_sys_key->frag_info_mask = 3;
            break;

        case CTC_IP_FRAG_NOT_FIRST:
            /* Not first fragment (Fragmented of cause) */
            p_sys_key->frag_info      = 3;
            p_sys_key->frag_info_mask = 3;
            break;

        default:
            return CTC_E_INVALID_PARAM;
        }
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_PBR_IPV6_KEY_FLAG_DSCP))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->dscp, 0x3F);
        p_sys_key->flag.dscp = 1;
        p_sys_key->dscp      = p_ctc_key->dscp;
        p_sys_key->dscp_mask = p_ctc_key->dscp_mask;
    }
    else if (CTC_FLAG_ISSET(flag, CTC_ACL_PBR_IPV6_KEY_FLAG_PRECEDENCE))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->dscp, 7);

        p_sys_key->flag.dscp = 1;
        p_sys_key->dscp      = (p_ctc_key->dscp) << 3;
        p_sys_key->dscp_mask = (p_ctc_key->dscp_mask & 0x7) << 3;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_PBR_IPV6_KEY_FLAG_FLOW_LABEL))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->flow_label, 0xFFFFF);
        p_sys_key->flag.flow_label = 1;
        p_sys_key->flow_label      = p_ctc_key->flow_label;
        p_sys_key->flow_label_mask = p_ctc_key->flow_label_mask & 0xFFFFF;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_PBR_IPV6_KEY_FLAG_MAC_DA))
    {
        p_sys_key->flag.mac_da = 1;
        sal_memcpy(p_sys_key->mac_da, p_ctc_key->mac_da, CTC_ETH_ADDR_LEN);
        sal_memcpy(p_sys_key->mac_da_mask, p_ctc_key->mac_da_mask, CTC_ETH_ADDR_LEN);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_PBR_IPV6_KEY_FLAG_MAC_SA))
    {
        p_sys_key->flag.mac_sa = 1;
        sal_memcpy(p_sys_key->mac_sa, p_ctc_key->mac_sa, CTC_ETH_ADDR_LEN);
        sal_memcpy(p_sys_key->mac_sa_mask, p_ctc_key->mac_sa_mask, CTC_ETH_ADDR_LEN);
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_PBR_IPV6_KEY_FLAG_CVLAN))
    {
        SYS_ACL_VLAN_ID_CHECK(p_ctc_key->cvlan);
        p_sys_key->flag.cvlan = 1;
        p_sys_key->cvlan      = p_ctc_key->cvlan;
        p_sys_key->cvlan_mask = p_ctc_key->cvlan_mask & 0xFFF;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_PBR_IPV6_KEY_FLAG_CTAG_COS))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->ctag_cos, 0x7);
        p_sys_key->flag.ctag_cos = 1;
        p_sys_key->ctag_cos      = p_ctc_key->ctag_cos;
        p_sys_key->ctag_cos_mask = (p_ctc_key->ctag_cos_mask) & 0x7;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_PBR_IPV6_KEY_FLAG_CTAG_CFI))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->ctag_cfi, 0x1);
        p_sys_key->flag.ctag_cfi = 1;
        p_sys_key->ctag_cfi      = p_ctc_key->ctag_cfi;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_PBR_IPV6_KEY_FLAG_SVLAN))
    {
        SYS_ACL_VLAN_ID_CHECK(p_ctc_key->svlan);
        p_sys_key->flag.svlan = 1;
        p_sys_key->svlan      = p_ctc_key->svlan;
        p_sys_key->svlan_mask = p_ctc_key->svlan_mask & 0xFFF;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_PBR_IPV6_KEY_FLAG_STAG_COS))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->stag_cos, 0x7);
        p_sys_key->flag.stag_cos = 1;
        p_sys_key->stag_cos      = p_ctc_key->stag_cos;
        p_sys_key->stag_cos_mask = (p_ctc_key->stag_cos_mask) & 0x7;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_PBR_IPV6_KEY_FLAG_STAG_CFI))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->stag_cfi, 0x1);
        p_sys_key->flag.stag_cfi = 1;
        p_sys_key->stag_cfi      = p_ctc_key->stag_cfi;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_PBR_IPV6_KEY_FLAG_ETH_TYPE))
    {
        p_sys_key->flag.eth_type = 1;
        p_sys_key->eth_type      = p_ctc_key->eth_type;
        p_sys_key->eth_type_mask = p_ctc_key->eth_type_mask;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_PBR_IPV6_KEY_FLAG_L2_TYPE))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->l2_type, 0xF);
        p_sys_key->flag.l2_type = 1;
        p_sys_key->l2_type      = p_ctc_key->l2_type;
        p_sys_key->l2_type_mask = p_ctc_key->l2_type_mask;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_PBR_IPV6_KEY_FLAG_L3_TYPE))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->l3_type, 0xF);
        p_sys_key->flag.l3_type = 1;
        p_sys_key->l3_type      = p_ctc_key->l3_type;
        p_sys_key->l3_type_mask = p_ctc_key->l3_type_mask;
    }

    if (CTC_FLAG_ISSET(flag, CTC_ACL_PBR_IPV6_KEY_FLAG_VRFID))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->vrfid, 0x3FFF);
        p_sys_key->flag.vrfid = 1;
        p_sys_key->vrfid      = p_ctc_key->vrfid;
        p_sys_key->vrfid_mask = p_ctc_key->vrfid_mask;
    }

    if (p_sys_key->flag.is_tcp
        && p_sys_key->is_tcp
        && CTC_FLAG_ISSET(sub_flag, CTC_ACL_PBR_IPV6_KEY_SUB_FLAG_TCP_FLAGS))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->tcp_flags_match_any, 1);
        CTC_MAX_VALUE_CHECK(p_ctc_key->tcp_flags, 0x3F);

        CTC_ERROR_RETURN(_sys_greatbelt_acl_lkup_tcp_flag(lchip, p_ctc_key->tcp_flags_match_any,
                                                          p_ctc_key->tcp_flags, &idx_target, &idx_lkup));

        if (SYS_ACL_TCP_FLAG_NUM != idx_lkup)
        {   /* lkup success */
            acl_master[lchip]->tcp_flag[idx_lkup].ref++;

            idx_valid = idx_lkup;
        }
        else if ((SYS_ACL_TCP_FLAG_NUM == idx_lkup) && (acl_master[lchip]->tcp_flag_free_cnt > 0))
        {
            _sys_greatbelt_acl_add_tcp_flag(lchip, p_ctc_key->tcp_flags_match_any,
                                            p_ctc_key->tcp_flags, idx_target);
            idx_valid = idx_target;
        }
        else
        {
            ret = CTC_E_ACL_TCP_FLAG_EXHAUSTED;
            goto error;
        }

        p_sys_key->flag.l4info_mapped  = 1;
        p_sys_key->l4info_mapped      |= 1 << (idx_valid + 8);
        p_sys_key->l4info_mapped_mask |= 1 << (idx_valid + 8);

        p_sys_key->flag.tcp_flags = 1;
        p_sys_key->tcp_flags_idx  = idx_valid;
    }

    return CTC_E_NONE;

 error:
    return ret;
}

/*
 * get sys key based on ctc key
 */
STATIC int32
_sys_greatbelt_acl_map_key(uint8 lchip, ctc_acl_key_t* p_ctc_key,
                           sys_acl_key_t* p_sys_key)
{
    CTC_PTR_VALID_CHECK(p_ctc_key);
    CTC_PTR_VALID_CHECK(p_sys_key);

    SYS_ACL_DBG_FUNC();

    p_sys_key->type = p_ctc_key->type;

    switch (p_ctc_key->type)
    {
    case CTC_ACL_KEY_MAC:
        /* merge key cannot add mac key */
        #if 0 /* for add default entry to discard */
        if (acl_master[lchip]->acl_register.merge)
        {
            return CTC_E_INVALID_PARAM;
        }
        #endif
        CTC_ERROR_RETURN(
            _sys_greatbelt_acl_map_tcam_key_port(lchip, &p_ctc_key->u.mac_key.port, &p_sys_key->u0.mac_key.port, 0));
        CTC_ERROR_RETURN(
            _sys_greatbelt_acl_map_mac_key(lchip, &p_ctc_key->u.mac_key, &p_sys_key->u0.mac_key));
        break;

    case CTC_ACL_KEY_IPV4:
        CTC_ERROR_RETURN(
            _sys_greatbelt_acl_map_tcam_key_port(lchip, &p_ctc_key->u.ipv4_key.port, &p_sys_key->u0.ipv4_key.port, 0));
        CTC_ERROR_RETURN(
            _sys_greatbelt_acl_map_ipv4_key(lchip, &p_ctc_key->u.ipv4_key, &p_sys_key->u0.ipv4_key));
        break;

    case CTC_ACL_KEY_MPLS:
        CTC_ERROR_RETURN(
            _sys_greatbelt_acl_map_tcam_key_port(lchip, &p_ctc_key->u.mpls_key.port, &p_sys_key->u0.mpls_key.port, 1));
        CTC_ERROR_RETURN(
            _sys_greatbelt_acl_map_mpls_key(lchip, &p_ctc_key->u.mpls_key, &p_sys_key->u0.mpls_key));
        break;

    case CTC_ACL_KEY_IPV6:
        CTC_ERROR_RETURN(
            _sys_greatbelt_acl_map_tcam_key_port(lchip, &p_ctc_key->u.ipv6_key.port, &p_sys_key->u0.ipv6_key.port, 0));
        CTC_ERROR_RETURN(
            _sys_greatbelt_acl_map_ipv6_key(lchip, &p_ctc_key->u.ipv6_key, &p_sys_key->u0.ipv6_key));
        break;

    case CTC_ACL_KEY_HASH_MAC:
        CTC_ERROR_RETURN(
            _sys_greatbelt_acl_map_hash_mac_key(lchip, &p_ctc_key->u.hash_mac_key, &p_sys_key->u0.hash.u0.hash_mac_key));
        break;

    case CTC_ACL_KEY_HASH_IPV4:
        CTC_ERROR_RETURN(
            _sys_greatbelt_acl_map_hash_ipv4_key(lchip, &p_ctc_key->u.hash_ipv4_key, &p_sys_key->u0.hash.u0.hash_ipv4_key));
        break;

    case CTC_ACL_KEY_PBR_IPV4:
        CTC_ERROR_RETURN(
            _sys_greatbelt_acl_map_tcam_key_port(lchip, &p_ctc_key->u.pbr_ipv4_key.port, &p_sys_key->u0.pbr_ipv4_key.port, 0));
        CTC_ERROR_RETURN(
            _sys_greatbelt_acl_map_pbr_ipv4_key(lchip, &p_ctc_key->u.pbr_ipv4_key, &p_sys_key->u0.pbr_ipv4_key));
        break;

    case CTC_ACL_KEY_PBR_IPV6:
        CTC_ERROR_RETURN(
            _sys_greatbelt_acl_map_tcam_key_port(lchip, &p_ctc_key->u.pbr_ipv6_key.port, &p_sys_key->u0.pbr_ipv6_key.port, 0));
        CTC_ERROR_RETURN(
            _sys_greatbelt_acl_map_pbr_ipv6_key(lchip, &p_ctc_key->u.pbr_ipv6_key, &p_sys_key->u0.pbr_ipv6_key));
        break;

    default:
        return CTC_E_ACL_INVALID_KEY_TYPE;
    }

    if (!ACL_ENTRY_IS_TCAM(p_ctc_key->type))
    {
        p_sys_key->u0.hash.lchip = lchip;
    }


    return CTC_E_NONE;
}

/*
 * check group info if it is valid.
 */
STATIC int32
_sys_greatbelt_acl_check_group_info(uint8 lchip, ctc_acl_group_info_t* pinfo, uint8 type, uint8 is_create)
{
    uint8 idx = 0;

    CTC_PTR_VALID_CHECK(pinfo);
    SYS_ACL_DBG_FUNC();

    if (is_create)
    {
        if ((CTC_ACL_GROUP_TYPE_PORT_BITMAP == type) || (CTC_ACL_GROUP_TYPE_PORT_CLASS == type))
        {   /* port bitmap , port class will care lchip. others ignore. */

        }
        if (pinfo->priority >= acl_master[lchip]->block_num_max)
        {
            return CTC_E_ACL_GROUP_PRIORITY;
        }
    }

    CTC_MAX_VALUE_CHECK(pinfo->dir, CTC_BOTH_DIRECTION);

    switch (type)
    {
    case CTC_ACL_GROUP_TYPE_PORT_BITMAP:

        /* port_bitmap support 56 bits for ipv4/ipv6 key, but mpls key only support 52 bits */
        for (idx = 1; idx < CTC_PORT_BITMAP_IN_WORD; idx++)
        {
            if ((1 == idx) && ((pinfo->un.port_bitmap[1]) >> 24))
            {
                return CTC_E_ACL_PORT_BITMAP_OVERFLOW;
            }
            else if ((1 < idx) && (pinfo->un.port_bitmap[idx]))
            {
                return CTC_E_ACL_PORT_BITMAP_OVERFLOW;
            }
        }
        break;

    case CTC_ACL_GROUP_TYPE_GLOBAL:
    case CTC_ACL_GROUP_TYPE_NONE:
        break;

    case CTC_ACL_GROUP_TYPE_VLAN_CLASS:
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

    default:
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

/*
 * check if install group info is right, and if it is new.
 */
STATIC bool
_sys_greatbelt_acl_is_group_info_new(uint8 lchip, sys_acl_group_info_t* sys,
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
_sys_greatbelt_acl_map_group_info(uint8 lchip, sys_acl_group_info_t* sys, ctc_acl_group_info_t* ctc, uint8 is_create)
{
    if (is_create) /* create group care type, priority, gchip*/
    {
        sys->type     = ctc->type;
        sys->block_id = ctc->priority;
    }

    sys->dir = ctc->dir;

    sal_memcpy(&sys->un, &ctc->un, sizeof(ctc_port_bitmap_t));

    return CTC_E_NONE;
}


/*
 * get ctc group info based on sys group info
 */
STATIC int32
_sys_greatbelt_acl_unmap_group_info(uint8 lchip, ctc_acl_group_info_t* ctc, sys_acl_group_info_t* sys)
{
    ctc->dir      = sys->dir;
    ctc->type     = sys->type;
    ctc->priority = sys->block_id;
    ctc->lchip    = lchip;

    sal_memcpy(&ctc->un, &sys->un, sizeof(ctc_port_bitmap_t));

    return CTC_E_NONE;
}


/*
 * remove accessory property
 */
STATIC int32
_sys_greatbelt_acl_remove_accessory_property(uint8 lchip, sys_acl_entry_t* pe, sys_acl_action_t* pa)
{

    CTC_PTR_VALID_CHECK(pe);
    CTC_PTR_VALID_CHECK(pa);

    /* remove tcp flags and port range */
    switch (pe->key.type)
    {
    case CTC_ACL_KEY_MAC:
    case CTC_ACL_KEY_MPLS:
        break;

    case CTC_ACL_KEY_IPV4:
        if (pe->key.u0.ipv4_key.flag.tcp_flags)
        {
            CTC_ERROR_RETURN(_sys_greatbelt_acl_remove_tcp_flag(lchip, pe->key.u0.ipv4_key.tcp_flags_idx));
        }

        if (pe->key.u0.ipv4_key.flag.l4_src_port_range)
        {
            CTC_ERROR_RETURN(_sys_greatbelt_acl_remove_l4_port_range(lchip, pe->key.u0.ipv4_key.l4_src_port_range_idx));
        }

        if (pe->key.u0.ipv4_key.flag.l4_dst_port_range)
        {
            CTC_ERROR_RETURN(_sys_greatbelt_acl_remove_l4_port_range(lchip, pe->key.u0.ipv4_key.l4_dst_port_range_idx));
        }

        break;

    case CTC_ACL_KEY_IPV6:
        if (pe->key.u0.ipv6_key.flag.tcp_flags)
        {
            CTC_ERROR_RETURN(_sys_greatbelt_acl_remove_tcp_flag(lchip, pe->key.u0.ipv6_key.tcp_flags_idx));
        }

        if (pe->key.u0.ipv6_key.flag.l4_src_port_range)
        {
            CTC_ERROR_RETURN(_sys_greatbelt_acl_remove_l4_port_range(lchip, pe->key.u0.ipv6_key.l4_src_port_range_idx));
        }

        if (pe->key.u0.ipv6_key.flag.l4_dst_port_range)
        {
            CTC_ERROR_RETURN(_sys_greatbelt_acl_remove_l4_port_range(lchip, pe->key.u0.ipv6_key.l4_dst_port_range_idx));
        }

        break;

    case CTC_ACL_KEY_HASH_MAC:
    case CTC_ACL_KEY_HASH_IPV4:
        break;

    case CTC_ACL_KEY_PBR_IPV4:
        if (pe->key.u0.pbr_ipv4_key.flag.tcp_flags)
        {
            CTC_ERROR_RETURN(_sys_greatbelt_acl_remove_tcp_flag(lchip, pe->key.u0.pbr_ipv4_key.tcp_flags_idx));
        }

        if (pe->key.u0.pbr_ipv4_key.flag.l4_src_port_range)
        {
            CTC_ERROR_RETURN(_sys_greatbelt_acl_remove_l4_port_range(lchip, pe->key.u0.pbr_ipv4_key.l4_src_port_range_idx));
        }

        if (pe->key.u0.pbr_ipv4_key.flag.l4_dst_port_range)
        {
            CTC_ERROR_RETURN(_sys_greatbelt_acl_remove_l4_port_range(lchip, pe->key.u0.pbr_ipv4_key.l4_dst_port_range_idx));
        }

        break;

    case CTC_ACL_KEY_PBR_IPV6:
        if (pe->key.u0.pbr_ipv6_key.flag.tcp_flags)
        {
            CTC_ERROR_RETURN(_sys_greatbelt_acl_remove_tcp_flag(lchip, pe->key.u0.pbr_ipv6_key.tcp_flags_idx));
        }

        if (pe->key.u0.pbr_ipv6_key.flag.l4_src_port_range)
        {
            CTC_ERROR_RETURN(_sys_greatbelt_acl_remove_l4_port_range(lchip, pe->key.u0.pbr_ipv6_key.l4_src_port_range_idx));
        }

        if (pe->key.u0.pbr_ipv6_key.flag.l4_dst_port_range)
        {
            CTC_ERROR_RETURN(_sys_greatbelt_acl_remove_l4_port_range(lchip, pe->key.u0.pbr_ipv6_key.l4_dst_port_range_idx));
        }

        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    /* remove statsptr */
    if (pa->flag.stats)
    {
            pa->chip.stats_ptr = 0;
    }

    return CTC_E_NONE;
}

#define  __ACL_BUILD_FREE_INDEX__
/* free index of HASH ad */
STATIC int32
_sys_greatbelt_acl_free_hash_ad_index(uint8 lchip, uint32 ad_index)
{

    SYS_ACL_DBG_FUNC();

    CTC_ERROR_RETURN(sys_greatbelt_ftm_free_table_offset(lchip, DsAcl_t, CTC_INGRESS, 1, (uint32)ad_index));

    return CTC_E_NONE;
}



/* build index of HASH ad */
STATIC int32
_sys_greatbelt_acl_build_hash_ad_index(uint8 lchip, uint32* p_ad_index)
{
    uint32              value_32 = 0;

    SYS_ACL_DBG_FUNC();

    CTC_ERROR_RETURN(sys_greatbelt_ftm_alloc_table_offset(lchip, DsAcl_t, CTC_INGRESS, 1, &value_32));
    *p_ad_index = value_32;

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_acl_asic_hash_lkup(uint8 lchip, sys_acl_entry_t* pe, lookup_result_t* p_lookup_result)
{
    lookup_info_t lookup_info;
    uint32        key[MAX_ENTRY_WORD] = { 0 };

    CTC_PTR_VALID_CHECK(pe);
    CTC_PTR_VALID_CHECK(p_lookup_result);
    SYS_ACL_DBG_FUNC();

    sal_memset(&lookup_info, 0, sizeof(lookup_info_t));
    sal_memset(p_lookup_result, 0, sizeof(lookup_result_t));

    /* only chip 0 */
    lookup_info.chip_id     = lchip;
    lookup_info.hash_module = HASH_MODULE_FIB;

    switch (pe->key.type)
    {
    case CTC_ACL_KEY_HASH_MAC:
        lookup_info.hash_type = FIB_HASH_TYPE_ACL_MAC;
        /* asic hash care not ad_index.  */
        _sys_greatbelt_acl_build_ds_hash_mac_key(lchip, pe, (void *) key, 0);
        break;

    case CTC_ACL_KEY_HASH_IPV4:
        lookup_info.hash_type = FIB_HASH_TYPE_ACL_IPV4;
        /* asic hash care not ad_index.  */
        _sys_greatbelt_acl_build_ds_hash_ipv4_key(lchip, pe, (void *) key, 0);
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    lookup_info.p_ds_key = key;

    /* get index of Key */
    CTC_ERROR_RETURN(DRV_HASH_LOOKUP(&lookup_info, p_lookup_result));

    return CTC_E_NONE;
}

#define  __ACL_ESSENTIAL_API__

STATIC int32
_sys_greatbelt_acl_hash_ad_spool_remove(uint8 lchip, sys_acl_entry_t* pe,
                                        sys_acl_action_t* pa)

{
    uint32          index_free = 0;

    int32           ret = 0;
    sys_acl_action_t* pa_lkup;


        ret = ctc_spool_remove(acl_master[lchip]->ad_spool, pa, &pa_lkup);

        if (ret < 0)
        {
            return CTC_E_SPOOL_REMOVE_FAILED;
        }
        else if (ret == CTC_SPOOL_E_OPERATE_MEMORY)
        {
            index_free = pa_lkup->chip.ad_index;

            CTC_ERROR_RETURN(_sys_greatbelt_acl_free_hash_ad_index(lchip, index_free));

            SYS_ACL_DBG_INFO("  %% INFO: acl action removed: index = %d\n", index_free);
        }

    if (ret == CTC_SPOOL_E_OPERATE_MEMORY)
    {
        mem_free(pa_lkup);
    }

    return CTC_E_NONE;
}

/*
 * pe, pg, pb are lookup result.
 */
int32
_sys_greatbelt_acl_remove_entry(uint8 lchip, uint32 entry_id)
{
    uint32 block_index = 0;
    uint8  is_hash = 0;
    sys_acl_entry_t* pe_lkup = NULL;
    sys_acl_group_t* pg_lkup = NULL;
    sys_acl_block_t* pb_lkup = NULL;

    SYS_ACL_DBG_FUNC();

    /* check raw entry */
    _sys_greatbelt_acl_get_nodes_by_eid(lchip, entry_id, &pe_lkup, &pg_lkup, &pb_lkup);
    if(!pe_lkup)
    {
        return CTC_E_ACL_ENTRY_UNEXIST;
    }
    if(!pg_lkup)
    {
        return CTC_E_ACL_GROUP_UNEXIST;
    }

    if (FPA_ENTRY_FLAG_INSTALLED == pe_lkup->fpae.flag)
    {
        return CTC_E_ACL_ENTRY_INSTALLED;
    }

   /* remove from block */
    block_index = pe_lkup->fpae.offset_a;

    /* remove from group */
    ctc_slist_delete_node(pg_lkup->entry_list, &(pe_lkup->head));

    /* remove from hash */
    ctc_hash_remove(acl_master[lchip]->entry, pe_lkup);

    /* remove accessory property */
    _sys_greatbelt_acl_remove_accessory_property(lchip, pe_lkup, pe_lkup->action);

    /* remove action. this must after remove accesory property */
    is_hash = !ACL_ENTRY_IS_TCAM(pe_lkup->key.type);

    if (!is_hash) /* tcam entry */
    {
        fpa_greatbelt_free_offset(acl_master[lchip]->fpa, &pb_lkup->fpab, block_index);

        /* -- free action -- */
        mem_free(pe_lkup->action);
    }
    else /* hash entry */
    {
        /*delete hash entry from hw*/
        _sys_greatbelt_acl_set_hash_valid( lchip, block_index,pe_lkup->key.type,0);

        _sys_greatbelt_acl_hash_ad_spool_remove(lchip, pe_lkup, pe_lkup->action);

        /* remove from hash by key */
        ctc_hash_remove(acl_master[lchip]->hash_ent_key, pe_lkup);
        /* -- spool has already free pe_lkup->action, don't free again -- */
        /*mem_free(pe_lkup->action);*/

    }

    /* memory free */
    mem_free(pe_lkup);

    (pg_lkup->entry_count)--;

    return CTC_E_NONE;
}


STATIC int32
_sys_greatbelt_acl_hash_ad_spool_add(uint8 lchip, sys_acl_entry_t* pe,
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
        ret = ctc_spool_add(acl_master[lchip]->ad_spool, pa_new, NULL, &pa_get);

        if (ret < 0)
        {
            ret = CTC_E_SPOOL_ADD_UPDATE_FAILED;
            goto cleanup;
        }
        else if (ret == CTC_SPOOL_E_OPERATE_MEMORY)
        {
            uint32 ad_index_get;

            ret = _sys_greatbelt_acl_build_hash_ad_index(lchip, &ad_index_get);
            if (ret < 0)
            {
                ctc_spool_remove(acl_master[lchip]->ad_spool, pa_new, NULL);
                goto cleanup;
            }

            pa_get->chip.ad_index = ad_index_get;
        }

    if (CTC_SPOOL_E_OPERATE_REFCNT == ret)
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
_sys_greatbelt_acl_hash_key_hash_add(uint8 lchip, sys_acl_entry_t* pe,
                                     sys_acl_entry_t** pe_out)
{
    uint32          block_index = 0;
    sys_acl_entry_t * pe_new                            = NULL;
    lookup_result_t lookup_result;

    /* cacluate asic hash index */
    /* ASIC hash lookup, check if hash conflict */
    CTC_ERROR_RETURN(_sys_greatbelt_acl_asic_hash_lkup(lchip, pe, &lookup_result));

    if (!lookup_result.free && !lookup_result.valid)
    {
        lookup_result.conflict = 1;
        return CTC_E_SCL_HASH_CONFLICT;
    }
    else
    {
        block_index = lookup_result.key_index;
    }

    /* malloc sys entry */
    MALLOC_ZERO(MEM_ACL_MODULE, pe_new, acl_master[lchip]->entry_size[pe->key.type]);
    if (!pe_new)
    {
        return CTC_E_NO_MEMORY;
    }
    sal_memcpy(pe_new, pe, acl_master[lchip]->entry_size[pe->key.type]);

    /* set block index */
    pe_new->fpae.offset_a = block_index;

    /* add to hash */
    ctc_hash_insert(acl_master[lchip]->hash_ent_key, pe_new);

    /* clear hw table */
    if (block_index < acl_master[lchip]->hash_base)
    {
        _sys_greatbelt_acl_remove_cam(lchip, block_index);
    }
    else
    {
        hash_io_by_idx_para_t by_idx;
        sal_memset(&by_idx, 0, sizeof(by_idx));
        by_idx.chip_id = lchip;
        by_idx.table_id = (pe->key.type==CTC_ACL_KEY_HASH_IPV4 ? DsAclQosIpv4HashKey_t : DsAclQosMacHashKey_t);
        by_idx.table_index = block_index - acl_master[lchip]->hash_base;
        by_idx.key         = NULL;

        DRV_HASH_KEY_IOCTL(&by_idx, HASH_OP_TP_DEL_BY_INDEX, NULL);
    }

    CTC_ERROR_RETURN(_sys_greatbelt_acl_set_hash_valid( lchip, block_index,pe->key.type,1));
    *pe_out = pe_new;
    return CTC_E_NONE;
}


STATIC int32
_sys_greatbelt_acl_tcam_entry_add(uint8 lchip, sys_acl_group_t* pg,
                                  sys_acl_entry_t* pe,
                                  sys_acl_action_t* pa,
                                  sys_acl_entry_t** pe_out,
                                  sys_acl_action_t** pa_out)
{
    uint8           alloc_id;
    int32           ret;
    sys_acl_entry_t * pe_new                            = NULL;
    sys_acl_action_t* pa_new                            = NULL;
    sys_acl_block_t * pb                                = NULL;
    int32          block_index = 0;
    /* malloc sys entry */
    MALLOC_ZERO(MEM_ACL_MODULE, pe_new, acl_master[lchip]->entry_size[pe->key.type]);
    if (!pe_new)
    {
        return CTC_E_NO_MEMORY;
    }
    sal_memcpy(pe_new, pe, acl_master[lchip]->entry_size[pe->key.type]);

    /* malloc sys action */
    MALLOC_ZERO(MEM_ACL_MODULE, pa_new, sizeof(sys_acl_action_t));
    if (!pa_new)
    {
        mem_free(pe_new);
        return CTC_E_NO_MEMORY;
    }
    sal_memcpy(pa_new, pa, sizeof(sys_acl_action_t));

    /* get block index. */
    CTC_ERROR_RETURN(_sys_greatbelt_acl_get_tcam_alloc_info(lchip, pe->key.type, &(pe->group->group_info), &alloc_id, NULL));
    pb       = &acl_master[lchip]->block[alloc_id][pg->group_info.block_id];

        CTC_ERROR_GOTO(fpa_greatbelt_alloc_offset(acl_master[lchip]->fpa, &pb->fpab,
                                      pe->fpae.priority, &block_index), ret, cleanup);
        /* add to block */
        pb->fpab.entries[block_index] = &pe_new->fpae;

        /* free_count-- */
        (pb->fpab.free_count)--;

        /* set block index */
        pe_new->fpae.offset_a = block_index;

        fpa_greatbelt_reorder(acl_master[lchip]->fpa, &pb->fpab);

    *pe_out = pe_new;
    *pa_out = pa_new;

    return CTC_E_NONE;

 cleanup:
    mem_free(pe_new);
    mem_free(pa_new);
    return ret;
}


/*
 * add acl entry to software table
 * pe pa is all ready
 *
 */
STATIC int32
_sys_greatbelt_acl_add_entry(uint8 lchip, sys_acl_group_t* pg,
                             sys_acl_entry_t* pe,
                             sys_acl_action_t* pa)
{
    sys_acl_entry_t * pe_get = NULL;
    sys_acl_action_t* pa_get = NULL; /* get from share pool*/

    if (ACL_ENTRY_IS_TCAM(pe->key.type))
    {
        CTC_ERROR_RETURN(_sys_greatbelt_acl_tcam_entry_add(lchip, pg, pe, pa, &pe_get, &pa_get));
    }
    else
    {
        CTC_ERROR_RETURN(_sys_greatbelt_acl_hash_ad_spool_add(lchip, pe, pa, &pa_get));
        CTC_ERROR_RETURN(_sys_greatbelt_acl_hash_key_hash_add(lchip, pe, &pe_get));
    }

    /* -- set action ptr -- */
    pe_get->action = pa_get;

    /* add to hash */
    ctc_hash_insert(acl_master[lchip]->entry, pe_get);

    /* add to group */
    ctc_slist_add_head(pg->entry_list, &(pe_get->head));

    /* mark flag */
    pe_get->fpae.flag = FPA_ENTRY_FLAG_UNINSTALLED;

    (pg->entry_count)++;

    return CTC_E_NONE;
}

int32
_sys_greatbelt_acl_create_group(uint8 lchip, uint32 group_id, ctc_acl_group_info_t* group_info)
{
    sys_acl_group_t* pg_new = NULL;
    int32          ret;

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
            ret = CTC_E_INVALID_PTR;
            goto cleanup;
        }
        CTC_ERROR_GOTO(_sys_greatbelt_acl_check_group_info(lchip, group_info, group_info->type, 1), ret, cleanup);
        CTC_ERROR_GOTO(_sys_greatbelt_acl_map_group_info(lchip, &pg_new->group_info, group_info, 1), ret, cleanup);
    }

    pg_new->group_id   = group_id;
    pg_new->entry_list = ctc_slist_new();

    /* add to hash */
    ctc_hash_insert(acl_master[lchip]->group, pg_new);

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
_sys_greatbelt_acl_show_action(uint8 lchip, sys_acl_action_t* pa)
{
    char* tag_op[4]   = { "None", "Replace", "Add", "Delete" };
    char* field_op[3] = { "None", "Alternative", "New" };
    CTC_PTR_VALID_CHECK(pa);

    if (pa->flag.discard)
    {
        SYS_ACL_DBG_DUMP("  =================== deny ====================");
    }
    else
    {
        SYS_ACL_DBG_DUMP("  =================== permit ====================");
    }

    /* next line */
    SYS_ACL_DBG_DUMP("\n");

    if (pa->flag.deny_bridge)
    {
        SYS_ACL_DBG_DUMP("  deny-bridge,");
    }

    if (pa->flag.deny_learning)
    {
        SYS_ACL_DBG_DUMP("  deny-learning,");
    }

    if (pa->flag.deny_route)
    {
        SYS_ACL_DBG_DUMP("  deny-route,");
    }

    if ((pa->flag.deny_bridge) +
        (pa->flag.deny_learning) +
        (pa->flag.deny_route))
    {
        SYS_ACL_DBG_DUMP("\n");
    }

    if (pa->flag.stats)
    {
        SYS_ACL_DBG_DUMP("  stats,");
    }

    if (pa->flag.copy_to_cpu)
    {
        SYS_ACL_DBG_DUMP("  copy-to-cpu,");
    }

    if (pa->flag.copy_to_cpu_with_timestamp)
    {
        SYS_ACL_DBG_DUMP("  copy-to-cpu-with-timestamp,");
    }

    if (pa->flag.aging)
    {
        SYS_ACL_DBG_DUMP("  aging,");
    }

    if ((pa->flag.stats) +
        (pa->flag.copy_to_cpu) +
        (pa->flag.aging) +
        (pa->flag.copy_to_cpu_with_timestamp))
    {
        SYS_ACL_DBG_DUMP("\n");
    }

    if (pa->flag.trust)
    {
        SYS_ACL_DBG_DUMP("  trust %u,", pa->trust);
    }

    if (pa->flag.priority_and_color)
    {
        SYS_ACL_DBG_DUMP("  priority %u color %u,", pa->priority, pa->color);
    }

    if (pa->flag.dscp)
    {
        SYS_ACL_DBG_DUMP("  dscp %u,", pa->dscp);
    }

    if ((pa->flag.trust) +
        (pa->flag.priority_and_color) +
        (pa->flag.dscp))
    {
        SYS_ACL_DBG_DUMP("\n");
    }

    if (pa->flag.random_log)
    {
        SYS_ACL_DBG_DUMP("  random-log id %u  log-percent %u,", pa->acl_log_id, pa->random_threshold_shift);
    }

    if (pa->flag.redirect)
    {
        SYS_ACL_DBG_DUMP("  redirect nhid %u,", pa->nh_id);
    }

    if (pa->flag.qos_domain)
    {
        SYS_ACL_DBG_DUMP("  qos-domain :%u,", pa->qos_domain);
    }

    if ((pa->flag.random_log) +
        (pa->flag.redirect) +
        (pa->flag.qos_domain))
    {
        SYS_ACL_DBG_DUMP("\n");
    }

    if (pa->flag.micro_flow_policer)
    {
        SYS_ACL_DBG_DUMP("  micro policer-id %u,", pa->micro_policer_id);
    }

    if (pa->flag.macro_flow_policer)
    {
        SYS_ACL_DBG_DUMP("  macro policer-id %u,", pa->macro_policer_id);
    }

    if ((pa->flag.micro_flow_policer) +
        (pa->flag.macro_flow_policer))
    {
        SYS_ACL_DBG_DUMP("\n");
    }

    SYS_ACL_DBG_DUMP("\n");
    if (pa->flag.vlan_edit == 1)
    {
        if (pa->vlan_edit.stag_op != CTC_ACL_VLAN_TAG_OP_NONE)
        {
            SYS_ACL_DBG_DUMP("  stag opeation:  %s\n", tag_op[pa->vlan_edit.stag_op]);
            if (pa->vlan_edit.stag_op != CTC_ACL_VLAN_TAG_OP_DEL)
            {
                SYS_ACL_DBG_DUMP("  svid opeation:  %s;  new_svid:  %d\n", field_op[pa->vlan_edit.svid_sl], pa->vlan_edit.svid);
                SYS_ACL_DBG_DUMP("  scos opeation:  %s;  new_scos:  %d\n", field_op[pa->vlan_edit.scos_sl], pa->vlan_edit.scos);
                SYS_ACL_DBG_DUMP("  scfi opeation:  %s;  new_scfi:  %d\n", field_op[pa->vlan_edit.scfi_sl], pa->vlan_edit.scfi);
            }
        }
        if (pa->vlan_edit.ctag_op != CTC_ACL_VLAN_TAG_OP_NONE)
        {
            SYS_ACL_DBG_DUMP("  ctag opeation:  %s\n", tag_op[pa->vlan_edit.ctag_op]);
            if (pa->vlan_edit.ctag_op != CTC_ACL_VLAN_TAG_OP_DEL)
            {
                SYS_ACL_DBG_DUMP("  cvid opeation:  %s;  new_cvid:  %d\n", field_op[pa->vlan_edit.cvid_sl], pa->vlan_edit.cvid);
                SYS_ACL_DBG_DUMP("  ccos opeation:  %s;  new_ccos:  %d\n", field_op[pa->vlan_edit.ccos_sl], pa->vlan_edit.ccos);
                SYS_ACL_DBG_DUMP("  ccfi opeation:  %s;  new_ccfi:  %d\n", field_op[pa->vlan_edit.ccfi_sl], pa->vlan_edit.ccfi);
            }
        }
    }
    return CTC_E_NONE;
}

/*
 * show acl action
 */
STATIC int32
_sys_greatbelt_acl_show_pbr_action(uint8 lchip, sys_acl_action_t* pa)
{
    CTC_PTR_VALID_CHECK(pa);

    if (pa->flag.pbr_deny)
    {
        SYS_ACL_DBG_DUMP("  =================== deny ====================");
    }
    else
    {
        SYS_ACL_DBG_DUMP("  =================== permit ====================");
    }

    /* next line */
    SYS_ACL_DBG_DUMP("\n");

    if (pa->flag.pbr_fwd)
    {
        SYS_ACL_DBG_DUMP("  forward nhid %u,", pa->nh_id);
    }

    if (pa->flag.pbr_ecmp)
    {
        SYS_ACL_DBG_DUMP("  ecmp-nexthop,");
    }

    if ((pa->flag.pbr_fwd) +
        (pa->flag.pbr_ecmp))
    {
        SYS_ACL_DBG_DUMP("\n");
    }

    if (pa->flag.pbr_ttl_check)
    {
        SYS_ACL_DBG_DUMP("  ttl-check,");
    }

    if (pa->flag.pbr_icmp_check)
    {
        SYS_ACL_DBG_DUMP("  icmp-check,");
    }

    if (pa->flag.pbr_cpu)
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
_sys_greatbelt_acl_show_mac_entry(uint8 lchip, sys_acl_entry_t* p_entry)
{
    sys_acl_action_t * pa;
    sys_acl_mac_key_t* pk;

    CTC_PTR_VALID_CHECK(p_entry);

    pa = p_entry->action;
    pk = &p_entry->key.u0.mac_key;

    DUMP_L2_0(pk)
    DUMP_L2_1(pk)
    DUMP_MAC_UNIQUE(pk)

    CTC_ERROR_RETURN(_sys_greatbelt_acl_show_action(lchip, pa));

    return CTC_E_NONE;
}

/*
 * show ipv4 entry
 */
STATIC int32
_sys_greatbelt_acl_show_ipv4_entry(uint8 lchip, sys_acl_entry_t* p_entry)
{
    sys_acl_action_t  * pa;
    sys_acl_ipv4_key_t* pk;

    CTC_PTR_VALID_CHECK(p_entry);

    pa = p_entry->action;
    pk = &p_entry->key.u0.ipv4_key;

    DUMP_L2_0(pk)
    DUMP_L2_1(pk)
    DUMP_L3(pk)
    DUMP_IPV4_UNIQUE(pk)

    CTC_ERROR_RETURN(_sys_greatbelt_acl_show_action(lchip, pa));

    return CTC_E_NONE;
}

/*
 * show ipv6 entry
 */
STATIC int32
_sys_greatbelt_acl_show_ipv6_entry(uint8 lchip, sys_acl_entry_t* p_entry)
{
    sys_acl_action_t  * pa;
    sys_acl_ipv6_key_t* pk;

    CTC_PTR_VALID_CHECK(p_entry);

    pa = p_entry->action;
    pk = &p_entry->key.u0.ipv6_key;

    DUMP_L2_0(pk)
    DUMP_L2_1(pk)
    DUMP_L3(pk)
    DUMP_IPV6_UNIQUE(pk)

    CTC_ERROR_RETURN(_sys_greatbelt_acl_show_action(lchip, pa));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_acl_show_hash_mac_entry(uint8 lchip, sys_acl_entry_t* p_entry)
{
    sys_acl_action_t      * pa;
    sys_acl_hash_mac_key_t* pk;

    CTC_PTR_VALID_CHECK(p_entry);

    pa = p_entry->action;
    pk = &p_entry->key.u0.hash.u0.hash_mac_key;

        SYS_ACL_DBG_DUMP("  ad_index[%d] %d", lchip, pa->chip.ad_index);
    SYS_ACL_DBG_DUMP("\n");
    SYS_ACL_DBG_DUMP("  mac-da host %02x%02x.%02x%02x.%02x%02x \n",
                     pk->mac_da[0], pk->mac_da[1], pk->mac_da[2],
                     pk->mac_da[3], pk->mac_da[4], pk->mac_da[5]);
    SYS_ACL_DBG_DUMP("  eth-type 0x%x,  gport 0x%x,  cos %d\n", pk->eth_type, pk->gport, pk->cos);
    SYS_ACL_DBG_DUMP("  is-logic-port %d,  ipv4_packet %d,  vlan_id %d\n", pk->is_logic_port, pk->ipv4_packet, pk->vlan_id);


    CTC_ERROR_RETURN(_sys_greatbelt_acl_show_action(lchip, pa));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_acl_show_hash_ipv4_entry(uint8 lchip, sys_acl_entry_t* p_entry)
{
    sys_acl_action_t       * pa;
    sys_acl_hash_ipv4_key_t* pk;
    char                   ip_addr[16];
    uint32                 addr;

    CTC_PTR_VALID_CHECK(p_entry);

    pa = p_entry->action;
    pk = &p_entry->key.u0.hash.u0.hash_ipv4_key;
        SYS_ACL_DBG_DUMP("  ad_index[%d] %d", lchip, pa->chip.ad_index);

    SYS_ACL_DBG_DUMP("\n");

    SYS_ACL_DBG_DUMP("  ip-sa host");
    addr = sal_ntohl(pk->ip_sa);
    sal_inet_ntop(AF_INET, &addr, ip_addr, sizeof(ip_addr));
    SYS_ACL_DBG_DUMP(" %s", ip_addr);

    SYS_ACL_DBG_DUMP("  ip-da host");
    addr = sal_ntohl(pk->ip_da);
    sal_inet_ntop(AF_INET, &addr, ip_addr, sizeof(ip_addr));
    SYS_ACL_DBG_DUMP(" %s\n", ip_addr);

    SYS_ACL_DBG_DUMP("  l4-src-port 0x%x,  l4-dst-port 0x%x\n", pk->l4_src_port, pk->l4_dst_port);
    SYS_ACL_DBG_DUMP("  gport 0x%x,  dscp %d,  is-logic-port %d \n", pk->gport, pk->dscp, pk->is_logic_port);
    SYS_ACL_DBG_DUMP("  l4-protocol 0x%x,  ipv4-packet %d,  arp-packet %d\n", pk->l4_protocol, pk->ipv4_packet, pk->arp_packet);


    CTC_ERROR_RETURN(_sys_greatbelt_acl_show_action(lchip, pa));

    return CTC_E_NONE;
}

/*
 * show mpls entry
 */
STATIC int32
_sys_greatbelt_acl_show_mpls_entry(uint8 lchip, sys_acl_entry_t* p_entry)
{
    sys_acl_action_t  * pa;
    sys_acl_mpls_key_t* pk;

    CTC_PTR_VALID_CHECK(p_entry);

    pa = p_entry->action;
    pk = &p_entry->key.u0.mpls_key;

    DUMP_L2_0(pk)
    DUMP_MPLS_UNIQUE(pk)

    CTC_ERROR_RETURN(_sys_greatbelt_acl_show_action(lchip, pa));

    return CTC_E_NONE;
}

/*
 * show pbr ipv4 entry
 */
STATIC int32
_sys_greatbelt_acl_show_pbr_ipv4_entry(uint8 lchip, sys_acl_entry_t* p_entry)
{
    sys_acl_action_t      * pa;
    sys_acl_pbr_ipv4_key_t* pk;

    CTC_PTR_VALID_CHECK(p_entry);

    pa = p_entry->action;
    pk = &p_entry->key.u0.pbr_ipv4_key;

    DUMP_L3_PBR(pk)

    CTC_ERROR_RETURN(_sys_greatbelt_acl_show_pbr_action(lchip, pa));

    return CTC_E_NONE;
}

/*
 * show pbr ipv6 entry
 */
STATIC int32
_sys_greatbelt_acl_show_pbr_ipv6_entry(uint8 lchip, sys_acl_entry_t* p_entry)
{
    sys_acl_action_t      * pa;
    sys_acl_pbr_ipv6_key_t* pk;

    CTC_PTR_VALID_CHECK(p_entry);

    pa = p_entry->action;
    pk = &p_entry->key.u0.pbr_ipv6_key;

    DUMP_L2_0(pk)
    DUMP_PBR_L2_1(pk)
    DUMP_L3_PBR_IPV6(pk)
    DUMP_IPV6_UNIQUE(pk)

    CTC_ERROR_RETURN(_sys_greatbelt_acl_show_pbr_action(lchip, pa));

    return CTC_E_NONE;
}

/*
 * show acl entry to a specific entry with specific key type
 */
STATIC int32
_sys_greatbelt_acl_show_entry(uint8 lchip, sys_acl_entry_t* pe, uint32* p_cnt, ctc_acl_key_type_t key_type, uint8 detail)
{
    char           * type[CTC_ACL_KEY_NUM] = { "Mac ", "Ipv4", "MPLS", "Ipv6", "Hash-Mac", "Hash-Ipv4", NULL, NULL, NULL, "Pbr-ipv4", "Pbr-ipv6"};
    sys_acl_group_t* pg;
    uint8 alloc_id = 0;
    uint32 hw_index_base = 0;
    uint32 hw_index = 0;

    CTC_PTR_VALID_CHECK(pe);

    pg = pe->group;
    if(!pg)
    {
        return CTC_E_ACL_GROUP_UNEXIST;
    }

    if ((SYS_ACL_ALL_KEY != key_type)
        && (pe->key.type != key_type))
    {
        return CTC_E_NONE;
    }

    ACL_PRINT_COUNT(*p_cnt-1);
    (*p_cnt)++;

    hw_index = pe->fpae.offset_a;
    if (ACL_ENTRY_IS_TCAM(pe->key.type))
    {
        CTC_ERROR_RETURN(_sys_greatbelt_acl_get_tcam_alloc_info(lchip, pe->key.type, &(pg->group_info), &alloc_id, &hw_index_base));
        hw_index += hw_index_base;
    }

     /*if (lchip_num == lchip + CTC_MAX_LOCAL_CHIP_NUM) // on 2chips . entry prio 2chips equal*/
     /*{*/
     /*    SYS_ACL_DBG_DUMP("0x%08x   0x%08x   %-10s %-13u %-9u %-8s 0x%08x,0x%08x \n",*/
     /*                     pe->fpae.entry_id, pg->group_id, (pe->fpae.flag) ? "Y" : "N",*/
     /*                     pe->fpae.priority, pg->group_info.block_id, type[pe->key.type], pe->fpae.offset_a, pe->fpae.offset_a);*/
     /*}*/
     /*else // on 1 chip, use that lchip's prio */
     /*{*/
        SYS_ACL_DBG_DUMP("0x%08x   0x%08x   %-10s %-13u %-9u %-8s 0x%08x \n",
                         pe->fpae.entry_id, pg->group_id, (pe->fpae.flag) ? "Y" : "N",
                         pe->fpae.priority, pg->group_info.block_id, type[pe->key.type], hw_index);
     /*}*/

    if (detail)
    {
        switch (pe->key.type)
        {
        case CTC_ACL_KEY_MAC:
            _sys_greatbelt_acl_show_mac_entry(lchip, pe);
            break;

        case CTC_ACL_KEY_IPV4:
            _sys_greatbelt_acl_show_ipv4_entry(lchip, pe);
            break;

        case CTC_ACL_KEY_IPV6:
            _sys_greatbelt_acl_show_ipv6_entry(lchip, pe);
            break;

        case CTC_ACL_KEY_MPLS:
            _sys_greatbelt_acl_show_mpls_entry(lchip, pe);
            break;

        case CTC_ACL_KEY_HASH_MAC:
            _sys_greatbelt_acl_show_hash_mac_entry(lchip, pe);
            break;

        case CTC_ACL_KEY_HASH_IPV4:
            _sys_greatbelt_acl_show_hash_ipv4_entry(lchip, pe);
            break;

        case CTC_ACL_KEY_PBR_IPV4:
            _sys_greatbelt_acl_show_pbr_ipv4_entry(lchip, pe);
            break;

        case CTC_ACL_KEY_PBR_IPV6:
            _sys_greatbelt_acl_show_pbr_ipv6_entry(lchip, pe);
            break;

        default:
            break;
        }
    }

    return CTC_E_NONE;
}

/*
 * show acl entriy by entry id
 */
STATIC int32
_sys_greatbelt_acl_show_entry_by_entry_id(uint8 lchip, uint32 eid, ctc_acl_key_type_t key_type, uint8 detail)
{
    sys_acl_entry_t* pe = NULL;

    uint32         count = 1;

    SYS_ACL_LOCK(lchip);
    _sys_greatbelt_acl_get_nodes_by_eid(lchip, eid, &pe, NULL, NULL);
    if (!pe)
    {
        SYS_ACL_UNLOCK(lchip);
        return CTC_E_ACL_ENTRY_UNEXIST;
    }

     /*ACL_PRINT_ENTRY_ROW(eid);*/

    CTC_ERROR_RETURN_ACL_UNLOCK(_sys_greatbelt_acl_show_entry(lchip, pe, &count, key_type, detail));
    SYS_ACL_UNLOCK(lchip);

    return CTC_E_NONE;
}

/*
 * show acl entries in a group with specific key type
 */
STATIC int32
_sys_greatbelt_acl_show_entry_in_group(uint8 lchip, sys_acl_group_t* pg, uint32* p_cnt, ctc_acl_key_type_t key_type, uint8 detail)
{
    ctc_slistnode_t* pe;

    CTC_PTR_VALID_CHECK(pg);

    CTC_SLIST_LOOP(pg->entry_list, pe)
    {
        _sys_greatbelt_acl_show_entry(lchip, (sys_acl_entry_t *) pe, p_cnt, key_type, detail);
    }

    return CTC_E_NONE;
}

/*
 * show acl entries by group id with specific key type
 */
STATIC int32
_sys_greatbelt_acl_show_entry_by_group_id(uint8 lchip, uint32 gid, ctc_acl_key_type_t key_type, uint8 detail, uint32 *p_count)
{
    sys_acl_group_t* pg  = NULL;

    _sys_greatbelt_acl_get_group_by_gid(lchip, gid, &pg);
    SYS_ACL_CHECK_GROUP_UNEXIST(pg);

     /*ACL_PRINT_GROUP_ROW(gid);*/

    CTC_ERROR_RETURN(_sys_greatbelt_acl_show_entry_in_group(lchip, pg, p_count, key_type, detail));

    return CTC_E_NONE;
}

/*
 * show acl entries in a block with specific key type
 */
STATIC int32
_sys_greatbelt_acl_show_entry_in_block(sys_acl_block_t* pb, uint32* p_cnt, ctc_acl_key_type_t key_type, uint8 detail, uint8 lchip)
{
    ctc_fpa_entry_t* pe;
    uint16         block_idx;

    CTC_PTR_VALID_CHECK(pb);
    CTC_PTR_VALID_CHECK(p_cnt);

    for (block_idx = 0; block_idx < pb->fpab.entry_count; block_idx++)
    {
        pe = pb->fpab.entries[block_idx];

        if (pe)
        {
            CTC_ERROR_RETURN(_sys_greatbelt_acl_show_entry(lchip, ACL_OUTER_ENTRY(pe), p_cnt, key_type, detail));
        }
    }

    return CTC_E_NONE;
}

/*
 * show acl entries by group priority
 */
STATIC int32
_sys_greatbelt_acl_show_entry_by_priority(uint8 lchip, uint8 prio, ctc_acl_key_type_t key_type, uint8 detail)
{
    uint32         count = 1;
    uint8          alloc_id;
    sys_acl_block_t* pb;

    SYS_ACL_CHECK_GROUP_PRIO(prio);

    /* ACL_PRINT_PRIO_ROW(prio); */

        count = 1;
        /* ACL_PRINT_CHIP(lchip); */
        SYS_ACL_LOCK(lchip);
        for (alloc_id = 0; alloc_id < acl_master[lchip]->tcam_alloc_num; alloc_id++)
        {
            pb = &acl_master[lchip]->block[alloc_id][prio];
            if (pb->fpab.free_count != pb->fpab.entry_count) /*not empty*/
            {
                CTC_ERROR_RETURN_ACL_UNLOCK(_sys_greatbelt_acl_show_entry_in_block
                                     (pb, &count, key_type, detail, lchip));
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
_sys_greatbelt_acl_hash_traverse_cb_show_entry(sys_acl_group_t* pg, _acl_cb_para_t* cb_para)
{
    uint8 lchip = 0;
    CTC_PTR_VALID_CHECK(pg);
    CTC_PTR_VALID_CHECK(cb_para);
    lchip = pg->lchip;
    CTC_ERROR_RETURN(_sys_greatbelt_acl_show_entry_in_group(lchip, pg, &(cb_para->count), cb_para->key_type, cb_para->detail));
    return CTC_E_NONE;
}

/*
 * callback for hash traverse. traverse group hash table, show each group's group id.
 *
 */
STATIC int32
_sys_greatbelt_acl_hash_traverse_cb_show_gid(sys_acl_group_t* pg, _acl_cb_para_t* cb_para)
{
    CTC_PTR_VALID_CHECK(cb_para);
    cb_para->count++;
    SYS_ACL_DBG_DUMP("No.%03u  :  gid 0x%0X \n", cb_para->count, pg->group_id);
    return CTC_E_NONE;
}

/*
 * show all acl entries separate by priority or group
 * sort_type = 0: by priority
 * sort_type = 1: by group
 */
STATIC int32
_sys_greatbelt_acl_show_entry_all(uint8 lchip, uint8 sort_type, ctc_acl_key_type_t key_type, uint8 detail)
{
    uint8          prio;
    uint8          asic_type;
    uint32         count = 1;
    uint32         gid;
    _acl_cb_para_t para;
    sys_acl_block_t* pb;

    sal_memset(&para, 0, sizeof(_acl_cb_para_t));
    para.detail   = detail;
    para.key_type = key_type;
    para.count    = 1;

    /* ACL_PRINT_ALL_SORT_SEP_BY_ROW(sort_type); */

    SYS_ACL_LOCK(lchip);
    if (sort_type == 0) /* separate by priority */
    {
        /* tcam entry by priority */
            count = 1;
            /* ACL_PRINT_CHIP(lchip); */
            for (prio = 0; prio < SYS_ACL_BLOCK_NUM_MAX; prio++)
            {
                for (asic_type = 0; asic_type < acl_master[lchip]->tcam_alloc_num; asic_type++)
                {
                    pb = &acl_master[lchip]->block[asic_type][prio];
                    CTC_ERROR_RETURN_ACL_UNLOCK(_sys_greatbelt_acl_show_entry_in_block
                                         (pb, &count, key_type, detail, lchip));
                }
            }

        for (gid = CTC_ACL_GROUP_ID_HASH_MAC; gid < (CTC_ACL_GROUP_ID_HASH_IPV4+1); gid++)
        {
            CTC_ERROR_RETURN_ACL_UNLOCK(_sys_greatbelt_acl_show_entry_by_group_id(lchip, gid, key_type, detail,&count));
        }

        /* hash entry by group */
    }
    else /* separate by group */
    {
        ctc_hash_traverse_through(acl_master[lchip]->group,
                                  (hash_traversal_fn) _sys_greatbelt_acl_hash_traverse_cb_show_entry, &para);
    }
    SYS_ACL_UNLOCK(lchip);

    return CTC_E_NONE;
}

int32
sys_greatbelt_acl_show_status(uint8 lchip)
{
    uint8          idx;
    uint8          mac_type;
    uint8          ipv4_type;
    uint8          ipv6_type;
    uint8          pbr_ipv4_type;
    uint8          pbr_ipv6_type;
    uint8          egs_ipv4_type;
    uint8          egs_ipv6_type;

    uint16         block_idx = 0;
    uint8          prio      = 0;
    uint8          asic_type = 0;
    sys_acl_block_t* pb      = NULL;
    ctc_fpa_entry_t* pe      = NULL;

    uint16         mac_cnt       = 0;
    uint16         ipv4_cnt      = 0;
    uint16         ipv6_cnt      = 0;
    uint16         pbr_ipv4_cnt  = 0;
    uint16         pbr_ipv6_cnt  = 0;
    uint16         mpls_cnt      = 0;
    uint16         hash_mac_cnt  = 0;
    uint16         hash_ipv4_cnt = 0;

    sys_acl_group_t* pg;

    _acl_cb_para_t para;
    SYS_ACL_INIT_CHECK(lchip);

    mac_type      = acl_master[lchip]->alloc_id[CTC_ACL_KEY_MAC];
    ipv4_type     = acl_master[lchip]->alloc_id[CTC_ACL_KEY_IPV4];
    ipv6_type     = acl_master[lchip]->alloc_id[CTC_ACL_KEY_IPV6];
    pbr_ipv4_type = acl_master[lchip]->alloc_id[CTC_ACL_KEY_PBR_IPV4];
    pbr_ipv6_type = acl_master[lchip]->alloc_id[CTC_ACL_KEY_PBR_IPV6];
    egs_ipv4_type = acl_master[lchip]->egs_ipv4_alloc_id;
    egs_ipv6_type = acl_master[lchip]->egs_ipv6_alloc_id;

    SYS_ACL_DBG_DUMP("================= ACL Overall Status ==================\n");

    SYS_ACL_DBG_DUMP("\n");
    SYS_ACL_DBG_DUMP("#0 Group Status\n");
    SYS_ACL_DBG_DUMP("-------------------------------------------------------\n");

    sal_memset(&para, 0, sizeof(_acl_cb_para_t));
    para.count = 0;
    SYS_ACL_LOCK(lchip);
    ctc_hash_traverse_through(acl_master[lchip]->group,
                              (hash_traversal_fn) _sys_greatbelt_acl_hash_traverse_cb_show_gid, &para);
    SYS_ACL_UNLOCK(lchip);

    SYS_ACL_DBG_DUMP("Totol group count :%u \n", para.count);

        SYS_ACL_DBG_DUMP("\n");
        SYS_ACL_DBG_DUMP("#1 Priority Status \n");
        SYS_ACL_DBG_DUMP("-------------------------------------------------------\n");
        if (mac_type != ipv4_type)
        {
            SYS_ACL_DBG_DUMP("%-12s  %-12s  %-12s  %-12s  %-12s  %-12s\n", "Prio\\Type", "Mac", "Ipv4/MPLS", "Ipv6", "PBR_Ipv4", "PBR_Ipv6");
            /* ACL_PRINT_CHIP(lchip); */
            SYS_ACL_LOCK(lchip);
            for (idx = 0; idx < 4; idx++)
            {
                SYS_ACL_DBG_DUMP("%-4s %d  %6d of %d  %6d of %d  %6d of %d  %6d of %d  %6d of %d\n",
                                 "Prio", idx,
                                 acl_master[lchip]->block[mac_type][idx].fpab.entry_count - acl_master[lchip]->block[mac_type][idx].fpab.free_count,
                                 acl_master[lchip]->block[mac_type][idx].fpab.entry_count,
                                 acl_master[lchip]->block[ipv4_type][idx].fpab.entry_count - acl_master[lchip]->block[ipv4_type][idx].fpab.free_count,
                                 acl_master[lchip]->block[ipv4_type][idx].fpab.entry_count,
                                 acl_master[lchip]->block[ipv6_type][idx].fpab.entry_count - acl_master[lchip]->block[ipv6_type][idx].fpab.free_count,
                                 acl_master[lchip]->block[ipv6_type][idx].fpab.entry_count,
                                 acl_master[lchip]->block[pbr_ipv4_type][idx].fpab.entry_count - acl_master[lchip]->block[pbr_ipv4_type][idx].fpab.free_count,
                                 acl_master[lchip]->block[pbr_ipv4_type][idx].fpab.entry_count,
                                 acl_master[lchip]->block[pbr_ipv6_type][idx].fpab.entry_count - acl_master[lchip]->block[pbr_ipv6_type][idx].fpab.free_count,
                                 acl_master[lchip]->block[pbr_ipv6_type][idx].fpab.entry_count);
            }
            SYS_ACL_UNLOCK(lchip);
        }
        else
        {
            SYS_ACL_DBG_DUMP("%-12s  %-12s       %-12s  %-12s    %-12s  %-12s  %-12s\n", "Prio\\Type", "Mac/Ipv4/MPLS", "Ipv6", "PBR_Ipv4", "PBR_Ipv6",
                "Mac/Ipv4/MPLS EGS",  "Ipv6 EGS");

            SYS_ACL_LOCK(lchip);
            for (idx = 0; idx < 4; idx++)
            {
                SYS_ACL_DBG_DUMP("%-4s %d      %6d of %-5d %6d of %-5d %6d of %-5d %6d of %-5d %6d of %-5d %6d of %-5d\n",
                                 "Prio", idx,
                                 acl_master[lchip]->block[mac_type][idx].fpab.entry_count - acl_master[lchip]->block[mac_type][idx].fpab.free_count,
                                 acl_master[lchip]->block[mac_type][idx].fpab.entry_count,
                                 acl_master[lchip]->block[ipv6_type][idx].fpab.entry_count - acl_master[lchip]->block[ipv6_type][idx].fpab.free_count,
                                 acl_master[lchip]->block[ipv6_type][idx].fpab.entry_count,
                                 acl_master[lchip]->block[pbr_ipv4_type][idx].fpab.entry_count - acl_master[lchip]->block[pbr_ipv4_type][idx].fpab.free_count,
                                 acl_master[lchip]->block[pbr_ipv4_type][idx].fpab.entry_count,
                                 acl_master[lchip]->block[pbr_ipv6_type][idx].fpab.entry_count - acl_master[lchip]->block[pbr_ipv6_type][idx].fpab.free_count,
                                 acl_master[lchip]->block[pbr_ipv6_type][idx].fpab.entry_count,
                                 acl_master[lchip]->block[egs_ipv4_type][idx].fpab.entry_count - acl_master[lchip]->block[egs_ipv4_type][idx].fpab.free_count,
                                 acl_master[lchip]->block[egs_ipv4_type][idx].fpab.entry_count,
                                 acl_master[lchip]->block[egs_ipv6_type][idx].fpab.entry_count - acl_master[lchip]->block[egs_ipv6_type][idx].fpab.free_count,
                                 acl_master[lchip]->block[egs_ipv6_type][idx].fpab.entry_count);
            }
            SYS_ACL_UNLOCK(lchip);
        }

        SYS_ACL_DBG_DUMP("\n");
        SYS_ACL_DBG_DUMP("#2 Tcam Entry Status :\n");
        SYS_ACL_DBG_DUMP("-------------------------------------------------------\n");
        SYS_ACL_LOCK(lchip);
        for (prio = 0; prio < SYS_ACL_BLOCK_NUM_MAX; prio++)
        {
            for (asic_type = 0; asic_type < acl_master[lchip]->tcam_alloc_num; asic_type++)
            {
                pb = &acl_master[lchip]->block[asic_type][prio];

                for (block_idx = 0; block_idx < pb->fpab.entry_count; block_idx++)
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

                        case CTC_ACL_KEY_MPLS:
                            mpls_cnt++;
                            break;

                        case CTC_ACL_KEY_PBR_IPV4:
                            pbr_ipv4_cnt++;
                            break;

                        case CTC_ACL_KEY_PBR_IPV6:
                            pbr_ipv6_cnt++;
                            break;

                        case CTC_ACL_KEY_HASH_MAC: /* won't hit, juts shut warning */
                        case CTC_ACL_KEY_HASH_IPV4:
                        case CTC_ACL_KEY_NUM:
                            break;
                        default:
                            break;
                        }
                    }
                }
            }
        }
        SYS_ACL_UNLOCK(lchip);

        SYS_ACL_DBG_DUMP("%-9s entries: %u \n", "Mac", mac_cnt);
        SYS_ACL_DBG_DUMP("%-9s entries: %u \n", "MPLS", mpls_cnt);
        SYS_ACL_DBG_DUMP("%-9s entries: %u \n", "Ipv4", ipv4_cnt);
        SYS_ACL_DBG_DUMP("%-9s entries: %u \n", "Ipv6", ipv6_cnt);
        SYS_ACL_DBG_DUMP("%-9s entries: %u \n", "pbr_Ipv4", pbr_ipv4_cnt);
        SYS_ACL_DBG_DUMP("%-9s entries: %u \n", "pbr_Ipv6", pbr_ipv6_cnt);

        SYS_ACL_DBG_DUMP("\n");
        SYS_ACL_DBG_DUMP("#3 Hash Entry Status :\n");
        SYS_ACL_DBG_DUMP("-------------------------------------------------------\n");

        SYS_ACL_LOCK(lchip);
        _sys_greatbelt_acl_get_group_by_gid(lchip, CTC_ACL_GROUP_ID_HASH_MAC, &pg);
        hash_mac_cnt = pg->entry_count;

        _sys_greatbelt_acl_get_group_by_gid(lchip, CTC_ACL_GROUP_ID_HASH_IPV4, &pg);
        hash_ipv4_cnt = pg->entry_count;
        SYS_ACL_UNLOCK(lchip);

        SYS_ACL_DBG_DUMP("%-9s entries: %u \n", "Hash-Mac", hash_mac_cnt);
        SYS_ACL_DBG_DUMP("%-9s entries: %u \n", "Hash-Ipv4", hash_ipv4_cnt);

    return CTC_E_NONE;
}

/** show acl entry **
 * type = 0 :by all
 * type = 1 :by entry
 * type = 2 :by group
 * type = 3 :by priority
 */
int32
sys_greatbelt_acl_show_entry(uint8 lchip, uint8 type, uint32 param, ctc_acl_key_type_t key_type, uint8 detail)
{
    /* CTC_ACL_KEY_NUM represents all type*/
    uint32 count = 1;
    SYS_ACL_INIT_CHECK(lchip);
    CTC_MAX_VALUE_CHECK(type, CTC_ACL_KEY_NUM);

    SYS_ACL_DBG_DUMP("No.     ENTRY_ID     GROUP_ID     HW     ENTRY_PRI     GROUP_PRI     TYPE     INDEX\n");
    SYS_ACL_DBG_DUMP("-----------------------------------------------------------------------------------\n");

    switch (type)
    {
    case 0:
        CTC_ERROR_RETURN(_sys_greatbelt_acl_show_entry_all(lchip, param, key_type, detail));
        SYS_ACL_DBG_DUMP("\n");
        break;

    case 1:
        CTC_ERROR_RETURN(_sys_greatbelt_acl_show_entry_by_entry_id(lchip, param, key_type, detail));
        SYS_ACL_DBG_DUMP("\n");
        break;

    case 2:
        SYS_ACL_LOCK(lchip);
        CTC_ERROR_RETURN_ACL_UNLOCK(_sys_greatbelt_acl_show_entry_by_group_id(lchip, param, key_type, detail,&count));
        SYS_ACL_UNLOCK(lchip);
        SYS_ACL_DBG_DUMP("\n");
        break;

    case 3:
        CTC_ERROR_RETURN(_sys_greatbelt_acl_show_entry_by_priority(lchip, param, key_type, detail));
        SYS_ACL_DBG_DUMP("\n");
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

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
sys_greatbelt_acl_create_group(uint8 lchip, uint32 group_id, ctc_acl_group_info_t* group_info)
{
    sys_acl_group_t* pg     = NULL;
    int32          ret;

    SYS_ACL_INIT_CHECK(lchip);
    SYS_ACL_CHECK_GROUP_ID(group_id);
    SYS_ACL_CHECK_RSV_GROUP_ID(group_id); /* reserve group cannot be created by user */

    SYS_ACL_DBG_FUNC();
    SYS_ACL_DBG_PARAM("  %% PARAM: group_id: %u \n", group_id);

    /*
     *  group_id is uint32.
     *  #1 check block_num from p_acl_master. precedence cannot bigger than block_num.
     *  #2 malloc a sys_acl_group_t, add to hash based on group_id.
     */

    SYS_ACL_LOCK(lchip);
    /* check if group exist */
    _sys_greatbelt_acl_get_group_by_gid(lchip, group_id, &pg);
    if (pg)
    {
        SYS_ACL_UNLOCK(lchip);
        return CTC_E_ACL_GROUP_EXIST;
    }

    CTC_ERROR_GOTO(_sys_greatbelt_acl_create_group(lchip, group_id, group_info), ret, cleanup);

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
sys_greatbelt_acl_destroy_group(uint8 lchip, uint32 group_id)
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

    SYS_ACL_LOCK(lchip);
    /* check if group exist */
    _sys_greatbelt_acl_get_group_by_gid(lchip, group_id, &pg);
    SYS_ACL_CHECK_GROUP_UNEXIST(pg);

    /* check if all entry removed */
    if (0 != pg->entry_list->count)
    {
        SYS_ACL_UNLOCK(lchip);
        return CTC_E_ACL_GROUP_NOT_EMPTY;
    }

    ctc_hash_remove(acl_master[lchip]->group, pg);
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
sys_greatbelt_acl_install_group(uint8 lchip, uint32 group_id, ctc_acl_group_info_t* group_info)
{
    sys_acl_group_t       * pg        = NULL;
    uint8                 install_all = 0;
    ctc_slistnode_t* pe;

    uint32                eid = 0;

    SYS_ACL_INIT_CHECK(lchip);
    SYS_ACL_CHECK_GROUP_ID(group_id);

    SYS_ACL_DBG_FUNC();
    SYS_ACL_DBG_PARAM("  %% PARAM: gid %u \n", group_id);

    SYS_ACL_LOCK(lchip);
    /* get group node */
    _sys_greatbelt_acl_get_group_by_gid(lchip, group_id, &pg);
    SYS_ACL_CHECK_GROUP_UNEXIST(pg);

    if (group_info) /* group_info != NULL*/
    {
        bool is_new;

        /* if group_info is new, rewrite all entries */
        is_new = _sys_greatbelt_acl_is_group_info_new(lchip, &pg->group_info, group_info);
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
        CTC_ERROR_RETURN_ACL_UNLOCK(_sys_greatbelt_acl_check_group_info(lchip, group_info, pg->group_info.type, 0));

        CTC_ERROR_RETURN_ACL_UNLOCK(_sys_greatbelt_acl_map_group_info(lchip, &pg->group_info, group_info, 0));

        CTC_SLIST_LOOP(pg->entry_list, pe)
        {
            eid = ((sys_acl_entry_t *) pe)->fpae.entry_id;
            _sys_greatbelt_acl_install_entry(lchip, eid, SYS_ACL_ENTRY_OP_FLAG_ADD, 0);
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
                _sys_greatbelt_acl_install_entry(lchip, eid, SYS_ACL_ENTRY_OP_FLAG_ADD, 0);
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
sys_greatbelt_acl_uninstall_group(uint8 lchip, uint32 group_id)
{
    sys_acl_group_t       * pg = NULL;
    struct ctc_slistnode_s* pe = NULL;

    uint32                eid = 0;

    SYS_ACL_INIT_CHECK(lchip);
    SYS_ACL_CHECK_GROUP_ID(group_id);

    SYS_ACL_DBG_FUNC();
    SYS_ACL_DBG_PARAM("  %% PARAM: gid %u \n", group_id);

    SYS_ACL_LOCK(lchip);
    /* get group node */
    _sys_greatbelt_acl_get_group_by_gid(lchip, group_id, &pg);
    SYS_ACL_CHECK_GROUP_UNEXIST(pg);

    CTC_SLIST_LOOP(pg->entry_list, pe)
    {
        eid = ((sys_acl_entry_t *) pe)->fpae.entry_id;
        CTC_ERROR_RETURN_ACL_UNLOCK(_sys_greatbelt_acl_install_entry(lchip, eid, SYS_ACL_ENTRY_OP_FLAG_DELETE, 0));
    }
    SYS_ACL_UNLOCK(lchip);

    return CTC_E_NONE;
}

/*
 * get group info by group id. NOT For hash group.
 */
int32
sys_greatbelt_acl_get_group_info(uint8 lchip, uint32 group_id, ctc_acl_group_info_t* group_info)
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
    _sys_greatbelt_acl_get_group_by_gid(lchip, group_id, &pg);
    SYS_ACL_CHECK_GROUP_UNEXIST(pg);

    pinfo = &(pg->group_info);

    /* get ctc group info based on pinfo (sys group info) */
    CTC_ERROR_RETURN_ACL_UNLOCK(_sys_greatbelt_acl_unmap_group_info(lchip, group_info, pinfo));
    SYS_ACL_UNLOCK(lchip);

    return CTC_E_NONE;
}

#define _2_ACL_ENTRY_

/*
 * install entry to hardware table
 */
int32
sys_greatbelt_acl_install_entry(uint8 lchip, uint32 eid)
{
    SYS_ACL_INIT_CHECK(lchip);
    SYS_ACL_CHECK_ENTRY_ID(eid);

    SYS_ACL_DBG_FUNC();
    SYS_ACL_DBG_PARAM("  %% PARAM: eid %u \n", eid);

    SYS_ACL_LOCK(lchip);
    CTC_ERROR_RETURN_ACL_UNLOCK(_sys_greatbelt_acl_install_entry(lchip, eid, SYS_ACL_ENTRY_OP_FLAG_ADD, 1));
    SYS_ACL_UNLOCK(lchip);

    return CTC_E_NONE;
}

/*
 * uninstall entry from hardware table
 */
int32
sys_greatbelt_acl_uninstall_entry(uint8 lchip, uint32 eid)
{
    SYS_ACL_INIT_CHECK(lchip);
    SYS_ACL_CHECK_ENTRY_ID(eid);

    SYS_ACL_DBG_FUNC();
    SYS_ACL_DBG_PARAM("  %% PARAM: eid %u \n", eid);

    SYS_ACL_LOCK(lchip);
    CTC_ERROR_RETURN_ACL_UNLOCK(_sys_greatbelt_acl_install_entry(lchip, eid, SYS_ACL_ENTRY_OP_FLAG_DELETE, 1));
    SYS_ACL_UNLOCK(lchip);

    return CTC_E_NONE;
}

/*
 * check entry type if it is valid.
 */
STATIC int32
_sys_greatbelt_acl_check_entry_type(uint8 lchip, uint32 group_id, sys_acl_group_info_t* group_info, uint8 entry_type)
{
    uint8 idx = 0;

    switch (entry_type)
    {
    case CTC_ACL_KEY_PBR_IPV4:
    case CTC_ACL_KEY_PBR_IPV6:
        if ((CTC_ACL_GROUP_TYPE_PBR_CLASS != group_info->type) && (CTC_ACL_GROUP_TYPE_NONE != group_info->type))
        {
            return CTC_E_ACLQOS_INVALID_KEY_TYPE;
        }
        break;

    case CTC_ACL_KEY_MAC:
    case CTC_ACL_KEY_IPV4:
    case CTC_ACL_KEY_MPLS:
    case CTC_ACL_KEY_IPV6:
        if ((CTC_ACL_GROUP_TYPE_PBR_CLASS == group_info->type) || (CTC_ACL_GROUP_TYPE_HASH == group_info->type))
        {
            return CTC_E_ACLQOS_INVALID_KEY_TYPE;
        }
        else if ((CTC_ACL_GROUP_TYPE_PORT_BITMAP == group_info->type) && (CTC_ACL_KEY_MPLS == entry_type))
        {
            /* port_bitmap only allow to use 52 bits use mpls key, others is 56 bits */
            for (idx = 1; idx < CTC_PORT_BITMAP_IN_WORD; idx++)
            {
                if ((1 == idx) && ((group_info->un.port_bitmap[1]) >> 20))
                {
                    return CTC_E_ACL_PORT_BITMAP_OVERFLOW;
                }
                else if ((1 < idx) && (group_info->un.port_bitmap[idx]))
                {
                    return CTC_E_ACL_PORT_BITMAP_OVERFLOW;
                }
            }
        }
        break;

    case CTC_ACL_KEY_HASH_MAC:
        if (CTC_ACL_GROUP_ID_HASH_MAC != group_id)
        {
            return CTC_E_ACLQOS_INVALID_KEY_TYPE;
        }
        break;

    case CTC_ACL_KEY_HASH_IPV4:
        if (CTC_ACL_GROUP_ID_HASH_IPV4 != group_id)
        {
            return CTC_E_ACLQOS_INVALID_KEY_TYPE;
        }
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_acl_check_port_field_type(uint8 lchip, sys_acl_group_t * pg, ctc_acl_entry_t* acl_entry, uint8 entry_type)
{
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

    return CTC_E_NONE;
}

int32
sys_greatbelt_acl_add_entry(uint8 lchip, uint32 group_id, ctc_acl_entry_t* acl_entry)
{
    sys_acl_group_t  * pg      = NULL;
    sys_acl_entry_t  * pe_lkup = NULL;

    sys_acl_entry_t*  e_tmp = NULL;
    sys_acl_action_t a_tmp;
    int32            ret;

    SYS_ACL_INIT_CHECK(lchip);
    SYS_ACL_CHECK_GROUP_ID(group_id);
    CTC_PTR_VALID_CHECK(acl_entry);
    SYS_ACL_CHECK_ENTRY_ID(acl_entry->entry_id);

    SYS_ACL_DBG_FUNC();
    SYS_ACL_DBG_PARAM("  %% PARAM: eid %u -- key_type %u\n",
                      acl_entry->entry_id, acl_entry->key.type);


    SYS_ACL_LOCK(lchip);
    /* check sys group */
    _sys_greatbelt_acl_get_group_by_gid(lchip, group_id, &pg);
    SYS_ACL_CHECK_GROUP_UNEXIST(pg);
    CTC_ERROR_RETURN_ACL_UNLOCK(_sys_greatbelt_acl_check_entry_type(lchip, group_id, &(pg->group_info), acl_entry->key.type));

    CTC_ERROR_RETURN_ACL_UNLOCK(_sys_greatbelt_acl_check_port_field_type(lchip, pg, acl_entry, acl_entry->key.type));

    /* check sys entry */
    _sys_greatbelt_acl_get_nodes_by_eid(lchip, acl_entry->entry_id, &pe_lkup, NULL, NULL);
    if (pe_lkup)
    {
        SYS_ACL_UNLOCK(lchip);
        return CTC_E_ACL_ENTRY_EXIST;
    }

    /* check if support stats */
    SYS_ACL_CHECK_SUPPORT_STATS(acl_entry->action.flag, pg->group_info.block_id);

    e_tmp = mem_malloc(MEM_ACL_MODULE, sizeof(sys_acl_entry_t));
    if (e_tmp == NULL)
    {
        SYS_ACL_UNLOCK(lchip);
        return CTC_E_NO_MEMORY;
    }

    sal_memset(e_tmp, 0, sizeof(sys_acl_entry_t));
    sal_memset(&a_tmp, 0, sizeof(a_tmp));
    a_tmp.lchip = lchip;
    e_tmp->fpae.lchip = lchip;
    /* build sys entry action, key*/
    CTC_ERROR_GOTO(_sys_greatbelt_acl_map_key(lchip, &acl_entry->key, &e_tmp->key), ret, error);

    e_tmp->group = pg; /* map action need this */
    CTC_ERROR_GOTO(_sys_greatbelt_acl_map_stats_action(lchip, e_tmp, &acl_entry->action, &a_tmp, 0, NULL), ret, error);
    CTC_ERROR_GOTO(_sys_greatbelt_acl_map_action(lchip, pg->group_info.dir, e_tmp, &acl_entry->action, &a_tmp), ret, error);

    /* build sys entry inner field */
    e_tmp->fpae.entry_id    = acl_entry->entry_id;
    e_tmp->head.next        = NULL;
    if (acl_entry->priority_valid)
    {
        e_tmp->fpae.priority = acl_entry->priority; /* meaningless for HASH */
    }
    else
    {
        e_tmp->fpae.priority = FPA_PRIORITY_DEFAULT; /* meaningless for HASH */
    }

    /* set lchip --> key and action */

    if (!ACL_ENTRY_IS_TCAM(acl_entry->key.type)) /* hash entry */
    {
        /* hash check +1*/
        if (!SYS_ACL_IS_RSV_GROUP(pg->group_id)) /* hash entry only in hash group */
        {
            ret = CTC_E_ACL_ADD_HASH_ENTRY_TO_WRONG_GROUP;
            goto error;
        }

        /* hash check +2. lchip for linkagg is 0. */
        _sys_greatbelt_acl_get_nodes_by_key(lchip, e_tmp, &pe_lkup, NULL);
        if (pe_lkup) /* hash's key must be unique */
        {
            ret =  CTC_E_ACL_ENTRY_EXIST;
            goto error;
        }
    }
    else /* tcam entry */
    {
        /* tcam check +1 */
        if (SYS_ACL_IS_RSV_GROUP(pg->group_id)) /* tcam entry only in tcam group */
        {
            ret = CTC_E_ACL_ADD_TCAM_ENTRY_TO_WRONG_GROUP;
            goto error;
        }
    }

    CTC_ERROR_GOTO(_sys_greatbelt_acl_add_entry(lchip, pg, e_tmp, &a_tmp), ret, cleanup);
    SYS_ACL_UNLOCK(lchip);
    mem_free(e_tmp);

    return CTC_E_NONE;

 cleanup:
    _sys_greatbelt_acl_remove_accessory_property(lchip, e_tmp, &a_tmp);

 error:
    SYS_ACL_UNLOCK(lchip);
    mem_free(e_tmp);
    return ret;
}

/*
 * remove entry from software table
 */
int32
sys_greatbelt_acl_remove_entry(uint8 lchip, uint32 entry_id)
{
    SYS_ACL_INIT_CHECK(lchip);
    SYS_ACL_CHECK_ENTRY_ID(entry_id);

    SYS_ACL_DBG_FUNC();
    SYS_ACL_DBG_PARAM("  %% PARAM: eid %u \n", entry_id);

    SYS_ACL_LOCK(lchip);
    CTC_ERROR_RETURN_ACL_UNLOCK(_sys_greatbelt_acl_remove_entry(lchip, entry_id));
    SYS_ACL_UNLOCK(lchip);

    return CTC_E_NONE;
}

/*
 * remove all entries from a group
 */
int32
sys_greatbelt_acl_remove_all_entry(uint8 lchip, uint32 group_id)
{
    sys_acl_group_t       * pg      = NULL;
    struct ctc_slistnode_s* pe      = NULL;
    struct ctc_slistnode_s* pe_next = NULL;
    uint32                eid       = 0;

    SYS_ACL_INIT_CHECK(lchip);
    SYS_ACL_CHECK_GROUP_ID(group_id);

    SYS_ACL_DBG_FUNC();
    SYS_ACL_DBG_PARAM("  %% PARAM: gid %u \n", group_id);

    SYS_ACL_LOCK(lchip);
    /* get group node */
    _sys_greatbelt_acl_get_group_by_gid(lchip, group_id, &pg);
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
        _sys_greatbelt_acl_remove_entry(lchip, eid);
    }
    SYS_ACL_UNLOCK(lchip);

    return CTC_E_NONE;
}



/*
 * set priority of entry
 */
int32
_sys_greatbelt_acl_set_entry_priority(uint8 lchip, uint32 entry_id, uint32 priority)
{
    sys_acl_entry_t* pe = NULL;
    sys_acl_group_t* pg = NULL;
    sys_acl_block_t* pb = NULL;

    /* get sys entry */
    _sys_greatbelt_acl_get_nodes_by_eid(lchip, entry_id, &pe, &pg, &pb);
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

    CTC_ERROR_RETURN(fpa_greatbelt_set_entry_prio(acl_master[lchip]->fpa, &pe->fpae, &pb->fpab, priority));

    return CTC_E_NONE;
}

int32
sys_greatbelt_acl_set_entry_priority(uint8 lchip, uint32 entry_id, uint32 priority)
{
    SYS_ACL_INIT_CHECK(lchip);
    SYS_ACL_CHECK_ENTRY_ID(entry_id);
    SYS_ACL_CHECK_ENTRY_PRIO(priority);

    SYS_ACL_DBG_FUNC();
    SYS_ACL_DBG_PARAM("  %% PARAM: eid %u -- prio %u \n", entry_id, priority);

    SYS_ACL_LOCK(lchip);
    CTC_ERROR_RETURN_ACL_UNLOCK(_sys_greatbelt_acl_set_entry_priority(lchip, entry_id, priority));
    SYS_ACL_UNLOCK(lchip);

    return CTC_E_NONE;
}

/*
 * get multiple entries
 */
int32
sys_greatbelt_acl_get_multi_entry(uint8 lchip, ctc_acl_query_t* query)
{
    uint32                entry_index = 0;
    sys_acl_group_t       * pg        = NULL;
    struct ctc_slistnode_s* pe        = NULL;

    SYS_ACL_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(query);
    SYS_ACL_CHECK_GROUP_ID(query->group_id);

    SYS_ACL_DBG_FUNC();
    SYS_ACL_DBG_PARAM("  %% PARAM: gid %u -- entry_sz %u\n", query->group_id, query->entry_size);

    SYS_ACL_LOCK(lchip);
    /* get group node */
    _sys_greatbelt_acl_get_group_by_gid(lchip, query->group_id, &pg);
    SYS_ACL_CHECK_GROUP_UNEXIST(pg);

    if (query->entry_size == 0)
    {
        query->entry_count = pg->entry_count;
    }
    else
    {
        uint32* p_array = query->entry_array;
        if(NULL == p_array)
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

/* update acl action*/
int32
sys_greatbelt_acl_update_action(uint8 lchip, uint32 entry_id, ctc_acl_action_t* action)
{
    sys_acl_entry_t  * pe_old = NULL;
    sys_acl_group_t  * pg     = NULL;
    sys_acl_action_t new_action;

    SYS_ACL_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(action);
    SYS_ACL_DBG_FUNC();

    SYS_ACL_LOCK(lchip);
    /* lookup */
    _sys_greatbelt_acl_get_nodes_by_eid(lchip, entry_id, &pe_old, &pg, NULL);
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

    /* check if support stats */
    SYS_ACL_CHECK_SUPPORT_STATS(action->flag, pe_old->group->group_info.block_id);

    CTC_ERROR_RETURN_ACL_UNLOCK(_sys_greatbelt_acl_map_stats_action(lchip, pe_old, action, &new_action, 1, pe_old->action));

    CTC_ERROR_RETURN_ACL_UNLOCK(_sys_greatbelt_acl_map_action(lchip, pg->group_info.dir, pe_old, action, &new_action));

    if (ACL_ENTRY_IS_TCAM(pe_old->key.type))
    {
        sal_memcpy(pe_old->action, &new_action, sizeof(sys_acl_action_t));
    }
    else
    {
        sys_acl_action_t* pa_get;

        CTC_ERROR_RETURN_ACL_UNLOCK(_sys_greatbelt_acl_hash_ad_spool_add(lchip, pe_old, &new_action, &pa_get));
        CTC_ERROR_RETURN_ACL_UNLOCK(_sys_greatbelt_acl_hash_ad_spool_remove(lchip, pe_old, pe_old->action));
        /* update action pointer */
        pe_old->action = pa_get;
    }

    if (pe_old->fpae.flag == FPA_ENTRY_FLAG_INSTALLED)
    {
            CTC_ERROR_RETURN_ACL_UNLOCK(_sys_greatbelt_acl_add_hw(lchip, entry_id));
    }
    SYS_ACL_UNLOCK(lchip);

    return CTC_E_NONE;
}

int32
sys_greatbelt_acl_copy_entry(uint8 lchip, ctc_acl_copy_entry_t* copy_entry)
{
    sys_acl_entry_t * pe_src = NULL;

    sys_acl_entry_t * pe_dst    = NULL;
    sys_acl_action_t* pa_dst    = NULL;
    sys_acl_block_t * pb_dst    = NULL;
    sys_acl_group_t * pg_dst    = NULL;
    int32           block_index = 0;
    uint8           asic_type   = 0;
    int32           ret         = 0;
    uint8           index;

    SYS_ACL_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(copy_entry);
    SYS_ACL_CHECK_ENTRY_ID(copy_entry->entry_id);
    SYS_ACL_DBG_FUNC();

    SYS_ACL_LOCK(lchip);
    /* check src entry */
    _sys_greatbelt_acl_get_nodes_by_eid(lchip, copy_entry->src_entry_id, &pe_src, NULL, NULL);
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
    _sys_greatbelt_acl_get_nodes_by_eid(lchip, copy_entry->dst_entry_id, &pe_dst, NULL, NULL);
    if (pe_dst)
    {
        SYS_ACL_UNLOCK(lchip);
        return CTC_E_ACL_ENTRY_EXIST;
    }

    _sys_greatbelt_acl_get_group_by_gid(lchip, copy_entry->dst_group_id, &pg_dst);
    SYS_ACL_CHECK_GROUP_UNEXIST(pg_dst);

    /* check if support stats */
    if ((!CTC_IS_BIT_SET(acl_master[lchip]->acl_register.priority_bitmap_of_stats, pg_dst->group_info.block_id))
        && (pe_src->action->flag.stats))
    {
        SYS_ACL_UNLOCK(lchip);
        return CTC_E_ACL_STATS_NOT_SUPPORT;
    }

    MALLOC_ZERO(MEM_ACL_MODULE, pe_dst, acl_master[lchip]->entry_size[pe_src->key.type]);
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

    sal_memcpy(pe_dst, pe_src, acl_master[lchip]->entry_size[pe_src->key.type]);
    sal_memcpy(pa_dst, pe_src->action, sizeof(sys_acl_action_t));
    pe_dst->action = pa_dst;

    /* flag count ++ */
    switch (pe_dst->key.type)
    {
    case CTC_ACL_KEY_MAC:
    case CTC_ACL_KEY_MPLS:
    case CTC_ACL_KEY_HASH_MAC:
    case CTC_ACL_KEY_HASH_IPV4:
        break;

    case CTC_ACL_KEY_IPV4:
        if (pe_dst->key.u0.ipv4_key.flag.tcp_flags)
        {
            index = pe_dst->key.u0.ipv4_key.tcp_flags_idx;
            acl_master[lchip]->tcp_flag[index].ref++;
        }

        if (pe_dst->key.u0.ipv4_key.flag.l4_src_port_range)
        {
            index = pe_dst->key.u0.ipv4_key.l4_src_port_range_idx;
            acl_master[lchip]->l4_port[index].ref++;
        }

        if (pe_dst->key.u0.ipv4_key.flag.l4_dst_port_range)
        {
            index = pe_dst->key.u0.ipv4_key.l4_dst_port_range_idx;
            acl_master[lchip]->l4_port[index].ref++;
        }

        break;

    case CTC_ACL_KEY_IPV6:
        if (pe_dst->key.u0.ipv6_key.flag.tcp_flags)
        {
            index = pe_dst->key.u0.ipv6_key.tcp_flags_idx;
            acl_master[lchip]->tcp_flag[index].ref++;
        }

        if (pe_dst->key.u0.ipv6_key.flag.l4_src_port_range)
        {
            index = pe_dst->key.u0.ipv6_key.l4_src_port_range_idx;
            acl_master[lchip]->l4_port[index].ref++;
        }

        if (pe_dst->key.u0.ipv6_key.flag.l4_dst_port_range)
        {
            index = pe_dst->key.u0.ipv6_key.l4_dst_port_range_idx;
            acl_master[lchip]->l4_port[index].ref++;
        }

        break;

    case CTC_ACL_KEY_PBR_IPV4:
        if (pe_dst->key.u0.pbr_ipv4_key.flag.tcp_flags)
        {
            index = pe_dst->key.u0.pbr_ipv4_key.tcp_flags_idx;
            acl_master[lchip]->tcp_flag[index].ref++;
        }

        if (pe_dst->key.u0.pbr_ipv4_key.flag.l4_src_port_range)
        {
            index = pe_dst->key.u0.pbr_ipv4_key.l4_src_port_range_idx;
            acl_master[lchip]->l4_port[index].ref++;
        }

        if (pe_dst->key.u0.pbr_ipv4_key.flag.l4_dst_port_range)
        {
            index = pe_dst->key.u0.pbr_ipv4_key.l4_dst_port_range_idx;
            acl_master[lchip]->l4_port[index].ref++;
        }

        break;

    case CTC_ACL_KEY_PBR_IPV6:
        if (pe_dst->key.u0.pbr_ipv6_key.flag.tcp_flags)
        {
            index = pe_dst->key.u0.pbr_ipv6_key.tcp_flags_idx;
            acl_master[lchip]->tcp_flag[index].ref++;
        }

        if (pe_dst->key.u0.pbr_ipv6_key.flag.l4_src_port_range)
        {
            index = pe_dst->key.u0.pbr_ipv6_key.l4_src_port_range_idx;
            acl_master[lchip]->l4_port[index].ref++;
        }

        if (pe_dst->key.u0.pbr_ipv6_key.flag.l4_dst_port_range)
        {
            index = pe_dst->key.u0.pbr_ipv6_key.l4_dst_port_range_idx;
            acl_master[lchip]->l4_port[index].ref++;
        }

        break;

    default:
        ret = CTC_E_INVALID_PARAM;
        goto cleanup;
    }

    pe_dst->fpae.entry_id    = copy_entry->dst_entry_id;
    pe_dst->fpae.priority = pe_src->fpae.priority;
    pe_dst->group            = pg_dst;
    pe_dst->head.next        = NULL;

    CTC_ERROR_GOTO(_sys_greatbelt_acl_get_tcam_alloc_info(lchip, pe_dst->key.type, &(pe_dst->group->group_info), &asic_type, NULL), ret, cleanup);
    pb_dst    = &acl_master[lchip]->block[asic_type][pg_dst->group_info.block_id];
    if (NULL == pb_dst)
    {
        ret = CTC_E_INVALID_PTR;
        goto cleanup;
    }
    CTC_ERROR_GOTO(fpa_greatbelt_alloc_offset(acl_master[lchip]->fpa, &(pb_dst->fpab),
                                pe_src->fpae.priority, &block_index), ret, cleanup);
    pe_dst->fpae.offset_a = block_index;
    /* add to hash */
    ctc_hash_insert(acl_master[lchip]->entry, pe_dst);

    /* add to group */
    ctc_slist_add_head(pg_dst->entry_list, &(pe_dst->head));

    /* mark flag */
    pe_dst->fpae.flag = FPA_ENTRY_FLAG_UNINSTALLED;

        /* add to block */
        pb_dst->fpab.entries[block_index] = &pe_dst->fpae;

        /* free_count-- */
        (pb_dst->fpab.free_count)--;

        /* set new priority */
        CTC_ERROR_RETURN(_sys_greatbelt_acl_set_entry_priority(lchip, copy_entry->dst_entry_id, pe_dst->fpae.priority));

    (pg_dst->entry_count)++;
    SYS_ACL_UNLOCK(lchip);

    return CTC_E_NONE;

 cleanup:
    SYS_ACL_UNLOCK(lchip);
    mem_free(pa_dst);
    mem_free(pe_dst);
    return ret;
}

/*
 * set acl register
 */
STATIC int32
_sys_greatbelt_acl_set_register(uint8 lchip, sys_acl_register_t* p_reg)
{
    uint32                  cmd = 0;

    ipe_lookup_ctl_t        ipe_ctl;
    epe_acl_qos_ctl_t       epe_ctl;
    fib_engine_lookup_ctl_t fib_ctl;

    CTC_PTR_VALID_CHECK(p_reg);

    /* ----  FibEngineLookupCtl ---- */
    cmd = DRV_IOR(FibEngineLookupCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &fib_ctl));

    fib_ctl.mac_acl_cos_en            = CTC_FLAG_ISSET(p_reg->hash_mac_key_flag, CTC_ACL_HASH_MAC_KEY_FLAG_COS);
    fib_ctl.mac_acl_src_phy_port_en   = CTC_FLAG_ISSET(p_reg->hash_mac_key_flag, CTC_ACL_HASH_MAC_KEY_FLAG_PHY_PORT);
    fib_ctl.mac_acl_src_logic_port_en = CTC_FLAG_ISSET(p_reg->hash_mac_key_flag, CTC_ACL_HASH_MAC_KEY_FLAG_LOGIC_PORT);
    fib_ctl.mac_acl_mac_da_en         = CTC_FLAG_ISSET(p_reg->hash_mac_key_flag, CTC_ACL_HASH_MAC_KEY_FLAG_MAC_DA);
    fib_ctl.mac_acl_ether_type_en     = CTC_FLAG_ISSET(p_reg->hash_mac_key_flag, CTC_ACL_HASH_MAC_KEY_FLAG_ETH_TYPE);
    fib_ctl.mac_acl_vlan_id_en        = CTC_FLAG_ISSET(p_reg->hash_mac_key_flag, CTC_ACL_HASH_MAC_KEY_FLAG_VLAN_ID);

    fib_ctl.ipv4_acl_src_phy_port_en   = CTC_FLAG_ISSET(p_reg->hash_ipv4_key_flag, CTC_ACL_HASH_IPV4_KEY_FLAG_PHY_PORT);
    fib_ctl.ipv4_acl_src_logic_port_en = CTC_FLAG_ISSET(p_reg->hash_ipv4_key_flag, CTC_ACL_HASH_IPV4_KEY_FLAG_LOGIC_PORT);
    fib_ctl.ipv4_acl_l4_source_port_en = CTC_FLAG_ISSET(p_reg->hash_ipv4_key_flag, CTC_ACL_HASH_IPV4_KEY_FLAG_L4_SRC_PORT);
    fib_ctl.ipv4_acl_ip_sa_en          = CTC_FLAG_ISSET(p_reg->hash_ipv4_key_flag, CTC_ACL_HASH_IPV4_KEY_FLAG_IP_SA);
    fib_ctl.ipv4_acl_protocol_en       = CTC_FLAG_ISSET(p_reg->hash_ipv4_key_flag, CTC_ACL_HASH_IPV4_KEY_FLAG_L4_PROTOCOL);
    fib_ctl.ipv4_acl_l4_dest_port_en   = CTC_FLAG_ISSET(p_reg->hash_ipv4_key_flag, CTC_ACL_HASH_IPV4_KEY_FLAG_L4_DST_PORT);
    fib_ctl.ipv4_acl_ip_da_en          = CTC_FLAG_ISSET(p_reg->hash_ipv4_key_flag, CTC_ACL_HASH_IPV4_KEY_FLAG_IP_DA);
    fib_ctl.ipv4_aclis_arp_en          = CTC_FLAG_ISSET(p_reg->hash_ipv4_key_flag, CTC_ACL_HASH_IPV4_KEY_FLAG_ARP_PACKET);
    fib_ctl.ipv4_acl_dscp_en           = CTC_FLAG_ISSET(p_reg->hash_ipv4_key_flag, CTC_ACL_HASH_IPV4_KEY_FLAG_DSCP);

    cmd = DRV_IOW(FibEngineLookupCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &fib_ctl));

    /* ----  IpeLookupCtl ---- */
    cmd = DRV_IOR(IpeLookupCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_ctl));

    ipe_ctl.acl_use_packet_vlan      = p_reg->ingress_use_mapped_vlan ? 0 : 1;
    ipe_ctl.merge_mac_ip_acl_key     = p_reg->merge ? 1 : 0;
    ipe_ctl.trill_use_ipv6_key       = p_reg->trill_use_ipv6_key ? 1 : 0;
    ipe_ctl.arp_use_ipv6_key         = p_reg->arp_use_ipv6_key ? 1 : 0;
    ipe_ctl.acl_hash_lookup_en       = p_reg->hash_acl_en  ? 1 : 0;
    ipe_ctl.non_ip_mpls_use_ipv6_key = p_reg->non_ip_mpls_use_ipv6_key ? 1 : 0;
    ipe_ctl.l4_dest_port_overwrite   = 1;

#if 0
    ipe_ctl.route_obey_stp     = p_reg->route_obey_stp ? 1 : 0;
    ipe_ctl.oam_obey_acl_qos   = p_reg->oam_obey_acl_qos ? 1 : 0;
    ipe_ctl.acl_hash_lookup_en = p_reg->acl_hash_lookup_en ? 1 : 0;
#endif
    /* set in port */
    ipe_ctl.service_acl_qos_en = 0xFFF;

    cmd = DRV_IOW(IpeLookupCtl_t, DRV_ENTRY_FLAG);

        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_ctl));

    /* ----  EpeAclQosCtl ---- */

    cmd = DRV_IOR(EpeAclQosCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_ctl));

    epe_ctl.merge_mac_ip_acl_key     = p_reg->merge ? 1 : 0;
    epe_ctl.non_ip_mpls_use_ipv6_key = p_reg->non_ip_mpls_use_ipv6_key ? 1 : 0;
    epe_ctl.arp_use_ipv6_key         = p_reg->arp_use_ipv6_key ? 1 : 0;
    epe_ctl.trill_use_ipv6_key       = p_reg->trill_use_ipv6_key ? 1 : 0;
   /* 1:use ipSa in ipSa field,
    * 0:use { 12'd0, layer2Type[3:0], etherType[15:0] } in ipSa field
    */
    epe_ctl.ip_sa_mode_for_arp       = 1;
#if 0
    epe_ctl.stacking_disable_acl = p_reg->stacking_disable_acl ? 1 : 0;
    epe_ctl.oam_obey_acl_discard = p_reg->oam_obey_acl_discard ? 1 : 0;
    epe_ctl.oam_obey_acl_qos     = p_reg->oam_obey_acl_qos ? 1 : 0;
#endif

    /* set out port */
    epe_ctl.service_acl_qos_en = 0xFFF;

    cmd = DRV_IOW(EpeAclQosCtl_t, DRV_ENTRY_FLAG);

        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_ctl));

    return CTC_E_NONE;
}


/*
 * init acl register
 */
STATIC int32
_sys_greatbelt_acl_init_register(uint8 lchip, ctc_acl_global_cfg_t* acl_global_cfg)
{
    sys_acl_register_t acl_register;

    CTC_PTR_VALID_CHECK(acl_global_cfg);

    sal_memset(&acl_register, 0, sizeof(acl_register));

    acl_register.ingress_use_mapped_vlan  = acl_global_cfg->ingress_use_mapped_vlan;
    acl_register.merge                    = acl_global_cfg->merge_mac_ip;
    acl_register.arp_use_ipv6_key         = acl_global_cfg->arp_use_ipv6;
    acl_register.hash_acl_en              = acl_global_cfg->hash_acl_en;
    acl_register.trill_use_ipv6_key       = acl_global_cfg->trill_use_ipv6;
    acl_register.non_ip_mpls_use_ipv6_key = acl_global_cfg->non_ipv4_mpls_use_ipv6;

    acl_register.ingress_port_service_acl_en = acl_global_cfg->ingress_port_service_acl_en;
    acl_register.ingress_vlan_service_acl_en = acl_global_cfg->ingress_vlan_service_acl_en;
    acl_register.egress_port_service_acl_en  = acl_global_cfg->egress_port_service_acl_en;
    acl_register.egress_vlan_service_acl_en  = acl_global_cfg->egress_vlan_service_acl_en;

    acl_register.hash_ipv4_key_flag = acl_global_cfg->hash_ipv4_key_flag;
    acl_register.hash_mac_key_flag  = acl_global_cfg->hash_mac_key_flag;

    CTC_MAX_VALUE_CHECK(acl_global_cfg->priority_bitmap_of_stats, 0xF);
    acl_register.priority_bitmap_of_stats = acl_global_cfg->priority_bitmap_of_stats;

    CTC_ERROR_RETURN(_sys_greatbelt_acl_set_register(lchip, &acl_register));

    /* save it. not just for internal usage. like merge */
    sal_memcpy(&(acl_master[lchip]->acl_register), &acl_register, sizeof(acl_register));

    return CTC_E_NONE;
}


STATIC int32
_sys_greatbelt_acl_create_rsv_group(uint8 lchip)
{
    ctc_acl_group_info_t hash_group;
    sal_memset(&hash_group, 0, sizeof(hash_group));

    hash_group.type = CTC_ACL_GROUP_TYPE_HASH;

    CTC_ERROR_RETURN(_sys_greatbelt_acl_create_group(lchip, CTC_ACL_GROUP_ID_HASH_MAC, &hash_group));
    CTC_ERROR_RETURN(_sys_greatbelt_acl_create_group(lchip, CTC_ACL_GROUP_ID_HASH_IPV4, &hash_group));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_acl_opf_init(uint8 lchip)
{
    uint32              entry_num = 0;
    sys_greatbelt_opf_t opf;

    sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));

    CTC_ERROR_RETURN(sys_greatbelt_ftm_get_table_entry_num(lchip, DsAcl_t, &entry_num));
    if (entry_num)
    {
        CTC_ERROR_RETURN(sys_greatbelt_opf_init(lchip, OPF_ACL_AD, 1));

            opf.pool_index = 0;
            opf.pool_type  = OPF_ACL_AD;
            CTC_ERROR_RETURN(sys_greatbelt_opf_init_offset(lchip, &opf, 0, entry_num));
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_acl_white_list_init(uint8 lchip, ctc_acl_global_cfg_t* acl_global_cfg)
{
    acl_master[lchip]->white_list = acl_global_cfg->white_list_en;

    if(acl_master[lchip]->white_list)
    {
        sys_greatbelt_qos_policer_reserv(lchip);
    }

    return CTC_E_NONE;
}


int32
sys_greatbelt_acl_tcam_reinit(uint8 lchip, uint8 is_add)
{
    uint8       idx2;
    uint8       idx1;
    uint8       alloc_id_raw0;           /* raw tcam key_0 alloc id*/
    uint8       alloc_id_raw1;           /* raw tcam key_1 alloc id*/
    uint8       alloc_id_raw2;           /* raw tcam key_2 alloc id*/
    uint8       alloc_id_raw3;           /* raw tcam key_3 alloc id*/
    uint8       alloc_id_raw4;           /* raw tcam key_4 alloc id*/

    uint32      size;
    uint32      pb_sz[SYS_ACL_TCAM_RAW_NUM][SYS_ACL_BLOCK_NUM_MAX] = { { 0 } };
    uint8       pb_num[SYS_ACL_TCAM_RAW_NUM]                       = { 0 };
    uint8          prio      = 0;
    uint8          asic_type = 0;
    sys_acl_block_t* pb      = NULL;

    /* check init */
    if (NULL == acl_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }

    if (0 == is_add)
    {
        return CTC_E_NONE;
    }

    /*must clear all tcam entry*/
    for (prio = 0; prio < SYS_ACL_BLOCK_NUM_MAX; prio++)
    {
        for (asic_type = 0; asic_type < acl_master[lchip]->tcam_alloc_num; asic_type++)
        {
            pb = &acl_master[lchip]->block[asic_type][prio];
            if (pb->fpab.entry_count != pb->fpab.free_count)
            {
                return CTC_E_INVALID_CONFIG;
            }
        }
    }

    /* init block_num_max */
    {
        alloc_id_raw0              = 0;
        alloc_id_raw1              = 0;
        alloc_id_raw2              = 1;
        alloc_id_raw3              = 2;
        alloc_id_raw4              = 3;

        ACL_GET_TABLE_ENTYR_NUM(DsAclMacKey0_t, pb_sz[alloc_id_raw0]);
        ACL_GET_TABLE_ENTYR_NUM(DsAclMacKey1_t, pb_sz[alloc_id_raw0] + 1);
        ACL_GET_TABLE_ENTYR_NUM(DsAclMacKey2_t, pb_sz[alloc_id_raw0] + 2);
        ACL_GET_TABLE_ENTYR_NUM(DsAclMacKey3_t, pb_sz[alloc_id_raw0] + 3);

        ACL_GET_TABLE_ENTYR_NUM(DsAclIpv4Key0_t, pb_sz[alloc_id_raw1]);
        ACL_GET_TABLE_ENTYR_NUM(DsAclIpv4Key1_t, pb_sz[alloc_id_raw1] + 1);
        ACL_GET_TABLE_ENTYR_NUM(DsAclIpv4Key2_t, pb_sz[alloc_id_raw1] + 2);
        ACL_GET_TABLE_ENTYR_NUM(DsAclIpv4Key3_t, pb_sz[alloc_id_raw1] + 3);

        ACL_GET_TABLE_ENTYR_NUM(DsAclIpv6Key0_t, pb_sz[alloc_id_raw2]);
        ACL_GET_TABLE_ENTYR_NUM(DsAclIpv6Key1_t, pb_sz[alloc_id_raw2] + 1);

        ACL_GET_TABLE_ENTYR_NUM(DsIpv4PbrKey_t, pb_sz[alloc_id_raw3]);
        ACL_GET_TABLE_ENTYR_NUM(DsIpv6PbrKey_t, pb_sz[alloc_id_raw4]);
        /* init block_num */
        pb_num[alloc_id_raw0] =
            (pb_sz[alloc_id_raw0][0] ? 1 : 0) +
            (pb_sz[alloc_id_raw0][1] ? 1 : 0) +
            (pb_sz[alloc_id_raw0][2] ? 1 : 0) +
            (pb_sz[alloc_id_raw0][3] ? 1 : 0);

        pb_num[alloc_id_raw1] =
            (pb_sz[alloc_id_raw1][0] ? 1 : 0) +
            (pb_sz[alloc_id_raw1][1] ? 1 : 0) +
            (pb_sz[alloc_id_raw1][2] ? 1 : 0) +
            (pb_sz[alloc_id_raw1][3] ? 1 : 0);

        pb_num[alloc_id_raw2] =
            (pb_sz[alloc_id_raw2][0] ? 1 : 0) +
            (pb_sz[alloc_id_raw2][1] ? 1 : 0);

        pb_num[alloc_id_raw3] =
            (pb_sz[alloc_id_raw3][0] ? 1 : 0);

        pb_num[alloc_id_raw4] =
            (pb_sz[alloc_id_raw4][0] ? 1 : 0);


        acl_master[lchip]->block_num_max = (pb_num[alloc_id_raw0] > pb_num[alloc_id_raw1]) ?
                                    pb_num[alloc_id_raw0] : pb_num[alloc_id_raw1];
        acl_master[lchip]->block_num_max = (acl_master[lchip]->block_num_max > pb_num[alloc_id_raw2]) ?
                                    acl_master[lchip]->block_num_max : pb_num[alloc_id_raw2];
        acl_master[lchip]->block_num_max = (acl_master[lchip]->block_num_max > pb_num[alloc_id_raw3]) ?
                                    acl_master[lchip]->block_num_max : pb_num[alloc_id_raw3];
        acl_master[lchip]->block_num_max = (acl_master[lchip]->block_num_max > pb_num[alloc_id_raw4]) ?
                                    acl_master[lchip]->block_num_max : pb_num[alloc_id_raw4];
    }


    /* init sort algor / hash table /share pool */
    {
        /* init each block */
        for (idx1 = 0; idx1 < acl_master[lchip]->tcam_alloc_num; idx1++)
        {
            for (idx2 = 0; idx2 < SYS_ACL_BLOCK_NUM_MAX; idx2++)
            {
                acl_master[lchip]->block[idx1][idx2].fpab.lchip       = lchip;
                acl_master[lchip]->block[idx1][idx2].fpab.entry_count = (uint16) pb_sz[idx1][idx2];
                acl_master[lchip]->block[idx1][idx2].fpab.free_count  = (uint16) pb_sz[idx1][idx2];
                size                                                  = sizeof(sys_acl_entry_t*) * pb_sz[idx1][idx2];
                if (acl_master[lchip]->block[idx1][idx2].fpab.entries)
                {
                    mem_free(acl_master[lchip]->block[idx1][idx2].fpab.entries);
                }
                MALLOC_ZERO(MEM_ACL_MODULE, acl_master[lchip]->block[idx1][idx2].fpab.entries, size);
                if (NULL == acl_master[lchip]->block[idx1][idx2].fpab.entries && size)
                {
                    goto ERROR_FREE_MEM0;
                }
            }
        }

        if (!(acl_master[lchip]->fpa &&
            acl_master[lchip]->entry &&
        acl_master[lchip]->hash_ent_key))
        {
            goto ERROR_FREE_MEM0;
        }
    }

    CTC_ERROR_RETURN(_sys_greatbelt_acl_set_register(lchip, &(acl_master[lchip]->acl_register)));

    return CTC_E_NONE;

 ERROR_FREE_MEM0:

    return CTC_E_ACL_INIT;
}

/*
 * init acl module
 */
int32
sys_greatbelt_acl_init(uint8 lchip, ctc_acl_global_cfg_t* acl_global_cfg)
{
    /*
     *   #1 get key_size. decide use which mode.
     *   #2 160 bit mode, only label can be used. In this mode, only one block is support.
     *      This mode, we support port_group, vlan_group;
     *   #3 320 bit mode, port 0-51 use port_bitmap, port 52-55 use label, one for each.
     *      This mode, we support port_bitmap, vlan_mask. Vlan_group/Vlan_id cannot be support.
     *   above is the old way.

     *   The new way:
     *   #1 160 mode, ristrict to one block? Discuss later. This mode supports
     *      port_bitmap, global, SeviceACL
     *   #2 320 mode, leave the dicission of using port_bitmap or label to users.
     *      This mode supports port_bitmap, global, port_group, vlan_group, service ACL.

     */
    uint8       idx2;
    uint8       common_size0;
    uint8       common_size1;
    uint8       common_size2;
    uint8       idx1;
    uint8       alloc_id_raw0;           /* raw tcam key_0 alloc id*/
    uint8       alloc_id_raw1;           /* raw tcam key_1 alloc id*/
    uint8       alloc_id_raw2;           /* raw tcam key_2 alloc id*/
    uint8       alloc_id_raw3;           /* raw tcam key_3 alloc id*/
    uint8       alloc_id_raw4;           /* raw tcam key_4 alloc id*/
    uint8       alloc_id_raw5;           /* raw tcam key_5 (egress mac/ipv4/mpls 320bit) alloc id*/
    uint8       alloc_id_raw6;           /* raw tcam key_6 (egress ipv6 640bit) alloc id*/

    uint32      size;
    uint32      pb_sz[SYS_ACL_TCAM_RAW_NUM][SYS_ACL_BLOCK_NUM_MAX] = { { 0 } };
    uint8       pb_num[SYS_ACL_TCAM_RAW_NUM]                       = { 0 };
    uint32      ad_entry_size;
    ctc_spool_t spool;

    /* check init */
    if (acl_master[lchip])
    {
        return CTC_E_NONE;
    }

    CTC_PTR_VALID_CHECK(acl_global_cfg);

    /* malloc master */
    MALLOC_ZERO(MEM_ACL_MODULE, acl_master[lchip], sizeof(sys_acl_master_t));
    if (NULL == acl_master[lchip])
    {
        return CTC_E_NO_MEMORY;
    }

    acl_master[lchip]->group = ctc_hash_create(1,
                                        SYS_ACL_GROUP_HASH_BLOCK_SIZE,
                                        (hash_key_fn) _sys_greatbelt_acl_hash_make_group,
                                        (hash_cmp_fn) _sys_greatbelt_acl_hash_compare_group);


    /* init asic_key_type */
    acl_master[lchip]->tcam_alloc_num = SYS_ACL_TCAM_ALLOC_NUM;
    alloc_id_raw0              = 0;
    alloc_id_raw1              = 0;
    alloc_id_raw2              = 1;
    alloc_id_raw3              = 2;
    alloc_id_raw4              = 3;
    alloc_id_raw5              = 4;
    alloc_id_raw6              = 5;

    /* mac/ipv4/mpls key is 320bits, use same alloc_id = 0  */
    acl_master[lchip]->alloc_id[CTC_ACL_KEY_MAC]      = alloc_id_raw0;
    acl_master[lchip]->alloc_id[CTC_ACL_KEY_IPV4]     = alloc_id_raw1;
    acl_master[lchip]->alloc_id[CTC_ACL_KEY_MPLS]     = alloc_id_raw1;
    acl_master[lchip]->alloc_id[CTC_ACL_KEY_IPV6]     = alloc_id_raw2;
    acl_master[lchip]->alloc_id[CTC_ACL_KEY_PBR_IPV4] = alloc_id_raw3;
    acl_master[lchip]->alloc_id[CTC_ACL_KEY_PBR_IPV6] = alloc_id_raw4;
    acl_master[lchip]->egs_ipv4_alloc_id = alloc_id_raw5;
    acl_master[lchip]->egs_ipv6_alloc_id = alloc_id_raw6;

    /* init max priority */
    acl_master[lchip]->max_entry_priority = 0xFFFFFFFF;

    /* init hash base */
    acl_master[lchip]->hash_base = 16;

    /* init block_num_max */
    {
        ACL_GET_TABLE_ENTYR_NUM(DsAclMacKey0_t, pb_sz[alloc_id_raw0]);
        ACL_GET_TABLE_ENTYR_NUM(DsAclMacKey1_t, pb_sz[alloc_id_raw0] + 1);
        ACL_GET_TABLE_ENTYR_NUM(DsAclMacKey2_t, pb_sz[alloc_id_raw0] + 2);
        ACL_GET_TABLE_ENTYR_NUM(DsAclMacKey3_t, pb_sz[alloc_id_raw0] + 3);

        ACL_GET_TABLE_ENTYR_NUM(DsAclIpv6Key0_t, pb_sz[alloc_id_raw2]);
        ACL_GET_TABLE_ENTYR_NUM(DsAclIpv6Key1_t, pb_sz[alloc_id_raw2] + 1);

        ACL_GET_TABLE_ENTYR_NUM(DsIpv4PbrKey_t, pb_sz[alloc_id_raw3]);
        ACL_GET_TABLE_ENTYR_NUM(DsIpv6PbrKey_t, pb_sz[alloc_id_raw4]);

        /* ingress/egress key may not share same block */
        CTC_ERROR_RETURN(sys_greatbelt_ftm_get_entry_num_by_type(lchip, CTC_FTM_KEY_TYPE_ACL0_EGRESS, pb_sz[alloc_id_raw5]));
        CTC_ERROR_RETURN(sys_greatbelt_ftm_get_entry_num_by_type(lchip, CTC_FTM_KEY_TYPE_ACL1_EGRESS, pb_sz[alloc_id_raw5] + 1));
        CTC_ERROR_RETURN(sys_greatbelt_ftm_get_entry_num_by_type(lchip, CTC_FTM_KEY_TYPE_ACL2_EGRESS, pb_sz[alloc_id_raw5] + 2));
        CTC_ERROR_RETURN(sys_greatbelt_ftm_get_entry_num_by_type(lchip, CTC_FTM_KEY_TYPE_ACL3_EGRESS, pb_sz[alloc_id_raw5] + 3));

        CTC_ERROR_RETURN(sys_greatbelt_ftm_get_entry_num_by_type(lchip, CTC_FTM_KEY_TYPE_IPV6_ACL0_EGRESS, pb_sz[alloc_id_raw6]));
        CTC_ERROR_RETURN(sys_greatbelt_ftm_get_entry_num_by_type(lchip, CTC_FTM_KEY_TYPE_IPV6_ACL1_EGRESS, pb_sz[alloc_id_raw6] + 1));

        for (idx2 = 0; idx2 < SYS_ACL_BLOCK_NUM_MAX; idx2++)
        {
            if (pb_sz[alloc_id_raw5][idx2])
            {
                pb_sz[alloc_id_raw0][idx2] -= pb_sz[alloc_id_raw5][idx2];
            }

            if (pb_sz[alloc_id_raw6][idx2])
            {
                pb_sz[alloc_id_raw2][idx2] -= pb_sz[alloc_id_raw6][idx2];
            }
        }


        /* init block_num */
        pb_num[alloc_id_raw0] =
            (pb_sz[alloc_id_raw0][0] ? 1 : 0) +
            (pb_sz[alloc_id_raw0][1] ? 1 : 0) +
            (pb_sz[alloc_id_raw0][2] ? 1 : 0) +
            (pb_sz[alloc_id_raw0][3] ? 1 : 0);

        pb_num[alloc_id_raw2] =
            (pb_sz[alloc_id_raw2][0] ? 1 : 0) +
            (pb_sz[alloc_id_raw2][1] ? 1 : 0);

        pb_num[alloc_id_raw3] =
            (pb_sz[alloc_id_raw3][0] ? 1 : 0);

        pb_num[alloc_id_raw4] =
            (pb_sz[alloc_id_raw4][0] ? 1 : 0);

        pb_num[alloc_id_raw5] =
            (pb_sz[alloc_id_raw5][0] ? 1 : 0) +
            (pb_sz[alloc_id_raw5][1] ? 1 : 0) +
            (pb_sz[alloc_id_raw5][2] ? 1 : 0) +
            (pb_sz[alloc_id_raw5][3] ? 1 : 0);

        pb_num[alloc_id_raw6] =
            (pb_sz[alloc_id_raw6][0] ? 1 : 0) +
            (pb_sz[alloc_id_raw6][1] ? 1 : 0);

        acl_master[lchip]->block_num_max = pb_num[alloc_id_raw0];
        acl_master[lchip]->block_num_max = (acl_master[lchip]->block_num_max > pb_num[alloc_id_raw2]) ?
                                    acl_master[lchip]->block_num_max : pb_num[alloc_id_raw2];
        acl_master[lchip]->block_num_max = (acl_master[lchip]->block_num_max > pb_num[alloc_id_raw3]) ?
                                    acl_master[lchip]->block_num_max : pb_num[alloc_id_raw3];
        acl_master[lchip]->block_num_max = (acl_master[lchip]->block_num_max > pb_num[alloc_id_raw4]) ?
                                    acl_master[lchip]->block_num_max : pb_num[alloc_id_raw4];
        acl_master[lchip]->block_num_max = (acl_master[lchip]->block_num_max > pb_num[alloc_id_raw5]) ?
                                    acl_master[lchip]->block_num_max : pb_num[alloc_id_raw5];
        acl_master[lchip]->block_num_max = (acl_master[lchip]->block_num_max > pb_num[alloc_id_raw6]) ?
                                    acl_master[lchip]->block_num_max : pb_num[alloc_id_raw6];
    }


    /* init sort algor / hash table /share pool */
    {
        acl_master[lchip]->fpa = fpa_greatbelt_create(_sys_greatbelt_acl_get_block_by_pe_fpa,
                                     _sys_greatbelt_acl_entry_move_hw_fpa,
                                     sizeof(ctc_slistnode_t));
        /* init hash table :
         *    instead of using one linklist, acl use 32 linklist.
         *    hash caculatition is pretty fast, just label_id % hash_size
         */


        acl_master[lchip]->entry = ctc_hash_create(1,
                                            SYS_ACL_ENTRY_HASH_BLOCK_SIZE,
                                            (hash_key_fn) _sys_greatbelt_acl_hash_make_entry,
                                            (hash_cmp_fn) _sys_greatbelt_acl_hash_compare_entry);

        acl_master[lchip]->hash_ent_key = ctc_hash_create(1,
                                                   SYS_ACL_ENTRY_HASH_BLOCK_SIZE,
                                                   (hash_key_fn) _sys_greatbelt_acl_hash_make_entry_by_key,
                                                   (hash_cmp_fn) _sys_greatbelt_acl_hash_compare_entry_by_key);

        CTC_ERROR_RETURN(sys_greatbelt_ftm_get_table_entry_num(lchip, DsAcl_t, &ad_entry_size));
            sal_memset(&spool, 0, sizeof(spool));
            spool.lchip = lchip;
            spool.block_num = 1;
            spool.block_size = SYS_ACL_ENTRY_HASH_BLOCK_SIZE_BY_KEY;
            spool.max_count = ad_entry_size;
            spool.user_data_size = sizeof(sys_acl_action_t);
            spool.spool_key = (hash_key_fn) _sys_greatbelt_acl_hash_make_action;
            spool.spool_cmp = (hash_cmp_fn) _sys_greatbelt_acl_hash_compare_action;
            acl_master[lchip]->ad_spool = ctc_spool_create(&spool);

            if (!acl_master[lchip]->ad_spool)
            {
                goto ERROR_FREE_MEM0;
            }
        /* init each block */
        for (idx1 = 0; idx1 < acl_master[lchip]->tcam_alloc_num; idx1++)
        {
            for (idx2 = 0; idx2 < SYS_ACL_BLOCK_NUM_MAX; idx2++)
            {
                    acl_master[lchip]->block[idx1][idx2].fpab.lchip       = lchip;
                    acl_master[lchip]->block[idx1][idx2].fpab.entry_count = (uint16) pb_sz[idx1][idx2];
                    acl_master[lchip]->block[idx1][idx2].fpab.free_count  = (uint16) pb_sz[idx1][idx2];
                    size                                                  = sizeof(sys_acl_entry_t*) * pb_sz[idx1][idx2];
                    MALLOC_ZERO(MEM_ACL_MODULE, acl_master[lchip]->block[idx1][idx2].fpab.entries, size);
                    if (NULL == acl_master[lchip]->block[idx1][idx2].fpab.entries && size)
                    {
                        goto ERROR_FREE_MEM0;
                    }
            }
        }


        if (!(acl_master[lchip]->fpa &&
              acl_master[lchip]->entry &&
              acl_master[lchip]->hash_ent_key))
        {
            goto ERROR_FREE_MEM0;
        }
    }

    /* init l4_port_free_cnt */
    acl_master[lchip]->l4_port_free_cnt = SYS_ACL_L4_PORT_NUM;

    /* init tcp_flag_free_cnt */
    acl_master[lchip]->tcp_flag_free_cnt = SYS_ACL_TCP_FLAG_NUM;


    /* init entry size */
    {
        common_size0                                  = sizeof(sys_acl_entry_t) - sizeof(sys_acl_key_union_t);
        common_size1                                  = sizeof(sys_acl_hash_key_t) - sizeof(sys_acl_hash_key_union_t);
        acl_master[lchip]->entry_size[CTC_ACL_KEY_MAC]       = common_size0 + sizeof(sys_acl_mac_key_t);
        acl_master[lchip]->entry_size[CTC_ACL_KEY_IPV4]      = common_size0 + sizeof(sys_acl_ipv4_key_t);
        acl_master[lchip]->entry_size[CTC_ACL_KEY_MPLS]      = common_size0 + sizeof(sys_acl_mpls_key_t);
        acl_master[lchip]->entry_size[CTC_ACL_KEY_IPV6]      = common_size0 + sizeof(sys_acl_ipv6_key_t);
        acl_master[lchip]->entry_size[CTC_ACL_KEY_HASH_MAC]  = common_size0 + common_size1 + sizeof(sys_acl_hash_mac_key_t);
        acl_master[lchip]->entry_size[CTC_ACL_KEY_HASH_IPV4] = common_size0 + common_size1 + sizeof(sys_acl_hash_ipv4_key_t);
        acl_master[lchip]->entry_size[CTC_ACL_KEY_PBR_IPV4]  = common_size0 + sizeof(sys_acl_pbr_ipv4_key_t);
        acl_master[lchip]->entry_size[CTC_ACL_KEY_PBR_IPV6]  = common_size0 + sizeof(sys_acl_pbr_ipv6_key_t);

        common_size2 = sizeof(sys_acl_key_t) - sizeof(sys_acl_key_union_t);

        acl_master[lchip]->key_size[CTC_ACL_KEY_MAC]       = common_size2 + sizeof(sys_acl_mac_key_t);
        acl_master[lchip]->key_size[CTC_ACL_KEY_IPV4]      = common_size2 + sizeof(sys_acl_ipv4_key_t);
        acl_master[lchip]->key_size[CTC_ACL_KEY_MPLS]      = common_size2 + sizeof(sys_acl_mpls_key_t);
        acl_master[lchip]->key_size[CTC_ACL_KEY_IPV6]      = common_size2 + sizeof(sys_acl_ipv6_key_t);
        acl_master[lchip]->key_size[CTC_ACL_KEY_HASH_MAC]  = common_size2 + common_size1 + sizeof(sys_acl_hash_mac_key_t);
        acl_master[lchip]->key_size[CTC_ACL_KEY_HASH_IPV4] = common_size2 + common_size1 + sizeof(sys_acl_hash_ipv4_key_t);
        acl_master[lchip]->key_size[CTC_ACL_KEY_PBR_IPV4]  = common_size2 + sizeof(sys_acl_pbr_ipv4_key_t);
        acl_master[lchip]->key_size[CTC_ACL_KEY_PBR_IPV6]  = common_size2 + sizeof(sys_acl_pbr_ipv6_key_t);

        acl_master[lchip]->hash_action_size = (sizeof(sys_acl_action_t) - sizeof(sys_acl_action_with_chip_t));
    }

    CTC_ERROR_RETURN(_sys_greatbelt_acl_init_register(lchip, acl_global_cfg));

    SYS_ACL_CREATE_LOCK(lchip);
    CTC_ERROR_RETURN(_sys_greatbelt_acl_create_rsv_group(lchip));

    CTC_ERROR_RETURN(_sys_greatbelt_acl_opf_init(lchip));

    if((pb_num[alloc_id_raw0] > 1)
        || (pb_num[alloc_id_raw1] > 1)
        || (pb_num[alloc_id_raw2] > 1))
    {
        CTC_ERROR_RETURN(_sys_greatbelt_acl_white_list_init(lchip, acl_global_cfg));
    }

    sys_greatbelt_ftm_tcam_cb_register(lchip, SYS_FTM_TCAM_KEY_ACL, sys_greatbelt_acl_tcam_reinit);
    return CTC_E_NONE;


 ERROR_FREE_MEM0:

    return CTC_E_ACL_INIT;
}

STATIC int32
_sys_greatbelt_acl_free_node_data(void* node_data, void* user_data)
{
    uint8 free_group_hash = 0;

    if (user_data)
    {
        free_group_hash = *(uint8*)user_data;
        if (1 == free_group_hash)
        {
            ctc_slist_free(((sys_acl_group_t*)node_data)->entry_list);
        }
    }

    mem_free(node_data);

    return CTC_E_NONE;
}

int32
sys_greatbelt_acl_deinit(uint8 lchip)
{
    uint8       idx1=0;
    uint8       idx2=0;
    uint8       free_group_hash = 1;

    LCHIP_CHECK(lchip);
    if (NULL == acl_master[lchip])
    {
        return CTC_E_NONE;
    }

    /* deinit each block */
    for (idx1 = 0; idx1 < acl_master[lchip]->tcam_alloc_num; idx1++)
    {
        for (idx2 = 0; idx2 < SYS_ACL_BLOCK_NUM_MAX; idx2++)
        {
            mem_free(acl_master[lchip]->block[idx1][idx2].fpab.entries);
        }
    }

    /*free ad data*/
    ctc_spool_free(acl_master[lchip]->ad_spool);

    /*free acl key*/
    ctc_hash_traverse(acl_master[lchip]->entry, (hash_traversal_fn)_sys_greatbelt_acl_free_node_data, NULL);
    ctc_hash_free(acl_master[lchip]->entry);
    ctc_hash_free(acl_master[lchip]->hash_ent_key);

    fpa_greatbelt_free(acl_master[lchip]->fpa);

    /*free acl group*/
    ctc_hash_traverse(acl_master[lchip]->group, (hash_traversal_fn)_sys_greatbelt_acl_free_node_data, &free_group_hash);
    ctc_hash_free(acl_master[lchip]->group);

    /*opf free*/
    sys_greatbelt_opf_deinit(lchip, OPF_ACL_AD);

    sal_mutex_destroy(acl_master[lchip]->mutex);
    mem_free(acl_master[lchip]);

    return CTC_E_NONE;
}

/***********************************
   Below For Internal test
 ************************************/

/* Internal function for internal cli */
int32
sys_greatbelt_acl_set_global_cfg(uint8 lchip, sys_acl_global_cfg_t* p_cfg)
{
    sys_acl_register_t acl_register;

    /* get first */
    SYS_ACL_INIT_CHECK(lchip);
    SYS_ACL_LOCK(lchip);
    sal_memcpy(&acl_register, &(acl_master[lchip]->acl_register), sizeof(acl_register));
    SYS_ACL_UNLOCK(lchip);

    /* set user input */
    if (p_cfg->flag.ingress_use_mapped_vlan)
    {
        acl_register.ingress_use_mapped_vlan = p_cfg->value;
    }

    if (p_cfg->flag.merge_mac_ip)
    {
        acl_register.merge = p_cfg->value;
    }

    if (p_cfg->flag.arp_use_ipv6)
    {
        acl_register.arp_use_ipv6_key = p_cfg->value;
    }

    if (p_cfg->flag.hash_acl_en)
    {
        acl_register.hash_acl_en = p_cfg->value;
    }

    if (p_cfg->flag.hash_mac_key_flag)
    {
        acl_register.hash_mac_key_flag = p_cfg->value;
    }

    if (p_cfg->flag.hash_ipv4_key_flag)
    {
        acl_register.hash_ipv4_key_flag = p_cfg->value;
    }

    if (p_cfg->flag.priority_bitmap_of_stats)
    {
        CTC_MAX_VALUE_CHECK(p_cfg->value, 0xF);
        acl_register.priority_bitmap_of_stats = p_cfg->value;
    }

    if (p_cfg->flag.trill_use_ipv6)
    {
        acl_register.trill_use_ipv6_key = p_cfg->value;
    }

    if (p_cfg->flag.non_ipv4_mpls_use_ipv6)
    {
        acl_register.non_ip_mpls_use_ipv6_key = p_cfg->value;
    }

    if (p_cfg->flag.ingress_port_service_acl_en)
    {
        acl_register.ingress_port_service_acl_en = p_cfg->value;
    }

    if (p_cfg->flag.ingress_vlan_service_acl_en)
    {
        acl_register.ingress_vlan_service_acl_en = p_cfg->value;
    }

    if (p_cfg->flag.egress_port_service_acl_en)
    {
        acl_register.egress_port_service_acl_en = p_cfg->value;
    }

    if (p_cfg->flag.egress_vlan_service_acl_en)
    {
        acl_register.egress_vlan_service_acl_en = p_cfg->value;
    }

    CTC_ERROR_RETURN(_sys_greatbelt_acl_set_register(lchip, &acl_register));

    SYS_ACL_LOCK(lchip);
    /* save it for internal usage */
    sal_memcpy(&(acl_master[lchip]->acl_register), &acl_register, sizeof(acl_register));
    SYS_ACL_UNLOCK(lchip);

    return CTC_E_NONE;
}


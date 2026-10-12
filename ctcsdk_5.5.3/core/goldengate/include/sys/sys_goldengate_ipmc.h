/**
 @file sys_goldengate_ipmc.h

 @date 2011-11-07

 @version v2.0

*/
#ifndef _SYS_GOLDENGATE_IPMC_H
#define _SYS_GOLDENGATE_IPMC_H
#ifdef __cplusplus
extern "C" {
#endif

/***************************************************************
 *
 * Header Files
 *
 ***************************************************************/
#include "ctc_debug.h"
#include "ctc_stats.h"

#include "ctc_ipmc.h"

/***************************************************************
 *
 *  Defines and Macros
 *
 ***************************************************************/
#define SYS_IPMC_INIT_CHECK(lchip) \
    do { \
        SYS_LCHIP_CHECK_ACTIVE(lchip); \
        if (NULL == p_gg_ipmc_master[lchip]) \
        { \
            return CTC_E_NOT_INIT; \
        } \
    } while (0)


#define SYS_IPMC_DBG_OUT(level, FMT, ...)                              \
    {                                                                       \
        CTC_DEBUG_OUT(ip,  ipmc, IPMC_SYS, level, FMT, ##__VA_ARGS__);  \
    }

#define SYS_IPMC_DBG_FUNC()           SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__)
#define SYS_IPMC_DBG_INFO(FMT, ...) SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, FMT, ##__VA_ARGS__)

#define IPMC_LOCK   \
    if (p_gg_ipmc_master[lchip]->p_ipmc_mutex) sal_mutex_lock(p_gg_ipmc_master[lchip]->p_ipmc_mutex)

#define IPMC_UNLOCK \
    if (p_gg_ipmc_master[lchip]->p_ipmc_mutex) sal_mutex_unlock(p_gg_ipmc_master[lchip]->p_ipmc_mutex)

#define CTC_ERROR_RETURN_WITH_IPMC_UNLOCK(op) \
    do { \
        int32 rv; \
        if ((rv = (op)) < 0) \
        { \
            sal_mutex_unlock(p_gg_ipmc_master[lchip]->p_ipmc_mutex); \
            return (rv); \
        } \
    } while (0)

#define CTC_RETURN_IPMC_UNLOCK(op) \
    do \
    { \
        sal_mutex_unlock(p_gg_ipmc_master[lchip]->p_ipmc_mutex); \
        return (op); \
    } while (0)

#define SYS_IPMC_MASK_LEN_CHECK(ver, src_mask, dst_mask)         \
    do                                                \
    {                                                 \
        uint8 max_mask_len = (ver == CTC_IP_VER_6) ? 128 : 32; \
        if (((src_mask) != 0 && (src_mask) != max_mask_len) \
            || (dst_mask != max_mask_len)){         \
            return CTC_E_IPMC_INVALID_MASK_LEN; }               \
    } while (0)

#define SYS_IPMC_ADDRESS_SORT(val)                                          \
    {                                                                           \
        uint32 t;                                                           \
        t = val->address.ipv6.group_addr[0];                                \
        val->address.ipv6.group_addr[0] = val->address.ipv6.group_addr[3];  \
        val->address.ipv6.group_addr[3] = t;                                \
                                                                        \
        t = val->address.ipv6.group_addr[1];                                \
        val->address.ipv6.group_addr[1] = val->address.ipv6.group_addr[2];  \
        val->address.ipv6.group_addr[2] = t;                                \
                                                                        \
        t = val->address.ipv6.src_addr[0];                                  \
        val->address.ipv6.src_addr[0] = val->address.ipv6.src_addr[3];      \
        val->address.ipv6.src_addr[3] = t;                                  \
                                                                        \
        t = val->address.ipv6.src_addr[1];                                  \
        val->address.ipv6.src_addr[1] = val->address.ipv6.src_addr[2];      \
        val->address.ipv6.src_addr[2] = t;                                  \
    }

#define SYS_IPMC_GROUP_ADDRESS_CHECK(ver, address) \
    do \
    {                           \
        if (CTC_IP_VER_4 == (ver)) \
        { \
            if (0xE != ((address).ipv4.group_addr >> 28)) \
            { \
                return CTC_E_IPMC_NOT_MCAST_ADDRESS; \
            } \
        } \
        else                    \
        { \
            if (0xFF != ((((address).ipv6.group_addr[0]) >> 24) & 0xFF)) \
            { \
                return CTC_E_IPMC_NOT_MCAST_ADDRESS; \
            } \
        } \
    } while (0)

#define IPMC_MASK_IPV4_ADDR(ip, len)  \
    do                          \
    {                           \
        uint32 mask = (len) ? ~((1 << (CTC_IPV4_ADDR_LEN_IN_BIT - (len))) - 1) : 0; \
        (ip) &= mask;             \
    } while (0)

#define IPMC_MASK_IPV6_ADDR(ip, len)  \
    do                          \
    {                           \
        uint8 feedlen = CTC_IPV6_ADDR_LEN_IN_BIT - (len); \
        uint8 sublen = feedlen % 32; \
        int8 index = feedlen / 32; \
        if (sublen)              \
        {                       \
            uint32 mask = ~(1 << sublen) - 1; \
            (ip)[index] &= mask;  \
        }                       \
        index--;                \
        for (; index >= 0; index--) \
        {                       \
            (ip)[index] = 0;      \
        }                       \
    } while (0)

#define SYS_IPMC_VRFID_CHECK(address, ver) \
    do \
    {  uint16 vrfid = 0; \
       if (CTC_IP_VER_4 == (ver)) \
       { \
           vrfid = (address).ipv4.vrfid; \
       } \
       else \
       { \
           vrfid = (address).ipv6.vrfid; \
       } \
       if ((vrfid) >= (sys_goldengate_l3if_get_max_vrfid(lchip, ver)) && (vrfid != 0)) \
       { \
           return CTC_E_IPMC_INVALID_VRF; \
       } \
    } while (0)

#define SYS_L2MC_VRFID_CHECK(address, ver) \
    do \
    {  uint16 vrfid = 0; \
       if (CTC_IP_VER_4 == (ver)) \
       { \
           vrfid = (address).ipv4.vrfid; \
       } \
       else \
       { \
           vrfid = (address).ipv6.vrfid; \
       } \
       if ((vrfid) < CTC_MIN_VLAN_ID || (vrfid) > CTC_MAX_VLAN_ID) \
       { \
           return CTC_E_IPMC_INVALID_VRF; \
       } \
    } while (0)

#define SYS_IPMC_MASK_ADDR(address, src_mask_len, group_mask_len, ver) \
    do                          \
    {                           \
        if (CTC_IP_VER_4 == (ver)) \
        { \
            IPMC_MASK_IPV4_ADDR((address).ipv4.src_addr, (src_mask_len)); \
            IPMC_MASK_IPV4_ADDR((address).ipv4.group_addr, (group_mask_len)); \
        } \
        else                    \
        { \
            IPMC_MASK_IPV6_ADDR((address).ipv6.src_addr, (src_mask_len)); \
            IPMC_MASK_IPV6_ADDR((address).ipv6.group_addr, (group_mask_len)); \
        } \
    } while (0)

#define SYS_IPMC_BIDIPIM_CHECK(rp_id) \
    if (p_gg_ipmc_master[lchip]->bidipim_mode == SYS_IPMC_BIDIPIM_MODE0)\
    {\
        CTC_MAX_VALUE_CHECK(rp_id, 15);\
    }\
    else if (p_gg_ipmc_master[lchip]->bidipim_mode == SYS_IPMC_BIDIPIM_MODE1)\
    {\
        CTC_MAX_VALUE_CHECK(rp_id, 31);\
    }\
    else if (p_gg_ipmc_master[lchip]->bidipim_mode == SYS_IPMC_BIDIPIM_MODE2)\
    {\
        CTC_MAX_VALUE_CHECK(rp_id, 63);\
    }\
    else if (p_gg_ipmc_master[lchip]->bidipim_mode == SYS_IPMC_BIDIPIM_MODE3)\
    {\
        CTC_MAX_VALUE_CHECK(rp_id, 127);\
    }

#define SYS_IPMC_TCAM_LOOKUP            0x01        /* TCAM is used to lookup IPMC entry */

#define IPMC_IP_SA_PREFIX_INDEX_TYPE 7
#define IPMC_INVALID_SA_INDEX     0xFF

/**
 @brief Ipmc bidipim mode
*/
enum sys_ipmc_bidipim_mode_e
{
    SYS_IPMC_BIDIPIM_MODE0 = 0,           /**< [GG] IPMC bidipim (1k interface * 16 rp group) */
    SYS_IPMC_BIDIPIM_MODE1 = 1,           /**< [GG] IPMC bidipim (512 interface * 32 rp group) */
    SYS_IPMC_BIDIPIM_MODE2 = 2,           /**< [GG] IPMC bidipim (256 interface * 64 rp group) */
    SYS_IPMC_BIDIPIM_MODE3 = 3            /**< [GG] IPMC bidipim (128 interface * 128 rp group) */
};
typedef enum sys_ipmc_bidipim_mode_e sys_ipmc_bidipim_mode_t;

/* define group DS used in AVL or Hash node */
struct sys_ipmc_group_node_s
{
    uint32 ad_index;
    uint32 extern_flag;         /* group attribute */
    uint32 nexthop_id;          /* created in Nexthop model using GroupId */
    uint32 stats_id;
    uint32 pointer;             /* pointer */

    uint16 group_id;            /* Group id */
    uint16 refer_count;         /* (*,g) count */

    uint32 sa_index;            /* indicate sa offset */
    uint8  rp_id;
    uint8  flag;                /*0 bit:group_ip_mask_len; 1 bit:src_ip_mask_len; 2 bit:ip_version; 3 bit: use rpf profile;  4 bit:(*,g) valid  */
    uint8  write_base;
    uint8  is_group_exist;

    uint32 g_key_index;
    uint32 sg_key_index;
    union
    {
        ctc_ipmc_ipv4_addr_t ipv4;
        ctc_ipmc_ipv6_addr_t ipv6;
    } address;
};
typedef struct sys_ipmc_group_node_s sys_ipmc_group_node_t;

struct sys_ipmc_group_db_node_s
{
    uint32 nexthop_id;
    uint16 group_id;
    uint16 refer_count;
    uint8  flag;

    uint16 rpf_index;/*rpf profile*/
    uint32 extern_flag; 
    union
    {
        ctc_ipmc_ipv4_addr_t ipv4;
        ctc_ipmc_ipv6_addr_t ipv6;
    } address;
};
typedef struct sys_ipmc_group_db_node_s sys_ipmc_group_db_node_t;

struct sys_ipmc_master_s
{
    sal_mutex_t* p_ipmc_mutex;
    uint8 info_size[MAX_CTC_IP_VER];
    uint8 db_info_size[MAX_CTC_IP_VER];
    uint8 bidipim_mode;
    uint32 ipv4_l2mc_default_index;
    uint32 ipv4_ipmc_default_index;
    uint32 ipv6_l2mc_default_index;
    uint32 ipv6_ipmc_default_index;
    uint32 ipmc_group_cnt;
};
typedef struct sys_ipmc_master_s sys_ipmc_master_t;

struct sys_ipmc_rpf_info_s
{
    uint16 rpf_intf[SYS_GG_MAX_IPMC_RPF_IF];
    uint8 rpf_intf_valid[SYS_GG_MAX_IPMC_RPF_IF];
};
typedef struct sys_ipmc_rpf_info_s sys_ipmc_rpf_info_t;

#define SYS_IPMC_FLAG_GROUP_IP_MASK_LEN     0x01
#define SYS_IPMC_FLAG_SRC_IP_MASK_LEN       0x02
#define SYS_IPMC_FLAG_VERSION               0x04
#define SYS_IPMC_FLAG_RPF_PROFILE           0x08

#define SYS_IPMC_FLAG_DB_VERSION            0x01
#define SYS_IPMC_FLAG_DB_L2MC               0x02
#define SYS_IPMC_FLAG_DB_SRC_ADDR           0x04
#define SYS_IPMC_FLAG_DB_VALID              0x08
#define SYS_IPMC_FLAG_DB_DROP               0x10
#define SYS_IPMC_FLAG_DB_RPF_PROFILE        0x20


#define SYS_IPMC_IPV4_KEY_OFFSET 32
#define SYS_IPMC_IPV6_KEY_OFFSET SYS_IPMC_IPV4_KEY_OFFSET

enum sys_ipmc_ipda_type_e
{
    SYS_IPMC_IPDA_RPF_CHECK = 1
};
typedef enum sys_ipmc_ipda_type_e sys_ipmc_ipda_type_t;

extern int32
sys_goldengate_ipmc_init(uint8 lchip);

/**
 @brief De-Initialize ipmc module
*/
extern int32
sys_goldengate_ipmc_deinit(uint8 lchip);

extern int32
_sys_goldengate_ipmc_check(uint8 lchip, ctc_ipmc_group_info_t* p_group, uint8 support_def_entry);

extern int32
sys_goldengate_ipmc_add_group(uint8 lchip, ctc_ipmc_group_info_t* p_group);

extern int32
sys_goldengate_ipmc_remove_group(uint8 lchip, ctc_ipmc_group_info_t* p_group);

extern int32
sys_goldengate_ipmc_add_member(uint8 lchip, ctc_ipmc_group_info_t* p_group);

extern int32
sys_goldengate_ipmc_remove_member(uint8 lchip, ctc_ipmc_group_info_t* p_group);

extern int32
sys_goldengate_ipmc_add_rp_intf(uint8 lchip, ctc_ipmc_rp_t * p_rp);

extern int32
sys_goldengate_ipmc_remove_rp_intf(uint8 lchip, ctc_ipmc_rp_t * p_rp);

extern int32
sys_goldengate_ipmc_update_rpf(uint8 lchip, ctc_ipmc_group_info_t* p_group);

extern int32
sys_goldengate_ipmc_get_group_info(uint8 lchip, ctc_ipmc_group_info_t* p_group);

extern int32
sys_goldengate_ipmc_add_default_entry(uint8 lchip, uint8 ip_version, ctc_ipmc_default_action_t type);

extern int32
_sys_goldengate_ipmc_write_key(uint8 lchip, uint8 write_base, sys_ipmc_group_node_t* p_group_node);

extern int32
_sys_goldengate_ipmc_remove_key(uint8 lchip, uint8 delete_base, sys_ipmc_group_node_t* p_group_node);

extern int32
sys_goldengate_ipmc_set_mcast_force_route(uint8 lchip, ctc_ipmc_force_route_t* p_data);

extern int32
sys_goldengate_ipmc_get_mcast_force_route(uint8 lchip, ctc_ipmc_force_route_t* p_data);

extern int32
sys_goldengate_ipmc_traverse(uint8 lchip, uint8 ip_ver, ctc_ipmc_traverse_fn fn, void* user_data);

extern int32
_sys_goldengate_ipmc_get_ipda_info(uint8 lchip, sys_ipmc_group_node_t* p_group_node);

#ifdef __cplusplus
}
#endif

#endif


/**
 @file sys_greatbelt_ipmc.h

 @date 2011-11-07

 @version v2.0

*/
#ifndef _SYS_GREATBELT_IPMC_H
#define _SYS_GREATBELT_IPMC_H
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
        if (p_gb_ipmc_master[lchip] == NULL) \
        { \
            return CTC_E_NOT_INIT; \
        } \
    } while (0)

#define SYS_IPMC_DBG_OUT(level, FMT, ...)                              \
    {                                                                       \
        CTC_DEBUG_OUT(ip,  ipmc, IPMC_SYS, level, FMT, ##__VA_ARGS__);  \
    }

#define SYS_IPMC_DBG_INFO(FMT, ...) SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, FMT, ##__VA_ARGS__)

#define IPMC_LOCK   \
    if (p_gb_ipmc_master[lchip]->p_ipmc_mutex) sal_mutex_lock(p_gb_ipmc_master[lchip]->p_ipmc_mutex)

#define IPMC_UNLOCK \
    if (p_gb_ipmc_master[lchip]->p_ipmc_mutex) sal_mutex_unlock(p_gb_ipmc_master[lchip]->p_ipmc_mutex)

#define CTC_ERROR_RETURN_WITH_IPMC_UNLOCK(op) \
    do { \
        int32 rv; \
        if ((rv = (op)) < 0) \
        { \
            sal_mutex_unlock(p_gb_ipmc_master[lchip]->p_ipmc_mutex); \
            return (rv); \
        } \
    } while (0)

#define CTC_RETURN_IPMC_UNLOCK(op) \
    do \
    { \
        sal_mutex_unlock(p_gb_ipmc_master[lchip]->p_ipmc_mutex); \
        return (op); \
    } while (0)

#define SYS_IPMC_VERSION_ENABLE_CHECK(ver) \
    do                          \
    {                           \
        if (!p_gb_ipmc_master[lchip]->version_en[ver]) \
        { \
            return CTC_E_NO_RESOURCE; \
        } \
    } while (0)

#define SYS_IPMC_MASK_LEN_CHECK(ver, src_mask, dst_mask)         \
    do                                                \
    {                                                 \
        if (((src_mask) != 0 && (src_mask) != p_gb_ipmc_master[lchip]->max_mask_len[ver]) \
            || (dst_mask != p_gb_ipmc_master[lchip]->max_mask_len[ver])){         \
            return CTC_E_IPMC_INVALID_MASK_LEN; }               \
    } while (0)

#define SYS_IPMC_ADDRESS_SORT(val)                                          \
    {                                                                           \
        if (CTC_IP_VER_6 == (val->ip_version))                                   \
        {                                                                       \
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
        }                                                                       \
    }

#define SYS_IPMC_ADDRESS_SORT_0(val)                                          \
    {                                                                           \
        if (CTC_IP_VER_6 == (val->ip_version))                                   \
        {                                                                       \
            uint32 t;                                                           \
            t = val->ip_addr0.ipv6[0];                                \
            val->ip_addr0.ipv6[0] = val->ip_addr0.ipv6[3];  \
            val->ip_addr0.ipv6[3] = t;                                \
                                                                            \
            t = val->ip_addr0.ipv6[1];                                \
            val->ip_addr0.ipv6[1] = val->ip_addr0.ipv6[2];  \
            val->ip_addr0.ipv6[2] = t;                                \
                                                                            \
            t = val->ip_addr1.ipv6[0];                                  \
            val->ip_addr1.ipv6[0] = val->ip_addr1.ipv6[3];      \
            val->ip_addr1.ipv6[3] = t;                                  \
                                                                            \
            t = val->ip_addr1.ipv6[1];                                  \
            val->ip_addr1.ipv6[1] = val->ip_addr1.ipv6[2];      \
            val->ip_addr1.ipv6[2] = t;                                  \
        }                                                                       \
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
            if (0xFF != ((((address).ipv6.group_addr[3]) >> 24) & 0xFF)) \
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
            (ip)[(uint8)index] &= mask;  \
        }                       \
        index--;                \
        for (; index >= 0; index--) \
        {                       \
            (ip)[(uint8)index] = 0;      \
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
       if ((vrfid) >= p_gb_ipmc_master[lchip]->max_vrf_num[ver] && (vrfid != 0)) \
       { \
           return CTC_E_IPMC_INVALID_VRF; \
       } \
    } while (0)

#define SYS_IPMC_NO_VRFID_CHECK(address, ver) \
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
       if (vrfid != 0) \
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

#define SYS_IPMC_TCAM_LOOKUP            0x01        /* TCAM is used to lookup IPMC entry */

#define IPMC_IP_SA_PREFIX_INDEX_TYPE 7
#define IPMC_INVALID_SA_INDEX     0xFF

/* define group DS used in AVL or Hash node */
struct sys_ipmc_group_node_s
{
    uint32 ad_index;            /* indicate ad offset */
    uint32 extern_flag;         /* group attribute */
    uint32 nexthop_id;          /* created in Nexthop model using GroupId */
    uint16 group_id;            /* Group id */
    uint8  sa_index;            /* indicate sa offset */
    uint8  flag;                 /*0 bit:group_ip_mask_len; 1 bit:src_ip_mask_len; 2 bit:ip_version; 3 bit: use rpf profile*/
    union
    {
        ctc_ipmc_ipv4_addr_t ipv4;
        ctc_ipmc_ipv6_addr_t ipv6;
    } address;
};
typedef struct sys_ipmc_group_node_s sys_ipmc_group_node_t;

struct sys_ipmc_master_s
{
    sal_mutex_t* p_ipmc_mutex;
    uint32 sa_table_id[MAX_CTC_IP_VER];
    uint32 key_tcam_table_id[MAX_CTC_IP_VER];
    uint32 da_tcam_table_id[MAX_CTC_IP_VER];
    uint32 key_tcam_l2_table_id[MAX_CTC_IP_VER];
    uint32 da_tcam_l2_table_id[MAX_CTC_IP_VER];
    uint32 default_base[MAX_CTC_IP_VER];

    uint8 max_mask_len[MAX_CTC_IP_VER];
    uint8 addr_len[MAX_CTC_IP_VER];

    uint8 info_size[MAX_CTC_IP_VER];
    uint8 version_en[MAX_CTC_IP_VER];

    uint16 max_vrf_num[MAX_CTC_IP_VER];

    uint8 cpu_rpf;
    uint8  lookup_mode[MAX_CTC_IP_VER];
    uint32 default_entry_adindex[MAX_CTC_IP_VER];  /*ipv4/v6 MC default enry adindex*/
    uint8 is_ipmcv4_key160;

    uint8 ipv4_router_key_tbl_id;
    uint8 ipv4_router_key_sub_tbl_id;
    uint8 ipv6_router_key_tbl_id;
    uint8 ipv6_router_key_sub_tbl_id;

    uint8 mac_ipv4_key_tbl_id;
    uint8 mac_ipv4_key_sub_tbl_id;
    uint8 mac_ipv6_key_tbl_id;
    uint8 mac_ipv6_key_sub_tbl_id;

    uint8 ipv4_force_route;
    uint8 ipv6_force_route;
    sys_ipmc_group_node_t** hash_node[MAX_CTC_IP_VER];

    uint8 is_sort_mode;   
};
typedef struct sys_ipmc_master_s sys_ipmc_master_t;

struct sys_ipmc_rpf_info_s
{
    uint16 rpf_intf[SYS_GB_MAX_IPMC_RPF_IF];
    uint8 rpf_intf_valid[SYS_GB_MAX_IPMC_RPF_IF];
};
typedef struct sys_ipmc_rpf_info_s sys_ipmc_rpf_info_t;

#define SYS_IPMC_FLAG_GROUP_IP_MASK_LEN     0x01
#define SYS_IPMC_FLAG_SRC_IP_MASK_LEN       0x02
#define SYS_IPMC_FLAG_VERSION               0x04
#define SYS_IPMC_FLAG_RPF_PROFILE           0x08

extern int32
sys_greatbelt_ipmc_init(uint8 lchip);

/**
 @brief De-Initialize ipmc module
*/
extern int32
sys_greatbelt_ipmc_deinit(uint8 lchip);

extern int32
sys_greatbelt_ipmc_add_group(uint8 lchip, ctc_ipmc_group_info_t* p_group);

extern int32
sys_greatbelt_ipmc_remove_group(uint8 lchip, ctc_ipmc_group_info_t* p_group);

extern int32
sys_greatbelt_ipmc_add_member(uint8 lchip, ctc_ipmc_group_info_t* p_group);

extern int32
sys_greatbelt_ipmc_remove_member(uint8 lchip, ctc_ipmc_group_info_t* p_group);

extern int32
sys_greatbelt_ipmc_update_rpf(uint8 lchip, ctc_ipmc_group_info_t* p_group);

extern int32
sys_greatbelt_ipmc_get_group_info(uint8 lchip, ctc_ipmc_group_info_t* p_group);

extern int32
sys_greatbelt_ipmc_add_default_entry(uint8 lchip, uint8 ip_version, ctc_ipmc_default_action_t type);

extern int32
_sys_greatbelt_ipmc_write_key(uint8 lchip, sys_ipmc_group_node_t* p_group_node);

extern int32
_sys_greatbelt_ipmc_remove_key(uint8 lchip, sys_ipmc_group_node_t* p_group_node);

extern int32
sys_greatbelt_ipmc_set_mcast_force_route(uint8 lchip, ctc_ipmc_force_route_t* p_data);

extern int32
sys_greatbelt_ipmc_get_mcast_force_route(uint8 lchip, ctc_ipmc_force_route_t* p_data);

extern int32
sys_greatbelt_ipmc_traverse(uint8 lchip, uint8 ip_ver, ctc_ipmc_traverse_fn fn, void* user_data);

#ifdef __cplusplus
}
#endif

#endif


/**
 @file sys_greatbelt_ipuc.h

 @date 2011-11-07

 @version v2.0

*/
#ifndef _SYS_GREATBELT_IPUC_H
#define _SYS_GREATBELT_IPUC_H
#ifdef __cplusplus
extern "C" {
#endif
/***************************************************************
 *
 * Header Files
 *
 ***************************************************************/
#include "sal.h"
#include "ctc_const.h"
#include "ctc_vector.h"
#include "ctc_hash.h"

#include "ctc_ipuc.h"
/***************************************************************
 *
 *  Defines and Macros
 *
 ***************************************************************/

#define SYS_IPUC_DEFAULT_ENTRY_MIN_VRFID_NUM                    64        /* min VRF id number*/

#define SYS_IPUC_DBG_DUMP(FMT, ...)               \
    {                                                 \
        SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, FMT, ##__VA_ARGS__) \
    }

#define SYS_IPUC_DBG_FUNC()                          \
    { \
        CTC_DEBUG_OUT_FUNC(ip, ipuc, IPUC_SYS); \
    }

#define SYS_IPUC_DBG_ERROR(FMT, ...) \
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, FMT, ##__VA_ARGS__)

#define SYS_IPUC_DBG_OUT(level, FMT, ...)                              \
    {                                                                       \
        CTC_DEBUG_OUT(ip, ipuc, IPUC_SYS, level, FMT, ##__VA_ARGS__);  \
    }

#define SYS_IPUC_INIT_CHECK(lchip) \
    do{ \
        SYS_LCHIP_CHECK_ACTIVE(lchip); \
        if (p_gb_ipuc_master[lchip] == NULL) \
        { \
            return CTC_E_NOT_INIT; \
        } \
    }while(0)

#define SYS_IPUC_MASK_LEN_CHECK(ver, len)         \
    {                                                 \
        if ((len) > p_gb_ipuc_master[lchip]->max_mask_len[ver]){  \
            return CTC_E_INVALID_PARAM; }               \
    }

#define SYS_IPUC_KEY_MAP(p_ctc_ipuc, p_sys_ipuc)        \
    {                                                       \
        (p_sys_ipuc)->vrf_id = (p_ctc_ipuc)->vrf_id;        \
        (p_sys_ipuc)->masklen = (p_ctc_ipuc)->masklen;      \
        (p_sys_ipuc)->l4_dst_port = (p_ctc_ipuc)->l4_dst_port;      \
        if ((p_ctc_ipuc)->l4_dst_port > 0)                  \
        { \
            (p_sys_ipuc)->route_flag |= (p_ctc_ipuc)->is_tcp_port ? \
                SYS_IPUC_FLAG_IS_TCP_PORT : 0;                  \
        } \
        (p_sys_ipuc)->route_flag |=                         \
            (CTC_IP_VER_6 == (p_ctc_ipuc)->ip_ver) ?         \
            SYS_IPUC_FLAG_IS_IPV6 : 0;                     \
        sal_memcpy(&(p_sys_ipuc)->ip, &(p_ctc_ipuc)->ip,    \
                   p_gb_ipuc_master[lchip]->addr_len[(p_ctc_ipuc)->ip_ver]); \
    }

#define SYS_IPUC_DATA_MAP(p_ctc_ipuc, p_sys_ipuc)                            \
    {                                                                            \
        (p_sys_ipuc)->nh_id = (p_ctc_ipuc)->nh_id & 0xFFFF;                      \
        (p_sys_ipuc)->l3if = (p_ctc_ipuc)->l3_inf & 0xFFFF;                      \
        (p_sys_ipuc)->route_flag = (p_ctc_ipuc)->route_flag & MAX_CTC_IPUC_FLAG; \
        (p_sys_ipuc)->route_flag |= (CTC_IP_VER_6 == (p_ctc_ipuc)->ip_ver) ? SYS_IPUC_FLAG_IS_IPV6 : 0; \
        if ((p_ctc_ipuc)->l4_dst_port > 0)                  \
        { \
            (p_sys_ipuc)->route_flag |= (p_ctc_ipuc)->is_tcp_port ? \
                SYS_IPUC_FLAG_IS_TCP_PORT : 0;                  \
        } \
    }

#define SYS_IPUC_NAT_KEY_MAP(p_ctc_nat, p_sys_nat)        \
    {                                                       \
        (p_sys_nat)->vrf_id = (p_ctc_nat)->vrf_id;        \
        (p_sys_nat)->l4_src_port = (p_ctc_nat)->l4_src_port;      \
        sal_memcpy(&(p_sys_nat->ipv4), &(p_ctc_nat->ipsa.ipv4),    \
                   p_gb_ipuc_master[lchip]->addr_len[p_ctc_nat->ip_ver]); \
        if ((p_sys_nat)->l4_src_port > 0)   \
        {   \
            (p_sys_nat)->is_tcp_port = ((p_ctc_nat)->flag & CTC_IPUC_NAT_FLAG_USE_TCP_PORT) ? 1:0; \
        }   \
    }

#define SYS_IPUC_NAT_DATA_MAP(p_ctc_nat, p_sys_nat)                            \
    {                                                                            \
        (p_sys_nat)->new_l4_src_port = (p_ctc_nat)->new_l4_src_port;      \
        sal_memcpy(&(p_sys_nat->new_ipv4), &(p_ctc_nat->new_ipsa.ipv4),    \
                   p_gb_ipuc_master[lchip]->addr_len[p_ctc_nat->ip_ver]); \
    }

#define IPV4_MASK(ip, len)                                                          \
    {                                                                                   \
        uint32 mask = (len) ? ~((1 << (CTC_IPV4_ADDR_LEN_IN_BIT - (len))) - 1) : 0;    \
        (ip) &= mask;                                                                   \
    }

#define IPV6_MASK(ip, len)                              \
    {                                                       \
        uint8 feedlen = CTC_IPV6_ADDR_LEN_IN_BIT - (len);   \
        uint8 sublen = feedlen % 32;                        \
        int8 index = feedlen / 32;                          \
        if (sublen)                                          \
        {                                                   \
            uint32 mask = ~((1 << sublen) - 1);             \
            (ip)[(uint8)index] &= mask;                            \
        }                                                   \
        index--;                                            \
        for (; index >= 0; index--)                          \
        {                                                   \
            (ip)[(uint8)index] = 0;                                \
        }                                                   \
    }

#define SYS_IP_ADDR_MASK(ip, len, ver)      \
    {                                           \
        if (CTC_IP_VER_4 == (ver))               \
        {                                       \
            IPV4_MASK((ip.ipv4), (len));        \
        }                                       \
        else                                    \
        {                                       \
            IPV6_MASK((ip.ipv6), (len));        \
        }                                       \
    }

#define SYS_IP_FUNC_DBG_DUMP(val)                                                           \
    {                                                                                           \
        if (CTC_IP_VER_4 == val->ip_ver)                                                \
        {                                                                                       \
            SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_PARAM,                                             \
                             "nh_id :%d  vrf_id:%d  route_flag:0x%x  masklen:%d  ip_ver:%s  ip:%x\n",        \
                             val->nh_id, val->vrf_id, val->route_flag, val->masklen,                         \
                             "IPv4", val->ip.ipv4);                                                          \
        }                                                                                       \
        else                                                                                    \
        {                                                                                       \
            SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_PARAM,                                            \
                             "nh_id :%d  vrf_id:%d  route_flag:0x%x  masklen:%d  ip_ver:%s  ip:%x%x%x%x\n",  \
                             val->nh_id, val->vrf_id, val->route_flag, val->masklen,                         \
                             "IPv6", val->ip.ipv6[0], val->ip.ipv6[1],                                        \
                             val->ip.ipv6[2], val->ip.ipv6[3]);                                               \
        }                                                                                       \
    }

#define SYS_IP_ADDRESS_SORT(val)           \
    {                                          \
        if (CTC_IP_VER_6 == (val)->ip_ver)      \
        {                                      \
            uint32 t;                          \
            t = val->ip.ipv6[0];               \
            val->ip.ipv6[0] = val->ip.ipv6[3]; \
            val->ip.ipv6[3] = t;               \
                                           \
            t = val->ip.ipv6[1];               \
            val->ip.ipv6[1] = val->ip.ipv6[2]; \
            val->ip.ipv6[2] = t;               \
        }                                      \
    }

 /*#define SYS_IPUC_OCTO(ip32, index)       ((ip32[(index) / 4] >> (((index) % 4) * 8)) & 0xFF)*/

#define SYS_IPUC_VER(ptr)       (CTC_FLAG_ISSET((ptr)->route_flag, SYS_IPUC_FLAG_IS_IPV6) ? CTC_IP_VER_6 : CTC_IP_VER_4)
#define SYS_IPUC_VER_VAL(val)   (CTC_FLAG_ISSET((val).route_flag, SYS_IPUC_FLAG_IS_IPV6) ? CTC_IP_VER_6 : CTC_IP_VER_4)

enum sys_ipuc_flag_e
{
    /* MAX_CTC_IPUC_FLAG is used by CTC layer */
    SYS_IPUC_FLAG_MERGE_KEY         = MAX_CTC_IPUC_FLAG +1 ,        /* this entry DsFwd merge into DsIpDa */
    SYS_IPUC_FLAG_DEFAULT           = SYS_IPUC_FLAG_MERGE_KEY << 1, /* use to identify a default router */
    SYS_IPUC_FLAG_IS_IPV6           = SYS_IPUC_FLAG_DEFAULT << 1,   /* use to identify a ipv6 router */
    SYS_IPUC_FLAG_IS_TCP_PORT       = SYS_IPUC_FLAG_IS_IPV6 << 1,    /* if set, indicate l4_dst_port is a TCP port or is a UDP port */
    SYS_IPUC_FLAG_IS_BIND_NH        = SYS_IPUC_FLAG_IS_TCP_PORT << 1,
    SYS_IPUC_FLAG_HIGH_PRIORITY     = SYS_IPUC_FLAG_IS_BIND_NH << 1 /* means ipv6 128 bit route will set Dsipda.resultPriority */
};
typedef enum sys_ipuc_flag_e sys_ipuc_flag_t;

enum sys_ipuc_lookup_mode_e
{
    SYS_IPUC_TCAM_LOOKUP            =0x01,        /* TCAM is used to lookup IPUC entry */
    SYS_IPUC_HASH_LOOKUP            =0x02        /* LPM  is used to lookup IPUC entry */
};
typedef enum sys_ipuc_lookup_mode_e sys_ipuc_lookup_mode_t;

enum sys_ipuc_conflict_flag_e
{
    SYS_IPUC_CONFLICT_FLAG_ALL           =(0x1 << 0), /* means use big tcam */
    SYS_IPUC_CONFLICT_FLAG_IPV4          =(0x1 << 1), /* use small tcam resolve hash conflict */
    SYS_IPUC_CONFLICT_FLAG_HIGH          =(0x1 << 2), /* ipv6 hash high conflict */
    SYS_IPUC_CONFLICT_FLAG_MID           =(0x1 << 3), /* ipv6 hash mid conflict */
    SYS_IPUC_CONFLICT_FLAG_LOW           =(0x1 << 4), /* ipv6 hash low conflict */
    SYS_IPUC_CONFLICT_FLAG_CONFLICT_TCAM =(0x1 << 5), /* whole entry is in small tcam, not support now */
    SYS_IPUC_CONFLICT_FLAG_CAM           =(0x1 << 6)  /* if ipv6 is in cam, not support now */
};
typedef enum sys_ipuc_conflict_flag_e sys_ipuc_conflict_flag_t;

enum sys_ip_octo_e
{
    SYS_IP_OCTO_0_7,            /*  0   */
    SYS_IP_OCTO_8_15,           /*  1   */
    SYS_IP_OCTO_16_23,          /*  2   */
    SYS_IP_OCTO_24_31,          /*  3   */
    SYS_IP_OCTO_32_39,          /*  4   */
    SYS_IP_OCTO_40_47,          /*  5   */
    SYS_IP_OCTO_48_55,          /*  6   */
    SYS_IP_OCTO_56_63,          /*  7   */
    SYS_IP_OCTO_64_71,          /*  8   */
    SYS_IP_OCTO_72_79,          /*  9   */
    SYS_IP_OCTO_80_87,          /*  10  */
    SYS_IP_OCTO_88_95,          /*  11  */
    SYS_IP_OCTO_96_103,         /*  12  */
    SYS_IP_OCTO_104_111,        /*  13  */
    SYS_IP_OCTO_112_119,        /*  14  */
    SYS_IP_OCTO_120_127         /*  15  */
};
typedef enum sys_ip_octo_e sys_ip_octo_t;

enum sys_ipuc_hash_type_e
{
    SYS_IPUC_IPV4_HASH8,
    SYS_IPUC_IPV4_HASH16,
    SYS_IPUC_IPV6_HASH_HIGH32,
    SYS_IPUC_IPV6_HASH_MID32,
    SYS_IPUC_IPV6_HASH_LOW32,
    SYS_IPUC_HASH_MAX
};
typedef enum sys_ipuc_hash_type_e sys_ipuc_hash_type_t;

enum sys_ipuc_key_priority_e
{
    SYS_IPUC_KEY_PRIORITY_TCAM,
    SYS_IPUC_KEY_PRIORITY_LPM
};
typedef enum sys_ipuc_key_priority_e sys_ipuc_key_priority_t;

typedef uint32 ipv4_addr_t[1];

struct sys_ipuc_info_s
{
    uint16 nh_id;
    uint16 ad_offset;

    void*  lpm_result;

    uint16 vrf_id;
    uint8  masklen;
    uint8  conflict_flag;   /* sys_ipuc_conflict_flag_e IPv6<32 64<IPv6<128 and IPv4/IPv6 hash conflict use small TCAM store*/

    uint16 l4_dst_port;     /* layer4 destination port, if not 0, indicate NAPT key */
    uint16 l3if;

    uint32 route_flag;  /* sys_ipuc_flag_e  ctc_ipuc_flag_e*/

    uint8 binding_nh;   /* used for unbinding, temp value, get from ad spool*/
    uint8 rsv1[3];

    union
    {
        ipv4_addr_t ipv4;
        ipv6_addr_t ipv6;
    } ip;

    uint16 pointer;
    uint16 mid;
};
typedef struct sys_ipuc_info_s sys_ipuc_info_t;

struct sys_ipuc_nat_sa_info_s
{
    uint16 ad_offset;
    uint16 key_offset;
    uint16 vrf_id;
    uint8  in_tcam;
    uint8  is_tcp_port;

    uint16 l4_src_port;
    uint16 new_l4_src_port;

    ipv4_addr_t ipv4;
    ipv4_addr_t new_ipv4;
};
typedef struct sys_ipuc_nat_sa_info_s sys_ipuc_nat_sa_info_t;

struct sys_ipuc_info_list_s
{
    sys_ipuc_info_t* p_ipuc_info;
    struct sys_ipuc_info_list_s* p_next_info;
};
typedef struct sys_ipuc_info_list_s sys_ipuc_info_list_t;

typedef int32 (* ipuc_write_key_t)(uint8 lchip, sys_ipuc_info_t* p_ipuc_info, void* p_key);

struct sys_ipuc_master_s
{
    sal_mutex_t* mutex;
    ipuc_write_key_t write_key[MAX_CTC_IP_VER];
    uint32 tcam_sa_table[MAX_CTC_IP_VER];
    uint32 hash_sa_table[MAX_CTC_IP_VER];
    uint32 tcam_da_table[MAX_CTC_IP_VER];
    uint32 tcam_key_table[MAX_CTC_IP_VER];
    uint32 lpm_da_table;
    uint32 conflict_da_table;
    uint32 conflict_key_table;
    uint32 conflict_key_l4_port[SYS_IPUC_HASH_MAX];  /* confilct key use l4 port 19bit as sub table id*/
    uint32 conflict_key_l4_port_mask;

    uint8 conflict_key_table_id;
    uint8 conflict_key_table_id_mask;
    uint8 tcam_key_table_id[MAX_CTC_IP_VER];
    uint8 tcam_key_table_id_mask;
    uint8 tcam_key_sub_table_id[MAX_CTC_IP_VER];
    uint8 tcam_key_sub_table_id_mask;

    uint16 max_vrfid[MAX_CTC_IP_VER];
    uint8 max_mask_len[MAX_CTC_IP_VER];
    uint8 addr_len[MAX_CTC_IP_VER];
    uint8 info_size[MAX_CTC_IP_VER];
    uint8 version_en[MAX_CTC_IP_VER];

    uint16 big_tcam_num[MAX_CTC_IP_VER];
    uint16 nat_tcam_num[MAX_CTC_IP_VER];
    uint8 lookup_mode[MAX_CTC_IP_VER]; /* sys_ipuc_lookup_mode_e */
    uint8 ipv6_cam_bit_map;
    uint8 use_hash8;
    uint8 tcam_mode;    /* 0: no tcam waste; 1: use sort algorithm, may have some tcam waste, but more efficiency */
    uint8 must_use_dsfwd;    /* for ip-tunnel look up route in loop, if set ipuc must use dsfwd unless host route */

    uint16 default_base[MAX_CTC_IP_VER];

    uint32 lpm_counter;
    uint32 nat_lpm_counter;
    uint32 longest_counter[MAX_CTC_IP_VER];
    uint32 host_counter[MAX_CTC_IP_VER];
    uint16 tcam_counter[MAX_CTC_IP_VER];
    uint16 nat_tcam_counter[MAX_CTC_IP_VER];
    uint16 conflict_tcam_counter;
    uint16 hash_counter;
    uint32* default_route_nhid[MAX_CTC_IP_VER];
};
typedef struct sys_ipuc_master_s sys_ipuc_master_t;

/***************************************************************
 *
 *  Functions
 *
 ***************************************************************/

uint8
SYS_IPUC_OCTO(uint32* ip32, sys_ip_octo_t index);

extern int32
sys_greatbelt_ipuc_init(uint8 lchip, ctc_ipuc_global_cfg_t* p_ipuc_global_cfg);

/**
 @brief De-Initialize ipuc module
*/
extern int32
sys_greatbelt_ipuc_deinit(uint8 lchip);

extern int32
sys_greatbelt_ipuc_reinit(uint8 lchip, bool use_hash8);

extern int32
sys_greatbelt_ipuc_add(uint8 lchip, ctc_ipuc_param_t* p_ipuc_param);

extern int32
sys_greatbelt_ipuc_remove(uint8 lchip, ctc_ipuc_param_t* p_ipuc_param);

extern int32
sys_greatbelt_ipuc_get(uint8 lchip, ctc_ipuc_param_t* p_ipuc_param);

extern int32
sys_greatbelt_ipuc_add_default_entry(uint8 lchip, uint8 ip_ver, uint16 vrfid, uint32 nh_id);

extern int32
sys_greatbelt_ipuc_add_nat_sa(uint8 lchip, ctc_ipuc_nat_sa_param_t* p_ipuc_nat_sa_param);

extern int32
sys_greatbelt_ipuc_remove_nat_sa(uint8 lchip, ctc_ipuc_nat_sa_param_t* p_ipuc_nat_sa_param);

extern int32
sys_greatbelt_ipuc_add_tunnel(uint8 lchip, ctc_ipuc_tunnel_param_t* p_ipuc_tunnel_param);

extern int32
sys_greatbelt_ipuc_remove_tunnel(uint8 lchip, ctc_ipuc_tunnel_param_t* p_ipuc_tunnel_param);

extern int32
sys_greatbelt_ipuc_set_global_property(uint8 lchip, ctc_ipuc_global_property_t* p_global_prop);

extern int32
sys_greatbelt_ipuc_arrange_fragment(uint8 lchip);

extern int32
sys_greatbelt_ipuc_update_ipda_self_address(uint8 lchip, ctc_ipuc_param_t* p_ipuc_param, uint8 self_address);

extern int32
sys_greatbelt_ipuc_traverse(uint8 lchip, uint8 ip_ver, ctc_ipuc_traverse_fn fn, void* data);

extern int32
sys_greatbelt_ipuc_retrieve_ipv4(uint8 lchip, sys_ipuc_info_t* p_sys_ipuc_info);

extern int32
sys_greatbelt_ipuc_retrieve_ipv6(uint8 lchip, sys_ipuc_info_t* p_sys_ipuc_info);

extern int32
sys_greatbelt_ipuc_show_debug_info(uint8 lchip, ctc_ipuc_param_t* p_ipuc_param, uint8 detail);

#ifdef __cplusplus
}
#endif

#endif


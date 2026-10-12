/**
 @file sys_goldengate_ipuc.h

 @date 2011-11-07

 @version v2.0

*/
#ifndef _SYS_GOLDENGATE_IPUC_H
#define _SYS_GOLDENGATE_IPUC_H
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

#define SYS_IPUC_DEFAULT_ENTRY_MIN_VRFID_NUM                    256        /* min VRF id number*/

#define SYS_IPUC_DBG_OUT(level, FMT, ...)                              \
    {                                                                       \
        CTC_DEBUG_OUT(ip, ipuc, IPUC_SYS, level, FMT, ##__VA_ARGS__);  \
    }

#define SYS_IPUC_DBG_DUMP(FMT, ...)               \
    {                                                 \
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, FMT, ##__VA_ARGS__);\
    }

#define SYS_IPUC_DBG_FUNC()                          \
    { \
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n",__FUNCTION__);\
    }

#define SYS_IPUC_DBG_ERROR(FMT, ...) \
    { \
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, FMT, ##__VA_ARGS__);\
    }

#define SYS_IPUC_INIT_CHECK(lchip)                                 \
    {                                                              \
        SYS_LCHIP_CHECK_ACTIVE(lchip);                                  \
        if (NULL == p_gg_ipuc_master[lchip])                          \
        {                                                          \
            return CTC_E_NOT_INIT;                                 \
        }                                                          \
    }

#define SYS_IPUC_MASK_LEN_CHECK(ver, len)         \
    {                                                 \
        if ((len) > p_gg_ipuc_master[lchip]->max_mask_len[ver]){  \
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
                   p_gg_ipuc_master[lchip]->addr_len[(p_ctc_ipuc)->ip_ver]); \
    }

#define SYS_IPUC_DATA_MAP(p_ctc_ipuc, p_sys_ipuc)                            \
    {                                                                            \
        (p_sys_ipuc)->nh_id = (p_ctc_ipuc)->nh_id;                      \
        (p_sys_ipuc)->l3if = (p_ctc_ipuc)->l3_inf & 0xFFFF;                      \
        (p_sys_ipuc)->route_flag = (p_ctc_ipuc)->route_flag; \
        (p_sys_ipuc)->route_flag |= (CTC_IP_VER_6 == (p_ctc_ipuc)->ip_ver) ? SYS_IPUC_FLAG_IS_IPV6 : 0; \
        if ((p_ctc_ipuc)->l4_dst_port > 0)                  \
        { \
            (p_sys_ipuc)->route_flag |= (p_ctc_ipuc)->is_tcp_port ? \
                SYS_IPUC_FLAG_IS_TCP_PORT : 0;                  \
        } \
        (p_sys_ipuc)->gport = (p_ctc_ipuc)->gport;              \
        (p_sys_ipuc)->stats_id = (p_ctc_ipuc)->stats_id;        \
    }

#define SYS_IPUC_NAT_KEY_MAP(p_ctc_nat, p_sys_nat)        \
    {                                                       \
        (p_sys_nat)->vrf_id = (p_ctc_nat)->vrf_id;        \
        (p_sys_nat)->l4_src_port = (p_ctc_nat)->l4_src_port;      \
        sal_memcpy(&(p_sys_nat->ipv4), &(p_ctc_nat->ipsa.ipv4),    \
                   p_gg_ipuc_master[lchip]->addr_len[p_ctc_nat->ip_ver]); \
        if ((p_sys_nat)->l4_src_port > 0)   \
        {   \
            (p_sys_nat)->is_tcp_port = ((p_ctc_nat)->flag & CTC_IPUC_NAT_FLAG_USE_TCP_PORT) ? 1:0; \
        }   \
    }

#define SYS_IPUC_NAT_DATA_MAP(p_ctc_nat, p_sys_nat)                            \
    {                                                                            \
        (p_sys_nat)->new_l4_src_port = (p_ctc_nat)->new_l4_src_port;      \
        sal_memcpy(&(p_sys_nat->new_ipv4), &(p_ctc_nat->new_ipsa.ipv4),    \
                   p_gg_ipuc_master[lchip]->addr_len[CTC_IP_VER_4]); \
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
            (ip)[index] &= mask;                            \
        }                                                   \
        index--;                                            \
        for (; index >= 0; index--)                          \
        {                                                   \
            (ip)[index] = 0;                                \
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

enum sys_ipuc_operation_e
{
    DO_HASH = 1,
    DO_LPM  = 2,
    DO_TCAM = 3
};
typedef enum sys_ipuc_operation_e sys_ipuc_operation_t;

enum sys_ipuc_flag_e
{
    /* 0xff is used by CTC layer */
    SYS_IPUC_FLAG_MERGE_KEY         = MAX_CTC_IPUC_FLAG +1 ,        /* this entry DsFwd merge into DsIpDa */
    SYS_IPUC_FLAG_DEFAULT           = SYS_IPUC_FLAG_MERGE_KEY << 1, /* use to identify a default router */
    SYS_IPUC_FLAG_IS_IPV6           = SYS_IPUC_FLAG_DEFAULT << 1,   /* use to identify a ipv6 router */
    SYS_IPUC_FLAG_IS_TCP_PORT       = SYS_IPUC_FLAG_IS_IPV6 << 1,    /* if set, indicate l4_dst_port is a TCP port or is a UDP port */
    SYS_IPUC_FLAG_IS_BIND_NH        = SYS_IPUC_FLAG_IS_TCP_PORT << 1
};
typedef enum sys_ipuc_flag_e sys_ipuc_flag_t;

enum sys_ipuc_lookup_mode_e
{
    SYS_IPUC_TCAM_LOOKUP            =0x01,          /* TCAM is used to lookup IPUC entry */
    SYS_IPUC_HASH_LOOKUP            =0x02,          /* Host HASH is used to lookup IPUC entry */
    SYS_IPUC_LPM_LOOKUP             =0x04,          /* LPM HASH is used to lookup IPUC entry */
    SYS_IPUC_NAT_HASH_LOOKUP        =0x08           /* NAT HASH is used to lookup NAT entry */
};
typedef enum sys_ipuc_lookup_mode_e sys_ipuc_lookup_mode_t;

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

typedef uint32 ipv4_addr_t[1];

/* Warning!!! If modify this struct must sync to sys_ipv4_info_t and sys_ipv6_info_t */
struct sys_ipuc_info_s
{
    uint32 nh_id;
    uint32 ad_offset;
    uint8  rsv[2];

    void*  lpm_result;

    uint32 key_offset;  /* for l3 hash, must get key_offset from p_hash. Because tcam may shift by _sys_goldengate_ipv4_syn_key */
    uint32 l3if;        /* used to store rpf_id */

    uint16 vrf_id;
    uint8  route_opt;
    uint8  is_exist;
    uint32 route_flag;  /* sys_ipuc_flag_e  ctc_ipuc_flag_e*/

    uint8  masklen;
    uint8  conflict_flag;
    uint16 l4_dst_port;     /* layer4 destination port, if not 0, indicate NAPT key */
    uint32 gport;           /* assign output port */

    uint8  rpf_mode;        /* refer to sys_rpf_chk_mode_t */
    uint8 binding_nh;   /* used for unbinding, temp value, get from ad spool*/
    uint8  rsv1[2];
    uint32 stats_id;
    union
    {
        ipv4_addr_t ipv4;
        ipv6_addr_t ipv6;
    } ip;
};
typedef struct sys_ipuc_info_s sys_ipuc_info_t;

struct sys_ipv4_info_s
{
    uint32 nh_id;
    uint32 ad_offset;
    uint8  rsv[2];

    void*  lpm_result;

    uint32 key_offset;
    uint32 l3if;

    uint16 vrf_id;
    uint8  route_opt;
    uint8  is_exist;
    uint32 route_flag;

    uint8 masklen;
    uint8  conflict_flag;
    uint16 l4_dst_port;
    uint32 gport;

    uint8  rpf_mode;        /* refer to sys_rpf_chk_mode_t */
    uint8 binding_nh;   /* used for unbinding, temp value, get from ad spool*/
    uint8  rsv1[2];
    uint32 stats_id;

    ipv4_addr_t ipv4;
};
typedef struct sys_ipv4_info_s sys_ipv4_info_t;

struct sys_ipv6_info_s
{
    uint32 nh_id;
    uint32 ad_offset;
    uint8  rsv[2];

    void*  lpm_result;

    uint32 key_offset;
    uint32 l3if;

    uint16 vrf_id;
    uint8  route_opt;
    uint8  is_exist;
    uint32 route_flag;

    uint8 masklen;
    uint8  conflict_flag;
    uint16 l4_dst_port;
    uint32 gport;

    uint8  rpf_mode;        /* refer to sys_rpf_chk_mode_t */
    uint8 binding_nh;   /* used for unbinding, temp value, get from ad spool*/
    uint8  rsv1[2];
    uint32 stats_id;

    ipv6_addr_t ipv6;
};
typedef struct sys_ipv6_info_s sys_ipv6_info_t;

struct sys_ipuc_nat_sa_info_s
{
    uint32 ad_offset;
    uint32 key_offset;
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

struct sys_ipuc_master_s
{
    sal_mutex_t* mutex;
    uint32 tcam_sa_table[MAX_CTC_IP_VER];
    uint32 hash_sa_table[MAX_CTC_IP_VER];
    uint32 tcam_da_table[MAX_CTC_IP_VER];
    uint32 tcam_key_table[MAX_CTC_IP_VER];

    uint16 max_vrfid[MAX_CTC_IP_VER];
    uint8 max_mask_len[MAX_CTC_IP_VER];
    uint8 addr_len[MAX_CTC_IP_VER];
    uint8 info_size[MAX_CTC_IP_VER];
    uint8 version_en[MAX_CTC_IP_VER];

    uint16 default_base[MAX_CTC_IP_VER];

    uint32 max_hash_num;
    uint32 max_napt_hash_num;
    uint32 max_snat_hash_num;
    uint32 max_snat_tcam_num;
    uint32 max_tcam_num[MAX_CTC_IP_VER];

    uint32 host_counter[MAX_CTC_IP_VER]; /*masklen = 32 or 128*/
    uint32 lpm_counter[MAX_CTC_IP_VER]; /*masklen < 32 or <128*/
    uint32 hash_counter[MAX_CTC_IP_VER]; /*store in host0 hash*/
    uint32 alpm_counter[MAX_CTC_IP_VER]; /*store in lpm algorithm*/
    uint16 conflict_tcam_counter[MAX_CTC_IP_VER]; /*masklen = 32 or 128, but store in tcam*/
    uint16 conflict_alpm_counter[MAX_CTC_IP_VER]; /*masklen = 32 or 128, but store in alpm*/

    uint32 snat_hash_counter; /*snat store in host1 SaPort hash*/
    uint32 snat_tcam_counter; /*snat store in tcam*/
    uint16 napt_hash_counter; /*napt store in host1 Daport hash*/
    uint8 lookup_mode[MAX_CTC_IP_VER]; /* sys_ipuc_lookup_mode_e */

    uint8 default_route_mode;   /* 0:use AD mode; 1:use TCAM mode; */
    uint8 tcam_mode;    /* 0: no tcam waste; 1: use sort algorithm, may have some tcam waste, but more efficiency; now only support mode 0 */
    uint8 tcam_move;    /* for lpm route, the tcam may not move for p_hash is hit, otherwise need move */
    uint8 use_hash16;   /* defaut use hash8 */

    uint8 masklen_l;    /* masklen_l = use_hash16 ? 16 : 8; */
    uint8 masklen_ipv6_l;    /* 2 step pipeline for masklen_ipv6_l ~ (masklen_ipv6_l+16). Such as 16bit of 32~48,40~56. Must 32 <= masklen_ipv6_l <= 112*/
    uint8 host_route_mode;  /* when mask length is 32 for IPv4 or 128 for IPv6 , 0: add the key to hash table,in this mode can not enable rpf-check, 1: add the key to tcam/lpm */
    uint8 rsv;

};
typedef struct sys_ipuc_master_s sys_ipuc_master_t;

/***************************************************************
 *
 *  Functions
 *
 ***************************************************************/

static INLINE uint8
SYS_IPUC_OCTO(uint32* ip32, sys_ip_octo_t index)
{
    return ((ip32[index / 4] >> ((index & 3) * 8)) & 0xFF);
}

extern int32
sys_goldengate_ipuc_init(uint8 lchip, ctc_ipuc_global_cfg_t* p_ipuc_global_cfg);

/**
 @brief De-Initialize ipuc module
*/
extern int32
sys_goldengate_ipuc_deinit(uint8 lchip);

extern int32
sys_goldengate_ipuc_add(uint8 lchip, ctc_ipuc_param_t* p_ipuc_param);

extern int32
sys_goldengate_ipuc_remove(uint8 lchip,ctc_ipuc_param_t* p_ipuc_param);

extern int32
sys_goldengate_ipuc_get(uint8 lchip, ctc_ipuc_param_t* p_ipuc_param);

extern int32
sys_goldengate_ipuc_add_default_entry(uint8 lchip, uint8 ip_ver, uint16 vrfid, uint32 nh_id, ctc_ipuc_param_t* p_ipuc_param_info);

extern int32
sys_goldengate_ipuc_add_nat_sa(uint8 lchip, ctc_ipuc_nat_sa_param_t* p_ipuc_nat_sa_param);

extern int32
sys_goldengate_ipuc_remove_nat_sa(uint8 lchip, ctc_ipuc_nat_sa_param_t* p_ipuc_nat_sa_param);

extern int32
sys_goldengate_ipuc_add_tunnel(uint8 lchip, ctc_ipuc_tunnel_param_t* p_ipuc_tunnel_param);

extern int32
sys_goldengate_ipuc_remove_tunnel(uint8 lchip, ctc_ipuc_tunnel_param_t* p_ipuc_tunnel_param);

extern int32
sys_goldengate_ipuc_set_global_property(uint8 lchip, ctc_ipuc_global_property_t* p_global_prop);

extern int32
sys_goldengate_ipuc_arrange_fragment(uint8 lchip);

extern int32
sys_goldengate_ipuc_traverse(uint8 lchip, uint8 ip_ver, ctc_ipuc_traverse_fn fn, void* data);

extern int32
sys_goldengate_ipuc_retrieve_ip(uint8 lchip, sys_ipuc_info_t* p_sys_ipuc_info);

extern int32
sys_goldengate_ipuc_show_debug_info(uint8 lchip, ctc_ipuc_param_t* p_ipuc_param);

#ifdef __cplusplus
}
#endif

#endif


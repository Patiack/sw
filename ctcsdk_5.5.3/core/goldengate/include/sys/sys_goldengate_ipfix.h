/**
 @file sys_goldengate_ipfix.h

 @author  Copyright (C) 2013 Centec Networks Inc.  All rights reserved.

 @date 2013-10-17

 @version v3.0
*/
#ifndef _SYS_GOLDENGATE_IPFIX_H
#define _SYS_GOLDENGATE_IPFIX_H


/****************************************************************
 *
 * Header Files
 *
 ***************************************************************/
#include "ctc_const.h"
#include "ctc_ipfix.h"

/****************************************************************
*
* Defines and Macros
*
****************************************************************/
#define IPFIX_IPV4_MASK(ip, len)                              \
{  \
    uint32 temp;  \
    temp = (ip) >> (31 - (len-1));  \
    (ip) = temp << (31 - (len-1));  \
}

#define IPFIX_IPV6_MASK(ip, len)                              \
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
        for (; index > 0; index--)                          \
        {                                                   \
            (ip)[4-index] = 0;                                \
        }                                                   \
    }

#define SYS_IPFIX_CHECK_DIR(dir) \
    if ((dir) >= CTC_BOTH_DIRECTION) \
    { \
        return CTC_E_INVALID_PARAM; \
    }

#define IPFIX_IPV4_MASK_LEN_CHECK(len)         \
    {                                                 \
        if ((len > CTC_IPV4_ADDR_LEN_IN_BIT) || (len < 1)){  \
            return CTC_E_INVALID_PARAM; }               \
    }

#define IPFIX_IPV6_MASK_LEN_CHECK(len)         \
    {                                                 \
        if (len > CTC_IPV6_ADDR_LEN_IN_BIT){  \
            return CTC_E_INVALID_PARAM; }               \
    }

enum sys_ipfix_traverse_action_e
{
    SYS_IPFIX_DUMP_ENYRY_INFO = 0x0011,
};
typedef enum sys_ipfix_traverse_action_e sys_ipfix_traverse_action_t;

#define SYS_IPFIX_TCP_PROTOCOL 6
#define SYS_IPFIX_UDP_PROTOCOL 17
#define SYS_IPFIX_ICMP_PROTOCOL 1
#define SYS_IPFIX_GRE_PROTOCOL 47
#define SYS_IPFIX_SCTP_PROTOCOL 132

enum sys_ipfix_hash_type_e
{
    SYS_IPFIX_HASH_TYPE_INVALID                     = 0x0,
    SYS_IPFIX_HASH_TYPE_L2                          = 0x1,
    SYS_IPFIX_HASH_TYPE_L2L3                        = 0x2,
    SYS_IPFIX_HASH_TYPE_IPV4                      = 0x3,
    SYS_IPFIX_HASH_TYPE_IPV6                      = 0x4,
    SYS_IPFIX_HASH_TYPE_MPLS                      = 0x5,
};
typedef enum sys_ipfix_hash_type_e sys_ipfix_hash_type_t;

enum sys_ipfix_dest_type_e
{
    SYS_IPFIX_UNICAST_DEST,
    SYS_IPFIX_LINKAGG_DEST,
    SYS_IPFIX_L2MC_DEST,
    SYS_IPFIX_L3MC_DEST,
    SYS_IPFIX_BCAST_DEST,
    SYS_IPFIX_UNKNOW_PKT_DEST,
    SYS_IPFIX_APS_DEST,
    SYS_IPFIX_ECMP_DEST
};
typedef enum sys_ipfix_dest_type_e sys_ipfix_dest_type_t;

#define SYS_IPFIX_DBG_OUT(level, FMT, ...) \
    do { \
        CTC_DEBUG_OUT(ipfix, ipfix, IPFIX_SYS, level, FMT, ##__VA_ARGS__); \
    } while (0)

#define SYS_IPFIX_MAX_IP_MASK_LEN 31
#define IPFIX_SET_HW_IP6(d, s)    SYS_GOLDENGATE_REVERT_IP6(d, s)

/****************************************************************************
 *
* Function
*
*****************************************************************************/
extern int32
sys_goldengate_ipfix_init(uint8 lchip, void* p_global_cfg);

/**
 @brief De-Initialize ipfix module
*/
extern int32
sys_goldengate_ipfix_deinit(uint8 lchip);
extern int32
sys_goldengate_ipfix_set_port_cfg(uint8 lchip, uint16 gport,ctc_ipfix_port_cfg_t* ipfix_cfg);
extern int32
sys_goldengate_ipfix_get_port_cfg(uint8 lchip, uint16 gport,ctc_ipfix_port_cfg_t* p_ipfix_port_cfg);
extern int32
sys_goldengate_ipfix_sync_data(uint8 lchip, void* p_dma_info);
extern int32
sys_goldengate_ipfix_set_hash_field_sel(uint8 lchip, ctc_ipfix_hash_field_sel_t* field_sel);
extern int32
sys_goldengate_ipfix_set_global_cfg(uint8 lchip, ctc_ipfix_global_cfg_t *ipfix_cfg);
extern int32
sys_goldengate_ipfix_get_global_cfg(uint8 lchip, ctc_ipfix_global_cfg_t *ipfix_cfg);
extern int32
sys_goldengate_ipfix_register_cb(uint8 lchip, ctc_ipfix_fn_t callback,void *userdata);
int32
sys_goldengate_ipfix_get_cb(uint8 lchip, void **cb, void** user_data);

extern int32
sys_goldengate_ipfix_get_entry_by_key(uint8 lchip, ctc_ipfix_data_t* p_key, uint32* p_rst_hit, uint32* p_rst_key_index);
extern int32
sys_goldengate_ipfix_add_entry_by_key(uint8 lchip, ctc_ipfix_data_t* p_key);
extern int32
sys_goldengate_ipfix_add_entry_by_index(uint8 lchip, ctc_ipfix_data_t* p_key, uint32 index);
extern int32
sys_goldengate_ipfix_add_ad_by_index(uint8 lchip, ctc_ipfix_data_t* p_key, uint32 index);
extern int32
sys_goldengate_ipfix_delete_ad_by_index(uint8 lchip, uint32 index);
extern int32
sys_goldengate_ipfix_delete_entry_by_index(uint8 lchip, uint8 type, uint32 index);
extern int32
sys_goldengate_ipfix_delete_entry_by_key(uint8 lchip, ctc_ipfix_data_t* p_key);
extern uint32
sys_goldengate_ipfix_get_flow_cnt(uint8 lchip, uint32 detail);
extern int32
sys_goldengate_ipfix_get_ad_by_index(uint8 lchip, uint32 index, ctc_ipfix_data_t* p_out);
extern int32
sys_goldengate_ipfix_get_entry_by_index(uint8 lchip, uint32 index, uint8 key_type, ctc_ipfix_data_t* p_out);
extern int32
sys_goldengate_ipfix_traverse(uint8 lchip, ctc_ipfix_traverse_fn fn, ctc_ipfix_traverse_t* p_data, uint8 is_remove);
extern int32
sys_goldengate_ipfix_show_status(uint8 lchip, void *user_data);
extern int32
sys_goldengate_ipfix_show_stats(uint8 lchip, void *user_data);
extern int32
sys_goldengate_ipfix_clear_stats(uint8 lchip, void * user_data);
extern int32
sys_goldengate_ipfix_show_entry_info(ctc_ipfix_data_t* p_info, void* user_data);
extern int32
sys_goldengate_ipfix_export_stats(uint8 lchip, uint8 exp_reason, uint64 pkt_cnt, uint64 byte_cnt);
#endif

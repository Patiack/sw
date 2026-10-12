/****************************************************************************
 * drv_userid_lookup.h  All error code Deinfines, include SDK error code.
 *
 * Copyright:    (c)2011 Centec Networks Inc.  All rights reserved.
 *
 * Modify History:
 * Revision:     V4.3.0
 * Author:       XuZx
 * Date:         2011-07-15
 * Reason:       Create for GreatBelt v4.3.0
 *
 * Revision:     V4.28.2
 * Revisor:      XuZx
 * Date:         2011-09-27
 * Reason:       Revise for GreatBelt v4.28.2
 *
 * Revision:     V5.6.0
 * Revisor:      XuZx
 * Date:         2012-01-06
 * Reason:       Revise for GreatBelt v5.6.0
 *
 * Revision:     v5.7.0
 * Revisor:      XuZx
 * Date:         2012-01-17.
 * Reason:       Revise for GreatBelt v5.7.0
 *
 * Revision:     v5.9.0
 * Revisor:      XuZx
 * Date:         2012-02-03.
 * Reason:       Revise for GreatBelt v5.9.0
 ****************************************************************************/

#ifndef _DRV_USERID_LOOKUP_H
#define _DRV_USERID_LOOKUP_H

#define USERID_HASH_INDEX_BITS_LEN    16
#define USERID_HASH_INDEX_MASK        ((1 << USERID_HASH_INDEX_BITS_LEN) - 1)

enum userid_key_type_e
{
    USERID_KEY_TYPE_DISABLE          = 0x0,

    /* UserId, VLAN related */
    USERID_KEY_TYPE_TWO_VLAN         = 0x1,
    USERID_KEY_TYPE_SVLAN            = 0x2,
    USERID_KEY_TYPE_CVLAN            = 0x3,
    USERID_KEY_TYPE_SVLAN_COS        = 0x4,
    USERID_KEY_TYPE_CVLAN_COS        = 0x5,

    /* UserId, MAC related */
    USERID_KEY_TYPE_MAC_SA           = 0x6,
    USERID_KEY_TYPE_PORT_MAC_SA      = 0x7,

    /* UserId, IP related */
    USERID_KEY_TYPE_IPV4_SA          = 0x8,
    USERID_KEY_TYPE_PORT_IPV4_SA     = 0x9,

    /* UserId, port based */
    USERID_KEY_TYPE_PORT             = 0xA,
    USERID_KEY_TYPE_L2               = 0xB,
    USERID_KEY_TYPE_IPV6_SA          = 0xC,

    /*TunnelId */
    USERID_KEY_TYPE_PBB              = 0xD,
    USERID_KEY_TYPE_IPV4_TUNNEL      = 0xE,
    USERID_KEY_TYPE_IPV4_GRE_KEY     = 0xF,
    USERID_KEY_TYPE_IPV4_UDP         = 0x10,
    USERID_KEY_TYPE_CAPWAP           = 0x11,
    USERID_KEY_TYPE_TRILL_UC_RPF     = 0x12,
    USERID_KEY_TYPE_TRILL_MC_RPF     = 0x13,
    USERID_KEY_TYPE_TRILL_MC_ADJ     = 0x14,
    USERID_KEY_TYPE_TRILL_UC         = 0x15,
    USERID_KEY_TYPE_TRILL_MC         = 0x16,
    USERID_KEY_TYPE_IPV4_RPF         = 0x17,

    /* OAM */
    USERID_KEY_TYPE_MPLS_SECTION_OAM = 0x18,
    USERID_KEY_TYPE_PBT_OAM          = 0x19,
    USERID_KEY_TYPE_MPLS_LABEL_OAM   = 0x1A,
    USERID_KEY_TYPE_BFD_OAM          = 0x1B,
    USERID_KEY_TYPE_ETHER_FID_OAM    = 0x1C,
    USERID_KEY_TYPE_ETHER_VLAN_OAM   = 0x1D,
    USERID_KEY_TYPE_ETHER_RMEP       = 0x1E,

    USERID_KEY_TYPE_NUM
};
typedef enum userid_key_type_e userid_key_type_t;

enum userid_hash_type_e
{
    USERID_HASH_TYPE_TWO_VLAN,
    USERID_HASH_TYPE_SVLAN,
    USERID_HASH_TYPE_CVLAN,
    USERID_HASH_TYPE_SVLAN_COS,
    USERID_HASH_TYPE_CVLAN_COS,
    USERID_HASH_TYPE_MAC_SA,
    USERID_HASH_TYPE_PORT_MAC_SA,
    USERID_HASH_TYPE_IPV4_SA,
    USERID_HASH_TYPE_PORT_VLAN_CROSS,
    USERID_HASH_TYPE_PORT_IPV4_SA,
    USERID_HASH_TYPE_PORT_CROSS,
    USERID_HASH_TYPE_PORT,
    USERID_HASH_TYPE_L2,
    USERID_HASH_TYPE_IPV6_SA,
    USERID_HASH_TYPE_PBB,
    USERID_HASH_TYPE_IPV4_TUNNEL,
    USERID_HASH_TYPE_IPV4_GRE_KEY,
    USERID_HASH_TYPE_IPV4_UDP,
    USERID_HASH_TYPE_CAPWAP,
    USERID_HASH_TYPE_TRILL_UC_RPF,
    USERID_HASH_TYPE_TRILL_MC_RPF,
    USERID_HASH_TYPE_TRILL_MC_ADJ,
    USERID_HASH_TYPE_TRILL_UC,
    USERID_HASH_TYPE_TRILL_MC,
    USERID_HASH_TYPE_IPV4_RPF,
    USERID_HASH_TYPE_MPLS_SECTION_OAM,
    USERID_HASH_TYPE_PBT_OAM,
    USERID_HASH_TYPE_MPLS_LABEL_OAM,
    USERID_HASH_TYPE_BFD_OAM,
    USERID_HASH_TYPE_ETHER_FID_OAM,
    USERID_HASH_TYPE_ETHER_VLAN_OAM,
    USERID_HASH_TYPE_ETHER_RMEP,
    USERID_HASH_TYPE_NUM,
    USERID_HASH_TYPE_INVALID = USERID_HASH_TYPE_NUM
};
typedef enum userid_hash_type_e userid_hash_type_t;

enum userid_direction_e
{
    USERID_DIRECTION_IPE_USERID,
    USERID_DIRECTION_OTHER,
    USERID_DIRECTION_NUM
};
typedef enum userid_direction_e userid_direction_t;

enum userid_usage_e
{
    USERID_USAGE_USERID,
    USERID_USAGE_OAM,
    USERID_USAGE_INVALID,
    USERID_USAGE_NUM
};
typedef enum userid_usage_e userid_usage_t;

enum userid_hash_level_e
{
    USERID_HASH_LEVEL0,
    USERID_HASH_LEVEL1,
    USERID_HASH_LEVEL_NUM
};
typedef enum userid_hash_level_e userid_hash_level_t;

enum userid_crc_polynomial_e
{
    USERID_CRC_POLYNOMIAL0,
    USERID_CRC_POLYNOMIAL1,
    USERID_CRC_POLYNOMIAL2,
    USERID_CRC_POLYNOMIAL3,
    USERID_CRC_POLYNOMIAL_NUM
};
typedef enum userid_crc_polynomial_e userid_crc_polynomial_t;

struct userid_lookup_extra_info_s
{
    uint32 local_phy_port;
    userid_direction_t direction;
};
typedef struct userid_lookup_extra_info_s userid_lookup_extra_info_t;

struct userid_lookup_extra_result_s
{
    uint8 default_entry_valid;
    void* data;
};
typedef struct userid_lookup_extra_result_s userid_lookup_extra_result_t;

extern int32
drv_greatbelt_userid_lookup(lookup_info_t*, lookup_result_t*);

#endif


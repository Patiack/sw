/****************************************************************************
 * drv_fib_lookup.h  All error code Deinfines, include SDK error code.
 *
 * Copyright:    (c)2011 Centec Networks Inc.  All rights reserved.
 *
 * Modify History:
 * Revision:     V4.3.0
 * Author:       XuZx
 * Date:         2011-07-15
 * Reason:       Create for GreatBelt v4.3.0
 *
 * Vision:       V5.1.0
 * Revisor:      XuZx
 * Date:         2011-12-12.
 * Reason:       Revise for GreatBelt v5.1.0.
 *
 * Vision:       V5.6.0
 * Revisor:      XuZx
 * Date:         2012-01-07.
 * Reason:       Revise for GreatBelt v5.6.0.
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
#ifndef _DRV_FIB_LOOKUP_H
#define _DRV_FIB_LOOKUP_H

#define FIB_HASH_INDEX_BITS_LEN    15
#define FIB_HASH_INDEX_MASK        ((1 << FIB_HASH_INDEX_BITS_LEN) - 1)
#define FIB_BUCKET_ENTRY_NUM       2
#define DDB_MAX_NUM                81920
#define PIN_FULL_SIZE_EN           1
#define FDB_CAM_NUM                (DYNAMIC_DS_FDB_LOOKUP_INDEX_CAM_MAX_INDEX * 2)

enum fib_hash_level_e
{
    FIB_HASH_LEVEL0,
    FIB_HASH_LEVEL1,
    FIB_HASH_LEVEL2,
    FIB_HASH_LEVEL3,
    FIB_HASH_LEVEL4,
    FIB_HASH_LEVEL5,
    FIB_HASH_LEVEL_NUM
};
typedef enum fib_hash_level_e fib_hash_level_t;

enum fib_crc_polynomial_e
{
    FIB_CRC_POLYNOMIAL0,
    FIB_CRC_POLYNOMIAL1,
    FIB_CRC_POLYNOMIAL2,
    FIB_CRC_POLYNOMIAL3,
    FIB_CRC_POLYNOMIAL4,
    FIB_CRC_POLYNOMIAL5,
    FIB_CRC_POLYNOMIAL6,
    FIB_CRC_POLYNOMIAL7,
    FIB_CRC_POLYNOMIAL_NUM
};
typedef enum fib_crc_polynomial_e fib_crc_polynomial_t;

enum fib_key_type_e
{
    /* IPv4 Ucast */
    FIB_KEY_TYPE_IPV4_NAT_DA,             /* 5'b0_0000 */
    FIB_KEY_TYPE_IPV4_UCAST,              /* 5'b0_0001 */

    /* IPv4 Mcast */
    FIB_KEY_TYPE_IPV4_MCAST,              /* 5'b0_0010 */

    /* IPv6 Ucast */
    FIB_KEY_TYPE_IPV6_NAT_DA = 4,         /* 5'b0_0100 */
    FIB_KEY_TYPE_IPV6_UCAST,              /* 5'b0_0101 */

    /* IPv6 Mcast */
    FIB_KEY_TYPE_IPV6_MCAST = 0x6,        /* 5'b0_0110 */

    /* IPv4 RPF */
    FIB_KEY_TYPE_IPV4_RPF = 0x9,          /* 0x9 */

    /* IPv6 RPF */
    FIB_KEY_TYPE_IPV6_RPF,                /* 0xA*/

    /* IPv4 NAT SA */
    FIB_KEY_TYPE_IPV4_NAT_SA,             /* 0xB */

    /* IPv6 NAT SA */
    FIB_KEY_TYPE_IPV6_NAT_SA,             /* 0xC */

    /* MAC IPv4 Multicast */
    FIB_KEY_TYPE_MAC_IPV4_MULTICAST = 0xE, /* 0xE */

    /* MAC IPv6 Multicast */
    FIB_KEY_TYPE_MAC_IPV6_MULTICAST,      /* 0xF */

    /* MAC DA */
    FIB_KEY_TYPE_MAC_DA = 0x18,           /* 0x18 */

    /* MAC SA */
    FIB_KEY_TYPE_MAC_SA = 0x19,           /* 0x19 */

    /* FCoE */
    FIB_KEY_TYPE_FCOE = 0x1A,             /* 0x1A */

    /* FCoE RPF */
    FIB_KEY_TYPE_FCOE_RPF,                /* 0x1B */

    /* TRILL Ucast */
    FIB_KEY_TYPE_TRILL_UCAST,             /* 0x1C */

    /* TRILL Mcast */
    FIB_KEY_TYPE_TRILL_MCAST,             /* 0x1D */

    /* TRILL Mcast VLAN */
    FIB_KEY_TYPE_TRILL_MCAST_VLAN,        /* 0x1E */

    /* ACL */
    FIB_KEY_TYPE_ACL,                     /* 0x1F */

    FIB_KEY_TYPE_NUM
};
typedef enum fib_key_type_e fib_key_type_t;

enum fib_hash_type_e
{
    /* FIB_KEY_TYPE_MAC_DA */
    /* FIB_KEY_TYPE_MAC_SA */
    FIB_HASH_TYPE_MAC,
    /* FIB_KEY_TYPE_FCOE */
    FIB_HASH_TYPE_FCOE,
    /* FIB_KEY_TYPE_FCOE_RPF */
    FIB_HASH_TYPE_FCOE_RPF,
    /* FIB_HASH_TYPE_TRILL_UCAST */
    FIB_HASH_TYPE_TRILL_UCAST,
    /* FIB_HASH_TYPE_TRILL_MCAST */
    FIB_HASH_TYPE_TRILL_MCAST,
    /* FIB_HASH_TYPE_TRILL_MCAST_VLAN */
    FIB_HASH_TYPE_TRILL_MCAST_VLAN,
    /* FIB_KEY_TYPE_ACL */
    FIB_HASH_TYPE_ACL_MAC,
    /* FIB_KEY_TYPE_ACL */
    FIB_HASH_TYPE_ACL_IPV4,
    FIB_HASH_TYPE_NUM
};
typedef enum fib_hash_type_e fib_hash_type_t;

enum fib_dynamic_ds_type_e
{
    FIB_DYNAMIC_DS_MAC_HASH_KEY,
    FIB_DYNAMIC_DS_FWD,
    FIB_DYNAMIC_DS_MAC,
    FIB_DYNAMIC_DS_NUM
};
typedef enum fib_dynamic_ds_type_e fib_dynamic_ds_type_t;

extern int32
drv_greatbelt_fib_lookup(lookup_info_t*, lookup_result_t*);

#endif


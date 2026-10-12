/****************************************************************************
 * drv_lpm_lookup.h  All error code Deinfines, include SDK error code.
 *
 * Copyright:    (c)2011 Centec Networks Inc.  All rights reserved.
 *
 * Modify History:
 * Revision:     V4.3.0
 * Author:       XuZx
 * Date:         2011-07-15
 * Reason:       Create for GreatBelt v4.3.0

 * Revision:     V5.1.0
 * Revisor:      XuZx
 * Date:         2011-12-12
 * Reason:       Revise for GreatBelt v5.1.0
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
#ifndef _DRV_LPM_LOOKUP_H
#define _DRV_LPM_LOOKUP_H

#define LPM_DEFAULT_INDEX_MASK    0xFF
#define DS_LPM_PREFIX_MAX_INDEX   512
#define INVALID_HASH_INDEX_MASK   0xFFFFFFFF

#define POINTER_STAGE_BITS_LEN    2
#define POINTER_OFFSET_BITS_LEN   16
#define POINTER_BITS_LEN          (POINTER_STAGE_BITS_LEN + POINTER_OFFSET_BITS_LEN)
#define NEXTHOP_BITS_LEN          16
#define IPV6_127_64_HASH_BITS_LEN 13
#define IPV6_63_32_HASH_BITS_LEN  13

#define INVALID_POINTER           ((1 << POINTER_BITS_LEN) - 1)
#define INVALID_POINTER_OFFSET    ((1 << POINTER_OFFSET_BITS_LEN) - 1)
#define INVALID_STAGE_INDEX       ((1 << POINTER_STAGE_BITS_LEN) - 1)
#define INVALID_NEXTHOP           ((1 << NEXTHOP_BITS_LEN) - 1)
#define NEXTHOP_MASK              ((1 << NEXTHOP_BITS_LEN) - 1)

#define POINTER_OFFSET_MASK       ((1 << POINTER_OFFSET_BITS_LEN) - 1)
#define POINTER_STAGE_MASK        (((1 << POINTER_BITS_LEN) - 1) - POINTER_OFFSET_MASK)
#define STAGE_OFFSET              (1 << POINTER_OFFSET_BITS_LEN)

enum lpm_pipeline_stage_e
{
    LPM_PIPE_LINE_STAGE0,
    LPM_PIPE_LINE_STAGE1,
    LPM_PIPE_LINE_STAGE2,
    LPM_PIPE_LINE_STAGE3,
    LPM_PIPE_LINE_STAGE_NUM
};
typedef enum lpm_pipeline_stage_e lpm_pipeline_stage_t;

#define STAGE0_OFFSET             (LPM_PIPE_LINE_STAGE0 << POINTER_OFFSET_BITS_LEN)
#define STAGE1_OFFSET             (LPM_PIPE_LINE_STAGE1 << POINTER_OFFSET_BITS_LEN)
#define STAGE2_OFFSET             (LPM_PIPE_LINE_STAGE2 << POINTER_OFFSET_BITS_LEN)
#define STAGE3_OFFSET             (LPM_PIPE_LINE_STAGE3 << POINTER_OFFSET_BITS_LEN)

#define LPM_PIPELINE_STAGE(value)  ((value & POINTER_STAGE_MASK) >> POINTER_OFFSET_BITS_LEN)
#define LPM_POINTER_OFFSET(value)  (value & POINTER_OFFSET_MASK)

struct lpm_pointer_s
{
    uint32 value : (POINTER_BITS_LEN);
    uint32 rsv   : (32 - POINTER_BITS_LEN);
};
typedef struct lpm_pointer_s lpm_pointer_t;

struct lpm_nexthop_s
{
    uint32 value : (NEXTHOP_BITS_LEN);
    uint32 rsv   : (32 - NEXTHOP_BITS_LEN);
};
typedef struct lpm_nexthop_s lpm_nexthop_t;

struct ipv6_mcast128_info_s
{
    uint32 ipv6_mcast128_result_valid  : 1;
    uint32 ipv6_mcast128_result        : 14;
    uint32 ipv6_mcast128_nexthop       : 16;
};
typedef struct ipv6_mcast128_info_s ipv6_mcast128_info_t;

struct lpm_info_s
{
    uint32 free                 : 1;
    uint32 free_key_index       : 15;
    uint32 index_mask           : 8;
    uint32 valid                : 1;
    uint32 nexthop_valid        : 1;
    uint32 pointer_valid        : 1;
    uint32 rsv                  : 5;

    lpm_pointer_t  pointer;
    lpm_nexthop_t  nexthop;

    void* extra;
};
typedef struct lpm_info_s lpm_info_t;

enum lpm_hash_mode_e
{
    LPM_HASH_MODE_IPV4_UC,
    LPM_HASH_MODE_IPV4_MC,
    LPM_HASH_MODE_IPV6_UC,
    LPM_HASH_MODE_IPV6_MC,
    LPM_HASH_MODE_NUM
};
typedef enum lpm_hash_mode_e lpm_hash_mode_t;

enum lpm_hash_type_e
{
    LPM_HASH_TYPE_IPV4_16,
    LPM_HASH_TYPE_IPV4_SG,
    LPM_HASH_TYPE_IPV6_LOW32,
    LPM_HASH_TYPE_IPV6_MID32,
    LPM_HASH_TYPE_IPV4_8,
    LPM_HASH_TYPE_IPV4_XG,
    LPM_HASH_TYPE_IPV6_HIGH32,
    LPM_HASH_TYPE_IPV6_SG1,
    LPM_HASH_TYPE_IPV6_SG0,
    LPM_HASH_TYPE_IPV6_XG,
    LPM_HASH_TYPE_IPV4_NAT_SA_PORT,
    LPM_HASH_TYPE_IPV4_NAT_SA,
    LPM_HASH_TYPE_IPV6_NAT_SA_PORT,
    LPM_HASH_TYPE_IPV6_NAT_SA,
    LPM_HASH_TYPE_IPV4_NAT_DA_PORT,
    LPM_HASH_TYPE_IPV6_NAT_DA_PORT,
    LPM_HASH_TYPE_NUM,
    LPM_HASH_TYPE_INVALID = LPM_HASH_TYPE_NUM
};
typedef enum lpm_hash_type_e lpm_hash_type_t;

enum lpm_lookup_key_type_e
{
    LPM_LOOKUP_KEY_TYPE_EMPTY,
    LPM_LOOKUP_KEY_TYPE_NEXTHOP,
    LPM_LOOKUP_KEY_TYPE_POINTER,
    LPM_LOOKUP_KEY_TYPE_RESERVED
};
typedef enum lpm_lookup_key_type_e lpm_lookup_key_type_t;

enum lpm_hash_key_type_e
{
    LPM_HASH_KEY_TYPE_NEXTHOP,
    LPM_HASH_KEY_TYPE_POINTER,
    LPM_HASH_KEY_TYPE_INVALID
};
typedef enum lpm_hash_key_type_e lpm_hash_key_type_t;

enum lpm_hash_level_e
{
    LPM_HASH_LEVEL0,
    LPM_HASH_LEVEL_NUM
};
typedef enum lpm_hash_level_e lpm_hash_level_t;

enum lpm_crc_polynomial_e
{
    LPM_CRC_POLYNOMIAL0,
    LPM_CRC_POLYNOMIAL1,
    LPM_CRC_POLYNOMIAL_NUM
};
typedef enum lpm_crc_polynomial_e lpm_crc_polynomial_t;

struct ds_lpm_prefix_s
{
    uint32 nexthop1                                                         : 1;
    uint32 type                                                             : 2;
    uint32 rsv_0                                                            : 29;

    uint32 ip                                                               : 8;
    uint32 mask                                                             : 8;
    uint32 nexthop0                                                         : 16;
};
typedef struct ds_lpm_prefix_s ds_lpm_prefix_t;

extern int32
drv_greatbelt_lpm_lookup(lookup_info_t*, lookup_result_t*);

#endif


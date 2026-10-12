/****************************************************************************
 * drv_queue_lookup.h  All error code Deinfines, include SDK error code.
 *
 * Copyright:    (c)2011 Centec Networks Inc.  All rights reserved.
 *
 * Modify History:
 * Revision:     V4.3.0
 * Author:       XuZx
 * Date:         2011-07-15
 * Reason:       Create for GreatBelt v4.3.0
 *
 * Revision:     v5.9.0
 * Revisor:      XuZx
 * Date:         2012-02-03.
 * Reason:       Revise for GreatBelt v5.9.0
 ****************************************************************************/
#ifndef _DRV_QUEUE_LOOKUP_H
#define _DRV_QUEUE_LOOKUP_H

enum queue_key_type_e
{
    QUEUE_KEY_TYPE_KEY,
    QUEUE_KEY_TYPE_NUM
};
typedef enum queue_key_type_e queue_key_type_t;

#if (HOST_IS_LE == 1)
struct ds_queue_hash_cfg_s
{
    uint32 sgmac                                                            : 6;
    uint32 rsv_0                                                            : 2;
    uint32 hash_type                                                        : 7;
    uint32 rsv_1                                                            : 1;
    uint32 queue_base                                                       : 10;
    uint32 rsv_2                                                            : 6;

    uint32 dest_chip_id                                                     : 5;
    uint32 mcast                                                            : 1;
    uint32 rsv_3                                                            : 2;
    uint32 channel_id                                                       : 6;
    uint32 rsv_4                                                            : 1;
    uint32 valid                                                            : 1;
    uint32 dest_queue                                                       : 16;

    uint32 fid                                                              : 14;
    uint32 rsv_5                                                            : 2;
    uint32 service_id                                                       : 14;
    uint32 rsv_6                                                            : 2;

};
typedef struct ds_queue_hash_cfg_s ds_queue_hash_cfg_t;
#else
struct ds_queue_hash_cfg_s
{
    uint32 rsv_0                                                            : 6;
    uint32 queue_base                                                      : 10;
    uint32 rsv_1                                                            : 1;
    uint32 hash_type                                                        : 7;
    uint32 rsv_2                                                            : 2;
    uint32 sgmac                                                            : 6;

    uint32 dest_queue                                                       : 16;
    uint32 valid                                                            : 1;
    uint32 rsv_3                                                            : 1;
    uint32 channel_id                                                       : 6;
    uint32 rsv_4                                                            : 2;
    uint32 mcast                                                            : 1;
    uint32 dest_chip_id                                                     : 5;

    uint32 rsv_5                                                            : 2;
    uint32 service_id                                                       : 14;
    uint32 rsv_6                                                            : 2;
    uint32 fid                                                              : 14;
};
typedef struct ds_queue_hash_cfg_s ds_queue_hash_cfg_t;
#endif

enum queue_hash_key_entry_record_num_e
{
    QUEUE_HASH_KEY_ENTRY_RECORD0,
    QUEUE_HASH_KEY_ENTRY_RECORD1,
    QUEUE_HASH_KEY_ENTRY_RECORD2,
    QUEUE_HASH_KEY_ENTRY_RECORD3,
    QUEUE_HASH_KEY_ENTRY_RECORD_NUM
};
typedef enum queue_hash_key_entry_record_num_e queue_hash_key_entry_record_num_t;

enum queue_hash_level_e
{
    QUEUE_HASH_LEVEL0,
    QUEUE_HASH_LEVEL_NUM
};
typedef enum queue_hash_level_e queue_hash_level_t;

enum queue_crc_polynomial_e
{
    QUEUE_CRC_POLYNOMIAL0,
    QUEUE_CRC_POLYNOMIAL_NUM
};
typedef enum queue_crc_polynomial_e queue_crc_polynomial_t;

extern int32
drv_greatbelt_queue_lookup(lookup_info_t*, lookup_result_t*);

#endif


/****************************************************************************
 * drv_hash_calculate.h  Provides driver hash calculate function declaration.
 *
 * Copyright (C) 2011 Centec Networks Inc.  All rights reserved.

 * Modify History:
 * Revision:    V2.0.
 * Author:      ZhouW
 * Date:        2011-05-12.
 * Reason:      Create for GreatBelt v2.0.
 *
 * Revision:    V4.1.1
 * Revisor      XuZx.
 * Date:        2011-7-7.
 * Reason:      Create for GreatBelt 4.2.1
 *
 * Revision:    V4.7.1
 * Revisor      XuZx.
 * Date:        2011-7-29.
 * Reason:      Revise for GreatBelt 4.7.1
 *
 * Revision:    V5.0.0
 * Revisor:     XuZx.
 * Date:        2010-11-11.
 * Reason:      Revise for GreatBelt v5.0.0.
 *
 * Revision:    V5.6.0
 * Revisor:     XuZx.
 * Date:        2012-01-10.
 * Reason:      Revise for GreatBelt v5.6.0.
 *
 * Revision:    v5.9.0
 * Revisor:     XuZx
 * Date:        2012-02-03.
 * Reason:      Revise for GreatBelt v5.9.0
 ****************************************************************************/
#ifndef _DRV_HASH_CALCULATE_H_
#define _DRV_HASH_CALCULATE_H_

#include "sal.h"
#include "greatbelt/include/drv_lib.h"

#define USERID_CRC_DATA_WIDTH                    160
#define FIB_CRC_DATA_WIDTH                       160
#define LPM_CRC_DATA_WIDTH                       160
#define QUEUE_CRC_DATA_WIDTH                      70
#define LPM_POINTER_DATA_WIDTH                    80
#define LPM_MID_DATA_WIDTH                        32
#define LPM_VRF_ID_DATA_WIDTH                    140

#define MAX_PARSER_DATA_WIDTH                    512

#define USERID_CRC_POLYNOMIAL0_WIDTH              12
#define USERID_CRC_POLYNOMIAL1_WIDTH              15
#define USERID_CRC_POLYNOMIAL2_WIDTH              16
#define USERID_CRC_POLYNOMIAL3_WIDTH              16

#define USERID_LEVEL0_CRC_WIDTH                   16
#define USERID_LEVEL1_CRC_WIDTH                   16

#define FIB_CRC_POLYNOMIAL0_WIDTH                 16
#define FIB_CRC_POLYNOMIAL1_WIDTH                 16
#define FIB_CRC_POLYNOMIAL2_WIDTH                 16
#define FIB_CRC_POLYNOMIAL3_WIDTH                 16
#define FIB_CRC_POLYNOMIAL4_WIDTH                 15
#define FIB_CRC_POLYNOMIAL5_WIDTH                 14
#define FIB_CRC_POLYNOMIAL6_WIDTH                 12
#define FIB_CRC_POLYNOMIAL7_WIDTH                 14

#define LPM_CRC_POLYNOMIAL0_WIDTH                 16
#define LPM_CRC_POLYNOMIAL1_WIDTH                 14

#define QUEUE_CRC_POLYNOMIAL_WIDTH                 8

#define LPM_MID_CRC_WIDTH                         13
#define LPM_POINTER_BUCKET_WIDTH                  13
#define LPM_MID_BUCKET_WIDTH                      13

#define BUCKET_WIDTH_USERID                       14
#define BUCKET_WIDTH_FIB                          14
#define BUCKET_WIDTH_LPM                          14
#define BUCKET_WIDTH_QUEUE                         7

#define LPM_MID_BUCKET_MASK                       ((1 << LPM_MID_BUCKET_WIDTH) - 1)

extern uint32
drv_greatbelt_hash_calculate_lpm_pointer(uint8*, uint32);

extern uint32
drv_greatbelt_hash_calculate_lpm_mid(uint8*, uint32);

extern int32
drv_greatbelt_hash_calculate_index(key_info_t* p_key_info, key_result_t* p_key_result);

extern uint32
drv_greatbelt_hash_calculate_poly(uint32* p_seed, uint8* p_data, uint32 bit_len, uint32 poly, uint32 poly_len, uint32 bucket_width);

extern uint32
drv_greatbelt_hash_calculate_xor(uint8* p_data, uint32 data_width, uint32 bucket_width, uint8 is_parser);

extern uint32
drv_greatbelt_hash_crc_data(uint32* p_seed, uint8* p_data, uint32 data_width, uint32 poly, uint32 poly_len, uint32 bucket_width);

#endif


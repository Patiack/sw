/****************************************************************************
 * drv_hash_calculate.c: All hash arithmetic define.
 *
 * Copyright: (c)2011 Centec Networks Inc. All rights reserved.
 *
 * Modify History:
 * Revision:     V2.0.
 * Author:       ZhouW
 * Date:         2011-05-12.
 * Reason:       Create for GreatBelt v2.0.
 *
 * Revision:     V4.2.1
 * Revisor:      XuZx
 * Date:         2011-07-08.
 * Reason:       Revise for GreatBelt v4.2.1.
 *
 * Revision:     V4.5.1
 * Revisor:      XuZx
 * Date:         2011-07-27.
 * Reason:       Revise for GreatBelt v4.5.1.
 *
 * Revision:     V5.0.0
 * Revisor:      XuZx.
 * Date:         2011-11-11.
 * Reason:       Revise for GreatBelt v5.0.0.
 *
 * Revision:     V5.1.0
 * Revisor:      XuZx.
 * Date:         2011-12-16.
 * Reason:       Revise for GreatBelt v5.1.0.
 *
 * Revision:     V5.6.0
 * Revisor:      XuZx.
 * Date:         2012-01-10.
 * Reason:       Revise for GreatBelt v5.6.0.
 *
 * Revision:     V5.9.0
 * Revisor:      XuZx.
 * Date:         2012-02-02.
 * Reason:       Revise for GreatBelt v5.9.0.
 ****************************************************************************/
#include "greatbelt/include/drv_lib.h"

#define DRV_POLY_ANSI_CRC16   0x00018005  /* polynomial: (0 2 15 16) */
#define DRV_POLY_CCITT_CRC16  0x00011021  /* polynomial: (0 5 12 16) */
#define DRV_POLY_T10DIF_CRC16 0x00018BB7  /* polynomial: (0 1 2 4 5 7 8 9 11 15 16) */
#define DRV_POLY_DNP_CRC16    0x00013D65  /* polynomial: (0 2 5 6 8 10 11 12 13 16) */
#define DRV_POLY_CRC15        0x0000C599  /* polynomial: (0 3 4 7 8 10 14 15) */
#define DRV_POLY_CRC14        0x00004805  /* polynomial: (0 2 11 14) */
#define DRV_POLY_CRC12        0x0000180F  /* polynomial: (0 1 2 3 11 12) */
#define DRV_POLY_CCITT_CRC8   0x00000107  /* polynomial: (0 1 2 8) */
#define DRV_POLY_CRC8         0x000001D5  /* polynomial: (0 2 4 6 7 8) */
#define DRV_POLY_INVALID      0xFFFFFFFF

struct drv_hash_s
{
    uint32 poly;
    uint32 poly_len;
    uint32 bucket_width;
};
typedef struct drv_hash_s drv_hash_t;

uint32
drv_greatbelt_hash_calculate_xor(uint8* p_data, uint32 data_width, uint32 bucket_width, uint8 is_parser)
{
    uint32 i = 0;
    uint32 j = 0;
    uint32 bit_index = 0;
    uint32 xor_num = data_width / bucket_width;
    uint16* p_xor = NULL;
    uint16 result = 0;
    uint8  bit = 0;
    uint32 len = ((data_width / bucket_width) + 1) * sizeof(uint16);

    p_xor = (uint16*)sal_malloc(len);

    DRV_PTR_VALID_CHECK(p_xor);
    sal_memset(p_xor, 0, len);
    if (data_width % bucket_width)
    {
        xor_num++;
    }

    for (i = 0; i < xor_num; i++)
    {
        for (j = 0; j < bucket_width; j++)
        {
            bit_index = (i * bucket_width) + j;

            if (bit_index >= data_width)
            {
                break;
            }

            if (is_parser)
            {
                bit = IS_BIT_SET(p_data[bit_index / 8], (bit_index % 8));
            }
            else
            {
                bit = IS_BIT_SET(p_data[bit_index / 8], (7 - bit_index % 8));
            }
            p_xor[i] |= bit << j;
        }
    }

    result = p_xor[0];

    for (i = 1; i < xor_num; i++)
    {
        result ^= p_xor[i];
    }

    sal_free(p_xor);

    return result;
}

uint32
drv_greatbelt_hash_calculate_poly(uint32* p_seed, uint8* p_data, uint32 bit_len, uint32 poly, uint32 poly_len, uint32 bucket_width)
{
    uint32 crc = 0;
    uint32 i = 0;
    uint32 msb_bit = 0;
    uint32 poly_mask = 0;
    uint32 topone = 0;

    poly_mask = (1 << poly_len) - 1;
    topone = (1 << (poly_len - 1));
    crc = *p_seed & poly_mask;

    for (i = 0; i < bit_len; i++)
    {
        msb_bit = ((p_data[i / 8] >> (7 - (i % 8))) & 0x1) << (poly_len - 1);
        crc ^= msb_bit;
        crc = (crc << 1) ^ ((crc & topone) ? poly : 0);
    }
    *p_seed = crc;
    crc &= (1 << bucket_width) - 1;

    return crc;
}

static drv_hash_t drv_userid_hash[USERID_CRC_POLYNOMIAL_NUM] =
{
    {DRV_POLY_CRC12,       USERID_CRC_POLYNOMIAL0_WIDTH, BUCKET_WIDTH_USERID},
    {DRV_POLY_CRC15,       USERID_CRC_POLYNOMIAL1_WIDTH, BUCKET_WIDTH_USERID},
    {DRV_POLY_ANSI_CRC16,  USERID_CRC_POLYNOMIAL2_WIDTH, BUCKET_WIDTH_USERID},
    {DRV_POLY_CCITT_CRC16, USERID_CRC_POLYNOMIAL3_WIDTH, BUCKET_WIDTH_USERID}
};

static drv_hash_t drv_fib_hash[FIB_CRC_POLYNOMIAL_NUM] =
{
    {DRV_POLY_ANSI_CRC16,   FIB_CRC_POLYNOMIAL0_WIDTH, BUCKET_WIDTH_FIB},
    {DRV_POLY_CCITT_CRC16,  FIB_CRC_POLYNOMIAL1_WIDTH, BUCKET_WIDTH_FIB},
    {DRV_POLY_T10DIF_CRC16, FIB_CRC_POLYNOMIAL2_WIDTH, BUCKET_WIDTH_FIB},
    {DRV_POLY_DNP_CRC16,    FIB_CRC_POLYNOMIAL3_WIDTH, BUCKET_WIDTH_FIB},
    {DRV_POLY_CRC15,        FIB_CRC_POLYNOMIAL4_WIDTH, BUCKET_WIDTH_FIB},
    {DRV_POLY_CRC14,        FIB_CRC_POLYNOMIAL5_WIDTH, BUCKET_WIDTH_FIB},
    {DRV_POLY_CRC12,        FIB_CRC_POLYNOMIAL6_WIDTH, BUCKET_WIDTH_FIB},
    {DRV_POLY_INVALID,      FIB_CRC_POLYNOMIAL7_WIDTH, BUCKET_WIDTH_FIB}
};

static drv_hash_t drv_lpm_hash[LPM_CRC_POLYNOMIAL_NUM] =
{
    {DRV_POLY_CCITT_CRC16, LPM_CRC_POLYNOMIAL0_WIDTH, BUCKET_WIDTH_LPM},
    {DRV_POLY_CRC14,       LPM_CRC_POLYNOMIAL1_WIDTH, BUCKET_WIDTH_LPM}
};

static drv_hash_t drv_queue_hash[QUEUE_HASH_LEVEL_NUM] =
{
    {DRV_POLY_CCITT_CRC8, QUEUE_CRC_POLYNOMIAL_WIDTH, BUCKET_WIDTH_QUEUE}
};

static drv_hash_t* drv_hash[HASH_MODULE_NUM] = {drv_userid_hash, drv_fib_hash, drv_lpm_hash, drv_queue_hash};

STATIC int32
_drv_greatbelt_hash_calcaulate_check_key_info(key_info_t* p_key_info)
{
    int32 ret = DRV_E_NONE;

    if (HASH_MODULE_USERID == p_key_info->hash_module)
    {
        if (p_key_info->polynomial_index >= USERID_CRC_POLYNOMIAL_NUM)
        {
            ret = DRV_E_INVALID_CRC_POLYNOMIAL_TYPE;
        }
    }
    else if (HASH_MODULE_FIB == p_key_info->hash_module)
    {
        if (p_key_info->polynomial_index >= FIB_CRC_POLYNOMIAL_NUM)
        {
            ret = DRV_E_INVALID_CRC_POLYNOMIAL_TYPE;
        }
    }
    else if (HASH_MODULE_LPM == p_key_info->hash_module)
    {
        if (p_key_info->polynomial_index >= LPM_CRC_POLYNOMIAL_NUM)
        {
            ret = DRV_E_INVALID_CRC_POLYNOMIAL_TYPE;
        }
    }
    else if (HASH_MODULE_QUEUE == p_key_info->hash_module)
    {
        if (p_key_info->polynomial_index >= QUEUE_CRC_POLYNOMIAL_NUM)
        {
            ret = DRV_E_INVALID_CRC_POLYNOMIAL_TYPE;
        }
    }
    else
    {
        ret = DRV_E_INVALID_CRC_POLYNOMIAL_TYPE;
    }

    return ret;
}

uint32
drv_greatbelt_hash_crc_data(uint32* p_seed, uint8* p_data, uint32 data_width, uint32 poly, uint32 poly_len, uint32 bucket_width)
{
    if (DRV_POLY_INVALID == poly)
    {
        return drv_greatbelt_hash_calculate_xor(p_data, data_width, bucket_width, FALSE);
    }
    else
    {
        return drv_greatbelt_hash_calculate_poly(p_seed, p_data, data_width, poly, poly_len, bucket_width);
    }
}


uint32
drv_greatbelt_hash_calculate_lpm_pointer(uint8* p_data, uint32 bit_num)
{
    uint32 seed = 0;

    return drv_greatbelt_hash_calculate_poly(&seed, p_data, LPM_POINTER_DATA_WIDTH, DRV_POLY_CCITT_CRC16, 16, bit_num);
}

uint32
drv_greatbelt_hash_calculate_lpm_mid(uint8* p_data, uint32 bit_num)
{
    uint32 seed = 0;

    return drv_greatbelt_hash_calculate_poly(&seed, p_data, LPM_MID_DATA_WIDTH, DRV_POLY_CCITT_CRC16, 16, bit_num);
}

int32
drv_greatbelt_hash_calculate_index(key_info_t* p_key_info, key_result_t* p_key_result)
{
    uint32 seed = 0;
    uint32 bucket_width = 0;
    uint32 poly_len = 0;
    uint32 poly = 0;
    uint32 polynomial_index = 0;
    hash_module_t hash_module = 0;
    uint32 data_width[HASH_MODULE_NUM] = {USERID_CRC_DATA_WIDTH, FIB_CRC_DATA_WIDTH, LPM_CRC_DATA_WIDTH, QUEUE_CRC_DATA_WIDTH};

    DRV_PTR_VALID_CHECK(p_key_info);
    DRV_PTR_VALID_CHECK(p_key_result);
    p_key_result->bucket_index = INVALID_HASH_INDEX_MASK;
    DRV_IF_ERROR_RETURN(_drv_greatbelt_hash_calcaulate_check_key_info(p_key_info));

    hash_module = p_key_info->hash_module;
    polynomial_index = p_key_info->polynomial_index;

    bucket_width = drv_hash[hash_module][polynomial_index].bucket_width;
    poly_len = drv_hash[hash_module][polynomial_index].poly_len;
    poly = drv_hash[hash_module][polynomial_index].poly;

    p_key_result->bucket_index = drv_greatbelt_hash_crc_data(&seed, p_key_info->p_hash_key, data_width[hash_module], poly, poly_len, bucket_width);


    return DRV_E_NONE;
}


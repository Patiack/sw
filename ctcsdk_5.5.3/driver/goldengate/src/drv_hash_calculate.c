/****************************************************************************
 * drv_hash_calculate.c  Provides driver hash calculate function declaration.
 *
 * Copyright (C) 2012 Centec Networks Inc.  All rights reserved.

 * Modify History:
 * Revision:    V1.0.
 * Author:      JiangSZ
 * Date:        2012-11-27.
 * Reason:      Create for goldenGate V1.0.
 *
 ****************************************************************************/
#include "goldengate/include/drv_hash_calculate.h"

uint32
hash_crc(uint32 seed, uint8 *data, uint32 bit_len, uint32 poly, uint32 poly_len)
{
    uint32 crc = 0;
    int32 i = 0;
    uint32 msb_bit = 0;
    uint32 poly_mask = 0;
    uint32 topone=0;
    poly_mask = (1<<poly_len) -1;
    topone = (1 << (poly_len-1));
    crc = seed & poly_mask;

    for (i = (bit_len-1); i >=0; i--) {  /* bits*/
      msb_bit = ((data[i/8]>>(i%8))&0x1) << (poly_len-1);
      crc ^= msb_bit;
      crc = (crc << 1) ^ ((crc & topone) ? poly: 0);
       /*bit_len --;*/
    }

    crc &= poly_mask;
    return crc;
}

uint16
hash_xor16(uint16* data, uint32 bit_len)
{
    uint32 i = 0;
    uint16 result = 0;
    uint16 len = bit_len/16;
    uint16 last_bits = bit_len%16;

    for (i = 0; i < len; i++)
    {
        result ^= data[i];
    }

    if (last_bits)
    {
        result ^= (data[len]&((1<<last_bits)-1));
    }

    return result;
}

uint8
hash_xor8(uint8* data, uint32 bit_len)
{
    uint32 i = 0;
    uint16 result = 0;
    uint16 len = bit_len/8;
    uint16 last_bits = bit_len%8;

    for (i = 0; i < len; i++)
    {
        result ^= data[i];
    }

    if (last_bits)
    {
        result ^= (data[len]&((1<<last_bits)-1));
    }

    return result;
}



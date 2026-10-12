/**
 @file dal.h

 @author  Copyright (C) 2012 Centec Networks Inc.  All rights reserved.

 @date 2012-4-9

 @version v2.0

*/
#ifndef _DRV_CHIP_CTRL_H_
#define _DRV_CHIP_CTRL_H_
#ifdef __cplusplus
extern "C" {
#endif

#define DRV_PCI_CMD_STATUS  0x0
#define DRV_PCI_ADDR  0x4
#define DRV_PCI_DATA_BUF  0x8
#define DRV_PCI_CHECK_STATUS 0x0c0ec000

#define DRV_I2C_REQ_READ     0x80
#define DRV_I2C_REQ_WRITE    0x0
#define DRV_I2C_WRDATA_OFFSET  0
#define DRV_I2C_ADDRCMD_OFFSET 4
#define DRV_I2C_RDDATA_OFFSET 5
#define DRV_I2C_STATUS_OFFSET 9

#define DRV_I2C_CHIP_READ    0
#define DRV_I2C_CHIP_WRITE    1
#define DRV_I2C_CHIP_WRDATA_REG_OFFSET  0x18
#define DRV_I2C_CHIP_ADDR_REG_OFFSET  0x19
#define DRV_I2C_CHIP_STATUS_REG_OFFSET  0x1b
#define DRV_I2C_CHIP_DATA_REG_OFFSET  0x1a

#define DRV_I2C_RCBUS_WRDATA_REG_OFFSET  0x12
#define DRV_I2C_RCBUS_ADDR_REG_OFFSET  0x11
#define DRV_I2C_RCBUS_CMD_REG_OFFSET  0x13

#define DRV_CMD_TIMEOUT   0x6400

#define DRV_HSS12G_MACRO_NUM   4

#define DRV_ACCESS_DAL(func) \
    { \
        int32 rv; \
        if ((rv = (func)) < 0)  \
        {  \
            return rv; \
        } \
    }

#define DRV_ACCESS_DAL_WITH_UNLOCK(func, lock) \
    { \
        int32 rv; \
        if ((rv = (func)) < 0)  \
        {  \
            sal_mutex_unlock(lock); \
            return rv; \
        } \
    }

#define DRV_CONVT_LEN(len, len_convt) \
    { \
        uint8 temp = len; \
        do \
        { \
            temp++; \
        } while ((temp = < 16) && (temp & (temp - 1))); \
        len_convt = temp; \
    }

#if (HOST_IS_LE == 0)
#define DRV_SWAP32(val) (uint32)(val)
#define DRV_SWAP16(val) (uint16)(val)

/* pci cmd struct define */
typedef struct drv_pci_cmd_status_s
{
    uint32 pcieReqOverlap   : 1;
    uint32 wrReqState       : 3;
    uint32 pciePoison       : 1;
    uint32 rcvregInProc     : 1;
    uint32 regInProc        : 1;
    uint32 reqProcAckCnt    : 5;
    uint32 reqProcAckError  : 1;
    uint32 reqProcTimeout   : 1;
    uint32 reqProcError     : 1;
    uint32 reqProcDone      : 1;
    uint32 pcieDataError    : 1;
    uint32 pcieReqError     : 1;
    uint32 reserved         : 1;
    uint32 cmdDataLen       : 5;
    uint32 cmdEntryWords    : 4;
    uint32 pcieReqCmdChk    : 3;
    uint32 cmdReadType      : 1;
} drv_pci_cmd_status_t;

#else

#define DRV_SWAP32(val) \
    ((uint32)( \
         (((uint32)(val) & (uint32)0x000000ffUL) << 24) | \
         (((uint32)(val) & (uint32)0x0000ff00UL) << 8) | \
         (((uint32)(val) & (uint32)0x00ff0000UL) >> 8) | \
         (((uint32)(val) & (uint32)0xff000000UL) >> 24)))

#define DRV_SWAP16(val) \
    ((uint16)( \
         (((uint16)(val) & (uint16)0x00ffU) << 8) | \
         (((uint16)(val) & (uint16)0xff00U) >> 8)))

typedef struct drv_pci_cmd_status_s
{
    uint32 cmdReadType      : 1;       /* bit0 */
    uint32 pcieReqCmdChk    : 3;      /* bit1~3 */
    uint32 cmdEntryWords    : 4;      /* bit4~7 */
    uint32 cmdDataLen       : 5;        /* bit8~12 */
    uint32 reserved : 1;                    /* bit13 */
    uint32 pcieReqError     : 1;
    uint32 pcieDataError    : 1;
    uint32 reqProcDone      : 1;
    uint32 reqProcError     : 1;
    uint32 reqProcTimeout   : 1;
    uint32 reqProcAckError  : 1;
    uint32 reqProcAckCnt    : 5;
    uint32 regInProc        : 1;
    uint32 rcvregInProc     : 1;
    uint32 pciePoison       : 1;
    uint32 wrReqState       : 3;
    uint32 pcieReqOverlap   : 1;
} drv_pci_cmd_status_t;
#endif

typedef union drv_pci_cmd_status_u_e
{
    drv_pci_cmd_status_t cmd_status;
    uint32 val;
} drv_pci_cmd_status_u_t;

extern int32 drv_greatbelt_chip_hss_mutex_init(uint8 chip_id_offset);
extern int32 drv_greatbelt_chip_i2c_mutex_init(uint8 chip_id_offset);
extern int32 drv_greatbelt_chip_pci_mutex_init(uint8 chip_id_offset);
extern int32 drv_greatbelt_chip_read(uint8 lchip, uint32 offset, uint32* value);
extern int32 drv_greatbelt_chip_write(uint8 lchip, uint32 offset, uint32 p_value);
extern int32 drv_greatbelt_chip_set_access_type(drv_access_type_t access_type);
extern int32 drv_greatbelt_chip_get_access_type(drv_access_type_t* p_access_type);
extern int32 drv_greatbelt_pci_read_chip(uint8 lchip, uint32 offset, uint32 len, uint32* value);
extern int32 drv_greatbelt_pci_write_chip(uint8 lchip, uint32 offset, uint32 len, uint32* value);
extern int32 drv_greatbelt_i2c_read_chip(uint8 lchip, uint32 offset, uint32* value);
extern int32 drv_greatbelt_i2c_write_chip(uint8 lchip, uint32 offset, uint32 value);
extern int32 drv_greatbelt_i2c_read_local(uint8 lchip, uint32 offset, uint32* value);
extern int32 drv_greatbelt_i2c_write_local(uint8 lchip, uint32 offset, uint32 value);
extern int32 drv_greatbelt_i2c_read_hss6g(uint8 lchip, uint32 offset, uint16* value);
extern int32 drv_greatbelt_i2c_write_hss6g(uint8 lchip, uint32 offset, uint16 value, uint16 mask);
extern int32 drv_greatbelt_chip_read_hss12g(uint8 lchip, uint8 hssid, uint32 addr, uint16* p_data);
extern int32 drv_greatbelt_chip_write_hss12g(uint8 lchip, uint8 hssid, uint32 addr, uint16 data);
extern int32 drv_greatbelt_chip_read_ext(uint8 lchip, uint32 offset, uint32* p_value, uint32 length);
extern int32 drv_greatbelt_chip_write_ext(uint8 lchip, uint32 offset, uint32* p_value, uint32 length);
#ifdef __cplusplus
}
#endif

#endif


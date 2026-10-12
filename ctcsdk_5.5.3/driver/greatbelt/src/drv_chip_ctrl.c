/**
 @file drv_chip_io.c

 @date 2011-10-09

 @version v4.28.2

 The file performs adapter layer between drv and dal
*/
#include "sal.h"
#include "greatbelt/include/drv_lib.h"
#include "greatbelt/include/drv_chip_ctrl.h"
/* ut test */
#include "dal.h"

static sal_mutex_t* p_gb_pci_mutex[MAX_LOCAL_CHIP_NUM];
static sal_mutex_t* p_gb_i2c_mutex[MAX_LOCAL_CHIP_NUM];
static sal_mutex_t* p_gb_hss_mutex[MAX_LOCAL_CHIP_NUM];

extern drv_access_type_t  g_gb_access_type;
extern dal_op_t g_dal_op;

#define DRV_PCI_LOCK(chip_id_offset)          sal_mutex_lock(p_gb_pci_mutex[chip_id_offset])
#define DRV_PCI_UNLOCK(chip_id_offset)        sal_mutex_unlock(p_gb_pci_mutex[chip_id_offset])
#define DRV_I2C_LOCK(chip_id_offset)          sal_mutex_lock(p_gb_i2c_mutex[chip_id_offset])
#define DRV_I2C_UNLOCK(chip_id_offset)        sal_mutex_unlock(p_gb_i2c_mutex[chip_id_offset])
#define DRV_HSS_LOCK(chip_id_offset)           if (p_gb_hss_mutex[chip_id_offset]) sal_mutex_lock(p_gb_hss_mutex[chip_id_offset])
#define DRV_HSS_UNLOCK(chip_id_offset)       if (p_gb_hss_mutex[chip_id_offset]) sal_mutex_unlock(p_gb_hss_mutex[chip_id_offset])

int32
drv_greatbelt_chip_pci_mutex_init(uint8 chip_id_offset)
{
    int32 ret;

    DRV_CHIP_ID_VALID_CHECK(chip_id_offset + drv_gb_init_chip_info.drv_init_chipid_base);

    ret = sal_mutex_create(&p_gb_pci_mutex[chip_id_offset]);
    if (ret || (!p_gb_pci_mutex[chip_id_offset]))
    {
        return DRV_E_FAIL_CREATE_MUTEX;
    }

    return DRV_E_NONE;
}

int32
drv_greatbelt_chip_i2c_mutex_init(uint8 chip_id_offset)
{
    int32 ret;

    DRV_CHIP_ID_VALID_CHECK(chip_id_offset + drv_gb_init_chip_info.drv_init_chipid_base);

    ret = sal_mutex_create(&p_gb_i2c_mutex[chip_id_offset]);
    if (ret || (!p_gb_i2c_mutex[chip_id_offset]))
    {
        return DRV_E_FAIL_CREATE_MUTEX;
    }

    return DRV_E_NONE;
}

int32
drv_greatbelt_chip_hss_mutex_init(uint8 chip_id_offset)
{
    int32 ret;

    DRV_CHIP_ID_VALID_CHECK(chip_id_offset + drv_gb_init_chip_info.drv_init_chipid_base);

    ret = sal_mutex_create(&p_gb_hss_mutex[chip_id_offset]);
    if (ret || (!p_gb_hss_mutex[chip_id_offset]))
    {
        return DRV_E_FAIL_CREATE_MUTEX;
    }

    return DRV_E_NONE;
}
/**
 @brief common read interface
*/
int32
drv_greatbelt_chip_read(uint8 lchip, uint32 offset, uint32* p_value)
{
    int32 ret = 0;
    dal_access_type_t dal_access;

    /* for emulation */
    ret = dal_get_device_access_type(&dal_access);
    if (dal_access == DAL_SPECIAL_EMU)
    {
        ret = g_dal_op.pci_read(lchip, offset, p_value);
        if (ret < 0)
        {
            return ret;
        }

        return DRV_E_NONE;
    }

    switch (g_gb_access_type)
    {
    case DRV_PCI_ACCESS:
        DRV_IF_ERROR_RETURN(drv_greatbelt_pci_read_chip(lchip, offset, 1, p_value));
        break;

    case DRV_I2C_ACCESS:
        DRV_IF_ERROR_RETURN(drv_greatbelt_i2c_read_chip(lchip, offset, p_value));
        break;

    default:
        return DRV_E_INVALID_ACCESS_TYPE;
    }

    return DRV_E_NONE;
}

int32
drv_greatbelt_chip_read_ext(uint8 lchip, uint32 offset, uint32* p_value, uint32 length)
{
    DRV_IF_ERROR_RETURN(drv_greatbelt_pci_read_chip(lchip, offset, length, p_value));
    return DRV_E_NONE;
}

/**
 @brief common write interface
*/
int32
drv_greatbelt_chip_write(uint8 lchip, uint32 offset, uint32 value)
{
    int32 ret = 0;
    dal_access_type_t dal_access;

    /* for emulation */
    ret = dal_get_device_access_type(&dal_access);
    if (dal_access == DAL_SPECIAL_EMU)
    {
        ret = g_dal_op.pci_write(lchip, offset, value);
        if (ret < 0)
        {
            return ret;
        }

        return DRV_E_NONE;
    }

    switch (g_gb_access_type)
    {
    case DRV_PCI_ACCESS:
        DRV_IF_ERROR_RETURN(drv_greatbelt_pci_write_chip(lchip, offset, 1, &value));
        break;

    case DRV_I2C_ACCESS:
        DRV_IF_ERROR_RETURN(drv_greatbelt_i2c_write_chip(lchip, offset, value));
        break;

    default:
        DRV_DBG_INFO("access type is invalid \n");
        return DRV_E_INVALID_ACCESS_TYPE;
    }

    return DRV_E_NONE;
}

int32
drv_greatbelt_chip_write_ext(uint8 lchip, uint32 offset, uint32* p_value, uint32 length)
{
    DRV_IF_ERROR_RETURN(drv_greatbelt_pci_write_chip(lchip, offset, length, p_value));
    return DRV_E_NONE;
}

int32
drv_greatbelt_chip_set_access_type(drv_access_type_t access_type)
{
    g_gb_access_type = access_type;

    return DRV_E_NONE;
}

int32
drv_greatbelt_chip_get_access_type(drv_access_type_t* p_access_type)
{
    *p_access_type = g_gb_access_type;

    return DRV_E_NONE;
}

/**
 @brief pci read interface
*/
int32
drv_greatbelt_pci_read_chip(uint8 lchip, uint32 offset, uint32 len, uint32* p_value)
{
    drv_pci_cmd_status_u_t cmd_status_u;
    uint32 timeout = DRV_CMD_TIMEOUT;
    int32 ret = 0;
    uint8 index = 0;

    DRV_PTR_VALID_CHECK(p_value);

    /* pcie only have 16 databuf, len must not exceed 16 */
    if ((16 < len) || (0 == len))
    {
        DRV_DBG_INFO("pci read length error! len = %d \n", len);
        return DRV_E_EXCEED_MAX_SIZE;
    }

    DRV_PCI_LOCK(lchip);

    /* 1. write CmdStatusReg */
    sal_memset(&cmd_status_u, 0, sizeof(drv_pci_cmd_status_u_t));
    cmd_status_u.cmd_status.cmdReadType = 1;
    cmd_status_u.cmd_status.cmdEntryWords = (len==16)?0:len;   /* normal operate only support 1 entry */
    cmd_status_u.cmd_status.cmdDataLen = len;
    DRV_ACCESS_DAL_WITH_UNLOCK(g_dal_op.pci_write(lchip, DRV_PCI_CMD_STATUS, cmd_status_u.val), p_gb_pci_mutex[lchip]);

    /* 2. write AddrReg */
    DRV_ACCESS_DAL_WITH_UNLOCK(g_dal_op.pci_write(lchip, DRV_PCI_ADDR, offset), p_gb_pci_mutex[lchip]);

    /* 3. polling status and check */
    DRV_ACCESS_DAL_WITH_UNLOCK(g_dal_op.pci_read(lchip, DRV_PCI_CMD_STATUS, &cmd_status_u.val), p_gb_pci_mutex[lchip]);

    while (!(cmd_status_u.cmd_status.reqProcDone) && (--timeout))
    {
        ret = g_dal_op.pci_read(lchip, DRV_PCI_CMD_STATUS, &cmd_status_u.val);
        if (ret < 0)
        {
            DRV_DBG_INFO("pci read error! cmd_status = %x, line = %d\n", cmd_status_u.val, __LINE__);
            DRV_PCI_UNLOCK(lchip);
            return ret;
        }
    }

    /* check cmd done */
    if (!(cmd_status_u.cmd_status.reqProcDone))
    {
        DRV_DBG_INFO("pci read error! cmd_status = %x, offset=0x%x, line = %d\n", cmd_status_u.val, offset, __LINE__);
        DRV_PCI_UNLOCK(lchip);
        return DRV_E_CMD_NOT_DONE;
    }

    /*check pcie read status */
    if ((cmd_status_u.val & DRV_PCI_CHECK_STATUS) != 0)
    {
        DRV_DBG_INFO("pci read error! cmd_status = %x, offset=0x%x, line = %d\n", cmd_status_u.val, offset, __LINE__);
        DRV_PCI_UNLOCK(lchip);
        return DRV_E_PCI_CMD_ERROR;
    }

    /* AckCnt need not to check */

    /* 4. read data from buffer */
    for (index = 0; index < len; index++)
    {
        DRV_ACCESS_DAL_WITH_UNLOCK(g_dal_op.pci_read(lchip, DRV_PCI_DATA_BUF + index * 4, &p_value[index]), p_gb_pci_mutex[lchip]);
    }

    DRV_PCI_UNLOCK(lchip);

    return DRV_E_NONE;
}


/**
 @brief pci write interface
*/
int32
drv_greatbelt_pci_write_chip(uint8 lchip, uint32 offset, uint32 len, uint32* p_value)
{
    drv_pci_cmd_status_u_t cmd_status_u;
    uint32 timeout = DRV_CMD_TIMEOUT;  /* need to be confirmed */
    int32 ret = 0;
    uint8 index = 0;

    DRV_PTR_VALID_CHECK(p_value);

    /* pcie only have 16 databuf, len must not exceed 16 */
    if ((16 < len) || (0 == len))
    {
        DRV_DBG_INFO("pci read length error! len = %d \n", len);
        return DRV_E_EXCEED_MAX_SIZE;
    }

    DRV_PCI_LOCK(lchip);

    /* 1. write CmdStatusReg */
    sal_memset(&cmd_status_u, 0, sizeof(drv_pci_cmd_status_u_t));
    cmd_status_u.cmd_status.cmdReadType = 0;
    cmd_status_u.cmd_status.cmdEntryWords = (len==16)?0:len;   /* normal operate only support 1 entry */
    cmd_status_u.cmd_status.cmdDataLen = len;
    DRV_ACCESS_DAL_WITH_UNLOCK(g_dal_op.pci_write(lchip, DRV_PCI_CMD_STATUS, cmd_status_u.val), p_gb_pci_mutex[lchip]);

    /* 2. write AddrReg */
    DRV_ACCESS_DAL_WITH_UNLOCK(g_dal_op.pci_write(lchip, DRV_PCI_ADDR, offset), p_gb_pci_mutex[lchip]);

    /* 3. write data into databuffer */
    for (index = 0; index < len; index++)
    {
        DRV_ACCESS_DAL_WITH_UNLOCK(g_dal_op.pci_write(lchip, DRV_PCI_DATA_BUF + index * 4, p_value[index]), p_gb_pci_mutex[lchip]);
    }

    /* 4. polling status and check */
    DRV_ACCESS_DAL_WITH_UNLOCK(g_dal_op.pci_read(lchip, DRV_PCI_CMD_STATUS, &cmd_status_u.val), p_gb_pci_mutex[lchip]);

    while (!(cmd_status_u.cmd_status.reqProcDone) && (--timeout))
    {
        ret = g_dal_op.pci_read(lchip, DRV_PCI_CMD_STATUS, &cmd_status_u.val);
        if (ret < 0)
        {
            DRV_DBG_INFO("pci read error! cmd_status = %x, line = %d\n", cmd_status_u.val, __LINE__);
            DRV_PCI_UNLOCK(lchip);
            return ret;
        }
    }

    /* check cmd done */
    if (!(cmd_status_u.cmd_status.reqProcDone))
    {
        DRV_DBG_INFO("pci read error! cmd_status = %x, offset = 0x%x,  line = %d\n", cmd_status_u.val, offset,  __LINE__);
        DRV_PCI_UNLOCK(lchip);
        return DRV_E_CMD_NOT_DONE;
    }

    /*check pcie read status */
    if ((cmd_status_u.val & DRV_PCI_CHECK_STATUS) != 0)
    {
        DRV_DBG_INFO("pci read error! cmd_status = %x, offset =0x%x,  line = %d\n", cmd_status_u.val, offset,__LINE__);
        DRV_PCI_UNLOCK(lchip);
        return DRV_E_PCI_CMD_ERROR;
    }

    DRV_PCI_UNLOCK(lchip);

    return DRV_E_NONE;
}

/**
 @brief i2c read tbl/reg interface
*/
int32
drv_greatbelt_i2c_read_chip(uint8 lchip, uint32 offset, uint32* p_value)
{
    int32 ret = 0;
    uint8 i2c_cmd = 0;
    uint32 data_buf = 0;
    uint8  cmd_status = 0;
    uint32 timeout = DRV_CMD_TIMEOUT;

    DRV_PTR_VALID_CHECK(p_value);

    DRV_I2C_LOCK(lchip);

    /* 1. Write request address into WrData registers which will configure ChipAccessReg */
    offset |= DRV_I2C_CHIP_READ;
    offset = DRV_SWAP32(offset);
    DRV_ACCESS_DAL_WITH_UNLOCK(g_dal_op.i2c_write(lchip, DRV_I2C_WRDATA_OFFSET, 4, (uint8*)&offset), p_gb_i2c_mutex[lchip]);

    /* 2. Configure request address and set the request types as write to configure address of ChipAccessReg */
    i2c_cmd = DRV_I2C_CHIP_ADDR_REG_OFFSET | DRV_I2C_REQ_WRITE;
    DRV_ACCESS_DAL_WITH_UNLOCK(g_dal_op.i2c_write(lchip, DRV_I2C_ADDRCMD_OFFSET, 1, (uint8*)&i2c_cmd), p_gb_i2c_mutex[lchip]);

    /* 3. Configure request address and set the request types as read to get status of ChipAccessReg */
    i2c_cmd = DRV_I2C_CHIP_STATUS_REG_OFFSET | DRV_I2C_REQ_READ;
    DRV_ACCESS_DAL_WITH_UNLOCK(g_dal_op.i2c_write(lchip, DRV_I2C_ADDRCMD_OFFSET, 1, (uint8*)&i2c_cmd), p_gb_i2c_mutex[lchip]);

    /* 4. Read RdData to get status of accessing supervisor register */
    DRV_ACCESS_DAL_WITH_UNLOCK(g_dal_op.i2c_read(lchip, DRV_I2C_RDDATA_OFFSET, 4, (uint8*)&data_buf), p_gb_i2c_mutex[lchip]);

    data_buf = DRV_SWAP32(data_buf);
    /* check the second inderect access status */
    while (((data_buf & 0xc0000000) != 0xc0000000) && (--timeout))
    {
        ret = g_dal_op.i2c_read(lchip, DRV_I2C_RDDATA_OFFSET, 4, (uint8*)&data_buf);
        data_buf = DRV_SWAP32(data_buf);
        if (ret < 0)
        {
            DRV_DBG_INFO("i2c read error! cmd_status = 0x%x line = %d\n", data_buf, __LINE__);
            DRV_I2C_UNLOCK(lchip);
            return ret;
        }
    }

    /* check timeout */
    if ((data_buf & 0x20000000) != 0)
    {
        DRV_DBG_INFO("i2c read error! cmd_status = 0x%x offset=0x%x line = %d\n", data_buf,offset, __LINE__);
        DRV_I2C_UNLOCK(lchip);
        return DRV_E_CMD_NOT_DONE;
    }

    /* check cmd status error */
    if ((data_buf & 0x10000000) != 0)
    {
        DRV_DBG_INFO("i2c read error! cmd_status = 0x%x offset=0x%x  line = %d\n", data_buf, offset, __LINE__);
        DRV_I2C_UNLOCK(lchip);
        return DRV_E_I2C_CMD_ERROR;
    }

    /* 5. Configure request address and set the request types as read to get readData of ChipAccessReg */
    i2c_cmd = DRV_I2C_CHIP_DATA_REG_OFFSET | DRV_I2C_REQ_READ;
    DRV_ACCESS_DAL_WITH_UNLOCK(g_dal_op.i2c_write(lchip, DRV_I2C_ADDRCMD_OFFSET, 1, (uint8*)&i2c_cmd), p_gb_i2c_mutex[lchip]);

    /* 6. Check whether current reading status is done, selective step, can omit it if waiting time is enough */
    timeout = DRV_CMD_TIMEOUT;
    DRV_ACCESS_DAL_WITH_UNLOCK(g_dal_op.i2c_read(lchip, DRV_I2C_STATUS_OFFSET, 1, (uint8*)&cmd_status), p_gb_i2c_mutex[lchip]);

    while (!(cmd_status & 0x80) && (--timeout))
    {
        ret = g_dal_op.i2c_read(lchip, DRV_I2C_STATUS_OFFSET, 1, (uint8*)&cmd_status);
        if (ret < 0)
        {
            DRV_DBG_INFO("i2c read error! cmd_status = 0x%x line = %d\n", cmd_status, __LINE__);
            DRV_I2C_UNLOCK(lchip);
            return ret;
        }
    }

    if (!(cmd_status & 0x80))
    {
        DRV_DBG_INFO("i2c read error! cmd_status = 0x%x offset = 0x%x line = %d\n", cmd_status, offset, __LINE__);
        DRV_I2C_UNLOCK(lchip);
        return DRV_E_CMD_NOT_DONE;
    }

    if ((cmd_status & 0x60) != 0)
    {
        DRV_DBG_INFO("i2c read error! cmd_status = 0x%x offset=0x%x  line = %d\n", cmd_status, offset,  __LINE__);
        DRV_I2C_UNLOCK(lchip);
        return DRV_E_I2C_CMD_ERROR;
    }

    /* 7. Read RdData to get read data of accessing supervisor 0x3010 register */
    DRV_ACCESS_DAL_WITH_UNLOCK(g_dal_op.i2c_read(lchip, DRV_I2C_RDDATA_OFFSET, 4, (uint8*)&data_buf), p_gb_i2c_mutex[lchip]);
    *p_value = DRV_SWAP32((uint32)data_buf);

    DRV_I2C_UNLOCK(lchip);

    return DRV_E_NONE;
}

/**
 @brief i2c write tbl/reg interface
*/
int32
drv_greatbelt_i2c_write_chip(uint8 lchip, uint32 offset, uint32 value)
{
    int32 ret = 0;
    uint8 i2c_cmd = 0;
    uint32 data_buf = 0;
    uint8  cmd_status = 0;
    uint32 timeout = DRV_CMD_TIMEOUT;

    DRV_I2C_LOCK(lchip);

    value = DRV_SWAP32(value);

    /* 1. Write data into WrData registers which will configure ChipAccessReg */
    DRV_ACCESS_DAL_WITH_UNLOCK(g_dal_op.i2c_write(lchip, DRV_I2C_WRDATA_OFFSET, 4, (uint8*)&value), p_gb_i2c_mutex[lchip]);

    /* 2. Configure request address and set the request types as write to configure Write Data of ChipAccessReg */
    i2c_cmd = DRV_I2C_CHIP_WRDATA_REG_OFFSET | DRV_I2C_REQ_WRITE;
    DRV_ACCESS_DAL_WITH_UNLOCK(g_dal_op.i2c_write(lchip, DRV_I2C_ADDRCMD_OFFSET, 1, (uint8*)&i2c_cmd), p_gb_i2c_mutex[lchip]);

    /* 3. Write request address into WrData registers which will configure ChipAccessReg */
    offset |= DRV_I2C_CHIP_WRITE;
    offset = DRV_SWAP32(offset);
    DRV_ACCESS_DAL_WITH_UNLOCK(g_dal_op.i2c_write(lchip, DRV_I2C_WRDATA_OFFSET, 4, (uint8*)&offset), p_gb_i2c_mutex[lchip]);

    /* 4. Configure request address and set the request types as write to configure address of ChipAccessReg */
    i2c_cmd = DRV_I2C_CHIP_ADDR_REG_OFFSET | DRV_I2C_REQ_WRITE;
    DRV_ACCESS_DAL_WITH_UNLOCK(g_dal_op.i2c_write(lchip, DRV_I2C_ADDRCMD_OFFSET, 1, (uint8*)&i2c_cmd), p_gb_i2c_mutex[lchip]);

    /* 5. Configure request address and set the request types as read to get status of ChipAccessReg */
    i2c_cmd = DRV_I2C_CHIP_STATUS_REG_OFFSET | DRV_I2C_REQ_READ;
    DRV_ACCESS_DAL_WITH_UNLOCK(g_dal_op.i2c_write(lchip, DRV_I2C_ADDRCMD_OFFSET, 1, (uint8*)&i2c_cmd), p_gb_i2c_mutex[lchip]);

    /* 6. Read RdData to get status of accessing supervisor  register */
    DRV_ACCESS_DAL_WITH_UNLOCK(g_dal_op.i2c_read(lchip, DRV_I2C_RDDATA_OFFSET, 4, (uint8*)&data_buf), p_gb_i2c_mutex[lchip]);

    data_buf = DRV_SWAP32(data_buf);
    /* check the second inderect access status */
    while (((data_buf & 0xc0000000) != 0xc0000000) && (--timeout))
    {
        ret = g_dal_op.i2c_read(lchip, DRV_I2C_RDDATA_OFFSET, 4, (uint8*)&data_buf);
        data_buf = DRV_SWAP32(data_buf);
        if (ret < 0)
        {
            DRV_DBG_INFO("i2c read error! cmd_status = 0x%x line = %d\n", data_buf, __LINE__);
            DRV_I2C_UNLOCK(lchip);
            return ret;
        }
    }

    /* check timeout */
    if ((data_buf & 0xc0000000) != 0xc0000000)
    {
        DRV_DBG_INFO("i2c read error! cmd_status = 0x%x line = %d\n", data_buf, __LINE__);
        DRV_I2C_UNLOCK(lchip);
        return DRV_E_CMD_NOT_DONE;
    }

    if ((data_buf & 0x30000000) != 0)
    {
        DRV_DBG_INFO("i2c read error! cmd_status = 0x%x line = %d\n", data_buf, __LINE__);
        DRV_I2C_UNLOCK(lchip);
        return DRV_E_I2C_CMD_ERROR;
    }

    /* 7. Check whether current reading Status is done, selective step, can omit it if waiting time is enough */
    timeout = DRV_CMD_TIMEOUT;
    DRV_ACCESS_DAL_WITH_UNLOCK(g_dal_op.i2c_read(lchip, DRV_I2C_STATUS_OFFSET, 1, (uint8*)&cmd_status), p_gb_i2c_mutex[lchip]);

    while (!(cmd_status & 0x80) && (--timeout))
    {
        ret = g_dal_op.i2c_read(lchip, DRV_I2C_STATUS_OFFSET, 1, (uint8*)&cmd_status);
        if (ret < 0)
        {
            DRV_DBG_INFO("i2c read error! cmd_status = 0x%x line = %d\n", cmd_status, __LINE__);
            DRV_I2C_UNLOCK(lchip);
            return ret;
        }
    }

    if (!(cmd_status & 0x80))
    {
        DRV_DBG_INFO("i2c read error! cmd_status = 0x%x line = %d\n", cmd_status, __LINE__);
        DRV_I2C_UNLOCK(lchip);
        return DRV_E_CMD_NOT_DONE;
    }

    if ((cmd_status & 0x60) != 0)
    {
        DRV_DBG_INFO("i2c read error! cmd_status = 0x%x line = %d\n", cmd_status, __LINE__);
        DRV_I2C_UNLOCK(lchip);
        return DRV_E_I2C_CMD_ERROR;
    }

    DRV_I2C_UNLOCK(lchip);

    return DRV_E_NONE;
}

/**
 @brief i2c read pcie configuration space interface
*/
int32
drv_greatbelt_i2c_read_local(uint8 lchip, uint32 offset, uint32* p_value)
{
    uint8 cmd_status = 0;
    int32 ret = 0;
    uint32 timeout = DRV_CMD_TIMEOUT;
    uint32 data_buf = 0;
    uint8 temp = 0;

    DRV_PTR_VALID_CHECK(p_value);

    DRV_I2C_LOCK(lchip);

    offset = DRV_SWAP32(offset);

    /* 1.Configure request address and set the request types as write */
    offset |= DRV_I2C_REQ_READ;
    temp = (uint8)(offset & 0xff);
    DRV_ACCESS_DAL_WITH_UNLOCK(g_dal_op.i2c_write(lchip, DRV_I2C_ADDRCMD_OFFSET, 1, &temp), p_gb_i2c_mutex[lchip]);

    /* 2. Check status and read data, selective step, can omit it if waiting time is enough */
    DRV_ACCESS_DAL_WITH_UNLOCK(g_dal_op.i2c_read(lchip, DRV_I2C_STATUS_OFFSET, 1, (uint8*)&cmd_status), p_gb_i2c_mutex[lchip]);

    while (!(cmd_status & 0x80) && (--timeout))
    {
        ret = g_dal_op.i2c_read(lchip, DRV_I2C_STATUS_OFFSET, 1, (uint8*)&cmd_status);
        if (ret < 0)
        {
            DRV_DBG_INFO("i2c read error! cmd_status = 0x%x line = %d\n", cmd_status, __LINE__);
            DRV_I2C_UNLOCK(lchip);
            return ret;
        }
    }

    if (!(cmd_status & 0x80))
    {
        DRV_DBG_INFO("i2c read error! cmd_status = 0x%x line = %d\n", cmd_status, __LINE__);
        DRV_I2C_UNLOCK(lchip);
        return DRV_E_CMD_NOT_DONE;
    }

    if ((cmd_status & 0x60) != 0)
    {
        DRV_DBG_INFO("i2c read error! cmd_status = 0x%x line = %d\n", cmd_status, __LINE__);
        DRV_I2C_UNLOCK(lchip);
        return DRV_E_I2C_CMD_ERROR;
    }

    DRV_ACCESS_DAL_WITH_UNLOCK(g_dal_op.i2c_read(lchip, DRV_I2C_RDDATA_OFFSET, 4, (uint8*)&data_buf), p_gb_i2c_mutex[lchip]);
    *p_value = DRV_SWAP32((uint32)data_buf);

    DRV_I2C_UNLOCK(lchip);

    return DRV_E_NONE;
}

/**
 @brief i2c write pcie configuration space interface
*/
int32
drv_greatbelt_i2c_write_local(uint8 lchip, uint32 offset, uint32 value)
{
    uint8 cmd_status = 0;
    int32 ret = 0;
    uint32 timeout = DRV_CMD_TIMEOUT;
    uint8 temp = 0;

    DRV_I2C_LOCK(lchip);

    offset = DRV_SWAP32(offset);
    value = DRV_SWAP32(value);

    /* 1. Write configuration value into WrData registers */
    DRV_ACCESS_DAL_WITH_UNLOCK(g_dal_op.i2c_write(lchip, DRV_I2C_WRDATA_OFFSET, 4, (uint8*)&value), p_gb_i2c_mutex[lchip]);

    /* 2. Configure request address and set the request types as write to configure address of PCIe Cfg Reg */
    offset |= DRV_I2C_REQ_WRITE;
    temp = (uint8)(offset & 0xff);
    DRV_ACCESS_DAL_WITH_UNLOCK(g_dal_op.i2c_write(lchip, DRV_I2C_ADDRCMD_OFFSET, 1, &temp), p_gb_i2c_mutex[lchip]);

    /* 3. read status and check */
    DRV_ACCESS_DAL_WITH_UNLOCK(g_dal_op.i2c_read(lchip, DRV_I2C_STATUS_OFFSET, 1, (uint8*)&cmd_status), p_gb_i2c_mutex[lchip]);

    while (!(cmd_status & 0x80) && (--timeout))
    {
        ret = g_dal_op.i2c_read(lchip, DRV_I2C_STATUS_OFFSET, 1, (uint8*)&cmd_status);
        if (ret < 0)
        {
            DRV_DBG_INFO("i2c read error! cmd_status = %d line = %d\n", cmd_status, __LINE__);
            DRV_I2C_UNLOCK(lchip);
            return ret;
        }
    }

    if (!(cmd_status & 0x80))
    {
        DRV_DBG_INFO("i2c read error! cmd_status = %d line = %d\n", cmd_status, __LINE__);
        DRV_I2C_UNLOCK(lchip);
        return DRV_E_CMD_NOT_DONE;
    }

    if ((cmd_status & 0x60) != 0)
    {
        DRV_DBG_INFO("i2c read error! cmd_status = %d line = %d\n", cmd_status, __LINE__);
        DRV_I2C_UNLOCK(lchip);
        return DRV_E_I2C_CMD_ERROR;
    }

    DRV_I2C_UNLOCK(lchip);

    return DRV_E_NONE;
}

/**
 @brief i2c read hss6g configuration space interface
*/
int32
drv_greatbelt_i2c_read_hss6g(uint8 lchip, uint32 offset, uint16* p_value)
{
    uint8 cmd_status = 0;
    int32 ret = 0;
    uint32 timeout = DRV_CMD_TIMEOUT;
    uint32 cmd = 0;
    uint8 i2c_cmd = 0;
    uint16 data_buf = 0;

    DRV_PTR_VALID_CHECK(p_value);

    DRV_I2C_LOCK(lchip);

    offset = DRV_SWAP32(offset);

    /* 1. Write request address into WrData registers which will configure address of RCBusCfg */
    DRV_ACCESS_DAL_WITH_UNLOCK(g_dal_op.i2c_write(lchip, DRV_I2C_WRDATA_OFFSET, 4, (uint8*)&offset), p_gb_i2c_mutex[lchip]);

    /* 2. Configure request address and set the request types as write to configure address of RCBusCfg */
    i2c_cmd = DRV_I2C_RCBUS_ADDR_REG_OFFSET | DRV_I2C_REQ_WRITE;
    DRV_ACCESS_DAL_WITH_UNLOCK(g_dal_op.i2c_write(lchip, DRV_I2C_ADDRCMD_OFFSET, 1, (uint8*)&i2c_cmd), p_gb_i2c_mutex[lchip]);

    /* 3. Write request type of RCBusCfg into WrData */
    cmd = 0x02;
    DRV_ACCESS_DAL_WITH_UNLOCK(g_dal_op.i2c_write(lchip, DRV_I2C_WRDATA_OFFSET, 4, (uint8*)&cmd), p_gb_i2c_mutex[lchip]);

    /* 4. Configure request address and set the request types as write to configure command of RCBusCfg */
    i2c_cmd = DRV_I2C_RCBUS_CMD_REG_OFFSET | DRV_I2C_REQ_WRITE;
    DRV_ACCESS_DAL_WITH_UNLOCK(g_dal_op.i2c_write(lchip, DRV_I2C_ADDRCMD_OFFSET, 1, (uint8*)&i2c_cmd), p_gb_i2c_mutex[lchip]);

    /* 5. Configure request address and set the request types as read to get status and readData of RCBusCfg */
    i2c_cmd = DRV_I2C_RCBUS_CMD_REG_OFFSET | DRV_I2C_REQ_READ;
    DRV_ACCESS_DAL_WITH_UNLOCK(g_dal_op.i2c_write(lchip, DRV_I2C_ADDRCMD_OFFSET, 1, (uint8*)&i2c_cmd), p_gb_i2c_mutex[lchip]);

    /* 6. Check whether current reading status is done, selective step, can omit it if waiting time is enough */
    DRV_ACCESS_DAL_WITH_UNLOCK(g_dal_op.i2c_read(lchip, DRV_I2C_STATUS_OFFSET, 1, (uint8*)&cmd_status), p_gb_i2c_mutex[lchip]);

    while (!(cmd_status & 0x80) && (--timeout))
    {
        ret = g_dal_op.i2c_read(lchip, DRV_I2C_STATUS_OFFSET, 1, (uint8*)&cmd_status);
        if (ret < 0)
        {
            DRV_DBG_INFO("i2c read error! cmd_status = %d line = %d\n", cmd_status, __LINE__);
            DRV_I2C_UNLOCK(lchip);
            return ret;
        }
    }

    if (!(cmd_status & 0x80))
    {
        DRV_DBG_INFO("i2c read error! cmd_status = %d line = %d\n", cmd_status, __LINE__);
        DRV_I2C_UNLOCK(lchip);
        return DRV_E_CMD_NOT_DONE;
    }

    if ((cmd_status & 0x60) != 0)
    {
        DRV_DBG_INFO("i2c read error! cmd_status = %d line = %d\n", cmd_status, __LINE__);
        DRV_I2C_UNLOCK(lchip);
        return DRV_E_I2C_CMD_ERROR;
    }

    /* 7. Read RdData to get status and read data of RCBusCfg ,check 2st inderect access status */
    timeout = DRV_CMD_TIMEOUT;
    DRV_ACCESS_DAL_WITH_UNLOCK(g_dal_op.i2c_read(lchip, DRV_I2C_RDDATA_OFFSET + 3, 1, (uint8*)&cmd_status), p_gb_i2c_mutex[lchip]);

    while (!(cmd_status & 0x10) && (--timeout))
    {
        ret = g_dal_op.i2c_read(lchip, DRV_I2C_RDDATA_OFFSET + 3, 1, (uint8*)&cmd_status);
        if (ret < 0)
        {
            DRV_DBG_INFO("i2c read error! cmd_status = %d line = %d\n", cmd_status, __LINE__);
            DRV_I2C_UNLOCK(lchip);
            return ret;
        }
    }

    if (!(cmd_status & 0x10))
    {
        DRV_DBG_INFO("i2c read error! cmd_status = %d line = %d\n", cmd_status, __LINE__);
        DRV_I2C_UNLOCK(lchip);
        return DRV_E_CMD_NOT_DONE;
    }

    DRV_ACCESS_DAL_WITH_UNLOCK(g_dal_op.i2c_read(lchip, DRV_I2C_RDDATA_OFFSET, 2, (uint8*)&data_buf), p_gb_i2c_mutex[lchip]);
    data_buf = DRV_SWAP16(data_buf);

    *p_value = (uint16)data_buf;

    DRV_I2C_UNLOCK(lchip);

    return DRV_E_NONE;
}

/**
 @brief i2c write hss6g configuration space interface
*/
int32
drv_greatbelt_i2c_write_hss6g(uint8 lchip, uint32 offset, uint16 value, uint16 mask)
{
    uint8 i2c_cmd = 0;
    uint32 cmd = 0;
    uint8 cmd_status = 0;
    uint32 timeout = DRV_CMD_TIMEOUT;
    int32 ret = 0;

    DRV_I2C_LOCK(lchip);

    value = DRV_SWAP16(value);
    mask = DRV_SWAP16(mask);
    offset = DRV_SWAP32(offset);

    /* 1. Write mask and data into WrData registers which will configure rcWrMask of RCBusCfg */
    DRV_ACCESS_DAL_WITH_UNLOCK(g_dal_op.i2c_write(lchip, DRV_I2C_WRDATA_OFFSET, 2, (uint8*)&mask), p_gb_i2c_mutex[lchip]);
    DRV_ACCESS_DAL_WITH_UNLOCK(g_dal_op.i2c_write(lchip, DRV_I2C_WRDATA_OFFSET + 2, 2, (uint8*)&value), p_gb_i2c_mutex[lchip]);

    /* 2. Configure request address and set the request types as write to configure write data/mask of RCBusCfg */
    i2c_cmd = DRV_I2C_RCBUS_WRDATA_REG_OFFSET | DRV_I2C_REQ_WRITE;
    DRV_ACCESS_DAL_WITH_UNLOCK(g_dal_op.i2c_write(lchip, DRV_I2C_ADDRCMD_OFFSET, 1, (uint8*)&i2c_cmd), p_gb_i2c_mutex[lchip]);

    /* 3. write request address into WrData registers which will configure address of RCBusCfg */
    DRV_ACCESS_DAL_WITH_UNLOCK(g_dal_op.i2c_write(lchip, DRV_I2C_WRDATA_OFFSET, 4, (uint8*)&offset), p_gb_i2c_mutex[lchip]);

    /* 4. Configure request address and set the request types as write to configure address of RCBusCfg */
    i2c_cmd = DRV_I2C_RCBUS_ADDR_REG_OFFSET | DRV_I2C_REQ_WRITE;
    DRV_ACCESS_DAL_WITH_UNLOCK(g_dal_op.i2c_write(lchip, DRV_I2C_ADDRCMD_OFFSET, 1, (uint8*)&i2c_cmd), p_gb_i2c_mutex[lchip]);

    /* 5. Write request type of RCBUS into WrData, mask-write */
    cmd = 0x01;
    DRV_ACCESS_DAL_WITH_UNLOCK(g_dal_op.i2c_write(lchip, DRV_I2C_WRDATA_OFFSET, 4, (uint8*)&cmd), p_gb_i2c_mutex[lchip]);

    /* 6. Configure request address and set the request types as write to configure command of RCBusCfg */
    i2c_cmd = DRV_I2C_RCBUS_CMD_REG_OFFSET | DRV_I2C_REQ_WRITE;
    DRV_ACCESS_DAL_WITH_UNLOCK(g_dal_op.i2c_write(lchip, DRV_I2C_ADDRCMD_OFFSET, 1, (uint8*)&i2c_cmd), p_gb_i2c_mutex[lchip]);

    /* 7. Read RdData to get status of RCBusCfg */
    DRV_ACCESS_DAL_WITH_UNLOCK(g_dal_op.i2c_read(lchip, DRV_I2C_STATUS_OFFSET, 1, (uint8*)&cmd_status), p_gb_i2c_mutex[lchip]);

    while (!(cmd_status & 0x80) && (--timeout))
    {
        ret = g_dal_op.i2c_read(lchip, DRV_I2C_STATUS_OFFSET, 1, (uint8*)&cmd_status);
        if (ret < 0)
        {
            DRV_DBG_INFO("i2c read error! cmd_status = %d line = %d\n", cmd_status, __LINE__);
            DRV_I2C_UNLOCK(lchip);
            return ret;
        }
    }

    if (!(cmd_status & 0x80))
    {
        DRV_DBG_INFO("i2c read error! cmd_status = %d line = %d\n", cmd_status, __LINE__);
        DRV_I2C_UNLOCK(lchip);
        return DRV_E_CMD_NOT_DONE;
    }

    if ((cmd_status & 0x60) != 0)
    {
        DRV_DBG_INFO("i2c read error! cmd_status = %d line = %d\n", cmd_status, __LINE__);
        DRV_I2C_UNLOCK(lchip);
        return DRV_E_I2C_CMD_ERROR;
    }

    i2c_cmd = DRV_I2C_RCBUS_CMD_REG_OFFSET | DRV_I2C_REQ_READ;
    DRV_ACCESS_DAL_WITH_UNLOCK(g_dal_op.i2c_write(lchip, DRV_I2C_ADDRCMD_OFFSET, 1, (uint8*)&i2c_cmd), p_gb_i2c_mutex[lchip]);

    /* check 2st inderect access status */
    timeout = DRV_CMD_TIMEOUT;
    DRV_ACCESS_DAL_WITH_UNLOCK(g_dal_op.i2c_read(lchip, DRV_I2C_RDDATA_OFFSET + 3, 1, (uint8*)&cmd_status), p_gb_i2c_mutex[lchip]);

    while (!(cmd_status & 0x10) && (--timeout))
    {
        ret = g_dal_op.i2c_read(lchip, DRV_I2C_RDDATA_OFFSET + 3, 1, (uint8*)&cmd_status);
        if (ret < 0)
        {
            DRV_DBG_INFO("i2c read error! cmd_status = %d line = %d\n", cmd_status, __LINE__);
            DRV_I2C_UNLOCK(lchip);
            return ret;
        }
    }

    if (!(cmd_status & 0x10))
    {
        DRV_DBG_INFO("i2c read error! cmd_status = %d line = %d\n", cmd_status, __LINE__);
        DRV_I2C_UNLOCK(lchip);
        return DRV_E_CMD_NOT_DONE;
    }

    DRV_I2C_UNLOCK(lchip);

    return DRV_E_NONE;
}


/**
 @brief access hss12g control register
*/
int32
drv_greatbelt_chip_read_hss12g(uint8 lchip, uint8 hssid, uint32 addr, uint16* p_data)
{
    int32 ret = 0;
    hss_access_ctl_t hss_ctl;
    hss_write_data_t hss_wrdata;
    hss_read_data_t  hss_rddata;
    uint32 cmd = 0;

    DRV_PTR_VALID_CHECK(p_data);

    if (hssid > DRV_HSS12G_MACRO_NUM - 1)
    {
        return DRV_E_EXCEED_MAX_SIZE;
    }

    sal_memset(&hss_ctl, 0, sizeof(hss_access_ctl_t));
    sal_memset(&hss_wrdata, 0, sizeof(hss_write_data_t));
    sal_memset(&hss_rddata, 0, sizeof(hss_read_data_t));

    DRV_HSS_LOCK(lchip);

    /* 1. Write request hssReqId and hssReqAddr register */
    hss_ctl.hss_req_id = hssid;
    hss_ctl.hss_req_is_read = 1;
    hss_ctl.hss_req_addr = addr;
    hss_ctl.hss_req_valid = 1;

    cmd = DRV_IOW(HssAccessCtl_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, 0, cmd, &hss_ctl);
    if (ret < 0)
    {
        DRV_HSS_UNLOCK(lchip);
        return DRV_E_ACCESS_HSS12G_FAIL;
    }

    if (0 == SDK_WORK_PLATFORM)
    {
        /* 2. Read hssReadDataValid register, and check whether current accssing is done */
        cmd = DRV_IOR(HssReadData_t, DRV_ENTRY_FLAG);
        ret = DRV_IOCTL(lchip, 0, cmd, &hss_rddata);
        if (ret < 0)
        {
            DRV_HSS_UNLOCK(lchip);
            return DRV_E_ACCESS_HSS12G_FAIL;
        }

        if (hss_rddata.hss_read_data_valid == 0)
        {
            DRV_HSS_UNLOCK(lchip);
            return DRV_E_ACCESS_HSS12G_FAIL;
        }
    }

    /* 3. Read hssReadData registers */
    *p_data = hss_rddata.hss_read_data;

    DRV_HSS_UNLOCK(lchip);

    return DRV_E_NONE;
}

/**
 @brief access hss12g control register
*/
int32
drv_greatbelt_chip_write_hss12g(uint8 lchip, uint8 hssid, uint32 addr, uint16 data)
{
    int32 ret = 0;
    hss_access_ctl_t hss_ctl;
    hss_write_data_t hss_wrdata;
    uint32 cmd = 0;

    if (hssid > DRV_HSS12G_MACRO_NUM - 1)
    {
        return DRV_E_EXCEED_MAX_SIZE;
    }

    sal_memset(&hss_ctl, 0, sizeof(hss_access_ctl_t));
    sal_memset(&hss_wrdata, 0, sizeof(hss_write_data_t));

    /*1. Write data into hssWriteData register */
    DRV_HSS_LOCK(lchip);

    hss_wrdata.hss_write_data = data;
    cmd = DRV_IOW(HssWriteData_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, 0, cmd, &hss_wrdata);
    if (ret < 0)
    {
        DRV_HSS_UNLOCK(lchip);
        return DRV_E_ACCESS_HSS12G_FAIL;
    }

    /* 2. Write request hssReqId and hssReqAddr register */
    hss_ctl.hss_req_is_read = 0;
    hss_ctl.hss_req_addr = addr;
    hss_ctl.hss_req_id = hssid;
    hss_ctl.hss_req_valid = 1;

    cmd = DRV_IOW(HssAccessCtl_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, 0, cmd, &hss_ctl);
    if (ret < 0)
    {
        DRV_HSS_UNLOCK(lchip);
        return DRV_E_ACCESS_HSS12G_FAIL;
    }

    DRV_HSS_UNLOCK(lchip);

    return DRV_E_NONE;
}



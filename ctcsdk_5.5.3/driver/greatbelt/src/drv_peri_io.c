/**
 @file drv_chip_io.c

 @date 2011-10-09

 @version v4.28.2

 The file contains all periphery device access interface
*/
#include "greatbelt/include/drv_lib.h"
#include "greatbelt/include/drv_tbl_reg.h"
#include "greatbelt/include/drv_io.h"
#include "greatbelt/include/drv_model_io.h"
#include "greatbelt/include/drv_chip_io.h"
#include "greatbelt/include/drv_peri_io.h"

static sal_mutex_t* p_gb_peri_smi_mutex[MAX_LOCAL_CHIP_NUM];
static sal_mutex_t* p_gb_peri_i2c_mutex[MAX_LOCAL_CHIP_NUM];

#define DRV_PERI_SMI_LOCK(chip_id_offset)          sal_mutex_lock(p_gb_peri_smi_mutex[chip_id_offset])
#define DRV_PERI_SMI_UNLOCK(chip_id_offset)        sal_mutex_unlock(p_gb_peri_smi_mutex[chip_id_offset])
#define DRV_PERI_I2C_LOCK(chip_id_offset)          sal_mutex_lock(p_gb_peri_i2c_mutex[chip_id_offset])
#define DRV_PERI_I2C_UNLOCK(chip_id_offset)        sal_mutex_unlock(p_gb_peri_i2c_mutex[chip_id_offset])


extern uint32 drv_greatbelt_get_core_freq(uint8 chip_id);

int32
drv_greatbelt_chip_peri_mutex_init(uint8 chip_id_offset)
{
    int32 ret;

    DRV_CHIP_ID_VALID_CHECK(chip_id_offset + drv_gb_init_chip_info.drv_init_chipid_base);

    ret = sal_mutex_create(&p_gb_peri_smi_mutex[chip_id_offset]);
    if (ret || (!p_gb_peri_smi_mutex[chip_id_offset]))
    {
        return DRV_E_FAIL_CREATE_MUTEX;
    }

    ret = sal_mutex_create(&p_gb_peri_i2c_mutex[chip_id_offset]);
    if (ret || (!p_gb_peri_i2c_mutex[chip_id_offset]))
    {
        return DRV_E_FAIL_CREATE_MUTEX;
    }

    return DRV_E_NONE;
}

/**
@brief deri init function
*/
int32
drv_greatbelt_peri_init(uint8 lchip)
{
    int32 ret = 0;

    /* 1. init mutex */
    ret = drv_greatbelt_chip_peri_mutex_init(lchip);
    if (ret < 0)
    {
        return ret;
    }

    return DRV_E_NONE;
}

/**
 @brief mdio interface for read ge phy
*/
int32
drv_greatbelt_peri_read_gephy_reg(uint8 lchip, uint8 mdio_bus, uint8 phy_addr, uint8 reg, uint16* p_value)
{
    mdio_cmd1_g_t mdio_cmd;
    mdio_status1_g_t mdio_status;
    uint32 timeout = 256;
    uint32 cmd = 0;
    int32 ret = 0;

    DRV_PTR_VALID_CHECK(p_value);
    if (mdio_bus > 1)
    {
        return DRV_E_INVALID_PARAMETER;
    }

    DRV_PERI_SMI_LOCK(lchip);

    sal_memset(&mdio_cmd, 0, sizeof(mdio_cmd1_g_t));
    sal_memset(&mdio_status, 0, sizeof(mdio_status1_g_t));

    mdio_cmd.intf_sel_cmd1g = mdio_bus;
    mdio_cmd.dev_add_cmd1g = reg;
    mdio_cmd.op_code_cmd1g = DRV_1G_MDIO_READ;
    mdio_cmd.port_add_cmd1g = phy_addr;
    mdio_cmd.start_cmd1g = 0x1;

    cmd = DRV_IOW(MdioCmd1G_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, 0, cmd, &mdio_cmd);
    if (ret < 0)
    {
        DRV_PERI_SMI_UNLOCK(lchip);
        return DRV_E_MDIO_CMD_ERROR;
    }

    /* check status done */
    cmd = DRV_IOR(MdioStatus1G_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, 0, cmd, &mdio_status);
    if (ret < 0)
    {
        DRV_PERI_SMI_UNLOCK(lchip);
        return DRV_E_MDIO_CMD_ERROR;
    }

    while ((mdio_status.mdio1_g_cmd_done == 0) && (--timeout))
    {
        ret = DRV_IOCTL(lchip, 0, cmd, &mdio_status);
        if (ret < 0)
        {
            DRV_PERI_SMI_UNLOCK(lchip);
            return DRV_E_MDIO_CMD_ERROR;
        }
    }

    if (mdio_status.mdio1_g_cmd_done == 0)
    {
        DRV_PERI_SMI_UNLOCK(lchip);
        return DRV_E_MDIO_CMD_TIMEOUT;
    }

    /* read data */
    *p_value = mdio_status.mdio1_g_read_data;

    DRV_PERI_SMI_UNLOCK(lchip);

    return DRV_E_NONE;
}

/**
 @brief mdio interface for write ge phy
*/
int32
drv_greatbelt_peri_write_gephy_reg(uint8 lchip, uint8 mdio_bus, uint8 phy_addr, uint8 reg, uint16 value)
{
    mdio_cmd1_g_t mdio_cmd;
    mdio_status1_g_t mdio_status;
    uint32 timeout = 256;
    uint32 cmd = 0;
    int32 ret = 0;

    if (mdio_bus > 1)
    {
        return DRV_E_INVALID_PARAMETER;
    }

    DRV_PERI_SMI_LOCK(lchip);

    sal_memset(&mdio_cmd, 0, sizeof(mdio_cmd1_g_t));
    sal_memset(&mdio_status, 0, sizeof(mdio_status1_g_t));

    mdio_cmd.intf_sel_cmd1g = mdio_bus;
    mdio_cmd.dev_add_cmd1g = reg;
    mdio_cmd.op_code_cmd1g = DRV_MDIO_WRITE;
    mdio_cmd.port_add_cmd1g = phy_addr;
    mdio_cmd.start_cmd1g = 0x1;
    mdio_cmd.data_cmd1g = value;

    cmd = DRV_IOW(MdioCmd1G_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, 0, cmd, &mdio_cmd);
    if (ret < 0)
    {
        DRV_PERI_SMI_UNLOCK(lchip);
        return DRV_E_MDIO_CMD_ERROR;
    }

    /* check status done */
    cmd = DRV_IOR(MdioStatus1G_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, 0, cmd, &mdio_status);
    if (ret < 0)
    {
        DRV_PERI_SMI_UNLOCK(lchip);
        return DRV_E_MDIO_CMD_ERROR;
    }

    while ((mdio_status.mdio1_g_cmd_done == 0) && (--timeout))
    {
        ret = DRV_IOCTL(lchip, 0, cmd, &mdio_status);
        if (ret < 0)
        {
            DRV_PERI_SMI_UNLOCK(lchip);
            return DRV_E_MDIO_CMD_ERROR;
        }
    }

    if (mdio_status.mdio1_g_cmd_done == 0)
    {
        DRV_PERI_SMI_UNLOCK(lchip);
        return DRV_E_MDIO_CMD_TIMEOUT;
    }

    DRV_PERI_SMI_UNLOCK(lchip);

    return DRV_E_NONE;
}

/**
 @brief mdio interface for read xg phy
*/
int32
drv_greatbelt_peri_read_xgphy_reg(uint8 lchip, uint8 mdio_bus, uint8 phy_addr, uint8 dev, uint16 reg, uint16* p_value)
{
    mdio_cmd_x_g_t mdio_cmd;
    mdio_status_x_g_t mdio_status;
    uint32 cmd = 0;
    uint32 timeout = 256;
    int32 ret = 0;

    DRV_PTR_VALID_CHECK(p_value);

    if ((mdio_bus != 2) && (mdio_bus != 3))
    {
        return DRV_E_INVALID_PARAMETER;
    }

    DRV_PERI_SMI_LOCK(lchip);

    sal_memset(&mdio_cmd, 0, sizeof(mdio_cmd_x_g_t));
    sal_memset(&mdio_status, 0, sizeof(mdio_status_x_g_t));
    mdio_cmd.dev_add_cmd_xg = dev;
    mdio_cmd.intf_sel_cmd_xg = mdio_bus - 2;
    mdio_cmd.op_code_cmd_xg = DRV_XG_MDIO_READ;
    mdio_cmd.port_add_cmd_xg = phy_addr;
    mdio_cmd.reg_add_cmd_xg = reg;
    mdio_cmd.start_cmd_xg = 0;

    cmd = DRV_IOW(MdioCmdXG_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, 0, cmd, &mdio_cmd);
    if (ret < 0)
    {
        DRV_PERI_SMI_UNLOCK(lchip);
        return DRV_E_MDIO_CMD_ERROR;
    }

    /* check status done */
    cmd = DRV_IOR(MdioStatusXG_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, 0, cmd, &mdio_status);
    if (ret < 0)
    {
        DRV_PERI_SMI_UNLOCK(lchip);
        return DRV_E_MDIO_CMD_ERROR;
    }

    while ((mdio_status.mdio_x_g_cmd_done == 0) && (--timeout))
    {
        ret = DRV_IOCTL(lchip, 0, cmd, &mdio_status);
        if (ret < 0)
        {
            DRV_PERI_SMI_UNLOCK(lchip);
            return DRV_E_MDIO_CMD_ERROR;
        }
    }

    if (mdio_status.mdio_x_g_cmd_done == 0)
    {
        DRV_PERI_SMI_UNLOCK(lchip);
        return DRV_E_MDIO_CMD_TIMEOUT;
    }

    /* read data */
    *p_value = mdio_status.mdio_x_g_read_data;

    DRV_PERI_SMI_UNLOCK(lchip);

    return DRV_E_NONE;
}

/**
 @brief mdio interface for write xg phy
*/
int32
drv_greatbelt_peri_write_xgphy_reg(uint8 lchip, uint8 mdio_bus, uint8 phy_addr, uint8 dev, uint16 reg, uint16 value)
{
    mdio_cmd_x_g_t mdio_cmd;
    mdio_status_x_g_t mdio_status;
    uint32 cmd = 0;
    uint32 timeout = 256;
    int32 ret = 0;

    if ((mdio_bus != 2) && (mdio_bus != 3))
    {
        return DRV_E_INVALID_PARAMETER;
    }

    DRV_PERI_SMI_LOCK(lchip);

    sal_memset(&mdio_cmd, 0, sizeof(mdio_cmd_x_g_t));
    sal_memset(&mdio_status, 0, sizeof(mdio_status_x_g_t));

    /* write data */
    mdio_cmd.data_cmd_xg = value;
#if 0
    cmd = DRV_IOW(MdioCmdXG_t, MdioCmdXG_DataCmdXg_f);
    ret = DRV_IOCTL(lchip, 0, cmd, &mdio_cmd);
    if (ret < 0)
    {
        DRV_PERI_SMI_UNLOCK(lchip);
        return DRV_E_MDIO_CMD_ERROR;
    }

#endif
    /* write cmd */
    mdio_cmd.dev_add_cmd_xg = dev;
    mdio_cmd.intf_sel_cmd_xg = mdio_bus - 2;
    mdio_cmd.op_code_cmd_xg = DRV_MDIO_WRITE;
    mdio_cmd.port_add_cmd_xg = phy_addr;
    mdio_cmd.reg_add_cmd_xg = reg;
    mdio_cmd.start_cmd_xg = 0;

    cmd = DRV_IOW(MdioCmdXG_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, 0, cmd, &mdio_cmd);
    if (ret < 0)
    {
        DRV_PERI_SMI_UNLOCK(lchip);
        return DRV_E_MDIO_CMD_ERROR;
    }

    /* check status done */
    cmd = DRV_IOR(MdioStatusXG_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, 0, cmd, &mdio_status);
    if (ret < 0)
    {
        DRV_PERI_SMI_UNLOCK(lchip);
        return DRV_E_MDIO_CMD_ERROR;
    }

    while ((mdio_status.mdio_x_g_cmd_done == 0) && (--timeout))
    {
        ret = DRV_IOCTL(lchip, 0, cmd, &mdio_status);
        if (ret < 0)
        {
            DRV_PERI_SMI_UNLOCK(lchip);
            return DRV_E_MDIO_CMD_ERROR;
        }
    }

    if (mdio_status.mdio_x_g_cmd_done == 0)
    {
        DRV_PERI_SMI_UNLOCK(lchip);
        return DRV_E_MDIO_CMD_TIMEOUT;
    }

    DRV_PERI_SMI_UNLOCK(lchip);

    return DRV_E_NONE;
}

int32
drv_greatbelt_peri_read_gephy_xgreg(uint8 lchip, uint8 mdio_bus, uint8 phy_addr, uint8 dev, uint16 reg, uint16* p_value)
{
    mdio_cmd1_g_t mdio_cmd;
    mdio_status1_g_t mdio_status;
    uint32 cmd = 0;
    uint32 timeout = 256;
    int32 ret = 0;

    DRV_PTR_VALID_CHECK(p_value);

    DRV_PERI_SMI_LOCK(lchip);

    sal_memset(&mdio_cmd, 0, sizeof(mdio_cmd1_g_t));
    sal_memset(&mdio_status, 0, sizeof(mdio_status1_g_t));

    /*Send address*/
    mdio_cmd.intf_sel_cmd1g = mdio_bus;
    mdio_cmd.dev_add_cmd1g = dev;
    mdio_cmd.op_code_cmd1g = 0x0; /*Op code for address*/
    mdio_cmd.port_add_cmd1g = phy_addr;
    mdio_cmd.start_cmd1g = 0x0;
    mdio_cmd.data_cmd1g = reg;

    cmd = DRV_IOW(MdioCmd1G_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, 0, cmd, &mdio_cmd);
    if (ret < 0)
    {
        DRV_PERI_SMI_UNLOCK(lchip);
        return DRV_E_MDIO_CMD_ERROR;
    }

    /* check status done */
    cmd = DRV_IOR(MdioStatus1G_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, 0, cmd, &mdio_status);
    if (ret < 0)
    {
        DRV_PERI_SMI_UNLOCK(lchip);
        return DRV_E_MDIO_CMD_ERROR;
    }

    while ((mdio_status.mdio1_g_cmd_done == 0) && (--timeout))
    {
        ret = DRV_IOCTL(lchip, 0, cmd, &mdio_status);
        if (ret < 0)
        {
            DRV_PERI_SMI_UNLOCK(lchip);
            return DRV_E_MDIO_CMD_ERROR;
        }
    }

    if (mdio_status.mdio1_g_cmd_done == 0)
    {
        DRV_PERI_SMI_UNLOCK(lchip);
        return DRV_E_MDIO_CMD_TIMEOUT;
    }

    /*send read request*/
    mdio_cmd.intf_sel_cmd1g = mdio_bus;
    mdio_cmd.dev_add_cmd1g = dev;
    mdio_cmd.op_code_cmd1g = DRV_XG_MDIO_READ; /* Op code for xg read */
    mdio_cmd.port_add_cmd1g = phy_addr;
    mdio_cmd.start_cmd1g = 0x0;

    cmd = DRV_IOW(MdioCmd1G_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, 0, cmd, &mdio_cmd);
    if (ret < 0)
    {
        DRV_PERI_SMI_UNLOCK(lchip);
        return DRV_E_MDIO_CMD_ERROR;
    }

    /* check status done */
    cmd = DRV_IOR(MdioStatus1G_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, 0, cmd, &mdio_status);
    if (ret < 0)
    {
        DRV_PERI_SMI_UNLOCK(lchip);
        return DRV_E_MDIO_CMD_ERROR;
    }

    while ((mdio_status.mdio1_g_cmd_done == 0) && (--timeout))
    {
        ret = DRV_IOCTL(lchip, 0, cmd, &mdio_status);
        if (ret < 0)
        {
            DRV_PERI_SMI_UNLOCK(lchip);
            return DRV_E_MDIO_CMD_ERROR;
        }
    }

    if (mdio_status.mdio1_g_cmd_done == 0)
    {
        DRV_PERI_SMI_UNLOCK(lchip);
        return DRV_E_MDIO_CMD_TIMEOUT;
    }
    /* read data */
    *p_value = mdio_status.mdio1_g_read_data;

    DRV_PERI_SMI_UNLOCK(lchip);

    return DRV_E_NONE;
}

/**
 @brief mdio interface for write xg phy
*/
int32
drv_greatbelt_peri_write_gephy_xgreg(uint8 lchip, uint8 mdio_bus, uint8 phy_addr, uint8 dev, uint16 reg, uint16 value)
{
    mdio_cmd1_g_t mdio_cmd;
    mdio_status1_g_t mdio_status;
    uint32 cmd = 0;
    uint32 timeout = 256;
    int32 ret = 0;

    DRV_PERI_SMI_LOCK(lchip);

    sal_memset(&mdio_cmd, 0, sizeof(mdio_cmd1_g_t));
    sal_memset(&mdio_status, 0, sizeof(mdio_status1_g_t));

    /*Send address*/
    mdio_cmd.intf_sel_cmd1g = mdio_bus;
    mdio_cmd.dev_add_cmd1g = dev;
    mdio_cmd.op_code_cmd1g = 0x0; /*Op code for address*/
    mdio_cmd.port_add_cmd1g = phy_addr;
    mdio_cmd.start_cmd1g = 0x0;
    mdio_cmd.data_cmd1g = reg;

    cmd = DRV_IOW(MdioCmd1G_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, 0, cmd, &mdio_cmd);
    if (ret < 0)
    {
        DRV_PERI_SMI_UNLOCK(lchip);
        return DRV_E_MDIO_CMD_ERROR;
    }

    /* check status done */
    cmd = DRV_IOR(MdioStatus1G_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, 0, cmd, &mdio_status);
    if (ret < 0)
    {
        DRV_PERI_SMI_UNLOCK(lchip);
        return DRV_E_MDIO_CMD_ERROR;
    }

    while ((mdio_status.mdio1_g_cmd_done == 0) && (--timeout))
    {
        ret = DRV_IOCTL(lchip, 0, cmd, &mdio_status);
        if (ret < 0)
        {
            DRV_PERI_SMI_UNLOCK(lchip);
            return DRV_E_MDIO_CMD_ERROR;
        }
    }

    if (mdio_status.mdio1_g_cmd_done == 0)
    {
        DRV_PERI_SMI_UNLOCK(lchip);
        return DRV_E_MDIO_CMD_TIMEOUT;
    }

    /* send write data */
    mdio_cmd.intf_sel_cmd1g = mdio_bus;
    mdio_cmd.dev_add_cmd1g = dev;
    mdio_cmd.op_code_cmd1g = DRV_MDIO_WRITE; /*Op code for write*/
    mdio_cmd.port_add_cmd1g = phy_addr;
    mdio_cmd.start_cmd1g = 0x0;
    mdio_cmd.data_cmd1g = value;

    cmd = DRV_IOW(MdioCmd1G_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, 0, cmd, &mdio_cmd);
    if (ret < 0)
    {
        DRV_PERI_SMI_UNLOCK(lchip);
        return DRV_E_MDIO_CMD_ERROR;
    }

    /* check status done */
    cmd = DRV_IOR(MdioStatus1G_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, 0, cmd, &mdio_status);
    if (ret < 0)
    {
        DRV_PERI_SMI_UNLOCK(lchip);
        return DRV_E_MDIO_CMD_ERROR;
    }

    while ((mdio_status.mdio1_g_cmd_done == 0) && (--timeout))
    {
        ret = DRV_IOCTL(lchip, 0, cmd, &mdio_status);
        if (ret < 0)
        {
            DRV_PERI_SMI_UNLOCK(lchip);
            return DRV_E_MDIO_CMD_ERROR;
        }
    }

    if (mdio_status.mdio1_g_cmd_done == 0)
    {
        DRV_PERI_SMI_UNLOCK(lchip);
        return DRV_E_MDIO_CMD_TIMEOUT;
    }

    DRV_PERI_SMI_UNLOCK(lchip);

    return DRV_E_NONE;
}
/**
 @brief smi interface for set auto scan ge phy optional reg
*/
int32
drv_greatbelt_peri_set_gephy_scan_special_reg(uint8 lchip, drv_peri_ge_opt_reg_t* p_gephy_opt, bool enable)
{
    uint32 cmd = 0;
    uint32 bitmap = 0;
    uint32 intr_enable = 0;
    uint32 speci_reg = 0;
    uint32 field_id;
    uint32 temp = 0;

    DRV_PTR_VALID_CHECK(p_gephy_opt);

    if (p_gephy_opt->reg_index > 1)
    {
        return DRV_E_INVALID_PARAMETER;
    }

    if (enable)
    {
        speci_reg = p_gephy_opt->reg;
        bitmap = p_gephy_opt->bit_ctl;
        intr_enable = p_gephy_opt->intr_enable;

        field_id = MdioSpeciCfg_Speci1GReg0Addr_f + p_gephy_opt->reg_index;

        /*config special reg */
        cmd = DRV_IOW(MdioSpeciCfg_t, field_id);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &speci_reg));

        field_id = MdioScanCtl_Speci0AddBmp1g_f + p_gephy_opt->reg_index;

        /* config bitmap ctrl */
        cmd = DRV_IOW(MdioScanCtl_t, field_id);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &bitmap));

        /* config whether gen interrupt or not */
        cmd = DRV_IOR(MdioScanCtl_t, MdioScanCtl_Specified1gIntrEn_f);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &temp));

        if (intr_enable)
        {
            temp |= (1 << (p_gephy_opt->reg_index));
        }
        else
        {
            temp &= (~(1 << (p_gephy_opt->reg_index)));
        }

        cmd = DRV_IOW(MdioScanCtl_t, MdioScanCtl_Specified1gIntrEn_f);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &temp));

        /* enable special reg */
        cmd = DRV_IOR(MdioScanCtl_t, MdioScanCtl_SpecifiedScanEn1g_f);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &temp));

        temp |= (1 << (p_gephy_opt->reg_index));

        cmd = DRV_IOW(MdioScanCtl_t, MdioScanCtl_SpecifiedScanEn1g_f);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &temp));
    }
    else
    {
        /* disable special reg */
        cmd = DRV_IOR(MdioScanCtl_t, MdioScanCtl_SpecifiedScanEn1g_f);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &temp));

        temp &= (~(1 << (p_gephy_opt->reg_index)));

        cmd = DRV_IOW(MdioScanCtl_t, MdioScanCtl_SpecifiedScanEn1g_f);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &temp));
    }

    return DRV_E_NONE;
}

/**
 @brief smi interface for set auto scan xg phy optional reg
*/
int32
drv_greatbelt_peri_set_xgphy_scan_special_reg(uint8 lchip, drv_peri_xg_opt_reg_t* xgphy_opt, bool enable)
{
    uint32 cmd = 0;
    uint32 bitmap = 0;
    uint32 intr_enable = 0;
    uint32 speci_reg = 0;
    uint32 speci_dev = 0;
    uint32 field_value = (TRUE == enable) ? 1 : 0;

    DRV_PTR_VALID_CHECK(xgphy_opt);

    if (enable)
    {
        speci_reg = xgphy_opt->reg;
        speci_dev = xgphy_opt->dev;
        bitmap = xgphy_opt->bit_ctl;
        intr_enable = xgphy_opt->intr_enable;

        /*config special reg */
        cmd = DRV_IOW(MdioSpeciCfg_t, MdioSpeciCfg_SpeciXgRegAddr_f);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &speci_reg));

        /*config special dev */
        cmd = DRV_IOW(MdioSpeciCfg_t, MdioSpeciCfg_SpeciXgDevAddr_f);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &speci_dev));

        /* config bitmap ctrl */
        cmd = DRV_IOW(MdioScanCtl_t, MdioScanCtl_SpeciAddBmpXg_f);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &bitmap));

        /* enable special reg intr*/
        cmd = DRV_IOW(MdioScanCtl_t, MdioScanCtl_SpecifiedXgIntrEn_f);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &intr_enable));

        /* enable scan special reg */
        cmd = DRV_IOW(MdioScanCtl_t, MdioScanCtl_SpecifiedScanEnXg_f);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    }
    else
    {
        /* disable scan special reg */
        cmd = DRV_IOW(MdioScanCtl_t, MdioScanCtl_SpecifiedScanEnXg_f);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    }

    return DRV_E_NONE;
}

/**
 @brief set auto scan para
*/
int32
drv_greatbelt_peri_set_phy_scan_para(uint8 lchip, drv_peri_phy_scan_ctrl_t* p_scan_ctrl)
{
    uint32 cmd = 0;
    mdio_scan_ctl_t scan_ctrl;
    mdio_use_phy_t use_phy;

    DRV_PTR_VALID_CHECK(p_scan_ctrl);

    cmd = DRV_IOR(MdioScanCtl_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &scan_ctrl));

    if (DRV_FLAG_ISSET(p_scan_ctrl->op_flag, DRV_PERI_INTERVAL_OP) || DRV_FLAG_ISZERO(p_scan_ctrl->op_flag))
    {
        scan_ctrl.scan_interval = p_scan_ctrl->scan_interval;
    }

    if (DRV_FLAG_ISSET(p_scan_ctrl->op_flag, DRV_PERI_GE_BITMAP_OP) || DRV_FLAG_ISZERO(p_scan_ctrl->op_flag))
    {
        scan_ctrl.mdio1g_scan_bmp0 = p_scan_ctrl->scan_gephy_bitmap0;
        scan_ctrl.mdio1g_scan_bmp1 = p_scan_ctrl->scan_gephy_bitmap1;
    }

    if (DRV_FLAG_ISSET(p_scan_ctrl->op_flag, DRV_PERI_XG_BITMAP_OP) || DRV_FLAG_ISZERO(p_scan_ctrl->op_flag))
    {
        scan_ctrl.mdio_xg_scan_bmp0 = p_scan_ctrl->scan_xgphy_bitmap0;
        scan_ctrl.mdio_xg_scan_bmp1 = p_scan_ctrl->scan_xgphy_bitmap1;
    }

    if (DRV_FLAG_ISSET(p_scan_ctrl->op_flag, DRV_PERI_XG_LINK_MASK_OP) || DRV_FLAG_ISZERO(p_scan_ctrl->op_flag))
    {
        scan_ctrl.xg_link_bmp_mask = p_scan_ctrl->xgphy_link_bitmask;
    }

    if (DRV_FLAG_ISSET(p_scan_ctrl->op_flag, DRV_PERI_XG_TWICE_OP) || DRV_FLAG_ISZERO(p_scan_ctrl->op_flag))
    {
        scan_ctrl.scan_xg_twice = p_scan_ctrl->xgphy_scan_twice;
    }

    cmd = DRV_IOW(MdioScanCtl_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &scan_ctrl));

    if (DRV_FLAG_ISSET(p_scan_ctrl->op_flag, DRV_PERI_USE_PHY_OP) || DRV_FLAG_ISZERO(p_scan_ctrl->op_flag))
    {
        cmd = DRV_IOR(MdioUsePhy_t, DRV_ENTRY_FLAG);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &use_phy));
        use_phy.use_phy31_0 = p_scan_ctrl->mdio_use_phy0;
        use_phy.use_phy59_32 = p_scan_ctrl->mdio_use_phy1;
        cmd = DRV_IOW(MdioUsePhy_t, DRV_ENTRY_FLAG);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &use_phy));
    }

    return DRV_E_NONE;
}

/**
 @brief set auto scan enable
*/
int32
drv_greatbelt_peri_set_phy_scan_en(uint8 lchip, bool enable)
{
    uint32 cmd = 0;
    uint32 field_value = (TRUE == enable) ? 1 : 0;

    cmd = DRV_IOW(MdioScanCtl_t, MdioScanCtl_ScanStart_f);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    return DRV_E_NONE;
}

/**
 @brief i2c master for read sfp, this interface is only for trigger read
*/
int32
drv_greatbelt_peri_i2c_read(uint8 lchip, drv_peri_i2c_read_t* p_read_para)
{
    uint32 cmd = 0;
    uint32 field_value = 0;
    uint32 timeout = 4 * p_read_para->length;
    uint32 total_len = 0;
    uint32 data_idx = 0;
    uint8  index = 0;
    int32 ret = 0;
    i2_c_master_read_cfg_t master_rd;
    i2_c_master_read_ctl_t read_ctl;
    i2_c_master_bmp_cfg_t bitmap_ctl;
    i2_c_master_status_t master_status;
    i2_c_master_data_ram_t data_buf;
    i2_c_master_read_status_t read_status;

    DRV_PTR_VALID_CHECK(p_read_para);

    DRV_PERI_I2C_LOCK(lchip);

    sal_memset(&master_rd, 0, sizeof(i2_c_master_read_cfg_t));
    sal_memset(&read_ctl, 0, sizeof(i2_c_master_read_ctl_t));
    sal_memset(&bitmap_ctl, 0, sizeof(i2_c_master_bmp_cfg_t));
    sal_memset(&read_status, 0, sizeof(i2_c_master_read_status_t));

    bitmap_ctl.slave_bit_map = p_read_para->slave_bitmap;
    cmd = DRV_IOW(I2CMasterBmpCfg_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, 0, cmd, &bitmap_ctl);
    if (ret < 0)
    {
        DRV_PERI_I2C_UNLOCK(lchip);
        return DRV_E_I2C_MASTER_CMD_ERROR;
    }

    master_rd.dev_addr = p_read_para->dev_addr;
    master_rd.length = p_read_para->length;
    master_rd.offset = p_read_para->offset;
    cmd = DRV_IOW(I2CMasterReadCfg_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, 0, cmd, &master_rd);
    if (ret < 0)
    {
        DRV_PERI_I2C_UNLOCK(lchip);
        return DRV_E_I2C_MASTER_CMD_ERROR;
    }

    read_ctl.i2c_master_polling_sel = 0;
    read_ctl.i2c_master_read_en = 1;
    cmd = DRV_IOW(I2CMasterReadCtl_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, 0, cmd, &read_ctl);
    if (ret < 0)
    {
        DRV_PERI_I2C_UNLOCK(lchip);
        return DRV_E_I2C_MASTER_CMD_ERROR;
    }

    /* polling status */
    cmd = DRV_IOR(I2CMasterStatus_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, 0, cmd, &master_status);
    if (ret < 0)
    {
        DRV_PERI_I2C_UNLOCK(lchip);
        return DRV_E_I2C_MASTER_CMD_ERROR;
    }

    while ((master_status.trigger_read_valid == 0) && (--timeout))
    {
        sal_task_sleep(1);
        ret = DRV_IOCTL(lchip, 0, cmd, &master_status);
        if (ret < 0)
        {
            DRV_PERI_I2C_UNLOCK(lchip);
            return DRV_E_I2C_MASTER_CMD_ERROR;
        }
    }

    if (master_status.trigger_read_valid == 0)
    {
        DRV_DBG_INFO("i2c master read cmd not done! line = %d\n",  __LINE__);
        DRV_PERI_I2C_UNLOCK(lchip);
        return DRV_E_I2C_MASTER_CMD_ERROR;
    }

    /* read data */
    for (index = 0; index < 32; index++)
    {
        if (((p_read_para->slave_bitmap) >> index) & 0x1)
        {
            total_len += p_read_para->length;
        }
    }

    cmd = DRV_IOR(I2CMasterDataRam_t, DRV_ENTRY_FLAG);

    for (index = 0; index < total_len; index++)
    {
        ret = DRV_IOCTL(lchip, index, cmd, &data_buf);
        if (ret < 0)
        {
            DRV_PERI_I2C_UNLOCK(lchip);
            return DRV_E_I2C_MASTER_CMD_ERROR;
        }

        p_read_para->buf[index] = data_buf.i2c_cpu_rd_data;
    }

    /* clear read cmd */
    field_value = 0;
    cmd = DRV_IOW(I2CMasterReadCtl_t, I2CMasterReadCtl_I2cMasterReadEn_f);
    ret = DRV_IOCTL(lchip, 0, cmd, &field_value);
    if (ret < 0)
    {
        DRV_PERI_I2C_UNLOCK(lchip);
        return DRV_E_I2C_MASTER_CMD_ERROR;
    }

    /*check read status */
    cmd = DRV_IOR(I2CMasterReadStatus_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, 0, cmd, &read_status);
    if (ret < 0)
    {
        DRV_PERI_I2C_UNLOCK(lchip);
        return DRV_E_I2C_MASTER_CMD_ERROR;
    }

    for (index = 0; index < 32; index++)
    {
        if (((p_read_para->slave_bitmap) >> index) & 0x1)
        {
            if (((read_status.i2c_read_status_bit_map) >> index) & 0x1)
            {
                if (data_idx >= total_len)
                {
                    DRV_PERI_I2C_UNLOCK(lchip);
                    return DRV_E_EXCEED_MAX_SIZE;
                }

                if (p_read_para->buf[data_idx] == 0xff)
                {
                    DRV_PERI_I2C_UNLOCK(lchip);
                    return DRV_E_I2C_MASTER_NACK_ERROR;
                }
                else if (p_read_para->buf[data_idx] == 0)
                {
                    DRV_PERI_I2C_UNLOCK(lchip);
                    return DRV_E_I2C_MASTER_CRC_ERROR;
                }
                else
                {
                    DRV_PERI_I2C_UNLOCK(lchip);
                    return DRV_E_I2C_MASTER_CMD_ERROR;
                }
            }

            data_idx += (p_read_para->length);
        }
    }

    DRV_PERI_I2C_UNLOCK(lchip);

    return DRV_E_NONE;
}

/**
 @brief set i2c polling read para
*/
int32
drv_greatbelt_peri_set_i2c_scan_para(uint8 lchip, drv_peri_i2c_scan_t* p_i2c_scan)
{
    uint32 cmd = 0;
    uint32 field_value = 0;
    i2_c_master_read_cfg_t master_rd;
    i2_c_master_read_ctl_t read_ctl;
    i2_c_master_bmp_cfg_t bitmap_ctl;

    DRV_PTR_VALID_CHECK(p_i2c_scan);

    sal_memset(&master_rd, 0, sizeof(i2_c_master_read_cfg_t));
    sal_memset(&read_ctl, 0, sizeof(i2_c_master_read_ctl_t));
    sal_memset(&bitmap_ctl, 0, sizeof(i2_c_master_bmp_cfg_t));

    if (DRV_FLAG_ISSET(p_i2c_scan->op_flag, DRV_PERI_SFP_BITMAP_OP) || DRV_FLAG_ISZERO(p_i2c_scan->op_flag))
    {
        bitmap_ctl.slave_bit_map = p_i2c_scan->slave_bitmap;
        cmd = DRV_IOW(I2CMasterBmpCfg_t, DRV_ENTRY_FLAG);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &bitmap_ctl));
    }

    if (DRV_FLAG_ISSET(p_i2c_scan->op_flag, DRV_PERI_SFP_SCAN_REG_OP) || DRV_FLAG_ISZERO(p_i2c_scan->op_flag))
    {
        master_rd.dev_addr = p_i2c_scan->dev_addr;
        master_rd.length = p_i2c_scan->length;
        master_rd.offset = p_i2c_scan->offset;
        cmd = DRV_IOW(I2CMasterReadCfg_t, DRV_ENTRY_FLAG);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &master_rd));
    }

    if (DRV_FLAG_ISSET(p_i2c_scan->op_flag, DRV_PERI_SFP_INTERVAL_OP) || DRV_FLAG_ISZERO(p_i2c_scan->op_flag))
    {
        cmd = DRV_IOW(I2CMasterPollingCfg_t, DRV_ENTRY_FLAG);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &master_rd));
    }

    field_value = 1;
    cmd = DRV_IOW(I2CMasterReadCtl_t, I2CMasterReadCtl_I2cMasterPollingSel_f);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    return DRV_E_NONE;
}

/**
 @brief i2c master polling read start or stop
*/
int32
drv_greatbelt_peri_set_i2c_scan_en(uint8 lchip, bool enable)
{
    uint32 cmd = 0;
    uint32 field_value = (TRUE == enable) ? 0 : 1;

    cmd = DRV_IOW(I2CMasterReadCtl_t, I2CMasterReadCtl_I2cMasterReadEn_f);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    return DRV_E_NONE;
}

/**
 @brief i2c master read databuffer ,the interface is used usually for polling read result
*/
int32
drv_greatbelt_peri_read_i2c_buf(uint8 lchip, uint32 len, uint8* p_buffer)
{
    uint32 index = 0;
    uint32 cmd = 0;
    i2_c_master_data_ram_t data_buf;

    DRV_PTR_VALID_CHECK(p_buffer);

    if (len > 256)
    {
        return DRV_E_ILLEGAL_LENGTH;
    }

    cmd = DRV_IOR(I2CMasterDataRam_t, DRV_ENTRY_FLAG);

    for (index = 0; index < len; index++)
    {
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &data_buf));

        p_buffer[index] = data_buf.i2c_cpu_rd_data;
    }

    return DRV_E_NONE;
}

/**
 @brief i2c master for write i2c device
*/
int32
drv_greatbelt_peri_i2c_write(uint8 lchip, drv_peri_i2c_write_t* p_i2c_para)
{
    uint32 cmd = 0;
    uint32 field_value = 0;
    uint32 timeout = 4;
    i2_c_master_status_t master_status;
    i2_c_master_wr_cfg_t master_wr;
    i2_c_master_read_cfg_t master_rd;
    int32 ret = 0;

    DRV_PTR_VALID_CHECK(p_i2c_para);

    DRV_PERI_I2C_LOCK(lchip);

    /* check status */
    cmd = DRV_IOR(I2CMasterReadCtl_t, I2CMasterReadCtl_I2cMasterReadEn_f);
    ret = DRV_IOCTL(lchip, 0, cmd, &field_value);
    if (ret < 0)
    {
        DRV_PERI_I2C_UNLOCK(lchip);
        return DRV_E_I2C_MASTER_CMD_ERROR;
    }

    if (field_value != 0)
    {
        DRV_PERI_I2C_UNLOCK(lchip);
        return DRV_E_I2C_MASTER_READ_NOT_CLEAR;
    }

    cmd = DRV_IOR(I2CMasterStatus_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, 0, cmd, &master_status);
    if (ret < 0)
    {
        DRV_PERI_I2C_UNLOCK(lchip);
        return DRV_E_I2C_MASTER_CMD_ERROR;
    }

    if (master_status.polling_done != 1)
    {
        /* chip init status polling_done is set */
        DRV_DBG_INFO("i2c master write check polling fail! line = %d\n",  __LINE__);
        DRV_PERI_I2C_UNLOCK(lchip);
        return DRV_E_I2C_MASTER_POLLING_NOT_DONE;
    }

    sal_memset(&master_wr, 0, sizeof(i2_c_master_wr_cfg_t));
    sal_memset(&master_rd, 0, sizeof(i2_c_master_read_cfg_t));

    master_rd.dev_addr = p_i2c_para->dev_addr;
    master_rd.offset = p_i2c_para->offset;

    cmd = DRV_IOW(I2CMasterReadCfg_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, 0, cmd, &master_rd);
    if (ret < 0)
    {
        DRV_PERI_I2C_UNLOCK(lchip);
        return DRV_E_I2C_MASTER_CMD_ERROR;
    }

    master_wr.i2c_wr_data = p_i2c_para->data;
    master_wr.i2c_wr_slave_id = p_i2c_para->slave_id;
    master_wr.i2c_wr_en = 1;

    cmd = DRV_IOW(I2CMasterWrCfg_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, 0, cmd, &master_wr);
    if (ret < 0)
    {
        DRV_PERI_I2C_UNLOCK(lchip);
        return DRV_E_I2C_MASTER_CMD_ERROR;
    }

    /* wait write op done */
    cmd = DRV_IOR(I2CMasterWrCfg_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, 0, cmd, &master_wr);
    if (ret < 0)
    {
        DRV_PERI_I2C_UNLOCK(lchip);
        return DRV_E_I2C_MASTER_CMD_ERROR;
    }

    while ((master_wr.i2c_wr_en == 1) && (--timeout))
    {
        sal_task_sleep(1);
        ret = DRV_IOCTL(lchip, 0, cmd, &master_wr);
        if (ret < 0)
        {
            DRV_PERI_I2C_UNLOCK(lchip);
            return DRV_E_I2C_MASTER_CMD_ERROR;
        }
    }

    if (master_wr.i2c_wr_en == 1)
    {
        DRV_DBG_INFO("i2c master write cmd not done! line = %d\n",  __LINE__);
        DRV_PERI_I2C_UNLOCK(lchip);
        return DRV_E_I2C_MASTER_CMD_ERROR;
    }

    DRV_PERI_I2C_UNLOCK(lchip);

    return DRV_E_NONE;
}

/**
 @brief mac led interface to get mac led mode cfg
*/
STATIC int32
_drv_greatbelt_peri_get_mac_led_mode_cfg(drv_peri_led_mode_t mode, uint8* p_value)
{
    uint8 temp = 0;

    switch(mode)
    {
        case DRV_PERI_RXLINK_MODE:
            temp = (DRV_LED_ON_RX_LINK & 0x3) | ((DRV_LED_BLINK_OFF & 0x3) << 2);
            break;

        case DRV_PERI_TXLINK_MODE:
            temp = (DRV_LED_ON_TX_LINK & 0x3) | ((DRV_LED_BLINK_OFF & 0x3) << 2);
            break;

        case DRV_PERI_RXLINK_RXACTIVITY_MODE:
            temp = (DRV_LED_ON_RX_LINK & 0x3) | ((DRV_LED_BLINK_RX_ACT & 0x3) << 2);
            break;

        case DRV_PERI_TXLINK_TXACTIVITY_MODE:
            temp = (DRV_LED_ON_TX_LINK & 0x3) | ((DRV_LED_BLINK_TX_ACT & 0x3) << 2);
            break;

        case DRV_PERI_RXLINK_BIACTIVITY_MODE:
            temp = (DRV_LED_ON_RX_LINK & 0x3) | ((DRV_LED_BLINK_ON & 0x3) << 2);
            break;

        case DRV_PERI_TXACTIVITY_MODE:
            temp = (DRV_LED_FORCE_OFF & 0x3) | ((DRV_LED_BLINK_TX_ACT & 0x3) << 2);
            break;

        case DRV_PERI_RXACTIVITY_MODE:
            temp = (DRV_LED_FORCE_OFF & 0x3) | ((DRV_LED_BLINK_RX_ACT & 0x3) << 2);
            break;

        case DRV_PERI_BIACTIVITY_MODE:
            temp = (DRV_LED_FORCE_OFF & 0x3) | ((DRV_LED_BLINK_ON & 0x3) << 2);
            break;

        case DRV_PERI_FORCE_ON_MODE:
            temp = (DRV_LED_FORCE_ON & 0x3) | ((DRV_LED_BLINK_OFF & 0x3) << 2);
            break;

        case DRV_PERI_FORCE_OFF_MODE:
            temp = (DRV_LED_FORCE_OFF & 0x3) | ((DRV_LED_BLINK_OFF & 0x3) << 2);
            break;

        case DRV_PERI_FORCE_ON_TXACTIVITY_MODE:
            temp = (DRV_LED_FORCE_ON & 0x3) | ((DRV_LED_BLINK_TX_ACT & 0x3) << 2);
            break;

        case DRV_PERI_FORCE_ON_RXACTIVITY_MODE:
            temp = (DRV_LED_FORCE_ON & 0x3) | ((DRV_LED_BLINK_RX_ACT & 0x3) << 2);
            break;

        case DRV_PERI_FORCE_ON_BIACTIVITY_MODE:
            temp = (DRV_LED_FORCE_ON & 0x3) | ((DRV_LED_BLINK_ON & 0x3) << 2);
            break;

        default:
            return DRV_E_INVALID_PARAMETER;
    }

    *p_value = temp;

    return DRV_E_NONE;
}

/**
 @brief mac led interface
*/
int32
drv_greatbelt_peri_set_mac_led_mode(uint8 lchip, drv_peri_led_para_t* p_led_para, drv_peri_mac_led_type_t led_type)
{
    uint32 cmd = 0;
    uint32 field_index = 0;
    mac_led_cfg_port_mode_t led_mode;
    mac_led_polarity_cfg_t led_polarity;
    uint8 led_cfg = 0;

    DRV_PTR_VALID_CHECK(p_led_para);

    if (led_type >= DRV_PERI_MAX_LED_TYPE)
    {
        return DRV_E_INVALID_PARAMETER;
    }

    sal_memset(&led_mode, 0, sizeof(mac_led_cfg_port_mode_t));
    sal_memset(&led_polarity, 0, sizeof(mac_led_polarity_cfg_t));

    if (DRV_FLAG_ISSET(p_led_para->op_flag, DRV_PERI_LED_MODE_SET_OP) || DRV_FLAG_ISZERO(p_led_para->op_flag))
    {
        DRV_IF_ERROR_RETURN(_drv_greatbelt_peri_get_mac_led_mode_cfg(p_led_para->first_mode, &led_cfg));

        led_mode.primary_led_mode = led_cfg;

        if (led_type == DRV_PERI_USING_TWO_LED)
        {
            DRV_IF_ERROR_RETURN(_drv_greatbelt_peri_get_mac_led_mode_cfg(p_led_para->sec_mode, &led_cfg));
            led_mode.secondary_led_mode = led_cfg;
            led_mode.secondary_led_mode_en = 1;
        }

        cmd = DRV_IOW(MacLedCfgPortMode_t, DRV_ENTRY_FLAG);
        field_index = p_led_para->port_id;
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, field_index, cmd, &led_mode));
    }

    if (DRV_FLAG_ISSET(p_led_para->op_flag, DRV_PERI_LED_POLARITY_SET_OP) || DRV_FLAG_ISZERO(p_led_para->op_flag))
    {
        /* set polarity for driving led 1: low driver led 0: high driver led */
        led_polarity.polarity_inv = p_led_para->polarity;
        cmd = DRV_IOW(MacLedPolarityCfg_t, DRV_ENTRY_FLAG);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &led_polarity));
    }

    return DRV_E_NONE;
}

/**
 @brief set mac and led mapping
*/
int32
drv_greatbelt_peri_set_mac_led_mapping(uint8 lchip, drv_peri_mac_led_mapping_t* p_led_mapping)
{
    uint32 index = 0;
    uint32 cmd = 0;

    mac_led_cfg_port_seq_map_t seq_map;
    mac_led_port_range_t port_range;

    DRV_PTR_VALID_CHECK(p_led_mapping);

    if ((p_led_mapping->mac_led_num > DRV_MAX_PHY_PORT) || (p_led_mapping->mac_led_num < 1))
    {
        return DRV_E_INVALID_PARAMETER;
    }

    sal_memset(&seq_map, 0, sizeof(mac_led_cfg_port_seq_map_t));
    sal_memset(&port_range, 0, sizeof(mac_led_port_range_t));

    /* set portseq */
    cmd = DRV_IOW(MacLedCfgPortSeqMap_t, DRV_ENTRY_FLAG);

    for (index = 0; index < p_led_mapping->mac_led_num; index++)
    {
        seq_map.mac_id = *((uint8*)p_led_mapping->p_mac_id + index);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &seq_map));
    }

    /* set MacLedPortRange */
    port_range.port_start_index = 0;
    port_range.port_end_index = p_led_mapping->mac_led_num - 1;
    cmd = DRV_IOW(MacLedPortRange_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &port_range));

    return DRV_E_NONE;
}

/**
 @brief set mac led enable
*/
int32
drv_greatbelt_peri_set_mac_led_en(uint8 lchip, bool enable)
{
    uint32 cmd = 0;
    mac_led_sample_interval_t led_sample;
    mac_led_raw_status_cfg_t raw_cfg;
    mac_led_cfg_cal_ctl_t cal_ctrl;
    uint32 field_value = (TRUE == enable) ? 1 : 0;

    sal_memset(&led_sample, 0, sizeof(mac_led_sample_interval_t));
    sal_memset(&raw_cfg, 0, sizeof(mac_led_raw_status_cfg_t));
    sal_memset(&cal_ctrl, 0, sizeof(mac_led_cfg_cal_ctl_t));

    cmd = DRV_IOW(MacLedCfgCalCtl_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &cal_ctrl));

    cmd = DRV_IOW(MacLedRawStatusCfg_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &raw_cfg));

    /* set sample enbale */
    cmd = DRV_IOR(MacLedSampleInterval_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &led_sample));

    led_sample.histogram_en = field_value;
    led_sample.sample_en = field_value;

    cmd = DRV_IOW(MacLedSampleInterval_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &led_sample));

    /* set refresh enable */
    cmd = DRV_IOW(MacLedRefreshInterval_t, MacLedRefreshInterval_RefreshEn_f);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    return DRV_E_NONE;
}

/**
 @brief set gpio mode interface
*/
int32
drv_greatbelt_peri_set_gpio_mode(uint8 lchip, uint8 gpio_id, drv_peri_gpio_mode_t gpio_mode)
{
    uint32 cmd = 0;
    sup_gpio_ctl_t gpio_ctl;

    if (gpio_id > DRV_MAX_GPIO_NUM - 1)
    {
        return DRV_E_INVALID_PARAMETER;
    }

    if (gpio_mode >= DRV_PERI_MAX_GPIO_MODE)
    {
        return DRV_E_INVALID_PARAMETER;
    }

    sal_memset(&gpio_ctl, 0, sizeof(sup_gpio_ctl_t));

    cmd = DRV_IOR(SupGpioCtl_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &gpio_ctl));

    switch (gpio_mode)
    {
    case DRV_PERI_INPUT_MODE:
        gpio_ctl.gpio_out_en &= (~((uint16)(1 << gpio_id)));

        if (gpio_id >= 4)
        {
            if (gpio_id < 12)
            {
                /* gpio id between 4 and 12 */
                gpio_ctl.gpio_scl_en &= (~((uint16)(1 << (gpio_id - 4))));
            }
            else if (gpio_id < 14)
            {
                /* gpio id between 12 and 14 */
                gpio_ctl.gpio_tod_en &= (~((uint16)(1 << (gpio_id - 12))));
            }
            else
            {
                /* gpio id between 14 and 15 */
                gpio_ctl.gpio_capture_en &= (~((uint16)(1 << (gpio_id - 14))));
            }
        }

        break;

    case DRV_PERI_OUTPUT_MODE:
        gpio_ctl.gpio_out_en |= ((uint16)(1 << gpio_id));

        if (gpio_id >= 4)
        {
            if (gpio_id < 12)
            {
                /* gpio id between 4 and 12 */
                gpio_ctl.gpio_scl_en &= (~((uint16)(1 << (gpio_id - 4))));
            }
            else if (gpio_id < 14)
            {
                /* gpio id between 12 and 14 */
                gpio_ctl.gpio_tod_en &= (~((uint16)(1 << (gpio_id - 12))));
            }
            else
            {
                /* gpio id between 14 and 15 */
                gpio_ctl.gpio_capture_en &= (~((uint16)(1 << (gpio_id - 14))));
            }
        }

        break;

    case DRV_PERI_SPECIAL_MODE:
        if (gpio_id < 4)
        {
            return DRV_E_INVALID_PARAMETER;
        }

        if (gpio_id < 12)
        {
            /* gpio id between 4 and 12 */
            gpio_ctl.gpio_scl_en |= ((uint16)(1 << (gpio_id - 4)));
        }
        else if (gpio_id < 14)
        {
            /* gpio id between 12 and 14 */
            gpio_ctl.gpio_tod_en |= ((uint16)(1 << (gpio_id - 12)));
        }
        else
        {
            /* gpio id between 14 and 15 */
            gpio_ctl.gpio_capture_en &= (~((uint16)(1 << (gpio_id - 14))));
        }

        break;

    default:
        return DRV_E_INVALID_PARAMETER;
    }

    cmd = DRV_IOW(SupGpioCtl_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &gpio_ctl));

    return DRV_E_NONE;
}

/**
 @brief gpio interface
*/
int32
drv_greatbelt_peri_set_gpio_output(uint8 lchip, uint8 gpio, uint8 out_para)
{
    uint32 cmd = 0;
    sup_gpio_ctl_t gpio_ctl;

    /* only gpio is set output mode can use this interface,this should be checked by caller */

    /* before value set, need carry on value reset */
    cmd = DRV_IOR(SupGpioCtl_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &gpio_ctl));

    gpio_ctl.gpio_wr_reset |= ((uint16)(1 << gpio));

    cmd = DRV_IOW(SupGpioCtl_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &gpio_ctl));

    /* set value */
    cmd = DRV_IOR(SupGpioCtl_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &gpio_ctl));

    gpio_ctl.gpio_wr_set &= ~((uint16)(1 << gpio));
    gpio_ctl.gpio_wr_set |= ((uint16)(out_para << gpio));

    cmd = DRV_IOW(SupGpioCtl_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &gpio_ctl));

    /* after value set, need carry on value reset */
    cmd = DRV_IOR(SupGpioCtl_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &gpio_ctl));

    gpio_ctl.gpio_wr_reset &= ~((uint16)(1 << gpio));

    cmd = DRV_IOW(SupGpioCtl_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &gpio_ctl));

    return DRV_E_NONE;
}

int32
drv_greatbelt_peri_get_gpio_input(uint8 lchip, uint8 gpio, uint8* in_value)
{
    uint32 cmd = 0;
    sup_gpio_ctl_t gpio_ctl;

    /* only gpio is set output mode can use this interface,this should be checked by caller */

    /* before value set, need carry on value reset */
    cmd = DRV_IOR(SupGpioCtl_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &gpio_ctl));

    *in_value = (((gpio_ctl.gpio_rd_data) & (1 << (gpio))) ? 1 : 0);

    return DRV_E_NONE;

}


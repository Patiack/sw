#include "goldengate/include/drv_lib.h"
#include "goldengate/include/drv_chip_ctrl.h"

/* use for testing cpu endian */
struct endian_test_e
{
    uint32 test1    :1;
    uint32 test2    :31;
};
typedef struct endian_test_e endian_test_t;

/* Driver init chip info */
drv_init_chip_info_t drv_gg_init_chip_info;

/* global host type flag */
host_type_t gg_host_type;

/* driver IO callback function */
drv_io_callback_fun_t drv_gg_io_api;

drv_work_platform_type_t g_gg_plaform_type;
drv_work_platform_type_t g_gg_workenv_type;
drv_access_type_t  g_gg_access_type;
static bool drv_gg_init_flag;

sal_mutex_t * gg_fib_acc_mutex;
sal_mutex_t * gg_cpu_acc_mutex;
sal_mutex_t * gg_ipfix_acc_mutex;



/**
 @brief get host type automatically. Little-Endian or Big-Endian.
*/
STATIC void
_drv_goldengate_get_host_type(void)
{
    endian_test_t test;
    uint32* p_test = NULL;

    p_test = (uint32 *)&test;
    *p_test = 0;
    test.test1 = 1;

    if (*p_test == 0x01)
    {
        gg_host_type = HOST_LE;
    }
    else
    {
        gg_host_type = HOST_BE;
    }


}

/**
 @brief get host endian
*/
STATIC int32
_drv_goldengate_preinit(void)
{
    _drv_goldengate_get_host_type();

    return DRV_E_NONE;
}


/**
 @brief init chip number and operation type in driver
*/
STATIC int32
_drv_goldengate_init_chipnum(uint8 chip_num)
{
    drv_gg_init_chip_info.drv_init_chip_num = chip_num;
    return DRV_E_NONE;
}

/**
 @brief init chip id base and operation type in driver
*/
STATIC int32
_drv_goldengate_init_chipid_base(uint8 chip_id_base)
{
    drv_gg_init_chip_info.drv_init_chipid_base = chip_id_base;
    return DRV_E_NONE;
}

/**
 @brief Install driver I/O API
*/
STATIC int32
_drv_goldengate_install_io_api(drv_work_platform_type_t platform_type)
{
     /*init a mux for fib acc and cpu acc*/
    gg_fib_acc_mutex = NULL;

    gg_cpu_acc_mutex = NULL;
    gg_ipfix_acc_mutex = NULL;         /*[SDK_Modify] */
    sal_mutex_create(&gg_fib_acc_mutex);
    sal_mutex_create(&gg_cpu_acc_mutex);
    sal_mutex_create(&gg_ipfix_acc_mutex); /*[SDK_Modify] */
    if (NULL == gg_fib_acc_mutex)
    {
        return DRV_E_INIT_FAILED;
    }
    if (NULL == gg_cpu_acc_mutex)
    {
        sal_mutex_destroy(gg_fib_acc_mutex);
        return DRV_E_INIT_FAILED;
    }

    /*[SDK_Modify] Add for ipfix acc */
    if (NULL == gg_ipfix_acc_mutex)
    {
        sal_mutex_destroy(gg_fib_acc_mutex);
        sal_mutex_destroy(gg_cpu_acc_mutex);
        return DRV_E_INIT_FAILED;
    }

    if (platform_type == HW_PLATFORM)   /* Operation real chip */
    {
        /* Sram operation I/O interfaces (according to table's index) */
        drv_gg_io_api.drv_sram_tbl_read = &drv_goldengate_chip_sram_tbl_read;
        drv_gg_io_api.drv_sram_tbl_write = &drv_goldengate_chip_sram_tbl_write;

        /* Sram operation I/O interfaces (according to address) */
        drv_gg_io_api.drv_sram_read_entry = &drv_goldengate_chip_read_sram_entry;
        drv_gg_io_api.drv_sram_write_entry = &drv_goldengate_chip_write_sram_entry;

        /* Sram indirect operation I/O interfaces (wait to add)*/
        drv_gg_io_api.drv_indirect_sram_tbl_ioctl = NULL;

        /* Tcam operation I/O interfaces */
        drv_gg_io_api.drv_tcam_tbl_read = drv_goldengate_chip_tcam_tbl_read;
        drv_gg_io_api.drv_tcam_tbl_write = drv_goldengate_chip_tcam_tbl_write;
        drv_gg_io_api.drv_tcam_tbl_remove = drv_goldengate_chip_tcam_tbl_remove;

        /* Hash operation I/O interfaces (wait to add)*/
        drv_gg_io_api.drv_hash_key_add_by_key = &drv_goldengate_model_hash_key_add_by_key;
        drv_gg_io_api.drv_hash_key_add_by_index = &drv_goldengate_model_hash_key_add_by_index;
        drv_gg_io_api.drv_hash_key_del_by_key = &drv_goldengate_model_hash_key_delete_by_key;
        drv_gg_io_api.drv_hash_key_del_by_index = &drv_goldengate_model_hash_key_delete_by_index;
        drv_gg_io_api.drv_hash_lookup = &drv_goldengate_model_hash_lookup;
        /* Field operation I/O interfaces */
        drv_gg_io_api.drv_set_field = drv_goldengate_set_field;
        drv_gg_io_api.drv_get_field = drv_goldengate_get_field;
        /* Sram & Tcam data convert interfaces between data Structure and entry(data stream) */
        drv_gg_io_api.drv_sram_ds_to_entry = drv_goldengate_sram_ds_to_entry;
        drv_gg_io_api.drv_sram_entry_to_ds = drv_goldengate_sram_entry_to_ds;
        drv_gg_io_api.drv_tcam_ds_to_entry = drv_goldengate_tcam_ds_to_entry;
        drv_gg_io_api.drv_tcam_entry_to_ds = drv_goldengate_tcam_entry_to_ds;

        /* CPU acc interface */
        drv_gg_io_api.drv_fib_acc_process = &drv_goldengate_chip_fib_acc_process;
        drv_gg_io_api.drv_cpu_acc_asic_lkup = &drv_goldengate_chip_cpu_acc_asic_lkup;

        /*[SDK_Modify]ipfix acc interface */
        drv_gg_io_api.drv_ipfix_acc_process = &drv_goldengate_chip_ipfix_acc_process;
    }
    else if (platform_type == SW_SIM_PLATFORM)/* Operation memory model */
    {
        /* for compile pass on emu */
#if (SDK_WORK_PLATFORM == 1)
        /* Sram operation I/O interfaces */
        drv_gg_io_api.drv_sram_tbl_read = &drv_goldengate_model_sram_tbl_read;
        drv_gg_io_api.drv_sram_tbl_write = &drv_goldengate_model_sram_tbl_write;

        /* Sram operation I/O interfaces (according to address) */
        drv_gg_io_api.drv_sram_read_entry = &drv_goldengate_model_read_sram_entry;
        drv_gg_io_api.drv_sram_write_entry = &drv_goldengate_model_write_sram_entry;

        drv_gg_io_api.drv_indirect_sram_tbl_ioctl = NULL;

        /* Tcam operation I/O interfaces */
        drv_gg_io_api.drv_tcam_tbl_read = &drv_goldengate_model_tcam_tbl_read;
        drv_gg_io_api.drv_tcam_tbl_write = &drv_goldengate_model_tcam_tbl_write;
        drv_gg_io_api.drv_tcam_tbl_remove = &drv_goldengate_model_tcam_tbl_remove;

        /* Hash operation I/O interfaces */
        drv_gg_io_api.drv_hash_key_add_by_key = &drv_goldengate_model_hash_key_add_by_key;
        drv_gg_io_api.drv_hash_key_add_by_index = &drv_goldengate_model_hash_key_add_by_index;
        drv_gg_io_api.drv_hash_key_del_by_key = &drv_goldengate_model_hash_key_delete_by_key;
        drv_gg_io_api.drv_hash_key_del_by_index = &drv_goldengate_model_hash_key_delete_by_index;
        drv_gg_io_api.drv_hash_lookup = &drv_goldengate_model_hash_lookup;

        /* Field operation I/O interfaces */
        drv_gg_io_api.drv_set_field = drv_goldengate_set_field;
        drv_gg_io_api.drv_get_field = drv_goldengate_get_field;
        /* Sram & Tcam data convert interfaces between data Structure and entry(data stream) */
        drv_gg_io_api.drv_sram_ds_to_entry = drv_goldengate_sram_ds_to_entry;
        drv_gg_io_api.drv_sram_entry_to_ds = drv_goldengate_sram_entry_to_ds;
        drv_gg_io_api.drv_tcam_ds_to_entry = drv_goldengate_tcam_ds_to_entry;
        drv_gg_io_api.drv_tcam_entry_to_ds = drv_goldengate_tcam_entry_to_ds;

        /* CPU acc interface */
        drv_gg_io_api.drv_fib_acc_process = &drv_goldengate_model_fib_acc_process;
        drv_gg_io_api.drv_cpu_acc_asic_lkup = &drv_goldengate_model_cpu_acc_asic_lkup;

        /*[SDK_Modify]ipfix acc interface */
        drv_gg_io_api.drv_ipfix_acc_process = &drv_goldengate_model_ipfix_acc_process;

#else
        /* Sram operation I/O interfaces */
        drv_gg_io_api.drv_sram_tbl_read = NULL;
        drv_gg_io_api.drv_sram_tbl_write = NULL;

        /* Sram operation I/O interfaces (according to address) */
        drv_gg_io_api.drv_sram_read_entry = NULL;
        drv_gg_io_api.drv_sram_write_entry = NULL;

        drv_gg_io_api.drv_indirect_sram_tbl_ioctl = NULL;

        /* Tcam operation I/O interfaces */
        drv_gg_io_api.drv_tcam_tbl_read = NULL;
        drv_gg_io_api.drv_tcam_tbl_write = NULL;
        drv_gg_io_api.drv_tcam_tbl_remove = NULL;

        /* Hash operation I/O interfaces */
        drv_gg_io_api.drv_hash_key_add_by_key = NULL;
        drv_gg_io_api.drv_hash_key_add_by_index = NULL;
        drv_gg_io_api.drv_hash_key_del_by_key = NULL;
        drv_gg_io_api.drv_hash_key_del_by_index = NULL;
        drv_gg_io_api.drv_hash_lookup = NULL;

        /* Field operation I/O interfaces */
        drv_gg_io_api.drv_set_field = drv_goldengate_set_field;
        drv_gg_io_api.drv_get_field = drv_goldengate_get_field;
        /* Sram & Tcam data convert interfaces between data Structure and entry(data stream) */
        drv_gg_io_api.drv_sram_ds_to_entry = drv_goldengate_sram_ds_to_entry;
        drv_gg_io_api.drv_sram_entry_to_ds = drv_goldengate_sram_entry_to_ds;
        drv_gg_io_api.drv_tcam_ds_to_entry = drv_goldengate_tcam_ds_to_entry;
        drv_gg_io_api.drv_tcam_entry_to_ds = drv_goldengate_tcam_entry_to_ds;

        /* Fib acc interface */
        drv_gg_io_api.drv_fib_acc_process = NULL;
        drv_gg_io_api.drv_cpu_acc_asic_lkup = NULL;

        /*[SDK_Modify]ipfix acc interface */
        drv_gg_io_api.drv_ipfix_acc_process = NULL;

#endif
    }
    else
    {
       return DRV_E_INVALD_RUNNING_ENV_TYPE;
    }

    return DRV_E_NONE;
}

STATIC int32
_drv_goldengate_mem_usrctrl_init(drv_work_platform_type_t platform_type)
{
    uint8 chip_id = 0;

    if (platform_type == HW_PLATFORM)
    {
        for (chip_id = drv_gg_init_chip_info.drv_init_chipid_base; \
             chip_id < drv_gg_init_chip_info.drv_init_chipid_base + drv_gg_init_chip_info.drv_init_chip_num; \
             chip_id++)
        {
            /* initialize tcam I/O mutex */
            DRV_IF_ERROR_RETURN(drv_goldengate_chip_tcam_mutex_init(chip_id-drv_gg_init_chip_info.drv_init_chipid_base));

            /* initialize pci I/O mutex */
            DRV_IF_ERROR_RETURN(drv_goldengate_chip_pci_mutex_init(chip_id - drv_gg_init_chip_info.drv_init_chipid_base));

            /* initialize i2c I/O mutex */
            DRV_IF_ERROR_RETURN(drv_goldengate_chip_i2c_mutex_init(chip_id - drv_gg_init_chip_info.drv_init_chipid_base));

            /* initialize Hss I/O mutex */
            DRV_IF_ERROR_RETURN(drv_goldengate_chip_hss_mutex_init(chip_id - drv_gg_init_chip_info.drv_init_chipid_base));
        }
    }
    else
    {
        return DRV_E_NONE;
    }

    return DRV_E_NONE;
}

STATIC int32
_drv_goldengate_init_cascade_entry(drv_work_platform_type_t platform_type)
{
    tbls_id_t tbl_id = 0;

    for(tbl_id = 0; tbl_id < MaxTblId_t; tbl_id++)
    {
        if (TABLE_ENTRY_TYPE(tbl_id) == SLICE_Cascade)
        {
            TABLE_MAX_INDEX(tbl_id) = 2*TABLE_MAX_INDEX(tbl_id);
        }
    }

    return DRV_E_NONE;
}

/**
  @driver init interface
*/
int32
drv_goldengate_init(uint8 chip_num, uint8 chip_id_base)
{
    if (drv_gg_init_flag)
    {
        return DRV_E_NONE;
    }

    g_gg_plaform_type = SDK_WORK_PLATFORM;
    g_gg_workenv_type = SDK_WORK_ENV;
    g_gg_access_type = DRV_PCI_ACCESS;
    _drv_goldengate_preinit();
    _drv_goldengate_init_chipnum(chip_num);
    _drv_goldengate_init_chipid_base(chip_id_base);
    DRV_IF_ERROR_RETURN(_drv_goldengate_install_io_api(g_gg_plaform_type));
    DRV_IF_ERROR_RETURN(_drv_goldengate_mem_usrctrl_init(g_gg_plaform_type));
    _drv_goldengate_init_cascade_entry(g_gg_plaform_type);
    DRV_IF_ERROR_RETURN(drv_goldengate_io_init());
    drv_gg_init_flag = 1;

    return DRV_E_NONE;
}

/**
  @driver get chip number interface
*/
int32
drv_goldengate_get_chipnum(uint8 *chipnum)
{
    if (drv_gg_init_flag)
    {
        *chipnum = drv_gg_init_chip_info.drv_init_chip_num;
        return DRV_E_NONE;
    }
    else
    {
        DRV_DBG_INFO("@@@ERROR, Driver is not initialized!\n");
        return DRV_E_INVALID_CHIP;
    }
}

/**
  @driver get chip id base
*/
int32
drv_goldengate_get_chipid_base(uint8 *chipid_base)
{
    if (drv_gg_init_flag)
    {
        *chipid_base = drv_gg_init_chip_info.drv_init_chipid_base;
        return DRV_E_NONE;
    }
    else
    {
        DRV_DBG_INFO("@@@ERROR, Driver is not initialized!\n");
        return DRV_E_INVALID_CHIP;
    }
}

/**
  @driver get chip id from chip offset
*/
int32
drv_goldengate_get_chipid_from_offset(uint8 chip_id_offset, uint8 *chip_id)
{
    if (drv_gg_init_flag)
    {
        *chip_id = drv_gg_init_chip_info.drv_init_chipid_base + chip_id_offset;
        return DRV_E_NONE;
    }
    else
    {
        DRV_DBG_INFO("@@@ERROR, Driver is not initialized!\n");
        return DRV_E_INVALID_CHIP;
    }
}

/**
  @driver get environmnet type interface
*/
int32
drv_goldengate_get_platform_type(drv_work_platform_type_t *plaform_type)
{
    if (drv_gg_init_flag)
    {
       *plaform_type = g_gg_plaform_type;
        return DRV_E_NONE;
    }
    else
    {
        DRV_DBG_INFO("@@@ERROR, Humber driver is not initialized!\n");
        return  DRV_E_INVALID_PARAMETER;
    }
}

int32
drv_goldengate_get_workenv_type(drv_work_env_type_t *workenv_type)
{
  if (drv_gg_init_flag)
    {
       *workenv_type = g_gg_workenv_type;
        return DRV_E_NONE;
    }
    else
    {
        return  DRV_E_INVALID_PARAMETER;
    }

  return DRV_E_NONE;
}

int32
drv_goldengate_get_host_type (uint8 lchip)
{
    return gg_host_type;
}


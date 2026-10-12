/**
 @file drv_io.c

 @date 2010-02-26

 @version v5.1

 The file contains all driver I/O interface realization
*/

#include "goldengate/include/drv_lib.h"
#include "goldengate/include/drv_chip_agent.h"
/**********************************************************************************
              Define Gloabal var, Typedef, define and Data Structure
***********************************************************************************/
/* TODO: ACL key lookup bitmap, remove it later */
static uint32 _drv_gg_acl_key_lookup_bitmap[DRV_ACL_LOOKUP_NUM] = {0};

static uint32 _drv_gg_tcam_key_lookup_bitmap[IG_USERID_1_TCAM_KEY_SHARE_TABLE + 1] = {0};

static hash_lookup_bitmap_t _drv_gg_hash_key_lookup_bitmap[HASH_MODULE_NUM];

static uint32 _drv_gg_dynamic_ram_couple_mode = 0;

uint32 g_gg_drv_io_is_asic = 0;
bool  gg_client_init_done = TRUE;

drv_io_t g_drv_gg_io_master;
#ifdef _SAL_LINUX_UM
drv_chip_agent_t g_drv_gg_chip_agent_master;
#endif
extern sal_mutex_t* p_gg_mep_mutex[MAX_LOCAL_CHIP_NUM];
extern drv_io_callback_fun_t drv_gg_io_api;

#define DRV_MEP_LOCK(chip_id_offset)         sal_mutex_lock(p_gg_mep_mutex[chip_id_offset])
#define DRV_MEP_UNLOCK(chip_id_offset)       sal_mutex_unlock(p_gg_mep_mutex[chip_id_offset])


/**********************************************************************************
                      Function interfaces realization
***********************************************************************************/

/**
 @brief judge the IO is real ASIC for EADP
*/
int32
drv_goldengate_io_is_asic(void)
{
    return g_gg_drv_io_is_asic;
}


STATIC int32
drv_goldengate_ioctl_mep(uint8 chip_id, int32 index, uint32 cmd, void* val)
{
    tbls_id_t tbl_id;
    uint32 data_entry[MAX_ENTRY_WORD] = {0};
    uint32 chip_id_offset = 0;


    if (g_drv_gg_io_master.chip_agent_cb&&(TRUE == gg_client_init_done))
    {
        chip_io_para_t para;
        para.op = CHIP_IO_OP_IOCTL;
        para.u.ioctl.chip_id = chip_id;
        para.u.ioctl.index = index;
        para.u.ioctl.cmd = cmd;
        para.u.ioctl.val = val;
        return g_drv_gg_io_master.chip_agent_cb(chip_id, &para);
    }
    else if(DRV_CHIP_AGT_MODE_CLIENT == drv_goldengate_chip_agent_mode())
    {
        return 0;
    }

    if ((drv_gg_init_chip_info.drv_init_chipid_base
         + drv_gg_init_chip_info.drv_init_chip_num) <= (chip_id))
    {
        DRV_DBG_INFO("\nERROR! INVALID ChipID! chipid: %d, file:%s line:%d function:%s\n", chip_id, __FILE__, __LINE__, __FUNCTION__);
        return DRV_E_INVALID_CHIP;
    }

    DRV_PTR_VALID_CHECK(val);

    tbl_id = DRV_IOC_MEMID(cmd);
    chip_id_offset = chip_id - drv_gg_init_chip_info.drv_init_chipid_base;

    DRV_TBL_ID_VALID_CHECK(tbl_id);
    if ((chip_id_offset >= MAX_LOCAL_CHIP_NUM) || (chip_id_offset < 0))
    {
        DRV_DBG_INFO("Error chip_id_offset: %d, tbl_id %d\n", chip_id_offset, tbl_id);
        return DRV_E_INVALID_CHIP;
    }

#if (SDK_WORK_PLATFORM == 0)
    DRV_MEP_LOCK(chip_id_offset);
    {
	uint32 mask_entry[MAX_ENTRY_WORD] = {0};
    /* Write Sram all the entry */
    if (drv_gg_io_api.drv_sram_ds_to_entry)
    {
        DRV_IF_ERROR_RETURN(drv_gg_io_api.drv_sram_ds_to_entry(tbl_id, ((tbl_entry_t*)val)->mask_entry, mask_entry));
    }
    DRV_IF_ERROR_RETURN(drv_gg_io_api.drv_sram_tbl_write(chip_id_offset, OamDsMpDataMask_t, 0, mask_entry));
    }
#endif
    if (drv_gg_io_api.drv_sram_ds_to_entry)
    {
        DRV_IF_ERROR_RETURN(drv_gg_io_api.drv_sram_ds_to_entry(tbl_id, ((tbl_entry_t*)val)->data_entry, data_entry));
    }
    if (drv_gg_io_api.drv_sram_tbl_write)
    {
        DRV_IF_ERROR_RETURN(drv_gg_io_api.drv_sram_tbl_write(chip_id_offset, tbl_id, index, data_entry));
    }

#if (SDK_WORK_PLATFORM == 0)
    DRV_MEP_UNLOCK(chip_id_offset);
#endif
    return DRV_E_NONE;
}



int32
drv_goldengate_ioctl_api(uint8 chip_id, int32 index, uint32 cmd, void* val)
{
    tbls_id_t tbl_id;
    uint16 field_id;
    uint32 action;

 /*    DRV_IO_ADD_STATS(CHIP_IO_OP_IOCTL);*/

    if (g_drv_gg_io_master.debug_cb)
    {
        chip_io_para_t para;
        para.op = CHIP_IO_OP_IOCTL;
        para.u.ioctl.chip_id = chip_id;
        para.u.ioctl.index = index;
        para.u.ioctl.cmd = cmd;
        para.u.ioctl.val = val;
        g_drv_gg_io_master.debug_cb(chip_id, &para);
    }

    action = DRV_IOC_OP(cmd);
    tbl_id = DRV_IOC_MEMID(cmd);
    field_id = DRV_IOC_FIELDID(cmd);

#if 1
 /* mep ???*/
    if(((DsEthMep_t == tbl_id) || (DsEthRmep_t == tbl_id) || (DsBfdMep_t == tbl_id) || (DsBfdRmep_t == tbl_id))
        && (DRV_ENTRY_FLAG == field_id) && (DRV_IOC_WRITE == action))
    {
        DRV_IF_ERROR_RETURN(drv_goldengate_ioctl_mep(chip_id, index, cmd, val));
    }
    else
#endif
    {
        if ((DsDestPort_t != tbl_id) || (!g_drv_gg_io_master.gb_gg_interconnect_en) || (index <320) || (index >= 368) || (DRV_IOC_WRITE != action))
        {
            DRV_IF_ERROR_RETURN(drv_goldengate_ioctl(chip_id, index, cmd, val));
        }
        if ((DsDestPort_t == tbl_id) && g_drv_gg_io_master.gb_gg_interconnect_en && (index >= 256) && (index < 304) && (DRV_IOC_WRITE == action))
        {
            DRV_IF_ERROR_RETURN(drv_goldengate_ioctl(chip_id, index+64, cmd, val));
        }
    }

    return DRV_E_NONE;
}


/**
 @brief The function is the table I/O control API
*/
int32
drv_goldengate_ioctl(uint8 chip_id, int32 index, uint32 cmd, void* val)
{
    int32 action;
    tbl_entry_t entry;
    tbls_id_t tbl_id;
    uint16 field_id;
    uint32* p_data_entry = NULL;
    uint32* p_mask_entry = NULL;
    uint32 chip_id_offset = 0;
    #if (SDK_WORK_PLATFORM == 0)
    uint32 value0 = 0;
    #endif
    int32 ret = DRV_E_NONE;

    DRV_CHIP_ID_VALID_CHECK(chip_id);
    DRV_PTR_VALID_CHECK(val);

    action = DRV_IOC_OP(cmd);
    tbl_id = DRV_IOC_MEMID(cmd);
    field_id = DRV_IOC_FIELDID(cmd);
    chip_id_offset = chip_id - drv_gg_init_chip_info.drv_init_chipid_base;

    if (g_drv_gg_io_master.chip_agent_cb&&(TRUE == gg_client_init_done))
    {
        chip_io_para_t para;
        para.op = CHIP_IO_OP_IOCTL;
        para.u.ioctl.chip_id = chip_id;
        para.u.ioctl.index = index;
        para.u.ioctl.cmd = cmd;
        para.u.ioctl.val = val;
        return g_drv_gg_io_master.chip_agent_cb(chip_id, &para);
    }
    else if(DRV_CHIP_AGT_MODE_CLIENT == drv_goldengate_chip_agent_mode())
    {
        return 0;
    }

    if ((chip_id_offset>= MAX_LOCAL_CHIP_NUM) || (chip_id_offset<0))
    {
        DRV_DBG_INFO("Error chip_id_offset: %d, tbl_id %d\n", chip_id_offset, tbl_id);
        return DRV_E_INVALID_CHIP;
    }

    sal_memset(&entry, 0, sizeof(entry));
    DRV_TBL_ID_VALID_CHECK(tbl_id);

    if(action == DRV_IOC_WRITE && (g_drv_gg_io_master.wb_status[chip_id] == DRV_WB_STATUS_RELOADING))
   	{
      return 0;
    }

    p_data_entry = (uint32*)mem_malloc(MEM_SYSTEM_MODULE, sizeof(uint32)*MAX_ENTRY_WORD);
    p_mask_entry = (uint32*)mem_malloc(MEM_SYSTEM_MODULE, sizeof(uint32)*MAX_ENTRY_WORD);
    if ((NULL == p_data_entry) || (NULL == p_mask_entry))
    {
        ret = DRV_E_NO_MEMORY;
        goto out;
    }
    sal_memset(p_data_entry, 0, sizeof(uint32)*MAX_ENTRY_WORD);
    sal_memset(p_mask_entry, 0, sizeof(uint32)*MAX_ENTRY_WORD);

    entry.data_entry = p_data_entry;
    entry.mask_entry = p_mask_entry;

    if (DRV_ENTRY_FLAG == field_id) /* the whole entry operation */
    {
        switch (action)
        {
        case DRV_IOC_WRITE:
            if ((!drv_goldengate_table_is_tcam_key(tbl_id))
                && (!drv_goldengate_table_is_lpm_tcam_key(tbl_id)))
            {
#if (SDK_WORK_PLATFORM == 0)
                /* Write Sram all the entry */
                if((tbl_id == DsEthMep_t) || (tbl_id == DsEthRmep_t) || (tbl_id == DsBfdMep_t) || (tbl_id == DsBfdRmep_t))
                {
                    sal_memset(p_data_entry, 0, sizeof(uint32)*MAX_ENTRY_WORD);
                    DRV_IF_ERROR_GOTO(drv_gg_io_api.drv_sram_tbl_write(chip_id_offset, OamDsMpDataMask_t, 0, p_data_entry), ret, out);
                }
#endif
                if (drv_gg_io_api.drv_sram_ds_to_entry)
                {
                    DRV_IF_ERROR_GOTO(drv_gg_io_api.drv_sram_ds_to_entry(tbl_id, val, p_data_entry), ret, out);
                }

                if (drv_gg_io_api.drv_sram_tbl_write)
                {
                    DRV_IF_ERROR_GOTO(drv_gg_io_api.drv_sram_tbl_write(chip_id_offset, tbl_id, index, p_data_entry), ret, out);
                }
            }
            else
            {
                /* Write Tcam all the entry */
                if (drv_gg_io_api.drv_tcam_ds_to_entry)
                {
                    DRV_IF_ERROR_GOTO(drv_gg_io_api.drv_tcam_ds_to_entry(tbl_id, val, (tbl_entry_t *)&entry), ret, out);
                }

                if (drv_gg_io_api.drv_tcam_tbl_write)
                {
                    DRV_IF_ERROR_GOTO(drv_gg_io_api.drv_tcam_tbl_write(chip_id_offset, tbl_id, index, &entry), ret, out);
                }
            }
            break;
        case DRV_IOC_READ:
            if ((!drv_goldengate_table_is_tcam_key(tbl_id))
                && (!drv_goldengate_table_is_lpm_tcam_key(tbl_id)))
            {
                /* Read Sram all the entry */
                if (drv_gg_io_api.drv_sram_tbl_read)
                {
                    DRV_IF_ERROR_GOTO(drv_gg_io_api.drv_sram_tbl_read(chip_id_offset, tbl_id, index, (uint32 *)p_data_entry), ret, out);
                }

                if (drv_gg_io_api.drv_sram_entry_to_ds)
                {
                    DRV_IF_ERROR_GOTO(drv_gg_io_api.drv_sram_entry_to_ds(tbl_id, (uint32 *)p_data_entry, val), ret, out);
                }

            }
            else
            {
                /* Read Tcam all the entry */
                if (drv_gg_io_api.drv_tcam_tbl_read)
                {
                    DRV_IF_ERROR_GOTO(drv_gg_io_api.drv_tcam_tbl_read(chip_id_offset, tbl_id, index, &entry), ret, out);
                }

                if (drv_gg_io_api.drv_tcam_entry_to_ds)
                {
                    DRV_IF_ERROR_GOTO(drv_gg_io_api.drv_tcam_entry_to_ds(tbl_id, (tbl_entry_t *)&entry, val), ret, out);
                }
            }
            break;
        default:
            break;
        }
    }
    else                             /* per-field operation */
    {
        switch (action)
        {
        case DRV_IOC_WRITE:
            if ((!drv_goldengate_table_is_tcam_key(tbl_id))
                && (!drv_goldengate_table_is_lpm_tcam_key(tbl_id)))
            {
                /* Write Sram one field of the entry */
#if (SDK_WORK_PLATFORM == 0)
                if((tbl_id == DsEthMep_t) || (tbl_id == DsEthRmep_t) || (tbl_id == DsBfdMep_t) || (tbl_id == DsBfdRmep_t))
                {
                    /*set mask according field id*/
                    sal_memset(p_mask_entry, 0xFF, sizeof(uint32)*MAX_ENTRY_WORD);
                    DRV_IF_ERROR_GOTO(drv_gg_io_api.drv_set_field(tbl_id, field_id, p_mask_entry, &value0), ret, out);
                    DRV_IF_ERROR_GOTO(drv_gg_io_api.drv_sram_tbl_write(chip_id_offset, OamDsMpDataMask_t, 0, p_mask_entry), ret, out);
                    sal_memset(p_data_entry, 0, sizeof(uint32)*MAX_ENTRY_WORD);
                }
                else
                {
                    if (drv_gg_io_api.drv_sram_tbl_read)
                    {
                        DRV_IF_ERROR_GOTO(drv_gg_io_api.drv_sram_tbl_read(chip_id_offset, tbl_id, index, p_data_entry), ret, out);
                    }
                }
#else
                if (drv_gg_io_api.drv_sram_tbl_read)
                {
                    DRV_IF_ERROR_GOTO(drv_gg_io_api.drv_sram_tbl_read(chip_id_offset, tbl_id, index, p_data_entry), ret, out);
                }
#endif
                if (drv_gg_io_api.drv_set_field)
                {
                    DRV_IF_ERROR_GOTO(drv_gg_io_api.drv_set_field(tbl_id, field_id, p_data_entry, (uint32*)val), ret, out);
                }

                if (drv_gg_io_api.drv_sram_tbl_write)
                {
                    DRV_IF_ERROR_GOTO(drv_gg_io_api.drv_sram_tbl_write(chip_id_offset, tbl_id, index, p_data_entry), ret, out);
                }
            }
            else
            {
                /* Write Tcam one field of the entry */
                /* For Tcam entry, we could only operate its data's filed, app should not opreate tcam field */
                /*
                DRV_IF_ERROR_RETURN(drv_goldengate_tcam_field_chk(tbl_id, field_id));
                if (drv_gg_io_api.drv_tcam_tbl_read)
                {
                    DRV_IF_ERROR_RETURN(drv_gg_io_api.drv_tcam_tbl_read(chip_id_offset, tbl_id, index, &entry));
                }

                if (drv_gg_io_api.drv_set_field)
                {
                    DRV_IF_ERROR_RETURN(drv_gg_io_api.drv_set_field(tbl_id, field_id, entry.data_entry, *(uint32*)val));
                }

                if (drv_gg_io_api.drv_tcam_tbl_write)
                {
                    DRV_IF_ERROR_RETURN(drv_gg_io_api.drv_tcam_tbl_write(chip_id_offset, tbl_id, index, &entry));
                }
                */
                DRV_IF_ERROR_GOTO(drv_goldengate_tcam_field_chk(tbl_id, field_id), ret, out);
                if (drv_gg_io_api.drv_tcam_tbl_read)
                {
                    DRV_IF_ERROR_GOTO(drv_gg_io_api.drv_tcam_tbl_read(chip_id_offset, tbl_id, index, &entry), ret, out);
                }


                if (drv_gg_io_api.drv_set_field)
                {
                    DRV_IF_ERROR_GOTO(drv_gg_io_api.drv_set_field(tbl_id, field_id, entry.data_entry, (uint32 *)((tbl_entry_t*)val)->data_entry), ret, out);
                    DRV_IF_ERROR_GOTO(drv_gg_io_api.drv_set_field(tbl_id, field_id, entry.mask_entry, (uint32 *)((tbl_entry_t*)val)->mask_entry), ret, out);
                }

                if (drv_gg_io_api.drv_tcam_tbl_write)
                {
                    DRV_IF_ERROR_GOTO(drv_gg_io_api.drv_tcam_tbl_write(chip_id_offset, tbl_id, index, &entry), ret, out);
                }
            }
            break;
        case DRV_IOC_READ:
            if ((!drv_goldengate_table_is_tcam_key(tbl_id))
                && (!drv_goldengate_table_is_lpm_tcam_key(tbl_id)))
            {
                /* Read Sram one field of the entry */
                if (drv_gg_io_api.drv_sram_tbl_read)
                {
                    DRV_IF_ERROR_GOTO(drv_gg_io_api.drv_sram_tbl_read(chip_id_offset, tbl_id, index, p_data_entry), ret, out);
                }

                if (drv_gg_io_api.drv_get_field)
                {
                    DRV_IF_ERROR_GOTO(drv_gg_io_api.drv_get_field(tbl_id, field_id, p_data_entry, (uint32*)val), ret, out);
                }
            }
            else
            {
                /* Read Tcam one field of the entry */
                /* For Tcam entry, we could only operate its data's filed, app should not opreate tcam field */
                DRV_IF_ERROR_GOTO(drv_goldengate_tcam_field_chk(tbl_id, field_id), ret, out);

                if (drv_gg_io_api.drv_tcam_tbl_read)
                {
                    DRV_IF_ERROR_GOTO(drv_gg_io_api.drv_tcam_tbl_read(chip_id_offset, tbl_id, index, &entry), ret, out);
                }

                if (drv_gg_io_api.drv_get_field)
                {
                    DRV_IF_ERROR_GOTO(drv_gg_io_api.drv_get_field(tbl_id, field_id, (entry.data_entry), (uint32 *)((tbl_entry_t*)val)->data_entry), ret, out);
                    DRV_IF_ERROR_GOTO(drv_gg_io_api.drv_get_field(tbl_id, field_id, (entry.mask_entry), (uint32 *)((tbl_entry_t*)val)->mask_entry), ret, out);
                }
            }
            break;
        default:
            break;
        }
    }

out:
    if (p_data_entry)
    {
        mem_free(p_data_entry);
    }

    if (p_mask_entry)
    {
        mem_free(p_mask_entry);
    }
    return ret;
}

/**
 @brief add or modify tcam data
*/
int32
drv_goldengate_tcam_tbl_add(uint8 chip_id, int32 index, uint32 cmd, void* val)
{
    int32 action;
    tbl_entry_t entry;
    tbls_id_t tbl_id;
    uint16 field_id;
    uint32* p_data_entry = NULL;
    uint32* p_mask_entry = NULL;
    uint32 chip_id_offset = 0;
    int32 ret = DRV_E_NONE;

    DRV_CHIP_ID_VALID_CHECK(chip_id);
    DRV_PTR_VALID_CHECK(val);

    action = DRV_IOC_OP(cmd);
    tbl_id = DRV_IOC_MEMID(cmd);
    field_id = DRV_IOC_FIELDID(cmd);
    chip_id_offset = chip_id - drv_gg_init_chip_info.drv_init_chipid_base;

    if ((chip_id_offset>= MAX_LOCAL_CHIP_NUM) || (chip_id_offset<0))
    {
        DRV_DBG_INFO("Error chip_id_offset: %d, tbl_id %d\n", chip_id_offset, tbl_id);
        return DRV_E_INVALID_CHIP;
    }

    DRV_TBL_ID_VALID_CHECK(tbl_id);

    if ((chip_id_offset>= MAX_LOCAL_CHIP_NUM) || (chip_id_offset<0))
    {
        DRV_DBG_INFO("Error chip_id_offset: %d, tbl_id %d\n", chip_id_offset, tbl_id);
        return DRV_E_INVALID_CHIP;
    }

    sal_memset(&entry, 0, sizeof(entry));
    p_data_entry = mem_malloc(MEM_SYSTEM_MODULE, sizeof(uint32)*MAX_ENTRY_WORD);
    p_mask_entry = mem_malloc(MEM_SYSTEM_MODULE, sizeof(uint32)*MAX_ENTRY_WORD);
    if ((NULL == p_data_entry) || (NULL == p_mask_entry))
    {
        ret = DRV_E_NO_MEMORY;
        goto out;
    }
    sal_memset(p_data_entry, 0, sizeof(uint32)*MAX_ENTRY_WORD);
    sal_memset(p_mask_entry, 0, sizeof(uint32)*MAX_ENTRY_WORD);

    entry.data_entry = p_data_entry;
    entry.mask_entry = p_mask_entry;

    DRV_TBL_ID_VALID_CHECK(tbl_id);

    if (DRV_ENTRY_FLAG == field_id) /* the whole entry operation */
    {
        switch (action)
        {
            case DRV_IOC_WRITE:

                /* Write Tcam all the entry */
                if (drv_gg_io_api.drv_tcam_ds_to_entry)
                {
                    DRV_IF_ERROR_GOTO(drv_gg_io_api.drv_tcam_ds_to_entry(tbl_id, val, (tbl_entry_t *)&entry), ret, out);
                }

                if (drv_gg_io_api.drv_tcam_tbl_write)
                {
                    DRV_IF_ERROR_GOTO(drv_gg_io_api.drv_tcam_tbl_write(chip_id_offset, tbl_id, index, &entry), ret, out);
                }
                break;
            case DRV_IOC_READ:

                /* Read Tcam all the entry */
                if (drv_gg_io_api.drv_tcam_tbl_read)
                {
                    DRV_IF_ERROR_GOTO(drv_gg_io_api.drv_tcam_tbl_read(chip_id_offset, tbl_id, index, &entry), ret, out);
                }

                if (drv_gg_io_api.drv_tcam_entry_to_ds)
                {
                    DRV_IF_ERROR_GOTO(drv_gg_io_api.drv_tcam_entry_to_ds(tbl_id, (tbl_entry_t *)&entry, val), ret, out);
                }

            break;
        default:
            break;
        }
    }
    else                             /* per-field operation */
    {
        switch (action)
        {
            case DRV_IOC_WRITE:
              /* Write Tcam one field of the entry */
                /* For Tcam entry, we could only operate its data's filed, app should not opreate tcam field */

                if (drv_gg_io_api.drv_tcam_tbl_read)
                {
                    DRV_IF_ERROR_GOTO(drv_gg_io_api.drv_tcam_tbl_read(chip_id_offset, tbl_id, index, &entry), ret, out);
                }

                if (drv_gg_io_api.drv_set_field)
                {
                    DRV_IF_ERROR_GOTO(drv_gg_io_api.drv_set_field(tbl_id, field_id, entry.data_entry, (uint32*)val), ret, out);
                }

                if (drv_gg_io_api.drv_tcam_tbl_write)
                {
                    DRV_IF_ERROR_GOTO(drv_gg_io_api.drv_tcam_tbl_write(chip_id_offset, tbl_id, index, &entry), ret, out);
                }

                break;
            case DRV_IOC_READ:
               /* Read Tcam one field of the entry */
                /* For Tcam entry, we could only operate its data's filed, app should not opreate tcam field */
                if (drv_gg_io_api.drv_tcam_tbl_read)
                {
                    DRV_IF_ERROR_GOTO(drv_gg_io_api.drv_tcam_tbl_read(chip_id_offset, tbl_id, index, &entry), ret, out);
                }

                if (drv_gg_io_api.drv_get_field)
                {
                    DRV_IF_ERROR_GOTO(drv_gg_io_api.drv_get_field(tbl_id, field_id, (entry.data_entry), (uint32*)val), ret, out);
                }

            break;
            default:
                break;
        }
    }

out:
    if (p_data_entry)
    {
        mem_free(p_data_entry);
    }

    if (p_mask_entry)
    {
        mem_free(p_mask_entry);
    }
    return ret;
}

/**
 @brief remove tcam entry interface according to key id and index
*/
int32
drv_goldengate_tcam_tbl_remove(uint8 chip_id, tbls_id_t tbl_id, uint32 index)
{
    uint32 chip_id_offset = 0;

    chip_id_offset = chip_id - drv_gg_init_chip_info.drv_init_chipid_base;


    if (g_drv_gg_io_master.chip_agent_cb&&(TRUE == gg_client_init_done))
    {
        chip_io_para_t para;
        para.op = CHIP_IO_OP_TCAM_REMOVE;
        para.u.tcam_remove.chip_id = chip_id;
        para.u.tcam_remove.tbl_id = tbl_id;
        para.u.tcam_remove.index = index;
        return g_drv_gg_io_master.chip_agent_cb(chip_id, &para);
    }
    else if(DRV_CHIP_AGT_MODE_CLIENT == drv_goldengate_chip_agent_mode())
    {
        return 0;
    }

    if (g_drv_gg_io_master.debug_cb)
    {
        chip_io_para_t para;
        para.op = CHIP_IO_OP_TCAM_REMOVE;
        para.u.tcam_remove.chip_id = chip_id;
        para.u.tcam_remove.tbl_id = tbl_id;
        para.u.tcam_remove.index = index;
        g_drv_gg_io_master.debug_cb(0, &para);
    }


    if (drv_gg_io_api.drv_tcam_tbl_remove)
    {
        DRV_IF_ERROR_RETURN(drv_gg_io_api.drv_tcam_tbl_remove(chip_id_offset, tbl_id, index));
    }
    return DRV_E_NONE;
}

int32
drv_goldengate_hash_key_ioctl(void* p_para, hash_op_type_t op_type)
{

    if (g_drv_gg_io_master.chip_agent_cb&&(TRUE == gg_client_init_done))
    {
        chip_io_para_t para;
        para.op = CHIP_IO_OP_HASH_KEY_IOCTL;
        para.u.hash_key_ioctl.op_type = op_type;
        para.u.hash_key_ioctl.p_para = p_para;
        return g_drv_gg_io_master.chip_agent_cb(0, &para);
    }

    if (g_drv_gg_io_master.debug_cb)
    {
        chip_io_para_t para;
        para.op = CHIP_IO_OP_HASH_KEY_IOCTL;
        para.u.hash_key_ioctl.op_type = op_type;
        para.u.hash_key_ioctl.p_para = p_para;
        g_drv_gg_io_master.debug_cb(0, &para);
    }

    if (HASH_OP_TP_ADD_BY_KEY == op_type)
    {
        DRV_PTR_VALID_CHECK(drv_gg_io_api.drv_hash_key_add_by_key);
        DRV_IF_ERROR_RETURN(drv_gg_io_api.drv_hash_key_add_by_key(p_para));
    }
    else if (HASH_OP_TP_DEL_BY_KEY == op_type)
    {
        DRV_PTR_VALID_CHECK(drv_gg_io_api.drv_hash_key_del_by_key);
        DRV_IF_ERROR_RETURN(drv_gg_io_api.drv_hash_key_del_by_key(p_para));
    }
    else if (HASH_OP_TP_ADD_BY_INDEX == op_type)
    {
        DRV_PTR_VALID_CHECK(drv_gg_io_api.drv_hash_key_add_by_index);
        DRV_IF_ERROR_RETURN(drv_gg_io_api.drv_hash_key_add_by_index(p_para));
    }
    else if (HASH_OP_TP_DEL_BY_INDEX == op_type)
    {
        DRV_PTR_VALID_CHECK(drv_gg_io_api.drv_hash_key_del_by_index);
        DRV_IF_ERROR_RETURN(drv_gg_io_api.drv_hash_key_del_by_index(p_para));
    }

    return DRV_E_NONE;
}

/**
 @brief hash lookup
*/
extern int32
drv_goldengate_hash_lookup(hash_lookup_info_t* p_lookup_info, hash_lookup_result_t* p_lookup_result)
{
     /* DRV_IO_ADD_STATS(CHIP_IO_OP_HASH_LOOKUP);*/

    DRV_PTR_VALID_CHECK(p_lookup_info);
    DRV_PTR_VALID_CHECK(p_lookup_result);
    if (g_drv_gg_io_master.chip_agent_cb&&(TRUE == gg_client_init_done))
    {
        chip_io_para_t para;
        para.op = CHIP_IO_OP_HASH_LOOKUP;
        para.u.hash_lookup.info = p_lookup_info;
        para.u.hash_lookup.result = p_lookup_result;
        return g_drv_gg_io_master.chip_agent_cb(0, &para);
    }

    DRV_PTR_VALID_CHECK(drv_gg_io_api.drv_hash_lookup);
    DRV_IF_ERROR_RETURN(drv_gg_io_api.drv_hash_lookup(p_lookup_info, p_lookup_result));

    if (g_drv_gg_io_master.debug_cb)
    {
        chip_io_para_t para;
        para.op = CHIP_IO_OP_HASH_LOOKUP;
        para.u.hash_lookup.info = p_lookup_info;
        para.u.hash_lookup.result = p_lookup_result;
        g_drv_gg_io_master.debug_cb(0, &para);
    }

    return DRV_E_NONE;
}

/**
 @brief get acl lookup bitmap
*/
int32
drv_goldengate_get_acl_lookup_bitmap(uint8 chip_id, uint8 lookup_index, uint32* lookup_bitmap)
{
    if(lookup_index>=DRV_ACL_LOOKUP_NUM || lookup_bitmap == NULL)
    {
        return  DRV_E_INVALID_PARAMETER;
    }

    *lookup_bitmap = _drv_gg_acl_key_lookup_bitmap[lookup_index];
    return DRV_E_NONE;
}

/**
 @brief set acl lookup bitmap
*/
int32
drv_goldengate_set_acl_lookup_bitmap(uint8 chip_id, uint8 lookup_index, uint32 lookup_bitmap)
{
    if(lookup_index>=DRV_ACL_LOOKUP_NUM)
    {
        return  DRV_E_INVALID_PARAMETER;
    }

    _drv_gg_acl_key_lookup_bitmap[lookup_index] = lookup_bitmap;


    /* TODO: set bitmap to acl lookup control registers */

    return DRV_E_NONE;
}

/**
 @brief get tcam lookup bitmap
*/
int32 drv_goldengate_get_tcam_lookup_bitmap(uint32 mem_id, uint32 *bitmap)
{
    *bitmap = _drv_gg_tcam_key_lookup_bitmap[mem_id];

    return DRV_E_NONE;
}

/**
 @brief set tcam lookup bitmap
*/
int32 drv_goldengate_set_tcam_lookup_bitmap(uint32 mem_id, uint32 bitmap)
{
    _drv_gg_tcam_key_lookup_bitmap[mem_id] = bitmap;

    return DRV_E_NONE;
}

/**
 @brief get hash lookup bitmap
*/
int32 drv_goldengate_get_hash_lookup_bitmap(uint32 hash_module, uint32 *bitmap, uint32 *extra_mode)
{
    *bitmap = _drv_gg_hash_key_lookup_bitmap[hash_module].bitmap;
    *extra_mode = _drv_gg_hash_key_lookup_bitmap[hash_module].extra_mode;

    return DRV_E_NONE;
}

/**
 @brief set tcam lookup bitmap
*/
int32 drv_goldengate_set_hash_lookup_bitmap(uint32 hash_module, uint32 bitmap, uint32 extra_mode)
{
    _drv_gg_hash_key_lookup_bitmap[hash_module].bitmap= bitmap;
    _drv_gg_hash_key_lookup_bitmap[hash_module].extra_mode= extra_mode;

    return DRV_E_NONE;
}

/**
 @brief get dynamic ram couple mode
*/
int32 drv_goldengate_get_dynamic_ram_couple_mode(uint32 *couple_mode)
{
    *couple_mode = _drv_gg_dynamic_ram_couple_mode;
    return DRV_E_NONE;
}

/**
 @brief set dynamic ram couple mode
*/
int32 drv_goldengate_set_dynamic_ram_couple_mode(uint32 couple_mode)
{
    _drv_gg_dynamic_ram_couple_mode = couple_mode;

    return DRV_E_NONE;
}



/* define all  */
int32
drv_goldengate_register_chip_agent_cb(DRV_IO_AGENT_CALLBACK cb)
{
    g_drv_gg_io_master.chip_agent_cb = cb;
    return DRV_E_NONE;
}

int32
drv_goldengate_register_io_debug_cb(DRV_IO_AGENT_CALLBACK cb)
{
    g_drv_gg_io_master.debug_cb = cb;
    return DRV_E_NONE;
}

int32
drv_goldengate_set_warmboot_status(uint8 chip_id, uint32 wb_status)
{
    g_drv_gg_io_master.wb_status[chip_id] = wb_status;
    return DRV_E_NONE;
}

int32
drv_goldengate_set_gb_gg_interconnect_en(uint8 value)
{
    g_drv_gg_io_master.gb_gg_interconnect_en = value;
    return DRV_E_NONE;
}

int32
drv_goldengate_io_init(void)
{
    sal_memset(&g_drv_gg_io_master, 0, sizeof(g_drv_gg_io_master));
#ifdef _SAL_LINUX_UM
    sal_memset(&g_drv_gg_chip_agent_master, 0, sizeof(g_drv_gg_chip_agent_master));
#endif
    return DRV_E_NONE;
}



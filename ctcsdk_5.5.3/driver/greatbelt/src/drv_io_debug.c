/**
 @file drv_io_debug.c

 @date 2012-12-11

 @version v5.1

 The file contains all driver I/O debug realization
*/

#include "greatbelt/include/drv_lib.h"

#define DRV_IO_DEBUG_ACTION_PRINT       0x01
#define DRV_IO_DEBUG_ACTION_CACHE       0x02
#define DRV_IO_LOG                      sal_printf

struct drv_io_hash_lkp_s
{
    uint8   chip_id;
    uint8   hash_type;
    uint8   hash_module;
    uint8   rev0[1];

    uint32  key_index;
    uint32  ad_index;

    uint8   valid;
    uint8   free;
    uint8   conflict;
    uint8   rev1[1];
};
typedef struct drv_io_hash_lkp_s drv_io_hash_lkp_t;

/* defined in drv_io.c */
extern drv_io_t g_drv_io_master;
extern int32
drv_greatbelt_register_io_debug_cb(DRV_IO_AGENT_CALLBACK func);

/**********************************************************************************
                      Function interfaces realization
***********************************************************************************/
STATIC char*
_drv_greatbelt_io_cmd_hash_module_str(hash_module_t module)
{
    switch (module)
    {
    case HASH_MODULE_USERID:
        return "user-id";

    case HASH_MODULE_FIB:
        return "fib";

    case HASH_MODULE_LPM:
        return "lpm";

    case HASH_MODULE_QUEUE:
        return "queue";

    default:
        return "invalid";
    }
}

STATIC char*
_drv_greatbelt_io_cmd_hash_op_str(hash_op_type_t op)
{
    switch (op)
    {
    case HASH_OP_TP_ADD_BY_KEY:
        return "add-key";

    case HASH_OP_TP_DEL_BY_KEY:
        return "del-key";

    case HASH_OP_TP_ADD_BY_INDEX:
        return "add-idx";

    case HASH_OP_TP_DEL_BY_INDEX:
        return "del-idx";

    default:
        return "invalid";
    }
}

STATIC char*
_drv_greatbelt_io_cmd_op_str(chip_io_op_t op)
{
    switch (op)
    {
    case CHIP_IO_OP_IOCTL:
        return "ioctl";

    case CHIP_IO_OP_TCAM_REMOVE:
        return "tcam-del";

    case CHIP_IO_OP_HASH_KEY_IOCTL:
        return "hash-key";

    case CHIP_IO_OP_HASH_LOOKUP:
        return "hash-lkp";

    default:
        return "invalid";
    }
}

STATIC char*
_drv_greatbelt_io_cmd_action_str(uint32 action)
{
    switch (action)
    {
    case DRV_IOC_READ:
        return "R";

    case DRV_IOC_WRITE:
        return "W";

    default:
        return "invalid";
    }
}

STATIC int32
_drv_greatbelt_io_debug_add_cache_entry(drv_io_debug_cache_info_t* p_cache, sal_systime_t* p_tv, chip_io_para_t* p_io)
{
    drv_io_debug_cache_entry_t* p_entry = NULL;
    drv_io_hash_lkp_t* p_hash_lkp = NULL;
    lookup_info_t* p_lkp = NULL;
    lookup_result_t* p_rst = NULL;

    if (0 == p_cache->count)
    {
        p_cache->index = 0;
    }

    p_cache->count++;
    p_entry = &(p_cache->entry[p_cache->index]);
    sal_memcpy(&p_entry->tv, p_tv, sizeof(sal_systime_t));
    sal_memcpy(&p_entry->io, p_io, sizeof(chip_io_para_t));

    switch (p_io->op)
    {
    case CHIP_IO_OP_HASH_KEY_IOCTL:
        if (HASH_OP_TP_ADD_BY_KEY == p_io->u.hash_key_ioctl.op_type
            || HASH_OP_TP_DEL_BY_KEY == p_io->u.hash_key_ioctl.op_type)
        {
            sal_memcpy(&p_entry->data, p_io->u.hash_key_ioctl.p_para, sizeof(hash_io_by_key_para_t));
        }
        else
        {
            sal_memcpy(&p_entry->data, p_io->u.hash_key_ioctl.p_para, sizeof(hash_io_by_idx_para_t));
        }

        break;

    case CHIP_IO_OP_HASH_LOOKUP:
        p_hash_lkp = (drv_io_hash_lkp_t*)p_entry->data;
        p_lkp = p_io->u.hash_lookup.info;
        p_rst = p_io->u.hash_lookup.result;

        p_hash_lkp->chip_id = p_lkp->chip_id;
        p_hash_lkp->hash_type = p_lkp->hash_type;
        p_hash_lkp->hash_module = p_lkp->hash_module;
        p_hash_lkp->key_index = p_rst->key_index;
        p_hash_lkp->ad_index = p_rst->ad_index;
        p_hash_lkp->valid = p_rst->valid;
        p_hash_lkp->free = p_rst->free;
        p_hash_lkp->conflict = p_rst->conflict;
        break;

    default:
        break;
    }

    p_cache->index++;
    if (p_cache->index >= DRV_IO_DEBUG_CACHE_SIZE)
    {
        p_cache->index = 0;
    }

    return DRV_E_NONE;
}

STATIC int32
_drv_greatbelt_io_debug_show_io(uint32 index, sal_systime_t* p_tv, chip_io_para_t* p_io, void* p_data)
{
    uint8 chip_id = 0;
    uint32 cmd = 0;
    uint32 tbl_id = 0;
    uint32 tbl_index = 0;
    uint32 fld_id = 0;
    char* tbl_name = NULL;
    char* fld_name = NULL;
    fields_t* p_fld_info = NULL;
    char all_fld_str[] = "all";
    hash_io_by_key_para_t* p_hash_key = NULL;
    hash_io_by_idx_para_t* p_hash_idx = NULL;
    void* p_hash_io_data = NULL;
    drv_io_hash_lkp_t* p_hash_lkp = NULL;
    lookup_info_t* p_lkp = NULL;
    lookup_result_t* p_rst = NULL;
    lookup_info_t info;
    lookup_result_t result;

    switch (p_io->op)
    {
    case CHIP_IO_OP_IOCTL:
        chip_id = p_io->u.ioctl.chip_id;
        cmd = p_io->u.ioctl.cmd;
        tbl_index = p_io->u.ioctl.index;
        tbl_id = DRV_IOC_MEMID(cmd);
        if (tbl_id >= MaxTblId_t)
        {
            return DRV_E_INVALID_TBL;
        }

        tbl_name = (tbl_id < MaxTblId_t) ? TABLE_NAME(tbl_id) : "Invalid";
        fld_id = DRV_IOC_FIELDID(cmd);
        if (DRV_ENTRY_FLAG == fld_id)
        {
            fld_name = all_fld_str;
        }
        else
        {
            p_fld_info = TABLE_FIELD_INFO_PTR(tbl_id);
            fld_name = (fld_id < TABLE_FIELD_NUM(tbl_id)) ? p_fld_info[fld_id].ptr_field_name : "Invalid";
        }

        DRV_IO_LOG("%-7s %-17s %-8s %-4s %-6s %-10s %-20s\n", "Index", "TimeStamp", "OP", "Chip", "Index", "CMD", "(Action,Table,Field)");
        DRV_IO_LOG("--------------------------------------------------------------------------------\n");
        DRV_IO_LOG("%-7d %-10d.%06d %-8s %-4d %-6d 0x%08X (%s,%s/%d,%s/%d)\n",
                   index, p_tv->tv_sec, p_tv->tv_usec, _drv_greatbelt_io_cmd_op_str(p_io->op),
                   chip_id, tbl_index, cmd,
                   _drv_greatbelt_io_cmd_action_str(DRV_IOC_OP(cmd)), tbl_name, tbl_id, fld_name, fld_id);
        break;

    case CHIP_IO_OP_TCAM_REMOVE:
        chip_id = p_io->u.tcam_remove.chip_id;
        tbl_index = p_io->u.tcam_remove.index;
        tbl_id = p_io->u.tcam_remove.tbl_id;
        tbl_name = (tbl_id < MaxTblId_t) ? TABLE_NAME(tbl_id) : "Invalid";
        DRV_IO_LOG("%-7s %-17s %-8s %-4s %-6s %-20s\n", "Index", "TimeStamp", "OP", "Chip", "Index", "Table");
        DRV_IO_LOG("--------------------------------------------------------------------------------\n");
        DRV_IO_LOG("%-7d %-10d.%06d %-8s %-4d %-6d %s/%d\n",
                   index, p_tv->tv_sec, p_tv->tv_usec, _drv_greatbelt_io_cmd_op_str(p_io->op),
                   chip_id, tbl_index, tbl_name, tbl_id);
        break;

    case CHIP_IO_OP_HASH_KEY_IOCTL:
        if (p_data)
        {
            p_hash_io_data = p_data;
        }
        else
        {
            p_hash_io_data = p_io->u.hash_key_ioctl.p_para;
        }

        if (HASH_OP_TP_ADD_BY_KEY == p_io->u.hash_key_ioctl.op_type
            || HASH_OP_TP_DEL_BY_KEY == p_io->u.hash_key_ioctl.op_type)
        {
            p_hash_key = p_hash_io_data;
            chip_id = p_hash_key->chip_id;
            DRV_IO_LOG("%-7s %-17s %-8s %-7s %-4s %-8s %-6s\n", "Index", "TimeStamp", "OP", "Hash-OP", "Chip", "Module", "Type");
            DRV_IO_LOG("--------------------------------------------------------------------------------\n");
            DRV_IO_LOG("%-7d %-10d.%06d %-8s %-7s %-4d %-8s %-6d\n",
                       index, p_tv->tv_sec, p_tv->tv_usec, _drv_greatbelt_io_cmd_op_str(p_io->op),
                       _drv_greatbelt_io_cmd_hash_op_str(p_io->u.hash_key_ioctl.op_type),
                       chip_id, _drv_greatbelt_io_cmd_hash_module_str(p_hash_key->hash_module), p_hash_key->hash_type);
        }
        else
        {
            p_hash_idx = p_hash_io_data;
            chip_id = p_hash_idx->chip_id;
            tbl_index = p_hash_idx->table_index;
            tbl_id = p_hash_idx->table_id;
            tbl_name = (tbl_id < MaxTblId_t) ? TABLE_NAME(tbl_id) : "Invalid";
            DRV_IO_LOG("%-7s %-17s %-8s %-7s %-4s %-6s %-20s\n", "Index", "TimeStamp", "OP", "Hash-OP", "Chip", "Index", "Table");
            DRV_IO_LOG("--------------------------------------------------------------------------------\n");
            DRV_IO_LOG("%-7d %-10d.%06d %-8s %-7s %-4d %-6d %s/%d\n",
                       index, p_tv->tv_sec, p_tv->tv_usec, _drv_greatbelt_io_cmd_op_str(p_io->op),
                       _drv_greatbelt_io_cmd_hash_op_str(p_io->u.hash_key_ioctl.op_type),
                       chip_id, tbl_index, tbl_name, tbl_id);
        }

        break;

    case CHIP_IO_OP_HASH_LOOKUP:
        if (p_data)
        {
            sal_memset(&info, 0, sizeof(info));
            sal_memset(&result, 0, sizeof(result));
            p_lkp = &info;
            p_rst = &result;
            p_hash_lkp = (drv_io_hash_lkp_t*)p_data;

            p_lkp->chip_id = p_hash_lkp->chip_id;
            p_lkp->hash_type = p_hash_lkp->hash_type;
            p_lkp->hash_module = p_hash_lkp->hash_module;
            p_rst->key_index = p_hash_lkp->key_index;
            p_rst->ad_index = p_hash_lkp->ad_index;
            p_rst->valid = p_hash_lkp->valid;
            p_rst->free = p_hash_lkp->free;
            p_rst->conflict = p_hash_lkp->conflict;
        }
        else
        {
            p_lkp = p_io->u.hash_lookup.info;
            p_rst = p_io->u.hash_lookup.result;
        }

        chip_id = p_lkp->chip_id;
        DRV_IO_LOG("%-7s %-17s %-8s %-4s %-8s %-6s %-6s %-6s %-3s %-4s %-4s\n",
                   "Index", "TimeStamp", "OP", "Chip", "Module", "Type", "KeyIdx", "AdIdx", "Vld", "Free", "Cflt");
        DRV_IO_LOG("--------------------------------------------------------------------------------\n");
        DRV_IO_LOG("%-7d %-10d.%06d %-8s %-4d %-8s %-6d %-6d %-6d %-3d %-4d %-4d\n",
                   index, p_tv->tv_sec, p_tv->tv_usec, _drv_greatbelt_io_cmd_op_str(p_io->op),
                   chip_id, _drv_greatbelt_io_cmd_hash_module_str(p_lkp->hash_module), p_lkp->hash_type,
                   p_rst->key_index, p_rst->ad_index, p_rst->valid, p_rst->free, p_rst->conflict);
        break;

    default:
        break;
    }

    DRV_IO_LOG("\n");

    return DRV_E_NONE;
}

STATIC int32
_drv_greatbelt_io_debug_show_config(void)
{
    uint32 action = g_drv_io_master.debug_action;

    if (action == (DRV_IO_DEBUG_ACTION_PRINT | DRV_IO_DEBUG_ACTION_CACHE))
    {
        DRV_IO_LOG("Action          : print and cache\n");
    }
    else if (action == DRV_IO_DEBUG_ACTION_PRINT)
    {
        DRV_IO_LOG("Action          : print\n");
    }
    else if (action == DRV_IO_DEBUG_ACTION_CACHE)
    {
        DRV_IO_LOG("Action          : cache\n");
    }
    else
    {
        DRV_IO_LOG("Action          : disable\n");
    }

    return DRV_E_NONE;
}

STATIC int32
_drv_greatbelt_io_debug_show_stats(void)
{
    uint32 op = 0;

    DRV_IO_LOG("########## Driver IO Statistics ##########\n");
    DRV_IO_LOG("%-10s %-10s\n", "OP", "Count");
    DRV_IO_LOG("---------------------\n");

    for (op = 0; op <= CHIP_IO_OP_HASH_LOOKUP; op++)
    {
        DRV_IO_LOG("%-10s %-10d\n", _drv_greatbelt_io_cmd_op_str(op), g_drv_io_master.debug_stats[op]);
    }

    DRV_IO_LOG("\n");
    return DRV_E_NONE;
}

STATIC int32
_drv_greatbelt_io_debug_show_cache(uint32 last_count)
{
    drv_io_debug_cache_info_t* p_cache = &g_drv_io_master.cache;
    drv_io_debug_cache_entry_t* p_entry = NULL;
    uint32 start = 0;
    uint32 count = 0;
    uint32 index = 0;
    uint32 i = 0;

    DRV_IO_LOG("########## Driver IO Cache ##########\n");
    if (0 == p_cache->count)
    {
        DRV_IO_LOG("No entry\n");
        return DRV_E_NONE;
    }

    if (p_cache->count >= DRV_IO_DEBUG_CACHE_SIZE)
    {
        start = p_cache->index;
        count = DRV_IO_DEBUG_CACHE_SIZE;
        if (last_count < count)
        {
            start += (count - last_count);
            if (start >= DRV_IO_DEBUG_CACHE_SIZE)
            {
                start -= DRV_IO_DEBUG_CACHE_SIZE;
            }

            count = last_count;
        }
    }
    else
    {
        start = 0;
        count = p_cache->index;
        if (last_count < count)
        {
            start += (count - last_count);
            if (start >= DRV_IO_DEBUG_CACHE_SIZE)
            {
                start -= DRV_IO_DEBUG_CACHE_SIZE;
            }

            count = last_count;
        }
    }

    DRV_IO_LOG("Total Count     : %d\n", p_cache->count);
    DRV_IO_LOG("Cache Count     : %d\n",
               (p_cache->count >= DRV_IO_DEBUG_CACHE_SIZE) ? DRV_IO_DEBUG_CACHE_SIZE : p_cache->count);
    DRV_IO_LOG("Show Last Count : %d\n", last_count);

    for (i = 0, index = start; i < count; i++, index++)
    {
        if (index >= DRV_IO_DEBUG_CACHE_SIZE)
        {
            index -= DRV_IO_DEBUG_CACHE_SIZE;
        }

        p_entry = &(p_cache->entry[index]);
        _drv_greatbelt_io_debug_show_io(index, &p_entry->tv, &p_entry->io, p_entry->data);
    }

    DRV_IO_LOG("\n");

    return DRV_E_NONE;
}

STATIC int32
_drv_greatbelt_io_debug_callback(void* p)
{
    sal_systime_t tv;
    chip_io_para_t* p_io = (chip_io_para_t*)p;
    drv_io_debug_cache_info_t* p_cache = &g_drv_io_master.cache;

    if (!g_drv_io_master.debug_action)
    {
        return DRV_E_NONE;
    }

    sal_gettime(&tv);

    if (g_drv_io_master.debug_action & DRV_IO_DEBUG_ACTION_PRINT)
    {
        _drv_greatbelt_io_debug_show_io(g_drv_io_master.debug_stats[p_io->op], &tv, p_io, NULL);
    }

    if (g_drv_io_master.debug_action & DRV_IO_DEBUG_ACTION_CACHE)
    {
        _drv_greatbelt_io_debug_add_cache_entry(p_cache, &tv, p_io);
    }

    return DRV_E_NONE;
}

int32
drv_greatbelt_io_debug_set_action(uint32 action)
{
    if (action)
    {
        drv_greatbelt_register_io_debug_cb(_drv_greatbelt_io_debug_callback);
    }
    else
    {
        drv_greatbelt_register_io_debug_cb(NULL);
    }

    g_drv_io_master.debug_action = action;

    return DRV_E_NONE;
}

int32
drv_greatbelt_io_debug_clear_cache(void)
{
    sal_memset(&g_drv_io_master.cache, 0, sizeof(g_drv_io_master.cache));
    return DRV_E_NONE;
}

int32
drv_greatbelt_io_debug_clear_stats(void)
{
    uint32 op = 0;

    for (op = 0; op <= CHIP_IO_OP_HASH_LOOKUP; op++)
    {
        g_drv_io_master.debug_stats[op] = 0;
    }

    return DRV_E_NONE;
}

int32
drv_greatbelt_io_debug_show(uint32 last_count)
{
    _drv_greatbelt_io_debug_show_config();
    _drv_greatbelt_io_debug_show_stats();

    if (last_count)
    {
        _drv_greatbelt_io_debug_show_cache(last_count);
    }

    return DRV_E_NONE;
}


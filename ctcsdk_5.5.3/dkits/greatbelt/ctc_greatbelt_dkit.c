#include "greatbelt/include/drv_lib.h"
#include "greatbelt/include/drv_chip_ctrl.h"
#include "ctc_cli.h"
#include "ctc_dkit.h"
#include "ctc_dkit_common.h"

#include "ctc_greatbelt_dkit.h"
#include "ctc_greatbelt_dkit_memory.h"
#include "ctc_greatbelt_dkit_drv.h"
#include "ctc_greatbelt_dkit_misc.h"
#include "ctc_greatbelt_dkit_internal.h"
#include "ctc_greatbelt_dkit_monitor.h"
#include "ctc_greatbelt_dkit_discard.h"
#include "ctc_greatbelt_dkit_dump_cfg.h"
#include "ctc_greatbelt_dkit_tcam.h"

ctc_dkit_master_t* g_gb_dkit_master[CTC_DKITS_MAX_LOCAL_CHIP_NUM] = {NULL};
extern ctc_dkit_api_t* g_dkit_api[CTC_DKITS_MAX_LOCAL_CHIP_NUM];
extern ctc_dkit_chip_api_t* g_dkit_chip_api[CTC_DKITS_MAX_LOCAL_CHIP_NUM];
extern int32
drv_greatbelt_get_host_type (uint8 lchip);
int32 ctc_greatbelt_dkit_chip_init(uint8 lchip)
{
    if (g_dkit_chip_api[lchip] == NULL)
    {
        return CLI_ERROR;
    }

    g_dkit_chip_api[lchip]->dkits_dump_decode_entry = ctc_greatbelt_dkits_dump_decode_entry;
    g_dkit_chip_api[lchip]->drv_chip_read = drv_greatbelt_chip_read;
    g_dkit_chip_api[lchip]->drv_chip_write = drv_greatbelt_chip_write;
    g_dkit_chip_api[lchip]->drv_get_tbl_string_by_id = drv_greatbelt_dkit_get_tbl_string_by_id;
    g_dkit_chip_api[lchip]->drv_get_host_type = drv_greatbelt_get_host_type;
    g_dkit_chip_api[lchip]->dkits_dump_data2tbl = ctc_greatbelt_dkits_dump_data2tbl;

    return 0;
}

int32 ctc_greatbelt_dkit_init(uint8 lchip)
{
    if(lchip >= CTC_DKITS_MAX_LOCAL_CHIP_NUM)
    {
        return CLI_SUCCESS;
    }

    g_dkit_api[lchip] = (ctc_dkit_api_t*)mem_malloc(MEM_CLI_MODULE, sizeof(ctc_dkit_api_t));
    if (NULL == g_dkit_api[lchip])
    {
        CTC_DKIT_PRINT("No memory for dkits!!!\n");
        goto error;
    }

    g_dkit_chip_api[lchip] = (ctc_dkit_chip_api_t*)mem_malloc(MEM_CLI_MODULE, sizeof(ctc_dkit_chip_api_t));
    if (NULL == g_dkit_chip_api[lchip])
    {
        CTC_DKIT_PRINT("No memory for dkits!!!\n");
        goto error;
    }

    sal_memset(g_dkit_api[lchip], 0 , sizeof(ctc_dkit_api_t));
    sal_memset(g_dkit_chip_api[lchip], 0 , sizeof(ctc_dkit_chip_api_t));

    g_gb_dkit_master[lchip] = (ctc_dkit_master_t*)mem_malloc(MEM_CLI_MODULE, sizeof(ctc_dkit_master_t));
    if (NULL == g_gb_dkit_master[lchip])
    {
        CTC_DKIT_PRINT("No memory for dkits!!!\n");
        goto error;
    }
    sal_memset(g_gb_dkit_master[lchip], 0 , sizeof(ctc_dkit_master_t));

    g_gb_dkit_master[lchip]->discard_enable_bitmap = mem_malloc(MEM_CLI_MODULE, ((CTC_DKIT_DISCARD_MAX / 32) + 1)*4);
    if (NULL == g_gb_dkit_master[lchip]->discard_enable_bitmap)
    {
        CTC_DKIT_PRINT("No memory for dkits!!!\n");
        goto error;
    }
    sal_memset(g_gb_dkit_master[lchip]->discard_enable_bitmap, 0xFF , ((CTC_DKIT_DISCARD_MAX / 32) + 1)*4);


    /*register API*/
    /*1. Basic*/
    g_dkit_api[lchip]->memory_process = ctc_greatbelt_dkit_memory_process;

    /*2. Normal*/
    g_dkit_api[lchip]->show_path = ctc_greatbelt_dkit_path_process;
    g_dkit_api[lchip]->show_discard = ctc_greatbelt_dkit_discard_process;
    g_dkit_api[lchip]->show_discard_type = ctc_greatbelt_dkit_discard_show_type;
    g_dkit_api[lchip]->discard_display_en = ctc_greatbelt_dkit_discard_display_en;

    /*3. Advanced*/
    g_dkit_api[lchip]->show_sensor_result = ctc_greatbelt_dkit_monitor_show_sensor_result;
    g_dkit_api[lchip]->show_queue_id = ctc_greatbelt_dkit_monitor_show_queue_id;
    g_dkit_api[lchip]->show_queue_depth = ctc_greatbelt_dkit_monitor_show_queue_depth;
    g_dkit_api[lchip]->serdes_ctl = ctc_greatbelt_dkit_misc_serdes_ctl;
    g_dkit_api[lchip]->integrity_check = ctc_greatbelt_dkit_misc_integrity_check;
    g_dkit_api[lchip]->packet_dump = ctc_greatbelt_dkit_misc_packet_dump;

    g_dkit_api[lchip]->tcam_scan = ctc_greatbelt_dkits_tcam_scan;
    g_dkit_api[lchip]->tcam_capture_start = ctc_greatbelt_dkits_tcam_capture_start;
    g_dkit_api[lchip]->tcam_capture_stop = ctc_greatbelt_dkits_tcam_capture_stop;
    g_dkit_api[lchip]->tcam_capture_show = ctc_greatbelt_dkits_tcam_capture_show;
    g_dkit_api[lchip]->cfg_usage = ctc_greatbelt_dkits_dump_memory_usage;
    g_dkit_api[lchip]->cfg_dump = ctc_greatbelt_dkits_dump_cfg_dump;
    g_dkit_api[lchip]->cfg_decode = ctc_greatbelt_dkits_dump_cfg_decode;
    g_dkit_api[lchip]->cfg_cmp = ctc_greatbelt_dkits_dump_cfg_cmp;

    /*4. dkit api for user*/
    g_dkit_api[lchip]->read_table = ctc_greatbelt_dkit_memory_read_table;
    g_dkit_api[lchip]->write_table = ctc_greatbelt_dkit_memory_write_table;

    ctc_greatbelt_dkit_chip_init(lchip);

    ctc_greatbelt_dkit_drv_register();
    return CLI_SUCCESS;
error:
    CTC_DKIT_PRINT("Dkits init fail!!!\n");
    if (g_dkit_api[lchip])
    {
        mem_free(g_dkit_api[lchip]);
        g_dkit_api[lchip] = NULL;
    }

    if (g_dkit_chip_api[lchip])
    {
        mem_free(g_dkit_chip_api[lchip]);
        g_dkit_chip_api[lchip] = NULL;
    }

    if (g_gb_dkit_master[lchip])
    {
        mem_free(g_gb_dkit_master[lchip]);
        g_gb_dkit_master[lchip] = NULL;
    }

    if (g_gb_dkit_master[lchip]->discard_enable_bitmap)
    {
        mem_free(g_gb_dkit_master[lchip]->discard_enable_bitmap);
        g_gb_dkit_master[lchip]->discard_enable_bitmap = NULL;
    }

    return CLI_SUCCESS;
}


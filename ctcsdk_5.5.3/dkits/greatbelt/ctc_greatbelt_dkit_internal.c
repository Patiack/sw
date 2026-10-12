#include "sal.h"
#include "dal.h"
#include "ctc_cli.h"
#include "greatbelt/include/drv_lib.h"
#include "greatbelt/include/drv_data_path.h"
#include "ctc_dkit.h"
#include "ctc_dkit_common.h"
#include "greatbelt/include/drv_chip_ctrl.h"
#include "ctc_greatbelt_dkit.h"
#include "ctc_greatbelt_dkit_drv.h"
#include "ctc_greatbelt_dkit_internal.h"
#include "ctc_greatbelt_dkit_misc.h"
#include "ctc_greatbelt_dkit_discard.h"
#include "ctc_greatbelt_dkit_monitor.h"

enum dkits_pcie_ctl_e
{
    DKIT_PCIE_CFG_R,
    DKIT_PCIE_CFG_W,
    DKIT_PCIE_IO_R,
    DKIT_PCIE_IO_W,
    DKIT_PCIE_MEM_R,
    DKIT_PCIE_MEM_W,
    DKIT_PCIE_MAX
};
typedef enum dkits_pcie_ctl_e dkits_pcie_ctl_t;
extern dal_op_t g_dal_op;

struct bufstore_bus_msg_hdr_info_s
{
    uint32 lo_destmap;
    uint32 hi_destmap;
    uint32 lo_nexthop_ext;
    uint32 hi_nexthop_ext;
};
typedef struct bufstore_bus_msg_hdr_info_s bufstore_bus_msg_hdr_info_t;

extern uint8 g_api_lchip;
extern uint8 g_ctcs_api_en;

extern  int32
_ctc_greatbelt_dkit_memory_show_ram_by_data_tbl_id( uint32 tbl_id, uint32 index, uint32* data_entry, char* regex);

STATIC int32
_ctc_greatbelt_dkit_internal_bus_process(ctc_cmd_element_t* self, ctc_vti_t* vty, int argc, char** argv)
{
    uint32 lchip = 0;
    char *bus_name = NULL;
    uint32 data_entry[MAX_ENTRY_WORD] = {0};

    tbls_id_t tbl_id = 0;

    uint32 i = 0;
    uint32 max_idx = 0;
    uint32 index = 0xFFFFFFFF;
    uint32 cmd = 0;

    if (argc >=1 )
    {
        bus_name = argv[0];
    }
    else
    {
        CTC_DKIT_PRINT("input bus name");
        return DRV_E_NONE;
    }

    if (!ctc_greatbelt_dkit_drv_get_tbl_id_by_bus_name(bus_name, (uint32 *)&tbl_id))
    {
        return DRV_E_NONE;
    }

    if (argc == 2)
    {
        CTC_CLI_GET_INTEGER("index", index, argv[1]);
    }
    else
    {
        index = 0xFFFFFFFF;
    }


    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_INTEGER("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;

    if (index != 0xFFFFFFFF)
    {
        cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, index, cmd, &data_entry);
         _ctc_greatbelt_dkit_memory_show_ram_by_data_tbl_id(tbl_id, index, data_entry, NULL);
    }
    else
    {
        max_idx =   TABLE_MAX_INDEX(tbl_id);
        for (i = 0; i < max_idx; i++)
        {
            cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, i, cmd, &data_entry);
             _ctc_greatbelt_dkit_memory_show_ram_by_data_tbl_id(tbl_id, i, data_entry, NULL);
        }
    }
    return DRV_E_NONE;
}


STATIC int32
_ctc_greatbelt_dkit_internal_bus_init(int (* func) (ctc_cmd_element_t*, ctc_vti_t*, int, char**), uint8 cli_tree_mode, uint8 install)
{
#define MAX_BUS_CLI_LEN         (8024*10)
#define MAX_BUS_CLI_TIPS_LEN    256
    ctc_cmd_element_t *cli_cmd = NULL;
    int32 i = 0, j = 0;
    char *cmd_str = NULL;
    char **cli_help = NULL;
    int cnt = 0, tip_node_num = 0;
    char bus_name[MAX_BUS_CLI_TIPS_LEN] = {0};
    char *name = NULL;
    uint32 bus_cnt = 0;

    bus_cnt = ctc_greatbelt_dkit_drv_get_bus_list_num();
    for (i = 0; i < bus_cnt; i++)
    {
        sal_memset(bus_name, 0, MAX_BUS_CLI_TIPS_LEN);
        name = DKIT_BUS_GET_NAME(i);
        if (('D' == name[0]) && ('b' == name[1]) && ('g' == name[2]))
        {
            break;
        }
    }
    bus_cnt = i;/*get real bus number*/

    cmd_str = sal_malloc(MAX_BUS_CLI_LEN);
    if (NULL == cmd_str)
    {
        CTC_DKIT_PRINT("Dkits bus init no memory\n");
        return DRV_E_NO_MEMORY;
    }
    sal_memset(cmd_str, 0, MAX_BUS_CLI_LEN);

    sal_sprintf(cmd_str,"%s", "show bus (");

    tip_node_num = 4 + bus_cnt;

    cli_help = sal_malloc(sizeof(char*)*tip_node_num);
    for (cnt= 0; cnt < tip_node_num; cnt++)
    {
        cli_help[cnt] = sal_malloc(MAX_BUS_CLI_TIPS_LEN);
        sal_memset(cli_help[cnt], 0, MAX_BUS_CLI_TIPS_LEN);
    }
    cli_help[tip_node_num -1] = NULL;

    sal_sprintf(cli_help[0], CTC_CLI_SHOW_STR);
    sal_sprintf(cli_help[1], "show bus");

    sal_sprintf(cli_help[tip_node_num-2], "bus index");

    for (i = 0; i < bus_cnt; i++)
    {
        sal_memset(bus_name, 0, MAX_BUS_CLI_TIPS_LEN);
        name = DKIT_BUS_GET_NAME(i);
        for (j = 0; j < sal_strlen(name); j++)
        {
            bus_name[j] = sal_tolower(name[j]);
        }
        if ((i+1) == bus_cnt)
        {
            sal_sprintf(cmd_str + sal_strlen(cmd_str), " %s ) (INDEX|) (lchip CHIP_ID|)", bus_name);
        }
        else
        {
            sal_sprintf(cmd_str + sal_strlen(cmd_str), " %s |", bus_name);
        }

        sal_sprintf(cli_help[i+2], "show %s bus info", bus_name);
    }


    cli_cmd = sal_malloc(sizeof(ctc_cmd_element_t));
    if (NULL == cli_cmd)
    {
        return CLI_ERROR;
    }
    sal_memset(cli_cmd, 0 ,sizeof(ctc_cmd_element_t));

    cli_cmd->func   = func;
    cli_cmd->string = cmd_str;
    cli_cmd->doc    = cli_help;

    if (install)
    {
        install_element(cli_tree_mode, cli_cmd);
    }
    else
    {
        uninstall_element(cli_tree_mode, cli_cmd);
    }
    return CLI_SUCCESS;
}

STATIC int32
_ctc_greatbelt_dkit_internal_pcie_read(uint8 lchip, dkits_pcie_ctl_t pcie_ctl, uint32 addr)
{
    uint32 value = 0;
    int32 ret = CLI_SUCCESS;
    switch (pcie_ctl)
    {
        case DKIT_PCIE_CFG_R:
            if (g_dal_op.pci_conf_read)
            {
                ret = g_dal_op.pci_conf_read(lchip, addr, &value);
                if (ret >= 0)
                {
                    CTC_DKIT_PRINT("Read pcie config address 0x%08x, value = 0x%08x\n", addr, value);
                }
            }
            else
            {
                CTC_DKIT_PRINT("g_dal_op.pci_conf_read is not registered!!!\n");
            }
            break;
        case DKIT_PCIE_MEM_R:
            if (g_dal_op.pci_read)
            {
                ret = g_dal_op.pci_read(lchip, addr, &value);
                if (ret >= 0)
                {
                    CTC_DKIT_PRINT("Read pcie memory address 0x%08x, value = 0x%08x\n", addr, value);
                }
            }
            else
            {
                CTC_DKIT_PRINT("g_dal_op.pci_read is not registered!!!\n");
            }
            break;
        default:
            break;
    }

    return ret;
}

STATIC int32
_ctc_greatbelt_dkit_internal_pcie_write(uint8 lchip, dkits_pcie_ctl_t pcie_ctl, uint32 addr, uint32 value)
{
    int32 ret = CLI_SUCCESS;
    switch (pcie_ctl)
    {
        case DKIT_PCIE_CFG_W:
            if (g_dal_op.pci_conf_write)
            {
                ret = g_dal_op.pci_conf_write(lchip, addr, value);
                if (ret >= 0)
                {
                    CTC_DKIT_PRINT("Write pcie config address 0x%08x, value = 0x%08x\n", addr, value);
                }
            }
            else
            {
                CTC_DKIT_PRINT("g_dal_op.pci_conf_write is not registered!!!\n");
            }
            break;
        case DKIT_PCIE_MEM_W:
            if (g_dal_op.pci_write)
            {
                ret = g_dal_op.pci_write(lchip, addr, value);
                if (ret >= 0)
                {
                    CTC_DKIT_PRINT("Write pcie memory address 0x%08x, value = 0x%08x\n", addr, value);
                }
            }
            else
            {
                CTC_DKIT_PRINT("g_dal_op.pci_write is not registered!!!\n");
            }
            break;
        default:
            break;
    }
    return ret;
}


STATIC int
_ctc_greatbelt_dkit_internal_module_read(uint32 chip_id, dkit_modules_t *ptr_module, uint32 index, uint32 sub_index, sal_file_t p_wf)
{
    tbls_id_t* reg_list_id    = NULL;
    uint32      id_num          = 0;

    tbls_id_t    reg_id_base     = MaxTblId_t;
    tbls_id_t    reg_id          = MaxTblId_t;
    fld_id_t    field_id        = 0;

    tables_info_t* id_node        = NULL;

    tbls_ext_info_t* ext_info       = NULL;
    entry_desc_t* desc = NULL;

    fields_t* first_f = NULL;
    fields_t* end_f = NULL;
    uint8 num_f = 0;

    uint32 i = 0;
    uint32 cmd = 0;
    uint32 value = 0;

    uint8 b_show = FALSE;
    uint32 entry_no = 0;
    uint32 entry_cn = 0;
    uint32 data_entry[MAX_ENTRY_WORD] = {0};

    reg_list_id = ptr_module->reg_list_id;
    id_num      = ptr_module->id_num;

    for (i = 0; i < id_num; i++)
    {
        b_show  = FALSE;
        desc    = NULL;
        reg_id_base  = reg_list_id[i];
        reg_id = reg_id_base + index;

        id_node = TABLE_INFO_PTR(reg_id);

        entry_no = id_node->max_index_num;
        ext_info = id_node->ptr_ext_info;

        while (ext_info)
        {
            switch (ext_info->ext_content_type)
            {
            case EXT_INFO_TYPE_DESC:
                desc = ext_info->ptr_ext_content;
                break;

            case EXT_INFO_TYPE_DBG:
                b_show = TRUE;
                break;

            case EXT_INFO_TYPE_TCAM:
            default:
                break;
            }

            ext_info = ext_info->ptr_next;
        }

        if (b_show)
        {
            CTC_DKITS_PRINT_FILE(p_wf, "\n*********%s*********\n", TABLE_NAME(reg_id));
            if ((NULL == p_wf) && (entry_no > 1))
            {
                CTC_DKIT_PRINT("\n##index > 1, Need dump to file!##\n");
            }
            if (!desc)
            {
                for (entry_cn = 0; entry_cn < entry_no; entry_cn++)
                {
                    if (entry_no > 1)
                    {
                        if (NULL == p_wf)
                        {
                            continue;
                        }
                        else
                        {
                            CTC_DKITS_PRINT_FILE(p_wf, "\n*********index %d*********\n", entry_cn);
                        }
                    }

                    first_f = TABLE_INFO(reg_id).ptr_fields;
                    num_f = TABLE_INFO(reg_id).num_fields;
                    end_f = &first_f[num_f];
                    cmd = DRV_IOR(reg_id, DRV_ENTRY_FLAG);
                    DRV_IOCTL(chip_id, entry_cn, cmd, data_entry);
                    field_id = 0;
                    while (first_f < end_f)
                    {
                        drv_greatbelt_get_field(reg_id, field_id, data_entry, &value);
                        CTC_DKITS_PRINT_FILE(p_wf, "%-40s  : 0x%08x\n", first_f->ptr_field_name, value);
                        first_f++;
                        field_id++;
                    }
                }
            }
            else
            {
                for (entry_cn = 0; entry_cn < entry_no; entry_cn++)
                {
                    field_id = 0;
                    first_f = TABLE_INFO(reg_id).ptr_fields;
                    num_f = TABLE_INFO(reg_id).num_fields;

                    if ((0 == sal_strcasecmp("QuadMac", ptr_module->module_name))
                       && (0xFFFFFFFF != sub_index) && ((sub_index * 36) != entry_cn))
                    {
                        continue;
                    }

                    if ((0 == sal_strcasecmp("QuadMac", ptr_module->module_name)) && (0 == entry_cn % 36))
                    {
                        CTC_DKITS_PRINT_FILE(p_wf, "\n******************Quad MAC index%d******************\n", entry_cn / 36);
                    }

                    end_f = &first_f[num_f];
                    CTC_DKITS_PRINT_FILE(p_wf, "\n*********%s*********\n", desc[entry_cn].desc);

                    cmd = DRV_IOR(reg_id, DRV_ENTRY_FLAG);
                    DRV_IOCTL(chip_id, entry_cn, cmd, data_entry);

                    while (first_f < end_f)
                    {
                        drv_greatbelt_get_field(reg_id, field_id, data_entry, &value);
                        CTC_DKITS_PRINT_FILE(p_wf, "%-40s  : 0x%08x\n", first_f->ptr_field_name, value);
                        first_f++;
                        field_id++;
                    }
                }
            }
        }

        b_show  = FALSE;
    }

    return CLI_SUCCESS;
}


STATIC int32
_ctc_greatbelt_dkit_internal_module_process(ctc_cmd_element_t *self, ctc_vti_t *vty, int argc, char **argv)
{

    uint32 chip_id = 0;
    uint8 arg_id = 0;
    char* module_name = NULL;
    char *master_name = NULL;
    dkit_mod_id_t module_id = 0;

    dkit_modules_t* ptr_module = NULL;
    uint32 inst_num = 0;

    uint32 index    = 0xFFFF;
    uint32 sub_index = 0xFFFFFFFF;
    uint32 start_no = 0;
    uint32 end_no   = 0;
    uint8  dump_all = 0;
    dkit_mod_id_t  dump_idx = 0;
    sal_file_t p_wf = NULL;
    char cfg_file[256] = {0};
    int32 ret = CLI_SUCCESS;

    master_name = argv[0];
    module_name = argv[1];

    arg_id = ctc_cli_get_prefix_item(&argv[0], argc, "index", sal_strlen("index"));
    if (0xFF != arg_id)
    {
        CTC_CLI_GET_INTEGER("index", index, argv[arg_id + 1]);
    }

    arg_id = ctc_cli_get_prefix_item(&argv[0], argc, "sub-index", sal_strlen("sub-index"));
    if (0xFF != arg_id)
    {
        CTC_CLI_GET_INTEGER("sub-index", sub_index, argv[arg_id + 1]);
    }

    arg_id = ctc_cli_get_prefix_item(&argv[0], argc, "lchip", sal_strlen("lchip"));
    if (0xFF != arg_id)
    {
        CTC_CLI_GET_INTEGER("lchip", chip_id, argv[arg_id + 1]);
    }

    arg_id = ctc_cli_get_prefix_item(&argv[0], argc, "output-file", sal_strlen("output-file"));
    if (0xFF != arg_id)
    {
        sal_memcpy(cfg_file, argv[arg_id + 1], sal_strlen(argv[arg_id + 1]));
        sal_sprintf(cfg_file, "%s%s", cfg_file, DUMP_DECODC_TXT_FILE_POSTFIX);
        CTC_DKIT_PRINT("file name: %s\n",cfg_file);
        p_wf = sal_fopen(cfg_file, "wb+");
        if (NULL == p_wf)
        {
            CTC_DKIT_PRINT(" Open file: %s failed!\n\n", cfg_file);
            return CLI_ERROR;
        }
    }

    if (p_wf || dkits_log_file)
    {
        /*Header*/
        ctc_dkit_discard_para_t para_discard;
        ctc_dkit_monitor_para_t para_monitor;

        /*show chip base info*/
        ctc_greatbelt_dkit_misc_chip_info(chip_id, p_wf);

        /*show discard*/
        sal_memset(&para_discard, 0, sizeof(para_discard));
        para_discard.lchip = chip_id;
        para_discard.p_wf = p_wf;
        ctc_greatbelt_dkit_discard_process((void*)(&para_discard));
        ctc_greatbelt_dkit_discard_process((void*)(&para_discard));

        /*show monitor queue-resource ingress*/
        sal_memset(&para_monitor, 0, sizeof(para_monitor));
        para_monitor.gport = 0xFFFF;
        para_monitor.dir = CTC_DKIT_INGRESS;
        para_monitor.lchip = chip_id;
        para_monitor.p_wf = p_wf;
        CTC_DKITS_PRINT_FILE(p_wf, "\n### Monitor queue-resource ingress: ###\n");
        ctc_greatbelt_dkit_monitor_show_queue_depth((void*)(&para_monitor));
        sal_memset(&para_monitor, 0, sizeof(para_monitor));
        para_monitor.gport = 0xFFFF;
        para_monitor.dir = CTC_DKIT_EGRESS;
        para_monitor.lchip = chip_id;
        para_monitor.p_wf = p_wf;
        CTC_DKITS_PRINT_FILE(p_wf, "\n### Monitor queue-resource egress: ###\n");
        ctc_greatbelt_dkit_monitor_show_queue_depth((void*)(&para_monitor));

        sal_memset(&para_monitor, 0, sizeof(para_monitor));
        para_monitor.gport = 0xFFFF;
        para_monitor.dir = CTC_DKIT_BOTH_DIRECTION;
        para_monitor.lchip = chip_id;
        para_monitor.p_wf = p_wf;
        CTC_DKITS_PRINT_FILE(p_wf, "\n### Monitor queue-depth: ###\n");
        ctc_greatbelt_dkit_monitor_show_queue_depth((void*)(&para_monitor));
    }

    /*get module id*/
    if ((0 == sal_strcmp("all", module_name)) || (0 == sal_strcmp("all", master_name)))
    {
        dump_all = 1;
    }
    else if (!ctc_greatbelt_dkit_drv_get_module_id_by_string(module_name, (uint32*)&module_id))
    {
        ret = CLI_ERROR;
        goto ERROR_EXIT;
    }

    for (; dump_idx < (dump_all ? Mod_IPE : 1); dump_idx++)
    {
        module_id = dump_all ? dump_idx : module_id;
        if (sal_strcasecmp(DKIT_MODULE_GET_INFO(module_id).master_name, master_name)
            && sal_strcmp("all", master_name))
        {
            continue;
        }

        ptr_module = DKIT_MODULE_GET_INFOPTR(module_id);
        inst_num   = ptr_module->inst_num;
        if (0xFFFF == index)
        {
            start_no    = 0;
            end_no      = inst_num;
        }
        else if (inst_num < index)
        {
            ret = CLI_ERROR;
            goto ERROR_EXIT;
        }
        else
        {
            start_no    = index;
            end_no      = index + 1;
        }

        for (; start_no < end_no; start_no++)
        {
            /*process*/
            CTC_DKITS_PRINT_FILE(p_wf, "\nChip[%d] %s %d statistics:\n", chip_id, module_name, start_no);
            _ctc_greatbelt_dkit_internal_module_read(chip_id, ptr_module, start_no, sub_index, p_wf);
        }
    }

ERROR_EXIT:
    if (NULL != p_wf)
    {
        sal_fclose(p_wf);
    }

    return ret;
}


STATIC int32
_ctc_greatbelt_dkit_internal_module_init(int (*func) (ctc_cmd_element_t *, ctc_vti_t *, int, char **), uint8 cli_tree_mode, uint8 install)
{

#define MAX_CLI_LEN         128
#define MAX_CLI_TIPS_LEN    64

    ctc_cmd_element_t* cli_cmd = NULL;
    int32 i = 0, j = 0;
    char* cmd_str = NULL;
    char** cli_help = NULL;
    int cnt = 0, tip_node_num = 0;
    char module_name[MAX_CLI_TIPS_LEN] = {0};

    for (i = 0; i < MaxModId_m; i++)
    {
        cmd_str = sal_malloc(MAX_CLI_LEN);
        if (NULL == cmd_str)
        {
            goto error_return;
        }

        sal_memset(cmd_str, 0, MAX_CLI_LEN);

        if (i >= Mod_IPE)
        {
            gb_dkit_modules_list[i].module_name = "all";
            gb_dkit_modules_list[i].inst_num = 1;
            gb_dkit_modules_list[i].sub_num = 1;
            if (i == Mod_IPE)
            {
                gb_dkit_modules_list[i].master_name = "ipe";
            }
            else if (i == Mod_EPE)
            {
                gb_dkit_modules_list[i].master_name = "epe";
            }
            else if (i == Mod_BSR)
            {
                gb_dkit_modules_list[i].master_name = "bsr";
            }
            else if (i == Mod_SHARE)
            {
                gb_dkit_modules_list[i].master_name = "share";
            }
            else if (i == Mod_MISC)
            {
                gb_dkit_modules_list[i].master_name = "misc";
            }
            else if (i == Mod_MAC)
            {
                gb_dkit_modules_list[i].master_name = "mac";
            }
            else if (i == Mod_OAM)
            {
                gb_dkit_modules_list[i].master_name = "oam";
            }
            else if (i == Mod_ALL)
            {
                gb_dkit_modules_list[i].master_name = "all";
            }
        }

        sal_memset(module_name, 0, MAX_CLI_TIPS_LEN);

        if (i == Mod_ALL)
        {
            sal_strcpy(module_name, "all");
        }
        else
        {
            for (j = 0; j < sal_strlen(gb_dkit_modules_list[i].module_name); j++)
            {
                module_name[j] = sal_tolower(gb_dkit_modules_list[i].module_name[j]);
            }
        }

        if (gb_dkit_modules_list[i].inst_num > 1)
        {
            if (gb_dkit_modules_list[i].sub_num > 1)
            {
                sal_sprintf(cmd_str, "show statistic (%s) (%s) (index INDEX) (sub-index SUB_INDEX|) (output-file FILE_NAME|) (lchip CHIP_ID|)", gb_dkit_modules_list[i].master_name, module_name);
                tip_node_num = 13;
            }
            else
            {
                sal_sprintf(cmd_str, "show statistic (%s) (%s) (index INDEX) (output-file FILE_NAME|) (lchip CHIP_ID|)", gb_dkit_modules_list[i].master_name, module_name);
                tip_node_num = 11;
            }
        }
        else
        {
            if (gb_dkit_modules_list[i].sub_num > 1)
            {
                sal_sprintf(cmd_str, "show statistic (%s) (%s) (sub-index SUB_INDEX|) (output-file FILE_NAME|) (lchip CHIP_ID|)", gb_dkit_modules_list[i].master_name, module_name);
                tip_node_num = 11;
            }
            else
            {
                sal_sprintf(cmd_str, "show statistic (%s) (%s) (output-file FILE_NAME|) (lchip CHIP_ID|)", gb_dkit_modules_list[i].master_name, module_name);
                tip_node_num = 9;
            }
        }

        cli_help = sal_malloc(sizeof(char*) * tip_node_num);
        if (NULL == cli_help)
        {
            goto error_return;
        }
        for (cnt = 0; cnt < tip_node_num; cnt++)
        {
            cli_help[cnt] = sal_malloc(MAX_CLI_TIPS_LEN);
            if (NULL == cli_help[cnt])
            {
                goto error_return;
            }
            sal_memset(cli_help[cnt], 0, MAX_CLI_TIPS_LEN);
        }

        cli_help[tip_node_num - 1] = NULL;

        sal_sprintf(cli_help[0], "show chip statistic info cmd");
        sal_sprintf(cli_help[1], "ipe/bsr/mac/misc/oam/share/all status info");
        if (gb_dkit_modules_list[i].inst_num > 1)
        {
            if (gb_dkit_modules_list[i].sub_num > 1)
            {
                sal_sprintf(cli_help[2], "%s status", gb_dkit_modules_list[i].master_name);
                sal_sprintf(cli_help[3], "show %s statistic", gb_dkit_modules_list[i].module_name);
                sal_sprintf(cli_help[4], "%s index", gb_dkit_modules_list[i].module_name);
                sal_sprintf(cli_help[5], "<0-%d>", gb_dkit_modules_list[i].inst_num - 1);
                sal_sprintf(cli_help[6], "%s internal sub index", gb_dkit_modules_list[i].module_name);
                sal_sprintf(cli_help[7], "<0-%d>", gb_dkit_modules_list[i].sub_num - 1);
                sal_sprintf(cli_help[8], "Centec file");
                sal_sprintf(cli_help[9], "Centec file");
                sal_sprintf(cli_help[10], "Chip id on linecard");
                sal_sprintf(cli_help[11], "<0-1>");
            }
            else
            {
                sal_sprintf(cli_help[2], "%s status", gb_dkit_modules_list[i].master_name);
                sal_sprintf(cli_help[3], "show %s statistic", gb_dkit_modules_list[i].module_name);
                sal_sprintf(cli_help[4], "Index");
                sal_sprintf(cli_help[5], "<0-%d>", gb_dkit_modules_list[i].inst_num - 1);
                sal_sprintf(cli_help[6], "Centec file");
                sal_sprintf(cli_help[7], "Centec file");
                sal_sprintf(cli_help[8], "Chip id on linecard");
                sal_sprintf(cli_help[9], "<0-1>");
            }
        }
        else
        {
            if (gb_dkit_modules_list[i].sub_num > 1)
            {
                sal_sprintf(cli_help[2], "%s status", gb_dkit_modules_list[i].master_name);
                sal_sprintf(cli_help[3], "show %s statistic", gb_dkit_modules_list[i].module_name);
                sal_sprintf(cli_help[4], "%s internal sub index", gb_dkit_modules_list[i].module_name);
                sal_sprintf(cli_help[5], "<0-%d>", gb_dkit_modules_list[i].sub_num - 1);
                sal_sprintf(cli_help[6], "Centec file");
                sal_sprintf(cli_help[7], "Centec file");
                sal_sprintf(cli_help[8], "Chip id on linecard");
                sal_sprintf(cli_help[9], "<0-1>");
            }
            else
            {
                sal_sprintf(cli_help[2], "%s status", gb_dkit_modules_list[i].master_name);
                sal_sprintf(cli_help[3], "show %s statistic", gb_dkit_modules_list[i].module_name);
                sal_sprintf(cli_help[4], "Centec file");
                sal_sprintf(cli_help[5], "Centec file");
                sal_sprintf(cli_help[6], "Chip id on linecard");
                sal_sprintf(cli_help[7], "<0-1>");
            }
        }

        cli_cmd = sal_malloc(sizeof(ctc_cmd_element_t));
        if (NULL == cli_cmd)
        {
            goto error_return;
        }
        sal_memset(cli_cmd, 0, sizeof(ctc_cmd_element_t));

        cli_cmd->func   = func;
        cli_cmd->string = cmd_str;
        cli_cmd->doc    = cli_help;

        if (install)
        {
            install_element(cli_tree_mode, cli_cmd);
        }
        else
        {
            uninstall_element(cli_tree_mode, cli_cmd);
        }
    }

    return CLI_SUCCESS;

error_return:

    for (cnt= 0; (cnt < tip_node_num) && cli_help; cnt++)
    {
        if (cli_help[cnt])
        {
            sal_free(cli_help[cnt]);
            cli_help[cnt] = NULL;
        }
    }
    if (cli_help)
    {
        sal_free(cli_help);
        cli_help = NULL;
    }
    if (cmd_str)
    {
        sal_free(cmd_str);
        cmd_str = NULL;
    }
    return CLI_ERROR;
}

STATIC uint8
_ctc_greatbelt_dkit_internal_common_get_lport_with_mac(uint8 lchip, uint8 mac_id)
{
#define CTC_DKIT_MAX_GMAC_PORT_NUM   48
#define CTC_DKIT_MAX_SGMAC_PORT_NUM  12
    uint32 chan_id = 0;
    uint32 cmd = 0;
    int32 ret = 0;

    ipe_header_adjust_phy_port_map_t ipe_header_adjust_phyport_map;
    sal_memset(&ipe_header_adjust_phyport_map, 0, sizeof(ipe_header_adjust_phy_port_map_t));

    /* check mac valid */
    if (mac_id >= (CTC_DKIT_MAX_GMAC_PORT_NUM+CTC_DKIT_MAX_SGMAC_PORT_NUM))
    {
         return 0xFF;
    }

    cmd = DRV_IOR(NetRxChannelMap_t, NetRxChannelMap_ChanId_f);
    ret = DRV_IOCTL(lchip, mac_id, cmd, &chan_id);

    cmd = DRV_IOR(IpeHeaderAdjustPhyPortMap_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, chan_id, cmd, &ipe_header_adjust_phyport_map);

    if (!ret)
    {
        return ipe_header_adjust_phyport_map.local_phy_port;
    }
    else
    {
        return 0xFF;
    }
}

STATIC int32
_ctc_greatbelt_dkit_internal_soft_reset_gmac(uint8 mac_id, uint8 pcs_mode, uint8 enable)
{
    uint8  lchip = 0;
    uint32 tbl_step1 = 0;
    uint32 tbl_step2 = 0;
    uint32 field_value = 0;
    uint32 field_id = 0;
    uint32 tbl_id = 0;
    uint32 cmd = 0;

    /* 0. check whether to reset quadmac/quadpcs */
    if ((DRV_SERDES_SGMII_MODE == pcs_mode) || (DRV_SERDES_2DOT5_MODE == pcs_mode))
    {
        if (mac_id > 23)
        {
            return CLI_ERROR;
        }
    }

    /* 1. set gmac tx soft reset */
    tbl_step1 = QuadMacGmac1SoftRst0_t - QuadMacGmac0SoftRst0_t;
    tbl_step2 = QuadMacGmac0SoftRst1_t - QuadMacGmac0SoftRst0_t;
    tbl_id = QuadMacGmac0SoftRst0_t + (mac_id % 4) * tbl_step1 + (mac_id / 4) * tbl_step2;
    field_id = QuadMacGmac0SoftRst_Gmac0SgmiiTxSoftRst_f;
    field_value = enable;
    cmd = DRV_IOW(tbl_id, field_id);
    DRV_IOCTL(lchip, 0, cmd, &field_value);

    /* 2. set pcs soft reset */
    if ((DRV_SERDES_SGMII_MODE == pcs_mode) || (DRV_SERDES_2DOT5_MODE == pcs_mode))
    {
        tbl_step1 = QuadPcsPcs1SoftRst0_t - QuadPcsPcs0SoftRst0_t;
        tbl_step2 = QuadPcsPcs0SoftRst1_t - QuadPcsPcs0SoftRst0_t;
        tbl_id = QuadPcsPcs0SoftRst0_t + (mac_id % 4) * tbl_step1 + (mac_id / 4) * tbl_step2;

        field_id = QuadPcsPcs0SoftRst_Pcs0RxSoftRst_f;
        field_value = enable;
        cmd = DRV_IOW(tbl_id, field_id);
        DRV_IOCTL(lchip, 0, cmd, &field_value);

        field_id = QuadPcsPcs0SoftRst_Pcs0TxSoftRst_f;
        field_value = enable;
        cmd = DRV_IOW(tbl_id, field_id);
        DRV_IOCTL(lchip, 0, cmd, &field_value);
    }
    else if (DRV_SERDES_QSGMII_MODE == pcs_mode)
    {

    }

    /* delay sometime to occur link interrupt */
    sal_task_sleep(10);

    /* 3. set gmac rx soft reset */
    tbl_step1 = QuadMacGmac1SoftRst0_t - QuadMacGmac0SoftRst0_t;
    tbl_step2 = QuadMacGmac0SoftRst1_t - QuadMacGmac0SoftRst0_t;
    tbl_id = QuadMacGmac0SoftRst0_t + (mac_id % 4) * tbl_step1 + (mac_id / 4) * tbl_step2;
    field_id = QuadMacGmac0SoftRst_Gmac0SgmiiRxSoftRst_f;
    field_value = enable;
    cmd = DRV_IOW(tbl_id, field_id);
    DRV_IOCTL(lchip, 0, cmd, &field_value);

    /* 4. wait flush, 5ms */
    sal_task_sleep(5);

    return CLI_SUCCESS;
}

STATIC int32
_ctc_greatbelt_dkit_internal_soft_reset_sgmac(uint8 mac_id, uint8 pcs_mode, uint8 enable)
{
    uint32 field_value = 0;
    uint32 field_id = 0;
    uint32 tbl_id = 0;
    uint32 cmd = 0;
    uint8  index = 0, lchip = 0;

    /* 1. set xaui serdes rx soft reset */
    if (DRV_SERDES_XAUI_MODE == pcs_mode)
    {
        tbl_id = SgmacSoftRst4_t + (mac_id - 4);
        field_value = enable;

        for (index = 0; index < 4; index++)
        {
            field_id = SgmacSoftRst_SerdesRx0SoftRst_f + index;
            cmd = DRV_IOW(tbl_id, field_id);
            DRV_IOCTL(lchip, 0, cmd, &field_value);
        }
    }

    /* 2. set pcs rx soft reset */
    tbl_id = SgmacSoftRst0_t + mac_id;
    field_value = enable;
    cmd = DRV_IOW(tbl_id, SgmacSoftRst_PcsRxSoftRst_f);
    DRV_IOCTL(lchip, 0, cmd, &field_value);

    /* 3. set pcs tx soft reset */
    tbl_id = SgmacSoftRst0_t + mac_id;
    field_value = enable;
    cmd = DRV_IOW(tbl_id, SgmacSoftRst_PcsTxSoftRst_f);
    DRV_IOCTL(lchip, 0, cmd, &field_value);

    return CLI_ERROR;
}

STATIC int32
_ctc_greatbelt_dkit_internal_mac_soft_rst(uint8 lchip, uint8 mac_id, bool enable)
{
    int32 ret = CLI_SUCCESS;
    uint8 lport = 0, chanid = 0;
    drv_datapath_port_capability_t port_cap;

    lport = _ctc_greatbelt_dkit_internal_common_get_lport_with_mac(lchip, mac_id);
    if (0xFF == lport)
    {
        CTC_DKIT_PRINT("Invalid macid:%u.\n", mac_id);
        return CLI_ERROR;
    }

    sal_memset(&port_cap, 0, sizeof(drv_datapath_port_capability_t));

    chanid = drv_greatbelt_get_port_capability(lchip, lport, &port_cap);
    if (INVALID_CHAN_ID == chanid)
    {
        CTC_DKIT_PRINT("Invalid chanid:%u.\n", chanid);
        return CLI_ERROR;
    }

    if (DRV_PORT_TYPE_1G == port_cap.port_type)
    {
        _ctc_greatbelt_dkit_internal_soft_reset_gmac(mac_id, port_cap.pcs_mode, enable);
    }
    else if (DRV_PORT_TYPE_SG == port_cap.port_type)
    {
        _ctc_greatbelt_dkit_internal_soft_reset_sgmac(mac_id, port_cap.pcs_mode, enable);
    }

    return ret;
}


STATIC int32
_ctc_greatbelt_dkit_internal_bs_pb_hdr_bus2ms_packet_header(bs_pb_hdr_bus_t* p_bs_pb_hdr_bus, ms_packet_header_t* p_ms_packet_header)
{
    p_ms_packet_header->source_port15_14 = p_bs_pb_hdr_bus->source_port15_14;
    p_ms_packet_header->packet_offset = (p_bs_pb_hdr_bus->packet_offset_6_7 << 6) | p_bs_pb_hdr_bus->packet_offset_0_5;
    /* p_ms_packet_header->dest_map = */
    p_ms_packet_header->priority = p_bs_pb_hdr_bus->priority;
    p_ms_packet_header->packet_type = p_bs_pb_hdr_bus->packet_type;
    p_ms_packet_header->source_cos = p_bs_pb_hdr_bus->source_cos;
    p_ms_packet_header->src_queue_select = p_bs_pb_hdr_bus->src_queue_select;
    p_ms_packet_header->header_hash2_0 = p_bs_pb_hdr_bus->header_hash2_0;
    p_ms_packet_header->logic_port_type = p_bs_pb_hdr_bus->logic_port_type;
    p_ms_packet_header->src_ctag_offset_type = p_bs_pb_hdr_bus->src_ctag_offset_type;
    p_ms_packet_header->source_port = (p_bs_pb_hdr_bus->source_port_6_13 << 6) | p_bs_pb_hdr_bus->source_port_0_5;
    p_ms_packet_header->src_vlan_id = p_bs_pb_hdr_bus->src_vlan_id;
    p_ms_packet_header->color = p_bs_pb_hdr_bus->color;
    p_ms_packet_header->bridge_operation = p_bs_pb_hdr_bus->bridge_operation;
    p_ms_packet_header->next_hop_ptr = (p_bs_pb_hdr_bus->next_hop_ptr_6_16 << 6) | p_bs_pb_hdr_bus->next_hop_ptr_0_5;
    /* p_ms_packet_header->length_adjust_type = */
    p_ms_packet_header->critical_packet = p_bs_pb_hdr_bus->critical_packet;
    p_ms_packet_header->rxtx_fcl22_17 = p_bs_pb_hdr_bus->rxtx_fcl22_17;
    p_ms_packet_header->flow = p_bs_pb_hdr_bus->flow;
    p_ms_packet_header->ttl = p_bs_pb_hdr_bus->ttl;
    p_ms_packet_header->from_fabric = p_bs_pb_hdr_bus->from_fabric;
    p_ms_packet_header->bypass_ingress_edit = p_bs_pb_hdr_bus->bypass_ingress_edit;
    p_ms_packet_header->source_port_extender = p_bs_pb_hdr_bus->source_port_extender;
    p_ms_packet_header->loopback_discard = p_bs_pb_hdr_bus->loopback_discard;
    /* p_ms_packet_header->header_crc = */
    p_ms_packet_header->source_port_isolate_id = p_bs_pb_hdr_bus->source_port_isolate_id;
    p_ms_packet_header->pbb_src_port_type = p_bs_pb_hdr_bus->pbb_src_port_type;
    p_ms_packet_header->svlan_tag_operation_valid = p_bs_pb_hdr_bus->svlan_tag_operation_valid;
    p_ms_packet_header->source_cfi = p_bs_pb_hdr_bus->source_cfi;
    /* p_ms_packet_header->next_hop_ext = ; */
    p_ms_packet_header->non_crc = p_bs_pb_hdr_bus->non_crc;
    p_ms_packet_header->from_cpu_or_oam = p_bs_pb_hdr_bus->from_cpu_or_oam;
    p_ms_packet_header->svlan_tpid_index = p_bs_pb_hdr_bus->svlan_tpid_index;
    p_ms_packet_header->stag_action = p_bs_pb_hdr_bus->stag_action;
    p_ms_packet_header->src_svlan_id_valid = p_bs_pb_hdr_bus->src_svlan_id_valid;
    p_ms_packet_header->src_cvlan_id_valid = p_bs_pb_hdr_bus->src_cvlan_id_valid;
    p_ms_packet_header->src_cvlan_id = p_bs_pb_hdr_bus->src_cvlan_id;
    p_ms_packet_header->src_vlan_ptr = p_bs_pb_hdr_bus->src_vlan_ptr;
    p_ms_packet_header->fid = p_bs_pb_hdr_bus->fid;
    p_ms_packet_header->logic_src_port = p_bs_pb_hdr_bus->logic_src_port;
    p_ms_packet_header->rxtx_fcl3 = p_bs_pb_hdr_bus->rxtx_fcl3;
    p_ms_packet_header->cut_through = p_bs_pb_hdr_bus->cut_through;
    p_ms_packet_header->rxtx_fcl2_1 = p_bs_pb_hdr_bus->rxtx_fcl2_1;
    p_ms_packet_header->mux_length_type = p_bs_pb_hdr_bus->mux_length_type;
    p_ms_packet_header->oam_tunnel_en = p_bs_pb_hdr_bus->oam_tunnel_en;
    p_ms_packet_header->rxtx_fcl0 = p_bs_pb_hdr_bus->rxtx_fcl0;
    p_ms_packet_header->operation_type = p_bs_pb_hdr_bus->operation_type;
    p_ms_packet_header->header_hash7_3 = p_bs_pb_hdr_bus->header_hash7_3;
    p_ms_packet_header->ip_sa = p_bs_pb_hdr_bus->ip_sa;

    return DRV_E_NONE;
}

STATIC int32
_ctc_greatbelt_dkit_internal_show_bridge_header(ms_packet_header_t* p_ms_packet_header)
{
    CTC_DKIT_PRINT("\n----------Bridge Header Info---------\n");
    CTC_DKIT_PRINT("field name               :value\n");
    CTC_DKIT_PRINT("-------------------------------------\n");
    CTC_DKIT_PRINT("%-25s: 0x%X\n", "source_port15_14",          p_ms_packet_header->source_port15_14);
    CTC_DKIT_PRINT("%-25s: 0x%X\n", "packet_offset",             p_ms_packet_header->packet_offset);
    CTC_DKIT_PRINT("%-25s: 0x%X\n", "dest_map",                  p_ms_packet_header->dest_map);
    CTC_DKIT_PRINT("%-25s: 0x%X\n", "priority",                  p_ms_packet_header->priority);
    CTC_DKIT_PRINT("%-25s: 0x%X\n", "packet_type",               p_ms_packet_header->packet_type);
    CTC_DKIT_PRINT("%-25s: 0x%X\n", "source_cos",                p_ms_packet_header->source_cos);
    CTC_DKIT_PRINT("%-25s: 0x%X\n", "src_queue_select",          p_ms_packet_header->src_queue_select);
    CTC_DKIT_PRINT("%-25s: 0x%X\n", "header_hash2_0",            p_ms_packet_header->header_hash2_0);
    CTC_DKIT_PRINT("%-25s: 0x%X\n", "logic_port_type",           p_ms_packet_header->logic_port_type);
    CTC_DKIT_PRINT("%-25s: 0x%X\n", "src_ctag_offset_type",      p_ms_packet_header->src_ctag_offset_type);
    CTC_DKIT_PRINT("%-25s: 0x%X\n", "source_port",               p_ms_packet_header->source_port);
    CTC_DKIT_PRINT("%-25s: 0x%X\n", "src_vlan_id",               p_ms_packet_header->src_vlan_id);
    CTC_DKIT_PRINT("%-25s: 0x%X\n", "color",                     p_ms_packet_header->color);
    CTC_DKIT_PRINT("%-25s: 0x%X\n", "bridge_operation",          p_ms_packet_header->bridge_operation);
    CTC_DKIT_PRINT("%-25s: 0x%X\n", "next_hop_ptr",              p_ms_packet_header->next_hop_ptr);
    CTC_DKIT_PRINT("%-25s: 0x%X\n", "length_adjust_type",        p_ms_packet_header->length_adjust_type);
    CTC_DKIT_PRINT("%-25s: 0x%X\n", "critical_packet",           p_ms_packet_header->critical_packet);
    CTC_DKIT_PRINT("%-25s: 0x%X\n", "rxtx_fcl22_17",             p_ms_packet_header->rxtx_fcl22_17);
    CTC_DKIT_PRINT("%-25s: 0x%X\n", "flow",                      p_ms_packet_header->flow);
    CTC_DKIT_PRINT("%-25s: 0x%X\n", "ttl",                       p_ms_packet_header->ttl);
    CTC_DKIT_PRINT("%-25s: 0x%X\n", "from_fabric",               p_ms_packet_header->from_fabric);
    CTC_DKIT_PRINT("%-25s: 0x%X\n", "bypass_ingress_edit",       p_ms_packet_header->bypass_ingress_edit);
    CTC_DKIT_PRINT("%-25s: 0x%X\n", "source_port_extender",      p_ms_packet_header->source_port_extender);
    CTC_DKIT_PRINT("%-25s: 0x%X\n", "loopback_discard",          p_ms_packet_header->loopback_discard);
    CTC_DKIT_PRINT("%-25s: 0x%X\n", "header_crc",                p_ms_packet_header->header_crc);
    CTC_DKIT_PRINT("%-25s: 0x%X\n", "source_port_isolate_id",    p_ms_packet_header->source_port_isolate_id);
    CTC_DKIT_PRINT("%-25s: 0x%X\n", "pbb_src_port_type",         p_ms_packet_header->pbb_src_port_type);
    CTC_DKIT_PRINT("%-25s: 0x%X\n", "svlan_tag_operation_valid", p_ms_packet_header->svlan_tag_operation_valid);
    CTC_DKIT_PRINT("%-25s: 0x%X\n", "source_cfi",                p_ms_packet_header->source_cfi);
    CTC_DKIT_PRINT("%-25s: 0x%X\n", "next_hop_ext",              p_ms_packet_header->next_hop_ext);
    CTC_DKIT_PRINT("%-25s: 0x%X\n", "non_crc",                   p_ms_packet_header->non_crc);
    CTC_DKIT_PRINT("%-25s: 0x%X\n", "from_cpu_or_oam",           p_ms_packet_header->from_cpu_or_oam);
    CTC_DKIT_PRINT("%-25s: 0x%X\n", "svlan_tpid_index",          p_ms_packet_header->svlan_tpid_index);
    CTC_DKIT_PRINT("%-25s: 0x%X\n", "stag_action",               p_ms_packet_header->stag_action);
    CTC_DKIT_PRINT("%-25s: 0x%X\n", "src_svlan_id_valid",        p_ms_packet_header->src_svlan_id_valid);
    CTC_DKIT_PRINT("%-25s: 0x%X\n", "src_cvlan_id_valid",        p_ms_packet_header->src_cvlan_id_valid);
    CTC_DKIT_PRINT("%-25s: 0x%X\n", "src_cvlan_id",              p_ms_packet_header->src_cvlan_id);
    CTC_DKIT_PRINT("%-25s: 0x%X\n", "src_vlan_ptr",              p_ms_packet_header->src_vlan_ptr);
    CTC_DKIT_PRINT("%-25s: 0x%X\n", "fid",                       p_ms_packet_header->fid);
    CTC_DKIT_PRINT("%-25s: 0x%X\n", "logic_src_port",            p_ms_packet_header->logic_src_port);
    CTC_DKIT_PRINT("%-25s: 0x%X\n", "rxtx_fcl3",                 p_ms_packet_header->rxtx_fcl3);
    CTC_DKIT_PRINT("%-25s: 0x%X\n", "cut_through",               p_ms_packet_header->cut_through);
    CTC_DKIT_PRINT("%-25s: 0x%X\n", "rxtx_fcl2_1",               p_ms_packet_header->rxtx_fcl2_1);
    CTC_DKIT_PRINT("%-25s: 0x%X\n", "mux_length_type",           p_ms_packet_header->mux_length_type);
    CTC_DKIT_PRINT("%-25s: 0x%X\n", "oam_tunnel_en",             p_ms_packet_header->oam_tunnel_en);
    CTC_DKIT_PRINT("%-25s: 0x%X\n", "rxtx_fcl0",                 p_ms_packet_header->rxtx_fcl0);
    CTC_DKIT_PRINT("%-25s: 0x%X\n", "operation_type",            p_ms_packet_header->operation_type);
    CTC_DKIT_PRINT("%-25s: 0x%X\n", "header_hash7_3",            p_ms_packet_header->header_hash7_3);
    CTC_DKIT_PRINT("%-25s: 0x%X\n", "ip_sa",                     p_ms_packet_header->ip_sa);

    return DRV_E_NONE;
}


STATIC int32
_ctc_greatbelt_dkit_internal_show_ipe_fwd_bridge_header(uint8 chip_id)
{
    uint32 cmd = 0;
    int32  ret = 0;

    ms_packet_header_t ms_packet_header;

    sal_memset(&ms_packet_header, 0, sizeof(ms_packet_header_t));

    cmd = DRV_IOR(IpeFwdBridgeHdrRec_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(chip_id, 0, cmd, &ms_packet_header);

    if (DRV_E_NONE != ret)
    {
        CTC_DKIT_PRINT("tbl %s index 0x%x read error!\n", TABLE_NAME(IpeFwdBridgeHdrRec_t), 0);
        return DRV_E_NONE;
    }
    _ctc_greatbelt_dkit_internal_show_bridge_header(&ms_packet_header);

    return DRV_E_NONE;
}

STATIC int32
_ctc_greatbelt_dkit_internal_show_ipe_fwd_exception_info(uint8 chip_id)
{
    uint32 cmd = 0;
    int32  ret = 0;

    ms_excp_info_t ms_excp_info;

    sal_memset(&ms_excp_info, 0, sizeof(ms_excp_info_t));

    cmd = DRV_IOR(IpeFwdExcpRec_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(chip_id, 0, cmd, &ms_excp_info);

    if (DRV_E_NONE != ret)
    {
        CTC_DKIT_PRINT("tbl %s index 0x%x read error!\n", TABLE_NAME(IpeFwdExcpRec_t), 0);
        return DRV_E_NONE;
    }

    CTC_DKIT_PRINT("\n------------Exception Info-----------\n");
    CTC_DKIT_PRINT("field name               :value\n");
    CTC_DKIT_PRINT("-------------------------------------\n");
    CTC_DKIT_PRINT("%-25s: 0x%X\n", "dest_id_discard",       ms_excp_info.dest_id_discard);
    /* CTC_DKIT_PRINT("%-25s: 0x%X\n", "rsv_0",                 ms_excp_info.rsv_0); */
    CTC_DKIT_PRINT("%-25s: 0x%X\n", "exception_sub_index",   ms_excp_info.exception_sub_index);
    /* CTC_DKIT_PRINT("%-25s: 0x%X\n", "rsv_1",                 ms_excp_info.rsv_1); */
    CTC_DKIT_PRINT("%-25s: 0x%X\n", "l2_span_id",            ms_excp_info.l2_span_id);
    CTC_DKIT_PRINT("%-25s: 0x%X\n", "l3_span_id",            ms_excp_info.l3_span_id);
    CTC_DKIT_PRINT("%-25s: 0x%X\n", "acl_log_id1",           ms_excp_info.acl_log_id1);
    CTC_DKIT_PRINT("%-25s: 0x%X\n", "acl_log_id0",           ms_excp_info.acl_log_id0);
    CTC_DKIT_PRINT("%-25s: 0x%X\n", "egress_exception",      ms_excp_info.egress_exception);
    CTC_DKIT_PRINT("%-25s: 0x%X\n", "exception_packet_type", ms_excp_info.exception_packet_type);
    CTC_DKIT_PRINT("%-25s: 0x%X\n", "exception_vector",      ms_excp_info.exception_vector);
    /* CTC_DKIT_PRINT("%-25s: 0x%X\n", "rsv_2",                 ms_excp_info.rsv_2); */
    CTC_DKIT_PRINT("%-25s: 0x%X\n", "exception_from_sgmac",  ms_excp_info.exception_from_sgmac);
    CTC_DKIT_PRINT("%-25s: 0x%X\n", "exception_number",      ms_excp_info.exception_number);
    /* CTC_DKIT_PRINT("%-25s: 0x%X\n", "rsv_3",                 ms_excp_info.rsv_3); */
    CTC_DKIT_PRINT("%-25s: 0x%X\n", "acl_log_id3",           ms_excp_info.acl_log_id3);
    CTC_DKIT_PRINT("%-25s: 0x%X\n", "acl_log_id2",           ms_excp_info.acl_log_id2);
    /* CTC_DKIT_PRINT("%-25s: 0x%X\n", "rsv_4",                 ms_excp_info.rsv_4); */

    return DRV_E_NONE;
}

STATIC int32
_ctc_greatbelt_dkit_internal_show_epe_bridge_header(uint8 chip_id, uint8 index)
{
    uint32 cmd = 0;
    epe_hdr_adj_brg_hdr_ds_t epe_hdr_adj_brg_hdr_ds;

    if (index > EPE_HDR_ADJ_BRG_HDR_FIFO_MAX_INDEX)
    {
        CTC_DKIT_PRINT("%s %d header index exceed the max value 31.\n", __FUNCTION__, __LINE__);
    }

    sal_memset(&epe_hdr_adj_brg_hdr_ds, 0, sizeof(epe_hdr_adj_brg_hdr_ds_t));

    cmd = DRV_IOR(EpeHdrAdjBrgHdrFifo_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, index, cmd, &epe_hdr_adj_brg_hdr_ds));

    _ctc_greatbelt_dkit_internal_show_bridge_header(&(epe_hdr_adj_brg_hdr_ds.ms_packet_header));

    return DRV_E_NONE;
}


STATIC int32
_ctc_greatbelt_dkit_internal_get_bustroe_hdr_info(uint8 chip_id, bufstore_bus_msg_hdr_info_t* p_hdr_info, uint16 hdr_info_num)
{
    uint32 cmd = 0;
    int32 ret = DRV_E_NONE;
    met_fifo_msg_ram_t met_fifo_msg_ram;
    met_fifo_end_ptr_t met_fifo_end_ptr;
    met_fifo_start_ptr_t met_fifo_start_ptr;
    uint32 destmap0 = 0;
    uint32 destmap1 = 0;
    uint32 nexthop_ext = 0;
    uint32 start_ptr = 0;
    uint16 idx = 0;

  /*
    msgType[2:0]
    [2]: cut through mode
    [1]: sent for Sop
    [0]: sent for Eop
    3'b000: The message for normal (non cut through mode) packet, sent after Eop buffer and bridge header reach BufStore.
    3'b110: The sop message for cut through mode packet, sent after sop buffer and bridge header reach BufStore
    3'b101: The eop message for cut through mode packet, sent after sopMessage is sent and eop buffer reaches BufStore.
    3'b111: The single message for cut through mode packet. This message is sent because the packet is less than 257 bytes or the bridge header arrives later than Eop data.

    MetFifoSelect[1:0]
    2'b00: ucastLo
    2'b01: ucastHi
    2'b10: mcastLo
    2'b11: mcastHi
   */
    sal_memset(&met_fifo_msg_ram, 0, sizeof(met_fifo_msg_ram_t));

    sal_memset(&met_fifo_start_ptr, 0, sizeof(met_fifo_start_ptr_t));
    cmd = DRV_IOR(MetFifoStartPtr_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(chip_id, 0, cmd, &met_fifo_start_ptr);

    sal_memset(&met_fifo_end_ptr, 0, sizeof(met_fifo_end_ptr_t));
    cmd = DRV_IOR(MetFifoEndPtr_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(chip_id, 0, cmd, &met_fifo_end_ptr);

    start_ptr = met_fifo_start_ptr.ucast_lo_start_ptr;

    for (idx = 0; (idx < hdr_info_num) || (start_ptr + idx) <= met_fifo_end_ptr.ucast_lo_end_ptr; idx++)
    {
        cmd = DRV_IOR(MetFifoMsgRam_t, 36);
        ret = DRV_IOCTL(chip_id, start_ptr + idx, cmd, &destmap1);

        cmd = DRV_IOR(MetFifoMsgRam_t, 37);
        ret = DRV_IOCTL(chip_id, start_ptr + idx, cmd, &destmap0);

        cmd = DRV_IOR(MetFifoMsgRam_t, 26);
        ret = DRV_IOCTL(chip_id, start_ptr + idx, cmd, &nexthop_ext);

        p_hdr_info[idx].lo_destmap = (destmap1 << 4) | destmap0;
        p_hdr_info[idx].lo_nexthop_ext = nexthop_ext;
    }

    start_ptr = met_fifo_start_ptr.ucast_hi_start_ptr;

    for (idx = 0; (idx < hdr_info_num) || (start_ptr + idx) <= met_fifo_end_ptr.ucast_hi_end_ptr; idx++)
    {
        cmd = DRV_IOR(MetFifoMsgRam_t, 36);
        ret = DRV_IOCTL(chip_id, start_ptr + idx, cmd, &destmap1);

        cmd = DRV_IOR(MetFifoMsgRam_t, 37);
        ret = DRV_IOCTL(chip_id, start_ptr + idx, cmd, &destmap0);

        cmd = DRV_IOR(MetFifoMsgRam_t, 26);
        ret = DRV_IOCTL(chip_id, start_ptr + idx, cmd, &nexthop_ext);

        p_hdr_info[idx].hi_destmap = (destmap1 << 4) | destmap0;
        p_hdr_info[idx].hi_nexthop_ext = nexthop_ext;
    }

    return ret;
}


STATIC int32
_ctc_greatbelt_dkit_internal_show_bufstore_bridge_header(uint8 chip_id, uint32 chan_id)
{
    uint32 cmd = 0;
    uint32 idx = 0;
    int32  ret = 0;

    bs_pb_hdr_bus_t bs_pb_hdr_bus;
    ds_chan_info_ram_t ds_chan_info_ram;
    ms_packet_header_t ms_packet_header;
    bufstore_bus_msg_hdr_info_t hdr_info[10];

    sal_memset(&ds_chan_info_ram, 0, sizeof(ds_chan_info_ram_t));
    sal_memset(&bs_pb_hdr_bus, 0, sizeof(bs_pb_hdr_bus_t));
    sal_memset(&ms_packet_header, 0, sizeof(ms_packet_header_t));

    cmd = DRV_IOR(ChanInfoRam_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(chip_id, chan_id, cmd, &ds_chan_info_ram);

    if (DRV_E_NONE != ret)
    {
        CTC_DKIT_PRINT("tbl %s index 0x%x read error!\n", TABLE_NAME(ChanInfoRam_t), 0);
        return DRV_E_NONE;
    }

    cmd = DRV_IOR(PbCtlHdrBuf_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(chip_id, ds_chan_info_ram.head_buffer_ptr, cmd, &bs_pb_hdr_bus);

    if (DRV_E_NONE != ret)
    {
        CTC_DKIT_PRINT("tbl %s index 0x%x read error!\n", TABLE_NAME(PbCtlHdrBuf_t), 0);
        return DRV_E_NONE;
    }

    sal_memset(hdr_info, 0, sizeof(bufstore_bus_msg_hdr_info_t) * 10);
    _ctc_greatbelt_dkit_internal_get_bustroe_hdr_info(chan_id, hdr_info, 10);

    _ctc_greatbelt_dkit_internal_bs_pb_hdr_bus2ms_packet_header(&bs_pb_hdr_bus, &ms_packet_header);
    _ctc_greatbelt_dkit_internal_show_bridge_header(&ms_packet_header);

    CTC_DKIT_PRINT("Potential first 20 destmaps in bufstore ram.\n");
    for (idx = 0; idx < 10; idx++)
    {
        CTC_DKIT_PRINT("destmap = 0x%X\n", hdr_info[idx].lo_destmap);
    }
    for (idx = 0; idx < 10; idx++)
    {
        CTC_DKIT_PRINT("destmap = 0x%X\n", hdr_info[idx].hi_destmap);
    }

    return DRV_E_NONE;
}


STATIC int32
_ctc_greatbelt_dkit_internal_show_bufretrieve_packet_header(uint8 chip_id, uint8 chan_id)
{
    uint32 cmd = 0;
    uint32 tail_ptr = 0;
    uint32 head_buf_ptr = 0;
    pb_ctl_pkt_buf_t pb_ctl_pkt_buf;
    bs_pb_hdr_bus_t bs_pb_hdr_bus;
    buf_retrv_pkt_msg_mem_t buf_retrv_pkt_msg_mem;
    buf_retrv_pkt_status_mem_t buf_retrv_pkt_status_mem;
    buf_retrv_pkt_config_mem_t buf_retrv_pkt_config_mem;
    ms_packet_header_t ms_packet_header;

    sal_memset(&pb_ctl_pkt_buf, 0, sizeof(pb_ctl_pkt_buf_t));
    sal_memset(&bs_pb_hdr_bus, 0, sizeof(bs_pb_hdr_bus_t));
    sal_memset(&buf_retrv_pkt_msg_mem, 0, sizeof(buf_retrv_pkt_msg_mem_t));
    sal_memset(&buf_retrv_pkt_status_mem, 0, sizeof(buf_retrv_buf_config_mem_t));
    sal_memset(&buf_retrv_pkt_config_mem, 0, sizeof(buf_retrv_pkt_config_mem_t));
    sal_memset(&ms_packet_header, 0, sizeof(ms_packet_header_t));

    if (chan_id >= CTC_DKIT_ONE_SLICE_CHANNEL_NUM)
    {
        CTC_DKIT_PRINT("%s %d channel id exceed the max value 63.\n", __FUNCTION__, __LINE__);
    }

    /*Step 1.Read BufRetrvPktStatusMem using destination channel as index */
    cmd = DRV_IOR(BufRetrvPktStatusMem_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, chan_id, cmd, &buf_retrv_pkt_status_mem));
    tail_ptr = buf_retrv_pkt_status_mem.tail_ptr;

    /*recal tail_ptr*/
    cmd = DRV_IOR(BufRetrvPktConfigMem_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, chan_id, cmd, &buf_retrv_pkt_config_mem));
    if (tail_ptr == buf_retrv_pkt_config_mem.start_ptr)
    {
        tail_ptr = buf_retrv_pkt_config_mem.end_ptr;
    }
    else
    {
        tail_ptr--;
    }

    /*Step 2.Read BufRetrvPktMsgMem using the tailPtr get above */
    cmd = DRV_IOR(BufRetrvPktMsgMem_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, tail_ptr, cmd, &buf_retrv_pkt_msg_mem));

    /*Get two key information: headPtrOffset[29:28], headBufferPtr[27:14]*/
    head_buf_ptr = (buf_retrv_pkt_msg_mem.data0 & 0x0fffc000) >> 14;

    /*read PbCtlHdrBuf in PbCtl using the headBufferPtr above to get Bridge Header*/
    cmd = DRV_IOR(PbCtlHdrBuf_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, head_buf_ptr, cmd, &bs_pb_hdr_bus));

    _ctc_greatbelt_dkit_internal_bs_pb_hdr_bus2ms_packet_header(&bs_pb_hdr_bus, &ms_packet_header);
    _ctc_greatbelt_dkit_internal_show_bridge_header(&ms_packet_header);

    return DRV_E_NONE;
}


STATIC int32
_ctc_greatbelt_dkit_internal_start_capture_oam_engine_packet_header(uint8 chip_id)
{
    uint32 cmd = 0;
    uint32 enable = 0;
    oam_parser_flow_ctl_t oam_parser_flow_ctl;

    sal_memset(&oam_parser_flow_ctl, 0, sizeof(oam_parser_flow_ctl_t));

    cmd = DRV_IOW(OamParserFlowCtl_t, OamParserFlowCtl_HdrAdjustOamDrainEnable_f);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, &enable));

    return DRV_E_NONE;
}

STATIC int32
_ctc_greatbelt_dkit_internal_show_oam_engine_packet_header(uint32 chip_id, uint8 index)
{
    uint32 cmd = 0;
    uint32 enable = TRUE;
    oam_parser_pkt_fifo_t oam_parser_pkt_fifo[4];
    uint32 ms_packet_header[8];

    if (index >= (OAM_PARSER_PKT_FIFO_MAX_INDEX >> 2))
    {
        CTC_DKIT_PRINT("%s %d header index exceed the max value 8.\n", __FUNCTION__, __LINE__);
    }

    sal_memset(oam_parser_pkt_fifo, 0, sizeof(oam_parser_pkt_fifo_t) * 4);
    sal_memset(&ms_packet_header, 0, sizeof(ms_packet_header_t));

    cmd = DRV_IOW(OamParserPktFifo_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, index + 0, cmd, &oam_parser_pkt_fifo[0]));

    cmd = DRV_IOW(OamParserPktFifo_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, index + 1, cmd, &oam_parser_pkt_fifo[1]));

    cmd = DRV_IOW(OamParserPktFifo_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, index + 2, cmd, &oam_parser_pkt_fifo[2]));

    cmd = DRV_IOW(OamParserPktFifo_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, index + 3, cmd, &oam_parser_pkt_fifo[3]));

    ms_packet_header[0] = oam_parser_pkt_fifo[0].oam_parser_pkt_fifo_field1;
    ms_packet_header[1] = oam_parser_pkt_fifo[0].oam_parser_pkt_fifo_field2;
    ms_packet_header[2] = oam_parser_pkt_fifo[1].oam_parser_pkt_fifo_field1;
    ms_packet_header[3] = oam_parser_pkt_fifo[1].oam_parser_pkt_fifo_field2;
    ms_packet_header[4] = oam_parser_pkt_fifo[2].oam_parser_pkt_fifo_field1;
    ms_packet_header[5] = oam_parser_pkt_fifo[2].oam_parser_pkt_fifo_field2;
    ms_packet_header[6] = oam_parser_pkt_fifo[3].oam_parser_pkt_fifo_field1;
    ms_packet_header[7] = oam_parser_pkt_fifo[3].oam_parser_pkt_fifo_field2;

    _ctc_greatbelt_dkit_internal_show_bridge_header((ms_packet_header_t*)ms_packet_header);

    cmd = DRV_IOW(OamParserFlowCtl_t, OamParserFlowCtl_HdrAdjustOamDrainEnable_f);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, &enable));

    return DRV_E_NONE;
}


#define __CLI__

CTC_CLI(cli_gb_dkit_pcie_read,
        cli_gb_dkit_pcie_read_cmd,
        "pcie read (config|memory) ADDR (lchip CHIP_ID|)",
        "Pcie",
        "Read pcie address",
        "Read pcie cfg address",
        "Read pcie memory address",
        "Address",
        CTC_DKITS_CHIP_DESC,
        CTC_DKITS_CHIP_ID_DESC)
{
    uint32 index = 0;
    uint32 addr = 0;
    uint8 lchip = 0;
    uint32 pcie_ct_type = DKIT_PCIE_MAX;

    CTC_CLI_GET_UINT32_RANGE("address", addr, argv[1], 0, CTC_MAX_UINT32_VALUE);
    index = CTC_CLI_GET_ARGC_INDEX("config");
    if (index != 0xFF)
    {
        pcie_ct_type = DKIT_PCIE_CFG_R;
    }
    else
    {
        pcie_ct_type = DKIT_PCIE_MEM_R;
    }
    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_INTEGER("lchip", lchip, argv[index + 1]);
    }
    lchip = g_ctcs_api_en? g_api_lchip : lchip;
    _ctc_greatbelt_dkit_internal_pcie_read(lchip, pcie_ct_type, addr);
    return CLI_SUCCESS;
}


CTC_CLI(cli_gb_dkit_pcie_write,
        cli_gb_dkit_pcie_write_cmd,
        "pcie write (config|memory) ADDR VALUE (lchip CHIP_ID|)",
        "Pcie",
        "Write pcie address",
        "Write pcie cfg address",
        "Write pcie memory address",
        "Address",
        "Value",
        CTC_DKITS_CHIP_DESC,
        CTC_DKITS_CHIP_ID_DESC)
{
    uint32 index = 0;
    uint32 addr = 0;
    uint32 value = 0;
    uint32 pcie_ct_type = DKIT_PCIE_MAX;
    uint8 lchip = 0;

    CTC_CLI_GET_UINT32_RANGE("address", addr, argv[1], 0, CTC_MAX_UINT32_VALUE);
    CTC_CLI_GET_UINT32_RANGE("value", addr, argv[2], 0, CTC_MAX_UINT32_VALUE);
    index = CTC_CLI_GET_ARGC_INDEX("config");
    if (index != 0xFF)
    {
        pcie_ct_type = DKIT_PCIE_CFG_W;
    }
    else
    {
        pcie_ct_type = DKIT_PCIE_MEM_W;
    }
    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_INTEGER("lchip", lchip, argv[index + 1]);
    }
    lchip = g_ctcs_api_en? g_api_lchip : lchip;
    _ctc_greatbelt_dkit_internal_pcie_write(lchip, pcie_ct_type, addr, value);
    return CLI_SUCCESS;
}


CTC_CLI(cli_gb_dkit_mac_en,
        cli_gb_dkit_mac_en_cmd,
        "mac MAC_ID (enable | disable) (lchip CHIP_ID|)",
        "Mac",
        "Mac id",
        "Enable",
        "Disable",
        CTC_DKITS_CHIP_DESC,
        CTC_DKITS_CHIP_ID_DESC)
{
    uint8  index = 0, enable = 0;
    uint32 mac_id = 0;
    uint8 lchip = 0;

    CTC_CLI_GET_UINT32("mac-id", mac_id, argv[0]);

    index = CTC_CLI_GET_ARGC_INDEX("enable");
    if (0xFF != index)
    {
        enable = 0;
    }
    else
    {
        enable = 1;
    }
    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_INTEGER("lchip", lchip, argv[index + 1]);
    }
    lchip = g_ctcs_api_en? g_api_lchip : lchip;
    return _ctc_greatbelt_dkit_internal_mac_soft_rst(lchip, mac_id, enable);
}

CTC_CLI(cli_gb_dkit_access_mode,
        cli_gb_dkit_access_mode_cmd,
        "access-mode (pcie|i2c)",
        "Set access mode cmd",
        "PCI mode",
        "I2C mode")
{
    drv_access_type_t access_type = DRV_MAX_ACCESS_TYPE;

    if (0 == sal_strncmp(argv[0], "pcie", sal_strlen("pcie")))
    {
        access_type = DRV_PCI_ACCESS;
    }
    else if (0 == sal_strncmp(argv[0], "i2c", sal_strlen("i2c")))
    {
        access_type = DRV_I2C_ACCESS;
    }

    drv_greatbelt_chip_set_access_type(access_type);
    return CLI_SUCCESS;
}

CTC_CLI(cli_gb_dkit_serdes_read,
        cli_gb_dkit_serdes_read_cmd,
        "serdes SERDES_ID read (link-tx|link-rx|pll) ADDR_OFFSET (lchip CHIP_ID|)",
        "Serdes",
        "Serdes ID",
        "Read address",
        "Tx address",
        "Rx address",
        "PLL address",
        "Address offset",
        CTC_DKITS_CHIP_DESC,
        CTC_DKITS_CHIP_ID_DESC)
{
    ctc_dkit_serdes_wr_t ctc_dkit_serdes_wr;
    uint32 index = 0;
    uint8 lchip = 0;

    sal_memset(&ctc_dkit_serdes_wr, 0, sizeof(ctc_dkit_serdes_wr_t));

    CTC_CLI_GET_UINT32_RANGE("serdes-id", ctc_dkit_serdes_wr.serdes_id, argv[0], 0, CTC_MAX_UINT32_VALUE);
    CTC_CLI_GET_UINT32_RANGE("addr-offset", ctc_dkit_serdes_wr.addr_offset, argv[2], 0, CTC_MAX_UINT32_VALUE);

    index = CTC_CLI_GET_ARGC_INDEX("link-tx");
    if (index != 0xFF)
    {
        ctc_dkit_serdes_wr.type = CTC_DKIT_SERDES_TX;
    }
    index = CTC_CLI_GET_ARGC_INDEX("link-rx");
    if (index != 0xFF)
    {
        ctc_dkit_serdes_wr.type = CTC_DKIT_SERDES_RX;
    }
    index = CTC_CLI_GET_ARGC_INDEX("pll");
    if (index != 0xFF)
    {
        ctc_dkit_serdes_wr.type = CTC_DKIT_SERDES_PLLA;
    }
    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_INTEGER("lchip", ctc_dkit_serdes_wr.lchip, argv[index + 1]);
    }
    lchip = g_ctcs_api_en? g_api_lchip : lchip;
    ctc_dkit_serdes_wr.lchip = lchip;
    ctc_greatbelt_dkit_misc_read_serdes(&ctc_dkit_serdes_wr);

    CTC_DKIT_PRINT("Read data:0x%04x\n", ctc_dkit_serdes_wr.data);

    return CLI_SUCCESS;
}


CTC_CLI(cli_gb_dkit_serdes_write,
        cli_gb_dkit_serdes_write_cmd,
        "serdes SERDES_ID write (link-tx|link-rx|pll) ADDR_OFFSET VALUE (lchip CHIP_ID|)",
        "Serdes",
        "Serdes ID",
        "Write address",
        "Tx address",
        "Rx address",
        "PLL address",
        "Address offset",
        "Value",
        CTC_DKITS_CHIP_DESC,
        CTC_DKITS_CHIP_ID_DESC)
{
    ctc_dkit_serdes_wr_t ctc_dkit_serdes_wr;
    uint32 index = 0;
    uint8 lchip = 0;

    sal_memset(&ctc_dkit_serdes_wr, 0, sizeof(ctc_dkit_serdes_wr_t));

    CTC_CLI_GET_UINT32_RANGE("serdes-id", ctc_dkit_serdes_wr.serdes_id, argv[0], 0, CTC_MAX_UINT32_VALUE);
    CTC_CLI_GET_UINT32_RANGE("addr-offset", ctc_dkit_serdes_wr.addr_offset, argv[2], 0, CTC_MAX_UINT32_VALUE);
    CTC_CLI_GET_UINT16_RANGE("value", ctc_dkit_serdes_wr.data, argv[3], 0, CTC_MAX_UINT16_VALUE);

    index = CTC_CLI_GET_ARGC_INDEX("link-tx");
    if (index != 0xFF)
    {
        ctc_dkit_serdes_wr.type = CTC_DKIT_SERDES_TX;
    }
    index = CTC_CLI_GET_ARGC_INDEX("link-rx");
    if (index != 0xFF)
    {
        ctc_dkit_serdes_wr.type = CTC_DKIT_SERDES_RX;
    }
    index = CTC_CLI_GET_ARGC_INDEX("pll");
    if (index != 0xFF)
    {
        ctc_dkit_serdes_wr.type = CTC_DKIT_SERDES_PLLA;
    }
    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_INTEGER("lchip", ctc_dkit_serdes_wr.lchip, argv[index + 1]);
    }
    lchip = g_ctcs_api_en? g_api_lchip : lchip;
    ctc_dkit_serdes_wr.lchip = lchip;
    ctc_greatbelt_dkit_misc_write_serdes(&ctc_dkit_serdes_wr);

    return CLI_SUCCESS;
}



CTC_CLI(cli_gb_dkit_show_bridge_header,
            cli_gb_dkit_show_bridge_header_cmd,
            "show pkthdr (ipe-fwd|(epe-hdr-adjust (index INDEX|))|cpu-bufstore|iloop-bufstore|eloop-bufstore|oam-bufstore|(bufretrieve (chanid CHAD_ID|))|start-capture-oam-engine|(oam-engine (index INDEX|))) (lchip CHIP_ID|)",
            "show cmd",
            "ipe/bsr/epe/oam packet header info",
            "ipe forward packet header",
            "epe header adjust packet header",
            "epe packet header index",
            "<0, 31>",
            "cpu to buffer store packet header",
            "iloop to buffer store packet header",
            "eloop to buffer store packet header",
            "oam to buffer store packet header",
            "buffer retrieve packet header",
            "channel id of packet header",
            "<0, 63>",
            "start capture oam-engine packet header",
            "oam engine packet header",
            "oam engine packet header index",
            "<0, 8>",
            CTC_DKITS_CHIP_DESC,
            CTC_DKITS_CHIP_ID_DESC)
{
    uint8 index = 0;
    uint8 lchip = 0;
    uint8 hdr_index = 0;
    uint8 chan_id = 0;

    index = ctc_cli_get_prefix_item(&argv[0], argc, "lchip", sal_strlen("lchip"));
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }
    lchip = g_ctcs_api_en? g_api_lchip : lchip;
    index = ctc_cli_get_prefix_item(&argv[0], argc, "ipe-fwd", sal_strlen("ipe-fwd"));
    if (0xFF != index)
    {
        _ctc_greatbelt_dkit_internal_show_ipe_fwd_bridge_header(lchip);
        _ctc_greatbelt_dkit_internal_show_ipe_fwd_exception_info(lchip);
    }

    index = ctc_cli_get_prefix_item(&argv[0], argc, "index", sal_strlen("index"));
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", hdr_index, argv[index + 1]);
    }

    index = ctc_cli_get_prefix_item(&argv[0], argc, "chan_id", sal_strlen("chan_id"));
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", chan_id, argv[index + 1]);
    }

    index = ctc_cli_get_prefix_item(&argv[0], argc, "epe-hdr-adjust", sal_strlen("epe-hdr-adjust"));
    if (0xFF != index)
    {
        _ctc_greatbelt_dkit_internal_show_epe_bridge_header(lchip, hdr_index);
    }

    index = ctc_cli_get_prefix_item(&argv[0], argc, "cpu-bufstore", sal_strlen("cpu-bufstore"));
    if (0xFF != index)
    {
        _ctc_greatbelt_dkit_internal_show_bufstore_bridge_header(lchip, CTC_DKIT_CPU_CHANID);
    }

    index = ctc_cli_get_prefix_item(&argv[0], argc, "iloop-bufstore", sal_strlen("iloop-bufstore"));
    if (0xFF != index)
    {
        _ctc_greatbelt_dkit_internal_show_bufstore_bridge_header(lchip, CTC_DKIT_I_LOOPBACK_CHANID);
    }

    index = ctc_cli_get_prefix_item(&argv[0], argc, "eloop-bufstore", sal_strlen("eloop-bufstore"));
    if (0xFF != index)
    {
        _ctc_greatbelt_dkit_internal_show_bufstore_bridge_header(lchip, CTC_DKIT_E_LOOPBACK_CHANID);
    }

    index = ctc_cli_get_prefix_item(&argv[0], argc, "bufretrieve", sal_strlen("bufretrieve"));
    if (0xFF != index)
    {
        _ctc_greatbelt_dkit_internal_show_bufretrieve_packet_header(lchip, chan_id);
    }

    index = ctc_cli_get_prefix_item(&argv[0], argc, "oam-bufstore", sal_strlen("oam-bufstore"));
    if (0xFF != index)
    {
         _ctc_greatbelt_dkit_internal_show_bufstore_bridge_header(lchip, CTC_DKIT_OAM_CHANID);
    }

    index = ctc_cli_get_prefix_item(&argv[0], argc, "oam-engine", sal_strlen("oam-engine"));
    if (0xFF != index)
    {
        _ctc_greatbelt_dkit_internal_show_oam_engine_packet_header(lchip, index);
    }

    index = ctc_cli_get_prefix_item(&argv[0], argc, "start-capture-oam-engine", sal_strlen("start-capture-oam-engine"));
    if (0xFF != index)
    {
        _ctc_greatbelt_dkit_internal_start_capture_oam_engine_packet_header(lchip);
    }

    return CLI_SUCCESS;
}


int32
ctc_greatbelt_dkit_internal_cli_init(uint8 cli_tree_mode)
{
    CTC_DKIT_PRINT_DEBUG("%s %d\n", __FUNCTION__, __LINE__);

    _ctc_greatbelt_dkit_internal_module_init(_ctc_greatbelt_dkit_internal_module_process,  cli_tree_mode, TRUE);
    _ctc_greatbelt_dkit_internal_bus_init(_ctc_greatbelt_dkit_internal_bus_process, cli_tree_mode, TRUE);
    install_element(cli_tree_mode, &cli_gb_dkit_mac_en_cmd);

    install_element(cli_tree_mode, &cli_gb_dkit_access_mode_cmd);
    install_element(cli_tree_mode, &cli_gb_dkit_pcie_read_cmd);
    install_element(cli_tree_mode, &cli_gb_dkit_pcie_write_cmd);
    install_element(cli_tree_mode, &cli_gb_dkit_serdes_read_cmd);
    install_element(cli_tree_mode, &cli_gb_dkit_serdes_write_cmd);
    install_element(cli_tree_mode, &cli_gb_dkit_show_bridge_header_cmd);

    return CLI_SUCCESS;
}

int32
ctc_greatbelt_dkit_internal_cli_deinit(uint8 cli_tree_mode)
{
    CTC_DKIT_PRINT_DEBUG("%s %d\n", __FUNCTION__, __LINE__);

    _ctc_greatbelt_dkit_internal_module_init(_ctc_greatbelt_dkit_internal_module_process,  cli_tree_mode, FALSE);
    _ctc_greatbelt_dkit_internal_bus_init(_ctc_greatbelt_dkit_internal_bus_process, cli_tree_mode, FALSE);
    uninstall_element(cli_tree_mode, &cli_gb_dkit_mac_en_cmd);

    uninstall_element(cli_tree_mode, &cli_gb_dkit_access_mode_cmd);
    uninstall_element(cli_tree_mode, &cli_gb_dkit_pcie_read_cmd);
    uninstall_element(cli_tree_mode, &cli_gb_dkit_pcie_write_cmd);
    uninstall_element(cli_tree_mode, &cli_gb_dkit_serdes_read_cmd);
    uninstall_element(cli_tree_mode, &cli_gb_dkit_serdes_write_cmd);
    uninstall_element(cli_tree_mode, &cli_gb_dkit_show_bridge_header_cmd);

    return CLI_SUCCESS;
}


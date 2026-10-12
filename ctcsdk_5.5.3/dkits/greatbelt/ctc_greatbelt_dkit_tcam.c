#include "greatbelt/include/drv_lib.h"
#include "ctc_cli.h"
#include "ctc_dkit.h"
#include "greatbelt/include/drv_ftm.h"
#include "ctc_greatbelt_dkit.h"
#include "ctc_greatbelt_dkit_drv.h"
#include "ctc_greatbelt_dkit_discard.h"
#include "ctc_greatbelt_dkit_discard_type.h"
#include "ctc_greatbelt_dkit_tcam.h"

#define CTC_DKITS_INVALID_INDEX             0xFFFFFFFF
#define CTC_DKITS_FLOW_TCAM_BLK_NUM         7
#define DKITS_TCAM_DETECT_MEM_NUM           32
#define DKITS_TCAM_BLOCK_INVALID_TYPE       0xFF
#define DRV_TIME_OUT  1000                  /* Time out setting */
#define TCAM_BIST_MAX_ENTRY 64

enum ctc_dkit_tcam_bist_type_e
{
    CTC_DKIT_TCAM_BIST_LPM,
    CTC_DKIT_TCAM_BIST_INT
};
typedef enum ctc_dkit_tcam_bist_type_e ctc_dkit_tcam_bist_type_t;

enum dkits_tcam_capture_tick_e
{
    DKITS_TCAM_CAPTURE_TICK_BEGINE,
    DKITS_TCAM_CAPTURE_TICK_TERMINATE,
    DKITS_TCAM_CAPTURE_TICK_LASTEST,
    DKITS_TCAM_CAPTURE_TICK_NUM
};
typedef enum dkits_tcam_capture_tick_e dkits_tcam_capture_tick_t;

/*TBD*/
enum dkits_tcam_module_type_e
{
    DKITS_TCAM_MODULE_TYPE_FLOW,
    DKITS_TCAM_MODULE_TYPE_IPPREFIX,
    DKITS_TCAM_MODULE_TYPE_IPNAT,
    DKITS_TCAM_MODULE_TYPE_NUM
};
typedef enum dkits_tcam_module_type_e dkits_tcam_module_type_t;

struct dkits_tcam_tbl_info_s
{
    uint32  id;
    uint32  idx;
    uint32  result_valid;
    uint32  lookup_valid;
};
typedef struct dkits_tcam_tbl_info_s dkits_tcam_tbl_info_t;

struct dkits_tcam_capture_info_s
{
    dkits_tcam_tbl_info_t tbl[32][4];
};
typedef struct dkits_tcam_capture_info_s dkits_tcam_capture_info_t;

extern int32
drv_greatbelt_chip_read(uint8 lchip, uint32 offset, uint32* p_value);
extern int32
drv_greatbelt_chip_write(uint8 lchip, uint32 offset, uint32 value);
#define ________DKITS_TCAM_INNER_FUNCTION__________

#define __0_CHIP_TCAM__

#define __2_BIST__

int32
_ctc_greatbelt_dkits_tcam_start_bist_intcam(uint32 chip_id, uint8 type, uint32 latency)
{
    uint32 key_len = 160;
    uint32 key_size = 0;
    uint32 key_num = 0;
    uint32 entry_num = 0;
    uint32 cmd = 0;
    uint32 i = 0;
    uint32 j = 0;
    tcam_ctl_int_cpu_request_mem_t request_mem;
    tcam_ctl_int_cpu_result_mem_t expect_mem;
    tcam_ctl_int_cpu_result_mem_t result_mem;
    tcam_ctl_int_cpu_wr_data_t wr_data;
    tcam_ctl_int_cpu_wr_mask_t wr_mask;
    tcam_ctl_int_cpu_access_cmd_t access_cmd;
    tcam_ctl_int_bist_ctl_t bist_ctl;
    tcam_ctl_int_init_ctrl_t init_ctl;
    tcam_ctl_int_key_size_cfg_t key_size_cfg;
    uint32 real_key[] = {0x0000, 0x1111, 0x2222};
    uint32 key_mask[] = {0xFFFFFFFF, 0xFFFF};

    sal_memset(&request_mem, 0, sizeof(tcam_ctl_int_cpu_request_mem_t));
    sal_memset(&expect_mem, 0, sizeof(tcam_ctl_int_cpu_result_mem_t));
    sal_memset(&result_mem, 0, sizeof(tcam_ctl_int_cpu_result_mem_t));
    sal_memset(&wr_data, 0, sizeof(tcam_ctl_int_cpu_wr_data_t));
    sal_memset(&wr_mask, 0, sizeof(tcam_ctl_int_cpu_wr_mask_t));
    sal_memset(&access_cmd, 0, sizeof(tcam_ctl_int_cpu_access_cmd_t));
    sal_memset(&bist_ctl, 0, sizeof(tcam_ctl_int_bist_ctl_t));
    sal_memset(&init_ctl, 0, sizeof(tcam_ctl_int_init_ctrl_t));
    sal_memset(&key_size_cfg, 0, sizeof(tcam_ctl_int_key_size_cfg_t));

    if (80 == key_len)
    {
        key_size = 0;
        key_num = 1;
        entry_num = 64;
    }
    else if (160 == key_len)
    {
        key_size = 1;
        key_num = 2;
        entry_num = 64;
    }
    else if (320 == key_len)
    {
        key_size = 2;
        key_num = 4;
        entry_num = 32;
    }
    else if (640 == key_len)
    {
        key_size = 3;
        key_num = 8;
        entry_num = 16;
    }

    /*Init TCAM and cfg keysize*/
    init_ctl.cfg_init_en = 1;
    init_ctl.cfg_init_start_addr = 0;
    init_ctl.cfg_init_end_addr = 0x1fff;
    cmd = DRV_IOW(TcamCtlIntInitCtrl_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, &init_ctl));

    key_size_cfg.key80_en = 0xFFFF;
    key_size_cfg.key160_en = 0xFF;
    key_size_cfg.key320_en = 0xF;
    key_size_cfg.key640_en = 0x3;
    cmd = DRV_IOW(TcamCtlIntKeySizeCfg_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, &key_size_cfg));

    /*Write testing key and keyMask into TCAM through Indirect accessing bus.
    And the bus accessing width is 80bits, so it need (n/80) times to complete one key accessing.*/
    for (i = 0; i < entry_num; i++)
    {
        /*keysize 160*/
        for (j = 0; j < key_num; j++)
        {
            wr_data.tcam_write_data00 = real_key[2];
            wr_data.tcam_write_data01 = real_key[1];
            wr_data.tcam_write_data02 = real_key[0] + i;

            cmd = DRV_IOW(TcamCtlIntCpuWrData_t, DRV_ENTRY_FLAG);
            DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, &wr_data));

            wr_mask.tcam_write_data10 = key_mask[1];
            wr_mask.tcam_write_data11 = key_mask[0];
            wr_mask.tcam_write_data12 = key_mask[0];
            cmd = DRV_IOW(TcamCtlIntCpuWrMask_t, DRV_ENTRY_FLAG);
            DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, &wr_mask));

            access_cmd.cpu_req = 1;
            access_cmd.cpu_req_type = 0x5;
            access_cmd.cpu_index = (i << key_size) + j;
            cmd = DRV_IOW(TcamCtlIntCpuAccessCmd_t, DRV_ENTRY_FLAG);
            DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, &access_cmd));
        }
    }

    request_mem.key_valid = 1;
    request_mem.key_size = key_size;
    request_mem.key_dual_lkup = 0;
    cmd = DRV_IOW(TcamCtlIntCpuRequestMem_t, DRV_ENTRY_FLAG);

    for (i = 0; i < entry_num; i++)
    {
        request_mem.key159_to144 = real_key[2];
        request_mem.key143_to112 = real_key[1];
        request_mem.key111_to80 = real_key[0] + i;

        request_mem.key79_to64 = real_key[2];
        request_mem.key63_to32 = real_key[1];
        request_mem.key31_to0 = real_key[0] + i;

        DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, i, cmd, &request_mem));
    }

    cmd = DRV_IOW(TcamCtlIntCpuResultMem_t, DRV_ENTRY_FLAG);

    for (i = 0; i < TCAM_CTL_INT_CPU_RESULT_MEM_MAX_INDEX; i++)
    {
        DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, i, cmd, &result_mem));
    }

    expect_mem.result_compare_valid = 1;
    expect_mem.index_valid = 1;
    expect_mem.index_acl0_comp_en = 1; /*just for single lookup*/
    cmd = DRV_IOW(TcamCtlIntCpuResultMem_t, DRV_ENTRY_FLAG);

    for (i = 0; i < entry_num; i++)
    {
        expect_mem.index_acl0 = i << key_size;
        DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, i, cmd, &expect_mem));
    }

    bist_ctl.cfg_bist_en = 1;
    bist_ctl.cfg_stop_on_error = 0;
    bist_ctl.cfg_bist_once = 1;
    bist_ctl.cfg_bist_entries = 63; /*the max value is 63*/
    cmd = DRV_IOW(TcamCtlIntBistCtl_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, &bist_ctl));

    return DRV_E_NONE;
}


int32
_ctc_greatbelt_dkits_tcam_check_bist_tcam(uint32 chip_id, uint8 type, uint32 latency)
{
    uint32 cmd = 0;
    uint32 i = 0;
    uint32 mis_match_count = 0;
    tcam_ctl_int_cpu_result_mem_t result_mem;
    tcam_ctl_int_bist_ctl_t bist_ctl;
    tcam_ctl_int_init_ctrl_t init_ctl;

    sal_memset(&result_mem, 0, sizeof(tcam_ctl_int_cpu_result_mem_t));
    sal_memset(&bist_ctl, 0, sizeof(tcam_ctl_int_bist_ctl_t));
    sal_memset(&init_ctl, 0, sizeof(tcam_ctl_int_init_ctrl_t));

    if (CTC_DKIT_TCAM_BIST_LPM == type)
    {
        /* TBD */
    }
    else if (CTC_DKIT_TCAM_BIST_INT == type)
    {
        cmd = DRV_IOR(TcamCtlIntBistCtl_t, TcamCtlIntBistCtl_CfgBistMismatchCount_f);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, &mis_match_count));
    }
    else
    {
        ctc_cli_out("Not Tcam Type!\n");
    }

    if (0 == mis_match_count)
    {
        ctc_cli_out("\nTcam Scan Result:\n");
        ctc_cli_out("--------------------------\n");
        ctc_cli_out("  Result pass!\n");
    }
    else
    {
        ctc_cli_out("\nTcam Scan Result:\n");
        ctc_cli_out("--------------------------\n");
        ctc_cli_out("  Result failed!\n");

        ctc_cli_out("  Total mismatch count: %d\n", mis_match_count);

        cmd = DRV_IOR(TcamCtlIntCpuResultMem_t, DRV_ENTRY_FLAG);

        for (i = 0; i < TCAM_CTL_INT_CPU_REQUEST_MEM_MAX_INDEX; i++)
        {
            DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, TCAM_CTL_INT_CPU_REQUEST_MEM_MAX_INDEX + i, cmd, &result_mem));
            if (result_mem.index_acl0_comp_en)
            {
                ctc_cli_out("   index:%d mismatch\n", result_mem.index_acl0);
            }
        }
    }

    /*Disable bist*/
    cmd = DRV_IOW(TcamCtlIntBistCtl_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, &bist_ctl));
    /*Clear tcam initctl*/
    cmd = DRV_IOW(TcamCtlIntInitCtrl_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, &init_ctl));

    return DRV_E_NONE;
}

#define __3_CAPTURE__

STATIC int32
_ctc_greatbelt_dkits_tcam_capture_timestamp(dkits_tcam_module_type_t module, dkits_tcam_capture_tick_t tick, sal_time_t* p_stamp)
{
    static sal_time_t flow_time;
    static sal_time_t ip_time;
    static sal_time_t nat_time;

    if (DKITS_TCAM_CAPTURE_TICK_BEGINE == tick)
    {
        if (DKITS_TCAM_MODULE_TYPE_FLOW == module)
        {
            sal_time(&flow_time);
        }
        else if (DKITS_TCAM_MODULE_TYPE_IPPREFIX == module)
        {
            sal_time(&ip_time);;
        }
        else if (DKITS_TCAM_MODULE_TYPE_IPNAT == module)
        {
            sal_time(&nat_time);;
        }
    }
    else if (DKITS_TCAM_CAPTURE_TICK_TERMINATE == tick)
    {
        sal_time(p_stamp);;
    }
    else if (DKITS_TCAM_CAPTURE_TICK_LASTEST == tick)
    {
        if (DKITS_TCAM_MODULE_TYPE_FLOW == module)
        {
            sal_memcpy((uint8*)p_stamp, (uint8*)&flow_time, sizeof(sal_time_t));
        }
        else if (DKITS_TCAM_MODULE_TYPE_IPPREFIX == module)
        {
            sal_memcpy((uint8*)p_stamp, (uint8*)&ip_time, sizeof(sal_time_t));
        }
        else if (DKITS_TCAM_MODULE_TYPE_IPNAT == module)
        {
            sal_memcpy((uint8*)p_stamp, (uint8*)&nat_time, sizeof(sal_time_t));
        }
    }

    return  CLI_SUCCESS;
}

STATIC int32
_ctc_greatbelt_dkits_tcam_capture_explore_result(dkits_tcam_module_type_t tcam_module, uint32 idx)
{
    uint32 i = 0;
    dkits_tcam_capture_info_t* capture_info = NULL;
    uint8 index = 0;
    uint8 flag = 0;
    uint32 value[48] = {0};
    uint32 result[2] = {0};
    uint32 lkpIdx[4] = {0};
    uint8 keySize = 0, entryNum = 0, entryNo = 0;
    uint32 tcam_bistreqram = 0, tcam_bistrltram = 0;

    tcam_module = tcam_module;
    idx = idx;
    tcam_bistreqram = TCAM_CTL_INT_CPU_REQUEST_MEM_OFFSET;

    capture_info = (dkits_tcam_capture_info_t*)sal_malloc(sizeof(dkits_tcam_capture_info_t));
    if(NULL == capture_info)
    {
        return CLI_ERROR;
    }
    sal_memset(capture_info, 0, sizeof(dkits_tcam_capture_info_t));

    /* here need to attention : TcamCtlIntCpuResultMem, 0-63 for bist, and 64-127 for capture */
    tcam_bistrltram = TCAM_CTL_INT_CPU_RESULT_MEM_OFFSET+ TCAM_BIST_MAX_ENTRY * TCAM_CTL_INT_CPU_RESULT_MEM_ENTRY_SIZE;

    while (index < TCAM_BIST_MAX_ENTRY)
    {
        /*read the key information from request ram*/
        for (i = 0; i < 7; i++)
        {
            drv_greatbelt_chip_read(0, tcam_bistreqram + index * TCAM_CTL_INT_CPU_REQUEST_MEM_ENTRY_SIZE + i * 4, &value[i]);
        }

        /*check key is valid */
        if ((value[0] & 0x80000000) == 0)
        {
            index++;
            flag++;
            continue;
        }

        /*
        Size of looup key:
            2'b00: 80 bits
            2'b01:160 bits
            2'b10: 320 bits
            2'b11: 640 bits
        */
        keySize = (value[0] & 0x00000300) >> 8;
        if (keySize <= 1)
        {
            entryNum = 1;
        }
        else if (keySize <= 2)
        {
            entryNum = 2;
        }
        else
        {
            entryNum = 4;
        }

        index++;

        for (entryNo = 1; entryNo < entryNum; entryNo++)
        {
            for (i = 0; i < 7; i++)
            {
                drv_greatbelt_chip_read(0, tcam_bistreqram + index * TCAM_CTL_INT_CPU_REQUEST_MEM_ENTRY_SIZE + i * 4, &value[entryNo * 8 + i]);
            }

            if ((value[entryNo * 8] & 0x80000000) == 0)
            {
                ctc_cli_out("\ncapture tcam bist req ram keyinvalid! index=%d, entryNo=%d\n", index, entryNo);
            }

            index++;
        }

        drv_greatbelt_chip_read(0, tcam_bistrltram + (index - 1) * 8, &result[0]);
        drv_greatbelt_chip_read(0, tcam_bistrltram + (index - 1) * 8 + 4, &result[1]);
        if (((result[0] & 0x40000000) != 0) && ((result[0] & 0x80000000) != 0)) /* indexValid = 1 && resultCompareValid = 1*/
        {
            if ((result[0] & 0x20000000) != 0) /* indexAcl0CompEn = 1 */
            {
                lkpIdx[0] = (result[0] >> 16) & 0x00001fff;
            }

            if ((result[0] & 0x2000) != 0)     /* indexAcl1CompEn = 1 */
            {
                lkpIdx[1] = result[0] & 0x00001fff;
            }

            if ((result[1] & 0x20000000) != 0) /* indexAcl2CompEn = 1 */
            {
                lkpIdx[2] = (result[1] >> 16) & 0x00001fff;
            }

            if ((result[1] & 0x2000) != 0)     /* indexAcl3CompEn = 1 */
            {
                lkpIdx[3] = result[1] & 0x00001fff;
            }
        }

        if (keySize == 0)
        {
            ctc_cli_out("entry #%d: tcamkey:%04x/%08x/%08x\nkeyCmd: 0x%x index0: 0x%x, index1:0x%x, index2:0x%x, index3:0x%x\n",
                       index - 1, value[4] & 0xffff, value[5], value[6], value[0] & 0xff, lkpIdx[0], lkpIdx[1], lkpIdx[2], lkpIdx[3]);
        }
        else if (keySize == 1)
        {
            ctc_cli_out("entry #%d: tcamkey:%04x/%08x/%08x//%04x/%08x/%08x\nkeyCmd: 0x%x index0: 0x%x, index1:0x%x, index2:0x%x, index3:0x%x\n",
                       index - 1, value[1] & 0xffff, value[2], value[3], value[4] & 0xffff, value[5], value[6], value[0] & 0xff, lkpIdx[0], lkpIdx[1], lkpIdx[2], lkpIdx[3]);
        }
        else if (keySize == 2)
        {
            ctc_cli_out("entry #%d: tcamkey:%04x/%08x/%08x//%04x/%08x/%08x//%04x/%08x/%08x//%04x/%08x/%08x\n",
                       index - 1, value[1] & 0xffff, value[2], value[3], value[4] & 0xffff, value[5], value[6],
                       value[10] & 0xffff, value[11], value[12], value[13] & 0xffff, value[14], value[15]);
            ctc_cli_out("keyCmd: 0x%x index0: 0x%x, index1:0x%x, index2:0x%x, index3:0x%x\n", value[0] & 0xff, lkpIdx[0], lkpIdx[1], lkpIdx[2], lkpIdx[3]);
        }
        else
        {
            ctc_cli_out("entry #%d: tcamkey:%04x/%08x/%08x//%04x/%08x/%08x//%04x/%08x/%08x//%04x/%08x/%08x\n",
                       index - 1, value[1] & 0xffff, value[2], value[3], value[4] & 0xffff, value[5], value[6],
                       value[10] & 0xffff, value[11], value[12], value[13] & 0xffff, value[14], value[15]);
            ctc_cli_out("        %04x/%08x/%08x//%04x/%08x/%08x//%04x/%08x/%08x//%04x/%08x/%08x\n",
                       value[19] & 0xffff, value[20], value[21], value[22] & 0xffff, value[23], value[24],
                       value[28] & 0xffff, value[29], value[30], value[31] & 0xffff, value[32], value[33]);
            ctc_cli_out("keyCmd: 0x%x index0: 0x%x, index1:0x%x, index2:0x%x, index3:0x%x\n", value[0] & 0xff, lkpIdx[0], lkpIdx[1], lkpIdx[2], lkpIdx[3]);
        }
    }

    if(NULL != capture_info)
    {
        sal_free(capture_info);
    }

    return CLI_SUCCESS;
}

#define ________DKITS_TCAM_EXTERNAL_FUNCTION__________
int32
ctc_greatbelt_dkits_tcam_scan(void* p_info)
{
    uint8 chip_id = 0;
 /*    uint8 index = 0;*/

    _ctc_greatbelt_dkits_tcam_start_bist_intcam(chip_id, CTC_DKIT_TCAM_BIST_INT, 0);

    _ctc_greatbelt_dkits_tcam_check_bist_tcam(chip_id, CTC_DKIT_TCAM_BIST_INT, 0);

    return CLI_SUCCESS;
}

/*Only support int tcam capture*/
int32
ctc_greatbelt_dkits_tcam_capture_start(void* p_info)
{
    uint32 bistContrl;
    uint32 captureEn, captureOnce, bistEntry;

    if (0 != SDK_WORK_PLATFORM)
    {
        return 0;
    }

    captureEn = 1;
    captureOnce = 0;
    bistEntry = TCAM_BIST_MAX_ENTRY - 1;

    DKITS_PTR_VALID_CHECK(p_info);

 /*    _ctc_greatbelt_dkits_tcam_reset_detect();*/

    bistContrl = (captureEn << 12) + (captureOnce << 11) + bistEntry;
    drv_greatbelt_chip_write(0, TCAM_CTL_INT_BIST_CTL_OFFSET, bistContrl);

    _ctc_greatbelt_dkits_tcam_capture_timestamp(DKITS_TCAM_MODULE_TYPE_FLOW, DKITS_TCAM_CAPTURE_TICK_BEGINE, NULL);

    return CLI_SUCCESS;
}

int32
ctc_greatbelt_dkits_tcam_capture_stop(void* p_info)
{
    uint32 tcam_module = 0;
    sal_time_t start, now;
    ctc_dkits_tcam_info_t* p_tcam_info = NULL;
    char* p_str1 = NULL;
    char* p_str2 = NULL;
    char* p_tmp = NULL;

    if (0 != SDK_WORK_PLATFORM)
    {
        return 0;
    }

    DKITS_PTR_VALID_CHECK(p_info);
    p_tcam_info = (ctc_dkits_tcam_info_t*)p_info;

    drv_greatbelt_chip_write(0, TCAM_CTL_INT_BIST_CTL_OFFSET, 0);

    if ((DKITS_FLAG_ISSET(p_tcam_info->tcam_type, CTC_DKITS_TCAM_TYPE_FLAG_IGS_SCL))
       || (DKITS_FLAG_ISSET(p_tcam_info->tcam_type, CTC_DKITS_TCAM_TYPE_FLAG_IGS_ACL))
       || (DKITS_FLAG_ISSET(p_tcam_info->tcam_type, CTC_DKITS_TCAM_TYPE_FLAG_EGS_ACL)))
    {
        _ctc_greatbelt_dkits_tcam_capture_timestamp(DKITS_TCAM_MODULE_TYPE_FLOW, DKITS_TCAM_CAPTURE_TICK_LASTEST, &start);
        sal_time(&now);
        p_str1 = sal_ctime(&start);
        p_str2 = sal_ctime(&now);
        p_tmp = sal_strchr(p_str1, '\n');
        if (NULL != p_tmp)
        {
            *p_tmp = '\0';
        }
        p_tmp = sal_strchr(p_str2, '\n');
        if (NULL != p_tmp)
        {
            *p_tmp = '\0';
        }
        CTC_DKIT_PRINT("SCL/ACL tcam start capture timestamp %s, stop capture timestamp %s.\n", p_str1, p_str2);
        DKITS_BIT_SET(tcam_module, DKITS_TCAM_MODULE_TYPE_FLOW);
    }

    if (DKITS_FLAG_ISSET(p_tcam_info->tcam_type, CTC_DKITS_TCAM_TYPE_FLAG_IPPREFIX))
    {
        _ctc_greatbelt_dkits_tcam_capture_timestamp(DKITS_TCAM_MODULE_TYPE_IPPREFIX, DKITS_TCAM_CAPTURE_TICK_LASTEST, &start);
        sal_time(&now);
        p_str1 = sal_ctime(&start);
        p_str2 = sal_ctime(&now);
        p_tmp = sal_strchr(p_str1, '\n');
        if (NULL != p_tmp)
        {
            *p_tmp = '\0';
        }
        p_tmp = sal_strchr(p_str2, '\n');
        if (NULL != p_tmp)
        {
            *p_tmp = '\0';
        }
        CTC_DKIT_PRINT("LPM IP tcam start capture timestamp %s, stop capture timestamp %s.\n", p_str1, p_str2);
        DKITS_BIT_SET(tcam_module, DKITS_TCAM_MODULE_TYPE_IPPREFIX);
    }

    if (DKITS_FLAG_ISSET(p_tcam_info->tcam_type, CTC_DKITS_TCAM_TYPE_FLAG_IPNAT))
    {
        _ctc_greatbelt_dkits_tcam_capture_timestamp(DKITS_TCAM_MODULE_TYPE_IPNAT, DKITS_TCAM_CAPTURE_TICK_LASTEST, &start);
        sal_time(&now);
        p_str1 = sal_ctime(&start);
        p_str2 = sal_ctime(&now);
        p_tmp = sal_strchr(p_str1, '\n');
        if (NULL != p_tmp)
        {
            *p_tmp = '\0';
        }
        p_tmp = sal_strchr(p_str2, '\n');
        if (NULL != p_tmp)
        {
            *p_tmp = '\0';
        }
        CTC_DKIT_PRINT("LPM NAT//PBR tcam start capture timestamp %s, stop capture timestamp %s.\n", p_str1, p_str2);
        DKITS_BIT_SET(tcam_module, DKITS_TCAM_MODULE_TYPE_IPNAT);
    }
    CTC_DKIT_PRINT("\n");

    _ctc_greatbelt_dkits_tcam_capture_explore_result(tcam_module, 0xFFFFFFFF);

    return CLI_SUCCESS;
}

int32
ctc_greatbelt_dkits_tcam_capture_show(void* p_info)
{
    uint32 tcam_module = 0;
     /*uint32 capture_en = 0;*/
     /*uint32 capture_vec = 0;*/
     /*uint32 expect_vec = 0;*/
    ctc_dkits_tcam_info_t* p_tcam_info;
     /*char* flow_tcam_interface[] = {"igs-scl", "igs-acl", "egs-acl"};*/

    if (0 != SDK_WORK_PLATFORM)
    {
        return 0;
    }

    DKITS_PTR_VALID_CHECK(p_info);
    p_tcam_info = (ctc_dkits_tcam_info_t*)p_info;

    if ((DKITS_FLAG_ISSET(p_tcam_info->tcam_type, CTC_DKITS_TCAM_TYPE_FLAG_IGS_SCL))
       || (DKITS_FLAG_ISSET(p_tcam_info->tcam_type, CTC_DKITS_TCAM_TYPE_FLAG_IGS_ACL))
       || (DKITS_FLAG_ISSET(p_tcam_info->tcam_type, CTC_DKITS_TCAM_TYPE_FLAG_EGS_ACL)))
    {
        DKITS_BIT_SET(tcam_module, DKITS_TCAM_MODULE_TYPE_FLOW);
    }

    if (DKITS_FLAG_ISSET(p_tcam_info->tcam_type, CTC_DKITS_TCAM_TYPE_FLAG_IPPREFIX))
    {
        DKITS_BIT_SET(tcam_module, DKITS_TCAM_MODULE_TYPE_IPPREFIX);
    }

    if (DKITS_FLAG_ISSET(p_tcam_info->tcam_type, CTC_DKITS_TCAM_TYPE_FLAG_IPNAT))
    {
        DKITS_BIT_SET(tcam_module, DKITS_TCAM_MODULE_TYPE_IPNAT);
    }

    _ctc_greatbelt_dkits_tcam_capture_explore_result(tcam_module, p_tcam_info->index);

    return CLI_SUCCESS;
}

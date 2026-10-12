#include "goldengate/include/drv_lib.h"
#include "ctc_cli.h"
#include "ctc_dkit.h"
#include "goldengate/include/drv_ftm.h"
#include "ctc_goldengate_dkit.h"
#include "ctc_goldengate_dkit_drv.h"
#include "ctc_goldengate_dkit_discard.h"
#include "ctc_goldengate_dkit_discard_type.h"
#include "ctc_goldengate_dkit_path.h"
#include "ctc_goldengate_dkit_captured_info.h"
#include "ctc_goldengate_dkit_tcam.h"

#define CTC_DKITS_INVALID_INDEX             0xFFFFFFFF
#define CTC_DKITS_FLOW_TCAM_BLK_NUM         7
#define DKITS_TCAM_DETECT_MEM_NUM           1
#define DKITS_TCAM_BLOCK_INVALID_TYPE       0xFF
#define DRV_TIME_OUT  1000                  /* Time out setting */
#define CTC_DKITS_TCAM_TYPE_FLAG_NUM        5

struct ctc_dkits_tcam_interface_status_s
{
    uint8 bist_request_done;
    uint8 bist_result_done;
    uint8 bist_mismatch_count;
};
typedef struct ctc_dkits_tcam_interface_status_s ctc_dkits_tcam_interface_status_t;

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

enum dkits_flow_tcam_lookup_type_e
{
    DKITS_FLOW_TCAM_LOOKUP_TYPE_L2KEY,
    DKITS_FLOW_TCAM_LOOKUP_TYPE_L2L3KEY,
    DKITS_FLOW_TCAM_LOOKUP_TYPE_L3KEY,
    DKITS_FLOW_TCAM_LOOKUP_TYPE_VLANKEY
};
typedef enum dkits_flow_tcam_lookup_type_e dkits_flow_tcam_lookup_type_t;

enum dkits_lpm_tcam_key_size_type_e
{
    DKITS_LPM_TCAM_KEY_SIZE_TYPE_HALF,
    DKITS_LPM_TCAM_KEY_SIZE_TYPE_SINGLE,
    DKITS_LPM_TCAM_KEY_SIZE_TYPE_DOUBLE,
    DKITS_LPM_TCAM_KEY_SIZE_TYPE_NUM
};
typedef enum dkits_lpm_tcam_key_size_type_e dkits_lpm_tcam_key_size_type_t;

enum dkits_lpm_ip_key_type_e
{
    DKITS_LPM_IP_KEY_TYPE_IPV4,
    DKITS_LPM_IP_KEY_TYPE_IPV6,
    DKITS_LPM_IP_KEY_TYPE_NUM
};
typedef enum dkits_lpm_ip_key_type_e dkits_lpm_ip_key_type_t;

enum dkits_lpm_nat_key_type_e
{
    DKITS_LPM_NAT_KEY_TYPE_IPV4PBR,
    DKITS_LPM_NAT_KEY_TYPE_IPV6PBR,
    DKITS_LPM_NAT_KEY_TYPE_IPV4NAT,
    DKITS_LPM_NAT_KEY_TYPE_IPV6NAT,
    DKITS_LPM_NAT_KEY_TYPE_NUM
};
typedef enum dkits_lpm_nat_key_type_e dkits_lpm_nat_key_type_t;

enum dkits_tcam_capture_tick_e
{
    DKITS_TCAM_CAPTURE_TICK_BEGINE,
    DKITS_TCAM_CAPTURE_TICK_TERMINATE,
    DKITS_TCAM_CAPTURE_TICK_LASTEST,
    DKITS_TCAM_CAPTURE_TICK_NUM
};
typedef enum dkits_tcam_capture_tick_e dkits_tcam_capture_tick_t;

enum dkits_tcam_module_type_e
{
    DKITS_TCAM_MODULE_TYPE_FLOW,
    DKITS_TCAM_MODULE_TYPE_IPPREFIX,
    DKITS_TCAM_MODULE_TYPE_IPNAT,
    DKITS_TCAM_MODULE_TYPE_NUM
};
typedef enum dkits_tcam_module_type_e dkits_tcam_module_type_t;

enum dkits_tcam_flow_interface_e
{
    DKITS_TCAM_FLOW_INTERFACE_USERID,
    DKITS_TCAM_FLOW_INTERFACE_INGACL,
    DKITS_TCAM_FLOW_INTERFACE_EGRACL,
    DKITS_TCAM_FLOW_INTERFACE_NUM
};
typedef enum dkits_tcam_flow_interface_e dkits_tcam_flow_interface_t;

enum dkits_tcam_capture_tbl_property_e
{
    DKITS_TCAM_CAPTURE_TBL_CAPTURE_EN,
    DKITS_TCAM_CAPTURE_TBL_CAPTURE_ONCE,
    DKITS_TCAM_CAPTURE_TBL_CAPTURE_VEC,
    DKITS_TCAM_CAPTURE_TBL_STOP_ON_ERROR,
};
typedef enum dkits_tcam_capture_tbl_property_e dkits_tcam_capture_tbl_property_t;

enum dkits_tcam_scan_info_type_e
{
    DKITS_TCAM_SCAN_INFO_TYPE_BRIEF,   /* scan specified tcam type */
    DKITS_TCAM_SCAN_INFO_TYPE_GENERAL, /* scan specified tcam tbl based on tcam type and key type */
    DKITS_TCAM_SCAN_INFO_TYPE_DETAIL,  /* scan specfiied tcam tbl and tbl index */
    DKITS_TCAM_SCAN_INFO_TYPE_NUM
};
typedef enum dkits_tcam_scan_info_type_e dkits_tcam_scan_info_type_t;

/* Tcam memory type */
enum ctc_dkit_tcam_mem_type_e
{
    CTC_DKIT_DRV_INT_TCAM_RECORD_DATA,
    CTC_DKIT_DRV_INT_TCAM_RECORD_MASK,
    CTC_DKIT_DRV_INT_TCAM_RECORD_REG,
    CTC_DKIT_DRV_INT_LPM_TCAM_RECORD_DATA,
    CTC_DKIT_DRV_INT_LPM_TCAM_RECORD_MASK,
    CTC_DKIT_DRV_INT_LPM_TCAM_RECORD_REG
};
typedef enum ctc_dkit_tcam_mem_type_e ctc_dkit_tcam_mem_type_t;

enum ctc_dkit_flow_tcam_operation_type_e
{
    CTC_DKIT_FLOW_TCAM_CPU_ACCESS_REQ_READ_X,
    CTC_DKIT_FLOW_TCAM_CPU_ACCESS_REQ_READ_Y,
    CTC_DKIT_FLOW_TCAM_CPU_ACCESS_REQ_WRITE_DATA_MASK,
    CTC_DKIT_FLOW_TCAM_CPU_ACCESS_REQ_INVALID_ENTRY
};
typedef enum ctc_dkit_flow_tcam_operation_type_e ctc_dkit_flow_tcam_operation_type_t;

enum ctc_dkit_fib_tcam_operation_type_e
{
    CTC_DKIT_FIB_TCAM_CPU_ACCESS_REQ_READ_X,
    CTC_DKIT_FIB_TCAM_CPU_ACCESS_REQ_READ_Y,
    CTC_DKIT_FIB_TCAM_CPU_ACCESS_REQ_WRITE_DATA_MASK,
    CTC_DKIT_FIB_TCAM_CPU_ACCESS_REQ_INVALID_ENTRY
};
typedef enum ctc_dkit_fib_tcam_operation_type_e ctc_dkit_fib_tcam_operation_type_t;

char* gg_tcam_module_desc[DKITS_TCAM_BLOCK_TYPE_NUM] = {"Ingress ACL0", "Ingress ACL1",
                                                     "Ingress ACL2", "Ingress ACL3",
                                                     "Egress ACL",   "Ingress SCL0",
                                                     "Ingress SCL1", "IP LPM/PREFIX",
                                                     "IP PBR/NAT"};

#define ________DKITS_TCAM_INNER_FUNCTION__________

#define __0_CHIP_TCAM__

/**
 @brief Real embeded tcam read operation I/O
*/
STATIC int32
_ctc_goldengate_dkits_tcam_read_flow_tcam_entry(uint8 chip_id, uint32 blknum, uint32 index,
                                     uint32* data, ctc_dkit_tcam_mem_type_t type)
{
    uint32 flow_tcam_cpu_req_ctl[FLOW_TCAM_CPU_REQ_CTL_BYTES/4] = {0};
    uint32 cmd;
    int32 ret = CLI_SUCCESS;
    uint32 cpu_req = 0, cpu_req_type = 0, cpu_index = 0, block_num = 0;
    uint32 cpu_read_data_valid = 0, time_out = 0;

    DRV_PTR_VALID_CHECK(data);

    if (CTC_DKIT_DRV_INT_TCAM_RECORD_MASK == type)
    {
        cpu_req_type = CTC_DKIT_FLOW_TCAM_CPU_ACCESS_REQ_READ_Y;
    }
    else if (CTC_DKIT_DRV_INT_TCAM_RECORD_DATA == type)
    {
        cpu_req_type = CTC_DKIT_FLOW_TCAM_CPU_ACCESS_REQ_READ_X;
    }
    else
    {
        CTC_DKIT_PRINT("\nEmbeded Tcam Memory Type is useless!\n");
        return DRV_E_INVALID_TCAM_TYPE;
    }
    cpu_req = TRUE;
    cpu_index = index;
    block_num = blknum;

    DRV_IOW_FIELD(FlowTcamCpuReqCtl_t, FlowTcamCpuReqCtl_tcamCpuReq_f,
                          &cpu_req, flow_tcam_cpu_req_ctl);
    DRV_IOW_FIELD(FlowTcamCpuReqCtl_t, FlowTcamCpuReqCtl_tcamCpuReqType_f,
                          &cpu_req_type, flow_tcam_cpu_req_ctl);
    DRV_IOW_FIELD(FlowTcamCpuReqCtl_t, FlowTcamCpuReqCtl_tcamCpuIndex_f,
                          &cpu_index, flow_tcam_cpu_req_ctl);
    DRV_IOW_FIELD(FlowTcamCpuReqCtl_t, FlowTcamCpuReqCtl_tcamCpuBlkNum_f,
                          &block_num, flow_tcam_cpu_req_ctl);

    cmd = DRV_IOW(FlowTcamCpuReqCtl_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(chip_id, 0, cmd, flow_tcam_cpu_req_ctl);
     /*CTC_DKIT_PRINT("chipid = %u\n", chip_id);*/

    if (ret < CLI_SUCCESS)
    {
        return ret;
    }

    while (TRUE)
    {
        cmd = DRV_IOR(FlowTcamCpuReqCtl_t, FlowTcamCpuReqCtl_tcamCpuReadDataValid_f);
        ret = DRV_IOCTL(chip_id, 0, cmd, &cpu_read_data_valid);
        if (ret < CLI_SUCCESS)
        {
            return ret;
        }

        if (cpu_read_data_valid)
        {
            break;
        }

        if ((time_out ++) > DRV_TIME_OUT)
        {
            return DRV_E_TIME_OUT;
        }
    }

    cmd = DRV_IOR(FlowTcamCpuRdData_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(chip_id, 0, cmd, data);
    if (ret < CLI_SUCCESS)
    {
        return ret;
    }

    cpu_read_data_valid = FALSE;

    cmd = DRV_IOW(FlowTcamCpuReqCtl_t, FlowTcamCpuReqCtl_tcamCpuReadDataValid_f);
    ret = DRV_IOCTL(chip_id, 0, cmd, &cpu_read_data_valid);
    if (ret < CLI_SUCCESS)
    {
        return ret;
    }

    return CLI_SUCCESS;
}

/**
 @brief Real embeded LPM tcam read operation I/O
*/
STATIC int32
_ctc_goldengate_dkit_tcam_read_lpm_tcam_ip_entry(uint8 chip_id, uint32 index,
                                      uint32* data, ctc_dkit_tcam_mem_type_t type)
{
    uint32 lpm_tcam_ip_cpu_req_ctl[LPM_TCAM_IP_CPU_REQ_CTL_BYTES/4] = {0};
    uint32 cmd;
    int32 ret = CLI_SUCCESS;
    uint32 cpu_req = 0, cpu_req_type = 0, cpu_index = 0;
    uint32 cpu_read_data_valid = 0, time_out = 0;

    DRV_PTR_VALID_CHECK(data);

    cpu_index = index;
    cpu_req = TRUE;
    if (CTC_DKIT_DRV_INT_LPM_TCAM_RECORD_MASK == type)
    {
        cpu_req_type = CTC_DKIT_FIB_TCAM_CPU_ACCESS_REQ_READ_Y;
    }
    else if (CTC_DKIT_DRV_INT_LPM_TCAM_RECORD_DATA == type)
    {
        cpu_req_type = CTC_DKIT_FIB_TCAM_CPU_ACCESS_REQ_READ_X;
    }
    else
    {
        CTC_DKIT_PRINT("\nLPM Embeded Tcam Memory Type is useless!\n");
        return DRV_E_INVALID_TCAM_TYPE;
    }

    DRV_IOW_FIELD(LpmTcamIpCpuReqCtl_t, LpmTcamIpCpuReqCtl_tcamIpCpuReq_f,
                          &cpu_req, lpm_tcam_ip_cpu_req_ctl);
    DRV_IOW_FIELD(LpmTcamIpCpuReqCtl_t, LpmTcamIpCpuReqCtl_tcamIpCpuReqType_f,
                          &cpu_req_type, lpm_tcam_ip_cpu_req_ctl);
    DRV_IOW_FIELD(LpmTcamIpCpuReqCtl_t, LpmTcamIpCpuReqCtl_tcamIpCpuIndex_f,
                          &cpu_index, lpm_tcam_ip_cpu_req_ctl);
    cmd = DRV_IOW(LpmTcamIpCpuReqCtl_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(chip_id, 0, cmd, lpm_tcam_ip_cpu_req_ctl);

    if (ret < CLI_SUCCESS)
    {
        return ret;
    }

    while (TRUE)
    {
        cmd = DRV_IOR(LpmTcamIpCpuReqCtl_t, LpmTcamIpCpuReqCtl_tcamIpCpuReadDataValid_f);
        ret = DRV_IOCTL(chip_id, 0, cmd, &cpu_read_data_valid);
        if (ret < CLI_SUCCESS)
        {
            return ret;
        }

        if (cpu_read_data_valid)
        {
            break;
        }

        if ((time_out ++) > DRV_TIME_OUT)
        {
            return DRV_E_TIME_OUT;
        }

    }

    cmd = DRV_IOR(LpmTcamIpCpuRdData_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(chip_id, 0, cmd, data);
    if (ret < CLI_SUCCESS)
    {
        return ret;
    }

    cpu_read_data_valid = FALSE;

    cmd = DRV_IOW(LpmTcamIpCpuReqCtl_t, LpmTcamIpCpuReqCtl_tcamIpCpuReadDataValid_f);
    ret = DRV_IOCTL(chip_id, 0, cmd, &cpu_read_data_valid);
    if (ret < CLI_SUCCESS)
    {
        return ret;
    }

    return CLI_SUCCESS;
}

/**
 @brief Real embeded LPM tcam read operation I/O
*/
STATIC int32
_ctc_goldengate_dkit_tcam_read_lpm_tcam_nat_entry(uint8 chip_id, uint32 index,
                                       uint32* data, ctc_dkit_tcam_mem_type_t type)
{
    uint32 lpm_tcam_nat_cpu_req_ctl[LPM_TCAM_NAT_CPU_REQ_CTL_BYTES/4] = {0};
    uint32 cmd;
    int32 ret = CLI_SUCCESS;
    uint32 cpu_req = 0, cpu_req_type = 0, cpu_index = 0;
    uint32 cpu_read_data_valid = 0, time_out = 0;

    DRV_PTR_VALID_CHECK(data);

    cpu_index = index;
    cpu_req = TRUE;
    if (CTC_DKIT_DRV_INT_LPM_TCAM_RECORD_MASK == type)
    {
        cpu_req_type = CTC_DKIT_FIB_TCAM_CPU_ACCESS_REQ_READ_Y;
    }
    else if (CTC_DKIT_DRV_INT_LPM_TCAM_RECORD_DATA == type)
    {
        cpu_req_type = CTC_DKIT_FIB_TCAM_CPU_ACCESS_REQ_READ_X;
    }
    else
    {
        CTC_DKIT_PRINT("\nLPM Embeded Tcam Memory Type is useless!\n");
        return DRV_E_INVALID_TCAM_TYPE;
    }

    DRV_IOW_FIELD(LpmTcamNatCpuReqCtl2_t, LpmTcamNatCpuReqCtl2_tcamNatCpuReq_f,
                          &cpu_req, lpm_tcam_nat_cpu_req_ctl);
    DRV_IOW_FIELD(LpmTcamNatCpuReqCtl2_t, LpmTcamNatCpuReqCtl2_tcamNatCpuReqType_f,
                          &cpu_req_type, lpm_tcam_nat_cpu_req_ctl);
    DRV_IOW_FIELD(LpmTcamNatCpuReqCtl2_t, LpmTcamNatCpuReqCtl2_tcamNatCpuIndex_f,
                          &cpu_index, lpm_tcam_nat_cpu_req_ctl);

    cmd = DRV_IOW(LpmTcamNatCpuReqCtl2_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(chip_id, 0, cmd, lpm_tcam_nat_cpu_req_ctl);
    if (ret < CLI_SUCCESS)
    {
        return ret;
    }

    while (TRUE)
    {
        cmd = DRV_IOR(LpmTcamNatCpuReqCtl2_t, LpmTcamNatCpuReqCtl2_tcamNatCpuReadDataValid_f);
        ret = DRV_IOCTL(chip_id, 0, cmd, &cpu_read_data_valid);
        if (ret < CLI_SUCCESS)
        {
            return ret;
        }

        if (cpu_read_data_valid)
        {
            break;
        }

        if ((time_out ++) > DRV_TIME_OUT)
        {
            return DRV_E_TIME_OUT;
        }
    }

    cmd = DRV_IOR(LpmTcamNatCpuRdData2_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(chip_id, 0, cmd, data);
    if (ret < CLI_SUCCESS)
    {
        return ret;
    }

    cpu_read_data_valid = FALSE;

    cmd = DRV_IOW(LpmTcamNatCpuReqCtl2_t, LpmTcamNatCpuReqCtl2_tcamNatCpuReadDataValid_f);
    ret = DRV_IOCTL(chip_id, 0, cmd, &cpu_read_data_valid);
    if (ret < CLI_SUCCESS)
    {
        return ret;
    }

    return CLI_SUCCESS;
}

/**
 @brief convert embeded tcam content from X/Y format to data/mask format
*/
STATIC int32
_ctc_goldengate_dkit_tcam_resolve_empty(uint8 chip_id, uint32 tcam_entry_width, uint32 *data, uint32 *mask, uint32* p_is_empty)
{
    uint32 bit_pos = 0;
    uint32 index = 0, bit_offset = 0;

    /* data[1] -- [64,80]; data[2] -- [32,63]; data[3] -- [0,31] */
    for (bit_pos = 0; bit_pos < tcam_entry_width; bit_pos++)
    {
        index = bit_pos / 32;
        bit_offset = bit_pos % 32;

        if ((IS_BIT_SET(data[index], bit_offset))
            && IS_BIT_SET(mask[index], bit_offset))    /* X=1, Y=1: No Write but read. */
        {
            *p_is_empty |= 1;
            break;
        }
    }

    return CLI_SUCCESS;
}

int32
_ctc_goldengate_dkit_tcam_tbl_empty(uint8 chip_id_offset, tbls_id_t tbl_id, uint32 index, uint32* p_empty)
{
    ds_t    data;
    ds_t    mask;
    uint32* p_mask = NULL;
    uint32* p_data = NULL;
    uint32  key_size = TABLE_EXT_INFO_PTR(tbl_id)? TCAM_KEY_SIZE(tbl_id) : 0;
    uint32  entry_num_each_idx = 0, entry_idx = 0;
    bool    is_lpm_tcam = FALSE, is_lpm_tcam_ip = FALSE;
    uint8   chip_id = chip_id_offset + drv_gg_init_chip_info.drv_init_chipid_base;
    uint32  blknum = 0, local_idx = 0, is_sec_half = 0;
    tbl_entry_t entry;

    sal_memset(&data, 0, sizeof(data));
    sal_memset(&mask, 0, sizeof(mask));
    sal_memset(&entry, 0, sizeof(tbl_entry_t));

    entry.data_entry = (uint32 *)&data;
    entry.mask_entry = (uint32 *)&mask;

    DRV_CHIP_ID_VALID_CHECK(chip_id);
    DRV_TBL_ID_VALID_CHECK(tbl_id);

    *p_empty = 0;

    if ((!drv_goldengate_table_is_tcam_key(tbl_id))
        && (!drv_goldengate_table_is_lpm_tcam_key(tbl_id)))
    {
        CTC_DKIT_PRINT("@@ ERROR! When read operation, INVALID Tcam key TblID! TblID: %d, file:%s line:%d function:%s\n",
                       tbl_id, __FILE__, __LINE__, __FUNCTION__);
        return DRV_E_INVALID_TBL;
    }

    if (TABLE_MAX_INDEX(tbl_id) <= index)
    {
        CTC_DKIT_PRINT("ERROR (drv_chip_tcam_tbl_read): chip-0x%x, tbl-%d, index-0x%x exceeds the max_index-0x%x.\n",
                        chip_id, tbl_id, index, TABLE_MAX_INDEX(tbl_id));
        return DRV_E_INVALID_TBL;
    }

    if (drv_goldengate_table_is_tcam_key(tbl_id))
    {
        /* flow tcam w/r per 160 bit */
        entry_num_each_idx = key_size / (DRV_BYTES_PER_ENTRY*2);

        drv_goldengate_chip_flow_tcam_get_blknum_index(chip_id, tbl_id, index, &blknum, &local_idx, &is_sec_half);
        is_lpm_tcam = 0;

        /* bit 12 indicate if it is in high half TCAM in double size mode */
        local_idx = (is_sec_half << 12) | local_idx;
    }
    else if(drv_goldengate_table_is_lpm_tcam_key(tbl_id))
    { /*SDK Modify {*/
        uint32 offset = 0;
        is_lpm_tcam = TRUE;
        if(drv_goldengate_table_is_lpm_tcam_key_ip_prefix(tbl_id))
        {
            /* lpm ip tcam w/r per 44 bit */
            entry_num_each_idx = key_size / DRV_LPM_KEY_BYTES_PER_ENTRY;
            is_lpm_tcam_ip = TRUE;

            offset = (TABLE_DATA_BASE(tbl_id, 0)  -DRV_LPM_TCAM_DATA_ASIC_BASE0 ) / DRV_LPM_KEY_BYTES_PER_ENTRY;
            local_idx =( (offset / entry_num_each_idx) +  index)*entry_num_each_idx;

            /* bit 12 indicate if it is in high half TCAM in double size mode */
            local_idx = (is_sec_half << 13) | local_idx;
        }
        else
        {
            /* lpm nat tcam w/r per 184 bit */
            entry_num_each_idx = key_size / (DRV_BYTES_PER_ENTRY*2);

            offset = (TABLE_DATA_BASE(tbl_id, 0)  -DRV_LPM_TCAM_DATA_ASIC_BASE1 ) / DRV_ADDR_BYTES_PER_ENTRY;
            local_idx = ((offset / (entry_num_each_idx*2)) +  index)*entry_num_each_idx;
            /* bit 12 indicate if it is in high half TCAM in double size mode */
             /*local_idx = (is_sec_half << 9) | local_idx;*/
        }     /*SDK Modify }*/
    }

    p_data = entry.data_entry;
    p_mask = entry.mask_entry;

    for (entry_idx = 0; entry_idx < entry_num_each_idx; ++entry_idx)
    {
        /* read real tcam address */
        if (!is_lpm_tcam)
        {
            DRV_IF_ERROR_RETURN(_ctc_goldengate_dkits_tcam_read_flow_tcam_entry(chip_id, blknum, local_idx,
                                    p_mask + entry_idx*DRV_WORDS_PER_ENTRY*2,
                                    CTC_DKIT_DRV_INT_TCAM_RECORD_MASK));

            DRV_IF_ERROR_RETURN(_ctc_goldengate_dkits_tcam_read_flow_tcam_entry(chip_id, blknum, local_idx,
                                    p_data + entry_idx*DRV_WORDS_PER_ENTRY*2,
                                    CTC_DKIT_DRV_INT_TCAM_RECORD_DATA));

            DRV_IF_ERROR_RETURN(_ctc_goldengate_dkit_tcam_resolve_empty(chip_id, DRV_BYTES_PER_ENTRY*8*2,
                                    p_data + entry_idx*DRV_WORDS_PER_ENTRY*2,
                                    p_mask + entry_idx*DRV_WORDS_PER_ENTRY*2, p_empty));
        }
        else
        {
            if(is_lpm_tcam_ip)
            {
                DRV_IF_ERROR_RETURN(_ctc_goldengate_dkit_tcam_read_lpm_tcam_ip_entry(chip_id, local_idx,
                                        p_mask + entry_idx*DRV_LPM_WORDS_PER_ENTRY,
                                        CTC_DKIT_DRV_INT_LPM_TCAM_RECORD_MASK));

                DRV_IF_ERROR_RETURN(_ctc_goldengate_dkit_tcam_read_lpm_tcam_ip_entry(chip_id, local_idx,
                                        p_data + entry_idx*DRV_LPM_WORDS_PER_ENTRY,
                                        CTC_DKIT_DRV_INT_LPM_TCAM_RECORD_DATA));

                DRV_IF_ERROR_RETURN(_ctc_goldengate_dkit_tcam_resolve_empty(chip_id, DRV_LPM_KEY_BYTES_PER_ENTRY*8,
                                        p_data + entry_idx*DRV_LPM_WORDS_PER_ENTRY,
                                        p_mask + entry_idx*DRV_LPM_WORDS_PER_ENTRY, p_empty));

            }
            else
            {
                DRV_IF_ERROR_RETURN(_ctc_goldengate_dkit_tcam_read_lpm_tcam_nat_entry(chip_id, local_idx,
                                        p_mask + entry_idx*DRV_WORDS_PER_ENTRY*2,
                                        CTC_DKIT_DRV_INT_LPM_TCAM_RECORD_MASK));

                DRV_IF_ERROR_RETURN(_ctc_goldengate_dkit_tcam_read_lpm_tcam_nat_entry(chip_id, local_idx,
                                        p_data + entry_idx*DRV_WORDS_PER_ENTRY*2,
                                        CTC_DKIT_DRV_INT_LPM_TCAM_RECORD_DATA));

                DRV_IF_ERROR_RETURN(_ctc_goldengate_dkit_tcam_resolve_empty(chip_id, DRV_BYTES_PER_ENTRY*8*2,
                                        p_data + entry_idx*DRV_WORDS_PER_ENTRY*2,
                                        p_mask + entry_idx*DRV_WORDS_PER_ENTRY*2, p_empty));
            }
        }

        local_idx++;
    }

    return CLI_SUCCESS;
}

#define __1_COMMON__

STATIC int32
_ctc_goldengate_dkits_tcam_tbl2desc(tbls_id_t tblid, char* p_desc, uint8 len)
{
#define DESC_STR_COPY(dest, src, len) sal_strncpy(dest, src, (((sal_strlen(src) + 1) <= len) ? sal_strlen(src) : (len - 1)));

    sal_memset(p_desc, 0, len);

    if (IGS_ACL0_TCAM_TBL(tblid))
    {
        DESC_STR_COPY(p_desc, gg_tcam_module_desc[0], len);
    }
    else if (IGS_ACL1_TCAM_TBL(tblid))
    {
        DESC_STR_COPY(p_desc, gg_tcam_module_desc[1], len);
    }
    else if (IGS_ACL2_TCAM_TBL(tblid))
    {
        DESC_STR_COPY(p_desc, gg_tcam_module_desc[2], len);
    }
    else if (IGS_ACL3_TCAM_TBL(tblid))
    {
        DESC_STR_COPY(p_desc, gg_tcam_module_desc[3], len);
    }
    else if (EGS_ACL_TCAM_TBL(tblid))
    {
        DESC_STR_COPY(p_desc, gg_tcam_module_desc[4], len);
    }
    else if (IGS_SCL0_TCAM_TBL(tblid))
    {
        DESC_STR_COPY(p_desc, gg_tcam_module_desc[5], len);
    }
    else if (IGS_SCL1_TCAM_TBL(tblid))
    {
        DESC_STR_COPY(p_desc, gg_tcam_module_desc[6], len);
    }
    else if (IP_PREFIX_TCAM_TBL(tblid))
    {
        DESC_STR_COPY(p_desc, gg_tcam_module_desc[7], len);
    }
    else if (IP_NAT_PBR_TCAM_TBL(tblid))
    {
        DESC_STR_COPY(p_desc, gg_tcam_module_desc[8], len);
    }

    return CLI_SUCCESS;
}

STATIC void
_ctc_goldengate_dkits_tcam_type2desc(ctc_dkits_tcam_type_flag_t type_flag, uint32 priority, char* p_desc, uint8 len)
{
#define DESC_STR_COPY(dest, src, len) sal_strncpy(dest, src, (((sal_strlen(src) + 1) <= len) ? sal_strlen(src) : (len - 1)));

    sal_memset(p_desc, 0, len);

    if (DKITS_FLAG_ISSET(type_flag, CTC_DKITS_TCAM_TYPE_FLAG_IGS_SCL))
    {
        if (0 == priority)
        {
            DESC_STR_COPY(p_desc, gg_tcam_module_desc[5], len);
        }
        else
        {
            DESC_STR_COPY(p_desc, gg_tcam_module_desc[6], len);
        }
    }
    else if (DKITS_FLAG_ISSET(type_flag, CTC_DKITS_TCAM_TYPE_FLAG_IGS_ACL))
    {
        if (0 == priority)
        {
            DESC_STR_COPY(p_desc, gg_tcam_module_desc[0], len);
        }
        else if (1 == priority)
        {
            DESC_STR_COPY(p_desc, gg_tcam_module_desc[1], len);
        }
        else if (2 == priority)
        {
            DESC_STR_COPY(p_desc, gg_tcam_module_desc[2], len);
        }
        else if (3 == priority)
        {
            DESC_STR_COPY(p_desc, gg_tcam_module_desc[3], len);
        }
    }
    else if (DKITS_FLAG_ISSET(type_flag, CTC_DKITS_TCAM_TYPE_FLAG_EGS_ACL))
    {
        DESC_STR_COPY(p_desc, gg_tcam_module_desc[4], len);
    }
    else if (DKITS_FLAG_ISSET(type_flag, CTC_DKITS_TCAM_TYPE_FLAG_IPPREFIX))
    {
        DESC_STR_COPY(p_desc, gg_tcam_module_desc[7], len);
    }
    else if (DKITS_FLAG_ISSET(type_flag, CTC_DKITS_TCAM_TYPE_FLAG_IPNAT))
    {
        DESC_STR_COPY(p_desc, gg_tcam_module_desc[8], len);
    }

    return;
}

STATIC int32
_ctc_goldengate_dkits_tcam_tbl2module(tbls_id_t tblid, uint32* p_module)
{
    if (IGS_ACL0_TCAM_TBL(tblid) || IGS_ACL1_TCAM_TBL(tblid)
        || IGS_ACL2_TCAM_TBL(tblid) || IGS_ACL3_TCAM_TBL(tblid)
        || IGS_SCL0_TCAM_TBL(tblid) || IGS_SCL1_TCAM_TBL(tblid)
        || EGS_ACL_TCAM_TBL(tblid))
    {
        *p_module = DKITS_TCAM_MODULE_TYPE_FLOW;
    }
    else if (IP_PREFIX_TCAM_TBL(tblid))
    {
        *p_module = DKITS_TCAM_MODULE_TYPE_IPPREFIX;
    }
    else if (IP_NAT_PBR_TCAM_TBL(tblid))
    {
        *p_module = DKITS_TCAM_MODULE_TYPE_IPNAT;
    }
    else
    {
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

STATIC int32
_ctc_goldengate_dkits_tcam_key2tbl(ctc_dkits_tcam_info_t* p_tcam_info, tbls_id_t* p_tblid)
{
    if (DKITS_FLAG_ISSET(p_tcam_info->tcam_type, CTC_DKITS_TCAM_TYPE_FLAG_IGS_SCL))
    {
        if (DRV_TCAMKEYTYPE_MACKEY160 == p_tcam_info->key_type)
        {
            *p_tblid = (0 == p_tcam_info->priority) ? DsUserId0MacKey160_t : DsUserId1MacKey160_t;
        }
        else if (DRV_TCAMKEYTYPE_IPV6KEY640 == p_tcam_info->key_type)
        {
            *p_tblid = (0 == p_tcam_info->priority) ? DsUserId0Ipv6Key640_t : DsUserId1Ipv6Key640_t;
        }
        else if (DRV_TCAMKEYTYPE_MACL3KEY320 == p_tcam_info->key_type)
        {
            *p_tblid = (0 == p_tcam_info->priority) ?  DsUserId0L3Key320_t :  DsUserId1L3Key320_t;
        }
        else if (DRV_TCAMKEYTYPE_L3KEY160 == p_tcam_info->key_type)
        {
            *p_tblid = (0 == p_tcam_info->priority) ?  DsUserId0L3Key160_t :  DsUserId1L3Key160_t;
        }
    }
    else if (DKITS_FLAG_ISSET(p_tcam_info->tcam_type, CTC_DKITS_TCAM_TYPE_FLAG_IGS_ACL))
    {
        if (DRV_TCAMKEYTYPE_MACKEY160 == p_tcam_info->key_type)
        {
            *p_tblid = DsAclQosMacKey160Ingr0_t + ((DsAclQosMacKey160Ingr1_t - DsAclQosMacKey160Ingr0_t) * p_tcam_info->priority);
        }
        else if (DRV_TCAMKEYTYPE_IPV6KEY640 == p_tcam_info->key_type)
        {
            *p_tblid = DsAclQosIpv6Key640Ingr0_t + ((DsAclQosIpv6Key640Ingr1_t - DsAclQosIpv6Key640Ingr0_t) * p_tcam_info->priority);
        }
        else if (DRV_TCAMKEYTYPE_MACL3KEY320 == p_tcam_info->key_type)
        {
            *p_tblid = DsAclQosL3Key320Ingr0_t + ((DsAclQosL3Key320Ingr1_t -  DsAclQosL3Key320Ingr0_t) * p_tcam_info->priority);
        }
        else if (DRV_TCAMKEYTYPE_L3KEY160 == p_tcam_info->key_type)
        {
            *p_tblid = DsAclQosL3Key160Ingr0_t + ((DsAclQosL3Key160Ingr1_t - DsAclQosL3Key160Ingr0_t) * p_tcam_info->priority);
        }
    }
    else if (DKITS_FLAG_ISSET(p_tcam_info->tcam_type, CTC_DKITS_TCAM_TYPE_FLAG_EGS_ACL))
    {
        if (DRV_TCAMKEYTYPE_MACKEY160 == p_tcam_info->key_type)
        {
            *p_tblid = DsAclQosMacKey160Egr0_t;
        }
        else if (DRV_TCAMKEYTYPE_IPV6KEY640 == p_tcam_info->key_type)
        {
            *p_tblid = DsAclQosIpv6Key640Egr0_t;
        }
        else if (DRV_TCAMKEYTYPE_MACL3KEY320 == p_tcam_info->key_type)
        {
            *p_tblid = DsAclQosL3Key320Egr0_t;
        }
        else if (DRV_TCAMKEYTYPE_L3KEY160 == p_tcam_info->key_type)
        {
            *p_tblid = DsAclQosL3Key160Egr0_t;
        }
    }
    else if (DKITS_FLAG_ISSET(p_tcam_info->tcam_type, CTC_DKITS_TCAM_TYPE_FLAG_IPPREFIX))
    {
        if (DRV_FIBLPMTCAMKEYTYPE_IPV4UCAST == p_tcam_info->key_type)
        {
            *p_tblid = DsLpmTcamIpv440Key_t;
        }
        else if (DRV_FIBLPMTCAMKEYTYPE_IPV6UCAST == p_tcam_info->key_type)
        {
            *p_tblid = DsLpmTcamIpv6160Key0_t;
        }
    }
    else if (DKITS_FLAG_ISSET(p_tcam_info->tcam_type, CTC_DKITS_TCAM_TYPE_FLAG_IPNAT))
    {
        if (DRV_FIBNATPBRTCAMKEYTYPE_IPV4PBR == p_tcam_info->key_type)
        {
            *p_tblid = DsLpmTcamIpv4Pbr160Key_t;
        }
        else if (DRV_FIBNATPBRTCAMKEYTYPE_IPV6PBR == p_tcam_info->key_type)
        {
            *p_tblid = DsLpmTcamIpv6320Key_t;
        }
        else if (DRV_FIBNATPBRTCAMKEYTYPE_IPV4NAT == p_tcam_info->key_type)
        {
            *p_tblid = DsLpmTcamIpv4NAT160Key_t;
        }
        else if (DRV_FIBNATPBRTCAMKEYTYPE_IPV6NAT == p_tcam_info->key_type)
        {
            *p_tblid = DsLpmTcamIpv6160Key1_t;
        }
    }

    return CLI_SUCCESS;
}

STATIC void
_ctc_goldengate_dkits_tcam_entry2offset(tbls_id_t tblid, uint8 blkid, uint32 entry_idx, uint32* p_bist_offset)
{
    uint32 module = 0;
    uint32 couple = 0;

    _ctc_goldengate_dkits_tcam_tbl2module(tblid, &module);
    drv_goldengate_get_dynamic_ram_couple_mode(&couple);

    *p_bist_offset = entry_idx;

    if (couple && (DKITS_TCAM_MODULE_TYPE_FLOW == module)
       && (entry_idx >= drv_goldengate_get_tcam_entry_num(blkid, 0)/4))
    {
        *p_bist_offset = 0x1000 + (entry_idx - drv_goldengate_get_tcam_entry_num(blkid, 0)/4);
    }

    return;
}

STATIC void
_ctc_goldengate_dkits_tcam_offset2entry(tbls_id_t tblid, uint8 blkid, uint32 bist_offset, uint32* p_entry_idx)
{
    uint32 module = 0;
    uint32 couple = 0;

    _ctc_goldengate_dkits_tcam_tbl2module(tblid, &module);
    drv_goldengate_get_dynamic_ram_couple_mode(&couple);

    *p_entry_idx = bist_offset;

    if (couple && (DKITS_TCAM_MODULE_TYPE_FLOW == module)
       && (bist_offset >= 0x1000))
    {
        *p_entry_idx = bist_offset - 0x1000 + (drv_goldengate_get_tcam_entry_num(blkid, 0)/4);
    }

    return;
}

STATIC int32
_ctc_goldengate_dkits_tcam_reset_detect(uint8 lchip, dkits_tcam_module_type_t type)
{
    uint32 i = 0;
    uint32 cmd = 0;

    FlowTcamBistCtl_m flow_tcam_bist_ctl;
    FlowTcamBistStatus_m flow_tcam_bist_status;
    FlowTcamBistReqMem_m flow_tcam_bist_req_mem;
    FlowTcamBistResultMem_m flow_tcam_bist_result_mem;

    LpmTcamIpBistCtl_m lpm_tcam_ip_bist_ctl;
    LpmTcamIpBistStatus_m lpm_tcam_ip_bist_status;
    LpmTcamIpBistReqMem_m lpm_tcam_ip_bist_req_mem;
    LpmTcamIpBistResultMem_m lpm_tcam_ip_bist_result_mem;

    LpmTcamNatBistCtl0_m lpm_tcam_nat_bist_ctl;
    LpmTcamNatBistStatus0_m lpm_tcam_nat_bist_status;
    LpmTcamNatBistReqMem0_m lpm_tcam_nat_bist_req_mem;
    LpmTcamNatBistResultMem0_m lpm_tcam_nat_bist_result_mem;

    if (DKITS_TCAM_MODULE_TYPE_FLOW == type)
    {
        sal_memset(&flow_tcam_bist_ctl, 0, sizeof(FlowTcamBistCtl_m));
        cmd = DRV_IOW(FlowTcamBistCtl_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &flow_tcam_bist_ctl);

        sal_memset(&flow_tcam_bist_status, 0, sizeof(FlowTcamBistStatus_m));
        cmd = DRV_IOW(FlowTcamBistStatus_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &flow_tcam_bist_status);

        sal_memset(&flow_tcam_bist_req_mem, 0, sizeof(FlowTcamBistReqMem_m));
        for (i = 0; i < TABLE_MAX_INDEX(FlowTcamBistReqMem_t); i++)
        {
            cmd = DRV_IOW(FlowTcamBistReqMem_t, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, i, cmd, &flow_tcam_bist_req_mem);
        }

        sal_memset(&flow_tcam_bist_result_mem, 0, sizeof(FlowTcamBistResultMem_m));
        for (i = 0; i < TABLE_MAX_INDEX(FlowTcamBistResultMem_t); i++)
        {
            cmd = DRV_IOW(FlowTcamBistResultMem_t, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, i, cmd, &flow_tcam_bist_result_mem);
        }
    }

    if (DKITS_TCAM_MODULE_TYPE_IPPREFIX == type)
    {
        sal_memset(&lpm_tcam_ip_bist_ctl, 0, sizeof(LpmTcamIpBistCtl_m));
        cmd = DRV_IOW(LpmTcamIpBistCtl_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &lpm_tcam_ip_bist_ctl);

        sal_memset(&lpm_tcam_ip_bist_status, 0, sizeof(LpmTcamIpBistStatus_m));
        cmd = DRV_IOW(LpmTcamIpBistStatus_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &lpm_tcam_ip_bist_status);

        sal_memset(&lpm_tcam_ip_bist_req_mem, 0, sizeof(LpmTcamIpBistReqMem_m));
        for (i = 0; i < TABLE_MAX_INDEX(LpmTcamIpBistReqMem_t); i++)
        {
            cmd = DRV_IOW(LpmTcamIpBistReqMem_t, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, i, cmd, &lpm_tcam_ip_bist_req_mem);
        }

        sal_memset(&lpm_tcam_ip_bist_result_mem, 0, sizeof(LpmTcamIpBistResultMem_m));
        for (i = 0; i < TABLE_MAX_INDEX(LpmTcamIpBistResultMem_t); i++)
        {
            cmd = DRV_IOW(LpmTcamIpBistResultMem_t, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, i, cmd, &lpm_tcam_ip_bist_result_mem);
        }
    }

    if (DKITS_TCAM_MODULE_TYPE_IPNAT == type)
    {
        sal_memset(&lpm_tcam_nat_bist_ctl, 0, sizeof(LpmTcamNatBistCtl0_m));
        cmd = DRV_IOW(LpmTcamNatBistCtl2_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &lpm_tcam_nat_bist_ctl);

        sal_memset(&lpm_tcam_nat_bist_status, 0, sizeof(LpmTcamNatBistStatus0_m));
        cmd = DRV_IOW(LpmTcamNatBistStatus2_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &lpm_tcam_nat_bist_status);

        sal_memset(&lpm_tcam_nat_bist_req_mem, 0, sizeof(LpmTcamNatBistReqMem0_m));
        for (i = 0; i < TABLE_MAX_INDEX(LpmTcamNatBistReqMem2_t); i++)
        {
            cmd = DRV_IOW(LpmTcamNatBistReqMem2_t, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, i, cmd, &lpm_tcam_nat_bist_req_mem);
        }

        sal_memset(&lpm_tcam_nat_bist_result_mem, 0, sizeof(LpmTcamNatBistResultMem0_m));
        for (i = 0; i < TABLE_MAX_INDEX(LpmTcamNatBistResultMem2_t); i++)
        {
            cmd = DRV_IOW(LpmTcamNatBistResultMem2_t, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, i, cmd, &lpm_tcam_nat_bist_result_mem);
        }
    }

    return DRV_E_NONE;
}

int32
ctc_goldengate_dkits_tcam_entry_offset(tbls_id_t tblid, uint32 tbl_idx, uint32* p_offset, uint32* p_blk)
{
    uint8  lchip = 0;
    uint32 is_sec_half = 0;
    uint32 idx = 0;
    uint32 module = 0;
    uint32 start_offset = 0;
    uint32 entry_num;
    uint32 entry_size;

    _ctc_goldengate_dkits_tcam_tbl2module(tblid, &module);

    if (DKITS_TCAM_MODULE_TYPE_FLOW == module)
    {
        drv_goldengate_chip_flow_tcam_get_blknum_index(lchip, tblid, tbl_idx, p_blk, &idx, &is_sec_half);

        /* unit is 160bit */
        *p_offset = is_sec_half ? idx + (drv_goldengate_get_tcam_entry_num(*p_blk, 0)/4) : idx;
    }
    else
    {
        drv_goldengate_ftm_get_lpm_tcam_info(tblid, &start_offset, &entry_num, &entry_size);

        if (DKITS_TCAM_MODULE_TYPE_IPPREFIX == module)
        {
            *p_blk = DKITS_TCAM_BLOCK_TYPE_IGS_LPM0;
            *p_offset = start_offset + ((TCAM_KEY_SIZE(tblid) / DRV_LPM_KEY_BYTES_PER_ENTRY) * tbl_idx);
        }
        else if (DKITS_TCAM_MODULE_TYPE_IPNAT == module)
        {
            *p_blk = DKITS_TCAM_BLOCK_TYPE_IGS_LPM1;
            *p_offset = (start_offset + ((TCAM_KEY_SIZE(tblid) / DRV_BYTES_PER_ENTRY) * tbl_idx)) / 2;
        }
    }

    return CLI_SUCCESS;
}

STATIC int32
_ctc_goldengate_dkits_tcam_tbl_index(tbls_id_t tblid, uint32 blk, uint32 offset, uint32* p_tbl_idx)
{
    uint8  lchip = 0;
    uint32 idx = 0;
    uint32 is_sec_half = 0;
    uint32 entry_offset = 0;
    uint32 entry_num = 0;
    uint32 entry_size = 0;
    uint32 module = 0;

    _ctc_goldengate_dkits_tcam_tbl2module(tblid, &module);

    if ((DKITS_TCAM_MODULE_TYPE_IPPREFIX == module) || (DKITS_TCAM_MODULE_TYPE_IPNAT == module))
    {
        drv_goldengate_ftm_get_lpm_tcam_info(tblid, &entry_offset, &entry_num, &entry_size);
        *p_tbl_idx = (offset - entry_offset) / entry_size;
    }
    else
    {
        drv_goldengate_chip_flow_tcam_get_blknum_index(lchip, tblid, TCAM_START_INDEX(tblid, blk), &blk, &idx, &is_sec_half);

        /* offset unit is 160bit */
        if (idx  > offset)
        {
            *p_tbl_idx = CTC_DKITS_INVALID_INDEX;
        }
        else
        {
            entry_num =  TABLE_ENTRY_SIZE(tblid) / DRV_BYTES_PER_ENTRY;
            *p_tbl_idx = TCAM_START_INDEX(tblid, blk) + (((offset * 2) - (idx * entry_num)) / entry_num);
        }
    }

    return CLI_SUCCESS;
}

STATIC int32
_ctc_goldengate_dkits_tcam_check_tbl_empty(uint32 tbl_id, uint32* p_data, uint32* p_empty)
{
    uint8 i = 0, j = 0;
    fields_t* fld_ptr = NULL;
    tables_info_t* tbl_ptr = NULL;
    uint32 data[MAX_ENTRY_WORD] = {0};
    *p_empty = 1;

    tbl_ptr = TABLE_INFO_PTR(tbl_id);

    for (i = 0; i < tbl_ptr->field_num; i++)
    {
        sal_memset(data, 0, sizeof(data));
        fld_ptr = &(tbl_ptr->ptr_fields[i]);

        drv_goldengate_get_field(tbl_id, i, (uint32*)p_data, data);

        for (j = 0; j < ((fld_ptr->bits + 31)/32); j++)
        {
            if (0 != data[j])
            {
                *p_empty = 0;
                break;
            }
        }
        if (0 == *p_empty)
        {
            break;
        }
    }

    return CLI_SUCCESS;
}

int32
ctc_goldengate_dkits_tcam_mem2tbl(uint32 mem_id, uint32 key_type, uint32* p_tblid)
{
    if (DsAclQosL3Key160Ingr0_t == mem_id)
    {
        *p_tblid = (DKITS_FLOW_TCAM_LOOKUP_TYPE_L2KEY == key_type) ? DsAclQosMacKey160Ingr0_t : DsAclQosL3Key160Ingr0_t;
    }
    else if (DsAclQosL3Key160Ingr1_t == mem_id)
    {
        *p_tblid = (DKITS_FLOW_TCAM_LOOKUP_TYPE_L2KEY == key_type) ? DsAclQosMacKey160Ingr1_t : DsAclQosL3Key160Ingr1_t;
    }
    else if (DsAclQosL3Key160Ingr2_t == mem_id)
    {
        *p_tblid = (DKITS_FLOW_TCAM_LOOKUP_TYPE_L2KEY == key_type) ? DsAclQosMacKey160Ingr2_t : DsAclQosL3Key160Ingr2_t;
    }
    else if (DsAclQosL3Key160Ingr3_t == mem_id)
    {
        *p_tblid = (DKITS_FLOW_TCAM_LOOKUP_TYPE_L2KEY == key_type) ? DsAclQosMacKey160Ingr3_t : DsAclQosL3Key160Ingr3_t;
    }
    else
    {
        *p_tblid = mem_id;
    }

    if (DsAclQosL3Key160Egr0_t == mem_id)
    {
        *p_tblid = (DKITS_FLOW_TCAM_LOOKUP_TYPE_L2KEY == key_type) ? DsAclQosMacKey160Egr0_t : DsAclQosL3Key160Egr0_t;
    }
    else
    {
        *p_tblid = mem_id;
    }

    if (DsUserId0L3Key160_t == mem_id)
    {
        *p_tblid = (DKITS_FLOW_TCAM_LOOKUP_TYPE_L2KEY == key_type) ? DsUserId0MacKey160_t : DsUserId0L3Key160_t;
    }
    else if (DsUserId1L3Key160_t == mem_id)
    {
        *p_tblid = (DKITS_FLOW_TCAM_LOOKUP_TYPE_L2KEY == key_type) ? DsUserId1MacKey160_t : DsUserId1L3Key160_t;
    }
    else
    {
        *p_tblid = mem_id;
    }

    return CLI_SUCCESS;
}

int32
ctc_goldengate_dkits_tcam_parser_key_type(tbls_id_t tbl_id, tbl_entry_t* p_tcam_key, uint32* p_key_type)
{
    if ((DsUserId0L3Key160_t == tbl_id) || (DsUserId0MacKey160_t == tbl_id)
       || (DsUserId1L3Key160_t == tbl_id) || (DsUserId1MacKey160_t == tbl_id)
       || (DsAclQosMacKey160Ingr0_t == tbl_id) || (DsAclQosMacKey160Ingr1_t == tbl_id)
       || (DsAclQosMacKey160Ingr2_t == tbl_id) || (DsAclQosMacKey160Ingr3_t == tbl_id)
       || (DsAclQosL3Key160Ingr0_t == tbl_id) || (DsAclQosL3Key160Ingr1_t == tbl_id)
       || (DsAclQosL3Key160Ingr2_t == tbl_id) || (DsAclQosL3Key160Ingr3_t == tbl_id))
    {
        *p_key_type = GetDsAclQosL3Key160Ingr0(V, aclQosKeyType_f, p_tcam_key->data_entry);
        *p_key_type &= GetDsAclQosL3Key160Ingr0(V, aclQosKeyType_f, p_tcam_key->mask_entry);
    }
    else if ((DsUserId0L3Key320_t == tbl_id) || (DsUserId1L3Key320_t == tbl_id)
            || (DsAclQosL3Key320Ingr0_t == tbl_id) || (DsAclQosL3Key320Ingr1_t == tbl_id)
            || (DsAclQosL3Key320Ingr2_t == tbl_id) || (DsAclQosL3Key320Ingr3_t == tbl_id))
    {
        *p_key_type = GetDsAclQosL3Key320Ingr0(V, aclQosKeyType0_f, p_tcam_key->data_entry);
        *p_key_type &= GetDsAclQosL3Key320Ingr0(V, aclQosKeyType0_f, p_tcam_key->mask_entry);
    }
    else if ((DsUserId0Ipv6Key640_t == tbl_id) || (DsUserId1Ipv6Key640_t == tbl_id)
            || (DsAclQosIpv6Key640Ingr0_t == tbl_id) || (DsAclQosIpv6Key640Ingr1_t == tbl_id)
            || (DsAclQosIpv6Key640Ingr2_t == tbl_id) || (DsAclQosIpv6Key640Ingr3_t == tbl_id))
    {
        *p_key_type = GetDsAclQosIpv6Key640(V, aclQosKeyType0_f, p_tcam_key->data_entry);
        *p_key_type &= GetDsAclQosIpv6Key640(V, aclQosKeyType0_f, p_tcam_key->mask_entry);
    }
    else if ((DsAclQosL3Key160Egr0_t == tbl_id) || (DsAclQosMacKey160Egr0_t == tbl_id))
    {
        *p_key_type = GetDsAclQosMacKey160(V, aclQosKeyType_f, p_tcam_key->data_entry);
        *p_key_type &= GetDsAclQosMacKey160(V, aclQosKeyType_f, p_tcam_key->mask_entry);
    }
    else if (DsAclQosL3Key320Egr0_t == tbl_id)
    {
        *p_key_type = GetDsAclQosL3Key320(V, aclQosKeyType0_f, p_tcam_key->data_entry);
        *p_key_type &= GetDsAclQosL3Key320(V, aclQosKeyType0_f, p_tcam_key->mask_entry);
    }
    else if (DsAclQosIpv6Key640Egr0_t == tbl_id)
    {
        *p_key_type = GetDsAclQosIpv6Key640(V, aclQosKeyType0_f, p_tcam_key->data_entry);
        *p_key_type &= GetDsAclQosIpv6Key640(V, aclQosKeyType0_f, p_tcam_key->mask_entry);
    }
    else if (DsLpmTcamIpv440Key_t == tbl_id)
    {
        *p_key_type = GetDsLpmTcamIpv440Key(V, lpmTcamKeyType_f, p_tcam_key->data_entry);
        *p_key_type &= GetDsLpmTcamIpv440Key(V, lpmTcamKeyType_f, p_tcam_key->mask_entry);
    }
    else if (DsLpmTcamIpv6160Key0_t == tbl_id)
    {
        *p_key_type = GetDsLpmTcamIpv6160Key0(V, lpmTcamKeyType0_f, p_tcam_key->data_entry);
        *p_key_type &= GetDsLpmTcamIpv6160Key0(V, lpmTcamKeyType0_f, p_tcam_key->mask_entry);
    }
    else if (DsLpmTcamIpv4NAT160Key_t == tbl_id)
    {
        *p_key_type = GetDsLpmTcamIpv4NAT160Key(V, lpmTcamKeyType_f, p_tcam_key->data_entry);
        *p_key_type &= GetDsLpmTcamIpv4NAT160Key(V, lpmTcamKeyType_f, p_tcam_key->mask_entry);
    }
    else if (DsLpmTcamIpv6160Key1_t == tbl_id)
    {
        *p_key_type = GetDsLpmTcamIpv6160Key1(V, lpmTcamKeyType_f, p_tcam_key->data_entry);
        *p_key_type &= GetDsLpmTcamIpv6160Key1(V, lpmTcamKeyType_f, p_tcam_key->mask_entry);
    }
    else if (DsLpmTcamIpv4Pbr160Key_t == tbl_id)
    {
        *p_key_type = GetDsLpmTcamIpv4Pbr160Key(V, lpmTcamKeyType_f, p_tcam_key->data_entry);
        *p_key_type &= GetDsLpmTcamIpv4Pbr160Key(V, lpmTcamKeyType_f, p_tcam_key->mask_entry);
    }
    else if (DsLpmTcamIpv6320Key_t == tbl_id)
    {
        *p_key_type = GetDsLpmTcamIpv6320Key(V, lpmTcamKeyType0_f, p_tcam_key->data_entry);
        *p_key_type &= GetDsLpmTcamIpv6320Key(V, lpmTcamKeyType0_f, p_tcam_key->mask_entry);
    }
    else
    {
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

int32
ctc_goldengate_dkits_tcam_read_tcam_type(tbls_id_t tbl_id, uint32 idx, uint32* p_key_type)
{
    int32       ret = 0;
    uint8       lchip = 0;
    uint32      cmd = 0;
    tbl_entry_t tcam_key;
    ds_t        data;
    ds_t        mask;

    sal_memset(&tcam_key, 0, sizeof(tcam_key));
    sal_memset(&data, 0, sizeof(data));
    sal_memset(&mask, 0, sizeof(mask));

    tcam_key.data_entry = (uint32 *) &data;
    tcam_key.mask_entry = (uint32 *) &mask;

    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, idx, cmd, &tcam_key);

    ret = ctc_goldengate_dkits_tcam_parser_key_type(tbl_id, &tcam_key, p_key_type);

    return ret;
}

int32
ctc_goldengate_dkits_tcam_read_tcam_key(uint8 lchip, tbls_id_t tbl_id, uint32 index, uint32* p_empty, tbl_entry_t* p_tcam_key)
{
    int32  ret = 0;
    uint32 cmd = 0;
    uint32 data_empty = 0;
    uint32 mask_empty = 0;

    sal_memset(p_tcam_key->data_entry, 0, sizeof(ds_t));
    sal_memset(p_tcam_key->mask_entry, 0, sizeof(ds_t));

    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, index, cmd, p_tcam_key);
    if (ret < 0)
    {
        CTC_DKIT_PRINT("Read tcam key %s:%u error!\n", TABLE_NAME(tbl_id), index);
        return CLI_ERROR;
    }

    if (0 == SDK_WORK_PLATFORM)
    {
        _ctc_goldengate_dkit_tcam_tbl_empty(0, tbl_id, index, p_empty);
    }
    else
    {
        _ctc_goldengate_dkits_tcam_check_tbl_empty(tbl_id, p_tcam_key->data_entry, &data_empty);
        _ctc_goldengate_dkits_tcam_check_tbl_empty(tbl_id, p_tcam_key->mask_entry, &mask_empty);
        *p_empty = (1 == data_empty) || (1 == mask_empty);
    }

    return CLI_SUCCESS;
}

#define __2_BIST__

STATIC int32
_ctc_goldengate_dkits_tcam_bist_start(uint8 lchip, dkits_tcam_module_type_t module, uint8 bist_entry, uint8 blk)
{
    uint32 cmd = 0;
    uint32 bmp = 0;
    FlowTcamBistCtl_m flow_tcam_bist_ctl;
    LpmTcamIpBistCtl_m lpm_tcam_ip_bist_ctl;
    LpmTcamNatBistCtl0_m lpm_tcam_nat_bist_ctl;

    if (0 == bist_entry)
    {
        return 0;
    }

    if (DKITS_TCAM_MODULE_TYPE_FLOW == module)
    {
        sal_memset(&flow_tcam_bist_ctl, 0, sizeof(FlowTcamBistCtl_m));
        SetFlowTcamBistCtl(V, cfgBistEntries_f, &flow_tcam_bist_ctl, bist_entry);
        SetFlowTcamBistCtl(V, cfgBistEn_f, &flow_tcam_bist_ctl, 1);
        SetFlowTcamBistCtl(V, cfgBistOnce_f, &flow_tcam_bist_ctl, 1);
        DKITS_BIT_SET(bmp, blk);
        SetFlowTcamBistCtl(V, cfgBistVec_f, &flow_tcam_bist_ctl, bmp);
        SetFlowTcamBistCtl(V, cfgStopOnError_f, &flow_tcam_bist_ctl, 0);

        cmd = DRV_IOW(FlowTcamBistCtl_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &flow_tcam_bist_ctl);
    }
    else if (DKITS_TCAM_MODULE_TYPE_IPPREFIX == module)
    {
        sal_memset(&lpm_tcam_ip_bist_ctl, 0, sizeof(LpmTcamIpBistCtl_m));
        SetLpmTcamIpBistCtl(V, cfgBistEn_f, &lpm_tcam_ip_bist_ctl, 1);
        SetLpmTcamIpBistCtl(V, cfgBistOnce_f, &lpm_tcam_ip_bist_ctl, 1);
        SetLpmTcamIpBistCtl(V, cfgBistEntries_f, &lpm_tcam_ip_bist_ctl, bist_entry);
        SetLpmTcamIpBistCtl(V, cfgStopOnError_f, &lpm_tcam_ip_bist_ctl, 0);

        cmd = DRV_IOW(LpmTcamIpBistCtl_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &lpm_tcam_ip_bist_ctl);
    }
    else if (DKITS_TCAM_MODULE_TYPE_IPNAT == module)
    {
        sal_memset(&lpm_tcam_nat_bist_ctl, 0, sizeof(LpmTcamNatBistCtl0_m));
        SetLpmTcamNatBistCtl2(V, cfgBistEn_f, &lpm_tcam_nat_bist_ctl, 1);
        SetLpmTcamNatBistCtl2(V, cfgBistOnce_f, &lpm_tcam_nat_bist_ctl, 1);
        SetLpmTcamNatBistCtl2(V, cfgBistEntries_f, &lpm_tcam_nat_bist_ctl, bist_entry);
        SetLpmTcamNatBistCtl2(V, cfgStopOnError_f, &lpm_tcam_nat_bist_ctl, 0);

        cmd = DRV_IOW(LpmTcamNatBistCtl0_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &lpm_tcam_nat_bist_ctl);

        cmd = DRV_IOW(LpmTcamNatBistCtl1_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &lpm_tcam_nat_bist_ctl);

        cmd = DRV_IOW(LpmTcamNatBistCtl2_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &lpm_tcam_nat_bist_ctl);
    }

    return 0;
}

STATIC int32
_ctc_goldengate_dkits_tcam_bist_detect(uint8 lchip, dkits_tcam_module_type_t module, ctc_dkits_tcam_interface_status_t* p_status)
{
    int32  ret = 0;
    uint32 cmd = 0;
    FlowTcamBistStatus_m flow_tcam_bist_status;
    LpmTcamIpBistStatus_m lpm_tcam_ip_bist_status;
    LpmTcamNatBistStatus0_m lpm_tcam_nat_bist_status;

    if (DKITS_TCAM_MODULE_TYPE_FLOW == module)
    {
        sal_memset(&flow_tcam_bist_status, 0, sizeof(FlowTcamBistStatus_m));
        cmd = DRV_IOR(FlowTcamBistStatus_t, DRV_ENTRY_FLAG);
        ret = DRV_IOCTL(lchip, 0, cmd, &flow_tcam_bist_status);
        if (ret != 0)
        {
            CTC_DKIT_PRINT("%d\n", ret);
        }
        p_status->bist_mismatch_count = GetFlowTcamBistStatus(V, cfgBistMismatchCnt_f, &flow_tcam_bist_status);
        p_status->bist_request_done = GetFlowTcamBistStatus(V, cfgBistRequestDoneOnce_f, &flow_tcam_bist_status);
        p_status->bist_result_done = GetFlowTcamBistStatus(V, cfgBistResultDoneOnce_f, &flow_tcam_bist_status);
    }
    if (DKITS_TCAM_MODULE_TYPE_IPPREFIX == module)
    {
        sal_memset(&lpm_tcam_ip_bist_status, 0, sizeof(LpmTcamIpBistStatus_m));
        cmd = DRV_IOR(LpmTcamIpBistStatus_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &lpm_tcam_ip_bist_status);

        p_status->bist_mismatch_count = GetLpmTcamIpBistStatus(V, cfgBistMismatchCnt_f, &lpm_tcam_ip_bist_status);
        p_status->bist_request_done = GetLpmTcamIpBistStatus(V, cfgBistRequestDoneOnce_f, &lpm_tcam_ip_bist_status);
        p_status->bist_result_done = GetLpmTcamIpBistStatus(V, cfgBistResultDoneOnce_f, &lpm_tcam_ip_bist_status);
    }
    if (DKITS_TCAM_MODULE_TYPE_IPNAT == module)
    {
        sal_memset(&lpm_tcam_nat_bist_status, 0, sizeof(LpmTcamNatBistStatus0_m));
        cmd = DRV_IOR(LpmTcamNatBistStatus2_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &lpm_tcam_nat_bist_status);

        p_status->bist_mismatch_count = GetLpmTcamNatBistStatus2(V, cfgBistMismatchCnt_f, &lpm_tcam_nat_bist_status);
        p_status->bist_request_done = GetLpmTcamNatBistStatus2(V, cfgBistRequestDoneOnce_f, &lpm_tcam_nat_bist_status);
        p_status->bist_result_done = GetLpmTcamNatBistStatus2(V, cfgBistResultDoneOnce_f, &lpm_tcam_nat_bist_status);
    }

    return CLI_SUCCESS;
}

STATIC int32
_ctc_goldengate_dkits_tcam_bist_present_detail(uint32 tbl_id, uint32 expect_tbl_idx, uint32 result_tbl_idx)
{
    CTC_DKIT_PRINT("    Tcam key real table:  %s\n", TABLE_NAME(tbl_id));
    CTC_DKIT_PRINT("    Tcam key expect lookup result index: 0x%04X\n", expect_tbl_idx);
    CTC_DKIT_PRINT("    Tcam key real lookup result index:   0x%04X\n", result_tbl_idx);
    if (expect_tbl_idx == result_tbl_idx)
    {
        CTC_DKIT_PRINT("    Scan pass!\n\n");
    }
    else
    {
        CTC_DKIT_PRINT("    Scan fail!\n\n");
        /*Expect key value XXX mask XXX    Real key value XXX mask XXX */
    }

    return CLI_SUCCESS;
}

STATIC int32
_ctc_goldengate_dkits_tcam_bist_tbl_brief_status(tbls_id_t* p_tblid, uint8 is_get)
{
    static tbls_id_t tblid = MaxTblId_t;

    if (is_get)
    {
        *p_tblid = tblid;
    }
    else
    {
        if (NULL == p_tblid)
        {
            tblid = MaxTblId_t;
        }
        else
        {
            tblid = *p_tblid;
        }
    }

    return CLI_SUCCESS;
}

STATIC int32
_ctc_goldengate_dkits_tcam_bist_present_general(uint32 expect_tbl_idx, uint32 real_tbl_index)
{
    static uint8 head = 0;
    tbls_id_t tblid = MaxTblId_t;

    if (expect_tbl_idx != real_tbl_index)
    {
        _ctc_goldengate_dkits_tcam_bist_tbl_brief_status(&tblid, 0);

        if (0 == head)
        {
            CTC_DKIT_PRINT("    ExpectIndex   RealIndex\n");
            CTC_DKIT_PRINT("    -------------------------\n");
            head = 1;
        }

        CTC_DKIT_PRINT("    0x%04X         0x%04X\n", expect_tbl_idx, real_tbl_index);
    }

    return CLI_SUCCESS;
}

STATIC int32
_ctc_goldengate_dkits_tcam_bist_present_brief(uint32 tbl_id, uint32 expect_tbl_idx, uint32 result_tbl_idx)
{
    tbls_id_t tblid = MaxTblId_t;

    if (expect_tbl_idx != result_tbl_idx)
    {
        _ctc_goldengate_dkits_tcam_bist_tbl_brief_status(&tblid, 0);
        CTC_DKIT_PRINT("    Failed table:%s index:0x%04X\n", TABLE_NAME(tbl_id), expect_tbl_idx);
    }

    return CLI_SUCCESS;
}

STATIC void
_ctc_goldengate_dkits_tcam_bist_show_result(tbls_id_t tblid, dkits_tcam_scan_info_type_t info, uint32 expect_tbl_idx, uint32 result_tbl_idx)
{
    if (DKITS_TCAM_SCAN_INFO_TYPE_DETAIL == info)
    {
        /* tcam type, key type, key index */
        _ctc_goldengate_dkits_tcam_bist_present_detail(tblid, expect_tbl_idx, result_tbl_idx);
    }
    else if (DKITS_TCAM_SCAN_INFO_TYPE_GENERAL == info)
    {
        /* tcam type, key type*/
        _ctc_goldengate_dkits_tcam_bist_present_general(expect_tbl_idx, result_tbl_idx);
    }
    else if (DKITS_TCAM_SCAN_INFO_TYPE_BRIEF == info)
    {
        _ctc_goldengate_dkits_tcam_bist_present_brief(tblid, expect_tbl_idx, result_tbl_idx);
    }

    return;
}

STATIC int32
_ctc_goldengate_dkits_tcam_bist_present_result(uint8 lchip, tbls_id_t mem_id, uint8 blk, uint8 entry_num, dkits_tcam_scan_info_type_t info)
{
    uint8  i = 0;
    uint32 cmd = 0;
    uint32 idx = 0;
    uint32 valid = 0;
    uint32 key_type = 0;
    uint32 tcam_module = 0;
    uint32 expect_offset = 0;
    uint32 real_offset = 0;
    uint32 expect_tbl_idx = 0;
    uint32 result_tbl_idx = 0;
    uint32 tbl_id = 0;

    FlowTcamBistResultMem_m flow_tcam_bist_expcet_mem;
    FlowTcamBistResultMem_m flow_tcam_bist_result_mem;

    LpmTcamIpBistResultMem_m lpm_tcam_ip_bist_expect_mem;
    LpmTcamIpBistResultMem_m lpm_tcam_ip_bist_result_mem;

    LpmTcamNatBistResultMem0_m lpm_tcam_nat_bist_expect_mem;
    LpmTcamNatBistResultMem0_m lpm_tcam_nat_bist_result_mem;

    _ctc_goldengate_dkits_tcam_tbl2module(mem_id, &tcam_module);

    if (DKITS_TCAM_MODULE_TYPE_FLOW == tcam_module)
    {
        for (i = 0; i < entry_num; i++)
        {
            sal_memset(&flow_tcam_bist_expcet_mem, 0, sizeof(FlowTcamBistResultMem_m));
            sal_memset(&flow_tcam_bist_result_mem, 0, sizeof(FlowTcamBistResultMem_m));

            cmd = DRV_IOR(FlowTcamBistResultMem_t, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, i, cmd, &flow_tcam_bist_expcet_mem);
            DRV_IOCTL(lchip, i + 32, cmd, &flow_tcam_bist_result_mem);

            GetFlowTcamBistResultMem(A, index0_f, &flow_tcam_bist_expcet_mem, &expect_offset);
            _ctc_goldengate_dkits_tcam_offset2entry(tbl_id, blk, expect_offset, &expect_offset);
            _ctc_goldengate_dkits_tcam_tbl_index(mem_id, blk, expect_offset, &expect_tbl_idx);

            GetFlowTcamBistResultMem(A, index0_f, &flow_tcam_bist_result_mem, &real_offset);
            _ctc_goldengate_dkits_tcam_offset2entry(tbl_id, blk, real_offset, &real_offset);
            _ctc_goldengate_dkits_tcam_tbl_index(mem_id, blk, real_offset, &result_tbl_idx);

            GetFlowTcamBistResultMem(A, indexValidVec_f, &flow_tcam_bist_result_mem, &valid);

            ctc_goldengate_dkits_tcam_read_tcam_type(mem_id, idx, &key_type);
            ctc_goldengate_dkits_tcam_mem2tbl(mem_id, key_type, &tbl_id);
            _ctc_goldengate_dkits_tcam_bist_show_result(tbl_id, info, expect_tbl_idx, result_tbl_idx);
             /*CTC_DKIT_PRINT("%s %d, tbl name = %s, expect offset = %u, real offset = %u, valid = %u.\n", __FUNCTION__, __LINE__, TABLE_NAME(tbl_id), expect_offset, real_offset, valid);*/
        }
    }
    else if (DKITS_TCAM_MODULE_TYPE_IPPREFIX == tcam_module)
    {
        for (i = 0; i < entry_num; i++)
        {
            sal_memset(&lpm_tcam_ip_bist_expect_mem, 0, sizeof(LpmTcamIpBistResultMem_m));
            sal_memset(&lpm_tcam_ip_bist_result_mem, 0, sizeof(LpmTcamIpBistResultMem_m));

            cmd = DRV_IOR(LpmTcamIpBistResultMem_t, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, i, cmd, &lpm_tcam_ip_bist_expect_mem);
            DRV_IOCTL(lchip, i + 32, cmd, &lpm_tcam_ip_bist_result_mem);

            GetLpmTcamIpBistResultMem(A, index_f, &lpm_tcam_ip_bist_expect_mem, &expect_offset);
            _ctc_goldengate_dkits_tcam_tbl_index(mem_id, blk, expect_offset, &expect_tbl_idx);

            GetLpmTcamIpBistResultMem(A, index_f, &lpm_tcam_ip_bist_result_mem, &real_offset);
            _ctc_goldengate_dkits_tcam_tbl_index(mem_id, blk, real_offset, &result_tbl_idx);

            GetLpmTcamIpBistResultMem(A, indexValid_f, &lpm_tcam_ip_bist_result_mem, &valid);

            ctc_goldengate_dkits_tcam_read_tcam_type(mem_id, idx, &key_type);
            ctc_goldengate_dkits_tcam_mem2tbl(mem_id, key_type, &tbl_id);
            _ctc_goldengate_dkits_tcam_bist_show_result(tbl_id, info, expect_offset, real_offset);
             /*CTC_DKIT_PRINT("%s %d, entry_num = %u, tbl name = %s, expect offset = %u, real offset = %u, valid = %u.\n", __FUNCTION__, __LINE__, entry_num, TABLE_NAME(tbl_id), expect_offset, real_offset, valid);*/
        }
    }
    else if (DKITS_TCAM_MODULE_TYPE_IPNAT == tcam_module)
    {
        for (i = 0; i < entry_num; i++)
        {
            sal_memset(&lpm_tcam_nat_bist_expect_mem, 0, sizeof(LpmTcamNatBistResultMem0_m));
            sal_memset(&lpm_tcam_nat_bist_result_mem, 0, sizeof(LpmTcamNatBistResultMem0_m));

            cmd = DRV_IOR(LpmTcamIpBistResultMem_t, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, i, cmd, &lpm_tcam_nat_bist_expect_mem);
            DRV_IOCTL(lchip, i + 32, cmd, &lpm_tcam_nat_bist_result_mem);

            GetLpmTcamNatBistResultMem2(A, index_f, &lpm_tcam_nat_bist_expect_mem, &expect_offset);
            _ctc_goldengate_dkits_tcam_tbl_index(mem_id, blk, expect_offset, &expect_tbl_idx);

            GetLpmTcamNatBistResultMem2(A, index_f, &lpm_tcam_nat_bist_result_mem, &real_offset);
            _ctc_goldengate_dkits_tcam_tbl_index(mem_id, blk, real_offset, &result_tbl_idx);

            GetLpmTcamNatBistResultMem2(A, indexValid_f, &lpm_tcam_nat_bist_result_mem, &valid);

            ctc_goldengate_dkits_tcam_read_tcam_type(mem_id, idx, &key_type);
            ctc_goldengate_dkits_tcam_mem2tbl(mem_id, key_type, &tbl_id);
            _ctc_goldengate_dkits_tcam_bist_show_result(tbl_id, info, expect_offset, real_offset);
             /*CTC_DKIT_PRINT("%s %d, tbl name = %s, expect offset = %u, real offset = %u, valid = %u.\n", __FUNCTION__, __LINE__, TABLE_NAME(tbl_id), expect_offset, real_offset, valid);*/
        }
    }

    return CLI_SUCCESS;
}


STATIC int32
_ctc_goldengate_dkits_tcam_bist_deliver_result(uint8 lchip, tbls_id_t tblid, uint8 blk, dkits_tcam_module_type_t module, uint8 num, uint32 detail)
{
    uint8 sleep_num = 0;
    ctc_dkits_tcam_interface_status_t status;

    sal_memset(&status, 0, sizeof(ctc_dkits_tcam_interface_status_t));
    while (1)
    {
        _ctc_goldengate_dkits_tcam_bist_detect(lchip, module, &status);
        if (1 == status.bist_result_done)
        {
            _ctc_goldengate_dkits_tcam_bist_present_result(lchip, tblid, blk, num, detail);
            break;
        }
        else
        {
            sleep_num++;
            if (5 == sleep_num)
            {
                break;
            }
            sal_task_sleep(1);
        }
    }

    return 0;
}

STATIC int32
_ctc_goldengate_dkits_tcam_bist_once(uint8 lchip, tbls_id_t tblid, uint32* p_bist_entry, uint32 blk, uint32 detail)
{
    dkits_tcam_module_type_t module = 0;

    /* Bug need consider tbl locate in more than on tcam block */
    _ctc_goldengate_dkits_tcam_tbl2module(tblid, &module);

    _ctc_goldengate_dkits_tcam_bist_start(lchip, module, *p_bist_entry, blk);
    _ctc_goldengate_dkits_tcam_bist_deliver_result(lchip, tblid, blk, module, *p_bist_entry, detail);
    _ctc_goldengate_dkits_tcam_reset_detect(lchip, module);
    *p_bist_entry = 0;

    return CLI_SUCCESS;
}

STATIC int32
_ctc_goldengate_dkits_tcam_bist_write_expect_result(uint8 lchip, tbls_id_t tblid, uint8 mem_idx, uint32 expect_idx)
{
    uint32 cmd = 0;
    uint32 module = 0;
    FlowTcamBistResultMem_m flow_tcam_bist_result_mem;
    LpmTcamIpBistResultMem_m lpm_tcam_ip_bist_result_mem;
    LpmTcamNatBistResultMem0_m lpm_tcam_nat_bist_result_mem;

    _ctc_goldengate_dkits_tcam_tbl2module(tblid, &module);

    if (DKITS_TCAM_MODULE_TYPE_FLOW == module)
    {
        sal_memset(&flow_tcam_bist_result_mem, 0, sizeof(FlowTcamBistResultMem_m));
        SetFlowTcamBistResultMem(V, resultComapreEn_f, &flow_tcam_bist_result_mem, 1);
        SetFlowTcamBistResultMem(V, index0_f, &flow_tcam_bist_result_mem, expect_idx);

        cmd = DRV_IOW(FlowTcamBistResultMem_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, mem_idx, cmd, &flow_tcam_bist_result_mem);
    }
    else if (DKITS_TCAM_MODULE_TYPE_IPPREFIX == module)
    {
        sal_memset(&lpm_tcam_ip_bist_result_mem, 0, sizeof(LpmTcamIpBistResultMem_m));
        SetLpmTcamIpBistResultMem(V, resultCompEn_f, &lpm_tcam_ip_bist_result_mem, 1);
        SetLpmTcamIpBistResultMem(V, index_f, &lpm_tcam_ip_bist_result_mem, expect_idx);

        cmd = DRV_IOW(LpmTcamIpBistResultMem_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, mem_idx, cmd, &lpm_tcam_ip_bist_result_mem);
    }
    else if (DKITS_TCAM_MODULE_TYPE_IPNAT == module)
    {
        sal_memset(&lpm_tcam_nat_bist_result_mem, 0, sizeof(LpmTcamNatBistResultMem0_m));
        SetLpmTcamNatBistResultMem2(V, resultCompEn_f, &lpm_tcam_nat_bist_result_mem, 1);
        SetLpmTcamNatBistResultMem2(V, index_f, &lpm_tcam_nat_bist_result_mem, expect_idx);

        cmd = DRV_IOW(LpmTcamNatBistResultMem2_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, mem_idx, cmd, &lpm_tcam_nat_bist_result_mem);
    }

    return CLI_SUCCESS;
}

STATIC int32
_ctc_goldengate_dkits_tcam_bist_write_lookup_info(uint8 lchip, tbls_id_t tbl_id, tbl_entry_t* p_tcam_key, uint32 index)
{
    uint32 cmd = 0;
    uint32 module = 0;
    FlowTcamBistReqMem_m flow_tcam_bist_req_mem;
    LpmTcamIpBistReqMem_m lpm_tcam_ip_bist_req_mem;
    LpmTcamNatBistReqMem0_m lpm_tcam_nat_bist_req_mem;

    _ctc_goldengate_dkits_tcam_tbl2module(tbl_id, &module);

    if (DKITS_TCAM_MODULE_TYPE_FLOW == module)
    {
        sal_memset(&flow_tcam_bist_req_mem, 0, sizeof(FlowTcamBistReqMem_m));

        if (24 == TCAM_KEY_SIZE(tbl_id))
        {
            SetFlowTcamBistReqMem(V, keySize_f, &flow_tcam_bist_req_mem, 0);
            SetFlowTcamBistReqMem(V, key5_f,  &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[5] & p_tcam_key->mask_entry[5]));
            SetFlowTcamBistReqMem(V, key4_f,  &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[4] & p_tcam_key->mask_entry[4]));
            SetFlowTcamBistReqMem(V, key3_f,  &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[3] & p_tcam_key->mask_entry[3]));

            SetFlowTcamBistReqMem(V, key2_f,  &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[2] & p_tcam_key->mask_entry[2]));
            SetFlowTcamBistReqMem(V, key1_f,  &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[1] & p_tcam_key->mask_entry[1]));
            SetFlowTcamBistReqMem(V, key0_f,  &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[0] & p_tcam_key->mask_entry[0]));

            SetFlowTcamBistReqMem(V, key11_f, &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[5] & p_tcam_key->mask_entry[5]));
            SetFlowTcamBistReqMem(V, key10_f, &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[4] & p_tcam_key->mask_entry[4]));
            SetFlowTcamBistReqMem(V, key9_f,  &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[3] & p_tcam_key->mask_entry[3]));

            SetFlowTcamBistReqMem(V, key8_f,  &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[2] & p_tcam_key->mask_entry[2]));
            SetFlowTcamBistReqMem(V, key7_f,  &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[1] & p_tcam_key->mask_entry[1]));
            SetFlowTcamBistReqMem(V, key6_f,  &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[0] & p_tcam_key->mask_entry[0]));

            SetFlowTcamBistReqMem(V, key17_f, &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[5] & p_tcam_key->mask_entry[5]));
            SetFlowTcamBistReqMem(V, key16_f, &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[4] & p_tcam_key->mask_entry[4]));
            SetFlowTcamBistReqMem(V, key15_f, &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[3] & p_tcam_key->mask_entry[3]));

            SetFlowTcamBistReqMem(V, key14_f, &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[2] & p_tcam_key->mask_entry[2]));
            SetFlowTcamBistReqMem(V, key13_f, &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[1] & p_tcam_key->mask_entry[1]));
            SetFlowTcamBistReqMem(V, key12_f, &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[0] & p_tcam_key->mask_entry[0]));

            SetFlowTcamBistReqMem(V, key23_f, &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[5] & p_tcam_key->mask_entry[5]));
            SetFlowTcamBistReqMem(V, key22_f, &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[4] & p_tcam_key->mask_entry[4]));
            SetFlowTcamBistReqMem(V, key21_f, &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[3] & p_tcam_key->mask_entry[3]));

            SetFlowTcamBistReqMem(V, key20_f, &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[2] & p_tcam_key->mask_entry[2]));
            SetFlowTcamBistReqMem(V, key19_f, &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[1] & p_tcam_key->mask_entry[1]));
            SetFlowTcamBistReqMem(V, key18_f, &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[0] & p_tcam_key->mask_entry[0]));
        }
        else if (48 == TCAM_KEY_SIZE(tbl_id))
        {
            SetFlowTcamBistReqMem(V, keySize_f, &flow_tcam_bist_req_mem, 1);
            SetFlowTcamBistReqMem(V, key11_f, &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[11] & p_tcam_key->mask_entry[11]));
            SetFlowTcamBistReqMem(V, key10_f, &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[10] & p_tcam_key->mask_entry[10]));
            SetFlowTcamBistReqMem(V, key9_f,  &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[9] & p_tcam_key->mask_entry[9]));

            SetFlowTcamBistReqMem(V, key8_f,  &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[8] & p_tcam_key->mask_entry[8]));
            SetFlowTcamBistReqMem(V, key7_f,  &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[7] & p_tcam_key->mask_entry[7]));
            SetFlowTcamBistReqMem(V, key6_f,  &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[6] & p_tcam_key->mask_entry[6]));

            SetFlowTcamBistReqMem(V, key5_f,  &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[5] & p_tcam_key->mask_entry[5]));
            SetFlowTcamBistReqMem(V, key4_f,  &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[4] & p_tcam_key->mask_entry[4]));
            SetFlowTcamBistReqMem(V, key3_f,  &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[3] & p_tcam_key->mask_entry[3]));

            SetFlowTcamBistReqMem(V, key2_f,  &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[2] & p_tcam_key->mask_entry[2]));
            SetFlowTcamBistReqMem(V, key1_f,  &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[1] & p_tcam_key->mask_entry[1]));
            SetFlowTcamBistReqMem(V, key0_f,  &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[0] & p_tcam_key->mask_entry[0]));

            SetFlowTcamBistReqMem(V, key23_f, &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[11] & p_tcam_key->mask_entry[11]));
            SetFlowTcamBistReqMem(V, key22_f, &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[10] & p_tcam_key->mask_entry[10]));
            SetFlowTcamBistReqMem(V, key21_f, &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[9] & p_tcam_key->mask_entry[9]));

            SetFlowTcamBistReqMem(V, key20_f, &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[8] & p_tcam_key->mask_entry[8]));
            SetFlowTcamBistReqMem(V, key19_f, &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[7] & p_tcam_key->mask_entry[7]));
            SetFlowTcamBistReqMem(V, key18_f, &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[6] & p_tcam_key->mask_entry[6]));

            SetFlowTcamBistReqMem(V, key17_f, &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[5] & p_tcam_key->mask_entry[5]));
            SetFlowTcamBistReqMem(V, key16_f, &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[4] & p_tcam_key->mask_entry[4]));
            SetFlowTcamBistReqMem(V, key15_f, &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[3] & p_tcam_key->mask_entry[3]));

            SetFlowTcamBistReqMem(V, key14_f, &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[2] & p_tcam_key->mask_entry[2]));
            SetFlowTcamBistReqMem(V, key13_f, &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[1] & p_tcam_key->mask_entry[1]));
            SetFlowTcamBistReqMem(V, key12_f, &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[0] & p_tcam_key->mask_entry[0]));
        }
        else if (96 == TCAM_KEY_SIZE(tbl_id))
        {
            SetFlowTcamBistReqMem(V, keySize_f, &flow_tcam_bist_req_mem, 2);
            SetFlowTcamBistReqMem(V, key23_f, &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[23] & p_tcam_key->mask_entry[23]));
            SetFlowTcamBistReqMem(V, key22_f, &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[22] & p_tcam_key->mask_entry[22]));
            SetFlowTcamBistReqMem(V, key21_f, &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[21] & p_tcam_key->mask_entry[21]));

            SetFlowTcamBistReqMem(V, key20_f, &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[20] & p_tcam_key->mask_entry[20]));
            SetFlowTcamBistReqMem(V, key19_f, &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[19] & p_tcam_key->mask_entry[19]));
            SetFlowTcamBistReqMem(V, key18_f, &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[18] & p_tcam_key->mask_entry[18]));

            SetFlowTcamBistReqMem(V, key17_f, &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[17] & p_tcam_key->mask_entry[17]));
            SetFlowTcamBistReqMem(V, key16_f, &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[16] & p_tcam_key->mask_entry[16]));
            SetFlowTcamBistReqMem(V, key15_f, &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[15] & p_tcam_key->mask_entry[15]));

            SetFlowTcamBistReqMem(V, key14_f, &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[14] & p_tcam_key->mask_entry[14]));
            SetFlowTcamBistReqMem(V, key13_f, &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[13] & p_tcam_key->mask_entry[13]));
            SetFlowTcamBistReqMem(V, key12_f, &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[12] & p_tcam_key->mask_entry[12]));

            SetFlowTcamBistReqMem(V, key11_f, &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[11] & p_tcam_key->mask_entry[11]));
            SetFlowTcamBistReqMem(V, key10_f, &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[10] & p_tcam_key->mask_entry[10]));
            SetFlowTcamBistReqMem(V, key9_f,  &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[9] & p_tcam_key->mask_entry[9]));

            SetFlowTcamBistReqMem(V, key8_f,  &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[8] & p_tcam_key->mask_entry[8]));
            SetFlowTcamBistReqMem(V, key7_f,  &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[7] & p_tcam_key->mask_entry[7]));
            SetFlowTcamBistReqMem(V, key6_f,  &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[6] & p_tcam_key->mask_entry[6]));

            SetFlowTcamBistReqMem(V, key5_f,  &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[5] & p_tcam_key->mask_entry[5]));
            SetFlowTcamBistReqMem(V, key4_f,  &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[4] & p_tcam_key->mask_entry[4]));
            SetFlowTcamBistReqMem(V, key3_f,  &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[3] & p_tcam_key->mask_entry[3]));

            SetFlowTcamBistReqMem(V, key2_f,  &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[2] & p_tcam_key->mask_entry[2]));
            SetFlowTcamBistReqMem(V, key1_f,  &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[1] & p_tcam_key->mask_entry[1]));
            SetFlowTcamBistReqMem(V, key0_f,  &flow_tcam_bist_req_mem, (p_tcam_key->data_entry[0] & p_tcam_key->mask_entry[0]));
        }

        cmd = DRV_IOW(FlowTcamBistReqMem_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, index, cmd, &flow_tcam_bist_req_mem);
    }
    else if (DKITS_TCAM_MODULE_TYPE_IPPREFIX == module)
    {
        sal_memset(&lpm_tcam_ip_bist_req_mem, 0, sizeof(LpmTcamIpBistReqMem_m));

        if (8 == TCAM_KEY_SIZE(tbl_id))
        {
            SetLpmTcamIpBistReqMem(V, keySize_f, &lpm_tcam_ip_bist_req_mem, 0);

            SetLpmTcamIpBistReqMem(V, key7_f, &lpm_tcam_ip_bist_req_mem, (p_tcam_key->data_entry[1] & p_tcam_key->mask_entry[1]));
            SetLpmTcamIpBistReqMem(V, key6_f, &lpm_tcam_ip_bist_req_mem, (p_tcam_key->data_entry[0] & p_tcam_key->mask_entry[0]));

            SetLpmTcamIpBistReqMem(V, key5_f, &lpm_tcam_ip_bist_req_mem, (p_tcam_key->data_entry[1] & p_tcam_key->mask_entry[1]));
            SetLpmTcamIpBistReqMem(V, key4_f, &lpm_tcam_ip_bist_req_mem, (p_tcam_key->data_entry[0] & p_tcam_key->mask_entry[0]));

            SetLpmTcamIpBistReqMem(V, key3_f, &lpm_tcam_ip_bist_req_mem, (p_tcam_key->data_entry[1] & p_tcam_key->mask_entry[1]));
            SetLpmTcamIpBistReqMem(V, key2_f, &lpm_tcam_ip_bist_req_mem, (p_tcam_key->data_entry[0] & p_tcam_key->mask_entry[0]));

            SetLpmTcamIpBistReqMem(V, key1_f, &lpm_tcam_ip_bist_req_mem, (p_tcam_key->data_entry[1] & p_tcam_key->mask_entry[1]));
            SetLpmTcamIpBistReqMem(V, key0_f, &lpm_tcam_ip_bist_req_mem, (p_tcam_key->data_entry[0] & p_tcam_key->mask_entry[0]));
        }
        else if (32 == TCAM_KEY_SIZE(tbl_id))
        {
            SetLpmTcamIpBistReqMem(V, keySize_f, &lpm_tcam_ip_bist_req_mem, 2);

            SetLpmTcamIpBistReqMem(V, key7_f, &lpm_tcam_ip_bist_req_mem, (p_tcam_key->data_entry[1] & p_tcam_key->mask_entry[1]));
            SetLpmTcamIpBistReqMem(V, key6_f, &lpm_tcam_ip_bist_req_mem, (p_tcam_key->data_entry[0] & p_tcam_key->mask_entry[0]));

            SetLpmTcamIpBistReqMem(V, key5_f, &lpm_tcam_ip_bist_req_mem, (p_tcam_key->data_entry[3] & p_tcam_key->mask_entry[3]));
            SetLpmTcamIpBistReqMem(V, key4_f, &lpm_tcam_ip_bist_req_mem, (p_tcam_key->data_entry[2] & p_tcam_key->mask_entry[2]));

            SetLpmTcamIpBistReqMem(V, key3_f, &lpm_tcam_ip_bist_req_mem, (p_tcam_key->data_entry[5] & p_tcam_key->mask_entry[5]));
            SetLpmTcamIpBistReqMem(V, key2_f, &lpm_tcam_ip_bist_req_mem, (p_tcam_key->data_entry[4] & p_tcam_key->mask_entry[4]));

            SetLpmTcamIpBistReqMem(V, key1_f, &lpm_tcam_ip_bist_req_mem, (p_tcam_key->data_entry[7] & p_tcam_key->mask_entry[7]));
            SetLpmTcamIpBistReqMem(V, key0_f, &lpm_tcam_ip_bist_req_mem, (p_tcam_key->data_entry[6] & p_tcam_key->mask_entry[6]));
        }

        cmd = DRV_IOW(LpmTcamIpBistReqMem_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, index, cmd, &lpm_tcam_ip_bist_req_mem);
    }
    else if (DKITS_TCAM_MODULE_TYPE_IPNAT == module)
    {
        sal_memset(&lpm_tcam_nat_bist_req_mem, 0, sizeof(LpmTcamNatBistReqMem0_m));

        if (24 == TCAM_KEY_SIZE(tbl_id))
        {
            SetLpmTcamNatBistReqMem2(V, keySize_f, &lpm_tcam_nat_bist_req_mem, 0);

            SetLpmTcamNatBistReqMem2(V, key5_f,  &lpm_tcam_nat_bist_req_mem, (p_tcam_key->data_entry[5] & p_tcam_key->mask_entry[5]));
            SetLpmTcamNatBistReqMem2(V, key4_f,  &lpm_tcam_nat_bist_req_mem, (p_tcam_key->data_entry[4] & p_tcam_key->mask_entry[4]));
            SetLpmTcamNatBistReqMem2(V, key3_f,  &lpm_tcam_nat_bist_req_mem, (p_tcam_key->data_entry[3] & p_tcam_key->mask_entry[3]));
            SetLpmTcamNatBistReqMem2(V, key2_f,  &lpm_tcam_nat_bist_req_mem, (p_tcam_key->data_entry[2] & p_tcam_key->mask_entry[2]));
            SetLpmTcamNatBistReqMem2(V, key1_f,  &lpm_tcam_nat_bist_req_mem, (p_tcam_key->data_entry[1] & p_tcam_key->mask_entry[1]));
            SetLpmTcamNatBistReqMem2(V, key0_f,  &lpm_tcam_nat_bist_req_mem, (p_tcam_key->data_entry[0] & p_tcam_key->mask_entry[0]));

            SetLpmTcamNatBistReqMem2(V, key11_f, &lpm_tcam_nat_bist_req_mem, (p_tcam_key->data_entry[5] & p_tcam_key->mask_entry[5]));
            SetLpmTcamNatBistReqMem2(V, key10_f, &lpm_tcam_nat_bist_req_mem, (p_tcam_key->data_entry[4] & p_tcam_key->mask_entry[4]));
            SetLpmTcamNatBistReqMem2(V, key9_f,  &lpm_tcam_nat_bist_req_mem, (p_tcam_key->data_entry[3] & p_tcam_key->mask_entry[3]));
            SetLpmTcamNatBistReqMem2(V, key8_f,  &lpm_tcam_nat_bist_req_mem, (p_tcam_key->data_entry[2] & p_tcam_key->mask_entry[2]));
            SetLpmTcamNatBistReqMem2(V, key7_f,  &lpm_tcam_nat_bist_req_mem, (p_tcam_key->data_entry[1] & p_tcam_key->mask_entry[1]));
            SetLpmTcamNatBistReqMem2(V, key6_f,  &lpm_tcam_nat_bist_req_mem, (p_tcam_key->data_entry[0] & p_tcam_key->mask_entry[0]));
        }
        else if (48 == TCAM_KEY_SIZE(tbl_id))
        {
            SetLpmTcamNatBistReqMem2(V, keySize_f, &lpm_tcam_nat_bist_req_mem, 1);

            SetLpmTcamNatBistReqMem2(V, key11_f, &lpm_tcam_nat_bist_req_mem, (p_tcam_key->data_entry[11] & p_tcam_key->mask_entry[11]));
            SetLpmTcamNatBistReqMem2(V, key10_f, &lpm_tcam_nat_bist_req_mem, (p_tcam_key->data_entry[10] & p_tcam_key->mask_entry[10]));
            SetLpmTcamNatBistReqMem2(V, key9_f,  &lpm_tcam_nat_bist_req_mem, (p_tcam_key->data_entry[9] & p_tcam_key->mask_entry[9]));
            SetLpmTcamNatBistReqMem2(V, key8_f,  &lpm_tcam_nat_bist_req_mem, (p_tcam_key->data_entry[8] & p_tcam_key->mask_entry[8]));
            SetLpmTcamNatBistReqMem2(V, key7_f,  &lpm_tcam_nat_bist_req_mem, (p_tcam_key->data_entry[7] & p_tcam_key->mask_entry[7]));
            SetLpmTcamNatBistReqMem2(V, key6_f,  &lpm_tcam_nat_bist_req_mem, (p_tcam_key->data_entry[6] & p_tcam_key->mask_entry[6]));
            SetLpmTcamNatBistReqMem2(V, key5_f,  &lpm_tcam_nat_bist_req_mem, (p_tcam_key->data_entry[5] & p_tcam_key->mask_entry[5]));
            SetLpmTcamNatBistReqMem2(V, key4_f,  &lpm_tcam_nat_bist_req_mem, (p_tcam_key->data_entry[4] & p_tcam_key->mask_entry[4]));
            SetLpmTcamNatBistReqMem2(V, key3_f,  &lpm_tcam_nat_bist_req_mem, (p_tcam_key->data_entry[3] & p_tcam_key->mask_entry[3]));
            SetLpmTcamNatBistReqMem2(V, key2_f,  &lpm_tcam_nat_bist_req_mem, (p_tcam_key->data_entry[2] & p_tcam_key->mask_entry[2]));
            SetLpmTcamNatBistReqMem2(V, key1_f,  &lpm_tcam_nat_bist_req_mem, (p_tcam_key->data_entry[1] & p_tcam_key->mask_entry[1]));
            SetLpmTcamNatBistReqMem2(V, key0_f,  &lpm_tcam_nat_bist_req_mem, (p_tcam_key->data_entry[0] & p_tcam_key->mask_entry[0]));
        }

        cmd = DRV_IOW(LpmTcamNatBistReqMem2_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, index, cmd, &lpm_tcam_nat_bist_req_mem);
    }

    return CLI_SUCCESS;
}

STATIC int32
_ctc_goldengate_dkits_tcam_bist_build_request(uint8 lchip, uint32 tbl_id, uint32 tbl_idx, tbl_entry_t* p_tcam_key, uint32 mem_idx, uint32* p_blk)
{
    uint32 entry_offset = 0;
    uint32 bist_offset = 0;

    ctc_goldengate_dkits_tcam_entry_offset(tbl_id, tbl_idx, &entry_offset, p_blk);
    _ctc_goldengate_dkits_tcam_entry2offset(tbl_id, *p_blk, entry_offset, &bist_offset);
    _ctc_goldengate_dkits_tcam_bist_write_expect_result(lchip, tbl_id, mem_idx, bist_offset);
    _ctc_goldengate_dkits_tcam_bist_write_lookup_info(lchip, tbl_id, p_tcam_key, mem_idx);

    return CLI_SUCCESS;
}

STATIC int32
_ctc_goldengate_dkits_tcam_bist_scan_key_index(uint8 lchip, uint32 tblid, uint32 idx, uint32* p_num, dkits_tcam_scan_info_type_t info)
{
    ds_t    data;
    ds_t    mask;
    tbl_entry_t tcam_key;
    uint32  empty = 0;
    uint32  blk = 0;

    sal_memset(&data, 0, sizeof(data));
    sal_memset(&mask, 0, sizeof(mask));

    tcam_key.data_entry = (uint32 *)&data;
    tcam_key.mask_entry = (uint32 *)&mask;

    ctc_goldengate_dkits_tcam_read_tcam_key(lchip, tblid, idx, &empty, &tcam_key);
    if (!empty)
    {
        _ctc_goldengate_dkits_tcam_bist_build_request(lchip, tblid, idx, &tcam_key, (*p_num), &blk);
        (*p_num)++;

        if ((!((*p_num) % DKITS_TCAM_DETECT_MEM_NUM))
           || (idx == (TABLE_MAX_INDEX(tblid) - 1))
           || (DKITS_TCAM_SCAN_INFO_TYPE_DETAIL == info))
        {
            _ctc_goldengate_dkits_tcam_bist_once(lchip, tblid, p_num, blk, info);
        }
    }
    else
    {
        if (DKITS_TCAM_SCAN_INFO_TYPE_DETAIL == info)
        {
            CTC_DKIT_PRINT(" Tcam key:%s index:%u is empty!\n\n", TABLE_NAME(tblid), idx);
            return CLI_SUCCESS;
        }
    }

    if ((idx == (TABLE_MAX_INDEX(tblid) - 1)) && (0 != *p_num))
    {
        _ctc_goldengate_dkits_tcam_bist_once(lchip, tblid, p_num, blk, info);
    }

    return CLI_SUCCESS;
}

STATIC int32
_ctc_goldengate_dkits_tcam_bist_scan_tcam_tbl(uint8 lchip, uint32 tblid, dkits_tcam_scan_info_type_t info)
{
    uint32 i = 0;
    uint32 num = 0;

    for (i = 0; i < TABLE_MAX_INDEX(tblid); i++)
    {
        _ctc_goldengate_dkits_tcam_bist_scan_key_index(lchip, tblid, i, &num, info);
    }

    return CLI_SUCCESS;
}

STATIC int32
_ctc_goldengate_dkits_tcam_bist_scan_tcam_type(uint8 lchip,uint32 scan_idx, dkits_tcam_block_type_t tcam_type, tbls_id_t tblid, uint32 key_index)
{
    uint32  num = 0;
    uint32  i = 0;
    tbls_id_t tblid_ = MaxTblId_t - 1;
    dkits_tcam_module_type_t module;
    static tbls_id_t tcam_mem[DKITS_TCAM_BLOCK_TYPE_NUM][DKITS_TCAM_PER_TYPE_LKP_NUM] =
    {
        {DsAclQosL3Key160Ingr0_t,  DsAclQosL3Key320Ingr0_t, DsAclQosIpv6Key640Ingr0_t, MaxTblId_t},
        {DsAclQosL3Key160Ingr1_t,  DsAclQosL3Key320Ingr1_t, DsAclQosIpv6Key640Ingr1_t, MaxTblId_t},
        {DsAclQosL3Key160Ingr2_t,  DsAclQosL3Key320Ingr2_t, DsAclQosIpv6Key640Ingr2_t, MaxTblId_t},
        {DsAclQosL3Key160Ingr3_t,  DsAclQosL3Key320Ingr3_t, DsAclQosIpv6Key640Ingr3_t, MaxTblId_t},
        {DsAclQosL3Key160Egr0_t,   DsAclQosL3Key320Egr0_t,  DsAclQosIpv6Key640Egr0_t,  MaxTblId_t},
        {DsUserId0L3Key160_t,      DsUserId0L3Key320_t,     DsUserId0Ipv6Key640_t,     MaxTblId_t},
        {DsUserId1L3Key160_t,      DsUserId1L3Key320_t,     DsUserId1Ipv6Key640_t,     MaxTblId_t},
        {DsLpmTcamIpv440Key_t,     DsLpmTcamIpv6160Key0_t,  MaxTblId_t},
        {DsLpmTcamIpv4NAT160Key_t, DsLpmTcamIpv6160Key1_t,  DsLpmTcamIpv4Pbr160Key_t, DsLpmTcamIpv6320Key_t}
    };

    _ctc_goldengate_dkits_tcam_tbl2module(tblid, &module);
    _ctc_goldengate_dkits_tcam_reset_detect(lchip, module);

    if (MaxTblId_t != tblid)
    {
        if (CTC_DKITS_INVALID_INDEX != key_index)
        {
            if (key_index > TABLE_MAX_INDEX(tblid))
            {
                CTC_DKIT_PRINT(" Tcam key %s's max index is %u, %u is invalid index\n\n",
                               TABLE_NAME(tblid), (TABLE_MAX_INDEX(tblid) - 1), key_index);
                return CLI_SUCCESS;
            }
            CTC_DKIT_PRINT(" Scan tcam tbl: %s, index: %u\n", TABLE_NAME(tblid), key_index);
            _ctc_goldengate_dkits_tcam_bist_scan_key_index(lchip, tblid, key_index, &num, DKITS_TCAM_SCAN_INFO_TYPE_DETAIL);
        }
        else
        {
            _ctc_goldengate_dkits_tcam_bist_tbl_brief_status(&tblid_, 0);
            CTC_DKIT_PRINT(" Scan tcam tbl: %s\n", TABLE_NAME(tblid));
            _ctc_goldengate_dkits_tcam_bist_scan_tcam_tbl(lchip, tblid, DKITS_TCAM_SCAN_INFO_TYPE_GENERAL);
            _ctc_goldengate_dkits_tcam_bist_tbl_brief_status(&tblid_, 1);
            if (MaxTblId_t == tblid_)
            {
                CTC_DKIT_PRINT("    Result fail!\n");
                tblid_ = MaxTblId_t - 1;
                _ctc_goldengate_dkits_tcam_bist_tbl_brief_status(&tblid_, 0);
            }
            else
            {
                CTC_DKIT_PRINT("    Result pass!\n");
            }
            CTC_DKIT_PRINT("\n");
        }
    }
    else
    {
        for (i = 0; (i < DKITS_TCAM_PER_TYPE_LKP_NUM) && (MaxTblId_t != tcam_mem[tcam_type][i]); i++)
        {
            _ctc_goldengate_dkits_tcam_bist_scan_tcam_tbl(lchip, tcam_mem[tcam_type][i], DKITS_TCAM_SCAN_INFO_TYPE_BRIEF);
        }
    }

    return CLI_SUCCESS;
}

#define __3_CAPTURE__

STATIC int32
_ctc_goldengate_dkits_tcam_capture_get_tbl_property(uint8 lchip, dkits_tcam_module_type_t module_type, dkits_tcam_capture_tbl_property_t tbl_property, uint32* p_value)
{
    uint32 cmd = 0;
    FlowTcamBistCtl_m flow_tcam_bist_ctl;
    LpmTcamIpBistCtl_m lpm_tcam_ip_bist_ctl;
    LpmTcamNatBistCtl0_m lpm_tcam_nat_bist_ctl;

    sal_memset(&flow_tcam_bist_ctl, 0, sizeof(FlowTcamBistCtl_m));
    sal_memset(&lpm_tcam_ip_bist_ctl, 0, sizeof(LpmTcamIpBistCtl_m));
    sal_memset(&lpm_tcam_ip_bist_ctl, 0, sizeof(LpmTcamNatBistCtl0_m));

    if (DKITS_TCAM_MODULE_TYPE_FLOW == module_type)
    {
        cmd = DRV_IOR(FlowTcamBistCtl_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &flow_tcam_bist_ctl);

        if (DKITS_TCAM_CAPTURE_TBL_CAPTURE_EN == tbl_property)
        {
            GetFlowTcamBistCtl(A, cfgCaptureEn_f, &flow_tcam_bist_ctl, p_value);
        }
        else if (DKITS_TCAM_CAPTURE_TBL_CAPTURE_ONCE == tbl_property)
        {
            GetFlowTcamBistCtl(A, cfgCaptureOnce_f, &flow_tcam_bist_ctl, p_value);
        }
        else if (DKITS_TCAM_CAPTURE_TBL_CAPTURE_VEC == tbl_property)
        {
            GetFlowTcamBistCtl(A, cfgCaptureVec_f, &flow_tcam_bist_ctl, p_value);
        }
        else if (DKITS_TCAM_CAPTURE_TBL_STOP_ON_ERROR == tbl_property)
        {
            GetFlowTcamBistCtl(A, cfgStopOnError_f, &flow_tcam_bist_ctl, p_value);
        }
    }
    else if (DKITS_TCAM_MODULE_TYPE_IPPREFIX == module_type)
    {
        cmd = DRV_IOR(LpmTcamIpBistCtl_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &lpm_tcam_ip_bist_ctl);

        if (DKITS_TCAM_CAPTURE_TBL_CAPTURE_EN == tbl_property)
        {
            GetLpmTcamIpBistCtl(A, cfgCaptureEn_f, &lpm_tcam_ip_bist_ctl, p_value);
        }
        else if (DKITS_TCAM_CAPTURE_TBL_CAPTURE_ONCE == tbl_property)
        {
            GetLpmTcamIpBistCtl(A, cfgCaptureOnce_f, &lpm_tcam_ip_bist_ctl, p_value);
        }
        else if (DKITS_TCAM_CAPTURE_TBL_STOP_ON_ERROR == tbl_property)
        {
            GetLpmTcamIpBistCtl(A, cfgStopOnError_f, &lpm_tcam_ip_bist_ctl, p_value);
        }
    }
    else if (DKITS_TCAM_MODULE_TYPE_IPNAT == module_type)
    {
        cmd = DRV_IOR(LpmTcamNatBistCtl2_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &lpm_tcam_nat_bist_ctl);

        if (DKITS_TCAM_CAPTURE_TBL_CAPTURE_EN == tbl_property)
        {
            GetLpmTcamNatBistCtl2(A, cfgCaptureEn_f, &lpm_tcam_nat_bist_ctl, p_value);
        }
        else if (DKITS_TCAM_CAPTURE_TBL_CAPTURE_ONCE == tbl_property)
        {
            GetLpmTcamNatBistCtl2(A, cfgCaptureOnce_f, &lpm_tcam_nat_bist_ctl, p_value);
        }
        else if (DKITS_TCAM_CAPTURE_TBL_STOP_ON_ERROR == tbl_property)
        {
            GetLpmTcamNatBistCtl2(A, cfgStopOnError_f, &lpm_tcam_nat_bist_ctl, p_value);
        }
    }

    return CLI_SUCCESS;
}

STATIC int32
_ctc_goldengate_dkits_tcam_capture_set_tbl_property(uint8 lchip, dkits_tcam_module_type_t module_type, dkits_tcam_capture_tbl_property_t tbl_property, uint32 value)
{
    uint32 cmd = 0;
    FlowTcamBistCtl_m flow_tcam_bist_ctl;
    LpmTcamIpBistCtl_m lpm_tcam_ip_bist_ctl;
    LpmTcamNatBistCtl0_m lpm_tcam_nat_bist_ctl;

    sal_memset(&flow_tcam_bist_ctl, 0, sizeof(FlowTcamBistCtl_m));
    sal_memset(&lpm_tcam_ip_bist_ctl, 0, sizeof(LpmTcamIpBistCtl_m));
    sal_memset(&lpm_tcam_ip_bist_ctl, 0, sizeof(LpmTcamNatBistCtl0_m));

    if (DKITS_TCAM_MODULE_TYPE_FLOW == module_type)
    {
        cmd = DRV_IOR(FlowTcamBistCtl_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &flow_tcam_bist_ctl);

        if (DKITS_TCAM_CAPTURE_TBL_CAPTURE_EN == tbl_property)
        {
            SetFlowTcamBistCtl(V, cfgCaptureEn_f, &flow_tcam_bist_ctl, value);
        }
        else if (DKITS_TCAM_CAPTURE_TBL_CAPTURE_ONCE == tbl_property)
        {
            SetFlowTcamBistCtl(V, cfgCaptureOnce_f, &flow_tcam_bist_ctl, value);
        }
        else if (DKITS_TCAM_CAPTURE_TBL_CAPTURE_VEC == tbl_property)
        {
            SetFlowTcamBistCtl(V, cfgCaptureVec_f, &flow_tcam_bist_ctl, value);
        }
        else if (DKITS_TCAM_CAPTURE_TBL_STOP_ON_ERROR == tbl_property)
        {
            SetFlowTcamBistCtl(V, cfgStopOnError_f, &flow_tcam_bist_ctl, value);
        }

        cmd = DRV_IOW(FlowTcamBistCtl_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &flow_tcam_bist_ctl);
    }
    else if (DKITS_TCAM_MODULE_TYPE_IPPREFIX == module_type)
    {
        cmd = DRV_IOR(LpmTcamIpBistCtl_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &lpm_tcam_ip_bist_ctl);

        if (DKITS_TCAM_CAPTURE_TBL_CAPTURE_EN == tbl_property)
        {
            SetLpmTcamIpBistCtl(V, cfgCaptureEn_f, &lpm_tcam_ip_bist_ctl, value);
        }
        else if (DKITS_TCAM_CAPTURE_TBL_CAPTURE_ONCE == tbl_property)
        {
            SetLpmTcamIpBistCtl(V, cfgCaptureOnce_f, &lpm_tcam_ip_bist_ctl, value);
        }
        else if (DKITS_TCAM_CAPTURE_TBL_STOP_ON_ERROR == tbl_property)
        {
            SetLpmTcamIpBistCtl(V, cfgStopOnError_f, &lpm_tcam_ip_bist_ctl, value);
        }

        cmd = DRV_IOW(LpmTcamIpBistCtl_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &lpm_tcam_ip_bist_ctl);
    }
    else if (DKITS_TCAM_MODULE_TYPE_IPNAT == module_type)
    {
        cmd = DRV_IOR(LpmTcamNatBistCtl2_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &lpm_tcam_nat_bist_ctl);

        if (DKITS_TCAM_CAPTURE_TBL_CAPTURE_EN == tbl_property)
        {
            SetLpmTcamNatBistCtl2(V, cfgCaptureEn_f, &lpm_tcam_nat_bist_ctl, value);
        }
        else if (DKITS_TCAM_CAPTURE_TBL_CAPTURE_ONCE == tbl_property)
        {
            SetLpmTcamNatBistCtl2(V, cfgCaptureOnce_f, &lpm_tcam_nat_bist_ctl, value);
        }
        else if (DKITS_TCAM_CAPTURE_TBL_STOP_ON_ERROR == tbl_property)
        {
            SetLpmTcamNatBistCtl2(V, cfgStopOnError_f, &lpm_tcam_nat_bist_ctl, value);
        }

        cmd = DRV_IOW(LpmTcamNatBistCtl2_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &lpm_tcam_nat_bist_ctl);
    }

    return CLI_SUCCESS;
}

STATIC int32
_ctc_goldengate_dkits_tcam_capture_present_result(dkits_tcam_capture_info_t* p_capture_info, uint32* p_idx, uint32 num, uint32 idx)
{
    uint32 i = 0, j = 0;
    char desc[50] = {0};

    for (i = 0; i < num; i++)
    {
        for (j = 0; j < DKITS_TCAM_PER_TYPE_LKP_NUM; j++)
        {
            if (1 == p_capture_info->tbl[i][j].lookup_valid)
            {
                _ctc_goldengate_dkits_tcam_tbl2desc(p_capture_info->tbl[i][j].id, desc, sizeof(desc));

                if ((0xFFFFFFFF == idx) || ((0xFFFFFFFF != idx) && (++(*p_idx) == idx)))
                {
                    if (1 == p_capture_info->tbl[i][j].result_valid)
                    {
                        CTC_DKIT_PRINT("%-6u%-27s%-28s%-15s%u\n",
                                   ++(*p_idx), desc, TABLE_NAME(p_capture_info->tbl[i][j].id),
                                   "True", p_capture_info->tbl[i][j].idx);
                    }
                    else
                    {
                        CTC_DKIT_PRINT("%-6u%-27s%-28s%-15s%s\n",
                                   ++(*p_idx), desc, TABLE_NAME(p_capture_info->tbl[i][j].id),
                                   "False", "-");
                    }
                }
            }
        }
    }

    return CLI_SUCCESS;
}

STATIC int32
_ctc_goldengate_dkits_tcam_capture_parser_flow_tcam_mem(dkits_tcam_capture_pr_t* p_capture_pr, dkits_tcam_lookup_info_t* p_lookup_info, tbls_id_t* p_mem_id)
{
    uint8  lchip = 0;
    uint32 cmd = 0;
    uint32 l3_merge_key = 0;
    uint32 ip_header_err_lookup_mode = 0;
    uint32 l2_key_use160 = 0;

    FlowTcamLookupCtl_m flow_tcam_lookup_ctl;
    sal_memset(&flow_tcam_lookup_ctl, 0, sizeof(FlowTcamLookupCtl_m));

    cmd = DRV_IOR(FlowTcamLookupCtl_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &flow_tcam_lookup_ctl);

    GetFlowTcamLookupCtl(A, l3MergeKey_f, &flow_tcam_lookup_ctl, &l3_merge_key);
    GetFlowTcamLookupCtl(A, ipHeaderErrLookupMode_f, &flow_tcam_lookup_ctl, &ip_header_err_lookup_mode);
    GetFlowTcamLookupCtl(A, l2KeyUse160_f, &flow_tcam_lookup_ctl, &l2_key_use160);

    if (DKITS_FLOW_TCAM_LOOKUP_TYPE_L2L3KEY == p_lookup_info->acl_lookup_type)
    {
        if (L3TYPE_IPV6 == p_capture_pr->layer3_type)
        {
            *p_mem_id = DsAclQosIpv6Key640_t;
        }
        else
        {
            *p_mem_id = DsAclQosL3Key320_t;
        }
    }
    else if (DKITS_FLOW_TCAM_LOOKUP_TYPE_L3KEY == p_lookup_info->acl_lookup_type)
    {
        if (L3TYPE_IPV6 == p_capture_pr->layer3_type)
        {
            *p_mem_id = DsAclQosIpv6Key640_t;
        }
        else if (DKITS_FLAG_ISSET(l3_merge_key, p_capture_pr->layer3_type))
        {
            *p_mem_id = DsAclQosL3Key320_t;
        }
        else if ((L3TYPE_IPV4 != p_capture_pr->layer3_type) || !p_capture_pr->ip_header_error)
        {
            *p_mem_id = DsAclQosL3Key160_t;
        }
        else if (ip_header_err_lookup_mode)
        {
            *p_mem_id = DsAclQosL3Key320_t;
        }
        else
        {
            *p_mem_id = DsAclQosL3Key160_t;
        }
    }
    else if (DKITS_FLOW_TCAM_LOOKUP_TYPE_VLANKEY == p_lookup_info->acl_lookup_type)
    {
        *p_mem_id = DsAclQosMacKey160_t;
    }
    else
    {
        if (l2_key_use160)
        {
            *p_mem_id = DsAclQosMacKey160_t;
        }
        else
        {
            *p_mem_id = DsAclQosL3Key320_t;
        }
    }

    return CLI_SUCCESS;
}

STATIC int32
_ctc_goldengate_dkits_tcam_capture_parser_flow_tcam_key(dkits_tcam_flow_interface_t interface_type, uint32 memid, uint8 lkp_idx, uint32* p_tbl_id)
{
    if (DKITS_TCAM_FLOW_INTERFACE_USERID == interface_type)
    {
        if (0 == lkp_idx)
        {
            switch (memid)
            {
                case DsAclQosL3Key160_t:
                    *p_tbl_id = DsUserId0L3Key160_t;
                    break;
                case DsAclQosIpv6Key640_t:
                    *p_tbl_id = DsUserId0Ipv6Key640_t;
                    break;
                case DsAclQosL3Key320_t:
                    *p_tbl_id = DsUserId0L3Key320_t;
                    break;
                case DsAclQosMacKey160_t:
                    *p_tbl_id = DsUserId0MacKey160_t;
                    break;
                default:
                    *p_tbl_id = DsUserId0Ipv6Key640_t;
            }
        }
        else
        {
            switch (memid)
            {
                case DsAclQosL3Key160_t:
                    *p_tbl_id = DsUserId1L3Key160_t;
                    break;
                case DsAclQosIpv6Key640_t:
                    *p_tbl_id = DsUserId1Ipv6Key640_t;
                    break;
                case DsAclQosL3Key320_t:
                    *p_tbl_id = DsUserId1L3Key320_t;
                    break;
                case DsAclQosMacKey160_t:
                    *p_tbl_id = DsUserId1MacKey160_t;
                    break;
                default:
                    *p_tbl_id = DsUserId1Ipv6Key640_t;
            }
        }
    }
    else if (DKITS_TCAM_FLOW_INTERFACE_INGACL == interface_type)
    {
        if (0 == lkp_idx)
        {
            switch (memid)
            {
                case DsAclQosL3Key160_t:
                    *p_tbl_id = DsAclQosL3Key160Ingr0_t;
                    break;
                case DsAclQosIpv6Key640_t:
                    *p_tbl_id = DsAclQosIpv6Key640Ingr0_t;
                    break;
                case DsAclQosL3Key320_t:
                    *p_tbl_id = DsAclQosL3Key320Ingr0_t;
                    break;
                case DsAclQosMacKey160_t:
                    *p_tbl_id = DsAclQosMacKey160Ingr0_t;
                    break;
                default:
                    *p_tbl_id = DsAclQosIpv6Key640Ingr0_t;
            }
        }
        else if (1 == lkp_idx)
        {
            switch (memid)
            {
                case DsAclQosL3Key160_t:
                    *p_tbl_id = DsAclQosL3Key160Ingr1_t;
                    break;
                case DsAclQosIpv6Key640_t:
                    *p_tbl_id = DsAclQosIpv6Key640Ingr1_t;
                    break;
                case DsAclQosL3Key320_t:
                    *p_tbl_id = DsAclQosL3Key320Ingr1_t;
                    break;
                case DsAclQosMacKey160_t:
                    *p_tbl_id = DsAclQosMacKey160Ingr1_t;
                    break;
                default:
                    *p_tbl_id = DsAclQosIpv6Key640Ingr1_t;
            }
        }
        else if (2 == lkp_idx)
        {
            switch (memid)
            {
                case DsAclQosL3Key160_t:
                    *p_tbl_id = DsAclQosL3Key160Ingr2_t;
                    break;
                case DsAclQosIpv6Key640_t:
                    *p_tbl_id = DsAclQosIpv6Key640Ingr2_t;
                    break;
                case DsAclQosL3Key320_t:
                    *p_tbl_id = DsAclQosL3Key320Ingr2_t;
                    break;
                case DsAclQosMacKey160_t:
                    *p_tbl_id = DsAclQosMacKey160Ingr2_t;
                    break;
                default:
                    *p_tbl_id = DsAclQosIpv6Key640Ingr2_t;
            }
        }
        else
        {
            switch (memid)
            {
                case DsAclQosL3Key160_t:
                    *p_tbl_id = DsAclQosL3Key160Ingr3_t;
                    break;
                case DsAclQosIpv6Key640_t:
                    *p_tbl_id = DsAclQosIpv6Key640Ingr3_t;
                    break;
                case DsAclQosL3Key320_t:
                    *p_tbl_id = DsAclQosL3Key320Ingr3_t;
                    break;
                case DsAclQosMacKey160_t:
                    *p_tbl_id = DsAclQosMacKey160Ingr3_t;
                    break;
                default:
                    *p_tbl_id = DsAclQosIpv6Key640Ingr3_t;
            }
        }
    }
    else
    {
        if (0 == lkp_idx)
        {
            switch (memid)
            {
                case DsAclQosL3Key160_t:
                    *p_tbl_id = DsAclQosL3Key160Egr0_t;
                    break;
                case DsAclQosIpv6Key640_t:
                    *p_tbl_id = DsAclQosIpv6Key640Egr0_t;
                    break;
                case DsAclQosL3Key320_t:
                    *p_tbl_id = DsAclQosL3Key320Egr0_t;
                    break;
                case DsAclQosMacKey160_t:
                    *p_tbl_id = DsAclQosMacKey160Egr0_t;
                    break;
                default:
                    *p_tbl_id = DsAclQosIpv6Key640Egr0_t;
            }
        }
        else
        {
            switch (memid)
            {
                case DsAclQosL3Key160_t:
                    *p_tbl_id = DsAclQosL3Key160Egr1_t;
                    break;
                case DsAclQosIpv6Key640_t:
                    *p_tbl_id = DsAclQosIpv6Key640Egr1_t;
                    break;
                case DsAclQosL3Key320_t:
                    *p_tbl_id = DsAclQosL3Key320Egr1_t;
                    break;
                case DsAclQosMacKey160_t:
                    *p_tbl_id = DsAclQosMacKey160Egr1_t;
                    break;
                default:
                    *p_tbl_id = DsAclQosIpv6Key640Egr1_t;
            }
        }
    }

    return CLI_SUCCESS;
}

STATIC int32
_ctc_goldengate_dkits_tcam_capture_parser_lpm_ip_tcam_key(LpmTcamIpBistReqMem_m* p_lpm_tcam_ip_bist_req_mem, uint32* p_tblid)
{
    uint32 key_size = 0;
    uint32 key_type = 0;

    DsLpmTcamIpv440Key_m   ds_lpm_tcam_ipv440_key;
    DsLpmTcamIpv6160Key0_m ds_lpm_tcam_ipv6160_key0;

    GetLpmTcamIpBistReqMem(A, keySize_f, p_lpm_tcam_ip_bist_req_mem, &key_size);

    if (DKITS_LPM_TCAM_KEY_SIZE_TYPE_HALF == key_size)
    {
        sal_memset(&ds_lpm_tcam_ipv440_key, 0, sizeof(DsLpmTcamIpv440Key_m));
        sal_memcpy((uint8*)&ds_lpm_tcam_ipv440_key, (uint8*)p_lpm_tcam_ip_bist_req_mem, sizeof(DsLpmTcamIpv440Key_m));
        GetDsLpmTcamIpv440Key(A, lpmTcamKeyType_f, &ds_lpm_tcam_ipv440_key, &key_type);

        if (DKITS_LPM_IP_KEY_TYPE_IPV4 == key_type)
        {
            *p_tblid = DsLpmTcamIpv440Key_t;
        }
        else
        {
            *p_tblid = MaxTblId_t;
            return CLI_ERROR;
        }
    }
    else if (DKITS_LPM_TCAM_KEY_SIZE_TYPE_DOUBLE == key_size)
    {
        sal_memset(&ds_lpm_tcam_ipv6160_key0, 0, sizeof(DsLpmTcamIpv6160Key0_m));
        sal_memcpy((uint8*)&ds_lpm_tcam_ipv6160_key0, (uint8*)p_lpm_tcam_ip_bist_req_mem, sizeof(DsLpmTcamIpv6160Key0_m));
        GetDsLpmTcamIpv6160Key0(A, lpmTcamKeyType0_f, &ds_lpm_tcam_ipv440_key, &key_type);

        if (DKITS_LPM_IP_KEY_TYPE_IPV6 == key_type)
        {
            *p_tblid = DsLpmTcamIpv6160Key0_t;
        }
        else
        {
            *p_tblid = MaxTblId_t;
            return CLI_ERROR;
        }
    }
    else
    {
        *p_tblid = MaxTblId_t;
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

STATIC int32
_ctc_goldengate_dkits_tcam_capture_parser_lpm_nat_tcam_key(LpmTcamNatBistReqMem0_m* p_lpm_tcam_nat_bist_req_mem, uint32* p_tblid)
{
    uint32 key_size = 0;
    uint32 key_type = 0;
    DsLpmTcamIpv4160Key_m ds_lpm_tcam_ipv4160_key;
    DsLpmTcamIpv6320Key_m ds_lpm_tcam_ipv6320_key;

    if (DKITS_LPM_TCAM_KEY_SIZE_TYPE_SINGLE == key_size)
    {
        sal_memset(&ds_lpm_tcam_ipv4160_key, 0, sizeof(DsLpmTcamIpv4160Key_m));
        sal_memcpy((uint8*)&ds_lpm_tcam_ipv4160_key, (uint8*)p_lpm_tcam_nat_bist_req_mem, sizeof(DsLpmTcamIpv4160Key_m));
        GetDsLpmTcamIpv4160Key(A, lpmTcamKeyType_f, &ds_lpm_tcam_ipv4160_key, &key_type);

        if (DKITS_LPM_NAT_KEY_TYPE_IPV4PBR == key_type)
        {
            *p_tblid = DsLpmTcamIpv4Pbr160Key_t;
        }
        else if (DKITS_LPM_NAT_KEY_TYPE_IPV4NAT == key_type)
        {
            *p_tblid = DsLpmTcamIpv4NAT160Key_t;
        }
        else if (DKITS_LPM_NAT_KEY_TYPE_IPV6NAT == key_type)
        {
            *p_tblid = DsLpmTcamIpv6160Key1_t;
        }
        else
        {
            *p_tblid = MaxTblId_t;
            return CLI_ERROR;
        }
    }
    else if (DKITS_LPM_TCAM_KEY_SIZE_TYPE_DOUBLE == key_size)
    {
        sal_memset(&ds_lpm_tcam_ipv6320_key, 0, sizeof(DsLpmTcamIpv6320Key_m));
        sal_memcpy((uint8*)&ds_lpm_tcam_ipv6320_key, (uint8*)p_lpm_tcam_nat_bist_req_mem, sizeof(DsLpmTcamIpv6320Key_m));
        GetDsLpmTcamIpv6320Key(A, lpmTcamKeyType0_f, &ds_lpm_tcam_ipv6320_key, &key_type);

        if (DKITS_LPM_NAT_KEY_TYPE_IPV6PBR == key_type)
        {
            *p_tblid = DsLpmTcamIpv6320Key_t;
        }
        else
        {
            *p_tblid = MaxTblId_t;
            return CLI_ERROR;
        }
    }
    else
    {
        *p_tblid = MaxTblId_t;
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

STATIC int32
_ctc_goldengate_dkits_tcam_capture_flow_tcam_lookup_info(FlowTcamBistReqMem_m* p_flow_tcam_bist_req_mem, dkits_tcam_lookup_info_t* p_dkits_tcam_lookup_info, dkits_tcam_capture_pr_t* p_capture_pr, dkits_tcam_flow_interface_t interface_type, dkits_tcam_tbl_info_t* p_tbl_info)
{
    uint32 i = 0;
    uint32 field = 0;
    uint32 info1 = 0, info2 = 0, info3 = 0;
    uint32 key_info0 = 0, key_info1 = 0, key_info2 = 0, key_info3 = 0;

    /* 24 bit */
    info3 = GetFlowTcamBistReqMem(V, info3_f, p_flow_tcam_bist_req_mem);
    /* 32 bit */
    info2 = GetFlowTcamBistReqMem(V, info2_f, p_flow_tcam_bist_req_mem);
    /* 32 bit */
    info1 = GetFlowTcamBistReqMem(V, info1_f, p_flow_tcam_bist_req_mem);

    key_info0 = info3 >> 6;
    key_info1 = ((info3 & 0x3F) << 12) + ((info2 >> 20) & 0xFFF);
    key_info2 = (info2 & 0xFFFFF) >> 2;
    key_info3 = ((info2 & 0x3) << 16) + (info1 >> 16);

    sal_memcpy((uint8*)&p_dkits_tcam_lookup_info[0], (uint8*)&key_info0, sizeof(dkits_tcam_lookup_info_t));
    sal_memcpy((uint8*)&p_dkits_tcam_lookup_info[1], (uint8*)&key_info1, sizeof(dkits_tcam_lookup_info_t));
    sal_memcpy((uint8*)&p_dkits_tcam_lookup_info[2], (uint8*)&key_info2, sizeof(dkits_tcam_lookup_info_t));
    sal_memcpy((uint8*)&p_dkits_tcam_lookup_info[3], (uint8*)&key_info3, sizeof(dkits_tcam_lookup_info_t));

    for (i = 0; i < 4; i++)
    {
        if (DKITS_TCAM_FLOW_INTERFACE_USERID == interface_type)
        {
            if (p_dkits_tcam_lookup_info[i].from_userid)
            {
                p_tbl_info[i].lookup_valid = 1;
            }
        }
        else if ((DKITS_TCAM_FLOW_INTERFACE_INGACL == interface_type)
                || (DKITS_TCAM_FLOW_INTERFACE_EGRACL == interface_type))
        {
            if (p_dkits_tcam_lookup_info[i].acl_en)
            {
                p_tbl_info[i].lookup_valid = 1;
            }
        }
    }

    for (i = 0; i < 22; i++)
    {
        field = 0;
        DRV_GET_FIELD(FlowTcamBistReqMem_t, FlowTcamBistReqMem_key0_f + i, p_flow_tcam_bist_req_mem, &field);
        ((uint32*)p_capture_pr)[i] = field;
    }

    return CLI_SUCCESS;
}

STATIC int32
_ctc_goldengate_dkits_tcam_capture_decap_request(uint8 lchip, dkits_tcam_module_type_t module, dkits_tcam_flow_interface_t interface_type, dkits_tcam_capture_info_t* p_capture_info, uint32* p_captured_num)
{
    uint8     i = 0, j = 0;
    uint32    cmd = 0;
    ds_t      ds;
    tbls_id_t mem_id;
    FlowTcamBistReqMem_m    flow_tcam_bist_req_mem;
    LpmTcamIpBistReqMem_m  lpm_tcam_ip_bist_req_mem;
    LpmTcamNatBistReqMem0_m lpm_tcam_nat_bist_req_mem;

    dkits_tcam_capture_pr_t capture_pr;
    dkits_tcam_lookup_info_t tcam_lookup_info[4];

    sal_memset(&ds, 0, sizeof(ds_t));

    if (DKITS_TCAM_MODULE_TYPE_FLOW == module)
    {
        for (i = 0; i < 32; i++)
        {
            sal_memset(&flow_tcam_bist_req_mem, 0, sizeof(FlowTcamBistReqMem_m));
            cmd = DRV_IOR(FlowTcamBistReqMem_t, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, i, cmd, &flow_tcam_bist_req_mem);

            if (0 == sal_memcmp(&flow_tcam_bist_req_mem, &ds, sizeof(FlowTcamBistReqMem_m)))
            {
                break;
            }
            (*p_captured_num)++;

            _ctc_goldengate_dkits_tcam_capture_flow_tcam_lookup_info(&flow_tcam_bist_req_mem, tcam_lookup_info, &capture_pr, interface_type, p_capture_info->tbl[i]);
            _ctc_goldengate_dkits_tcam_capture_parser_flow_tcam_mem(&capture_pr, tcam_lookup_info, &mem_id);

            for (j = 0; j < 4; j++)
            {
                if (1 == p_capture_info->tbl[i][j].lookup_valid)
                {
                    _ctc_goldengate_dkits_tcam_capture_parser_flow_tcam_key(interface_type, mem_id, j, &p_capture_info->tbl[i][j].id);
                }
                else
                {
                    ;
                }
            }
        }
    }
    else if (DKITS_TCAM_MODULE_TYPE_IPPREFIX == module)
    {
        for (i = 0; i < 32; i++)
        {
            sal_memset(&lpm_tcam_ip_bist_req_mem, 0, sizeof(LpmTcamIpBistReqMem_m));

            cmd = DRV_IOR(LpmTcamIpBistReqMem_t, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, i, cmd, &lpm_tcam_ip_bist_req_mem);

            if (0 == sal_memcmp(&lpm_tcam_ip_bist_req_mem, &ds, sizeof(LpmTcamIpBistReqMem_m)))
            {
                break;
            }
            (*p_captured_num)++;
            _ctc_goldengate_dkits_tcam_capture_parser_lpm_ip_tcam_key(&lpm_tcam_ip_bist_req_mem, &p_capture_info->tbl[i][0].id);
        }
    }
    else if (DKITS_TCAM_MODULE_TYPE_IPNAT == module)
    {
        for (i = 0; i < 32; i++)
        {
            sal_memset(&lpm_tcam_nat_bist_req_mem, 0, sizeof(LpmTcamNatBistReqMem0_m));

            cmd = DRV_IOR(LpmTcamNatBistReqMem2_t, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &lpm_tcam_nat_bist_req_mem);

            if (0 == sal_memcmp(&lpm_tcam_nat_bist_req_mem, &ds, sizeof(LpmTcamNatBistReqMem0_m)))
            {
                break;
            }
            (*p_captured_num)++;
            _ctc_goldengate_dkits_tcam_capture_parser_lpm_nat_tcam_key(&lpm_tcam_nat_bist_req_mem, &p_capture_info->tbl[i][0].id);
        }
    }
    else
    {
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

STATIC int32
_ctc_goldengate_dkits_tcam_capture_deliver_result(uint8 lchip, dkits_tcam_module_type_t module, dkits_tcam_capture_info_t* p_capture_info, uint32 capture_num)
{
    uint32 i = 0, j = 0, k = 0;
    uint32 cmd = 0;
    uint32 block_vec = 0;
    uint32 ftm_blk_vec = 0;
    uint32 entry_offset[4] = {0};

    FlowTcamBistResultMem_m flow_tcam_bist_result_mem;
    LpmTcamIpBistResultMem_m lpm_tcam_ip_bist_result_mem;
    LpmTcamNatBistResultMem0_m lpm_tcam_nat_bist_result_mem;

    if (DKITS_TCAM_MODULE_TYPE_FLOW == module)
    {
        for (i = 0; i < capture_num; i++)
        {
            sal_memset(&flow_tcam_bist_result_mem, 0, sizeof(FlowTcamBistResultMem_m));
            cmd = DRV_IOR(FlowTcamBistResultMem_t, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, i, cmd, &flow_tcam_bist_result_mem);

            entry_offset[0] = GetFlowTcamBistResultMem(V, index0_f, &flow_tcam_bist_result_mem);
            entry_offset[1] = GetFlowTcamBistResultMem(V, index1_f, &flow_tcam_bist_result_mem);
            entry_offset[2] = GetFlowTcamBistResultMem(V, index2_f, &flow_tcam_bist_result_mem);
            entry_offset[3] = GetFlowTcamBistResultMem(V, index3_f, &flow_tcam_bist_result_mem);

            block_vec = GetFlowTcamBistResultMem(V, indexValidVec_f, &flow_tcam_bist_result_mem);

            /* max 4 times lookup */
            for (j = 0; j < DKITS_TCAM_PER_TYPE_LKP_NUM; j++)
            {
                if (!p_capture_info->tbl[i][j].lookup_valid)
                {
                    continue;
                }

                ftm_blk_vec = (TABLE_INFO_PTR(p_capture_info->tbl[i][j].id)
                              &&  TABLE_EXT_INFO_PTR(p_capture_info->tbl[i][j].id)
                              &&  TCAM_EXT_INFO_PTR(p_capture_info->tbl[i][j].id))
                              ? TCAM_BITMAP(p_capture_info->tbl[i][j].id) : 0;
                block_vec = block_vec & ftm_blk_vec;

                 /*CTC_DKIT_PRINT("%s %d, ftm_blk_vec:0x%04X, block_vec:0x%04X.\n", __FUNCTION__, __LINE__, ftm_blk_vec, block_vec);*/

                for (k = 0; k < CTC_DKITS_FLOW_TCAM_BLK_NUM; k++)
                {
                    if (DKITS_IS_BIT_SET(block_vec, k))
                    {
                        _ctc_goldengate_dkits_tcam_tbl_index(p_capture_info->tbl[i][j].id, k, entry_offset[j], &p_capture_info->tbl[i][j].idx);
                         /*CTC_DKIT_PRINT("%s %d, entry_offset[%u] = 0x%04X, index = 0x%04X.\n", __FUNCTION__, __LINE__, entry_offset[j], p_capture_info->tbl[i][j].idx);*/

                        if (CTC_DKITS_INVALID_INDEX != p_capture_info->tbl[i][j].idx)
                        {
                            p_capture_info->tbl[i][j].result_valid = 1;
                            break;
                        }
                    }
                }
            }
        }
    }
    else if (DKITS_TCAM_MODULE_TYPE_IPPREFIX == module)
    {
        for (i = 0; i < capture_num; i++)
        {
            sal_memset(&lpm_tcam_ip_bist_result_mem, 0, sizeof(LpmTcamIpBistResultMem_m));
            cmd = DRV_IOR(LpmTcamIpBistResultMem_t, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &lpm_tcam_ip_bist_result_mem);

            GetLpmTcamIpBistResultMem(A, indexValid_f, &lpm_tcam_ip_bist_result_mem, &p_capture_info->tbl[i][0].result_valid);
            GetLpmTcamIpBistResultMem(A, index_f, &lpm_tcam_ip_bist_result_mem, &entry_offset[0]);
            _ctc_goldengate_dkits_tcam_tbl_index(p_capture_info->tbl[i][0].id, 0, entry_offset[0], &p_capture_info->tbl[i][0].idx);
        }
    }
    else if (DKITS_TCAM_MODULE_TYPE_IPNAT == module)
    {
        for (i = 0; i < capture_num; i++)
        {
            sal_memset(&lpm_tcam_nat_bist_result_mem, 0, sizeof(LpmTcamNatBistResultMem0_m));
            cmd = DRV_IOR(LpmTcamNatBistResultMem2_t, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &lpm_tcam_nat_bist_result_mem);

            GetLpmTcamNatBistResultMem2(A, indexValid_f, &lpm_tcam_nat_bist_result_mem, &p_capture_info->tbl[i][0].result_valid);
            GetLpmTcamNatBistResultMem2(A, index_f, &lpm_tcam_nat_bist_result_mem, &entry_offset[0]);
            _ctc_goldengate_dkits_tcam_tbl_index(p_capture_info->tbl[i][0].id, 0, entry_offset[0], &p_capture_info->tbl[i][0].idx);
        }
    }

    return CLI_SUCCESS;
}

STATIC int32
_ctc_goldengate_dkits_tcam_capture_timestamp(dkits_tcam_module_type_t module, dkits_tcam_capture_tick_t tick, sal_time_t* p_stamp)
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
_ctc_goldengate_dkits_tcam_capture_explore_result(uint8 lchip, dkits_tcam_module_type_t tcam_module, uint32 idx)
{
    uint32 i = 0;
    uint32 cfg_capture_vec = 0;
    uint32 captured_num = 0;
    uint32 no = 0;
    dkits_tcam_capture_info_t* capture_info = NULL;

    capture_info = (dkits_tcam_capture_info_t*)sal_malloc(sizeof(dkits_tcam_capture_info_t));
    if(NULL == capture_info)
    {
        return CLI_ERROR;
    }
    sal_memset(capture_info, 0, sizeof(dkits_tcam_capture_info_t));

    CTC_DKIT_PRINT("%-6s%-27s%-28s%-15s%s\n", "No.", "Tcam module", "Tcam table", "Result valid", "Key index");
    CTC_DKIT_PRINT("-----------------------------------------------------------------------------------------------------\n");

    if (DKITS_IS_BIT_SET(tcam_module, DKITS_TCAM_MODULE_TYPE_FLOW))
    {
        _ctc_goldengate_dkits_tcam_capture_get_tbl_property(lchip, DKITS_TCAM_MODULE_TYPE_FLOW, DKITS_TCAM_CAPTURE_TBL_CAPTURE_VEC, &cfg_capture_vec);

        if (0 == cfg_capture_vec)
        {
            CTC_DKIT_PRINT("Warning, Flow tcam no capture vector enable!");
            if(NULL != capture_info)
            {
                sal_free(capture_info);
            }
            return CLI_SUCCESS;
        }

        /* only one interface can be enabled for once capture */
        for (i = 0; i < DKITS_TCAM_FLOW_INTERFACE_NUM; i++)
        {
            if (DKITS_IS_BIT_SET(cfg_capture_vec, i))
            {
                sal_memset(capture_info, 0, sizeof(dkits_tcam_capture_info_t));
                _ctc_goldengate_dkits_tcam_capture_decap_request(lchip, DKITS_TCAM_MODULE_TYPE_FLOW, i, capture_info, &captured_num);
                _ctc_goldengate_dkits_tcam_capture_deliver_result(lchip, DKITS_TCAM_MODULE_TYPE_FLOW, capture_info, captured_num);
                _ctc_goldengate_dkits_tcam_capture_present_result(capture_info, &no, captured_num, idx);
                break;
            }
        }
    }

    if (DKITS_IS_BIT_SET(tcam_module, DKITS_TCAM_MODULE_TYPE_IPPREFIX))
    {
        sal_memset(capture_info, 0, sizeof(dkits_tcam_capture_info_t));
        _ctc_goldengate_dkits_tcam_capture_decap_request(lchip, DKITS_TCAM_MODULE_TYPE_IPPREFIX, 0, capture_info, &captured_num);
        _ctc_goldengate_dkits_tcam_capture_deliver_result(lchip, DKITS_TCAM_MODULE_TYPE_IPPREFIX, capture_info, captured_num);
        _ctc_goldengate_dkits_tcam_capture_present_result(capture_info, &no, captured_num, idx);
    }

    if (DKITS_IS_BIT_SET(tcam_module, DKITS_TCAM_MODULE_TYPE_IPNAT))
    {
        sal_memset(capture_info, 0, sizeof(dkits_tcam_capture_info_t));
        _ctc_goldengate_dkits_tcam_capture_decap_request(lchip, DKITS_TCAM_MODULE_TYPE_IPNAT, 0, capture_info, &captured_num);
        _ctc_goldengate_dkits_tcam_capture_deliver_result(lchip, DKITS_TCAM_MODULE_TYPE_IPNAT, capture_info, captured_num);
        _ctc_goldengate_dkits_tcam_capture_present_result(capture_info, &no, captured_num, idx);
    }

    if(NULL != capture_info)
    {
        sal_free(capture_info);
    }

    return CLI_SUCCESS;
}

STATIC int32
_ctc_goldengate_dkits_tcam_capture_flow_tcam_interface(ctc_dkits_tcam_info_t* p_tcam_info, uint32* p_interface_type)
{
    uint32 i = 0;
    uint32 exclude = 0;

    for (i = 0; i < DKITS_TCAM_FLOW_INTERFACE_NUM; i++)
    {
        if(DKITS_IS_BIT_SET(p_tcam_info->tcam_type, i))
        {
            if (0 == exclude)
            {
                *p_interface_type = i;
                exclude = 1;
            }
            else
            {
                CTC_DKIT_PRINT("igs-scl, igs-acl and egs-acl tcam capture is exclusive with each other.\n");
                return CLI_ERROR;
            }
        }
    }

    return CLI_SUCCESS;
}

#define ________DKITS_TCAM_EXTERNAL_FUNCTION__________
int32
ctc_goldengate_dkits_tcam_scan(void* p_info)
{
    uint32 i = 0, j = 0, k = 0;
    uint32 exclude = 0;
    tbls_id_t tblid = MaxTblId_t;
    tbls_id_t tblid_ = MaxTblId_t - 1;
    char desc[50] = {0};
    ctc_dkits_tcam_info_t* p_tcam_info = NULL;

    uint8 tcam_type2tcam_block[CTC_DKITS_TCAM_TYPE_FLAG_NUM][4] =
    {
        {DKITS_TCAM_BLOCK_TYPE_IGS_USERID0, DKITS_TCAM_BLOCK_TYPE_IGS_USERID1, DKITS_TCAM_BLOCK_TYPE_NUM},
        {DKITS_TCAM_BLOCK_TYPE_IGS_ACL0, DKITS_TCAM_BLOCK_TYPE_IGS_ACL1, DKITS_TCAM_BLOCK_TYPE_IGS_ACL2, DKITS_TCAM_BLOCK_TYPE_IGS_ACL3},
        {DKITS_TCAM_BLOCK_TYPE_EGS_ACL0, DKITS_TCAM_BLOCK_TYPE_NUM},
        {DKITS_TCAM_BLOCK_TYPE_IGS_LPM0, DKITS_TCAM_BLOCK_TYPE_NUM},
        {DKITS_TCAM_BLOCK_TYPE_IGS_LPM1, DKITS_TCAM_BLOCK_TYPE_NUM}
    };

    DKITS_PTR_VALID_CHECK(p_info);
    p_tcam_info = (ctc_dkits_tcam_info_t*)p_info;
    CTC_DKIT_LCHIP_CHECK(p_tcam_info->lchip);

    if (0xFFFFFFFF != p_tcam_info->priority)
    {
        for (i = 0; i < CTC_DKITS_TCAM_TYPE_FLAG_NUM; i++)
        {
            if (DKITS_IS_BIT_SET(p_tcam_info->tcam_type, i))
            {
                if (0 == exclude)
                {
                    j = i;
                    exclude = 1;
                }
                else
                {
                    CTC_DKIT_PRINT("When tcam capture specify priority, tcam-type is exclusive with each other in one scan!\n");
                    return CLI_ERROR;
                }
            }
        }

        if (0xFFFFFFFF != p_tcam_info->key_type)
        {
            _ctc_goldengate_dkits_tcam_key2tbl(p_tcam_info, &tblid);

            if (MaxTblId_t == tblid)
            {
                CTC_DKIT_PRINT("Key type value: %u: is invalid\n", p_tcam_info->key_type);
                return CLI_ERROR;
            }
        }
        else
        {
            if (DKITS_TCAM_BLOCK_TYPE_NUM == tcam_type2tcam_block[j][p_tcam_info->priority])
            {
                CTC_DKIT_PRINT("Priority value %u: is invalid\n", p_tcam_info->priority);
                return CLI_ERROR;
            }
        }
        k = 0xFFFFFFFF;

        CTC_DKIT_PRINT("\n");
        CTC_DKIT_PRINT(" Tcam scan result:\n");

        if (MaxTblId_t == tblid)
        {
            _ctc_goldengate_dkits_tcam_bist_tbl_brief_status(&tblid_, 0);
            _ctc_goldengate_dkits_tcam_type2desc((1 << j), p_tcam_info->priority, desc, 50);
            CTC_DKIT_PRINT(" ----------------------------------------------------\n");
            CTC_DKIT_PRINT(" Scan %s\n", desc);
        }
        _ctc_goldengate_dkits_tcam_bist_scan_tcam_type(p_tcam_info->lchip, k, tcam_type2tcam_block[j][p_tcam_info->priority], tblid, p_tcam_info->index);
        if (MaxTblId_t == tblid)
        {
            _ctc_goldengate_dkits_tcam_bist_tbl_brief_status(&tblid_, 1);
            if (MaxTblId_t == tblid_)
            {
                CTC_DKIT_PRINT("    Result fail!\n");
                tblid_ = MaxTblId_t - 1;
                _ctc_goldengate_dkits_tcam_bist_tbl_brief_status(&tblid_, 0);
            }
            else
            {
                CTC_DKIT_PRINT("    Result pass!\n");
            }
            CTC_DKIT_PRINT("\n");
        }
    }
    else
    {
        k = 0;

        CTC_DKIT_PRINT("\n");
        CTC_DKIT_PRINT(" Tcam scan result:\n");
        for (i = 0; i < CTC_DKITS_TCAM_TYPE_FLAG_NUM; i++)
        {
            if (0 == DKITS_IS_BIT_SET(p_tcam_info->tcam_type, i))
            {
                continue;
            }
            for (j = 0; (j < DKITS_TCAM_PER_TYPE_LKP_NUM) && (DKITS_TCAM_BLOCK_TYPE_NUM != tcam_type2tcam_block[i][j]); j++)
            {
                _ctc_goldengate_dkits_tcam_bist_tbl_brief_status(&tblid_, 0);
                _ctc_goldengate_dkits_tcam_type2desc((1 << i), j, desc, 50);
                CTC_DKIT_PRINT(" ----------------------------------------------------\n");
                k++;
                CTC_DKIT_PRINT(" %u. Scan %s\n", k, desc);
                _ctc_goldengate_dkits_tcam_bist_scan_tcam_type(p_tcam_info->lchip, k, tcam_type2tcam_block[i][j], tblid, p_tcam_info->index);
                _ctc_goldengate_dkits_tcam_bist_tbl_brief_status(&tblid_, 1);
                if (MaxTblId_t == tblid_)
                {
                    CTC_DKIT_PRINT("    Result fail!\n");
                    tblid_ = MaxTblId_t - 1;
                    _ctc_goldengate_dkits_tcam_bist_tbl_brief_status(&tblid_, 0);
                }
                else
                {
                    CTC_DKIT_PRINT("    Result pass!\n");
                }
            }
        }
        CTC_DKIT_PRINT("\n");
    }

    return CLI_SUCCESS;
}

int32
ctc_goldengate_dkits_tcam_capture_start(void* p_info)
{
    uint32 i = 0;
    ctc_dkits_tcam_info_t* p_tcam_info = NULL;
    uint32 interface_type = 0;

    DKITS_PTR_VALID_CHECK(p_info);
    p_tcam_info = (ctc_dkits_tcam_info_t*)p_info;
    CTC_DKIT_LCHIP_CHECK(p_tcam_info->lchip);

    _ctc_goldengate_dkits_tcam_capture_flow_tcam_interface(p_tcam_info, &interface_type);

    for (i = 0; i < DKITS_TCAM_MODULE_TYPE_NUM; i++)
    {
        _ctc_goldengate_dkits_tcam_reset_detect(p_tcam_info->lchip, i);
    }

    if ((DKITS_FLAG_ISSET(p_tcam_info->tcam_type, CTC_DKITS_TCAM_TYPE_FLAG_IGS_SCL))
       || (DKITS_FLAG_ISSET(p_tcam_info->tcam_type, CTC_DKITS_TCAM_TYPE_FLAG_IGS_ACL))
       || (DKITS_FLAG_ISSET(p_tcam_info->tcam_type, CTC_DKITS_TCAM_TYPE_FLAG_EGS_ACL)))
    {
        _ctc_goldengate_dkits_tcam_capture_set_tbl_property(p_tcam_info->lchip, DKITS_TCAM_MODULE_TYPE_FLOW, DKITS_TCAM_CAPTURE_TBL_CAPTURE_EN, 1);
        _ctc_goldengate_dkits_tcam_capture_set_tbl_property(p_tcam_info->lchip, DKITS_TCAM_MODULE_TYPE_FLOW, DKITS_TCAM_CAPTURE_TBL_CAPTURE_ONCE, 1);
        _ctc_goldengate_dkits_tcam_capture_set_tbl_property(p_tcam_info->lchip, DKITS_TCAM_MODULE_TYPE_FLOW, DKITS_TCAM_CAPTURE_TBL_CAPTURE_VEC, (1 << interface_type));
        _ctc_goldengate_dkits_tcam_capture_set_tbl_property(p_tcam_info->lchip, DKITS_TCAM_MODULE_TYPE_FLOW, DKITS_TCAM_CAPTURE_TBL_STOP_ON_ERROR, 0);
        _ctc_goldengate_dkits_tcam_capture_timestamp(DKITS_TCAM_MODULE_TYPE_FLOW, DKITS_TCAM_CAPTURE_TICK_BEGINE, NULL);
    }

    if (DKITS_FLAG_ISSET(p_tcam_info->tcam_type, CTC_DKITS_TCAM_TYPE_FLAG_IPPREFIX))
    {
        _ctc_goldengate_dkits_tcam_capture_set_tbl_property(p_tcam_info->lchip, DKITS_TCAM_MODULE_TYPE_IPPREFIX, DKITS_TCAM_CAPTURE_TBL_CAPTURE_EN, 1);
        _ctc_goldengate_dkits_tcam_capture_set_tbl_property(p_tcam_info->lchip, DKITS_TCAM_MODULE_TYPE_IPPREFIX, DKITS_TCAM_CAPTURE_TBL_CAPTURE_ONCE, 1);
        _ctc_goldengate_dkits_tcam_capture_set_tbl_property(p_tcam_info->lchip, DKITS_TCAM_MODULE_TYPE_IPPREFIX, DKITS_TCAM_CAPTURE_TBL_STOP_ON_ERROR, 0);
        _ctc_goldengate_dkits_tcam_capture_timestamp(DKITS_TCAM_MODULE_TYPE_IPPREFIX, DKITS_TCAM_CAPTURE_TICK_BEGINE, NULL);
    }

    if (DKITS_FLAG_ISSET(p_tcam_info->tcam_type, CTC_DKITS_TCAM_TYPE_FLAG_IPNAT))
    {
        _ctc_goldengate_dkits_tcam_capture_set_tbl_property(p_tcam_info->lchip, DKITS_TCAM_MODULE_TYPE_IPNAT, DKITS_TCAM_CAPTURE_TBL_CAPTURE_EN, 1);
        _ctc_goldengate_dkits_tcam_capture_set_tbl_property(p_tcam_info->lchip, DKITS_TCAM_MODULE_TYPE_IPNAT, DKITS_TCAM_CAPTURE_TBL_CAPTURE_ONCE, 1);
        _ctc_goldengate_dkits_tcam_capture_set_tbl_property(p_tcam_info->lchip, DKITS_TCAM_MODULE_TYPE_IPNAT, DKITS_TCAM_CAPTURE_TBL_STOP_ON_ERROR, 0);
        _ctc_goldengate_dkits_tcam_capture_timestamp(DKITS_TCAM_MODULE_TYPE_IPNAT, DKITS_TCAM_CAPTURE_TICK_BEGINE, NULL);
    }

    return CLI_SUCCESS;
}

int32
ctc_goldengate_dkits_tcam_capture_stop(void* p_info)
{
    uint32 tcam_module = 0;
    uint32 capture_en = 0;
    sal_time_t start, now;
    ctc_dkits_tcam_info_t* p_tcam_info = NULL;
    char* p_str1 = NULL;
    char* p_str2 = NULL;
    char* p_tmp = NULL;

    DKITS_PTR_VALID_CHECK(p_info);
    p_tcam_info = (ctc_dkits_tcam_info_t*)p_info;
    CTC_DKIT_LCHIP_CHECK(p_tcam_info->lchip);

    if ((DKITS_FLAG_ISSET(p_tcam_info->tcam_type, CTC_DKITS_TCAM_TYPE_FLAG_IGS_SCL))
       || (DKITS_FLAG_ISSET(p_tcam_info->tcam_type, CTC_DKITS_TCAM_TYPE_FLAG_IGS_ACL))
       || (DKITS_FLAG_ISSET(p_tcam_info->tcam_type, CTC_DKITS_TCAM_TYPE_FLAG_EGS_ACL)))
    {
        _ctc_goldengate_dkits_tcam_capture_get_tbl_property(p_tcam_info->lchip, DKITS_TCAM_MODULE_TYPE_FLOW, DKITS_TCAM_CAPTURE_TBL_CAPTURE_EN, &capture_en);
        if (!capture_en)
        {
            CTC_DKIT_PRINT("Tcam capture toward igs-scl, igs-acl and egs-acl has not been started yet!\n");
        }
        else
        {
            _ctc_goldengate_dkits_tcam_capture_set_tbl_property(p_tcam_info->lchip, DKITS_TCAM_MODULE_TYPE_FLOW, DKITS_TCAM_CAPTURE_TBL_CAPTURE_EN, 0);
            _ctc_goldengate_dkits_tcam_capture_timestamp(DKITS_TCAM_MODULE_TYPE_FLOW, DKITS_TCAM_CAPTURE_TICK_LASTEST, &start);
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
    }

    if (DKITS_FLAG_ISSET(p_tcam_info->tcam_type, CTC_DKITS_TCAM_TYPE_FLAG_IPPREFIX))
    {
        _ctc_goldengate_dkits_tcam_capture_get_tbl_property(p_tcam_info->lchip, DKITS_TCAM_MODULE_TYPE_IPPREFIX, DKITS_TCAM_CAPTURE_TBL_CAPTURE_EN, &capture_en);
        if (!capture_en)
        {
            CTC_DKIT_PRINT("Tcam capture toward lpm-ip has not been started yet!\n");
        }
        else
        {
            _ctc_goldengate_dkits_tcam_capture_set_tbl_property(p_tcam_info->lchip, DKITS_TCAM_MODULE_TYPE_IPPREFIX, DKITS_TCAM_CAPTURE_TBL_CAPTURE_EN, 0);
            _ctc_goldengate_dkits_tcam_capture_timestamp(DKITS_TCAM_MODULE_TYPE_IPPREFIX, DKITS_TCAM_CAPTURE_TICK_LASTEST, &start);
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
    }

    if (DKITS_FLAG_ISSET(p_tcam_info->tcam_type, CTC_DKITS_TCAM_TYPE_FLAG_IPNAT))
    {
        _ctc_goldengate_dkits_tcam_capture_get_tbl_property(p_tcam_info->lchip, DKITS_TCAM_MODULE_TYPE_IPNAT, DKITS_TCAM_CAPTURE_TBL_CAPTURE_EN, &capture_en);
        if (!capture_en)
        {
            CTC_DKIT_PRINT("Tcam capture toward lpm-nat and lpm-pbr has not been started yet!\n");
        }
        else
        {
            _ctc_goldengate_dkits_tcam_capture_set_tbl_property(p_tcam_info->lchip, DKITS_TCAM_MODULE_TYPE_IPNAT, DKITS_TCAM_CAPTURE_TBL_CAPTURE_EN, 0);
            _ctc_goldengate_dkits_tcam_capture_timestamp(DKITS_TCAM_MODULE_TYPE_IPNAT, DKITS_TCAM_CAPTURE_TICK_LASTEST, &start);
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
    }
    CTC_DKIT_PRINT("\n");
    _ctc_goldengate_dkits_tcam_capture_explore_result(p_tcam_info->lchip, tcam_module, 0xFFFFFFFF);

    return CLI_SUCCESS;
}

int32
ctc_goldengate_dkits_tcam_capture_show(void* p_info)
{
    uint32 tcam_module = 0;
    uint32 capture_en = 0;
    uint32 interface_type = 0;
    uint32 capture_vec = 0;
    uint32 expect_vec = 0;
    ctc_dkits_tcam_info_t* p_tcam_info;
    char* flow_tcam_interface[] = {"igs-scl", "igs-acl", "egs-acl"};

    DKITS_PTR_VALID_CHECK(p_info);
    p_tcam_info = (ctc_dkits_tcam_info_t*)p_info;
    CTC_DKIT_LCHIP_CHECK(p_tcam_info->lchip);

    _ctc_goldengate_dkits_tcam_capture_flow_tcam_interface(p_tcam_info, &interface_type);

    if ((DKITS_FLAG_ISSET(p_tcam_info->tcam_type, CTC_DKITS_TCAM_TYPE_FLAG_IGS_SCL))
       || (DKITS_FLAG_ISSET(p_tcam_info->tcam_type, CTC_DKITS_TCAM_TYPE_FLAG_IGS_ACL))
       || (DKITS_FLAG_ISSET(p_tcam_info->tcam_type, CTC_DKITS_TCAM_TYPE_FLAG_EGS_ACL)))
    {
        _ctc_goldengate_dkits_tcam_capture_get_tbl_property(p_tcam_info->lchip, DKITS_TCAM_MODULE_TYPE_FLOW, DKITS_TCAM_CAPTURE_TBL_CAPTURE_EN, &capture_en);
        if (1 == capture_en)
        {
            CTC_DKIT_PRINT("Please stop the capture of igs-scl, igs-acl and egs-acl tcam type before show capture result!\n");
            return CLI_ERROR;
        }

        _ctc_goldengate_dkits_tcam_capture_get_tbl_property(p_tcam_info->lchip, DKITS_TCAM_MODULE_TYPE_FLOW, DKITS_TCAM_CAPTURE_TBL_CAPTURE_VEC, &capture_vec);
        DKITS_BIT_SET(expect_vec , interface_type);

        if (capture_vec != expect_vec)
        {
            CTC_DKIT_PRINT("Tcam capture toward %s tcam type has not been started yet!\n", flow_tcam_interface[interface_type]);
            return CLI_ERROR;
        }

        DKITS_BIT_SET(tcam_module, DKITS_TCAM_MODULE_TYPE_FLOW);
    }

    if (DKITS_FLAG_ISSET(p_tcam_info->tcam_type, CTC_DKITS_TCAM_TYPE_FLAG_IPPREFIX))
    {
        _ctc_goldengate_dkits_tcam_capture_get_tbl_property(p_tcam_info->lchip, DKITS_TCAM_MODULE_TYPE_IPPREFIX, DKITS_TCAM_CAPTURE_TBL_CAPTURE_EN, &capture_en);
        if (1 == capture_en)
        {
            CTC_DKIT_PRINT("Please stop the capture of lpm-ip tcam type before show capture result!\n");
            return CLI_SUCCESS;
        }

        DKITS_BIT_SET(tcam_module, DKITS_TCAM_MODULE_TYPE_IPPREFIX);
    }

    if (DKITS_FLAG_ISSET(p_tcam_info->tcam_type, CTC_DKITS_TCAM_TYPE_FLAG_IPNAT))
    {
        _ctc_goldengate_dkits_tcam_capture_get_tbl_property(p_tcam_info->lchip, DKITS_TCAM_MODULE_TYPE_IPNAT, DKITS_TCAM_CAPTURE_TBL_CAPTURE_EN, &capture_en);
        if (1 == capture_en)
        {
            CTC_DKIT_PRINT("Please stop the capture of lpm-nat and lpm-pbr tcam type before show capture result!\n");
            return CLI_SUCCESS;
        }

        DKITS_BIT_SET(tcam_module, DKITS_TCAM_MODULE_TYPE_IPNAT);
    }

    _ctc_goldengate_dkits_tcam_capture_explore_result(p_tcam_info->lchip, tcam_module, p_tcam_info->index);

    return CLI_SUCCESS;
}

int32
ctc_goldengate_dkits_tcam_show_key_type(uint8 lchip)
{
    uint8 i = 0;
    tbls_id_t tblid = MaxTblId_t;
    ctc_dkits_tcam_info_t tcam_info;
    char desc[50] = {0};

    sal_memset(&tcam_info, 0, sizeof(ctc_dkits_tcam_info_t));

    CTC_DKIT_PRINT("%-18s%-14s%s\n", "TcamType", "KeyType", "TcamTblName");
    CTC_DKIT_PRINT("%s\n", "-------------------------------------------------------");

    tcam_info.tcam_type = 0;
    DKITS_SET_FLAG(tcam_info.tcam_type, CTC_DKITS_TCAM_TYPE_FLAG_IGS_SCL);
    tcam_info.priority = 0;
    for (i = DRV_TCAMKEYTYPE_MACKEY160; i <= DRV_TCAMKEYTYPE_IPV6KEY640; i++)
    {
        tcam_info.key_type = i;
        _ctc_goldengate_dkits_tcam_key2tbl(&tcam_info, &tblid);
        _ctc_goldengate_dkits_tcam_tbl2desc(tblid, desc, sizeof(desc));
        if (tblid != MaxTblId_t)
        {
            CTC_DKIT_PRINT("%-18s%-14u%s\n", desc, i, TABLE_NAME(tblid));
        }
    }

    tcam_info.priority = 1;
    for (i = DRV_TCAMKEYTYPE_MACKEY160; i <= DRV_TCAMKEYTYPE_IPV6KEY640; i++)
    {
        tcam_info.key_type = i;
        _ctc_goldengate_dkits_tcam_key2tbl(&tcam_info, &tblid);
        _ctc_goldengate_dkits_tcam_tbl2desc(tblid, desc, sizeof(desc));
        if (tblid != MaxTblId_t)
        {
            CTC_DKIT_PRINT("%-18s%-14u%s\n", desc, i, TABLE_NAME(tblid));
        }
    }

    tcam_info.tcam_type = 0;
    DKITS_SET_FLAG(tcam_info.tcam_type, CTC_DKITS_TCAM_TYPE_FLAG_IGS_ACL);
    tcam_info.priority = 0;
    for (i = DRV_TCAMKEYTYPE_MACKEY160; i <= DRV_TCAMKEYTYPE_IPV6KEY640; i++)
    {
        tcam_info.key_type = i;
        _ctc_goldengate_dkits_tcam_key2tbl(&tcam_info, &tblid);
        _ctc_goldengate_dkits_tcam_tbl2desc(tblid, desc, sizeof(desc));
        CTC_DKIT_PRINT("%-18s%-14u%s\n", desc, i, TABLE_NAME(tblid));
    }

    tcam_info.priority = 1;
    for (i = DRV_TCAMKEYTYPE_MACKEY160; i <= DRV_TCAMKEYTYPE_IPV6KEY640; i++)
    {
        tcam_info.key_type = i;
        _ctc_goldengate_dkits_tcam_key2tbl(&tcam_info, &tblid);
        _ctc_goldengate_dkits_tcam_tbl2desc(tblid, desc, sizeof(desc));
        CTC_DKIT_PRINT("%-18s%-14u%s\n", desc, i, TABLE_NAME(tblid));
    }

    tcam_info.priority = 2;
    for (i = DRV_TCAMKEYTYPE_MACKEY160; i <= DRV_TCAMKEYTYPE_IPV6KEY640; i++)
    {
        tcam_info.key_type = i;
        _ctc_goldengate_dkits_tcam_key2tbl(&tcam_info, &tblid);
        _ctc_goldengate_dkits_tcam_tbl2desc(tblid, desc, sizeof(desc));
        CTC_DKIT_PRINT("%-18s%-14u%s\n", desc, i, TABLE_NAME(tblid));
    }

    tcam_info.priority = 3;
    for (i = DRV_TCAMKEYTYPE_MACKEY160; i <= DRV_TCAMKEYTYPE_IPV6KEY640; i++)
    {
        tcam_info.key_type = i;
        _ctc_goldengate_dkits_tcam_key2tbl(&tcam_info, &tblid);
        _ctc_goldengate_dkits_tcam_tbl2desc(tblid, desc, sizeof(desc));
        CTC_DKIT_PRINT("%-18s%-14u%s\n", desc, i, TABLE_NAME(tblid));
    }

    tcam_info.tcam_type = 0;
    DKITS_SET_FLAG(tcam_info.tcam_type, CTC_DKITS_TCAM_TYPE_FLAG_EGS_ACL);
    tcam_info.priority = 0;
    for (i = DRV_TCAMKEYTYPE_MACKEY160; i <= DRV_TCAMKEYTYPE_IPV6KEY640; i++)
    {
        tcam_info.key_type = i;
        _ctc_goldengate_dkits_tcam_key2tbl(&tcam_info, &tblid);
        _ctc_goldengate_dkits_tcam_tbl2desc(tblid, desc, sizeof(desc));
        CTC_DKIT_PRINT("%-18s%-14u%s\n", desc, i, TABLE_NAME(tblid));
    }

    tcam_info.tcam_type = 0;
    DKITS_SET_FLAG(tcam_info.tcam_type, CTC_DKITS_TCAM_TYPE_FLAG_IPPREFIX);
    for (i = DRV_FIBLPMTCAMKEYTYPE_IPV4UCAST; i <= DRV_FIBLPMTCAMKEYTYPE_IPV6UCAST; i++)
    {
        tcam_info.key_type = i;
        _ctc_goldengate_dkits_tcam_key2tbl(&tcam_info, &tblid);
        _ctc_goldengate_dkits_tcam_tbl2desc(tblid, desc, sizeof(desc));
        CTC_DKIT_PRINT("%-18s%-14u%s\n", desc, i, TABLE_NAME(tblid));
    }

    tcam_info.tcam_type = 0;
    DKITS_SET_FLAG(tcam_info.tcam_type, CTC_DKITS_TCAM_TYPE_FLAG_IPNAT);
    for (i = DRV_FIBNATPBRTCAMKEYTYPE_IPV4PBR; i <= DRV_FIBNATPBRTCAMKEYTYPE_IPV6NAT; i++)
    {
        tcam_info.key_type = i;
        _ctc_goldengate_dkits_tcam_key2tbl(&tcam_info, &tblid);
        _ctc_goldengate_dkits_tcam_tbl2desc(tblid, desc, sizeof(desc));
        CTC_DKIT_PRINT("%-18s%-14u%s\n", desc, i, TABLE_NAME(tblid));
    }
    CTC_DKIT_PRINT("\n");

    return CLI_SUCCESS;
}


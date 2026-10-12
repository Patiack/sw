/**
 @file drv_chip_io.c

 @date 2011-10-09

 @version v4.28.2

 The file contains all driver I/O interface realization
*/
#include "greatbelt/include/drv_lib.h"
#include "greatbelt/include/drv_chip_ctrl.h"
extern tables_info_t drv_gb_tbls_list[MaxTblId_t];

#define DRV_TIME_OUT  1000    /* Time out setting */

/* Cpu access Tcam control operation type */
enum int_tcam_operation_type_t
{
    CPU_ACCESS_REQ_READ_X = 1,
    CPU_ACCESS_REQ_READ_Y = 2,
    CPU_ACCESS_REQ_WRITE_DATA_MASK = 5,
    CPU_ACCESS_REQ_INVALID_ENTRY = 9
};
typedef enum int_tcam_operation_type_t int_tcam_op_tp_e;

enum fib_tcam_operation_type_t
{
    FIB_CPU_ACCESS_REQ_READ_X = 0,
    FIB_CPU_ACCESS_REQ_READ_Y = 1,
    FIB_CPU_ACCESS_REQ_WRITE_DATA_MASK = 2,
    FIB_CPU_ACCESS_REQ_INVALID_ENTRY = 3
};
typedef enum fib_tcam_operation_type_t fib_tcam_op_tp_e;

/* Tcam memory type */
enum tcam_mem_type
{
    DRV_INT_TCAM_RECORD_DATA = 0,
    DRV_INT_TCAM_RECORD_MASK = 1,
    DRV_INT_TCAM_RECORD_REG = 2,
    DRV_INT_LPM_TCAM_RECORD_DATA = 3,
    DRV_INT_LPM_TCAM_RECORD_MASK = 4,
    DRV_INT_LPM_TCAM_RECORD_REG = 5
};
typedef enum tcam_mem_type tcam_mem_type_e;

/* Tcam/Hash/Tbl/Reg I/O operation mutex definition */
static sal_mutex_t* p_gb_tcam_mutex[MAX_LOCAL_CHIP_NUM];
static sal_mutex_t* p_gb_lpm_tcam_mutex[MAX_LOCAL_CHIP_NUM];

sal_mutex_t* p_gb_entry_mutex[MAX_LOCAL_CHIP_NUM];

sal_mutex_t* p_gb_mep_mutex[MAX_LOCAL_CHIP_NUM];

/* Tcam/Hash/Tbl/Reg I/O operation mutex control */
#define TCAM_LOCK(chip_id_offset)         sal_mutex_lock(p_gb_tcam_mutex[chip_id_offset])
#define TCAM_UNLOCK(chip_id_offset)       sal_mutex_unlock(p_gb_tcam_mutex[chip_id_offset])
#define LPM_TCAM_LOCK(chip_id_offset)     sal_mutex_lock(p_gb_lpm_tcam_mutex[chip_id_offset])
#define LPM_TCAM_UNLOCK(chip_id_offset)   sal_mutex_unlock(p_gb_lpm_tcam_mutex[chip_id_offset])
#define DRV_ENTRY_LOCK(chip_id_offset)         sal_mutex_lock(p_gb_entry_mutex[chip_id_offset])
#define DRV_ENTRY_UNLOCK(chip_id_offset)       sal_mutex_unlock(p_gb_entry_mutex[chip_id_offset])



/* Each hash key and the corresponding action tb's
   position information in hash ram and sram */
struct hash_tblbase_info_s
{
    uint32 key_bucket_base;
    uint32 hash_action_tbl_pos;
    uint32 action_tbl_offset;
};
typedef struct hash_tblbase_info_s hash_tblbase_info_t;

int gb_burst_en = 1;
/**
 @brief Real sram direct write operation I/O
*/
int32
drv_greatbelt_chip_write_sram_entry(uint8 chip_id, uintptr addr,
                          uint32* data, int32 len)
{
    int32 i = 0;
    int32 tmp = 0;
    uint32 length = 0;
    uint8 io_times = 0;

    DRV_PTR_VALID_CHECK(data);

    /* the length must be 4*n(n=1,2,3...) */
    if (len & 0x3)
    {
        return DRV_E_ILLEGAL_LENGTH;
    }

    if(gb_burst_en)
    {
        tmp = (len >> 2);
        io_times = tmp/16 + ((tmp%16)?1:0);
        DRV_ENTRY_LOCK(chip_id);

        /*for burst write,no mask, max length is 16dword at one times*/
        for (i = 0; i < io_times; i++)
        {
            length = (tmp>=16)?16:tmp;

            DRV_IF_ERROR_RETURN_WITH_UNLOCK(drv_greatbelt_chip_write_ext(chip_id, addr, data+i*16, length),p_gb_entry_mutex[chip_id]);

            addr += 4*length;
            if ((tmp > 16))
            {
                tmp = tmp - 16;
            }
        }
        DRV_ENTRY_UNLOCK(chip_id);
    }
    else
    {
        tmp = (len >> 2);
        DRV_ENTRY_LOCK(chip_id);

        for (i = 0; i < tmp; i++)
        {
            DRV_IF_ERROR_RETURN_WITH_UNLOCK(drv_greatbelt_chip_write(chip_id, addr, data[i]), p_gb_entry_mutex[chip_id]);
            addr += 4;
        }

        DRV_ENTRY_UNLOCK(chip_id);
    }

    return DRV_E_NONE;
}

/**
 @brief Real sram direct read operation I/O
*/
int32
drv_greatbelt_chip_read_sram_entry(uint8 chip_id, uintptr addr, uint32* data, int32 len)
{
    int32 i = 0;
    int32 tmp = 0;
    int32 length = 0;
    uint8 io_times = 0;

    DRV_PTR_VALID_CHECK(data);

    /* the length must be 4*n(n=1,2,3...) */
    if (len & 0x3)
    {
        return DRV_E_ILLEGAL_LENGTH;
    }

    if(gb_burst_en)
    {
        tmp = (len >> 2);
        io_times = tmp/16 + ((tmp%16)?1:0);

        DRV_ENTRY_LOCK(chip_id);

        /*for burst write,no mask, max length is 16dword at one times*/
        for (i = 0; i < io_times; i++)
        {
            length = (tmp>=16)?16:tmp;

            DRV_IF_ERROR_RETURN_WITH_UNLOCK(drv_greatbelt_chip_read_ext(chip_id, addr, data+i*16, length),p_gb_entry_mutex[chip_id]);

            addr += 4*length;
            if (tmp > 16)
            {
                tmp = tmp - 16;
            }
        }

        DRV_ENTRY_UNLOCK(chip_id);
    }
    else
    {
        tmp = (len >> 2);
        DRV_ENTRY_LOCK(chip_id);

        for (i = 0; i < tmp; i++)
        {
            DRV_IF_ERROR_RETURN_WITH_UNLOCK(drv_greatbelt_chip_read(chip_id, addr, &data[i]), p_gb_entry_mutex[chip_id]);
            addr += 4;
        }

        DRV_ENTRY_UNLOCK(chip_id);
    }

    return DRV_E_NONE;
}

/**
 @brief Real embeded tcam read operation I/O
*/
STATIC int32
_drv_greatbelt_chip_read_int_tcam_entry(uint8 chip_id, uint32 index,
                              uint32* data, tcam_mem_type_e type)
{
    tcam_ctl_int_cpu_access_cmd_t access;
    uint32 time_out = 0;
    tcam_ctl_int_cpu_rd_data_t tcam_data;
    uint32 cmd;
    int32 ret = DRV_E_NONE;

    DRV_PTR_VALID_CHECK(data);

    if (index >= DRV_INT_TCAM_KEY_MAX_ENTRY_NUM)
    {
        DRV_DBG_INFO("\nEmbeded Tcam's index range is 0~(8k-1), read intTcam entry fail!\n");
        return DRV_E_INVALID_TCAM_INDEX;
    }

    TCAM_LOCK(chip_id);

    sal_memset(&access, 0, sizeof(access));
    sal_memset(&tcam_data, 0, sizeof(tcam_data));

    access.cpu_index = index;
    access.cpu_req = TRUE;
    if (DRV_INT_TCAM_RECORD_MASK == type)
    {
        access.cpu_req_type = CPU_ACCESS_REQ_READ_Y;
    }
    else if (DRV_INT_TCAM_RECORD_DATA == type)
    {
        access.cpu_req_type = CPU_ACCESS_REQ_READ_X;
    }
    else
    {
        DRV_DBG_INFO("\nEmbeded Tcam Memory Type is useless!\n");
        TCAM_UNLOCK(chip_id);
        return DRV_E_INVALID_TCAM_TYPE;
    }

    cmd = DRV_IOW(TcamCtlIntCpuAccessCmd_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(chip_id, 0, cmd, &access);
    if (ret < DRV_E_NONE)
    {
        TCAM_UNLOCK(chip_id);
        return ret;
    }

    while (TRUE)
    {
        cmd = DRV_IOR(TcamCtlIntCpuAccessCmd_t, DRV_ENTRY_FLAG);
        ret = DRV_IOCTL(chip_id, 0, cmd, &access);
        if (ret < DRV_E_NONE)
        {
            TCAM_UNLOCK(chip_id);
            return ret;
        }

        if (access.cpu_read_data_valid)
        {
            break;
        }

        if ((time_out++) > DRV_TIME_OUT)
        {
            TCAM_UNLOCK(chip_id);
            return DRV_E_TIME_OUT;
        }
    }

    cmd = DRV_IOR(TcamCtlIntCpuRdData_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(chip_id, 0, cmd, &tcam_data);
    if (ret < DRV_E_NONE)
    {
        TCAM_UNLOCK(chip_id);
        return ret;
    }

    data[0] = tcam_data.tcam_read_data0;
    data[1] = tcam_data.tcam_read_data1;
    data[2] = tcam_data.tcam_read_data2;

    access.cpu_read_data_valid = FALSE;

    cmd = DRV_IOW(TcamCtlIntCpuAccessCmd_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(chip_id, 0, cmd, &access);
    if (ret < DRV_E_NONE)
    {
        TCAM_UNLOCK(chip_id);
        return ret;
    }

    TCAM_UNLOCK(chip_id);

    return DRV_E_NONE;
}

/**
 @brief Real embeded tcam write operation I/O (data&mask write)
*/
STATIC int32
_drv_greatbelt_chip_write_int_tcam_data_mask(uint8 chip_id, uint32 index,
                                   uint32* data, uint32* mask)
{
    tcam_ctl_int_cpu_access_cmd_t access;
    tcam_ctl_int_cpu_wr_data_t tcam_data;
    tcam_ctl_int_cpu_wr_mask_t tcam_mask;
    uint32 cmd;
    int32 ret = DRV_E_NONE;

    DRV_PTR_VALID_CHECK(data);
    DRV_PTR_VALID_CHECK(mask);

    if (index >= DRV_INT_TCAM_KEY_MAX_ENTRY_NUM)
    {
        DRV_DBG_INFO("\nEmbeded Tcam's index range must be 0~(8k-1), write intTcam entry fail!\n");
        return DRV_E_INVALID_TCAM_INDEX;
    }

    TCAM_LOCK(chip_id);

    sal_memset(&access, 0, sizeof(access));
    sal_memset(&tcam_data, 0, sizeof(tcam_data));
    sal_memset(&tcam_mask, 0, sizeof(tcam_mask));

    /* Attention that we MUST write the mask before writing corresponding data!! */
    /* because the real store in TCAM is X/Y , and the TCAM access write data and mask at the same time, */
    /* so we should write mask at first, then tcam data. */
    /* X = ~data & mask ; Y = data & mask */
    tcam_data.tcam_write_data00 = data[0] & 0xFFFF;
    tcam_data.tcam_write_data01 = data[1];
    tcam_data.tcam_write_data02 = data[2];

    tcam_mask.tcam_write_data10 = mask[0] & 0xFFFF;
    tcam_mask.tcam_write_data11 = mask[1];
    tcam_mask.tcam_write_data12 = mask[2];

    cmd = DRV_IOW(TcamCtlIntCpuWrMask_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(chip_id, 0, cmd, &tcam_mask);
    if (ret < DRV_E_NONE)
    {
        TCAM_UNLOCK(chip_id);
        return ret;
    }

    cmd = DRV_IOW(TcamCtlIntCpuWrData_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(chip_id, 0, cmd, &tcam_data);
    if (ret < DRV_E_NONE)
    {
        TCAM_UNLOCK(chip_id);
        return ret;
    }

    access.cpu_index = index;
    access.cpu_req_type = CPU_ACCESS_REQ_WRITE_DATA_MASK;
    access.cpu_req = TRUE;

    cmd = DRV_IOW(TcamCtlIntCpuAccessCmd_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(chip_id, 0, cmd, &access);
    if (ret < DRV_E_NONE)
    {
        TCAM_UNLOCK(chip_id);
        return ret;
    }

    TCAM_UNLOCK(chip_id);

    return DRV_E_NONE;
}

/**
 @brief Real embeded tcam remove operation I/O
*/
STATIC int32
_drv_greatbelt_chip_remove_int_tcam_entry(uint8 chip_id, uint32 tcam_index)
{
    tcam_ctl_int_cpu_access_cmd_t access;
    uint32 value;
    int32 ret = DRV_E_NONE;

    if (tcam_index >= DRV_INT_TCAM_KEY_MAX_ENTRY_NUM)
    {
        DRV_DBG_INFO("\nEmbeded Tcam's index range must be 0~(8k-1) during remove intTcam entry fail!\n");
        return DRV_E_INVALID_TCAM_INDEX;
    }

    sal_memset(&access, 0, sizeof(access));
    access.cpu_index = tcam_index;
    access.cpu_req = TRUE;
    access.cpu_req_type = CPU_ACCESS_REQ_INVALID_ENTRY;
    value = *(uint32*)&access;

    TCAM_LOCK(chip_id);
    ret = (drv_greatbelt_chip_write(chip_id,
                          TCAM_CTL_INT_CPU_ACCESS_CMD_OFFSET,
                          value));
    if (ret < DRV_E_NONE)
    {
        TCAM_UNLOCK(chip_id);
        return ret;
    }

    TCAM_UNLOCK(chip_id);

    return DRV_E_NONE;
}

/**
 @brief Real embeded LPM tcam read operation I/O
*/
STATIC int32
_drv_greatbelt_chip_read_int_lpm_tcam_entry(uint8 chip_id, uint32 index,
                                  uint32* data, tcam_mem_type_e type)
{
    fib_tcam_cpu_access_t access;
    uint32 time_out = 0;
    fib_tcam_read_data_t tcam_data;
    uint32 cmd;
    int32 ret = DRV_E_NONE;

    DRV_PTR_VALID_CHECK(data);

    if (index >= DRV_INT_LPM_TCAM_MAX_ENTRY_NUM)
    {
        DRV_DBG_INFO("\nLPM Embeded Tcam's index range is 0~255, read intLpmTcam entry fail!\n");
        return DRV_E_INVALID_TCAM_INDEX;
    }

    LPM_TCAM_LOCK(chip_id);

    sal_memset(&access, 0, sizeof(access));
    sal_memset(&tcam_data, 0, sizeof(tcam_data));

    access.tcam_cpu_req_index = index;
    access.tcam_cpu_req = TRUE;
    if (DRV_INT_LPM_TCAM_RECORD_MASK == type)
    {
        access.tcam_cpu_req_type = FIB_CPU_ACCESS_REQ_READ_Y;
    }
    else if (DRV_INT_LPM_TCAM_RECORD_DATA == type)
    {
        access.tcam_cpu_req_type = FIB_CPU_ACCESS_REQ_READ_X;
    }
    else
    {
        DRV_DBG_INFO("\nLPM Embeded Tcam Memory Type is useless!\n");
        LPM_TCAM_UNLOCK(chip_id);
        return DRV_E_INVALID_TCAM_TYPE;
    }

    cmd = DRV_IOW(FibTcamCpuAccess_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(chip_id, 0, cmd, &access);
    if (ret < DRV_E_NONE)
    {
        LPM_TCAM_UNLOCK(chip_id);
        return ret;
    }

    while (TRUE)
    {
        cmd = DRV_IOR(FibTcamCpuAccess_t, DRV_ENTRY_FLAG);
        ret = DRV_IOCTL(chip_id, 0, cmd, &access);
        if (ret < DRV_E_NONE)
        {
            LPM_TCAM_UNLOCK(chip_id);
            return ret;
        }

        if (access.tcam_cpu_rd_data_valid)
        {
            break;
        }

        if ((time_out++) > DRV_TIME_OUT)
        {
            LPM_TCAM_UNLOCK(chip_id);
            return DRV_E_TIME_OUT;
        }
    }

    cmd = DRV_IOR(FibTcamReadData_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(chip_id, 0, cmd, &tcam_data);
    if (ret < DRV_E_NONE)
    {
        LPM_TCAM_UNLOCK(chip_id);
        return ret;
    }

    data[0] = tcam_data.tcam_read_data0;
    data[1] = tcam_data.tcam_read_data1;
    data[2] = tcam_data.tcam_read_data2;

    access.tcam_cpu_rd_data_valid = FALSE;

    cmd = DRV_IOW(FibTcamCpuAccess_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(chip_id, 0, cmd, &access);
    if (ret < DRV_E_NONE)
    {
        LPM_TCAM_UNLOCK(chip_id);
        return ret;
    }

    LPM_TCAM_UNLOCK(chip_id);

    return DRV_E_NONE;
}

/**
 @brief Real embeded LPM tcam write operation I/O (data&mask write)
*/
STATIC int32
_drv_greatbelt_chip_write_int_lpm_tcam_data_mask(uint8 chip_id, uint32 index,
                                       uint32* data, uint32* mask)
{
    fib_tcam_cpu_access_t access;
    fib_tcam_write_data_t tcam_data;
    fib_tcam_write_mask_t tcam_mask;
    uint32 cmd;
    int32 ret = DRV_E_NONE;

    DRV_PTR_VALID_CHECK(data);
    DRV_PTR_VALID_CHECK(mask);

    if (index >= DRV_INT_LPM_TCAM_MAX_ENTRY_NUM)
    {
        DRV_DBG_INFO("\nLPM Embeded Tcam's index range must be 0~(8k-1), write intTcam entry fail!\n");
        return DRV_E_INVALID_TCAM_INDEX;
    }

    LPM_TCAM_LOCK(chip_id);

    sal_memset(&access, 0, sizeof(access));
    sal_memset(&tcam_data, 0, sizeof(tcam_data));
    sal_memset(&tcam_mask, 0, sizeof(tcam_mask));

    /* Attention that we MUST write the mask before writing corresponding data!! */
    /* because the real store in TCAM is X/Y , and the TCAM access write data and mask at the same time, */
    /* so we should write mask at first, then tcam data. */
    /* X = ~data & mask ; Y = data & mask */
    tcam_data.tcam_write_data0 = data[0] & 0xFFFF;
    tcam_data.tcam_write_data1 = data[1];
    tcam_data.tcam_write_data2 = data[2];

    tcam_mask.tcam_write_mask0 = mask[0] & 0xFFFF;
    tcam_mask.tcam_write_mask1 = mask[1];
    tcam_mask.tcam_write_mask2 = mask[2];

    cmd = DRV_IOW(FibTcamWriteMask_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(chip_id, 0, cmd, &tcam_mask);
    if (ret < DRV_E_NONE)
    {
        LPM_TCAM_UNLOCK(chip_id);
        return ret;
    }

    cmd = DRV_IOW(FibTcamWriteData_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(chip_id, 0, cmd, &tcam_data);
    if (ret < DRV_E_NONE)
    {
        LPM_TCAM_UNLOCK(chip_id);
        return ret;
    }

    access.tcam_cpu_req_index = index;
    access.tcam_cpu_req_type = FIB_CPU_ACCESS_REQ_WRITE_DATA_MASK;
    access.tcam_cpu_req = TRUE;

    cmd = DRV_IOW(FibTcamCpuAccess_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(chip_id, 0, cmd, &access);
    if (ret < DRV_E_NONE)
    {
        LPM_TCAM_UNLOCK(chip_id);
        return ret;
    }

    LPM_TCAM_UNLOCK(chip_id);

    return DRV_E_NONE;
}

/**
 @brief Real embeded LPM tcam remove operation I/O
*/
STATIC int32
_drv_greatbelt_chip_remove_int_lpm_tcam_entry(uint8 chip_id, uint32 tcam_index)
{
    fib_tcam_cpu_access_t access;
    uint32 value;
    int32 ret = DRV_E_NONE;

    if (tcam_index >= DRV_INT_LPM_TCAM_MAX_ENTRY_NUM)
    {
        DRV_DBG_INFO("\nLPM Embeded Tcam's index range must be 0~(8k-1) during remove intTcam entry fail!\n");
        return DRV_E_INVALID_TCAM_INDEX;
    }

    sal_memset(&access, 0, sizeof(access));
    access.tcam_cpu_req_index = tcam_index;
    access.tcam_cpu_req = TRUE;
    access.tcam_cpu_req_type = FIB_CPU_ACCESS_REQ_INVALID_ENTRY;
    value = *(uint32*)&access;

    LPM_TCAM_LOCK(chip_id);
    ret = (drv_greatbelt_chip_write(chip_id,
                          FIB_TCAM_CPU_ACCESS_OFFSET,
                          value));
    if (ret < DRV_E_NONE)
    {
        LPM_TCAM_UNLOCK(chip_id);
        return ret;
    }

    LPM_TCAM_UNLOCK(chip_id);

    return DRV_E_NONE;
}

/**
 @brief convert embeded tcam content from X/Y format to data/mask format
*/
STATIC int32
_drv_greatbelt_chip_convert_tcam_dump_content(uint8 chip_id, uint32* data, uint32* mask)
{
#define TCAM_ENTRY_WIDTH 80
    uint32 bit_pos = 0;
    uint32 index = 0, bit_offset = 0;

    /* for IBM embeded tcam, mask bit = 1 denote compare,
       self-define the return mask bit = 1 denote compare
       to unify the embeded and external tcam read interface's return value;

       X    Y    Read_Data    Read_Mask   Return_Data   Return_Mask
       0    0       X            0            0            0
       0    1       1            1            1            1
       1    0       0            1            0            1
       1    1       X            X            0            0
       X=1, Y=1: No Write but read.  */

    /* data[1] -- [64,80]; data[2] -- [32,63]; data[3] -- [0,31] */
    for (bit_pos = 0; bit_pos < TCAM_ENTRY_WIDTH; bit_pos++)
    {
        index = 2 - bit_pos / 32;
        bit_offset = bit_pos % 32;

        if ((!IS_BIT_SET(data[index], bit_offset))
            && IS_BIT_SET(mask[index], bit_offset))    /* X = 0; Y = 1 */
        {
            SET_BIT(data[index], bit_offset);      /* Return_Data = 1; Return_Mask = 1 */
        }
        else if (IS_BIT_SET(data[index], bit_offset)
                 && (!IS_BIT_SET(mask[index], bit_offset))) /* X = 1; Y = 0 */
        {
            CLEAR_BIT(data[index], bit_offset);    /* Return_Data = 0; Return_Mask = 1 */
            SET_BIT(mask[index], bit_offset);
        }
        else if ((!IS_BIT_SET(data[index], bit_offset))
                 && (!IS_BIT_SET(mask[index], bit_offset))) /* X = 0; Y = 0 */
        {
            continue;                              /* Return_Data = X(not cared); Return_Mask = 0 */
            /* Here data = X = 0, self-defined */
        }
        else                                           /* X = 1; Y = 1 */
        {
            /* Here data = 0, mask = 0, self-defined */
            CLEAR_BIT(data[index], bit_offset);    /* Return_Data = 0; Return_Mask = 0 */
            CLEAR_BIT(mask[index], bit_offset);
             /*DRV_DBG_INFO("ERROR! Read un-set Tcam entry!\n");*/
        }
    }

    return DRV_E_NONE;
}

/**
 @brief Init embeded and external tcam mutex
*/
int32
drv_greatbelt_chip_tcam_mutex_init(uint8 chip_id_offset)
{
    int32 ret;

    DRV_CHIP_ID_VALID_CHECK(chip_id_offset + drv_gb_init_chip_info.drv_init_chipid_base);

    ret = sal_mutex_create(&p_gb_tcam_mutex[chip_id_offset]);
    if (ret || (!p_gb_tcam_mutex[chip_id_offset]))
    {
        return DRV_E_FAIL_CREATE_MUTEX;
    }

    ret = sal_mutex_create(&p_gb_lpm_tcam_mutex[chip_id_offset]);
    if (ret || (!p_gb_lpm_tcam_mutex[chip_id_offset]))
    {
        return DRV_E_FAIL_CREATE_MUTEX;
    }

    ret = sal_mutex_create(&p_gb_entry_mutex[chip_id_offset]);
    if (ret || (!p_gb_entry_mutex[chip_id_offset]))
    {
        return DRV_E_FAIL_CREATE_MUTEX;
    }

    ret = sal_mutex_create(&p_gb_mep_mutex[chip_id_offset]);
    if (ret || (!p_gb_mep_mutex[chip_id_offset]))
    {
        return DRV_E_FAIL_CREATE_MUTEX;
    }

    return DRV_E_NONE;
}

STATIC int32
_drv_greatbelt_chip_init_tcam_bist_lookup(uint32 chip_id, uint32* key,
                                uint32 aclqos_en, uint32 key_size, uint32* index)
{
    uint32 time_out = 0;
    uint32 cmd, entries, enumber, bist_done = FALSE;
    tcam_ctl_int_bist_ctl_t bist_control;
    tcam_ctl_int_cpu_request_mem_t bist_request;
    tcam_ctl_int_cpu_result_mem_t bist_result;

    /*initial all the tcam ctrl ram and reg*/
    sal_memset(&bist_control, 0, sizeof(bist_control));
    sal_memset(&bist_request, 0, sizeof(bist_request));
    sal_memset(&bist_result, 0, sizeof(bist_result));

    cmd = DRV_IOW(TcamCtlIntBistCtl_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, &bist_control));

    for (entries = 0; entries < TCAM_CTL_INT_CPU_REQUEST_MEM_MAX_INDEX; entries++)
    {
        cmd = DRV_IOW(TcamCtlIntCpuRequestMem_t, DRV_ENTRY_FLAG);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, entries, cmd, &bist_request));
    }

    for (entries = 0; entries < TCAM_CTL_INT_CPU_RESULT_MEM_MAX_INDEX; entries++)
    {
        cmd = DRV_IOW(TcamCtlIntCpuResultMem_t, DRV_ENTRY_FLAG);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, entries, cmd, &bist_result));
    }

    /* calculate entries number */
    /* keysize:
       0  -  80bits
       1  -  160bits
       2  -  320bits
       3  -  640bits */
    if (key_size < 2)
    {
        enumber = 1;
    }
    else if (key_size < 3)
    {
        enumber = 2;
    }
    else
    {
        enumber = 4;
    }

    /* write key to request ram */
    for (entries = 0; entries < enumber; entries++)
    {
        sal_memset(&bist_request, 0, sizeof(bist_request));
        if (0 == key_size)
        {
            bist_request.key31_to0 = key[3];
            bist_request.key63_to32 = key[2];
            bist_request.key79_to64 = key[1] & 0xFFFF;
        }
        else
        {
            bist_request.key31_to0 = key[entries * 8 + 7];
            bist_request.key63_to32 = key[entries * 8 + 6];
            bist_request.key79_to64 = key[entries * 8 + 5] & 0xFFFF;

            bist_request.key111_to80 = key[entries * 8 + 3];
            bist_request.key143_to112 = key[entries * 8 + 2];
            bist_request.key159_to144 = key[entries * 8 + 1] & 0xFFFF;
            /*      bist_request.key_valid = TRUE;
                    bist_request.key_size = enumber;
                    bist_request.key_cmd = (aclqos_en? TRUE : FALSE)&0x1;*/
        }

        bist_request.key_valid = TRUE;
        bist_request.key_size = key_size;
        bist_request.key_dual_lkup = (aclqos_en ? TRUE : FALSE);

        cmd = DRV_IOW(TcamCtlIntCpuRequestMem_t, DRV_ENTRY_FLAG);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, entries, cmd, &bist_request));
    }

    /* write bist eanble and bist once */
    bist_control.cfg_bist_en = TRUE;
    bist_control.cfg_bist_once = TRUE;
    bist_control.cfg_bist_entries = enumber - 1; /* enumber - 1 for case of 320bits key */

    cmd = DRV_IOW(TcamCtlIntBistCtl_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, &bist_control));

    /* bist done */
    while (TRUE)
    {
        cmd = DRV_IOR(TcamCtlIntBistPointers_t, TcamCtlIntBistPointers_CfgBistResultDoneOnce_f);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, &bist_done));

        if (bist_done)
        {
            break;
        }

        if ((time_out++) > DRV_TIME_OUT)
        {
            return DRV_E_TIME_OUT;
        }
    }

    /* read resutl ram */
    cmd = DRV_IOR(TcamCtlIntCpuResultMem_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, (enumber - 1) + 64, cmd, &bist_result));
    if (aclqos_en == 1)
    {
        index[0] = bist_result.index_valid;
        index[1] = bist_result.index_acl0;
        index[2] = bist_result.index_acl1;
        index[3] = bist_result.index_acl2;
        index[4] = bist_result.index_acl3;
        DRV_DBG_INFO("\nTcam hit, AclIndex1 = 0x%x; AclIndex2 = 0x%0x, AclIndex2 = 0x%x; AclIndex3 = 0x%0x !\n",
                     index[1], index[2], index[3], index[4]);
    }
    else
    {
        index[0] = bist_result.index_valid;
        index[1] = bist_result.index_acl0;
        index[2] = bist_result.index_acl1;
        DRV_DBG_INFO("\nTcam hit, index1 = 0x%x; index2 = 0x%0x !\n", index[1], index[2]);
    }

    /* Disable bist */
    sal_memset(&bist_control, 0, sizeof(bist_control));
    cmd = DRV_IOW(TcamCtlIntBistCtl_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, &bist_control));

    return DRV_E_NONE;
}

#if 0
STATIC int32
_drv_greatbelt_chip_lpm_tcam_bist_lookup(uint32 chip_id, uint32* key,
                               uint32 key_size, uint32* index)
{
    uint32 time_out = 0;
    uint32 cmd, entries, enumber, bist_done = FALSE;
    tcam_ctl_int_bist_ctl_t bist_control;
    tcam_ctl_int_cpu_request_mem_t bist_request;
    tcam_ctl_int_cpu_result_mem_t bist_result;

    /*initial all the tcam ctrl ram and reg*/
    sal_memset(&bist_control, 0, sizeof(bist_control));
    sal_memset(&bist_request, 0, sizeof(bist_request));
    sal_memset(&bist_result, 0, sizeof(bist_result));

    cmd = DRV_IOW(TcamCtlIntBistCtl_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, &bist_control));

    for (entries = 0; entries < TCAM_CTL_INT_CPU_REQUEST_MEM_MAX_INDEX; entries++)
    {
        cmd = DRV_IOW(TcamCtlIntCpuRequestMem_t, DRV_ENTRY_FLAG);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, entries, cmd, &bist_request));
    }

    for (entries = 0; entries < TCAM_CTL_INT_CPU_RESULT_MEM_MAX_INDEX; entries++)
    {
        cmd = DRV_IOW(TcamCtlIntCpuResultMem_t, DRV_ENTRY_FLAG);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, entries, cmd, &bist_result));
    }

    /* calculate entries number */
    if (key_size < 2)
    {
        enumber = 1;
    }
    else if (key_size < 3)
    {
        enumber = 2;
    }
    else
    {
        enumber = 4;
    }

    /* write key to request ram */
    for (entries = 0; entries < enumber; entries++)
    {
        sal_memset(&bist_request, 0, sizeof(bist_request));
        if (0 == key_size)
        {
            bist_request.key31_to0 = key[3];
            bist_request.key63_to32 = key[2];
            bist_request.key79_to64 = key[1] & 0xFFFF;
        }
        else
        {
            bist_request.key31_to0 = key[entries * 8 + 7];
            bist_request.key63_to32 = key[entries * 8 + 6];
            bist_request.key79_to64 = key[entries * 8 + 5] & 0xFFFF;

            bist_request.key111_to80 = key[entries * 8 + 3];
            bist_request.key143_to112 = key[entries * 8 + 2];
            bist_request.key159_to144 = key[entries * 8 + 1] & 0xFFFF;
            /*      bist_request.key_valid = TRUE;
                    bist_request.key_size = enumber;
                    bist_request.key_cmd = (aclqos_en? TRUE : FALSE)&0x1;*/
        }

        bist_request.key_valid = TRUE;
        bist_request.key_size = key_size;
        bist_request.key_dual_lkup = (aclqos_en ? TRUE : FALSE);

        cmd = DRV_IOW(TcamCtlIntCpuRequestMem_t, DRV_ENTRY_FLAG);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, entries, cmd, &bist_request));
    }

    /* write bist eanble and bist once */
    bist_control.cfg_bist_en = TRUE;
    bist_control.cfg_bist_once = TRUE;
    bist_control.cfg_bist_entries = enumber - 1; /* enumber - 1 for case of 320bits key */

    cmd = DRV_IOW(TcamCtlIntBistCtl_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, &bist_control));

    /* bist done */
    while (TRUE)
    {
        cmd = DRV_IOR(TcamCtlIntBistPointers_t, TcamCtlIntBistPointers_CfgBistResultDoneOnce_f);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, &bist_done));

        if (bist_done)
        {
            break;
        }

        if ((time_out++) > DRV_TIME_OUT)
        {
            return DRV_E_TIME_OUT;
        }
    }

    /* read resutl ram */
    cmd = DRV_IOR(TcamCtlIntCpuResultMem_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, (enumber - 1) + 64, cmd, &bist_result));
    if (aclqos_en == 1)
    {
        index[0] = bist_result.index_valid;
        index[1] = bist_result.index_acl0;
        index[2] = bist_result.index_acl1;
        index[3] = bist_result.index_acl2;
        index[4] = bist_result.index_acl3;
        DRV_DBG_INFO("\nTcam hit, AclIndex1 = 0x%x; AclIndex2 = 0x%0x, AclIndex2 = 0x%x; AclIndex3 = 0x%0x !\n",
                     index[1], index[2], index[3], index[4]);
    }
    else
    {
        index[0] = bist_result.index_valid;
        index[1] = bist_result.index_qos;
        index[2] = bist_result.index_acl;
        DRV_DBG_INFO("\nTcam hit, index1 = 0x%x; index2 = 0x%0x !\n", index[1], index[2]);
    }

    /* Disable bist */
    sal_memset(&bist_control, 0, sizeof(bist_control));
    cmd = DRV_IOW(TcamCtlIntBistCtl_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, &bist_control));
    return DRV_E_NONE;
}

#endif

/**
 @brief Embeded Tcam lkp process
  Only use for the show forwarding tools
*/
int32
drv_greatbelt_chip_tcam_lookup(uint8 chip_id, uint8* key, int32 keysize,
                     bool dual_lkp, uint32* result_index)
{
    int32 ret = DRV_E_NONE;
    uint32 index[5] = {0};
    uint8 key_size = 0;

    result_index[0] = 0xffffffff;
    result_index[1] = 0xffffffff;
    result_index[2] = 0xffffffff;
    result_index[3] = 0xffffffff;

    if (DRV_BYTES_PER_ENTRY == keysize)      /* 80 bits */
    {
        key_size = 0;
    }
    else if (DRV_BYTES_PER_ENTRY * 2 == keysize) /* 160 bits */
    {
        key_size = 1;
    }
    else if (DRV_BYTES_PER_ENTRY * 4 == keysize) /* 320 bits */
    {
        key_size = 2;
    }
    else if (DRV_BYTES_PER_ENTRY * 8 == keysize) /* 640 bits */
    {
        key_size = 3;
    }
    else
    {
        DRV_DBG_INFO("Invalid Tcam keysize = %d return! The value Must be 12B(80bits), 24B(160bits), 48B(320bits), 96B(640bits)\n", keysize);
        return DRV_E_TCAM_KEY_CONFIG_ERROR;
    }

    /* will be updated later */
    ret = _drv_greatbelt_chip_init_tcam_bist_lookup(chip_id, (uint32*)key, dual_lkp, key_size, index);
    if (ret != DRV_E_NONE)
    {
        DRV_DBG_INFO("ERROR! CPU Tcam bist lookup error return!\n");
        return ret;
    }

    if (1 == index[0])
    {
        result_index[0] = index[1];
        result_index[1] = index[2];
        result_index[2] = index[3];
        result_index[3] = index[4];
    }

    return ret;
}

/**
 @brief Real sram direct write operation I/O
*/
int32
drv_greatbelt_chip_write_interrupt_entry(uint8 chip_id, uint32 addr,
                          uint32* data, int32 len)
{
    int32 i, length;
    length = len;

    DRV_PTR_VALID_CHECK(data);

    /* the length must be 4*n(n=1,2,3...) */
    if (length & 0x3)
    {
        return DRV_E_ILLEGAL_LENGTH;
    }

    length = (length >> 2);

    DRV_ENTRY_LOCK(chip_id);

    for (i = 0; i < length; i++)
    {
        DRV_IF_ERROR_RETURN_WITH_UNLOCK(drv_greatbelt_chip_write(chip_id, addr, data[i]), p_gb_entry_mutex[chip_id]);
        addr += 16;
    }

    DRV_ENTRY_UNLOCK(chip_id);

    return DRV_E_NONE;
}

/**
 @brief The function write table data to a sram memory location
*/
int32
drv_greatbelt_chip_sram_tbl_write(uint8 chip_id, tbls_id_t tbl_id, uint32 index, uint32* data)
{
    uint32 start_data_addr = 0, entry_size = 0, max_index_num = 0;
    int32 ret = DRV_E_NONE;

    DRV_PTR_VALID_CHECK(data);
    DRV_CHIP_ID_VALID_CHECK(chip_id);
    DRV_TBL_ID_VALID_CHECK(tbl_id);

    /* check if the index num exceed the max index num of the tbl */
    max_index_num = TABLE_MAX_INDEX(tbl_id);
    entry_size = TABLE_ENTRY_SIZE(tbl_id);

    /* According to index value to get the table 's index address in sw */
    DRV_IF_ERROR_RETURN(drv_greatbelt_table_get_hw_addr(tbl_id, index, &start_data_addr));

    if (max_index_num <= index)
    {
        DRV_DBG_INFO("ERROR! (drv_greatbelt_write_sram_tbl): chip-0x%x, tbl-0x%x, index-0x%x exceeds the max_index-0x%x.\n",
                     chip_id, tbl_id, index, max_index_num);
        return DRV_E_EXCEED_MAX_SIZE;
    }

    DRV_IS_INTR_MULTI_WORD_TBL(tbl_id)
    {
        ret = drv_greatbelt_chip_write_interrupt_entry(chip_id, start_data_addr, data, entry_size);
    }
    else if((DRV_BYTES_PER_ENTRY*4 == entry_size) && (drv_greatbelt_table_is_dynamic_table(tbl_id)))
    {
        ret = drv_greatbelt_chip_write_sram_entry(chip_id, start_data_addr, data, DRV_BYTES_PER_ENTRY*2);
        ret = drv_greatbelt_chip_write_sram_entry(chip_id, start_data_addr+DRV_ADDR_BYTES_PER_ENTRY*2, data+6, DRV_BYTES_PER_ENTRY*2);
    }
    else
    {
        ret = drv_greatbelt_chip_write_sram_entry(chip_id, start_data_addr, data, entry_size);
    }

    drv_greatbelt_ecc_recover_store(chip_id, tbl_id, index, data);
    return ret;
}


/**
 @brief write register field data to a memory location on chip
*/
int32
drv_greatbelt_chip_register_field_write(uint8 chip_id, tbls_id_t tbl_id, uint32 fld_id, uint32* data)
{
    uint32 start_data_addr = 0;
    int32 ret = DRV_E_NONE;
    fields_t* field = NULL;
    uint32 offset   = 0;

    DRV_PTR_VALID_CHECK(data);
    DRV_CHIP_ID_VALID_CHECK(chip_id);
    DRV_TBL_ID_VALID_CHECK(tbl_id);

    /* According to index value to get the table 's index address in sw */
    DRV_IF_ERROR_RETURN(drv_greatbelt_table_get_hw_addr(tbl_id, 0, &start_data_addr));

    field   = drv_greatbelt_find_field(tbl_id, fld_id);
    offset  = field->word_offset*DRV_BYTES_PER_WORD;
    ret     = drv_greatbelt_chip_write_sram_entry(chip_id, (start_data_addr + offset), (data + field->word_offset), DRV_BYTES_PER_WORD);

    return ret;
}

/**
 @brief Real sram direct read operation I/O
*/
int32
drv_greatbelt_chip_read_interrupt_entry(uint8 chip_id, uint32 addr, uint32* data, int32 len)
{
    int32 i, length;
    length = len;

    DRV_PTR_VALID_CHECK(data);

    /* the length must be 4*n(n=1,2,3...) */
    if (length & 0x3)
    {
        return DRV_E_ILLEGAL_LENGTH;
    }

    length = (length >> 2);

    DRV_ENTRY_LOCK(chip_id);

    for (i = 0; i < length; i++)
    {
        DRV_IF_ERROR_RETURN_WITH_UNLOCK(drv_greatbelt_chip_read(chip_id, addr, &data[i]), p_gb_entry_mutex[chip_id]);
        addr += 16;
    }

    DRV_ENTRY_UNLOCK(chip_id);

    return DRV_E_NONE;
}

/**
 @brief The function read table data from a sram memory location
*/
int32
drv_greatbelt_chip_sram_tbl_read(uint8 chip_id, tbls_id_t tbl_id, uint32 index, uint32* data)
{
#define DYN_SRAM_ENTRY_BYTE 16
    uint32 start_data_addr = 0, entry_size = 0, max_index_num = 0;
    int32 ret = DRV_E_NONE;

    DRV_PTR_VALID_CHECK(data);
    DRV_CHIP_ID_VALID_CHECK(chip_id);
    DRV_TBL_ID_VALID_CHECK(tbl_id);

    /* check if the index num exceed the max index num of the tbl */
    max_index_num = TABLE_MAX_INDEX(tbl_id);
    entry_size = TABLE_ENTRY_SIZE(tbl_id);

    /* According to index value to get the table 's index address in sw */
    DRV_IF_ERROR_RETURN(drv_greatbelt_table_get_hw_addr(tbl_id, index, &start_data_addr));

    if (max_index_num <= index)
    {
        DRV_DBG_INFO("ERROR! (drv_greatbelt_chip_sram_tbl_read): chip-0x%x, tbl-0x%x, index-0x%x exceeds the max_index-0x%x.\n",
                     chip_id, tbl_id, index, max_index_num);
        return DRV_E_EXCEED_MAX_SIZE;
    }

    DRV_IS_INTR_MULTI_WORD_TBL(tbl_id)
    {
        ret = drv_greatbelt_chip_read_interrupt_entry(chip_id, start_data_addr, data, entry_size);
    }
    else if((DRV_BYTES_PER_ENTRY*4 == entry_size) && (drv_greatbelt_table_is_dynamic_table(tbl_id)))
    {
        ret = drv_greatbelt_chip_read_sram_entry(chip_id, start_data_addr, data, DRV_BYTES_PER_ENTRY*2);
        ret = drv_greatbelt_chip_read_sram_entry(chip_id, start_data_addr+DRV_ADDR_BYTES_PER_ENTRY*2, data+6, DRV_BYTES_PER_ENTRY*2);
    }
    else
    {
        ret = drv_greatbelt_chip_read_sram_entry(chip_id, start_data_addr, data, entry_size);
    }

    return ret;
}

/**
 @brief write tcam interface (include operate model and real tcam)
*/
int32
drv_greatbelt_chip_tcam_tbl_write(uint8 chip_id_offset, tbls_id_t tbl_id, uint32 index, tbl_entry_t* entry)
{
    uint32* mask = NULL;
    uint32* data = NULL;
    uint32 hw_database_addr = TABLE_DATA_BASE(tbl_id);
    uint32 entry_size = TABLE_ENTRY_SIZE(tbl_id);
    uint32 key_size = TCAM_KEY_SIZE(tbl_id);
    uint32 entry_num_each_idx = 0, entry_idx = 0, offset = 0;
    uint32 index_by_tcam = 0;
    uint8 chip_id = chip_id_offset + drv_gb_init_chip_info.drv_init_chipid_base;
    bool is_lpm_tcam = FALSE;
    uint32 max_int_tcam_data_base_tmp;
    uint32 max_int_lpm_tcam_data_base_tmp;

    DRV_PTR_VALID_CHECK(entry);
    DRV_PTR_VALID_CHECK(entry->data_entry);
    DRV_PTR_VALID_CHECK(entry->mask_entry);
    DRV_TBL_ID_VALID_CHECK(tbl_id);

    if (!drv_greatbelt_table_is_tcam_key(tbl_id))
    {
        DRV_DBG_INFO("@@ ERROR! When write operation, INVALID Tcam key TblID! TblID: %d, file:%s line:%d function:%s\n",
                     tbl_id, __FILE__, __LINE__, __FUNCTION__);
        return DRV_E_INVALID_TBL;
    }

    if (TABLE_MAX_INDEX(tbl_id) <= index)
    {
        DRV_DBG_INFO("ERROR (drv_greatbelt_chip_tcam_tbl_write): chip-0x%x, tbl-%d, index-0x%x exceeds the max_index-0x%x.\n",
                     chip_id, tbl_id, index, TABLE_MAX_INDEX(tbl_id));
        return DRV_E_EXCEED_MAX_SIZE;
    }

    max_int_tcam_data_base_tmp = DRV_INT_TCAM_KEY_DATA_ASIC_BASE
        + (DRV_INT_TCAM_KEY_MAX_ENTRY_NUM * DRV_BYTES_PER_ENTRY);
    max_int_lpm_tcam_data_base_tmp = DRV_INT_LPM_TCAM_DATA_ASIC_BASE
        + (DRV_INT_LPM_TCAM_MAX_ENTRY_NUM * DRV_BYTES_PER_ENTRY);

    /* the key's each index includes 80 bits entry number */
    entry_num_each_idx = key_size / DRV_BYTES_PER_ENTRY;

    if ((hw_database_addr >= DRV_INT_TCAM_KEY_DATA_ASIC_BASE)
        && (hw_database_addr < max_int_tcam_data_base_tmp))
    {

        if (8 == entry_num_each_idx)
        {
            index_by_tcam = (hw_database_addr - DRV_INT_TCAM_KEY_DATA_ASIC_BASE) / DRV_BYTES_PER_ENTRY + (index * 4);
        }
        else
        {
            index_by_tcam = (hw_database_addr - DRV_INT_TCAM_KEY_DATA_ASIC_BASE) / DRV_BYTES_PER_ENTRY + (index * entry_num_each_idx);
        }

        is_lpm_tcam = FALSE;
    }
    else if ((hw_database_addr >= DRV_INT_LPM_TCAM_DATA_ASIC_BASE)
             && (hw_database_addr < max_int_lpm_tcam_data_base_tmp))
    {
        index_by_tcam = (hw_database_addr - DRV_INT_LPM_TCAM_DATA_ASIC_BASE + (index * DRV_BYTES_PER_ENTRY)) / DRV_BYTES_PER_ENTRY;
        is_lpm_tcam = TRUE;
    }
    else
    {
        DRV_DBG_INFO("Tcam key id 0x%x 's database is not correct, please check it!\n", tbl_id);
        return DRV_E_TCAM_KEY_DATA_ADDRESS;
    }

#if 0
    /* check whether the int tcam index is valid on emulation board */
    ret = _drv_check_emulation_tcam_address_valid(index_by_tcam * TCAM_MIN_UNIT_BYTES, (!in_external_tcam));
    if (FALSE == ret)
    {
        DRV_DBG_INFO("\nTcam key id 0x%x 's allocation is not correct, please check it!\n", tbl_id);
        return DRV_E_TCAM_KEY_DATA_ADDRESS;
    }

#endif

    offset = (entry_size - key_size) / DRV_BYTES_PER_WORD; /* word offset */

    data = entry->data_entry + offset;
    mask = entry->mask_entry + offset;

    for (entry_idx = 0; entry_idx < entry_num_each_idx; ++entry_idx)
    {
        /* write real tcam address */
        if (!is_lpm_tcam)
        {
            if (!IS_BIT_SET(entry_idx, 2))
            {
                 /*DRV_DBG_INFO("+++ Write Tcam Index = 0x%x \r\n", index_by_tcam);*/
                DRV_IF_ERROR_RETURN(_drv_greatbelt_chip_write_int_tcam_data_mask(chip_id, index_by_tcam,
                                                                       data + entry_idx * DRV_WORDS_PER_ENTRY,
                                                                       mask + entry_idx * DRV_WORDS_PER_ENTRY));
            }
            else
            {
                 /*DRV_DBG_INFO("+++ Write Tcam Index = 0x%x \r\n", (index_by_tcam - 4 + DRV_ACL_TCAM_640_OFFSET_IDX));*/
                DRV_IF_ERROR_RETURN(_drv_greatbelt_chip_write_int_tcam_data_mask(chip_id, (index_by_tcam - 4 + DRV_ACL_TCAM_640_OFFSET_IDX),
                                                                       data + entry_idx * DRV_WORDS_PER_ENTRY,
                                                                       mask + entry_idx * DRV_WORDS_PER_ENTRY));
            }
        }
        else
        {
            DRV_IF_ERROR_RETURN(_drv_greatbelt_chip_write_int_lpm_tcam_data_mask(chip_id, index_by_tcam,
                                                                       data + entry_idx * DRV_WORDS_PER_ENTRY,
                                                                       mask + entry_idx * DRV_WORDS_PER_ENTRY));
        }

        index_by_tcam++;
    }

    return DRV_E_NONE;
}

/**
 @brief read tcam interface (include operate model and real tcam)
*/
int32
drv_greatbelt_chip_tcam_tbl_read(uint8 chip_id_offset, tbls_id_t tbl_id, uint32 index, tbl_entry_t* entry)
{
    uint32* mask = NULL;
    uint32* data = NULL;
    uint32 hw_database_addr = TABLE_DATA_BASE(tbl_id);
    uint32 entry_size = TABLE_ENTRY_SIZE(tbl_id);
    uint32 key_size = TCAM_KEY_SIZE(tbl_id);
    uint32 entry_num_each_idx, entry_idx, offset;
    bool is_lpm_tcam = FALSE;
    uint8 chip_id = chip_id_offset + drv_gb_init_chip_info.drv_init_chipid_base;
    uint32 index_by_tcam;
    uint32 max_int_tcam_data_base_tmp;
    uint32 max_int_lpm_tcam_data_base_tmp;

    DRV_PTR_VALID_CHECK(entry);
    DRV_PTR_VALID_CHECK(entry->data_entry);
    DRV_PTR_VALID_CHECK(entry->mask_entry);
    DRV_CHIP_ID_VALID_CHECK(chip_id);
    DRV_TBL_ID_VALID_CHECK(tbl_id);

    if (!drv_greatbelt_table_is_tcam_key(tbl_id))
    {
        DRV_DBG_INFO("@@ ERROR! When read operation, INVALID Tcam key TblID! TblID: %d, file:%s line:%d function:%s\n",
                     tbl_id, __FILE__, __LINE__, __FUNCTION__);
        return DRV_E_INVALID_TBL;
    }

    if (TABLE_MAX_INDEX(tbl_id) <= index)
    {
        DRV_DBG_INFO("ERROR (drv_greatbelt_chip_tcam_tbl_read): chip-0x%x, tbl-%d, index-0x%x exceeds the max_index-0x%x.\n",
                     chip_id, tbl_id, index, TABLE_MAX_INDEX(tbl_id));
        return DRV_E_EXCEED_MAX_SIZE;
    }

    max_int_tcam_data_base_tmp = DRV_INT_TCAM_KEY_DATA_ASIC_BASE
        + (DRV_INT_TCAM_KEY_MAX_ENTRY_NUM * DRV_BYTES_PER_ENTRY);
    max_int_lpm_tcam_data_base_tmp = DRV_INT_LPM_TCAM_DATA_ASIC_BASE
        + (DRV_INT_LPM_TCAM_MAX_ENTRY_NUM * DRV_BYTES_PER_ENTRY);

    /* the key's each index includes 80 bits entry number */
    entry_num_each_idx = key_size / DRV_BYTES_PER_ENTRY;

    if ((hw_database_addr >= DRV_INT_TCAM_KEY_DATA_ASIC_BASE)
        && (hw_database_addr < max_int_tcam_data_base_tmp))
    {

        if (8 == entry_num_each_idx)
        {
            index_by_tcam = (hw_database_addr - DRV_INT_TCAM_KEY_DATA_ASIC_BASE) / DRV_BYTES_PER_ENTRY + (index * 4);
        }
        else
        {
            index_by_tcam = (hw_database_addr - DRV_INT_TCAM_KEY_DATA_ASIC_BASE) / DRV_BYTES_PER_ENTRY + (index * entry_num_each_idx);
        }

        is_lpm_tcam = FALSE;
    }
    else if ((hw_database_addr >= DRV_INT_LPM_TCAM_DATA_ASIC_BASE)
             && (hw_database_addr < max_int_lpm_tcam_data_base_tmp))
    {
        index_by_tcam = (hw_database_addr - DRV_INT_LPM_TCAM_DATA_ASIC_BASE + (index * DRV_BYTES_PER_ENTRY)) / DRV_BYTES_PER_ENTRY;
        is_lpm_tcam = TRUE;
    }
    else
    {
        DRV_DBG_INFO("Tcam key id 0x%x 's database is not correct, please check it!\n", tbl_id);
        return DRV_E_TCAM_KEY_DATA_ADDRESS;
    }

#if 0
    /* check whether the int tcam index is valid on emulation board */
    ret = _drv_check_emulation_tcam_address_valid(index_by_tcam * TCAM_MIN_UNIT_BYTES, (!in_external_tcam));
    if (FALSE == ret)
    {
        DRV_DBG_INFO("\nTcam key id 0x%x 's allocation is not correct, please check it!\n", tbl_id);
        return DRV_E_TCAM_KEY_DATA_ADDRESS;
    }

#endif

    offset = (entry_size - key_size) / DRV_BYTES_PER_WORD; /* word offset */

    data = entry->data_entry + offset;
    mask = entry->mask_entry + offset;

    for (entry_idx = 0; entry_idx < entry_num_each_idx; ++entry_idx)
    {
        /* read real tcam address */
        if (!is_lpm_tcam)
        {
            if (!IS_BIT_SET(entry_idx, 2))
            {
                 /*DRV_DBG_INFO("+++ Read Tcam Index = 0x%x \r\n", index_by_tcam);*/
                DRV_IF_ERROR_RETURN(_drv_greatbelt_chip_read_int_tcam_entry(chip_id, index_by_tcam,
                                                                  mask + entry_idx * DRV_WORDS_PER_ENTRY,
                                                                  DRV_INT_TCAM_RECORD_MASK));

                DRV_IF_ERROR_RETURN(_drv_greatbelt_chip_read_int_tcam_entry(chip_id, index_by_tcam,
                                                                  data + entry_idx * DRV_WORDS_PER_ENTRY,
                                                                  DRV_INT_TCAM_RECORD_DATA));
            }
            else
            {
                 /*DRV_DBG_INFO("+++ Read Tcam Index = 0x%x \r\n", (index_by_tcam - 4 + DRV_ACL_TCAM_640_OFFSET_IDX));*/
                DRV_IF_ERROR_RETURN(_drv_greatbelt_chip_read_int_tcam_entry(chip_id, (index_by_tcam - 4 + DRV_ACL_TCAM_640_OFFSET_IDX),
                                                                  mask + entry_idx * DRV_WORDS_PER_ENTRY,
                                                                  DRV_INT_TCAM_RECORD_MASK));

                DRV_IF_ERROR_RETURN(_drv_greatbelt_chip_read_int_tcam_entry(chip_id, (index_by_tcam - 4 + DRV_ACL_TCAM_640_OFFSET_IDX),
                                                                  data + entry_idx * DRV_WORDS_PER_ENTRY,
                                                                  DRV_INT_TCAM_RECORD_DATA));
            }
        }
        else
        {
            DRV_IF_ERROR_RETURN(_drv_greatbelt_chip_read_int_lpm_tcam_entry(chip_id, index_by_tcam,
                                                                  mask + entry_idx * DRV_WORDS_PER_ENTRY,
                                                                  DRV_INT_LPM_TCAM_RECORD_MASK));

            DRV_IF_ERROR_RETURN(_drv_greatbelt_chip_read_int_lpm_tcam_entry(chip_id, index_by_tcam,
                                                                  data + entry_idx * DRV_WORDS_PER_ENTRY,
                                                                  DRV_INT_LPM_TCAM_RECORD_MASK));
        }

        DRV_IF_ERROR_RETURN(_drv_greatbelt_chip_convert_tcam_dump_content(chip_id,
                                                                data + entry_idx * DRV_WORDS_PER_ENTRY,
                                                                mask + entry_idx * DRV_WORDS_PER_ENTRY));

        index_by_tcam++;
    }

    return DRV_E_NONE;
}

/**
 @brief remove tcam entry interface (include operate model and real tcam)
*/
int32
drv_greatbelt_chip_tcam_tbl_remove(uint8 chip_id_offset, tbls_id_t tbl_id, uint32 index)
{
    uint32 entry_idx = 0, entry_num_each_idx = 0;
    uint32 hw_database_addr = 0, key_size = 0;
    uint32 index_by_tcam = 0;
    uint8 chip_id = chip_id_offset + drv_gb_init_chip_info.drv_init_chipid_base;
    bool is_lpm_tcam = FALSE;
    uint32 max_int_tcam_data_base_tmp;
    uint32 max_int_lpm_tcam_data_base_tmp;

    DRV_CHIP_ID_VALID_CHECK(chip_id);

    if (!drv_greatbelt_table_is_tcam_key(tbl_id))
    {
        DRV_DBG_INFO("@@ ERROR! When read operation, INVALID Tcam key TblID! TblID: %d, file:%s line:%d function:%s\n",
                     tbl_id, __FILE__, __LINE__, __FUNCTION__);
        return DRV_E_INVALID_TBL;
    }

    if (TABLE_MAX_INDEX(tbl_id) <= index)
    {
        DRV_DBG_INFO("ERROR (drv_greatbelt_chip_tcam_tbl_read): chip-0x%x, tbl-%d, index-0x%x exceeds the max_index-0x%x.\n",
                     chip_id, tbl_id, index, TABLE_MAX_INDEX(tbl_id));
        return DRV_E_EXCEED_MAX_SIZE;
    }

    hw_database_addr = TABLE_DATA_BASE(tbl_id);

    max_int_tcam_data_base_tmp = DRV_INT_TCAM_KEY_DATA_ASIC_BASE
        + (DRV_INT_TCAM_KEY_MAX_ENTRY_NUM * DRV_BYTES_PER_ENTRY);
    max_int_lpm_tcam_data_base_tmp = DRV_INT_LPM_TCAM_DATA_ASIC_BASE
        + (DRV_INT_LPM_TCAM_MAX_ENTRY_NUM * DRV_BYTES_PER_ENTRY);

    key_size = TCAM_KEY_SIZE(tbl_id);
    /* the key's each index includes 80bit entry number */
    entry_num_each_idx = key_size / DRV_BYTES_PER_ENTRY;

    if ((hw_database_addr >= DRV_INT_TCAM_KEY_DATA_ASIC_BASE)
        && (hw_database_addr < max_int_tcam_data_base_tmp))
    {
        if (8 == entry_num_each_idx)
        {
            index_by_tcam = (hw_database_addr - DRV_INT_TCAM_KEY_DATA_ASIC_BASE) / DRV_BYTES_PER_ENTRY + (index * 4);
        }
        else
        {
            index_by_tcam = (hw_database_addr - DRV_INT_TCAM_KEY_DATA_ASIC_BASE) / DRV_BYTES_PER_ENTRY + (index * entry_num_each_idx);
        }

        is_lpm_tcam = FALSE;
    }
    else if ((hw_database_addr >= DRV_INT_LPM_TCAM_DATA_ASIC_BASE)
             && (hw_database_addr < max_int_lpm_tcam_data_base_tmp))
    {
        index_by_tcam = (hw_database_addr - DRV_INT_LPM_TCAM_DATA_ASIC_BASE) / DRV_BYTES_PER_ENTRY + index;
        is_lpm_tcam = TRUE;
    }
    else
    {
        DRV_DBG_INFO("Tcam key id 0x%x 's database is not correct, please check it!\n", tbl_id);
        return DRV_E_TCAM_KEY_DATA_ADDRESS;
    }

#if 0
    /* check whether the int tcam index is valid on emulation board */
    ret = _drv_check_emulation_tcam_address_valid(index_by_tcam * TCAM_MIN_UNIT_BYTES, (!in_external_tcam));
    if (FALSE == ret)
    {
        DRV_DBG_INFO("\nTcam key id 0x%x 's allocation is not correct, please check it!\n", tbl_id);
        return DRV_E_TCAM_KEY_DATA_ADDRESS;
    }

#endif

    /* according to keysize to clear tcam wbit(one bit each 80 bits tcam entry)*/
    for (entry_idx = 0; entry_idx < entry_num_each_idx; ++entry_idx)
    {
        /* remove tcam entry on emulation board */
        if (!is_lpm_tcam)
        {
            if (!IS_BIT_SET(entry_idx, 2))
            {
                 /*DRV_DBG_INFO("+++ Invalid Tcam Index = 0x%x \r\n", index_by_tcam);*/
                DRV_IF_ERROR_RETURN(_drv_greatbelt_chip_remove_int_tcam_entry(chip_id, index_by_tcam));
            }
            else
            {
                 /*DRV_DBG_INFO("+++ Invalid Tcam Index = 0x%x \r\n", (index_by_tcam - 4 + DRV_ACL_TCAM_640_OFFSET_IDX));*/
                DRV_IF_ERROR_RETURN(_drv_greatbelt_chip_remove_int_tcam_entry(chip_id, (index_by_tcam - 4 + DRV_ACL_TCAM_640_OFFSET_IDX)));
            }
        }
        else
        {
            DRV_IF_ERROR_RETURN(_drv_greatbelt_chip_remove_int_lpm_tcam_entry(chip_id, index_by_tcam));
        }

        index_by_tcam++;
    }

    return DRV_E_NONE;
}

#if 0
/* zhouw note */
STATIC int32
_drv_greatbelt_chip_hash_tblid_get_tblbase_info(uint32 hash_tableid,
                                      hash_tblbase_info_t* base_info)
{
    uint32 cmd;
    hash_ds_ctl_lookup_ctl_t hash_ctl;
    ipe_hash_lookup_result_ctl_t hash_lkp_rst_ctl;

    sal_memset(&hash_ctl, 0, sizeof(hash_ctl));
    cmd = DRV_IOR(HASH_DS_CTL_LOOKUP_CTL, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(drv_greatbelt_reg_ioctl(0, 0, cmd, &hash_ctl));

    sal_memset(&hash_lkp_rst_ctl, 0, sizeof(hash_lkp_rst_ctl));
    cmd = DRV_IOR(IPE_HASH_LOOKUP_RESULT_CTL, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(drv_greatbelt_reg_ioctl(0, 0, cmd, &hash_lkp_rst_ctl));

    switch (hash_tableid)
    {
    case DS_MAC_HASH_KEY0:
        /* hash key's bucket offset, each bucket have 4 hash entry(each entry is 72 bits)*/
        base_info->key_bucket_base = hash_ctl.mac_da_table_base;
        /* if set, tableBase is hash action table offset, else is tcam action tbl's offset */
        base_info->hash_action_tbl_pos = hash_lkp_rst_ctl.mac_da_lookup_table_base_pos;
        /* if pos=1, is hash action table's offset, and tcam action table is front */
        base_info->action_tbl_offset = hash_lkp_rst_ctl.mac_da_lookup_table_base;
        break;

    case DS_IPV4_UCAST_HASH_KEY0:
        base_info->key_bucket_base = hash_ctl.ipv4_ucast_table_base;
        base_info->hash_action_tbl_pos = hash_lkp_rst_ctl.ipv4_ucast_lookup_table_base_pos;
        base_info->action_tbl_offset = hash_lkp_rst_ctl.ipv4_ucast_lookup_table_base;
        break;

    case DS_IPV4_MCAST_HASH_KEY0:
        base_info->key_bucket_base = hash_ctl.ipv4_mcast_table_base;
        base_info->hash_action_tbl_pos = hash_lkp_rst_ctl.ipv4_mcast_lookup_table_base_pos;
        base_info->action_tbl_offset = hash_lkp_rst_ctl.ipv4_mcast_lookup_table_base;
        break;

    case DS_IPV6_UCAST_HASH_KEY0:
        base_info->key_bucket_base = hash_ctl.ipv6_ucast_table_base;
        base_info->hash_action_tbl_pos = hash_lkp_rst_ctl.ipv6_ucast_lookup_table_base_pos;
        base_info->action_tbl_offset = hash_lkp_rst_ctl.ipv6_ucast_lookup_table_base;
        break;

    case DS_IPV6_MCAST_HASH_KEY0:
        base_info->key_bucket_base = hash_ctl.ipv6_mcast_table_base;
        base_info->hash_action_tbl_pos = hash_lkp_rst_ctl.ipv6_mcast_lookup_table_base_pos;
        base_info->action_tbl_offset = hash_lkp_rst_ctl.ipv6_mcast_lookup_table_base;
        break;

    /* need to consider OAM hash key ??? */
    default:
        break;
    }

    return DRV_E_NONE;
}

/**
 @brief add hash entry after lkp operation on real chip
*/
int32
drv_greatbelt_chip_hash_key_add_entry(uint8 chip_id, void* add_para)
{
    uint32 cmd;
    uint32 trigger = 1;
    uint32 write_bit = 1;
    uint32 time_out = 0;
    int32 ret = DRV_E_NONE;

    /* check para */
    DRV_CHIP_ID_VALID_CHECK(chip_id);

    HASH_LOCK(chip_id);
    write_bit = 1;
    cmd = DRV_IOW(HASH_DS_CTL_CPU_KEY_REQ, HASH_DS_CTL_CPU_KEY_REQ_CPU_KEY_REQ_WR);
    ret = drv_reg_ioctl(chip_id, 0, cmd, &write_bit);
    if (ret < DRV_E_NONE)
    {
        HASH_UNLOCK(chip_id);
        return ret;
    }

    trigger = TRUE;
    cmd = DRV_IOW(HASH_DS_CTL_CPU_KEY_REQ, HASH_DS_CTL_CPU_KEY_REQ_CPU_KEY_REQ_VALID);
    ret = drv_reg_ioctl(chip_id, 0, cmd, &trigger);
    if (ret < DRV_E_NONE)
    {
        HASH_UNLOCK(chip_id);
        return ret;
    }

    /* waitting the trigger = 0 */
    while (trigger)
    {
        cmd = DRV_IOR(HASH_DS_CTL_CPU_KEY_REQ, HASH_DS_CTL_CPU_KEY_REQ_CPU_KEY_REQ_VALID);
        ret = drv_reg_ioctl(chip_id, 0, cmd, &trigger);
        if (ret < DRV_E_NONE)
        {
            HASH_UNLOCK(chip_id);
            return ret;
        }

        /* Check Time Out */
        if ((time_out++) > DRV_TIME_OUT)
        {
            HASH_UNLOCK(chip_id);
            return DRV_E_TIME_OUT;
        }
    }

    HASH_UNLOCK(chip_id);

    return ret;
}

/**
 @brief delete hash entry according to detailed key value on real chip
*/
int32
drv_greatbelt_chip_hash_key_del_entry_by_key(uint8 chip_id, void* del_para)
{
    /* TBD */
    return DRV_E_NONE;
}

/**
 @brief delete hash entry according to hash index on real chip
*/
int32
drv_greatbelt_chip_hash_key_del_by_index(uint8 chip_id, void* del_para)
{
    uint32 cmd;
    hash_ds_ctl_cpu_key_req_t hash_req;
    uint32 trigger = 1;
    uint32 time_out = 0;
    uint8 index = 0;
    int32 ret = DRV_E_NONE;
    uint32 tbl_id;
    uint32 tbl_idx;
    /* (tbl_id, tbl_idx) -> hash_idx (hash ram's index, unit:72 bits) */
    uint32 hash_idx = 0;

    /* Hash table size */
    uint32 table_size = 0;

    /* Use to store hashkey and the action table's position base info */
    hash_tblbase_info_t hashtbl_pos_info;

    hash_del_para_s* para = NULL;

    /* check para */
    DRV_CHIP_ID_VALID_CHECK(chip_id);
    DRV_PTR_VALID_CHECK(del_para);

    para = (hash_del_para_s*)del_para;
    tbl_id = para->table_id;
    tbl_idx = para->table_index;
    table_size = drv_gb_tbls_list[tbl_id].entry_size;

    /* according to hash tablid to get hashkey table and action table's position info */
    sal_memset(&hashtbl_pos_info, 0, sizeof(hashtbl_pos_info));
    DRV_IF_ERROR_RETURN(_drv_greatbelt_chip_hash_tblid_get_tblbase_info(tbl_id, &hashtbl_pos_info));

    /* according to the action table's index to get the hash key's index in the total hash ram */
    if (16 == table_size)     /* hash key size is 72 bits */
    {
        /* Each bucket is 288bits, hash sram's unit is 72bits, so shift 10 bits */
        hash_idx = (hashtbl_pos_info.key_bucket_base << 10) + tbl_idx;
    }
    else if (32 == table_size)  /* hash key size is 144 bits */
    {
        /* Each bucket is 288bits, hash sram's unit is 72bits, so shift 10 bits */
        hash_idx = (hashtbl_pos_info.key_bucket_base << 10) + tbl_idx * 2;
    }
    else
    {
        DRV_DBG_INFO("%%ERROR! When Do Hash I/O operation, find invalid hash table size!\n");
        DRV_DBG_INFO("%%Invalid Hash table size = %d Bytes\n", table_size);
        return DRV_E_INVALID_HASH_TABLE_SIZE;
    }

    HASH_LOCK(chip_id);

    for (index = 0; index < (table_size / 16); index++)
    {
        sal_memset(&hash_req, 0, sizeof(hash_ds_ctl_cpu_key_req_t));
        hash_req.cpu_ip31_to0 = hash_idx + index;
        hash_req.cpu_key_req_del = 1;

        cmd = DRV_IOW(HASH_DS_CTL_CPU_KEY_REQ, DRV_ENTRY_FLAG);
        ret = drv_reg_ioctl(chip_id, 0, cmd, &hash_req);
        if (ret < DRV_E_NONE)
        {
            HASH_UNLOCK(chip_id);
            return ret;
        }

        trigger = TRUE;
        cmd = DRV_IOW(HASH_DS_CTL_CPU_KEY_REQ, HASH_DS_CTL_CPU_KEY_REQ_CPU_KEY_REQ_VALID);
        ret = drv_reg_ioctl(chip_id, 0, cmd, &trigger);
        if (ret < DRV_E_NONE)
        {
            HASH_UNLOCK(chip_id);
            return ret;
        }

        /* waitting the trigger = 0 */
        while (trigger)
        {
            cmd = DRV_IOR(HASH_DS_CTL_CPU_KEY_REQ, HASH_DS_CTL_CPU_KEY_REQ_CPU_KEY_REQ_VALID);
            ret = drv_reg_ioctl(chip_id, 0, cmd, &trigger);
            if (ret < DRV_E_NONE)
            {
                HASH_UNLOCK(chip_id);
                return ret;
            }

            /* Check Time Out */
            if ((time_out++) > DRV_TIME_OUT)
            {
                HASH_UNLOCK(chip_id);
                return DRV_E_TIME_OUT;
            }
        }
    }

    HASH_UNLOCK(chip_id);

    return ret;
}

/**
 @brief Hash lookup I/O control API on real chip
*/
int32
drv_greatbelt_chip_hash_lookup(uint8 chip_id,
                     uint32* key,
                     hash_ds_ctl_cpu_key_status_t* hash_cpu_status,
                     cpu_req_hash_key_type_e cpu_hashkey_type,
                     ip_hash_rst_info_t* ip_hash_rst_info)
{
    ds_mac_hash_key0_t* mac_key = NULL;
    ds_ipv6_ucast_hash_key0_t* v6uc_key = NULL;
    ds_ipv6_mcast_hash_key0_t* v6mc_key = NULL;
    uint32 cmd;
    hash_ds_ctl_cpu_key_req_t hash_lkp_cpu_req;
    uint32 time_out = 0;
    uint32 trigger = TRUE;
    int32 ret = DRV_E_NONE;

    if (cpu_hashkey_type >= CPU_HASH_KEY_TYPE_RESERVED0)
    {
        return DRV_E_INVALID_PARAMETER;
    }

    DRV_PTR_VALID_CHECK(key);
    DRV_PTR_VALID_CHECK(hash_cpu_status);
    DRV_CHIP_ID_VALID_CHECK(chip_id);
    hash_cpu_status->cpu_key_hit = FALSE;
    hash_cpu_status->cpu_lu_index = DRV_HASH_INVALID_INDEX;

    HASH_LOCK(chip_id);
    sal_memset(&hash_lkp_cpu_req, 0, sizeof(hash_ds_ctl_cpu_key_req_t));
    hash_lkp_cpu_req.cpu_key_type = cpu_hashkey_type;

    switch (cpu_hashkey_type)
    {
    case CPU_HASH_KEY_TYPE_MAC_DA:
    case CPU_HASH_KEY_TYPE_MAC_SA:
        /*  table_id = DS_MAC_HASH_KEY0;          mac hash key */
        if (0 == DRV_TBL_MAX_INDEX(DS_MAC_HASH_KEY0))
        {
            return DRV_E_NONE;
        }

        mac_key = (ds_mac_hash_key0_t*)key;
        hash_lkp_cpu_req.cpu_ip31_to0 = mac_key->mapped_macl;
        hash_lkp_cpu_req.cpu_ip63_to32 = mac_key->mapped_mach;
        hash_lkp_cpu_req.cpu_ip95_to64 = 0;
        hash_lkp_cpu_req.cpu_ip127_to96 = 0;
        hash_lkp_cpu_req.cpu_vrf_id = mac_key->mapped_vlanid;
        break;

#if 0
    case CPU_HASH_KEY_TYPE_IPV4_UC:
        /*  table_id = DS_IPV4_UCAST_HASH_KEY0;  ipv4uc hash key */
        if (0 == DRV_TBL_MAX_INDEX(DS_IPV4_UCAST_HASH_KEY0))
        {
            return DRV_E_NONE;
        }

        v4uc_key = (ds_ipv4_ucast_hash_key0_t*)key;
        hash_lkp_cpu_req.cpu_ip31_to0 = v4uc_key->key_mapped_ip;
        hash_lkp_cpu_req.cpu_ip63_to32 = 0;
        hash_lkp_cpu_req.cpu_ip95_to64 = 0;
        hash_lkp_cpu_req.cpu_ip127_to96 = 0;
        hash_lkp_cpu_req.cpu_vrf_id = v4uc_key->key_vrfid;
        break;

    case CPU_HASH_KEY_TYPE_IPV4_MC:
        /*   table_id = DS_IPV4_MCAST_HASH_KEY0; ipv4mc hash key */
        if (0 == DRV_TBL_MAX_INDEX(CPU_HASH_KEY_TYPE_IPV4_MC))
        {
            return DRV_E_NONE;
        }

        v4mc_key = (ds_ipv4_mcast_hash_key0_t*)key;
        hash_lkp_cpu_req.cpu_ip31_to0 = v4mc_key->key_mapped_ip;
        hash_lkp_cpu_req.cpu_ip63_to32 = 0;
        hash_lkp_cpu_req.cpu_ip95_to64 = 0;
        hash_lkp_cpu_req.cpu_ip127_to96 = 0;
        hash_lkp_cpu_req.cpu_vrf_id = v4mc_key->key_vrfid;
        break;

#endif
    case CPU_HASH_KEY_TYPE_IPV4_UC_HASH8:
        break;

    case CPU_HASH_KEY_TYPE_IPV4_UC_HASH16:
        break;

    case CPU_HASH_KEY_TYPE_IPV6_UC:
        /*  table_id = DS_IPV6_UCAST_HASH_KEY0;  ipv6uc hash key */
        if (0 == DRV_TBL_MAX_INDEX(CPU_HASH_KEY_TYPE_IPV6_UC))
        {
            return DRV_E_NONE;
        }

        v6uc_key = (ds_ipv6_ucast_hash_key0_t*)key;
        hash_lkp_cpu_req.cpu_ip31_to0 = v6uc_key->key_ipda0;
        hash_lkp_cpu_req.cpu_ip63_to32 = v6uc_key->key_ipda1;
        hash_lkp_cpu_req.cpu_ip95_to64 = v6uc_key->key_ipda2;
        hash_lkp_cpu_req.cpu_ip127_to96 = v6uc_key->key_ipda3;
        hash_lkp_cpu_req.cpu_vrf_id = (v6uc_key->vrfid3 << 9)
            | (v6uc_key->vrfid2 << 6)
            | (v6uc_key->vrfid1 << 3)
            | v6uc_key->vrfid0;
        break;

    case CPU_HASH_KEY_TYPE_IPV6_MC:
        /* table_id = DS_IPV6_MCAST_HASH_KEY0;  ipv6mc hash key */
        if (0 == DRV_TBL_MAX_INDEX(CPU_HASH_KEY_TYPE_IPV6_MC))
        {
            return DRV_E_NONE;
        }

        v6mc_key = (ds_ipv6_mcast_hash_key0_t*)key;
        hash_lkp_cpu_req.cpu_ip31_to0 = v6mc_key->key_ipda0;
        hash_lkp_cpu_req.cpu_ip63_to32 = v6mc_key->key_ipda1;
        hash_lkp_cpu_req.cpu_ip95_to64 = v6mc_key->key_ipda2;
        hash_lkp_cpu_req.cpu_ip127_to96 = v6mc_key->key_ipda3;
        hash_lkp_cpu_req.cpu_vrf_id = (v6mc_key->vrfid3 << 9)
            | (v6mc_key->vrfid2 << 6)
            | (v6mc_key->vrfid1 << 3)
            | v6mc_key->vrfid0;
        break;

    default:
        break;
    }

    hash_lkp_cpu_req.cpu_key_req_lu = TRUE;  /* Do hash lookup operation */

    /* set key and key type */
    cmd = DRV_IOW(HASH_DS_CTL_CPU_KEY_REQ, DRV_ENTRY_FLAG);
    ret = drv_reg_ioctl(chip_id, 0, cmd, &hash_lkp_cpu_req);
    if (ret < DRV_E_NONE)
    {
        HASH_UNLOCK(chip_id);
        return ret;
    }

    /* set the trigger field */
    cmd = DRV_IOW(HASH_DS_CTL_CPU_KEY_REQ, HASH_DS_CTL_CPU_KEY_REQ_CPU_KEY_REQ_VALID);
    ret = drv_reg_ioctl(chip_id, 0, cmd, &trigger);
    if (ret < DRV_E_NONE)
    {
        HASH_UNLOCK(chip_id);
        return ret;
    }

    while (trigger) /* wait the trigger = 0 */
    {
        cmd = DRV_IOR(HASH_DS_CTL_CPU_KEY_REQ, HASH_DS_CTL_CPU_KEY_REQ_CPU_KEY_REQ_VALID);
        ret = drv_reg_ioctl(chip_id, 0, cmd, &trigger);
        if (ret < DRV_E_NONE)
        {
            HASH_UNLOCK(chip_id);
            return ret;
        }

        if ((time_out++) > DRV_TIME_OUT)   /* Check Time Out */
        {
            HASH_UNLOCK(chip_id);
            return DRV_E_TIME_OUT;
        }
    }

    /* get hash lkp result */
    /* Note:
       if cpu_key_hit = 1, cpu_lu_index is the hitted hash key absolute index,
       if cpu_key_hit = 0 & cpu_lu_index = 0x1FFFF, conflict, no hit and hash two bucket is full,
       if cpu_key_hit = 0 & cpu_lu_index != 0x1FFFF, no hit and return a idle index,
    */
    cmd = DRV_IOR(HASH_DS_CTL_CPU_KEY_STATUS, DRV_ENTRY_FLAG);
    ret = (drv_greatbelt_reg_ioctl(chip_id, 0, cmd, hash_cpu_status));
    if (ret < DRV_E_NONE)
    {
        HASH_UNLOCK(chip_id);
        return ret;
    }

    HASH_UNLOCK(chip_id);

    return DRV_E_NONE;
}

#endif


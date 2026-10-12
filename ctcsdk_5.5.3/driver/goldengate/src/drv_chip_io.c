/**
 @file drv_chip_io.c

 @date 2011-10-09

 @version v4.28.2

 The file contains all driver I/O interface realization
*/
#include "goldengate/include/drv_lib.h"
#include "goldengate/include/drv_chip_ctrl.h"

extern tables_info_t drv_gg_tbls_list[MaxTblId_t];
extern dup_address_offset_type_t gg_duplicate_addr_type;

#define DRV_TIME_OUT  1000    /* Time out setting */
#define DRV_FLOW_TCAM_WR_BYTES 20  /* tcam w/r per 160 bit */
#define DRV_FLOW_TCAM_WR_ENTRY_BYTES 10  /* tcam w/r per 160 bit */

uint8  g_gg_burst_en = 1;
uint8  g_gg_tcam_dma = 0;
uint8  g_gg_lpm_dma = 0;

/* Cpu access Tcam control operation type */
enum flow_tcam_operation_type_t
{
    CPU_ACCESS_REQ_READ_X = 0,
    CPU_ACCESS_REQ_READ_Y = 1,
    CPU_ACCESS_REQ_WRITE_DATA_MASK = 2,
    CPU_ACCESS_REQ_INVALID_ENTRY = 3,
};
typedef enum flow_tcam_operation_type_t flow_tcam_op_tp_e;

enum fib_tcam_operation_type_t
{
    FIB_CPU_ACCESS_REQ_READ_X = 0,
    FIB_CPU_ACCESS_REQ_READ_Y = 1,
    FIB_CPU_ACCESS_REQ_WRITE_DATA_MASK = 2,
    FIB_CPU_ACCESS_REQ_INVALID_ENTRY = 3,
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
    DRV_INT_LPM_TCAM_RECORD_REG = 5,
};
typedef enum tcam_mem_type tcam_mem_type_e;

/* Tcam/Hash/Tbl/Reg I/O operation mutex definition */
static sal_mutex_t* p_gg_flow_tcam_mutex[MAX_LOCAL_CHIP_NUM];
static sal_mutex_t* p_gg_lpm_ip_tcam_mutex[MAX_LOCAL_CHIP_NUM];
static sal_mutex_t* p_gg_lpm_nat_tcam_mutex[MAX_LOCAL_CHIP_NUM];
static sal_mutex_t* p_gg_entry_mutex[MAX_LOCAL_CHIP_NUM];

sal_mutex_t* p_gg_mep_mutex[MAX_LOCAL_CHIP_NUM];

extern drv_access_type_t  g_gg_access_type;

/* Tcam/Hash/Tbl/Reg I/O operation mutex control */
#define FLOW_TCAM_LOCK(chip_id_offset)         sal_mutex_lock(p_gg_flow_tcam_mutex[chip_id_offset])
#define FLOW_TCAM_UNLOCK(chip_id_offset)       sal_mutex_unlock(p_gg_flow_tcam_mutex[chip_id_offset])
#define LPM_IP_TCAM_LOCK(chip_id_offset)       sal_mutex_lock(p_gg_lpm_ip_tcam_mutex[chip_id_offset])
#define LPM_IP_TCAM_UNLOCK(chip_id_offset)     sal_mutex_unlock(p_gg_lpm_ip_tcam_mutex[chip_id_offset])
#define LPM_NAT_TCAM_LOCK(chip_id_offset)      sal_mutex_lock(p_gg_lpm_nat_tcam_mutex[chip_id_offset])
#define LPM_NAT_TCAM_UNLOCK(chip_id_offset)    sal_mutex_unlock(p_gg_lpm_nat_tcam_mutex[chip_id_offset])
#define DRV_ENTRY_LOCK(chip_id_offset)         sal_mutex_lock(p_gg_entry_mutex[chip_id_offset])
#define DRV_ENTRY_UNLOCK(chip_id_offset)       sal_mutex_unlock(p_gg_entry_mutex[chip_id_offset])

DMA_TCAM_CB_FUN_P gg_tcam_dma_cb;

/* Each hash key and the corresponding action tb's
   position infomation in hash ram and sram */
struct hash_tblbase_info_s
{
    uint32 key_bucket_base;
    uint32 hash_action_tbl_pos;
    uint32 action_tbl_offset;
};
typedef struct hash_tblbase_info_s hash_tblbase_info_t;


/**
 @brief Dma register callback function
*/
int32
drv_goldengate_chip_register_tcam_write_cb(DMA_TCAM_CB_FUN_P cb)
{
     gg_tcam_dma_cb = cb;

    return 0;
}

/**
 @brief Set flow tcam uing dma or io
  type: 0-lpm tcam, 1-flow tcam
 mode: 0-IO, 1-DMA
*/
int32
drv_goldengate_chip_set_write_tcam_mode(uint8 type, uint8 mode)
{
    if (type)
    {
        g_gg_tcam_dma = (mode)?1:0;
    }
    else
    {
        g_gg_lpm_dma = (mode)?1:0;
    }

    return 0;
}


/**
 @brief get flow tcam blknum and local index
*/
int32
drv_goldengate_chip_flow_tcam_get_blknum_index(uint8 chip_id, tbls_id_t tbl_id, uint32 index,
                                            uint32 *blknum, uint32 *local_idx, uint32 *is_sec_half)
{
    uint8 addr_offset = 0;
    uint32 blk_id = 0;
    uint32 map_index = 0, mid_index = 0;
    uint32 couple_mode = 0;
    uint32 offset = 0;
   uint32 entry_num_each_idx =0 ;

    DRV_IF_ERROR_RETURN(drv_goldengate_get_dynamic_ram_couple_mode(&couple_mode));
    *is_sec_half = 0;

    DRV_IF_ERROR_RETURN(drv_goldengate_get_tbl_index_base(tbl_id, index, &addr_offset));
    entry_num_each_idx =  TABLE_ENTRY_SIZE(tbl_id) / (DRV_BYTES_PER_ENTRY);  /*80 bit*/

    if (drv_goldengate_table_is_tcam_key(tbl_id))
    {
        for (blk_id = 0; blk_id < MAX_NOR_TCAM_NUM; blk_id++)
        {
            if (!IS_BIT_SET(TCAM_BITMAP(tbl_id), blk_id))
            {
                continue;
            }

            if ((index >= TCAM_START_INDEX(tbl_id, blk_id)) && (index <= TCAM_END_INDEX(tbl_id, blk_id)))
            {
                if(couple_mode)
                {
                    mid_index = (drv_goldengate_get_tcam_entry_num(blk_id, 0) + 1)/2; /*80 bit*/
                    offset = ((TCAM_DATA_BASE(tbl_id, blk_id, addr_offset) - drv_goldengate_get_tcam_addr_offset(blk_id, FALSE))) / DRV_ADDR_BYTES_PER_ENTRY; /*80*/
                    map_index = offset + (index - TCAM_START_INDEX(tbl_id, blk_id)) * entry_num_each_idx;

                    if (map_index >= mid_index)
                    {
                        *is_sec_half = 1;
                        map_index = (map_index - mid_index);
                    }
                }
                else
                {
                    /*SDK MODIFY*/
                    offset = (TCAM_DATA_BASE(tbl_id, blk_id, addr_offset)  - drv_goldengate_get_tcam_addr_offset(blk_id, FALSE)) / DRV_ADDR_BYTES_PER_ENTRY;
                    map_index = offset +  (index - TCAM_START_INDEX(tbl_id, blk_id))*entry_num_each_idx;
                }

                *blknum = blk_id;
                *local_idx = (map_index/2); /*160 bit*/

                break;
            }
        }
    }
    else if(drv_goldengate_table_is_tcam_ad(tbl_id))
    {
        for (blk_id = 0; blk_id < MAX_NOR_TCAM_NUM; blk_id++)
        {
            if (!IS_BIT_SET(TCAM_BITMAP(tbl_id), blk_id))
            {
                continue;
            }

            if ((index >= TCAM_START_INDEX(tbl_id, blk_id)) && (index <= TCAM_END_INDEX(tbl_id, blk_id)))
            {
                offset = ((TCAM_DATA_BASE(tbl_id, blk_id, addr_offset) - drv_goldengate_get_tcam_ad_addr_offset(blk_id))) / DRV_ADDR_BYTES_PER_ENTRY;
                map_index = (offset / entry_num_each_idx) + (index - TCAM_START_INDEX(tbl_id, blk_id));

                *blknum = blk_id;
                *local_idx = map_index;

                break;
            }
        }
    }
    else
    {
        DRV_DBG_INFO("\nInvalid table id %d when get flow tcam block number and index!\n", tbl_id);
        return DRV_E_INVALID_TBL;
    }

    return DRV_E_NONE;
}

/**
 @brief Real sram direct write operation I/O
*/
int32
drv_goldengate_chip_write_sram_entry(uint8 chip_id, uintptr addr,
                                uint32* data, int32 len)
{
    int32 i = 0;
    int32 tmp = 0;
    uint32 length = 0;
    uint8 burst_op = 0;
    uint8 io_times = 0;
    uint8 word_num = 0;

    DRV_PTR_VALID_CHECK(data);

    /* the length must be 4*n(n=1,2,3...) */
    if (tmp & 0x3)
    {
        return DRV_E_ILLEGAL_LENGTH;
    }

    /*for burst io*/
    burst_op = addr&0x3;
    if (burst_op >= 3)
    {
        return DRV_E_INVAILD_TYPE;
    }

    tmp = (len >> 2);
    word_num = (burst_op)?16:1;
    io_times = tmp/word_num + ((tmp%word_num)?1:0);

    DRV_ENTRY_LOCK(chip_id);

    /*for burst write,no mask, max length is 16dword at one times*/
    for (i = 0; i < io_times; i++)
    {
        if (burst_op)
        {
            length = (tmp>=16)?16:tmp;
        }
        else
        {
            length = 1;
        }

        DRV_IF_ERROR_RETURN_WITH_UNLOCK(
        drv_goldengate_chip_write_ext(chip_id, addr, data+i*word_num, length),
        p_gg_entry_mutex[chip_id]);

        addr += 4*length;
        if ((tmp > 16) && burst_op)
        {
            tmp = tmp - 16;
        }
    }

    DRV_ENTRY_UNLOCK(chip_id);

    return DRV_E_NONE;
}

/**
 @brief Real sram direct read operation I/O
*/
int32
drv_goldengate_chip_read_sram_entry(uint8 chip_id, uintptr addr, uint32* data, int32 len)
{
    int32 i = 0;
    int32 tmp = 0;
    int32 length = 0;
    uint8 burst_op = 0;
    uint8 io_times = 0;
    uint8 word_num = 0;

    tmp = len;

    DRV_PTR_VALID_CHECK(data);

    /* the length must be 4*n(n=1,2,3...) */
    if (tmp & 0x3)
    {
        return DRV_E_ILLEGAL_LENGTH;
    }

    /*for burst io*/
    burst_op = addr&0x3;
    if (burst_op >= 3)
    {
        return DRV_E_INVAILD_TYPE;
    }

    tmp = (len >> 2);
    word_num = (burst_op)?16:1;
    io_times = tmp/word_num + ((tmp%word_num)?1:0);

    DRV_ENTRY_LOCK(chip_id);

    /*for burst write,no mask, max length is 16dword at one times*/
    for (i = 0; i < io_times; i++)
    {
        if (burst_op)
        {
            length = (tmp>=16)?16:tmp;
        }
        else
        {
            length = 1;
        }

        DRV_IF_ERROR_RETURN_WITH_UNLOCK(
        drv_goldengate_chip_read_ext(chip_id, addr, data+i*word_num, length),
        p_gg_entry_mutex[chip_id]);

        addr += 4*length;
        if ((tmp > 16) && burst_op)
        {
            tmp = tmp - 16;
        }
    }

    DRV_ENTRY_UNLOCK(chip_id);
    return DRV_E_NONE;
}
#if 1
/**
 @brief Real embeded tcam ad read operation I/O
*/
STATIC int32
_drv_goldengate_chip_read_flow_tcam_ad_entry(uint8 chip_id, uint32 blknum, uint32 index, uint32* data)
{
    uint32 flow_tcam_cpu_req_ctl[FLOW_TCAM_CPU_REQ_CTL_BYTES/4] = {0};
     /*uint32 flow_tcam_ad_mem[FLOW_TCAM_AD_MEM_BYTES/4] = {0};*/
    uint32 cmd;
    int32 ret = DRV_E_NONE;
    uint32 chipid_offset = chip_id + drv_gg_init_chip_info.drv_init_chipid_base;

    DRV_PTR_VALID_CHECK(data);

    FLOW_TCAM_LOCK(chipid_offset);

    DRV_IF_ERROR_RETURN_WITH_UNLOCK(
    drv_goldengate_set_field(FlowTcamCpuReqCtl_t, FlowTcamCpuReqCtl_adCpublkNum_f
                          , flow_tcam_cpu_req_ctl, &blknum),
    p_gg_flow_tcam_mutex[chipid_offset]);

    cmd = DRV_IOW(FlowTcamCpuReqCtl_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN_WITH_UNLOCK(
    drv_goldengate_ioctl(chipid_offset, 0, cmd, flow_tcam_cpu_req_ctl),
    p_gg_flow_tcam_mutex[chipid_offset]);

    cmd = DRV_IOR(FlowTcamAdMem_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN_WITH_UNLOCK(
    drv_goldengate_ioctl(chipid_offset, index, cmd, data),
    p_gg_flow_tcam_mutex[chipid_offset]);

    FLOW_TCAM_UNLOCK(chipid_offset);

    return ret;
}

/**
 @brief Real embeded tcam ad write operation I/O
*/
int32
drv_goldengate_chip_write_flow_tcam_ad_entry(uint8 chip_id, uint32 blknum, uint32 index, uint32* data)
{
    uint32 flow_tcam_cpu_req_ctl[FLOW_TCAM_CPU_REQ_CTL_BYTES/4] = {0};
    uint32 flow_tcam_ad_mem[FLOW_TCAM_AD_MEM_BYTES/4] = {0};
    uint32 cmd;
    int32 ret = DRV_E_NONE;
    uint32 chipid_offset = chip_id + drv_gg_init_chip_info.drv_init_chipid_base;

    DRV_PTR_VALID_CHECK(data);

    FLOW_TCAM_LOCK(chipid_offset);

    DRV_IF_ERROR_RETURN_WITH_UNLOCK(
    drv_goldengate_set_field(FlowTcamCpuReqCtl_t, FlowTcamCpuReqCtl_adCpublkNum_f
                          , flow_tcam_cpu_req_ctl, &blknum),
    p_gg_flow_tcam_mutex[chipid_offset]);

    cmd = DRV_IOW(FlowTcamCpuReqCtl_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN_WITH_UNLOCK(
    drv_goldengate_ioctl(chipid_offset, 0, cmd, flow_tcam_cpu_req_ctl),
    p_gg_flow_tcam_mutex[chipid_offset]);

    sal_memcpy((uint8*)flow_tcam_ad_mem,(uint8*)data, DRV_BYTES_PER_ENTRY*2);

    cmd = DRV_IOW(FlowTcamAdMem_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN_WITH_UNLOCK(
    drv_goldengate_ioctl(chipid_offset, index, cmd, flow_tcam_ad_mem),
    p_gg_flow_tcam_mutex[chipid_offset]);

    FLOW_TCAM_UNLOCK(chipid_offset);

    return ret;
}

/**
 @brief Real embeded tcam read operation I/O
*/
STATIC int32
_drv_goldengate_chip_read_flow_tcam_entry(uint8 chip_id, uint32 blknum, uint32 index,
                             uint32* data, tcam_mem_type_e type)
{
    uint32 flow_tcam_cpu_req_ctl[FLOW_TCAM_CPU_REQ_CTL_BYTES/4] = {0};
    uint32 cmd;
    int32 ret = DRV_E_NONE;
    uint32 cpu_req = 0, cpu_req_type = 0, cpu_index = 0, block_num = 0;
    uint32 cpu_read_data_valid = 0, time_out = 0;

    DRV_PTR_VALID_CHECK(data);

/*
    if (index >= DRV_INT_TCAM_KEY_MAX_ENTRY_NUM)
    {
        DRV_DBG_INFO("\nEmbeded Tcam's index range is 0~(8k-1), read intTcam entry fail!\n");
        return DRV_E_INVALID_TCAM_INDEX;
    }
*/
    if (DRV_INT_TCAM_RECORD_MASK == type)
    {
        cpu_req_type = CPU_ACCESS_REQ_READ_Y;
    }
    else if (DRV_INT_TCAM_RECORD_DATA == type)
    {
        cpu_req_type = CPU_ACCESS_REQ_READ_X;
    }
    else
    {
        DRV_DBG_INFO("\nEmbeded Tcam Memory Type is useless!\n");
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
    ret = drv_goldengate_ioctl(chip_id, 0, cmd, flow_tcam_cpu_req_ctl);
    if (ret < DRV_E_NONE)
    {
        return ret;
    }

    while (TRUE)
    {
        cmd = DRV_IOR(FlowTcamCpuReqCtl_t, FlowTcamCpuReqCtl_tcamCpuReadDataValid_f);
        ret = drv_goldengate_ioctl(chip_id, 0, cmd, &cpu_read_data_valid);
        if (ret < DRV_E_NONE)
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
    ret = drv_goldengate_ioctl(chip_id, 0, cmd, data);
    if (ret < DRV_E_NONE)
    {
        return ret;
    }

    cpu_read_data_valid = FALSE;

    cmd = DRV_IOW(FlowTcamCpuReqCtl_t, FlowTcamCpuReqCtl_tcamCpuReadDataValid_f);
    ret = drv_goldengate_ioctl(chip_id, 0, cmd, &cpu_read_data_valid);
    if (ret < DRV_E_NONE)
    {
        return ret;
    }

    return DRV_E_NONE;
}


/**
@brief Real embeded tcam write operation I / O (data&mask write)
*/
int32
_drv_goldengate_chip_tcam_drain_en(uint8 chip_id, bool enable)
{
    uint32 cmd = 0;
    uint32 field_val = 0;

    field_val = enable?1:0;

    cmd = DRV_IOW(IpeIntfMapDrainEnable0_t, IpeIntfMapDrainEnable0_drainEnable_f);
    drv_goldengate_ioctl(chip_id, 0, cmd, &field_val);
    cmd = DRV_IOW(IpeIntfMapDrainEnable1_t, IpeIntfMapDrainEnable1_drainEnable_f);
    drv_goldengate_ioctl(chip_id, 0, cmd, &field_val);

    cmd = DRV_IOW(IpeLkupMgrDrainEnable0_t, IpeLkupMgrDrainEnable0_aclLkupFifoDrainEn_f);
    drv_goldengate_ioctl(chip_id, 0, cmd, &field_val);
    cmd = DRV_IOW(IpeLkupMgrDrainEnable1_t, IpeLkupMgrDrainEnable1_aclLkupFifoDrainEn_f);
    drv_goldengate_ioctl(chip_id, 0, cmd, &field_val);

    cmd = DRV_IOW(EpeAclOamDrainEnable0_t, EpeAclOamDrainEnable0_epeAclQosDrainEnable_f);
    drv_goldengate_ioctl(chip_id, 0, cmd, &field_val);
    cmd = DRV_IOW(EpeAclOamDrainEnable1_t, EpeAclOamDrainEnable1_epeAclQosDrainEnable_f);
    drv_goldengate_ioctl(chip_id, 0, cmd, &field_val);

    return DRV_E_NONE;
}

/**
 @brief Real embeded tcam write operation I/O (data&mask write)
*/
STATIC int32
_drv_goldengate_chip_write_flow_tcam_data_mask(uint8 chip_id, uint32 blknum, uint32 index,
                                    uint32* data, uint32* mask)
{
    uint32 cmd;
    int32 ret = DRV_E_NONE;
    uint32 flow_tcam_cpu_req_ctl[FLOW_TCAM_CPU_REQ_CTL_BYTES/4] = {0};
    uint32 flow_tcam_cpu_wr_data[FLOW_TCAM_CPU_WR_DATA_BYTES/4] = {0};
    uint32 cpu_req = 0, cpu_req_type = 0, cpu_index = 0, block_num = 0;
    uint32  time_out = 0;
    tbl_entry_t tbl_entry;

    DRV_PTR_VALID_CHECK(data);
    DRV_PTR_VALID_CHECK(mask);

    /* Attention that we MUST write the mask before writing corresponding data!! */
    /* because the real store in TCAM is X/Y , and the TCAM access write data and mask at the same time, */
    /* so we should write mask at first, then tcam data. */
    /* X = ~data & mask ; Y = data & mask */

    sal_memcpy((uint8*)flow_tcam_cpu_wr_data,(uint8*)data, DRV_BYTES_PER_ENTRY*2);

    sal_memcpy((uint8*)flow_tcam_cpu_wr_data + DRV_BYTES_PER_ENTRY * 2, (uint8*)mask, DRV_BYTES_PER_ENTRY*2);

    cmd = DRV_IOW(FlowTcamCpuWrData_t, DRV_ENTRY_FLAG);
    ret = drv_goldengate_ioctl(chip_id, 0, cmd, flow_tcam_cpu_wr_data);
    if (ret < DRV_E_NONE)
    {
        return ret;
    }

    cpu_index = index;
    cpu_req_type = CPU_ACCESS_REQ_WRITE_DATA_MASK;
    cpu_req = FALSE;
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
    ret = drv_goldengate_ioctl(chip_id, 0, cmd, flow_tcam_cpu_req_ctl);
    if (ret < DRV_E_NONE)
    {
        return ret;
    }

    cpu_req = TRUE;
    DRV_IOW_FIELD(FlowTcamCpuReqCtl_t, FlowTcamCpuReqCtl_tcamCpuReq_f,
                  &cpu_req, flow_tcam_cpu_req_ctl);

    if (g_gg_tcam_dma)
    {
        if (gg_tcam_dma_cb)
        {
            ret =  gg_tcam_dma_cb(chip_id, flow_tcam_cpu_req_ctl[0]);
            if (ret < DRV_E_NONE)
            {
                return ret;
            }
        }
    }
    else
    {
        cmd = DRV_IOW(FlowTcamCpuReqCtl_t, DRV_ENTRY_FLAG);
        ret = drv_goldengate_ioctl(chip_id, 0, cmd, flow_tcam_cpu_req_ctl);
        if (ret < DRV_E_NONE)
        {
            return ret;
        }
    }

    while (TRUE)
    {
        cpu_req = TRUE;
        cmd = DRV_IOR(FlowTcamCpuReqCtl_t, FlowTcamCpuReqCtl_tcamCpuReq_f);
        ret = drv_goldengate_ioctl(chip_id, 0, cmd, &cpu_req);
        if (ret < DRV_E_NONE)
        {
            return ret;
        }

        if (!cpu_req)
        {
            break;
        }

        if ((time_out ++) > DRV_TIME_OUT)
        {
            return DRV_E_TIME_OUT;
        }
    }

    tbl_entry.data_entry = data;
    tbl_entry.mask_entry = mask;
    drv_goldengate_ecc_recover_store(chip_id, DRV_ECC_MEM_TCAM_KEY0 + blknum, index, &tbl_entry);

    return DRV_E_NONE;
}

/**
 @brief Real embeded tcam remove operation I/O
*/
STATIC int32
_drv_goldengate_chip_remove_flow_tcam_entry(uint8 chip_id, uint32 blknum, uint32 tcam_index)
{
    uint32 flow_tcam_cpu_req_ctl[FLOW_TCAM_CPU_REQ_CTL_BYTES/4] = {0};
    int32 ret = DRV_E_NONE;
    uint32 cmd = 0, mask = 0;
    uint32 time_out = 0;
    tbl_entry_t tbl_entry;
    uint32 cpu_req = 0, cpu_req_type = 0, cpu_index = 0, block_num = 0;

    cpu_index = tcam_index;
    cpu_req_type = CPU_ACCESS_REQ_INVALID_ENTRY;
    cpu_req = FALSE;
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
    ret = drv_goldengate_ioctl(chip_id, 0, cmd, flow_tcam_cpu_req_ctl);
    if (ret < DRV_E_NONE)
    {
        return ret;
    }

    cpu_req = TRUE;
    DRV_IOW_FIELD(FlowTcamCpuReqCtl_t, FlowTcamCpuReqCtl_tcamCpuReq_f,
                  &cpu_req, flow_tcam_cpu_req_ctl);

    if (g_gg_tcam_dma)
    {
        if (gg_tcam_dma_cb)
        {
            ret =  gg_tcam_dma_cb(chip_id, flow_tcam_cpu_req_ctl[0]);
            if (ret < DRV_E_NONE)
            {
                return ret;
            }
        }
    }
    else
    {
        cmd = DRV_IOW(FlowTcamCpuReqCtl_t, DRV_ENTRY_FLAG);
        ret = drv_goldengate_ioctl(chip_id, 0, cmd, flow_tcam_cpu_req_ctl);
        if (ret < DRV_E_NONE)
        {
            return ret;
        }
    }

    while (TRUE)
    {
        cpu_req = TRUE;
        cmd = DRV_IOR(FlowTcamCpuReqCtl_t, FlowTcamCpuReqCtl_tcamCpuReq_f);
        ret = drv_goldengate_ioctl(chip_id, 0, cmd, &cpu_req);
        if (ret < DRV_E_NONE)
        {
            return ret;
        }

        if (!cpu_req)
        {
            break;
        }

        if ((time_out ++) > DRV_TIME_OUT)
        {
            return DRV_E_TIME_OUT;
        }
    }

    tbl_entry.data_entry = NULL;
    tbl_entry.mask_entry = &mask;
    drv_goldengate_ecc_recover_store(chip_id, DRV_ECC_MEM_TCAM_KEY0 + blknum,
                          tcam_index, &tbl_entry);

    return DRV_E_NONE;
}

/**
 @brief Real embeded LPM tcam read operation I/O
*/
STATIC int32
_drv_goldengate_chip_read_lpm_tcam_ip_entry(uint8 chip_id, uint32 index,
                             uint32* data, tcam_mem_type_e type)
{
    uint32 lpm_tcam_ip_cpu_req_ctl[LPM_TCAM_IP_CPU_REQ_CTL_BYTES/4] = {0};
    uint32 cmd;
    int32 ret = DRV_E_NONE;
    uint32 cpu_req = 0, cpu_req_type = 0, cpu_index = 0;
    uint32 cpu_read_data_valid = 0, time_out = 0;

    DRV_PTR_VALID_CHECK(data);

    cpu_index = index;
    cpu_req = TRUE;
    if (DRV_INT_LPM_TCAM_RECORD_MASK == type)
    {
        cpu_req_type = FIB_CPU_ACCESS_REQ_READ_Y;
    }
    else if (DRV_INT_LPM_TCAM_RECORD_DATA == type)
    {
        cpu_req_type = FIB_CPU_ACCESS_REQ_READ_X;
    }
    else
    {
        DRV_DBG_INFO("\nLPM Embeded Tcam Memory Type is useless!\n");
        return DRV_E_INVALID_TCAM_TYPE;
    }

    DRV_IOW_FIELD(LpmTcamIpCpuReqCtl_t, LpmTcamIpCpuReqCtl_tcamIpCpuReq_f,
                          &cpu_req, lpm_tcam_ip_cpu_req_ctl);
    DRV_IOW_FIELD(LpmTcamIpCpuReqCtl_t, LpmTcamIpCpuReqCtl_tcamIpCpuReqType_f,
                          &cpu_req_type, lpm_tcam_ip_cpu_req_ctl);
    DRV_IOW_FIELD(LpmTcamIpCpuReqCtl_t, LpmTcamIpCpuReqCtl_tcamIpCpuIndex_f,
                          &cpu_index, lpm_tcam_ip_cpu_req_ctl);
    cmd = DRV_IOW(LpmTcamIpCpuReqCtl_t, DRV_ENTRY_FLAG);
    ret = drv_goldengate_ioctl(chip_id, 0, cmd, lpm_tcam_ip_cpu_req_ctl);

    if (ret < DRV_E_NONE)
    {
        return ret;
    }

    while (TRUE)
    {
        cmd = DRV_IOR(LpmTcamIpCpuReqCtl_t, LpmTcamIpCpuReqCtl_tcamIpCpuReadDataValid_f);
        ret = drv_goldengate_ioctl(chip_id, 0, cmd, &cpu_read_data_valid);
        if (ret < DRV_E_NONE)
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
    ret = drv_goldengate_ioctl(chip_id, 0, cmd, data);
    if (ret < DRV_E_NONE)
    {
        return ret;
    }

    cpu_read_data_valid = FALSE;

    cmd = DRV_IOW(LpmTcamIpCpuReqCtl_t, LpmTcamIpCpuReqCtl_tcamIpCpuReadDataValid_f);
    ret = drv_goldengate_ioctl(chip_id, 0, cmd, &cpu_read_data_valid);
    if (ret < DRV_E_NONE)
    {
        return ret;
    }

    return DRV_E_NONE;
}

/**
 @brief Real embeded LPM tcam write operation I/O (data&mask write)
*/
STATIC int32
_drv_goldengate_chip_write_lpm_tcam_ip_data_mask(uint8 chip_id, uint32 index,
                                      uint32* data, uint32* mask)
{
    uint32 lpm_tcam_ip_cpu_req_ctl[LPM_TCAM_IP_CPU_REQ_CTL_BYTES/4] = {0};
    uint32 lpm_tcam_ip_cpu_wr_data[LPM_TCAM_IP_CPU_WR_DATA_BYTES/4] = {0};
    uint32 cpu_req = 0, cpu_req_type = 0, cpu_index = 0;
    uint32 cmd = 0;
    uint32 time_out = 0;
    int32 ret = DRV_E_NONE;
    tbl_entry_t tbl_entry;

    DRV_PTR_VALID_CHECK(data);
    DRV_PTR_VALID_CHECK(mask);

    /* Attention that we MUST write the mask before writing corresponding data!! */
    /* because the real store in TCAM is X/Y , and the TCAM access write data and mask at the same time, */
    /* so we should write mask at first, then tcam data. */
    /* X = ~data & mask ; Y = data & mask */
    sal_memcpy((uint8*)lpm_tcam_ip_cpu_wr_data,(uint8*)data, DRV_LPM_KEY_BYTES_PER_ENTRY);

    sal_memcpy((uint8*)lpm_tcam_ip_cpu_wr_data + DRV_LPM_KEY_BYTES_PER_ENTRY, (uint8*)mask, DRV_LPM_KEY_BYTES_PER_ENTRY);

    cmd = DRV_IOW(LpmTcamIpCpuWrData_t, DRV_ENTRY_FLAG);
    ret = drv_goldengate_ioctl(chip_id, 0, cmd, lpm_tcam_ip_cpu_wr_data);
    if (ret < DRV_E_NONE)
    {
        return ret;
    }

    cpu_index = index;
    cpu_req_type = FIB_CPU_ACCESS_REQ_WRITE_DATA_MASK;
    cpu_req = TRUE;

    DRV_IOW_FIELD(LpmTcamIpCpuReqCtl_t, LpmTcamIpCpuReqCtl_tcamIpCpuReq_f,
                          &cpu_req, lpm_tcam_ip_cpu_req_ctl);
    DRV_IOW_FIELD(LpmTcamIpCpuReqCtl_t, LpmTcamIpCpuReqCtl_tcamIpCpuReqType_f,
                          &cpu_req_type, lpm_tcam_ip_cpu_req_ctl);
    DRV_IOW_FIELD(LpmTcamIpCpuReqCtl_t, LpmTcamIpCpuReqCtl_tcamIpCpuIndex_f,
                          &cpu_index, lpm_tcam_ip_cpu_req_ctl);

    cmd = DRV_IOW(LpmTcamIpCpuReqCtl_t, DRV_ENTRY_FLAG);
    ret = drv_goldengate_ioctl(chip_id, 0, cmd, lpm_tcam_ip_cpu_req_ctl);
    if (ret < DRV_E_NONE)
    {
        return ret;
    }

    while (TRUE)
    {
        cpu_req = TRUE;
        cmd = DRV_IOR(LpmTcamIpCpuReqCtl_t, LpmTcamIpCpuReqCtl_tcamIpCpuReq_f);
        ret = drv_goldengate_ioctl(chip_id, 0, cmd, &cpu_req);
        if (ret < DRV_E_NONE)
        {
            return ret;
        }

        if (!cpu_req)
        {
            break;
        }

        if ((time_out ++) > DRV_TIME_OUT)
        {
            return DRV_E_TIME_OUT;
        }
    }

    tbl_entry.data_entry = data;
    tbl_entry.mask_entry = mask;
    drv_goldengate_ecc_recover_store(chip_id, DRV_ECC_MEM_TCAM_KEY7, index, &tbl_entry);

    return DRV_E_NONE;
}

/**
 @brief Real embeded LPM tcam remove operation I/O
*/
STATIC int32
_drv_goldengate_chip_remove_lpm_tcam_ip_entry(uint8 chip_id, uint32 tcam_index)
{
    uint32 lpm_tcam_ip_cpu_req_ctl[LPM_TCAM_IP_CPU_REQ_CTL_BYTES/4] = {0};
    int32 ret = DRV_E_NONE;
    uint32 cmd = 0;
    uint32 time_out = 0;
    uint32 mask = 0;
    tbl_entry_t tbl_entry;
    uint32 cpu_req = 0, cpu_req_type = 0, cpu_index = 0;

    cpu_index = tcam_index;
    cpu_req_type = FIB_CPU_ACCESS_REQ_INVALID_ENTRY;
    cpu_req = TRUE;

    DRV_IOW_FIELD(LpmTcamIpCpuReqCtl_t, LpmTcamIpCpuReqCtl_tcamIpCpuReq_f,
                          &cpu_req, lpm_tcam_ip_cpu_req_ctl);
    DRV_IOW_FIELD(LpmTcamIpCpuReqCtl_t, LpmTcamIpCpuReqCtl_tcamIpCpuReqType_f,
                          &cpu_req_type, lpm_tcam_ip_cpu_req_ctl);
    DRV_IOW_FIELD(LpmTcamIpCpuReqCtl_t, LpmTcamIpCpuReqCtl_tcamIpCpuIndex_f,
                          &cpu_index, lpm_tcam_ip_cpu_req_ctl);

    cmd = DRV_IOW(LpmTcamIpCpuReqCtl_t, DRV_ENTRY_FLAG);
    ret = drv_goldengate_ioctl(chip_id, 0, cmd, lpm_tcam_ip_cpu_req_ctl);
    if (ret < DRV_E_NONE)
    {
        return ret;
    }

    while (TRUE)
    {
        cpu_req = TRUE;
        cmd = DRV_IOR(LpmTcamIpCpuReqCtl_t, LpmTcamIpCpuReqCtl_tcamIpCpuReq_f);
        ret = drv_goldengate_ioctl(chip_id, 0, cmd, &cpu_req);
        if (ret < DRV_E_NONE)
        {
            return ret;
        }

        if (!cpu_req)
        {
            break;
        }

        if ((time_out ++) > DRV_TIME_OUT)
        {
            return DRV_E_TIME_OUT;
        }
    }

    tbl_entry.data_entry = NULL;
    tbl_entry.mask_entry = &mask;
    drv_goldengate_ecc_recover_store(chip_id, DRV_ECC_MEM_TCAM_KEY7, tcam_index, &tbl_entry);

    return DRV_E_NONE;
}

/**
 @brief Real embeded LPM tcam read operation I/O
*/
STATIC int32
_drv_goldengate_chip_read_lpm_tcam_nat_entry(uint8 chip_id, uint32 index,
                             uint32* data, tcam_mem_type_e type, uint8 slice_id)
{
    uint32 lpm_tcam_nat_cpu_req_ctl[LPM_TCAM_NAT_CPU_REQ_CTL_BYTES/4] = {0};
    uint32 cmd;
    int32 ret = DRV_E_NONE;
    uint32 cpu_req = 0, cpu_req_type = 0, cpu_index = 0;
    uint32 cpu_read_data_valid = 0, time_out = 0;
    uint32 tb_id = 0;
    uint32 tb_data_id = 0;

    DRV_PTR_VALID_CHECK(data);

    cpu_index = index;
    cpu_req = TRUE;
    if (DRV_INT_LPM_TCAM_RECORD_MASK == type)
    {
        cpu_req_type = FIB_CPU_ACCESS_REQ_READ_Y;
    }
    else if (DRV_INT_LPM_TCAM_RECORD_DATA == type)
    {
        cpu_req_type = FIB_CPU_ACCESS_REQ_READ_X;
    }
    else
    {
        DRV_DBG_INFO("\nLPM Embeded Tcam Memory Type is useless!\n");
        return DRV_E_INVALID_TCAM_TYPE;
    }

    tb_id = (slice_id == 2)?LpmTcamNatCpuReqCtl2_t:(LpmTcamNatCpuReqCtl0_t+slice_id);
    tb_data_id = (slice_id == 2)?LpmTcamNatCpuRdData2_t:(LpmTcamNatCpuRdData0_t+slice_id);

    DRV_IOW_FIELD(tb_id, LpmTcamNatCpuReqCtl0_tcamNatCpuReq_f,
                          &cpu_req, lpm_tcam_nat_cpu_req_ctl);
    DRV_IOW_FIELD(tb_id, LpmTcamNatCpuReqCtl0_tcamNatCpuReqType_f,
                          &cpu_req_type, lpm_tcam_nat_cpu_req_ctl);
    DRV_IOW_FIELD(tb_id, LpmTcamNatCpuReqCtl0_tcamNatCpuIndex_f,
                          &cpu_index, lpm_tcam_nat_cpu_req_ctl);

    cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
    ret = drv_goldengate_ioctl(chip_id, 0, cmd, lpm_tcam_nat_cpu_req_ctl);
    if (ret < DRV_E_NONE)
    {
        return ret;
    }

    while (TRUE)
    {
        cmd = DRV_IOR(tb_id, LpmTcamNatCpuReqCtl0_tcamNatCpuReadDataValid_f);
        ret = drv_goldengate_ioctl(chip_id, 0, cmd, &cpu_read_data_valid);
        if (ret < DRV_E_NONE)
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

    cmd = DRV_IOR(tb_data_id, DRV_ENTRY_FLAG);
    ret = drv_goldengate_ioctl(chip_id, 0, cmd, data);
    if (ret < DRV_E_NONE)
    {
        return ret;
    }

    cpu_read_data_valid = FALSE;

    cmd = DRV_IOW(tb_id, LpmTcamNatCpuReqCtl0_tcamNatCpuReadDataValid_f);
    ret = drv_goldengate_ioctl(chip_id, 0, cmd, &cpu_read_data_valid);
    if (ret < DRV_E_NONE)
    {
        return ret;
    }

    return DRV_E_NONE;
}

/**
 @brief Real embeded LPM tcam write operation I/O (data&mask write)
*/
STATIC int32
_drv_goldengate_chip_write_lpm_tcam_nat_data_mask(uint8 chip_id, uint32 index,
                                       uint32* data, uint32* mask)
{
    uint32 lpm_tcam_nat_cpu_req_ctl[LPM_TCAM_NAT_CPU_REQ_CTL_BYTES/4] = {0};
    uint32 lpm_tcam_nat_cpu_wr_data[LPM_TCAM_NAT_CPU_WR_DATA_BYTES/4] = {0};
    uint32 cpu_req = 0, cpu_req_type = 0, cpu_index = 0;
    uint32 cmd = 0;
    uint32 time_out = 0;
    int32 ret = DRV_E_NONE;
    tbl_entry_t tbl_entry;

    DRV_PTR_VALID_CHECK(data);
    DRV_PTR_VALID_CHECK(mask);

    /* Attention that we MUST write the mask before writing corresponding data!! */
    /* because the real store in TCAM is X/Y , and the TCAM access write data and mask at the same time, */
    /* so we should write mask at first, then tcam data. */
    /* X = ~data & mask ; Y = data & mask */
    sal_memcpy((uint8*)lpm_tcam_nat_cpu_wr_data,(uint8*)data, DRV_BYTES_PER_ENTRY*2);

    sal_memcpy((uint8*)lpm_tcam_nat_cpu_wr_data + DRV_BYTES_PER_ENTRY*2, (uint8*)mask, DRV_BYTES_PER_ENTRY*2);

    cmd = DRV_IOW(LpmTcamNatCpuWrData2_t, DRV_ENTRY_FLAG);
    ret = drv_goldengate_ioctl(chip_id, 0, cmd, lpm_tcam_nat_cpu_wr_data);
    if (ret < DRV_E_NONE)
    {
        return ret;
    }

    cpu_index = index;
    cpu_req_type = FIB_CPU_ACCESS_REQ_WRITE_DATA_MASK;
    cpu_req = TRUE;

    DRV_IOW_FIELD(LpmTcamNatCpuReqCtl2_t, LpmTcamNatCpuReqCtl2_tcamNatCpuReq_f,
                          &cpu_req, lpm_tcam_nat_cpu_req_ctl);
    DRV_IOW_FIELD(LpmTcamNatCpuReqCtl2_t, LpmTcamNatCpuReqCtl2_tcamNatCpuReqType_f,
                          &cpu_req_type, lpm_tcam_nat_cpu_req_ctl);
    DRV_IOW_FIELD(LpmTcamNatCpuReqCtl2_t, LpmTcamNatCpuReqCtl2_tcamNatCpuIndex_f,
                          &cpu_index, lpm_tcam_nat_cpu_req_ctl);

    cmd = DRV_IOW(LpmTcamNatCpuReqCtl2_t, DRV_ENTRY_FLAG);
    ret = drv_goldengate_ioctl(chip_id, 0, cmd, lpm_tcam_nat_cpu_req_ctl);
    if (ret < DRV_E_NONE)
    {
        return ret;
    }

    while (TRUE)
    {
        cpu_req = TRUE;
        cmd = DRV_IOR(LpmTcamNatCpuReqCtl2_t, LpmTcamNatCpuReqCtl2_tcamNatCpuReq_f);
        ret = drv_goldengate_ioctl(chip_id, 0, cmd, &cpu_req);
        if (ret < DRV_E_NONE)
        {
            return ret;
        }

        if (!cpu_req)
        {
            break;
        }

        if ((time_out ++) > DRV_TIME_OUT)
        {
            return DRV_E_TIME_OUT;
        }
    }

    tbl_entry.data_entry = data;
    tbl_entry.mask_entry = mask;
    drv_goldengate_ecc_recover_store(chip_id, DRV_ECC_MEM_TCAM_KEY8, index, &tbl_entry);

    return DRV_E_NONE;
}

/**
 @brief Real embeded LPM tcam remove operation I/O
*/
STATIC int32
_drv_goldengate_chip_remove_lpm_tcam_nat_entry(uint8 chip_id, uint32 tcam_index)
{
    uint32 lpm_tcam_nat_cpu_req_ctl[LPM_TCAM_NAT_CPU_REQ_CTL_BYTES/4] = {0};
    int32 ret = DRV_E_NONE;
    uint32 mask = 0;
    uint32 cmd = 0;
    uint32 time_out = 0;
    tbl_entry_t tbl_entry;
    uint32 cpu_req = 0, cpu_req_type = 0, cpu_index = 0;

    cpu_index = tcam_index;
    cpu_req_type = FIB_CPU_ACCESS_REQ_INVALID_ENTRY;
    cpu_req = TRUE;

    DRV_IOW_FIELD(LpmTcamNatCpuReqCtl2_t, LpmTcamNatCpuReqCtl2_tcamNatCpuReq_f,
                          &cpu_req, lpm_tcam_nat_cpu_req_ctl);
    DRV_IOW_FIELD(LpmTcamNatCpuReqCtl2_t, LpmTcamNatCpuReqCtl2_tcamNatCpuReqType_f,
                          &cpu_req_type, lpm_tcam_nat_cpu_req_ctl);
    DRV_IOW_FIELD(LpmTcamNatCpuReqCtl2_t, LpmTcamNatCpuReqCtl2_tcamNatCpuIndex_f,
                          &cpu_index, lpm_tcam_nat_cpu_req_ctl);

    cmd = DRV_IOW(LpmTcamNatCpuReqCtl2_t, DRV_ENTRY_FLAG);
    ret = drv_goldengate_ioctl(chip_id, 0, cmd, lpm_tcam_nat_cpu_req_ctl);
    if (ret < DRV_E_NONE)
    {
        return ret;
    }

    while (TRUE)
    {
        cpu_req = TRUE;
        cmd = DRV_IOR(LpmTcamNatCpuReqCtl2_t, LpmTcamNatCpuReqCtl2_tcamNatCpuReq_f);
        ret = drv_goldengate_ioctl(chip_id, 0, cmd, &cpu_req);
        if (ret < DRV_E_NONE)
        {
            return ret;
        }

        if (!cpu_req)
        {
            break;
        }

        if ((time_out ++) > DRV_TIME_OUT)
        {
            return DRV_E_TIME_OUT;
        }
    }

    tbl_entry.data_entry = NULL;
    tbl_entry.mask_entry = &mask;
    drv_goldengate_ecc_recover_store(chip_id, DRV_ECC_MEM_TCAM_KEY8, tcam_index, &tbl_entry);

    return DRV_E_NONE;
}

/**
 @brief convert embeded tcam content from X/Y format to data/mask format
*/
STATIC int32
_drv_goldengate_chip_convert_tcam_dump_content(uint8 chip_id, uint32 tcam_entry_width, uint32 *data, uint32 *mask, uint8* p_empty)
{
     /*#define TCAM_ENTRY_WIDTH 80*/
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
    for (bit_pos = 0; bit_pos < tcam_entry_width; bit_pos++)
    {
        index = bit_pos / 32;
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
            *p_empty |= 1;
            /* Here data = 0, mask = 0, self-defined */
            CLEAR_BIT(data[index], bit_offset);    /* Return_Data = 0; Return_Mask = 0 */
            CLEAR_BIT(mask[index], bit_offset);
             /*DRV_DBG_INFO("ERROR! Read un-set Tcam entry!\n");*/
        }
    }

    return DRV_E_NONE;
}
#endif

/**
 @brief Init embeded and external tcam mutex
*/
int32
drv_goldengate_chip_tcam_mutex_init(uint8 chip_id_offset)
{
    int32 ret;

    DRV_CHIP_ID_VALID_CHECK(chip_id_offset + drv_gg_init_chip_info.drv_init_chipid_base);

    ret = sal_mutex_create(&p_gg_flow_tcam_mutex[chip_id_offset]);
    if (ret || (!p_gg_flow_tcam_mutex[chip_id_offset]))
    {
        return DRV_E_FAIL_CREATE_MUTEX;
    }

    ret = sal_mutex_create(&p_gg_lpm_ip_tcam_mutex[chip_id_offset]);
    if (ret || (!p_gg_lpm_ip_tcam_mutex[chip_id_offset]))
    {
        return DRV_E_FAIL_CREATE_MUTEX;
    }

    ret = sal_mutex_create(&p_gg_lpm_nat_tcam_mutex[chip_id_offset]);
    if (ret || (!p_gg_lpm_nat_tcam_mutex[chip_id_offset]))
    {
        return DRV_E_FAIL_CREATE_MUTEX;
    }

    ret = sal_mutex_create(&p_gg_entry_mutex[chip_id_offset]);
    if (ret || (!p_gg_entry_mutex[chip_id_offset]))
    {
        return DRV_E_FAIL_CREATE_MUTEX;
    }

    ret = sal_mutex_create(&p_gg_mep_mutex[chip_id_offset]);
    if (ret || (!p_gg_mep_mutex[chip_id_offset]))
    {
        return DRV_E_FAIL_CREATE_MUTEX;
    }

    return DRV_E_NONE;
}


/* SDK Modify */
int32
_drv_goldengate_chip_table_is_slice1(tbls_id_t tbl_id)
{
    /*for two slice*/

    uint32 i = 0;
    uint32 slice1_tbl_id[] = {
    DynamicDsHashLpmPipeIndexCam1_t,
    DynamicDsHashMacHashIndexCam1_t,
    DynamicDsAdDsFlowIndexCam1_t,
    DynamicDsHashUserIdHashIndexCam1_t,
    DynamicDsHashUserIdAdIndexCam1_t,
    DynamicDsAdEgrOamHashIndexCam1_t,
    DynamicDsHashIpfixHashIndexCam1_t,
    DynamicDsAdDsIpfixIndexCam1_t,
    DynamicDsAdDsMacIndexCam1_t,
    DynamicDsAdDsIpIndexCam1_t,
    DynamicDsAdDsNextHopIndexCam1_t,
    DynamicDsAdDsMetIndexCam1_t,
    DynamicDsHashSharedRam0ArbCtl1_t,
    DynamicDsHashSharedRam1ArbCtl1_t,
    DynamicDsHashSharedRam2ArbCtl1_t,
    DynamicDsHashSharedRam3ArbCtl1_t,
    DynamicDsHashSharedRam4ArbCtl1_t,
    DynamicDsHashSharedRam5ArbCtl1_t,
    DynamicDsHashSharedRam6ArbCtl1_t,
    DynamicDsAdDsIpMacRam0ArbCtl1_t,
    DynamicDsAdDsIpMacRam1ArbCtl1_t,
    DynamicDsAdDsIpMacRam2ArbCtl1_t,
    DynamicDsAdDsIpMacRam3ArbCtl1_t,
    DynamicDsHashUserIdHashRam0ArbCtl1_t,
    DynamicDsHashUserIdHashRam1ArbCtl1_t,
    DynamicDsHashUserIdAdRamArbCtl1_t,
    DynamicDsAdNextHopMetRam0ArbCtl1_t,
    DynamicDsAdNextHopMetRam1ArbCtl1_t};

    return 0;

    for (i = 0 ; i< (sizeof(slice1_tbl_id)/ sizeof(uint32)); i++)
    {
        if (tbl_id == slice1_tbl_id[i])
        {
            return 1;
        }
    }
    return 0;
}


/**
 @brief The function write table data to a sram memory location
*/
int32
drv_goldengate_chip_sram_tbl_write(uint8 chip_id, tbls_id_t tbl_id, uint32 index, uint32* data)
{
    uint32 start_data_addr = 0, entry_size = 0, max_index_num = 0;
    int32 ret = DRV_E_NONE;
    uint32 blknum = 0, local_idx = 0,is_sec_half = 0;
    uint32 burst_op = 0;
    uint8  ecc_en = 0;
    tbl_entry_t tbl_entry;
    #if (SDK_WORK_PLATFORM == 0)
    uint32 hw_addr_size_per_idx = 0;
    #endif



    DRV_PTR_VALID_CHECK(data);
    DRV_CHIP_ID_VALID_CHECK(chip_id);
    DRV_TBL_ID_VALID_CHECK(tbl_id);

#if (SDK_WORK_PLATFORM == 0) /* SDK Modify */
    if (_drv_goldengate_chip_table_is_slice1(tbl_id))
    {
        return DRV_E_NONE;
    }
#endif
    /* check if the index num exceed the max index num of the tbl */
    max_index_num = TABLE_MAX_INDEX(tbl_id);
    entry_size = TABLE_ENTRY_SIZE(tbl_id);
    if (g_gg_access_type == DRV_PCI_ACCESS)
    {
        burst_op = !(TABLE_OP_TYPE(tbl_id)&(Op_DisBurst|Op_DisWriteMask)) && g_gg_burst_en;
    }

    if (drv_goldengate_table_is_tcam_ad(tbl_id) || drv_goldengate_table_is_lpm_tcam_ad(tbl_id)) /* SDK Modify*/
    {
        index = index* TCAM_KEY_SIZE(tbl_id);

#if 0
        DRV_DBG_INFO("Table[%d]: OldIndex = %d, NewIndex:%d \n", tbl_id, old_index, index);
#endif
    }

    if(drv_goldengate_table_is_tcam_ad(tbl_id))
    {
        drv_goldengate_chip_flow_tcam_get_blknum_index(chip_id, tbl_id, index, &blknum, &local_idx, &is_sec_half);
        DRV_IF_ERROR_RETURN(drv_goldengate_chip_write_flow_tcam_ad_entry(chip_id, blknum, local_idx, data));
    }
    else
    {
        /* According to index value to get the table 's index address in sw */
        DRV_IF_ERROR_RETURN(drv_goldengate_table_get_hw_addr(tbl_id, index, &start_data_addr, FALSE));

        /*for burst io*/
        start_data_addr = start_data_addr +burst_op*BURST_IO_WRITE;

        #if (SDK_WORK_PLATFORM == 0)
        if(drv_goldengate_table_is_parser_tbl(tbl_id))
        {

            DRV_IF_ERROR_RETURN(drv_goldengate_table_consum_hw_addr_size_per_index(tbl_id, &hw_addr_size_per_idx));
            if (gg_duplicate_addr_type == SLICE_Addr_0)
            {
                start_data_addr = TABLE_DATA_BASE(tbl_id, 1) + index *hw_addr_size_per_idx*DRV_BYTES_PER_WORD+burst_op*BURST_IO_WRITE;
                ret = drv_goldengate_chip_write_sram_entry(chip_id, start_data_addr, data, entry_size);

                start_data_addr = TABLE_DATA_BASE(tbl_id, 4) + index *hw_addr_size_per_idx*DRV_BYTES_PER_WORD+burst_op*BURST_IO_WRITE;
                ret = drv_goldengate_chip_write_sram_entry(chip_id, start_data_addr, data, entry_size);
            }
            else if (gg_duplicate_addr_type == SLICE_Addr_1)
            {
                start_data_addr = TABLE_DATA_BASE(tbl_id, 2) + index *hw_addr_size_per_idx*DRV_BYTES_PER_WORD+burst_op*BURST_IO_WRITE;
                ret = drv_goldengate_chip_write_sram_entry(chip_id, start_data_addr, data, entry_size);

                start_data_addr = TABLE_DATA_BASE(tbl_id, 5) + index *hw_addr_size_per_idx*DRV_BYTES_PER_WORD+burst_op*BURST_IO_WRITE;
                ret = drv_goldengate_chip_write_sram_entry(chip_id, start_data_addr, data, entry_size);
            }
            else
            {
                start_data_addr = TABLE_DATA_BASE(tbl_id, 0) + index *hw_addr_size_per_idx*DRV_BYTES_PER_WORD+burst_op*BURST_IO_WRITE;
                ret = drv_goldengate_chip_write_sram_entry(chip_id, start_data_addr, data, entry_size);

                start_data_addr = TABLE_DATA_BASE(tbl_id, 3) + index *hw_addr_size_per_idx*DRV_BYTES_PER_WORD+burst_op*BURST_IO_WRITE;
                ret = drv_goldengate_chip_write_sram_entry(chip_id, start_data_addr, data, entry_size);
            }

            return ret;
        }

        if (gg_duplicate_addr_type == SLICE_Addr_0)
        {
            if (TABLE_ENTRY_TYPE(tbl_id) == SLICE_Cascade)
            {
                if (index >= (TABLE_MAX_INDEX(tbl_id)/2))
                {
                    return DRV_E_NONE;
                }
            }
        }
        else if (gg_duplicate_addr_type == SLICE_Addr_1)
        {
            if (TABLE_ENTRY_TYPE(tbl_id) == SLICE_Cascade)
            {
                if (index < (TABLE_MAX_INDEX(tbl_id)/2))
                {
                    return DRV_E_NONE;
                }
            }
        }
        #endif

        if (max_index_num <= index)
    {
        DRV_DBG_INFO("ERROR! (drv_goldengate_write_sram_tbl): chip-0x%x, tbl-0x%x, index-0x%x exceeds the max_index-0x%x.\n",
                    chip_id, tbl_id, index, max_index_num);
        return DRV_E_EXCEED_MAX_SIZE;
    }

        ret = drv_goldengate_chip_write_sram_entry(chip_id, start_data_addr, data, entry_size);
    }

    ecc_en = drv_goldengate_ecc_recover_get_enable();

    if (ecc_en && (FlowTcamAdMem_t != tbl_id) && (FlowTcamCpuReqCtl_t != tbl_id))
    {
        tbl_entry.data_entry = data;
        tbl_entry.mask_entry = NULL;

        drv_goldengate_ecc_recover_store(chip_id, tbl_id, index, &tbl_entry);
    }

    if (ret!= DRV_E_NONE)
    {
        DRV_DBG_INFO("tbl_id:%d, index:%d, start_data_addr:%08x \n",tbl_id, index,start_data_addr);
    }

    return ret;
}

/**
 @brief The function read table data from a sram memory location
*/
int32
drv_goldengate_chip_sram_tbl_read(uint8 chip_id, tbls_id_t tbl_id, uint32 index, uint32* data)
{
    #define DYN_SRAM_ENTRY_BYTE 16
    uint32 start_data_addr = 0, entry_size = 0, max_index_num = 0;
    int32 ret = DRV_E_NONE;
    uint32 blknum = 0, local_idx = 0,is_sec_half = 0;
    uint32 burst_op = 0;

    DRV_PTR_VALID_CHECK(data);
    DRV_CHIP_ID_VALID_CHECK(chip_id);
    DRV_TBL_ID_VALID_CHECK(tbl_id);

    /* check if the index num exceed the max index num of the tbl */
    max_index_num = TABLE_MAX_INDEX(tbl_id);
    entry_size = TABLE_ENTRY_SIZE(tbl_id);

#if (SDK_WORK_PLATFORM == 0) /* SDK Modify */
    if (_drv_goldengate_chip_table_is_slice1(tbl_id))
    {
        return DRV_E_NONE;
    }
#endif

    if (g_gg_access_type == DRV_PCI_ACCESS)
    {
        burst_op = !(TABLE_OP_TYPE(tbl_id)&Op_DisBurst) && g_gg_burst_en;
    }

    if (drv_goldengate_table_is_tcam_ad(tbl_id) || drv_goldengate_table_is_lpm_tcam_ad(tbl_id)) /* SDK Modify*/
    {
        index = index* TCAM_KEY_SIZE(tbl_id);

#if 0
        DRV_DBG_INFO("Table[%d]: OldIndex = %d, NewIndex:%d \n", tbl_id, old_index, index);
#endif
    }

    if(drv_goldengate_table_is_tcam_ad(tbl_id))
    {
        drv_goldengate_chip_flow_tcam_get_blknum_index(chip_id, tbl_id, index, &blknum, &local_idx, &is_sec_half);

        DRV_IF_ERROR_RETURN(_drv_goldengate_chip_read_flow_tcam_ad_entry(chip_id, blknum, local_idx, data));
    }
    else
    {
        /* According to index value to get the table 's index address in sw */
        DRV_IF_ERROR_RETURN(drv_goldengate_table_get_hw_addr(tbl_id, index, &start_data_addr, FALSE));

        #if (SDK_WORK_PLATFORM == 0)
        if (gg_duplicate_addr_type == SLICE_Addr_0)
        {
            if (TABLE_ENTRY_TYPE(tbl_id) == SLICE_Cascade)
            {
                if (index >= (TABLE_MAX_INDEX(tbl_id)/2))
                {
                    return DRV_E_NONE;
                }
            }

            if(drv_goldengate_table_is_slice1(tbl_id))
            {
                return DRV_E_NONE;
            }
        }
        else if (gg_duplicate_addr_type == SLICE_Addr_1)
        {
            if (TABLE_ENTRY_TYPE(tbl_id) == SLICE_Cascade)
            {
                if (index < (TABLE_MAX_INDEX(tbl_id)/2))
                {
                    return DRV_E_NONE;
                }
            }
        }
        #endif

        if (max_index_num <= index)
        {
            DRV_DBG_INFO("ERROR! (drv_goldengate_read_sram_tbl): chip-0x%x, tbl-0x%x, index-0x%x exceeds the max_index-0x%x.\n",
                        chip_id, tbl_id, index, max_index_num);
            return DRV_E_EXCEED_MAX_SIZE;
        }

        start_data_addr = start_data_addr + burst_op*BURST_IO_READ;

         /*12W entry use 12 continues address to read & write*/
        #if 0
        if((DRV_BYTES_PER_ENTRY*4 == entry_size) && (drv_goldengate_table_is_dynamic_table(tbl_id)))
        {
            ret = drv_chip_read_sram_entry(chip_id, start_data_addr, data, DRV_BYTES_PER_ENTRY*2);
            ret = drv_chip_read_sram_entry(chip_id, start_data_addr+DRV_ADDR_BYTES_PER_ENTRY*2, data+6, DRV_BYTES_PER_ENTRY*2);
        }
        else
        #endif
        {
            ret = drv_goldengate_chip_read_sram_entry(chip_id, start_data_addr, data, entry_size);
        }
    }
	#if 0 /*SDK modify*/
    if (DRV_E_PCI_CMD_ERROR == ret)
    {
        ret = DRV_E_NONE;
    }
	#endif

    return ret;
}

STATIC uint32
_drv_goldengate_chip_tcam_write_check_match(uint32* p_r_data, uint32* p_w_data, uint32* p_r_mask, uint32* p_w_mask, uint32 num)
{
    uint32 i = 0;

    for (i = 0; i < num; i++)
    {
#if 0
        DRV_DBG_INFO("Table(%d), Index: %d, No.%d r_data:0x%x, r_mask:0x%x, w_data:0x%x, w_mask:0x%x\n",
                  tbl_id,index, i,p_r_data_tmp[i], p_r_mask_tmp[i], p_w_data_tmp[i], p_w_mask_tmp[i]);
#endif

        if (((p_r_data[i])&(p_r_mask[i])) != ((p_w_data[i])&(p_w_mask[i])))
        {

            /* DRV_DBG_INFO("Not Match!!!\n");*/
           p_r_data[i] = 0;
           p_r_mask[i] = 0;
           return  FALSE;
        }
    }

    return TRUE;

}

STATIC uint32
_drv_goldengate_chip_tcam_remove_check_match(uint32* p_r_data, uint32* p_r_mask, uint32 num)
{
    uint32 i = 0;

    if (DRV_LPM_WORDS_PER_ENTRY*4 <= num)
    {
        return FALSE;
    }

    for (i = 0; i < num; i++)
    {
#if 0
        DRV_DBG_INFO("Table(%d), Index: %d, No.%d r_data:0x%x, r_mask:0x%x, w_data:0x%x, w_mask:0x%x\n",
                  tbl_id,index, i,p_r_data_tmp[i], p_r_mask_tmp[i], p_w_data_tmp[i], p_w_mask_tmp[i]);
#endif

        if (((p_r_data[i]) | (p_r_mask[i])))
        {

             /*DRV_DBG_INFO("Not Match!!!\n");*/
            p_r_data[i] = 0;
            p_r_mask[i] = 0;
            return FALSE;
        }
    }

    return TRUE;

}

/**
 @brief write tcam interface (include operate model and real tcam)
*/
int32
drv_goldengate_chip_tcam_tbl_write(uint8 chip_id_offset, tbls_id_t tbl_id, uint32 index, tbl_entry_t* entry)
{
#if 1
    uint32* mask = NULL;
    uint32* data = NULL;
     /*uint32 hw_database_addr = TABLE_DATA_BASE(tbl_id);*/
     /*uint32 hw_maskbase_addr = 0;*/
     /*uint32 entry_size = TABLE_ENTRY_SIZE(tbl_id);*/
    uint32 key_size = TCAM_KEY_SIZE(tbl_id);
     /*uint32 tcam_asic_data_base = 0, tcam_asic_mask_base = 0;*/
    uint32 entry_num_each_idx = 0, entry_idx = 0;
     /*uint32 index_by_tcam = 0;*/
    uint8 empty = 0;
    uint8 chip_id = chip_id_offset + drv_gg_init_chip_info.drv_init_chipid_base;
    bool is_lpm_tcam = FALSE, is_lpm_tcam_ip = FALSE;
     /*uint32 max_int_tcam_data_base_tmp;*/
     /*uint32 max_int_lpm_tcam_data_base_tmp;*/
    uint32 blknum = 0, local_idx = 0, is_sec_half = 0;
    uint32 try_cnt = 0;
    uint8 slice_id = 0;
    uint8 not_match = 0;

    DRV_PTR_VALID_CHECK(entry);
    DRV_PTR_VALID_CHECK(entry->data_entry);
    DRV_PTR_VALID_CHECK(entry->mask_entry);
    DRV_TBL_ID_VALID_CHECK(tbl_id);

     /*DRV_DBG_INFO("AddTcam Table(%d), Index:%d\n", tbl_id, index);*/

    if ((!drv_goldengate_table_is_tcam_key(tbl_id))
        && (!drv_goldengate_table_is_lpm_tcam_key(tbl_id)))
    {
        DRV_DBG_INFO("@@ ERROR! When write operation, INVALID Tcam key TblID! TblID: %d, file:%s line:%d function:%s\n",
                      tbl_id, __FILE__, __LINE__, __FUNCTION__);
        return DRV_E_INVALID_TBL;
    }

    if (TABLE_MAX_INDEX(tbl_id) <= index)
    {
        DRV_DBG_INFO("ERROR (drv_goldengate_chip_tcam_tbl_write): chip-0x%x, tbl-%d, index-0x%x exceeds the max_index-0x%x.\n",
                        chip_id, tbl_id, index, TABLE_MAX_INDEX(tbl_id));
        return DRV_E_EXCEED_MAX_SIZE;
    }

    if (drv_goldengate_table_is_tcam_key(tbl_id))
    {
        /* flow tcam w/r per 160 bit */
        entry_num_each_idx = key_size / (DRV_BYTES_PER_ENTRY*2);

        drv_goldengate_chip_flow_tcam_get_blknum_index(chip_id, tbl_id, index, &blknum, &local_idx, &is_sec_half);
        is_lpm_tcam = FALSE;

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
            local_idx = ((offset / entry_num_each_idx) +  index)*entry_num_each_idx;

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
           /*  local_idx = (is_sec_half << 9) | local_idx;*/
            is_lpm_tcam_ip = FALSE;
        }/*SDK Modify }*/
    }




     /*offset = (entry_size - key_size) / DRV_BYTES_PER_WORD; // word offset */

     /*data = entry->data_entry + offset;*/
     /*mask = entry->mask_entry + offset;*/

    data = entry->data_entry;
    mask = entry->mask_entry;

    for (entry_idx = 0; entry_idx < entry_num_each_idx; ++entry_idx)
    {
        uint32 data_tmp[DRV_LPM_WORDS_PER_ENTRY*4] = {0};
        uint32 mask_tmp[DRV_LPM_WORDS_PER_ENTRY*4] = {0};
        uint32 *p_r_data_tmp = data_tmp;
        uint32 *p_r_mask_tmp = mask_tmp;
        uint32 *p_w_data_tmp = 0;
        uint32 *p_w_mask_tmp = 0;
        uint32 word = 0;

        /* write real tcam address */
        if (!is_lpm_tcam)
        {
            FLOW_TCAM_LOCK(chip_id_offset);
            DRV_IF_ERROR_RETURN_WITH_UNLOCK(
            _drv_goldengate_chip_write_flow_tcam_data_mask(chip_id, blknum, local_idx,
            data + entry_idx*DRV_WORDS_PER_ENTRY*2,
            mask + entry_idx*DRV_WORDS_PER_ENTRY*2),
            p_gg_flow_tcam_mutex[chip_id_offset]);
            FLOW_TCAM_UNLOCK(chip_id_offset);
        }
        else
        {
            if (!g_gg_lpm_dma)
            {
                if (is_lpm_tcam_ip)
                {
					LPM_IP_TCAM_LOCK(chip_id_offset);
                    p_w_data_tmp = data + entry_idx*DRV_LPM_WORDS_PER_ENTRY;
                    p_w_mask_tmp = mask + entry_idx*DRV_LPM_WORDS_PER_ENTRY;
                    DRV_IF_ERROR_RETURN_WITH_UNLOCK(
                    _drv_goldengate_chip_write_lpm_tcam_ip_data_mask(chip_id, local_idx,
                    p_w_data_tmp, p_w_mask_tmp), p_gg_lpm_ip_tcam_mutex[chip_id_offset]);
					LPM_IP_TCAM_UNLOCK(chip_id_offset);
                }
                else
                {
					LPM_NAT_TCAM_LOCK(chip_id_offset);
                    p_w_data_tmp = data + entry_idx*DRV_WORDS_PER_ENTRY*2;
                    p_w_mask_tmp = mask + entry_idx*DRV_WORDS_PER_ENTRY*2;
                    DRV_IF_ERROR_RETURN_WITH_UNLOCK(
                    _drv_goldengate_chip_write_lpm_tcam_nat_data_mask(chip_id, local_idx,
                    p_w_data_tmp,p_w_mask_tmp), p_gg_lpm_nat_tcam_mutex[chip_id_offset]);
                    LPM_NAT_TCAM_UNLOCK(chip_id_offset);
                }
            }
            else
            {
                while (1)
                {
                    not_match = 0;
                    empty = 0;

                    if (is_lpm_tcam_ip)
                    {
                        LPM_IP_TCAM_LOCK(chip_id_offset);
                        p_w_data_tmp = data + entry_idx*DRV_LPM_WORDS_PER_ENTRY;
                        p_w_mask_tmp = mask + entry_idx*DRV_LPM_WORDS_PER_ENTRY;

                        DRV_IF_ERROR_RETURN_WITH_UNLOCK(
                        _drv_goldengate_chip_write_lpm_tcam_ip_data_mask(chip_id, local_idx,
                        p_w_data_tmp, p_w_mask_tmp), p_gg_lpm_ip_tcam_mutex[chip_id_offset]);

                        LPM_IP_TCAM_UNLOCK(chip_id_offset);

                        word = DRV_LPM_WORDS_PER_ENTRY;
                    }
                    else
                    {
                        LPM_NAT_TCAM_LOCK(chip_id_offset);
                        p_w_data_tmp = data + entry_idx*DRV_WORDS_PER_ENTRY*2;
                        p_w_mask_tmp = mask + entry_idx*DRV_WORDS_PER_ENTRY*2;

                        DRV_IF_ERROR_RETURN_WITH_UNLOCK(
                        _drv_goldengate_chip_write_lpm_tcam_nat_data_mask(chip_id, local_idx,
                        p_w_data_tmp,p_w_mask_tmp), p_gg_lpm_nat_tcam_mutex[chip_id_offset]);

                        LPM_NAT_TCAM_UNLOCK(chip_id_offset);
                        word = DRV_LPM_WORDS_PER_ENTRY*2;
                    }

                    /*read tcam for confirm write correct*/
                    if (is_lpm_tcam_ip)
                    {
                        LPM_IP_TCAM_LOCK(chip_id_offset);

                        DRV_IF_ERROR_RETURN_WITH_UNLOCK(
                        _drv_goldengate_chip_read_lpm_tcam_ip_entry(chip_id, local_idx,
                        p_r_mask_tmp, DRV_INT_LPM_TCAM_RECORD_MASK),
                        p_gg_lpm_ip_tcam_mutex[chip_id_offset]);

                        DRV_IF_ERROR_RETURN_WITH_UNLOCK(
                        _drv_goldengate_chip_read_lpm_tcam_ip_entry(chip_id, local_idx,
                        p_r_data_tmp, DRV_INT_LPM_TCAM_RECORD_DATA),
                        p_gg_lpm_ip_tcam_mutex[chip_id_offset]);

                        DRV_IF_ERROR_RETURN_WITH_UNLOCK(
                        _drv_goldengate_chip_convert_tcam_dump_content(chip_id,
                        DRV_LPM_KEY_BYTES_PER_ENTRY*8, p_r_data_tmp, p_r_mask_tmp,
                        &empty), p_gg_lpm_ip_tcam_mutex[chip_id_offset]);

                        LPM_IP_TCAM_UNLOCK(chip_id_offset);
                        if (_drv_goldengate_chip_tcam_write_check_match(p_r_data_tmp, p_w_data_tmp, p_r_mask_tmp, p_w_mask_tmp, word) == TRUE)
                        {
                            /*write tcam success*/
                            break;
                        }
                    }
                    else
                    {
                        for (slice_id = 0; slice_id < 2; slice_id++)
                        {
                            LPM_NAT_TCAM_LOCK(chip_id_offset);

                            DRV_IF_ERROR_RETURN_WITH_UNLOCK(
                            _drv_goldengate_chip_read_lpm_tcam_nat_entry(chip_id, local_idx,
                            p_r_mask_tmp, DRV_INT_LPM_TCAM_RECORD_MASK, slice_id),
                            p_gg_lpm_nat_tcam_mutex[chip_id_offset]);

                            DRV_IF_ERROR_RETURN_WITH_UNLOCK(
                            _drv_goldengate_chip_read_lpm_tcam_nat_entry(chip_id, local_idx,
                            p_r_data_tmp, DRV_INT_LPM_TCAM_RECORD_DATA, slice_id),
                            p_gg_lpm_nat_tcam_mutex[chip_id_offset]);

                            DRV_IF_ERROR_RETURN_WITH_UNLOCK(
                            _drv_goldengate_chip_convert_tcam_dump_content(chip_id,
                            DRV_BYTES_PER_ENTRY*8*2, p_r_data_tmp, p_r_mask_tmp,
                            &empty), p_gg_lpm_nat_tcam_mutex[chip_id_offset]);

                            LPM_NAT_TCAM_UNLOCK(chip_id_offset);

                            if (_drv_goldengate_chip_tcam_write_check_match(p_r_data_tmp, p_w_data_tmp, p_r_mask_tmp, p_w_mask_tmp, word) == FALSE)
                            {
                                not_match = 1;
                                break;
                            }
                        }

                        if (not_match == 0)
                        {
                            /*write tcam success*/
                            break;
                        }
                    }

                    try_cnt++;

                    if (try_cnt > 1000)
                    {
                        DRV_DBG_INFO("ADD_LPM_NOT_MATCH_CNT:%d, tbl_id:%d, index:%d!!!\n", try_cnt, tbl_id, index);
                        return DRV_E_TIME_OUT;
                    }

                }
            }
        }

        local_idx++;
    }
#endif



    return DRV_E_NONE;
}

/**
 @brief read tcam interface (include operate model and real tcam)
*/
int32
drv_goldengate_chip_tcam_tbl_read(uint8 chip_id_offset, tbls_id_t tbl_id, uint32 index, tbl_entry_t *entry)
{
    uint32* mask = NULL;
    uint32* data = NULL;
     /*uint32 hw_database_addr = TABLE_DATA_BASE(tbl_id);*/
     /*uint32 hw_maskbase_addr = 0;*/
     /*uint32 entry_size = TABLE_ENTRY_SIZE(tbl_id);*/
    uint32 key_size = TCAM_KEY_SIZE(tbl_id);
     /*uint32 tcam_asic_data_base, tcam_asic_mask_base;*/
    uint32 entry_num_each_idx = 0, entry_idx = 0;
    bool is_lpm_tcam = FALSE, is_lpm_tcam_ip = FALSE;
    uint8 empty = 0;
    uint8 chip_id = chip_id_offset + drv_gg_init_chip_info.drv_init_chipid_base;
     /*uint32 index_by_tcam;*/
     /*uint32 max_int_tcam_data_base_tmp;*/
     /*uint32 max_int_lpm_tcam_data_base_tmp;*/
    uint32 blknum = 0, local_idx = 0, is_sec_half = 0;
    tbl_entry_t tbl_entry;
    drv_ecc_recover_action_t recover_action;

    DRV_PTR_VALID_CHECK(entry);
    DRV_PTR_VALID_CHECK(entry->data_entry);
    DRV_PTR_VALID_CHECK(entry->mask_entry);
    DRV_CHIP_ID_VALID_CHECK(chip_id);
    DRV_TBL_ID_VALID_CHECK(tbl_id);

    if ((!drv_goldengate_table_is_tcam_key(tbl_id))
        && (!drv_goldengate_table_is_lpm_tcam_key(tbl_id)))
    {
        DRV_DBG_INFO("@@ ERROR! When read operation, INVALID Tcam key TblID! TblID: %d, file:%s line:%d function:%s\n",
                      tbl_id, __FILE__, __LINE__, __FUNCTION__);
        return DRV_E_INVALID_TBL;
    }

    if (TABLE_MAX_INDEX(tbl_id) <= index)
    {
        DRV_DBG_INFO("ERROR (drv_goldengate_chip_tcam_tbl_read): chip-0x%x, tbl-%d, index-0x%x exceeds the max_index-0x%x.\n",
                        chip_id, tbl_id, index, TABLE_MAX_INDEX(tbl_id));
        return DRV_E_EXCEED_MAX_SIZE;
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

     /*offset = (entry_size - key_size) / DRV_BYTES_PER_WORD; // word offset */

     /*data = entry->data_entry + offset;*/
     /*mask = entry->mask_entry + offset;*/

    data = entry->data_entry;
    mask = entry->mask_entry;

    for (entry_idx = 0; entry_idx < entry_num_each_idx; ++entry_idx)
    {
        empty = 0;
        /* read real tcam address */
        if (!is_lpm_tcam)
        {
            FLOW_TCAM_LOCK(chip_id_offset);

            DRV_IF_ERROR_RETURN_WITH_UNLOCK(
            _drv_goldengate_chip_read_flow_tcam_entry(chip_id, blknum, local_idx,
            mask + entry_idx*DRV_WORDS_PER_ENTRY*2, DRV_INT_TCAM_RECORD_MASK),
            p_gg_flow_tcam_mutex[chip_id_offset]);

            DRV_IF_ERROR_RETURN_WITH_UNLOCK(
            _drv_goldengate_chip_read_flow_tcam_entry(chip_id, blknum, local_idx,
            data + entry_idx*DRV_WORDS_PER_ENTRY*2, DRV_INT_TCAM_RECORD_DATA),
            p_gg_flow_tcam_mutex[chip_id_offset]);

            DRV_IF_ERROR_RETURN_WITH_UNLOCK(
            _drv_goldengate_chip_convert_tcam_dump_content(chip_id, DRV_BYTES_PER_ENTRY*8*2,
            data + entry_idx*DRV_WORDS_PER_ENTRY*2,
            mask + entry_idx*DRV_WORDS_PER_ENTRY*2, &empty),
            p_gg_flow_tcam_mutex[chip_id_offset]);

            tbl_entry.data_entry = empty ? NULL : data + entry_idx*DRV_WORDS_PER_ENTRY*2;
            tbl_entry.mask_entry = empty ? NULL : mask + entry_idx*DRV_WORDS_PER_ENTRY*2;

            drv_goldengate_ecc_recover_tcam(chip_id, DRV_ECC_MEM_TCAM_KEY0 + blknum,
                                 local_idx, &tbl_entry, &recover_action);

            if (DRV_ECC_RECOVER_ACTION_REMOVE == recover_action)
            {
                DRV_IF_ERROR_RETURN_WITH_UNLOCK(
                _drv_goldengate_chip_remove_flow_tcam_entry(chip_id, blknum, local_idx),
                p_gg_flow_tcam_mutex[chip_id_offset]);
            }
            else if (DRV_ECC_RECOVER_ACTION_OVERWRITE == recover_action)
            {
                DRV_IF_ERROR_RETURN_WITH_UNLOCK(
                _drv_goldengate_chip_write_flow_tcam_data_mask(chip_id, blknum, local_idx,
                tbl_entry.data_entry, tbl_entry.mask_entry),
                p_gg_flow_tcam_mutex[chip_id_offset]);
            }

            FLOW_TCAM_UNLOCK(chip_id_offset);
        }
        else
        {
            if(is_lpm_tcam_ip)
            {
                LPM_IP_TCAM_LOCK(chip_id_offset);

                DRV_IF_ERROR_RETURN_WITH_UNLOCK(
                _drv_goldengate_chip_read_lpm_tcam_ip_entry(chip_id, local_idx,
                mask + entry_idx*DRV_LPM_WORDS_PER_ENTRY,
                DRV_INT_LPM_TCAM_RECORD_MASK),
                p_gg_lpm_ip_tcam_mutex[chip_id_offset]);

                DRV_IF_ERROR_RETURN_WITH_UNLOCK(
                _drv_goldengate_chip_read_lpm_tcam_ip_entry(chip_id, local_idx,
                data + entry_idx*DRV_LPM_WORDS_PER_ENTRY,
                DRV_INT_LPM_TCAM_RECORD_DATA),
                p_gg_lpm_ip_tcam_mutex[chip_id_offset]);

                DRV_IF_ERROR_RETURN_WITH_UNLOCK(
                _drv_goldengate_chip_convert_tcam_dump_content(chip_id,
                DRV_LPM_KEY_BYTES_PER_ENTRY*8,
                data + entry_idx*DRV_LPM_WORDS_PER_ENTRY,
                mask + entry_idx*DRV_LPM_WORDS_PER_ENTRY, &empty),
                p_gg_lpm_ip_tcam_mutex[chip_id_offset]);

                tbl_entry.data_entry = empty ? NULL : data
                                       + entry_idx*DRV_LPM_WORDS_PER_ENTRY;
                tbl_entry.mask_entry = empty ? NULL : mask
                                       + entry_idx*DRV_LPM_WORDS_PER_ENTRY;
                drv_goldengate_ecc_recover_tcam(chip_id, DRV_ECC_MEM_TCAM_KEY7,
                                     local_idx, &tbl_entry, &recover_action);

                if (DRV_ECC_RECOVER_ACTION_REMOVE == recover_action)
                {
                    DRV_IF_ERROR_RETURN_WITH_UNLOCK(
                    _drv_goldengate_chip_remove_lpm_tcam_ip_entry(chip_id, local_idx),
                    p_gg_lpm_ip_tcam_mutex[chip_id_offset]);
                }
                else if (DRV_ECC_RECOVER_ACTION_OVERWRITE == recover_action)
                {
                    DRV_IF_ERROR_RETURN_WITH_UNLOCK(
                    _drv_goldengate_chip_write_lpm_tcam_ip_data_mask(chip_id, local_idx,
                    tbl_entry.data_entry, tbl_entry.mask_entry),
                    p_gg_lpm_ip_tcam_mutex[chip_id_offset]);
                }

                LPM_IP_TCAM_UNLOCK(chip_id_offset);
            }
            else
            {
                LPM_NAT_TCAM_LOCK(chip_id_offset);

                DRV_IF_ERROR_RETURN_WITH_UNLOCK(
                _drv_goldengate_chip_read_lpm_tcam_nat_entry(chip_id, local_idx,
                mask + entry_idx*DRV_WORDS_PER_ENTRY*2,
                DRV_INT_LPM_TCAM_RECORD_MASK, 2),
                p_gg_lpm_nat_tcam_mutex[chip_id_offset]);

                DRV_IF_ERROR_RETURN_WITH_UNLOCK(
                _drv_goldengate_chip_read_lpm_tcam_nat_entry(chip_id, local_idx,
                data + entry_idx*DRV_WORDS_PER_ENTRY*2,
                DRV_INT_LPM_TCAM_RECORD_DATA, 2),
                p_gg_lpm_nat_tcam_mutex[chip_id_offset]);

                DRV_IF_ERROR_RETURN_WITH_UNLOCK(
                _drv_goldengate_chip_convert_tcam_dump_content(chip_id,
                DRV_BYTES_PER_ENTRY*8*2, data + entry_idx*DRV_WORDS_PER_ENTRY*2,
                mask + entry_idx*DRV_WORDS_PER_ENTRY*2,&empty),
                p_gg_lpm_nat_tcam_mutex[chip_id_offset]);

                tbl_entry.data_entry = empty ? NULL : data
                                               + entry_idx*DRV_WORDS_PER_ENTRY*2;
                tbl_entry.mask_entry = empty ? NULL : mask
                                               + entry_idx*DRV_WORDS_PER_ENTRY*2;
                drv_goldengate_ecc_recover_tcam(chip_id, DRV_ECC_MEM_TCAM_KEY8,
                                     local_idx, &tbl_entry, &recover_action);

                if (DRV_ECC_RECOVER_ACTION_REMOVE == recover_action)
                {
                    DRV_IF_ERROR_RETURN_WITH_UNLOCK(
                    _drv_goldengate_chip_remove_lpm_tcam_nat_entry(chip_id, local_idx),
                    p_gg_lpm_nat_tcam_mutex[chip_id_offset]);
                }
                else if (DRV_ECC_RECOVER_ACTION_OVERWRITE == recover_action)
                {
                    DRV_IF_ERROR_RETURN_WITH_UNLOCK(
                    _drv_goldengate_chip_write_lpm_tcam_nat_data_mask(chip_id, local_idx,
                    tbl_entry.data_entry, tbl_entry.mask_entry),
                    p_gg_lpm_nat_tcam_mutex[chip_id_offset]);
                }

                LPM_NAT_TCAM_UNLOCK(chip_id_offset);
            }
        }

        local_idx++;
    }

    return DRV_E_NONE;
}

/**
 @brief remove tcam entry interface (include operate model and real tcam)
*/
int32
drv_goldengate_chip_tcam_tbl_remove(uint8 chip_id_offset, tbls_id_t tbl_id, uint32 index)
{
     /*uint32 tcam_asic_data_base = 0, tcam_asic_mask_base = 0;*/
    uint32 entry_idx = 0, entry_num_each_idx = 0;
     /*uint32 hw_database_addr = 0, key_size = 0;*/
    uint32 key_size = TCAM_KEY_SIZE(tbl_id);
     /*uint32 index_by_tcam = 0;*/
    uint8 empty = 0;
    uint8 chip_id = chip_id_offset + drv_gg_init_chip_info.drv_init_chipid_base;
    bool is_lpm_tcam = FALSE, is_lpm_tcam_ip = FALSE;
     /*uint32 max_int_tcam_data_base_tmp;*/
     /*uint32 max_int_lpm_tcam_data_base_tmp;*/
    uint32 blknum = 0, local_idx = 0, is_sec_half = 0;
    uint32 try_cnt = 0;
    DRV_CHIP_ID_VALID_CHECK(chip_id);

     /*DRV_DBG_INFO("RemoveTcam Table(%d), Index:%d\n", tbl_id, index);*/

    if ((!drv_goldengate_table_is_tcam_key(tbl_id))
        && (!drv_goldengate_table_is_lpm_tcam_key(tbl_id)))
    {
        DRV_DBG_INFO("@@ ERROR! When read operation, INVALID Tcam key TblID! TblID: %d, file:%s line:%d function:%s\n",
                      tbl_id, __FILE__, __LINE__, __FUNCTION__);
        return DRV_E_INVALID_TBL;
    }

    if (TABLE_MAX_INDEX(tbl_id) <= index)
    {
        DRV_DBG_INFO("ERROR (drv_goldengate_chip_tcam_tbl_read): chip-0x%x, tbl-%d, index-0x%x exceeds the max_index-0x%x.\n",
                        chip_id, tbl_id, index, TABLE_MAX_INDEX(tbl_id));
        return DRV_E_EXCEED_MAX_SIZE;
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
            local_idx = ((offset / entry_num_each_idx) +  index)*entry_num_each_idx;

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
            is_lpm_tcam_ip = FALSE;
        } /*SDK Modify }*/
    }


    for (entry_idx = 0; entry_idx < entry_num_each_idx; ++entry_idx)
    {
        /* remove tcam entry on emulation board */
        if (!is_lpm_tcam)
        {
            FLOW_TCAM_LOCK(chip_id_offset);

            DRV_IF_ERROR_RETURN_WITH_UNLOCK(
            _drv_goldengate_chip_remove_flow_tcam_entry(chip_id, blknum, local_idx),
            p_gg_flow_tcam_mutex[chip_id_offset]);

            FLOW_TCAM_UNLOCK(chip_id_offset);
        }
        else
        {
            uint32 data_tmp[DRV_LPM_WORDS_PER_ENTRY*4] = {0};
            uint32 mask_tmp[DRV_LPM_WORDS_PER_ENTRY*4] = {0};
            uint32 *p_r_data_tmp = data_tmp;
            uint32 *p_r_mask_tmp = mask_tmp;
            uint8 slice_id = 0;
            uint8 not_match = 0;
            uint32 word = 0;

            while (1)
            {
                not_match = 0;
                empty = 0;

                if (is_lpm_tcam_ip)
                {
                    LPM_IP_TCAM_LOCK(chip_id_offset);

                    DRV_IF_ERROR_RETURN_WITH_UNLOCK(
                    _drv_goldengate_chip_remove_lpm_tcam_ip_entry(chip_id, local_idx),
                    p_gg_lpm_ip_tcam_mutex[chip_id_offset]);

                    LPM_IP_TCAM_UNLOCK(chip_id_offset);
                    word = DRV_LPM_WORDS_PER_ENTRY;
                }
                else
                {
                    LPM_NAT_TCAM_LOCK(chip_id_offset);

                    DRV_IF_ERROR_RETURN_WITH_UNLOCK(
                    _drv_goldengate_chip_remove_lpm_tcam_nat_entry(chip_id, local_idx),
                    p_gg_lpm_nat_tcam_mutex[chip_id_offset]);

                    LPM_NAT_TCAM_UNLOCK(chip_id_offset);
                    word = DRV_LPM_WORDS_PER_ENTRY*2;
                }

                /*read tcam for confirm write correct*/
                if (is_lpm_tcam_ip)
                {
                    LPM_IP_TCAM_LOCK(chip_id_offset);

                    DRV_IF_ERROR_RETURN_WITH_UNLOCK(
                    _drv_goldengate_chip_read_lpm_tcam_ip_entry(chip_id, local_idx,
                    p_r_mask_tmp, DRV_INT_LPM_TCAM_RECORD_MASK),
                    p_gg_lpm_ip_tcam_mutex[chip_id_offset]);

                    DRV_IF_ERROR_RETURN_WITH_UNLOCK(
                    _drv_goldengate_chip_read_lpm_tcam_ip_entry(chip_id, local_idx,
                    p_r_data_tmp, DRV_INT_LPM_TCAM_RECORD_DATA),
                    p_gg_lpm_ip_tcam_mutex[chip_id_offset]);

                    DRV_IF_ERROR_RETURN_WITH_UNLOCK(
                    _drv_goldengate_chip_convert_tcam_dump_content(chip_id,
                    DRV_LPM_KEY_BYTES_PER_ENTRY*8, p_r_data_tmp, p_r_mask_tmp,
                    &empty), p_gg_lpm_ip_tcam_mutex[chip_id_offset]);

                    LPM_IP_TCAM_UNLOCK(chip_id_offset);

                    if (_drv_goldengate_chip_tcam_remove_check_match(p_r_data_tmp, p_r_mask_tmp, word) == TRUE)
                    {
                        /*remove tcam success*/
                        break;
                    }
                }
                else
                {
                    for (slice_id = 0; slice_id < 2 ; slice_id++)
                    {
                        LPM_NAT_TCAM_LOCK(chip_id_offset);

                        DRV_IF_ERROR_RETURN_WITH_UNLOCK(
                        _drv_goldengate_chip_read_lpm_tcam_nat_entry(chip_id, local_idx,
                        p_r_mask_tmp, DRV_INT_LPM_TCAM_RECORD_MASK, slice_id),
                        p_gg_lpm_nat_tcam_mutex[chip_id_offset]);

                        DRV_IF_ERROR_RETURN_WITH_UNLOCK(
                        _drv_goldengate_chip_read_lpm_tcam_nat_entry(chip_id, local_idx,
                        p_r_data_tmp, DRV_INT_LPM_TCAM_RECORD_DATA, slice_id),
                        p_gg_lpm_nat_tcam_mutex[chip_id_offset]);

                        DRV_IF_ERROR_RETURN_WITH_UNLOCK(
                        _drv_goldengate_chip_convert_tcam_dump_content(chip_id,
                        DRV_BYTES_PER_ENTRY*8*2, p_r_data_tmp, p_r_mask_tmp,
                        &empty), p_gg_lpm_nat_tcam_mutex[chip_id_offset]);

                        LPM_NAT_TCAM_UNLOCK(chip_id_offset);

                        if (_drv_goldengate_chip_tcam_remove_check_match(p_r_data_tmp, p_r_mask_tmp, word) == FALSE)
                        {
                            not_match = 1;
                            break;
                        }
                    }

                    if (not_match == 0)
                    {
                        /*remove tcam success*/
                        break;
                    }
                }

                try_cnt++;

                if (try_cnt > 1000)
                {
                    DRV_DBG_INFO("RM_LPM_NOT_MATCH_CNT:%d, tbl_id:%d, index:%d!!!\n", try_cnt, tbl_id, index);
                    return DRV_E_TIME_OUT;
                }

            }

        }

        local_idx++;
    }



    return DRV_E_NONE;
}



#define DRV_FIB_ACC_SLEEP_TIME    1

STATIC void
_drv_goldengate_chip_fib_acc_store(uint8 chip_id, uint32* p_fib_acc_cpu_req, uint32* p_fib_acc_cpu_rlt)
{
    uint8       conflict = 0;
    uint8       hit = 0;
    uint8       req_type = 0;
    uint8       overwrite_en = 0;
    uint32      key_idx = 0;
    tbl_entry_t tbl_entry;

    overwrite_en = GetFibHashKeyCpuReq(V, cpuKeyPortDelMode_f, p_fib_acc_cpu_req);
    req_type = GetFibHashKeyCpuReq(V, hashKeyCpuReqType_f, p_fib_acc_cpu_req);
    conflict = GetFibHashKeyCpuResult(V, conflict_f, p_fib_acc_cpu_rlt);
    hit      = GetFibHashKeyCpuResult(V, hashCpuKeyHit_f, p_fib_acc_cpu_rlt);

    if (((1 == req_type) && (0 == conflict))      /* DRV_FIB_ACC_WRITE_MAC_BY_KEY */
       || ((10 == req_type) && ((0 == conflict)   /* DRV_FIB_ACC_WRITE_FIB0_BY_KEY */
       || ((1 == overwrite_en) && (1 == hit))))
       || ((3 == req_type) && (1 == hit))         /* DRV_FIB_ACC_DEL_MAC_BY_KEY */
       || (0 == req_type)                         /* DRV_FIB_ACC_WRITE_MAC_BY_IDX */
       || (9 == req_type))                        /* DRV_FIB_ACC_WRITE_FIB0_BY_IDX */
    {
        key_idx = GetFibHashKeyCpuResult(V, hashCpuLuIndex_f, p_fib_acc_cpu_rlt);
        if (key_idx < FIB_HOST0_CAM_NUM)
        {
            return;
        }
        key_idx -= FIB_HOST0_CAM_NUM;
        tbl_entry.data_entry = p_fib_acc_cpu_req;
        tbl_entry.mask_entry = NULL;

        drv_goldengate_ecc_recover_store(chip_id, DsFibHost0MacHashKey_t, key_idx, &tbl_entry);
    }

    return;
}

int32
drv_goldengate_chip_fib_acc_process(uint8 chip_id, uint32* i_fib_acc_cpu_req, uint32* o_fib_acc_cpu_rlt, uint8 is_store)
{
    uint32        cmd   = 0;
    uint16         loop  = 0;
    uint8         count = 0;
    uint8         done  = 0;
    uint8         ecc_en = 0;
     /*-sal_systime_t tv1,tv2;*/

#if 0
    ex_cplusplus_sim_cpu_add_delete(chip_id, i_fib_acc_cpu_req, o_fib_acc_cpu_rlt);
#else
    cmd = DRV_IOW(FibHashKeyCpuReq_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, i_fib_acc_cpu_req));

    cmd = DRV_IOR(FibHashKeyCpuReq_t, DRV_ENTRY_FLAG);

 /*-sal_gettime(&tv1);*/
    for (loop = 0; (loop < 200) && !done; loop++)
    {
        for (count = 0; (count < 150) && !done; count++)
        {
            DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, i_fib_acc_cpu_req));
            done = !GetFibHashKeyCpuReq(V, hashKeyCpuReqValid_f, i_fib_acc_cpu_req);
        }

        if (done)
        {
            break;
        }
        sal_task_sleep(DRV_FIB_ACC_SLEEP_TIME);
        if(loop > 0 && ((GetFibHashKeyCpuReq(V,hashKeyCpuReqType_f, i_fib_acc_cpu_req) == 1) || (GetFibHashKeyCpuReq(V,hashKeyCpuReqType_f, i_fib_acc_cpu_req) == 10)))
        {
            uint32 temp_value = 0;
            uint32 cmd_drain = DRV_IOW(FibEngineDrainEnable2_t, FibEngineDrainEnable2_macDaLkupDrainEnable_f);
            DRV_IOCTL(chip_id, 0, cmd_drain, &temp_value);
            temp_value = 1;
            DRV_IOCTL(chip_id, 0, cmd_drain, &temp_value);
        }
    }

     /*-sal_gettime(&tv2);*/

    if (!done)
    {
        DRV_IF_ERROR_RETURN(DRV_E_TIME_OUT);
    }

    cmd = DRV_IOR(FibHashKeyCpuResult_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, o_fib_acc_cpu_rlt));

    ecc_en = drv_goldengate_ecc_recover_get_enable();

    if (is_store && ecc_en)
    {
        _drv_goldengate_chip_fib_acc_store(chip_id, i_fib_acc_cpu_req, o_fib_acc_cpu_rlt);
    }
#endif

    return DRV_E_NONE;
}

STATIC int32
_drv_goldengate_chip_cpu_acc_get_done(uint8 acc_type, void* data)
{
    uint32 done = 0;
    if (DRV_CPU_LOOKUP_ACC_FIB_HOST1 == acc_type)
    {
        done = GetFibHost1CpuLookupResult(V, cpuLookupReqDone_f, data);
    }
    else if (DRV_CPU_LOOKUP_ACC_USER_ID == acc_type)
    {
        done = GetUserIdCpuLookupResult(V, cpuLookupReqDone_f, data);
    }
    else if (DRV_CPU_LOOKUP_ACC_XC_OAM == acc_type)
    {
        done = GetEgressXcOamCpuLookupResult(V, cpuLookupReqDone_f, data);
    }
    else if (DRV_CPU_LOOKUP_ACC_FLOW_HASH == acc_type)
    {
        done = GetFlowHashCpuLookupResult(V, cpuLookupReqDone_f, data);
    }
    return  done;
}

int32
drv_goldengate_chip_cpu_acc_get_valid(uint8 acc_type, uint8* p_valid)
{
    uint32 valid = 0;
    uint32 cmd = 0;
    uint32 data[MAX_ENTRY_BYTE / 4] = { 0 };

    if (DRV_CPU_LOOKUP_ACC_FIB_HOST1 == acc_type)
    {
        cmd = DRV_IOR(FibHost1CpuLookupReq_t, DRV_ENTRY_FLAG);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(0, 0, cmd, data));
        valid = GetFibHost1CpuLookupReq(V, cpuLookupReqValid_f, data);
    }
    else if (DRV_CPU_LOOKUP_ACC_USER_ID == acc_type)
    {
        cmd = DRV_IOR(UserIdCpuLookupReq_t, DRV_ENTRY_FLAG);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(0, 0, cmd, data));
        valid = GetUserIdCpuLookupReq(V, cpuLookupReqValid_f, data);
    }
    else if (DRV_CPU_LOOKUP_ACC_XC_OAM == acc_type)
    {
        cmd = DRV_IOR(EgressXcOamCpuLookupReq_t, DRV_ENTRY_FLAG);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(0, 0, cmd, data));
        valid = GetEgressXcOamCpuLookupReq(V, cpuLookupReqValid_f, data);
    }
    else if (DRV_CPU_LOOKUP_ACC_FLOW_HASH == acc_type)
    {
        cmd = DRV_IOR(FlowHashCpuLookupReq0_t, DRV_ENTRY_FLAG);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(0, 0, cmd, data));
        valid = GetFlowHashCpuLookupReq0(V, cpuLookupReqValid_f, data);
    }

    *p_valid = valid;

    return  DRV_E_NONE;
}

int32
drv_goldengate_chip_cpu_acc_asic_lkup(uint8 chip_id, uint8 req_type, void * cpu_req, void * cpu_req1, void *cpu_rlt)
{
    uint32 cmdw  = 0;
    uint32 cmdw1 = 0;
    uint32 cmdr  = 0;

    uint8  loop = 0;
    uint8  count= 0;
    uint32 done = 0;


    if (DRV_CPU_LOOKUP_ACC_FIB_HOST1 == req_type)
    {
        cmdw     = DRV_IOW(FibHost1CpuLookupReq_t, DRV_ENTRY_FLAG);
        cmdr     = DRV_IOR(FibHost1CpuLookupResult_t, DRV_ENTRY_FLAG);
    }
    else if (DRV_CPU_LOOKUP_ACC_USER_ID == req_type )
    {
        cmdw     = DRV_IOW(UserIdCpuLookupReq_t, DRV_ENTRY_FLAG);
        cmdr     = DRV_IOR(UserIdCpuLookupResult_t, DRV_ENTRY_FLAG);
    }
    else if (DRV_CPU_LOOKUP_ACC_XC_OAM == req_type )
    {
        cmdw     = DRV_IOW(EgressXcOamCpuLookupReq_t, DRV_ENTRY_FLAG);
        cmdr     = DRV_IOR(EgressXcOamCpuLookupResult_t, DRV_ENTRY_FLAG);
    }
    else if (DRV_CPU_LOOKUP_ACC_FLOW_HASH == req_type )
    {
        cmdw     = DRV_IOW(FlowHashCpuLookupReq0_t, DRV_ENTRY_FLAG);
        cmdw1    = DRV_IOW(FlowHashCpuLookupReq1_t, DRV_ENTRY_FLAG);
        cmdr     = DRV_IOR(FlowHashCpuLookupResult_t, DRV_ENTRY_FLAG);
    }

    if (cmdw1 != 0)
    {
        DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmdw1, cpu_req1));
    }

    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmdw, cpu_req));

    for (loop = 0; (loop < 20) && !done; loop++)
    {
        for (count = 0; (count < 50) && !done; count++)
        {
            DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmdr, cpu_rlt));
            done = _drv_goldengate_chip_cpu_acc_get_done(req_type,  cpu_rlt);
        }
        if (done)
        {
            break;
        }
        sal_task_sleep(1);
    }

    if (!done)
    {
        DRV_IF_ERROR_RETURN(DRV_E_CMD_NOT_DONE);
    }

    return DRV_E_NONE;
}

int32
drv_goldengate_chip_ipfix_acc_process(uint8 chip_id, void* i_ipfix_req, void* o_ipfix_result)
{

    uint8                      loop  = 0;
    uint8                      count = 0;
    uint32                     done  = 0;
    uint32 cmdw = DRV_IOW(IpfixHashAdCpuReq_t, DRV_ENTRY_FLAG);
    uint32 cmdr = DRV_IOR(IpfixHashAdCpuResult_t, DRV_ENTRY_FLAG);

    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmdw, i_ipfix_req));
    for (loop = 0; (loop < 20) && !done; loop++)
    {
        for (count = 0; (count < 50) && !done; count++)
        {
            DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmdr, o_ipfix_result));
            done = GetIpfixHashAdCpuResult(V, operationDone_f, o_ipfix_result);
        }
        if (done)
        {
            break;
        }
        sal_task_sleep(1);
    }

    if (!done)
    {
        DRV_IF_ERROR_RETURN(DRV_E_CMD_NOT_DONE);
    }

    /*for ipfix acc sometime cannot return data following done is set, do one more io*/
    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmdr, o_ipfix_result));

    return DRV_E_NONE;
}


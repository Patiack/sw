/**
 @file drv_model_dma.c

 @date 2010-07-23

 @version v5.1

 The file contains all driver I/O interface realization
*/
#if (SDK_WORK_PLATFORM == 1)
#include "goldengate/include/drv_model_dma.h"

static drv_model_dma_info_t drv_gg_model_dma_info[MAX_LOCAL_CHIP_NUM][DRV_MODEL_DMA_MAX_NUM];

/**
 @brief sram table model set entry write bit
*/
int32
drv_goldengate_model_dma_add_fifo(uint8 chip_id_offset, drv_model_dma_info_t * dma_info)
{
    uint16 i = 0;

    sal_printf("Add DMA info!!!\n");

     /* update DMA Stats entry, add by yaof*/
    for ( i = 0 ; i < DRV_MODEL_DMA_MAX_NUM ; i++)
    {
        if (drv_gg_model_dma_info[chip_id_offset][i].valid == 1 && drv_gg_model_dma_info[chip_id_offset][i].u.stats.statsPtr == dma_info->u.stats.statsPtr && drv_gg_model_dma_info[chip_id_offset][i].dma_mode == DRV_MODEL_DMA_MODE_STATS)
        {
            drv_gg_model_dma_info[chip_id_offset][i].u.stats.packetCount = drv_gg_model_dma_info[chip_id_offset][i].u.stats.packetCount + dma_info->u.stats.packetCount;
            drv_gg_model_dma_info[chip_id_offset][i].u.stats.byteCount= drv_gg_model_dma_info[chip_id_offset][i].u.stats.byteCount + dma_info->u.stats.byteCount;
            return DRV_E_NONE;
        }
    }
     /* end, add by yaof*/
    for ( i = 0 ; i < DRV_MODEL_DMA_MAX_NUM ; i++)
    {
        if (drv_gg_model_dma_info[chip_id_offset][i].valid == 0)
        {
            sal_memcpy(&drv_gg_model_dma_info[chip_id_offset][i], dma_info, sizeof(drv_model_dma_info_t));
            return DRV_E_NONE;
        }
    }

    return DRV_E_EXCEED_MAX_SIZE;
}

int32
drv_goldengate_model_dma_del_fifo(uint8 chip_id_offset, drv_model_dma_info_t * dma_info)
{
    uint16 i = 0;
    uint32 rlt = 0;

    sal_printf("Remove DMA info!!!\n");

    for ( i = 0 ; i < DRV_MODEL_DMA_MAX_NUM ; i++)
    {
        if (drv_gg_model_dma_info[chip_id_offset][i].valid == 1)
        {
            rlt = sal_memcmp(&drv_gg_model_dma_info[chip_id_offset][i], dma_info, sizeof(drv_model_dma_info_t));
            if (0 == rlt)
            {
                sal_memset(&drv_gg_model_dma_info[chip_id_offset][i],0,sizeof(drv_model_dma_info_t));
                return DRV_E_NONE;
            }
        }
    }

    return DRV_E_NOT_FOUND;
}


int32
drv_goldengate_model_dma_get_fifo(uint8 chip_id_offset, uint16 idx, drv_model_dma_info_t * dma_info)
{
    if (chip_id_offset >= MAX_LOCAL_CHIP_NUM )
    {
        return DRV_E_INVALID_ADDR;
    }
    if (idx >= DRV_MODEL_DMA_MAX_NUM )
    {
        return DRV_E_INVALID_ADDR;
    }

    sal_memcpy(dma_info, &drv_gg_model_dma_info[chip_id_offset][idx], sizeof(drv_model_dma_info_t));

    return DRV_E_NONE;
}

int32
drv_goldengate_model_dma_is_not_full(uint8 chip_id_offset)
{
    uint16 i = 0;
    if (chip_id_offset >= MAX_LOCAL_CHIP_NUM )
    {
        return 0;
    }

    for ( i = 0 ; i < DRV_MODEL_DMA_MAX_NUM ; i++)
    {
        if (drv_gg_model_dma_info[chip_id_offset][i].valid != 1)
        {
            return 1;
        }
    }

    return 0;
}

int32
drv_goldengate_model_dma_set_fifo_full(uint8 chip_id_offset)
{
    uint16 i = 0;
    if (chip_id_offset >= MAX_LOCAL_CHIP_NUM )
    {
        return 0;
    }

    for ( i = 0 ; i < DRV_MODEL_DMA_MAX_NUM ; i++)
    {
        drv_gg_model_dma_info[chip_id_offset][i].valid = 1;
    }
    return 1;
}

#endif




/**
 @file sys_greatbelt_dma_priv.h

 @date 2012-11-30

 @version v2.0

*/
#ifndef _SYS_GREATBELT_DMA_DEBUG_H
#define _SYS_GREATBELT_DMA_DEBUG_H
#ifdef __cplusplus
extern "C" {
#endif

/***************************************************************
 *
 * Header Files
 *
 ***************************************************************/
#include "ctc_debug.h"
#include "ctc_const.h"
#include "greatbelt/include/drv_io.h"



extern int32
sys_greatbelt_dma_show_state(uint8 lchip);
extern int32
sys_greatbelt_dma_show_desc(uint8 lchip, sys_dma_func_type_t type, uint8 chan_id);
/***************************************************************
 *
 *  Functions
 *
 ***************************************************************/

#ifdef __cplusplus
}
#endif

#endif


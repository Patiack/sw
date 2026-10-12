/**
 @file sys_goldengate_monitor.h

 @date 2009-10-19

 @version v2.0

 The file define APIs and types use in sys layer
*/
#ifndef _SYS_GOLDENGATE_MONITOR_H
#define _SYS_GOLDENGATE_MONITOR_H
#ifdef __cplusplus
extern "C" {
#endif
/***************************************************************
 *
 * Header Files
 *
 ***************************************************************/
#include "sal.h"
#include "ctc_const.h"
#include "ctc_vector.h"

#include "ctc_monitor.h"
/***************************************************************
 *
 *  Defines and Macros
 *
 ***************************************************************/
#define SYS_MONITOR_DBG_OUT(level, FMT, ...) \
    do { \
        CTC_DEBUG_OUT(monitor, monitor, MONITOR_SYS, level, FMT, ##__VA_ARGS__); \
    } while (0)

/***************************************************************
 *
 *  Functions
 *
 ***************************************************************/
extern int32
sys_goldengate_monitor_init(uint8 lchip, void* p_global_cfg);

/**
 @brief De-Initialize monitor module
*/
extern int32
sys_goldengate_monitor_deinit(uint8 lchip);

extern int32
sys_goldengate_monitor_set_config(uint8 lchip, ctc_monitor_config_t *p_cfg);

extern int32
sys_goldengate_monitor_get_config(uint8 lchip, ctc_monitor_config_t *p_cfg);

extern int32
sys_goldengate_monitor_get_watermark(uint8 lchip, ctc_monitor_watermark_t *p_watermark);

extern int32
sys_goldengate_monitor_clear_watermark(uint8 lchip, ctc_monitor_watermark_t *p_watermark);

extern int32
sys_goldengate_monitor_set_global_config(uint8 lchip, ctc_monitor_glb_cfg_t* p_cfg);

extern int32
sys_goldengate_monitor_register_cb(uint8 lchip, ctc_monitor_fn_t callback, void* userdata);

extern int32
sys_goldengate_monitor_get_cb(uint8 lchip, void **cb, void** user_data);

#ifdef __cplusplus
}
#endif

#endif



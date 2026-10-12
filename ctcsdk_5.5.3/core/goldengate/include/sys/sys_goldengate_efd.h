/**
 @file sys_goldengate_efd.h

 @date 2014-10-28

 @version v3.0

Macro, data structure for system common operations

*/

#ifndef _SYS_GOLDENGATE_EFD
#define _SYS_GOLDENGATE_EFD
#ifdef __cplusplus
extern "C" {
#endif

#include "sal.h"
#include "ctc_efd.h"

/***************************************************************
 *
 *  Defines and Macros
 *
 ***************************************************************/

    
/***************************************************************
 *
 *  Functions
 *
 ***************************************************************/

extern int32
sys_goldengate_efd_set_global_ctl(uint8 lchip, ctc_efd_global_control_type_t type, void* value);

extern int32
sys_goldengate_efd_get_global_ctl(uint8 lchip, ctc_efd_global_control_type_t type, void* value);

extern int32
sys_goldengate_efd_register_cb(uint8 lchip, ctc_efd_fn_t callback, void* userdata);

extern void
sys_goldengate_efd_sync_data(uint8 lchip, ctc_efd_data_t* info);

extern int32
sys_goldengate_efd_init(uint8 lchip);

/**
 @brief De-Initialize efd module
*/
extern int32
sys_goldengate_efd_deinit(uint8 lchip);

#ifdef __cplusplus
}
#endif

#endif


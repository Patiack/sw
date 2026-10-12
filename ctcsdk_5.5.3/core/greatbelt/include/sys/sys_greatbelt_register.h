/**
 @file sys_greatbelt_register.h

 @date 2009-11-3

 @version v2.0

Macro, data structure for system common operations

*/

#ifndef _SYS_GREATBELT_REGISTER
#define _SYS_GREATBELT_REGISTER
#ifdef __cplusplus
extern "C" {
#endif

#include "sal.h"
#include "ctc_register.h"
#include "ctc_port.h"

#define SYS_GREATBELT_DSNH_INTERNAL_BASE 0x3FFF
#define SYS_GREATBELT_DSNH_INTERNAL_SHIFT   2

struct sys_register_master_s
{
    uint32 chip_capability[CTC_GLOBAL_CAPABILITY_MAX];
    uint8 xgpon_en;
};
typedef struct sys_register_master_s sys_register_master_t;

extern int32
sys_greatbelt_global_ctl_set(uint8 lchip, ctc_global_control_type_t type, void* value);

extern int32
sys_greatbelt_global_ctl_get(uint8 lchip, ctc_global_control_type_t type, void* value);

extern int32
sys_greatbelt_lkup_ttl_index(uint8 lchip, uint8 ttl, uint32* ttl_index);

extern int32
sys_greatbelt_lkup_ttl(uint8 lchip, uint8 ttl_index, uint32* ttl);

extern int32
sys_greatbelt_register_init(uint8 lchip);

/**
 @brief De-Initialize register module
*/
extern int32
sys_greatbelt_register_deinit(uint8 lchip);

extern int32
sys_greatbelt_device_feature_init(uint8 lchip);

extern int32
sys_greatbelt_global_set_chip_capability(uint8 lchip, ctc_global_capability_type_t type, uint32 value);

extern int32
sys_greatbelt_global_set_xgpon_en(uint8 lchip, uint8 enable);

extern int32
sys_greatbelt_global_get_xgpon_en(uint8 lchip, uint8* enable);

#ifdef __cplusplus
}
#endif

#endif


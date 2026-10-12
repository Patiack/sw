/**
 @file sys_goldengate_register.h

 @date 2009-11-3

 @version v2.0

Macro, data structure for system common operations

*/

#ifndef _SYS_GOLDENGATE_REGISTER
#define _SYS_GOLDENGATE_REGISTER
#ifdef __cplusplus
extern "C" {
#endif

#include "sal.h"
#include "ctc_register.h"
#include "ctc_port.h"

typedef int32 (* sys_wb_sync_fn)(uint8 lchip);

struct sys_register_master_s
{
    sys_wb_sync_fn wb_sync_cb[CTC_FEATURE_MAX];
    uint32 chip_capability[CTC_GLOBAL_CAPABILITY_MAX];
};
typedef struct sys_register_master_s sys_register_master_t;

extern int32
sys_goldengate_global_ctl_set(uint8 lchip, ctc_global_control_type_t type, void* value);

extern int32
sys_goldengate_global_ctl_get(uint8 lchip, ctc_global_control_type_t type, void* value);

extern int32
sys_goldengate_lkup_ttl_index(uint8 lchip, uint8 ttl, uint32* ttl_index);
extern int32
sys_goldengate_lkup_adjust_len_index(uint8 lchip, uint8 adjust_len, uint8* len_index);

extern int32
sys_goldengate_lkup_ttl(uint8 lchip, uint8 ttl_index, uint32* ttl);

extern int32
sys_goldengate_register_init(uint8 lchip);

/**
 @brief De-Initialize register module
*/
extern int32
sys_goldengate_register_deinit(uint8 lchip);

/* mode 0: txThrd0=3, txThrd1=4; mode 1: txThrd0=5, txThrd1=11; if cut through enable, default use mode 1 */
/* Mode 1 can avoid CRC, but will increase latency. Mode 0 can reduce latency, but lead to CRC. */
extern int32
sys_goldengate_net_tx_threshold_cfg(uint8 lchip, uint8 thrd0, uint8 thrd1);

extern int32
sys_goldengate_wb_sync_register_cb(uint8 lchip, uint8 module, sys_wb_sync_fn fn);

extern int32
sys_goldengate_global_set_chip_capability(uint8 lchip, ctc_global_capability_type_t type, uint32 value);

extern int32
sys_goldengate_global_set_oversub_pdu(uint8 lchip, mac_addr_t macda, mac_addr_t macda_mask, uint16 eth_type, uint16 eth_type_mask, uint8 with_vlan);

extern int32
sys_goldengate_global_check_acl_memory(uint8 lchip, ctc_direction_t dir, uint32 value);

#ifdef __cplusplus
}
#endif

#endif


/**
 @file sys_greatbelt_bpe.h

 @date 2013-05-28

 @version v2.0

 The file define APIs and types use in sys layer
*/
#ifndef _SYS_GREATBELT_BPE_H
#define _SYS_GREATBELT_BPE_H
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

#include "ctc_bpe.h"


#define SYS_INTLK_PORT_BASE_SEGMENT   24
#define SYS_INTLK_PORT_BASE_PKT   76

#define SYS_INTLK_MAX_LOCAL_PORT_SEGMENT 24
#define SYS_INTLK_MAX_LOCAL_PORT_PKT 52

#define SYS_INTLK_INTERNAL_PORT_BASE 76

#define SYS_BPE_DBG_OUT(level, FMT, ...) \
    do { \
        CTC_DEBUG_OUT(bpe, bpe, BPE_SYS, level, FMT, ##__VA_ARGS__); \
    } while (0);


struct sys_bpe_master_s
{
    uint8 intlk_mode;   /* refer to ctc_bpe_intlk_mode_t */
    uint8 extender_en[SYS_GB_MAX_PHY_PORT];
    uint8 extender_mode[SYS_GB_MAX_PHY_PORT];
    uint8 intlk_en;
    uint8 rsv[2];
};
typedef struct sys_bpe_master_s sys_bpe_master_t;

extern int32
sys_greatbelt_bpe_init(uint8 lchip, void* bpe_global_cfg);

/**
 @brief De-Initialize bpe module
*/
extern int32
sys_greatbelt_bpe_deinit(uint8 lchip);
extern int32
sys_greatbelt_bpe_set_intlk_en(uint8 lchip, bool enable);
extern int32
sys_greatbelt_bpe_get_intlk_en(uint8 lchip, bool* enable);
extern int32
sys_greatbelt_bpe_set_port_extender(uint8 lchip, uint16 gport, ctc_bpe_extender_t* p_extender);
extern int32
sys_greatbelt_bpe_get_port_extender(uint8 lchip, uint16 gport, bool* enable);
#ifdef __cplusplus
}
#endif

#endif


/**
 @file sys_goldengate_fcoe.h

 @date 2015-10-10

 @version v2.0

*/
#ifndef _SYS_GOLDENGATE_FCOE_H
#define _SYS_GOLDENGATE_FCOE_H
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
#include "ctc_hash.h"

#include "ctc_fcoe.h"
/***************************************************************
 *
 *  Defines and Macros
 *
 ***************************************************************/


#define SYS_FCOE_DBG_OUT(level, FMT, ...)                              \
    {                                                                       \
        CTC_DEBUG_OUT(fcoe, fcoe, FCOE_SYS, level, FMT, ##__VA_ARGS__);  \
    }

#define SYS_FCOE_INIT_CHECK(lchip)                                 \
    do {                                                            \
        SYS_LCHIP_CHECK_ACTIVE(lchip);                                  \
        if (NULL == p_gg_fcoe_master[lchip])                          \
        {                                                          \
            return CTC_E_NOT_INIT;                                 \
        }                                                          \
    } while(0)


struct sys_fcoe_master_s
{
    uint32 default_base;
    uint32 rpf_default_base;
};
typedef struct sys_fcoe_master_s sys_fcoe_master_t;

struct sys_fcoe_traverse_s
{
    void*   user_data;
    uint32  start_index;         /**< If it is the first traverse, it is equal to 0, else it is equal to the last next_traverse_index */
    uint32  next_traverse_index; /**< [out] return index of the next traverse */
    uint8   is_end;              /**< [out] if is_end == 1, indicate the end of traverse */
    uint16  entry_num;           /**< indicate entry number for one traverse function */
};
typedef struct sys_fcoe_traverse_s sys_fcoe_traverse_t;

/***************************************************************
 *
 *  Functions
 *
 ***************************************************************/


extern int32
sys_goldengate_fcoe_init(uint8 lchip, void* p_fcoe_global_cfg);

/**
 @brief De-Initialize fcoe module
*/
extern int32
sys_goldengate_fcoe_deinit(uint8 lchip);

extern int32
sys_goldengate_fcoe_add_route(uint8 lchip, ctc_fcoe_route_t* p_fcoe_route);

extern int32
sys_goldengate_fcoe_remove_route(uint8 lchip, ctc_fcoe_route_t* p_fcoe_route);

extern int32
sys_goldengate_fcoe_dump_entry(uint8 lchip, void* user_data);

typedef void (*sys_fcoe_fn_t)( ctc_fcoe_route_t* info, void* userdata);
typedef int32 (* sys_fcoe_traverse_fn)(ctc_fcoe_route_t* p_data, void* user_data);

#ifdef __cplusplus
}
#endif

#endif


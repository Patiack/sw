/**
 @file sys_greatbelt_rpf_spool.h

 @date 2012-09-18

 @version v2.0

*/
#ifndef _SYS_GREATBELT_RPF_SPOOL_H
#define _SYS_GREATBELT_RPF_SPOOL_H
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

/***************************************************************
 *
 *  Defines and Macros
 *
 ***************************************************************/
enum sys_rpf_usage_type_e
{
    SYS_RPF_USAGE_TYPE_IPUC,
    SYS_RPF_USAGE_TYPE_IPMC,
    SYS_RPF_USAGE_TYPE_DEFAULT,
    SYS_RPF_USAGE_TYPE_NUM
};
typedef enum sys_rpf_usage_type_e sys_rpf_usage_type_t;

#define SYS_RPF_DBG_OUT(level, FMT, ...)                              \
    {                                                                       \
        CTC_DEBUG_OUT(ip, rpf, RPF_SYS, level, FMT, ##__VA_ARGS__);  \
    }

#define SYS_RPF_INVALID_PROFILE_ID        0
#define SYS_RPF_PROFILE_DEFAULT_ENTRY     0x3F
#define SYS_RPF_PROFILE_START_INDEX       1

enum sys_rpf_chk_type_e
{
    SYS_RPF_CHK_TYPE_STRICK,
    SYS_RPF_CHK_TYPE_LOOSE,
    SYS_RPF_CHK_TYPE_NUM
};
typedef enum sys_rpf_chk_type_e sys_rpf_chk_type_t;

enum sys_rpf_strick_type_e
{
    SYS_RPF_STRICK_TYPE_RPFID_MATCH,
    SYS_RPF_STRICK_TYPE_PORT_MATCH,
    SYS_RPF_STRICK_TYPE_RPFID_AND_PORT_MATCH,
    SYS_RPF_STRICK_TYPE_NUM
};
typedef enum sys_rpf_strick_type_e sys_rpf_strick_type_t;

enum sys_rpf_chk_mode_e
{
    SYS_RPF_CHK_MODE_PROFILE,
    SYS_RPF_CHK_MODE_IFID,
    SYS_RPF_CHK_MODE_INVALID,
    SYS_RPF_CHK_MODE_NUM
};
typedef enum sys_rpf_chk_mode_e sys_rpf_chk_mode_t;

enum sys_rpf_profile_resv_type_e
{
    SYS_RPF_PROFILE_RESV_TYPE_INVALID,
    SYS_RPF_PROFILE_RESV_TYPE_DEFAULT,
    SYS_RPF_PROFILE_RESV_TYPE_NUM
};
typedef enum sys_rpf_profile_resv_type_e sys_rpf_profile_resv_type_t;

struct sys_rpf_intf_s
{
    uint16 rpf_intf[SYS_GB_MAX_IPMC_RPF_IF];
    uint8  rpf_intf_valid[SYS_GB_MAX_IPMC_RPF_IF];
};
typedef struct sys_rpf_intf_s sys_rpf_intf_t;

struct sys_rpf_profile_s
{
    uint16  rpf_intf[SYS_GB_MAX_IPMC_RPF_IF];
    uint16  rpf_profile_index;
};
typedef struct sys_rpf_profile_s sys_rpf_profile_t;

struct sys_rpf_info_s
{
    sys_rpf_usage_type_t usage;
    sys_rpf_intf_t* intf;
    uint8 profile_index;
    uint8 force_profile;
};
typedef struct sys_rpf_info_s sys_rpf_info_t;

struct sys_rpf_rslt_s
{
    sys_rpf_chk_mode_t mode;
    uint16 id;
};
typedef struct sys_rpf_rslt_s sys_rpf_rslt_t;

/***************************************************************
 *
 *  Functions
 *
 ***************************************************************/

extern int32
sys_greatbelt_rpf_init(uint8 lchip);

extern int32
sys_greatbelt_rpf_deinit(uint8 lchip);

extern int32
sys_greatbelt_rpf_remove_profile(uint8 lchip, uint8 rpf_profile_index);

extern int32
sys_greatbelt_rpf_add_profile(uint8 lchip, sys_rpf_info_t* p_rpf_info, sys_rpf_rslt_t* p_rpf_rslt);

#ifdef __cplusplus
}
#endif

#endif


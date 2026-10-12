#ifndef _CTC_GREATBELT_DKIT_TCAM_H_
#define _CTC_GREATBELT_DKIT_TCAM_H_

#ifdef __cplusplus
extern "C" {
#endif
#include "sal.h"

#define DKITS_TCAM_PER_TYPE_LKP_NUM 4


enum ctc_dkits_tcam_tbl_module_e
{
    CTC_DKITS_TCAM_FUN_TYPE_ACL,
    CTC_DKITS_TCAM_FUN_TYPE_SCL,
    CTC_DKITS_TCAM_FUN_TYPE_IP,
    CTC_DKITS_TCAM_FUN_TYPE_NUM
};
typedef enum ctc_dkits_tcam_tbl_module_e ctc_dkits_tcam_tbl_module_t;

enum dkits_tcam_key_size_e
{
    DKITS_TCAM_SIZE_INVALID,
    DKITS_TCAM_SIZE_MAC,
    DKITS_TCAM_SIZE_IPV4,
    DKITS_TCAM_SIZE_MPLS,
    DKITS_TCAM_SIZE_IPV6,
    DKITS_TCAM_SIZE_MAX
};
typedef enum dkits_tcam_key_size_e dkits_tcam_key_size_t;


extern int32
ctc_greatbelt_dkits_tcam_capture_start(void*);

extern int32
ctc_greatbelt_dkits_tcam_capture_stop(void*);

extern int32
ctc_greatbelt_dkits_tcam_capture_show(void*);

extern int32
ctc_greatbelt_dkits_tcam_scan(void*);

#ifdef __cplusplus
}
#endif

#endif


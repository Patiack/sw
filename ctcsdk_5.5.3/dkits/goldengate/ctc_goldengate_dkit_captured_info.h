#ifndef _CTC_DKIT_GOLDENGATE_CAPTURED_INFO_H_
#define _CTC_DKIT_GOLDENGATE_CAPTURED_INFO_H_


#ifdef __cplusplus
extern "C" {
#endif


#include "sal.h"
#include "ctc_dkit.h"

extern int32
ctc_goldengate_dkit_captured_install_flow(void* p_para);

extern int32
ctc_goldengate_dkit_captured_clear(void* p_para);

extern int32
ctc_goldengate_dkit_captured_path_process(void* p_para);

extern int32
ctc_goldengate_dkit_captured_get_discard_sub_reason(ctc_dkit_discard_para_t* p_para, ctc_dkit_discard_info_t* p_discard_info);


#ifdef __cplusplus
}
#endif

#endif

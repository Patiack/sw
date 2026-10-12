#ifndef _CTC_DKIT_BUS_INFO_H_
#define _CTC_DKIT_BUS_INFO_H_


#ifdef __cplusplus
extern "C" {
#endif


#include "sal.h"
#include "ctc_dkit.h"


extern int32
ctc_goldengate_dkit_bus_get_discard_sub_reason(ctc_dkit_discard_para_t* p_para, ctc_dkit_discard_info_t* p_discard_info);

extern int32
ctc_goldengate_dkit_bus_path_process(uint8 lchip, void* p_para, uint8 detail);

#ifdef __cplusplus
}
#endif

#endif



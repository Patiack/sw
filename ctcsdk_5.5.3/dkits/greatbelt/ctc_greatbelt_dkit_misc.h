#ifndef _CTC_DKIT_GREATBELT_MISC_H
#define _CTC_DKIT_GREATBELT_MISC_H
#ifdef __cplusplus
extern "C" {
#endif

#include "sal.h"
#include "ctc_cli.h"
#include "ctc_dkit.h"

extern int32
ctc_greatbelt_dkit_misc_read_serdes(void* para);

extern int32
ctc_greatbelt_dkit_misc_write_serdes(void* para);

extern int32
ctc_greatbelt_dkit_misc_serdes_ctl(void* p_para);

extern int32
ctc_greatbelt_dkit_misc_integrity_check(uint8 lchip, void*, void*, void*);

extern int32
ctc_greatbelt_dkit_misc_packet_dump(uint8 lchip, void* p_para1, void* p_para2, void* p_para3);

extern int32
ctc_greatbelt_dkit_misc_chip_info(uint8 lchip, sal_file_t p_wf);

#ifdef __cplusplus
}
#endif

#endif



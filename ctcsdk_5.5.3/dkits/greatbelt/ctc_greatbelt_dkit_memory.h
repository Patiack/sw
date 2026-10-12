#ifndef _CTC_DKIT_GREATBELT_MEMORY_H
#define _CTC_DKIT_GREATBELT_MEMORY_H
#ifdef __cplusplus
extern "C" {
#endif

#include "sal.h"
#include "ctc_cli.h"
#include "ctc_dkit.h"


extern int32
drv_greatbelt_dkit_get_tbl_string_by_id(uint8 lchip, tbls_id_t tbl_id, char* name);

extern int32
ctc_greatbelt_dkit_memory_process(void* p_para);

extern int32
ctc_greatbelt_dkit_memory_is_invalid_table(uint32 tbl_id);

extern int32
ctc_greatbelt_dkit_memory_read_table(uint8 lchip, void* p_param);

extern int32
ctc_greatbelt_dkit_memory_write_table(uint8 lchip, void* p_param);

#ifdef __cplusplus
}
#endif

#endif



#ifndef _CTC_GREATBELT_DKIT_DUMP_H_
#define _CTC_GREATBELT_DKIT_DUMP_H_

#ifdef __cplusplus
extern "C" {
#endif
#include "sal.h"

#define CTC_DKITS_FIELD_LEN(p_field)    (p_field->len)    /* Size of fields */

extern int32 ctc_greatbelt_dkits_dump_memory_usage(uint8 lchip, void* p_para);
extern int32 ctc_greatbelt_dkits_dump_cfg_dump(uint8 lchip, void* p_info);
extern int32 ctc_greatbelt_dkits_dump_cfg_decode(uint8 lchip, void* p_para);
extern int32 ctc_greatbelt_dkits_dump_cfg_cmp(uint8 lchip, void* p_para1, void* p_para2);
extern int32 ctc_greatbelt_dkits_dump_decode_entry(uint8 lchip, void* p_para1, void* p_para2);
extern int32 ctc_greatbelt_dkits_dump_diff_field(ctc_dkits_dump_key_tbl_info_t* p_key_tbl_info, ctc_dkits_dump_tbl_entry_t* p_src_tbl, ctc_dkits_dump_tbl_entry_t* p_dst_tbl, sal_file_t p_wf);
extern int32 ctc_greatbelt_dkits_dump_data2tbl(uint8 lchip, ctc_dkits_dump_tbl_type_t tbl_type, uint8 cfg_endian, ctc_dkits_dump_tbl_block_hdr_t* p_tbl_block_hdr, uint8* p_buf, ctc_dkits_dump_tbl_entry_t* p_tbl_entry);
#ifdef __cplusplus
}
#endif

#endif


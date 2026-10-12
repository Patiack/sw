/**
 @file sys_goldengate_cpu_reason.h

 @date 2010-01-13

 @version v2.0

 The file defines macro, data structure, and function for en-queue
*/

#ifndef _SYS_GOLDENGATE_CPU_REASON_H_
#define _SYS_GOLDENGATE_CPU_REASON_H_
#ifdef __cplusplus
extern "C" {
#endif


extern int32
sys_goldengate_queue_cpu_reason_init(uint8 lchip);


extern int32
sys_goldengate_cpu_reason_set_map(uint8 lchip, uint16 reason_id,
                                  uint8 sub_queue_id,
                                  uint8 group_id);
extern int32
sys_goldengate_cpu_reason_set_dest(uint8 lchip, uint16 reason_id,
                                  uint8 dest_type,
                                  uint32 dest_port);

extern int32
sys_goldengate_get_sub_queue_id_by_cpu_reason(uint8 lchip, uint16 reason_id, uint8* sub_queue_id);

extern int32
sys_goldengate_cpu_reason_set_truncation_length(uint8 lchip, uint32 length);
extern int32
sys_goldengate_cpu_reason_set_misc_param(uint8 lchip, uint16 reason_id, uint8 truncation_en);

extern int32
sys_goldengate_cpu_reason_get_dsfwd_offset(uint8 lchip, uint16 reason_id,
                                           uint8 lport_en,
                                           uint32 *dsfwd_offset,
                                           uint32 *dsnh_offset,
                                           uint32 *dest_port);

extern int32
sys_goldengate_cpu_reason_get_info(uint8 lchip, uint16 reason_id,
                                           uint32 *destmap);

#ifdef __cplusplus
}
#endif

#endif


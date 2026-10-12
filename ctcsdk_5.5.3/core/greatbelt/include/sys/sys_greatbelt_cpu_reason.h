/**
 @file sys_greatbelt_cpu_reason.h

 @date 2010-01-13

 @version v2.0

 The file defines macro, data structure, and function for en-queue
*/

#ifndef _SYS_GREATBELT_CPU_REASON_H_
#define _SYS_GREATBELT_CPU_REASON_H_
#ifdef __cplusplus
extern "C" {
#endif

#define SYS_REASON_ENCAP_DEST_ID(queue_id) \
    ((SYS_QSEL_TYPE_EXCP_CPU << 13) | queue_id)

extern int32
sys_greatbelt_cpu_reason_set_dest(uint8 lchip,
                                  uint16 reason_id,
                                  uint8 dest_type,
                                  uint32 dest_port);

extern int32
sys_greatbelt_cpu_reason_get_info(uint8 lchip, uint16 reason_id,
                                   uint32 *destmap);

extern int32
sys_greatbelt_cpu_reason_get_dsfwd_offset(uint8 lchip, uint16 reason_id, uint8 lport_en,
                                           uint32 *dsfwd_offset,
                                           uint16 *dsnh_offset,
                                           uint32 *dest_port);

extern int32
sys_greatbelt_cpu_reason_set_priority(uint8 lchip,
                                          uint16 reason_id,
                                          uint8 cos);

extern int32
sys_greatbelt_cpu_reason_get_queue_offset(uint8 lchip,
                                          uint16 reason_id,
                                          uint8* offset,
                                          uint16* sub_queue_id);

#ifdef __cplusplus
}
#endif

#endif


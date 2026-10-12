/**
 @file sys_goldengate_fpa.h

 @date 2016-07-23

 @version v3.0

*/
#ifndef _SYS_GOLDENGATE_FPA_H
#define _SYS_GOLDENGATE_FPA_H

#ifdef __cplusplus
extern "C" {
#endif
/***************************************************************
 *
 * Header Files
 *
 ***************************************************************/
#include "ctc_const.h"


/***************************************************************
 *
 *  Defines and Macros
 *
 ***************************************************************/
#define FPA_PRIORITY_HIGHEST  0xFFFFFFFF
#define FPA_PRIORITY_LOWEST   0
#define FPA_PRIORITY_DEFAULT  1

typedef enum
{
    CTC_FPA_KEY_SIZE_160,
    CTC_FPA_KEY_SIZE_320,
    CTC_FPA_KEY_SIZE_640,
    CTC_FPA_KEY_SIZE_NUM
}ctc_fpa_key_size_t;

typedef struct
{
    uint32      entry_id; /* entry id*/
    uint32      priority; /* entry priority. . */

    uint32      offset_a; /* key index. max: 2 local_chips*/
    uint8       flag;      /* entry flag, FPA_ENTRY_FLAG_xxx */
    uint8       lchip;
    uint8       rsv0[2];
}ctc_fpa_entry_t;

typedef struct
{
    uint16      entry_count;  /* entry count on each block. uint16 is enough.*/
    uint16      free_count;   /* entry count left on each block*/

    uint16      start_offset[CTC_FPA_KEY_SIZE_NUM];
    uint16      sub_entry_count[CTC_FPA_KEY_SIZE_NUM];
    uint16      sub_free_count[CTC_FPA_KEY_SIZE_NUM];
    uint16      sub_rsv_count[CTC_FPA_KEY_SIZE_NUM];    /*rsv count can not be taken when adjust resource*/

    uint16      trend_active[CTC_FPA_KEY_SIZE_NUM];
    uint16      increase_trend[CTC_FPA_KEY_SIZE_NUM];
    uint16      decrease_trend[CTC_FPA_KEY_SIZE_NUM];
    uint32      last_priority[CTC_FPA_KEY_SIZE_NUM];
    uint32      move_num;

    ctc_fpa_entry_t** entries;      /* pointer to entry*/
}ctc_fpa_block_t;

typedef int (*fpa_get_info_by_pe_fn)  (ctc_fpa_entry_t* pe, ctc_fpa_block_t** pb);
typedef int (*fpa_entry_move_hw_fn)   (ctc_fpa_entry_t* pe, int32 new_offset);

typedef struct
{
    fpa_get_info_by_pe_fn  get_info_by_pe;
    fpa_entry_move_hw_fn   entry_move_hw;
    uint8                  offset; /* fpa_entry offset to user_entry struct's head.*/
}ctc_fpa_t;


/*
 *move entry to a new place with amount steps.
 */
#define FPA_ENTRY_FLAG_UNINSTALLED  0
#define FPA_ENTRY_FLAG_INSTALLED    1


extern int32
fpa_goldengate_set_entry_prio(ctc_fpa_t* fpa, ctc_fpa_entry_t* pe, ctc_fpa_block_t* pb, uint8 key_size, uint32 prio);

extern int32
fpa_goldengate_alloc_offset(ctc_fpa_t* fpa, ctc_fpa_block_t* pb, uint8 key_size, uint32 prio, uint32* block_index);

extern int32
fpa_goldengate_free_offset(ctc_fpa_block_t* pb, uint32 block_index);

extern int32
fpa_goldengate_reorder(ctc_fpa_t* fpa, ctc_fpa_block_t* pb, uint8 key_size);

extern ctc_fpa_t*
fpa_goldengate_create(fpa_get_info_by_pe_fn fn1,
           fpa_entry_move_hw_fn fn2,
           uint8 offset);

extern void
fpa_goldengate_free(ctc_fpa_t* fpa);

#ifdef __cplusplus
}
#endif

#endif


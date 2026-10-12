/**
 @file sys_greatbelt_fpa.h

 @date 2013-02-15

 @version v2.0

*/
#ifndef _SYS_GREATBELT_FPA_H
#define _SYS_GREATBELT_FPA_H

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

typedef struct
{
    uint32      entry_id; /* entry id*/
    uint32      priority; /* entry priority. . */
    uint8       lchip;
    int32      offset_a; /* key index. max: 2 local_chips*/
    uint8       flag;      /* entry flag */
    uint8       rsv0[2];
}ctc_fpa_entry_t;

typedef struct
{
    uint8       block_number; /* physical block 0~4*/
    uint8       lchip;        /* block belong to which chip. 0 or 1. no 0xFF */
    uint16      zero_count;   /* entry count of prio == 0 */
    uint16      high_count;   /* entry count of prio == 0xFFFFFFFF */

    uint16      entry_count;  /* entry count on each block. uint16 is enough.*/
    uint16      free_count;   /* entry count left on each block*/

    uint32       last_priority;
    uint8        trend_active;
    uint32       increase_trend;
    uint32       decrease_trend;
    uint32       move_num;
    ctc_fpa_entry_t**      entries;      /* pointer to entry*/
}ctc_fpa_block_t;

typedef int (*fpa_get_info_by_pe_fn)  (uint8 lchip, ctc_fpa_entry_t* pe, ctc_fpa_block_t** pb);
typedef int (*fpa_entry_move_hw_fn)   (uint8 lchip, ctc_fpa_entry_t* pe, int32 new_offset);

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
fpa_greatbelt_set_entry_prio(ctc_fpa_t* fpa, ctc_fpa_entry_t* pe, ctc_fpa_block_t* pb, uint32 prio);

extern int32
fpa_greatbelt_alloc_offset(ctc_fpa_t* fpa, ctc_fpa_block_t* pb, uint32 prio, int32* block_index);

extern int32
fpa_greatbelt_free_offset(ctc_fpa_t* fpa, ctc_fpa_block_t* pb, int32 block_index);

extern int32
fpa_greatbelt_reorder(ctc_fpa_t* fpa, ctc_fpa_block_t* pb);

extern ctc_fpa_t*
fpa_greatbelt_create(fpa_get_info_by_pe_fn fn1,
           fpa_entry_move_hw_fn fn2,
           uint8 offset);

extern void
fpa_greatbelt_free(ctc_fpa_t* fpa);

#ifdef __cplusplus
}
#endif

#endif


/**
   @file sys_greatbelt_acl.c

   @date 2012-09-19

   @version v2.0

 */

/****************************************************************************
*
* Header Files
*

****************************************************************************/
#include "sal.h"
#include "ctc_error.h"
#include "ctc_debug.h"
/*#include <time.h>*/


#include "sys_greatbelt_fpa.h"



/* pe is ctc_fpa_entry_t */
#define FPA_OUTER_ENTRY(pe)          ((uint8 *) pe - (fpa->offset))
/*#define FPA_INNER_ENTRY(i) ( (ctc_fpa_entry_t*) ( (pb->entries[i]) + (fpa->offset) ))*/
#define FPA_INNER_ENTRY(i)           (pb->entries[i])
#define PREV_IDX(idx)                ((idx) - 1)
#define NEXT_IDX(idx)                ((idx) + 1)
#define TRIGGER_REORDER      25
#define TRIGGER_PRESS        10
#define DO_REORDER(move_num, all)    ((move_num) >= ((all) / TRIGGER_REORDER))
#define DO_PRESS(move_num, all)      ((move_num) == TRIGGER_PRESS)
#define FPA_INVALID_INDEX    (-1)

#define SYS_FPA_DBG_OUT(level, FMT, ...) \
    CTC_DEBUG_OUT(fpa, fpa, FPA, level, FMT, ## __VA_ARGS__)

#define SYS_FPA_DBG_FUNC() \
    SYS_FPA_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "  %% FUNC: %s()\n", __FUNCTION__)

#define SYS_FPA_DBG_INFO(FMT, ...) \
    SYS_FPA_DBG_OUT(CTC_DEBUG_LEVEL_INFO, FMT, ## __VA_ARGS__)

#define SYS_FPA_DBG_PARAM(FMT, ...) \
    SYS_FPA_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, FMT, ## __VA_ARGS__)

#define SYS_FPA_DBG_ERROR(FMT, ...) \
    SYS_FPA_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, FMT, ## __VA_ARGS__)

#define SYS_FPA_DBG_DUMP(FMT, ...) \
    SYS_FPA_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, FMT, ## __VA_ARGS__)



/*
 * move entry to new position in specific chip.
 */
STATIC int32
_fpa_greatbelt_entry_move(ctc_fpa_t* fpa, ctc_fpa_entry_t* pe, int32 amount, uint8 lchip)
{
    int32          tcam_idx_old = 0; /* Original entry tcam index.    */
    int32          tcam_idx_new = 0; /* Next tcam index for the entry.*/
    ctc_fpa_block_t* pb;             /* Field slice control.          */

    int32          ret;

    if (!pe)
    {
        return  CTC_E_NONE;/* skip */
    }

    fpa->get_info_by_pe(lchip, pe, &pb);
    CTC_PTR_VALID_CHECK(pb);

    if (amount == 0) /* move no where */
    {
        return CTC_E_NONE;
    }

    tcam_idx_old = pe->offset_a;
    tcam_idx_new = tcam_idx_old + amount;

    /* Move the hardware entry.*/
    if (FPA_ENTRY_FLAG_INSTALLED == pe->flag)
    {
        ret = fpa->entry_move_hw(lchip, pe, tcam_idx_new);
        CTC_ERROR_RETURN(ret);
    }

    /* Move the software entry.*/

    pb->entries[tcam_idx_old] = NULL;
    pb->entries[tcam_idx_new] = pe;
    pe->offset_a       = tcam_idx_new;

    return(CTC_E_NONE);
}

/*
 * shift up entries from target entry to prev null entry
 */
STATIC int32
_fpa_greatbelt_entry_shift_up(ctc_fpa_t* fpa,
                    ctc_fpa_block_t* pb,
                    int32 target_index,
                    int32 prev_null_index,
                    uint32* move_num)
{
    int32 temp;

    /* Input parameter check. */
    CTC_PTR_VALID_CHECK(pb);

    temp = prev_null_index;

    /* start from prev-1:
     *  prev + 1 -- > prev
     *  prev + 2 -- > prev + 1
     *  ...
     *  target -- > target - 1
     */
    while ((temp < target_index))
    {
        /* Move the entry at the next index to the prev. empty index. */

        /* Perform entry move. */
        CTC_ERROR_RETURN(_fpa_greatbelt_entry_move(fpa, pb->entries[temp + 1], -1, pb->lchip));

        temp++;
    }

    temp = target_index - prev_null_index;
    *move_num = (temp < 0) ? 0 : temp;

    return(CTC_E_NONE);
}

/*
 * shift down entries from target entry to next null entry
 */
STATIC int32
_fpa_greatbelt_entry_shift_down(ctc_fpa_t* fpa,
                      ctc_fpa_block_t* pb,
                      int32 target_index,
                      int32 next_null_index,
                      uint32* move_num)
{
    int32 temp;

    /* Input parameter check. */
    CTC_PTR_VALID_CHECK(pb);

    temp = next_null_index;

    /*
     * Move entries one step down
     *     starting from the last entry
     * start from prev-1:
     *  next - 1 -- > next
     *  next - 2 -- > next -1
     *  ...
     *  target -- > target + 1
     */
    while (temp > target_index)
    {
        /* Perform entry move. */
        CTC_ERROR_RETURN(_fpa_greatbelt_entry_move(fpa, pb->entries[temp - 1], 1, pb->lchip));
        temp--;
    }

    temp = next_null_index - target_index;
    *move_num = (temp < 0) ? 0 : temp;

    return(CTC_E_NONE);
}

/*
 * get how many step need to shift entries from target entry to null entry.
 * return value:
 *   TRUE  -- shift is ok
 *   FALSE -- shift is not ok
 *   shift_count --  how many entries need shift
 */
STATIC bool
_fpa_greatbelt_get_shift_count(int32 null, int32 target, int32 dir, int32* count)
{
    if (-1 == dir)
    {
        target--; /* shift up, targe is one up. */
    }
    *count = dir * (null - target);

    if (null == FPA_INVALID_INDEX)
    {
        return FALSE;
    }
    else
    {
        return TRUE;
    }
}

/*
 * get shift direction, up or down
 * return value
 *   TRUE  -- shift is feasible
 *   FALSE -- shift is not feasible
 *   dir   -- shift to which direction
 */
STATIC int32
_fpa_greatbelt_get_shift_direction(int32 prev_null_index,
                         int32 target_index,
                         int32 next_null_index,
                         int32* dir)
{
    bool  shift_up          = FALSE;
    bool  shift_down        = FALSE;
    int32 shift_up_amount   = 0;
    int32 shift_down_amount = 0;

    /* Input parameter check. */

    CTC_PTR_VALID_CHECK(dir);


    shift_up = _fpa_greatbelt_get_shift_count(prev_null_index, target_index, -1, &shift_up_amount);

    shift_down = _fpa_greatbelt_get_shift_count(next_null_index, target_index, 1, &shift_down_amount);

    SYS_FPA_DBG_INFO("  %%INFO: shift up   %s count %d \n", shift_up ? "T" : "F", shift_up_amount);
    SYS_FPA_DBG_INFO("  %%INFO: shift down %s count %d \n", shift_down ? "T" : "F", shift_down_amount);

    if (shift_up == TRUE)
    {
        if (shift_down == TRUE)
        {
            if (shift_up_amount < shift_down_amount)
            {
                *dir = -1;
            }
            else
            {
                *dir = 1;
            }
        }
        else
        {
            *dir = -1;
        }
    }
    else
    {
        if (shift_down == TRUE)
        {
            *dir = 1;
        }
        else
        {
            return CTC_E_NO_RESOURCE;
        }
    }

    return CTC_E_NONE;
}

typedef struct
{
    uint16 t_idx; /* target index */
    uint16 o_idx; /* old index */
}_fpa_target_t;

#define REORDE_TYPE_SCATTER     0
#define REORDE_TYPE_DECREASE    1
#define REORDE_TYPE_INCREASE    2
#define IS_NORMAL_ENTRY(prio) \
    (prio != FPA_PRIORITY_LOWEST) && \
    (prio != FPA_PRIORITY_HIGHEST)

STATIC int32
_fpa_greatbelt_reorder(ctc_fpa_t* fpa, ctc_fpa_block_t* pb, int32 bottom_idx, uint8 type)
{
    uint8         lchip;
    int32         idx;
    int32         revise;
    int32         t_idx;
    int32         o_idx;
    _fpa_target_t * target_a = NULL;
    int32         ret        = 0;
    uint8         move_ok;
    static uint32 cnt = 0;
/*    static double time;*/
/*    clock_t begin,end;*/

    uint32 full_num;
    uint32 free_num;
    uint32 real_num;             /* actual entry number */
    uint32 left_num;

    cnt++;
/*    begin = clock();*/
    CTC_PTR_VALID_CHECK(pb);
    lchip = pb->lchip;

    /* malloc a new array based on new exist entry*/
    full_num = pb->entry_count - pb->zero_count - pb->high_count; /* middle priority count */
    free_num = pb->free_count;

    real_num = full_num - pb->free_count;
    if(real_num == 0)
    {
       return CTC_E_NONE;
    }
    MALLOC_ZERO(MEM_FPA_MODULE, target_a, real_num * sizeof(_fpa_target_t))
    if (NULL == target_a)
    {
        return CTC_E_NO_MEMORY;
    }

    /* save target idx to array */
    if (REORDE_TYPE_SCATTER == type)
    {
        for (t_idx = 0; t_idx < real_num; t_idx++)
        {
            target_a[t_idx].t_idx  = (full_num * t_idx) / real_num;
            target_a[t_idx].t_idx += pb->high_count;
        }
    }
    else if (REORDE_TYPE_DECREASE == type)
    {
        for (t_idx = 0; t_idx < real_num; t_idx++)
        {
            target_a[t_idx].t_idx  = t_idx;
            target_a[t_idx].t_idx += pb->high_count;
        }
    }
    else
    {
        for (t_idx = 0; t_idx < real_num; t_idx++)
        {
            target_a[t_idx].t_idx  = free_num + t_idx;
            target_a[t_idx].t_idx += pb->high_count;
        }
    }

    /* save old idx to array */
    o_idx = 0;
    for (idx = 0; idx < pb->entry_count; idx++)  /* through all entry */
    {
        if ((pb->entries[idx]) &&
            IS_NORMAL_ENTRY(pb->entries[idx]->priority))
        {
            if (o_idx >= real_num )
            {
                SYS_FPA_DBG_DUMP("  unexpect real_num %d! \n", real_num);
            }
            target_a[o_idx].o_idx = idx;
            o_idx++;
        }
    }

    left_num = real_num;
    while (left_num) /* move_num */
    {
        SYS_FPA_DBG_INFO("left_num %d, real_num %d\n", left_num, real_num);

        for (idx = 0; idx < left_num; idx++)
        {
            revise  = (REORDE_TYPE_INCREASE == type) ? (left_num - 1 - idx) : idx;
            move_ok = 0;

            if (target_a[revise].o_idx == target_a[revise].t_idx) /* stay */
            {
                SYS_FPA_DBG_INFO("stay !\n");
                move_ok = 1;
            }
            else
            {
                if (target_a[revise].o_idx < target_a[revise].t_idx) /* move forward */
                {
                    if ((revise == left_num - 1) || (target_a[revise + 1].o_idx > target_a[revise].t_idx))
                    {
                        move_ok = 1;
                    }
                }
                else /* move backward */
                {
                    if ((revise == 0) || (target_a[revise - 1].o_idx > target_a[revise].t_idx))
                    {
                        move_ok = 1;
                    }
                }

                if (move_ok)
                {
                    SYS_FPA_DBG_INFO(" move from %d to %d!\n", target_a[revise].o_idx, target_a[revise].t_idx);
                    /* move revise to temp */
                    CTC_ERROR_GOTO(_fpa_greatbelt_entry_move(fpa, pb->entries[target_a[revise].o_idx],
                                                   (target_a[revise].t_idx - target_a[revise].o_idx), lchip), ret, cleanup);
                }
            }

            if (move_ok)
            {
                if (REORDE_TYPE_INCREASE != type)
                {
                    sal_memmove(&target_a[revise], &target_a[revise + 1], (left_num - revise - 1) * sizeof(_fpa_target_t));
                }
                left_num--;
                idx--;
            }
        }
    }

    mem_free(target_a);

/*    end = clock();*/
/*    time = time + ((double)(end - begin) / CLOCKS_PER_SEC);*/
/*    SYS_FPA_DBG_INFO(" t:%lf", time);*/

    return CTC_E_NONE;

 cleanup:
    mem_free(target_a);
    return ret;
}


/* output:
 * targ, if invalid: 1. all null. 2. prio is the smallest.
 * prev, if invalid: 1. full.
 * next, if invalid: 1. targ is invalid 2. targ found, but after targ no null.
 */


/*
 first_valid != first_normal. <==> highest entry exist.
 last_valid  != last_normal. <==> lowest entry exist.
 targ != targ_normal.   <==> targ_normal = -1, targ points to first zero entry.
 prev === prev_n. next === next_n.
*/
STATIC int32
_fpa_greatbelt_get_indexes(uint32 prio, ctc_fpa_entry_t* pe, ctc_fpa_block_t* pb,
                 int32* targ, int* targ_normal, int32* prev, int32* next,
                 int* first_normal, int* last_normal,
                 int* first_null  , int* last_null,
                 int* first_valid, int* last_valid)
{
    int32 idx;
    for (idx = 0; idx < pb->entry_count; idx++)
    {
        /* Skip the pe itself */
        if (pe && (pe == pb->entries[idx]))
        {
            continue;
        }

        if (pb->entries[idx] == NULL)
        {
            if (FPA_INVALID_INDEX == *targ)
            {
                *prev = idx;
            }
            else if (FPA_INVALID_INDEX == *next)
            {
                *next = idx;
            }
            if (FPA_INVALID_INDEX == *first_null)
            {
                *first_null = idx;
            }
            *last_null = idx;
        }
        else /* valid entry */
        {
            *last_valid = idx;

            if ((prio > pb->entries[idx]->priority)
                && (FPA_INVALID_INDEX == *targ)) /* found target */
            {
                *targ = idx;
            }

            if (FPA_INVALID_INDEX == *first_valid)  /* found first_valid */
            {
                *first_valid = idx;
            }

            if (IS_NORMAL_ENTRY(pb->entries[idx]->priority))
            {
                *last_normal = idx;

                if ((prio > pb->entries[idx]->priority)
                    && (FPA_INVALID_INDEX == *targ_normal)) /* found target */
                {
                    *targ_normal = idx;
                }

                if (FPA_INVALID_INDEX == *first_normal)
                {
                    *first_normal = idx;
                }
            }
        }
    }
    if (prio > pb->last_priority)
    {
        pb->decrease_trend = 0;
        pb->increase_trend++;
    }
    else
    {
        pb->decrease_trend++;
        pb->increase_trend = 0;
    }

    pb->trend_active = ((pb->decrease_trend + pb->increase_trend) >= TRIGGER_PRESS);
    pb->last_priority = prio;

    return CTC_E_NONE;
}
#define BLOCK_END             (pb->entry_count - 1)
#define BLOCK_NORMAL_END      (pb->entry_count - pb->zero_count - 1)

#define BLOCK_START           0
#define BLOCK_NORMAL_STATR    (pb->high_count)

STATIC int32
_fpa_greatbelt_get_offset(ctc_fpa_t* fpa, ctc_fpa_entry_t* pe, ctc_fpa_block_t* pb,
                uint32 prio, int32* target, uint32* move_num_out)
{
    int32  target_out  = 0;
    int32  prev        = FPA_INVALID_INDEX;
    int32  next        = FPA_INVALID_INDEX;
    int32  first_valid = FPA_INVALID_INDEX;
    int32  last_valid  = FPA_INVALID_INDEX;
    int32  first_normal= FPA_INVALID_INDEX;
    int32  last_normal = FPA_INVALID_INDEX;
    int32  first_null  = FPA_INVALID_INDEX;
    int32  last_null   = FPA_INVALID_INDEX;
    int32  targ        = FPA_INVALID_INDEX;
    int32  targ_n      = FPA_INVALID_INDEX;
    int32  dir = 0;
    uint32 move_num = 0;

    if ((pb->entry_count == (pb->free_count + pb->zero_count + pb->high_count))
        && IS_NORMAL_ENTRY(prio))
    {
        *target = ((pb->entry_count - pb->zero_count - pb->high_count) >> 1) + pb->high_count;
        return CTC_E_NONE;
    }

    _fpa_greatbelt_get_indexes(prio, pe, pb, &targ, &targ_n,
                     &prev, &next, &first_normal, &last_normal,
                     &first_null, &last_null, &first_valid, &last_valid);

    if (FPA_PRIORITY_LOWEST == prio)
    {
        target_out = BLOCK_END;
        if (BLOCK_END == last_valid)
        {
            CTC_ERROR_RETURN(_fpa_greatbelt_entry_shift_up(fpa, pb, BLOCK_END, last_null, &move_num));
        }
        goto end;
    }
    else if (FPA_PRIORITY_HIGHEST == prio)
    {
        target_out = BLOCK_START;
        if (BLOCK_START == first_valid)
        {
            CTC_ERROR_RETURN(_fpa_greatbelt_entry_shift_down(fpa, pb, BLOCK_START, first_null, &move_num));
        }
        goto end;
    }

    /* new prio is normal: nor 0 neither 0xFFFFFFFF. */
    if (FPA_INVALID_INDEX == targ_n)  /* smallest priority. but not 0. maybe 0 exists*/
    {
        if(FPA_INVALID_INDEX == first_normal)
        {
            if (pe)
            {
                target_out = pe->offset_a;
            }
        }
        else if (BLOCK_NORMAL_END == last_normal)
        {
            target_out = BLOCK_NORMAL_END;
            CTC_ERROR_RETURN(_fpa_greatbelt_entry_shift_up(fpa, pb, target_out, prev, &move_num));
        }
        else  /* last_normal is valid*/
        {
            target_out = NEXT_IDX(last_normal);
        }
    }
    else if (first_normal == targ_n) /* biggest priority. but not 0xFFFFFFFF. maybe 0xFFFFFFFF exists */
    {
        if (BLOCK_NORMAL_STATR == first_normal)
        {
            target_out = BLOCK_NORMAL_STATR;
            CTC_ERROR_RETURN(_fpa_greatbelt_entry_shift_down(fpa, pb, target_out, next, &move_num));
        }
        else
        {
            target_out = PREV_IDX(first_normal);
        }
    }
    else /* not smallest, not biggest priority */
    {
        _fpa_greatbelt_get_shift_direction(prev, targ_n, next, &dir);
        if (dir == 1)
        {
            target_out = targ_n;
            /*
             * Move the entry at the targ index to target_index+1. This may
             * mean shifting more entries down to make room. In other words,
             * shift the targ index and any that follow it down 1 as far as the
             * next empty index.
             */
            SYS_FPA_DBG_INFO("  %%INFO: from %d to %d do shift down \n", target_out, next - 1);
            CTC_ERROR_RETURN(_fpa_greatbelt_entry_shift_down(fpa, pb, target_out, next, &move_num));
        }
        else
        {
            /*
             * for Shifting UP , the targ is one up.
             */
            target_out = targ_n - 1;

            SYS_FPA_DBG_INFO("  %%INFO: from %d to %d do shift up \n", target_out, prev + 1);
            CTC_ERROR_RETURN(_fpa_greatbelt_entry_shift_up(fpa, pb, target_out, prev, &move_num));
        }
    }


 end:
    *target = target_out;
    *move_num_out = move_num;
    return CTC_E_NONE;
}



#define  ___FPA_SORT_OUTER__


/*
 * set priority of entry specific by pe and pb. pe must be in pb
 */
int32
fpa_greatbelt_set_entry_prio(ctc_fpa_t* fpa, ctc_fpa_entry_t* pe, ctc_fpa_block_t* pb, uint32 prio)
{
    int32  target = 0;
    uint8  lchip;
    int32  temp;
    int32  temp1;
    uint32 move_num = 0;
    uint32 old_prio = 0;
    int32 loop_idx;
    ctc_fpa_entry_t* tmp_pe;

    lchip = pb->lchip;

    /* If the priority isn't changing, just return.*/
    old_prio = pe->priority;
    if (old_prio == prio)
    {
        return(CTC_E_NONE);
    }
    if (0 == (pb->free_count))
    {
        return CTC_E_NO_RESOURCE;
    }


    CTC_ERROR_RETURN(_fpa_greatbelt_get_offset(fpa, pe, pb, prio, &target, &move_num));
    temp  = target;
    temp1 = pe->offset_a;

    if ((temp - temp1) != 0)
    {

        CTC_ERROR_RETURN(_fpa_greatbelt_entry_move(fpa, pe, (temp - temp1), lchip));
    }

    /* Assign the requested priority to the entry. */
    pe->priority = prio;

    if (FPA_PRIORITY_LOWEST == prio)
    {
        pb->zero_count++;
    }
    if (FPA_PRIORITY_LOWEST == old_prio)
    {
        pb->zero_count--;
        loop_idx = temp1-1;
        tmp_pe = pb->entries[loop_idx];
        while(tmp_pe && tmp_pe->priority == FPA_PRIORITY_LOWEST)
        {
            CTC_ERROR_RETURN(_fpa_greatbelt_entry_move(fpa, tmp_pe, 1, lchip));
            loop_idx--;
            if(loop_idx < 0)
            {
                break;
            }
            tmp_pe = pb->entries[loop_idx];
        }
    }

    if (FPA_PRIORITY_HIGHEST == prio)
    {
        pb->high_count++;
    }
    if (FPA_PRIORITY_HIGHEST == old_prio)
    {
        pb->high_count--;
        loop_idx = temp1+1;
        tmp_pe = pb->entries[loop_idx];
        while(tmp_pe && tmp_pe->priority == FPA_PRIORITY_HIGHEST)
        {
            CTC_ERROR_RETURN(_fpa_greatbelt_entry_move(fpa, tmp_pe, -1, lchip));
            loop_idx++;
            if(loop_idx >= pb->entry_count)
            {
                break;
            }
            tmp_pe = pb->entries[loop_idx];
        }
    }

	/* do reorde after update count. this process is independant. */
    if (DO_REORDER(move_num, pb->entry_count))
    {
        CTC_ERROR_RETURN(_fpa_greatbelt_reorder(fpa, pb, pb->entry_count - 1, REORDE_TYPE_SCATTER));
    }

    return(CTC_E_NONE);
}

/*
 * worst is best
 */
int32
fpa_greatbelt_alloc_offset(ctc_fpa_t* fpa, ctc_fpa_block_t* pb, uint32 prio, int32* block_index)
{
    int32 target = 0;
    CTC_PTR_VALID_CHECK(pb);
    CTC_PTR_VALID_CHECK(block_index);

    if (0 == (pb->free_count))
    {
        return CTC_E_NO_RESOURCE;
    }

    if (DO_PRESS(pb->decrease_trend, pb->entry_count))
    {
        CTC_ERROR_RETURN(_fpa_greatbelt_reorder(fpa, pb, pb->entry_count - 1, REORDE_TYPE_DECREASE));
    }
    else if (DO_PRESS(pb->increase_trend, pb->entry_count))
    {
        CTC_ERROR_RETURN(_fpa_greatbelt_reorder(fpa, pb, pb->entry_count - 1, REORDE_TYPE_INCREASE));
    }

    CTC_ERROR_RETURN(_fpa_greatbelt_get_offset(fpa, NULL, pb, prio, &target, &pb->move_num));

    if (FPA_PRIORITY_LOWEST == prio)
    {
        pb->zero_count++;
    }
    else if (FPA_PRIORITY_HIGHEST == prio)
    {
        pb->high_count++;
    }

    *block_index = target;
    return CTC_E_NONE;
}


/*
 * worst is best
 */
int32
fpa_greatbelt_free_offset(ctc_fpa_t* fpa, ctc_fpa_block_t* pb, int32 block_index)

{
    int32 first_lowest_index = 0;
    int32 last_highest_index = 0;
    uint32 move_num = 0;

    if (pb->entries[block_index])
    {
        if (FPA_PRIORITY_LOWEST == pb->entries[block_index]->priority)
        {
            first_lowest_index = pb->entry_count - pb->zero_count;
            if (block_index != first_lowest_index)
            {
                CTC_ERROR_RETURN(_fpa_greatbelt_entry_shift_down(fpa, pb, first_lowest_index, block_index, &move_num));
            }

            pb->zero_count--;
        }
        else if (FPA_PRIORITY_HIGHEST == pb->entries[block_index]->priority)
        {
            last_highest_index = pb->high_count - 1;
            if (block_index != last_highest_index)
            {
                CTC_ERROR_RETURN(_fpa_greatbelt_entry_shift_up(fpa, pb, last_highest_index, block_index, &move_num));
            }

            pb->high_count--;
        }
    }

    (pb->free_count)++;

    if (0 == move_num)
    {
        pb->entries[block_index] = NULL;
    }

    pb->decrease_trend  = 0;
    pb->increase_trend  = 0;
    pb->trend_active    = 0;

    return CTC_E_NONE;
}

int32
fpa_greatbelt_reorder(ctc_fpa_t* fpa, ctc_fpa_block_t* pb)
{
    if (DO_REORDER(pb->move_num, pb->entry_count) && !pb->trend_active)
    {
        CTC_ERROR_RETURN(_fpa_greatbelt_reorder(fpa, pb, pb->entry_count - 1, REORDE_TYPE_SCATTER));
    }

    pb->move_num = 0;
    return CTC_E_NONE;
}

/*
 * set priority of entry specific by entry id
 */
ctc_fpa_t*
fpa_greatbelt_create(fpa_get_info_by_pe_fn fn1,
           fpa_entry_move_hw_fn fn2,
           uint8 offset)
{
    ctc_fpa_t* fpa = NULL;

    MALLOC_ZERO(MEM_FPA_MODULE, fpa, sizeof(ctc_fpa_t));

    if (NULL == fpa)
    {
        return NULL;
    }

    fpa->get_info_by_pe = fn1;
    fpa->entry_move_hw  = fn2;
    fpa->offset         = offset;

    return fpa;
}

void
fpa_greatbelt_free(ctc_fpa_t* fpa)
{
    mem_free(fpa);
}


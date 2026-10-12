/**
 @file sys_greatbelt_qos_policer.c

 @date 2009-10-16

 @version v2.0

*/

/****************************************************************************
  *
  * Header Files
  *
  ****************************************************************************/
#include "ctc_error.h"
#include "ctc_const.h"
#include "ctc_macro.h"
#include "ctc_qos.h"
#include "ctc_debug.h"
#include "ctc_hash.h"
#include "ctc_spool.h"

#include "sys_greatbelt_chip.h"
#include "sys_greatbelt_opf.h"
#include "sys_greatbelt_port.h"
#include "sys_greatbelt_stats.h"
#include "sys_greatbelt_ftm.h"
#include "sys_greatbelt_qos_policer.h"
#include "sys_greatbelt_queue_enq.h"
#include "sys_greatbelt_register.h"
#if 1
#include "greatbelt/include/drv_io.h"
#include "greatbelt/include/drv_lib.h"

/****************************************************************************
  *
  * Defines and Macros
  *
  ****************************************************************************/

#define SYS_QOS_POLICER_BENCH_GRAN 128 /*128K*/

#define MAX_PHB_OFFSET_NUM             4

#define SYS_QOS_POLICER_MAX_POLICER_NUM        8192
#define SYS_QOS_POLICER_MAX_PROFILE_NUM        512

#define SYS_QOS_POLICER_HASH_TBL_SIZE         512
#define SYS_QOS_POLICER_HASH_BLOCK_SIZE       (SYS_QOS_POLICER_MAX_POLICER_NUM / SYS_QOS_POLICER_HASH_TBL_SIZE)

#define SYS_QOS_POLICER_PROF_HASH_TBL_SIZE    64
#define SYS_QOS_POLICER_PROF_HASH_BLOCK_SIZE  (SYS_QOS_POLICER_MAX_PROFILE_NUM / SYS_QOS_POLICER_PROF_HASH_TBL_SIZE)

#define SYS_QOS_POLICER_MAX_RATE      10000000 * (1000 / 8)     /* convert 10Gbps into byte/s */
#define SYS_QOS_POLICER_MAX_TOKEN_SIZE      0x7ff8000

#define SYS_MAX_POLICER_GRANULARITY_RANGE_NUM       10
#define SYS_MAX_POLICER_SUPPORTED_FREQ_NUM          20

/* max port policer num in both direction */
#define MAX_PORT_POLICER_NUM   (64 * 2)

/* get port policer offset */
#define SYS_QOS_PORT_POLICER_INDEX(lport, dir, cos_index_offset) \
    ((lport) + (cos_index_offset) + p_gb_qos_policer_master[lchip]->port_policer_base[dir])

#define ROT(x, k) (((x) << (k)) | ((x) >> (32 - (k))))

#define MIX(a, b, c) \
    do \
    { \
        a -= c;  a ^= ROT(c, 4);  c += b; \
        b -= a;  b ^= ROT(a, 6);  a += c; \
        c -= b;  c ^= ROT(b, 8);  b += a; \
        a -= c;  a ^= ROT(c, 16);  c += b; \
        b -= a;  b ^= ROT(a, 19);  a += c; \
        c -= b;  c ^= ROT(b, 4);  b += a; \
    } while (0)

#define FINAL(a, b, c) \
    { \
        c ^= b; c -= ROT(b, 14); \
        a ^= c; a -= ROT(c, 11); \
        b ^= a; b -= ROT(a, 25); \
        c ^= b; c -= ROT(b, 16); \
        a ^= c; a -= ROT(c, 4);  \
        b ^= a; b -= ROT(a, 14); \
        c ^= b; c -= ROT(b, 24); \
    }

/****************************************************************************
  *
  * Global and Declaration
  *
  ****************************************************************************/

#define SYS_QOS_POLICER_DBG_OUT(level, FMT, ...) \
    do { \
        CTC_DEBUG_OUT(qos, policer, QOS_PLC_SYS, level, FMT, ##__VA_ARGS__); \
    } while (0);

#define SYS_QOS_POLICER_DBG_FUNC()           SYS_QOS_POLICER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__)
#define SYS_QOS_POLICER_DBG_INFO(FMT, ...)  SYS_QOS_POLICER_DBG_OUT(CTC_DEBUG_LEVEL_INFO, FMT, ##__VA_ARGS__)
#define SYS_QOS_POLICER_DBG_ERROR(FMT, ...) SYS_QOS_POLICER_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, FMT, ##__VA_ARGS__)
#define SYS_QOS_POLICER_DBG_PARAM(FMT, ...) SYS_QOS_POLICER_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, FMT, ##__VA_ARGS__)
#define SYS_QOS_POLICER_DBG_DUMP(FMT, ...)  SYS_QOS_POLICER_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, FMT, ##__VA_ARGS__)

/*********************************************************************
  *
  * data structure definition
  *
  *********************************************************************/

/**
 @brief  qos policer granularity for rate range
*/
struct sys_qos_policer_granularity_range_s
{
    uint32 min_rate;        /* unit is Mbps */
    uint32 max_rate;        /* unit is Mbps */
    uint32 granularity;     /* unit is Kbps */
    uint8  is_valid;
    uint8  rsv[3];
};
typedef struct sys_qos_policer_granularity_range_s sys_qos_policer_granularity_range_t;

/**
 @brief  qos policer granularity
*/
struct sys_qos_policer_granularity_s
{
    uint16 core_frequency;
    uint16 tick_gen_interval;
    uint16 min_policer_ptr;
    uint16 max_policer_ptr;
    sys_qos_policer_granularity_range_t range[SYS_MAX_POLICER_GRANULARITY_RANGE_NUM];
};
typedef struct sys_qos_policer_granularity_s sys_qos_policer_granularity_t;

/**
 @brief  qos prof entry data structure
*/
struct sys_qos_policer_profile_s
{
    uint16  profile_id;
    uint8   profile_mem_id;

    ds_policer_profile_t profile;
};
typedef struct sys_qos_policer_profile_s sys_qos_policer_profile_t;

struct sys_qos_policer_s
{
    uint8  type; /*ctc_qos_policer_type_t*/
    uint8  dir;          /*ctc_direction_t */
    uint16 id;

    uint8 stats_en;
    uint16 policer_ptr;

    sys_qos_policer_profile_t* p_sys_profile[4];

    uint8 hbwp_en; /*bitmap of hbwp enable*/
    uint8 entry_size;
    ctc_qos_policer_hbwp_t hbwp[4];
    ctc_qos_policer_param_t policer;
};
typedef struct sys_qos_policer_s sys_qos_policer_t;

struct sys_qos_policer_master_s
{
    ctc_hash_t* p_policer_hash;
    ctc_spool_t* p_profile_pool[4];

    uint16 port_policer_base[CTC_BOTH_DIRECTION];
    uint8  policing_enable;
    uint8  stats_enable;

    sys_qos_policer_granularity_t granularity;

    sal_mutex_t* mutex;
};
typedef struct sys_qos_policer_master_s sys_qos_policer_master_t;

sys_qos_policer_master_t* p_gb_qos_policer_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};

#define SYS_QOS_POLICER_CREATE_LOCK(lchip)                         \
    do                                                              \
    {                                                               \
        sal_mutex_create(&p_gb_qos_policer_master[lchip]->mutex);  \
        if (NULL == p_gb_qos_policer_master[lchip]->mutex)         \
        {                                                           \
            CTC_ERROR_RETURN(CTC_E_FAIL_CREATE_MUTEX);              \
        }                                                           \
    } while (0)

#define SYS_QOS_POLICER_LOCK(lchip) \
    sal_mutex_lock(p_gb_qos_policer_master[lchip]->mutex)

#define SYS_QOS_POLICER_UNLOCK(lchip) \
    sal_mutex_unlock(p_gb_qos_policer_master[lchip]->mutex)

#define CTC_ERROR_RETURN_QOS_POLICER_UNLOCK(op) \
    do \
    { \
        int32 rv = (op); \
        if (rv < 0) \
        { \
            sal_mutex_unlock(p_gb_qos_policer_master[lchip]->mutex); \
            return rv; \
        } \
    } while (0)
/****************************************************************************
  *
  * Function
  *
  ****************************************************************************/

/**
@brief Policer hash key hook.
*/
uint32
_sys_greatbelt_policer_hash_make(sys_qos_policer_t* backet)
{
    uint32 val = 0;
    uint8* data = NULL;
    uint8   length = 0;

    SYS_QOS_POLICER_DBG_FUNC();

    if (!backet)
    {
        return 0;
    }

    SYS_QOS_POLICER_DBG_INFO("backet->type = %d, backet->dir = %d, backet->id = %d\n",
                             backet->type, backet->dir, backet->id);

    val = (backet->type << 24) | (backet->dir << 16) | (backet->id);

    data = (uint8*)&val;
    length = sizeof(uint32);

    return ctc_hash_caculate(length, data);
}

/**
 @brief Policer hash comparison hook.
*/
bool
_sys_greatbelt_policer_hash_cmp(sys_qos_policer_t* p_policer1,
                                sys_qos_policer_t* p_policer2)
{
    SYS_QOS_POLICER_DBG_FUNC();

    if (!p_policer1 || !p_policer2)
    {
        return FALSE;
    }

    if ((p_policer1->type == p_policer2->type) &&
        (p_policer1->dir == p_policer2->dir) &&
        (p_policer1->id == p_policer2->id))
    {
        return TRUE;
    }

    return FALSE;
}

STATIC uint32
_sys_greatbelt_policer_profile_hash_make(sys_qos_policer_profile_t* p_prof)
{
    uint8* data = (uint8*)&p_prof->profile;
    uint8   length = sizeof(ds_policer_profile_t);

    return ctc_hash_caculate(length, data);
}

STATIC bool
_sys_greatbelt_policer_profile_hash_cmp(sys_qos_policer_profile_t* p_prof1,
                                        sys_qos_policer_profile_t* p_prof2)
{
    SYS_QOS_POLICER_DBG_FUNC();

    if (!p_prof1 || !p_prof2)
    {
        return FALSE;
    }

    if (!sal_memcmp(&p_prof1->profile, &p_prof2->profile, sizeof(ds_policer_profile_t)))
    {
        return TRUE;
    }

    return 0;
}

    /* a table of supported granularity, it can be changed by modifying ts_tick_shift */
    sys_qos_policer_granularity_t granularity[SYS_MAX_POLICER_SUPPORTED_FREQ_NUM] =
    {
        /*Core(MHz) Interval  minPtr MaxPtr  mR(Mbps)  MR(Mbps)  Gran  valid */
        {450,       3,     0,     3515,
         {
             {0,       2,        8,     1},
             {2,       100,      32,    1},
             {100,     1000,     64,    1},
             {1000,    2000,     128,   1},
             {2000,    4000,     256,   1},
             {4000,    10000,    512,   1}
         }
        },

        {400,       3,     0,     3124,
         {
             {0,       2,        8,     1},
             {2,       100,      32,    1},
             {100,     1000,     64,    1},
             {1000,    2000,     128,   1},
             {2000,    4000,     256,   1},
             {4000,    10000,    512,   1}
         }
        },

        {500,       3,     0,     3905,
         {
             {0,       2,        8,     1},
             {2,       100,      32,    1},
             {100,     1000,     64,    1},
             {1000,    2000,     128,   1},
             {2000,    4000,     256,   1},
             {4000,    10000,    512,   1}
         }
        },


        {550,       3,     0,     4296,
         {
             {0,       2,        8,     1},
             {2,       100,      32,    1},
             {100,     1000,     64,    1},
             {1000,    2000,     128,   1},
             {2000,    4000,     256,   1},
             {4000,    10000,    512,   1}
         }
        },

        {300,       3,     0,     2343,
         {
             {0,       2,        8,     1},
             {2,       100,      32,    1},
             {100,     1000,     64,    1},
             {1000,    2000,     128,   1},
             {2000,    4000,     256,   1},
             {4000,    10000,    512,   1}
         }
        },
    };


STATIC int32
_sys_greatbelt_qos_policer_init_reg(uint8 lchip, uint16 policer_num)
{
    uint32 cmd            = 0;
    uint32 core_frequency = 0;
    uint8 i               = 0;
    uint32 field_val      = 0;

    static ipe_policing_ctl_t policer_ctl;
    static ipe_classification_ctl_t ipe_classification_ctl;
    static epe_classification_ctl_t epe_classification_ctl;


    SYS_QOS_POLICER_DBG_FUNC();

    core_frequency = sys_greatbelt_get_core_freq(0);

    for (i = 0; i < SYS_MAX_POLICER_SUPPORTED_FREQ_NUM; i++)
    {
        if (granularity[i].core_frequency == core_frequency)
        {
            sal_memcpy(&p_gb_qos_policer_master[lchip]->granularity, &granularity[i], sizeof(sys_qos_policer_granularity_t));
            break;
        }
    }

    if(i == SYS_MAX_POLICER_SUPPORTED_FREQ_NUM)
    {
        return CTC_E_INVALID_PARAM;
    }

    sal_memset(&policer_ctl, 0, sizeof(ipe_policing_ctl_t));
    sal_memset(&ipe_classification_ctl, 0, sizeof(ipe_classification_ctl_t));
    sal_memset(&epe_classification_ctl, 0, sizeof(epe_classification_ctl_t));


    policer_ctl.min_ptr = p_gb_qos_policer_master[lchip]->granularity.min_policer_ptr;
    policer_ctl.max_ptr = p_gb_qos_policer_master[lchip]->granularity.max_policer_ptr;
    policer_ctl.max_phy_ptr = policer_num - 1;
    policer_ctl.update_interval = 1;
    policer_ctl.ts_tick_gen_interval = p_gb_qos_policer_master[lchip]->granularity.tick_gen_interval;
    cmd = DRV_IOW(IpePolicingCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &policer_ctl));

    field_val = 1;
    cmd = DRV_IOW(IpePolicingCtl_t, IpePolicingCtl_TsTickGenEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    field_val = 1;
    cmd = DRV_IOW(IpePolicingCtl_t, IpePolicingCtl_UpdateEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    /* ingress port policer base */
    ipe_classification_ctl.port_policer_base = p_gb_qos_policer_master[lchip]->port_policer_base[CTC_INGRESS];
    ipe_classification_ctl.channel_policing_en31_0  = 0xFFFFFFFF;
    ipe_classification_ctl.channel_policing_en63_32 = 0xFFFFFFFF;
    cmd = DRV_IOW(IpeClassificationCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_classification_ctl));

    epe_classification_ctl.port_policer_base = p_gb_qos_policer_master[lchip]->port_policer_base[CTC_EGRESS];
    epe_classification_ctl.channel_policing_en31_0  = 0xFFFFFFFF;
    epe_classification_ctl.channel_policing_en63_32 = 0xFFFFFFFF;
    cmd = DRV_IOW(EpeClassificationCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_classification_ctl));


    return CTC_E_NONE;
}


/**
 @brief get policer id based on type
*/
STATIC uint16
_sys_greatbelt_qos_get_policer_id(uint8 lchip, uint8 type, uint16 plc_id, uint32 gport)
{

    uint16 policer_id = 0;
    switch(type)
    {
    case CTC_QOS_POLICER_TYPE_PORT:
        policer_id = (uint16)gport;
        break;
    case CTC_QOS_POLICER_TYPE_FLOW:
        policer_id = plc_id;
        break;
    case CTC_QOS_POLICER_TYPE_SERVICE:
        policer_id = plc_id;
        break;
    default:
        break;

    }

    return policer_id;
}


/**
 @brief Get policer granularity according to the rate.
*/
STATIC int32
_sys_greatbelt_qos_get_policer_gran_by_rate(uint8 lchip, uint32 rate, uint32* granularity)
{
    int i = 0;

    SYS_QOS_POLICER_DBG_FUNC();
    CTC_PTR_VALID_CHECK(granularity);

    for (i = 0; i < SYS_MAX_POLICER_GRANULARITY_RANGE_NUM; i++)
    {
        if (p_gb_qos_policer_master[lchip]->granularity.range[i].is_valid)
        {
            if ((rate >= ((uint64)p_gb_qos_policer_master[lchip]->granularity.range[i].min_rate * 1000))
                && (rate <= ((uint64)p_gb_qos_policer_master[lchip]->granularity.range[i].max_rate * 1000)))
            {
                *granularity = p_gb_qos_policer_master[lchip]->granularity.range[i].granularity;
                return CTC_E_NONE;
            }
        }
    }

    if (i == SYS_MAX_POLICER_GRANULARITY_RANGE_NUM)
    {
        return CTC_E_UNEXPECT;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_qos_policer_get_count_shift(uint8 lchip, uint32 gran, uint8* p_count_shift)
{
    uint8 shift       = 0;
    uint32 gran_bech  = 0;
    uint8 is_positive = 0;

    SYS_QOS_POLICER_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_count_shift);

    is_positive = (gran < SYS_QOS_POLICER_BENCH_GRAN) ? 0 : 1;

    for (shift = 0; shift < 8; shift++)
    {
        gran_bech = (is_positive) ? (gran >> shift) : (gran << shift);

        if (gran_bech == SYS_QOS_POLICER_BENCH_GRAN)
        {
            *p_count_shift = shift;
        }
    }

    if (0 == is_positive)
    {
        CTC_BIT_SET(*p_count_shift, 3);
    }

    return CTC_E_NONE;

}

STATIC int32
_sys_greatbelt_qos_policer_get_threshold_shift(uint8 lchip, uint32 burst, uint32 gran, uint16* p_threshold, uint8* p_shift)
{
    uint8 i           = 0;
    uint32 value      = 0;
    uint8 count_shift = 0;
    uint32 base       = 0;
    static uint16 policer_exp[16][2] =
    {
        {0,               0},
        {(1 << 0),    (1 << 1) - 1},
        {(1 << 1),    (1 << 2) - 1},
        {(1 << 2),    (1 << 3) - 1},
        {(1 << 3),    (1 << 4) - 1},
        {(1 << 4),    (1 << 5) - 1},
        {(1 << 5),    (1 << 6) - 1},
        {(1 << 6),    (1 << 7) - 1},
        {(1 << 7),    (1 << 8) - 1},
        {(1 << 8),    (1 << 9) - 1},
        {(1 << 9),    (1 << 10) - 1},
        {(1 << 10),  (1 << 11) - 1},
        {(1 << 11),  (1 << 12) - 1},
        {(1 << 12),  (1 << 13) - 1},
        {(1 << 13),  (1 << 14) - 1},
        {(1 << 14),  (1 << 15) - 1}
    };

    SYS_QOS_POLICER_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_threshold);
    CTC_PTR_VALID_CHECK(p_shift);

    _sys_greatbelt_qos_policer_get_count_shift(lchip, gran, &count_shift);

    if (IS_BIT_SET(count_shift, 3))
    {
        burst = (burst << (count_shift & 0x7));
    }
    else
    {
        burst = (burst >> (count_shift & 0x7));
    }

    SYS_QOS_POLICER_DBG_INFO("burst: %d\n", burst);
    CTC_MAX_VALUE_CHECK(burst, SYS_QOS_POLICER_MAX_TOKEN_SIZE);

    for (i = 0; i < 16; i++)
    {
        base = (burst >> i);

        if (base < 4096)
        {
            *p_threshold = base;
            *p_shift     = i;

            return CTC_E_NONE;
        }
    }

    value = burst / 4096;

    for (i = 0; i < 16; i++)
    {
        if ((value >= policer_exp[i][0]) && (value <= policer_exp[i][1]))
        {
            /* round up value */
            if ((burst + ((1 << i) - 1)) / (1 << i) >= 4096)
            {
                i++;
            }

            *p_shift = i;
            *p_threshold = (burst + ((1 << i) - 1)) / (1 << i);

            return CTC_E_NONE;
        }
    }

    return CTC_E_EXCEED_MAX_SIZE;
}


STATIC int32
_sys_greatbelt_qos_policer_map_token_rate_hw_to_user(uint8 lchip, uint32 hw_rate, uint8 count_shift,uint32 *user_rate)
{
    uint8 shift = 0;
    uint16 gran = 0;
    /*base :128k ,128k's count_shift is equal to 0*/

    shift = (count_shift&0x7);

    if (CTC_IS_BIT_SET(count_shift, 3))
    {
        gran = (SYS_QOS_POLICER_BENCH_GRAN >> shift);
    }
    else
    {
        gran  = (SYS_QOS_POLICER_BENCH_GRAN << shift);
    }

    *user_rate = hw_rate * gran;

    return CTC_E_NONE;
}


/**
 @brief Get prof according to the given policer data.
*/
STATIC int32
_sys_greatbelt_qos_policer_profile_build_data(uint8 lchip, ctc_qos_policer_t* p_policer,
                                              ds_policer_profile_t* p_ds_prof)
{
    uint32 value                 = 0;
    uint32 rate                  = 0;
    uint16 peak_threshold        = 0;
    uint8 peak_threshold_shift   = 0;
    uint16 commit_threshold      = 0;
    uint8 commit_threshold_shift = 0;
    uint8 count_shift            = 0;
    uint32 gran_cir              = 0;
    uint32 gran_pir              = 0;

    SYS_QOS_POLICER_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_policer);
    CTC_PTR_VALID_CHECK(p_ds_prof);

    if((0xFFFFFFFF == p_policer->policer.cir)
        && (0xFFFFFFFF == p_policer->policer.pir))
    {
        /*Reserve for max policer*/
        p_ds_prof->commit_rate  = 0xFFFF;
        p_ds_prof->peak_rate    = 0xFFFF;
        p_ds_prof->commit_count_shift       = 0;
        p_ds_prof->peak_count_shift         = 0;
        p_ds_prof->commit_threshold         = 0xFFF;
        p_ds_prof->commit_threshold_shift   = 0xF;
        p_ds_prof->peak_threshold           = 0xFFF;
        p_ds_prof->peak_threshold_shift     = 0xF;
        p_ds_prof->commit_rate_max          = 0xFFFF;
        p_ds_prof->peak_rate_max            = 0xFFFF;
    }
    else
    {
        rate = p_policer->policer.cir;
        /* get granularity*/
        CTC_ERROR_RETURN(_sys_greatbelt_qos_get_policer_gran_by_rate(lchip, rate, &gran_cir));

        if (p_policer->hbwp_en == 1)
        {
            gran_cir = 64;
        }

        SYS_QOS_POLICER_DBG_INFO("cir granularity : %d\n", gran_cir);

        /* compute commit rate(CIR) */
        value = p_policer->policer.cir / gran_cir;
        p_ds_prof->commit_rate = value;
        SYS_QOS_POLICER_DBG_INFO(" p_ds_prof->commit_rate : %d\n",  p_ds_prof->commit_rate);

        /* compute count shift */
        CTC_ERROR_RETURN(_sys_greatbelt_qos_policer_get_count_shift(lchip, gran_cir, &count_shift));
        p_ds_prof->commit_count_shift = (count_shift & 0xF);
        SYS_QOS_POLICER_DBG_INFO(" p_ds_prof->commit_count_shift : %d\n",  p_ds_prof->commit_count_shift);

        /* compute CBS threshold and threshold shift */
        if (CTC_MAX_UINT32_VALUE != p_policer->policer.cbs)
        {
            value = (p_policer->policer.cbs * 1000) / 8;
            CTC_ERROR_RETURN(
            _sys_greatbelt_qos_policer_get_threshold_shift(lchip, value,
                                                           gran_cir,
                                                           &commit_threshold,
                                                           &commit_threshold_shift));
            p_ds_prof->commit_threshold = commit_threshold;
            p_ds_prof->commit_threshold_shift = commit_threshold_shift;
        }
        else
        {
            /*policer token cnt maxsize is 0x7FFFFF*/
            p_ds_prof->commit_threshold = 0xFFF;
            p_ds_prof->commit_threshold_shift = 0xB;
        }
        SYS_QOS_POLICER_DBG_INFO("CommitCout: %d\n", (commit_threshold << commit_threshold_shift));


        rate = p_policer->policer.pir;
        /* get granularity*/
        CTC_ERROR_RETURN(_sys_greatbelt_qos_get_policer_gran_by_rate(lchip, rate, &gran_pir));

        if (p_policer->hbwp_en == 1)
        {
            gran_pir = 64;
        }

        /* compute peak rate(PIR or EIR)*/
        value = p_policer->policer.pir / gran_pir;
        p_ds_prof->peak_rate = value;
        SYS_QOS_POLICER_DBG_INFO(" p_ds_prof->peak_rate : %d\n",  p_ds_prof->peak_rate);

        /* compute count shift */
        CTC_ERROR_RETURN(_sys_greatbelt_qos_policer_get_count_shift(lchip, gran_pir, &count_shift));
        p_ds_prof->peak_count_shift = (count_shift & 0xF);

        SYS_QOS_POLICER_DBG_INFO(" p_ds_prof->peak_count_shift : %d\n",  p_ds_prof->peak_count_shift);



        /* compute EBS peak threshold and threshold shift */
        if (CTC_MAX_UINT32_VALUE != p_policer->policer.pbs)
        {
            value = (p_policer->policer.pbs * 1000) / 8;
            CTC_ERROR_RETURN(
            _sys_greatbelt_qos_policer_get_threshold_shift(lchip, value,
                                                           gran_pir,
                                                           &peak_threshold,
                                                           &peak_threshold_shift));
            p_ds_prof->peak_threshold = peak_threshold;
            p_ds_prof->peak_threshold_shift = peak_threshold_shift;
        }
        else
        {
            /*policer token cnt maxsize is 0x7FFFFF*/
            p_ds_prof->peak_threshold = 0xFFF;
            p_ds_prof->peak_threshold_shift = 0xB;
        }
        SYS_QOS_POLICER_DBG_INFO("PeakCout: %d\n", (peak_threshold << peak_threshold_shift));


        if (p_policer->hbwp_en == 1)
        {
            /* compute commit rate(CIR) */
            value = p_policer->hbwp.cir_max / gran_cir;
            p_ds_prof->commit_rate_max = value;
            SYS_QOS_POLICER_DBG_INFO("cir_max: %d\n",  p_ds_prof->commit_rate_max);

            /* compute peak rate(PIR) */
            value = p_policer->hbwp.pir_max / gran_pir;
            p_ds_prof->peak_rate_max = value;
            SYS_QOS_POLICER_DBG_INFO("pir_max: %d\n",  p_ds_prof->peak_rate_max);
        }
    }


    return CTC_E_NONE;
}

/**
 @brief Write policer to asic.
*/
STATIC int32
_sys_greatbelt_qos_policer_add_to_asic(uint8 lchip,
                                       ctc_qos_policer_t* p_policer_param,
                                       sys_qos_policer_t* p_sys_policer,
                                       sys_qos_policer_profile_t* p_sys_prof)
{
    ds_policer_control_t ds_policer_control;
    ds_policer_count_t ds_policer_count;
    uint32 policer_ptr = 0;
    uint32 cmd         = 0;
    int32  ret         = 0;
    uint32 index       = 0;
    uint8 mem_index    = 0;
    uint8 profile_id = 0;
    uint8 stats_en = 0;
    uint8 color_mode  = 0;
    uint8 color_drop  = 0;
    uint8 use_len  = 0;
    uint8 sr_tcm  = 0;
    uint8 cf  = 0;
    uint8 sf  = 0;
    uint8 rfc4115  = 0;
    uint32 cir_cnt = 0;
    uint32 pir_cnt = 0;

    SYS_QOS_POLICER_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_policer_param);
    CTC_PTR_VALID_CHECK(p_sys_policer);
    CTC_PTR_VALID_CHECK(p_sys_prof);

    policer_ptr = p_sys_policer->policer_ptr;
    index = policer_ptr / 4;

    if (p_sys_policer->hbwp_en)
    {
        mem_index = p_policer_param->hbwp.cos_index;
    }
    else
    {
        mem_index = policer_ptr % 4;
    }

    cmd = DRV_IOR(DsPolicerControl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_policer_control));

    cmd = DRV_IOR(DsPolicerCount_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_policer_count));

    profile_id = p_sys_policer->p_sys_profile[mem_index]->profile_id;
    stats_en   = p_policer_param->policer.stats_en;
    color_mode = p_policer_param->policer.is_color_aware ? 0 : 1;
    if(p_policer_param->policer.drop_color == CTC_QOS_COLOR_NONE)
    {
        color_drop = 1;
    }
    else if(p_policer_param->policer.drop_color == CTC_QOS_COLOR_RED)
    {
        color_drop = 2;
    }
    else if(p_policer_param->policer.drop_color == CTC_QOS_COLOR_YELLOW)
    {
        color_drop = 3;
    }
    use_len    = p_policer_param->policer.use_l3_length ? 1 : 0;

    p_sys_policer->stats_en = stats_en;

    if (p_policer_param->policer.policer_mode == CTC_QOS_POLICER_MODE_RFC2697)
    {
        sr_tcm = 1;
    }

    if (p_policer_param->policer.policer_mode == CTC_QOS_POLICER_MODE_MEF_BWP)
    {
        cf = p_policer_param->policer.cf_en?1:0;
        rfc4115    = 1;
    }

    if (p_policer_param->policer.policer_mode == CTC_QOS_POLICER_MODE_RFC4115)
    {
        rfc4115    = 1;
    }

    if (p_policer_param->hbwp_en)
    {
        sf  = p_policer_param->hbwp.sf_en?1:0;
        ds_policer_control.tc_sp_en = p_policer_param->hbwp.sp_en;
        ds_policer_control.tp_sp_en = p_policer_param->hbwp.sp_en;
        ds_policer_control.weight   =  p_policer_param->hbwp.weight;
        ds_policer_control.coupling_flag_total = p_policer_param->hbwp.cf_total_dis?0:1;;
    }

    cir_cnt = (p_sys_prof->profile.commit_threshold << p_sys_prof->profile.commit_threshold_shift);
    pir_cnt = (p_sys_prof->profile.peak_threshold << p_sys_prof->profile.peak_threshold_shift);

    switch(mem_index)
    {
    case 0:
        ds_policer_control.profile0           = profile_id;
        ds_policer_control.stats_en0          = stats_en;
        ds_policer_control.color_blind_mode0  = color_mode;
        ds_policer_control.color_drop_code0   = color_drop;
        ds_policer_control.use_layer3_length0 = use_len;
        ds_policer_control.sr_tcm_mode0       = sr_tcm;
        ds_policer_control.coupling_flag0     = cf;
        ds_policer_control.share_flag0        = sf;
        ds_policer_control.rfc4115_mode0      = rfc4115;

        ds_policer_count.commit_count0 = cir_cnt;
        ds_policer_count.peak_count0   = pir_cnt;
        break;

    case 1:
        ds_policer_control.profile1           = profile_id;
        ds_policer_control.stats_en1          = stats_en;
        ds_policer_control.color_blind_mode1  = color_mode;
        ds_policer_control.color_drop_code1   = color_drop;
        ds_policer_control.use_layer3_length1 = use_len;
        ds_policer_control.sr_tcm_mode1       = sr_tcm;
        ds_policer_control.coupling_flag1     = cf;
        ds_policer_control.share_flag1        = sf;
        ds_policer_control.rfc4115_mode1      = rfc4115;

        ds_policer_count.commit_count1 = cir_cnt;
        ds_policer_count.peak_count1   = pir_cnt;
        break;

    case 2:
        ds_policer_control.profile2           = profile_id;
        ds_policer_control.stats_en2          = stats_en;
        ds_policer_control.color_blind_mode2  = color_mode;
        ds_policer_control.color_drop_code2   = color_drop;
        ds_policer_control.use_layer3_length2 = use_len;
        ds_policer_control.sr_tcm_mode2       = sr_tcm;
        ds_policer_control.coupling_flag2     = cf;
        ds_policer_control.share_flag2        = sf;
        ds_policer_control.rfc4115_mode2      = rfc4115;

        ds_policer_count.commit_count2 = cir_cnt;
        ds_policer_count.peak_count2   = pir_cnt;

        break;

    case 3:
        ds_policer_control.profile3           = profile_id;
        ds_policer_control.stats_en3          = stats_en;
        ds_policer_control.color_blind_mode3  = color_mode;
        ds_policer_control.color_drop_code3   = color_drop;
        ds_policer_control.use_layer3_length3 = use_len;
        ds_policer_control.sr_tcm_mode3       = sr_tcm;
        ds_policer_control.coupling_flag3     = cf;
        ds_policer_control.share_flag3        = sf;
        ds_policer_control.rfc4115_mode3      = rfc4115;

        ds_policer_count.commit_count3 = cir_cnt;
        ds_policer_count.peak_count3   = pir_cnt;
        break;
    default:
        break;
    }

    cmd = DRV_IOW(DsPolicerControl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_policer_control));

    cmd = DRV_IOW(DsPolicerCount_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_policer_count));

    SYS_QOS_POLICER_DBG_INFO("Add to asic DsPolicerControl_t, lchip = %d, index = %d, mem_index = %d, profid = %d\n",
                             lchip, index, mem_index, p_sys_policer->p_sys_profile[mem_index]->profile_id);
    SYS_QOS_POLICER_DBG_INFO("========================================\n");
    SYS_QOS_POLICER_DBG_INFO("DsPolicerCount_CommitCount: 0x%x\n", (p_sys_prof->profile.commit_threshold << p_sys_prof->profile.commit_threshold_shift));
    SYS_QOS_POLICER_DBG_INFO("DsPolicerCount_PeakCount: 0x%x\n", (p_sys_prof->profile.peak_threshold << p_sys_prof->profile.peak_threshold_shift));

    return ret;
}

STATIC int32
_sys_greatbelt_qos_policer_remove_from_asic(uint8 lchip,
                                            ctc_qos_policer_t* p_policer_param,
                                            sys_qos_policer_t* p_sys_policer)
{
    ds_policer_control_t ds_policer_control;
    ds_policer_count_t ds_policer_count;
    uint32 policer_ptr = 0;
    uint32 cmd         = 0;
    int32  ret         = 0;
    uint32 index       = 0;
    uint8 mem_index    = 0;
    uint8 profile_id = 0;
    uint8 stats_en = 0;
    uint8 color_mode  = 0;
    uint8 color_drop  = 0;
    uint8 use_len  = 0;
    uint8 sr_tcm  = 0;
    uint8 cf  = 0;
    uint8 sf  = 0;
    uint8 rfc4115  = 0;
    uint32 cir_cnt = 0;
    uint32 pir_cnt = 0;

    SYS_QOS_POLICER_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_policer_param);
    CTC_PTR_VALID_CHECK(p_sys_policer);

    policer_ptr = p_sys_policer->policer_ptr;

    index = policer_ptr / 4;


    if (p_sys_policer->hbwp_en)
    {
        mem_index = p_policer_param->hbwp.cos_index;
    }
    else
    {
        mem_index = policer_ptr % 4;
    }


    cmd = DRV_IOR(DsPolicerControl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_policer_control));

    cmd = DRV_IOR(DsPolicerCount_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_policer_count));

    profile_id = 0;
    stats_en   = 0;
    color_mode = 0;
    color_drop = 0;
    use_len    = 0;
    sr_tcm     = 0;

    p_sys_policer->stats_en = stats_en;

    if (p_policer_param->policer.policer_mode == CTC_QOS_POLICER_MODE_MEF_BWP)
    {
        rfc4115    = 0;
    }

    if (p_policer_param->policer.policer_mode == CTC_QOS_POLICER_MODE_RFC4115)
    {
        rfc4115    = 0;
    }

    if (p_policer_param->hbwp_en)
    {
        sf  = 0;
        ds_policer_control.tc_sp_en = sf;
        ds_policer_control.tp_sp_en = sf;
        ds_policer_control.weight   = 0;
    }

    cir_cnt = 0;
    pir_cnt = 0;

    switch(mem_index)
    {
    case 0:
        ds_policer_control.profile0           = profile_id;
        ds_policer_control.stats_en0          = stats_en;
        ds_policer_control.color_blind_mode0  = color_mode;
        ds_policer_control.color_drop_code0   = color_drop;
        ds_policer_control.use_layer3_length0 = use_len;
        ds_policer_control.sr_tcm_mode0       = sr_tcm;
        ds_policer_control.coupling_flag0     = cf;
        ds_policer_control.share_flag0        = sf;
        ds_policer_control.rfc4115_mode0      = rfc4115;

        ds_policer_count.commit_count0 = cir_cnt;
        ds_policer_count.peak_count0   = pir_cnt;
        break;

    case 1:
        ds_policer_control.profile1           = profile_id;
        ds_policer_control.stats_en1          = stats_en;
        ds_policer_control.color_blind_mode1  = color_mode;
        ds_policer_control.color_drop_code1   = color_drop;
        ds_policer_control.use_layer3_length1 = use_len;
        ds_policer_control.sr_tcm_mode1       = sr_tcm;
        ds_policer_control.coupling_flag1     = cf;
        ds_policer_control.share_flag1        = sf;
        ds_policer_control.rfc4115_mode1      = rfc4115;

        ds_policer_count.commit_count1 = cir_cnt;
        ds_policer_count.peak_count1   = pir_cnt;
        break;

    case 2:
        ds_policer_control.profile2           = profile_id;
        ds_policer_control.stats_en2          = stats_en;
        ds_policer_control.color_blind_mode2  = color_mode;
        ds_policer_control.color_drop_code2   = color_drop;
        ds_policer_control.use_layer3_length2 = use_len;
        ds_policer_control.sr_tcm_mode2       = sr_tcm;
        ds_policer_control.coupling_flag2     = cf;
        ds_policer_control.share_flag2        = sf;
        ds_policer_control.rfc4115_mode2      = rfc4115;

        ds_policer_count.commit_count2 = cir_cnt;
        ds_policer_count.peak_count2   = pir_cnt;

        break;

    case 3:
        ds_policer_control.profile3           = profile_id;
        ds_policer_control.stats_en3          = stats_en;
        ds_policer_control.color_blind_mode3  = color_mode;
        ds_policer_control.color_drop_code3   = color_drop;
        ds_policer_control.use_layer3_length3 = use_len;
        ds_policer_control.sr_tcm_mode3       = sr_tcm;
        ds_policer_control.coupling_flag3     = cf;
        ds_policer_control.share_flag3        = sf;
        ds_policer_control.rfc4115_mode3      = rfc4115;

        ds_policer_count.commit_count3 = cir_cnt;
        ds_policer_count.peak_count3   = pir_cnt;
        break;
    default:
        break;
    }


    cmd = DRV_IOW(DsPolicerControl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_policer_control));

    cmd = DRV_IOW(DsPolicerCount_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_policer_count));

    SYS_QOS_POLICER_DBG_INFO("Remove from asic DsPolicerControl_t, lchip = %d, index = %d, mem_index = %d\n",
                             lchip, index, mem_index);
    SYS_QOS_POLICER_DBG_INFO("========================================\n");

    return ret;

}

/**
 @brief Write policer to asic.
*/
STATIC int32
_sys_greatbelt_qos_policer_profile_add_to_asic(uint8 lchip,
                                               sys_qos_policer_profile_t* p_sys_profile)
{
    uint32 cmd       = 0;
    int32  ret       = 0;
    uint32 field_val = 0;

    SYS_QOS_POLICER_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_sys_profile);

    field_val = p_sys_profile->profile_mem_id;
    cmd = DRV_IOW(PolicingMiscCtl_t, PolicingMiscCtl_DsPolicerProfileMemSel_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    cmd = DRV_IOW(DsPolicerProfile_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_sys_profile->profile_id, cmd, &p_sys_profile->profile));

    SYS_QOS_POLICER_DBG_INFO("write DsPolicerProfile_t, lchip = %d, mem_index = %d, profid = %d\n",
                             lchip,  p_sys_profile->profile_mem_id, p_sys_profile->profile_id);
    SYS_QOS_POLICER_DBG_INFO("================================================================\n");
    SYS_QOS_POLICER_DBG_INFO("commit_count_shift     : %d\n", p_sys_profile->profile.commit_count_shift);
    SYS_QOS_POLICER_DBG_INFO("commit_rate            : %d\n", p_sys_profile->profile.commit_rate);
    SYS_QOS_POLICER_DBG_INFO("commit_threshold_shift : %d\n", p_sys_profile->profile.commit_threshold_shift);
    SYS_QOS_POLICER_DBG_INFO("commit_threshold       : %d\n", p_sys_profile->profile.commit_threshold);
    SYS_QOS_POLICER_DBG_INFO("peak_count_shift       : %d\n", p_sys_profile->profile.peak_count_shift);
    SYS_QOS_POLICER_DBG_INFO("peak_rate              : %d\n", p_sys_profile->profile.peak_rate);
    SYS_QOS_POLICER_DBG_INFO("peak_threshold_shift   : %d\n", p_sys_profile->profile.peak_threshold_shift);
    SYS_QOS_POLICER_DBG_INFO("peak_threshold         : %d\n", p_sys_profile->profile.peak_threshold);
    SYS_QOS_POLICER_DBG_INFO("commit_rate_max        : %d\n", p_sys_profile->profile.commit_rate_max);
    SYS_QOS_POLICER_DBG_INFO("peak_rate_max          : %d\n", p_sys_profile->profile.peak_rate_max);

    return ret;
}

STATIC int32
_sys_greatbelt_qos_policer_profile_remove_from_asic(uint8 lchip,
                                                    uint8 profile_mem_id, uint8 profile_id)
{
    /*only remove from SW DB*/
    return CTC_E_NONE;
}

/**
 @brief Lookup policer in hash table.
*/
STATIC int32
_sys_greatbelt_qos_policer_lookup(uint8 lchip, uint8 type,
                                  uint8 dir,
                                  uint16 policer_id,
                                  sys_qos_policer_t** pp_sys_policer)
{
    sys_qos_policer_t sys_policer;

    SYS_QOS_POLICER_DBG_FUNC();
    CTC_PTR_VALID_CHECK(pp_sys_policer);

    sys_policer.type = type;
    sys_policer.dir = dir;
    sys_policer.id = policer_id;

    *pp_sys_policer = ctc_hash_lookup(p_gb_qos_policer_master[lchip]->p_policer_hash, &sys_policer);

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_qos_policer_alloc_offset(uint8 lchip, ctc_qos_policer_t* p_policer_param,
                                                 sys_qos_policer_t* p_sys_policer)
{
    SYS_QOS_POLICER_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_sys_policer);

    if (p_sys_policer->type == CTC_QOS_POLICER_TYPE_PORT)
    {
        uint8 lport = CTC_MAP_GPORT_TO_LPORT(p_sys_policer->id);
        uint8 dir = p_sys_policer->dir;
        uint8 cos_index_offset = 0;
        p_sys_policer->policer_ptr = SYS_QOS_PORT_POLICER_INDEX(lport, dir, cos_index_offset);
    }
    else
    {
        uint32 offset = 0;
        sys_greatbelt_opf_t opf = {0};
        opf.pool_index = 0;
        opf.pool_type = OPF_QOS_FLOW_POLICER;

        if (p_policer_param->hbwp_en)
        {
            opf.multiple = 4;
            p_sys_policer->entry_size = 4;
        }
        else
        {
            opf.multiple = 1;
            p_sys_policer->entry_size = 1;
        }

        CTC_ERROR_RETURN(sys_greatbelt_opf_alloc_offset(lchip, &opf, p_sys_policer->entry_size, &offset));
        p_sys_policer->policer_ptr = offset;

    }

    SYS_QOS_POLICER_DBG_INFO("Alloc policer offset = %d\n", p_sys_policer->policer_ptr);

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_qos_policer_free_offset(uint8 lchip, sys_qos_policer_t* p_sys_policer)
{

    CTC_PTR_VALID_CHECK(p_sys_policer);

    if (p_sys_policer->type != CTC_QOS_POLICER_TYPE_PORT)
    {
        uint32 offset = p_sys_policer->policer_ptr;
        sys_greatbelt_opf_t opf = {0};
        opf.pool_index = 0;
        opf.pool_type = OPF_QOS_FLOW_POLICER;

        CTC_ERROR_RETURN(sys_greatbelt_opf_free_offset(lchip, &opf, p_sys_policer->entry_size, offset));
    }

    SYS_QOS_POLICER_DBG_INFO("Free policer offset = %d\n", p_sys_policer->policer_ptr);

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_qos_policer_profile_alloc_offset(uint8 lchip,
                                                uint8 profile_mem_id,
                                                uint16* offset)
{
    uint32 offset_tmp = 0;
    sys_greatbelt_opf_t opf = {0};

    SYS_QOS_POLICER_DBG_FUNC();

    opf.pool_index = 0;
    opf.pool_type = OPF_QOS_POLICER_PROFILE0 + profile_mem_id;
    CTC_ERROR_RETURN(sys_greatbelt_opf_alloc_offset(lchip, &opf, 1, &offset_tmp));
    *offset = offset_tmp;

    SYS_QOS_POLICER_DBG_INFO("profile alloc offset = %d\n", *offset);

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_qos_policer_profile_free_offset(uint8 lchip,
                                               uint8 profile_mem_id,
                                               uint16 offset)
{
    uint32 offset_tmp = offset;
    sys_greatbelt_opf_t opf = {0};

    SYS_QOS_POLICER_DBG_FUNC();

    opf.pool_index = 0;
    opf.pool_type = OPF_QOS_POLICER_PROFILE0 + profile_mem_id;
    CTC_ERROR_RETURN(sys_greatbelt_opf_free_offset(lchip, &opf, 1, offset_tmp));

    SYS_QOS_POLICER_DBG_INFO("profile free offset = %d\n", offset_tmp);

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_qos_policer_profile_remove(uint8 lchip,
                                          sys_qos_policer_profile_t* p_sys_profile_old)
{
    sys_qos_policer_profile_t* p_sys_profile_find = NULL;
    ctc_spool_t* p_profile_pool               = NULL;
    int32 ret            = 0;
    uint16 profile_id    = 0;
    uint8 profile_mem_id = 0;

    SYS_QOS_POLICER_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_sys_profile_old);

    profile_mem_id = p_sys_profile_old->profile_mem_id;
    p_profile_pool = p_gb_qos_policer_master[lchip]->p_profile_pool[profile_mem_id];

    p_sys_profile_find = ctc_spool_lookup(p_profile_pool, p_sys_profile_old);
    if (NULL == p_sys_profile_find)
    {
        SYS_QOS_POLICER_DBG_ERROR("p_sys_profile_find no found !!!!!!!!\n");
        return CTC_E_ENTRY_NOT_EXIST;
    }

    ret = ctc_spool_remove(p_profile_pool, p_sys_profile_old, NULL);
    if (ret < 0)
    {
        SYS_QOS_POLICER_DBG_ERROR("ctc_spool_remove fail!!!!!!!!\n");
        return CTC_E_SPOOL_REMOVE_FAILED;
    }

    if (ret == CTC_SPOOL_E_OPERATE_MEMORY)
    {
        profile_id = p_sys_profile_find->profile_id;
        CTC_ERROR_RETURN(_sys_greatbelt_qos_policer_profile_remove_from_asic(lchip, profile_mem_id, profile_id));
        /*free offset*/
        CTC_ERROR_RETURN(_sys_greatbelt_qos_policer_profile_free_offset(lchip, profile_mem_id, profile_id));
        mem_free(p_sys_profile_find);
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_qos_policer_profile_add(uint8 lchip,
                                       ctc_qos_policer_t* p_policer_param,
                                       sys_qos_policer_t* p_sys_policer,
                                       sys_qos_policer_profile_t** pp_sys_profile_get)
{
    ctc_spool_t* p_profile_pool    = NULL;
    sys_qos_policer_profile_t* p_sys_profile_old = NULL;
    sys_qos_policer_profile_t* p_sys_profile_new = NULL;
    int32 ret            = 0;
    uint16 profile_id    = 0;
    uint16 old_profile_id = 0;
    uint8 profile_mem_id = 0;

    SYS_QOS_POLICER_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_policer_param);
    CTC_PTR_VALID_CHECK(p_sys_policer);
    CTC_PTR_VALID_CHECK(pp_sys_profile_get);

    p_sys_profile_new = mem_malloc(MEM_QUEUE_MODULE, sizeof(sys_qos_policer_profile_t));
    if (NULL == p_sys_profile_new)
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_sys_profile_new, 0, sizeof(sys_qos_policer_profile_t));

    if (p_policer_param->hbwp_en == 1)
    {
        profile_mem_id = p_policer_param->hbwp.cos_index;
        CTC_BIT_SET(p_sys_policer->hbwp_en, profile_mem_id);
    }
    else
    {
        profile_mem_id = p_sys_policer->policer_ptr % 4;
    }

    p_sys_profile_new->profile_mem_id = profile_mem_id;

    ret = _sys_greatbelt_qos_policer_profile_build_data(lchip, p_policer_param, &p_sys_profile_new->profile);
    if (CTC_E_NONE != ret)
    {
        goto ERROR_RETURN;
    }

    p_sys_profile_old = p_sys_policer->p_sys_profile[profile_mem_id];
    p_profile_pool = p_gb_qos_policer_master[lchip]->p_profile_pool[profile_mem_id];

    SYS_QOS_POLICER_DBG_INFO("profile_mem_id = %d\n", profile_mem_id);

    if (p_sys_profile_old)
    {
        if(TRUE == _sys_greatbelt_policer_profile_hash_cmp(p_sys_profile_new, p_sys_profile_old))
        {
            *pp_sys_profile_get = p_sys_profile_old;
            mem_free(p_sys_profile_new);
            return CTC_E_NONE;
        }
        old_profile_id = p_sys_profile_old->profile_id;
        p_sys_profile_new->profile_id = old_profile_id;
        SYS_QOS_POLICER_DBG_INFO("update old profile !!!!!!!!!!!!\n");
        ret = ctc_spool_add(p_profile_pool,
                               p_sys_profile_new,
                               p_sys_profile_old,
                               pp_sys_profile_get);
    }
    else
    {

        SYS_QOS_POLICER_DBG_INFO("add new profile !!!!!!!!!!!!\n");
        ret = ctc_spool_add(p_profile_pool,
                            p_sys_profile_new,
                            NULL,
                            pp_sys_profile_get);

    }

    if (ret != CTC_SPOOL_E_OPERATE_MEMORY)
    {
        mem_free(p_sys_profile_new);
    }

    if (ret < 0)
    {
        ret = CTC_E_SPOOL_ADD_UPDATE_FAILED;
        goto ERROR_RETURN;
    }
    else
    {
        if (ret == CTC_SPOOL_E_OPERATE_MEMORY)
        {
            ret = _sys_greatbelt_qos_policer_profile_alloc_offset(lchip, profile_mem_id, &profile_id);
            if (ret < 0)
            {
                ctc_spool_remove(p_profile_pool, p_sys_profile_new, NULL);
                goto ERROR_RETURN;
            }

            (*pp_sys_profile_get)->profile_id = profile_id;

        }
    }

    /*key is found, so there is an old ad need to be deleted.*/
    if (p_sys_profile_old && (*pp_sys_profile_get)->profile_id != old_profile_id)
    {
        ret = _sys_greatbelt_qos_policer_profile_remove(lchip, p_sys_profile_old);
        if (ret < 0)
        {
            ctc_spool_remove(p_profile_pool, p_sys_profile_new, NULL);
            goto ERROR_RETURN;
        }
    }

    p_sys_policer->p_sys_profile[profile_mem_id] = *pp_sys_profile_get;
    SYS_QOS_POLICER_DBG_INFO("profile_mem_id = %d, profile_id = %d\n",
                             profile_mem_id,
                             (*pp_sys_profile_get)->profile_id);

    return CTC_E_NONE;

ERROR_RETURN:
    mem_free(p_sys_profile_new);
    return ret;

}

int32
_sys_greatbelt_qos_policer_build_node(uint8 lchip, ctc_qos_policer_t* p_policer_param,
                                      sys_qos_policer_t** pp_sys_policer)
{
    sys_qos_policer_t* p_sys_policer = NULL;
    uint16 policer_id = 0;

    SYS_QOS_POLICER_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_policer_param);
    CTC_PTR_VALID_CHECK(pp_sys_policer);

    policer_id = _sys_greatbelt_qos_get_policer_id(lchip, p_policer_param->type,
                                                   p_policer_param->id.policer_id, p_policer_param->id.gport);

    /*new policer node*/
    p_sys_policer = mem_malloc(MEM_QUEUE_MODULE, sizeof(sys_qos_policer_t));
    if (NULL == p_sys_policer)
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_sys_policer, 0, sizeof(sys_qos_policer_t));

    p_sys_policer->type = p_policer_param->type;
    p_sys_policer->dir = p_policer_param->dir;
    p_sys_policer->id = policer_id;

    ctc_hash_insert(p_gb_qos_policer_master[lchip]->p_policer_hash, p_sys_policer);

    *pp_sys_policer = p_sys_policer;

    return CTC_E_NONE;

}

int32
_sys_greatbelt_qos_policer_delete_node(uint8 lchip, sys_qos_policer_t* p_sys_policer)
{
    SYS_QOS_POLICER_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_sys_policer);

    ctc_hash_remove(p_gb_qos_policer_master[lchip]->p_policer_hash, p_sys_policer);

    mem_free(p_sys_policer);
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_qos_policer_add(uint8 lchip, ctc_qos_policer_t* p_policer_param,
                               sys_qos_policer_t* p_sys_policer)
{
    sys_qos_policer_profile_t* p_sys_profile_new = NULL;
    int32 ret = 0;
    uint8 first_add = 0;

    SYS_QOS_POLICER_DBG_FUNC();

    if (NULL == p_sys_policer)
    {
        /*build policer node*/
        CTC_ERROR_RETURN(_sys_greatbelt_qos_policer_build_node(lchip, p_policer_param, &p_sys_policer));

        /*alloc policer offset*/
        ret = _sys_greatbelt_qos_policer_alloc_offset(lchip, p_policer_param ,p_sys_policer);
        if (CTC_E_NONE != ret)
        {   /*--------------roll back ------------------------*/
            _sys_greatbelt_qos_policer_delete_node(lchip, p_sys_policer);
            return ret;
        }

        first_add = 1;
    }
#if 0
    if (CTC_QOS_POLICER_TYPE_PORT == p_policer_param->type)
    {
        gchip = CTC_MAP_GPORT_TO_GCHIP(p_policer_param->id.gport);
        sys_greatbelt_chip_is_local(lchip, gchip);
        CTC_BIT_SET(lchip_bitmap, lchip);
    }
    else
    {
        lchip_bitmap = 0xFF;
    }

#endif
    /*write policer profile and table*/

    /*add policer prof*/
    ret = _sys_greatbelt_qos_policer_profile_add(lchip, p_policer_param, p_sys_policer, &p_sys_profile_new);

    /*write policer prof*/
    ret = ret ? ret : _sys_greatbelt_qos_policer_profile_add_to_asic(lchip, p_sys_profile_new);

    /*write policer ctl and count*/
    ret = ret ? ret : _sys_greatbelt_qos_policer_add_to_asic(lchip, p_policer_param, p_sys_policer, p_sys_profile_new);


    if (CTC_E_NONE != ret && first_add)
    {   /*--------------roll back ------------------------*/
        /*this have problem because profile no rool back*/
        _sys_greatbelt_qos_policer_free_offset(lchip, p_sys_policer);
        _sys_greatbelt_qos_policer_delete_node(lchip, p_sys_policer);
        return ret;
    }

    sal_memcpy(&p_sys_policer->policer, &p_policer_param->policer, sizeof(ctc_qos_policer_param_t));
    if (p_policer_param->hbwp_en)
    {
        CTC_BIT_SET(p_sys_policer->hbwp_en, p_policer_param->hbwp.cos_index);
        sal_memcpy(&p_sys_policer->hbwp[p_policer_param->hbwp.cos_index],
                   &p_policer_param->hbwp,
                   sizeof(ctc_qos_policer_hbwp_t));
    }

    return ret;

}

STATIC int32
_sys_greatbelt_qos_policer_remove(uint8 lchip, ctc_qos_policer_t* p_policer_param,
                                  sys_qos_policer_t* p_sys_policer)
{
    sys_qos_policer_profile_t* p_sys_profile_old = {NULL};
    uint8 profile_mem_id    = 0;
    int32 ret               = 0;
    uint8 cos_index         = 0;

    SYS_QOS_POLICER_DBG_FUNC();

    if (NULL == p_sys_policer)
    {
        return CTC_E_QOS_POLICER_NOT_EXIST;
    }

    if (p_sys_policer->hbwp_en && (!p_policer_param->hbwp_en))
    {
        return CTC_E_INVALID_PARAM;
    }

    if (p_policer_param->hbwp_en == 1)
    {
        cos_index = p_policer_param->hbwp.cos_index;
        CTC_MAX_VALUE_CHECK(cos_index, p_sys_policer->entry_size-1);
        if (!CTC_IS_BIT_SET(p_sys_policer->hbwp_en, cos_index))
        {
            return CTC_E_INVALID_PARAM;
        }
    }

    CTC_ERROR_RETURN(
    _sys_greatbelt_qos_policer_remove_from_asic(lchip, p_policer_param, p_sys_policer));

    if (IS_BIT_SET(p_sys_policer->hbwp_en, p_policer_param->hbwp.cos_index))
    {
        profile_mem_id = p_policer_param->hbwp.cos_index;
        p_sys_profile_old = p_sys_policer->p_sys_profile[profile_mem_id];
        p_sys_policer->p_sys_profile[profile_mem_id] = NULL;
    }
    else
    {
        profile_mem_id = p_sys_policer->policer_ptr % 4;
        p_sys_profile_old = p_sys_policer->p_sys_profile[profile_mem_id];
        p_sys_policer->p_sys_profile[profile_mem_id] = NULL;
    }

    CTC_ERROR_RETURN(
    _sys_greatbelt_qos_policer_profile_remove(lchip, p_sys_profile_old));


    if (p_sys_policer->hbwp_en)
    {
        CTC_BIT_UNSET(p_sys_policer->hbwp_en, p_policer_param->hbwp.cos_index);
    }

    SYS_QOS_POLICER_DBG_INFO("p_sys_policer->hbwp_en:0x%x !!!!!!!!!!!!\n", p_sys_policer->hbwp_en);

    if (p_sys_policer->hbwp_en == 0)
    {
        /*free policer offset*/
        CTC_ERROR_RETURN(
        _sys_greatbelt_qos_policer_free_offset(lchip, p_sys_policer));

        /*free policer node*/
        CTC_ERROR_RETURN(
        _sys_greatbelt_qos_policer_delete_node(lchip, p_sys_policer));


        SYS_QOS_POLICER_DBG_INFO("remove policer !!!!!!!!!!!!\n");
    }

    return ret;

}

STATIC int32
_sys_greatbelt_qos_set_policer_en(uint8 lchip, ctc_qos_policer_t* p_policer_param)
{
    int32 ret = 0;

    SYS_QOS_POLICER_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_policer_param);

    if (CTC_QOS_POLICER_TYPE_PORT == p_policer_param->type)
    {
        ret = sys_greatbelt_port_set_direction_property(lchip, p_policer_param->id.gport,
                                                        CTC_PORT_DIR_PROP_PORT_POLICER_VALID,
                                                        p_policer_param->dir,
                                                        p_policer_param->enable);
    }

    return ret;
}

int32
_sys_greatbelt_qos_policer_policer_id_check(uint8 lchip, uint8 type, uint8 dir, uint16 policer_id)
{

    SYS_QOS_POLICER_DBG_PARAM("type           = %d\n", type);
    SYS_QOS_POLICER_DBG_PARAM("dir            = %d\n", dir);
    SYS_QOS_POLICER_DBG_PARAM("policer_id     = %d\n", policer_id);

    SYS_QOS_POLICER_DBG_FUNC();

    /*direction check*/
    if (dir >= CTC_BOTH_DIRECTION)
    {
        return CTC_E_INVALID_DIR;
    }

    /*police type check*/
    if (CTC_QOS_POLICER_TYPE_PORT == type)
    {
        uint16 gport = 0;
        uint8 lport  = 0;

        gport = policer_id;
        SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);
        CTC_MAX_VALUE_CHECK(lport, (MAX_PORT_POLICER_NUM / 2 - 1));

    }
    else if (CTC_QOS_POLICER_TYPE_FLOW == type)
    {
        CTC_MIN_VALUE_CHECK(policer_id, 1);
    }
    else if (CTC_QOS_POLICER_TYPE_SERVICE == type)
    {
        CTC_MIN_VALUE_CHECK(policer_id, 1);
    }
    else
    {
        return CTC_E_NOT_SUPPORT;
    }

    return CTC_E_NONE;

}

STATIC int32
_sys_greatbelt_qos_policer_bucket_check(uint8 lchip, ctc_qos_policer_t* p_policer)
{
    uint32 rate                 = 0;
    uint32 value                = 0;
    uint8  count_shift          = 0;
    uint32 max_cir_bucket       = 0;
    uint32 max_pir_bucket       = 0;
    uint32 gran_cir             = 0;
    uint32 gran_pir             = 0;

    rate = p_policer->policer.cir;

    /* get granularity*/
    CTC_ERROR_RETURN(_sys_greatbelt_qos_get_policer_gran_by_rate(lchip, rate, &gran_cir));

    if (p_policer->hbwp_en == 1)
    {
        gran_cir = 64;
    }

    _sys_greatbelt_qos_policer_get_count_shift(lchip, gran_cir, &count_shift);

    if (IS_BIT_SET(count_shift, 3))
    {
        value = ((0xFFF<<0xB) >> (count_shift & 0x7));
    }
    else
    {
        value = ((0xFFF<<0xB) << (count_shift & 0x7));
    }
    max_cir_bucket = value *8;


    rate = p_policer->policer.pir;

    /* get granularity*/
    CTC_ERROR_RETURN(_sys_greatbelt_qos_get_policer_gran_by_rate(lchip, rate, &gran_pir));

    if (p_policer->hbwp_en == 1)
    {
        gran_pir = 64;
    }

    _sys_greatbelt_qos_policer_get_count_shift(lchip, gran_pir, &count_shift);

    if (IS_BIT_SET(count_shift, 3))
    {
        value = ((0xFFF<<0xB) >> (count_shift & 0x7));
    }
    else
    {
        value = ((0xFFF<<0xB) << (count_shift & 0x7));
    }
    max_pir_bucket = value *8;

    if ((p_policer->policer.cbs*1000) > max_cir_bucket)
    {
        p_policer->policer.cbs = CTC_MAX_UINT32_VALUE;
    }

    if ((p_policer->policer.pbs*1000) > max_pir_bucket)
    {
        p_policer->policer.pbs = CTC_MAX_UINT32_VALUE;
    }

    return CTC_E_NONE;

}

int32
_sys_greatbelt_qos_policer_param_check(uint8 lchip, ctc_qos_policer_t* p_policer_param)
{

    uint16 policer_id = 0;

    SYS_QOS_POLICER_DBG_PARAM("p_policer_param->type           = %d\n", p_policer_param->type);
    SYS_QOS_POLICER_DBG_PARAM("p_policer_param->dir            = %d\n", p_policer_param->dir);
    SYS_QOS_POLICER_DBG_PARAM("p_policer_param->enable         = %d\n", p_policer_param->enable);
    SYS_QOS_POLICER_DBG_PARAM("p_policer_param->policer_id     = %d\n", p_policer_param->id.policer_id);
    SYS_QOS_POLICER_DBG_PARAM("p_policer_param->use_l3_length  = %d\n", p_policer_param->policer.use_l3_length);
    SYS_QOS_POLICER_DBG_PARAM("p_policer_param->is_color_aware = %d\n", p_policer_param->policer.is_color_aware);
    SYS_QOS_POLICER_DBG_PARAM("p_policer_param->drop_color     = %d\n", p_policer_param->policer.drop_color);
    SYS_QOS_POLICER_DBG_PARAM("p_policer_param->cir            = %d\n", p_policer_param->policer.cir);
    SYS_QOS_POLICER_DBG_PARAM("p_policer_param->cbs            = %d\n", p_policer_param->policer.cbs);
    SYS_QOS_POLICER_DBG_PARAM("p_policer_param->pir            = %d\n", p_policer_param->policer.pir);
    SYS_QOS_POLICER_DBG_PARAM("p_policer_param->pbs            = %d\n", p_policer_param->policer.pbs);

    SYS_QOS_POLICER_DBG_PARAM("p_policer_param->cf_en          = %d\n", p_policer_param->policer.cf_en);
    SYS_QOS_POLICER_DBG_PARAM("p_policer_param->policer_mode   = %d\n", p_policer_param->policer.policer_mode);
    SYS_QOS_POLICER_DBG_PARAM("p_policer_param->hbwp_en        = %d\n", p_policer_param->hbwp_en);
    SYS_QOS_POLICER_DBG_PARAM("p_policer_param->sf_en          = %d\n", p_policer_param->hbwp.sf_en);
    SYS_QOS_POLICER_DBG_PARAM("p_policer_param->sp_en          = %d\n", p_policer_param->hbwp.sp_en);
    SYS_QOS_POLICER_DBG_PARAM("p_policer_param->cir_max        = %d\n", p_policer_param->hbwp.cir_max);
    SYS_QOS_POLICER_DBG_PARAM("p_policer_param->pir_max        = %d\n", p_policer_param->hbwp.pir_max);
    SYS_QOS_POLICER_DBG_PARAM("p_policer_param->cos_index      = %d\n", p_policer_param->hbwp.cos_index);


    SYS_QOS_POLICER_DBG_FUNC();

    policer_id = _sys_greatbelt_qos_get_policer_id(lchip, p_policer_param->type,
                                                   p_policer_param->id.policer_id, p_policer_param->id.gport);

    CTC_ERROR_RETURN(_sys_greatbelt_qos_policer_policer_id_check(lchip, p_policer_param->type,
                                                                 p_policer_param->dir,
                                                                 policer_id));


    if (!p_policer_param->enable)
    {
        return CTC_E_NONE;
    }

    if (p_policer_param->policer.policer_mode == CTC_QOS_POLICER_MODE_RFC2697 )
    {
        p_policer_param->policer.pir = p_policer_param->policer.cir;
    }

    CTC_MAX_VALUE_CHECK(p_policer_param->policer.drop_color, CTC_QOS_COLOR_YELLOW);
    CTC_MAX_VALUE_CHECK(p_policer_param->policer.policer_mode, CTC_QOS_POLICER_MODE_MEF_BWP);

    if (p_policer_param->hbwp_en)
    {
        if ((p_policer_param->type != CTC_QOS_POLICER_TYPE_SERVICE)
            && (p_policer_param->type != CTC_QOS_POLICER_TYPE_FLOW))
        {
            return CTC_E_INVALID_PARAM;
        }
        CTC_MAX_VALUE_CHECK(p_policer_param->hbwp.cos_index, 3);

        if (!p_policer_param->hbwp.sp_en)
        {
            CTC_MAX_VALUE_CHECK(p_policer_param->hbwp.weight, 1023);
        }

        CTC_MAX_VALUE_CHECK(p_policer_param->hbwp.cir_max, 10000000);
        CTC_MAX_VALUE_CHECK(p_policer_param->hbwp.pir_max, 10000000);
    }

    CTC_MAX_VALUE_CHECK(p_policer_param->policer.cir, 10000000);
    CTC_MAX_VALUE_CHECK(p_policer_param->policer.pir, 10000000);

    CTC_ERROR_RETURN(_sys_greatbelt_qos_policer_bucket_check(lchip, p_policer_param));

    if (p_policer_param->policer.policer_mode == CTC_QOS_POLICER_MODE_RFC2698)
    {
        if (p_policer_param->policer.pir < p_policer_param->policer.cir)
        {
            return CTC_E_QOS_POLICER_CIR_GREATER_THAN_PIR;
        }
    }

    if (p_policer_param->policer.policer_mode == CTC_QOS_POLICER_MODE_RFC2698)
    {
        if (p_policer_param->policer.pir < p_policer_param->policer.cir)
        {
            return CTC_E_QOS_POLICER_CIR_GREATER_THAN_PIR;
        }
        if (p_policer_param->policer.pbs < p_policer_param->policer.cbs)
        {
            return CTC_E_QOS_POLICER_CBS_GREATER_THAN_PBS;
        }
    }

    return CTC_E_NONE;
}

extern int32
sys_greatbelt_qos_policer_set(uint8 lchip, ctc_qos_policer_t* p_policer_param)
{
    sys_qos_policer_t* p_sys_policer = NULL;
    uint16 policer_id = 0;

    SYS_QOS_POLICER_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_policer_param);

    SYS_QOS_POLICER_LOCK(lchip);
    CTC_ERROR_RETURN_QOS_POLICER_UNLOCK(_sys_greatbelt_qos_policer_param_check(lchip, p_policer_param));

    policer_id = _sys_greatbelt_qos_get_policer_id(lchip, p_policer_param->type,
                                                   p_policer_param->id.policer_id, p_policer_param->id.gport);

    CTC_ERROR_RETURN_QOS_POLICER_UNLOCK(_sys_greatbelt_qos_policer_lookup(lchip, p_policer_param->type,
                                                       p_policer_param->dir,
                                                       policer_id,
                                                       &p_sys_policer));

    if (p_policer_param->enable)
    {
        CTC_ERROR_RETURN_QOS_POLICER_UNLOCK(_sys_greatbelt_qos_policer_add(lchip, p_policer_param, p_sys_policer));
        CTC_ERROR_RETURN_QOS_POLICER_UNLOCK(_sys_greatbelt_qos_set_policer_en(lchip, p_policer_param));
    }
    else
    {
        CTC_ERROR_RETURN_QOS_POLICER_UNLOCK(_sys_greatbelt_qos_set_policer_en(lchip, p_policer_param));
        CTC_ERROR_RETURN_QOS_POLICER_UNLOCK(_sys_greatbelt_qos_policer_remove(lchip, p_policer_param, p_sys_policer));
    }
    SYS_QOS_POLICER_UNLOCK(lchip);

    return CTC_E_NONE;
}

int32
sys_greatbelt_qos_policer_get(uint8 lchip, ctc_qos_policer_t* p_policer_param)
{
    sys_qos_policer_t* p_sys_policer = NULL;
    uint16 policer_id = 0;

    SYS_QOS_POLICER_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_policer_param);
    SYS_QOS_POLICER_DBG_PARAM("p_policer_param->type           = %d\n", p_policer_param->type);
    SYS_QOS_POLICER_DBG_PARAM("p_policer_param->dir            = %d\n", p_policer_param->dir);
    SYS_QOS_POLICER_DBG_PARAM("p_policer_param->policer_id     = %d\n", p_policer_param->id.policer_id);

    SYS_QOS_POLICER_LOCK(lchip);
    policer_id = _sys_greatbelt_qos_get_policer_id(lchip, p_policer_param->type,
                                                   p_policer_param->id.policer_id, p_policer_param->id.gport);

    CTC_ERROR_RETURN_QOS_POLICER_UNLOCK(_sys_greatbelt_qos_policer_lookup(lchip, p_policer_param->type,
                                                       p_policer_param->dir,
                                                       policer_id,
                                                       &p_sys_policer));
    SYS_QOS_POLICER_UNLOCK(lchip);
    if (NULL == p_sys_policer)
    {
        return CTC_E_QOS_POLICER_NOT_EXIST;
    }
    p_policer_param->enable = 1;
    p_policer_param->hbwp_en = (p_sys_policer->hbwp_en != 0);
    sal_memcpy(&p_policer_param->policer, &p_sys_policer->policer, sizeof(ctc_qos_policer_param_t));

    if (p_policer_param->hbwp_en)
    {
        sal_memcpy(&p_policer_param->hbwp,
                   &p_sys_policer->hbwp[p_policer_param->hbwp.cos_index],
                   sizeof(ctc_qos_policer_hbwp_t));
    }


    return CTC_E_NONE;
}

int32
sys_greatbelt_qos_policer_index_get(uint8 lchip, ctc_qos_policer_type_t type,
                                    uint16 plc_id,
                                    uint16* p_index)
{
    sys_qos_policer_t* p_sys_policer = NULL;

    SYS_QOS_POLICER_DBG_FUNC();

    SYS_QOS_POLICER_DBG_PARAM("Get: type           = %d\n", type);
    SYS_QOS_POLICER_DBG_PARAM("Get: dir            = %d\n", plc_id);

    SYS_QOS_POLICER_LOCK(lchip);
    CTC_ERROR_RETURN_QOS_POLICER_UNLOCK(_sys_greatbelt_qos_policer_lookup(lchip, type,
                                                       CTC_INGRESS,
                                                       plc_id,
                                                       &p_sys_policer));
    SYS_QOS_POLICER_UNLOCK(lchip);
    if (NULL == p_sys_policer)
    {
        return CTC_E_QOS_POLICER_NOT_EXIST;
    }

    *p_index = p_sys_policer->policer_ptr;

    SYS_QOS_POLICER_DBG_INFO("Get: index           = %d\n", p_sys_policer->policer_ptr);

    return CTC_E_NONE;
}

extern int32
sys_greatbelt_qos_set_policer_update_enable(uint8 lchip, uint8 enable)
{
    uint32 cmd = 0;
    uint32 field_val;

    SYS_QOS_POLICER_DBG_FUNC();

    field_val = enable;

    cmd = DRV_IOW(IpePolicingCtl_t, IpePolicingCtl_TsTickGenEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    cmd = DRV_IOW(IpePolicingCtl_t, IpePolicingCtl_UpdateEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    return CTC_E_NONE;
}


extern int32
sys_greatbelt_qos_set_phb(uint8 lchip, ctc_qos_phb_map_t *p_phb_map)
{
    uint32 cmd = 0;
    uint32 field_val;
    uint32 index = 0;
    uint8 field_id = 0;
    uint8 cos_index_label = 0;

    SYS_QOS_POLICER_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_phb_map);


    SYS_QOS_POLICER_DBG_PARAM("p_phb_map->map_type = %d\n", p_phb_map->map_type);
    SYS_QOS_POLICER_DBG_PARAM("p_phb_map->priority = %d\n", p_phb_map->priority);
    SYS_QOS_POLICER_DBG_PARAM("p_phb_map->cos_index      = %d\n", p_phb_map->cos_index);
    if (p_phb_map->map_type == CTC_QOS_PHB_MAP_PRI)
    {
        CTC_MAX_VALUE_CHECK(p_phb_map->priority, SYS_QOS_CLASS_PRIORITY_MAX);
        CTC_MAX_VALUE_CHECK(p_phb_map->cos_index, 3);
        cos_index_label = p_phb_map->priority;
    }
    else
    {
        return CTC_E_NONE;
    }

    index = cos_index_label / 16;
    field_val = p_phb_map->cos_index;
    field_id = IpeClassificationPhbOffset_PhbOffset0_f + cos_index_label % 16;
    cmd = DRV_IOW(IpeClassificationPhbOffset_t, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &field_val));

    index = 0;
    field_val = p_phb_map->cos_index;
    field_id = EpeClassificationPhbOffset_PhbOffset0_f + cos_index_label;
    cmd = DRV_IOW(EpeClassificationPhbOffset_t, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &field_val));

    return CTC_E_NONE;
}


extern int32
sys_greatbelt_qos_get_phb(uint8 lchip, ctc_qos_phb_map_t *p_phb_map)
{
    uint32 cmd = 0;
    uint32 field_val;
    uint32 index = 0;
    uint8 field_id = 0;
    uint8 cos_index_label = 0;

    SYS_QOS_POLICER_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_phb_map);

    if (p_phb_map->map_type == CTC_QOS_PHB_MAP_PRI)
    {
        CTC_MAX_VALUE_CHECK(p_phb_map->priority, SYS_QOS_CLASS_PRIORITY_MAX);

        cos_index_label = p_phb_map->priority;

        index = cos_index_label / 16;
        field_id = IpeClassificationPhbOffset_PhbOffset0_f + cos_index_label % 16;
        cmd = DRV_IOR(IpeClassificationPhbOffset_t, field_id);
    }
    else
    {
        return CTC_E_NONE;
    }

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &field_val));


    if (p_phb_map->map_type == CTC_QOS_PHB_MAP_PRI)
    {
        p_phb_map->cos_index =  field_val;
    }

    return CTC_E_NONE;
}

extern int32
sys_greatbelt_qos_set_policer_flow_first(uint8 lchip, uint8 dir, uint8 enable)
{

    uint32 cmd = 0;
    uint32 field_val;

    SYS_QOS_POLICER_DBG_FUNC();

    field_val = enable;

    if ((CTC_INGRESS == dir) || (CTC_BOTH_DIRECTION == dir))
    {
        cmd = DRV_IOW(IpeClassificationCtl_t, IpeClassificationCtl_FlowPolicerFirst_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    }

    if ((CTC_EGRESS == dir) || (CTC_BOTH_DIRECTION == dir))
    {
        cmd = DRV_IOW(EpeClassificationCtl_t, EpeClassificationCtl_FlowPolicerFirst_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    }


    return CTC_E_NONE;
}

int32
sys_greatbelt_qos_set_policer_sequential_enable(uint8 lchip, uint8 enable)
{
    uint32 cmd = 0;
    uint32 field_val;

    SYS_QOS_POLICER_DBG_FUNC();

    field_val = enable;

    cmd = DRV_IOW(IpePolicingCtl_t, IpePolicingCtl_SequentialPolicing_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    return CTC_E_NONE;
}

int32
sys_greatbelt_qos_set_policer_ipg_enable(uint8 lchip, uint8 enable)
{
    uint32 cmd = 0;
    uint32 field_val;

    SYS_QOS_POLICER_DBG_FUNC();

    field_val = enable;

    cmd = DRV_IOW(IpePolicingCtl_t, IpePolicingCtl_IpgEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    return CTC_E_NONE;
}

int32
sys_greatbelt_qos_set_policer_hbwp_share_enable(uint8 lchip, uint8 enable)
{
    uint32 cmd = 0;
    uint32 field_val;

    SYS_QOS_POLICER_DBG_FUNC();

    field_val = enable;

    cmd = DRV_IOW(IpePolicingCtl_t, IpePolicingCtl_HbwpShareMode_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    return CTC_E_NONE;
}


int32
sys_greatbelt_qos_set_policer_stats_enable(uint8 lchip, uint8 enable)
{

    SYS_QOS_POLICER_DBG_FUNC();

    CTC_ERROR_RETURN(sys_greatbelt_stats_set_policing_en(lchip, enable));


    return CTC_E_NONE;
}

int32
sys_greatbelt_qos_policer_stats_query(uint8 lchip, ctc_qos_policer_stats_t* p_stats_param)
{
    sys_stats_policing_t stats_result;
    sys_qos_policer_t* p_sys_policer = NULL;
    ctc_qos_policer_stats_info_t* p_stats = NULL;
    uint16 policer_ptr = 0;
    uint16 policer_id = 0;

    SYS_QOS_POLICER_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_stats_param);

    SYS_QOS_POLICER_LOCK(lchip);
    policer_id = _sys_greatbelt_qos_get_policer_id(lchip, p_stats_param->type,
                                                   p_stats_param->id.policer_id, p_stats_param->id.gport);

    CTC_ERROR_RETURN_QOS_POLICER_UNLOCK(_sys_greatbelt_qos_policer_policer_id_check(lchip, p_stats_param->type,
                                                                 p_stats_param->dir,
                                                                 policer_id));

    CTC_ERROR_RETURN_QOS_POLICER_UNLOCK(_sys_greatbelt_qos_policer_lookup(lchip, p_stats_param->type,
                                                       p_stats_param->dir,
                                                       policer_id,
                                                       &p_sys_policer));

    SYS_QOS_POLICER_UNLOCK(lchip);

    if (!p_sys_policer)
    {
        return CTC_E_QOS_POLICER_NOT_EXIST;
    }

    if (!p_sys_policer->policer.stats_en)
    {
        return CTC_E_QOS_POLICER_STATS_NOT_ENABLE;
    }

    if (p_stats_param->hbwp_en)
    {
        CTC_MAX_VALUE_CHECK(p_stats_param->cos_index, 3);
        policer_ptr = p_sys_policer->policer_ptr + p_stats_param->cos_index;
    }
    else
    {
        policer_ptr = p_sys_policer->policer_ptr;
    }


    p_stats = &p_stats_param->stats;


    SYS_QOS_POLICER_DBG_INFO("policer_ptr:%d\n", policer_ptr);

    CTC_ERROR_RETURN(sys_greatbelt_stats_get_policing_stats(lchip, policer_ptr, &stats_result));
    p_stats->confirm_pkts  = stats_result.policing_confirm_pkts;
    p_stats->confirm_bytes = stats_result.policing_confirm_bytes;
    p_stats->exceed_pkts   = stats_result.policing_exceed_pkts;
    p_stats->exceed_bytes  = stats_result.policing_exceed_bytes;
    p_stats->violate_pkts  = stats_result.policing_violate_pkts;
    p_stats->violate_bytes = stats_result.policing_violate_bytes;

    SYS_QOS_POLICER_DBG_INFO("get policer stats\n");
    SYS_QOS_POLICER_DBG_INFO("============================================\n");
    SYS_QOS_POLICER_DBG_INFO("confirm_packet = %"PRIu64", confirm_bytes = %"PRIu64"\n", p_stats->confirm_pkts, p_stats->confirm_bytes);
    SYS_QOS_POLICER_DBG_INFO("exceed_packet = %"PRIu64", exceed_bytes = %"PRIu64"\n", p_stats->exceed_pkts, p_stats->exceed_bytes);
    SYS_QOS_POLICER_DBG_INFO("violate_packet = %"PRIu64", violate_bytes = %"PRIu64"\n", p_stats->violate_pkts, p_stats->violate_bytes);

    return CTC_E_NONE;
}

int32
sys_greatbelt_qos_policer_stats_clear(uint8 lchip, ctc_qos_policer_stats_t* p_stats_param)
{
    sys_qos_policer_t* p_sys_policer = NULL;
    uint16 policer_ptr = 0;
    uint16 policer_id = 0;

    SYS_QOS_POLICER_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_stats_param);

    SYS_QOS_POLICER_LOCK(lchip);
    policer_id = _sys_greatbelt_qos_get_policer_id(lchip, p_stats_param->type,
                                                   p_stats_param->id.policer_id, p_stats_param->id.gport);

    CTC_ERROR_RETURN_QOS_POLICER_UNLOCK(_sys_greatbelt_qos_policer_policer_id_check(lchip, p_stats_param->type,
                                                                 p_stats_param->dir,
                                                                 policer_id));

    CTC_ERROR_RETURN_QOS_POLICER_UNLOCK(_sys_greatbelt_qos_policer_lookup(lchip, p_stats_param->type,
                                                       p_stats_param->dir,
                                                       policer_id,
                                                       &p_sys_policer));
    SYS_QOS_POLICER_UNLOCK(lchip);

    if (!p_sys_policer)
    {
        return CTC_E_QOS_POLICER_NOT_EXIST;
    }

    if (!p_sys_policer->stats_en)
    {
        return CTC_E_QOS_POLICER_STATS_NOT_ENABLE;
    }


    if (p_stats_param->hbwp_en)
    {
        CTC_MAX_VALUE_CHECK(p_stats_param->cos_index, 3);
        policer_ptr = p_sys_policer->policer_ptr + p_stats_param->cos_index;
    }
    else
    {
        policer_ptr = p_sys_policer->policer_ptr;
    }

    SYS_QOS_POLICER_DBG_INFO("policer_ptr:%d\n", policer_ptr);

    CTC_ERROR_RETURN(sys_greatbelt_stats_clear_policing_stats(lchip, policer_ptr));

    return CTC_E_NONE;

}

int32
sys_greatbelt_qos_policer_dump(uint8 type,
                               uint8 dir,
                               uint8 lchip,
                               uint16 start,
                               uint16 end,
                               uint8 detail)
{
    uint16 index = 0;
    uint8 gchip = 0;
    uint16 logic_port = 0;
    uint8 mem_index    = 0;
    uint32 cir = 0;
    uint32 cbs = 0;
    uint32 pir = 0;
    uint32 pbs = 0;
    uint8 first_mem_index = 1;
    sys_qos_policer_t* p_sys_policer = NULL;
    sys_qos_policer_profile_t* p_sys_profile = NULL;

    SYS_QOS_POLICER_INIT_CHECK(lchip);
    SYS_QOS_POLICER_DBG_DUMP("show policer detail:\n");
    SYS_QOS_POLICER_DBG_DUMP("============================================\n");
    LCHIP_CHECK(lchip);

    if (type == CTC_QOS_POLICER_TYPE_PORT)
    {
        SYS_QOS_POLICER_DBG_DUMP("%-6s %-10s ", "gport", "policer-en");
    }
    else if (type == CTC_QOS_POLICER_TYPE_FLOW)
    {
        SYS_QOS_POLICER_DBG_DUMP("%-6s %-10s ", "plc-id", "policer-en");
    }

    SYS_QOS_POLICER_DBG_DUMP("%-10s %-10s %-10s %-10s\n", "cir", "pir", "cbs", "pbs");


    for (index = start; index <= end; index++)
    {
        if (type == CTC_QOS_POLICER_TYPE_PORT)
        {
            sys_greatbelt_get_gchip_id(lchip, &gchip);
            logic_port = CTC_MAP_LPORT_TO_GPORT(gchip, index);
        }
        else if (type == CTC_QOS_POLICER_TYPE_FLOW)
        {
            logic_port = index;
        }
        SYS_QOS_POLICER_LOCK(lchip);
        CTC_ERROR_RETURN_QOS_POLICER_UNLOCK(_sys_greatbelt_qos_policer_lookup(lchip, type,
                                                           dir,
                                                           logic_port,
                                                           &p_sys_policer));
        SYS_QOS_POLICER_UNLOCK(lchip);

        if (NULL == p_sys_policer)
        {
            SYS_QOS_POLICER_DBG_DUMP("0x%04x %-10d ", logic_port, 0);
            SYS_QOS_POLICER_DBG_DUMP("%-10d %-10d %-10d %-10d\n",
                                     0,
                                     0,
                                     0,
                                     0);

            if (detail && (start == end))
            {
                SYS_QOS_POLICER_DBG_DUMP("============================================\n");
                SYS_QOS_POLICER_DBG_DUMP("%-10s %-10s %-10s %-10s\n", "stats-en", "policer-ptr", "profile-id", "prof_memid");
                SYS_QOS_POLICER_DBG_DUMP("%-10d %-10d %-10d %-10d\n",
                                         0,
                                         0,
                                         0,
                                         0);
            }

            continue;

        }
        for (mem_index = 0;  mem_index < 4; mem_index++)
        {
            p_sys_profile = p_sys_policer->p_sys_profile[mem_index];

            if (NULL ==  p_sys_profile)
            {
                continue;
            }

            if (first_mem_index)
            {
                SYS_QOS_POLICER_DBG_DUMP("0x%04x %-10d ", logic_port, 1);
                first_mem_index = 0;
            }
            else
            {
                SYS_QOS_POLICER_DBG_DUMP("       %-10d ", 1);
            }

            _sys_greatbelt_qos_policer_map_token_rate_hw_to_user(lchip, p_sys_profile->profile.commit_rate, p_sys_profile->profile.commit_count_shift, &cir);
            cbs = (p_sys_profile->profile.commit_threshold << p_sys_profile->profile.commit_threshold_shift) / 125;/*125 = 8/1000* -->kbps*/;
            cbs = CTC_IS_BIT_SET(p_sys_profile->profile.commit_count_shift, 3)?(cbs >> (p_sys_profile->profile.commit_count_shift&0x7)):
            (cbs << (p_sys_profile->profile.commit_count_shift&0x7));
            _sys_greatbelt_qos_policer_map_token_rate_hw_to_user(lchip, p_sys_profile->profile.peak_rate, p_sys_profile->profile.peak_count_shift, &pir);
            pbs = (p_sys_profile->profile.peak_threshold << p_sys_profile->profile.peak_threshold_shift) / 125;     /*125 = 8/1000*/;
            pbs = CTC_IS_BIT_SET(p_sys_profile->profile.peak_count_shift, 3)?(pbs >> (p_sys_profile->profile.peak_count_shift&0x7)):
            (pbs << (p_sys_profile->profile.peak_count_shift&0x7));

            SYS_QOS_POLICER_DBG_DUMP("%-10d %-10d %-10u %-10u\n",cir,pir,cbs,pbs);
        }
        first_mem_index = 1;
        if (detail && (start == end))
        {
            SYS_QOS_POLICER_DBG_DUMP("============================================\n");
            SYS_QOS_POLICER_DBG_DUMP("%-10s %-10s %-10s %-10s\n", "stats-en", "policer-ptr", "profile-id", "prof_memid");

            for (mem_index = 0;  mem_index < 4; mem_index++)
            {

                p_sys_profile = p_sys_policer->p_sys_profile[mem_index];

                if (NULL ==  p_sys_profile)
                {
                    continue;
                }
                SYS_QOS_POLICER_DBG_DUMP("%-10d %-10d %-10d %-10d\n",
                                         p_sys_policer->stats_en,
                                         p_sys_policer->policer_ptr,
                                         p_sys_profile->profile_id,
                                         p_sys_profile->profile_mem_id);
            }
        }

    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_qos_policer_init(uint8 lchip)
{
    uint32 policer_num         = 0;
    uint32 policer_profile_num = 0;
    uint8 idx                  = 0;
    uint32 flow_policer_base   = 0;
    uint32 flow_policer_num    = 0;
    sys_greatbelt_opf_t opf    = {0};
    ctc_spool_t spool = {0};

    p_gb_qos_policer_master[lchip] = mem_malloc(MEM_QUEUE_MODULE, sizeof(sys_qos_policer_master_t));
    if (NULL == p_gb_qos_policer_master[lchip])
    {
        return CTC_E_NO_MEMORY;
    }
    sal_memset(p_gb_qos_policer_master[lchip], 0, sizeof(sys_qos_policer_master_t));
    SYS_QOS_POLICER_CREATE_LOCK(lchip);

    p_gb_qos_policer_master[lchip]->p_policer_hash  = ctc_hash_create(
            SYS_QOS_POLICER_HASH_BLOCK_SIZE,
            SYS_QOS_POLICER_HASH_TBL_SIZE,
            (hash_key_fn)_sys_greatbelt_policer_hash_make,
            (hash_cmp_fn)_sys_greatbelt_policer_hash_cmp);


    sys_greatbelt_ftm_get_table_entry_num(lchip, DsPolicerControl_t, &policer_num);
    sys_greatbelt_ftm_get_table_entry_num(lchip, DsPolicerProfile_t, &policer_profile_num);

    for (idx = 0; idx < 4; idx++)
    {
        sal_memset(&spool, 0, sizeof(ctc_spool_t));
        spool.lchip = lchip;
        spool.block_num = 1;
        spool.block_size = SYS_QOS_POLICER_PROF_HASH_BLOCK_SIZE;
        spool.max_count = policer_profile_num;
        spool.user_data_size = sizeof(sys_qos_policer_profile_t);
        spool.spool_key = (hash_key_fn)_sys_greatbelt_policer_profile_hash_make;
        spool.spool_cmp = (hash_cmp_fn)_sys_greatbelt_policer_profile_hash_cmp;
        p_gb_qos_policer_master[lchip]->p_profile_pool[idx] = ctc_spool_create(&spool);
    }

    p_gb_qos_policer_master[lchip]->port_policer_base[CTC_INGRESS] = 0;
    p_gb_qos_policer_master[lchip]->port_policer_base[CTC_EGRESS]  = MAX_PORT_POLICER_NUM / 2;

    CTC_ERROR_RETURN(_sys_greatbelt_qos_policer_init_reg(lchip, policer_num));

    policer_num = policer_num * 4;

    flow_policer_base = MAX_PORT_POLICER_NUM;
    flow_policer_num = policer_num - MAX_PORT_POLICER_NUM;


    CTC_ERROR_RETURN(sys_greatbelt_opf_init(lchip, OPF_QOS_FLOW_POLICER, 1));
    CTC_ERROR_RETURN(sys_greatbelt_opf_init(lchip, OPF_QOS_POLICER_PROFILE0, 1));
    CTC_ERROR_RETURN(sys_greatbelt_opf_init(lchip, OPF_QOS_POLICER_PROFILE1, 1));
    CTC_ERROR_RETURN(sys_greatbelt_opf_init(lchip, OPF_QOS_POLICER_PROFILE2, 1));
    CTC_ERROR_RETURN(sys_greatbelt_opf_init(lchip, OPF_QOS_POLICER_PROFILE3, 1));

    /* init flow policer free index pool */
    opf.pool_index = 0;
    opf.pool_type = OPF_QOS_FLOW_POLICER;
    CTC_ERROR_RETURN(sys_greatbelt_opf_init_offset(lchip, &opf, flow_policer_base, flow_policer_num));

    opf.pool_index = 0;

    opf.pool_type = OPF_QOS_POLICER_PROFILE0;
    CTC_ERROR_RETURN(sys_greatbelt_opf_init_offset(lchip, &opf, 0, policer_profile_num));

    opf.pool_type = OPF_QOS_POLICER_PROFILE1;
    CTC_ERROR_RETURN(sys_greatbelt_opf_init_offset(lchip, &opf, 0, policer_profile_num));

    opf.pool_type = OPF_QOS_POLICER_PROFILE2;
    CTC_ERROR_RETURN(sys_greatbelt_opf_init_offset(lchip, &opf, 0, policer_profile_num));

    opf.pool_type = OPF_QOS_POLICER_PROFILE3;
    CTC_ERROR_RETURN(sys_greatbelt_opf_init_offset(lchip, &opf, 0, policer_profile_num));


    /* set chip_capability */
    CTC_ERROR_RETURN(sys_greatbelt_global_set_chip_capability(lchip, CTC_GLOBAL_CAPABILITY_TOTAL_POLICER_NUM, policer_num));
    CTC_ERROR_RETURN(sys_greatbelt_global_set_chip_capability(lchip, CTC_GLOBAL_CAPABILITY_POLICER_NUM, flow_policer_num));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_qos_policer_node_data(void* node_data, void* user_data)
{
    mem_free(node_data);

    return CTC_E_NONE;
}

int32
sys_greatbelt_qos_policer_deinit(uint8 lchip)
{
    uint8 idx                  = 0;

    if (NULL == p_gb_qos_policer_master[lchip])
    {
        return CTC_E_NONE;
    }

    sys_greatbelt_opf_deinit(lchip, OPF_QOS_FLOW_POLICER);
    sys_greatbelt_opf_deinit(lchip, OPF_QOS_POLICER_PROFILE0);
    sys_greatbelt_opf_deinit(lchip, OPF_QOS_POLICER_PROFILE1);
    sys_greatbelt_opf_deinit(lchip, OPF_QOS_POLICER_PROFILE2);
    sys_greatbelt_opf_deinit(lchip, OPF_QOS_POLICER_PROFILE3);

    /*free profile*/
    for (idx = 0; idx < 4; idx++)
    {
        ctc_spool_free(p_gb_qos_policer_master[lchip]->p_profile_pool[idx]);
    }

    /*free policer*/
    ctc_hash_traverse(p_gb_qos_policer_master[lchip]->p_policer_hash, (hash_traversal_fn)_sys_greatbelt_qos_policer_node_data, NULL);
    ctc_hash_free(p_gb_qos_policer_master[lchip]->p_policer_hash);

    sal_mutex_destroy(p_gb_qos_policer_master[lchip]->mutex);

    mem_free(p_gb_qos_policer_master[lchip]);

    return CTC_E_NONE;
}

int32
sys_greatbelt_qos_policer_reserv(uint8 lchip)
{
    ctc_qos_policer_t policer_param;

    sal_memset(&policer_param, 0, sizeof(policer_param));
    /*1. drop flow policer*/
    policer_param.type = CTC_QOS_POLICER_TYPE_FLOW;
    policer_param.enable = 1;
    policer_param.id.policer_id = SYS_QOS_POLICER_FLOW_ID_DROP;
    policer_param.policer.cir = 0;
    policer_param.policer.pir = 0;
    policer_param.policer.policer_mode = CTC_QOS_POLICER_MODE_RFC4115;
    policer_param.policer.drop_color   = CTC_QOS_COLOR_YELLOW;
    _sys_greatbelt_qos_policer_add(lchip, &policer_param, NULL);

    /*2. max flow policer*/
    sal_memset(&policer_param, 0, sizeof(policer_param));
    policer_param.type = CTC_QOS_POLICER_TYPE_FLOW;
    policer_param.enable = 1;
    policer_param.id.policer_id = SYS_QOS_POLICER_FLOW_ID_RATE_MAX;
    policer_param.policer.cir = 0xFFFFFFFF;
    policer_param.policer.pir = 0xFFFFFFFF;
    policer_param.policer.policer_mode = CTC_QOS_POLICER_MODE_RFC4115;
    _sys_greatbelt_qos_policer_add(lchip, &policer_param, NULL);

    return CTC_E_NONE;
}


int32
sys_greatbelt_qos_policer_reserv_service_hbwp(uint8 lchip)
{
    ctc_qos_policer_t policer_param;
    sys_qos_policer_t* p_sys_policer = NULL;

    sal_memset(&policer_param, 0, sizeof(policer_param));

    /*2. max flow policer*/
    sal_memset(&policer_param, 0, sizeof(policer_param));
    policer_param.type = CTC_QOS_POLICER_TYPE_FLOW;
    policer_param.enable = 1;
    policer_param.id.policer_id = SYS_QOS_POLICER_FLOW_ID_RATE_MAX;
    policer_param.policer.cir = 0xFFFFFFFF;
    policer_param.policer.pir = 0xFFFFFFFF;
    policer_param.policer.policer_mode = CTC_QOS_POLICER_MODE_RFC4115;
    policer_param.hbwp_en = 1;
    policer_param.hbwp.cos_index = 0;
    policer_param.hbwp.sf_en = 1;
    policer_param.hbwp.sp_en = 1;
    policer_param.hbwp.cir_max = 0xFFFFFFFF;
    policer_param.hbwp.pir_max = 0xFFFFFFFF;
    CTC_ERROR_RETURN(_sys_greatbelt_qos_policer_add(lchip, &policer_param, NULL));


    CTC_ERROR_RETURN(_sys_greatbelt_qos_policer_lookup(lchip, policer_param.type,
                                                       policer_param.dir,
                                                       policer_param.id.policer_id,
                                                       &p_sys_policer));

    policer_param.hbwp.cos_index = 1;
    CTC_ERROR_RETURN(_sys_greatbelt_qos_policer_add(lchip, &policer_param, p_sys_policer));

    policer_param.hbwp.cos_index = 2;
    CTC_ERROR_RETURN(_sys_greatbelt_qos_policer_add(lchip, &policer_param, p_sys_policer));

    policer_param.hbwp.cos_index = 3;
    CTC_ERROR_RETURN(_sys_greatbelt_qos_policer_add(lchip, &policer_param, p_sys_policer));

    return CTC_E_NONE;
}



#endif


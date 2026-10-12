/**
 @file sys_goldengate_qos_policer.c

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
#include "ctc_warmboot.h"

#include "sys_goldengate_common.h"
#include "sys_goldengate_chip.h"
#include "sys_goldengate_opf.h"
#include "sys_goldengate_port.h"
#include "sys_goldengate_stats.h"
#include "sys_goldengate_ftm.h"
#include "sys_goldengate_queue_enq.h"
#include "sys_goldengate_qos_policer.h"
#include "sys_goldengate_register.h"
#include "sys_goldengate_wb_common.h"

#include "goldengate/include/drv_enum.h"
#include "goldengate/include/drv_io.h"

/****************************************************************************
  *
  * Defines and Macros
  *
  ****************************************************************************/

#define SYS_QOS_POLICER_BENCH_GRAN p_gg_qos_policer_master[lchip]->update_gran_base /*1024K*/

#define MAX_PHB_OFFSET_NUM             4

#define SYS_QOS_POLICER_MAX_POLICER_NUM        4096
#define SYS_QOS_POLICER_MAX_PROFILE_NUM        256

#define SYS_QOS_POLICER_HASH_TBL_SIZE         512
#define SYS_QOS_POLICER_HASH_BLOCK_SIZE       (SYS_QOS_POLICER_MAX_POLICER_NUM / SYS_QOS_POLICER_HASH_TBL_SIZE)

#define SYS_QOS_POLICER_PROF_HASH_TBL_SIZE    64
#define SYS_QOS_POLICER_PROF_HASH_BLOCK_SIZE  (SYS_QOS_POLICER_MAX_PROFILE_NUM / SYS_QOS_POLICER_PROF_HASH_TBL_SIZE)
#define SYS_QOS_POLICER_PROF_TBL_NUM          4

#define SYS_QOS_POLICER_MAX_CBS         5120        /* 0xFFFF/125 */
#define SYS_MAX_POLICER_GRANULARITY_RANGE_NUM       8
#define SYS_MAX_POLICER_SUPPORTED_FREQ_NUM          20
#define SYS_POLICER_GRAN_RANAGE_NUM   8

/* max port policer num in both direction */
#define MAX_PORT_POLICER_NUM   128

/* max port policer num for 8 queue in both direction */
#define MAX_PORT_POLICER_NUM_8Q   240

/* max port policer num for 4 queue in both direction */
#define MAX_PORT_POLICER_NUM_4Q   480

enum sys_policer_cnt_s
{
    SYS_POLICER_CNT_PORT_POLICER,
    SYS_POLICER_CNT_FLOW_POLICER,
    SYS_POLICER_CNT_MAX
}sys_policer_cnt_t;


/* get port policer offset */
#define SYS_QOS_PORT_POLICER_INDEX(lport, dir, phb_offset) \
    ((lport & 0xff) + (phb_offset) \
    + (SYS_MAP_DRV_LPORT_TO_SLICE(lport) ? p_gg_qos_policer_master[lchip]->slice_port_policer_base : 0) \
    + p_gg_qos_policer_master[lchip]->port_policer_base[dir])

#define MAX(a, b) (((a)>(b))?(a):(b))

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
 @brief  qos prof entry data structure
*/
struct sys_qos_policer_profile_s
{
    uint32  profile_id         :8;
    uint32  rsv0               :24;

	uint32  peak_count_shift     :16;
    uint32  commit_count_shift   :16;

    uint32  peak_threshold        :12;
    uint32  peak_threshold_shift   :4;
    uint32  peak_rate             :16;

    uint32  commit_threshold      :12;
    uint32  commit_threshold_shift :4;
    uint32  commit_rate           :16;

    uint32  peak_rate_max          :16;
    uint32  commit_rate_max        :16;

};
typedef struct sys_qos_policer_profile_s sys_qos_policer_profile_t;

struct sys_qos_policer_s
{
    uint8  type; /*ctc_qos_policer_type_t*/
    uint8  dir;   /*ctc_direction_t */
    uint16 id;

    uint8  cos_bitmap; /*bitmap of hbwp enable*/
	uint8  triple_play; /*bitmap of hbwp enable*/
	uint8  entry_size;
    uint16 policer_ptr;

	uint16 stats_ptr[4];
    sys_qos_policer_profile_t* p_sys_profile[4];
};
typedef struct sys_qos_policer_s sys_qos_policer_t;

struct sys_qos_policer_granularity_s
{
    uint32 max_rate;        /* unit is Mbps */
    uint32 granularity;     /* unit is Kbps */
};
typedef struct sys_qos_policer_granularity_s sys_qos_policer_granularity_t;

struct sys_qos_policer_master_s
{
    ctc_hash_t* p_policer_hash;
    ctc_spool_t* p_profile_pool;

    uint16  port_policer_base[CTC_BOTH_DIRECTION];
    uint16  slice_port_policer_base;
	uint8   max_cos_level;
	uint8   flow_plus_service_policer_en;
	uint16  policer_count[SYS_POLICER_CNT_MAX];
    uint16  update_gran_base;
    uint8   policer_gran_mode;
    uint8   rsv;

    sal_mutex_t* mutex;
};
typedef struct sys_qos_policer_master_s sys_qos_policer_master_t;

sys_qos_policer_master_t* p_gg_qos_policer_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};

#define SYS_QOS_POLICER_INIT_CHECK(lchip)       \
do{                                             \
    SYS_LCHIP_CHECK_ACTIVE(lchip);              \
    if (p_gg_qos_policer_master[lchip] == NULL) \
    {return CTC_E_NOT_INIT;}                    \
}while (0)

#define SYS_QOS_POLICER_CREATE_LOCK(lchip)                         \
    do                                                              \
    {                                                               \
        sal_mutex_create(&p_gg_qos_policer_master[lchip]->mutex);  \
        if (NULL == p_gg_qos_policer_master[lchip]->mutex)         \
        {                                                           \
            CTC_ERROR_RETURN(CTC_E_FAIL_CREATE_MUTEX);              \
        }                                                           \
    } while (0)

#define SYS_QOS_POLICER_LOCK(lchip) \
    sal_mutex_lock(p_gg_qos_policer_master[lchip]->mutex)

#define SYS_QOS_POLICER_UNLOCK(lchip) \
    sal_mutex_unlock(p_gg_qos_policer_master[lchip]->mutex)

#define CTC_ERROR_RETURN_QOS_POLICER_UNLOCK(op) \
    do \
    { \
        int32 rv = (op); \
        if (rv < 0) \
        { \
            sal_mutex_unlock(p_gg_qos_policer_master[lchip]->mutex); \
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
_sys_goldengate_policer_hash_make(sys_qos_policer_t* backet)
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
_sys_goldengate_policer_hash_cmp(sys_qos_policer_t* p_policer1,
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
_sys_goldengate_policer_profile_hash_make(sys_qos_policer_profile_t* p_prof)
{

    uint8* data = (uint8*)(p_prof)+ 4;
    uint8   length = sizeof(sys_qos_policer_profile_t) - 4;

    return ctc_hash_caculate(length, data);
}

STATIC bool
_sys_goldengate_policer_profile_hash_cmp(sys_qos_policer_profile_t* p_prof1,
                                        sys_qos_policer_profile_t* p_prof2)
{
    SYS_QOS_POLICER_DBG_FUNC();
    if (!p_prof1 || !p_prof2)
    {
        return FALSE;
    }

    if (!sal_memcmp((uint8*)(p_prof1) + 4, (uint8*)(p_prof2) + 4, sizeof(sys_qos_policer_profile_t)-4))
    {
        return TRUE;
    }
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_qos_policer_init_reg(uint8 lchip, uint16 policer_num, uint32 divide_factor)
{
    uint32 cmd             = 0;
    uint32 field_val       = 0;

    PolicingCtl_m policing_ctl;
    IpeClassificationCtl_m  cls_ctl;
	EpeClassificationCtl_m   epe_cls_ctl;
	IpeClassificationPhbOffset_m ipe_phb;
	EpeClassificationPhbOffset_m  epe_phb;

    sal_memset(&policing_ctl, 0, sizeof(policing_ctl));
    sal_memset(&cls_ctl, 0, sizeof(cls_ctl));
    sal_memset(&epe_cls_ctl, 0, sizeof(epe_cls_ctl));
    sal_memset(&ipe_phb, 0, sizeof(IpeClassificationPhbOffset_m));
    sal_memset(&epe_phb, 0, sizeof(EpeClassificationPhbOffset_m));

	/*PolicingCtl_t*/
    SetPolicingCtl(V,sdcStatsBase_f,&policing_ctl, 0x3FFF); /*policer stats  which be sent to cpu by sdc*/
    SetPolicingCtl(V,ipgEn_f,&policing_ctl,0);
    SetPolicingCtl(V,tsTickGenEn_f,&policing_ctl,1);
    SetPolicingCtl(V,minLengthCheckEn_f,&policing_ctl,0);
    SetPolicingCtl(V,sequentialPolicing_f,&policing_ctl,1);
    SetPolicingCtl(V,statsEnViolate_f,&policing_ctl,1);
    SetPolicingCtl(V,statsEnConfirm_f,&policing_ctl,1);
	SetPolicingCtl(V,statsEnNotConfirm_f,&policing_ctl,1);
    SetPolicingCtl(V,updateEn_f,&policing_ctl,1);
    SetPolicingCtl(V, maxPtr_f, &policing_ctl, policer_num - 1);
    SetPolicingCtl(V, minPtr_f, &policing_ctl, 0);
    SetPolicingCtl(V, tsTickGenInterval_f, &policing_ctl, 1);
    SetPolicingCtl(V, updateInterval_f, &policing_ctl, 1);
    cmd = DRV_IOW(PolicingCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &policing_ctl));

    /*IpeClassificationCtl_t*/
    SetIpeClassificationCtl(V,flowPolicerFirst_f,&cls_ctl,1);
    SetIpeClassificationCtl(V,portPolicerPhbEn_f,&cls_ctl,0);
    SetIpeClassificationCtl(V,portPolicerShift_f,&cls_ctl,0);
    SetIpeClassificationCtl(V,portPolicerBase_f,&cls_ctl,p_gg_qos_policer_master[lchip]->port_policer_base[CTC_INGRESS]);
    cmd = DRV_IOW(IpeClassificationCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &cls_ctl));

    /*EpeClassificationCtl_t*/
    SetEpeClassificationCtl(V,oamBypassPolicingDiscard_f,&epe_cls_ctl,1);
    SetEpeClassificationCtl(V,servicePolicerMode_f,&epe_cls_ctl,0);
    SetEpeClassificationCtl(V,portPolicerPhbEn_f,&epe_cls_ctl,0);
    SetEpeClassificationCtl(V,portPolicerBase_f,&epe_cls_ctl,p_gg_qos_policer_master[lchip]->port_policer_base[CTC_EGRESS]);
    SetEpeClassificationCtl(V,flowPolicerFirst_f,&epe_cls_ctl,0);
    SetEpeClassificationCtl(V,portPolicerShift_f ,&epe_cls_ctl,0);
    cmd = DRV_IOW(EpeClassificationCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_cls_ctl));


    field_val = p_gg_qos_policer_master[lchip]->slice_port_policer_base;
    cmd = DRV_IOW(EpeClassificationPortBaseCtl_t, EpeClassificationPortBaseCtl_claLocalPhyPortBase_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 1, cmd, &field_val));

    field_val = p_gg_qos_policer_master[lchip]->slice_port_policer_base;
    cmd = DRV_IOW(IpeClassificationPortBaseCtl_t, IpeClassificationPortBaseCtl_localPhyPortBase_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 1, cmd, &field_val));

    field_val = divide_factor;
    cmd = DRV_IOW(RefDivPolicingUpdatePulse_t, RefDivPolicingUpdatePulse_cfgRefDivPolicingUpdatePulse_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    field_val = 0;
    cmd = DRV_IOW(RefDivPolicingUpdatePulse_t, RefDivPolicingUpdatePulse_cfgResetDivPolicingUpdatePulse_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    if (8192 == policer_num)
    {
        field_val = 1;
        cmd = DRV_IOW(PolicingMiscCtl_t, PolicingMiscCtl_coupleModeEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    }

     /*reset 0*/
	cmd = DRV_IOW(IpeClassificationPhbOffset_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_phb));
	CTC_ERROR_RETURN(DRV_IOCTL(lchip, 1, cmd, &ipe_phb));
	CTC_ERROR_RETURN(DRV_IOCTL(lchip, 2, cmd, &ipe_phb));
	CTC_ERROR_RETURN(DRV_IOCTL(lchip, 3, cmd, &ipe_phb));

    cmd = DRV_IOW(EpeClassificationPhbOffset_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_phb));
    return CTC_E_NONE;
}

/**
 @brief get policer id based on type
*/
STATIC uint16
_sys_goldengate_qos_get_policer_id(uint8 type, uint16 plc_id, uint32 gport)
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
_sys_goldengate_qos_get_policer_gran_by_rate(uint8 lchip, uint32 rate, uint32* granularity)
{
    SYS_QOS_POLICER_DBG_FUNC();
    CTC_PTR_VALID_CHECK(granularity);

#define M 1000

    if (p_gg_qos_policer_master[lchip]->policer_gran_mode == 1)
    {
        if (rate <= 2*M)/*2M */
        {
            *granularity = 16;
        }
        else if(rate <= 100*M)/*100M */
        {
            *granularity = 512;
        }
        else if(rate <= 1000*M)  /*1G */
        {
            *granularity = 4096;
        }
        else if(rate <= 2000*M)/*2G */
        {
            *granularity = 8192;
        }
        else if(rate <= 4000*M)/*4G */
        {
            *granularity = 8192;
        }
        else if(rate <= 10000*M)/*10G */
        {
            *granularity = 16384;
        }
        else if(rate <= 40000*M)/*40G */
        {
            *granularity = 1024;
        }
        else if(rate <= 100000*M)/*100G */
        {
            *granularity = 2048;
        }
        else
        {
            return CTC_E_UNEXPECT;
        }
    }
    else if(p_gg_qos_policer_master[lchip]->policer_gran_mode == 2)
    {
        if (rate <= 2*M)/*2M */
        {
            *granularity = 10;
        }
        else if (rate <= 100*M)/*100M */
        {
            *granularity = 40;
        }
        else if (rate <= 1000*M)  /*1G */
        {
            *granularity = 80;
        }
        else if (rate <= 2000*M)/*2G */
        {
            *granularity = 120;
        }
        else if (rate <= 4000*M)/*4G */
        {
            *granularity = 250;
        }
        else if (rate <= 10000*M)/*10G */
        {
            *granularity = 500;
        }
        else if (rate <= 40000*M)/*40G */
        {
            *granularity = 1000;
        }
        else if (rate <= 100000*M)/*100G */
        {
            *granularity = 2000;
        }
        else
        {
            return CTC_E_UNEXPECT;
        }
    }
    else
    {
        if (rate <= 2*M)/*2M */
        {
            *granularity = 16;
        }
        else if(rate <= 100*M)/*100M */
        {
            *granularity = 32;
        }
        else if(rate <= 1000*M)  /*1G */
        {
            *granularity = 64;
        }
        else if(rate <= 2000*M)/*2G */
        {
            *granularity = 128;
        }
        else if(rate <= 4000*M)/*4G */
        {
            *granularity = 256;
        }
        else if(rate <= 10000*M)/*10G */
        {
            *granularity = 512;
        }
        else if(rate <= 40000*M)/*40G */
        {
            *granularity = 1024;
        }
        else if(rate <= 100000*M)/*100G */
        {
            *granularity = 2048;
        }
        else
        {
            return CTC_E_UNEXPECT;
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_qos_policer_get_count_shift(uint8 lchip, uint32 gran, uint8* p_count_shift)
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
_sys_goldengate_qos_policer_map_token_rate_hw_to_user(uint8 lchip, uint32 hw_rate, uint8 count_shift,uint32 *user_rate)
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
_sys_goldengate_qos_policer_profile_build_data(uint8 lchip, ctc_qos_policer_t* p_policer,
                                              sys_qos_policer_profile_t* p_ds_prof)
{
    uint32 value                 = 0;
    uint32 max_rate              = 0;
    uint32 gran_cir              = 0;
    uint32 gran_pir              = 0;
    uint8 count_shift            = 0;
	uint32 max_thrd = 0;
	uint32 token_thrd = 0;

    SYS_QOS_POLICER_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_policer);
    CTC_PTR_VALID_CHECK(p_ds_prof);

    /* compute commit rate(CIR) */
    max_rate =  p_policer->policer.cir;

    if (p_policer->hbwp_en== 1)
    {
        max_rate = MAX(p_policer->hbwp.cir_max, max_rate);
    }

    CTC_ERROR_RETURN(_sys_goldengate_qos_get_policer_gran_by_rate(lchip, max_rate, &gran_cir));
    CTC_ERROR_RETURN(_sys_goldengate_qos_policer_get_count_shift(lchip, gran_cir, &count_shift));
    SYS_QOS_POLICER_DBG_INFO("cir granularity : %d\n", gran_cir);

    p_ds_prof->commit_rate = p_policer->policer.cir / gran_cir;
    p_ds_prof->commit_count_shift = (count_shift & 0xF);
    SYS_QOS_POLICER_DBG_INFO(" p_ds_prof->commit_rate : %d\n",  p_ds_prof->commit_rate);

    /* compute CBS threshold and threshold shift */
    if (CTC_MAX_UINT32_VALUE != p_policer->policer.cbs)
    {
        value = p_policer->policer.cbs *125;   /* cbs*1000/8--> B/S*/;
        max_thrd = (1 << 12) -1;
        if (CTC_IS_BIT_SET(count_shift, 3))
        {
            value = (value << (count_shift&0x7));
        }
        else
        {
            value = (value >> (count_shift&0x7));
        }
        CTC_ERROR_RETURN(sys_goldengate_qos_map_token_thrd_user_to_hw(lchip, value,
                                                                      &token_thrd,
                                                                      4,
                                                                      max_thrd));
        p_ds_prof->commit_threshold = token_thrd >> 4;
        p_ds_prof->commit_threshold_shift = token_thrd & 0xF;
    }
    else
    {
        /*policer token cnt maxsize is 0x7FFFFF*/
        p_ds_prof->commit_threshold = 0xFFF;
        p_ds_prof->commit_threshold_shift = 0xB;
    }

	if(p_ds_prof->commit_rate > (p_ds_prof->commit_threshold << p_ds_prof->commit_threshold_shift))
	{
	    SYS_QOS_POLICER_DBG_INFO("CBS is too samll,token will overflow!!!!\n");
	}


    /* compute peak rate(PIR or EIR)*/
    max_rate = p_policer->policer.pir;

    if (p_policer->hbwp_en == 1)
    {
        max_rate = MAX(p_policer->hbwp.pir_max, max_rate);
    }

    CTC_ERROR_RETURN(_sys_goldengate_qos_get_policer_gran_by_rate(lchip, max_rate, &gran_pir));
    CTC_ERROR_RETURN(_sys_goldengate_qos_policer_get_count_shift(lchip, gran_pir, &count_shift));
    SYS_QOS_POLICER_DBG_INFO("pir granularity : %d\n", gran_pir);

    p_ds_prof->peak_rate = p_policer->policer.pir / gran_pir; /*B/S*/
	p_ds_prof->peak_count_shift = (count_shift & 0xF);
    SYS_QOS_POLICER_DBG_INFO(" p_ds_prof->peak_rate : %d\n",  p_ds_prof->peak_rate);

    /* compute EBS peak threshold and threshold shift */
    if (CTC_MAX_UINT32_VALUE != p_policer->policer.pbs)
    {
        value = (p_policer->policer.pbs * 1000) / 8;
        max_thrd = (1 << 12) -1;
        if (CTC_IS_BIT_SET(count_shift, 3))
        {
            value = (value << (count_shift&0x7));
        }
        else
        {
            value = (value >> (count_shift&0x7));
        }
        CTC_ERROR_RETURN(sys_goldengate_qos_map_token_thrd_user_to_hw(lchip, value,
                                                                      &token_thrd,
                                                                      4,
                                                                      max_thrd));
        p_ds_prof->peak_threshold = token_thrd >> 4;
        p_ds_prof->peak_threshold_shift = token_thrd & 0xF;
    }
    else
    {
        /*policer token cnt maxsize is 0x7FFFFF*/
        p_ds_prof->peak_threshold = 0xFFF;
        p_ds_prof->peak_threshold_shift = 0xB;
    }

	if(p_ds_prof->peak_rate > (p_ds_prof->peak_threshold << p_ds_prof->peak_threshold_shift))
	{
	    SYS_QOS_POLICER_DBG_INFO("PBS is too samll,token will overflow!!!!\n");
	}
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
    else if ((p_policer->policer.policer_mode == CTC_QOS_POLICER_MODE_RFC2697)
            || (p_policer->policer.policer_mode == CTC_QOS_POLICER_MODE_RFC4115)
            || (p_policer->policer.policer_mode == CTC_QOS_POLICER_MODE_MEF_BWP))
    {
        p_ds_prof->peak_rate_max = 0xFFFF;
        SYS_QOS_POLICER_DBG_INFO("pir_max: %d\n",  0xFFFF);
    }

    return CTC_E_NONE;
}

/**
 @brief Write policer to asic.
*/
STATIC int32
_sys_goldengate_qos_policer_add_to_asic(uint8 lchip, ctc_qos_policer_t* p_policer_param,
                                       sys_qos_policer_t* p_sys_policer,
                                       sys_qos_policer_profile_t* p_sys_prof)
{

   	DsPolicerControl_m policer_ctl;
    uint32 policer_ptr = 0;
    uint32 cmd         = 0;
    uint8 profile_id = 0;
    uint8 color_mode  = 0;
    uint8 color_drop  = 0;
    uint8 use_len  = 0;
    uint8 sr_tcm  = 0;
    uint8 cf  = 0;
    uint8 cos_index =0;
    uint8 rfc4115  = 0;
    uint32 cir_cnt = 0;
    uint32 pir_cnt = 0;

    SYS_QOS_POLICER_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_policer_param);
    CTC_PTR_VALID_CHECK(p_sys_policer);
    CTC_PTR_VALID_CHECK(p_sys_prof);

    policer_ptr = p_sys_policer->policer_ptr;

    if (p_sys_policer->cos_bitmap)
    {
        cos_index = p_policer_param->hbwp.cos_index;
        policer_ptr += cos_index;
    }
    sal_memset(&policer_ctl,0,sizeof(DsPolicerControl_m));

    profile_id = p_sys_policer->p_sys_profile[cos_index]->profile_id;
    color_mode = p_policer_param->policer.is_color_aware ? 0 : 1;
    use_len    = p_policer_param->policer.use_l3_length ? 1 : 0;
    if (p_policer_param->policer.drop_color == CTC_QOS_COLOR_NONE)
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
    if (p_policer_param->policer.policer_mode == CTC_QOS_POLICER_MODE_RFC2697)
    {
        sr_tcm = 1;
    }

    if (p_policer_param->policer.policer_mode == CTC_QOS_POLICER_MODE_MEF_BWP ||
        p_policer_param->policer.policer_mode == CTC_QOS_POLICER_MODE_RFC4115)
    {
        cf = p_policer_param->policer.cf_en?1:0;
        rfc4115    = 1;
    }
    else if (p_policer_param->policer.policer_mode == CTC_QOS_POLICER_MODE_RFC2697 )
    {
        cf = 1;
    }

    if (p_policer_param->hbwp_en)
    {
		SetDsPolicerControl(V,envelopeEn_f,&policer_ctl,p_policer_param->hbwp.sf_en);
		SetDsPolicerControl(V,couplingFlagTotal_f,&policer_ctl,!p_policer_param->hbwp.cf_total_dis);
    }

    cir_cnt = (p_sys_prof->commit_threshold << p_sys_prof->commit_threshold_shift);
    pir_cnt = (p_sys_prof->peak_threshold << p_sys_prof->peak_threshold_shift);

    /*policer token cnt maxsize is 0x7FFFFF*/
    if (cir_cnt > 0x7fffff)
    {
        cir_cnt = 0xFFF << 0xB;
    }

    if (pir_cnt > 0x7fffff)
    {
        pir_cnt = 0xFFF << 0xB;
    }

    SetDsPolicerControl(V, profile_f, &policer_ctl, profile_id);
    SetDsPolicerControl(V, colorDropCode_f, &policer_ctl, color_drop);
    SetDsPolicerControl(V, colorBlindMode_f, &policer_ctl, color_mode);
    SetDsPolicerControl(V, srTcmMode_f , &policer_ctl, sr_tcm);
    SetDsPolicerControl(V, rfc4115Mode_f , &policer_ctl, rfc4115);
    SetDsPolicerControl(V, useLayer3Length_f, &policer_ctl, use_len);
    SetDsPolicerControl(V, couplingFlag_f, &policer_ctl, cf);
    SetDsPolicerControl(V, phbStatsEn_f , &policer_ctl, 1);

    if(p_sys_policer->stats_ptr[cos_index])
    {
       SetDsPolicerControl(V, statsPtr_f , &policer_ctl, p_sys_policer->stats_ptr[cos_index]&0x3FFF);
       SetDsPolicerControl(V, statsBitmap_f , &policer_ctl, 0x7);
    }
    cmd = DRV_IOW(DsPolicerControl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, policer_ptr, cmd, &policer_ctl));

    cmd = DRV_IOW(DsPolicerCountCommit_t, DsPolicerCountCommit_commitCount_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, policer_ptr, cmd, &cir_cnt));

    cmd = DRV_IOW(DsPolicerCountExcess_t,DsPolicerCountExcess_excessCount_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, policer_ptr, cmd, &pir_cnt));

    SYS_QOS_POLICER_DBG_INFO("Add to asic DsPolicerControl_t,  index = %d, profid = %d\n",
                             policer_ptr,  p_sys_policer->p_sys_profile[cos_index]->profile_id);
    SYS_QOS_POLICER_DBG_INFO("========================================\n");
    SYS_QOS_POLICER_DBG_INFO("DsPolicerCount_CommitCount: 0x%x\n", (p_sys_prof->commit_threshold << p_sys_prof->commit_threshold_shift));
    SYS_QOS_POLICER_DBG_INFO("DsPolicerCount_PeakCount: 0x%x\n", (p_sys_prof->peak_threshold << p_sys_prof->peak_threshold_shift));

    /*For HBWP, need  configure cos 0  couplingFlagTotal_f and envelopeEn_f*/
    if (p_policer_param->hbwp_en)
    {
        uint32 field_val = 0;

        if (p_sys_policer->triple_play == 1)
        {
            policer_ptr =  p_sys_policer->policer_ptr + 1;
        }
        else
        {
            policer_ptr =  p_sys_policer->policer_ptr;
        }

        field_val = p_policer_param->hbwp.sf_en;
        cmd = DRV_IOW(DsPolicerControl_t, DsPolicerControl_envelopeEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, policer_ptr, cmd, &field_val));

        field_val = p_policer_param->hbwp.cf_total_dis ? 0 : 1;
        cmd = DRV_IOW(DsPolicerControl_t, DsPolicerControl_couplingFlagTotal_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, policer_ptr, cmd, &field_val));
    }


    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_qos_policer_remove_from_asic(uint8 lchip, ctc_qos_policer_t* p_policer_param,
                                            sys_qos_policer_t* p_sys_policer)
{
    /*For normal policer, only remover SW DB,
      For HBWP, need reset the profie to 0 for no rate share*/
    if (p_policer_param->hbwp_en)
    {
        uint32 policer_ptr = 0;
        uint32 cmd = 0;
        uint32 field_val = 0;

        policer_ptr = p_sys_policer->policer_ptr + p_policer_param->hbwp.cos_index;
        cmd = DRV_IOW(DsPolicerControl_t, DsPolicerControl_profile_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, policer_ptr, cmd, &field_val));
    }

    return CTC_E_NONE;
}

/**
 @brief Write policer to asic.
*/
STATIC int32
_sys_goldengate_qos_policer_profile_add_to_asic(uint8 lchip, sys_qos_policer_profile_t* p_sys_profile)
{
    uint32 cmd       = 0;
    DsPolicerProfile_m profile;
    SYS_QOS_POLICER_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_sys_profile);

    SetDsPolicerProfile(V, excessCountShift_f, &profile, p_sys_profile->peak_count_shift);
    SetDsPolicerProfile(V, commitCountShift_f , &profile, p_sys_profile->commit_count_shift);
    SetDsPolicerProfile(V, excessThreshold_f , &profile, p_sys_profile->peak_threshold);
    SetDsPolicerProfile(V, excessThresholdShift_f   , &profile, p_sys_profile->peak_threshold_shift);
    SetDsPolicerProfile(V, excessRate_f , &profile, p_sys_profile->peak_rate);
    SetDsPolicerProfile(V, commitThreshold_f , &profile, p_sys_profile->commit_threshold);
    SetDsPolicerProfile(V, commitThresholdShift_f , &profile, p_sys_profile->commit_threshold_shift);
    SetDsPolicerProfile(V, commitRate_f , &profile, p_sys_profile->commit_rate);
    SetDsPolicerProfile(V, excessRateMax_f , &profile, p_sys_profile->peak_rate_max);
    SetDsPolicerProfile(V, commitRateMax_f , &profile, p_sys_profile->commit_rate_max);
    cmd = DRV_IOW(DsPolicerProfile_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_sys_profile->profile_id , cmd, &profile));

    SYS_QOS_POLICER_DBG_INFO("write DsPolicerProfile_t, profid = %d\n",
                              p_sys_profile->profile_id);
    SYS_QOS_POLICER_DBG_INFO("================================================================\n");
    SYS_QOS_POLICER_DBG_INFO("commit_count_shift     : %d\n", p_sys_profile->commit_count_shift);
    SYS_QOS_POLICER_DBG_INFO("commit_rate            : %d\n", p_sys_profile->commit_rate);
    SYS_QOS_POLICER_DBG_INFO("commit_threshold_shift : %d\n", p_sys_profile->commit_threshold_shift);
    SYS_QOS_POLICER_DBG_INFO("commit_threshold       : %d\n", p_sys_profile->commit_threshold);
    SYS_QOS_POLICER_DBG_INFO("peak_count_shift       : %d\n", p_sys_profile->peak_count_shift);
    SYS_QOS_POLICER_DBG_INFO("peak_rate              : %d\n", p_sys_profile->peak_rate);
    SYS_QOS_POLICER_DBG_INFO("peak_threshold_shift   : %d\n", p_sys_profile->peak_threshold_shift);
    SYS_QOS_POLICER_DBG_INFO("peak_threshold         : %d\n", p_sys_profile->peak_threshold);
    SYS_QOS_POLICER_DBG_INFO("commit_rate_max        : %d\n", p_sys_profile->commit_rate_max);
    SYS_QOS_POLICER_DBG_INFO("peak_rate_max          : %d\n", p_sys_profile->peak_rate_max);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_qos_policer_profile_remove_from_asic(uint8 lchip, sys_qos_policer_profile_t* p_sys_profile)
{
   /*only remove from SW DB*/
   return CTC_E_NONE;
}

/**
 @brief Lookup policer in hash table.
*/
STATIC int32
_sys_goldengate_qos_policer_lookup(uint8 lchip, uint8 type,
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

    *pp_sys_policer = ctc_hash_lookup(p_gg_qos_policer_master[lchip]->p_policer_hash, &sys_policer);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_qos_policer_alloc_offset(uint8 lchip, ctc_qos_policer_t* p_policer_param,
                                                 sys_qos_policer_t* p_sys_policer)
{
    SYS_QOS_POLICER_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_sys_policer);

    if (p_sys_policer->type == CTC_QOS_POLICER_TYPE_PORT)
    {
        uint16 lport = CTC_MAP_GPORT_TO_LPORT(p_sys_policer->id);
        uint8 dir = p_sys_policer->dir;
        p_sys_policer->policer_ptr = SYS_QOS_PORT_POLICER_INDEX(lport, dir, 0);
        p_gg_qos_policer_master[lchip]->policer_count[SYS_POLICER_CNT_PORT_POLICER] += 1;
        p_sys_policer->entry_size = 1;
    }
    else if(p_sys_policer->type != CTC_QOS_POLICER_TYPE_PORT
        && p_sys_policer->dir == CTC_EGRESS)
    {
        p_sys_policer->policer_ptr = (p_sys_policer->id)*4;
        p_sys_policer->entry_size = 4;
    }
    else
    {
        uint32 offset = 0;
        sys_goldengate_opf_t opf = {0};
        opf.pool_index = 0;
        opf.pool_type = OPF_QOS_FLOW_POLICER;

        if (p_policer_param->hbwp_en)
        {
            if (p_policer_param->hbwp.triple_play)
            {/*Per EVC /Per EVC per Cos*/
                p_sys_policer->triple_play = 1;
                p_sys_policer->entry_size = 8;
            }
            else
            {
                /*asic mulst 4 mulitple*/
                p_sys_policer->entry_size = 4;
            }

            CTC_MAX_VALUE_CHECK(p_policer_param->hbwp.cos_index, p_sys_policer->entry_size-1);
            CTC_BIT_SET(p_sys_policer->cos_bitmap, p_policer_param->hbwp.cos_index);

        }
		else
        {
            p_sys_policer->entry_size = 1;
        }
        opf.multiple =  p_sys_policer->entry_size;
        CTC_ERROR_RETURN(sys_goldengate_opf_alloc_offset(lchip, &opf, p_sys_policer->entry_size, &offset));
        p_sys_policer->policer_ptr = offset;

        /* triple_play mode need alloc 8 entry, use 5 entry, free 3 entry*/
        if (p_sys_policer->triple_play == 1)
        {
            CTC_ERROR_RETURN(sys_goldengate_opf_free_offset(lchip, &opf, 3, offset));
            p_sys_policer->entry_size = 5;
            p_sys_policer->policer_ptr = offset + 3;
        }

		p_gg_qos_policer_master[lchip]->policer_count[SYS_POLICER_CNT_FLOW_POLICER] += p_sys_policer->entry_size;

    }

    SYS_QOS_POLICER_DBG_INFO("Alloc policer offset = %d\n", p_sys_policer->policer_ptr);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_qos_policer_free_offset(uint8 lchip, sys_qos_policer_t* p_sys_policer)
{
    CTC_PTR_VALID_CHECK(p_sys_policer);

    if (p_sys_policer->type != CTC_QOS_POLICER_TYPE_PORT)
    {
        if (p_sys_policer->dir == CTC_INGRESS)
        {
            uint32 offset = p_sys_policer->policer_ptr;
            sys_goldengate_opf_t opf = {0};
            opf.pool_index = 0;
            opf.pool_type = OPF_QOS_FLOW_POLICER;

            CTC_ERROR_RETURN(sys_goldengate_opf_free_offset(lchip, &opf, p_sys_policer->entry_size, offset));
            if (p_gg_qos_policer_master[lchip]->policer_count[SYS_POLICER_CNT_FLOW_POLICER] >= p_sys_policer->entry_size)
            {
                p_gg_qos_policer_master[lchip]->policer_count[SYS_POLICER_CNT_FLOW_POLICER] -= p_sys_policer->entry_size;
            }
        }
        else
        {
             /*Egress sevice policer*/
        }
    }
    else
    {
        if (p_gg_qos_policer_master[lchip]->policer_count[SYS_POLICER_CNT_PORT_POLICER] >= 1)
        {
            p_gg_qos_policer_master[lchip]->policer_count[SYS_POLICER_CNT_PORT_POLICER] -= 1;
        }
    }
    SYS_QOS_POLICER_DBG_INFO("Free policer offset = %d\n", p_sys_policer->policer_ptr);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_qos_policer_profile_alloc_offset(uint8 lchip, uint16* offset)
{
    uint32 offset_tmp = 0;
    sys_goldengate_opf_t opf = {0};

    SYS_QOS_POLICER_DBG_FUNC();

    opf.pool_index = 0;
    opf.pool_type = OPF_QOS_POLICER_PROFILE;
    CTC_ERROR_RETURN(sys_goldengate_opf_alloc_offset(lchip, &opf, 1, &offset_tmp));
    *offset = offset_tmp;


    SYS_QOS_POLICER_DBG_INFO("profile alloc offset = %d\n", *offset);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_qos_policer_profile_free_offset(uint8 lchip, uint16 offset)
{
    uint32 offset_tmp = offset;
    sys_goldengate_opf_t opf = {0};

    SYS_QOS_POLICER_DBG_FUNC();

    opf.pool_index = 0;
    opf.pool_type = OPF_QOS_POLICER_PROFILE ;
    CTC_ERROR_RETURN(sys_goldengate_opf_free_offset(lchip, &opf, 1, offset_tmp));

    SYS_QOS_POLICER_DBG_INFO("profile free offset = %d\n", offset_tmp);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_qos_policer_profile_remove(uint8 lchip, sys_qos_policer_profile_t* p_sys_profile_old)
{
    sys_qos_policer_profile_t* p_sys_profile_find = NULL;
    ctc_spool_t* p_profile_pool               = NULL;
    int32 ret            = 0;
    uint16 profile_id    = 0;


    SYS_QOS_POLICER_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_sys_profile_old);


    p_profile_pool = p_gg_qos_policer_master[lchip]->p_profile_pool;

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
        /*free offset*/
        CTC_ERROR_RETURN(_sys_goldengate_qos_policer_profile_free_offset(lchip,  profile_id));
        mem_free(p_sys_profile_find);
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_qos_policer_profile_add(uint8 lchip,  ctc_qos_policer_t* p_policer_param,
                                       sys_qos_policer_t* p_sys_policer,
                                       sys_qos_policer_profile_t** pp_sys_profile_get)
{
    ctc_spool_t* p_profile_pool    = NULL;
    sys_qos_policer_profile_t* p_sys_profile_old = NULL;
    sys_qos_policer_profile_t* p_sys_profile_new = NULL;
    int32 ret            = 0;
    uint16 old_profile_id = 0;
    uint16 profile_id    = 0;
    uint8 cos_index      = 0;
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
        cos_index = p_policer_param->hbwp.cos_index;
    }

    ret = _sys_goldengate_qos_policer_profile_build_data(lchip, p_policer_param, p_sys_profile_new);
    if (CTC_E_NONE != ret)
    {
        goto ERROR_RETURN;
    }

    p_sys_profile_old = p_sys_policer->p_sys_profile[cos_index];
    /*if use new date to replace old data, profile_id will not change*/
    if(p_sys_profile_old)
    {
        if(TRUE == _sys_goldengate_policer_profile_hash_cmp(p_sys_profile_old, p_sys_profile_new))
        {
            *pp_sys_profile_get = p_sys_profile_old;
            mem_free(p_sys_profile_new);
            return CTC_E_NONE;
        }
        old_profile_id = p_sys_profile_old->profile_id;
        p_sys_profile_new->profile_id = old_profile_id;
    }

    p_profile_pool = p_gg_qos_policer_master[lchip]->p_profile_pool;
    ret = ctc_spool_add(p_profile_pool,
                           p_sys_profile_new,
                           p_sys_profile_old,
                           pp_sys_profile_get);

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
            ret = _sys_goldengate_qos_policer_profile_alloc_offset(lchip, &profile_id);
            if (ret < 0)
            {
                ctc_spool_remove(p_profile_pool, p_sys_profile_new, NULL);
                goto ERROR_RETURN;
            }

            (*pp_sys_profile_get)->profile_id = profile_id;

        }
    }
    /*key is found, so there is an old ad need to be deleted.*/
    /*if profile_id not change, no need to delete old ad(old ad is replaced by new)*/
    if (p_sys_profile_old && (*pp_sys_profile_get)->profile_id != old_profile_id)
    {
        ret = _sys_goldengate_qos_policer_profile_remove(lchip, p_sys_profile_old);
        if (ret < 0)
        {
            ctc_spool_remove(p_profile_pool, p_sys_profile_new, NULL);
            goto ERROR_RETURN;
        }
    }

    p_sys_policer->p_sys_profile[cos_index] = *pp_sys_profile_get;
    SYS_QOS_POLICER_DBG_INFO(" profile_id = %d\n",  (*pp_sys_profile_get)->profile_id);

    return CTC_E_NONE;

ERROR_RETURN:
    mem_free(p_sys_profile_new);
    return ret;
}

int32
_sys_goldengate_qos_policer_build_node(uint8 lchip, ctc_qos_policer_t* p_policer_param,
                                      sys_qos_policer_t** pp_sys_policer)
{
    sys_qos_policer_t* p_sys_policer = NULL;
    uint16 policer_id = 0;

    SYS_QOS_POLICER_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_policer_param);
    CTC_PTR_VALID_CHECK(pp_sys_policer);

    policer_id = _sys_goldengate_qos_get_policer_id(p_policer_param->type,
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

    ctc_hash_insert(p_gg_qos_policer_master[lchip]->p_policer_hash, p_sys_policer);

    *pp_sys_policer = p_sys_policer;

    return CTC_E_NONE;
}

int32
_sys_goldengate_qos_policer_delete_node(uint8 lchip, sys_qos_policer_t* p_sys_policer)
{
    SYS_QOS_POLICER_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_sys_policer);

    ctc_hash_remove(p_gg_qos_policer_master[lchip]->p_policer_hash, p_sys_policer);

    mem_free(p_sys_policer);
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_qos_policer_add(uint8 lchip, ctc_qos_policer_t* p_policer_param,
                               sys_qos_policer_t* p_sys_policer)
{
    sys_qos_policer_profile_t* p_sys_profile_new = NULL;
    int32 ret = 0;
    uint8 first_add = 0;
    uint8 cos_index = 0;
	uint16 stats_ptr = 0;
    uint8 stats_ptr_alloc = 0;

    SYS_QOS_POLICER_DBG_FUNC();

    cos_index = (p_policer_param->hbwp_en == 1) ? p_policer_param->hbwp.cos_index : 0;

    if (NULL == p_sys_policer)
    {
        first_add = 1;
        if(p_policer_param->policer.stats_en)
        {
    	    CTC_ERROR_RETURN(sys_goldengate_stats_alloc_policing_statsptr(lchip, &stats_ptr));
            stats_ptr_alloc = 1;
        }
        /*build policer node*/
        CTC_ERROR_GOTO(_sys_goldengate_qos_policer_build_node(lchip, p_policer_param, &p_sys_policer),ret,error0);

        /*alloc policer offset*/
        CTC_ERROR_GOTO(_sys_goldengate_qos_policer_alloc_offset(lchip, p_policer_param ,p_sys_policer),ret,error1);

		p_sys_policer->stats_ptr[cos_index]  = stats_ptr;

        if (p_policer_param->hbwp_en == 1)
        {
            CTC_BIT_SET(p_sys_policer->cos_bitmap, cos_index);
        }
    }
	else
	{
        CTC_MAX_VALUE_CHECK(p_policer_param->hbwp.cos_index, p_sys_policer->entry_size-1);

	    if(p_policer_param->policer.stats_en
	   	    && (p_sys_policer->stats_ptr[cos_index] == 0))
        {
   	        CTC_ERROR_RETURN(sys_goldengate_stats_alloc_policing_statsptr(lchip, &p_sys_policer->stats_ptr[cos_index]));
            stats_ptr_alloc = 1;
        }
        else if ((!p_policer_param->policer.stats_en)
            && (p_sys_policer->stats_ptr[cos_index] != 0))
        {
            CTC_ERROR_RETURN(sys_goldengate_stats_free_policing_statsptr(lchip, p_sys_policer->stats_ptr[cos_index]));
            p_sys_policer->stats_ptr[cos_index] = 0;
        }

        if (p_policer_param->hbwp_en == 1)
        {
            CTC_BIT_SET(p_sys_policer->cos_bitmap, cos_index);
        }
	}


    /*write policer profile and table*/

    /*add policer prof*/
    CTC_ERROR_GOTO(_sys_goldengate_qos_policer_profile_add(lchip, p_policer_param, p_sys_policer, &p_sys_profile_new),ret,error2);

    /*write policer prof*/
    CTC_ERROR_GOTO(_sys_goldengate_qos_policer_profile_add_to_asic(lchip, p_sys_profile_new),ret,error3);

    /*write policer ctl and count*/
    CTC_ERROR_GOTO(_sys_goldengate_qos_policer_add_to_asic(lchip, p_policer_param, p_sys_policer, p_sys_profile_new),ret,error3);

    return ret;

error3:
    _sys_goldengate_qos_policer_profile_remove(lchip,p_sys_profile_new);
error2:
    if(first_add == 1)
    {
        _sys_goldengate_qos_policer_free_offset(lchip, p_sys_policer);
    }
error1:
    if(first_add == 1)
    {
        _sys_goldengate_qos_policer_delete_node(lchip, p_sys_policer);
    }
    else
    {
        CTC_BIT_UNSET(p_sys_policer->cos_bitmap, cos_index);
    }

error0:
    if(stats_ptr_alloc == 1)
    {
        sys_goldengate_stats_free_policing_statsptr(lchip, p_sys_policer->stats_ptr[cos_index]);
        p_sys_policer->stats_ptr[cos_index] = 0;
    }
    return ret;
}

STATIC int32
_sys_goldengate_qos_policer_remove(uint8 lchip, ctc_qos_policer_t* p_policer_param,
                                  sys_qos_policer_t* p_sys_policer)
{
    int32 ret = 0;
    uint8 cos_index = 0;

    SYS_QOS_POLICER_DBG_FUNC();

    if (NULL == p_sys_policer)
    {
        return CTC_E_QOS_POLICER_NOT_EXIST;
    }

    if (p_sys_policer->cos_bitmap && (!p_policer_param->hbwp_en))
    {
        return CTC_E_INVALID_PARAM;
    }

    if (p_policer_param->hbwp_en == 1)
    {
        cos_index = p_policer_param->hbwp.cos_index;
        CTC_MAX_VALUE_CHECK(cos_index, p_sys_policer->entry_size-1);
        if (!CTC_IS_BIT_SET(p_sys_policer->cos_bitmap, cos_index))
        {
            return CTC_E_INVALID_PARAM;
        }
    }

    CTC_ERROR_RETURN(
    _sys_goldengate_qos_policer_remove_from_asic(lchip,  p_policer_param, p_sys_policer));


    CTC_ERROR_RETURN(
    _sys_goldengate_qos_policer_profile_remove_from_asic(lchip, p_sys_policer->p_sys_profile[cos_index]));

    CTC_ERROR_RETURN(
    _sys_goldengate_qos_policer_profile_remove(lchip, p_sys_policer->p_sys_profile[cos_index]));

    if (p_sys_policer->stats_ptr[cos_index])
    {
        CTC_ERROR_RETURN(sys_goldengate_stats_free_policing_statsptr(lchip, p_sys_policer->stats_ptr[cos_index]));
        p_sys_policer->stats_ptr[cos_index] = 0;
    }

    if (p_sys_policer->cos_bitmap)
    {
        CTC_BIT_UNSET(p_sys_policer->cos_bitmap, cos_index);
        p_sys_policer->p_sys_profile[cos_index] = NULL;
    }

    if (p_sys_policer->cos_bitmap == 0)
    {
        /*free policer offset*/
        CTC_ERROR_RETURN(
        _sys_goldengate_qos_policer_free_offset(lchip, p_sys_policer));

        /*free policer node*/
        CTC_ERROR_RETURN(
        _sys_goldengate_qos_policer_delete_node(lchip, p_sys_policer));

        SYS_QOS_POLICER_DBG_INFO("remove policer !!!!!!!!!!!!\n");
    }

    return ret;
}

STATIC int32
_sys_goldengate_qos_set_policer_en(uint8 lchip, ctc_qos_policer_t* p_policer_param)
{
    int32 ret = 0;

    SYS_QOS_POLICER_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_policer_param);

    if (CTC_QOS_POLICER_TYPE_PORT == p_policer_param->type)
    {
        ret = sys_goldengate_port_set_direction_property(lchip, p_policer_param->id.gport,
                                                        CTC_PORT_DIR_PROP_PORT_POLICER_VALID,
                                                        p_policer_param->dir,
                                                        p_policer_param->enable);
    }

    return ret;
}

int32
_sys_goldengate_qos_policer_policer_id_check(uint8 lchip, uint8 type, uint8 dir, uint16 policer_id)
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
        uint16 lport  = 0;

        gport = policer_id;
        SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);
        lport = SYS_MAP_DRV_LPORT_TO_SYS_LPORT(lport);
        CTC_MAX_VALUE_CHECK(lport & 0xff, (p_gg_qos_policer_master[lchip]->slice_port_policer_base - 1));
    }
    else if (CTC_QOS_POLICER_TYPE_FLOW == type)
    {
        CTC_MIN_VALUE_CHECK(policer_id, 1);

        if (dir == CTC_EGRESS)
        {
            /*check max policer ptr for service policer ptr (logic port)*/
            CTC_MAX_VALUE_CHECK(policer_id, ((1024/p_gg_qos_policer_master[lchip]->update_gran_base)*1024)/4 -1);
        }
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
_sys_goldengate_qos_policer_bucket_check(uint8 lchip, ctc_qos_policer_t* p_policer)
{
    uint32 value                = 0;
    uint8  count_shift          = 0;
    uint32 max_cir_bucket       = 0;
    uint32 max_pir_bucket       = 0;
    uint32 max_rate             = 0;
    uint32 gran_cir             = 0;
    uint32 gran_pir             = 0;

    /* get cir granularity and count_shift*/
    max_rate =  p_policer->policer.cir;
    if (p_policer->hbwp_en == 1)
    {
        max_rate = MAX(p_policer->hbwp.cir_max, max_rate);
    }
    CTC_ERROR_RETURN(_sys_goldengate_qos_get_policer_gran_by_rate(lchip, max_rate, &gran_cir));
    CTC_ERROR_RETURN(_sys_goldengate_qos_policer_get_count_shift(lchip, gran_cir, &count_shift));

    /*policer token cnt maxsize is 0x7FFFFF*/
    if (CTC_IS_BIT_SET(count_shift, 3))
    {
        value = ((0xFFF<<0xB) >> (count_shift & 0x7));
    }
    else
    {
        value = ((0xFFF<<0xB) << (count_shift & 0x7));
    }
    max_cir_bucket = value *8;


    /* get pir granularity and count_shift*/
    max_rate = p_policer->policer.pir;
    if (p_policer->hbwp_en == 1)
    {
        max_rate = MAX(p_policer->hbwp.pir_max, max_rate);
    }

    CTC_ERROR_RETURN(_sys_goldengate_qos_get_policer_gran_by_rate(lchip, max_rate, &gran_pir));
    CTC_ERROR_RETURN(_sys_goldengate_qos_policer_get_count_shift(lchip, gran_pir, &count_shift));

    /*policer token cnt maxsize is 0x7FFFFF*/
    if (CTC_IS_BIT_SET(count_shift, 3))
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
_sys_goldengate_qos_policer_param_check(uint8 lchip, ctc_qos_policer_t* p_policer_param)
{
    uint16 policer_id = 0;

    SYS_QOS_POLICER_DBG_PARAM("type           = %d\n", p_policer_param->type);
    SYS_QOS_POLICER_DBG_PARAM("dir            = %d\n", p_policer_param->dir);
    SYS_QOS_POLICER_DBG_PARAM("enable         = %d\n", p_policer_param->enable);
    SYS_QOS_POLICER_DBG_PARAM("policer_id     = %d\n", p_policer_param->id.policer_id);
	SYS_QOS_POLICER_DBG_PARAM("stats_en       = %d\n",p_policer_param->policer.stats_en);
    SYS_QOS_POLICER_DBG_PARAM("use_l3_length  = %d\n", p_policer_param->policer.use_l3_length);
    SYS_QOS_POLICER_DBG_PARAM("is_color_aware = %d\n", p_policer_param->policer.is_color_aware);
    SYS_QOS_POLICER_DBG_PARAM("drop_color     = %d\n", p_policer_param->policer.drop_color);
    SYS_QOS_POLICER_DBG_PARAM("cir            = %d\n", p_policer_param->policer.cir);
    SYS_QOS_POLICER_DBG_PARAM("cbs            = %d\n", p_policer_param->policer.cbs);
    SYS_QOS_POLICER_DBG_PARAM("pir            = %d\n", p_policer_param->policer.pir);
    SYS_QOS_POLICER_DBG_PARAM("pbs            = %d\n", p_policer_param->policer.pbs);
    SYS_QOS_POLICER_DBG_PARAM("cf_en          = %d\n", p_policer_param->policer.cf_en);
    SYS_QOS_POLICER_DBG_PARAM("policer_mode   = %d\n", p_policer_param->policer.policer_mode);
    SYS_QOS_POLICER_DBG_PARAM("hbwp_en        = %d\n", p_policer_param->hbwp_en);
    SYS_QOS_POLICER_DBG_PARAM("sf_en          = %d\n", p_policer_param->hbwp.sf_en);
    SYS_QOS_POLICER_DBG_PARAM("sp_en          = %d\n", p_policer_param->hbwp.sp_en);
    SYS_QOS_POLICER_DBG_PARAM("cir_max        = %d\n", p_policer_param->hbwp.cir_max);
    SYS_QOS_POLICER_DBG_PARAM("pir_max        = %d\n", p_policer_param->hbwp.pir_max);
    SYS_QOS_POLICER_DBG_PARAM("cos_index      = %d\n", p_policer_param->hbwp.cos_index);


    SYS_QOS_POLICER_DBG_FUNC();

    policer_id = _sys_goldengate_qos_get_policer_id(p_policer_param->type,
                                                    p_policer_param->id.policer_id, p_policer_param->id.gport);

    CTC_ERROR_RETURN(_sys_goldengate_qos_policer_policer_id_check(lchip, p_policer_param->type,
                                                                 p_policer_param->dir,
                                                                 policer_id));

    if (!p_policer_param->enable)
    {
        return CTC_E_NONE;
    }
	if(p_gg_qos_policer_master[lchip]->flow_plus_service_policer_en && p_policer_param->hbwp_en)
	{
		return CTC_E_FEATURE_NOT_SUPPORT;
	}

    if (p_policer_param->type >= CTC_QOS_POLICER_TYPE_SERVICE)
    {
           return CTC_E_FEATURE_NOT_SUPPORT;
    }
    if (p_policer_param->hbwp_en)
    {
        if (p_policer_param->type != CTC_QOS_POLICER_TYPE_FLOW)
        {
            return CTC_E_INVALID_PARAM;
        }
        if(p_policer_param->hbwp.cos_index > p_gg_qos_policer_master[lchip]->max_cos_level)
		{
		   return CTC_E_INVALID_PARAM;
		}

        if (!p_policer_param->hbwp.sp_en)
        {
            CTC_MAX_VALUE_CHECK(p_policer_param->hbwp.weight, 1023);
        }

        CTC_MAX_VALUE_CHECK(p_policer_param->hbwp.cir_max, 100000000); /*100G*/
        CTC_MAX_VALUE_CHECK(p_policer_param->hbwp.pir_max, 100000000);/*100G*/
    }


    CTC_MAX_VALUE_CHECK(p_policer_param->policer.drop_color, CTC_QOS_COLOR_YELLOW);
	CTC_MAX_VALUE_CHECK(p_policer_param->policer.policer_mode, CTC_QOS_POLICER_MODE_MEF_BWP);
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


    CTC_MAX_VALUE_CHECK(p_policer_param->policer.cir, 100000000);/*100G*/
    CTC_MAX_VALUE_CHECK(p_policer_param->policer.pir, 100000000);/*100G*/

    CTC_ERROR_RETURN(_sys_goldengate_qos_policer_bucket_check(lchip, p_policer_param));

    return CTC_E_NONE;
}

/**
 @brief Globally set policer granularity mode.
  mode 0: default
  mode 1: modify gran for TCP test
*/
int32
sys_goldengate_qos_policer_set_gran_mode(uint8 lchip, uint8 mode)
{
    SYS_QOS_POLICER_INIT_CHECK(lchip);
    CTC_MAX_VALUE_CHECK(mode, 2);

    SYS_QOS_POLICER_LOCK(lchip);
    p_gg_qos_policer_master[lchip]->policer_gran_mode = mode;
    SYS_QOS_POLICER_UNLOCK(lchip);
    CTC_ERROR_RETURN(sys_godengate_qos_set_shape_gran(lchip,mode));

    return CTC_E_NONE;
}

extern int32
sys_goldengate_qos_policer_set(uint8 lchip, ctc_qos_policer_t* p_policer_param)
{
    sys_qos_policer_t* p_sys_policer = NULL;
    uint16 policer_id = 0;

    SYS_QOS_POLICER_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_policer_param);

    SYS_QOS_POLICER_LOCK(lchip);
    CTC_ERROR_RETURN_QOS_POLICER_UNLOCK(_sys_goldengate_qos_policer_param_check(lchip, p_policer_param));

    policer_id = _sys_goldengate_qos_get_policer_id(p_policer_param->type,
                                                    p_policer_param->id.policer_id, p_policer_param->id.gport);


    CTC_ERROR_RETURN_QOS_POLICER_UNLOCK(_sys_goldengate_qos_policer_lookup(lchip, p_policer_param->type,
                                                       p_policer_param->dir,
                                                       policer_id,
                                                       &p_sys_policer));
    if (p_policer_param->enable)
    {
        CTC_ERROR_RETURN_QOS_POLICER_UNLOCK(_sys_goldengate_qos_policer_add(lchip, p_policer_param, p_sys_policer));
        CTC_ERROR_RETURN_QOS_POLICER_UNLOCK(_sys_goldengate_qos_set_policer_en(lchip, p_policer_param));
    }
    else
    {
        CTC_ERROR_RETURN_QOS_POLICER_UNLOCK(_sys_goldengate_qos_set_policer_en(lchip, p_policer_param));
        CTC_ERROR_RETURN_QOS_POLICER_UNLOCK(_sys_goldengate_qos_policer_remove(lchip, p_policer_param, p_sys_policer));
    }
    SYS_QOS_POLICER_UNLOCK(lchip);
    return CTC_E_NONE;
}

int32
sys_goldengate_qos_policer_get(uint8 lchip, ctc_qos_policer_t* p_policer_param)
{
    sys_qos_policer_t* p_sys_policer = NULL;
    uint16 policer_id = 0;
    uint32 cir = 0;
    uint32 cbs = 0;
    uint32 pir = 0;
    uint32 pbs = 0;
    uint8 cos_index = 0;
    uint32 cmd = 0;
    uint8 sr_tcm  = 0;
    uint8 rfc4115  = 0;
    uint8 cf  = 0;
    uint32 value = 0;
    DsPolicerControl_m policer_ctl;

    SYS_QOS_POLICER_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_policer_param);
    sal_memset(&policer_ctl, 0, sizeof(DsPolicerControl_m));
    SYS_QOS_POLICER_LOCK(lchip);
    policer_id = _sys_goldengate_qos_get_policer_id(p_policer_param->type,
                                                    p_policer_param->id.policer_id, p_policer_param->id.gport);


    CTC_ERROR_RETURN_QOS_POLICER_UNLOCK(_sys_goldengate_qos_policer_lookup(lchip, p_policer_param->type,
                                                       p_policer_param->dir,
                                                       policer_id,
                                                       &p_sys_policer));
    if (NULL == p_sys_policer || p_sys_policer->p_sys_profile[p_policer_param->hbwp.cos_index] == NULL)
    {
        SYS_QOS_POLICER_UNLOCK(lchip);
        return CTC_E_NOT_EXIST;
    }
    if(p_policer_param->hbwp.cos_index > p_gg_qos_policer_master[lchip]->max_cos_level)
    {
        SYS_QOS_POLICER_UNLOCK(lchip);
        return CTC_E_INVALID_PARAM;
    }
    p_policer_param->enable = 1;
    p_policer_param->hbwp_en = p_sys_policer->cos_bitmap ? 1:0;
    cos_index = p_policer_param->hbwp.cos_index;
    _sys_goldengate_qos_policer_map_token_rate_hw_to_user(lchip, p_sys_policer->p_sys_profile[cos_index]->commit_rate, p_sys_policer->p_sys_profile[cos_index]->commit_count_shift, &cir);
    cbs = ( p_sys_policer->p_sys_profile[cos_index]->commit_threshold <<  p_sys_policer->p_sys_profile[cos_index]->commit_threshold_shift) / 125;/*125 = 8/1000* -->kbps*/;
    cbs = CTC_IS_BIT_SET(p_sys_policer->p_sys_profile[cos_index]->commit_count_shift, 3)?(cbs >> (p_sys_policer->p_sys_profile[cos_index]->commit_count_shift&0x7)):(cbs << (p_sys_policer->p_sys_profile[cos_index]->commit_count_shift&0x7));
    _sys_goldengate_qos_policer_map_token_rate_hw_to_user(lchip, p_sys_policer->p_sys_profile[cos_index]->peak_rate, p_sys_policer->p_sys_profile[cos_index]->peak_count_shift, &pir);
    pbs = (p_sys_policer->p_sys_profile[cos_index]->peak_threshold << p_sys_policer->p_sys_profile[cos_index]->peak_threshold_shift) / 125;     /*125 = 8/1000*/;
    pbs = CTC_IS_BIT_SET(p_sys_policer->p_sys_profile[cos_index]->peak_count_shift, 3)?(pbs >> (p_sys_policer->p_sys_profile[cos_index]->peak_count_shift&0x7)):(pbs << (p_sys_policer->p_sys_profile[cos_index]->peak_count_shift&0x7));
    p_policer_param->policer.cir = cir;
    p_policer_param->policer.cbs = cbs;
    p_policer_param->policer.pir = pir;
    p_policer_param->policer.pbs = pbs;
    p_policer_param->policer.stats_en = p_sys_policer->stats_ptr[cos_index] ? 1:0;
    cmd = DRV_IOR(DsPolicerControl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_QOS_POLICER_UNLOCK(DRV_IOCTL(lchip, p_sys_policer->policer_ptr + cos_index, cmd, &policer_ctl));
    rfc4115 = GetDsPolicerControl(V, rfc4115Mode_f , &policer_ctl);
    sr_tcm = GetDsPolicerControl(V, srTcmMode_f , &policer_ctl);
    if(rfc4115 && p_policer_param->hbwp_en)
    {
        p_policer_param->policer.policer_mode = CTC_QOS_POLICER_MODE_MEF_BWP;
    }
    else if(rfc4115 && !p_policer_param->hbwp_en)
    {
        p_policer_param->policer.policer_mode = CTC_QOS_POLICER_MODE_RFC4115;
    }
    else if(sr_tcm)
    {
        p_policer_param->policer.policer_mode = CTC_QOS_POLICER_MODE_RFC2697;
    }
    else
    {
        p_policer_param->policer.policer_mode = CTC_QOS_POLICER_MODE_RFC2698;
    }
    cf = GetDsPolicerControl(V, couplingFlag_f , &policer_ctl);
    if(rfc4115 && cf)
    {
        p_policer_param->policer.cf_en = 1;
    }
    p_policer_param->policer.is_color_aware = GetDsPolicerControl(V, colorBlindMode_f , &policer_ctl) ? 0 : 1;
    p_policer_param->policer.use_l3_length = GetDsPolicerControl(V, useLayer3Length_f , &policer_ctl);
    value = GetDsPolicerControl(V, colorDropCode_f , &policer_ctl);
    if(value == 1)
    {
        p_policer_param->policer.drop_color = CTC_QOS_COLOR_NONE;
    }
    else if(value == 2)
    {
        p_policer_param->policer.drop_color = CTC_QOS_COLOR_RED;
    }
    else if(value == 3)
    {
        p_policer_param->policer.drop_color = CTC_QOS_COLOR_YELLOW;
    }

    if(p_policer_param->hbwp_en)
    {
        p_policer_param->hbwp.sf_en = GetDsPolicerControl(V, envelopeEn_f , &policer_ctl);
        p_policer_param->hbwp.triple_play = p_sys_policer->triple_play;
        p_policer_param->hbwp.cf_total_dis = GetDsPolicerControl(V, couplingFlagTotal_f , &policer_ctl)? 0:1;
        _sys_goldengate_qos_policer_map_token_rate_hw_to_user(lchip, p_sys_policer->p_sys_profile[cos_index]->commit_rate_max, p_sys_policer->p_sys_profile[cos_index]->commit_count_shift, &cir);
        _sys_goldengate_qos_policer_map_token_rate_hw_to_user(lchip, p_sys_policer->p_sys_profile[cos_index]->peak_rate_max, p_sys_policer->p_sys_profile[cos_index]->peak_count_shift, &pir);
        p_policer_param->hbwp.cir_max = cir;
        p_policer_param->hbwp.pir_max = pir;
    }
    SYS_QOS_POLICER_UNLOCK(lchip);
    return CTC_E_NONE;
}

int32
sys_goldengate_qos_policer_index_get(uint8 lchip, uint16 plc_id,uint16* p_policer_ptr,
                                    uint8 *is_bwp,uint8 *triple_play)
{
    sys_qos_policer_t* p_sys_policer = NULL;
    SYS_QOS_POLICER_DBG_FUNC();

    SYS_QOS_POLICER_DBG_PARAM("Get: dir            = %d\n", plc_id);

    SYS_QOS_POLICER_LOCK(lchip);
    CTC_ERROR_RETURN_QOS_POLICER_UNLOCK(_sys_goldengate_qos_policer_lookup(lchip, CTC_QOS_POLICER_TYPE_FLOW,
                                                       CTC_INGRESS,
                                                       plc_id,
                                                       &p_sys_policer));
    SYS_QOS_POLICER_UNLOCK(lchip);
    if (NULL == p_sys_policer)
    {
        return CTC_E_QOS_POLICER_NOT_EXIST;
    }

    *p_policer_ptr = p_sys_policer->policer_ptr;
	*is_bwp =       p_sys_policer->cos_bitmap?1:0;
    *triple_play = p_sys_policer->triple_play ;

    SYS_QOS_POLICER_DBG_INFO("Get: policer_ptr        = %d\n", p_sys_policer->policer_ptr);

    return CTC_E_NONE;
}

extern int32
sys_goldengate_qos_set_policer_update_enable(uint8 lchip, uint8 enable)
{
    uint32 cmd = 0;
    PolicingCtl_m policer_ctl;
    SYS_QOS_POLICER_DBG_FUNC();

    cmd = DRV_IOR(PolicingCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &policer_ctl));

    SetPolicingCtl(V,tsTickGenEn_f,&policer_ctl,enable);
    SetPolicingCtl(V,updateEn_f,&policer_ctl,enable);
    cmd = DRV_IOW(PolicingCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &policer_ctl));

    return CTC_E_NONE;
}


extern int32
sys_goldengate_qos_set_phb(uint8 lchip, ctc_qos_phb_map_t *p_phb_map)
{
    uint32 cmd = 0;
    uint32 value = 0;
    uint32  field = 0;

    SYS_QOS_POLICER_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_phb_map);


    SYS_QOS_POLICER_DBG_PARAM("p_phb_map->map_type = %d\n", p_phb_map->map_type);
    SYS_QOS_POLICER_DBG_PARAM("p_phb_map->priority = %d\n", p_phb_map->priority);
    SYS_QOS_POLICER_DBG_PARAM("p_phb_map->phb      = %d\n", p_phb_map->cos_index);

	if(p_gg_qos_policer_master[lchip]->flow_plus_service_policer_en )
	{
		return CTC_E_FEATURE_NOT_SUPPORT;
	}

    if (p_phb_map->map_type == CTC_QOS_PHB_MAP_PRI)
    {
        CTC_MAX_VALUE_CHECK(p_phb_map->priority, SYS_QOS_CLASS_PRIORITY_MAX);
        CTC_MAX_VALUE_CHECK(p_phb_map->cos_index, p_gg_qos_policer_master[lchip]->max_cos_level);
    }
    else
    {
        return CTC_E_NONE;
    }

    value = p_phb_map->cos_index;

	field = IpeClassificationPhbOffset_array_0_phbOffset_f
		  + (p_phb_map->priority%16 - IpeClassificationPhbOffset_array_0_phbOffset_f);

    cmd = DRV_IOW(IpeClassificationPhbOffset_t, field);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_phb_map->priority / 16, cmd, &value));

    cmd = DRV_IOW(EpeClassificationPhbOffset_t, p_phb_map->priority);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));

    return CTC_E_NONE;
}


extern int32
sys_goldengate_qos_get_phb(uint8 lchip, ctc_qos_phb_map_t *p_phb_map)
{
   uint32 cmd = 0;
   uint32 value = 0;

    SYS_QOS_POLICER_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_phb_map);

    if (p_phb_map->map_type == CTC_QOS_PHB_MAP_PRI)
    {
        CTC_MAX_VALUE_CHECK(p_phb_map->priority, SYS_QOS_CLASS_PRIORITY_MAX);

        cmd = DRV_IOR(EpeClassificationPhbOffset_t, p_phb_map->priority);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
        p_phb_map->cos_index = value;
    }
    else
    {
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

extern int32
sys_goldengate_qos_set_policer_flow_first(uint8 lchip, uint8 dir, uint8 enable)
{

	IpeClassificationCtl_m  cls_ctl;
	EpeClassificationCtl_m  epe_cls_ctl;
    uint32 cmd = 0;

    SYS_QOS_POLICER_DBG_FUNC();

    sal_memset(&cls_ctl, 0, sizeof(cls_ctl));
    sal_memset(&epe_cls_ctl, 0, sizeof(epe_cls_ctl));

    if (dir == CTC_INGRESS || dir == CTC_BOTH_DIRECTION)
    {
        cmd = DRV_IOR(IpeClassificationCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &cls_ctl))
        /*IpeClassificationCtl_t*/
        SetIpeClassificationCtl(V, flowPolicerFirst_f, &cls_ctl, enable);
        cmd = DRV_IOW(IpeClassificationCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &cls_ctl));
    }
    if (dir == CTC_EGRESS || dir == CTC_BOTH_DIRECTION)
    {
        cmd = DRV_IOR(EpeClassificationCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_cls_ctl))
        /*EpeClassificationCtl_t*/
        SetEpeClassificationCtl(V, flowPolicerFirst_f, &epe_cls_ctl, enable);
        cmd = DRV_IOW(EpeClassificationCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_cls_ctl));
    }
    return CTC_E_NONE;
}

int32
sys_goldengate_qos_set_policer_sequential_enable(uint8 lchip, uint8 enable)
{
	PolicingCtl_m  policing_ctl;
    uint32 cmd = 0;

    SYS_QOS_POLICER_DBG_FUNC();

    cmd = DRV_IOR(PolicingCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &policing_ctl))
    SetPolicingCtl(V,sequentialPolicing_f,&policing_ctl,enable);
	cmd = DRV_IOW(PolicingCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &policing_ctl));
    return CTC_E_NONE;
}

int32
sys_goldengate_qos_set_policer_ipg_enable(uint8 lchip, uint8 enable)
{
	PolicingCtl_m  policing_ctl;
    uint32 cmd = 0;

    SYS_QOS_POLICER_DBG_FUNC();

    cmd = DRV_IOR(PolicingCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &policing_ctl))
    SetPolicingCtl(V,ipgEn_f,&policing_ctl,enable);
	cmd = DRV_IOW(PolicingCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &policing_ctl));
    return CTC_E_NONE;
}

int32
sys_goldengate_qos_set_policer_mark_ecn_enable(uint8 lchip, uint8 enable)
{
    uint32 field_val = 0;
    uint32 cmd = 0;

    SYS_QOS_POLICER_DBG_FUNC();

    cmd = DRV_IOR(IpeForwardReserved2_t, IpeForwardReserved2_reserved_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val))

    /*reserved[10] enable , reserved[9:8] policer mark yellow color*/
    field_val &= (~(0x7 << 8)); /*clear bit reserved[10:8]*/

    if (enable)
    {
        field_val |= (1<<10)|(0x2<<8);
    }

	cmd = DRV_IOW(IpeForwardReserved2_t, IpeForwardReserved2_reserved_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    return CTC_E_NONE;
}


int32
sys_goldengate_qos_set_policer_hbwp_share_enable(uint8 lchip, uint8 enable)
{
    return CTC_E_FEATURE_NOT_SUPPORT;
}


int32
sys_goldengate_qos_set_policer_stats_enable(uint8 lchip, uint8 enable)
{
    SYS_QOS_POLICER_DBG_FUNC();

    CTC_ERROR_RETURN(sys_goldengate_stats_set_policing_en(lchip, enable));

    return CTC_E_NONE;
}

int32
sys_goldengate_qos_policer_stats_query(uint8 lchip, ctc_qos_policer_stats_t* p_stats_param)
{
    sys_stats_policing_t stats_result;
    sys_qos_policer_t* p_sys_policer = NULL;
    ctc_qos_policer_stats_info_t* p_stats = NULL;
    uint16 stats_ptr = 0;
	uint8 cos_index = 0;
    uint16 policer_id = 0;

    SYS_QOS_POLICER_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_stats_param);

    SYS_QOS_POLICER_LOCK(lchip);
    policer_id = _sys_goldengate_qos_get_policer_id(p_stats_param->type,
                                                    p_stats_param->id.policer_id, p_stats_param->id.gport);

    CTC_ERROR_RETURN_QOS_POLICER_UNLOCK(_sys_goldengate_qos_policer_policer_id_check(lchip, p_stats_param->type,
                                                                 p_stats_param->dir,
                                                                 policer_id));

    CTC_ERROR_RETURN_QOS_POLICER_UNLOCK(_sys_goldengate_qos_policer_lookup(lchip, p_stats_param->type,
                                                       p_stats_param->dir,
                                                       policer_id,
                                                       &p_sys_policer));

    SYS_QOS_POLICER_UNLOCK(lchip);
    if (!p_sys_policer)
    {
        return CTC_E_QOS_POLICER_NOT_EXIST;
    }

    if (p_stats_param->hbwp_en)
    {
        CTC_MAX_VALUE_CHECK(p_stats_param->cos_index, 3);
		cos_index = p_stats_param->cos_index;
    }
    else
    {
        cos_index = 0;
    }
    stats_ptr = p_sys_policer->stats_ptr[cos_index];
    p_stats = &p_stats_param->stats;

    if (0 == stats_ptr)
    {
        return CTC_E_STATS_NOT_ENABLE;
    }

    SYS_QOS_POLICER_DBG_INFO("stats_ptr:%d\n", stats_ptr);

    CTC_ERROR_RETURN(sys_goldengate_stats_get_policing_stats(lchip,  stats_ptr, &stats_result));
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
sys_goldengate_qos_policer_stats_clear(uint8 lchip, ctc_qos_policer_stats_t* p_stats_param)
{

    sys_qos_policer_t* p_sys_policer = NULL;
    uint16 stats_ptr = 0;
	uint8 cos_index = 0;
    uint16 policer_id = 0;

    SYS_QOS_POLICER_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_stats_param);

    SYS_QOS_POLICER_LOCK(lchip);
    policer_id = _sys_goldengate_qos_get_policer_id(p_stats_param->type,
                                                    p_stats_param->id.policer_id, p_stats_param->id.gport);

    CTC_ERROR_RETURN_QOS_POLICER_UNLOCK(_sys_goldengate_qos_policer_policer_id_check(lchip, p_stats_param->type,
                                                                 p_stats_param->dir,
                                                                 policer_id));

    CTC_ERROR_RETURN_QOS_POLICER_UNLOCK(_sys_goldengate_qos_policer_lookup(lchip, p_stats_param->type,
                                                       p_stats_param->dir,
                                                       policer_id,
                                                       &p_sys_policer));
    SYS_QOS_POLICER_UNLOCK(lchip);
    if (!p_sys_policer)
    {
        return CTC_E_QOS_POLICER_NOT_EXIST;
    }

    if (p_stats_param->hbwp_en)
    {
        CTC_MAX_VALUE_CHECK(p_stats_param->cos_index, p_gg_qos_policer_master[lchip]->max_cos_level);
		cos_index = p_stats_param->cos_index;
    }
    else
    {
        cos_index = 0;
    }
    stats_ptr = p_sys_policer->stats_ptr[cos_index];
    SYS_QOS_POLICER_DBG_INFO("stats_ptr:%d\n", stats_ptr);

    CTC_ERROR_RETURN(sys_goldengate_stats_clear_policing_stats(lchip,  stats_ptr));

    return CTC_E_NONE;
}

int32
sys_goldengate_qos_policer_dump_status(uint8 lchip)
{
    uint32 cmd = 0;
    uint32 value1 = 0;
    uint32 value2 = 0;

    SYS_QOS_POLICER_INIT_CHECK(lchip);
    SYS_QOS_POLICER_DBG_DUMP("--------------------------Policer--------------------------\n");
	cmd = DRV_IOR(PolicingCtl_t, PolicingCtl_updateEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value1))
    SYS_QOS_POLICER_DBG_DUMP("%-30s: %d\n","Global Policer enable", value1);
	cmd = DRV_IOR(PolicingCtl_t, PolicingCtl_statsEnConfirm_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value1))
    SYS_QOS_POLICER_DBG_DUMP("%-30s: %d\n","Global Policer Stats enable", value1);
	cmd = DRV_IOR(IpeClassificationCtl_t, IpeClassificationCtl_flowPolicerFirst_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value1));
    cmd = DRV_IOR(EpeClassificationCtl_t, EpeClassificationCtl_flowPolicerFirst_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value2));
    SYS_QOS_POLICER_DBG_DUMP("%-30s: %d \n","flowPolicerFirst[in]", value1);
    SYS_QOS_POLICER_DBG_DUMP("%-30s: %d \n","flowPolicerFirst[out]",  value2);
     cmd = DRV_IOR(PolicingCtl_t, PolicingCtl_ipgEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value1))
    SYS_QOS_POLICER_DBG_DUMP("%-30s: %d\n","IPG enable", value1);
    cmd = DRV_IOR(PolicingCtl_t, PolicingCtl_sequentialPolicing_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value1))
    SYS_QOS_POLICER_DBG_DUMP("%-30s: %d\n","SequentialPolicing enable",value1);
    SYS_QOS_POLICER_DBG_DUMP("%-30s: %d\n","Max Cos Level",p_gg_qos_policer_master[lchip]->max_cos_level);

    sys_goldengate_ftm_query_table_entry_num(lchip, DsPolicerProfile_t, &value1);
    SYS_QOS_POLICER_DBG_DUMP("%-30s: %d\n", "Total Policer",(1024/p_gg_qos_policer_master[lchip]->update_gran_base)*1024);
    SYS_QOS_POLICER_DBG_DUMP("%-30s: %d\n", "--Reserved for port Policer", p_gg_qos_policer_master[lchip]->port_policer_base[CTC_EGRESS]*2);
    SYS_QOS_POLICER_DBG_DUMP("%-30s: %d\n","--Port Policer count", p_gg_qos_policer_master[lchip]->policer_count[SYS_POLICER_CNT_PORT_POLICER]);
    SYS_QOS_POLICER_DBG_DUMP("%-30s: %d\n","--Flow Policer count", p_gg_qos_policer_master[lchip]->policer_count[SYS_POLICER_CNT_FLOW_POLICER]);
    SYS_QOS_POLICER_DBG_DUMP("%-30s: %d\n","Total Policer profile", value1-1);
    SYS_QOS_POLICER_DBG_DUMP("%-30s: %d\n","--Used count", p_gg_qos_policer_master[lchip]->p_profile_pool->count);
    SYS_QUEUE_DBG_DUMP("\n");

    return CTC_E_NONE;
}

int32
sys_goldengate_qos_policer_dump(uint8 type,
                               uint8 dir,
                               uint8 lchip,
                               uint16 start,
                               uint16 end,
                               uint8 detail)
{
    uint32 index = 0;
    uint8 gchip = 0;
    uint16 policer_id = 0;
    sys_qos_policer_t* p_sys_policer = NULL;
    sys_qos_policer_profile_t* p_sys_profile = NULL;
    uint32 cir = 0;
    uint32 cbs = 0;
    uint32 pir = 0;
    uint32 pbs = 0;
    uint8 loop_end = 0;
    uint16 start_id = 0;
    uint16 end_id = 0;
    uint8 cos_index = 0;
    uint8 first_cos_index = 1;

    SYS_QOS_POLICER_INIT_CHECK(lchip);

    if (start > end)
    {
        return CTC_E_NONE;
    }

    if (type == CTC_QOS_POLICER_TYPE_PORT)
    {
        SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(start, lchip, start_id);
        SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(end, lchip, end_id);
    }
    else
    {
        start_id = start;
        end_id = end;
    }

    if (type == CTC_QOS_POLICER_TYPE_PORT)
    {
        SYS_QOS_POLICER_DBG_DUMP("%-6s %-7s %-7s", "port", "cos_idx", "enable");
    }
    else if (type == CTC_QOS_POLICER_TYPE_FLOW)
    {
        SYS_QOS_POLICER_DBG_DUMP("%-6s %-7s %-7s", "plc-id", "cos_idx", "enable");
    }

    SYS_QOS_POLICER_DBG_DUMP("%-10s %-10s %-8s %-8s %-8s\n", "cir", "pir", "cbs", "pbs", "stats-en");
    SYS_QOS_POLICER_DBG_DUMP("-------------------------------------------------------------------------------\n");

    for (index = start_id; index <= end_id; index++)
    {
        if (type == CTC_QOS_POLICER_TYPE_PORT)
        {

            sys_goldengate_get_gchip_id(lchip, &gchip);
            policer_id = CTC_MAP_LPORT_TO_GPORT(gchip, index);
        }
        else if (type == CTC_QOS_POLICER_TYPE_FLOW)
        {
            policer_id = index;
        }

        SYS_QOS_POLICER_LOCK(lchip);
        CTC_ERROR_RETURN_QOS_POLICER_UNLOCK(_sys_goldengate_qos_policer_lookup(lchip, type,
                                                           dir,
                                                           policer_id,
                                                           &p_sys_policer));
        SYS_QOS_POLICER_UNLOCK(lchip);

        if (NULL == p_sys_policer)
        {
            SYS_QOS_POLICER_DBG_DUMP("0x%04x %-7s %-6d ", policer_id, "-", 0);
            SYS_QOS_POLICER_DBG_DUMP("%-10d %-10d %-8d %-8d %-8d\n",
                                     0, 0, 0, 0, 0);
            continue;
        }

        loop_end = p_gg_qos_policer_master[lchip]->flow_plus_service_policer_en ?1:p_sys_policer->entry_size;
        for (cos_index = 0;  cos_index < loop_end; cos_index++)
        {
            p_sys_profile = p_sys_policer->p_sys_profile[cos_index];

            if (NULL == p_sys_profile)
            {
                continue;
            }
            if (p_sys_policer->cos_bitmap)
            {
                if (first_cos_index)
                {
                    SYS_QOS_POLICER_DBG_DUMP("0x%04x %-7d %-6d ", policer_id, cos_index, 1);
                    first_cos_index = 0;
                }
                else
                {
                    SYS_QOS_POLICER_DBG_DUMP("       %-7d %-6d ", cos_index, 1);
                }
            }
            else
            {
                SYS_QOS_POLICER_DBG_DUMP("0x%04x %-7s %-6d ", policer_id, "-", 1);
            }
            _sys_goldengate_qos_policer_map_token_rate_hw_to_user(lchip, p_sys_profile->commit_rate, p_sys_profile->commit_count_shift, &cir);
            cbs = (p_sys_profile->commit_threshold << p_sys_profile->commit_threshold_shift) / 125;/*125 = 8/1000* -->kbps*/;
            cbs = CTC_IS_BIT_SET(p_sys_profile->commit_count_shift, 3)?(cbs >> (p_sys_profile->commit_count_shift&0x7)):
            (cbs << (p_sys_profile->commit_count_shift&0x7));
            _sys_goldengate_qos_policer_map_token_rate_hw_to_user(lchip, p_sys_profile->peak_rate, p_sys_profile->peak_count_shift, &pir);
            pbs = (p_sys_profile->peak_threshold << p_sys_profile->peak_threshold_shift) / 125;     /*125 = 8/1000*/;
            pbs = CTC_IS_BIT_SET(p_sys_profile->peak_count_shift, 3)?(pbs >> (p_sys_profile->peak_count_shift&0x7)):
            (pbs << (p_sys_profile->peak_count_shift&0x7));
            SYS_QOS_POLICER_DBG_DUMP("%-10d %-10d %-8d %-8d %-8d\n", cir,  pir,  cbs, pbs, p_sys_policer->stats_ptr[0]?1:0);
        }
        first_cos_index = 1;
    }


    if (start_id == end_id && p_sys_policer)
    {
        SYS_QUEUE_DBG_DUMP("\nDetail information:\n");
        SYS_QUEUE_DBG_DUMP("----------------------------------------------------------\n");
        SYS_QOS_POLICER_DBG_DUMP("%-7s %-11s %-8s %-8s %-7s\n","cos_idx","policer_ptr","prof_id","stats_en","stats_ptr");
        SYS_QOS_POLICER_DBG_DUMP("------------------------------------------------------------------------------------\n");
        loop_end = p_gg_qos_policer_master[lchip]->flow_plus_service_policer_en ?1:p_sys_policer->entry_size;

        for (index = 0;  index < loop_end; index++)
        {

            p_sys_profile = p_sys_policer->p_sys_profile[index];

            if (NULL ==  p_sys_profile)
            {
                continue;
            }

            SYS_QOS_POLICER_DBG_DUMP("%-7d %-11d %-8d ", index, p_sys_policer->policer_ptr + index, 	p_sys_profile->profile_id);
            SYS_QOS_POLICER_DBG_DUMP("%-8d %-7d\n", p_sys_policer->stats_ptr[index] ? 1:0, p_sys_policer->stats_ptr[index]);
        }
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_qos_policer_init(uint8 lchip,  ctc_qos_global_cfg_t *p_glb_parm)
{
    uint32 policer_num         = 0;
    uint32 policer_profile_num = 0;
    uint32 flow_policer_base    = 0;
    uint32 flow_policer_num    = 0;
    uint32 divide_factor       = 0;
    sys_goldengate_opf_t opf    = {0};
    ctc_spool_t spool;
    uint16 max_port_polier_num = 0;

    sal_memset(&spool, 0, sizeof(ctc_spool_t));

    if (p_gg_qos_policer_master[lchip])
    {
        return CTC_E_NONE;
    }

    p_gg_qos_policer_master[lchip] = mem_malloc(MEM_QUEUE_MODULE, sizeof(sys_qos_policer_master_t));
    if (NULL == p_gg_qos_policer_master[lchip])
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_gg_qos_policer_master[lchip], 0, sizeof(sys_qos_policer_master_t));

    SYS_QOS_POLICER_CREATE_LOCK(lchip);

    if (p_glb_parm->max_cos_level == 0)
    {
        p_glb_parm->max_cos_level = 3;
    }

    p_gg_qos_policer_master[lchip]->max_cos_level = p_glb_parm->max_cos_level;
	p_gg_qos_policer_master[lchip]->flow_plus_service_policer_en = (p_glb_parm->max_cos_level == 1); /* service policer + flow policer mode*/
    p_gg_qos_policer_master[lchip]->p_policer_hash  = ctc_hash_create(
            SYS_QOS_POLICER_HASH_BLOCK_SIZE,
            SYS_QOS_POLICER_HASH_TBL_SIZE,
            (hash_key_fn)_sys_goldengate_policer_hash_make,
            (hash_cmp_fn)_sys_goldengate_policer_hash_cmp);


    spool.lchip = lchip;
    spool.block_num = 1;
    spool.block_size = SYS_QOS_POLICER_PROF_HASH_BLOCK_SIZE;
    spool.max_count = SYS_QOS_POLICER_MAX_PROFILE_NUM - 1;
    spool.user_data_size = sizeof(sys_qos_policer_profile_t);
    spool.spool_key = (hash_key_fn)_sys_goldengate_policer_profile_hash_make;
    spool.spool_cmp = (hash_cmp_fn)_sys_goldengate_policer_profile_hash_cmp;
    p_gg_qos_policer_master[lchip]->p_profile_pool = ctc_spool_create(&spool);



    sys_goldengate_ftm_query_table_entry_num(lchip, DsPolicerControl_t, &policer_num);
    sys_goldengate_ftm_query_table_entry_num(lchip, DsPolicerProfile_t, &policer_profile_num);

    if (CTC_QOS_PORT_QUEUE_NUM_4_BPE == p_glb_parm->queue_num_per_network_port)
    {
        max_port_polier_num = MAX_PORT_POLICER_NUM_4Q;
    }
    else if (CTC_QOS_PORT_QUEUE_NUM_8_BPE == p_glb_parm->queue_num_per_network_port)
    {
        max_port_polier_num = MAX_PORT_POLICER_NUM_8Q;
    }
    else
    {
        max_port_polier_num = MAX_PORT_POLICER_NUM;
    }

    p_gg_qos_policer_master[lchip]->port_policer_base[CTC_INGRESS] = 0;
    p_gg_qos_policer_master[lchip]->port_policer_base[CTC_EGRESS]  = max_port_polier_num;
    p_gg_qos_policer_master[lchip]->slice_port_policer_base = max_port_polier_num/2;


    if (p_glb_parm->policer_num == 1024)
    {
        p_gg_qos_policer_master[lchip]->update_gran_base = 1024;
        divide_factor = 0x4c3b4; /*frequence = 128K*/
        policer_num = p_glb_parm->policer_num;
    }
    else if (p_glb_parm->policer_num == 2048)
    {
        p_gg_qos_policer_master[lchip]->update_gran_base = 512;
        divide_factor = 0x98868; /*frequence = 64K*/
        policer_num = p_glb_parm->policer_num;
    }
    else if (p_glb_parm->policer_num == 8192)
    {
        p_gg_qos_policer_master[lchip]->update_gran_base = 128;
        divide_factor = 0x2624a0;  /*frequence = 16K*/
        policer_num = p_glb_parm->policer_num;
    }
    else
    {
        p_gg_qos_policer_master[lchip]->update_gran_base = 256;
        divide_factor = 0x1311d0; /*frequence = 32K*/
        policer_num = 4096;
    }

    flow_policer_base = max_port_polier_num*2; /* from ingress and egress port policer*/
    flow_policer_num = policer_num - flow_policer_base;

	CTC_ERROR_RETURN(_sys_goldengate_qos_policer_init_reg(lchip, policer_num, divide_factor));

    CTC_ERROR_RETURN(sys_goldengate_opf_init(lchip, OPF_QOS_FLOW_POLICER, 1));
    CTC_ERROR_RETURN(sys_goldengate_opf_init(lchip, OPF_QOS_POLICER_PROFILE, 1));

    /* init flow policer free index pool */
    opf.pool_index = 0;
    opf.pool_type = OPF_QOS_FLOW_POLICER;
    CTC_ERROR_RETURN(sys_goldengate_opf_init_offset(lchip, &opf, flow_policer_base, flow_policer_num));


    opf.pool_index = 0;
    opf.pool_type = OPF_QOS_POLICER_PROFILE;
    CTC_ERROR_RETURN(sys_goldengate_opf_init_offset(lchip, &opf, 1, policer_profile_num - 1));

    /* set chip_capability */
    CTC_ERROR_RETURN(sys_goldengate_global_set_chip_capability(lchip, CTC_GLOBAL_CAPABILITY_TOTAL_POLICER_NUM, policer_num));
    CTC_ERROR_RETURN(sys_goldengate_global_set_chip_capability(lchip, CTC_GLOBAL_CAPABILITY_POLICER_NUM, flow_policer_num));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_qos_policer_node_data(void* node_data, void* user_data)
{
    mem_free(node_data);

    return CTC_E_NONE;
}

int32
sys_goldengate_qos_policer_deinit(uint8 lchip)
{
    LCHIP_CHECK(lchip);
    if (NULL == p_gg_qos_policer_master[lchip])
    {
        return CTC_E_NONE;
    }

    /*free dsmpls data*/
    ctc_spool_free(p_gg_qos_policer_master[lchip]->p_profile_pool);

    /*free policer*/
    ctc_hash_traverse(p_gg_qos_policer_master[lchip]->p_policer_hash, (hash_traversal_fn)_sys_goldengate_qos_policer_node_data, NULL);
    ctc_hash_free(p_gg_qos_policer_master[lchip]->p_policer_hash);

    sys_goldengate_opf_deinit(lchip, OPF_QOS_FLOW_POLICER);
    sys_goldengate_opf_deinit(lchip, OPF_QOS_POLICER_PROFILE);

    sal_mutex_destroy(p_gg_qos_policer_master[lchip]->mutex);

    mem_free(p_gg_qos_policer_master[lchip]);

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_qos_policer_wb_mapping_policer(uint8 lchip, sys_wb_qos_policer_t *p_wb_policer, sys_qos_policer_t *p_policer, uint8 sync)
{
    sys_goldengate_opf_t opf;
    int32 ret = CTC_E_NONE;
    uint32 cmd = 0;
    uint32 cmd1 = 0;
    uint32 stats_ptr = 0;
    uint32 profile_id[4] = { 0 };
    uint8 index = 0;
    sys_qos_policer_profile_t* p_sys_profile_get = NULL;
    sys_qos_policer_profile_t* p_sys_profile_new = NULL;
    DsPolicerProfile_m profile;

    if (sync)
    {
        p_wb_policer->type = p_policer->type;
        p_wb_policer->dir = p_policer->dir;
        p_wb_policer->id = p_policer->id;
        p_wb_policer->cos_bitmap = p_policer->cos_bitmap;
        p_wb_policer->triple_play = p_policer->triple_play;
        p_wb_policer->entry_size = p_policer->entry_size;
        p_wb_policer->policer_ptr = p_policer->policer_ptr;
    }
    else
    {
        p_policer->type = p_wb_policer->type;
        p_policer->dir = p_wb_policer->dir;
        p_policer->id = p_wb_policer->id;
        p_policer->cos_bitmap = p_wb_policer->cos_bitmap;
        p_policer->triple_play = p_wb_policer->triple_play;
        p_policer->entry_size = p_wb_policer->entry_size;
        p_policer->policer_ptr = p_wb_policer->policer_ptr;

        cmd = DRV_IOR(DsPolicerControl_t, DsPolicerControl_statsPtr_f);
        cmd1 = DRV_IOR(DsPolicerControl_t, DsPolicerControl_profile_f);

        if (p_policer->cos_bitmap)
        {
            for (index = 0; index < p_gg_qos_policer_master[lchip]->max_cos_level; index++)
            {
                if (CTC_IS_BIT_SET(p_policer->cos_bitmap, index))
                {
                    CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_policer->policer_ptr+index, cmd, &stats_ptr));
                    p_policer->stats_ptr[index] = stats_ptr;

                    CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_policer->policer_ptr+index, cmd1, &profile_id[index]));
                }
            }
        }
        else
        {
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_policer->policer_ptr, cmd, &stats_ptr));
            p_policer->stats_ptr[0] = stats_ptr;

            CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_policer->policer_ptr, cmd1, &profile_id[0]));
        }

        cmd = DRV_IOR(DsPolicerProfile_t, DRV_ENTRY_FLAG);
        opf.pool_index = 0;
        opf.pool_type = OPF_QOS_POLICER_PROFILE;

        for (index = 0; index < p_gg_qos_policer_master[lchip]->max_cos_level; index++)
        {
            if (0 == profile_id[index])
            {
                continue;
            }

            /*add policer spool*/
            p_sys_profile_new = mem_malloc(MEM_QUEUE_MODULE, sizeof(sys_qos_policer_profile_t));
            if (NULL == p_sys_profile_new)
            {
                ret = CTC_E_NO_MEMORY;
                return ret;
            }

            sal_memset(p_sys_profile_new, 0, sizeof(sys_qos_policer_profile_t));
            sal_memset(&profile, 0, sizeof(DsPolicerProfile_m));

            CTC_ERROR_GOTO(DRV_IOCTL(lchip, profile_id[index], cmd, &profile), ret, ERROR_RETURN1);

            p_sys_profile_new->profile_id = profile_id[index];
            p_sys_profile_new->peak_count_shift = GetDsPolicerProfile(V, excessCountShift_f, &profile);
            p_sys_profile_new->commit_count_shift = GetDsPolicerProfile(V, commitCountShift_f, &profile);
            p_sys_profile_new->peak_threshold = GetDsPolicerProfile(V, excessThreshold_f, &profile);
            p_sys_profile_new->peak_threshold_shift = GetDsPolicerProfile(V, excessThresholdShift_f, &profile);
            p_sys_profile_new->peak_rate = GetDsPolicerProfile(V, excessRate_f, &profile);
            p_sys_profile_new->commit_threshold = GetDsPolicerProfile(V, commitThreshold_f, &profile);
            p_sys_profile_new->commit_threshold_shift = GetDsPolicerProfile(V, commitThresholdShift_f, &profile);
            p_sys_profile_new->commit_rate = GetDsPolicerProfile(V, commitRate_f, &profile);
            p_sys_profile_new->peak_rate_max = GetDsPolicerProfile(V, excessRateMax_f, &profile);
            p_sys_profile_new->commit_rate_max = GetDsPolicerProfile(V, commitRateMax_f, &profile);

            ret = ctc_spool_add(p_gg_qos_policer_master[lchip]->p_profile_pool, p_sys_profile_new, NULL,&p_sys_profile_get);
            if (ret < 0)
            {
                CTC_WB_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "ctc_spool_add error! ret: %d.\n", ret);

                goto ERROR_RETURN1;
            }
            else
            {
                if (ret == CTC_SPOOL_E_OPERATE_MEMORY)
                {
                    /*alloc index from position*/
                    ret = sys_goldengate_opf_alloc_offset_from_position(lchip, &opf, 1, profile_id[index]);
                    if (ret)
                    {
                        ctc_spool_remove(p_gg_qos_policer_master[lchip]->p_profile_pool, p_sys_profile_get, NULL);
                        CTC_WB_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "alloc PolicerProfile table offset from position %u error! ret: %d.\n", profile_id[index], ret);

                        goto ERROR_RETURN1;
                    }
                }
                else /* means get an old. */
                {
                    mem_free(p_sys_profile_new);
                    p_sys_profile_new = NULL;
                }
            }

            p_sys_profile_get->profile_id = profile_id[index];
            p_policer->p_sys_profile[index] = p_sys_profile_get;
        }
    }

    return ret;

ERROR_RETURN1:
    mem_free(p_sys_profile_new);
    return ret;
}


STATIC int32
_sys_goldengate_qos_policer_wb_sync_policer_func(sys_qos_policer_t *p_policer, void *user_data)
{
    uint16 max_entry_cnt = 0;
    sys_wb_qos_policer_t  *p_wb_policer;
    sys_traverse_t *traversal_data = (sys_traverse_t *)user_data;
    ctc_wb_data_t *wb_data = (ctc_wb_data_t *)(traversal_data->data);
    uint8 lchip = (uint8)traversal_data->value1;

    max_entry_cnt = CTC_WB_DATA_BUFFER_LENGTH / (wb_data->key_len + wb_data->data_len);

    p_wb_policer = (sys_wb_qos_policer_t *)wb_data->buffer + wb_data->valid_cnt;
    sal_memset(p_wb_policer, 0, sizeof(sys_wb_qos_policer_t));

    CTC_ERROR_RETURN(_sys_goldengate_qos_policer_wb_mapping_policer(lchip, p_wb_policer, p_policer, 1));
    if (++wb_data->valid_cnt == max_entry_cnt)
    {
        CTC_ERROR_RETURN(ctc_wb_add_entry(wb_data));
        wb_data->valid_cnt = 0;
    }

    return CTC_E_NONE;
}


int32
sys_goldengate_qos_policer_wb_sync(uint8 lchip)
{
    int32 ret = CTC_E_NONE;
    ctc_wb_data_t wb_data;
    sys_traverse_t user_data;
    sys_wb_qos_policer_master_t  *p_wb_policer_master;

    /*syncup policer matser*/
    wb_data.buffer = mem_malloc(MEM_QUEUE_MODULE, CTC_WB_DATA_BUFFER_LENGTH);
    if (NULL == wb_data.buffer)
    {
        ret = CTC_E_NO_MEMORY;
        goto done;
    }
    sal_memset(wb_data.buffer, 0, CTC_WB_DATA_BUFFER_LENGTH);

    CTC_WB_INIT_DATA_T((&wb_data),sys_wb_qos_policer_master_t, CTC_FEATURE_QOS, SYS_WB_APPID_QOS_SUBID_POLICER_MASTER);

    p_wb_policer_master = (sys_wb_qos_policer_master_t *)wb_data.buffer;

    p_wb_policer_master->lchip = lchip;
    p_wb_policer_master->egs_port_policer_base = p_gg_qos_policer_master[lchip]->port_policer_base[CTC_EGRESS];
    p_wb_policer_master->update_gran_base = p_gg_qos_policer_master[lchip]->update_gran_base;
    p_wb_policer_master->policer_count[SYS_POLICER_CNT_PORT_POLICER] = p_gg_qos_policer_master[lchip]->policer_count[SYS_POLICER_CNT_PORT_POLICER];
    p_wb_policer_master->policer_count[SYS_POLICER_CNT_FLOW_POLICER] = p_gg_qos_policer_master[lchip]->policer_count[SYS_POLICER_CNT_FLOW_POLICER];
    p_wb_policer_master->max_cos_level = p_gg_qos_policer_master[lchip]->max_cos_level;
    p_wb_policer_master->policer_gran_mode = p_gg_qos_policer_master[lchip]->policer_gran_mode;

    wb_data.valid_cnt = 1;
    CTC_ERROR_GOTO(ctc_wb_add_entry(&wb_data), ret, done);


    /*syncup policer*/
    CTC_WB_INIT_DATA_T((&wb_data),sys_wb_qos_policer_t, CTC_FEATURE_QOS, SYS_WB_APPID_QOS_SUBID_POLICER);
    user_data.data = &wb_data;
    user_data.value1 = lchip;

    CTC_ERROR_GOTO(ctc_hash_traverse(p_gg_qos_policer_master[lchip]->p_policer_hash, (hash_traversal_fn) _sys_goldengate_qos_policer_wb_sync_policer_func, (void *)&user_data), ret, done);
    if (wb_data.valid_cnt > 0)
    {
        CTC_ERROR_GOTO(ctc_wb_add_entry(&wb_data), ret, done);
        wb_data.valid_cnt = 0;
    }

done:
    if (wb_data.buffer)
    {
        mem_free(wb_data.buffer);
    }

    return ret;
}

int32
sys_goldengate_qos_policer_wb_restore(uint8 lchip)
{
    uint16 entry_cnt = 0;
    int32 ret = CTC_E_NONE;
    ctc_wb_query_t wb_query;
    sys_wb_qos_policer_master_t  *p_wb_policer_master;
    sys_qos_policer_t *p_policer;
    sys_wb_qos_policer_t  *p_wb_policer;


    wb_query.buffer = mem_malloc(MEM_QUEUE_MODULE,  CTC_WB_DATA_BUFFER_LENGTH);
    if (NULL == wb_query.buffer)
    {
        ret = CTC_E_NO_MEMORY;
        goto done;
    }
    sal_memset(wb_query.buffer, 0, CTC_WB_DATA_BUFFER_LENGTH);

    CTC_WB_INIT_QUERY_T((&wb_query),sys_wb_qos_policer_master_t, CTC_FEATURE_QOS, SYS_WB_APPID_QOS_SUBID_POLICER_MASTER);

    CTC_ERROR_GOTO(ctc_wb_query_entry(&wb_query), ret, done);

    /*restore  qos_policer_master*/
    if (wb_query.valid_cnt != 1 || wb_query.is_end != 1)
    {
        CTC_WB_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "query qos_policer master error! valid_cnt: %d, is_end: %d.\n", wb_query.valid_cnt, wb_query.is_end);
        goto done;
    }

    p_wb_policer_master = (sys_wb_qos_policer_master_t  *)wb_query.buffer;

    p_gg_qos_policer_master[lchip]->port_policer_base[CTC_EGRESS] = p_wb_policer_master->egs_port_policer_base;
    p_gg_qos_policer_master[lchip]->update_gran_base = p_wb_policer_master->update_gran_base;
    p_gg_qos_policer_master[lchip]->policer_count[SYS_POLICER_CNT_PORT_POLICER] = p_wb_policer_master->policer_count[SYS_POLICER_CNT_PORT_POLICER];
    p_gg_qos_policer_master[lchip]->policer_count[SYS_POLICER_CNT_FLOW_POLICER] = p_wb_policer_master->policer_count[SYS_POLICER_CNT_FLOW_POLICER];
    p_gg_qos_policer_master[lchip]->max_cos_level = p_wb_policer_master->max_cos_level;
    p_gg_qos_policer_master[lchip]->policer_gran_mode = p_wb_policer_master->policer_gran_mode;


    /*restore  policer*/
    CTC_WB_INIT_QUERY_T((&wb_query),sys_wb_qos_policer_t, CTC_FEATURE_QOS, SYS_WB_APPID_QOS_SUBID_POLICER);

    CTC_WB_QUERY_ENTRY_BEGIN((&wb_query));
        p_wb_policer = (sys_wb_qos_policer_t *)wb_query.buffer + entry_cnt++;

        p_policer = mem_malloc(MEM_QUEUE_MODULE,  sizeof(sys_qos_policer_t));
        if (NULL == p_policer)
        {
            ret = CTC_E_NO_MEMORY;

            goto done;
        }
        sal_memset(p_policer, 0, sizeof(sys_qos_policer_t));

        ret = _sys_goldengate_qos_policer_wb_mapping_policer(lchip, p_wb_policer, p_policer, 0);
        if (ret)
        {
            continue;
        }

        /*add to soft table*/
        ctc_hash_insert(p_gg_qos_policer_master[lchip]->p_policer_hash, p_policer);

    CTC_WB_QUERY_ENTRY_END((&wb_query));

done:

    if (wb_query.buffer)
    {
        mem_free(wb_query.buffer);
    }


    return ret;
}




/**
 @file sys_greatbelt_queue_shape.c

 @date 2010-01-13

 @version v2.0

*/

/****************************************************************************
 *
 * Header Files
 *
 ****************************************************************************/

#include "ctc_const.h"
#include "ctc_error.h"
#include "ctc_hash.h"
#include "ctc_vector.h"

#include "ctc_spool.h"
#include "sys_greatbelt_chip.h"
#include "sys_greatbelt_opf.h"
#include "sys_greatbelt_queue_enq.h"
#include "sys_greatbelt_queue_shape.h"

#include "greatbelt/include/drv_io.h"
#include "greatbelt/include/drv_lib.h"
/****************************************************************************
 *
 * Global and Declaration
 *
 ****************************************************************************/

#define SYS_SPECIAL_SHAPE_BURST             0xFFFFFFFF
#define SHAPE_UPDATE_UNIT                   4
#define SYS_RESERVED_SHAPE_PROFILE_ID       0
#define SYS_RESERVED_GROUP_SHAPE_PROFILE_ID 0
#define SYS_FULL_SHAPE_TOKENS               0xFFFFFF
#define SYS_MIN_SHAPE_BURST                 16000
#define SYS_MAX_SHAPE_BURST                 4194300
#define SYS_MAX_QUEUE_SHAPE_PROFILE_NUM      64
#define SYS_QUEUE_SHAPE_HASH_BUCKET_SIZE     16
#define SYS_QUEUE_INVALID_GROUP             0x1FF
#define SYS_MAX_GROUP_SHAPE_PROFILE_NUM      32

#define SYS_MAX_TOKEN_RATE          0x3FFFFF
#define SYS_MAX_TOKEN_RATE_FRAC     0xFF
#define SYS_MAX_TOKEN_THRD          0xFF
#define SYS_MAX_TOKEN_THRD_SHIFT    0xF

#define SYS_QUEUE_SHAPE_PROFILE_BLOCK_SIZE 8
#define SYS_GROUP_SHAPE_PROFILE_BLOCK_SIZE 8


#define SYS_MAX_SHAPE_RATE      10000000 /*bps*/

#define SYS_SHP_TOKE_THRD_DEFAULT  16

#define MAX_QUEUE_WEIGHT_BASE 1024
#define MAX_SHAPE_WEIGHT_BASE 256
#define SYS_SHAPE_TOKEN_THRD_SHIFTS_WIDTH 4
#define SYS_SHAPE_MAX_TOKEN_THRD    ((1 << 8) -1)

/* max value is max valid value */
#define    SYS_SHAPE_MAX_VALUE_CHECK(var, max_value) \
    { \
        if (((var) > (max_value)) &&  ((max_value) != CTC_QOS_SHP_TOKE_THRD_DEFAULT)){return CTC_E_INVALID_PARAM; } \
    }


#define    SYS_SHAPE_PPS_EN(port) \
    (p_gb_queue_master[lchip]->shp_pps_en && ((port == SYS_RESERVE_PORT_ID_CPU)||(port == SYS_RESERVE_PORT_ID_DMA)))


extern sys_queue_master_t* p_gb_queue_master[CTC_MAX_LOCAL_CHIP_NUM];
struct sys_qos_shape_info_s
{
    uint8  type;
    uint8  pps_en;

    uint32   pir;
    uint32   pbs;
    bool     round_up;
};
typedef struct sys_qos_shape_info_s sys_qos_shape_info_t;


struct sys_qos_shape_result_s
{
    uint32 token_rate;
    uint32 token_rate_frac;
    uint8  threshold;
    uint8  shift;
};
typedef struct sys_qos_shape_result_s sys_qos_shape_result_t;

/****************************************************************************
 *
 * Function
 *
 ****************************************************************************/

int32
sys_greatbelt_qos_map_token_thrd_user_to_hw(uint8 lchip, uint32  user_bucket_thrd,
                                             uint32 *hw_bucket_thrd,
                                             uint8 shift_bits,
                                             uint32 max_thrd)
{
    /*PBS = DsChanShpProfile.tokenThrd * 2^DsChanShpProfile.tokenThrdShift*/
    /*user_bucket_thrd = min_near_division_value * 2^min_near_shift_value */

    /*tokenThrdShift = ceil(log2[(PBS + 255) / 256]) */
    /* tokenThrd = ceil(PBS / 2^DsChanShpProfile.tokenThrdShift) */


    int8 loop = 0;
    uint32 shift_bits_value = (1 << shift_bits) -1;
    uint32 min_near_value = 1 << shift_bits_value;
    uint32 min_near_division_value = 0;
    uint32 temp_value = 0;
    uint32 mod_value = 0;
    uint32 division_value;
    uint32 shift_value = 0;
    uint8  min_near_shift_value = 0;

    if (user_bucket_thrd == 0)
    {
        *hw_bucket_thrd  = 0;
        return CTC_E_NONE;
    }

    for (loop = shift_bits_value ; loop >= 0 ; loop--)
    {
        shift_value = (1 << loop);
        mod_value = user_bucket_thrd % shift_value;
        division_value = user_bucket_thrd / shift_value;
        if (division_value > max_thrd)
        {
            break;
        }
        if (division_value == 0)
        {
            continue;
        }
        if (mod_value == 0 )
        {
            min_near_value = 0;
            min_near_division_value = division_value;
            min_near_shift_value = loop;
            break;
        }
        temp_value = (division_value + 1) * shift_value - user_bucket_thrd;
        if (temp_value < min_near_value)
        {
            min_near_value = temp_value;
            min_near_division_value = division_value + 1;
            min_near_shift_value = loop;
        }
    }
    if (min_near_value == (1 << shift_bits_value))
    {
        return CTC_E_EXCEED_MAX_SIZE;
    }
    *hw_bucket_thrd = min_near_division_value << shift_bits | min_near_shift_value;

    SYS_QUEUE_DBG_INFO("valid_bucket_thrd:%d, user_bucket_thrd:%d\n",
                       (min_near_division_value << min_near_shift_value), user_bucket_thrd);


    return CTC_E_NONE;

}
int32
sys_greatbelt_qos_map_token_thrd_hw_to_user(uint8 lchip, uint32  *user_bucket_thrd,
                                             uint16 hw_bucket_thrd,
                                             uint8 shift_bits)
{

    uint16 shift_bits_value = (1 << shift_bits) -1;

    uint16 bucket_thrd = hw_bucket_thrd >> shift_bits;
    uint16 bucket_shift  = hw_bucket_thrd & shift_bits_value;

    *user_bucket_thrd  = bucket_thrd << bucket_shift;
    return CTC_E_NONE;
}

/**
 @brief Get policer granularity according to the rate.
*/
STATIC int32
_sys_greatbelt_qos_get_shape_gran_by_rate(uint8 lchip, uint32 rate,
                                          uint8 type,
                                          uint8* exponet,
                                          uint32* granularity)
{
    uint8 i = 0;

    CTC_PTR_VALID_CHECK(granularity);

    for (i = 0; i < SYS_MAX_SHAPE_GRANULARITY_RANGE_NUM; i++)
    {
        if (p_gb_queue_master[lchip]->granularity.range[i].is_valid)
        {
            if ((rate >= ((uint64)p_gb_queue_master[lchip]->granularity.range[i].min_rate * 1000))
                && (rate <= ((uint64)p_gb_queue_master[lchip]->granularity.range[i].max_rate * 1000)))
            {
                *exponet = p_gb_queue_master[lchip]->granularity.exponet[type];
                *granularity = p_gb_queue_master[lchip]->granularity.range[i].granularity;
                return CTC_E_NONE;
            }
        }
    }

    if (i == SYS_MAX_SHAPE_GRANULARITY_RANGE_NUM)
    {
        return CTC_E_UNEXPECT;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_qos_shape_get_exp_by_grain(uint8 lchip, uint32 granularity, uint8* exp)
{
    uint8 index  = 0;

    for (index = 3; index < 16; index++)
    {
        if ((1 << index) == granularity)
        {
            *exp = index - 3;
            return CTC_E_NONE;
        }
    }

    return CTC_E_INVALID_PARAM;
}

/**
 @brief Compute channel shape token rate.
*/
int32
sys_greatbelt_qos_shape_token_rate_compute(uint8 lchip, uint32 rate,
                                            uint8 type,
                                            uint8 pps_en,
                                            uint32* p_token_rate,
                                            uint32* p_token_rate_frac)
{
    uint8 exponet = 0;
    uint8 exp = 0;
    uint8 exp_sum = 0;
    uint32 granularity = 0;
    uint8 token_rate_frac_mod = 0;

    SYS_QUEUE_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_token_rate);
    CTC_PTR_VALID_CHECK(p_token_rate_frac);

    if (pps_en)
    {
        uint8 factor_queue = p_gb_queue_master[lchip]->granularity.factor[SYS_SHAPE_TYPE_QUEUE];
        uint8 factor_port = p_gb_queue_master[lchip]->granularity.factor[SYS_SHAPE_TYPE_PORT];
        uint8 factor = 0;

        if (CTC_IS_BIT_SET(factor_port, 7))
        {
            factor = (factor_port&0x7F)*factor_queue;
        }
        else
        {
            factor = factor_queue/factor_port;
        }

        SYS_QUEUE_DBG_INFO("factor = %d\n", factor);

        if (type == SYS_SHAPE_TYPE_PORT) /*channel shaping*/
        {
            *p_token_rate = rate / 256;
            *p_token_rate_frac = (rate % 256 );
        }
        else
        {
            *p_token_rate = rate / (256 / factor);
            *p_token_rate_frac = (rate % (256 / factor)*factor);
        }

        return CTC_E_NONE;
    }

    CTC_ERROR_RETURN(_sys_greatbelt_qos_get_shape_gran_by_rate(lchip, rate, type, &exponet, &granularity));

    *p_token_rate = rate / granularity;

    CTC_ERROR_RETURN(_sys_greatbelt_qos_shape_get_exp_by_grain(lchip, granularity, &exp));

    if (CTC_IS_BIT_SET(exponet, 7))
    {
        exp_sum = (exp - (exponet&0x7F));
    }
    else
    {
        exp_sum = (exp + (exponet&0x7F));
    }

    if (exp_sum < 8)
    {
        *p_token_rate = (*p_token_rate) / (1 << (8 - exp_sum));

        token_rate_frac_mod = (rate / granularity) % (1 << (8 - exp_sum));
        *p_token_rate_frac = token_rate_frac_mod * (1 << exp_sum);
    }
    else
    {
        *p_token_rate = ((*p_token_rate) * (1 << (exp_sum-8)));
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_qos_shape_token_rate_threshold_compute(uint8 lchip, sys_qos_shape_info_t* p_shape_info
                                            , sys_qos_shape_result_t* p_shape_result)
{
    uint32 burst = 0;
    uint32 max_bucket_thrd = 0;
    uint32 threshold = 0;

    /* compute token rate */
    CTC_ERROR_RETURN(sys_greatbelt_qos_shape_token_rate_compute(lchip, p_shape_info->pir,
                                                                 p_shape_info->type,
                                                                 p_shape_info->pps_en,
                                                                 &p_shape_result->token_rate,
                                                                 &p_shape_result->token_rate_frac));

    /* compute token threshold & shift */
    if (0 == p_shape_info->pbs)
    {
        if (p_shape_info->pps_en)
        {
            burst = 16 * 1000;  /*1pps = 16k bytes*/
        }
        else
        {
            burst = p_shape_result->token_rate + 1;
        }
    }
    else if (CTC_QOS_SHP_TOKE_THRD_DEFAULT == p_shape_info->pbs)
    {
        burst = SYS_SHP_TOKE_THRD_DEFAULT;
    }
    else
    {
        if (p_shape_info->pps_en)
        {
            burst = p_shape_info->pbs * 1000; /* one packet convers to 1000B */
        }
        else
        {
            burst = p_shape_info->pbs * 125; /* 1000/8 */
        }
    }

    /*compute max_bucket_thrd*/
    sys_greatbelt_qos_map_token_thrd_hw_to_user(lchip, &max_bucket_thrd, (0xFF << SYS_SHAPE_TOKEN_THRD_SHIFTS_WIDTH | 0xF), SYS_SHAPE_TOKEN_THRD_SHIFTS_WIDTH);

    /*bucket_thrd can not exceed max value*/
    if (burst > max_bucket_thrd)
    {
        burst = max_bucket_thrd;
    }

    /* compute token threshold & shift */
    CTC_ERROR_RETURN(sys_greatbelt_qos_map_token_thrd_user_to_hw(lchip, burst,
                                                                &threshold,
                                                                SYS_SHAPE_TOKEN_THRD_SHIFTS_WIDTH,
                                                                SYS_SHAPE_MAX_TOKEN_THRD));
    p_shape_result->threshold = threshold >> SYS_SHAPE_TOKEN_THRD_SHIFTS_WIDTH;
    p_shape_result->shift =  threshold & 0xF;

    return CTC_E_NONE;
}
#define chan_shp ""

STATIC int32
_sys_greatbelt_queue_set_channel_shape_enable(uint8 lchip,
                                             uint8 channel,
                                             ctc_qos_shape_port_t* p_shape)
{
    ds_chan_shp_profile_t ds_profile;
    q_mgr_chan_shape_ctl_t q_mgr_chan_shape_ctl;
    uint32 cmd = 0;
    uint32 tmp = 0;
    uint8 pps_en = 0;
    uint8 lport  = 0;

    sys_qos_shape_info_t shape_info;
    sys_qos_shape_result_t shape_result;
    CTC_PTR_VALID_CHECK(p_shape);
    SYS_SHAPE_MAX_VALUE_CHECK(p_shape->pir, SYS_MAX_SHAPE_RATE);

    SYS_QUEUE_DBG_FUNC();
    if(CTC_IS_CPU_PORT(p_shape->gport))
    {
        lport = SYS_RESERVE_PORT_ID_DMA;
        SYS_MAP_GCHIP_TO_LCHIP((SYS_MAP_GPORT_TO_GCHIP(p_shape->gport)), lchip);
    }
    else
    {
        SYS_MAP_GPORT_TO_LPORT(p_shape->gport, lchip, lport);
    }

    pps_en = SYS_SHAPE_PPS_EN(lport);

    sal_memset(&shape_info, 0, sizeof(sys_qos_shape_info_t));
    sal_memset(&shape_result, 0, sizeof(sys_qos_shape_result_t));

    shape_info.pir      = p_shape->pir;
    shape_info.pbs      = p_shape->pbs;
    shape_info.pps_en   = pps_en;
    shape_info.type     = SYS_SHAPE_TYPE_PORT;
    shape_info.round_up = TRUE;

    CTC_ERROR_RETURN(_sys_greatbelt_qos_shape_token_rate_threshold_compute(lchip, &shape_info, &shape_result));

    ds_profile.token_rate_frac  = shape_result.token_rate_frac; /*need configure*/
    ds_profile.token_rate       = shape_result.token_rate;
    ds_profile.token_thrd       = shape_result.threshold;
    ds_profile.token_thrd_shift = shape_result.shift;


    /* write ds channel shape */
    cmd = DRV_IOW(DsChanShpProfile_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel, cmd, &ds_profile));

    cmd = DRV_IOW(DsChanShpToken_t, DsChanShpToken_Token_f);
    tmp = (ds_profile.token_thrd << ds_profile.token_thrd_shift)<<8;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel, cmd, &tmp));

    sal_memset(&q_mgr_chan_shape_ctl, 0, sizeof(q_mgr_chan_shape_ctl_t));
    cmd = DRV_IOR(QMgrChanShapeCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &q_mgr_chan_shape_ctl));

    if (channel >= 32)
    {
        CTC_BIT_SET(q_mgr_chan_shape_ctl.chan_shp_en63_to32, channel - 32);
    }
    else
    {
        CTC_BIT_SET(q_mgr_chan_shape_ctl.chan_shp_en31_to0, channel);
    }

    cmd = DRV_IOW(QMgrChanShapeCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &q_mgr_chan_shape_ctl))

    p_gb_queue_master[lchip]->channel[lport].shp_en = 1;
    p_gb_queue_master[lchip]->channel[lport].pir    = p_shape->pir;
    p_gb_queue_master[lchip]->channel[lport].pbs    = p_shape->pbs;

    SYS_QUEUE_DBG_INFO("channel ds_profile.token_rate = 0x%x\n", ds_profile.token_rate);
    SYS_QUEUE_DBG_INFO("channel ds_profile.token_rate_frac = 0x%x\n", ds_profile.token_rate_frac);
    SYS_QUEUE_DBG_INFO("channel ds_profile.token_thrd = 0x%x\n", ds_profile.token_thrd);
    SYS_QUEUE_DBG_INFO("channel ds_profile.token_thrd_shift = 0x%x\n", ds_profile.token_thrd_shift);

    return CTC_E_NONE;
}

/**
 @brief Unset shaping for the given channel in a chip.
*/
STATIC int32
_sys_greatbelt_queue_set_channel_shape_disable(uint8 lchip,
                                              uint8 channel)
{
    ds_chan_shp_profile_t ds_profile;
    q_mgr_chan_shape_ctl_t q_mgr_chan_shape_ctl;
    uint32 cmd = 0;
    uint8 lport = 0;
    ipe_header_adjust_phy_port_map_t ipe_header_adjust_phyport_map;
    sal_memset(&ipe_header_adjust_phyport_map, 0, sizeof(ipe_header_adjust_phy_port_map_t));

    SYS_QUEUE_DBG_FUNC();

    cmd = DRV_IOR(IpeHeaderAdjustPhyPortMap_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel, cmd, &ipe_header_adjust_phyport_map));
    lport = ipe_header_adjust_phyport_map.local_phy_port;

    sal_memset(&q_mgr_chan_shape_ctl, 0, sizeof(q_mgr_chan_shape_ctl_t));
    cmd = DRV_IOR(QMgrChanShapeCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &q_mgr_chan_shape_ctl));
    if (channel >= 32)
    {
        CTC_BIT_SET(q_mgr_chan_shape_ctl.chan_shp_en63_to32, channel - 32);
    }
    else
    {
        CTC_BIT_SET(q_mgr_chan_shape_ctl.chan_shp_en31_to0, channel);
    }

    cmd = DRV_IOW(QMgrChanShapeCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &q_mgr_chan_shape_ctl))

    sal_memset(&ds_profile, 0, sizeof(ds_chan_shp_profile_t));
    ds_profile.token_rate = SYS_MAX_TOKEN_RATE;
    ds_profile.token_thrd = SYS_MAX_TOKEN_THRD;
    ds_profile.token_thrd_shift = SYS_MAX_TOKEN_THRD_SHIFT;

    cmd = DRV_IOW(DsChanShpProfile_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel, cmd, &ds_profile));

    p_gb_queue_master[lchip]->channel[lport].shp_en = 0;
    p_gb_queue_master[lchip]->channel[lport].pir    = 0;
    p_gb_queue_master[lchip]->channel[lport].pbs    = 0;

    return CTC_E_NONE;
}

int32
sys_greatbelt_queue_set_channel_shape(uint8 lchip, ctc_qos_shape_port_t* p_shape)
{
    uint8 channel = 0;
    uint8 lport = 0;
    uint16 gport = 0;
    uint8 gchip = 0;
    uint8 cpu_port_en = 0;

    if(CTC_IS_CPU_PORT(p_shape->gport))
    {
        CTC_ERROR_RETURN(sys_greatbelt_chip_get_cpu_eth_en(lchip, &cpu_port_en));
        lport = cpu_port_en ? SYS_RESERVE_PORT_ID_CPU : SYS_RESERVE_PORT_ID_DMA;
        SYS_MAP_GCHIP_TO_LCHIP((SYS_MAP_GPORT_TO_GCHIP(p_shape->gport)), lchip);
    }
    else
    {
        SYS_MAP_GPORT_TO_LPORT(p_shape->gport, lchip, lport);
    }

    CTC_ERROR_RETURN(sys_greatbelt_get_gchip_id(lchip, &gchip));
    gport = CTC_MAP_LPORT_TO_GPORT(gchip, lport);
    channel = SYS_GET_CHANNEL_ID(lchip, gport);
    if (0xFF == channel)
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }

    if (p_shape->enable)
    {
        CTC_ERROR_RETURN(
            _sys_greatbelt_queue_set_channel_shape_enable(lchip, channel, p_shape));
    }
    else
    {
        CTC_ERROR_RETURN(
            _sys_greatbelt_queue_set_channel_shape_disable(lchip, channel));
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_queue_get_channel_shape(uint8 lchip, ctc_qos_shape_port_t* p_shape)
{
    uint8 channel = 0;
    uint8 lport = 0;

    SYS_MAP_GPORT_TO_LPORT(p_shape->gport, lchip, lport);

    channel = SYS_GET_CHANNEL_ID(lchip, p_shape->gport);
    if (0xFF == channel)
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }

    p_shape->enable = p_gb_queue_master[lchip]->channel[lport].shp_en;
    p_shape->pir    = p_gb_queue_master[lchip]->channel[lport].pir;
    p_shape->pbs    = p_gb_queue_master[lchip]->channel[lport].pbs;

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_queue_init_channel_shape(uint8 lchip)
{
    uint16 channel_id;
    uint32 cmd;
    uint32 tmp;
    ds_chan_shp_profile_t ds_chan_shp_profile;
    q_mgr_chan_shape_ctl_t q_mgr_chan_shape_ctl;
    uint8 exponet = 0;
    uint32 factor = 0;
    /*init chan shaping token rate to max*/
    sal_memset(&ds_chan_shp_profile, 0, sizeof(ds_chan_shp_profile_t));
    ds_chan_shp_profile.token_rate = SYS_MAX_TOKEN_RATE;
    ds_chan_shp_profile.token_thrd = SYS_MAX_TOKEN_THRD;
    ds_chan_shp_profile.token_thrd_shift = SYS_MAX_TOKEN_THRD_SHIFT;

    for (channel_id = 0; channel_id <  SYS_MAX_CHANNEL_NUM; channel_id++)
    {
        tmp = 0xFFFFF;
        cmd = DRV_IOW(DsChanShpToken_t, DsChanShpToken_Token_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel_id, cmd, &tmp));

        cmd = DRV_IOW(DsChanShpProfile_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel_id, cmd, &ds_chan_shp_profile));
    }

    /*init chan shaping update timer*/
    sal_memset(&q_mgr_chan_shape_ctl, 0, sizeof(q_mgr_chan_shape_ctl_t));
    q_mgr_chan_shape_ctl.chan_shp_min_ptr = p_gb_queue_master[lchip]->granularity.min_shape_ptr[0];
    q_mgr_chan_shape_ctl.chan_shp_max_ptr = p_gb_queue_master[lchip]->granularity.max_shape_ptr[0];
    q_mgr_chan_shape_ctl.chan_shp_max_physical_ptr = 61;
    q_mgr_chan_shape_ctl.chan_shp_upd_max_cnt = 0;
    q_mgr_chan_shape_ctl.chan_shp_en31_to0 = 0;
    q_mgr_chan_shape_ctl.chan_shp_en63_to32 = 0;
    cmd = DRV_IOW(QMgrChanShapeCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &q_mgr_chan_shape_ctl));

    exponet = p_gb_queue_master[lchip]->granularity.exponet[SYS_SHAPE_TYPE_PORT];
    if (IS_BIT_SET(exponet, 7))
    {
        factor = 10 + (exponet&0x3F);
    }
    else
    {
        factor = 10 - (exponet&0x3F);
    }
    cmd = DRV_IOW(QMgrEnqCtl_t, QMgrEnqCtl_CpuPktLenAdjFactor_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &factor));

    return CTC_E_NONE;
}




#define group_shp ""

STATIC uint32
_sys_greatbelt_group_shape_profile_hash_make(sys_group_shp_profile_t* p_prof)
{
    uint8* data = (uint8*)&p_prof->profile;
    uint8   length = sizeof(ds_que_shp_profile_t);

    return ctc_hash_caculate(length, data);
}

/**
 @brief Queue shape hash comparison hook.
*/
STATIC bool
_sys_greatbelt_group_shape_profile_hash_cmp(sys_group_shp_profile_t* p_prof1,
                                            sys_group_shp_profile_t* p_prof2)
{
    SYS_QUEUE_DBG_FUNC();

    if (!p_prof1 || !p_prof2)
    {
        return FALSE;
    }

    if ((p_prof2->profile.bucket0_token_rate != SYS_MAX_TOKEN_RATE) || (p_prof2->profile.bucket0_token_rate_frac != SYS_MAX_TOKEN_RATE_FRAC)
        || (p_prof2->profile.bucket0_token_thrd != SYS_MAX_TOKEN_THRD) || (p_prof2->profile.bucket0_token_thrd_shift != SYS_MAX_TOKEN_THRD_SHIFT))
    {
        if ((p_prof1->profile.bucket0_token_rate != p_prof2->profile.bucket0_token_rate) || (p_prof1->profile.bucket0_token_rate_frac != p_prof2->profile.bucket0_token_rate_frac)
            || (p_prof1->profile.bucket0_token_thrd != p_prof2->profile.bucket0_token_thrd) || (p_prof1->profile.bucket0_token_thrd_shift != p_prof2->profile.bucket0_token_thrd_shift))
        {
            return FALSE;
        }
    }

    if ((p_prof2->profile.bucket1_token_rate != SYS_MAX_TOKEN_RATE) || (p_prof2->profile.bucket1_token_rate_frac != SYS_MAX_TOKEN_RATE_FRAC)
        || (p_prof2->profile.bucket1_token_thrd != SYS_MAX_TOKEN_THRD) || (p_prof2->profile.bucket1_token_thrd_shift != SYS_MAX_TOKEN_THRD_SHIFT))
    {
        if ((p_prof1->profile.bucket1_token_rate != p_prof2->profile.bucket1_token_rate) || (p_prof1->profile.bucket1_token_rate_frac != p_prof2->profile.bucket1_token_rate_frac)
            || (p_prof1->profile.bucket1_token_thrd != p_prof2->profile.bucket1_token_thrd) || (p_prof1->profile.bucket1_token_thrd_shift != p_prof2->profile.bucket1_token_thrd_shift))
        {
            return FALSE;
        }
    }

    if ((p_prof2->profile.bucket2_token_rate != SYS_MAX_TOKEN_RATE) || (p_prof2->profile.bucket2_token_rate_frac != SYS_MAX_TOKEN_RATE_FRAC)
        || (p_prof2->profile.bucket2_token_thrd != SYS_MAX_TOKEN_THRD) || (p_prof2->profile.bucket2_token_thrd_shift != SYS_MAX_TOKEN_THRD_SHIFT))
    {
        if ((p_prof1->profile.bucket2_token_rate != p_prof2->profile.bucket2_token_rate) || (p_prof1->profile.bucket2_token_rate_frac != p_prof2->profile.bucket2_token_rate_frac)
            || (p_prof1->profile.bucket2_token_thrd != p_prof2->profile.bucket2_token_thrd) || (p_prof1->profile.bucket2_token_thrd_shift != p_prof2->profile.bucket2_token_thrd_shift))
        {
            return FALSE;
        }
    }

    if ((p_prof2->profile.bucket3_token_rate != SYS_MAX_TOKEN_RATE) || (p_prof2->profile.bucket3_token_rate_frac != SYS_MAX_TOKEN_RATE_FRAC)
        || (p_prof2->profile.bucket3_token_thrd != SYS_MAX_TOKEN_THRD) || (p_prof2->profile.bucket3_token_thrd_shift != SYS_MAX_TOKEN_THRD_SHIFT))
    {
        if ((p_prof1->profile.bucket3_token_rate != p_prof2->profile.bucket3_token_rate) || (p_prof1->profile.bucket3_token_rate_frac != p_prof2->profile.bucket3_token_rate_frac)
            || (p_prof1->profile.bucket3_token_thrd != p_prof2->profile.bucket3_token_thrd) || (p_prof1->profile.bucket3_token_thrd_shift != p_prof2->profile.bucket3_token_thrd_shift))
        {
            return FALSE;
        }
    }

    if ((p_prof2->profile.bucket4_token_rate != SYS_MAX_TOKEN_RATE) || (p_prof2->profile.bucket4_token_rate_frac != SYS_MAX_TOKEN_RATE_FRAC)
        || (p_prof2->profile.bucket4_token_thrd != SYS_MAX_TOKEN_THRD) || (p_prof2->profile.bucket4_token_thrd_shift != SYS_MAX_TOKEN_THRD_SHIFT))
    {
        if ((p_prof1->profile.bucket4_token_rate != p_prof2->profile.bucket4_token_rate) || (p_prof1->profile.bucket4_token_rate_frac != p_prof2->profile.bucket4_token_rate_frac)
            || (p_prof1->profile.bucket4_token_thrd != p_prof2->profile.bucket4_token_thrd) || (p_prof1->profile.bucket4_token_thrd_shift != p_prof2->profile.bucket4_token_thrd_shift))
        {
            return FALSE;
        }
    }

    if ((p_prof2->profile.bucket5_token_rate != SYS_MAX_TOKEN_RATE) || (p_prof2->profile.bucket5_token_rate_frac != SYS_MAX_TOKEN_RATE_FRAC)
        || (p_prof2->profile.bucket5_token_thrd != SYS_MAX_TOKEN_THRD) || (p_prof2->profile.bucket5_token_thrd_shift != SYS_MAX_TOKEN_THRD_SHIFT))
    {
        if ((p_prof1->profile.bucket5_token_rate != p_prof2->profile.bucket5_token_rate) || (p_prof1->profile.bucket5_token_rate_frac != p_prof2->profile.bucket5_token_rate_frac)
            || (p_prof1->profile.bucket5_token_thrd != p_prof2->profile.bucket5_token_thrd) || (p_prof1->profile.bucket5_token_thrd_shift != p_prof2->profile.bucket5_token_thrd_shift))
        {
            return FALSE;
        }
    }

    if ((p_prof2->profile.bucket6_token_rate != SYS_MAX_TOKEN_RATE) || (p_prof2->profile.bucket6_token_rate_frac != SYS_MAX_TOKEN_RATE_FRAC)
        || (p_prof2->profile.bucket6_token_thrd != SYS_MAX_TOKEN_THRD) || (p_prof2->profile.bucket6_token_thrd_shift != SYS_MAX_TOKEN_THRD_SHIFT))
    {
        if ((p_prof1->profile.bucket6_token_rate != p_prof2->profile.bucket6_token_rate) || (p_prof1->profile.bucket6_token_rate_frac != p_prof2->profile.bucket6_token_rate_frac)
            || (p_prof1->profile.bucket6_token_thrd != p_prof2->profile.bucket6_token_thrd) || (p_prof1->profile.bucket6_token_thrd_shift != p_prof2->profile.bucket6_token_thrd_shift))
        {
            return FALSE;
        }
    }

    if ((p_prof2->profile.bucket7_token_rate != SYS_MAX_TOKEN_RATE) || (p_prof2->profile.bucket7_token_rate_frac != SYS_MAX_TOKEN_RATE_FRAC)
        || (p_prof2->profile.bucket7_token_thrd != SYS_MAX_TOKEN_THRD) || (p_prof2->profile.bucket7_token_thrd_shift != SYS_MAX_TOKEN_THRD_SHIFT))
    {
        if ((p_prof1->profile.bucket7_token_rate != p_prof2->profile.bucket7_token_rate) || (p_prof1->profile.bucket7_token_rate_frac != p_prof2->profile.bucket7_token_rate_frac)
            || (p_prof1->profile.bucket7_token_thrd != p_prof2->profile.bucket7_token_thrd) || (p_prof1->profile.bucket7_token_thrd_shift != p_prof2->profile.bucket7_token_thrd_shift))
        {
            return FALSE;
        }
    }

    if ((p_prof1->profile.bucket8_token_rate != p_prof2->profile.bucket8_token_rate) || (p_prof1->profile.bucket8_token_rate_frac != p_prof2->profile.bucket8_token_rate_frac)
        || (p_prof1->profile.bucket8_token_thrd != p_prof2->profile.bucket8_token_thrd) || (p_prof1->profile.bucket8_token_thrd_shift != p_prof2->profile.bucket8_token_thrd_shift))
    {
        return FALSE;
    }

    if ((p_prof1->profile.bucket9_token_rate != p_prof2->profile.bucket9_token_rate) || (p_prof1->profile.bucket9_token_rate_frac != p_prof2->profile.bucket9_token_rate_frac)
        || (p_prof1->profile.bucket9_token_thrd != p_prof2->profile.bucket9_token_thrd) || (p_prof1->profile.bucket9_token_thrd_shift != p_prof2->profile.bucket9_token_thrd_shift))
    {
        return FALSE;
    }

    return TRUE;
}


STATIC int32
_sys_greatbelt_group_shape_profile_free_offset(uint8 lchip,
                                               uint16 profile_id)
{
    sys_greatbelt_opf_t opf;
    uint32 offset  = 0;

    sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));
    opf.pool_type = OPF_GROUP_SHAPE_PROFILE;
    opf.pool_index = 0;

    offset = profile_id;

    CTC_ERROR_RETURN(sys_greatbelt_opf_free_offset(lchip, &opf, 1, offset));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_group_shape_profile_alloc_offset(uint8 lchip,
                                                uint16* profile_id)
{
    sys_greatbelt_opf_t opf;
    uint32 offset  = 0;

    sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));
    opf.pool_type = OPF_GROUP_SHAPE_PROFILE;
    opf.pool_index = 0;

    CTC_ERROR_RETURN(sys_greatbelt_opf_alloc_offset(lchip, &opf, 1, &offset));
    *profile_id = offset;

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_group_shape_profile_add_to_asic(uint8 lchip,
                                               sys_queue_group_node_t* p_sys_grp,
                                               sys_group_shp_profile_t* p_sys_profile)
{

    uint32 cmd = 0;
    int32  ret = 0;
    uint16 profile_id = 0;

    SYS_QUEUE_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_sys_profile);

    profile_id = p_sys_profile->profile_id;

    cmd = DRV_IOW(DsGrpShpProfile_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, profile_id, cmd, &p_sys_profile->profile);

    SYS_QUEUE_DBG_INFO("add profile_id = 0x%x\n", profile_id);

    return ret;
}

int32
_sys_greatbelt_group_shape_profile_remove_from_asic(uint8 lchip,
                                                                   sys_group_shp_profile_t* p_sys_profile)
{
    /*do nothing*/
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_group_shape_add_to_asic(uint8 lchip,
                                       uint8 is_pir,
                                       uint8 que_offset,
                                       uint8 bucket_sel,
                                       sys_queue_group_node_t* p_sys_group,
                                       sys_group_shp_profile_t* p_sys_profile)
{
    uint32 cmd = 0;
    int32  ret = 0;
    ds_grp_shp_wfq_ctl_t ds_grp_shp_wfq_ctl;
    uint8 shp_bucket;

    SYS_QUEUE_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_sys_profile);


    cmd = DRV_IOR(DsGrpShpWfqCtl_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, p_sys_group->group_id, cmd, &ds_grp_shp_wfq_ctl);

    ds_grp_shp_wfq_ctl.grp_shp_en = 1;
    ds_grp_shp_wfq_ctl.grp_shp_prof_id =  p_sys_profile->profile_id*2;

    shp_bucket = p_sys_group->group_bond_en?SYS_SHAPE_BUCKET_PIR_PASS1:
                                            SYS_SHAPE_BUCKET_PIR_PASS0;

    ds_grp_shp_wfq_ctl.que_offset0_sel_pir0 = shp_bucket;
    ds_grp_shp_wfq_ctl.que_offset1_sel_pir0 = shp_bucket;
    ds_grp_shp_wfq_ctl.que_offset2_sel_pir0 = shp_bucket;
    ds_grp_shp_wfq_ctl.que_offset3_sel_pir0 = shp_bucket;
    ds_grp_shp_wfq_ctl.que_offset4_sel_pir0 = shp_bucket;
    ds_grp_shp_wfq_ctl.que_offset5_sel_pir0 = shp_bucket;
    ds_grp_shp_wfq_ctl.que_offset6_sel_pir0 = shp_bucket;
    ds_grp_shp_wfq_ctl.que_offset7_sel_pir0 = shp_bucket;

    if (!is_pir)
    {
        switch(que_offset)
        {
        case 0:
            ds_grp_shp_wfq_ctl.que_offset0_sel_cir = bucket_sel;
            break;
        case 1:
            ds_grp_shp_wfq_ctl.que_offset1_sel_cir = bucket_sel;
            break;
        case 2:
            ds_grp_shp_wfq_ctl.que_offset2_sel_cir = bucket_sel;
            break;
        case 3:
            ds_grp_shp_wfq_ctl.que_offset3_sel_cir = bucket_sel;
            break;
        case 4:
            ds_grp_shp_wfq_ctl.que_offset4_sel_cir = bucket_sel;
            break;
        case 5:
            ds_grp_shp_wfq_ctl.que_offset5_sel_cir = bucket_sel;
            break;
        case 6:
            ds_grp_shp_wfq_ctl.que_offset6_sel_cir = bucket_sel;
            break;
        case 7:
            ds_grp_shp_wfq_ctl.que_offset7_sel_cir = bucket_sel;
            break;
        default:
            break;
        }
    }

    cmd = DRV_IOW(DsGrpShpWfqCtl_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, p_sys_group->group_id, cmd, &ds_grp_shp_wfq_ctl);

    SYS_QUEUE_DBG_INFO("group_id = %d, profile_id = %d\n", p_sys_group->group_id, p_sys_profile->profile_id);

    return ret;
}

int32
_sys_greatbelt_group_shape_remove_from_asic(uint8 lchip,
                                            uint8 is_pir,
                                            uint8 que_offset,
                                            sys_queue_group_node_t* p_sys_group)
{
    uint32 cmd = 0;
    int32  ret = 0;
    uint8 bucket_sel = 0;
    ds_grp_shp_wfq_ctl_t ds_grp_shp_wfq_ctl;

    SYS_QUEUE_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_sys_group);


    cmd = DRV_IOR(DsGrpShpWfqCtl_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, p_sys_group->group_id, cmd, &ds_grp_shp_wfq_ctl);

    ds_grp_shp_wfq_ctl.grp_shp_en = 1;
    ds_grp_shp_wfq_ctl.grp_shp_prof_id = 0;

    if(!is_pir)
    {
        bucket_sel = p_sys_group->group_bond_en?SYS_SHAPE_BUCKET_CIR_FAIL1:
                                        SYS_SHAPE_BUCKET_CIR_FAIL0;
        switch(que_offset)
        {
            case 0:
                ds_grp_shp_wfq_ctl.que_offset0_sel_cir = bucket_sel;
                break;
            case 1:
                ds_grp_shp_wfq_ctl.que_offset1_sel_cir = bucket_sel;
                break;
            case 2:
                ds_grp_shp_wfq_ctl.que_offset2_sel_cir = bucket_sel;
                break;
            case 3:
                ds_grp_shp_wfq_ctl.que_offset3_sel_cir = bucket_sel;
                break;
            case 4:
                ds_grp_shp_wfq_ctl.que_offset4_sel_cir = bucket_sel;
                break;
            case 5:
                ds_grp_shp_wfq_ctl.que_offset5_sel_cir = bucket_sel;
                break;
            case 6:
                ds_grp_shp_wfq_ctl.que_offset6_sel_cir = bucket_sel;
                break;
            case 7:
                ds_grp_shp_wfq_ctl.que_offset7_sel_cir = bucket_sel;
                break;
            default:
                break;
        }
    }

    cmd = DRV_IOW(DsGrpShpWfqCtl_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, p_sys_group->group_id, cmd, &ds_grp_shp_wfq_ctl);

    SYS_QUEUE_DBG_INFO("group_id = 0x%x\n", p_sys_group->group_id);


    return ret;
}

STATIC int32
_sys_greatbelt_group_shape_profile_remove(uint8 lchip,
                                          sys_group_shp_profile_t* p_sys_profile_old)
{
    sys_queue_shp_profile_t* p_sys_profile_find = NULL;
    ctc_spool_t* p_profile_pool               = NULL;
    int32 ret            = 0;
    uint16 profile_id    = 0;

    SYS_QUEUE_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_sys_profile_old);

    p_profile_pool = p_gb_queue_master[lchip]->p_group_profile_pool;

    p_sys_profile_find = ctc_spool_lookup(p_profile_pool, p_sys_profile_old);
    if (NULL == p_sys_profile_find)
    {
        SYS_QUEUE_DBG_ERROR("p_sys_profile_find no found !!!!!!!!\n");
        return CTC_E_ENTRY_NOT_EXIST;
    }

    ret = ctc_spool_remove(p_profile_pool, p_sys_profile_old, NULL);
    if (ret < 0)
    {
        SYS_QUEUE_DBG_ERROR("ctc_spool_remove fail!!!!!!!!\n");
        return CTC_E_SPOOL_REMOVE_FAILED;
    }

    if (ret == CTC_SPOOL_E_OPERATE_MEMORY)
    {
        profile_id = p_sys_profile_find->profile_id;
        CTC_ERROR_RETURN(_sys_greatbelt_group_shape_profile_remove_from_asic(lchip, p_sys_profile_old));

        /*free ad index*/
        CTC_ERROR_RETURN(_sys_greatbelt_group_shape_profile_free_offset(lchip, profile_id));
        mem_free(p_sys_profile_find);
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_group_shape_profile_add(uint8 lchip,
                                       ds_grp_shp_profile_t *p_profile,
                                       sys_queue_group_node_t* p_sys_group,
                                       sys_group_shp_profile_t** pp_sys_profile_get)
{
    ctc_spool_t* p_profile_pool    = NULL;
    sys_group_shp_profile_t* p_sys_profile_old = NULL;
    sys_group_shp_profile_t* p_sys_profile_new = NULL;
    int32 ret            = 0;
    uint16 profile_id    = 0;
    uint16 old_profile_id = 0;

    SYS_QUEUE_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_sys_group);
    CTC_PTR_VALID_CHECK(pp_sys_profile_get);

    p_sys_profile_new = mem_malloc(MEM_QUEUE_MODULE, sizeof(sys_group_shp_profile_t));
    if (NULL == p_sys_profile_new)
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_sys_profile_new, 0, sizeof(sys_group_shp_profile_t));

    sal_memcpy(&p_sys_profile_new->profile, p_profile, sizeof(ds_grp_shp_profile_t));

    p_sys_profile_old = p_sys_group->p_group_shp_profile;
    p_profile_pool = p_gb_queue_master[lchip]->p_group_profile_pool;

    if (p_sys_profile_old)
    {
        if(TRUE == _sys_greatbelt_group_shape_profile_hash_cmp(p_sys_profile_old, p_sys_profile_new))
        {
            *pp_sys_profile_get = p_sys_profile_old;
            mem_free(p_sys_profile_new);
            return CTC_E_NONE;
        }
        old_profile_id = p_sys_profile_old->profile_id;
        p_sys_profile_new->profile_id = old_profile_id;
        SYS_QUEUE_DBG_INFO("update old profile !!!!!!!!!!!!\n");
        ret = ctc_spool_add(p_profile_pool,
                               p_sys_profile_new,
                               p_sys_profile_old,
                               pp_sys_profile_get);
    }
    else
    {

        SYS_QUEUE_DBG_INFO("add new profile !!!!!!!!!!!!\n");
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
            ret = _sys_greatbelt_group_shape_profile_alloc_offset(lchip, &profile_id);
            if (ret < 0)
            {
                ctc_spool_remove(p_profile_pool, p_sys_profile_new, NULL);
                mem_free(p_sys_profile_new);
                goto ERROR_RETURN;
            }

            (*pp_sys_profile_get)->profile_id = profile_id;
            SYS_QUEUE_DBG_INFO("Need new profile:%d\n", profile_id);

        }
    }

    /*key is found, so there is an old ad need to be deleted.*/
    if (p_sys_profile_old && (*pp_sys_profile_get)->profile_id != old_profile_id)
    {
        CTC_ERROR_RETURN(_sys_greatbelt_group_shape_profile_remove(lchip, p_sys_profile_old));
    }

    p_sys_group->p_group_shp_profile = *pp_sys_profile_get;

    return CTC_E_NONE;

ERROR_RETURN:
    return ret;

}


int32
sys_greatbelt_queue_group_shaping_map_rate(uint8 lchip, uint32 rate,
                                           uint32 burst,
                                           uint8 bucket_sel,
                                           uint8 pps_en,
                                           ds_grp_shp_profile_t* p_profile,
                                           sys_queue_group_node_t* p_sys_group_node)
{
    uint32 token_rate = 0;
    uint32 token_rate_frac = 0;
    uint8 token_threshold = 0;
    uint8 token_thrd_shift = 0;
    sys_qos_shape_info_t shape_info;
    sys_qos_shape_result_t shape_result;

    CTC_PTR_VALID_CHECK(p_profile);
    SYS_QUEUE_DBG_FUNC();

    sal_memset(&shape_info, 0, sizeof(sys_qos_shape_info_t));
    sal_memset(&shape_result, 0, sizeof(sys_qos_shape_result_t));

    shape_info.pir      = rate;
    shape_info.pbs      = burst;
    shape_info.pps_en   = pps_en;
    shape_info.type     = SYS_SHAPE_TYPE_QUEUE;
    shape_info.round_up = TRUE;

    CTC_ERROR_RETURN(_sys_greatbelt_qos_shape_token_rate_threshold_compute(lchip, &shape_info, &shape_result));

    token_rate          = shape_result.token_rate;
    token_rate_frac     = shape_result.token_rate_frac;
    token_threshold     = shape_result.threshold;
    token_thrd_shift    = shape_result.shift;

    if (!CTC_IS_BIT_SET(p_sys_group_node->shp_bitmap, bucket_sel))
    {
        token_rate       = SYS_MAX_TOKEN_RATE;
        token_rate_frac  = SYS_MAX_TOKEN_RATE_FRAC;
        token_threshold  = SYS_MAX_TOKEN_THRD;
        token_thrd_shift = SYS_MAX_TOKEN_THRD_SHIFT;
    }

    switch(bucket_sel)
    {
    case 0:
        p_profile->bucket0_token_rate       = token_rate;
        p_profile->bucket0_token_rate_frac  = token_rate_frac;
        p_profile->bucket0_token_thrd       = token_threshold;
        p_profile->bucket0_token_thrd_shift = token_thrd_shift;
        break;

    case 1:
        p_profile->bucket1_token_rate       = token_rate;
        p_profile->bucket1_token_rate_frac  = token_rate_frac;
        p_profile->bucket1_token_thrd       = token_threshold;
        p_profile->bucket1_token_thrd_shift = token_thrd_shift;
        break;

    case 2:
        p_profile->bucket2_token_rate       = token_rate;
        p_profile->bucket2_token_rate_frac  = token_rate_frac;
        p_profile->bucket2_token_thrd       = token_threshold;
        p_profile->bucket2_token_thrd_shift = token_thrd_shift;
        break;

    case 3:
        p_profile->bucket3_token_rate       = token_rate;
        p_profile->bucket3_token_rate_frac  = token_rate_frac;
        p_profile->bucket3_token_thrd       = token_threshold;
        p_profile->bucket3_token_thrd_shift = token_thrd_shift;
        break;

    case 4:
        p_profile->bucket4_token_rate       = token_rate;
        p_profile->bucket4_token_rate_frac  = token_rate_frac;
        p_profile->bucket4_token_thrd       = token_threshold;
        p_profile->bucket4_token_thrd_shift = token_thrd_shift;
        break;

    case 5:
        p_profile->bucket5_token_rate       = token_rate;
        p_profile->bucket5_token_rate_frac  = token_rate_frac;
        p_profile->bucket5_token_thrd       = token_threshold;
        p_profile->bucket5_token_thrd_shift = token_thrd_shift;
        break;

    case 6:
        p_profile->bucket6_token_rate       = token_rate;
        p_profile->bucket6_token_rate_frac  = token_rate_frac;
        p_profile->bucket6_token_thrd       = token_threshold;
        p_profile->bucket6_token_thrd_shift = token_thrd_shift;
        break;

    case 7:
        p_profile->bucket7_token_rate       = token_rate;
        p_profile->bucket7_token_rate_frac  = token_rate_frac;
        p_profile->bucket7_token_thrd       = token_threshold;
        p_profile->bucket7_token_thrd_shift = token_thrd_shift;
        break;

    case 8:
        p_profile->bucket8_token_rate       = token_rate;
        p_profile->bucket8_token_rate_frac  = token_rate_frac;
        p_profile->bucket8_token_thrd       = token_threshold;
        p_profile->bucket8_token_thrd_shift = token_thrd_shift;
        break;

    case 9:
        p_profile->bucket9_token_rate       = token_rate;
        p_profile->bucket9_token_rate_frac  = token_rate_frac;
        p_profile->bucket9_token_thrd       = token_threshold;
        p_profile->bucket9_token_thrd_shift = token_thrd_shift;
        break;

    default:
        return CTC_E_INTR_INVALID_PARAM;

    }


    SYS_QUEUE_DBG_INFO("shp_bitmap:0x%x\n", p_sys_group_node->shp_bitmap);


    if (p_sys_group_node->group_bond_en)
    {
        if (!CTC_IS_BIT_SET(p_sys_group_node->shp_bitmap, 9))
        {
            p_profile->bucket9_token_rate       = SYS_MAX_TOKEN_RATE;
            p_profile->bucket9_token_rate_frac  = 0;
            p_profile->bucket9_token_thrd       = SYS_MAX_TOKEN_THRD;
            p_profile->bucket9_token_thrd_shift = SYS_MAX_TOKEN_THRD_SHIFT;
        }
    }
    else
    {
        if (!CTC_IS_BIT_SET(p_sys_group_node->shp_bitmap, 8))
        {
            p_profile->bucket8_token_rate       = SYS_MAX_TOKEN_RATE;
            p_profile->bucket8_token_rate_frac  = 0;
            p_profile->bucket8_token_thrd       = SYS_MAX_TOKEN_THRD;
            p_profile->bucket8_token_thrd_shift = SYS_MAX_TOKEN_THRD_SHIFT;
        }
    }

    SYS_QUEUE_DBG_INFO("p_profile->token_rate = 0x%x\n", token_rate);
    SYS_QUEUE_DBG_INFO("p_profile->token_rate_frac = 0x%x\n", token_rate_frac);
    SYS_QUEUE_DBG_INFO("p_profile->token_thrd = 0x%x\n", token_threshold);
    SYS_QUEUE_DBG_INFO("p_profile->token_thrd_shift = 0x%x\n", token_thrd_shift);

    return CTC_E_NONE;
}



int32
sys_greatbelt_queue_update_group_shape(uint8 lchip,
                                       uint16 group_id,
                                       uint32 rate,
                                       uint32 burst,
                                       uint8 queue_offset,
                                       uint8 is_pir,
                                       uint8 enable)
{
    sys_group_shp_profile_t* p_sys_profile_new = NULL;
    int32 ret = 0;
    uint8 token_sel = 0;
    uint8 bucket_sel = 0;
    ds_grp_shp_profile_t shp_profile;
    ds_grp_shp_profile_t* p_profile = NULL;
    sys_queue_group_node_t* p_sys_group_node = NULL;
    uint8 pps_en = 0;

    SYS_QUEUE_DBG_FUNC();

    /*cpu reason not exist cir*/
    if (group_id >= p_gb_queue_master[lchip]->reason_grp_start)
    {
       return CTC_E_NONE;
    }

    p_sys_group_node = ctc_vector_get(p_gb_queue_master[lchip]->group_vec, group_id);
    if (NULL == p_sys_group_node)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    SYS_QUEUE_DBG_INFO("group_id:%d\n", group_id);

    p_profile = &shp_profile;

    if (p_sys_group_node->p_group_shp_profile)
    {
        sal_memcpy(p_profile,
                   &p_sys_group_node->p_group_shp_profile->profile,
                   sizeof(ds_grp_shp_profile_t));
    }
    else
    {
        sal_memset(&shp_profile, 0, sizeof(shp_profile));

    }

    if (is_pir)
    {
        token_sel = p_sys_group_node->group_bond_en?9:8;
        bucket_sel = (p_sys_group_node->group_bond_en || (1 == group_id % 2))?9:8;
    }
    else
    {
        if (p_sys_group_node->group_bond_en)
        {
            token_sel = queue_offset;
            bucket_sel = queue_offset;
        }
        else
        {
            token_sel = queue_offset;
            bucket_sel = queue_offset%4;
        }
    }

    if (enable)
    {
        CTC_BIT_SET(p_sys_group_node->shp_bitmap, token_sel);
    }
    else
    {
        CTC_BIT_UNSET(p_sys_group_node->shp_bitmap, token_sel);
    }

    SYS_QUEUE_DBG_INFO("bucket_sel:%d, token_sel:%d, shp_bitmap:0x%x\n",
                       bucket_sel, token_sel, p_sys_group_node->shp_bitmap);

    if (p_sys_group_node->shp_bitmap)
    {

        p_sys_group_node->shp_en = 1;

        if ((group_id == p_gb_queue_master[lchip]->channel[SYS_RESERVE_PORT_ID_CPU].queue_info.group_id)
            ||(group_id == p_gb_queue_master[lchip]->channel[SYS_RESERVE_PORT_ID_DMA].queue_info.group_id) )
        {
            pps_en = p_gb_queue_master[lchip]->shp_pps_en;
        }
        CTC_ERROR_RETURN(sys_greatbelt_queue_group_shaping_map_rate(lchip, rate,
                                                                    burst,
                                                                    token_sel,
                                                                    pps_en,
                                                                    p_profile,
                                                                    p_sys_group_node));
        /*add prof*/
        CTC_ERROR_RETURN(_sys_greatbelt_group_shape_profile_add(lchip,
                                                                p_profile,
                                                                p_sys_group_node,
                                                                &p_sys_profile_new));
        /*write  prof*/
        CTC_ERROR_RETURN(_sys_greatbelt_group_shape_profile_add_to_asic(lchip,
                                                                        p_sys_group_node,
                                                                        p_sys_profile_new));

        if (!enable && !is_pir)
        {
           bucket_sel = p_sys_group_node->group_bond_en?SYS_SHAPE_BUCKET_CIR_FAIL1:
                                                        SYS_SHAPE_BUCKET_CIR_FAIL0;
        }

        /*write ctl and count*/
        CTC_ERROR_RETURN(_sys_greatbelt_group_shape_add_to_asic(lchip,
                                                                is_pir,
                                                                queue_offset,
                                                                bucket_sel,
                                                                p_sys_group_node,
                                                                p_sys_profile_new));

    }
    else
    {

        if (NULL == p_sys_group_node->p_group_shp_profile)
        {
            SYS_QUEUE_DBG_INFO("p_sys_group_node->p_group_shp_profile ==  NULL!!!\n");
            return CTC_E_NONE;
        }

        CTC_ERROR_RETURN(_sys_greatbelt_group_shape_remove_from_asic(lchip, is_pir, queue_offset, p_sys_group_node));
        CTC_ERROR_RETURN(_sys_greatbelt_group_shape_profile_remove(lchip, p_sys_group_node->p_group_shp_profile));
        p_sys_group_node->p_group_shp_profile = NULL;
        p_sys_group_node->shp_en = 0;
    }

    if (is_pir)
    {
        p_sys_group_node->pbs = burst;
        p_sys_group_node->pir = rate;
    }
    return ret;

}



int32
sys_greatbelt_queue_set_group_shape_enable(uint8 lchip,
                                           uint16 group_id,
                                           ctc_qos_shape_group_t* p_shape)
{
    int32 ret = 0;

    SYS_QUEUE_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_shape);
    SYS_SHAPE_MAX_VALUE_CHECK(p_shape->pir, SYS_MAX_SHAPE_RATE);


    CTC_ERROR_RETURN(sys_greatbelt_queue_update_group_shape(lchip,
                                           group_id,
                                           p_shape->pir,
                                           p_shape->pbs,
                                           0,
                                           1,
                                           p_shape->enable));

    return ret;

}

/**
 @brief Unset shaping for the given queue in a chip.
*/
int32
sys_greatbelt_queue_set_group_shape_disable(uint8 lchip,
                                            uint16 group_id)
{
    int32 ret = 0;
    sys_queue_group_node_t* p_sys_group_node = NULL;
     /*sys_group_shp_profile_t* p_sys_profile_old = NULL;*/

    p_sys_group_node = ctc_vector_get(p_gb_queue_master[lchip]->group_vec, group_id);
    if (NULL == p_sys_group_node)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }
    /*
    p_sys_profile_old = p_sys_group_node->p_group_shp_profile;

    if (NULL == p_sys_profile_old)
    {
        return CTC_E_NONE;
    }
    CTC_ERROR_RETURN(_sys_greatbelt_group_shape_remove_from_asic(lchip, 1, 0, p_sys_group_node));
    p_sys_group_node->p_group_shp_profile = NULL;

    CTC_ERROR_RETURN(_sys_greatbelt_group_shape_profile_remove(lchip, p_sys_profile_old));

    */
    CTC_ERROR_RETURN(sys_greatbelt_queue_update_group_shape(lchip,
                                           group_id,
                                           0,
                                           0,
                                           0,
                                           1,
                                           0));


    p_sys_group_node->shp_en = 0;
    p_sys_group_node->pbs = 0;
    p_sys_group_node->pir = 0;

    return ret;
}

int32
sys_greatbelt_queue_set_group_shape(uint8 lchip, ctc_qos_shape_group_t* p_shape)
{
    uint8 lport = 0;
    uint16 group_id = 0;
    sys_queue_info_t queue_info;
    sys_queue_info_t* p_queue_info = NULL;

    /*get queue_id*/

    SYS_QUEUE_DBG_INFO("Parm: p_shape->service_id = %d\n", p_shape->service_id);
    SYS_QUEUE_DBG_INFO("Parm: p_shape->gport = %d\n", p_shape->gport);
    SYS_QUEUE_DBG_INFO("Parm: p_shape->enable = %d\n", p_shape->enable);
    SYS_QUEUE_DBG_INFO("Parm: p_shape->pir = %d\n", p_shape->pir);
    SYS_QUEUE_DBG_INFO("Parm: p_shape->pbs = %d\n", p_shape->pbs);

    SYS_MAP_GPORT_TO_LPORT(p_shape->gport, lchip, lport);

    if (p_shape->group_type == CTC_QOS_SCHED_GROUP_SERVICE)
    {
        sal_memset(&queue_info, 0, sizeof(sys_queue_info_t));
        queue_info.service_id = p_shape->service_id;
        queue_info.dest_queue = lport;
        queue_info.queue_type = SYS_QUEUE_TYPE_SERVICE;
        p_queue_info = sys_greatbelt_queue_info_lookup(lchip, &queue_info);
    }
    else
    {
        p_queue_info = &p_gb_queue_master[lchip]->channel[lport].queue_info;
    }
    if (NULL == p_queue_info)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    group_id = p_queue_info->group_id;

    CTC_ERROR_RETURN(sys_greatbelt_queue_set_group_shape_enable(lchip,
                                                                group_id,
                                                                p_shape));

    return CTC_E_NONE;
}

int32
sys_greatbelt_queue_get_group_shape(uint8 lchip, ctc_qos_shape_group_t* p_shape)
{
    uint8 lport = 0;
    uint16 group_id = 0;
    sys_queue_info_t queue_info;
    sys_queue_info_t* p_queue_info = NULL;
    sys_queue_group_node_t* p_sys_group_node = NULL;

    SYS_QUEUE_DBG_FUNC();
    /*get queue_id*/

    SYS_QUEUE_DBG_INFO("Parm: p_shape->service_id = %d\n", p_shape->service_id);
    SYS_QUEUE_DBG_INFO("Parm: p_shape->gport = %d\n", p_shape->gport);

    SYS_MAP_GPORT_TO_LPORT(p_shape->gport, lchip, lport);

    if (p_shape->group_type == CTC_QOS_SCHED_GROUP_SERVICE)
    {
        sal_memset(&queue_info, 0, sizeof(sys_queue_info_t));
        queue_info.service_id = p_shape->service_id;
        queue_info.dest_queue = lport;
        queue_info.queue_type = SYS_QUEUE_TYPE_SERVICE;
        p_queue_info = sys_greatbelt_queue_info_lookup(lchip, &queue_info);
    }
    else
    {
        p_queue_info = &p_gb_queue_master[lchip]->channel[lport].queue_info;
    }

    if (NULL == p_queue_info)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    group_id = p_queue_info->group_id;

    p_sys_group_node = ctc_vector_get(p_gb_queue_master[lchip]->group_vec, group_id);
    if (NULL == p_sys_group_node)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }
    p_shape->pbs = p_sys_group_node->pbs;
    p_shape->pir = p_sys_group_node->pir;

    return CTC_E_NONE;

}

STATIC int32
_sys_greatbelt_queue_init_group_shape(uint8 lchip)
{
    uint32 cmd = 0;
    uint16 group_id = 0;
    uint8 shp_bucket_cir = 0;
    uint8 shp_bucket_pir = 0;
    sys_queue_group_node_t* p_sys_group_node = NULL;

    ds_grp_shp_profile_t ds_grp_shp_profile;
    ds_grp_shp_wfq_ctl_t ds_grp_shp_wfq_ctl;
    q_mgr_grp_shape_ctl_t q_mgr_grp_shape_ctl;

    sal_memset(&ds_grp_shp_profile, 0, sizeof(ds_grp_shp_profile_t));
    ds_grp_shp_profile.bucket0_token_rate = SYS_MAX_TOKEN_RATE;
    ds_grp_shp_profile.bucket0_token_thrd = SYS_MAX_TOKEN_THRD;
    ds_grp_shp_profile.bucket0_token_thrd_shift = SYS_MAX_TOKEN_THRD_SHIFT;
    ds_grp_shp_profile.bucket1_token_rate = SYS_MAX_TOKEN_RATE;
    ds_grp_shp_profile.bucket1_token_thrd = SYS_MAX_TOKEN_THRD;
    ds_grp_shp_profile.bucket1_token_thrd_shift = SYS_MAX_TOKEN_THRD_SHIFT;
    ds_grp_shp_profile.bucket2_token_rate = SYS_MAX_TOKEN_RATE;
    ds_grp_shp_profile.bucket2_token_thrd = SYS_MAX_TOKEN_THRD;
    ds_grp_shp_profile.bucket2_token_thrd_shift = SYS_MAX_TOKEN_THRD_SHIFT;
    ds_grp_shp_profile.bucket3_token_rate = SYS_MAX_TOKEN_RATE;
    ds_grp_shp_profile.bucket3_token_thrd = SYS_MAX_TOKEN_THRD;
    ds_grp_shp_profile.bucket3_token_thrd_shift = SYS_MAX_TOKEN_THRD_SHIFT;
    ds_grp_shp_profile.bucket4_token_rate = SYS_MAX_TOKEN_RATE;
    ds_grp_shp_profile.bucket4_token_thrd = SYS_MAX_TOKEN_THRD;
    ds_grp_shp_profile.bucket4_token_thrd_shift = SYS_MAX_TOKEN_THRD_SHIFT;
    ds_grp_shp_profile.bucket5_token_rate = SYS_MAX_TOKEN_RATE;
    ds_grp_shp_profile.bucket5_token_thrd = SYS_MAX_TOKEN_THRD;
    ds_grp_shp_profile.bucket5_token_thrd_shift = SYS_MAX_TOKEN_THRD_SHIFT;
    ds_grp_shp_profile.bucket6_token_rate = SYS_MAX_TOKEN_RATE;
    ds_grp_shp_profile.bucket6_token_thrd = SYS_MAX_TOKEN_THRD;
    ds_grp_shp_profile.bucket6_token_thrd_shift = SYS_MAX_TOKEN_THRD_SHIFT;
    ds_grp_shp_profile.bucket7_token_rate = SYS_MAX_TOKEN_RATE;
    ds_grp_shp_profile.bucket7_token_thrd = SYS_MAX_TOKEN_THRD;
    ds_grp_shp_profile.bucket7_token_thrd_shift = SYS_MAX_TOKEN_THRD_SHIFT;
    ds_grp_shp_profile.bucket8_token_rate = SYS_MAX_TOKEN_RATE;
    ds_grp_shp_profile.bucket8_token_thrd = SYS_MAX_TOKEN_THRD;
    ds_grp_shp_profile.bucket8_token_thrd_shift = SYS_MAX_TOKEN_THRD_SHIFT;
    ds_grp_shp_profile.bucket9_token_rate = SYS_MAX_TOKEN_RATE;
    ds_grp_shp_profile.bucket9_token_thrd = SYS_MAX_TOKEN_THRD;
    ds_grp_shp_profile.bucket9_token_thrd_shift = SYS_MAX_TOKEN_THRD_SHIFT;
    cmd = DRV_IOW(DsGrpShpProfile_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ds_grp_shp_profile));

    sal_memset(&q_mgr_grp_shape_ctl, 0, sizeof(q_mgr_grp_shape_ctl_t));
    q_mgr_grp_shape_ctl.grp_bucket0_debit_vec_set0 = 0;
    q_mgr_grp_shape_ctl.grp_bucket1_debit_vec_set0 = 0;
    q_mgr_grp_shape_ctl.grp_bucket2_debit_vec_set0 = 0;
    q_mgr_grp_shape_ctl.grp_bucket3_debit_vec_set0 = 0;
    q_mgr_grp_shape_ctl.grp_bucket0_debit_vec_set1 = 0xF;
    q_mgr_grp_shape_ctl.grp_bucket1_debit_vec_set1 = 0xF;
    q_mgr_grp_shape_ctl.grp_bucket2_debit_vec_set1 = 0xF;
    q_mgr_grp_shape_ctl.grp_bucket3_debit_vec_set1 = 0xF;
    q_mgr_grp_shape_ctl.grp_bucket0_debit_vec_set2 = 0x1;
    q_mgr_grp_shape_ctl.grp_bucket1_debit_vec_set2 = 0x3;
    q_mgr_grp_shape_ctl.grp_bucket2_debit_vec_set2 = 0x7;
    q_mgr_grp_shape_ctl.grp_bucket3_debit_vec_set2 = 0xF;
    cmd = DRV_IOW(QMgrGrpShapeCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &q_mgr_grp_shape_ctl));



    for (group_id = 0; group_id < SYS_MAX_QUEUE_GROUP_NUM; group_id++)
    {
        cmd = DRV_IOR(DsGrpShpWfqCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, group_id, cmd, &ds_grp_shp_wfq_ctl));

        ds_grp_shp_wfq_ctl.grp_shp_prof_id      = 0;
        ds_grp_shp_wfq_ctl.bucket_debit_prof_id = 0;
        ds_grp_shp_wfq_ctl.grp_shp_en           = 1;
        ds_grp_shp_wfq_ctl.grp_flush_valid      = 0;


        shp_bucket_cir = SYS_SHAPE_BUCKET_CIR_FAIL1;
        shp_bucket_pir = SYS_SHAPE_BUCKET_PIR_PASS1;

        p_sys_group_node = ctc_vector_get(p_gb_queue_master[lchip]->group_vec, group_id);

        if (NULL != p_sys_group_node)
        {
            ds_grp_shp_wfq_ctl.start_que_id = p_sys_group_node->start_queue_id;

            shp_bucket_cir = p_sys_group_node->group_bond_en?SYS_SHAPE_BUCKET_CIR_FAIL1:
            SYS_SHAPE_BUCKET_CIR_FAIL0;

            shp_bucket_pir = p_sys_group_node->group_bond_en?SYS_SHAPE_BUCKET_PIR_PASS1:
            SYS_SHAPE_BUCKET_PIR_PASS0;
        }

        ds_grp_shp_wfq_ctl.que_offset0_sel_cir  = shp_bucket_cir;
        ds_grp_shp_wfq_ctl.que_offset1_sel_cir  = shp_bucket_cir;
        ds_grp_shp_wfq_ctl.que_offset2_sel_cir  = shp_bucket_cir;
        ds_grp_shp_wfq_ctl.que_offset3_sel_cir  = shp_bucket_cir;
        ds_grp_shp_wfq_ctl.que_offset4_sel_cir  = shp_bucket_cir;
        ds_grp_shp_wfq_ctl.que_offset5_sel_cir  = shp_bucket_cir;
        ds_grp_shp_wfq_ctl.que_offset6_sel_cir  = shp_bucket_cir;
        ds_grp_shp_wfq_ctl.que_offset7_sel_cir  = shp_bucket_cir;

        ds_grp_shp_wfq_ctl.que_offset0_sel_pir0 = shp_bucket_pir;
        ds_grp_shp_wfq_ctl.que_offset1_sel_pir0 = shp_bucket_pir;
        ds_grp_shp_wfq_ctl.que_offset2_sel_pir0 = shp_bucket_pir;
        ds_grp_shp_wfq_ctl.que_offset3_sel_pir0 = shp_bucket_pir;
        ds_grp_shp_wfq_ctl.que_offset4_sel_pir0 = shp_bucket_pir;
        ds_grp_shp_wfq_ctl.que_offset5_sel_pir0 = shp_bucket_pir;
        ds_grp_shp_wfq_ctl.que_offset6_sel_pir0 = shp_bucket_pir;
        ds_grp_shp_wfq_ctl.que_offset7_sel_pir0 = shp_bucket_pir;

        cmd = DRV_IOW(DsGrpShpWfqCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, group_id, cmd, &ds_grp_shp_wfq_ctl));
    }

    return CTC_E_NONE;
}


#define queue_shp ""
STATIC uint32
_sys_greatbelt_queue_shape_profile_hash_make(sys_queue_shp_profile_t* p_prof)
{
    uint8* data = (uint8*)p_prof;
    uint8   length = (sizeof(sys_queue_shp_profile_t) - sizeof(uint8));

    return ctc_hash_caculate(length, data + sizeof(uint8));

}

/**
 @brief Queue shape hash comparison hook.
*/
STATIC bool
_sys_greatbelt_queue_shape_profile_hash_cmp(sys_queue_shp_profile_t* p_prof1,
                                            sys_queue_shp_profile_t* p_prof2)
{
    SYS_QUEUE_DBG_FUNC();

    if (!p_prof1 || !p_prof2)
    {
        return FALSE;
    }

    if ((!sal_memcmp(&p_prof1->profile, &p_prof2->profile, sizeof(ds_que_shp_profile_t)))
         && (p_prof1->is_cpu_que_prof == p_prof2->is_cpu_que_prof))
    {
        return TRUE;
    }

    return 0;
}

STATIC int32
_sys_greatbelt_queue_shape_profile_free_offset(uint8 lchip, uint8 is_cpu_queue,
                                               uint16 profile_id)
{
    sys_greatbelt_opf_t opf;
    uint32 offset  = 0;

    sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));
    opf.pool_type = OPF_QUEUE_SHAPE_PROFILE;
    opf.pool_index = 0;
    if (is_cpu_queue)
    {
        opf.pool_index = 1;
    }
    offset = profile_id;

    CTC_ERROR_RETURN(sys_greatbelt_opf_free_offset(lchip, &opf, 1, offset));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_queue_shape_profile_alloc_offset(uint8 lchip, uint8 is_cpu_queue,
                                                uint16* profile_id)
{
    sys_greatbelt_opf_t opf;
    uint32 offset  = 0;

    sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));
    opf.pool_type = OPF_QUEUE_SHAPE_PROFILE;
    opf.pool_index = 0;
    if (is_cpu_queue)
    {
        opf.pool_index = 1;
    }

    CTC_ERROR_RETURN(sys_greatbelt_opf_alloc_offset(lchip, &opf, 1, &offset));
    *profile_id = offset;

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_queue_shape_profile_build_data(uint8 lchip, ctc_qos_shape_queue_t* p_shape,
                                              ds_que_shp_profile_t* p_profile)
{
    uint8 pps_en = 0;
    uint8 lport = 0;
    sys_qos_shape_info_t shape_info;
    sys_qos_shape_result_t  shape_result;

    CTC_PTR_VALID_CHECK(p_shape);
    CTC_PTR_VALID_CHECK(p_profile);

    SYS_QUEUE_DBG_FUNC();

    /*get pps mode*/
    if (CTC_QUEUE_TYPE_EXCP_CPU  == p_shape->queue.queue_type)
    {
        pps_en = p_gb_queue_master[lchip]->shp_pps_en ;
    }
    else if (CTC_QUEUE_TYPE_NETWORK_EGRESS  == p_shape->queue.queue_type)
    {
        SYS_MAP_GPORT_TO_LPORT(p_shape->queue.gport, lchip, lport);
        pps_en = SYS_SHAPE_PPS_EN(lport);
    }
    else
    {
        pps_en = 0;
    }

    sal_memset(&shape_info, 0, sizeof(sys_qos_shape_info_t));
    sal_memset(&shape_result, 0, sizeof(sys_qos_shape_result_t));

    shape_info.pir      = p_shape->pir;
    shape_info.pbs      = p_shape->pbs;
    shape_info.pps_en   = pps_en;
    shape_info.type     = SYS_SHAPE_TYPE_QUEUE;
    shape_info.round_up = TRUE;

    CTC_ERROR_RETURN(_sys_greatbelt_qos_shape_token_rate_threshold_compute(lchip, &shape_info, &shape_result));

    p_profile->token_rate = shape_result.token_rate;
    p_profile->token_rate_frac = shape_result.token_rate_frac;
    p_profile->token_thrd = shape_result.threshold;
    p_profile->token_thrd_shift = shape_result.shift;

    SYS_QUEUE_DBG_INFO("p_profile->token_rate = 0x%x\n", p_profile->token_rate);
    SYS_QUEUE_DBG_INFO("p_profile->token_rate_frac = 0x%x\n", p_profile->token_rate_frac);
    SYS_QUEUE_DBG_INFO("p_profile->token_thrd = 0x%x\n", p_profile->token_thrd);
    SYS_QUEUE_DBG_INFO("p_profile->token_thrd_shift = 0x%x\n", p_profile->token_thrd_shift);

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_queue_shape_profile_add_to_asic(uint8 lchip,
                                               sys_queue_node_t* p_sys_queue,
                                               sys_queue_shp_profile_t* p_sys_profile)
{
    uint32 cmd = 0;
    int32  ret = 0;

    CTC_PTR_VALID_CHECK(p_sys_profile);

    cmd = DRV_IOW(DsQueShpProfile_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, p_sys_profile->profile_id, cmd, &p_sys_profile->profile);

    SYS_QUEUE_DBG_INFO("profile_id = 0x%x\n", p_sys_profile->profile_id);

    return ret;
}

STATIC int32
_sys_greatbelt_queue_shape_profile_remove_from_asic(uint8 lchip,
                                                    uint32 profile_id)
{
    /*do nothing*/
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_queue_shape_add_to_asic(uint8 lchip,
                                       sys_queue_node_t* p_sys_queue,
                                       sys_queue_shp_profile_t* p_sys_profile)
{
    uint32 cmd = 0;
    int32  ret = 0;
    uint32 field_val = 0;

    CTC_PTR_VALID_CHECK(p_sys_profile);

    field_val = p_sys_profile->profile_id;
    cmd = DRV_IOW(DsQueShpWfqCtl_t, DsQueShpWfqCtl_QueShpProfId_f);
    ret = DRV_IOCTL(lchip, p_sys_queue->queue_id, cmd, &field_val);

    cmd = DRV_IOR(DsQueShpCtl_t, DsQueShpCtl_QueShpEnVec_f);
    ret = DRV_IOCTL(lchip, p_sys_queue->group_id, cmd, &field_val);
    CTC_BIT_SET(field_val, p_sys_queue->offset);
    cmd = DRV_IOW(DsQueShpCtl_t, DsQueShpCtl_QueShpEnVec_f);
    ret = DRV_IOCTL(lchip, p_sys_queue->group_id, cmd, &field_val);

    SYS_QUEUE_DBG_INFO("queue_id = 0x%x\n", p_sys_queue->queue_id);

    return ret;
}

STATIC int32
_sys_greatbelt_queue_shape_remove_from_asic(uint8 lchip,
                                            sys_queue_node_t* p_sys_queue)
{
    uint32 cmd = 0;
    int32  ret = 0;
    uint32 field_val = 0;

    cmd = DRV_IOR(DsQueShpCtl_t, DsQueShpCtl_QueShpEnVec_f);
    ret = DRV_IOCTL(lchip, p_sys_queue->group_id, cmd, &field_val);
    CTC_BIT_UNSET(field_val, p_sys_queue->offset);
    cmd = DRV_IOW(DsQueShpCtl_t, DsQueShpCtl_QueShpEnVec_f);
    ret = DRV_IOCTL(lchip, p_sys_queue->group_id, cmd, &field_val);

    field_val = 0;
    cmd = DRV_IOW(DsQueShpWfqCtl_t, DsQueShpWfqCtl_QueShpProfId_f);
    ret = DRV_IOCTL(lchip, p_sys_queue->queue_id, cmd, &field_val);

    return ret;
}

STATIC int32
_sys_greatbelt_queue_shape_profile_remove(uint8 lchip,
                                          sys_queue_shp_profile_t* p_sys_profile_old)
{
    sys_queue_shp_profile_t* p_sys_profile_find = NULL;
    ctc_spool_t* p_profile_pool               = NULL;
    int32 ret            = 0;
    uint16 profile_id    = 0;

    SYS_QUEUE_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_sys_profile_old);

    p_profile_pool = p_gb_queue_master[lchip]->p_queue_profile_pool;

    p_sys_profile_find = ctc_spool_lookup(p_profile_pool, p_sys_profile_old);
    if (NULL == p_sys_profile_find)
    {
        SYS_QUEUE_DBG_ERROR("p_sys_profile_find no found !!!!!!!!\n");
        return CTC_E_ENTRY_NOT_EXIST;
    }

    ret = ctc_spool_remove(p_profile_pool, p_sys_profile_old, NULL);
    if (ret < 0)
    {
        SYS_QUEUE_DBG_ERROR("ctc_spool_remove fail!!!!!!!!\n");
        return CTC_E_SPOOL_REMOVE_FAILED;
    }

    if (ret == CTC_SPOOL_E_OPERATE_MEMORY)
    {
        profile_id = p_sys_profile_find->profile_id;
        /*free ad index*/
        CTC_ERROR_RETURN(_sys_greatbelt_queue_shape_profile_remove_from_asic(lchip, profile_id));
        CTC_ERROR_RETURN(_sys_greatbelt_queue_shape_profile_free_offset(lchip, p_sys_profile_old->is_cpu_que_prof, profile_id));
        mem_free(p_sys_profile_find);
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_queue_shape_profile_add(uint8 lchip,
                                       ctc_qos_shape_queue_t* p_shp_param,
                                       sys_queue_node_t* p_sys_queue,
                                       sys_queue_shp_profile_t** pp_sys_profile_get)
{
    ctc_spool_t* p_profile_pool    = NULL;
    sys_queue_shp_profile_t* p_sys_profile_old = NULL;
    sys_queue_shp_profile_t* p_sys_profile_new = NULL;
    int32 ret            = 0;
    uint16 profile_id    = 0;
    uint16 old_profile_id = 0;

    CTC_PTR_VALID_CHECK(p_shp_param);
    CTC_PTR_VALID_CHECK(p_sys_queue);
    CTC_PTR_VALID_CHECK(pp_sys_profile_get);

    p_sys_profile_new = mem_malloc(MEM_QUEUE_MODULE, sizeof(sys_queue_shp_profile_t));
    if (NULL == p_sys_profile_new)
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_sys_profile_new, 0, sizeof(sys_queue_shp_profile_t));

    ret = _sys_greatbelt_queue_shape_profile_build_data(lchip, p_shp_param,
                                                        &p_sys_profile_new->profile);
    if (CTC_E_NONE != ret)
    {
        mem_free(p_sys_profile_new);
        return ret;
    }

    p_sys_profile_old = p_sys_queue->p_queue_shp_profile;
    p_profile_pool = p_gb_queue_master[lchip]->p_queue_profile_pool;

    /*reserve queue shape profile for cpu queue*/
    if ((p_shp_param->queue.queue_type == CTC_QUEUE_TYPE_EXCP_CPU) && (p_gb_queue_master[lchip]->cpu_que_shp_prof_num))
    {
        p_sys_profile_new->is_cpu_que_prof = 1;
    }

    if (p_sys_profile_old)
    {
        if (TRUE == _sys_greatbelt_queue_shape_profile_hash_cmp(p_sys_profile_old, p_sys_profile_new))
        {
            *pp_sys_profile_get = p_sys_profile_old;
            mem_free(p_sys_profile_new);
            return CTC_E_NONE;
        }
        old_profile_id = p_sys_profile_old->profile_id;
        p_sys_profile_new->profile_id = old_profile_id;
        SYS_QUEUE_DBG_INFO("update old profile !!!!!!!!!!!!\n");
        ret = ctc_spool_add(p_profile_pool,
                               p_sys_profile_new,
                               p_sys_profile_old,
                               pp_sys_profile_get);
    }
    else
    {

        SYS_QUEUE_DBG_INFO("add new profile !!!!!!!!!!!!\n");
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
            ret = _sys_greatbelt_queue_shape_profile_alloc_offset(lchip, p_sys_profile_new->is_cpu_que_prof, &profile_id);
            if (ret < 0)
            {
                ctc_spool_remove(p_profile_pool, p_sys_profile_new, NULL);
                mem_free(p_sys_profile_new);
                goto ERROR_RETURN;
            }

            (*pp_sys_profile_get)->profile_id = profile_id;
            SYS_QUEUE_DBG_INFO("Need new profile:%d\n", profile_id);

        }
    }

    /*key is found, so there is an old ad need to be deleted.*/
    if (p_sys_profile_old && (*pp_sys_profile_get)->profile_id != old_profile_id)
    {
        CTC_ERROR_RETURN(_sys_greatbelt_queue_shape_profile_remove(lchip, p_sys_profile_old));
    }

    p_sys_queue->p_queue_shp_profile = *pp_sys_profile_get;

    return CTC_E_NONE;

ERROR_RETURN:
    return ret;

}

int32
sys_greatbelt_queue_set_queue_shape_enable(uint8 lchip,
                                           uint16 queue_id,
                                           ctc_qos_shape_queue_t* p_shape)
{
    sys_queue_shp_profile_t* p_sys_profile_new = NULL;
    sys_queue_node_t* p_sys_queue_node = NULL;
    int32 ret = 0;

    SYS_QUEUE_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_shape);
    SYS_SHAPE_MAX_VALUE_CHECK(p_shape->pir, SYS_MAX_SHAPE_RATE);

    p_sys_queue_node = ctc_vector_get(p_gb_queue_master[lchip]->queue_vec, queue_id);
    if (NULL == p_sys_queue_node)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    SYS_QUEUE_DBG_INFO("queue_id:%d\n", queue_id);

    /*add policer prof*/
   CTC_ERROR_RETURN(_sys_greatbelt_queue_shape_profile_add(lchip,
                                                 p_shape,
                                                 p_sys_queue_node,
                                                 &p_sys_profile_new));

    /*write policer prof*/
    CTC_ERROR_RETURN(_sys_greatbelt_queue_shape_profile_add_to_asic(lchip,
                                                         p_sys_queue_node,
                                                         p_sys_profile_new));

    /*write policer ctl and count*/
    CTC_ERROR_RETURN(_sys_greatbelt_queue_shape_add_to_asic(lchip,
                                                 p_sys_queue_node,
                                                 p_sys_profile_new));


   CTC_ERROR_RETURN(sys_greatbelt_queue_update_group_shape(lchip,
                                            p_sys_queue_node->group_id,
                                            p_shape->cir,
                                            p_shape->cbs,
                                            p_shape->queue.queue_id,
                                            0,
                                            1));

    p_sys_queue_node->cbs = p_shape->cbs;
    p_sys_queue_node->pbs = p_shape->pbs;
    p_sys_queue_node->cir = p_shape->cir;
    p_sys_queue_node->pir = p_shape->pir;
    p_sys_queue_node->shp_en = 1;

    return ret;

}

/**
 @brief Unset shaping for the given queue in a chip.
*/
int32
sys_greatbelt_queue_set_queue_shape_disable(uint8 lchip,
                                            uint16 queue_id)
{
    int32 ret = 0;
    sys_queue_node_t* p_sys_queue_node = NULL;
    sys_queue_shp_profile_t* p_sys_profile_old = NULL;

    p_sys_queue_node = ctc_vector_get(p_gb_queue_master[lchip]->queue_vec, queue_id);
    if (NULL == p_sys_queue_node)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    p_sys_profile_old = p_sys_queue_node->p_queue_shp_profile;

    if (NULL == p_sys_profile_old)
    {
        return CTC_E_NONE;
    }

    CTC_ERROR_RETURN(_sys_greatbelt_queue_shape_remove_from_asic(lchip, p_sys_queue_node));
    p_sys_queue_node->p_queue_shp_profile = NULL;

    CTC_ERROR_RETURN(_sys_greatbelt_queue_shape_profile_remove(lchip, p_sys_profile_old));

   CTC_ERROR_RETURN(sys_greatbelt_queue_update_group_shape(lchip,
                                            p_sys_queue_node->group_id,
                                            0,
                                            0,
                                            p_sys_queue_node->offset,
                                            0,
                                            0));


    p_sys_queue_node->shp_en = 0;
    p_sys_queue_node->cbs = 0;
    p_sys_queue_node->pbs = 0;
    p_sys_queue_node->cir = 0;
    p_sys_queue_node->pir = 0;

    return ret;
}

int32
sys_greatbelt_queue_set_queue_shape(uint8 lchip, ctc_qos_shape_queue_t* p_shape)
{
    uint16 queue_id = 0;

    /*get queue_id*/

    SYS_QUEUE_DBG_INFO("Parm: p_shape->queue_id = %d\n", p_shape->queue.queue_id);
    SYS_QUEUE_DBG_INFO("Parm: p_shape->queue_type = %d\n", p_shape->queue.queue_type);
    SYS_QUEUE_DBG_INFO("Parm: p_shape->enable = %d\n", p_shape->enable);
    SYS_QUEUE_DBG_INFO("Parm: p_shape->gport = %d\n", p_shape->queue.gport);
    SYS_QUEUE_DBG_INFO("Parm: p_shape->pir = %d\n", p_shape->pir);
    SYS_QUEUE_DBG_INFO("Parm: p_shape->pbs = %d\n", p_shape->pbs);


    CTC_ERROR_RETURN(_sys_greatbelt_queue_get_queue_id(lchip,
                                                       &p_shape->queue,
                                                       &queue_id));
    if (p_shape->enable)
    {
        CTC_ERROR_RETURN(sys_greatbelt_queue_set_queue_shape_enable(lchip,
                                                                    queue_id,
                                                                    p_shape));
    }
    else
    {
        CTC_ERROR_RETURN(sys_greatbelt_queue_set_queue_shape_disable(lchip, queue_id));
    }


    return CTC_E_NONE;
}

int32
sys_greatbelt_queue_get_queue_shape(uint8 lchip, ctc_qos_shape_queue_t* p_shape)
{
    uint16 queue_id = 0;
    sys_queue_node_t* p_sys_queue_node = NULL;

    CTC_ERROR_RETURN(_sys_greatbelt_queue_get_queue_id(lchip,
                                                       &p_shape->queue,
                                                       &queue_id));

    p_sys_queue_node = ctc_vector_get(p_gb_queue_master[lchip]->queue_vec, queue_id);
    if (NULL == p_sys_queue_node)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    p_shape->cbs = p_sys_queue_node->cbs;
    p_shape->cir = p_sys_queue_node->cir;
    p_shape->pbs = p_sys_queue_node->pbs;
    p_shape->pir = p_sys_queue_node->pir;

    return CTC_E_NONE;

}

STATIC int32
_sys_greatbelt_queue_init_queue_shape(uint8 lchip)
{
    ds_que_shp_profile_t ds_profile;
    ds_que_shp_wfq_ctl_t ds_que_shp_wfq_ctl;
    q_mgr_que_shape_ctl_t q_mgr_que_shape_ctl;
    uint16 queue_id;
    uint32 cmd;
    uint32 tmp;

    sal_memset(&ds_profile, 0, sizeof(ds_que_shp_profile_t));

    /* reserved queue shape profile 0 for max*/
    ds_profile.token_rate        = SYS_MAX_TOKEN_RATE;
    ds_profile.token_thrd        = SYS_MAX_TOKEN_THRD;
    ds_profile.token_thrd_shift  = SYS_MAX_TOKEN_THRD_SHIFT;
    cmd = DRV_IOW(DsQueShpProfile_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ds_profile));

    /* reserved queue shape profile 1 for drop*/
    ds_profile.token_rate        = SYS_MAX_TOKEN_RATE;
    ds_profile.token_thrd        = SYS_MAX_TOKEN_THRD;
    ds_profile.token_thrd_shift  = SYS_MAX_TOKEN_THRD_SHIFT;
    cmd = DRV_IOW(DsQueShpProfile_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 1, cmd, &ds_profile));

    /* travarse all queue to the reserved shape profile */
    sal_memset(&ds_que_shp_wfq_ctl, 0, sizeof(ds_que_shp_wfq_ctl_t));
    ds_que_shp_wfq_ctl.cir_weight = SYS_WFQ_DEFAULT_WEIGHT;
    ds_que_shp_wfq_ctl.pir_weight = SYS_WFQ_DEFAULT_WEIGHT;
    ds_que_shp_wfq_ctl.cir_weight_shift = SYS_WFQ_DEFAULT_SHIFT;
    ds_que_shp_wfq_ctl.pir_weight_shift = SYS_WFQ_DEFAULT_SHIFT;
    ds_que_shp_wfq_ctl.que_flush_valid = 0;

    for (queue_id = 0; queue_id < SYS_MAX_QUEUE_NUM; queue_id++)
    {
        /* shape profile id */
        cmd = DRV_IOW(DsQueShpWfqCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, queue_id, cmd, &ds_que_shp_wfq_ctl));

        tmp = 0xFFFFF;
        cmd = DRV_IOW(DsQueShpToken_t, DsQueShpToken_Token_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, queue_id, cmd, &tmp));

    }

    /* init queue shape update timer*/
    sal_memset(&q_mgr_que_shape_ctl, 0, sizeof(q_mgr_que_shape_ctl_t));
    q_mgr_que_shape_ctl.que_shp_min_ptr = p_gb_queue_master[lchip]->granularity.min_shape_ptr[1];
    q_mgr_que_shape_ctl.que_shp_max_ptr = p_gb_queue_master[lchip]->granularity.max_shape_ptr[1]; /*variable*/
    q_mgr_que_shape_ctl.que_shp_max_physical_ptr = 1023;
    q_mgr_que_shape_ctl.que_shp_upd_max_cnt = 1;
    cmd = DRV_IOW(QMgrQueShapeCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &q_mgr_que_shape_ctl));

    tmp = 0;
    cmd = DRV_IOW(QMgrNetPktAdj_t, QMgrNetPktAdj_NetPktAdjVal0_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));
    tmp = 0x10;
    cmd = DRV_IOW(QMgrNetPktAdj_t, QMgrNetPktAdj_NetPktAdjVal1_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));
    tmp = 0x2E;
    cmd = DRV_IOW(QMgrNetPktAdj_t, QMgrNetPktAdj_NetPktAdjVal2_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));

    tmp = 1;
    cmd = DRV_IOW(QMgrEnqCreditConfig_t, QMgrEnqCreditConfig_QueMapCreditConfig_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));

    return CTC_E_NONE;
}


#define queue_shp_en ""

/**
 @brief Globally enable/disable channel shaping function.
*/
int32
sys_greatbelt_qos_set_port_shape_enable(uint8 lchip, uint8 enable)
{
    uint32 cmd;
    uint32 tmp;

    SYS_QUEUE_DBG_FUNC();

    tmp = enable;

    cmd = DRV_IOW(QMgrChanShapeCtl_t, QMgrChanShapeCtl_ChanShpGlbEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));

    cmd = DRV_IOW(QMgrChanShapeCtl_t, QMgrChanShapeCtl_ChanShpUpdEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));

    SYS_QUEUE_DBG_INFO("sys_shape_ctl.channel_shape_enable = %d\n", enable);


    return CTC_E_NONE;
}

/**
 @brief Get channel shape global enable stauts.
*/
int32
sys_greatbelt_qos_get_port_shape_enable(uint8 lchip, uint8* p_enable)
{
    uint32 cmd;
    uint32 tmp;

    SYS_QUEUE_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_enable);

    cmd = DRV_IOR(QMgrChanShapeCtl_t, QMgrChanShapeCtl_ChanShpGlbEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));

    *p_enable = tmp;

    SYS_QUEUE_DBG_INFO("sys_shape_ctl.channel_shape_enable = %d\n", *p_enable);

    return CTC_E_NONE;
}

int32
sys_greatbelt_qos_set_queue_shape_enable(uint8 lchip, uint8 enable)
{
    uint32 tmp;
    uint32 cmd;

    SYS_QUEUE_DBG_FUNC();

    tmp = enable ? 1 : 0;

    cmd = DRV_IOW(QMgrQueShapeCtl_t, QMgrQueShapeCtl_QueShpGlbEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));

    cmd = DRV_IOW(QMgrQueShapeCtl_t, QMgrQueShapeCtl_QueShpUpdateEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));


    SYS_QUEUE_DBG_INFO("sys_shape_ctl.queue_shape_enable = %d\n", enable);

    return CTC_E_NONE;
}

/**
 @brief Get queue shape global enable status.
*/
int32
sys_greatbelt_qos_get_queue_shape_enable(uint8 lchip, uint8* p_enable)
{
    uint32 cmd;
    uint32 tmp;

    SYS_QUEUE_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_enable);

    cmd = DRV_IOR(QMgrQueShapeCtl_t, QMgrQueShapeCtl_QueShpGlbEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));

    *p_enable = tmp;

    SYS_QUEUE_DBG_INFO("sys_shape_ctl.queue_shape_enable = %d\n", *p_enable);

    return CTC_E_NONE;
}

/**
 @brief Globally enable/disable group shaping function.
*/
int32
sys_greatbelt_qos_set_group_shape_enable(uint8 lchip, uint8 enable)
{
    uint32 tmp;
    uint32 cmd;

    SYS_QUEUE_DBG_FUNC();

    tmp = enable ? 1 : 0;

    cmd = DRV_IOW(QMgrGrpShapeCtl_t, QMgrGrpShapeCtl_GrpShpGlbEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));


    SYS_QUEUE_DBG_INFO("sys_shape_ctl.queue_shape_enable = %d\n", enable);

    return CTC_E_NONE;
}

/**
 @brief Get group shape global enable status.
*/
int32
sys_greatbelt_qos_get_group_shape_enable(uint8 lchip, uint8* p_enable)
{
    uint32 cmd;
    uint32 tmp;

    SYS_QUEUE_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_enable);

    cmd = DRV_IOR(QMgrGrpShapeCtl_t, QMgrGrpShapeCtl_GrpShpGlbEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));

    *p_enable = tmp;

    SYS_QUEUE_DBG_INFO("queue_shape_enable = %d\n", *p_enable);

    return CTC_E_NONE;
}

int32
sys_greatbelt_qos_set_shape_ipg_enable(uint8 lchip, uint8 enable)
{
    uint32 cmd = 0;
    uint32 field_val = 0;

    SYS_QUEUE_DBG_FUNC();


    field_val = enable? CTC_DEFAULT_IPG : 0;

    cmd = DRV_IOW(QMgrNetPktAdj_t, QMgrNetPktAdj_NetPktAdjVal0_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));


    return CTC_E_NONE;
}

int32
sys_greatbelt_qos_set_reason_shp_base_pkt_en(uint8 lchip, uint8 enable)
{
    uint32 cmd = 0;
    uint32 field_val = 0;

    SYS_QUEUE_DBG_FUNC();

    field_val = enable? 1 : 0;

    cmd = DRV_IOW(QMgrEnqCtl_t, QMgrEnqCtl_CpuBasedOnPktNum_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    SYS_QOS_QUEUE_LOCK(lchip);
    p_gb_queue_master[lchip]->shp_pps_en = enable;
    SYS_QOS_QUEUE_UNLOCK(lchip);

    return CTC_E_NONE;
}

int32
sys_greatbelt_qos_set_shp_remark_color_en(uint8 lchip, uint8 enable)
{
    uint32 cmd = 0;
    uint32 field_val = 0;
    uint8 priority = 0;
    uint8 in_profile = 0;
    uint8 index = 0;

    SYS_QUEUE_DBG_FUNC();

    field_val = enable? 1 : 0;


    for (priority = 0; priority <= SYS_QOS_CLASS_PRIORITY_MAX; priority++)
    {
        /*In profiel (CIR)*/
        in_profile = 1;
        index = ((priority << 1) | in_profile );
        field_val =
        cmd = DRV_IOW(DsBufRetrvColorMap_t, DsBufRetrvColorMap_Color_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &field_val));

        /*Out profiel (PIR)*/
        in_profile = 0;
        index = ((priority << 1) | in_profile );
        cmd = DRV_IOW(DsBufRetrvColorMap_t, DsBufRetrvColorMap_Color_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &field_val));


    }


    SYS_QOS_QUEUE_LOCK(lchip);
    p_gb_queue_master[lchip]->shp_pps_en = enable;
    SYS_QOS_QUEUE_UNLOCK(lchip);

    return CTC_E_NONE;
}


#define shp_api ""

int32
_sys_greatbelt_qos_set_shape(uint8 lchip, ctc_qos_shape_t* p_shape)
{

    CTC_PTR_VALID_CHECK(p_shape);

    SYS_QOS_QUEUE_LOCK(lchip);
    switch (p_shape->type)
    {
    case CTC_QOS_SHAPE_PORT:
        CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(
            sys_greatbelt_queue_set_channel_shape(lchip, &p_shape->shape.port_shape));
        break;

    case CTC_QOS_SHAPE_QUEUE:
        CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(
            sys_greatbelt_queue_set_queue_shape(lchip, &p_shape->shape.queue_shape));
        break;

    case CTC_QOS_SHAPE_GROUP:
        CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(
            sys_greatbelt_queue_set_group_shape(lchip, &p_shape->shape.group_shape));
        break;

    default:
        SYS_QOS_QUEUE_UNLOCK(lchip);
        return CTC_E_INVALID_PARAM;
    }
    SYS_QOS_QUEUE_UNLOCK(lchip);
    return CTC_E_NONE;
}

int32
_sys_greatbelt_qos_get_shape(uint8 lchip, ctc_qos_shape_t* p_shape)
{
    CTC_PTR_VALID_CHECK(p_shape);

    SYS_QOS_QUEUE_LOCK(lchip);
    switch (p_shape->type)
    {
    case CTC_QOS_SHAPE_PORT:
        CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(
            sys_greatbelt_queue_get_channel_shape(lchip, &p_shape->shape.port_shape));
        break;

    case CTC_QOS_SHAPE_QUEUE:
        CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(
            sys_greatbelt_queue_get_queue_shape(lchip, &p_shape->shape.queue_shape));
        break;

    case CTC_QOS_SHAPE_GROUP:
        CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(
            sys_greatbelt_queue_get_group_shape(lchip, &p_shape->shape.group_shape));
        break;

    default:
        SYS_QOS_QUEUE_UNLOCK(lchip);
        return CTC_E_INVALID_PARAM;
    }
    SYS_QOS_QUEUE_UNLOCK(lchip);
    return CTC_E_NONE;
}

int32
sys_greatbelt_queue_shape_init_timer(uint8 lchip)
{
    uint32 core_frequency = 0;
    uint8 i  = 0;

    /* a table of supported granularity, it can be changed by modifying ts_tick_shift */
    sys_qos_shape_granularity_t granularity[SYS_MAX_SHAPE_SUPPORTED_FREQ_NUM] =
    {
        /*Core(MHz) Interval  minPtr MaxPtr Mints    minRate(Mbps) MaxRate(Mbps) Gran  valid */
        {550,  3,     {0, 0},     {715, 2864},   0, {1, 8}, {0, 3},
         {
             {0,       2,        8,    1, {0}},
             {2,       100,      32,   1, {0}},
             {100,     1000,     64,   1, {0}},
             {1000,    10000,    512,  1, {0}},
         }
        },

        {500,  3,     {0, 0},     {650, 1301},   0, {1, 4}, {0, 2},
         {
             {0,       2,        8,    1, {0}},
             {2,       100,      32,   1, {0}},
             {100,     1000,     64,   1, {0}},
             {1000,    10000,    512,  1, {0}},
         }
        },

        {450,  3,     {0, 0},     {585, 1171},   0, {1, 4}, {0, 2},
         {
             {0,       2,        8,    1, {0}},
             {2,       100,      32,   1, {0}},
             {100,     1000,     64,   1, {0}},
             {1000,    10000,    512,  1, {0}},
         }
        },

        {400,  3,     {0, 0},     {520, 1041},   0, {1, 4}, {0, 2},
         {
             {0,       2,        8,    1, {0}},
             {2,       100,      32,   1, {0}},
             {100,     1000,     64,   1, {0}},
             {1000,    10000,    512,  1, {0}},
         }
        },

        {300,  3,     {0, 0},     {390, 1561},   0, {1, 8}, {0, 3},
         {
             {0,       2,        8,    1, {0}},
             {2,       100,      32,   1, {0}},
             {100,     1000,     64,   1, {0}},
             {1000,    10000,    512,  1, {0}},
         }
        },
    };

    core_frequency = sys_greatbelt_get_core_freq(0);

    for (i = 0; i < SYS_MAX_SHAPE_SUPPORTED_FREQ_NUM; i++)
    {
        if (granularity[i].core_frequency == core_frequency)
        {
            sal_memcpy(&p_gb_queue_master[lchip]->granularity, &granularity[i], sizeof(sys_qos_shape_granularity_t));
            break;
        }
    }

    if(i == SYS_MAX_SHAPE_SUPPORTED_FREQ_NUM)
    {
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

/**
 @brief Queue shaper initialization.
*/
int32
sys_greatbelt_queue_shape_init(uint8 lchip, void* p_glb_parm)
{
    uint8 opf_pool_num = 0;
    sys_greatbelt_opf_t opf;
    ctc_spool_t spool;
    ctc_qos_global_cfg_t * p_glb_cfg = NULL;

    p_glb_cfg = (ctc_qos_global_cfg_t *)p_glb_parm;
    p_glb_cfg->cpu_queue_shape_profile_num = (p_glb_cfg->cpu_queue_shape_profile_num) > (SYS_MAX_QUEUE_SHAPE_PROFILE_NUM - 2)
                                            ? (SYS_MAX_QUEUE_SHAPE_PROFILE_NUM - 2) : (p_glb_cfg->cpu_queue_shape_profile_num);

    /*add pool index for cpu queue profile*/
    opf_pool_num = (p_glb_cfg->cpu_queue_shape_profile_num) ? 2 : 1;
    CTC_ERROR_RETURN(sys_greatbelt_opf_init(lchip, OPF_QUEUE_SHAPE_PROFILE, opf_pool_num));
    CTC_ERROR_RETURN(sys_greatbelt_opf_init(lchip, OPF_GROUP_SHAPE_PROFILE, 1));

    sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));

    /* init queue shape hash table */
    sal_memset(&spool, 0, sizeof(ctc_spool_t));
    spool.lchip = lchip;
    spool.block_num = 1;
    spool.block_size = SYS_QUEUE_SHAPE_PROFILE_BLOCK_SIZE;
    spool.max_count = SYS_MAX_QUEUE_SHAPE_PROFILE_NUM;
    spool.user_data_size = sizeof(sys_queue_shp_profile_t);
    spool.spool_key = (hash_key_fn)_sys_greatbelt_queue_shape_profile_hash_make;
    spool.spool_cmp = (hash_cmp_fn)_sys_greatbelt_queue_shape_profile_hash_cmp;
    p_gb_queue_master[lchip]->p_queue_profile_pool = ctc_spool_create(&spool);

    /* init queue shape offset pool */
    opf.pool_type = OPF_QUEUE_SHAPE_PROFILE;
    opf.pool_index = 0;
    CTC_ERROR_RETURN(sys_greatbelt_opf_init_offset(lchip, &opf, 2 + p_glb_cfg->cpu_queue_shape_profile_num,
                                                    SYS_MAX_QUEUE_SHAPE_PROFILE_NUM - p_glb_cfg->cpu_queue_shape_profile_num - 2));
    /*reserved for cpu queue profile*/
    if (p_glb_cfg->cpu_queue_shape_profile_num)
    {
        p_gb_queue_master[lchip]->cpu_que_shp_prof_num = p_glb_cfg->cpu_queue_shape_profile_num;
        opf.pool_index = 1;
        CTC_ERROR_RETURN(sys_greatbelt_opf_init_offset(lchip, &opf, 2, p_glb_cfg->cpu_queue_shape_profile_num));
    }



    /* init queue shape hash table */
    sal_memset(&spool, 0, sizeof(ctc_spool_t));
    spool.lchip = lchip;
    spool.block_num = 1;
    spool.block_size = SYS_GROUP_SHAPE_PROFILE_BLOCK_SIZE;
    spool.max_count = SYS_MAX_GROUP_SHAPE_PROFILE_NUM - 1;
    spool.user_data_size = sizeof(sys_group_shp_profile_t);
    spool.spool_key = (hash_key_fn)_sys_greatbelt_group_shape_profile_hash_make;
    spool.spool_cmp = (hash_cmp_fn)_sys_greatbelt_group_shape_profile_hash_cmp;
    p_gb_queue_master[lchip]->p_group_profile_pool = ctc_spool_create(&spool);


    /* init group shape offset pool */
    opf.pool_type = OPF_GROUP_SHAPE_PROFILE;
    opf.pool_index = 0;
    CTC_ERROR_RETURN(sys_greatbelt_opf_init_offset(lchip, &opf, 1, SYS_MAX_GROUP_SHAPE_PROFILE_NUM - 1));

    CTC_ERROR_RETURN(sys_greatbelt_queue_shape_init_timer(lchip));
    CTC_ERROR_RETURN(_sys_greatbelt_queue_init_queue_shape(lchip));
    CTC_ERROR_RETURN(_sys_greatbelt_queue_init_group_shape(lchip));
    CTC_ERROR_RETURN(_sys_greatbelt_queue_init_channel_shape(lchip));


    CTC_ERROR_RETURN(sys_greatbelt_qos_set_port_shape_enable(lchip, 1));
    CTC_ERROR_RETURN(sys_greatbelt_qos_set_queue_shape_enable(lchip, 1));
    CTC_ERROR_RETURN(sys_greatbelt_qos_set_group_shape_enable(lchip, 1));

    CTC_ERROR_RETURN(sys_greatbelt_qos_set_reason_shp_base_pkt_en(lchip, TRUE));

    return CTC_E_NONE;
}


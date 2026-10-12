/**
 @file sys_goldengate_queue_shp_shape.c

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
#include "ctc_queue.h"

#include "ctc_spool.h"
#include "sys_goldengate_chip.h"
#include "sys_goldengate_opf.h"
#include "sys_goldengate_queue_enq.h"
#include "sys_goldengate_queue_shape.h"
#include "sys_goldengate_port.h"

#include "goldengate/include/drv_enum.h"
#include "goldengate/include/drv_io.h"

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
#define SYS_QUEUE_SHAPE_HASH_BUCKET_SIZE     64
#define SYS_QUEUE_INVALID_GROUP             0x1FF
#define SYS_MAX_GROUP_SHAPE_PROFILE_NUM      32
#define SYS_MAX_TOKEN_RATE          0x3FFFFF
#define SYS_MAX_TOKEN_RATE_FRAC     0xFF
#define SYS_MAX_TOKEN_THRD          0xFF
#define SYS_MAX_TOKEN_THRD_SHIFT    0xF


#define SYS_SHAPE_BUCKET_PIR 8

#define SYS_SHAPE_BUCKET_CIR_PASS0    4
#define SYS_SHAPE_BUCKET_CIR_PASS1    16
#define SYS_SHAPE_BUCKET_PIR_PASS0    7
#define SYS_SHAPE_BUCKET_PIR_PASS1    15

#define SYS_QUEUE_SHAPE_PROFILE_BLOCK_SIZE 8
#define SYS_GROUP_SHAPE_PROFILE_BLOCK_SIZE 8
#define SYS_SHAPE_TOKEN_RATE_BIT_WIDTH 256
#define SYS_SHAPE_TOKEN_THRD_SHIFTS_WIDTH 4
#define SYS_SHAPE_MAX_TOKEN_THRD    ((1 << 8) -1)

#define SYS_MAX_SHAPE_RATE      100000000 /*100Gbps*/

#define MAX_QUEUE_WEIGHT_BASE 1024
#define MAX_SHAPE_WEIGHT_BASE 256



extern sys_queue_master_t* p_gg_queue_master[CTC_MAX_LOCAL_CHIP_NUM];

/****************************************************************************
 *
 * Function
 *
 ****************************************************************************/

int32
sys_goldengate_qos_map_token_thrd_user_to_hw(uint8 lchip, uint32  user_bucket_thrd,
                                            uint32 *hw_bucket_thrd,
                                            uint8 shift_bits,
                                            uint32 max_thrd)
{
/*PBS = DsChanShpProfile.tokenThrd * 2^DsChanShpProfile.tokenThrdShift*/
 /*user_bucket_thrd = min_near_division_value * 2^min_near_shift_value */

 /*tokenThrdShift = ceil(log2[(PBS + 255) / 256]) */
 /* tokenThrd = ceil(PBS / 2^DsChanShpProfile.tokenThrdShift) */


  int8 loop = 0;
  uint32 shift_bits_value = (1 <<shift_bits) -1;
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


  for(loop = shift_bits_value ; loop >= 0 ;loop--)
  {
     shift_value = (1<<loop);
     mod_value = user_bucket_thrd % shift_value;
     division_value = user_bucket_thrd / shift_value;
     if(division_value > max_thrd)
     {
      	break;
     }
     if(division_value == 0)
     {
     	 continue;
     }
	if(mod_value == 0 )
	{
  	    min_near_value = 0;
  	    min_near_division_value = division_value;
  	    min_near_shift_value = loop;
	   break;
	}
    temp_value = (division_value + 1) * shift_value - user_bucket_thrd;
    if(temp_value < min_near_value)
    {
      	min_near_value = temp_value;
		min_near_division_value = division_value + 1;
		min_near_shift_value = loop;
    }
  }
  if(min_near_value == (1<< shift_bits_value))
  {
    return CTC_E_EXCEED_MAX_SIZE;
  }
  *hw_bucket_thrd = min_near_division_value << shift_bits | min_near_shift_value;

  SYS_QUEUE_DBG_INFO("valid_bucket_thrd:%d, user_bucket_thrd:%d\n",
    (min_near_division_value << min_near_shift_value),user_bucket_thrd);


  return CTC_E_NONE;

}
int32
sys_goldengate_qos_map_token_thrd_hw_to_user(uint8 lchip, uint32  *user_bucket_thrd,
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
 @brief Compute channel shape token rate.
*/
int32
sys_goldengate_qos_get_shp_granularity(uint8 lchip, uint32 rate, /*kb/s*/
                                            uint8 type,
                                            uint32 *token_rate_gran)
{
	uint32 token_rate_Mbps = 0;
	uint8 loop = 0;

    SYS_QUEUE_DBG_FUNC();

    if (type == SYS_SHAPE_TYPE_PORT)
    {
		*token_rate_gran = 1;  /**/
    }
	else
	{
	    *token_rate_gran = 1;
	    token_rate_Mbps = rate /1000;
		for(loop =0 ; loop < SYS_SHP_GRAN_RANAGE_NUM ; loop++)
		{
		    if( token_rate_Mbps < p_gg_queue_master[lchip]->que_shp_gran[loop].max_rate)
		    {
                *token_rate_gran = p_gg_queue_master[lchip]->que_shp_gran[loop].granularity;
                break;
		    }
	    }
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_qos_map_token_rate_user_to_hw(uint8 lchip, uint8 is_pps,
                                             uint32 user_rate, /*kb/s*/
                                             uint32 *hw_rate,
                                             uint16 bit_with,
                                             uint32 gran,
                                             uint32 upd_freq )
{

    uint32 token_rate = 0;
	uint32 mod_rate = 0;
	uint8 token_rate_frac =0;

    if (is_pps)
    {
        token_rate =  user_rate / (upd_freq / 1000);   /* ---> user_rate *1000/upd_freq, one packet convers to 1000B */
        token_rate_frac = user_rate % (upd_freq / 1000);
        *hw_rate = token_rate << 8 | token_rate_frac;
    }
    else
    {

        user_rate  = (user_rate / gran) * gran / 8;
        token_rate = user_rate / (upd_freq / 1000);   /* ---> user_rate *1000/upd_freq */
        mod_rate = user_rate % (upd_freq / 1000);   /*kb*/

        /*mod_rate/upd_freq  = x/256   --> x = mod_rate *1000(bytes) * 256/upd_freq
        -- > mod_rate(kb) * 256 / (upd_freq / 1000) */
        token_rate_frac = (mod_rate *bit_with) / (upd_freq / 1000);
        *hw_rate = token_rate << 8 | token_rate_frac;
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_qos_map_token_rate_hw_to_user(uint8 lchip, uint8 is_pps,
                                            uint32 hw_rate,
                                            uint32 *user_rate,
                                            uint16 bit_with,
                                            uint32 upd_freq)
{

    uint32 token_rate = hw_rate >> 8;
    uint8  token_rate_frac = hw_rate & 0xFF;

    if (is_pps)
    {
        *user_rate = token_rate * (upd_freq / 1000) + (token_rate_frac*(upd_freq / 1000)) / bit_with ;

    }
    else
    {
        /*unit:kb/s*/
        /*(token_rate + token_rate_frac/bit_with ) * upd_freq * 8 /1000  -->  kbps/s */
        *user_rate = token_rate * (upd_freq/1000)*8 + (token_rate_frac*(upd_freq/1000)*8)/bit_with ;
    }

	return 0;
}

int32 _sys_godengate_qos_shape_param_check(uint8 lchip, uint8 is_pps, uint32 hw_rate,
                                                              uint32 rate, uint32* thrd, uint32 upd_freq)
{
    uint32 min_rate = 0;
    uint32 max_rate = 0;
    uint16 max_size = 0;
    ctc_chip_device_info_t device_info;
    max_size = SYS_GOLDENGATE_MAX_FRAMESIZE_DEFAULT_VALUE;
    sal_memset(&device_info, 0, sizeof(ctc_chip_device_info_t));
    CTC_ERROR_RETURN(sys_goldengate_chip_get_device_info(lchip, &device_info));

    /*for queue cnt overturn*/
    if (device_info.version_id <= 1)
    {
        min_rate = is_pps ? 0 : ((max_size*2+1)*8*(upd_freq/1000));
        max_rate = is_pps ? (upd_freq/2) : (SYS_MAX_TOKEN_RATE*8*(upd_freq/1000));

        if (rate < min_rate || rate > max_rate)
        {
            return CTC_E_INVALID_PARAM;
        }

        *thrd = (hw_rate >> 8) + 1;
    }

    return CTC_E_NONE;
}
int sys_godengate_qos_set_shape_gran(uint8 lchip,uint8 mode)
{
    sys_qos_rate_granularity_t shp_gran1[SYS_SHP_GRAN_RANAGE_NUM] = {{ 2,       10} ,
                                                                        {100,      40},
                                                                        {1000,     80},
                                                                        {10000,    500},
                                                                        {40000,    1000},
                                                                        {100000,   2000}};
    sys_qos_rate_granularity_t shp_gran0[SYS_SHP_GRAN_RANAGE_NUM] = {{ 2,        8} ,
                                                                        {100,      32},
                                                                        {1000,     64},
                                                                        {10000,    512},
                                                                        {40000,    1024},
                                                                        {100000,   2048}};
    SYS_QOS_QUEUE_LOCK(lchip);
    if(mode == 2)
    {
        sal_memcpy(p_gg_queue_master[lchip]->que_shp_gran,shp_gran1,SYS_SHP_GRAN_RANAGE_NUM * sizeof(sys_qos_rate_granularity_t));
    }
    else
    {
        sal_memcpy(p_gg_queue_master[lchip]->que_shp_gran,shp_gran0,SYS_SHP_GRAN_RANAGE_NUM * sizeof(sys_qos_rate_granularity_t));
    }
    SYS_QOS_QUEUE_UNLOCK(lchip);

    return CTC_E_NONE;
}
#define chan_shp ""
bool
sys_goldengate_queue_shp_get_channel_pps_enbale(uint8 lchip, uint8 chand_id)
{
    uint32 cmd = 0;
    uint32 wdrr_en = 0;
    uint8  slice_chan = (chand_id > 64)?(chand_id-64):chand_id;

    cmd = DRV_IOR(QMgrDeqSchCtl_t, QMgrDeqSchCtl_schBasedOnPktLen_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &wdrr_en));

    if (p_gg_queue_master[lchip]->shp_pps_en &&
        (slice_chan == SYS_DMA_CHANNEL_ID
        || slice_chan == SYS_DMA_CHANNEL_RX1
        || slice_chan == SYS_DMA_CHANNEL_RX2
        || slice_chan == SYS_DMA_CHANNEL_RX3
        || (p_gg_queue_master[lchip]->have_lcpu_by_eth && chand_id == p_gg_queue_master[lchip]->cpu_eth_chan_id)
        || wdrr_en == 0))
    {
        return TRUE;
    }
    else
    {
        return FALSE;
    }
}

int32
sys_goldengate_queue_shp_set_channel_shape_enable(uint8 lchip, uint8 slice_id,
                                             uint16 channel,
                                             ctc_qos_shape_port_t* p_shape)
{

    DsChanShpProfile_m  chan_shp_profile;
	QMgrChanShapeCtl_m chan_shape_ctl;
    uint32 cmd                     = 0;
    uint32 burst                   = 0;
    uint32 db_rate                 = 0;
    uint32 pq_rate                 = 0;
    uint32 gran                    = 0;
	uint8  is_pps = 0;

    uint32 threshold = 0;
	uint32 table_index = 0;
	uint32 chan_shp_en[2];
    uint32 max_bucket_thrd = 0;


    CTC_PTR_VALID_CHECK(p_shape);
    CTC_MAX_VALUE_CHECK(p_shape->pir, SYS_MAX_SHAPE_RATE);

    SYS_QUEUE_DBG_FUNC();


    sys_goldengate_qos_get_shp_granularity(lchip, p_shape->pir,
                                                        SYS_SHAPE_TYPE_PORT,&gran);

	is_pps = sys_goldengate_queue_shp_get_channel_pps_enbale(lchip, channel);

    /* compute token rate */
    CTC_ERROR_RETURN(sys_goldengate_qos_map_token_rate_user_to_hw(lchip, is_pps, p_shape->pir,
                                                                  &db_rate,
                                                                  SYS_SHAPE_TOKEN_RATE_BIT_WIDTH,
                                                                  gran,
                                                                  p_gg_queue_master[lchip]->chan_shp_update_freq));
    if (0 == p_shape->pbs)
    {
        if (is_pps)
        {
            burst = 16 * 1000;  /*1pps = 16k bytes*/
        }
        else
        {
            burst = (db_rate >> 8 ) + 1;
        }
    }
    else
    {
        if (is_pps)
        {
            burst = p_shape->pbs * 1000; /* one packet convers to 1000B */
        }
        else
        {
            burst = p_shape->pbs * 125; /* 1000/8 */
        }
    }

    /*compute max_bucket_thrd*/
    sys_goldengate_qos_map_token_thrd_hw_to_user(lchip, &max_bucket_thrd, (0xFF << SYS_SHAPE_TOKEN_THRD_SHIFTS_WIDTH | 0xF), SYS_SHAPE_TOKEN_THRD_SHIFTS_WIDTH);

    /*bucket_thrd can not exceed max value*/
    if (burst > max_bucket_thrd)
    {
        burst = max_bucket_thrd;
    }

    /* compute token threshold & shift */
    CTC_ERROR_RETURN(sys_goldengate_qos_map_token_thrd_user_to_hw(lchip, burst,
                                                                &threshold,
                                                                SYS_SHAPE_TOKEN_THRD_SHIFTS_WIDTH,
                                                                SYS_SHAPE_MAX_TOKEN_THRD));

    /* compute token pq rate */
    CTC_ERROR_RETURN(sys_goldengate_qos_map_token_rate_user_to_hw(lchip, is_pps, p_shape->ecn_mark_rate,
                                                                  &pq_rate,
                                                                  SYS_SHAPE_TOKEN_RATE_BIT_WIDTH,
                                                                  gran,
                                                                  p_gg_queue_master[lchip]->chan_shp_update_freq));

    pq_rate = (p_shape->ecn_mark_rate == 0) ? SYS_MAX_TOKEN_RATE : (pq_rate >> 8 );

    SetDsChanShpProfile(V, thrdProtect_f, &chan_shp_profile, 0);
    SetDsChanShpProfile(V, tokenThrdCfg_f, &chan_shp_profile, (threshold >> SYS_SHAPE_TOKEN_THRD_SHIFTS_WIDTH));
    SetDsChanShpProfile(V, tokenThrdShift_f  , &chan_shp_profile, threshold & 0xF);
    SetDsChanShpProfile(V, tokenRateFrac_f, &chan_shp_profile, (db_rate & 0xFF));
    SetDsChanShpProfile(V, tokenRate_f  , &chan_shp_profile, (db_rate >> 8 ));
    SetDsChanShpProfile(V, tokenRatePq_f  , &chan_shp_profile, pq_rate);

	table_index = slice_id *SYS_MAX_CHANNEL_ID + channel;
    cmd = DRV_IOW(DsChanShpProfile_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip,table_index, cmd, &chan_shp_profile));

    SYS_QUEUE_DBG_INFO("Write DsChanShpProfile_t[%d]\n", table_index);
    SYS_QUEUE_DBG_INFO("tokenRate=%d\n", (db_rate >> 8 ));
    SYS_QUEUE_DBG_INFO("tokenRateFrac=%d\n", (db_rate & 0xFF));
    SYS_QUEUE_DBG_INFO("tokenThrdCfg=%d\n", (threshold >> SYS_SHAPE_TOKEN_THRD_SHIFTS_WIDTH));
    SYS_QUEUE_DBG_INFO("tokenThrdShift=%d\n", (threshold & 0xF));
    SYS_QUEUE_DBG_INFO("tokenRatePq=%d\n", pq_rate);

    cmd = DRV_IOR(QMgrChanShapeCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, slice_id, cmd, &chan_shape_ctl));
    GetQMgrChanShapeCtl(A, chanShpEn_f, &chan_shape_ctl,chan_shp_en);

    if (channel < 32)
    {
        CTC_BIT_SET(chan_shp_en[0], channel);
    }
    else
    {
        CTC_BIT_SET(chan_shp_en[1], (channel - 32));
    }
    SetQMgrChanShapeCtl(A, chanShpEn_f, &chan_shape_ctl,chan_shp_en);
	cmd = DRV_IOW(QMgrChanShapeCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, slice_id, cmd, &chan_shape_ctl));

    return CTC_E_NONE;
}

/**
 @brief Unset shaping for the given channel in a chip.
*/
int32
sys_goldengate_queue_shp_set_channel_shape_disable(uint8 lchip, uint8 slice_id,
                                              uint16 channel)
{
    DsChanShpProfile_m  chan_shp_profile;
    QMgrChanShapeCtl_m chan_shape_ctl;
    uint32 cmd = 0;
    uint32 table_index = 0;
    uint32 chan_shp_en[2];

    SYS_QUEUE_DBG_FUNC();

    SetDsChanShpProfile(V, thrdProtect_f, &chan_shp_profile, 0);
    SetDsChanShpProfile(V, tokenRatePq_f  , &chan_shp_profile, SYS_MAX_TOKEN_RATE);
    SetDsChanShpProfile(V, tokenThrdCfg_f, &chan_shp_profile, SYS_MAX_TOKEN_THRD);
    SetDsChanShpProfile(V, tokenThrdShift_f  , &chan_shp_profile, SYS_MAX_TOKEN_THRD_SHIFT);
    SetDsChanShpProfile(V, tokenRateFrac_f, &chan_shp_profile, SYS_MAX_TOKEN_RATE_FRAC);
    SetDsChanShpProfile(V, tokenRate_f  , &chan_shp_profile, SYS_MAX_TOKEN_RATE);

    table_index = slice_id *SYS_MAX_CHANNEL_ID + channel;
    cmd = DRV_IOW(DsChanShpProfile_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, table_index, cmd, &chan_shp_profile));


    cmd = DRV_IOR(QMgrChanShapeCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, slice_id, cmd, &chan_shape_ctl))
    GetQMgrChanShapeCtl(A,chanShpEn_f, &chan_shape_ctl, chan_shp_en);

    if (channel < 32)
    {

        CTC_BIT_UNSET(chan_shp_en[0], channel);
    }
    else
    {
        CTC_BIT_UNSET(chan_shp_en[1], (channel - 32));
    }
    SetQMgrChanShapeCtl(A, chanShpEn_f, &chan_shape_ctl, chan_shp_en);
    cmd = DRV_IOW(QMgrChanShapeCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, slice_id, cmd, &chan_shape_ctl));

    return CTC_E_NONE;
}

int32
sys_goldengate_queue_shp_set_channel_shape(uint8 lchip, ctc_qos_shape_port_t* p_shape)
{
    uint16 lport = 0;
	uint8 channel = 0;
    uint8 slice_id = 0;

    if (CTC_IS_CPU_PORT(p_shape->gport))
    {
        CTC_MAX_VALUE_CHECK(p_shape->sub_dest_id, p_gg_queue_master[lchip]->max_dma_rx_num-1);
        channel = SYS_DMA_CHANNEL_ID + p_shape->sub_dest_id;
        CTC_ERROR_RETURN(sys_goldengate_chip_get_pcie_select(lchip, &slice_id));
    }
    else
    {
        SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(p_shape->gport, lchip, lport);
        sys_goldengate_get_channel_by_port(lchip, p_shape->gport,&channel);

        channel = channel&0x3F;
        slice_id = lport>>8;
    }

    CTC_MAX_VALUE_CHECK(channel, SYS_MAX_CHANNEL_ID - 1);
    if (p_shape->enable)
    {
        CTC_ERROR_RETURN(
            sys_goldengate_queue_shp_set_channel_shape_enable(lchip, slice_id, channel, p_shape));
    }
    else
    {
        CTC_ERROR_RETURN(
            sys_goldengate_queue_shp_set_channel_shape_disable(lchip, slice_id, channel));
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_queue_shp_init_channel_shape(uint8 lchip, uint8 slice_id)
{

    uint16 channel_id              = 0;
    uint32 cmd                     = 0;
    uint32 chan_shp_en[2] = {0};

    DsChanShpProfile_m chan_shp_profile;
  	QMgrChanShapeCtl_m chan_shape_ctl;
    RefDivQMgrDeqShpPulse_m shp_pulse;

   sal_memset(&chan_shape_ctl, 0, sizeof(chan_shape_ctl));
   sal_memset(&chan_shp_profile, 0, sizeof(chan_shp_profile));
   sal_memset(&shp_pulse, 0, sizeof(shp_pulse));


    for (channel_id = 0; channel_id < SYS_MAX_CHANNEL_ID; channel_id++)
    {
        SetDsChanShpProfile(V, thrdProtect_f, &chan_shp_profile, 0);
        SetDsChanShpProfile(V, tokenRatePq_f  , &chan_shp_profile, 0);
        SetDsChanShpProfile(V, tokenThrdCfg_f, &chan_shp_profile, SYS_MAX_TOKEN_THRD);
        SetDsChanShpProfile(V, tokenThrdShift_f  , &chan_shp_profile, SYS_MAX_TOKEN_THRD_SHIFT);
        SetDsChanShpProfile(V, tokenRateFrac_f, &chan_shp_profile, SYS_MAX_TOKEN_RATE_FRAC);
        SetDsChanShpProfile(V, tokenRate_f  , &chan_shp_profile, SYS_MAX_TOKEN_RATE);
        cmd = DRV_IOW(DsChanShpProfile_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip,slice_id *SYS_MAX_CHANNEL_ID + channel_id, cmd, &chan_shp_profile));
    }

    SetQMgrChanShapeCtl(A, chanShpEn_f, &chan_shape_ctl, chan_shp_en);
    SetQMgrChanShapeCtl(V, chanShpGlbEn_f, &chan_shape_ctl, 1);
    SetQMgrChanShapeCtl(V, chanShpMinPtr_f, &chan_shape_ctl, 0);
	SetQMgrChanShapeCtl(V, chanShpMaxPtr_f, &chan_shape_ctl, 63);
    SetQMgrChanShapeCtl(V, chanShpUpdEn_f, &chan_shape_ctl, 1);
    cmd = DRV_IOW(QMgrChanShapeCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, slice_id, cmd, &chan_shape_ctl));

    /*Freq: 4.096MHz, Peroid:0.244140625 us
    19.07348625 => Cfg:19.07348625
    305 0.17578 => Cfg:0x1302d
    RefCoreFreq: 156.25, DivFactor: 76.293945 = > Cfg: 0x4B4B*/
    SetRefDivQMgrDeqShpPulse(V, cfgRefDivQMgrDeqShpPulse_f, &shp_pulse, 0x2615a);
    SetRefDivQMgrDeqShpPulse(V, cfgResetDivQMgrDeqShpPulse_f, &shp_pulse, 0);
    cmd = DRV_IOW(RefDivQMgrDeqShpPulse_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &shp_pulse));

    return CTC_E_NONE;
}


#define group_shp ""

STATIC uint32
_sys_goldengate_queue_shp_hash_make_group_shape_profile(sys_group_shp_profile_t* p_prof)
{
    uint32 val = 0;

#if 0 /*not use hash, use list*/

    uint8* data =  (uint8*)p_prof;
    uint8   length = sizeof(sys_group_shp_profile_t) - sizeof(uint32);
    val = ctc_hash_caculate(length, (data + sizeof(uint32)));
#endif

    return val;
}

/**
 @brief Queue shape hash comparison hook.
*/
STATIC bool
_sys_goldengate_queue_shp_hash_cmp_group_shape_profile(sys_group_shp_profile_t* p_old_prof,
                                            sys_group_shp_profile_t* p_new_prof)
{
    uint8 bucket_index1 = 0;
    uint8 bucket_index2 = 0;
    uint8 bucket_use_bitmap = 0;

    if (!p_new_prof || !p_old_prof)
    {
        return FALSE;
    }

    if (!p_new_prof->exact_match)
    {
        for (bucket_index1 = 0; bucket_index1 < SYS_GRP_SHP_CBUCKET_NUM; bucket_index1++)
        {
            if (p_new_prof->c_bucket[bucket_index1].cir_type != SYS_QUEUE_GRP_SHP_CIR_TYPE_CIR_USER)
            {
                continue;
            }

            for (bucket_index2 = 0; bucket_index2 < SYS_GRP_SHP_CBUCKET_NUM; bucket_index2++)
            {
                if (CTC_IS_BIT_SET(bucket_use_bitmap, bucket_index2)
                    || p_old_prof->c_bucket[bucket_index2].cir_type != SYS_QUEUE_GRP_SHP_CIR_TYPE_CIR_USER)
                {
                    continue;
                }

                if (p_new_prof->c_bucket[bucket_index1].rate == p_old_prof->c_bucket[bucket_index2].rate &&
                    p_new_prof->c_bucket[bucket_index1].thrd ==  p_old_prof->c_bucket[bucket_index2].thrd)
                {
                    CTC_BIT_SET(bucket_use_bitmap, bucket_index2);
                    break;
                }
            }

            if (bucket_index2 == SYS_GRP_SHP_CBUCKET_NUM)
            {
                return FALSE;
            }
        }


    }
    else
    {
        if (sal_memcmp(p_new_prof->c_bucket, p_old_prof->c_bucket, sizeof(p_old_prof->c_bucket)))
        {
            return FALSE;
        }
    }

    if (p_new_prof->bucket_rate != p_old_prof->bucket_rate ||
        p_new_prof->bucket_thrd != p_old_prof->bucket_thrd)
    {
        return FALSE;
    }

    SYS_QUEUE_DBG_INFO("Exact Match:%d, Found group shping profile:%d!!!\n", p_new_prof->exact_match, p_old_prof->profile_id);

    return TRUE;
}



int32
_sys_goldengate_queue_shp_free_offset_group_shape_profile(uint8 lchip, uint8 slice_id,

                                               uint16 profile_id)
{
    sys_goldengate_opf_t opf;
    uint32 offset  = 0;

    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
    opf.pool_type = OPF_GROUP_SHAPE_PROFILE;
    opf.pool_index = slice_id;

    offset = profile_id;

    CTC_ERROR_RETURN(sys_goldengate_opf_free_offset(lchip, &opf, 1, offset));

    return CTC_E_NONE;
}

int32
_sys_goldengate_queue_shp_alloc_offset_group_shape_profile(uint8 lchip, uint8 slice_id,
                                                uint16* profile_id)
{
    sys_goldengate_opf_t opf;
    uint32 offset  = 0;

    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
    opf.pool_type = OPF_GROUP_SHAPE_PROFILE;
    opf.pool_index = slice_id;

    CTC_ERROR_RETURN(sys_goldengate_opf_alloc_offset(lchip, &opf, 1, &offset));
    *profile_id = offset;

    return CTC_E_NONE;
}

int32
_sys_goldengate_queue_shp_add_group_shape_profile_to_asic(uint8 lchip, uint8 slice_id,
                                               uint16 sub_group_id,
                                               sys_group_shp_profile_t* p_sys_profile)
{
    uint32 cmd = 0;
    uint32 token_rate;
    uint32 token_rate_frac;
    uint32 token_threshold;
    uint32 token_thrd_shift;
    uint32 tbl_index = 0;
    uint8 group_mode = 0;
    DsGrpShpProfile_m grp_shp_profile;
    CTC_PTR_VALID_CHECK(p_sys_profile);

    CTC_ERROR_RETURN(sys_goldengate_queue_get_grp_mode(lchip, sub_group_id/2, &group_mode));

    tbl_index  = slice_id *SYS_MAX_GROUP_SHAPE_PROFILE_NUM + p_sys_profile->profile_id;
    sal_memset(&grp_shp_profile, 0, sizeof(DsGrpShpProfile_m));

    cmd = DRV_IOR(DsGrpShpProfile_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, tbl_index, cmd, &grp_shp_profile));

    if (group_mode || !CTC_IS_BIT_SET(sub_group_id, 0))
    {
        token_rate = p_sys_profile->c_bucket[0].rate >> 8;
        token_rate_frac = p_sys_profile->c_bucket[0].rate & 0xFF;
        token_threshold = p_sys_profile->c_bucket[0].thrd >> 4;
        token_thrd_shift = p_sys_profile->c_bucket[0].thrd & 0xF;

        SetDsGrpShpProfile(V, bucket0TokenRate_f,       &grp_shp_profile, token_rate);
        SetDsGrpShpProfile(V, bucket0TokenRateFrac_f,   &grp_shp_profile, token_rate_frac);
        SetDsGrpShpProfile(V, bucket0TokenThrd_f,       &grp_shp_profile, token_threshold);
        SetDsGrpShpProfile(V, bucket0TokenThrdShift_f,  &grp_shp_profile, token_thrd_shift);
    }

    if (group_mode)
    {
        token_rate = p_sys_profile->c_bucket[1].rate >> 8;
        token_rate_frac = p_sys_profile->c_bucket[1].rate & 0xFF;
        token_threshold = p_sys_profile->c_bucket[1].thrd >> 4;
        token_thrd_shift = p_sys_profile->c_bucket[1].thrd & 0xF;

        SetDsGrpShpProfile(V, bucket1TokenRate_f,       &grp_shp_profile, token_rate);
        SetDsGrpShpProfile(V, bucket1TokenRateFrac_f,   &grp_shp_profile, token_rate_frac);
        SetDsGrpShpProfile(V, bucket1TokenThrd_f,       &grp_shp_profile, token_threshold);
        SetDsGrpShpProfile(V, bucket1TokenThrdShift_f,  &grp_shp_profile, token_thrd_shift);

        token_rate = p_sys_profile->c_bucket[2].rate >> 8;
        token_rate_frac = p_sys_profile->c_bucket[2].rate & 0xFF;
        token_threshold = p_sys_profile->c_bucket[2].thrd >> 4;
        token_thrd_shift = p_sys_profile->c_bucket[2].thrd & 0xF;

        SetDsGrpShpProfile(V, bucket2TokenRate_f,       &grp_shp_profile, token_rate);
        SetDsGrpShpProfile(V, bucket2TokenRateFrac_f,   &grp_shp_profile, token_rate_frac);
        SetDsGrpShpProfile(V, bucket2TokenThrd_f,       &grp_shp_profile, token_threshold);
        SetDsGrpShpProfile(V, bucket2TokenThrdShift_f,  &grp_shp_profile, token_thrd_shift);
    }
    else if (CTC_IS_BIT_SET(sub_group_id, 0))
    {
        token_rate = p_sys_profile->c_bucket[0].rate >> 8;
        token_rate_frac = p_sys_profile->c_bucket[0].rate & 0xFF;
        token_threshold = p_sys_profile->c_bucket[0].thrd >> 4;
        token_thrd_shift = p_sys_profile->c_bucket[0].thrd & 0xF;

        SetDsGrpShpProfile(V, bucket2TokenRate_f,       &grp_shp_profile, token_rate);
        SetDsGrpShpProfile(V, bucket2TokenRateFrac_f,   &grp_shp_profile, token_rate_frac);
        SetDsGrpShpProfile(V, bucket2TokenThrd_f,       &grp_shp_profile, token_threshold);
        SetDsGrpShpProfile(V, bucket2TokenThrdShift_f,  &grp_shp_profile, token_thrd_shift);
    }

    /*token token rate */
    token_rate = p_sys_profile->bucket_rate >> 8;
    token_rate_frac = p_sys_profile->bucket_rate & 0xFF;
    token_threshold = p_sys_profile->bucket_thrd >> 4;
    token_thrd_shift = p_sys_profile->bucket_thrd & 0xF;

    if (group_mode || CTC_IS_BIT_SET(sub_group_id, 0))
    {
        SetDsGrpShpProfile(V, bucket3TokenRate_f,       &grp_shp_profile, token_rate);
        SetDsGrpShpProfile(V, bucket3TokenRateFrac_f,   &grp_shp_profile, token_rate_frac);
        SetDsGrpShpProfile(V, bucket3TokenThrd_f,       &grp_shp_profile, token_threshold);
        SetDsGrpShpProfile(V, bucket3TokenThrdShift_f,  &grp_shp_profile, token_thrd_shift);
    }
    else
    {
        SetDsGrpShpProfile(V, bucket1TokenRate_f,       &grp_shp_profile, token_rate);
        SetDsGrpShpProfile(V, bucket1TokenRateFrac_f,   &grp_shp_profile, token_rate_frac);
        SetDsGrpShpProfile(V, bucket1TokenThrd_f,       &grp_shp_profile, token_threshold);
        SetDsGrpShpProfile(V, bucket1TokenThrdShift_f,  &grp_shp_profile, token_thrd_shift);
    }

    cmd = DRV_IOW(DsGrpShpProfile_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, tbl_index, cmd, &grp_shp_profile));

    SYS_QUEUE_DBG_INFO("add profile_id = %d\n", tbl_index);

    return CTC_E_NONE;
}

int32
_sys_goldengate_queue_shp_remove_group_shape_profile_from_asic(uint8 lchip, uint8 slice_id,
                                                    sys_group_shp_profile_t* p_sys_profile)
{
    /*do nothing*/
    return CTC_E_NONE;
}

int32
_sys_goldengate_queue_shp_add_group_shape_to_asic(uint8 lchip, uint8 slice_id,
                                       uint8 is_pir, uint16 sub_grp_id,
                                       sys_queue_group_shp_node_t* p_sys_group_shp)
{
    uint8 queOffset = 0;
    uint32 fld_id = 0 ;
    uint32 fld_value = 0;
    uint32 cmd = 0;
    uint16 super_grp = 0;
    uint8 bucket_idx = 0;
    sys_group_shp_profile_t* p_sys_profile = NULL;
    uint8 bucket_use_bitmap = 0;
    uint8 group_mode = 0;
    uint16 grp_profId_index = 0;

    CTC_PTR_VALID_CHECK(p_sys_group_shp);
    CTC_PTR_VALID_CHECK(p_sys_group_shp->p_shp_profile);

    CTC_ERROR_RETURN(sys_goldengate_queue_get_grp_mode(lchip, sub_grp_id/2, &group_mode));

    p_sys_profile = p_sys_group_shp->p_shp_profile;

    grp_profId_index = sub_grp_id;
    /*if group_mode, only use even sub_grp_id as index*/
    if (group_mode)
    {
        CTC_BIT_UNSET(grp_profId_index, 0);
    }

    for (queOffset = 0; queOffset < SYS_QUEUE_NUM_PER_GROUP; queOffset++)
    {
        if(p_sys_group_shp->c_bucket[queOffset].cir_type == SYS_QUEUE_GRP_SHP_CIR_TYPE_CIR_FAIL)
        {
            fld_value  = group_mode ?  7 : 3; /*CIR fail*/
        }
        else if (p_sys_group_shp->c_bucket[queOffset].cir_type == SYS_QUEUE_GRP_SHP_CIR_TYPE_CIR_PASS)
        {
            fld_value  = group_mode ? 4 : 0; /*CIR pass*/
        }
        else
        {
            if (group_mode)
            {
                for (bucket_idx = 0; bucket_idx < SYS_GRP_SHP_CBUCKET_NUM; bucket_idx++)
                {
                    if (CTC_IS_BIT_SET(bucket_use_bitmap, bucket_idx))
                    {
                        continue;
                    }

                    if ( p_sys_group_shp->c_bucket[queOffset].cir_type == SYS_QUEUE_GRP_SHP_CIR_TYPE_CIR_USER
                        && p_sys_group_shp->c_bucket[queOffset].rate == p_sys_profile->c_bucket[bucket_idx].rate
                        && p_sys_group_shp->c_bucket[queOffset].thrd == p_sys_profile->c_bucket[bucket_idx].thrd)
                    {
                        p_sys_group_shp->c_bucket[queOffset].bucket_sel = bucket_idx;
                        fld_value = bucket_idx;
                        CTC_BIT_SET(bucket_use_bitmap, bucket_idx);
                        break;
                    }
                }
            }
            else
            {
                if ( p_sys_group_shp->c_bucket[queOffset].cir_type == SYS_QUEUE_GRP_SHP_CIR_TYPE_CIR_USER
                    && p_sys_group_shp->c_bucket[queOffset].rate == p_sys_profile->c_bucket[0].rate
                    && p_sys_group_shp->c_bucket[queOffset].thrd == p_sys_profile->c_bucket[0].thrd)
                {
                    p_sys_group_shp->c_bucket[queOffset].bucket_sel = 1;
                    fld_value = 1;
                    CTC_BIT_SET(bucket_use_bitmap, 1);
                }
            }

        }
        fld_id = DsSchServiceProfile_queOffset0SelCir_f +
                (DsSchServiceProfile_queOffset1SelCir_f - DsSchServiceProfile_queOffset0SelCir_f) * queOffset;
        cmd = DRV_IOW(DsSchServiceProfile_t, fld_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, grp_profId_index, cmd, &fld_value));

    }

    fld_value = p_sys_group_shp->p_shp_profile->profile_id*2 + CTC_IS_BIT_SET(sub_grp_id, 0);
    cmd = DRV_IOW(RaGrpShpProfId_t, RaGrpShpProfId_profId_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, grp_profId_index, cmd, &fld_value));

    super_grp =  sub_grp_id/2;   /*Super Group*/
    cmd = DRV_IOR(RaGrpQueShpCtl_t, RaGrpQueShpCtl_grpShpEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, super_grp, cmd, &fld_value));
    if(group_mode || !CTC_IS_BIT_SET(sub_grp_id, 0))
    {
        CTC_BIT_SET(fld_value, 0);
    }
    else
    {
        CTC_BIT_SET(fld_value, 1);
    }
    cmd = DRV_IOW(RaGrpQueShpCtl_t, RaGrpQueShpCtl_grpShpEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, super_grp, cmd, &fld_value));

    return CTC_E_NONE;
}

int32
_sys_goldengate_queue_shp_remove_group_shape_from_asic(uint8 lchip, uint8 slice_id, uint16 sub_grp_id,
                                            sys_queue_group_shp_node_t* p_sys_group)
{
    uint32 fld_value = 0;
    uint32 cmd = 0;
    uint16 super_grp = 0;
    uint8 group_mode = 0;
    uint32 sel_cir   = 0;
    uint16 grp_profId_index = 0;
    DsSchServiceProfile_m sch_service_profile;
    sal_memset(&sch_service_profile, 0, sizeof(sch_service_profile));

    super_grp = sub_grp_id/2;

    CTC_ERROR_RETURN(sys_goldengate_queue_get_grp_mode(lchip, super_grp, &group_mode));

    /*group shaping always enable*/
    cmd = DRV_IOR(RaGrpQueShpCtl_t, RaGrpQueShpCtl_grpShpEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, super_grp, cmd, &fld_value));
    if(group_mode || !CTC_IS_BIT_SET(sub_grp_id, 0))
    {
        CTC_BIT_SET(fld_value, 0);
    }
    else
    {
        CTC_BIT_SET(fld_value, 1);
    }
    cmd = DRV_IOW(RaGrpQueShpCtl_t, RaGrpQueShpCtl_grpShpEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, super_grp, cmd, &fld_value));

    grp_profId_index = sub_grp_id;
    /*if group_mode, only use even sub_grp_id as index*/
    if (group_mode)
    {
        CTC_BIT_UNSET(grp_profId_index, 0);
    }

    sel_cir = (group_mode ? 7 : 3);
    cmd = DRV_IOR(DsSchServiceProfile_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, grp_profId_index, cmd, &sch_service_profile));
    SetDsSchServiceProfile(V, queOffset0SelCir_f, &sch_service_profile, sel_cir);/*CIR Fail*/
    SetDsSchServiceProfile(V, queOffset1SelCir_f, &sch_service_profile, sel_cir);/*CIR Fail*/
    SetDsSchServiceProfile(V, queOffset2SelCir_f, &sch_service_profile, sel_cir);/*CIR Fail*/
    SetDsSchServiceProfile(V, queOffset3SelCir_f, &sch_service_profile, sel_cir);/*CIR Fail*/
    SetDsSchServiceProfile(V, queOffset4SelCir_f, &sch_service_profile, sel_cir);/*CIR Fail*/
    SetDsSchServiceProfile(V, queOffset5SelCir_f, &sch_service_profile, sel_cir);/*CIR Fail*/
    SetDsSchServiceProfile(V, queOffset6SelCir_f, &sch_service_profile, sel_cir);/*CIR Fail*/
    SetDsSchServiceProfile(V, queOffset7SelCir_f, &sch_service_profile, sel_cir);/*CIR Fail*/
    cmd = DRV_IOW(DsSchServiceProfile_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, grp_profId_index, cmd, &sch_service_profile));

    fld_value = 0;
    cmd = DRV_IOW(RaGrpShpProfId_t, RaGrpShpProfId_profId_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, grp_profId_index, cmd, &fld_value));

    return CTC_E_NONE;
}

int32
_sys_goldengate_queue_shp_remove_group_shape_profile(uint8 lchip, uint8 slice_id,
                                          sys_group_shp_profile_t* p_sys_profile_old)
{
    sys_group_shp_profile_t* p_sys_profile_find = NULL;
    ctc_spool_t* p_profile_pool               = NULL;
    int32 ret            = 0;
    uint16 profile_id    = 0;

    SYS_QUEUE_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_sys_profile_old);

    p_profile_pool = p_gg_queue_master[lchip]->p_group_profile_pool[slice_id];
    p_sys_profile_old->exact_match = 1;
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

    SYS_QUEUE_DBG_INFO("remove ret:%d!!!!!!!!\n", ret);

    if (ret == CTC_SPOOL_E_OPERATE_MEMORY)
    {
        CTC_ERROR_RETURN(_sys_goldengate_queue_shp_remove_group_shape_profile_from_asic(lchip, slice_id, p_sys_profile_old));
        profile_id = p_sys_profile_find->profile_id;
        /*free ad index*/
        CTC_ERROR_RETURN(_sys_goldengate_queue_shp_free_offset_group_shape_profile(lchip, slice_id, profile_id));
        mem_free(p_sys_profile_find);
        SYS_QUEUE_DBG_INFO("free profile_id:%d!!!!!!!!\n", profile_id);
    }
    return CTC_E_NONE;

}

int32
_sys_goldengate_queue_shp_add_group_shape_profile(uint8 lchip, uint8 slice_id, uint16 sub_grp_id,
                                       sys_queue_group_shp_node_t* p_sys_group_shp,
                                       sys_group_shp_profile_t* p_shp_profile)
{

    ctc_spool_t* p_profile_pool    = NULL;
    sys_group_shp_profile_t* p_sys_profile_old = NULL;
    sys_group_shp_profile_t* p_sys_profile_new = NULL;
    sys_group_shp_profile_t* p_sys_profile_get = NULL;
    int32 ret            = 0;
    uint16 old_profile_id = 0;
    uint16 profile_id    = 0;

    CTC_PTR_VALID_CHECK(p_sys_group_shp);
    p_sys_profile_old = p_sys_group_shp->p_shp_profile;
    p_profile_pool = p_gg_queue_master[lchip]->p_group_profile_pool[slice_id];


    p_sys_profile_new = mem_malloc(MEM_QUEUE_MODULE, sizeof(sys_group_shp_profile_t));
    if (NULL == p_sys_profile_new)
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memcpy(p_sys_profile_new, p_shp_profile, sizeof(sys_group_shp_profile_t));
    /*if use new date to replace old data, profile_id will not change*/
    if(p_sys_profile_old)
    {
        if(TRUE == _sys_goldengate_queue_shp_hash_cmp_group_shape_profile(p_sys_profile_old, p_sys_profile_new))
        {
            mem_free(p_sys_profile_new);
            return CTC_E_NONE;
        }
        old_profile_id = p_sys_profile_old->profile_id;
        p_sys_profile_new->profile_id= old_profile_id;
    }

	ret = ctc_spool_add(p_profile_pool,
                           p_sys_profile_new,
                           p_sys_profile_old,
                           &p_sys_profile_get);

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
            ret = _sys_goldengate_queue_shp_alloc_offset_group_shape_profile(lchip, slice_id, &profile_id);
            if (ret < 0)
            {
                ctc_spool_remove(p_profile_pool, p_sys_profile_new, NULL);
                mem_free(p_sys_profile_new);
                goto ERROR_RETURN;
            }

            p_sys_profile_get->profile_id = profile_id;
            SYS_QUEUE_DBG_INFO("Need new profile:%d\n", profile_id);

            /*write  prof*/
            CTC_ERROR_RETURN(_sys_goldengate_queue_shp_add_group_shape_profile_to_asic(lchip, slice_id, sub_grp_id,p_sys_profile_new));
        }


    }

    /*key is found, so there is an old ad need to be deleted.*/
    /*if profile_id not change, no need to delete old ad(old ad is replaced by new)*/
    if (p_sys_profile_old && p_sys_profile_get->profile_id != old_profile_id)
    {
        CTC_ERROR_RETURN(_sys_goldengate_queue_shp_remove_group_shape_profile(lchip, slice_id,p_sys_profile_old));
    }

    p_sys_group_shp->p_shp_profile = p_sys_profile_get;

	return CTC_E_NONE;

ERROR_RETURN:
    return ret;


}

int32
sys_goldengate_queue_shp_save_group_shape_bucket(uint8 lchip, uint8 enable,
                                            uint8  is_pir,
                                            uint8  queue_offset,
                                            uint32 bucket_rate,
                                            uint32 bucket_thrd,
                                            uint8 new_cir_type,
                                            uint8 group_mode,
                                            sys_queue_group_shp_node_t* p_sys_group_shp_node,
                                            sys_group_shp_profile_t *p_shp_profile)
{
    uint8 offset    = 0;
    uint8 queue_num = 0;
    uint8 c_bucket_num = 0;
    uint8 bucket_index = 0;

    sal_memset(p_shp_profile,0,sizeof(sys_group_shp_profile_t));
    /*Profile operation*/
    if (is_pir)
    {
        if (p_sys_group_shp_node->p_shp_profile)
        {
            sal_memcpy(p_shp_profile, p_sys_group_shp_node->p_shp_profile, sizeof(sys_group_shp_profile_t));
        }
        p_shp_profile->bucket_rate = bucket_rate;
        p_shp_profile->bucket_thrd = bucket_thrd;

        queue_offset = SYS_QUEUE_PIR_BUCKET;
    }
    else
    {
        sys_queue_shp_grp_info_t old_queue_shp;

        enable = enable && bucket_rate;
        sal_memcpy(&old_queue_shp, &p_sys_group_shp_node->c_bucket[queue_offset], sizeof(sys_queue_shp_grp_info_t));

        p_sys_group_shp_node->c_bucket[queue_offset].cir_type = new_cir_type;
        p_sys_group_shp_node->c_bucket[queue_offset].rate = bucket_rate;
        p_sys_group_shp_node->c_bucket[queue_offset].thrd = bucket_thrd;

        queue_num = group_mode ? SYS_QUEUE_NUM_PER_GROUP : SYS_QUEUE_NUM_PER_GROUP/2;
        c_bucket_num = group_mode ? SYS_GRP_SHP_CBUCKET_NUM : SYS_GRP_SHP_CBUCKET_NUM/2;
        for (offset = 0; offset < queue_num; offset++)
        {
            if (p_sys_group_shp_node->c_bucket[offset].cir_type == SYS_QUEUE_GRP_SHP_CIR_TYPE_CIR_USER)
            {
                if (bucket_index == c_bucket_num)
                {
                    sal_memcpy(&p_sys_group_shp_node->c_bucket[queue_offset],&old_queue_shp, sizeof(sys_queue_shp_grp_info_t));
                    return CTC_E_EXCEED_MAX_SIZE;
                }
                p_shp_profile->c_bucket[bucket_index].cir_type = SYS_QUEUE_GRP_SHP_CIR_TYPE_CIR_USER;
                p_shp_profile->c_bucket[bucket_index].rate = p_sys_group_shp_node->c_bucket[offset].rate;
                p_shp_profile->c_bucket[bucket_index].thrd = p_sys_group_shp_node->c_bucket[offset].thrd;
                bucket_index++;
            }
        }
        if (CTC_IS_BIT_SET(p_sys_group_shp_node->shp_bitmap, SYS_QUEUE_PIR_BUCKET)
            && p_sys_group_shp_node->p_shp_profile)
        {
            p_shp_profile->bucket_rate =  p_sys_group_shp_node->p_shp_profile->bucket_rate;
            p_shp_profile->bucket_thrd = p_sys_group_shp_node->p_shp_profile->bucket_thrd ;
        }
    }

    if (enable)
    {
        CTC_BIT_SET(p_sys_group_shp_node->shp_bitmap, queue_offset);
    }
    else
    {
        CTC_BIT_UNSET(p_sys_group_shp_node->shp_bitmap, queue_offset);
    }
    if (!CTC_IS_BIT_SET(p_sys_group_shp_node->shp_bitmap, SYS_QUEUE_PIR_BUCKET))
    {
        p_shp_profile->bucket_rate = (SYS_MAX_TOKEN_RATE << 8 | SYS_MAX_TOKEN_RATE_FRAC);
        p_shp_profile->bucket_thrd = (SYS_MAX_TOKEN_THRD << 4 | SYS_MAX_TOKEN_THRD_SHIFT);
    }

    return CTC_E_NONE;
}

int32 sys_goldengate_queue_get_grp_mode(uint8 lchip, uint16 group_id, uint8 *mode)
{
    uint32 cmd = 0;
    uint8 slice_id = 0;
    uint32 grp_map_mode[4];

    slice_id = SYS_MAP_GROUPID_TO_SLICE(group_id);

    cmd = DRV_IOR(QMgrDeqGrpMapMode_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, slice_id, cmd, &grp_map_mode));

    *mode = CTC_IS_BIT_SET(grp_map_mode[(group_id>>5)&0x3], group_id&0x1F);

    return CTC_E_NONE;
}

int32 sys_goldengate_queue_set_grp_mode(uint8 lchip, uint8 slice_id)
{
    QMgrDeqGrpMapMode_m grp_map_mode;
    uint32 cmd = 0;
    uint8 offset = 0;
    sal_memset(&grp_map_mode, 0xFF, sizeof(QMgrDeqGrpMapMode_m));

    if (1 == p_gg_queue_master[lchip]->enq_mode)
    {
        sal_memset(&grp_map_mode, 0x55, 48 / 4); /*8uc + 4mc + 1mirror*/
    }
    else if ((2 == p_gg_queue_master[lchip]->enq_mode)&&(p_gg_queue_master[lchip]->queue_num_per_chanel == 4))
    {
        /*(120 - ((p_gg_queue_master[lchip]->queue_num_for_cpu_reason - 32)/8))/8*/
        offset = ((p_gg_queue_master[lchip]->queue_num_for_cpu_reason - 32 + 63)/64);
        sal_memset(&grp_map_mode, 0x00, (15 - offset)); /*4*/
        if (0 != ((p_gg_queue_master[lchip]->queue_num_for_cpu_reason - 32)%64))
        {
            sal_memset((((uint8*)(&grp_map_mode)) + offset), 0x0F, 1); /*4*/
        }
    }

    cmd = DRV_IOW(QMgrDeqGrpMapMode_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, slice_id, cmd, &grp_map_mode));

    return CTC_E_NONE;
}

int32
sys_goldengate_queue_shp_update_group_shape(uint8 lchip, uint16 sub_grp_id,
                                       uint32 cir,
                                       uint32 cbs,
                                       uint8  queue_offset,
                                       uint8  is_pir,
                                       uint8  enable)
{
    sys_queue_group_node_t* p_sys_group_node = NULL;
    sys_queue_group_shp_node_t* p_sys_group_shp_node = NULL;
    sys_group_shp_profile_t  shp_profile;
    uint8  slice_id = 0;
    uint32 token_thrd = 0 ;
    uint32 gran = 0;
    uint8  new_cir_type =0 ;
    uint8 is_pps ;
    uint8 mode = 0;
    uint16 group_id = 0;
    uint32 max_token_thrd = 0;
    uint32 user_rate = cir;

    SYS_QUEUE_DBG_FUNC();

    sal_memset(&shp_profile, 0, sizeof(shp_profile));
    group_id = sub_grp_id/2;
    slice_id = SYS_MAP_GROUPID_TO_SLICE(group_id);

    CTC_ERROR_RETURN(sys_goldengate_queue_get_grp_mode(lchip, group_id, &mode));
    if((mode == 0) && !SYS_QUEUE_DESTID_ENQ(lchip))
    {
        return CTC_E_NONE;
    }

    if (queue_offset >= SYS_QUEUE_NUM_PER_GROUP)
    {
        return CTC_E_INVALID_PARAM;
    }

    p_sys_group_node = ctc_vector_get(p_gg_queue_master[lchip]->group_vec, group_id);
    if (NULL == p_sys_group_node)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    if (mode)
    {
        p_sys_group_shp_node = &p_sys_group_node->grp_shp[0];
    }
    else
    {
        p_sys_group_shp_node = &p_sys_group_node->grp_shp[sub_grp_id%2];
    }

    if (!enable && (NULL == p_sys_group_shp_node->p_shp_profile))
    {
        return CTC_E_NONE;
    }
    p_sys_group_node->sub_group_id = sub_grp_id;

    /*config CIR Bucket :CIR = 0->CIR Fail;cir =CTC_MAX_UINT32_VALUE -> CIR pass */
    if (!is_pir)
    {
        if (cir == 0 || !enable)
        {
            new_cir_type = SYS_QUEUE_GRP_SHP_CIR_TYPE_CIR_FAIL;
            cir = 0;
        }
        else if(cir == CTC_MAX_UINT32_VALUE )
        {
            new_cir_type = SYS_QUEUE_GRP_SHP_CIR_TYPE_CIR_PASS;
        }
        else if( enable)
        {
            new_cir_type = SYS_QUEUE_GRP_SHP_CIR_TYPE_CIR_USER;
        }
    }

    if ( new_cir_type == SYS_QUEUE_GRP_SHP_CIR_TYPE_CIR_USER || is_pir)
    {
        is_pps = sys_goldengate_queue_shp_get_channel_pps_enbale(lchip, p_sys_group_node->chan_id);

        CTC_ERROR_RETURN(sys_goldengate_qos_get_shp_granularity(lchip, cir,SYS_SHAPE_TYPE_QUEUE, &gran));

        /* convert to internal token rate */
        CTC_ERROR_RETURN(sys_goldengate_qos_map_token_rate_user_to_hw(lchip, is_pps, cir,
                                                                      &cir,
                                                                      SYS_SHAPE_TOKEN_RATE_BIT_WIDTH,
                                                                      gran,
                                                                      p_gg_queue_master[lchip]->que_shp_update_freq));

        if (0 == cbs)
        {
            if (is_pps)
            {
                cbs = 16 * 1000;/*bytes*/
            }
            else
            {
                cbs = cir + 1; /*bytes, extend token_thrd for schedule*/
            }

        }
        else
        {
            if (is_pps)
            {
                cbs = cbs * 1000; /* one packet convers to 1000B */
            }
            else
            {
                cbs = cbs * 125; /* 1000/8 */
            }
        }

        /*for queue cnt overturn*/
        if (is_pir && enable)
        {
            CTC_ERROR_RETURN(_sys_godengate_qos_shape_param_check(lchip, is_pps, cir, user_rate, &cbs,
                                                                  p_gg_queue_master[lchip]->que_shp_update_freq));
        }

        /*compute max_token_thrd*/
        sys_goldengate_qos_map_token_thrd_hw_to_user(lchip, &max_token_thrd, (0xFF << SYS_SHAPE_TOKEN_THRD_SHIFTS_WIDTH | 0xF), SYS_SHAPE_TOKEN_THRD_SHIFTS_WIDTH);

        /*bucket_thrd can not exceed max value*/
        if (cbs > max_token_thrd)
        {
            cbs = max_token_thrd;
        }


        /* convert to internal token thrd */
        CTC_ERROR_RETURN(sys_goldengate_qos_map_token_thrd_user_to_hw(lchip, cbs,
                                                                      &token_thrd,
                                                                      SYS_SHAPE_TOKEN_THRD_SHIFTS_WIDTH,
                                                                      SYS_SHAPE_MAX_TOKEN_THRD));

        cbs = token_thrd;
    }




    CTC_ERROR_RETURN (sys_goldengate_queue_shp_save_group_shape_bucket(lchip, enable, is_pir, queue_offset,
                                    cir, cbs, new_cir_type, mode, p_sys_group_shp_node, &shp_profile));

    /*add new prof*/
    if (p_sys_group_shp_node->shp_bitmap)
    {
        CTC_ERROR_RETURN(_sys_goldengate_queue_shp_add_group_shape_profile(lchip, slice_id, sub_grp_id,
                                                                 p_sys_group_shp_node,
                                                                 &shp_profile));

        CTC_ERROR_RETURN(_sys_goldengate_queue_shp_add_group_shape_to_asic(lchip, slice_id, is_pir, sub_grp_id,
                                                                 p_sys_group_shp_node));

    }
    else if (p_sys_group_shp_node->p_shp_profile)
    {
        /*disable Group Shape*/
        CTC_ERROR_RETURN(_sys_goldengate_queue_shp_remove_group_shape_from_asic(lchip, slice_id, sub_grp_id,
                                                                      p_sys_group_shp_node));
        /*remove prof*/
        CTC_ERROR_RETURN(_sys_goldengate_queue_shp_remove_group_shape_profile(lchip, slice_id,
                                                                    p_sys_group_shp_node->p_shp_profile));
        p_sys_group_shp_node->p_shp_profile = 0;
    }

    return CTC_E_NONE;
}


int32
sys_goldengate_queue_shp_set_group_shape(uint8 lchip, ctc_qos_shape_group_t* p_shape)
{

    uint16 queue_id = 0;

    /*get queue_id*/
    if(p_shape->group_type == CTC_QOS_SCHED_GROUP_SERVICE)
    {
        return CTC_E_FEATURE_NOT_SUPPORT;
    }

    SYS_QUEUE_DBG_PARAM("p_shape->enable = %d\n", p_shape->enable);
    SYS_QUEUE_DBG_PARAM("p_shape->pir = %d\n", p_shape->pir);


    CTC_ERROR_RETURN(sys_goldengate_queue_get_queue_id(lchip, &p_shape->queue,
                                                           &queue_id));


    CTC_ERROR_RETURN(sys_goldengate_queue_shp_update_group_shape(lchip,
                                           queue_id/4,
                                           p_shape->pir,
                                           p_shape->pbs,
                                           0,
                                           1,
                                           p_shape->enable));



    return CTC_E_NONE;
}

int32
_sys_goldengate_queue_shp_init_group_shape(uint8 lchip, uint8 slice_id)
{

    uint32 cmd = 0;
    uint32 fld_value = 0;

    DsGrpShpProfile_m grp_shp_profile;
    QMgrGrpShapeCtl_m grp_shape_ctl;
    RaGrpQueShpCtl_m grp_que_shp_ctl;
    DsSchServiceProfile_m sch_service_profile;
    uint16 profile_id;
    uint16 super_grp = 0;
    uint16 sub_grp = 0;

    uint8 group_mode    = 0;
    uint32 sel_cir      = 7;
    uint32 sel_pir      = 3;

    sal_memset(&grp_shp_profile, 0, sizeof(grp_shp_profile));
    sal_memset(&grp_que_shp_ctl, 0, sizeof(grp_que_shp_ctl));
    sal_memset(&grp_shape_ctl, 0, sizeof(grp_shape_ctl));
    sal_memset(&sch_service_profile, 0, sizeof(sch_service_profile));


    SetDsGrpShpProfile(V, bucket0TokenRateFrac_f, &grp_shp_profile, SYS_MAX_TOKEN_RATE_FRAC);
    SetDsGrpShpProfile(V, bucket0TokenRate_f, &grp_shp_profile, SYS_MAX_TOKEN_RATE);
    SetDsGrpShpProfile(V, bucket0TokenThrdShift_f, &grp_shp_profile, SYS_MAX_TOKEN_THRD_SHIFT);
    SetDsGrpShpProfile(V, bucket0TokenThrd_f, &grp_shp_profile, SYS_MAX_TOKEN_THRD);

    SetDsGrpShpProfile(V, bucket1TokenRateFrac_f, &grp_shp_profile, SYS_MAX_TOKEN_RATE_FRAC);
    SetDsGrpShpProfile(V, bucket1TokenRate_f, &grp_shp_profile, SYS_MAX_TOKEN_RATE);
    SetDsGrpShpProfile(V, bucket1TokenThrdShift_f, &grp_shp_profile, SYS_MAX_TOKEN_THRD_SHIFT);
    SetDsGrpShpProfile(V, bucket1TokenThrd_f, &grp_shp_profile, SYS_MAX_TOKEN_THRD);

    SetDsGrpShpProfile(V, bucket2TokenRateFrac_f, &grp_shp_profile, SYS_MAX_TOKEN_RATE_FRAC);
    SetDsGrpShpProfile(V, bucket2TokenRate_f, &grp_shp_profile, SYS_MAX_TOKEN_RATE);
    SetDsGrpShpProfile(V, bucket2TokenThrdShift_f, &grp_shp_profile, SYS_MAX_TOKEN_THRD_SHIFT);
    SetDsGrpShpProfile(V, bucket2TokenThrd_f, &grp_shp_profile, SYS_MAX_TOKEN_THRD);

    SetDsGrpShpProfile(V, bucket3TokenRateFrac_f, &grp_shp_profile, SYS_MAX_TOKEN_RATE_FRAC);
    SetDsGrpShpProfile(V, bucket3TokenRate_f, &grp_shp_profile, SYS_MAX_TOKEN_RATE);
    SetDsGrpShpProfile(V, bucket3TokenThrdShift_f, &grp_shp_profile, SYS_MAX_TOKEN_THRD_SHIFT);
    SetDsGrpShpProfile(V, bucket3TokenThrd_f, &grp_shp_profile, SYS_MAX_TOKEN_THRD);
    cmd = DRV_IOW(DsGrpShpProfile_t, DRV_ENTRY_FLAG);

    for (profile_id = 0; profile_id < SYS_MAX_GROUP_SHAPE_PROFILE_NUM; profile_id++)
    {
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, slice_id *SYS_MAX_GROUP_SHAPE_PROFILE_NUM + profile_id, cmd, &grp_shp_profile));
    }

    for (super_grp = 0; super_grp < (SYS_MAX_QUEUE_GROUP_NUM/2); super_grp++)
    {
        SetRaGrpQueShpCtl(V, grpShpEn_f, &grp_que_shp_ctl, 3);
        SetRaGrpQueShpCtl(V, queCreditBaseEn_f, &grp_que_shp_ctl, 0);
        /*Grp shping always enable*/
        SetRaGrpQueShpCtl(V, queShpEn_f, &grp_que_shp_ctl, 0);
        cmd = DRV_IOW(RaGrpQueShpCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, slice_id *(SYS_MAX_QUEUE_GROUP_NUM/2) + super_grp, cmd, &grp_que_shp_ctl));
    }

    fld_value = 0;
    for (sub_grp = 0; sub_grp < SYS_MAX_QUEUE_GROUP_NUM; sub_grp++)
    {
        cmd = DRV_IOW(RaGrpShpProfId_t, RaGrpShpProfId_profId_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, slice_id *SYS_MAX_QUEUE_GROUP_NUM + sub_grp, cmd, &fld_value));

    }

    SetQMgrGrpShapeCtl(V, grpShpGlbEn_f, &grp_shape_ctl, 1);
    cmd = DRV_IOW(QMgrGrpShapeCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, slice_id, cmd, &grp_shape_ctl));

    CTC_ERROR_RETURN(sys_goldengate_queue_set_grp_mode(lchip, slice_id));

    cmd = DRV_IOW(DsSchServiceProfile_t, DRV_ENTRY_FLAG);
    for (sub_grp = 0; sub_grp < SYS_MAX_QUEUE_GROUP_NUM; sub_grp++)
    {
        sys_goldengate_queue_get_grp_mode(lchip, ((SYS_MAX_QUEUE_GROUP_NUM*slice_id + sub_grp)/2), &group_mode);
        sel_cir = (group_mode ? 7 : 3);

        sel_pir = (group_mode ? 3 : 2);

        SetDsSchServiceProfile(V, queOffset0GreenRemapSchPri_f, &sch_service_profile, 7);
        SetDsSchServiceProfile(V, queOffset0RedRemapSchPri_f, &sch_service_profile, 8);
        SetDsSchServiceProfile(V, queOffset0SelCir_f, &sch_service_profile, sel_cir);/*CIR Fail*/
        SetDsSchServiceProfile(V, queOffset0SelPir0_f, &sch_service_profile, sel_pir);/*EIR Pass*/
        SetDsSchServiceProfile(V, queOffset0YellowRemapSchPri_f, &sch_service_profile, 0);

        SetDsSchServiceProfile(V, queOffset1GreenRemapSchPri_f, &sch_service_profile, 7);
        SetDsSchServiceProfile(V, queOffset1RedRemapSchPri_f, &sch_service_profile, 8);
        SetDsSchServiceProfile(V, queOffset1SelCir_f, &sch_service_profile, sel_cir);
        SetDsSchServiceProfile(V, queOffset1SelPir0_f, &sch_service_profile, sel_pir);
        SetDsSchServiceProfile(V, queOffset1YellowRemapSchPri_f, &sch_service_profile, 0);

        SetDsSchServiceProfile(V, queOffset2GreenRemapSchPri_f, &sch_service_profile, 7);
        SetDsSchServiceProfile(V, queOffset2RedRemapSchPri_f, &sch_service_profile, 8);
        SetDsSchServiceProfile(V, queOffset2SelCir_f, &sch_service_profile, sel_cir);
        SetDsSchServiceProfile(V, queOffset2SelPir0_f, &sch_service_profile, sel_pir);
        SetDsSchServiceProfile(V, queOffset2YellowRemapSchPri_f, &sch_service_profile, 0);

        SetDsSchServiceProfile(V, queOffset3GreenRemapSchPri_f, &sch_service_profile, 7);
        SetDsSchServiceProfile(V, queOffset3RedRemapSchPri_f, &sch_service_profile, 8);
        SetDsSchServiceProfile(V, queOffset3SelCir_f, &sch_service_profile, sel_cir);
        SetDsSchServiceProfile(V, queOffset3SelPir0_f, &sch_service_profile, sel_pir);
        SetDsSchServiceProfile(V, queOffset3YellowRemapSchPri_f, &sch_service_profile, 0);

        SetDsSchServiceProfile(V, queOffset4GreenRemapSchPri_f, &sch_service_profile, 7);
        SetDsSchServiceProfile(V, queOffset4RedRemapSchPri_f, &sch_service_profile, 8);
        SetDsSchServiceProfile(V, queOffset4SelCir_f, &sch_service_profile, sel_cir);
        SetDsSchServiceProfile(V, queOffset4SelPir0_f, &sch_service_profile, sel_pir);
        SetDsSchServiceProfile(V, queOffset4YellowRemapSchPri_f, &sch_service_profile, 0);

        SetDsSchServiceProfile(V, queOffset5GreenRemapSchPri_f, &sch_service_profile, 7);
        SetDsSchServiceProfile(V, queOffset5RedRemapSchPri_f, &sch_service_profile, 8);
        SetDsSchServiceProfile(V, queOffset5SelCir_f, &sch_service_profile, sel_cir);
        SetDsSchServiceProfile(V, queOffset5SelPir0_f, &sch_service_profile, sel_pir);
        SetDsSchServiceProfile(V, queOffset5YellowRemapSchPri_f, &sch_service_profile, 0);

        SetDsSchServiceProfile(V, queOffset6GreenRemapSchPri_f, &sch_service_profile, 7);
        SetDsSchServiceProfile(V, queOffset6RedRemapSchPri_f, &sch_service_profile, 8);
        SetDsSchServiceProfile(V, queOffset6SelCir_f, &sch_service_profile, sel_cir);
        SetDsSchServiceProfile(V, queOffset6SelPir0_f, &sch_service_profile, sel_pir);
        SetDsSchServiceProfile(V, queOffset6YellowRemapSchPri_f, &sch_service_profile, 0);

        SetDsSchServiceProfile(V, queOffset7GreenRemapSchPri_f, &sch_service_profile, 7);
        SetDsSchServiceProfile(V, queOffset7RedRemapSchPri_f, &sch_service_profile, 8);
        SetDsSchServiceProfile(V, queOffset7SelCir_f, &sch_service_profile, sel_cir);
        SetDsSchServiceProfile(V, queOffset7SelPir0_f, &sch_service_profile, sel_pir);
        SetDsSchServiceProfile(V, queOffset7YellowRemapSchPri_f, &sch_service_profile, 0);

        /*8 group level WFQ Select 4 SP */
        SetDsSchServiceProfile(V, grpSchPri0RemapChPri_f, &sch_service_profile, 0);
        SetDsSchServiceProfile(V, grpSchPri1RemapChPri_f, &sch_service_profile, 0);
        SetDsSchServiceProfile(V, grpSchPri2RemapChPri_f, &sch_service_profile, 1);
        SetDsSchServiceProfile(V, grpSchPri3RemapChPri_f, &sch_service_profile, 1);
        SetDsSchServiceProfile(V, grpSchPri4RemapChPri_f, &sch_service_profile, 2);
        SetDsSchServiceProfile(V, grpSchPri5RemapChPri_f, &sch_service_profile, 2);
        SetDsSchServiceProfile(V, grpSchPri6RemapChPri_f, &sch_service_profile, 3);
        SetDsSchServiceProfile(V, grpSchPri7RemapChPri_f, &sch_service_profile, 3);

        /*Sub group Sp select WFQ*/
        SetDsSchServiceProfile(V, subGrpSp0PriSel_f, &sch_service_profile, 0);
        SetDsSchServiceProfile(V, subGrpSp1PriSel_f, &sch_service_profile, 1);
        SetDsSchServiceProfile(V, subGrpSp2PriSel_f, &sch_service_profile, 2);
        SetDsSchServiceProfile(V, subGrpSp3PriSel_f, &sch_service_profile, 3);

        /*  last sch select WFQ*/
        SetDsSchServiceProfile(V, grpLastSchPri0Sel_f, &sch_service_profile, 7);
        SetDsSchServiceProfile(V, grpLastSchPri1Sel_f, &sch_service_profile, 7);
        SetDsSchServiceProfile(V, grpLastSchPri2Sel_f, &sch_service_profile, 7);
        SetDsSchServiceProfile(V, grpLastSchPri3Sel_f, &sch_service_profile, 7);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, (SYS_MAX_QUEUE_GROUP_NUM*slice_id + sub_grp), cmd, &sch_service_profile));
    }

    return CTC_E_NONE;
}


#define queue_shp ""
STATIC uint32
_sys_goldengate_queue_shp_hash_make_queue_shape_profile(sys_queue_shp_profile_t* p_prof)
{
    uint32 val = 0;
    uint8* data = (uint8*)p_prof;
    uint8 length = (sizeof(sys_queue_shp_profile_t) - sizeof(uint8));

    val = ctc_hash_caculate(length, data + sizeof(uint8));
    SYS_QUEUE_DBG_INFO("hash val = %d\n", val);
    SYS_QUEUE_DBG_INFO("p_prof->bucket_rate:%d\n", p_prof->bucket_rate);
    SYS_QUEUE_DBG_INFO("p_prof->bucket_thrd:%d\n", p_prof->bucket_thrd);
    SYS_QUEUE_DBG_INFO("p_prof->is_cpu_que_prof:%d\n", p_prof->is_cpu_que_prof);

    return val;
}

/**
 @brief Queue shape hash comparison hook.
*/
STATIC bool
_sys_goldengate_queue_shp_hash_cmp_queue_shape_profile(sys_queue_shp_profile_t* p_prof1,
                                            sys_queue_shp_profile_t* p_prof2)
{


    if (!p_prof1 || !p_prof2)
    {
        return FALSE;
    }

    SYS_QUEUE_DBG_INFO("p_prof1->bucket_rate:%d\n", p_prof1->bucket_rate);
    SYS_QUEUE_DBG_INFO("p_prof2->bucket_rate:%d\n", p_prof2->bucket_rate);

    SYS_QUEUE_DBG_INFO("p_prof1->bucket_thrd:%d\n", p_prof1->bucket_thrd);
    SYS_QUEUE_DBG_INFO("p_prof2->bucket_thrd:%d\n", p_prof2->bucket_thrd);

    SYS_QUEUE_DBG_INFO("p_prof1->is_cpu_que_prof:%d\n", p_prof1->is_cpu_que_prof);
    SYS_QUEUE_DBG_INFO("p_prof2->is_cpu_que_prof:%d\n", p_prof2->is_cpu_que_prof);

    if(p_prof1->bucket_rate == p_prof2->bucket_rate
		&& p_prof1->bucket_thrd == p_prof2->bucket_thrd
		&& p_prof1->is_cpu_que_prof == p_prof2->is_cpu_que_prof)
    {

        SYS_QUEUE_DBG_INFO("Found!!!\n");
        return TRUE;
    }
    return FALSE;
}

int32
_sys_goldengate_queue_shp_free_offset_queue_shape_profile(uint8 lchip, uint8 slice_id,
                                               uint8 is_cpu_queue, uint16 profile_id)
{
    sys_goldengate_opf_t opf;
    uint32 offset  = 0;

    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
    opf.pool_type = OPF_QUEUE_SHAPE_PROFILE;
    opf.pool_index = slice_id;
    if (is_cpu_queue)
    {
        opf.pool_index = 2;
    }

    offset = profile_id;

    CTC_ERROR_RETURN(sys_goldengate_opf_free_offset(lchip, &opf, 1, offset));

    return CTC_E_NONE;
}

int32
_sys_goldengate_queue_shp_alloc_offset_queue_shape_profile(uint8 lchip, uint8 slice_id,
                                                uint8 is_cpu_queue, uint16* profile_id)
{
    sys_goldengate_opf_t opf;
    uint32 offset  = 0;

    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
    opf.pool_type = OPF_QUEUE_SHAPE_PROFILE;
    opf.pool_index = slice_id;
    if (is_cpu_queue)
    {
        opf.pool_index = 2;
    }

    CTC_ERROR_RETURN(sys_goldengate_opf_alloc_offset(lchip, &opf, 1, &offset));
    *profile_id = offset;

    return CTC_E_NONE;
}

int32
_sys_goldengate_queue_shp_add_queue_shape_profile_to_asic(uint8 lchip, uint8 slice_id,
                                               sys_queue_shp_profile_t* p_sys_profile)
{
    DsQueShpProfile_m que_shp_profile;
    uint32 cmd = 0;

    uint32 table_index = 0;

    CTC_PTR_VALID_CHECK(p_sys_profile);

    SYS_QUEUE_DBG_FUNC();

    sal_memset(&que_shp_profile, 0 , sizeof(que_shp_profile));

    /* reserved queue shape profile 0 for max*/
    SetDsQueShpProfile(V,tokenRateFrac_f,&que_shp_profile,p_sys_profile->bucket_rate & 0xFF);
    SetDsQueShpProfile(V,tokenRate_f,&que_shp_profile,p_sys_profile->bucket_rate >> 8);
    SetDsQueShpProfile(V,tokenThrdShift_f,&que_shp_profile,p_sys_profile->bucket_thrd & 0xF);   /* low 4bit is shift */
    SetDsQueShpProfile(V,tokenThrd_f,&que_shp_profile,p_sys_profile->bucket_thrd >>4);
    table_index = (slice_id *SYS_MAX_QUEUE_SHAPE_PROFILE_NUM ) + p_sys_profile->profile_id;
    cmd = DRV_IOW(DsQueShpProfile_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, table_index, cmd, &que_shp_profile);

    return CTC_E_NONE;
}

int32
_sys_goldengate_queue_shp_remove_queue_shape_profile_from_asic(uint8 lchip, uint8 slice_id,
                                                    sys_queue_shp_profile_t* p_sys_profile)
{
    uint32 cmd = 0;
    uint32 table_index = 0;
    DsQueShpProfile_m que_shp_profile;

    CTC_PTR_VALID_CHECK(p_sys_profile);
    sal_memset(&que_shp_profile, 0 , sizeof(que_shp_profile));

    /* reserved queue shape profile 0 for max*/
    SetDsQueShpProfile(V,tokenRateFrac_f,&que_shp_profile,SYS_MAX_TOKEN_RATE_FRAC);
    SetDsQueShpProfile(V,tokenRate_f,&que_shp_profile,SYS_MAX_TOKEN_RATE);
    SetDsQueShpProfile(V,tokenThrdShift_f,&que_shp_profile,SYS_MAX_TOKEN_THRD_SHIFT);
    SetDsQueShpProfile(V,tokenThrd_f,&que_shp_profile,SYS_MAX_TOKEN_THRD);
    table_index = (slice_id *SYS_MAX_QUEUE_SHAPE_PROFILE_NUM ) + p_sys_profile->profile_id;
    cmd = DRV_IOW(DsQueShpProfile_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, table_index, cmd, &que_shp_profile);

    return CTC_E_NONE;
}

int32
_sys_goldengate_queue_shp_add_queue_shape_to_asic(uint8 lchip, uint8 slice_id,
                                       sys_queue_node_t* p_sys_queue)
{
    uint32 cmd = 0;
    uint32 field_val = 0;
    uint16 super_grp = 0;

    CTC_PTR_VALID_CHECK(p_sys_queue);
    CTC_PTR_VALID_CHECK(p_sys_queue->p_shp_profile);

    super_grp = p_sys_queue->queue_id / 8;
    field_val = p_sys_queue->p_shp_profile->profile_id;

    cmd = DRV_IOW(DsQueShpProfId_t, DsQueShpProfId_profId_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_sys_queue->queue_id, cmd, &field_val));

    cmd = DRV_IOR(RaGrpQueShpCtl_t, RaGrpQueShpCtl_queShpEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, super_grp, cmd, &field_val));
    CTC_BIT_SET(field_val, p_sys_queue->queue_id % 8);
    cmd = DRV_IOW(RaGrpQueShpCtl_t, RaGrpQueShpCtl_queShpEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, super_grp, cmd, &field_val));

    SYS_QUEUE_DBG_INFO("queue_id = %d\n", p_sys_queue->queue_id);

    return CTC_E_NONE;
}

int32
_sys_goldengate_queue_shp_remove_queue_shape_from_asic(uint8 lchip,
                                            sys_queue_node_t* p_sys_queue)
{
    uint32 cmd = 0;
    uint16 super_grp = 0;
    uint32 field_val = 1;

    super_grp = p_sys_queue->queue_id / 8;

    cmd = DRV_IOR(RaGrpQueShpCtl_t, RaGrpQueShpCtl_queShpEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, super_grp, cmd, &field_val));
    CTC_BIT_UNSET(field_val, p_sys_queue->queue_id % 8);
    cmd = DRV_IOW(RaGrpQueShpCtl_t, RaGrpQueShpCtl_queShpEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, super_grp, cmd, &field_val));

    field_val = 0;
    cmd = DRV_IOW(DsQueShpProfId_t, DsQueShpProfId_profId_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_sys_queue->queue_id, cmd, &field_val));

    SYS_QUEUE_DBG_INFO("queue_id = %d\n", p_sys_queue->queue_id);


    return CTC_E_NONE;
}

int32
_sys_goldengate_queue_shp_remove_queue_shape_profile(uint8 lchip, uint8 slice_id,
                                          sys_queue_shp_profile_t* p_sys_profile_old)
{
    sys_queue_shp_profile_t* p_sys_profile_find = NULL;
    ctc_spool_t* p_profile_pool               = NULL;
    int32 ret            = 0;
    uint16 profile_id    = 0;

    SYS_QUEUE_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_sys_profile_old);

    p_profile_pool = p_gg_queue_master[lchip]->p_queue_profile_pool[slice_id];

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
		CTC_ERROR_RETURN(_sys_goldengate_queue_shp_remove_queue_shape_profile_from_asic(lchip, slice_id, p_sys_profile_old));

        CTC_ERROR_RETURN(_sys_goldengate_queue_shp_free_offset_queue_shape_profile(lchip, slice_id, p_sys_profile_old->is_cpu_que_prof, profile_id));
        mem_free(p_sys_profile_find);
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_queue_shp_add_queue_shape_profile(uint8 lchip, uint8 slice_id,
                                       ctc_qos_shape_queue_t* p_shp_param,
                                       sys_queue_node_t* p_sys_queue)
{
    ctc_spool_t* p_profile_pool    = NULL;
	sys_queue_group_node_t* p_sys_group_node = NULL;
    sys_queue_shp_profile_t* p_sys_profile_old = NULL;
    sys_queue_shp_profile_t* p_sys_profile_new = NULL;
    sys_queue_shp_profile_t* p_sys_profile_get = NULL;
    int32 ret            = 0;
    uint16 old_profile_id = 0;
    uint16 profile_id    = 0;
	uint32 gran = 0;
    uint32 bucket_rate = 0;
    uint32 bucket_thrd = 0;
	uint8  is_pps = 0;
    uint32 max_bucket_thrd = 0;

    CTC_PTR_VALID_CHECK(p_shp_param);
    CTC_PTR_VALID_CHECK(p_sys_queue);

    p_sys_group_node = ctc_vector_get(p_gg_queue_master[lchip]->group_vec, (p_sys_queue->queue_id/8));
    if (NULL == p_sys_group_node)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }
	is_pps = sys_goldengate_queue_shp_get_channel_pps_enbale(lchip, p_sys_group_node->chan_id);

    CTC_ERROR_RETURN(sys_goldengate_qos_get_shp_granularity(lchip, p_shp_param->pir,
                                                                  SYS_SHAPE_TYPE_QUEUE, &gran));
   /* convert to internal token rate */

   CTC_ERROR_RETURN(sys_goldengate_qos_map_token_rate_user_to_hw(lchip, is_pps,
                                                                 p_shp_param->pir,
                                                                 &bucket_rate,
                                                                 SYS_SHAPE_TOKEN_RATE_BIT_WIDTH,
                                                                 gran,
                                                                 p_gg_queue_master[lchip]->que_shp_update_freq));



   if (0 == p_shp_param->pbs)
   {
       if (is_pps)
       {
           p_shp_param->pbs = 16 * 1000;  /*1pps = 16k bytes*/
       }
       else
       {
           p_shp_param->pbs = bucket_rate + 1; /*extend token_thrd for schedule*/
       }
   }
   else
   {
       if (is_pps)
       {
           p_shp_param->pbs = p_shp_param->pbs * 1000; /* one packet convers to 1000B */
       }
       else
       {
           p_shp_param->pbs = p_shp_param->pbs * 125; /* 1000/8 */
       }
   }

   /*for queue cnt overturn*/
   CTC_ERROR_RETURN(_sys_godengate_qos_shape_param_check(lchip, is_pps, bucket_rate, p_shp_param->pir, &p_shp_param->pbs,
                                                         p_gg_queue_master[lchip]->que_shp_update_freq));

   /*compute max_bucket_thrd*/
   sys_goldengate_qos_map_token_thrd_hw_to_user(lchip, &max_bucket_thrd, (0xFF << SYS_SHAPE_TOKEN_THRD_SHIFTS_WIDTH | 0xF), SYS_SHAPE_TOKEN_THRD_SHIFTS_WIDTH);

   /*bucket_thrd can not exceed max value*/
   if (p_shp_param->pbs > max_bucket_thrd)
   {
       p_shp_param->pbs = max_bucket_thrd;
   }

   /* convert to internal token thrd */
   CTC_ERROR_RETURN(sys_goldengate_qos_map_token_thrd_user_to_hw(lchip, p_shp_param->pbs,
                                                                 &bucket_thrd,
                                                                 SYS_SHAPE_TOKEN_THRD_SHIFTS_WIDTH,
                                                                 SYS_SHAPE_MAX_TOKEN_THRD));

   p_sys_profile_old = p_sys_queue->p_shp_profile;
   p_profile_pool = p_gg_queue_master[lchip]->p_queue_profile_pool[slice_id];

    p_sys_profile_new = mem_malloc(MEM_QUEUE_MODULE, sizeof(sys_queue_shp_profile_t));
    if (NULL == p_sys_profile_new)
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_sys_profile_new, 0, sizeof(sys_queue_shp_profile_t));

    p_sys_profile_new->bucket_rate = bucket_rate;
    p_sys_profile_new->bucket_thrd = bucket_thrd;

    /*reserve queue shape profile for cpu queue*/
    if ((p_shp_param->queue.queue_type == CTC_QUEUE_TYPE_EXCP_CPU) && (p_gg_queue_master[lchip]->cpu_que_shp_prof_num))
    {
        p_sys_profile_new->is_cpu_que_prof = 1;
    }

    SYS_QUEUE_DBG_INFO("p_sys_profile_new->bucket_rate:%d\n", p_sys_profile_new->bucket_rate);
    SYS_QUEUE_DBG_INFO("p_sys_profile_new->bucket_thrd:%d\n", p_sys_profile_new->bucket_thrd);

    /*if use new date to replace old data, profile_id will not change*/
    if (p_sys_profile_old)
    {
        if (TRUE == _sys_goldengate_queue_shp_hash_cmp_queue_shape_profile(p_sys_profile_old, p_sys_profile_new))
        {
            mem_free(p_sys_profile_new);
            return CTC_E_NONE;
        }
        SYS_QUEUE_DBG_INFO("update old profile !!!!!!!!!!!!\n");
        old_profile_id = p_sys_profile_old->profile_id;
        p_sys_profile_new->profile_id = old_profile_id;
    }

    ret = ctc_spool_add(p_profile_pool,
                           p_sys_profile_new,
                           p_sys_profile_old,
                           &p_sys_profile_get);


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
            ret = _sys_goldengate_queue_shp_alloc_offset_queue_shape_profile(lchip, slice_id, p_sys_profile_new->is_cpu_que_prof, &profile_id);
            if (ret < 0)
            {
                ctc_spool_remove(p_profile_pool, p_sys_profile_new, NULL);
                mem_free(p_sys_profile_new);
                goto ERROR_RETURN;
            }

            p_sys_profile_get->profile_id = profile_id;
            /*write policer prof*/
            SYS_QUEUE_DBG_INFO("Need new profile:%d\n", profile_id);
            CTC_ERROR_RETURN(_sys_goldengate_queue_shp_add_queue_shape_profile_to_asic(lchip, slice_id, p_sys_profile_new));
        }

    }

    /*key is found, so there is an old ad need to be deleted.*/
    /*if profile_id not change, no need to delete old ad(old ad is replaced by new)*/
    if (p_sys_profile_old && p_sys_profile_get->profile_id != old_profile_id)
    {
        CTC_ERROR_RETURN(_sys_goldengate_queue_shp_remove_queue_shape_profile(lchip, slice_id, p_sys_profile_old));
    }

    p_sys_queue->p_shp_profile = p_sys_profile_get;

    return CTC_E_NONE;

ERROR_RETURN:
    return ret;

}

int32
sys_goldengate_queue_shp_set_queue_shape_enable(uint8 lchip, uint16 queue_id,
                                           ctc_qos_shape_queue_t* p_shape)
{
    sys_queue_node_t* p_sys_queue_node = NULL;
    int32 ret = 0;
    uint8  slice_id = 0;

    SYS_QUEUE_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_shape);
    CTC_MAX_VALUE_CHECK(p_shape->pir, SYS_MAX_SHAPE_RATE);
    CTC_MAX_VALUE_CHECK(p_shape->cir, SYS_MAX_SHAPE_RATE);

    p_sys_queue_node = ctc_vector_get(p_gg_queue_master[lchip]->queue_vec,queue_id);
    if (NULL == p_sys_queue_node)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    slice_id =  SYS_MAP_QUEUEID_TO_SLICE(queue_id);

    SYS_QUEUE_DBG_INFO("queue_id:%d\n", queue_id);

    CTC_ERROR_RETURN(sys_goldengate_queue_shp_update_group_shape(lchip, queue_id/4,
                                            p_shape->cir,
                                            p_shape->cbs,
                                            p_shape->queue.queue_id % 8,
                                            0,
                                            1));

    /*add queue shape prof*/
    CTC_ERROR_RETURN(_sys_goldengate_queue_shp_add_queue_shape_profile(lchip, slice_id,
                                                 p_shape,
                                                 p_sys_queue_node));

    CTC_ERROR_RETURN(_sys_goldengate_queue_shp_add_queue_shape_to_asic(lchip, slice_id,
                                                 p_sys_queue_node));

    p_sys_queue_node->shp_en = 1;

    return ret;
}

/**
 @brief Unset shaping for the given queue in a chip.
*/
int32
sys_goldengate_queue_shp_set_queue_shape_disable(uint8 lchip, uint16 queue_id,
                                                                    ctc_qos_shape_queue_t* p_shape)
{
    int32 ret = 0;
    uint8  slice_id = 0;
    sys_queue_node_t* p_sys_queue_node = NULL;

    p_sys_queue_node = ctc_vector_get(p_gg_queue_master[lchip]->queue_vec,queue_id);
    if (NULL == p_sys_queue_node)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

	CTC_ERROR_RETURN(_sys_goldengate_queue_shp_remove_queue_shape_from_asic(lchip, p_sys_queue_node));

    slice_id =  SYS_MAP_QUEUEID_TO_SLICE(queue_id);

    if (NULL == p_sys_queue_node->p_shp_profile)
    {
        return CTC_E_NONE;
    }

    CTC_ERROR_RETURN(_sys_goldengate_queue_shp_remove_queue_shape_profile(lchip, slice_id, p_sys_queue_node->p_shp_profile));


    CTC_ERROR_RETURN(sys_goldengate_queue_shp_update_group_shape(lchip, queue_id/4,
                                            0,
                                            0,
                                            p_shape->queue.queue_id % 8,
                                            0,
                                            0));
    p_sys_queue_node->shp_en = 0;
    p_sys_queue_node->p_shp_profile = NULL;

    return ret;
}

int32
sys_goldengate_queue_shp_set_queue_shape(uint8 lchip, ctc_qos_shape_queue_t* p_shape)
{
    uint16 queue_id = 0;


    /*get queue_id*/

    SYS_QUEUE_DBG_PARAM("p_shape->queue_id = %d\n", p_shape->queue.queue_id);
    SYS_QUEUE_DBG_PARAM("p_shape->queue_type = %d\n", p_shape->queue.queue_type);
    SYS_QUEUE_DBG_PARAM("p_shape->enable = %d\n", p_shape->enable);
    SYS_QUEUE_DBG_PARAM("p_shape->gport = %d\n", p_shape->queue.gport);
    SYS_QUEUE_DBG_PARAM("p_shape->pir = %d\n", p_shape->pir);
    SYS_QUEUE_DBG_PARAM("p_shape->cir = %d\n", p_shape->cir);

    CTC_ERROR_RETURN(sys_goldengate_queue_get_queue_id(lchip,  &p_shape->queue, &queue_id));

    if (p_shape->enable)
    {
        CTC_ERROR_RETURN(sys_goldengate_queue_shp_set_queue_shape_enable(lchip, queue_id, p_shape));
    }
    else
    {
        CTC_ERROR_RETURN(sys_goldengate_queue_shp_set_queue_shape_disable(lchip, queue_id, p_shape));
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_queue_shp_init_queue_shape(uint8 lchip, uint8 slice_id)
{

    DsQueShpProfile_m ds_profile;
    QMgrQueShapeCtl_m que_shape_ctl;
    uint32 cmd;
    uint32 tmp;

    sal_memset(&ds_profile, 0, sizeof(DsQueShpProfile_m));

    /* reserved queue shape profile 0 for max*/
    SetDsQueShpProfile(V,tokenRateFrac_f,&ds_profile,SYS_MAX_TOKEN_RATE_FRAC);
    SetDsQueShpProfile(V,tokenRate_f,&ds_profile,SYS_MAX_TOKEN_RATE);
    SetDsQueShpProfile(V,tokenThrdShift_f,&ds_profile,SYS_MAX_TOKEN_RATE_FRAC);
    SetDsQueShpProfile(V,tokenThrd_f,&ds_profile,SYS_MAX_TOKEN_THRD);
    cmd = DRV_IOW(DsQueShpProfile_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip,(slice_id *SYS_MAX_QUEUE_SHAPE_PROFILE_NUM ) + 0, cmd, &ds_profile));

	/* reserved queue shape profile 1 for drop*/
    SetDsQueShpProfile(V,tokenRateFrac_f,&ds_profile,SYS_MAX_TOKEN_RATE_FRAC);
    SetDsQueShpProfile(V,tokenRate_f,&ds_profile,SYS_MAX_TOKEN_RATE);
    SetDsQueShpProfile(V,tokenThrdShift_f,&ds_profile,SYS_MAX_TOKEN_RATE_FRAC);
    SetDsQueShpProfile(V,tokenThrd_f,&ds_profile,SYS_MAX_TOKEN_THRD);
    cmd = DRV_IOW(DsQueShpProfile_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, (slice_id *SYS_MAX_QUEUE_SHAPE_PROFILE_NUM ) +1, cmd, &ds_profile));

    /* init queue shape update timer*/
    sal_memset(&que_shape_ctl, 0, sizeof(QMgrQueShapeCtl_m));
    SetQMgrQueShapeCtl(V,queShpGlbEn_f,&que_shape_ctl,1);
    SetQMgrQueShapeCtl(V,queShpMaxPtr_f,&que_shape_ctl,2047);
    SetQMgrQueShapeCtl(V,queShpMinPtr_f,&que_shape_ctl,0);
    SetQMgrQueShapeCtl(V,queRefPulseMaxCnt_f,&que_shape_ctl,0);
    SetQMgrQueShapeCtl(V,queShpUpdMaxCnt_f,&que_shape_ctl,1);
    SetQMgrQueShapeCtl(V,queShpUpdateEn_f,&que_shape_ctl,1);
    cmd = DRV_IOW(QMgrQueShapeCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, slice_id, cmd, &que_shape_ctl));

    tmp = 0;
    cmd = DRV_IOW(QMgrNetPktAdj_t, QMgrNetPktAdj_netPktAdjVal0_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));
    tmp = 0;
    cmd = DRV_IOW(QMgrNetPktAdj_t, QMgrNetPktAdj_netPktAdjVal1_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));
    tmp = 0;
    cmd = DRV_IOW(QMgrNetPktAdj_t, QMgrNetPktAdj_netPktAdjVal2_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));
    tmp = 0;
    cmd = DRV_IOW(QMgrNetPktAdj_t, QMgrNetPktAdj_netPktAdjVal3_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));

    return CTC_E_NONE;
}


#define global_shaping_enable ""


/**
 @brief Globally set shape mode.
  mode 0: chan_shp_update_freq = que_shp_update_freq
  mode 1: chan_shp_update_freq = 16*que_shp_update_freq
*/
int32
sys_goldengate_qos_set_shape_mode(uint8 lchip, uint8 mode)
{

    uint32 cmd;
    uint32 field_value;

    SYS_QOS_QUEUE_INIT_CHECK(lchip);
    CTC_MAX_VALUE_CHECK(mode, 1);

    if (mode)
    {
        SYS_QOS_QUEUE_LOCK(lchip);
        p_gg_queue_master[lchip]->chan_shp_update_freq = 4096000; /*granularity:128kbps*/
        SYS_QOS_QUEUE_UNLOCK(lchip);

        /*Freq: 4.096MHz, Peroid:0.244140625 us
        RefCoreFreq: 156.25, DivFactor: 38.146972 = > Cfg: 0x2525*/
        field_value = 0x2525;
        cmd = DRV_IOW(RefDivQMgrDeqShpPulse_t, RefDivQMgrDeqShpPulse_cfgRefDivQMgrDeqShpPulse_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

        field_value = 15;
        cmd = DRV_IOW(QMgrQueShapeCtl_t, QMgrQueShapeCtl_queRefPulseMaxCnt_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 1, cmd, &field_value));
    }
    else
    {
        SYS_QOS_QUEUE_LOCK(lchip);
        p_gg_queue_master[lchip]->chan_shp_update_freq = 256000; /*granularity:8kbps*/
        SYS_QOS_QUEUE_UNLOCK(lchip);

        /*Freq: 256KHz,
        RefCoreFreq: 156.25, DivFactor: 610.3515625 = > Cfg: 0x2615a*/
        field_value = 0x2615a;
        cmd = DRV_IOW(RefDivQMgrDeqShpPulse_t, RefDivQMgrDeqShpPulse_cfgRefDivQMgrDeqShpPulse_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

        field_value = 0;
        cmd = DRV_IOW(QMgrQueShapeCtl_t, QMgrQueShapeCtl_queRefPulseMaxCnt_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 1, cmd, &field_value));
    }

    return CTC_E_NONE;
}


/**
 @brief Globally enable/disable channel shaping function.
*/
int32
sys_goldengate_qos_set_port_shape_enable(uint8 lchip, uint8 enable)
{

    uint8  slice_id;
    uint32 cmd;
    uint32 tmp;

    SYS_QUEUE_DBG_FUNC();
    tmp = enable;


    for (slice_id = 0; slice_id < SYS_MAX_LOCAL_SLICE_NUM; slice_id++)
    {
        cmd = DRV_IOW(QMgrChanShapeCtl_t, QMgrChanShapeCtl_chanShpGlbEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, slice_id, cmd, &tmp));

        cmd = DRV_IOW(QMgrChanShapeCtl_t, QMgrChanShapeCtl_chanShpUpdEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, slice_id, cmd, &tmp));

        SYS_QUEUE_DBG_INFO("sys_shape_ctl.channel_shape_enable = %d\n", enable);
    }


    return CTC_E_NONE;
}

/**
 @brief Get channel shape global enable stauts.
*/
int32
sys_goldengate_qos_get_port_shape_enable(uint8 lchip, uint8* p_enable)
{

    uint32 cmd;
    uint32 tmp;

    SYS_QUEUE_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_enable);

    cmd = DRV_IOR(QMgrChanShapeCtl_t, QMgrChanShapeCtl_chanShpGlbEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));

    *p_enable = tmp;

    SYS_QUEUE_DBG_INFO("sys_shape_ctl.channel_shape_enable = %d\n", *p_enable);

    return CTC_E_NONE;
}

int32
sys_goldengate_qos_set_queue_shape_enable(uint8 lchip, uint8 enable)
{

    uint8  slice_id;
    uint32 tmp;
    uint32 cmd;

    SYS_QUEUE_DBG_FUNC();

    tmp = enable ? 1 : 0;


    for (slice_id = 0; slice_id < SYS_MAX_LOCAL_SLICE_NUM; slice_id++)
    {
        cmd = DRV_IOW(QMgrQueShapeCtl_t, QMgrQueShapeCtl_queShpGlbEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, slice_id, cmd, &tmp));

        cmd = DRV_IOW(QMgrQueShapeCtl_t, QMgrQueShapeCtl_queShpUpdateEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, slice_id, cmd, &tmp));

    }

    SYS_QUEUE_DBG_INFO("sys_shape_ctl.queue_shape_enable = %d\n", enable);

    return CTC_E_NONE;
}

/**
 @brief Get queue shape global enable status.
*/
int32
sys_goldengate_qos_get_queue_shape_enable(uint8 lchip, uint8* p_enable)
{
    uint32 cmd;
    uint32 tmp;

    SYS_QUEUE_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_enable);

    cmd = DRV_IOR(QMgrQueShapeCtl_t, QMgrQueShapeCtl_queShpGlbEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));

    *p_enable = tmp;

    SYS_QUEUE_DBG_INFO("sys_shape_ctl.queue_shape_enable = %d\n", *p_enable);

    return CTC_E_NONE;
}

/**
 @brief Globally enable/disable group shaping function.
*/
int32
sys_goldengate_qos_set_group_shape_enable(uint8 lchip, uint8 enable)
{

    uint8  slice_id;
    uint32 tmp;
    uint32 cmd;

    SYS_QUEUE_DBG_FUNC();

    tmp = enable ? 1 : 0;


    for (slice_id = 0; slice_id < SYS_MAX_LOCAL_SLICE_NUM; slice_id++)
    {
        cmd = DRV_IOW(QMgrGrpShapeCtl_t, QMgrGrpShapeCtl_grpShpGlbEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, slice_id, cmd, &tmp));
    }

    SYS_QUEUE_DBG_INFO("sys_shape_ctl.queue_shape_enable = %d\n", enable);


    return CTC_E_NONE;
}

/**
 @brief Get group shape global enable status.
*/
int32
sys_goldengate_qos_get_group_shape_enable(uint8 lchip, uint8* p_enable)
{
    uint32 cmd;
    uint32 tmp;

    SYS_QUEUE_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_enable);

    cmd = DRV_IOR(QMgrGrpShapeCtl_t, QMgrGrpShapeCtl_grpShpGlbEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));

    *p_enable = tmp;

    SYS_QUEUE_DBG_INFO("queue_shape_enable = %d\n", *p_enable);

    return CTC_E_NONE;
}

int32
sys_goldengate_qos_set_shape_ipg_enable(uint8 lchip, uint8 enable)
{
    uint32 cmd = 0;
	uint32 field_val = enable ? CTC_DEFAULT_IPG : 0;

    SYS_QUEUE_DBG_FUNC();

    cmd = DRV_IOW(QMgrNetPktAdj_t, QMgrNetPktAdj_netPktAdjVal0_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    return CTC_E_NONE;
}


int32
sys_goldengate_qos_set_reason_shp_base_pkt_en(uint8 lchip, uint8 enable)
{
    uint32 cmd = 0;
    uint32 field_val = 0;
    uint32 channelid[4] = {0};
    uint8  channel = 0;

    SYS_QUEUE_DBG_FUNC();

    field_val = enable? 1 : 0;

    cmd = DRV_IOW(QMgrEnqCtl_t, QMgrEnqCtl_cpuBasedOnPktNum_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    SYS_QOS_QUEUE_LOCK(lchip);
    p_gg_queue_master[lchip]->shp_pps_en = enable;

    /* channel always enable */
    if (p_gg_queue_master[lchip]->have_lcpu_by_eth)
    {
        channel = p_gg_queue_master[lchip]->cpu_eth_chan_id;
        CTC_BIT_SET(channelid[channel / 32], channel % 32);
    }
    SYS_QOS_QUEUE_UNLOCK(lchip);
    channel = SYS_DMA_CHANNEL_ID;
    CTC_BIT_SET(channelid[channel/32], channel%32);

    channel = SYS_DMA_CHANNEL_RX1;
    CTC_BIT_SET(channelid[channel/32], channel%32);

    channel = SYS_DMA_CHANNEL_RX2;
    CTC_BIT_SET(channelid[channel/32], channel%32);

    channel = SYS_DMA_CHANNEL_RX3;
    CTC_BIT_SET(channelid[channel/32], channel%32);

    channel = SYS_DMA_CHANNEL_ID+64;
    CTC_BIT_SET(channelid[channel/32], channel%32);

    channel = SYS_DMA_CHANNEL_RX1+64;
    CTC_BIT_SET(channelid[channel/32], channel%32);

    channel = SYS_DMA_CHANNEL_RX2+64;
    CTC_BIT_SET(channelid[channel/32], channel%32);

    channel = SYS_DMA_CHANNEL_RX3+64;
    CTC_BIT_SET(channelid[channel/32], channel%32);

    cmd = DRV_IOW(QMgrEnqChanIdCfg_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &channelid));

    return CTC_E_NONE;
}

int32
sys_goldengate_qos_set_shp_remark_color_en(uint8 lchip, uint8 enable)
{

    uint8  slice_id;
    uint32 cmd = 0;
    uint32 field_val = 0;
    uint8 priority = 0;
    uint8 in_profile = 0;
    uint8 index = 0;

    SYS_QUEUE_DBG_FUNC();

    field_val = enable? 1 : 0;


    for (slice_id = 0; slice_id < SYS_MAX_LOCAL_SLICE_NUM; slice_id++)
    {

        for (priority = 0; priority <= SYS_QOS_CLASS_PRIORITY_MAX; priority++)
        {
            /*In profiel (CIR)*/
            in_profile = 1;
            index = ((priority << 1) | in_profile ) + slice_id * 128;
            field_val =
            cmd = DRV_IOW(DsBufRetrvColorMap_t, DsBufRetrvColorMap_color_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &field_val));

            /*Out profiel (PIR)*/
            in_profile = 0;
            index = ((priority << 1) | in_profile ) + slice_id * 128;
            cmd = DRV_IOW(DsBufRetrvColorMap_t, DsBufRetrvColorMap_color_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &field_val));


        }
    }

    return CTC_E_NONE;
}
STATIC int32 _sys_godengate_qos_mux_port(uint8 lchip, uint32 gport)
{
    uint8 channel = 0xFF;
    uint32 cmd = 0;
    uint32 field_val =0;

    sys_goldengate_get_channel_by_port(lchip, (uint16)gport, &channel);

    cmd = DRV_IOR(IpePhyPortMuxCtl_t, IpePhyPortMuxCtl_muxType_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel, cmd, &field_val));
    /*SYS_STK_MUX_TYPE_HDR_REGULAR_PORT     = 0,  4'b0000: Regular port(No MUX)
      SYS_STK_MUX_TYPE_HDR_WITHOUT_TUNNEL   = 6,   4'b0110: BridgeHeader without tunnel
      #define MUXTYPE_MUXDEMUX                         "1"
      #define MUXTYPE_EVB                              "2"
      #define MUXTYPE_CB_DOWNLINK                      "3"
      #define MUXTYPE_PE_DOWNLINK_WITH_CASCADE_PORT    "4"
      #define MUXTYPE_PE_UPLINK                        "5"*/
    if (field_val >= 5 || field_val == 0)
    {
        return 0;
    }
    else
    {
        return 1;
    }
}


#define shp_api ""

int32
_sys_goldengate_qos_set_shape(uint8 lchip, ctc_qos_shape_t* p_shape)
{
    uint8 b_channel = 0;

    CTC_PTR_VALID_CHECK(p_shape);

    SYS_QOS_QUEUE_LOCK(lchip);
    if ((CTC_QOS_SHAPE_PORT == p_shape->type)
        || (CTC_QOS_SHAPE_GROUP == p_shape->type))
    {
        uint32 gport = 0;
        gport = (CTC_QOS_SHAPE_PORT == p_shape->type) ? p_shape->shape.port_shape.gport : p_shape->shape.group_shape.gport;

        if (CTC_IS_CPU_PORT(gport))
        {
            b_channel  = 1;
        }
        else
        {
            if ((4 == p_gg_queue_master[lchip]->queue_num_per_chanel)
                || ((8 == p_gg_queue_master[lchip]->queue_num_per_chanel)
                    && _sys_godengate_qos_mux_port(lchip, gport)))
            {
                b_channel = 0;
            }
            else
            {
                b_channel = 1;
            }
        }

    }

    switch (p_shape->type)
    {
    case CTC_QOS_SHAPE_PORT:
        if (0 == b_channel)
        {
            ctc_qos_shape_group_t group_shape;
            sal_memset(&group_shape, 0, sizeof(ctc_qos_shape_group_t));

            group_shape.enable      = p_shape->shape.port_shape.enable;
            group_shape.gport       = p_shape->shape.port_shape.gport;
            group_shape.pbs         = p_shape->shape.port_shape.pbs;
            group_shape.pir         = p_shape->shape.port_shape.pir;
            group_shape.queue.gport         = p_shape->shape.port_shape.gport;
            group_shape.queue.queue_id      = 0;
            group_shape.queue.queue_type    = CTC_QUEUE_TYPE_NETWORK_EGRESS;

            CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(
                sys_goldengate_queue_shp_set_group_shape(lchip, &group_shape));
        }
        else
        {
            CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(
                sys_goldengate_queue_shp_set_channel_shape(lchip, &p_shape->shape.port_shape));
        }
        break;

    case CTC_QOS_SHAPE_QUEUE:
        CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(
            sys_goldengate_queue_shp_set_queue_shape(lchip, &p_shape->shape.queue_shape));
        break;

    case CTC_QOS_SHAPE_GROUP:
        if (0 == b_channel)
        {
            return CTC_E_NOT_SUPPORT;
        }
        else
        {
            CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(
                sys_goldengate_queue_shp_set_group_shape(lchip, &p_shape->shape.group_shape));
        }
        break;

    default:
        SYS_QOS_QUEUE_UNLOCK(lchip);
        return CTC_E_INVALID_PARAM;
    }
    SYS_QOS_QUEUE_UNLOCK(lchip);
    return CTC_E_NONE;
}

int32
sys_goldengate_qos_shape_dump_status(uint8 lchip)
{
    uint32 cmd = 0;
    uint32 value1 = 0;

	SYS_QUEUE_DBG_DUMP("---------------------------Shape--------------------------\n");
    SYS_QUEUE_DBG_DUMP("%-30s: %d\n","Shape mode for To LCPU", p_gg_queue_master[lchip]->shp_pps_en);
    cmd = DRV_IOR(IpeIpgCtl_t, IpeIpgCtl_array_0_ipg_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value1));
    SYS_QUEUE_DBG_DUMP("%-30s: %d\n", "Shape IPG enable", (value1 != 0));
    SYS_QUEUE_DBG_DUMP("%-30s: %d\n", "IPG Size", value1);
    SYS_QUEUE_DBG_DUMP("%-30s: %d(per slice)\n", "Total Queue shape profile", SYS_MAX_QUEUE_SHAPE_PROFILE_NUM);

    SYS_QUEUE_DBG_DUMP("%-30s: %d\n", "--Used count[slice0]", p_gg_queue_master[lchip]->p_queue_profile_pool[0]->count + 2);
    SYS_QUEUE_DBG_DUMP("%-30s: %d\n", "--Used count[slice1]", p_gg_queue_master[lchip]->p_queue_profile_pool[1]->count + 2);

    SYS_QUEUE_DBG_DUMP("%-30s: %d(per slice)\n", "Total Group shape profile", SYS_MAX_GROUP_SHAPE_PROFILE_NUM);
    SYS_QUEUE_DBG_DUMP("%-30s: %d\n", "--Used count[slice0]",  p_gg_queue_master[lchip]->p_group_profile_pool[0]->count + 1);
    SYS_QUEUE_DBG_DUMP("%-30s: %d\n", "--Used count[slice1]",  p_gg_queue_master[lchip]->p_group_profile_pool[1]->count + 1);
    SYS_QUEUE_DBG_DUMP("\n");

    return CTC_E_NONE;

}

/**
 @brief Queue shaper initialization.
*/
int32
sys_goldengate_queue_shape_init(uint8 lchip, void* p_glb_parm)
{
    sys_goldengate_opf_t opf;
    uint8  slice_id = 0;
    uint8  opf_pool_num = 0;
    ctc_spool_t spool;
    ctc_qos_global_cfg_t * p_glb_cfg = NULL;
    ctc_chip_device_info_t device_info;
    uint32 field_value = 0;
    uint32 cmd = 0;
	sys_qos_rate_granularity_t shp_gran[SYS_SHP_GRAN_RANAGE_NUM] = {{ 2,        8} ,
                                                                    {100,      32},
                                                                    {1000,     64},
                                                                    {10000,    512},
     	                                                            {40000,    1024},
     		                                                        {100000,   2048}};


    p_glb_cfg = (ctc_qos_global_cfg_t *)p_glb_parm;
    p_glb_cfg->cpu_queue_shape_profile_num = ((p_glb_cfg->cpu_queue_shape_profile_num) > (SYS_MAX_QUEUE_SHAPE_PROFILE_NUM -2))
                                              ? (SYS_MAX_QUEUE_SHAPE_PROFILE_NUM -2) : (p_glb_cfg->cpu_queue_shape_profile_num);

    /*add pool index for cpu queue profile*/
    opf_pool_num = (p_glb_cfg->cpu_queue_shape_profile_num) ? (SYS_MAX_LOCAL_SLICE_NUM + 1) : SYS_MAX_LOCAL_SLICE_NUM;
    CTC_ERROR_RETURN(sys_goldengate_opf_init(lchip, OPF_QUEUE_SHAPE_PROFILE, opf_pool_num));
    CTC_ERROR_RETURN(sys_goldengate_opf_init(lchip, OPF_GROUP_SHAPE_PROFILE, SYS_MAX_LOCAL_SLICE_NUM));

    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));

    for (slice_id = 0; slice_id < SYS_MAX_LOCAL_SLICE_NUM; slice_id++)
    {
        /* init queue shape hash table */
        sal_memset(&spool, 0, sizeof(ctc_spool_t));
        spool.lchip = lchip;
        spool.block_num = 1;
        spool.block_size = SYS_QUEUE_SHAPE_PROFILE_BLOCK_SIZE;
        spool.max_count = SYS_MAX_QUEUE_SHAPE_PROFILE_NUM;
        spool.user_data_size = sizeof(sys_queue_shp_profile_t);
        spool.spool_key = (hash_key_fn)_sys_goldengate_queue_shp_hash_make_queue_shape_profile;
        spool.spool_cmp = (hash_cmp_fn)_sys_goldengate_queue_shp_hash_cmp_queue_shape_profile;
        p_gg_queue_master[lchip]->p_queue_profile_pool[slice_id] = ctc_spool_create(&spool);

        /* init queue shape offset pool */
        opf.pool_type = OPF_QUEUE_SHAPE_PROFILE;
        opf.pool_index = slice_id;
        /* 0/1 reserved for default profile*/
        if (opf.pool_index == 0)
        {
            CTC_ERROR_RETURN(sys_goldengate_opf_init_offset(lchip, &opf, 2 + p_glb_cfg->cpu_queue_shape_profile_num,
                                                            SYS_MAX_QUEUE_SHAPE_PROFILE_NUM - p_glb_cfg->cpu_queue_shape_profile_num - 2));
            /*reserved for cpu queue profile*/
            if (p_glb_cfg->cpu_queue_shape_profile_num)
            {
                p_gg_queue_master[lchip]->cpu_que_shp_prof_num = p_glb_cfg->cpu_queue_shape_profile_num;
                opf.pool_index = 2;
                CTC_ERROR_RETURN(sys_goldengate_opf_init_offset(lchip, &opf, 2, p_glb_cfg->cpu_queue_shape_profile_num));
            }
        }
        else
        {
            CTC_ERROR_RETURN(sys_goldengate_opf_init_offset(lchip, &opf, 2, SYS_MAX_QUEUE_SHAPE_PROFILE_NUM - 2));
        }


        /* init group shape hash table */
        sal_memset(&spool, 0, sizeof(ctc_spool_t));
        spool.lchip = lchip;
        spool.block_num = 1;
        spool.block_size = SYS_GROUP_SHAPE_PROFILE_BLOCK_SIZE;
        spool.max_count = SYS_MAX_GROUP_SHAPE_PROFILE_NUM;
        spool.user_data_size = sizeof(sys_group_shp_profile_t);
        spool.spool_key = (hash_key_fn)_sys_goldengate_queue_shp_hash_make_group_shape_profile;
        spool.spool_cmp = (hash_cmp_fn)_sys_goldengate_queue_shp_hash_cmp_group_shape_profile;
        p_gg_queue_master[lchip]->p_group_profile_pool[slice_id] = ctc_spool_create(&spool);


        /* init group shape offset pool */
        opf.pool_type = OPF_GROUP_SHAPE_PROFILE;
        opf.pool_index = slice_id;
		/* 0 reserved for default profile*/
        CTC_ERROR_RETURN(sys_goldengate_opf_init_offset(lchip, &opf, 1, SYS_MAX_GROUP_SHAPE_PROFILE_NUM-1));

        p_gg_queue_master[lchip]->que_shp_update_freq = 256000; /*granularity:8kbps*/
        p_gg_queue_master[lchip]->chan_shp_update_freq = 256000; /*granularity:8kbps*/
        p_gg_queue_master[lchip]->shp_pps_en = 1;
         /*p_gg_queue_master[lchip]->chan_shp_update_freq = 4096000; //granularity:128kbps*/
	    sal_memcpy(p_gg_queue_master[lchip]->que_shp_gran,shp_gran,
	  	         SYS_SHP_GRAN_RANAGE_NUM * sizeof(sys_qos_rate_granularity_t));

        CTC_ERROR_RETURN(_sys_goldengate_queue_shp_init_queue_shape(lchip, slice_id));
        CTC_ERROR_RETURN(_sys_goldengate_queue_shp_init_group_shape(lchip, slice_id));
        CTC_ERROR_RETURN(_sys_goldengate_queue_shp_init_channel_shape(lchip, slice_id));
    }


    CTC_ERROR_RETURN(sys_goldengate_qos_set_reason_shp_base_pkt_en(lchip, TRUE));

    sal_memset(&device_info, 0, sizeof(ctc_chip_device_info_t));
    sys_goldengate_chip_get_device_info(lchip, &device_info);
    /*for queue cnt overturn*/
    if (device_info.version_id <= 1)
    {
        p_gg_queue_master[lchip]->chan_shp_update_freq = 32000;
        p_gg_queue_master[lchip]->que_shp_update_freq = 2000;

        field_value = 0x1311d0;
        cmd = DRV_IOW(RefDivQMgrDeqShpPulse_t, RefDivQMgrDeqShpPulse_cfgRefDivQMgrDeqShpPulse_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

        field_value = 15;
        cmd = DRV_IOW(QMgrQueShapeCtl_t, QMgrQueShapeCtl_queRefPulseMaxCnt_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 1, cmd, &field_value));
    }

    return CTC_E_NONE;
}


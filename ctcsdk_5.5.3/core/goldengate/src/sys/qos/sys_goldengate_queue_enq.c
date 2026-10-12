/**
 @file sys_goldengate_queue_enq.c

 @date 2010-01-13

 @version v2.0

*/

/****************************************************************************
 *  Default Queue Mode
 *
 *           type          port id    QSelType     queue number    Enqueue Param
 *     ----------------  ----------   ---------   ------------    --------------
 *      network egress     0 - 47      0            16 * 48       Based on Channel Id
 *      drop port          248         2            8             Based on Channel Id
 *      RX OAM             249         4            8             Based on RxOamType
 *      Reserved port      248 - 255   2            4*8           Based on Channel Id
        exception to cpu   49 (DMA)    1            128           Based on SubQueId
        exception to cpu   50 (ETH)    1            16            Based on SubQueId(share network egress queue)
*********************************************************************************/

/****************************************************************************
 *
 * Header Files
 *
 ****************************************************************************/

#include "ctc_debug.h"
#include "ctc_const.h"
#include "ctc_error.h"
#include "ctc_macro.h"
#include "ctc_vector.h"
#include "ctc_linklist.h"
#include "ctc_qos.h"

#include "sys_goldengate_chip.h"
#include "sys_goldengate_opf.h"
#include "sys_goldengate_internal_port.h"
#include "sys_goldengate_stats.h"
#include "sys_goldengate_dma.h"

#include "sys_goldengate_queue_enq.h"
#include "sys_goldengate_queue_sch.h"
#include "sys_goldengate_queue_drop.h"
#include "sys_goldengate_cpu_reason.h"
#include "sys_goldengate_nexthop_api.h"
#include "sys_goldengate_nexthop.h"
#include "goldengate/include/drv_enum.h"
#include "goldengate/include/drv_io.h"

/****************************************************************************
 *
 * Global and Declaration
 *
 ****************************************************************************/

#define SYS_QUEUE_VEC_BLOCK_SIZE 64
#define SYS_QUEUE_VEC_BLOCK_NUM (SYS_MAX_QUEUE_NUM / SYS_QUEUE_VEC_BLOCK_SIZE)

#define SYS_QUEUE_GRP_VEC_BLOCK_SIZE 64
#define SYS_QUEUE_GRP_VEC_BLOCK_NUM (SYS_MAX_QUEUE_GROUP_NUM / SYS_QUEUE_GRP_VEC_BLOCK_SIZE)

#define SYS_REASON_VEC_BLOCK_SIZE 8
#define SYS_REASON_VEC_BLOCK_NUM ((CTC_PKT_CPU_REASON_MAX_COUNT + (SYS_REASON_VEC_BLOCK_SIZE - 1)) / SYS_REASON_VEC_BLOCK_SIZE)

#define SYS_QUEUE_HASH_MAX   256
#define SYS_QUEUE_HASH_BLOCK_SIZE 8
#define SYS_QUEUE_HASH_BLOCK_NUM (SYS_QUEUE_HASH_MAX + (SYS_QUEUE_HASH_BLOCK_SIZE - 1) / SYS_QUEUE_HASH_BLOCK_SIZE)

#define SYS_SERVICE_HASH_MAX   1024
#define SYS_SERVICE_HASH_BLOCK_SIZE 8
#define SYS_SERVICE_HASH_BLOCK_NUM (SYS_SERVICE_HASH_MAX + (SYS_SERVICE_HASH_BLOCK_SIZE - 1) / SYS_SERVICE_HASH_BLOCK_SIZE)


#define SYS_REASON_GRP_QUEUE_NUM 8

#define SYS_QUEUE_MAX_GRP_NUM_FOR_CPU_REASON 16
#define SYS_QUEUE_BASE_TYPE_EXCP 808

#define SYS_QUEUE_RSV_QUEUE_NUM ((2 == p_gg_queue_master[lchip]->enq_mode) ? (8 + 8 + 8 + 8) :(8 + 8 + 16 + 8))/*drop = 8, eloop = 8, iloop = 16, oam = 8*/

#define SYS_QOS_ENCODE_GEN_CTL_INDEX(slice,mc,fabric, que_on_chip,src_qsel, dest_chip_match, qsel_type) \
 ( (((slice) & 0x1) << 8) | \
  (((mc) & 0x1) << 7) | \
   (((fabric) & 0x1) <<6) |\
   (((que_on_chip) & 0x1) << 5) |\
   (((src_qsel) & 0x1) << 4) | \
     (((dest_chip_match) & 0x1) << 3) |\
     ((qsel_type) & 0x7) )

#define SYS_QOS_RX_DMA_CHANNEL(excp_group) {\
   if(p_gg_queue_master[lchip]->max_dma_rx_num == 4)\
   {\
      if(excp_group < 4) \
  	  {\
  	     channel = SYS_DMA_CHANNEL_ID;\
  	     start_group = (excp_group == 0);\
  	  }\
      else if (excp_group < 8)\
      {\
          channel = SYS_DMA_CHANNEL_RX1;\
          start_group = (excp_group == 4);\
      }\
      else if (excp_group < 12)\
      {\
         channel = SYS_DMA_CHANNEL_RX2;\
         start_group  = (excp_group == 8);\
      }\
      else\
      {\
         channel = SYS_DMA_CHANNEL_RX3;\
         start_group  = (excp_group == 12);\
      }\
   }\
   else  if(p_gg_queue_master[lchip]->max_dma_rx_num == 2)\
   {\
      if(excp_group < 8) \
  	  {\
  	     channel = SYS_DMA_CHANNEL_ID;\
  	     start_group = (excp_group == 0);\
  	  }\
      else\
      {\
          channel = SYS_DMA_CHANNEL_RX1;\
          start_group = (excp_group == 8);\
      }\
   }\
   else  if(p_gg_queue_master[lchip]->max_dma_rx_num == 1)\
   {\
     channel = SYS_DMA_CHANNEL_ID;\
     start_group = (excp_group == 0);\
   }  \
}

sys_queue_master_t* p_gg_queue_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};

/****************************************************************************
 *
 * Function
 *
 ****************************************************************************/
extern int32
sys_goldengate_queue_get_excp_idx_by_reason(uint8 lchip, uint16 reason_id,
                                            uint16* excp_idx,
                                            uint8* excp_num);
extern int32
sys_goldengate_queue_sch_set_queue_sched(uint8 lchip, ctc_qos_sched_queue_t* p_sched);
extern int32
sys_goldengate_queue_set_chan_drop_en(uint8 lchip, uint16 queue_id, uint8 chan_id, bool enable);

int32
_sys_goldengate_queue_get_qsel_shift(uint8 lchip, uint8 queue_num,
                                    uint8* p_shift)
{
    uint8 i = 0;

    for (i = 0; i < 16; i++)
    {
        if ((1 << i) == queue_num)
        {
            *p_shift = i;
            return CTC_E_NONE;
        }
    }

    return CTC_E_INVALID_PARAM;
}

int32
sys_goldengate_queue_map_gen_ctl(uint8 lchip, DsQueueNumGenCtl_m * p_gen_ctl,
                                 sys_enqueue_info_t *p_info)
{
    uint8 dest_shift_fld = 0;
    uint8 dest_mask_fld = 0;
    uint8 dest_base_fld = 0;
    uint8 shift = 0;
    uint8 shift_num = 0;

    if ((p_info->queue_type == SYS_QUEUE_TYPE_NORMAL)
        || (p_info->queue_type == SYS_QUEUE_TYPE_RSV_PORT)
        || p_info->stacking)
    {
        dest_shift_fld = DsQueueNumGenCtl_channelIdShift_f;
        dest_mask_fld = DsQueueNumGenCtl_channelIdMask_f;
        dest_base_fld = DsQueueNumGenCtl_channelIdBase_f;
    }
    else
    {
        dest_shift_fld = DsQueueNumGenCtl_destQueueShift_f;
        dest_mask_fld = DsQueueNumGenCtl_destQueueMask_f;
        dest_base_fld = DsQueueNumGenCtl_destQueueBase_f;
    }

    /*cos queue number*/
    if (p_info->subq_num == 1 || p_info->subq_num == 4 || p_info->subq_num == 8 || 16 == p_info->subq_num)
    {
        uint8 shift = 0;
        uint8 sel_shift = 0;
        uint8 sel_mask = 0;
        _sys_goldengate_queue_get_qsel_shift(lchip, p_info->subq_num, &shift);
        shift_num = (p_gg_queue_master[lchip]->priority_mode)? 4:6;
        sel_shift = (shift == 0)?0:(shift_num - shift);
        DRV_SET_FIELD_V(DsQueueNumGenCtl_t, DsQueueNumGenCtl_queueSelectShift_f, p_gen_ctl, sel_shift);

        sel_mask = ((p_info->subq_num - 1) << sel_shift);
        DRV_SET_FIELD_V(DsQueueNumGenCtl_t, DsQueueNumGenCtl_queueSelectMask_f, p_gen_ctl, sel_mask);
    }
    else
    {
        return CTC_E_INVALID_PARAM;
    }

    /*dest queue number*/
    if (p_info->queue_num == 1 || p_info->queue_num == 4 || p_info->queue_num == 8 || p_info->queue_num == 16)
    {
        _sys_goldengate_queue_get_qsel_shift(lchip, p_info->queue_num, &shift);
        shift = (shift == 0)?0:(16-shift);
        DRV_SET_FIELD_V(DsQueueNumGenCtl_t, dest_shift_fld, p_gen_ctl, shift);
    }
    else
    {
        return CTC_E_INVALID_PARAM;
    }

    /*dest queue mask*/
    if (p_info->dest_num == 1 || p_info->dest_num == 16 || p_info->dest_num == 32 ||p_info->dest_num == 64 ||p_info->dest_num == 128
        || p_info->dest_num == 256)
    {
        DRV_SET_FIELD_V(DsQueueNumGenCtl_t, dest_mask_fld, p_gen_ctl, p_info->dest_num-1);
    }
    else
    {
        return CTC_E_INVALID_PARAM;
    }

    DRV_SET_FIELD_V(DsQueueNumGenCtl_t, dest_base_fld, p_gen_ctl, p_info->dest_base);
    DRV_SET_FIELD_V(DsQueueNumGenCtl_t, DsQueueNumGenCtl_queueNumBase_f, p_gen_ctl, p_info->queue_base);

     return CTC_E_NONE;
}

int32
_sys_goldengate_queue_init(uint8 lchip)
{
    uint16 channel = 0;
    uint16 super_grp = 0;
    uint32 cmd = 0;
    uint32 field_val = 0;

    field_val = (SYS_MAX_QUEUE_SUPER_GRP_NUM - 4);
    for (channel = 0; channel < (SYS_MAX_CHANNEL_ID*2); channel++)
    {
        cmd = DRV_IOW(RaChanMap_t, RaChanMap_startGrpId_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel, cmd, &field_val));
    }

    field_val = SYS_RSV_CHANNEL_ID;
    for (super_grp = 0; super_grp < SYS_MAX_QUEUE_SUPER_GRP_NUM; super_grp++)
    {
        cmd = DRV_IOW(RaGrpMap_t, RaGrpMap_chanId_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, super_grp, cmd, &field_val));
    }

    return CTC_E_NONE;

}

/*
 get queue id by queue type
*/
 int32
sys_goldengate_queue_get_queue_id(uint8 lchip, ctc_qos_queue_id_t* p_queue,
                                  uint16* queue_id)
{
    uint8 channel = 0;
    uint16 lport = 0;

    SYS_QUEUE_DBG_FUNC();

    switch (p_queue->queue_type)
    {
    case CTC_QUEUE_TYPE_NETWORK_EGRESS:
    case CTC_QUEUE_TYPE_INTERNAL_PORT:
    {
        SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(p_queue->gport, lchip, lport);
        if (p_gg_queue_master[lchip]->group_mode)
        {
            p_queue->queue_id = p_queue->queue_id * 2;
        }
        CTC_MAX_VALUE_CHECK(p_queue->queue_id, (p_gg_queue_master[lchip]->queue_num_per_chanel-1));
        if (SYS_QUEUE_DESTID_ENQ(lchip))
        {
            *queue_id  =  p_gg_queue_master[lchip]->queue_base[SYS_QUEUE_TYPE_EXTENDER_WITH_Q]
                         + ((lport&0xFF) * p_gg_queue_master[lchip]->queue_num_per_chanel)
                         + p_queue->queue_id
                         + ((lport >> 8) ? 1024 :0);
        }
        else
        {
            CTC_ERROR_RETURN(sys_goldengate_get_channel_by_port(lchip, p_queue->gport, &channel));
            *queue_id  =  p_gg_queue_master[lchip]->queue_base[SYS_QUEUE_TYPE_NORMAL]
                         + (((channel>=64)?(channel-64):channel) * p_gg_queue_master[lchip]->queue_num_per_chanel)
                         + p_queue->queue_id
                         + ((lport >> 8) ? 1024 :0);
        }
        SYS_QUEUE_DBG_INFO("channel:%d, lport:%d, queue_id:%d\n", channel, lport, *queue_id);

    }
        break;
    case CTC_QUEUE_TYPE_EXCP_CPU:
    {
        sys_cpu_reason_t  *p_cpu_reason;
        if (p_queue->cpu_reason >= CTC_PKT_CPU_REASON_MAX_COUNT)
        {
            return CTC_E_INVALID_PARAM;
        }
        p_cpu_reason = &p_gg_queue_master[lchip]->cpu_reason[p_queue->cpu_reason];
        if (p_cpu_reason->sub_queue_id == CTC_MAX_UINT16_VALUE)
        {
            return CTC_E_QUEUE_CPU_REASON_NOT_CREATE;
        }
        if (p_cpu_reason->dest_type== CTC_PKT_CPU_REASON_TO_LOCAL_CPU)
        {
            uint8 pcie_sel = 0;
            CTC_ERROR_RETURN(sys_goldengate_chip_get_pcie_select(lchip, &pcie_sel));
            *queue_id  = p_gg_queue_master[lchip]->queue_base[SYS_QUEUE_TYPE_EXCP] + p_cpu_reason->sub_queue_id + (pcie_sel?1024:0);
            if (p_queue->cpu_reason == CTC_PKT_CPU_REASON_C2C_PKT)
            {
                CTC_MAX_VALUE_CHECK(p_queue->queue_id, 2*SYS_REASON_GRP_QUEUE_NUM - 1);
                *queue_id  = *queue_id + p_queue->queue_id;
            }
            else
            {
                CTC_MAX_VALUE_CHECK(p_queue->queue_id, 0);
            }
        }
        else if(p_cpu_reason->dest_type == CTC_PKT_CPU_REASON_TO_LOCAL_CPU_ETH)
        {
            *queue_id  = p_gg_queue_master[lchip]->queue_base[SYS_QUEUE_TYPE_EXCP_BY_ETH]
                + p_cpu_reason->sub_queue_id % p_gg_queue_master[lchip]->queue_num_per_chanel;

        }
        else if(p_cpu_reason->dest_type == CTC_PKT_CPU_REASON_TO_LOCAL_PORT)
        {
            if(SYS_QUEUE_DESTID_ENQ(lchip))
            {
                 lport = p_cpu_reason->dest_port & 0xFF;
                *queue_id  = p_gg_queue_master[lchip]->queue_base[SYS_QUEUE_TYPE_EXTENDER_WITH_Q]
                            + (lport * p_gg_queue_master[lchip]->queue_num_per_chanel)
                            + ((lport >> 8) ? 1024 :0);
            }
            else
            {
                CTC_ERROR_RETURN(sys_goldengate_get_channel_by_port(lchip, p_cpu_reason->dest_port, &channel));
                *queue_id  = p_gg_queue_master[lchip]->queue_base[SYS_QUEUE_TYPE_NORMAL]
                            + (channel * p_gg_queue_master[lchip]->queue_num_per_chanel)
                            + ((channel >> 8) ? 1024 :0);
            }
        }
        else if (p_cpu_reason->dest_type == CTC_PKT_CPU_REASON_TO_DROP)
        {
            CTC_ERROR_RETURN(sys_goldengate_get_channel_by_port(lchip, p_cpu_reason->dest_port, &channel));
            *queue_id  = p_gg_queue_master[lchip]->queue_base[SYS_QUEUE_TYPE_RSV_PORT]
                            + ((channel - SYS_DROP_CHANNEL_ID)* SYS_MIN_QUEUE_NUM_PER_CHAN)
                            + ((channel >> 8) ? 1024 : 0);
        }

        else if(p_cpu_reason->dest_type == CTC_PKT_CPU_REASON_TO_NHID)
        {
            sys_nh_info_dsnh_t nhinfo;
            sal_memset(&nhinfo, 0, sizeof(sys_nh_info_dsnh_t));
            CTC_ERROR_RETURN(sys_goldengate_nh_get_nhinfo(lchip, p_cpu_reason->dest_port, &nhinfo));
            if(nhinfo.aps_en ||
                (nhinfo.nh_entry_type != SYS_NH_TYPE_BRGUC
                    && nhinfo.nh_entry_type != SYS_NH_TYPE_IPUC
                    && nhinfo.nh_entry_type != SYS_NH_TYPE_ILOOP
                    && nhinfo.nh_entry_type != SYS_NH_TYPE_ELOOP))
            {
                return CTC_E_NH_INVALID_NHID;
            }

            lport = nhinfo.dest_id & 0xFF;
            if (SYS_NH_TYPE_ILOOP == nhinfo.nh_entry_type)
            {
                *queue_id = p_gg_queue_master[lchip]->queue_base[SYS_QUEUE_TYPE_RSV_PORT]
                                     + ((SYS_ILOOP_CHANNEL_ID - SYS_DROP_CHANNEL_ID +1 ) * SYS_MIN_QUEUE_NUM_PER_CHAN);
            }
            else if (SYS_NH_TYPE_ELOOP == nhinfo.nh_entry_type)
            {
                *queue_id = p_gg_queue_master[lchip]->queue_base[SYS_QUEUE_TYPE_RSV_PORT]
                                     + ((SYS_ELOOP_CHANNEL_ID - SYS_DROP_CHANNEL_ID) * SYS_MIN_QUEUE_NUM_PER_CHAN);
            }
            else if (SYS_QUEUE_DESTID_ENQ(lchip))
            {
                *queue_id  = p_gg_queue_master[lchip]->queue_base[SYS_QUEUE_TYPE_EXTENDER_WITH_Q]
                            + ((lport&0xFF) * p_gg_queue_master[lchip]->queue_num_per_chanel)
                            + ((lport >> 8) ? 1024 :0);
            }
            else
            {
                *queue_id  = p_gg_queue_master[lchip]->queue_base[SYS_QUEUE_TYPE_NORMAL]
                            + (lport * p_gg_queue_master[lchip]->queue_num_per_chanel)
                            + ((lport >> 8) ? 1024 :0);
            }

        }
        else
        {
            /*TBD*/
            return CTC_E_INVALID_PARAM;
        }
    }
        break;
    case CTC_QUEUE_TYPE_ILOOP:
        CTC_MAX_VALUE_CHECK(p_queue->queue_id,SYS_MIN_QUEUE_NUM_PER_CHAN-1);

        *queue_id = p_gg_queue_master[lchip]->queue_base[SYS_QUEUE_TYPE_RSV_PORT]
                     + ((SYS_ILOOP_CHANNEL_ID-SYS_DROP_CHANNEL_ID) * SYS_MIN_QUEUE_NUM_PER_CHAN)
                     + p_queue->queue_id;
        break;

    case CTC_QUEUE_TYPE_ELOOP:
        CTC_MAX_VALUE_CHECK(p_queue->queue_id,SYS_MIN_QUEUE_NUM_PER_CHAN-1);
        *queue_id = p_gg_queue_master[lchip]->queue_base[SYS_QUEUE_TYPE_RSV_PORT]
                     + ((SYS_ELOOP_CHANNEL_ID-SYS_DROP_CHANNEL_ID) * SYS_MIN_QUEUE_NUM_PER_CHAN)
                     + p_queue->queue_id;
        break;
    case CTC_QUEUE_TYPE_OAM:
        CTC_MAX_VALUE_CHECK(p_queue->queue_id,SYS_OAM_QUEUE_NUM-1);
        *queue_id  =  p_gg_queue_master[lchip]->queue_base[SYS_QUEUE_TYPE_OAM] + p_queue->queue_id;
        break;
    default:
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}


int32
sys_goldengate_queue_get_enqueue_info(uint8 lchip, uint16* port_num, uint8 *destid_mode)
{
    uint32 queue_num = 0;
    uint16 max_port_num = 0;
    uint16 cpu_reason_queue_num = 0;

    CTC_PTR_VALID_CHECK(port_num);
    CTC_PTR_VALID_CHECK(destid_mode);

    cpu_reason_queue_num = p_gg_queue_master[lchip]->queue_num_for_cpu_reason;
    if (p_gg_queue_master[lchip]->max_dma_rx_num == 1)
    {
        cpu_reason_queue_num = ((p_gg_queue_master[lchip]->queue_num_for_cpu_reason > 64) ? 64 : (p_gg_queue_master[lchip]->queue_num_for_cpu_reason));
    }
    /* drop queue = 8, eloop queue = 8, iloop queue = 16, oam queue = 8 */
    /* total port number(per slice):
     8 queue bpe : 107(cpu queue = 128)/111(cpu queue = 64)/119(cpu queue = 32)
     4 queue bpe : 214(cpu queue = 128)/222(cpu queue = 64)/238(cpu queue = 32)*/

    queue_num = 1024 - SYS_QUEUE_RSV_QUEUE_NUM -  cpu_reason_queue_num;

    max_port_num = queue_num/ p_gg_queue_master[lchip]->queue_num_per_chanel;

    *port_num = max_port_num;
    *destid_mode = p_gg_queue_master[lchip]->enq_mode;
    if (( 0 == *destid_mode) || (1 == *destid_mode))
    {
       *port_num = 216;  /* 256 -40 :reserved 32 eloop port + reserved 8 internal port */
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_queue_get_c2c_queue_base(uint8 lchip, uint16* c2c_queue_base)
{
    *c2c_queue_base = p_gg_queue_master[lchip]->c2c_group_base*SYS_REASON_GRP_QUEUE_NUM;
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_queue_set_queue_offset(uint8 lchip, uint16 queue_base,
                                                    uint16 queue_end, uint8 grp_mode)
{
    uint16 queue_id = 0;
    uint8 offset    = 0;
    sys_queue_node_t *p_queue_node = NULL;

    for (queue_id = queue_base; queue_id < queue_end; queue_id++)
    {
        if (1 == p_gg_queue_master[lchip]->enq_mode) /*8+4+1 mode*/
        {
            grp_mode = (((queue_id % 16) < 8) ? 1 : 0);
        }
        offset = (grp_mode ? (queue_id % 8) : (queue_id % 4));
        /*slice 0*/
        p_queue_node = ctc_vector_get(p_gg_queue_master[lchip]->queue_vec,queue_id);
        if (NULL == p_queue_node)
        {
            return CTC_E_ENTRY_NOT_EXIST;
        }
        p_queue_node->offset = offset;

        /*slice 1*/
        p_queue_node = ctc_vector_get(p_gg_queue_master[lchip]->queue_vec, (queue_id + 1024));
        if (NULL == p_queue_node)
        {
            return CTC_E_ENTRY_NOT_EXIST;
        }

        p_queue_node->offset = offset;

    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_queue_init_queue_base(uint8 lchip, sys_enqueue_info_t *p_enqueue)
{
    DsQueueNumGenCtl_m dsQueueGenCtl;
    DsQueueNumGenCtl_m dsQueueGenCtl_stacking;
    uint16   index[64] = {0};
    uint8    idx = 0;
    uint32   cmd = 0;
    uint8    loop = 0;
    uint8    qSelType = 0;
    uint8    QselIndex = 0;
    SYS_QUEUE_DBG_FUNC();


    switch (p_enqueue->queue_type)
    {
    case SYS_QUEUE_TYPE_NORMAL:
            if(p_enqueue->mirror)
            {
                qSelType = SYS_QSEL_TYPE_MIRROR;
            }
            else if (p_enqueue->cpu_tx)
            {
                qSelType = SYS_QSEL_TYPE_CPU_TX;
            }
            else if (p_enqueue->is_c2c)
            {
                qSelType = SYS_QSEL_TYPE_C2C;
            }
            else
            {
                qSelType = SYS_QSEL_TYPE_REGULAR;
            }
            break;
    case SYS_QUEUE_TYPE_RSV_PORT:
            qSelType = (p_enqueue->iloop_to_cpu)? SYS_QSEL_TYPE_ILOOP : SYS_QSEL_TYPE_RSV_PORT;
            break;
    case SYS_QUEUE_TYPE_EXTENDER_WITH_Q:                /* exterder queue */
            qSelType = p_enqueue->mirror? SYS_QSEL_TYPE_MIRROR:
                              SYS_QSEL_TYPE_EXTERN_PORT_WITH_Q;
            break;
    case SYS_QUEUE_TYPE_EXCP:
            if (p_enqueue->is_c2c)
            {
                qSelType = SYS_QSEL_TYPE_C2C;
            }
            else
            {
                qSelType = SYS_QSEL_TYPE_EXCP_CPU;
            }
            break;
    case SYS_QUEUE_TYPE_EXCP_BY_ETH:
            qSelType = SYS_QSEL_TYPE_EXCP_CPU_BY_ETH;
            break;
    default :
            break;
    }

    switch (p_enqueue->queue_type)
    {
    case SYS_QUEUE_TYPE_NORMAL:
        if(p_enqueue->is_c2c)
        {
            /*uplink to remote (ucast)*/
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(0, 0, 1, 0, 0, 0, qSelType);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(1, 0, 1, 0, 0, 0, qSelType);
            /*local to remote (ucast)*/
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(0, 0, 0, 0, 0, 0, qSelType);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(1, 0, 0, 0, 0, 0, qSelType);
            /*local to remote (mcast, chip not match)*/
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(0, 1, 0, 1, 0, 0, qSelType);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(1, 1, 0, 1, 0, 0, qSelType);
            /*local to remote (mcast, chip match)*/
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(0, 1, 0, 1, 0, 1, qSelType);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(1, 1, 0, 1, 0, 1, qSelType);
            /*uplink to remote (mcast, chip not match)*/
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(0, 1, 1, 1, 0, 0, qSelType);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(1, 1, 1, 1, 0, 0, qSelType);
            /*uplink to remote (mcast, chip match)*/
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(0, 1, 1, 1, 0, 1, qSelType);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(1, 1, 1, 1, 0, 1, qSelType);
            if(p_gg_queue_master[lchip]->stacking_queue_mode)
            {
                index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(0,0,0, 0, 0, 0, SYS_QSEL_TYPE_EXCP_CPU);
                index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(1,0,0, 0, 0, 0, SYS_QSEL_TYPE_EXCP_CPU);
            }
        }
        else if (p_enqueue->ucast)
        {
           /*1.1 ucast to local networks port/stacking port */
            /*from local to local (ucast)*/
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(0, 0, 0, 0, 0, 1, qSelType);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(1, 0, 0, 0, 0, 1, qSelType);
            /*from uplink to local (ucast)*/
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(0, 0, 1, 0, 0, 1, qSelType);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(1, 0, 1, 0, 0, 1, qSelType);

            /*from uplink to remote (ucast)*/
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(0, 0, 1, 0, 0, 0, qSelType);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(1, 0, 1, 0, 0, 0, qSelType);

            /*from uplink to remote (resvport)*/
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(0, 0, 1, 0, 0, 0, SYS_QSEL_TYPE_RSV_PORT);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(1, 0, 1, 0, 0, 0, SYS_QSEL_TYPE_RSV_PORT);

            /*local to remote (ucast)*/
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(0, 0, 0, 0, 0, 0, qSelType);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(1, 0, 0, 0, 0, 0, qSelType);

            /*local to remote (resvport)*/
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(0, 0, 0, 0, 0, 0, SYS_QSEL_TYPE_RSV_PORT);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(1, 0, 0, 0, 0, 0, SYS_QSEL_TYPE_RSV_PORT);

           /*1.2 mcast to stacking port */
            /*local to remote (mcast,  chip not match)*/
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(0, 1, 0, 1, 0, 0, qSelType);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(1, 1, 0, 1, 0, 0, qSelType);

            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(0, 1, 0, 1, 0, 0, SYS_QSEL_TYPE_RSV_PORT);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(1, 1, 0, 1, 0, 0, SYS_QSEL_TYPE_RSV_PORT);

            /*local to remote (mcast, chip match)*/
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(0, 1, 0, 1, 0, 1, qSelType);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(1, 1, 0, 1, 0, 1, qSelType);

            /*dest chip not match, For OAM*/
            for (QselIndex = 0; QselIndex < 8; QselIndex++)
            {
                index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(0, 1, 0, 0, 0, 0, QselIndex);
                index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(1, 1, 0, 0, 0, 0, QselIndex);

                index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(0, 1, 1, 0, 0, 0, QselIndex);
                index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(1, 1, 1, 0, 0, 0, QselIndex);
            }

            /*uplink to remote (mcast,  chip not match)*/
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(0, 1, 1, 1, 0, 0, qSelType);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(1, 1, 1, 1, 0, 0, qSelType);

            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(0, 1, 1, 1, 0, 0, SYS_QSEL_TYPE_RSV_PORT);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(1, 1, 1, 1, 0, 0, SYS_QSEL_TYPE_RSV_PORT);

            /*uplink to remote (mcast, chip match)*/
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(0, 1, 1, 1, 0, 1, qSelType);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(1, 1, 1, 1, 0, 1, qSelType);

        }
        else
        {
           /*2.1 mcast to local networks port */
            /*from local to local (mcast)*/
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(0, 1, 0, 0, 0, 1, qSelType);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(1, 1, 0, 0, 0, 1, qSelType);

            /*from uplink to local (mcast)*/
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(0, 1, 1, 0, 0, 1, qSelType);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(1, 1, 1, 0, 0, 1, qSelType);

            /*2.2 exception  to stacking port*/
            /*local to remote (ucast)*/
            if(!p_gg_queue_master[lchip]->stacking_queue_mode)
            {
                index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(0,0,0, 0, 0, 0, SYS_QSEL_TYPE_EXCP_CPU);
                index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(1,0,0, 0, 0, 0, SYS_QSEL_TYPE_EXCP_CPU);
            }
            /*from uplink to remote (ucast)*/
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(0,0,1, 0, 0, 0, SYS_QSEL_TYPE_EXCP_CPU);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(1,0,1, 0, 0, 0, SYS_QSEL_TYPE_EXCP_CPU);


        }
        break;
    case SYS_QUEUE_TYPE_RSV_PORT:
        if (p_enqueue->ucast)
        {
           /*1.1 ucast to local networks port/stacking port */
            /*from local to local (ucast)*/
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(0, 0, 0, 0, 0, 1, qSelType);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(1, 0, 0, 0, 0, 1, qSelType);

            /*from uplink to local (ucast)*/
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(0, 0, 1, 0, 0, 1, qSelType);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(1, 0, 1, 0, 0, 1, qSelType);
        }
        else
        {
           /*2.1 mcast to local networks port */
            /*from local to local (mcast)*/
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(0, 1, 0, 0, 0, 1, qSelType);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(1, 1, 0, 0, 0, 1, qSelType);

            /*from uplink to local (mcast)*/
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(0, 1, 1, 0, 0, 1, qSelType);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(1, 1, 1, 0, 0, 1, qSelType);

           /*2.2 mcast to stacking port */
            /*local to remote (mcast, chip match)*/
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(0, 1, 0, 1, 0, 1, qSelType);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(1, 1, 0, 1, 0, 1, qSelType);

            /*uplink to remote (mcast, chip match)*/
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(0, 1, 1, 1, 0, 1, qSelType);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(1, 1, 1, 1, 0, 1, qSelType);
        }
        break;

    case SYS_QUEUE_TYPE_EXTENDER_WITH_Q:
        if (p_enqueue->ucast)
        {
           /*1.1 ucast to local networks port/stacking port */
            /*from local to local (ucast)*/
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(0, 0, 0, 0, 0, 1, qSelType);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(1, 0, 0, 0, 0, 1, qSelType);

            /*from uplink to local (ucast)*/
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(0, 0, 1, 0, 0, 1, qSelType);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(1, 0, 1, 0, 0, 1, qSelType);

            /*from uplink to remote (ucast)*/
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(0, 0, 1, 0, 0, 0, qSelType);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(1, 0, 1, 0, 0, 0, qSelType);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(0, 0, 1, 0, 0, 0, SYS_QSEL_TYPE_REGULAR);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(1, 0, 1, 0, 0, 0, SYS_QSEL_TYPE_REGULAR);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(0, 0, 0, 0, 0, 0, SYS_QSEL_TYPE_EXCP_CPU);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(1, 0, 0, 0, 0, 0, SYS_QSEL_TYPE_EXCP_CPU);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(0, 0, 0, 0, 0, 0, SYS_QSEL_TYPE_RSV_PORT);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(1, 0, 0, 0, 0, 0, SYS_QSEL_TYPE_RSV_PORT);

            /*local to remote (ucast)*/
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(0, 0, 0, 0, 0, 0, qSelType);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(1, 0, 0, 0, 0, 0, qSelType);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(0, 0, 0, 0, 0, 0, SYS_QSEL_TYPE_REGULAR);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(1, 0, 0, 0, 0, 0, SYS_QSEL_TYPE_REGULAR);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(0, 0, 1, 0, 0, 0, SYS_QSEL_TYPE_EXCP_CPU);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(1, 0, 1, 0, 0, 0, SYS_QSEL_TYPE_EXCP_CPU);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(0, 0, 1, 0, 0, 0, SYS_QSEL_TYPE_RSV_PORT);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(1, 0, 1, 0, 0, 0, SYS_QSEL_TYPE_RSV_PORT);
        }
        else
        {
           /*2.1 mcast to local networks port */
            /*from local to local (mcast)*/
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(0, 1, 0, 0, 0, 1, qSelType);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(1, 1, 0, 0, 0, 1, qSelType);

            /*from uplink to local (mcast)*/
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(0, 1, 1, 0, 0, 1, qSelType);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(1, 1, 1, 0, 0, 1, qSelType);

           /*2.2 mcast to stacking port */
            /*local to remote (mcast,  chip not match)*/
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(0, 1, 0, 1, 0, 0, SYS_QSEL_TYPE_REGULAR);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(1, 1, 0, 1, 0, 0, SYS_QSEL_TYPE_REGULAR);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(0, 1, 0, 1, 0, 0, SYS_QSEL_TYPE_EXCP_CPU);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(1, 1, 0, 1, 0, 0, SYS_QSEL_TYPE_EXCP_CPU);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(0, 1, 0, 1, 0, 0, SYS_QSEL_TYPE_RSV_PORT);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(1, 1, 0, 1, 0, 0, SYS_QSEL_TYPE_RSV_PORT);
            /*local to remote (mcast, chip match)*/
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(0, 1, 0, 1, 0, 1, SYS_QSEL_TYPE_REGULAR);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(1, 1, 0, 1, 0, 1, SYS_QSEL_TYPE_REGULAR);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(0, 1, 0, 1, 0, 1, SYS_QSEL_TYPE_EXCP_CPU);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(1, 1, 0, 1, 0, 1, SYS_QSEL_TYPE_EXCP_CPU);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(0, 1, 0, 1, 0, 1, SYS_QSEL_TYPE_RSV_PORT);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(1, 1, 0, 1, 0, 1, SYS_QSEL_TYPE_RSV_PORT);

            /*uplink to remote (mcast,  chip not match)*/
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(0, 1, 1, 1, 0, 0, SYS_QSEL_TYPE_REGULAR);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(1, 1, 1, 1, 0, 0, SYS_QSEL_TYPE_REGULAR);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(0, 1, 1, 1, 0, 0, SYS_QSEL_TYPE_EXCP_CPU);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(1, 1, 1, 1, 0, 0, SYS_QSEL_TYPE_EXCP_CPU);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(0, 1, 1, 1, 0, 0, SYS_QSEL_TYPE_RSV_PORT);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(1, 1, 1, 1, 0, 0, SYS_QSEL_TYPE_RSV_PORT);

            /*uplink to remote (mcast, chip match)*/
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(0, 1, 1, 1, 0, 1, SYS_QSEL_TYPE_REGULAR);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(1, 1, 1, 1, 0, 1, SYS_QSEL_TYPE_REGULAR);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(0, 1, 1, 1, 0, 1, SYS_QSEL_TYPE_EXCP_CPU);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(1, 1, 1, 1, 0, 1, SYS_QSEL_TYPE_EXCP_CPU);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(0, 1, 1, 1, 0, 1, SYS_QSEL_TYPE_RSV_PORT);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(1, 1, 1, 1, 0, 1, SYS_QSEL_TYPE_RSV_PORT);

            /*dest chip not match, For OAM*/
            for (QselIndex = 0; QselIndex < 8; QselIndex++)
            {
                index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(0, 1, 0, 0, 0, 0, QselIndex);
                index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(1, 1, 0, 0, 0, 0, QselIndex);

                index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(0, 1, 1, 0, 0, 0, QselIndex);
                index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(1, 1, 1, 0, 0, 0, QselIndex);
            }

        }
        break;
    case SYS_QUEUE_TYPE_EXCP:
        if(p_enqueue->is_c2c)
        {
            /*from uplink to local cpu (ucast)*/
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(0, 0, 1, 0, 0, 1, qSelType);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(1, 0, 1, 0, 0, 1, qSelType);

            /*from uplink to local cpu (mcast)*/
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(0, 1, 1, 0, 0, 1, qSelType);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(1, 1, 1, 0, 0, 1, qSelType);
        }
        else
        {
            /*1. exception to local cpu */
            /* local to local cpu(ucast)*/
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(0, 0, 0, 0, 0, 1, qSelType);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(1, 0, 0, 0, 0, 1, qSelType);

            /* local to local cpu(mcast)*/
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(0, 1, 0, 0, 0, 1, qSelType);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(1, 1, 0, 0, 0, 1, qSelType);

            /*from uplink to local cpu (ucast)*/
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(0, 0, 1, 0, 0, 1, qSelType);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(1, 0, 1, 0, 0, 1, qSelType);

            /*from uplink to local cpu (mcast)*/
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(0, 1, 1, 0, 0, 1, qSelType);
            index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(1, 1, 1, 0, 0, 1, qSelType);
        }

        break;
    case SYS_QUEUE_TYPE_EXCP_BY_ETH:
       /*1. exception to local cpu */
        /* local to local cpu(ucast)*/
        index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(0,0,0, 0, 0, 1, qSelType);
        index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(1,0,0, 0, 0, 1, qSelType);
        /*from uplink to local cpu (ucast)*/
        index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(0,0,1, 0, 0, 1, qSelType);
        index[idx++] = SYS_QOS_ENCODE_GEN_CTL_INDEX(1,0,1, 0, 0, 1, qSelType);


        break;
    case SYS_QUEUE_TYPE_OAM:
        break;
    default:
        return CTC_E_INVALID_PARAM;
    }

    if(p_enqueue->queue_type == SYS_QUEUE_TYPE_OAM)
    {

        QWriteCtl_m  q_write_ctl;
        cmd = DRV_IOR(QWriteCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &q_write_ctl));

        /*for oam enqueue*/
        SetQWriteCtl(V,genQueIdRxEtherOam_f,&q_write_ctl,1);
        SetQWriteCtl(V,rxEtherOamQueueSelectShift_f,&q_write_ctl,6);
        SetQWriteCtl(V,array_0_rxEtherOamQueueBase_f,&q_write_ctl, (p_enqueue->queue_base++));
        SetQWriteCtl(V,array_1_rxEtherOamQueueBase_f,&q_write_ctl, (p_enqueue->queue_base));
        SetQWriteCtl(V,array_2_rxEtherOamQueueBase_f,&q_write_ctl, (p_enqueue->queue_base++));
        SetQWriteCtl(V,array_3_rxEtherOamQueueBase_f,&q_write_ctl, (p_enqueue->queue_base));
        SetQWriteCtl(V,array_4_rxEtherOamQueueBase_f,&q_write_ctl, (p_enqueue->queue_base++));
        SetQWriteCtl(V,array_5_rxEtherOamQueueBase_f,&q_write_ctl, (p_enqueue->queue_base));
        SetQWriteCtl(V,array_6_rxEtherOamQueueBase_f,&q_write_ctl, (p_enqueue->queue_base++));
        SetQWriteCtl(V,array_7_rxEtherOamQueueBase_f,&q_write_ctl, (p_enqueue->queue_base));
        SetQWriteCtl(V,array_8_rxEtherOamQueueBase_f,&q_write_ctl, (p_enqueue->queue_base++));
        SetQWriteCtl(V,array_9_rxEtherOamQueueBase_f,&q_write_ctl, (p_enqueue->queue_base));
        cmd = DRV_IOW(QWriteCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &q_write_ctl));

    }
    else
    {
        uint8 pcie_sel = 0;
        sys_enqueue_info_t enqueue_info;
        CTC_ERROR_RETURN(sys_goldengate_chip_get_pcie_select(lchip, &pcie_sel));

        sal_memset(&dsQueueGenCtl, 0, sizeof(dsQueueGenCtl));
        sal_memset(&dsQueueGenCtl_stacking, 0, sizeof(dsQueueGenCtl_stacking));
        cmd = DRV_IOW(DsQueueNumGenCtl_t, DRV_ENTRY_FLAG);

         /*slice0*/
        sal_memset(&enqueue_info, 0, sizeof(enqueue_info));
        sal_memcpy(&enqueue_info, p_enqueue, sizeof(enqueue_info));

        if (p_enqueue->queue_type != SYS_QUEUE_TYPE_NORMAL)
        {
            uint8 slice1_queue = 0;
            slice1_queue = (p_enqueue->queue_type == SYS_QUEUE_TYPE_EXCP && pcie_sel) || (p_enqueue->queue_type == SYS_QUEUE_TYPE_EXCP_BY_ETH
            && p_gg_queue_master[lchip]->have_lcpu_by_eth
            && p_gg_queue_master[lchip]->cpu_eth_chan_id >= 64);
            if (slice1_queue)
            { /*LCPU on slice1*/
                enqueue_info.queue_base += 1024;
            }
        }

        CTC_ERROR_RETURN(sys_goldengate_queue_map_gen_ctl(lchip, &dsQueueGenCtl, &enqueue_info));
        enqueue_info.stacking = 1;
        enqueue_info.dest_num = 64;
        CTC_ERROR_RETURN(sys_goldengate_queue_map_gen_ctl(lchip, &dsQueueGenCtl_stacking, &enqueue_info));
        for (loop = 0; loop < idx; (loop += 2))
        {
            if (CTC_IS_BIT_SET(index[loop], 5)/*que_on_chip = 1*/
                || !CTC_IS_BIT_SET(index[loop], 3))/*dest_chip_match = 0*/
            {
                /*to remote*/
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, index[loop], cmd, &dsQueueGenCtl_stacking));
            }
            else
            {
                /*to local*/
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, index[loop], cmd, &dsQueueGenCtl));
            }
        }

        /*slice1*/
        sal_memset(&enqueue_info, 0, sizeof(enqueue_info));
        sal_memcpy(&enqueue_info, p_enqueue, sizeof(enqueue_info));
        if (p_enqueue->queue_type == SYS_QUEUE_TYPE_NORMAL)
        {
            enqueue_info.dest_base += 64;
            enqueue_info.queue_base += 1024;
        }
        else
        {
            uint8 slice0_queue = 0;
            slice0_queue = ((p_enqueue->queue_type == SYS_QUEUE_TYPE_EXCP) && !pcie_sel) ||
            (p_enqueue->queue_type == SYS_QUEUE_TYPE_EXCP_BY_ETH
            && p_gg_queue_master[lchip]->have_lcpu_by_eth
            && p_gg_queue_master[lchip]->cpu_eth_chan_id < 64);
            if (!slice0_queue)
            {
                enqueue_info.queue_base += 1024;
            }
        }

        CTC_ERROR_RETURN(sys_goldengate_queue_map_gen_ctl(lchip, &dsQueueGenCtl, &enqueue_info));
        enqueue_info.stacking = 1;
        enqueue_info.dest_num = 64;
        CTC_ERROR_RETURN(sys_goldengate_queue_map_gen_ctl(lchip, &dsQueueGenCtl_stacking, &enqueue_info));
        for (loop = 1; loop < idx; (loop += 2))
        {
            if (CTC_IS_BIT_SET(index[loop], 5)/*que_on_chip = 1*/
                || !CTC_IS_BIT_SET(index[loop], 3))/*dest_chip_match = 0*/
            {
                /*to remote*/
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, index[loop], cmd, &dsQueueGenCtl_stacking));
            }
            else
            {
                /*to local*/
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, index[loop], cmd, &dsQueueGenCtl));
            }
        }
    }
    return DRV_E_NONE;
}
int32
_sys_goldengate_queue_init_excp_queue(uint8 lchip, uint16 *queue_base)
{
    sys_enqueue_info_t info;

    DsQueueBaseProfileId_m profile_id;
    DsQueueBaseProfile_m   profile;
    uint32 offset = 0;
    uint32 cmd = 0;
    uint8 reason_group = 0;
    uint16 queue_id = 0;
    uint8 channel = 0;
    uint8 start_group = 0;

    /*exception to cpu by DMA*/
    p_gg_queue_master[lchip]->queue_base[SYS_QUEUE_TYPE_EXCP] = *queue_base;
    sal_memset(&info, 0, sizeof(info));
    info.dest_base = 0;
    /*
    info.dest_num  = SYS_LCPU_QUEUE_NUM;
    if (SYS_QUEUE_DESTID_ENQ(lchip))
    {
        info.dest_num  = 32;
    }
    */
    info.dest_num  = p_gg_queue_master[lchip]->queue_num_for_cpu_reason;
    if(p_gg_queue_master[lchip]->max_dma_rx_num == 1)
    {
        info.dest_num = ((p_gg_queue_master[lchip]->queue_num_for_cpu_reason > 64) ? 64 : p_gg_queue_master[lchip]->queue_num_for_cpu_reason);
    }

    info.queue_type = SYS_QUEUE_TYPE_EXCP;
    info.subq_num  = 1; /* only have uc queue num*/
    info.ucast = 1;
    info.queue_base = (*queue_base);
    info.queue_num = 1;
    if(CTC_E_NONE != _sys_goldengate_queue_init_queue_base(lchip, &info))
    {
       SYS_QUEUE_DBG_DUMP("init_queue_base error!\n");
    }
    *queue_base += info.dest_num;
    _sys_goldengate_queue_set_queue_offset(lchip, p_gg_queue_master[lchip]->queue_base[SYS_QUEUE_TYPE_EXCP], (*queue_base), 1);

   offset = 0;
   cmd = DRV_IOR(DsQueueBaseProfileId_t, DRV_ENTRY_FLAG);
   CTC_ERROR_RETURN(DRV_IOCTL(lchip, SYS_DMA_CHANNEL_ID, cmd, &profile_id));
   SetDsQueueBaseProfileId(V, queueBaseProfId_f, &profile_id, offset);
   cmd = DRV_IOW(DsQueueBaseProfileId_t, DRV_ENTRY_FLAG);
   CTC_ERROR_RETURN(DRV_IOCTL(lchip, SYS_DMA_CHANNEL_ID, cmd, &profile_id));

   cmd = DRV_IOR(DsQueueBaseProfileId_t, DRV_ENTRY_FLAG);
   CTC_ERROR_RETURN(DRV_IOCTL(lchip, SYS_DMA_CHANNEL_ID+64, cmd, &profile_id));
   SetDsQueueBaseProfileId(V, queueBaseProfId_f, &profile_id, offset);
   cmd = DRV_IOW(DsQueueBaseProfileId_t, DRV_ENTRY_FLAG);
   CTC_ERROR_RETURN(DRV_IOCTL(lchip, SYS_DMA_CHANNEL_ID+64, cmd, &profile_id));

   cmd = DRV_IOR(DsQueueBaseProfile_t, DRV_ENTRY_FLAG);
   CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &profile));
   DRV_SET_FIELD_V(DsQueueBaseProfile_t, DsQueueBaseProfile_g_0_queueNumBase_f + offset, &profile, p_gg_queue_master[lchip]->queue_base[SYS_QUEUE_TYPE_EXCP]);
   cmd = DRV_IOW(DsQueueBaseProfile_t, DRV_ENTRY_FLAG);
   CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &profile))

    /*DMA*/
    for (reason_group = 0; reason_group < info.dest_num/SYS_GG_MAX_QNUM_PER_GROUP; reason_group++)
    {
        queue_id = p_gg_queue_master[lchip]->queue_base[SYS_QUEUE_TYPE_EXCP] + reason_group *SYS_GG_MAX_QNUM_PER_GROUP ;

         SYS_QOS_RX_DMA_CHANNEL(reason_group);
         /*slice 0*/
         sys_goldengate_qos_add_group_to_channel(lchip,  queue_id / 8, channel, start_group);
         /*slice 1*/
         sys_goldengate_qos_add_group_to_channel(lchip,  (queue_id+1024) / 8, channel+64, start_group);
    }

    return CTC_E_NONE;

}


int32
_sys_goldengate_queue_init_excp_by_eth_queue(uint8 lchip)
{
    sys_enqueue_info_t info;
    uint16 queue_base = 0;

    queue_base = p_gg_queue_master[lchip]->queue_base[SYS_QUEUE_TYPE_NORMAL] +
                 (p_gg_queue_master[lchip]->cpu_eth_chan_id&0x3F) *p_gg_queue_master[lchip]->queue_num_per_chanel;
    /*exception to cpu by DMA*/
    p_gg_queue_master[lchip]->queue_base[SYS_QUEUE_TYPE_EXCP_BY_ETH] = queue_base;

    sal_memset(&info, 0, sizeof(info));
    info.dest_base = 0;
    info.dest_num  = p_gg_queue_master[lchip]->queue_num_per_chanel;
    info.queue_type = SYS_QUEUE_TYPE_EXCP_BY_ETH;
    info.subq_num  = 1; /* only have uc queue num*/
    info.ucast = 1;
    info.queue_base = queue_base;
    info.queue_num = 1;
    if(CTC_E_NONE != _sys_goldengate_queue_init_queue_base(lchip, &info))
    {
       SYS_QUEUE_DBG_DUMP("init_queue_base error!\n");
    }
    return CTC_E_NONE;

}

int32
_sys_goldengate_queue_init_networks_port_queue(uint8 lchip, uint16 *queue_base)

{
    uint8 queue_group = 0;
    uint16 queue_id = 0;
    uint8  chan_id;
    uint16 ucast_queue_base = 0;
    uint16 mcast_queue_base = 0;
    uint16 mirror_queue_base = 0;
    uint16 cpu_tx_queue_base = 0;
    uint8 ucast_queue_num = 0;
    uint8 mcast_queue_num = 0;
    uint8 mirror_queue_num = 0;
    uint8 cpu_tx_queue_num = 0;
    uint16 dest_num     = 0;
    uint8  dest_id_num  = 0;
    uint8 queue_type = SYS_QUEUE_TYPE_NORMAL;
    uint16 queue_start = 0;
    uint8 grp_mode = 0;
    sys_enqueue_info_t info;

    sal_memset(&info, 0, sizeof(sys_enqueue_info_t));

    queue_start = *queue_base;

    if (0 == p_gg_queue_master[lchip]->enq_mode)/*8+8 mode*/
    {
        ucast_queue_num = 8;
        mcast_queue_num = 8;
        mirror_queue_num = 8;
        cpu_tx_queue_num = 16;

        ucast_queue_base = (*queue_base);
        mcast_queue_base = (*queue_base);
        mirror_queue_base = (*queue_base);
        cpu_tx_queue_base = (*queue_base);

        dest_num    = 64;
        queue_type  = SYS_QUEUE_TYPE_NORMAL;
        grp_mode    = 1;
    }
    else if (1 == p_gg_queue_master[lchip]->enq_mode) /*8+4+1 mode*/
    {
        ucast_queue_num = 8;
        mcast_queue_num = 4;
        mirror_queue_num = 1;

        ucast_queue_base = (*queue_base);
        mcast_queue_base = (*queue_base) + 8;
        mirror_queue_base = (*queue_base) + 12;

        dest_num    = 64;
        queue_type  = SYS_QUEUE_TYPE_NORMAL;
        grp_mode    = 1;
    }
    else if(2 == p_gg_queue_master[lchip]->enq_mode) /*bpe mode*/
    {
        if (8 == p_gg_queue_master[lchip]->queue_num_per_chanel)/*8 share mode */
        {
            ucast_queue_num = 8;
            mcast_queue_num = 8;
            mirror_queue_num = 8;

            ucast_queue_base = (*queue_base);
            mcast_queue_base = (*queue_base);
            mirror_queue_base = (*queue_base);

            dest_num    = 128;
            dest_id_num = (1024 - p_gg_queue_master[lchip]->queue_num_for_cpu_reason - SYS_QUEUE_RSV_QUEUE_NUM) / 8;
            queue_type  = SYS_QUEUE_TYPE_EXTENDER_WITH_Q;
            grp_mode    = 1;
        }
        else if (4 == p_gg_queue_master[lchip]->queue_num_per_chanel) /*4 share mode */
        {
            ucast_queue_num     = 4;
            mcast_queue_num     = 4;
            mirror_queue_num    = 4;

            ucast_queue_base    = (*queue_base);
            mcast_queue_base    = (*queue_base);
            mirror_queue_base   = (*queue_base);

            dest_num    = 256;
            dest_id_num = (1024 - p_gg_queue_master[lchip]->queue_num_for_cpu_reason - SYS_QUEUE_RSV_QUEUE_NUM) / 4;
            queue_type  = SYS_QUEUE_TYPE_EXTENDER_WITH_Q;
            grp_mode    = 0;
        }
    }
    else
    {
        return CTC_E_INVALID_PARAM;
    }

    p_gg_queue_master[lchip]->queue_base[queue_type] = *queue_base;

    /*uc queues*/
    info.dest_base = 0;
    info.dest_num  = dest_num;
    info.queue_type = queue_type;
    info.subq_num  = ucast_queue_num;
    info.ucast = 1;
    info.queue_base = ucast_queue_base;
    info.queue_num  = p_gg_queue_master[lchip]->queue_num_per_chanel;
    if(CTC_E_NONE != _sys_goldengate_queue_init_queue_base(lchip, &info))
    {
       SYS_QUEUE_DBG_DUMP("init_queue_base error!\n");
    }

   /*mc queues*/
    info.dest_base = 0;
    info.dest_num  = dest_num;
    info.queue_type = queue_type;
    info.subq_num  = mcast_queue_num;
    info.ucast = 0;
    info.queue_base = mcast_queue_base;
    info.queue_num  = p_gg_queue_master[lchip]->queue_num_per_chanel;
    if (CTC_E_NONE != _sys_goldengate_queue_init_queue_base(lchip, &info))
    {
        SYS_QUEUE_DBG_DUMP("init_queue_base error!\n");
    }
    /*Mirror  queues */
    info.dest_base = 0;
    info.dest_num  = dest_num;
    info.queue_type = queue_type;
    info.subq_num  = mirror_queue_num;
    info.ucast = 1;
    info.mirror = 1;
    info.queue_base = mirror_queue_base;
    info.queue_num  = p_gg_queue_master[lchip]->queue_num_per_chanel;
    if (CTC_E_NONE != _sys_goldengate_queue_init_queue_base(lchip, &info))
    {
        SYS_QUEUE_DBG_DUMP("init_queue_base error!\n");
    }
    info.ucast = 0;
    info.mirror = 1;
    if (CTC_E_NONE != _sys_goldengate_queue_init_queue_base(lchip, &info))
    {
        SYS_QUEUE_DBG_DUMP("init_queue_base error!\n");
    }

    if (0 == p_gg_queue_master[lchip]->enq_mode)
    {
        sal_memset(&info, 0, sizeof(sys_enqueue_info_t));
        info.dest_base = 0;
        info.dest_num  = dest_num;
        info.queue_type = queue_type;
        info.subq_num  = cpu_tx_queue_num;
        info.ucast = 1;
        info.cpu_tx = 1;
        info.queue_base = cpu_tx_queue_base;
        info.queue_num  = p_gg_queue_master[lchip]->queue_num_per_chanel;
        if (CTC_E_NONE != _sys_goldengate_queue_init_queue_base(lchip, &info))
        {
            SYS_QUEUE_DBG_DUMP("init_queue_base error!\n");
        }
        info.ucast = 0;
        info.cpu_tx = 1;
        if (CTC_E_NONE != _sys_goldengate_queue_init_queue_base(lchip, &info))
        {
            SYS_QUEUE_DBG_DUMP("init_queue_base error!\n");
        }
    }

    /*init channel queue*/
    if ((0 == p_gg_queue_master[lchip]->enq_mode)||(1 == p_gg_queue_master[lchip]->enq_mode))
    {
        for (chan_id = 0; chan_id  < (p_gg_queue_master[lchip]->max_chan_per_slice ); chan_id++)
        {
            for (queue_group = 0; queue_group < p_gg_queue_master[lchip]->queue_num_per_chanel / SYS_QUEUE_NUM_PER_GROUP; queue_group++)
            {
                queue_id = (*queue_base) + chan_id * p_gg_queue_master[lchip]->queue_num_per_chanel + queue_group *SYS_QUEUE_NUM_PER_GROUP ;
                /*slice0*/
                CTC_ERROR_RETURN(sys_goldengate_qos_add_group_to_channel(lchip,  queue_id / 8, chan_id, (queue_group == 0)));
                /*slice1*/
                CTC_ERROR_RETURN(sys_goldengate_qos_add_group_to_channel(lchip,  (queue_id + 1024) / 8, (chan_id + 64), (queue_group == 0)));
            }
        }
        (*queue_base) += (p_gg_queue_master[lchip]->queue_num_per_chanel*(p_gg_queue_master[lchip]->max_chan_per_slice));
    }
    else
    {
        (*queue_base) += (p_gg_queue_master[lchip]->queue_num_per_chanel*dest_id_num);
    }
    _sys_goldengate_queue_set_queue_offset(lchip, queue_start, (*queue_base),  grp_mode);

    return CTC_E_NONE;
}



/*temp code for system, need called when qos init, and set queue type = SYS_QUEUE_TYPE_EXTENDER_WITH_Q when packet tx*/
int32
sys_goldengate_queue_init_networks_port_queue_for_cb(uint8 lchip)

{
    uint16 ucast_queue_base = 0;
    uint16 mcast_queue_base = 0;
    uint8 ucast_queue_num = 0;
    uint8 mcast_queue_num = 0;
    uint16 dest_num     = 0;
    uint8 queue_type = SYS_QUEUE_TYPE_NORMAL;
    uint16 queue_base = 0;
    sys_enqueue_info_t info;

    sal_memset(&info, 0, sizeof(sys_enqueue_info_t));

    if (p_gg_queue_master[lchip]->queue_num_per_chanel == 16) /*8+4+1 mode*/
    {
        ucast_queue_num = 8;
        mcast_queue_num = 4;

        ucast_queue_base = (queue_base);
        mcast_queue_base = (queue_base) + 8;

        dest_num    = 64;
        queue_type  = SYS_QUEUE_TYPE_EXTENDER_WITH_Q;
    }
    else
    {
        return CTC_E_INVALID_PARAM;
    }

    p_gg_queue_master[lchip]->queue_base[queue_type] = queue_base;

    /*uc queues*/
    info.dest_base = 0;
    info.dest_num  = dest_num;
    info.queue_type = queue_type;
    info.subq_num  = ucast_queue_num;
    info.ucast = 1;
    info.queue_base = ucast_queue_base;
    info.queue_num  = p_gg_queue_master[lchip]->queue_num_per_chanel;
    if(CTC_E_NONE != _sys_goldengate_queue_init_queue_base(lchip, &info))
    {
       SYS_QUEUE_DBG_DUMP("init_queue_base error!\n");
    }

   /*mc queues*/
    info.dest_base = 0;
    info.dest_num  = dest_num;
    info.queue_type = queue_type;
    info.subq_num  = mcast_queue_num;
    info.ucast = 0;
    info.queue_base = mcast_queue_base;
    info.queue_num  = p_gg_queue_master[lchip]->queue_num_per_chanel;
    if (CTC_E_NONE != _sys_goldengate_queue_init_queue_base(lchip, &info))
    {
        SYS_QUEUE_DBG_DUMP("init_queue_base error!\n");
    }

    return CTC_E_NONE;
}


/*port 56~63*/
int32
_sys_goldengate_queue_init_reserved_queue(uint8 lchip, uint16 *queue_base)

{
    uint32  cmd = 0;
    uint8 queue_group = 0;
    uint8 chan_id = 0;
    uint16 queue_id = 0;
    uint32  field = 0;
    uint16 queue_start = 0;
    sys_enqueue_info_t info;
    sal_memset(&info, 0, sizeof(info));

    queue_start = (*queue_base);
    p_gg_queue_master[lchip]->queue_base[SYS_QUEUE_TYPE_RSV_PORT] = (*queue_base);
    info.dest_base = SYS_DROP_CHANNEL_ID;
    info.dest_num  = 64;
    info.queue_type = SYS_QUEUE_TYPE_RSV_PORT;
    info.subq_num  = 8;/*uc & mc queue num*/
    info.ucast = 1;
    info.queue_base = (*queue_base);
    info.queue_num = SYS_MIN_QUEUE_NUM_PER_CHAN;
    if(CTC_E_NONE != _sys_goldengate_queue_init_queue_base(lchip, &info))
    {
       SYS_QUEUE_DBG_DUMP("init_queue_base error!\n");
    }

    info.dest_base = SYS_DROP_CHANNEL_ID;
    info.dest_num  = 64;
    info.queue_type = SYS_QUEUE_TYPE_RSV_PORT;
    info.subq_num  = 8;/*uc & mc queue num*/
    info.ucast = 0;
    info.queue_base = (*queue_base);
    info.queue_num = SYS_MIN_QUEUE_NUM_PER_CHAN;
    if(CTC_E_NONE != _sys_goldengate_queue_init_queue_base(lchip, &info))
    {
       SYS_QUEUE_DBG_DUMP("init_queue_base error!\n");
    }
    /* binding drop, eloop & iloop group to channel and channel's start group */
    for (chan_id = SYS_DROP_CHANNEL_ID; chan_id  <=  SYS_ILOOP_CHANNEL_ID; chan_id++)
    {
        for (queue_group = 0; queue_group < SYS_MIN_QUEUE_NUM_PER_CHAN / SYS_QUEUE_NUM_PER_GROUP; queue_group++)
        {
            queue_id = (*queue_base) + (chan_id -SYS_DROP_CHANNEL_ID) * SYS_MIN_QUEUE_NUM_PER_CHAN + queue_group *SYS_QUEUE_NUM_PER_GROUP ;
            /*slice0*/
            CTC_ERROR_RETURN(sys_goldengate_qos_add_group_to_channel(lchip,  queue_id / 8, chan_id, (queue_group == 0)));
            /*slice1*/
            CTC_ERROR_RETURN(sys_goldengate_qos_add_group_to_channel(lchip,  (queue_id + 1024) / 8, (chan_id + 64), (queue_group == 0)));
        }
    }
    (*queue_base) += (SYS_QUEUE_NUM_PER_GROUP*(SYS_ILOOP_CHANNEL_ID - SYS_DROP_CHANNEL_ID + 1));

    field = SYS_ILOOP_CHANNEL_ID;
    cmd   = DRV_IOW(IpeGlobalChannelMap_t, IpeGlobalChannelMap_iloopChannel_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field));

    field = SYS_ELOOP_CHANNEL_ID;
    cmd = DRV_IOW(EpeGlobalChannelMap_t, EpeGlobalChannelMap_eloopChannel_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field));

    if(p_gg_queue_master[lchip]->enq_mode != 2)
    {
        /* iloop for packet to CPU*/
        sal_memset(&info, 0, sizeof(info));
        info.dest_base = SYS_ILOOP_CHANNEL_ID;
        info.dest_num  = 64;
        info.queue_type = SYS_QUEUE_TYPE_RSV_PORT;
        info.subq_num  = 8;/*uc & mc queue num*/
        info.queue_base = (*queue_base);
        info.queue_num = SYS_MIN_QUEUE_NUM_PER_CHAN;
        info.iloop_to_cpu = 1;
        info.ucast = 1;
        if(CTC_E_NONE != _sys_goldengate_queue_init_queue_base(lchip, &info))
        {
           SYS_QUEUE_DBG_DUMP("init_queue_base error!\n");
        }
        info.ucast = 0;
        if(CTC_E_NONE != _sys_goldengate_queue_init_queue_base(lchip, &info))
        {
           SYS_QUEUE_DBG_DUMP("init_queue_base error!\n");
        }
        /* only bind iloop group to channel */
        CTC_ERROR_RETURN(sys_goldengate_qos_add_group_to_channel(lchip,  (*queue_base) / 8, SYS_ILOOP_CHANNEL_ID, 0));
        CTC_ERROR_RETURN(sys_goldengate_qos_add_group_to_channel(lchip,  ((*queue_base) + 1024) / 8, (SYS_ILOOP_CHANNEL_ID + 64),0));
        (*queue_base) += SYS_QUEUE_NUM_PER_GROUP;
    }

    /*OAM*/
    p_gg_queue_master[lchip]->queue_base[SYS_QUEUE_TYPE_OAM] = (*queue_base);

    info.dest_base = 0;
    info.dest_num  = 0;
    info.queue_type = SYS_QUEUE_TYPE_OAM;
    info.subq_num  = 1;/*uc & mc queue num*/
    info.ucast = 0;
    info.queue_base = (*queue_base);
    info.queue_num = SYS_OAM_QUEUE_NUM;
    if(CTC_E_NONE != _sys_goldengate_queue_init_queue_base(lchip, &info))
    {
       SYS_QUEUE_DBG_DUMP("init_queue_base error!\n");
    }

    chan_id = SYS_OAM_CHANNEL_ID;

    for (queue_group = 0; queue_group < SYS_MIN_QUEUE_NUM_PER_CHAN / SYS_QUEUE_NUM_PER_GROUP; queue_group++)
    {
        queue_id = (*queue_base) + queue_group *SYS_QUEUE_NUM_PER_GROUP ;
        /*slice0*/
        CTC_ERROR_RETURN(sys_goldengate_qos_add_group_to_channel(lchip,  queue_id / 8, chan_id, (queue_group == 0)));
        /*slice1*/
        CTC_ERROR_RETURN(sys_goldengate_qos_add_group_to_channel(lchip,  (queue_id + 1024) / 8, (chan_id + 64), (queue_group == 0)));
    }

    (*queue_base) += 8;

    _sys_goldengate_queue_set_queue_offset(lchip, queue_start, (*queue_base), 1);

    return CTC_E_NONE;
}

int32
sys_goldengate_queue_init_c2c_queue(uint8 lchip, uint8 c2c_group_base, uint8 enq_mode, uint8 queue_type)
{
    uint8 c2c_subq_num = 0;
    uint8 c2c_queue_base = 0;
    sys_enqueue_info_t info;
    /*trunk port c2c queue num*/
    c2c_subq_num = enq_mode ? 16 : 8;
    c2c_queue_base = enq_mode ? p_gg_queue_master[lchip]->queue_base[SYS_QUEUE_TYPE_NORMAL]
                     : (p_gg_queue_master[lchip]->queue_base[SYS_QUEUE_TYPE_NORMAL] + 8);

    sal_memset(&info, 0, sizeof(sys_enqueue_info_t));

    if (SYS_QUEUE_TYPE_NORMAL == queue_type)
    {
        info.dest_base = 0;
        info.dest_num  = 64;
        info.queue_type = SYS_QUEUE_TYPE_NORMAL;
        info.subq_num  = c2c_subq_num;
        info.is_c2c = 1;
        info.queue_base = c2c_queue_base;
        info.queue_num  = p_gg_queue_master[lchip]->queue_num_per_chanel;
        if (CTC_E_NONE != _sys_goldengate_queue_init_queue_base(lchip, &info))
        {
            SYS_QUEUE_DBG_DUMP("init_queue_base error!\n");
        }
    }

    if (SYS_QUEUE_TYPE_EXCP == queue_type)
    {
        info.dest_base = 0;
        info.dest_num  = 1;
        info.queue_type = SYS_QUEUE_TYPE_EXCP;
        info.subq_num  = 16;
        info.ucast = 1;
        info.queue_base = p_gg_queue_master[lchip]->queue_base[SYS_QUEUE_TYPE_EXCP] + c2c_group_base*SYS_QUEUE_NUM_PER_GROUP;
        info.queue_num = 1;
        info.is_c2c = 1;
        if (CTC_E_NONE != _sys_goldengate_queue_init_queue_base(lchip, &info))
        {
            SYS_QUEUE_DBG_DUMP("init_queue_base error!\n");
        }
    }

    return CTC_E_NONE;
}


/**
 @brief Globally set c2c queue mode.
  mode 0: c2c pkt use 8 queue(8-15) in trunk port
  mode 1: c2c pkt use 16 queue(0-15) in trunk port
*/
int32
sys_goldengate_queue_set_c2c_queue_mode(uint8 lchip, uint8 mode)
{
    SYS_QOS_QUEUE_INIT_CHECK(lchip);
    CTC_MAX_VALUE_CHECK(mode, 1);
    sys_goldengate_queue_init_c2c_queue(lchip, p_gg_queue_master[lchip]->c2c_group_base, mode, SYS_QUEUE_TYPE_NORMAL);
    return CTC_E_NONE;
}

int32
sys_goldengate_queue_set_queue_num(uint8 lchip, ctc_qos_queue_number_t* p_queue_num)
{
    return CTC_E_FEATURE_NOT_SUPPORT;
}

int32
sys_goldengate_queue_set_group_mode(uint8 lchip, uint8 mode)
{
    uint16 ucast_queue_base = 0;
    uint16 mcast_queue_base = 0;
    uint16 mirror_queue_base = 0;
    uint16 cpu_tx_queue_base = 0;
    uint8 ucast_queue_num = 0;
    uint8 mcast_queue_num = 0;
    uint8 mirror_queue_num = 0;
    uint8 cpu_tx_queue_num = 0;
    uint16 dest_num     = 0;
    uint8 queue_type = SYS_QUEUE_TYPE_NORMAL;
    uint16 queue_base = 0;
    uint32 cmd = 0;
    uint16 sub_grp = 0;
    uint8 group_mode = 0;
    uint8  slice_id = 0;
    DsSchServiceProfile_m sch_service_profile;
    sys_enqueue_info_t info;

    SYS_QOS_QUEUE_INIT_CHECK(lchip);
    CTC_MAX_VALUE_CHECK(mode, 1);

    sal_memset(&info, 0, sizeof(sys_enqueue_info_t));
    sal_memset(&sch_service_profile, 0, sizeof(DsSchServiceProfile_m));

    p_gg_queue_master[lchip]->group_mode = mode;

    if (mode)
    {
        if (0 == p_gg_queue_master[lchip]->enq_mode)/*8+8 mode*/
        {
            ucast_queue_num = 16;
            mcast_queue_num = 16;
            mirror_queue_num = 16;
            cpu_tx_queue_num = 16;

            ucast_queue_base = queue_base;
            mcast_queue_base = queue_base;
            mirror_queue_base = queue_base;
            cpu_tx_queue_base = queue_base;

            dest_num    = 64;
            queue_type  = SYS_QUEUE_TYPE_NORMAL;
        }
        else
        {
            return CTC_E_INVALID_PARAM;
        }

        p_gg_queue_master[lchip]->queue_base[queue_type] = queue_base;

        /*uc queues*/
        info.dest_base = 0;
        info.dest_num  = dest_num;
        info.queue_type = queue_type;
        info.subq_num  = ucast_queue_num;
        info.ucast = 1;
        info.queue_base = ucast_queue_base;
        info.queue_num  = p_gg_queue_master[lchip]->queue_num_per_chanel;
        if (CTC_E_NONE != _sys_goldengate_queue_init_queue_base(lchip, &info))
        {
            SYS_QUEUE_DBG_DUMP("init_queue_base error!\n");
        }

        /*mc queues*/
        info.dest_base = 0;
        info.dest_num  = dest_num;
        info.queue_type = queue_type;
        info.subq_num  = mcast_queue_num;
        info.ucast = 0;
        info.queue_base = mcast_queue_base;
        info.queue_num  = p_gg_queue_master[lchip]->queue_num_per_chanel;
        if (CTC_E_NONE != _sys_goldengate_queue_init_queue_base(lchip, &info))
        {
            SYS_QUEUE_DBG_DUMP("init_queue_base error!\n");
        }
        /*Mirror  queues */
        info.dest_base = 0;
        info.dest_num  = dest_num;
        info.queue_type = queue_type;
        info.subq_num  = mirror_queue_num;
        info.ucast = 1;
        info.mirror = 1;
        info.queue_base = mirror_queue_base;
        info.queue_num  = p_gg_queue_master[lchip]->queue_num_per_chanel;
        if (CTC_E_NONE != _sys_goldengate_queue_init_queue_base(lchip, &info))
        {
            SYS_QUEUE_DBG_DUMP("init_queue_base error!\n");
        }
        info.ucast = 0;
        info.mirror = 1;
        if (CTC_E_NONE != _sys_goldengate_queue_init_queue_base(lchip, &info))
        {
            SYS_QUEUE_DBG_DUMP("init_queue_base error!\n");
        }

        if (0 == p_gg_queue_master[lchip]->enq_mode)
        {
            sal_memset(&info, 0, sizeof(sys_enqueue_info_t));
            info.dest_base = 0;
            info.dest_num  = dest_num;
            info.queue_type = queue_type;
            info.subq_num  = cpu_tx_queue_num;
            info.ucast = 1;
            info.cpu_tx = 1;
            info.queue_base = cpu_tx_queue_base;
            info.queue_num  = p_gg_queue_master[lchip]->queue_num_per_chanel;
            if (CTC_E_NONE != _sys_goldengate_queue_init_queue_base(lchip, &info))
            {
                SYS_QUEUE_DBG_DUMP("init_queue_base error!\n");
            }
            info.ucast = 0;
            info.cpu_tx = 1;
            if (CTC_E_NONE != _sys_goldengate_queue_init_queue_base(lchip, &info))
            {
                SYS_QUEUE_DBG_DUMP("init_queue_base error!\n");
            }
        }

        for (slice_id = 0; slice_id < SYS_MAX_LOCAL_SLICE_NUM; slice_id++)
        {
            for (sub_grp = 0; sub_grp < SYS_MAX_QUEUE_GROUP_NUM; sub_grp++)
            {
                sys_goldengate_queue_get_grp_mode(lchip, (SYS_MAX_QUEUE_GROUP_NUM*slice_id + sub_grp) / 2, &group_mode);

                cmd = DRV_IOR(DsSchServiceProfile_t, DRV_ENTRY_FLAG);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, (SYS_MAX_QUEUE_GROUP_NUM*slice_id + sub_grp), cmd, &sch_service_profile));
                if (16 == p_gg_queue_master[lchip]->queue_num_per_chanel)
                {
                    if (group_mode == 1)  /*ucast group*/
                    {
                        SetDsSchServiceProfile(V, queOffset0GreenRemapSchPri_f, &sch_service_profile, 7);
                        SetDsSchServiceProfile(V, queOffset0RedRemapSchPri_f, &sch_service_profile, 8);
                        SetDsSchServiceProfile(V, queOffset0YellowRemapSchPri_f, &sch_service_profile, 0);
                        SetDsSchServiceProfile(V, queOffset1GreenRemapSchPri_f, &sch_service_profile, 7);
                        SetDsSchServiceProfile(V, queOffset1RedRemapSchPri_f, &sch_service_profile, 8);
                        SetDsSchServiceProfile(V, queOffset1YellowRemapSchPri_f, &sch_service_profile, 1);
                        SetDsSchServiceProfile(V, queOffset2GreenRemapSchPri_f, &sch_service_profile, 7);
                        SetDsSchServiceProfile(V, queOffset2RedRemapSchPri_f, &sch_service_profile, 8);
                        SetDsSchServiceProfile(V, queOffset2YellowRemapSchPri_f, &sch_service_profile, 2);
                        SetDsSchServiceProfile(V, queOffset3GreenRemapSchPri_f, &sch_service_profile, 7);
                        SetDsSchServiceProfile(V, queOffset3RedRemapSchPri_f, &sch_service_profile, 8);
                        SetDsSchServiceProfile(V, queOffset3YellowRemapSchPri_f, &sch_service_profile, 3);
                        SetDsSchServiceProfile(V, queOffset4GreenRemapSchPri_f, &sch_service_profile, 7);
                        SetDsSchServiceProfile(V, queOffset4RedRemapSchPri_f, &sch_service_profile, 8);
                        SetDsSchServiceProfile(V, queOffset4YellowRemapSchPri_f, &sch_service_profile, 4);
                        SetDsSchServiceProfile(V, queOffset5GreenRemapSchPri_f, &sch_service_profile, 7);
                        SetDsSchServiceProfile(V, queOffset5RedRemapSchPri_f, &sch_service_profile, 8);
                        SetDsSchServiceProfile(V, queOffset5YellowRemapSchPri_f, &sch_service_profile, 5);
                        SetDsSchServiceProfile(V, queOffset6GreenRemapSchPri_f, &sch_service_profile, 7);
                        SetDsSchServiceProfile(V, queOffset6RedRemapSchPri_f, &sch_service_profile, 8);
                        SetDsSchServiceProfile(V, queOffset6YellowRemapSchPri_f, &sch_service_profile, 6);
                        SetDsSchServiceProfile(V, queOffset7GreenRemapSchPri_f, &sch_service_profile, 7);
                        SetDsSchServiceProfile(V, queOffset7RedRemapSchPri_f, &sch_service_profile, 8);
                        SetDsSchServiceProfile(V, queOffset7YellowRemapSchPri_f, &sch_service_profile, 7);

                        if ((0 == p_gg_queue_master[lchip]->enq_mode) && (sub_grp % 4 > 1) && (sub_grp*4 < p_gg_queue_master[lchip]->queue_base[SYS_QUEUE_TYPE_EXCP]))
                        {  /*CPU TX group*/
                            SetDsSchServiceProfile(V, grpSchPri0RemapChPri_f, &sch_service_profile, 3);
                            SetDsSchServiceProfile(V, grpSchPri1RemapChPri_f, &sch_service_profile, 3);
                            SetDsSchServiceProfile(V, grpSchPri2RemapChPri_f, &sch_service_profile, 3);
                            SetDsSchServiceProfile(V, grpSchPri3RemapChPri_f, &sch_service_profile, 3);
                            SetDsSchServiceProfile(V, grpSchPri4RemapChPri_f, &sch_service_profile, 3);
                            SetDsSchServiceProfile(V, grpSchPri5RemapChPri_f, &sch_service_profile, 3);
                            SetDsSchServiceProfile(V, grpSchPri6RemapChPri_f, &sch_service_profile, 3);
                            SetDsSchServiceProfile(V, grpSchPri7RemapChPri_f, &sch_service_profile, 3);
                        }
                        else
                        {
                            /*8 group Level WFQ --> 4 Channel level WFQ (Port level)*/
                            SetDsSchServiceProfile(V, grpSchPri0RemapChPri_f, &sch_service_profile, 0);
                            SetDsSchServiceProfile(V, grpSchPri1RemapChPri_f, &sch_service_profile, 2);
                            SetDsSchServiceProfile(V, grpSchPri2RemapChPri_f, &sch_service_profile, 2);
                            SetDsSchServiceProfile(V, grpSchPri3RemapChPri_f, &sch_service_profile, 2);
                            SetDsSchServiceProfile(V, grpSchPri4RemapChPri_f, &sch_service_profile, 2);
                            SetDsSchServiceProfile(V, grpSchPri5RemapChPri_f, &sch_service_profile, 2);
                            SetDsSchServiceProfile(V, grpSchPri6RemapChPri_f, &sch_service_profile, 2);
                            SetDsSchServiceProfile(V, grpSchPri7RemapChPri_f, &sch_service_profile, 3);
                        }

                    }
                }

                cmd = DRV_IOW(DsSchServiceProfile_t, DRV_ENTRY_FLAG);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, (SYS_MAX_QUEUE_GROUP_NUM*slice_id + sub_grp), cmd, &sch_service_profile));
            }
        }
    }
    else
    {
        _sys_goldengate_queue_init_networks_port_queue(lchip, &queue_base);
        CTC_ERROR_RETURN(sys_goldengate_queue_sch_init(lchip));
    }

    return CTC_E_NONE;
}

/**
 @brief Mapping a queue in the specific local chip to the given channel.
*/
int32
sys_goldengate_qos_add_group_to_channel(uint8 lchip, uint16 group_id,
                                   uint8 channel,uint8 start_group)
{
    ds_t ds;
    uint32 cmd = 0;
    uint8 channel_tmp = 0;
    uint8 group_tmp = 0;
    sys_queue_group_node_t *p_sys_group_node = NULL;

    sal_memset(ds, 0, sizeof(ds));
    cmd = DRV_IOR(RaGrpMap_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, group_id, cmd, ds));
    channel_tmp = (channel >= 64 ) ? (channel - 64) :channel;
    SetRaGrpMap(V, chanId_f, ds, channel_tmp);
    cmd = DRV_IOW(RaGrpMap_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, group_id, cmd, ds));

    if (start_group)
    {
       cmd = DRV_IOR(RaChanMap_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel, cmd, ds));
        group_tmp = SYS_MAP_GROUPID_TO_SLICE(group_id)?(group_id - 128):group_id;
        SetRaChanMap(V, startGrpId_f, ds, group_tmp*2);
        cmd = DRV_IOW(RaChanMap_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel, cmd, ds));
    }

    SYS_QOS_QUEUE_LOCK(lchip);
    p_sys_group_node = ctc_vector_get(p_gg_queue_master[lchip]->group_vec, group_id);
    if (NULL == p_sys_group_node)
    {
        p_sys_group_node =
            (sys_queue_group_node_t*)mem_malloc(MEM_QUEUE_MODULE, sizeof(sys_queue_group_node_t));

        if (!p_sys_group_node)
        {
            SYS_QOS_QUEUE_UNLOCK(lchip);
            return CTC_E_NO_MEMORY;
        }
        sal_memset(p_sys_group_node,0,sizeof(sys_queue_group_node_t));
        p_sys_group_node->group_id = group_id;
    }
    p_sys_group_node->chan_id       = channel;  /*0~127*/

    ctc_vector_add(p_gg_queue_master[lchip]->group_vec, group_id, p_sys_group_node);
    SYS_QOS_QUEUE_UNLOCK(lchip);
    return CTC_E_NONE;
}
/**
 @brief Remove a queue in the specific local chip from the given channel.
*/
int32
sys_goldengate_qos_remove_group_to_channel(uint8 lchip, uint16 queue_id,uint8 channel)
{
    ds_t ds;
    uint32 cmd = 0;
    uint32 depth = 0;
    uint8 group_id = queue_id / 8;
    uint8 old_chan_id = 0;
    uint8 i = 0;
    channel = SYS_DROP_CHANNEL_ID;

    sal_memset(ds, 0, sizeof(ds));
    cmd = DRV_IOR(RaGrpMap_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, group_id, cmd, ds));
    old_chan_id =  GetRaGrpMap(V, chanId_f, ds);

    sys_goldengate_queue_set_chan_drop_en(lchip, queue_id, old_chan_id, TRUE);
    cmd = DRV_IOR(RaEgrPortCnt_t, RaEgrPortCnt_portCnt_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, old_chan_id, cmd, &depth));
    while(depth)
    {
        sal_task_sleep(1);
        if((i++) >50)
        {
            SYS_QUEUE_DBG_ERROR("Cannot flush queue depth(%d) to Zero \n", depth);
            break;
        }
        cmd = DRV_IOR(RaEgrPortCnt_t, RaEgrPortCnt_portCnt_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, old_chan_id, cmd, &depth));
    }
    sys_goldengate_queue_set_chan_drop_en(lchip, queue_id, old_chan_id, FALSE);
    SetRaGrpMap(V, chanId_f, ds, channel);
    cmd = DRV_IOW(RaGrpMap_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, group_id, cmd, ds));

    return CTC_E_NONE;
}


extern int32
sys_goldengate_get_channel_by_queue_id(uint8 lchip, uint16 queue_id, uint8 *channel)
{
    uint32 field_val               = 0;
    uint32 cmd                     = 0;

    cmd = DRV_IOR(RaGrpMap_t, RaGrpMap_chanId_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, queue_id / 8, cmd, &field_val));
    *channel = (queue_id >= SYS_MAX_QUEUE_NUM)?(field_val + SYS_MAX_CHANNEL_ID):field_val;

    return CTC_E_NONE;
}


extern int32
sys_goldengate_get_channel_by_port(uint8 lchip, uint16 gport, uint8 *channel)
{
    uint16 lport = 0;
    uint32 field_val = 0;
    uint32 cmd = 0;

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);
    cmd = DRV_IOR(DsLinkAggregationPort_t, DsLinkAggregationPort_channelId_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));
    *channel = field_val;

    return CTC_E_NONE;
}
extern int32
sys_goldengate_add_port_to_channel(uint8 lchip, uint16 lport, uint8 channel)
{

    ds_t ds;
    uint32 cmd = 0;
    uint16 chanagg_grp_num = TABLE_MAX_INDEX(DsLinkAggregateChannelGroup_t);

    sal_memset(ds, 0, sizeof(ds));
    cmd = DRV_IOR(DsLinkAggregationPort_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, ds));
    SetDsLinkAggregationPort(V, channelId_f, ds, channel);
    chanagg_grp_num = (SYS_DROP_CHANNEL_ID == (channel%64))?(TABLE_MAX_INDEX(DsLinkAggregateChannelGroup_t)-1):0;
    SetDsLinkAggregationPort(V, linkAggregationChannelGroup_f, ds, chanagg_grp_num);
    cmd = DRV_IOW(DsLinkAggregationPort_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, ds));

    return CTC_E_NONE;
}

extern int32
sys_goldengate_remove_port_from_channel(uint8 lchip, uint16 lport, uint8 channel)
{
    ds_t   ds;
    uint32 cmd = 0;

    channel = SYS_DROP_CHANNEL_ID;

    sal_memset(ds, 0, sizeof(ds));
    cmd = DRV_IOR(DsLinkAggregationPort_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, ds));
    SetDsLinkAggregationPort(V, channelId_f, ds, channel);
    SetDsLinkAggregationPort(V, linkAggregationChannelGroup_f, ds, (TABLE_MAX_INDEX(DsLinkAggregateChannelGroup_t)-1));
    cmd = DRV_IOW(DsLinkAggregationPort_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, ds));

    return CTC_E_NONE;
}

int32
sys_goldengate_get_channel_agg_group(uint8 lchip, uint16 chan_id, uint8* chanagg_id)
{
    uint8 grp_index = 0;
    uint16 mem_index = 0;
    uint16 chanagg_grp_num = 0;
    uint32 cmd = 0;
    uint32 mem_num = 0;
    uint32 mem_base = 0;
    uint32 mem_channel_id = 0;

    chanagg_grp_num = TABLE_MAX_INDEX(DsLinkAggregateChannelGroup_t);
    for (grp_index = 0; grp_index < chanagg_grp_num; grp_index++)
    {
        cmd = DRV_IOR(DsLinkAggregateChannelGroup_t, DsLinkAggregateChannelGroup_channelLinkAggMemNum_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, grp_index, cmd, &mem_num));
        if (mem_num)
        {
            cmd = DRV_IOR(DsLinkAggregateChannelGroup_t, DsLinkAggregateChannelGroup_channelLinkAggMemBase_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, grp_index, cmd, &mem_base));
            for (mem_index = mem_base; mem_index < (mem_base + mem_num); mem_index++)
            {
                cmd = DRV_IOR(DsLinkAggregateChannelMember_t, DsLinkAggregateChannelMember_channelId_f);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, mem_index, cmd, &mem_channel_id));
                if (chan_id == mem_channel_id)
                {
                    *chanagg_id = grp_index;
                    return CTC_E_NONE;
                }
            }
        }
    }

     return CTC_E_NONE;
}

int32
sys_goldengate_add_port_to_channel_agg(uint8 lchip, uint16 lport, uint8 channel_agg)
{
    ds_t ds;
    uint32 cmd = 0;

    SYS_QUEUE_DBG_PARAM("lport = %d, channel agg group = %d!\n", lport, channel_agg);

    sal_memset(ds, 0, sizeof(ds));
    cmd = DRV_IOR(DsLinkAggregationPort_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, ds));
    SetDsLinkAggregationPort(V, linkAggregationChannelGroup_f, ds, channel_agg);
    cmd = DRV_IOW(DsLinkAggregationPort_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, ds));

    return CTC_E_NONE;
}

int32
sys_goldengate_remove_port_from_channel_agg(uint8 lchip, uint16 lport)
{
    ds_t   ds;
    uint32 cmd = 0;

    sal_memset(ds, 0, sizeof(ds));
    cmd = DRV_IOR(DsLinkAggregationPort_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, ds));
    SetDsLinkAggregationPort(V, linkAggregationChannelGroup_f, ds, 0);
    cmd = DRV_IOW(DsLinkAggregationPort_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, ds));

    return CTC_E_NONE;
}


int32
sys_goldengate_add_port_range_to_channel(uint8 lchip, uint16 lport_start, uint16 lport_end, uint8 channel)
{
    uint16 queue_group   = 0;
    uint16 start_group   = 0;
    uint8 group_num     = 0;

    if(SYS_QUEUE_DESTID_ENQ(lchip))
    {

        start_group =  (p_gg_queue_master[lchip]->queue_base[SYS_QUEUE_TYPE_EXTENDER_WITH_Q]
                        + SYS_MAP_DRV_LPORT_TO_SLICE(lport_start) * SYS_MAX_QUEUE_NUM
                        + (lport_start & 0xFF) * p_gg_queue_master[lchip]->queue_num_per_chanel)/SYS_QUEUE_NUM_PER_GROUP;
        group_num   = ((lport_end - lport_start + 1) * p_gg_queue_master[lchip]->queue_num_per_chanel)/SYS_QUEUE_NUM_PER_GROUP;

        for (queue_group = start_group; queue_group < (start_group + group_num); queue_group++)
        {
            CTC_ERROR_RETURN(sys_goldengate_qos_add_group_to_channel(lchip,  queue_group, channel,
                                                                    (queue_group == start_group)));
        }
    }

    return CTC_E_NONE;
}


int32
sys_goldengate_remove_port_range_to_channel(uint8 lchip, uint16 lport_start, uint16 lport_end, uint8 channel)
{
    uint16 queue_group   = 0;
    uint16 start_group   = 0;
    uint8 group_num     = 0;
    uint16 queue_id     = 0;

    if(SYS_QUEUE_DESTID_ENQ(lchip))
    {

        start_group =  (p_gg_queue_master[lchip]->queue_base[SYS_QUEUE_TYPE_EXTENDER_WITH_Q]
                        + SYS_MAP_DRV_LPORT_TO_SLICE(lport_start) * SYS_MAX_QUEUE_NUM
                        + (lport_start & 0xFF) * p_gg_queue_master[lchip]->queue_num_per_chanel) / SYS_QUEUE_NUM_PER_GROUP;
        group_num   = ((lport_end - lport_start + 1) * p_gg_queue_master[lchip]->queue_num_per_chanel)/SYS_QUEUE_NUM_PER_GROUP;

        for (queue_group = start_group; queue_group < (start_group + group_num); queue_group++)
        {
            queue_id = queue_group*SYS_QUEUE_NUM_PER_GROUP;
            CTC_ERROR_RETURN(sys_goldengate_qos_remove_group_to_channel(lchip,  queue_id, channel));
        }
    }
    return CTC_E_NONE;
}


/**
 @brief set priority color map to queue select and drop_precedence.
*/
int32
sys_goldengate_set_priority_queue_map(uint8 lchip, ctc_qos_pri_map_t* p_queue_pri_map)
{

    uint32 index = 0;
    uint32 cmd = 0;
    ds_t ds;

    CTC_MAX_VALUE_CHECK(p_queue_pri_map->priority, SYS_QOS_CLASS_PRIORITY_MAX);
    CTC_MAX_VALUE_CHECK(p_queue_pri_map->queue_select, SYS_QOS_CLASS_PRIORITY_MAX);
    sal_memset(ds, 0, sizeof(ds));

    index = (p_queue_pri_map->priority << 2) | p_queue_pri_map->color;

    cmd = DRV_IOR(DsQueueSelectMap_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, ds));

    SetDsQueueSelectMap(V,queueSelect_f, ds, p_queue_pri_map->queue_select);
    SetDsQueueSelectMap(V,dropPrecedence_f, ds, p_queue_pri_map->drop_precedence);
    cmd = DRV_IOW(DsQueueSelectMap_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, ds));


    return CTC_E_NONE;

}

int32
_sys_goldengate_queue_select_init(uint8 lchip)
{
    uint8 priority = 0;
    uint8 color = 0;
    ctc_qos_pri_map_t qos_pri_map;

    sal_memset(&qos_pri_map, 0, sizeof(ctc_qos_pri_map_t));

    for (priority = 0; priority <= SYS_QOS_CLASS_PRIORITY_MAX; priority++)
    {
        for (color = 0; color < 4; color++)
        {
            qos_pri_map.priority = priority;
            qos_pri_map.color = color;
            qos_pri_map.queue_select = priority;
            qos_pri_map.drop_precedence = (color + 3) % 4;

            CTC_ERROR_RETURN(sys_goldengate_set_priority_queue_map(lchip, &qos_pri_map));
        }
    }

    return CTC_E_NONE;

}

/**
 @brief reserved channel drop init
*/
int32
_sys_goldengate_rsv_channel_drop_init(uint8 lchip)
{

    uint32 cmd = 0;
    ds_t ds;

    sal_memset(ds, 0, sizeof(ds));

    SetQMgrReservedChannelRange(V, reservedChannelValid0_f, ds, 1);
    SetQMgrReservedChannelRange(V, reservedChannelValid1_f, ds, 1);
    SetQMgrReservedChannelRange(V, reservedChannelMax0_f, ds, SYS_DROP_CHANNEL_ID);
    SetQMgrReservedChannelRange(V, reservedChannelMin0_f, ds, SYS_DROP_CHANNEL_ID);
    SetQMgrReservedChannelRange(V, reservedChannelMax1_f, ds, SYS_RSV_CHANNEL_ID);
    SetQMgrReservedChannelRange(V, reservedChannelMin1_f, ds, SYS_RSV_CHANNEL_ID)

    cmd = DRV_IOW(QMgrReservedChannelRange_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));

    return CTC_E_NONE;
}

int32
sys_goldengate_set_dma_channel_drop_en(uint8 lchip, bool enable)
{
    uint32 cmd = 0;
    uint8 channel_min = 0;
    uint8 channel_max = 0;
    QMgrReservedChannelRange_m qmgr_rsv_channel_range;

    sal_memset(&qmgr_rsv_channel_range, 0, sizeof(QMgrReservedChannelRange_m));

    channel_min = enable ? SYS_DMA_CHANNEL_ID : SYS_RSV_CHANNEL_ID;
    channel_max = enable ? SYS_DMA_CHANNEL_RX3 : SYS_RSV_CHANNEL_ID;

    cmd = DRV_IOR(QMgrReservedChannelRange_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &qmgr_rsv_channel_range));

    SetQMgrReservedChannelRange(V, reservedChannelMax1_f, &qmgr_rsv_channel_range, channel_max);
    SetQMgrReservedChannelRange(V, reservedChannelMin1_f, &qmgr_rsv_channel_range, channel_min);

    cmd = DRV_IOW(QMgrReservedChannelRange_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &qmgr_rsv_channel_range));

    return CTC_E_NONE;
}

int32
sys_goldengate_queue_set_stats_enable(uint8 lchip, ctc_qos_queue_stats_t* p_stats)
{
    uint8 channel_id = 0, channel_id_end = 0, pcie_sel = 0;
    uint32 cmd = 0;
    uint32 chan_bitmap[4];
    QMgrStatsUpdCtl_m q_mgr_stats_upd_ctl;

    sal_memset(&q_mgr_stats_upd_ctl, 0, sizeof(q_mgr_stats_upd_ctl));

    SYS_QUEUE_DBG_FUNC();
    CTC_ERROR_RETURN(sys_goldengate_chip_get_pcie_select(lchip, &pcie_sel));

    if(p_stats->queue.queue_type == CTC_QUEUE_TYPE_EXCP_CPU)
    {
        channel_id = (pcie_sel?64:0)+SYS_DMA_CHANNEL_ID;
        channel_id_end = (pcie_sel?64:0)+SYS_DMA_CHANNEL_RX3;
    }
    else
    {
        CTC_ERROR_RETURN(sys_goldengate_get_channel_by_port(lchip, p_stats->queue.gport, &channel_id));
        channel_id_end = channel_id;
    }
    SYS_QUEUE_DBG_INFO("channel_id = %d, stats_en =%d\n", channel_id, p_stats->stats_en);

    cmd = DRV_IOR(QMgrStatsUpdCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &q_mgr_stats_upd_ctl));
    GetQMgrStatsUpdCtl(A, statsUpdEn_f, &q_mgr_stats_upd_ctl, chan_bitmap);



    for ( ; channel_id <= channel_id_end; channel_id++ )
    {
        if (p_stats->stats_en)
        {
            CTC_BIT_SET(chan_bitmap[(channel_id >> 5)&0x3], (channel_id&0x1F));
        }
        else
        {
            CTC_BIT_UNSET(chan_bitmap[(channel_id >> 5)&0x3], (channel_id&0x1F));
        }
        SetQMgrStatsUpdCtl(A, statsUpdEn_f, &q_mgr_stats_upd_ctl, chan_bitmap);
    }
    cmd = DRV_IOW(QMgrStatsUpdCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &q_mgr_stats_upd_ctl));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_queue_get_stats_enable(uint8 lchip, uint8 channel_id, uint8* p_enable)
{
    uint32 cmd = 0;
    uint32 chan_bitmap[4];
    QMgrStatsUpdCtl_m q_mgr_stats_upd_ctl;

    sal_memset(&q_mgr_stats_upd_ctl, 0, sizeof(q_mgr_stats_upd_ctl));

    SYS_QUEUE_DBG_FUNC();

    cmd = DRV_IOR(QMgrStatsUpdCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &q_mgr_stats_upd_ctl));
    GetQMgrStatsUpdCtl(A, statsUpdEn_f, &q_mgr_stats_upd_ctl, chan_bitmap);

    *p_enable = CTC_IS_BIT_SET(chan_bitmap[(channel_id >> 5)&0x3], (channel_id&0x1F));

    return CTC_E_NONE;
}


int32
sys_goldengate_qos_set_cpu_eth_port(uint8 lchip, uint16 gport)
{

    uint16 lport = 0;
    uint32 value = 0;
    ds_t ds;
    uint32 cmd = 0;
    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport)
    sal_memset(ds, 0, sizeof(ds));
    cmd = DRV_IOR(DsLinkAggregationPort_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, ds));
    GetDsLinkAggregationPort(A, channelId_f, ds, &value);

    SYS_QOS_QUEUE_LOCK(lchip);
    p_gg_queue_master[lchip]->cpu_eth_chan_id  = value;
    SYS_QOS_QUEUE_UNLOCK(lchip);
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_qos_check_pkt_adjust_profile(uint8 lchip, uint8 channel)
{
    uint32 field_val = 0;
    uint32 cmd = 0;
    uint8 profile_id = 0;
    uint8 index = 0;

    cmd = DRV_IOR(RaEgrPortCtl_t, RaEgrPortCtl_netPktAdjIndex_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel, cmd, &field_val));
    profile_id = field_val;
    for (index = 0; index < 128; index++)
    {
        cmd = DRV_IOR(RaEgrPortCtl_t, RaEgrPortCtl_netPktAdjIndex_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &field_val));
        if(field_val == profile_id && index != channel)
        {
            return CTC_E_NONE;
        }
    }

    field_val = 0;
    cmd = DRV_IOW(QMgrNetPktAdj_t, QMgrNetPktAdj_netPktAdjVal0_f + profile_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    return CTC_E_NONE;
}

int32
sys_goldengate_queue_set_queue_pkt_adjust(uint8 lchip, ctc_qos_queue_pkt_adjust_t* p_pkt)
{
    uint32 cmd = 0;
    uint32 field_val = 0;
    uint8 index = 0;
    uint8 new_index = 0xFF;
    uint8 old_index = 0;
    uint8 exist = 0;
    uint8 channel = 0;
    uint16 queue_id = 0;

    SYS_QUEUE_DBG_FUNC();

    SYS_QOS_QUEUE_LOCK(lchip);
    CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(sys_goldengate_queue_get_queue_id(lchip,  &p_pkt->queue, &queue_id));
    SYS_QOS_QUEUE_UNLOCK(lchip);

    CTC_ERROR_RETURN(sys_goldengate_get_channel_by_queue_id(lchip, queue_id, &channel));

    SYS_QUEUE_DBG_INFO("channel id = %d\n", channel);
    if(p_pkt->adjust_len == 0)
    {
        cmd = DRV_IOW(QMgrNetPktAdj_t, QMgrNetPktAdj_netPktAdjVal0_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        CTC_ERROR_RETURN(_sys_goldengate_qos_check_pkt_adjust_profile(lchip, channel));
        cmd = DRV_IOW(RaEgrPortCtl_t, RaEgrPortCtl_netPktAdjIndex_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel, cmd, &field_val));

        return CTC_E_NONE;
    }

    for (index = 0; index < 4; index++)
    {
        cmd = DRV_IOR(QMgrNetPktAdj_t, QMgrNetPktAdj_netPktAdjVal0_f+ index);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

        if (field_val == p_pkt->adjust_len)
        {
            new_index = index;
            exist = 1;
            break;
        }

        if (0 == field_val && index != 0)
        {
            new_index = index;
        }
    }

    if (new_index == 0xFF)
    {
        return CTC_E_NO_RESOURCE;
    }

    cmd = DRV_IOR(RaEgrPortCtl_t, RaEgrPortCtl_netPktAdjIndex_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel, cmd, &field_val));
    old_index = field_val;

    if (new_index == old_index)
    {
        return CTC_E_NONE;
    }

    /*new index*/
    if (!exist)
    {
        CTC_ERROR_RETURN(_sys_goldengate_qos_check_pkt_adjust_profile(lchip, channel));
        field_val = p_pkt->adjust_len;
        cmd = DRV_IOW(QMgrNetPktAdj_t, QMgrNetPktAdj_netPktAdjVal0_f + new_index );
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    }

    field_val = new_index;
    cmd = DRV_IOW(RaEgrPortCtl_t, RaEgrPortCtl_netPktAdjIndex_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel, cmd, &field_val));

    SYS_QUEUE_DBG_INFO("channel id = %d, NetPktAdjIndex:%d, value:%d\n", channel,  new_index, p_pkt->adjust_len);

    return CTC_E_NONE;
}


int32
sys_goldengate_qos_queue_set(uint8 lchip, ctc_qos_queue_cfg_t* p_que_cfg)
{
    uint8 sub_queue_id = 0;
    SYS_QUEUE_DBG_FUNC();
    SYS_QOS_QUEUE_INIT_CHECK(lchip);

    switch (p_que_cfg->type)
    {
    case CTC_QOS_QUEUE_CFG_PRI_MAP:
        CTC_ERROR_RETURN(
            sys_goldengate_set_priority_queue_map(lchip, &p_que_cfg->value.pri_map));
        break;

    case CTC_QOS_QUEUE_CFG_QUEUE_STATS_EN:
        CTC_ERROR_RETURN(
            sys_goldengate_queue_set_stats_enable(lchip, &p_que_cfg->value.stats));
        break;
    case CTC_QOS_QUEUE_CFG_SCHED_WDRR_MTU:

        CTC_VALUE_RANGE_CHECK(p_que_cfg->value.value_32, 1, 0xFFFF);
        SYS_QOS_QUEUE_LOCK(lchip);
        p_gg_queue_master[lchip]->wrr_weight_mtu = p_que_cfg->value.value_32;
        SYS_QOS_QUEUE_UNLOCK(lchip);
        break;

    case CTC_QOS_QUEUE_CFG_QUEUE_REASON_MAP:
        {
            ctc_qos_queue_cpu_reason_map_t* p_reason_map = &p_que_cfg->value.reason_map;
            sub_queue_id = p_reason_map->reason_group * SYS_GG_MAX_QNUM_PER_GROUP + p_reason_map->queue_id;
            if((p_gg_queue_master[lchip]->monitor_drop_en == 1) &&
                p_gg_queue_master[lchip]->cpu_reason[CTC_PKT_CPU_REASON_QUEUE_DROP_PKT].sub_queue_id == sub_queue_id)
            {
                return CTC_E_INVALID_PARAM;
            }
            if (sub_queue_id >= p_gg_queue_master[lchip]->queue_num_for_cpu_reason)
            { /*Exceed Max Cpu Reason Group*/
                return CTC_E_INVALID_PARAM;
            }
            SYS_QOS_QUEUE_LOCK(lchip);
            CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(sys_goldengate_cpu_reason_set_map(lchip,
                                                              p_reason_map->cpu_reason,
                                                              p_reason_map->queue_id,
                                                              p_reason_map->reason_group));
            SYS_QOS_QUEUE_UNLOCK(lchip);

        }
        break;

    case CTC_QOS_QUEUE_CFG_QUEUE_REASON_DEST:
        {

            ctc_qos_queue_cpu_reason_dest_t* p_reason_dest = &p_que_cfg->value.reason_dest;
            SYS_QOS_QUEUE_LOCK(lchip);
            if (CTC_PKT_CPU_REASON_TO_NHID == p_reason_dest->dest_type)
            {
                CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(sys_goldengate_cpu_reason_set_dest(lchip, p_reason_dest->cpu_reason,
                                                                    p_reason_dest->dest_type,
                                                                    p_reason_dest->nhid));
            }
            else
            {
                CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(sys_goldengate_cpu_reason_set_dest(lchip, p_reason_dest->cpu_reason,
                                                                    p_reason_dest->dest_type,
                                                                    p_reason_dest->dest_port));

            }
            SYS_QOS_QUEUE_UNLOCK(lchip);
        }
        break;
    case CTC_QOS_QUEUE_CFG_QUEUE_REASON_MISC:
        {

            ctc_qos_group_cpu_reason_misc_t* p_reason_misc = &p_que_cfg->value.reason_misc;


             CTC_ERROR_RETURN(sys_goldengate_cpu_reason_set_misc_param(lchip, p_reason_misc->cpu_reason,
                                                                    p_reason_misc->truncation_en));



        }
        break;
    case CTC_QOS_QUEUE_CFG_LENGTH_ADJUST:  /*GG will use nexthop adjust length index*/
        CTC_ERROR_RETURN(sys_goldengate_queue_set_queue_pkt_adjust(lchip, &p_que_cfg->value.pkt));
        break;
	case CTC_QOS_QUEUE_CFG_QUEUE_NUM:
    case CTC_QOS_QUEUE_CFG_QUEUE_REASON_MODE:

    case CTC_QOS_QUEUE_CFG_QUEUE_REASON_PRIORITY:
    case CTC_QOS_QUEUE_CFG_SERVICE_BIND:
    default:
        return CTC_E_FEATURE_NOT_SUPPORT;
    }

    return CTC_E_NONE;
}

extern int32
sys_goldengate_qos_set_monitor_drop_queue_id(uint8 lchip,  ctc_qos_queue_drop_monitor_t* drop_monitor)
{

    uint32 cmd = 0;
    uint32 dst_gport= 0;
    uint32 src_gport= 0;
    QMgrEnqDropXlateCtl_m qmgr_enqdrop_ctl;
    uint8 chan_id = 0;
    uint16 lport = 0;
    uint32 array_val[4] = {0};
    uint16 sub_queue_id = 0;
    uint16 reason = 0;
    uint8 flag = 0;
    uint8 group_id = 0;
    uint8 queue_id = 0;
    uint8 pcie_sel = 0;
    uint16 index = 0;
    uint32 field_val = 0;
    BufferRetrieveCtl_m buffer_retrieve_ctl;
    ctc_qos_sched_queue_t p_sched;

    sal_memset(&p_sched, 0, sizeof(p_sched));
    sal_memset(&qmgr_enqdrop_ctl, 0, sizeof(qmgr_enqdrop_ctl));
    sal_memset(&buffer_retrieve_ctl, 0, sizeof(buffer_retrieve_ctl));
    dst_gport = drop_monitor->dst_gport;
    if (!CTC_IS_CPU_PORT(dst_gport))
    {
        return CTC_E_INVALID_PARAM;
    }
    src_gport = drop_monitor->src_gport;
    if(src_gport == dst_gport)
    {
        return CTC_E_INVALID_PARAM;
    }
    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(src_gport, lchip, lport);
    chan_id = SYS_GET_CHANNEL_ID(lchip, src_gport);
    if (chan_id == 0xFF)
    {
        return CTC_E_INVALID_PARAM;
    }
    if(drop_monitor->enable >= 1)
    {
        for(sub_queue_id = 0; sub_queue_id < p_gg_queue_master[lchip]->queue_num_for_cpu_reason; sub_queue_id ++)
        {
            for(reason = 1; reason < CTC_PKT_CPU_REASON_MAX_COUNT; reason ++)
            {
                if(p_gg_queue_master[lchip]->cpu_reason[reason].sub_queue_id == sub_queue_id)
                {
                    if(reason == CTC_PKT_CPU_REASON_QUEUE_DROP_PKT)
                    {
                        flag = 1;
                    }
                    else
                    {
                        flag = 0;
                        break;
                    }
                }
                else if(reason == CTC_PKT_CPU_REASON_MAX_COUNT - 1)
                {
                    flag = 1;
                    break;
                }
            }
            if(flag == 1)
            {
                break;
            }
        }
        if(flag == 0)
        {
            return CTC_E_INVALID_PARAM;
        }
        reason = CTC_PKT_CPU_REASON_QUEUE_DROP_PKT;
        group_id = sub_queue_id / SYS_GG_MAX_QNUM_PER_GROUP;
        queue_id = sub_queue_id % SYS_GG_MAX_QNUM_PER_GROUP;
        CTC_ERROR_RETURN(sys_goldengate_cpu_reason_set_map(lchip, reason, queue_id, group_id));
        CTC_ERROR_RETURN(sys_goldengate_chip_get_pcie_select(lchip, &pcie_sel));
        sub_queue_id = pcie_sel?(sub_queue_id + SYS_QUEUE_BASE_TYPE_EXCP+1024) : (sub_queue_id + SYS_QUEUE_BASE_TYPE_EXCP);
        p_sched.cfg_type = CTC_QOS_SCHED_CFG_EXCEED_CLASS;
        p_sched.exceed_class = 0;
        p_sched.queue.cpu_reason = CTC_PKT_CPU_REASON_QUEUE_DROP_PKT;
        p_sched.queue.queue_id = 0;
        p_sched.queue.queue_type = CTC_QUEUE_TYPE_EXCP_CPU;
        CTC_ERROR_RETURN(sys_goldengate_queue_sch_set_queue_sched(lchip, &p_sched));

        cmd = DRV_IOR(BufferRetrieveCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &buffer_retrieve_ctl))
        SetBufferRetrieveCtl(V, dropOnSpanRsvQueId_f , &buffer_retrieve_ctl, sub_queue_id &0x3FF);
        SetBufferRetrieveCtl(V, dropOnSpanEn_f , &buffer_retrieve_ctl, 1);
        SetBufferRetrieveCtl(V, dropOnSpanNextHopPtr_f , &buffer_retrieve_ctl, CTC_PKT_CPU_REASON_QUEUE_DROP_PKT << 4);
        cmd = DRV_IOW(BufferRetrieveCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &buffer_retrieve_ctl));
        cmd = DRV_IOW(BufferRetrieveCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 1, cmd, &buffer_retrieve_ctl));

        p_gg_queue_master[lchip]->monitor_drop_en = 1;
        cmd = DRV_IOR(QMgrEnqDropXlateCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &qmgr_enqdrop_ctl));
        GetQMgrEnqDropXlateCtl(A, spanEnBitMap_f, &qmgr_enqdrop_ctl, array_val);
        CTC_BIT_SET(array_val[chan_id / 32], chan_id&0x1F);
        SetQMgrEnqDropXlateCtl(V, spanDropEn_f , &qmgr_enqdrop_ctl, 1);
        SetQMgrEnqDropXlateCtl(A, spanEnBitMap_f, &qmgr_enqdrop_ctl, array_val);
        SetQMgrEnqDropXlateCtl(V, rsvQueId_f , &qmgr_enqdrop_ctl, sub_queue_id);
        field_val = p_gg_queue_master[lchip]->egs_pool[3].egs_congest_level_num ? 3 :0;
        SetQMgrEnqDropXlateCtl(V, rsvSc_f , &qmgr_enqdrop_ctl, field_val);
        cmd = DRV_IOW(QMgrEnqDropXlateCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &qmgr_enqdrop_ctl));
    }
    else
    {
        cmd = DRV_IOR(QMgrEnqDropXlateCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &qmgr_enqdrop_ctl));
        GetQMgrEnqDropXlateCtl(A, spanEnBitMap_f, &qmgr_enqdrop_ctl, array_val);
        CTC_BIT_UNSET(array_val[chan_id / 32], chan_id&0x1F);
        SetQMgrEnqDropXlateCtl(A, spanEnBitMap_f, &qmgr_enqdrop_ctl, array_val);
        cmd = DRV_IOW(QMgrEnqDropXlateCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &qmgr_enqdrop_ctl));
        if(array_val[0] != 0 || array_val[1] != 0 ||array_val[2] != 0 ||array_val[3] != 0)
        {
            return CTC_E_NONE;
        }
        /*2.delete span dest ,if spanCnt is not 0 can not delete span dest*/
        cmd = DRV_IOR(QMgrEnqDropXlateCtl_t, QMgrEnqDropXlateCtl_rsvQueCnt_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        while (field_val)
        {
            sal_task_sleep(20);
            if ((index++) > 1000)
            {
                return CTC_E_INVALID_PARAM;
            }
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        }
        p_gg_queue_master[lchip]->monitor_drop_en = 0;
        cmd = DRV_IOR(BufferRetrieveCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &buffer_retrieve_ctl))
        SetBufferRetrieveCtl(V, dropOnSpanEn_f , &buffer_retrieve_ctl, 0);
        cmd = DRV_IOW(BufferRetrieveCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &buffer_retrieve_ctl));
        cmd = DRV_IOW(BufferRetrieveCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 1, cmd, &buffer_retrieve_ctl));
    }

    return CTC_E_NONE;
}
extern int32
sys_goldengate_qos_get_monitor_drop_queue_id(uint8 lchip,  ctc_qos_queue_drop_monitor_t* drop_monitor)
{
    uint32 cmd = 0;
    uint32 src_gport= 0;
    QMgrEnqDropXlateCtl_m qmgr_enqdrop_ctl;
    uint8 chan_id = 0;
    uint16 lport = 0;
    uint32 array_val[4] = {0};

    sal_memset(&qmgr_enqdrop_ctl, 0, sizeof(qmgr_enqdrop_ctl));

    src_gport = drop_monitor->src_gport;
    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(src_gport, lchip, lport);
    chan_id = SYS_GET_CHANNEL_ID(lchip, src_gport);
    if (chan_id == 0xFF)
    {
        return CTC_E_INVALID_PARAM;
    }
    cmd = DRV_IOR(QMgrEnqDropXlateCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &qmgr_enqdrop_ctl));
    GetQMgrEnqDropXlateCtl(A, spanEnBitMap_f, &qmgr_enqdrop_ctl, array_val);
    drop_monitor->enable = CTC_IS_BIT_SET(array_val[chan_id / 32], chan_id&0x1F);
    if(drop_monitor->enable == 1)
    {
        drop_monitor->dst_gport = 0x10000000;
    }

    return CTC_E_NONE;
}
/**
 @brief Get service queue statistics.
*/
int32
sys_goldengate_queue_stats_query(uint8 lchip, ctc_qos_queue_stats_t* p_stats_param)
{
    sys_stats_queue_t stats;
    ctc_qos_queue_stats_info_t* p_stats = NULL;
    uint16 queue_id = 0;

    SYS_QUEUE_DBG_FUNC();
    SYS_QOS_QUEUE_INIT_CHECK(lchip);

    SYS_QOS_QUEUE_LOCK(lchip);
    CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(sys_goldengate_queue_get_queue_id(lchip,
                                                       &p_stats_param->queue,
                                                       &queue_id));
    SYS_QOS_QUEUE_UNLOCK(lchip);

    SYS_QUEUE_DBG_INFO("Queue id = %d\n", queue_id);

    p_stats = &p_stats_param->stats;

    CTC_ERROR_RETURN(sys_goldengate_stats_get_queue_stats(lchip, queue_id, &stats));
    p_stats->drop_packets = stats.queue_drop_pkts;
    p_stats->drop_bytes   = stats.queue_drop_bytes;
    p_stats->deq_packets  = stats.queue_deq_pkts;
    p_stats->deq_bytes    = stats.queue_deq_bytes;

    return CTC_E_NONE;
}

/**
 @brief Get service queue statistics.
*/
int32
sys_goldengate_queue_stats_clear(uint8 lchip, ctc_qos_queue_stats_t* p_stats_param)
{
    uint16 queue_id = 0;

    SYS_QUEUE_DBG_FUNC();
    SYS_QOS_QUEUE_INIT_CHECK(lchip);

    SYS_QOS_QUEUE_LOCK(lchip);
    CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(sys_goldengate_queue_get_queue_id(lchip, &p_stats_param->queue,
                                                       &queue_id));
    SYS_QOS_QUEUE_UNLOCK(lchip);

	SYS_QUEUE_DBG_INFO("Queue id = %d\n", queue_id);

    CTC_ERROR_RETURN(sys_goldengate_stats_clear_queue_stats(lchip, queue_id));

    return CTC_E_NONE;
}


int32
sys_goldengate_qos_queue_dump_status(uint8 lchip)
{
    char str_networks_port[64]  = {0};
    uint8 group_num = 0;

    group_num = (p_gg_queue_master[lchip]->enq_mode == 2) ? 1 : 2;
    SYS_QUEUE_DBG_DUMP("---------------------Queue allocation---------------------\n");
    SYS_QUEUE_DBG_DUMP("%-30s: %d\n", "Queue number per port", p_gg_queue_master[lchip]->queue_num_per_chanel);
    SYS_QUEUE_DBG_DUMP("%-30s: %d\n", "Enqueue mode", p_gg_queue_master[lchip]->enq_mode);
    SYS_QUEUE_DBG_DUMP("----------------------------------------------------------\n");
    SYS_QUEUE_DBG_DUMP("%-15s %-10s %-10s %-10s %-8s\n", "type", "port-id", "QSelType", "Qbase", "SubQNum");

    SYS_QUEUE_DBG_DUMP("-----------------------------------------------------------\n");
    if (SYS_QUEUE_DESTID_ENQ(lchip))
    {
        sal_sprintf(str_networks_port, "%s%d", "0~",
                    ((SYS_MAX_QUEUE_NUM - p_gg_queue_master[lchip]->queue_num_for_cpu_reason - SYS_QUEUE_RSV_QUEUE_NUM)
                    / p_gg_queue_master[lchip]->queue_num_per_chanel) - 1 ) ;
    }
    else
    {
        sal_sprintf(str_networks_port, "%s", "0~47");
    }
    SYS_QUEUE_DBG_DUMP("%-15s %-10s %-10d %-10d %8d\n", "network port", str_networks_port,
                       SYS_QUEUE_DESTID_ENQ(lchip)? SYS_QSEL_TYPE_EXTERN_PORT_WITH_Q : SYS_QSEL_TYPE_REGULAR,
                       p_gg_queue_master[lchip]->queue_base[SYS_QUEUE_TYPE_NORMAL],
                       p_gg_queue_master[lchip]->queue_num_per_chanel);

    SYS_QUEUE_DBG_DUMP("%-15s %-11d%-10d %-10d %8d\n", "drop port", SYS_RSV_PORT_DROP_ID, SYS_QSEL_TYPE_RSV_PORT,
                       p_gg_queue_master[lchip]->queue_base[SYS_QUEUE_TYPE_RSV_PORT] + SYS_QUEUE_NUM_PER_GROUP*0,
                       SYS_QUEUE_NUM_PER_GROUP);
    SYS_QUEUE_DBG_DUMP("%-15s %-10s %-10d %-10d %8d\n", "eloop", "alloc", SYS_QSEL_TYPE_RSV_PORT,
                       p_gg_queue_master[lchip]->queue_base[SYS_QUEUE_TYPE_RSV_PORT] + SYS_QUEUE_NUM_PER_GROUP*1,
                       SYS_QUEUE_NUM_PER_GROUP);
    SYS_QUEUE_DBG_DUMP("%-15s %-10s %-10d %-10d %8d\n", "iloop", "alloc", SYS_QSEL_TYPE_RSV_PORT,
                       p_gg_queue_master[lchip]->queue_base[SYS_QUEUE_TYPE_RSV_PORT] + SYS_QUEUE_NUM_PER_GROUP*2,
                       SYS_QUEUE_NUM_PER_GROUP*group_num);

    SYS_QUEUE_DBG_DUMP("%-15s %-10s %-10d %-10d %8d\n", "Rx OAM", "-", SYS_QSEL_TYPE_RSV_PORT,
                       p_gg_queue_master[lchip]->queue_base[SYS_QUEUE_TYPE_OAM], SYS_QUEUE_NUM_PER_GROUP);

    SYS_QUEUE_DBG_DUMP("%-15s %-10s %-10d %-10d %8d\n", "Local CPU(DMA)", "-", SYS_QSEL_TYPE_EXCP_CPU,
                       p_gg_queue_master[lchip]->queue_base[SYS_QUEUE_TYPE_EXCP], p_gg_queue_master[lchip]->queue_num_for_cpu_reason);
    SYS_QUEUE_DBG_DUMP("%-15s %-10s %-10d %-10s %8d\n", "Local CPU(ETH)", "specific", SYS_QSEL_TYPE_EXCP_CPU_BY_ETH,
                       "-",
                       p_gg_queue_master[lchip]->queue_num_per_chanel);

    SYS_QUEUE_DBG_DUMP("\n");
    return CTC_E_NONE;

}

int32
sys_goldengate_qos_dump_status(uint8 lchip)
{
    SYS_QOS_QUEUE_INIT_CHECK(lchip);

    SYS_QOS_QUEUE_LOCK(lchip);
    sys_goldengate_qos_policer_dump_status(lchip);
    sys_goldengate_qos_queue_dump_status(lchip);
    sys_goldengate_qos_shape_dump_status(lchip);
    sys_goldengate_qos_sched_dump_status(lchip);
    sys_goldengate_qos_drop_dump_status(lchip);
    SYS_QOS_QUEUE_UNLOCK(lchip);
	return CTC_E_NONE;

}

int32
sys_goldengate_qos_queue_dump(uint8 lchip, uint16 start, uint16 end, uint8 detail)
{
    uint16 queue_id = 0;
    uint8 que_offset = 0;
    uint32 cmd;
    uint32 value;
    uint8 stats_en = 0;
    sys_queue_node_t* p_queue_node = NULL;
    sys_queue_group_node_t* p_group_node = NULL;
    sys_queue_group_shp_node_t* p_grp_shp = NULL;
    uint32 token_thrd = 0;
    uint32 token_rate = 0;
    uint8 is_pps = 0;
    uint8 group_mode = 0;

    SYS_QOS_QUEUE_INIT_CHECK(lchip);

    SYS_QUEUE_DBG_DUMP("\nQueue information:\n");
    SYS_QUEUE_DBG_DUMP("%-7s %-7s %-7s %-7s %-8s %-7s\n", "queue", "offset", "channel", "group","stats-en","shp-en");
    SYS_QUEUE_DBG_DUMP("---------------------------------------------------------\n");

    SYS_QOS_QUEUE_LOCK(lchip);
    for (queue_id = start; queue_id <= end && queue_id < 2048; queue_id++)
    {
        p_queue_node = ctc_vector_get(p_gg_queue_master[lchip]->queue_vec,queue_id);
        if (NULL == p_queue_node)
        {
            que_offset = queue_id%8;
            continue;
        }
        else
        {
            que_offset = p_queue_node->offset;
        }

        cmd = DRV_IOR(RaGrpMap_t, RaGrpMap_chanId_f);
        CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(DRV_IOCTL(lchip, queue_id/8, cmd, &value));

        value = (queue_id > 1023) ? (value +64) : value;  /*0~127*/

        _sys_goldengate_queue_get_stats_enable(lchip, value, &stats_en);

        SYS_QUEUE_DBG_DUMP("%-7d %-7d %-7d %-7d %-8d %-7d\n", queue_id, que_offset, value,queue_id/8,
                        stats_en,p_queue_node->shp_en);
    }
    if(start == end)
    {
        queue_id = start;
    }
    if ((start == end) && p_queue_node )
    {

        p_group_node = ctc_vector_get(p_gg_queue_master[lchip]->group_vec, queue_id / 8);
        if (NULL == p_group_node)
        {
            SYS_QOS_QUEUE_UNLOCK(lchip);
            return CTC_E_NONE;
        }
        is_pps = sys_goldengate_queue_shp_get_channel_pps_enbale(lchip, p_group_node->chan_id);

        SYS_QUEUE_DBG_DUMP("\nQueue shape:\n");
        SYS_QUEUE_DBG_DUMP("-------------------------------\n");

        SYS_QUEUE_DBG_DUMP("%-30s: %s\n","Shape mode",is_pps?"PPS":"BPS");

        CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(sys_goldengate_queue_get_grp_mode(lchip, queue_id / 8, &group_mode));

        if (group_mode)
        {
            p_grp_shp = &p_group_node->grp_shp[0];
        }
        else
        {
            p_grp_shp = &p_group_node->grp_shp[(queue_id / 4)%2];
        }

        if(!p_queue_node->shp_en || p_grp_shp->c_bucket[que_offset].cir_type == SYS_QUEUE_GRP_SHP_CIR_TYPE_CIR_FAIL)
        {
            SYS_QUEUE_DBG_DUMP("No CIR Bucket, all packet always mark as yellow!\n" );
        }
        else if (p_grp_shp->c_bucket[que_offset].cir_type == SYS_QUEUE_GRP_SHP_CIR_TYPE_CIR_USER )
        {
            sys_goldengate_qos_map_token_rate_hw_to_user(lchip, is_pps, p_grp_shp->c_bucket[que_offset].rate, &token_rate, 256, p_gg_queue_master[lchip]->que_shp_update_freq );
            sys_goldengate_qos_map_token_thrd_hw_to_user(lchip, &token_thrd , p_grp_shp->c_bucket[que_offset].thrd, SYS_SHAPE_TOKEN_THRD_SHIFTS_WIDTH);
            SYS_QOS_GET_SHAPE_TOKEN_THRD(is_pps, token_thrd);
            SYS_QUEUE_DBG_DUMP("%-30s: %d (kbps/pps)\n","CIR",token_rate );
            SYS_QUEUE_DBG_DUMP("%-30s: %d (kb/pkt)\n","CBS",token_thrd );
        }
        else if(p_grp_shp->c_bucket[que_offset].cir_type == SYS_QUEUE_GRP_SHP_CIR_TYPE_CIR_PASS )
        {
            SYS_QUEUE_DBG_DUMP("No CIR Bucket, all packet always mark as green!\n" );
        }
        if(p_queue_node->shp_en)
        {
            sys_goldengate_qos_map_token_rate_hw_to_user(lchip, is_pps, p_queue_node->p_shp_profile->bucket_rate, &token_rate, 256, p_gg_queue_master[lchip]->que_shp_update_freq );
            sys_goldengate_qos_map_token_thrd_hw_to_user(lchip, &token_thrd , p_queue_node->p_shp_profile->bucket_thrd, SYS_SHAPE_TOKEN_THRD_SHIFTS_WIDTH);
            SYS_QOS_GET_SHAPE_TOKEN_THRD(is_pps, token_thrd);
            SYS_QUEUE_DBG_DUMP("%-30s: %d (kbps/pps)\n","PIR",token_rate );
            SYS_QUEUE_DBG_DUMP("%-30s: %d (kb/pkt)\n","PBS",token_thrd );
        }
        else
        {
            SYS_QUEUE_DBG_DUMP("No PIR Bucket\n");
        }

    }

    SYS_QOS_QUEUE_UNLOCK(lchip);

   if (start == end)
   {
        uint16 sub_grp = 0;
        ctc_qos_sched_group_t grp_sched;
        ctc_qos_sched_queue_t  queue_sched;

        sal_memset(&queue_sched, 0,  sizeof(queue_sched));
        sal_memset(&grp_sched, 0, sizeof(grp_sched));

        sys_goldengate_queue_sch_get_queue_sched(lchip, queue_id ,&queue_sched);

        SYS_QUEUE_DBG_DUMP("\nQueue Scheduler:\n");
        SYS_QUEUE_DBG_DUMP("-------------------------------\n");
        SYS_QUEUE_DBG_DUMP("%-30s: %d \n","Queue Class(Green)",queue_sched.confirm_class);
        SYS_QUEUE_DBG_DUMP("%-30s: %d \n","Queue Class(Yellow)",queue_sched.exceed_class);
        SYS_QUEUE_DBG_DUMP("%-30s: %d \n", "Weight", queue_sched.exceed_weight);

        sub_grp = queue_id / 4;  /*Queue Id :0~2047*/
        grp_sched.queue_class = queue_sched.confirm_class;
        sys_goldengate_queue_sch_get_group_sched(lchip, sub_grp, &grp_sched);
        SYS_QUEUE_DBG_DUMP("Queue Class(Green)  %d priority: %d\n", queue_sched.confirm_class, grp_sched.class_priority);

        grp_sched.queue_class = queue_sched.exceed_class;
        sys_goldengate_queue_sch_get_group_sched(lchip, sub_grp, &grp_sched);
        SYS_QUEUE_DBG_DUMP("Queue Class(Yellow) %d priority: %d\n", queue_sched.exceed_class, grp_sched.class_priority);

   }


   if (start == end)
   {
        uint8 wred = 0 ;
        uint16 wredMaxThrd[4];
        uint16 drop_prob[4];
        DsEgrResrcCtl_m ds_egr_resrc_ctl;
        DsQueThrdProfile_m ds_que_thrd_profile;
        uint8 cngest = 0;
        uint16 ecnthreshold = 0;

        sal_memset(&ds_que_thrd_profile, 0, sizeof(ds_que_thrd_profile));
        sal_memset(&ds_egr_resrc_ctl, 0, sizeof(ds_egr_resrc_ctl));

        SYS_QUEUE_DBG_DUMP("\nQueue Drop:\n");
        SYS_QUEUE_DBG_DUMP("-------------------------------\n");

        cmd = DRV_IOR(DsEgrResrcCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, queue_id, cmd, &ds_egr_resrc_ctl));

        wred = GetDsEgrResrcCtl(V, wredDropMode_f , &ds_egr_resrc_ctl);
        SYS_QUEUE_DBG_DUMP("%-30s: %s\n","Drop Mode", wred? "WRED":"WTD");

        value = GetDsEgrResrcCtl(V, mappedTc_f , &ds_egr_resrc_ctl);
        SYS_QUEUE_DBG_DUMP("%-30s: %d\n","Mapped tc", value);
        value = GetDsEgrResrcCtl(V, mappedSc_f , &ds_egr_resrc_ctl);
        SYS_QUEUE_DBG_DUMP("%-30s: %d\n","Mapped sc", value);
        value = GetDsEgrResrcCtl(V, queThrdProfType_f, &ds_egr_resrc_ctl);

        cmd = DRV_IOR(RaQueThrdProfId_t, RaQueThrdProfId_queThrdProfIdHigh_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, value , cmd, &value));

        for (cngest = 0; cngest < 8; cngest++)
        {
            cmd = DRV_IOR(DsQueThrdProfile_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, (value << 3) + cngest, cmd, &ds_que_thrd_profile));

            wredMaxThrd[0] = GetDsQueThrdProfile(V, wredMaxThrd0_f , &ds_que_thrd_profile);
            wredMaxThrd[1] = GetDsQueThrdProfile(V, wredMaxThrd1_f , &ds_que_thrd_profile);
            wredMaxThrd[2] = GetDsQueThrdProfile(V, wredMaxThrd2_f , &ds_que_thrd_profile);
            wredMaxThrd[3] = GetDsQueThrdProfile(V, wredMaxThrd3_f , &ds_que_thrd_profile);
            drop_prob[0] = GetDsQueThrdProfile(V, factor0_f , &ds_que_thrd_profile);
            drop_prob[1] = GetDsQueThrdProfile(V, factor1_f , &ds_que_thrd_profile);
            drop_prob[2] = GetDsQueThrdProfile(V, factor2_f , &ds_que_thrd_profile);
            drop_prob[3] = GetDsQueThrdProfile(V, factor3_f , &ds_que_thrd_profile);

            if (wred)
            {
                SYS_QUEUE_DBG_DUMP("Congest:%d %-19s : /%-8u/%-8u/%-8u\n", cngest, "MaxThrd",  wredMaxThrd[0], wredMaxThrd[1], wredMaxThrd[2]);
                SYS_QUEUE_DBG_DUMP("Congest:%d %-19s : /%-8u/%-8u/%-8u \n", cngest, "Drop_prob", drop_prob[0], drop_prob[1], drop_prob[2]);
            }
            else
            {
                SYS_QUEUE_DBG_DUMP("Congest:%d %-19s : /%-8u/%-8u/%-8u\n", cngest, "DropThrd", wredMaxThrd[0], wredMaxThrd[1], wredMaxThrd[2]);
            }

        }
        for (cngest = 0; cngest < 8; cngest++)
        {
            cmd = DRV_IOR(DsQueThrdProfile_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, (value << 3) + cngest, cmd, &ds_que_thrd_profile));

            ecnthreshold  = GetDsQueThrdProfile(V, ecnMarkThrd_f , &ds_que_thrd_profile);

            if (wred == 0)
            {
                SYS_QUEUE_DBG_DUMP("Congest:%d %-19s : /%-8u\n", cngest, "EcnThrd", ecnthreshold);
            }
        }

    }

    return CTC_E_NONE;
}

int32
sys_goldengate_qos_group_dump(uint8 lchip, uint16 start, uint16 end, uint8 detail)
{
    uint16 group_id = 0;
    uint32 token_thrd = 0;
    uint32 token_rate = 0;
    uint8 loop = 0;
    uint8 is_pps = 0;
    uint8 group_mode = 0;
    sys_queue_group_node_t  * p_group_node = NULL;

    SYS_QOS_QUEUE_INIT_CHECK(lchip);
    SYS_QUEUE_DBG_DUMP("Group information:\n");
    SYS_QUEUE_DBG_DUMP("%-10s %-10s %-10s", "group", "subgroup", "channel");
    SYS_QUEUE_DBG_DUMP("%-8s %-13s %-13s ", "shp_en", "pir(kbps/pps)", "pbs(kb/pkt)");
    SYS_QUEUE_DBG_DUMP("\n");
    SYS_QUEUE_DBG_DUMP("---------------------------------------------------------------\n");

    for (group_id = start; group_id <= end && group_id < 256; group_id++)
    {
        p_group_node = ctc_vector_get(p_gg_queue_master[lchip]->group_vec,group_id);
        if (NULL == p_group_node)
        {
            continue;
        }
        is_pps = sys_goldengate_queue_shp_get_channel_pps_enbale(lchip, p_group_node->chan_id);
        sys_goldengate_queue_get_grp_mode(lchip, group_id, &group_mode);

        token_thrd = 0;
        token_rate = 0;

        if (group_mode)
        {
            if(p_group_node->grp_shp[0].shp_bitmap && p_group_node->grp_shp[0].p_shp_profile)
            {
                sys_goldengate_qos_map_token_rate_hw_to_user(lchip, is_pps,p_group_node->grp_shp[0].p_shp_profile->bucket_rate,&token_rate,256,p_gg_queue_master[lchip]->que_shp_update_freq );
                sys_goldengate_qos_map_token_thrd_hw_to_user(lchip, &token_thrd ,p_group_node->grp_shp[0].p_shp_profile->bucket_thrd,SYS_SHAPE_TOKEN_THRD_SHIFTS_WIDTH);
                SYS_QOS_GET_SHAPE_TOKEN_THRD(is_pps, token_thrd);
            }
            SYS_QUEUE_DBG_DUMP("%-10d %-10d %-10d %-8d %-13u %-13u\n", group_id, group_id * 2, p_group_node->chan_id,CTC_IS_BIT_SET(p_group_node->grp_shp[0].shp_bitmap, SYS_QUEUE_PIR_BUCKET),token_rate,token_thrd);
        }
        else
        {
            if(p_group_node->grp_shp[0].shp_bitmap && p_group_node->grp_shp[0].p_shp_profile)
            {
                sys_goldengate_qos_map_token_rate_hw_to_user(lchip, is_pps,p_group_node->grp_shp[0].p_shp_profile->bucket_rate,&token_rate,256,p_gg_queue_master[lchip]->que_shp_update_freq );
                sys_goldengate_qos_map_token_thrd_hw_to_user(lchip, &token_thrd ,p_group_node->grp_shp[0].p_shp_profile->bucket_thrd,SYS_SHAPE_TOKEN_THRD_SHIFTS_WIDTH);
                SYS_QOS_GET_SHAPE_TOKEN_THRD(is_pps, token_thrd);
            }
            SYS_QUEUE_DBG_DUMP("%-10d %-10d %-10d %-8d %-13u %-13u\n", group_id, group_id * 2, p_group_node->chan_id,CTC_IS_BIT_SET(p_group_node->grp_shp[0].shp_bitmap, SYS_QUEUE_PIR_BUCKET),token_rate,token_thrd);

            if(p_group_node->grp_shp[1].shp_bitmap && p_group_node->grp_shp[1].p_shp_profile)
            {
                sys_goldengate_qos_map_token_rate_hw_to_user(lchip, is_pps,p_group_node->grp_shp[1].p_shp_profile->bucket_rate,&token_rate,256,p_gg_queue_master[lchip]->que_shp_update_freq );
                sys_goldengate_qos_map_token_thrd_hw_to_user(lchip, &token_thrd ,p_group_node->grp_shp[1].p_shp_profile->bucket_thrd,SYS_SHAPE_TOKEN_THRD_SHIFTS_WIDTH);
                SYS_QOS_GET_SHAPE_TOKEN_THRD(is_pps, token_thrd);
            }
            SYS_QUEUE_DBG_DUMP("%-10d %-10d %-10d %-8d %-13u %-13u\n", group_id, group_id * 2 + 1, p_group_node->chan_id,CTC_IS_BIT_SET(p_group_node->grp_shp[1].shp_bitmap, SYS_QUEUE_PIR_BUCKET),token_rate,token_thrd);
        }
    }

    if (start == end )
    {
        ctc_qos_sched_group_t  group_sched;
        group_id = start;
        SYS_QUEUE_DBG_DUMP("\nGroup Scheduler:\n");
        SYS_QUEUE_DBG_DUMP("-------------------------------\n");

        SYS_QUEUE_DBG_DUMP("%-12s %-10s %-6s\n", "Queue-Class", "Class-PRI", "Weight");
        for (loop = 0;  loop < 8; loop++)
        {
            group_sched.queue_class = loop;
            sys_goldengate_queue_sch_get_group_sched(lchip, group_id*2 , &group_sched);
            SYS_QUEUE_DBG_DUMP("%-12d %-10d %-6d\n", loop, group_sched.class_priority, group_sched.weight);
        }
    }
    return CTC_E_NONE;

}

int32
sys_goldengate_qos_get_port_info(uint8 lchip, uint32 channel, uint32 *shp_en, uint32 *token_rate, uint32 *token_thrd, uint8 *is_pps)
{
    uint32 cmd = 0;
    DsChanShpProfile_m chan_shp_profile;
    QMgrChanShapeCtl_m chan_shape_ctl;
    uint32 tmp_token_thrd = 0;
    uint32 token_thrd_shift = 0;
    uint32 token_rate_frac = 0;
    uint32 tmp_token_rate = 0;
    uint32 chan_shp_en[2];
    uint8 slice_channel = 0;

    cmd = DRV_IOR(DsChanShpProfile_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel, cmd, &chan_shp_profile));

    tmp_token_thrd = GetDsChanShpProfile(V, tokenThrdCfg_f, &chan_shp_profile);
    token_thrd_shift = GetDsChanShpProfile(V, tokenThrdShift_f  , &chan_shp_profile);
    token_rate_frac = GetDsChanShpProfile(V, tokenRateFrac_f, &chan_shp_profile);
    tmp_token_rate = GetDsChanShpProfile(V, tokenRate_f  , &chan_shp_profile);
    *is_pps = sys_goldengate_queue_shp_get_channel_pps_enbale(lchip, channel);

    sys_goldengate_qos_map_token_rate_hw_to_user(lchip, *is_pps, (tmp_token_rate << 8 | token_rate_frac)  , &tmp_token_rate, 256, p_gg_queue_master[lchip]->chan_shp_update_freq );
    sys_goldengate_qos_map_token_thrd_hw_to_user(lchip, &tmp_token_thrd , (tmp_token_thrd << SYS_SHAPE_TOKEN_THRD_SHIFTS_WIDTH | token_thrd_shift ), SYS_SHAPE_TOKEN_THRD_SHIFTS_WIDTH);

    cmd = DRV_IOR(QMgrChanShapeCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, (channel > 64), cmd, &chan_shape_ctl))
    GetQMgrChanShapeCtl(A, chanShpEn_f, &chan_shape_ctl, &chan_shp_en[0]);

    slice_channel = (channel > 64 ) ? (channel - 64):channel;
    *shp_en = (slice_channel < 32) ? CTC_IS_BIT_SET(chan_shp_en[0], slice_channel): CTC_IS_BIT_SET(chan_shp_en[1], (slice_channel - 32));
    *token_rate = tmp_token_rate;
    *token_thrd = tmp_token_thrd;

    return CTC_E_NONE;
}

int32
sys_goldengate_qos_port_dump(uint8 lchip, uint32 start, uint32 end, uint8 detail)
{
    uint32 channel = 0;
    uint16 port = 0;
    uint16 lport = 0;
    uint32 cmd;
    uint32 shp_en = 0;
    uint32 token_thrd = 0;
    uint32 token_rate = 0;
    uint8 is_pps = 0;
    uint16 lport_start = 0;
    uint16 lport_end = 0;
    uint8 gchip = 0;
    uint16 group_id = 0;
    uint8 group_mode = 0;
    sys_queue_group_node_t  * p_group_node = NULL;
    uint16 sub_group_id = 0;

    SYS_QOS_QUEUE_INIT_CHECK(lchip);
    gchip = SYS_MAP_CTC_GPORT_TO_GCHIP(start);

    if((CTC_IS_CPU_PORT(end)) && (CTC_IS_CPU_PORT(start)))
    {
       uint8 pcie_sel = 0;
       CTC_ERROR_RETURN(sys_goldengate_chip_get_pcie_select(lchip, &pcie_sel));

       SYS_QUEUE_DBG_DUMP("Port information:\n");
       SYS_QUEUE_DBG_DUMP("%-13s %-10s %-10s %-13s %-13s %-7s\n","port","channel", "shp-en", "pir(kbps/pps)", "pbs(kb/pkt)","shp_mode");
       SYS_QUEUE_DBG_DUMP("-----------------------------------------------------------------\n");

       CTC_ERROR_RETURN(sys_goldengate_qos_get_port_info(lchip, (SYS_DMA_CHANNEL_ID+(pcie_sel?64:0)), &shp_en, &token_rate, &token_thrd, &is_pps));
       SYS_QOS_GET_SHAPE_TOKEN_THRD(is_pps, token_thrd);
       SYS_QUEUE_DBG_DUMP("0x%x     %-10d %-10d %-13u %-13u %-7s\n",start, (SYS_DMA_CHANNEL_ID+(pcie_sel?64:0)), shp_en, token_rate, token_thrd, is_pps?"PPS":"BPS");

       return CTC_E_NONE;
    }
    if(CTC_IS_CPU_PORT(end))
    {
        return CTC_E_INVALID_PORT;
    }
    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(start, lchip, lport_start);
    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(end, lchip, lport_end);
    SYS_QUEUE_DBG_DUMP("Port information:\n");
    SYS_QUEUE_DBG_DUMP("%-10s %-10s %-10s %-13s %-13s %-7s\n","port","channel", "shp-en", "pir(kbps/pps)", "pbs(kb/pkt)","shp_mode");
    SYS_QUEUE_DBG_DUMP("-----------------------------------------------------------------\n");
    for (port = lport_start; port <= lport_end && port < SYS_GG_MAX_PORT_NUM_PER_CHIP; port++)
    {
       lport = port;

       if (SYS_QUEUE_DESTID_ENQ(lchip))
       {
           sub_group_id = (p_gg_queue_master[lchip]->queue_base[SYS_QUEUE_TYPE_EXTENDER_WITH_Q]
                      + SYS_MAP_DRV_LPORT_TO_SLICE(lport) * SYS_MAX_QUEUE_NUM
                      + (lport & 0xFF) * p_gg_queue_master[lchip]->queue_num_per_chanel) / 4;

           group_id = sub_group_id/2;
           p_group_node = ctc_vector_get(p_gg_queue_master[lchip]->group_vec, group_id);
           if (NULL == p_group_node)
           {
               continue;
           }
           is_pps = sys_goldengate_queue_shp_get_channel_pps_enbale(lchip, p_group_node->chan_id);
           sys_goldengate_queue_get_grp_mode(lchip, group_id, &group_mode);

           /*default value for token_rate and token_thrd*/
           sys_goldengate_qos_map_token_rate_hw_to_user(lchip, is_pps, (0x3FFFFF << 8 | 0xFF), &token_rate, 256, p_gg_queue_master[lchip]->que_shp_update_freq );
           sys_goldengate_qos_map_token_thrd_hw_to_user(lchip, &token_thrd , (0xFF << SYS_SHAPE_TOKEN_THRD_SHIFTS_WIDTH | 0xF), SYS_SHAPE_TOKEN_THRD_SHIFTS_WIDTH);
           SYS_QOS_GET_SHAPE_TOKEN_THRD(is_pps, token_thrd);

           if (group_mode)
           {
               if (p_group_node->grp_shp[0].shp_bitmap && p_group_node->grp_shp[0].p_shp_profile)
               {
                   sys_goldengate_qos_map_token_rate_hw_to_user(lchip, is_pps, p_group_node->grp_shp[0].p_shp_profile->bucket_rate, &token_rate, 256, p_gg_queue_master[lchip]->que_shp_update_freq );
                   sys_goldengate_qos_map_token_thrd_hw_to_user(lchip, &token_thrd , p_group_node->grp_shp[0].p_shp_profile->bucket_thrd, SYS_SHAPE_TOKEN_THRD_SHIFTS_WIDTH);
                   SYS_QOS_GET_SHAPE_TOKEN_THRD(is_pps, token_thrd);
               }
               SYS_QUEUE_DBG_DUMP("0x%04x     %-10d %-10d %-13u %-13u %-7s\n", SYS_MAP_DRV_LPORT_TO_CTC_GPORT(gchip, port), p_group_node->chan_id,
                                   CTC_IS_BIT_SET(p_group_node->grp_shp[0].shp_bitmap, SYS_QUEUE_PIR_BUCKET), token_rate, token_thrd,is_pps?"PPS":"BPS");
           }
           else
           {
               if (CTC_IS_BIT_SET(sub_group_id, 0))
               {
                   if (p_group_node->grp_shp[1].shp_bitmap && p_group_node->grp_shp[1].p_shp_profile)
                   {
                       sys_goldengate_qos_map_token_rate_hw_to_user(lchip, is_pps, p_group_node->grp_shp[1].p_shp_profile->bucket_rate, &token_rate, 256, p_gg_queue_master[lchip]->que_shp_update_freq );
                       sys_goldengate_qos_map_token_thrd_hw_to_user(lchip, &token_thrd , p_group_node->grp_shp[1].p_shp_profile->bucket_thrd, SYS_SHAPE_TOKEN_THRD_SHIFTS_WIDTH);
                       SYS_QOS_GET_SHAPE_TOKEN_THRD(is_pps, token_thrd);
                   }
                   SYS_QUEUE_DBG_DUMP("0x%04x     %-10d %-10d %-13u %-13u %-7s\n", SYS_MAP_DRV_LPORT_TO_CTC_GPORT(gchip, port), p_group_node->chan_id,
                                      CTC_IS_BIT_SET(p_group_node->grp_shp[1].shp_bitmap, SYS_QUEUE_PIR_BUCKET), token_rate, token_thrd, is_pps?"PPS":"BPS");
               }
               else
               {
                   if (p_group_node->grp_shp[0].shp_bitmap && p_group_node->grp_shp[0].p_shp_profile)
                   {
                       sys_goldengate_qos_map_token_rate_hw_to_user(lchip, is_pps, p_group_node->grp_shp[0].p_shp_profile->bucket_rate, &token_rate, 256, p_gg_queue_master[lchip]->que_shp_update_freq );
                       sys_goldengate_qos_map_token_thrd_hw_to_user(lchip, &token_thrd , p_group_node->grp_shp[0].p_shp_profile->bucket_thrd, SYS_SHAPE_TOKEN_THRD_SHIFTS_WIDTH);
                       SYS_QOS_GET_SHAPE_TOKEN_THRD(is_pps, token_thrd);
                   }
                   SYS_QUEUE_DBG_DUMP("0x%04x     %-10d %-10d %-13u %-13u %-7s\n", SYS_MAP_DRV_LPORT_TO_CTC_GPORT(gchip, port), p_group_node->chan_id,
                                      CTC_IS_BIT_SET(p_group_node->grp_shp[0].shp_bitmap, SYS_QUEUE_PIR_BUCKET), token_rate, token_thrd, is_pps?"PPS":"BPS");
               }

           }
       }
       else
       {
           cmd = DRV_IOR(DsLinkAggregationPort_t, DsLinkAggregationPort_channelId_f);
           CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &channel));

           CTC_ERROR_RETURN(sys_goldengate_qos_get_port_info(lchip, channel, &shp_en, &token_rate, &token_thrd, &is_pps));
           SYS_QOS_GET_SHAPE_TOKEN_THRD(is_pps, token_thrd);
           SYS_QUEUE_DBG_DUMP("0x%04x     %-10d %-10d %-13u %-13u %-7s\n", SYS_MAP_DRV_LPORT_TO_CTC_GPORT(gchip, port), channel, shp_en, token_rate, token_thrd, is_pps?"PPS":"BPS");
       }
    }

    if (start == end)
    {
        uint16 queue_id = 0;
        uint16 queue_index = 0;
        uint32 queue_depth = 0;
        ctc_qos_queue_id_t queue;
        sal_memset(&queue, 0, sizeof(ctc_qos_queue_id_t));

        queue.queue_type = CTC_QUEUE_TYPE_NETWORK_EGRESS;
        queue.gport = start;

        SYS_QUEUE_DBG_DUMP("\n%-10s %-10s\n","queue-id","queue-depth");
        SYS_QUEUE_DBG_DUMP("-------------------------------\n");

        for (queue_index = 0; queue_index < p_gg_queue_master[lchip]->queue_num_per_chanel; queue_index++)
        {
            queue.queue_id = queue_index;
            CTC_ERROR_RETURN(sys_goldengate_queue_get_queue_id(lchip, &queue, &queue_id));

            cmd = DRV_IOR(RaQueCnt_t, RaQueCnt_queInstCnt_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, queue_id, cmd, &queue_depth));

            SYS_QUEUE_DBG_DUMP("%-10d %-10d\n", queue_id, queue_depth);

        }
    }

    return CTC_E_NONE;

}
/*for sdk cosim pass*/
int32
sys_goldengate_queue_init_qmgr(uint8 lchip)
{

    uint32 cmd = 0;
    uint8 gchip = 0;
    uint32 value = 0;
    uint16 network_port_queue_num = 0;
    uint8 network_port_enqueue_mode = 0;
    QWriteCtl_m  q_write_ctl;
    BufferRetrieveCtl_m buffer_retrieve_ctl;
    uint8 network_port_qsel_type = SYS_QUEUE_DESTID_ENQ(lchip)? SYS_QSEL_TYPE_EXTERN_PORT_WITH_Q : SYS_QSEL_TYPE_REGULAR;
   sys_goldengate_queue_get_enqueue_info(lchip,&network_port_queue_num,&network_port_enqueue_mode);
   cmd = DRV_IOR(QWriteCtl_t, DRV_ENTRY_FLAG);
   CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &q_write_ctl));
   SetQWriteCtl(V, gToCpuGenQ_0_valid_f, &q_write_ctl, 1);
   SetQWriteCtl(V, gToCpuGenQ_0_rsvQSelType_f, &q_write_ctl, 1);
   SetQWriteCtl(V, gToCpuGenQ_0_chanId_f, &q_write_ctl, SYS_OAM_CHANNEL_ID);
   if (0 == p_gg_queue_master[lchip]->enq_mode)
   {
       SetQWriteCtl(V, gToCpuGenQ_1_valid_f, &q_write_ctl, 1);
       SetQWriteCtl(V, gToCpuGenQ_1_rsvQSelType_f, &q_write_ctl, SYS_QSEL_TYPE_C2C);
       SetQWriteCtl(V, gToCpuGenQ_1_chanId_f, &q_write_ctl, SYS_OAM_CHANNEL_ID);
   }

   /*ReMapping Change QSelType according to port id range*/
   /*Slice 0 QueSelTypeRemap*/
   SetQWriteCtl(V, g0_0_qSelTypeRemapEn_f, &q_write_ctl, 1);
   SetQWriteCtl(V, g0_0_rsvQSelType_f, &q_write_ctl, 0);
    /* 0. network_port_queue_num~255  QSelType : SYS_QSEL_TYPE_RSV_PORT and enqueue based on channel*/
   SetQWriteCtl(V, g0_0_newQSelTypeValid0_f, &q_write_ctl, 1);
   SetQWriteCtl(V, g0_0_rangeMin0_f , &q_write_ctl, network_port_queue_num);
   SetQWriteCtl(V, g0_0_rangeMax0_f , &q_write_ctl, SYS_RSV_PORT_END);
   SetQWriteCtl(V, g0_0_newQSelType0_f , &q_write_ctl, SYS_QSEL_TYPE_RSV_PORT);
    /* 1. 0~network_port_queue_num  QSelType : SYS_QSEL_TYPE_REGULAR or SYS_QSEL_TYPE_EXTERN_PORT_WITH_Q*/
   SetQWriteCtl(V, g0_0_newQSelTypeValid1_f, &q_write_ctl, (network_port_qsel_type !=0) );
   SetQWriteCtl(V, g0_0_rangeMin1_f, &q_write_ctl, 0);
   SetQWriteCtl(V, g0_0_rangeMax1_f, &q_write_ctl, network_port_queue_num );
   SetQWriteCtl(V, g0_0_newQSelType1_f, &q_write_ctl, network_port_qsel_type); /*enqueue based on port id*/

   /*Slice 1 QueSelTypeRemap*/
   SetQWriteCtl(V, g0_1_qSelTypeRemapEn_f, &q_write_ctl, 1);
   SetQWriteCtl(V, g0_1_rsvQSelType_f, &q_write_ctl, 0);
    /* 0. network_port_queue_num~255  QSelType : SYS_QSEL_TYPE_RSV_PORT and enqueue based on channel*/
   SetQWriteCtl(V, g0_1_newQSelTypeValid0_f, &q_write_ctl, 1);
   SetQWriteCtl(V, g0_1_rangeMin0_f , &q_write_ctl, network_port_queue_num);
   SetQWriteCtl(V, g0_1_rangeMax0_f , &q_write_ctl, SYS_RSV_PORT_END);
   SetQWriteCtl(V, g0_1_newQSelType0_f , &q_write_ctl, SYS_QSEL_TYPE_RSV_PORT);

    /* 1. 0~network_port_queue_num  QSelType : SYS_QSEL_TYPE_REGULAR or SYS_QSEL_TYPE_EXTERN_PORT_WITH_Q*/
   SetQWriteCtl(V, g0_1_newQSelTypeValid1_f, &q_write_ctl, (network_port_qsel_type !=0));
   SetQWriteCtl(V, g0_1_rangeMin1_f, &q_write_ctl, 0);
   SetQWriteCtl(V, g0_1_rangeMax1_f, &q_write_ctl, network_port_queue_num );
   SetQWriteCtl(V, g0_1_newQSelType1_f, &q_write_ctl, network_port_qsel_type);/*enqueue based on port id*/

    /*exception packet on statcking port  QSelType : 1*/
   SetQWriteCtl(V,  g1_qBaseRemapEn_f, &q_write_ctl, 0);
   SetQWriteCtl(V,  g1_rsvQSelType_f, &q_write_ctl, SYS_QUEUE_TYPE_EXCP);
   cmd = DRV_IOW(QWriteCtl_t, DRV_ENTRY_FLAG);
   CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &q_write_ctl));


   sal_memset(&buffer_retrieve_ctl, 0xFF, sizeof(BufferRetrieveCtl_m));
   sys_goldengate_get_gchip_id(lchip,&gchip);
   SetBufferRetrieveCtl(V, chipId_f, &buffer_retrieve_ctl, gchip);
   SetBufferRetrieveCtl(V, colorMapEn_f , &buffer_retrieve_ctl, 0);
   SetBufferRetrieveCtl(V, criticalPacketToFabric_f, &buffer_retrieve_ctl, 1);
   SetBufferRetrieveCtl(V, crossChipHighPriorityEn_f , &buffer_retrieve_ctl, 1);
   SetBufferRetrieveCtl(V, dropOnSpanEn_f , &buffer_retrieve_ctl, 0);
   SetBufferRetrieveCtl(V, dropOnSpanRsvQueId_f , &buffer_retrieve_ctl, 0);
   value = SYS_NH_ENCODE_ILOOP_DSNH(SYS_RSV_PORT_IPE_QUE_DROP,0,0,0,0);
   SetBufferRetrieveCtl(V, dropOnSpanNextHopPtr_f , &buffer_retrieve_ctl, value);
   SetBufferRetrieveCtl(V, dropOnSpanOffsetForceResetWaive_f, &buffer_retrieve_ctl, 1);
   SetBufferRetrieveCtl(V, dropOnSpanOffsetReset_f, &buffer_retrieve_ctl, 1);

   SetBufferRetrieveCtl(V, exceptionFollowMirror_f, &buffer_retrieve_ctl, 0);
   SetBufferRetrieveCtl(V, exceptionPktEditMode_f, &buffer_retrieve_ctl, 1);
   SetBufferRetrieveCtl(V, exceptionResetMode_f , &buffer_retrieve_ctl, 1);
   SetBufferRetrieveCtl(V, removeMuxHeader_f, &buffer_retrieve_ctl, 0);
   SetBufferRetrieveCtl(V, sgmacExceptionNexthopBase_f, &buffer_retrieve_ctl, 0);
   SetBufferRetrieveCtl(V, stackingEn_f , &buffer_retrieve_ctl, 1);
   SetBufferRetrieveCtl(V, truncationLength_f  , &buffer_retrieve_ctl, 144);
   cmd = DRV_IOW(BufferRetrieveCtl_t, DRV_ENTRY_FLAG);
   CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &buffer_retrieve_ctl));
   cmd = DRV_IOW(BufferRetrieveCtl_t, DRV_ENTRY_FLAG);
   CTC_ERROR_RETURN(DRV_IOCTL(lchip, 1, cmd, &buffer_retrieve_ctl));


    return CTC_E_NONE;
}



int32
sys_goldengate_queue_set_to_stacking_port(uint8 lchip, uint16 gport,uint8 enable)
{

    uint16 lport = 0;
    uint32 offset = 0;
    uint8  channel = 0;
    uint32 cmd = 0;
    sys_goldengate_opf_t opf;
    DsQueueBaseProfileId_m profile_id;
    DsQueueBaseProfile_m   profile;
    ctc_qos_queue_id_t queue;
    uint16 queue_base = 0 ;

    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);

    /* init exception queue on statcking port */
    opf.pool_index = lchip;
    opf.pool_type = OPF_QUEUE_EXCEPTION_STACKING_PROFILE;
    sys_goldengate_get_channel_by_port(lchip, gport, &channel);

    if(enable)
    {
	  CTC_ERROR_RETURN(sys_goldengate_opf_alloc_offset(lchip, &opf, 1,&offset));
      cmd = DRV_IOR(DsQueueBaseProfileId_t, DRV_ENTRY_FLAG);
      CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel, cmd, &profile_id));
      SetDsQueueBaseProfileId(V, queueBaseProfId_f, &profile_id,offset);
      cmd = DRV_IOW(DsQueueBaseProfileId_t, DRV_ENTRY_FLAG);
      CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel, cmd, &profile_id));

	  queue.queue_type = CTC_QUEUE_TYPE_NETWORK_EGRESS;
      queue.gport  = gport;
      queue.queue_id = 0;
      sys_goldengate_queue_get_queue_id(lchip, &queue,&queue_base);
	  queue_base += p_gg_queue_master[lchip]->queue_num_per_chanel/2;

       cmd = DRV_IOR(DsQueueBaseProfile_t, DRV_ENTRY_FLAG);
       CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &profile));
	   DRV_SET_FIELD_V(DsQueueBaseProfile_t,DsQueueBaseProfile_g_0_queueNumBase_f+offset,&profile,queue_base);
       cmd = DRV_IOW(DsQueueBaseProfile_t, DRV_ENTRY_FLAG);
       CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &profile))
    }
	else
	{
	  cmd = DRV_IOR(DsQueueBaseProfileId_t, DRV_ENTRY_FLAG);
      CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel, cmd, &profile_id));
      offset = GetDsQueueBaseProfileId(V, queueBaseProfId_f, &profile_id);
	  CTC_ERROR_RETURN(sys_goldengate_opf_free_offset(lchip, &opf, 1,offset));

	}
    return CTC_E_NONE;
}

/**
 @brief En-queue component initialization.
*/
int32
sys_goldengate_queue_enq_init(uint8 lchip, void *p_glb_parm)
{
    uint8  gchip = 0;
    uint16 lport = 0;
    uint16 queue_base = 0;
    uint16 loop = 0;
    uint8 c2c_group_base = 14;/* 0~15 CPU group*/
    sys_queue_node_t  *p_queue_node;
    sys_goldengate_opf_t  opf;
    ctc_qos_global_cfg_t * p_glb_cfg = NULL;
    uint16 chan_num     = 0;
    uint8 channel       = 0;
    uint16 reason_id = 0;
    sys_datapath_lport_attr_t* port_attr = NULL;
    p_glb_cfg = (ctc_qos_global_cfg_t *)p_glb_parm;



    /*************************************************
    *init global prarm
    *************************************************/
    if (p_gg_queue_master[lchip])
    {
        return CTC_E_NONE;
    }

    /*check queue mode*/
    if((CTC_QOS_PORT_QUEUE_NUM_4_BPE != p_glb_cfg->queue_num_per_network_port)
        && (CTC_QOS_PORT_QUEUE_NUM_8 != p_glb_cfg->queue_num_per_network_port)
        && (CTC_QOS_PORT_QUEUE_NUM_8_BPE != p_glb_cfg->queue_num_per_network_port)
        && (12 != p_glb_cfg->queue_num_per_network_port)
        && (CTC_QOS_PORT_QUEUE_NUM_16 != p_glb_cfg->queue_num_per_network_port))
    {
        return CTC_E_INTR_INVALID_PARAM;
    }

    if((128 != p_glb_cfg->queue_num_for_cpu_reason)
        && (64 != p_glb_cfg->queue_num_for_cpu_reason)
        && (32 != p_glb_cfg->queue_num_for_cpu_reason))
    {
        return CTC_E_INTR_INVALID_PARAM;
    }

    p_gg_queue_master[lchip] = mem_malloc(MEM_QUEUE_MODULE, sizeof(sys_queue_master_t));
    if (NULL == p_gg_queue_master[lchip])
    {
        return CTC_E_NO_MEMORY;
    }
    sal_memset(p_gg_queue_master[lchip], 0, sizeof(sys_queue_master_t));

    SYS_QOS_QUEUE_CREATE_LOCK(lchip);
    /* default use priority_mode : 1  0-15 priority */
    p_gg_queue_master[lchip]->priority_mode = (p_glb_cfg->priority_mode == 0)? 1:0;
    /*two slice: 2048 queue,128 * 16 = 2048 queue*/
    p_gg_queue_master[lchip]->queue_vec = ctc_vector_init(128,16);


    for (loop = 0; loop < 2048; loop++)
    {
        p_queue_node = (sys_queue_node_t*)mem_malloc(MEM_QUEUE_MODULE, sizeof(sys_queue_node_t));
        if (!p_queue_node)
        {
            return CTC_E_NO_MEMORY;
        }
        sal_memset(p_queue_node, 0, sizeof(sys_queue_node_t));
        p_queue_node->queue_id = loop;
        p_queue_node->offset = loop % 8;
        ctc_vector_add(p_gg_queue_master[lchip]->queue_vec, loop, p_queue_node);
    }
    /*two slice: 256 super group,2 * 128 = super group*/
    p_gg_queue_master[lchip]->group_vec = ctc_vector_init(2,128);


    CTC_ERROR_RETURN(sys_goldengate_opf_init(lchip, OPF_QUEUE_EXCEPTION_STACKING_PROFILE,2));

    /* init exception queue on statcking port */
    opf.pool_index = 0;
    opf.pool_type = OPF_QUEUE_EXCEPTION_STACKING_PROFILE;
    /*0 reserved for normal DMA/CPU channle*/
    CTC_ERROR_RETURN(sys_goldengate_opf_init_offset(lchip, &opf, 1, 7));
    opf.pool_index = 1;
    opf.pool_type = OPF_QUEUE_EXCEPTION_STACKING_PROFILE;
    CTC_ERROR_RETURN(sys_goldengate_opf_init_offset(lchip, &opf, 1, 7));


    /*************************************************
    *init queue (MUST before alloc queue)
    *************************************************/
    CTC_ERROR_RETURN(_sys_goldengate_queue_init(lchip));

    /*************************************************
    *init drop channel
    *************************************************/
    CTC_ERROR_RETURN(_sys_goldengate_rsv_channel_drop_init(lchip));

   /*************************************************
    *init queue alloc */
    sys_goldengate_get_gchip_id(lchip,&gchip);
    for (lport = 0; lport < SYS_GG_MAX_PORT_NUM_PER_CHIP; lport++)
    {
        if (((p_glb_cfg->queue_num_per_network_port == CTC_QOS_PORT_QUEUE_NUM_4_BPE)
            || (p_glb_cfg->queue_num_per_network_port == CTC_QOS_PORT_QUEUE_NUM_8_BPE)))
        {
            channel = SYS_DROP_CHANNEL_ID;
        }
        else
        {
            CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, lport, &port_attr));
            if (SYS_DATAPATH_NETWORK_PORT == port_attr->port_type)
            {
                channel = SYS_GET_CHANNEL_ID(lchip, CTC_MAP_LPORT_TO_GPORT(gchip, lport));
            }
            else
            {
                channel = SYS_DROP_CHANNEL_ID;
            }
        }
        sys_goldengate_add_port_to_channel(lchip, lport, channel);
    }

    CTC_ERROR_RETURN(_sys_goldengate_queue_select_init(lchip));

    sys_goldengate_dma_get_packet_rx_chan(lchip, &chan_num);

    p_gg_queue_master[lchip]->max_dma_rx_num       = chan_num;  /* must get from DMA module */

    if (CTC_QOS_PORT_QUEUE_NUM_4_BPE == p_glb_cfg->queue_num_per_network_port)
    {
        p_gg_queue_master[lchip]->queue_num_per_chanel = 4;
        p_gg_queue_master[lchip]->enq_mode = 2;
    }
    else if (CTC_QOS_PORT_QUEUE_NUM_8_BPE == p_glb_cfg->queue_num_per_network_port)
    {
        p_gg_queue_master[lchip]->queue_num_per_chanel = 8;
        p_gg_queue_master[lchip]->enq_mode = 2;
    }
    else if (CTC_QOS_PORT_QUEUE_NUM_16 == p_glb_cfg->queue_num_per_network_port)
    {
        p_gg_queue_master[lchip]->queue_num_per_chanel = 16;
        p_gg_queue_master[lchip]->enq_mode = 1;
    }
    else
    {
        p_gg_queue_master[lchip]->queue_num_per_chanel = 16;
        p_gg_queue_master[lchip]->enq_mode = 0;
    }


    p_gg_queue_master[lchip]->queue_num_for_cpu_reason = p_glb_cfg->queue_num_for_cpu_reason;
    if((chan_num == 1) && (p_glb_cfg->queue_num_for_cpu_reason > 64))
    {
        p_gg_queue_master[lchip]->queue_num_for_cpu_reason = 64;
    }

    p_gg_queue_master[lchip]->max_chan_per_slice   = SYS_PHY_PORT_NUM_PER_SLICE; /*channel enq*/

    CTC_ERROR_RETURN(sys_goldengate_queue_init_qmgr(lchip));

    CTC_ERROR_RETURN(sys_goldengate_get_chip_cpu_eth_en(lchip, &p_gg_queue_master[lchip]->have_lcpu_by_eth,
                                        &p_gg_queue_master[lchip]->cpu_eth_chan_id));
    _sys_goldengate_queue_init_networks_port_queue(lchip, &queue_base);
    _sys_goldengate_queue_init_reserved_queue(lchip, &queue_base);
    _sys_goldengate_queue_init_excp_queue(lchip, &queue_base);
    if (0 == p_gg_queue_master[lchip]->enq_mode)
    {
        sys_goldengate_queue_init_c2c_queue(lchip, c2c_group_base, 0, SYS_QUEUE_TYPE_NORMAL);
        sys_goldengate_queue_init_c2c_queue(lchip, c2c_group_base, 0, SYS_QUEUE_TYPE_EXCP);
        p_gg_queue_master[lchip]->c2c_group_base = c2c_group_base;
        p_gg_queue_master[lchip]->cpu_reason[CTC_PKT_CPU_REASON_C2C_PKT].sub_queue_id = c2c_group_base*SYS_QUEUE_NUM_PER_GROUP;
    }

    if (p_gg_queue_master[lchip]->have_lcpu_by_eth)
    {
        _sys_goldengate_queue_init_excp_by_eth_queue(lchip);
        sys_goldengate_chip_set_eth_cpu_cfg(lchip);

    }

    /*fwd to cpu not cut through*/
    if(sys_goldengate_get_cut_through_en(lchip))
    {
        for (reason_id = CTC_PKT_CPU_REASON_CUSTOM_BASE; reason_id < CTC_PKT_CPU_REASON_MAX_COUNT; reason_id++)
        {
            p_gg_queue_master[lchip]->cpu_reason[reason_id].sub_queue_id = SYS_PHY_PORT_NUM_PER_SLICE;
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_queue_free_node_data(void* node_data, void* user_data)
{
    mem_free(node_data);

    return CTC_E_NONE;
}

int32
sys_goldengate_queue_deinit(uint8 lchip)
{
    uint8  slice_id = 0;

    LCHIP_CHECK(lchip);
    if (NULL == p_gg_queue_master[lchip])
    {
        return CTC_E_NONE;
    }

    /*free resrc profile data*/
    ctc_spool_free(p_gg_queue_master[lchip]->p_resrc_profile_pool[SYS_RESRC_PROFILE_FC]);
    ctc_spool_free(p_gg_queue_master[lchip]->p_resrc_profile_pool[SYS_RESRC_PROFILE_PFC]);

    sys_goldengate_opf_deinit(lchip, OPF_QUEUE_DROP_PROFILE);

    for (slice_id = 0; slice_id < SYS_MAX_LOCAL_SLICE_NUM; slice_id++)
    {
        /*free gruop profile data*/
        ctc_spool_free(p_gg_queue_master[lchip]->p_group_profile_pool[slice_id]);

        /*free queue profile data*/
        ctc_spool_free(p_gg_queue_master[lchip]->p_queue_profile_pool[slice_id]);
    }

    sys_goldengate_opf_deinit(lchip, OPF_QUEUE_SHAPE_PROFILE);
    sys_goldengate_opf_deinit(lchip, OPF_GROUP_SHAPE_PROFILE);

    /*free drop profile data*/
    ctc_spool_free(p_gg_queue_master[lchip]->p_drop_profile_pool);

    /*free group vec*/
    ctc_vector_traverse(p_gg_queue_master[lchip]->group_vec, (vector_traversal_fn)_sys_goldengate_queue_free_node_data, NULL);
    ctc_vector_release(p_gg_queue_master[lchip]->group_vec);

    /*free queue vec*/
    ctc_vector_traverse(p_gg_queue_master[lchip]->queue_vec, (hash_traversal_fn)_sys_goldengate_queue_free_node_data, NULL);
    ctc_vector_release(p_gg_queue_master[lchip]->queue_vec);

    sys_goldengate_opf_deinit(lchip, OPF_QUEUE_EXCEPTION_STACKING_PROFILE);

    sal_mutex_destroy(p_gg_queue_master[lchip]->mutex);

    mem_free(p_gg_queue_master[lchip]);

    return CTC_E_NONE;
}


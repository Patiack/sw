/**
 @file sys_goldengate_queue_sch.c

 @date 2010-01-13

 @version v2.0

*/

/****************************************************************************
 *
 * Header Files
 *
 ****************************************************************************/

#include "ctc_error.h"
#include "ctc_macro.h"
#include "ctc_queue.h"

#include "sys_goldengate_chip.h"
#include "sys_goldengate_queue_enq.h"
#include "sys_goldengate_queue_sch.h"


#include "goldengate/include/drv_enum.h"
#include "goldengate/include/drv_io.h"

/****************************************************************************
 *
 * Global and Declaration
 *
 ****************************************************************************/

extern sys_queue_master_t* p_gg_queue_master[CTC_MAX_LOCAL_CHIP_NUM];

#define QOS_SCHED_MAX_QUE_CLASS 7
#define QOS_SCHED_MAX_CHAN_CLASS 3
#define QOS_SCHED_MAX_QUE_WEITGHT 0x1FFF

#define MAX_SCHED_WEIGHT_BASE 1024
/****************************************************************************
 *
 * Function
 *
 ****************************************************************************/

int32
_sys_goldengate_queue_sch_threshold_compute(uint8 lchip, uint32 weight,
                                           uint16* p_threshold,
                                           uint8* p_shift)
{
   uint32 hw_weight;
   sys_goldengate_qos_map_token_thrd_user_to_hw(lchip, weight, &hw_weight,4, ((1 << 10) -1));
   *p_threshold = hw_weight >> 4;
   *p_shift   = hw_weight & 0xF;
   return CTC_E_NONE;

}

int32
_sys_goldengate_queue_sch_threshold_compute_to_user(uint8 lchip, uint32 *user_weight,
                                                    uint16 threshold,
                                                    uint8 shift)
{
#if 0
    uint32 hw_bucket_thrd = 0;
    hw_bucket_thrd = (threshold << 4)  | shift;
    sys_goldengate_qos_map_token_thrd_hw_to_user(lchip, user_weight, hw_bucket_thrd, 4);
#endif
    *user_weight = threshold << shift;

    return CTC_E_NONE;
}

int32
sys_goldengate_qos_set_sch_wrr_enable(uint8 lchip, uint8 enable)
{
    uint32 cmd = 0;
    uint32 field_val = 0;

    field_val = enable ? 0 : 1;
    cmd = DRV_IOW(QMgrDeqSchCtl_t, QMgrDeqSchCtl_schBasedOnPktLen_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 1, cmd, &field_val));

    cmd = DRV_IOW(QMgrMsgStoreCtl_t, QMgrMsgStoreCtl_schBasedOnPktLen_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    cmd = DRV_IOW(QMgrEnqCtl_t, QMgrEnqCtl_basedOnCellCnt_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    return CTC_E_NONE;
}

int32
sys_goldengate_queue_sch_set_queue_sched(uint8 lchip, ctc_qos_sched_queue_t* p_sched)
{

    uint32 cmd = 0;
    uint16 queue_id = 0;
    uint16 offset = 0;
    uint16 sub_grp   = 0;
    uint32 field_val = 0;
    uint32 field_id  = 0;
    uint8 mode  = 0;
    uint16 step = 0;
    uint32 pir_weight_shift = 0;
    uint32 pir_weight = 0;
    uint16 index = 0;
    uint16 queue_start = 0;
    uint32 group_weight = 0;
    sys_queue_node_t* p_sys_queue_node = NULL;
    DsQueWfqWeight_m que_wfq_weight;
    DsGrpWfqWeight_m  grp_wfq_weight;

    SYS_QUEUE_DBG_FUNC();
    SYS_QUEUE_DBG_PARAM("p_sched->cfg_type = %d\n", p_sched->cfg_type);
    SYS_QUEUE_DBG_PARAM("p_sched->queue_type = %d\n", p_sched->queue.queue_type);
    SYS_QUEUE_DBG_PARAM("p_sched->queue_id = %d\n", p_sched->queue.queue_id);
    SYS_QUEUE_DBG_PARAM("p_sched->gport = 0x%x\n", p_sched->queue.gport);
    SYS_QUEUE_DBG_PARAM("p_sched->service_id = 0x%x\n", p_sched->queue.service_id);
    SYS_QUEUE_DBG_PARAM("p_sched->confirm_weight = %d\n", p_sched->confirm_weight);
    SYS_QUEUE_DBG_PARAM("p_sched->confirm_class = %d\n", p_sched->confirm_class);
    SYS_QUEUE_DBG_PARAM("p_sched->exceed_weight = %d\n", p_sched->exceed_weight);
    SYS_QUEUE_DBG_PARAM("p_sched->exceed_class = %d\n", p_sched->exceed_class);



    CTC_ERROR_RETURN(sys_goldengate_queue_get_queue_id(lchip, &p_sched->queue,
                                                       &queue_id));

    SYS_QUEUE_DBG_INFO("queue_id = %d\n", queue_id);

    SYS_QOS_QUEUE_LOCK(lchip);
    p_sys_queue_node = ctc_vector_get(p_gg_queue_master[lchip]->queue_vec,queue_id);
    SYS_QOS_QUEUE_UNLOCK(lchip);
    if (NULL == p_sys_queue_node)
    {
        return CTC_E_INVALID_PARAM;
    }

    /*super group id with slice id*/
    sub_grp     = queue_id/4; /*Queue Id :0~2047*/
    offset      = p_sys_queue_node->offset;

    CTC_ERROR_RETURN(sys_goldengate_queue_get_grp_mode(lchip, queue_id / 8, &mode));
    /*super group id with slice id*/
    if (mode) /*8 queue mode use even subgroup id*/
    {
        CTC_BIT_UNSET(sub_grp, 0);
    }
    else
    {
        /*4 queue mode, odd subgroup use queue offset 4-7, even subgroup use 0-3*/
        if (CTC_IS_BIT_SET(sub_grp, 0))
        {
            offset = offset + 4;
        }
    }
    SYS_QUEUE_DBG_INFO("sub group_id = %d\n", sub_grp);
    SYS_QUEUE_DBG_INFO("queue offset = %d\n", offset);

    switch (p_sched->cfg_type)
    {
    case CTC_QOS_SCHED_CFG_CONFIRM_CLASS:
        CTC_MAX_VALUE_CHECK(p_sched->confirm_class, QOS_SCHED_MAX_QUE_CLASS);
        field_val = p_sched->confirm_class;
        step = DsSchServiceProfile_queOffset1GreenRemapSchPri_f - DsSchServiceProfile_queOffset0GreenRemapSchPri_f;
        field_id = DsSchServiceProfile_queOffset0GreenRemapSchPri_f + offset * step;
        cmd = DRV_IOW(DsSchServiceProfile_t, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, sub_grp, cmd, &field_val));
        break;

    case CTC_QOS_SCHED_CFG_EXCEED_CLASS:
        CTC_MAX_VALUE_CHECK(p_sched->exceed_class, QOS_SCHED_MAX_QUE_CLASS);
        field_val = p_sched->exceed_class;
        step = DsSchServiceProfile_queOffset1YellowRemapSchPri_f - DsSchServiceProfile_queOffset0YellowRemapSchPri_f;
        field_id = DsSchServiceProfile_queOffset0YellowRemapSchPri_f + offset * step;
        cmd = DRV_IOW(DsSchServiceProfile_t, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, sub_grp, cmd, &field_val));
        break;

    case CTC_QOS_SCHED_CFG_CONFIRM_WEIGHT:
        return CTC_E_FEATURE_NOT_SUPPORT;

    case CTC_QOS_SCHED_CFG_EXCEED_WEIGHT:
        {
            uint32 weight = 0;
            uint16 threshold = 0;
            uint8  shift = 0;
            CTC_MIN_VALUE_CHECK(p_sched->exceed_weight, 1);
            SYS_QOS_QUEUE_LOCK(lchip);
            weight = (p_sched->exceed_weight * p_gg_queue_master[lchip]->wrr_weight_mtu);
            SYS_QOS_QUEUE_UNLOCK(lchip);
            CTC_MAX_VALUE_CHECK(weight, 0x3FFFFFF);

            CTC_ERROR_RETURN(_sys_goldengate_queue_sch_threshold_compute(lchip, weight, &threshold, &shift));


            cmd = DRV_IOR(DsQueWfqWeight_t, DRV_ENTRY_FLAG);
	        CTC_ERROR_RETURN(DRV_IOCTL(lchip, queue_id, cmd, &que_wfq_weight));

            SetDsQueWfqWeight(V, pirWeightShift_f, &que_wfq_weight,shift);
            SetDsQueWfqWeight(V, pirWeight_f, &que_wfq_weight, threshold);
            cmd = DRV_IOW(DsQueWfqWeight_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, queue_id, cmd, &que_wfq_weight));

            if (p_gg_queue_master[lchip]->group_mode)
            {
                queue_start = queue_id / 8 * 8;
                for (index = queue_start; index < queue_start + SYS_QUEUE_NUM_PER_GROUP; index++)
                {
                    cmd = DRV_IOR(DsQueWfqWeight_t, DRV_ENTRY_FLAG);
                    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &que_wfq_weight));
                    pir_weight_shift =   GetDsQueWfqWeight(V, pirWeightShift_f, &que_wfq_weight);
                    pir_weight =  GetDsQueWfqWeight(V, pirWeight_f, &que_wfq_weight);
                    if ((pir_weight_shift != SYS_WFQ_DEFAULT_SHIFT) && (pir_weight != SYS_WFQ_DEFAULT_WEIGHT)
                        && (index != queue_id))
                    {
                        group_weight += pir_weight << pir_weight_shift;
                    }
                }

                group_weight = group_weight + weight;
                CTC_MAX_VALUE_CHECK(group_weight, 0x3FFFFFF);

                CTC_ERROR_RETURN(_sys_goldengate_queue_sch_threshold_compute(lchip, group_weight, &threshold, &shift));
                SYS_QUEUE_DBG_INFO("weight = %d, threshold = %d, shift = %d\n", group_weight, threshold, shift);

                cmd = DRV_IOR(DsGrpWfqWeight_t, DRV_ENTRY_FLAG);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, sub_grp, cmd, &grp_wfq_weight));
                SetDsGrpWfqWeight(V, weightShift_f, &grp_wfq_weight, shift);
                SetDsGrpWfqWeight(V, weight_f, &grp_wfq_weight, threshold);
                cmd = DRV_IOW(DsGrpWfqWeight_t, DRV_ENTRY_FLAG);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip,  sub_grp , cmd, &grp_wfq_weight));

            }
        }
        break;

    default:
        return CTC_E_INVALID_PARAM;

    }
    return CTC_E_NONE;
}

int32
sys_goldengate_queue_sch_get_queue_sched(uint8 lchip, uint16 queue_id ,ctc_qos_sched_queue_t* p_sched)
{
    sys_queue_node_t* p_sys_queue_node = NULL;
    DsQueWfqWeight_m que_wfq_weight;

    uint16 offset = 0;
    uint16 sub_grp   = 0;

    uint32 field_val = 0;
    uint32 field_id  = 0;
    uint32 cmd = 0;

    uint16 index = 0;
    uint8 step = 0;
    uint8 mode  = 0;

    SYS_QUEUE_DBG_FUNC();

    if(queue_id == CTC_MAX_UINT16_VALUE)
    {
        CTC_ERROR_RETURN(sys_goldengate_queue_get_queue_id(lchip, &p_sched->queue,
                                                           &queue_id));
    }
    SYS_QUEUE_DBG_INFO("queue_id = %d\n", queue_id);

    SYS_QOS_QUEUE_LOCK(lchip);
    p_sys_queue_node = ctc_vector_get(p_gg_queue_master[lchip]->queue_vec,queue_id);
    SYS_QOS_QUEUE_UNLOCK(lchip);
    if (NULL == p_sys_queue_node)
    {
        return CTC_E_INVALID_PARAM;
    }
    /*super group id with slice id*/
    /*Queue Id :0~2047*/
    sub_grp   = queue_id/4;

    CTC_ERROR_RETURN(sys_goldengate_queue_get_grp_mode(lchip, queue_id / 8, &mode));
    /*super group id with slice id*/
    if (mode) /*8 queue mode use even subgroup id*/
    {
        CTC_BIT_UNSET(sub_grp, 0);
        offset = queue_id % 8;
    }
    else
    {
        /*4 queue mode, odd subgroup use queue offset 4-7, even subgroup use 0-3*/
        if (CTC_IS_BIT_SET(sub_grp, 0))
        {
            offset = queue_id % 8;
        }
        else
        {
            offset = queue_id % 4;
        }
    }

    step = DsSchServiceProfile_queOffset1GreenRemapSchPri_f - DsSchServiceProfile_queOffset0GreenRemapSchPri_f;
    field_id = DsSchServiceProfile_queOffset0GreenRemapSchPri_f + offset * step;
    cmd = DRV_IOR(DsSchServiceProfile_t, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, sub_grp, cmd, &field_val));
    p_sched->confirm_class = field_val;

    step = DsSchServiceProfile_queOffset1YellowRemapSchPri_f - DsSchServiceProfile_queOffset0YellowRemapSchPri_f;
    field_id = DsSchServiceProfile_queOffset0YellowRemapSchPri_f + offset * step;
    cmd = DRV_IOR(DsSchServiceProfile_t, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, sub_grp, cmd, &field_val));
    p_sched->exceed_class = field_val;


    cmd = DRV_IOR(DsQueWfqWeight_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, queue_id, cmd, &que_wfq_weight));
    index =   GetDsQueWfqWeight(V, pirWeightShift_f, &que_wfq_weight);
    field_id =  GetDsQueWfqWeight(V, pirWeight_f, &que_wfq_weight);
    _sys_goldengate_queue_sch_threshold_compute_to_user(lchip, &field_val, field_id, index);
    SYS_QOS_QUEUE_LOCK(lchip);
    p_sched->exceed_weight = field_val/p_gg_queue_master[lchip]->wrr_weight_mtu;
    SYS_QOS_QUEUE_UNLOCK(lchip);
    return CTC_E_NONE;

}

int32
sys_goldengate_queue_sch_set_group_sched(uint8 lchip, ctc_qos_sched_group_t* p_sched)
{

    sys_queue_node_t* p_sys_queue_node = NULL;
    uint32 cmd                     = 0;
    uint16 wfq_offset              = 0;
    uint16 sub_grp                 = 0;
    uint32 field_val               = 0;
    uint32 field_id                = 0;
    uint16 queue_id                = 0;
    uint8 mode                     = 0;

    SYS_QUEUE_DBG_FUNC();

    SYS_QUEUE_DBG_PARAM("p_sched->group_type = %d\n", p_sched->group_type);
    SYS_QUEUE_DBG_PARAM("p_sched->class_priority = %d\n", p_sched->class_priority);
    SYS_QUEUE_DBG_PARAM("p_sched->queue_class = %d\n", p_sched->queue_class);
    SYS_QUEUE_DBG_PARAM("p_sched->service_id = 0x%x\n", p_sched->service_id);
    SYS_QUEUE_DBG_PARAM("p_sched->gport = 0x%x\n", p_sched->gport);

    if (p_sched->group_type == CTC_QOS_SCHED_GROUP_SERVICE)
    {
      return CTC_E_FEATURE_NOT_SUPPORT;
    }

    CTC_ERROR_RETURN(sys_goldengate_queue_get_queue_id(lchip, &p_sched->queue,
                                                       &queue_id));
    SYS_QOS_QUEUE_LOCK(lchip);
    p_sys_queue_node = ctc_vector_get(p_gg_queue_master[lchip]->queue_vec, queue_id);
    SYS_QOS_QUEUE_UNLOCK(lchip);
    if (NULL == p_sys_queue_node)
    {
        return CTC_E_INVALID_PARAM;
    }

    CTC_ERROR_RETURN(sys_goldengate_queue_get_grp_mode(lchip, queue_id / 8, &mode));

    sub_grp = queue_id / 4;  /*Queue Id :0~2047*/
    /*super group id with slice id*/
    if (mode)
    {
        CTC_BIT_UNSET(sub_grp, 0);
    }

    wfq_offset = p_sched->queue_class;

    SYS_QUEUE_DBG_INFO("group_id = %d\n", sub_grp);

    switch (p_sched->cfg_type)
    {
    case CTC_QOS_SCHED_CFG_CONFIRM_CLASS:
    {
        CTC_MAX_VALUE_CHECK(p_sched->class_priority, QOS_SCHED_MAX_CHAN_CLASS);
        CTC_MAX_VALUE_CHECK(wfq_offset, QOS_SCHED_MAX_QUE_CLASS);
        field_val = p_sched->class_priority;
        field_id = DsSchServiceProfile_grpSchPri0RemapChPri_f + wfq_offset;
        cmd = DRV_IOW(DsSchServiceProfile_t, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, sub_grp, cmd, &field_val));
    }
    break;
    case CTC_QOS_SCHED_CFG_CONFIRM_WEIGHT:
      {
          uint32 weight = 0;
          uint16 threshold = 0;
          uint8  shift = 0;
          DsGrpWfqWeight_m  grp_wfq_weight;

          CTC_MIN_VALUE_CHECK(p_sched->weight, 1);
          SYS_QOS_QUEUE_LOCK(lchip);
          weight = (p_sched->weight* p_gg_queue_master[lchip]->wrr_weight_mtu);
          SYS_QOS_QUEUE_UNLOCK(lchip);
          CTC_MAX_VALUE_CHECK(weight, 0x3FFFFFF);

          CTC_ERROR_RETURN(_sys_goldengate_queue_sch_threshold_compute(lchip, weight, &threshold, &shift));
          SYS_QUEUE_DBG_INFO("weight = %d, threshold = %d, shift = %d\n", weight, threshold, shift);

          cmd = DRV_IOR(DsGrpWfqWeight_t, DRV_ENTRY_FLAG);
          CTC_ERROR_RETURN(DRV_IOCTL(lchip, sub_grp, cmd, &grp_wfq_weight));
  		  SetDsGrpWfqWeight(V, weightShift_f, &grp_wfq_weight, shift);
          SetDsGrpWfqWeight(V, weight_f, &grp_wfq_weight, threshold);
          cmd = DRV_IOW(DsGrpWfqWeight_t, DRV_ENTRY_FLAG);
          CTC_ERROR_RETURN(DRV_IOCTL(lchip,  sub_grp , cmd, &grp_wfq_weight));
      }
      break;
    default:
        break;
    }
    return CTC_E_NONE;
}


int32
sys_goldengate_queue_sch_set_c2c_group_sched(uint8 lchip, uint8 c2c_group, uint8 class_priority)
{
    DsSchServiceProfile_m sch_service_profile;
    uint16 sub_grp = 0;
    uint32 cmd                     = 0;

    sal_memset(&sch_service_profile, 0, sizeof(DsSchServiceProfile_m));
    sub_grp = p_gg_queue_master[lchip]->queue_base[SYS_QUEUE_TYPE_EXCP] / 4 + c2c_group*2;
    cmd = DRV_IOR(DsSchServiceProfile_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, sub_grp, cmd, &sch_service_profile));

    SetDsSchServiceProfile(V, grpSchPri0RemapChPri_f, &sch_service_profile, class_priority);
    SetDsSchServiceProfile(V, grpSchPri1RemapChPri_f, &sch_service_profile, class_priority);
    SetDsSchServiceProfile(V, grpSchPri2RemapChPri_f, &sch_service_profile, class_priority);
    SetDsSchServiceProfile(V, grpSchPri3RemapChPri_f, &sch_service_profile, class_priority);
    SetDsSchServiceProfile(V, grpSchPri4RemapChPri_f, &sch_service_profile, class_priority);
    SetDsSchServiceProfile(V, grpSchPri5RemapChPri_f, &sch_service_profile, class_priority);
    SetDsSchServiceProfile(V, grpSchPri6RemapChPri_f, &sch_service_profile, class_priority);
    SetDsSchServiceProfile(V, grpSchPri7RemapChPri_f, &sch_service_profile, class_priority);

    cmd = DRV_IOW(DsSchServiceProfile_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, sub_grp, cmd, &sch_service_profile));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, sub_grp + 1, cmd, &sch_service_profile));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, SYS_MAX_QUEUE_GROUP_NUM + sub_grp, cmd, &sch_service_profile));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, SYS_MAX_QUEUE_GROUP_NUM + sub_grp + 1, cmd, &sch_service_profile));

    return CTC_E_NONE;
}

int32
sys_goldengate_queue_sch_get_group_sched(uint8 lchip, uint16 sub_grp,ctc_qos_sched_group_t* p_sched)
{

    sys_queue_node_t* p_sys_queue_node = NULL;
    DsGrpWfqWeight_m  grp_wfq_weight;
    uint32 cmd                     = 0;
    uint16 wfq_offset              = 0;
    uint32 field_val               = 0;
    uint32 field_id                = 0;
    uint16 queue_id                = 0;
    uint16 threshold               = 0;
    uint8  shift                   = 0;
    uint8 mode                     = 0;

    if (sub_grp  == CTC_MAX_UINT16_VALUE)
    {
        CTC_ERROR_RETURN(sys_goldengate_queue_get_queue_id(lchip, &p_sched->queue, &queue_id));
        SYS_QOS_QUEUE_LOCK(lchip);
        p_sys_queue_node = ctc_vector_get(p_gg_queue_master[lchip]->queue_vec, queue_id);
        SYS_QOS_QUEUE_UNLOCK(lchip);
        if (NULL == p_sys_queue_node)
        {
            return CTC_E_INVALID_PARAM;
        }

        /*super group id with slice id*/
        sub_grp = queue_id / 4;  /*Queue Id :0~2047*/
    }

    CTC_ERROR_RETURN(sys_goldengate_queue_get_grp_mode(lchip, sub_grp / 2, &mode));
    if (mode)
    {
        CTC_BIT_UNSET(sub_grp, 0);
    }

	wfq_offset = p_sched->queue_class;
    field_id = DsSchServiceProfile_grpSchPri0RemapChPri_f + wfq_offset;
    cmd = DRV_IOR(DsSchServiceProfile_t, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, sub_grp, cmd, &field_val));
    p_sched->class_priority = field_val; /*0-3*/

    cmd = DRV_IOR(DsGrpWfqWeight_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, sub_grp, cmd, &grp_wfq_weight));
    shift = GetDsGrpWfqWeight(V,  weightShift_f, &grp_wfq_weight);
    threshold = GetDsGrpWfqWeight(V, weight_f, &grp_wfq_weight);
   _sys_goldengate_queue_sch_threshold_compute_to_user(lchip, &field_val, threshold, shift);
   SYS_QOS_QUEUE_LOCK(lchip);
    p_sched->weight = field_val/p_gg_queue_master[lchip]->wrr_weight_mtu;
    SYS_QOS_QUEUE_UNLOCK(lchip);
    return CTC_E_NONE;
}

int32
_sys_goldengate_qos_set_sched(uint8 lchip, ctc_qos_sched_t* p_sched)
{
        switch (p_sched->type)
        {
        case CTC_QOS_SCHED_QUEUE:
            CTC_ERROR_RETURN(sys_goldengate_queue_sch_set_queue_sched(lchip, &p_sched->sched.queue_sched));
            break;

        case CTC_QOS_SCHED_GROUP:
            CTC_ERROR_RETURN(sys_goldengate_queue_sch_set_group_sched(lchip, &p_sched->sched.group_sched));
            break;

        default:
            return CTC_E_INVALID_PARAM;

        }
    return CTC_E_NONE;
}

int32
_sys_goldengate_qos_get_sched(uint8 lchip, ctc_qos_sched_t* p_sched)
{
    uint16 queue_id = 0;
    switch (p_sched->type)
    {
        case CTC_QOS_SCHED_QUEUE:
            CTC_ERROR_RETURN(sys_goldengate_queue_get_queue_id(lchip, &p_sched->sched.queue_sched.queue,
                                                           &queue_id));
            CTC_ERROR_RETURN(sys_goldengate_queue_sch_get_queue_sched(lchip, queue_id, &p_sched->sched.queue_sched));
            break;

        case CTC_QOS_SCHED_GROUP:
            CTC_ERROR_RETURN(sys_goldengate_queue_sch_get_group_sched(lchip, CTC_MAX_UINT16_VALUE, &p_sched->sched.group_sched));
            break;

        default:
            return CTC_E_INVALID_PARAM;

    }
    return CTC_E_NONE;
}

int32
_sys_goldengate_queue_sch_init_wfq_sched(uint8 lchip, uint8 slice_id)
{

    uint32 cmd = 0;
    DsSchServiceProfile_m sch_service_profile;
    DsQueWfqWeight_m que_wfq_weight;
    DsGrpWfqWeight_m  grp_wfq_weight;
    uint16 sub_grp = 0;
    uint32 field_val = 0;
    uint16 queue_id = 0;

    sal_memset(&sch_service_profile, 0, sizeof(DsSchServiceProfile_m));
    sal_memset(&que_wfq_weight, 0, sizeof(que_wfq_weight));
    sal_memset(&grp_wfq_weight, 0, sizeof(grp_wfq_weight));

    field_val = 1;
    cmd = DRV_IOW(QMgrDeqSchCtl_t, QMgrDeqSchCtl_schBasedOnPktLen_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, slice_id, cmd, &field_val));

    SetDsQueWfqWeight(V, pirWeightShift_f, &que_wfq_weight, SYS_WFQ_DEFAULT_SHIFT);
    SetDsQueWfqWeight(V, pirWeight_f, &que_wfq_weight, SYS_WFQ_DEFAULT_WEIGHT);
    cmd = DRV_IOW(DsQueWfqWeight_t, DRV_ENTRY_FLAG);
    for (queue_id = 0; queue_id < SYS_MAX_QUEUE_NUM ;  queue_id++)
    {
        CTC_ERROR_RETURN(DRV_IOCTL(lchip,SYS_MAX_QUEUE_NUM*slice_id + queue_id, cmd, &que_wfq_weight));
    }

    for (sub_grp = 0; sub_grp < SYS_MAX_QUEUE_GROUP_NUM; sub_grp++)
    {
        uint8 mode = 0;
        sys_goldengate_queue_get_grp_mode(lchip, (SYS_MAX_QUEUE_GROUP_NUM*slice_id + sub_grp)/2, &mode);

        cmd = DRV_IOR(DsSchServiceProfile_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, (SYS_MAX_QUEUE_GROUP_NUM*slice_id + sub_grp), cmd, &sch_service_profile));
        if (16 == p_gg_queue_master[lchip]->queue_num_per_chanel)
        {
            if (mode == 1)  /*ucast group*/
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
                    SetDsSchServiceProfile(V, grpSchPri7RemapChPri_f, &sch_service_profile, 2);
                }

            }
            else  /*mcast group*/
            {
                SetDsSchServiceProfile(V, queOffset0GreenRemapSchPri_f, &sch_service_profile, 7);
                SetDsSchServiceProfile(V, queOffset0RedRemapSchPri_f, &sch_service_profile, 8);
                SetDsSchServiceProfile(V, queOffset0YellowRemapSchPri_f, &sch_service_profile, 0);
                SetDsSchServiceProfile(V, queOffset1GreenRemapSchPri_f, &sch_service_profile, 7);
                SetDsSchServiceProfile(V, queOffset1RedRemapSchPri_f, &sch_service_profile, 8);
                SetDsSchServiceProfile(V, queOffset1YellowRemapSchPri_f, &sch_service_profile, 0);
                SetDsSchServiceProfile(V, queOffset2GreenRemapSchPri_f, &sch_service_profile, 7);
                SetDsSchServiceProfile(V, queOffset2RedRemapSchPri_f, &sch_service_profile, 8);
                SetDsSchServiceProfile(V, queOffset2YellowRemapSchPri_f, &sch_service_profile, 0);
                SetDsSchServiceProfile(V, queOffset3GreenRemapSchPri_f, &sch_service_profile, 7);
                SetDsSchServiceProfile(V, queOffset3RedRemapSchPri_f, &sch_service_profile, 8);
                SetDsSchServiceProfile(V, queOffset3YellowRemapSchPri_f, &sch_service_profile, 0);
                SetDsSchServiceProfile(V, queOffset4GreenRemapSchPri_f, &sch_service_profile, 7);
                SetDsSchServiceProfile(V, queOffset4RedRemapSchPri_f, &sch_service_profile, 8);
                SetDsSchServiceProfile(V, queOffset4YellowRemapSchPri_f, &sch_service_profile, 0);
                SetDsSchServiceProfile(V, queOffset5GreenRemapSchPri_f, &sch_service_profile, 7);
                SetDsSchServiceProfile(V, queOffset5RedRemapSchPri_f, &sch_service_profile, 8);
                SetDsSchServiceProfile(V, queOffset5YellowRemapSchPri_f, &sch_service_profile, 0);
                SetDsSchServiceProfile(V, queOffset6GreenRemapSchPri_f, &sch_service_profile, 7);
                SetDsSchServiceProfile(V, queOffset6RedRemapSchPri_f, &sch_service_profile, 8);
                SetDsSchServiceProfile(V, queOffset6YellowRemapSchPri_f, &sch_service_profile, 0);
                SetDsSchServiceProfile(V, queOffset7GreenRemapSchPri_f, &sch_service_profile, 7);
                SetDsSchServiceProfile(V, queOffset7RedRemapSchPri_f, &sch_service_profile, 8);
                SetDsSchServiceProfile(V, queOffset7YellowRemapSchPri_f, &sch_service_profile, 0);

                /*8 group Level WFQ --> 4 Channel level WFQ (Port level)*/
                SetDsSchServiceProfile(V, grpSchPri0RemapChPri_f, &sch_service_profile, 0);
                SetDsSchServiceProfile(V, grpSchPri1RemapChPri_f, &sch_service_profile, 0);
                SetDsSchServiceProfile(V, grpSchPri2RemapChPri_f, &sch_service_profile, 0);
                SetDsSchServiceProfile(V, grpSchPri3RemapChPri_f, &sch_service_profile, 0);
                SetDsSchServiceProfile(V, grpSchPri4RemapChPri_f, &sch_service_profile, 0);
                SetDsSchServiceProfile(V, grpSchPri5RemapChPri_f, &sch_service_profile, 0);
                SetDsSchServiceProfile(V, grpSchPri6RemapChPri_f, &sch_service_profile, 0);
                SetDsSchServiceProfile(V, grpSchPri7RemapChPri_f, &sch_service_profile, 0);
            }
        }
        else
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

        /* 8 Group Level WFQ Select Sub group Sp  (8 bit)*/
        SetDsSchServiceProfile(V, subGrpSp0PriSel_f, &sch_service_profile, 0x03); /* WFQ 0/1 -->Sp0*/
        SetDsSchServiceProfile(V, subGrpSp1PriSel_f, &sch_service_profile, 0x0C); /* WFQ 2/3 -->Sp1*/
        SetDsSchServiceProfile(V, subGrpSp2PriSel_f, &sch_service_profile, 0x30); /* WFQ 4/5 -->Sp2*/
        SetDsSchServiceProfile(V, subGrpSp3PriSel_f, &sch_service_profile, 0xC0); /* WFQ 6/7 -->Sp3*/

        /*  SP sch select WFQ in Sub Group*/
        SetDsSchServiceProfile(V, grpLastSchPri0Sel_f, &sch_service_profile, 1); /* SP 0  -->WFQ 0*/
        SetDsSchServiceProfile(V, grpLastSchPri1Sel_f, &sch_service_profile, 2); /* SP 1  -->WFQ 1*/
        SetDsSchServiceProfile(V, grpLastSchPri2Sel_f, &sch_service_profile, 4); /* SP 2  -->WFQ 2*/
        SetDsSchServiceProfile(V, grpLastSchPri3Sel_f, &sch_service_profile, 8); /* SP 3  -->WFQ 3*/
        cmd = DRV_IOW(DsSchServiceProfile_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, (SYS_MAX_QUEUE_GROUP_NUM*slice_id + sub_grp), cmd, &sch_service_profile));

        SetDsGrpWfqWeight(V, weightShift_f, &grp_wfq_weight, SYS_WFQ_DEFAULT_SHIFT);
        SetDsGrpWfqWeight(V, weight_f, &grp_wfq_weight, SYS_WFQ_DEFAULT_WEIGHT);
        SetDsGrpWfqWeight(V, subGrpSp0WeightShift_f, &grp_wfq_weight, SYS_WFQ_DEFAULT_SHIFT);
        SetDsGrpWfqWeight(V, subGrpSp0Weight_f, &grp_wfq_weight, SYS_WFQ_DEFAULT_WEIGHT);
        SetDsGrpWfqWeight(V, subGrpSp1WeightShift_f, &grp_wfq_weight, SYS_WFQ_DEFAULT_SHIFT);
        SetDsGrpWfqWeight(V, subGrpSp1Weight_f, &grp_wfq_weight, SYS_WFQ_DEFAULT_WEIGHT);
        SetDsGrpWfqWeight(V, subGrpSp2WeightShift_f, &grp_wfq_weight, SYS_WFQ_DEFAULT_SHIFT);
        SetDsGrpWfqWeight(V, subGrpSp2Weight_f, &grp_wfq_weight, SYS_WFQ_DEFAULT_WEIGHT);
        SetDsGrpWfqWeight(V, subGrpSp3WeightShift_f, &grp_wfq_weight, SYS_WFQ_DEFAULT_SHIFT);
        SetDsGrpWfqWeight(V, subGrpSp3Weight_f, &grp_wfq_weight, SYS_WFQ_DEFAULT_WEIGHT);

        cmd = DRV_IOW(DsGrpWfqWeight_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, (SYS_MAX_QUEUE_GROUP_NUM*slice_id + sub_grp), cmd, &grp_wfq_weight));

    }

    field_val = 3; /* bit[2:0], bit2:Mcast; bit1:UcastHigh; bit0:UcastLow, set 1 means use SP*/
    cmd = DRV_IOW(MetFifoWeightCfg_t, MetFifoWeightCfg_cfgMsgSpBitmap_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    return CTC_E_NONE;
}

int32
sys_goldengate_qos_sched_dump_status(uint8 lchip)
{
    uint32 cmd = 0;
    uint32 field_val = 0;

	SYS_QUEUE_DBG_DUMP("-------------------------Scheduler-------------------------\n");

    cmd = DRV_IOR(QMgrDeqSchCtl_t, QMgrDeqSchCtl_schBasedOnPktLen_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    SYS_QUEUE_DBG_DUMP("%-30s: %d bytes\n","Weight MTU", p_gg_queue_master[lchip]->wrr_weight_mtu );
    if (field_val)
    {
        SYS_QUEUE_DBG_DUMP("%-30s: %s\n","Schedule Mode", "WDRR");
    }
    else
    {
        SYS_QUEUE_DBG_DUMP("%-30s: %s\n","Schedule Mode", "WRR");
    }

    SYS_QUEUE_DBG_DUMP("\n");
   return CTC_E_NONE;

}

/**
 @brief Queue scheduler initialization.
*/
int32
sys_goldengate_queue_sch_init(uint8 lchip)
{
    uint8  slice_id = 0;

    p_gg_queue_master[lchip]->wrr_weight_mtu = (SYS_WFQ_DEFAULT_WEIGHT << SYS_WFQ_DEFAULT_SHIFT);

    for (slice_id = 0; slice_id < SYS_MAX_LOCAL_SLICE_NUM; slice_id++)
    {
        CTC_ERROR_RETURN(_sys_goldengate_queue_sch_init_wfq_sched(lchip, slice_id));
    }

    CTC_ERROR_RETURN(sys_goldengate_queue_sch_set_c2c_group_sched(lchip, p_gg_queue_master[lchip]->c2c_group_base, 2));
    CTC_ERROR_RETURN(sys_goldengate_queue_sch_set_c2c_group_sched(lchip, p_gg_queue_master[lchip]->c2c_group_base + 1, 3));
    return CTC_E_NONE;
}


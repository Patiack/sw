/**
 @file sys_greatbelt_queue_sch.c

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

#include "sys_greatbelt_chip.h"
#include "sys_greatbelt_queue_enq.h"
#include "sys_greatbelt_queue_sch.h"

#include "greatbelt/include/drv_io.h"
/****************************************************************************
 *
 * Global and Declaration
 *
 ****************************************************************************/

extern sys_queue_master_t* p_gb_queue_master[CTC_MAX_LOCAL_CHIP_NUM];

#define QOS_SCHED_MAX_QUE_CLASS 7
#define QOS_SCHED_MAX_CHAN_CLASS 3
#define QOS_SCHED_MAX_QUE_WEITGHT 0x1FFF

#define MAX_SCHED_WEIGHT_BASE 1024
/****************************************************************************
 *
 * Function
 *
 ****************************************************************************/

STATIC int32
_sys_greatbelt_qos_sched_threshold_compute(uint8 lchip, uint32 weight,
                                           uint16* p_threshold,
                                           uint8* p_shift,
                                           bool round_up)
{
    uint8 i = 0;
    uint32 value = 0;
    uint32 base = 0;

    static uint16 exponent[16][2] =
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

    CTC_PTR_VALID_CHECK(p_threshold);
    CTC_PTR_VALID_CHECK(p_shift);

    SYS_QUEUE_DBG_FUNC();

    for (i = 0; i < 16; i++)
    {
        base = (weight >> i);

        if (base < MAX_SCHED_WEIGHT_BASE)
        {
            *p_threshold = base;
            *p_shift     = i;
            return CTC_E_NONE;
        }
    }

    value = weight / MAX_SCHED_WEIGHT_BASE;

    for (i = 0; i < 16; i++)
    {
        if ((value >= exponent[i][0]) && (value <= exponent[i][1]))
        {
            /* round up value */
            if (((weight + ((1 << i) - 1)) / (1 << i) >= MAX_SCHED_WEIGHT_BASE) || round_up)
            {
                i++;
            }

            *p_shift = i;
            *p_threshold = (weight + ((1 << i) - 1)) / (1 << i);

            return CTC_E_NONE;
        }
    }

    return CTC_E_EXCEED_MAX_SIZE;

}

STATIC int32
_sys_greatbelt_qos_sched_get_group_id(uint8 lchip, ctc_qos_sched_group_t* p_sched, uint16* group_id)
{
    ctc_qos_queue_id_t queue;
    ctc_qos_queue_id_t queue_temp;
    uint16 queue_id = 0;
    sys_queue_node_t* p_sys_queue_node = NULL;

    sal_memset(&queue_temp, 0, sizeof(ctc_qos_queue_id_t));
    sal_memset(&queue, 0, sizeof(ctc_qos_queue_id_t));
    if (sal_memcmp(&(p_sched->queue), &queue_temp, sizeof(ctc_qos_queue_id_t)))
    {
        sal_memcpy(&queue, &(p_sched->queue), sizeof(ctc_qos_queue_id_t));
    }
    else
    {
        queue.queue_type = p_sched->group_type ? CTC_QUEUE_TYPE_SERVICE_INGRESS : CTC_QUEUE_TYPE_NETWORK_EGRESS;
        queue.gport = p_sched->gport;
        queue.service_id = p_sched->service_id;
    }

    SYS_QOS_QUEUE_LOCK(lchip);
    CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(_sys_greatbelt_queue_get_queue_id(lchip,
                                                   &queue,
                                                   &queue_id));

    SYS_QUEUE_DBG_INFO("queue_id = %d\n", queue_id);

    p_sys_queue_node = ctc_vector_get(p_gb_queue_master[lchip]->queue_vec, queue_id);
    if (NULL == p_sys_queue_node)
    {
        SYS_QOS_QUEUE_UNLOCK(lchip);
        return CTC_E_INVALID_PARAM;
    }
    *group_id = p_sys_queue_node->group_id;
    SYS_QOS_QUEUE_UNLOCK(lchip);
    SYS_QUEUE_DBG_INFO("group_id = %d\n", *group_id);

    return CTC_E_NONE;
}

int32
_sys_greatbelt_qos_set_queue_sched(uint8 lchip, ctc_qos_sched_queue_t* p_sched)
{
    uint32 cmd = 0;
    uint16 queue_id = 0;
    uint16 offset = 0;
    uint16 group_id = 0;
    uint32 field_val = 0;
    uint32 field_id  = 0;
    uint16 index = 0;
    uint16 step = 0;
    sys_queue_node_t* p_sys_queue_node = NULL;
    ds_grp_shp_wfq_ctl_t ds_grp_shp_wfq_ctl;

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

    sal_memset(&ds_grp_shp_wfq_ctl, 0, sizeof(ds_grp_shp_wfq_ctl_t));

    SYS_QOS_QUEUE_LOCK(lchip);
    CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(_sys_greatbelt_queue_get_queue_id(lchip,
                                                       &p_sched->queue,
                                                       &queue_id));

    SYS_QUEUE_DBG_INFO("queue_id = %d\n", queue_id);

    p_sys_queue_node = ctc_vector_get(p_gb_queue_master[lchip]->queue_vec, queue_id);
    SYS_QOS_QUEUE_UNLOCK(lchip);
    if (NULL == p_sys_queue_node)
    {
        return CTC_E_INVALID_PARAM;
    }

    group_id = p_sys_queue_node->group_id;
    offset = p_sys_queue_node->offset;

    SYS_QUEUE_DBG_INFO("group_id = %d\n", group_id);
    SYS_QUEUE_DBG_INFO("queue offset = %d\n", offset);

    switch (p_sched->cfg_type)
    {
    case CTC_QOS_SCHED_CFG_CONFIRM_CLASS:
        CTC_MAX_VALUE_CHECK(p_sched->confirm_class, QOS_SCHED_MAX_QUE_CLASS);
        field_val = p_sched->confirm_class;
        step = DsGrpShpWfqCtl_QueOffset1GreenRemapSchPri_f - DsGrpShpWfqCtl_QueOffset0GreenRemapSchPri_f;
        field_id = DsGrpShpWfqCtl_QueOffset0GreenRemapSchPri_f + offset * step;
        cmd = DRV_IOW(DsGrpShpWfqCtl_t, field_id);
        index = group_id;
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &field_val));
        p_sys_queue_node->confirm_class = p_sched->confirm_class;
        break;

    case CTC_QOS_SCHED_CFG_EXCEED_CLASS:
        CTC_MAX_VALUE_CHECK(p_sched->exceed_class, QOS_SCHED_MAX_QUE_CLASS);
        field_val = p_sched->exceed_class;
        step = DsGrpShpWfqCtl_QueOffset1YellowRemapSchPri_f - DsGrpShpWfqCtl_QueOffset0YellowRemapSchPri_f;
        field_id = DsGrpShpWfqCtl_QueOffset0YellowRemapSchPri_f + offset * step;
        cmd = DRV_IOW(DsGrpShpWfqCtl_t, field_id);
        index = group_id;
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &field_val));
        p_sys_queue_node->exceed_class = p_sched->exceed_class;
        break;

    case CTC_QOS_SCHED_CFG_CONFIRM_WEIGHT:
        {
            uint32 weight = 0;
            uint16 threshold = 0;
            uint8  shift = 0;
            CTC_MIN_VALUE_CHECK(p_sched->confirm_weight, 1);
            SYS_QOS_QUEUE_LOCK(lchip);
            weight = (p_sched->confirm_weight * p_gb_queue_master[lchip]->wrr_weight_mtu);
            SYS_QOS_QUEUE_UNLOCK(lchip);
            CTC_MAX_VALUE_CHECK(weight, 0x3FFFFFF);

            CTC_ERROR_RETURN(_sys_greatbelt_qos_sched_threshold_compute(lchip, weight, &threshold, &shift, FALSE));
            SYS_QUEUE_DBG_INFO("weight = %d, threshold = %d, shift = %d\n", weight, threshold, shift);
            field_val = threshold;
            cmd = DRV_IOW(DsQueShpWfqCtl_t, DsQueShpWfqCtl_CirWeight_f);
            index = queue_id;
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &field_val));
            field_val = shift;
            cmd = DRV_IOW(DsQueShpWfqCtl_t, DsQueShpWfqCtl_CirWeightShift_f);
            index = queue_id;
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &field_val));
            p_sys_queue_node->confirm_weight = p_sched->confirm_weight;
        }

        break;

    case CTC_QOS_SCHED_CFG_EXCEED_WEIGHT:
        {
            uint32 weight = 0;
            uint16 threshold = 0;
            uint8  shift = 0;
            CTC_MIN_VALUE_CHECK(p_sched->exceed_weight, 1);
            SYS_QOS_QUEUE_LOCK(lchip);
            weight = (p_sched->exceed_weight * p_gb_queue_master[lchip]->wrr_weight_mtu);
            SYS_QOS_QUEUE_UNLOCK(lchip);
            CTC_MAX_VALUE_CHECK(weight, 0x3FFFFFF);
            CTC_ERROR_RETURN(_sys_greatbelt_qos_sched_threshold_compute(lchip, weight, &threshold, &shift, FALSE));
            SYS_QUEUE_DBG_INFO("weight = %d, threshold = %d, shift = %d\n", weight, threshold, shift);
            field_val = threshold;
            cmd = DRV_IOW(DsQueShpWfqCtl_t, DsQueShpWfqCtl_PirWeight_f);
            index = queue_id;
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &field_val));
            field_val = shift;
            cmd = DRV_IOW(DsQueShpWfqCtl_t, DsQueShpWfqCtl_PirWeightShift_f);
            index = queue_id;
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &field_val));
            p_sys_queue_node->exceed_weight = p_sched->exceed_weight;

        }
        break;

    default:
        return CTC_E_INVALID_PARAM;

    }

    return CTC_E_NONE;
}

int32
_sys_greatbelt_qos_get_queue_sched(uint8 lchip, ctc_qos_sched_queue_t* p_sched)
{

    uint16 queue_id = 0;
    sys_queue_node_t* p_sys_queue_node = NULL;

    SYS_QUEUE_DBG_FUNC();

    SYS_QOS_QUEUE_LOCK(lchip);
    CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(_sys_greatbelt_queue_get_queue_id(lchip,
                                                       &p_sched->queue,
                                                       &queue_id));

    SYS_QUEUE_DBG_INFO("queue_id = %d\n", queue_id);

    p_sys_queue_node = ctc_vector_get(p_gb_queue_master[lchip]->queue_vec, queue_id);
    SYS_QOS_QUEUE_UNLOCK(lchip);
    if (NULL == p_sys_queue_node)
    {
        return CTC_E_INVALID_PARAM;
    }

    p_sched->confirm_class = p_sys_queue_node->confirm_class;
    p_sched->confirm_weight= p_sys_queue_node->confirm_weight;
    p_sched->exceed_class = p_sys_queue_node->exceed_class;
    p_sched->exceed_weight= p_sys_queue_node->exceed_weight;

    return CTC_E_NONE;

}

int32
_sys_greatbelt_qos_set_group_sched(uint8 lchip, ctc_qos_sched_group_t* p_sched)
{
    uint32 cmd = 0;
    uint16 offset = 0;
    uint16 group_id = 0;
    uint32 field_val = 0;
    uint32 field_id  = 0;

    SYS_QUEUE_DBG_FUNC();

    SYS_QUEUE_DBG_PARAM("p_sched->group_type = %d\n", p_sched->group_type);
    SYS_QUEUE_DBG_PARAM("p_sched->class_priority = %d\n", p_sched->class_priority);
    SYS_QUEUE_DBG_PARAM("p_sched->queue_class = %d\n", p_sched->queue_class);
    SYS_QUEUE_DBG_PARAM("p_sched->service_id = 0x%x\n", p_sched->service_id);
    SYS_QUEUE_DBG_PARAM("p_sched->gport = 0x%x\n", p_sched->gport);

    CTC_ERROR_RETURN(_sys_greatbelt_qos_sched_get_group_id(lchip, p_sched, &group_id));

    switch (p_sched->cfg_type)
    {
    case CTC_QOS_SCHED_CFG_CONFIRM_CLASS:
    {
        CTC_MAX_VALUE_CHECK(p_sched->class_priority, QOS_SCHED_MAX_CHAN_CLASS);
        CTC_MAX_VALUE_CHECK(p_sched->queue_class, QOS_SCHED_MAX_QUE_CLASS);
        field_val = p_sched->class_priority;
        offset    = p_sched->queue_class;
        field_id = DsGrpShpWfqCtl_GrpSchPri0RemapChPri_f + offset;
        cmd = DRV_IOW(DsGrpShpWfqCtl_t, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, group_id, cmd, &field_val));
    }
        break;

    case CTC_QOS_SCHED_CFG_CONFIRM_WEIGHT:
    {
        uint32 weight = 0;
        uint16 threshold = 0;
        uint8  shift = 0;
        ds_grp_shp_wfq_ctl_t ds_grp_shp_wfq_ctl;

        CTC_MIN_VALUE_CHECK(p_sched->weight, 1);
        SYS_QOS_QUEUE_LOCK(lchip);
        weight = (p_sched->weight* p_gb_queue_master[lchip]->wrr_weight_mtu);
        SYS_QOS_QUEUE_UNLOCK(lchip);
        CTC_MAX_VALUE_CHECK(weight, 0x3FFFFFF);

        CTC_ERROR_RETURN(_sys_greatbelt_qos_sched_threshold_compute(lchip, weight, &threshold, &shift, FALSE));
        SYS_QUEUE_DBG_INFO("weight = %d, threshold = %d, shift = %d\n", weight, threshold, shift);

        sal_memset(&ds_grp_shp_wfq_ctl, 0, sizeof(ds_grp_shp_wfq_ctl));
        cmd = DRV_IOR(DsGrpShpWfqCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, group_id, cmd, &ds_grp_shp_wfq_ctl));
        ds_grp_shp_wfq_ctl.weight       = threshold;
        ds_grp_shp_wfq_ctl.weight_shift = shift;
        cmd = DRV_IOW(DsGrpShpWfqCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, group_id, cmd, &ds_grp_shp_wfq_ctl));

    }
        break;

    default:
        break;
    }



    return CTC_E_NONE;
}

int32
_sys_greatbelt_qos_get_group_sched(uint8 lchip, ctc_qos_sched_group_t* p_sched)
{
    uint32 cmd = 0;
    uint16 offset = 0;
    uint16 group_id = 0;
    uint32 field_val = 0;
    uint32 field_id  = 0;

    SYS_QUEUE_DBG_FUNC();

    SYS_QUEUE_DBG_PARAM("p_sched->group_type = %d\n", p_sched->group_type);
    SYS_QUEUE_DBG_PARAM("p_sched->queue_class = %d\n", p_sched->queue_class);
    SYS_QUEUE_DBG_PARAM("p_sched->gport = 0x%x\n", p_sched->gport);

    CTC_MAX_VALUE_CHECK(p_sched->queue_class, QOS_SCHED_MAX_QUE_CLASS);
    CTC_ERROR_RETURN(_sys_greatbelt_qos_sched_get_group_id(lchip, p_sched, &group_id));

    offset    = p_sched->queue_class;
    field_id = DsGrpShpWfqCtl_GrpSchPri0RemapChPri_f + offset;
    cmd = DRV_IOR(DsGrpShpWfqCtl_t, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, group_id, cmd, &field_val));
    p_sched->class_priority = field_val;

    return CTC_E_NONE;
}

int32
_sys_greatbelt_qos_set_sched(uint8 lchip, ctc_qos_sched_t* p_sched)
{

    switch (p_sched->type)
    {
    case CTC_QOS_SCHED_QUEUE:
        CTC_ERROR_RETURN(_sys_greatbelt_qos_set_queue_sched(lchip, &p_sched->sched.queue_sched));
        break;

    case CTC_QOS_SCHED_GROUP:
        CTC_ERROR_RETURN(_sys_greatbelt_qos_set_group_sched(lchip, &p_sched->sched.group_sched));
        break;

    default:
        return CTC_E_INVALID_PARAM;

    }


    return CTC_E_NONE;
}

int32
_sys_greatbelt_qos_get_sched(uint8 lchip, ctc_qos_sched_t* p_sched)
{

    switch (p_sched->type)
    {
    case CTC_QOS_SCHED_QUEUE:
        CTC_ERROR_RETURN(_sys_greatbelt_qos_get_queue_sched(lchip, &p_sched->sched.queue_sched));
        break;

    case CTC_QOS_SCHED_GROUP:
        CTC_ERROR_RETURN(_sys_greatbelt_qos_get_group_sched(lchip, &p_sched->sched.group_sched));
        break;

    default:
        return CTC_E_INVALID_PARAM;

    }

    return CTC_E_NONE;
}

int32
_sys_greatbelt_qos_init_wfq_sched(uint8 lchip)
{
    ds_grp_shp_wfq_ctl_t ds_grp_shp_wfq_ctl;
    uint32 cmd = 0;
    uint16 group_id = 0;
    uint32 field_val = 0;

    field_val = 1;
    cmd = DRV_IOW(QMgrDeqSchCtl_t, QMgrDeqSchCtl_SchBasedOnPktLen_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    sal_memset(&ds_grp_shp_wfq_ctl, 0, sizeof(ds_grp_shp_wfq_ctl_t));

    for (group_id = 0; group_id < SYS_MAX_QUEUE_GROUP_NUM; group_id++)
    {
        cmd = DRV_IOR(DsGrpShpWfqCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, group_id, cmd, &ds_grp_shp_wfq_ctl));
        ds_grp_shp_wfq_ctl.weight_shift         = SYS_WFQ_DEFAULT_SHIFT;
        ds_grp_shp_wfq_ctl.weight               = SYS_WFQ_DEFAULT_WEIGHT;

        ds_grp_shp_wfq_ctl.que_offset0_green_remap_sch_pri = 0;
        ds_grp_shp_wfq_ctl.que_offset1_green_remap_sch_pri = 1;
        ds_grp_shp_wfq_ctl.que_offset2_green_remap_sch_pri = 2;
        ds_grp_shp_wfq_ctl.que_offset3_green_remap_sch_pri = 3;
        ds_grp_shp_wfq_ctl.que_offset4_green_remap_sch_pri = 4;
        ds_grp_shp_wfq_ctl.que_offset5_green_remap_sch_pri = 5;
        ds_grp_shp_wfq_ctl.que_offset6_green_remap_sch_pri = 6;
        ds_grp_shp_wfq_ctl.que_offset7_green_remap_sch_pri = 7;

        ds_grp_shp_wfq_ctl.que_offset0_yellow_remap_sch_pri = 0;
        ds_grp_shp_wfq_ctl.que_offset1_yellow_remap_sch_pri = 1;
        ds_grp_shp_wfq_ctl.que_offset2_yellow_remap_sch_pri = 2;
        ds_grp_shp_wfq_ctl.que_offset3_yellow_remap_sch_pri = 3;
        ds_grp_shp_wfq_ctl.que_offset4_yellow_remap_sch_pri = 4;
        ds_grp_shp_wfq_ctl.que_offset5_yellow_remap_sch_pri = 5;
        ds_grp_shp_wfq_ctl.que_offset6_yellow_remap_sch_pri = 6;
        ds_grp_shp_wfq_ctl.que_offset7_yellow_remap_sch_pri = 7;

        ds_grp_shp_wfq_ctl.que_offset0_red_remap_sch_pri = 8;
        ds_grp_shp_wfq_ctl.que_offset1_red_remap_sch_pri = 8;
        ds_grp_shp_wfq_ctl.que_offset2_red_remap_sch_pri = 8;
        ds_grp_shp_wfq_ctl.que_offset3_red_remap_sch_pri = 8;
        ds_grp_shp_wfq_ctl.que_offset4_red_remap_sch_pri = 8;
        ds_grp_shp_wfq_ctl.que_offset5_red_remap_sch_pri = 8;
        ds_grp_shp_wfq_ctl.que_offset6_red_remap_sch_pri = 8;
        ds_grp_shp_wfq_ctl.que_offset7_red_remap_sch_pri = 8;

        ds_grp_shp_wfq_ctl.grp_sch_pri0_remap_ch_pri = 0;
        ds_grp_shp_wfq_ctl.grp_sch_pri1_remap_ch_pri = 0;
        ds_grp_shp_wfq_ctl.grp_sch_pri2_remap_ch_pri = 1;
        ds_grp_shp_wfq_ctl.grp_sch_pri3_remap_ch_pri = 1;
        ds_grp_shp_wfq_ctl.grp_sch_pri4_remap_ch_pri = 2;
        ds_grp_shp_wfq_ctl.grp_sch_pri5_remap_ch_pri = 2;
        ds_grp_shp_wfq_ctl.grp_sch_pri6_remap_ch_pri = 3;
        ds_grp_shp_wfq_ctl.grp_sch_pri7_remap_ch_pri = 3;

        ds_grp_shp_wfq_ctl.sub_grp0_weight_shift = SYS_WFQ_DEFAULT_SHIFT;
        ds_grp_shp_wfq_ctl.sub_grp0_weight       = SYS_WFQ_DEFAULT_WEIGHT;
        ds_grp_shp_wfq_ctl.sub_grp1_weight_shift = SYS_WFQ_DEFAULT_SHIFT;
        ds_grp_shp_wfq_ctl.sub_grp1_weight       = SYS_WFQ_DEFAULT_WEIGHT;
        ds_grp_shp_wfq_ctl.sub_grp_sp0_pri_sel = 0;
        ds_grp_shp_wfq_ctl.sub_grp_sp1_pri_sel = 0;
        ds_grp_shp_wfq_ctl.sub_grp_sp2_pri_sel = 0xFF;

        cmd = DRV_IOW(DsGrpShpWfqCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, group_id, cmd, &ds_grp_shp_wfq_ctl));

    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_queue_sch_set_c2c_group_sched(uint8 lchip, uint8 group_id, uint8 class_priority)
{
    ds_grp_shp_wfq_ctl_t ds_grp_shp_wfq_ctl;
    uint32 cmd = 0;
    uint32 field_val = 0;

    field_val = 1;
    cmd = DRV_IOW(QMgrDeqSchCtl_t, QMgrDeqSchCtl_SchBasedOnPktLen_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    sal_memset(&ds_grp_shp_wfq_ctl, 0, sizeof(ds_grp_shp_wfq_ctl_t));


    cmd = DRV_IOR(DsGrpShpWfqCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, group_id, cmd, &ds_grp_shp_wfq_ctl));
    ds_grp_shp_wfq_ctl.weight_shift         = SYS_WFQ_DEFAULT_SHIFT;
    ds_grp_shp_wfq_ctl.weight               = SYS_WFQ_DEFAULT_WEIGHT;

    ds_grp_shp_wfq_ctl.que_offset0_green_remap_sch_pri = 0;
    ds_grp_shp_wfq_ctl.que_offset1_green_remap_sch_pri = 1;
    ds_grp_shp_wfq_ctl.que_offset2_green_remap_sch_pri = 2;
    ds_grp_shp_wfq_ctl.que_offset3_green_remap_sch_pri = 3;
    ds_grp_shp_wfq_ctl.que_offset4_green_remap_sch_pri = 4;
    ds_grp_shp_wfq_ctl.que_offset5_green_remap_sch_pri = 5;
    ds_grp_shp_wfq_ctl.que_offset6_green_remap_sch_pri = 6;
    ds_grp_shp_wfq_ctl.que_offset7_green_remap_sch_pri = 7;

    ds_grp_shp_wfq_ctl.que_offset0_yellow_remap_sch_pri = 0;
    ds_grp_shp_wfq_ctl.que_offset1_yellow_remap_sch_pri = 1;
    ds_grp_shp_wfq_ctl.que_offset2_yellow_remap_sch_pri = 2;
    ds_grp_shp_wfq_ctl.que_offset3_yellow_remap_sch_pri = 3;
    ds_grp_shp_wfq_ctl.que_offset4_yellow_remap_sch_pri = 4;
    ds_grp_shp_wfq_ctl.que_offset5_yellow_remap_sch_pri = 5;
    ds_grp_shp_wfq_ctl.que_offset6_yellow_remap_sch_pri = 6;
    ds_grp_shp_wfq_ctl.que_offset7_yellow_remap_sch_pri = 7;

    ds_grp_shp_wfq_ctl.que_offset0_red_remap_sch_pri = 8;
    ds_grp_shp_wfq_ctl.que_offset1_red_remap_sch_pri = 8;
    ds_grp_shp_wfq_ctl.que_offset2_red_remap_sch_pri = 8;
    ds_grp_shp_wfq_ctl.que_offset3_red_remap_sch_pri = 8;
    ds_grp_shp_wfq_ctl.que_offset4_red_remap_sch_pri = 8;
    ds_grp_shp_wfq_ctl.que_offset5_red_remap_sch_pri = 8;
    ds_grp_shp_wfq_ctl.que_offset6_red_remap_sch_pri = 8;
    ds_grp_shp_wfq_ctl.que_offset7_red_remap_sch_pri = 8;

    ds_grp_shp_wfq_ctl.grp_sch_pri0_remap_ch_pri = class_priority;
    ds_grp_shp_wfq_ctl.grp_sch_pri1_remap_ch_pri = class_priority;
    ds_grp_shp_wfq_ctl.grp_sch_pri2_remap_ch_pri = class_priority;
    ds_grp_shp_wfq_ctl.grp_sch_pri3_remap_ch_pri = class_priority;
    ds_grp_shp_wfq_ctl.grp_sch_pri4_remap_ch_pri = class_priority;
    ds_grp_shp_wfq_ctl.grp_sch_pri5_remap_ch_pri = class_priority;
    ds_grp_shp_wfq_ctl.grp_sch_pri6_remap_ch_pri = class_priority;
    ds_grp_shp_wfq_ctl.grp_sch_pri7_remap_ch_pri = class_priority;

    ds_grp_shp_wfq_ctl.sub_grp0_weight_shift = SYS_WFQ_DEFAULT_SHIFT;
    ds_grp_shp_wfq_ctl.sub_grp0_weight       = SYS_WFQ_DEFAULT_WEIGHT;
    ds_grp_shp_wfq_ctl.sub_grp1_weight_shift = SYS_WFQ_DEFAULT_SHIFT;
    ds_grp_shp_wfq_ctl.sub_grp1_weight       = SYS_WFQ_DEFAULT_WEIGHT;
    ds_grp_shp_wfq_ctl.sub_grp_sp0_pri_sel = 0;
    ds_grp_shp_wfq_ctl.sub_grp_sp1_pri_sel = 0;
    ds_grp_shp_wfq_ctl.sub_grp_sp2_pri_sel = 0xFF;

    cmd = DRV_IOW(DsGrpShpWfqCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, group_id, cmd, &ds_grp_shp_wfq_ctl));


    return CTC_E_NONE;
}
/**
 @brief Queue scheduler initialization.
*/
int32
sys_greatbelt_queue_sch_init(uint8 lchip)
{

    p_gb_queue_master[lchip]->wrr_weight_mtu = (SYS_WFQ_DEFAULT_WEIGHT << SYS_WFQ_DEFAULT_SHIFT);

    CTC_ERROR_RETURN(_sys_greatbelt_qos_init_wfq_sched(lchip));


    return CTC_E_NONE;
}


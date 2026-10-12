/**
 @file sys_goldengate_aclqos_policer.c

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
#include "ctc_warmboot.h"

#include "sys_goldengate_chip.h"
#include "sys_goldengate_port.h"
#include "sys_goldengate_qos.h"

#include "sys_goldengate_qos_class.h"
#include "sys_goldengate_qos_policer.h"
#include "sys_goldengate_queue_enq.h"
#include "sys_goldengate_queue_shape.h"
#include "sys_goldengate_queue_sch.h"
#include "sys_goldengate_queue_drop.h"
#include "sys_goldengate_cpu_reason.h"
#include "sys_goldengate_wb_common.h"
#include "sys_goldengate_register.h"
#include "sys_goldengate_opf.h"

#include "goldengate/include/drv_enum.h"
#include "goldengate/include/drv_io.h"
/* #include "goldengate/include/drv_lib.h" --never--*/

/****************************************************************************
  *
  * Defines and Macros
  *
  ****************************************************************************/
int32
sys_goldengate_qos_policer_wb_sync(uint8 lchip);
int32
sys_goldengate_qos_policer_wb_restore(uint8 lchip);
int32
sys_goldengate_qos_wb_sync(uint8 lchip);
int32
sys_goldengate_qos_queue_wb_restore(uint8 lchip);

extern sys_queue_master_t* p_gg_queue_master[CTC_MAX_LOCAL_CHIP_NUM];

/*init*/
extern int32
sys_goldengate_qos_init(uint8 lchip, void* p_glb_parm)
{
    int32 ret = CTC_E_NONE;

    if (p_gg_queue_master[lchip])
    {
        return CTC_E_NONE;
    }

    CTC_ERROR_RETURN(sys_goldengate_qos_policer_init(lchip, p_glb_parm));

    CTC_ERROR_RETURN(sys_goldengate_queue_enq_init(lchip, p_glb_parm));

    CTC_ERROR_RETURN(sys_goldengate_qos_class_init(lchip));

    CTC_ERROR_RETURN(sys_goldengate_queue_shape_init(lchip, p_glb_parm));

    CTC_ERROR_RETURN(sys_goldengate_queue_sch_init(lchip));

    CTC_ERROR_RETURN(sys_goldengate_queue_drop_init(lchip, p_glb_parm));

    CTC_ERROR_RETURN(sys_goldengate_queue_cpu_reason_init(lchip));

    CTC_ERROR_RETURN(sys_goldengate_wb_sync_register_cb(lchip, CTC_FEATURE_QOS, sys_goldengate_qos_wb_sync));

    if (CTC_WB_ENABLE && CTC_WB_STATUS(lchip) == CTC_WB_STATUS_RELOADING)
    {
        CTC_ERROR_RETURN(sys_goldengate_qos_policer_wb_restore(lchip));
        CTC_ERROR_RETURN(sys_goldengate_qos_queue_wb_restore(lchip));
    }

    return ret;
}

extern int32
sys_goldengate_qos_deinit(uint8 lchip)
{
    if (NULL == p_gg_queue_master[lchip])
    {
        return CTC_E_NONE;
    }

    CTC_ERROR_RETURN(sys_goldengate_qos_policer_deinit(lchip));

    CTC_ERROR_RETURN(sys_goldengate_queue_deinit(lchip));

    return CTC_E_NONE;
}

/*policer*/
extern int32
sys_goldengate_qos_set_policer(uint8 lchip, ctc_qos_policer_t* p_policer)
{
    CTC_ERROR_RETURN(sys_goldengate_qos_policer_set(lchip, p_policer));
    return CTC_E_NONE;
}

extern int32
sys_goldengate_qos_get_policer(uint8 lchip, ctc_qos_policer_t* p_policer)
{
    CTC_ERROR_RETURN(sys_goldengate_qos_policer_get(lchip, p_policer));
    return CTC_E_NONE;
}

/*shape*/
extern int32
sys_goldengate_qos_set_shape(uint8 lchip, ctc_qos_shape_t* p_shape)
{
    CTC_ERROR_RETURN(_sys_goldengate_qos_set_shape(lchip, p_shape));
    return CTC_E_NONE;
}

/*schedule*/
extern int32
sys_goldengate_qos_set_sched(uint8 lchip, ctc_qos_sched_t* p_sched)
{
    CTC_ERROR_RETURN(_sys_goldengate_qos_set_sched(lchip, p_sched));
    return CTC_E_NONE;
}

extern int32
sys_goldengate_qos_get_sched(uint8 lchip, ctc_qos_sched_t* p_sched)
{
    CTC_ERROR_RETURN(_sys_goldengate_qos_get_sched(lchip, p_sched));
    return CTC_E_NONE;
}
/*mapping*/
extern int32
sys_goldengate_qos_set_domain_map(uint8 lchip, ctc_qos_domain_map_t* p_domain_map)
{
    CTC_ERROR_RETURN(sys_goldengate_qos_domain_map_set(lchip, p_domain_map));
    return CTC_E_NONE;
}

extern int32
sys_goldengate_qos_get_domain_map(uint8 lchip, ctc_qos_domain_map_t* p_domain_map)
{
    CTC_ERROR_RETURN(sys_goldengate_qos_domain_map_get(lchip, p_domain_map));
    return CTC_E_NONE;
}

/*global param*/
extern int32
sys_goldengate_qos_set_global_config(uint8 lchip, ctc_qos_glb_cfg_t* p_glb_cfg)
{
    switch (p_glb_cfg->cfg_type)
    {
    case CTC_QOS_GLB_CFG_POLICER_EN:
        CTC_ERROR_RETURN(
            sys_goldengate_qos_set_policer_update_enable(lchip, p_glb_cfg->u.value));
        break;

    case CTC_QOS_GLB_CFG_POLICER_STATS_EN:
        CTC_ERROR_RETURN(
            sys_goldengate_qos_set_policer_stats_enable(lchip, p_glb_cfg->u.value));
        break;

    case  CTC_QOS_GLB_CFG_POLICER_IPG_EN:
        CTC_ERROR_RETURN(
            sys_goldengate_qos_set_policer_ipg_enable(lchip, p_glb_cfg->u.value));
        break;

    case CTC_QOS_GLB_CFG_POLICER_SEQENCE_EN:
        CTC_ERROR_RETURN(
            sys_goldengate_qos_set_policer_sequential_enable(lchip, p_glb_cfg->u.value));
        break;

    case  CTC_QOS_GLB_CFG_QUE_SHAPE_EN:
        CTC_ERROR_RETURN(
            sys_goldengate_qos_set_queue_shape_enable(lchip, p_glb_cfg->u.value));
        break;

    case  CTC_QOS_GLB_CFG_GROUP_SHAPE_EN:
        CTC_ERROR_RETURN(
            sys_goldengate_qos_set_group_shape_enable(lchip, p_glb_cfg->u.value));
        break;

    case  CTC_QOS_GLB_CFG_PORT_SHAPE_EN:
        CTC_ERROR_RETURN(
            sys_goldengate_qos_set_port_shape_enable(lchip, p_glb_cfg->u.value));
        break;

    case CTC_QOS_GLB_CFG_SHAPE_IPG_EN:
        CTC_ERROR_RETURN(
            sys_goldengate_qos_set_shape_ipg_enable(lchip, p_glb_cfg->u.value));
        break;

    case CTC_QOS_GLB_CFG_POLICER_FLOW_FIRST_EN:
        CTC_ERROR_RETURN(
        sys_goldengate_qos_set_policer_flow_first(lchip, (p_glb_cfg->u.value >> 16) & 0xFFFF,
                                                  (p_glb_cfg->u.value & 0xFFFF)));
        break;

    case CTC_QOS_GLB_CFG_RESRC_MGR_EN:
        CTC_ERROR_RETURN(
            sys_goldengate_qos_resrc_mgr_en(lchip, p_glb_cfg->u.value));
        break;

    case CTC_QOS_GLB_CFG_QUE_STATS_EN:
        return CTC_E_NOT_SUPPORT;
        break;


    case CTC_QOS_GLB_CFG_PHB_MAP:
        CTC_ERROR_RETURN(
            sys_goldengate_qos_set_phb(lchip, &p_glb_cfg->u.phb_map));
        break;

    case CTC_QOS_GLB_CFG_REASON_SHAPE_PKT_EN:
        CTC_ERROR_RETURN(
            sys_goldengate_qos_set_reason_shp_base_pkt_en(lchip, p_glb_cfg->u.value));
        break;

    case CTC_QOS_GLB_CFG_POLICER_HBWP_SHARE_EN:
        CTC_ERROR_RETURN(
            sys_goldengate_qos_set_policer_hbwp_share_enable(lchip, p_glb_cfg->u.value));
        break;
    case CTC_QOS_GLB_CFG_QUEUE_DROP_MONITOR_EN:
        CTC_ERROR_RETURN(
            sys_goldengate_qos_set_monitor_drop_queue_id(lchip, &p_glb_cfg->u.drop_monitor));
        break;
    case CTC_QOS_GLB_CFG_TRUNCATION_LEN:
        CTC_ERROR_RETURN(
            sys_goldengate_cpu_reason_set_truncation_length(lchip, p_glb_cfg->u.value));
        break;

    case CTC_QOS_GLB_CFG_POLICER_MARK_ECN_EN:
        CTC_ERROR_RETURN(
            sys_goldengate_qos_set_policer_mark_ecn_enable(lchip, p_glb_cfg->u.value));
        break;

    case CTC_QOS_GLB_CFG_SCH_WRR_EN:
        CTC_ERROR_RETURN(
            sys_goldengate_qos_set_sch_wrr_enable(lchip, p_glb_cfg->u.value));
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

extern int32
sys_goldengate_qos_get_global_config(uint8 lchip, ctc_qos_glb_cfg_t* p_glb_cfg)
{
    switch (p_glb_cfg->cfg_type)
    {
    case CTC_QOS_GLB_CFG_QUEUE_DROP_MONITOR_EN:
        CTC_ERROR_RETURN(
            sys_goldengate_qos_get_monitor_drop_queue_id(lchip, &p_glb_cfg->u.drop_monitor));
        break;
    default:
        return CTC_E_INVALID_PARAM;
    }
    return CTC_E_NONE;
}

/*queue*/
extern int32
sys_goldengate_qos_set_queue(uint8 lchip, ctc_qos_queue_cfg_t* p_que_cfg)
{
    CTC_ERROR_RETURN(sys_goldengate_qos_queue_set(lchip, p_que_cfg));
    return CTC_E_NONE;
}

/*drop*/
extern int32
sys_goldengate_qos_set_drop_scheme(uint8 lchip, ctc_qos_drop_t* p_drop)
{
    CTC_ERROR_RETURN(sys_goldengate_queue_set_drop(lchip, p_drop));
    return CTC_E_NONE;
}

extern int32
sys_goldengate_qos_get_drop_scheme(uint8 lchip, ctc_qos_drop_t* p_drop)
{
    CTC_ERROR_RETURN(sys_goldengate_queue_get_drop(lchip, p_drop));
    return CTC_E_NONE;
}

/*Resrc*/
extern int32
sys_goldengate_qos_set_resrc(uint8 lchip, ctc_qos_resrc_t* p_resrc)
{
    CTC_ERROR_RETURN(sys_goldengate_queue_set_resrc(lchip, p_resrc));
    return CTC_E_NONE;
}

extern int32
sys_goldengate_qos_get_resrc(uint8 lchip, ctc_qos_resrc_t* p_resrc)
{
    CTC_ERROR_RETURN(sys_goldengate_queue_get_resrc(lchip, p_resrc));
    return CTC_E_NONE;
}

extern int32
sys_goldengate_qos_query_pool_stats(uint8 lchip, ctc_qos_resrc_pool_stats_t* p_stats)
{
    CTC_ERROR_RETURN(sys_goldengate_queue_query_pool_stats(lchip, p_stats));
    return CTC_E_NONE;
}

/*stats*/
extern int32
sys_goldengate_qos_query_queue_stats(uint8 lchip, ctc_qos_queue_stats_t* p_queue_stats)
{
    CTC_ERROR_RETURN(sys_goldengate_queue_stats_query(lchip, p_queue_stats));
    return CTC_E_NONE;
}

/*stats*/
extern int32
sys_goldengate_qos_clear_queue_stats(uint8 lchip, ctc_qos_queue_stats_t* p_queue_stats)
{
    CTC_ERROR_RETURN(sys_goldengate_queue_stats_clear(lchip, p_queue_stats));
    return CTC_E_NONE;
}

extern int32
sys_goldengate_qos_query_policer_stats(uint8 lchip, ctc_qos_policer_stats_t* p_policer_stats)
{
    CTC_ERROR_RETURN(sys_goldengate_qos_policer_stats_query(lchip, p_policer_stats));
    return CTC_E_NONE;
}

extern int32
sys_goldengate_qos_clear_policer_stats(uint8 lchip, ctc_qos_policer_stats_t* p_policer_stats)
{
    CTC_ERROR_RETURN(sys_goldengate_qos_policer_stats_clear(lchip, p_policer_stats));
    return CTC_E_NONE;
}

extern int32
sys_goldengate_qos_set_port_to_statcking_port(uint8 lchip, uint16 gport,uint8 enable)
{
	CTC_ERROR_RETURN(sys_goldengate_queue_set_to_stacking_port(lchip, gport,enable));
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_qos_wb_mapping_queue_node(uint8 lchip, sys_wb_qos_queue_node_t *p_wb_node, sys_queue_node_t *p_node, uint8 sync)
{
    sys_goldengate_opf_t opf;
    int32 ret = CTC_E_NONE;
    uint32 cmd = 0;
    uint8 index = 0;
    uint32 profile_id = 0;
    uint32 sc = 0;
    uint32 value = 0;
    uint8  level_num = 0;
    uint8  cng_level = 0;
    uint8  slice_id = 0;
    sys_queue_drop_profile_t* p_drop_profile_get = NULL;
    sys_queue_drop_profile_t* p_drop_profile_new = NULL;
    DsQueThrdProfile_m drop_profile;
    sys_queue_shp_profile_t* p_shp_profile_get = NULL;
    sys_queue_shp_profile_t* p_shp_profile_new = NULL;
    DsQueShpProfile_m shp_profile;

    if (sync)
    {
        p_wb_node->queue_id = p_node->queue_id;
        p_wb_node->type = p_node->type;
        p_wb_node->offset = p_node->offset;
        p_wb_node->shp_en = p_node->shp_en;
        p_wb_node->is_cpu_que_prof = p_node->p_shp_profile ? p_node->p_shp_profile->is_cpu_que_prof : 0;
    }
    else
    {
        p_node->queue_id = p_wb_node->queue_id;
        p_node->type = p_wb_node->type;
        p_node->offset = p_wb_node->offset;
        p_node->shp_en = p_wb_node->shp_en;

        cmd = DRV_IOR(DsEgrResrcCtl_t, DsEgrResrcCtl_queThrdProfType_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_node->queue_id, cmd, &profile_id));

        if (profile_id > 1) /*profile_id 0/1 is reserve for static spool */
        {
            /* get queue sc */
            cmd = DRV_IOR(DsEgrResrcCtl_t, DsEgrResrcCtl_mappedSc_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_node->queue_id, cmd, &sc));

            /*add drop spool*/
            p_drop_profile_new = mem_malloc(MEM_QUEUE_MODULE, sizeof(sys_queue_drop_profile_t));
            if (NULL == p_drop_profile_new)
            {
                ret = CTC_E_NO_MEMORY;
                return ret;
            }

            sal_memset(p_drop_profile_new, 0, sizeof(sys_queue_drop_profile_t));

            level_num = p_gg_queue_master[lchip]->egs_pool[sc].egs_congest_level_num;
            for (cng_level = 0; cng_level < level_num; cng_level++)
            {
                sal_memset(&drop_profile, 0, sizeof(DsQueThrdProfile_m));

                cmd = DRV_IOR(DsQueThrdProfile_t, DRV_ENTRY_FLAG);
                index = (profile_id << 3) + cng_level + (8-level_num);
                CTC_ERROR_GOTO((DRV_IOCTL(lchip, index, cmd, &drop_profile)), ret, ERROR_RETURN1);

                p_drop_profile_new->profile[cng_level].wred_max_thrd0 = GetDsQueThrdProfile(V, wredMaxThrd0_f, &drop_profile);
                p_drop_profile_new->profile[cng_level].wred_max_thrd1 = GetDsQueThrdProfile(V, wredMaxThrd1_f, &drop_profile);
                p_drop_profile_new->profile[cng_level].wred_max_thrd2 = GetDsQueThrdProfile(V, wredMaxThrd2_f, &drop_profile);
                p_drop_profile_new->profile[cng_level].wred_max_thrd3 = GetDsQueThrdProfile(V, wredMaxThrd3_f, &drop_profile);
                p_drop_profile_new->profile[cng_level].ecn_mark_thrd = GetDsQueThrdProfile(V, ecnMarkThrd_f, &drop_profile);
                p_drop_profile_new->profile[cng_level].factor0 = GetDsQueThrdProfile(V, factor0_f, &drop_profile);
                p_drop_profile_new->profile[cng_level].factor1 = GetDsQueThrdProfile(V, factor1_f, &drop_profile);
                p_drop_profile_new->profile[cng_level].factor2 = GetDsQueThrdProfile(V, factor2_f, &drop_profile);
                p_drop_profile_new->profile[cng_level].factor3 = GetDsQueThrdProfile(V, factor3_f, &drop_profile);
            }

            p_drop_profile_new->profile_id = profile_id;
            ret = ctc_spool_add(p_gg_queue_master[lchip]->p_drop_profile_pool, p_drop_profile_new, NULL,&p_drop_profile_get);
            if (ret < 0)
            {
                CTC_WB_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "ctc_spool_add error! ret: %d.\n", ret);

                goto ERROR_RETURN1;
            }
            else
            {
                if (ret == CTC_SPOOL_E_OPERATE_MEMORY)
                {
                    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
                    opf.pool_type = OPF_QUEUE_DROP_PROFILE;
                    opf.pool_index = SYS_RESRC_PROFILE_QUEUE;
                    /*alloc index from position*/
                    ret = sys_goldengate_opf_alloc_offset_from_position(lchip, &opf, 1, profile_id);
                    if (ret)
                    {
                        ctc_spool_remove(p_gg_queue_master[lchip]->p_drop_profile_pool, p_drop_profile_get, NULL);
                        CTC_WB_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "alloc QueThrdProfile table offset from position %u error! ret: %d.\n", profile_id, ret);

                        goto ERROR_RETURN1;
                    }
                }
                else /* means get an old. */
                {
                    mem_free(p_drop_profile_new);
                    p_drop_profile_new = NULL;
                }
            }

            p_drop_profile_get->profile_id = profile_id;
            p_node->p_drop_profile = p_drop_profile_get;
        }

        /*add shp spool*/

        cmd = DRV_IOR(DsQueShpProfId_t, DsQueShpProfId_profId_f);
        CTC_ERROR_GOTO((DRV_IOCTL(lchip, p_node->queue_id, cmd, &profile_id)), ret, ERROR_RETURN1);

        if (profile_id > 1)  /*profile_id 0/1 is reserve*/
        {
            p_shp_profile_new = mem_malloc(MEM_QUEUE_MODULE, sizeof(sys_queue_shp_profile_t));
            if (NULL == p_shp_profile_new)
            {
                ret = CTC_E_NO_MEMORY;
                return ret;
            }

            sal_memset(p_shp_profile_new, 0, sizeof(sys_queue_shp_profile_t));
            sal_memset(&shp_profile, 0, sizeof(DsQueShpProfile_m));

            cmd = DRV_IOR(DsQueShpProfile_t, DRV_ENTRY_FLAG);
            slice_id =  SYS_MAP_QUEUEID_TO_SLICE(p_node->queue_id);
            index = (slice_id *64 ) + profile_id;
            CTC_ERROR_GOTO((DRV_IOCTL(lchip, index, cmd, &shp_profile)), ret,ERROR_RETURN1);

            p_shp_profile_new->bucket_rate = GetDsQueShpProfile(V, tokenRateFrac_f, &shp_profile);
            value = GetDsQueShpProfile(V, tokenRate_f, &shp_profile);
            p_shp_profile_new->bucket_rate = (p_shp_profile_new->bucket_rate & 0xFF) | (value << 8);

            p_shp_profile_new->bucket_thrd = GetDsQueShpProfile(V, tokenThrdShift_f, &shp_profile);
            value = GetDsQueShpProfile(V, tokenThrd_f, &shp_profile);
            p_shp_profile_new->bucket_thrd = (p_shp_profile_new->bucket_thrd & 0xF) | (value << 4);

            p_shp_profile_new->is_cpu_que_prof = p_wb_node->is_cpu_que_prof;
            p_shp_profile_new->profile_id = profile_id;
            ret = ctc_spool_add(p_gg_queue_master[lchip]->p_queue_profile_pool[slice_id], p_shp_profile_new, NULL, &p_shp_profile_get);
            if (ret < 0)
            {
                CTC_WB_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "ctc_spool_add error! ret: %d.\n", ret);

                goto ERROR_RETURN1;
            }
            else
            {
                if (ret == CTC_SPOOL_E_OPERATE_MEMORY)
                {
                    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
                    opf.pool_type = OPF_QUEUE_SHAPE_PROFILE;
                    opf.pool_index = slice_id;
                    if (p_shp_profile_new->is_cpu_que_prof)
                    {
                        opf.pool_index = 2;
                    }
                    /*alloc index from position*/
                    ret = sys_goldengate_opf_alloc_offset_from_position(lchip, &opf, 1, profile_id);
                    if (ret)
                    {
                        ctc_spool_remove(p_gg_queue_master[lchip]->p_queue_profile_pool[slice_id], p_shp_profile_get, NULL);
                        CTC_WB_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "alloc QueShpProfile table offset from position %u error! ret: %d.\n", profile_id, ret);

                        goto ERROR_RETURN1;
                    }
                }
                else /* means get an old. */
                {
                    mem_free(p_shp_profile_new);
                    p_shp_profile_new = NULL;
                }
            }

            p_shp_profile_get->profile_id = profile_id;
            p_node->p_shp_profile = p_shp_profile_get;
        }
    }

     return ret;

ERROR_RETURN1:
    if (p_drop_profile_new)
    {
        mem_free(p_drop_profile_new);
    }

    if (p_shp_profile_new)
    {
        mem_free(p_shp_profile_new);
    }
    return ret;
}


STATIC int32
_sys_goldengate_qos_wb_sync_queue_node(void* array_data, uint32 index, void *user_data)
{
    uint16 max_entry_cnt = 0;
    sys_queue_node_t *p_node = (sys_queue_node_t *)array_data;
    sys_wb_qos_queue_node_t  *p_wb_node;
    sys_traverse_t *traversal_data = (sys_traverse_t *)user_data;
    ctc_wb_data_t *wb_data = (ctc_wb_data_t *)traversal_data->data;
    uint8 lchip = (uint8)traversal_data->value1;

    max_entry_cnt = CTC_WB_DATA_BUFFER_LENGTH / (wb_data->key_len + wb_data->data_len);

    p_wb_node = (sys_wb_qos_queue_node_t *)wb_data->buffer + wb_data->valid_cnt;
    sal_memset(p_wb_node, 0, sizeof(sys_wb_qos_queue_node_t));

    CTC_ERROR_RETURN(_sys_goldengate_qos_wb_mapping_queue_node(lchip, p_wb_node, p_node, 1));
    if (++wb_data->valid_cnt == max_entry_cnt)
    {
        CTC_ERROR_RETURN(ctc_wb_add_entry(wb_data));
        wb_data->valid_cnt = 0;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_qos_wb_mapping_queue_group_node(uint8 lchip, sys_wb_qos_queue_group_node_t *p_wb_node, sys_queue_group_node_t *p_node, uint8 sync)
{
    sys_goldengate_opf_t opf;
    int32 ret = CTC_E_NONE;
    uint32 cmd = 0;
    uint8 index = 0;
    uint8 loop = 0;
    uint32 profile_id = 0;
    uint32 token_rate;
    uint32 token_rate_frac;
    uint32 token_threshold;
    uint32 token_thrd_shift;
    uint32 fld_id = 0 ;
    uint32 fld_value = 0;
    uint8 group_mode = 0;
    uint8  slice_id = 0;
    uint8  cnt = 0;
    uint8  sub_group_index = 0;
    sys_group_shp_profile_t* p_group_shp_profile_new = NULL;
    sys_group_shp_profile_t* p_group_shp_profile_get = NULL;
    DsGrpShpProfile_m grp_shp_profile;

    if (sync)
    {
        p_wb_node->group_id = p_node->group_id;
        p_wb_node->chan_id = p_node->chan_id;
        p_wb_node->sub_group_id = p_node->sub_group_id;
        p_wb_node->shp_bitmap[0] = p_node->grp_shp[0].shp_bitmap;
        p_wb_node->shp_bitmap[1] = p_node->grp_shp[1].shp_bitmap;
    }
    else
    {
        p_node->group_id = p_wb_node->group_id;
        p_node->chan_id = p_wb_node->chan_id;
        p_node->sub_group_id = p_wb_node->sub_group_id;
        p_node->grp_shp[0].shp_bitmap = p_wb_node->shp_bitmap[0];
        p_node->grp_shp[1].shp_bitmap = p_wb_node->shp_bitmap[1];

        /*add group shp spool*/
        CTC_ERROR_RETURN(sys_goldengate_queue_get_grp_mode(lchip, p_node->sub_group_id/2, &group_mode));
        index = p_node->sub_group_id;
        if (group_mode)
        {
            CTC_BIT_UNSET(index, 0);
        }

        cmd = DRV_IOR(RaGrpShpProfId_t, RaGrpShpProfId_profId_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &profile_id));
        profile_id = (profile_id - CTC_IS_BIT_SET(p_node->sub_group_id, 0))/2;

        if (profile_id != 0)
        {
            p_group_shp_profile_new = mem_malloc(MEM_QUEUE_MODULE, sizeof(sys_queue_shp_profile_t));
            if (NULL == p_group_shp_profile_new)
            {
                ret = CTC_E_NO_MEMORY;
                return ret;
            }

            sal_memset(p_group_shp_profile_new, 0, sizeof(sys_group_shp_profile_t));
            sal_memset(&grp_shp_profile, 0, sizeof(DsGrpShpProfile_m));

            sub_group_index = group_mode ? 0 : (p_node->sub_group_id % 2);

            for (loop = 0; loop < 8; loop++)
            {
                fld_id = DsSchServiceProfile_queOffset0SelCir_f +
                (DsSchServiceProfile_queOffset1SelCir_f - DsSchServiceProfile_queOffset0SelCir_f) * loop;
                cmd = DRV_IOR(DsSchServiceProfile_t, fld_id);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &fld_value));

                if ((7 == fld_value) || (3 == fld_value))
                {
                    p_node->grp_shp[sub_group_index].c_bucket[loop].cir_type = SYS_QUEUE_GRP_SHP_CIR_TYPE_CIR_FAIL;
                }
                else if ((group_mode && (4 == fld_value)) || ((0 == group_mode) && (0 == fld_value)))
                {
                    p_node->grp_shp[sub_group_index].c_bucket[loop].cir_type = SYS_QUEUE_GRP_SHP_CIR_TYPE_CIR_PASS;
                }
                else
                {
                    p_node->grp_shp[sub_group_index].c_bucket[loop].cir_type = SYS_QUEUE_GRP_SHP_CIR_TYPE_CIR_USER;
                    p_node->grp_shp[sub_group_index].c_bucket[loop].bucket_sel = fld_value;
                    p_group_shp_profile_new->c_bucket[cnt].bucket_sel = fld_value;
                    p_group_shp_profile_new->c_bucket[cnt].cir_type = SYS_QUEUE_GRP_SHP_CIR_TYPE_CIR_USER;
                    cnt++;
                }
            }

            cmd = DRV_IOR(DsGrpShpProfile_t, DRV_ENTRY_FLAG);
            slice_id = SYS_MAP_GROUPID_TO_SLICE(p_node->group_id);
            index = (slice_id *32) + profile_id;
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &grp_shp_profile));

            if (group_mode || !CTC_IS_BIT_SET(p_node->sub_group_id, 0))
            {
                GetDsGrpShpProfile(A, bucket0TokenRate_f,       &grp_shp_profile, &token_rate);
                GetDsGrpShpProfile(A, bucket0TokenRateFrac_f,   &grp_shp_profile, &token_rate_frac);
                GetDsGrpShpProfile(A, bucket0TokenThrd_f,       &grp_shp_profile, &token_threshold);
                GetDsGrpShpProfile(A, bucket0TokenThrdShift_f,  &grp_shp_profile, &token_thrd_shift);

                p_group_shp_profile_new->c_bucket[0].rate = token_rate_frac | (token_rate << 8);
                p_group_shp_profile_new->c_bucket[0].thrd = token_thrd_shift | (token_threshold << 4);
            }

            if (group_mode)
            {
                GetDsGrpShpProfile(A, bucket1TokenRate_f,       &grp_shp_profile, &token_rate);
                GetDsGrpShpProfile(A, bucket1TokenRateFrac_f,   &grp_shp_profile, &token_rate_frac);
                GetDsGrpShpProfile(A, bucket1TokenThrd_f,       &grp_shp_profile, &token_threshold);
                GetDsGrpShpProfile(A, bucket1TokenThrdShift_f,  &grp_shp_profile, &token_thrd_shift);

                p_group_shp_profile_new->c_bucket[1].rate = token_rate_frac | (token_rate << 8);
                p_group_shp_profile_new->c_bucket[1].thrd = token_thrd_shift | (token_threshold << 4);

                GetDsGrpShpProfile(A, bucket2TokenRate_f,       &grp_shp_profile, &token_rate);
                GetDsGrpShpProfile(A, bucket2TokenRateFrac_f,   &grp_shp_profile, &token_rate_frac);
                GetDsGrpShpProfile(A, bucket2TokenThrd_f,       &grp_shp_profile, &token_threshold);
                GetDsGrpShpProfile(A, bucket2TokenThrdShift_f,  &grp_shp_profile, &token_thrd_shift);

                p_group_shp_profile_new->c_bucket[2].rate = token_rate_frac | (token_rate << 8);
                p_group_shp_profile_new->c_bucket[2].thrd = token_thrd_shift | (token_threshold << 4);

            }
            else if (CTC_IS_BIT_SET(p_node->sub_group_id, 0))
            {
                GetDsGrpShpProfile(A, bucket2TokenRate_f,       &grp_shp_profile, &token_rate);
                GetDsGrpShpProfile(A, bucket2TokenRateFrac_f,   &grp_shp_profile, &token_rate_frac);
                GetDsGrpShpProfile(A, bucket2TokenThrd_f,       &grp_shp_profile, &token_threshold);
                GetDsGrpShpProfile(A, bucket2TokenThrdShift_f,  &grp_shp_profile, &token_thrd_shift);

                p_group_shp_profile_new->c_bucket[0].rate = token_rate_frac | (token_rate << 8);
                p_group_shp_profile_new->c_bucket[0].thrd = token_thrd_shift | (token_threshold << 4);
            }

            if (group_mode || CTC_IS_BIT_SET(p_node->sub_group_id, 0))
            {
                GetDsGrpShpProfile(A, bucket3TokenRate_f,       &grp_shp_profile, &token_rate);
                GetDsGrpShpProfile(A, bucket3TokenRateFrac_f,   &grp_shp_profile, &token_rate_frac);
                GetDsGrpShpProfile(A, bucket3TokenThrd_f,       &grp_shp_profile, &token_threshold);
                GetDsGrpShpProfile(A, bucket3TokenThrdShift_f,  &grp_shp_profile, &token_thrd_shift);
            }
            else
            {
                GetDsGrpShpProfile(A, bucket1TokenRate_f,       &grp_shp_profile, &token_rate);
                GetDsGrpShpProfile(A, bucket1TokenRateFrac_f,   &grp_shp_profile, &token_rate_frac);
                GetDsGrpShpProfile(A, bucket1TokenThrd_f,       &grp_shp_profile, &token_threshold);
                GetDsGrpShpProfile(A, bucket1TokenThrdShift_f,  &grp_shp_profile, &token_thrd_shift);
            }

            p_group_shp_profile_new->bucket_rate = token_rate_frac | (token_rate << 8);
            p_group_shp_profile_new->bucket_thrd = token_thrd_shift | (token_threshold << 4);
            p_group_shp_profile_new->profile_id = profile_id;
             /*p_group_shp_profile_new->exact_match   only used for remove spool*/


            ret = ctc_spool_add(p_gg_queue_master[lchip]->p_group_profile_pool[slice_id], p_group_shp_profile_new, NULL, &p_group_shp_profile_get);
            if (ret < 0)
            {
                CTC_WB_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "ctc_spool_add error! ret: %d.\n", ret);

                goto ERROR_RETURN1;
            }
            else
            {
                if (ret == CTC_SPOOL_E_OPERATE_MEMORY)
                {
                    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
                    opf.pool_type = OPF_GROUP_SHAPE_PROFILE;
                    opf.pool_index = slice_id;
                    /*alloc index from position*/
                    ret = sys_goldengate_opf_alloc_offset_from_position(lchip, &opf, 1, profile_id);
                    if (ret)
                    {
                        ctc_spool_remove(p_gg_queue_master[lchip]->p_group_profile_pool[slice_id], p_group_shp_profile_get, NULL);
                        CTC_WB_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "alloc GrpShpProfile table offset from position %u error! ret: %d.\n", profile_id, ret);

                        goto ERROR_RETURN1;
                    }
                }
                else /* means get an old. */
                {
                    mem_free(p_group_shp_profile_new);
                    p_group_shp_profile_new = NULL;
                }
            }

            p_group_shp_profile_get->profile_id = profile_id;
            p_node->grp_shp[sub_group_index].p_shp_profile = p_group_shp_profile_get;
        }
    }

     return ret;

ERROR_RETURN1:
    if (p_group_shp_profile_new)
    {
        mem_free(p_group_shp_profile_new);
    }

    return ret;
}


STATIC int32
_sys_goldengate_qos_wb_sync_queue_group_node(void* array_data, uint32 index, void *user_data)
{
    uint16 max_entry_cnt = 0;
    sys_queue_group_node_t *p_node = (sys_queue_group_node_t *)array_data;
    sys_wb_qos_queue_group_node_t  *p_wb_node;
    sys_traverse_t *traversal_data = (sys_traverse_t *)user_data;
    ctc_wb_data_t *wb_data = (ctc_wb_data_t *)traversal_data->data;
    uint8 lchip = (uint8)traversal_data->value1;

    max_entry_cnt = CTC_WB_DATA_BUFFER_LENGTH / (wb_data->key_len + wb_data->data_len);

    p_wb_node = (sys_wb_qos_queue_group_node_t *)wb_data->buffer + wb_data->valid_cnt;
    sal_memset(p_wb_node, 0, sizeof(sys_wb_qos_queue_group_node_t));

    CTC_ERROR_RETURN(_sys_goldengate_qos_wb_mapping_queue_group_node(lchip, p_wb_node, p_node, 1));
    if (++wb_data->valid_cnt == max_entry_cnt)
    {
        CTC_ERROR_RETURN(ctc_wb_add_entry(wb_data));
        wb_data->valid_cnt = 0;
    }

    return CTC_E_NONE;
}


int32
sys_goldengate_qos_wb_sync(uint8 lchip)
{
    int32 ret = CTC_E_NONE;
    uint8 index = 0;
    uint16 loop = 0;
    uint32  max_entry_cnt = 0;
    ctc_wb_data_t wb_data;
    sys_traverse_t user_data;
    sys_wb_qos_queue_master_t  *p_wb_queue_master;
    sys_wb_qos_cpu_reason_t wb_cpu_reason;

    /*syncup policer*/
    CTC_ERROR_RETURN(sys_goldengate_qos_policer_wb_sync(lchip));

    /*syncup queue matser*/
    wb_data.buffer = mem_malloc(MEM_QUEUE_MODULE, CTC_WB_DATA_BUFFER_LENGTH);
    if (NULL == wb_data.buffer)
    {
        ret = CTC_E_NO_MEMORY;
        goto done;
    }
    sal_memset(wb_data.buffer, 0, CTC_WB_DATA_BUFFER_LENGTH);

    CTC_WB_INIT_DATA_T((&wb_data),sys_wb_qos_queue_master_t, CTC_FEATURE_QOS, SYS_WB_APPID_QOS_SUBID_QUEUE_MASTER);

    p_wb_queue_master = (sys_wb_qos_queue_master_t *)wb_data.buffer;

    p_wb_queue_master->lchip = lchip;
    p_wb_queue_master->queue_num_per_chanel = p_gg_queue_master[lchip]->queue_num_per_chanel;
    p_wb_queue_master->enq_mode = p_gg_queue_master[lchip]->enq_mode;
    p_wb_queue_master->queue_num_for_cpu_reason = p_gg_queue_master[lchip]->queue_num_for_cpu_reason;
    p_wb_queue_master->c2c_group_base = p_gg_queue_master[lchip]->c2c_group_base;

    for (index = 0; index < SYS_QUEUE_TYPE_MAX; index++)
    {
        p_wb_queue_master->queue_base[index] = p_gg_queue_master[lchip]->queue_base[index];
    }

    p_wb_queue_master->shp_pps_en = p_gg_queue_master[lchip]->shp_pps_en;
    p_wb_queue_master->wrr_weight_mtu = p_gg_queue_master[lchip]->wrr_weight_mtu;

    sal_memcpy((uint8*)&p_wb_queue_master->egs_pool, (uint8*)&p_gg_queue_master[lchip]->egs_pool, sizeof(sys_qos_resc_egs_pool_t) * CTC_QOS_EGS_RESRC_POOL_MAX);
    sal_memcpy((uint8*)&p_wb_queue_master->igs_port_min, (uint8*)&p_gg_queue_master[lchip]->igs_port_min, sizeof(sys_qos_resc_igs_port_min_t) * SYS_RESRC_IGS_MAX_PORT_MIN_PROFILE);

    wb_data.valid_cnt = 1;
    CTC_ERROR_GOTO(ctc_wb_add_entry(&wb_data), ret, done);


    /*syncup queue node*/
    CTC_WB_INIT_DATA_T((&wb_data),sys_wb_qos_queue_node_t, CTC_FEATURE_QOS, SYS_WB_APPID_QOS_SUBID_QUEUE_NODE);
    user_data.data = &wb_data;
    user_data.value1 = lchip;

    CTC_ERROR_GOTO(ctc_vector_traverse2(p_gg_queue_master[lchip]->queue_vec, 0, _sys_goldengate_qos_wb_sync_queue_node, (void *)&user_data), ret, done);
    if (wb_data.valid_cnt > 0)
    {
        CTC_ERROR_GOTO(ctc_wb_add_entry(&wb_data), ret, done);
        wb_data.valid_cnt = 0;
    }

    /*syncup queue group node*/
    CTC_WB_INIT_DATA_T((&wb_data),sys_wb_qos_queue_group_node_t, CTC_FEATURE_QOS, SYS_WB_APPID_QOS_SUBID_QUEUE_GROUP_NODE);
    user_data.data = &wb_data;
    user_data.value1 = lchip;

    CTC_ERROR_GOTO(ctc_vector_traverse2(p_gg_queue_master[lchip]->group_vec, 0, _sys_goldengate_qos_wb_sync_queue_group_node, (void *)&user_data), ret, done);
    if (wb_data.valid_cnt > 0)
    {
        CTC_ERROR_GOTO(ctc_wb_add_entry(&wb_data), ret, done);
        wb_data.valid_cnt = 0;
    }


    /*syncup cpu reason*/
    CTC_WB_INIT_DATA_T((&wb_data),sys_wb_qos_cpu_reason_t, CTC_FEATURE_QOS, SYS_WB_APPID_QOS_SUBID_CPU_REASON);
    max_entry_cnt = CTC_WB_DATA_BUFFER_LENGTH/(wb_data.key_len + wb_data.data_len);
    sal_memset(wb_data.buffer, 0, CTC_WB_DATA_BUFFER_LENGTH);

    sal_memset(&wb_cpu_reason, 0, sizeof(sys_wb_qos_cpu_reason_t));
    do
    {
        if (CTC_MAX_UINT16_VALUE == p_gg_queue_master[lchip]->cpu_reason[loop].sub_queue_id)
        {
            continue;
        }

        wb_cpu_reason.reason_id = loop;
        wb_cpu_reason.cpu_reason.dest_type = p_gg_queue_master[lchip]->cpu_reason[loop].dest_type;
        wb_cpu_reason.cpu_reason.is_user_define = p_gg_queue_master[lchip]->cpu_reason[loop].is_user_define;
        wb_cpu_reason.cpu_reason.dsfwd_offset = p_gg_queue_master[lchip]->cpu_reason[loop].dsfwd_offset;
        wb_cpu_reason.cpu_reason.dest_port = p_gg_queue_master[lchip]->cpu_reason[loop].dest_port;
        wb_cpu_reason.cpu_reason.sub_queue_id = p_gg_queue_master[lchip]->cpu_reason[loop].sub_queue_id;
        wb_cpu_reason.cpu_reason.dest_map = p_gg_queue_master[lchip]->cpu_reason[loop].dest_map;

        sal_memcpy((uint8*)wb_data.buffer + wb_data.valid_cnt * sizeof(sys_wb_qos_cpu_reason_t),  (uint8*)&wb_cpu_reason, sizeof(sys_wb_qos_cpu_reason_t));
        if (++wb_data.valid_cnt == max_entry_cnt)
        {
            CTC_ERROR_GOTO(ctc_wb_add_entry(&wb_data), ret, done);
            wb_data.valid_cnt = 0;
        }
    }while(++loop < CTC_PKT_CPU_REASON_MAX_COUNT);

    if(wb_data.valid_cnt)
    {
        CTC_ERROR_GOTO(ctc_wb_add_entry(&wb_data), ret, done);
    }

done:
    if (wb_data.buffer)
    {
        mem_free(wb_data.buffer);
    }

    return ret;
}

int32
sys_goldengate_qos_queue_wb_restore(uint8 lchip)
{
    uint16 entry_cnt = 0;
    int32 ret = CTC_E_NONE;
    uint8 index = 0;
    uint8 slice_id = 0;
    uint8 lchan_id = 0;
    uint8 chan_id = 0;
    uint8 priority_class = 0;
    uint32 profile_id = 0;
    uint32 cmd = 0;
    ctc_wb_query_t wb_query;
    sys_wb_qos_queue_master_t  *p_wb_queue_master;
    sys_wb_qos_queue_node_t *p_wb_queue_node;
    sys_queue_node_t *p_node;
    sys_wb_qos_queue_group_node_t *p_wb_queue_group_node;
    sys_queue_group_node_t *p_group_node;
    sys_qos_fc_profile_t *p_sys_fc_profile_get = NULL;
    sys_qos_fc_profile_t *p_sys_fc_profile_new = NULL;
    sys_goldengate_opf_t opf;
    DsIgrPortTcThrdProfile_m ds_igr_port_tc_thrd_profile;
    DsIgrPortThrdProfile_m ds_igr_port_thrd_profile;
    sys_wb_qos_cpu_reason_t *p_wb_cpu_reason;

    wb_query.buffer = mem_malloc(MEM_QUEUE_MODULE,  CTC_WB_DATA_BUFFER_LENGTH);
    if (NULL == wb_query.buffer)
    {
        ret = CTC_E_NO_MEMORY;
        goto done;
    }
    sal_memset(wb_query.buffer, 0, CTC_WB_DATA_BUFFER_LENGTH);

    CTC_WB_INIT_QUERY_T((&wb_query),sys_wb_qos_queue_master_t, CTC_FEATURE_QOS, SYS_WB_APPID_QOS_SUBID_QUEUE_MASTER);

    CTC_ERROR_GOTO(ctc_wb_query_entry(&wb_query), ret, done);

    /*restore  qos_queue_master*/
    if (wb_query.valid_cnt != 1 || wb_query.is_end != 1)
    {
        CTC_WB_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "query qos_policer master error! valid_cnt: %d, is_end: %d.\n", wb_query.valid_cnt, wb_query.is_end);
        goto done;
    }

    p_wb_queue_master = (sys_wb_qos_queue_master_t  *)wb_query.buffer;

    p_gg_queue_master[lchip]->queue_num_per_chanel = p_wb_queue_master->queue_num_per_chanel;
    p_gg_queue_master[lchip]->enq_mode = p_wb_queue_master->enq_mode;
    p_gg_queue_master[lchip]->queue_num_for_cpu_reason = p_wb_queue_master->queue_num_for_cpu_reason;
    p_gg_queue_master[lchip]->c2c_group_base = p_wb_queue_master->c2c_group_base;

    for (index = 0; index < SYS_QUEUE_TYPE_MAX; index++)
    {
        p_gg_queue_master[lchip]->queue_base[index] = p_wb_queue_master->queue_base[index];
    }

    p_gg_queue_master[lchip]->shp_pps_en = p_wb_queue_master->shp_pps_en;
    p_gg_queue_master[lchip]->wrr_weight_mtu = p_wb_queue_master->wrr_weight_mtu;

    sal_memcpy((uint8*)&p_gg_queue_master[lchip]->egs_pool, (uint8*)&p_wb_queue_master->egs_pool, sizeof(sys_qos_resc_egs_pool_t) * CTC_QOS_EGS_RESRC_POOL_MAX);
    sal_memcpy((uint8*)&p_gg_queue_master[lchip]->igs_port_min, (uint8*)&p_wb_queue_master->igs_port_min, sizeof(sys_qos_resc_igs_port_min_t) * SYS_RESRC_IGS_MAX_PORT_MIN_PROFILE);



    /*restore  queue node*/
    CTC_WB_INIT_QUERY_T((&wb_query),sys_wb_qos_queue_node_t, CTC_FEATURE_QOS, SYS_WB_APPID_QOS_SUBID_QUEUE_NODE);

    CTC_WB_QUERY_ENTRY_BEGIN((&wb_query));
        p_wb_queue_node = (sys_wb_qos_queue_node_t *)wb_query.buffer + entry_cnt++;

        p_node = mem_malloc(MEM_QUEUE_MODULE,  sizeof(sys_queue_node_t));
        if (NULL == p_node)
        {
            ret = CTC_E_NO_MEMORY;

            goto done;
        }
        sal_memset(p_node, 0, sizeof(sys_queue_node_t));

        ret = _sys_goldengate_qos_wb_mapping_queue_node(lchip, p_wb_queue_node, p_node, 0);
        if (ret)
        {
            continue;
        }

        /*add to soft table*/
        ctc_vector_add(p_gg_queue_master[lchip]->queue_vec, p_node->queue_id, p_node);

    CTC_WB_QUERY_ENTRY_END((&wb_query));


    /*restore  queue group node*/
    CTC_WB_INIT_QUERY_T((&wb_query),sys_wb_qos_queue_group_node_t, CTC_FEATURE_QOS, SYS_WB_APPID_QOS_SUBID_QUEUE_GROUP_NODE);

    CTC_WB_QUERY_ENTRY_BEGIN((&wb_query));
        p_wb_queue_group_node = (sys_wb_qos_queue_group_node_t *)wb_query.buffer + entry_cnt++;

        p_group_node = mem_malloc(MEM_QUEUE_MODULE,  sizeof(sys_queue_group_node_t));
        if (NULL == p_group_node)
        {
            ret = CTC_E_NO_MEMORY;

            goto done;
        }
        sal_memset(p_group_node, 0, sizeof(sys_queue_group_node_t));

        ret = _sys_goldengate_qos_wb_mapping_queue_group_node(lchip, p_wb_queue_group_node, p_group_node, 0);
        if (ret)
        {
            continue;
        }

        /*add to soft table*/
        ctc_vector_add(p_gg_queue_master[lchip]->group_vec, p_group_node->group_id, p_group_node);

    CTC_WB_QUERY_ENTRY_END((&wb_query));


    /*restore  pfc/fc profile*/
    for (slice_id = 0; slice_id < SYS_MAX_LOCAL_SLICE_NUM; slice_id++)
    {
        for (lchan_id = 0; lchan_id < SYS_MAX_CHANNEL_ID; lchan_id++)
        {
            index = 96 *slice_id + lchan_id;
            chan_id = SYS_MAX_CHANNEL_ID * slice_id + lchan_id;

            /*restore  pfc profile*/
            for (priority_class = 0; priority_class < 8; priority_class++)
            {
                cmd = DRV_IOR(DsIgrPortTcThrdProfId_t, DsIgrPortTcThrdProfId_profIdHigh0_f + priority_class);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &profile_id));
                if (profile_id > 1)
                {
                    p_sys_fc_profile_new = mem_malloc(MEM_QUEUE_MODULE, sizeof(sys_qos_fc_profile_t));
                    if (NULL == p_sys_fc_profile_new)
                    {
                        ret = CTC_E_NO_MEMORY;
                        goto done;
                    }

                    sal_memset(p_sys_fc_profile_new, 0, sizeof(sys_qos_fc_profile_t));
                    sal_memset(&ds_igr_port_tc_thrd_profile, 0, sizeof(DsIgrPortTcThrdProfile_m));

                    cmd = DRV_IOR(DsIgrPortTcThrdProfile_t, DRV_ENTRY_FLAG);
                    DRV_IOCTL(lchip, profile_id << 3, cmd, &ds_igr_port_tc_thrd_profile);
                    GetDsIgrPortTcThrdProfile(A, portTcThrd_f, &ds_igr_port_tc_thrd_profile, &p_sys_fc_profile_new->thrd);
                    p_sys_fc_profile_new->thrd = p_sys_fc_profile_new->thrd * 16;
                    GetDsIgrPortTcThrdProfile(A, portTcXoffThrd_f  , &ds_igr_port_tc_thrd_profile, &p_sys_fc_profile_new->xoff);
                    GetDsIgrPortTcThrdProfile(A, portTcXonThrd_f  , &ds_igr_port_tc_thrd_profile, &p_sys_fc_profile_new->xon);

                    p_sys_fc_profile_new->head.profile_id = profile_id;
                    p_sys_fc_profile_new->head.profile_len = sizeof(sys_qos_fc_profile_t);

                    ret = ctc_spool_add(p_gg_queue_master[lchip]->p_resrc_profile_pool[SYS_RESRC_PROFILE_PFC], p_sys_fc_profile_new, NULL, &p_sys_fc_profile_get);
                    if (ret < 0)
                    {
                        CTC_WB_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "ctc_spool_add error! ret: %d.\n", ret);

                        goto done;
                    }
                    else
                    {
                        if (ret == CTC_SPOOL_E_OPERATE_MEMORY)
                        {
                            sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
                            opf.pool_type = OPF_QUEUE_DROP_PROFILE;
                            opf.pool_index = SYS_RESRC_PROFILE_PFC;

                            /*alloc index from position*/
                            ret = sys_goldengate_opf_alloc_offset_from_position(lchip, &opf, 1, profile_id);
                            if (ret)
                            {
                                ctc_spool_remove(p_gg_queue_master[lchip]->p_resrc_profile_pool[SYS_RESRC_PROFILE_PFC], p_sys_fc_profile_get, NULL);
                                CTC_WB_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "alloc IgrPortTcThrdProfile table offset from position %u error! ret: %d.\n", profile_id, ret);

                                goto done;
                            }
                        }
                        else /* means get an old. */
                        {
                            mem_free(p_sys_fc_profile_new);
                            p_sys_fc_profile_new = NULL;
                        }
                    }

                    p_sys_fc_profile_get->head.profile_id = profile_id;
                    p_gg_queue_master[lchip]->p_pfc_profile[chan_id][priority_class] = p_sys_fc_profile_get;
                }
            }

            /*restore  fc profile*/
            index = slice_id*40 + (lchan_id) / 8;
            cmd = DRV_IOR(DsIgrPortThrdProfId_t,  DsIgrPortThrdProfId_profIdHigh0_f + (lchan_id) % 8);
            DRV_IOCTL(lchip, index, cmd, &profile_id);

            if (profile_id > 1)
            {
                p_sys_fc_profile_new = NULL;
                p_sys_fc_profile_new = mem_malloc(MEM_QUEUE_MODULE, sizeof(sys_qos_fc_profile_t));
                if (NULL == p_sys_fc_profile_new)
                {
                    ret = CTC_E_NO_MEMORY;
                    goto done;
                }

                sal_memset(p_sys_fc_profile_new, 0, sizeof(sys_qos_fc_profile_t));
                sal_memset(&ds_igr_port_thrd_profile, 0, sizeof(DsIgrPortThrdProfile_m));

                cmd = DRV_IOR(DsIgrPortThrdProfile_t, DRV_ENTRY_FLAG);
                DRV_IOCTL(lchip, profile_id << 3, cmd, &ds_igr_port_thrd_profile);

                GetDsIgrPortThrdProfile(A, portThrd_f, &ds_igr_port_thrd_profile, &p_sys_fc_profile_new->thrd);
                p_sys_fc_profile_new->thrd = p_sys_fc_profile_new->thrd * 16;
                GetDsIgrPortThrdProfile(A, portXoffThrd_f  , &ds_igr_port_thrd_profile, &p_sys_fc_profile_new->xoff);
                GetDsIgrPortThrdProfile(A, portXonThrd_f  , &ds_igr_port_thrd_profile, &p_sys_fc_profile_new->xon);

                p_sys_fc_profile_new->head.profile_id = profile_id;
                p_sys_fc_profile_new->head.profile_len = sizeof(sys_qos_fc_profile_t);

                ret = ctc_spool_add(p_gg_queue_master[lchip]->p_resrc_profile_pool[SYS_RESRC_PROFILE_FC], p_sys_fc_profile_new, NULL, &p_sys_fc_profile_get);
                if (ret < 0)
                {
                    CTC_WB_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "ctc_spool_add error! ret: %d.\n", ret);
                    mem_free(p_sys_fc_profile_new);
                    goto done;
                }
                else
                {
                    if (ret == CTC_SPOOL_E_OPERATE_MEMORY)
                    {
                        sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
                        opf.pool_type = OPF_QUEUE_DROP_PROFILE;
                        opf.pool_index = SYS_RESRC_PROFILE_FC;

                        /*alloc index from position*/
                        ret = sys_goldengate_opf_alloc_offset_from_position(lchip, &opf, 1, profile_id);
                        if (ret)
                        {
                            ctc_spool_remove(p_gg_queue_master[lchip]->p_resrc_profile_pool[SYS_RESRC_PROFILE_FC], p_sys_fc_profile_get, NULL);
                            mem_free(p_sys_fc_profile_new);

                            CTC_WB_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "alloc IgrPortThrdProfile table offset from position %u error! ret: %d.\n", profile_id, ret);

                            goto done;
                        }
                    }
                    else /* means get an old. */
                    {
                        mem_free(p_sys_fc_profile_new);
                        p_sys_fc_profile_new = NULL;
                    }
                }

                p_sys_fc_profile_get->head.profile_id = profile_id;
                p_gg_queue_master[lchip]->p_fc_profile[chan_id] = p_sys_fc_profile_get;
            }
        }
    }


    /*restore  cpu reason*/
    CTC_WB_INIT_QUERY_T((&wb_query),sys_wb_qos_cpu_reason_t, CTC_FEATURE_QOS, SYS_WB_APPID_QOS_SUBID_CPU_REASON);

    CTC_WB_QUERY_ENTRY_BEGIN((&wb_query));
        p_wb_cpu_reason = (sys_wb_qos_cpu_reason_t*)wb_query.buffer + entry_cnt++;

        p_gg_queue_master[lchip]->cpu_reason[p_wb_cpu_reason->reason_id].dest_type = p_wb_cpu_reason->cpu_reason.dest_type;
        p_gg_queue_master[lchip]->cpu_reason[p_wb_cpu_reason->reason_id].is_user_define = p_wb_cpu_reason->cpu_reason.is_user_define;
        p_gg_queue_master[lchip]->cpu_reason[p_wb_cpu_reason->reason_id].dsfwd_offset = p_wb_cpu_reason->cpu_reason.dsfwd_offset;
        p_gg_queue_master[lchip]->cpu_reason[p_wb_cpu_reason->reason_id].dest_port = p_wb_cpu_reason->cpu_reason.dest_port;
        p_gg_queue_master[lchip]->cpu_reason[p_wb_cpu_reason->reason_id].sub_queue_id = p_wb_cpu_reason->cpu_reason.sub_queue_id;
        p_gg_queue_master[lchip]->cpu_reason[p_wb_cpu_reason->reason_id].dest_map = p_wb_cpu_reason->cpu_reason.dest_map;
    CTC_WB_QUERY_ENTRY_END((&wb_query));



done:


    if (wb_query.buffer)
    {
        mem_free(wb_query.buffer);
    }


    return ret;
}




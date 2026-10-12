/**
 @file sys_greatbelt_aclqos_policer.c

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

#include "sys_greatbelt_chip.h"
#include "sys_greatbelt_port.h"
#include "sys_greatbelt_qos.h"

#include "sys_greatbelt_qos_class.h"
#include "sys_greatbelt_qos_policer.h"
#include "sys_greatbelt_queue_enq.h"
#include "sys_greatbelt_queue_shape.h"
#include "sys_greatbelt_queue_sch.h"
#include "sys_greatbelt_queue_drop.h"

#include "greatbelt/include/drv_io.h"
#include "greatbelt/include/drv_lib.h"

/****************************************************************************
  *
  * Defines and Macros
  *
  ****************************************************************************/

/*init*/
extern int32
sys_greatbelt_qos_init(uint8 lchip, void* p_glb_parm)
{
    int32 ret = CTC_E_NONE;
    ctc_qos_global_cfg_t* glb_parm = NULL;
    ctc_qos_resrc_pool_cfg_t* p_resrc_pool_0 = NULL;

    LCHIP_CHECK(lchip);

    glb_parm = (ctc_qos_global_cfg_t*)mem_malloc(MEM_QUEUE_MODULE, sizeof(ctc_qos_global_cfg_t));
    p_resrc_pool_0 = (ctc_qos_resrc_pool_cfg_t*)mem_malloc(MEM_QUEUE_MODULE, sizeof(ctc_qos_resrc_pool_cfg_t));
    if ((NULL == glb_parm) || (NULL == p_resrc_pool_0))
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(glb_parm, 0, sizeof(ctc_qos_global_cfg_t));
    sal_memset(p_resrc_pool_0, 0, sizeof(ctc_qos_resrc_pool_cfg_t));

    if (NULL == p_glb_parm
        || !sal_memcmp(&(((ctc_qos_global_cfg_t *)p_glb_parm)->resrc_pool), p_resrc_pool_0, sizeof(ctc_qos_resrc_pool_cfg_t)))
    {
        ctc_qos_resrc_drop_profile_t *p_profile = NULL;
        
        if (NULL == p_glb_parm)
        {   
            glb_parm->queue_num_per_network_port     = 8;
            glb_parm->queue_num_per_internal_port    = 8;
            glb_parm->queue_num_per_cpu_reason_group = 8;
            glb_parm->queue_num_per_ingress_service  = 4;
            glb_parm->queue_aging_time               = 0;

        }
        else
        {
            sal_memcpy(glb_parm, (ctc_qos_global_cfg_t*)p_glb_parm, sizeof(ctc_qos_global_cfg_t));  
            glb_parm->queue_num_per_network_port     = 8;
            glb_parm->queue_num_per_internal_port    = 8;
            glb_parm->queue_num_per_cpu_reason_group = 8;
        }
                       
        glb_parm->resrc_pool.igs_pool_mode = CTC_QOS_RESRC_POOL_DISABLE;
        glb_parm->resrc_pool.egs_pool_mode = CTC_QOS_RESRC_POOL_USER_DEFINE;
        glb_parm->resrc_pool.egs_pool_size[CTC_QOS_EGS_RESRC_DEFAULT_POOL]  = 11008;
        glb_parm->resrc_pool.egs_pool_size[CTC_QOS_EGS_RESRC_NON_DROP_POOL] = 0;
        glb_parm->resrc_pool.egs_pool_size[CTC_QOS_EGS_RESRC_SPAN_POOL]     = 0;
        glb_parm->resrc_pool.egs_pool_size[CTC_QOS_EGS_RESRC_CONTROL_POOL]  = 512;

        p_profile = &glb_parm->resrc_pool.drop_profile[CTC_QOS_EGS_RESRC_DEFAULT_POOL];
        p_profile->congest_level_num = 8;
        p_profile->congest_threshold[0] = 9408;
        p_profile->congest_threshold[1] = 9664;
        p_profile->congest_threshold[2] = 9920;
        p_profile->congest_threshold[3] = 10176;
        p_profile->congest_threshold[4] = 10432;
        p_profile->congest_threshold[5] = 10688;
        p_profile->congest_threshold[6] = 10944;
        p_profile->queue_drop[0].max_th[0] = 128;
        p_profile->queue_drop[0].max_th[1] = 256;
        p_profile->queue_drop[0].max_th[2] = 512;
        p_profile->queue_drop[0].max_th[3] = 512;
        p_profile->queue_drop[1].max_th[0] = 128;
        p_profile->queue_drop[1].max_th[1] = 256;
        p_profile->queue_drop[1].max_th[2] = 512;
        p_profile->queue_drop[1].max_th[3] = 512;
        p_profile->queue_drop[2].max_th[0] = 128;
        p_profile->queue_drop[2].max_th[1] = 256;
        p_profile->queue_drop[2].max_th[2] = 512;
        p_profile->queue_drop[2].max_th[3] = 512;
        p_profile->queue_drop[3].max_th[0] = 128;
        p_profile->queue_drop[3].max_th[1] = 256;
        p_profile->queue_drop[3].max_th[2] = 512;
        p_profile->queue_drop[3].max_th[3] = 512;
        p_profile->queue_drop[4].max_th[0] = 128;
        p_profile->queue_drop[4].max_th[1] = 256;
        p_profile->queue_drop[4].max_th[2] = 512;
        p_profile->queue_drop[4].max_th[3] = 512;
        p_profile->queue_drop[5].max_th[0] = 128;
        p_profile->queue_drop[5].max_th[1] = 256;
        p_profile->queue_drop[5].max_th[2] = 512;
        p_profile->queue_drop[5].max_th[3] = 512;
        p_profile->queue_drop[6].max_th[0] = 128;
        p_profile->queue_drop[6].max_th[1] = 256;
        p_profile->queue_drop[6].max_th[2] = 512;
        p_profile->queue_drop[6].max_th[3] = 512;
        p_profile->queue_drop[7].max_th[0] = 128;
        p_profile->queue_drop[7].max_th[1] = 256;
        p_profile->queue_drop[7].max_th[2] = 512;
        p_profile->queue_drop[7].max_th[3] = 512;

        p_profile = &glb_parm->resrc_pool.drop_profile[CTC_QOS_EGS_RESRC_CONTROL_POOL];
        p_profile->congest_level_num = 1;
        p_profile->queue_drop[0].max_th[0] = 64;
        p_profile->queue_drop[0].max_th[1] = 64;
        p_profile->queue_drop[0].max_th[2] = 64;
        p_profile->queue_drop[0].max_th[3] = 64;
        p_glb_parm = glb_parm;
        
    }
    else
    {
        sal_memcpy(glb_parm, (ctc_qos_global_cfg_t*)p_glb_parm, sizeof(ctc_qos_global_cfg_t));
        glb_parm->queue_num_per_network_port     = 8;
        glb_parm->queue_num_per_internal_port    = 8;
        glb_parm->queue_num_per_cpu_reason_group = 8;
        p_glb_parm = glb_parm;
    }
   
    CTC_ERROR_GOTO(sys_greatbelt_qos_policer_init(lchip), ret, error);

    CTC_ERROR_GOTO(sys_greatbelt_queue_enq_init(lchip, p_glb_parm), ret, error);

    CTC_ERROR_GOTO(sys_greatbelt_qos_class_init(lchip), ret, error);

    CTC_ERROR_GOTO(sys_greatbelt_queue_shape_init(lchip, p_glb_parm), ret, error);

    CTC_ERROR_GOTO(sys_greatbelt_queue_sch_init(lchip), ret, error);

    CTC_ERROR_GOTO(sys_greatbelt_queue_drop_init(lchip, p_glb_parm), ret, error);
error:
    if (glb_parm)
    {
        mem_free(glb_parm);
    }
    if (p_resrc_pool_0)
    {
        mem_free(p_resrc_pool_0);
    }
    return ret;
}

int32
sys_greatbelt_qos_deinit(uint8 lchip)
{
    LCHIP_CHECK(lchip);
    CTC_ERROR_RETURN(sys_greatbelt_qos_policer_deinit(lchip));

    CTC_ERROR_RETURN(sys_greatbelt_queue_deinit(lchip));

    return CTC_E_NONE;
}

/*policer*/
extern int32
sys_greatbelt_qos_set_policer(uint8 lchip, ctc_qos_policer_t* p_policer)
{
    LCHIP_CHECK(lchip);
    CTC_ERROR_RETURN(sys_greatbelt_qos_policer_set(lchip, p_policer));
    return CTC_E_NONE;
}

extern int32
sys_greatbelt_qos_get_policer(uint8 lchip, ctc_qos_policer_t* p_policer)
{
    LCHIP_CHECK(lchip);
    CTC_ERROR_RETURN(sys_greatbelt_qos_policer_get(lchip, p_policer));
    return CTC_E_NONE;
}

/*shape*/
extern int32
sys_greatbelt_qos_set_shape(uint8 lchip, ctc_qos_shape_t* p_shape)
{
    LCHIP_CHECK(lchip);
    CTC_ERROR_RETURN(_sys_greatbelt_qos_set_shape(lchip, p_shape));
    return CTC_E_NONE;
}

extern int32
sys_greatbelt_qos_get_shape(uint8 lchip, ctc_qos_shape_t* p_shape)
{
    LCHIP_CHECK(lchip);
    CTC_ERROR_RETURN(_sys_greatbelt_qos_get_shape(lchip, p_shape));
    return CTC_E_NONE;
}

/*schedule*/
extern int32
sys_greatbelt_qos_set_sched(uint8 lchip, ctc_qos_sched_t* p_sched)
{
    LCHIP_CHECK(lchip);
    CTC_ERROR_RETURN(_sys_greatbelt_qos_set_sched(lchip, p_sched));
    return CTC_E_NONE;
}

extern int32
sys_greatbelt_qos_get_sched(uint8 lchip, ctc_qos_sched_t* p_sched)
{
    LCHIP_CHECK(lchip);
    CTC_ERROR_RETURN(_sys_greatbelt_qos_get_sched(lchip, p_sched));
    return CTC_E_NONE;
}

/*mapping*/
extern int32
sys_greatbelt_qos_set_domain_map(uint8 lchip, ctc_qos_domain_map_t* p_domain_map)
{
    LCHIP_CHECK(lchip);
    CTC_ERROR_RETURN(sys_greatbelt_qos_domain_map_set(lchip, p_domain_map));
    return CTC_E_NONE;
}

extern int32
sys_greatbelt_qos_get_domain_map(uint8 lchip, ctc_qos_domain_map_t* p_domain_map)
{
    LCHIP_CHECK(lchip);
    CTC_ERROR_RETURN(sys_greatbelt_qos_domain_map_get(lchip, p_domain_map));
    return CTC_E_NONE;
}

/*global param*/
extern int32
sys_greatbelt_qos_set_global_config(uint8 lchip, ctc_qos_glb_cfg_t* p_glb_cfg)
{
    LCHIP_CHECK(lchip);
    switch (p_glb_cfg->cfg_type)
    {
    case CTC_QOS_GLB_CFG_POLICER_EN:
        CTC_ERROR_RETURN(
            sys_greatbelt_qos_set_policer_update_enable(lchip, p_glb_cfg->u.value));
        break;

    case CTC_QOS_GLB_CFG_POLICER_STATS_EN:
        CTC_ERROR_RETURN(
            sys_greatbelt_qos_set_policer_stats_enable(lchip, p_glb_cfg->u.value));
        break;

    case  CTC_QOS_GLB_CFG_POLICER_IPG_EN:
        CTC_ERROR_RETURN(
            sys_greatbelt_qos_set_policer_ipg_enable(lchip, p_glb_cfg->u.value));
        break;

    case CTC_QOS_GLB_CFG_POLICER_SEQENCE_EN:
        CTC_ERROR_RETURN(
            sys_greatbelt_qos_set_policer_sequential_enable(lchip, p_glb_cfg->u.value));
        break;

    case  CTC_QOS_GLB_CFG_QUE_SHAPE_EN:
        CTC_ERROR_RETURN(
            sys_greatbelt_qos_set_queue_shape_enable(lchip, p_glb_cfg->u.value));
        break;

    case  CTC_QOS_GLB_CFG_GROUP_SHAPE_EN:
        CTC_ERROR_RETURN(
            sys_greatbelt_qos_set_group_shape_enable(lchip, p_glb_cfg->u.value));
        break;

    case  CTC_QOS_GLB_CFG_PORT_SHAPE_EN:
        CTC_ERROR_RETURN(
            sys_greatbelt_qos_set_port_shape_enable(lchip, p_glb_cfg->u.value));
        break;

    case CTC_QOS_GLB_CFG_SHAPE_IPG_EN:
        CTC_ERROR_RETURN(
            sys_greatbelt_qos_set_shape_ipg_enable(lchip, p_glb_cfg->u.value));
        break;

    case CTC_QOS_GLB_CFG_POLICER_FLOW_FIRST_EN:
        CTC_ERROR_RETURN(
            sys_greatbelt_qos_set_policer_flow_first(lchip, (p_glb_cfg->u.value >> 16) & 0xFFFF, (p_glb_cfg->u.value & 0xFFFF)));
        break;

    case CTC_QOS_GLB_CFG_RESRC_MGR_EN:
        CTC_ERROR_RETURN(
            sys_greatbelt_qos_resrc_mgr_en(lchip, p_glb_cfg->u.value));
        break;

    case CTC_QOS_GLB_CFG_QUE_STATS_EN:
        return CTC_E_NOT_SUPPORT;
        break;


    case CTC_QOS_GLB_CFG_PHB_MAP:
        CTC_ERROR_RETURN(
            sys_greatbelt_qos_set_phb(lchip, &p_glb_cfg->u.phb_map));
        break;

    case CTC_QOS_GLB_CFG_REASON_SHAPE_PKT_EN:
        CTC_ERROR_RETURN(
            sys_greatbelt_qos_set_reason_shp_base_pkt_en(lchip, p_glb_cfg->u.value));
        break;

    case CTC_QOS_GLB_CFG_POLICER_HBWP_SHARE_EN:
        CTC_ERROR_RETURN(
            sys_greatbelt_qos_set_policer_hbwp_share_enable(lchip, p_glb_cfg->u.value));
        break;


    default:
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

extern int32
sys_greatbelt_qos_get_global_config(uint8 lchip, ctc_qos_glb_cfg_t* p_glb_cfg)
{
    LCHIP_CHECK(lchip);
    return CTC_E_NONE;
}

/*queue*/
extern int32
sys_greatbelt_qos_set_queue(uint8 lchip, ctc_qos_queue_cfg_t* p_que_cfg)
{
    LCHIP_CHECK(lchip);
    CTC_ERROR_RETURN(sys_greatbelt_qos_queue_set(lchip, p_que_cfg));
    return CTC_E_NONE;
}

extern int32
sys_greatbelt_qos_get_queue(uint8 lchip, ctc_qos_queue_cfg_t* p_que_cfg)
{
    LCHIP_CHECK(lchip);
    CTC_ERROR_RETURN(sys_greatbelt_qos_queue_get(lchip, p_que_cfg));
    return CTC_E_NONE;
}

/*drop*/
extern int32
sys_greatbelt_qos_set_drop_scheme(uint8 lchip, ctc_qos_drop_t* p_drop)
{
    LCHIP_CHECK(lchip);
    CTC_ERROR_RETURN(sys_greatbelt_queue_set_drop(lchip, p_drop));
    return CTC_E_NONE;
}

extern int32
sys_greatbelt_qos_get_drop_scheme(uint8 lchip, ctc_qos_drop_t* p_drop)
{
    LCHIP_CHECK(lchip);
    CTC_ERROR_RETURN(sys_greatbelt_queue_get_drop(lchip, p_drop));
    return CTC_E_NONE;
}

extern int32
sys_greatbelt_qos_set_resrc(uint8 lchip, ctc_qos_resrc_t* p_resrc)
{
    LCHIP_CHECK(lchip);
    CTC_ERROR_RETURN(sys_greatbelt_queue_set_resrc(lchip, p_resrc));
    return CTC_E_NONE;
}

extern int32
sys_greatbelt_qos_get_resrc(uint8 lchip, ctc_qos_resrc_t* p_resrc)
{
    LCHIP_CHECK(lchip);
    CTC_ERROR_RETURN(sys_greatbelt_queue_get_resrc(lchip, p_resrc));
    return CTC_E_NONE;
}

extern int32
sys_greatbelt_qos_query_pool_stats(uint8 lchip, ctc_qos_resrc_pool_stats_t* p_stats)
{
    LCHIP_CHECK(lchip);
    CTC_ERROR_RETURN(sys_greatbelt_queue_query_pool_stats(lchip, p_stats));
    return CTC_E_NONE;
}

/*stats*/
extern int32
sys_greatbelt_qos_query_queue_stats(uint8 lchip, ctc_qos_queue_stats_t* p_queue_stats)
{
    LCHIP_CHECK(lchip);
    CTC_ERROR_RETURN(sys_greatbelt_queue_stats_query(lchip, p_queue_stats));
    return CTC_E_NONE;
}

/*stats*/
extern int32
sys_greatbelt_qos_clear_queue_stats(uint8 lchip, ctc_qos_queue_stats_t* p_queue_stats)
{
    LCHIP_CHECK(lchip);
    CTC_ERROR_RETURN(sys_greatbelt_queue_stats_clear(lchip, p_queue_stats));
    return CTC_E_NONE;
}

extern int32
sys_greatbelt_qos_query_policer_stats(uint8 lchip, ctc_qos_policer_stats_t* p_policer_stats)
{
    LCHIP_CHECK(lchip);
    CTC_ERROR_RETURN(sys_greatbelt_qos_policer_stats_query(lchip, p_policer_stats));
    return CTC_E_NONE;
}

extern int32
sys_greatbelt_qos_clear_policer_stats(uint8 lchip, ctc_qos_policer_stats_t* p_policer_stats)
{
    LCHIP_CHECK(lchip);
    CTC_ERROR_RETURN(sys_greatbelt_qos_policer_stats_clear(lchip, p_policer_stats));
    return CTC_E_NONE;
}


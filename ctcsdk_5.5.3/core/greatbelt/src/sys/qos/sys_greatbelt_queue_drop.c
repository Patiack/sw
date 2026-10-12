/**
 @file sys_greatbelt_queue_drop.c

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

#include "sys_greatbelt_chip.h"
#include "sys_greatbelt_opf.h"
#include "sys_greatbelt_queue_enq.h"
#include "sys_greatbelt_queue_drop.h"

#include "greatbelt/include/drv_lib.h"
#include "greatbelt/include/drv_io.h"

/****************************************************************************
 *
 * Global and Declaration
 *
 ****************************************************************************/
#define SYS_DROP_PROFILE_BLOCK_NUM 2
#define SYS_DROP_PROFILE_BLOCK_SIZE 4
#define SYS_MAX_DROP_PROFILE_NUM  8
#define SYS_MAX_DROP_PROFILE 15
#define SYS_MAX_FC_PROFILE_NUM  8
#define SYS_MAX_PFC_PROFILE_NUM  32

#define SYS_MAX_DROP_THRD (16 * 1024 - 1)
#define SYS_MAX_DROP_PROB (128 - 1)

#define SYS_RESRC_EGS_COND(que_min,total,sc,tc,port, grp,queue) \
    ((que_min<<6)|(total<<5)|(sc<<4)|(tc<<3)|(port<<2)|(grp<<1)|(queue<<0))

extern sys_queue_master_t* p_gb_queue_master[CTC_MAX_LOCAL_CHIP_NUM];

/****************************************************************************
 *
 * Function
 *
 ****************************************************************************/

STATIC uint32
_sys_greatbelt_queue_drop_profile_hash_make(sys_queue_drop_profile_t* p_prof)
{
    uint8* data= (uint8*)&p_prof->profile;
    uint8   length = sizeof(ds_que_thrd_profile_t);

    return ctc_hash_caculate(length, data);
}

/**
 @brief Queue drop hash comparison hook.
*/
STATIC bool
_sys_greatbelt_queue_drop_profile_hash_cmp(sys_queue_drop_profile_t* p_prof1,
                                           sys_queue_drop_profile_t* p_prof2)
{
    SYS_QUEUE_DBG_FUNC();

    if (!p_prof1 || !p_prof2)
    {
        return FALSE;
    }

    if (!sal_memcmp(&p_prof1->profile, &p_prof2->profile, sizeof(ds_que_thrd_profile_t)))
    {
        return TRUE;
    }

    return 0;
}

STATIC int32
_sys_greatbelt_qos_profile_alloc_offset(uint8 lchip, uint8 type,
                                               uint16* profile_id)
{
    sys_greatbelt_opf_t opf;
    uint32 offset  = 0;

    SYS_QUEUE_DBG_FUNC();

    sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));
    opf.pool_type = OPF_QUEUE_DROP_PROFILE;
    opf.pool_index = type;

    CTC_ERROR_RETURN(sys_greatbelt_opf_alloc_offset(lchip, &opf, 1, &offset));

    *profile_id = offset;

    SYS_QUEUE_DBG_INFO("alloc drop profile id = %d\n", offset);


    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_qos_profile_free_offset(uint8 lchip, uint8 type,
                                              uint16 profile_id)
{
    sys_greatbelt_opf_t opf;
    uint32 offset  = 0;

    SYS_QUEUE_DBG_FUNC();

    sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));
    opf.pool_type = OPF_QUEUE_DROP_PROFILE;
    opf.pool_index = type;

    offset = profile_id;

    CTC_ERROR_RETURN(sys_greatbelt_opf_free_offset(lchip, &opf, 1, offset));

    SYS_QUEUE_DBG_INFO("free drop profile id = %d\n", offset);

    return CTC_E_NONE;
}

STATIC uint32
_sys_greatbelt_qos_fc_profile_hash_make(void* p_prof)
{
    sys_qos_profile_head_t *head = p_prof;
    uint8* data = (uint8*)(p_prof) + sizeof(sys_qos_profile_head_t);
    uint16   length = head->profile_len - sizeof(sys_qos_profile_head_t);

    return ctc_hash_caculate(length, data);

    return CTC_E_NONE;
}

STATIC bool
_sys_greatbelt_qos_fc_profile_hash_cmp(void* p_prof1,
                                           void* p_prof2)
{

    sys_qos_profile_head_t *head = p_prof1;
    uint16   length = 0;
    SYS_QUEUE_DBG_FUNC();

    if (!p_prof1 || !p_prof2)
    {
        return FALSE;
    }

    length = head->profile_len - sizeof(sys_qos_profile_head_t);

    if (!sal_memcmp((uint8*)(p_prof1) + sizeof(sys_qos_profile_head_t),
                    (uint8*)(p_prof2) + sizeof(sys_qos_profile_head_t),
                    length))
    {
        return TRUE;
    }

    return FALSE;
}

STATIC int32
_sys_greatbelt_qos_fc_profile_build_data(uint8 lchip, uint8 type,
                                        void* p_data,
                                        void* p_sys_profile)
{
    sys_qos_fc_profile_t *p_fc_data = NULL;
    sys_qos_fc_profile_t *p_fc_profile = NULL;


    p_fc_data = p_data;
    p_fc_profile = p_sys_profile;


    p_fc_profile->xon = p_fc_data->xon;
    p_fc_profile->xoff = p_fc_data->xoff;
    p_fc_profile->thrd = p_fc_data->thrd;

    return CTC_E_NONE;
}

int32
_sys_greatbelt_qos_fc_remove_profile(uint8 lchip, uint8 type,
                                         void* p_sys_profile_old)
{
    void* p_sys_profile_find = NULL;
    ctc_spool_t* p_profile_pool               = NULL;
    int32 ret            = 0;
    uint16 profile_id    = 0;

    SYS_QUEUE_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_sys_profile_old);

    p_profile_pool = p_gb_queue_master[lchip]->p_resrc_profile_pool[type];

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
        profile_id = ((sys_qos_profile_head_t *)p_sys_profile_find)->profile_id;
        /*free ad index*/
        CTC_ERROR_RETURN(_sys_greatbelt_qos_profile_free_offset(lchip, type,profile_id));
        mem_free(p_sys_profile_find);
        p_sys_profile_old = NULL;
    }

    return CTC_E_NONE;
}

int32
_sys_greatbelt_qos_fc_add_profile(uint8 lchip, uint8 type,
                                      void* p_data,
                                      void* p_sys_profile_old,
                                      void** pp_sys_profile_get)
{
    ctc_spool_t* p_profile_pool    = NULL;
    void* p_sys_profile_new = NULL;
    int32 ret            = 0;
    uint16 old_profile_id  =0;
    uint16 new_profile_id    = 0;

    SYS_QUEUE_DBG_FUNC();

    p_profile_pool = p_gb_queue_master[lchip]->p_resrc_profile_pool[type];

    p_sys_profile_new = mem_malloc(MEM_QUEUE_MODULE, p_profile_pool->user_data_size);
    if (NULL == p_sys_profile_new)
    {
        return CTC_E_NO_MEMORY;
    }
    sal_memset(p_sys_profile_new, 0, p_profile_pool->user_data_size);
    ((sys_qos_profile_head_t *)p_sys_profile_new)->profile_len = p_profile_pool->user_data_size;

    _sys_greatbelt_qos_fc_profile_build_data(lchip, type, p_data, p_sys_profile_new);
    /*if use new date to replace old data, profile_id will not change*/
    if(p_sys_profile_old)
    {
        if(TRUE == _sys_greatbelt_qos_fc_profile_hash_cmp(p_sys_profile_old, p_sys_profile_new))
        {
            *pp_sys_profile_get = p_sys_profile_old;
            mem_free(p_sys_profile_new);
            return CTC_E_NONE;
        }
        old_profile_id = ((sys_qos_profile_head_t *)p_sys_profile_old)->profile_id;
        ((sys_qos_profile_head_t *)p_sys_profile_new)->profile_id = old_profile_id;
    }

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
            ret = _sys_greatbelt_qos_profile_alloc_offset(lchip, type, &new_profile_id);
            if (ret < 0)
            {
                ctc_spool_remove(p_profile_pool, p_sys_profile_new, NULL);
                mem_free(p_sys_profile_new);
                goto ERROR_RETURN;
            }
            (*(sys_qos_profile_head_t **)pp_sys_profile_get)->profile_id = new_profile_id;
        }
    }
    SYS_QUEUE_DBG_INFO("use profile id = %d\n",
                       (*(sys_qos_profile_head_t **)pp_sys_profile_get)->profile_id);

    /*key is found, so there is an old ad need to be deleted.*/
    /*if profile_id not change, no need to delete old ad(old ad is replaced by new)*/
    if (p_sys_profile_old  && (*(sys_qos_profile_head_t **)pp_sys_profile_get)->profile_id != old_profile_id)
    {
        CTC_ERROR_RETURN(_sys_greatbelt_qos_fc_remove_profile(lchip, type, p_sys_profile_old));
    }

    return CTC_E_NONE;

ERROR_RETURN:
    return ret;
}




int32
_sys_greatbelt_qos_add_static_profile(uint8 lchip, uint8 type,
                                               void* p_data,
                                               uint16 profile_id,
                                               void** pp_sys_profile_get)
{
    ctc_spool_t* p_profile_pool    = NULL;
    void* p_sys_profile_new = NULL;
    int32 ret            = 0;

    SYS_QUEUE_DBG_FUNC();

    p_profile_pool = p_gb_queue_master[lchip]->p_resrc_profile_pool[type];

    p_sys_profile_new = mem_malloc(MEM_QUEUE_MODULE, p_profile_pool->user_data_size);
    if (NULL == p_sys_profile_new)
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_sys_profile_new, 0, p_profile_pool->user_data_size);

    ((sys_qos_profile_head_t *)p_sys_profile_new)->profile_id = profile_id;
    ((sys_qos_profile_head_t *)p_sys_profile_new)->profile_len = p_profile_pool->user_data_size;

    _sys_greatbelt_qos_fc_profile_build_data(lchip, type, p_data, p_sys_profile_new);

    ret = ctc_spool_static_add(p_profile_pool,  p_sys_profile_new);
    if (ret != CTC_SPOOL_E_OPERATE_MEMORY)
    {
        mem_free(p_sys_profile_new);
        return ret;
    }

    *(sys_qos_profile_head_t **)pp_sys_profile_get = p_sys_profile_new;

    return ret;
}


int32
_sys_greatbelt_qos_fc_add_static_profile(uint8 lchip)
{
    sys_qos_fc_profile_t fc_data;
    sys_qos_fc_profile_t *p_sys_fc_profile_new = NULL;
    uint8 type = 0;
    uint8 profile = 0;
    uint16 index = 0;
    uint32 cmd = 0;
    ds_igr_port_tc_thrd_profile_t ds_igr_port_tc_thrd_profile;
    ds_igr_port_thrd_profile_t ds_igr_port_thrd_profile;
    uint16 chan_id = 0;
    uint16 field_id = 0;
    uint32 field_val = 0;
    uint8 tc = 0;

    uint32 tc_thrd[2][3] =
    {
        {7488, 52, 36},
        {7488, 0x3FFF, 0x3FFF}
    };

    uint32 port_thrd[2][3] =
    {
        {10576, 52, 36},
        {10576, 0x3FFF, 0x3FFF}
    };

    sal_memset(&fc_data, 0, sizeof(sys_qos_fc_profile_t));
    sal_memset(&ds_igr_port_tc_thrd_profile, 0, sizeof(ds_igr_port_tc_thrd_profile_t));
    sal_memset(&ds_igr_port_thrd_profile, 0, sizeof(ds_igr_port_thrd_profile_t));

    for (profile = 0; profile < 2; profile++)   /* here has 2 profile */
    {
        index = (profile << 3 | 0);
        cmd = DRV_IOR(DsIgrPortTcThrdProfile_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_igr_port_tc_thrd_profile));
        ds_igr_port_tc_thrd_profile.port_tc_thrd      = tc_thrd[profile][0] / 16;
        ds_igr_port_tc_thrd_profile.port_tc_xoff_thrd = tc_thrd[profile][1];
        ds_igr_port_tc_thrd_profile.port_tc_xon_thrd  = tc_thrd[profile][2];
        cmd = DRV_IOW(DsIgrPortTcThrdProfile_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_igr_port_tc_thrd_profile));

        fc_data.thrd =  tc_thrd[profile][0];
        fc_data.xoff = tc_thrd[profile][1];
        fc_data.xon = tc_thrd[profile][2];
        type = SYS_RESRC_PROFILE_PFC;
        CTC_ERROR_RETURN(_sys_greatbelt_qos_add_static_profile(lchip, type, &fc_data,
                                                               profile, (void**)&p_sys_fc_profile_new));

        if (profile == 0)
        {
            /*configure portTcThrdProfile ID*/
            for (chan_id = 0; chan_id < SYS_MAX_CHANNEL_NUM; chan_id++)
            {
                for (tc = 0; tc < 8; tc++)
                {
                    field_val = 0;
                    field_id = DsIgrPortTcThrdProfId_ProfIdHigh0_f + (chan_id << 3 | tc) % 4;
                    index = (chan_id << 3 | tc) / 4;
                    cmd = DRV_IOW(DsIgrPortTcThrdProfId_t, field_id);
                    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &field_val));
                    p_gb_queue_master[lchip]->p_pfc_profile[chan_id][tc] = p_sys_fc_profile_new;
                }
            }
        }

    }

    for (profile = 0; profile < 2; profile++)
    {
        index = profile << 3 | 0;
        cmd = DRV_IOR(DsIgrPortThrdProfile_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_igr_port_thrd_profile));
        ds_igr_port_thrd_profile.port_thrd      = port_thrd[profile][0] / 16;
        ds_igr_port_thrd_profile.port_xoff_thrd = port_thrd[profile][1];
        ds_igr_port_thrd_profile.port_xon_thrd  = port_thrd[profile][2];
        cmd = DRV_IOW(DsIgrPortThrdProfile_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_igr_port_thrd_profile));

        fc_data.thrd = port_thrd[profile][0];
        fc_data.xoff = port_thrd[profile][1];
        fc_data.xon = port_thrd[profile][2];
        type = SYS_RESRC_PROFILE_FC;
        CTC_ERROR_RETURN(_sys_greatbelt_qos_add_static_profile(lchip, type, &fc_data,
                                                               profile, (void**)&p_sys_fc_profile_new));

        if (profile == 0)
        {
            /* port threshold profile ID */
            for (chan_id = 0; chan_id < SYS_MAX_CHANNEL_NUM; chan_id++)
            {
                field_val = 0;
                field_id = DsIgrPortThrdProfId_ProfIdHigh0_f + (chan_id) % 8;
                index = (chan_id) / 8;
                cmd = DRV_IOW(DsIgrPortThrdProfId_t, field_id);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &field_val));
                p_gb_queue_master[lchip]->p_fc_profile[chan_id] = p_sys_fc_profile_new;
            }
        }

    }

    return CTC_E_NONE;
}


STATIC int32
_sys_greatbelt_queue_drop_profile_build_data(uint8 lchip, ctc_qos_drop_array drop_arraw,
                                             sys_queue_node_t* p_sys_queue, sys_queue_drop_profile_t* p_sys_profile_new)
{
    uint32 cmd = 0;
    uint32 sc = 0;
    uint8  level_num = 0;
    uint8  cng_level = 0;
    ds_que_thrd_profile_t ds_que_thrd_profile;

    SYS_QUEUE_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_sys_profile_new);

    sal_memset(&ds_que_thrd_profile, 0, sizeof(ds_que_thrd_profile_t));

    /* get queue sc */
    cmd = DRV_IOR(DsEgrResrcCtl_t, DsEgrResrcCtl_MappedSc_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_sys_queue->queue_id, cmd, &sc));

    /* config db */
    level_num = p_gb_queue_master[lchip]->egs_pool[sc].egs_congest_level_num;
    if (level_num > 0)
    {
        for (cng_level = 0; cng_level < level_num; cng_level++)
        {
            sal_memset(&ds_que_thrd_profile, 0, sizeof(ds_que_thrd_profile_t));

            ds_que_thrd_profile.wred_max_thrd0 = drop_arraw[cng_level].drop.max_th[0];
            ds_que_thrd_profile.wred_max_thrd1 = drop_arraw[cng_level].drop.max_th[1];
            ds_que_thrd_profile.wred_max_thrd2 = drop_arraw[cng_level].drop.max_th[2];
            ds_que_thrd_profile.wred_max_thrd3 = drop_arraw[cng_level].drop.max_th[3];
            ds_que_thrd_profile.wred_min_thrd0 = drop_arraw[cng_level].drop.min_th[0];
            ds_que_thrd_profile.wred_min_thrd1 = drop_arraw[cng_level].drop.min_th[1];
            ds_que_thrd_profile.wred_min_thrd2 = drop_arraw[cng_level].drop.min_th[2];
            ds_que_thrd_profile.wred_min_thrd3 = drop_arraw[cng_level].drop.min_th[3];
            ds_que_thrd_profile.max_drop_prob0 = drop_arraw[cng_level].drop.drop_prob[0];
            ds_que_thrd_profile.max_drop_prob1 = drop_arraw[cng_level].drop.drop_prob[1];
            ds_que_thrd_profile.max_drop_prob2 = drop_arraw[cng_level].drop.drop_prob[2];
            ds_que_thrd_profile.max_drop_prob3 = drop_arraw[cng_level].drop.drop_prob[3];

            sal_memcpy(&(p_sys_profile_new->profile[cng_level]), &ds_que_thrd_profile, sizeof(ds_que_thrd_profile_t));
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_queue_drop_profile_write_asic(uint8 lchip,
                                             sys_queue_node_t* p_sys_queue,
                                             sys_queue_drop_profile_t* p_sys_profile)
{
    uint32 cmd = 0;
    uint8  cng_level = 0;
    uint8  profile_id = 0;
    uint32 sc = 0;
    uint8  level_num = 0;

    SYS_QUEUE_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_sys_profile);

    /* get queue sc */
    cmd = DRV_IOR(DsEgrResrcCtl_t, DsEgrResrcCtl_MappedSc_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_sys_queue->queue_id, cmd, &sc));

    cmd = DRV_IOW(DsQueThrdProfile_t, DRV_ENTRY_FLAG);

    level_num = p_gb_queue_master[lchip]->egs_pool[sc].egs_congest_level_num;
    if(level_num > 0)
    {
        for (cng_level = 0; cng_level < level_num; cng_level++)
        {
            profile_id = (p_sys_profile->profile_id << 3) + cng_level + (8-level_num);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip,
                                       profile_id,
                                       cmd,
                                       &p_sys_profile->profile[cng_level]));

            SYS_QUEUE_DBG_INFO("write drop profile id = %d\n", profile_id);
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_queue_drop_write_asic(uint8 lchip,
                                     uint8 mode,
                                     uint16 queue_id,
                                     uint16 profile_id)
{
    uint32 cmd = 0;
    ds_egr_resrc_ctl_t ds_egr_resrc_ctl;

    SYS_QUEUE_DBG_FUNC();

    sal_memset(&ds_egr_resrc_ctl, 0, sizeof(ds_egr_resrc_ctl_t));

    cmd = DRV_IOR(DsEgrResrcCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, queue_id, cmd, &ds_egr_resrc_ctl));
    ds_egr_resrc_ctl.wred_drop_mode = mode;
    ds_egr_resrc_ctl.que_min_prof_id = 1;
    ds_egr_resrc_ctl.que_thrd_prof_id_high = profile_id;
    cmd = DRV_IOW(DsEgrResrcCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, queue_id, cmd, &ds_egr_resrc_ctl));
    SYS_QUEUE_DBG_INFO("queue_id = %d\n", queue_id);
    SYS_QUEUE_DBG_INFO("wred_drop_mode = %d\n", ds_egr_resrc_ctl.wred_drop_mode);
    SYS_QUEUE_DBG_INFO("que_min_prof_id = %d\n", ds_egr_resrc_ctl.que_min_prof_id);
    SYS_QUEUE_DBG_INFO("que_thrd_prof_id_high = %d\n", ds_egr_resrc_ctl.que_thrd_prof_id_high);

    return CTC_E_NONE;

}

STATIC int32
_sys_greatbelt_queue_drop_profile_remove(uint8 lchip, uint8 type,
                                         sys_queue_drop_profile_t* p_sys_profile_old)
{
    sys_queue_drop_profile_t* p_sys_profile_find = NULL;
    ctc_spool_t* p_profile_pool               = NULL;
    int32 ret            = 0;
    uint16 profile_id    = 0;

    SYS_QUEUE_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_sys_profile_old);

    p_profile_pool = p_gb_queue_master[lchip]->p_drop_profile_pool;

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
        CTC_ERROR_RETURN(_sys_greatbelt_qos_profile_free_offset(lchip, type, profile_id));
        mem_free(p_sys_profile_find);
    }

    return CTC_E_NONE;
}


STATIC int32
_sys_greatbelt_queue_drop_profile_add(uint8 lchip,
                                      ctc_qos_drop_array drop_arraw,
                                      sys_queue_node_t* p_sys_queue,
                                      sys_queue_drop_profile_t** pp_sys_profile_get)
{
    ctc_spool_t* p_profile_pool    = NULL;
    sys_queue_drop_profile_t* p_sys_profile_old = NULL;
    sys_queue_drop_profile_t* p_sys_profile_new = NULL;
    int32 ret            = 0;
    uint16 profile_id    = 0;
    uint16 old_profile_id = 0;

    SYS_QUEUE_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_sys_queue);
    CTC_PTR_VALID_CHECK(pp_sys_profile_get);

    p_sys_profile_new = mem_malloc(MEM_QUEUE_MODULE, sizeof(sys_queue_drop_profile_t));
    if (NULL == p_sys_profile_new)
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_sys_profile_new, 0, sizeof(sys_queue_drop_profile_t));
    _sys_greatbelt_queue_drop_profile_build_data(lchip, drop_arraw, p_sys_queue, p_sys_profile_new);

    p_sys_profile_old = p_sys_queue->p_drop_profile;

    p_profile_pool = p_gb_queue_master[lchip]->p_drop_profile_pool;

    if (p_sys_profile_old)
    {
        if (!sal_memcmp(&p_sys_profile_old->profile,
                        &p_sys_profile_new->profile,
                        sizeof(ds_que_thrd_profile_t)))
        {
            *pp_sys_profile_get = p_sys_profile_old;
            mem_free(p_sys_profile_new);
            return CTC_E_NONE;
        }
        old_profile_id = p_sys_profile_old->profile_id;
        p_sys_profile_new->profile_id = old_profile_id;
    }

    ret = ctc_spool_add(p_profile_pool,
                        p_sys_profile_new,
                        p_sys_profile_old,
                        pp_sys_profile_get);

    if (ret == CTC_SPOOL_E_OPERATE_MEMORY)
    {
        ret = _sys_greatbelt_qos_profile_alloc_offset(lchip, SYS_RESRC_PROFILE_QUEUE, &profile_id);
        if (ret < 0)
        {
            ctc_spool_remove(p_profile_pool, p_sys_profile_new, NULL);
            mem_free(p_sys_profile_new);
            goto ERROR_RETURN;
        }

        (*pp_sys_profile_get)->profile_id = profile_id;
    }
    else
    {
        mem_free(p_sys_profile_new);

        if (ret < 0)
        {
            ret = CTC_E_SPOOL_ADD_UPDATE_FAILED;
            goto ERROR_RETURN;
        }
    }

    /*key is found, so there is an old ad need to be deleted.*/
    if (p_sys_profile_old && (*pp_sys_profile_get)->profile_id != old_profile_id)
    {
        CTC_ERROR_RETURN(_sys_greatbelt_queue_drop_profile_remove(lchip, SYS_RESRC_PROFILE_QUEUE, p_sys_profile_old));
    }

    p_sys_queue->p_drop_profile = *pp_sys_profile_get;


    return CTC_E_NONE;

ERROR_RETURN:
    return ret;

}


STATIC int32
_sys_greatbelt_queue_drop_add_static_profile(uint8 lchip, ctc_qos_global_cfg_t* p_cfg)
{
    sys_queue_drop_profile_t* p_sys_profile_new = NULL;
    ds_que_thrd_profile_t ds_que_thrd_profile;
    int32  ret = 0;
    uint8  sc = 0;
    uint8  level_num = 0;
    uint8  cng_level = 0;
    uint16 index = 0;
    uint32 cmd = 0;
    uint16 profile_index = 0;

    SYS_QUEUE_DBG_FUNC();

    for (sc = 0; sc < 4; sc ++)
    {
        level_num = p_gb_queue_master[lchip]->egs_pool[sc].egs_congest_level_num;
        profile_index = p_gb_queue_master[lchip]->egs_pool[sc].default_profile_id;

        if (level_num > 0)
        {
            p_sys_profile_new = mem_malloc(MEM_QUEUE_MODULE, sizeof(sys_queue_drop_profile_t));
            if (NULL == p_sys_profile_new)
            {
                return CTC_E_NO_MEMORY;
            }
            sal_memset(p_sys_profile_new, 0, sizeof(sys_queue_drop_profile_t));

            for (cng_level = 0; cng_level < level_num; cng_level++)
            {
                sal_memset(&ds_que_thrd_profile, 0, sizeof(ds_que_thrd_profile_t));

                ds_que_thrd_profile.wred_max_thrd0 = p_cfg->resrc_pool.drop_profile[sc].queue_drop[cng_level].max_th[0];
                ds_que_thrd_profile.wred_max_thrd1 = p_cfg->resrc_pool.drop_profile[sc].queue_drop[cng_level].max_th[1];
                ds_que_thrd_profile.wred_max_thrd2 = p_cfg->resrc_pool.drop_profile[sc].queue_drop[cng_level].max_th[2];
                ds_que_thrd_profile.wred_max_thrd3 = p_cfg->resrc_pool.drop_profile[sc].queue_drop[cng_level].max_th[3];
                ds_que_thrd_profile.wred_min_thrd0 = p_cfg->resrc_pool.drop_profile[sc].queue_drop[cng_level].min_th[0];
                ds_que_thrd_profile.wred_min_thrd1 = p_cfg->resrc_pool.drop_profile[sc].queue_drop[cng_level].min_th[1];
                ds_que_thrd_profile.wred_min_thrd2 = p_cfg->resrc_pool.drop_profile[sc].queue_drop[cng_level].min_th[2];
                ds_que_thrd_profile.wred_min_thrd3 = p_cfg->resrc_pool.drop_profile[sc].queue_drop[cng_level].min_th[3];

                cmd = DRV_IOW(DsQueThrdProfile_t, DRV_ENTRY_FLAG);
                index = (profile_index << 3) + cng_level + (8-level_num);
                CTC_ERROR_GOTO(DRV_IOCTL(lchip, index, cmd, &ds_que_thrd_profile), ret, error_1);

                p_sys_profile_new->profile_id = profile_index;
                sal_memcpy(&(p_sys_profile_new->profile[cng_level]), &ds_que_thrd_profile, sizeof(ds_que_thrd_profile_t));

            }

            ret = ctc_spool_static_add(p_gb_queue_master[lchip]->p_drop_profile_pool, p_sys_profile_new);
            if (ret != CTC_SPOOL_E_OPERATE_MEMORY)
            {
                goto error_1;
            }
        }
    }

    return CTC_E_NONE;

error_1:
    mem_free(p_sys_profile_new);
    return ret;
}
STATIC int32
_sys_greatbelt_queue_get_queue_drop(uint8 lchip, sys_queue_node_t* p_sys_queue_node, ctc_qos_drop_array p_drop, uint16 queue_id)
{
    uint8 profile_id = 0;
    uint32 cmd = 0;
    uint8 mode = 0;
    uint32 sc = 0;
    ds_que_thrd_profile_t profile;
    ds_egr_resrc_ctl_t ds_egr_resrc_ctl;
    uint8  level_num = 0;
    uint8 cng_level = 0;
    uint8 tmpprofile_id = 0;

    SYS_QUEUE_DBG_FUNC();

    sal_memset(&profile, 0, sizeof(ds_que_thrd_profile_t));
    sal_memset(&ds_egr_resrc_ctl, 0, sizeof(ds_egr_resrc_ctl_t));

    cmd = DRV_IOR(DsEgrResrcCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, queue_id, cmd, &ds_egr_resrc_ctl));
    mode = ds_egr_resrc_ctl.wred_drop_mode;
    sc = ds_egr_resrc_ctl.mapped_sc;

    if (p_sys_queue_node->p_drop_profile)
    {
        profile_id = (p_sys_queue_node->p_drop_profile->profile_id << 3);
    }
    else
    {
        profile_id = (p_gb_queue_master[lchip]->egs_pool[sc].default_profile_id << 3);
    }

    cmd = DRV_IOR(DsQueThrdProfile_t, DRV_ENTRY_FLAG);
    level_num = p_gb_queue_master[lchip]->egs_pool[sc].egs_congest_level_num;
    if(level_num > 0)
    {
        for (cng_level = 0; cng_level < 8; cng_level++)
        {
            tmpprofile_id = profile_id + cng_level + (8-level_num);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip,
                                       tmpprofile_id,
                                       cmd,
                                       &profile));
            p_drop[cng_level].drop.mode = mode;
            p_drop[cng_level].drop.max_th[0] = profile.wred_max_thrd0;
            p_drop[cng_level].drop.max_th[1] = profile.wred_max_thrd1;
            p_drop[cng_level].drop.max_th[2] = profile.wred_max_thrd2;
            p_drop[cng_level].drop.max_th[3] = profile.wred_max_thrd3;

            p_drop[cng_level].drop.min_th[0] = profile.wred_min_thrd0;
            p_drop[cng_level].drop.min_th[1] = profile.wred_min_thrd1;
            p_drop[cng_level].drop.min_th[2] = profile.wred_min_thrd2;
            p_drop[cng_level].drop.min_th[3] = profile.wred_min_thrd3;

            p_drop[cng_level].drop.drop_prob[0] = profile.max_drop_prob0;
            p_drop[cng_level].drop.drop_prob[1] = profile.max_drop_prob1;
            p_drop[cng_level].drop.drop_prob[2] = profile.max_drop_prob2;
            p_drop[cng_level].drop.drop_prob[3] = profile.max_drop_prob3;

        }
    }
    return CTC_E_NONE;
}
int32
sys_greatbelt_queue_set_queue_drop(uint8 lchip, sys_queue_node_t* p_sys_queue_node, ctc_qos_drop_array drop_array)
{
    sys_queue_drop_profile_t* p_sys_profile_new = NULL;
    int32 ret = 0;

    SYS_QUEUE_DBG_FUNC();

    /*add policer prof*/
    CTC_ERROR_RETURN(_sys_greatbelt_queue_drop_profile_add(lchip,
                                                drop_array,
                                                p_sys_queue_node,
                                                &p_sys_profile_new));

    /*write policer prof*/
    CTC_ERROR_RETURN(_sys_greatbelt_queue_drop_profile_write_asic(lchip,
                                                        p_sys_queue_node,
                                                        p_sys_profile_new));

    /*write policer ctl and count*/
    CTC_ERROR_RETURN(_sys_greatbelt_queue_drop_write_asic(lchip,
                                               drop_array[0].drop.mode,
                                               p_sys_queue_node->queue_id,
                                               p_sys_profile_new->profile_id));

    return ret;

}

int32
sys_greatbelt_queue_set_drop(uint8 lchip, ctc_qos_drop_t* p_drop)
{
    uint8  cng_level = 0;
    uint8  drop_prec = 0;
    uint16 queue_id = 0;
    sys_queue_node_t* p_sys_queue_node = NULL;
    ctc_qos_drop_array drop_array;
    ctc_qos_queue_drop_t *p_queue_drop = &p_drop->drop;

    SYS_QUEUE_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_drop);

    sal_memset(&drop_array, 0, sizeof(drop_array));

    /*get queue_id*/
    SYS_QOS_QUEUE_LOCK(lchip);
    CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(_sys_greatbelt_queue_get_queue_id(lchip,
                                                       &p_drop->queue,
                                                       &queue_id));
    p_sys_queue_node = ctc_vector_get(p_gb_queue_master[lchip]->queue_vec, queue_id);

    if (NULL == p_sys_queue_node)
    {   SYS_QOS_QUEUE_UNLOCK(lchip);
        return CTC_E_ENTRY_NOT_EXIST;
    }

    for (drop_prec = 0; drop_prec < CTC_DROP_PREC_NUM; drop_prec++)
    {
        if ((p_queue_drop->max_th[drop_prec] > SYS_MAX_DROP_THRD)
            || (p_queue_drop->min_th[drop_prec] > SYS_MAX_DROP_THRD)
            || (p_queue_drop->drop_prob[drop_prec] > SYS_MAX_DROP_PROB))
        {
            SYS_QOS_QUEUE_UNLOCK(lchip);
            return CTC_E_INVALID_PARAM;
        }
    }
    for (cng_level = 0; cng_level < CTC_RESRC_MAX_CONGEST_LEVEL_NUM; cng_level ++)
    {
        sal_memcpy(&drop_array[cng_level].drop, p_queue_drop, sizeof(ctc_qos_queue_drop_t));
    }

    CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(sys_greatbelt_queue_set_queue_drop(lchip, p_sys_queue_node, drop_array));
    SYS_QOS_QUEUE_UNLOCK(lchip);
    return CTC_E_NONE;
}

int32
sys_greatbelt_queue_get_drop(uint8 lchip, ctc_qos_drop_t* p_drop)
{

    uint16 queue_id = 0;
    sys_queue_node_t* p_sys_queue_node = NULL;
    ctc_qos_drop_array drop_array;

    SYS_QUEUE_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_drop);

    sal_memset(&drop_array, 0, sizeof(drop_array));

    /*get queue_id*/
    SYS_QOS_QUEUE_LOCK(lchip);
    CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(_sys_greatbelt_queue_get_queue_id(lchip,
                                                       &p_drop->queue,
                                                       &queue_id));

    p_sys_queue_node = ctc_vector_get(p_gb_queue_master[lchip]->queue_vec, queue_id);

    if (NULL == p_sys_queue_node)
    {
        SYS_QOS_QUEUE_UNLOCK(lchip);
        return CTC_E_ENTRY_NOT_EXIST;
    }
    CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(_sys_greatbelt_queue_get_queue_drop(lchip, p_sys_queue_node, drop_array, queue_id));
    sal_memcpy(&p_drop->drop, &drop_array[0].drop, sizeof(ctc_qos_queue_drop_t));

    SYS_QOS_QUEUE_UNLOCK(lchip);
    return CTC_E_NONE;
}

int32
sys_greatbelt_queue_set_queue_resrc(uint8 lchip,
                                      uint16 queue_id,
                                      uint8 pool)
{
    uint32 cmd;
    uint8 profile_id = 0;
    ds_egr_resrc_ctl_t ds_egr_resrc_ctl;

    profile_id = p_gb_queue_master[lchip]->egs_pool[pool].default_profile_id;
    sal_memset(&ds_egr_resrc_ctl, 0, sizeof(DsEgrResrcCtl_t));

    cmd = DRV_IOR(DsEgrResrcCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, queue_id, cmd, &ds_egr_resrc_ctl));
    ds_egr_resrc_ctl.mapped_sc = pool;
    ds_egr_resrc_ctl.mapped_tc = queue_id % 8;
    ds_egr_resrc_ctl.que_thrd_prof_id_high = profile_id;
    cmd = DRV_IOW(DsEgrResrcCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, queue_id, cmd, &ds_egr_resrc_ctl));

    return CTC_E_NONE;
}

int32
sys_greatbelt_queue_get_queue_resrc(uint8 lchip,
                                      uint16 queue_id,
                                      uint8 *pool)
{
    uint32 cmd = 0;
    ds_egr_resrc_ctl_t ds_egr_resrc_ctl;

    sal_memset(&ds_egr_resrc_ctl, 0, sizeof(ds_egr_resrc_ctl_t));

    cmd = DRV_IOR(DsEgrResrcCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, queue_id, cmd, &ds_egr_resrc_ctl));
    *pool = ds_egr_resrc_ctl.mapped_sc;

    return CTC_E_NONE;
}

int32
sys_greatbelt_queue_set_cpu_queue_egs_pool_classify(uint8 lchip, uint16 reason_id, uint8 pool)
{
    uint16 queue_id                = 0;
    uint8  queue_offset            = 0;
    uint8  queue_offset_num        = 0;
    ctc_qos_queue_id_t queue;

    SYS_QUEUE_DBG_FUNC();

    sal_memset(&queue, 0, sizeof(queue));

    if (p_gb_queue_master[lchip]->egs_pool[pool].egs_congest_level_num == 0)   /* the sc is not exist */
    {
        pool = CTC_QOS_EGS_RESRC_DEFAULT_POOL;
    }

    queue.cpu_reason = reason_id;
    queue.queue_type = CTC_QUEUE_TYPE_EXCP_CPU;
    _sys_greatbelt_queue_get_queue_id(lchip, &queue, &queue_id);

    queue_offset_num = 1;
    if (reason_id == CTC_PKT_CPU_REASON_C2C_PKT)
    {
        queue_offset_num = 16;
    }
    else if(reason_id == CTC_PKT_CPU_REASON_FWD_CPU)
    {
        queue_offset_num = 8;
    }

    for (queue_offset = 0; queue_offset < queue_offset_num; queue_offset++)
    {
        CTC_ERROR_RETURN(sys_greatbelt_queue_set_queue_resrc(lchip, (queue_id + queue_offset), pool));
    }

    return CTC_E_NONE;
}


int32
sys_greatbelt_qos_set_resrc_classify_pool(uint8 lchip, ctc_qos_resrc_classify_pool_t *p_pool)
{
    uint16 queue_id                 = 0;
    uint16 index                    = 0;
    uint32 cmd                      = 0;
    uint32 field_value              = 0;
    uint32 field_id                 = 0;
    uint16 chan_id                  = 0;
    uint8  color                    = 0;

    ds_igr_pri_to_tc_map_t ds_igr_pri_to_tc_map;
    ctc_qos_queue_id_t queue;

    sal_memset(&ds_igr_pri_to_tc_map, 0, sizeof(ds_igr_pri_to_tc_map));
    sal_memset(&queue, 0, sizeof(queue));
    CTC_GLOBAL_PORT_CHECK(p_pool->gport);
    CTC_MAX_VALUE_CHECK(p_pool->pool, 3);
    CTC_MAX_VALUE_CHECK(p_pool->priority, SYS_QOS_CLASS_PRIORITY_MAX);

    if (p_pool->dir == CTC_INGRESS)
    {
        chan_id = SYS_GET_CHANNEL_ID(lchip, p_pool->gport);
        CTC_MAX_VALUE_CHECK(chan_id, SYS_MAX_CHANNEL_NUM-1);

        sal_memset(&ds_igr_pri_to_tc_map, 0, sizeof(ds_igr_pri_to_tc_map_t));

        if (p_pool->pool == CTC_QOS_IGS_RESRC_NON_DROP_POOL)
        {
            for (color = 0; color < 4; color++)
            {
                index = p_pool->priority << 2 | color;
                cmd = DRV_IOR(DsIgrPriToTcMap_t, DRV_ENTRY_FLAG);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_igr_pri_to_tc_map));
                ds_igr_pri_to_tc_map.sc1 = p_pool->pool;
                cmd = DRV_IOW(DsIgrPriToTcMap_t, DRV_ENTRY_FLAG);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_igr_pri_to_tc_map));
            }

            field_value = 1;
        }
        else
        {
            field_value = 0;
        }

        field_id = DsIgrPortToTcProfId_ProfId0_f + chan_id % 8;
        index = chan_id / 8;
        cmd = DRV_IOW(DsIgrPortToTcProfId_t, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &field_value));


    }
    else if (p_pool->dir == CTC_EGRESS)
    {
        if (p_gb_queue_master[lchip]->egs_pool[p_pool->pool].egs_congest_level_num == 0)
        {
            return CTC_E_INVALID_PARAM;
        }

        queue.queue_type = CTC_QUEUE_TYPE_NETWORK_EGRESS;
        queue.gport = p_pool->gport;
        queue.queue_id = p_pool->priority/((p_gb_queue_master[lchip]->priority_mode)? 2:8);

        CTC_ERROR_RETURN(_sys_greatbelt_queue_get_queue_id(lchip, &queue, &queue_id));
        CTC_ERROR_RETURN(sys_greatbelt_queue_set_queue_resrc(lchip, queue_id,  p_pool->pool));
    }
    else
    {
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_qos_get_resrc_classify_pool(uint8 lchip, ctc_qos_resrc_classify_pool_t *p_pool)
{
    uint16 queue_id                 = 0;
    uint16 index                    = 0;
    uint32 cmd                      = 0;
    uint32 field_value              = 0;
    uint32 field_id                 = 0;
    uint16 chan_id                  = 0;
    ctc_qos_queue_id_t queue;

    sal_memset(&queue, 0, sizeof(queue));
    CTC_GLOBAL_PORT_CHECK(p_pool->gport);
    CTC_MAX_VALUE_CHECK(p_pool->pool, 3);
    CTC_MAX_VALUE_CHECK(p_pool->priority, SYS_QOS_CLASS_PRIORITY_MAX);

    if (p_pool->dir == CTC_INGRESS)
    {
        chan_id = SYS_GET_CHANNEL_ID(lchip, p_pool->gport);
        CTC_MAX_VALUE_CHECK(chan_id, SYS_MAX_CHANNEL_NUM-1);

        field_id = DsIgrPortToTcProfId_ProfId0_f + chan_id % 8;
        index = chan_id / 8;
        cmd = DRV_IOR(DsIgrPortToTcProfId_t, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &field_value));
        if(field_value)
        {
            p_pool->pool = CTC_QOS_IGS_RESRC_NON_DROP_POOL;
        }
        else
        {
            p_pool->pool = CTC_QOS_IGS_RESRC_DEFAULT_POOL;
        }
    }
    else if (p_pool->dir == CTC_EGRESS)
    {
        queue.queue_type = CTC_QUEUE_TYPE_NETWORK_EGRESS;
        queue.gport = p_pool->gport;
        queue.queue_id = p_pool->priority/((p_gb_queue_master[lchip]->priority_mode)? 2:8);

        CTC_ERROR_RETURN(_sys_greatbelt_queue_get_queue_id(lchip, &queue, &queue_id));
        CTC_ERROR_RETURN(sys_greatbelt_queue_get_queue_resrc(lchip, queue_id,  &p_pool->pool));
        if (p_gb_queue_master[lchip]->egs_pool[p_pool->pool].egs_congest_level_num == 0)
        {
            return CTC_E_INVALID_PARAM;
        }
    }
    else
    {
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_qos_set_resrc_queue_drop(uint8 lchip, ctc_qos_drop_array p_drop)
{
    uint8  drop_prec                = 0;
    uint8  cng_level                = 0;
    uint16 queue_id                 = 0;
    sys_queue_node_t* p_sys_queue_node = NULL;

    /* check para */
    for (cng_level = 0; cng_level < CTC_RESRC_MAX_CONGEST_LEVEL_NUM; cng_level++)
    {
        for (drop_prec = 0; drop_prec < CTC_DROP_PREC_NUM; drop_prec++)
        {
            CTC_MAX_VALUE_CHECK(p_drop[cng_level].drop.max_th[drop_prec], SYS_MAX_DROP_THRD);
            CTC_MAX_VALUE_CHECK(p_drop[cng_level].drop.min_th[drop_prec], SYS_MAX_DROP_THRD);
            CTC_MAX_VALUE_CHECK(p_drop[cng_level].drop.drop_prob[drop_prec], SYS_MAX_DROP_PROB);
        }
    }

    /* get queue_id */
    CTC_ERROR_RETURN(_sys_greatbelt_queue_get_queue_id(lchip, &(p_drop[0].queue), &queue_id));
    p_sys_queue_node = ctc_vector_get(p_gb_queue_master[lchip]->queue_vec, queue_id);
    if (NULL == p_sys_queue_node)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    CTC_ERROR_RETURN(sys_greatbelt_queue_set_queue_drop(lchip, p_sys_queue_node, p_drop));
    return CTC_E_NONE;
}

int32
sys_greatbelt_qos_get_resrc_queue_drop(uint8 lchip, ctc_qos_drop_array p_drop)
{
    uint16 queue_id                 = 0;
    sys_queue_node_t* p_sys_queue_node = NULL;

    /* get queue_id */
    CTC_ERROR_RETURN(_sys_greatbelt_queue_get_queue_id(lchip, &(p_drop[0].queue), &queue_id));
    p_sys_queue_node = ctc_vector_get(p_gb_queue_master[lchip]->queue_vec, queue_id);
    if (NULL == p_sys_queue_node)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }
    CTC_ERROR_RETURN(_sys_greatbelt_queue_get_queue_drop(lchip, p_sys_queue_node, p_drop, queue_id));

    return CTC_E_NONE;
}

int32
sys_greatbelt_qos_fc_add_profile(uint8 lchip, ctc_qos_resrc_fc_t *p_fc)
{
    sys_qos_fc_profile_t fc_data;
    uint8 index = 0;
    uint32 profile_id = 0;
    uint32 cmd = 0;
    uint8 type = 0;
    uint8 chan_id = 0;
    sys_qos_fc_profile_t *p_sys_fc_profile = NULL;
    sys_qos_fc_profile_t *p_sys_fc_profile_new = NULL;
    ds_igr_port_tc_thrd_profile_t ds_igr_port_tc_thrd_profile;
    ds_igr_port_thrd_profile_t ds_igr_port_thrd_profile;

    sal_memset(&fc_data, 0, sizeof(fc_data));

    CTC_GLOBAL_PORT_CHECK(p_fc->gport);
    CTC_MAX_VALUE_CHECK(p_fc->priority_class, 7);
    chan_id = SYS_GET_CHANNEL_ID(lchip, p_fc->gport);
    CTC_MAX_VALUE_CHECK(chan_id, SYS_MAX_CHANNEL_NUM-1);
    CTC_MAX_VALUE_CHECK(p_fc->xon_thrd, 0x3FFF);
    CTC_MAX_VALUE_CHECK(p_fc->xoff_thrd, 0x3FFF);
    CTC_MAX_VALUE_CHECK(p_fc->drop_thrd, 0x3FFF);

    fc_data.xon = p_fc->xon_thrd;
    fc_data.xoff = p_fc->xoff_thrd;
    fc_data.thrd = p_fc->drop_thrd;

    if (p_fc->is_pfc)
    {
        p_sys_fc_profile = p_gb_queue_master[lchip]->p_pfc_profile[chan_id][p_fc->priority_class];
        type = SYS_RESRC_PROFILE_PFC;
    }
    else
    {
        p_sys_fc_profile = p_gb_queue_master[lchip]->p_fc_profile[chan_id];
        type = SYS_RESRC_PROFILE_FC;
    }

    CTC_ERROR_RETURN(_sys_greatbelt_qos_fc_add_profile(lchip, type, &fc_data,
                                                             p_sys_fc_profile, (void**)&p_sys_fc_profile_new));

    profile_id = p_sys_fc_profile_new->head.profile_id;
    if (p_fc->is_pfc)
    {
        sal_memset(&ds_igr_port_tc_thrd_profile, 0, sizeof(ds_igr_port_tc_thrd_profile_t));

        index = (profile_id << 3);
        cmd = DRV_IOR(DsIgrPortTcThrdProfile_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_igr_port_tc_thrd_profile));
        ds_igr_port_tc_thrd_profile.port_tc_thrd      = p_fc->drop_thrd / 16;
        ds_igr_port_tc_thrd_profile.port_tc_xoff_thrd = p_fc->xoff_thrd;
        ds_igr_port_tc_thrd_profile.port_tc_xon_thrd  = p_fc->xon_thrd;
        cmd = DRV_IOW(DsIgrPortTcThrdProfile_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_igr_port_tc_thrd_profile));

        index = (chan_id << 3 | p_fc->priority_class) / 4;
        cmd = DRV_IOW(DsIgrPortTcThrdProfId_t, DsIgrPortTcThrdProfId_ProfIdHigh0_f + (chan_id << 3 | p_fc->priority_class) % 4);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &profile_id));

        p_gb_queue_master[lchip]->p_pfc_profile[chan_id][p_fc->priority_class] = p_sys_fc_profile_new;
    }
    else
    {
        sal_memset(&ds_igr_port_thrd_profile, 0, sizeof(ds_igr_port_thrd_profile_t));
        index = (profile_id << 3);
        cmd = DRV_IOR(DsIgrPortThrdProfile_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_igr_port_thrd_profile));
        ds_igr_port_thrd_profile.port_thrd      = p_fc->drop_thrd / 16;
        ds_igr_port_thrd_profile.port_xoff_thrd = p_fc->xoff_thrd;
        ds_igr_port_thrd_profile.port_xon_thrd  = p_fc->xon_thrd;
        cmd = DRV_IOW(DsIgrPortThrdProfile_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_igr_port_thrd_profile));

        index = (chan_id) / 8;
        cmd = DRV_IOW(DsIgrPortThrdProfId_t, DsIgrPortThrdProfId_ProfIdHigh0_f + (chan_id) % 8);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &profile_id));

        p_gb_queue_master[lchip]->p_fc_profile[chan_id] = p_sys_fc_profile_new;
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_qos_fc_get_profile(uint8 lchip, ctc_qos_resrc_fc_t *p_fc)
{
    uint8 chan_id = 0;
    sys_qos_fc_profile_t *p_sys_fc_profile = NULL;

    CTC_GLOBAL_PORT_CHECK(p_fc->gport);
    CTC_MAX_VALUE_CHECK(p_fc->priority_class, 7);
    chan_id = SYS_GET_CHANNEL_ID(lchip, p_fc->gport);
    CTC_MAX_VALUE_CHECK(chan_id, SYS_MAX_CHANNEL_NUM-1);

    if (p_fc->is_pfc)
    {
        p_sys_fc_profile = p_gb_queue_master[lchip]->p_pfc_profile[chan_id][p_fc->priority_class];
    }
    else
    {
        p_sys_fc_profile = p_gb_queue_master[lchip]->p_fc_profile[chan_id];
    }

    p_fc->xon_thrd = p_sys_fc_profile->xon;
    p_fc->xoff_thrd = p_sys_fc_profile->xoff;
    p_fc->drop_thrd = p_sys_fc_profile->thrd;

    return CTC_E_NONE;
}

int32
sys_greatbelt_queue_set_resrc(uint8 lchip, ctc_qos_resrc_t* p_resrc)
{
    uint32 cmd                      = 0;
    uint32 field_value              = 0;

    SYS_QUEUE_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_resrc);

    switch (p_resrc->cfg_type)
    {
    case CTC_QOS_RESRC_CFG_POOL_CLASSIFY:
        CTC_ERROR_RETURN(sys_greatbelt_qos_set_resrc_classify_pool(lchip, &p_resrc->u.pool));
        break;

    case CTC_QOS_RESRC_CFG_PORT_MIN:
        CTC_MAX_VALUE_CHECK(p_resrc->u.port_min.threshold, 255);
        field_value = p_resrc->u.port_min.threshold;
        if (p_resrc->u.port_min.dir == CTC_INGRESS)
        {
            cmd = DRV_IOW(IgrPortTcMinProfile_t, IgrPortTcMinProfile_PortTcMin0_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
        }
        else if (p_resrc->u.port_min.dir == CTC_EGRESS)
        {
            cmd = DRV_IOW(QueMinProfile_t, QueMinProfile_QueMin0_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
        }
        else
        {
            return CTC_E_INVALID_PARAM;
        }
        break;

    case CTC_QOS_RESRC_CFG_QUEUE_DROP:
        SYS_QOS_QUEUE_LOCK(lchip);
        CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(sys_greatbelt_qos_set_resrc_queue_drop(lchip, p_resrc->u.queue_drop));
        SYS_QOS_QUEUE_UNLOCK(lchip);
        break;

    case CTC_QOS_RESRC_CFG_FLOW_CTL:
        SYS_QOS_QUEUE_LOCK(lchip);
        CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(sys_greatbelt_qos_fc_add_profile(lchip, &p_resrc->u.flow_ctl));
        SYS_QOS_QUEUE_UNLOCK(lchip);
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_queue_get_resrc(uint8 lchip, ctc_qos_resrc_t* p_resrc)
{
    uint32 cmd                      = 0;
    uint32 field_value              = 0;

    SYS_QUEUE_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_resrc);

    switch (p_resrc->cfg_type)
    {
    case CTC_QOS_RESRC_CFG_POOL_CLASSIFY:
        CTC_ERROR_RETURN(sys_greatbelt_qos_get_resrc_classify_pool(lchip, &p_resrc->u.pool));
        break;

    case CTC_QOS_RESRC_CFG_PORT_MIN:
        if (p_resrc->u.port_min.dir == CTC_INGRESS)
        {
            cmd = DRV_IOR(IgrPortTcMinProfile_t, IgrPortTcMinProfile_PortTcMin0_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
            p_resrc->u.port_min.threshold = field_value;
        }
        else if (p_resrc->u.port_min.dir == CTC_EGRESS)
        {
            cmd = DRV_IOR(QueMinProfile_t, QueMinProfile_QueMin0_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
            p_resrc->u.port_min.threshold = field_value;
        }
        else
        {
            return CTC_E_INVALID_PARAM;
        }
        break;

    case CTC_QOS_RESRC_CFG_QUEUE_DROP:
        SYS_QOS_QUEUE_LOCK(lchip);
        CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(sys_greatbelt_qos_get_resrc_queue_drop(lchip, p_resrc->u.queue_drop));
        SYS_QOS_QUEUE_UNLOCK(lchip);
        break;

    case CTC_QOS_RESRC_CFG_FLOW_CTL:
        SYS_QOS_QUEUE_LOCK(lchip);
        CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(sys_greatbelt_qos_fc_get_profile(lchip, &p_resrc->u.flow_ctl));
        SYS_QOS_QUEUE_UNLOCK(lchip);
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_queue_query_pool_stats(uint8 lchip, ctc_qos_resrc_pool_stats_t* p_stats)
{
    uint32 cmd = 0;
    uint32 field_value = 0;
    uint16 queue_id = 0;
    ctc_qos_queue_id_t queue;
    ctc_qos_queue_id_t queue_temp;

    CTC_PTR_VALID_CHECK(p_stats);
    SYS_QUEUE_DBG_FUNC();

    sal_memset(&queue, 0, sizeof(queue));

    switch (p_stats->type)
    {
    case CTC_QOS_RESRC_STATS_IGS_POOL_COUNT:
        CTC_MAX_VALUE_CHECK(p_stats->pool, CTC_QOS_IGS_RESRC_NON_DROP_POOL);
        cmd = DRV_IOR(IgrScCnt_t, IgrScCnt_ScCnt0_f + p_stats->pool);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
        p_stats->count = field_value;

        break;

    case CTC_QOS_RESRC_STATS_IGS_TOTAL_COUNT:
        cmd = DRV_IOR(BufStoreTotalResrcInfo_t, BufStoreTotalResrcInfo_IgrTotalCnt_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
        p_stats->count = field_value;
        break;

    case CTC_QOS_RESRC_STATS_EGS_POOL_COUNT:
        CTC_MAX_VALUE_CHECK(p_stats->pool, CTC_QOS_EGS_RESRC_CONTROL_POOL);
        cmd = DRV_IOR(EgrScCnt_t, EgrScCnt_ScCnt0_f + p_stats->pool);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
        p_stats->count = field_value;
        break;

    case CTC_QOS_RESRC_STATS_EGS_TOTAL_COUNT:
        cmd = DRV_IOR(EgrTotalCnt_t, EgrTotalCnt_TotalCnt_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
        p_stats->count = field_value;
        break;

    case CTC_QOS_RESRC_STATS_QUEUE_COUNT:
        CTC_MAX_VALUE_CHECK(p_stats->priority, SYS_QOS_CLASS_PRIORITY_MAX);

        sal_memset(&queue_temp, 0, sizeof(ctc_qos_queue_id_t));
        if (sal_memcmp(&(p_stats->queue), &queue_temp, sizeof(ctc_qos_queue_id_t)))
        {
            sal_memcpy(&queue, &(p_stats->queue), sizeof(ctc_qos_queue_id_t));
        }
        else
        {
            queue.queue_type = CTC_QUEUE_TYPE_NETWORK_EGRESS;
            queue.gport = p_stats->gport;
            queue.queue_id = p_stats->priority / ((p_gb_queue_master[lchip]->priority_mode)? 2:8);
        }

        SYS_QOS_QUEUE_LOCK(lchip);
        CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(_sys_greatbelt_queue_get_queue_id(lchip, &queue, &queue_id));
        SYS_QOS_QUEUE_UNLOCK(lchip);

        cmd = DRV_IOR(DsQueCnt_t, DsQueCnt_QueInstCnt_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, queue_id, cmd, &field_value));
        p_stats->count = field_value;
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_queue_set_default_drop(uint8 lchip,
                                     uint16 queue_id,
                                     uint8 profile_id)
{
    uint32 cmd;
    ds_egr_resrc_ctl_t ds_egr_resrc_ctl;
    sys_queue_node_t* p_sys_queue_node = NULL;

    sal_memset(&ds_egr_resrc_ctl, 0, sizeof(ds_egr_resrc_ctl_t));



    p_sys_queue_node = ctc_vector_get(p_gb_queue_master[lchip]->queue_vec, queue_id);
    if (NULL == p_sys_queue_node)
    {
        uint32 field_val = 0;
        cmd = DRV_IOW(DsQueMap_t, DsQueMap_GrpId_f);
        field_val = SYS_MAX_QUEUE_GROUP_NUM-1;
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, queue_id, cmd, &field_val));
        return CTC_E_NONE;

    }

    cmd = DRV_IOR(DsEgrResrcCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, queue_id, cmd, &ds_egr_resrc_ctl));

    ds_egr_resrc_ctl.mapped_tc = p_sys_queue_node->offset;
    ds_egr_resrc_ctl.que_min_prof_id = 0;
    ds_egr_resrc_ctl.que_thrd_prof_id_high = profile_id;

    cmd = DRV_IOW(DsEgrResrcCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, queue_id, cmd, &ds_egr_resrc_ctl));

    return CTC_E_NONE;
}

/**
 @brief Initialize default queue drop.
*/
STATIC int32
_sys_greatbelt_queue_drop_default_init(uint8 lchip, ctc_qos_global_cfg_t* p_cfg)
{
    uint16 queue_id;
    uint8 cng_level = 0;
    uint8 tc = 0;
    uint8 sc = 0;
    uint32 index = 0;
    uint32 field_val = 0;
    uint32 field_id = 0;
    uint32 cmd;
    uint8 level_num = 0;
    uint8 profile_index = 0;
    ds_que_thrd_profile_t ds_que_thrd_profile;
    egr_resrc_mgr_ctl_t egr_resrc_mgr_ctl;
    q_mgr_enq_ctl_t q_mgr_enq_ctl;
    q_mgr_enq_drop_ctl_t q_mgr_enq_drop_ctl;
    que_min_profile_t que_min_profile;
    ds_grp_drop_profile_t ds_grp_drop_profile;
    ds_egr_port_drop_profile_t ds_egr_port_drop_profile;
    egr_sc_thrd_t egr_sc_thrd;
    uint16 grpThrd[8]  = {588, 441, 294, 147, 73, 36, 12, 9};
    uint16 portThrd[8] = {588, 441, 294, 147, 73, 36, 12, 9};
    uint16 tcThrd[8]   = {720, 716, 712, 708, 704, 700, 696, 692};
    uint8  drop_sed[128] =
                          {
                      13  , 46  , 23  , 17  , 67  , 5   , 92  , 93  , 41  , 10  ,
                      43  , 81  , 115 , 6   , 94  , 127 , 66  , 123 , 4   , 47  ,
                      45  , 102 , 88  , 85  , 118 , 65  , 116 , 56  , 119 , 70  ,
                      39  , 72  , 73  , 97  , 40  , 33  , 25  , 76  , 19  , 26  ,
                      51  , 64  , 8   , 87  , 36  , 44  , 60  , 16  , 1   , 2   ,
                      112 , 50  , 37  , 100 , 84  , 121 , 98  , 30  , 108 , 3   ,
                      101 , 57  , 58  , 59  , 110 , 107 , 32  , 48  , 96  , 122 ,
                      63  , 68  , 52  , 49  , 71  , 104 , 69  , 86  , 91  , 24  ,
                      111 , 105 , 82  , 27  , 34  , 53  , 62  , 106 , 114 , 125 ,
                      21  , 109 , 38  , 9   , 20  , 11  , 35  , 7   , 78  , 113 ,
                      75  , 1   , 15  , 103 , 18  , 14  , 117 , 89  , 126 , 61  ,
                      22  , 42  , 55  , 83  , 90  , 29  , 124 , 31  , 80  , 120 ,
                      99  , 79  , 95  , 12  , 74  , 54  , 28  , 77
                      };



    uint32 pool_size[CTC_QOS_EGS_RESRC_POOL_MAX] = {0};

    /* 1. Buffer assign, total buffer cell is 12K */
    if (p_cfg->resrc_pool.egs_pool_mode == CTC_QOS_RESRC_POOL_DISABLE)
    {
        /* only check sc|total sc */
        pool_size[CTC_QOS_EGS_RESRC_DEFAULT_POOL]   = 11776;    /* 11.5K */
        pool_size[CTC_QOS_EGS_RESRC_NON_DROP_POOL]  = 0;
        pool_size[CTC_QOS_EGS_RESRC_SPAN_POOL]      = 0;
        pool_size[CTC_QOS_EGS_RESRC_CONTROL_POOL]   = 512;     /* 0.5K */
        pool_size[CTC_QOS_EGS_RESRC_MIN_POOL]       = 0;
        pool_size[CTC_QOS_EGS_RESRC_C2C_POOL]       = 0;
    }
    else if (p_cfg->resrc_pool.egs_pool_mode == CTC_QOS_RESRC_POOL_MODE1)
    {
        sal_memcpy(pool_size, p_cfg->resrc_pool.egs_pool_size, sizeof(pool_size));
        if (pool_size[CTC_QOS_EGS_RESRC_NON_DROP_POOL] || pool_size[CTC_QOS_EGS_RESRC_SPAN_POOL])
        {
            return CTC_E_INVALID_PARAM;
        }
    }
    else if (p_cfg->resrc_pool.egs_pool_mode == CTC_QOS_RESRC_POOL_MODE2)
    {
        sal_memcpy(pool_size, p_cfg->resrc_pool.egs_pool_size, sizeof(pool_size));
        if ((pool_size[CTC_QOS_EGS_RESRC_DEFAULT_POOL] == 0)
            || (pool_size[CTC_QOS_EGS_RESRC_NON_DROP_POOL] == 0)
            || (pool_size[CTC_QOS_EGS_RESRC_CONTROL_POOL] == 0))
        {
            return CTC_E_INVALID_PARAM;
        }
    }
    else if (p_cfg->resrc_pool.egs_pool_mode == CTC_QOS_RESRC_POOL_USER_DEFINE)
    {
        sal_memcpy(pool_size, p_cfg->resrc_pool.egs_pool_size, sizeof(pool_size));
    }

    if (pool_size[CTC_QOS_EGS_RESRC_DEFAULT_POOL] == 0) /* must have default pool */
    {
        return CTC_E_INVALID_PARAM;
    }

    /*
    configure register fields for egress resource management
    1. enable egress resource management
    2. egress resource management is queue entry - based
    3. don't drop c2c and critical packets
    4. c2c and critical pools are reserved with 512 queue entries
    */
    sal_memset(&egr_resrc_mgr_ctl, 0, sizeof(egr_resrc_mgr_ctl_t));
    cmd = DRV_IOR(EgrResrcMgrCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &egr_resrc_mgr_ctl));
    egr_resrc_mgr_ctl.egr_resrc_mgr_en = 1;
    egr_resrc_mgr_ctl.egr_total_thrd = (12*1024 - 512)/16;
    egr_resrc_mgr_ctl.en_c2c_packet_chk = 1;
    egr_resrc_mgr_ctl.en_critical_packet_chk = 1;
    cmd = DRV_IOW(EgrResrcMgrCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &egr_resrc_mgr_ctl));

    sal_memset(&q_mgr_enq_ctl, 0, sizeof(q_mgr_enq_ctl_t));
    cmd = DRV_IOR(QMgrEnqCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &q_mgr_enq_ctl));
    q_mgr_enq_ctl.based_on_buf_cnt = 1;
    q_mgr_enq_ctl.c2c_pkt_force_no_drop = 0;
    q_mgr_enq_ctl.critical_pkt_force_no_drop = 0;
    cmd = DRV_IOW(QMgrEnqCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &q_mgr_enq_ctl));

    /*
    configure QMgrEnqDropCtl register fields
    1. global enable wred random drop
    2. WRED don't care resource management discard conditions
    3. all non - tcp use drop precedence 0
    */
    sal_memset(&q_mgr_enq_drop_ctl, 0, sizeof(q_mgr_enq_drop_ctl_t));
    cmd = DRV_IOR(QMgrEnqDropCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &q_mgr_enq_drop_ctl));
    q_mgr_enq_drop_ctl.critical_pkt_drop_pri = 3;
    q_mgr_enq_drop_ctl.non_tcp_drop_pri = 0;
    q_mgr_enq_drop_ctl.wred_care_resrc_mgr = 0;
    q_mgr_enq_drop_ctl.wred_mode_glb_en = 1;
    cmd = DRV_IOW(QMgrEnqDropCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &q_mgr_enq_drop_ctl));

    /*
    three queue minimum guarantee profiles
    profile 0: 4 queue entries, used for GE channel queues
    profile 1: 7 queue entries, used for XGE channel queues
    profile 2: 0 queue entry, no guarantee
    */
    sal_memset(&que_min_profile, 0, sizeof(que_min_profile_t));
    cmd = DRV_IOR(QueMinProfile_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &que_min_profile));
    que_min_profile.que_min0 = 7;
    cmd = DRV_IOW(QueMinProfile_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &que_min_profile));

    for (sc = 0; sc < 4; sc ++)
    {
        if (p_cfg->resrc_pool.drop_profile[sc].congest_level_num == 0)
        {
            p_cfg->resrc_pool.drop_profile[sc].congest_level_num = 1;
        }
        level_num = p_cfg->resrc_pool.drop_profile[sc].congest_level_num;

        if (pool_size[sc])
        {
            p_gb_queue_master[lchip]->egs_pool[sc].egs_congest_level_num = p_cfg->resrc_pool.drop_profile[sc].congest_level_num;
            p_gb_queue_master[lchip]->egs_pool[sc].default_profile_id = profile_index;
            profile_index ++;
        }
    }

    /*queue threshold profile 1 -- for tail-drop purpose*/
    for (cng_level = 0; cng_level < 8; cng_level++)
    {
        sal_memset(&ds_que_thrd_profile, 0, sizeof(ds_que_thrd_profile_t));
        ds_que_thrd_profile.wred_max_thrd0 = 0;
        ds_que_thrd_profile.wred_max_thrd1 = 0;
        ds_que_thrd_profile.wred_max_thrd2 = 0;
        ds_que_thrd_profile.wred_max_thrd3 = 0;
        cmd = DRV_IOW(DsQueThrdProfile_t, DRV_ENTRY_FLAG);
        index = (SYS_MAX_TAIL_DROP_PROFILE << 3) + cng_level;
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_que_thrd_profile));
    }

    for (index = 0; index < 128; index++)
    {
        cmd = DRV_IOW(DsUniformRand_t, DsUniformRand_DropRandomNum_f);
        field_val = drop_sed[index];
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &field_val));
    }

    /*group threshold profile 0, default disable*/
    for (cng_level = 0; cng_level < 8; cng_level++)
    {
        sal_memset(&ds_grp_drop_profile, 0, sizeof(ds_grp_drop_profile_t));
        cmd = DRV_IOW(DsGrpDropProfile_t, DRV_ENTRY_FLAG);
        ds_grp_drop_profile.grp_drop_thrd = grpThrd[cng_level];
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, cng_level, cmd, &ds_grp_drop_profile));
    }

    /*port threshold profile 0, default disable*/
    for (cng_level = 0; cng_level < 8; cng_level++)
    {
        sal_memset(&ds_egr_port_drop_profile, 0, sizeof(ds_egr_port_drop_profile_t));
        cmd = DRV_IOW(DsEgrPortDropProfile_t, DRV_ENTRY_FLAG);
        ds_egr_port_drop_profile.port_drop_thrd = portThrd[cng_level];
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, cng_level, cmd, &ds_egr_port_drop_profile));
    }

    /*tc threshold, default disable */
    for (tc = 0; tc < 8; tc++)
    {
        field_val = tcThrd[tc];
        field_id = EgrTcThrd_TcThrd0_f + tc;
        cmd = DRV_IOW(EgrTcThrd_t, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    }

    /*scthreshold */
     /*sc0 field_val = 720;*/
    sal_memset(&egr_sc_thrd, 0, sizeof(egr_sc_thrd_t));
    cmd = DRV_IOR(EgrScThrd_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &egr_sc_thrd));
    egr_sc_thrd.sc_thrd0 = pool_size[CTC_QOS_EGS_RESRC_DEFAULT_POOL]/16;
    egr_sc_thrd.sc_thrd1 = pool_size[CTC_QOS_EGS_RESRC_NON_DROP_POOL]/16;
    /*egr_sc_thrd.sc_thrd2 = pool_size[CTC_QOS_EGS_RESRC_SPAN_POOL]/16;*/
    egr_sc_thrd.sc_thrd3 = pool_size[CTC_QOS_EGS_RESRC_CONTROL_POOL]/16;
    cmd = DRV_IOW(EgrScThrd_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &egr_sc_thrd));

    /*evaluate congestion level to enable self-tuning threshold */
    for (sc = 0; sc < 4; sc ++)
    {
        if (p_cfg->resrc_pool.drop_profile[sc].congest_level_num == 0)
        {
            p_cfg->resrc_pool.drop_profile[sc].congest_level_num = 1;
        }
        level_num = p_cfg->resrc_pool.drop_profile[sc].congest_level_num-1;
        for (cng_level = 0; cng_level < level_num; cng_level ++)
        {
            field_val = p_cfg->resrc_pool.drop_profile[sc].congest_threshold[cng_level]/64;
            field_id = EgrCongestLevelThrd_Sc0Thrd0_f + sc*7 + cng_level+(7-level_num);
            cmd = DRV_IOW(EgrCongestLevelThrd_t, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        }
    }

    /*EgrCondDisProfile 0*/
    /* SYS_RESRC_EGS_COND (que_min, total, sc, tc, port, grp, queue) */
    field_val = SYS_RESRC_EGS_COND(0, 0, 0, 1, 1, 1, 0);
    field_id = EgrCondDisProfile_CondDisBmp0_f;
    cmd = DRV_IOW(EgrCondDisProfile_t, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    /*
    queue configurations for egress resource management
    1. all queue mapped to SC 0
    2. GE channel queues mapped to TC 0
    3. XGE channel queues mapped to TC 0 - 7, respectively
    */
    for (queue_id = 0; queue_id < SYS_MAX_QUEUE_NUM; queue_id++)
    {
        CTC_ERROR_RETURN(
            sys_greatbelt_queue_set_default_drop(lchip, queue_id, SYS_DEFAULT_TAIL_DROP_PROFILE));

    }


    /*for drop queue*/
    /*group threshold profile 1 for discard*/
    for (cng_level = 0; cng_level < 8; cng_level++)
    {
        sal_memset(&ds_grp_drop_profile, 0, sizeof(ds_grp_drop_profile_t));
        cmd = DRV_IOW(DsGrpDropProfile_t, DRV_ENTRY_FLAG);
        ds_grp_drop_profile.grp_drop_thrd = 0;
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, (1*8 + cng_level), cmd, &ds_grp_drop_profile));
    }

    /*EgrCondDisProfile 1, for group drop*/
    /* SYS_RESRC_EGS_COND (que_min, total, sc, tc, port, grp, queue) */
    field_val = SYS_RESRC_EGS_COND(1, 0, 0, 1, 1, 0, 0);
    field_id = EgrCondDisProfile_CondDisBmp0_f + 1;
    cmd = DRV_IOW(EgrCondDisProfile_t, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    return CTC_E_NONE;
}



int32
sys_greatbelt_queue_get_port_depth(uint8 lchip, uint16 gport, uint32* p_depth)
{
    uint32 cmd = 0;
    uint32 field_val = 0;
    uint8 chan_id = 0;

    CTC_PTR_VALID_CHECK(p_depth);

    chan_id = SYS_GET_CHANNEL_ID(lchip, gport);
    if (0xff == chan_id)
    {
        return CTC_E_INVALID_PORT;
    }

    cmd = DRV_IOR(DsEgrPortCnt_t, DsEgrPortCnt_PortCnt_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &field_val));

    *p_depth = field_val;

    return CTC_E_NONE;
}


int32
sys_greatbelt_queue_set_port_drop_en(uint8 lchip, uint16 gport, bool enable, uint32 drop_type)
{

    uint32 cmd = 0;
    uint8 queue_num = 0 ;
    uint16 queue_id =0;
    uint8 lport = 0;
    uint8 offset = 0;
    uint8 drop_chan = 0xFF;
    ctc_qos_queue_id_t queue;
    ds_egr_resrc_ctl_t ds_egr_resrc_ctl;
    q_mgr_reserved_channel_range_t qmgt_rsv_chan_rag;

    sal_memset(&ds_egr_resrc_ctl, 0, sizeof(ds_egr_resrc_ctl_t));
    sal_memset(&queue, 0, sizeof(queue));
    sal_memset(&qmgt_rsv_chan_rag, 0, sizeof(qmgt_rsv_chan_rag));

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    queue.gport = gport;
    if (lport < SYS_GB_MAX_PHY_PORT)
    {
        queue.queue_type = CTC_QUEUE_TYPE_NETWORK_EGRESS;
    }
    else
    {
        queue.queue_type = CTC_QUEUE_TYPE_INTERNAL_PORT;
    }


    /* get drop channel */
    cmd =  DRV_IOR(QMgrReservedChannelRange_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &qmgt_rsv_chan_rag));
    if (qmgt_rsv_chan_rag.reserved_channel_valid0)
    {
        drop_chan = qmgt_rsv_chan_rag.reserved_channel_min0;
    }

    if(CTC_FLAG_ISSET(drop_type, SYS_QUEUE_DROP_TYPE_PROFILE))
    {
        SYS_QOS_QUEUE_LOCK(lchip);
        queue_num = p_gb_queue_master[lchip]->channel[lport].queue_info.queue_num;
        for (offset = 0; offset < queue_num; offset++)
        {
            queue.queue_id = offset;
            CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(_sys_greatbelt_queue_get_queue_id(lchip,
                                                               &queue,
                                                               &queue_id));

            cmd = DRV_IOR(DsEgrResrcCtl_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(DRV_IOCTL(lchip, queue_id, cmd, &ds_egr_resrc_ctl));

            ds_egr_resrc_ctl.grp_drop_prof_id_high = enable?1:0;
            ds_egr_resrc_ctl.egr_cond_dis_prof_id = enable?1:0;

            cmd = DRV_IOW(DsEgrResrcCtl_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(DRV_IOCTL(lchip, queue_id, cmd, &ds_egr_resrc_ctl));
        }
        SYS_QOS_QUEUE_UNLOCK(lchip);
    }

    if(CTC_FLAG_ISSET(drop_type, SYS_QUEUE_DROP_TYPE_CHANNEL) && qmgt_rsv_chan_rag.reserved_channel_valid0)
    {
        /* add drop channel */
        if(enable)
        {
            sys_greatbelt_add_port_to_channel(lchip, gport, drop_chan);
        }
        else
        {
            sys_greatbelt_remove_port_from_channel(lchip, gport, drop_chan);
        }
    }

    return CTC_E_NONE;

}

/**
 @brief Queue dropping initialization.
*/
int32
sys_greatbelt_queue_drop_init(uint8 lchip, void *p_glb_parm)
{
    sys_greatbelt_opf_t opf;
    ctc_spool_t spool;
    uint8 gchip = 0;
    uint8 profile_used_cnt = 0;
    ctc_qos_global_cfg_t * p_glb_cfg = NULL;

    p_glb_cfg = (ctc_qos_global_cfg_t *)p_glb_parm;

    /* init queue shape hash table */
    sal_memset(&spool, 0, sizeof(ctc_spool_t));
    spool.lchip = lchip;
    spool.block_num = SYS_DROP_PROFILE_BLOCK_NUM;
    spool.block_size = SYS_DROP_PROFILE_BLOCK_SIZE;
    spool.max_count = SYS_MAX_DROP_PROFILE_NUM;
    spool.user_data_size = sizeof(sys_queue_drop_profile_t);
    spool.spool_key = (hash_key_fn)_sys_greatbelt_queue_drop_profile_hash_make;
    spool.spool_cmp = (hash_cmp_fn)_sys_greatbelt_queue_drop_profile_hash_cmp;
    p_gb_queue_master[lchip]->p_drop_profile_pool = ctc_spool_create(&spool);

    sal_memset(&spool, 0, sizeof(ctc_spool_t));
    spool.lchip = lchip;
    spool.block_num = 1;
    spool.block_size = 1;
    spool.max_count = SYS_MAX_FC_PROFILE_NUM;
    spool.user_data_size = sizeof(sys_qos_fc_profile_t);
    spool.spool_key = (hash_key_fn)_sys_greatbelt_qos_fc_profile_hash_make;
    spool.spool_cmp = (hash_cmp_fn)_sys_greatbelt_qos_fc_profile_hash_cmp;
    p_gb_queue_master[lchip]->p_resrc_profile_pool[SYS_RESRC_PROFILE_FC] = ctc_spool_create(&spool);

    sal_memset(&spool, 0, sizeof(ctc_spool_t));
    spool.lchip = lchip;
    spool.block_num = 1;
    spool.block_size = 1;
    spool.max_count = SYS_MAX_PFC_PROFILE_NUM;
    spool.user_data_size = sizeof(sys_qos_fc_profile_t);
    spool.spool_key = (hash_key_fn)_sys_greatbelt_qos_fc_profile_hash_make;
    spool.spool_cmp = (hash_cmp_fn)_sys_greatbelt_qos_fc_profile_hash_cmp;
    p_gb_queue_master[lchip]->p_resrc_profile_pool[SYS_RESRC_PROFILE_PFC] = ctc_spool_create(&spool);

    CTC_ERROR_RETURN(_sys_greatbelt_queue_drop_default_init(lchip, p_glb_cfg));

    /* add static profile */
    CTC_ERROR_RETURN(_sys_greatbelt_queue_drop_add_static_profile(lchip, p_glb_cfg));
    CTC_ERROR_RETURN(_sys_greatbelt_qos_fc_add_static_profile(lchip));

    /*opf init*/
    CTC_ERROR_RETURN(sys_greatbelt_opf_init(lchip, OPF_QUEUE_DROP_PROFILE, SYS_RESRC_PROFILE_MAX));

    sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));

    opf.pool_index = SYS_RESRC_PROFILE_QUEUE;
    opf.pool_type = OPF_QUEUE_DROP_PROFILE;
    profile_used_cnt = p_gb_queue_master[lchip]->p_drop_profile_pool->count;
    CTC_ERROR_RETURN(sys_greatbelt_opf_init_offset(lchip, &opf, profile_used_cnt,
                                                   SYS_MAX_DROP_PROFILE_NUM - profile_used_cnt));

    opf.pool_index = SYS_RESRC_PROFILE_FC;
    opf.pool_type = OPF_QUEUE_DROP_PROFILE;
    profile_used_cnt = p_gb_queue_master[lchip]->p_resrc_profile_pool[SYS_RESRC_PROFILE_FC]->count;
    CTC_ERROR_RETURN(sys_greatbelt_opf_init_offset(lchip, &opf, profile_used_cnt,
                                                   SYS_MAX_FC_PROFILE_NUM - profile_used_cnt));


    opf.pool_index = SYS_RESRC_PROFILE_PFC;
    opf.pool_type = OPF_QUEUE_DROP_PROFILE;
    profile_used_cnt = p_gb_queue_master[lchip]->p_resrc_profile_pool[SYS_RESRC_PROFILE_PFC]->count;
    CTC_ERROR_RETURN(sys_greatbelt_opf_init_offset(lchip, &opf, profile_used_cnt,
                                                   SYS_MAX_PFC_PROFILE_NUM - profile_used_cnt));


    /* init drop local phy port */

    sys_greatbelt_get_gchip_id(lchip, &gchip);
    CTC_ERROR_RETURN(sys_greatbelt_queue_set_port_drop_en(lchip, CTC_MAP_LPORT_TO_GPORT(gchip, SYS_RESERVE_PORT_ID_DROP),
                                                          TRUE, SYS_QUEUE_DROP_TYPE_ALL));

    return CTC_E_NONE;
}


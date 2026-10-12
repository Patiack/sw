/**
 @file sys_goldengate_queue_drop.c

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

#include "sys_goldengate_chip.h"
#include "sys_goldengate_opf.h"
#include "sys_goldengate_ftm.h"
#include "sys_goldengate_queue_enq.h"
#include "sys_goldengate_queue_drop.h"

#include "goldengate/include/drv_enum.h"
#include "goldengate/include/drv_io.h"

/****************************************************************************
 *
 * Global and Declaration
 *
 ****************************************************************************/

#define SYS_MAX_DROP_THRD (32 * 1024 - 1)
#define SYS_MAX_DROP_PROB (32 - 1)

#define SYS_RESRC_IGS_COND(tc_min,total,sc,tc,port,port_tc) \
    ((tc_min<<5)|(total<<4)|(sc<<3)|(tc<<2)|(port <<1)|(port_tc<<0))

#define SYS_RESRC_EGS_COND(que_min,total,sc_pkt,sc,tc,port, grp,queue) \
    ((que_min<<7)|(total<<6)|(sc_pkt<<5)|(sc<<4)|(tc<<3)|(port<<2)|(grp<<1)|(queue<<0))


extern sys_queue_master_t* p_gg_queue_master[CTC_MAX_LOCAL_CHIP_NUM];

/****************************************************************************
 *
 * Function
 *
 ****************************************************************************/

STATIC uint32
_sys_goldengate_qos_profile_hash_make(void* p_prof)
{
    sys_qos_profile_head_t *head = p_prof;
    uint8* data = (uint8*)(p_prof) + sizeof(sys_qos_profile_head_t);
    uint16   length = head->profile_len - sizeof(sys_qos_profile_head_t);

    return ctc_hash_caculate(length, data);

    return CTC_E_NONE;
}

STATIC bool
_sys_goldengate_qos_profile_hash_cmp(void* p_prof1,
                                           void* p_prof2)
{

    sys_qos_profile_head_t *head = p_prof1;
    uint16   length = head->profile_len - sizeof(sys_qos_profile_head_t);
    SYS_QUEUE_DBG_FUNC();

    if (!p_prof1 || !p_prof2)
    {
        return FALSE;
    }

    if (!sal_memcmp((uint8*)(p_prof1) + sizeof(sys_qos_profile_head_t),
                    (uint8*)(p_prof2) + sizeof(sys_qos_profile_head_t),
                    length))
    {
        return TRUE;
    }

    return FALSE;
}


STATIC int32
_sys_goldengate_qos_profile_alloc_offset(uint8 lchip, uint8 type,
                                               uint16* profile_id)
{
    sys_goldengate_opf_t opf;
    uint32 offset  = 0;

    SYS_QUEUE_DBG_FUNC();

    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
    opf.pool_type = OPF_QUEUE_DROP_PROFILE;
    opf.pool_index = type;

    CTC_ERROR_RETURN(sys_goldengate_opf_alloc_offset(lchip, &opf, 1, &offset));

    *profile_id = offset;

    SYS_QUEUE_DBG_INFO("alloc drop profile id = %d\n", offset);


    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_qos_profile_free_offset(uint8 lchip, uint8 type,
                                              uint16 profile_id)
{
    sys_goldengate_opf_t opf;
    uint32 offset  = 0;

    SYS_QUEUE_DBG_FUNC();

    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
    opf.pool_type = OPF_QUEUE_DROP_PROFILE;
    opf.pool_index = type;

    offset = profile_id;

    CTC_ERROR_RETURN(sys_goldengate_opf_free_offset(lchip, &opf, 1, offset));

    SYS_QUEUE_DBG_INFO("free drop profile id = %d\n", offset);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_qos_profile_build_data(uint8 lchip, uint8 type,
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
_sys_goldengate_qos_remove_profile(uint8 lchip, uint8 type,
                                         void* p_sys_profile_old)
{
    void* p_sys_profile_find = NULL;
    ctc_spool_t* p_profile_pool               = NULL;
    int32 ret            = 0;
    uint16 profile_id    = 0;

    SYS_QUEUE_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_sys_profile_old);

    p_profile_pool = p_gg_queue_master[lchip]->p_resrc_profile_pool[type];

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
        CTC_ERROR_RETURN(_sys_goldengate_qos_profile_free_offset(lchip, type,profile_id));
        mem_free(p_sys_profile_find);
        p_sys_profile_old = NULL;
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_qos_add_static_profile(uint8 lchip, uint8 type,
                                               void* p_data,
                                               uint16 profile_id,
                                               void** pp_sys_profile_get)
{
    ctc_spool_t* p_profile_pool    = NULL;
    void* p_sys_profile_new = NULL;
    int32 ret            = 0;

    SYS_QUEUE_DBG_FUNC();

    p_profile_pool = p_gg_queue_master[lchip]->p_resrc_profile_pool[type];

    p_sys_profile_new = mem_malloc(MEM_QUEUE_MODULE, p_profile_pool->user_data_size);
    if (NULL == p_sys_profile_new)
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_sys_profile_new, 0, p_profile_pool->user_data_size);

    ((sys_qos_profile_head_t *)p_sys_profile_new)->profile_id = profile_id;
    ((sys_qos_profile_head_t *)p_sys_profile_new)->profile_len = p_profile_pool->user_data_size;

    _sys_goldengate_qos_profile_build_data(lchip, type, p_data, p_sys_profile_new);

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
_sys_goldengate_qos_add_profile(uint8 lchip, uint8 type,
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

    p_profile_pool = p_gg_queue_master[lchip]->p_resrc_profile_pool[type];

    p_sys_profile_new = mem_malloc(MEM_QUEUE_MODULE, p_profile_pool->user_data_size);
    if (NULL == p_sys_profile_new)
    {
        return CTC_E_NO_MEMORY;
    }
    sal_memset(p_sys_profile_new, 0, p_profile_pool->user_data_size);
    ((sys_qos_profile_head_t *)p_sys_profile_new)->profile_len = p_profile_pool->user_data_size;

    _sys_goldengate_qos_profile_build_data(lchip, type, p_data, p_sys_profile_new);
    /*if use new date to replace old data, profile_id will not change*/
    if(p_sys_profile_old)
    {
        if(TRUE == _sys_goldengate_qos_profile_hash_cmp(p_sys_profile_old, p_sys_profile_new))
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
            ret = _sys_goldengate_qos_profile_alloc_offset(lchip, type, &new_profile_id);
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
        CTC_ERROR_RETURN(_sys_goldengate_qos_remove_profile(lchip, type, p_sys_profile_old));
    }

    return CTC_E_NONE;

ERROR_RETURN:
    return ret;
}

int32
_sys_goldengate_qos_fc_add_static_profile(uint8 lchip)
{
    sys_qos_fc_profile_t fc_data;
    sys_qos_fc_profile_t *p_sys_fc_profile_new = NULL;
    uint8 type = 0;
    uint8 profile = 0;
    uint16 index = 0;
    uint32 cmd = 0;
    DsIgrPortTcThrdProfile_m ds_igr_port_tc_thrd_profile;
    DsIgrPortThrdProfile_m ds_igr_port_thrd_profile;
    uint8 slice_id = 0;
    uint16 chan_id = 0;
    uint16 field_id = 0;
    uint32 field_val = 0;
    uint8 tc = 0;

    uint32 tc_thrd[2][3] =
    {
        {32752, 256, 224},
        {32752, 0x3FFFF, 0x3FFFF}
    };

    uint32 port_thrd[2][3] =
    {
        {32752, 256, 224},
        {32752, 0x3FFFF, 0x3FFFF}
    };

    sal_memset(&fc_data, 0, sizeof(fc_data));
    sal_memset(&ds_igr_port_tc_thrd_profile, 0, sizeof(DsIgrPortTcThrdProfile_m));
    sal_memset(&ds_igr_port_thrd_profile, 0, sizeof(ds_igr_port_thrd_profile));

    for (profile = 0; profile < 2; profile++)   /* here has 2 profile */
    {
        index = (profile << 3 | 0);

        SetDsIgrPortTcThrdProfile(V, portTcThrd_f, &ds_igr_port_tc_thrd_profile, tc_thrd[profile][0] / 16);  /*31k cell*/
        SetDsIgrPortTcThrdProfile(V, portTcXoffThrd_f  , &ds_igr_port_tc_thrd_profile, tc_thrd[profile][1]);
        SetDsIgrPortTcThrdProfile(V, portTcXonThrd_f  , &ds_igr_port_tc_thrd_profile, tc_thrd[profile][2]);
        cmd = DRV_IOW(DsIgrPortTcThrdProfile_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_igr_port_tc_thrd_profile));

        fc_data.thrd =  tc_thrd[profile][0];
        fc_data.xoff = tc_thrd[profile][1];
        fc_data.xon = tc_thrd[profile][2];
        type = SYS_RESRC_PROFILE_PFC;
        CTC_ERROR_RETURN(_sys_goldengate_qos_add_static_profile(lchip, type, &fc_data,
                                                                profile, (void**)&p_sys_fc_profile_new));

        if (profile == 0)
        {
            /* configure portTcThrdProfile ID, precascade */
            for (slice_id = 0; slice_id < 2; slice_id++)
            {
                for (chan_id = 0; chan_id < 64; chan_id++)
                {
                    index = 96 *slice_id + chan_id ;
                    field_val = 0;
                    for (tc = 0; tc < 8; tc++)
                    {
                        field_id = DsIgrPortTcThrdProfId_profIdHigh0_f +  tc;
                        cmd = DRV_IOW(DsIgrPortTcThrdProfId_t, field_id);
                        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &field_val));
                        p_gg_queue_master[lchip]->p_pfc_profile[chan_id + slice_id*64][tc] = p_sys_fc_profile_new;
                    }
                }
            }
        }

    }


    for (profile = 0; profile < 2; profile++)
    {
        index = profile << 3 | 0;
        cmd = DRV_IOR(DsIgrPortThrdProfile_t, DRV_ENTRY_FLAG);
        SetDsIgrPortThrdProfile(V, portThrd_f, &ds_igr_port_thrd_profile, port_thrd[profile][0] / 16);  /*31k cell*/
        SetDsIgrPortThrdProfile(V, portXoffThrd_f  , &ds_igr_port_thrd_profile, port_thrd[profile][1]);
        SetDsIgrPortThrdProfile(V, portXonThrd_f  , &ds_igr_port_thrd_profile, port_thrd[profile][2]);
        cmd = DRV_IOW(DsIgrPortThrdProfile_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_igr_port_thrd_profile));

        fc_data.thrd = port_thrd[profile][0];
        fc_data.xoff = port_thrd[profile][1];
        fc_data.xon = port_thrd[profile][2];
        type = SYS_RESRC_PROFILE_FC;
        CTC_ERROR_RETURN(_sys_goldengate_qos_add_static_profile(lchip, type, &fc_data,
                                                                profile, (void**)&p_sys_fc_profile_new));

        if (profile == 0)
        {
            /* port threshold profile ID, 40*2*8=640 profId */
            for (slice_id = 0; slice_id < 2; slice_id ++)
            {
                for (chan_id = 0; chan_id < 64; chan_id ++)
                {
                    field_val = 0;
                    field_id = DsIgrPortThrdProfId_profIdHigh0_f + (chan_id) % 8;
                    index = slice_id*40 + (chan_id) / 8;
                    cmd = DRV_IOW(DsIgrPortThrdProfId_t, field_id);
                    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &field_val));
                    p_gg_queue_master[lchip]->p_fc_profile[chan_id + slice_id*64] = p_sys_fc_profile_new;
                }
            }
        }
    }

    return CTC_E_NONE;
}


int32
sys_goldengate_qos_fc_add_profile(uint8 lchip, ctc_qos_resrc_fc_t *p_fc)
{
    sys_qos_fc_profile_t fc_data;
    uint8 index = 0;
    uint8 slice_id = 0;
    uint32 profile_id = 0;
    uint32 cmd = 0;

    uint8 type = 0;
    uint8 lchan_id = 0;
    uint8 chan_id = 0;
    sys_qos_fc_profile_t *p_sys_fc_profile = NULL;
    sys_qos_fc_profile_t *p_sys_fc_profile_new = NULL;
    DsIgrPortTcThrdProfile_m ds_igr_port_tc_thrd_profile;
    DsIgrPortThrdProfile_m ds_igr_port_thrd_profile;

    sal_memset(&fc_data, 0, sizeof(fc_data));

    CTC_GLOBAL_PORT_CHECK(p_fc->gport);
    CTC_MAX_VALUE_CHECK(p_fc->priority_class, 7);
    CTC_MAX_VALUE_CHECK(p_fc->xon_thrd, 0x7FFF);
    CTC_MAX_VALUE_CHECK(p_fc->xoff_thrd, 0x7FFF);
    CTC_MAX_VALUE_CHECK(p_fc->drop_thrd, 0x7FFF);
    chan_id = SYS_GET_CHANNEL_ID(lchip, p_fc->gport);
    if ( chan_id >= (SYS_MAX_CHANNEL_ID*2))
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }
    slice_id = SYS_MAP_CHANID_TO_SLICE(chan_id);
    lchan_id = chan_id&0x3F;

    fc_data.xon = p_fc->xon_thrd;
    fc_data.xoff = p_fc->xoff_thrd;
    fc_data.thrd = p_fc->drop_thrd;

    if (p_fc->is_pfc)
    {
        p_sys_fc_profile = p_gg_queue_master[lchip]->p_pfc_profile[chan_id][p_fc->priority_class];
        type = SYS_RESRC_PROFILE_PFC;
    }
    else
    {
        p_sys_fc_profile = p_gg_queue_master[lchip]->p_fc_profile[chan_id];
        type = SYS_RESRC_PROFILE_FC;
    }

    CTC_ERROR_RETURN(_sys_goldengate_qos_add_profile(lchip, type, &fc_data,
                                                             p_sys_fc_profile, (void**)&p_sys_fc_profile_new));

    profile_id = p_sys_fc_profile_new->head.profile_id;
    if (p_fc->is_pfc)
    {
        sal_memset(&ds_igr_port_tc_thrd_profile, 0, sizeof(ds_igr_port_tc_thrd_profile));

        index = (profile_id << 3);
        SetDsIgrPortTcThrdProfile(V, portTcThrd_f, &ds_igr_port_tc_thrd_profile, p_fc->drop_thrd / 16);  /*31k cell*/
        SetDsIgrPortTcThrdProfile(V, portTcXoffThrd_f  , &ds_igr_port_tc_thrd_profile, p_fc->xoff_thrd);
        SetDsIgrPortTcThrdProfile(V, portTcXonThrd_f  , &ds_igr_port_tc_thrd_profile, p_fc->xon_thrd);
        cmd = DRV_IOW(DsIgrPortTcThrdProfile_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_igr_port_tc_thrd_profile));

        index = 96 *slice_id + lchan_id ;
        cmd = DRV_IOW(DsIgrPortTcThrdProfId_t, DsIgrPortTcThrdProfId_profIdHigh0_f + p_fc->priority_class);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &profile_id));


        p_gg_queue_master[lchip]->p_pfc_profile[chan_id][p_fc->priority_class] = p_sys_fc_profile_new;
    }
    else
    {
        sal_memset(&ds_igr_port_thrd_profile, 0, sizeof(ds_igr_port_thrd_profile));
        index = (profile_id << 3);
        SetDsIgrPortThrdProfile(V, portThrd_f, &ds_igr_port_thrd_profile,  p_fc->drop_thrd / 16);
        SetDsIgrPortThrdProfile(V, portXoffThrd_f  , &ds_igr_port_thrd_profile, p_fc->xoff_thrd);
        SetDsIgrPortThrdProfile(V, portXonThrd_f  , &ds_igr_port_thrd_profile, p_fc->xon_thrd);
        cmd = DRV_IOW(DsIgrPortThrdProfile_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_igr_port_thrd_profile));

        index = slice_id*40 + (lchan_id) / 8;
        cmd = DRV_IOW(DsIgrPortThrdProfId_t,  DsIgrPortThrdProfId_profIdHigh0_f + (lchan_id) % 8);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &profile_id));

        p_gg_queue_master[lchip]->p_fc_profile[chan_id] = p_sys_fc_profile_new;
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_qos_fc_get_profile(uint8 lchip, ctc_qos_resrc_fc_t *p_fc)
{
    uint8 chan_id = 0;
    sys_qos_fc_profile_t *p_sys_fc_profile = NULL;

    CTC_GLOBAL_PORT_CHECK(p_fc->gport);
    CTC_MAX_VALUE_CHECK(p_fc->priority_class, 7);
    chan_id = SYS_GET_CHANNEL_ID(lchip, p_fc->gport);
    if ( chan_id >= (SYS_MAX_CHANNEL_ID*2))
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }

    if (p_fc->is_pfc)
    {
        p_sys_fc_profile = p_gg_queue_master[lchip]->p_pfc_profile[chan_id][p_fc->priority_class];
    }
    else
    {
        p_sys_fc_profile = p_gg_queue_master[lchip]->p_fc_profile[chan_id];
    }
    p_fc->xon_thrd = p_sys_fc_profile->xon;
    p_fc->xoff_thrd = p_sys_fc_profile->xoff;
    p_fc->drop_thrd = p_sys_fc_profile->thrd;

    return CTC_E_NONE;
}

STATIC uint32
_sys_goldengate_queue_drop_hash_make_profile(sys_queue_drop_profile_t* p_prof)
{

    uint8* data= (uint8*)(p_prof) + 4;
    uint8   length = sizeof(sys_queue_drop_profile_t) - 4;

    return ctc_hash_caculate(length, data);

    return CTC_E_NONE;
}

/**
 @brief Queue drop hash comparison hook.
*/
STATIC bool
_sys_goldengate_queue_drop_hash_cmp_profile(sys_queue_drop_profile_t* p_prof1,
                                           sys_queue_drop_profile_t* p_prof2)
{

    SYS_QUEUE_DBG_FUNC();

    if (!p_prof1 || !p_prof2)
    {
        return FALSE;
    }

    if (!sal_memcmp((uint8*)(p_prof1) + 4, (uint8*)(p_prof2) + 4, sizeof(sys_queue_drop_profile_t) - 4 ))
    {
        return TRUE;
    }

    return FALSE;
}


STATIC int32
_sys_goldengate_queue_drop_write_profile_to_asic(uint8 lchip,
                                             sys_queue_node_t* p_sys_queue,
                                             sys_queue_drop_profile_t* p_sys_profile)
{
    uint32 cmd = 0;
    uint32 sc = 0;
    uint8  level_num = 0;
    uint8  cng_level = 0;
    uint8  index = 0;

    DsQueThrdProfile_m ds_que_thrd_profile;
    SYS_QUEUE_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_sys_profile);

    /* get queue sc */
    cmd = DRV_IOR(DsEgrResrcCtl_t, DsEgrResrcCtl_mappedSc_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_sys_queue->queue_id, cmd, &sc));

    /* config db */
    level_num = p_gg_queue_master[lchip]->egs_pool[sc].egs_congest_level_num;
    if (level_num > 0)
    {
        for (cng_level = 0; cng_level < level_num; cng_level++)
        {
            sal_memset(&ds_que_thrd_profile, 0, sizeof(DsQueThrdProfile_m));
            SetDsQueThrdProfile(V,wredMaxThrd0_f, &ds_que_thrd_profile, p_sys_profile->profile[cng_level].wred_max_thrd0);
            SetDsQueThrdProfile(V,wredMaxThrd1_f, &ds_que_thrd_profile, p_sys_profile->profile[cng_level].wred_max_thrd1);
            SetDsQueThrdProfile(V,wredMaxThrd2_f, &ds_que_thrd_profile, p_sys_profile->profile[cng_level].wred_max_thrd2);
            SetDsQueThrdProfile(V,wredMaxThrd3_f, &ds_que_thrd_profile, p_sys_profile->profile[cng_level].wred_max_thrd3);
            SetDsQueThrdProfile(V,ecnMarkThrd_f , &ds_que_thrd_profile, p_sys_profile->profile[cng_level].ecn_mark_thrd);

            SetDsQueThrdProfile(V, factor0_f , &ds_que_thrd_profile, p_sys_profile->profile[cng_level].factor0);
            SetDsQueThrdProfile(V, factor1_f , &ds_que_thrd_profile, p_sys_profile->profile[cng_level].factor1);
            SetDsQueThrdProfile(V, factor2_f , &ds_que_thrd_profile, p_sys_profile->profile[cng_level].factor2);
            SetDsQueThrdProfile(V, factor3_f , &ds_que_thrd_profile, p_sys_profile->profile[cng_level].factor3);

            cmd = DRV_IOW(DsQueThrdProfile_t, DRV_ENTRY_FLAG);
            index = (p_sys_profile->profile_id << 3) + cng_level + (8-level_num);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_que_thrd_profile));
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_queue_drop_write_asic(uint8 lchip,
                                     uint8  wred_mode,
                                     uint16 queue_id,
                                     uint16 profile_id,
                                     uint8 ecn_en)
{
    uint32 cmd = 0;
    DsEgrResrcCtl_m ds_egr_resrc_ctl;
    RaQueThrdProfId_m que_thrd_prof_id;

    SYS_QUEUE_DBG_FUNC();

    sal_memset(&ds_egr_resrc_ctl, 0, sizeof(DsEgrResrcCtl_m));
    sal_memset(&que_thrd_prof_id, 0, sizeof(que_thrd_prof_id));

    /*Current code,minProfile only use profile0, so queThrdProfType_f =  QueThrdprofile_id*/
    cmd = DRV_IOR(RaQueThrdProfId_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, profile_id, cmd, &que_thrd_prof_id));
    SetRaQueThrdProfId(V, queThrdProfIdHigh_f, &que_thrd_prof_id, profile_id);
    SetRaQueThrdProfId(V, queThrdProfIdLow_f , &que_thrd_prof_id, 0);
    cmd = DRV_IOW(RaQueThrdProfId_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, profile_id, cmd, &que_thrd_prof_id));

    cmd = DRV_IOR(DsEgrResrcCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, queue_id, cmd, &ds_egr_resrc_ctl));
    SetDsEgrResrcCtl(V, ecnLocalEn_f, &ds_egr_resrc_ctl,ecn_en);
    SetDsEgrResrcCtl(V, queThrdProfType_f, &ds_egr_resrc_ctl, profile_id);
    SetDsEgrResrcCtl(V, wredDropMode_f , &ds_egr_resrc_ctl,wred_mode);
    cmd = DRV_IOW(DsEgrResrcCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, queue_id, cmd, &ds_egr_resrc_ctl));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_queue_drop_remove_profile(uint8 lchip, uint8 type,
                                         sys_queue_drop_profile_t* p_sys_profile_old)
{
    sys_queue_drop_profile_t* p_sys_profile_find = NULL;
    ctc_spool_t* p_profile_pool               = NULL;
    int32 ret            = 0;
    uint16 profile_id    = 0;

    SYS_QUEUE_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_sys_profile_old);

    p_profile_pool = p_gg_queue_master[lchip]->p_drop_profile_pool;

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
        CTC_ERROR_RETURN(_sys_goldengate_qos_profile_free_offset(lchip, type, profile_id));
        mem_free(p_sys_profile_find);
    }

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_queue_drop_add_profile(uint8 lchip, ctc_qos_drop_array drop_arraw,
                                      sys_queue_node_t* p_sys_queue,
                                      sys_queue_drop_profile_t** pp_sys_profile_get)
{

    ctc_spool_t* p_profile_pool    = NULL;
    sys_queue_drop_profile_t* p_sys_profile_old = NULL;
    sys_queue_drop_profile_t* p_sys_profile_new = NULL;
    int32 ret            = 0;
    uint16 old_profile_id = 0;
    uint16 profile_id    = 0;
    uint32 sc = 0;
    uint8  level_num = 0;
    uint8  cng_level = 0;
    uint32 cmd = 0;

    SYS_QUEUE_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_sys_queue);
    CTC_PTR_VALID_CHECK(pp_sys_profile_get);

    p_sys_profile_new = mem_malloc(MEM_QUEUE_MODULE, sizeof(sys_queue_drop_profile_t));
    if (NULL == p_sys_profile_new)
    {
        return CTC_E_NO_MEMORY;
    }
    sal_memset(p_sys_profile_new, 0, sizeof(sys_queue_drop_profile_t));

    /* get queue sc */
    cmd = DRV_IOR(DsEgrResrcCtl_t, DsEgrResrcCtl_mappedSc_f);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, p_sys_queue->queue_id, cmd, &sc), ret, exit);

    /* config db */
    level_num = p_gg_queue_master[lchip]->egs_pool[sc].egs_congest_level_num;
    if (level_num > 0)
    {
        for (cng_level = 0; cng_level < level_num; cng_level++)
        {
            p_sys_profile_new->profile[cng_level].wred_max_thrd0 = drop_arraw[cng_level].drop.max_th[0];
            p_sys_profile_new->profile[cng_level].wred_max_thrd1 = drop_arraw[cng_level].drop.max_th[1];
            p_sys_profile_new->profile[cng_level].wred_max_thrd2 = drop_arraw[cng_level].drop.max_th[2];
            p_sys_profile_new->profile[cng_level].wred_max_thrd3 = drop_arraw[cng_level].drop.max_th[3];

            p_sys_profile_new->profile[cng_level].factor0 = drop_arraw[cng_level].drop.drop_prob[0];
            p_sys_profile_new->profile[cng_level].factor1 = drop_arraw[cng_level].drop.drop_prob[1];
            p_sys_profile_new->profile[cng_level].factor2 = drop_arraw[cng_level].drop.drop_prob[2];
            p_sys_profile_new->profile[cng_level].factor3 = drop_arraw[cng_level].drop.drop_prob[3];

            p_sys_profile_new->profile[cng_level].ecn_mark_thrd = drop_arraw[cng_level].drop.ecn_mark_th;
        }
    }
    else
    {
        ret = CTC_E_UNEXPECT;
        goto exit;
    }

    p_sys_profile_old = p_sys_queue->p_drop_profile;
    p_profile_pool = p_gg_queue_master[lchip]->p_drop_profile_pool;

    /*if use new date to replace old data, profile_id will not change*/
    if(p_sys_profile_old)
    {
        if(TRUE == _sys_goldengate_queue_drop_hash_cmp_profile(p_sys_profile_old, p_sys_profile_new))
        {
            *pp_sys_profile_get = p_sys_profile_old;
            ret = CTC_E_NONE;
            goto exit;
        }
        old_profile_id = p_sys_profile_old->profile_id;
        p_sys_profile_new->profile_id = old_profile_id;
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
        return CTC_E_SPOOL_ADD_UPDATE_FAILED;

    }
    else
    {
        if (ret == CTC_SPOOL_E_OPERATE_MEMORY)
        {
            ret = _sys_goldengate_qos_profile_alloc_offset(lchip, SYS_RESRC_PROFILE_QUEUE, &profile_id);
            if (ret < 0)
            {
                ctc_spool_remove(p_profile_pool, p_sys_profile_new, NULL);
                goto exit;
            }

            (*pp_sys_profile_get)->profile_id = profile_id;

        }
    }

    /*key is found, so there is an old ad need to be deleted.*/
    /*if profile_id not change, no need to delete old ad(old ad is replaced by new)*/
    if (p_sys_profile_old && (*pp_sys_profile_get)->profile_id != old_profile_id)
    {
        CTC_ERROR_GOTO(_sys_goldengate_queue_drop_remove_profile(lchip, SYS_RESRC_PROFILE_QUEUE, p_sys_profile_old), ret, exit);
    }

    p_sys_queue->p_drop_profile = *pp_sys_profile_get;


    return CTC_E_NONE;

exit:
    mem_free(p_sys_profile_new);
    return ret;
}
STATIC int32
_sys_goldengate_queue_get_queue_drop(uint8 lchip, sys_queue_node_t* p_sys_queue_node, ctc_qos_drop_array p_drop, uint16 queue_id)
{
    uint8 profile_id = 0;
    uint32 cmd = 0;
    uint8 mode = 0;
    uint32 sc = 0;
    uint8  level_num = 0;
    uint8 cng_level = 0;
    uint8 tmpprofile_id = 0;
    DsQueThrdProfile_m ds_que_thrd_profile;
    DsEgrResrcCtl_m ds_egr_resrc_ctl;

    sal_memset(&ds_que_thrd_profile, 0, sizeof(DsQueThrdProfile_m));
    sal_memset(&ds_egr_resrc_ctl, 0, sizeof(DsEgrResrcCtl_m));

    cmd = DRV_IOR(DsEgrResrcCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, queue_id, cmd, &ds_egr_resrc_ctl));
    mode = GetDsEgrResrcCtl(V, wredDropMode_f, &ds_egr_resrc_ctl);
    sc = GetDsEgrResrcCtl(V, mappedSc_f, &ds_egr_resrc_ctl);

    if (p_sys_queue_node->p_drop_profile)
    {
        profile_id = (p_sys_queue_node->p_drop_profile->profile_id << 3);
    }
    else
    {
        profile_id = (p_gg_queue_master[lchip]->egs_pool[sc].default_profile_id << 3);
    }

    cmd = DRV_IOR(DsQueThrdProfile_t, DRV_ENTRY_FLAG);
    level_num = p_gg_queue_master[lchip]->egs_pool[sc].egs_congest_level_num;
    if(level_num > 0)
    {
        for (cng_level = 0; cng_level < 8; cng_level++)
        {
            tmpprofile_id = profile_id + cng_level + (8-level_num);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip,
                                       tmpprofile_id,
                                       cmd,
                                       &ds_que_thrd_profile));
            p_drop[cng_level].drop.mode = mode;
            p_drop[cng_level].drop.max_th[0] = GetDsQueThrdProfile(V, wredMaxThrd0_f, &ds_que_thrd_profile);
            p_drop[cng_level].drop.max_th[1] = GetDsQueThrdProfile(V, wredMaxThrd1_f, &ds_que_thrd_profile);
            p_drop[cng_level].drop.max_th[2] = GetDsQueThrdProfile(V, wredMaxThrd2_f, &ds_que_thrd_profile);
            p_drop[cng_level].drop.max_th[3] = GetDsQueThrdProfile(V, wredMaxThrd3_f, &ds_que_thrd_profile);

            p_drop[cng_level].drop.drop_prob[0] = GetDsQueThrdProfile(V, factor0_f, &ds_que_thrd_profile);
            p_drop[cng_level].drop.drop_prob[1] = GetDsQueThrdProfile(V, factor1_f, &ds_que_thrd_profile);
            p_drop[cng_level].drop.drop_prob[2] = GetDsQueThrdProfile(V, factor2_f, &ds_que_thrd_profile);
            p_drop[cng_level].drop.drop_prob[3] = GetDsQueThrdProfile(V, factor3_f, &ds_que_thrd_profile);

            p_drop[cng_level].drop.ecn_mark_th = GetDsQueThrdProfile(V, ecnMarkThrd_f, &ds_que_thrd_profile);
        }
    }
    return CTC_E_NONE;
}
STATIC int32
_sys_goldengate_queue_drop_add_static_profile(uint8 lchip, ctc_qos_global_cfg_t* p_cfg)
{
    sys_queue_drop_profile_t* p_sys_profile_new = NULL;
    DsQueThrdProfile_m ds_que_thrd_profile;
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
        level_num = p_gg_queue_master[lchip]->egs_pool[sc].egs_congest_level_num;
        profile_index = p_gg_queue_master[lchip]->egs_pool[sc].default_profile_id;

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

                sal_memset(&ds_que_thrd_profile, 0, sizeof(DsQueThrdProfile_m));
                SetDsQueThrdProfile(V,wredMaxThrd0_f, &ds_que_thrd_profile, p_cfg->resrc_pool.drop_profile[sc].queue_drop[cng_level].max_th[0]);
                SetDsQueThrdProfile(V,wredMaxThrd1_f, &ds_que_thrd_profile, p_cfg->resrc_pool.drop_profile[sc].queue_drop[cng_level].max_th[1]);
                SetDsQueThrdProfile(V,wredMaxThrd2_f, &ds_que_thrd_profile, p_cfg->resrc_pool.drop_profile[sc].queue_drop[cng_level].max_th[2]);
                SetDsQueThrdProfile(V,wredMaxThrd3_f, &ds_que_thrd_profile, p_cfg->resrc_pool.drop_profile[sc].queue_drop[cng_level].max_th[3]);
                SetDsQueThrdProfile(V,ecnMarkThrd_f , &ds_que_thrd_profile, 0x3FFF);

                cmd = DRV_IOW(DsQueThrdProfile_t, DRV_ENTRY_FLAG);
                index = (profile_index << 3) + cng_level + (8-level_num);
                ret = DRV_IOCTL(lchip, index, cmd, &ds_que_thrd_profile);
                if (ret < 0)
                {
                    mem_free(p_sys_profile_new);
                    return ret;
                }

                p_sys_profile_new->profile_id = profile_index;
                p_sys_profile_new->profile[cng_level].wred_max_thrd0 = p_cfg->resrc_pool.drop_profile[sc].queue_drop[cng_level].max_th[0];
                p_sys_profile_new->profile[cng_level].wred_max_thrd1 = p_cfg->resrc_pool.drop_profile[sc].queue_drop[cng_level].max_th[1];
                p_sys_profile_new->profile[cng_level].wred_max_thrd2 = p_cfg->resrc_pool.drop_profile[sc].queue_drop[cng_level].max_th[2];
                p_sys_profile_new->profile[cng_level].wred_max_thrd3 = p_cfg->resrc_pool.drop_profile[sc].queue_drop[cng_level].max_th[3];
            }

            ret = ctc_spool_static_add(p_gg_queue_master[lchip]->p_drop_profile_pool, p_sys_profile_new);
            if (ret != CTC_SPOOL_E_OPERATE_MEMORY)
            {
                mem_free(p_sys_profile_new);
                return ret;
            }
        }
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_queue_set_drop(uint8 lchip, ctc_qos_drop_t* p_drop)
{
    uint16 queue_id = 0;
    sys_queue_drop_profile_t* p_sys_profile_new = NULL;
	ctc_qos_queue_drop_t *p_queue_drop = &p_drop->drop;
    sys_queue_node_t* p_sys_queue_node = NULL;
    ctc_qos_drop_array drop_array;
    uint8 i = 0;

    SYS_QUEUE_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_drop);

    sal_memset(&drop_array, 0, sizeof(drop_array));

    /*get queue_id*/
    SYS_QOS_QUEUE_LOCK(lchip);
    CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(sys_goldengate_queue_get_queue_id(lchip,
                                                       &p_drop->queue,
                                                       &queue_id));
    p_sys_queue_node = ctc_vector_get(p_gg_queue_master[lchip]->queue_vec,queue_id);
    if (NULL == p_sys_queue_node)
    {
        SYS_QOS_QUEUE_UNLOCK(lchip);
        return CTC_E_ENTRY_NOT_EXIST;
    }

    for (i = 0; i < 4; i++)
    {
        CTC_MAX_VALUE_CHECK_QOS_QUEUE_UNLOCK(p_queue_drop->max_th[i],    SYS_MAX_DROP_THRD);
        CTC_MAX_VALUE_CHECK_QOS_QUEUE_UNLOCK(p_queue_drop->drop_prob[i], SYS_MAX_DROP_PROB);
    }

    CTC_MAX_VALUE_CHECK_QOS_QUEUE_UNLOCK(p_queue_drop->ecn_mark_th,    SYS_MAX_DROP_THRD)

    for (i = 0; i < CTC_RESRC_MAX_CONGEST_LEVEL_NUM; i ++)
    {
        sal_memcpy(&drop_array[i].drop, p_queue_drop, sizeof(ctc_qos_queue_drop_t));
    }

    /*add drop prof*/
    CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(_sys_goldengate_queue_drop_add_profile(lchip, drop_array,
                                                p_sys_queue_node,
                                                &p_sys_profile_new));

    /*write drop prof*/
    CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(_sys_goldengate_queue_drop_write_profile_to_asic(lchip,
                                                       p_sys_queue_node,
                                                       p_sys_profile_new));

    /*write drop ctl and count*/
    CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(_sys_goldengate_queue_drop_write_asic(lchip,
                                              (p_queue_drop->mode ==CTC_QUEUE_DROP_WRED) ,
                                               queue_id,
                                               p_sys_profile_new->profile_id,
                                               p_queue_drop->ecn_mark_th?1:0));


    SYS_QOS_QUEUE_UNLOCK(lchip);

    return CTC_E_NONE;
}

int32
sys_goldengate_queue_get_drop(uint8 lchip, ctc_qos_drop_t* p_drop)
{
    uint16 queue_id = 0;
    sys_queue_node_t* p_sys_queue_node = NULL;
    DsQueThrdProfile_m ds_que_thrd_profile;
    ctc_qos_drop_array drop_array;

    SYS_QUEUE_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_drop);
    SYS_QOS_QUEUE_LOCK(lchip);

    sal_memset(&ds_que_thrd_profile, 0, sizeof(DsQueThrdProfile_m));
    sal_memset(&drop_array, 0, sizeof(drop_array));

    /*get queue_id*/
    CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(sys_goldengate_queue_get_queue_id(lchip,
                                                       &p_drop->queue,
                                                       &queue_id));

	p_sys_queue_node = ctc_vector_get(p_gg_queue_master[lchip]->queue_vec,queue_id);
    if (NULL == p_sys_queue_node)
    {
        SYS_QOS_QUEUE_UNLOCK(lchip);
        return CTC_E_ENTRY_NOT_EXIST;
    }
    CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(_sys_goldengate_queue_get_queue_drop(lchip, p_sys_queue_node, drop_array, queue_id));
    sal_memcpy(&p_drop->drop, &drop_array[0].drop, sizeof(ctc_qos_queue_drop_t));
    SYS_QOS_QUEUE_UNLOCK(lchip);

    return CTC_E_NONE;
}

int32
sys_goldengate_queue_set_queue_resrc(uint8 lchip,
                                      uint16 queue_id,
                                      uint8 pool)
{
    uint32 cmd;
    DsEgrResrcCtl_m ds_egr_resrc_ctl;
    RaQueThrdProfId_m que_thrd_prof_id;
    uint8 profile_id;

    sal_memset(&ds_egr_resrc_ctl, 0, sizeof(DsEgrResrcCtl_m));

    profile_id = p_gg_queue_master[lchip]->egs_pool[pool].default_profile_id;

    cmd = DRV_IOR(DsEgrResrcCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, queue_id, cmd, &ds_egr_resrc_ctl));
    SetDsEgrResrcCtl(V, mappedSc_f , &ds_egr_resrc_ctl, pool);
    if (4 == p_gg_queue_master[lchip]->queue_num_per_chanel)
    {
        SetDsEgrResrcCtl(V, mappedTc_f, &ds_egr_resrc_ctl, queue_id % 4);
    }
    else
    {
        SetDsEgrResrcCtl(V, mappedTc_f, &ds_egr_resrc_ctl, queue_id % 8);
    }
    SetDsEgrResrcCtl(V, ecnLocalEn_f , &ds_egr_resrc_ctl, 0);
    SetDsEgrResrcCtl(V, queThrdProfType_f, &ds_egr_resrc_ctl,profile_id);
    SetDsEgrResrcCtl(V, wredDropMode_f , &ds_egr_resrc_ctl, 0); /* default wtd mode */
    cmd = DRV_IOW(DsEgrResrcCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, queue_id, cmd, &ds_egr_resrc_ctl));

    SetRaQueThrdProfId(V, queMinProfId_f , &que_thrd_prof_id, 0);
    SetRaQueThrdProfId(V, queThrdProfIdHigh_f, &que_thrd_prof_id, profile_id);
    SetRaQueThrdProfId(V, queThrdProfIdLow_f , &que_thrd_prof_id, 0);
    cmd = DRV_IOW(RaQueThrdProfId_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, profile_id, cmd, &que_thrd_prof_id));

    return CTC_E_NONE;
}

int32
sys_goldengate_queue_get_queue_resrc(uint8 lchip,
                                      uint16 queue_id,
                                      uint8 *pool)
{
    uint32 cmd = 0;
    uint32 value = 0;

    cmd = DRV_IOR(DsEgrResrcCtl_t, DsEgrResrcCtl_mappedSc_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, queue_id, cmd, &value));
    *pool = value;

    return CTC_E_NONE;
}

int32
sys_goldengate_queue_set_egs_pool_classify(uint8 lchip, uint8 queue_id, uint8 pool, uint16 port_id, uint16 port_num)
{

    uint8  gchip                   = 0;
    uint8  slice_id                = 0;
    uint16 lport                   = 0;
    uint16 index                   = 0;
    uint16 tmp_queue_id            = 0;
    ctc_qos_queue_id_t queue;
    DsEgrResrcCtl_m ds_egr_resrc_ctl;

    SYS_QUEUE_DBG_FUNC();

    sal_memset(&queue, 0, sizeof(queue));
    sal_memset(&ds_egr_resrc_ctl, 0, sizeof(DsEgrResrcCtl_m));

    if (p_gg_queue_master[lchip]->egs_pool[pool].egs_congest_level_num == 0)   /* the sc is not exist */
    {
        pool = CTC_QOS_EGS_RESRC_DEFAULT_POOL;
    }

    CTC_ERROR_RETURN(sys_goldengate_get_gchip_id(lchip, &gchip));
    for (slice_id = 0; slice_id < 2; slice_id ++)
    {
        for (index = port_id; index < (port_num + port_id); index ++)
        {
            lport = index + slice_id * 256;
            queue.queue_type = CTC_QUEUE_TYPE_NETWORK_EGRESS;
            queue.queue_id = queue_id;
            queue.gport = SYS_MAP_DRV_LPORT_TO_CTC_GPORT(gchip, lport);
            CTC_ERROR_RETURN(sys_goldengate_queue_get_queue_id(lchip, &queue, &tmp_queue_id));
            CTC_ERROR_RETURN(sys_goldengate_queue_set_queue_resrc(lchip, tmp_queue_id, pool));
        }
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_queue_set_cpu_queue_egs_pool_classify(uint8 lchip, uint16 reason_id, uint8 pool)
{
    uint16 queue_id                = 0;
    uint8  queue_offset            = 0;
    uint8  queue_offset_num        = 0;
    ctc_qos_queue_id_t queue;
    DsEgrResrcCtl_m ds_egr_resrc_ctl;

    SYS_QUEUE_DBG_FUNC();

    sal_memset(&queue, 0, sizeof(queue));
    sal_memset(&ds_egr_resrc_ctl, 0, sizeof(DsEgrResrcCtl_m));

    if (p_gg_queue_master[lchip]->egs_pool[pool].egs_congest_level_num == 0)   /* the sc is not exist */
    {
        pool = CTC_QOS_EGS_RESRC_DEFAULT_POOL;
    }

    queue.cpu_reason = reason_id;
    queue.queue_type = CTC_QUEUE_TYPE_EXCP_CPU;
    sys_goldengate_queue_get_queue_id(lchip, &queue, &queue_id);

    queue_offset_num = (reason_id == CTC_PKT_CPU_REASON_C2C_PKT) ? 16 : 1;
    for (queue_offset = 0; queue_offset < queue_offset_num; queue_offset++)
    {
        CTC_ERROR_RETURN(sys_goldengate_queue_set_queue_resrc(lchip, (queue_id + queue_offset), pool));
    }

    return CTC_E_NONE;
}


int32
sys_goldengate_queue_set_port_min(uint8 lchip, uint16 gport, uint16 thrd)
{
    uint8  profile_id              = 0;
    uint8  find                    = 0;

    uint8  slice_id                = 0;
    uint8  tc                      = 0;
    uint16 lport                   = 0;
    uint16 index                   = 0;
    uint32 cmd                     = 0;
    uint32 field_id                = 0;
    uint32 field_value             = 0;

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);

    if (lport % 256 > 96) /* only support 96 port per slice */
    {
        return CTC_E_INVALID_PARAM;
    }

    /* total 8 profile */
    for (index = 0; index < SYS_RESRC_IGS_MAX_PORT_MIN_PROFILE; index++)
    {
        if (p_gg_queue_master[lchip]->igs_port_min[index].ref)
        {
            if (p_gg_queue_master[lchip]->igs_port_min[index].min == thrd)
            {
                profile_id = index;
                find = 1;       /* exist */
                if (index != 0)
                {
                    p_gg_queue_master[lchip]->igs_port_min[index].ref++;
                }
                SYS_QUEUE_DBG_INFO("use old min profile id = %d\n", profile_id);

                break;
            }
        }
    }

    if (!find)
    {
        /* find a free profile */
        for (index = 1; index < SYS_RESRC_IGS_MAX_PORT_MIN_PROFILE; index++)
        {
            if (p_gg_queue_master[lchip]->igs_port_min[index].ref == 0)
            {
                p_gg_queue_master[lchip]->igs_port_min[index].ref++;
                p_gg_queue_master[lchip]->igs_port_min[index].min = thrd;
                profile_id = index;
                /* write profile to asic */
                field_value = thrd;
                cmd = DRV_IOW(IgrPortTcMinProfile_t, IgrPortTcMinProfile_g_0_portTcMin_f + index);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
                SYS_QUEUE_DBG_INFO("new min profile id = %d\n", profile_id);

                break;
            }
        }

        if (index == SYS_RESRC_IGS_MAX_PORT_MIN_PROFILE)
        {
            return CTC_E_NO_RESOURCE;
        }

    }

    /* get the old profile_id, ref -- */
    slice_id = (lport >= 256) ? 1 : 0;
    index = slice_id *96 + lport % 256;
    cmd = DRV_IOR(DsIgrPortTcMinProfId_t, DsIgrPortTcMinProfId_profId0_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &field_value));
    if ((field_value > 0) && (p_gg_queue_master[lchip]->igs_port_min[field_value].ref > 0))
    {
        p_gg_queue_master[lchip]->igs_port_min[field_value].ref--;
    }

    /* write the new profile_id */
    field_value = profile_id;
    for (tc = 0; tc < 8; tc++)
    {
        field_id = DsIgrPortTcMinProfId_profId0_f + tc;
        cmd = DRV_IOW(DsIgrPortTcMinProfId_t, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &field_value));
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_queue_get_port_min(uint8 lchip, uint16 gport, uint16* thrd)
{
    uint8  slice_id                = 0;
    uint16 lport                   = 0;
    uint16 index                   = 0;
    uint32 cmd                     = 0;
    uint32 field_value             = 0;

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);

    if (lport % 256 > 96) /* only support 96 port per slice */
    {
        return CTC_E_INVALID_PARAM;
    }

    /* get the old profile_id, ref -- */
    slice_id = (lport >= 256) ? 1 : 0;
    index = slice_id *96 + lport % 256;
    cmd = DRV_IOR(DsIgrPortTcMinProfId_t, DsIgrPortTcMinProfId_profId0_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &field_value));
    *thrd = p_gg_queue_master[lchip]->igs_port_min[field_value].min;

    return CTC_E_NONE;
}

int32
sys_goldengate_qos_set_resrc_classify_pool(uint8 lchip, ctc_qos_resrc_classify_pool_t *p_pool)
{
    uint16 queue_id                 = 0;
    uint16 index                    = 0;
    uint32 cmd                      = 0;
    uint32 field_value              = 0;
    uint16 chan_id                  = 0;
    uint16 lchan_id                 = 0;
    uint16 slice_id                 = 0;
    uint8  color                    = 0;

    DsIgrPriToTcMap_m ds_igr_pri_to_tc_map;
    ctc_qos_queue_id_t queue;

    CTC_GLOBAL_PORT_CHECK(p_pool->gport);
    CTC_MAX_VALUE_CHECK(p_pool->pool, 3);
    CTC_MAX_VALUE_CHECK(p_pool->priority, SYS_QOS_CLASS_PRIORITY_MAX);

    if (p_pool->dir == CTC_INGRESS)
    {
        chan_id = SYS_GET_CHANNEL_ID(lchip, p_pool->gport);
        if ( chan_id >= (SYS_MAX_CHANNEL_ID*2))
        {
            return CTC_E_INVALID_LOCAL_PORT;
        }
        slice_id = SYS_MAP_CHANID_TO_SLICE(chan_id);
        lchan_id = chan_id&0x3F;

        sal_memset(&ds_igr_pri_to_tc_map, 0, sizeof(DsIgrPriToTcMap_m));

        if (p_pool->pool == CTC_QOS_IGS_RESRC_NON_DROP_POOL)
        {
            for (color = 0; color < 4; color++)
            {
                index = p_pool->priority << 2 | color;
                cmd = DRV_IOR(DsIgrPriToTcMap_t, DRV_ENTRY_FLAG);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_igr_pri_to_tc_map));
                SetDsIgrPriToTcMap(V, g2_1_sc_f , &ds_igr_pri_to_tc_map, p_pool->pool);
                cmd = DRV_IOW(DsIgrPriToTcMap_t, DRV_ENTRY_FLAG);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_igr_pri_to_tc_map));
            }

            field_value = 1;
        }
        else
        {
            field_value = 0;
        }

        /* all port are mapping profile 0 */
        index = slice_id*12 + lchan_id / 8;
        cmd = DRV_IOW(DsIgrPortToTcProfId_t, DsIgrPortToTcProfId_profId0_f + lchan_id % 8);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &field_value));
    }
    else if (p_pool->dir == CTC_EGRESS)
    {
        if (p_gg_queue_master[lchip]->egs_pool[p_pool->pool].egs_congest_level_num == 0)   /* the sc is not exist */
        {
            return CTC_E_INVALID_PARAM;
        }

        queue.queue_type = CTC_QUEUE_TYPE_NETWORK_EGRESS;
        queue.gport = p_pool->gport;
        if (4 == p_gg_queue_master[lchip]->queue_num_per_chanel)
        {
            queue.queue_id = p_pool->priority/((p_gg_queue_master[lchip]->priority_mode)? 4:16);
        }
        else
        {
            queue.queue_id = p_pool->priority/((p_gg_queue_master[lchip]->priority_mode)? 2:8);
        }
        CTC_ERROR_RETURN(sys_goldengate_queue_get_queue_id(lchip, &queue, &queue_id));
        CTC_ERROR_RETURN(sys_goldengate_queue_set_queue_resrc(lchip, queue_id,  p_pool->pool));
    }
    else
    {
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_qos_get_resrc_classify_pool(uint8 lchip, ctc_qos_resrc_classify_pool_t *p_pool)
{
    uint16 queue_id                 = 0;
    uint16 index                    = 0;
    uint32 cmd                      = 0;
    uint32 field_value              = 0;
    uint16 chan_id                  = 0;
    uint16 lchan_id                 = 0;
    uint16 slice_id                 = 0;
    ctc_qos_queue_id_t queue;

    CTC_GLOBAL_PORT_CHECK(p_pool->gport);
    CTC_MAX_VALUE_CHECK(p_pool->pool, 3);
    CTC_MAX_VALUE_CHECK(p_pool->priority, SYS_QOS_CLASS_PRIORITY_MAX);
    sal_memset(&queue, 0, sizeof(ctc_qos_queue_id_t));

    if (p_pool->dir == CTC_INGRESS)
    {
        chan_id = SYS_GET_CHANNEL_ID(lchip, p_pool->gport);
        if ( chan_id >= (SYS_MAX_CHANNEL_ID*2))
        {
            return CTC_E_INVALID_LOCAL_PORT;
        }
        slice_id = SYS_MAP_CHANID_TO_SLICE(chan_id);
        lchan_id = chan_id&0x3F;

        /* all port are mapping profile 0 */
        index = slice_id*12 + lchan_id / 8;
        cmd = DRV_IOR(DsIgrPortToTcProfId_t, DsIgrPortToTcProfId_profId0_f + lchan_id % 8);
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
        if (4 == p_gg_queue_master[lchip]->queue_num_per_chanel)
        {
            queue.queue_id = p_pool->priority/((p_gg_queue_master[lchip]->priority_mode)? 4:16);
        }
        else
        {
            queue.queue_id = p_pool->priority/((p_gg_queue_master[lchip]->priority_mode)? 2:8);
        }
        CTC_ERROR_RETURN(sys_goldengate_queue_get_queue_id(lchip, &queue, &queue_id));
        CTC_ERROR_RETURN(sys_goldengate_queue_get_queue_resrc(lchip, queue_id,  &p_pool->pool));
        if (p_gg_queue_master[lchip]->egs_pool[p_pool->pool].egs_congest_level_num == 0)   /* the sc is not exist */
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
sys_goldengate_qos_set_resrc_queue_drop(uint8 lchip, ctc_qos_drop_array p_drop)
{
    uint8  tc                       = 0;
    uint16 index                    = 0;
    uint16 queue_id                 = 0;
    sys_queue_drop_profile_t* p_sys_profile_new = NULL;
    sys_queue_node_t* p_sys_queue_node = NULL;

    /* check para */
    for (index = 0; index < CTC_RESRC_MAX_CONGEST_LEVEL_NUM; index++)
    {
        for (tc = 0; tc < 4; tc++)
        {
            CTC_MAX_VALUE_CHECK(p_drop[index].drop.max_th[tc], SYS_MAX_DROP_THRD);
            CTC_MAX_VALUE_CHECK(p_drop[index].drop.drop_prob[tc], SYS_MAX_DROP_PROB);
        }
    }

    /* get queue_id */
    CTC_ERROR_RETURN(sys_goldengate_queue_get_queue_id(lchip, &(p_drop[0].queue), &queue_id));
    p_sys_queue_node = ctc_vector_get(p_gg_queue_master[lchip]->queue_vec, queue_id);
    if (NULL == p_sys_queue_node)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    /* add drop profile */
    CTC_ERROR_RETURN(_sys_goldengate_queue_drop_add_profile(lchip,  p_drop,
                                                            p_sys_queue_node,
                                                            &p_sys_profile_new));

    /* write drop prof to asic */
    CTC_ERROR_RETURN(_sys_goldengate_queue_drop_write_profile_to_asic(lchip,
                                                                      p_sys_queue_node,
                                                                      p_sys_profile_new));

    /* write drop ctl and count */
    CTC_ERROR_RETURN(_sys_goldengate_queue_drop_write_asic(lchip,
                                                           (p_drop[0].drop.mode == CTC_QUEUE_DROP_WRED) ,
                                                           queue_id,
                                                           p_sys_profile_new->profile_id,
                                                           0));

    return CTC_E_NONE;
}

int32
sys_goldengate_qos_get_resrc_queue_drop(uint8 lchip, ctc_qos_drop_array p_drop)
{
    uint16 queue_id                 = 0;
    sys_queue_node_t* p_sys_queue_node = NULL;

    /* get queue_id */
    CTC_ERROR_RETURN(sys_goldengate_queue_get_queue_id(lchip, &(p_drop[0].queue), &queue_id));
    p_sys_queue_node = ctc_vector_get(p_gg_queue_master[lchip]->queue_vec, queue_id);
    if (NULL == p_sys_queue_node)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }
    CTC_ERROR_RETURN(_sys_goldengate_queue_get_queue_drop(lchip, p_sys_queue_node, p_drop, queue_id));
    return CTC_E_NONE;
}
int32
sys_goldengate_queue_set_resrc(uint8 lchip, ctc_qos_resrc_t* p_resrc)
{
    uint32 cmd                      = 0;
    uint32 field_value              = 0;

    SYS_QUEUE_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_resrc);

    SYS_QOS_QUEUE_LOCK(lchip);
    switch (p_resrc->cfg_type)
    {
    case CTC_QOS_RESRC_CFG_POOL_CLASSIFY:
        CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(sys_goldengate_qos_set_resrc_classify_pool(lchip, &p_resrc->u.pool));
        break;

    case CTC_QOS_RESRC_CFG_PORT_MIN:
        CTC_MAX_VALUE_CHECK_QOS_QUEUE_UNLOCK(p_resrc->u.port_min.threshold, 255);
        if (p_resrc->u.port_min.dir == CTC_INGRESS)
        {
            CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(sys_goldengate_queue_set_port_min(lchip, p_resrc->u.port_min.gport,
                                              p_resrc->u.port_min.threshold));
        }
        else if (p_resrc->u.port_min.dir == CTC_EGRESS)
        {
            field_value = p_resrc->u.port_min.threshold;
            cmd = DRV_IOW(QueMinProfile_t, QueMinProfile_queMin0_f);
            CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(DRV_IOCTL(lchip, 0, cmd, &field_value));
        }
        else
        {
            SYS_QOS_QUEUE_UNLOCK(lchip);
            return CTC_E_INVALID_PARAM;
        }
        break;

    case CTC_QOS_RESRC_CFG_QUEUE_DROP:
        CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(sys_goldengate_qos_set_resrc_queue_drop(lchip, p_resrc->u.queue_drop));
        break;

    case CTC_QOS_RESRC_CFG_FLOW_CTL:
        CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(sys_goldengate_qos_fc_add_profile(lchip, &p_resrc->u.flow_ctl));
        break;

    default:
        SYS_QOS_QUEUE_UNLOCK(lchip);
        return CTC_E_INVALID_PARAM;
    }

    SYS_QOS_QUEUE_UNLOCK(lchip);

    return CTC_E_NONE;
}

int32
sys_goldengate_queue_get_resrc(uint8 lchip, ctc_qos_resrc_t* p_resrc)
{
    uint32 cmd                      = 0;
    uint32 field_value              = 0;

    SYS_QUEUE_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_resrc);

    SYS_QOS_QUEUE_LOCK(lchip);
    switch (p_resrc->cfg_type)
    {
    case CTC_QOS_RESRC_CFG_POOL_CLASSIFY:
        CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(sys_goldengate_qos_get_resrc_classify_pool(lchip, &p_resrc->u.pool));
        break;

    case CTC_QOS_RESRC_CFG_PORT_MIN:
        if (p_resrc->u.port_min.dir == CTC_INGRESS)
        {
            CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(sys_goldengate_queue_get_port_min(lchip, p_resrc->u.port_min.gport,
                                              &p_resrc->u.port_min.threshold));
        }
        else if (p_resrc->u.port_min.dir == CTC_EGRESS)
        {
            cmd = DRV_IOR(QueMinProfile_t, QueMinProfile_queMin0_f);
            CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(DRV_IOCTL(lchip, 0, cmd, &field_value));
            p_resrc->u.port_min.threshold = field_value;
        }
        else
        {
            SYS_QOS_QUEUE_UNLOCK(lchip);
            return CTC_E_INVALID_PARAM;
        }
        break;

    case CTC_QOS_RESRC_CFG_QUEUE_DROP:
        CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(sys_goldengate_qos_get_resrc_queue_drop(lchip, p_resrc->u.queue_drop));
        break;

    case CTC_QOS_RESRC_CFG_FLOW_CTL:
        CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(sys_goldengate_qos_fc_get_profile(lchip, &p_resrc->u.flow_ctl));
        break;

    default:
        SYS_QOS_QUEUE_UNLOCK(lchip);
        return CTC_E_INVALID_PARAM;
    }

    SYS_QOS_QUEUE_UNLOCK(lchip);

    return CTC_E_NONE;
}

int32
sys_goldengate_queue_query_pool_stats(uint8 lchip, ctc_qos_resrc_pool_stats_t* p_stats)
{
    uint32 cmd = 0;
    uint32 field_value = 0;
    uint16 queue_id = 0;
    ctc_qos_queue_id_t queue;
    BufStoreTotalResrcInfo_m total_resrc;
    ctc_qos_queue_id_t queue_temp;

    CTC_PTR_VALID_CHECK(p_stats);
    SYS_QUEUE_DBG_FUNC();

    sal_memset(&queue, 0, sizeof(queue));
    sal_memset(&total_resrc, 0, sizeof(total_resrc));

    switch (p_stats->type)
    {
    case CTC_QOS_RESRC_STATS_IGS_POOL_COUNT:
        CTC_MAX_VALUE_CHECK(p_stats->pool, CTC_QOS_IGS_RESRC_NON_DROP_POOL);
        cmd = DRV_IOR(IgrScCnt_t, IgrScCnt_scCnt0Slice0_f+p_stats->pool);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
        p_stats->count = field_value;

        cmd = DRV_IOR(IgrScCnt_t, IgrScCnt_scCnt0Slice1_f+p_stats->pool);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
        p_stats->count += field_value;
        break;

    case CTC_QOS_RESRC_STATS_IGS_TOTAL_COUNT:
        cmd = DRV_IOR(BufStoreTotalResrcInfo_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &total_resrc));
        p_stats->count = GetBufStoreTotalResrcInfo(V, igrTotalCntSlice0_f, &total_resrc);
        p_stats->count += GetBufStoreTotalResrcInfo(V, igrTotalCntSlice1_f, &total_resrc);
        break;

    case CTC_QOS_RESRC_STATS_EGS_POOL_COUNT:
        CTC_MAX_VALUE_CHECK(p_stats->pool, CTC_QOS_EGS_RESRC_CONTROL_POOL);
        cmd = DRV_IOR(EgrScCnt_t, EgrScCnt_scCnt0_f+p_stats->pool);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
        p_stats->count = field_value;
        break;

    case CTC_QOS_RESRC_STATS_EGS_TOTAL_COUNT:
        cmd = DRV_IOR(EgrTotalCnt_t, EgrTotalCnt_totalCnt_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
        p_stats->count = field_value;
        break;

    case CTC_QOS_RESRC_STATS_QUEUE_COUNT:
        CTC_MAX_VALUE_CHECK(p_stats->priority, SYS_QOS_CLASS_PRIORITY_MAX);
        SYS_QOS_QUEUE_LOCK(lchip);

        sal_memset(&queue_temp, 0, sizeof(ctc_qos_queue_id_t));
        if (sal_memcmp(&(p_stats->queue), &queue_temp, sizeof(ctc_qos_queue_id_t)))
        {
            sal_memcpy(&queue, &(p_stats->queue), sizeof(ctc_qos_queue_id_t));
        }
        else
        {
            queue.queue_type = CTC_QUEUE_TYPE_NETWORK_EGRESS;
            queue.gport = p_stats->gport;

            if (4 == p_gg_queue_master[lchip]->queue_num_per_chanel)
            {
                queue.queue_id = p_stats->priority / ((p_gg_queue_master[lchip]->priority_mode)? 4:16);
            }
            else
            {
                queue.queue_id = p_stats->priority / ((p_gg_queue_master[lchip]->priority_mode)? 2:8);
            }
        }

        CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(sys_goldengate_queue_get_queue_id(lchip, &queue, &queue_id));

        cmd = DRV_IOR(RaQueCnt_t, RaQueCnt_queInstCnt_f);
        CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(DRV_IOCTL(lchip, queue_id, cmd, &field_value));
        p_stats->count = field_value;
        SYS_QOS_QUEUE_UNLOCK(lchip);
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_qos_resrc_mgr_en(uint8 lchip, uint8 enable)
{
    uint32 cmd;
    uint32 value = 0;

    /* SYS_RESRC_EGS_COND (que_min, total, sc_pkt, sc, tc, port, grp, queue) */
    if (enable)
    {
        value = SYS_RESRC_EGS_COND(0, 0, 1, 0, 1, 1, 1, 0);
    }
    else
    {
        /* must set min guarantee pool size to 0, or will waste the buffer */
        value = SYS_RESRC_EGS_COND(0, 0, 1, 0, 1, 1, 1, 1);
    }
    cmd = DRV_IOW(EgrCondDisProfile_t, EgrCondDisProfile_condDisBmp0_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));

    return CTC_E_NONE;
}

int32
sys_goldengate_queue_set_egs_resrc_guarantee(uint8 lchip, uint8 enable)
{
    uint32 cmd;
    uint32 min;
    QueMinProfile_m que_min_profile;

    LCHIP_CHECK(lchip);

    min = enable ? 12 : 0;

    sal_memset(&que_min_profile, 0, sizeof(QueMinProfile_m));
    cmd = DRV_IOR(QueMinProfile_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &que_min_profile));
    SetQueMinProfile(V,queMin0_f ,&que_min_profile,min);
    cmd = DRV_IOW(QueMinProfile_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &que_min_profile));

    return CTC_E_NONE;
}

int32
sys_goldengate_qos_flow_ctl_profile(uint8 lchip, uint16 gport,
                                    uint8 priority_class,
                                    uint8 pfc_class,
                                    uint8 is_pfc,
                                    uint8 enable)
{
    uint32 cmd = 0;
    uint32 index = 0;
    uint32 field_id = 0;
    uint32 field_val = 0;
    uint16 lport   = 0;
    uint8 chan_id  = 0;
    uint8 lchan_id = 0;
    uint8 slice_id = 0;
	uint32 pri     = 0;
    uint8 mac_id   = 0;
    uint8 lmac_id  = 0;
    sys_datapath_lport_attr_t* port_attr = NULL;

    lport = CTC_MAP_GPORT_TO_LPORT(gport);

    CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, lport, &port_attr));

    if (port_attr->port_type != SYS_DATAPATH_NETWORK_PORT)
    {
        return CTC_E_MAC_NOT_USED;
    }

    slice_id = port_attr->slice_id;
    chan_id = ((slice_id << 6)|port_attr->chan_id);
    mac_id  = port_attr->mac_id;
    lchan_id = chan_id&0x3F;
    lmac_id =  mac_id&0x3F;

    cmd = DRV_IOR(NetTxPauseCurReqTab0_t + slice_id, NetTxPauseCurReqTab0_pri_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lmac_id, cmd, &pri));

    /* 2. modify XON/XOFF threashold */
    if (is_pfc)
    {

        field_val = enable ? p_gg_queue_master[lchip]->p_pfc_profile[chan_id][priority_class]->head.profile_id: 1;
        SYS_QUEUE_DBG_INFO(" profile id = %d\n", field_val);
        field_id = DsIgrPortTcThrdProfId_profIdHigh0_f + priority_class;
        index = 96 *slice_id + lchan_id ;
        cmd = DRV_IOW(DsIgrPortTcThrdProfId_t, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &field_val));

        /*enable port tc drop check*/
        field_val = enable?2:0;  /* default use profile 0 */
        index = slice_id * 96 + lchan_id;
        cmd = DRV_IOW(DsIgrCondDisProfId_t, DsIgrCondDisProfId_profId0_f + priority_class);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &field_val));

        if (0 == enable)
        {
            CTC_BIT_UNSET(pri, pfc_class);
        }
    }
    else
    {
        /*port threshold profile ID*/
        field_val = enable ? p_gg_queue_master[lchip]->p_fc_profile[chan_id]->head.profile_id : 1;
        SYS_QUEUE_DBG_INFO(" profile id = %d\n", field_val);
        field_id = DsIgrPortThrdProfId_profIdHigh0_f + (lchan_id) % 8;
        index = slice_id*40 + (lchan_id) / 8;
        cmd = DRV_IOW(DsIgrPortThrdProfId_t, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &field_val));

        /*enable port drop check*/
        field_val = enable?1:0;  /* default use profile 0 */
        index = slice_id * 96 + lchan_id;
        for (priority_class = 0; priority_class < 8; priority_class++)
        {
            cmd = DRV_IOW(DsIgrCondDisProfId_t, DsIgrCondDisProfId_profId0_f + priority_class);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &field_val));
        }

        if (0 == enable)
        {
            pri = 0;
        }
    }

    if (0 == enable)
    {
        cmd = DRV_IOW(NetTxPauseCurReqTab0_t + slice_id, NetTxPauseCurReqTab0_pri_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, lmac_id, cmd, &pri));
    }

    return CTC_E_NONE;
}



/**
 @brief Initialize default queue drop.
*/
STATIC int32
_sys_goldengate_queue_egs_resrc_mgr_init(uint8 lchip, ctc_qos_global_cfg_t* p_cfg)
{
    uint8 cng_level                = 0;
    uint8 tc                       = 0;
    uint8 sc                       = 0;
    uint8 level_num                = 0;
    uint8 profile_index            = 0;
    uint16 lport                   = 0;
    uint32 index                   = 0;
    uint32 field_val               = 0;
    uint32 field_id                = 0;
    uint32 cmd                     = 0;
    uint32 pool_size[CTC_QOS_EGS_RESRC_POOL_MAX] = {0};
    DsQueThrdProfile_m ds_que_thrd_profile;
    EgrResrcMgrCtl_m egr_resrc_mgr_ctl;
    QMgrEnqCtl_m q_mgr_enq_ctl;
    QMgrEnqDropCtl_m q_mgr_enq_drop_ctl;
    QueMinProfile_m que_min_profile;
    EgrScThrd_m egr_sc_thrd;
    QMgrEnqDropXlateCtl_m qmgr_enqdrop_ctl;

    sal_memset(&ds_que_thrd_profile, 0, sizeof(ds_que_thrd_profile));
    sal_memset(&egr_resrc_mgr_ctl, 0, sizeof(egr_resrc_mgr_ctl));
    sal_memset(&q_mgr_enq_ctl, 0, sizeof(q_mgr_enq_ctl));
    sal_memset(&q_mgr_enq_drop_ctl, 0, sizeof(q_mgr_enq_drop_ctl));
    sal_memset(&que_min_profile, 0, sizeof(que_min_profile));
    sal_memset(&egr_sc_thrd, 0, sizeof(egr_sc_thrd));
    sal_memset(&qmgr_enqdrop_ctl, 0, sizeof(qmgr_enqdrop_ctl));

    /* 1. Buffer assign, total buffer cell is 30K */
    if (p_cfg->resrc_pool.egs_pool_mode == CTC_QOS_RESRC_POOL_DISABLE)
    {
        /* only check sc|total sc */
        pool_size[CTC_QOS_EGS_RESRC_DEFAULT_POOL]   = 29184;    /* 28.5K */
        pool_size[CTC_QOS_EGS_RESRC_NON_DROP_POOL]  = 0;
        pool_size[CTC_QOS_EGS_RESRC_SPAN_POOL]      = 0;
        pool_size[CTC_QOS_EGS_RESRC_CONTROL_POOL]   = 1536;     /* 1.5K */
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
            || (pool_size[CTC_QOS_EGS_RESRC_SPAN_POOL] == 0)
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

    /* 2. configure register fields for egress resource management
    #1. enable egress resource management
    #2. egress resource management is buffer cells - based
    #3. don't drop c2c and critical packets
    */
	SetEgrResrcMgrCtl(V,egrResrcMgrEn_f,&egr_resrc_mgr_ctl,1);
	SetEgrResrcMgrCtl(V,egrTotalThrd_f,&egr_resrc_mgr_ctl,(30*1024-pool_size[CTC_QOS_EGS_RESRC_C2C_POOL])/16);
	SetEgrResrcMgrCtl(V,enC2cPacketChk_f,&egr_resrc_mgr_ctl,1);     /* must disable, or the reserved cells can not be used */
	SetEgrResrcMgrCtl(V,enCriticalPacketChk_f,&egr_resrc_mgr_ctl,1);/* use control pool */
    cmd = DRV_IOW(EgrResrcMgrCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &egr_resrc_mgr_ctl));

    sal_memset(&q_mgr_enq_ctl, 0, sizeof(QMgrEnqCtl_m));
    cmd = DRV_IOR(QMgrEnqCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &q_mgr_enq_ctl));
    SetQMgrEnqCtl(V,basedOnCellCnt_f,&q_mgr_enq_ctl,1);
    SetQMgrEnqCtl(V,bufScanInterval_f,&q_mgr_enq_ctl,1000);
    SetQMgrEnqCtl(V,cellCntAdjEn_f ,&q_mgr_enq_ctl,1);          /* must set to 1, than buffer cells egs:igs=1:1 */
    SetQMgrEnqCtl(V,cpuBasedOnPktNum_f ,&q_mgr_enq_ctl,1);
    SetQMgrEnqCtl(V,cpuPktLenAdjFactor_f,&q_mgr_enq_ctl,10);    /* a packet_len = 1<<10, use the length to do shaping */
    SetQMgrEnqCtl(V,criticalPktForceNoDrop_f ,&q_mgr_enq_ctl,0);
    SetQMgrEnqCtl(V,c2cPktForceNoDrop_f,&q_mgr_enq_ctl,0);
    SetQMgrEnqCtl(V,lowTc_f ,&q_mgr_enq_ctl,4);
    SetQMgrEnqCtl(V,pFabricEn_f  ,&q_mgr_enq_ctl,0);
    cmd = DRV_IOW(QMgrEnqCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &q_mgr_enq_ctl));

    /* 3. threshold config, total :30k cells (30*1024) */
    /* sc threshold */
    SetEgrScThrd(V,scThrd0_f,&egr_sc_thrd,pool_size[CTC_QOS_EGS_RESRC_DEFAULT_POOL]/16);
    SetEgrScThrd(V,scThrd1_f,&egr_sc_thrd,pool_size[CTC_QOS_EGS_RESRC_NON_DROP_POOL]/16);
    SetEgrScThrd(V,scThrd2_f,&egr_sc_thrd,pool_size[CTC_QOS_EGS_RESRC_SPAN_POOL]/16);
    SetEgrScThrd(V,scThrd3_f,&egr_sc_thrd,pool_size[CTC_QOS_EGS_RESRC_CONTROL_POOL]/16);
    cmd = DRV_IOW(EgrScThrd_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &egr_sc_thrd));

    SetQMgrEnqDropXlateCtl(V, rsvCondDisable_f , &qmgr_enqdrop_ctl, 0);
    SetQMgrEnqDropXlateCtl(V, rsvQueThrd_f , &qmgr_enqdrop_ctl, 128);
    SetQMgrEnqDropXlateCtl(V, tcEnBitMap_f , &qmgr_enqdrop_ctl, 0xFF);
    cmd = DRV_IOW(QMgrEnqDropXlateCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &qmgr_enqdrop_ctl));

    /*tc threshold, Disable, alawy be 0 */
    for (tc = 0; tc < 8; tc++)
    {
        field_val = 0;
        field_id = EgrTcThrd_tcThrd0_f + tc;
        cmd = DRV_IOW(EgrTcThrd_t, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    }
    for (tc = 0; tc < 8; tc++)
    {
        field_val = 0;
        field_id = EgrTcThrd_tcThrdLow0_f + tc;
        cmd = DRV_IOW(EgrTcThrd_t, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    }

    /* 4. configure WRED
    #1. global enable wred random drop
    #2. WRED don't care resource management discard conditions
    #3. all non - tcp use drop precedence 0
    */
    sal_memset(&q_mgr_enq_drop_ctl, 0, sizeof(QMgrEnqDropCtl_m));
	SetQMgrEnqDropCtl(V,criticalPktDropPri_f ,&q_mgr_enq_drop_ctl,3);
    SetQMgrEnqDropCtl(V,nonTcpDropPri_f,&q_mgr_enq_drop_ctl,0);
    SetQMgrEnqDropCtl(V,queDropProfExt_f  ,&q_mgr_enq_drop_ctl,0);  /* use congestion level */
    SetQMgrEnqDropCtl(V,wredCareResrcMgr_f ,&q_mgr_enq_drop_ctl,0);
    SetQMgrEnqDropCtl(V,wredModeGlbEn_f  ,&q_mgr_enq_drop_ctl,1);
    SetQMgrEnqDropCtl(V,wredMinThrdShift0_f,&q_mgr_enq_drop_ctl,3); /* min = max >> 3 */
    SetQMgrEnqDropCtl(V,wredMinThrdShift1_f,&q_mgr_enq_drop_ctl,3);
    SetQMgrEnqDropCtl(V,wredMinThrdShift2_f,&q_mgr_enq_drop_ctl,3);
    SetQMgrEnqDropCtl(V,wredMinThrdShift3_f,&q_mgr_enq_drop_ctl,3);

    cmd = DRV_IOW(QMgrEnqDropCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &q_mgr_enq_drop_ctl));

    /* 5. config congestion level to enable self-tuning threshold, disable pktThrd */
    for (sc = 0; sc < 4; sc ++)
    {
        if (p_cfg->resrc_pool.drop_profile[sc].congest_level_num == 0)
        {
            p_cfg->resrc_pool.drop_profile[sc].congest_level_num = 1;
        }
        level_num = p_cfg->resrc_pool.drop_profile[sc].congest_level_num-1;

        for (cng_level = 0; cng_level < 7; cng_level ++)
        {
            field_val = 0xFF;   /* config 0xFF to disable */
            field_id = EgrCongestLevelThrd_pktSc0Thrd0_f + sc*7 + cng_level;
            cmd = DRV_IOW(EgrCongestLevelThrd_t, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        }
    	for (cng_level = 0; cng_level < level_num; cng_level ++)
        {
            field_val = p_cfg->resrc_pool.drop_profile[sc].congest_threshold[cng_level]/64;
            field_id = EgrCongestLevelThrd_sc0Thrd0_f + sc*7 + cng_level+(7-level_num);
            cmd = DRV_IOW(EgrCongestLevelThrd_t, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        }
    }

    /* 6. config default drop profile */
    /* config minimum guarantee profile 0: 12 cells, used for XGE channel queues*/
    cmd = DRV_IOR(QueMinProfile_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &que_min_profile));
    SetQueMinProfile(V,queMin0_f ,&que_min_profile,12);
    cmd = DRV_IOW(QueMinProfile_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &que_min_profile));

    /* queue threshold profile 0 -- for tail-drop purpose */
    profile_index = 0;
    for (sc = 0; sc < 4; sc ++)
    {
        if (p_cfg->resrc_pool.drop_profile[sc].congest_level_num == 0)
        {
            p_cfg->resrc_pool.drop_profile[sc].congest_level_num = 1;
        }
        level_num = p_cfg->resrc_pool.drop_profile[sc].congest_level_num;

        if (pool_size[sc])
        {
            p_gg_queue_master[lchip]->egs_pool[sc].egs_congest_level_num = p_cfg->resrc_pool.drop_profile[sc].congest_level_num;
            p_gg_queue_master[lchip]->egs_pool[sc].default_profile_id = profile_index;
            profile_index ++;
        }
    }

    /* 7. config group threshold profile 0. Now not used, alawy be 0 */
    for (cng_level = 0; cng_level < 8; cng_level++)
    {
        cmd = DRV_IOW(RaGrpDropProfile_t, RaGrpDropProfile_grpDropThrd_f);
        field_val = 0;
	    index = (0 << 3) + cng_level;
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &field_val));
    }

    /* 8. config port threshold profile 0. only used for queue flush, alawy be 0 */
    for (cng_level = 0; cng_level < 8; cng_level++)
    {
        field_val = 0;
		index = (0 << 3) + cng_level;
        cmd = DRV_IOW(RaEgrPortDropProfile_t, RaEgrPortDropProfile_portDropThrd_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &field_val));
    }

    /* 9. confgi egress drop condition profile 0, enable queue|sc|total|min check*/
    if (p_cfg->resrc_pool.egs_pool_mode == CTC_QOS_RESRC_POOL_DISABLE)
    {
        CTC_ERROR_RETURN(sys_goldengate_qos_resrc_mgr_en(lchip, FALSE));
    }
    else
    {
        CTC_ERROR_RETURN(sys_goldengate_qos_resrc_mgr_en(lchip, TRUE));
    }

    /*((que_min<<7)|(total<<6)|(sc_pkt<<5)|(sc<<4)|(tc<<3)|(port<<2)|(grp<<1)|(queue<<0))*/
    /*used for drop channel which port threshold is 0 */
    field_val = SYS_RESRC_EGS_COND(1, 0, 1, 0, 1, 0, 1, 1);
    cmd = DRV_IOW(EgrCondDisProfile_t, EgrCondDisProfile_condDisBmp1_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));


    /* All port use condition profile 0*/
    field_val = 0;
    field_id = RaEgrPortCtl_egrCondDisProfId_f;
    for (lport = 0; lport < 128; lport ++)
    {
        cmd = DRV_IOW(RaEgrPortCtl_t, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));
    }

    return CTC_E_NONE;
}


extern int32
_sys_goldengate_queue_igs_resrc_mgr_init(uint8 lchip, ctc_qos_global_cfg_t* p_cfg)
{
    uint32 cmd                     = 0;
    uint16 lport                   = 0;
    uint8 priority                 = 0;
    uint8 color                    = 0;
    uint8 index                    = 0;
    uint8 tc                       = 0;
    uint8 cng_level                = 0;
    uint8  slice_id                = 0;
    uint32 field_id                = 0;
    uint32 field_val               = 0;
    IgrResrcMgrMiscCtl_m igr_resrc_mgr_misc_ctl;
    DsIgrPriToTcMap_m   ds_igr_pri_to_tc_map;
    BufStoreMiscCtl_m     buf_store_misc_ctl;
    IgrCondDisProfile_m igr_cond_dis_profile;
    IgrPortTcMinProfile_m igr_port_tc_min_profile;

	uint32 localphy_en[4] = {0};
    uint32 pool_size[CTC_QOS_IGS_RESRC_POOL_MAX] = {0};
    BufferStoreResrcIdCtl_m  buffer_store_resrc_id;


    /* 1. Buffer assign, total buffer cell is 30K */
    if (p_cfg->resrc_pool.igs_pool_mode == CTC_QOS_RESRC_POOL_DISABLE)
    {
        pool_size[CTC_QOS_IGS_RESRC_DEFAULT_POOL]   = 26112;
        pool_size[CTC_QOS_IGS_RESRC_NON_DROP_POOL]  = 0;
        pool_size[CTC_QOS_IGS_RESRC_MIN_POOL]       = 1536;     /*128*12*/
        pool_size[CTC_QOS_IGS_RESRC_C2C_POOL]       = 0;
        pool_size[CTC_QOS_IGS_RESRC_ROUND_TRIP_POOL]= 1024;
        pool_size[CTC_QOS_IGS_RESRC_CONTROL_POOL]   = 2048;
    }
    else if (p_cfg->resrc_pool.igs_pool_mode == CTC_QOS_RESRC_POOL_MODE1)
    {
        sal_memcpy(pool_size, p_cfg->resrc_pool.igs_pool_size, sizeof(pool_size));
        if (pool_size[CTC_QOS_IGS_RESRC_NON_DROP_POOL])
        {
            return CTC_E_INVALID_PARAM;
        }
    }
    else if (p_cfg->resrc_pool.igs_pool_mode == CTC_QOS_RESRC_POOL_MODE2)
    {
        sal_memcpy(pool_size, p_cfg->resrc_pool.igs_pool_size, sizeof(pool_size));
        if (pool_size[CTC_QOS_IGS_RESRC_NON_DROP_POOL] == 0)
        {
            return CTC_E_INVALID_PARAM;
        }
    }
    else if (p_cfg->resrc_pool.igs_pool_mode == CTC_QOS_RESRC_POOL_USER_DEFINE)
    {
        sal_memcpy(pool_size, p_cfg->resrc_pool.igs_pool_size, sizeof(pool_size));
    }

    if (pool_size[CTC_QOS_IGS_RESRC_DEFAULT_POOL] == 0)
    {
        return CTC_E_INVALID_PARAM;
    }

    /* 2. enable ingress resource management*/
    cmd = DRV_IOR(BufStoreMiscCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &buf_store_misc_ctl));
    SetBufStoreMiscCtl(V, overLenErrorChkEnable_f , &buf_store_misc_ctl, 1);
    SetBufStoreMiscCtl(V, resrcMgrDisable_f, &buf_store_misc_ctl, 0);
    SetBufStoreMiscCtl(V, underLenErrorChkEnable_f, &buf_store_misc_ctl, 1);
    cmd = DRV_IOW(BufStoreMiscCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &buf_store_misc_ctl));

    /* 3. threshold config, total :30k cells (30*1024) */
    sal_memset(&igr_resrc_mgr_misc_ctl, 0, sizeof(IgrResrcMgrMiscCtl_m));
    SetIgrResrcMgrMiscCtl(V, c2cPacketThrd_f, &igr_resrc_mgr_misc_ctl, pool_size[CTC_QOS_IGS_RESRC_C2C_POOL]);
    SetIgrResrcMgrMiscCtl(V, cellUseAdjEn_f, &igr_resrc_mgr_misc_ctl, 1);
    SetIgrResrcMgrMiscCtl(V, criticalPacketThrd_f  , &igr_resrc_mgr_misc_ctl, pool_size[CTC_QOS_IGS_RESRC_CONTROL_POOL]);
    SetIgrResrcMgrMiscCtl(V, enC2cPacketChk_f, &igr_resrc_mgr_misc_ctl, 0);
    SetIgrResrcMgrMiscCtl(V, enCriticalPacketChk_f , &igr_resrc_mgr_misc_ctl, 0);
    SetIgrResrcMgrMiscCtl(V, noCareC2cPacket_f , &igr_resrc_mgr_misc_ctl, 0);
    SetIgrResrcMgrMiscCtl(V, noCareCriticalPacket_f, &igr_resrc_mgr_misc_ctl,0);
    SetIgrResrcMgrMiscCtl(V, igrTotalThrd_f, &igr_resrc_mgr_misc_ctl,
                            (pool_size[CTC_QOS_IGS_RESRC_DEFAULT_POOL] +
                             pool_size[CTC_QOS_IGS_RESRC_NON_DROP_POOL] +
                             pool_size[CTC_QOS_IGS_RESRC_MIN_POOL])/16);
    cmd = DRV_IOW(IgrResrcMgrMiscCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &igr_resrc_mgr_misc_ctl));

    /* configure default pool threshold */
    field_val = pool_size[CTC_QOS_IGS_RESRC_DEFAULT_POOL]/16;
    cmd = DRV_IOW(IgrScThrd_t, IgrScThrd_g_0_scThrd_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    /* configure non-drop pool threshold */
    field_val = pool_size[CTC_QOS_IGS_RESRC_NON_DROP_POOL]/16;
    cmd = DRV_IOW(IgrScThrd_t, IgrScThrd_g_1_scThrd_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    /* configure tc threshold, must disable check */
   	for (tc = 0; tc < 8; tc++)
    {
        field_val = 0;
        cmd = DRV_IOW(IgrTcThrd_t, IgrTcThrd_g_0_tcThrd_f + tc );
	    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
	}

    /* 4. mapping profiles, only use profile 0: all priority mapped to tc priority/8 and sc 0 */
    sal_memset(&ds_igr_pri_to_tc_map, 0, sizeof(DsIgrPriToTcMap_m));
    for (priority = 0; priority <= SYS_QOS_CLASS_PRIORITY_MAX; priority++)
    {
        for (color = 0; color < 4; color++)
        {
            index = priority << 2 | color;
            SetDsIgrPriToTcMap(V, g1_0_tc_f, &ds_igr_pri_to_tc_map, priority/((p_gg_queue_master[lchip]->priority_mode)? 2:8));    /* only for pfc */
            SetDsIgrPriToTcMap(V, g2_0_sc_f , &ds_igr_pri_to_tc_map, CTC_QOS_IGS_RESRC_DEFAULT_POOL);
            SetDsIgrPriToTcMap(V, g1_1_tc_f, &ds_igr_pri_to_tc_map, priority/((p_gg_queue_master[lchip]->priority_mode)? 2:8));    /* only for pfc */
            SetDsIgrPriToTcMap(V, g2_1_sc_f , &ds_igr_pri_to_tc_map, CTC_QOS_IGS_RESRC_DEFAULT_POOL);
            cmd = DRV_IOW(DsIgrPriToTcMap_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_igr_pri_to_tc_map));
        }
    }

    for (slice_id = 0; slice_id < 2; slice_id++)
    {
        /* all port are mapping profile 0 */
        for (lport = 0; lport < 64; lport++)    /*slice 0*/
        {
            field_val = 0;
            field_id = DsIgrPortToTcProfId_profId0_f + lport % 8;
            index = slice_id*12 + lport / 8;
            cmd = DRV_IOW(DsIgrPortToTcProfId_t, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &field_val));
        }
    }

    /*
    configure port, TC minimum guarantee profile
    profile 0: TcMin = 12
    */
    sal_memset(&igr_port_tc_min_profile, 0, sizeof(IgrPortTcMinProfile_m));
    cmd = DRV_IOR(IgrPortTcMinProfile_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &igr_port_tc_min_profile));
 	SetIgrPortTcMinProfile(V, g_0_portTcMin_f, &igr_port_tc_min_profile, 12);
    cmd = DRV_IOW(IgrPortTcMinProfile_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &igr_port_tc_min_profile));

    /*
    all 10G ports uses minimum guarantee profile 0: TcMin = 12
    all 40G ports uses minimum guarantee profile 1: TcMin = 18
    */
    /* default all ports are 10G Port, precascade */
    for (slice_id = 0; slice_id < 2; slice_id++)
    {
        for (lport = 0; lport < 64; lport++)
        {
            for (tc = 0; tc < 8; tc++)
            {
                field_val = 0;
                field_id = DsIgrPortTcMinProfId_profId0_f + tc;
                index = slice_id *96 + lport;
                cmd = DRV_IOW(DsIgrPortTcMinProfId_t, field_id);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &field_val));
            }
        }
    }



    /* 9. configure SC0 congestion level watermarks, disable ingress congest level */
   	for (cng_level = 0; cng_level < 7; cng_level++)
    {
	   field_val = 0x3FFF/16; /*aways in congest level 0*/
       cmd = DRV_IOW(IgrCongestLevelThrd_t, IgrCongestLevelThrd_sc0ThrdLvl0_f + cng_level );
	   CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    }


    /*SYS_RESRC_IGS_COND(tc_min,total,sc,tc,port,port_tc)*/
    sal_memset(&igr_cond_dis_profile, 0, sizeof(igr_cond_dis_profile));
    field_val = SYS_RESRC_IGS_COND(0, 0, 0, 1, 1, 1);
    SetIgrCondDisProfile(V, g_0_condDisBmp_f, &igr_cond_dis_profile, field_val);
    field_val = SYS_RESRC_IGS_COND(0, 0, 0, 1, 0, 1);
    SetIgrCondDisProfile(V, g_1_condDisBmp_f, &igr_cond_dis_profile, field_val);
    field_val = SYS_RESRC_IGS_COND(0, 0, 0, 1, 1, 0);
    SetIgrCondDisProfile(V, g_2_condDisBmp_f, &igr_cond_dis_profile, field_val);
    cmd = DRV_IOW(IgrCondDisProfile_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &igr_cond_dis_profile));

    /*  config discard condition profile 0, all port,tc use the same condition profile */
    for (slice_id = 0; slice_id < 2; slice_id ++)
    {
        for (lport = 0; lport < 64; lport ++)
        {
            for (tc = 0; tc < 8; tc ++)
            {
                field_val = 0;  /* default use profile 0 */
                field_id = DsIgrCondDisProfId_profId0_f + tc;
                index = slice_id * 96 + lport;
                cmd = DRV_IOW(DsIgrCondDisProfId_t, field_id);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &field_val));
            }
        }
    }

    /* 11. other config */
    /* based on channel to do IRM */
    sal_memset(&buffer_store_resrc_id,0,sizeof(BufferStoreResrcIdCtl_m));
    SetBufferStoreResrcIdCtl(A,resrcIdUseLocalPhyPort_f,&buffer_store_resrc_id,localphy_en);
    cmd = DRV_IOW(BufferStoreResrcIdCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &buffer_store_resrc_id));


    /*drop condition */
    field_val = 0x1D;
    cmd = DRV_IOW(IgrGlbDropCondCtl_t, IgrGlbDropCondCtl_igrGlbDropCondDis_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

	field_val = 0x1E;
	cmd = DRV_IOW(IgrGlbDropCondCtl_t, IgrGlbDropCondCtl_igrGlbTcDropCondDis_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    return CTC_E_NONE;
}

int32
sys_goldengate_queue_set_chan_drop_en(uint8 lchip, uint16 queue_id, uint8 chan_id, bool enable)
{
    uint32 cmd                     = 0;
    uint32 field_val               = 0;
    uint8 lchan_id                 = 0;
    uint8 slice_id                 = 0;
    uint32 bmp[2]                  = {0};
    QMgrChanShapeCtl_m chan_shape_ctl;
    uint32 chan_shp_en[2];
    uint8 super_grp = 0;
    QMgrChanFlushCtl_m q_mgr_chan_flush_ctl;
    slice_id = SYS_MAP_CHANID_TO_SLICE(chan_id);
    lchan_id = (chan_id&0x3F);
    super_grp = queue_id / 8;

    if (enable)
    {
        /*1. Enable Drop incoming channel packets*/
        field_val = 1;
        cmd = DRV_IOW(RaEgrPortCtl_t, RaEgrPortCtl_egrCondDisProfId_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &field_val));

        /*2. Disable channel*/
        cmd = DRV_IOR(QMgrChanShapeCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, slice_id, cmd, &chan_shape_ctl));
        GetQMgrChanShapeCtl(A, chanShpEn_f, &chan_shape_ctl, chan_shp_en);
        p_gg_queue_master[lchip]->store_chan_shp_en = CTC_IS_BIT_SET(chan_shp_en[lchan_id >> 5], (lchan_id&0x1F));
        CTC_BIT_UNSET(chan_shp_en[lchan_id >> 5], (lchan_id&0x1F));
        SetQMgrChanShapeCtl(A, chanShpEn_f, &chan_shape_ctl, chan_shp_en);
        cmd = DRV_IOW(QMgrChanShapeCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, slice_id, cmd, &chan_shape_ctl));

        /*3. Disable queue/grp shaping*/
        field_val = 0;
        cmd = DRV_IOW(RaGrpQueShpCtl_t, RaGrpQueShpCtl_grpShpEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, super_grp, cmd, &field_val));

        cmd = DRV_IOR(RaGrpQueShpCtl_t, RaGrpQueShpCtl_queShpEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, super_grp, cmd, &field_val));
        p_gg_queue_master[lchip]->store_que_shp_en = field_val;
        field_val = 0;
        cmd = DRV_IOW(RaGrpQueShpCtl_t, RaGrpQueShpCtl_queShpEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, super_grp, cmd, &field_val));
        if (16 == p_gg_queue_master[lchip]->queue_num_per_chanel)
        {
            field_val = 0;
            cmd = DRV_IOW(RaGrpQueShpCtl_t, RaGrpQueShpCtl_grpShpEn_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, (super_grp + 1), cmd, &field_val));
            cmd = DRV_IOW(RaGrpQueShpCtl_t, RaGrpQueShpCtl_queShpEn_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, (super_grp + 1), cmd, &field_val));
        }

        /*4. Enable Flush Channel queue packets*/
        cmd = DRV_IOR(QMgrChanFlushCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, slice_id, cmd, &q_mgr_chan_flush_ctl));
        GetQMgrChanFlushCtl(A, chanFlushValid_f, &q_mgr_chan_flush_ctl, bmp);
        CTC_BIT_SET(bmp[lchan_id >> 5], (lchan_id&0x1F));
        SetQMgrChanFlushCtl(A, chanFlushValid_f, &q_mgr_chan_flush_ctl, bmp);
        cmd = DRV_IOW(QMgrChanFlushCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, slice_id, cmd, &q_mgr_chan_flush_ctl));

    }
    else
    {
        /*1. Disable Flush Channel queue packets*/
        cmd = DRV_IOR(QMgrChanFlushCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, slice_id, cmd, &q_mgr_chan_flush_ctl));
        GetQMgrChanFlushCtl(A, chanFlushValid_f, &q_mgr_chan_flush_ctl, bmp);
        CTC_BIT_UNSET(bmp[lchan_id >> 5], (lchan_id&0x1F));
        SetQMgrChanFlushCtl(A, chanFlushValid_f, &q_mgr_chan_flush_ctl, bmp);
        cmd = DRV_IOW(QMgrChanFlushCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, slice_id, cmd, &q_mgr_chan_flush_ctl));

        /*2. Enable channel shaping*/
        if (p_gg_queue_master[lchip]->store_chan_shp_en)
        {
            cmd = DRV_IOR(QMgrChanShapeCtl_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, slice_id, cmd, &chan_shape_ctl));
            GetQMgrChanShapeCtl(A, chanShpEn_f, &chan_shape_ctl, chan_shp_en);
            CTC_BIT_SET(chan_shp_en[lchan_id >> 5], (lchan_id&0x1F));
            SetQMgrChanShapeCtl(A, chanShpEn_f, &chan_shape_ctl, chan_shp_en);
            cmd = DRV_IOW(QMgrChanShapeCtl_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, slice_id, cmd, &chan_shape_ctl));
        }

        /*3. Eanble queue/grp shaping*/
        field_val = 3;
        cmd = DRV_IOW(RaGrpQueShpCtl_t, RaGrpQueShpCtl_grpShpEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, super_grp, cmd, &field_val));

        if (p_gg_queue_master[lchip]->store_que_shp_en)
        {
            field_val = p_gg_queue_master[lchip]->store_que_shp_en;
            cmd = DRV_IOW(RaGrpQueShpCtl_t, RaGrpQueShpCtl_queShpEn_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, super_grp, cmd, &field_val));
        }

        if (16 == p_gg_queue_master[lchip]->queue_num_per_chanel)
        {
            field_val = 1;
            cmd = DRV_IOW(RaGrpQueShpCtl_t, RaGrpQueShpCtl_grpShpEn_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, (super_grp + 1), cmd, &field_val));
            field_val = p_gg_queue_master[lchip]->store_que_shp_en;
            cmd = DRV_IOW(RaGrpQueShpCtl_t, RaGrpQueShpCtl_queShpEn_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, (super_grp + 1), cmd, &field_val));
        }

        /*4. Disable Drop incoming channel packets*/
        field_val = 0;
        cmd = DRV_IOW(RaEgrPortCtl_t, RaEgrPortCtl_egrCondDisProfId_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &field_val));

    }

    return CTC_E_NONE;
}

int32
sys_goldengate_queue_set_port_drop_en(uint8 lchip, uint16 gport, bool enable)
{
    uint8 chan_id                  = 0;
    uint16 queue_id = 0;
    ctc_qos_queue_id_t queue;

    chan_id = SYS_GET_CHANNEL_ID(lchip, gport);
    if (0xff == chan_id)
    {
        return CTC_E_INVALID_PORT;
    }
    sal_memset(&queue, 0, sizeof(queue));

    queue.gport = gport;
    queue.queue_id = 0;
    queue.queue_type = CTC_QUEUE_TYPE_NETWORK_EGRESS;
    SYS_QOS_QUEUE_LOCK(lchip);
    CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(sys_goldengate_queue_get_queue_id(lchip, &queue, &queue_id));

    sys_goldengate_queue_set_chan_drop_en(lchip, queue_id, chan_id, enable);
    SYS_QOS_QUEUE_UNLOCK(lchip);

    return CTC_E_NONE;
}

int32
sys_goldengate_queue_get_port_depth(uint8 lchip, uint16 gport, uint32* p_depth)
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

    cmd = DRV_IOR(RaEgrPortCnt_t, RaEgrPortCnt_portCnt_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &field_val));

    *p_depth = field_val;

    return CTC_E_NONE;
}


int32
sys_goldengate_qos_drop_dump_status(uint8 lchip)
{
    SYS_QUEUE_DBG_DUMP("-------------------------Queue Drop-----------------------\n");
    SYS_QUEUE_DBG_DUMP("%-30s: %d\n", "Total drop profile",p_gg_queue_master[lchip]->p_drop_profile_pool->max_count);
    SYS_QUEUE_DBG_DUMP("%-30s: %d\n","--Used count", p_gg_queue_master[lchip]->p_drop_profile_pool->count);
    SYS_QUEUE_DBG_DUMP("\n");
    SYS_QUEUE_DBG_DUMP("%-30s\n", "Reserv profile id");
    SYS_QUEUE_DBG_DUMP("%-30s: %d\n", "--default pool", p_gg_queue_master[lchip]->egs_pool[0].default_profile_id);
    SYS_QUEUE_DBG_DUMP("%-30s: %d\n", "--non-drop pool", p_gg_queue_master[lchip]->egs_pool[1].default_profile_id);
    SYS_QUEUE_DBG_DUMP("%-30s: %d\n", "--span pool", p_gg_queue_master[lchip]->egs_pool[2].default_profile_id);
    SYS_QUEUE_DBG_DUMP("%-30s: %d\n", "--control pool", p_gg_queue_master[lchip]->egs_pool[3].default_profile_id);
    SYS_QUEUE_DBG_DUMP("\n");

    SYS_QUEUE_DBG_DUMP("-------------------------Pause profile-----------------------\n");
    SYS_QUEUE_DBG_DUMP("%-30s: %d\n", "Normal Pause total profile",p_gg_queue_master[lchip]->p_resrc_profile_pool[SYS_RESRC_PROFILE_FC]->max_count);
    SYS_QUEUE_DBG_DUMP("%-30s: %d\n","--Used count", p_gg_queue_master[lchip]->p_resrc_profile_pool[SYS_RESRC_PROFILE_FC]->count);
    SYS_QUEUE_DBG_DUMP("%-30s: %d\n", "Priority Pause total profile",p_gg_queue_master[lchip]->p_resrc_profile_pool[SYS_RESRC_PROFILE_PFC]->max_count);
    SYS_QUEUE_DBG_DUMP("%-30s: %d\n","--Used count", p_gg_queue_master[lchip]->p_resrc_profile_pool[SYS_RESRC_PROFILE_PFC]->count);
    SYS_QUEUE_DBG_DUMP("\n");

    SYS_QUEUE_DBG_DUMP("-------------------------Resrc status-----------------------\n");
    SYS_QUEUE_DBG_DUMP("%-30s: %d\n", "Ingress resrc mode", p_gg_queue_master[lchip]->igs_resrc_mode);
    SYS_QUEUE_DBG_DUMP("%-30s: %d\n", "Egress  resrc mode", p_gg_queue_master[lchip]->egs_resrc_mode);
    SYS_QUEUE_DBG_DUMP("\n");

   return CTC_E_NONE;
}


int32
sys_goldengate_qos_set_fc_default_profile(uint8 lchip, uint32 gport)
{
    uint16 lport = 0;
    uint8 priority_class = 0;
    ctc_qos_resrc_fc_t fc;
    sys_datapath_lport_attr_t* port_attr = NULL;

    sal_memset(&fc, 0, sizeof(ctc_qos_resrc_fc_t));

    lport = CTC_MAP_GPORT_TO_LPORT(gport);
    CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, lport, &port_attr));
    if (SYS_DATAPATH_NETWORK_PORT != port_attr->port_type)
    {
         return CTC_E_NONE;
    }

    if ((CTC_CHIP_SERDES_SGMII_MODE == port_attr->pcs_mode)
        || (CTC_CHIP_SERDES_XFI_MODE == port_attr->pcs_mode)
        || (CTC_CHIP_SERDES_XAUI_MODE == port_attr->pcs_mode)
        || (CTC_CHIP_SERDES_DXAUI_MODE == port_attr->pcs_mode)
        || (CTC_CHIP_SERDES_2DOT5G_MODE == port_attr->pcs_mode))
    {
        fc.xon_thrd = 50;
        fc.xoff_thrd = 60;
        fc.drop_thrd = 32752;
    }
    else
    {
        fc.xon_thrd = 240;
        fc.xoff_thrd = 256;
        fc.drop_thrd = 32752;
    }
    fc.gport = gport;
    fc.is_pfc = 0;
    SYS_QOS_QUEUE_LOCK(lchip);
    for (priority_class = 0; priority_class < 8; priority_class++)
    {
        fc.priority_class = priority_class;
        CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(sys_goldengate_qos_fc_add_profile(lchip, &fc));
    }
    SYS_QOS_QUEUE_UNLOCK(lchip);

    return CTC_E_NONE;
}
int32
sys_goldengate_queue_drop_init_egs_pool_classify(uint8 lchip, uint16 port_id, uint16 queue_num, uint16 port_num)
{
    uint16 queue_id = 0;
    /*default pool */
    if (p_gg_queue_master[lchip]->egs_pool[CTC_QOS_EGS_RESRC_DEFAULT_POOL].egs_congest_level_num)
    {
        for (queue_id = 0; queue_id < queue_num; queue_id ++)
        {
            CTC_ERROR_RETURN(sys_goldengate_queue_set_egs_pool_classify(lchip, queue_id, CTC_QOS_EGS_RESRC_DEFAULT_POOL, port_id,port_num));
        }
    }

    /* Control pool: queue 7*/
    if (p_gg_queue_master[lchip]->egs_pool[CTC_QOS_EGS_RESRC_CONTROL_POOL].egs_congest_level_num)
    {
        if ( 0 != p_gg_queue_master[lchip]->enq_mode)
        {
            CTC_ERROR_RETURN(sys_goldengate_queue_set_egs_pool_classify(lchip, (queue_num - 1), CTC_QOS_EGS_RESRC_CONTROL_POOL, port_id,port_num));
        }
        else
        {
            for (queue_id = queue_num; queue_id < queue_num + 8; queue_id ++)
            {
                CTC_ERROR_RETURN(sys_goldengate_queue_set_egs_pool_classify(lchip, queue_id, CTC_QOS_EGS_RESRC_CONTROL_POOL, port_id,port_num));
            }
        }
    }

    /*Span-Mcast pool: queue 8-15*/
    if (p_gg_queue_master[lchip]->egs_pool[CTC_QOS_EGS_RESRC_SPAN_POOL].egs_congest_level_num &&
        (1 == p_gg_queue_master[lchip]->enq_mode))
    {
        for (queue_id = 8; queue_id < 16; queue_id ++)
        {
            CTC_ERROR_RETURN(sys_goldengate_queue_set_egs_pool_classify(lchip, queue_id, CTC_QOS_EGS_RESRC_SPAN_POOL, port_id,port_num));
        }
    }

    return CTC_E_NONE;
}
int32
sys_goldengate_queue_drop_init_profile(uint8 lchip, ctc_qos_global_cfg_t* p_cfg)
{
    sys_goldengate_opf_t opf;
    uint8 profile_used_cnt = 0;
    uint32 que_drop_profile_num = 0;
    uint32 fc_profile_num = 0;
    uint32 pfc_profile_num = 0;
    ctc_spool_t spool;

    /* init drop profile spool table */
    sys_goldengate_ftm_query_table_entry_num(lchip, DsQueThrdProfile_t, &que_drop_profile_num);
    sys_goldengate_ftm_query_table_entry_num(lchip, DsIgrPortThrdProfile_t, &fc_profile_num);
    sys_goldengate_ftm_query_table_entry_num(lchip, DsIgrPortTcThrdProfile_t, &pfc_profile_num);

    /* care congest level*/
    que_drop_profile_num  = que_drop_profile_num / 8;
    fc_profile_num  = fc_profile_num / 8;
    pfc_profile_num  = pfc_profile_num / 8;

    sal_memset(&spool, 0, sizeof(ctc_spool_t));
    spool.lchip = lchip;
    spool.block_num = 1;
    spool.block_size = 1;
    spool.max_count = que_drop_profile_num;
    spool.user_data_size = sizeof(sys_queue_drop_profile_t);
    spool.spool_key = (hash_key_fn)_sys_goldengate_queue_drop_hash_make_profile;
    spool.spool_cmp = (hash_cmp_fn)_sys_goldengate_queue_drop_hash_cmp_profile;
    p_gg_queue_master[lchip]->p_drop_profile_pool = ctc_spool_create(&spool);

    sal_memset(&spool, 0, sizeof(ctc_spool_t));
    spool.lchip = lchip;
    spool.block_num = 1;
    spool.block_size = 1;
    spool.max_count = fc_profile_num;
    spool.user_data_size = sizeof(sys_qos_fc_profile_t);
    spool.spool_key = (hash_key_fn)_sys_goldengate_qos_profile_hash_make;
    spool.spool_cmp = (hash_cmp_fn)_sys_goldengate_qos_profile_hash_cmp;
    p_gg_queue_master[lchip]->p_resrc_profile_pool[SYS_RESRC_PROFILE_FC] = ctc_spool_create(&spool);

    sal_memset(&spool, 0, sizeof(ctc_spool_t));
    spool.lchip = lchip;
    spool.block_num = 1;
    spool.block_size = 1;
    spool.max_count = pfc_profile_num;
    spool.user_data_size = sizeof(sys_qos_fc_profile_t);
    spool.spool_key = (hash_key_fn)_sys_goldengate_qos_profile_hash_make;
    spool.spool_cmp = (hash_cmp_fn)_sys_goldengate_qos_profile_hash_cmp;
    p_gg_queue_master[lchip]->p_resrc_profile_pool[SYS_RESRC_PROFILE_PFC] = ctc_spool_create(&spool);

    /* add static profile */
    CTC_ERROR_RETURN(_sys_goldengate_queue_drop_add_static_profile(lchip, p_cfg));
    CTC_ERROR_RETURN(_sys_goldengate_qos_fc_add_static_profile(lchip));


    /*opf init*/
    CTC_ERROR_RETURN(sys_goldengate_opf_init(lchip, OPF_QUEUE_DROP_PROFILE, SYS_RESRC_PROFILE_MAX));

    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
    opf.pool_index = SYS_RESRC_PROFILE_QUEUE;
    opf.pool_type = OPF_QUEUE_DROP_PROFILE;
    profile_used_cnt = p_gg_queue_master[lchip]->p_drop_profile_pool->count;
    CTC_ERROR_RETURN(sys_goldengate_opf_init_offset(lchip, &opf,
                                                    profile_used_cnt,
                                                    (que_drop_profile_num - profile_used_cnt)));

    opf.pool_index = SYS_RESRC_PROFILE_FC;
    opf.pool_type = OPF_QUEUE_DROP_PROFILE;
    profile_used_cnt = p_gg_queue_master[lchip]->p_resrc_profile_pool[SYS_RESRC_PROFILE_FC]->count;
    CTC_ERROR_RETURN(sys_goldengate_opf_init_offset(lchip, &opf,
                                                    profile_used_cnt,
                                                    (fc_profile_num - profile_used_cnt)));

    opf.pool_index = SYS_RESRC_PROFILE_PFC;
    opf.pool_type = OPF_QUEUE_DROP_PROFILE;
    profile_used_cnt = p_gg_queue_master[lchip]->p_resrc_profile_pool[SYS_RESRC_PROFILE_PFC]->count;
    CTC_ERROR_RETURN(sys_goldengate_opf_init_offset(lchip, &opf,
                                                    profile_used_cnt,
                                                    (pfc_profile_num - profile_used_cnt)));


   return CTC_E_NONE;


}

/**
 @brief Queue dropping initialization.
*/
int32
sys_goldengate_queue_drop_init(uint8 lchip, void *p_glb_parm)
{
    uint16 port_id      = 0;
    uint16 port_num      = 0;
    uint16 queue_num    = 0;
    ctc_qos_global_cfg_t * p_glb_cfg = NULL;
    p_glb_cfg = (ctc_qos_global_cfg_t *)p_glb_parm;

    CTC_ERROR_RETURN(_sys_goldengate_queue_igs_resrc_mgr_init(lchip, p_glb_cfg));
    CTC_ERROR_RETURN(_sys_goldengate_queue_egs_resrc_mgr_init(lchip, p_glb_cfg));
    CTC_ERROR_RETURN(sys_goldengate_queue_drop_init_profile(lchip, p_glb_cfg));

    p_gg_queue_master[lchip]->igs_resrc_mode = p_glb_cfg->resrc_pool.igs_pool_mode;
    p_gg_queue_master[lchip]->egs_resrc_mode = p_glb_cfg->resrc_pool.egs_pool_mode;
    p_gg_queue_master[lchip]->igs_port_min[0].ref = 0xFFFF;   /* 0 is default profile */
    p_gg_queue_master[lchip]->igs_port_min[0].min = 12;


    if (16 == p_gg_queue_master[lchip]->queue_num_per_chanel)
    {
        port_num     = 48;
        queue_num   = 8;
    }
    else if (8 == p_gg_queue_master[lchip]->queue_num_per_chanel)
    {
        port_num     = 120;
        queue_num   = 8;
    }
    else if (4 == p_gg_queue_master[lchip]->queue_num_per_chanel)
    {
        port_num     = 240;
        queue_num   = 4;
    }

/*
   8+1 mode:     0~7 use default pool, 8 use critical pool
   8+4+1 mode:   0~6 use default pool, 7 use critical pool, 8~15 use span pool
   4 queue bpe mode: 0~2 use default pool, 3 use critical pool
   8 queue bpe mode: 0~6 use default pool, 7 use critical pool
*/
    CTC_ERROR_RETURN(sys_goldengate_queue_drop_init_egs_pool_classify(lchip, port_id, queue_num, port_num));

    return CTC_E_NONE;
}

/**
 @brief Serdes Queue dropping initialization.
*/
int32
sys_goldengate_queue_serdes_drop_init(uint8 lchip, uint16 port_id)
{
    uint16 queue_num    = 0;
    uint16 port_num      = 1;

    if (16 == p_gg_queue_master[lchip]->queue_num_per_chanel)
    {
        queue_num   = 8;
    }
    else if (8 == p_gg_queue_master[lchip]->queue_num_per_chanel)
    {
        queue_num   = 8;
    }
    else if (4 == p_gg_queue_master[lchip]->queue_num_per_chanel)
    {
        queue_num   = 4;
    }

/*
   8+1 mode:     0~7 use default pool, 8 use critical pool
   8+4+1 mode:   0~6 use default pool, 7 use critical pool, 8~15 use span pool
   4 queue bpe mode: 0~2 use default pool, 3 use critical pool
   8 queue bpe mode: 0~6 use default pool, 7 use critical pool
*/
    CTC_ERROR_RETURN(sys_goldengate_queue_drop_init_egs_pool_classify(lchip, port_id, queue_num, port_num));

    return CTC_E_NONE;
}


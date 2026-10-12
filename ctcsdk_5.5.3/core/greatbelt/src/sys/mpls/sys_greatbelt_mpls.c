/**
 @file sys_greatbelt_mpls.c

 @date 2010-03-12

 @version v2.0

 The file contains all mpls related function
*/

/****************************************************************************
 *
* Header Files
*
****************************************************************************/
#include "ctc_const.h"
#include "ctc_error.h"

#include "ctc_mpls.h"
#include "ctc_vector.h"
#include "ctc_stats.h"

#include "sys_greatbelt_chip.h"
#include "sys_greatbelt_scl.h"
#include "sys_greatbelt_mpls.h"
#include "sys_greatbelt_nexthop_api.h"
#include "sys_greatbelt_qos_policer.h"
#include "sys_greatbelt_queue_enq.h"
#include "sys_greatbelt_scl.h"
#include "sys_greatbelt_vlan.h"
#include "sys_greatbelt_nexthop.h"
#include "sys_greatbelt_ftm.h"

#include "sys_greatbelt_stats.h"
#include "sys_greatbelt_l3if.h"
#include "sys_greatbelt_l2_fdb.h"

#include "greatbelt/include/drv_io.h"

/****************************************************************************
 *
* Global and Declaration
*
*****************************************************************************/
sys_mpls_master_t* p_gb_mpls_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};
#define SYS_MPLS_CREAT_LOCK                   \
    do                                            \
    {                                             \
        sal_mutex_create(&p_gb_mpls_master[lchip]->mutex); \
        if (NULL == p_gb_mpls_master[lchip]->mutex)  \
        { \
            CTC_ERROR_RETURN(CTC_E_FAIL_CREATE_MUTEX); \
        } \
    } while (0)

#define SYS_MPLS_LOCK \
    sal_mutex_lock(p_gb_mpls_master[lchip]->mutex)

#define SYS_MPLS_UNLOCK \
    sal_mutex_unlock(p_gb_mpls_master[lchip]->mutex)

#define CTC_ERROR_RETURN_MPLS_UNLOCK(op) \
    do \
    { \
        int32 rv = (op); \
        if (rv < 0) \
        { \
            sal_mutex_unlock(p_gb_mpls_master[lchip]->mutex); \
            return rv; \
        } \
    } while (0)

#define CTC_RETURN_MPLS_UNLOCK(op) \
    do \
    { \
        sal_mutex_unlock(p_gb_mpls_master[lchip]->mutex); \
        return (op); \
    } while (0)

int32
sys_greatbelt_mpls_calc_min_label_for_2_block(uint8 lchip, uint32 *min_label)
{
    SYS_MPLS_INIT_CHECK(lchip);

    if(p_gb_mpls_master[lchip]->end_label1 && p_gb_mpls_master[lchip]->end_label1<SYS_MPLS_MAX_LABEL_RANGE)
    {
        /*e.g. 0-1k  30-40k */
        *min_label = 0;
    }
    else if(p_gb_mpls_master[lchip]->start_label1 && p_gb_mpls_master[lchip]->start_label1<SYS_MPLS_MAX_LABEL_RANGE)
    {
        /*e.g. 0-1k  110-140k */
        if(p_gb_mpls_master[lchip]->end_label0&&
           ((p_gb_mpls_master[lchip]->end_label1-SYS_MPLS_MAX_LABEL_RANGE)<(p_gb_mpls_master[lchip]->start_label1-p_gb_mpls_master[lchip]->end_label0)))
        {
            *min_label = p_gb_mpls_master[lchip]->start_label1-p_gb_mpls_master[lchip]->end_label0-1;
        }
        else
        {
            return CTC_E_MPLS_LABEL_RANGE_ERROR;
        }
    }
    else if(p_gb_mpls_master[lchip]->end_label0 && p_gb_mpls_master[lchip]->end_label0<SYS_MPLS_MAX_LABEL_RANGE)
    {
        /*e.g. 0-1k  130-140k */
        if(((p_gb_mpls_master[lchip]->start_label1-p_gb_mpls_master[lchip]->end_label0-1)>SYS_MPLS_MAX_LABEL_RANGE)
            &&(p_gb_mpls_master[lchip]->end_label1&&((p_gb_mpls_master[lchip]->end_label1-SYS_MPLS_120K_LABEL_RANGE)<SYS_MPLS_MAX_LABEL_RANGE)))
        {
            *min_label = SYS_MPLS_120K_LABEL_RANGE;
        }
        else if(p_gb_mpls_master[lchip]->end_label1
            &&(p_gb_mpls_master[lchip]->end_label1-(p_gb_mpls_master[lchip]->start_label1-p_gb_mpls_master[lchip]->end_label0-1))<SYS_MPLS_MAX_LABEL_RANGE)
        {
            /*e.g. 0-20k  130-140k */
            *min_label = (p_gb_mpls_master[lchip]->start_label1-p_gb_mpls_master[lchip]->end_label0-1);
        }
        else
        {
            return CTC_E_MPLS_LABEL_RANGE_ERROR;
        }
    }
    else if(p_gb_mpls_master[lchip]->start_label0 && p_gb_mpls_master[lchip]->start_label0<SYS_MPLS_MAX_LABEL_RANGE)
    {
        /*e.g. 0-130k 140-180k */
        return CTC_E_MPLS_LABEL_RANGE_ERROR;
    }
    else
    {
        return CTC_E_MPLS_LABEL_RANGE_ERROR;
    }

    p_gb_mpls_master[lchip]->min_label = *min_label;

    return CTC_E_NONE;

}


int32
sys_greatbelt_mpls_calc_min_label(uint8 lchip, uint32 *min_label)
{
    SYS_MPLS_INIT_CHECK(lchip);

    if(p_gb_mpls_master[lchip]->end_label2 && p_gb_mpls_master[lchip]->end_label2<SYS_MPLS_MAX_LABEL_RANGE)
    {
        /*e.g. 0-1k  30-40k 60-70k */
        *min_label = 0;
    }
    else if(p_gb_mpls_master[lchip]->start_label2 && p_gb_mpls_master[lchip]->start_label2<SYS_MPLS_MAX_LABEL_RANGE)
    {
        /*e.g. 0-1k  30-40k 120-130k */
        if(p_gb_mpls_master[lchip]->end_label1&&
          ((p_gb_mpls_master[lchip]->end_label2-SYS_MPLS_MAX_LABEL_RANGE)<(p_gb_mpls_master[lchip]->start_label2-p_gb_mpls_master[lchip]->end_label1)))
        {
            /*e.g. 0-1k  30-40k 110-130k */
            *min_label = p_gb_mpls_master[lchip]->start_label2-p_gb_mpls_master[lchip]->end_label1-1;   /*70k */
        }
        else if((p_gb_mpls_master[lchip]->end_label0)&&(p_gb_mpls_master[lchip]->start_label1)
            &&((p_gb_mpls_master[lchip]->end_label2-p_gb_mpls_master[lchip]->start_label1)<(p_gb_mpls_master[lchip]->start_label2-p_gb_mpls_master[lchip]->end_label0)))
        {
            /*e.g. 0-40k  90-100k 110-140k */
            *min_label = p_gb_mpls_master[lchip]->start_label2-p_gb_mpls_master[lchip]->end_label0-1; /* 70k */
        }
        else
        {
            return CTC_E_MPLS_LABEL_RANGE_ERROR;
        }
    }
    else if(p_gb_mpls_master[lchip]->end_label1 && p_gb_mpls_master[lchip]->end_label1<SYS_MPLS_MAX_LABEL_RANGE)
    {
        /*e.g. 0-1k  30-40k 130-140k */
        if(p_gb_mpls_master[lchip]->end_label1&&
          ((p_gb_mpls_master[lchip]->end_label2-SYS_MPLS_MAX_LABEL_RANGE)<(p_gb_mpls_master[lchip]->start_label2-p_gb_mpls_master[lchip]->end_label1)))
        {
            /*e.g. 0-1k  30-40k 130-140k */
            *min_label = p_gb_mpls_master[lchip]->start_label2-p_gb_mpls_master[lchip]->end_label1-1;    /* min 90k*/
        }
        else if((p_gb_mpls_master[lchip]->end_label0)&&(p_gb_mpls_master[lchip]->start_label1)
            &&((p_gb_mpls_master[lchip]->end_label2-p_gb_mpls_master[lchip]->start_label1)<(p_gb_mpls_master[lchip]->start_label2-p_gb_mpls_master[lchip]->end_label0)))
        {
            /*e.g. 0-40k  75-85k 130-136k  min can be 90k */
            *min_label = p_gb_mpls_master[lchip]->start_label2-p_gb_mpls_master[lchip]->end_label0-1;    /* min 90k*/
            if(((*min_label<p_gb_mpls_master[lchip]->end_label1)&&(*min_label>p_gb_mpls_master[lchip]->start_label1))
                ||(*min_label<p_gb_mpls_master[lchip]->start_label1))
            {
                /*e.g. 0-40k  115-125k 130-136k  min can be 75k */
                *min_label = p_gb_mpls_master[lchip]->start_label1-p_gb_mpls_master[lchip]->end_label0-1; /*75k */
                if(*min_label+SYS_MPLS_MAX_LABEL_RANGE < p_gb_mpls_master[lchip]->end_label2)
                {
                    return CTC_E_MPLS_LABEL_RANGE_ERROR;
                }
            }
        }
        else
        {
            return CTC_E_MPLS_LABEL_RANGE_ERROR;
        }
    }
    else if(p_gb_mpls_master[lchip]->start_label1 && p_gb_mpls_master[lchip]->start_label1<SYS_MPLS_MAX_LABEL_RANGE)
    {
        /*e.g. 0-1k  90-130k 160-180k */
        if(p_gb_mpls_master[lchip]->end_label2
            &&(p_gb_mpls_master[lchip]->end_label2-(p_gb_mpls_master[lchip]->start_label1-p_gb_mpls_master[lchip]->end_label0-1))<SYS_MPLS_MAX_LABEL_RANGE)
        {
            *min_label = (p_gb_mpls_master[lchip]->start_label1-p_gb_mpls_master[lchip]->end_label0-1);
        }
        else
        {
            return CTC_E_MPLS_LABEL_RANGE_ERROR;
        }
    }
    else if(p_gb_mpls_master[lchip]->end_label0 && p_gb_mpls_master[lchip]->end_label0<SYS_MPLS_MAX_LABEL_RANGE)
    {
        /*e.g. 0-1k  130-140k 160-180k */
        if(((p_gb_mpls_master[lchip]->start_label1-p_gb_mpls_master[lchip]->end_label0-1)>SYS_MPLS_MAX_LABEL_RANGE)
            &&(p_gb_mpls_master[lchip]->end_label2&&((p_gb_mpls_master[lchip]->end_label2-SYS_MPLS_120K_LABEL_RANGE)<SYS_MPLS_MAX_LABEL_RANGE)))
        {
            *min_label = SYS_MPLS_120K_LABEL_RANGE;
        }
        else if(p_gb_mpls_master[lchip]->end_label2
            &&(p_gb_mpls_master[lchip]->end_label2-(p_gb_mpls_master[lchip]->start_label1-p_gb_mpls_master[lchip]->end_label0-1))<SYS_MPLS_MAX_LABEL_RANGE)
        {
            /*e.g. 0-20k  130-140k 160-180k */
            *min_label = (p_gb_mpls_master[lchip]->start_label1-p_gb_mpls_master[lchip]->end_label0-1);
        }
        else
        {
            return CTC_E_MPLS_LABEL_RANGE_ERROR;
        }
    }
    else
    {
        return CTC_E_MPLS_LABEL_RANGE_ERROR;
    }

    p_gb_mpls_master[lchip]->min_label = *min_label;

    return CTC_E_NONE;

}

int32
sys_greatbelt_mpls_set_label_range_mode(uint8 lchip, bool mode_en)
{
    SYS_MPLS_INIT_CHECK(lchip);

    p_gb_mpls_master[lchip]->label_range_mode = (mode_en) ? 1 : 0;

    return CTC_E_NONE;
}


int32
sys_greatbelt_mpls_set_label_range(uint8 lchip, uint8 block,uint32 start_label,uint32 size)
{
    uint32 min_label = 0;
    int32 ret = 0;
    ipe_mpls_ctl_t  ipe_mpls_ctl;
    ds_mpls_ctl_t mpls_ctl;
    uint32 cmdr, cmdw;
    uint32 label_space_size_type_global = 0;
    uint32 old_start_label0 = 0;
    uint32 old_end_label0 = 0;
    uint32 old_start_label1 = 0;
    uint32 old_end_label1 = 0;
    uint32 old_start_label2 = 0;
    uint32 old_end_label2 = 0;

    SYS_MPLS_INIT_CHECK(lchip);
    if(!p_gb_mpls_master[lchip]->label_range_mode)
    {
        return CTC_E_NONE;
    }

    if(block > 2||(start_label+size)>SYS_MPLS_MAX_CHECK_LABEL)
    {
        return CTC_E_MPLS_LABEL_RANGE_ERROR;
    }

    old_start_label0 = p_gb_mpls_master[lchip]->start_label0;
    old_end_label0 = p_gb_mpls_master[lchip]->end_label0;
    old_start_label1 = p_gb_mpls_master[lchip]->start_label1;
    old_end_label1 = p_gb_mpls_master[lchip]->end_label1;
    old_start_label2 = p_gb_mpls_master[lchip]->start_label2;
    old_end_label2 = p_gb_mpls_master[lchip]->end_label2;

    if(block==0)
    {
        if((p_gb_mpls_master[lchip]->start_label0==0)&&(size==0))
        {
            return CTC_E_MPLS_LABEL_RANGE_ERROR;
        }
        else
        {
            p_gb_mpls_master[lchip]->start_label0 = start_label;
            p_gb_mpls_master[lchip]->end_label0 = start_label+size-1;
        }
    }
    else if(block==1)
    {
        if(start_label)
        {
            if(size==0)
            {
                return CTC_E_MPLS_LABEL_RANGE_ERROR;
            }
            p_gb_mpls_master[lchip]->start_label1 = start_label;
            p_gb_mpls_master[lchip]->end_label1 = start_label+size-1;
        }
        else
        {
            return CTC_E_MPLS_LABEL_RANGE_ERROR;
        }
    }
    else if(block==2)
    {
        if(start_label)
        {
            if(size==0)
            {
                return CTC_E_MPLS_LABEL_RANGE_ERROR;
            }
            p_gb_mpls_master[lchip]->start_label2 = start_label;
            p_gb_mpls_master[lchip]->end_label2 = start_label + size - 1;
        }
        else
        {
            return CTC_E_MPLS_LABEL_RANGE_ERROR;
        }
    }

    if(!p_gb_mpls_master[lchip]->end_label0&&!p_gb_mpls_master[lchip]->end_label1&&!p_gb_mpls_master[lchip]->end_label2)
    {
        p_gb_mpls_master[lchip]->min_label = 0;
        return CTC_E_NONE;
    }

    if((p_gb_mpls_master[lchip]->end_label0&&(p_gb_mpls_master[lchip]->start_label0>p_gb_mpls_master[lchip]->end_label0))
        ||(p_gb_mpls_master[lchip]->start_label1&&p_gb_mpls_master[lchip]->end_label1&&(p_gb_mpls_master[lchip]->start_label1>p_gb_mpls_master[lchip]->end_label1))
        ||(p_gb_mpls_master[lchip]->start_label2&&p_gb_mpls_master[lchip]->end_label2&&(p_gb_mpls_master[lchip]->start_label2>p_gb_mpls_master[lchip]->end_label2))
        ||(p_gb_mpls_master[lchip]->end_label0&&p_gb_mpls_master[lchip]->start_label1&&(p_gb_mpls_master[lchip]->end_label0>p_gb_mpls_master[lchip]->start_label1))
        ||(p_gb_mpls_master[lchip]->end_label1&&p_gb_mpls_master[lchip]->start_label2&&(p_gb_mpls_master[lchip]->end_label1>p_gb_mpls_master[lchip]->start_label2)))
    {
        goto  error_0;
    }

    if((p_gb_mpls_master[lchip]->end_label0)
       &&(p_gb_mpls_master[lchip]->start_label1&&p_gb_mpls_master[lchip]->end_label1)
       &&(p_gb_mpls_master[lchip]->start_label2&&p_gb_mpls_master[lchip]->end_label2))
    {
        ret = sys_greatbelt_mpls_calc_min_label(lchip, &min_label);
        if(ret<0)
        {
            goto error_0;
        }
        SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "%d\n", min_label);

    }
    else if((p_gb_mpls_master[lchip]->end_label0)
       &&(p_gb_mpls_master[lchip]->start_label1&&p_gb_mpls_master[lchip]->end_label1))
    {
        ret = sys_greatbelt_mpls_calc_min_label_for_2_block(lchip, &min_label);
        if(ret<0)
        {
            goto error_0;
        }
        SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "%d\n", min_label);
    }
    else if(p_gb_mpls_master[lchip]->end_label0)
    {
        min_label = 0;
        SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "%d\n", min_label);
    }

    if(block==2)
    {
        if((p_gb_mpls_master[lchip]->start_label2<min_label))
        {
            CTC_ERROR_RETURN(sys_greatbelt_ftm_set_mpls_label_offset(lchip, 2, 0,size));
        }
        else
        {
            CTC_ERROR_RETURN(sys_greatbelt_ftm_set_mpls_label_offset(lchip, 2, p_gb_mpls_master[lchip]->start_label2-min_label,size));
        }
    }

    if((block==2)||(block==1))
    {
        if((p_gb_mpls_master[lchip]->start_label1<min_label))
        {
            if((block==2))
            {
                CTC_ERROR_RETURN(sys_greatbelt_ftm_set_mpls_label_offset(lchip, 1, p_gb_mpls_master[lchip]->start_label1,size));
            }
            else
            {
                CTC_ERROR_RETURN(sys_greatbelt_ftm_set_mpls_label_offset(lchip, 1, 0,size));
            }
        }
        else
        {
            CTC_ERROR_RETURN(sys_greatbelt_ftm_set_mpls_label_offset(lchip, 1, p_gb_mpls_master[lchip]->start_label1-min_label,size));
        }
    }

    if((block==2)||(block==1)||(block==0))
    {
        CTC_ERROR_RETURN(sys_greatbelt_ftm_set_mpls_label_offset(lchip, 0, 0,size));
    }

    /* write register for gb ipe mpls calc effective label */

    cmdr = DRV_IOR(IpeMplsCtl_t, DRV_ENTRY_FLAG);
    cmdw = DRV_IOW(IpeMplsCtl_t, DRV_ENTRY_FLAG);

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmdr, &ipe_mpls_ctl));
    ipe_mpls_ctl.min_interface_label_mcast = ipe_mpls_ctl.min_interface_label  = min_label;
    label_space_size_type_global = ipe_mpls_ctl.label_space_size_type_global;

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmdw, &ipe_mpls_ctl));

    sal_memset(&mpls_ctl, 0, sizeof(ds_mpls_ctl_t));
    cmdr = DRV_IOR(DsMplsCtl_t, DRV_ENTRY_FLAG);
    cmdw = DRV_IOW(DsMplsCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmdr, &mpls_ctl));

    mpls_ctl.label_space_size_type = mpls_ctl.label_space_size_type_mcast = label_space_size_type_global;
    mpls_ctl.interface_label_valid = mpls_ctl.interface_label_valid_mcast = 1;

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmdw, &mpls_ctl));

    return CTC_E_NONE;

error_0:
    p_gb_mpls_master[lchip]->start_label0 = old_start_label0;
    p_gb_mpls_master[lchip]->end_label0 = old_end_label0;
    p_gb_mpls_master[lchip]->start_label1 = old_start_label1;
    p_gb_mpls_master[lchip]->end_label1 = old_end_label1;
    p_gb_mpls_master[lchip]->start_label2 = old_start_label2;
    p_gb_mpls_master[lchip]->end_label2 = old_end_label2;
    return CTC_E_MPLS_LABEL_RANGE_ERROR;
}


void
_sys_greatbelt_mpls_get_sizetype(uint8 lchip, uint16 size, uint8* sizetype)
{
    if (size)
    {
        while (size > 1)
        {
            size >>= 1;
            (*sizetype)++;
        }
    }

    return;
}

STATIC int32
_sys_greatbelt_mpls_add_service(uint8 lchip, sys_mpls_ilm_t* p_mpls_ilm, ds_mpls_t* p_dsmpls, sys_mpls_ilm_t ilm_data_old)
{
    uint16 service_id = 0;

    CTC_PTR_VALID_CHECK(p_mpls_ilm);
    CTC_PTR_VALID_CHECK(p_dsmpls);

    service_id = p_mpls_ilm->service_id;

    if (p_mpls_ilm->service_policer_en)
    {
        uint16 policer_ptr = 0;
        CTC_ERROR_RETURN(sys_greatbelt_qos_policer_index_get(lchip,
                                                             CTC_QOS_POLICER_TYPE_SERVICE,
                                                             service_id,
                                                             &policer_ptr));
        p_dsmpls->mpls_flow_policer_ptr = policer_ptr;
        p_dsmpls->service_policer_valid = 1;
        p_dsmpls->policer_ptr_share_mode = 0;
    }

    if (p_mpls_ilm->service_aclqos_enable)
    {
        p_dsmpls->service_acl_qo_s_en = 1;
    }
    else
    {
        p_dsmpls->service_acl_qo_s_en = 0;
    }

    if (p_mpls_ilm->service_queue_en)
    {
        CTC_ERROR_RETURN(sys_greatbelt_qos_bind_service_logic_port(lchip, service_id, p_mpls_ilm->pwid));
        if(ilm_data_old.service_queue_en && (ilm_data_old.service_id != service_id || ilm_data_old.pwid != p_mpls_ilm->pwid))
        {
            CTC_ERROR_RETURN(sys_greatbelt_qos_unbind_service_logic_port(lchip, ilm_data_old.service_id, ilm_data_old.pwid));
        }        
    }
    else
    {
        sys_greatbelt_qos_unbind_service_logic_port(lchip, service_id, p_mpls_ilm->pwid);
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_mpls_remove_service(uint8 lchip, sys_mpls_ilm_t* p_mpls_ilm)
{

    uint16 service_id = 0;

    CTC_PTR_VALID_CHECK(p_mpls_ilm);

    service_id = p_mpls_ilm->service_id;

    if (p_mpls_ilm->service_queue_en)
    {
        CTC_ERROR_RETURN(sys_greatbelt_qos_unbind_service_logic_port(lchip, service_id, p_mpls_ilm->pwid));
    }

    return CTC_E_NONE;
}



STATIC int32
_sys_greatbelt_mpls_ilm_normal(uint8 lchip, sys_mpls_ilm_t* p_ilm_info, void* mpls)
{

    ds_mpls_t* dsmpls = mpls;

    if (p_ilm_info->pop)
    {
        dsmpls->_continue = TRUE;
        if(p_ilm_info->out_intf_spaceid)
        {
            dsmpls->label_space_valid = 1;
            dsmpls->ds_fwd_ptr = p_ilm_info->out_intf_spaceid;
        }
    }

    dsmpls->offset_bytes = 1;

    if (p_ilm_info->ecpn)
    {
        p_ilm_info->ecpn = (p_ilm_info->ecpn > 8) ? 8 : p_ilm_info->ecpn;
        dsmpls->equal_cost_path_num = (p_ilm_info->ecpn - 1) & 0x7;
    }

    /* for normal mpls pkt, we need the condition for pop mode than it'll decap! */
    /* php don't use decap pkt, without php must use inner pkt to transit */
    if (p_ilm_info->decap)
    {
        dsmpls->inner_packet_lookup = TRUE;

        /* according to  rfc3270*/
        if (CTC_MPLS_TUNNEL_MODE_SHORT_PIPE != p_ilm_info->model)
        {
            dsmpls->aclqos_use_outer_info = 1;
        }
    }

    if (CTC_MPLS_TUNNEL_MODE_UNIFORM != p_ilm_info->model)
    {
        dsmpls->use_label_ttl = TRUE;
        dsmpls->ttl_update = TRUE;
    }



    dsmpls->overwrite_priority = TRUE;
    if (p_ilm_info->trust_outer_exp)
    {
        dsmpls->overwrite_priority = FALSE;
    }

    dsmpls->stats_mode = 1;  /* lsp stats */

    if (CTC_MPLS_INNER_IPV4 == p_ilm_info->inner_pkt_type)
    {
        dsmpls->packet_type = PKT_TYPE_IPV4;
    }
    else if (CTC_MPLS_INNER_IPV6 == p_ilm_info->inner_pkt_type)
    {
        dsmpls->packet_type = PKT_TYPE_IPV6;
    }
    else if (CTC_MPLS_INNER_IP == p_ilm_info->inner_pkt_type)
    {
        dsmpls->packet_type = PKT_TYPE_FLEXIBLE;
    }

    dsmpls->logic_src_port = p_ilm_info->pwid;

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_mpls_ilm_l3vpn(uint8 lchip, sys_mpls_ilm_t* p_ilm_info, void* mpls)
{

    ds_mpls_t* dsmpls = mpls;

    dsmpls->offset_bytes = 1;
    dsmpls->s_bit = TRUE;
    dsmpls->s_bit_check_en = TRUE;

    if (CTC_MPLS_TUNNEL_MODE_UNIFORM != p_ilm_info->model)
    {
        dsmpls->use_label_ttl = TRUE;
    }



    dsmpls->overwrite_priority = TRUE;

    dsmpls->fid_valid = TRUE;
    dsmpls->fid_type = FALSE;
    dsmpls->inner_packet_lookup = TRUE;
    dsmpls->stats_mode = 0;  /* pw stats */

    if (CTC_MPLS_INNER_IPV4 == p_ilm_info->inner_pkt_type)
    {
        dsmpls->packet_type = PKT_TYPE_IPV4;
    }
    else if (CTC_MPLS_INNER_IPV6 == p_ilm_info->inner_pkt_type)
    {
        dsmpls->packet_type = PKT_TYPE_IPV6;
    }
    else if (CTC_MPLS_INNER_IP == p_ilm_info->inner_pkt_type)
    {
        dsmpls->packet_type = PKT_TYPE_FLEXIBLE;
    }

    dsmpls->aclqos_use_outer_info = (p_ilm_info->qos_use_outer_info) ? TRUE : FALSE;

    dsmpls->logic_src_port = p_ilm_info->pwid;

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_mpls_ilm_vpws(uint8 lchip, sys_mpls_ilm_t* p_ilm_info, void* mpls)
{
    ds_mpls_t* dsmpls = mpls;

    dsmpls->s_bit = TRUE;
    dsmpls->s_bit_check_en = TRUE;

    /* get vpws source port??? */

    if (p_ilm_info->cwen)
    {
        dsmpls->cw_exist = 1;
        dsmpls->offset_bytes = 2;
    }
    else
    {
        dsmpls->offset_bytes = 1;
    }

    if (p_gb_mpls_master[lchip]->decap_mode)
    {
        dsmpls->offset_bytes = 0;
    }


    dsmpls->use_label_ttl = TRUE;


    dsmpls->stats_mode = 0;  /* pw stats */

    dsmpls->overwrite_priority = TRUE;
    if (p_ilm_info->trust_outer_exp)
    {
        dsmpls->overwrite_priority = FALSE;
    }

    if (CTC_MPLS_INNER_RAW != p_ilm_info->inner_pkt_type)
    {
        dsmpls->packet_type = PKT_TYPE_ETH;
    }
    else
    {
        dsmpls->packet_type = PKT_TYPE_RESERVED;
    }
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_mpls_ilm_vpls(uint8 lchip, sys_mpls_ilm_t* p_ilm_info, void* mpls)
{
    ds_mpls_t* dsmpls = mpls;

    dsmpls->use_label_ttl = TRUE;

    dsmpls->overwrite_priority = TRUE;
    if(p_ilm_info->trust_outer_exp)
    {
        dsmpls->overwrite_priority = FALSE;
    }


    if (p_ilm_info->cwen)
    {
        dsmpls->cw_exist = 1;
        dsmpls->offset_bytes = 2;
    }
    else
    {
        dsmpls->offset_bytes = 1;
    }

    if (p_ilm_info->qos_use_outer_info)
    {
        dsmpls->aclqos_use_outer_info = 1;
    }


    dsmpls->s_bit = TRUE;
    dsmpls->s_bit_check_en = TRUE;
    dsmpls->packet_type = PKT_TYPE_ETH;

    dsmpls->inner_packet_lookup = TRUE;
    dsmpls->fid_valid = TRUE;
    if (p_gb_mpls_master[lchip]->decap_mode)
    {
        dsmpls->inner_packet_lookup = FALSE;
        dsmpls->fid_valid = FALSE;
        dsmpls->offset_bytes = 0;
    }

    dsmpls->stats_mode = 0;  /* pw stats */

    dsmpls->fid_type = TRUE;
    /* dsmpls->fid_type = FALSE;    for l3vpn x l2vpn  */
    dsmpls->logic_src_port = p_ilm_info->pwid;

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_mpls_write_ilm(uint8 lchip, sys_mpls_ilm_t* p_mpls_info, uint32 fwd_offset, sys_nh_info_dsnh_t *p_nhinfo)
{

    ds_mpls_t dsmpls;
    uint8 gchip = 0;
    uint32 cmd = 0;
    uint32 tmp_destmap = 0;
    uint8  tmp_speed = 0;
    uint8  oam_check_type = 0;
    uint16 mep_index = 0;
    uint8  oam_dest_chipid1_0 = 0;
    uint8  oam_dest_chipid3_2 = 0;
    uint8  oam_dest_chipid4 = 0;
    uint8  update_oam = 0;
    uint16 lm_base = 0;
    uint8  lm_cos = 0;
    uint8  lm_cos_type = 0;
    uint8  lm_type = 0;
    uint8  mpls_lm_type = 0;

    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_MPLS_DUMP_MPLS_DB_INFO(p_mpls_info);

    /* read oam config */
    sal_memset(&dsmpls, 0, sizeof(ds_mpls_t));
    cmd = DRV_IOR(DsMpls_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_mpls_info->key_offset, cmd, &dsmpls));
    oam_check_type = dsmpls.oam_check_type;
    mep_index = dsmpls.mep_index;
    oam_dest_chipid1_0 = dsmpls.oam_dest_chipid1_0;
    oam_dest_chipid3_2 = dsmpls.oam_dest_chipid3_2;
    oam_dest_chipid4 = dsmpls.oam_dest_chipid4;
    lm_base = dsmpls.lm_base;
    lm_cos = dsmpls.lm_cos;
    lm_cos_type = dsmpls.lm_cos_type;
    lm_type = dsmpls.lm_type;
    mpls_lm_type = dsmpls.mpls_lm_type;

    sal_memset(&dsmpls, 0, sizeof(ds_mpls_t));
    dsmpls.oam_check_type = oam_check_type;
    dsmpls.mep_index = mep_index;
    dsmpls.oam_dest_chipid1_0 = oam_dest_chipid1_0;
    dsmpls.oam_dest_chipid3_2 = oam_dest_chipid3_2;
    dsmpls.oam_dest_chipid4 = oam_dest_chipid4;
    dsmpls.lm_base = lm_base;
    dsmpls.lm_cos = lm_cos;
    dsmpls.lm_cos_type = lm_cos_type;
    dsmpls.lm_type = lm_type;
    dsmpls.mpls_lm_type = mpls_lm_type;

    /* flow policer ptr */
    if (((p_mpls_info->id_type & CTC_MPLS_ID_APS_SELECT) && (p_mpls_info->id_type & CTC_MPLS_ID_FLOW)) ||
        ((p_mpls_info->id_type & CTC_MPLS_ID_APS_SELECT) && (p_mpls_info->policer_id != 0)) ||
        ((p_mpls_info->policer_id != 0) && (p_mpls_info->id_type & CTC_MPLS_ID_FLOW)))
    {
        return CTC_E_GLOBAL_CONFIG_CONFLICT;
    }

    if ((p_mpls_info->id_type & CTC_MPLS_ID_APS_SELECT))
    {
        dsmpls.aps_select_valid  = 1;
        if (p_mpls_info->aps_select_ppath)
        {
            dsmpls.aps_select_protecting_path = 1;
        }

        dsmpls.policer_ptr_share_mode = 1;
        dsmpls.mpls_flow_policer_ptr = p_mpls_info->aps_group_id & 0x1FFF;
    }
    else if (p_mpls_info->id_type & CTC_MPLS_ID_FLOW)
    {
        dsmpls.mpls_flow_policer_ptr = p_mpls_info->flow_id;
    }
    else if (p_mpls_info->policer_id != 0)
    {
       uint16 policer_ptr = 0;
       CTC_ERROR_RETURN(sys_greatbelt_qos_policer_index_get(lchip,
                                                            CTC_QOS_POLICER_TYPE_SERVICE,
                                                            p_mpls_info->policer_id,
                                                            &policer_ptr));
       dsmpls.mpls_flow_policer_ptr = policer_ptr;
       dsmpls.service_policer_valid = 1;
       dsmpls.policer_ptr_share_mode = 0;
    }



    if (p_mpls_info->cwen)
    {
        dsmpls.cw_exist = 1;     /* control word exist */
    }

    if (p_mpls_info->type != CTC_MPLS_LABEL_TYPE_NORMAL)
    {   /*vc label*/
        if ((p_mpls_info->id_type & CTC_MPLS_ID_VRF)
            || (p_mpls_info->id_type & CTC_MPLS_ID_SERVICE)
            || ((p_mpls_info->type == CTC_MPLS_LABEL_TYPE_VPLS || p_mpls_info->type == CTC_MPLS_LABEL_TYPE_VPWS) && p_mpls_info->pwid <= SYS_MPLS_VPLS_SRC_PORT_NUM))
        {
            dsmpls.ttl_check_mode = 1;
        }
    }
    else
    {   /*normal*/
        dsmpls.ttl_check_mode = 1;   /*only support global ttl check*/
    }

     /*  dsmpls.oam_check = (p_mpls_info->oam_en) ? TRUE : FALSE;*/
    update_oam = (mep_index == 0) ? 1 : 0;
    if (update_oam)     /* mep_index>0, means oam have configed */
    {
        dsmpls.oam_check_type       =   (p_mpls_info->oam_en) ? 1 : 0;
        dsmpls.mep_index            =   (p_mpls_info->oam_en) ? 0x1FFF : 0;
    }

    dsmpls.use_label_exp = (p_mpls_info->trust_outer_exp) ? FALSE : TRUE;
    p_gb_mpls_master[lchip]->write_ilm[p_mpls_info->type](lchip, p_mpls_info, &dsmpls);

    cmd = DRV_IOW(DsMpls_t, DRV_ENTRY_FLAG);


    sys_greatbelt_get_gchip_id(lchip, &gchip);

    if (update_oam)
    {
        dsmpls.oam_dest_chipid1_0   = (gchip&0x3);
        dsmpls.oam_dest_chipid3_2   = ((gchip>>2)&0x3);
        dsmpls.oam_dest_chipid4     = CTC_IS_BIT_SET(gchip, 4);
    }

    if(!dsmpls.label_space_valid)
    {
        dsmpls.ds_fwd_ptr = fwd_offset & 0xffff;
        SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "dsmpls.ds_fwd_ptr :%d\n",
                         dsmpls.ds_fwd_ptr);
    }
    else
    {
        SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "dsmpls.ds_fwd_ptr :%d used as label space id!\n",
                         dsmpls.ds_fwd_ptr);
    }

    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "key offset :%d\n",
                     p_mpls_info->key_offset);

    if (p_mpls_info->id_type & CTC_MPLS_ID_STATS)
    {
        dsmpls.stats_ptr =  p_mpls_info->stats_ptr;
    }

#if 0
    if (ds_mpls.next_hop_ptr_valid)
    {
        pkt_info->ds_fwd_ptr_valid = FALSE;
        pkt_info->next_hop_ptr_valid = TRUE;
        pkt_info->ad_next_hop_ptr = ds_mpls.ds_fwd_ptr;                       /* nextHopPtr[15:0] */
        pkt_info->ad_dest_map = ((ds_mpls.lm_base & 0x3F) << 16)              /* adDestMap[21:16] */
                             | (ds_mpls.oam_dest_chipid3_2 << 14)          /* adDestMap[15:14] */
                             | ds_mpls.mep_index;                          /* adDestMap[13:0] */
        pkt_info->ad_length_adjust_type = IS_BIT_SET(ds_mpls.lm_base, 13);    /* lengthAdjustType */
        pkt_info->ad_critical_packet = IS_BIT_SET(ds_mpls.lm_base, 12);       /* criticalPacket */
        pkt_info->ad_next_hop_ext = IS_BIT_SET(ds_mpls.lm_base, 11);          /* nextHopExt */
        pkt_info->ad_send_local_phy_port = IS_BIT_SET(ds_mpls.lm_base, 10);   /* sendLocalPhyPort */
        pkt_info->ad_aps_type = (ds_mpls.lm_base >> 8) & 0x3;                 /* apsType[1:0] */
        pkt_info->ad_speed = (ds_mpls.lm_base >> 6) & 0x3;                    /* speed[1:0] */
    }
#endif

    if(!p_gb_mpls_master[lchip]->lm_datapkt_fwd&&
        p_mpls_info->use_merged_nh&&
        !((CTC_MPLS_LABEL_TYPE_L3VPN == p_mpls_info->type)
          ||(CTC_MPLS_LABEL_TYPE_VPLS == p_mpls_info->type)))
    {
        dsmpls.next_hop_ptr_valid = TRUE;
        dsmpls.ds_fwd_ptr = p_nhinfo->dsnh_offset;                              /* nextHopPtr[15:0] */
        tmp_destmap = SYS_GREATBELT_BUILD_DESTMAP(0, p_nhinfo->dest_chipid, p_nhinfo->dest_id);
        dsmpls.lm_base = (tmp_destmap>>16)& 0x3F;                                      /* adDestMap[21:16] */
        dsmpls.oam_dest_chipid3_2 = (tmp_destmap>>14)& 0x3;                            /* adDestMap[15:14] */
        dsmpls.mep_index = tmp_destmap& 0x3FFF;                                  /* adDestMap[13:0] */
         /*SET_BIT(dsmpls.lm_base,13);                                                    // lengthAdjustType */

        /* nextHopExt */
        if(p_nhinfo->nexthop_ext)
        {
            SET_BIT(dsmpls.lm_base,11);
        }

        /* apsType[1:0] */
        if(p_nhinfo->aps_en)
        {
            dsmpls.lm_base |= (CTC_APS_BRIDGE &0x3)<<8;
        }

        /* lengthAdjustType */
        if(!p_mpls_info->pop)
        {
            SET_BIT(dsmpls.lm_base, 13);
        }

        /* speed[1:0] */
        if(sys_greatbelt_get_cut_through_en(lchip))
        {
            uint16 gport = 0;
            if(p_nhinfo->is_mcast)
            {
                tmp_speed = 3;
            }
            else if(!p_nhinfo->aps_en && (CTC_LINKAGG_CHIPID!= p_nhinfo->dest_chipid))
            {
                gport = SYS_GREATBELT_DESTMAP_TO_GPORT(tmp_destmap);
                tmp_speed = sys_greatbelt_get_cut_through_speed(lchip, gport);
            }
            else
            {
                tmp_speed   = 0;
            }

            dsmpls.mep_index |= (tmp_speed&0x3)<<6;
        }
    }
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_mpls_info->key_offset, cmd, &dsmpls));


    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_mpls_remove_ilm(uint8 lchip, sys_mpls_ilm_t* p_mpls_info)
{
    ds_mpls_t dsmpls;
    uint32 cmd;

    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_MPLS_DUMP_MPLS_DB_INFO(p_mpls_info);

    sal_memset(&dsmpls, 0, sizeof(ds_mpls_t));

    dsmpls.mpls_flow_policer_ptr = 0x1FFF;
    dsmpls.ds_fwd_ptr = 0xFFFF;

    cmd = DRV_IOW(DsMpls_t, DRV_ENTRY_FLAG);


    if (p_mpls_info->id_type & CTC_MPLS_ID_SERVICE)
    {
        CTC_ERROR_RETURN(_sys_greatbelt_mpls_remove_service(lchip, p_mpls_info));
    }

    DRV_IOCTL(lchip, p_mpls_info->key_offset, cmd, &dsmpls);


    return CTC_E_NONE;
}

/**
 @brief function of lookup mpls ilm information

 @param[in] pp_mpls_info, information used for lookup mpls ilm entry

 @param[out] pp_mpls_info, information of mpls ilm entry finded

 @return CTC_E_XXX
 */
STATIC int32
_sys_greatbelt_mpls_db_lookup(uint8 lchip, sys_mpls_ilm_t** pp_mpls_info)
{
    sys_mpls_ilm_t* p_mpls_info;
    uint32 offset = (*pp_mpls_info)->label;

    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    /* get mpls entry index */
    if ((*pp_mpls_info)->spaceid)
    {
        offset -= p_gb_mpls_master[lchip]->min_int_label;
    }

#if 0
    if(offset>p_gb_mpls_master[lchip]->min_label)
    {
        offset -= p_gb_mpls_master[lchip]->min_label;
    }
#endif

    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "    spaceid = %d , offset = %d\n",
                     (*pp_mpls_info)->spaceid, offset);

    p_mpls_info = ctc_vector_get(p_gb_mpls_master[lchip]->space[(*pp_mpls_info)->spaceid].p_vet, offset);
    if (NULL != p_mpls_info)
    {
        SYS_MPLS_DUMP_MPLS_DB_INFO(p_mpls_info);
    }
    else
    {
        SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "    don't find the p_mpls_info\n");
    }

    *pp_mpls_info = p_mpls_info;

    return CTC_E_NONE;
}

/**
 @brief function of add mpls ilm information

 @param[in] p_mpls_info, information should be maintained for mpls

 @return CTC_E_XXX
 */
STATIC int32
_sys_greatbelt_mpls_db_add(uint8 lchip, sys_mpls_ilm_t* p_mpls_info)
{
    uint32 offset = p_mpls_info->label;

    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_MPLS_DUMP_MPLS_DB_INFO(p_mpls_info);

    /* get mpls entry index */
    if (p_mpls_info->spaceid)
    {
        offset -= p_gb_mpls_master[lchip]->min_int_label;
    }

#if 0
    if(offset>p_gb_mpls_master[lchip]->min_label)
    {
        offset -= p_gb_mpls_master[lchip]->min_label;
    }
#endif

    p_mpls_info->key_offset = p_gb_mpls_master[lchip]->space[p_mpls_info->spaceid].base + offset;

    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "generate p_mpls_info->key_offset=%d\n", p_mpls_info->key_offset);

    if (FALSE == ctc_vector_add(p_gb_mpls_master[lchip]->space[p_mpls_info->spaceid].p_vet, offset, p_mpls_info))
    {
        return CTC_E_NO_MEMORY;
    }

    return CTC_E_NONE;
}

/**
 @brief function of remove mpls ilm information

 @param[in] p_ipuc_info, information maintained by mpls

 @return CTC_E_XXX
 */
STATIC int32
_sys_greatbelt_mpls_db_remove(uint8 lchip, sys_mpls_ilm_t* p_mpls_info)
{
    uint32 offset = p_mpls_info->label;

    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_MPLS_DUMP_MPLS_DB_INFO(p_mpls_info);

    /* get mpls entry index */
    if (p_mpls_info->spaceid)
    {
        offset -= p_gb_mpls_master[lchip]->min_int_label;
    }

#if 0
    if(offset>p_gb_mpls_master[lchip]->min_label)
    {
        offset -= p_gb_mpls_master[lchip]->min_label;
    }
#endif

    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "generate p_mpls_info->key_offset=%d", p_mpls_info->key_offset);
    ctc_vector_del(p_gb_mpls_master[lchip]->space[p_mpls_info->spaceid].p_vet, offset);

    return CTC_E_NONE;
}

/**
 @brief function of add mpls ilm entry

 @param[in] p_mpls_ilm, parameters used to add mpls ilm entry

 @return CTC_E_XXX
 */
int32
sys_greatbelt_mpls_add_ilm(uint8 lchip, ctc_mpls_ilm_t* p_mpls_ilm)
{

    uint16 stats_ptr = 0;
    sys_mpls_ilm_t *p_ilm_data;
    sys_mpls_ilm_t  ilm_data;
    sys_nh_info_dsnh_t nhinfo;
    uint32 fwd_offset = 0;
    int32 ret = 0;
    uint16 max_vrfid = 0;
    uint8 merged_nh = 0;

    SYS_MPLS_INIT_CHECK(lchip);

    CTC_PTR_VALID_CHECK(p_mpls_ilm);
    if((CTC_MPLS_LABEL_TYPE_VPWS!=p_mpls_ilm->type)&&(CTC_MPLS_LABEL_TYPE_VPLS!=p_mpls_ilm->type))
    {
        SYS_MPLS_NHID_EXTERNAL_VALID_CHECK(p_mpls_ilm->nh_id);
    }
    SYS_MPLS_ILM_SPACE_CHECK(p_mpls_ilm->spaceid);
    SYS_MPLS_ILM_LABEL_CHECK(p_mpls_ilm->spaceid, p_mpls_ilm->label);
    SYS_MPLS_ILM_DATA_CHECK(p_mpls_ilm);
    SYS_MPLS_ILM_DATA_MASK(p_mpls_ilm);

    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_MPLS_DUMP_ILM(p_mpls_ilm);

    if((p_gb_mpls_master[lchip]->min_label)&&(p_mpls_ilm->label>p_gb_mpls_master[lchip]->min_label))
    {
        p_mpls_ilm->label -= p_gb_mpls_master[lchip]->min_label;
    }

    /* prepare data */
    p_ilm_data = &ilm_data;
    SYS_MPLS_KEY_MAP(p_mpls_ilm, p_ilm_data);

    SYS_MPLS_LOCK;
    /* lookup for mpls entrise */
    CTC_ERROR_RETURN_MPLS_UNLOCK(_sys_greatbelt_mpls_db_lookup(lchip, &p_ilm_data));
    if (p_ilm_data) /* ilm have exist */
    {
        CTC_RETURN_MPLS_UNLOCK(CTC_E_ENTRY_EXIST);
    }

    sal_memset(&nhinfo, 0, sizeof(sys_nh_info_dsnh_t));

    /* check vrf id */
    if (CTC_MPLS_ID_VRF & p_mpls_ilm->id_type)
    {
        max_vrfid = sys_greatbelt_l3if_get_max_vrfid(lchip, MAX_CTC_IP_VER);
        if (p_mpls_ilm->flw_vrf_srv_aps.vrf_id >= max_vrfid)
        {
            CTC_RETURN_MPLS_UNLOCK(CTC_E_MPLS_VRF_ID_ERROR);
        }
    }

    /* get dsfwd offset or merged dsfwd to AD*/
    if (p_mpls_ilm->nh_id)
    {
        CTC_ERROR_RETURN_MPLS_UNLOCK(sys_greatbelt_nh_get_nhinfo(lchip, p_mpls_ilm->nh_id, &nhinfo));
        if(!p_gb_mpls_master[lchip]->lm_datapkt_fwd&&!nhinfo.dsfwd_valid && (nhinfo.merge_dsfwd == 1))
        {
            merged_nh = 1;
        }
    }

    p_ilm_data = mem_malloc(MEM_MPLS_MODULE, sizeof(sys_mpls_ilm_t));
    if (NULL == p_ilm_data)
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_ilm_data, 0, sizeof(sys_mpls_ilm_t));
    SYS_MPLS_KEY_MAP(p_mpls_ilm, p_ilm_data);
    SYS_MPLS_DATA_MAP(p_mpls_ilm, p_ilm_data);
    p_ilm_data->out_intf_spaceid = p_mpls_ilm->out_intf_spaceid;
    p_ilm_data->use_merged_nh = merged_nh;

    if (!merged_nh)
    {
        if ((CTC_MPLS_LABEL_TYPE_VPLS != p_mpls_ilm->type) || (p_gb_mpls_master[lchip]->decap_mode))
        {
            if ((TRUE != p_mpls_ilm->pop) || ((TRUE == p_mpls_ilm->pop) && (p_mpls_ilm->nh_id != 0)))
            {
                ret = sys_greatbelt_nh_get_dsfwd_offset(lchip, p_mpls_ilm->nh_id, &fwd_offset);
                if (CTC_E_NONE != ret)
                {
                    mem_free(p_ilm_data);
                    CTC_ERROR_RETURN_MPLS_UNLOCK(ret);
                }
            }
            else
            {
                /* drop nhid = 1 */
                ret = sys_greatbelt_nh_get_dsfwd_offset(lchip, 1, &fwd_offset);
                if (CTC_E_NONE != ret)
                {
                    mem_free(p_ilm_data);
                    CTC_ERROR_RETURN_MPLS_UNLOCK(ret);
                }
            }
        }
    }

    /* for mpls ecmp */
    if (nhinfo.ecmp_valid)
    {
        p_ilm_data->ecpn  = nhinfo.ecmp_num;
    }
    else
    {
        p_ilm_data->ecpn  = 0;
    }

    if (p_mpls_ilm->trust_outer_exp)
    {
        p_ilm_data->trust_outer_exp = TRUE;
    }

    /* for inner packet type */
    if ((CTC_MPLS_LABEL_TYPE_NORMAL == p_mpls_ilm->type)
        || (CTC_MPLS_LABEL_TYPE_L3VPN == p_mpls_ilm->type)
        || (CTC_MPLS_LABEL_TYPE_VPWS == p_mpls_ilm->type))
    {
        p_ilm_data->inner_pkt_type  = p_mpls_ilm->inner_pkt_type;
    }

    if(p_mpls_ilm->id_type&CTC_MPLS_ID_FLOW)
    {
        p_ilm_data->flow_id = p_mpls_ilm->flw_vrf_srv_aps.flow_id;
    }

    p_ilm_data->policer_id = p_mpls_ilm->policer_id;

    if(p_mpls_ilm->id_type&CTC_MPLS_ID_VRF)
    {
        p_ilm_data->vrf_id = p_mpls_ilm->flw_vrf_srv_aps.vrf_id;
        fwd_offset = p_ilm_data->vrf_id;

    }

    if(p_mpls_ilm->id_type&CTC_MPLS_ID_APS_SELECT)
    {
        p_ilm_data->aps_group_id = p_mpls_ilm->flw_vrf_srv_aps.aps_select_grp_id;
        p_ilm_data->aps_select_ppath = p_mpls_ilm->aps_select_protect_path;
    }

    if(p_mpls_ilm->stats_id)
    {
        /*alloc stats_ptr*/
        ret = sys_greatbelt_stats_get_statsptr(lchip, p_mpls_ilm->stats_id, &stats_ptr);
        /*for rollback*/
        if (CTC_E_NONE != ret)
        {
            p_ilm_data->stats_ptr = 0;
            mem_free(p_ilm_data);
            CTC_ERROR_RETURN_MPLS_UNLOCK(ret);
        }
        p_ilm_data->stats_ptr = stats_ptr;
        p_ilm_data->id_type |= CTC_MPLS_ID_STATS;
    }


    p_ilm_data->oam_en = p_mpls_ilm->oam_en;

    ret = _sys_greatbelt_mpls_db_add(lchip, p_ilm_data);
    if (ret)
    {
        mem_free(p_ilm_data);
        CTC_ERROR_RETURN_MPLS_UNLOCK(ret);
    }

    /* write mpls ilm entry */
    ret = _sys_greatbelt_mpls_write_ilm(lchip, p_ilm_data, fwd_offset, &nhinfo);
    if (ret)
    {
        _sys_greatbelt_mpls_db_remove(lchip, p_ilm_data);
        mem_free(p_ilm_data);
        CTC_ERROR_RETURN_MPLS_UNLOCK(ret);
    }

    SYS_MPLS_UNLOCK;

    return CTC_E_NONE;
}

int32
sys_greatbelt_mpls_update_ilm(uint8 lchip, ctc_mpls_ilm_t* p_mpls_ilm)
{
#if 1
    sys_mpls_ilm_t* p_ilm_data, ilm_data;
    uint32 cmd;
    uint32 use_label;
    ds_mpls_t dsmpls;

    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);


    SYS_MPLS_INIT_CHECK(lchip);

    CTC_PTR_VALID_CHECK(p_mpls_ilm);
    SYS_MPLS_DUMP_ILM(p_mpls_ilm);
    SYS_MPLS_ILM_SPACE_CHECK(p_mpls_ilm->spaceid);
    SYS_MPLS_ILM_LABEL_CHECK(p_mpls_ilm->spaceid, p_mpls_ilm->label);
    if (p_mpls_ilm->model >= CTC_MPLS_MAX_TUNNEL_MODE)
    {
        return CTC_E_INVALID_PARAM;
    }

    /* prepare data */
    p_ilm_data = &ilm_data;
    SYS_MPLS_KEY_MAP(p_mpls_ilm, p_ilm_data);

    sal_memset(&dsmpls, 0, sizeof(ds_mpls_t));

    if((p_gb_mpls_master[lchip]->min_label)&&(p_ilm_data->label>p_gb_mpls_master[lchip]->min_label))
    {
        p_ilm_data->label -= p_gb_mpls_master[lchip]->min_label;
    }

    SYS_MPLS_LOCK;
    /* lookup for mpls entrise */
    CTC_ERROR_RETURN_MPLS_UNLOCK(_sys_greatbelt_mpls_db_lookup(lchip, &p_ilm_data));
    if (p_ilm_data) /* ilm have exist */
    {
        if (p_mpls_ilm->model != p_ilm_data->model)
        {
            if (CTC_MPLS_TUNNEL_MODE_UNIFORM != p_mpls_ilm->model)
            {
                use_label = TRUE;
            }
            else
            {
                use_label = FALSE;
            }

            cmd = DRV_IOW(DsMpls_t, DsMpls_UseLabelTtl_f);


            CTC_ERROR_RETURN_MPLS_UNLOCK(DRV_IOCTL(lchip, p_ilm_data->key_offset, cmd, &use_label));


            cmd = DRV_IOW(DsMpls_t, DsMpls_UseLabelExp_f);


            CTC_ERROR_RETURN_MPLS_UNLOCK(DRV_IOCTL(lchip, p_ilm_data->key_offset, cmd, &use_label));


            p_ilm_data->model = p_mpls_ilm->model;
        }

        if(p_mpls_ilm->oam_en != p_ilm_data->oam_en)
        {

            cmd = DRV_IOR(DsMpls_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN_MPLS_UNLOCK(DRV_IOCTL(lchip, p_ilm_data->key_offset, cmd, &dsmpls));

            dsmpls.oam_check_type       =   (p_mpls_ilm->oam_en) ? 1 : 0;
            dsmpls.mep_index            =   (p_mpls_ilm->oam_en) ? 0x1FFF : 0;

            cmd = DRV_IOW(DsMpls_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN_MPLS_UNLOCK(DRV_IOCTL(lchip, p_ilm_data->key_offset, cmd, &dsmpls));

            p_ilm_data->oam_en = p_mpls_ilm->oam_en;
        }

        if (p_mpls_ilm->nh_id != p_ilm_data->nh_id)
        {
            /* get dsfwd offset or merged dsfwd to AD*/
            if (p_mpls_ilm->nh_id && !p_ilm_data->use_merged_nh)
            {
                sys_nh_info_dsnh_t nhinfo;
                uint32 fwd_offset;

                sal_memset(&nhinfo, 0, sizeof(sys_nh_info_dsnh_t));

                CTC_ERROR_RETURN_MPLS_UNLOCK(sys_greatbelt_nh_get_nhinfo(lchip, p_mpls_ilm->nh_id, &nhinfo));
                if(!p_gb_mpls_master[lchip]->lm_datapkt_fwd&&!nhinfo.dsfwd_valid && (nhinfo.merge_dsfwd == 1))
                {
                }
                else
                {
                    CTC_ERROR_RETURN_MPLS_UNLOCK(sys_greatbelt_nh_get_dsfwd_offset(lchip, p_mpls_ilm->nh_id, &fwd_offset));
                    cmd = DRV_IOR(DsMpls_t, DRV_ENTRY_FLAG);
                    CTC_ERROR_RETURN_MPLS_UNLOCK(DRV_IOCTL(lchip, p_ilm_data->key_offset, cmd, &dsmpls));

                    dsmpls.ds_fwd_ptr = fwd_offset & 0xffff;

                    cmd = DRV_IOW(DsMpls_t, DRV_ENTRY_FLAG);
                    CTC_ERROR_RETURN_MPLS_UNLOCK(DRV_IOCTL(lchip, p_ilm_data->key_offset, cmd, &dsmpls));
                    p_ilm_data->nh_id = p_mpls_ilm->nh_id;
                }
            }
        }

    }

    SYS_MPLS_UNLOCK;
#endif
    return CTC_E_NONE;
}

int32
sys_greatbelt_mpls_set_ilm_property(uint8 lchip, ctc_mpls_property_t* p_mpls_pro_info)
{

    sys_mpls_ilm_t* p_ilm_data = NULL;
    sys_mpls_ilm_t  ilm_data;
    uint32 update_type = 0;
    uint32 cmd      = 0;
    uint32 field    = 0;
    ctc_mpls_ilm_t* p_mpls_ilm = NULL;
    ds_mpls_t ds_mpls;

    SYS_MPLS_INIT_CHECK(lchip);

    sal_memset(&ilm_data, 0, sizeof(sys_mpls_ilm_t));
    sal_memset(&ds_mpls, 0, sizeof(ds_mpls_t));
    p_ilm_data = &ilm_data;

    p_ilm_data->label   = p_mpls_pro_info->label;
    p_ilm_data->spaceid = p_mpls_pro_info->space_id;

    update_type = p_mpls_pro_info->property_type;


    if((p_gb_mpls_master[lchip]->min_label)&&(p_ilm_data->label>p_gb_mpls_master[lchip]->min_label))
    {
        p_ilm_data->label -= p_gb_mpls_master[lchip]->min_label;
    }

    SYS_MPLS_LOCK;
    /* lookup for mpls entrise */
    CTC_ERROR_RETURN_MPLS_UNLOCK(_sys_greatbelt_mpls_db_lookup(lchip, &p_ilm_data));

    if(p_ilm_data)
    {
        switch(update_type)
        {
            case CTC_MPLS_ILM_DATA_DISCARD:
                /*  SYS_OAM_TP_NONE_TYPE,
                    SYS_OAM_TP_MEP_TYPE,
                    SYS_OAM_TP_MIP_TYPE,
                    SYS_OAM_TP_OAM_DISCARD_TYPE,
                    SYS_OAM_TP_TO_CPU_TYPE,
                    SYS_OAM_TP_DATA_DISCARD_TYPE
                */
                if ( (*((uint32 *)p_mpls_pro_info->value)) > 1 )
                {
                    CTC_ERROR_RETURN_MPLS_UNLOCK(CTC_E_INVALID_PARAM);
                }
                field = (*((uint32 *)p_mpls_pro_info->value)) ? 5 : 1;
                cmd = DRV_IOW(DsMpls_t, DsMpls_OamCheckType_f);

                CTC_ERROR_RETURN_MPLS_UNLOCK(DRV_IOCTL(lchip, p_ilm_data->key_offset, cmd, &field));

                break;

            case CTC_MPLS_ILM_QOS_DOMAIN:
                field = *((uint32 *)p_mpls_pro_info->value);
                if ( (field > 7) &&  (0xFF != field))
                {
                    CTC_ERROR_RETURN_MPLS_UNLOCK(CTC_E_INVALID_PARAM);
                }
                field = (0xFF == *((uint32 *)p_mpls_pro_info->value)) ? 0:(*((uint32 *)p_mpls_pro_info->value));
                cmd = DRV_IOW(DsMpls_t, DsMpls_QosDomain_f);
                CTC_ERROR_RETURN_MPLS_UNLOCK(DRV_IOCTL(lchip, p_ilm_data->key_offset, cmd, &field));
                field = (0xFF == *((uint32 *)p_mpls_pro_info->value)) ? 0:1;
                cmd = DRV_IOW(DsMpls_t, DsMpls_OverwriteQosDomain_f);

                CTC_ERROR_RETURN_MPLS_UNLOCK(DRV_IOCTL(lchip, p_ilm_data->key_offset, cmd, &field));


                break;

            case CTC_MPLS_ILM_APS_SELECT:
                p_mpls_ilm = (ctc_mpls_ilm_t *)(p_mpls_pro_info->value);


                cmd = DRV_IOR(DsMpls_t, DRV_ENTRY_FLAG);
                CTC_ERROR_RETURN_MPLS_UNLOCK(DRV_IOCTL(lchip, p_ilm_data->key_offset, cmd, &ds_mpls));
                /* no aps -> aps */
                if ((p_mpls_ilm->flw_vrf_srv_aps.aps_select_grp_id) != 0xFFFF)
                {
                    if ((p_mpls_ilm->flw_vrf_srv_aps.aps_select_grp_id) > 1023)
                    {
                        CTC_ERROR_RETURN_MPLS_UNLOCK(CTC_E_INVALID_PARAM);
                    }
                    /* check conflict */
                    if (CTC_FLAG_ISSET(p_ilm_data->id_type, CTC_MPLS_ID_FLOW))
                    {
                        if(ds_mpls.mpls_flow_policer_ptr)
                        {
                            SYS_MPLS_DUMP("ds_mpls.mpls_flow_policer_ptr is already used as policer_ptr!\n");
                            CTC_ERROR_RETURN_MPLS_UNLOCK(CTC_E_INVALID_PARAM);
                        }
                    }


                    ds_mpls.aps_select_valid  = 1;
                    ds_mpls.aps_select_protecting_path = (p_mpls_ilm->aps_select_protect_path) ? 1 : 0;
                    ds_mpls.policer_ptr_share_mode = 1;
                    ds_mpls.mpls_flow_policer_ptr = ((p_mpls_ilm->flw_vrf_srv_aps.aps_select_grp_id) & 0x1FFF);

                    p_ilm_data->aps_group_id = (p_mpls_ilm->flw_vrf_srv_aps.aps_select_grp_id) & 0x1FFF;
                    p_ilm_data->aps_select_ppath = (p_mpls_ilm->aps_select_protect_path) ? 1 : 0;
                    CTC_SET_FLAG(p_ilm_data->id_type, CTC_MPLS_ID_APS_SELECT);
                }
                else  /* aps -> no aps */
                {
                    if (!CTC_FLAG_ISSET(p_ilm_data->id_type, CTC_MPLS_ID_APS_SELECT))
                    {
                        SYS_MPLS_DUMP("This ilm entry is not aps entry!\n");
                        CTC_ERROR_RETURN_MPLS_UNLOCK(CTC_E_INVALID_PARAM);
                    }

                    ds_mpls.aps_select_valid  = 0;
                    ds_mpls.aps_select_protecting_path = 0;
                    ds_mpls.policer_ptr_share_mode = 0;
                    ds_mpls.mpls_flow_policer_ptr = (p_ilm_data->flow_id & 0x1FFF);

                    CTC_UNSET_FLAG(p_ilm_data->id_type, CTC_MPLS_ID_APS_SELECT);
                }

                cmd = DRV_IOW(DsMpls_t, DRV_ENTRY_FLAG);

                CTC_ERROR_RETURN_MPLS_UNLOCK(DRV_IOCTL(lchip, p_ilm_data->key_offset, cmd, &ds_mpls));


                break;

            case CTC_MPLS_ILM_LLSP:
                field = *((uint32 *)p_mpls_pro_info->value);
                cmd = DRV_IOR(DsMpls_t, DRV_ENTRY_FLAG);
                CTC_ERROR_RETURN_MPLS_UNLOCK(DRV_IOCTL(lchip, p_ilm_data->key_offset, cmd, &ds_mpls));
                if (0xFF == field)
                {
                    ds_mpls.llsp_valid          = 0;
                    ds_mpls.llsp_priority       = 0;
                }
                else
                {
                    if ( field > 7 )
                    {
                        CTC_ERROR_RETURN_MPLS_UNLOCK(CTC_E_INVALID_PARAM);
                    }
                    ds_mpls.overwrite_priority  = 1;
                    ds_mpls.llsp_valid          = 1;
                    ds_mpls.llsp_priority       = field;
                }
                cmd = DRV_IOW(DsMpls_t, DRV_ENTRY_FLAG);

                CTC_ERROR_RETURN_MPLS_UNLOCK(DRV_IOCTL(lchip, p_ilm_data->key_offset, cmd, &ds_mpls));

                break;
            default:
                break;
        }
    }

    SYS_MPLS_UNLOCK;

    return CTC_E_NONE;
}

/**
 @brief function of remove mpls ilm entry

 @param[in] p_ipuc_param, parameters used to remove mpls ilm entry

 @return CTC_E_XXX
 */
int32
sys_greatbelt_mpls_del_ilm(uint8 lchip, ctc_mpls_ilm_t* p_mpls_ilm)
{
    sys_mpls_ilm_t  ilm_data;
    sys_mpls_ilm_t* p_ilm_data;
    uint32 cmd;
    ds_mpls_t dsmpls;

    SYS_MPLS_INIT_CHECK(lchip);

    CTC_PTR_VALID_CHECK(p_mpls_ilm);
    /*    SYS_MPLS_NHID_EXTERNAL_VALID_CHECK(p_mpls_ilm->nh_id); */
    SYS_MPLS_ILM_SPACE_CHECK(p_mpls_ilm->spaceid);
    SYS_MPLS_ILM_LABEL_CHECK(p_mpls_ilm->spaceid, p_mpls_ilm->label);

    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_MPLS_DUMP_ILM(p_mpls_ilm);

    /* prepare data */
    sal_memset(&ilm_data,0,sizeof(sys_mpls_ilm_t));
    p_ilm_data = &ilm_data;
    SYS_MPLS_KEY_MAP(p_mpls_ilm, p_ilm_data);

    sal_memset(&dsmpls, 0, sizeof(ds_mpls_t));
    if((p_gb_mpls_master[lchip]->min_label)&&(p_ilm_data->label>p_gb_mpls_master[lchip]->min_label))
    {
        p_ilm_data->label -= p_gb_mpls_master[lchip]->min_label;
    }

    SYS_MPLS_LOCK;
    /* lookup for mpls entries */
    CTC_ERROR_RETURN_MPLS_UNLOCK(_sys_greatbelt_mpls_db_lookup(lchip, &p_ilm_data));
    if (!p_ilm_data)
    {
        CTC_RETURN_MPLS_UNLOCK(CTC_E_ENTRY_NOT_EXIST);
    }

#if 0
    if (p_ilm_data->nh_id != p_mpls_ilm->nh_id)
    {
        CTC_RETURN_MPLS_UNLOCK(CTC_E_ENTRY_NOT_EXIST);
    }

#endif

    /* write mpls ilm entry */
    _sys_greatbelt_mpls_remove_ilm(lchip, p_ilm_data);

    if (p_ilm_data->id_type & CTC_MPLS_ID_STATS)
    {


        cmd = DRV_IOR(DsMpls_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, p_ilm_data->key_offset, cmd, &dsmpls);

        /*0 indicates invalid*/
        dsmpls.stats_ptr = 0;

        cmd = DRV_IOW(DsMpls_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, p_ilm_data->key_offset, cmd, &dsmpls);

        p_ilm_data->stats_ptr = 0;


        p_ilm_data->id_type = CTC_MPLS_ID_NULL;
    }

    _sys_greatbelt_mpls_db_remove(lchip, p_ilm_data);

    mem_free(p_ilm_data);

    SYS_MPLS_UNLOCK;

    return CTC_E_NONE;
}

int32
sys_greatbelt_mpls_get_ilm(uint8 lchip, uint32 nh_id[SYS_GB_MAX_ECPN], ctc_mpls_ilm_t* p_mpls_ilm)
{
    uint8 i = 0;
    sys_mpls_ilm_t* p_ilm_data, ilm_data;

    SYS_MPLS_INIT_CHECK(lchip);

    CTC_PTR_VALID_CHECK(nh_id);
    CTC_PTR_VALID_CHECK(p_mpls_ilm);
    SYS_MPLS_ILM_SPACE_CHECK(p_mpls_ilm->spaceid);
    SYS_MPLS_ILM_LABEL_CHECK(p_mpls_ilm->spaceid, p_mpls_ilm->label);
    SYS_MPLS_DUMP_ILM(p_mpls_ilm);
    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "nh id[]:%d,%d,%d,%d,%d,%d,%d,%d\n",
                     nh_id[0], nh_id[1], nh_id[2], nh_id[3], nh_id[4], nh_id[5], nh_id[6], nh_id[7]);

    /* prepare data */
    p_ilm_data = &ilm_data;
    SYS_MPLS_KEY_MAP(p_mpls_ilm, p_ilm_data);

    if((p_gb_mpls_master[lchip]->min_label)&&(p_ilm_data->label>p_gb_mpls_master[lchip]->min_label))
    {
        p_ilm_data->label -= p_gb_mpls_master[lchip]->min_label;
    }

    SYS_MPLS_LOCK;
    /* lookup for ipuc entrise */
    CTC_ERROR_RETURN_MPLS_UNLOCK(_sys_greatbelt_mpls_db_lookup(lchip, &p_ilm_data));
    if (!p_ilm_data)
    {
        CTC_RETURN_MPLS_UNLOCK(CTC_E_ENTRY_NOT_EXIST);
    }


    if ((0 == p_ilm_data->nh_id) && (1 == p_ilm_data->pop))
    {
        p_ilm_data->nh_id = 1;
    }

    for (i = 0; i < SYS_GB_MAX_ECPN; i++)
    {
        nh_id[i] = CTC_INVLD_NH_ID;
    }

    if (0 != p_ilm_data->ecpn)
    {
        sys_nh_info_dsnh_t nh_info;

        sal_memset(&nh_info, 0, sizeof(sys_nh_info_dsnh_t));
        CTC_ERROR_RETURN_MPLS_UNLOCK(sys_greatbelt_nh_get_nhinfo(lchip, p_ilm_data->nh_id, &nh_info));
        if (TRUE != nh_info.ecmp_valid)
        {
            CTC_RETURN_MPLS_UNLOCK(CTC_E_ENTRY_NOT_EXIST);
        }

        for (i = 0; i < nh_info.valid_cnt; i++)
        {
            nh_id[i] = nh_info.nh_array[i];
        }
    }
    else
    {
        nh_id[0] = p_ilm_data->nh_id;
    }

    p_mpls_ilm->pwid = p_ilm_data->pwid;
    p_mpls_ilm->id_type = p_ilm_data->id_type;
    p_mpls_ilm->type = p_ilm_data->type;
    p_mpls_ilm->model = p_ilm_data->model;
    p_mpls_ilm->cwen = p_ilm_data->cwen;
    p_mpls_ilm->pop = p_ilm_data->pop;
    p_mpls_ilm->nh_id = p_ilm_data->nh_id;
    p_mpls_ilm->oam_en  = p_ilm_data->oam_en;
    p_mpls_ilm->logic_port_type  = p_ilm_data->logic_port_type;

    if(p_ilm_data->id_type & CTC_MPLS_ID_FLOW)
    {
        p_mpls_ilm->flw_vrf_srv_aps.flow_id = p_ilm_data->flow_id;
    }

    if(p_ilm_data->id_type & CTC_MPLS_ID_VRF)
    {
        p_mpls_ilm->flw_vrf_srv_aps.vrf_id = p_ilm_data->vrf_id;
    }

    if(p_ilm_data->id_type & CTC_MPLS_ID_APS_SELECT)
    {
        p_mpls_ilm->flw_vrf_srv_aps.aps_select_grp_id = p_ilm_data->aps_group_id;
        p_mpls_ilm->aps_select_protect_path = p_ilm_data->aps_select_ppath;
    }

    SYS_MPLS_UNLOCK;

    return CTC_E_NONE;
}

int32
sys_greatbelt_mpls_add_stats(uint8 lchip, ctc_mpls_stats_index_t* stats_index)
{

    uint16 stats_ptr = 0;
    uint32 cmd;
    int32 ret = CTC_E_NONE;
    ds_mpls_t dsmpls;
    sys_mpls_ilm_t* p_ilm_data, ilm_data;

    SYS_MPLS_INIT_CHECK(lchip);

    CTC_PTR_VALID_CHECK(stats_index);
    SYS_MPLS_ILM_SPACE_CHECK(stats_index->spaceid);
    SYS_MPLS_ILM_LABEL_CHECK(stats_index->spaceid, stats_index->label);

    /* prepare data */
    p_ilm_data = &ilm_data;
    p_ilm_data->label = stats_index->label;
    p_ilm_data->spaceid = stats_index->spaceid;

    if((p_gb_mpls_master[lchip]->min_label)&&(p_ilm_data->label>p_gb_mpls_master[lchip]->min_label))
    {
        p_ilm_data->label -= p_gb_mpls_master[lchip]->min_label;
    }

    SYS_MPLS_LOCK;
    /* lookup for mpls entrise */
    CTC_ERROR_RETURN_MPLS_UNLOCK(_sys_greatbelt_mpls_db_lookup(lchip, &p_ilm_data));
    if (!p_ilm_data)
    {
        CTC_RETURN_MPLS_UNLOCK(CTC_E_ENTRY_NOT_EXIST);
    }

    if (CTC_MPLS_ID_STATS & p_ilm_data->id_type)
    {
        CTC_RETURN_MPLS_UNLOCK(CTC_E_MPLS_ENTRY_STATS_EXIST);
    }


    /*alloc stats_ptr*/
    ret = sys_greatbelt_stats_get_statsptr(lchip, stats_index->stats_id, &stats_ptr);
    if (CTC_E_NONE != ret)
    {

    }

    p_ilm_data->stats_ptr = stats_ptr;


    /*for rollback*/
    if (CTC_E_NONE != ret)
    {

        p_ilm_data->stats_ptr = 0;


        CTC_ERROR_RETURN_MPLS_UNLOCK(ret);
    }


    cmd = DRV_IOR(DsMpls_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_MPLS_UNLOCK(DRV_IOCTL(lchip, p_ilm_data->key_offset, cmd, &dsmpls));

    dsmpls.stats_ptr = stats_ptr;

    cmd = DRV_IOW(DsMpls_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_MPLS_UNLOCK(DRV_IOCTL(lchip, p_ilm_data->key_offset, cmd, &dsmpls));


    p_ilm_data->id_type |= CTC_MPLS_ID_STATS;

    SYS_MPLS_UNLOCK;

    return CTC_E_NONE;
}

int32
sys_greatbelt_mpls_del_stats(uint8 lchip, ctc_mpls_stats_index_t* stats_index)
{
    uint32 cmd;
    ds_mpls_t dsmpls;
    sys_mpls_ilm_t* p_ilm_data, ilm_data;

    SYS_MPLS_INIT_CHECK(lchip);

    CTC_PTR_VALID_CHECK(stats_index);
    SYS_MPLS_ILM_SPACE_CHECK(stats_index->spaceid);
    SYS_MPLS_ILM_LABEL_CHECK(stats_index->spaceid, stats_index->label);

    /* prepare data */
    p_ilm_data = &ilm_data;
    p_ilm_data->label = stats_index->label;
    p_ilm_data->spaceid = stats_index->spaceid;
    if((p_gb_mpls_master[lchip]->min_label)&&(p_ilm_data->label>p_gb_mpls_master[lchip]->min_label))
    {
        p_ilm_data->label -= p_gb_mpls_master[lchip]->min_label;
    }

    SYS_MPLS_LOCK;
    /* lookup for mpls entrise */
    CTC_ERROR_RETURN_MPLS_UNLOCK(_sys_greatbelt_mpls_db_lookup(lchip, &p_ilm_data));
    if (!p_ilm_data)
    {
        CTC_RETURN_MPLS_UNLOCK(CTC_E_ENTRY_NOT_EXIST);
    }

    if (!(CTC_MPLS_ID_STATS & p_ilm_data->id_type))
    {
        CTC_RETURN_MPLS_UNLOCK(CTC_E_MPLS_ENTRY_STATS_NOT_EXIST);
    }


    cmd = DRV_IOR(DsMpls_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, p_ilm_data->key_offset, cmd, &dsmpls);

    /*0 indicates invalid*/
    dsmpls.stats_ptr = 0;

    cmd = DRV_IOW(DsMpls_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, p_ilm_data->key_offset, cmd, &dsmpls);

    p_ilm_data->stats_ptr = 0;


    p_ilm_data->id_type = CTC_MPLS_ID_NULL;

    SYS_MPLS_UNLOCK;

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_mpls_add_vpws_pw(uint8 lchip, ctc_mpls_l2vpn_pw_t* p_mpls_pw)
{
    ds_mpls_t dsmpls;
    sys_mpls_ilm_t* p_ilm_data = NULL;
    sys_mpls_ilm_t* p_ilm_sdata = NULL;
    sys_mpls_ilm_t  ilm_data;
    sys_mpls_ilm_t ilm_data_old;
    uint32 cmd;

    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_MPLS_DUMP_VPWS_PARAM(p_mpls_pw);

    /* prepare data */
    sal_memset(&ilm_data_old, 0, sizeof(sys_mpls_ilm_t));
    sal_memset(&ilm_data, 0, sizeof(sys_mpls_ilm_t));
    p_ilm_data = &ilm_data;
    p_ilm_data->spaceid = p_mpls_pw->space_id;
    p_ilm_data->label = p_mpls_pw->label;
    if((p_gb_mpls_master[lchip]->min_label)&&(p_ilm_data->label>p_gb_mpls_master[lchip]->min_label))
    {
        p_ilm_data->label -= p_gb_mpls_master[lchip]->min_label;
    }

    SYS_MPLS_LOCK;
    /* lookup for ipuc entrise */
    CTC_ERROR_RETURN_MPLS_UNLOCK(_sys_greatbelt_mpls_db_lookup(lchip, &p_ilm_data));
    if (p_ilm_data)
    {
        ilm_data_old.service_id = p_ilm_data->service_id;
        ilm_data_old.service_queue_en = p_ilm_data->service_queue_en;
        ilm_data_old.pwid = p_ilm_data->pwid;
        CTC_ERROR_RETURN_MPLS_UNLOCK(_sys_greatbelt_mpls_db_remove(lchip, p_ilm_data));
        mem_free(p_ilm_data);
    }

    {
        int32 ret = CTC_E_NONE;
        ctc_mpls_ilm_t  mpls_ilm;
        sal_memset(&mpls_ilm, 0, sizeof(ctc_mpls_ilm_t));
        mpls_ilm.type = CTC_MPLS_LABEL_TYPE_VPWS;
        mpls_ilm.spaceid = p_mpls_pw->space_id;
        mpls_ilm.label = p_mpls_pw->label;
        mpls_ilm.nh_id = p_mpls_pw->u.pw_nh_id;
        mpls_ilm.pwid = p_mpls_pw->logic_port;
        mpls_ilm.cwen = p_mpls_pw->cwen;
        mpls_ilm.flw_vrf_srv_aps.aps_select_grp_id = p_mpls_pw->aps_select_grp_id;
        mpls_ilm.aps_select_protect_path = p_mpls_pw->aps_select_protect_path;
        mpls_ilm.id_type = p_mpls_pw->id_type;
        mpls_ilm.stats_id = p_mpls_pw->stats_id;
        mpls_ilm.qos_use_outer_info = p_mpls_pw->qos_use_outer_info;
        mpls_ilm.oam_en         = p_mpls_pw->oam_en;
        mpls_ilm.trust_outer_exp = p_mpls_pw->trust_outer_exp;
        mpls_ilm.inner_pkt_type = p_mpls_pw->inner_pkt_type;

        SYS_MPLS_UNLOCK;
        ret = sys_greatbelt_mpls_add_ilm(lchip, &mpls_ilm);
        if (CTC_E_NONE != ret)
        {
            return ret;
        }

        SYS_MPLS_LOCK;
        p_ilm_sdata = &ilm_data;
        p_ilm_sdata->spaceid = p_mpls_pw->space_id;
        p_ilm_sdata->label = p_mpls_pw->label;
        if((p_gb_mpls_master[lchip]->min_label)&&(p_ilm_sdata->label>p_gb_mpls_master[lchip]->min_label))
        {
            p_ilm_sdata->label -= p_gb_mpls_master[lchip]->min_label;
        }
        CTC_ERROR_RETURN_MPLS_UNLOCK(_sys_greatbelt_mpls_db_lookup(lchip, &p_ilm_sdata));
        if (!p_ilm_sdata)
        {
            CTC_RETURN_MPLS_UNLOCK(CTC_E_ENTRY_NOT_EXIST);
        }

        p_ilm_data = p_ilm_sdata;

    }

    SYS_MPLS_UNLOCK;

    sal_memset(&dsmpls, 0, sizeof(dsmpls));
    cmd = DRV_IOR(DsMpls_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_ilm_data->key_offset, cmd, &dsmpls));
    dsmpls.svlan_tpid_index = p_mpls_pw->svlan_tpid_index;

    if (CTC_MPLS_L2VPN_RAW == (p_ilm_data->pw_mode = p_mpls_pw->pw_mode))
    {
        dsmpls.outer_vlan_is_cvlan = TRUE;
    }
    else
    {
        dsmpls.outer_vlan_is_cvlan = FALSE;
    }

    if(p_mpls_pw->cwen)
    {
        dsmpls.cw_exist = 1;     /* control word exist */
        dsmpls.offset_bytes = 2;
    }
    else
    {
        dsmpls.cw_exist = 0;
        dsmpls.offset_bytes = 1;
    }    
    dsmpls.logic_src_port = p_ilm_data->pwid = p_mpls_pw->logic_port;

    p_ilm_data->service_id            = p_mpls_pw->service_id;
    p_ilm_data->service_aclqos_enable = p_mpls_pw->service_aclqos_enable;
    p_ilm_data->service_policer_en    = p_mpls_pw->service_policer_en;
    p_ilm_data->service_queue_en      = p_mpls_pw->service_queue_en;

    p_ilm_data->cwen                  = p_mpls_pw->cwen;

    SYS_MPLS_DUMP_MPLS_DB_INFO(p_ilm_data);

    /* write to driver */
    cmd = DRV_IOW(DsMpls_t, DRV_ENTRY_FLAG);


    if (p_mpls_pw->service_queue_en)
    {
        p_ilm_data->id_type |= CTC_MPLS_ID_SERVICE;
    }
    else
    {
        p_ilm_data->id_type &= ~CTC_MPLS_ID_SERVICE;
    }

    CTC_ERROR_RETURN(_sys_greatbelt_mpls_add_service(lchip, p_ilm_data, &dsmpls,ilm_data_old));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_ilm_data->key_offset, cmd, &dsmpls));


    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_mpls_add_vpls_pw(uint8 lchip, ctc_mpls_l2vpn_pw_t* p_mpls_pw)
{
    ds_mpls_t dsmpls;
    sys_mpls_ilm_t * p_ilm_data = NULL;
    sys_mpls_ilm_t * p_ilm_sdata = NULL;
    sys_mpls_ilm_t ilm_data_old;
    sys_mpls_ilm_t ilm_data;
    uint32 cmd;

    /* prepare data */
    sal_memset(&ilm_data, 0, sizeof(sys_mpls_ilm_t));
    sal_memset(&ilm_data_old, 0, sizeof(sys_mpls_ilm_t));
    p_ilm_data = &ilm_data;
    p_ilm_data->spaceid = p_mpls_pw->space_id;
    p_ilm_data->label = p_mpls_pw->label;

    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_MPLS_DUMP_VPLS_PARAM(p_mpls_pw);
    SYS_L2_FID_CHECK(p_mpls_pw->u.vpls_info.fid);

    if((p_gb_mpls_master[lchip]->min_label)&&(p_ilm_data->label>p_gb_mpls_master[lchip]->min_label))
    {
        p_ilm_data->label -= p_gb_mpls_master[lchip]->min_label;
    }

    SYS_MPLS_LOCK;
    /* lookup for ipuc entrise */
    CTC_ERROR_RETURN_MPLS_UNLOCK(_sys_greatbelt_mpls_db_lookup(lchip, &p_ilm_data));
    if (p_ilm_data)
    {
        ilm_data_old.service_id = p_ilm_data->service_id;
        ilm_data_old.service_queue_en = p_ilm_data->service_queue_en;
        ilm_data_old.pwid = p_ilm_data->pwid;
        CTC_ERROR_RETURN_MPLS_UNLOCK(_sys_greatbelt_mpls_db_remove(lchip, p_ilm_data));
        mem_free(p_ilm_data);
    }

    {
        int32 ret = 0;
        ctc_mpls_ilm_t  mpls_ilm;
        sal_memset(&mpls_ilm, 0, sizeof(ctc_mpls_ilm_t));
        mpls_ilm.type = CTC_MPLS_LABEL_TYPE_VPLS;
        mpls_ilm.pwid = p_mpls_pw->logic_port;
        mpls_ilm.spaceid = p_mpls_pw->space_id;
        mpls_ilm.label = p_mpls_pw->label;
        mpls_ilm.cwen = p_mpls_pw->cwen;
        mpls_ilm.flw_vrf_srv_aps.aps_select_grp_id = p_mpls_pw->aps_select_grp_id;

        mpls_ilm.aps_select_protect_path = p_mpls_pw->aps_select_protect_path;
        mpls_ilm.logic_port_type = p_mpls_pw->u.vpls_info.logic_port_type;
        mpls_ilm.id_type    = p_mpls_pw->id_type;
        mpls_ilm.stats_id   = p_mpls_pw->stats_id;
        mpls_ilm.qos_use_outer_info = p_mpls_pw->qos_use_outer_info;
        mpls_ilm.oam_en = p_mpls_pw->oam_en;
        mpls_ilm.trust_outer_exp = p_mpls_pw->trust_outer_exp;
        mpls_ilm.inner_pkt_type = p_mpls_pw->inner_pkt_type;

        SYS_MPLS_UNLOCK;
        ret = sys_greatbelt_mpls_add_ilm(lchip, &mpls_ilm);
        if (CTC_E_NONE != ret)
        {
            return ret;
        }

        SYS_MPLS_LOCK;
        p_ilm_sdata = &ilm_data;
        p_ilm_sdata->spaceid = p_mpls_pw->space_id;
        p_ilm_sdata->label = p_mpls_pw->label;
        if((p_gb_mpls_master[lchip]->min_label)&&(p_ilm_sdata->label>p_gb_mpls_master[lchip]->min_label))
        {
            p_ilm_sdata->label -= p_gb_mpls_master[lchip]->min_label;
        }
        CTC_ERROR_RETURN_MPLS_UNLOCK(_sys_greatbelt_mpls_db_lookup(lchip, &p_ilm_sdata));
        if (!p_ilm_sdata)
        {
            CTC_RETURN_MPLS_UNLOCK(CTC_E_ENTRY_NOT_EXIST);
        }
        p_ilm_data = p_ilm_sdata;

    }

    SYS_MPLS_UNLOCK;

    sal_memset(&dsmpls, 0, sizeof(dsmpls));
    cmd = DRV_IOR(DsMpls_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_ilm_data->key_offset, cmd, &dsmpls));
    
    dsmpls.vsi_learning_disable = p_ilm_data->learn_disable = p_mpls_pw->learn_disable;
    dsmpls.igmp_snoop_en = p_ilm_data->igmp_snooping_enable = p_mpls_pw->igmp_snooping_enable;
    dsmpls.mac_security_vsi_discard = p_ilm_data->maclimit_enable = p_mpls_pw->maclimit_enable;
    dsmpls.ds_fwd_ptr = p_ilm_data->fid = p_mpls_pw->u.vpls_info.fid;
    dsmpls.logic_port_type = p_ilm_data->logic_port_type = p_mpls_pw->u.vpls_info.logic_port_type;
    dsmpls.logic_src_port = p_ilm_data->pwid = p_mpls_pw->logic_port;
    dsmpls.svlan_tpid_index =  p_mpls_pw->svlan_tpid_index;
    p_ilm_data->service_id            = p_mpls_pw->service_id;
    p_ilm_data->service_aclqos_enable = p_mpls_pw->service_aclqos_enable;
    p_ilm_data->service_policer_en    = p_mpls_pw->service_policer_en;
    p_ilm_data ->service_queue_en     = p_mpls_pw->service_queue_en;

    p_ilm_data->cwen                  = p_mpls_pw->cwen;
    p_ilm_data->qos_use_outer_info    = p_mpls_pw->qos_use_outer_info;

    if(p_mpls_pw->cwen)
    {
        dsmpls.cw_exist = 1;     /* control word exist */
        dsmpls.offset_bytes = 2;
    }
    else
    {
        dsmpls.cw_exist = 0;
        dsmpls.offset_bytes = 1;
    }

    /* qos use outer info update */
    if (p_mpls_pw->qos_use_outer_info)
    {
        dsmpls.aclqos_use_outer_info = 1;
    }
    else
    {
        dsmpls.aclqos_use_outer_info = 0;
    }


    if (CTC_MPLS_L2VPN_RAW == (p_ilm_data->pw_mode = p_mpls_pw->pw_mode))
    {
        dsmpls.outer_vlan_is_cvlan = TRUE;
    }
    else
    {
        dsmpls.outer_vlan_is_cvlan = FALSE;
    }

    SYS_MPLS_DUMP_MPLS_DB_INFO(p_ilm_data);

    /* write to driver */

    cmd = DRV_IOW(DsMpls_t, DRV_ENTRY_FLAG);


    if (p_mpls_pw->service_queue_en)
    {
        p_ilm_data->id_type |= CTC_MPLS_ID_SERVICE;
    }
    else
    {
        p_ilm_data->id_type &= ~CTC_MPLS_ID_SERVICE;
    }
    CTC_ERROR_RETURN(_sys_greatbelt_mpls_add_service(lchip, p_ilm_data, &dsmpls,ilm_data_old));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_ilm_data->key_offset, cmd, &dsmpls));


    return CTC_E_NONE;
}

/* for vpls and vpws */
STATIC int32
_sys_greatbelt_mpls_build_scl_key(uint8 lchip, ctc_mpls_l2vpn_pw_t* p_mpls_pw, sys_scl_entry_t* scl_entry)
{
    sys_mpls_ilm_t *p_ilm_data = NULL;
    sys_mpls_ilm_t  ilm_data;
    sal_memset(&ilm_data, 0, sizeof(sys_mpls_ilm_t));

    /* only care about customer id(PW label), label: 20bit lable | 3bit exp | 1bit sbit | 8bit ttl*/
    scl_entry->key.u.tcam_vlan_key.customer_id = (p_mpls_pw->label << 12);
    scl_entry->key.u.tcam_vlan_key.customer_id_mask = 0xFFFFF000;
    scl_entry->key.type    = SYS_SCL_KEY_TCAM_VLAN;
    scl_entry->action.type = SYS_SCL_ACTION_INGRESS;
    CTC_SET_FLAG(scl_entry->key.u.tcam_vlan_key.flag ,CTC_SCL_TCAM_VLAN_KEY_FLAG_CUSTOMER_ID);

    ilm_data.label = p_mpls_pw->label;
    ilm_data.spaceid = p_mpls_pw->space_id;
    p_ilm_data = &ilm_data;
    _sys_greatbelt_mpls_db_lookup(lchip, &p_ilm_data);
    if (p_ilm_data)
    {
        sys_nh_info_dsnh_t nhinfo;
        sal_memset(&nhinfo, 0, sizeof(sys_nh_info_dsnh_t));
        CTC_ERROR_RETURN(sys_greatbelt_nh_get_nhinfo(lchip, p_ilm_data->nh_id, &nhinfo));
        if(SYS_NH_TYPE_ILOOP == nhinfo.nh_entry_type)
        {
            scl_entry->key.u.tcam_vlan_key.gport =
                CTC_MAP_LPORT_TO_GPORT(nhinfo.dest_chipid, nhinfo.dsnh_offset&0x7F);
            scl_entry->key.u.tcam_vlan_key.gport_mask = 0xFFFFFFFF;
            CTC_SET_FLAG(scl_entry->key.u.tcam_vlan_key.flag , CTC_SCL_TCAM_VLAN_KEY_FLAG_GPORT);
        }
    }
    else
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    return CTC_E_NONE;
}

/* for vpls and vpws */
STATIC int32
_sys_greatbelt_mpls_build_scl_ad(uint8 lchip, ctc_mpls_l2vpn_pw_t* p_mpls_pw, ctc_scl_igs_action_t* pia)
{

    /* logic port and logic port type */
    CTC_SET_FLAG(pia->flag, CTC_SCL_IGS_ACTION_FLAG_LOGIC_PORT);
    pia->logic_port.logic_port  = p_mpls_pw->logic_port;
    pia->logic_port.logic_port_type = ((p_mpls_pw->u.vpls_info.logic_port_type) ? 1 : 0);

    if (p_mpls_pw->id_type & CTC_MPLS_ID_SERVICE)
    {
        CTC_SET_FLAG(pia->flag, CTC_SCL_IGS_ACTION_FLAG_SERVICE_ID);
        pia->service_id = p_mpls_pw->service_id;
    }

    if (p_mpls_pw->service_policer_en)
    {
        CTC_SET_FLAG(pia->sub_flag, CTC_SCL_IGS_ACTION_SUB_FLAG_SERVICE_POLICER_EN);
    }

    if (p_mpls_pw->service_aclqos_enable)
    {
        CTC_SET_FLAG(pia->sub_flag, CTC_SCL_IGS_ACTION_SUB_FLAG_SERVICE_ACL_EN);
    }

    if (p_mpls_pw->service_queue_en)
    {
        CTC_SET_FLAG(pia->sub_flag, CTC_SCL_IGS_ACTION_SUB_FLAG_SERVICE_QUEUE_EN);
    }

    if (p_mpls_pw->id_type & CTC_MPLS_ID_STATS)
    {
        CTC_SET_FLAG(pia->flag, CTC_SCL_IGS_ACTION_FLAG_STATS);
        pia->stats_id = p_mpls_pw->stats_id;
    }

    /* p_mpls_pw->cwen , not support this feature when use userid */
    if (p_mpls_pw->cwen)
    {
        return CTC_E_FEATURE_NOT_SUPPORT;
    }

    if (CTC_FLAG_ISSET(p_mpls_pw->flag, CTC_MPLS_ILM_FLAG_L2VPN_OAM))
    {
        CTC_MAX_VALUE_CHECK(p_mpls_pw->l2vpn_oam_id, 5118);/*USER_VLAN_PTR_RANGE*/
        pia->user_vlanptr = p_mpls_pw->l2vpn_oam_id;
        CTC_SET_FLAG(pia->flag, CTC_SCL_IGS_ACTION_FLAG_USER_VLANPTR);       
    }

    /* aps */
    else if (p_mpls_pw->id_type & CTC_MPLS_ID_APS_SELECT)
    {
        CTC_SET_FLAG(pia->flag, CTC_SCL_IGS_ACTION_FLAG_APS);
        CTC_APS_GROUP_ID_CHECK(p_mpls_pw->aps_select_grp_id);
        pia->aps.is_working_path = ((p_mpls_pw->aps_select_protect_path) ? 0 : 1);
        pia->aps.aps_select_group_id = p_mpls_pw->aps_select_grp_id;
        pia->user_vlanptr = SYS_SCL_MPLS_DECAP_VLAN_PTR;
    }

    if (CTC_MPLS_L2VPN_RAW == p_mpls_pw->pw_mode)
    {
        pia->vlan_edit.vlan_domain = CTC_SCL_VLAN_DOMAIN_CVLAN;
    }

    /* p_mpls_pw->pw_mode encapsule mode, will do by loopback port */
    /* p_mpls_pw->oam_en */
    /* p_mpls_pw->qos_use_outer_info */

    return CTC_E_NONE;
}


/* refer to _sys_greatbelt_vlan_mapping_build_igs_ad
   _sys_greatbelt_scl_map_igs_action in add entry
   _sys_greatbelt_scl_build_igs_action in write hw */
STATIC int32
_sys_greatbelt_mpls_build_vpls_scl_ad(uint8 lchip, ctc_mpls_l2vpn_pw_t* p_mpls_pw, ctc_scl_igs_action_t* pia)
{
    SYS_L2_FID_CHECK(p_mpls_pw->u.vpls_info.fid);

    /* fid */
    CTC_SET_FLAG(pia->flag, CTC_SCL_IGS_ACTION_FLAG_FID);
    pia->fid    = p_mpls_pw->u.vpls_info.fid;

    /* learning */
    CTC_SET_FLAG(pia->flag, CTC_SCL_IGS_ACTION_FLAG_VPLS);
    pia->vpls.learning_en  = ((p_mpls_pw->learn_disable)  ? 0 : 1);
    pia->vpls.mac_limit_en  = (p_mpls_pw->maclimit_enable ? 1 : 0);

    /* igmp snooping */
    if (p_mpls_pw->igmp_snooping_enable)
    {
        CTC_SET_FLAG(pia->flag, CTC_SCL_IGS_ACTION_FLAG_IGMP_SNOOPING);
    }

    return CTC_E_NONE;
}


STATIC int32
_sys_greatbelt_mpls_build_vpws_scl_ad(uint8 lchip, ctc_mpls_l2vpn_pw_t* p_mpls_pw, ctc_scl_igs_action_t* pia)
{
    CTC_SET_FLAG(pia->flag, CTC_SCL_IGS_ACTION_FLAG_REDIRECT);
    pia->nh_id      =  p_mpls_pw->u.pw_nh_id;;

    return CTC_E_NONE;
}


/**
 @brief function of add l2vpn pw entry

 @param[in] p_mpls_pw, parameters used to add l2vpn pw entry

 @return CTC_E_XXX
 */
int32
sys_greatbelt_mpls_add_l2vpn_pw(uint8 lchip, ctc_mpls_l2vpn_pw_t* p_mpls_pw)
{
    SYS_MPLS_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_mpls_pw);

    if ((CTC_MPLS_L2VPN_VPWS != p_mpls_pw->l2vpntype) &&
        (CTC_MPLS_L2VPN_VPLS != p_mpls_pw->l2vpntype))
    {
        return CTC_E_INVALID_PARAM;
    }
    if (!p_gb_mpls_master[lchip]->decap_mode)
    {
        /* greatbelt update the dsmpls for l2vpn */
        CTC_ERROR_RETURN(p_gb_mpls_master[lchip]->write_pw[p_mpls_pw->l2vpntype](lchip, p_mpls_pw));
    }
    else
    {
        sys_scl_entry_t scl_entry;
        sal_memset(&scl_entry, 0, sizeof(sys_scl_entry_t));

        CTC_ERROR_RETURN(_sys_greatbelt_mpls_build_scl_key(lchip, p_mpls_pw, &scl_entry));
        CTC_ERROR_RETURN(_sys_greatbelt_mpls_build_scl_ad(lchip, p_mpls_pw, &(scl_entry.action.u.igs_action)));
        if(CTC_MPLS_L2VPN_VPWS == p_mpls_pw->l2vpntype)
        {
            CTC_ERROR_RETURN(_sys_greatbelt_mpls_build_vpws_scl_ad(lchip, p_mpls_pw, &(scl_entry.action.u.igs_action)));

        }
        else if (CTC_MPLS_L2VPN_VPLS == p_mpls_pw->l2vpntype)
        {
            CTC_ERROR_RETURN(_sys_greatbelt_mpls_build_vpls_scl_ad(lchip, p_mpls_pw, &(scl_entry.action.u.igs_action)));
        }

        CTC_ERROR_RETURN(sys_greatbelt_scl_add_entry(lchip, SYS_SCL_GROUP_ID_INNER_CUSTOMER_ID, &scl_entry, 1));
        CTC_ERROR_RETURN(sys_greatbelt_scl_install_entry(lchip, scl_entry.entry_id, 1));

    }

    return CTC_E_NONE;
}

/**
 @brief function of remove l2vpn pw entry

 @param[in] label, vc label of the l2vpn pw entry

 @return CTC_E_XXX
 */
int32
sys_greatbelt_mpls_del_l2vpn_pw(uint8 lchip, ctc_mpls_l2vpn_pw_t* p_mpls_pw)
{
    sys_mpls_ilm_t* p_ilm_data = NULL;
    sys_mpls_ilm_t  ilm_data;
    uint32 cmd;
    ds_mpls_t dsmpls;

    CTC_PTR_VALID_CHECK(p_mpls_pw);

    sal_memset(&ilm_data, 0, sizeof(sys_mpls_ilm_t));

    if ((CTC_MPLS_L2VPN_VPWS != p_mpls_pw->l2vpntype) &&
        (CTC_MPLS_L2VPN_VPLS != p_mpls_pw->l2vpntype))
    {
        return CTC_E_INVALID_PARAM;
    }

    SYS_MPLS_INIT_CHECK(lchip);
    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    if (!p_gb_mpls_master[lchip]->decap_mode)
    {
    /* prepare data */
    p_ilm_data = &ilm_data;
    p_ilm_data->spaceid = p_mpls_pw->space_id;
    p_ilm_data->label = p_mpls_pw->label;

    if((p_gb_mpls_master[lchip]->min_label)&&(p_ilm_data->label>p_gb_mpls_master[lchip]->min_label))
    {
        p_ilm_data->label -= p_gb_mpls_master[lchip]->min_label;
    }

    sal_memset(&dsmpls, 0, sizeof(ds_mpls_t));

    SYS_MPLS_LOCK;
    /* lookup for mpls entrise */
    CTC_ERROR_RETURN_MPLS_UNLOCK(_sys_greatbelt_mpls_db_lookup(lchip, &p_ilm_data));
    if (!p_ilm_data)
    {
        CTC_RETURN_MPLS_UNLOCK(CTC_E_ENTRY_NOT_EXIST);
    }

    SYS_MPLS_UNLOCK;

    if (CTC_MPLS_LABEL_TYPE_VPLS == p_ilm_data->type)
    {
        SYS_MPLS_DUMP_VPLS_PARAM(p_mpls_pw);
    }
    else if (CTC_MPLS_LABEL_TYPE_VPWS == p_ilm_data->type)
    {
        SYS_MPLS_DUMP_VPWS_PARAM(p_mpls_pw);
    }
    else
    {
        SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "wrong! type isn't vpls & vpws!\n");
        return CTC_E_MPLS_LABEL_TYPE_ERROR;
    }

    /* write mpls ilm entry */
    _sys_greatbelt_mpls_remove_ilm(lchip, p_ilm_data);
    if (p_ilm_data->id_type & CTC_MPLS_ID_STATS)
    {


        cmd = DRV_IOR(DsMpls_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, p_ilm_data->key_offset, cmd, &dsmpls);

        /*0 indicates invalid*/
        dsmpls.stats_ptr = 0;

        cmd = DRV_IOW(DsMpls_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, p_ilm_data->key_offset, cmd, &dsmpls);

        p_ilm_data->stats_ptr = 0;


        p_ilm_data->id_type = CTC_MPLS_ID_NULL;
    }
    _sys_greatbelt_mpls_db_remove(lchip, p_ilm_data);
    mem_free(p_ilm_data);
    }
    else
    {
        sys_scl_entry_t scl_entry;
        sal_memset(&scl_entry, 0, sizeof(sys_scl_entry_t));
        _sys_greatbelt_mpls_build_scl_key(lchip, p_mpls_pw, &scl_entry);

        CTC_ERROR_RETURN(sys_greatbelt_scl_get_entry_id(lchip, &scl_entry, SYS_SCL_GROUP_ID_INNER_CUSTOMER_ID));
        CTC_ERROR_RETURN(sys_greatbelt_scl_uninstall_entry(lchip, scl_entry.entry_id, 1));
        CTC_ERROR_RETURN(sys_greatbelt_scl_remove_entry(lchip, scl_entry.entry_id, 1));
    }
    return CTC_E_NONE;
}


int32
sys_greatbelt_mpls_init_entries(uint8 lchip)
{

    ds_mpls_t dsmpls;
    uint32 i;
    uint32 cmd;
    uint32 entry_num = 0;

    LCHIP_CHECK(lchip);
    sal_memset(&dsmpls, 0, sizeof(ds_mpls_t));

    dsmpls.mpls_flow_policer_ptr = 0x1FFF;
    dsmpls.ds_fwd_ptr = 0xFFFF;

    CTC_ERROR_RETURN(sys_greatbelt_ftm_get_table_entry_num(lchip, DsMpls_t, &entry_num));
    if (0 == entry_num)
    {
        return CTC_E_NONE;
    }

    cmd = DRV_IOW(DsMpls_t, DRV_ENTRY_FLAG);

    /* ilm entries */
#if 0
    for (i = 0; i < CTC_MPLS_SPACE_NUMBER; i++)
    {
        if (p_gb_mpls_master[lchip]->space[i].enable)
        {
            for (j = 0; j < p_gb_mpls_master[lchip]->space[i].size; j++)
            {

                CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_gb_mpls_master[lchip]->space[i].base + j, cmd, &dsmpls));

            }
        }
    }
#endif

    for (i = 0; i < entry_num; i++)
    {

        CTC_ERROR_RETURN(DRV_IOCTL(lchip,i, cmd, &dsmpls));

    }

    return CTC_E_NONE;
}


int32
sys_greatbelt_mpls_init(uint8 lchip, ctc_mpls_init_t* p_mpls_info)
{

    ds_mpls_ctl_t mpls_ctl;
    ipe_mpls_ctl_t  ipe_mpls_ctl;
    uint32 cmdr, cmdw;
    uint16 i;
    uint32 base;
    uint16 block_num;
    uint32 ftm_mpls_num = 0;
    uint32 user_mpls_num = 0;
     /* uint8 sizetype = 0;*/
    q_mgr_enq_length_adjust_t q_mgr_enq_length_adjust;

    LCHIP_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_mpls_info);

    /* get num of mpls resource */
    sys_greatbelt_ftm_get_table_entry_num(lchip, DsMpls_t, &ftm_mpls_num);
    if (0 == ftm_mpls_num)
    {
        return CTC_E_NONE;
    }

     /*  _sys_greatbelt_mpls_get_sizetype(lchip, mpls_entry_num / SYS_MPLS_TBL_BLOCK_SIZE, &sizetype);*/
    p_mpls_info->space_info[0].sizetype = p_mpls_info->space_info[0].sizetype;

    for (i = 0; i < CTC_MPLS_SPACE_NUMBER; i++)
    {
        if (p_mpls_info->space_info[i].enable)
        {
            user_mpls_num +=SYS_MPLS_TBL_BLOCK_SIZE * (1 << p_mpls_info->space_info[i].sizetype);
        }
    }

    #if 0
    if(user_mpls_num>ftm_mpls_num)
    {
        SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "ftm_mpls_num  0x%x  user_mpls_num 0x%x\n", ftm_mpls_num,user_mpls_num);
        return CTC_E_MPLS_SPACE_NO_RESOURCE;
    }
    #endif

    /* config  QMgrEnqLengthAdjust_t for mpls label switch shapping */
    sal_memset(&q_mgr_enq_length_adjust, 0, sizeof(q_mgr_enq_length_adjust_t));
    q_mgr_enq_length_adjust.adjust_length1 = 18;
    cmdw = DRV_IOW(QMgrEnqLengthAdjust_t, DRV_ENTRY_FLAG);

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmdw, &q_mgr_enq_length_adjust));


    /* check the parameter */
    CTC_PTR_VALID_CHECK(p_mpls_info);

    for (i = 0; i < CTC_MPLS_SPACE_NUMBER; i++)
    {
        if (p_mpls_info->space_info[i].enable &&
            p_mpls_info->space_info[i].sizetype > 15)
        {
            return CTC_E_INVALID_PARAM;
        }
    }

    p_gb_mpls_master[lchip] = mem_malloc(MEM_MPLS_MODULE, sizeof(sys_mpls_master_t));
    if (NULL == p_gb_mpls_master[lchip])
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_gb_mpls_master[lchip], 0, sizeof(sys_mpls_master_t));

    /* global space */
    if (p_mpls_info->min_interface_label > 0)
    {
        /* 64 label num for global space */
        p_mpls_info->space_info[0].enable = 1;
        p_mpls_info->space_info[0].sizetype = 0;

        p_mpls_info->min_interface_label = (p_mpls_info->min_interface_label < 64) ? 64 : p_mpls_info->min_interface_label;
    }

    if (p_mpls_info->space_info[0].sizetype > 3)
    {
        block_num = 1 << (p_mpls_info->space_info[0].sizetype - 3);
    }
    else
    {
        block_num = 1;
    }

    cmdr = DRV_IOR(IpeMplsCtl_t, DRV_ENTRY_FLAG);
    cmdw = DRV_IOW(IpeMplsCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmdr, &ipe_mpls_ctl));
    p_gb_mpls_master[lchip]->space[0].base = ipe_mpls_ctl.label_base_global << 8;
    /* p_gb_mpls_master[lchip]->space[0].size = SYS_MPLS_TBL_BLOCK_SIZE * (1 << p_mpls_info->space_info[0].sizetype); */
     /* p_gb_mpls_master[lchip]->space[0].size = mpls_entry_num;*/
    p_gb_mpls_master[lchip]->space[0].size =  SYS_MPLS_TBL_BLOCK_SIZE * (1 << p_mpls_info->space_info[0].sizetype);
    p_gb_mpls_master[lchip]->space[0].p_vet = ctc_vector_init(block_num, SYS_MPLS_TBL_BLOCK_SIZE * 8);
    if (NULL == p_gb_mpls_master[lchip]->space[0].p_vet)
    {
        return CTC_E_NO_MEMORY;
    }

    ipe_mpls_ctl.label_base_global = 0;
    ipe_mpls_ctl.label_base_global_mcast = 0;
    ipe_mpls_ctl.label_space_size_type_global_mcast = ipe_mpls_ctl.label_space_size_type_global = p_mpls_info->space_info[0].sizetype;   /*(upper layer sizetype unit is 256 ,but asic layer sizetype unit is 64 )*/

     /* problem,   p_mpls_info->space_info[0]=2, 1<<2=4,size = 256*4=1024, min_interface_label = 1<<(2*4)=256 ?*/
    ipe_mpls_ctl.min_interface_label_mcast = ipe_mpls_ctl.min_interface_label = p_mpls_info->min_interface_label;


    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmdw, &ipe_mpls_ctl));



    p_gb_mpls_master[lchip]->decap_mode = 0;
    p_gb_mpls_master[lchip]->space[0].enable = p_mpls_info->space_info[0].enable;
    p_gb_mpls_master[lchip]->min_label = 0;
    p_gb_mpls_master[lchip]->label_range_mode = 0;

    sal_memset(&mpls_ctl, 0, sizeof(ds_mpls_ctl_t));
    mpls_ctl.interface_label_valid = 0;
    cmdw = DRV_IOW(DsMplsCtl_t, DRV_ENTRY_FLAG);


    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmdw, &mpls_ctl));


    base = ipe_mpls_ctl.label_base_global + p_gb_mpls_master[lchip]->space[0].size;

    /*  added  by cawwj */
#if  0
    p_mpls_info->space_info[1].enable = 1;
    p_mpls_info->space_info[1].sizetype = 2;

    p_mpls_info->space_info[2].enable = 1;
    p_mpls_info->space_info[2].sizetype = 2;
#endif

    /* interface space */
    for (i = 1; i < CTC_MPLS_SPACE_NUMBER; i++)
    {
        sal_memset(&mpls_ctl, 0, sizeof(ds_mpls_ctl_t));

        if (p_mpls_info->space_info[i].enable)
        {
            if (p_mpls_info->space_info[i].sizetype > 3)
            {
                block_num = 1 << (p_mpls_info->space_info[i].sizetype - 3);
            }
            else
            {
                block_num = 1;
            }

            p_gb_mpls_master[lchip]->space[i].p_vet = ctc_vector_init(block_num, SYS_MPLS_TBL_BLOCK_SIZE * 8);
            if (NULL == p_gb_mpls_master[lchip]->space[i].p_vet)
            {
                return CTC_E_NO_MEMORY;
            }

            p_gb_mpls_master[lchip]->space[i].base = base;
            p_gb_mpls_master[lchip]->space[i].size = SYS_MPLS_TBL_BLOCK_SIZE * (1 << p_mpls_info->space_info[i].sizetype);
            p_gb_mpls_master[lchip]->space[i].enable = TRUE;

            if ((base / SYS_MPLS_TBL_BLOCK_SIZE) > 127)
            {
                return CTC_E_INVALID_PARAM;
            }

            mpls_ctl.interface_label_valid_mcast= mpls_ctl.interface_label_valid = TRUE;
            mpls_ctl.label_base_mcast = mpls_ctl.label_base = base / SYS_MPLS_TBL_BLOCK_SIZE;
            mpls_ctl.label_space_size_type_mcast= mpls_ctl.label_space_size_type = p_mpls_info->space_info[i].sizetype;

            base += p_gb_mpls_master[lchip]->space[i].size;
        }
        else
        {
            p_gb_mpls_master[lchip]->space[i].enable = FALSE;

            mpls_ctl.interface_label_valid = FALSE;
            mpls_ctl.label_base = 0;
            mpls_ctl.label_space_size_type = 0;
        }


        CTC_ERROR_RETURN(DRV_IOCTL(lchip, i, cmdw, &mpls_ctl));

    }

    /* min interface label, the label littler than it must be a global label */
    /* pls_master[lchip]->min_int_label = 1<<ipe_mpls_ctl.min_interface_label; */
    p_gb_mpls_master[lchip]->min_int_label = ipe_mpls_ctl.min_interface_label;
    p_gb_mpls_master[lchip]->lm_datapkt_fwd = 0;

    /* call back function */
    p_gb_mpls_master[lchip]->write_ilm[CTC_MPLS_LABEL_TYPE_NORMAL] = _sys_greatbelt_mpls_ilm_normal;
    p_gb_mpls_master[lchip]->write_ilm[CTC_MPLS_LABEL_TYPE_L3VPN] = _sys_greatbelt_mpls_ilm_l3vpn;
    p_gb_mpls_master[lchip]->write_ilm[CTC_MPLS_LABEL_TYPE_VPWS] = _sys_greatbelt_mpls_ilm_vpws;
    p_gb_mpls_master[lchip]->write_ilm[CTC_MPLS_LABEL_TYPE_VPLS] = _sys_greatbelt_mpls_ilm_vpls;

#if 0
    p_gb_mpls_master[lchip]->write_ac[CTC_MPLS_L2VPN_VPWS] = _sys_greatbelt_mpls_add_vpws_ac;
    p_gb_mpls_master[lchip]->write_ac[CTC_MPLS_L2VPN_VPLS] = _sys_greatbelt_mpls_add_vpls_ac;
#endif

    p_gb_mpls_master[lchip]->write_pw[CTC_MPLS_L2VPN_VPWS] = _sys_greatbelt_mpls_add_vpws_pw;
    p_gb_mpls_master[lchip]->write_pw[CTC_MPLS_L2VPN_VPLS] = _sys_greatbelt_mpls_add_vpls_pw;
    CTC_ERROR_RETURN(sys_greatbelt_nh_get_max_external_nhid(lchip, &p_gb_mpls_master[lchip]->max_external_nhid));

    SYS_MPLS_CREAT_LOCK;

    sys_greatbelt_mpls_init_entries(lchip);

    return CTC_E_NONE;
}

int32
sys_greatbelt_mpls_init_min_interface_label(uint8 lchip, ctc_mpls_init_t* p_mpls_info)
{

    ds_mpls_ctl_t mpls_ctl;
    ipe_mpls_ctl_t  ipe_mpls_ctl;
    uint32 cmdr, cmdw;
    uint16 i;
    uint32 base;
    uint16 block_num;
    uint32 ftm_mpls_num = 0;

    SYS_MPLS_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_mpls_info);

    /* get num of mpls resource */
    sys_greatbelt_ftm_get_table_entry_num(lchip, DsMpls_t, &ftm_mpls_num);
    if (0 == ftm_mpls_num)
    {
        return CTC_E_NONE;
    }

    p_mpls_info->space_info[0].sizetype = p_mpls_info->space_info[0].sizetype;

    for (i = 0; i < CTC_MPLS_SPACE_NUMBER; i++)
    {
        if (p_mpls_info->space_info[i].enable &&
            p_mpls_info->space_info[i].sizetype > 15)
        {
            return CTC_E_INVALID_PARAM;
        }
    }

    /* global space */
    if (p_mpls_info->min_interface_label > 0)
    {
        /* 64 label num for global space */
        p_mpls_info->space_info[0].enable = 1;
        p_mpls_info->space_info[0].sizetype = 0;

        p_mpls_info->min_interface_label = (p_mpls_info->min_interface_label < 64) ? 64 : p_mpls_info->min_interface_label;
    }

    if (p_mpls_info->space_info[0].sizetype > 3)
    {
        block_num = 1 << (p_mpls_info->space_info[0].sizetype - 3);
    }
    else
    {
        block_num = 1;
    }

    cmdr = DRV_IOR(IpeMplsCtl_t, DRV_ENTRY_FLAG);
    cmdw = DRV_IOW(IpeMplsCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmdr, &ipe_mpls_ctl));
    p_gb_mpls_master[lchip]->space[0].base = ipe_mpls_ctl.label_base_global << 8;
    p_gb_mpls_master[lchip]->space[0].size =  SYS_MPLS_TBL_BLOCK_SIZE * (1 << p_mpls_info->space_info[0].sizetype);

    if (NULL != p_gb_mpls_master[lchip]->space[0].p_vet)
    {
        ctc_vector_release(p_gb_mpls_master[lchip]->space[0].p_vet);
    }
    p_gb_mpls_master[lchip]->space[0].p_vet = ctc_vector_init(block_num, SYS_MPLS_TBL_BLOCK_SIZE * 8);
    if (NULL == p_gb_mpls_master[lchip]->space[0].p_vet)
    {
        return CTC_E_NO_MEMORY;
    }

    ipe_mpls_ctl.label_base_global = 0;
    ipe_mpls_ctl.label_base_global_mcast = 0;
    ipe_mpls_ctl.label_space_size_type_global_mcast = ipe_mpls_ctl.label_space_size_type_global = p_mpls_info->space_info[0].sizetype;   /*(upper layer sizetype unit is 256 ,but asic layer sizetype unit is 64 )*/
    ipe_mpls_ctl.min_interface_label_mcast = ipe_mpls_ctl.min_interface_label = p_mpls_info->min_interface_label;


    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmdw, &ipe_mpls_ctl));



    p_gb_mpls_master[lchip]->decap_mode = 0;
    p_gb_mpls_master[lchip]->space[0].enable = p_mpls_info->space_info[0].enable;
    p_gb_mpls_master[lchip]->min_label = 0;
    p_gb_mpls_master[lchip]->label_range_mode = 0;

    sal_memset(&mpls_ctl, 0, sizeof(ds_mpls_ctl_t));
    mpls_ctl.interface_label_valid = 0;
    cmdw = DRV_IOW(DsMplsCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmdw, &mpls_ctl));

    base = ipe_mpls_ctl.label_base_global + p_gb_mpls_master[lchip]->space[0].size;


    /* interface space */
    for (i = 1; i < CTC_MPLS_SPACE_NUMBER; i++)
    {
        sal_memset(&mpls_ctl, 0, sizeof(ds_mpls_ctl_t));

        if (p_mpls_info->space_info[i].enable)
        {
            if (p_mpls_info->space_info[i].sizetype > 3)
            {
                block_num = 1 << (p_mpls_info->space_info[i].sizetype - 3);
            }
            else
            {
                block_num = 1;
            }

            if (NULL != p_gb_mpls_master[lchip]->space[i].p_vet)
            {
                ctc_vector_release(p_gb_mpls_master[lchip]->space[i].p_vet);
            }
            p_gb_mpls_master[lchip]->space[i].p_vet = ctc_vector_init(block_num, SYS_MPLS_TBL_BLOCK_SIZE * 8);
            if (NULL == p_gb_mpls_master[lchip]->space[i].p_vet)
            {
                return CTC_E_NO_MEMORY;
            }

            p_gb_mpls_master[lchip]->space[i].base = base;
            p_gb_mpls_master[lchip]->space[i].size = SYS_MPLS_TBL_BLOCK_SIZE * (1 << p_mpls_info->space_info[i].sizetype);
            p_gb_mpls_master[lchip]->space[i].enable = TRUE;

            if ((base / SYS_MPLS_TBL_BLOCK_SIZE) > 127)
            {
                return CTC_E_INVALID_PARAM;
            }

            mpls_ctl.interface_label_valid_mcast= mpls_ctl.interface_label_valid = TRUE;
            mpls_ctl.label_base_mcast = mpls_ctl.label_base = base / SYS_MPLS_TBL_BLOCK_SIZE;
            mpls_ctl.label_space_size_type_mcast= mpls_ctl.label_space_size_type = p_mpls_info->space_info[i].sizetype;

            base += p_gb_mpls_master[lchip]->space[i].size;
        }
        else
        {
            p_gb_mpls_master[lchip]->space[i].enable = FALSE;

            mpls_ctl.interface_label_valid = FALSE;
            mpls_ctl.label_base = 0;
            mpls_ctl.label_space_size_type = 0;
        }


        CTC_ERROR_RETURN(DRV_IOCTL(lchip, i, cmdw, &mpls_ctl));

    }

    /* min interface label, the label littler than it must be a global label */
    p_gb_mpls_master[lchip]->min_int_label = ipe_mpls_ctl.min_interface_label;
    p_gb_mpls_master[lchip]->lm_datapkt_fwd = 0;


    return CTC_E_NONE;
}


STATIC int32
_sys_greatbelt_mpls_free_node_data(void* node_data, void* user_data)
{
    mem_free(node_data);

    return CTC_E_NONE;
}

int32
sys_greatbelt_mpls_deinit(uint8 lchip)
{
    uint16 i;

    LCHIP_CHECK(lchip);
    if (NULL == p_gb_mpls_master[lchip])
    {
        return CTC_E_NONE;
    }

    /*free space vector data*/
    for (i = 0; i < CTC_MPLS_SPACE_NUMBER; i++)
    {
        if (p_gb_mpls_master[lchip]->space[i].enable)
        {
            ctc_vector_traverse(p_gb_mpls_master[lchip]->space[i].p_vet, (vector_traversal_fn)_sys_greatbelt_mpls_free_node_data, NULL);
            ctc_vector_release(p_gb_mpls_master[lchip]->space[i].p_vet);
        }
    }

    sal_mutex_destroy(p_gb_mpls_master[lchip]->mutex);
    mem_free(p_gb_mpls_master[lchip]);

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_show_ilm_info(uint8 lchip, sys_mpls_ilm_t* p_ilm_data, uint8 space_id, uint32 label_id)
{

    if (!p_ilm_data)
    {
        return CTC_E_NONE;
    }

    if (((0xFF == space_id) && (0xFFFFFFFF == label_id)) ||
        ((space_id == p_ilm_data->spaceid) && (label_id == p_ilm_data->label)))
    {
        if ((0 == p_ilm_data->nh_id) && (1 == p_ilm_data->pop))
        {
            p_ilm_data->nh_id = 1;
        }

        SYS_MPLS_DUMP("%-7d %-10d", p_ilm_data->spaceid, p_ilm_data->label);

        switch (p_ilm_data->type)
        {
        case  CTC_MPLS_LABEL_TYPE_NORMAL:     /* This label is a normal label */
            SYS_MPLS_DUMP("%-7s %-6d %-8d %-8d", "-",
                          p_ilm_data->ecpn, p_ilm_data->key_offset, p_ilm_data->nh_id);
            break;

        case  CTC_MPLS_LABEL_TYPE_L3VPN:       /* This label is a l3vpn VC label */
            SYS_MPLS_DUMP("%-7s %-6d %-8d %-8d",  "L3VPN",
                          p_ilm_data->ecpn, p_ilm_data->key_offset, p_ilm_data->nh_id);
            break;

        case  CTC_MPLS_LABEL_TYPE_VPWS:        /* This label is a vpws VC label */
            SYS_MPLS_DUMP("%-7s %-6d %-8d %-8d", "VPWS",
                          p_ilm_data->ecpn, p_ilm_data->key_offset, p_ilm_data->nh_id);
            break;

        case  CTC_MPLS_LABEL_TYPE_VPLS:       /* This label is a vpls VC label */
            SYS_MPLS_DUMP("%-7s %-6d %-8d %-8d", "VPLS",
                          p_ilm_data->ecpn, p_ilm_data->key_offset, p_ilm_data->nh_id);
            break;

        default:
            break;
        }

        if (p_ilm_data->type == CTC_MPLS_LABEL_TYPE_VPLS)
        {
            SYS_MPLS_DUMP("%-7d", p_ilm_data->pwid);
        }
        else
        {
            SYS_MPLS_DUMP("-      ");
        }

        if (p_ilm_data->type == CTC_MPLS_LABEL_TYPE_L3VPN)
        {
            SYS_MPLS_DUMP("%-8d", p_ilm_data->vrf_id);
        }
        else
        {
            SYS_MPLS_DUMP("-       ");
        }

        if (p_ilm_data->type == CTC_MPLS_LABEL_TYPE_NORMAL ||
            p_ilm_data->type == CTC_MPLS_LABEL_TYPE_L3VPN)
        {
            if (p_ilm_data->model == CTC_MPLS_TUNNEL_MODE_UNIFORM)
            {
                SYS_MPLS_DUMP("U      ");
            }
            else if (p_ilm_data->model == CTC_MPLS_TUNNEL_MODE_SHORT_PIPE)
            {
                SYS_MPLS_DUMP("S      ");
            }
            else
            {
                SYS_MPLS_DUMP("P      ");
            }

            SYS_MPLS_DUMP("-      ");
        }
        else
        {
            SYS_MPLS_DUMP("P      ");
            if (p_ilm_data->cwen)
            {
                SYS_MPLS_DUMP("U      ");
            }
            else
            {
                SYS_MPLS_DUMP("-      ");
            }
        }

        if ((p_ilm_data->type == CTC_MPLS_LABEL_TYPE_VPLS) || (p_ilm_data->type == CTC_MPLS_LABEL_TYPE_VPWS))
        {
            if (CTC_MPLS_L2VPN_TAGGED == p_ilm_data->pw_mode)
            {
                SYS_MPLS_DUMP("TAGGED      ");
            }
            else if (CTC_MPLS_L2VPN_RAW == p_ilm_data->pw_mode)
            {
                SYS_MPLS_DUMP("RAW         ");
            }
        }
        else
        {
            SYS_MPLS_DUMP("-           ");
        }

        if (p_ilm_data->type == CTC_MPLS_LABEL_TYPE_VPLS)
        {
            if (1 == p_ilm_data->logic_port_type)
            {
                SYS_MPLS_DUMP("VPLS       ");
            }
            else
            {
                SYS_MPLS_DUMP("HVPLS      ");
            }
        }
        else
        {
            SYS_MPLS_DUMP("-       ");
        }

        SYS_MPLS_DUMP("\n\r");

    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_mpls_ilm_show(uint8 lchip, uint8 space_id, uint32 label_id)
{
    int i, size;
    uint32 index = 0;
    uint32 label=0;
    sys_mpls_ilm_t* p_mpls_info = NULL;

    SYS_MPLS_INIT_CHECK(lchip);
    ctc_debug_set_flag("mpls", "mpls", 1, CTC_DEBUG_LEVEL_INFO, TRUE);

    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Mode : U-Uniform     S-Short pipe     P-Pipe\n\r");
    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Space   Label     Type    Ecpn   Offset   NHID    PW     VRF     Mode   CW     PW_MODE     VPLS_TYPE\n\r");
    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "-----------------------------------------------------------------------------------------------------\n\r");

    if(p_gb_mpls_master[lchip]->end_label0)
    {
        for(index=0;index<=p_gb_mpls_master[lchip]->end_label0;index++)
        {
            p_mpls_info = ctc_vector_get(p_gb_mpls_master[lchip]->space[0].p_vet, index);
            if (NULL != p_mpls_info)
            {
                SYS_MPLS_DUMP_MPLS_DB_INFO(p_mpls_info);
                _sys_greatbelt_show_ilm_info(lchip, p_mpls_info, space_id, label_id);
            }
        }

        if(p_gb_mpls_master[lchip]->start_label1)
        {
            for(index=p_gb_mpls_master[lchip]->start_label1;index<=p_gb_mpls_master[lchip]->end_label1;index++)
            {
                if((p_gb_mpls_master[lchip]->min_label)&&(index>p_gb_mpls_master[lchip]->min_label))
                {
                    label = index-p_gb_mpls_master[lchip]->min_label;
                }
                else
                {
                    label = index;
                }
                p_mpls_info = ctc_vector_get(p_gb_mpls_master[lchip]->space[0].p_vet, label);
                if (NULL != p_mpls_info)
                {
                    if((p_gb_mpls_master[lchip]->min_label)&&(index>p_gb_mpls_master[lchip]->min_label))
                    {
                        p_mpls_info->label += p_gb_mpls_master[lchip]->min_label;
                        SYS_MPLS_DUMP_MPLS_DB_INFO(p_mpls_info);
                        _sys_greatbelt_show_ilm_info(lchip, p_mpls_info, space_id, label_id);
                        p_mpls_info->label -= p_gb_mpls_master[lchip]->min_label;
                    }
                    else
                    {
                        SYS_MPLS_DUMP_MPLS_DB_INFO(p_mpls_info);
                        _sys_greatbelt_show_ilm_info(lchip, p_mpls_info, space_id, label_id);
                    }

                }
            }
        }

        if(p_gb_mpls_master[lchip]->start_label2)
        {
            for(index=p_gb_mpls_master[lchip]->start_label2;index<=p_gb_mpls_master[lchip]->end_label2;index++)
            {
                if((p_gb_mpls_master[lchip]->min_label)&&(index>p_gb_mpls_master[lchip]->min_label))
                {
                    label = index-p_gb_mpls_master[lchip]->min_label;
                }
                else
                {
                    label = index;
                }
                p_mpls_info = ctc_vector_get(p_gb_mpls_master[lchip]->space[0].p_vet, label);
                if (NULL != p_mpls_info)
                {
                    if((p_gb_mpls_master[lchip]->min_label)&&(index>p_gb_mpls_master[lchip]->min_label))
                    {
                        p_mpls_info->label += p_gb_mpls_master[lchip]->min_label;
                        SYS_MPLS_DUMP_MPLS_DB_INFO(p_mpls_info);
                        _sys_greatbelt_show_ilm_info(lchip, p_mpls_info, space_id, label_id);
                        p_mpls_info->label -= p_gb_mpls_master[lchip]->min_label;
                    }
                    else
                    {
                        SYS_MPLS_DUMP_MPLS_DB_INFO(p_mpls_info);
                        _sys_greatbelt_show_ilm_info(lchip, p_mpls_info, space_id, label_id);
                    }
                }
            }
        }
    }
    else
    {
        for (i = 0; i < CTC_MPLS_SPACE_NUMBER; i++)
        {
            if (p_gb_mpls_master[lchip]->space[i].enable)
            {
                size = p_gb_mpls_master[lchip]->space[i].size;

                for (index = 0; index < size; index++)
                {
                    p_mpls_info = ctc_vector_get(p_gb_mpls_master[lchip]->space[i].p_vet, index);
                    if (NULL != p_mpls_info)
                    {
                        SYS_MPLS_DUMP_MPLS_DB_INFO(p_mpls_info);
                        _sys_greatbelt_show_ilm_info(lchip, p_mpls_info, space_id, label_id);
                    }
                }
            }
        }
    }

    ctc_debug_set_flag("mpls", "mpls", 1, CTC_DEBUG_LEVEL_INFO, FALSE);
    return CTC_E_NONE;
}

uint32 g_ilm_count = 0;

STATIC int32
_sys_greatbelt_get_ilm_count(uint8 lchip, sys_mpls_ilm_t* p_ilm_data, void* data)
{
    if (p_ilm_data != NULL)
    {
        g_ilm_count++;
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_show_mpls_status(uint8 lchip)
{
    uint32 i;
    uint32 count = 0;
    SYS_MPLS_INIT_CHECK(lchip);

    if(p_gb_mpls_master[lchip]->decap_mode)
    {
        SYS_MPLS_DUMP("Decap mode is %d\n\r", p_gb_mpls_master[lchip]->decap_mode);
    }
    SYS_MPLS_DUMP("The minimum interface space label: %d\n\r", p_gb_mpls_master[lchip]->min_int_label);
    SYS_MPLS_DUMP("Space         Base          Size          Ilm count\n\r");
    SYS_MPLS_DUMP("--------------------------------------------------\n");

    for (i = 0; i < CTC_MPLS_SPACE_NUMBER; i++)
    {
        if (p_gb_mpls_master[lchip]->space[i].enable)
        {
            g_ilm_count = 0;
            ctc_vector_traverse(p_gb_mpls_master[lchip]->space[i].p_vet, (vector_traversal_fn)_sys_greatbelt_get_ilm_count, &count);
            SYS_MPLS_DUMP("%-14d%-14d%-14d%-19d\n\r", i, p_gb_mpls_master[lchip]->space[i].base ,p_gb_mpls_master[lchip]->space[i].size, g_ilm_count);
        }
    }

    return CTC_E_NONE;
}

int32
_sys_greatbelt_mpls_for_tp_oam(uint8 lchip, uint32 gal_check)
{
    uint32 cmd = 0;
    uint32 field_val = 0;

    cmd = DRV_IOW(DsMpls_t, DsMpls_OamCheckType_f);

    field_val = (gal_check > 0) ? 1 : 0;


    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 13, cmd, &field_val));


    return CTC_E_NONE;
}


int32
sys_greatbelt_mpls_set_decap_mode(uint8 lchip, uint8 mode)
{
    SYS_MPLS_INIT_CHECK(lchip);
    p_gb_mpls_master[lchip]->decap_mode = (mode ? 1 : 0);

    return CTC_E_NONE;
}

int32
sys_greatbelt_mpls_calc_real_label_for_label_range(uint8 lchip, uint8 space_id, uint32 in_label,uint32 *out_label)
{
    uint32 offset = 0;

    SYS_MPLS_INIT_CHECK(lchip);

    if((p_gb_mpls_master[lchip]->label_range_mode)&&(in_label > p_gb_mpls_master[lchip]->min_label))
    {
        offset = (in_label-p_gb_mpls_master[lchip]->min_label);
    }
    else
    {
        offset = in_label;
    }

    /* get mpls entry index */
    if (space_id)
    {
        offset -= p_gb_mpls_master[lchip]->min_int_label;
    }
    *out_label = p_gb_mpls_master[lchip]->space[space_id].base + offset;

    return CTC_E_NONE;
}



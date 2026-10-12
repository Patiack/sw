/**
 @file sys_greatbelt_nexthop_l3.c

 @date 2009-11-23

 @version v2.0

 The file contains all nexthop layer2 related callback function
*/

/****************************************************************************
 *
* Header Files
*
****************************************************************************/
#include "ctc_const.h"
#include "ctc_macro.h"
#include "ctc_error.h"
#include "ctc_linklist.h"
#include "ctc_l3if.h"

#include "sys_greatbelt_chip.h"
#include "sys_greatbelt_nexthop_api.h"
#include "sys_greatbelt_nexthop.h"
#include "sys_greatbelt_vlan.h"
#include "sys_greatbelt_stats.h"
#include "sys_greatbelt_queue_enq.h"
#include "sys_greatbelt_l3if.h"

#include "sys_greatbelt_cpu_reason.h"
#include "sys_greatbelt_register.h"

#include "greatbelt/include/drv_io.h"
#include "greatbelt/include/drv_tbl_reg.h"

#define SYS_NH_DSMET_BITMAP_MAX_PORT_ID   56

extern int32 sys_greatbelt_stacking_get_enable(uint8 lchip, bool* enable, uint32* stacking_mcast_offset);
extern int32 sys_greatbelt_stacking_get_mcast_profile_met_offset(uint8 lchip,  uint16 mcast_profile_id, uint16 *p_stacking_met_offset);

extern int32 sys_greatbelt_stacking_get_rsv_trunk_number(uint8 lchip, uint8* number);

/****************************************************************************
 *
* Defines and Macros
*
*****************************************************************************/
#define SYS_NH_MCAST_REPLI_INCR_STEP   4
#define SYS_NH_LOGICAL_REPLI_MAX_NUM   32

#define SYS_NH_MET_END_REPLICATE_OFFSET  0x7FFF
#define SYS_NH_MET_DROP_UCAST_ID_UPPER    0xF
#define SYS_NH_MET_DROP_UCAST_ID_LOWER    0xFFF
#define SYS_NH_MET_DROP_UCAST_ID          0x7FFF
#define SYS_NH_PHY_REP_MAX_NUM             127

struct sys_nh_mcast_dsmet_io_s
{
    sys_nh_param_mcast_member_t* p_mem_param;
    uint32 met_offset;
    uint32 next_met_offset;
    uint32 dsnh_offset;
    uint8 use_pbm;
    sys_nh_mcast_meminfo_t* p_next_mem;
};
typedef struct sys_nh_mcast_dsmet_io_s sys_nh_mcast_dsmet_io_t;

/****************************************************************************
 *
* Global and Declaration
*
*****************************************************************************/

/****************************************************************************
 *
* Function
*
*****************************************************************************/

STATIC int32
_sys_greatbelt_nh_mcast_analyze_member(uint8 lchip, ctc_list_pointer_t* p_db_member_list,
                                       sys_nh_param_mcast_member_t* p_member,
                                       sys_nh_mcast_meminfo_t** pp_mem_node,
                                       bool* p_entry_exit,
                                       uint32* p_repli_pos)
{

    sys_nh_mcast_meminfo_t* p_meminfo;
    ctc_list_pointer_node_t* p_pos_mem;
    uint8 lport = 0;
    uint8 index = 0;

    *pp_mem_node = NULL;
    *p_entry_exit = FALSE;
    *p_repli_pos = 0;
    if ((p_member->member_type != SYS_NH_PARAM_MCAST_MEM_LOCAL_WITH_NH) &&
        (p_member->member_type != SYS_NH_PARAM_BRGMC_MEM_LOCAL_WITH_APS_BRIDGE))
    {
        p_member->is_logic_port = 0;
    }

    CTC_LIST_POINTER_LOOP(p_pos_mem, p_db_member_list)
    {
        p_meminfo = _ctc_container_of(p_pos_mem, sys_nh_mcast_meminfo_t, list_head);

        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "p_meminfo::member_type:%d, dsmet.flag:0x%x, bmp0:0x%x\n",
             p_meminfo->dsmet.member_type,p_meminfo->dsmet.flag, p_meminfo->dsmet.pbm0);
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "p_member:: member_type:%d ,destid:%d\n ",
            p_member->member_type, p_member->destid);

        if ((p_meminfo->dsmet.member_type != p_member->member_type))
        {
            if(((SYS_NH_PARAM_BRGMC_MEM_LOCAL_WITH_APS_BRIDGE == p_meminfo->dsmet.member_type)
                ||(SYS_NH_PARAM_BRGMC_MEM_LOCAL_WITH_APS_BRIDGE == p_member->member_type))
                &&(p_meminfo->dsmet.ref_nhid == p_member->ref_nhid))
            {

            }
            else
            {
                continue;
            }
        }

        switch (p_meminfo->dsmet.member_type)
        {
        case  SYS_NH_PARAM_BRGMC_MEM_LOCAL_WITH_APS_BRIDGE:
        case    SYS_NH_PARAM_MCAST_MEM_LOCAL_WITH_NH:
            if (p_meminfo->dsmet.ref_nhid == p_member->ref_nhid)
            {
                *pp_mem_node = p_meminfo;
                *p_entry_exit = TRUE;
                return CTC_E_NONE;
            }

            break;

        case SYS_NH_PARAM_MCAST_MEM_LOCAL_WITH_NH_DESTID:
        if (!p_member->is_logic_port && CTC_FLAG_ISSET(p_meminfo->dsmet.flag, SYS_NH_DSMET_FLAG_USE_PBM)
            && CTC_FLAG_ISSET(p_meminfo->dsmet.flag, SYS_NH_DSMET_FLAG_IS_LINKAGG) == p_member->is_linkagg
            && (lport < SYS_NH_DSMET_BITMAP_MAX_PORT_ID)
            && (p_meminfo->dsmet.ref_nhid == p_member->ref_nhid))
            {
                *pp_mem_node = p_meminfo;
                *p_entry_exit = FALSE;
                if (lport < 32 && CTC_IS_BIT_SET(p_meminfo->dsmet.pbm0, lport))
                {
                    *p_entry_exit = TRUE;
                }
                else if ((lport >= 32) && (lport < SYS_NH_DSMET_BITMAP_MAX_PORT_ID) && CTC_IS_BIT_SET(p_meminfo->dsmet.pbm1, (lport - 32)))
                {
                    *p_entry_exit = TRUE;
                }
            }
            else if (
                CTC_FLAG_ISSET(p_meminfo->dsmet.flag, SYS_NH_DSMET_FLAG_IS_LINKAGG) == p_member->is_linkagg &&
                p_meminfo->dsmet.ucastid == p_member->destid &&
                CTC_FLAG_ISSET(p_meminfo->dsmet.flag, SYS_NH_DSMET_FLAG_LOGIC_PORT_CHK) == p_member->is_logic_port &&
                p_meminfo->dsmet.logic_port == p_member->logic_port)
            {
                *pp_mem_node = p_meminfo;
                *p_entry_exit = TRUE;
                return CTC_E_NONE;
            }

            break;
        case SYS_NH_PARAM_MCAST_MEM_MIRROR_WITH_NH:
            if ((p_meminfo->dsmet.ref_nhid == p_member->ref_nhid)
                && (p_meminfo->dsmet.ucastid == p_member->destid)
            && (CTC_FLAG_ISSET(p_meminfo->dsmet.flag, SYS_NH_DSMET_FLAG_IS_LINKAGG) == p_member->is_linkagg))
            {
                *pp_mem_node = p_meminfo;
                *p_entry_exit = TRUE;
                return CTC_E_NONE;
            }

            break;
        case SYS_NH_PARAM_BRGMC_MEM_LOCAL:
        case SYS_NH_PARAM_BRGMC_MEM_RAPS:
            lport = p_member->destid & 0xFF;
            if (!p_member->is_logic_port && CTC_FLAG_ISSET(p_meminfo->dsmet.flag, SYS_NH_DSMET_FLAG_USE_PBM)
                && CTC_FLAG_ISSET(p_meminfo->dsmet.flag, SYS_NH_DSMET_FLAG_IS_LINKAGG) == p_member->is_linkagg
                && lport < SYS_NH_DSMET_BITMAP_MAX_PORT_ID)
            {      /*check port/linkagg bitmap  */
                *pp_mem_node = p_meminfo;
                *p_entry_exit = FALSE;
                if (lport < 32 && CTC_IS_BIT_SET(p_meminfo->dsmet.pbm0, lport))
                {

                    *p_entry_exit = TRUE;

                }
                else if ((lport >= 32) && (lport < SYS_NH_DSMET_BITMAP_MAX_PORT_ID) && CTC_IS_BIT_SET(p_meminfo->dsmet.pbm1, (lport - 32)))
                {

                    *p_entry_exit = TRUE;
                }

                return CTC_E_NONE;
            }
            else if (
                CTC_FLAG_ISSET(p_meminfo->dsmet.flag, SYS_NH_DSMET_FLAG_IS_LINKAGG) == p_member->is_linkagg &&
                p_meminfo->dsmet.ucastid == p_member->destid &&
                CTC_FLAG_ISSET(p_meminfo->dsmet.flag, SYS_NH_DSMET_FLAG_LOGIC_PORT_CHK) == p_member->is_logic_port &&
                p_meminfo->dsmet.logic_port == p_member->logic_port)
            {
                *pp_mem_node = p_meminfo;
                *p_entry_exit = TRUE;
                return CTC_E_NONE;
            }

            break;

        case SYS_NH_PARAM_MCAST_MEM_REMOTE:

            if ((CTC_FLAG_ISSET(p_meminfo->dsmet.flag, SYS_NH_DSMET_FLAG_IS_LINKAGG) == p_member->is_linkagg) &&
                (p_meminfo->dsmet.ucastid == p_member->destid))
            {
                *pp_mem_node = p_meminfo;
                *p_entry_exit = TRUE;
                return CTC_E_NONE;
            }

            break;

        case  SYS_NH_PARAM_IPMC_MEM_LOCAL:
            lport = p_member->destid & 0xFF;
            if (CTC_FLAG_ISSET(p_meminfo->dsmet.flag, SYS_NH_DSMET_FLAG_USE_PBM)
                && (p_meminfo->dsmet.vid == p_member->vid)
                && CTC_FLAG_ISSET(p_meminfo->dsmet.flag, SYS_NH_DSMET_FLAG_IS_LINKAGG) == p_member->is_linkagg
                && lport < SYS_NH_DSMET_BITMAP_MAX_PORT_ID)
            {
                lport = p_member->destid & 0xFF;
                 /* check local phy port bitmap*/
                *pp_mem_node = p_meminfo;
                *p_entry_exit = FALSE;
                if (lport < 32 && CTC_IS_BIT_SET(p_meminfo->dsmet.pbm0, lport))
                {
                    *p_entry_exit = TRUE;
                }
                else if ((lport >= 32) && (lport < SYS_NH_DSMET_BITMAP_MAX_PORT_ID) && CTC_IS_BIT_SET(p_meminfo->dsmet.pbm1, (lport - 32)))
                {

                    *p_entry_exit = TRUE;
                }

                return CTC_E_NONE;
            }
            else
            {
                if ((CTC_FLAG_ISSET(p_meminfo->dsmet.flag, SYS_NH_DSMET_FLAG_IS_LINKAGG) == p_member->is_linkagg)
                    && (p_meminfo->dsmet.ucastid == p_member->destid)
                    && (p_meminfo->dsmet.vid == p_member->vid))
                {
                    *pp_mem_node = p_meminfo;
                    *p_entry_exit = TRUE;
                    return CTC_E_NONE;
                }
            }

            break;

        case SYS_NH_PARAM_IPMC_MEM_LOCAL_LOGIC_REP:  /* logic replication  */
            /* logic replication  */
            if ((CTC_FLAG_ISSET(p_meminfo->dsmet.flag, SYS_NH_DSMET_FLAG_IS_LINKAGG) == p_member->is_linkagg)
                && (p_meminfo->dsmet.ucastid == p_member->destid))
            {
                if ((p_meminfo->dsmet.replicate_num + 1) < SYS_NH_LOGICAL_REPLI_MAX_NUM)
                {
                    *pp_mem_node = p_meminfo;
                }

                if (p_meminfo->dsmet.replicate_num == 0)
                {
                    if (p_meminfo->dsmet.vid == p_member->vid)
                    {
                        *p_entry_exit = TRUE;
                        *pp_mem_node = p_meminfo;
                        return CTC_E_NONE;
                    }
                }
                else
                {
                    for (index = 0; index < (p_meminfo->dsmet.replicate_num + 1); index++)
                    {
                        if (p_meminfo->dsmet.vid_list[index] == p_member->vid)
                        {
                            *p_repli_pos = index;
                            *p_entry_exit = TRUE;
                            *pp_mem_node = p_meminfo;
                            return CTC_E_NONE;
                        }
                    }
                }
            }

            break;

        case SYS_NH_PARAM_MCAST_MEM_LOCAL_LOGIC_REP:  /* logic replication  */
            /* logic replication  */
            if ((CTC_FLAG_ISSET(p_meminfo->dsmet.flag, SYS_NH_DSMET_FLAG_IS_LINKAGG) == p_member->is_linkagg)
                && (p_meminfo->dsmet.ucastid == p_member->destid))
            {
                 uint16 offset = 0;
                 uint8 use_dsnh8w = 0;
                 uint8 max_num = 0;
                 CTC_ERROR_RETURN(sys_greatbelt_nh_get_dsnh_offset_by_nhid(lchip, p_member->ref_nhid, &offset, &use_dsnh8w));

                max_num = use_dsnh8w?(SYS_NH_LOGICAL_REPLI_MAX_NUM*2): SYS_NH_LOGICAL_REPLI_MAX_NUM;

                if ((p_meminfo->dsmet.replicate_num + 1) < SYS_NH_LOGICAL_REPLI_MAX_NUM &&
                    offset >= (p_meminfo->dsmet.dsnh_offset/max_num)*max_num &&
                    offset < (p_meminfo->dsmet.dsnh_offset/max_num + 1)*max_num)
                {
                    *pp_mem_node = p_meminfo;
                }

                if (p_meminfo->dsmet.replicate_num == 0)
                {
                    if (p_meminfo->dsmet.ref_nhid == p_member->ref_nhid)
                    {
                        *p_entry_exit = TRUE;
                        *pp_mem_node = p_meminfo;
                        return CTC_E_NONE;
                    }
                }
                else
                {
                    for (index = 0; index < (p_meminfo->dsmet.replicate_num + 1)*2; index++)
                    {
                        if (p_meminfo->dsmet.vid_list[index] == p_member->ref_nhid)
                        {
                            *p_repli_pos = index;
                            *p_entry_exit = TRUE;
                            *pp_mem_node = p_meminfo;
                            return CTC_E_NONE;
                        }
                    }
                }
            }

            break;


        default:
            break;
        }
    }

    /*In IPMC bitmap,the first dsMet can not be port bitmap member.*/
    p_meminfo = _ctc_container_of(ctc_list_pointer_head(p_db_member_list),
                                  sys_nh_mcast_meminfo_t, list_head);

    if ((*p_entry_exit == FALSE)
        && p_meminfo
        && (p_meminfo->dsmet.ucastid == SYS_NH_MET_DROP_UCAST_ID))
    {
        *p_entry_exit = FALSE;
        *pp_mem_node = p_meminfo;

    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_nh_mcast_repli_update_dsnh(uint8 lchip, uint32 old_dsnh_offset,
                                          uint32 new_dsnh_offset, uint32 update_cnt)
{
    ds_next_hop4_w_t dsnh[SYS_NH_LOGICAL_REPLI_MAX_NUM];
    uint32 cmd = 0;
    uint32 offset = 0;
    uint32 i = 0;

    cmd = DRV_IOR(DsNextHop4W_t, DRV_ENTRY_FLAG);
    for (i = 0; i < update_cnt ; i++)
    {
        offset = old_dsnh_offset + i;
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, offset, cmd, &dsnh[i]));
    }


    cmd = DRV_IOW(DsNextHop4W_t, DRV_ENTRY_FLAG);
    for (i = 0; i < update_cnt ; i++)
    {
        offset = new_dsnh_offset + i;
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, offset, cmd, &dsnh[i]));
    }

    return CTC_E_NONE;

}


STATIC int32
_sys_greatbelt_nh_mcast_rep_process_dsnh(uint8 lchip, sys_nh_param_mcast_member_t* p_mem_param,
                                     sys_nh_mcast_meminfo_t* p_member_info,
                                     uint32* p_dsnh_offset,
                                     uint8* p_use_dsnh8w)
{
    uint8 dsnh_is_8w = 0;
    uint16 dsnh_offset = 0;
    uint8 max_num = 0;

    CTC_ERROR_RETURN(sys_greatbelt_nh_get_dsnh_offset_by_nhid(lchip, p_mem_param->ref_nhid, &dsnh_offset, &dsnh_is_8w));


    max_num = dsnh_is_8w?(SYS_NH_LOGICAL_REPLI_MAX_NUM*2): SYS_NH_LOGICAL_REPLI_MAX_NUM;

    if (p_member_info)     /*Update dsnh offset*/
    {
        if (p_member_info->dsmet.vid_list)
        {
            *p_dsnh_offset = dsnh_offset;
            p_member_info->dsmet.vid_list[dsnh_offset%max_num] = p_mem_param->ref_nhid;
        }
        else
        {

            p_member_info->dsmet.vid_list = mem_malloc(MEM_NEXTHOP_MODULE, max_num * sizeof(uint16));

            if (!p_member_info->dsmet.vid_list)
            {
                return CTC_E_NO_MEMORY;
            }
            sal_memset(p_member_info->dsmet.vid_list, 0, max_num * sizeof(uint16));
            /*First nexhtop*/
            p_member_info->dsmet.vid_list[p_member_info->dsmet.dsnh_offset%max_num] = p_member_info->dsmet.ref_nhid;

            /*Current nexhtop*/
           *p_dsnh_offset = dsnh_offset;
            p_member_info->dsmet.vid_list[dsnh_offset%max_num] = p_mem_param->ref_nhid;

            /*update met ds nexthop offset*/
            p_member_info->dsmet.dsnh_offset = (dsnh_offset / max_num)*max_num;
        }


    }
    else
    {
        *p_dsnh_offset = dsnh_offset;
    }


   *p_use_dsnh8w = dsnh_is_8w;

    return CTC_E_NONE;
}


STATIC int32
_sys_greatbelt_nh_mcast_process_dsnh(uint8 lchip, sys_nh_param_mcast_member_t* p_mem_param,
                                     sys_nh_mcast_meminfo_t* p_member_info,
                                     uint32* p_dsnh_offset)
{

    sys_nh_param_dsnh_t dsnh_param;
    sys_l3if_prop_t l3if_prop;
    uint16 l2edit_ptr = 0;
    uint8 entry_num = 0;

    sal_memset(&dsnh_param, 0, sizeof(sys_nh_param_dsnh_t));

    if (p_mem_param->is_linkagg)
    {
        l3if_prop.gport = CTC_MAP_TID_TO_GPORT(p_mem_param->destid);
    }
    else
    {
        uint8 gchip;
        CTC_ERROR_RETURN(sys_greatbelt_get_gchip_id(lchip, &gchip));
        l3if_prop.gport = CTC_MAP_LPORT_TO_GPORT(gchip, p_mem_param->destid);
    }

    l3if_prop.l3if_type = p_mem_param->l3if_type;
    l3if_prop.vlan_id = p_mem_param->vid & 0xFFF;

    CTC_ERROR_RETURN(sys_greatbelt_l3if_get_l3if_info(lchip, 0, &l3if_prop));
    CTC_ERROR_RETURN(sys_greatbelt_nh_get_ipmc_l2edit_offset(lchip, &l2edit_ptr));
    dsnh_param.l2edit_ptr = l2edit_ptr;
    dsnh_param.l3edit_ptr = 0;
    dsnh_param.dest_vlan_ptr = l3if_prop.vlan_ptr;

    if (p_member_info && p_member_info->dsmet.free_dsnh_offset_cnt)
    /*Logical replicate*/
    {
        if (p_member_info->dsmet.replicate_num + 1 >= SYS_NH_LOGICAL_REPLI_MAX_NUM)
        {
            return CTC_E_NH_EXCEED_MAX_LOGICAL_REPLI_CNT;
        }

        *p_dsnh_offset = p_member_info->dsmet.dsnh_offset + p_member_info->dsmet.replicate_num + 1;
        p_member_info->dsmet.free_dsnh_offset_cnt--;
        p_member_info->dsmet.vid_list[p_member_info->dsmet.replicate_num + 1] = l3if_prop.vlan_id;

    }
    else if (p_member_info)
    /*Update dsnh offset*/
    {
        if ((p_member_info->dsmet.replicate_num + SYS_NH_MCAST_REPLI_INCR_STEP) > \
            SYS_NH_LOGICAL_REPLI_MAX_NUM)
        {
            return CTC_E_NH_EXCEED_MAX_LOGICAL_REPLI_CNT;
        }
        else
        {
            /*Logical replicate*/
            if (p_member_info->dsmet.vid_list == NULL)
            {
                p_member_info->dsmet.vid_list = mem_malloc(MEM_NEXTHOP_MODULE, SYS_NH_LOGICAL_REPLI_MAX_NUM * sizeof(uint16));

                if (!p_member_info->dsmet.vid_list)
                {
                    return CTC_E_NO_MEMORY;
                }

                p_member_info->dsmet.vid_list[0] = p_member_info->dsmet.vid;
                CTC_UNSET_FLAG(p_member_info->dsmet.flag, SYS_NH_DSMET_FLAG_USE_RSV_NH);
            }

            if ((p_member_info->dsmet.replicate_num + 1) < SYS_NH_MCAST_REPLI_INCR_STEP)
            {
                entry_num = SYS_NH_MCAST_REPLI_INCR_STEP;
            }
            else
            {
                entry_num = p_member_info->dsmet.replicate_num + 1 + SYS_NH_MCAST_REPLI_INCR_STEP;
            }

            /*Free old dsnh offset*/
            CTC_ERROR_RETURN(sys_greatbelt_nh_offset_free(lchip,
                                                          SYS_NH_ENTRY_TYPE_NEXTHOP_4W, (p_member_info->dsmet.replicate_num + 1),
                                                          (p_member_info->dsmet.dsnh_offset)));
            /*Allocate new dsnh*/
            CTC_ERROR_RETURN(sys_greatbelt_nh_offset_alloc(lchip,
                                                           SYS_NH_ENTRY_TYPE_NEXTHOP_4W, entry_num, p_dsnh_offset));

            CTC_ERROR_RETURN(_sys_greatbelt_nh_mcast_repli_update_dsnh(lchip,
                                                                       p_member_info->dsmet.dsnh_offset, (*p_dsnh_offset), (p_member_info->dsmet.replicate_num + 1)));

            p_member_info->dsmet.free_dsnh_offset_cnt = SYS_NH_MCAST_REPLI_INCR_STEP;

            p_member_info->dsmet.free_dsnh_offset_cnt -= ((p_member_info->dsmet.replicate_num) ? 1 : 2);

            p_member_info->dsmet.vid_list[p_member_info->dsmet.replicate_num + 1] = l3if_prop.vlan_id;
            p_member_info->dsmet.dsnh_offset = *p_dsnh_offset;

            /*Current used dsnh offset*/
            *p_dsnh_offset = (*p_dsnh_offset) + p_member_info->dsmet.replicate_num + 1;

        }
    }
    else
    {   /*First member */
        CTC_ERROR_RETURN(sys_greatbelt_nh_offset_alloc(lchip,
                                                       SYS_NH_ENTRY_TYPE_NEXTHOP_4W,
                                                       1, p_dsnh_offset));
    }

    dsnh_param.dsnh_offset = (*p_dsnh_offset);
    dsnh_param.dsnh_type = SYS_NH_PARAM_DSNH_TYPE_IPMC;
    dsnh_param.mtu_no_chk = p_mem_param->mtu_no_chk;
    CTC_ERROR_RETURN(sys_greatbelt_nh_write_entry_dsnh4w(lchip, &dsnh_param));
    return CTC_E_NONE;

}

STATIC int32
_sys_greatbelt_nh_mcast_write_dsmet(uint8 lchip, sys_nh_info_dsmet_t* p_met_info)
{

    ds_met_entry_t dsmet;
    uint8 enable = 0;
    uint8 loop = 0;
    uint8 xgpon_en = 0;
    uint8 use_bitmap = CTC_FLAG_ISSET(p_met_info->flag,SYS_NH_DSMET_FLAG_USE_PBM);

    sal_memset(&dsmet, 0, sizeof(ds_met_entry_t));

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "\n\
    lchip = % d\n\
    dsmet_offset = 0x %x, \n\
    dsnh_offset = 0x %x, \n\
    is_linkagg = %d, \n\
    member_type = %d, \n\
    replicate_num = %d, \n\
    ucastid = %d, \n\
    use_pbm = %d, pbm0 = 0x %x, pbm1 = 0x %x, \n\
    next_met_offset = 0x %x, \n\
    port_check_discard = %d, \n\
    use_bitmap:%d\n",
                   lchip,
                   p_met_info->dsmet_offset,
                   p_met_info->dsnh_offset,
                   CTC_FLAG_ISSET(p_met_info->flag, SYS_NH_DSMET_FLAG_IS_LINKAGG),
                   p_met_info->member_type,
                   p_met_info->replicate_num,
                   p_met_info->ucastid,
                   CTC_FLAG_ISSET(p_met_info->flag, SYS_NH_DSMET_FLAG_USE_PBM), p_met_info->pbm0, p_met_info->pbm1,
                   p_met_info->next_dsmet_offset,
                   CTC_FLAG_ISSET(p_met_info->flag, SYS_NH_DSMET_FLAG_PORT_CHK_DISCARD),
                   use_bitmap);

    /*
     dsmet.is_met =1;
    */
    sys_greatbelt_nh_get_reflective_brg_en(lchip, &enable);
    if (enable)
    {
       CTC_UNSET_FLAG(p_met_info->flag, SYS_NH_DSMET_FLAG_PORT_CHK_DISCARD);
    }

    sys_greatbelt_global_get_xgpon_en(lchip, &xgpon_en);
    if(CTC_FLAG_ISSET(p_met_info->flag,SYS_NH_DSMET_FLAG_USE_PBM)
          && (p_met_info->replicate_num == 0) && (0 == xgpon_en))
    {
       use_bitmap = 0;

       for (loop = 0; ((loop < 32)&&p_met_info->pbm0); loop++)
       {
           if(CTC_IS_BIT_SET(p_met_info->pbm0, loop))
           {
                p_met_info->ucastid = loop;
                break;
           }
       }
       for (loop = 0; ((loop < 32)&&p_met_info->pbm1); loop++)
       {
           if(CTC_IS_BIT_SET(p_met_info->pbm1, loop))
           {
                p_met_info->ucastid = loop + 32;
                break;
           }
       }
    }
    if (use_bitmap)
    {
        dsmet.mcast_mode  = 1;
        dsmet.replication_ctl  = p_met_info->pbm0 & 0xFFFFF;
        dsmet.logic_dest_port4_0 = (p_met_info->pbm0 >> 20) & 0x1F;
        dsmet.logic_dest_port5   = (p_met_info->pbm0 >> 25) & 0x1;
        dsmet.logic_port_type_check = (p_met_info->pbm0 >> 26) & 0x1;
        dsmet.ucast_id13 = (p_met_info->pbm0 >> 27) & 0x1;
        dsmet.leaf_check_en = (p_met_info->pbm0 >> 28) & 0x1;
        dsmet.logic_dest_port7_6 = (p_met_info->pbm0 >> 29) & 0x3;
        dsmet.next_hop_ext = (p_met_info->pbm0 >> 31) & 0x1;
        dsmet.ucast_id_low = p_met_info->pbm1 & 0xFFF;
        dsmet.aps_bridge_en = (p_met_info->pbm1 >> 12) & 0x1;
        dsmet.logic_dest_port9_8 = (p_met_info->pbm1 >> 13) & 0x3;
        dsmet.logic_port_check_en = (p_met_info->pbm1 >> 15) & 0x7;
        dsmet.port_bitmap52_50 = (p_met_info->pbm1 >> 18) & 0x7;
        dsmet.logic_dest_port10 = (p_met_info->pbm1 >> 21) & 0x1;
        dsmet.logic_dest_port12_11 = (p_met_info->pbm1 >> 22) & 0x3;
        dsmet.is_link_aggregation = CTC_FLAG_ISSET(p_met_info->flag, SYS_NH_DSMET_FLAG_IS_LINKAGG);
        dsmet.phy_port_check_discard = CTC_FLAG_ISSET(p_met_info->flag, SYS_NH_DSMET_FLAG_PORT_CHK_DISCARD);
        /*  dsmet.ucast_id15_14  =   qselType   */
    }
    else
    {

        if (CTC_FLAG_ISSET(p_met_info->flag, SYS_NH_DSMET_FLAG_HORIZON_SPLIT_EN))
        {
            dsmet.logic_port_type_check = 1;
        }

        dsmet.ucast_id_low = p_met_info->ucastid & 0xFFF;   /*0-11 bit*/
        if ((p_met_info->ucastid >> 12) & 0x1)
        {
            CTC_BIT_SET(dsmet.logic_port_check_en, 1); /*12 bit*/
        }
        dsmet.ucast_id13 = (p_met_info->ucastid >> 13) & 0x1;
        dsmet.ucast_id15_14 = (p_met_info->ucastid >> 14) & 0x3;

        dsmet.remote_chip = (SYS_NH_PARAM_MCAST_MEM_REMOTE == p_met_info->member_type) ? 1 : 0;
        dsmet.next_hop_ext  = CTC_FLAG_ISSET(p_met_info->flag, SYS_NH_DSMET_FLAG_USE_DSNH8W);
        dsmet.is_link_aggregation = CTC_FLAG_ISSET(p_met_info->flag, SYS_NH_DSMET_FLAG_IS_LINKAGG);
        if (CTC_FLAG_ISSET(p_met_info->flag, SYS_NH_DSMET_FLAG_LOGIC_PORT_CHK))
        {  /*only apply to vpls/trill/wlan/pbb*/
            CTC_BIT_SET(dsmet.logic_port_check_en, 0);
            dsmet.logic_dest_port4_0 = p_met_info->logic_port & 0x1F;
            dsmet.logic_dest_port5 = (p_met_info->logic_port >> 5) & 0x1;
            dsmet.logic_dest_port7_6 = (p_met_info->logic_port >> 6) & 0x3;
            dsmet.logic_dest_port9_8 = (p_met_info->logic_port >> 8) & 0x3;
            dsmet.logic_dest_port10 = (p_met_info->logic_port >> 10) & 0x1;
            dsmet.logic_dest_port12_11 = (p_met_info->logic_port >> 11) & 0x3;
        }
        else
        {
            dsmet.phy_port_check_discard = CTC_FLAG_ISSET(p_met_info->flag, SYS_NH_DSMET_FLAG_PORT_CHK_DISCARD);
        }

        if (p_met_info->replicate_num > 16)
        {
            CTC_BIT_SET(dsmet.logic_port_check_en, 2);   /*replication_ctl_ext*/
        }

        dsmet.replication_ctl = (p_met_info->dsnh_offset << 4) | (p_met_info->replicate_num & 0xF);
        dsmet.aps_bridge_en  = (p_met_info->member_type == SYS_NH_PARAM_BRGMC_MEM_LOCAL_WITH_APS_BRIDGE) ? 1 : 0;

    }
    dsmet.next_met_entry_ptr = p_met_info->next_dsmet_offset;
    dsmet.end_local_rep = CTC_FLAG_ISSET(p_met_info->flag, SYS_NH_DSMET_FLAG_ENDLOCAL);
    dsmet.is_met = TRUE;
    CTC_ERROR_RETURN(sys_greatbelt_nh_write_asic_table(lchip, SYS_NH_ENTRY_TYPE_MET,
                                                       p_met_info->dsmet_offset, &dsmet));
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_nh_mcast_entry_set_default(uint8 lchip, sys_nh_info_mcast_t* p_info_mcast, uint32 met_offset, bool bCreateGroup)
{

    sys_nh_info_dsmet_t dsmet;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    sal_memset(&dsmet, 0, sizeof(sys_nh_info_dsmet_t));

    dsmet.dsmet_offset = met_offset;

    dsmet.flag = 0;
    dsmet.ucastid = SYS_NH_MET_DROP_UCAST_ID;

    if (bCreateGroup && p_info_mcast->stacking_met_offset)
    {
        dsmet.next_dsmet_offset = p_info_mcast->stacking_met_offset;
    }
    else
    {
        dsmet.next_dsmet_offset = SYS_NH_MET_END_REPLICATE_OFFSET;
    }

    CTC_ERROR_RETURN(_sys_greatbelt_nh_mcast_write_dsmet(lchip, &dsmet));
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_nh_mcast_build_dsmet_info(uint8 lchip, sys_nh_info_mcast_t* p_info_mcast,
                                         sys_nh_mcast_dsmet_io_t* p_dsmet_io,
                                         sys_nh_info_dsmet_t* p_dsmet)
{
    uint32 reason_id = 0;
    uint32 dest_map = 0;
    uint8 is_cpu_nhid = 0;

    p_dsmet->replicate_num = 0;
    p_dsmet->dsnh_offset = p_dsmet_io->dsnh_offset,
    p_dsmet->dsmet_offset = p_dsmet_io->met_offset;
    p_dsmet->member_type = p_dsmet_io->p_mem_param->member_type;
    p_dsmet->flag      |= p_dsmet_io->p_mem_param->is_linkagg ? SYS_NH_DSMET_FLAG_IS_LINKAGG : 0;
    p_dsmet->flag      |= p_dsmet_io->p_mem_param->is_logic_port ? SYS_NH_DSMET_FLAG_LOGIC_PORT_CHK : 0;
    p_dsmet->flag      |= p_dsmet_io->p_mem_param->is_horizon_split ? SYS_NH_DSMET_FLAG_HORIZON_SPLIT_EN : 0;

    p_dsmet->logic_port  = p_dsmet_io->p_mem_param->logic_port;
    p_dsmet->ref_nhid = p_dsmet_io->p_mem_param->ref_nhid;
    p_dsmet->vid      = p_dsmet_io->p_mem_param->vid;
    p_dsmet->ucastid = p_dsmet_io->p_mem_param->destid;

    switch (p_dsmet_io->p_mem_param->member_type)
    {
    case SYS_NH_PARAM_BRGMC_MEM_LOCAL:
    case SYS_NH_PARAM_BRGMC_MEM_LOCAL_WITH_APS_BRIDGE:
    case SYS_NH_PARAM_BRGMC_MEM_RAPS:
    case SYS_NH_PARAM_MCAST_MEM_LOCAL_WITH_NH:
    case SYS_NH_PARAM_MCAST_MEM_LOCAL_WITH_NH_DESTID:
        p_dsmet->flag      |= p_dsmet_io->p_mem_param->is_reflective ? 0 : SYS_NH_DSMET_FLAG_PORT_CHK_DISCARD;

        if (SYS_NH_PARAM_MCAST_MEM_LOCAL_WITH_NH == p_dsmet_io->p_mem_param->member_type)
        {
            sys_greatbelt_nh_is_cpu_nhid(lchip, p_dsmet->ref_nhid, &is_cpu_nhid);
            if (is_cpu_nhid)
            {
                reason_id = CTC_PKT_CPU_REASON_GET_BY_NHPTR(p_dsmet->dsnh_offset);
                CTC_ERROR_RETURN(sys_greatbelt_cpu_reason_get_info(lchip, reason_id, &dest_map));
                CTC_UNSET_FLAG(p_dsmet->flag, SYS_NH_DSMET_FLAG_PORT_CHK_DISCARD);
                p_dsmet->ucastid = dest_map;
            }
        }
    /*No need break here*/
    case SYS_NH_PARAM_MCAST_MEM_LOCAL_LOGIC_REP:
    case SYS_NH_PARAM_MCAST_MEM_MIRROR_WITH_NH:
    case SYS_NH_PARAM_IPMC_MEM_LOCAL:
    case SYS_NH_PARAM_IPMC_MEM_LOCAL_LOGIC_REP:
    case SYS_NH_PARAM_MCAST_MEM_REMOTE:
        if (p_dsmet_io->p_next_mem)
        {
            p_dsmet->next_dsmet_offset = p_dsmet_io->p_next_mem->dsmet.dsmet_offset;
        }
        else
        {

            CTC_SET_FLAG(p_dsmet->flag, SYS_NH_DSMET_FLAG_ENDLOCAL);
            p_dsmet->next_dsmet_offset = SYS_NH_MET_END_REPLICATE_OFFSET;
        }

        break;

    default:
        return CTC_E_INVALID_PARAM;

    }

    /*If stacking en, need hook stacking mcast group*/
    if (SYS_NH_MET_END_REPLICATE_OFFSET == p_dsmet->next_dsmet_offset)
    {
        if (p_info_mcast->stacking_met_offset)
        {
            p_dsmet->next_dsmet_offset = p_info_mcast->stacking_met_offset;
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_nh_mcast_add_dsmet_entry(uint8 lchip, sys_nh_param_mcast_member_t* p_mem_param,
                                        uint32 basic_met_offset, uint32 dsnh_offset,
                                        uint8 use_dsnh8w,
                                        ctc_list_pointer_t* p_db_member_list,
                                        sys_nh_info_mcast_t* p_info_mcast)
{
    int32 ret = CTC_E_NONE;
    sys_nh_mcast_meminfo_t* p_mem_flex = NULL;
    uint32 new_met_offset = 0;
    sys_nh_mcast_dsmet_io_t dsmet_io;
    sys_nh_mcast_meminfo_t* p_member_info;

    p_member_info = mem_malloc(MEM_NEXTHOP_MODULE, sizeof(sys_nh_mcast_meminfo_t));

    if (!p_member_info)
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_member_info, 0, sizeof(sys_nh_mcast_meminfo_t));
    p_member_info->dsmet.flag |= use_dsnh8w ? SYS_NH_DSMET_FLAG_USE_DSNH8W : 0;

    /*1. Init param*/
    sal_memset(&dsmet_io, 0, sizeof(sys_nh_mcast_dsmet_io_t));
    dsmet_io.p_mem_param = p_mem_param;

    if ((SYS_NH_PARAM_BRGMC_MEM_LOCAL == p_mem_param->member_type)
        &&(!p_mem_param->is_mirror)
        && !p_mem_param->is_logic_port && p_mem_param->destid < SYS_NH_DSMET_BITMAP_MAX_PORT_ID)
    {
        CTC_SET_FLAG(p_member_info->dsmet.flag, SYS_NH_DSMET_FLAG_USE_PBM);
        p_member_info->dsmet.replicate_num  = 0;
        if (p_mem_param->destid < 32)
        {
            CTC_BIT_SET(p_member_info->dsmet.pbm0, p_mem_param->destid);
        }
        else
        {
            CTC_BIT_SET(p_member_info->dsmet.pbm1, (p_mem_param->destid - 32));
        }
    }

    /*2. Build new allocate member, and update flex member(prev member or next member)*/
    /*2.1 New member is the list's first member*/
    if (ctc_list_pointer_empty(p_db_member_list))
    {
            dsmet_io.met_offset = basic_met_offset;
            dsmet_io.dsnh_offset = dsnh_offset;
            dsmet_io.p_next_mem = NULL;
            CTC_ERROR_GOTO(_sys_greatbelt_nh_mcast_build_dsmet_info(lchip, p_info_mcast, &dsmet_io, &(p_member_info->dsmet)), ret, error2);
            /*Add this new member to db member list*/
            ctc_list_pointer_insert_head(p_db_member_list, &(p_member_info->list_head));
            CTC_ERROR_GOTO(_sys_greatbelt_nh_mcast_write_dsmet(lchip,
                                                               &(p_member_info->dsmet)),
                           ret, error1);

            return CTC_E_NONE;
    }

    /*2.2 New member is not the list's first member*/
    if (SYS_NH_PARAM_IPMC_MEM_LOCAL == p_mem_param->member_type
        && (p_mem_param->destid < SYS_NH_DSMET_BITMAP_MAX_PORT_ID)
        && (p_mem_param->l3if_type == CTC_L3IF_TYPE_VLAN_IF)
        && sys_greatbelt_nh_is_ipmc_bitmap_enable(lchip))
    {

        uint32 new_dsnh_offset = 0;
		ret = sys_greatbelt_nh_offset_alloc(lchip,
                                                           SYS_NH_ENTRY_TYPE_MET_FOR_IPMCBITMAP, 1, &new_met_offset);

        if (ret != CTC_E_NONE)
        {
            CTC_ERROR_GOTO(sys_greatbelt_nh_offset_alloc(lchip,
                                                           SYS_NH_ENTRY_TYPE_MET, 1, &new_met_offset), ret, error2);
        }
        else
        {
            CTC_ERROR_GOTO(sys_greatbelt_nh_get_ipmc_bitmap_dsnh_offset(lchip, new_met_offset, &new_dsnh_offset), ret, error2);
            CTC_ERROR_GOTO(_sys_greatbelt_nh_mcast_repli_update_dsnh(lchip, dsnh_offset,
                                                      new_dsnh_offset, 1), ret, error2);
            CTC_SET_FLAG(p_member_info->dsmet.flag, SYS_NH_DSMET_FLAG_USE_PBM);

            p_member_info->dsmet.replicate_num  = 0;
            if (p_mem_param->destid < 32)
            {
                CTC_BIT_SET(p_member_info->dsmet.pbm0, p_mem_param->destid);
            }
            else
            {
                CTC_BIT_SET(p_member_info->dsmet.pbm1, (p_mem_param->destid - 32));
            }
        }
    }
    else
    {
        CTC_ERROR_GOTO(sys_greatbelt_nh_offset_alloc(lchip,
                                                       SYS_NH_ENTRY_TYPE_MET, 1, &new_met_offset), ret, error2);
    }

    /*(1) insert new member into tail*/
    dsmet_io.met_offset = new_met_offset;
    dsmet_io.dsnh_offset = dsnh_offset;
    dsmet_io.p_next_mem = NULL;
    CTC_ERROR_GOTO(_sys_greatbelt_nh_mcast_build_dsmet_info(lchip, p_info_mcast, &dsmet_io, &(p_member_info->dsmet)), ret, error2);
    /*The list have one member at least, so the tail node should not be NULL*/
    p_mem_flex = _ctc_container_of(ctc_list_pointer_node_tail(p_db_member_list),
                                   sys_nh_mcast_meminfo_t, list_head);
    /*Add this new member to db member list*/
    ctc_list_pointer_insert_tail(p_db_member_list, &(p_member_info->list_head));
    CTC_ERROR_GOTO(_sys_greatbelt_nh_mcast_write_dsmet(lchip,
                                                       &(p_member_info->dsmet)),
                   ret, error1);

    /*(2) update previous member's next met offset*/
    p_mem_flex->dsmet.next_dsmet_offset = p_member_info->dsmet.dsmet_offset;
    CTC_UNSET_FLAG(p_mem_flex->dsmet.flag, SYS_NH_DSMET_FLAG_ENDLOCAL);

    CTC_ERROR_GOTO(_sys_greatbelt_nh_mcast_write_dsmet(lchip,
                                                       &(p_mem_flex->dsmet)),
                   ret, error1);

    return CTC_E_NONE;

error1:
    ctc_list_pointer_delete(p_db_member_list, &(p_member_info->list_head));
    mem_free(p_member_info);
    return ret;
error2:
    mem_free(p_member_info);
    return ret;
}

STATIC int32
_sys_greatbelt_nh_mcast_del_dsmet_entry(uint8 lchip, sys_nh_mcast_meminfo_t* p_mem_info,
                                        ctc_list_pointer_t* p_db_member_list,
                                        uint8 *phy_if_first_met,
                                        sys_nh_info_mcast_t* p_info_mcast)
{
    sys_nh_mcast_meminfo_t* p_mem_target, * p_mem_flex;

    /*1. Init param*/
    p_mem_target = _ctc_container_of(ctc_list_pointer_head(p_db_member_list),
                                     sys_nh_mcast_meminfo_t, list_head);

    /*2. Remove member*/
    if (ctc_list_pointer_head(p_db_member_list) == (&(p_mem_info->list_head)))
    /*Target member is first member*/
    {
        if (p_mem_target->list_head.p_next)
        {
            /*Get next member*/
            p_mem_flex = _ctc_container_of(p_mem_target->list_head.p_next,
                                           sys_nh_mcast_meminfo_t, list_head);

            if (p_mem_flex->dsmet.member_type == SYS_NH_PARAM_IPMC_MEM_LOCAL
                && CTC_FLAG_ISSET(p_mem_flex->dsmet.flag, SYS_NH_DSMET_FLAG_USE_PBM))
            { /*when second member is ipmc bitmap member in linklist ,the fisrt member only set to drop*/
                p_mem_target->dsmet.flag = 0;
                p_mem_target->dsmet.ucastid = SYS_NH_MET_DROP_UCAST_ID;
                p_mem_target->dsmet.pbm0 = 0;
                p_mem_target->dsmet.pbm1 = 0;

                *phy_if_first_met = 1;
                CTC_ERROR_RETURN(_sys_greatbelt_nh_mcast_write_dsmet(lchip, &(p_mem_target->dsmet)));
                return CTC_E_NONE;
            }
            else
            {
                CTC_ERROR_RETURN(sys_greatbelt_nh_offset_free(lchip, SYS_NH_ENTRY_TYPE_MET,
                                                              1, p_mem_flex->dsmet.dsmet_offset));

                p_mem_flex->dsmet.dsmet_offset = p_mem_target->dsmet.dsmet_offset;
                CTC_ERROR_RETURN(_sys_greatbelt_nh_mcast_write_dsmet(lchip, &(p_mem_flex->dsmet)));
            }
        }
        else
        /*This is the last member of this group in this chip, deinit basic met entry*/
        {
            /*Free basic met offset*/
            CTC_ERROR_RETURN(_sys_greatbelt_nh_mcast_entry_set_default(lchip,p_info_mcast,
                                                                       p_mem_target->dsmet.dsmet_offset, TRUE));
        }
    }
    else
    {
        p_mem_flex = _ctc_container_of(p_mem_info->list_head.p_prev, sys_nh_mcast_meminfo_t, list_head);
        if (SYS_NH_PARAM_MCAST_MEM_REMOTE != p_mem_info->dsmet.member_type)
        {
            if (CTC_FLAG_ISSET(p_mem_info->dsmet.flag, SYS_NH_DSMET_FLAG_ENDLOCAL))
            {
                CTC_SET_FLAG(p_mem_flex->dsmet.flag, SYS_NH_DSMET_FLAG_ENDLOCAL);
            }
            else
            {
                CTC_UNSET_FLAG(p_mem_flex->dsmet.flag, SYS_NH_DSMET_FLAG_ENDLOCAL);
            }
        }

        p_mem_flex->dsmet.next_dsmet_offset = p_mem_info->dsmet.next_dsmet_offset;
        CTC_ERROR_RETURN(_sys_greatbelt_nh_mcast_write_dsmet(lchip, &(p_mem_flex->dsmet)));
        if (p_mem_info->dsmet.member_type == SYS_NH_PARAM_IPMC_MEM_LOCAL
            && CTC_FLAG_ISSET(p_mem_info->dsmet.flag, SYS_NH_DSMET_FLAG_USE_PBM))
        {
            CTC_ERROR_RETURN(sys_greatbelt_nh_offset_free(lchip, SYS_NH_ENTRY_TYPE_MET_FOR_IPMCBITMAP,
                                                          1, p_mem_info->dsmet.dsmet_offset));
        }
        else
        {
            CTC_ERROR_RETURN(sys_greatbelt_nh_offset_free(lchip, SYS_NH_ENTRY_TYPE_MET,
                                                          1, p_mem_info->dsmet.dsmet_offset));
        }
    }

    ctc_list_pointer_delete(p_db_member_list, &(p_mem_info->list_head));

    if (p_mem_info->dsmet.vid_list)
    {
        mem_free(p_mem_info->dsmet.vid_list);
    }

    mem_free(p_mem_info);
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_nh_mcast_get_nh_and_edit_data(uint8 lchip, sys_nh_param_mcast_member_t* p_mem_param,
                                             sys_nh_mcast_meminfo_t* p_member_info,
                                             uint32* p_dsnh_offset, uint8* p_use_dsnh8w)
{

    uint16 offset_array = 0;

    *p_use_dsnh8w = 0;

    switch (p_mem_param->member_type)
    {
    case SYS_NH_PARAM_BRGMC_MEM_LOCAL:
        if (p_mem_param->is_mirror)
        {
            CTC_ERROR_RETURN(sys_greatbelt_nh_get_resolved_offset(lchip,
                                 SYS_NH_RES_OFFSET_TYPE_MIRROR_NH, p_dsnh_offset));
        }
        else
        {
            CTC_ERROR_RETURN(sys_greatbelt_nh_get_resolved_offset(lchip,
                                 SYS_NH_RES_OFFSET_TYPE_BRIDGE_NH, p_dsnh_offset));
        }

        break;

    case SYS_NH_PARAM_MCAST_MEM_LOCAL_WITH_NH:
    case SYS_NH_PARAM_BRGMC_MEM_LOCAL_WITH_APS_BRIDGE:
    case SYS_NH_PARAM_MCAST_MEM_MIRROR_WITH_NH:
    case SYS_NH_PARAM_MCAST_MEM_LOCAL_WITH_NH_DESTID:
        CTC_ERROR_RETURN(sys_greatbelt_nh_get_dsnh_offset_by_nhid(lchip,
                             p_mem_param->ref_nhid, &offset_array, p_use_dsnh8w));
        *p_dsnh_offset = offset_array;
        break;
    case SYS_NH_PARAM_IPMC_MEM_LOCAL:
        {
            sys_l3if_prop_t l3if_prop;
            if (p_mem_param->is_linkagg)
            {
                l3if_prop.gport = CTC_MAP_TID_TO_GPORT(p_mem_param->destid);
            }
            else
            {
                uint8 gchip;
                CTC_ERROR_RETURN(sys_greatbelt_get_gchip_id(lchip, &gchip));
                l3if_prop.gport = CTC_MAP_LPORT_TO_GPORT(gchip, p_mem_param->destid);
            }

            l3if_prop.l3if_type = p_mem_param->l3if_type;
            l3if_prop.vlan_id = p_mem_param->vid & 0xFFF;

            CTC_ERROR_RETURN(sys_greatbelt_l3if_get_l3if_info(lchip, 0, &l3if_prop));
            CTC_ERROR_RETURN(sys_greatbelt_nh_get_ipmc_dsnh_offset(lchip, l3if_prop.l3if_id, p_mem_param->mtu_no_chk, p_dsnh_offset));

        }
        break;

    case SYS_NH_PARAM_IPMC_MEM_LOCAL_LOGIC_REP:
        CTC_ERROR_RETURN(_sys_greatbelt_nh_mcast_process_dsnh(lchip, p_mem_param, p_member_info, p_dsnh_offset));
        break;

    case SYS_NH_PARAM_MCAST_MEM_LOCAL_LOGIC_REP:
        CTC_ERROR_RETURN(_sys_greatbelt_nh_mcast_rep_process_dsnh(lchip, p_mem_param, p_member_info, p_dsnh_offset, p_use_dsnh8w));
        break;


    case SYS_NH_PARAM_MCAST_MEM_REMOTE:
        break;

    case SYS_NH_PARAM_BRGMC_MEM_RAPS:
        *p_dsnh_offset = 0;
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_nh_mcast_free_nh_and_edit_data(uint8 lchip,
                                              sys_nh_mcast_meminfo_t* p_member_info,
                                              uint32 del_pos)
{
    uint32 dsnh_offset_del, dsnh_offset_last;

    if (p_member_info->dsmet.member_type == SYS_NH_PARAM_IPMC_MEM_LOCAL_LOGIC_REP)
    {
        if (p_member_info->dsmet.replicate_num > 0)
            /*Remove none last member*/
        {
            dsnh_offset_last  = p_member_info->dsmet.dsnh_offset + p_member_info->dsmet.replicate_num;
            dsnh_offset_del  = p_member_info->dsmet.dsnh_offset + del_pos;
            /*Update dsnh table*/
            CTC_ERROR_RETURN(_sys_greatbelt_nh_mcast_repli_update_dsnh(lchip, dsnh_offset_last, dsnh_offset_del, 1));

            p_member_info->dsmet.vid_list[del_pos] = p_member_info->dsmet.vid_list[p_member_info->dsmet.replicate_num];
            if (1 == p_member_info->dsmet.replicate_num)
            {
                p_member_info->dsmet.vid = p_member_info->dsmet.vid_list[del_pos];
            }

            p_member_info->dsmet.free_dsnh_offset_cnt++;
            if (SYS_NH_MCAST_REPLI_INCR_STEP == p_member_info->dsmet.free_dsnh_offset_cnt)
            {
                p_member_info->dsmet.free_dsnh_offset_cnt = 0;
                CTC_ERROR_RETURN(sys_greatbelt_nh_offset_free(lchip,
                                                              SYS_NH_ENTRY_TYPE_NEXTHOP_4W,
                                                              SYS_NH_MCAST_REPLI_INCR_STEP,
                                                              dsnh_offset_last));
            }
        }
        else
            /*Remove last member*/
        {
            CTC_ERROR_RETURN(sys_greatbelt_nh_offset_free(lchip,
                                                          SYS_NH_ENTRY_TYPE_NEXTHOP_4W,
                                                          1,
                                                          p_member_info->dsmet.dsnh_offset));
        }

    }
    else if(p_member_info->dsmet.member_type == SYS_NH_PARAM_MCAST_MEM_LOCAL_LOGIC_REP)
    {
        if (p_member_info->dsmet.replicate_num > 0)
            /*Remove none last member*/
        {
            uint8 last_pos = 0;
            uint16 last_nhid = 0;

            last_pos = p_member_info->dsmet.replicate_num*2;
            last_nhid =  p_member_info->dsmet.vid_list[last_pos];

            CTC_ERROR_RETURN(sys_greatbelt_nh_swap_nexthop_offset(lchip, p_member_info->dsmet.vid_list[del_pos], last_nhid));

            p_member_info->dsmet.vid_list[del_pos] = last_nhid;
            if (1 == p_member_info->dsmet.replicate_num)
            {
                p_member_info->dsmet.ref_nhid = p_member_info->dsmet.vid_list[0];
            }


        }
        else     /*Remove last member*/
        {

            mem_free(p_member_info->dsmet.vid_list);
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "remove last member, nhid:%d, dsnh_offset:%d\n", p_member_info->dsmet.ref_nhid, p_member_info->dsmet.dsnh_offset);
        }

    }

    return CTC_E_NONE;
}


STATIC int32
_sys_greatbelt_nh_mcast_add_remote_dsmet_entry(uint8 lchip, sys_nh_param_mcast_member_t* p_mem_param,
                                                uint32 basic_met_offset,
                                                ctc_list_pointer_t* p_db_member_list,
                                                sys_nh_info_mcast_t* p_info_mcast)
{
    uint16 mcast_profile_id = 0;
    uint8 xgpon_en = 0;

    mcast_profile_id = p_mem_param->destid;


    CTC_ERROR_RETURN(sys_greatbelt_stacking_get_mcast_profile_met_offset(lchip, mcast_profile_id, &p_info_mcast->stacking_met_offset));

    sys_greatbelt_global_get_xgpon_en(lchip, &xgpon_en);
    if (xgpon_en)
    {
        p_info_mcast->stacking_met_offset = p_mem_param->destid;
    }


    if (ctc_list_pointer_empty(p_db_member_list))
    {
        CTC_ERROR_RETURN(_sys_greatbelt_nh_mcast_entry_set_default(lchip,
                                                                    p_info_mcast,
                                                                    p_info_mcast->basic_met_offset,
                                                                    TRUE));
    }
    else
    {
        sys_nh_mcast_meminfo_t* p_mem_flex = NULL;

        p_mem_flex = _ctc_container_of(ctc_list_pointer_node_tail(p_db_member_list),
                                       sys_nh_mcast_meminfo_t, list_head);

        /*(2) update previous member's next met offset*/
        p_mem_flex->dsmet.next_dsmet_offset = p_info_mcast->stacking_met_offset;

        CTC_ERROR_RETURN(_sys_greatbelt_nh_mcast_write_dsmet(lchip, &(p_mem_flex->dsmet)));

    }

    p_info_mcast->stacking_mcast_profile_id = mcast_profile_id;

    return CTC_E_NONE;

 }


STATIC int32
_sys_greatbelt_nh_mcast_remove_remote_dsmet_entry(uint8 lchip, sys_nh_param_mcast_member_t* p_mem_param,
                                                   uint32 basic_met_offset,
                                                   ctc_list_pointer_t* p_db_member_list,
                                                   sys_nh_info_mcast_t* p_info_mcast)
{
    uint16 mcast_profile_id = 0;

    mcast_profile_id = 0;

    CTC_ERROR_RETURN(sys_greatbelt_stacking_get_mcast_profile_met_offset(lchip, mcast_profile_id, &p_info_mcast->stacking_met_offset));

    if (ctc_list_pointer_empty(p_db_member_list))
    {
        CTC_ERROR_RETURN(_sys_greatbelt_nh_mcast_entry_set_default(lchip,
                                                                    p_info_mcast,
                                                                    p_info_mcast->basic_met_offset,
                                                                    TRUE));
    }
    else
    {
        sys_nh_mcast_meminfo_t* p_mem_flex = NULL;

        p_mem_flex = _ctc_container_of(ctc_list_pointer_node_tail(p_db_member_list),
                                       sys_nh_mcast_meminfo_t, list_head);

        /*(2) update previous member's next met offset*/
        p_mem_flex->dsmet.next_dsmet_offset = p_info_mcast->stacking_met_offset;

        CTC_ERROR_RETURN(_sys_greatbelt_nh_mcast_write_dsmet(lchip, &(p_mem_flex->dsmet)));

    }

    p_info_mcast->stacking_mcast_profile_id = 0;

    return CTC_E_NONE;

 }



int32
_sys_greatbelt_nh_mcast_add_member(uint8 lchip, uint32 basic_met_offset,
                                   sys_nh_param_mcast_member_t* p_mem_param,
                                   ctc_list_pointer_t* p_db_member_list,
                                   sys_nh_info_mcast_t* p_info_mcast)
{
    sys_nh_mcast_meminfo_t* p_member_info = NULL;
    bool entry_exist = FALSE;
    uint8 use_dsnh8w = 0;
    uint8 stk_rsv_mem = 0;
    uint32 repli_pos = 0;
    uint32 dsnh_offset = 0;
    int32 ret = 0;
    uint8 is_cpu_nh = 0;

    /*1. Check if mcast member have been existed*/

    if(p_mem_param->ref_nhid)
    {
        sys_greatbelt_nh_is_cpu_nhid(lchip, p_mem_param->ref_nhid, &is_cpu_nh);
    }

    CTC_PTR_VALID_CHECK(p_mem_param);
    if (p_mem_param->is_linkagg &&
        (p_mem_param->destid >= SYS_GB_MAX_LINKAGG_GROUP_NUM))
    {
        return CTC_E_INVALID_TID;
    }
    else if ((p_mem_param->destid >= SYS_GB_MAX_PORT_NUM_PER_CHIP)
                && (SYS_NH_PARAM_BRGMC_MEM_LOCAL_WITH_APS_BRIDGE != p_mem_param->member_type)
                && (SYS_NH_PARAM_MCAST_MEM_REMOTE != p_mem_param->member_type)
                && (!is_cpu_nh))
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }

    p_mem_param->is_mirror = p_info_mcast->is_mirror;
    if (p_mem_param->is_mirror
        && (p_mem_param->member_type == SYS_NH_PARAM_MCAST_MEM_LOCAL_WITH_NH))
    {
        p_mem_param->member_type = SYS_NH_PARAM_MCAST_MEM_MIRROR_WITH_NH;
    }

    if (p_info_mcast->is_logic_rep_en && (p_mem_param->member_type == SYS_NH_PARAM_IPMC_MEM_LOCAL))
    {
        p_mem_param->member_type = SYS_NH_PARAM_IPMC_MEM_LOCAL_LOGIC_REP;
    }

    if (p_mem_param->member_type == SYS_NH_PARAM_MCAST_MEM_LOCAL_WITH_NH)
    {
        uint8 repli_en = 0;
        sys_greatbelt_nh_is_logic_repli_en(lchip, p_mem_param->ref_nhid, &repli_en);
        if (repli_en)
        {
            p_mem_param->member_type = SYS_NH_PARAM_MCAST_MEM_LOCAL_LOGIC_REP;
        }
    }

    /*Add remote member*/
    if (SYS_NH_PARAM_MCAST_MEM_REMOTE == p_mem_param->member_type)
    {
        /* mcast trunk profile, check exist */
        if (p_info_mcast->stacking_mcast_profile_id)
        {
            return CTC_E_EXIST;
        }


        CTC_ERROR_RETURN(_sys_greatbelt_nh_mcast_add_remote_dsmet_entry( lchip,  p_mem_param,
                                                                         basic_met_offset,
                                                                         p_db_member_list,
                                                                         p_info_mcast));
        return CTC_E_NONE;
    }


    CTC_ERROR_RETURN(_sys_greatbelt_nh_mcast_analyze_member(lchip, p_db_member_list,
                                                            p_mem_param, &p_member_info,
                                                            &entry_exist, &repli_pos));
    if (entry_exist)
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Entry Exist!!!!\n");
        return CTC_E_NONE;
    }

    if (p_member_info
        && (p_mem_param->member_type == SYS_NH_PARAM_IPMC_MEM_LOCAL_LOGIC_REP)
        && ((p_member_info->dsmet.replicate_num + 1) == SYS_NH_LOGICAL_REPLI_MAX_NUM))
    {
        p_member_info = NULL;
    }

    sys_greatbelt_stacking_get_rsv_trunk_number(lchip, &stk_rsv_mem);
    if (((!p_member_info) || CTC_FLAG_ISSET(p_member_info->dsmet.flag, SYS_NH_DSMET_FLAG_USE_PBM))
        && ((p_info_mcast->physical_replication_num + stk_rsv_mem) >= SYS_NH_PHY_REP_MAX_NUM))
    {
        return CTC_E_NH_EXCEED_MCAST_PHY_REP_NUM;
    }

    if (NULL == p_member_info
        && p_mem_param->member_type == SYS_NH_PARAM_MCAST_MEM_LOCAL_LOGIC_REP
        && ((p_info_mcast->physical_replication_num + 16) >= SYS_NH_PHY_REP_MAX_NUM))
    {
        return CTC_E_NH_EXCEED_MCAST_PHY_REP_NUM;
    }



    /*3. Op  DsNH*/
    CTC_ERROR_RETURN(_sys_greatbelt_nh_mcast_get_nh_and_edit_data(lchip,
                         p_mem_param, p_member_info, &dsnh_offset, &use_dsnh8w));

    /*3. Op DsMet*/
    if (p_member_info && p_member_info->dsmet.ucastid == 0x7FFF)
    {
        sys_nh_mcast_dsmet_io_t dsmet_io;
        uint16 next_dsmet_offset = 0;

        next_dsmet_offset  = p_member_info->dsmet.next_dsmet_offset;
        dsmet_io.met_offset = p_member_info->dsmet.dsmet_offset;
        dsmet_io.dsnh_offset = dsnh_offset;
        dsmet_io.use_pbm = 0;
        dsmet_io.p_mem_param = p_mem_param;
        dsmet_io.p_next_mem = NULL;
        _sys_greatbelt_nh_mcast_build_dsmet_info(lchip, p_info_mcast, &dsmet_io, &p_member_info->dsmet);
        p_member_info->dsmet.next_dsmet_offset = next_dsmet_offset;

        if ((SYS_NH_PARAM_BRGMC_MEM_LOCAL == p_mem_param->member_type)&&(!p_mem_param->is_mirror)
            && !p_mem_param->is_logic_port && p_mem_param->destid < SYS_NH_DSMET_BITMAP_MAX_PORT_ID)
        {
            CTC_SET_FLAG(p_member_info->dsmet.flag, SYS_NH_DSMET_FLAG_USE_PBM);
            p_member_info->dsmet.replicate_num = 0;
            if (p_mem_param->destid < 32)
            {
                CTC_BIT_SET(p_member_info->dsmet.pbm0, p_mem_param->destid);
            }
            else if (p_mem_param->destid < 64)
            {
                CTC_BIT_SET(p_member_info->dsmet.pbm1, (p_mem_param->destid - 32));
            }
        }

        p_member_info->dsmet.flag |= use_dsnh8w ? SYS_NH_DSMET_FLAG_USE_DSNH8W : 0;
        if (NULL == p_member_info->list_head.p_next)
        {
            p_member_info->dsmet.next_dsmet_offset = p_info_mcast->stacking_met_offset;
        }
        CTC_ERROR_RETURN(_sys_greatbelt_nh_mcast_write_dsmet(lchip, &p_member_info->dsmet));
    }
    else if (p_member_info)
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "\ndsnh_offset:%d use_dsnh8w:%d replicate_num:%d\n",
                       dsnh_offset, use_dsnh8w, p_member_info->dsmet.replicate_num);

        if (CTC_FLAG_ISSET(p_member_info->dsmet.flag, SYS_NH_DSMET_FLAG_USE_PBM))
        {
            p_member_info->dsmet.replicate_num += 1;
            if (p_mem_param->destid < 32)
            {
                CTC_BIT_SET(p_member_info->dsmet.pbm0, p_mem_param->destid);
            }
            else if (p_mem_param->destid < 64)
            {
                CTC_BIT_SET(p_member_info->dsmet.pbm1, (p_mem_param->destid - 32));
            }

            p_info_mcast->physical_replication_num += 1;

        }
        else if (SYS_NH_PARAM_IPMC_MEM_LOCAL_LOGIC_REP == p_mem_param->member_type ||
			SYS_NH_PARAM_MCAST_MEM_LOCAL_LOGIC_REP == p_mem_param->member_type)
        {
            p_member_info->dsmet.replicate_num += 1;
        }
        else
        {
            return CTC_E_NONE;
        }

        p_member_info->dsmet.flag |= use_dsnh8w ? SYS_NH_DSMET_FLAG_USE_DSNH8W : 0;
        CTC_ERROR_RETURN(_sys_greatbelt_nh_mcast_write_dsmet(lchip, &p_member_info->dsmet));
    }
    else
    {

        ret = _sys_greatbelt_nh_mcast_add_dsmet_entry(lchip, p_mem_param,
                                                      basic_met_offset,
                                                      dsnh_offset,
                                                      use_dsnh8w,
                                                      p_db_member_list,
                                                      p_info_mcast);
        if (ret == CTC_E_NONE)
        {
            p_info_mcast->physical_replication_num += 1;
        }
    }

    return ret;
}

int32
_sys_greatbelt_nh_mcast_remove_member(uint8 lchip, sys_nh_param_mcast_member_t* p_mem_param,
                                      ctc_list_pointer_t* p_db_member_list,
                                      sys_nh_info_mcast_t* p_info_mcast)
{
    sys_nh_mcast_meminfo_t* p_member_info = NULL;
    bool entry_exist = FALSE;
    uint8 phy_if_first_met = 0;
    uint32 repli_pos;

    /*1. Check if mcast member have been existed*/
    if (p_mem_param->is_linkagg &&
        (p_mem_param->destid > SYS_GB_MAX_LINKAGG_GROUP_NUM))
    {
        return CTC_E_INVALID_TID;
    }

    p_mem_param->is_mirror = p_info_mcast->is_mirror;
    if (p_mem_param->is_mirror
        && (p_mem_param->member_type == SYS_NH_PARAM_MCAST_MEM_LOCAL_WITH_NH))
    {
        p_mem_param->member_type = SYS_NH_PARAM_MCAST_MEM_MIRROR_WITH_NH;
    }

    if (p_info_mcast->is_logic_rep_en && (p_mem_param->member_type == SYS_NH_PARAM_IPMC_MEM_LOCAL))
    {
        p_mem_param->member_type = SYS_NH_PARAM_IPMC_MEM_LOCAL_LOGIC_REP;
    }

    if (p_mem_param->member_type == SYS_NH_PARAM_MCAST_MEM_LOCAL_WITH_NH)
    {
        uint8 repli_en = 0;
        sys_greatbelt_nh_is_logic_repli_en(lchip, p_mem_param->ref_nhid, &repli_en);
        if (repli_en)
        {
            p_mem_param->member_type = SYS_NH_PARAM_MCAST_MEM_LOCAL_LOGIC_REP;
        }
    }

    /*Remove remote member*/
    if (SYS_NH_PARAM_MCAST_MEM_REMOTE == p_mem_param->member_type)
    {
        /* mcast trunk profile, check exist */
        if ((0 == p_info_mcast->stacking_mcast_profile_id)
            ||(p_info_mcast->stacking_mcast_profile_id != p_mem_param->destid))
        {
            return CTC_E_NOT_EXIST;
        }

        CTC_ERROR_RETURN(_sys_greatbelt_nh_mcast_remove_remote_dsmet_entry( lchip,  p_mem_param,
                                                                            p_info_mcast->basic_met_offset,
                                                                            p_db_member_list,
                                                                            p_info_mcast));
        return CTC_E_NONE;
    }



    CTC_ERROR_RETURN(_sys_greatbelt_nh_mcast_analyze_member(lchip, p_db_member_list,
                                                            p_mem_param, &p_member_info,  &entry_exist, &repli_pos));
    if (!entry_exist || NULL == p_member_info)
    {
        return CTC_E_MEMBER_NOT_EXIST;
    }

    CTC_ERROR_RETURN(_sys_greatbelt_nh_mcast_free_nh_and_edit_data(lchip,
                                                                   p_member_info,  repli_pos));

    if (p_member_info->dsmet.replicate_num == 0)
    {
        /*Remove member node*/

        CTC_ERROR_RETURN(_sys_greatbelt_nh_mcast_del_dsmet_entry(lchip,
                                                                 p_member_info,
                                                                 p_db_member_list,
                                                                 &phy_if_first_met,
                                                                 p_info_mcast));

        if(!phy_if_first_met)
        {
            p_info_mcast->physical_replication_num -= 1;
        }
    }
    else
    {
        if (CTC_FLAG_ISSET(p_member_info->dsmet.flag, SYS_NH_DSMET_FLAG_USE_PBM))
        {
            p_member_info->dsmet.replicate_num -= 1;
            p_info_mcast->physical_replication_num -= 1;
            if (p_mem_param->destid < 32)
            {
                CTC_BIT_UNSET(p_member_info->dsmet.pbm0, p_mem_param->destid);
            }
            else if (p_mem_param->destid < 64)
            {
                CTC_BIT_UNSET(p_member_info->dsmet.pbm1, (p_mem_param->destid - 32));
            }
        }
        else if (SYS_NH_PARAM_IPMC_MEM_LOCAL_LOGIC_REP == p_mem_param->member_type ||
			SYS_NH_PARAM_MCAST_MEM_LOCAL_LOGIC_REP == p_mem_param->member_type)
        {
            p_member_info->dsmet.replicate_num -= 1;
        }

        CTC_ERROR_RETURN(_sys_greatbelt_nh_mcast_write_dsmet(lchip, &p_member_info->dsmet));
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_nh_mcast_get_l3if_type(uint8 * l3if_type, uint16 dest_id,uint16 vlan_id,
                                                uint8 lchip, uint8 is_linkagg)
{
    int32 ret = CTC_E_NONE;
    sys_l3if_prop_t l3if_prop;
    uint16 gport = 0;
    uint8 gchip = 0;

    sys_greatbelt_get_gchip_id(lchip, &gchip);
    if (is_linkagg)
    {
         gport = 0x1F00 + dest_id;
    }
    else
    {
        gport = CTC_MAP_LPORT_TO_GPORT(gchip, (dest_id & 0xFF));
    }

    sal_memset(&l3if_prop,0,sizeof(sys_l3if_prop_t));
    l3if_prop.gport = gport;
    l3if_prop.vlan_id = vlan_id;

    l3if_prop.l3if_type = CTC_L3IF_TYPE_PHY_IF;
    ret = sys_greatbelt_l3if_get_l3if_info(lchip, 0, &l3if_prop);
    if (CTC_E_NONE == ret)
    {
        *l3if_type = CTC_L3IF_TYPE_PHY_IF;
        return CTC_E_NONE;
    }

    l3if_prop.l3if_type = CTC_L3IF_TYPE_VLAN_IF;
    ret = sys_greatbelt_l3if_get_l3if_info(lchip, 0, &l3if_prop);
    if (CTC_E_NONE == ret)
    {
        *l3if_type = CTC_L3IF_TYPE_VLAN_IF;
        return CTC_E_NONE;
    }

    l3if_prop.l3if_type = CTC_L3IF_TYPE_SUB_IF;
    ret = sys_greatbelt_l3if_get_l3if_info(lchip, 0, &l3if_prop);
    if (CTC_E_NONE == ret)
    {
        *l3if_type = CTC_L3IF_TYPE_SUB_IF;
        return CTC_E_NONE;
    }
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_nh_mcast_set_info(uint16 uncastid, uint8 lchip, uint16 vlan_id,
                                           sys_nh_info_dsmet_t * p_dsmet,uint32 type,
                                           ctc_mcast_nh_param_member_t ** pp_member_info, uint16 vid_index)
{
    uint8 gchip = 0;
    uint8 is_linkagg = 0;
    is_linkagg = CTC_FLAG_ISSET(p_dsmet->flag,SYS_NH_DSMET_FLAG_IS_LINKAGG);


    if (is_linkagg)
    {
        gchip = 0x1F;
    }
    else
    {
        sys_greatbelt_get_gchip_id(lchip, &gchip);
    }

    (*pp_member_info)->member_type  = type;
    (*pp_member_info)->is_source_check_dis = !CTC_FLAG_ISSET(p_dsmet->flag, SYS_NH_DSMET_FLAG_PORT_CHK_DISCARD)? 1:0;

    if ( CTC_NH_PARAM_MEM_LOCAL_WITH_NH == type)
    {
        (*pp_member_info)->ref_nhid    = p_dsmet->ref_nhid;
    }
    else
    {
        (*pp_member_info)->destid       = ( ( gchip & 0xFF ) << 8) + ( uncastid & 0xFF);

        if ((SYS_NH_PARAM_IPMC_MEM_LOCAL_LOGIC_REP == p_dsmet->member_type)&&(p_dsmet->replicate_num))
        {
            (*pp_member_info)->vid          = p_dsmet->vid_list[vid_index];
        }
        else
        {
            (*pp_member_info)->vid          = vlan_id;
        }

        (*pp_member_info)->is_vlan_port = 0;
        _sys_greatbelt_nh_mcast_get_l3if_type(&((*pp_member_info)->l3if_type),
                                              (*pp_member_info)->destid, (*pp_member_info)->vid, lchip, is_linkagg);
    }

    (*pp_member_info)++;
    sal_memset((*pp_member_info), 0, sizeof(ctc_mcast_nh_param_member_t));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_nh_mcast_member_cnt(uint8 lchip, uint32 * p_valid_cnt, uint32 * p_member_idx,
                                                ctc_nh_info_t* p_nh_info)
{
    (*p_member_idx) ++;

    if( * p_member_idx < (p_nh_info->start_index + 1) )
    {
        /* -1 means start index not reach , should continue */
        return -1;
    }

    if (p_nh_info->buffer_len <= * p_valid_cnt)
    {
        /* 1 means buff full , should return */
        p_nh_info->next_query_index = *p_member_idx - 1;
        return 1;
    }

    (*p_valid_cnt) ++;
    return 0;
}

STATIC int32
_sys_greatbelt_nh_mcast_map_member_info(ctc_mcast_nh_param_member_t ** pp_member_info, uint8 lchip,
                                                      sys_nh_info_dsmet_t * p_dsmet, ctc_nh_info_t* p_nh_info,
                                                      uint32 * p_valid_cnt, uint32 * p_member_idx)
{
    int32 ret = 0;
    uint8 loop = 0;
    uint32 temp = 0;

    sal_memset((*pp_member_info),0,sizeof(ctc_mcast_nh_param_member_t));

    if ((SYS_NH_PARAM_IPMC_MEM_LOCAL == p_dsmet->member_type)
        && (CTC_FLAG_ISSET(p_dsmet->flag, SYS_NH_DSMET_FLAG_USE_PBM)))
    {


        for (loop = 0; loop < 56 ; loop ++)
        {
            temp = (loop < 32) ? p_dsmet->pbm0 : p_dsmet->pbm1;

            if (!CTC_IS_BIT_SET(temp, ((loop < 32)? loop : loop - 32)))
            {
                continue;
            }

            ret = _sys_greatbelt_nh_mcast_member_cnt(lchip, p_valid_cnt, p_member_idx, p_nh_info);

            if (-1 == ret)
            {
                continue;
            }
            if (1 == ret)
            {
                return CTC_E_EXCEED_MAX_SIZE;
            }
            _sys_greatbelt_nh_mcast_set_info(loop, lchip, p_dsmet->vid,p_dsmet,
                                            CTC_NH_PARAM_MEM_IPMC_LOCAL, pp_member_info, 0);
        }

    }
    else if ((SYS_NH_PARAM_IPMC_MEM_LOCAL == p_dsmet->member_type)
            ||(SYS_NH_PARAM_MCAST_MEM_LOCAL_WITH_NH == p_dsmet->member_type))
    {
        uint32 type = CTC_NH_PARAM_MEM_LOCAL_WITH_NH;
        if (SYS_NH_PARAM_IPMC_MEM_LOCAL == p_dsmet->member_type)
        {
            type = CTC_NH_PARAM_MEM_IPMC_LOCAL;
        }

        ret = _sys_greatbelt_nh_mcast_member_cnt(lchip, p_valid_cnt, p_member_idx, p_nh_info);

        if (-1 == ret)
        {
            return CTC_E_NONE;
        }
        if (1 == ret)
        {
            return CTC_E_EXCEED_MAX_SIZE;
        }

        _sys_greatbelt_nh_mcast_set_info(p_dsmet->ucastid, lchip, p_dsmet->vid,p_dsmet,
                                            type, pp_member_info, 0);
    }
    else if (SYS_NH_PARAM_IPMC_MEM_LOCAL_LOGIC_REP == p_dsmet->member_type)
    {
        for (loop = 0; loop <= p_dsmet->replicate_num ; loop ++)
        {
            ret = _sys_greatbelt_nh_mcast_member_cnt(lchip, p_valid_cnt, p_member_idx, p_nh_info);

            if (-1 == ret)
            {
                continue;
            }
            if (1 == ret)
            {
                return CTC_E_EXCEED_MAX_SIZE;
            }
            _sys_greatbelt_nh_mcast_set_info(p_dsmet->ucastid, lchip, p_dsmet->vid,p_dsmet,
                                            CTC_NH_PARAM_MEM_IPMC_LOCAL, pp_member_info, loop);
        }
    }

    p_nh_info->next_query_index = *p_member_idx - 1;

    return CTC_E_NONE;

}

/**
 @brief This function is used to get mcast member info
 */
int32
sys_greatbelt_nh_get_mcast_member_info(uint8 lchip, sys_nh_info_mcast_t* p_mcast_db, ctc_nh_info_t* p_nh_info)
{
    ctc_list_pointer_t* p_db_member_list = NULL;
    sys_nh_mcast_meminfo_t* p_meminfo = NULL;
    ctc_list_pointer_node_t* p_pos_mem = NULL;
    sys_nh_info_dsmet_t * p_dsmet = NULL;
    ctc_mcast_nh_param_member_t * p_member_info = NULL;
    uint32 valid_cnt = 0;
    uint32 member_idx = 0;
    uint32 ret = CTC_E_NONE;
    uint8 not_end = 0;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);


    p_nh_info->valid_number = 0;
    p_member_info = p_nh_info->buffer;
    member_idx = 0;
    valid_cnt = 0;

    p_db_member_list = &(p_mcast_db->p_mem_list);

    CTC_LIST_POINTER_LOOP(p_pos_mem, p_db_member_list)
    {
        p_meminfo = _ctc_container_of(p_pos_mem, sys_nh_mcast_meminfo_t, list_head);
        p_dsmet = &(p_meminfo->dsmet);
        ret = _sys_greatbelt_nh_mcast_map_member_info(&p_member_info, lchip, p_dsmet, p_nh_info,
                                                      &valid_cnt, &member_idx);
        if (CTC_E_NONE != ret)
        {
            not_end = 1;
            break;
        }
    }
    if (not_end)
    {

    }


    if (!not_end)
    {
        p_nh_info->is_end = 1;
        p_nh_info->next_query_index = 0;
    }
    p_nh_info->valid_number = valid_cnt;

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_nh_mcast_add_dsfwd(uint8 lchip, sys_nh_info_com_t* p_com_db)
{
    sys_nh_info_mcast_t* p_nh_info = NULL;
    sys_nh_param_dsfwd_t dsfwd_param;
    int32 ret = 0;

    p_nh_info     = (sys_nh_info_mcast_t*)(p_com_db);
    sal_memset(&dsfwd_param, 0, sizeof(sys_nh_param_dsfwd_t));

    /*Build DsFwd Table and write table*/
    if (!CTC_FLAG_ISSET(p_nh_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD))
    {
       ret = sys_greatbelt_nh_offset_alloc(lchip, SYS_NH_ENTRY_TYPE_FWD, 1, &p_nh_info->hdr.dsfwd_info.dsfwd_offset);
    }
    CTC_SET_FLAG(p_nh_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD);

    sys_greatbelt_get_gchip_id(lchip, & dsfwd_param.dest_chipid);


    dsfwd_param.drop_pkt = FALSE;
    dsfwd_param.dest_id = 0xFFFF & (p_nh_info->basic_met_offset);
    dsfwd_param.is_mcast = TRUE;
    dsfwd_param.dsfwd_offset = p_nh_info->hdr.dsfwd_info.dsfwd_offset;
    dsfwd_param.dsnh_offset = 0;
    dsfwd_param.stats_ptr = p_nh_info->hdr.dsfwd_info.stats_ptr;
    /*Write table*/
    ret = sys_greatbelt_nh_write_entry_dsfwd(lchip, &dsfwd_param);

    if (ret != CTC_E_NONE)
    {
      sys_greatbelt_nh_offset_free(lchip,  SYS_NH_ENTRY_TYPE_FWD, 1, p_nh_info->hdr.dsfwd_info.dsfwd_offset);

    }
    return ret;
}



/**
 @brief Callback function to create multicast bridge nexthop

 @param[in] p_com_nh_para, parameters used to create bridge nexthop

 @param[out] p_com_db, pointer used to store nexthop data

 @return CTC_E_XXX
 */
int32
sys_greatbelt_nh_create_mcast_cb(uint8 lchip, sys_nh_param_com_t* p_com_nh_para,
                                 sys_nh_info_com_t* p_com_db)
{

    sys_nh_param_mcast_t* p_nh_para_mcast;
    sys_nh_info_mcast_t* p_mcast_db;
    uint32 dsfwd_offset;
    uint8 gchip;
    sys_nh_param_dsfwd_t dsfwd_param;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    /* 1. sanity check & init */
    CTC_PTR_VALID_CHECK(p_com_nh_para);
    CTC_PTR_VALID_CHECK(p_com_db);
    CTC_EQUAL_CHECK(SYS_NH_TYPE_MCAST, p_com_nh_para->hdr.nh_param_type);
    p_nh_para_mcast = (sys_nh_param_mcast_t*)p_com_nh_para;
    p_mcast_db = (sys_nh_info_mcast_t*)p_com_db;
    sal_memset(&dsfwd_param, 0, sizeof(sys_nh_param_dsfwd_t));
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "nhType = %d, NHID = %d, isInternaNH = %d, \n\
        GroupId = %d, Opcode = %d\n",
                   p_nh_para_mcast->hdr.nh_param_type,
                   p_nh_para_mcast->hdr.nhid,
                   p_nh_para_mcast->hdr.is_internal_nh,
                   p_nh_para_mcast->groupid,
                   p_nh_para_mcast->opcode);

    /*2. Malloc and init DB structure*/
    p_mcast_db->hdr.nh_entry_type = SYS_NH_TYPE_MCAST;
    p_mcast_db->basic_met_offset = p_nh_para_mcast->groupid;
    p_mcast_db->is_mirror = p_nh_para_mcast->is_mirror;
    p_mcast_db->nhid = p_nh_para_mcast->hdr.nhid;
    CTC_ERROR_RETURN(sys_greatbelt_nh_check_glb_met_offset(lchip, p_mcast_db->basic_met_offset, 1, TRUE));

    if (p_nh_para_mcast->hdr.have_dsfwd)
    {
        CTC_SET_FLAG(p_mcast_db->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD);
    }

    CTC_ERROR_RETURN(sys_greatbelt_stacking_get_mcast_profile_met_offset(lchip, p_mcast_db->stacking_mcast_profile_id, &p_mcast_db->stacking_met_offset));

    ctc_list_pointer_init(&(p_mcast_db->p_mem_list));

    /*Init basic dsmet entry*/
    CTC_ERROR_RETURN(_sys_greatbelt_nh_mcast_entry_set_default(lchip,p_mcast_db,
                                                               p_mcast_db->basic_met_offset, TRUE));
    if (p_nh_para_mcast->hdr.have_dsfwd)
    {
        /*get stats ptr form stats module*/
        if (p_com_nh_para->hdr.stats_valid)
        {
            CTC_ERROR_RETURN(sys_greatbelt_stats_get_statsptr(lchip, p_com_nh_para->hdr.stats_id, &p_nh_para_mcast->hdr.stats_ptr));
            p_mcast_db->hdr.dsfwd_info.stats_ptr = p_nh_para_mcast->hdr.stats_ptr;
            dsfwd_param.stats_ptr = p_nh_para_mcast->hdr.stats_ptr;
        }
        /*Build DsFwd Table and write table*/
        CTC_ERROR_RETURN(sys_greatbelt_nh_offset_alloc(lchip, SYS_NH_ENTRY_TYPE_FWD, 1, &dsfwd_offset));
        p_mcast_db->hdr.dsfwd_info.dsfwd_offset = dsfwd_offset;
        p_nh_para_mcast->fwd_offset = dsfwd_offset;
        /*Writeback dsfwd offset*/

        sys_greatbelt_get_gchip_id(lchip, &gchip);
        dsfwd_param.dest_chipid = gchip;
        dsfwd_param.drop_pkt = FALSE;
        dsfwd_param.dest_id = 0xFFFF & (p_mcast_db->basic_met_offset);
        dsfwd_param.is_mcast = TRUE;
        dsfwd_param.dsfwd_offset = dsfwd_offset;
        dsfwd_param.dsnh_offset = 0; /*This value should be 0 for multicast,
        this nexthopPtr will be stored in humberHeader,
                                      For cross - chip case, this nexthopPtr will send from
        local chip to remote chip*/

        CTC_SET_FLAG(p_mcast_db->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD);
        /*Write table*/
        CTC_ERROR_RETURN(sys_greatbelt_nh_write_entry_dsfwd(lchip, &dsfwd_param));
    }


    CTC_ERROR_RETURN(sys_greatbelt_nh_set_glb_met_offset(lchip, p_mcast_db->basic_met_offset, 1, TRUE));
    sys_greatbelt_nh_add_mcast_id(lchip, p_nh_para_mcast->groupid, p_mcast_db);

    p_mcast_db->is_logic_rep_en = sys_greatbelt_nh_is_ipmc_logic_rep_enable(lchip)?1:0;
    
    return CTC_E_NONE;
}

/**
 @brief Callback function to delete multicast bridge nexthop

 @param[in] p_data, pointer used to store nexthop data

 @return CTC_E_XXX
 */
int32
sys_greatbelt_nh_delete_mcast_cb(uint8 lchip, sys_nh_info_com_t* p_data, uint32* p_nhid)
{

    sys_nh_info_mcast_t* p_mcast_db;
    ctc_list_pointer_node_t* p_pos, * p_pos_next;
    sys_nh_mcast_meminfo_t* p_member;
    sys_nh_param_dsfwd_t dsfwd_param;
    bool first_node = TRUE;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    CTC_PTR_VALID_CHECK(p_data);
    CTC_EQUAL_CHECK(SYS_NH_TYPE_MCAST, p_data->hdr.nh_entry_type);
    p_mcast_db = (sys_nh_info_mcast_t*)(p_data);

    sal_memset(&dsfwd_param, 0, sizeof(sys_nh_param_dsfwd_t));


    first_node = TRUE;
    /*Free basic met offset*/
    CTC_ERROR_RETURN(_sys_greatbelt_nh_mcast_entry_set_default(lchip,p_mcast_db,
                                                               p_mcast_db->basic_met_offset, FALSE));

    CTC_LIST_POINTER_LOOP_DEL(p_pos, p_pos_next, &(p_mcast_db->p_mem_list))
    {
        p_member = _ctc_container_of(p_pos, sys_nh_mcast_meminfo_t, list_head);
        if (first_node)
        {
            first_node = FALSE;
        }
        else
        {
            if (p_member->dsmet.member_type == SYS_NH_PARAM_IPMC_MEM_LOCAL
                && CTC_FLAG_ISSET(p_member->dsmet.flag, SYS_NH_DSMET_FLAG_USE_PBM))
            {

                CTC_ERROR_RETURN(sys_greatbelt_nh_offset_free(lchip, \
                SYS_NH_ENTRY_TYPE_MET_FOR_IPMCBITMAP, 1, p_member->dsmet.dsmet_offset));

            }
            else
            {
                CTC_ERROR_RETURN(sys_greatbelt_nh_offset_free(lchip, \
                SYS_NH_ENTRY_TYPE_MET, 1, p_member->dsmet.dsmet_offset));
            }
        }

        if (p_member->dsmet.vid_list)
        {
            mem_free(p_member->dsmet.vid_list);
        }

        mem_free(p_member);

    }

    if (CTC_FLAG_ISSET(p_mcast_db->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD))
    {
        CTC_ERROR_RETURN(sys_greatbelt_nh_offset_free(lchip, \
        SYS_NH_ENTRY_TYPE_FWD, 1, \
        p_mcast_db->hdr.dsfwd_info.dsfwd_offset));
        dsfwd_param.dsfwd_offset = p_mcast_db->hdr.dsfwd_info.dsfwd_offset;
        dsfwd_param.drop_pkt = 1;
        dsfwd_param.dsnh_offset = 0;
        /*Write table*/
        CTC_ERROR_RETURN(sys_greatbelt_nh_write_entry_dsfwd(lchip, &dsfwd_param));
    }


    CTC_ERROR_RETURN(sys_greatbelt_nh_set_glb_met_offset(lchip, p_mcast_db->basic_met_offset, 1, FALSE));

    sys_greatbelt_nh_remove_mcast_id(lchip, p_mcast_db->basic_met_offset);

    return CTC_E_NONE;
}

/**
 @brief Callback function used to update bridge multicast nexthop

 @param[in] p_nh_ptr, pointer of multicast nexthop DB

 @param[in] p_para, member information

 @return CTC_E_XXX
 */
int32
sys_greatbelt_nh_update_mcast_cb(uint8 lchip, sys_nh_info_com_t* p_nh_info,
                                 sys_nh_param_com_t* p_para /*Member info*/)
{

    sys_nh_param_mcast_t* p_nh_para_mcast;
    sys_nh_info_mcast_t* p_mcast_db;
    uint8 gchip;
    sys_nh_param_dsfwd_t dsfwd_param;
    bool update_dsfwd = FALSE;
    uint8 xgpon_en = 0;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    CTC_PTR_VALID_CHECK(p_para);
    CTC_PTR_VALID_CHECK(p_nh_info);
    CTC_EQUAL_CHECK(SYS_NH_TYPE_MCAST, p_para->hdr.nh_param_type);
    if (SYS_NH_TYPE_IP_TUNNEL != p_nh_info->hdr.nh_entry_type)
    {
        CTC_EQUAL_CHECK(SYS_NH_TYPE_MCAST, p_nh_info->hdr.nh_entry_type);
    }

    p_nh_para_mcast = (sys_nh_param_mcast_t*)(p_para);

    if(p_nh_para_mcast->hdr.change_type == SYS_NH_CHANGE_TYPE_ADD_DYNTBL)
    {
          if (!CTC_FLAG_ISSET(p_nh_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD))
          {
              CTC_ERROR_RETURN(_sys_greatbelt_nh_mcast_add_dsfwd(lchip,  p_nh_info));
          }
          return CTC_E_NONE;
    }

    if (NULL == p_nh_para_mcast->p_member)
    {
        return CTC_E_NONE;
    }

    sal_memset(&dsfwd_param, 0, sizeof(sys_nh_param_dsfwd_t));
    p_mcast_db = (sys_nh_info_mcast_t*)(p_nh_info);
    sys_greatbelt_global_get_xgpon_en(lchip, &xgpon_en);
    if(!xgpon_en)
    {
        CTC_ERROR_RETURN(sys_greatbelt_stacking_get_mcast_profile_met_offset(lchip, p_mcast_db->stacking_mcast_profile_id, &p_mcast_db->stacking_met_offset));
    }

    if (p_nh_para_mcast->p_member->member_type == SYS_NH_PARAM_BRGMC_MEM_LOCAL_WITH_APS_BRIDGE
        || p_nh_para_mcast->p_member->member_type == SYS_NH_PARAM_MCAST_MEM_LOCAL_WITH_NH )
    {
        sys_nh_info_com_t* p_nhinfo;
        CTC_ERROR_RETURN(sys_greatbelt_nh_get_nhinfo_by_nhid(lchip,   p_nh_para_mcast->p_member->ref_nhid, &p_nhinfo));
        if (p_nhinfo && CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_LOGIC_PORT))
        {
            if (SYS_NH_TYPE_BRGUC == p_nhinfo->hdr.nh_entry_type)
            {
                p_nh_para_mcast->p_member->logic_port = ((sys_nh_info_brguc_t*)p_nhinfo)->dest_logic_port;
            }
            else if(SYS_NH_TYPE_MPLS == p_nhinfo->hdr.nh_entry_type)
            {
                p_nh_para_mcast->p_member->logic_port = ((sys_nh_info_mpls_t*)p_nhinfo)->dest_logic_port;
            }
            p_nh_para_mcast->p_member->is_logic_port = 1;

        }

        if (p_nhinfo && CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HORIZON_SPLIT_EN))
        {
            p_nh_para_mcast->p_member->is_horizon_split = 1;
        }

    }

    switch (p_nh_para_mcast->opcode)
    {
    case SYS_NH_PARAM_MCAST_ADD_MEMBER:
        if (ctc_list_pointer_empty(&(p_mcast_db->p_mem_list)))
        {
            update_dsfwd = TRUE;
        }

        CTC_ERROR_RETURN(_sys_greatbelt_nh_mcast_add_member(lchip, p_mcast_db->basic_met_offset,
                                                            p_nh_para_mcast->p_member,
                                                            &(p_mcast_db->p_mem_list),
                                                            p_mcast_db));

        if (update_dsfwd
            && CTC_FLAG_ISSET(p_mcast_db->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD))
        {
            sys_greatbelt_get_gchip_id(lchip, &gchip);
            dsfwd_param.dsfwd_offset = p_mcast_db->hdr.dsfwd_info.dsfwd_offset;
            dsfwd_param.dest_chipid = gchip;
            dsfwd_param.drop_pkt = FALSE;
            dsfwd_param.dest_id = 0xFFFF & (p_mcast_db->basic_met_offset);
            dsfwd_param.is_mcast = TRUE;
            dsfwd_param.dsnh_offset = 0;    /*This value should be 0 for multicast*/
            dsfwd_param.stats_ptr = p_mcast_db->hdr.dsfwd_info.stats_ptr;
            /*Write table*/
            CTC_ERROR_RETURN(sys_greatbelt_nh_write_entry_dsfwd(lchip, &dsfwd_param));
        }

        break;

    case SYS_NH_PARAM_MCAST_DEL_MEMBER:
        CTC_ERROR_RETURN(_sys_greatbelt_nh_mcast_remove_member(lchip, p_nh_para_mcast->p_member,
                                                               &(p_mcast_db->p_mem_list),
                                                               p_mcast_db));
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_create_mcast_aps(uint8 lchip, sys_nh_info_oam_aps_t* p_oam_aps , uint32* group_id)
{
    int32 ret = CTC_E_NONE;
    uint32 dsmet_offset;
    sys_nh_info_dsmet_t dsmet_info;
    bool stacking_en = FALSE;
    uint32 stacking_mcast_offset = 0;

    sal_memset(&dsmet_info, 0, sizeof(sys_nh_info_dsmet_t));

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    /* 1. sanity check & init */
    CTC_PTR_VALID_CHECK(group_id);
    CTC_PTR_VALID_CHECK(p_oam_aps);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "ApsGroupId = %d, lchip = %d\n",
                    p_oam_aps->aps_group_id, lchip);

    /*2. Malloc Index*/
    CTC_ERROR_RETURN(sys_greatbelt_nh_offset_alloc(lchip, SYS_NH_ENTRY_TYPE_MET, 1, &dsmet_offset));

    /*3.Write asic*/
    if (p_oam_aps->nexthop_is_8w)
    {
        CTC_SET_FLAG(dsmet_info.flag, SYS_NH_DSMET_FLAG_USE_DSNH8W);
    }
    dsmet_info.dsmet_offset     = dsmet_offset;
    dsmet_info.ucastid          = p_oam_aps->aps_group_id;
    dsmet_info.dsnh_offset      = p_oam_aps->dsnh_offset;

    sys_greatbelt_stacking_get_enable(lchip, &stacking_en, &stacking_mcast_offset);
    if ( stacking_en)
    {
        dsmet_info.next_dsmet_offset = stacking_mcast_offset;
    }
    else
    {
      dsmet_info.next_dsmet_offset = SYS_NH_MET_END_REPLICATE_OFFSET;
    }
    dsmet_info.member_type      = SYS_NH_PARAM_BRGMC_MEM_LOCAL_WITH_APS_BRIDGE;


    ret = _sys_greatbelt_nh_mcast_write_dsmet(lchip, &dsmet_info);
    if (CTC_E_NONE != ret)
    {
        sys_greatbelt_nh_offset_free(lchip, SYS_NH_ENTRY_TYPE_MET, 1, dsmet_offset);
    }
    *group_id = dsmet_offset;

    return ret;
}

int32
sys_greatbelt_nh_delete_mcast_aps(uint8 lchip, uint32 group_id)
{
    int32 ret = CTC_E_NONE;
    ds_met_entry_t dsmet;

    sal_memset(&dsmet, 0, sizeof(ds_met_entry_t));

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    /* 1. sanity check & init */
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "MutiGroupId = %d, lchip = %d\n", group_id, lchip);

    /*2. Clear asic*/
    CTC_ERROR_RETURN(sys_greatbelt_nh_write_asic_table(lchip, SYS_NH_ENTRY_TYPE_MET,
                                                       group_id, &dsmet));

    /*3. Free index */
    CTC_ERROR_RETURN(sys_greatbelt_nh_offset_free(lchip, SYS_NH_ENTRY_TYPE_MET, 1, group_id));

    return ret;
}



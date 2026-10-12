/**
 @file sys_greatbelt_nexthop_ecmp.c

 @date 2009-12-23

 @version v2.0

 The file contains all ecmp related function
*/

/****************************************************************************
*
* Header Files
*
****************************************************************************/
#include "ctc_const.h"
#include "ctc_error.h"
#include "sys_greatbelt_chip.h"
#include "sys_greatbelt_nexthop_api.h"
#include "sys_greatbelt_nexthop.h"
#include "sys_greatbelt_l3if.h"
#include "sys_greatbelt_stats.h"

#include "greatbelt/include/drv_io.h"
#include "greatbelt/include/drv_tbl_reg.h"

/*
if fixed max ecmp number is  8.
About p_sys_greatbelt_nh_master->ecmp_mode usage.*/

/**<if set,DsIPDa/DsMPLS's equal_cost_path_num should be use actual member number of a ECMP group ,
      so user must update DsIPDa/DsMPLS when ecmp member change by call ctc_ipuc_add() and ctc_mpls_update_ilm().
       in this case(case 1), ecmp should be in balance ;
    else DsIPDa/DsMPLS's equal_cost_path_num is fixed (configured max_ecmp),ecmp members of the changes will
        not affect the DsIPDa/DsMPLS,but in this case(case 2),ecmp may not be in balance.*/

/*
 1) ecmp add member
  Case 1:
   DsIpda.equal_cost_path_num = 8 (fixed)
  Case 2:
    DsIpda.equal_cost_path_num = 3  (actual)

 ECMP group : 100 ,member:1,2,3
 |-------------|
 |       1     |
 |-------------|
 |       2     |
 |-------------|
 |       3     |
 |-------------|
 |       1     |
 |-------------|
 |       2     |
 |-------------|
 |       3     |
 |-------------|
 |       1 (3) |
 |-------------|
 |       2 (2) |
 |-------------|

 *after add a member nh 4 , becomes:
 Case 1:
    DsIpda.equal_cost_path_num = 8 (fixed)
 Case 2:
    DsIpda.equal_cost_path_num = 4  (actual)
    if user don't update DsIpda.equal_cost_path_num, nh4 is inavlid in the ecmp group.

  |-------------|
  |       1     |
  |-------------|
  |       2     |
  |-------------|
  |       3     |
  |-------------|
  |       4     |
  |-------------|
  |       1     |
  |-------------|
  |       2     |
  |-------------|
  |       3     |
  |-------------|
  |       4     |
  |-------------|

 2) ecmp remove member
 Case 1:
    DsIpda.equal_cost_path_num = 8 (fixed)
 Case 2:
    DsIpda.equal_cost_path_num = 4 (actual)

|-------------|
|       1     |
|-------------|
|       2     |
|-------------|
|       3     |
|-------------|
|       4     |
|-------------|
|       1     |
|-------------|
|       2     |
|-------------|
|       3     |
|-------------|
|       4     |
|-------------|

 *after remove a member nh 4 , ecmp group becomes:
     Case 1:
        DsIpda.equal_cost_path_num = 8 (fixed)
    Case 2:
        DsIpda.equal_cost_path_num = 3  (actual)
     if user don't update DsIpda.equal_cost_path_num, nh1 will have 50% chance of
      being chosen.
   |-------------|
   |       1     |
   |-------------|
   |       2     |
   |-------------|
   |       3     |
   |-------------|
   |       1     |
   |-------------|
   |       2     |
   |-------------|
   |       3     |
   |-------------|
   |       1     |
   |-------------|
   |       2     |
   |-------------|

 */

#define SYS_ECMP_ALLOC_MEM_STEP  4

/****************************************************************************
*
* Global and Declaration
*
*****************************************************************************/
int32
sys_greatbelt_nh_update_ecmp_group(uint8 lchip, sys_nh_info_ecmp_t* p_nhdb);
int32
sys_greatbelt_nh_delete_ecmp_member(uint8 lchip, sys_nh_info_ecmp_t* p_nhdb, sys_nh_info_com_t* p_nh_info);
int32
sys_greatbelt_nh_add_ecmp_member(uint8 lchip, sys_nh_info_ecmp_t* p_nhdb, sys_nh_info_com_t* p_nh_info);

int32
sys_greatbelt_nh_update_ecmp_member(uint8 lchip, uint32 nhid, uint8 change_type)
{
    sys_nh_info_com_t* p_nh_info = NULL;
    sys_nh_info_ecmp_t* p_nhdb = NULL;
    uint8 ecmp_mem_idx = 0;
    sys_nh_ref_list_node_t* p_curr = NULL;
    uint16 move_num = 0,member_num = 0;
    uint8 loop,loop2 = 0;
    uint32 move_mem_array[SYS_GB_MAX_ECPN] = {0};
    sys_nh_info_dsnh_t nh_info;




    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    sal_memset(&nh_info, 0, sizeof(sys_nh_info_dsnh_t));
    CTC_ERROR_RETURN(sys_greatbelt_nh_get_nhinfo(lchip, nhid, &nh_info));
    CTC_ERROR_RETURN(sys_greatbelt_nh_get_nhinfo_by_nhid(lchip, nhid, (sys_nh_info_com_t**)&p_nh_info));


    p_curr = p_nh_info->hdr.p_ref_nh_list;

    while (p_curr)
    {
        member_num = 0;
        ecmp_mem_idx = 0;
        p_nhdb = (sys_nh_info_ecmp_t*)p_curr->p_ref_nhinfo;
        if (!p_nhdb)
        {
            p_curr = p_curr->p_next;
            continue;
        }

        for (loop = 0; loop < p_nhdb->ecmp_cnt; loop++)
        {
            if (p_nhdb->nh_array[loop] == nhid)
            {
                member_num ++;
                ecmp_mem_idx = loop;
            }
        }

        if (member_num == 0)
        {
            p_curr = p_curr->p_next;
            continue;
        }

        if ((SYS_MAP_CTC_GPORT_TO_GCHIP(nh_info.gport) == 0x1f) && (p_nhdb->is_dlb))
        {
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, " [Nexthop]Ecmp Dlb member cannot support linkagg \n");
            return CTC_E_INVALID_CONFIG;
        }


        switch (change_type)
        {
        case SYS_NH_CHANGE_TYPE_FWD_TO_UNROV:
            if (ecmp_mem_idx < p_nhdb->valid_cnt)
            {
              for (loop = 0,loop2 = 0; loop < p_nhdb->valid_cnt; loop++)
                {
                    if (p_nhdb->nh_array[loop] != nhid)
                    {/*fwd member list*/
                        move_mem_array[loop2++] = p_nhdb->nh_array[loop];
                    }
                }
                move_num = p_nhdb->valid_cnt - member_num;

                for (loop = 0,loop2 =0; loop < p_nhdb->valid_cnt; loop++)
                {
                    if (loop < move_num )
                        p_nhdb->nh_array[loop] = move_mem_array[loop2++];
                    else
                        p_nhdb->nh_array[loop]  =  nhid;

                }
                p_nhdb->valid_cnt -= member_num;
            }

            break;

        case SYS_NH_CHANGE_TYPE_UNROV_TO_FWD:
		case SYS_NH_CHANGE_TYPE_ADD_DYNTBL:
            if (ecmp_mem_idx >= p_nhdb->valid_cnt)
            {

                for (loop = p_nhdb->valid_cnt,loop2 =0; loop < p_nhdb->ecmp_cnt; loop++)
                {
                    if (p_nhdb->nh_array[loop] != nhid)
                    {/*unrov member list*/
                        move_mem_array[loop2++] = p_nhdb->nh_array[loop];
                    }
                }

                for (loop = p_nhdb->valid_cnt, loop2 = 0; loop < p_nhdb->ecmp_cnt; loop++)
                {
                    if (loop < (p_nhdb->valid_cnt + member_num) )
                        p_nhdb->nh_array[loop] = nhid;
                    else
                        p_nhdb->nh_array[loop] =  move_mem_array[loop2++];

                }
                p_nhdb->valid_cnt += member_num;
            }

            break;

        case SYS_NH_CHANGE_TYPE_NH_DELETE:
            for (loop = 0,loop2 =0; loop < p_nhdb->ecmp_cnt; loop++)
            {
                if (p_nhdb->nh_array[loop] != nhid)
                {
                    move_mem_array[loop2++] = p_nhdb->nh_array[loop];
                }
            }
            for (loop = 0,loop2 =0; loop < (p_nhdb->ecmp_cnt - member_num); loop++)
            {
                p_nhdb->nh_array[loop] = move_mem_array[loop2++];
            }
            if (ecmp_mem_idx < p_nhdb->valid_cnt)
            {
                p_nhdb->valid_cnt -= member_num ;
            }
            p_nhdb->ecmp_cnt -= member_num;

            break;

        case SYS_NH_CHANGE_TYPE_UPDATE_FWD_ATTR:
            break;
        }

        sys_greatbelt_nh_update_ecmp_group(lchip, p_nhdb);
        p_curr = p_curr->p_next;
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_add_ecmp_member(uint8 lchip, sys_nh_info_ecmp_t* p_nhdb, sys_nh_info_com_t* p_nh_info)
{
    sys_nh_ref_list_node_t* p_curr = NULL;
    sys_nh_ref_list_node_t* p_nh_ref_list_node = NULL;
    uint8 find_flag = 0;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    DRV_PTR_VALID_CHECK(p_nhdb);
    DRV_PTR_VALID_CHECK(p_nh_info);

    p_curr = p_nh_info->hdr.p_ref_nh_list;

    while (p_curr)
    {
        if ((sys_nh_info_ecmp_t*)p_curr->p_ref_nhinfo == p_nhdb)
        {
            find_flag = 1;
            break;
        }

        p_curr = p_curr->p_next;
    }

    if (find_flag == 0)
    {
        p_nh_ref_list_node = (sys_nh_ref_list_node_t*)mem_malloc(MEM_NEXTHOP_MODULE, sizeof(sys_nh_ref_list_node_t));
        if (p_nh_ref_list_node == NULL)
        {
            return CTC_E_NO_MEMORY;
        }
        p_nh_ref_list_node->p_ref_nhinfo = (sys_nh_info_com_t*)p_nhdb;
        p_nh_ref_list_node->p_next = p_nh_info->hdr.p_ref_nh_list;

        p_nh_info->hdr.p_ref_nh_list = p_nh_ref_list_node;
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_delete_ecmp_member(uint8 lchip, sys_nh_info_ecmp_t* p_nhdb, sys_nh_info_com_t* p_nh_info)
{
    sys_nh_ref_list_node_t* p_curr = NULL;
    sys_nh_ref_list_node_t* p_prev = NULL;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    CTC_PTR_VALID_CHECK(p_nhdb);
    CTC_PTR_VALID_CHECK(p_nh_info);

    p_curr = p_nh_info->hdr.p_ref_nh_list;

    while (p_curr)
    {
        if ((sys_nh_info_ecmp_t*)p_curr->p_ref_nhinfo == p_nhdb)
        {
            if (NULL == p_prev)
            /*Remove first node*/
            {
                p_nh_info->hdr.p_ref_nh_list = p_curr->p_next;
            }
            else
            {
                p_prev->p_next = p_curr->p_next;
            }

            mem_free(p_curr);
            return CTC_E_NONE;
        }

        p_prev = p_curr;
        p_curr = p_curr->p_next;
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_update_ecmp_group(uint8 lchip, sys_nh_info_ecmp_t* p_nhdb)
{
    sys_nh_info_com_t* p_nh_info = NULL;
    int ret = 0;

    uint8 valid_nh_idx = 0;
    uint8 ecmp_mem_idx = 0;
    ds_fwd_t dsfwd;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    DRV_PTR_VALID_CHECK(p_nhdb);


    for (ecmp_mem_idx = 0; ecmp_mem_idx < p_nhdb->mem_num; ecmp_mem_idx++)
    {

        if (p_nhdb->valid_cnt == 0)
        {
            CTC_ERROR_RETURN(sys_greatbelt_nh_get_nhinfo_by_nhid(lchip, CTC_NH_RESERVED_NHID_FOR_DROP, (sys_nh_info_com_t**)&p_nh_info));
        }
        else
        {
            CTC_ERROR_RETURN(sys_greatbelt_nh_get_nhinfo_by_nhid(lchip, p_nhdb->nh_array[valid_nh_idx], (sys_nh_info_com_t**)&p_nh_info));

            if ((++valid_nh_idx) == p_nhdb->valid_cnt)
            {
                valid_nh_idx = 0;
            }
        }
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, " INFO: ecmp_mem_idx:%d()\n", ecmp_mem_idx);


        ret = sys_greatbelt_nh_get_entry_dsfwd(lchip, p_nh_info->hdr.dsfwd_info.dsfwd_offset, &dsfwd);

        if ((0 != p_nhdb->hdr.dsfwd_info.stats_ptr) && (p_nhdb->valid_cnt != 0))
        {
            dsfwd.stats_ptr_low = (p_nhdb->hdr.dsfwd_info.stats_ptr) & 0xFFF;
            dsfwd.stats_ptr_high = ((p_nhdb->hdr.dsfwd_info.stats_ptr) >> 12) & 0xF;
        }

        ret = sys_greatbelt_nh_write_asic_table(lchip, SYS_NH_ENTRY_TYPE_FWD,
                                                p_nhdb->hdr.dsfwd_info.dsfwd_offset + ecmp_mem_idx, &dsfwd);


    }

    return ret;
}

int32
_sys_greatbelt_nh_ecmp_update_member_nh(uint8 lchip, uint32 nhid, sys_nh_info_com_t * p_com_db)
{
    sys_nh_param_com_t nh_param;
    sal_memset(&nh_param, 0, sizeof(sys_nh_param_com_t));
    nh_param.hdr.nhid = nhid;
    nh_param.hdr.nh_param_type = p_com_db->hdr.nh_entry_type;
    nh_param.hdr.change_type  = SYS_NH_CHANGE_TYPE_ADD_DYNTBL;


    if (p_com_db->hdr.nh_entry_type == SYS_NH_TYPE_IPUC)
    {
        CTC_ERROR_RETURN(sys_greatbelt_nh_update_ipuc_cb(lchip, (sys_nh_info_com_t*)p_com_db, (sys_nh_param_com_t*)&nh_param));
    }
    else if(p_com_db->hdr.nh_entry_type == SYS_NH_TYPE_MPLS)
    {
        CTC_ERROR_RETURN(sys_greatbelt_nh_update_mpls_cb(lchip, (sys_nh_info_com_t*)p_com_db, (sys_nh_param_com_t*)&nh_param));
    }
    else if(p_com_db->hdr.nh_entry_type == SYS_NH_TYPE_IP_TUNNEL)
    {
        CTC_ERROR_RETURN(sys_greatbelt_nh_update_ip_tunnel_cb(lchip, (sys_nh_info_com_t*)p_com_db, (sys_nh_param_com_t*)&nh_param));
    }
    else if(p_com_db->hdr.nh_entry_type == SYS_NH_TYPE_MISC)
    {
        CTC_ERROR_RETURN(sys_greatbelt_nh_update_misc_cb(lchip, (sys_nh_info_com_t*)p_com_db, (sys_nh_param_com_t*)&nh_param));
    }
    return CTC_E_NONE;
}
int32
sys_greatbelt_nh_create_ecmp_cb(uint8 lchip, sys_nh_param_com_t* p_com_nh_param, sys_nh_info_com_t* p_com_db)
{
    sys_nh_param_ecmp_t* p_nh_param = NULL;
    ctc_nh_ecmp_nh_param_t* p_nh_ecmp_param = NULL;
    sys_nh_info_ecmp_t* p_nhdb = NULL;
    uint16 max_ecmp = 0;
    uint16 member_num = 0;
    uint8  nh_array_size = 0;
    uint8  loop = 0,loop2 = 0;
    uint8  valid_cnt = 0, unrov_nh_cnt = 0;
    uint32 unrov_nh_array[SYS_GB_MAX_ECPN] = {0};
    uint32 valid_nh_array[SYS_GB_MAX_ECPN] = {0};
    uint32 nh_array[SYS_GB_MAX_ECPN] = {0};
    int32  ret = CTC_E_NONE;

    sys_nh_info_com_t* p_nh_info = NULL;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    DRV_PTR_VALID_CHECK(p_com_nh_param);
    DRV_PTR_VALID_CHECK(p_com_db);

    p_nh_param = (sys_nh_param_ecmp_t*)(p_com_nh_param);
    p_nhdb = (sys_nh_info_ecmp_t*)(p_com_db);
    p_nhdb->hdr.nh_entry_type = SYS_NH_TYPE_ECMP;
    p_nh_ecmp_param = p_nh_param->p_ecmp_param;


    CTC_ERROR_RETURN(sys_greatbelt_nh_get_max_ecmp(lchip, &max_ecmp));

    if (p_nh_ecmp_param->nh_num > max_ecmp)
    {
        return CTC_E_INVALID_PARAM;
    }

    /*1.Judge repeated Member */
    for (loop = 0; loop < p_nh_ecmp_param->nh_num; loop++)
    {
        for (loop2 = 0; loop2 < valid_cnt; loop2++)
        {
            if (p_nh_ecmp_param->nhid[loop] == nh_array[loop2]
                && (p_nh_ecmp_param->type != CTC_NH_ECMP_TYPE_STATIC))
            {
                 /*only staic ecmp support repeated Member*/
                return CTC_E_INVALID_CONFIG;
            }
        }
        nh_array[valid_cnt] = p_nh_ecmp_param->nhid[loop];
        valid_cnt++;

    }
    /* group member num is flex */
    if (max_ecmp == 0)
    {
        member_num = p_nh_ecmp_param->member_num;

        if ((member_num < 2) || (member_num > SYS_GB_MAX_ECPN))
        {
            return CTC_E_INVALID_PARAM;
        }

        if (member_num > 8)
        {
            /*In asic, 9~16 is regarded as 16 */
            member_num = SYS_GB_MAX_ECPN;
        }

        if (p_nh_ecmp_param->nh_num > member_num)
        {
            return CTC_E_NH_EXCEED_MAX_ECMP_NUM;
        }

        p_nhdb->mem_num = member_num;

        member_num = (member_num+SYS_ECMP_ALLOC_MEM_STEP-1)/SYS_ECMP_ALLOC_MEM_STEP*SYS_ECMP_ALLOC_MEM_STEP;
    }
    else  /* group member num is fixed */
    {
        member_num = max_ecmp;
        if (p_nh_ecmp_param->nh_num > member_num)
        {
            return CTC_E_NH_EXCEED_MAX_ECMP_NUM;
        }

        p_nhdb->mem_num = member_num;
    }


    /*2.Judge  Member is FWD or unrsv */
    valid_cnt = 0;

    for (loop = 0; loop < p_nh_ecmp_param->nh_num; loop++)
    {
        sys_nh_info_dsnh_t nh_info;
        sal_memset(&nh_info, 0, sizeof(sys_nh_info_dsnh_t));

        CTC_ERROR_RETURN(sys_greatbelt_nh_get_nhinfo_by_nhid(lchip, p_nh_ecmp_param->nhid[loop], (sys_nh_info_com_t**)&p_nh_info));

        if (p_nh_info->hdr.nh_entry_type != SYS_NH_TYPE_IPUC && p_nh_info->hdr.nh_entry_type != SYS_NH_TYPE_MPLS
            && p_nh_info->hdr.nh_entry_type != SYS_NH_TYPE_IP_TUNNEL && p_nh_info->hdr.nh_entry_type != SYS_NH_TYPE_MISC)
        {
            return CTC_E_NH_INVALID_NH_TYPE;
        }

        CTC_ERROR_RETURN(sys_greatbelt_nh_get_nhinfo(lchip, p_nh_ecmp_param->nhid[loop], &nh_info));
        if ((SYS_MAP_CTC_GPORT_TO_GCHIP(nh_info.gport) == 0x1f) && (p_nh_ecmp_param->type == CTC_NH_ECMP_TYPE_DLB))
        {
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, " [Nexthop]Ecmp Dlb member cannot support linkagg \n");
            return CTC_E_INVALID_CONFIG;
        }

        if (!CTC_FLAG_ISSET(p_nh_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_UNROV)
            && !CTC_FLAG_ISSET(p_nh_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD))
        {
            _sys_greatbelt_nh_ecmp_update_member_nh(lchip, p_nh_ecmp_param->nhid[loop],(sys_nh_info_com_t*)p_nh_info);
        }

        if (!CTC_FLAG_ISSET(p_nh_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_UNROV))
        {
            valid_nh_array[valid_cnt++] = p_nh_ecmp_param->nhid[loop];

        }
        else
        {
            unrov_nh_array[unrov_nh_cnt++] = p_nh_ecmp_param->nhid[loop];
        }
    }

    /*3.Save DB */
    p_nhdb->valid_cnt = valid_cnt;
    p_nhdb->ecmp_cnt = valid_cnt + unrov_nh_cnt;
    p_nhdb->ecmp_nh_id = p_nh_param->hdr.nhid;

    /* process stats */
    if (p_nh_ecmp_param->stats_id)
    {
        CTC_ERROR_RETURN(sys_greatbelt_stats_get_statsptr(lchip, p_nh_ecmp_param->stats_id, &p_nhdb->hdr.dsfwd_info.stats_ptr));
    }

    if (0 != p_nhdb->ecmp_cnt)
    {
        nh_array_size = (p_nhdb->ecmp_cnt + SYS_ECMP_ALLOC_MEM_STEP - 1) / SYS_ECMP_ALLOC_MEM_STEP;
        nh_array_size = nh_array_size * SYS_ECMP_ALLOC_MEM_STEP;

        p_nhdb->nh_array = mem_malloc(MEM_NEXTHOP_MODULE, nh_array_size * sizeof(uint32));

        if (!p_nhdb->nh_array)
        {
            return CTC_E_NO_MEMORY;
        }

        sal_memcpy(&p_nhdb->nh_array[0], &valid_nh_array[0], valid_cnt * sizeof(uint32));
        if (unrov_nh_cnt)
        {
            sal_memcpy(&p_nhdb->nh_array[valid_cnt], &unrov_nh_array[0], unrov_nh_cnt * sizeof(uint32));
        }
    }

    /*4.Create ECMP group */


    /*Build DsFwd Table*/
    ret = sys_greatbelt_nh_offset_alloc(lchip, SYS_NH_ENTRY_TYPE_FWD, member_num,
                                        &p_nhdb->hdr.dsfwd_info.dsfwd_offset);
    if (ret < 0)
    {
        goto error_proc;
    }

    CTC_SET_FLAG(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD);

    sys_greatbelt_nh_update_ecmp_group(lchip, p_nhdb);

    /*5.Add ECMP Nhid to member nexthop's linklist*/
    for (loop = 0; loop < p_nhdb->ecmp_cnt; loop++)
    {
        ret = sys_greatbelt_nh_get_nhinfo_by_nhid(lchip, p_nhdb->nh_array[loop], (sys_nh_info_com_t**)&p_nh_info);
        if (ret < 0)
        {
            p_nhdb->ecmp_cnt = loop;
            goto error_proc;
        }
        sys_greatbelt_nh_add_ecmp_member(lchip, p_nhdb, p_nh_info);
    }
error_proc:
    if (ret == CTC_E_NONE)
    {
        return ret;
    }

    for (loop = 0; loop < p_nhdb->ecmp_cnt; loop++)
    {
        sys_greatbelt_nh_get_nhinfo_by_nhid(lchip, p_nhdb->nh_array[loop], (sys_nh_info_com_t**)&p_nh_info);
        sys_greatbelt_nh_delete_ecmp_member(lchip, p_nhdb, p_nh_info);
    }

    sys_greatbelt_nh_offset_free(lchip, SYS_NH_ENTRY_TYPE_FWD, member_num, p_nhdb->hdr.dsfwd_info.dsfwd_offset);

      mem_free(p_nhdb->nh_array);
    return ret;
}

int32
sys_greatbelt_nh_update_ecmp_cb(uint8 lchip, sys_nh_info_com_t* p_com_db, sys_nh_param_com_t* p_com_nh_param)
{
    sys_nh_param_ecmp_t* p_nh_param = NULL;
    ctc_nh_ecmp_nh_param_t* p_nh_ecmp_param = NULL;
    sys_nh_info_ecmp_t* p_nhdb = NULL;
    uint8 nh_array_size = 0, nh_array_size2 = 0;
    uint8 loop = 0, loop2 = 0;
    uint8 valid_cnt = 0;
    uint8 exist = 0;
    uint8  unrov_nh_cnt = 0;
    uint32 unrov_nh_array[SYS_GB_MAX_ECPN] = {0};
    uint32 valid_nh_array[SYS_GB_MAX_ECPN] = {0};
    sys_nh_info_com_t* p_nh_info = NULL;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    DRV_PTR_VALID_CHECK(p_com_db);
    DRV_PTR_VALID_CHECK(p_com_nh_param);

    p_nh_param = (sys_nh_param_ecmp_t*)(p_com_nh_param);
    p_nhdb = (sys_nh_info_ecmp_t*)(p_com_db);
    p_nhdb->hdr.nh_entry_type = SYS_NH_TYPE_ECMP;
    p_nh_ecmp_param = p_nh_param->p_ecmp_param;

    if (p_nh_ecmp_param->nh_num == 0)
    {
        return CTC_E_INVALID_PARAM;
    }

    sal_memcpy(&valid_nh_array[0], &p_nhdb->nh_array[0], p_nhdb->valid_cnt * sizeof(uint32));
    sal_memcpy(&unrov_nh_array[0], &p_nhdb->nh_array[p_nhdb->valid_cnt], (p_nhdb->ecmp_cnt - p_nhdb->valid_cnt) * sizeof(uint32));
    unrov_nh_cnt = (p_nhdb->ecmp_cnt - p_nhdb->valid_cnt);
    valid_cnt = p_nhdb->valid_cnt;

    if (p_nh_ecmp_param->upd_type == CTC_NH_ECMP_ADD_MEMBER)
    {
        sys_nh_info_dsnh_t nh_info;
        sal_memset(&nh_info, 0, sizeof(sys_nh_info_dsnh_t));

        for (loop = 0; loop < p_nh_ecmp_param->nh_num; loop++)
        {
           for (loop2 = 0; loop2 < p_nhdb->ecmp_cnt; loop2++)
            {
                if(p_nh_ecmp_param->nhid[loop] == p_nhdb->nh_array[loop2]
                     && (p_nhdb->is_dlb))
                {
                   /*only staic ecmp support repeated Member*/
                   return CTC_E_INVALID_CONFIG;;
                }
            }

            CTC_ERROR_RETURN(sys_greatbelt_nh_get_nhinfo_by_nhid(lchip, p_nh_ecmp_param->nhid[loop], (sys_nh_info_com_t**)&p_nh_info));

            if (p_nh_info->hdr.nh_entry_type != SYS_NH_TYPE_IPUC && p_nh_info->hdr.nh_entry_type != SYS_NH_TYPE_MPLS
                && p_nh_info->hdr.nh_entry_type != SYS_NH_TYPE_IP_TUNNEL && p_nh_info->hdr.nh_entry_type != SYS_NH_TYPE_MISC)
            {
                return CTC_E_NH_INVALID_NH_TYPE;
            }

            CTC_ERROR_RETURN(sys_greatbelt_nh_get_nhinfo(lchip, p_nh_ecmp_param->nhid[loop], &nh_info));
            if ((SYS_MAP_CTC_GPORT_TO_GCHIP(nh_info.gport) == 0x1f) && (p_nh_ecmp_param->type == CTC_NH_ECMP_TYPE_DLB))
            {
                SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, " [Nexthop]Ecmp Dlb member cannot support linkagg \n");
                return CTC_E_INVALID_CONFIG;
            }

            if ((!CTC_FLAG_ISSET(p_nh_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_UNROV))
                && (!CTC_FLAG_ISSET(p_nh_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD)))
            {

                     _sys_greatbelt_nh_ecmp_update_member_nh(lchip, p_nh_ecmp_param->nhid[loop],(sys_nh_info_com_t*)p_nh_info);
           #if 0
                    sys_nh_param_ipuc_t nh_param;

                    sal_memset(&nh_param, 0, sizeof(sys_nh_param_ipuc_t));
                    nh_param.hdr.nhid = p_nh_ecmp_param->nhid[loop];
                    nh_param.is_dsfwd = TRUE;
                    nh_param.is_dsl2edit = FALSE;    /*IPUC only use ROUTE_COMPACT in current GB SDK */
                    nh_param.hdr.nh_param_type = SYS_NH_TYPE_IPUC;
                    nh_param.hdr.change_type       = SYS_NH_CHANGE_TYPE_ADD_DYNTBL;

                    CTC_ERROR_RETURN(sys_greatbelt_nh_update_ipuc_cb(lchip, (sys_nh_info_com_t*)p_nh_info, (sys_nh_param_com_t*)(&nh_param)));
			#endif
            }

            if (!CTC_FLAG_ISSET(p_nh_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_UNROV))
            {
                valid_nh_array[valid_cnt++] = p_nh_ecmp_param->nhid[loop];
            }
            else
            {
                unrov_nh_array[unrov_nh_cnt++] = p_nh_ecmp_param->nhid[loop];
            }
        }
    }
    else if (p_nh_ecmp_param->upd_type == CTC_NH_ECMP_REMOVE_MEMBER)
    {
        for (loop = 0; loop < p_nh_ecmp_param->nh_num; loop++)
        {
            for (loop2 = 0; loop2 < p_nhdb->ecmp_cnt; loop2++)
            {
                if (p_nh_ecmp_param->nhid[loop] == p_nhdb->nh_array[loop2])
                {
                    exist = 1;
                    break;
                }
            }

            if (exist == 0)
            {
                return CTC_E_NH_ECMP_MEM_NOS_EXIST;
            }
            else
            {
                p_nhdb->nh_array[loop2]  = CTC_MAX_UINT32_VALUE;
            }
        }

        for (loop = 0; loop < p_nhdb->ecmp_cnt; loop++)
        {
            if (p_nhdb->nh_array[loop] != CTC_MAX_UINT32_VALUE)
            {
                continue;
            }

            if (loop < p_nhdb->valid_cnt)
            {
                valid_nh_array[loop] = valid_nh_array[valid_cnt - 1];
                valid_cnt--;
            }
            else
            {
                unrov_nh_array[loop - valid_cnt] = unrov_nh_array[unrov_nh_cnt - 1];
                unrov_nh_cnt--;
            }
        }
    }
    else
    {
        return CTC_E_INVALID_PARAM;
    }


    if ((valid_cnt + unrov_nh_cnt) > p_nhdb->mem_num)
    {
        return CTC_E_NH_EXCEED_MAX_ECMP_NUM;
    }

    /*old nh_array_size*/
    nh_array_size = (p_nhdb->ecmp_cnt + SYS_ECMP_ALLOC_MEM_STEP - 1) / SYS_ECMP_ALLOC_MEM_STEP;

    /*new nh_array_size*/
    nh_array_size2 = (valid_cnt + unrov_nh_cnt + SYS_ECMP_ALLOC_MEM_STEP - 1) / SYS_ECMP_ALLOC_MEM_STEP;
    nh_array_size2  = (nh_array_size2 == 0) ? 1 : nh_array_size2;

    if (nh_array_size != nh_array_size2)
    {
        mem_free(p_nhdb->nh_array);

        nh_array_size2 = nh_array_size2 * SYS_ECMP_ALLOC_MEM_STEP;
        p_nhdb->nh_array = mem_malloc(MEM_NEXTHOP_MODULE, nh_array_size2 * sizeof(uint32));

        if (!p_nhdb->nh_array)
        {
            return CTC_E_NO_MEMORY;
        }
    }

    p_nhdb->valid_cnt = valid_cnt;
    p_nhdb->ecmp_cnt = valid_cnt + unrov_nh_cnt;
    sal_memcpy(&p_nhdb->nh_array[0], &valid_nh_array[0], valid_cnt * sizeof(uint32));
    if (unrov_nh_cnt)
    {
        sal_memcpy(&p_nhdb->nh_array[valid_cnt], &unrov_nh_array[0], unrov_nh_cnt * sizeof(uint32));
    }

    sys_greatbelt_nh_update_ecmp_group(lchip, p_nhdb);

    if (p_nh_ecmp_param->upd_type == CTC_NH_ECMP_ADD_MEMBER)
    {
        for (loop = 0; loop < p_nh_ecmp_param->nh_num; loop++)
        {
            if (CTC_MAX_UINT32_VALUE != p_nh_ecmp_param->nhid[loop])
            {
                CTC_ERROR_RETURN(sys_greatbelt_nh_get_nhinfo_by_nhid(lchip, p_nh_ecmp_param->nhid[loop], (sys_nh_info_com_t**)&p_nh_info));
                sys_greatbelt_nh_add_ecmp_member(lchip, p_nhdb, p_nh_info);
            }
        }
    }
    else if (p_nh_ecmp_param->upd_type == CTC_NH_ECMP_REMOVE_MEMBER)
    {
        for (loop = 0; loop < p_nh_ecmp_param->nh_num; loop++)
        {
            if (CTC_MAX_UINT32_VALUE != p_nh_ecmp_param->nhid[loop])
            {
                CTC_ERROR_RETURN(sys_greatbelt_nh_get_nhinfo_by_nhid(lchip, p_nh_ecmp_param->nhid[loop], (sys_nh_info_com_t**)&p_nh_info));
                sys_greatbelt_nh_delete_ecmp_member(lchip, p_nhdb, p_nh_info);
            }
        }
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_delete_ecmp_cb(uint8 lchip, sys_nh_info_com_t* p_com_db, uint32* p_nhid)
{
    sys_nh_info_ecmp_t* p_nhdb = NULL;
    uint8 loop = 0;
    uint16 alloc_member_num = 0;
    sys_nh_info_com_t* p_nh_info = NULL;
    sys_nh_param_dsfwd_t dsfwd_param;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    DRV_PTR_VALID_CHECK(p_com_db);
    DRV_PTR_VALID_CHECK(p_nhid);

    p_nhdb = (sys_nh_info_ecmp_t*)(p_com_db);

    if (p_nhdb->is_dlb)
    {
        /*DLB mode*/
    }
    else
    {
        CTC_ERROR_RETURN(sys_greatbelt_nh_get_max_ecmp(lchip, &alloc_member_num));
        if (0 == alloc_member_num)
        {
            alloc_member_num = (p_nhdb->mem_num+SYS_ECMP_ALLOC_MEM_STEP-1)/SYS_ECMP_ALLOC_MEM_STEP*SYS_ECMP_ALLOC_MEM_STEP;
        }
        sys_greatbelt_nh_offset_free(lchip, SYS_NH_ENTRY_TYPE_FWD, alloc_member_num, p_nhdb->hdr.dsfwd_info.dsfwd_offset);

        for (loop = 0; loop < p_nhdb->mem_num; loop++)
        {
            /*2. Free DsFwd offset*/
            sal_memset(&dsfwd_param, 0, sizeof(sys_nh_param_dsfwd_t));

            dsfwd_param.drop_pkt  = TRUE;
            dsfwd_param.dsfwd_offset = p_nhdb->hdr.dsfwd_info.dsfwd_offset + loop;

            /*Write table*/
            CTC_ERROR_RETURN(sys_greatbelt_nh_write_entry_dsfwd(lchip, &dsfwd_param));
        }


        for (loop = 0; loop < p_nhdb->ecmp_cnt; loop++)
        {
            CTC_ERROR_RETURN(sys_greatbelt_nh_get_nhinfo_by_nhid(lchip, p_nhdb->nh_array[loop], (sys_nh_info_com_t**)&p_nh_info));
            sys_greatbelt_nh_delete_ecmp_member(lchip, p_nhdb, p_nh_info);
        }
    }

    mem_free(p_nhdb->nh_array);
    return CTC_E_NONE;
}


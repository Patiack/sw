/**
  @file sys_greatbelt_nexthop_api.c

  @date 2009-11-11

  @version v2.0

  The file contains all nexthop API function for upper layer module
 */
/****************************************************************************
 *
* Header Files
*
****************************************************************************/
#include "ctc_const.h"
#include "ctc_error.h"
#include "ctc_linklist.h"
#include "ctc_nexthop.h"
#include "sys_greatbelt_chip.h"
#include "sys_greatbelt_aps.h"
#include "sys_greatbelt_common.h"
#include "sys_greatbelt_nexthop_api.h"
#include "sys_greatbelt_nexthop.h"
#include "sys_greatbelt_queue_enq.h"
#include "sys_greatbelt_port.h"

#include "greatbelt/include/drv_lib.h"

/****************************************************************************
 *
* Defines and Macros
*
*****************************************************************************/
#define SYS_NH_INIT_CHECK(lchip)                    \
    do {                                            \
        SYS_LCHIP_CHECK_ACTIVE(lchip);                   \
        if (NULL == p_gb_nh_api_master[lchip])         \
        {                                           \
            return CTC_E_NOT_INIT;                  \
        }                                           \
    } while (0)


struct sys_hbnh_brguc_node_info_s
{
    uint32 nhid_brguc;
    uint32 dsfwd_offset_brguc;

    uint32 nhid_bypass;
    uint32 dsfwd_offset_bypass;

};
typedef struct sys_hbnh_brguc_node_info_s sys_hbnh_brguc_node_info_t;

struct sys_greatbelt_brguc_info_s
{
    ctc_vector_t* brguc_vector; /*Maintains ucast bridge DB(no egress vlan trans)*/
    sal_mutex_t*  brguc_mutex;
};
typedef struct sys_greatbelt_brguc_info_s sys_greatbelt_brguc_info_t;

struct sys_greatbelt_nh_api_master_s
{
    sys_greatbelt_brguc_info_t brguc_info;
    uint32 max_external_nhid;
};
typedef struct sys_greatbelt_nh_api_master_s sys_greatbelt_nh_api_master_t;

#define SYS_NEXTHOP_MAX_CHIP_NUM  31
#define SYS_NEXTHOP_PORT_NUM_PER_CHIP 512
#define GPORT_TO_INDEX(gport)  ((CTC_IS_LINKAGG_PORT(gport)) ? \
    (SYS_NEXTHOP_PORT_NUM_PER_CHIP * SYS_NEXTHOP_MAX_CHIP_NUM + (gport & 0xff)) : ((CTC_MAP_GPORT_TO_GCHIP(gport)) * SYS_NEXTHOP_PORT_NUM_PER_CHIP + CTC_MAP_GPORT_TO_LPORT(gport)))
#define SYS_MTU_MAX_SIZE     0x3FFF

/****************************************************************************
 *
* Global and Declaration
*
*****************************************************************************/
static sys_greatbelt_nh_api_master_t* p_gb_nh_api_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};

STATIC int32
_sys_greatbelt_misc_nh_create(uint8 lchip, uint32 nhid, sys_greatbelt_nh_type_t nh_param_type);

/****************************************************************************
 *
* Function
*
*****************************************************************************/
/**
 @brief This function is to initialize nexthop API data,
 */
int32
sys_greatbelt_nh_api_init(uint8 lchip, ctc_nh_global_cfg_t* nh_cfg)
{

    uint32   vec_block_num  = 0;
    int32    ret = 0;

    if (p_gb_nh_api_master[lchip])
    {
        return CTC_E_NONE;
    }

    CTC_ERROR_RETURN(sys_greatbelt_nh_init(lchip, nh_cfg));

    p_gb_nh_api_master[lchip] = (sys_greatbelt_nh_api_master_t*)mem_malloc(MEM_NEXTHOP_MODULE, (sizeof(sys_greatbelt_nh_api_master_t)));
    sal_memset(p_gb_nh_api_master[lchip], 0, (sizeof(sys_greatbelt_nh_api_master_t)));
    /*1. Bridge Ucast Data init*/
    if (NULL != p_gb_nh_api_master[lchip])
    {
        SYS_NH_CREAT_LOCK(p_gb_nh_api_master[lchip]->brguc_info.brguc_mutex);
    }
    else
    {
        mem_free(p_gb_nh_api_master[lchip]);
        p_gb_nh_api_master[lchip] = NULL;
        return CTC_E_NO_MEMORY;
    }

    vec_block_num  = (SYS_NEXTHOP_PORT_NUM_PER_CHIP * SYS_NEXTHOP_MAX_CHIP_NUM + 64) / 64;  /*31*128 (physical port ) + 64 (linkagg) */
    p_gb_nh_api_master[lchip]->brguc_info.brguc_vector = ctc_vector_init(vec_block_num, 64);
    if (NULL == p_gb_nh_api_master[lchip]->brguc_info.brguc_vector)
    {
        mem_free(p_gb_nh_api_master[lchip]);
        p_gb_nh_api_master[lchip] = NULL;
        return CTC_E_NO_MEMORY;
    }

    ret = sys_greatbelt_nh_get_max_external_nhid(lchip, &p_gb_nh_api_master[lchip]->max_external_nhid);

    if (ret)
    {
        mem_free(p_gb_nh_api_master[lchip]);
        p_gb_nh_api_master[lchip] = NULL;
        return ret;
    }


    /*2. Create default nexthop*/
    CTC_ERROR_RETURN(_sys_greatbelt_misc_nh_create(lchip, SYS_NH_RESOLVED_NHID_FOR_DROP, SYS_NH_TYPE_DROP));
    CTC_ERROR_RETURN(_sys_greatbelt_misc_nh_create(lchip, SYS_NH_RESOLVED_NHID_FOR_TOCPU, SYS_NH_TYPE_TOCPU));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_nh_api_free_node_data(void* node_data, void* user_data)
{
    mem_free(node_data);

    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_api_deinit(uint8 lchip)
{
    LCHIP_CHECK(lchip);
    if (NULL == p_gb_nh_api_master[lchip])
    {
        return CTC_E_NONE;
    }

    sys_greatbelt_nh_deinit(lchip);

    /*free bridge uc vector*/
    ctc_vector_traverse(p_gb_nh_api_master[lchip]->brguc_info.brguc_vector, (vector_traversal_fn)_sys_greatbelt_nh_api_free_node_data, NULL);
    ctc_vector_release(p_gb_nh_api_master[lchip]->brguc_info.brguc_vector);

    sal_mutex_destroy(p_gb_nh_api_master[lchip]->brguc_info.brguc_mutex);
    mem_free(p_gb_nh_api_master[lchip]);

    return CTC_E_NONE;
}

/**
 @brief This function is to create normal bridge nexthop in different types

 @param[in] gport, global port id

 @param[in] nh_type, nexthop type

 @return CTC_E_XXX
 */
int32
_sys_greatbelt_brguc_nh_create_by_type(uint8 lchip, uint16 gport, sys_nh_param_brguc_sub_type_t nh_type)
{
    sys_nh_param_brguc_t brguc_nh;
    sys_hbnh_brguc_node_info_t* p_brguc_node;
    bool is_add = FALSE;
    int32 ret = CTC_E_NONE;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "gport = %d , nh_type = %d\n", gport, nh_type);

    sal_memset(&brguc_nh, 0, sizeof(sys_nh_param_brguc_t));
    brguc_nh.hdr.nh_param_type = SYS_NH_TYPE_BRGUC;
    brguc_nh.hdr.nhid = SYS_NH_INVALID_NHID;
    brguc_nh.hdr.is_internal_nh = TRUE;
    brguc_nh.nh_sub_type = nh_type;
    brguc_nh.gport = gport;

    p_brguc_node = ctc_vector_get(p_gb_nh_api_master[lchip]->brguc_info.brguc_vector,  GPORT_TO_INDEX(gport));
    if (NULL == p_brguc_node)
    {
        p_brguc_node = (sys_hbnh_brguc_node_info_t*)mem_malloc(MEM_NEXTHOP_MODULE, sizeof(sys_hbnh_brguc_node_info_t));
        if (NULL == p_brguc_node)
        {
            return CTC_E_NO_MEMORY;
        }

        sal_memset(p_brguc_node, 0, sizeof(sys_hbnh_brguc_node_info_t));
        p_brguc_node->nhid_brguc  = SYS_NH_INVALID_NHID;
        p_brguc_node->nhid_bypass  = SYS_NH_INVALID_NHID;
        is_add = TRUE;
    }

    if ((nh_type == SYS_NH_PARAM_BRGUC_SUB_TYPE_BASIC)
        && (p_brguc_node->nhid_brguc != SYS_NH_INVALID_NHID))
    {
        ret = CTC_E_ENTRY_EXIST;
        goto error;
    }

    if ((nh_type == SYS_NH_PARAM_BRGUC_SUB_TYPE_BYPASS)
        && (p_brguc_node->nhid_bypass != SYS_NH_INVALID_NHID))
    {
        ret = CTC_E_ENTRY_EXIST;
        goto error;
    }

    ret = sys_greatbelt_nh_api_create(lchip, (sys_nh_param_com_t*)(&brguc_nh));
    if (ret < 0)
    {
        goto error;
    }
    if (nh_type == SYS_NH_PARAM_BRGUC_SUB_TYPE_BASIC)
    {
        p_brguc_node->nhid_brguc = brguc_nh.hdr.nhid;
        p_brguc_node->dsfwd_offset_brguc = brguc_nh.fwd_offset;

    }

    if (nh_type == SYS_NH_PARAM_BRGUC_SUB_TYPE_BYPASS)
    {
        p_brguc_node->nhid_bypass = brguc_nh.hdr.nhid;
        p_brguc_node->dsfwd_offset_bypass = brguc_nh.fwd_offset;
    }

    if (is_add)
    {
        if (FALSE == ctc_vector_add(p_gb_nh_api_master[lchip]->brguc_info.brguc_vector, GPORT_TO_INDEX(gport), p_brguc_node))
        {
            if (p_brguc_node->nhid_brguc != SYS_NH_INVALID_NHID)
            {
                sys_greatbelt_nh_api_delete(lchip, p_brguc_node->nhid_brguc, SYS_NH_TYPE_BRGUC);
            }

            if (p_brguc_node->nhid_bypass != SYS_NH_INVALID_NHID)
            {
                sys_greatbelt_nh_api_delete(lchip, p_brguc_node->nhid_bypass, SYS_NH_TYPE_BRGUC);
            }

            ret = CTC_E_NO_MEMORY;
        }
    }

error:
    if ((is_add)&&(CTC_E_NONE != ret))
    {
        mem_free(p_brguc_node);
    }

    return ret;
}

/**
 @brief This function is to create normal bridge nexthop

 @param[in] gport, global port id

 @param[in] nh_type, nexthop type

 @return CTC_E_XXX
 */
int32
sys_greatbelt_brguc_nh_create(uint8 lchip, uint16 gport, ctc_nh_param_brguc_sub_type_t nh_type)
{

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "gport = %d , nh_type = %d\n", gport, nh_type);

    SYS_NH_INIT_CHECK(lchip);
    CTC_ERROR_RETURN(sys_greatbelt_port_dest_gport_check(lchip, gport));

    if (nh_type == CTC_NH_PARAM_BRGUC_SUB_TYPE_UNTAGGED
        || nh_type == CTC_NH_PARAM_BRGUC_SUB_TYPE_RAW_PACKET_ELOG_CPU
        || nh_type == CTC_NH_PARAM_BRGUC_SUB_TYPE_SERVICE_QUEUE)
    {
        return CTC_E_NOT_SUPPORT;
    }

    if (((nh_type & CTC_NH_PARAM_BRGUC_SUB_TYPE_BASIC) != 0) || (nh_type == CTC_NH_PARAM_BRGUC_SUB_TYPE_ALL))
    {
        CTC_ERROR_RETURN(_sys_greatbelt_brguc_nh_create_by_type(lchip, gport, SYS_NH_PARAM_BRGUC_SUB_TYPE_BASIC));
    }

    if (((nh_type & CTC_NH_PARAM_BRGUC_SUB_TYPE_BYPASS) != 0) || (nh_type == CTC_NH_PARAM_BRGUC_SUB_TYPE_ALL))
    {
        CTC_ERROR_RETURN(_sys_greatbelt_brguc_nh_create_by_type(lchip, gport, SYS_NH_PARAM_BRGUC_SUB_TYPE_BYPASS));
    }

    return CTC_E_NONE;
}

/**
 @brief This function is to delete normal bridge nexthop

 @param[in] gport, global port id

 @param[in] nh_type, nexthop type

 @return CTC_E_XXX
 */
int32
sys_greatbelt_brguc_nh_delete(uint8 lchip, uint16 gport)
{

    sys_hbnh_brguc_node_info_t* p_brguc_node;
    int32 ret = 0;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "gport = %d\n", gport);

    SYS_NH_INIT_CHECK(lchip);
    CTC_ERROR_RETURN(sys_greatbelt_port_dest_gport_check(lchip, gport));

    SYS_NH_LOCK(p_gb_nh_api_master[lchip]->brguc_info.brguc_mutex);

    p_brguc_node = ctc_vector_get(p_gb_nh_api_master[lchip]->brguc_info.brguc_vector, GPORT_TO_INDEX(gport));
    if (NULL == p_brguc_node)
    {
        SYS_NH_UNLOCK(p_gb_nh_api_master[lchip]->brguc_info.brguc_mutex);
        return CTC_E_NH_NOT_EXIST;
    }

    if (p_brguc_node->nhid_brguc != SYS_NH_INVALID_NHID)
    {
        ret = (sys_greatbelt_nh_api_delete(lchip, p_brguc_node->nhid_brguc, SYS_NH_TYPE_BRGUC));
        p_brguc_node->nhid_brguc = SYS_NH_INVALID_NHID;
        p_brguc_node->dsfwd_offset_brguc = 0;

    }

    if (p_brguc_node->nhid_bypass != SYS_NH_INVALID_NHID)
    {
        ret = ret ? ret : (sys_greatbelt_nh_api_delete(lchip, p_brguc_node->nhid_bypass, SYS_NH_TYPE_BRGUC));
        p_brguc_node->nhid_bypass = SYS_NH_INVALID_NHID;
       p_brguc_node->dsfwd_offset_bypass = 0;

    }

    if ((p_brguc_node->nhid_bypass == SYS_NH_INVALID_NHID)
        && (p_brguc_node->nhid_brguc == SYS_NH_INVALID_NHID))
    {
        ctc_vector_del(p_gb_nh_api_master[lchip]->brguc_info.brguc_vector, GPORT_TO_INDEX(gport));
        mem_free(p_brguc_node);
    }

    SYS_NH_UNLOCK(p_gb_nh_api_master[lchip]->brguc_info.brguc_mutex);
    return ret;
}

int32
sys_greatbelt_brguc_get_dsfwd_offset(uint8 lchip, uint16 gport, ctc_nh_param_brguc_sub_type_t nh_type,
                                     uint32* p_offset)
{
    int32 ret = CTC_E_NONE;
    sys_hbnh_brguc_node_info_t* p_brguc_nhid_info = NULL;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "gport = %d\n", gport);

    SYS_NH_INIT_CHECK(lchip);
    CTC_ERROR_RETURN(sys_greatbelt_port_dest_gport_check(lchip, gport));

    SYS_NH_LOCK(p_gb_nh_api_master[lchip]->brguc_info.brguc_mutex);

    p_brguc_nhid_info = ctc_vector_get(p_gb_nh_api_master[lchip]->brguc_info.brguc_vector, GPORT_TO_INDEX(gport));
    if (NULL == p_brguc_nhid_info)
    {
        SYS_NH_UNLOCK(p_gb_nh_api_master[lchip]->brguc_info.brguc_mutex);
        return CTC_E_NH_NOT_EXIST;
    }

    switch (nh_type)
    {
    case CTC_NH_PARAM_BRGUC_SUB_TYPE_BASIC:
        if (p_brguc_nhid_info->nhid_brguc != SYS_NH_INVALID_NHID)
        {
           *p_offset =  p_brguc_nhid_info->dsfwd_offset_brguc;
        }
        else
        {
            ret  = CTC_E_NH_NOT_EXIST;
        }

        break;

    case CTC_NH_PARAM_BRGUC_SUB_TYPE_BYPASS:
        if (p_brguc_nhid_info->nhid_bypass != SYS_NH_INVALID_NHID)
        {
            *p_offset =   p_brguc_nhid_info->dsfwd_offset_bypass;
        }

        break;

    default:
        SYS_NH_UNLOCK(p_gb_nh_api_master[lchip]->brguc_info.brguc_mutex);
        return CTC_E_INVALID_PARAM;
    }

    SYS_NH_UNLOCK(p_gb_nh_api_master[lchip]->brguc_info.brguc_mutex);

    return ret;
}

int32
sys_greatbelt_brguc_update_speed(uint8 lchip, uint16 gport, uint8 speed)
{
    uint32 fwd_offset;


    CTC_ERROR_RETURN(sys_greatbelt_brguc_get_dsfwd_offset(lchip, gport,

                                                          CTC_NH_PARAM_BRGUC_SUB_TYPE_BASIC, &fwd_offset));

    CTC_ERROR_RETURN(sys_greatbelt_nh_update_dsfwd_speed(lchip, fwd_offset, speed));

    return CTC_E_NONE;
}

/**
 @brief This function is used to get dsFwd offset array

 @param[in] nhid, nexthop id

 @param[in] offset_array, dsfwd offset array

 @return CTC_E_XXX
 */
int32
sys_greatbelt_nh_get_dsfwd_offset(uint8 lchip, uint32 nhid, uint32* p_dsfwd_offset)
{
    sys_nh_info_com_t* p_nhinfo;

    /* get ds_fwd */

    SYS_NH_INIT_CHECK(lchip);
    CTC_ERROR_RETURN(sys_greatbelt_nh_get_nhinfo_by_nhid(lchip, nhid, (sys_nh_info_com_t**)&p_nhinfo));

    if (!CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD))
    {
    #if 0
        sys_nh_update_dsnh_param_t update_dsnh;
        sal_memset(&update_dsnh, 0, sizeof(sys_nh_update_dsnh_param_t));
        /* add DsFwd */
        update_dsnh.isAddDsFwd    = TRUE;
        update_dsnh.isAddDsL2Edit = TRUE;
        update_dsnh.data = NULL;
        update_dsnh.updateAd = NULL;
        CTC_ERROR_RETURN(sys_greatbelt_nh_update_nhInfo(lchip, nhid, &update_dsnh));
#endif

        sys_nh_param_com_t nh_param;
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "nhid = %d\n", nhid);

        sal_memset(&nh_param, 0, sizeof(sys_nh_param_com_t));
        nh_param.hdr.nhid = nhid;
        nh_param.hdr.nh_param_type = p_nhinfo->hdr.nh_entry_type;
        nh_param.hdr.change_type  = SYS_NH_CHANGE_TYPE_ADD_DYNTBL;
        CTC_ERROR_RETURN(sys_greatbelt_nh_api_update(lchip, nhid, (sys_nh_param_com_t*)(&nh_param)));

        CTC_ERROR_RETURN(sys_greatbelt_nh_get_nhinfo_by_nhid(lchip, nhid, (sys_nh_info_com_t**)&p_nhinfo));

    }


    if (!(p_nhinfo->hdr.nh_entry_flags & SYS_NH_INFO_FLAG_HAVE_DSFWD))
    {
        return CTC_E_INVALID_PARAM;
    }


    *p_dsfwd_offset = p_nhinfo->hdr.dsfwd_info.dsfwd_offset;


    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_free_dsfwd_offset(uint8 lchip, uint32 nhid)
{
    sys_nh_info_com_t* p_nhinfo;

    SYS_NH_INIT_CHECK(lchip);
    CTC_ERROR_RETURN(sys_greatbelt_nh_get_nhinfo_by_nhid(lchip, nhid, (sys_nh_info_com_t**)&p_nhinfo));

    if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD))
    {
        CTC_ERROR_RETURN(sys_greatbelt_nh_offset_free(lchip, SYS_NH_ENTRY_TYPE_FWD, 1, p_nhinfo->hdr.dsfwd_info.dsfwd_offset));
        CTC_UNSET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD);
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_set_service_queue_en(uint8 lchip, uint32 nhid, uint8 service_queue_en)
{
    sys_nh_info_com_t* p_nhinfo  = NULL;
    uint32 cmd_r  = 0;
    uint32 cmd_w  = 0;
    ds_fwd_t ds_fwd;
    ds_aps_bridge_t ds_aps_bridge;

    /* get ds_fwd */

    SYS_NH_INIT_CHECK(lchip);
    CTC_ERROR_RETURN(sys_greatbelt_nh_get_nhinfo_by_nhid(lchip, nhid, (sys_nh_info_com_t**)&p_nhinfo));

    if (!CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD))
    {
        return CTC_E_NH_INVALID_NHID;
    }

    if ((SYS_NH_TYPE_MPLS == p_nhinfo->hdr.nh_entry_type)
        && CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_APS_EN))
    {
        sys_nh_info_mpls_t* p_nh_mpls_info = (sys_nh_info_mpls_t*)p_nhinfo;
        sal_memset(&ds_aps_bridge, 0, sizeof(ds_aps_bridge_t));
        cmd_r = DRV_IOR(DsApsBridge_t, DRV_ENTRY_FLAG);
        cmd_w = DRV_IOW(DsApsBridge_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_nh_mpls_info->aps_group_id, cmd_r, &ds_aps_bridge));
        ds_aps_bridge.working_dest_map &= 0x3F1FFF;
        ds_aps_bridge.protecting_dest_map &= 0x3F1FFF;
        if (service_queue_en)
        {
            ds_aps_bridge.working_dest_map |= (SYS_QSEL_TYPE_SERVICE << 13); /*queueseltype:destmap[15:13]*/
            ds_aps_bridge.protecting_dest_map |= (SYS_QSEL_TYPE_SERVICE << 13); /*queueseltype:destmap[15:13]*/
        }
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_nh_mpls_info->aps_group_id, cmd_w, &ds_aps_bridge));
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "update for pw aps, aps group = %d\n", p_nh_mpls_info->aps_group_id);
    }
    else
    {
        cmd_r = DRV_IOR(DsFwd_t, DRV_ENTRY_FLAG);
        cmd_w = DRV_IOW(DsFwd_t, DRV_ENTRY_FLAG);
        sal_memset(&ds_fwd, 0, sizeof(ds_fwd_t));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_nhinfo->hdr.dsfwd_info.dsfwd_offset, cmd_r, &ds_fwd));
        ds_fwd.dest_map &= 0x3F1FFF;
        if (service_queue_en)
        {
            ds_fwd.dest_map |= (SYS_QSEL_TYPE_SERVICE << 13); /*queueseltype:destmap[15:13]*/
        }
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_nhinfo->hdr.dsfwd_info.dsfwd_offset, cmd_w, &ds_fwd));
    }

    return CTC_E_NONE;
}


int32
sys_greatbelt_nh_update_mcast_service_queue(uint8 lchip, uint16 mc_grp_id, uint32 member_nhid)
{
    sys_nh_info_mcast_t* p_mc_nhinfo  = NULL;
    sys_nh_info_com_t* p_nhinfo     = NULL;
    uint32 offset = 0;
    uint32 cmd = 0;
    uint32 dest_map = 0;

    bool b_found = FALSE;
    ds_met_entry_t  ds_met_entry;
    uint8 service_queue_en = 0;

    /*Set mcast member*/
    sys_nh_mcast_meminfo_t* p_meminfo = NULL;
    ctc_list_pointer_node_t* p_pos_mem = NULL;
    SYS_NH_INIT_CHECK(lchip);
    CTC_ERROR_RETURN(sys_greatbelt_nh_lkup_mcast_id(lchip, mc_grp_id, (sys_nh_info_mcast_t**) &p_mc_nhinfo));

    CTC_ERROR_RETURN(sys_greatbelt_nh_get_nhinfo_by_nhid(lchip, member_nhid, (sys_nh_info_com_t**)&p_nhinfo));

    if (!CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD))
    {
        return CTC_E_NH_INVALID_NHID;
    }

    if(SYS_NH_TYPE_MCAST != p_mc_nhinfo->hdr.nh_entry_type)
    {
        return CTC_E_NH_INVALID_DSNH_TYPE;
    }

    if(p_mc_nhinfo->basic_met_offset != mc_grp_id)
    {
        return CTC_E_NH_INVALID_NHID;
    }

    /*Get member service queue info*/
    cmd = DRV_IOR(DsFwd_t, DsFwd_DestMap_f);
    offset = p_nhinfo->hdr.dsfwd_info.dsfwd_offset;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, offset, cmd, &dest_map));
    if(dest_map&0xE000)
    {
        service_queue_en = 1;
    }



    b_found = FALSE;
    CTC_LIST_POINTER_LOOP(p_pos_mem, &(p_mc_nhinfo->p_mem_list))
    {
        p_meminfo = _ctc_container_of(p_pos_mem, sys_nh_mcast_meminfo_t, list_head);

        if (CTC_FLAG_ISSET(p_meminfo->dsmet.flag, SYS_NH_DSMET_FLAG_USE_PBM))
        {
            continue;
        }

        if (p_meminfo->dsmet.ref_nhid == member_nhid)
        {
            b_found = TRUE;
            break;
        }
    }

    if (TRUE == b_found)
    {/*update dsmet*/
        cmd = DRV_IOR(DsMetEntry_t, DRV_ENTRY_FLAG);
        sal_memset(&ds_met_entry, 0, sizeof(ds_met_entry_t));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_meminfo->dsmet.dsmet_offset, cmd, &ds_met_entry));
        if (service_queue_en)
        {
            ds_met_entry.ucast_id13    = 1;
            ds_met_entry.ucast_id15_14 = 1;
        }
        else
        {
            ds_met_entry.ucast_id13    = 0;
            ds_met_entry.ucast_id15_14 = 0;
        }
        cmd = DRV_IOW(DsMetEntry_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_meminfo->dsmet.dsmet_offset, cmd, &ds_met_entry));
    }



    if(FALSE == b_found)
    {
        return CTC_E_NH_INVALID_NHID;
    }

    return CTC_E_NONE;
}


int32
sys_greatbelt_mpls_nh_get_oam_info(uint8 lchip, uint32 nhid, uint32 b_protection, uint16* destid, uint8* next_aps_en, uint32* out_label)
{
    sys_nh_info_com_t* p_nh_com_info;
    sys_nh_info_mpls_t* p_nh_mpls_info;
    ds_l3_edit_mpls4_w_t  dsl3edit;
    uint32 cmd = 0;
    bool bFind = FALSE;
    uint8 gchip = 0;

    CTC_ERROR_RETURN(sys_greatbelt_nh_get_nhinfo_by_nhid(lchip, nhid, (sys_nh_info_com_t**)&p_nh_com_info));

    p_nh_mpls_info = (sys_nh_info_mpls_t*)p_nh_com_info;
    if (!p_nh_mpls_info)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    gchip = SYS_MAP_CTC_GPORT_TO_GCHIP(p_nh_mpls_info->gport);

    if (!CTC_FLAG_ISSET(p_nh_mpls_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_APS_EN)
        && CTC_FLAG_ISSET(p_nh_mpls_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_EGRESS_EDIT_MODE) )
    {
        if (SYS_GCHIP_IS_REMOTE(lchip, gchip))
        {
            return CTC_E_NONE;
        }
    }

    if (CTC_FLAG_ISSET(p_nh_mpls_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L3EDIT))
    { /*PW nexthop*/
        if (CTC_FLAG_ISSET(p_nh_mpls_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_APS_SHARE_DSNH))
        { /*Pw don't enable aps*/
            if (CTC_FLAG_ISSET(p_nh_mpls_info->working_path.p_mpls_tunnel->flag, SYS_NH_MPLS_TUNNEL_FLAG_APS))
            { /*tunnel label enable aps*/
                *destid = p_nh_mpls_info->working_path.p_mpls_tunnel->gport;
                *next_aps_en = 1;
            }
            else
            {
                *destid  = p_nh_mpls_info->working_path.p_mpls_tunnel->gport;
                *next_aps_en = 0;
            }

            cmd = DRV_IOR(DsL3EditMpls4W_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_nh_mpls_info->working_path.dsl3edit_offset, cmd, &dsl3edit));
            *out_label = dsl3edit.label0;
            bFind = TRUE;
        }
        else
        {
            cmd = DRV_IOR(DsL3EditMpls4W_t, DRV_ENTRY_FLAG);

            if (!b_protection)
            {/*work path*/
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_nh_mpls_info->working_path.dsl3edit_offset, cmd, &dsl3edit));

                if (CTC_FLAG_ISSET(p_nh_mpls_info->working_path.p_mpls_tunnel->flag, SYS_NH_MPLS_TUNNEL_FLAG_APS))
                { /*tunnel label enable aps*/
                    *destid = p_nh_mpls_info->working_path.p_mpls_tunnel->gport;
                    *next_aps_en = 1;
                }
                else
                {
                    *destid  = p_nh_mpls_info->working_path.p_mpls_tunnel->gport;
                    *next_aps_en = 0;
                }
                *out_label = dsl3edit.label0;
                bFind = TRUE;
                return CTC_E_NONE;
            }
            else
            {/*protection path*/
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_nh_mpls_info->protection_path->dsl3edit_offset, cmd, &dsl3edit));

                if (CTC_FLAG_ISSET(p_nh_mpls_info->protection_path->p_mpls_tunnel->flag, SYS_NH_MPLS_TUNNEL_FLAG_APS))
                { /*tunnel label enable aps*/
                    *destid = p_nh_mpls_info->protection_path->p_mpls_tunnel->gport;
                    *next_aps_en = 1;
                }
                else
                {
                    *destid  = p_nh_mpls_info->protection_path->p_mpls_tunnel->gport;
                    *next_aps_en = 0;
                }
                *out_label = dsl3edit.label0;
                bFind = TRUE;
            }

        }
    }
    else if (CTC_FLAG_ISSET(p_nh_mpls_info->working_path.p_mpls_tunnel->flag, SYS_NH_MPLS_TUNNEL_FLAG_APS))
    { /*Tunnel Nexthop, LSP Enabel APS*/
        ds_l2_edit_eth8_w_t  ds_l2edit;
        ds_l3_edit_mpls8_w_t  ds_l3edit_mpls_8w;
        ds_aps_bridge_t ds_aps_bridge;

        sal_memset(&ds_aps_bridge, 0, sizeof(ds_aps_bridge_t));
        cmd  = DRV_IOR(DsApsBridge_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_nh_mpls_info->aps_group_id, cmd, &ds_aps_bridge));

        if (p_nh_mpls_info->working_path.p_mpls_tunnel->p_dsl2edit && !b_protection)
        {
            *destid         = ds_aps_bridge.working_dest_map;
            *next_aps_en    = ds_aps_bridge.working_next_aps_bridge_en;
            *out_label      = 0;
            bFind = TRUE;
            return CTC_E_NONE;
        }
        else
        {
            cmd = DRV_IOR(DsL2EditEth8W_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, ds_aps_bridge.working_l2_edit_ptr, cmd, &ds_l2edit));

            sal_memcpy((uint8*)&ds_l3edit_mpls_8w + sizeof(ds_l3_edit_mpls4_w_t), (uint8*)&ds_l2edit + sizeof(ds_l3_edit_mpls4_w_t), sizeof(ds_l3_edit_mpls4_w_t));

            if (ds_l3edit_mpls_8w.label_valid2 && !b_protection)
            {   /*working path*/

                *destid         = ds_aps_bridge.working_dest_map;
                *next_aps_en    = ds_aps_bridge.working_next_aps_bridge_en;
                *out_label      = ds_l3edit_mpls_8w.label2;
                bFind = TRUE;
                return CTC_E_NONE;
            }
        }

        if (p_nh_mpls_info->working_path.p_mpls_tunnel->p_p_dsl2edit && b_protection)
        {   /*protection path*/
            *destid         = ds_aps_bridge.protecting_dest_map;
            *next_aps_en    = ds_aps_bridge.protecting_next_aps_bridge_en;
            *out_label      = 0;
            bFind = TRUE;
            return CTC_E_NONE;
        }
        else
        {
            cmd = DRV_IOR(DsL2EditEth8W_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, ds_aps_bridge.protecting_l2_edit_ptr, cmd, &ds_l2edit));

            sal_memcpy((uint8*)&ds_l3edit_mpls_8w + sizeof(ds_l3_edit_mpls4_w_t), (uint8*)&ds_l2edit + sizeof(ds_l3_edit_mpls4_w_t), sizeof(ds_l3_edit_mpls4_w_t));
            if (ds_l3edit_mpls_8w.label_valid2 && b_protection)
            {   /*protection path*/
                *destid         = ds_aps_bridge.protecting_dest_map;
                *next_aps_en    = ds_aps_bridge.protecting_next_aps_bridge_en;
                *out_label      = ds_l3edit_mpls_8w.label2;
                bFind = TRUE;
            }
    }
        }
    else
    {/*Tunnel Nexthop, LSP not Enabel APS, SPME Enable APS*/

    }


    if (!bFind)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_get_nhinfo(uint8 lchip, uint32 nhid, sys_nh_info_dsnh_t* p_nhinfo)
{

    sys_nh_info_com_t* p_nh_com_info;
    uint32 cmd = 0;


    SYS_NH_INIT_CHECK(lchip);
    CTC_ERROR_RETURN(sys_greatbelt_nh_get_nhinfo_by_nhid(lchip, nhid, (sys_nh_info_com_t**)&p_nh_com_info));

    p_nhinfo->nh_entry_type = p_nh_com_info->hdr.nh_entry_type;


    switch (p_nh_com_info->hdr.nh_entry_type)
    {
    case SYS_NH_TYPE_MCAST:
        {
            sys_nh_info_mcast_t * p_nh_mcast_info = (sys_nh_info_mcast_t*)p_nh_com_info;
            p_nhinfo->is_mcast = 1;
            p_nhinfo->dsfwd_valid  = CTC_FLAG_ISSET(p_nh_mcast_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD);
            p_nhinfo->dest_id  = 0xFFFF & (p_nh_mcast_info->basic_met_offset);
            p_nhinfo->nexthop_ext = 0;
            p_nhinfo->is_mcast   = TRUE;
            p_nhinfo->drop_pkt   = FALSE;
            p_nhinfo->ecmp_valid = FALSE;
            p_nhinfo->merge_dsfwd = 1;


        }
        break;
    case SYS_NH_TYPE_BRGUC:
        {
            sys_nh_info_brguc_t * p_nh_brguc_info = (sys_nh_info_brguc_t*)p_nh_com_info;

             /*p_nhinfo->dsfwd_valid = TRUE;*/
            p_nhinfo->dsfwd_valid  = CTC_FLAG_ISSET(p_nh_brguc_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD);

            if (CTC_FLAG_ISSET(p_nh_brguc_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_APS_EN))
            {
                p_nhinfo->aps_en = 1;
                p_nhinfo->dest_id = p_nh_brguc_info->dest.aps_bridge_group_id;
            }
            else
            {
                p_nhinfo->dest_chipid  = SYS_MAP_GPORT_TO_GCHIP(p_nh_brguc_info->dest.dest_gport);
                p_nhinfo->dest_id     = CTC_MAP_GPORT_TO_LPORT(p_nh_brguc_info->dest.dest_gport);
            }
            p_nhinfo->nexthop_ext = CTC_FLAG_ISSET(p_nh_brguc_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W);;
            p_nhinfo->drop_pkt   = FALSE;
            p_nhinfo->ecmp_valid = FALSE;

            p_nhinfo->dsnh_offset = p_nh_brguc_info->hdr.dsfwd_info.dsnh_offset;

            if (!p_nhinfo->dsfwd_valid)
            {
                p_nhinfo->merge_dsfwd  = 1; /*L2Uc Nexthop don't need update,so it can use merge DsFwd mode*/
            }
        }
        break;

    case SYS_NH_TYPE_MPLS:
        {
            sys_nh_info_mpls_t* p_nh_mpls_info = (sys_nh_info_mpls_t*)p_nh_com_info;
            p_nhinfo->dest_chipid  = SYS_MAP_GPORT_TO_GCHIP(p_nh_mpls_info->gport);
            p_nhinfo->dest_id     = CTC_MAP_GPORT_TO_LPORT(p_nh_mpls_info->gport);
            p_nhinfo->dsfwd_valid  = CTC_FLAG_ISSET(p_nh_mpls_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD);
            p_nhinfo->nexthop_ext = CTC_FLAG_ISSET(p_nh_mpls_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W);

            if (CTC_FLAG_ISSET(p_nh_mpls_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_APS_EN))
            {
                if(p_nhinfo->get_by_oam)
                {
                    sys_greatbelt_mpls_nh_get_oam_info(lchip, nhid, p_nhinfo->b_protection, &p_nhinfo->dest_id, &p_nhinfo->aps_en, &p_nhinfo->mpls_out_label);
                }
                else
                {
                    p_nhinfo->aps_en = 1;
                    p_nhinfo->dest_id = p_nh_mpls_info->aps_group_id;
                }
            }
            else
            {
                if(p_nhinfo->get_by_oam)
                {
                    ds_l3_edit_mpls4_w_t  dsl3edit;
                    if (CTC_FLAG_ISSET(p_nh_mpls_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L3EDIT))
                    {
                        cmd = DRV_IOR(DsL3EditMpls4W_t, DRV_ENTRY_FLAG);
                        CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_nh_mpls_info->working_path.dsl3edit_offset, cmd, &dsl3edit));
                        p_nhinfo->mpls_out_label = dsl3edit.label0;
                    }
                    else if(p_nh_mpls_info->working_path.p_mpls_tunnel)
                    {
                        if (p_nh_mpls_info->working_path.p_mpls_tunnel->p_dsl2edit)
                        {
                            p_nhinfo->mpls_out_label = 0;
                        }
                        else
                        {
                            ds_l2_edit_eth8_w_t  ds_l2edit;
                            ds_l3_edit_mpls8_w_t  ds_l3edit_mpls_8w;
                            cmd = DRV_IOR(DsL2EditEth8W_t, DRV_ENTRY_FLAG);
                            CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_nh_mpls_info->working_path.p_mpls_tunnel->dsl2edit_offset, cmd, &ds_l2edit));
                            sal_memcpy((uint8*)&ds_l3edit_mpls_8w + sizeof(ds_l3_edit_mpls4_w_t), (uint8*)&ds_l2edit + sizeof(ds_l3_edit_mpls4_w_t), sizeof(ds_l3_edit_mpls4_w_t));
                            p_nhinfo->mpls_out_label = ds_l3edit_mpls_8w.label2;
                        }
                    }
                }
                p_nhinfo->aps_en = 0;
            }

            p_nhinfo->is_mcast   = 0;
            p_nhinfo->drop_pkt   = CTC_FLAG_ISSET(p_nh_mpls_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_UNROV);
            p_nhinfo->ecmp_valid = FALSE;

            if (p_nhinfo->b_protection)
            {
                p_nhinfo->dsnh_offset = p_nh_mpls_info->hdr.dsfwd_info.dsnh_offset + 1*(1 + p_nhinfo->nexthop_ext);
            }
            else
            {
                p_nhinfo->dsnh_offset = p_nh_mpls_info->hdr.dsfwd_info.dsnh_offset;
            }

        }
        break;

    case SYS_NH_TYPE_ECMP:
        {
            sys_nh_info_ecmp_t* p_nh_ecmp_info = (sys_nh_info_ecmp_t*)p_nh_com_info;

            p_nhinfo->ecmp_valid = TRUE;
            p_nhinfo->dsfwd_valid = TRUE;
            p_nhinfo->ecmp_num = p_nh_ecmp_info->mem_num;
            p_nhinfo->ecmp_cnt = p_nh_ecmp_info->ecmp_cnt;
            p_nhinfo->valid_cnt = p_nh_ecmp_info->valid_cnt;
            sal_memcpy(&p_nhinfo->nh_array[0], &p_nh_ecmp_info->nh_array[0], p_nh_ecmp_info->ecmp_cnt * sizeof(uint32));

        }
        break;

    case SYS_NH_TYPE_IPUC:
        {
            sys_nh_info_ipuc_t* p_nh_ipuc_info = (sys_nh_info_ipuc_t*)p_nh_com_info;

            p_nhinfo->dest_chipid  = SYS_MAP_GPORT_TO_GCHIP(p_nh_ipuc_info->gport);
            p_nhinfo->dest_id     = CTC_MAP_GPORT_TO_LPORT(p_nh_ipuc_info->gport);
            p_nhinfo->gport = p_nh_ipuc_info->gport;

            p_nhinfo->dsfwd_valid  = CTC_FLAG_ISSET(p_nh_ipuc_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD);
            p_nhinfo->nexthop_ext = CTC_FLAG_ISSET(p_nh_ipuc_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W);
            p_nhinfo->is_mcast   = 0;
            p_nhinfo->drop_pkt   = CTC_FLAG_ISSET(p_nh_ipuc_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_UNROV);
            p_nhinfo->ecmp_valid = FALSE;
            p_nhinfo->aps_en = CTC_FLAG_ISSET(p_nh_ipuc_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_APS_EN);
            p_nhinfo->stats_en = CTC_FLAG_ISSET(p_nh_ipuc_info->flag,SYS_NH_IPUC_FLAG_STATS_EN);

            p_nhinfo->dsnh_offset = p_nh_ipuc_info->hdr.dsfwd_info.dsnh_offset;
            p_nhinfo->merge_dsfwd = CTC_FLAG_ISSET(p_nh_ipuc_info->flag,SYS_NH_IPUC_FLAG_MERGE_DSFWD)?1:0;

        }
        break;

    case SYS_NH_TYPE_IP_TUNNEL:
        {
            sys_nh_info_ip_tunnel_t* p_nh_ip_tunnel_info = (sys_nh_info_ip_tunnel_t*)p_nh_com_info;

            p_nhinfo->dest_chipid   = SYS_MAP_GPORT_TO_GCHIP(p_nh_ip_tunnel_info->gport);
            p_nhinfo->dest_id       = CTC_MAP_GPORT_TO_LPORT(p_nh_ip_tunnel_info->gport);

            p_nhinfo->dsfwd_valid   = CTC_FLAG_ISSET(p_nh_ip_tunnel_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD);
            p_nhinfo->nexthop_ext   = CTC_FLAG_ISSET(p_nh_ip_tunnel_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W);
            p_nhinfo->is_mcast      = 0;
            p_nhinfo->drop_pkt      = CTC_FLAG_ISSET(p_nh_ip_tunnel_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_UNROV);
            p_nhinfo->ecmp_valid    = FALSE;
            p_nhinfo->re_route = CTC_FLAG_ISSET(p_nh_ip_tunnel_info->flag, SYS_NH_IP_TUNNEL_REROUTE);
            p_nhinfo->aps_en = CTC_FLAG_ISSET(p_nh_ip_tunnel_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_APS_EN);
            p_nhinfo->is_ivi = CTC_FLAG_ISSET(p_nh_ip_tunnel_info->flag, SYS_NH_IP_TUNNEL_FLAG_IVI_6TO4) +
                CTC_FLAG_ISSET(p_nh_ip_tunnel_info->flag, SYS_NH_IP_TUNNEL_FLAG_IVI_4TO6);

            p_nhinfo->is_nat = CTC_FLAG_ISSET(p_nh_ip_tunnel_info->flag, SYS_NH_IP_TUNNEL_FLAG_NAT_V4);

            p_nhinfo->dsnh_offset = p_nh_ip_tunnel_info->hdr.dsfwd_info.dsnh_offset;

        }
        break;

    case SYS_NH_TYPE_DROP:
        {
            p_nhinfo->dsfwd_valid = TRUE;
            p_nhinfo->drop_pkt = TRUE;
        }
        break;

    case SYS_NH_TYPE_TOCPU:
        {
            sys_nh_info_ipuc_t* p_nh_ipuc_info = (sys_nh_info_ipuc_t*)p_nh_com_info;

            p_nhinfo->dsfwd_valid = TRUE;
            p_nhinfo->dsnh_offset = p_nh_ipuc_info->hdr.dsfwd_info.dsnh_offset;

        }
        break;

    case SYS_NH_TYPE_MISC:
        {
            sys_nh_info_misc_t* p_nh_misc_info = (sys_nh_info_misc_t*)p_nh_com_info;
            p_nhinfo->dest_chipid   = SYS_MAP_GPORT_TO_GCHIP(p_nh_misc_info->gport);
            p_nhinfo->dest_id       = CTC_MAP_GPORT_TO_LPORT(p_nh_misc_info->gport);

            p_nhinfo->dsfwd_valid   = CTC_FLAG_ISSET(p_nh_misc_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD);
            p_nhinfo->nexthop_ext   = CTC_FLAG_ISSET(p_nh_misc_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W);
            p_nhinfo->is_mcast      = 0;
            p_nhinfo->drop_pkt      = CTC_FLAG_ISSET(p_nh_misc_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_UNROV);

            p_nhinfo->dsnh_offset = p_nh_misc_info->hdr.dsfwd_info.dsnh_offset;
            p_nhinfo->stats_en = p_nh_misc_info->stats_valid;

        }
        break;

    case SYS_NH_TYPE_ILOOP:
        {
            sys_nh_info_special_t* p_nh_iloop_info = (sys_nh_info_special_t *)p_nh_com_info;


            p_nhinfo->dsnh_offset = p_nh_iloop_info->hdr.dsfwd_info.dsnh_offset;

            p_nhinfo->dest_chipid   = SYS_MAP_GPORT_TO_GCHIP(p_nh_iloop_info->dest_gport);
            p_nhinfo->dest_id       = CTC_MAP_GPORT_TO_LPORT(p_nh_iloop_info->dest_gport);

            p_nhinfo->dsfwd_valid   = CTC_FLAG_ISSET(p_nh_iloop_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD);
            p_nhinfo->nexthop_ext   = CTC_FLAG_ISSET(p_nh_iloop_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W);
            p_nhinfo->aps_en = CTC_FLAG_ISSET(p_nh_iloop_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_APS_EN);
        }
        break;


    case SYS_NH_TYPE_RSPAN:
    default:
        return CTC_E_INVALID_PARAM;
    }
	if(CTC_FLAG_ISSET(p_nh_com_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_EGRESS_EDIT_MODE))
	{
	   p_nhinfo->merge_dsfwd  = 2; /* must need dsfwd*/
	}
    return CTC_E_NONE;

}

int32
sys_greatbelt_l2_get_ucast_nh(uint8 lchip, uint16 gport, ctc_nh_param_brguc_sub_type_t nh_type, uint32* nhid)
{
    int32 ret = CTC_E_NONE;
    sys_hbnh_brguc_node_info_t* p_brguc_nhid_info = NULL;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "gport = %d\n", gport);

    SYS_NH_INIT_CHECK(lchip);
    CTC_ERROR_RETURN(sys_greatbelt_port_dest_gport_check(lchip, gport));
    if (nh_type == CTC_NH_PARAM_BRGUC_SUB_TYPE_UNTAGGED
        || nh_type == CTC_NH_PARAM_BRGUC_SUB_TYPE_RAW_PACKET_ELOG_CPU
        || nh_type == CTC_NH_PARAM_BRGUC_SUB_TYPE_SERVICE_QUEUE)
    {
        return CTC_E_NOT_SUPPORT;
    }


    SYS_NH_LOCK(p_gb_nh_api_master[lchip]->brguc_info.brguc_mutex);

    p_brguc_nhid_info = ctc_vector_get(p_gb_nh_api_master[lchip]->brguc_info.brguc_vector, GPORT_TO_INDEX(gport));
    if (NULL == p_brguc_nhid_info)
    {
        SYS_NH_UNLOCK(p_gb_nh_api_master[lchip]->brguc_info.brguc_mutex);
        return CTC_E_NH_NOT_EXIST;
    }

    switch (nh_type)
    {
    case CTC_NH_PARAM_BRGUC_SUB_TYPE_BASIC:
        if (p_brguc_nhid_info->nhid_brguc != SYS_NH_INVALID_NHID)
        {
            *nhid = p_brguc_nhid_info->nhid_brguc;
        }
        else
        {
            ret  = CTC_E_NH_NOT_EXIST;
        }

        break;

    case CTC_NH_PARAM_BRGUC_SUB_TYPE_BYPASS:
        if (p_brguc_nhid_info->nhid_bypass != SYS_NH_INVALID_NHID)
        {
            *nhid = p_brguc_nhid_info->nhid_bypass;
        }
        else
        {
            ret  = CTC_E_NH_NOT_EXIST;
        }

        break;

    default:
        SYS_NH_UNLOCK(p_gb_nh_api_master[lchip]->brguc_info.brguc_mutex);
        return CTC_E_INVALID_PARAM;
    }

    SYS_NH_UNLOCK(p_gb_nh_api_master[lchip]->brguc_info.brguc_mutex);
    return ret;
}

int32
sys_greatbelt_brguc_get_nhid(uint8 lchip, uint16 gport, sys_nh_brguc_nhid_info_t* p_brguc_nhid_info)
{
    sys_hbnh_brguc_node_info_t* p_brguc_node_info = NULL;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "gport = %d\n", gport);

    SYS_NH_INIT_CHECK(lchip);
    CTC_ERROR_RETURN(sys_greatbelt_port_dest_gport_check(lchip, gport));

    p_brguc_node_info = ctc_vector_get(p_gb_nh_api_master[lchip]->brguc_info.brguc_vector, GPORT_TO_INDEX(gport));
    if (NULL == p_brguc_node_info)
    {

        return CTC_E_NH_NOT_EXIST;
    }

    p_brguc_nhid_info->brguc_nhid = p_brguc_node_info->nhid_brguc;
    p_brguc_nhid_info->brguc_untagged_nhid  = p_brguc_node_info->nhid_brguc;
    p_brguc_nhid_info->bypass_nhid   = p_brguc_node_info->nhid_bypass;
    p_brguc_nhid_info->ram_pkt_nhid  = p_brguc_node_info->nhid_brguc;
    p_brguc_nhid_info->srv_queue_nhid  = p_brguc_node_info->nhid_brguc;

    return CTC_E_NONE;

}

STATIC int32
_sys_greatbelt_mpls_nh_set_drop(uint8 lchip, sys_nh_info_com_t* p_nh_com_info, uint8 b_protection, uint8 drop)
{
    sys_nh_info_mpls_t* p_nh_mpls_info;
    ds_l3_edit_mpls4_w_t  dsl3edit;
    uint32 cmd = 0;
    uint32 l3_offset = 0;
    uint32 l2_offset = 0;
    uint8  gchip = 0;

    p_nh_mpls_info = (sys_nh_info_mpls_t*)p_nh_com_info;

    gchip = CTC_MAP_GPORT_TO_GCHIP(p_nh_mpls_info->gport);
    if (CTC_FLAG_ISSET(p_nh_mpls_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_EGRESS_EDIT_MODE)
        && SYS_GCHIP_IS_REMOTE(lchip, gchip))
    {
        return CTC_E_NONE;
    }


    if (CTC_FLAG_ISSET(p_nh_mpls_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L3EDIT))
    { /*PW nexthop*/

        cmd = DRV_IOR(DsL3EditMpls4W_t, DRV_ENTRY_FLAG);
        if (CTC_FLAG_ISSET(p_nh_mpls_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_APS_SHARE_DSNH))
        { /*Pw don't enable aps*/
            l3_offset = p_nh_mpls_info->working_path.dsl3edit_offset;
        }
        else
        {
            if (!b_protection)
            {/*work path*/
                l3_offset = p_nh_mpls_info->working_path.dsl3edit_offset;
            }
            else
            {/*protection path*/
                l3_offset = p_nh_mpls_info->protection_path->dsl3edit_offset;
            }
        }
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, l3_offset, cmd, &dsl3edit));
        dsl3edit.ds_type = drop ? SYS_NH_DS_TYPE_DISCARD : SYS_NH_DS_TYPE_L3EDIT;

        cmd = DRV_IOW(DsL3EditMpls4W_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, l3_offset, cmd, &dsl3edit));

    }
    else
    {

        /*Tunnel Nexthop, LSP Enabel APS*/
        ds_l2_edit_eth8_w_t  ds_l2edit;
        ds_aps_bridge_t ds_aps_bridge;

        if (CTC_FLAG_ISSET(p_nh_mpls_info->working_path.p_mpls_tunnel->flag, SYS_NH_MPLS_TUNNEL_FLAG_APS))
        {
            sal_memset(&ds_aps_bridge, 0, sizeof(ds_aps_bridge_t));
            cmd  = DRV_IOR(DsApsBridge_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_nh_mpls_info->aps_group_id, cmd, &ds_aps_bridge));

            if (!b_protection)
            {   /*working path*/
                l2_offset = ds_aps_bridge.working_l2_edit_ptr;
            }
            else
            {/*protection path*/
                l2_offset = ds_aps_bridge.protecting_l2_edit_ptr;
            }
        }
        else
        {
            l2_offset = p_nh_mpls_info->working_path.p_mpls_tunnel->dsl2edit_offset;
        }
        cmd = DRV_IOR(DsL2EditEth8W_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, l2_offset, cmd, &ds_l2edit));

        ds_l2edit.ds_type = drop ? SYS_NH_DS_TYPE_DISCARD : SYS_NH_DS_TYPE_L2EDIT;

        cmd = DRV_IOW(DsL2EditEth8W_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, l2_offset, cmd, &ds_l2edit));

    }


    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_set_nh_drop(uint8 lchip, uint32 nhid, ctc_nh_drop_info_t *p_nh_drop)
{
    sys_nh_info_com_t* p_nh_com_info = NULL;
    uint8 nh_entry_type = SYS_NH_TYPE_NULL;
    uint8 b_protection  = 0;
    uint8 drop_en       = 0;
    uint32 cmd          = 0;
    uint32 offset       = 0;
    ds_fwd_t ds_fwd;

    SYS_NH_INIT_CHECK(lchip);
    CTC_ERROR_RETURN(sys_greatbelt_nh_get_nhinfo_by_nhid(lchip, nhid, (sys_nh_info_com_t**)&p_nh_com_info));
    if (!p_nh_com_info)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }
    b_protection    = p_nh_drop->is_protection_path;
    drop_en         = p_nh_drop->drop_en;
    nh_entry_type = p_nh_com_info->hdr.nh_entry_type;

    switch(nh_entry_type)
    {
    case SYS_NH_TYPE_MPLS:
        CTC_ERROR_RETURN(_sys_greatbelt_mpls_nh_set_drop(lchip, p_nh_com_info, b_protection, drop_en));
        break;

    case SYS_NH_TYPE_BRGUC:
    case SYS_NH_TYPE_IPUC:
    case SYS_NH_TYPE_ECMP:
    case SYS_NH_TYPE_IP_TUNNEL:
    case SYS_NH_TYPE_MCAST:
    case SYS_NH_TYPE_DROP:
    case SYS_NH_TYPE_TOCPU:
    case SYS_NH_TYPE_UNROV:
    case SYS_NH_TYPE_ILOOP:
    case SYS_NH_TYPE_ELOOP:
    case SYS_NH_TYPE_RSPAN:
    case SYS_NH_TYPE_MISC:
        if(CTC_FLAG_ISSET(p_nh_com_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD))
        {
            cmd = DRV_IOR(DsFwd_t, DRV_ENTRY_FLAG);
            offset = p_nh_com_info->hdr.dsfwd_info.dsfwd_offset;
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, offset, cmd, &ds_fwd));
            ds_fwd.ds_type = drop_en ? SYS_NH_DS_TYPE_DISCARD : SYS_NH_DS_TYPE_FWD;
            cmd = DRV_IOW(DsFwd_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, offset, cmd, &ds_fwd));
        }
        break;
    }

    return CTC_E_NONE;
}

/**
 @brief This function is to create egress vlan edit nexthop

 @param[in] nhid, nexthop id of this nexthop

 @param[in] gport, global port id

 @param[in] p_vlan_info, vlan edit information

 @param[in] dsnh_offset, nexthop offset of this nexthop

 @return CTC_E_XXX
 */
int32
sys_greatbelt_egress_vlan_edit_nh_create(uint8 lchip, uint32 nhid, uint16 gport,
                                         ctc_vlan_egress_edit_info_t* p_vlan_info, uint32 dsnh_offset,
                                         ctc_vlan_edit_nh_param_t* p_nh_param)
{
    sys_nh_param_brguc_t nh_brg;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "nhid = %d, gport = %d, dsnh_offset = %d",
                   nhid, gport, dsnh_offset);

    SYS_NH_INIT_CHECK(lchip);
    CTC_ERROR_RETURN(sys_greatbelt_port_dest_gport_check(lchip, gport));
    CTC_PTR_VALID_CHECK(p_vlan_info);
    if (CTC_FLAG_ISSET(p_vlan_info->edit_flag, CTC_VLAN_EGRESS_EDIT_OUPUT_CVID_VALID))
    {
        CTC_VLAN_RANGE_CHECK(p_vlan_info->output_cvid);
    }

    if (CTC_FLAG_ISSET(p_vlan_info->edit_flag, CTC_VLAN_EGRESS_EDIT_OUPUT_SVID_VALID))
    {
        CTC_VLAN_RANGE_CHECK(p_vlan_info->output_svid);
    }

    if (CTC_FLAG_ISSET(p_vlan_info->flag, CTC_VLAN_NH_MTU_CHECK_EN))
    {
        CTC_MAX_VALUE_CHECK(p_vlan_info->mtu_size, SYS_MTU_MAX_SIZE);
    }

    if(p_nh_param->logic_port_valid)
    {
        CTC_LOGIC_PORT_RANGE_CHECK(p_nh_param->logic_port)
    }

    CTC_MAX_VALUE_CHECK(p_vlan_info->user_vlanptr,0x1fff);

    sal_memset(&nh_brg, 0, sizeof(sys_nh_param_brguc_t));
    nh_brg.hdr.have_dsfwd  = TRUE;
    nh_brg.hdr.nh_param_type = SYS_NH_TYPE_BRGUC;
    nh_brg.hdr.is_internal_nh = FALSE;
    nh_brg.hdr.nhid = nhid;
    nh_brg.nh_sub_type = SYS_NH_PARAM_BRGUC_SUB_TYPE_VLAN_EDIT;
    nh_brg.gport = gport;
    nh_brg.p_vlan_edit_info = p_vlan_info;
    nh_brg.dsnh_offset = dsnh_offset;
    nh_brg.loop_nhid = p_vlan_info->loop_nhid;

    nh_brg.p_vlan_edit_nh_param = p_nh_param;
    CTC_ERROR_RETURN(sys_greatbelt_nh_api_create(lchip, (sys_nh_param_com_t*)(&nh_brg)));

    return CTC_E_NONE;
}



/**
 @brief This function is to delete egress vlan edit nexthop

 @param[in] nhid, nexthop id of this nexthop

 @return CTC_E_XXX
 */
int32
sys_greatbelt_egress_vlan_edit_nh_delete(uint8 lchip, uint32 nhid)
{
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "nhid = %d\n", nhid);

    SYS_NH_INIT_CHECK(lchip);
    CTC_ERROR_RETURN(sys_greatbelt_nh_api_delete(lchip, nhid, SYS_NH_TYPE_BRGUC));
    return CTC_E_NONE;
}

/**
 @brief This function is to create aps egress vlan edit nexthop

 @param[in] nhid, nexthop id of this nexthop

 @param[in] gport, global port id

 @param[in] p_vlan_info, vlan edit information

 @param[in] dsnh_offset, nexthop offset of this nexthop

 @return CTC_E_XXX
 */
int32
sys_greatbelt_aps_egress_vlan_edit_nh_create(uint8 lchip, uint32 nhid,
                                             uint32 dsnh_offset, uint16 aps_bridge_id,
                                             ctc_vlan_egress_edit_info_t* p_vlan_info_working_path,
                                             ctc_vlan_egress_edit_info_t* p_vlan_info_protection_path)
{
    sys_nh_param_brguc_t nh_brg;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "nhid = %d, apsBridgeId = %d, dsnh_offset = %d",
                   nhid, aps_bridge_id, dsnh_offset);

    /*CTC_GLOBAL_PORT_CHECK(gport); Check APS bridgeId ?*/
    SYS_NH_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_vlan_info_working_path);
    CTC_PTR_VALID_CHECK(p_vlan_info_protection_path);
    if (CTC_FLAG_ISSET(p_vlan_info_working_path->edit_flag, CTC_VLAN_EGRESS_EDIT_OUPUT_CVID_VALID))
    {
        CTC_VLAN_RANGE_CHECK(p_vlan_info_working_path->output_cvid);
    }

    if (CTC_FLAG_ISSET(p_vlan_info_working_path->edit_flag, CTC_VLAN_EGRESS_EDIT_OUPUT_SVID_VALID))
    {
        CTC_VLAN_RANGE_CHECK(p_vlan_info_working_path->output_svid);
    }

    if (CTC_FLAG_ISSET(p_vlan_info_protection_path->edit_flag, CTC_VLAN_EGRESS_EDIT_OUPUT_CVID_VALID))
    {
        CTC_VLAN_RANGE_CHECK(p_vlan_info_protection_path->output_cvid);
    }

    if (CTC_FLAG_ISSET(p_vlan_info_protection_path->edit_flag, CTC_VLAN_EGRESS_EDIT_OUPUT_SVID_VALID))
    {
        CTC_VLAN_RANGE_CHECK(p_vlan_info_protection_path->output_svid);
    }

    sal_memset(&nh_brg, 0, sizeof(sys_nh_param_brguc_t));
    nh_brg.hdr.have_dsfwd  = TRUE;
    nh_brg.hdr.nh_param_type = SYS_NH_TYPE_BRGUC;
    nh_brg.hdr.is_internal_nh = FALSE;
    nh_brg.hdr.nhid = nhid;
    nh_brg.nh_sub_type = SYS_NH_PARAM_BRGUC_SUB_TYPE_APS_VLAN_EDIT;
    nh_brg.gport = aps_bridge_id;
    nh_brg.p_vlan_edit_info = p_vlan_info_working_path;
    nh_brg.p_vlan_edit_info_prot_path = p_vlan_info_protection_path;
    nh_brg.dsnh_offset = dsnh_offset;

    CTC_ERROR_RETURN(sys_greatbelt_nh_api_create(lchip, (sys_nh_param_com_t*)(&nh_brg)));

    return CTC_E_NONE;
}

/**
 @brief This function is to delete egress vlan edit nexthop

 @param[in] nhid, nexthop id of this nexthop

 @return CTC_E_XXX
 */
int32
sys_greatbelt_aps_egress_vlan_edit_nh_delete(uint8 lchip, uint32 nhid)
{
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "nhid = %d\n", nhid);

    SYS_NH_INIT_CHECK(lchip);
    CTC_ERROR_RETURN(sys_greatbelt_nh_api_delete(lchip, nhid, SYS_NH_TYPE_BRGUC));

    return CTC_E_NONE;
}

/**
 @brief This function is to create ipuc nexthop

 @param[in] nhid, nexthop ID

 @param[in] p_member_list, member list to be added to this nexthop

 @return CTC_E_XXX
 */
int32
sys_greatbelt_ipuc_nh_create(uint8 lchip, uint32 nhid, ctc_ip_nh_param_t* p_nh_param)
{
    sys_nh_param_ipuc_t nh_param;


    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    SYS_NH_INIT_CHECK(lchip);

    if(!CTC_FLAG_ISSET(p_nh_param->flag, CTC_IP_NH_FLAG_UNROV))
    {
        CTC_PTR_VALID_CHECK(&(p_nh_param->oif));
        CTC_ERROR_RETURN(sys_greatbelt_port_dest_gport_check(lchip, p_nh_param->oif.gport));
        CTC_MAX_VALUE_CHECK(p_nh_param->oif.vid, CTC_MAX_VLAN_ID);
    }

    if(p_nh_param->aps_en)
    {
        bool protect_en = FALSE;
        CTC_PTR_VALID_CHECK(&(p_nh_param->p_oif));
        CTC_ERROR_RETURN(sys_greatbelt_port_dest_gport_check(lchip, p_nh_param->oif.gport));
        CTC_MAX_VALUE_CHECK(p_nh_param->p_oif.vid, CTC_MAX_VLAN_ID);
        CTC_ERROR_RETURN(sys_greatbelt_aps_get_aps_bridge(lchip, p_nh_param->aps_bridge_group_id, &protect_en));
    }

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "nhid = %d, dsnhoffset = %d, gport = %d, vid = %d, mac = %02x:%02x:%02x:%02x:%02x:%02x\n",
                   nhid, p_nh_param->dsnh_offset, p_nh_param->oif.gport, p_nh_param->oif.vid, p_nh_param->mac[0], p_nh_param->mac[1],
                   p_nh_param->mac[2], p_nh_param->mac[3], p_nh_param->mac[4], p_nh_param->mac[5]);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "protection_gport = %d, protection_vid = %d, protection_mac = %02x:%02x:%02x:%02x:%02x:%02x\n",
                   p_nh_param->p_oif.gport, p_nh_param->p_oif.vid, p_nh_param->p_mac[0], p_nh_param->p_mac[1],
                   p_nh_param->p_mac[2], p_nh_param->p_mac[3], p_nh_param->p_mac[4], p_nh_param->p_mac[5]);

    sal_memset(&nh_param, 0, sizeof(sys_nh_param_ipuc_t));

    nh_param.hdr.nh_param_type = SYS_NH_TYPE_IPUC;
    nh_param.hdr.is_internal_nh = FALSE;
    nh_param.hdr.nhid = nhid;
    nh_param.dsnh_offset = p_nh_param->dsnh_offset;
    sal_memcpy(&nh_param.oif, &(p_nh_param->oif), sizeof(ctc_nh_oif_info_t));
    sal_memcpy(nh_param.mac, (p_nh_param->mac), sizeof(mac_addr_t));
    nh_param.arp_id = p_nh_param->arp_id;

    nh_param.aps_en = p_nh_param->aps_en;
    if(nh_param.aps_en)
    {
        nh_param.aps_bridge_group_id = p_nh_param->aps_bridge_group_id;
        sal_memcpy(&nh_param.p_oif, &(p_nh_param->p_oif), sizeof(ctc_nh_oif_info_t));
        sal_memcpy(nh_param.p_mac, (p_nh_param->p_mac), sizeof(mac_addr_t));
        nh_param.p_arp_id = p_nh_param->p_arp_id;
        nh_param.hdr.have_dsfwd = TRUE;
    }
    else
    {
        nh_param.is_unrov_nh = CTC_FLAG_ISSET(p_nh_param->flag, CTC_IP_NH_FLAG_UNROV);  /* CTC_IP_NH_FLAG_UNROV*/
        if((SYS_RESERVE_PORT_MPLS_BFD == CTC_MAP_GPORT_TO_LPORT(p_nh_param->oif.gport))
            || (SYS_RESERVE_PORT_PW_VCCV_BFD == CTC_MAP_GPORT_TO_LPORT(p_nh_param->oif.gport)))
        {
            nh_param.hdr.have_dsfwd = TRUE;
        }

        if(p_nh_param->loop_nhid)
        {
            nh_param.loop_nhid = p_nh_param->loop_nhid;
            nh_param.hdr.have_dsfwd  = TRUE;
        }

        nh_param.ttl_no_dec  = CTC_FLAG_ISSET(p_nh_param->flag, CTC_IP_NH_FLAG_TTL_NO_DEC);
        nh_param.strip_l3    = CTC_FLAG_ISSET(p_nh_param->flag, CTC_IP_NH_FLAG_STRIP_MAC);
    }
    nh_param.mtu_no_chk = p_nh_param->mtu_no_chk;
    nh_param.merge_dsfwd = CTC_FLAG_ISSET(p_nh_param->flag, CTC_IP_NH_FLAG_MERGE_DSFWD);
    CTC_ERROR_RETURN(sys_greatbelt_nh_api_create(lchip, (sys_nh_param_com_t*)(&nh_param)));

    return CTC_E_NONE;
}

/**
 @brief This function is to remove member from multicast bridge nexthop

 @param[in] nhid, nexthop id

 @return CTC_E_XXX
 */
int32
sys_greatbelt_ipuc_nh_delete(uint8 lchip, uint32 nhid)
{
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "nhid = %d\n", nhid);

    SYS_NH_INIT_CHECK(lchip);
    CTC_ERROR_RETURN(sys_greatbelt_nh_api_delete(lchip, nhid, SYS_NH_TYPE_IPUC));
    return CTC_E_NONE;
}

/**
 @brief This function is to update ipuc nexthop

 @param[in] nhid, nexthop id
 @param[in] vid, vlan  id
 @param[in] gport, global destination port
 @param[in] mac, nexthop mac address
 @param[in] update_type, nexthop update type

 @return CTC_E_XXX
 */
int32
sys_greatbelt_ipuc_nh_update(uint8 lchip, uint32 nhid, ctc_ip_nh_param_t* p_nh_param,
                             sys_nh_entry_change_type_t update_type)
{
    sys_nh_param_ipuc_t nh_param;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "nhid = %d\n", nhid);

    SYS_NH_INIT_CHECK(lchip);
    sal_memset(&nh_param, 0, sizeof(sys_nh_param_ipuc_t));
    nh_param.hdr.nhid = nhid;
    nh_param.hdr.nh_param_type = SYS_NH_TYPE_IPUC;
    nh_param.hdr.change_type = update_type;
    sal_memcpy(&nh_param.oif, &p_nh_param->oif, sizeof(ctc_nh_oif_info_t));
    sal_memcpy(&nh_param.mac, &p_nh_param->mac, sizeof(mac_addr_t));
    nh_param.aps_en = p_nh_param->aps_en;
    if(nh_param.aps_en)
    {
        nh_param.aps_bridge_group_id = p_nh_param->aps_bridge_group_id;
        sal_memcpy(&nh_param.p_oif, &(p_nh_param->p_oif), sizeof(ctc_nh_oif_info_t));
        sal_memcpy(nh_param.p_mac, (p_nh_param->p_mac), sizeof(mac_addr_t));
        nh_param.p_arp_id = p_nh_param->p_arp_id;
        nh_param.hdr.have_dsfwd = TRUE;
    }
    else
    {
        if((SYS_RESERVE_PORT_MPLS_BFD == CTC_MAP_GPORT_TO_LPORT(p_nh_param->oif.gport))
            || (SYS_RESERVE_PORT_PW_VCCV_BFD == CTC_MAP_GPORT_TO_LPORT(p_nh_param->oif.gport)))
        {
            nh_param.hdr.have_dsfwd = TRUE;
        }

        if(p_nh_param->loop_nhid)
        {
            nh_param.loop_nhid = p_nh_param->loop_nhid;
            nh_param.hdr.have_dsfwd  = TRUE;
        }

        nh_param.ttl_no_dec  = CTC_FLAG_ISSET(p_nh_param->flag, CTC_IP_NH_FLAG_TTL_NO_DEC);
        nh_param.strip_l3    = CTC_FLAG_ISSET(p_nh_param->flag, CTC_IP_NH_FLAG_STRIP_MAC);
    }
    nh_param.mtu_no_chk = p_nh_param->mtu_no_chk;
    nh_param.merge_dsfwd = CTC_FLAG_ISSET(p_nh_param->flag, CTC_IP_NH_FLAG_MERGE_DSFWD);

    CTC_ERROR_RETURN(sys_greatbelt_nh_api_update(lchip, nhid, (sys_nh_param_com_t*)(&nh_param)));
    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_update_nhInfo(uint8 lchip, uint32 nhid, sys_nh_update_dsnh_param_t* p_update_dsnh)
{
    if (p_update_dsnh->isAddDsFwd)
    {
        sys_nh_param_ipuc_t nh_param;
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "nhid = %d\n", nhid);

        SYS_NH_INIT_CHECK(lchip);
        sal_memset(&nh_param, 0, sizeof(sys_nh_param_ipuc_t));
        if ((SYS_NH_TYPE_IPUC != p_update_dsnh->nh_entry_type) && (SYS_NH_TYPE_MISC != p_update_dsnh->nh_entry_type))
        {
            return CTC_E_NOT_SUPPORT;
        }
        nh_param.hdr.nhid = nhid;
        nh_param.hdr.have_dsfwd = p_update_dsnh->isAddDsFwd;
        nh_param.is_dsl2edit = FALSE;    /*IPUC only use ROUTE_COMPACT in current GB SDK */
        nh_param.hdr.nh_param_type = p_update_dsnh->nh_entry_type;
        nh_param.hdr.change_type = SYS_NH_CHANGE_TYPE_ADD_DYNTBL;
        if (p_update_dsnh->stats_en)
        {
            nh_param.hdr.stats_id = p_update_dsnh->stats_id;
            nh_param.hdr.stats_valid = TRUE;
        }

        CTC_ERROR_RETURN(sys_greatbelt_nh_api_update(lchip, nhid, (sys_nh_param_com_t*)(&nh_param)));
    }
    else
    {
        sys_nh_info_ipuc_t* p_nh_ipuc_info = NULL;

        SYS_NH_INIT_CHECK(lchip);
        sys_greatbelt_nh_get_nhinfo_by_nhid(lchip, nhid, (sys_nh_info_com_t**)&p_nh_ipuc_info);
        if (p_update_dsnh->updateAd && p_nh_ipuc_info->updateAd)
        {
            return CTC_E_NH_HR_NH_IN_USE;
        }

        if (p_nh_ipuc_info->hdr.nh_entry_type != SYS_NH_TYPE_IPUC)
        {
            return CTC_E_NH_INVALID_NH_TYPE;
        }

        p_nh_ipuc_info->updateAd  = p_update_dsnh->updateAd;
        p_nh_ipuc_info->data         = p_update_dsnh->data;
    }

    return CTC_E_NONE;
}


int32
sys_greatbelt_mcast_nh_get_nhid(uint8 lchip, uint32 group_id, uint32* nhid)
{
    CTC_ERROR_RETURN(sys_greatbelt_nh_get_mcast_nh(lchip, group_id, nhid));
    return CTC_E_NONE;
}


/**
 @brief This function is to create mcast nexthop

 @param[in] groupid, basic met offset of this multicast nexthop

 @param[in] p_nh_mcast_group,  group and member information this multicast nexthop

 @return CTC_E_XXX
 */

int32
sys_greatbelt_mcast_nh_create(uint8 lchip, uint32 groupid, sys_nh_param_mcast_group_t* p_nh_mcast_group)
{
    sys_nh_param_mcast_t nh_mcast;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "groupid = %d\n", groupid);

    /*Sanity Check*/
    SYS_NH_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_nh_mcast_group);

    CTC_ERROR_RETURN(sys_greatbelt_nh_check_max_glb_met_offset(lchip, groupid));

    sal_memset(&nh_mcast, 0, sizeof(sys_nh_param_mcast_t));
    sys_greatbelt_nh_get_dsfwd_mode(lchip, &nh_mcast.hdr.have_dsfwd);
    nh_mcast.hdr.nh_param_type = SYS_NH_TYPE_MCAST;
    if (p_nh_mcast_group->is_nhid_valid)
    {
        nh_mcast.hdr.nhid = p_nh_mcast_group->nhid;
        nh_mcast.hdr.is_internal_nh = FALSE;
    }
    else
    {
        nh_mcast.hdr.nhid = SYS_NH_INVALID_NHID;
        nh_mcast.hdr.is_internal_nh = TRUE;
    }

    nh_mcast.hdr.stats_valid = p_nh_mcast_group->stats_valid;
    nh_mcast.hdr.have_dsfwd= nh_mcast.hdr.have_dsfwd| p_nh_mcast_group->dsfwd_valid;
    nh_mcast.groupid = groupid;
    nh_mcast.p_member = &(p_nh_mcast_group->mem_info);
    nh_mcast.opcode = p_nh_mcast_group->opcode;
    nh_mcast.is_mirror = p_nh_mcast_group->is_mirror;
    nh_mcast.hdr.stats_id = p_nh_mcast_group->stats_id;
    CTC_ERROR_RETURN(sys_greatbelt_nh_api_create(lchip, (sys_nh_param_com_t*)(&nh_mcast)));
    p_nh_mcast_group->fwd_offset = nh_mcast.fwd_offset;
    p_nh_mcast_group->stats_ptr = nh_mcast.hdr.stats_ptr;
    p_nh_mcast_group->nhid = nh_mcast.hdr.nhid;


    return CTC_E_NONE;
}

/**
 @brief This function is to update mcast nexthop

 @param[in] nhid,  nexthopid

 @return CTC_E_XXX
 */

int32
sys_greatbelt_mcast_nh_delete(uint8 lchip, uint32 nhid)
{
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "nhid = %d\n", nhid);

    SYS_NH_INIT_CHECK(lchip);
    CTC_ERROR_RETURN(sys_greatbelt_nh_api_delete(lchip, nhid, SYS_NH_TYPE_MCAST));
    return CTC_E_NONE;
}

/**
 @brief This function is to update mcast nexthop

 @param[in] p_nh_mcast_group,  group and member information this multicast nexthop

 @return CTC_E_XXX
 */

int32
sys_greatbelt_mcast_nh_update(uint8 lchip, sys_nh_param_mcast_group_t* p_nh_mcast_group)
{
    sys_nh_param_mcast_t nh_mcast;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    SYS_NH_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_nh_mcast_group);

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "nhid = %d\n", p_nh_mcast_group->nhid);

    sal_memset(&nh_mcast, 0, sizeof(sys_nh_param_mcast_t));
    nh_mcast.hdr.nh_param_type = SYS_NH_TYPE_MCAST;
    nh_mcast.p_member = &(p_nh_mcast_group->mem_info);
    nh_mcast.hdr.nhid = p_nh_mcast_group->nhid;
    nh_mcast.opcode = p_nh_mcast_group->opcode;

    CTC_ERROR_RETURN(sys_greatbelt_nh_api_update(lchip, p_nh_mcast_group->nhid,
                                                 (sys_nh_param_com_t*)(&nh_mcast)));

    return CTC_E_NONE;
}

/**
 @brief This function is to create mpls nexthop

 @param[in] nhid  nexthop ID

 @param[in] dsnh_offset  DsNexthop offset used by this nexthop

 @param[in] p_nh_param  Nexthop param used to create this nexthop

 @return CTC_E_XXX
 */
int32
sys_greatbelt_mpls_nh_create(uint8 lchip, uint32 nhid,   ctc_mpls_nexthop_param_t* p_nh_param)
{
    sys_nh_param_mpls_t nh_param;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    SYS_NH_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_nh_param);

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "nhid = %d, dsnhoffset = %d\n", nhid, p_nh_param->dsnh_offset);
    sal_memset(&nh_param, 0, sizeof(sys_nh_param_mpls_t));
    sys_greatbelt_nh_get_dsfwd_mode(lchip, &nh_param.hdr.have_dsfwd);
    nh_param.hdr.nh_param_type = SYS_NH_TYPE_MPLS;
    nh_param.hdr.is_internal_nh = FALSE;
    nh_param.hdr.nhid = nhid;
    nh_param.p_mpls_nh_param = p_nh_param;

    CTC_ERROR_RETURN(sys_greatbelt_nh_api_create(lchip, (sys_nh_param_com_t*)(&nh_param)));

    return CTC_E_NONE;
}

/**
 @brief This function is to remove mpls nexthop by nexthop id

 @param[in] nhid nexthop id

 @return CTC_E_XXX
 */
int32
sys_greatbelt_mpls_nh_delete(uint8 lchip, uint32 nhid)
{
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "nhid = %d\n", nhid);

    SYS_NH_INIT_CHECK(lchip);
    CTC_ERROR_RETURN(sys_greatbelt_nh_api_delete(lchip, nhid, SYS_NH_TYPE_MPLS));
    return CTC_E_NONE;
}

/**
 @brief This function is to update mpls nexthop

 @param[in] nhid, nexthop id

 @param[in] p_nh_param nexthop paramter

 @return CTC_E_XXX
 */
int32
sys_greatbelt_mpls_nh_update(uint8 lchip, uint32 nhid, ctc_mpls_nexthop_param_t* p_nh_param,
                             sys_nh_entry_change_type_t change_type)
{
    sys_nh_param_mpls_t nh_param;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "nhid = %d\n", nhid);

    SYS_NH_INIT_CHECK(lchip);
    sal_memset(&nh_param, 0, sizeof(sys_nh_param_mpls_t));
    nh_param.hdr.nh_param_type = SYS_NH_TYPE_MPLS;
    nh_param.hdr.nhid = nhid;
    nh_param.p_mpls_nh_param = p_nh_param;
    nh_param.hdr.change_type = change_type;
    CTC_ERROR_RETURN(sys_greatbelt_nh_api_update(lchip, nhid, (sys_nh_param_com_t*)(&nh_param)));

    return CTC_E_NONE;
}

/**
 @brief This function is to create mist nexthop, include drop nh, tocpu nh and unresolve nh

 @param[in] nhid, nexthop id

 @param[in] nh_param_type, misc nexthop type

 @return CTC_E_XXX
 */
STATIC int32
_sys_greatbelt_misc_nh_create(uint8 lchip, uint32 nhid, sys_greatbelt_nh_type_t nh_param_type)
{
    sys_nh_param_special_t nh_para;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "nhid = %d\n", nhid);

    switch (nh_param_type)
    {
    case SYS_NH_TYPE_DROP:
    case SYS_NH_TYPE_TOCPU:
    case SYS_NH_TYPE_UNROV:
        break;

    default:
        CTC_ERROR_RETURN(CTC_E_INVALID_PARAM);
    }

    sal_memset(&nh_para, 0, sizeof(sys_nh_param_special_t));
    nh_para.hdr.nhid = nhid;
    nh_para.hdr.nh_param_type = nh_param_type;
    nh_para.hdr.is_internal_nh = FALSE;

    CTC_ERROR_RETURN(sys_greatbelt_nh_api_create(lchip, (sys_nh_param_com_t*)(&nh_para)));
    return CTC_E_NONE;
}

int32
sys_greatbelt_ecmp_nh_create(uint8 lchip, uint32 nhid, ctc_nh_ecmp_nh_param_t* p_nh_parama)
{
    sys_nh_param_ecmp_t nh_param;

    SYS_NH_INIT_CHECK(lchip);

    CTC_PTR_VALID_CHECK(p_nh_parama);

    sal_memset(&nh_param, 0, sizeof(sys_nh_param_ecmp_t));
    nh_param.hdr.have_dsfwd  = TRUE;
    nh_param.hdr.nh_param_type = SYS_NH_TYPE_ECMP;
    nh_param.hdr.is_internal_nh = FALSE;
    nh_param.p_ecmp_param = p_nh_parama;
    nh_param.hdr.nhid = nhid;

    CTC_ERROR_RETURN(sys_greatbelt_nh_api_create(lchip, (sys_nh_param_com_t*)(&nh_param)));

    return CTC_E_NONE;
}

int32
sys_greatbelt_ecmp_nh_delete(uint8 lchip, uint32 nhid)
{
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "nhid = %d\n", nhid);

    SYS_NH_INIT_CHECK(lchip);
    CTC_ERROR_RETURN(sys_greatbelt_nh_api_delete(lchip, nhid, SYS_NH_TYPE_ECMP));
    return CTC_E_NONE;
}

int32
sys_greatbelt_ecmp_nh_update(uint8 lchip, uint32 nhid, ctc_nh_ecmp_nh_param_t* p_nh_param)
{
    sys_nh_param_ecmp_t nh_param;
    ctc_nh_ecmp_nh_param_t ecmp_param;
    uint8 loop = 0;

    CTC_PTR_VALID_CHECK(p_nh_param);

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "nhid = %d\n", nhid);

    SYS_NH_INIT_CHECK(lchip);
    sal_memset(&nh_param, 0, sizeof(sys_nh_param_ecmp_t));
    sal_memset(&ecmp_param, 0, sizeof(ctc_nh_ecmp_nh_param_t));

    sal_memcpy(&ecmp_param, p_nh_param, sizeof(ctc_nh_ecmp_nh_param_t));
    nh_param.hdr.nh_param_type = SYS_NH_TYPE_ECMP;
    nh_param.hdr.nhid = nhid;
    nh_param.p_ecmp_param = &ecmp_param;

    for (loop = 0; loop < p_nh_param->nh_num; loop++)
    {
        nh_param.p_ecmp_param->nh_num = 1;
        nh_param.p_ecmp_param->nhid[0] = p_nh_param->nhid[loop];
        CTC_ERROR_RETURN(sys_greatbelt_nh_api_update(lchip, nhid, (sys_nh_param_com_t*)(&nh_param)));
    }

    return CTC_E_NONE;
}

/**
 @brief This function is to create IPE Loopback nexthop
 */
int32
sys_greatbelt_iloop_nh_create(uint8 lchip, uint32 nhid, ctc_loopback_nexthop_param_t* p_nh_param)
{
    sys_nh_param_iloop_t nh_param;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    SYS_NH_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_nh_param);
    sal_memset(&nh_param, 0, sizeof(sys_nh_param_iloop_t));

    nh_param.hdr.nh_param_type = SYS_NH_TYPE_ILOOP;
    nh_param.hdr.have_dsfwd  = TRUE;
    nh_param.hdr.is_internal_nh = FALSE;
    nh_param.p_iloop_param = p_nh_param;
    nh_param.hdr.nhid = nhid;

    CTC_ERROR_RETURN(sys_greatbelt_nh_api_create(lchip, (sys_nh_param_com_t*)(&nh_param)));

    return CTC_E_NONE;
}

int32
sys_greatbelt_iloop_nh_delete(uint8 lchip, uint32 nhid)
{
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "nhid = %d\n", nhid);

    SYS_NH_INIT_CHECK(lchip);
    CTC_ERROR_RETURN(sys_greatbelt_nh_api_delete(lchip, nhid, SYS_NH_TYPE_ILOOP));
    return CTC_E_NONE;
}

/**
 @brief This function is to create rspan  nexthop
 */
int32
sys_greatbelt_rspan_nh_create(uint8 lchip, uint32* nhid, uint32 dsnh_offset, ctc_rspan_nexthop_param_t* p_nh_param)
{
    sys_nh_param_rspan_t nh_param;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    SYS_NH_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_nh_param);
    CTC_PTR_VALID_CHECK(nhid);

    sal_memset(&nh_param, 0, sizeof(sys_nh_param_rspan_t));

    nh_param.hdr.nh_param_type = SYS_NH_TYPE_RSPAN;
    nh_param.hdr.have_dsfwd  = TRUE;
    nh_param.hdr.is_internal_nh = (*nhid == CTC_MAX_UINT32_VALUE) ? TRUE : FALSE;
    nh_param.p_rspan_param = p_nh_param;
    nh_param.hdr.nhid = *nhid;
    nh_param.dsnh_offset = dsnh_offset;

    CTC_ERROR_RETURN(sys_greatbelt_nh_api_create(lchip, (sys_nh_param_com_t*)(&nh_param)));

    if (nh_param.hdr.is_internal_nh)
    {
        *nhid = nh_param.hdr.nhid;
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_rspan_nh_delete(uint8 lchip, uint32 nhid)
{
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "nhid = %d\n", nhid);

    SYS_NH_INIT_CHECK(lchip);
    CTC_ERROR_RETURN(sys_greatbelt_nh_api_delete(lchip, nhid, SYS_NH_TYPE_RSPAN));
    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_add_stats(uint8 lchip, uint32 nhid)
{
    SYS_NH_INIT_CHECK(lchip);

    CTC_ERROR_RETURN(sys_greatbelt_nh_add_stats_action(lchip, nhid, 0));
    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_del_stats(uint8 lchip, uint32 nhid)
{
    SYS_NH_INIT_CHECK(lchip);

    CTC_ERROR_RETURN(sys_greatbelt_nh_del_stats_action(lchip, nhid));
    return CTC_E_NONE;
}

int32
sys_greatbelt_ip_tunnel_nh_create(uint8 lchip, uint32 nhid, ctc_ip_tunnel_nh_param_t* p_nh_param)
{
    sys_nh_param_ip_tunnel_t nh_param;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    SYS_NH_INIT_CHECK(lchip);

    CTC_PTR_VALID_CHECK(p_nh_param);
    CTC_ERROR_RETURN(sys_greatbelt_port_dest_gport_check(lchip, p_nh_param->oif.gport));

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "nhid = %d, dsnhoffset = %d, gport = %d\n",
                   nhid, p_nh_param->dsnh_offset, p_nh_param->oif.gport);

    sal_memset(&nh_param, 0, sizeof(nh_param));
    sys_greatbelt_nh_get_dsfwd_mode(lchip, &nh_param.hdr.have_dsfwd);
    nh_param.hdr.nh_param_type = SYS_NH_TYPE_IP_TUNNEL;
    nh_param.hdr.is_internal_nh = FALSE;
    nh_param.hdr.nhid = nhid;
    nh_param.p_ip_nh_param = p_nh_param;

    CTC_ERROR_RETURN(sys_greatbelt_nh_api_create(lchip, (sys_nh_param_com_t*)(&nh_param)));

    return CTC_E_NONE;
}

int32
sys_greatbelt_ip_tunnel_nh_delete(uint8 lchip, uint32 nhid)
{
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "nhid = %d\n", nhid);

    SYS_NH_INIT_CHECK(lchip);
    CTC_ERROR_RETURN(sys_greatbelt_nh_api_delete(lchip, nhid, SYS_NH_TYPE_IP_TUNNEL));
    return CTC_E_NONE;
}

int32
sys_greatbelt_ip_tunnel_nh_update(uint8 lchip, uint32 nhid, ctc_ip_tunnel_nh_param_t* p_nh_param)
{
    sys_nh_param_ip_tunnel_t nh_param;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "nhid = %d\n", nhid);

    SYS_NH_INIT_CHECK(lchip);
    sal_memset(&nh_param, 0, sizeof(sys_nh_param_ip_tunnel_t));
    nh_param.hdr.nh_param_type = SYS_NH_TYPE_IP_TUNNEL;
    nh_param.hdr.nhid = nhid;
    nh_param.p_ip_nh_param = p_nh_param;

    if (p_nh_param->upd_type == CTC_NH_UPD_FWD_ATTR)
    {
        nh_param.hdr.change_type = SYS_NH_CHANGE_TYPE_UPDATE_FWD_ATTR;
    }
    else if (p_nh_param->upd_type == CTC_NH_UPD_UNRSV_TO_FWD)
    {
        nh_param.hdr.change_type = SYS_NH_CHANGE_TYPE_UNROV_TO_FWD;
    }
    else
    {
        nh_param.hdr.change_type = SYS_NH_CHANGE_TYPE_FWD_TO_UNROV;
    }

    CTC_ERROR_RETURN(sys_greatbelt_nh_api_update(lchip, nhid, (sys_nh_param_com_t*)(&nh_param)));

    return CTC_E_NONE;
}

/**
 @brief The function is to add misc nexthop

*/

int32
sys_greatbelt_nh_add_misc(uint8 lchip, uint32* nhid, ctc_misc_nh_param_t* p_nh_param, bool is_internal_nh)
{

    sys_nh_param_misc_t nh_param;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    SYS_NH_INIT_CHECK(lchip);

    CTC_PTR_VALID_CHECK(p_nh_param);
    CTC_ERROR_RETURN(sys_greatbelt_port_dest_gport_check(lchip, p_nh_param->gport));

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "nhid = %d, dsnhoffset = %d, gport = %d\n",
                   *nhid, p_nh_param->dsnh_offset, p_nh_param->gport);

    sal_memset(&nh_param, 0, sizeof(nh_param));
    sys_greatbelt_nh_get_dsfwd_mode(lchip, &nh_param.hdr.have_dsfwd);
    nh_param.hdr.nh_param_type = SYS_NH_TYPE_MISC;
    nh_param.hdr.is_internal_nh = is_internal_nh;

    if (FALSE == is_internal_nh)
    {
        nh_param.hdr.nhid = *nhid;
    }

    nh_param.p_misc_param = p_nh_param;

    CTC_ERROR_RETURN(sys_greatbelt_nh_api_create(lchip, (sys_nh_param_com_t*)(&nh_param)));

    if (TRUE == is_internal_nh)
    {
        *nhid = nh_param.hdr.nhid;
    }

    return CTC_E_NONE;
}

/**
 @brief The function is to remove misc nexthop

*/
int32
sys_greatbelt_nh_remove_misc(uint8 lchip, uint32 nhid)
{
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "nhid = %d\n", nhid);

    SYS_NH_INIT_CHECK(lchip);
    CTC_ERROR_RETURN(sys_greatbelt_nh_api_delete(lchip, nhid, SYS_NH_TYPE_MISC));
    return CTC_E_NONE;
}

bool
sys_greatbelt_nh_is_ipmc_logic_rep_enable(uint8 lchip)
{
    int32 ret = 0;

    SYS_NH_INIT_CHECK(lchip);
    SYS_NH_LOCK(p_gb_nh_api_master[lchip]->brguc_info.brguc_mutex);
    ret = _sys_greatbelt_nh_is_ipmc_logic_rep_enable(lchip);
    SYS_NH_UNLOCK(p_gb_nh_api_master[lchip]->brguc_info.brguc_mutex);

    return ret ? TRUE : FALSE;
}


int32
sys_greatbelt_nh_set_ipmc_logic_replication(uint8 lchip, uint8 enable)
{
    SYS_NH_INIT_CHECK(lchip);
    SYS_NH_LOCK(p_gb_nh_api_master[lchip]->brguc_info.brguc_mutex);
    CTC_ERROR_RETURN_WITH_UNLOCK(_sys_greatbelt_nh_set_ipmc_logic_replication(lchip, enable), p_gb_nh_api_master[lchip]->brguc_info.brguc_mutex);
    SYS_NH_UNLOCK(p_gb_nh_api_master[lchip]->brguc_info.brguc_mutex);

    return CTC_E_NONE;
}



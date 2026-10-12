/**
 @file sys_goldengate_vlan_mapping.c

 @date 2010-1-4

 @version v2.0

*/

#include "ctc_debug.h"
#include "ctc_error.h"
#include "ctc_const.h"
#include "ctc_macro.h"
#include "ctc_register.h"

#include "sys_goldengate_vlan_mapping.h"
#include "sys_goldengate_scl.h"
#include "sys_goldengate_chip.h"
#include "sys_goldengate_vlan.h"
#include "sys_goldengate_nexthop_api.h"
#include "sys_goldengate_qos_policer.h"
#include "sys_goldengate_queue_enq.h"
#include "sys_goldengate_l2_fdb.h"
#include "sys_goldengate_overlay_tunnel.h"
#include "sys_goldengate_aps.h"

#include "goldengate/include/drv_lib.h"

/******************************************************************************
*
*   Macros and Defines
*
*******************************************************************************/

#define VLAN_RANGE_EN_BIT_POS           15
#define VLAN_RANGE_TYPE_BIT_POS         14

static sys_vlan_mapping_master_t* p_gg_vlan_mapping_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};

#define SYS_VLAN_RANGE_SW_EACH_GROUP() p_gg_vlan_mapping_master[lchip]->vrange[vrange_info->direction * VLAN_RANGE_ENTRY_NUM + vrange_info->vrange_grpid]

#define SYS_VLAN_RANGE_INFO_CHECK(vrange_info) \
    { \
        CTC_BOTH_DIRECTION_CHECK(vrange_info->direction); \
        if (VLAN_RANGE_ENTRY_NUM <= vrange_info->vrange_grpid) \
        {    return CTC_E_INVALID_PARAM; } \
    }

#define SYS_VLAN_ID_VALID_CHECK(vlan_id) \
    { \
        if ((vlan_id) > CTC_MAX_VLAN_ID){ \
            return CTC_E_VLAN_INVALID_VLAN_ID; } \
    }

#define SYS_VLAN_MAPPING_INIT_CHECK(lchip) \
    do { \
        SYS_LCHIP_CHECK_ACTIVE(lchip); \
        if (NULL == p_gg_vlan_mapping_master[lchip]){ \
            return CTC_E_NOT_INIT; } \
    } while(0)

#define SYS_VLAN_MAPPING_CREATE_LOCK(lchip)                         \
    do                                                              \
    {                                                               \
        sal_mutex_create(&p_gg_vlan_mapping_master[lchip]->mutex);  \
        if (NULL == p_gg_vlan_mapping_master[lchip]->mutex)         \
        {                                                           \
            CTC_ERROR_RETURN(CTC_E_FAIL_CREATE_MUTEX);              \
        }                                                           \
    } while (0)

#define SYS_VLAN_MAPPING_LOCK(lchip) \
    sal_mutex_lock(p_gg_vlan_mapping_master[lchip]->mutex)

#define SYS_VLAN_MAPPING_UNLOCK(lchip) \
    sal_mutex_unlock(p_gg_vlan_mapping_master[lchip]->mutex)

#define CTC_ERROR_RETURN_VLAN_MAPPING_UNLOCK(op) \
    do \
    { \
        int32 rv = (op); \
        if (rv < 0) \
        { \
            sal_mutex_unlock(p_gg_vlan_mapping_master[lchip]->mutex); \
            return rv; \
        } \
    } while (0)

#define SYS_VLAN_MAPPING_DBG_OUT(level, FMT, ...) \
    { \
        CTC_DEBUG_OUT(vlan, vlan_mapping, VLAN_MAPPING_SYS, level, FMT, ##__VA_ARGS__); \
    }

#define SYS_VLAN_MAPPING_DEBUG_INFO(FMT, ...) \
    { \
        SYS_VLAN_MAPPING_DBG_OUT(CTC_DEBUG_LEVEL_INFO, FMT, ##__VA_ARGS__); \
    }

#define SYS_VLAN_MAPPING_DEBUG_FUNC() \
    { \
        SYS_VLAN_MAPPING_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__); \
    }

#define SYS_VLAN_MAPPING_DEBUG_DUMP(FMT, ...) \
    { \
        SYS_VLAN_MAPPING_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, FMT, ##__VA_ARGS__); \
    }

#define SYS_VLAN_MAPPING_DEBUG_PRAM(FMT, ...) \
    { \
        SYS_VLAN_MAPPING_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, FMT, ##__VA_ARGS__); \
    }

#define TAG_CHK_OP(op) \
    { \
        if (op < CTC_VLAN_TAG_OP_NONE || op > CTC_VLAN_TAG_OP_DEL){ return CTC_E_INVALID_PARAM; } \
    }

#define VID_CHK_SELECT(vid_sl) \
    { \
        if (vid_sl < CTC_VLAN_TAG_SL_AS_PARSE || vid_sl > CTC_VLAN_TAG_SL_NEW){ return CTC_E_INVALID_PARAM; } \
    }

#define COS_CHK_SELECT(cos_sl) \
    { \
        if (cos_sl < CTC_VLAN_TAG_SL_AS_PARSE || cos_sl > CTC_VLAN_TAG_SL_NEW){ return CTC_E_INVALID_PARAM; } \
    }

enum sys_vlan_mapping_range_type_e
{
    RANGE_TYPE_IS_SVID  = 0,
    RANGE_TYPE_IS_CVID  = 1
};
typedef enum sys_vlan_mapping_range_type_e sys_vlan_mapping_range_type_t;

struct sys_vlan_mapping_tag_op_s
{
    ctc_vlan_tag_op_t      stag_op;
    ctc_vlan_tag_sl_t      svid_sl;
    ctc_vlan_tag_sl_t      scos_sl;

    ctc_vlan_tag_op_t      ctag_op;
    ctc_vlan_tag_sl_t      cvid_sl;
    ctc_vlan_tag_sl_t      ccos_sl;
};
typedef struct sys_vlan_mapping_tag_op_s sys_vlan_mapping_tag_op_t;

/******************************************************************************
*
*   Functions
*
*******************************************************************************/
STATIC int32
_sys_goldengate_vlan_get_vlan_range_mode(uint8 lchip, ctc_direction_t dir, uint8* range_mode)
{
    uint32 cmd = 0;
    uint32 vlan_range_mode = 0;

    if (CTC_INGRESS == dir)
    {
        cmd = DRV_IOR(IpeUserIdCtl_t, IpeUserIdCtl_vlanRangeMode_f);
    }
    else
    {
        cmd = DRV_IOR(EpeNextHopCtl_t, EpeNextHopCtl_vlanRangeMode_f);
    }

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &vlan_range_mode));
    *range_mode = vlan_range_mode;

    return CTC_E_NONE;
}

/*lchip's configs are the same
0~0 means disable
vlan range cannot overlap*/
int32
sys_goldengate_vlan_get_vlan_range_type(uint8 lchip, ctc_vlan_range_info_t* vrange_info, bool* is_svlan)
{

    SYS_VLAN_MAPPING_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(vrange_info);
    CTC_PTR_VALID_CHECK(is_svlan);
    SYS_VLAN_RANGE_INFO_CHECK(vrange_info);

    SYS_VLAN_MAPPING_LOCK(lchip);
    if (!CTC_IS_BIT_SET(SYS_VLAN_RANGE_SW_EACH_GROUP(), VLAN_RANGE_EN_BIT_POS))
    {
        SYS_VLAN_MAPPING_UNLOCK(lchip);
        return CTC_E_VLAN_RANGE_NOT_EXIST;
    }

    *is_svlan = (CTC_IS_BIT_SET(SYS_VLAN_RANGE_SW_EACH_GROUP(), VLAN_RANGE_TYPE_BIT_POS)) ? TRUE : FALSE;
    SYS_VLAN_MAPPING_UNLOCK(lchip);

    return CTC_E_NONE;
}

/*lchip's configs are the same
0~0 means disable
vlan range cannot overlap*/
int32
sys_goldengate_vlan_create_vlan_range(uint8 lchip, ctc_vlan_range_info_t* vrange_info, bool is_svlan)
{
    SYS_VLAN_MAPPING_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(vrange_info);
    SYS_VLAN_RANGE_INFO_CHECK(vrange_info);

    SYS_VLAN_MAPPING_LOCK(lchip);
    if (CTC_IS_BIT_SET(SYS_VLAN_RANGE_SW_EACH_GROUP(), VLAN_RANGE_EN_BIT_POS))
    {
        /*group created*/
        SYS_VLAN_MAPPING_UNLOCK(lchip);
        return CTC_E_VLAN_RANGE_EXIST;
    }

    CTC_BIT_SET(SYS_VLAN_RANGE_SW_EACH_GROUP(), VLAN_RANGE_EN_BIT_POS);

    if (is_svlan)
    {
        CTC_BIT_SET(SYS_VLAN_RANGE_SW_EACH_GROUP(), VLAN_RANGE_TYPE_BIT_POS);
    }
    else
    {
        CTC_BIT_UNSET(SYS_VLAN_RANGE_SW_EACH_GROUP(), VLAN_RANGE_TYPE_BIT_POS);
    }
    SYS_VLAN_MAPPING_UNLOCK(lchip);

    return CTC_E_NONE;
}

int32
sys_goldengate_vlan_destroy_vlan_range(uint8 lchip, ctc_vlan_range_info_t* vrange_info)
{
    uint32 cmd = 0;

    uint8 direction;
    int32 vrange_grpid;

    DsVlanRangeProfile_m igs_ds;
    DsEgressVlanRangeProfile_m egs_ds;

    SYS_VLAN_MAPPING_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(vrange_info);
    SYS_VLAN_RANGE_INFO_CHECK(vrange_info);

    SYS_VLAN_MAPPING_LOCK(lchip);
    if (!CTC_IS_BIT_SET(SYS_VLAN_RANGE_SW_EACH_GROUP(), VLAN_RANGE_EN_BIT_POS))
    {
        SYS_VLAN_MAPPING_UNLOCK(lchip);
        return CTC_E_VLAN_RANGE_NOT_EXIST;
    }
    SYS_VLAN_MAPPING_UNLOCK(lchip);

    direction = vrange_info->direction;
    vrange_grpid = vrange_info->vrange_grpid;

    sal_memset(&igs_ds, 0, sizeof(igs_ds));
    sal_memset(&egs_ds, 0, sizeof(egs_ds));


    if (CTC_INGRESS == direction)
    {
        cmd = DRV_IOW(DsVlanRangeProfile_t, DRV_ENTRY_FLAG);


            DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, vrange_grpid, cmd, &igs_ds));

    }
    else
    {
        cmd = DRV_IOW(DsEgressVlanRangeProfile_t, DRV_ENTRY_FLAG);


            DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, vrange_grpid, cmd, &egs_ds));

    }

    SYS_VLAN_MAPPING_LOCK(lchip);
    SYS_VLAN_RANGE_SW_EACH_GROUP() = 0;
    SYS_VLAN_MAPPING_UNLOCK(lchip);

    return CTC_E_NONE;
}

int32
sys_goldengate_vlan_add_vlan_range_member(uint8 lchip, ctc_vlan_range_info_t* vrange_info, ctc_vlan_range_t* vlan_range)
{
    uint32 cmd = 0;
    uint8  vlan_range_mode = 0;
    uint8  free_idx = 0;
    uint8  idx_start =0;
    uint8  idx_end = 0;
    DsVlanRangeProfile_m igs_ds;
    DsEgressVlanRangeProfile_m egs_ds;

    uint8 idx = 0;
    uint8 count = 0;
    uint8 direction;
    int32 vrange_grpid;
    uint16 vlan_end;
    uint16 vlan_start;
    ctc_vlan_range_group_t vrange_group;

    CTC_PTR_VALID_CHECK(vrange_info);
    CTC_PTR_VALID_CHECK(vlan_range);

    vlan_start = vlan_range->vlan_start;
    vlan_end = vlan_range->vlan_end;
    direction = vrange_info->direction;
    vrange_grpid = vrange_info->vrange_grpid;

    sal_memset(&igs_ds, 0, sizeof(igs_ds));
    sal_memset(&egs_ds, 0, sizeof(egs_ds));
    sal_memset(&vrange_group, 0, sizeof(ctc_vlan_range_group_t));

    SYS_VLAN_MAPPING_INIT_CHECK(lchip);
    SYS_VLAN_MAPPING_DEBUG_FUNC();
    CTC_VLAN_RANGE_CHECK(vlan_start);
    CTC_VLAN_RANGE_CHECK(vlan_end);
    SYS_VLAN_RANGE_INFO_CHECK(vrange_info);

    if (vlan_start >= vlan_end)
    {
        return CTC_E_VLAN_RANGE_END_GREATER_START;
    }

    SYS_VLAN_MAPPING_LOCK(lchip);
    if (!CTC_IS_BIT_SET(SYS_VLAN_RANGE_SW_EACH_GROUP(), VLAN_RANGE_EN_BIT_POS))
    {
        SYS_VLAN_MAPPING_UNLOCK(lchip);
        return CTC_E_VLAN_RANGE_NOT_EXIST;
    }
    SYS_VLAN_MAPPING_UNLOCK(lchip);

    /* get vlan range mode */
    CTC_ERROR_RETURN(_sys_goldengate_vlan_get_vlan_range_mode(lchip, direction, &vlan_range_mode));

    if (CTC_GLOBAL_VLAN_RANGE_MODE_SHARE == vlan_range_mode)
    {
        idx_start = vlan_range->is_acl ? 4 : 0;
        idx_end = vlan_range->is_acl ? 8 : 4;
    }
    else
    {
        idx_start = 0;
        idx_end = 8;
    }

    SYS_VLAN_MAPPING_LOCK(lchip);
    for (free_idx = idx_start; free_idx < idx_end; free_idx++)
    {
        if (!IS_BIT_SET(SYS_VLAN_RANGE_SW_EACH_GROUP(), free_idx))
        {
            break;
        }
    }
    SYS_VLAN_MAPPING_UNLOCK(lchip);

    if (free_idx >= idx_end)
    {
        return CTC_E_VLAN_MAPPING_VRANGE_FULL;
    }

    CTC_ERROR_RETURN(sys_goldengate_vlan_get_vlan_range_member(lchip, vrange_info, &vrange_group, &count));

    for (idx = 0; idx < count; idx++)
    {
        if ((vrange_group.vlan_range[idx].vlan_start == vlan_start) && (vlan_end == vrange_group.vlan_range[idx].vlan_end))
        {
            if ((CTC_GLOBAL_VLAN_RANGE_MODE_SHARE != vlan_range_mode) ||
                ((CTC_GLOBAL_VLAN_RANGE_MODE_SHARE == vlan_range_mode) && (vrange_group.vlan_range[idx].is_acl == vlan_range->is_acl)))
            {
                return CTC_E_NONE;
            }

        }

        if (((vrange_group.vlan_range[idx].vlan_start <= vlan_start) && (vlan_start <= vrange_group.vlan_range[idx].vlan_end))
            || ((vrange_group.vlan_range[idx].vlan_start <= vlan_end) && (vlan_end <= vrange_group.vlan_range[idx].vlan_end)))
        {
            return CTC_E_VLAN_MAPPING_VRANGE_OVERLAPPED;
        }
    }

    if (CTC_INGRESS == direction)
    {
        cmd = DRV_IOR(DsVlanRangeProfile_t, DRV_ENTRY_FLAG);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, vrange_grpid, cmd, &igs_ds));

        DRV_SET_FIELD_V(DsVlanRangeProfile_t, DsVlanRangeProfile_vlanMin0_f + (free_idx* p_gg_vlan_mapping_master[lchip]->igs_step), &igs_ds, vlan_start);
        DRV_SET_FIELD_V(DsVlanRangeProfile_t, DsVlanRangeProfile_vlanMax0_f + (free_idx* p_gg_vlan_mapping_master[lchip]->igs_step), &igs_ds, vlan_end);

        cmd = DRV_IOW(DsVlanRangeProfile_t, DRV_ENTRY_FLAG);

            DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, vrange_grpid, cmd, &igs_ds));

    }
    else
    {
        cmd = DRV_IOR(DsEgressVlanRangeProfile_t, DRV_ENTRY_FLAG);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, vrange_grpid, cmd, &egs_ds));

        DRV_SET_FIELD_V(DsEgressVlanRangeProfile_t, DsEgressVlanRangeProfile_vlanMin0_f + (free_idx*p_gg_vlan_mapping_master[lchip]->egs_step), &egs_ds, vlan_start);
        DRV_SET_FIELD_V(DsEgressVlanRangeProfile_t, DsEgressVlanRangeProfile_vlanMax0_f + (free_idx*p_gg_vlan_mapping_master[lchip]->egs_step), &egs_ds, vlan_end);

        cmd = DRV_IOW(DsEgressVlanRangeProfile_t, DRV_ENTRY_FLAG);

            DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, vrange_grpid, cmd, &egs_ds));

    }

    SYS_VLAN_MAPPING_LOCK(lchip);
    CTC_BIT_SET(SYS_VLAN_RANGE_SW_EACH_GROUP(), free_idx);
    SYS_VLAN_MAPPING_UNLOCK(lchip);

    return CTC_E_NONE;

}

int32
sys_goldengate_vlan_remove_vlan_range_member(uint8 lchip, ctc_vlan_range_info_t* vrange_info, ctc_vlan_range_t* vlan_range)
{

    uint32 cmd = 0;
    uint8  del_idx = 0;
    uint8  idx_start =0;
    uint8  idx_end = 0;
    uint8 direction;
    uint8  vlan_range_mode = 0;
    int32 vrange_grpid;
    uint16 vlan_start;
    uint16 vlan_end;

    DsVlanRangeProfile_m         igs_ds;
    DsEgressVlanRangeProfile_m  egs_ds;

    sal_memset(&igs_ds, 0, sizeof(igs_ds));
    sal_memset(&egs_ds, 0, sizeof(egs_ds));

    SYS_VLAN_MAPPING_INIT_CHECK(lchip);
    SYS_VLAN_MAPPING_DEBUG_FUNC();
    CTC_PTR_VALID_CHECK(vrange_info);
    CTC_PTR_VALID_CHECK(vlan_range);
    vlan_start = vlan_range->vlan_start;
    vlan_end = vlan_range->vlan_end;
    CTC_VLAN_RANGE_CHECK(vlan_start);
    CTC_VLAN_RANGE_CHECK(vlan_end);
    SYS_VLAN_RANGE_INFO_CHECK(vrange_info);

    direction = vrange_info->direction;
    vrange_grpid = vrange_info->vrange_grpid;

    SYS_VLAN_MAPPING_LOCK(lchip);
    if (!CTC_IS_BIT_SET(SYS_VLAN_RANGE_SW_EACH_GROUP(), VLAN_RANGE_EN_BIT_POS))
    {
        SYS_VLAN_MAPPING_UNLOCK(lchip);
        return CTC_E_VLAN_RANGE_NOT_EXIST;
    }
    SYS_VLAN_MAPPING_UNLOCK(lchip);

    if (vlan_start >= vlan_end)
    {
        return CTC_E_VLAN_RANGE_END_GREATER_START;
    }

    /* get vlan range mode */
    CTC_ERROR_RETURN(_sys_goldengate_vlan_get_vlan_range_mode(lchip, direction, &vlan_range_mode));

    if (CTC_GLOBAL_VLAN_RANGE_MODE_SHARE == vlan_range_mode)
    {
        idx_start = vlan_range->is_acl ? 4 : 0;
        idx_end = vlan_range->is_acl ? 8 : 4;
    }
    else
    {
        idx_start = 0;
        idx_end = 8;
    }

    if (CTC_INGRESS == direction)
    {
        cmd = DRV_IOR(DsVlanRangeProfile_t, DRV_ENTRY_FLAG);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, vrange_grpid, cmd, &igs_ds));

        for (del_idx = idx_start; del_idx < idx_end; del_idx++)
        {
            uint32 max;
            uint32 min;
            DRV_GET_FIELD_A(DsVlanRangeProfile_t, DsVlanRangeProfile_vlanMax0_f + (del_idx * p_gg_vlan_mapping_master[lchip]->igs_step), &igs_ds, &max);
            DRV_GET_FIELD_A(DsVlanRangeProfile_t, DsVlanRangeProfile_vlanMin0_f + (del_idx * p_gg_vlan_mapping_master[lchip]->igs_step), &igs_ds, &min);
            if ((vlan_end == max) && (vlan_start == min)
                && (max != min))
            {
                break;
            }
        }

        if (del_idx >= idx_end)
        {
            return CTC_E_VLAN_MAPPING_GET_VLAN_RANGE_FAILED;
        }

        DRV_SET_FIELD_V(DsVlanRangeProfile_t, DsVlanRangeProfile_vlanMax0_f + (del_idx * p_gg_vlan_mapping_master[lchip]->igs_step), &igs_ds, 0);
        DRV_SET_FIELD_V(DsVlanRangeProfile_t, DsVlanRangeProfile_vlanMin0_f + (del_idx * p_gg_vlan_mapping_master[lchip]->igs_step), &igs_ds, 0);

        cmd = DRV_IOW(DsVlanRangeProfile_t, DRV_ENTRY_FLAG);


            DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, vrange_grpid, cmd, &igs_ds));

    }
    else
    {
        cmd = DRV_IOR(DsEgressVlanRangeProfile_t, DRV_ENTRY_FLAG);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, vrange_grpid, cmd, &egs_ds));

        for (del_idx = idx_start; del_idx < idx_end; del_idx++)
        {
            uint32 max;
            uint32 min;
            DRV_GET_FIELD_A(DsEgressVlanRangeProfile_t, DsEgressVlanRangeProfile_vlanMax0_f + (del_idx * p_gg_vlan_mapping_master[lchip]->egs_step), &egs_ds, &max);
            DRV_GET_FIELD_A(DsEgressVlanRangeProfile_t, DsEgressVlanRangeProfile_vlanMin0_f + (del_idx * p_gg_vlan_mapping_master[lchip]->egs_step), &egs_ds, &min);
            if ((vlan_end == max) && (vlan_start == min)
                && (max != min))
            {
                break;
            }
        }

        if (del_idx >= idx_end)
        {
            return CTC_E_VLAN_MAPPING_GET_VLAN_RANGE_FAILED;
        }

        DRV_SET_FIELD_V(DsEgressVlanRangeProfile_t, DsEgressVlanRangeProfile_vlanMax0_f + (del_idx * p_gg_vlan_mapping_master[lchip]->egs_step), &egs_ds, 0);
        DRV_SET_FIELD_V(DsEgressVlanRangeProfile_t, DsEgressVlanRangeProfile_vlanMin0_f + (del_idx * p_gg_vlan_mapping_master[lchip]->egs_step), &egs_ds, 0);

        cmd = DRV_IOW(DsEgressVlanRangeProfile_t, DRV_ENTRY_FLAG);

            DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, vrange_grpid, cmd, &egs_ds));

    }

    SYS_VLAN_MAPPING_LOCK(lchip);
    CTC_BIT_UNSET(SYS_VLAN_RANGE_SW_EACH_GROUP(), del_idx);
    SYS_VLAN_MAPPING_UNLOCK(lchip);

    return CTC_E_NONE;
}

int32
sys_goldengate_vlan_get_vlan_range_member(uint8 lchip, ctc_vlan_range_info_t* vrange_info, ctc_vlan_range_group_t* vrange_group, uint8* count)
{
    uint32 cmd = 0;
    uint8 idx = 0;
    uint8 cnt = 0;
    uint32 max = 0;
    uint32 min = 0;
    DsVlanRangeProfile_m         igs_ds;
    DsEgressVlanRangeProfile_m  egs_ds;
    ctc_vlan_range_group_t _vrange_group;
    uint8 direction;
    int32 vrange_grpid;
    uint8  vlan_range_mode = 0;

    CTC_PTR_VALID_CHECK(count);
    CTC_PTR_VALID_CHECK(vrange_info);
    CTC_PTR_VALID_CHECK(vrange_group);

    sal_memset(&igs_ds, 0, sizeof(igs_ds));
    sal_memset(&egs_ds, 0, sizeof(egs_ds));
    sal_memset(&_vrange_group, 0, sizeof(ctc_vlan_range_group_t));

    SYS_VLAN_MAPPING_INIT_CHECK(lchip);
    SYS_VLAN_MAPPING_DEBUG_FUNC();
    SYS_VLAN_RANGE_INFO_CHECK(vrange_info);

    SYS_VLAN_MAPPING_LOCK(lchip);
    if (!CTC_IS_BIT_SET(SYS_VLAN_RANGE_SW_EACH_GROUP(), VLAN_RANGE_EN_BIT_POS))
    {
        SYS_VLAN_MAPPING_UNLOCK(lchip);
        return CTC_E_VLAN_RANGE_NOT_EXIST;
    }
    SYS_VLAN_MAPPING_UNLOCK(lchip);

    direction = vrange_info->direction;
    vrange_grpid = vrange_info->vrange_grpid;

    CTC_ERROR_RETURN(_sys_goldengate_vlan_get_vlan_range_mode(lchip, direction, &vlan_range_mode));

    if (CTC_INGRESS == direction)
    {
        cmd = DRV_IOR(DsVlanRangeProfile_t, DRV_ENTRY_FLAG);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, vrange_grpid, cmd, &igs_ds));

        for (idx = 0; idx <8 ;idx++)
        {
            DRV_GET_FIELD_A(DsVlanRangeProfile_t, DsVlanRangeProfile_vlanMax0_f + (idx * p_gg_vlan_mapping_master[lchip]->igs_step), &igs_ds, &max);
            DRV_GET_FIELD_A(DsVlanRangeProfile_t, DsVlanRangeProfile_vlanMin0_f + (idx * p_gg_vlan_mapping_master[lchip]->igs_step), &igs_ds, &min);
            _vrange_group.vlan_range[idx].vlan_start = min;
            _vrange_group.vlan_range[idx].vlan_end   = max;
        }

        for (idx = 0; idx < 8; idx++)
        {
            if (_vrange_group.vlan_range[idx].vlan_start < _vrange_group.vlan_range[idx].vlan_end)
            {
                if (CTC_GLOBAL_VLAN_RANGE_MODE_SHARE == vlan_range_mode)
                {
                    vrange_group->vlan_range[cnt].is_acl = (idx > 3) ? 1 : 0;
                }
                else if (CTC_GLOBAL_VLAN_RANGE_MODE_ACL == vlan_range_mode)
                {
                    vrange_group->vlan_range[cnt].is_acl = 1;
                }
                else
                {
                    vrange_group->vlan_range[cnt].is_acl = 0;
                }

                vrange_group->vlan_range[cnt].vlan_start = _vrange_group.vlan_range[idx].vlan_start;
                vrange_group->vlan_range[cnt].vlan_end = _vrange_group.vlan_range[idx].vlan_end;
                cnt++;
            }
        }
    }
    else if (CTC_EGRESS == direction)
    {
        cmd = DRV_IOR(DsEgressVlanRangeProfile_t, DRV_ENTRY_FLAG);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, vrange_grpid, cmd, &egs_ds));

        for (idx = 0; idx <8 ;idx++)
        {
            DRV_GET_FIELD_A(DsEgressVlanRangeProfile_t, DsEgressVlanRangeProfile_vlanMax0_f + (idx * p_gg_vlan_mapping_master[lchip]->egs_step), &egs_ds, &max);
            DRV_GET_FIELD_A(DsEgressVlanRangeProfile_t, DsEgressVlanRangeProfile_vlanMin0_f + (idx * p_gg_vlan_mapping_master[lchip]->egs_step), &egs_ds, &min);
            _vrange_group.vlan_range[idx].vlan_start = min;
            _vrange_group.vlan_range[idx].vlan_end   = max;
        }

        for (idx = 0; idx < 8; idx++)
        {
            if (_vrange_group.vlan_range[idx].vlan_start < _vrange_group.vlan_range[idx].vlan_end)
            {
                if (CTC_GLOBAL_VLAN_RANGE_MODE_SHARE == vlan_range_mode)
                {
                    vrange_group->vlan_range[cnt].is_acl = (idx > 3) ? 1 : 0;
                }
                else if (CTC_GLOBAL_VLAN_RANGE_MODE_ACL == vlan_range_mode)
                {
                    vrange_group->vlan_range[cnt].is_acl = 1;
                }
                else
                {
                    vrange_group->vlan_range[cnt].is_acl = 0;
                }

                vrange_group->vlan_range[cnt].vlan_start = _vrange_group.vlan_range[idx].vlan_start;
                vrange_group->vlan_range[cnt].vlan_end   = _vrange_group.vlan_range[idx].vlan_end;
                cnt++;
            }
        }
    }

    *count = cnt;

    return CTC_E_NONE;
}

int32
sys_goldengate_vlan_get_maxvid_from_vrange(uint8 lchip, ctc_vlan_range_info_t* vrange_info, ctc_vlan_range_t* vlan_range, bool is_svlan, uint16* p_max_vid)
{
    uint32 cmd = 0;
    uint32 max  = 0;
    uint32 min  = 0;
    uint8  idx = 0;
    uint8  idx_start =0;
    uint8  idx_end = 0;
    uint8  vlan_range_mode = 0;
    bool is_svlan_range = FALSE;
    DsVlanRangeProfile_m         igs_ds;
    DsEgressVlanRangeProfile_m  egs_ds;

    sal_memset(&igs_ds, 0, sizeof(igs_ds));
    sal_memset(&egs_ds, 0, sizeof(egs_ds));

    SYS_VLAN_MAPPING_DEBUG_FUNC();

    CTC_PTR_VALID_CHECK(vrange_info);
    CTC_PTR_VALID_CHECK(vlan_range);
    CTC_PTR_VALID_CHECK(p_max_vid);

    if (0 == vlan_range->vlan_end)
    {
        return CTC_E_NONE;
    }

    CTC_VLAN_RANGE_CHECK(vlan_range->vlan_start);
    CTC_VLAN_RANGE_CHECK(vlan_range->vlan_end);
    CTC_BOTH_DIRECTION_CHECK(vrange_info->direction);

    if (vlan_range->vlan_start >= vlan_range->vlan_end)
    {
        return CTC_E_VLAN_RANGE_END_GREATER_START;
    }

    CTC_ERROR_RETURN(sys_goldengate_vlan_get_vlan_range_type(lchip, vrange_info, &is_svlan_range));
    if (is_svlan != is_svlan_range)
    {
        return CTC_E_VLAN_MAPPING_RANGE_TYPE_NOT_MATCH;
    }

    /* get vlan range mode */
    CTC_ERROR_RETURN(_sys_goldengate_vlan_get_vlan_range_mode(lchip, vrange_info->direction, &vlan_range_mode));

    if (CTC_GLOBAL_VLAN_RANGE_MODE_SHARE == vlan_range_mode)
    {
        idx_start = vlan_range->is_acl ? 4 : 0;
        idx_end = vlan_range->is_acl ? 8 : 4;
    }
    else
    {
        idx_start = 0;
        idx_end = 8;
    }

    if (CTC_INGRESS == vrange_info->direction)
    {
        cmd = DRV_IOR(DsVlanRangeProfile_t, DRV_ENTRY_FLAG);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, vrange_info->vrange_grpid, cmd, &igs_ds));

        for (idx = idx_start; idx < idx_end; idx++)
        {
            DRV_GET_FIELD_A(DsVlanRangeProfile_t, DsVlanRangeProfile_vlanMax0_f + (idx * p_gg_vlan_mapping_master[lchip]->igs_step), &igs_ds, &max);
            DRV_GET_FIELD_A(DsVlanRangeProfile_t, DsVlanRangeProfile_vlanMin0_f + (idx * p_gg_vlan_mapping_master[lchip]->igs_step), &igs_ds, &min);
            if ((vlan_range->vlan_end == max) && (vlan_range->vlan_start == min)
                && (max != min))
            {
                break;
            }
        }

        if (idx >= idx_end)
        {
            return CTC_E_VLAN_MAPPING_GET_VLAN_RANGE_FAILED;
        }

    }
    else
    {
        cmd = DRV_IOR(DsEgressVlanRangeProfile_t, DRV_ENTRY_FLAG);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, vrange_info->vrange_grpid, cmd, &egs_ds));

        for (idx = idx_start; idx < idx_end; idx++)
        {
            DRV_GET_FIELD_A(DsEgressVlanRangeProfile_t, DsEgressVlanRangeProfile_vlanMax0_f + (idx * p_gg_vlan_mapping_master[lchip]->egs_step), &egs_ds, &max);
            DRV_GET_FIELD_A(DsEgressVlanRangeProfile_t, DsEgressVlanRangeProfile_vlanMin0_f + (idx * p_gg_vlan_mapping_master[lchip]->egs_step), &egs_ds, &min);
            if ((vlan_range->vlan_end == max) && (vlan_range->vlan_start == min)
                && (max != min))
            {
                break;
            }
        }

        if (idx >= idx_end)
        {
            return CTC_E_VLAN_MAPPING_GET_VLAN_RANGE_FAILED;
        }
    }

    *p_max_vid = max;

    return CTC_E_NONE;
}


#define INGRESS_VLAN_MAPPING


STATIC int32
_sys_goldengate_vlan_mapping_build_igs_key(uint8 lchip, uint16 gport,
                                          ctc_vlan_mapping_t* p_vlan_mapping_in,
                                          sys_scl_entry_t* pc)
{
    uint16 old_cvid = 0;
    uint16 old_svid = 0;

    ctc_vlan_range_info_t vrange_info;
    ctc_vlan_range_t vlan_range;

    sal_memset(&vrange_info, 0, sizeof(ctc_vlan_range_info_t));
    sal_memset(&vlan_range, 0, sizeof(ctc_vlan_range_t));

    SYS_VLAN_MAPPING_DEBUG_FUNC();
    if (!(p_vlan_mapping_in->flag & CTC_VLAN_MAPPING_FLAG_USE_FLEX))
    {
        if((p_vlan_mapping_in->key > CTC_VLAN_MAPPING_KEY_MAC_SA) && (p_vlan_mapping_in->key & 0xF))
        {
            return CTC_E_INVALID_PARAM;
        }

        switch (p_vlan_mapping_in->key
                & (CTC_VLAN_MAPPING_KEY_CVID | CTC_VLAN_MAPPING_KEY_SVID
                   | CTC_VLAN_MAPPING_KEY_CTAG_COS | CTC_VLAN_MAPPING_KEY_STAG_COS
                   | CTC_VLAN_MAPPING_KEY_MAC_SA | CTC_VLAN_MAPPING_KEY_IPV4_SA
                   | CTC_VLAN_MAPPING_KEY_IPV6_SA))
        {
        case (CTC_VLAN_MAPPING_KEY_CVID):
            SYS_VLAN_ID_VALID_CHECK(p_vlan_mapping_in->old_cvid);
            old_cvid = p_vlan_mapping_in->old_cvid;

            vrange_info.direction = CTC_INGRESS;
            vrange_info.vrange_grpid = p_vlan_mapping_in->vrange_grpid;
            vlan_range.is_acl = 0;
            vlan_range.vlan_start = old_cvid;
            vlan_range.vlan_end = p_vlan_mapping_in->cvlan_end;
            CTC_ERROR_RETURN(sys_goldengate_vlan_get_maxvid_from_vrange(lchip, &vrange_info, &vlan_range, FALSE, &old_cvid));

            pc->key.u.hash_port_cvlan_key.gport = gport;
            pc->key.u.hash_port_cvlan_key.cvlan = old_cvid;
            pc->key.u.hash_port_cvlan_key.dir = CTC_INGRESS;
            pc->key.type  = SYS_SCL_KEY_HASH_PORT_CVLAN;

            if (CTC_FLAG_ISSET(p_vlan_mapping_in->flag, CTC_VLAN_MAPPING_FLAG_USE_LOGIC_PORT))
            {
                pc->key.u.hash_port_cvlan_key.gport_type = CTC_SCL_GPROT_TYPE_LOGIC_PORT;
            }
            break;

        case (CTC_VLAN_MAPPING_KEY_SVID):
            SYS_VLAN_ID_VALID_CHECK(p_vlan_mapping_in->old_svid);
            old_svid = p_vlan_mapping_in->old_svid;

            vrange_info.direction = CTC_INGRESS;
            vrange_info.vrange_grpid = p_vlan_mapping_in->vrange_grpid;
            vlan_range.is_acl = 0;
            vlan_range.vlan_start = old_svid;
            vlan_range.vlan_end = p_vlan_mapping_in->svlan_end;
            CTC_ERROR_RETURN(sys_goldengate_vlan_get_maxvid_from_vrange(lchip, &vrange_info, &vlan_range, TRUE, &old_svid));

            pc->key.u.hash_port_svlan_key.gport = gport;
            pc->key.u.hash_port_svlan_key.svlan = old_svid;
            pc->key.u.hash_port_svlan_key.dir = CTC_INGRESS;
            pc->key.type  = SYS_SCL_KEY_HASH_PORT_SVLAN;

            if (CTC_FLAG_ISSET(p_vlan_mapping_in->flag, CTC_VLAN_MAPPING_FLAG_USE_LOGIC_PORT))
            {
                pc->key.u.hash_port_svlan_key.gport_type = CTC_SCL_GPROT_TYPE_LOGIC_PORT;
            }
            break;

        case (CTC_VLAN_MAPPING_KEY_CVID | CTC_VLAN_MAPPING_KEY_SVID):
            SYS_VLAN_ID_VALID_CHECK(p_vlan_mapping_in->old_cvid);
            SYS_VLAN_ID_VALID_CHECK(p_vlan_mapping_in->old_svid);
            old_cvid = p_vlan_mapping_in->old_cvid;
            old_svid = p_vlan_mapping_in->old_svid;

            vrange_info.direction = CTC_INGRESS;
            vrange_info.vrange_grpid = p_vlan_mapping_in->vrange_grpid;
            vlan_range.is_acl = 0;
            vlan_range.vlan_start = old_cvid;
            vlan_range.vlan_end = p_vlan_mapping_in->cvlan_end;
            CTC_ERROR_RETURN(sys_goldengate_vlan_get_maxvid_from_vrange(lchip, &vrange_info, &vlan_range, FALSE, &old_cvid));

            vrange_info.direction = CTC_INGRESS;
            vrange_info.vrange_grpid = p_vlan_mapping_in->vrange_grpid;
            vlan_range.is_acl = 0;
            vlan_range.vlan_start = old_svid;
            vlan_range.vlan_end = p_vlan_mapping_in->svlan_end;
            CTC_ERROR_RETURN(sys_goldengate_vlan_get_maxvid_from_vrange(lchip, &vrange_info, &vlan_range, TRUE, &old_svid));

            pc->key.u.hash_port_2vlan_key.gport = gport;
            pc->key.u.hash_port_2vlan_key.cvlan  = old_cvid;
            pc->key.u.hash_port_2vlan_key.svlan  = old_svid;
            pc->key.u.hash_port_2vlan_key.dir = CTC_INGRESS;

            pc->key.type  = SYS_SCL_KEY_HASH_PORT_2VLAN;

            if (CTC_FLAG_ISSET(p_vlan_mapping_in->flag, CTC_VLAN_MAPPING_FLAG_USE_LOGIC_PORT))
            {
                pc->key.u.hash_port_2vlan_key.gport_type = CTC_SCL_GPROT_TYPE_LOGIC_PORT;
            }
            break;

        case (CTC_VLAN_MAPPING_KEY_CVID | CTC_VLAN_MAPPING_KEY_CTAG_COS):
            SYS_VLAN_ID_VALID_CHECK(p_vlan_mapping_in->old_cvid);
            CTC_COS_RANGE_CHECK(p_vlan_mapping_in->old_ccos);
            old_cvid = p_vlan_mapping_in->old_cvid;

            vrange_info.direction = CTC_INGRESS;
            vrange_info.vrange_grpid = p_vlan_mapping_in->vrange_grpid;
            vlan_range.is_acl = 0;
            vlan_range.vlan_start = old_cvid;
            vlan_range.vlan_end = p_vlan_mapping_in->cvlan_end;
            CTC_ERROR_RETURN(sys_goldengate_vlan_get_maxvid_from_vrange(lchip, &vrange_info, &vlan_range, FALSE, &old_cvid));

            pc->key.u.hash_port_cvlan_cos_key.gport = gport;
            pc->key.u.hash_port_cvlan_cos_key.cvlan = old_cvid;
            pc->key.u.hash_port_cvlan_cos_key.ctag_cos= p_vlan_mapping_in->old_ccos;
            pc->key.u.hash_port_cvlan_cos_key.dir = CTC_INGRESS;

            pc->key.type  = SYS_SCL_KEY_HASH_PORT_CVLAN_COS;

            if (CTC_FLAG_ISSET(p_vlan_mapping_in->flag, CTC_VLAN_MAPPING_FLAG_USE_LOGIC_PORT))
            {
                pc->key.u.hash_port_cvlan_cos_key.gport_type = CTC_SCL_GPROT_TYPE_LOGIC_PORT;
            }
            break;

        case (CTC_VLAN_MAPPING_KEY_SVID | CTC_VLAN_MAPPING_KEY_STAG_COS):
            SYS_VLAN_ID_VALID_CHECK(p_vlan_mapping_in->old_svid);
            CTC_COS_RANGE_CHECK(p_vlan_mapping_in->old_scos);
            old_svid = p_vlan_mapping_in->old_svid;

            vrange_info.direction = CTC_INGRESS;
            vrange_info.vrange_grpid = p_vlan_mapping_in->vrange_grpid;
            vlan_range.is_acl = 0;
            vlan_range.vlan_start = old_svid;
            vlan_range.vlan_end = p_vlan_mapping_in->svlan_end;
            CTC_ERROR_RETURN(sys_goldengate_vlan_get_maxvid_from_vrange(lchip, &vrange_info, &vlan_range, TRUE, &old_svid));

            pc->key.u.hash_port_svlan_cos_key.gport = gport;
            pc->key.u.hash_port_svlan_cos_key.svlan = old_svid;
            pc->key.u.hash_port_svlan_cos_key.stag_cos= p_vlan_mapping_in->old_scos;
            pc->key.u.hash_port_svlan_cos_key.dir = CTC_INGRESS;

            pc->key.type  = SYS_SCL_KEY_HASH_PORT_SVLAN_COS;

            if (CTC_FLAG_ISSET(p_vlan_mapping_in->flag, CTC_VLAN_MAPPING_FLAG_USE_LOGIC_PORT))
            {
                pc->key.u.hash_port_svlan_cos_key.gport_type = CTC_SCL_GPROT_TYPE_LOGIC_PORT;
            }
            break;

        case CTC_VLAN_MAPPING_KEY_MAC_SA:
            sal_memcpy(pc->key.u.hash_port_mac_key.mac, p_vlan_mapping_in->macsa, sizeof(mac_addr_t));
            pc->key.u.hash_port_mac_key.mac_is_da = 0;
            pc->key.u.hash_port_mac_key.gport = gport;
            pc->key.type  = SYS_SCL_KEY_HASH_PORT_MAC;

            if (CTC_FLAG_ISSET(p_vlan_mapping_in->flag, CTC_VLAN_MAPPING_FLAG_USE_LOGIC_PORT))
            {
                pc->key.u.hash_port_mac_key.gport_type = CTC_SCL_GPROT_TYPE_LOGIC_PORT;
            }
            break;

        case CTC_VLAN_MAPPING_KEY_IPV4_SA:
            pc->key.u.hash_port_ipv4_key.ip_sa = p_vlan_mapping_in->ipv4_sa;
            pc->key.u.hash_port_ipv4_key.gport = gport;
            pc->key.type  = SYS_SCL_KEY_HASH_PORT_IPV4;

            if (CTC_FLAG_ISSET(p_vlan_mapping_in->flag, CTC_VLAN_MAPPING_FLAG_USE_LOGIC_PORT))
            {
                pc->key.u.hash_port_ipv4_key.gport_type = CTC_SCL_GPROT_TYPE_LOGIC_PORT;
            }
            break;

        case CTC_VLAN_MAPPING_KEY_IPV6_SA:
            sal_memcpy(pc->key.u.hash_port_ipv6_key.ip_sa, p_vlan_mapping_in->ipv6_sa, sizeof(ipv6_addr_t));
            pc->key.u.hash_port_ipv6_key.gport = gport;
            pc->key.type  = SYS_SCL_KEY_HASH_PORT_IPV6;

            if (CTC_FLAG_ISSET(p_vlan_mapping_in->flag, CTC_VLAN_MAPPING_FLAG_USE_LOGIC_PORT))
            {
                pc->key.u.hash_port_ipv6_key.gport_type = CTC_SCL_GPROT_TYPE_LOGIC_PORT;
            }
            break;
        case CTC_VLAN_MAPPING_KEY_NONE:

            pc->key.u.hash_port_key.gport = gport;
            pc->key.u.hash_port_key.dir = CTC_INGRESS;

            pc->key.type  = SYS_SCL_KEY_HASH_PORT;

            if (CTC_FLAG_ISSET(p_vlan_mapping_in->flag, CTC_VLAN_MAPPING_FLAG_USE_LOGIC_PORT))
            {
                pc->key.u.hash_port_key.gport_type = CTC_SCL_GPROT_TYPE_LOGIC_PORT;
            }
            break;

        default:
            {
                if (p_vlan_mapping_in->key & CTC_VLAN_MAPPING_KEY_CVID)
                {
                    SYS_VLAN_ID_VALID_CHECK(p_vlan_mapping_in->old_cvid);
                    old_cvid = p_vlan_mapping_in->old_cvid;

                    vrange_info.direction = CTC_INGRESS;
                    vrange_info.vrange_grpid = p_vlan_mapping_in->vrange_grpid;
                    vlan_range.is_acl = 0;
                    vlan_range.vlan_start = old_cvid;
                    vlan_range.vlan_end = p_vlan_mapping_in->cvlan_end;
                    CTC_ERROR_RETURN(sys_goldengate_vlan_get_maxvid_from_vrange(lchip, &vrange_info, &vlan_range, FALSE, &old_cvid));

                    pc->key.u.tcam_vlan_key.cvlan = old_cvid;
                    pc->key.u.tcam_vlan_key.cvlan_mask = 0xFFF;
                    CTC_SET_FLAG(pc->key.u.tcam_vlan_key.flag ,CTC_SCL_TCAM_VLAN_KEY_FLAG_CVLAN);
                }

                if (p_vlan_mapping_in->key & CTC_VLAN_MAPPING_KEY_CTAG_COS)
                {
                    CTC_COS_RANGE_CHECK(p_vlan_mapping_in->old_ccos);
                    pc->key.u.tcam_vlan_key.ctag_cos = p_vlan_mapping_in->old_ccos;
                    pc->key.u.tcam_vlan_key.ctag_cos_mask    = 0x7;
                    CTC_SET_FLAG(pc->key.u.tcam_vlan_key.flag ,CTC_SCL_TCAM_VLAN_KEY_FLAG_CTAG_COS);
                }

                if (p_vlan_mapping_in->key & CTC_VLAN_MAPPING_KEY_SVID)
                {
                    SYS_VLAN_ID_VALID_CHECK(p_vlan_mapping_in->old_svid);
                    old_svid = p_vlan_mapping_in->old_svid;

                    vrange_info.direction = CTC_INGRESS;
                    vrange_info.vrange_grpid = p_vlan_mapping_in->vrange_grpid;
                    vlan_range.is_acl = 0;
                    vlan_range.vlan_start = old_svid;
                    vlan_range.vlan_end = p_vlan_mapping_in->svlan_end;
                    CTC_ERROR_RETURN(sys_goldengate_vlan_get_maxvid_from_vrange(lchip, &vrange_info, &vlan_range, TRUE, &old_svid));

                    pc->key.u.tcam_vlan_key.svlan = old_svid;
                    pc->key.u.tcam_vlan_key.svlan_mask = 0xFFF;
                    CTC_SET_FLAG(pc->key.u.tcam_vlan_key.flag ,CTC_SCL_TCAM_VLAN_KEY_FLAG_SVLAN);
                }

                if (p_vlan_mapping_in->key & CTC_VLAN_MAPPING_KEY_STAG_COS)
                {
                    CTC_COS_RANGE_CHECK(p_vlan_mapping_in->old_scos);
                    pc->key.u.tcam_vlan_key.stag_cos = p_vlan_mapping_in->old_scos;
                    pc->key.u.tcam_vlan_key.stag_cos_mask = 0x7;
                    CTC_SET_FLAG(pc->key.u.tcam_vlan_key.flag ,CTC_SCL_TCAM_VLAN_KEY_FLAG_STAG_COS);
                }

                pc->key.type  = SYS_SCL_KEY_TCAM_VLAN;
            }

        }
    }
    else
    {
        if(p_vlan_mapping_in->key >= CTC_VLAN_MAPPING_KEY_MAC_SA)
        {
            return CTC_E_INVALID_PARAM;
        }

        if (p_vlan_mapping_in->key & CTC_VLAN_MAPPING_KEY_CVID)
        {
            SYS_VLAN_ID_VALID_CHECK(p_vlan_mapping_in->old_cvid);
            old_cvid = p_vlan_mapping_in->old_cvid;

            vrange_info.direction = CTC_INGRESS;
            vrange_info.vrange_grpid = p_vlan_mapping_in->vrange_grpid;
            vlan_range.is_acl = 0;
            vlan_range.vlan_start = old_cvid;
            vlan_range.vlan_end = p_vlan_mapping_in->cvlan_end;
            CTC_ERROR_RETURN(sys_goldengate_vlan_get_maxvid_from_vrange(lchip, &vrange_info, &vlan_range, FALSE, &old_cvid));

            pc->key.u.tcam_vlan_key.cvlan = old_cvid;
            pc->key.u.tcam_vlan_key.cvlan_mask = 0xFFF;
            CTC_SET_FLAG(pc->key.u.tcam_vlan_key.flag ,CTC_SCL_TCAM_VLAN_KEY_FLAG_CVLAN);
        }

        if (p_vlan_mapping_in->key & CTC_VLAN_MAPPING_KEY_CTAG_COS)
        {
            CTC_COS_RANGE_CHECK(p_vlan_mapping_in->old_ccos);
            pc->key.u.tcam_vlan_key.ctag_cos = p_vlan_mapping_in->old_ccos;
            pc->key.u.tcam_vlan_key.ctag_cos_mask    = 0x7;
            CTC_SET_FLAG(pc->key.u.tcam_vlan_key.flag ,CTC_SCL_TCAM_VLAN_KEY_FLAG_CTAG_COS);
        }

        if (p_vlan_mapping_in->key & CTC_VLAN_MAPPING_KEY_SVID)
        {
            SYS_VLAN_ID_VALID_CHECK(p_vlan_mapping_in->old_svid);
            old_svid = p_vlan_mapping_in->old_svid;

            vrange_info.direction = CTC_INGRESS;
            vrange_info.vrange_grpid = p_vlan_mapping_in->vrange_grpid;
            vlan_range.is_acl = 0;
            vlan_range.vlan_start = old_svid;
            vlan_range.vlan_end = p_vlan_mapping_in->svlan_end;
            CTC_ERROR_RETURN(sys_goldengate_vlan_get_maxvid_from_vrange(lchip, &vrange_info, &vlan_range, TRUE, &old_svid));

            pc->key.u.tcam_vlan_key.svlan = old_svid;
            pc->key.u.tcam_vlan_key.svlan_mask = 0xFFF;
            CTC_SET_FLAG(pc->key.u.tcam_vlan_key.flag ,CTC_SCL_TCAM_VLAN_KEY_FLAG_SVLAN);
        }

        if (p_vlan_mapping_in->key & CTC_VLAN_MAPPING_KEY_STAG_COS)
        {
            CTC_COS_RANGE_CHECK(p_vlan_mapping_in->old_scos);
            pc->key.u.tcam_vlan_key.stag_cos = p_vlan_mapping_in->old_scos;
            pc->key.u.tcam_vlan_key.stag_cos_mask = 0x7;
            CTC_SET_FLAG(pc->key.u.tcam_vlan_key.flag ,CTC_SCL_TCAM_VLAN_KEY_FLAG_STAG_COS);
        }

        pc->key.type  = SYS_SCL_KEY_TCAM_VLAN;
    }
/*    pc->chip_id = lchip;*/
    return CTC_E_NONE;

}



/*some props are not supported in ingress vlan, should return error.*/
STATIC int32
_sys_goldengate_vlan_mapping_build_igs_ad(uint8 lchip, ctc_vlan_mapping_t* p_vlan_mapping_in,
                                         ctc_scl_igs_action_t* pia)
{


    uint8  is_svid_en     = 0;
    uint8  is_cvid_en     = 0;
    uint8  is_scos_en     = 0;
    uint8  is_ccos_en     = 0;
    uint8  is_logic_port_en     = 0;
    uint8  is_aps_en      = 0;
    uint8  is_fid_en      = 0;
    uint8  is_nh_en       = 0;
    uint8  is_service_plc_en  = 0;
    uint8  is_service_acl_en  = 0;
    uint8  is_vn_id_en        = 0;
    uint8  is_stats_en        = 0;
    uint16 fid                = 0;
    uint8  is_stp_id_en        = 0;
    uint8 is_l2vpn_oam = 0;
    uint8 is_vlan_domain_en = 0;
    uint8  is_vrfid_en        = 0;
    uint8 mode = 0;

    SYS_VLAN_MAPPING_DEBUG_FUNC();

    if ((p_vlan_mapping_in->stag_op >= CTC_VLAN_TAG_OP_MAX)
        || (p_vlan_mapping_in->svid_sl >= CTC_VLAN_TAG_SL_MAX)
        || (p_vlan_mapping_in->scos_sl >= CTC_VLAN_TAG_SL_MAX)
        || (p_vlan_mapping_in->ctag_op >= CTC_VLAN_TAG_OP_MAX)
        || (p_vlan_mapping_in->cvid_sl >= CTC_VLAN_TAG_SL_MAX)
        || (p_vlan_mapping_in->ccos_sl >= CTC_VLAN_TAG_SL_MAX))
    {
        return CTC_E_INVALID_PARAM;
    }

    is_svid_en    = CTC_FLAG_ISSET(p_vlan_mapping_in->action, CTC_VLAN_MAPPING_OUTPUT_SVID);
    is_cvid_en    = CTC_FLAG_ISSET(p_vlan_mapping_in->action, CTC_VLAN_MAPPING_OUTPUT_CVID);
    is_scos_en    = CTC_FLAG_ISSET(p_vlan_mapping_in->action, CTC_VLAN_MAPPING_OUTPUT_SCOS);
    is_ccos_en    = CTC_FLAG_ISSET(p_vlan_mapping_in->action, CTC_VLAN_MAPPING_OUTPUT_CCOS);
    is_logic_port_en    = CTC_FLAG_ISSET(p_vlan_mapping_in->action, CTC_VLAN_MAPPING_OUTPUT_LOGIC_SRC_PORT);
    is_aps_en     = CTC_FLAG_ISSET(p_vlan_mapping_in->action, CTC_VLAN_MAPPING_OUTPUT_APS_SELECT);
    is_fid_en     = CTC_FLAG_ISSET(p_vlan_mapping_in->action, CTC_VLAN_MAPPING_OUTPUT_FID);
    is_nh_en      = CTC_FLAG_ISSET(p_vlan_mapping_in->action, CTC_VLAN_MAPPING_OUTPUT_NHID);
    is_service_plc_en = CTC_FLAG_ISSET(p_vlan_mapping_in->action, CTC_VLAN_MAPPING_FLAG_SERVICE_POLICER_EN);
    is_service_acl_en = CTC_FLAG_ISSET(p_vlan_mapping_in->action, CTC_VLAN_MAPPING_FLAG_SERVICE_ACL_EN);
    is_vn_id_en   = CTC_FLAG_ISSET(p_vlan_mapping_in->action, CTC_VLAN_MAPPING_OUTPUT_VN_ID);
    is_stats_en       = CTC_FLAG_ISSET(p_vlan_mapping_in->action, CTC_VLAN_MAPPING_FLAG_STATS_EN);
    is_stp_id_en   = CTC_FLAG_ISSET(p_vlan_mapping_in->action, CTC_VLAN_MAPPING_OUTPUT_STP_ID);
    is_l2vpn_oam = CTC_FLAG_ISSET(p_vlan_mapping_in->action, CTC_VLAN_MAPPING_FLAG_L2VPN_OAM);
    is_vlan_domain_en = CTC_FLAG_ISSET(p_vlan_mapping_in->action, CTC_VLAN_MAPPING_OUTPUT_VLAN_DOMAIN);
    is_vrfid_en       = CTC_FLAG_ISSET(p_vlan_mapping_in->action, CTC_VLAN_MAPPING_OUTPUT_VRFID);

    if ((is_fid_en + is_nh_en + is_vn_id_en + is_vrfid_en) > 1)
    {
        return CTC_E_INVALID_PARAM;
    }

    if (is_logic_port_en)
    {
        if (p_vlan_mapping_in->logic_src_port > 0x3fff)
        {
            return CTC_E_INVALID_PARAM;
        }
    }

    if (CTC_FLAG_ISSET(p_vlan_mapping_in->action, CTC_VLAN_MAPPING_FLAG_VPWS | CTC_VLAN_MAPPING_FLAG_VPLS))
    {
        return CTC_E_INVALID_PARAM;
    }

    if (is_l2vpn_oam)
    {
        if ((!CTC_FLAG_ISSET(p_vlan_mapping_in->action, CTC_VLAN_MAPPING_FLAG_VPWS))
            && (!CTC_FLAG_ISSET(p_vlan_mapping_in->action, CTC_VLAN_MAPPING_FLAG_VPLS)))
        {
            return CTC_E_INVALID_PARAM;
        }
    }

    if ((p_vlan_mapping_in->action & CTC_VLAN_MAPPING_FLAG_VPLS_LRN_DIS) ||
        (p_vlan_mapping_in->action & CTC_VLAN_MAPPING_FLAG_MACLIMIT_EN) ||
        (p_vlan_mapping_in->action & CTC_VLAN_MAPPING_FLAG_SERVICE_QUEUE_EN) ||
        (p_vlan_mapping_in->action & CTC_VLAN_MAPPING_OUTPUT_SERVICE_ID) ||
        (p_vlan_mapping_in->action & CTC_VLAN_MAPPING_FLAG_IGMP_SNOOPING_EN))
    {
        return CTC_E_NOT_SUPPORT;
    }

    sys_goldengate_vlan_overlay_get_vni_fid_mapping_mode(lchip, &mode);
    if(is_vn_id_en && mode)
    {
        return CTC_E_NOT_SUPPORT;
    }

    if (p_vlan_mapping_in->stag_op || p_vlan_mapping_in->ctag_op)
    {
        CTC_SET_FLAG(pia->flag, CTC_SCL_IGS_ACTION_FLAG_VLAN_EDIT);
    }


    pia->vlan_edit.stag_op    = p_vlan_mapping_in->stag_op;
    pia->vlan_edit.svid_sl    = p_vlan_mapping_in->svid_sl;
    pia->vlan_edit.scos_sl    = p_vlan_mapping_in->scos_sl;

    if((CTC_VLAN_TAG_OP_REP_OR_ADD == pia->vlan_edit.stag_op) ||
        (CTC_VLAN_TAG_OP_ADD == pia->vlan_edit.stag_op))
    {
        pia->vlan_edit.vlan_domain = CTC_SCL_VLAN_DOMAIN_SVLAN;
    }

    pia->vlan_edit.ctag_op    = p_vlan_mapping_in->ctag_op;
    pia->vlan_edit.cvid_sl    = p_vlan_mapping_in->cvid_sl;
    pia->vlan_edit.ccos_sl    = p_vlan_mapping_in->ccos_sl;
    pia->vlan_edit.tpid_index = p_vlan_mapping_in->tpid_index;

    if (is_svid_en)
    {
        SYS_VLAN_ID_VALID_CHECK(p_vlan_mapping_in->new_svid);
        pia->vlan_edit.svid_new = p_vlan_mapping_in->new_svid;
    }

    if (is_scos_en)
    {
        CTC_COS_RANGE_CHECK(p_vlan_mapping_in->new_scos);
        pia->vlan_edit.scos_new = p_vlan_mapping_in->new_scos;
    }

    if (is_cvid_en)
    {
        SYS_VLAN_ID_VALID_CHECK(p_vlan_mapping_in->new_cvid);
        pia->vlan_edit.cvid_new = p_vlan_mapping_in->new_cvid;
    }

    if (is_ccos_en)
    {
        CTC_COS_RANGE_CHECK(p_vlan_mapping_in->new_ccos);
        pia->vlan_edit.ccos_new = p_vlan_mapping_in->new_ccos;
    }

    if (is_logic_port_en)
    {
        CTC_SET_FLAG(pia->flag, CTC_SCL_IGS_ACTION_FLAG_LOGIC_PORT);
        pia->logic_port.logic_port  = p_vlan_mapping_in->logic_src_port;
        pia->logic_port.logic_port_type = p_vlan_mapping_in->logic_port_type;
    }

    if (is_aps_en)
    {
        CTC_SET_FLAG(pia->flag, CTC_SCL_IGS_ACTION_FLAG_APS);
        if (0 != p_vlan_mapping_in->protected_vlan)
        {
            pia->aps.protected_vlan_valid   = 1;
            CTC_VLAN_RANGE_CHECK(p_vlan_mapping_in->protected_vlan);
            pia->aps.protected_vlan = p_vlan_mapping_in->protected_vlan;
        }

        SYS_APS_GROUP_ID_CHECK(p_vlan_mapping_in->aps_select_group_id);
        pia->aps.aps_select_group_id = p_vlan_mapping_in->aps_select_group_id;
        pia->aps.is_working_path = p_vlan_mapping_in->is_working_path;

        CTC_SET_FLAG(pia->flag, CTC_SCL_IGS_ACTION_FLAG_LOGIC_PORT);
        pia->logic_port.logic_port  = p_vlan_mapping_in->logic_src_port;
    }

    if (p_vlan_mapping_in->policer_id > 0)
    {
        pia->policer_id = p_vlan_mapping_in->policer_id;

        if (is_service_plc_en)
        {
            CTC_SET_FLAG(pia->sub_flag, CTC_SCL_IGS_ACTION_SUB_FLAG_SERVICE_POLICER_EN);
        }
    }

    if (is_service_acl_en)
    {
        CTC_SET_FLAG(pia->sub_flag, CTC_SCL_IGS_ACTION_SUB_FLAG_SERVICE_ACL_EN);
    }

    if (is_fid_en)
    {
        SYS_L2_FID_CHECK(p_vlan_mapping_in->u3.fid);
        CTC_SET_FLAG(pia->flag, CTC_SCL_IGS_ACTION_FLAG_FID);
        pia->fid    = p_vlan_mapping_in->u3.fid;
    }

    if (is_stp_id_en)
    {
        CTC_MAX_VALUE_CHECK(p_vlan_mapping_in->stp_id, 127);
        CTC_SET_FLAG(pia->flag, CTC_SCL_IGS_ACTION_FLAG_STP_ID);
        pia->stp_id = p_vlan_mapping_in->stp_id;
    }

    if (CTC_FLAG_ISSET(p_vlan_mapping_in->action, CTC_VLAN_MAPPING_OUTPUT_VLANPTR))
    {
        CTC_MAX_VALUE_CHECK(p_vlan_mapping_in->user_vlanptr,0x1fff);
        pia->user_vlanptr = p_vlan_mapping_in->user_vlanptr;
        CTC_SET_FLAG(pia->flag, CTC_SCL_IGS_ACTION_FLAG_USER_VLANPTR);
    }

    if (is_vn_id_en)
    {
        CTC_ERROR_RETURN(sys_goldengate_vlan_overlay_get_fid(lchip, p_vlan_mapping_in->u3.vn_id, &fid));
        SYS_L2_FID_CHECK(fid);
        CTC_SET_FLAG(pia->flag, CTC_SCL_IGS_ACTION_FLAG_FID);
        pia->fid    = fid;
    }

    if (is_vrfid_en)
    {
         /*SYS_L2_FID_CHECK(p_vlan_mapping_in->u3.fid);*/
        CTC_SET_FLAG(pia->flag, CTC_SCL_IGS_ACTION_FLAG_VRFID);
        pia->vrf_id  = p_vlan_mapping_in->u3.vrf_id;
    }

    if (CTC_QOS_COLOR_NONE != p_vlan_mapping_in->color)
    {
        pia->color      = p_vlan_mapping_in->color;
        pia->priority   = p_vlan_mapping_in->priority;
        CTC_SET_FLAG(pia->flag, CTC_SCL_IGS_ACTION_FLAG_PRIORITY_AND_COLOR);
    }
    else
    {
        CTC_UNSET_FLAG(pia->flag, CTC_SCL_IGS_ACTION_FLAG_PRIORITY_AND_COLOR);
    }

    if (p_vlan_mapping_in->action & CTC_VLAN_MAPPING_FLAG_VPLS)
    {
        CTC_SET_FLAG(pia->flag, CTC_SCL_IGS_ACTION_FLAG_LOGIC_PORT);
        pia->logic_port.logic_port  = p_vlan_mapping_in->logic_src_port;
        if(0 == p_vlan_mapping_in->user_vlanptr)
        {
            pia->user_vlanptr = SYS_SCL_BY_PASS_VLAN_PTR;
            CTC_SET_FLAG(pia->flag, CTC_SCL_IGS_ACTION_FLAG_USER_VLANPTR);
        }
    }

    if ((p_vlan_mapping_in->action & CTC_VLAN_MAPPING_FLAG_VPLS) ||
        (p_vlan_mapping_in->action & CTC_VLAN_MAPPING_FLAG_VPWS))
    {
        pia->vlan_edit.vlan_domain = CTC_SCL_VLAN_DOMAIN_UNCHANGE;
        if (CTC_VLAN_TAG_OP_NONE == pia->vlan_edit.stag_op) /* Perfect solution is user set valid. Here consider compatible with old solution. */
        {
            pia->vlan_edit.stag_op     = CTC_VLAN_TAG_OP_VALID;
        }
        CTC_SET_FLAG(pia->flag, CTC_SCL_IGS_ACTION_FLAG_VLAN_EDIT);
    }

    if (CTC_FLAG_ISSET(p_vlan_mapping_in->action, CTC_VLAN_MAPPING_FLAG_ETREE_LEAF))
    {
        CTC_SET_FLAG(pia->flag, CTC_SCL_IGS_ACTION_FLAG_ETREE_LEAF);
    }

    /*for vpws*/
    if (is_nh_en)
    {
        CTC_SET_FLAG(pia->flag, CTC_SCL_IGS_ACTION_FLAG_REDIRECT);
        pia->nh_id      =  p_vlan_mapping_in->u3.nh_id;
    }

    if (is_stats_en)
    {
        CTC_SET_FLAG(pia->flag, CTC_SCL_IGS_ACTION_FLAG_STATS);
        pia->stats_id = p_vlan_mapping_in->stats_id;
    }

    if (is_l2vpn_oam)
    {
        CTC_SET_FLAG(pia->flag, CTC_SCL_IGS_ACTION_FLAG_L2VPN_OAM);
        if (CTC_FLAG_ISSET(p_vlan_mapping_in->action, CTC_VLAN_MAPPING_FLAG_VPLS))
        {
            CTC_SET_FLAG(pia->flag, CTC_SCL_IGS_ACTION_FLAG_VPLS_OAM);
        }
        pia->l2vpn_oam_id = p_vlan_mapping_in->l2vpn_oam_id;
    }

    if (is_vlan_domain_en)
    {
        CTC_MAX_VALUE_CHECK(p_vlan_mapping_in->vlan_domain, CTC_SCL_VLAN_DOMAIN_MAX-1);
        CTC_SET_FLAG(pia->flag, CTC_SCL_IGS_ACTION_FLAG_VLAN_EDIT);
        pia->vlan_edit.vlan_domain = p_vlan_mapping_in->vlan_domain;
    }

    return CTC_E_NONE;

}

int32
_sys_goldengate_vlan_mapping_unmap_igs_action(uint8 lchip, ctc_vlan_mapping_t* p_vlan_mapping, sys_scl_action_t action)
{

    /*original vlan mapping action flag*/
    uint32 action_flag = 0;
    ctc_scl_igs_action_t* pia = NULL;

    uint8  is_svid_en     = 0;
    uint8  is_cvid_en     = 0;
    uint8  is_scos_en     = 0;
    uint8  is_ccos_en     = 0;
    uint8  is_logic_port_en     = 0;
    uint8  is_aps_en      = 0;
    uint8  is_fid_en      = 0;
    uint8  is_nh_en       = 0;
    uint8  is_vn_id_en        = 0;
    uint8  is_stats_en        = 0;
    uint8  is_stp_id_en        = 0;
    uint8 is_l2vpn_oam = 0;
    uint8 is_vlan_domain_en = 0;
    uint8  is_vrfid_en        = 0;
    uint32 vn_id = 0;

    SYS_VLAN_MAPPING_DEBUG_FUNC();

    action_flag = action.action_flag;
    pia = &(action.u.igs_action);
    p_vlan_mapping->action = action_flag;

    is_svid_en    = CTC_FLAG_ISSET(p_vlan_mapping->action, CTC_VLAN_MAPPING_OUTPUT_SVID);
    is_cvid_en    = CTC_FLAG_ISSET(p_vlan_mapping->action, CTC_VLAN_MAPPING_OUTPUT_CVID);
    is_scos_en    = CTC_FLAG_ISSET(p_vlan_mapping->action, CTC_VLAN_MAPPING_OUTPUT_SCOS);
    is_ccos_en    = CTC_FLAG_ISSET(p_vlan_mapping->action, CTC_VLAN_MAPPING_OUTPUT_CCOS);
    is_logic_port_en    = CTC_FLAG_ISSET(p_vlan_mapping->action, CTC_VLAN_MAPPING_OUTPUT_LOGIC_SRC_PORT);
    is_aps_en     = CTC_FLAG_ISSET(p_vlan_mapping->action, CTC_VLAN_MAPPING_OUTPUT_APS_SELECT);
    is_fid_en     = CTC_FLAG_ISSET(p_vlan_mapping->action, CTC_VLAN_MAPPING_OUTPUT_FID);
    is_nh_en      = CTC_FLAG_ISSET(p_vlan_mapping->action, CTC_VLAN_MAPPING_OUTPUT_NHID);
    is_vn_id_en   = CTC_FLAG_ISSET(p_vlan_mapping->action, CTC_VLAN_MAPPING_OUTPUT_VN_ID);
    is_stats_en       = CTC_FLAG_ISSET(p_vlan_mapping->action, CTC_VLAN_MAPPING_FLAG_STATS_EN);
    is_stp_id_en   = CTC_FLAG_ISSET(p_vlan_mapping->action, CTC_VLAN_MAPPING_OUTPUT_STP_ID);
    is_l2vpn_oam = CTC_FLAG_ISSET(p_vlan_mapping->action, CTC_VLAN_MAPPING_FLAG_L2VPN_OAM);
    is_vlan_domain_en = CTC_FLAG_ISSET(p_vlan_mapping->action, CTC_VLAN_MAPPING_OUTPUT_VLAN_DOMAIN);
    is_vrfid_en       = CTC_FLAG_ISSET(p_vlan_mapping->action, CTC_VLAN_MAPPING_OUTPUT_VRFID);

    p_vlan_mapping->stag_op = pia->vlan_edit.stag_op;
    p_vlan_mapping->svid_sl = pia->vlan_edit.svid_sl;
    p_vlan_mapping->scos_sl = pia->vlan_edit.scos_sl;

    p_vlan_mapping->ctag_op = pia->vlan_edit.ctag_op;
    p_vlan_mapping->cvid_sl = pia->vlan_edit.cvid_sl;
    p_vlan_mapping->ccos_sl = pia->vlan_edit.ccos_sl;
    p_vlan_mapping->tpid_index = pia->vlan_edit.tpid_index;

    if (is_svid_en)
    {
        p_vlan_mapping->new_svid = pia->vlan_edit.svid_new;
    }

    if (is_scos_en)
    {
        p_vlan_mapping->new_scos = pia->vlan_edit.scos_new;
    }

    if (is_cvid_en)
    {
        p_vlan_mapping->new_cvid = pia->vlan_edit.cvid_new;
    }

    if (is_ccos_en)
    {
        p_vlan_mapping->new_ccos = pia->vlan_edit.ccos_new;
    }

    if (is_logic_port_en)
    {
        p_vlan_mapping->logic_src_port = pia->logic_port.logic_port;
        p_vlan_mapping->logic_port_type = pia->logic_port.logic_port_type;
    }

    if (is_aps_en)
    {

        if (pia->aps.protected_vlan_valid)
        {
            p_vlan_mapping->protected_vlan = pia->aps.protected_vlan;
        }

        p_vlan_mapping->aps_select_group_id = pia->aps.aps_select_group_id;
        p_vlan_mapping->is_working_path = pia->aps.is_working_path;

        p_vlan_mapping->logic_src_port = pia->logic_port.logic_port;
    }

    if (pia->policer_id > 0)
    {
        p_vlan_mapping->policer_id = pia->policer_id;
    }

    if (is_fid_en)
    {
        p_vlan_mapping->u3.fid = pia->fid;
    }

    if (is_stp_id_en)
    {
        p_vlan_mapping->stp_id = pia->stp_id;
    }

    if (CTC_FLAG_ISSET(p_vlan_mapping->action, CTC_VLAN_MAPPING_OUTPUT_VLANPTR))
    {
        p_vlan_mapping->user_vlanptr = pia->user_vlanptr;
    }

    if (is_vn_id_en)
    {
        /*must can be unmapped*/
        CTC_ERROR_RETURN(sys_goldengate_vlan_overlay_get_vn_id(lchip, pia->fid, &vn_id));
        p_vlan_mapping->u3.vn_id = vn_id;
    }

    if (is_vrfid_en)
    {
        p_vlan_mapping->u3.vrf_id = pia->vrf_id;
    }

    if (CTC_QOS_COLOR_NONE != pia->color)
    {
        p_vlan_mapping->color = pia->color;
        p_vlan_mapping->priority = pia->priority;
    }

    if (p_vlan_mapping->action & CTC_VLAN_MAPPING_FLAG_VPLS)
    {
        p_vlan_mapping->logic_src_port = pia->logic_port.logic_port;
        if(SYS_SCL_BY_PASS_VLAN_PTR == pia->user_vlanptr)
        {
            /*to be check*/
            p_vlan_mapping->user_vlanptr = 0;
        }
    }

    /*for vpws*/
    if (is_nh_en)
    {
        p_vlan_mapping->u3.nh_id = pia->nh_id;
    }

    if (is_stats_en)
    {
        p_vlan_mapping->stats_id = pia->stats_id;
    }

    if (is_l2vpn_oam)
    {
        p_vlan_mapping->l2vpn_oam_id = pia->l2vpn_oam_id;
    }

    if (is_vlan_domain_en)
    {
        p_vlan_mapping->vlan_domain = pia->vlan_edit.vlan_domain;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_vlan_check_vlan_mapping_action(uint8 lchip, ctc_direction_t dir, sys_vlan_mapping_tag_op_t* p_vlan_mapping)
{
    switch (p_vlan_mapping->stag_op)
    {
    case CTC_VLAN_TAG_OP_NONE:
    case CTC_VLAN_TAG_OP_DEL:
        if ((CTC_VLAN_TAG_SL_AS_PARSE != p_vlan_mapping->svid_sl) || (CTC_VLAN_TAG_SL_AS_PARSE != p_vlan_mapping->scos_sl))
        {
            return CTC_E_VLAN_MAPPING_TAG_OP_NOT_SUPPORT;
        }

        break;

    default:
        break;
    }

    switch (p_vlan_mapping->ctag_op)
    {
    case CTC_VLAN_TAG_OP_NONE:
    case CTC_VLAN_TAG_OP_DEL:
        if ((CTC_VLAN_TAG_SL_AS_PARSE != p_vlan_mapping->cvid_sl) || (CTC_VLAN_TAG_SL_AS_PARSE != p_vlan_mapping->ccos_sl))
        {
            return CTC_E_VLAN_MAPPING_TAG_OP_NOT_SUPPORT;
        }

        break;

    default:
        break;
    }

    if (CTC_EGRESS == dir)
    {
        if ((CTC_VLAN_TAG_SL_DEFAULT == p_vlan_mapping->cvid_sl) || (CTC_VLAN_TAG_SL_DEFAULT == p_vlan_mapping->svid_sl))
        {
            return CTC_E_VLAN_MAPPING_TAG_OP_NOT_SUPPORT;
        }
    }

    return CTC_E_NONE;
}


STATIC uint32
_sys_goldengate_vlan_mapping_get_scl_gid(uint8 lchip, uint8 type, uint8 dir,uint16 lport, uint8 block_id)
{
    uint32 gid = 0;
    switch(type)
    {
    case SYS_SCL_KEY_TCAM_VLAN:
        gid = SYS_SCL_GROUP_ID_INNER_TCAM_VLAN_MAPPING_BASE + 2*lport + block_id;
        break;
    case SYS_SCL_KEY_HASH_PORT:
    case SYS_SCL_KEY_HASH_PORT_CVLAN:
    case SYS_SCL_KEY_HASH_PORT_SVLAN:
    case SYS_SCL_KEY_HASH_PORT_2VLAN:
    case SYS_SCL_KEY_HASH_PORT_CVLAN_COS:
    case SYS_SCL_KEY_HASH_PORT_SVLAN_COS:
    case SYS_SCL_KEY_HASH_PORT_MAC:
    case SYS_SCL_KEY_HASH_PORT_IPV4:
    case SYS_SCL_KEY_HASH_PORT_IPV6:
        if (CTC_INGRESS == dir)
        {
            gid = SYS_SCL_GROUP_ID_INNER_HASH_VLAN_MAPPING_IGS;
        }
        else
        {
            gid = SYS_SCL_GROUP_ID_INNER_HASH_VLAN_MAPPING_EGS;
        }
        break;
    default:    /* never */
        gid = 0;
        break;
    }

    return gid;;

}

STATIC int32
_sys_goldengate_vlan_mapping_set_igs_default_action(uint8 lchip, uint32 gport, ctc_vlan_mapping_t* p_vlan_mapping, uint8 is_add)
{

    sys_scl_action_t action;

    sal_memset(&action, 0, sizeof(sys_scl_action_t));

    if (CTC_FLAG_ISSET(p_vlan_mapping->flag, CTC_VLAN_MAPPING_FLAG_USE_LOGIC_PORT) || CTC_IS_LINKAGG_PORT(gport))
    {
        /* default entry do not support logic port and linkagg port */
        return CTC_E_NOT_SUPPORT;
    }

    action.type = CTC_SCL_ACTION_INGRESS;

    if (is_add)
    {
        CTC_ERROR_RETURN(_sys_goldengate_vlan_mapping_build_igs_ad(lchip, p_vlan_mapping, &action.u.igs_action));
    }

    CTC_ERROR_RETURN(sys_goldengate_scl_set_default_action(lchip, gport, &action));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_vlan_add_vlan_mapping(uint8 lchip, uint16 gport, ctc_vlan_mapping_t* p_vlan_mapping, bool overwrite)
{

    int32 ret = CTC_E_NONE;
    sys_scl_entry_t   scl;
    uint32  gid  = 0;
    sys_vlan_mapping_tag_op_t vlan_mapping_op;
    uint16 lport = 0;

    SYS_VLAN_MAPPING_DEBUG_FUNC();

    SYS_VLAN_MAPPING_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_vlan_mapping);

    if (!CTC_FLAG_ISSET(p_vlan_mapping->flag, CTC_VLAN_MAPPING_FLAG_USE_LOGIC_PORT))
    {
        CTC_GLOBAL_PORT_CHECK(gport);
        if (!CTC_IS_LINKAGG_PORT(gport))
        {
            ret = sys_goldengate_chip_is_local(lchip, SYS_MAP_CTC_GPORT_TO_GCHIP(gport));
            if (FALSE == ret)
            {
                return CTC_E_CHIP_IS_REMOTE;
            }
        }
    }
    CTC_MAX_VALUE_CHECK(p_vlan_mapping->tpid_index, 3);
    lport = CTC_MAP_GPORT_TO_LPORT(gport);

    SYS_VLAN_MAPPING_DEBUG_INFO("------vlan mapping----------\n");
    SYS_VLAN_MAPPING_DEBUG_INFO(" gport                     :0x%X\n", gport);
    SYS_VLAN_MAPPING_DEBUG_INFO(" key value                :0x%X\n", p_vlan_mapping->key);
    SYS_VLAN_MAPPING_DEBUG_INFO(" action                    :0x%X\n", p_vlan_mapping->action);
    SYS_VLAN_MAPPING_DEBUG_INFO(" flag                       :0x%X\n", p_vlan_mapping->flag);
    SYS_VLAN_MAPPING_DEBUG_INFO(" svid                       :%d\n", p_vlan_mapping->old_svid);
    SYS_VLAN_MAPPING_DEBUG_INFO(" cvid                       :%d\n", p_vlan_mapping->old_cvid);
    SYS_VLAN_MAPPING_DEBUG_INFO(" svlan end                :%d\n", p_vlan_mapping->svlan_end);
    SYS_VLAN_MAPPING_DEBUG_INFO(" cvlan end                :%d\n", p_vlan_mapping->cvlan_end);
    SYS_VLAN_MAPPING_DEBUG_INFO("-------mapping to-----------\n");
    SYS_VLAN_MAPPING_DEBUG_INFO(" new svid                  :%d\n", p_vlan_mapping->new_svid);
    SYS_VLAN_MAPPING_DEBUG_INFO(" new cvid                  :%d\n", p_vlan_mapping->new_cvid);
    SYS_VLAN_MAPPING_DEBUG_INFO(" fid                       :%d\n", p_vlan_mapping->u3.fid);
    if (CTC_FLAG_ISSET(p_vlan_mapping->action, CTC_VLAN_MAPPING_FLAG_L2VPN_OAM))
    {
        SYS_VLAN_MAPPING_DEBUG_INFO(" l2vpn oam id              :%d\n", p_vlan_mapping->l2vpn_oam_id);
    }
    vlan_mapping_op.ccos_sl = p_vlan_mapping->ccos_sl;
    vlan_mapping_op.cvid_sl = p_vlan_mapping->cvid_sl;
    vlan_mapping_op.ctag_op = p_vlan_mapping->ctag_op;
    vlan_mapping_op.scos_sl = p_vlan_mapping->scos_sl;
    vlan_mapping_op.svid_sl = p_vlan_mapping->svid_sl;
    vlan_mapping_op.stag_op = p_vlan_mapping->stag_op;
    CTC_ERROR_RETURN(_sys_goldengate_vlan_check_vlan_mapping_action(lchip, CTC_INGRESS, &vlan_mapping_op));

    if (CTC_FLAG_ISSET(p_vlan_mapping->flag, CTC_VLAN_MAPPING_FLAG_DEFAULT_ENTRY))
    {
        return _sys_goldengate_vlan_mapping_set_igs_default_action(lchip, gport, p_vlan_mapping, 1);
    }

    sal_memset(&scl, 0, sizeof(sys_scl_entry_t));

    CTC_ERROR_RETURN(_sys_goldengate_vlan_mapping_build_igs_key(lchip, gport, p_vlan_mapping, &scl));

    scl.action.type = SYS_SCL_ACTION_INGRESS;
    /*original action flag in vlan mapping struct*/
    scl.action.action_flag = p_vlan_mapping->action;

    if (!overwrite)
    {
        CTC_ERROR_RETURN(_sys_goldengate_vlan_mapping_build_igs_ad(lchip, p_vlan_mapping, &scl.action.u.igs_action));
        gid = _sys_goldengate_vlan_mapping_get_scl_gid(lchip, scl.key.type, CTC_INGRESS, lport, p_vlan_mapping->scl_id);
        if(gid == 0)
        {
            return CTC_E_SCL_GROUP_ID;
        }

        SYS_VLAN_MAPPING_DEBUG_INFO(" group id :%u\n", gid);

        if (gid >= SYS_SCL_GROUP_ID_INNER_TCAM_VLAN_MAPPING_BASE) /* is vlan mapping tcam group */
        {
           ctc_scl_group_info_t port_group;
           sal_memset(&port_group, 0, sizeof(port_group));
           port_group.type = CTC_SCL_GROUP_TYPE_PORT;
           port_group.priority = p_vlan_mapping->scl_id;
           port_group.un.gport = gport;
           ret = sys_goldengate_scl_create_group(lchip, gid, &port_group, 1);
           if ((ret < 0 )&& (ret != CTC_E_SCL_GROUP_EXIST))
           {
               return ret;
           }
        }
        CTC_ERROR_GOTO(sys_goldengate_scl_add_entry(lchip, gid, &scl, 1), ret, ERROR1);
        CTC_ERROR_GOTO(sys_goldengate_scl_install_entry(lchip, scl.entry_id, 1), ret, ERROR2);
    }
    else
    {
        CTC_ERROR_RETURN(_sys_goldengate_vlan_mapping_build_igs_ad(lchip, p_vlan_mapping, &scl.action.u.igs_action));
        gid = _sys_goldengate_vlan_mapping_get_scl_gid(lchip, scl.key.type, CTC_INGRESS, lport, p_vlan_mapping->scl_id);
        if(gid == 0)
        {
            return CTC_E_SCL_GROUP_ID;
        }

        SYS_VLAN_MAPPING_DEBUG_INFO(" group id :%u\n", gid);

        /* get entry id.*/
        CTC_ERROR_RETURN(sys_goldengate_scl_get_entry_id(lchip, &scl, gid));

        /* update action */
        CTC_ERROR_RETURN(sys_goldengate_scl_update_action(lchip, scl.entry_id, &scl.action, 1));
    }


    return CTC_E_NONE;

ERROR2:
    sys_goldengate_scl_remove_entry(lchip, scl.entry_id, 1);

ERROR1:
    if (gid >= SYS_SCL_GROUP_ID_INNER_TCAM_VLAN_MAPPING_BASE)
    {
        /* is vlan mapping tcam group */
        sys_goldengate_scl_destroy_group(lchip, gid, 1);
    }

    return ret;

}

int32
sys_goldengate_vlan_add_vlan_mapping(uint8 lchip, uint16 gport, ctc_vlan_mapping_t* p_vlan_mapping)
{
    CTC_ERROR_RETURN(_sys_goldengate_vlan_add_vlan_mapping(lchip, gport, p_vlan_mapping, FALSE));
    return CTC_E_NONE;
}

int32
sys_goldengate_vlan_get_vlan_mapping(uint8 lchip, uint16 gport, ctc_vlan_mapping_t* p_vlan_mapping)
{
    int32 ret = CTC_E_NONE;
    sys_scl_entry_t   scl;
    uint32  gid  = 0;
    uint16 lport = 0;

    SYS_VLAN_MAPPING_DEBUG_FUNC();

    SYS_VLAN_MAPPING_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_vlan_mapping);

    if (!CTC_FLAG_ISSET(p_vlan_mapping->flag, CTC_VLAN_MAPPING_FLAG_USE_LOGIC_PORT))
    {
        CTC_GLOBAL_PORT_CHECK(gport);
        if (!CTC_IS_LINKAGG_PORT(gport))
        {
            ret = sys_goldengate_chip_is_local(lchip, SYS_MAP_CTC_GPORT_TO_GCHIP(gport));
            if (FALSE == ret)
            {
                return CTC_E_CHIP_IS_REMOTE;
            }
        }
    }

    lport = CTC_MAP_GPORT_TO_LPORT(gport);

    SYS_VLAN_MAPPING_DEBUG_INFO("------vlan mapping----------\n");
    SYS_VLAN_MAPPING_DEBUG_INFO(" gport                     :0x%X\n", gport);
    SYS_VLAN_MAPPING_DEBUG_INFO(" key value                :0x%X\n", p_vlan_mapping->key);
    SYS_VLAN_MAPPING_DEBUG_INFO(" action                    :0x%X\n", p_vlan_mapping->action);
    SYS_VLAN_MAPPING_DEBUG_INFO(" flag                       :0x%X\n", p_vlan_mapping->flag);
    SYS_VLAN_MAPPING_DEBUG_INFO(" svid                       :%d\n", p_vlan_mapping->old_svid);
    SYS_VLAN_MAPPING_DEBUG_INFO(" cvid                       :%d\n", p_vlan_mapping->old_cvid);
    SYS_VLAN_MAPPING_DEBUG_INFO(" svlan end                :%d\n", p_vlan_mapping->svlan_end);
    SYS_VLAN_MAPPING_DEBUG_INFO(" cvlan end                :%d\n", p_vlan_mapping->cvlan_end);


    sal_memset(&scl, 0, sizeof(sys_scl_entry_t));

    CTC_ERROR_RETURN(_sys_goldengate_vlan_mapping_build_igs_key(lchip, gport, p_vlan_mapping, &scl));

    gid = _sys_goldengate_vlan_mapping_get_scl_gid(lchip, scl.key.type, CTC_INGRESS, lport, p_vlan_mapping->scl_id);
    if(gid < 0)
    {
        return gid;
    }

    SYS_VLAN_MAPPING_DEBUG_INFO(" group id :%u\n", gid);

    /* get scl entry*/
    CTC_ERROR_RETURN(sys_goldengate_scl_get_entry(lchip, &scl, gid));

    CTC_ERROR_RETURN(_sys_goldengate_vlan_mapping_unmap_igs_action(lchip, p_vlan_mapping, scl.action));

    return CTC_E_NONE;
}

int32
sys_goldengate_vlan_update_vlan_mapping(uint8 lchip, uint16 gport, ctc_vlan_mapping_t* p_vlan_mapping)
{
    CTC_ERROR_RETURN(_sys_goldengate_vlan_add_vlan_mapping(lchip, gport, p_vlan_mapping, TRUE));
    return CTC_E_NONE;
}

int32
sys_goldengate_vlan_remove_vlan_mapping(uint8 lchip, uint16 gport, ctc_vlan_mapping_t* p_vlan_mapping)
{
    sys_scl_entry_t   scl;
    uint32            gid = 0;
    int32             ret = CTC_E_NONE;
    uint16            lport = 0;

    SYS_VLAN_MAPPING_DEBUG_FUNC();
    SYS_VLAN_MAPPING_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_vlan_mapping);

    if (!CTC_FLAG_ISSET(p_vlan_mapping->flag, CTC_VLAN_MAPPING_FLAG_USE_LOGIC_PORT))
    {
        CTC_GLOBAL_PORT_CHECK(gport);
        if (!CTC_IS_LINKAGG_PORT(gport))
        {
            ret = sys_goldengate_chip_is_local(lchip, SYS_MAP_CTC_GPORT_TO_GCHIP(gport));
            if (FALSE == ret)
            {
                return CTC_E_CHIP_IS_REMOTE;
            }
        }
    }

    lport = CTC_MAP_GPORT_TO_LPORT(gport);

    SYS_VLAN_MAPPING_DEBUG_FUNC();
    SYS_VLAN_MAPPING_DEBUG_INFO("------vlan mapping----------\n");
    SYS_VLAN_MAPPING_DEBUG_INFO(" gport                      :0x%X\n", gport);
    SYS_VLAN_MAPPING_DEBUG_INFO(" key value                :0x%X\n", p_vlan_mapping->key);
    SYS_VLAN_MAPPING_DEBUG_INFO(" flag                        :0x%X\n", p_vlan_mapping->flag);
    SYS_VLAN_MAPPING_DEBUG_INFO(" svid                        :%d\n", p_vlan_mapping->old_svid);
    SYS_VLAN_MAPPING_DEBUG_INFO(" cvid                        :%d\n", p_vlan_mapping->old_cvid);
    SYS_VLAN_MAPPING_DEBUG_INFO(" svlan end                 :%d\n", p_vlan_mapping->svlan_end);
    SYS_VLAN_MAPPING_DEBUG_INFO(" cvlan end                 :%d\n", p_vlan_mapping->cvlan_end);

    if (CTC_FLAG_ISSET(p_vlan_mapping->flag, CTC_VLAN_MAPPING_FLAG_DEFAULT_ENTRY))
    {
        return _sys_goldengate_vlan_mapping_set_igs_default_action(lchip, gport, p_vlan_mapping, 0);
    }
    sal_memset(&scl, 0, sizeof(sys_scl_entry_t));
    CTC_ERROR_RETURN(_sys_goldengate_vlan_mapping_build_igs_key(lchip, gport, p_vlan_mapping, &scl));


    gid = _sys_goldengate_vlan_mapping_get_scl_gid(lchip, scl.key.type, CTC_INGRESS, lport, p_vlan_mapping->scl_id);
    if(gid < 0)
    {
        return gid;
    }

    CTC_ERROR_RETURN(sys_goldengate_scl_get_entry_id(lchip, &scl, gid));
    CTC_ERROR_RETURN(sys_goldengate_scl_uninstall_entry(lchip, scl.entry_id, 1));
    CTC_ERROR_RETURN(sys_goldengate_scl_remove_entry(lchip, scl.entry_id, 1));

    return CTC_E_NONE;

}

int32
sys_goldengate_vlan_add_default_vlan_mapping(uint8 lchip, uint16 gport, ctc_vlan_miss_t* p_def_action)
{
    uint16 lport = 0;
    sys_scl_action_t action;

    SYS_VLAN_MAPPING_DEBUG_FUNC();
    SYS_VLAN_MAPPING_DEBUG_INFO("------vlan mapping----------\n");
    SYS_VLAN_MAPPING_DEBUG_INFO(" gport                      :%d\n", gport);
    SYS_VLAN_MAPPING_DEBUG_INFO(" flag value                 :%d\n", p_def_action->flag);

    SYS_VLAN_MAPPING_INIT_CHECK(lchip);
    SYS_VLAN_MAPPING_DEBUG_FUNC();

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);
    CTC_PTR_VALID_CHECK(p_def_action);

    sal_memset(&action, 0, sizeof(action));


    switch (p_def_action->flag)
    {
    case CTC_VLAN_MISS_ACTION_DO_NOTHING:
        break;

    case CTC_VLAN_MISS_ACTION_TO_CPU:
        CTC_SET_FLAG(action.u.igs_action.flag, CTC_SCL_IGS_ACTION_FLAG_COPY_TO_CPU);
        CTC_SET_FLAG(action.u.igs_action.flag, CTC_SCL_IGS_ACTION_FLAG_DISCARD);
        break;

    case CTC_VLAN_MISS_ACTION_DISCARD:
        CTC_SET_FLAG(action.u.igs_action.flag , CTC_SCL_IGS_ACTION_FLAG_DISCARD);
        break;

    case CTC_VLAN_MISS_ACTION_OUTPUT_VLANPTR:
        CTC_SET_FLAG(action.u.igs_action.flag , CTC_SCL_IGS_ACTION_FLAG_USER_VLANPTR);
        CTC_MAX_VALUE_CHECK(p_def_action->user_vlanptr,0x1fff);
        action.u.igs_action.user_vlanptr     = p_def_action->user_vlanptr;
        break;

    case CTC_VLAN_MISS_ACTION_APPEND_STAG:
        SYS_VLAN_ID_VALID_CHECK(p_def_action->new_svid);
        CTC_COS_RANGE_CHECK(p_def_action->new_scos);
        CTC_SET_FLAG(action.u.igs_action.flag , CTC_SCL_IGS_ACTION_FLAG_VLAN_EDIT);
        action.u.igs_action.vlan_edit.svid_new  =  p_def_action->new_svid;
        action.u.igs_action.vlan_edit.stag_op   =  CTC_VLAN_TAG_OP_ADD;
        action.u.igs_action.vlan_edit.svid_sl   =  CTC_VLAN_TAG_SL_NEW;
        action.u.igs_action.vlan_edit.scos_new =  p_def_action->new_scos;
        action.u.igs_action.vlan_edit.scos_sl =  p_def_action->scos_sl;
        break;

    default:
        return CTC_E_INVALID_PARAM;

    }
    action.type = CTC_SCL_ACTION_INGRESS;

    CTC_ERROR_RETURN(sys_goldengate_scl_set_default_action(lchip, gport, &action));
    return CTC_E_NONE;
}


int32
sys_goldengate_vlan_show_default_vlan_mapping(uint8 lchip, uint16 gport)
{

    uint16 lport = 0;
    sys_scl_action_t action;
    char str[35];
    char format[10];

    SYS_VLAN_MAPPING_INIT_CHECK(lchip);
    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);


    sal_memset(&action, 0, sizeof(action));
    action.type = CTC_SCL_ACTION_INGRESS;

    CTC_ERROR_RETURN(sys_goldengate_scl_get_default_action(lchip, gport, &action));

    SYS_VLAN_MAPPING_DEBUG_DUMP("GPort %s default Ingress action :  ", CTC_DEBUG_HEX_FORMAT(str, format, gport, 4, U));
    if (CTC_FLAG_ISSET(action.u.igs_action.flag, CTC_SCL_IGS_ACTION_FLAG_COPY_TO_CPU))
    {
        SYS_VLAN_MAPPING_DEBUG_DUMP("TO_CPU\n");
    }
    else if (CTC_FLAG_ISSET(action.u.igs_action.flag, CTC_SCL_IGS_ACTION_FLAG_DISCARD))
    {
        SYS_VLAN_MAPPING_DEBUG_DUMP("DISCARD\n");
    }
    else if (CTC_FLAG_ISSET(action.u.igs_action.flag, CTC_SCL_IGS_ACTION_FLAG_USER_VLANPTR))
    {
        SYS_VLAN_MAPPING_DEBUG_DUMP("USER_VLANPTR\n");
    }
    else if (CTC_FLAG_ISSET(action.u.igs_action.flag, CTC_SCL_IGS_ACTION_FLAG_VLAN_EDIT))
    {
        SYS_VLAN_MAPPING_DEBUG_DUMP("APPEND\n");
    }
    else
    {
        SYS_VLAN_MAPPING_DEBUG_DUMP("FWD\n");
    }

    sal_memset(&action, 0, sizeof(action));
    action.type = CTC_SCL_ACTION_EGRESS;

    CTC_ERROR_RETURN(sys_goldengate_scl_get_default_action(lchip, gport, &action));

    SYS_VLAN_MAPPING_DEBUG_DUMP("GPort %s default Egress action  :  ", CTC_DEBUG_HEX_FORMAT(str, format, gport, 4, U));
    if (CTC_FLAG_ISSET(action.u.egs_action.flag, CTC_SCL_EGS_ACTION_FLAG_DISCARD))
    {
        SYS_VLAN_MAPPING_DEBUG_DUMP("DISCARD\n");
    }
    else
    {
        SYS_VLAN_MAPPING_DEBUG_DUMP("FWD\n");
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_vlan_remove_default_vlan_mapping(uint8 lchip, uint16 gport)
{

    uint16 lport = 0;
    sys_scl_action_t action;

    SYS_VLAN_MAPPING_INIT_CHECK(lchip);
    SYS_VLAN_MAPPING_DEBUG_FUNC();

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);

    sal_memset(&action, 0, sizeof(action));
    action.type = CTC_SCL_ACTION_INGRESS;

    CTC_ERROR_RETURN(sys_goldengate_scl_set_default_action(lchip, gport, &action));
    return CTC_E_NONE;


}

int32
sys_goldengate_vlan_remove_all_vlan_mapping_by_port(uint8 lchip, uint16 gport)
{

    int32 ret = CTC_E_NONE;

    SYS_VLAN_MAPPING_INIT_CHECK(lchip);
    SYS_VLAN_MAPPING_DEBUG_FUNC();
    CTC_GLOBAL_PORT_CHECK(gport);
    if (!CTC_IS_LINKAGG_PORT(gport))
    {
        ret = sys_goldengate_chip_is_local(lchip, SYS_MAP_CTC_GPORT_TO_GCHIP(gport));
        if (FALSE == ret)
        {
            return CTC_E_CHIP_IS_REMOTE;
        }
    }

    CTC_ERROR_RETURN(sys_goldengate_scl_remove_all_inner_hash_vlan(lchip, gport, CTC_INGRESS));

    return CTC_E_NONE;
}

#define EGRESS_VLAN_MAPPING

STATIC int32
_sys_goldengate_vlan_mapping_build_egs_ad(uint8 lchip, ctc_egress_vlan_mapping_t* p_vlan_mapping_in,
                                         ctc_scl_egs_action_t* pea)
{

    uint8  is_svid_en     = 0;
    uint8  is_cvid_en     = 0;
    uint8  is_scos_en     = 0;
    uint8  is_ccos_en     = 0;
    uint8  is_scfi_en     = 0;

    SYS_VLAN_MAPPING_DEBUG_FUNC();

    if ((p_vlan_mapping_in->stag_op >= CTC_VLAN_TAG_OP_MAX)
        || (p_vlan_mapping_in->svid_sl >= CTC_VLAN_TAG_SL_MAX)
        || (p_vlan_mapping_in->scos_sl >= CTC_VLAN_TAG_SL_MAX)
        || (p_vlan_mapping_in->scfi_sl >= CTC_VLAN_TAG_SL_MAX)
        || (p_vlan_mapping_in->ctag_op >= CTC_VLAN_TAG_OP_MAX)
        || (p_vlan_mapping_in->cvid_sl >= CTC_VLAN_TAG_SL_MAX)
        || (p_vlan_mapping_in->ccos_sl >= CTC_VLAN_TAG_SL_MAX))
    {
        return CTC_E_INVALID_PARAM;
    }

    is_svid_en    = CTC_FLAG_ISSET(p_vlan_mapping_in->action, CTC_EGRESS_VLAN_MAPPING_OUTPUT_SVID);
    is_cvid_en    = CTC_FLAG_ISSET(p_vlan_mapping_in->action, CTC_EGRESS_VLAN_MAPPING_OUTPUT_CVID);
    is_scos_en    = CTC_FLAG_ISSET(p_vlan_mapping_in->action, CTC_EGRESS_VLAN_MAPPING_OUTPUT_SCOS);
    is_ccos_en    = CTC_FLAG_ISSET(p_vlan_mapping_in->action, CTC_EGRESS_VLAN_MAPPING_OUTPUT_CCOS);
    is_scfi_en    = CTC_FLAG_ISSET(p_vlan_mapping_in->action, CTC_EGRESS_VLAN_MAPPING_OUTPUT_SCFI);

    if (p_vlan_mapping_in->stag_op || p_vlan_mapping_in->ctag_op)
    {
        CTC_SET_FLAG(pea->flag, CTC_SCL_EGS_ACTION_FLAG_VLAN_EDIT);
    }

    if ((p_vlan_mapping_in->color != 0 ) || (p_vlan_mapping_in->priority != 0))
    {
        return CTC_E_NOT_SUPPORT;
    }

    pea->vlan_edit.stag_op    = p_vlan_mapping_in->stag_op;
    pea->vlan_edit.svid_sl    = p_vlan_mapping_in->svid_sl;
    pea->vlan_edit.scos_sl    = p_vlan_mapping_in->scos_sl;
    pea->vlan_edit.scfi_sl    = p_vlan_mapping_in->scfi_sl;

    pea->vlan_edit.ctag_op    = p_vlan_mapping_in->ctag_op;
    pea->vlan_edit.cvid_sl    = p_vlan_mapping_in->cvid_sl;
    pea->vlan_edit.ccos_sl    = p_vlan_mapping_in->ccos_sl;
    pea->vlan_edit.tpid_index = p_vlan_mapping_in->tpid_index;
    if (is_svid_en)
    {
        SYS_VLAN_ID_VALID_CHECK(p_vlan_mapping_in->new_svid);
        pea->vlan_edit.svid_new = p_vlan_mapping_in->new_svid;
    }

    if (is_scos_en)
    {
        CTC_COS_RANGE_CHECK(p_vlan_mapping_in->new_scos);
        pea->vlan_edit.scos_new = p_vlan_mapping_in->new_scos;
    }

    if (is_scfi_en)
    {
        CTC_MAX_VALUE_CHECK(p_vlan_mapping_in->new_scfi, 1);
        pea->vlan_edit.scfi_new = p_vlan_mapping_in->new_scfi;
    }

    if (is_cvid_en)
    {
        SYS_VLAN_ID_VALID_CHECK(p_vlan_mapping_in->new_cvid);
        pea->vlan_edit.cvid_new = p_vlan_mapping_in->new_cvid;
    }

    if (is_ccos_en)
    {
        CTC_COS_RANGE_CHECK(p_vlan_mapping_in->new_ccos);
        pea->vlan_edit.ccos_new = p_vlan_mapping_in->new_ccos;
    }

    return CTC_E_NONE;

}


STATIC int32
_sys_goldengate_vlan_mapping_build_egs_key(uint8 lchip, uint16 gport,
                                          ctc_egress_vlan_mapping_t* p_vlan_mapping_in,
                                          sys_scl_entry_t*   pc)
{
    uint16 old_cvid = 0;
    uint16 old_svid = 0;
    ctc_vlan_range_info_t vrange_info;
    ctc_vlan_range_t vlan_range;

    sal_memset(&vrange_info, 0, sizeof(ctc_vlan_range_info_t));
    sal_memset(&vlan_range, 0, sizeof(ctc_vlan_range_t));

    SYS_VLAN_MAPPING_DEBUG_FUNC();

    switch (p_vlan_mapping_in->key
            & (CTC_EGRESS_VLAN_MAPPING_KEY_CVID | CTC_EGRESS_VLAN_MAPPING_KEY_SVID
               | CTC_EGRESS_VLAN_MAPPING_KEY_CTAG_COS | CTC_EGRESS_VLAN_MAPPING_KEY_STAG_COS))
    {
    case (CTC_EGRESS_VLAN_MAPPING_KEY_CVID):
        SYS_VLAN_ID_VALID_CHECK(p_vlan_mapping_in->old_cvid);
        old_cvid = p_vlan_mapping_in->old_cvid;

        vrange_info.direction = CTC_EGRESS;
        vrange_info.vrange_grpid = p_vlan_mapping_in->vrange_grpid;
        vlan_range.is_acl = 0;
        vlan_range.vlan_start = old_cvid;
        vlan_range.vlan_end = p_vlan_mapping_in->cvlan_end;
        CTC_ERROR_RETURN(sys_goldengate_vlan_get_maxvid_from_vrange(lchip, &vrange_info, &vlan_range, FALSE, &old_cvid));

        pc->key.u.hash_port_cvlan_key.gport = gport;
        pc->key.u.hash_port_cvlan_key.cvlan = old_cvid;
        pc->key.u.hash_port_cvlan_key.dir = CTC_EGRESS;
        pc->key.type  = SYS_SCL_KEY_HASH_PORT_CVLAN;

        if (CTC_FLAG_ISSET(p_vlan_mapping_in->flag, CTC_VLAN_MAPPING_FLAG_USE_LOGIC_PORT))
        {
            pc->key.u.hash_port_cvlan_key.gport_type = CTC_SCL_GPROT_TYPE_LOGIC_PORT;
        }
        break;

    case (CTC_EGRESS_VLAN_MAPPING_KEY_SVID):
        SYS_VLAN_ID_VALID_CHECK(p_vlan_mapping_in->old_svid);
        old_svid = p_vlan_mapping_in->old_svid;

        vrange_info.direction = CTC_EGRESS;
        vrange_info.vrange_grpid = p_vlan_mapping_in->vrange_grpid;
        vlan_range.is_acl = 0;
        vlan_range.vlan_start = old_svid;
        vlan_range.vlan_end = p_vlan_mapping_in->svlan_end;
        CTC_ERROR_RETURN(sys_goldengate_vlan_get_maxvid_from_vrange(lchip, &vrange_info, &vlan_range, TRUE, &old_svid));

        pc->key.u.hash_port_svlan_key.gport = gport;
        pc->key.u.hash_port_svlan_key.svlan = old_svid;
        pc->key.u.hash_port_svlan_key.dir = CTC_EGRESS;
        pc->key.type  = SYS_SCL_KEY_HASH_PORT_SVLAN;

        if (CTC_FLAG_ISSET(p_vlan_mapping_in->flag, CTC_VLAN_MAPPING_FLAG_USE_LOGIC_PORT))
        {
            pc->key.u.hash_port_svlan_key.gport_type = CTC_SCL_GPROT_TYPE_LOGIC_PORT;
        }
        break;

    case (CTC_EGRESS_VLAN_MAPPING_KEY_CVID | CTC_EGRESS_VLAN_MAPPING_KEY_SVID):
        SYS_VLAN_ID_VALID_CHECK(p_vlan_mapping_in->old_cvid);
        SYS_VLAN_ID_VALID_CHECK(p_vlan_mapping_in->old_svid);
        old_cvid = p_vlan_mapping_in->old_cvid;
        old_svid = p_vlan_mapping_in->old_svid;

        vrange_info.direction = CTC_EGRESS;
        vrange_info.vrange_grpid = p_vlan_mapping_in->vrange_grpid;
        vlan_range.is_acl = 0;
        vlan_range.vlan_start = old_cvid;
        vlan_range.vlan_end = p_vlan_mapping_in->cvlan_end;
        CTC_ERROR_RETURN(sys_goldengate_vlan_get_maxvid_from_vrange(lchip, &vrange_info, &vlan_range, FALSE, &old_cvid));

        vrange_info.direction = CTC_EGRESS;
        vrange_info.vrange_grpid = p_vlan_mapping_in->vrange_grpid;
        vlan_range.is_acl = 0;
        vlan_range.vlan_start = old_svid;
        vlan_range.vlan_end = p_vlan_mapping_in->svlan_end;
        CTC_ERROR_RETURN(sys_goldengate_vlan_get_maxvid_from_vrange(lchip, &vrange_info, &vlan_range, TRUE, &old_svid));

        pc->key.u.hash_port_2vlan_key.gport = gport;
        pc->key.u.hash_port_2vlan_key.cvlan  = old_cvid;
        pc->key.u.hash_port_2vlan_key.svlan  = old_svid;
        pc->key.u.hash_port_2vlan_key.dir = CTC_EGRESS;

        pc->key.type  = SYS_SCL_KEY_HASH_PORT_2VLAN;

        if (CTC_FLAG_ISSET(p_vlan_mapping_in->flag, CTC_VLAN_MAPPING_FLAG_USE_LOGIC_PORT))
        {
            pc->key.u.hash_port_2vlan_key.gport_type = CTC_SCL_GPROT_TYPE_LOGIC_PORT;
        }
        break;

    case (CTC_EGRESS_VLAN_MAPPING_KEY_CVID | CTC_EGRESS_VLAN_MAPPING_KEY_CTAG_COS):
        SYS_VLAN_ID_VALID_CHECK(p_vlan_mapping_in->old_cvid);
        CTC_COS_RANGE_CHECK(p_vlan_mapping_in->old_ccos);
        old_cvid = p_vlan_mapping_in->old_cvid;

        vrange_info.direction = CTC_EGRESS;
        vrange_info.vrange_grpid = p_vlan_mapping_in->vrange_grpid;
        vlan_range.is_acl = 0;
        vlan_range.vlan_start = old_cvid;
        vlan_range.vlan_end = p_vlan_mapping_in->cvlan_end;
        CTC_ERROR_RETURN(sys_goldengate_vlan_get_maxvid_from_vrange(lchip, &vrange_info, &vlan_range, FALSE, &old_cvid));

        pc->key.u.hash_port_cvlan_cos_key.gport = gport;
        pc->key.u.hash_port_cvlan_cos_key.cvlan = old_cvid;
        pc->key.u.hash_port_cvlan_cos_key.ctag_cos= p_vlan_mapping_in->old_ccos;
        pc->key.u.hash_port_cvlan_cos_key.dir = CTC_EGRESS;

        pc->key.type  = SYS_SCL_KEY_HASH_PORT_CVLAN_COS;

        if (CTC_FLAG_ISSET(p_vlan_mapping_in->flag, CTC_VLAN_MAPPING_FLAG_USE_LOGIC_PORT))
        {
            pc->key.u.hash_port_cvlan_cos_key.gport_type = CTC_SCL_GPROT_TYPE_LOGIC_PORT;
        }
        break;

    case (CTC_EGRESS_VLAN_MAPPING_KEY_SVID | CTC_EGRESS_VLAN_MAPPING_KEY_STAG_COS):
        SYS_VLAN_ID_VALID_CHECK(p_vlan_mapping_in->old_svid);
        CTC_COS_RANGE_CHECK(p_vlan_mapping_in->old_scos);
        old_svid = p_vlan_mapping_in->old_svid;

        vrange_info.direction = CTC_EGRESS;
        vrange_info.vrange_grpid = p_vlan_mapping_in->vrange_grpid;
        vlan_range.is_acl = 0;
        vlan_range.vlan_start = old_svid;
        vlan_range.vlan_end = p_vlan_mapping_in->svlan_end;
        CTC_ERROR_RETURN(sys_goldengate_vlan_get_maxvid_from_vrange(lchip, &vrange_info, &vlan_range, TRUE, &old_svid));

        pc->key.u.hash_port_svlan_cos_key.gport = gport;
        pc->key.u.hash_port_svlan_cos_key.svlan = old_svid;
        pc->key.u.hash_port_svlan_cos_key.stag_cos= p_vlan_mapping_in->old_scos;
        pc->key.u.hash_port_svlan_cos_key.dir = CTC_EGRESS;

        pc->key.type  = SYS_SCL_KEY_HASH_PORT_SVLAN_COS;

        if (CTC_FLAG_ISSET(p_vlan_mapping_in->flag, CTC_VLAN_MAPPING_FLAG_USE_LOGIC_PORT))
        {
            pc->key.u.hash_port_svlan_cos_key.gport_type = CTC_SCL_GPROT_TYPE_LOGIC_PORT;
        }
        break;

    case CTC_EGRESS_VLAN_MAPPING_KEY_NONE:
        pc->key.u.hash_port_key.gport = gport;

        pc->key.type  = SYS_SCL_KEY_HASH_PORT;
        pc->key.u.hash_port_key.dir = CTC_EGRESS;

        if (CTC_FLAG_ISSET(p_vlan_mapping_in->flag, CTC_VLAN_MAPPING_FLAG_USE_LOGIC_PORT))
        {
            pc->key.u.hash_port_key.gport_type = CTC_SCL_GPROT_TYPE_LOGIC_PORT;
        }
        break;

    default:
        {
            return CTC_E_INVALID_PARAM;
        }
    }

    return CTC_E_NONE;

}

STATIC int32
_sys_goldengate_vlan_mapping_unmap_egs_action(uint8 lchip, ctc_egress_vlan_mapping_t* p_vlan_mapping_in,
                                                            sys_scl_action_t* action)
{
    /*original vlan mapping action flag*/
    uint32 action_flag = 0;
    ctc_scl_egs_action_t* pea = NULL;

    uint8  is_svid_en     = 0;
    uint8  is_cvid_en     = 0;
    uint8  is_scos_en     = 0;
    uint8  is_ccos_en     = 0;
    uint8  is_scfi_en     = 0;

    SYS_VLAN_MAPPING_DEBUG_FUNC();

    action_flag = action->action_flag;
    pea = &(action->u.egs_action);
    p_vlan_mapping_in->action = action_flag;

    is_svid_en    = CTC_FLAG_ISSET(p_vlan_mapping_in->action, CTC_EGRESS_VLAN_MAPPING_OUTPUT_SVID);
    is_cvid_en    = CTC_FLAG_ISSET(p_vlan_mapping_in->action, CTC_EGRESS_VLAN_MAPPING_OUTPUT_CVID);
    is_scos_en    = CTC_FLAG_ISSET(p_vlan_mapping_in->action, CTC_EGRESS_VLAN_MAPPING_OUTPUT_SCOS);
    is_ccos_en    = CTC_FLAG_ISSET(p_vlan_mapping_in->action, CTC_EGRESS_VLAN_MAPPING_OUTPUT_CCOS);
    is_scfi_en    = CTC_FLAG_ISSET(p_vlan_mapping_in->action, CTC_EGRESS_VLAN_MAPPING_OUTPUT_SCFI);

    p_vlan_mapping_in->stag_op = pea->vlan_edit.stag_op;
    p_vlan_mapping_in->svid_sl = pea->vlan_edit.svid_sl;
    p_vlan_mapping_in->scos_sl = pea->vlan_edit.scos_sl;
    p_vlan_mapping_in->scfi_sl = pea->vlan_edit.scfi_sl;

    p_vlan_mapping_in->ctag_op = pea->vlan_edit.ctag_op;
    p_vlan_mapping_in->cvid_sl = pea->vlan_edit.cvid_sl;
    p_vlan_mapping_in->ccos_sl = pea->vlan_edit.ccos_sl;
    p_vlan_mapping_in->tpid_index = pea->vlan_edit.tpid_index;

    if (is_svid_en)
    {
        p_vlan_mapping_in->new_svid = pea->vlan_edit.svid_new;
    }

    if (is_scos_en)
    {
        p_vlan_mapping_in->new_scos = pea->vlan_edit.scos_new;
    }

    if (is_scfi_en)
    {
        p_vlan_mapping_in->new_scfi = pea->vlan_edit.scfi_new;
    }

    if (is_cvid_en)
    {
        p_vlan_mapping_in->new_cvid = pea->vlan_edit.cvid_new;
    }

    if (is_ccos_en)
    {
        p_vlan_mapping_in->new_ccos = pea->vlan_edit.ccos_new;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_vlan_mapping_set_egs_default_action(uint8 lchip, uint32 gport, ctc_egress_vlan_mapping_t* p_vlan_mapping, uint8 is_add)
{
    sys_scl_action_t action;

    sal_memset(&action, 0, sizeof(sys_scl_action_t));

    if (CTC_FLAG_ISSET(p_vlan_mapping->flag, CTC_VLAN_MAPPING_FLAG_USE_LOGIC_PORT) || CTC_IS_LINKAGG_PORT(gport))
    {
        /* default entry do not support logic port and linkagg port */
        return CTC_E_NOT_SUPPORT;
    }

    action.type = CTC_SCL_ACTION_EGRESS;

    if (is_add)
    {
        CTC_ERROR_RETURN(_sys_goldengate_vlan_mapping_build_egs_ad(lchip, p_vlan_mapping, &action.u.egs_action));
    }

    CTC_ERROR_RETURN(sys_goldengate_scl_set_default_action(lchip, gport, &action));

    return CTC_E_NONE;
}

int32
sys_goldengate_vlan_add_egress_vlan_mapping(uint8 lchip, uint16 gport, ctc_egress_vlan_mapping_t* p_vlan_mapping)
{
    int32             ret = CTC_E_NONE;
    uint32            gid = 0;
    sys_scl_entry_t   scl;
    sys_vlan_mapping_tag_op_t vlan_mapping_op;
    uint16 lport = 0;

    SYS_VLAN_MAPPING_DEBUG_FUNC();

    SYS_VLAN_MAPPING_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_vlan_mapping);
    CTC_MAX_VALUE_CHECK(p_vlan_mapping->tpid_index, 3);
    if (!CTC_FLAG_ISSET(p_vlan_mapping->flag, CTC_VLAN_MAPPING_FLAG_USE_LOGIC_PORT))
    {
        CTC_GLOBAL_PORT_CHECK(gport);
        if (!CTC_IS_LINKAGG_PORT(gport))
        {
            ret = sys_goldengate_chip_is_local(lchip, SYS_MAP_CTC_GPORT_TO_GCHIP(gport));
            if (FALSE == ret)
            {
                return CTC_E_CHIP_IS_REMOTE;
            }
        }
    }

    lport = CTC_MAP_GPORT_TO_LPORT(gport);
    SYS_VLAN_MAPPING_DEBUG_FUNC();
    SYS_VLAN_MAPPING_DEBUG_INFO("------vlan mapping----------\n");
    SYS_VLAN_MAPPING_DEBUG_INFO(" gport                     :0x%X\n", gport);
    SYS_VLAN_MAPPING_DEBUG_INFO(" key value                :0x%X\n", p_vlan_mapping->key);
    SYS_VLAN_MAPPING_DEBUG_INFO(" action                    :0x%X\n", p_vlan_mapping->action);
    SYS_VLAN_MAPPING_DEBUG_INFO(" flag                       :0x%X\n", p_vlan_mapping->flag);
    SYS_VLAN_MAPPING_DEBUG_INFO(" svid                       :%d\n", p_vlan_mapping->old_svid);
    SYS_VLAN_MAPPING_DEBUG_INFO(" cvid                       :%d\n", p_vlan_mapping->old_cvid);

    SYS_VLAN_MAPPING_DEBUG_INFO("-------mapping to-----------\n");
    SYS_VLAN_MAPPING_DEBUG_INFO(" new svid                  :%d\n", p_vlan_mapping->new_svid);
    SYS_VLAN_MAPPING_DEBUG_INFO(" new cvid                  :%d\n", p_vlan_mapping->new_cvid);
    SYS_VLAN_MAPPING_DEBUG_INFO(" new scfi                  :%d\n", p_vlan_mapping->new_scfi);

    /*egress action check missed..*/
    sal_memset(&scl, 0, sizeof(sys_scl_entry_t));
    sal_memset(&vlan_mapping_op, 0, sizeof(sys_vlan_mapping_tag_op_t));

    vlan_mapping_op.ccos_sl = p_vlan_mapping->ccos_sl;
    vlan_mapping_op.cvid_sl = p_vlan_mapping->cvid_sl;
    vlan_mapping_op.ctag_op = p_vlan_mapping->ctag_op;
    vlan_mapping_op.scos_sl = p_vlan_mapping->scos_sl;
    vlan_mapping_op.svid_sl = p_vlan_mapping->svid_sl;
    vlan_mapping_op.stag_op = p_vlan_mapping->stag_op;
    CTC_ERROR_RETURN(_sys_goldengate_vlan_check_vlan_mapping_action(lchip, CTC_EGRESS, &vlan_mapping_op));

    if (CTC_FLAG_ISSET(p_vlan_mapping->flag, CTC_VLAN_MAPPING_FLAG_DEFAULT_ENTRY))
    {
        return _sys_goldengate_vlan_mapping_set_egs_default_action(lchip, gport, p_vlan_mapping, 1);
    }

    CTC_ERROR_RETURN(_sys_goldengate_vlan_mapping_build_egs_key(lchip, gport, p_vlan_mapping, &scl));

    scl.action.type  = SYS_SCL_ACTION_EGRESS;
    scl.action.action_flag = p_vlan_mapping->action;
    CTC_ERROR_RETURN(_sys_goldengate_vlan_mapping_build_egs_ad(lchip, p_vlan_mapping, &scl.action.u.egs_action));

    gid = _sys_goldengate_vlan_mapping_get_scl_gid(lchip, scl.key.type, CTC_EGRESS, lport, 0);
    if(gid == 0)
    {
        return CTC_E_SCL_GROUP_ID;
    }

    CTC_ERROR_RETURN(sys_goldengate_scl_add_entry(lchip, SYS_SCL_GROUP_ID_INNER_HASH_VLAN_MAPPING_EGS, &scl, 1));
    CTC_ERROR_RETURN(sys_goldengate_scl_install_entry(lchip, scl.entry_id, 1));

    return CTC_E_NONE;

}

int32
sys_goldengate_vlan_remove_egress_vlan_mapping(uint8 lchip, uint16 gport, ctc_egress_vlan_mapping_t* p_vlan_mapping)
{
    int32             ret = CTC_E_NONE;
    uint32            gid = 0;
    sys_scl_entry_t   scl;

    SYS_VLAN_MAPPING_DEBUG_FUNC();
    SYS_VLAN_MAPPING_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_vlan_mapping);

    if (!CTC_FLAG_ISSET(p_vlan_mapping->flag, CTC_VLAN_MAPPING_FLAG_USE_LOGIC_PORT))
    {
        CTC_GLOBAL_PORT_CHECK(gport);
        if (!CTC_IS_LINKAGG_PORT(gport))
        {
            ret = sys_goldengate_chip_is_local(lchip, SYS_MAP_CTC_GPORT_TO_GCHIP(gport));
            if (FALSE == ret)
            {
                return CTC_E_CHIP_IS_REMOTE;
            }
        }
    }

    SYS_VLAN_MAPPING_DEBUG_FUNC();
    SYS_VLAN_MAPPING_DEBUG_INFO("------vlan mapping----------\n");
    SYS_VLAN_MAPPING_DEBUG_INFO(" gport                      :0x%X\n", gport);
    SYS_VLAN_MAPPING_DEBUG_INFO(" key value                :0x%X\n", p_vlan_mapping->key);
    SYS_VLAN_MAPPING_DEBUG_INFO(" flag                        :0x%X\n", p_vlan_mapping->flag);
    SYS_VLAN_MAPPING_DEBUG_INFO(" svid                       :%d\n", p_vlan_mapping->old_svid);
    SYS_VLAN_MAPPING_DEBUG_INFO(" cvid                       :%d\n", p_vlan_mapping->old_cvid);

    if (CTC_FLAG_ISSET(p_vlan_mapping->flag, CTC_VLAN_MAPPING_FLAG_DEFAULT_ENTRY))
    {
        return _sys_goldengate_vlan_mapping_set_egs_default_action(lchip, gport, p_vlan_mapping, 0);
    }

    sal_memset(&scl, 0, sizeof(sys_scl_entry_t));
    CTC_ERROR_RETURN(_sys_goldengate_vlan_mapping_build_egs_key(lchip, gport, p_vlan_mapping, &scl));

    gid = _sys_goldengate_vlan_mapping_get_scl_gid(lchip, scl.key.type, CTC_EGRESS, CTC_MAP_GPORT_TO_LPORT(gport), 0);
    if(gid == 0)
    {
        return CTC_E_SCL_GROUP_ID;
    }

    CTC_ERROR_RETURN(sys_goldengate_scl_get_entry_id(lchip, &scl, gid));
    CTC_ERROR_RETURN(sys_goldengate_scl_uninstall_entry(lchip, scl.entry_id, 1));
    CTC_ERROR_RETURN(sys_goldengate_scl_remove_entry(lchip, scl.entry_id, 1));

    return CTC_E_NONE;

}

int32
sys_goldengate_vlan_get_egress_vlan_mapping(uint8 lchip, uint16 gport, ctc_egress_vlan_mapping_t* p_vlan_mapping)
{
    int32 ret = CTC_E_NONE;
    sys_scl_entry_t   scl;
    uint32  gid  = 0;
    uint16 lport = 0;

    SYS_VLAN_MAPPING_DEBUG_FUNC();

    SYS_VLAN_MAPPING_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_vlan_mapping);

    if (!CTC_FLAG_ISSET(p_vlan_mapping->flag, CTC_VLAN_MAPPING_FLAG_USE_LOGIC_PORT))
    {
        CTC_GLOBAL_PORT_CHECK(gport);
        if (!CTC_IS_LINKAGG_PORT(gport))
        {
            ret = sys_goldengate_chip_is_local(lchip, SYS_MAP_CTC_GPORT_TO_GCHIP(gport));
            if (FALSE == ret)
            {
                return CTC_E_CHIP_IS_REMOTE;
            }
        }
    }

    lport = CTC_MAP_GPORT_TO_LPORT(gport);

    SYS_VLAN_MAPPING_DEBUG_INFO("------vlan mapping----------\n");
    SYS_VLAN_MAPPING_DEBUG_INFO(" gport                     :0x%X\n", gport);
    SYS_VLAN_MAPPING_DEBUG_INFO(" key value                :0x%X\n", p_vlan_mapping->key);
    SYS_VLAN_MAPPING_DEBUG_INFO(" action                    :0x%X\n", p_vlan_mapping->action);
    SYS_VLAN_MAPPING_DEBUG_INFO(" flag                       :0x%X\n", p_vlan_mapping->flag);
    SYS_VLAN_MAPPING_DEBUG_INFO(" svid                       :%d\n", p_vlan_mapping->old_svid);
    SYS_VLAN_MAPPING_DEBUG_INFO(" cvid                       :%d\n", p_vlan_mapping->old_cvid);
    SYS_VLAN_MAPPING_DEBUG_INFO(" svlan end                :%d\n", p_vlan_mapping->svlan_end);
    SYS_VLAN_MAPPING_DEBUG_INFO(" cvlan end                :%d\n", p_vlan_mapping->cvlan_end);


    sal_memset(&scl, 0, sizeof(sys_scl_entry_t));

    CTC_ERROR_RETURN(_sys_goldengate_vlan_mapping_build_egs_key(lchip, gport, p_vlan_mapping, &scl));

    gid = _sys_goldengate_vlan_mapping_get_scl_gid(lchip, scl.key.type, CTC_EGRESS, lport, 0);
    if(gid == 0)
    {
        return CTC_E_SCL_GROUP_ID;
    }

    SYS_VLAN_MAPPING_DEBUG_INFO(" group id :%u\n", gid);

    /* get scl entry*/
    CTC_ERROR_RETURN(sys_goldengate_scl_get_entry(lchip, &scl, gid));
    _sys_goldengate_vlan_mapping_unmap_egs_action(lchip, p_vlan_mapping, &scl.action);

    return CTC_E_NONE;
}


int32
sys_goldengate_vlan_add_default_egress_vlan_mapping(uint8 lchip, uint16 gport, ctc_vlan_miss_t* p_def_action)
{

    uint16 lport = 0;
    sys_scl_action_t action;

    SYS_VLAN_MAPPING_INIT_CHECK(lchip);
    SYS_VLAN_MAPPING_DEBUG_FUNC();

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);
    CTC_PTR_VALID_CHECK(p_def_action);


    sal_memset(&action, 0, sizeof(action));

    switch (p_def_action->flag)
    {
    case CTC_VLAN_MISS_ACTION_DO_NOTHING:
        break;

    case CTC_VLAN_MISS_ACTION_DISCARD:
        CTC_SET_FLAG(action.u.egs_action.flag , CTC_SCL_EGS_ACTION_FLAG_DISCARD);
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    action.type = CTC_SCL_ACTION_EGRESS;
    CTC_ERROR_RETURN(sys_goldengate_scl_set_default_action(lchip, gport, &action));

    return CTC_E_NONE;
}


int32
sys_goldengate_vlan_remove_default_egress_vlan_mapping(uint8 lchip, uint16 gport)
{

    uint16 lport = 0;
    sys_scl_action_t action;

    SYS_VLAN_MAPPING_INIT_CHECK(lchip);
    SYS_VLAN_MAPPING_DEBUG_FUNC();

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);

    sal_memset(&action, 0, sizeof(action));

    action.type = CTC_SCL_ACTION_EGRESS;
    CTC_ERROR_RETURN(sys_goldengate_scl_set_default_action(lchip, gport, &action));
    return CTC_E_NONE;
}

int32
sys_goldengate_vlan_remove_all_egress_vlan_mapping_by_port(uint8 lchip, uint16 gport)
{
    int32  ret = CTC_E_NONE;


    SYS_VLAN_MAPPING_INIT_CHECK(lchip);
    SYS_VLAN_MAPPING_DEBUG_FUNC();

    CTC_GLOBAL_PORT_CHECK(gport);
    if (!CTC_IS_LINKAGG_PORT(gport))
    {
        ret = sys_goldengate_chip_is_local(lchip, SYS_MAP_CTC_GPORT_TO_GCHIP(gport));
        if (FALSE == ret)
        {
            return CTC_E_CHIP_IS_REMOTE;
        }
    }
    CTC_ERROR_RETURN(sys_goldengate_scl_remove_all_inner_hash_vlan(lchip,  gport, CTC_EGRESS));

    return CTC_E_NONE;
}

int32
sys_goldengate_vlan_mapping_init(uint8 lchip)
{
    LCHIP_CHECK(lchip);

    if (NULL != p_gg_vlan_mapping_master[lchip])
    {
        return CTC_E_NONE;
    }

    MALLOC_ZERO(MEM_VLAN_MODULE, p_gg_vlan_mapping_master[lchip], sizeof(sys_vlan_mapping_master_t));

    if (NULL == p_gg_vlan_mapping_master[lchip])
    {
        return CTC_E_NO_MEMORY;
    }

    p_gg_vlan_mapping_master[lchip]->igs_step = DsVlanRangeProfile_vlanMax1_f - DsVlanRangeProfile_vlanMax0_f ;
    p_gg_vlan_mapping_master[lchip]->egs_step = DsEgressVlanRangeProfile_vlanMax1_f - DsEgressVlanRangeProfile_vlanMax0_f ;
    SYS_VLAN_MAPPING_CREATE_LOCK(lchip);

    return CTC_E_NONE;
}

int32
sys_goldengate_vlan_mapping_deinit(uint8 lchip)
{
    LCHIP_CHECK(lchip);
    if (NULL == p_gg_vlan_mapping_master[lchip])
    {
        return CTC_E_NONE;
    }

    sal_mutex_destroy(p_gg_vlan_mapping_master[lchip]->mutex);
    mem_free(p_gg_vlan_mapping_master[lchip]);

    return CTC_E_NONE;
}

int32
sys_goldengate_vlan_mapping_get_master(uint8 lchip, sys_vlan_mapping_master_t*** p_vlan_mapping_master)
{
    SYS_VLAN_MAPPING_INIT_CHECK(lchip);
    (*p_vlan_mapping_master) = p_gg_vlan_mapping_master;
    return CTC_E_NONE;
}


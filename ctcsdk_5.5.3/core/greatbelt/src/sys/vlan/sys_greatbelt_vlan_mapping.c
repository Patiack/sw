/**
 @file sys_greatbelt_vlan_mapping.c

 @date 2010-1-4

 @version v2.0

*/

#include "ctc_debug.h"
#include "ctc_error.h"
#include "ctc_const.h"
#include "ctc_macro.h"

#include "sys_greatbelt_vlan_mapping.h"
#include "sys_greatbelt_scl.h"
#include "sys_greatbelt_chip.h"
#include "sys_greatbelt_vlan.h"
#include "sys_greatbelt_nexthop_api.h"
#include "sys_greatbelt_qos_policer.h"
#include "sys_greatbelt_queue_enq.h"
#include "sys_greatbelt_l2_fdb.h"

/******************************************************************************
*
*   Macros and Defines
*
*******************************************************************************/

#define VLAN_RANGE_ENTRY_NUM            64
#define VLAN_RANGE_EN_BIT_POS           15
#define VLAN_RANGE_TYPE_BIT_POS         14
#define USER_VLAN_PTR_RANGE         5118

typedef struct
{
    uint16  vrange[VLAN_RANGE_ENTRY_NUM * CTC_BOTH_DIRECTION];
    sal_mutex_t* mutex;
}sys_vlan_mapping_master_t;


static sys_vlan_mapping_master_t* p_gb_vlan_mapping_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};

#define SYS_VLAN_RANGE_SW_EACH_GROUP() p_gb_vlan_mapping_master[lchip]->vrange[vrange_info->direction * VLAN_RANGE_ENTRY_NUM + vrange_info->vrange_grpid]

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
        SYS_LCHIP_CHECK_ACTIVE(lchip);\
        if (NULL == p_gb_vlan_mapping_master[lchip]){ \
            return CTC_E_NOT_INIT; } \
    } while(0)

#define SYS_VLAN_MAPPING_CREATE_LOCK(lchip)                         \
    do                                                              \
    {                                                               \
        sal_mutex_create(&p_gb_vlan_mapping_master[lchip]->mutex);  \
        if (NULL == p_gb_vlan_mapping_master[lchip]->mutex)         \
        {                                                           \
            CTC_ERROR_RETURN(CTC_E_FAIL_CREATE_MUTEX);              \
        }                                                           \
    } while (0)

#define SYS_VLAN_MAPPING_LOCK(lchip) \
    sal_mutex_lock(p_gb_vlan_mapping_master[lchip]->mutex)

#define SYS_VLAN_MAPPING_UNLOCK(lchip) \
    sal_mutex_unlock(p_gb_vlan_mapping_master[lchip]->mutex)

#define CTC_ERROR_RETURN_VLAN_MAPPING_UNLOCK(op) \
    do \
    { \
        int32 rv = (op); \
        if (rv < 0) \
        { \
            sal_mutex_unlock(p_gb_vlan_mapping_master[lchip]->mutex); \
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
        SYS_VLAN_MAPPING_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "  %% FUNC: %s()\n", __FUNCTION__); \
    }

#define SYS_VLAN_MAPPING_DEBUG_DUMP(FMT, ...) \
    { \
        SYS_VLAN_MAPPING_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, FMT, ##__VA_ARGS__); \
    }

#define SYS_VLAN_MAPPING_DEBUG_PRAM(FMT, ...) \
    { \
        SYS_VLAN_MAPPING_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, FMT, ##__VA_ARGS__); \
    }

#define SYS_VLAN_MAPPING_GET_MAX_CVLAN(dir, p_vlan_mapping_in, old_cvid)            \
    {                                                                                  \
        if (0 != p_vlan_mapping_in->cvlan_end)                                         \
        {                                                                              \
            if (p_vlan_mapping_in->cvlan_end >= p_vlan_mapping_in->old_cvid)            \
            {                                                                          \
                SYS_VLAN_ID_VALID_CHECK(p_vlan_mapping_in->cvlan_end);                     \
                vrange_info.vrange_grpid = p_vlan_mapping_in->vrange_grpid;                 \
                vrange_info.direction = dir;                                                \
                CTC_ERROR_RETURN(sys_greatbelt_vlan_get_vlan_range_type(lchip, &vrange_info, &is_svlan)); \
                if (is_svlan)                                                               \
                { return CTC_E_VLAN_MAPPING_RANGE_TYPE_NOT_MATCH; }                        \
                CTC_ERROR_RETURN(_sys_greatbelt_vlan_get_maxvid_from_vrange(lchip,                \
                                     dir,                                       \
                                     p_vlan_mapping_in->old_cvid,               \
                                     p_vlan_mapping_in->cvlan_end,              \
                                     p_vlan_mapping_in->vrange_grpid,           \
                                     &old_cvid));                               \
            }                                                                           \
            else                                                                        \
            {                                                                           \
                return CTC_E_INVALID_PARAM;                                             \
            }                                                                           \
        }                                                                              \
    }

#define SYS_VLAN_MAPPING_GET_MAX_SVLAN(dir, p_vlan_mapping_in, old_svid)           \
    {                                                                                  \
        if (0 != p_vlan_mapping_in->svlan_end)                                         \
        {                                                                              \
            if (p_vlan_mapping_in->svlan_end >= p_vlan_mapping_in->old_svid)            \
            {                                                                          \
                SYS_VLAN_ID_VALID_CHECK(p_vlan_mapping_in->svlan_end);                     \
                vrange_info.vrange_grpid = p_vlan_mapping_in->vrange_grpid;                 \
                vrange_info.direction = dir;                                                \
                CTC_ERROR_RETURN(sys_greatbelt_vlan_get_vlan_range_type(lchip, &vrange_info, &is_svlan)); \
                if (!is_svlan)                                                              \
                { return CTC_E_VLAN_MAPPING_RANGE_TYPE_NOT_MATCH; }                        \
                CTC_ERROR_RETURN(_sys_greatbelt_vlan_get_maxvid_from_vrange(lchip,                \
                                     dir,                                       \
                                     p_vlan_mapping_in->old_svid,               \
                                     p_vlan_mapping_in->svlan_end,              \
                                     p_vlan_mapping_in->vrange_grpid,           \
                                     &old_svid));                               \
            }                                                                          \
            else                                                                       \
            {                                                                          \
                return CTC_E_INVALID_PARAM;                                            \
            }                                                                          \
        }                                                                              \
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
/*lchip's configs are the same
0~0 means disable
vlan range cannot overlap*/
int32
sys_greatbelt_vlan_get_vlan_range_type(uint8 lchip, ctc_vlan_range_info_t* vrange_info, bool* is_svlan)
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
sys_greatbelt_vlan_create_vlan_range(uint8 lchip, ctc_vlan_range_info_t* vrange_info, bool is_svlan)
{
    SYS_VLAN_MAPPING_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(vrange_info);
    SYS_VLAN_RANGE_INFO_CHECK(vrange_info);

    SYS_VLAN_MAPPING_LOCK(lchip);
    if (CTC_IS_BIT_SET(SYS_VLAN_RANGE_SW_EACH_GROUP(), VLAN_RANGE_EN_BIT_POS))
    { /*group created*/
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
sys_greatbelt_vlan_destroy_vlan_range(uint8 lchip, ctc_vlan_range_info_t* vrange_info)
{
    uint32 cmd = 0;
    uint8 direction;
    int32 vrange_grpid;

    ds_vlan_range_profile_t ds_vlan_range_profile;
    ds_egress_vlan_range_profile_t ds_egress_vlan_range_profile;

    SYS_VLAN_MAPPING_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(vrange_info);
    SYS_VLAN_RANGE_INFO_CHECK(vrange_info);

    SYS_VLAN_MAPPING_LOCK(lchip);
    if (!CTC_IS_BIT_SET(SYS_VLAN_RANGE_SW_EACH_GROUP(), VLAN_RANGE_EN_BIT_POS))
    {
        SYS_VLAN_MAPPING_UNLOCK(lchip);
        return CTC_E_VLAN_RANGE_NOT_EXIST;
    }

    direction = vrange_info->direction;
    vrange_grpid = vrange_info->vrange_grpid;

    sal_memset(&ds_vlan_range_profile, 0, sizeof(ds_vlan_range_profile_t));
    sal_memset(&ds_egress_vlan_range_profile, 0, sizeof(ds_egress_vlan_range_profile_t));

    if (CTC_INGRESS == direction)
    {
        cmd = DRV_IOW(DsVlanRangeProfile_t, DRV_ENTRY_FLAG);

        CTC_ERROR_RETURN_VLAN_MAPPING_UNLOCK(DRV_IOCTL(lchip, vrange_grpid, cmd, &ds_vlan_range_profile));
    }
    else
    {
        cmd = DRV_IOW(DsEgressVlanRangeProfile_t, DRV_ENTRY_FLAG);

        CTC_ERROR_RETURN_VLAN_MAPPING_UNLOCK(DRV_IOCTL(lchip, vrange_grpid, cmd, &ds_egress_vlan_range_profile));
    }

    SYS_VLAN_RANGE_SW_EACH_GROUP() = 0;
    SYS_VLAN_MAPPING_UNLOCK(lchip);

    return CTC_E_NONE;
}

int32
sys_greatbelt_vlan_add_vlan_range_member(uint8 lchip, ctc_vlan_range_info_t* vrange_info, ctc_vlan_range_t* vlan_range)
{
    uint32 cmd = 0;

    uint8  free_idx = 0;
    ds_vlan_range_profile_t ds_vlan_range_profile;
    ds_egress_vlan_range_profile_t ds_egress_vlan_range_profile;
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

    sal_memset(&ds_vlan_range_profile, 0, sizeof(ds_vlan_range_profile_t));
    sal_memset(&ds_egress_vlan_range_profile, 0, sizeof(ds_egress_vlan_range_profile_t));
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

    for (free_idx = 0; free_idx < 8; free_idx++)
    {
        if (!IS_BIT_SET(SYS_VLAN_RANGE_SW_EACH_GROUP(), free_idx))
        {
            break;
        }
    }


    if (free_idx >= 8)
    {
        SYS_VLAN_MAPPING_UNLOCK(lchip);
        return CTC_E_VLAN_MAPPING_VRANGE_FULL;
    }
    SYS_VLAN_MAPPING_UNLOCK(lchip);

    CTC_ERROR_RETURN(sys_greatbelt_vlan_get_vlan_range_member(lchip, vrange_info, &vrange_group, &count));

    for (idx = 0; idx < count; idx++)
    {
        if ((vrange_group.vlan_range[idx].vlan_start == vlan_start) && (vlan_end == vrange_group.vlan_range[idx].vlan_end))
        {
            return CTC_E_NONE;
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
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, vrange_grpid, cmd, &ds_vlan_range_profile));

        switch (free_idx)
        {
        case 0:
            ds_vlan_range_profile.vlan_min0 = vlan_start;
            ds_vlan_range_profile.vlan_max0 = vlan_end;
            break;

        case 1:
            ds_vlan_range_profile.vlan_min1 = vlan_start;
            ds_vlan_range_profile.vlan_max1 = vlan_end;
            break;

        case 2:
            ds_vlan_range_profile.vlan_min2 = vlan_start;
            ds_vlan_range_profile.vlan_max2 = vlan_end;
            break;

        case 3:
            ds_vlan_range_profile.vlan_min3 = vlan_start;
            ds_vlan_range_profile.vlan_max3 = vlan_end;
            break;

        case 4:
            ds_vlan_range_profile.vlan_min4 = vlan_start;
            ds_vlan_range_profile.vlan_max4 = vlan_end;
            break;

        case 5:
            ds_vlan_range_profile.vlan_min5 = vlan_start;
            ds_vlan_range_profile.vlan_max5 = vlan_end;
            break;

        case 6:
            ds_vlan_range_profile.vlan_min6 = vlan_start;
            ds_vlan_range_profile.vlan_max6 = vlan_end;
            break;

        case 7:
            ds_vlan_range_profile.vlan_min7 = vlan_start;
            ds_vlan_range_profile.vlan_max7 = vlan_end;
            break;

        default:
            break;
        }

        cmd = DRV_IOW(DsVlanRangeProfile_t, DRV_ENTRY_FLAG);

        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, vrange_grpid, cmd, &ds_vlan_range_profile));
    }
    else
    {

        cmd = DRV_IOR(DsEgressVlanRangeProfile_t, DRV_ENTRY_FLAG);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, vrange_grpid, cmd, &ds_egress_vlan_range_profile));

        switch (free_idx)
        {
        case 0:
            ds_egress_vlan_range_profile.vlan_min0 = vlan_start;
            ds_egress_vlan_range_profile.vlan_max0 = vlan_end;
            break;

        case 1:
            ds_egress_vlan_range_profile.vlan_min1 = vlan_start;
            ds_egress_vlan_range_profile.vlan_max1 = vlan_end;
            break;

        case 2:
            ds_egress_vlan_range_profile.vlan_min2 = vlan_start;
            ds_egress_vlan_range_profile.vlan_max2 = vlan_end;
            break;

        case 3:
            ds_egress_vlan_range_profile.vlan_min3 = vlan_start;
            ds_egress_vlan_range_profile.vlan_max3 = vlan_end;
            break;

        case 4:
            ds_egress_vlan_range_profile.vlan_min4 = vlan_start;
            ds_egress_vlan_range_profile.vlan_max4 = vlan_end;
            break;

        case 5:
            ds_egress_vlan_range_profile.vlan_min5 = vlan_start;
            ds_egress_vlan_range_profile.vlan_max5 = vlan_end;
            break;

        case 6:
            ds_egress_vlan_range_profile.vlan_min6 = vlan_start;
            ds_egress_vlan_range_profile.vlan_max6 = vlan_end;
            break;

        case 7:
            ds_egress_vlan_range_profile.vlan_min7 = vlan_start;
            ds_egress_vlan_range_profile.vlan_max7 = vlan_end;
            break;

        default:
            break;
        }

        cmd = DRV_IOW(DsEgressVlanRangeProfile_t, DRV_ENTRY_FLAG);

        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, vrange_grpid, cmd, &ds_egress_vlan_range_profile));
    }

    SYS_VLAN_MAPPING_LOCK(lchip);
    CTC_BIT_SET(SYS_VLAN_RANGE_SW_EACH_GROUP(), free_idx);
    SYS_VLAN_MAPPING_UNLOCK(lchip);

    return CTC_E_NONE;

}

int32
sys_greatbelt_vlan_remove_vlan_range_member(uint8 lchip, ctc_vlan_range_info_t* vrange_info, ctc_vlan_range_t* vlan_range)
{

    uint32 cmd = 0;
    uint8  del_idx = 0;
    uint8 direction;
    int32 vrange_grpid;
    uint16 vlan_start;
    uint16 vlan_end;

    ds_vlan_range_profile_t ds_vlan_range_profile;
    ds_egress_vlan_range_profile_t ds_egress_vlan_range_profile;

    sal_memset(&ds_vlan_range_profile, 0, sizeof(ds_vlan_range_profile_t));
    sal_memset(&ds_egress_vlan_range_profile, 0, sizeof(ds_egress_vlan_range_profile_t));

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

    if (CTC_INGRESS == direction)
    {
        cmd = DRV_IOR(DsVlanRangeProfile_t, DRV_ENTRY_FLAG);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, vrange_grpid, cmd, &ds_vlan_range_profile));

        if ((vlan_end <= ds_vlan_range_profile.vlan_max0)
            && (vlan_start >= ds_vlan_range_profile.vlan_min0)
            && (ds_vlan_range_profile.vlan_max0 != ds_vlan_range_profile.vlan_min0))
        {
            del_idx = 0;
        }
        else if ((vlan_end <= ds_vlan_range_profile.vlan_max1)
                 && (vlan_start >= ds_vlan_range_profile.vlan_min1)
                 && (ds_vlan_range_profile.vlan_max1 != ds_vlan_range_profile.vlan_min1))
        {
            del_idx = 1;
        }
        else if ((vlan_end <= ds_vlan_range_profile.vlan_max2)
                 && (vlan_start >= ds_vlan_range_profile.vlan_min2)
                 && (ds_vlan_range_profile.vlan_max2 != ds_vlan_range_profile.vlan_min2))
        {
            del_idx = 2;
        }
        else if ((vlan_end <= ds_vlan_range_profile.vlan_max3)
                 && (vlan_start >= ds_vlan_range_profile.vlan_min3)
                 && (ds_vlan_range_profile.vlan_max3 != ds_vlan_range_profile.vlan_min3))
        {
            del_idx = 3;
        }
        else if ((vlan_end <= ds_vlan_range_profile.vlan_max4)
                 && (vlan_start >= ds_vlan_range_profile.vlan_min4)
                 && (ds_vlan_range_profile.vlan_max4 != ds_vlan_range_profile.vlan_min4))
        {
            del_idx = 4;
        }
        else if ((vlan_end <= ds_vlan_range_profile.vlan_max5)
                 && (vlan_start >= ds_vlan_range_profile.vlan_min5)
                 && (ds_vlan_range_profile.vlan_max5 != ds_vlan_range_profile.vlan_min5))
        {
            del_idx = 5;
        }
        else if ((vlan_end <= ds_vlan_range_profile.vlan_max6)
                 && (vlan_start >= ds_vlan_range_profile.vlan_min6)
                 && (ds_vlan_range_profile.vlan_max6 != ds_vlan_range_profile.vlan_min6))
        {
            del_idx = 6;
        }
        else if ((vlan_end <= ds_vlan_range_profile.vlan_max7)
                 && (vlan_start >= ds_vlan_range_profile.vlan_min7)
                 && (ds_vlan_range_profile.vlan_max7 != ds_vlan_range_profile.vlan_min7))
        {
            del_idx = 7;
        }
        else
        {
            return CTC_E_VLAN_MAPPING_GET_VLAN_RANGE_FAILED;
        }

        switch (del_idx)
        {
        case 0:
            ds_vlan_range_profile.vlan_min0 = 0;
            ds_vlan_range_profile.vlan_max0 = 0;
            break;

        case 1:
            ds_vlan_range_profile.vlan_min1 = 0;
            ds_vlan_range_profile.vlan_max1 = 0;
            break;

        case 2:
            ds_vlan_range_profile.vlan_min2 = 0;
            ds_vlan_range_profile.vlan_max2 = 0;
            break;

        case 3:
            ds_vlan_range_profile.vlan_min3 = 0;
            ds_vlan_range_profile.vlan_max3 = 0;
            break;

        case 4:
            ds_vlan_range_profile.vlan_min4 = 0;
            ds_vlan_range_profile.vlan_max4 = 0;
            break;

        case 5:
            ds_vlan_range_profile.vlan_min5 = 0;
            ds_vlan_range_profile.vlan_max5 = 0;
            break;

        case 6:
            ds_vlan_range_profile.vlan_min6 = 0;
            ds_vlan_range_profile.vlan_max6 = 0;
            break;

        case 7:
            ds_vlan_range_profile.vlan_min7 = 0;
            ds_vlan_range_profile.vlan_max7 = 0;
            break;
        }

        cmd = DRV_IOW(DsVlanRangeProfile_t, DRV_ENTRY_FLAG);

        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, vrange_grpid, cmd, &ds_vlan_range_profile));
    }
    else
    {
        cmd = DRV_IOR(DsEgressVlanRangeProfile_t, DRV_ENTRY_FLAG);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, vrange_grpid, cmd, &ds_egress_vlan_range_profile));

        if ((vlan_end <= ds_egress_vlan_range_profile.vlan_max0)
            && (vlan_start >= ds_egress_vlan_range_profile.vlan_min0)
            && (ds_egress_vlan_range_profile.vlan_max0 != ds_egress_vlan_range_profile.vlan_min0))
        {
            del_idx = 0;
        }
        else if ((vlan_end <= ds_egress_vlan_range_profile.vlan_max1)
                 && (vlan_start >= ds_egress_vlan_range_profile.vlan_min1)
                 && (ds_egress_vlan_range_profile.vlan_max1 != ds_egress_vlan_range_profile.vlan_min1))
        {
            del_idx = 1;
        }
        else if ((vlan_end <= ds_egress_vlan_range_profile.vlan_max2)
                 && (vlan_start >= ds_egress_vlan_range_profile.vlan_min2)
                 && (ds_egress_vlan_range_profile.vlan_max2 != ds_egress_vlan_range_profile.vlan_min2))
        {
            del_idx = 2;
        }
        else if ((vlan_end <= ds_egress_vlan_range_profile.vlan_max3)
                 && (vlan_start >= ds_egress_vlan_range_profile.vlan_min3)
                 && (ds_egress_vlan_range_profile.vlan_max3 != ds_egress_vlan_range_profile.vlan_min3))
        {
            del_idx = 3;
        }
        else if ((vlan_end <= ds_egress_vlan_range_profile.vlan_max4)
                 && (vlan_start >= ds_egress_vlan_range_profile.vlan_min4)
                 && (ds_egress_vlan_range_profile.vlan_max4 != ds_egress_vlan_range_profile.vlan_min4))
        {
            del_idx = 4;
        }
        else if ((vlan_end <= ds_egress_vlan_range_profile.vlan_max5)
                 && (vlan_start >= ds_egress_vlan_range_profile.vlan_min5)
                 && (ds_egress_vlan_range_profile.vlan_max5 != ds_egress_vlan_range_profile.vlan_min5))
        {
            del_idx = 5;
        }
        else if ((vlan_end <= ds_egress_vlan_range_profile.vlan_max6)
                 && (vlan_start >= ds_egress_vlan_range_profile.vlan_min6)
                 && (ds_egress_vlan_range_profile.vlan_max6 != ds_egress_vlan_range_profile.vlan_min6))
        {
            del_idx = 6;
        }
        else if ((vlan_end <= ds_egress_vlan_range_profile.vlan_max7)
                 && (vlan_start >= ds_egress_vlan_range_profile.vlan_min7)
                 && (ds_egress_vlan_range_profile.vlan_max7 != ds_egress_vlan_range_profile.vlan_min7))
        {
            del_idx = 7;
        }
        else
        {
            return CTC_E_VLAN_MAPPING_GET_VLAN_RANGE_FAILED;
        }

        switch (del_idx)
        {
        case 0:
            ds_egress_vlan_range_profile.vlan_min0 = 0;
            ds_egress_vlan_range_profile.vlan_max0 = 0;
            break;

        case 1:
            ds_egress_vlan_range_profile.vlan_min1 = 0;
            ds_egress_vlan_range_profile.vlan_max1 = 0;
            break;

        case 2:
            ds_egress_vlan_range_profile.vlan_min2 = 0;
            ds_egress_vlan_range_profile.vlan_max2 = 0;
            break;

        case 3:
            ds_egress_vlan_range_profile.vlan_min3 = 0;
            ds_egress_vlan_range_profile.vlan_max3 = 0;
            break;

        case 4:
            ds_egress_vlan_range_profile.vlan_min4 = 0;
            ds_egress_vlan_range_profile.vlan_max4 = 0;
            break;

        case 5:
            ds_egress_vlan_range_profile.vlan_min5 = 0;
            ds_egress_vlan_range_profile.vlan_max5 = 0;
            break;

        case 6:
            ds_egress_vlan_range_profile.vlan_min6 = 0;
            ds_egress_vlan_range_profile.vlan_max6 = 0;
            break;

        case 7:
            ds_egress_vlan_range_profile.vlan_min7 = 0;
            ds_egress_vlan_range_profile.vlan_max7 = 0;
            break;
        }

        cmd = DRV_IOW(DsEgressVlanRangeProfile_t, DRV_ENTRY_FLAG);

        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, vrange_grpid, cmd, &ds_egress_vlan_range_profile));
    }

    SYS_VLAN_MAPPING_LOCK(lchip);
    CTC_BIT_UNSET(SYS_VLAN_RANGE_SW_EACH_GROUP(), del_idx);
    SYS_VLAN_MAPPING_UNLOCK(lchip);

    return CTC_E_NONE;

}

int32
sys_greatbelt_vlan_get_vlan_range_member(uint8 lchip, ctc_vlan_range_info_t* vrange_info, ctc_vlan_range_group_t* vrange_group, uint8* count)
{
    uint32 cmd = 0;
    uint8 idx = 0;
    uint8 cnt = 0;
    ds_vlan_range_profile_t ds_vlan_range_profile;
    ds_egress_vlan_range_profile_t ds_egress_vlan_range_profile;
    ctc_vlan_range_group_t _vrange_group;
    uint8 direction;
    int32 vrange_grpid;

    CTC_PTR_VALID_CHECK(count);
    CTC_PTR_VALID_CHECK(vrange_info);
    CTC_PTR_VALID_CHECK(vrange_group);

    sal_memset(&ds_vlan_range_profile, 0, sizeof(ds_vlan_range_profile_t));
    sal_memset(&ds_egress_vlan_range_profile, 0, sizeof(ds_egress_vlan_range_profile_t));
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

    if (CTC_INGRESS == direction)
    {
        cmd = DRV_IOR(DsVlanRangeProfile_t, DRV_ENTRY_FLAG);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, vrange_grpid, cmd, &ds_vlan_range_profile));
        _vrange_group.vlan_range[0].vlan_start = ds_vlan_range_profile.vlan_min0;
        _vrange_group.vlan_range[0].vlan_end = ds_vlan_range_profile.vlan_max0;
        _vrange_group.vlan_range[1].vlan_start = ds_vlan_range_profile.vlan_min1;
        _vrange_group.vlan_range[1].vlan_end = ds_vlan_range_profile.vlan_max1;
        _vrange_group.vlan_range[2].vlan_start = ds_vlan_range_profile.vlan_min2;
        _vrange_group.vlan_range[2].vlan_end = ds_vlan_range_profile.vlan_max2;
        _vrange_group.vlan_range[3].vlan_start = ds_vlan_range_profile.vlan_min3;
        _vrange_group.vlan_range[3].vlan_end = ds_vlan_range_profile.vlan_max3;
        _vrange_group.vlan_range[4].vlan_start = ds_vlan_range_profile.vlan_min4;
        _vrange_group.vlan_range[4].vlan_end = ds_vlan_range_profile.vlan_max4;
        _vrange_group.vlan_range[5].vlan_start = ds_vlan_range_profile.vlan_min5;
        _vrange_group.vlan_range[5].vlan_end = ds_vlan_range_profile.vlan_max5;
        _vrange_group.vlan_range[6].vlan_start = ds_vlan_range_profile.vlan_min6;
        _vrange_group.vlan_range[6].vlan_end = ds_vlan_range_profile.vlan_max6;
        _vrange_group.vlan_range[7].vlan_start = ds_vlan_range_profile.vlan_min7;
        _vrange_group.vlan_range[7].vlan_end = ds_vlan_range_profile.vlan_max7;

        for (idx = 0; idx < 8; idx++)
        {
            if (_vrange_group.vlan_range[idx].vlan_start < _vrange_group.vlan_range[idx].vlan_end)
            {
                vrange_group->vlan_range[cnt].vlan_start = _vrange_group.vlan_range[idx].vlan_start;
                vrange_group->vlan_range[cnt].vlan_end = _vrange_group.vlan_range[idx].vlan_end;
                cnt++;
            }
        }
    }
    else if (CTC_EGRESS == direction)
    {
        cmd = DRV_IOR(DsEgressVlanRangeProfile_t, DRV_ENTRY_FLAG);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, vrange_grpid, cmd, &ds_egress_vlan_range_profile));
        _vrange_group.vlan_range[0].vlan_start = ds_egress_vlan_range_profile.vlan_min0;
        _vrange_group.vlan_range[0].vlan_end = ds_egress_vlan_range_profile.vlan_max0;
        _vrange_group.vlan_range[1].vlan_start = ds_egress_vlan_range_profile.vlan_min1;
        _vrange_group.vlan_range[1].vlan_end = ds_egress_vlan_range_profile.vlan_max1;
        _vrange_group.vlan_range[2].vlan_start = ds_egress_vlan_range_profile.vlan_min2;
        _vrange_group.vlan_range[2].vlan_end = ds_egress_vlan_range_profile.vlan_max2;
        _vrange_group.vlan_range[3].vlan_start = ds_egress_vlan_range_profile.vlan_min3;
        _vrange_group.vlan_range[3].vlan_end = ds_egress_vlan_range_profile.vlan_max3;
        _vrange_group.vlan_range[4].vlan_start = ds_egress_vlan_range_profile.vlan_min4;
        _vrange_group.vlan_range[4].vlan_end = ds_egress_vlan_range_profile.vlan_max4;
        _vrange_group.vlan_range[5].vlan_start = ds_egress_vlan_range_profile.vlan_min5;
        _vrange_group.vlan_range[5].vlan_end = ds_egress_vlan_range_profile.vlan_max5;
        _vrange_group.vlan_range[6].vlan_start = ds_egress_vlan_range_profile.vlan_min6;
        _vrange_group.vlan_range[6].vlan_end = ds_egress_vlan_range_profile.vlan_max6;
        _vrange_group.vlan_range[7].vlan_start = ds_egress_vlan_range_profile.vlan_min7;
        _vrange_group.vlan_range[7].vlan_end = ds_egress_vlan_range_profile.vlan_max7;

        for (idx = 0; idx < 8; idx++)
        {
            if (_vrange_group.vlan_range[idx].vlan_start < _vrange_group.vlan_range[idx].vlan_end)
            {
                vrange_group->vlan_range[cnt].vlan_start = _vrange_group.vlan_range[idx].vlan_start;
                vrange_group->vlan_range[cnt].vlan_end   = _vrange_group.vlan_range[idx].vlan_end;
                cnt++;
            }
        }
    }

    *count = cnt;

    return CTC_E_NONE;
}

#define INGRESS_VLAN_MAPPING


STATIC int32
_sys_greatbelt_vlan_get_maxvid_from_vrange(uint8 lchip, ctc_direction_t direction, uint16 vlan_start,
                                           uint16 vlan_end,
                                           uint8  vrange_grpid,
                                           uint16* p_max_vid)
{
    uint32 cmd = 0;
    uint16 vlan_range_max = 0;
    ds_vlan_range_profile_t ds_vlan_range_profile;
    ds_egress_vlan_range_profile_t ds_egress_vlan_range_profile;

    sal_memset(&ds_vlan_range_profile, 0, sizeof(ds_vlan_range_profile_t));
    sal_memset(&ds_egress_vlan_range_profile, 0, sizeof(ds_egress_vlan_range_profile_t));
    SYS_VLAN_MAPPING_DEBUG_FUNC();
    CTC_VLAN_RANGE_CHECK(vlan_start);
    CTC_VLAN_RANGE_CHECK(vlan_end);

    CTC_BOTH_DIRECTION_CHECK(direction);

    if (CTC_INGRESS == direction)
    {
        cmd = DRV_IOR(DsVlanRangeProfile_t, DRV_ENTRY_FLAG);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, vrange_grpid, cmd, &ds_vlan_range_profile));

        if ((vlan_end == ds_vlan_range_profile.vlan_max0)
            && (vlan_start == ds_vlan_range_profile.vlan_min0)
            && (ds_vlan_range_profile.vlan_max0 != ds_vlan_range_profile.vlan_min0))
        {
            vlan_range_max = ds_vlan_range_profile.vlan_max0;
        }
        else if ((vlan_end == ds_vlan_range_profile.vlan_max1)
                 && (vlan_start == ds_vlan_range_profile.vlan_min1)
                 && (ds_vlan_range_profile.vlan_max1 != ds_vlan_range_profile.vlan_min1))
        {
            vlan_range_max = ds_vlan_range_profile.vlan_max1;
        }
        else if ((vlan_end == ds_vlan_range_profile.vlan_max2)
                 && (vlan_start == ds_vlan_range_profile.vlan_min2)
                 && (ds_vlan_range_profile.vlan_max2 != ds_vlan_range_profile.vlan_min2))
        {
            vlan_range_max = ds_vlan_range_profile.vlan_max2;
        }
        else if ((vlan_end == ds_vlan_range_profile.vlan_max3)
                 && (vlan_start == ds_vlan_range_profile.vlan_min3)
                 && (ds_vlan_range_profile.vlan_max3 != ds_vlan_range_profile.vlan_min3))
        {
            vlan_range_max = ds_vlan_range_profile.vlan_max3;
        }
        else if ((vlan_end == ds_vlan_range_profile.vlan_max4)
                 && (vlan_start == ds_vlan_range_profile.vlan_min4)
                 && (ds_vlan_range_profile.vlan_max4 != ds_vlan_range_profile.vlan_min4))
        {
            vlan_range_max = ds_vlan_range_profile.vlan_max4;
        }
        else if ((vlan_end == ds_vlan_range_profile.vlan_max5)
                 && (vlan_start == ds_vlan_range_profile.vlan_min5)
                 && (ds_vlan_range_profile.vlan_max5 != ds_vlan_range_profile.vlan_min5))
        {
            vlan_range_max = ds_vlan_range_profile.vlan_max5;
        }
        else if ((vlan_end == ds_vlan_range_profile.vlan_max6)
                 && (vlan_start == ds_vlan_range_profile.vlan_min6)
                 && (ds_vlan_range_profile.vlan_max6 != ds_vlan_range_profile.vlan_min6))
        {
            vlan_range_max = ds_vlan_range_profile.vlan_max6;
        }
        else if ((vlan_end == ds_vlan_range_profile.vlan_max7)
                 && (vlan_start == ds_vlan_range_profile.vlan_min7)
                 && (ds_vlan_range_profile.vlan_max7 != ds_vlan_range_profile.vlan_min7))
        {
            vlan_range_max = ds_vlan_range_profile.vlan_max7;
        }
        else
        {
            return CTC_E_VLAN_MAPPING_GET_VLAN_RANGE_FAILED;
        }
    }
    else
    {
        cmd = DRV_IOR(DsEgressVlanRangeProfile_t, DRV_ENTRY_FLAG);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, vrange_grpid, cmd, &ds_egress_vlan_range_profile));

        if ((vlan_end == ds_egress_vlan_range_profile.vlan_max0)
            && (vlan_start == ds_egress_vlan_range_profile.vlan_min0)
            && (ds_egress_vlan_range_profile.vlan_max0 != ds_egress_vlan_range_profile.vlan_min0))
        {
            vlan_range_max = ds_egress_vlan_range_profile.vlan_max0;
        }
        else if ((vlan_end == ds_egress_vlan_range_profile.vlan_max1)
                 && (vlan_start == ds_egress_vlan_range_profile.vlan_min1)
                 && (ds_egress_vlan_range_profile.vlan_max1 != ds_egress_vlan_range_profile.vlan_min1))
        {
            vlan_range_max = ds_egress_vlan_range_profile.vlan_max1;
        }
        else if ((vlan_end == ds_egress_vlan_range_profile.vlan_max2)
                 && (vlan_start == ds_egress_vlan_range_profile.vlan_min2)
                 && (ds_egress_vlan_range_profile.vlan_max2 != ds_egress_vlan_range_profile.vlan_min2))
        {
            vlan_range_max = ds_egress_vlan_range_profile.vlan_max2;
        }
        else if ((vlan_end == ds_egress_vlan_range_profile.vlan_max3)
                 && (vlan_start == ds_egress_vlan_range_profile.vlan_min3)
                 && (ds_egress_vlan_range_profile.vlan_max3 != ds_egress_vlan_range_profile.vlan_min3))
        {
            vlan_range_max = ds_egress_vlan_range_profile.vlan_max3;
        }
        else if ((vlan_end == ds_egress_vlan_range_profile.vlan_max4)
                 && (vlan_start == ds_egress_vlan_range_profile.vlan_min4)
                 && (ds_egress_vlan_range_profile.vlan_max4 != ds_egress_vlan_range_profile.vlan_min4))
        {
            vlan_range_max = ds_egress_vlan_range_profile.vlan_max4;
        }
        else if ((vlan_end == ds_egress_vlan_range_profile.vlan_max5)
                 && (vlan_start == ds_egress_vlan_range_profile.vlan_min5)
                 && (ds_egress_vlan_range_profile.vlan_max5 != ds_egress_vlan_range_profile.vlan_min5))
        {
            vlan_range_max = ds_egress_vlan_range_profile.vlan_max5;
        }
        else if ((vlan_end == ds_egress_vlan_range_profile.vlan_max6)
                 && (vlan_start == ds_egress_vlan_range_profile.vlan_min6)
                 && (ds_egress_vlan_range_profile.vlan_max6 != ds_egress_vlan_range_profile.vlan_min6))
        {
            vlan_range_max = ds_egress_vlan_range_profile.vlan_max6;
        }
        else if ((vlan_end == ds_egress_vlan_range_profile.vlan_max7)
                 && (vlan_start == ds_egress_vlan_range_profile.vlan_min7)
                 && (ds_egress_vlan_range_profile.vlan_max7 != ds_egress_vlan_range_profile.vlan_min7))
        {
            vlan_range_max = ds_egress_vlan_range_profile.vlan_max7;
        }
        else
        {
            return CTC_E_VLAN_MAPPING_GET_VLAN_RANGE_FAILED;
        }
    }

    *p_max_vid = vlan_range_max;

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_vlan_mapping_build_igs_key(uint8 lchip, uint16 gport,
                                          ctc_vlan_mapping_t* p_vlan_mapping_in,
                                          sys_scl_entry_t* pc)
{
    uint16 old_cvid = 0;
    uint16 old_svid = 0;

    ctc_vlan_range_info_t vrange_info;
    bool is_svlan = FALSE;

    sal_memset(&vrange_info, 0, sizeof(ctc_vlan_range_info_t));

    SYS_VLAN_MAPPING_DEBUG_FUNC();

    if (!(p_vlan_mapping_in->flag & CTC_VLAN_MAPPING_FLAG_USE_FLEX))
    {
        switch (p_vlan_mapping_in->key
                & (CTC_VLAN_MAPPING_KEY_CVID | CTC_VLAN_MAPPING_KEY_SVID
                   | CTC_VLAN_MAPPING_KEY_CTAG_COS | CTC_VLAN_MAPPING_KEY_STAG_COS))
        {
        case (CTC_VLAN_MAPPING_KEY_CVID):
            SYS_VLAN_ID_VALID_CHECK(p_vlan_mapping_in->old_cvid);
            old_cvid = p_vlan_mapping_in->old_cvid;

            SYS_VLAN_MAPPING_GET_MAX_CVLAN(CTC_INGRESS, p_vlan_mapping_in, old_cvid);

            pc->key.u.hash_port_cvlan_key.gport = gport;
            pc->key.u.hash_port_cvlan_key.cvlan = old_cvid;
            pc->key.u.hash_port_cvlan_key.dir = CTC_INGRESS;
            pc->key.type  = SYS_SCL_KEY_HASH_PORT_CVLAN;

            if (CTC_FLAG_ISSET(p_vlan_mapping_in->flag, CTC_VLAN_MAPPING_FLAG_USE_LOGIC_PORT))
            {
                pc->key.u.hash_port_cvlan_key.gport_type = 2;
            }
            break;

        case (CTC_VLAN_MAPPING_KEY_SVID):
            SYS_VLAN_ID_VALID_CHECK(p_vlan_mapping_in->old_svid);
            old_svid = p_vlan_mapping_in->old_svid;

            SYS_VLAN_MAPPING_GET_MAX_SVLAN(CTC_INGRESS, p_vlan_mapping_in, old_svid);

            pc->key.u.hash_port_svlan_key.gport = gport;
            pc->key.u.hash_port_svlan_key.svlan = old_svid;
            pc->key.u.hash_port_svlan_key.dir = CTC_INGRESS;
            pc->key.type  = SYS_SCL_KEY_HASH_PORT_SVLAN;

            if (CTC_FLAG_ISSET(p_vlan_mapping_in->flag, CTC_VLAN_MAPPING_FLAG_USE_LOGIC_PORT))
            {
                pc->key.u.hash_port_svlan_key.gport_type = 2;
            }
            break;

        case (CTC_VLAN_MAPPING_KEY_CVID | CTC_VLAN_MAPPING_KEY_SVID):
            SYS_VLAN_ID_VALID_CHECK(p_vlan_mapping_in->old_cvid);
            SYS_VLAN_ID_VALID_CHECK(p_vlan_mapping_in->old_svid);
            old_cvid = p_vlan_mapping_in->old_cvid;
            old_svid = p_vlan_mapping_in->old_svid;

            SYS_VLAN_MAPPING_GET_MAX_CVLAN(CTC_INGRESS, p_vlan_mapping_in, old_cvid);

            SYS_VLAN_MAPPING_GET_MAX_SVLAN(CTC_INGRESS, p_vlan_mapping_in, old_svid);

            pc->key.u.hash_port_2vlan_key.gport = gport;
            pc->key.u.hash_port_2vlan_key.cvlan  = old_cvid;
            pc->key.u.hash_port_2vlan_key.svlan  = old_svid;
            pc->key.u.hash_port_2vlan_key.dir = CTC_INGRESS;

            pc->key.type  = SYS_SCL_KEY_HASH_PORT_2VLAN;

            if (CTC_FLAG_ISSET(p_vlan_mapping_in->flag, CTC_VLAN_MAPPING_FLAG_USE_LOGIC_PORT))
            {
                pc->key.u.hash_port_2vlan_key.gport_type = 2;
            }
            break;

        case (CTC_VLAN_MAPPING_KEY_CVID | CTC_VLAN_MAPPING_KEY_CTAG_COS):
            SYS_VLAN_ID_VALID_CHECK(p_vlan_mapping_in->old_cvid);
            CTC_COS_RANGE_CHECK(p_vlan_mapping_in->old_ccos);
            old_cvid = p_vlan_mapping_in->old_cvid;

            SYS_VLAN_MAPPING_GET_MAX_CVLAN(CTC_INGRESS, p_vlan_mapping_in, old_cvid);

            pc->key.u.hash_port_cvlan_cos_key.gport = gport;
            pc->key.u.hash_port_cvlan_cos_key.cvlan = old_cvid;
            pc->key.u.hash_port_cvlan_cos_key.ctag_cos= p_vlan_mapping_in->old_ccos;
            pc->key.u.hash_port_cvlan_cos_key.dir = CTC_INGRESS;

            pc->key.type  = SYS_SCL_KEY_HASH_PORT_CVLAN_COS;

            if (CTC_FLAG_ISSET(p_vlan_mapping_in->flag, CTC_VLAN_MAPPING_FLAG_USE_LOGIC_PORT))
            {
                pc->key.u.hash_port_cvlan_cos_key.gport_type = 2;
            }
            break;

        case (CTC_VLAN_MAPPING_KEY_SVID | CTC_VLAN_MAPPING_KEY_STAG_COS):
            SYS_VLAN_ID_VALID_CHECK(p_vlan_mapping_in->old_svid);
            CTC_COS_RANGE_CHECK(p_vlan_mapping_in->old_scos);
            old_svid = p_vlan_mapping_in->old_svid;

            SYS_VLAN_MAPPING_GET_MAX_SVLAN(CTC_INGRESS, p_vlan_mapping_in, old_svid);

            pc->key.u.hash_port_svlan_cos_key.gport = gport;
            pc->key.u.hash_port_svlan_cos_key.svlan = old_svid;
            pc->key.u.hash_port_svlan_cos_key.stag_cos= p_vlan_mapping_in->old_scos;
            pc->key.u.hash_port_svlan_cos_key.dir = CTC_INGRESS;

            pc->key.type  = SYS_SCL_KEY_HASH_PORT_SVLAN_COS;

            if (CTC_FLAG_ISSET(p_vlan_mapping_in->flag, CTC_VLAN_MAPPING_FLAG_USE_LOGIC_PORT))
            {
                pc->key.u.hash_port_svlan_cos_key.gport_type = 2;
            }
            break;

        case CTC_VLAN_MAPPING_KEY_NONE:

            pc->key.u.hash_port_key.gport = gport;
            pc->key.u.hash_port_key.dir = CTC_INGRESS;

            pc->key.type  = SYS_SCL_KEY_HASH_PORT;
            if (CTC_FLAG_ISSET(p_vlan_mapping_in->flag, CTC_VLAN_MAPPING_FLAG_USE_LOGIC_PORT))
            {
                pc->key.u.hash_port_key.gport_type = 2;
            }
            break;

        default: /* tcam vlan */
            {
                if (p_vlan_mapping_in->key & CTC_VLAN_MAPPING_KEY_CVID)
                {
                    SYS_VLAN_ID_VALID_CHECK(p_vlan_mapping_in->old_cvid);
                    old_cvid = p_vlan_mapping_in->old_cvid;

                    SYS_VLAN_MAPPING_GET_MAX_CVLAN(CTC_INGRESS, p_vlan_mapping_in, old_cvid);

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
                    SYS_VLAN_MAPPING_GET_MAX_SVLAN(CTC_INGRESS, p_vlan_mapping_in, old_svid);

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
        if (p_vlan_mapping_in->key & CTC_VLAN_MAPPING_KEY_CVID)
        {
            SYS_VLAN_ID_VALID_CHECK(p_vlan_mapping_in->old_cvid);
            old_cvid = p_vlan_mapping_in->old_cvid;

            SYS_VLAN_MAPPING_GET_MAX_CVLAN(CTC_INGRESS, p_vlan_mapping_in, old_cvid);

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
            SYS_VLAN_MAPPING_GET_MAX_SVLAN(CTC_INGRESS, p_vlan_mapping_in, old_svid);

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
_sys_greatbelt_vlan_mapping_build_igs_ad(uint8 lchip, ctc_vlan_mapping_t* p_vlan_mapping_in,
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
    uint8  is_srvc_en     = 0;
    uint8  is_aging_en     = 0;
    uint8  is_service_plc_en  = 0;
    uint8  is_service_acl_en  = 0;
    uint8  is_stats_en        = 0;
    uint8  is_vrfid_en        = 0;
    uint8  is_igmp_snooping_en = 0;

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
    is_srvc_en    = CTC_FLAG_ISSET(p_vlan_mapping_in->action, CTC_VLAN_MAPPING_OUTPUT_SERVICE_ID);
    is_service_plc_en = CTC_FLAG_ISSET(p_vlan_mapping_in->action, CTC_VLAN_MAPPING_FLAG_SERVICE_POLICER_EN);
    is_service_acl_en = CTC_FLAG_ISSET(p_vlan_mapping_in->action, CTC_VLAN_MAPPING_FLAG_SERVICE_ACL_EN);
    is_stats_en       = CTC_FLAG_ISSET(p_vlan_mapping_in->action, CTC_VLAN_MAPPING_FLAG_STATS_EN);
    is_vrfid_en       = CTC_FLAG_ISSET(p_vlan_mapping_in->action, CTC_VLAN_MAPPING_OUTPUT_VRFID);
    is_igmp_snooping_en = CTC_FLAG_ISSET(p_vlan_mapping_in->action, CTC_VLAN_MAPPING_FLAG_IGMP_SNOOPING_EN);

    if ((is_fid_en + is_nh_en + is_vrfid_en) > 1)
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

    if (p_vlan_mapping_in->stag_op || p_vlan_mapping_in->ctag_op)
    {
        CTC_SET_FLAG(pia->flag, CTC_SCL_IGS_ACTION_FLAG_VLAN_EDIT);
    }


    pia->vlan_edit.stag_op    = p_vlan_mapping_in->stag_op;
    pia->vlan_edit.svid_sl    = p_vlan_mapping_in->svid_sl;
    pia->vlan_edit.scos_sl    = p_vlan_mapping_in->scos_sl;

    pia->vlan_edit.ctag_op    = p_vlan_mapping_in->ctag_op;
    pia->vlan_edit.cvid_sl    = p_vlan_mapping_in->cvid_sl;
    pia->vlan_edit.ccos_sl    = p_vlan_mapping_in->ccos_sl;
    pia->vlan_edit.tpid_index = p_vlan_mapping_in->tpid_index;

    if (is_svid_en)
    {
        if (p_vlan_mapping_in->new_svid > CTC_MAX_VLAN_ID)
        {
            return CTC_E_VLAN_INVALID_VLAN_ID;
        }
        pia->vlan_edit.svid_new = p_vlan_mapping_in->new_svid;
    }

    if (is_scos_en)
    {
        CTC_COS_RANGE_CHECK(p_vlan_mapping_in->new_scos);
        pia->vlan_edit.scos_new = p_vlan_mapping_in->new_scos;
    }

    if (is_cvid_en)
    {
        if (p_vlan_mapping_in->new_cvid > CTC_MAX_VLAN_ID)
        {
            return CTC_E_VLAN_INVALID_VLAN_ID;
        }
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

        CTC_APS_GROUP_ID_CHECK(p_vlan_mapping_in->aps_select_group_id);
        pia->aps.aps_select_group_id = p_vlan_mapping_in->aps_select_group_id;
        pia->aps.is_working_path = (p_vlan_mapping_in->is_working_path == TRUE) ? 0 : 1;

        CTC_SET_FLAG(pia->flag, CTC_SCL_IGS_ACTION_FLAG_LOGIC_PORT);
        pia->logic_port.logic_port  = p_vlan_mapping_in->logic_src_port;
    }

    if (is_srvc_en)
    {
        pia->service_id = p_vlan_mapping_in->service_id;
        CTC_SET_FLAG(pia->flag, CTC_SCL_IGS_ACTION_FLAG_SERVICE_ID);
    }

    pia->policer_id = p_vlan_mapping_in->policer_id;
    if (is_service_plc_en)
    {
        CTC_SET_FLAG(pia->sub_flag, CTC_SCL_IGS_ACTION_SUB_FLAG_SERVICE_POLICER_EN);
    }

    if CTC_FLAG_ISSET(p_vlan_mapping_in->action, CTC_VLAN_MAPPING_FLAG_SERVICE_QUEUE_EN)
    {
        CTC_SET_FLAG(pia->sub_flag, CTC_SCL_IGS_ACTION_SUB_FLAG_SERVICE_QUEUE_EN);
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

    if (is_vrfid_en)
    {
         /*SYS_L2_FID_CHECK(p_vlan_mapping_in->u3.fid);*/
        CTC_SET_FLAG(pia->flag, CTC_SCL_IGS_ACTION_FLAG_VRFID);
        pia->vrf_id  = p_vlan_mapping_in->u3.vrf_id;
    }

    if (CTC_QOS_COLOR_NONE !=p_vlan_mapping_in->color)
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
        CTC_SET_FLAG(pia->flag, CTC_SCL_IGS_ACTION_FLAG_VPLS);
        if (CTC_FLAG_ISSET(p_vlan_mapping_in->action, CTC_VLAN_MAPPING_FLAG_VPLS_LRN_DIS))
        {
            pia->vpls.learning_en  = 0;
        }
        else
        {
            pia->vpls.learning_en  = 1;
        }

        if (CTC_FLAG_ISSET(p_vlan_mapping_in->action, CTC_VLAN_MAPPING_FLAG_MACLIMIT_EN))
        {
            pia->vpls.mac_limit_en  = 1;
        }

        pia->logic_port.logic_port  = p_vlan_mapping_in->logic_src_port;
        CTC_SET_FLAG(pia->flag, CTC_SCL_IGS_ACTION_FLAG_LOGIC_PORT);

    }

    if ((p_vlan_mapping_in->action & CTC_VLAN_MAPPING_FLAG_VPLS) ||
        (p_vlan_mapping_in->action & CTC_VLAN_MAPPING_FLAG_VPWS))
    {
        pia->vlan_edit.vlan_domain = CTC_SCL_VLAN_DOMAIN_UNCHANGE;
        if (CTC_VLAN_TAG_OP_NONE == pia->vlan_edit.stag_op) /* Perfect solution is user set valid. Here consider compatible with old solution. */
        {
            pia->vlan_edit.stag_op     = CTC_VLAN_TAG_OP_VALID;
        }
        else if (CTC_VLAN_TAG_OP_VALID == pia->vlan_edit.stag_op)
        {
            pia->vlan_edit.stag_op = CTC_VLAN_TAG_OP_NONE;
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

    if (CTC_FLAG_ISSET(p_vlan_mapping_in->action, CTC_VLAN_MAPPING_OUTPUT_VLANPTR))
    {
        CTC_MAX_VALUE_CHECK(p_vlan_mapping_in->user_vlanptr,USER_VLAN_PTR_RANGE);

        pia->user_vlanptr = p_vlan_mapping_in->user_vlanptr;
        CTC_SET_FLAG(pia->flag, CTC_SCL_IGS_ACTION_FLAG_USER_VLANPTR);
    }

    if (is_aging_en)
    {
        CTC_SET_FLAG(pia->flag, CTC_SCL_IGS_ACTION_FLAG_AGING);
    }

    if (is_stats_en)
    {
        CTC_SET_FLAG(pia->flag, CTC_SCL_IGS_ACTION_FLAG_STATS);
        pia->stats_id = p_vlan_mapping_in->stats_id;
    }


    if (is_igmp_snooping_en)
    {
        CTC_SET_FLAG(pia->flag, CTC_SCL_IGS_ACTION_FLAG_IGMP_SNOOPING);
    }
    else
    {
        CTC_UNSET_FLAG(pia->flag, CTC_SCL_IGS_ACTION_FLAG_IGMP_SNOOPING);
    }

    return CTC_E_NONE;

}

int32
_sys_greatbelt_vlan_mapping_unmap_igs_action(uint8 lchip, ctc_vlan_mapping_t* p_vlan_mapping, sys_scl_action_t action)
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
    uint8  is_srvc_en     = 0;
    uint8  is_stats_en        = 0;
    uint8  is_vrfid_en        = 0;

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
    is_srvc_en    = CTC_FLAG_ISSET(p_vlan_mapping->action, CTC_VLAN_MAPPING_OUTPUT_SERVICE_ID);
    is_stats_en       = CTC_FLAG_ISSET(p_vlan_mapping->action, CTC_VLAN_MAPPING_FLAG_STATS_EN);
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
        p_vlan_mapping->is_working_path = (pia->aps.is_working_path == TRUE) ? 0 : 1;
        p_vlan_mapping->logic_src_port = pia->logic_port.logic_port;
    }

    if (is_srvc_en)
    {
        p_vlan_mapping->service_id = pia->service_id;
    }

    if (is_fid_en)
    {
        p_vlan_mapping->u3.fid = pia->fid;
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
    }

    /*for vpws*/
    if (is_nh_en)
    {
        p_vlan_mapping->u3.nh_id = pia->nh_id;
    }

    if (CTC_FLAG_ISSET(p_vlan_mapping->action, CTC_VLAN_MAPPING_OUTPUT_VLANPTR))
    {
        p_vlan_mapping->user_vlanptr = pia->user_vlanptr;
    }

    if (is_stats_en)
    {
        p_vlan_mapping->stats_id = pia->stats_id;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_vlan_check_vlan_mapping_action(uint8 lchip, ctc_direction_t dir, sys_vlan_mapping_tag_op_t* p_vlan_mapping)
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
_sys_greatbelt_vlan_mapping_get_scl_gid(uint8 lchip, uint8 type, uint8 dir, uint16 gport)
{
    uint32 gid = 0;
    uint8  lport = CTC_MAP_GPORT_TO_LPORT(gport);
    switch(type)
    {
    case SYS_SCL_KEY_TCAM_VLAN:
        gid = SYS_SCL_GROUP_ID_INNER_TCAM_VLAN_MAPPING_BASE + lport;
        break;
    case SYS_SCL_KEY_HASH_PORT:
    case SYS_SCL_KEY_HASH_PORT_CVLAN:
    case SYS_SCL_KEY_HASH_PORT_SVLAN:
    case SYS_SCL_KEY_HASH_PORT_2VLAN:
    case SYS_SCL_KEY_HASH_PORT_CVLAN_COS:
    case SYS_SCL_KEY_HASH_PORT_SVLAN_COS:
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
        break;
    }

    return gid;;

}

int32
sys_greatbelt_vlan_update_vlan_mapping_ext(uint8 lchip, uint16 gport, ctc_vlan_mapping_t* p_vlan_mapping)
{

    sys_scl_entry_t   scl;
    uint32  gid  = 0;
    sys_vlan_mapping_tag_op_t vlan_mapping_op;

    SYS_VLAN_MAPPING_DEBUG_FUNC();

    SYS_VLAN_MAPPING_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_vlan_mapping);

    if(!CTC_FLAG_ISSET(p_vlan_mapping->flag, CTC_VLAN_MAPPING_FLAG_USE_LOGIC_PORT))
    {
        CTC_GLOBAL_PORT_CHECK(gport);
    }

    SYS_VLAN_MAPPING_DEBUG_INFO("------vlan mapping----------\n");
    SYS_VLAN_MAPPING_DEBUG_INFO(" gport                     :0x%X\n", gport);
    SYS_VLAN_MAPPING_DEBUG_INFO(" key value                :0x%X\n", p_vlan_mapping->key);
    SYS_VLAN_MAPPING_DEBUG_INFO(" action                    :0x%X\n", p_vlan_mapping->action);
    SYS_VLAN_MAPPING_DEBUG_INFO(" flag                       :0x%X\n", p_vlan_mapping->flag);
    SYS_VLAN_MAPPING_DEBUG_INFO(" svid                       :%d\n", p_vlan_mapping->old_svid);
    SYS_VLAN_MAPPING_DEBUG_INFO(" cvid                       :%d\n", p_vlan_mapping->old_cvid);
    SYS_VLAN_MAPPING_DEBUG_INFO(" svlan end                 :%d\n", p_vlan_mapping->svlan_end);
    SYS_VLAN_MAPPING_DEBUG_INFO(" cvlan end                 :%d\n", p_vlan_mapping->cvlan_end);
    SYS_VLAN_MAPPING_DEBUG_INFO("-------mapping to-----------\n");
    SYS_VLAN_MAPPING_DEBUG_INFO(" new svid                  :%d\n", p_vlan_mapping->new_svid);
    SYS_VLAN_MAPPING_DEBUG_INFO(" new cvid                  :%d\n", p_vlan_mapping->new_cvid);
    SYS_VLAN_MAPPING_DEBUG_INFO(" fid                       :%d\n", p_vlan_mapping->u3.fid);
    vlan_mapping_op.ccos_sl = p_vlan_mapping->ccos_sl;
    vlan_mapping_op.cvid_sl = p_vlan_mapping->cvid_sl;
    vlan_mapping_op.ctag_op = p_vlan_mapping->ctag_op;
    vlan_mapping_op.scos_sl = p_vlan_mapping->scos_sl;
    vlan_mapping_op.svid_sl = p_vlan_mapping->svid_sl;
    vlan_mapping_op.stag_op = p_vlan_mapping->stag_op;
    CTC_ERROR_RETURN(_sys_greatbelt_vlan_check_vlan_mapping_action(lchip, CTC_INGRESS, &vlan_mapping_op));

    sal_memset(&scl, 0, sizeof(sys_scl_entry_t));

    CTC_ERROR_RETURN(_sys_greatbelt_vlan_mapping_build_igs_key(lchip, gport, p_vlan_mapping, &scl));

    scl.action.type = SYS_SCL_ACTION_INGRESS;

    CTC_ERROR_RETURN(_sys_greatbelt_vlan_mapping_build_igs_ad(lchip, p_vlan_mapping, &scl.action.u.igs_action));
    gid = _sys_greatbelt_vlan_mapping_get_scl_gid(lchip, scl.key.type, CTC_INGRESS, gport);
    SYS_VLAN_MAPPING_DEBUG_INFO(" group id :%u\n", gid);

    /* get entry id.*/
    CTC_ERROR_RETURN(sys_greatbelt_scl_get_entry_id(lchip, &scl, gid));

    /* update action */
    CTC_ERROR_RETURN(sys_greatbelt_scl_update_action(lchip, scl.entry_id, &scl.action, 1));


    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_vlan_mapping_set_igs_default_action(uint8 lchip, uint32 gport, ctc_vlan_mapping_t* p_vlan_mapping, uint8 is_add)
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
        CTC_ERROR_RETURN(_sys_greatbelt_vlan_mapping_build_igs_ad(lchip, p_vlan_mapping, &action.u.igs_action));
    }

    CTC_ERROR_RETURN(sys_greatbelt_scl_set_default_action(lchip, gport, &action));

    return CTC_E_NONE;
}


STATIC int32
_sys_greatbelt_vlan_add_vlan_mapping(uint8 lchip, uint16 gport, ctc_vlan_mapping_t* p_vlan_mapping, bool overwrite)
{
    sys_scl_entry_t   scl;
    uint32  gid  = 0;
    int32 ret = 0;
    sys_vlan_mapping_tag_op_t vlan_mapping_op;

    SYS_VLAN_MAPPING_DEBUG_FUNC();

    SYS_VLAN_MAPPING_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_vlan_mapping);
    if(!CTC_FLAG_ISSET(p_vlan_mapping->flag, CTC_VLAN_MAPPING_FLAG_USE_LOGIC_PORT))
    {
        CTC_GLOBAL_PORT_CHECK(gport);
    }
    CTC_MAX_VALUE_CHECK(p_vlan_mapping->tpid_index, 3);
    SYS_VLAN_MAPPING_DEBUG_INFO("------vlan mapping----------\n");
    SYS_VLAN_MAPPING_DEBUG_INFO(" gport                     :0x%X\n", gport);
    SYS_VLAN_MAPPING_DEBUG_INFO(" key value                :0x%X\n", p_vlan_mapping->key);
    SYS_VLAN_MAPPING_DEBUG_INFO(" action                    :0x%X\n", p_vlan_mapping->action);
    SYS_VLAN_MAPPING_DEBUG_INFO(" flag                       :0x%X\n", p_vlan_mapping->flag);
    SYS_VLAN_MAPPING_DEBUG_INFO(" svid                       :%d\n", p_vlan_mapping->old_svid);
    SYS_VLAN_MAPPING_DEBUG_INFO(" cvid                       :%d\n", p_vlan_mapping->old_cvid);
    SYS_VLAN_MAPPING_DEBUG_INFO(" svlan end                 :%d\n", p_vlan_mapping->svlan_end);
    SYS_VLAN_MAPPING_DEBUG_INFO(" cvlan end                 :%d\n", p_vlan_mapping->cvlan_end);
    SYS_VLAN_MAPPING_DEBUG_INFO("-------mapping to-----------\n");
    SYS_VLAN_MAPPING_DEBUG_INFO(" new svid                  :%d\n", p_vlan_mapping->new_svid);
    SYS_VLAN_MAPPING_DEBUG_INFO(" new cvid                  :%d\n", p_vlan_mapping->new_cvid);
    SYS_VLAN_MAPPING_DEBUG_INFO(" fid                       :%d\n", p_vlan_mapping->u3.fid);
    vlan_mapping_op.ccos_sl = p_vlan_mapping->ccos_sl;
    vlan_mapping_op.cvid_sl = p_vlan_mapping->cvid_sl;
    vlan_mapping_op.ctag_op = p_vlan_mapping->ctag_op;
    vlan_mapping_op.scos_sl = p_vlan_mapping->scos_sl;
    vlan_mapping_op.svid_sl = p_vlan_mapping->svid_sl;
    vlan_mapping_op.stag_op = p_vlan_mapping->stag_op;
    CTC_ERROR_RETURN(_sys_greatbelt_vlan_check_vlan_mapping_action(lchip, CTC_INGRESS, &vlan_mapping_op));

    if (CTC_FLAG_ISSET(p_vlan_mapping->flag, CTC_VLAN_MAPPING_FLAG_DEFAULT_ENTRY))
    {
        return _sys_greatbelt_vlan_mapping_set_igs_default_action(lchip, gport, p_vlan_mapping, 1);
    }

    sal_memset(&scl, 0, sizeof(sys_scl_entry_t));

    CTC_ERROR_RETURN(_sys_greatbelt_vlan_mapping_build_igs_key(lchip, gport, p_vlan_mapping, &scl));

    scl.action.type = SYS_SCL_ACTION_INGRESS;
    scl.action.action_flag = p_vlan_mapping->action;

    if (!overwrite)
    {
        CTC_ERROR_RETURN(_sys_greatbelt_vlan_mapping_build_igs_ad(lchip, p_vlan_mapping, &scl.action.u.igs_action));
        gid = _sys_greatbelt_vlan_mapping_get_scl_gid(lchip, scl.key.type, CTC_INGRESS, gport);
        SYS_VLAN_MAPPING_DEBUG_INFO(" group id :%u\n", gid);

        if (gid >= SYS_SCL_GROUP_ID_INNER_TCAM_VLAN_MAPPING_BASE) /* is vlan mapping tcam group */
        {
            ctc_scl_group_info_t port_group;
            sal_memset(&port_group, 0, sizeof(port_group));
            port_group.type = CTC_SCL_GROUP_TYPE_PORT;
            port_group.un.gport = gport;
            ret = sys_greatbelt_scl_create_group(lchip, gid, &port_group, 1);
            if ((ret < 0 )&& (ret != CTC_E_SCL_GROUP_EXIST))
            {
                return ret;
            }
        }

        CTC_ERROR_GOTO(sys_greatbelt_scl_add_entry(lchip, gid, &scl, 1), ret, ERROR1);
        CTC_ERROR_GOTO(sys_greatbelt_scl_install_entry(lchip, scl.entry_id, 1), ret, ERROR2);
    }
    else
    {
        CTC_ERROR_RETURN(_sys_greatbelt_vlan_mapping_build_igs_ad(lchip, p_vlan_mapping, &scl.action.u.igs_action));

        gid = _sys_greatbelt_vlan_mapping_get_scl_gid(lchip, scl.key.type, CTC_INGRESS, gport);
        SYS_VLAN_MAPPING_DEBUG_INFO(" group id :%u\n", gid);

        /* get entry id.*/
        CTC_ERROR_RETURN(sys_greatbelt_scl_get_entry_id(lchip, &scl, gid));

        /* update action */
        CTC_ERROR_RETURN(sys_greatbelt_scl_update_action(lchip, scl.entry_id, &scl.action, 1));
    }


    return CTC_E_NONE;

ERROR2:
    sys_greatbelt_scl_remove_entry(lchip, scl.entry_id, 1);

ERROR1:
    if (gid >= SYS_SCL_GROUP_ID_INNER_TCAM_VLAN_MAPPING_BASE)
    {
        sys_greatbelt_scl_destroy_group(lchip, gid, 1);
    }

    return ret;
}

int32
sys_greatbelt_vlan_add_vlan_mapping(uint8 lchip, uint16 gport, ctc_vlan_mapping_t* p_vlan_mapping)
{
    SYS_VLAN_MAPPING_INIT_CHECK(lchip);
    CTC_ERROR_RETURN(_sys_greatbelt_vlan_add_vlan_mapping(lchip, gport, p_vlan_mapping, FALSE));
    return CTC_E_NONE;
}

int32
sys_greatbelt_vlan_get_vlan_mapping(uint8 lchip, uint16 gport, ctc_vlan_mapping_t* p_vlan_mapping)
{
    int32 ret = CTC_E_NONE;
    sys_scl_entry_t   scl;
    uint32  gid  = 0;

    SYS_VLAN_MAPPING_DEBUG_FUNC();

    SYS_VLAN_MAPPING_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_vlan_mapping);

    if (!CTC_FLAG_ISSET(p_vlan_mapping->flag, CTC_VLAN_MAPPING_FLAG_USE_LOGIC_PORT))
    {
        CTC_GLOBAL_PORT_CHECK(gport);
        if (!CTC_IS_LINKAGG_PORT(gport))
        {
            ret = sys_greatbelt_chip_is_local(lchip, SYS_MAP_CTC_GPORT_TO_GCHIP(gport));
            if (FALSE == ret)
            {
                return CTC_E_CHIP_IS_REMOTE;
            }
        }
    }

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

    CTC_ERROR_RETURN(_sys_greatbelt_vlan_mapping_build_igs_key(lchip, gport, p_vlan_mapping, &scl));

    gid = _sys_greatbelt_vlan_mapping_get_scl_gid(lchip, scl.key.type, CTC_INGRESS, gport);

    SYS_VLAN_MAPPING_DEBUG_INFO(" group id :%u\n", gid);

    /* get scl entry*/
    CTC_ERROR_RETURN(sys_greatbelt_scl_get_entry(lchip, &scl, gid));

    CTC_ERROR_RETURN(_sys_greatbelt_vlan_mapping_unmap_igs_action(lchip, p_vlan_mapping, scl.action));

    return CTC_E_NONE;
}

int32
sys_greatbelt_vlan_update_vlan_mapping(uint8 lchip, uint16 gport, ctc_vlan_mapping_t* p_vlan_mapping)
{
    SYS_VLAN_MAPPING_INIT_CHECK(lchip);
    CTC_ERROR_RETURN(_sys_greatbelt_vlan_add_vlan_mapping(lchip, gport, p_vlan_mapping, TRUE));
    return CTC_E_NONE;
}

int32
sys_greatbelt_vlan_remove_vlan_mapping(uint8 lchip, uint16 gport, ctc_vlan_mapping_t* p_vlan_mapping)
{
    sys_scl_entry_t   scl;
    uint32            gid = 0;

    SYS_VLAN_MAPPING_DEBUG_FUNC();
    SYS_VLAN_MAPPING_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_vlan_mapping);
    if(!CTC_FLAG_ISSET(p_vlan_mapping->flag, CTC_VLAN_MAPPING_FLAG_USE_LOGIC_PORT))
    {
        CTC_GLOBAL_PORT_CHECK(gport);
    }

    SYS_VLAN_MAPPING_DEBUG_FUNC();
    SYS_VLAN_MAPPING_DEBUG_INFO("------vlan mapping----------\n");
    SYS_VLAN_MAPPING_DEBUG_INFO(" gport                      :0x%X\n", gport);
    SYS_VLAN_MAPPING_DEBUG_INFO(" key value                 :0x%X\n", p_vlan_mapping->key);
    SYS_VLAN_MAPPING_DEBUG_INFO(" flag                        :0x%X\n", p_vlan_mapping->flag);
    SYS_VLAN_MAPPING_DEBUG_INFO(" svid                        :%d\n", p_vlan_mapping->old_svid);
    SYS_VLAN_MAPPING_DEBUG_INFO(" cvid                        :%d\n", p_vlan_mapping->old_cvid);
    SYS_VLAN_MAPPING_DEBUG_INFO(" svlan end                  :%d\n", p_vlan_mapping->svlan_end);
    SYS_VLAN_MAPPING_DEBUG_INFO(" cvlan end                  :%d\n", p_vlan_mapping->cvlan_end);

    if (CTC_FLAG_ISSET(p_vlan_mapping->flag, CTC_VLAN_MAPPING_FLAG_DEFAULT_ENTRY))
    {
        return _sys_greatbelt_vlan_mapping_set_igs_default_action(lchip, gport, p_vlan_mapping, 0);
    }

    sal_memset(&scl, 0, sizeof(sys_scl_entry_t));
    CTC_ERROR_RETURN(_sys_greatbelt_vlan_mapping_build_igs_key(lchip, gport, p_vlan_mapping, &scl));


    gid = _sys_greatbelt_vlan_mapping_get_scl_gid(lchip, scl.key.type, CTC_INGRESS, gport);
    CTC_ERROR_RETURN(sys_greatbelt_scl_get_entry_id(lchip, &scl, gid));
    CTC_ERROR_RETURN(sys_greatbelt_scl_uninstall_entry(lchip, scl.entry_id, 1));
    CTC_ERROR_RETURN(sys_greatbelt_scl_remove_entry(lchip, scl.entry_id, 1));

    return CTC_E_NONE;

}

int32
sys_greatbelt_vlan_add_default_vlan_mapping(uint8 lchip, uint16 gport, ctc_vlan_miss_t* p_def_action)
{
    uint8 lport = 0;
    sys_scl_action_t action;

    SYS_VLAN_MAPPING_DEBUG_FUNC();
    SYS_VLAN_MAPPING_DEBUG_INFO("------vlan mapping----------\n");
    SYS_VLAN_MAPPING_DEBUG_INFO(" gport                      :%d\n", gport);
    SYS_VLAN_MAPPING_DEBUG_INFO(" flag value                 :%d\n", p_def_action->flag);

    SYS_VLAN_MAPPING_INIT_CHECK(lchip);

    SYS_VLAN_MAPPING_DEBUG_FUNC();
    CTC_PTR_VALID_CHECK(p_def_action);
    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);
    if(lport >= SYS_SCL_DEFAULT_ENTRY_PORT_NUM)
    {
        return CTC_E_INVALID_PARAM;
    }

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
        CTC_MAX_VALUE_CHECK(p_def_action->user_vlanptr,USER_VLAN_PTR_RANGE);
        action.u.igs_action.user_vlanptr = p_def_action->user_vlanptr;
        break;

    case CTC_VLAN_MISS_ACTION_APPEND_STAG:
        if (p_def_action->new_svid > CTC_MAX_VLAN_ID)
        {
            return CTC_E_VLAN_INVALID_VLAN_ID;
        }
        CTC_COS_RANGE_CHECK(p_def_action->new_scos);
        CTC_SET_FLAG(action.u.igs_action.flag , CTC_SCL_IGS_ACTION_FLAG_VLAN_EDIT);
        action.u.igs_action.vlan_edit.svid_new  =  p_def_action->new_svid;
        action.u.igs_action.vlan_edit.stag_op   =  CTC_VLAN_TAG_OP_ADD;
        action.u.igs_action.vlan_edit.svid_sl   =  CTC_VLAN_TAG_SL_NEW;
        action.u.igs_action.vlan_edit.scos_new = p_def_action->new_scos;
        action.u.igs_action.vlan_edit.scos_sl = p_def_action->scos_sl;
        break;

    default:
        return CTC_E_INVALID_PARAM;

    }
    action.type = CTC_SCL_ACTION_INGRESS;

    CTC_ERROR_RETURN(sys_greatbelt_scl_set_default_action(lchip, gport, &action));
    return CTC_E_NONE;
}


int32
sys_greatbelt_vlan_show_default_vlan_mapping(uint8 lchip, uint16 gport)
{

    uint8 lport = 0;
    sys_scl_action_t action;

    SYS_VLAN_MAPPING_INIT_CHECK(lchip);

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    if(lport >= SYS_SCL_DEFAULT_ENTRY_PORT_NUM)
    {
        return CTC_E_INVALID_PARAM;
    }

    sal_memset(&action, 0, sizeof(action));
    action.type = CTC_SCL_ACTION_INGRESS;

    CTC_ERROR_RETURN(sys_greatbelt_scl_get_default_action(lchip, gport, &action));

    SYS_VLAN_MAPPING_DEBUG_DUMP("Port 0x%.4x default Ingress action :  ",gport);
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

    CTC_ERROR_RETURN(sys_greatbelt_scl_get_default_action(lchip, gport, &action));

    SYS_VLAN_MAPPING_DEBUG_DUMP("Port 0x%.4x default Egress action  :  ",gport);
    if (CTC_FLAG_ISSET(action.u.egs_action.flag, CTC_SCL_EGS_ACTION_FLAG_DISCARD))
    {
        SYS_VLAN_MAPPING_DEBUG_DUMP("DISCARD\n");
    }
    else
    {
        SYS_VLAN_MAPPING_DEBUG_DUMP("Do Nothing\n");
    }
    return CTC_E_NONE;
}

int32
sys_greatbelt_vlan_remove_default_vlan_mapping(uint8 lchip, uint16 gport)
{

    uint8 lport = 0;
    sys_scl_action_t action;

    SYS_VLAN_MAPPING_INIT_CHECK(lchip);

    SYS_VLAN_MAPPING_DEBUG_FUNC();

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);
    if(lport >= SYS_SCL_DEFAULT_ENTRY_PORT_NUM)
    {
        return CTC_E_INVALID_PARAM;
    }

    sal_memset(&action, 0, sizeof(action));
    action.type = CTC_SCL_ACTION_INGRESS;

    CTC_ERROR_RETURN(sys_greatbelt_scl_set_default_action(lchip, gport, &action));
    return CTC_E_NONE;


}

int32
sys_greatbelt_vlan_remove_all_vlan_mapping_by_port(uint8 lchip, uint16 gport)
{

    SYS_VLAN_MAPPING_INIT_CHECK(lchip);
    SYS_VLAN_MAPPING_DEBUG_FUNC();
    CTC_GLOBAL_PORT_CHECK(gport);

    CTC_ERROR_RETURN(sys_greatbelt_scl_remove_all_inner_hash_vlan(lchip, gport, CTC_INGRESS));

    return CTC_E_NONE;
}

#define EGRESS_VLAN_MAPPING

STATIC int32
_sys_greatbelt_vlan_mapping_build_egs_ad(uint8 lchip, ctc_egress_vlan_mapping_t* p_vlan_mapping_in,
                                         ctc_scl_egs_action_t* pea)
{

    uint8  is_svid_en     = 0;
    uint8  is_cvid_en     = 0;
    uint8  is_scos_en     = 0;
    uint8  is_ccos_en     = 0;
    uint8  is_aging_en     = 0;

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

    is_svid_en    = CTC_FLAG_ISSET(p_vlan_mapping_in->action, CTC_EGRESS_VLAN_MAPPING_OUTPUT_SVID);
    is_cvid_en    = CTC_FLAG_ISSET(p_vlan_mapping_in->action, CTC_EGRESS_VLAN_MAPPING_OUTPUT_CVID);
    is_scos_en    = CTC_FLAG_ISSET(p_vlan_mapping_in->action, CTC_EGRESS_VLAN_MAPPING_OUTPUT_SCOS);
    is_ccos_en    = CTC_FLAG_ISSET(p_vlan_mapping_in->action, CTC_EGRESS_VLAN_MAPPING_OUTPUT_CCOS);

    if (p_vlan_mapping_in->stag_op || p_vlan_mapping_in->ctag_op)
    {
        CTC_SET_FLAG(pea->flag, CTC_SCL_EGS_ACTION_FLAG_VLAN_EDIT);
    }

    pea->vlan_edit.stag_op    = p_vlan_mapping_in->stag_op;
    pea->vlan_edit.svid_sl    = p_vlan_mapping_in->svid_sl;
    pea->vlan_edit.scos_sl    = p_vlan_mapping_in->scos_sl;

    pea->vlan_edit.ctag_op    = p_vlan_mapping_in->ctag_op;
    pea->vlan_edit.cvid_sl    = p_vlan_mapping_in->cvid_sl;
    pea->vlan_edit.ccos_sl    = p_vlan_mapping_in->ccos_sl;
    pea->vlan_edit.tpid_index = p_vlan_mapping_in->tpid_index;

    if (is_svid_en)
    {
        if (p_vlan_mapping_in->new_svid > CTC_MAX_VLAN_ID)
        {
            return CTC_E_VLAN_INVALID_VLAN_ID;
        }
        pea->vlan_edit.svid_new = p_vlan_mapping_in->new_svid;
    }

    if (is_scos_en)
    {
        CTC_COS_RANGE_CHECK(p_vlan_mapping_in->new_scos);
        pea->vlan_edit.scos_new = p_vlan_mapping_in->new_scos;
    }

    if (is_cvid_en)
    {
        if (p_vlan_mapping_in->new_cvid > CTC_MAX_VLAN_ID)
        {
            return CTC_E_VLAN_INVALID_VLAN_ID;
        }
        pea->vlan_edit.cvid_new = p_vlan_mapping_in->new_cvid;
    }

    if (is_ccos_en)
    {
        CTC_COS_RANGE_CHECK(p_vlan_mapping_in->new_ccos);
        pea->vlan_edit.ccos_new = p_vlan_mapping_in->new_ccos;
    }

    if (is_aging_en)
    {
        CTC_SET_FLAG(pea->flag, CTC_SCL_EGS_ACTION_FLAG_AGING);
    }

    if (CTC_QOS_COLOR_NONE !=p_vlan_mapping_in->color)
    {
        pea->color      = p_vlan_mapping_in->color;
        pea->priority   = p_vlan_mapping_in->priority;
        CTC_SET_FLAG(pea->flag, CTC_SCL_EGS_ACTION_FLAG_PRIORITY_AND_COLOR);
    }
    else
    {
        CTC_UNSET_FLAG(pea->flag, CTC_SCL_EGS_ACTION_FLAG_PRIORITY_AND_COLOR);
    }

    return CTC_E_NONE;

}


STATIC int32
_sys_greatbelt_vlan_mapping_build_egs_key(uint8 lchip, uint16 gport,
                                          ctc_egress_vlan_mapping_t* p_vlan_mapping_in,
                                          sys_scl_entry_t*   pc)
{
    uint16 old_cvid = 0;
    uint16 old_svid = 0;
    ctc_vlan_range_info_t vrange_info;
    bool is_svlan = FALSE;

    sal_memset(&vrange_info, 0, sizeof(ctc_vlan_range_info_t));

    SYS_VLAN_MAPPING_DEBUG_FUNC();

    switch (p_vlan_mapping_in->key
            & (CTC_EGRESS_VLAN_MAPPING_KEY_CVID | CTC_EGRESS_VLAN_MAPPING_KEY_SVID
               | CTC_EGRESS_VLAN_MAPPING_KEY_CTAG_COS | CTC_EGRESS_VLAN_MAPPING_KEY_STAG_COS))
    {
    case (CTC_EGRESS_VLAN_MAPPING_KEY_CVID):
        SYS_VLAN_ID_VALID_CHECK(p_vlan_mapping_in->old_cvid);
        old_cvid = p_vlan_mapping_in->old_cvid;

        SYS_VLAN_MAPPING_GET_MAX_CVLAN(CTC_EGRESS, p_vlan_mapping_in, old_cvid);

        pc->key.u.hash_port_cvlan_key.gport = gport;
        pc->key.u.hash_port_cvlan_key.cvlan = old_cvid;
        pc->key.u.hash_port_cvlan_key.dir = CTC_EGRESS;
        pc->key.type  = SYS_SCL_KEY_HASH_PORT_CVLAN;

        if (CTC_FLAG_ISSET(p_vlan_mapping_in->flag, CTC_VLAN_MAPPING_FLAG_USE_LOGIC_PORT))
        {
            pc->key.u.hash_port_cvlan_key.gport_type = 2;
        }
        break;

    case (CTC_EGRESS_VLAN_MAPPING_KEY_SVID):
        SYS_VLAN_ID_VALID_CHECK(p_vlan_mapping_in->old_svid);
        old_svid = p_vlan_mapping_in->old_svid;

        SYS_VLAN_MAPPING_GET_MAX_SVLAN(CTC_EGRESS, p_vlan_mapping_in, old_svid);

        pc->key.u.hash_port_svlan_key.gport = gport;
        pc->key.u.hash_port_svlan_key.svlan = old_svid;
        pc->key.u.hash_port_svlan_key.dir = CTC_EGRESS;
        pc->key.type  = SYS_SCL_KEY_HASH_PORT_SVLAN;
        if (CTC_FLAG_ISSET(p_vlan_mapping_in->flag, CTC_VLAN_MAPPING_FLAG_USE_LOGIC_PORT))
        {
            pc->key.u.hash_port_svlan_key.gport_type = 2;
        }
        break;

    case (CTC_EGRESS_VLAN_MAPPING_KEY_CVID | CTC_EGRESS_VLAN_MAPPING_KEY_SVID):
        SYS_VLAN_ID_VALID_CHECK(p_vlan_mapping_in->old_cvid);
        SYS_VLAN_ID_VALID_CHECK(p_vlan_mapping_in->old_svid);
        old_cvid = p_vlan_mapping_in->old_cvid;
        old_svid = p_vlan_mapping_in->old_svid;

        SYS_VLAN_MAPPING_GET_MAX_CVLAN(CTC_EGRESS, p_vlan_mapping_in, old_cvid);
        SYS_VLAN_MAPPING_GET_MAX_SVLAN(CTC_EGRESS, p_vlan_mapping_in, old_svid);

        pc->key.u.hash_port_2vlan_key.gport = gport;
        pc->key.u.hash_port_2vlan_key.cvlan  = old_cvid;
        pc->key.u.hash_port_2vlan_key.svlan  = old_svid;
        pc->key.u.hash_port_2vlan_key.dir = CTC_EGRESS;

        pc->key.type  = SYS_SCL_KEY_HASH_PORT_2VLAN;

        if (CTC_FLAG_ISSET(p_vlan_mapping_in->flag, CTC_VLAN_MAPPING_FLAG_USE_LOGIC_PORT))
        {
            pc->key.u.hash_port_2vlan_key.gport_type = 2;
        }
        break;

    case (CTC_EGRESS_VLAN_MAPPING_KEY_CVID | CTC_EGRESS_VLAN_MAPPING_KEY_CTAG_COS):
        SYS_VLAN_ID_VALID_CHECK(p_vlan_mapping_in->old_cvid);
        CTC_COS_RANGE_CHECK(p_vlan_mapping_in->old_ccos);
        old_cvid = p_vlan_mapping_in->old_cvid;

        SYS_VLAN_MAPPING_GET_MAX_CVLAN(CTC_EGRESS, p_vlan_mapping_in, old_cvid);

        pc->key.u.hash_port_cvlan_cos_key.gport = gport;
        pc->key.u.hash_port_cvlan_cos_key.cvlan = old_cvid;
        pc->key.u.hash_port_cvlan_cos_key.ctag_cos= p_vlan_mapping_in->old_ccos;
        pc->key.u.hash_port_cvlan_cos_key.dir = CTC_EGRESS;

        pc->key.type  = SYS_SCL_KEY_HASH_PORT_CVLAN_COS;

        if (CTC_FLAG_ISSET(p_vlan_mapping_in->flag, CTC_VLAN_MAPPING_FLAG_USE_LOGIC_PORT))
        {
            pc->key.u.hash_port_cvlan_cos_key.gport_type = 2;
        }
        break;

    case (CTC_EGRESS_VLAN_MAPPING_KEY_SVID | CTC_EGRESS_VLAN_MAPPING_KEY_STAG_COS):
        SYS_VLAN_ID_VALID_CHECK(p_vlan_mapping_in->old_svid);
        CTC_COS_RANGE_CHECK(p_vlan_mapping_in->old_scos);
        old_svid = p_vlan_mapping_in->old_svid;

        SYS_VLAN_MAPPING_GET_MAX_SVLAN(CTC_EGRESS, p_vlan_mapping_in, old_svid);

        pc->key.u.hash_port_svlan_cos_key.gport = gport;
        pc->key.u.hash_port_svlan_cos_key.svlan = old_svid;
        pc->key.u.hash_port_svlan_cos_key.stag_cos= p_vlan_mapping_in->old_scos;
        pc->key.u.hash_port_svlan_cos_key.dir = CTC_EGRESS;

        pc->key.type  = SYS_SCL_KEY_HASH_PORT_SVLAN_COS;

        if (CTC_FLAG_ISSET(p_vlan_mapping_in->flag, CTC_VLAN_MAPPING_FLAG_USE_LOGIC_PORT))
        {
            pc->key.u.hash_port_svlan_cos_key.gport_type = 2;
        }
        break;

    case CTC_EGRESS_VLAN_MAPPING_KEY_NONE:
        pc->key.u.hash_port_key.gport = gport;

        pc->key.type  = SYS_SCL_KEY_HASH_PORT;
        pc->key.u.hash_port_key.dir = CTC_EGRESS;

        if (CTC_FLAG_ISSET(p_vlan_mapping_in->flag, CTC_VLAN_MAPPING_FLAG_USE_LOGIC_PORT))
        {
            pc->key.u.hash_port_key.gport_type = 2;
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
_sys_greatbelt_vlan_mapping_unmap_egs_action(uint8 lchip, ctc_egress_vlan_mapping_t* p_vlan_mapping_in,
                                                            sys_scl_action_t* action)
{
    /*original vlan mapping action flag*/
    uint32 action_flag = 0;
    ctc_scl_egs_action_t* pea = NULL;

    uint8  is_svid_en     = 0;
    uint8  is_cvid_en     = 0;
    uint8  is_scos_en     = 0;
    uint8  is_ccos_en     = 0;

    SYS_VLAN_MAPPING_DEBUG_FUNC();

    action_flag = action->action_flag;
    pea = &(action->u.egs_action);
    p_vlan_mapping_in->action = action_flag;

    is_svid_en    = CTC_FLAG_ISSET(p_vlan_mapping_in->action, CTC_EGRESS_VLAN_MAPPING_OUTPUT_SVID);
    is_cvid_en    = CTC_FLAG_ISSET(p_vlan_mapping_in->action, CTC_EGRESS_VLAN_MAPPING_OUTPUT_CVID);
    is_scos_en    = CTC_FLAG_ISSET(p_vlan_mapping_in->action, CTC_EGRESS_VLAN_MAPPING_OUTPUT_SCOS);
    is_ccos_en    = CTC_FLAG_ISSET(p_vlan_mapping_in->action, CTC_EGRESS_VLAN_MAPPING_OUTPUT_CCOS);

    p_vlan_mapping_in->stag_op = pea->vlan_edit.stag_op;
    p_vlan_mapping_in->svid_sl = pea->vlan_edit.svid_sl;
    p_vlan_mapping_in->scos_sl = pea->vlan_edit.scos_sl;

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

    if (is_cvid_en)
    {
        p_vlan_mapping_in->new_cvid = pea->vlan_edit.cvid_new;
    }

    if (is_ccos_en)
    {
        p_vlan_mapping_in->new_ccos = pea->vlan_edit.ccos_new;
    }

    if (CTC_QOS_COLOR_NONE != pea->color)
    {
        p_vlan_mapping_in->color = pea->color;
        p_vlan_mapping_in->priority = pea->priority;
    }

    return CTC_E_NONE;
}


STATIC int32
_sys_greatbelt_vlan_mapping_set_egs_default_action(uint8 lchip, uint32 gport, ctc_egress_vlan_mapping_t* p_vlan_mapping, uint8 is_add)
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
        CTC_ERROR_RETURN(_sys_greatbelt_vlan_mapping_build_egs_ad(lchip, p_vlan_mapping, &action.u.egs_action));
    }

    CTC_ERROR_RETURN(sys_greatbelt_scl_set_default_action(lchip, gport, &action));

    return CTC_E_NONE;
}


int32
sys_greatbelt_vlan_add_egress_vlan_mapping(uint8 lchip, uint16 gport, ctc_egress_vlan_mapping_t* p_vlan_mapping)
{
    sys_scl_entry_t   scl;
    sys_vlan_mapping_tag_op_t vlan_mapping_op;

    SYS_VLAN_MAPPING_DEBUG_FUNC();

    SYS_VLAN_MAPPING_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_vlan_mapping);
    CTC_MAX_VALUE_CHECK(p_vlan_mapping->tpid_index, 3);

    if(!CTC_FLAG_ISSET(p_vlan_mapping->flag, CTC_VLAN_MAPPING_FLAG_USE_LOGIC_PORT))
    {
        CTC_GLOBAL_PORT_CHECK(gport);
    }

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

    /*egress action check missed..*/
    sal_memset(&scl, 0, sizeof(sys_scl_entry_t));

    vlan_mapping_op.ccos_sl = p_vlan_mapping->ccos_sl;
    vlan_mapping_op.cvid_sl = p_vlan_mapping->cvid_sl;
    vlan_mapping_op.ctag_op = p_vlan_mapping->ctag_op;
    vlan_mapping_op.scos_sl = p_vlan_mapping->scos_sl;
    vlan_mapping_op.svid_sl = p_vlan_mapping->svid_sl;
    vlan_mapping_op.stag_op = p_vlan_mapping->stag_op;
    CTC_ERROR_RETURN(_sys_greatbelt_vlan_check_vlan_mapping_action(lchip, CTC_EGRESS, &vlan_mapping_op));

    if (CTC_FLAG_ISSET(p_vlan_mapping->flag, CTC_VLAN_MAPPING_FLAG_DEFAULT_ENTRY))
    {
        return _sys_greatbelt_vlan_mapping_set_egs_default_action(lchip, gport, p_vlan_mapping, 1);
    }

    CTC_ERROR_RETURN(_sys_greatbelt_vlan_mapping_build_egs_key(lchip, gport, p_vlan_mapping, &scl));

    scl.action.type  = SYS_SCL_ACTION_EGRESS;
    scl.action.action_flag = p_vlan_mapping->action;
    CTC_ERROR_RETURN(_sys_greatbelt_vlan_mapping_build_egs_ad(lchip, p_vlan_mapping, &scl.action.u.egs_action));


    CTC_ERROR_RETURN(sys_greatbelt_scl_add_entry(lchip, SYS_SCL_GROUP_ID_INNER_HASH_VLAN_MAPPING_EGS, &scl, 1));
    CTC_ERROR_RETURN(sys_greatbelt_scl_install_entry(lchip, scl.entry_id, 1));

    return CTC_E_NONE;

}

int32
sys_greatbelt_vlan_remove_egress_vlan_mapping(uint8 lchip, uint16 gport, ctc_egress_vlan_mapping_t* p_vlan_mapping)
{
    uint32            gid = 0;
    sys_scl_entry_t   scl;

    SYS_VLAN_MAPPING_DEBUG_FUNC();
    SYS_VLAN_MAPPING_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_vlan_mapping);

    if(!CTC_FLAG_ISSET(p_vlan_mapping->flag, CTC_VLAN_MAPPING_FLAG_USE_LOGIC_PORT))
    {
        CTC_GLOBAL_PORT_CHECK(gport);
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
        return _sys_greatbelt_vlan_mapping_set_egs_default_action(lchip, gport, p_vlan_mapping, 0);
    }

    sal_memset(&scl, 0, sizeof(sys_scl_entry_t));
    CTC_ERROR_RETURN(_sys_greatbelt_vlan_mapping_build_egs_key(lchip, gport, p_vlan_mapping, &scl));

    gid = _sys_greatbelt_vlan_mapping_get_scl_gid(lchip, scl.key.type, CTC_EGRESS, gport);
    CTC_ERROR_RETURN(sys_greatbelt_scl_get_entry_id(lchip, &scl, gid));
    CTC_ERROR_RETURN(sys_greatbelt_scl_uninstall_entry(lchip, scl.entry_id, 1));
    CTC_ERROR_RETURN(sys_greatbelt_scl_remove_entry(lchip, scl.entry_id, 1));

    return CTC_E_NONE;

}

int32
sys_greatbelt_vlan_get_egress_vlan_mapping(uint8 lchip, uint16 gport, ctc_egress_vlan_mapping_t* p_vlan_mapping)
{
    int32 ret = CTC_E_NONE;
    sys_scl_entry_t   scl;
    uint32  gid  = 0;

    SYS_VLAN_MAPPING_DEBUG_FUNC();

    SYS_VLAN_MAPPING_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_vlan_mapping);

    if (!CTC_FLAG_ISSET(p_vlan_mapping->flag, CTC_VLAN_MAPPING_FLAG_USE_LOGIC_PORT))
    {
        CTC_GLOBAL_PORT_CHECK(gport);
        if (!CTC_IS_LINKAGG_PORT(gport))
        {
            ret = sys_greatbelt_chip_is_local(lchip, SYS_MAP_CTC_GPORT_TO_GCHIP(gport));
            if (FALSE == ret)
            {
                return CTC_E_CHIP_IS_REMOTE;
            }
        }
    }

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

    CTC_ERROR_RETURN(_sys_greatbelt_vlan_mapping_build_egs_key(lchip, gport, p_vlan_mapping, &scl));

    gid = _sys_greatbelt_vlan_mapping_get_scl_gid(lchip, scl.key.type, CTC_EGRESS, gport);
    SYS_VLAN_MAPPING_DEBUG_INFO(" group id :%u\n", gid);

    /* get scl entry*/
    CTC_ERROR_RETURN(sys_greatbelt_scl_get_entry(lchip, &scl, gid));
    _sys_greatbelt_vlan_mapping_unmap_egs_action(lchip, p_vlan_mapping, &scl.action);

    return CTC_E_NONE;
}


int32
sys_greatbelt_vlan_add_default_egress_vlan_mapping(uint8 lchip, uint16 gport, ctc_vlan_miss_t* p_def_action)
{
    uint8 lport = 0;
    sys_scl_action_t action;

    SYS_VLAN_MAPPING_INIT_CHECK(lchip);

    SYS_VLAN_MAPPING_DEBUG_FUNC();
    CTC_PTR_VALID_CHECK(p_def_action);

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);
    if(lport >= SYS_SCL_DEFAULT_ENTRY_PORT_NUM)
    {
        return CTC_E_INVALID_PARAM;
    }

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
    CTC_ERROR_RETURN(sys_greatbelt_scl_set_default_action(lchip, gport, &action));

    return CTC_E_NONE;
}


int32
sys_greatbelt_vlan_remove_default_egress_vlan_mapping(uint8 lchip, uint16 gport)
{
    uint8 lport = 0;
    sys_scl_action_t action;

    SYS_VLAN_MAPPING_INIT_CHECK(lchip);

    SYS_VLAN_MAPPING_DEBUG_FUNC();

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);
    if(lport >= SYS_SCL_DEFAULT_ENTRY_PORT_NUM)
    {
        return CTC_E_INVALID_PARAM;
    }

    sal_memset(&action, 0, sizeof(action));

    action.type = CTC_SCL_ACTION_EGRESS;
    CTC_ERROR_RETURN(sys_greatbelt_scl_set_default_action(lchip, gport, &action));
    return CTC_E_NONE;
}

int32
sys_greatbelt_vlan_remove_all_egress_vlan_mapping_by_port(uint8 lchip, uint16 gport)
{
    SYS_VLAN_MAPPING_INIT_CHECK(lchip);
    SYS_VLAN_MAPPING_DEBUG_FUNC();

    CTC_GLOBAL_PORT_CHECK(gport);

    CTC_ERROR_RETURN(sys_greatbelt_scl_remove_all_inner_hash_vlan(lchip,  gport, CTC_EGRESS));

    return CTC_E_NONE;
}

int32
sys_greatbelt_vlan_mapping_init(uint8 lchip)
{
    LCHIP_CHECK(lchip);
    if (NULL != p_gb_vlan_mapping_master[lchip])
    {
        return CTC_E_NONE;
    }

    MALLOC_ZERO(MEM_VLAN_MODULE, p_gb_vlan_mapping_master[lchip], sizeof(sys_vlan_mapping_master_t));

    if (NULL == p_gb_vlan_mapping_master[lchip])
    {
        return CTC_E_NO_MEMORY;
    }

    SYS_VLAN_MAPPING_CREATE_LOCK(lchip);

    return CTC_E_NONE;
}

int32
sys_greatbelt_vlan_mapping_deinit(uint8 lchip)
{
    LCHIP_CHECK(lchip);
    if (NULL == p_gb_vlan_mapping_master[lchip])
    {
        return CTC_E_NONE;
    }

    sal_mutex_destroy(p_gb_vlan_mapping_master[lchip]->mutex);
    mem_free(p_gb_vlan_mapping_master[lchip]);

    return CTC_E_NONE;
}


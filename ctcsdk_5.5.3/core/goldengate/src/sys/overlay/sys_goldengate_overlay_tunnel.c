/**
 @file sys_goldengate_overlay_tunnel.c

 @date 2013-10-25

 @version v2.0
*/

/****************************************************************************
 *
* Header Files
*
****************************************************************************/
#include "ctc_error.h"
#include "ctc_common.h"
#include "ctc_const.h"
#include "ctc_hash.h"
#include "ctc_parser.h"

#include "sys_goldengate_overlay_tunnel.h"
#include "sys_goldengate_common.h"
#include "sys_goldengate_opf.h"
#include "sys_goldengate_chip.h"
#include "sys_goldengate_scl.h"
#include "sys_goldengate_vlan.h"
#include "sys_goldengate_nexthop_api.h"
#include "sys_goldengate_port.h"
#include "sys_goldengate_stats.h"
#include "sys_goldengate_qos_policer.h"
#include "sys_goldengate_l3if.h"
#include "sys_goldengate_register.h"
#include "sys_goldengate_wb_common.h"
#include "sys_goldengate_l2_fdb.h"

#include "goldengate/include/drv_enum.h"
#include "goldengate/include/drv_io.h"
#include "goldengate/include/drv_common.h"

/****************************************************************************
 *
* Defines and Macros
*
*****************************************************************************/

#define SYS_OVERLAY_TUNNEL_MIN_INNER_FID      (0x1FFE)   /*reserve fid*/
#define SYS_OVERLAY_TUNNEL_MAX_INNER_FID      (0x3FFE)
#define SYS_OVERLAT_TUNNEL_MAX_IP_INDEX       4

#define SYS_OVERLAY_INIT_CHECK(lchip) \
    do { \
        SYS_LCHIP_CHECK_ACTIVE(lchip); \
        if (NULL == p_gg_overlay_tunnel_master[lchip]) \
        { \
            return CTC_E_OAM_NOT_INIT; \
        } \
    } while (0)

#define SYS_OVERLAY_CREATE_LOCK(lchip)                              \
    do                                                              \
    {                                                               \
        sal_mutex_create(&p_gg_overlay_tunnel_master[lchip]->mutex);\
        if (NULL == p_gg_overlay_tunnel_master[lchip]->mutex)       \
        {                                                           \
            CTC_ERROR_RETURN(CTC_E_FAIL_CREATE_MUTEX);              \
        }                                                           \
    } while (0)

#define SYS_OVERLAY_LOCK(lchip) \
    sal_mutex_lock(p_gg_overlay_tunnel_master[lchip]->mutex)

#define SYS_OVERLAY_UNLOCK(lchip) \
    sal_mutex_unlock(p_gg_overlay_tunnel_master[lchip]->mutex)

#define CTC_ERROR_RETURN_OVERLAY_UNLOCK(op) \
    do \
    { \
        int32 rv = (op); \
        if (rv < 0) \
        { \
            sal_mutex_unlock(p_gg_overlay_tunnel_master[lchip]->mutex); \
            return rv; \
        } \
    } while (0)

#define SYS_OVERLAY_TUNNEL_TYEP_CHECK(PARAM)                \
{                                                           \
    if ((PARAM->type != CTC_OVERLAY_TUNNEL_TYPE_NVGRE) &&   \
        (PARAM->type != CTC_OVERLAY_TUNNEL_TYPE_VXLAN)&& \
        (PARAM->type != CTC_OVERLAY_TUNNEL_TYPE_GENEVE))     \
        return CTC_E_OVERLAY_TUNNEL_INVALID_TYPE;           \
}

#define SYS_OVERLAY_TUNNEL_VNI_CHECK(VNI)           \
{                                                   \
    if ( VNI > 0xFFFFFF )               \
        return CTC_E_OVERLAY_TUNNEL_INVALID_VN_ID;  \
}

#define SYS_OVERLAY_TUNNEL_FID_CHECK(FID)           \
{                                                   \
    if ( FID > 0x3FFF )                 \
        return CTC_E_OVERLAY_TUNNEL_INVALID_FID;    \
}


#define SYS_OVERLAY_TUNNEL_LOGIC_PORT_CHECK(LOGIC_PORT)    \
{                                                          \
    if ( LOGIC_PORT > 0x3FFF )                             \
        return CTC_E_INVALID_LOGIC_PORT;                   \
}

#define SYS_OVERLAY_TUNNEL_SCL_ID_CHECK(SCL_ID)    \
{                                                          \
    if ( SCL_ID > 1 )                             \
        return CTC_E_INVALID_PARAM;                   \
}
#define SYS_OVERLAY_TUNNEL_INVALID_IPDA_INDEX     0xFF

enum sys_overlay_tunnel_packet_type_e
{
    SYS_OVERLAY_TUNNEL_PACKET_TYPE_UC,
    SYS_OVERLAY_TUNNEL_PACKET_TYPE_MC,
    SYS_OVERLAY_TUNNEL_PACKET_TYPE_MAX,
};
typedef enum sys_overlay_tunnel_packet_type_e sys_overlay_tunnel_packet_type_t;


struct sys_overlay_tunnel_master_s
{
    uint8 by_index[CTC_OVERLAY_TUNNEL_TYPE_MAX];

    uint32 mode0_key[CTC_OVERLAY_TUNNEL_TYPE_MAX][MAX_CTC_IP_VER][SYS_OVERLAY_TUNNEL_PACKET_TYPE_MAX]; /* no sa key*/
    uint32 mode1_key[CTC_OVERLAY_TUNNEL_TYPE_MAX][MAX_CTC_IP_VER][SYS_OVERLAY_TUNNEL_PACKET_TYPE_MAX]; /* with sa key*/

    uint32 ipda_index_cnt[SYS_OVERLAT_TUNNEL_MAX_IP_INDEX];
    sal_mutex_t* mutex;
};
typedef struct sys_overlay_tunnel_master_s sys_overlay_tunnel_master_t;

sys_overlay_tunnel_master_t* p_gg_overlay_tunnel_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};

/****************************************************************************
 *
* Function
*
*****************************************************************************/
STATIC uint8 _sys_goldengate_overlay_tunnel_with_sa(uint8 lchip, ctc_overlay_tunnel_param_t* p_tunnel_param)
{
    ctc_global_overlay_decap_mode_t decap_mode;

    sal_memset(&decap_mode, 0, sizeof(decap_mode));
    decap_mode.scl_id = p_tunnel_param->scl_id;
    sys_goldengate_global_ctl_get(lchip, CTC_GLOBAL_OVERLAY_DECAP_MODE, &decap_mode);

    return !((p_tunnel_param->type == CTC_OVERLAY_TUNNEL_TYPE_NVGRE)? decap_mode.nvgre_mode : decap_mode.vxlan_mode);
}
STATIC uint8
_sys_goldengate_overlay_tunnel_is_ipda_index(uint8 lchip, ctc_overlay_tunnel_param_t* p_tunnel_param)
{
    uint8 ip_ver = CTC_IP_VER_4;
    uint8 by_index = 0;
    uint8 with_sa  = 0;
    uint8 type = 0;

    type = (CTC_OVERLAY_TUNNEL_TYPE_GENEVE == p_tunnel_param->type)? CTC_OVERLAY_TUNNEL_TYPE_VXLAN : p_tunnel_param->type;
    ip_ver = CTC_FLAG_ISSET(p_tunnel_param->flag, CTC_OVERLAY_TUNNEL_FLAG_IP_VER_6)? CTC_IP_VER_6 : CTC_IP_VER_4;
    by_index = p_gg_overlay_tunnel_master[lchip]->by_index[type];
    with_sa = _sys_goldengate_overlay_tunnel_with_sa(lchip, p_tunnel_param);
    if ( ip_ver == CTC_IP_VER_4 && by_index && with_sa
        && (!((p_tunnel_param->ipda.ipv4 & 0xF0000000) == 0xE0000000))/*uc*/
        && (!CTC_FLAG_ISSET(p_tunnel_param->flag, CTC_OVERLAY_TUNNEL_FLAG_USE_FLEX))/*hash*/
        && (!CTC_FLAG_ISSET(p_tunnel_param->flag, CTC_OVERLAY_TUNNEL_FLAG_DEFAULT_ENTRY)))
    {
        return 1;
    }
    else
    {
        return 0;
    }
}

STATIC int32
_sys_goldengate_overlay_tunnel_add_ipda_index(uint8 lchip, uint8 type, uint32 ip_addr , uint32* ip_index)
{
    ds_t ipe_vxlan_nvgre_ipda_ctl;
    uint32 cmd    = 0;
    uint32 valid[SYS_OVERLAT_TUNNEL_MAX_IP_INDEX];
    uint32 isVxlan[SYS_OVERLAT_TUNNEL_MAX_IP_INDEX];
    uint32 ipda[SYS_OVERLAT_TUNNEL_MAX_IP_INDEX];
    uint8 i = 0;
    uint8 is_vxlan = ((type == CTC_OVERLAY_TUNNEL_TYPE_VXLAN)||(type == CTC_OVERLAY_TUNNEL_TYPE_GENEVE)) ? 1 : 0;

    SYS_OVERLAY_TUNNEL_DBG_FUNC();

    *ip_index = SYS_OVERLAY_TUNNEL_INVALID_IPDA_INDEX;
    sal_memset(&ipe_vxlan_nvgre_ipda_ctl, 0, sizeof(ipe_vxlan_nvgre_ipda_ctl));

    cmd = DRV_IOR(IpeVxlanNvgreIpdaCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_vxlan_nvgre_ipda_ctl));

    GetIpeVxlanNvgreIpdaCtl(A, array_0_valid_f, ipe_vxlan_nvgre_ipda_ctl, &valid[0]);
    GetIpeVxlanNvgreIpdaCtl(A, array_1_valid_f, ipe_vxlan_nvgre_ipda_ctl, &valid[1]);
    GetIpeVxlanNvgreIpdaCtl(A, array_2_valid_f, ipe_vxlan_nvgre_ipda_ctl, &valid[2]);
    GetIpeVxlanNvgreIpdaCtl(A, array_3_valid_f, ipe_vxlan_nvgre_ipda_ctl, &valid[3]);
    GetIpeVxlanNvgreIpdaCtl(A, array_0_isVxlan_f, ipe_vxlan_nvgre_ipda_ctl, &isVxlan[0]);
    GetIpeVxlanNvgreIpdaCtl(A, array_1_isVxlan_f, ipe_vxlan_nvgre_ipda_ctl, &isVxlan[1]);
    GetIpeVxlanNvgreIpdaCtl(A, array_2_isVxlan_f, ipe_vxlan_nvgre_ipda_ctl, &isVxlan[2]);
    GetIpeVxlanNvgreIpdaCtl(A, array_3_isVxlan_f, ipe_vxlan_nvgre_ipda_ctl, &isVxlan[3]);
    GetIpeVxlanNvgreIpdaCtl(A, array_0_ipDa_f, ipe_vxlan_nvgre_ipda_ctl, &ipda[0]);
    GetIpeVxlanNvgreIpdaCtl(A, array_1_ipDa_f, ipe_vxlan_nvgre_ipda_ctl, &ipda[1]);
    GetIpeVxlanNvgreIpdaCtl(A, array_2_ipDa_f, ipe_vxlan_nvgre_ipda_ctl, &ipda[2]);
    GetIpeVxlanNvgreIpdaCtl(A, array_3_ipDa_f, ipe_vxlan_nvgre_ipda_ctl, &ipda[3]);

    for ( i = 0 ; i < SYS_OVERLAT_TUNNEL_MAX_IP_INDEX ; i++)
    {
        if (valid[i] && ( is_vxlan == isVxlan[i] ) && ( ip_addr == ipda[i] )) /*return index if hit*/
        {
            *ip_index = i;
            SYS_OVERLAY_LOCK(lchip);
            p_gg_overlay_tunnel_master[lchip]->ipda_index_cnt[*ip_index]++;
            SYS_OVERLAY_UNLOCK(lchip);
            goto END;
        }
    }

    for ( i = 0 ; i < SYS_OVERLAT_TUNNEL_MAX_IP_INDEX ; i++)
    {
        if (!valid[i])
        {
            *ip_index = i;
            break;
        }
    }

    if (SYS_OVERLAY_TUNNEL_INVALID_IPDA_INDEX != *ip_index ) /* search a valid position */
    {
        SYS_OVERLAY_LOCK(lchip);
        p_gg_overlay_tunnel_master[lchip]->ipda_index_cnt[*ip_index] = 1;
        SYS_OVERLAY_UNLOCK(lchip);
        switch ( *ip_index)
        {
            case 0 :
                SetIpeVxlanNvgreIpdaCtl(V, array_0_valid_f, ipe_vxlan_nvgre_ipda_ctl, 1);
                SetIpeVxlanNvgreIpdaCtl(V, array_0_isVxlan_f, ipe_vxlan_nvgre_ipda_ctl, is_vxlan);
                SetIpeVxlanNvgreIpdaCtl(V, array_0_ipDa_f, ipe_vxlan_nvgre_ipda_ctl, ip_addr);
                break;

            case 1 :
                SetIpeVxlanNvgreIpdaCtl(V, array_1_valid_f, ipe_vxlan_nvgre_ipda_ctl, 1);
                SetIpeVxlanNvgreIpdaCtl(V, array_1_isVxlan_f, ipe_vxlan_nvgre_ipda_ctl, is_vxlan);
                SetIpeVxlanNvgreIpdaCtl(V, array_1_ipDa_f, ipe_vxlan_nvgre_ipda_ctl, ip_addr);
                break;

            case 2 :
                SetIpeVxlanNvgreIpdaCtl(V, array_2_valid_f, ipe_vxlan_nvgre_ipda_ctl, 1);
                SetIpeVxlanNvgreIpdaCtl(V, array_2_isVxlan_f, ipe_vxlan_nvgre_ipda_ctl, is_vxlan);
                SetIpeVxlanNvgreIpdaCtl(V, array_2_ipDa_f, ipe_vxlan_nvgre_ipda_ctl, ip_addr);
                break;

            case 3 :
                SetIpeVxlanNvgreIpdaCtl(V, array_3_valid_f, ipe_vxlan_nvgre_ipda_ctl, 1);
                SetIpeVxlanNvgreIpdaCtl(V, array_3_isVxlan_f, ipe_vxlan_nvgre_ipda_ctl, is_vxlan);
                SetIpeVxlanNvgreIpdaCtl(V, array_3_ipDa_f, ipe_vxlan_nvgre_ipda_ctl, ip_addr);
                break;
        }

        cmd = DRV_IOW(IpeVxlanNvgreIpdaCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_vxlan_nvgre_ipda_ctl));
    }

END:
    SYS_OVERLAY_TUNNEL_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "ipda index = %d, isvxlan = %d\n", *ip_index, is_vxlan);

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_overlay_tunnel_del_ipda_index(uint8 lchip, uint8 type, uint32 ip_addr , uint32* ip_index)
{
    ds_t ipe_vxlan_nvgre_ipda_ctl;
    uint32 cmd    = 0;
    uint32 valid[SYS_OVERLAT_TUNNEL_MAX_IP_INDEX];
    uint32 isVxlan[SYS_OVERLAT_TUNNEL_MAX_IP_INDEX];
    uint32 ipda[SYS_OVERLAT_TUNNEL_MAX_IP_INDEX];
    uint8 i = 0;
    uint8 is_vxlan = ((type == CTC_OVERLAY_TUNNEL_TYPE_VXLAN)||(type == CTC_OVERLAY_TUNNEL_TYPE_GENEVE)) ? 1 : 0;
    * ip_index = SYS_OVERLAY_TUNNEL_INVALID_IPDA_INDEX;

    SYS_OVERLAY_TUNNEL_DBG_FUNC();

    sal_memset(&ipe_vxlan_nvgre_ipda_ctl, 0, sizeof(ipe_vxlan_nvgre_ipda_ctl));

    cmd = DRV_IOR(IpeVxlanNvgreIpdaCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_vxlan_nvgre_ipda_ctl));

    GetIpeVxlanNvgreIpdaCtl(A, array_0_valid_f, ipe_vxlan_nvgre_ipda_ctl, &valid[0]);
    GetIpeVxlanNvgreIpdaCtl(A, array_1_valid_f, ipe_vxlan_nvgre_ipda_ctl, &valid[1]);
    GetIpeVxlanNvgreIpdaCtl(A, array_2_valid_f, ipe_vxlan_nvgre_ipda_ctl, &valid[2]);
    GetIpeVxlanNvgreIpdaCtl(A, array_3_valid_f, ipe_vxlan_nvgre_ipda_ctl, &valid[3]);
    GetIpeVxlanNvgreIpdaCtl(A, array_0_isVxlan_f, ipe_vxlan_nvgre_ipda_ctl, &isVxlan[0]);
    GetIpeVxlanNvgreIpdaCtl(A, array_1_isVxlan_f, ipe_vxlan_nvgre_ipda_ctl, &isVxlan[1]);
    GetIpeVxlanNvgreIpdaCtl(A, array_2_isVxlan_f, ipe_vxlan_nvgre_ipda_ctl, &isVxlan[2]);
    GetIpeVxlanNvgreIpdaCtl(A, array_3_isVxlan_f, ipe_vxlan_nvgre_ipda_ctl, &isVxlan[3]);
    GetIpeVxlanNvgreIpdaCtl(A, array_0_ipDa_f, ipe_vxlan_nvgre_ipda_ctl, &ipda[0]);
    GetIpeVxlanNvgreIpdaCtl(A, array_1_ipDa_f, ipe_vxlan_nvgre_ipda_ctl, &ipda[1]);
    GetIpeVxlanNvgreIpdaCtl(A, array_2_ipDa_f, ipe_vxlan_nvgre_ipda_ctl, &ipda[2]);
    GetIpeVxlanNvgreIpdaCtl(A, array_3_ipDa_f, ipe_vxlan_nvgre_ipda_ctl, &ipda[3]);

    for ( i = 0 ; i < SYS_OVERLAT_TUNNEL_MAX_IP_INDEX ; i++)
    {
        if (valid[i] && ( is_vxlan == isVxlan[i] ) && ( ip_addr == ipda[i] ))
        {
            * ip_index = i;
            break;
        }
    }

    if (i >= SYS_OVERLAT_TUNNEL_MAX_IP_INDEX)
    {
        return CTC_E_UNEXPECT;
    }

    SYS_OVERLAY_LOCK(lchip);
    p_gg_overlay_tunnel_master[lchip]->ipda_index_cnt[i]--;
    if ( 0 == p_gg_overlay_tunnel_master[lchip]->ipda_index_cnt[i])
    {
        switch (i)
        {
            case 0 :
                SetIpeVxlanNvgreIpdaCtl(V, array_0_valid_f, ipe_vxlan_nvgre_ipda_ctl, 0);
                SetIpeVxlanNvgreIpdaCtl(V, array_0_isVxlan_f, ipe_vxlan_nvgre_ipda_ctl, 0);
                SetIpeVxlanNvgreIpdaCtl(V, array_0_ipDa_f, ipe_vxlan_nvgre_ipda_ctl, 0);
                break;

            case 1 :
                SetIpeVxlanNvgreIpdaCtl(V, array_1_valid_f, ipe_vxlan_nvgre_ipda_ctl, 0);
                SetIpeVxlanNvgreIpdaCtl(V, array_1_isVxlan_f, ipe_vxlan_nvgre_ipda_ctl, 0);
                SetIpeVxlanNvgreIpdaCtl(V, array_1_ipDa_f, ipe_vxlan_nvgre_ipda_ctl, 0);
                break;

            case 2 :
                SetIpeVxlanNvgreIpdaCtl(V, array_2_valid_f, ipe_vxlan_nvgre_ipda_ctl, 0);
                SetIpeVxlanNvgreIpdaCtl(V, array_2_isVxlan_f, ipe_vxlan_nvgre_ipda_ctl, 0);
                SetIpeVxlanNvgreIpdaCtl(V, array_2_ipDa_f, ipe_vxlan_nvgre_ipda_ctl, 0);
                break;

            case 3 :
                SetIpeVxlanNvgreIpdaCtl(V, array_3_valid_f, ipe_vxlan_nvgre_ipda_ctl, 0);
                SetIpeVxlanNvgreIpdaCtl(V, array_3_isVxlan_f, ipe_vxlan_nvgre_ipda_ctl, 0);
                SetIpeVxlanNvgreIpdaCtl(V, array_3_ipDa_f, ipe_vxlan_nvgre_ipda_ctl, 0);
                break;
        }

        cmd = DRV_IOW(IpeVxlanNvgreIpdaCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN_OVERLAY_UNLOCK(DRV_IOCTL(lchip, 0, cmd, &ipe_vxlan_nvgre_ipda_ctl));
    }
    SYS_OVERLAY_UNLOCK(lchip);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_overlay_tunnel_get_ipda_index(uint8 lchip, uint8 type, uint32 ip_addr , uint32* ip_index)
{
    ds_t ipe_vxlan_nvgre_ipda_ctl;
    uint32 cmd    = 0;
    uint32 valid[SYS_OVERLAT_TUNNEL_MAX_IP_INDEX];
    uint32 isVxlan[SYS_OVERLAT_TUNNEL_MAX_IP_INDEX];
    uint32 ipda[SYS_OVERLAT_TUNNEL_MAX_IP_INDEX];
    uint8 i = 0;
    uint8 is_vxlan = ((type == CTC_OVERLAY_TUNNEL_TYPE_VXLAN)||(type == CTC_OVERLAY_TUNNEL_TYPE_GENEVE)) ? 1 : 0;

    SYS_OVERLAY_TUNNEL_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n",__FUNCTION__);

    *ip_index = SYS_OVERLAY_TUNNEL_INVALID_IPDA_INDEX;
    sal_memset(&ipe_vxlan_nvgre_ipda_ctl, 0, sizeof(ipe_vxlan_nvgre_ipda_ctl));

    cmd = DRV_IOR(IpeVxlanNvgreIpdaCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_vxlan_nvgre_ipda_ctl));

    GetIpeVxlanNvgreIpdaCtl(A, array_0_valid_f, ipe_vxlan_nvgre_ipda_ctl, &valid[0]);
    GetIpeVxlanNvgreIpdaCtl(A, array_1_valid_f, ipe_vxlan_nvgre_ipda_ctl, &valid[1]);
    GetIpeVxlanNvgreIpdaCtl(A, array_2_valid_f, ipe_vxlan_nvgre_ipda_ctl, &valid[2]);
    GetIpeVxlanNvgreIpdaCtl(A, array_3_valid_f, ipe_vxlan_nvgre_ipda_ctl, &valid[3]);
    GetIpeVxlanNvgreIpdaCtl(A, array_0_isVxlan_f, ipe_vxlan_nvgre_ipda_ctl, &isVxlan[0]);
    GetIpeVxlanNvgreIpdaCtl(A, array_1_isVxlan_f, ipe_vxlan_nvgre_ipda_ctl, &isVxlan[1]);
    GetIpeVxlanNvgreIpdaCtl(A, array_2_isVxlan_f, ipe_vxlan_nvgre_ipda_ctl, &isVxlan[2]);
    GetIpeVxlanNvgreIpdaCtl(A, array_3_isVxlan_f, ipe_vxlan_nvgre_ipda_ctl, &isVxlan[3]);
    GetIpeVxlanNvgreIpdaCtl(A, array_0_ipDa_f, ipe_vxlan_nvgre_ipda_ctl, &ipda[0]);
    GetIpeVxlanNvgreIpdaCtl(A, array_1_ipDa_f, ipe_vxlan_nvgre_ipda_ctl, &ipda[1]);
    GetIpeVxlanNvgreIpdaCtl(A, array_2_ipDa_f, ipe_vxlan_nvgre_ipda_ctl, &ipda[2]);
    GetIpeVxlanNvgreIpdaCtl(A, array_3_ipDa_f, ipe_vxlan_nvgre_ipda_ctl, &ipda[3]);

    for (i = 0 ; i < SYS_OVERLAT_TUNNEL_MAX_IP_INDEX ; i++)
    {
        if (valid[i] && ( is_vxlan == isVxlan[i] ) && ( ip_addr == ipda[i] ))
        {
            /* return index if hit */
            *ip_index = i;
            break;
        }
    }

    if (*ip_index == SYS_OVERLAY_TUNNEL_INVALID_IPDA_INDEX)
    {
        return CTC_E_NOT_EXIST;
    }

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_overlay_tunnel_build_key(uint8 lchip, sys_scl_entry_t* scl_entry, ctc_overlay_tunnel_param_t* p_tunnel_param, uint32 ipda_info)
{
    uint8 with_sa  = 0;
    uint8 by_index = 0;
    sys_scl_key_type_t key_type = 0;
    uint8 ip_ver = CTC_IP_VER_4;
    sys_overlay_tunnel_packet_type_t packet_type = SYS_OVERLAY_TUNNEL_PACKET_TYPE_UC;

    SYS_OVERLAY_TUNNEL_DBG_FUNC();

    ip_ver = CTC_FLAG_ISSET(p_tunnel_param->flag,CTC_OVERLAY_TUNNEL_FLAG_IP_VER_6)? CTC_IP_VER_6 : CTC_IP_VER_4;

    if (CTC_OVERLAY_TUNNEL_TYPE_NVGRE == p_tunnel_param->type)
    {
        scl_entry->key.tunnel_type = (CTC_IP_VER_4 == ip_ver ) ? SYS_SCL_TUNNEL_TYPE_NVGRE_IN4 : SYS_SCL_TUNNEL_TYPE_NVGRE_IN6;
    }
    else if (CTC_OVERLAY_TUNNEL_TYPE_VXLAN == p_tunnel_param->type)
    {
        scl_entry->key.tunnel_type = (CTC_IP_VER_4 == ip_ver ) ? SYS_SCL_TUNNEL_TYPE_VXLAN_IN4 : SYS_SCL_TUNNEL_TYPE_VXLAN_IN6;
    }
    else if (CTC_OVERLAY_TUNNEL_TYPE_GENEVE == p_tunnel_param->type)
    {
        scl_entry->key.tunnel_type = (CTC_IP_VER_4 == ip_ver ) ? SYS_SCL_TUNNEL_TYPE_GENEVE_IN4 : SYS_SCL_TUNNEL_TYPE_GENEVE_IN6;
    }

    if (CTC_OVERLAY_TUNNEL_TYPE_GENEVE == p_tunnel_param->type)
    {
        p_tunnel_param->type = CTC_OVERLAY_TUNNEL_TYPE_VXLAN; /*the same as VxLAN*/
    }

    if ( ip_ver == CTC_IP_VER_4 && ((p_tunnel_param->ipda.ipv4 & 0xF0000000) == 0xE0000000))
    {
        packet_type = SYS_OVERLAY_TUNNEL_PACKET_TYPE_MC;
    }
    else if ( ip_ver == CTC_IP_VER_6 && ((p_tunnel_param->ipda.ipv6[0] & 0xFF000000) == 0xFF000000))
    {
        packet_type = SYS_OVERLAY_TUNNEL_PACKET_TYPE_MC;
    }

    with_sa = _sys_goldengate_overlay_tunnel_with_sa(lchip, p_tunnel_param);;
    if (with_sa != CTC_FLAG_ISSET(p_tunnel_param->flag, CTC_OVERLAY_TUNNEL_FLAG_USE_IPSA)
        && (!CTC_FLAG_ISSET(p_tunnel_param->flag, CTC_OVERLAY_TUNNEL_FLAG_DEFAULT_ENTRY)))
    {
        return CTC_E_INVALID_PARAM;
    }

    if (CTC_FLAG_ISSET(p_tunnel_param->flag, CTC_OVERLAY_TUNNEL_FLAG_USE_FLEX))
    {
        if (ip_ver == CTC_IP_VER_4)
        {
            scl_entry->key.type = SYS_SCL_KEY_TCAM_IPV4_SINGLE;

            scl_entry->key.u.tcam_ipv4_key.flag |= CTC_SCL_TCAM_IPV4_KEY_FLAG_IP_DA;
            scl_entry->key.u.tcam_ipv4_key.ip_da = p_tunnel_param->ipda.ipv4;
            scl_entry->key.u.tcam_ipv4_key.ip_da_mask = 0xFFFFFFFF;
            if (with_sa)
            {
                scl_entry->key.u.tcam_ipv4_key.flag |= CTC_SCL_TCAM_IPV4_KEY_FLAG_IP_SA;
                scl_entry->key.u.tcam_ipv4_key.ip_sa = p_tunnel_param->ipsa.ipv4;
                scl_entry->key.u.tcam_ipv4_key.ip_sa_mask = 0xFFFFFFFF;
            }
            if (CTC_OVERLAY_TUNNEL_TYPE_NVGRE == p_tunnel_param->type)
            {
                scl_entry->key.u.tcam_ipv4_key.flag |= CTC_SCL_TCAM_IPV4_KEY_FLAG_L4_PROTOCOL;
                scl_entry->key.u.tcam_ipv4_key.sub_flag = CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_NVGRE_KEY;
                scl_entry->key.u.tcam_ipv4_key.l4_protocol = SYS_L4_PROTOCOL_GRE;
                scl_entry->key.u.tcam_ipv4_key.l4_protocol_mask = 0xFF;
                scl_entry->key.u.tcam_ipv4_key.gre_key = p_tunnel_param->src_vn_id << 8;
                scl_entry->key.u.tcam_ipv4_key.gre_key_mask = 0xFFFFFF << 8;
            }
            else
            {
                scl_entry->key.u.tcam_ipv4_key.flag |= CTC_SCL_TCAM_IPV4_KEY_FLAG_L4_PROTOCOL;
                scl_entry->key.u.tcam_ipv4_key.sub_flag   = CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_VNI;
                scl_entry->key.u.tcam_ipv4_key.l4_protocol = SYS_L4_PROTOCOL_UDP;
                scl_entry->key.u.tcam_ipv4_key.l4_protocol_mask = 0xFF;
                scl_entry->key.u.tcam_ipv4_key.vni = p_tunnel_param->src_vn_id;
                scl_entry->key.u.tcam_ipv4_key.vni_mask = 0xFFFFFF;
            }
        }
        else
        {
            scl_entry->key.type = SYS_SCL_KEY_TCAM_IPV6;
            scl_entry->key.u.tcam_ipv6_key.flag |= CTC_SCL_TCAM_IPV6_KEY_FLAG_IP_DA;
            sal_memcpy(scl_entry->key.u.tcam_ipv6_key.ip_da, p_tunnel_param->ipda.ipv6, 16);
            sal_memset(scl_entry->key.u.tcam_ipv6_key.ip_da_mask, 0xFF, 16);
            if (with_sa)
            {
                scl_entry->key.u.tcam_ipv6_key.flag |= CTC_SCL_TCAM_IPV6_KEY_FLAG_IP_SA;
                sal_memcpy(scl_entry->key.u.tcam_ipv6_key.ip_sa, p_tunnel_param->ipsa.ipv6, 16);
                sal_memset(scl_entry->key.u.tcam_ipv6_key.ip_da_mask, 0xFF, 16);
            }

            if (CTC_OVERLAY_TUNNEL_TYPE_NVGRE == p_tunnel_param->type)
            {
                scl_entry->key.u.tcam_ipv6_key.flag |= CTC_SCL_TCAM_IPV6_KEY_FLAG_L4_PROTOCOL;
                scl_entry->key.u.tcam_ipv6_key.sub_flag = CTC_SCL_TCAM_IPV6_KEY_SUB_FLAG_NVGRE_KEY;
                scl_entry->key.u.tcam_ipv6_key.l4_protocol = SYS_L4_PROTOCOL_GRE;
                scl_entry->key.u.tcam_ipv6_key.l4_protocol_mask = 0xFF;
                scl_entry->key.u.tcam_ipv6_key.gre_key = p_tunnel_param->src_vn_id << 8;
                scl_entry->key.u.tcam_ipv6_key.gre_key_mask = 0xFFFFFF << 8;
            }
            else
            {
                scl_entry->key.u.tcam_ipv6_key.flag |= CTC_SCL_TCAM_IPV6_KEY_FLAG_L4_PROTOCOL;
                scl_entry->key.u.tcam_ipv6_key.sub_flag   = CTC_SCL_TCAM_IPV6_KEY_SUB_FLAG_VNI;
                scl_entry->key.u.tcam_ipv6_key.l4_protocol = SYS_L4_PROTOCOL_UDP;
                scl_entry->key.u.tcam_ipv6_key.l4_protocol_mask = 0xFF;
                scl_entry->key.u.tcam_ipv6_key.vni = p_tunnel_param->src_vn_id;
                scl_entry->key.u.tcam_ipv6_key.vni_mask = 0xFFFFFF;
            }
        }

    }
    else
    {
        by_index = p_gg_overlay_tunnel_master[lchip]->by_index[p_tunnel_param->type];
        if (!by_index && (SYS_OVERLAY_TUNNEL_PACKET_TYPE_UC == packet_type) && (ip_ver == CTC_IP_VER_4) && with_sa)
        {   /* ipv4 & with ipsa & uncast & not by index key is shared with mcast */
            key_type = p_gg_overlay_tunnel_master[lchip]->mode1_key[p_tunnel_param->type][ip_ver][SYS_OVERLAY_TUNNEL_PACKET_TYPE_MC];
        }
        else if (!with_sa)
        {
            key_type = p_gg_overlay_tunnel_master[lchip]->mode0_key[p_tunnel_param->type][ip_ver][packet_type];
        }
        else
        {
            key_type = p_gg_overlay_tunnel_master[lchip]->mode1_key[p_tunnel_param->type][ip_ver][packet_type];
        }

        scl_entry->key.type = key_type;

        if ((ip_ver == CTC_IP_VER_4) && by_index && (SYS_OVERLAY_TUNNEL_PACKET_TYPE_UC == packet_type)&&with_sa)
        {
            if (CTC_FLAG_ISSET(p_tunnel_param->flag, CTC_OVERLAY_TUNNEL_FLAG_DEFAULT_ENTRY))
            {
                return CTC_E_NOT_SUPPORT;
            }
        }

        if ( ip_ver == CTC_IP_VER_4 )
        {
            scl_entry->key.u.hash_overlay_tunnel_v4_key.ip_da = ipda_info;/*ipda or ipdaindex*/
            scl_entry->key.u.hash_overlay_tunnel_v4_key.ip_sa = p_tunnel_param->ipsa.ipv4;
            scl_entry->key.u.hash_overlay_tunnel_v4_key.vn_id = p_tunnel_param->src_vn_id;
        }
        else
        {

            scl_entry->key.u.hash_overlay_tunnel_v6_key.ip_da[0] = p_tunnel_param->ipda.ipv6[3];
            scl_entry->key.u.hash_overlay_tunnel_v6_key.ip_da[1] = p_tunnel_param->ipda.ipv6[2];
            scl_entry->key.u.hash_overlay_tunnel_v6_key.ip_da[2] = p_tunnel_param->ipda.ipv6[1];
            scl_entry->key.u.hash_overlay_tunnel_v6_key.ip_da[3] = p_tunnel_param->ipda.ipv6[0];

            scl_entry->key.u.hash_overlay_tunnel_v6_key.ip_sa[0] = p_tunnel_param->ipsa.ipv6[3];
            scl_entry->key.u.hash_overlay_tunnel_v6_key.ip_sa[1] = p_tunnel_param->ipsa.ipv6[2];
            scl_entry->key.u.hash_overlay_tunnel_v6_key.ip_sa[2] = p_tunnel_param->ipsa.ipv6[1];
            scl_entry->key.u.hash_overlay_tunnel_v6_key.ip_sa[3] = p_tunnel_param->ipsa.ipv6[0];

            scl_entry->key.u.hash_overlay_tunnel_v6_key.vn_id = p_tunnel_param->src_vn_id;
        }
    }
    SYS_OVERLAY_TUNNEL_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "key type = %d\n",  scl_entry->key.type);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_overlay_tunnel_build_action(uint8 lchip, sys_scl_action_t* scl_action, ctc_overlay_tunnel_param_t* p_tunnel_param)
{
    sys_nh_info_dsnh_t nh_info;
    uint32 fwd_offset;
    uint16 dst_fid;
    uint16 stats_ptr = 0;
    uint16 policer_ptr = 0;
    uint8 route_mac_index = 0;
    uint8 mode = 0;
    mac_addr_t mac;
    int ret= CTC_E_NONE;

    SYS_OVERLAY_TUNNEL_DBG_FUNC();

    sal_memset(&nh_info, 0, sizeof(sys_nh_info_dsnh_t));

    if ((CTC_FLAG_ISSET(p_tunnel_param->flag, CTC_OVERLAY_TUNNEL_FLAG_VRF))
        +
        (CTC_FLAG_ISSET(p_tunnel_param->flag, CTC_OVERLAY_TUNNEL_FLAG_OVERWRITE_PORT_HASH_LKUP_PROP)
         ||CTC_FLAG_ISSET(p_tunnel_param->flag, CTC_OVERLAY_TUNNEL_FLAG_ACL_FLOW_HASH_EN)
         ||CTC_FLAG_ISSET(p_tunnel_param->flag, CTC_OVERLAY_TUNNEL_FLAG_OVERWRITE_PORT_TCAM_LKUP_PROP)
         ||CTC_FLAG_ISSET(p_tunnel_param->flag, CTC_OVERLAY_TUNNEL_FLAG_ACL_FLOW_TCAM_EN))
        > 1)
    {
        return CTC_E_INVALID_PARAM;
    }

    if ((p_tunnel_param->logic_src_port) && (CTC_FLAG_ISSET(p_tunnel_param->flag, CTC_OVERLAY_TUNNEL_FLAG_METADATA_EN)))
    {
        return CTC_E_INVALID_PARAM;
    }

    if (p_tunnel_param->inner_taged_chk_mode > 3)
    {
        return CTC_E_INVALID_PARAM;
    }

    sys_goldengate_vlan_overlay_get_vni_fid_mapping_mode(lchip, &mode);

    /* fid to vnid N:1 do not support config vnid */
    if(mode && !CTC_FLAG_ISSET(p_tunnel_param->flag, CTC_OVERLAY_TUNNEL_FLAG_FID) && !CTC_FLAG_ISSET(p_tunnel_param->flag, CTC_OVERLAY_TUNNEL_FLAG_OUTPUT_NHID))
    {
        return CTC_E_NOT_SUPPORT;
    }

    /* get ds_fwd */
    if (CTC_FLAG_ISSET(p_tunnel_param->flag, CTC_OVERLAY_TUNNEL_FLAG_OUTPUT_NHID))
    {
        CTC_ERROR_RETURN(sys_goldengate_nh_get_nhinfo(lchip, p_tunnel_param->action.nh_id, &nh_info));
        if (!nh_info.ecmp_valid)
        {
            CTC_ERROR_RETURN(sys_goldengate_nh_get_dsfwd_offset(lchip, p_tunnel_param->action.nh_id, &fwd_offset));
        }
    }

    /* build scl action */
    scl_action->type   = SYS_SCL_ACTION_TUNNEL;
    scl_action->u.tunnel_action.is_tunnel = 1;
    scl_action->u.tunnel_action.tunnel_payload_offset =
        (p_tunnel_param->type == CTC_OVERLAY_TUNNEL_TYPE_NVGRE) ? 4 : 0;/*IpeTunnelDecapCtl.offsetByteShift = 1*/
    scl_action->u.tunnel_action.inner_packet_lookup = 1;
    scl_action->u.tunnel_action.logic_port_type = p_tunnel_param->logic_port_type;
    scl_action->u.tunnel_action.ttl_check_en =
        CTC_FLAG_ISSET(p_tunnel_param->flag, CTC_OVERLAY_TUNNEL_FLAG_TTL_CHECK) ? 1 : 0;
    scl_action->u.tunnel_action.ttl_update =
        CTC_FLAG_ISSET(p_tunnel_param->flag, CTC_OVERLAY_TUNNEL_FLAG_USE_OUTER_TTL) ? 0 : 1;
    scl_action->u.tunnel_action.acl_qos_use_outer_info =
        CTC_FLAG_ISSET(p_tunnel_param->flag, CTC_OVERLAY_TUNNEL_FLAG_ACL_LKUP_BY_OUTER_HEAD) ? 1 : 0;
    scl_action->u.tunnel_action.classify_use_outer_info =
        CTC_FLAG_ISSET(p_tunnel_param->flag, CTC_OVERLAY_TUNNEL_FLAG_QOS_USE_OUTER_INFO) ? 1 : 0;
    scl_action->u.tunnel_action.inner_vtag_check_mode = p_tunnel_param->inner_taged_chk_mode;
    scl_action->u.tunnel_action.deny_learning = CTC_FLAG_ISSET(p_tunnel_param->flag, CTC_OVERLAY_TUNNEL_FLAG_DENY_LEARNING)? 1 : 0;
    scl_action->u.tunnel_action.svlan_tpid_index = p_tunnel_param->tpid_index;

    if (CTC_FLAG_ISSET(p_tunnel_param->flag, CTC_OVERLAY_TUNNEL_FLAG_OUTPUT_NHID))
    {
        if (nh_info.ecmp_valid)
        {
            scl_action->u.tunnel_action.ecmp_en = 1;
            scl_action->u.tunnel_action.u2.ecmp_group_id = nh_info.ecmp_group_id & 0xffff;
        }
        else
        {
            scl_action->u.tunnel_action.ds_fwd_ptr_valid = 1;
            scl_action->u.tunnel_action.u2.dsfwdptr = fwd_offset & 0xffff;
        }
        scl_action->u.tunnel_action.route_disable = 1;
        scl_action->u.tunnel_action.deny_bridge = 1;
    }
    else
    {
        if (!CTC_FLAG_ISSET(p_tunnel_param->flag, CTC_OVERLAY_TUNNEL_FLAG_FID))
        {
            /* set dest fid for lookup after decapsulate*/
            SYS_OVERLAY_TUNNEL_VNI_CHECK(p_tunnel_param->action.dst_vn_id);
            ret = sys_goldengate_vlan_overlay_get_fid (lchip,  p_tunnel_param->action.dst_vn_id, &dst_fid );
            if ((ret < 0) && !(CTC_FLAG_ISSET(p_tunnel_param->flag, CTC_OVERLAY_TUNNEL_FLAG_VRF)))
            {
                return ret;
            }
        }
        else
        {
            dst_fid = p_tunnel_param->fid;
        }
        scl_action->u.tunnel_action.fid_type = 1;
        scl_action->u.tunnel_action.u2.fid = (0x1 << 14 ) | dst_fid;
    }

    scl_action->u.tunnel_action.tunnel_gre_options =
        (CTC_OVERLAY_TUNNEL_TYPE_NVGRE == p_tunnel_param->type) ? 2 : 0;
    scl_action->u.tunnel_action.u1.g2.logic_src_port = p_tunnel_param->logic_src_port;

    if (CTC_FLAG_ISSET(p_tunnel_param->flag,CTC_OVERLAY_TUNNEL_FLAG_STATS_EN))
    {
        if(TRUE)
        {
            CTC_ERROR_RETURN(sys_goldengate_stats_get_statsptr(lchip, p_tunnel_param->stats_id, CTC_STATS_STATSID_TYPE_TUNNEL, &stats_ptr));
            scl_action->u.tunnel_action.chip.stats_ptr = stats_ptr;
        }
    }

    if (p_tunnel_param->policer_id > 0)
    {
        uint8 is_bwp, is_triple_play;

        CTC_ERROR_RETURN(sys_goldengate_qos_policer_index_get(lchip, p_tunnel_param->policer_id,
                                                              &policer_ptr,
                                                              &is_bwp,
                                                              &is_triple_play));
        if (CTC_FLAG_ISSET(p_tunnel_param->flag, CTC_OVERLAY_TUNNEL_FLAG_SERVICE_POLICER_EN))
        {
            scl_action->u.tunnel_action.u1.g2.service_policer_valid = 1;
            if (is_triple_play)
            {
                scl_action->u.tunnel_action.u1.g2.service_policer_mode = 1;
            }
        }
        else
        {
            if (is_bwp || is_triple_play)
            {
                return CTC_E_INVALID_PARAM;
            }
        }
        scl_action->u.tunnel_action.chip.policer_ptr = policer_ptr;

    }


    if(CTC_FLAG_ISSET(p_tunnel_param->flag,CTC_OVERLAY_TUNNEL_FLAG_SERVICE_ACL_EN))
    {
        scl_action->u.tunnel_action.service_acl_qos_en = 1;
    }

    if (CTC_FLAG_ISSET(p_tunnel_param->flag, CTC_OVERLAY_TUNNEL_FLAG_VRF))
    {
        if ((CTC_FLAG_ISSET(p_tunnel_param->flag, CTC_OVERLAY_TUNNEL_FLAG_IP_VER_6)
            && (p_tunnel_param->vrf_id >= sys_goldengate_l3if_get_max_vrfid(lchip, CTC_IP_VER_6)))
        ||
        ((!CTC_FLAG_ISSET(p_tunnel_param->flag, CTC_OVERLAY_TUNNEL_FLAG_IP_VER_6))
        && (p_tunnel_param->vrf_id >= sys_goldengate_l3if_get_max_vrfid(lchip, CTC_IP_VER_4))))
        {
            return CTC_E_IPUC_INVALID_VRF;
        }
        scl_action->u.tunnel_action.use_default_vlan_tag  = 0;
        scl_action->u.tunnel_action.trill_multi_rpf_check = 0;
        scl_action->u.tunnel_action.u3.g3.second_fid_en   = 1;
        scl_action->u.tunnel_action.u3.g3.second_fid = p_tunnel_param->vrf_id;
    }
    else
    {
        if (CTC_FLAG_ISSET(p_tunnel_param->flag, CTC_OVERLAY_TUNNEL_FLAG_OVERWRITE_PORT_HASH_LKUP_PROP)
            ||CTC_FLAG_ISSET(p_tunnel_param->flag, CTC_OVERLAY_TUNNEL_FLAG_ACL_FLOW_HASH_EN)
            ||CTC_FLAG_ISSET(p_tunnel_param->flag, CTC_OVERLAY_TUNNEL_FLAG_OVERWRITE_PORT_TCAM_LKUP_PROP)
            ||CTC_FLAG_ISSET(p_tunnel_param->flag, CTC_OVERLAY_TUNNEL_FLAG_ACL_FLOW_TCAM_EN))
        {
            if ((p_tunnel_param->acl_tcam_lookup_type > 3)
                || (p_tunnel_param->acl_hash_lookup_type > 3)
                || (p_tunnel_param->field_sel_id > 15))
            {
                return CTC_E_INVALID_PARAM;
            }

            scl_action->u.tunnel_action.use_default_vlan_tag  = 0;
            scl_action->u.tunnel_action.trill_multi_rpf_check = 0;
            scl_action->u.tunnel_action.u3.g3.second_fid_en   = 0;
            /*tcam*/
            scl_action->u.tunnel_action.u3.g3.second_fid |=
                                 CTC_FLAG_ISSET(p_tunnel_param->flag, CTC_OVERLAY_TUNNEL_FLAG_ACL_FLOW_TCAM_EN);
            scl_action->u.tunnel_action.u3.g3.second_fid |=
                            CTC_FLAG_ISSET(p_tunnel_param->flag, CTC_OVERLAY_TUNNEL_FLAG_OVERWRITE_PORT_TCAM_LKUP_PROP)?(1 << 1):0;
            scl_action->u.tunnel_action.u3.g3.second_fid |= (p_tunnel_param->acl_tcam_lookup_type & 0x3) << 2;
            scl_action->u.tunnel_action.u3.g3.second_fid |= (p_tunnel_param->acl_tcam_label & 0xFF) << 4;
            /*hash*/
            scl_action->u.tunnel_action.u3.g3.second_fid |= (p_tunnel_param->acl_hash_lookup_type & 0x3) << 12;
            scl_action->u.tunnel_action.trill_bfd_echo_en =
                           CTC_FLAG_ISSET(p_tunnel_param->flag, CTC_OVERLAY_TUNNEL_FLAG_OVERWRITE_PORT_HASH_LKUP_PROP);
            scl_action->u.tunnel_action.trill_bfd_en      = p_tunnel_param->field_sel_id & 0x1;
            scl_action->u.tunnel_action.isid_valid        = (p_tunnel_param->field_sel_id >> 1) & 0x1;
            scl_action->u.tunnel_action.trill_channel_en  = (p_tunnel_param->field_sel_id >> 2) & 0x1;
            scl_action->u.tunnel_action.trill_option_escape_en = (p_tunnel_param->field_sel_id >> 3) & 0x1;
        }
    }

    if(CTC_FLAG_ISSET(p_tunnel_param->flag,CTC_OVERLAY_TUNNEL_FLAG_METADATA_EN))
    {
        if (p_tunnel_param->metadata > 0x3FFF)
        {
            return CTC_E_INVALID_PARAM;
        }
        scl_action->u.tunnel_action.u1.g2.metadata_type  = CTC_METADATA_TYPE_METADATA;
        scl_action->u.tunnel_action.u1.g2.logic_src_port = p_tunnel_param->metadata & 0x3FFF;
    }

    sal_memset(&mac, 0, sizeof(mac_addr_t));
    if (sal_memcmp(&mac, &(p_tunnel_param->route_mac), sizeof(mac_addr_t)))
    {
        CTC_ERROR_RETURN(sys_goldengate_l3if_binding_inner_route_mac(lchip, &(p_tunnel_param->route_mac), &route_mac_index));
        scl_action->u.tunnel_action.router_mac_profile_en = 1;
        scl_action->u.tunnel_action.router_mac_profile = route_mac_index;
    }

    if (CTC_FLAG_ISSET(p_tunnel_param->flag, CTC_OVERLAY_TUNNEL_FLAG_ARP_ACTIOIN))
    {
        CTC_MAX_VALUE_CHECK(p_tunnel_param->arp_action, MAX_CTC_EXCP_TYPE - 1);
        scl_action->u.tunnel_action.arp_ctl_en = 1;
        scl_action->u.tunnel_action.arp_exception_type = p_tunnel_param->arp_action;
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_overlay_tunnel_wb_sync(uint8 lchip)
{
    int32 ret  = CTC_E_NONE;
    uint8 loop = 0;
    ctc_wb_data_t wb_data;
    sys_wb_overlay_tunnel_master_t* p_overlay_wb_master = NULL;


    /* sync up overlay tunnel matser */
    wb_data.buffer = mem_malloc(MEM_OVERLAY_TUNNEL_MODULE, CTC_WB_DATA_BUFFER_LENGTH);
    if (NULL == wb_data.buffer)
    {
        ret = CTC_E_NO_MEMORY;
        goto done;
    }
    sal_memset(wb_data.buffer, 0, CTC_WB_DATA_BUFFER_LENGTH);

    p_overlay_wb_master = (sys_wb_overlay_tunnel_master_t*)wb_data.buffer;

    CTC_WB_INIT_DATA_T((&wb_data), sys_wb_overlay_tunnel_master_t, CTC_FEATURE_OVERLAY, SYS_WB_APPID_OVERLAY_SUBID_TUNNEL_MASTER);


    p_overlay_wb_master->lchip = lchip;

    for(loop = 0; loop < SYS_OVERLAT_TUNNEL_MAX_IP_INDEX; loop++)
    {
        p_overlay_wb_master->ipda_index_cnt[loop] = p_gg_overlay_tunnel_master[lchip]->ipda_index_cnt[loop];
    }
    wb_data.valid_cnt = 1;

    CTC_ERROR_GOTO(ctc_wb_add_entry(&wb_data), ret, done);


done:
    if (wb_data.buffer)
    {
        mem_free(wb_data.buffer);
    }
    return ret;
}

int32
sys_goldengate_overlay_tunnel_wb_restore(uint8 lchip)
{
    uint8 loop = 0;
    int32 ret = CTC_E_NONE;
    ctc_wb_query_t wb_query;
    sys_wb_overlay_tunnel_master_t overlay_wb_master;

    sal_memset(&wb_query, 0, sizeof(ctc_wb_query_t));
    wb_query.buffer = mem_malloc(MEM_OVERLAY_TUNNEL_MODULE, CTC_WB_DATA_BUFFER_LENGTH);
    if (NULL == wb_query.buffer)
    {
        ret = CTC_E_NO_MEMORY;
        goto done;
    }
    sal_memset(wb_query.buffer, 0, CTC_WB_DATA_BUFFER_LENGTH);

    /*restore master*/
    CTC_WB_INIT_QUERY_T((&wb_query), sys_wb_overlay_tunnel_master_t, CTC_FEATURE_OVERLAY, SYS_WB_APPID_OVERLAY_SUBID_TUNNEL_MASTER);
    CTC_ERROR_GOTO(ctc_wb_query_entry(&wb_query), ret, done);
    if (wb_query.valid_cnt != 1 || wb_query.is_end != 1)
    {
        CTC_WB_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "query overlay master error! valid_cnt: %d, is_end: %d.\n", wb_query.valid_cnt, wb_query.is_end);
        goto done;
    }
    /* set default value to new added fields, default value may not be zeros */
    sal_memset(&overlay_wb_master, 0, sizeof(sys_wb_overlay_tunnel_master_t));

    sal_memcpy(&overlay_wb_master, wb_query.buffer, wb_query.key_len + wb_query.data_len);

    for(loop = 0; loop < SYS_OVERLAT_TUNNEL_MAX_IP_INDEX; loop++)
    {
        p_gg_overlay_tunnel_master[lchip]->ipda_index_cnt[loop] = overlay_wb_master.ipda_index_cnt[loop];
    }


 done:
     if (wb_query.key)
     {
         mem_free(wb_query.key);
     }

     if (wb_query.buffer)
     {
         mem_free(wb_query.buffer);
     }
     return ret;

}


int32
sys_goldengate_overlay_tunnel_init(uint8 lchip, void* p_param)
{
    ctc_overlay_tunnel_global_cfg_t* p_config = p_param;
    uint32 cmd = 0;
    ds_t ds_data;
    int32 ret;
    uint32 temp = 0;
    uint32 vxlan_udp_src_port_base = 49152; /* RFC7348*/

    LCHIP_CHECK(lchip);
    CTC_ERROR_RETURN(sys_goldengate_vlan_overlay_set_vni_fid_mapping_mode(lchip, p_config->vni_mapping_mode));
    if (NULL == p_gg_overlay_tunnel_master[lchip])
    {
        /* init master */
        p_gg_overlay_tunnel_master[lchip] = mem_malloc(MEM_OVERLAY_TUNNEL_MODULE, sizeof(sys_overlay_tunnel_master_t));
        if (!p_gg_overlay_tunnel_master[lchip])
        {
            return CTC_E_NO_MEMORY;
        }

        sal_memset(p_gg_overlay_tunnel_master[lchip], 0, sizeof(sys_overlay_tunnel_master_t));
        /* set key type */
        p_gg_overlay_tunnel_master[lchip]->mode0_key[CTC_OVERLAY_TUNNEL_TYPE_NVGRE][CTC_IP_VER_4][SYS_OVERLAY_TUNNEL_PACKET_TYPE_UC]
        = SYS_SCL_KEY_HASH_NVGRE_UC_V4_MODE0;
        p_gg_overlay_tunnel_master[lchip]->mode0_key[CTC_OVERLAY_TUNNEL_TYPE_NVGRE][CTC_IP_VER_4][SYS_OVERLAY_TUNNEL_PACKET_TYPE_MC]
        = SYS_SCL_KEY_HASH_NVGRE_MC_V4_MODE0;

        p_gg_overlay_tunnel_master[lchip]->mode0_key[CTC_OVERLAY_TUNNEL_TYPE_NVGRE][CTC_IP_VER_6][SYS_OVERLAY_TUNNEL_PACKET_TYPE_UC]
        = SYS_SCL_KEY_HASH_NVGRE_UC_V6_MODE0;
        p_gg_overlay_tunnel_master[lchip]->mode0_key[CTC_OVERLAY_TUNNEL_TYPE_NVGRE][CTC_IP_VER_6][SYS_OVERLAY_TUNNEL_PACKET_TYPE_MC]
        = SYS_SCL_KEY_HASH_NVGRE_MC_V6_MODE0;

        p_gg_overlay_tunnel_master[lchip]->mode0_key[CTC_OVERLAY_TUNNEL_TYPE_VXLAN][CTC_IP_VER_4][SYS_OVERLAY_TUNNEL_PACKET_TYPE_UC]
        = SYS_SCL_KEY_HASH_VXLAN_UC_V4_MODE0;
        p_gg_overlay_tunnel_master[lchip]->mode0_key[CTC_OVERLAY_TUNNEL_TYPE_VXLAN][CTC_IP_VER_4][SYS_OVERLAY_TUNNEL_PACKET_TYPE_MC]
        = SYS_SCL_KEY_HASH_VXLAN_MC_V4_MODE0;

        p_gg_overlay_tunnel_master[lchip]->mode0_key[CTC_OVERLAY_TUNNEL_TYPE_VXLAN][CTC_IP_VER_6][SYS_OVERLAY_TUNNEL_PACKET_TYPE_UC]
        = SYS_SCL_KEY_HASH_VXLAN_UC_V6_MODE0;
        p_gg_overlay_tunnel_master[lchip]->mode0_key[CTC_OVERLAY_TUNNEL_TYPE_VXLAN][CTC_IP_VER_6][SYS_OVERLAY_TUNNEL_PACKET_TYPE_MC]
        = SYS_SCL_KEY_HASH_VXLAN_MC_V6_MODE0;


        p_gg_overlay_tunnel_master[lchip]->mode1_key[CTC_OVERLAY_TUNNEL_TYPE_NVGRE][CTC_IP_VER_4][SYS_OVERLAY_TUNNEL_PACKET_TYPE_UC]
        = SYS_SCL_KEY_HASH_NVGRE_UC_V4_MODE1;
        p_gg_overlay_tunnel_master[lchip]->mode1_key[CTC_OVERLAY_TUNNEL_TYPE_NVGRE][CTC_IP_VER_4][SYS_OVERLAY_TUNNEL_PACKET_TYPE_MC]
        = SYS_SCL_KEY_HASH_NVGRE_V4_MODE1;

        p_gg_overlay_tunnel_master[lchip]->mode1_key[CTC_OVERLAY_TUNNEL_TYPE_NVGRE][CTC_IP_VER_6][SYS_OVERLAY_TUNNEL_PACKET_TYPE_UC]
        = SYS_SCL_KEY_HASH_NVGRE_UC_V6_MODE1;
        p_gg_overlay_tunnel_master[lchip]->mode1_key[CTC_OVERLAY_TUNNEL_TYPE_NVGRE][CTC_IP_VER_6][SYS_OVERLAY_TUNNEL_PACKET_TYPE_MC]
        = SYS_SCL_KEY_HASH_NVGRE_MC_V6_MODE1;

        p_gg_overlay_tunnel_master[lchip]->mode1_key[CTC_OVERLAY_TUNNEL_TYPE_VXLAN][CTC_IP_VER_4][SYS_OVERLAY_TUNNEL_PACKET_TYPE_UC]
        = SYS_SCL_KEY_HASH_VXLAN_UC_V4_MODE1;
        p_gg_overlay_tunnel_master[lchip]->mode1_key[CTC_OVERLAY_TUNNEL_TYPE_VXLAN][CTC_IP_VER_4][SYS_OVERLAY_TUNNEL_PACKET_TYPE_MC]
        = SYS_SCL_KEY_HASH_VXLAN_V4_MODE1;

        p_gg_overlay_tunnel_master[lchip]->mode1_key[CTC_OVERLAY_TUNNEL_TYPE_VXLAN][CTC_IP_VER_6][SYS_OVERLAY_TUNNEL_PACKET_TYPE_UC]
        = SYS_SCL_KEY_HASH_VXLAN_UC_V6_MODE1;
        p_gg_overlay_tunnel_master[lchip]->mode1_key[CTC_OVERLAY_TUNNEL_TYPE_VXLAN][CTC_IP_VER_6][SYS_OVERLAY_TUNNEL_PACKET_TYPE_MC]
        = SYS_SCL_KEY_HASH_VXLAN_MC_V6_MODE1;
    }

    /* config for overlay tunnel decapsulate key type*/
    /* get global config */
    sal_memset(ds_data, 0, sizeof(ds_data));
    cmd = DRV_IOR(IpeUserIdCtl_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, ds_data);

    /* config global decap type reg for NvGRE*/
    if (!CTC_FLAG_ISSET(p_config->nvgre_mode, CTC_OVERLAY_TUNNEL_DECAP_IPV4_UC_USE_INDIVIDUL_IPDA))
    {
        SetIpeUserIdCtl(V, v4UcNvgreUseIpDaIdxKey_f, ds_data, 1);
        p_gg_overlay_tunnel_master[lchip]->by_index[CTC_OVERLAY_TUNNEL_TYPE_NVGRE] = 1;
    }
    else
    {
        SetIpeUserIdCtl(V, v4UcNvgreUseIpDaIdxKey_f, ds_data, 0);
        p_gg_overlay_tunnel_master[lchip]->by_index[CTC_OVERLAY_TUNNEL_TYPE_NVGRE] = 0;
    }

    if (!CTC_FLAG_ISSET(p_config->nvgre_mode, CTC_OVERLAY_TUNNEL_DECAP_BY_IPDA_VNI))
    {
        SetIpeUserIdCtl(V, nvgreTunnelHash1LkpMode_f, ds_data, 1);
        SetIpeUserIdCtl(V, nvgreTunnelHash2LkpMode_f, ds_data, 1);
    }
    else
    {
        SetIpeUserIdCtl(V, nvgreTunnelHash1LkpMode_f, ds_data, 0);
        SetIpeUserIdCtl(V, nvgreTunnelHash2LkpMode_f, ds_data, 0);
    }

    /* config global decap type reg for VxLAN*/
    if (!CTC_FLAG_ISSET(p_config->vxlan_mode, CTC_OVERLAY_TUNNEL_DECAP_IPV4_UC_USE_INDIVIDUL_IPDA))
    {
        SetIpeUserIdCtl(V, v4UcVxlanUseIpDaIdxKey_f, ds_data, 1);
        p_gg_overlay_tunnel_master[lchip]->by_index[CTC_OVERLAY_TUNNEL_TYPE_VXLAN] = 1;
    }
    else
    {
        SetIpeUserIdCtl(V, v4UcVxlanUseIpDaIdxKey_f, ds_data, 0);
        p_gg_overlay_tunnel_master[lchip]->by_index[CTC_OVERLAY_TUNNEL_TYPE_VXLAN] = 0;
    }

    if (!CTC_FLAG_ISSET(p_config->vxlan_mode, CTC_OVERLAY_TUNNEL_DECAP_BY_IPDA_VNI))
    {
        SetIpeUserIdCtl(V, vxlanTunnelHash1LkpMode_f, ds_data, 1);
        SetIpeUserIdCtl(V, vxlanTunnelHash2LkpMode_f, ds_data, 1);
    }
    else
    {
        SetIpeUserIdCtl(V, vxlanTunnelHash1LkpMode_f, ds_data, 0);
        SetIpeUserIdCtl(V, vxlanTunnelHash2LkpMode_f, ds_data, 0);
    }

    /* write global config*/
    cmd = DRV_IOW(IpeUserIdCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, 0, cmd, ds_data), ret, OVERLAY_TUNNEL_INIT_ERROR);

    /* global config for UDP dest port and flag*/
    sal_memset(ds_data, 0, sizeof(ds_data));
    cmd = DRV_IOR(EpePktProcCtl_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &ds_data);
    SetEpePktProcCtl(V, vxlanV4UdpDestPort_f, ds_data, 4789);
    SetEpePktProcCtl(V, vxlanV6UdpDestPort_f, ds_data, 4789);
    SetEpePktProcCtl(V, vxlanUdpSrcPortBase_f, ds_data, vxlan_udp_src_port_base);
    SetEpePktProcCtl(V, vxlanFlagField_f, ds_data , 0x8);
    SetEpePktProcCtl(V, evxlanEnable_f, ds_data , 1);
    SetEpePktProcCtl(V, evxlanFlagField_f, ds_data , 0xC); /*I flag = 1, P flag = 1, O flag = 0*/
    SetEpePktProcCtl(V, evxlanFlagRsv0_f, ds_data , 0);
    SetEpePktProcCtl(V, evxlanFlagRsv2_f, ds_data , 0); /*Ver = 0*/
    SetEpePktProcCtl(V, array_0_vxlanProtocolType_f, ds_data , 1); /*ipv4*/
    SetEpePktProcCtl(V, array_1_vxlanProtocolType_f, ds_data , 2);/*ipv6*/
    SetEpePktProcCtl(V, array_2_vxlanProtocolType_f, ds_data , 3);/*ethernet*/
    SetEpePktProcCtl(V, array_3_vxlanProtocolType_f, ds_data , 4);
    SetEpePktProcCtl(V, geneveEnable_f, ds_data , 1);
    SetEpePktProcCtl(V, geneveV4UdpDestPort_f, ds_data , 6081);
    SetEpePktProcCtl(V, geneveV6UdpDestPort_f, ds_data , 6081);
    SetEpePktProcCtl(V, geneveFlagField_f, ds_data , 0); /*Ver = 0, Open Len = 0*/
    SetEpePktProcCtl(V, geneveFlagRsv0_f, ds_data , 0);/*O flag = 0, C flag = 0*/
    SetEpePktProcCtl(V, geneveFlagRsv2_f, ds_data , 0);
    cmd = DRV_IOW(EpePktProcCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, 0, cmd, ds_data), ret, OVERLAY_TUNNEL_INIT_ERROR);

    sal_memset(ds_data, 0, sizeof(ds_data));
    cmd = DRV_IOR(ParserLayer4AppCtl_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, ds_data);
    SetParserLayer4AppCtl(V, vxlanIpv4UdpDestPort_f, ds_data, 4789);
    SetParserLayer4AppCtl(V, vxlanIpv6UdpDestPort_f, ds_data, 4789);
    SetParserLayer4AppCtl(V, vxlanEn_f, ds_data , 0x1);
    SetParserLayer4AppCtl(V, identifyEvxlanEn_f, ds_data , 0x1);
    SetParserLayer4AppCtl(V, geneveVxlanUdpDestPort_f, ds_data, 6081);
    SetParserLayer4AppCtl(V, geneveVxlanEn_f, ds_data , 0x1);
    cmd = DRV_IOW(ParserLayer4AppCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, 0, cmd, ds_data), ret,  OVERLAY_TUNNEL_INIT_ERROR);

    sal_memset(ds_data, 0, sizeof(ds_data));
    cmd = DRV_IOR(ParserReserved0_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, ds_data);
    temp = ((0x0002&0x3F) << 10) |  (0x0001 << 2) | 1;
    SetParserReserved0(V, reserved_f, ds_data , temp);
    cmd = DRV_IOW(ParserReserved0_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, ds_data);
    cmd = DRV_IOW(ParserReserved1_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, ds_data);
    cmd = DRV_IOW(ParserReserved2_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, ds_data);
    cmd = DRV_IOW(ParserReserved3_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, ds_data);
    cmd = DRV_IOW(ParserReserved4_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, ds_data);
    cmd = DRV_IOW(ParserReserved5_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, ds_data);

    sal_memset(ds_data, 0, sizeof(ds_data));
    cmd = DRV_IOR(ParserLayer4FlexCtl_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, ds_data);
    temp = ((0x0003&0x7) << 2) | (0x0002 >> 6);
    SetParserLayer4FlexCtl(V, layer4ByteSelect0_f, ds_data , temp);
    temp = (0x0003 >> 3)&0x1F;
    SetParserLayer4FlexCtl(V, layer4ByteSelect1_f, ds_data , temp);
    cmd = DRV_IOW(ParserLayer4FlexCtl_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, ds_data);

    sal_memset(ds_data, 0, sizeof(ds_data));
    cmd = DRV_IOR(IpeLoopbackHeaderAdjustCtl_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &ds_data);
    SetIpeLoopbackHeaderAdjustCtl(V, disableEloopPktBypassTunnelDecaps_f, ds_data, 0x1);
    cmd = DRV_IOW(IpeLoopbackHeaderAdjustCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, 0, cmd, ds_data), ret, OVERLAY_TUNNEL_INIT_ERROR);

    temp = 1;
    cmd = DRV_IOW(IpeTunnelDecapCtl_t, IpeTunnelDecapCtl_vxlanAutoIdentifyPayload_f);
    DRV_IOCTL(lchip, 0, cmd, &temp);
    SYS_OVERLAY_CREATE_LOCK(lchip);

    CTC_ERROR_GOTO(sys_goldengate_wb_sync_register_cb(lchip, CTC_FEATURE_OVERLAY, sys_goldengate_overlay_tunnel_wb_sync), ret, OVERLAY_TUNNEL_INIT_ERROR);

    if (CTC_WB_ENABLE && CTC_WB_STATUS(lchip) == CTC_WB_STATUS_RELOADING)
    {
        CTC_ERROR_GOTO(sys_goldengate_overlay_tunnel_wb_restore(lchip), ret, OVERLAY_TUNNEL_INIT_ERROR);
    }

    return CTC_E_NONE;

    OVERLAY_TUNNEL_INIT_ERROR:
    if (p_gg_overlay_tunnel_master[lchip])
    {
        mem_free(p_gg_overlay_tunnel_master[lchip]);
        p_gg_overlay_tunnel_master[lchip] = NULL;
    }
    return ret;
}

int32
sys_goldengate_overlay_tunnel_deinit(uint8 lchip)
{
    LCHIP_CHECK(lchip);
    if (NULL == p_gg_overlay_tunnel_master[lchip])
    {
        return CTC_E_NONE;
    }

    sys_goldengate_opf_deinit(lchip, OPF_OVERLAY_TUNNEL_INNER_FID);

    sal_mutex_destroy(p_gg_overlay_tunnel_master[lchip]->mutex);
    mem_free(p_gg_overlay_tunnel_master[lchip]);

    return CTC_E_NONE;
}

int32
sys_goldengate_overlay_tunnel_set_fid (uint8 lchip, uint32 vn_id, uint16 fid )
{
    uint32 capability[CTC_GLOBAL_CAPABILITY_MAX] = {0};
    SYS_OVERLAY_INIT_CHECK(lchip);
    SYS_OVERLAY_TUNNEL_VNI_CHECK(vn_id);

    sys_goldengate_global_ctl_get(lchip, CTC_GLOBAL_CHIP_CAPABILITY, capability);

    if (fid > capability[CTC_GLOBAL_CAPABILITY_MAX_FID])
    {
        return CTC_E_BADID;
    }

    SYS_OVERLAY_TUNNEL_DBG_FUNC();

    CTC_ERROR_RETURN(sys_goldengate_vlan_overlay_set_fid(lchip, vn_id, fid));

    return CTC_E_NONE;
}


int32
sys_goldengate_overlay_tunnel_get_fid (uint8 lchip,  uint32 vn_id, uint16* p_fid )
{
    SYS_OVERLAY_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_fid);
    SYS_OVERLAY_TUNNEL_VNI_CHECK(vn_id);

    SYS_OVERLAY_TUNNEL_DBG_FUNC();

    CTC_ERROR_RETURN(sys_goldengate_vlan_overlay_get_fid(lchip, vn_id, p_fid));

    return CTC_E_NONE;
}

int32
sys_goldengate_overlay_tunnel_get_vn_id(uint8 lchip, uint16 fid, uint32* p_vn_id)
{
    SYS_OVERLAY_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_vn_id);
    SYS_L2_FID_CHECK(fid);

    SYS_OVERLAY_TUNNEL_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n",__FUNCTION__);

    CTC_ERROR_RETURN(sys_goldengate_vlan_overlay_get_vn_id(lchip, fid, p_vn_id));

    return CTC_E_NONE;
}

int32
sys_goldengate_overlay_tunnel_add_tunnel(uint8 lchip, ctc_overlay_tunnel_param_t* p_tunnel_param)
{
    sys_scl_entry_t  scl_entry;
    uint32 gid = 0;
    uint32 ipda_info = p_tunnel_param->ipda.ipv4;
    int32 ret = CTC_E_NONE;

    SYS_OVERLAY_INIT_CHECK(lchip);
    SYS_OVERLAY_TUNNEL_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_tunnel_param);
    SYS_OVERLAY_TUNNEL_TYEP_CHECK(p_tunnel_param);
    SYS_OVERLAY_TUNNEL_LOGIC_PORT_CHECK(p_tunnel_param->logic_src_port);
    SYS_OVERLAY_TUNNEL_SCL_ID_CHECK(p_tunnel_param->scl_id);
    CTC_MAX_VALUE_CHECK(p_tunnel_param->tpid_index, 3);

    if (!CTC_FLAG_ISSET(p_tunnel_param->flag, CTC_OVERLAY_TUNNEL_FLAG_DEFAULT_ENTRY))
    {
        SYS_OVERLAY_TUNNEL_VNI_CHECK(p_tunnel_param->src_vn_id);
    }

    if (CTC_FLAG_ISSET(p_tunnel_param->flag, CTC_OVERLAY_TUNNEL_FLAG_DEFAULT_ENTRY)
        && CTC_FLAG_ISSET(p_tunnel_param->flag, CTC_OVERLAY_TUNNEL_FLAG_USE_FLEX))
    {
        return CTC_E_INVALID_PARAM;
    }

    SYS_OVERLAY_TUNNEL_DBG_FUNC();

    sal_memset(&scl_entry, 0, sizeof(sys_scl_entry_t));
    scl_entry.key.type = SYS_SCL_KEY_NUM;

    /* alloc ipdaindex*/
    if (_sys_goldengate_overlay_tunnel_is_ipda_index(lchip, p_tunnel_param))
    {
        _sys_goldengate_overlay_tunnel_add_ipda_index(lchip, p_tunnel_param->type, p_tunnel_param->ipda.ipv4 , &ipda_info);
        if (ipda_info == SYS_OVERLAY_TUNNEL_INVALID_IPDA_INDEX)
        {
            SYS_OVERLAY_TUNNEL_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, " [OVERLAY_TUNNEL] The register to store IP DA index is full\n");
            return CTC_E_NO_RESOURCE;
        }
    }

    /* build scl key */
    CTC_ERROR_GOTO(_sys_goldengate_overlay_tunnel_build_key(lchip, &scl_entry, p_tunnel_param, ipda_info), ret, error0);

    /* build scl action */
    CTC_ERROR_GOTO(_sys_goldengate_overlay_tunnel_build_action(lchip, &(scl_entry.action), p_tunnel_param), ret, error0);

    if (CTC_FLAG_ISSET(p_tunnel_param->flag, CTC_OVERLAY_TUNNEL_FLAG_DEFAULT_ENTRY))
    {
        CTC_ERROR_GOTO(sys_goldengate_scl_set_default_tunnel_action(lchip, scl_entry.key.type, &(scl_entry.action)), ret, error1);
        return CTC_E_NONE;
    }

    /* write tunnel entry */
    if (CTC_FLAG_ISSET(p_tunnel_param->flag, CTC_OVERLAY_TUNNEL_FLAG_USE_FLEX))
    {
        gid = SYS_SCL_GROUP_ID_INNER_TCAM_OVERLAY_TUNNEL + p_tunnel_param->scl_id;
    }
    else
    {
        gid = SYS_SCL_GROUP_ID_INNER_HASH_OVERLAY_TUNNEL;
    }
    CTC_ERROR_GOTO(sys_goldengate_scl_add_entry(lchip, gid, &scl_entry, 1), ret, error1);
    CTC_ERROR_GOTO(sys_goldengate_scl_install_entry(lchip, scl_entry.entry_id, 1), ret, error2);
    return CTC_E_NONE;

error2:
    sys_goldengate_scl_remove_entry(lchip, scl_entry.entry_id, 1);
error1:
    if (scl_entry.action.u.tunnel_action.router_mac_profile_en)/*build in _sys_goldengate_overlay_tunnel_build_action*/
    {
        sys_goldengate_l3if_unbinding_inner_route_mac(lchip, scl_entry.action.u.tunnel_action.router_mac_profile);
    }
error0:
    if (_sys_goldengate_overlay_tunnel_is_ipda_index(lchip, p_tunnel_param))
    {
        _sys_goldengate_overlay_tunnel_del_ipda_index(lchip, p_tunnel_param->type, p_tunnel_param->ipda.ipv4 , &ipda_info);
    }

    return ret;
}


int32
sys_goldengate_overlay_tunnel_remove_tunnel(uint8 lchip, ctc_overlay_tunnel_param_t* p_tunnel_param)
{
    sys_scl_entry_t scl_entry;
    sys_scl_action_t scl_action;
    uint32 gid = 0;
    uint32 ipda_info = p_tunnel_param->ipda.ipv4;

    SYS_OVERLAY_INIT_CHECK(lchip);
    SYS_OVERLAY_TUNNEL_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_tunnel_param);
    SYS_OVERLAY_TUNNEL_TYEP_CHECK(p_tunnel_param);
    SYS_OVERLAY_TUNNEL_SCL_ID_CHECK(p_tunnel_param->scl_id);

    if (!CTC_FLAG_ISSET(p_tunnel_param->flag, CTC_OVERLAY_TUNNEL_FLAG_DEFAULT_ENTRY))
    {
        SYS_OVERLAY_TUNNEL_VNI_CHECK(p_tunnel_param->src_vn_id);
    }

    if (CTC_FLAG_ISSET(p_tunnel_param->flag, CTC_OVERLAY_TUNNEL_FLAG_DEFAULT_ENTRY)
        && CTC_FLAG_ISSET(p_tunnel_param->flag, CTC_OVERLAY_TUNNEL_FLAG_USE_FLEX))
    {
        return CTC_E_INVALID_PARAM;
    }

    SYS_OVERLAY_TUNNEL_DBG_FUNC();

    sal_memset(&scl_entry, 0, sizeof(sys_scl_entry_t));
    sal_memset(&scl_action, 0, sizeof(sys_scl_action_t));

    if(_sys_goldengate_overlay_tunnel_is_ipda_index(lchip, p_tunnel_param))
    {
        CTC_ERROR_RETURN(_sys_goldengate_overlay_tunnel_get_ipda_index(lchip, p_tunnel_param->type, p_tunnel_param->ipda.ipv4 , &ipda_info));
    }

    /* build scl key */
    CTC_ERROR_RETURN(_sys_goldengate_overlay_tunnel_build_key(lchip, &scl_entry, p_tunnel_param, ipda_info));

    if (CTC_FLAG_ISSET(p_tunnel_param->flag, CTC_OVERLAY_TUNNEL_FLAG_DEFAULT_ENTRY))
    {
        scl_entry.action.type = SYS_SCL_ACTION_TUNNEL;
        CTC_ERROR_RETURN(sys_goldengate_scl_set_default_tunnel_action(lchip, scl_entry.key.type, &(scl_entry.action)));
        return CTC_E_NONE;
    }

    /* remove entry */
    if (CTC_FLAG_ISSET(p_tunnel_param->flag, CTC_OVERLAY_TUNNEL_FLAG_USE_FLEX))
    {
        gid = SYS_SCL_GROUP_ID_INNER_TCAM_OVERLAY_TUNNEL + p_tunnel_param->scl_id;
    }
    else
    {
        gid = SYS_SCL_GROUP_ID_INNER_HASH_OVERLAY_TUNNEL;
    }
    CTC_ERROR_RETURN(sys_goldengate_scl_get_entry_id(lchip, &scl_entry, gid));
    CTC_ERROR_RETURN(sys_goldengate_scl_get_action(lchip, scl_entry.entry_id, &scl_action, 1));
    CTC_ERROR_RETURN(sys_goldengate_scl_uninstall_entry(lchip, scl_entry.entry_id, 1));
    CTC_ERROR_RETURN(sys_goldengate_scl_remove_entry(lchip, scl_entry.entry_id, 1));

    if (_sys_goldengate_overlay_tunnel_is_ipda_index(lchip, p_tunnel_param))
    {
        CTC_ERROR_RETURN(_sys_goldengate_overlay_tunnel_del_ipda_index(lchip, p_tunnel_param->type, p_tunnel_param->ipda.ipv4 , &ipda_info));
    }

    if (scl_action.u.tunnel_action.router_mac_profile_en)
    {
        CTC_ERROR_RETURN(sys_goldengate_l3if_unbinding_inner_route_mac(lchip, scl_action.u.tunnel_action.router_mac_profile));
    }

    return CTC_E_NONE;
}


int32
sys_goldengate_overlay_tunnel_update_tunnel(uint8 lchip, ctc_overlay_tunnel_param_t* p_tunnel_param)
{
    sys_scl_entry_t scl_entry;
    sys_scl_action_t scl_action;
    uint32 gid = 0;
    int32 ret = CTC_E_NONE;
    uint32 ipda_info = p_tunnel_param->ipda.ipv4;

    SYS_OVERLAY_INIT_CHECK(lchip);
    SYS_OVERLAY_TUNNEL_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_tunnel_param);
    SYS_OVERLAY_TUNNEL_TYEP_CHECK(p_tunnel_param);
    SYS_OVERLAY_TUNNEL_SCL_ID_CHECK(p_tunnel_param->scl_id);
    CTC_MAX_VALUE_CHECK(p_tunnel_param->tpid_index, 3);

    if (!CTC_FLAG_ISSET(p_tunnel_param->flag, CTC_OVERLAY_TUNNEL_FLAG_DEFAULT_ENTRY))
    {
        SYS_OVERLAY_TUNNEL_VNI_CHECK(p_tunnel_param->src_vn_id);
    }

    if (CTC_FLAG_ISSET(p_tunnel_param->flag, CTC_OVERLAY_TUNNEL_FLAG_DEFAULT_ENTRY)
        && CTC_FLAG_ISSET(p_tunnel_param->flag, CTC_OVERLAY_TUNNEL_FLAG_USE_FLEX))
    {
        return CTC_E_INVALID_PARAM;
    }

    SYS_OVERLAY_TUNNEL_DBG_FUNC();

    sal_memset(&scl_entry, 0, sizeof(sys_scl_entry_t));
    sal_memset(&scl_action, 0, sizeof(sys_scl_action_t));

    if (_sys_goldengate_overlay_tunnel_is_ipda_index(lchip, p_tunnel_param))
    {
        CTC_ERROR_RETURN(_sys_goldengate_overlay_tunnel_get_ipda_index(lchip, p_tunnel_param->type, p_tunnel_param->ipda.ipv4 , &ipda_info));
    }

    /* build scl key */
    CTC_ERROR_RETURN(_sys_goldengate_overlay_tunnel_build_key(lchip, &scl_entry, p_tunnel_param, ipda_info));

    /* build scl action*/
    CTC_ERROR_RETURN(_sys_goldengate_overlay_tunnel_build_action(lchip, &(scl_entry.action), p_tunnel_param));

    if (CTC_FLAG_ISSET(p_tunnel_param->flag, CTC_OVERLAY_TUNNEL_FLAG_DEFAULT_ENTRY))
    {
        CTC_ERROR_RETURN(sys_goldengate_scl_set_default_tunnel_action(lchip, scl_entry.key.type, &(scl_entry.action)));
        return CTC_E_NONE;
    }

    /* update action */
    if (CTC_FLAG_ISSET(p_tunnel_param->flag, CTC_OVERLAY_TUNNEL_FLAG_USE_FLEX))
    {
        gid = SYS_SCL_GROUP_ID_INNER_TCAM_OVERLAY_TUNNEL + p_tunnel_param->scl_id;
    }
    else
    {
        gid = SYS_SCL_GROUP_ID_INNER_HASH_OVERLAY_TUNNEL;
    }
    ret = sys_goldengate_scl_get_entry_id(lchip, &scl_entry, gid);
    if (ret < 0)
    {
        goto END;
    }
    ret = sys_goldengate_scl_get_action(lchip, scl_entry.entry_id, &scl_action, 1);/*get old action*/
    if (ret < 0)
    {
        goto END;
    }
    ret = sys_goldengate_scl_update_action(lchip, scl_entry.entry_id , &(scl_entry.action), 1);
    if (ret < 0)
    {
        goto END;
    }

    if (scl_action.u.tunnel_action.router_mac_profile_en)/*remove old*/
    {
        CTC_ERROR_RETURN(sys_goldengate_l3if_unbinding_inner_route_mac(lchip, scl_action.u.tunnel_action.router_mac_profile));
    }

END:
    if (ret < 0)
    {
        if (scl_entry.action.u.tunnel_action.router_mac_profile_en)/*build in _sys_goldengate_overlay_tunnel_build_action*/
        {
            CTC_ERROR_RETURN(sys_goldengate_l3if_unbinding_inner_route_mac(lchip, scl_entry.action.u.tunnel_action.router_mac_profile));
        }
        return ret;
    }

    return CTC_E_NONE;
}

int32 sys_goldengate_overlay_tunnel_set_vxlan_udp_port(uint8 lchip, uint16 vxlan_udp_port)
{
    uint32 cmd = 0;
    ds_t ds_data;

    LCHIP_CHECK(lchip);

    /* global config for UDP dest port and flag*/
    sal_memset(ds_data, 0, sizeof(ds_data));
    cmd = DRV_IOR(EpePktProcCtl_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &ds_data);
    SetEpePktProcCtl(V, vxlanV4UdpDestPort_f, ds_data, vxlan_udp_port);
    SetEpePktProcCtl(V, vxlanV6UdpDestPort_f, ds_data, vxlan_udp_port);
    cmd = DRV_IOW(EpePktProcCtl_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, ds_data);

    sal_memset(ds_data, 0, sizeof(ds_data));
    cmd = DRV_IOR(ParserLayer4AppCtl_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, ds_data);
    SetParserLayer4AppCtl(V, vxlanIpv4UdpDestPort_f, ds_data, vxlan_udp_port);
    SetParserLayer4AppCtl(V, vxlanIpv6UdpDestPort_f, ds_data, vxlan_udp_port);
    cmd = DRV_IOW(ParserLayer4AppCtl_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, ds_data);

    return CTC_E_NONE;
}


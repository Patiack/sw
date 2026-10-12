/**
@file sys_goldengate_l3if.c

@date 2009 - 12 - 7

@version v2.0

---file comments----
*/
#include "sal.h"
#include "ctc_error.h"
#include "ctc_debug.h"
#include "ctc_const.h"
#include "ctc_pdu.h"
#include "ctc_warmboot.h"
#include "sys_goldengate_common.h"
#include "sys_goldengate_chip.h"
#include "sys_goldengate_opf.h"
#include "sys_goldengate_l3if.h"
#include "sys_goldengate_nexthop_api.h"
#include "sys_goldengate_nexthop.h"
#include "sys_goldengate_vlan.h"
#include "sys_goldengate_scl.h"
#include "sys_goldengate_port.h"
#include "sys_goldengate_rpf_spool.h"
#include "sys_goldengate_stats.h"
#include "sys_goldengate_register.h"
#include "sys_goldengate_ftm.h"
#include "sys_goldengate_wb_common.h"

#include "goldengate/include/drv_io.h"

/**********************************************************************************
Defines and Macros
***********************************************************************************/
#define SYS_ROUTER_MAC_ENTRY_NUM        32
#define SYS_ROUTER_MAC_NUM_PER_ENTRY    4
#define SYS_L3IF_INNER_ROUTE_MAC_NUM    64
#define MAX_L3IF_RELATED_PROPERTY       2
#define MAX_L3IF_ECMP_GROUP_NUM         1024
#define SYS_ROUTER_MAC_NUM              128

#define SYS_L3IF_INIT_CHECK(lchip)                                  \
do {                                                              \
    SYS_LCHIP_CHECK_ACTIVE(lchip);                                  \
    if (NULL == p_gg_l3if_master[lchip])                          \
    {                                                          \
        return CTC_E_NOT_INIT;                                 \
    }                                                          \
} while(0)

#define SYS_L3IF_CREATE_LOCK(lchip)                             \
    do                                                          \
    {                                                           \
        sal_mutex_create(&p_gg_l3if_master[lchip]->mutex);      \
        if (NULL == p_gg_l3if_master[lchip]->mutex)             \
        {                                                       \
            CTC_ERROR_RETURN(CTC_E_FAIL_CREATE_MUTEX);          \
        }                                                       \
    } while (0)

#define SYS_L3IF_LOCK(lchip) \
    sal_mutex_lock(p_gg_l3if_master[lchip]->mutex)

#define SYS_L3IF_UNLOCK(lchip) \
    sal_mutex_unlock(p_gg_l3if_master[lchip]->mutex)

#define CTC_ERROR_RETURN_L3IF_UNLOCK(op) \
    do \
    { \
        int32 rv = (op); \
        if (rv < 0) \
        { \
            sal_mutex_unlock(p_gg_l3if_master[lchip]->mutex); \
            return rv; \
        } \
    } while (0)

#define SYS_L3IF_CREATE_CHECK(l3if_id)                  \
if (FALSE == p_gg_l3if_master[lchip]->l3if_prop[l3if_id].vaild)   \
{                                                       \
    return CTC_E_L3IF_NOT_EXIST;                        \
}

#define SYS_L3IFID_VAILD_CHECK(l3if_id)         \
if (l3if_id == 0 || l3if_id > SYS_GG_MAX_CTC_L3IF_ID)  \
{                                               \
    return CTC_E_L3IF_INVALID_IF_ID;            \
}

#define SYS_L3IF_DBG_OUT(level, FMT, ...)\
    CTC_DEBUG_OUT(l3if, l3if, L3IF_SYS, level, FMT, ##__VA_ARGS__)

#define SYS_L3IF_DUMP(FMT, ...)                    \
    do                                                       \
    {                                                        \
        CTC_DEBUG_OUT(l3if, l3if, L3IF_SYS, CTC_DEBUG_LEVEL_DUMP, FMT, ##__VA_ARGS__); \
    } while (0)

enum sys_l3if_route_mac_type_e
{
    SYS_L3IF_ROUTE_MAC_FREE,
    SYS_L3IF_ROUTE_MAC_NORMAL,
    SYS_L3IF_ROUTE_MAC_USE,
    SYS_L3IF_ROUTE_MAC_VMAC
};
typedef enum sys_l3if_route_mac_type_e sys_l3if_route_mac_type_t;

struct sys_l3if_router_mac_s
{
    uint16                  ref;
    uint16                  num_type;       /* router mac num type, normal mac = 1, vmac > 1 */
    mac_addr_t           mac;
};
typedef struct sys_l3if_router_mac_s sys_l3if_router_mac_t;

struct sys_l3if_inner_route_mac_s
{
    uint32 ref_cnt;
    mac_addr_t route_mac;
};
typedef struct sys_l3if_inner_route_mac_s sys_l3if_inner_route_mac_t;

struct sys_l3if_master_s
{
    sys_l3if_prop_t  l3if_prop[SYS_GG_MAX_CTC_L3IF_ID + 1];
    sys_l3if_router_mac_t router_mac[SYS_ROUTER_MAC_NUM];
    ctc_vector_t* ecmp_group_vec;
    uint16 max_vrfid_num;
    uint8  ipv4_vrf_en;
    uint8  ipv6_vrf_en;

    sys_l3if_inner_route_mac_t* inner_route_mac[SYS_L3IF_INNER_ROUTE_MAC_NUM];
    sal_mutex_t* mutex;
};
typedef struct sys_l3if_master_s sys_l3if_master_t;

/****************************************************************************
Global and Declaration
*****************************************************************************/

sys_l3if_master_t* p_gg_l3if_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};

int32
sys_goldengate_l3if_wb_sync(uint8 lchip);
int32
sys_goldengate_l3if_wb_restore(uint8 lchip);
extern int32
sys_goldengate_nh_update_ecmp_interface_group(uint8 lchip, sys_l3if_ecmp_if_t* p_group);
extern int32 sys_goldengate_l3if_wb_restore(uint8 lchip);
extern int32
sys_goldengate_stats_add_ecmp_stats(uint8 lchip, uint32 stats_id, uint16 ecmp_group_id);
extern int32
sys_goldengate_stats_remove_ecmp_stats(uint8 lchip, uint32 stats_id, uint16 ecmp_group_id);
/**********************************************************************************
Define API function interfaces
***********************************************************************************/

STATIC int32
_sys_goldengate_l3if_router_mac_alloc_offset(uint8 lchip, uint32 block_size, uint32* o_offset)
{
    uint32 offset = 0;
    sys_goldengate_opf_t opf;

    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));

    opf.pool_type = OPF_ROUTER_MAC;

    if (block_size == 1)
    {
        opf.multiple = 1;
    }
    else if (block_size == 2)
    {
        opf.multiple = 2;
    }
    else if (block_size > 2)
    {
        opf.multiple = 4;
    }

    CTC_ERROR_RETURN(sys_goldengate_opf_alloc_offset(lchip, &opf, block_size, &offset));

    if(o_offset)
    {
        *o_offset = offset;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_l3if_router_mac_free_offset(uint8 lchip, uint32 block_size, uint32 offset)
{
    sys_goldengate_opf_t opf;

    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));

    opf.pool_type = OPF_ROUTER_MAC;
    CTC_ERROR_RETURN(sys_goldengate_opf_free_offset(lchip, &opf, block_size, offset));

    return CTC_E_NONE;
}

/**
@brief    sort router mac
*/
int32
_sys_goldengate_l3if_sort_router_mac(ctc_l3if_router_mac_t* router_mac)
{
    uint8 idx1 = 0;
    uint8 idx2 = 0;
    uint64 mac1 = 0;
    uint64 mac2 = 0;
    mac_addr_t tmp_mac;
    mac_addr_t  mac[4];
    uint8 mac_ct = router_mac->num;

     if (0 == mac_ct)
     {
         return CTC_E_NONE;
     }

    sal_memcpy(mac, router_mac->mac, sizeof(mac_addr_t) * router_mac->num);

    for (idx1 = 0; idx1 < mac_ct - 1; idx1++)
    {
        for (idx2 = 0; idx2 < mac_ct - 1 - idx1; idx2++)
        {
            mac1 = (mac[idx2][0] << 8) | (mac[idx2][1]);
            mac1 <<= 32;
            mac1 |= ((mac[idx2][2] << 24) | (mac[idx2][3] << 16) |
                (mac[idx2][4] << 8) | (mac[idx2][5]));
            mac2 = (mac[idx2+1][0] << 8) | (mac[idx2+1][1]);
            mac2 <<= 32;
            mac2 |= ((mac[idx2+1][2] << 24) | (mac[idx2+1][3] << 16) |
                (mac[idx2+1][4] << 8) | (mac[idx2+1][5]));
            if (mac1 > mac2)
            {
                sal_memcpy(tmp_mac, mac[idx2], sizeof(mac_addr_t));
                sal_memcpy(mac[idx2], mac[idx2+1], sizeof(mac_addr_t));
                sal_memcpy(mac[idx2+1], tmp_mac, sizeof(mac_addr_t));
            }
        }
    }

    idx2 = 0;
    for (idx1 = 0; idx1 < router_mac->num - 1; idx1++)
    {
        if (!sal_memcmp(mac[idx1], mac[idx1+1], sizeof(mac_addr_t)))
        {
            mac_ct--;
            continue;
        }
        sal_memcpy(router_mac->mac[idx2++], mac[idx1], sizeof(mac_addr_t));
    }
    sal_memcpy(router_mac->mac[idx2++], mac[idx1], sizeof(mac_addr_t));

    router_mac->num = mac_ct;

    return CTC_E_NONE;
}

STATIC void
_sys_goldengate_l3if_print_router_mac(uint8 lchip)
{
    uint16 idx = 0;
    uint16 idx0 = 0;
    uint16 idx1 = 0;
    uint8 sub_idx = 0;
    sys_l3if_router_mac_t *rtmac = p_gg_l3if_master[lchip]->router_mac;
    SYS_L3IF_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "---------------------------------------------------------\n");
    SYS_L3IF_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "%-6s %-4s %-18s %-6s %-4s %-16s\n", "Index", "Ref", "Mac", "Index", "Ref", "Mac")
    SYS_L3IF_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "---------------------------------------------------------\n");
    for (idx = 0; idx < SYS_ROUTER_MAC_ENTRY_NUM/2; idx++)
    {
        for (sub_idx = 0; sub_idx < SYS_ROUTER_MAC_NUM_PER_ENTRY; sub_idx++)
        {
            idx0 = idx*SYS_ROUTER_MAC_NUM_PER_ENTRY + sub_idx;
            idx1 = (idx + (SYS_ROUTER_MAC_ENTRY_NUM / 2))*SYS_ROUTER_MAC_NUM_PER_ENTRY + sub_idx;
            SYS_L3IF_DBG_OUT(CTC_DEBUG_LEVEL_INFO,"%-6d %-4d %.2x%.2x.%.2x%.2x.%.2x%.2x     %-6d %-4d %.2x%.2x.%.2x%.2x.%.2x%.2x\n",
                       idx0, rtmac[idx0].ref, rtmac[idx0].mac[0], rtmac[idx0].mac[1], rtmac[idx0].mac[2], rtmac[idx0].mac[3], rtmac[idx0].mac[4], rtmac[idx0].mac[5],
                       idx1, rtmac[idx1].ref, rtmac[idx1].mac[0], rtmac[idx1].mac[1], rtmac[idx1].mac[2], rtmac[idx1].mac[3], rtmac[idx1].mac[4], rtmac[idx1].mac[5]);
        }
    }
    SYS_L3IF_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "---------------------------------------------------------\n");
    for (idx = 0; idx <= SYS_GG_MAX_CTC_L3IF_ID; idx++)
    {
        if (p_gg_l3if_master[lchip]->l3if_prop[idx].rtmac_bmp)
        {
            SYS_L3IF_DBG_OUT(CTC_DEBUG_LEVEL_INFO,"ifid = %d, rtmac_index = %d, rtmac_bmp = 0x%x\n", idx,
                p_gg_l3if_master[lchip]->l3if_prop[idx].rtmac_index,
                p_gg_l3if_master[lchip]->l3if_prop[idx].rtmac_bmp);
        }
    }
}

STATIC int32
_sys_goldengate_l3if_bmp2num(uint8 bitmap)
{
    uint8 i = 0;
    uint8 number = 0;

    if (0 == bitmap)
    {
        return 0;
    }
    for (i = 0; i < 8 ; i++)
    {
        if (CTC_IS_BIT_SET(bitmap, i))
        {
            number++;
        }
    }

    return number;
}

STATIC uint32
_sys_goldengate_l3if_get_egress_router_mac_index(uint8 lchip, uint32 idx, mac_addr_t egress_router_mac)
{
    uint8 sub_idx = 0;
    sys_l3if_router_mac_t *rtmac = p_gg_l3if_master[lchip]->router_mac;

    for (sub_idx = 0; sub_idx < SYS_ROUTER_MAC_NUM_PER_ENTRY; sub_idx++)
    {

        if (0 == sal_memcmp(egress_router_mac, rtmac[idx + sub_idx].mac, sizeof(mac_addr_t)))/* match */
        {
            return (idx + sub_idx);
        }
    }

    return 0;
}


STATIC int32
_sys_goldengate_l3if_router_mac_lookup(uint8 lchip, ctc_l3if_router_mac_t* router_mac, uint32* index, uint8*  bitmap, uint8 bypass_index)
{
    uint8 idx = 0;
    uint8 sub_idx = 0;
    uint8 rtmac_index = 0;
    uint8 loop_cnt = 0;
    uint8 bmp = 0;
    uint8 i = 0;
    sys_l3if_router_mac_t *rtmac = p_gg_l3if_master[lchip]->router_mac;

    CTC_PTR_VALID_CHECK(index);
    CTC_PTR_VALID_CHECK(router_mac);

    *index = 0xFF;
    loop_cnt = SYS_ROUTER_MAC_NUM / SYS_ROUTER_MAC_NUM_PER_ENTRY;
    /* 1. search in each DsRouterMac */
    for(idx = 0; idx < loop_cnt; idx++)
    {
        bmp = 0;
        /* 2. each mac for input*/
        for (sub_idx = 0; sub_idx < router_mac->num; sub_idx++)
        {
            /* 3. compare each mac in DsRouterMac */
            for (i = 0; i < SYS_ROUTER_MAC_NUM_PER_ENTRY; i++)
            {
                if ((0 == idx) && (0 == i))/* skip system route mac */
                {
                    continue;
                }
                rtmac_index = idx * SYS_ROUTER_MAC_NUM_PER_ENTRY + i;
                if (bypass_index == rtmac_index)/* not compare for itself when do merge, e.g. index 2 no need compare to index 2*/
                {
                    continue;
                }
                if (0 == sal_memcmp(router_mac->mac[sub_idx], rtmac[rtmac_index].mac, sizeof(mac_addr_t)))/* match */
                {
                    CTC_BIT_SET(bmp, i);
                    break;
                }
            }
        }

        if (_sys_goldengate_l3if_bmp2num(bmp) == router_mac->num )/* all input mac is in one DsRouterMac*/
        {
            *index = idx * SYS_ROUTER_MAC_NUM_PER_ENTRY;
            *bitmap = bmp;
            return CTC_E_NONE;
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_l3if_merge_interface_router_mac(uint8 lchip, uint16 bypass_l3if_id)
{
    uint16 l3if_id = 0;
    uint8 rtmac_index = 0;
    uint32 idx = 0;
    uint32 egs_idx = 0;
    uint8 rtmac_bmp = 0;
    uint8 bmp = 0;
    uint8 sub_idx = 0;
    uint32 cmd = 0;
    uint32 mac[2] = {0};
    uint8 first_sub_idx = 0;
    uint8 bypass_index = 0;
    DsSrcInterface_m   src_interface;
    DsDestInterface_m dest_interface;
    DsEgressRouterMac_m egress_router_mac;
    ctc_l3if_router_mac_t router_mac;
    sys_l3if_router_mac_t old_rtmac[4];
    sys_l3if_router_mac_t *rtmac = p_gg_l3if_master[lchip]->router_mac;

    for (l3if_id = 1; l3if_id <= SYS_GG_MAX_CTC_L3IF_ID; l3if_id++)
    {
        sal_memset(&router_mac, 0, sizeof(router_mac));
        rtmac_index = p_gg_l3if_master[lchip]->l3if_prop[l3if_id].rtmac_index;
        rtmac_bmp = p_gg_l3if_master[lchip]->l3if_prop[l3if_id].rtmac_bmp;
        if ((l3if_id == bypass_l3if_id) || (0 == rtmac_bmp) || (0xF == rtmac_bmp))
        {
            continue;
        }

        sal_memcpy(old_rtmac, &rtmac[rtmac_index], sizeof(old_rtmac));

        for (sub_idx = 0; sub_idx < SYS_ROUTER_MAC_NUM_PER_ENTRY; sub_idx++)
        {
            if (CTC_IS_BIT_SET(rtmac_bmp, sub_idx))
            {
                bypass_index = rtmac_index + sub_idx;
                sal_memcpy(router_mac.mac[router_mac.num++], rtmac[rtmac_index + sub_idx].mac, sizeof(mac_addr_t));
                rtmac[rtmac_index + sub_idx].ref--;
                if(0 == rtmac[rtmac_index + sub_idx].ref)
                {
                    sal_memset(rtmac[rtmac_index + sub_idx].mac, 0, sizeof(mac_addr_t));
                }
            }
        }

        if (router_mac.num)
        {
            CTC_ERROR_RETURN(_sys_goldengate_l3if_router_mac_lookup(lchip, &router_mac, &idx, &bmp, bypass_index));
            if ((0xFF == idx) || (0 == bmp))
            {
                sal_memcpy(&rtmac[rtmac_index], old_rtmac, sizeof(old_rtmac));
                continue;
            }
        }
        else
        {
            continue;
        }

        for (sub_idx = 0; sub_idx < SYS_ROUTER_MAC_NUM_PER_ENTRY; sub_idx++)
        {
            if (CTC_IS_BIT_SET(bmp, sub_idx))/* for egress router mac, set the first index*/
            {
                rtmac[idx + sub_idx].ref++;
                if (0 == first_sub_idx)
                {
                    first_sub_idx = sub_idx;
                }
            }
            if (CTC_IS_BIT_SET(rtmac_bmp, sub_idx) && (0 == rtmac[rtmac_index + sub_idx].ref))/*free old index*/
            {
                _sys_goldengate_l3if_router_mac_free_offset(lchip, 1, rtmac_index + sub_idx);
            }
        }

        SYS_GOLDENGATE_SET_HW_MAC(mac, p_gg_l3if_master[lchip]->l3if_prop[l3if_id].egress_router_mac);
        egs_idx = _sys_goldengate_l3if_get_egress_router_mac_index(lchip, idx, p_gg_l3if_master[lchip]->l3if_prop[l3if_id].egress_router_mac);
        SetDsEgressRouterMac(A, routerMac_f, &egress_router_mac, mac);
        cmd = DRV_IOW(DsEgressRouterMac_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, egs_idx, cmd, &egress_router_mac));

        cmd = DRV_IOR(DsSrcInterface_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, l3if_id, cmd, &src_interface));
        SetDsSrcInterface(V, routerMacProfile_f, &src_interface, idx / SYS_ROUTER_MAC_NUM_PER_ENTRY);
        SetDsSrcInterface(V, routerMacSelBitmap_f, &src_interface, bmp);
        cmd = DRV_IOW(DsSrcInterface_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, l3if_id, cmd, &src_interface));

        cmd = DRV_IOR(DsDestInterface_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, l3if_id, cmd, &dest_interface));
        SetDsDestInterface(V, routerMacProfile_f, &dest_interface, egs_idx);
        cmd = DRV_IOW(DsDestInterface_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, l3if_id, cmd, &dest_interface));

        p_gg_l3if_master[lchip]->l3if_prop[l3if_id].rtmac_index = (idx / SYS_ROUTER_MAC_NUM_PER_ENTRY)*SYS_ROUTER_MAC_NUM_PER_ENTRY;
        p_gg_l3if_master[lchip]->l3if_prop[l3if_id].rtmac_bmp = bmp;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_l3if_add_interface_router_mac(uint8 lchip, uint16 l3if_id, ctc_l3if_router_mac_t router_mac, mac_addr_t first_router_mac,  uint8* alloc_new)
{
    int32  ret = CTC_E_NONE;
    uint32 idx = 0;
    uint32 egs_idx = 0;
    uint32 cmd = 0;
    uint32 mac[2] = {0};
    uint8  sub_idx = 0;
    uint8  step = 0;
    uint8  rtmac_bmp = 0;
    uint8 first_sub_idx = 0;/* for egress */
    DsSrcInterface_m   src_interface;
    DsSrcInterface_m   old_src_interface;
    DsDestInterface_m dest_interface;
    DsRouterMac_m ingress_router_mac;
    DsEgressRouterMac_m egress_router_mac;
    sys_l3if_router_mac_t *rtmac = p_gg_l3if_master[lchip]->router_mac;

    SYS_L3IF_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    CTC_ERROR_RETURN(_sys_goldengate_l3if_router_mac_lookup(lchip, &router_mac, &idx, &rtmac_bmp, 0xFF));

    if (rtmac_bmp)/* find old*/
    {
        for (sub_idx = 0; sub_idx < SYS_ROUTER_MAC_NUM_PER_ENTRY; sub_idx++)
        {
            if (CTC_IS_BIT_SET(rtmac_bmp, sub_idx))
            {
                rtmac[idx + sub_idx].ref++;
                if (0 == first_sub_idx)
                {
                    first_sub_idx = sub_idx;
                }
            }
        }
        *alloc_new = 0;
        SYS_L3IF_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Find share conditon and merge, index = %d, rtmac_bmp = 0x%x\n", idx, rtmac_bmp);
    }
    else
    {
        CTC_ERROR_RETURN(_sys_goldengate_l3if_router_mac_alloc_offset(lchip, router_mac.num, &idx));
        for (sub_idx = 0; sub_idx < router_mac.num; sub_idx++)
        {
            rtmac_bmp |= 1 << ((idx + sub_idx) % SYS_ROUTER_MAC_NUM_PER_ENTRY);
            sal_memcpy(rtmac[idx + sub_idx].mac, router_mac.mac[sub_idx], sizeof(mac_addr_t));
            rtmac[idx + sub_idx].ref++;
        }

        cmd = DRV_IOR(DsRouterMac_t, DRV_ENTRY_FLAG);
        CTC_ERROR_GOTO(DRV_IOCTL(lchip, idx / SYS_ROUTER_MAC_NUM_PER_ENTRY, cmd, &ingress_router_mac), ret, error0);

        for (sub_idx = 0; sub_idx < router_mac.num; sub_idx++)
        {
            SYS_GOLDENGATE_SET_HW_MAC(mac, router_mac.mac[sub_idx]);

            step = ((idx + sub_idx) % SYS_ROUTER_MAC_NUM_PER_ENTRY) * 2;
            SetDsRouterMac(V, routerMac0En_f + step, &ingress_router_mac, 1);
            SetDsRouterMac(A, routerMac0_f + step, &ingress_router_mac, mac);
        }
        cmd = DRV_IOW(DsRouterMac_t, DRV_ENTRY_FLAG);
        CTC_ERROR_GOTO(DRV_IOCTL(lchip, idx / SYS_ROUTER_MAC_NUM_PER_ENTRY, cmd, &ingress_router_mac), ret, error0);
        *alloc_new = 1;
        SYS_L3IF_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Alloc new, index = %d, rtmac_bmp = 0x%x\n", idx, rtmac_bmp);
    }

    SYS_GOLDENGATE_SET_HW_MAC(mac, first_router_mac);
    egs_idx = _sys_goldengate_l3if_get_egress_router_mac_index(lchip, idx, first_router_mac);
    SetDsEgressRouterMac(A, routerMac_f, &egress_router_mac, mac);
    cmd = DRV_IOW(DsEgressRouterMac_t, DRV_ENTRY_FLAG);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, egs_idx, cmd, &egress_router_mac), ret, error0);

    cmd = DRV_IOR(DsSrcInterface_t, DRV_ENTRY_FLAG);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, l3if_id, cmd, &src_interface), ret, error0);

    sal_memcpy(&old_src_interface, &src_interface, sizeof(DsSrcInterface_m));
    SetDsSrcInterface(V, routerMacProfile_f, &src_interface, idx/SYS_ROUTER_MAC_NUM_PER_ENTRY);
    SetDsSrcInterface(V, routerMacSelBitmap_f, &src_interface, rtmac_bmp);
    cmd = DRV_IOW(DsSrcInterface_t, DRV_ENTRY_FLAG);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, l3if_id, cmd, &src_interface), ret, error0);

    cmd = DRV_IOR(DsDestInterface_t, DRV_ENTRY_FLAG);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, l3if_id, cmd, &dest_interface), ret, error1);
    SetDsDestInterface(V, routerMacProfile_f, &dest_interface, egs_idx);
    cmd = DRV_IOW(DsDestInterface_t, DRV_ENTRY_FLAG);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, l3if_id, cmd, &dest_interface), ret, error1);

    p_gg_l3if_master[lchip]->l3if_prop[l3if_id].rtmac_index = (idx / SYS_ROUTER_MAC_NUM_PER_ENTRY)*SYS_ROUTER_MAC_NUM_PER_ENTRY;
    p_gg_l3if_master[lchip]->l3if_prop[l3if_id].rtmac_bmp = rtmac_bmp;
    sal_memcpy(p_gg_l3if_master[lchip]->l3if_prop[l3if_id].egress_router_mac, first_router_mac, sizeof(mac_addr_t));

    return CTC_E_NONE;

error1:
    cmd = DRV_IOW(DsSrcInterface_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, l3if_id, cmd, &old_src_interface);

error0:
    for (sub_idx = 0; sub_idx < SYS_ROUTER_MAC_NUM_PER_ENTRY; sub_idx++)
    {
        if (CTC_IS_BIT_SET(rtmac_bmp, sub_idx))
        {
            rtmac[idx + sub_idx].ref--;
            if (0 == rtmac[idx + sub_idx].ref)
            {
                _sys_goldengate_l3if_router_mac_free_offset(lchip, 1, idx + sub_idx);
                sal_memset(&rtmac[idx + sub_idx], 0, sizeof(sys_l3if_router_mac_t));
            }
        }
    }

    return ret;
}

STATIC int32
_sys_goldengate_l3if_remove_interface_router_mac(uint8 lchip, uint16 l3if_id)
{
    uint8 sub_idx = 0;
    uint8 rtmac_bmp = p_gg_l3if_master[lchip]->l3if_prop[l3if_id].rtmac_bmp;
    uint8 idx = p_gg_l3if_master[lchip]->l3if_prop[l3if_id].rtmac_index;

    SYS_L3IF_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    CTC_MAX_VALUE_CHECK(idx, SYS_ROUTER_MAC_NUM - 1);


    for (sub_idx = 0; sub_idx < SYS_ROUTER_MAC_NUM_PER_ENTRY; sub_idx++)
    {
        if (CTC_IS_BIT_SET(rtmac_bmp, sub_idx))
        {
            if (p_gg_l3if_master[lchip]->router_mac[idx + sub_idx].ref)
            {
                p_gg_l3if_master[lchip]->router_mac[idx + sub_idx].ref--;
            }

            if (0 == p_gg_l3if_master[lchip]->router_mac[idx + sub_idx].ref)
            {
                _sys_goldengate_l3if_router_mac_free_offset(lchip, 1, idx + sub_idx);
                sal_memset(&p_gg_l3if_master[lchip]->router_mac[idx + sub_idx], 0, sizeof(sys_l3if_router_mac_t));
            }
        }
    }

    p_gg_l3if_master[lchip]->l3if_prop[l3if_id].rtmac_index = 0;
    p_gg_l3if_master[lchip]->l3if_prop[l3if_id].rtmac_bmp = 0;

    return CTC_E_NONE;
}


/**
@brief    init l3 interface module
*/
int32
sys_goldengate_l3if_init(uint8 lchip, ctc_l3if_global_cfg_t* l3if_global_cfg)
{
    uint32 index = 0;
    uint32 src_if_cmd = 0;
    uint32 dest_if_cmd = 0;
    uint32 igs_router_mac_cmd = 0;
    uint32 egs_router_mac_cmd = 0;
    ds_t src_interface = {0};
    ds_t dest_interface = {0};
    ds_t ingress_router_mac = {0};
    ds_t egress_router_mac = {0};
    uint32 entry_num = 0;
    sys_goldengate_opf_t opf;

    LCHIP_CHECK(lchip);

    if (p_gg_l3if_master[lchip] != NULL)
    {
        return CTC_E_NONE;
    }

    p_gg_l3if_master[lchip] = (sys_l3if_master_t*)mem_malloc(MEM_L3IF_MODULE, sizeof(sys_l3if_master_t));
    if (NULL == p_gg_l3if_master[lchip])
    {
        return CTC_E_NO_MEMORY;
    }
    sal_memset(p_gg_l3if_master[lchip], 0, sizeof(sys_l3if_master_t));
    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));

    p_gg_l3if_master[lchip]->max_vrfid_num = l3if_global_cfg->max_vrfid_num;
    p_gg_l3if_master[lchip]->ipv4_vrf_en  = l3if_global_cfg->ipv4_vrf_en;
    p_gg_l3if_master[lchip]->ipv6_vrf_en  = l3if_global_cfg->ipv6_vrf_en;

    /*RouterMac entry 0 is reserved for system router mac, sdk use system router mac as default router mac*/
    SetDsSrcInterface(V, routerMacProfile_f, src_interface, 0);
    SetDsDestInterface(V, routerMacProfile_f, dest_interface, 0);

    src_if_cmd = DRV_IOW(DsSrcInterface_t, DRV_ENTRY_FLAG);
    dest_if_cmd = DRV_IOW(DsDestInterface_t, DRV_ENTRY_FLAG);
    igs_router_mac_cmd = DRV_IOW(DsRouterMac_t, DRV_ENTRY_FLAG);
    egs_router_mac_cmd = DRV_IOW(DsEgressRouterMac_t, DRV_ENTRY_FLAG);


    for (index = 0; index < SYS_ROUTER_MAC_ENTRY_NUM; index++)
    {
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, igs_router_mac_cmd, &ingress_router_mac));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, egs_router_mac_cmd, &egress_router_mac));
    }

    for (index = 0; index <= SYS_GG_MAX_CTC_L3IF_ID; index++)
    {
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, src_if_cmd, &src_interface));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, dest_if_cmd, &dest_interface));
    }

    for (index = 0; index <= SYS_GG_MAX_CTC_L3IF_ID; index++)
    {
        p_gg_l3if_master[lchip]->l3if_prop[index].vaild = FALSE;
        p_gg_l3if_master[lchip]->l3if_prop[index].vlan_ptr = 0;
        p_gg_l3if_master[lchip]->l3if_prop[index].rtmac_index = 0;
    }

    p_gg_l3if_master[lchip]->ecmp_group_vec = ctc_vector_init(4, MAX_L3IF_ECMP_GROUP_NUM/4);

    CTC_ERROR_RETURN(sys_goldengate_opf_init(lchip, OPF_ROUTER_MAC, 1));
    CTC_ERROR_RETURN(sys_goldengate_ftm_query_table_entry_num(lchip, DsRouterMac_t, &entry_num));
    if (entry_num)
    {
        opf.pool_index = 0;
        opf.pool_type  = OPF_ROUTER_MAC;
        CTC_ERROR_RETURN(sys_goldengate_opf_init_offset(lchip, &opf, 1, entry_num*4 - 1));
    }

    SYS_L3IF_CREATE_LOCK(lchip);
    CTC_ERROR_RETURN(sys_goldengate_wb_sync_register_cb(lchip, CTC_FEATURE_L3IF, sys_goldengate_l3if_wb_sync));

    if (CTC_WB_ENABLE && CTC_WB_STATUS(lchip) == CTC_WB_STATUS_RELOADING)
    {
        CTC_ERROR_RETURN(sys_goldengate_l3if_wb_restore(lchip));
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_l3if_free_node_data(void* node_data, void* user_data)
{
    mem_free(node_data);

    return CTC_E_NONE;
}

int32
sys_goldengate_l3if_deinit(uint8 lchip)
{
    uint8 i = 0;

    LCHIP_CHECK(lchip);
    if (NULL == p_gg_l3if_master[lchip])
    {
        return CTC_E_NONE;
    }

    sys_goldengate_opf_deinit(lchip, OPF_ROUTER_MAC);

    /*free inner route mac*/
    for (i = 1; i < SYS_L3IF_INNER_ROUTE_MAC_NUM; i++)
    {
        if (p_gg_l3if_master[lchip]->inner_route_mac[i])
        {
            mem_free(p_gg_l3if_master[lchip]->inner_route_mac[i]);
        }
    }

    /*free ecmp group*/
    ctc_vector_traverse(p_gg_l3if_master[lchip]->ecmp_group_vec, (vector_traversal_fn)_sys_goldengate_l3if_free_node_data, NULL);
    ctc_vector_release(p_gg_l3if_master[lchip]->ecmp_group_vec);

    sal_mutex_destroy(p_gg_l3if_master[lchip]->mutex);
    mem_free(p_gg_l3if_master[lchip]);

    return CTC_E_NONE;
}

/**
@brief  Create l3 interfaces, gport should be global logical port
*/
int32
sys_goldengate_l3if_create(uint8 lchip, uint16 l3if_id, ctc_l3if_t* l3_if)
{
    uint16  vlan_ptr = 0;
    int32 ret = 0;
    uint16 index;
    uint32 src_if_cmd = 0;
    uint32 dest_if_cmd = 0;
    ds_t src_interface = {0};
    ds_t dest_interface = {0};
    uint16 exception3_en = 0;
    uint8 subif_use_scl = 0;
    uint8 gchip = 0;

    SYS_L3IF_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(l3_if);
    SYS_L3IFID_VAILD_CHECK(l3if_id);
    SYS_L3IF_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_L3IF_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "l3if_type:%d ,l3if_id :%d port:0x%x, vlan_id:%d \n",
                      l3_if->l3if_type, l3if_id, l3_if->gport, l3_if->vlan_id);

    SYS_L3IF_LOCK(lchip);
    if (TRUE == p_gg_l3if_master[lchip]->l3if_prop[l3if_id].vaild)
    {
        SYS_L3IF_UNLOCK(lchip);
        return CTC_E_L3IF_EXIST;
    }

    for (index = 0; index <= SYS_GG_MAX_CTC_L3IF_ID; index++)
    {
        if (CTC_L3IF_TYPE_SERVICE_IF == l3_if->l3if_type)
        {
            break;
        }

        if ((p_gg_l3if_master[lchip]->l3if_prop[index].vaild) &&
            (p_gg_l3if_master[lchip]->l3if_prop[index].l3if_type == l3_if->l3if_type) &&
            (p_gg_l3if_master[lchip]->l3if_prop[index].gport == l3_if->gport) &&
            (p_gg_l3if_master[lchip]->l3if_prop[index].vlan_id == l3_if->vlan_id))
        {
            SYS_L3IF_UNLOCK(lchip);
            return CTC_E_L3IF_EXIST;
        }
    }
    SYS_L3IF_UNLOCK(lchip);

    switch (l3_if->l3if_type)
    {
    case CTC_L3IF_TYPE_PHY_IF:
        {
            CTC_GLOBAL_PORT_CHECK(l3_if->gport);
            vlan_ptr  = 0x1000 + l3if_id;
            break;
        }

    case CTC_L3IF_TYPE_SUB_IF:
        {
            CTC_VLAN_RANGE_CHECK(l3_if->vlan_id);
            CTC_GLOBAL_PORT_CHECK(l3_if->gport);
            vlan_ptr  = 0x1000 + l3if_id;

            gchip = SYS_MAP_CTC_GPORT_TO_GCHIP(l3_if->gport);
            if(CTC_IS_LINKAGG_PORT(l3_if->gport) || sys_goldengate_chip_is_local(lchip, gchip))
            {
                subif_use_scl = 1;
            }
            break;
        }

    case CTC_L3IF_TYPE_VLAN_IF:
        {
            uint32 tmp_l3if_id = 0;
            CTC_VLAN_RANGE_CHECK(l3_if->vlan_id);
            sys_goldengate_vlan_get_vlan_ptr(lchip, l3_if->vlan_id, &vlan_ptr);
            if (!vlan_ptr)
            {
                return CTC_E_NOT_EXIST;
            }

            ret = sys_goldengate_vlan_get_internal_property(lchip, vlan_ptr, SYS_VLAN_PROP_INTERFACE_ID, &tmp_l3if_id);
            if (ret == CTC_E_NONE
                && (tmp_l3if_id != SYS_L3IF_INVALID_L3IF_ID)
                && (tmp_l3if_id != l3if_id))
            {
                return CTC_E_L3IF_EXIST;
            }
            else if (ret == CTC_E_NONE)
            {
                CTC_ERROR_RETURN(sys_goldengate_vlan_set_internal_property(lchip, vlan_ptr, SYS_VLAN_PROP_INTERFACE_ID, l3if_id));
            }
            else
            {
                return ret;
            }
            break;
        }

    case CTC_L3IF_TYPE_SERVICE_IF:
        {
            vlan_ptr  = 0x1000 + l3if_id;
            break;
        }
    default:
        return CTC_E_INVALID_PARAM;
    }

    SYS_L3IF_LOCK(lchip);
    /*only save the last port property*/
    p_gg_l3if_master[lchip]->l3if_prop[l3if_id].gport = l3_if->gport;
    p_gg_l3if_master[lchip]->l3if_prop[l3if_id].vlan_id = l3_if->vlan_id;
    p_gg_l3if_master[lchip]->l3if_prop[l3if_id].l3if_type = l3_if->l3if_type;
    p_gg_l3if_master[lchip]->l3if_prop[l3if_id].vaild = TRUE;
    p_gg_l3if_master[lchip]->l3if_prop[l3if_id].vlan_ptr = vlan_ptr;
    p_gg_l3if_master[lchip]->l3if_prop[l3if_id].l3if_id  = l3if_id;
    p_gg_l3if_master[lchip]->l3if_prop[l3if_id].rtmac_index = 0;
    p_gg_l3if_master[lchip]->l3if_prop[l3if_id].rtmac_bmp = 0;

    SYS_L3IF_UNLOCK(lchip);

    if (CTC_L3IF_TYPE_SUB_IF == l3_if->l3if_type && subif_use_scl)
    {
        sys_scl_entry_t   scl_entry;
        sal_memset(&scl_entry, 0, sizeof(sys_scl_entry_t));
        /* KEY */
        scl_entry.key.u.hash_port_svlan_key.gport = l3_if->gport;
        scl_entry.key.u.hash_port_svlan_key.svlan = l3_if->vlan_id;
        scl_entry.key.u.hash_port_svlan_key.dir = CTC_INGRESS;
        scl_entry.key.type = SYS_SCL_KEY_HASH_PORT_SVLAN;


        /* AD */
        scl_entry.action.type = SYS_SCL_ACTION_INGRESS;
        scl_entry.action.u.igs_action.user_vlanptr = vlan_ptr;
        CTC_SET_FLAG(scl_entry.action.u.igs_action.flag, CTC_SCL_IGS_ACTION_FLAG_USER_VLANPTR);
        CTC_ERROR_RETURN(sys_goldengate_scl_add_entry(lchip, SYS_SCL_GROUP_ID_INNER_HASH_VLAN_MAPPING_IGS, &scl_entry, 1));
        CTC_ERROR_RETURN(sys_goldengate_scl_install_entry(lchip, scl_entry.entry_id, 1));
    }

    src_if_cmd = DRV_IOR(DsSrcInterface_t, DRV_ENTRY_FLAG);
    ret = ret ? ret : DRV_IOCTL(lchip, l3if_id, src_if_cmd, &src_interface);

    /*Config SrcInterface default value*/
    exception3_en = 1 << CTC_L3PDU_ACTION_INDEX_RIP;   /* bit 0 always set to 1, 0 reserved for rip to cpu */
    exception3_en |= 1 << CTC_L3PDU_ACTION_INDEX_LDP;  /* bit 1 always set to 1, 1 reserved for ldp to cpu */
    exception3_en |= 1 << CTC_L3PDU_ACTION_INDEX_OSPF; /* bit 2 always set to 1, 2 reserved for ospf to cpu */
    exception3_en |= 1 << CTC_L3PDU_ACTION_INDEX_PIM;  /* bit 3 always set to 1, 3 reserved for pim to cpu */
    exception3_en |= 1 << CTC_L3PDU_ACTION_INDEX_BGP;  /* bit 4 always set to 1, 4 reserved for bgp to cpu */
    exception3_en |= 1 << CTC_L3PDU_ACTION_INDEX_RSVP; /* bit 5 always set to 1, 5 reserved for rsvp to cpu */
    exception3_en |= 1 << CTC_L3PDU_ACTION_INDEX_MSDP; /* bit 6 always set to 1, 6 reserved for msdp to cpu */
    exception3_en |= 1 << CTC_L3PDU_ACTION_INDEX_VRRP; /* bit 8 always set to 1, 8 reserved for vrrp to cpu */
    SetDsSrcInterface(V, exception3En_f, src_interface, exception3_en);
    SetDsSrcInterface(V, routerMacProfile_f, src_interface, 0);
    SetDsSrcInterface(V, routeLookupMode_f, src_interface, 0);
    SetDsSrcInterface(V, vrfId_f, src_interface, 0);
    SetDsSrcInterface(V, v4UcastEn_f, src_interface, 1);
    SetDsSrcInterface(V, routeDisable_f, src_interface, 0);
    SetDsSrcInterface(V, routerMacSelBitmap_f, src_interface, 0x1);

    /*Config DestInterface default value*/
    SetDsDestInterface(V, routerMacProfile_f, dest_interface, 0);
    if (CTC_L3IF_TYPE_VLAN_IF == l3_if->l3if_type || CTC_L3IF_TYPE_SUB_IF == l3_if->l3if_type )
    {
        SetDsDestInterface(V, vlanId_f, dest_interface, l3_if->vlan_id);
        SetDsDestInterface(V, interfaceSvlanTagged_f, dest_interface, 1);
    }

    src_if_cmd = DRV_IOW(DsSrcInterface_t, DRV_ENTRY_FLAG);
    dest_if_cmd = DRV_IOW(DsDestInterface_t, DRV_ENTRY_FLAG);

    ret = ret ? ret : DRV_IOCTL(lchip, l3if_id, src_if_cmd, &src_interface);
    ret = ret ? ret : DRV_IOCTL(lchip, l3if_id, dest_if_cmd, &dest_interface);

    if (ret != 0)
    {
        sys_goldengate_l3if_delete(lchip, l3if_id, l3_if);
        return ret;
    }

    return CTC_E_NONE;
}

/**
@brief    Delete  l3 interfaces
*/
int32
sys_goldengate_l3if_delete(uint8 lchip, uint16 l3if_id, ctc_l3if_t* l3_if)
{
    uint32 src_if_cmd = 0;
    uint32 dest_if_cmd = 0;
    ds_t src_interface = {0};
    ds_t dest_interface = {0};
    uint8 subif_use_scl = 0;
    uint8 gchip = 0;

    SYS_L3IF_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(l3_if);
    SYS_L3IFID_VAILD_CHECK(l3if_id);
    SYS_L3IF_CREATE_CHECK(l3if_id);

    SYS_L3IF_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_L3IF_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "l3if_type:%d ,l3if_id :%d port:0x%x, vlan_id:%d \n",
                                           l3_if->l3if_type, l3if_id, l3_if->gport, l3_if->vlan_id);

    switch (l3_if->l3if_type)
    {
    case CTC_L3IF_TYPE_PHY_IF:
        {
            CTC_GLOBAL_PORT_CHECK(l3_if->gport);
            SYS_L3IF_LOCK(lchip);
            if ((p_gg_l3if_master[lchip]->l3if_prop[l3if_id].gport != l3_if->gport)
                || (p_gg_l3if_master[lchip]->l3if_prop[l3if_id].l3if_type != l3_if->l3if_type))
            {
                SYS_L3IF_UNLOCK(lchip);
                return CTC_E_L3IF_MISMATCH;
            }
            SYS_L3IF_UNLOCK(lchip);
            break;
        }

    case CTC_L3IF_TYPE_SUB_IF:
        {
            CTC_GLOBAL_PORT_CHECK(l3_if->gport);
            CTC_VLAN_RANGE_CHECK(l3_if->vlan_id);
            gchip = SYS_MAP_CTC_GPORT_TO_GCHIP(l3_if->gport);

            if(CTC_IS_LINKAGG_PORT(l3_if->gport) || sys_goldengate_chip_is_local(lchip, gchip))
            {
                subif_use_scl = 1;
            }

            SYS_L3IF_LOCK(lchip);
            if ((p_gg_l3if_master[lchip]->l3if_prop[l3if_id].gport != l3_if->gport)
                || (p_gg_l3if_master[lchip]->l3if_prop[l3if_id].vlan_id != l3_if->vlan_id)
                || (p_gg_l3if_master[lchip]->l3if_prop[l3if_id].l3if_type != l3_if->l3if_type))
            {
                SYS_L3IF_UNLOCK(lchip);
                return CTC_E_L3IF_MISMATCH;
            }
            SYS_L3IF_UNLOCK(lchip);
            break;
        }

    case CTC_L3IF_TYPE_VLAN_IF:
        {
            CTC_VLAN_RANGE_CHECK(l3_if->vlan_id);

            SYS_L3IF_LOCK(lchip);
            if ((p_gg_l3if_master[lchip]->l3if_prop[l3if_id].vlan_id != l3_if->vlan_id)
               || (p_gg_l3if_master[lchip]->l3if_prop[l3if_id].l3if_type != l3_if->l3if_type))
            {
                SYS_L3IF_UNLOCK(lchip);
                return CTC_E_L3IF_MISMATCH;
            }
            SYS_L3IF_UNLOCK(lchip);
            sys_goldengate_vlan_set_internal_property(lchip, l3_if->vlan_id, SYS_VLAN_PROP_INTERFACE_ID, 0);
            break;
        }
    case CTC_L3IF_TYPE_SERVICE_IF:
        {
            SYS_L3IF_LOCK(lchip);
            if(p_gg_l3if_master[lchip]->l3if_prop[l3if_id].l3if_type != l3_if->l3if_type)
            {
                SYS_L3IF_UNLOCK(lchip);
                return CTC_E_L3IF_MISMATCH;
            }
            SYS_L3IF_UNLOCK(lchip);
            break;
        }
    default:
        return CTC_E_INVALID_PARAM;
    }

    SYS_L3IF_LOCK(lchip);
    p_gg_l3if_master[lchip]->l3if_prop[l3if_id].vaild = FALSE;
    p_gg_l3if_master[lchip]->l3if_prop[l3if_id].vlan_id = 0;
    p_gg_l3if_master[lchip]->l3if_prop[l3if_id].vlan_ptr = 0;
    p_gg_l3if_master[lchip]->l3if_prop[l3if_id].l3if_id  = SYS_L3IF_INVALID_L3IF_ID;
    SYS_L3IF_UNLOCK(lchip);

    SetDsSrcInterface(V, routerMacProfile_f, src_interface, 0);
    SetDsSrcInterface(V, routeLookupMode_f, src_interface, 0);
    SetDsSrcInterface(V, vrfId_f, src_interface, 0);
    SetDsSrcInterface(V, v4UcastEn_f, src_interface, 0);
    SetDsSrcInterface(V, v4McastEn_f, src_interface, 0);
    SetDsSrcInterface(V, v6UcastEn_f, src_interface, 0);
    SetDsSrcInterface(V, v6McastEn_f, src_interface, 0);
    SetDsSrcInterface(V, routeDisable_f, src_interface, 0);
    SetDsSrcInterface(V, mplsEn_f, src_interface, 0);

    /*Config DestInterface default value*/
    SetDsDestInterface(V, routerMacProfile_f, dest_interface, 0);
    SetDsDestInterface(V, vlanId_f, dest_interface, 0);
    SetDsDestInterface(V, interfaceSvlanTagged_f, dest_interface, 0);

    src_if_cmd = DRV_IOW(DsSrcInterface_t, DRV_ENTRY_FLAG);
    dest_if_cmd = DRV_IOW(DsDestInterface_t, DRV_ENTRY_FLAG);


    CTC_ERROR_RETURN(DRV_IOCTL(lchip, l3if_id, src_if_cmd, &src_interface));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, l3if_id, dest_if_cmd, &dest_interface));

    if ((CTC_L3IF_TYPE_SUB_IF == l3_if->l3if_type) && subif_use_scl)
    {
        sys_scl_entry_t   scl_entry;
        sal_memset(&scl_entry, 0, sizeof(sys_scl_entry_t));
        /* KEY */
        scl_entry.key.u.hash_port_svlan_key.gport = l3_if->gport;
        scl_entry.key.u.hash_port_svlan_key.svlan = l3_if->vlan_id;
        scl_entry.key.u.hash_port_svlan_key.dir = CTC_INGRESS;
        scl_entry.key.type = SYS_SCL_KEY_HASH_PORT_SVLAN;

        CTC_ERROR_RETURN(sys_goldengate_scl_get_entry_id(lchip, &scl_entry, SYS_SCL_GROUP_ID_INNER_HASH_VLAN_MAPPING_IGS));
        CTC_ERROR_RETURN(sys_goldengate_scl_uninstall_entry(lchip, scl_entry.entry_id, 1));
        CTC_ERROR_RETURN(sys_goldengate_scl_remove_entry(lchip, scl_entry.entry_id, 1));
    }

    sys_goldengate_nh_del_ipmc_dsnh_offset(lchip, l3if_id);
    SYS_L3IF_LOCK(lchip);
    if (p_gg_l3if_master[lchip]->l3if_prop[l3if_id].rtmac_bmp)
    {
        _sys_goldengate_l3if_remove_interface_router_mac(lchip, l3if_id);
        CTC_ERROR_RETURN_L3IF_UNLOCK(_sys_goldengate_l3if_merge_interface_router_mac(lchip, l3if_id));
        _sys_goldengate_l3if_print_router_mac(lchip);
    }
    SYS_L3IF_UNLOCK(lchip);

    return CTC_E_NONE;
}

int32
sys_goldengate_l3if_get_l3if_id(uint8 lchip, ctc_l3if_t* l3_if, uint16* l3if_id)
{
    uint16 index;

    SYS_L3IF_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(l3_if);
    CTC_PTR_VALID_CHECK(l3if_id);

    SYS_L3IF_LOCK(lchip);
    if(l3_if->l3if_type == 0xFF &&  (*l3if_id !=0) )
    { /*get l3if info by L3if id*/
        if (*l3if_id <= SYS_GG_MAX_CTC_L3IF_ID && p_gg_l3if_master[lchip]->l3if_prop[*l3if_id].vaild)
        {
            index = *l3if_id;
            l3_if->l3if_type = p_gg_l3if_master[lchip]->l3if_prop[*l3if_id].l3if_type;
            l3_if->gport = p_gg_l3if_master[lchip]->l3if_prop[*l3if_id].gport;
            l3_if->vlan_id = p_gg_l3if_master[lchip]->l3if_prop[*l3if_id].vlan_id;
        }
        else
        {
            index = SYS_GG_MAX_CTC_L3IF_ID + 1;
        }
    }
    else
    {/*get L3if id by l3if info*/
        if (CTC_L3IF_TYPE_SERVICE_IF == l3_if->l3if_type)
        {
            SYS_L3IF_UNLOCK(lchip);
            return CTC_E_INVALID_PARAM;
        }
        for (index = 0; index <= SYS_GG_MAX_CTC_L3IF_ID; index++)
        {
            if ((p_gg_l3if_master[lchip]->l3if_prop[index].vaild) &&
                (p_gg_l3if_master[lchip]->l3if_prop[index].l3if_type == l3_if->l3if_type) &&
                (p_gg_l3if_master[lchip]->l3if_prop[index].gport == l3_if->gport) &&
                (p_gg_l3if_master[lchip]->l3if_prop[index].vlan_id == l3_if->vlan_id))
            {
                break;;
            }
        }
    }
    SYS_L3IF_UNLOCK(lchip);

    *l3if_id = index;

    if ((SYS_GG_MAX_CTC_L3IF_ID + 1) == index)
    {
        return CTC_E_L3IF_NOT_EXIST;
    }

    return CTC_E_NONE;

}


bool
sys_goldengate_l3if_is_port_phy_if(uint8 lchip, uint16 gport)
{
    uint16 index = 0;
    bool  ret = FALSE;

    SYS_L3IF_INIT_CHECK(lchip);

    SYS_L3IF_LOCK(lchip);
    for (index = 0; index <= SYS_GG_MAX_CTC_L3IF_ID; index++)
    {
        if (p_gg_l3if_master[lchip]->l3if_prop[index].vaild == TRUE
            && p_gg_l3if_master[lchip]->l3if_prop[index].l3if_type == CTC_L3IF_TYPE_PHY_IF
            && p_gg_l3if_master[lchip]->l3if_prop[index].gport == gport)
        {
            ret = TRUE;
            break;
        }
    }
    SYS_L3IF_UNLOCK(lchip);

    return ret;
}

bool
sys_goldengate_l3if_is_port_sub_if(uint8 lchip, uint16 gport)
{
    uint16 index = 0;
    bool  ret = FALSE;

    SYS_L3IF_INIT_CHECK(lchip);

    SYS_L3IF_LOCK(lchip);
    for (index = 0; index <= SYS_GG_MAX_CTC_L3IF_ID; index++)
    {
        if (p_gg_l3if_master[lchip]->l3if_prop[index].vaild == TRUE
            && p_gg_l3if_master[lchip]->l3if_prop[index].l3if_type == CTC_L3IF_TYPE_SUB_IF
            && p_gg_l3if_master[lchip]->l3if_prop[index].gport == gport)
        {
            ret = TRUE;
            break;
        }
    }
    SYS_L3IF_UNLOCK(lchip);

    return ret;
}

int32
sys_goldengate_l3if_get_l3if_info(uint8 lchip, uint8 query_type, sys_l3if_prop_t* l3if_prop)
{
    int32 ret = CTC_E_L3IF_NOT_EXIST;
    uint16  index = 0;
    bool find = FALSE;

    SYS_L3IF_INIT_CHECK(lchip);

    SYS_L3IF_LOCK(lchip);
    if (query_type == 1)
    {
        if (p_gg_l3if_master[lchip]->l3if_prop[l3if_prop->l3if_id].vaild == TRUE)
        {
            find = TRUE;
            index = l3if_prop->l3if_id;
        }
    }
    else
    {
        for (index = 0; index <= SYS_GG_MAX_CTC_L3IF_ID; index++)
        {
            if (p_gg_l3if_master[lchip]->l3if_prop[index].vaild == FALSE ||
                p_gg_l3if_master[lchip]->l3if_prop[index].l3if_type != l3if_prop->l3if_type)
            {
                continue;
            }

            if (l3if_prop->l3if_type == CTC_L3IF_TYPE_PHY_IF)
            {
                if (p_gg_l3if_master[lchip]->l3if_prop[index].gport == l3if_prop->gport)
                {
                    find = TRUE;
                    break;
                }
            }
            else if (l3if_prop->l3if_type == CTC_L3IF_TYPE_SUB_IF)
            {
                if (p_gg_l3if_master[lchip]->l3if_prop[index].gport == l3if_prop->gport &&
                    p_gg_l3if_master[lchip]->l3if_prop[index].vlan_id == l3if_prop->vlan_id)
                {
                    find = TRUE;
                    break;
                }
            }
            else if (l3if_prop->l3if_type == CTC_L3IF_TYPE_VLAN_IF)
            {
                if (p_gg_l3if_master[lchip]->l3if_prop[index].vlan_id == l3if_prop->vlan_id)
                {
                    find = TRUE;
                    break;
                }
            }
            else
            {
                continue;
            }
        }
    }

    if (find)
    {
        sal_memcpy(l3if_prop, &p_gg_l3if_master[lchip]->l3if_prop[index], sizeof(sys_l3if_prop_t));
        ret = CTC_E_NONE;
    }
    SYS_L3IF_UNLOCK(lchip);

    return ret;
}

int32
sys_goldengate_l3if_get_l3if_info_with_port_and_vlan(uint8 lchip, uint16 gport, uint16 vlan_id, sys_l3if_prop_t* l3if_prop)
{
    int32 ret = CTC_E_L3IF_NOT_EXIST;
    uint16  index = 0;
    bool find = FALSE;

    SYS_L3IF_INIT_CHECK(lchip);

    SYS_L3IF_LOCK(lchip);
    for (index = 0; index <= SYS_GG_MAX_CTC_L3IF_ID; index++)
    {
        if (p_gg_l3if_master[lchip]->l3if_prop[index].vaild == FALSE)
        {
            continue;
        }
        if (p_gg_l3if_master[lchip]->l3if_prop[index].l3if_type == CTC_L3IF_TYPE_SERVICE_IF)
        {
            continue;
        }
        if (p_gg_l3if_master[lchip]->l3if_prop[index].gport == gport
            && p_gg_l3if_master[lchip]->l3if_prop[index].l3if_type == CTC_L3IF_TYPE_PHY_IF)
        {
            find = TRUE;
            break;
        }
        else if (p_gg_l3if_master[lchip]->l3if_prop[index].l3if_type == CTC_L3IF_TYPE_SUB_IF)
        {
            if (p_gg_l3if_master[lchip]->l3if_prop[index].gport == gport &&
                p_gg_l3if_master[lchip]->l3if_prop[index].vlan_id == vlan_id)
            {
                find = TRUE;
                break;
            }
        }
    }

    if(!find)
    {
        for (index = 0; index <= SYS_GG_MAX_CTC_L3IF_ID; index++)
        {
            if (p_gg_l3if_master[lchip]->l3if_prop[index].vaild == FALSE)
            {
                continue;
            }

            if (p_gg_l3if_master[lchip]->l3if_prop[index].vlan_id == vlan_id
                     && p_gg_l3if_master[lchip]->l3if_prop[index].l3if_type == CTC_L3IF_TYPE_VLAN_IF)
            {
                find = TRUE;
                break;
            }
        }
    }

    if (find)
    {
        sal_memcpy(l3if_prop, &p_gg_l3if_master[lchip]->l3if_prop[index], sizeof(sys_l3if_prop_t));
        ret = CTC_E_NONE;
    }
    SYS_L3IF_UNLOCK(lchip);

    return ret;
}

/**
@brief    Config  l3 interface's  properties
*/
int32
sys_goldengate_l3if_set_property(uint8 lchip, uint16 l3if_id, ctc_l3if_property_t l3if_prop, uint32 value)
{
    uint32 cmd[MAX_L3IF_RELATED_PROPERTY] = {0};
    uint32 idx = 0;
    uint32 field[MAX_L3IF_RELATED_PROPERTY] = {0};

    SYS_L3IF_INIT_CHECK(lchip);
    SYS_L3IFID_VAILD_CHECK(l3if_id);
    SYS_L3IF_CREATE_CHECK(l3if_id);

    SYS_L3IF_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_L3IF_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "l3if_id :%d, l3if_prop:%d, value:%d \n", l3if_id, l3if_prop, value);

    SYS_L3IF_LOCK(lchip);
    switch (l3if_prop)
    {
    /*src  interface */
    case CTC_L3IF_PROP_IPV4_UCAST:
        field[0] = (value) ? 1 : 0;
        cmd[0] = DRV_IOW(DsSrcInterface_t, DsSrcInterface_v4UcastEn_f);
        break;

    case CTC_L3IF_PROP_IPV4_MCAST:
        field[0] = (value) ? 1 : 0;
        cmd[0] = DRV_IOW(DsSrcInterface_t, DsSrcInterface_v4McastEn_f);
        break;

    case CTC_L3IF_PROP_IPV4_SA_TYPE:
        if (value >= MAX_CTC_L3IF_IPSA_LKUP_TYPE)
        {
            SYS_L3IF_UNLOCK(lchip);
            return CTC_E_INVALID_PARAM;
        }
        field[0] = value;
        cmd[0] = DRV_IOW(DsSrcInterface_t, DsSrcInterface_v4UcastSaType_f);
        break;

    case CTC_L3IF_PROP_IPV6_UCAST:
        field[0] = (value) ? 1 : 0;
        cmd[0] = DRV_IOW(DsSrcInterface_t, DsSrcInterface_v6UcastEn_f);
        break;

    case CTC_L3IF_PROP_IPV6_MCAST:
        field[0] = (value) ? 1 : 0;
        cmd[0] = DRV_IOW(DsSrcInterface_t, DsSrcInterface_v6McastEn_f);
        break;

    case CTC_L3IF_PROP_IPV6_SA_TYPE:
        if (value >= MAX_CTC_L3IF_IPSA_LKUP_TYPE)
        {
            SYS_L3IF_UNLOCK(lchip);
            return CTC_E_INVALID_PARAM;
        }
        field[0] = value;
        cmd[0] = DRV_IOW(DsSrcInterface_t, DsSrcInterface_v6UcastSaType_f);
        break;

    case CTC_L3IF_PROP_VRF_ID:
        if (value >= p_gg_l3if_master[lchip]->max_vrfid_num)
        {
            SYS_L3IF_UNLOCK(lchip);
            return CTC_E_INVALID_PARAM;
        }
        field[0] = value;
        cmd[0] = DRV_IOW(DsSrcInterface_t, DsSrcInterface_vrfId_f);
        break;

    case CTC_L3IF_PROP_ROUTE_EN:
        field[0] = (value) ? 0 : 1;
        cmd[0] = DRV_IOW(DsSrcInterface_t, DsSrcInterface_routeDisable_f);
        break;

    case CTC_L3IF_PROP_NAT_IFTYPE:
        field[0] = (value) ? 1 : 0;
        cmd[0] = DRV_IOW(DsSrcInterface_t, DsSrcInterface_l3IfType_f);
        break;

    case CTC_L3IF_PROP_ROUTE_ALL_PKT:
        field[0] = (value) ? 1 : 0;
        cmd[0] = DRV_IOW(DsSrcInterface_t, DsSrcInterface_routeAllPackets_f);
        break;

    case CTC_L3IF_PROP_PBR_LABEL:
        if (value > MAX_CTC_L3IF_PBR_LABEL)
        {
            SYS_L3IF_UNLOCK(lchip);
            return CTC_E_L3IF_INVALID_PBR_LABEL;
        }
        field[0] = value;
        cmd[0] = DRV_IOW(DsSrcInterface_t, DsSrcInterface_pbrLabel_f);
        break;

    case CTC_L3IF_PROP_VRF_EN:
        field[0] = (value) ? 1 : 0;
        cmd[0] = DRV_IOW(DsSrcInterface_t, DsSrcInterface_routeLookupMode_f);
        break;

    case CTC_L3IF_PROP_MTU_EN:
        field[0] = (value) ? 1 : 0;
        if (sys_goldengate_get_cut_through_en(lchip))
        {
            SYS_L3IF_UNLOCK(lchip);
            return CTC_E_UNEXPECT;
        }
        cmd[0] = DRV_IOW(DsDestInterface_t, DsDestInterface_mtuCheckEn_f);
        break;

    case CTC_L3IF_PROP_MTU_EXCEPTION_EN:
        field[0] = (value) ? 1 : 0;
        cmd[0] = DRV_IOW(DsDestInterface_t, DsDestInterface_mtuExceptionEn_f);
        break;

    case CTC_L3IF_PROP_EGS_MCAST_TTL_THRESHOLD:
        if (value >= MAX_CTC_L3IF_MCAST_TTL_THRESHOLD)
        {
            SYS_L3IF_UNLOCK(lchip);
            return CTC_E_INVALID_PARAM;
        }
        field[0] = value;
        cmd[0] = DRV_IOW(DsDestInterface_t, DsDestInterface_mcastTtlThreshold_f);
        break;

    case CTC_L3IF_PROP_MTU_SIZE:
        if (value >= MAX_CTC_L3IF_MTU_SIZE)
        {
            SYS_L3IF_UNLOCK(lchip);
            return CTC_E_INVALID_PARAM;
        }
        field[0] = value;
        cmd[0] = DRV_IOW(DsDestInterface_t, DsDestInterface_mtuSize_f);
        break;

    case CTC_L3IF_PROP_MPLS_EN:
        field[0] = (value) ? 1 : 0;
        cmd[0] = DRV_IOW(DsSrcInterface_t, DsSrcInterface_mplsEn_f);
        break;

    case CTC_L3IF_PROP_MPLS_LABEL_SPACE:
        if (value > 0xFF)
        {
            SYS_L3IF_UNLOCK(lchip);
            return CTC_E_INVALID_PARAM;
        }
        field[0] = value;
        cmd[0] = DRV_IOW(DsSrcInterface_t, DsSrcInterface_mplsLabelSpace_f);
        field[1] = value ? 1 : 0;
        cmd[1] = DRV_IOW(DsSrcInterface_t, DsSrcInterface_interfaceLabelValid_f);
        break;

    case CTC_L3IF_PROP_RPF_CHECK_TYPE:
        if (!(value < SYS_RPF_CHK_TYPE_NUM))
        {
            SYS_L3IF_UNLOCK(lchip);
            return CTC_E_L3IF_INVALID_RPF_CHECK_TYPE;
        }
        field[0] = value;
        cmd[0] = DRV_IOW(DsSrcInterface_t, DsSrcInterface_rpfType_f);
        break;

    case CTC_L3IF_PROP_RPF_PERMIT_DEFAULT:
        field[0] = (value) ? 1 : 0;
        cmd[0] = DRV_IOW(DsSrcInterface_t, DsSrcInterface_rpfPermitDefault_f);
        break;

    case CTC_L3IF_PROP_IGMP_SNOOPING_EN:
        field[0] = (value) ? 1 : 0;
        cmd[0] = DRV_IOW(DsSrcInterface_t, DsSrcInterface_igmpSnoopEn_f);
        break;

    case CTC_L3IF_PROP_CONTEXT_LABEL_EXIST:
        field[0] = (value) ? 1 : 0;
        cmd[0] = DRV_IOW(DsSrcInterface_t, DsSrcInterface_contextLabelExist_f);
        break;

    case CTC_L3IF_PROP_STATS:
        if (value == 0) /* disable stats */
        {
            field[0] = 0;
        }
        else    /* enable stats */
        {
            uint16 stats_ptr  = 0;
            CTC_ERROR_RETURN_WITH_UNLOCK(sys_goldengate_stats_get_statsptr(lchip, value, CTC_STATS_STATSID_TYPE_L3IF, &stats_ptr), p_gg_l3if_master[lchip]->mutex);
            field[0] = stats_ptr;
        }

        cmd[0] = DRV_IOW(DsSrcInterface_t, DsSrcInterface_l3IfStatsPtr_f);
        break;

    case CTC_L3IF_PROP_EGS_STATS:
        if (value == 0) /* disable stats */
        {
            field[0] = 0;
        }
        else    /* enable stats */
        {
            uint16 stats_ptr  = 0;
            CTC_ERROR_RETURN_WITH_UNLOCK(sys_goldengate_stats_get_statsptr(lchip, value, CTC_STATS_STATSID_TYPE_L3IF, &stats_ptr), p_gg_l3if_master[lchip]->mutex);
            field[0] = stats_ptr;
        }

        cmd[0] = DRV_IOW(DsDestInterface_t, DsDestInterface_statsPtr_f);
        break;

    case CTC_L3IF_PROP_ROUTE_MAC_PREFIX_TYPE:
    case CTC_L3IF_PROP_ROUTE_MAC_LOW_8BITS:
    case CTC_L3IF_PROP_EGS_MAC_SA_PREFIX_TYPE:
    case CTC_L3IF_PROP_EGS_MAC_SA_LOW_8BITS:
        SYS_L3IF_UNLOCK(lchip);
        return CTC_E_FEATURE_NOT_SUPPORT;
    default:
        SYS_L3IF_UNLOCK(lchip);
        return CTC_E_INVALID_PARAM;
    }

    for (idx = 0; idx < MAX_L3IF_RELATED_PROPERTY; idx++)
    {
        if (0 != cmd[idx])
        {
            CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, l3if_id, cmd[idx], &field[idx]), p_gg_l3if_master[lchip]->mutex);
        }
    }
    SYS_L3IF_UNLOCK(lchip);

    return CTC_E_NONE;
}

/**
@brief    Get  l3 interface's properties  according to interface id
*/
int32
sys_goldengate_l3if_get_property(uint8 lchip, uint16 l3if_id, ctc_l3if_property_t l3if_prop, uint32* value)
{
    uint32 cmd = 0;

    SYS_L3IF_INIT_CHECK(lchip);
    SYS_L3IFID_VAILD_CHECK(l3if_id);
    SYS_L3IF_CREATE_CHECK(l3if_id);

    switch (l3if_prop)
    {
    /*src  interface */
    case CTC_L3IF_PROP_IPV4_UCAST:
        cmd = DRV_IOR(DsSrcInterface_t, DsSrcInterface_v4UcastEn_f);
        break;

    case CTC_L3IF_PROP_IPV4_MCAST:
        cmd = DRV_IOR(DsSrcInterface_t, DsSrcInterface_v4McastEn_f);
        break;

    case CTC_L3IF_PROP_IPV4_SA_TYPE:
        cmd = DRV_IOR(DsSrcInterface_t, DsSrcInterface_v4UcastSaType_f);
        break;

    case CTC_L3IF_PROP_IPV6_UCAST:
        cmd = DRV_IOR(DsSrcInterface_t, DsSrcInterface_v6UcastEn_f);
        break;

    case CTC_L3IF_PROP_IPV6_MCAST:
        cmd = DRV_IOR(DsSrcInterface_t, DsSrcInterface_v6McastEn_f);
        break;

    case CTC_L3IF_PROP_IPV6_SA_TYPE:
        cmd = DRV_IOR(DsSrcInterface_t, DsSrcInterface_v6UcastSaType_f);
        break;

    case CTC_L3IF_PROP_VRF_ID:
        cmd = DRV_IOR(DsSrcInterface_t, DsSrcInterface_vrfId_f);
        break;

    case CTC_L3IF_PROP_ROUTE_EN:
        cmd = DRV_IOR(DsSrcInterface_t, DsSrcInterface_routeDisable_f);
        break;

    case CTC_L3IF_PROP_NAT_IFTYPE:
        cmd = DRV_IOR(DsSrcInterface_t, DsSrcInterface_l3IfType_f);
        break;

    case CTC_L3IF_PROP_ROUTE_ALL_PKT:
        cmd = DRV_IOR(DsSrcInterface_t, DsSrcInterface_routeAllPackets_f);
        break;

    case CTC_L3IF_PROP_PBR_LABEL:
        cmd = DRV_IOR(DsSrcInterface_t, DsSrcInterface_pbrLabel_f);
        break;

    case CTC_L3IF_PROP_VRF_EN:
        cmd = DRV_IOR(DsSrcInterface_t, DsSrcInterface_routeLookupMode_f);
        break;

    /*dest  interface */
    case CTC_L3IF_PROP_MTU_EN:
        cmd = DRV_IOR(DsDestInterface_t, DsDestInterface_mtuCheckEn_f);
        break;

    case CTC_L3IF_PROP_MTU_EXCEPTION_EN:
        cmd = DRV_IOR(DsDestInterface_t, DsDestInterface_mtuExceptionEn_f);
        break;

    case CTC_L3IF_PROP_EGS_MCAST_TTL_THRESHOLD:
        cmd = DRV_IOR(DsDestInterface_t, DsDestInterface_mcastTtlThreshold_f);
        break;

    case CTC_L3IF_PROP_MTU_SIZE:
        cmd = DRV_IOR(DsDestInterface_t, DsDestInterface_mtuSize_f);
        break;

    case CTC_L3IF_PROP_MPLS_EN:
        cmd = DRV_IOR(DsSrcInterface_t, DsSrcInterface_mplsEn_f);
        break;

    case CTC_L3IF_PROP_MPLS_LABEL_SPACE:
        cmd = DRV_IOR(DsSrcInterface_t, DsSrcInterface_mplsLabelSpace_f);
        break;

    case CTC_L3IF_PROP_RPF_CHECK_TYPE:
        cmd = DRV_IOR(DsSrcInterface_t, DsSrcInterface_rpfType_f);
        break;

    case CTC_L3IF_PROP_RPF_PERMIT_DEFAULT:
        cmd = DRV_IOR(DsSrcInterface_t, DsSrcInterface_rpfPermitDefault_f);
        break;

    case CTC_L3IF_PROP_IGMP_SNOOPING_EN:
        cmd = DRV_IOR(DsSrcInterface_t, DsSrcInterface_igmpSnoopEn_f);
        break;

    case CTC_L3IF_PROP_CONTEXT_LABEL_EXIST:
        cmd = DRV_IOR(DsSrcInterface_t, DsSrcInterface_contextLabelExist_f);
        break;

    case CTC_L3IF_PROP_ROUTE_MAC_PREFIX_TYPE:
    case CTC_L3IF_PROP_ROUTE_MAC_LOW_8BITS:
    case CTC_L3IF_PROP_EGS_MAC_SA_PREFIX_TYPE:
    case CTC_L3IF_PROP_EGS_MAC_SA_LOW_8BITS:
        return CTC_E_FEATURE_NOT_SUPPORT;

    default:
        return CTC_E_INVALID_PARAM;
    }

    SYS_L3IF_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_L3IF_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "l3if_id :%d l3if_prop:%d \n", l3if_id, l3if_prop);

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, l3if_id, cmd, value));
    if (CTC_L3IF_PROP_ROUTE_EN == l3if_prop)
    {
        *value = !(*value);
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_l3if_set_exception3_en(uint8 lchip, uint16 l3if_id, uint8 index, bool enbale)
{
    uint32  cmd = 0;
    uint32  value = 0;

    SYS_L3IF_INIT_CHECK(lchip);
    SYS_L3IFID_VAILD_CHECK(l3if_id);
    SYS_L3IF_CREATE_CHECK(l3if_id);

    SYS_L3IF_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_L3IF_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "l3if_id = %d, index = %d, enbale[0]= %d\n", l3if_id, index, enbale);

    SYS_L3IF_LOCK(lchip);
    cmd = DRV_IOR(DsSrcInterface_t, DsSrcInterface_exception3En_f);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, l3if_id, cmd, &value), p_gg_l3if_master[lchip]->mutex);
    if (TRUE == enbale)
    {
        value |= (1 << index);

    }
    else
    {
        value &= (~(1 << index));
    }

    cmd = DRV_IOW(DsSrcInterface_t, DsSrcInterface_exception3En_f);

    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, l3if_id, cmd, &value), p_gg_l3if_master[lchip]->mutex);
    SYS_L3IF_UNLOCK(lchip);

    return CTC_E_NONE;
}

int32
sys_goldengate_l3if_get_exception3_en(uint8 lchip, uint16 l3if_id, uint8 index, bool* enbale)
{
    uint32  cmd = 0;
    uint32  value = 0;

    SYS_L3IF_INIT_CHECK(lchip);
    SYS_L3IFID_VAILD_CHECK(l3if_id);
    SYS_L3IF_CREATE_CHECK(l3if_id);
    CTC_PTR_VALID_CHECK(enbale);

    SYS_L3IF_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_L3IF_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "l3if_id = %d, index = %d\n", l3if_id, index);

    cmd = DRV_IOR(DsSrcInterface_t, DsSrcInterface_exception3En_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, l3if_id, cmd, &value));
    *enbale = (value & (1 << index)) ? TRUE : FALSE;
    return CTC_E_NONE;
}

/**
@brief    Config  router mac

*/
int32
sys_goldengate_l3if_set_router_mac(uint8 lchip, mac_addr_t mac_addr)
{
    uint32  cmd = 0;
    uint32  mac[2] = {0};
    ds_t    ingress_router_mac = {0};
    ds_t    egress_router_mac = {0};
    uint8 mac_0[6] = {0};
    uint8 mac_f[6] = {0xff,0xff,0xff,0xff,0xff,0xff};
    sys_l3if_router_mac_t *rtmac = NULL;
    IpeAclLookupCtl_m ipe_acl_lookup_ctl;
    IpePortMacCtl1_m ipe_port_mac_ctl1;

    SYS_L3IF_INIT_CHECK(lchip);
    SYS_L3IF_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_L3IF_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "router_mac:%.2x%.2x.%.2x%.2x.%.2x%.2x \n",
                      mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);

    SYS_GOLDENGATE_SET_HW_MAC(mac, mac_addr);

    cmd = DRV_IOR(DsRouterMac_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ingress_router_mac));

    SetDsRouterMac(V, routerMac0En_f, ingress_router_mac, 1);
    SetDsRouterMac(A, routerMac0_f, ingress_router_mac, mac);
    cmd = DRV_IOW(DsRouterMac_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ingress_router_mac));

    SetDsEgressRouterMac(A, routerMac_f, egress_router_mac, mac);
    cmd = DRV_IOW(DsEgressRouterMac_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, egress_router_mac));

    /*route by inner after overlay decap*/
    sal_memset(&ipe_acl_lookup_ctl, 0, sizeof(ipe_acl_lookup_ctl));
    cmd = DRV_IOR(IpeAclLookupCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_acl_lookup_ctl));
    SetIpeAclLookupCtl(V, systemRouteMacEn_f, &ipe_acl_lookup_ctl, 1);
    SetIpeAclLookupCtl(A, systemRouteMac_f, &ipe_acl_lookup_ctl, mac);
    cmd = DRV_IOW(IpeAclLookupCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_acl_lookup_ctl));

    sal_memset(&ipe_port_mac_ctl1, 0, sizeof(ipe_port_mac_ctl1));
    cmd = DRV_IOR(IpePortMacCtl1_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_port_mac_ctl1));
    SetIpePortMacCtl1(V, systemMacEn_f, &ipe_port_mac_ctl1, 1);
    SetIpePortMacCtl1(A, systemMac_f, &ipe_port_mac_ctl1, mac);
    cmd = DRV_IOW(IpePortMacCtl1_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_port_mac_ctl1));

    /* write to software */
    SYS_L3IF_LOCK(lchip);
    rtmac = p_gg_l3if_master[lchip]->router_mac;
    if ((sal_memcmp(mac_addr, &mac_0, sizeof(mac_addr_t)))
        && (sal_memcmp(mac_addr, &mac_f, sizeof(mac_addr_t))))
    {
        /* write to software */
        sal_memcpy(rtmac[0].mac, mac_addr, sizeof(mac_addr_t));
        rtmac[0].num_type = SYS_L3IF_ROUTE_MAC_NORMAL;
        rtmac[0].ref = 1;
    }
    else
    {
        sal_memset(&rtmac[0], 0, sizeof(sys_l3if_router_mac_t));
    }
    SYS_L3IF_UNLOCK(lchip);

    return CTC_E_NONE;
}

/**
@brief    Get  router mac
*/
int32
sys_goldengate_l3if_get_router_mac(uint8 lchip, mac_addr_t mac_addr)
{
    sys_l3if_router_mac_t *rtmac = NULL;

    SYS_L3IF_INIT_CHECK(lchip);
    SYS_L3IF_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    SYS_L3IF_LOCK(lchip);
    rtmac = p_gg_l3if_master[lchip]->router_mac;
    sal_memcpy(mac_addr, rtmac[0].mac, sizeof(mac_addr_t));
    SYS_L3IF_UNLOCK(lchip);

    SYS_L3IF_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Router mac:%.2x%.2x.%.2x%.2x.%.2x%.2x \n",
                      mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);

    return CTC_E_NONE;
}

/**
     @brief      Config l3 interface router mac
*/
int32
sys_goldengate_l3if_set_interface_router_mac(uint8 lchip, uint16 l3if_id, ctc_l3if_router_mac_t router_mac)
{
    int32 ret = CTC_E_NONE;
    uint8   idx = 0;
    uint8   sub_idx = 0;
    uint8 rtmac_bmp = 0;
    uint8 i = 0;
    uint8 alloc_new = 0;
    sys_l3if_router_mac_t *rtmac = NULL;
    ctc_l3if_router_mac_t old_router_mac;
    mac_addr_t first_router_mac;
    mac_addr_t old_first_router_mac;
    uint32 cmd = 0;
    uint32 value = 0;
    mac_addr_t mac = {0};

    SYS_L3IF_INIT_CHECK(lchip);
    SYS_L3IFID_VAILD_CHECK(l3if_id);
    SYS_L3IF_CREATE_CHECK(l3if_id);
    CTC_MAX_VALUE_CHECK(router_mac.num, SYS_ROUTER_MAC_NUM_PER_ENTRY);

    sal_memset(&old_router_mac, 0, sizeof(ctc_l3if_router_mac_t));

    SYS_L3IF_LOCK(lchip);
    rtmac = p_gg_l3if_master[lchip]->router_mac;
    idx = p_gg_l3if_master[lchip]->l3if_prop[l3if_id].rtmac_index;
    rtmac_bmp = p_gg_l3if_master[lchip]->l3if_prop[l3if_id].rtmac_bmp;

    sal_memcpy(first_router_mac, router_mac.mac[0], sizeof(mac_addr_t));
    sal_memcpy(old_first_router_mac, p_gg_l3if_master[lchip]->l3if_prop[l3if_id].egress_router_mac, sizeof(mac_addr_t));

    CTC_ERROR_GOTO(_sys_goldengate_l3if_sort_router_mac(&router_mac), ret, error);

    if(rtmac_bmp)/*save old and remove old in sw*/
    {
        for(sub_idx = 0; sub_idx < SYS_ROUTER_MAC_NUM_PER_ENTRY; sub_idx++)
        {
            if (CTC_IS_BIT_SET(rtmac_bmp, sub_idx))
            {
                sal_memcpy(old_router_mac.mac[i++], rtmac[idx + sub_idx].mac, sizeof(mac_addr_t));
                old_router_mac.num++;
            }
        }
        CTC_ERROR_GOTO(_sys_goldengate_l3if_remove_interface_router_mac(lchip, l3if_id), ret, error);
    }

    if ((0 == router_mac.num)||
        ((!sal_memcmp(router_mac.mac[0], mac, sizeof(mac_addr_t)))
        &&(!sal_memcmp(router_mac.mac[1], mac, sizeof(mac_addr_t)))
        &&(!sal_memcmp(router_mac.mac[2], mac, sizeof(mac_addr_t)))
        &&(!sal_memcmp(router_mac.mac[3], mac, sizeof(mac_addr_t)))))
    {
        value = 0;
        cmd = DRV_IOW(DsSrcInterface_t, DsSrcInterface_routerMacProfile_f);
        DRV_IOCTL(lchip, l3if_id, cmd, &value);
        value = 0x1;
        cmd = DRV_IOW(DsSrcInterface_t, DsSrcInterface_routerMacSelBitmap_f);
        DRV_IOCTL(lchip, l3if_id, cmd, &value);
        value = 0;
        cmd = DRV_IOW(DsDestInterface_t, DsDestInterface_routerMacProfile_f);
        DRV_IOCTL(lchip, l3if_id, cmd, &value);
        SYS_L3IF_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Remove interface router mac!\n");
    }
    else
    {
        /* add router mac to soft db and hw table */
        ret = _sys_goldengate_l3if_add_interface_router_mac(lchip, l3if_id, router_mac, first_router_mac, &alloc_new);
    }
    if (ret < 0)
    {
        if (rtmac_bmp)/* revert old router mac to soft db and hw table */
        {
            CTC_ERROR_GOTO(_sys_goldengate_l3if_add_interface_router_mac(lchip, l3if_id, old_router_mac, old_first_router_mac, &alloc_new), ret, error);
        }
    }
    else
    {
        if (alloc_new && (router_mac.num > 1))
        {
            CTC_ERROR_GOTO(_sys_goldengate_l3if_merge_interface_router_mac(lchip, l3if_id), ret, error);
        }
    }
    _sys_goldengate_l3if_print_router_mac(lchip);

error:
    SYS_L3IF_UNLOCK(lchip);

    return ret;
}

/**
     @brief       Get l3 interface router mac
*/
int32
sys_goldengate_l3if_get_interface_router_mac(uint8 lchip, uint16 l3if_id, ctc_l3if_router_mac_t *router_mac)
{
    uint8   idx = 0;
    uint8   sub_idx = 0;
    uint8   rtmac_bmp;
    uint8 i = 0;

    SYS_L3IF_INIT_CHECK(lchip);
    SYS_L3IFID_VAILD_CHECK(l3if_id);
    SYS_L3IF_CREATE_CHECK(l3if_id);

    idx = p_gg_l3if_master[lchip]->l3if_prop[l3if_id].rtmac_index;
    rtmac_bmp = p_gg_l3if_master[lchip]->l3if_prop[l3if_id].rtmac_bmp;

    sal_memset(router_mac, 0, sizeof(ctc_l3if_router_mac_t));
    if (0 == rtmac_bmp)
    {
        SYS_L3IF_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, " Entry not found \n");
            return CTC_E_NOT_EXIST;
    }
    else
    {
        for (sub_idx = 0; sub_idx < SYS_ROUTER_MAC_NUM_PER_ENTRY; sub_idx++)
        {
            if (CTC_IS_BIT_SET(rtmac_bmp, sub_idx))
            {
                sal_memcpy(router_mac->mac[i++], p_gg_l3if_master[lchip]->router_mac[idx + sub_idx].mac, sizeof(mac_addr_t));
                router_mac->num++;
            }
        }
    }

    return CTC_E_NONE;
}

/**
@brief get max vrf id , 0 means disable else vrfid must less than return value
*/
uint16
sys_goldengate_l3if_get_max_vrfid(uint8 lchip, uint8 ip_ver)
{
    return (MAX_CTC_IP_VER == ip_ver) ? (p_gg_l3if_master[lchip]->max_vrfid_num) :
           ((CTC_IP_VER_4 == ip_ver) ? (p_gg_l3if_master[lchip]->ipv4_vrf_en ? p_gg_l3if_master[lchip]->max_vrfid_num : 0)
            : (p_gg_l3if_master[lchip]->ipv6_vrf_en ? p_gg_l3if_master[lchip]->max_vrfid_num : 0));
}

int32
sys_goldengate_l3if_set_vrf_stats_en(uint8 lchip, ctc_l3if_vrf_stats_t* p_vrf_stats)
{
    DsVrf_m dsvrf;
    uint16 stats_ptr = 0;
    uint32 cmd = 0;

    SYS_L3IF_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_vrf_stats);
    CTC_MAX_VALUE_CHECK(p_vrf_stats->vrf_id, (p_gg_l3if_master[lchip]->max_vrfid_num-1))

    sal_memset(&dsvrf, 0, sizeof(dsvrf));

    if (p_vrf_stats->enable)
    {
        if (p_vrf_stats->type == CTC_L3IF_VRF_STATS_UCAST)
        {
            SetDsVrf(V, statsType_f, &dsvrf, 1);
        }
        else if (p_vrf_stats->type == CTC_L3IF_VRF_STATS_ALL)
        {
            SetDsVrf(V, statsType_f, &dsvrf, 0);
        }
        else
        {
            return CTC_E_INVALID_PARAM;
        }

        CTC_ERROR_RETURN(sys_goldengate_stats_get_statsptr(lchip, p_vrf_stats->stats_id, CTC_STATS_STATSID_TYPE_VRF, &stats_ptr));
        SetDsVrf(V, fwdStatsPtr_f, &dsvrf, stats_ptr);
    }

    cmd = DRV_IOW(DsVrf_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_vrf_stats->vrf_id, cmd, &dsvrf));

    return CTC_E_NONE;
}

int32
sys_goldengate_l3if_set_lm_en(uint8 lchip, uint16 l3if_id, uint8 enable)
{
    uint32 cmd = 0;
    uint32 value = enable?1:0;
    SYS_L3IF_LOCK(lchip);

    cmd = DRV_IOW(DsSrcInterface_t, DsSrcInterface_mplsSectionLmEn_f);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, l3if_id, cmd, &value), p_gg_l3if_master[lchip]->mutex);
    cmd = DRV_IOW(DsDestInterface_t, DsDestInterface_mplsSectionLmEn_f);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, l3if_id, cmd, &value), p_gg_l3if_master[lchip]->mutex);
    SYS_L3IF_UNLOCK(lchip);

    return CTC_E_NONE;
}
int32
sys_goldengate_l3if_create_ecmp_if(uint8 lchip, ctc_l3if_ecmp_if_t* p_ecmp_if)
{
    sys_goldengate_opf_t opf;
    sys_l3if_ecmp_if_t* p_group = NULL;
    uint32 hw_group_id = 0;
    uint16 max_group_num = 0;
    uint32 ecmp_mem_base = 0;
    uint16 max_ecmp = 0;
    uint16 cur_emcp_num = 0;
    int32 ret = CTC_E_NONE;

    SYS_L3IF_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_ecmp_if);
    CTC_VALUE_RANGE_CHECK(p_ecmp_if->ecmp_if_id, 1, MAX_L3IF_ECMP_GROUP_NUM - 1);
    CTC_MAX_VALUE_CHECK(p_ecmp_if->ecmp_type, CTC_NH_ECMP_TYPE_DLB);

    SYS_L3IF_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_L3IF_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Create ecmp interface %d\n", p_ecmp_if->ecmp_if_id);

    sal_memset(&opf, 0, sizeof(opf));

    SYS_L3IF_LOCK(lchip);
    /* check if ecmp group is exist */
    p_group = ctc_vector_get(p_gg_l3if_master[lchip]->ecmp_group_vec, p_ecmp_if->ecmp_if_id);
    if (NULL != p_group)
    {
        SYS_L3IF_UNLOCK(lchip);
        return CTC_E_ENTRY_EXIST;
    }
    SYS_L3IF_UNLOCK(lchip);


    CTC_ERROR_RETURN(sys_goldengate_nh_get_current_ecmp_group_num(lchip, &cur_emcp_num));

    /* check if ecmp group is valid */
    CTC_ERROR_RETURN(sys_goldengate_nh_get_max_ecmp_group_num(lchip, &max_group_num));
    if (cur_emcp_num  >= max_group_num)
    {
        return CTC_E_NO_RESOURCE;
    }

    /* alloc hw ecmp group id */
    opf.pool_type = OPF_ECMP_GROUP_ID;
    opf.pool_index = 0;
    opf.reverse    = 1;
    CTC_ERROR_RETURN(sys_goldengate_opf_alloc_offset(lchip, &opf, 1, &hw_group_id));

    opf.pool_type = OPF_ECMP_MEMBER_ID;
    opf.pool_index = 0;
    max_ecmp = SYS_GG_MAX_DEFAULT_ECMP_NUM;
    CTC_ERROR_RETURN(sys_goldengate_opf_alloc_offset(lchip, &opf, max_ecmp, &ecmp_mem_base));

    p_group = mem_malloc(MEM_L3IF_MODULE, sizeof(sys_l3if_ecmp_if_t));
    if (NULL == p_group)
    {
        ret = CTC_E_NO_MEMORY;
        goto error1;
    }
    sal_memset(p_group, 0, sizeof(sys_l3if_ecmp_if_t));
    p_group->hw_group_id = hw_group_id;
    p_group->ecmp_member_base = ecmp_mem_base;
    p_group->ecmp_group_type = p_ecmp_if->ecmp_type;
    p_group->failover_en = p_ecmp_if->failover_en;
    p_group->stats_id = p_ecmp_if->stats_id;

    SYS_L3IF_LOCK(lchip);
    /* add to db */
    if (FALSE == ctc_vector_add(p_gg_l3if_master[lchip]->ecmp_group_vec, p_ecmp_if->ecmp_if_id, (void*)p_group))
    {
        SYS_L3IF_UNLOCK(lchip);
        ret = CTC_E_NO_MEMORY;
        goto error0;
    }
    if (p_group->stats_id)
    {
        ret = sys_goldengate_stats_add_ecmp_stats(lchip, p_group->stats_id, p_group->hw_group_id);
        if(ret)
        {
            ctc_vector_del(p_gg_l3if_master[lchip]->ecmp_group_vec, p_ecmp_if->ecmp_if_id);
            goto error0;
        }
    }
    SYS_L3IF_UNLOCK(lchip);

    return CTC_E_NONE;

error0:
    mem_free(p_group);
error1:
    sys_goldengate_opf_free_offset(lchip, &opf, 1, hw_group_id);

    opf.pool_type = OPF_ECMP_MEMBER_ID;
    opf.pool_index = 0;
    sys_goldengate_opf_free_offset(lchip, &opf, max_ecmp, ecmp_mem_base);

    return ret;
}

int32
sys_goldengate_l3if_destory_ecmp_if(uint8 lchip, uint16 ecmp_if_id)
{
    sys_goldengate_opf_t opf;
    sys_l3if_ecmp_if_t* p_group = NULL;
    uint32 hw_group_id = 0;
    uint8 index = 0;
    uint32 ecmp_mem_base = 0;
    uint16 max_ecmp = 0;

    SYS_L3IF_INIT_CHECK(lchip);
    CTC_VALUE_RANGE_CHECK(ecmp_if_id, 1, MAX_L3IF_ECMP_GROUP_NUM - 1);

    SYS_L3IF_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_L3IF_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Destory ecmp interface %d\n", ecmp_if_id);

    sal_memset(&opf, 0, sizeof(opf));

    SYS_L3IF_LOCK(lchip);
    /* check if ecmp group is exist */
    p_group = ctc_vector_get(p_gg_l3if_master[lchip]->ecmp_group_vec, ecmp_if_id);
    if (NULL == p_group)
    {
        SYS_L3IF_UNLOCK(lchip);
        return CTC_E_ENTRY_NOT_EXIST;
    }

    /* free dsfwdptr */
    for (index = 0; index < p_group->intf_count; index ++)
    {
        sys_goldengate_nh_offset_free(lchip, SYS_NH_ENTRY_TYPE_FWD, 1, p_group->dsfwd_offset[index]);
    }

    /* reset hw ecmp group */
    p_group->intf_count = 0;
    sys_goldengate_nh_update_ecmp_interface_group(lchip, p_group);
    if(p_group->stats_id)
    {
        sys_goldengate_stats_remove_ecmp_stats(lchip, p_group->stats_id, p_group->hw_group_id);
    }

    max_ecmp = SYS_GG_MAX_DEFAULT_ECMP_NUM;
    /* free hw ecmp group id */
    hw_group_id = p_group->hw_group_id;
    opf.pool_type = OPF_ECMP_GROUP_ID;
    opf.pool_index = 0;
    CTC_ERROR_RETURN_L3IF_UNLOCK(sys_goldengate_opf_free_offset(lchip, &opf, 1, hw_group_id));

    ecmp_mem_base = p_group->ecmp_member_base;
    opf.pool_type = OPF_ECMP_MEMBER_ID;
    opf.pool_index = 0;
    CTC_ERROR_RETURN_L3IF_UNLOCK(sys_goldengate_opf_free_offset(lchip, &opf, max_ecmp, ecmp_mem_base));

    /* remove db and free memory */
    ctc_vector_del(p_gg_l3if_master[lchip]->ecmp_group_vec, ecmp_if_id);
    SYS_L3IF_UNLOCK(lchip);

    mem_free(p_group);

    return CTC_E_NONE;
}

int32
sys_goldengate_l3if_add_ecmp_if_member(uint8 lchip, uint16 ecmp_if_id, uint16 l3if_id)
{
    sys_l3if_ecmp_if_t* p_group = NULL;
    sys_nh_param_dsfwd_t dsfwd_param;
    uint16 max_ecmp = 0;
    uint8 index = 0;
    int32 ret = CTC_E_NONE;

    SYS_L3IF_INIT_CHECK(lchip);
    SYS_L3IFID_VAILD_CHECK(l3if_id);
    SYS_L3IF_CREATE_CHECK(l3if_id);
    CTC_VALUE_RANGE_CHECK(ecmp_if_id, 1, MAX_L3IF_ECMP_GROUP_NUM - 1);

    SYS_L3IF_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_L3IF_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Add ecmp interface %d member %d\n", ecmp_if_id, l3if_id);

    sal_memset(&dsfwd_param, 0, sizeof(dsfwd_param));

    SYS_L3IF_LOCK(lchip);
    if (p_gg_l3if_master[lchip]->l3if_prop[l3if_id].l3if_type != CTC_L3IF_TYPE_PHY_IF)
    {
        SYS_L3IF_UNLOCK(lchip);
        return CTC_E_L3IF_INVALID_IF_TYPE;
    }

    p_group = ctc_vector_get(p_gg_l3if_master[lchip]->ecmp_group_vec, ecmp_if_id);
    if (NULL == p_group)
    {
        SYS_L3IF_UNLOCK(lchip);
        return CTC_E_ENTRY_NOT_EXIST;
    }

    max_ecmp = SYS_GG_MAX_DEFAULT_ECMP_NUM;
    if (p_group->intf_count >= max_ecmp)
    {
        SYS_L3IF_UNLOCK(lchip);
        return CTC_E_EXCEED_MAX_SIZE;
    }

    /* check if the member is exist */
    for (index = 0; index < p_group->intf_count; index ++)
    {
        if (p_group->intf_array[index] == l3if_id)
        {
            break;
        }
    }

    if (index != p_group->intf_count)
    {
        SYS_L3IF_UNLOCK(lchip);
        return CTC_E_MEMBER_EXIST;
    }

    /* assign index */
    index = p_group->intf_count;

    /* alloc dsfwdptr*/
    CTC_ERROR_RETURN_L3IF_UNLOCK(sys_goldengate_nh_offset_alloc(lchip, SYS_NH_ENTRY_TYPE_FWD, 1,
                                                    &(p_group->dsfwd_offset[index])));

    /* write dsfwd */
    dsfwd_param.is_mcast = FALSE;
    dsfwd_param.dest_chipid = SYS_MAP_CTC_GPORT_TO_GCHIP(p_gg_l3if_master[lchip]->l3if_prop[l3if_id].gport);
    dsfwd_param.dest_id = SYS_MAP_CTC_GPORT_TO_DRV_LPORT(p_gg_l3if_master[lchip]->l3if_prop[l3if_id].gport);
    dsfwd_param.dsfwd_offset = p_group->dsfwd_offset[index];

    ret = sys_goldengate_nh_write_entry_dsfwd(lchip, &dsfwd_param);
    if (ret < 0)
    {
        goto error0;
    }

    /* add to db */
    p_group->intf_array[index] = l3if_id;
    p_group->intf_count ++;

    /* update ecmp group table */
    ret = sys_goldengate_nh_update_ecmp_interface_group(lchip, p_group);
    if (ret < 0)
    {
        goto error1;
    }
    SYS_L3IF_UNLOCK(lchip);

    return ret;
error1:
    p_group->intf_array[index] = 0;
    p_group->intf_count --;

error0:
    sys_goldengate_nh_offset_free(lchip, SYS_NH_ENTRY_TYPE_FWD, 1, p_group->dsfwd_offset[index]);
    p_group->dsfwd_offset[index] = 0;
    SYS_L3IF_UNLOCK(lchip);

    return ret;
}

int32
sys_goldengate_l3if_remove_ecmp_if_member(uint8 lchip, uint16 ecmp_if_id, uint16 l3if_id)
{
    sys_l3if_ecmp_if_t* p_group = NULL;
    sys_nh_param_dsfwd_t dsfwd_param;
    uint8 index;
    uint32 dsfwd_offset = 0;
    int32 ret = CTC_E_NONE;

    SYS_L3IF_INIT_CHECK(lchip);
    SYS_L3IFID_VAILD_CHECK(l3if_id);
    CTC_VALUE_RANGE_CHECK(ecmp_if_id, 1, MAX_L3IF_ECMP_GROUP_NUM - 1);

    SYS_L3IF_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_L3IF_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Remove ecmp interface %d member %d\n", ecmp_if_id, l3if_id);

    sal_memset(&dsfwd_param, 0, sizeof(dsfwd_param));

    SYS_L3IF_LOCK(lchip);
    if (p_gg_l3if_master[lchip]->l3if_prop[l3if_id].l3if_type != CTC_L3IF_TYPE_PHY_IF)
    {
        SYS_L3IF_UNLOCK(lchip);
        return CTC_E_L3IF_INVALID_IF_TYPE;
    }

    p_group = ctc_vector_get(p_gg_l3if_master[lchip]->ecmp_group_vec, ecmp_if_id);
    if (NULL == p_group)
    {
        SYS_L3IF_UNLOCK(lchip);
        return CTC_E_ENTRY_NOT_EXIST;
    }

    /* find the index of the member removed */
    for (index = 0; index < p_group->intf_count; index ++)
    {
        if (p_group->intf_array[index] == l3if_id)
        {
            break;
        }
    }
    if (index == p_group->intf_count)
    {
        SYS_L3IF_UNLOCK(lchip);
        return CTC_E_MEMBER_NOT_EXIST;
    }

    dsfwd_offset = p_group->dsfwd_offset[index];

    /* remove db */
    p_group->intf_array[index] = p_group->intf_array[p_group->intf_count-1];
    p_group->dsfwd_offset[index] = p_group->dsfwd_offset[p_group->intf_count-1];
    p_group->intf_count --;

    /* free dsfwdptr*/
    sys_goldengate_nh_offset_free(lchip, SYS_NH_ENTRY_TYPE_FWD, 1, dsfwd_offset);

    /* update ecmp group table */
    sys_goldengate_nh_update_ecmp_interface_group(lchip, p_group);
    SYS_L3IF_UNLOCK(lchip);

    return ret;
}

int32
sys_goldengate_l3if_get_ecmp_if_info(uint8 lchip, uint16 ecmp_if_id, sys_l3if_ecmp_if_t** p_ecmp_if)
{
    sys_l3if_ecmp_if_t* p_group = NULL;

    SYS_L3IF_INIT_CHECK(lchip);
    CTC_VALUE_RANGE_CHECK(ecmp_if_id, 1, MAX_L3IF_ECMP_GROUP_NUM - 1);

    SYS_L3IF_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    SYS_L3IF_LOCK(lchip);
    p_group = ctc_vector_get(p_gg_l3if_master[lchip]->ecmp_group_vec, ecmp_if_id);
    if (NULL == p_group)
    {
        SYS_L3IF_UNLOCK(lchip);
        return CTC_E_ENTRY_NOT_EXIST;
    }

    *p_ecmp_if = p_group;
    SYS_L3IF_UNLOCK(lchip);

    return CTC_E_NONE;
}

int32
sys_goldengate_l3if_show_ecmp_if_info(uint8 lchip, uint16 ecmp_if_id)
{
    sys_l3if_ecmp_if_t* p_ecmp_if = NULL;
    uint8 index = 0;

    SYS_L3IF_INIT_CHECK(lchip);
    CTC_ERROR_RETURN(sys_goldengate_l3if_get_ecmp_if_info(lchip, ecmp_if_id, &p_ecmp_if));

    SYS_L3IF_DUMP("------------------------L3if Ecmp Info--------------------\r\n");

    SYS_L3IF_DUMP("%-26s:%10u \n", "Ecmp If Id", ecmp_if_id);
    SYS_L3IF_DUMP("%-26s:%10u \n", "Member Count", p_ecmp_if->intf_count);
    if(p_ecmp_if->failover_en)
    {
        SYS_L3IF_DUMP("%-26s:%10s \n", "Ecmp Group Type", "Failover");
    }
    else
    {
        SYS_L3IF_DUMP("%-26s:%10s \n", "Ecmp Group Type", (p_ecmp_if->ecmp_group_type==CTC_NH_ECMP_TYPE_DLB)?"Dynamic":"Static");
    }
    if (0 != p_ecmp_if->stats_id)
    {
        SYS_L3IF_DUMP("%-26s:%10u \n", "Stats Id", p_ecmp_if->stats_id);
    }
    SYS_L3IF_DUMP("%-26s:%10u \n", "HW Group Id", p_ecmp_if->hw_group_id);
    SYS_L3IF_DUMP("%-26s:%10u \n", "Ecmp Member Base", p_ecmp_if->ecmp_member_base);

    SYS_L3IF_DUMP("---------------------------------------------------------\r\n");

    SYS_L3IF_DUMP("--------------------L3if Ecmp Member Info----------------\r\n");

    for (index = 0; index < p_ecmp_if->intf_count; index++)
    {
        SYS_L3IF_DUMP("%-26s:%10u \n", "L3if Id", p_ecmp_if->intf_array[index]);
    }

    SYS_L3IF_DUMP("---------------------------------------------------------\r\n");

    return CTC_E_NONE;
}


uint32
sys_goldengate_l3if_binding_inner_route_mac(uint8 lchip, mac_addr_t* p_mac_addr, uint8* route_mac_index)
{
    uint8 i = 0;
    uint8 valid_index = 0;
    uint8 flag_match = 0;
    uint32  cmd = 0;
    uint32  mac[2] = {0};
    ds_t    router_mac = {0};
    sys_l3if_inner_route_mac_t* p_inner_route_mac = NULL;

    SYS_L3IF_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    SYS_L3IF_LOCK(lchip);
    for (i = 1; i < SYS_L3IF_INNER_ROUTE_MAC_NUM; i++)
    {
        if (p_gg_l3if_master[lchip]->inner_route_mac[i])
        {
            p_inner_route_mac = p_gg_l3if_master[lchip]->inner_route_mac[i];
            if (!sal_memcmp(p_inner_route_mac->route_mac, p_mac_addr, sizeof(mac_addr_t)))
            {
                (p_inner_route_mac->ref_cnt)++;
                flag_match = 1;
                valid_index = i;
                break;
            }
        }
        else
        {
            valid_index = i;
        }
    }

    if (!flag_match)
    {
        if (0 == valid_index)
        {
            SYS_L3IF_UNLOCK(lchip);
            return CTC_E_NO_RESOURCE;
        }

        p_gg_l3if_master[lchip]->inner_route_mac[valid_index] = \
            (sys_l3if_inner_route_mac_t *)mem_malloc(MEM_L3IF_MODULE,  \
                                                sizeof(sys_l3if_inner_route_mac_t));

        if (NULL == p_gg_l3if_master[lchip]->inner_route_mac[valid_index])
        {
            SYS_L3IF_UNLOCK(lchip);
            return CTC_E_NO_MEMORY;
        }

        p_inner_route_mac = p_gg_l3if_master[lchip]->inner_route_mac[valid_index];
        sal_memset(p_inner_route_mac, 0, sizeof(sys_l3if_inner_route_mac_t));
        (p_inner_route_mac->ref_cnt)++;
        sal_memcpy(&(p_inner_route_mac->route_mac), p_mac_addr, sizeof(mac_addr_t));

        SYS_GOLDENGATE_SET_HW_MAC(mac, *p_mac_addr);
        SetDsRouterMacInner(A, routerMac_f, router_mac, mac);
        cmd = DRV_IOW(DsRouterMacInner_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN_L3IF_UNLOCK(DRV_IOCTL(lchip, valid_index, cmd, router_mac));
    }
    SYS_L3IF_UNLOCK(lchip);

    SYS_L3IF_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "index = %d, ref_cnt = %d\n", valid_index, p_inner_route_mac->ref_cnt);
    *route_mac_index = valid_index;

    return CTC_E_NONE;
}


int32
sys_goldengate_l3if_unbinding_inner_route_mac(uint8 lchip, uint8 route_mac_index)
{
    sys_l3if_inner_route_mac_t* p_inner_route_mac = NULL;

    SYS_L3IF_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    SYS_L3IF_LOCK(lchip);
    p_inner_route_mac = p_gg_l3if_master[lchip]->inner_route_mac[route_mac_index];
    if (p_inner_route_mac)
    {
        if (p_inner_route_mac->ref_cnt)
        {
            (p_inner_route_mac->ref_cnt)--;
        }
        SYS_L3IF_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "index = %d, ref_cnt = %d\n", route_mac_index, p_inner_route_mac->ref_cnt);
        if (0 == p_inner_route_mac->ref_cnt)
        {
            mem_free(p_gg_l3if_master[lchip]->inner_route_mac[route_mac_index]);
        }
    }
    SYS_L3IF_UNLOCK(lchip);

    return CTC_E_NONE;
}

int32 sys_goldengate_l3if_wb_restore(uint8 lchip)
{
    int32 ret = CTC_E_NONE;
    uint16 entry_cnt = 0;
    uint16 ecmp_mem_idx = 0;
    uint8 mac_index ;
    sys_goldengate_opf_t opf;
    ctc_wb_query_t    wb_query;
    sys_wb_l3if_prop_t   *p_wb_l3if_prop;
    sys_wb_l3if_router_mac_t    *p_wb_l3if_mac;
    sys_wb_l3if_ecmp_if_t   *p_wb_ecmp_if;
    sys_l3if_ecmp_if_t   *p_ecmp_if;
    uint32 cmd = 0;
    uint32  mac[2] = {0};
    uint32 egs_idx = 0;

    /*restore l3if property*/
    wb_query.buffer = mem_malloc(MEM_L3IF_MODULE,  CTC_WB_DATA_BUFFER_LENGTH);
    if (NULL == wb_query.buffer)
    {
        return CTC_E_NO_MEMORY;
    }
    sal_memset(wb_query.buffer, 0, CTC_WB_DATA_BUFFER_LENGTH);

    CTC_WB_INIT_QUERY_T((&wb_query),sys_wb_l3if_prop_t, CTC_FEATURE_L3IF, SYS_WB_APPID_L3IF_SUBID_PROP);

    CTC_WB_QUERY_ENTRY_BEGIN((&wb_query));
        p_wb_l3if_prop = (sys_wb_l3if_prop_t*)wb_query.buffer + entry_cnt++;
        p_gg_l3if_master[lchip]->l3if_prop[p_wb_l3if_prop->l3if_id].gport =  p_wb_l3if_prop->gport;
        p_gg_l3if_master[lchip]->l3if_prop[p_wb_l3if_prop->l3if_id].vlan_id =   p_wb_l3if_prop->vlan_id ;
        p_gg_l3if_master[lchip]->l3if_prop[p_wb_l3if_prop->l3if_id].vaild =   p_wb_l3if_prop->vaild ;
        p_gg_l3if_master[lchip]->l3if_prop[p_wb_l3if_prop->l3if_id].l3if_type =    p_wb_l3if_prop->l3if_type;
        p_gg_l3if_master[lchip]->l3if_prop[p_wb_l3if_prop->l3if_id].rtmac_index =     p_wb_l3if_prop->rtmac_index ;
        p_gg_l3if_master[lchip]->l3if_prop[p_wb_l3if_prop->l3if_id].rtmac_bmp =     p_wb_l3if_prop->rtmac_bmp ;
        p_gg_l3if_master[lchip]->l3if_prop[p_wb_l3if_prop->l3if_id].vlan_ptr =    p_wb_l3if_prop->vlan_ptr ;
        p_gg_l3if_master[lchip]->l3if_prop[p_wb_l3if_prop->l3if_id].l3if_id = p_wb_l3if_prop->l3if_id;

        sal_memset(&mac, 0, sizeof(mac));
        cmd = DRV_IOR(DsDestInterface_t, DsDestInterface_routerMacProfile_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_wb_l3if_prop->l3if_id, cmd, &egs_idx));
        cmd = DRV_IOR(DsEgressRouterMac_t, DsEgressRouterMac_routerMac_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, egs_idx, cmd, &mac));
        SYS_GOLDENGATE_SET_USER_MAC(p_gg_l3if_master[lchip]->l3if_prop[p_wb_l3if_prop->l3if_id].egress_router_mac, mac);

    CTC_WB_QUERY_ENTRY_END((&wb_query));

     /*return 0;*/

    /*restore l3if l3if_router_mac and inner_route_mac*/
    CTC_WB_INIT_QUERY_T((&wb_query),sys_wb_l3if_router_mac_t, CTC_FEATURE_L3IF, SYS_WB_APPID_L3IF_SUBID_ROUTER_MAC);

    CTC_WB_QUERY_ENTRY_BEGIN((&wb_query));
        p_wb_l3if_mac = (sys_wb_l3if_router_mac_t*)wb_query.buffer + entry_cnt++;
        mac_index = p_wb_l3if_mac->profile_id;

        if (!p_wb_l3if_mac->is_inner)
        {
            p_gg_l3if_master[lchip]->router_mac[mac_index].ref = p_wb_l3if_mac->ref;
            sal_memcpy(p_gg_l3if_master[lchip]->router_mac[mac_index].mac, p_wb_l3if_mac->mac, sizeof(mac_addr_t));
            if (mac_index)
            {
                /* sync include system router mac, but opf allocation do not include system router mac */
                sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
                opf.pool_type = OPF_ROUTER_MAC;
                CTC_ERROR_RETURN(sys_goldengate_opf_alloc_offset_from_position(lchip, &opf, 1, mac_index));
            }
        }
        else
        {
            p_gg_l3if_master[lchip]->inner_route_mac[mac_index] =  mem_malloc(MEM_L3IF_MODULE, sizeof(sys_l3if_ecmp_if_t));
            p_gg_l3if_master[lchip]->inner_route_mac[mac_index]->ref_cnt = p_wb_l3if_mac->ref;
            sal_memcpy(p_gg_l3if_master[lchip]->inner_route_mac[mac_index]->route_mac,  p_wb_l3if_mac->mac, sizeof(mac_addr_t));
        }
    CTC_WB_QUERY_ENTRY_END((&wb_query));


    /*restore  ecmp if*/
    CTC_WB_INIT_QUERY_T((&wb_query),sys_wb_l3if_ecmp_if_t, CTC_FEATURE_L3IF, SYS_WB_APPID_L3IF_SUBID_ECMP_IF);

    CTC_WB_QUERY_ENTRY_BEGIN((&wb_query));
        p_wb_ecmp_if = (sys_wb_l3if_ecmp_if_t*)wb_query.buffer + entry_cnt++;
        p_ecmp_if = mem_malloc(MEM_L3IF_MODULE, sizeof(sys_l3if_ecmp_if_t));

        p_ecmp_if->hw_group_id = p_wb_ecmp_if->hw_group_id;
        p_ecmp_if->intf_count = p_wb_ecmp_if->intf_count;
        p_ecmp_if->failover_en = p_wb_ecmp_if->failover_en;
        sal_memcpy(p_ecmp_if->intf_array, p_wb_ecmp_if->intf_array, SYS_GG_MAX_ECPN * sizeof(uint16));
        sal_memcpy(p_ecmp_if->dsfwd_offset, p_wb_ecmp_if->dsfwd_offset, SYS_GG_MAX_ECPN * sizeof(uint32));
        p_ecmp_if->ecmp_group_type = p_wb_ecmp_if->ecmp_group_type;     /* refer to ctc_nh_ecmp_type_t */
        p_ecmp_if->ecmp_member_base = p_wb_ecmp_if->ecmp_member_base;
        p_ecmp_if->stats_id = p_wb_ecmp_if->stats_id;

        opf.pool_type = OPF_ECMP_GROUP_ID;
        opf.pool_index = 0;
        opf.reverse    = 1;
        CTC_ERROR_GOTO(sys_goldengate_opf_alloc_offset_from_position(lchip, &opf, 1, p_ecmp_if->hw_group_id), ret, done);

        opf.pool_type = OPF_ECMP_MEMBER_ID;
        opf.pool_index = 0;
        CTC_ERROR_GOTO(sys_goldengate_opf_alloc_offset_from_position(lchip, &opf, SYS_GG_MAX_DEFAULT_ECMP_NUM, p_ecmp_if->ecmp_member_base), ret, done);

        for (ecmp_mem_idx = 0; ecmp_mem_idx < p_ecmp_if->intf_count; ecmp_mem_idx++)
        {
            CTC_ERROR_GOTO(sys_goldengate_nh_offset_alloc_from_position(lchip, SYS_NH_ENTRY_TYPE_FWD, 1, p_ecmp_if->dsfwd_offset[ecmp_mem_idx]), ret, done);
        }
        ctc_vector_add(p_gg_l3if_master[lchip]->ecmp_group_vec,  p_wb_ecmp_if->ecmp_if_id, p_ecmp_if);
    CTC_WB_QUERY_ENTRY_END((&wb_query));

done:
    if (wb_query.buffer)
    {
        mem_free(wb_query.buffer);
    }

   return ret;
}

int32 _sys_goldengate_l3if_wb_traverse_sync_ecmp_if(void *data, uint32 vec_index, void *user_data)
{
    int32 ret;
    sys_l3if_ecmp_if_t *p_ecmp_if = (sys_l3if_ecmp_if_t*)data;
    ctc_wb_data_t *wb_data = (ctc_wb_data_t*)user_data;
    sys_wb_l3if_ecmp_if_t    *p_wb_l3if_ecmp_if;
    uint16 max_buffer_cnt = wb_data->buffer_len / (wb_data->key_len + wb_data->data_len);

    p_wb_l3if_ecmp_if = (sys_wb_l3if_ecmp_if_t *)wb_data->buffer + wb_data->valid_cnt;

    p_wb_l3if_ecmp_if->ecmp_if_id = vec_index;
    p_wb_l3if_ecmp_if->hw_group_id = p_ecmp_if->hw_group_id;
    p_wb_l3if_ecmp_if->intf_count = p_ecmp_if->intf_count;
    p_wb_l3if_ecmp_if->failover_en = p_ecmp_if->failover_en;
    sal_memcpy(p_wb_l3if_ecmp_if->intf_array, p_ecmp_if->intf_array, SYS_GG_MAX_ECPN * sizeof(uint16));
    sal_memcpy(p_wb_l3if_ecmp_if->dsfwd_offset, p_ecmp_if->dsfwd_offset, SYS_GG_MAX_ECPN * sizeof(uint32));
    p_wb_l3if_ecmp_if->ecmp_group_type = p_ecmp_if->ecmp_group_type;     /* refer to ctc_nh_ecmp_type_t */
    p_wb_l3if_ecmp_if->ecmp_member_base = p_ecmp_if->ecmp_member_base;
    p_wb_l3if_ecmp_if->stats_id = p_ecmp_if->stats_id;


    if (++wb_data->valid_cnt ==  max_buffer_cnt)
    {
        ret = ctc_wb_add_entry(wb_data);
        if ( ret != CTC_E_NONE )
        {
            return ret;
        }
        wb_data->valid_cnt = 0;
    }

    return CTC_E_NONE;
}

int32 sys_goldengate_l3if_wb_sync(uint8 lchip)
{
    uint32 loop = 0;
    int32 ret = CTC_E_NONE;
    uint32 max_entry_cnt = 0;

    ctc_wb_data_t wb_data;
    sys_wb_l3if_prop_t   wb_l3if_prop;
    sys_wb_l3if_router_mac_t l3if_mac;

    sal_memset(&l3if_mac, 0, sizeof(sys_wb_l3if_router_mac_t));
    sal_memset(&wb_l3if_prop, 0, sizeof(sys_wb_l3if_prop_t));

    /*syncup  l3if property*/
    wb_data.buffer = mem_malloc(MEM_L3IF_MODULE, CTC_WB_DATA_BUFFER_LENGTH);
    if (NULL == wb_data.buffer)
    {
        return CTC_E_NO_MEMORY;
    }
    sal_memset(wb_data.buffer, 0, CTC_WB_DATA_BUFFER_LENGTH);

    CTC_WB_INIT_DATA_T((&wb_data),sys_wb_l3if_prop_t, CTC_FEATURE_L3IF, SYS_WB_APPID_L3IF_SUBID_PROP);
    max_entry_cnt = CTC_WB_DATA_BUFFER_LENGTH / (wb_data.key_len + wb_data.data_len);

    do
    {
        if (!p_gg_l3if_master[lchip]->l3if_prop[loop].vaild)
        {
            continue;
        }
        wb_l3if_prop.l3if_id = p_gg_l3if_master[lchip]->l3if_prop[loop].l3if_id;
        wb_l3if_prop.gport = p_gg_l3if_master[lchip]->l3if_prop[loop].gport;
        wb_l3if_prop.vlan_id = p_gg_l3if_master[lchip]->l3if_prop[loop].vlan_id;
        wb_l3if_prop.vaild = p_gg_l3if_master[lchip]->l3if_prop[loop].vaild;
        wb_l3if_prop.l3if_type = p_gg_l3if_master[lchip]->l3if_prop[loop].l3if_type;
        wb_l3if_prop.rtmac_index = p_gg_l3if_master[lchip]->l3if_prop[loop].rtmac_index;
        wb_l3if_prop.rtmac_bmp = p_gg_l3if_master[lchip]->l3if_prop[loop].rtmac_bmp;
        wb_l3if_prop.vlan_ptr = p_gg_l3if_master[lchip]->l3if_prop[loop].vlan_ptr;


        sal_memcpy( (uint8*)wb_data.buffer + wb_data.valid_cnt * sizeof(sys_wb_l3if_prop_t), (uint8*)&wb_l3if_prop, sizeof(sys_wb_l3if_prop_t));

        if (++wb_data.valid_cnt == max_entry_cnt)
        {
            CTC_ERROR_GOTO(ctc_wb_add_entry(&wb_data), ret, done);
            wb_data.valid_cnt = 0;
        }
    }while ((++loop) < (SYS_GG_MAX_CTC_L3IF_ID + 1));
    if (wb_data.valid_cnt > 0)
    {
        CTC_ERROR_GOTO(ctc_wb_add_entry(&wb_data), ret, done);
        wb_data.valid_cnt = 0;
    }


    /*syncup  l3if l3if_router_mac*/
    CTC_WB_INIT_DATA_T((&wb_data),sys_wb_l3if_router_mac_t, CTC_FEATURE_L3IF, SYS_WB_APPID_L3IF_SUBID_ROUTER_MAC);
    max_entry_cnt = CTC_WB_DATA_BUFFER_LENGTH / (wb_data.key_len + wb_data.data_len);
    loop = 0;
    do
    {
        if (0 == p_gg_l3if_master[lchip]->router_mac[loop].ref)
        {
            continue;
        }
        l3if_mac.profile_id = loop;
        l3if_mac.ref =  p_gg_l3if_master[lchip]->router_mac[loop].ref;
        l3if_mac.is_inner = 0;
        sal_memcpy(l3if_mac.mac, p_gg_l3if_master[lchip]->router_mac[loop].mac, sizeof(mac_addr_t));

        sal_memcpy( (uint8*)wb_data.buffer+ wb_data.valid_cnt * sizeof(sys_wb_l3if_router_mac_t),
        	         (uint8*)&l3if_mac,sizeof(sys_wb_l3if_router_mac_t));

        if (++wb_data.valid_cnt == max_entry_cnt)
        {
            CTC_ERROR_GOTO(ctc_wb_add_entry(&wb_data), ret, done);
            wb_data.valid_cnt = 0;
        }
    }while ((++loop) < SYS_ROUTER_MAC_NUM);
    if (wb_data.valid_cnt > 0)
    {
        CTC_ERROR_GOTO(ctc_wb_add_entry(&wb_data), ret, done);
        wb_data.valid_cnt = 0;
    }

    /*syncup  l3if_inner_router_mac*/
    sal_memset(&l3if_mac, 0, sizeof(sys_wb_l3if_router_mac_t));
    CTC_WB_INIT_DATA_T((&wb_data),sys_wb_l3if_router_mac_t, CTC_FEATURE_L3IF, SYS_WB_APPID_L3IF_SUBID_ROUTER_MAC);
    max_entry_cnt = CTC_WB_DATA_BUFFER_LENGTH / (wb_data.key_len + wb_data.data_len);

    loop = 0;
    do
    {
        if (!p_gg_l3if_master[lchip]->inner_route_mac[loop] || !p_gg_l3if_master[lchip]->inner_route_mac[loop]->ref_cnt)
        {
            continue;
        }
        l3if_mac.profile_id = loop;
        l3if_mac.is_inner = 1;
        l3if_mac.ref =  p_gg_l3if_master[lchip]->inner_route_mac[loop]->ref_cnt;
        sal_memcpy(l3if_mac.mac, p_gg_l3if_master[lchip]->inner_route_mac[loop]->route_mac, sizeof(mac_addr_t));

        sal_memcpy( (uint8*)wb_data.buffer + wb_data.valid_cnt * sizeof(sys_wb_l3if_router_mac_t),
                   (uint8*)&l3if_mac, sizeof(sys_wb_l3if_router_mac_t));

        if (++wb_data.valid_cnt == max_entry_cnt)
        {
            CTC_ERROR_GOTO(ctc_wb_add_entry(&wb_data), ret, done);
            wb_data.valid_cnt = 0;
        }
    } while ((++loop) < SYS_L3IF_INNER_ROUTE_MAC_NUM);
    if (wb_data.valid_cnt > 0)
    {
        CTC_ERROR_GOTO(ctc_wb_add_entry(&wb_data), ret, done);
        wb_data.valid_cnt = 0;
    }

    /*sync ecmp_if*/
    CTC_WB_INIT_DATA_T((&wb_data),sys_wb_l3if_ecmp_if_t, CTC_FEATURE_L3IF, SYS_WB_APPID_L3IF_SUBID_ECMP_IF);
 	ctc_vector_traverse2(p_gg_l3if_master[lchip]->ecmp_group_vec,0,_sys_goldengate_l3if_wb_traverse_sync_ecmp_if,&wb_data);
    if (wb_data.valid_cnt > 0)
    {
        CTC_ERROR_GOTO(ctc_wb_add_entry(&wb_data), ret, done);
        wb_data.valid_cnt = 0;
    }

done:
    if ( wb_data.buffer )
    {
        mem_free(wb_data.buffer);
    }

    return ret;
}


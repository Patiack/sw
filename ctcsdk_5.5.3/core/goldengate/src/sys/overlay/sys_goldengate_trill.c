/**
 @file sys_goldengate_trill.c

 @date 2015-10-25

 @version v3.0
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


#include "sys_goldengate_trill.h"
#include "sys_goldengate_common.h"
#include "sys_goldengate_chip.h"
#include "sys_goldengate_nexthop_api.h"
#include "sys_goldengate_nexthop.h"
#include "sys_goldengate_ftm.h"
#include "sys_goldengate_scl.h"
#include "sys_goldengate_stats.h"
#include "sys_goldengate_dma.h"
#include "sys_goldengate_l3if.h"

#include "goldengate/include/drv_enum.h"
#include "goldengate/include/drv_io.h"
#include "goldengate/include/drv_common.h"

/****************************************************************************
 *
* Defines and Macros
*
*****************************************************************************/
#define SYS_TRILL_DBG_OUT(level, FMT, ...)                          \
    do                                                     \
    {                                                      \
        CTC_DEBUG_OUT(trill, trill, TRILL_SYS, level, FMT, ##__VA_ARGS__);  \
    } while (0)

#define SYS_TRILL_DBG_FUNC()                          \
    { \
    SYS_TRILL_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n",__FUNCTION__);\
    }

#define SYS_TRILL_DBG_DUMP(FMT, ...)               \
    {                                                 \
    SYS_TRILL_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, FMT, ##__VA_ARGS__);\
    }

#define SYS_TRILL_DBG_INFO(FMT, ...) \
    { \
    SYS_TRILL_DBG_OUT(CTC_DEBUG_LEVEL_INFO, FMT, ##__VA_ARGS__);\
    }

#define SYS_TRILL_DBG_PARAM(FMT, ...) \
    { \
    SYS_TRILL_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, FMT, ##__VA_ARGS__);\
    }

#define SYS_TRILL_DBG_ERROR(FMT, ...) \
    { \
    SYS_TRILL_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, FMT, ##__VA_ARGS__);\
    }

struct sys_trill_route_s
{
    /*hw para*/
    uint32 ad_index;
    uint32 key_index;
    uint32 key_tbl_id;
    DsTrillDa_m ds_trill_da;
    ds_t ds_trill_route_key;

    uint8 is_host1;
    uint8 is_remove;
};
typedef struct sys_trill_route_s sys_trill_route_t;


struct sys_trill_master_s
{
    uint8 rsv;
};
typedef struct sys_trill_master_s sys_trill_master_t;

sys_trill_master_t* p_gg_sys_trill_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};


#define SYS_TRILL_INIT_CHECK(lchip) \
    do { \
        SYS_LCHIP_CHECK_ACTIVE(lchip); \
        if (NULL == p_gg_sys_trill_master[lchip]) \
        { \
            return CTC_E_NOT_INIT; \
        } \
    } while (0)
/****************************************************************************
*
* Function
*
*****************************************************************************/
STATIC int32
_sys_goldengate_trill_show_route_entry(uint8 lchip, uint32 tbl_id)
{
    int32 ret = CTC_E_NONE;
    uint32 step = 512;
    uint32* p_buffer = NULL;
    uint8 tbl_size = TABLE_ENTRY_SIZE(tbl_id);
    drv_work_platform_type_t platform_type;
    sys_dma_tbl_rw_t dma_rw;
    uint32 i = 0, j = 0;
    void* ds_key = NULL;

    uint32 valid = 0;
    uint32 hashType = 0;
    uint32 egressNickname = 0;
    uint32 _type = 0;
    uint32 vlanId = 0;
    uint32 dsAdIndex = 0;

    uint32 tbl_index = 0;

    sal_memset(&dma_rw, 0, sizeof(sys_dma_tbl_rw_t));
    p_buffer = (uint32*)mem_malloc(MEM_TRILL_MODULE, step*tbl_size);
    if (NULL == p_buffer)
    {
        return CTC_E_NO_MEMORY;
    }

    dma_rw.rflag = 1;
    dma_rw.entry_len = tbl_size;
    dma_rw.entry_num =  step;
    dma_rw.buffer = p_buffer;
    drv_goldengate_get_platform_type(&platform_type);
    for (i = 0; i < (TABLE_MAX_INDEX(tbl_id)/step); i++)
    {
        sal_memset(dma_rw.buffer, 0, step*tbl_size);
        if (platform_type == HW_PLATFORM)
        {
            CTC_ERROR_GOTO(drv_goldengate_table_get_hw_addr(tbl_id, i*step, &dma_rw.tbl_addr, FALSE), ret, END);
        }
        else
        {
            dma_rw.tbl_addr = (tbl_id << 18) + (i*step);
        }
        CTC_ERROR_GOTO(sys_goldengate_dma_rw_table(lchip, &dma_rw), ret, END);

        for (j = 0; j < dma_rw.entry_num; j++)
        {
            tbl_index = j + i*step + 32;
            ds_key = (DsFibHost0TrillHashKey_m *)dma_rw.buffer + j;
            if (DsFibHost0TrillHashKey_t == tbl_id)
            {
                 GetDsFibHost0TrillHashKey(A, valid_f, ds_key, &valid);
                 GetDsFibHost0TrillHashKey(A, hashType_f, ds_key, &hashType);
                 if (valid && (FIBHOST0PRIMARYHASHTYPE_TRILL == hashType))
                 {
                     GetDsFibHost0TrillHashKey(A, egressNickname_f, ds_key, &egressNickname);
                     GetDsFibHost0TrillHashKey(A, dsAdIndex_f, ds_key, &dsAdIndex);
                     vlanId = 0;
                 }
                 else
                 {
                     continue;
                 }
            }
            else if(DsFibHost1TrillMcastVlanHashKey_t == tbl_id)
            {
                GetDsFibHost1TrillMcastVlanHashKey(A, hashType_f, ds_key, &hashType);
                GetDsFibHost1TrillMcastVlanHashKey(A, _type_f, ds_key, &_type);
                GetDsFibHost1TrillMcastVlanHashKey(A, valid_f, ds_key, &valid);
                if (valid && (FIBHOST1PRIMARYHASHTYPE_OTHER == hashType) && (1 == _type))
                {
                    GetDsFibHost1TrillMcastVlanHashKey(A, egressNickname_f, ds_key, &egressNickname);
                    GetDsFibHost1TrillMcastVlanHashKey(A, vlanId_f, ds_key, &vlanId);
                    GetDsFibHost1TrillMcastVlanHashKey(A, dsAdIndex_f, ds_key, &dsAdIndex);
                }
                else
                {
                    continue;
                }
            }
            if (DsFibHost1TrillMcastVlanHashKey_t == tbl_id)
            {
                SYS_TRILL_DBG_DUMP("0x%-13x%-15d%-10d%-10d\n", tbl_index, egressNickname, vlanId, dsAdIndex);
            }
            else
            {
                SYS_TRILL_DBG_DUMP("0x%-13x%-15d%-10s%-10d\n", tbl_index, egressNickname, "-", dsAdIndex);
            }
        }
    }

END:
    mem_free(p_buffer);
    return ret;
}

int32
sys_goldengate_trill_dump_route_entry(uint8 lchip)
{
    SYS_TRILL_INIT_CHECK(lchip);
    SYS_TRILL_DBG_DUMP("------ Ucast: DsFibHost0TrillHashKey -----------\n");
    SYS_TRILL_DBG_DUMP("%-15s%-15s%-10s%-10s\n", "ENTRY_IDX", "EGR_NICKNAME", "VLAN_ID", "AD_INDEX");
    SYS_TRILL_DBG_DUMP("------------------------------------------------\n");
    CTC_ERROR_RETURN(_sys_goldengate_trill_show_route_entry(lchip, DsFibHost0TrillHashKey_t));

    SYS_TRILL_DBG_DUMP("\n---- Mcast: DsFibHost1TrillMcastVlanHashKey ---\n");
    SYS_TRILL_DBG_DUMP("%-15s%-15s%-10s%-10s\n", "ENTRY_IDX", "EGR_NICKNAME", "VLAN_ID", "AD_INDEX");
    SYS_TRILL_DBG_DUMP("------------------------------------------------\n");
    CTC_ERROR_RETURN(_sys_goldengate_trill_show_route_entry(lchip, DsFibHost1TrillMcastVlanHashKey_t));
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_trill_build_route_key(uint8 lchip, ctc_trill_route_t* p_trill_route, sys_trill_route_t* p_sys_trill_route, uint8 is_add)
{
    drv_fib_acc_in_t  fib_acc_in;
    drv_fib_acc_out_t fib_acc_out;
    drv_cpu_acc_in_t  cpu_acc_in;
    drv_cpu_acc_out_t cpu_acc_out;
    ds_t* ds_trill_route_key = NULL;
    uint8 is_host1 = 0;

    SYS_TRILL_DBG_FUNC();
    sal_memset(&fib_acc_in, 0, sizeof(drv_fib_acc_in_t));
    sal_memset(&fib_acc_out, 0, sizeof(drv_fib_acc_out_t));
    sal_memset(&cpu_acc_in, 0, sizeof(drv_cpu_acc_in_t));
    sal_memset(&cpu_acc_out, 0, sizeof(drv_cpu_acc_out_t));

    ds_trill_route_key = &(p_sys_trill_route->ds_trill_route_key);

    if (CTC_FLAG_ISSET(p_trill_route->flag, CTC_TRILL_ROUTE_FLAG_MCAST)&&p_trill_route->vlan_id)
    {
        is_host1 = 1;

        SetDsFibHost1TrillMcastVlanHashKey(V, hashType_f, ds_trill_route_key, FIBHOST1PRIMARYHASHTYPE_OTHER);
        SetDsFibHost1TrillMcastVlanHashKey(V, _type_f, ds_trill_route_key, 1);
        SetDsFibHost1TrillMcastVlanHashKey(V, valid_f, ds_trill_route_key, 1);
        SetDsFibHost1TrillMcastVlanHashKey(V, egressNickname_f, ds_trill_route_key, p_trill_route->egress_nickname);
        SetDsFibHost1TrillMcastVlanHashKey(V, vlanId_f, ds_trill_route_key, p_trill_route->vlan_id);
    }
    else
    {
        SetDsFibHost0TrillHashKey(V, hashType_f, ds_trill_route_key, FIBHOST0PRIMARYHASHTYPE_TRILL);
        SetDsFibHost0TrillHashKey(V, valid_f, ds_trill_route_key, 1);
        SetDsFibHost0TrillHashKey(V, egressNickname_f, ds_trill_route_key, p_trill_route->egress_nickname);
        if (CTC_FLAG_ISSET(p_trill_route->flag, CTC_TRILL_ROUTE_FLAG_MCAST))
        {
            SetDsFibHost0TrillHashKey(V, trillType_f, ds_trill_route_key, 1);
        }
    }

    if (is_host1)
    {
        cpu_acc_in.data = (void*)ds_trill_route_key;
        cpu_acc_in.tbl_id = DsFibHost1TrillMcastVlanHashKey_t;
        CTC_ERROR_RETURN(drv_goldengate_cpu_acc(lchip, DRV_CPU_LOOKUP_ACC_FIB_HOST1, &cpu_acc_in, &cpu_acc_out));

        if (is_add)
        {
            if (1 == cpu_acc_out.conflict)
            {
                return CTC_E_HASH_CONFLICT;
            }
            if (1 == cpu_acc_out.hit)
            {
                return CTC_E_ENTRY_EXIST;
            }
        }
        else
        {
            if (0 == cpu_acc_out.hit)
            {
                return CTC_E_ENTRY_NOT_EXIST;
            }
        }
        p_sys_trill_route->key_index = cpu_acc_out.key_index;

        cpu_acc_in.tbl_id = DsFibHost1TrillMcastVlanHashKey_t;
        cpu_acc_in.key_index = cpu_acc_out.key_index;
        sal_memset(&cpu_acc_out, 0, sizeof(drv_cpu_acc_out_t));
        CTC_ERROR_RETURN(drv_goldengate_cpu_acc(lchip, DRV_CPU_READ_ACC_FIB_HOST1_BY_IDX, &cpu_acc_in, &cpu_acc_out));
        p_sys_trill_route->ad_index = GetDsFibHost1TrillMcastVlanHashKey(V, dsAdIndex_f, &cpu_acc_out.data);
        SYS_TRILL_DBG_INFO("DsFibHost1TrillMcastVlanHashKey index :0x%x, ad_index = 0x%x\n",
                                                                                p_sys_trill_route->key_index, p_sys_trill_route->ad_index);
    }
    else
    {
        fib_acc_in.fib.data = (void*)ds_trill_route_key;
        fib_acc_in.fib.key_type = FIBHOST0PRIMARYHASHTYPE_TRILL;
        CTC_ERROR_RETURN(drv_goldengate_fib_acc(lchip, DRV_FIB_ACC_LKP_FIB0, &fib_acc_in, &fib_acc_out));
        if (is_add)
        {
            if (1 == fib_acc_out.fib.conflict)
            {
                return CTC_E_HASH_CONFLICT;
            }
            if (1 == fib_acc_out.fib.hit)
            {
                return CTC_E_ENTRY_EXIST;
            }
        }
        else
        {
            if (0 == fib_acc_out.fib.hit)
            {
                return CTC_E_ENTRY_NOT_EXIST;
            }
        }
        p_sys_trill_route->key_index = fib_acc_out.fib.key_index;
        p_sys_trill_route->ad_index = fib_acc_out.fib.ad_index;
        SYS_TRILL_DBG_INFO("DsFibHost0TrillHashKey index :0x%x\n, ad_index = 0x%x\n",
                                                                                 p_sys_trill_route->key_index, p_sys_trill_route->ad_index);
    }

    p_sys_trill_route->is_host1 = is_host1;
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_trill_build_route_ad(uint8 lchip, ctc_trill_route_t* p_trill_route, sys_trill_route_t* p_sys_trill_route)
{
    DsTrillDa_m* p_ds_trill_da = NULL;
    sys_nh_info_dsnh_t nh_info;
    uint32 fwd_ptr = 0;
    uint32 fwd_offset = 0;

    p_ds_trill_da = &(p_sys_trill_route->ds_trill_da);

    SYS_TRILL_DBG_FUNC();
    SetDsTrillDa(V, ttlLimit_f, p_ds_trill_da, 1);
    SetDsTrillDa(V, mcastCheckEn_f, p_ds_trill_da, 1);
    SetDsTrillDa(V, esadiCheckEn_f, p_ds_trill_da, 1);


    if (CTC_FLAG_ISSET(p_trill_route->flag, CTC_TRILL_ROUTE_FLAG_DISCARD))
    {
        CTC_ERROR_RETURN(sys_goldengate_nh_get_dsfwd_offset(lchip, 1, &fwd_offset));
        SetDsTrillDa(V, nextHopPtrValid_f, p_ds_trill_da, 0);
        SetDsTrillDa(V, u1_g1_dsFwdPtr_f, p_ds_trill_da, fwd_offset);
    }
    else if (p_trill_route->nh_id)
    {
        sal_memset(&nh_info, 0, sizeof(sys_nh_info_dsnh_t));
        CTC_ERROR_RETURN(sys_goldengate_nh_get_nhinfo(lchip, p_trill_route->nh_id, &nh_info));
        if (nh_info.merge_dsfwd == 1)
        {
            if (nh_info.ecmp_valid)
            {
                return CTC_E_UNEXPECT;
            }
            SetDsTrillDa(V, nextHopPtrValid_f, p_ds_trill_da, 1);
            if (nh_info.is_mcast)
            {
                SetDsTrillDa(V, u1_g2_adDestMap_f, p_ds_trill_da, SYS_ENCODE_MCAST_IPE_DESTMAP(0, nh_info.dest_id));
            }
            else
            {
                SetDsTrillDa(V, u1_g2_adDestMap_f, p_ds_trill_da, SYS_ENCODE_DESTMAP(nh_info.dest_chipid, nh_info.dest_id));
            }
            SetDsTrillDa(V, u1_g2_adNextHopPtr_f, p_ds_trill_da,nh_info.dsnh_offset);
            SetDsTrillDa(V, u1_g2_adNextHopExt_f, p_ds_trill_da,nh_info.nexthop_ext);
            SetDsTrillDa(V, u1_g2_adApsBridgeEn_f, p_ds_trill_da,nh_info.aps_en);
        }
        else
        {

            if (nh_info.ecmp_valid)
            {
                SetDsTrillDa(V, ecmpEn_f, p_ds_trill_da, 1);
                SetDsTrillDa(V, u1_g3_ecmpGroupId_f, p_ds_trill_da, nh_info.ecmp_group_id);
				SetDsTrillDa(V, nextHopPtrValid_f, p_ds_trill_da, 0);
            }
			else
			{
				sys_goldengate_nh_get_dsfwd_offset(lchip, p_trill_route->nh_id, &fwd_ptr);
                SetDsTrillDa(V, u1_g1_dsFwdPtr_f, p_ds_trill_da, fwd_ptr);
				SetDsTrillDa(V, nextHopPtrValid_f, p_ds_trill_da, 0);
			}
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_trill_write_route_hw(uint8 lchip, sys_trill_route_t* p_sys_trill_route, uint8 is_add)
{
    drv_fib_acc_in_t  fib_acc_in;
    drv_fib_acc_out_t fib_acc_out;
    drv_cpu_acc_in_t  cpu_acc_in;
    drv_cpu_acc_out_t cpu_acc_out;
    uint32 cmd = 0;

    SYS_TRILL_DBG_FUNC();
    sal_memset(&fib_acc_in, 0, sizeof(drv_fib_acc_in_t));
    sal_memset(&fib_acc_out, 0, sizeof(drv_fib_acc_out_t));
    sal_memset(&cpu_acc_in, 0, sizeof(drv_cpu_acc_in_t));
    sal_memset(&cpu_acc_out, 0, sizeof(drv_cpu_acc_out_t));

    /*write AD*/
    if(!is_add)
    {
        sal_memset(&(p_sys_trill_route->ds_trill_da), 0, sizeof(DsTrillDa_m));
    }
    cmd = DRV_IOW(DsTrillDa_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_sys_trill_route->ad_index, cmd, &(p_sys_trill_route->ds_trill_da)));

    /*write key*/
    if (p_sys_trill_route->is_host1)
    {
        SetDsFibHost1TrillMcastVlanHashKey(V, dsAdIndex_f, (&(p_sys_trill_route->ds_trill_route_key)), p_sys_trill_route->ad_index);
        cpu_acc_in.data = (void*)(&(p_sys_trill_route->ds_trill_route_key));
        cpu_acc_in.tbl_id = DsFibHost1TrillMcastVlanHashKey_t;
        cpu_acc_in.key_index = p_sys_trill_route->key_index;
        if (is_add)
        {
            CTC_ERROR_RETURN(drv_goldengate_cpu_acc(lchip, DRV_CPU_ADD_ACC_FIB_HOST1, &cpu_acc_in, &cpu_acc_out));
            if (cpu_acc_out.conflict)
            {
                return CTC_E_HASH_CONFLICT;
            }
        }
        else
        {
            CTC_ERROR_RETURN(drv_goldengate_cpu_acc(lchip, DRV_CPU_DEL_ACC_FIB_HOST1, &cpu_acc_in, &cpu_acc_out));
        }
    }
    else
    {
        SetDsFibHost0TrillHashKey(V, dsAdIndex_f, (&(p_sys_trill_route->ds_trill_route_key)), p_sys_trill_route->ad_index);
        if (!is_add)
        {
            SetDsFibHost0TrillHashKey(V, valid_f, (&(p_sys_trill_route->ds_trill_route_key)), 0);
        }
        fib_acc_in.fib.data = (void*)(&(p_sys_trill_route->ds_trill_route_key));
        fib_acc_in.fib.key_type = FIBHOST0PRIMARYHASHTYPE_TRILL;
        fib_acc_in.fib.overwrite_en = 1;
        CTC_ERROR_RETURN(drv_goldengate_fib_acc(lchip, DRV_FIB_ACC_WRITE_FIB0_BY_KEY, &fib_acc_in, &fib_acc_out));
        if (is_add && fib_acc_out.fib.conflict)
        {
            return CTC_E_HASH_CONFLICT;
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_trill_add_route_entry(uint8 lchip, ctc_trill_route_t* p_trill_route)
{
    sys_trill_route_t sys_trill_route;
    int32 ret = CTC_E_NONE;

    SYS_TRILL_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_trill_route);
    sal_memset(&sys_trill_route, 0, sizeof(sys_trill_route_t));

    /*buile key*/
    CTC_ERROR_RETURN(_sys_goldengate_trill_build_route_key(lchip, p_trill_route, &sys_trill_route, 1));

    /*build AD*/
    CTC_ERROR_RETURN(_sys_goldengate_trill_build_route_ad(lchip, p_trill_route, &sys_trill_route));

    /*alloc AD index*/
    CTC_ERROR_RETURN(sys_goldengate_ftm_alloc_table_offset(lchip, DsTrillDa_t, 0, 1, &sys_trill_route.ad_index));
    SYS_TRILL_DBG_INFO("DsTrillDa index :0x%x\n", sys_trill_route.ad_index);

    /*write hw*/
    CTC_ERROR_GOTO(_sys_goldengate_trill_write_route_hw(lchip, &sys_trill_route, 1), ret, exit);

    return CTC_E_NONE;

exit:
    /*free AD index*/
     sys_goldengate_ftm_free_table_offset(lchip, DsTrillDa_t, 0, 1, sys_trill_route.ad_index);

    return ret;
}

STATIC int32
_sys_goldengate_trill_remove_route_entry(uint8 lchip, ctc_trill_route_t* p_trill_route)
{
    sys_trill_route_t sys_trill_route;

    SYS_TRILL_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_trill_route);
    sal_memset(&sys_trill_route, 0, sizeof(sys_trill_route_t));

    /*get ad index by key*/
    CTC_ERROR_RETURN(_sys_goldengate_trill_build_route_key(lchip, p_trill_route, &sys_trill_route, 0));

    /*free AD index*/
    CTC_ERROR_RETURN(sys_goldengate_ftm_free_table_offset(lchip, DsTrillDa_t, 0, 1, sys_trill_route.ad_index));

    /*write hw*/
    CTC_ERROR_RETURN(_sys_goldengate_trill_write_route_hw(lchip, &sys_trill_route, 0));

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_trill_build_check_key(uint8 lchip, ctc_trill_route_t* p_trill_route, sys_scl_entry_t* scl_entry)
{
    SYS_TRILL_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_trill_route);
    CTC_PTR_VALID_CHECK(scl_entry);

    if (CTC_FLAG_ISSET(p_trill_route->flag, CTC_TRILL_ROUTE_FLAG_MCAST))
    {
        if (CTC_FLAG_ISSET(p_trill_route->flag, CTC_TRILL_ROUTE_FLAG_MACSA_CHECK))
        {
            scl_entry->key.type = SYS_SCL_KEY_HASH_TRILL_MC_ADJ;
        }
        else if (CTC_FLAG_ISSET(p_trill_route->flag, CTC_TRILL_ROUTE_FLAG_SRC_PORT_CHECK))
        {
            scl_entry->key.type = SYS_SCL_KEY_HASH_TRILL_MC_RPF;
        }
    }
    else
    {
        scl_entry->key.type = SYS_SCL_KEY_HASH_TRILL_UC_RPF;
    }

    scl_entry->key.tunnel_type = SYS_SCL_TUNNEL_TYPE_TRILL;
    scl_entry->key.u.hash_trill_key.egress_nickname = p_trill_route->egress_nickname;
    scl_entry->key.u.hash_trill_key.ingress_nickname = p_trill_route->ingress_nickname;
    scl_entry->key.u.hash_trill_key.gport = SYS_MAP_CTC_GPORT_TO_DRV_GPORT(p_trill_route->src_gport);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_trill_build_check_action(uint8 lchip, ctc_trill_route_t* p_trill_route, sys_scl_action_t* scl_action)
{
    SYS_TRILL_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_trill_route);
    CTC_PTR_VALID_CHECK(scl_action);

    scl_action->type   = SYS_SCL_ACTION_TUNNEL;


    if ((CTC_FLAG_ISSET(p_trill_route->flag, CTC_TRILL_ROUTE_FLAG_MCAST))
        &&(CTC_FLAG_ISSET(p_trill_route->flag, CTC_TRILL_ROUTE_FLAG_MACSA_CHECK)))
    {
        scl_action->u.tunnel_action.binding_en = 1;
        scl_action->u.tunnel_action.binding_mac_sa = 1;
        sal_memcpy((scl_action->u.tunnel_action.mac_sa), &(p_trill_route->mac_sa), sizeof(mac_addr_t));
    }
    else
    {
        scl_action->u.tunnel_action.binding_en = 1;
        scl_action->u.tunnel_action.trill_multi_rpf_check = 1;
        scl_action->u.tunnel_action.u1.data[0] = SYS_MAP_CTC_GPORT_TO_DRV_GPORT(p_trill_route->src_gport);
        CTC_BIT_SET(scl_action->u.tunnel_action.u1.data[0], 14);
    }

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_trill_add_check_entry(uint8 lchip, ctc_trill_route_t* p_trill_route)
{
    sys_scl_entry_t  scl_entry;
    uint32 gid = 0;
    SYS_TRILL_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_trill_route);

    sal_memset(&scl_entry, 0, sizeof(sys_scl_entry_t));
    scl_entry.key.type = SYS_SCL_KEY_NUM;

    /* build scl key */
    CTC_ERROR_RETURN(_sys_goldengate_trill_build_check_key(lchip, p_trill_route, &scl_entry));

    /* build scl action */
    CTC_ERROR_RETURN(_sys_goldengate_trill_build_check_action(lchip, p_trill_route, &(scl_entry.action)));

    if (CTC_FLAG_ISSET(p_trill_route->flag, CTC_TRILL_ROUTE_FLAG_DEFAULT_ENTRY))
    {
        CTC_ERROR_RETURN(sys_goldengate_scl_set_default_tunnel_action(lchip, scl_entry.key.type, &(scl_entry.action)));
        return CTC_E_NONE;
    }

    /* write tunnel entry */
    gid = SYS_SCL_GROUP_ID_INNER_HASH_TRILL_TUNNEL;
    CTC_ERROR_RETURN(sys_goldengate_scl_add_entry(lchip, gid, &scl_entry, 1));
    CTC_ERROR_RETURN(sys_goldengate_scl_install_entry(lchip, scl_entry.entry_id, 1));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_trill_remove_check_entry(uint8 lchip, ctc_trill_route_t* p_trill_route)
{
    sys_scl_entry_t  scl_entry;
    uint32 gid = 0;
    SYS_TRILL_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_trill_route);

    sal_memset(&scl_entry, 0, sizeof(sys_scl_entry_t));
    scl_entry.key.type = SYS_SCL_KEY_NUM;

    /* build scl key */
    CTC_ERROR_RETURN(_sys_goldengate_trill_build_check_key(lchip, p_trill_route, &scl_entry));

    if (CTC_FLAG_ISSET(p_trill_route->flag, CTC_TRILL_ROUTE_FLAG_DEFAULT_ENTRY))
    {
        scl_entry.action.type = SYS_SCL_ACTION_TUNNEL;
        CTC_ERROR_RETURN(sys_goldengate_scl_set_default_tunnel_action(lchip, scl_entry.key.type, &(scl_entry.action)));
        return CTC_E_NONE;
    }

    /* write tunnel entry */
    gid = SYS_SCL_GROUP_ID_INNER_HASH_TRILL_TUNNEL;
    CTC_ERROR_RETURN(sys_goldengate_scl_get_entry_id(lchip, &scl_entry, gid));
    CTC_ERROR_RETURN(sys_goldengate_scl_uninstall_entry(lchip, scl_entry.entry_id, 1));
    CTC_ERROR_RETURN(sys_goldengate_scl_remove_entry(lchip, scl_entry.entry_id, 1));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_trill_build_tunnel_key(uint8 lchip, ctc_trill_tunnel_t* p_trill_tunnel, sys_scl_entry_t* scl_entry)
{
    SYS_TRILL_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_trill_tunnel);
    CTC_PTR_VALID_CHECK(scl_entry);

    if (CTC_FLAG_ISSET(p_trill_tunnel->flag, CTC_TRILL_TUNNEL_FLAG_MCAST))
    {
        scl_entry->key.type = SYS_SCL_KEY_HASH_TRILL_MC;
    }
    else
    {
        scl_entry->key.type = SYS_SCL_KEY_HASH_TRILL_UC;
    }

    scl_entry->key.tunnel_type = SYS_SCL_TUNNEL_TYPE_TRILL;
    scl_entry->key.u.hash_trill_key.egress_nickname = p_trill_tunnel->egress_nickname;
    scl_entry->key.u.hash_trill_key.ingress_nickname = p_trill_tunnel->ingress_nickname;

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_trill_build_tunnel_action(uint8 lchip, ctc_trill_tunnel_t* p_trill_tunnel, sys_scl_action_t* scl_action)
{
    sys_nh_info_dsnh_t nh_info;
    uint32 fwd_offset = 0;
    uint16 stats_ptr = 0;
    mac_addr_t mac = {0};
    uint8 route_mac_index = 0;

    SYS_TRILL_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_trill_tunnel);
    CTC_PTR_VALID_CHECK(scl_action);

    scl_action->type   = SYS_SCL_ACTION_TUNNEL;
    scl_action->u.tunnel_action.is_tunnel = 1;
    scl_action->u.tunnel_action.inner_packet_lookup = 1;
    scl_action->u.tunnel_action.logic_port_type = 1;

    if(p_trill_tunnel->fid)
    {
        scl_action->u.tunnel_action.fid_type = 1;
        scl_action->u.tunnel_action.u2.fid = (0x1 << 14 ) | p_trill_tunnel->fid;
    }
    else if(p_trill_tunnel->nh_id)
    {
        sal_memset(&nh_info, 0, sizeof(sys_nh_info_dsnh_t));
        CTC_ERROR_RETURN(sys_goldengate_nh_get_nhinfo(lchip, p_trill_tunnel->nh_id, &nh_info));
        if (nh_info.ecmp_valid)
        {
            return CTC_E_UNEXPECT;
        }
        else
        {
            CTC_ERROR_RETURN(sys_goldengate_nh_get_dsfwd_offset(lchip, p_trill_tunnel->nh_id, &fwd_offset));
            scl_action->u.tunnel_action.ds_fwd_ptr_valid = 1;
            scl_action->u.tunnel_action.u2.dsfwdptr = fwd_offset & 0xffff;
            scl_action->u.tunnel_action.route_disable = 1;
            scl_action->u.tunnel_action.deny_bridge = 1;
        }
    }

    scl_action->u.tunnel_action.u1.g2.logic_src_port = p_trill_tunnel->logic_src_port;
    scl_action->u.tunnel_action.deny_learning = CTC_FLAG_ISSET(p_trill_tunnel->flag, CTC_TRILL_TUNNEL_FLAG_DENY_LEARNING)? 1 : 0;
    if(p_trill_tunnel->stats_id)
    {
        CTC_ERROR_RETURN(sys_goldengate_stats_get_statsptr(lchip, p_trill_tunnel->stats_id, CTC_STATS_STATSID_TYPE_TUNNEL, &stats_ptr));
        scl_action->u.tunnel_action.chip.stats_ptr = stats_ptr;
    }

    if (sal_memcmp(&mac, &(p_trill_tunnel->route_mac), sizeof(mac_addr_t)))
    {
        CTC_ERROR_RETURN(sys_goldengate_l3if_binding_inner_route_mac(lchip, &(p_trill_tunnel->route_mac), &route_mac_index));
        scl_action->u.tunnel_action.router_mac_profile_en = 1;
        scl_action->u.tunnel_action.router_mac_profile = route_mac_index;
    }

    if (CTC_FLAG_ISSET(p_trill_tunnel->flag, CTC_TRILL_TUNNEL_FLAG_ARP_ACTION))
    {
        CTC_MAX_VALUE_CHECK(p_trill_tunnel->arp_action, MAX_CTC_EXCP_TYPE - 1);
        scl_action->u.tunnel_action.arp_ctl_en = 1;
        scl_action->u.tunnel_action.arp_exception_type = p_trill_tunnel->arp_action;
    }

    if (CTC_FLAG_ISSET(p_trill_tunnel->flag, CTC_TRILL_TUNNEL_FLAG_SERVICE_ACL_EN))
    {
        scl_action->u.tunnel_action.service_acl_qos_en = 1;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_trill_add_default_entry(uint8 lchip, uint8 action)
{
    uint32 cmd = 0;
    uint32 default_entry = 0;
    uint32 fwd_offset = 0;
    ds_t ds;

    if (0 == action)
    {
        /*drop*/
        CTC_ERROR_RETURN(sys_goldengate_nh_get_dsfwd_offset(lchip, 1, &fwd_offset));
    }
    else
    {
        /*to cpu*/
        CTC_ERROR_RETURN(sys_goldengate_nh_get_dsfwd_offset(lchip, 2, &fwd_offset));
    }
    CTC_ERROR_RETURN(sys_goldengate_ftm_alloc_table_offset(lchip, DsTrillDa_t, 0, 1, &default_entry));
    sal_memset(ds, 0, sizeof(ds));
    SetDsTrillDa(V, nextHopPtrValid_f, ds, 0);
    SetDsTrillDa(V, u1_g1_dsFwdPtr_f, ds, fwd_offset);
    cmd = DRV_IOW(DsTrillDa_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, default_entry, cmd, ds));

    sal_memset(ds, 0, sizeof(ds));
    cmd = DRV_IOR(FibEngineLookupResultCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));
    SetFibEngineLookupResultCtl(V, gTrillUcastLookupResultCtl_defaultEntryEn_f, ds, 1);
    SetFibEngineLookupResultCtl(V, gTrillUcastLookupResultCtl_defaultEntryPtr_f, ds, default_entry);
    SetFibEngineLookupResultCtl(V, gTrillMcastLookupResultCtl_defaultEntryEn_f, ds, 1);
    SetFibEngineLookupResultCtl(V, gTrillMcastLookupResultCtl_defaultEntryPtr_f, ds, default_entry);
    cmd = DRV_IOW(FibEngineLookupResultCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));

    return CTC_E_NONE;
}


#define _______API_______
int32
sys_goldengate_trill_init(uint8 lchip, void* trill_global_cfg)
{
    uint8 default_entry_action = 0;
    p_gg_sys_trill_master[lchip] = (sys_trill_master_t*)mem_malloc(MEM_TRILL_MODULE, sizeof(sys_trill_master_t));
    if (NULL == p_gg_sys_trill_master[lchip])
    {
        return CTC_E_NO_MEMORY;
    }
    sal_memset(p_gg_sys_trill_master[lchip], 0, sizeof(sys_trill_master_t));

    CTC_ERROR_RETURN(_sys_goldengate_trill_add_default_entry(lchip, default_entry_action));

    return CTC_E_NONE;
}

int32
sys_goldengate_trill_deinit(uint8 lchip)
{
    LCHIP_CHECK(lchip);
    if (NULL == p_gg_sys_trill_master[lchip])
    {
        return CTC_E_NONE;
    }

    mem_free(p_gg_sys_trill_master[lchip]);

    return CTC_E_NONE;
}

int32
sys_goldengate_trill_add_route(uint8 lchip, ctc_trill_route_t* p_trill_route)
{
    SYS_TRILL_DBG_FUNC();
    SYS_TRILL_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_trill_route);

    SYS_TRILL_DBG_PARAM("flag :0x%x\n", p_trill_route->flag);
    SYS_TRILL_DBG_PARAM("egress_nickname :%d\n", p_trill_route->egress_nickname);
    SYS_TRILL_DBG_PARAM("ingress_nickname :%d\n", p_trill_route->ingress_nickname);
    SYS_TRILL_DBG_PARAM("vlan_id :%d\n", p_trill_route->vlan_id);
    SYS_TRILL_DBG_PARAM("nh_id :%d\n", p_trill_route->nh_id);
    SYS_TRILL_DBG_PARAM("src_gport :0x%x\n", p_trill_route->src_gport);

    CTC_GLOBAL_PORT_CHECK(p_trill_route->src_gport);
    if (p_trill_route->vlan_id > CTC_MAX_VLAN_ID)
    {
        return CTC_E_VLAN_INVALID_VLAN_ID;
    }
    if ((!CTC_FLAG_ISSET(p_trill_route->flag, CTC_TRILL_ROUTE_FLAG_MCAST))
        && (CTC_FLAG_ISSET(p_trill_route->flag, CTC_TRILL_ROUTE_FLAG_MACSA_CHECK)))
    {
        return CTC_E_INVALID_PARAM;
    }

    /*check default entry flag*/
    if ((CTC_FLAG_ISSET(p_trill_route->flag, CTC_TRILL_ROUTE_FLAG_DEFAULT_ENTRY))
        && (!CTC_FLAG_ISSET(p_trill_route->flag, CTC_TRILL_ROUTE_FLAG_SRC_PORT_CHECK)
        && (!CTC_FLAG_ISSET(p_trill_route->flag, CTC_TRILL_ROUTE_FLAG_MACSA_CHECK)
        || !CTC_FLAG_ISSET(p_trill_route->flag, CTC_TRILL_ROUTE_FLAG_MCAST))))
    {
        return CTC_E_INVALID_PARAM;
    }

    if ((CTC_FLAG_ISSET(p_trill_route->flag, CTC_TRILL_ROUTE_FLAG_SRC_PORT_CHECK))
        ||
        ((CTC_FLAG_ISSET(p_trill_route->flag, CTC_TRILL_ROUTE_FLAG_MCAST))
        && (CTC_FLAG_ISSET(p_trill_route->flag, CTC_TRILL_ROUTE_FLAG_MACSA_CHECK))))
    {
        CTC_ERROR_RETURN(_sys_goldengate_trill_add_check_entry(lchip, p_trill_route));
    }
    else
    {
       CTC_ERROR_RETURN(_sys_goldengate_trill_add_route_entry(lchip, p_trill_route));
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_trill_remove_route(uint8 lchip, ctc_trill_route_t* p_trill_route)
{
    SYS_TRILL_DBG_FUNC();
    SYS_TRILL_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_trill_route);

    SYS_TRILL_DBG_PARAM("flag :0x%x\n", p_trill_route->flag);
    SYS_TRILL_DBG_PARAM("egress_nickname :%d\n", p_trill_route->egress_nickname);
    SYS_TRILL_DBG_PARAM("ingress_nickname :%d\n", p_trill_route->ingress_nickname);
    SYS_TRILL_DBG_PARAM("vlan_id :%d\n", p_trill_route->vlan_id);
    SYS_TRILL_DBG_PARAM("nh_id :%d\n", p_trill_route->nh_id);
    SYS_TRILL_DBG_PARAM("src_gport :0x%x\n", p_trill_route->src_gport);

    CTC_GLOBAL_PORT_CHECK(p_trill_route->src_gport);
    if (p_trill_route->vlan_id > CTC_MAX_VLAN_ID)
    {
        return CTC_E_VLAN_INVALID_VLAN_ID;
    }
    if ((!CTC_FLAG_ISSET(p_trill_route->flag, CTC_TRILL_ROUTE_FLAG_MCAST))
        && (CTC_FLAG_ISSET(p_trill_route->flag, CTC_TRILL_ROUTE_FLAG_MACSA_CHECK)))
    {
        return CTC_E_INVALID_PARAM;
    }

    if ((CTC_FLAG_ISSET(p_trill_route->flag, CTC_TRILL_ROUTE_FLAG_SRC_PORT_CHECK))
        || (CTC_FLAG_ISSET(p_trill_route->flag, CTC_TRILL_ROUTE_FLAG_MACSA_CHECK)))
    {
        CTC_ERROR_RETURN(_sys_goldengate_trill_remove_check_entry(lchip, p_trill_route));
    }
    else
    {
       CTC_ERROR_RETURN(_sys_goldengate_trill_remove_route_entry(lchip, p_trill_route));
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_trill_add_tunnel(uint8 lchip, ctc_trill_tunnel_t* p_trill_tunnel)
{
    sys_scl_entry_t  scl_entry;
    uint32 gid = 0;

    SYS_TRILL_DBG_FUNC();
    SYS_TRILL_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_trill_tunnel);

    SYS_TRILL_DBG_PARAM("flag :0x%x\n", p_trill_tunnel->flag);
    SYS_TRILL_DBG_PARAM("egress_nickname :%d\n", p_trill_tunnel->egress_nickname);
    SYS_TRILL_DBG_PARAM("ingress_nickname :%d\n", p_trill_tunnel->ingress_nickname);
    SYS_TRILL_DBG_PARAM("fid :%d\n", p_trill_tunnel->fid);
    SYS_TRILL_DBG_PARAM("nh_id :%d\n", p_trill_tunnel->nh_id);
    SYS_TRILL_DBG_PARAM("logic_src_port :0x%x\n", p_trill_tunnel->logic_src_port);
    SYS_TRILL_DBG_PARAM("stats_id :%d\n", p_trill_tunnel->stats_id);

    if ( p_trill_tunnel->logic_src_port > 0x3FFF )
    {
        return CTC_E_INVALID_LOGIC_PORT;
    }
    if ( p_trill_tunnel->fid > 0x3FFF )
    {
        return CTC_E_INVALID_PARAM;
    }

    sal_memset(&scl_entry, 0, sizeof(sys_scl_entry_t));
    scl_entry.key.type = SYS_SCL_KEY_NUM;

    /* build scl key */
    CTC_ERROR_RETURN(_sys_goldengate_trill_build_tunnel_key(lchip, p_trill_tunnel, &scl_entry));

    /* build scl action */
    CTC_ERROR_RETURN(_sys_goldengate_trill_build_tunnel_action(lchip, p_trill_tunnel, &(scl_entry.action)));

    if (CTC_FLAG_ISSET(p_trill_tunnel->flag, CTC_TRILL_TUNNEL_FLAG_DEFAULT_ENTRY))
    {
        CTC_ERROR_RETURN(sys_goldengate_scl_set_default_tunnel_action(lchip, scl_entry.key.type, &(scl_entry.action)));
        return CTC_E_NONE;
    }

    /* write tunnel entry */
    gid = SYS_SCL_GROUP_ID_INNER_HASH_TRILL_TUNNEL;
    CTC_ERROR_RETURN(sys_goldengate_scl_add_entry(lchip, gid, &scl_entry, 1));
    CTC_ERROR_RETURN(sys_goldengate_scl_install_entry(lchip, scl_entry.entry_id, 1));

    return CTC_E_NONE;
}

int32
sys_goldengate_trill_remove_tunnel(uint8 lchip, ctc_trill_tunnel_t* p_trill_tunnel)
{
    sys_scl_entry_t  scl_entry;
    sys_scl_action_t scl_action;
    uint32 gid = 0;

    SYS_TRILL_DBG_FUNC();
    SYS_TRILL_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_trill_tunnel);

    SYS_TRILL_DBG_PARAM("flag :0x%x\n", p_trill_tunnel->flag);
    SYS_TRILL_DBG_PARAM("egress_nickname :%d\n", p_trill_tunnel->egress_nickname);
    SYS_TRILL_DBG_PARAM("ingress_nickname :%d\n", p_trill_tunnel->ingress_nickname);
    SYS_TRILL_DBG_PARAM("fid :%d\n", p_trill_tunnel->fid);
    SYS_TRILL_DBG_PARAM("nh_id :%d\n", p_trill_tunnel->nh_id);
    SYS_TRILL_DBG_PARAM("logic_src_port :0x%x\n", p_trill_tunnel->logic_src_port);
    SYS_TRILL_DBG_PARAM("stats_id :%d\n", p_trill_tunnel->stats_id);

    if ( p_trill_tunnel->logic_src_port > 0x3FFF )
    {
        return CTC_E_INVALID_LOGIC_PORT;
    }
    if ( p_trill_tunnel->fid > 0x3FFF )
    {
        return CTC_E_INVALID_PARAM;
    }

    sal_memset(&scl_entry, 0, sizeof(sys_scl_entry_t));
    sal_memset(&scl_action, 0, sizeof(sys_scl_action_t));
    scl_entry.key.type = SYS_SCL_KEY_NUM;

    /* build scl key */
    CTC_ERROR_RETURN(_sys_goldengate_trill_build_tunnel_key(lchip, p_trill_tunnel, &scl_entry));

    if (CTC_FLAG_ISSET(p_trill_tunnel->flag, CTC_TRILL_TUNNEL_FLAG_DEFAULT_ENTRY))
    {
        scl_entry.action.type = SYS_SCL_ACTION_TUNNEL;
        CTC_ERROR_RETURN(sys_goldengate_scl_set_default_tunnel_action(lchip, scl_entry.key.type, &(scl_entry.action)));
        return CTC_E_NONE;
    }

    /* write tunnel entry */
    gid = SYS_SCL_GROUP_ID_INNER_HASH_TRILL_TUNNEL;
    CTC_ERROR_RETURN(sys_goldengate_scl_get_entry_id(lchip, &scl_entry, gid));
    CTC_ERROR_RETURN(sys_goldengate_scl_get_action(lchip, scl_entry.entry_id, &scl_action, 1));
    CTC_ERROR_RETURN(sys_goldengate_scl_uninstall_entry(lchip, scl_entry.entry_id, 1));
    CTC_ERROR_RETURN(sys_goldengate_scl_remove_entry(lchip, scl_entry.entry_id, 1));

    if (scl_action.u.tunnel_action.router_mac_profile_en)
    {
        CTC_ERROR_RETURN(sys_goldengate_l3if_unbinding_inner_route_mac(lchip, scl_action.u.tunnel_action.router_mac_profile));
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_trill_update_tunnel(uint8 lchip, ctc_trill_tunnel_t* p_trill_tunnel)
{
    sys_scl_entry_t scl_entry;
    sys_scl_action_t scl_action;
    int32 ret = CTC_E_NONE;

    SYS_TRILL_DBG_FUNC();
    SYS_TRILL_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_trill_tunnel);

    SYS_TRILL_DBG_PARAM("flag :0x%x\n", p_trill_tunnel->flag);
    SYS_TRILL_DBG_PARAM("egress_nickname :%d\n", p_trill_tunnel->egress_nickname);
    SYS_TRILL_DBG_PARAM("ingress_nickname :%d\n", p_trill_tunnel->ingress_nickname);
    SYS_TRILL_DBG_PARAM("fid :%d\n", p_trill_tunnel->fid);
    SYS_TRILL_DBG_PARAM("nh_id :%d\n", p_trill_tunnel->nh_id);
    SYS_TRILL_DBG_PARAM("logic_src_port :0x%x\n", p_trill_tunnel->logic_src_port);
    SYS_TRILL_DBG_PARAM("stats_id :%d\n", p_trill_tunnel->stats_id);

    if ( p_trill_tunnel->logic_src_port > 0x3FFF )
    {
        return CTC_E_INVALID_LOGIC_PORT;
    }
    if ( p_trill_tunnel->fid > 0x3FFF )
    {
        return CTC_E_INVALID_PARAM;
    }

    sal_memset(&scl_entry, 0, sizeof(sys_scl_entry_t));
    sal_memset(&scl_action, 0, sizeof(sys_scl_action_t));

    /* build scl key */
    CTC_ERROR_RETURN(_sys_goldengate_trill_build_tunnel_key(lchip, p_trill_tunnel, &scl_entry));

    /* build scl action */
    CTC_ERROR_RETURN(_sys_goldengate_trill_build_tunnel_action(lchip, p_trill_tunnel, &(scl_entry.action)));

    if (CTC_FLAG_ISSET(p_trill_tunnel->flag, CTC_TRILL_TUNNEL_FLAG_DEFAULT_ENTRY))
    {
        CTC_ERROR_RETURN(sys_goldengate_scl_set_default_tunnel_action(lchip, scl_entry.key.type, &(scl_entry.action)));
        return CTC_E_NONE;
    }

    /* update action */
    ret = sys_goldengate_scl_get_entry_id(lchip, &scl_entry, SYS_SCL_GROUP_ID_INNER_HASH_OVERLAY_TUNNEL);
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
        if (scl_entry.action.u.tunnel_action.router_mac_profile_en)/*build in _sys_goldengate_trill_build_tunnel_action*/
        {
            CTC_ERROR_RETURN(sys_goldengate_l3if_unbinding_inner_route_mac(lchip, scl_entry.action.u.tunnel_action.router_mac_profile));
        }
        return ret;
    }

    return CTC_E_NONE;
}




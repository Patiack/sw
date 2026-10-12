
#include "ctc_oam.h"
#include "ctc_error.h"
#include "ctc_common.h"
#include "ctc_linklist.h"
#include "ctc_hash.h"

#include "sys_greatbelt_oam.h"
#include "sys_greatbelt_oam_db.h"
#include "sys_greatbelt_oam_debug.h"
#include "sys_greatbelt_chip.h"
#include "sys_greatbelt_opf.h"
#include "sys_greatbelt_ftm.h"
#include "sys_greatbelt_common.h"
#include "sys_greatbelt_port.h"

#include "sys_greatbelt_vlan.h"

#include "sys_greatbelt_nexthop_api.h"
#include "sys_greatbelt_nexthop.h"
#include "sys_greatbelt_oam_com.h"

#include "greatbelt/include/drv_data_path.h"
#include "greatbelt/include/drv_io.h"
#include "greatbelt/include/drv_enum.h"

/***************************************************************
*
*  Defines and Macros
*
***************************************************************/
struct sys_oam_maid_hash_key_s
{
    uint8   mep_type;
    uint8   resv0[3];

    uint8   maid[SYS_OAM_MAX_MAID_LEN];
};
typedef struct sys_oam_maid_hash_key_s sys_oam_maid_hash_key_t;

struct sys_eth_oam_chan_key_s
{
    uint16    vlan_id;
    uint16    gport;
};
typedef struct sys_eth_oam_chan_key_s sys_eth_oam_chan_key_t;

struct sys_mpls_tp_chan_key_s
{
    uint32  label;
    uint16  gport_l3if_id;
    uint8   section_oam;
    uint8   spaceid;
};
typedef struct sys_mpls_tp_chan_key_s sys_mpls_tp_chan_key_t;


struct sys_bfd_chan_key_s
{
    uint32  my_discr;
};
typedef struct sys_bfd_chan_key_s sys_bfd_chan_key_t;


struct sys_oam_chan_hash_key_s
{
    uint8   mep_type;
    union
    {
        sys_eth_oam_chan_key_t eth;    /** eth oam key*/
        sys_mpls_tp_chan_key_t mpls_tp;
        sys_bfd_chan_key_t     bfd;
    } u;
};
typedef struct sys_oam_chan_hash_key_s sys_oam_chan_hash_key_t;

struct sys_oam_defect_type_s
{
    uint8 entry_num;
    uint8 defect_type[2];
    uint8 defect_sub_type[2];
};
typedef struct sys_oam_defect_type_s sys_oam_defect_type_t;

#define SYS_OAM_LM_CMD_READ(lchip, lm_index, ds_oam_lm_stats) \
    do { \
        uint32 cmd = 0; \
        if (lm_index < 128){ \
            cmd = DRV_IOR(DsOamLmStats0_t, DRV_ENTRY_FLAG); \
            DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, lm_index, cmd, &ds_oam_lm_stats)); \
        } else if (lm_index < 256){ \
            cmd = DRV_IOR(DsOamLmStats1_t, DRV_ENTRY_FLAG); \
            DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, lm_index - 128, cmd, &ds_oam_lm_stats)); \
        } else { \
            ds_oam_lm_stats_t ds_oam_lm_stats_dyn; \
            sal_memset(&ds_oam_lm_stats_dyn, 0, sizeof(ds_oam_lm_stats_t)); \
            cmd = DRV_IOR(DsOamLmStats_t, DRV_ENTRY_FLAG);  \
            DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, lm_index - 256, cmd, &ds_oam_lm_stats_dyn)); \
            ds_oam_lm_stats.rx_fcb_r    = ds_oam_lm_stats_dyn.rx_fcb_r; \
            ds_oam_lm_stats.rx_fcl      = ds_oam_lm_stats_dyn.rx_fcl;   \
            ds_oam_lm_stats.rx_fcl_r    = ds_oam_lm_stats_dyn.rx_fcl_r; \
            ds_oam_lm_stats.tx_fcb_r    = ds_oam_lm_stats_dyn.tx_fcb_r; \
            ds_oam_lm_stats.tx_fcf_r    = ds_oam_lm_stats_dyn.tx_fcf_r; \
            ds_oam_lm_stats.tx_fcl      = ds_oam_lm_stats_dyn.tx_fcl;   \
        } \
    } while (0)

#define SYS_OAM_LM_CMD_WRITE(lchip, lm_index, ds_oam_lm_stats) \
    do { \
        uint32 cmd = 0; \
        if (lm_index < 128){ \
            cmd = DRV_IOW(DsOamLmStats0_t, DRV_ENTRY_FLAG); \
            DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, lm_index, cmd, &ds_oam_lm_stats)); \
        } else if (lm_index < 256){ \
            cmd = DRV_IOW(DsOamLmStats1_t, DRV_ENTRY_FLAG); \
            DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, lm_index - 128, cmd, &ds_oam_lm_stats)); \
        } else { \
            ds_oam_lm_stats_t ds_oam_lm_stats_dyn; \
            sal_memset(&ds_oam_lm_stats_dyn, 0, sizeof(ds_oam_lm_stats_t)); \
            cmd = DRV_IOW(DsOamLmStats_t, DRV_ENTRY_FLAG);  \
            ds_oam_lm_stats_dyn.rx_fcb_r    = ds_oam_lm_stats.rx_fcb_r; \
            ds_oam_lm_stats_dyn.rx_fcl      = ds_oam_lm_stats.rx_fcl;   \
            ds_oam_lm_stats_dyn.rx_fcl_r    = ds_oam_lm_stats.rx_fcl_r; \
            ds_oam_lm_stats_dyn.tx_fcb_r    = ds_oam_lm_stats.tx_fcb_r; \
            ds_oam_lm_stats_dyn.tx_fcf_r    = ds_oam_lm_stats.tx_fcf_r; \
            ds_oam_lm_stats_dyn.tx_fcl      = ds_oam_lm_stats.tx_fcl;   \
            DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, lm_index - 256, cmd, &ds_oam_lm_stats_dyn)); \
        } \
    } while (0)

#define ROT(x, k) (((x) << (k)) | ((x) >> (32 - (k))))

#define MIX(a, b, c) \
    do \
    { \
        a -= c;  a ^= ROT(c, 4);  c += b; \
        b -= a;  b ^= ROT(a, 6);  a += c; \
        c -= b;  c ^= ROT(b, 8);  b += a; \
        a -= c;  a ^= ROT(c, 16);  c += b; \
        b -= a;  b ^= ROT(a, 19);  a += c; \
        c -= b;  c ^= ROT(b, 4);  b += a; \
    } while (0)

#define FINAL(a, b, c) \
    { \
        c ^= b; c -= ROT(b, 14); \
        a ^= c; a -= ROT(c, 11); \
        b ^= a; b -= ROT(a, 25); \
        c ^= b; c -= ROT(b, 16); \
        a ^= c; a -= ROT(c, 4);  \
        b ^= a; b -= ROT(a, 14); \
        c ^= b; c -= ROT(b, 24); \
    }

#define SYS_OAM_MAID_HASH_BLOCK_SIZE 32
#define SYS_OAM_CHAN_HASH_BLOCK_SIZE 32

#define CTC_GREATBELT_OAM_DEFECT_NUM 20

extern sys_oam_master_t* g_gb_oam_master[CTC_MAX_LOCAL_CHIP_NUM];
extern int32 sys_greatbelt_oam_trpt_set_autogen_ptr(uint8 lchip, uint8 session_id, uint16 lmep_idx);
extern int32 _sys_greatbelt_bfd_update_chan(uint8 lchip, sys_oam_lmep_bfd_t* p_sys_lmep_bfd, bool is_add);
extern int32 _sys_greatbelt_tp_chan_update(uint8 lchip, sys_oam_lmep_y1731_t* p_sys_lmep_tp_y1731, bool is_add);
extern int32 _sys_greatbelt_cfm_chan_update(uint8 lchip, sys_oam_lmep_y1731_t* p_sys_lmep_eth, bool is_add);

/***************************************************************
 *
 *  Functions
 *
 ***************************************************************/

#define HASH_KEY "Function Begin"

STATIC uint32
_sys_greatbelt_oam_build_hash_key(void* p_data, uint8 length)
{
    uint32 a, b, c;
    uint32* k = (uint32*)p_data;
    uint8*  k8;

    /* Set up the internal state */
    a = b = c = 0xdeadbeef;

    /*------ all but last block: aligned reads and affect 32 bits of (a,b,c) */
    while (length > 12)
    {
        a += k[0];
        b += k[1];
        c += k[2];
        MIX(a, b, c);
        length -= 12;
        k += 3;
    }

    k8 = (uint8*)k;

    switch (length)
    {
    case 12:
        c += k[2];
        b += k[1];
        a += k[0];
        break;

    case 11:
        c += ((uint8)k8[10]) << 16;       /* fall through */

    case 10:
        c += ((uint8)k8[9]) << 8;         /* fall through */

    case 9:
        c += k8[8];                          /* fall through */

    case 8:
        b += k[1];
        a += k[0];
        break;

    case 7:
        b += ((uint8)k8[6]) << 16;        /* fall through */

    case 6:
        b += ((uint8)k8[5]) << 8;         /* fall through */

    case 5:
        b += k8[4];                          /* fall through */

    case 4:
        a += k[0];
        break;

    case 3:
        a += ((uint8)k8[2]) << 16;        /* fall through */

    case 2:
        a += ((uint8)k8[1]) << 8;         /* fall through */

    case 1:
        a += k8[0];
        break;

    case 0:
        return c;
    }

    FINAL(a, b, c);
    return c;
}

#define MISC "Function Begin"

int32
_sys_greatbelt_oam_get_mep_entry_num(uint8 lchip)
{
    uint32 max_mep_index = 0;

    CTC_ERROR_RETURN(sys_greatbelt_ftm_get_table_entry_num(lchip, DsEthMep_t,  &max_mep_index));

    return max_mep_index;
}

int32
_sys_greatbelt_oam_get_mpls_entry_num(uint8 lchip)
{
    uint32 mpls_index = 0;

    CTC_ERROR_RETURN(sys_greatbelt_ftm_get_table_entry_num(lchip, DsMpls_t,  &mpls_index));

    return mpls_index;
}

uint8
_sys_greatbelt_bfd_csf_convert(uint8 lchip, uint8 type, bool to_asic)
{
    uint8 csf_type = 0;

    if (to_asic)
    {
        if(3 == type)
        {
            csf_type = 0;
        }
        else if (0 == type)
        {
            csf_type = 7;
        }
        else
        {
            csf_type = type;
        }
    }
    else
    {
        if(0 == type)
        {
            csf_type = 3;
        }
        else if (7 == type)
        {
            csf_type = 0;
        }
        else
        {
            csf_type = type;
        }
    }

    return csf_type;
}

int32
_sys_greatbelt_oam_get_nexthop_info(uint8 lchip, uint32 nhid, uint32 b_protection, sys_oam_nhop_info_t* p_nhop_info)
{
    sys_nh_info_dsnh_t nh_info;

    sal_memset(&nh_info, 0, sizeof(sys_nh_info_dsnh_t));
    nh_info.b_protection = b_protection;
    nh_info.get_by_oam = 1;

    CTC_ERROR_RETURN(sys_greatbelt_nh_get_nhinfo(lchip, nhid, &nh_info));

    if (nh_info.drop_pkt)
    {
        if(NULL != p_nhop_info)
        {
            p_nhop_info->dest_chipid    = 0;
            p_nhop_info->dest_id        = SYS_RESERVE_PORT_ID_DROP;
        }
    }
    else
    {
        if(NULL != p_nhop_info)
        {
            p_nhop_info->dest_chipid    = nh_info.dest_chipid;
            p_nhop_info->dest_id        = nh_info.dest_id;
            p_nhop_info->mpls_out_label = nh_info.mpls_out_label;
        }
    }
    if(NULL != p_nhop_info)
    {
        p_nhop_info->nh_entry_type  = nh_info.nh_entry_type;
        p_nhop_info->nexthop_is_8w  = nh_info.nexthop_ext;
        p_nhop_info->dsnh_offset    = nh_info.dsnh_offset;
        p_nhop_info->aps_bridge_en  = nh_info.aps_en;
        p_nhop_info->is_nat         = nh_info.is_nat;
    }

    return CTC_E_NONE;
}


int32
_sys_greatbelt_oam_check_nexthop_type (uint8 lchip, uint32 nhid, uint32 b_protection, uint8 mep_type)
{
    sys_oam_nhop_info_t nhop_info;
    int32 ret = CTC_E_NONE;

    sal_memset(&nhop_info, 0, sizeof(sys_oam_nhop_info_t));
    CTC_ERROR_RETURN(_sys_greatbelt_oam_get_nexthop_info(lchip, nhid, b_protection, &nhop_info));

    if(SYS_NH_TYPE_DROP != nhop_info.nh_entry_type)
    {
        if((CTC_OAM_MEP_TYPE_IP_BFD == mep_type)
            && (SYS_NH_TYPE_IP_TUNNEL != nhop_info.nh_entry_type))
        {
            ret = CTC_E_NH_INVALID_NHID;
        }

        if(((CTC_OAM_MEP_TYPE_MPLS_TP_BFD == mep_type)
            || (CTC_OAM_MEP_TYPE_MPLS_TP_Y1731 == mep_type)
            || (CTC_OAM_MEP_TYPE_MPLS_BFD == mep_type))
            && ((SYS_NH_TYPE_MPLS != nhop_info.nh_entry_type)
                &&(SYS_NH_TYPE_MCAST != nhop_info.nh_entry_type)))
        {
            ret = CTC_E_NH_INVALID_NHID;
        }
    }

    return ret;

}

STATIC uint8
_sys_greatbelt_oam_cal_bitmap_bit(uint8 lchip, uint32 bitmap)
{
    uint8 bit = 0;

    while (bitmap)
    {
        bit++;
        bitmap = (bitmap >> 1);
    }

    return (bit - 1);
}

int32
_sys_greatbelt_oam_get_defect_type(uint8 lchip, uint32 defect, sys_oam_defect_type_t* defect_type)
{
    switch (defect)
    {
    case CTC_OAM_DEFECT_EVENT_RX_FIRST_PKT:
        defect_type->defect_type[0]     = 2;
        defect_type->defect_sub_type[0] = 4;
        defect_type->entry_num          = 1;
        break;

    case CTC_OAM_DEFECT_MAC_STATUS_CHANGE:
        defect_type->defect_type[0]     = 0;
        defect_type->defect_sub_type[0] = 1;
        defect_type->entry_num          = 1;
        break;

    case CTC_OAM_DEFECT_SRC_MAC_MISMATCH:
        defect_type->defect_type[0]     = 0;
        defect_type->defect_sub_type[0] = 7;
        defect_type->entry_num          = 1;
        break;

    case CTC_OAM_DEFECT_EVENT_RDI_RX:
        /*rx rdi*/
        defect_type->defect_type[0]     = 1;
        defect_type->defect_sub_type[0] = 0;
        /*rx rdi clear*/
        defect_type->defect_type[1]     = 1;
        defect_type->defect_sub_type[1] = 7;
        defect_type->entry_num          = 2;
        break;

    case CTC_OAM_DEFECT_MISMERGE:
        /*mismerge*/
        defect_type->defect_type[0]     = 5;
        defect_type->defect_sub_type[0] = 0;
        /*mismerge clear*/
        defect_type->defect_type[1]     = 5;
        defect_type->defect_sub_type[1] = 7;
        defect_type->entry_num          = 2;
        break;

    case CTC_OAM_DEFECT_UNEXPECTED_LEVEL:
        /*low level*/
        defect_type->defect_type[0]     = 5;
        defect_type->defect_sub_type[0] = 1;
        /*low level clear*/
        defect_type->defect_type[1]     = 5;
        defect_type->defect_sub_type[1] = 6;
        defect_type->entry_num          = 2;
        break;

    case CTC_OAM_DEFECT_UNEXPECTED_MEP:
        /*unexpected mep*/
        defect_type->defect_type[0]     = 4;
        defect_type->defect_sub_type[0] = 0;
        /*unexpected mep clear*/
        defect_type->defect_type[1]     = 4;
        defect_type->defect_sub_type[1] = 6;
        defect_type->entry_num          = 2;
        break;

    case CTC_OAM_DEFECT_UNEXPECTED_PERIOD:
        /*unexpected period*/
        defect_type->defect_type[0]     = 4;
        defect_type->defect_sub_type[0] = 1;
        /*unexpected period clear*/
        defect_type->defect_type[1]     = 4;
        defect_type->defect_sub_type[1] = 7;
        defect_type->entry_num          = 2;
        break;

    case CTC_OAM_DEFECT_DLOC:
        /* dloc*/
        defect_type->defect_type[0]     = 3;
        defect_type->defect_sub_type[0] = 0;
        /* dloc clear*/
        defect_type->defect_type[1]     = 3;
        defect_type->defect_sub_type[1] = 7;
        defect_type->entry_num          = 2;
        break;

    case CTC_OAM_DEFECT_CSF:
        /* dcsf*/
        defect_type->defect_type[0]     = 3;
        defect_type->defect_sub_type[0] = 1;
        /* dcsf clear*/
        defect_type->defect_type[1]     = 3;
        defect_type->defect_sub_type[1] = 2;
        defect_type->entry_num          = 2;
        break;

    case CTC_OAM_DEFECT_EVENT_BFD_INIT:
        defect_type->defect_type[0]     = 2;
        defect_type->defect_sub_type[0] = 1;
        defect_type->entry_num          = 1;
        break;

    case CTC_OAM_DEFECT_EVENT_BFD_UP:
        defect_type->defect_type[0]     = 2;
        defect_type->defect_sub_type[0] = 2;
        defect_type->entry_num          = 1;
        break;

    case CTC_OAM_DEFECT_EVENT_BFD_DOWN:
        defect_type->defect_type[0]     = 2;
        defect_type->defect_sub_type[0] = 3;
        defect_type->entry_num          = 1;
        break;

    default:
        defect_type->defect_type[0]     = 0;
        defect_type->defect_sub_type[0] = 0;
        defect_type->entry_num          = 0;
        break;
    }

    return CTC_E_NONE;
}

int32
_sys_greatbelt_oam_get_rdi_defect_type(uint8 lchip, uint32 defect, uint8* defect_type, uint8* defect_sub_type)
{
    uint8 type      = 0;
    uint8 sub_type  = 0;

    switch (defect)
    {
    case CTC_OAM_DEFECT_MAC_STATUS_CHANGE:
        type        = 0;
        sub_type    = 1;
        break;

    case CTC_OAM_DEFECT_SRC_MAC_MISMATCH:
        type        = 0;
        sub_type    = 7;
        break;

    case CTC_OAM_DEFECT_MISMERGE:
        type        = 5;
        sub_type    = 0;
        break;

    case CTC_OAM_DEFECT_UNEXPECTED_LEVEL:
        type        = 5;
        sub_type    = 1;
        break;

    case CTC_OAM_DEFECT_UNEXPECTED_MEP:
        type        = 4;
        sub_type    = 0;
        break;

    case CTC_OAM_DEFECT_UNEXPECTED_PERIOD:
        type        = 4;
        sub_type    = 1;
        break;

    case CTC_OAM_DEFECT_DLOC:
        type        = 3;
        sub_type    = 0;
        break;

    case CTC_OAM_DEFECT_CSF:
        type        = 3;
        sub_type    = 1;
        break;

    default:
        type        = 0;
        sub_type    = 0;
        break;
    }

    *defect_type        = type;
    *defect_sub_type    = sub_type;

    return CTC_E_NONE;
}

#define MAID "Function Begin"

sys_oam_maid_com_t*
_sys_greatbelt_oam_maid_lkup(uint8 lchip, ctc_oam_maid_t* p_ctc_maid)
{
    ctc_hash_t* p_maid_hash = NULL;
    sys_oam_maid_com_t* p_sys_maid_db = NULL;
    sys_oam_maid_com_t sys_maid;

    sal_memset(&sys_maid, 0 , sizeof(sys_oam_maid_com_t));
    sys_maid.mep_type = p_ctc_maid->mep_type;
    sys_maid.maid_len = p_ctc_maid->maid_len;
    sal_memcpy(&sys_maid.maid, p_ctc_maid->maid, sys_maid.maid_len);

    p_maid_hash = g_gb_oam_master[lchip]->maid_hash;

    p_sys_maid_db = ctc_hash_lookup(p_maid_hash, &sys_maid);

    return p_sys_maid_db;
}

STATIC int32
_sys_greatbelt_oam_maid_cmp(sys_oam_maid_com_t* p_sys_maid,  sys_oam_maid_com_t* p_sys_maid_db)
{
    uint8 maid_len = 0;

    if ((NULL == p_sys_maid) || (NULL == p_sys_maid_db))
    {
        return 0;
    }

    maid_len = p_sys_maid->maid_len;

    if (p_sys_maid->maid_len != p_sys_maid_db->maid_len)
    {
        return 0;
    }

    if (sal_memcmp(&p_sys_maid->maid, &p_sys_maid_db->maid, maid_len))
    {
        return 0;
    }

    return 1;
}

STATIC uint32
_sys_greatbelt_oam_maid_build_key(sys_oam_maid_com_t* p_sys_maid)
{

    sys_oam_maid_hash_key_t maid_hash_key;
    uint8   length = 0;


    sal_memset(&maid_hash_key, 0, sizeof(sys_oam_maid_hash_key_t));

    maid_hash_key.mep_type = p_sys_maid->mep_type;
    sal_memcpy(&maid_hash_key.maid, &p_sys_maid->maid, sizeof(p_sys_maid->maid));
    length = sizeof(maid_hash_key);

    return _sys_greatbelt_oam_build_hash_key(&maid_hash_key, length);
}

sys_oam_maid_com_t*
_sys_greatbelt_oam_maid_build_node(uint8 lchip, ctc_oam_maid_t* p_maid_param)
{
    sys_oam_maid_com_t* p_sys_maid = NULL;
    uint8 maid_entry_num = 0;
    uint8 maid_len = 0;
    uint8 mep_type = CTC_OAM_MEP_TYPE_MAX;

    mep_type = p_maid_param->mep_type;

    p_sys_maid = (sys_oam_maid_com_t*)mem_malloc(MEM_OAM_MODULE, sizeof(sys_oam_maid_com_t));
    if (NULL != p_sys_maid)
    {
        maid_len = p_maid_param->maid_len;

        sal_memset(p_sys_maid, 0, sizeof(sys_oam_maid_com_t));
        p_sys_maid->mep_type    = p_maid_param->mep_type;
        p_sys_maid->maid_len    = maid_len;
        sal_memcpy(p_sys_maid->maid, p_maid_param->maid, maid_len);

        if(((CTC_OAM_MEP_TYPE_ETH_1AG == mep_type)
            || (CTC_OAM_MEP_TYPE_ETH_Y1731 == mep_type)) && (0 != P_COMMON_OAM_MASTER_GLB(lchip).maid_len_format))
        {
            switch (P_COMMON_OAM_MASTER_GLB(lchip).maid_len_format)
            {
            case CTC_OAM_MAID_LEN_16BYTES:
                maid_entry_num = 2;
                break;

            case CTC_OAM_MAID_LEN_32BYTES:
                maid_entry_num = 4;
                break;

            case CTC_OAM_MAID_LEN_48BYTES:
                maid_entry_num = 6;
                break;

            default:
                break;
            }
        }
        else
        {
            if (maid_len <= 16)
            {
                maid_entry_num = 2;
            }
            else if (maid_len <= 32)
            {
                maid_entry_num = 4;
            }
            else if (maid_len <= 48)
            {
                maid_entry_num = 6;
            }
        }
        p_sys_maid->maid_entry_num = maid_entry_num;

    }

    return p_sys_maid;
}

int32
_sys_greatbelt_oam_maid_free_node(uint8 lchip, sys_oam_maid_com_t* p_sys_maid)
{
    mem_free(p_sys_maid);
    p_sys_maid = NULL;

    return CTC_E_NONE;
}


int32
_sys_greatbelt_oam_maid_build_index(uint8 lchip, sys_oam_maid_com_t* p_sys_maid)
{
    sys_greatbelt_opf_t opf;
    uint8  maid_entry_num = 0;
    uint32 offset         = 0;

    sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));

    opf.pool_type  = OPF_OAM_MA_NAME;
    opf.pool_index = 0;

    maid_entry_num = p_sys_maid->maid_entry_num;
    CTC_ERROR_RETURN(sys_greatbelt_opf_alloc_offset(lchip, &opf, maid_entry_num, &offset));
    p_sys_maid->maid_index = offset;
    SYS_OAM_DBG_INFO("MAID: lchip->%d, index->%d\n", lchip, offset);


    return CTC_E_NONE;
}

int32
_sys_greatbelt_oam_maid_free_index(uint8 lchip, sys_oam_maid_com_t* p_sys_maid)
{
    sys_greatbelt_opf_t opf;
    uint8  maid_entry_num = 0;
    uint32 offset         = 0;

    sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));

    if (g_gb_oam_master[lchip]->no_mep_resource)
    {
        return CTC_E_NONE;
    }

    opf.pool_type  = OPF_OAM_MA_NAME;
    opf.pool_index = 0;

    maid_entry_num = p_sys_maid->maid_entry_num;
    offset = p_sys_maid->maid_index;
    CTC_ERROR_RETURN(sys_greatbelt_opf_free_offset(lchip, &opf, maid_entry_num, offset));

    return CTC_E_NONE;
}

int32
_sys_greatbelt_oam_maid_add_to_db(uint8 lchip, sys_oam_maid_com_t* p_sys_maid)
{
    ctc_hash_insert(g_gb_oam_master[lchip]->maid_hash, p_sys_maid);

    return CTC_E_NONE;
}

int32
_sys_greatbelt_oam_maid_del_from_db(uint8 lchip, sys_oam_maid_com_t* p_sys_maid)
{
    ctc_hash_remove(g_gb_oam_master[lchip]->maid_hash, p_sys_maid);

    return CTC_E_NONE;
}

int32
_sys_greatbelt_oam_maid_add_to_asic(uint8 lchip, sys_oam_maid_com_t* p_sys_maid)
{
    ds_ma_name_t ds_ma_name;
    uint32 cmd            = 0;
    uint32 ma_index       = 0;
    uint8  entry_index    = 0;
    uint8  maid_entry_num = 0;

    maid_entry_num  = p_sys_maid->maid_entry_num;
    ma_index        = p_sys_maid->maid_index;

    sal_memset(&ds_ma_name, 0, sizeof(ds_ma_name_t));
    cmd = DRV_IOW(DsMaName_t, DRV_ENTRY_FLAG);

    for (entry_index = 0; entry_index < maid_entry_num; entry_index++)
    {
        ds_ma_name.ma_id_umc0 = p_sys_maid->maid[entry_index * 8 + 0] << 24 |
            p_sys_maid->maid[entry_index * 8 + 1] << 16 |
            p_sys_maid->maid[entry_index * 8 + 2] << 8 |
            p_sys_maid->maid[entry_index * 8 + 3] << 0;

        ds_ma_name.ma_id_umc1 = p_sys_maid->maid[entry_index * 8 + 4] << 24 |
            p_sys_maid->maid[entry_index * 8 + 5] << 16 |
            p_sys_maid->maid[entry_index * 8 + 6] << 8 |
            p_sys_maid->maid[entry_index * 8 + 7] << 0;

        SYS_OAM_DBG_INFO("-----ds_ma_name(maIdx:%d, entryIdx:%d)--------\n", ma_index, entry_index);
        SYS_OAM_DBG_INFO("ds_ma_name.ma_id_umc0:0x%x\n", ds_ma_name.ma_id_umc0);
        SYS_OAM_DBG_INFO("ds_ma_name.ma_id_umc1:0x%x\n", ds_ma_name.ma_id_umc1);

        CTC_ERROR_RETURN(DRV_IOCTL(lchip, ma_index + entry_index, cmd, &ds_ma_name));

    }

    return CTC_E_NONE;
}

int32
_sys_greatbelt_oam_maid_del_from_asic(uint8 lchip, sys_oam_maid_com_t* p_sys_maid)
{
    ds_ma_name_t ds_ma_name;
    uint32 cmd              = 0;
    uint32 ma_index         = 0;
    uint8  entry_index      = 0;
    uint8  maid_entry_num   = 0;

    maid_entry_num  = p_sys_maid->maid_entry_num;
    ma_index        = p_sys_maid->maid_index;

    sal_memset(&ds_ma_name, 0, sizeof(ds_ma_name_t));
    cmd = DRV_IOW(DsMaName_t, DRV_ENTRY_FLAG);

    for (entry_index = 0; entry_index < maid_entry_num; entry_index++)
    {
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, ma_index + entry_index, cmd, &ds_ma_name));
    }

    return CTC_E_NONE;
}





#define CHAN "Function Begin"

sys_oam_chan_com_t*
_sys_greatbelt_oam_chan_lkup(uint8 lchip, sys_oam_chan_com_t* p_sys_chan)
{
    ctc_hash_t* p_chan_hash = NULL;
    sys_oam_chan_com_t* p_sys_chan_db = NULL;

    p_chan_hash = g_gb_oam_master[lchip]->chan_hash;

    p_sys_chan_db = ctc_hash_lookup(p_chan_hash, p_sys_chan);

    return p_sys_chan_db;
}

int32
_sys_greatbelt_oam_chan_cmp(sys_oam_chan_com_t* p_sys_chan, sys_oam_chan_com_t* p_sys_chan_db)
{
    uint8 oam_type = 0;
    sys_oam_cmp_t oam_cmp;

    oam_type = p_sys_chan->mep_type;
    if (NULL == SYS_OAM_FUNC_TABLE(p_sys_chan->lchip, oam_type, SYS_OAM_CHAN, SYS_OAM_CMP))
    {
        return CTC_E_INVALID_PTR;
    }

    oam_cmp.p_node_parm = p_sys_chan;
    oam_cmp.p_node_db   = p_sys_chan_db;

    return (SYS_OAM_FUNC_TABLE(p_sys_chan->lchip, oam_type, SYS_OAM_CHAN, SYS_OAM_CMP))(p_sys_chan->lchip, (void*)&oam_cmp);
}

STATIC uint32
_sys_greatbelt_oam_chan_build_key(sys_oam_chan_com_t* p_sys_chan)
{

    sys_oam_chan_hash_key_t chan_hash_key;
    uint8   length = 0;

    sal_memset(&chan_hash_key, 0, sizeof(sys_oam_chan_hash_key_t));

    chan_hash_key.mep_type = p_sys_chan->mep_type;

    if ((CTC_OAM_MEP_TYPE_ETH_1AG == chan_hash_key.mep_type)
        || (CTC_OAM_MEP_TYPE_ETH_Y1731 == chan_hash_key.mep_type))
    {
        chan_hash_key.u.eth.gport   = ((sys_oam_chan_eth_t*)p_sys_chan)->key.gport;
        chan_hash_key.u.eth.vlan_id = ((sys_oam_chan_eth_t*)p_sys_chan)->key.vlan_id;
    }
    else if ((CTC_OAM_MEP_TYPE_MPLS_TP_Y1731 == chan_hash_key.mep_type)
             || (CTC_OAM_MEP_TYPE_MPLS_TP_BFD == chan_hash_key.mep_type))
    {
        chan_hash_key.u.mpls_tp.gport_l3if_id   = ((sys_oam_chan_tp_t*)p_sys_chan)->key.gport_l3if_id;
        chan_hash_key.u.mpls_tp.section_oam     = ((sys_oam_chan_tp_t*)p_sys_chan)->key.section_oam;
        chan_hash_key.u.mpls_tp.label           = ((sys_oam_chan_tp_t*)p_sys_chan)->key.label;
        chan_hash_key.u.mpls_tp.spaceid         = ((sys_oam_chan_tp_t*)p_sys_chan)->key.spaceid;
    }
    else if ((CTC_OAM_MEP_TYPE_IP_BFD == chan_hash_key.mep_type)
             || (CTC_OAM_MEP_TYPE_MPLS_BFD == chan_hash_key.mep_type))
    {
        chan_hash_key.u.bfd.my_discr   = ((sys_oam_chan_bfd_t*)p_sys_chan)->key.my_discr;
    }

    length = sizeof(sys_oam_chan_hash_key_t);

    return _sys_greatbelt_oam_build_hash_key(&chan_hash_key, length);
}

int32
_sys_greatbelt_oam_chan_build_index(uint8 lchip, sys_oam_chan_com_t* p_sys_chan)
{
    sys_greatbelt_opf_t opf;
    uint32 offset       = 0;

    sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));

    opf.pool_type  = OPF_OAM_CHAN;
    opf.pool_index = 0;

    CTC_ERROR_RETURN(sys_greatbelt_opf_alloc_offset(lchip, &opf, 1, &offset));
    p_sys_chan->chan_index = offset;
    SYS_OAM_DBG_INFO("CAHN(chan): lchip->%d, index->%d\n", lchip, offset);

    return CTC_E_NONE;
}

int32
_sys_greatbelt_oam_lm_build_index(uint8 lchip, sys_oam_chan_com_t* p_sys_chan, uint8 is_link_sec)
{
    sys_greatbelt_opf_t opf;
    uint32 offset           = 0;
    uint8  mep_type         = 0;
    uint8  lm_cos_type      = 0;
    uint16* lm_index_base   = 0;
    uint32  block_size      = 0;
    uint8   cnt             = 0;
    sys_oam_chan_eth_t* p_eth_chan = NULL;
    sys_oam_chan_tp_t* p_tp_chan  = NULL;
    ds_oam_lm_stats0_t ds_oam_lm_stats;

    mep_type = p_sys_chan->mep_type;

    if ((CTC_OAM_MEP_TYPE_ETH_1AG == mep_type) || (CTC_OAM_MEP_TYPE_ETH_Y1731 == mep_type))
    {
        p_eth_chan = (sys_oam_chan_eth_t*)p_sys_chan;
        if (is_link_sec)
        {
            lm_cos_type     = p_eth_chan->link_lm_cos_type;
            lm_index_base   = &p_sys_chan->link_lm_index_base;
        }
        else
        {
            lm_cos_type     = p_eth_chan->lm_cos_type;
            lm_index_base   = &p_sys_chan->lm_index_base;
        }

        if ((CTC_OAM_LM_COS_TYPE_ALL_COS == lm_cos_type) || (CTC_OAM_LM_COS_TYPE_SPECIFIED_COS == lm_cos_type))
        {
            block_size = 1;
        }
        else if (CTC_OAM_LM_COS_TYPE_PER_COS == lm_cos_type)
        {
            block_size = 8;
        }

        if (!is_link_sec)
        {
            block_size = block_size * 3;
        }
    }
    else if ((CTC_OAM_MEP_TYPE_MPLS_TP_Y1731 == mep_type) || (CTC_OAM_MEP_TYPE_MPLS_TP_BFD == mep_type))
    {
        p_tp_chan = (sys_oam_chan_tp_t*)p_sys_chan;

        lm_cos_type     = p_tp_chan->lm_cos_type;
        lm_index_base   = &p_sys_chan->lm_index_base;

        if ((CTC_OAM_LM_COS_TYPE_ALL_COS == lm_cos_type) || (CTC_OAM_LM_COS_TYPE_SPECIFIED_COS == lm_cos_type))
        {
            block_size = 1;
        }
        else if (CTC_OAM_LM_COS_TYPE_PER_COS == lm_cos_type)
        {
            block_size = 8;
        }
    }

    sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));

    opf.pool_type  = OPF_OAM_LM;
    opf.pool_index = 0;

    CTC_ERROR_RETURN(sys_greatbelt_opf_alloc_offset(lchip, &opf, block_size, &offset));
    if (lm_index_base)
    {
        *lm_index_base = offset;
    }

    SYS_OAM_DBG_INFO("LM(LM): lchip->%d, index->%d\n", lchip, offset);

    for (cnt = 0; cnt < block_size; cnt++)
    {
        sal_memset(&ds_oam_lm_stats, 0, sizeof(ds_oam_lm_stats0_t));
        SYS_OAM_LM_CMD_WRITE(lchip, (offset + cnt), ds_oam_lm_stats);
    }


    return CTC_E_NONE;
}

int32
_sys_greatbelt_oam_lm_free_index(uint8 lchip, sys_oam_chan_com_t* p_sys_chan, uint8 is_link_sec)
{
    sys_greatbelt_opf_t opf;
    uint16 *offset          = NULL;
    uint8  mep_type         = 0;
    uint8  lm_cos_type      = 0;
    uint32  block_size      = 0;
    sys_oam_chan_eth_t* p_eth_chan = NULL;
    sys_oam_chan_tp_t* p_tp_chan  = NULL;

    sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));

    mep_type = p_sys_chan->mep_type;

    if ((CTC_OAM_MEP_TYPE_ETH_1AG == mep_type) || (CTC_OAM_MEP_TYPE_ETH_Y1731 == mep_type))
    {
        p_eth_chan = (sys_oam_chan_eth_t*)p_sys_chan;
        if (is_link_sec)
        {
            lm_cos_type     = p_eth_chan->link_lm_cos_type;
            offset          = &p_sys_chan->link_lm_index_base;
        }
        else
        {
            lm_cos_type     = p_eth_chan->lm_cos_type;
            offset          = &p_sys_chan->lm_index_base;
        }

        if ((CTC_OAM_LM_COS_TYPE_ALL_COS == lm_cos_type) || (CTC_OAM_LM_COS_TYPE_SPECIFIED_COS == lm_cos_type))
        {
            block_size = 1;
        }
        else if (CTC_OAM_LM_COS_TYPE_PER_COS == lm_cos_type)
        {
            block_size = 8;
        }

        if (!is_link_sec)
        {
            block_size = block_size * 3;
        }
    }
    else if ((CTC_OAM_MEP_TYPE_MPLS_TP_Y1731 == mep_type) || (CTC_OAM_MEP_TYPE_MPLS_TP_BFD == mep_type))
    {
        p_tp_chan = (sys_oam_chan_tp_t*)p_sys_chan;

        lm_cos_type     = p_tp_chan->lm_cos_type;
        offset          = &p_sys_chan->lm_index_base;

        if ((CTC_OAM_LM_COS_TYPE_ALL_COS == lm_cos_type) || (CTC_OAM_LM_COS_TYPE_SPECIFIED_COS == lm_cos_type))
        {
            block_size = 1;
        }
        else if (CTC_OAM_LM_COS_TYPE_PER_COS == lm_cos_type)
        {
            block_size = 8;
        }
    }

    opf.pool_type  = OPF_OAM_LM;
    opf.pool_index = 0;
    if (offset)
    {
        CTC_ERROR_RETURN(sys_greatbelt_opf_free_offset(lchip, &opf, block_size, (*offset)));
        *offset = 0;
    }
    return CTC_E_NONE;
}

int32
_sys_greatbelt_oam_chan_free_index(uint8 lchip, sys_oam_chan_com_t* p_sys_chan)
{
    sys_greatbelt_opf_t opf;
    uint32 offset       = 0;

    sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));

    opf.pool_type  = OPF_OAM_CHAN;
    opf.pool_index = 0;
    offset = p_sys_chan->chan_index;

    CTC_ERROR_RETURN(sys_greatbelt_opf_free_offset(lchip, &opf, 1, offset));

    return CTC_E_NONE;
}

int32
_sys_greatbelt_oam_chan_add_to_db(uint8 lchip, sys_oam_chan_com_t* p_sys_chan)
{
    ctc_hash_insert(g_gb_oam_master[lchip]->chan_hash, p_sys_chan);

    return CTC_E_NONE;
}

int32
_sys_greatbelt_oam_chan_del_from_db(uint8 lchip, sys_oam_chan_com_t* p_sys_chan)
{
    ctc_hash_remove(g_gb_oam_master[lchip]->chan_hash, p_sys_chan);

    return CTC_E_NONE;
}

#define LMEP "Function Begin"

sys_oam_lmep_com_t*
_sys_greatbelt_oam_lmep_lkup(uint8 lchip, sys_oam_chan_com_t* p_sys_chan, sys_oam_lmep_com_t* p_sys_lmep)
{
    sys_oam_lmep_com_t* p_sys_lmep_db = NULL;
    ctc_slistnode_t* ctc_slistnode = NULL;
    ctc_slist_t* p_lmep_list = NULL;
    uint8 oam_type = 0;
    uint8 cmp_result = 0;
    sys_oam_cmp_t oam_cmp;

    oam_type = p_sys_chan->mep_type;

    if (NULL == SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_LMEP, SYS_OAM_CMP))
    {
        return NULL;
    }

    p_lmep_list = p_sys_chan->lmep_list;

    CTC_SLIST_LOOP(p_lmep_list, ctc_slistnode)
    {
        p_sys_lmep_db = _ctc_container_of(ctc_slistnode, sys_oam_lmep_com_t, head);
        oam_cmp.p_node_parm = p_sys_lmep;
        oam_cmp.p_node_db   = p_sys_lmep_db;
        cmp_result = (SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_LMEP, SYS_OAM_CMP))(lchip, (void*)&oam_cmp);
        if (TRUE == cmp_result)
        {
            return p_sys_lmep_db;
        }
    }

    return NULL;
}

int32
_sys_greatbelt_oam_lmep_build_index(uint8 lchip, sys_oam_lmep_com_t* p_sys_lmep)
{
    sys_greatbelt_opf_t opf;
    uint32 offset       = 0;
    uint32 block_size   = 0;
    int32 ret           = CTC_E_NONE;

    if (p_sys_lmep->mep_on_cpu)
    {
        p_sys_lmep->lmep_index = 0x1FFF;
        SYS_OAM_DBG_INFO("LMEP: lchip->%d, index->%d\n", lchip, offset);
        return CTC_E_NONE;
    }


    sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));

    if (P_COMMON_OAM_MASTER_GLB(lchip).mep_index_alloc_by_sdk)
    {
        if (g_gb_oam_master[lchip]->no_mep_resource)
        {
            return CTC_E_INVALID_CONFIG;
        }

        p_sys_lmep->lchip = lchip;

        opf.pool_index = 0;
        opf.pool_type  = OPF_OAM_MA;
        CTC_ERROR_RETURN(sys_greatbelt_opf_alloc_offset(lchip, &opf, 1, &offset));
        p_sys_lmep->ma_index = offset;

        SYS_OAM_DBG_INFO("MA  : lchip->%d, index->%d\n", lchip, offset);

        block_size = 2;
        if ((CTC_OAM_MEP_TYPE_MPLS_BFD == p_sys_lmep->p_sys_chan->mep_type)
            || (CTC_OAM_MEP_TYPE_IP_BFD == p_sys_lmep->p_sys_chan->mep_type)
            || ((CTC_OAM_MEP_TYPE_MPLS_TP_BFD == p_sys_lmep->p_sys_chan->mep_type) && (!P_COMMON_OAM_MASTER_GLB(lchip).tp_bfd_333ms)) )
        {
            opf.pool_type  = OPF_OAM_MEP_BFD;
        }
        else
        {
            if (((CTC_OAM_MEP_TYPE_ETH_1AG == p_sys_lmep->p_sys_chan->mep_type)
                || (CTC_OAM_MEP_TYPE_ETH_Y1731 == p_sys_lmep->p_sys_chan->mep_type))
                && (!CTC_FLAG_ISSET(((sys_oam_lmep_y1731_t*)p_sys_lmep)->flag, CTC_OAM_Y1731_LMEP_FLAG_P2P_MODE)))
            {
                block_size = 1;
            }
            opf.pool_type  = OPF_OAM_MEP_LMEP;
        }

        ret = sys_greatbelt_opf_alloc_offset(lchip, &opf, block_size, &offset);
        if(CTC_E_NONE != ret)
        {
            opf.pool_type  = OPF_OAM_MA;
            offset         = p_sys_lmep->ma_index;
            p_sys_lmep->ma_index = 0;
            sys_greatbelt_opf_free_offset(lchip, &opf, 1, offset);
            return ret;
        }
        p_sys_lmep->lmep_index = offset;

    }
    else
    {
        if (g_gb_oam_master[lchip]->no_mep_resource)
        {
            p_sys_lmep->ma_index = 0;
            return CTC_E_NONE;
        }

        opf.pool_type  = OPF_OAM_MA;
        opf.pool_index = 0;
        CTC_ERROR_RETURN(sys_greatbelt_opf_alloc_offset(lchip, &opf, 1, &offset));
        p_sys_lmep->ma_index = offset;

        offset = p_sys_lmep->lmep_index;
    }

    SYS_OAM_DBG_INFO("LMEP: lchip->%d, index->%d\n", lchip, offset);

    return ret;
}

int32
_sys_greatbelt_oam_lmep_free_index(uint8 lchip, sys_oam_lmep_com_t* p_sys_lmep)
{
    sys_greatbelt_opf_t opf;
    uint32 offset       = 0;
    uint32 block_size   = 0;

    if (p_sys_lmep->mep_on_cpu)
    {
        p_sys_lmep->lmep_index = 0;
        return CTC_E_NONE;
    }

    sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));

    if (P_COMMON_OAM_MASTER_GLB(lchip).mep_index_alloc_by_sdk)
    {
        if (g_gb_oam_master[lchip]->no_mep_resource)
        {
            return CTC_E_INVALID_CONFIG;
        }

        lchip = p_sys_lmep->lchip;
        opf.pool_index = 0;
        offset = p_sys_lmep->ma_index;
        opf.pool_type  = OPF_OAM_MA;
        CTC_ERROR_RETURN(sys_greatbelt_opf_free_offset(lchip, &opf, 1, offset));

        offset      = p_sys_lmep->lmep_index;
        block_size  = 2;

        if ((CTC_OAM_MEP_TYPE_MPLS_BFD == p_sys_lmep->p_sys_chan->mep_type)
            || (CTC_OAM_MEP_TYPE_IP_BFD == p_sys_lmep->p_sys_chan->mep_type)
            || ((CTC_OAM_MEP_TYPE_MPLS_TP_BFD == p_sys_lmep->p_sys_chan->mep_type) && (!P_COMMON_OAM_MASTER_GLB(lchip).tp_bfd_333ms)))
        {
            opf.pool_type  = OPF_OAM_MEP_BFD;
        }
        else
        {
            if(((CTC_OAM_MEP_TYPE_ETH_1AG == p_sys_lmep->p_sys_chan->mep_type)
                || (CTC_OAM_MEP_TYPE_ETH_Y1731 == p_sys_lmep->p_sys_chan->mep_type))
                && (!CTC_FLAG_ISSET(((sys_oam_lmep_y1731_t*)p_sys_lmep)->flag, CTC_OAM_Y1731_LMEP_FLAG_P2P_MODE)))
            {
                block_size = 1;
            }
            opf.pool_type  = OPF_OAM_MEP_LMEP;
        }
        CTC_ERROR_RETURN(sys_greatbelt_opf_free_offset(lchip, &opf, block_size, offset));
    }
    else
    {
        if (g_gb_oam_master[lchip]->no_mep_resource)
        {
            return CTC_E_NONE;
        }

        opf.pool_type  = OPF_OAM_MA;
        opf.pool_index = 0;
        offset = p_sys_lmep->ma_index;
        CTC_ERROR_RETURN(sys_greatbelt_opf_free_offset(lchip, &opf, 1, offset));
    }

    return CTC_E_NONE;

}

int32
_sys_greatbelt_oam_lmep_add_to_db(uint8 lchip, sys_oam_lmep_com_t* p_sys_lmep)
{
    uint32 mep_index    = 0;
    sys_oam_chan_com_t* p_sys_chan = NULL;

    mep_index = p_sys_lmep->lmep_index;
    p_sys_chan = p_sys_lmep->p_sys_chan;

    if (NULL == p_sys_chan->lmep_list)
    {
        p_sys_chan->lmep_list = ctc_slist_new();
        if (NULL == p_sys_chan->lmep_list)
        {
            return CTC_E_NO_MEMORY;
        }
    }

    ctc_slist_add_tail(p_sys_chan->lmep_list, &(p_sys_lmep->head));
    if(!p_sys_lmep->mep_on_cpu)
    {
        ctc_vector_add(g_gb_oam_master[lchip]->mep_vec, mep_index, p_sys_lmep);
    }

    return CTC_E_NONE;
}

int32
_sys_greatbelt_oam_lmep_del_from_db(uint8 lchip, sys_oam_lmep_com_t* p_sys_lmep)
{

    uint32 mep_index = 0;
    sys_oam_chan_com_t* p_sys_chan = NULL;

    mep_index = p_sys_lmep->lmep_index;
    p_sys_chan = p_sys_lmep->p_sys_chan;

    if (NULL == p_sys_chan->lmep_list)
    {
        return CTC_E_OAM_CHAN_LMEP_NOT_FOUND;
    }

    ctc_slist_delete_node(p_sys_chan->lmep_list, &(p_sys_lmep->head));

    if (0 == CTC_SLISTCOUNT(p_sys_chan->lmep_list))
    {
        ctc_slist_free(p_sys_chan->lmep_list);
        p_sys_chan->lmep_list = NULL;
    }

    if(!p_sys_lmep->mep_on_cpu)
    {
        ctc_vector_del(g_gb_oam_master[lchip]->mep_vec, mep_index);
    }

    return CTC_E_NONE;
}

int32
_sys_greatbelt_oam_lmep_add_eth_to_asic(uint8 lchip, sys_oam_lmep_com_t* p_sys_lmep)
{
    sys_oam_chan_eth_t* p_sys_chan_eth = NULL;
    sys_oam_lmep_y1731_t* p_sys_eth_lmep = NULL;
    sys_oam_maid_com_t* p_sys_eth_maid = NULL;
    uint32 cmd        = 0;
    uint32 index      = 0;
    uint32 lmep_flag  = 0;
    uint32 gport      = 0;
    uint8  dest_chip  = 0;
    uint8  dest_port  = 0;
    uint8 maid_length_type = 0;
    uint16 src_vlan_ptr    = 0;
    ds_ma_t ds_ma;
    ds_eth_mep_t ds_eth_mep;
    ds_eth_mep_t ds_eth_mep_mask;
    tbl_entry_t tbl_entry;

    p_sys_eth_lmep = (sys_oam_lmep_y1731_t*)p_sys_lmep;
    p_sys_chan_eth = (sys_oam_chan_eth_t*)p_sys_lmep->p_sys_chan;
    p_sys_eth_maid = p_sys_lmep->p_sys_maid;

    if (SYS_OAM_NO_MEP_OPERATION(lchip, p_sys_lmep->p_sys_chan->master_chipid))
    {
        goto RETURN;
    }

    lmep_flag = p_sys_eth_lmep->flag;
    gport = p_sys_chan_eth->key.gport;

    if (0 == P_COMMON_OAM_MASTER_GLB(lchip).maid_len_format)
    {
        maid_length_type = (p_sys_eth_maid->maid_entry_num >> 1);
    }
    else
    {
        maid_length_type = P_COMMON_OAM_MASTER_GLB(lchip).maid_len_format;
    }

    dest_chip = CTC_MAP_GPORT_TO_GCHIP(gport);
    dest_port = CTC_MAP_GPORT_TO_LPORT(gport);

    sal_memset(&ds_ma, 0, sizeof(ds_ma_t));
    sal_memset(&ds_eth_mep, 0, sizeof(ds_eth_mep_t));
    sal_memset(&ds_eth_mep_mask, 0, sizeof(ds_eth_mep_t));

    /* set ma table entry */
    ds_ma.tx_untagged_oam         = p_sys_chan_eth->key.link_oam;
    ds_ma.ma_id_length_type       = maid_length_type;
    ds_ma.ccm_interval            = p_sys_eth_lmep->ccm_interval;
    ds_ma.mpls_label_valid        = 0;
    ds_ma.rx_oam_type             = 1;

    ds_ma.tx_with_port_status     = CTC_FLAG_ISSET(lmep_flag, CTC_OAM_Y1731_LMEP_FLAG_TX_PORT_STATUS);
    ds_ma.tx_with_send_id         = CTC_FLAG_ISSET(lmep_flag, CTC_OAM_Y1731_LMEP_FLAG_TX_SEND_ID);
    ds_ma.mpls_ttl                = 0;
    ds_ma.ma_name_index           = p_sys_eth_maid->maid_index;
    ds_ma.priority_index          = p_sys_eth_lmep->tx_cos_exp;

    ds_ma.sf_fail_while_cfg_type  = 0;
    ds_ma.aps_en                  = CTC_FLAG_ISSET(lmep_flag, CTC_OAM_Y1731_LMEP_FLAG_APS_EN);
    ds_ma.aps_signal_fail_local   = 0;
    ds_ma.ma_name_len             = p_sys_eth_maid->maid_len;
    ds_ma.md_lvl                  = p_sys_eth_lmep->md_level;
    ds_ma.tx_with_if_status       = CTC_FLAG_ISSET(lmep_flag, CTC_OAM_Y1731_LMEP_FLAG_TX_IF_STATUS);

    if (p_sys_chan_eth->key.link_oam)
    {
        uint32 default_vlan;
        CTC_ERROR_RETURN(sys_greatbelt_port_get_property(lchip, gport, CTC_PORT_PROP_DEFAULT_VLAN, &default_vlan));
        CTC_ERROR_RETURN(sys_greatbelt_vlan_get_vlan_ptr(lchip, default_vlan, &src_vlan_ptr));
        ds_ma.linkoam = 1;
    }
    else
    {
        src_vlan_ptr = p_sys_chan_eth->key.vlan_id;
    }

    if (p_sys_eth_lmep->is_up) /*up mep*/
    {
        if (p_sys_chan_eth->key.use_fid)
        {
            ds_ma.use_vrfid_lkup         = 1;
            ds_ma.next_hop_ptr           = p_sys_chan_eth->key.vlan_id; /*fid*/
        }
        else
        {
            ds_ma.use_vrfid_lkup         = 0;
            ds_ma.next_hop_ptr           = src_vlan_ptr;  /*vlan ptr*/
        }
    }
    else /*down mep*/
    {
        ds_ma.next_hop_ptr                = src_vlan_ptr; /*vlan ptr*/
    }

    ds_eth_mep.dest_chip                 = dest_chip;
    ds_eth_mep.dest_id                   = dest_port;

    /* set mep table entry */
    ds_eth_mep.active                    = CTC_FLAG_ISSET(lmep_flag, CTC_OAM_Y1731_LMEP_FLAG_MEP_EN);
    ds_eth_mep.is_up                     = p_sys_eth_lmep->is_up;
    ds_eth_mep.eth_oam_p2_p_mode         = CTC_FLAG_ISSET(lmep_flag, CTC_OAM_Y1731_LMEP_FLAG_P2P_MODE);
    ds_eth_mep.ma_id_check_disable       = 0;
    ds_eth_mep.cci_en                    = p_sys_eth_lmep->ccm_en;
    ds_eth_mep.enable_pm                 = CTC_FLAG_ISSET(lmep_flag, CTC_OAM_Y1731_LMEP_FLAG_DM_EN);
    ds_eth_mep.present_traffic_check_en  = 0;
    ds_eth_mep.seq_num_en                = CTC_FLAG_ISSET(lmep_flag, CTC_OAM_Y1731_LMEP_FLAG_CCM_SEQ_NUM_EN);
    ds_eth_mep.share_mac_en              = p_sys_eth_lmep->share_mac;

    ds_eth_mep.mep_id                    = p_sys_eth_lmep->mep_id;
    ds_eth_mep.tpid_type                 = p_sys_eth_lmep->tpid_type;
    ds_eth_mep.mep_primary_vid           = p_sys_chan_eth->key.vlan_id;
    ds_eth_mep.ma_index                  = p_sys_lmep->ma_index;

    ds_eth_mep.is_remote                 = 0;
    ds_eth_mep.cci_while                 = 4;
    ds_eth_mep.d_unexp_mep_timer         = 14;
    ds_eth_mep.d_mismerge_timer          = 14;
    ds_eth_mep.d_meg_lvl_timer           = 14;
    ds_eth_mep.is_bfd                    = 0;
    ds_eth_mep.mep_type                  = 0;
    ds_eth_mep.port_status               = 0;
    ds_eth_mep.present_traffic           = 0;
    ds_eth_mep.present_rdi               = 0;
    ds_eth_mep.ccm_seq_num0              = 0;
    ds_eth_mep.ccm_seq_num1              = 0;
    ds_eth_mep.ccm_seq_num2              = 0;
    ds_eth_mep.d_unexp_mep               = 0;
    ds_eth_mep.d_mismerge                = 0;
    ds_eth_mep.d_meg_lvl                 = 0;

    SYS_OAM_DBG_INFO("LMEP: ds_eth_mep.active            = %d\n", ds_eth_mep.active);
    SYS_OAM_DBG_INFO("LMEP: ds_eth_mep.is_up             = %d\n", ds_eth_mep.is_up);
    SYS_OAM_DBG_INFO("LMEP: ds_eth_mep.eth_oam_p2_p_mode = %d\n", ds_eth_mep.eth_oam_p2_p_mode);
    SYS_OAM_DBG_INFO("LMEP: ds_eth_mep.mep_id            = %d\n", ds_eth_mep.mep_id);
    SYS_OAM_DBG_INFO("LMEP: ds_eth_mep.mep_primary_vid   = %d\n", ds_eth_mep.mep_primary_vid);
    SYS_OAM_DBG_INFO("LMEP: ds_eth_mep.ma_index          = %d\n", ds_eth_mep.ma_index);
    SYS_OAM_DBG_INFO("LMEP: ds_eth_mep.is_remote         = %d\n", ds_eth_mep.is_remote);
    SYS_OAM_DBG_INFO("LMEP: ds_eth_mep.tpid_type         = %d\n", ds_eth_mep.tpid_type);

    index = p_sys_lmep->ma_index;
    cmd = DRV_IOW(DsMa_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_ma));

    index = p_sys_lmep->lmep_index;
    cmd = DRV_IOW(DsEthMep_t, DRV_ENTRY_FLAG);
     /*CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_eth_mep));*/
    tbl_entry.data_entry = (uint32*)&ds_eth_mep;
    tbl_entry.mask_entry = (uint32*)&ds_eth_mep_mask;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &tbl_entry));

RETURN:

    return CTC_E_NONE;
}

int32
_sys_greatbelt_oam_lmep_add_tp_y1731_to_asic(uint8 lchip, sys_oam_lmep_com_t* p_sys_lmep)
{
    sys_oam_chan_tp_t* p_sys_chan_tp = NULL;
    sys_oam_lmep_y1731_t* p_sys_tp_y1731_lmep = NULL;
    sys_oam_maid_com_t* p_sys_tp_y1731_maid = NULL;
    uint32 cmd        = 0;
    uint32 index      = 0;
    uint32 lmep_flag  = 0;
    uint8 maid_length_type = 0;
    ds_ma_t ds_ma;
    ds_eth_mep_t ds_eth_mep;
    ds_eth_mep_t ds_eth_mep_mask;
    tbl_entry_t tbl_entry;

    p_sys_tp_y1731_lmep = (sys_oam_lmep_y1731_t*)p_sys_lmep;
    p_sys_chan_tp       = (sys_oam_chan_tp_t*)p_sys_lmep->p_sys_chan;
    p_sys_tp_y1731_maid = (sys_oam_maid_com_t*)p_sys_lmep->p_sys_maid;

    if (SYS_OAM_NO_MEP_OPERATION(lchip, p_sys_lmep->p_sys_chan->master_chipid))
    {
        goto RETURN;
    }

    lmep_flag = p_sys_tp_y1731_lmep->flag;
    maid_length_type = (p_sys_tp_y1731_maid->maid_entry_num >> 1);

    sal_memset(&ds_ma, 0, sizeof(ds_ma_t));
    sal_memset(&ds_eth_mep, 0, sizeof(ds_eth_mep_t));
    sal_memset(&ds_eth_mep_mask, 0, sizeof(ds_eth_mep_t));

    /* set ma table entry */
    ds_ma.tx_with_port_status     = 0;
    ds_ma.tx_with_send_id         = 0;
    ds_ma.tx_with_if_status       = 0;
    ds_ma.use_vrfid_lkup          = 0;
    ds_ma.tx_untagged_oam         = CTC_FLAG_ISSET(lmep_flag, CTC_OAM_Y1731_LMEP_FLAG_WITHOUT_GAL);
    ds_ma.ma_id_length_type       = maid_length_type;
    ds_ma.ccm_interval            = p_sys_tp_y1731_lmep->ccm_interval;
    ds_ma.rx_oam_type             = 8;

    ds_ma.mpls_ttl                = p_sys_tp_y1731_lmep->mpls_ttl;
    ds_ma.ma_name_index           = p_sys_tp_y1731_maid->maid_index;
    ds_ma.priority_index          = p_sys_tp_y1731_lmep->tx_cos_exp;

    ds_ma.sf_fail_while_cfg_type  = 0;
    ds_ma.aps_en                  = CTC_FLAG_ISSET(lmep_flag, CTC_OAM_Y1731_LMEP_FLAG_APS_EN);
    ds_ma.aps_signal_fail_local   = 0;
    ds_ma.ma_name_len             = p_sys_tp_y1731_maid->maid_len;
    ds_ma.md_lvl                  = 7;

    if (p_sys_chan_tp->key.section_oam)
    {
        ds_ma.linkoam               = 1;
        ds_ma.mpls_label_valid      = 0;
    }
    else
    {
        ds_ma.linkoam               = 0;
        ds_ma.mpls_label_valid      = 0xF;
    }

    if(CTC_NH_RESERVED_NHID_FOR_DROP == p_sys_tp_y1731_lmep->nhid)
    {
        ds_ma.next_hop_ext              = 0;
        ds_ma.next_hop_ptr              = 0;
        ds_eth_mep.dest_chip            = 0;
        ds_eth_mep.dest_id              = SYS_RESERVE_PORT_ID_DROP;
        ds_eth_mep.aps_bridge_en        = 0;
    }
    else
    {
        sys_oam_nhop_info_t  mpls_nhop_info;
        sal_memset(&mpls_nhop_info, 0, sizeof(sys_oam_nhop_info_t));
        _sys_greatbelt_oam_get_nexthop_info(lchip, p_sys_tp_y1731_lmep->nhid, CTC_FLAG_ISSET(lmep_flag, CTC_OAM_Y1731_LMEP_FLAG_PROTECTION_PATH), &mpls_nhop_info);
        ds_ma.next_hop_ext              = mpls_nhop_info.nexthop_is_8w;
        ds_ma.next_hop_ptr              = mpls_nhop_info.dsnh_offset;
        ds_eth_mep.dest_chip            = mpls_nhop_info.dest_chipid;
        ds_eth_mep.dest_id              = mpls_nhop_info.dest_id;
        ds_eth_mep.aps_bridge_en        = mpls_nhop_info.aps_bridge_en;
        ds_eth_mep.tx_mcast_en          = ((SYS_NH_TYPE_MCAST == mpls_nhop_info.nh_entry_type)? 1:0);
    }


    /* set mep table entry */
    ds_eth_mep.active                    = CTC_FLAG_ISSET(lmep_flag, CTC_OAM_Y1731_LMEP_FLAG_MEP_EN);
    ds_eth_mep.is_up                     = 0;
    ds_eth_mep.eth_oam_p2_p_mode         = 1;
    ds_eth_mep.ma_id_check_disable       = 0;
    ds_eth_mep.cci_en                    = p_sys_tp_y1731_lmep->ccm_en;
    ds_eth_mep.enable_pm                 = CTC_FLAG_ISSET(lmep_flag, CTC_OAM_Y1731_LMEP_FLAG_DM_EN);
    ds_eth_mep.present_traffic_check_en  = 0;
    ds_eth_mep.seq_num_en                = 0;
    ds_eth_mep.share_mac_en              = 0;

    ds_eth_mep.mep_id                    = p_sys_tp_y1731_lmep->mep_id;
    ds_eth_mep.tpid_type                 = 0;
    ds_eth_mep.mep_primary_vid           = 0;
    ds_eth_mep.ma_index                  = p_sys_lmep->ma_index;

    ds_eth_mep.is_remote                 = 0;
    ds_eth_mep.cci_while                 = 4;
    ds_eth_mep.d_unexp_mep_timer         = 14;
    ds_eth_mep.d_mismerge_timer          = 14;
    ds_eth_mep.d_meg_lvl_timer           = 14;
    ds_eth_mep.is_bfd                    = 0;
    ds_eth_mep.mep_type                  = 6;
    ds_eth_mep.port_status               = 0;
    ds_eth_mep.present_traffic           = 0;
    ds_eth_mep.present_rdi               = 0;
    ds_eth_mep.ccm_seq_num0              = 0;
    ds_eth_mep.ccm_seq_num1              = 0;
    ds_eth_mep.ccm_seq_num2              = 0;
    ds_eth_mep.d_unexp_mep               = 0;
    ds_eth_mep.d_mismerge                = 0;
    ds_eth_mep.d_meg_lvl                 = 0;

    SYS_OAM_DBG_INFO("LMEP: ds_eth_mep.active            = %d\n", ds_eth_mep.active);
    SYS_OAM_DBG_INFO("LMEP: ds_eth_mep.is_up             = %d\n", ds_eth_mep.is_up);
    SYS_OAM_DBG_INFO("LMEP: ds_eth_mep.eth_oam_p2_p_mode = %d\n", ds_eth_mep.eth_oam_p2_p_mode);
    SYS_OAM_DBG_INFO("LMEP: ds_eth_mep.mep_id            = %d\n", ds_eth_mep.mep_id);
    SYS_OAM_DBG_INFO("LMEP: ds_eth_mep.mep_primary_vid   = %d\n", ds_eth_mep.mep_primary_vid);
    SYS_OAM_DBG_INFO("LMEP: ds_eth_mep.ma_index          = %d\n", ds_eth_mep.ma_index);
    SYS_OAM_DBG_INFO("LMEP: ds_eth_mep.is_remote         = %d\n", ds_eth_mep.is_remote);
    SYS_OAM_DBG_INFO("LMEP: ds_eth_mep.tpid_type         = %d\n", ds_eth_mep.tpid_type);

    index = p_sys_lmep->ma_index;
    cmd = DRV_IOW(DsMa_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_ma));

    index = p_sys_lmep->lmep_index;
    cmd = DRV_IOW(DsEthMep_t, DRV_ENTRY_FLAG);
     /*CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_eth_mep));*/
    tbl_entry.data_entry = (uint32*)&ds_eth_mep;
    tbl_entry.mask_entry = (uint32*)&ds_eth_mep_mask;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &tbl_entry));
RETURN:

    return CTC_E_NONE;
}




int32
_sys_greatbelt_oam_lmep_del_from_asic(uint8 lchip, sys_oam_lmep_com_t* p_sys_lmep)
{
    uint32 cmd          = 0;
    uint32 index        = 0;
    ds_ma_t ds_ma;                    /*DS_MA */
    ds_eth_mep_t ds_eth_mep;          /* DS_ETH_MEP */
    ds_eth_mep_t ds_eth_mep_mask;
    tbl_entry_t tbl_entry;

    if (SYS_OAM_NO_MEP_OPERATION(lchip, p_sys_lmep->p_sys_chan->master_chipid))
    {
        goto RETURN;
    }

    sal_memset(&ds_ma, 0, sizeof(ds_ma_t));
    sal_memset(&ds_eth_mep, 0, sizeof(ds_eth_mep));
    sal_memset(&ds_eth_mep_mask, 0, sizeof(ds_eth_mep));

    index = p_sys_lmep->ma_index;
    cmd = DRV_IOW(DsMa_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_ma));

    index = p_sys_lmep->lmep_index;
    cmd = DRV_IOW(DsEthMep_t, DRV_ENTRY_FLAG);
     /*CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_eth_mep));*/
    tbl_entry.data_entry = (uint32*)&ds_eth_mep;
    tbl_entry.mask_entry = (uint32*)&ds_eth_mep_mask;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &tbl_entry));

RETURN:

    return CTC_E_NONE;
}






int32
_sys_greatbelt_oam_lmep_check_rmep_dloc(uint8 lchip, sys_oam_lmep_com_t* p_sys_lmep)
{
    ctc_slistnode_t* ctc_slistnode = NULL;
    ctc_slist_t* p_rmep_list = NULL;
    sys_oam_rmep_com_t* p_sys_rmep = NULL;
    uint32 cmd = 0;
    uint32 r_index = 0;
    uint32 l_index = 0;
    oam_rx_proc_ether_ctl_t rx_ether_ctl;
    uint32 dloc_rdi = 0;
    uint8 idx = 0;
    uint8 defect_type       = 0;
    uint8 defect_sub_type   = 0;
    uint8 rdi_index         = 0;
    uint32 defect = 0;
    uint8 defect_check[CTC_GREATBELT_OAM_DEFECT_NUM] = {0};
    ds_eth_mep_t    ds_eth_mep;
    ds_eth_rmep_t   ds_eth_rmep;

    p_rmep_list = p_sys_lmep->rmep_list;
    l_index = p_sys_lmep->lmep_index;

    if (SYS_OAM_NO_MEP_OPERATION(lchip, p_sys_lmep->p_sys_chan->master_chipid))
    {
        goto RETURN;
    }

    /*1.Get defect to RDI bitmap*/
    cmd = DRV_IOR(OamUpdateCtl_t, OamUpdateCtl_GenRdiByDloc_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &dloc_rdi));

    sal_memset(&rx_ether_ctl,   0, sizeof(oam_rx_proc_ether_ctl_t));
    cmd = DRV_IOR(OamRxProcEtherCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &rx_ether_ctl));

    /*2.According bitmap to get defect type*/
    for (idx = 0; idx < CTC_GREATBELT_OAM_DEFECT_NUM; idx++)
    {
        defect = (1 << idx);
        _sys_greatbelt_oam_get_rdi_defect_type(lchip, (1 << idx), &defect_type, &defect_sub_type);

        switch (defect)
        {
        case CTC_OAM_DEFECT_DLOC:
            defect_check[idx] = dloc_rdi;
            break;

        case CTC_OAM_DEFECT_MISMERGE:
        case CTC_OAM_DEFECT_UNEXPECTED_LEVEL:
        case CTC_OAM_DEFECT_UNEXPECTED_MEP:
        case CTC_OAM_DEFECT_UNEXPECTED_PERIOD:
        case CTC_OAM_DEFECT_MAC_STATUS_CHANGE:
        case CTC_OAM_DEFECT_SRC_MAC_MISMATCH:
            {
                rdi_index = (defect_type << 3) | defect_sub_type;

                if (rdi_index < 32)
                {
                    defect_check[idx] = CTC_IS_BIT_SET(rx_ether_ctl.ether_defect_to_rdi0, rdi_index);
                }
                else
                {
                    defect_check[idx] = CTC_IS_BIT_SET(rx_ether_ctl.ether_defect_to_rdi1, (rdi_index - 32));
                }
            }
            break;

        default:
            defect_check[idx] = 0;
            break;
        }
    }

    /*3. check local MEP*/
    sal_memset(&ds_eth_mep,     0, sizeof(ds_eth_mep_t));
    cmd = DRV_IOR(DsEthMep_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, l_index, cmd, &ds_eth_mep));

    if ((ds_eth_mep.d_mismerge & defect_check[_sys_greatbelt_oam_cal_bitmap_bit(lchip, CTC_OAM_DEFECT_MISMERGE)])
        || (ds_eth_mep.d_meg_lvl & defect_check[_sys_greatbelt_oam_cal_bitmap_bit(lchip, CTC_OAM_DEFECT_UNEXPECTED_LEVEL)])
        || (ds_eth_mep.d_unexp_mep & defect_check[_sys_greatbelt_oam_cal_bitmap_bit(lchip, CTC_OAM_DEFECT_UNEXPECTED_MEP)]))
    {
        return CTC_E_OAM_RMEP_D_LOC_PRESENT;
    }

    /*4. check remote MEP */
    cmd = DRV_IOR(DsEthRmep_t, DRV_ENTRY_FLAG);
    CTC_SLIST_LOOP(p_rmep_list, ctc_slistnode)
    {
        p_sys_rmep = _ctc_container_of(ctc_slistnode, sys_oam_rmep_com_t, head);
        if (NULL != p_sys_rmep)
        {
            r_index = p_sys_rmep->rmep_index;
            sal_memset(&ds_eth_rmep, 0, sizeof(ds_eth_rmep_t));
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, r_index, cmd, &ds_eth_rmep));

            if ((ds_eth_rmep.d_loc & defect_check[_sys_greatbelt_oam_cal_bitmap_bit(lchip, CTC_OAM_DEFECT_DLOC)])
                || (ds_eth_rmep.d_unexp_period & defect_check[_sys_greatbelt_oam_cal_bitmap_bit(lchip, CTC_OAM_DEFECT_UNEXPECTED_PERIOD)])
                || (ds_eth_rmep.mac_status_change & defect_check[_sys_greatbelt_oam_cal_bitmap_bit(lchip, CTC_OAM_DEFECT_MAC_STATUS_CHANGE)])
                || (ds_eth_rmep.rmepmacmismatch & defect_check[_sys_greatbelt_oam_cal_bitmap_bit(lchip, CTC_OAM_DEFECT_SRC_MAC_MISMATCH)]))
            {
                return CTC_E_OAM_RMEP_D_LOC_PRESENT;
            }
        }
    }
RETURN:
    return CTC_E_NONE;
}

int32
_sys_greatbelt_oam_lmep_update_eth_asic(uint8 lchip, sys_oam_lmep_com_t* p_sys_lmep)
{
    sys_oam_lmep_y1731_t* p_sys_lmep_eth  = (sys_oam_lmep_y1731_t*)p_sys_lmep;
    uint32 cmd        = 0;
    uint32 index      = 0;
    ds_ma_t ds_ma;                    /*DS_MA */
    ds_eth_mep_t ds_eth_mep;          /* DS_ETH_MEP */
    ds_eth_rmep_t ds_eth_rmep;
    ds_eth_mep_t ds_eth_mep_mask;     /* DS_ETH_MEP */
    ds_eth_rmep_t ds_eth_rmep_mask;

    tbl_entry_t tbl_entry;

    sys_oam_rmep_com_t* p_sys_rmep = NULL;

    if (SYS_OAM_NO_MEP_OPERATION(lchip, p_sys_lmep->p_sys_chan->master_chipid))
    {
        goto RETURN;
    }

    sal_memset(&ds_eth_mep, 0, sizeof(ds_eth_mep));
    sal_memset(&ds_eth_mep_mask, 0xFF, sizeof(ds_eth_mep));
    sal_memset(&ds_eth_rmep, 0, sizeof(ds_eth_rmep_t));
    sal_memset(&ds_eth_rmep_mask, 0xFF, sizeof(ds_eth_rmep_t));
    index = p_sys_lmep->lmep_index;
    cmd = DRV_IOR(DsEthMep_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_eth_mep));

    sal_memset(&ds_ma, 0, sizeof(ds_ma_t));
    index = p_sys_lmep->ma_index;
    cmd = DRV_IOR(DsMa_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_ma));

    if (CTC_FLAG_ISSET(p_sys_lmep_eth->flag, CTC_OAM_Y1731_LMEP_FLAG_P2P_MODE)
        && ((CTC_OAM_Y1731_LMEP_UPDATE_TYPE_TX_CSF_EN == p_sys_lmep_eth->update_type)
            || (CTC_OAM_Y1731_LMEP_UPDATE_TYPE_TX_CSF_TYPE == p_sys_lmep_eth->update_type)
            || (CTC_OAM_Y1731_LMEP_UPDATE_TYPE_TX_CSF_USE_UC_DA == p_sys_lmep_eth->update_type)))
    {
        p_sys_rmep = (sys_oam_rmep_com_t*)CTC_SLISTHEAD(p_sys_lmep->rmep_list);
        if (NULL == p_sys_rmep)
        {
            return CTC_E_OAM_RMEP_NOT_FOUND;
        }

        index = p_sys_rmep->rmep_index;
        cmd = DRV_IOR(DsEthRmep_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_eth_rmep));
    }

    switch (p_sys_lmep_eth->update_type)
    {
    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_MEP_EN:

        if (CTC_FLAG_ISSET(p_sys_lmep_eth->flag, CTC_OAM_Y1731_LMEP_FLAG_MEP_EN))
        {
            ds_eth_mep.active                    = 1;
            ds_eth_mep.cci_while                 = 4;
            ds_eth_mep.d_unexp_mep_timer         = 14;
            ds_eth_mep.d_mismerge_timer          = 14;
            ds_eth_mep.d_meg_lvl_timer           = 14;
            ds_eth_mep.port_status               = 0;
            ds_eth_mep.present_traffic           = 0;
            ds_eth_mep.present_rdi               = 0;
            ds_eth_mep.ccm_seq_num0              = 0;
            ds_eth_mep.ccm_seq_num1              = 0;
            ds_eth_mep.ccm_seq_num2              = 0;
            ds_eth_mep.d_unexp_mep               = 0;
            ds_eth_mep.d_mismerge                = 0;
            ds_eth_mep.d_meg_lvl                 = 0;
        }
        else
        {
            ds_eth_mep.active                    = 0;
        }
        ds_eth_mep_mask.active                   = 0;
        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_CCM_EN:
        ds_eth_mep.cci_en                   = p_sys_lmep_eth->ccm_en;
        ds_eth_mep_mask.cci_en              = 0;
        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_CCM_SEQ_NUM_EN:
        ds_eth_mep.seq_num_en  = CTC_FLAG_ISSET(p_sys_lmep_eth->flag, CTC_OAM_Y1731_LMEP_FLAG_CCM_SEQ_NUM_EN);
        ds_eth_mep_mask.seq_num_en = 0;
        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_RDI:
        if (0 == p_sys_lmep_eth->present_rdi)
        {
            CTC_ERROR_RETURN(_sys_greatbelt_oam_lmep_check_rmep_dloc(lchip, p_sys_lmep));
        }

        ds_eth_mep.present_rdi              = p_sys_lmep_eth->present_rdi;
        ds_eth_mep_mask.present_rdi         = 0;
        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_TX_COS_EXP:
        ds_ma.priority_index                 = p_sys_lmep_eth->tx_cos_exp;
        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_PORT_STATUS:
        ds_eth_mep.port_status              = p_sys_lmep_eth->port_status;
        ds_eth_mep_mask.port_status         = 0;
        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_LMEP_SF_STATE:
        ds_ma.aps_signal_fail_local           = p_sys_lmep_eth->sf_state;
        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_TX_CSF_EN:
        p_sys_lmep_eth->tx_csf_en ? CTC_BIT_SET(ds_eth_rmep.ccm_seq_num0, 6) : CTC_BIT_UNSET(ds_eth_rmep.ccm_seq_num0, 6);
        CTC_BIT_UNSET(ds_eth_rmep_mask.ccm_seq_num0, 6);
        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_TX_CSF_TYPE:
        ds_eth_rmep.ccm_seq_num0              &= 0xFFC7;
        ds_eth_rmep.ccm_seq_num0              |= ((p_sys_lmep_eth->tx_csf_type & 0x7) << 3);
        ds_eth_rmep_mask.ccm_seq_num0         &= 0xFFC7;
        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_TX_CSF_USE_UC_DA:
        /*
        8'd0,isCsfUseUcDa,dCsf,
        rxCsfWhile[3:0],rxCsfType[2:0],
        isCsfTxEn,csfType[2:0],csfWhile[2:0]}
        */
        p_sys_lmep_eth->tx_csf_use_uc_da ?
        CTC_BIT_SET(ds_eth_rmep.ccm_seq_num0, 15) : CTC_BIT_UNSET(ds_eth_rmep.ccm_seq_num0, 15);
        CTC_BIT_UNSET(ds_eth_rmep_mask.ccm_seq_num0, 15);
        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_P2P_TX_USE_UC_DA:
        ds_eth_mep.p2p_use_uc_da            = p_sys_lmep_eth->ccm_p2p_use_uc_da;
        ds_eth_mep_mask.p2p_use_uc_da       = 0;
        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_DM_EN:
        ds_eth_mep.enable_pm            = p_sys_lmep_eth->dm_en;
        ds_eth_mep_mask.enable_pm       = 0;
        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_TRPT:
        ds_eth_mep.auto_gen_en = p_sys_lmep_eth->trpt_en;
        ds_eth_mep.auto_gen_pkt_ptr = p_sys_lmep_eth->trpt_session_id;
        ds_eth_mep_mask.auto_gen_en = 0;
        ds_eth_mep_mask.auto_gen_pkt_ptr = 0;

        if (p_sys_lmep_eth->trpt_en)
        {
            sys_greatbelt_oam_trpt_set_autogen_ptr(lchip, p_sys_lmep_eth->trpt_session_id, p_sys_lmep->lmep_index);
        }

        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    index = p_sys_lmep->ma_index;
    cmd = DRV_IOW(DsMa_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_ma));

    index = p_sys_lmep->lmep_index;
    cmd = DRV_IOW(DsEthMep_t, DRV_ENTRY_FLAG);
    tbl_entry.data_entry = (uint32*)&ds_eth_mep;
    tbl_entry.mask_entry = (uint32*)&ds_eth_mep_mask;
     /*CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_eth_mep));*/
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &tbl_entry));

    if (CTC_FLAG_ISSET(p_sys_lmep_eth->flag, CTC_OAM_Y1731_LMEP_FLAG_P2P_MODE)
        && ((CTC_OAM_Y1731_LMEP_UPDATE_TYPE_TX_CSF_EN == p_sys_lmep_eth->update_type)
            || (CTC_OAM_Y1731_LMEP_UPDATE_TYPE_TX_CSF_TYPE == p_sys_lmep_eth->update_type)
            || (CTC_OAM_Y1731_LMEP_UPDATE_TYPE_TX_CSF_USE_UC_DA == p_sys_lmep_eth->update_type)))
    {
        index = p_sys_rmep->rmep_index;
        cmd = DRV_IOW(DsEthRmep_t, DRV_ENTRY_FLAG);
        tbl_entry.data_entry = (uint32*)&ds_eth_rmep;
        tbl_entry.mask_entry = (uint32*)&ds_eth_rmep_mask;
         /*CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_eth_rmep));*/
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &tbl_entry));
    }

RETURN:

    return CTC_E_NONE;
}

int32
_sys_greatbelt_oam_lmep_update_tp_y1731_asic(uint8 lchip, sys_oam_lmep_com_t* p_sys_lmep)
{
    sys_oam_lmep_y1731_t* p_sys_lmep_tp  = (sys_oam_lmep_y1731_t*)p_sys_lmep;
    uint32 cmd        = 0;
    uint32 index      = 0;
    ds_ma_t ds_ma;                    /*DS_MA */
    ds_eth_mep_t ds_eth_mep;          /* DS_ETH_MEP */
    ds_eth_rmep_t ds_eth_rmep;
    ds_eth_mep_t ds_eth_mep_mask;     /* DS_ETH_MEP */
    ds_eth_rmep_t ds_eth_rmep_mask;
    tbl_entry_t tbl_entry;

    sys_oam_rmep_com_t* p_sys_rmep = NULL;
    sys_oam_nhop_info_t mpls_nhop_info;

    if (SYS_OAM_NO_MEP_OPERATION(lchip, p_sys_lmep->p_sys_chan->master_chipid))
    {
        goto RETURN;
    }

    sal_memset(&ds_eth_mep, 0, sizeof(ds_eth_mep_t));
    sal_memset(&ds_eth_mep_mask, 0xFF, sizeof(ds_eth_mep_t));
    index = p_sys_lmep->lmep_index;
    cmd = DRV_IOR(DsEthMep_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_eth_mep));

    sal_memset(&ds_ma, 0, sizeof(ds_ma_t));
    index = p_sys_lmep->ma_index;
    cmd = DRV_IOR(DsMa_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_ma));

    if ((CTC_OAM_Y1731_LMEP_UPDATE_TYPE_TX_CSF_EN == p_sys_lmep_tp->update_type)
        || (CTC_OAM_Y1731_LMEP_UPDATE_TYPE_TX_CSF_TYPE == p_sys_lmep_tp->update_type))
    {
        p_sys_rmep = (sys_oam_rmep_com_t*)CTC_SLISTHEAD(p_sys_lmep->rmep_list);
        if (NULL == p_sys_rmep)
        {
            return CTC_E_OAM_RMEP_NOT_FOUND;
        }

        index = p_sys_rmep->rmep_index;
        sal_memset(&ds_eth_rmep, 0, sizeof(ds_eth_rmep_t));
        sal_memset(&ds_eth_rmep_mask, 0xFF, sizeof(ds_eth_rmep_t));
        cmd = DRV_IOR(DsEthRmep_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_eth_rmep));
    }

    switch (p_sys_lmep_tp->update_type)
    {
    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_MEP_EN:

        if (CTC_FLAG_ISSET(p_sys_lmep_tp->flag, CTC_OAM_Y1731_LMEP_FLAG_MEP_EN))
        {
            ds_eth_mep.active                    = 1;
            ds_eth_mep.cci_while                 = 4;
            ds_eth_mep.d_unexp_mep_timer         = 14;
            ds_eth_mep.d_mismerge_timer          = 14;
            ds_eth_mep.d_meg_lvl_timer           = 14;
            ds_eth_mep.port_status               = 0;
            ds_eth_mep.present_traffic           = 0;
            ds_eth_mep.present_rdi               = 0;
            ds_eth_mep.ccm_seq_num0              = 0;
            ds_eth_mep.ccm_seq_num1              = 0;
            ds_eth_mep.ccm_seq_num2              = 0;
            ds_eth_mep.d_unexp_mep               = 0;
            ds_eth_mep.d_mismerge                = 0;
            ds_eth_mep.d_meg_lvl                 = 0;
        }
        else
        {
            ds_eth_mep.active                    = 0;
        }
        ds_eth_mep_mask.active                   = 0;
        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_CCM_EN:
        ds_eth_mep.cci_en                       = p_sys_lmep_tp->ccm_en;
        ds_eth_mep_mask.cci_en                  = 0;
        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_RDI:
        if (0 == p_sys_lmep_tp->present_rdi)
        {
            CTC_ERROR_RETURN(_sys_greatbelt_oam_lmep_check_rmep_dloc(lchip, p_sys_lmep));
        }

        ds_eth_mep.present_rdi                  = p_sys_lmep_tp->present_rdi;
        ds_eth_mep_mask.present_rdi             = 0;
        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_TX_COS_EXP:
        ds_ma.priority_index                 = p_sys_lmep_tp->tx_cos_exp;
        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_LMEP_SF_STATE:
        ds_ma.aps_signal_fail_local           = p_sys_lmep_tp->sf_state;
        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_TX_CSF_EN:
        p_sys_lmep_tp->tx_csf_en ? CTC_BIT_SET(ds_eth_rmep.ccm_seq_num0, 6) : CTC_BIT_UNSET(ds_eth_rmep.ccm_seq_num0, 6);
        CTC_BIT_UNSET(ds_eth_rmep_mask.ccm_seq_num0, 6);
        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_TX_CSF_TYPE:
        /*
        8'd0,isCsfUseUcDa,dCsf,
        rxCsfWhile[3:0],rxCsfType[2:0],
        isCsfTxEn,csfType[2:0],csfWhile[2:0]}
        */
        ds_eth_rmep.ccm_seq_num0              &= 0xFFC7;
        ds_eth_rmep.ccm_seq_num0              |= ((p_sys_lmep_tp->tx_csf_type & 0x7) << 3);
        ds_eth_rmep_mask.ccm_seq_num0         &= 0xFFC7;
        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_DM_EN:
        ds_eth_mep.enable_pm        = p_sys_lmep_tp->dm_en;
        ds_eth_mep_mask.enable_pm   = 0;
        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_NHOP:
        sal_memset(&mpls_nhop_info, 0, sizeof(sys_oam_nhop_info_t));
        _sys_greatbelt_oam_get_nexthop_info(lchip, p_sys_lmep_tp->nhid, CTC_FLAG_ISSET(p_sys_lmep_tp->flag, CTC_OAM_Y1731_LMEP_FLAG_PROTECTION_PATH), &mpls_nhop_info);
        ds_ma.next_hop_ext          = mpls_nhop_info.nexthop_is_8w;
        ds_ma.next_hop_ptr          = mpls_nhop_info.dsnh_offset;
        ds_eth_mep.dest_chip        = mpls_nhop_info.dest_chipid;
        ds_eth_mep.dest_id          = mpls_nhop_info.dest_id;
        ds_eth_mep.aps_bridge_en    = mpls_nhop_info.aps_bridge_en;
        ds_eth_mep.tx_mcast_en          = ((SYS_NH_TYPE_MCAST == mpls_nhop_info.nh_entry_type)? 1:0);
        ((sys_oam_chan_tp_t*)(p_sys_lmep_tp->com.p_sys_chan))->out_label = mpls_nhop_info.mpls_out_label;

        ds_eth_mep_mask.dest_chip       = 0;
        ds_eth_mep_mask.dest_id         = 0;
        ds_eth_mep_mask.aps_bridge_en   = 0;
        ds_eth_mep_mask.tx_mcast_en     = 0;
        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_MEL:
        ds_ma.md_lvl                    = p_sys_lmep_tp->md_level;
        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_TRPT:
        ds_eth_mep.auto_gen_en = p_sys_lmep_tp->trpt_en;
        ds_eth_mep.auto_gen_pkt_ptr = p_sys_lmep_tp->trpt_session_id;
        ds_eth_mep_mask.auto_gen_en = 0;
        ds_eth_mep_mask.auto_gen_pkt_ptr = 0;

        if (p_sys_lmep_tp->trpt_en)
        {
            sys_greatbelt_oam_trpt_set_autogen_ptr(lchip, p_sys_lmep_tp->trpt_session_id, p_sys_lmep->lmep_index);
        }
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    index = p_sys_lmep->ma_index;
    cmd = DRV_IOW(DsMa_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_ma));

    index = p_sys_lmep->lmep_index;
    cmd = DRV_IOW(DsEthMep_t, DRV_ENTRY_FLAG);
    tbl_entry.data_entry = (uint32*)&ds_eth_mep;
    tbl_entry.mask_entry = (uint32*)&ds_eth_mep_mask;
     /*CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_eth_mep));*/
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &tbl_entry));

    if ((CTC_OAM_Y1731_LMEP_UPDATE_TYPE_TX_CSF_EN == p_sys_lmep_tp->update_type)
        || (CTC_OAM_Y1731_LMEP_UPDATE_TYPE_TX_CSF_TYPE == p_sys_lmep_tp->update_type))
    {
        index = p_sys_rmep->rmep_index;
        cmd = DRV_IOW(DsEthRmep_t, DRV_ENTRY_FLAG);
        tbl_entry.data_entry = (uint32*)&ds_eth_rmep;
        tbl_entry.mask_entry = (uint32*)&ds_eth_rmep_mask;
         /*CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_eth_rmep));*/
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &tbl_entry));
    }

RETURN:

    return CTC_E_NONE;
}

int32
_sys_greatbelt_oam_lmep_update_master_chip(sys_oam_chan_com_t* p_sys_chan, uint32* master_chipid)
{

    uint8 old_master_chipid = 0;
    uint8 mep_type = p_sys_chan->mep_type;
    uint8 lchip = p_sys_chan->lchip;
    sys_oam_lmep_com_t* p_sys_lmep_db = NULL;
    ctc_slist_t* p_lmep_list = NULL;
    ctc_slistnode_t* ctc_slistnode = NULL;
    int32 ret = CTC_E_NONE;


    old_master_chipid = p_sys_chan->master_chipid;
    p_lmep_list = p_sys_chan->lmep_list;
    CTC_SLIST_LOOP(p_lmep_list, ctc_slistnode)
    {
        p_sys_lmep_db = _ctc_container_of(ctc_slistnode, sys_oam_lmep_com_t, head);
    }
    if (NULL == p_sys_lmep_db)
    {
        return CTC_E_INVALID_CONFIG;
    }

    p_sys_chan->master_chipid = *master_chipid;

    switch (mep_type)
    {
        case CTC_OAM_MEP_TYPE_IP_BFD:
        case CTC_OAM_MEP_TYPE_MPLS_BFD:
        case CTC_OAM_MEP_TYPE_MPLS_TP_BFD:
            CTC_ERROR_GOTO(_sys_greatbelt_bfd_update_chan(lchip, (sys_oam_lmep_bfd_t*)p_sys_lmep_db, TRUE), ret, error);
            break;
        case CTC_OAM_MEP_TYPE_MPLS_TP_Y1731:
            CTC_ERROR_GOTO(_sys_greatbelt_tp_chan_update(lchip, (sys_oam_lmep_y1731_t*)p_sys_lmep_db, TRUE), ret, error);
            break;
        case CTC_OAM_MEP_TYPE_ETH_1AG:
        case CTC_OAM_MEP_TYPE_ETH_Y1731:
            CTC_ERROR_GOTO(_sys_greatbelt_cfm_chan_update(lchip, (sys_oam_lmep_y1731_t*)p_sys_lmep_db, TRUE), ret, error);
            break;
        default:
            break;
    }

    return CTC_E_NONE;
error:
    p_sys_chan->master_chipid = old_master_chipid;
    return ret;
}

#define RMEP "Function Begin"

sys_oam_rmep_com_t*
_sys_greatbelt_oam_rmep_lkup(uint8 lchip, sys_oam_lmep_com_t* p_sys_lmep,
                             sys_oam_rmep_com_t* p_sys_rmep)
{
    ctc_slistnode_t* ctc_slistnode = NULL;
    ctc_slist_t* p_rmep_list = NULL;
    sys_oam_rmep_com_t* p_sys_rmep_db = NULL;
    uint8 oam_type = 0;
    uint8 cmp_result = 0;
    sys_oam_cmp_t oam_cmp;

    oam_type = p_sys_lmep->p_sys_chan->mep_type;

    if (NULL == SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_RMEP, SYS_OAM_CMP))
    {
        return NULL;
    }

    p_rmep_list = p_sys_lmep->rmep_list;

    CTC_SLIST_LOOP(p_rmep_list, ctc_slistnode)
    {
        p_sys_rmep_db = _ctc_container_of(ctc_slistnode, sys_oam_rmep_com_t, head);
        oam_cmp.p_node_parm = p_sys_rmep;
        oam_cmp.p_node_db   = p_sys_rmep_db;
        cmp_result = (SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_RMEP, SYS_OAM_CMP))(lchip, (void*)&oam_cmp);
        if (1 == cmp_result)
        {
            return p_sys_rmep_db;
        }
    }

    return NULL;
}

int32
_sys_greatbelt_oam_rmep_add_to_db(uint8 lchip, sys_oam_rmep_com_t* p_sys_rmep)
{
    uint32 rmep_index   = 0;
    sys_oam_lmep_com_t* p_sys_lmep = NULL;

    p_sys_lmep  = p_sys_rmep->p_sys_lmep;

    rmep_index  = p_sys_rmep->rmep_index;

    if (NULL == p_sys_lmep->rmep_list)
    {
        p_sys_lmep->rmep_list = ctc_slist_new();
        if (NULL == p_sys_lmep->rmep_list)
        {
            return CTC_E_NO_MEMORY;
        }
    }

    ctc_slist_add_tail(p_sys_lmep->rmep_list, &(p_sys_rmep->head));
    ctc_vector_add(g_gb_oam_master[lchip]->mep_vec, rmep_index, p_sys_rmep);


    return CTC_E_NONE;
}

int32
_sys_greatbelt_oam_rmep_del_from_db(uint8 lchip, sys_oam_rmep_com_t* p_sys_rmep)
{
    uint32 rmep_index = 0;
    sys_oam_lmep_com_t* p_sys_lmep = NULL;

    p_sys_lmep  = p_sys_rmep->p_sys_lmep;
    rmep_index  = p_sys_rmep->rmep_index;

    if (NULL == p_sys_lmep->rmep_list)
    {
        return CTC_E_OAM_CHAN_LMEP_NOT_FOUND;
    }

    ctc_slist_delete_node(p_sys_lmep->rmep_list, &(p_sys_rmep->head));

    if (0 == CTC_SLISTCOUNT(p_sys_lmep->rmep_list))
    {
        ctc_slist_free(p_sys_lmep->rmep_list);
        p_sys_lmep->rmep_list = NULL;
    }

    ctc_vector_del(g_gb_oam_master[lchip]->mep_vec, rmep_index);

    return CTC_E_NONE;
}

int32
_sys_greatbelt_oam_rmep_build_index(uint8 lchip, sys_oam_rmep_com_t* p_sys_rmep)
{
    sys_oam_lmep_com_t* p_sys_lmep = NULL;
    sys_greatbelt_opf_t opf;
    uint32 offset = 0;

    p_sys_lmep = p_sys_rmep->p_sys_lmep;

    sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));
    if (P_COMMON_OAM_MASTER_GLB(lchip).mep_index_alloc_by_sdk)
    {
        lchip           = p_sys_lmep->lchip;
        opf.pool_index  = 0;

        if ((CTC_OAM_MEP_TYPE_MPLS_BFD == p_sys_lmep->p_sys_chan->mep_type)
            || (CTC_OAM_MEP_TYPE_IP_BFD == p_sys_lmep->p_sys_chan->mep_type)
            || ((CTC_OAM_MEP_TYPE_MPLS_TP_BFD == p_sys_lmep->p_sys_chan->mep_type) && (!P_COMMON_OAM_MASTER_GLB(lchip).tp_bfd_333ms)))
        {
            opf.pool_type  = OPF_OAM_MEP_BFD;
            offset = p_sys_lmep->lmep_index + 1;
        }
        else
        {
            opf.pool_type  = OPF_OAM_MEP_LMEP;
            offset = p_sys_lmep->lmep_index + 1;
            if (((CTC_OAM_MEP_TYPE_ETH_1AG == p_sys_lmep->p_sys_chan->mep_type)
                ||(CTC_OAM_MEP_TYPE_ETH_Y1731 == p_sys_lmep->p_sys_chan->mep_type))
                && (!CTC_FLAG_ISSET(((sys_oam_lmep_y1731_t*)p_sys_lmep)->flag, CTC_OAM_Y1731_LMEP_FLAG_P2P_MODE)))
            {
                CTC_ERROR_RETURN(sys_greatbelt_opf_alloc_offset(lchip, &opf, 1, &offset));
            }
        }

        p_sys_rmep->rmep_index = offset;
    }
    else
    {
        offset = p_sys_rmep->rmep_index;
    }

    SYS_OAM_DBG_INFO("RMEP: lchip->%d, index->%d\n", lchip, offset);

    return CTC_E_NONE;
}

int32
_sys_greatbelt_oam_rmep_free_index(uint8 lchip, sys_oam_rmep_com_t* p_sys_rmep)
{
    sys_oam_lmep_com_t* p_sys_lmep = NULL;
    sys_greatbelt_opf_t opf;
    uint32 offset = 0;

    p_sys_lmep = p_sys_rmep->p_sys_lmep;

    sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));
    if (P_COMMON_OAM_MASTER_GLB(lchip).mep_index_alloc_by_sdk)
    {
        lchip = p_sys_lmep->lchip;
        offset = p_sys_rmep->rmep_index;
        opf.pool_index = 0;
        if ((CTC_OAM_MEP_TYPE_MPLS_BFD == p_sys_lmep->p_sys_chan->mep_type)
            || (CTC_OAM_MEP_TYPE_IP_BFD == p_sys_lmep->p_sys_chan->mep_type)
            || ((CTC_OAM_MEP_TYPE_MPLS_TP_BFD == p_sys_lmep->p_sys_chan->mep_type) && (!P_COMMON_OAM_MASTER_GLB(lchip).tp_bfd_333ms)))
        {
            opf.pool_type  = OPF_OAM_MEP_BFD;
        }
        else
        {
            opf.pool_type  = OPF_OAM_MEP_LMEP;
            if (((CTC_OAM_MEP_TYPE_ETH_1AG == p_sys_lmep->p_sys_chan->mep_type)
                ||(CTC_OAM_MEP_TYPE_ETH_Y1731 == p_sys_lmep->p_sys_chan->mep_type))
                && (!CTC_FLAG_ISSET(((sys_oam_lmep_y1731_t*)p_sys_lmep)->flag, CTC_OAM_Y1731_LMEP_FLAG_P2P_MODE)))
            {
                CTC_ERROR_RETURN(sys_greatbelt_opf_free_offset(lchip, &opf, 1, offset));
            }
        }

    }

    return CTC_E_NONE;

}

int32
_sys_greatbelt_oam_rmep_add_eth_to_asic(uint8 lchip, sys_oam_rmep_com_t* p_sys_rmep)
{
    ds_eth_rmep_t ds_eth_rmep;
    ds_eth_rmep_t ds_eth_rmep_mask;
    sys_oam_lmep_com_t* p_sys_lmep = NULL;
    sys_oam_rmep_y1731_t* p_sys_eth_rmep = NULL;
    uint32 cmd          = 0;
    uint32 index        = 0;
    uint32 rmep_flag    = 0;
    uint32 lmep_flag    = 0;
    uint8  tx_csf_type  = 0;
    tbl_entry_t tbl_entry;

    p_sys_lmep     = p_sys_rmep->p_sys_lmep;
    p_sys_eth_rmep = (sys_oam_rmep_y1731_t*)p_sys_rmep;

    if (SYS_OAM_NO_MEP_OPERATION(lchip, p_sys_lmep->p_sys_chan->master_chipid))
    {
        goto RETURN;
    }

    rmep_flag   = p_sys_eth_rmep->flag;
    lmep_flag   = ((sys_oam_lmep_y1731_t*)p_sys_lmep)->flag;
    tx_csf_type = ((sys_oam_lmep_y1731_t*)p_sys_lmep)->tx_csf_type;

    sal_memset(&ds_eth_rmep, 0, sizeof(ds_eth_rmep_t));
    sal_memset(&ds_eth_rmep_mask, 0, sizeof(ds_eth_rmep_t));
    ds_eth_rmep.active                  = CTC_FLAG_ISSET(rmep_flag, CTC_OAM_Y1731_RMEP_FLAG_MEP_EN);
    ds_eth_rmep.eth_oam_p2_p_mode       = CTC_FLAG_ISSET(lmep_flag, CTC_OAM_Y1731_LMEP_FLAG_P2P_MODE);
    ds_eth_rmep.seq_num_en              = CTC_FLAG_ISSET(rmep_flag, CTC_OAM_Y1731_RMEP_FLAG_CCM_SEQ_NUM_EN);
    ds_eth_rmep.mac_addr_update_disable = !CTC_FLAG_ISSET(rmep_flag, CTC_OAM_Y1731_RMEP_FLAG_MAC_UPDATE_EN);
    ds_eth_rmep.aps_signal_fail_remote  = 0;
    ds_eth_rmep.is_csf_en               = 0;

    if (ds_eth_rmep.eth_oam_p2_p_mode)
    {
        ds_eth_rmep.is_csf_en = CTC_FLAG_ISSET(rmep_flag, CTC_OAM_Y1731_RMEP_FLAG_CSF_EN);

        if (CTC_FLAG_ISSET(lmep_flag, CTC_OAM_Y1731_LMEP_FLAG_TX_CSF_EN))
        {
            /*
            8'd0,isCsfUseUcDa,dCsf,
            rxCsfWhile[3:0],rxCsfType[2:0],
            isCsfTxEn,csfType[2:0],csfWhile[2:0]}
            */
            /* set tx_csf_type */
            ds_eth_rmep.ccm_seq_num0 &= 0xFFC7;
            ds_eth_rmep.ccm_seq_num0 |= ((tx_csf_type & 0x7) << 3);

            /* set is_csf_tx_en*/
            CTC_BIT_SET(ds_eth_rmep.ccm_seq_num0, 6);
        }
    }

    if (ds_eth_rmep.eth_oam_p2_p_mode)
    {
        ds_eth_rmep.mep_index               = p_sys_eth_rmep->rmep_id;
    }
    else
    {
        ds_eth_rmep.mep_index               = p_sys_rmep->p_sys_lmep->lmep_index;
    }

    ds_eth_rmep.rmep_mac_sa_high        = p_sys_eth_rmep->rmep_mac[0] << 8 |
        p_sys_eth_rmep->rmep_mac[1];
    ds_eth_rmep.rmep_mac_sa_low         = p_sys_eth_rmep->rmep_mac[2] << 24 |
        p_sys_eth_rmep->rmep_mac[3] << 16 |
        p_sys_eth_rmep->rmep_mac[4] << 8 |
        p_sys_eth_rmep->rmep_mac[5];

    ds_eth_rmep.is_remote               = 1;
    ds_eth_rmep.rmep_while              = 14;
    ds_eth_rmep.d_unexp_period_timer    = 14;
    ds_eth_rmep.rmep_last_port_status   = 2;
    ds_eth_rmep.rmep_last_intf_status   = 1;
    ds_eth_rmep.ccm_seq_num1            = 0;
    ds_eth_rmep.cnt_shift_while         = 4;
    ds_eth_rmep.exp_ccm_num             = 0;
    ds_eth_rmep.d_loc                   = 0;
    ds_eth_rmep.d_unexp_period          = 0;
    ds_eth_rmep.rmep_last_rdi           = 0;
    ds_eth_rmep.seq_num_fail_counter    = 0;
    ds_eth_rmep.first_pkt_rx            =      g_gb_oam_master[lchip]->timer_update_disable?1:0;
    ds_eth_rmep.ccm_seq_num0            = 0;
    ds_eth_rmep.sf_fail_while           = 0;
    ds_eth_rmep.mac_status_change       = 0;
    ds_eth_rmep.is_bfd                  = 0;
    ds_eth_rmep.rmepmacmismatch         = 0;

    index = p_sys_rmep->rmep_index;
    cmd = DRV_IOW(DsEthRmep_t, DRV_ENTRY_FLAG);
    tbl_entry.data_entry = (uint32*)&ds_eth_rmep;
    tbl_entry.mask_entry = (uint32*)&ds_eth_rmep_mask;
     /*CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_eth_rmep));*/
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &tbl_entry));
RETURN:

    return CTC_E_NONE;
}

int32
_sys_greatbelt_oam_rmep_add_tp_y1731_to_asic(uint8 lchip, sys_oam_rmep_com_t* p_sys_rmep)
{
    ds_eth_rmep_t ds_eth_rmep;
    ds_eth_rmep_t ds_eth_rmep_mask;
    tbl_entry_t tbl_entry;
    sys_oam_lmep_com_t* p_sys_lmep = NULL;
    sys_oam_rmep_y1731_t* p_sys_tp_y1731_rmep = NULL;
    uint32 cmd          = 0;
    uint32 index        = 0;
    uint32 rmep_flag    = 0;
    uint32 lmep_flag    = 0;
    uint8  tx_csf_type  = 0;

    p_sys_lmep     = p_sys_rmep->p_sys_lmep;
    p_sys_tp_y1731_rmep = (sys_oam_rmep_y1731_t*)p_sys_rmep;

    if (SYS_OAM_NO_MEP_OPERATION(lchip, p_sys_lmep->p_sys_chan->master_chipid))
    {
        goto RETURN;
    }

    rmep_flag   = p_sys_tp_y1731_rmep->flag;

    lmep_flag   = ((sys_oam_lmep_y1731_t*)p_sys_lmep)->flag;
    tx_csf_type = ((sys_oam_lmep_y1731_t*)p_sys_lmep)->tx_csf_type;

    sal_memset(&ds_eth_rmep, 0, sizeof(ds_eth_rmep_t));
    sal_memset(&ds_eth_rmep_mask, 0, sizeof(ds_eth_rmep_t));

    ds_eth_rmep.active                  = CTC_FLAG_ISSET(rmep_flag, CTC_OAM_Y1731_RMEP_FLAG_MEP_EN);
    ds_eth_rmep.eth_oam_p2_p_mode       = 1;
    ds_eth_rmep.seq_num_en              = 0;
    ds_eth_rmep.mac_addr_update_disable = 0;
    ds_eth_rmep.aps_signal_fail_remote  = 0;

    ds_eth_rmep.is_csf_en = CTC_FLAG_ISSET(rmep_flag, CTC_OAM_Y1731_RMEP_FLAG_CSF_EN);

    if (CTC_FLAG_ISSET(lmep_flag, CTC_OAM_Y1731_LMEP_FLAG_TX_CSF_EN))
    {
        /*
        8'd0,isCsfUseUcDa,dCsf,
        rxCsfWhile[3:0],rxCsfType[2:0],
        isCsfTxEn,csfType[2:0],csfWhile[2:0]}
        */
        /* set tx_csf_type */
        ds_eth_rmep.ccm_seq_num0 &= 0xFFC7;
        ds_eth_rmep.ccm_seq_num0 |= ((tx_csf_type & 0x7) << 3);

        /* set is_csf_tx_en*/
        CTC_BIT_SET(ds_eth_rmep.ccm_seq_num0, 6);
    }

    ds_eth_rmep.mep_index               = p_sys_tp_y1731_rmep->rmep_id;

    ds_eth_rmep.rmep_mac_sa_high        = 0;
    ds_eth_rmep.rmep_mac_sa_low         = 0;

    ds_eth_rmep.is_remote               = 1;
    ds_eth_rmep.rmep_while              = 14;
    ds_eth_rmep.d_unexp_period_timer    = 14;
    ds_eth_rmep.rmep_last_port_status   = 0;
    ds_eth_rmep.rmep_last_intf_status   = 0;
    ds_eth_rmep.ccm_seq_num1            = 0;
    ds_eth_rmep.cnt_shift_while         = 4;
    ds_eth_rmep.exp_ccm_num             = 0;
    ds_eth_rmep.d_loc                   = 0;
    ds_eth_rmep.d_unexp_period          = 0;
    ds_eth_rmep.rmep_last_rdi           = 0;
    ds_eth_rmep.seq_num_fail_counter    = 0;
    ds_eth_rmep.first_pkt_rx            =  g_gb_oam_master[lchip]->timer_update_disable?1:0;
    ds_eth_rmep.ccm_seq_num0            = 0;
    ds_eth_rmep.sf_fail_while           = 0;
    ds_eth_rmep.mac_status_change       = 0;
    ds_eth_rmep.is_bfd                  = 0;
    ds_eth_rmep.rmepmacmismatch         = 0;

    index = p_sys_rmep->rmep_index;
    cmd = DRV_IOW(DsEthRmep_t, DRV_ENTRY_FLAG);
    tbl_entry.data_entry = (uint32*)&ds_eth_rmep;
    tbl_entry.mask_entry = (uint32*)&ds_eth_rmep_mask;
     /*CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_eth_rmep));*/
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &tbl_entry));
RETURN:

    return CTC_E_NONE;
}


int32
_sys_greatbelt_oam_rmep_del_from_asic(uint8 lchip, sys_oam_rmep_com_t* p_sys_rmep)
{
    uint32 cmd        = 0;
    uint32 index      = 0;
    ds_eth_rmep_t ds_eth_rmep;
    ds_eth_rmep_t ds_eth_rmep_mask;
    tbl_entry_t tbl_entry;

    if (SYS_OAM_NO_MEP_OPERATION(lchip, p_sys_rmep->p_sys_lmep->p_sys_chan->master_chipid))
    {
        goto RETURN;
    }

    sal_memset(&ds_eth_rmep, 0, sizeof(ds_eth_rmep));
    sal_memset(&ds_eth_rmep_mask, 0, sizeof(ds_eth_rmep));

    index = p_sys_rmep->rmep_index;
    cmd = DRV_IOW(DsEthRmep_t, DRV_ENTRY_FLAG);
    tbl_entry.data_entry = (uint32*)&ds_eth_rmep;
    tbl_entry.mask_entry = (uint32*)&ds_eth_rmep_mask;
     /*CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_eth_rmep));*/
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &tbl_entry));
RETURN:

    return CTC_E_NONE;
}

int32
_sys_greatbelt_oam_rmep_update_eth_asic(uint8 lchip, sys_oam_rmep_com_t* p_sys_rmep)
{
    sys_oam_rmep_y1731_t* p_sys_rmep_eth  = (sys_oam_rmep_y1731_t*)p_sys_rmep;
    sys_oam_lmep_com_t* p_sys_lmep = NULL;
    uint32 cmd        = 0;
    uint32 index      = 0;
    ds_eth_rmep_t ds_eth_rmep;
    ds_eth_rmep_t ds_eth_rmep_mask;
    tbl_entry_t tbl_entry;

    p_sys_lmep = p_sys_rmep->p_sys_lmep;

    if (SYS_OAM_NO_MEP_OPERATION(lchip, p_sys_lmep->p_sys_chan->master_chipid))
    {
        goto RETURN;
    }

    index = p_sys_rmep->rmep_index;

    sal_memset(&ds_eth_rmep, 0, sizeof(ds_eth_rmep));
    sal_memset(&ds_eth_rmep_mask, 0xFF, sizeof(ds_eth_rmep));
    cmd = DRV_IOR(DsEthRmep_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_eth_rmep));

    switch (p_sys_rmep_eth->update_type)
    {
    case CTC_OAM_Y1731_RMEP_UPDATE_TYPE_MEP_EN:

        if (CTC_FLAG_ISSET(p_sys_rmep_eth->flag, CTC_OAM_Y1731_RMEP_FLAG_MEP_EN))
        {
            ds_eth_rmep.active                  = 1;
            ds_eth_rmep.rmep_while              = 14;
            ds_eth_rmep.d_unexp_period_timer    = 14;
            ds_eth_rmep.rmep_last_port_status   = 2;
            ds_eth_rmep.rmep_last_intf_status   = 1;
            ds_eth_rmep.ccm_seq_num1            = 0;
            ds_eth_rmep.cnt_shift_while         = 4;
            ds_eth_rmep.exp_ccm_num             = 0;
            ds_eth_rmep.d_loc                   = 0;
            ds_eth_rmep.d_unexp_period          = 0;
            ds_eth_rmep.rmep_last_rdi           = 0;
            ds_eth_rmep.seq_num_fail_counter    = 0;
            ds_eth_rmep.ccm_seq_num0            = 0;
            ds_eth_rmep.sf_fail_while           = 0;
            ds_eth_rmep.mac_status_change       = 0;
            ds_eth_rmep.rmepmacmismatch         = 0;
        }
        else
        {
            ds_eth_rmep.active                  = 0;
        }
        ds_eth_rmep_mask.active                 = 0;
        break;

    case CTC_OAM_Y1731_RMEP_UPDATE_TYPE_CCM_SEQ_NUM_EN:
        ds_eth_rmep.seq_num_en = CTC_FLAG_ISSET(p_sys_rmep_eth->flag, CTC_OAM_Y1731_LMEP_FLAG_CCM_SEQ_NUM_EN);
        ds_eth_rmep_mask.seq_num_en             = 0;
        break;

    case CTC_OAM_Y1731_RMEP_UPDATE_TYPE_SEQ_FAIL_CLEAR:
        ds_eth_rmep.seq_num_fail_counter        = 0;
        ds_eth_rmep_mask.seq_num_fail_counter   = 0;
        break;

    case CTC_OAM_Y1731_RMEP_UPDATE_TYPE_CSF_EN:
        ds_eth_rmep.is_csf_en               = p_sys_rmep_eth->csf_en;
        ds_eth_rmep_mask.is_csf_en   = 0;
        ds_eth_rmep.first_pkt_rx              =1;
        ds_eth_rmep_mask.first_pkt_rx     =0;
        ds_eth_rmep.d_loc                   = 1;
        ds_eth_rmep_mask.d_loc                   = 0;
        break;

    case CTC_OAM_Y1731_RMEP_UPDATE_TYPE_DCSF:
        /*
        8'd0,isCsfUseUcDa,dCsf,
        rxCsfWhile[3:0],rxCsfType[2:0],
        isCsfTxEn,csfType[2:0],csfWhile[2:0]}
        */
        p_sys_rmep_eth->d_csf ? CTC_BIT_SET(ds_eth_rmep.ccm_seq_num0, 14) : CTC_BIT_UNSET(ds_eth_rmep.ccm_seq_num0, 14);
        CTC_BIT_UNSET(ds_eth_rmep_mask.ccm_seq_num0, 14);
        break;

    case CTC_OAM_Y1731_RMEP_UPDATE_TYPE_RMEP_SF_STATE:
        ds_eth_rmep.aps_signal_fail_remote = p_sys_rmep_eth->sf_state;
        ds_eth_rmep_mask.aps_signal_fail_remote = 0;
        break;

    case CTC_OAM_Y1731_RMEP_UPDATE_TYPE_RMAC_MIS:
        ds_eth_rmep.rmepmacmismatch         = p_sys_rmep_eth->src_mac_state;
        ds_eth_rmep_mask.rmepmacmismatch    = 0;
        break;

    case CTC_OAM_Y1731_RMEP_UPDATE_TYPE_PORT_INTF_MIS:
        ds_eth_rmep.mac_status_change       = p_sys_rmep_eth->port_intf_state;
        ds_eth_rmep_mask.mac_status_change  = 0;
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    cmd = DRV_IOW(DsEthRmep_t, DRV_ENTRY_FLAG);
    tbl_entry.data_entry = (uint32*)&ds_eth_rmep;
    tbl_entry.mask_entry = (uint32*)&ds_eth_rmep_mask;
     /*CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_eth_rmep));*/
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &tbl_entry));
RETURN:

    return CTC_E_NONE;
}

int32
_sys_greatbelt_oam_rmep_update_tp_y1731_asic(uint8 lchip, sys_oam_rmep_com_t* p_sys_rmep)
{
    sys_oam_rmep_y1731_t* p_sys_rmep_tp_y1731  = (sys_oam_rmep_y1731_t*)p_sys_rmep;
    sys_oam_lmep_com_t* p_sys_lmep = NULL;
    uint32 cmd        = 0;
    uint32 index      = 0;
    ds_eth_rmep_t ds_eth_rmep;
    ds_eth_rmep_t ds_eth_rmep_mask;
    tbl_entry_t tbl_entry;

    p_sys_lmep = p_sys_rmep->p_sys_lmep;

    if (SYS_OAM_NO_MEP_OPERATION(lchip, p_sys_lmep->p_sys_chan->master_chipid))
    {
        goto RETURN;
    }

    index = p_sys_rmep->rmep_index;

    sal_memset(&ds_eth_rmep, 0, sizeof(ds_eth_rmep_t));
    sal_memset(&ds_eth_rmep_mask, 0xFF, sizeof(ds_eth_rmep_t));
    cmd = DRV_IOR(DsEthRmep_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_eth_rmep));

    switch (p_sys_rmep_tp_y1731->update_type)
    {
    case CTC_OAM_Y1731_RMEP_UPDATE_TYPE_MEP_EN:

        if (CTC_FLAG_ISSET(p_sys_rmep_tp_y1731->flag, CTC_OAM_Y1731_RMEP_FLAG_MEP_EN))
        {
            ds_eth_rmep.active                  = 1;
            ds_eth_rmep.rmep_while              = 14;
            ds_eth_rmep.d_unexp_period_timer    = 14;
            ds_eth_rmep.ccm_seq_num1            = 0;
            ds_eth_rmep.cnt_shift_while         = 4;
            ds_eth_rmep.exp_ccm_num             = 0;
            ds_eth_rmep.d_loc                   = 0;
            ds_eth_rmep.d_unexp_period          = 0;
            ds_eth_rmep.rmep_last_rdi           = 0;
            ds_eth_rmep.seq_num_fail_counter    = 0;
            ds_eth_rmep.ccm_seq_num0            = 0;
            ds_eth_rmep.sf_fail_while           = 0;
            ds_eth_rmep.mac_status_change       = 0;
            ds_eth_rmep.rmepmacmismatch         = 0;
        }
        else
        {
            ds_eth_rmep.active                  = 0;
        }
        ds_eth_rmep_mask.active                 = 0;
        break;

    case CTC_OAM_Y1731_RMEP_UPDATE_TYPE_CSF_EN:
        ds_eth_rmep.is_csf_en               = p_sys_rmep_tp_y1731->csf_en;
        ds_eth_rmep_mask.is_csf_en          = 0;
        break;

    case CTC_OAM_Y1731_RMEP_UPDATE_TYPE_DCSF:
        /*
        8'd0,isCsfUseUcDa,dCsf,
        rxCsfWhile[3:0],rxCsfType[2:0],
        isCsfTxEn,csfType[2:0],csfWhile[2:0]}
        */
        p_sys_rmep_tp_y1731->d_csf ? CTC_BIT_SET(ds_eth_rmep.ccm_seq_num0, 14) : CTC_BIT_UNSET(ds_eth_rmep.ccm_seq_num0, 14);
        CTC_BIT_UNSET(ds_eth_rmep_mask.ccm_seq_num0, 14);
        break;

    case CTC_OAM_Y1731_RMEP_UPDATE_TYPE_RMEP_SF_STATE:
        ds_eth_rmep.aps_signal_fail_remote      = p_sys_rmep_tp_y1731->sf_state;
        ds_eth_rmep_mask.aps_signal_fail_remote = 0;
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    cmd = DRV_IOW(DsEthRmep_t, DRV_ENTRY_FLAG);
    tbl_entry.data_entry = (uint32*)&ds_eth_rmep;
    tbl_entry.mask_entry = (uint32*)&ds_eth_rmep_mask;
     /*CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_eth_rmep));*/
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &tbl_entry));
RETURN:

    return CTC_E_NONE;
}

#define COM "Function Begin"

int32
_sys_greatbelt_oam_defect_read_defect_status(uint8 lchip,
                                             void* p_defect)
{
    uint32 cmd = 0;
    uint32 cache_entry_valid = 0;
    uint8 cache_entry_idx  = 0;
    uint8 valid_cache_num = 0;
    uint32 defect_status_index = 0;
    oam_error_defect_ctl_t oam_error_defect_ctl;
    ds_oam_defect_status_t ds_oam_defect_status;
    oam_defect_cache_t oam_defect_cache;
    ctc_oam_defect_info_t* p_defect_info = NULL;

    CTC_PTR_VALID_CHECK(p_defect);

    p_defect_info = (ctc_oam_defect_info_t*)p_defect;

     /*sal_memset(&oam_error_defect_ctl, 0, sizeof(oam_error_defect_ctl));*/
    cmd = DRV_IOR(OamErrorDefectCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &oam_error_defect_ctl));

    cache_entry_valid = oam_error_defect_ctl.cache_entry_valid;

    if (0 == cache_entry_valid)
    {
        return CTC_E_NONE;
    }

    for (cache_entry_idx = 0; cache_entry_idx < CTC_OAM_MAX_ERROR_CACHE_NUM; cache_entry_idx++)
    {
        if (IS_BIT_SET(cache_entry_valid, cache_entry_idx))
        {
             /*sal_memset(&oam_defect_cache, 0, sizeof(oam_defect_cache));*/
            cmd = DRV_IOR(OamDefectCache_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, cache_entry_idx, cmd, &oam_defect_cache));
            defect_status_index = oam_defect_cache.scan_ptr;

             /*sal_memset(&ds_oam_defect_status, 0, sizeof(ds_oam_defect_status));*/
            cmd = DRV_IOR(DsOamDefectStatus_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, defect_status_index, cmd, &ds_oam_defect_status));

            p_defect_info->mep_index_base[valid_cache_num] = defect_status_index;
            p_defect_info->mep_index_bitmap[valid_cache_num++] = ds_oam_defect_status.defect_status;

            /*Clear after read the status */
#if (SDK_WORK_PLATFORM == 1)
            if (!drv_greatbelt_io_is_asic())
            {
                ds_oam_defect_status.defect_status = 0;
            }
#endif
            cmd = DRV_IOW(DsOamDefectStatus_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, defect_status_index, cmd, &ds_oam_defect_status));
        }
    }


#if (SDK_WORK_PLATFORM == 1)
    if (!drv_greatbelt_io_is_asic())
    {
        cache_entry_valid = 0;
    }
#endif
    cmd = DRV_IOW(OamErrorDefectCtl_t, OamErrorDefectCtl_CacheEntryValid_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &cache_entry_valid));

    p_defect_info->valid_cache_num = valid_cache_num;

    return CTC_E_NONE;
}

int32
_sys_greatbelt_oam_defect_scan_en(uint8 lchip, bool enable)
{
    uint32 cmd = 0;
    uint32 field_val = 0;
    uint32 i = 0;
    ds_oam_defect_status_t ds_oam_defect_status;
    oam_error_defect_ctl_t oam_error_defect_ctl;

    sal_memset(&ds_oam_defect_status, 0xFF, sizeof(ds_oam_defect_status_t));
    sal_memset(&oam_error_defect_ctl, 0, sizeof(oam_error_defect_ctl_t));
#if (SDK_WORK_PLATFORM == 1)
    ds_oam_defect_status.defect_status = 0;
#endif
    cmd = DRV_IOW(DsOamDefectStatus_t, DRV_ENTRY_FLAG);
    for (i = 0; i < 256; i++)
    {
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, i, cmd, &ds_oam_defect_status));
    }

    field_val = 0;
    cmd = DRV_IOW(OamErrorDefectCtl_t, OamErrorDefectCtl_MinPtr_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    field_val = 0xFF;
    cmd = DRV_IOW(OamErrorDefectCtl_t, OamErrorDefectCtl_MaxPtr_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    field_val = 0xFFFFFFFF;
    cmd = DRV_IOW(OamErrorDefectCtl_t, OamErrorDefectCtl_ReportDefectEn0_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    field_val = 0xFFFFFFFF;
    cmd = DRV_IOW(OamErrorDefectCtl_t, OamErrorDefectCtl_ReportDefectEn1_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    field_val = 40;
    cmd = DRV_IOW(OamErrorDefectCtl_t, OamErrorDefectCtl_ScanDefectInterval_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));


    field_val = enable ? 1 : 0;
    cmd = DRV_IOW(OamErrorDefectCtl_t, OamErrorDefectCtl_ScanEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));


    return CTC_E_NONE;
}

int32
_sys_greatbelt_oam_get_stats_info(uint8 lchip, sys_oam_lm_com_t* p_sys_oam_lm, ctc_oam_stats_info_t* p_stat_info)
{
    ds_oam_lm_stats0_t ds_oam_lm_stats;
    uint8 cnt = 0;

    p_stat_info->lm_cos_type    = p_sys_oam_lm->lm_cos_type;
    p_stat_info->lm_type        = p_sys_oam_lm->lm_type;
    if ((CTC_OAM_LM_COS_TYPE_ALL_COS == p_sys_oam_lm->lm_cos_type)
        || (CTC_OAM_LM_COS_TYPE_SPECIFIED_COS == p_sys_oam_lm->lm_cos_type))
    {
        sal_memset(&ds_oam_lm_stats, 0, sizeof(ds_oam_lm_stats0_t));

        SYS_OAM_LM_CMD_READ((p_sys_oam_lm->lchip), (p_sys_oam_lm->lm_index), ds_oam_lm_stats);
        p_stat_info->lm_info[0].rx_fcb = ds_oam_lm_stats.rx_fcb_r;
        p_stat_info->lm_info[0].rx_fcl = ds_oam_lm_stats.rx_fcl_r;
        p_stat_info->lm_info[0].tx_fcb = ds_oam_lm_stats.tx_fcb_r;
        p_stat_info->lm_info[0].tx_fcf = ds_oam_lm_stats.tx_fcf_r;
    }
    else
    {
        for (cnt = 0; cnt < CTC_OAM_STATS_COS_NUM; cnt++)
        {
            sal_memset(&ds_oam_lm_stats, 0, sizeof(ds_oam_lm_stats0_t));
            SYS_OAM_LM_CMD_READ((p_sys_oam_lm->lchip), (p_sys_oam_lm->lm_index + cnt), ds_oam_lm_stats);
            p_stat_info->lm_info[cnt].rx_fcb = ds_oam_lm_stats.rx_fcb_r;
            p_stat_info->lm_info[cnt].rx_fcl = ds_oam_lm_stats.rx_fcl_r;
            p_stat_info->lm_info[cnt].tx_fcb = ds_oam_lm_stats.tx_fcb_r;
            p_stat_info->lm_info[cnt].tx_fcf = ds_oam_lm_stats.tx_fcf_r;
        }
    }

    return CTC_E_NONE;
}

int32
_sys_greatbelt_oam_process_bfd_state(uint8 lchip, uint16 r_idx)
{
    uint32 lmep_index = 0;
    uint32 rmep_index = 0;

    ds_bfd_mep_t   ds_bfd_mep;
    ds_bfd_mep_t   ds_bfd_mep_mask;
    ds_bfd_rmep_t  ds_bfd_rmep;
    ds_bfd_rmep_t  ds_bfd_rmep_mask;
    tbl_entry_t    tbl_entry;
    uint32 cmd_bfd_mep_read     = 0;
    uint32 cmd_bfd_rmep_read    = 0;
    uint32 cmd_bfd_mep_write    = 0;
    uint32 cmd_bfd_rmep_write   = 0;

    sys_oam_lmep_com_t* p_sys_lmep_com  = NULL;
    sys_oam_lmep_bfd_t* p_sys_lmep_bfd  = NULL;
    sys_oam_rmep_bfd_t* p_sys_rmep_bfd  = NULL;

    sal_memset(&ds_bfd_mep, 0, sizeof(ds_bfd_mep_t));
    sal_memset(&ds_bfd_mep_mask, 0, sizeof(ds_bfd_mep_t));
    sal_memset(&ds_bfd_rmep, 0, sizeof(ds_bfd_rmep_t));
    sal_memset(&ds_bfd_rmep_mask, 0, sizeof(ds_bfd_rmep_t));

    rmep_index = r_idx;
    lmep_index = rmep_index - 1;

    cmd_bfd_rmep_read   = DRV_IOR(DsBfdRmep_t,  DRV_ENTRY_FLAG);
    cmd_bfd_mep_read    = DRV_IOR(DsBfdMep_t,   DRV_ENTRY_FLAG);
    cmd_bfd_rmep_write  = DRV_IOW(DsBfdRmep_t,  DRV_ENTRY_FLAG);
    cmd_bfd_mep_write   = DRV_IOW(DsBfdMep_t,   DRV_ENTRY_FLAG);

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lmep_index, cmd_bfd_mep_read, &ds_bfd_mep));
    if(!ds_bfd_mep.active)
    {
        return CTC_E_NONE;
    }

    p_sys_lmep_com = ctc_vector_get(g_gb_oam_master[lchip]->mep_vec, lmep_index);
    if (p_sys_lmep_com)
    {
        sys_oam_rmep_bfd_t sys_oam_rmep_bfd;
        sal_memset(&sys_oam_rmep_bfd, 0, sizeof(sys_oam_rmep_bfd_t));
        p_sys_lmep_bfd = (sys_oam_lmep_bfd_t*)p_sys_lmep_com;
        p_sys_rmep_bfd = (sys_oam_rmep_bfd_t*)_sys_greatbelt_oam_rmep_lkup(lchip, &p_sys_lmep_bfd->com, &sys_oam_rmep_bfd.com);

    }
    else
    {
        return CTC_E_NONE;
    }

    if (CTC_OAM_BFD_STATE_UP == ds_bfd_mep.local_stat )
    {
        /*remote mep p flag clear*/

        CTC_ERROR_RETURN(DRV_IOCTL(lchip, rmep_index, cmd_bfd_rmep_read, &ds_bfd_rmep));
        if(0 == ds_bfd_rmep.fbit)
        {
            sal_memset(&ds_bfd_rmep_mask, 0xFF, sizeof(ds_bfd_rmep_t));
            ds_bfd_rmep.required_min_rx_interval = p_sys_rmep_bfd->required_min_rx_interval;
            ds_bfd_rmep_mask.required_min_rx_interval = 0;
            tbl_entry.data_entry = (uint32*)&ds_bfd_rmep;
            tbl_entry.mask_entry = (uint32*)&ds_bfd_rmep_mask;
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, rmep_index, cmd_bfd_rmep_write, &tbl_entry));

            /*local mep actual tx interval*/
            ds_bfd_mep.desired_min_tx_interval = p_sys_lmep_bfd->desired_min_tx_interval;
            ds_bfd_mep.pbit = 1;

            ds_bfd_mep_mask.desired_min_tx_interval = 0;
            ds_bfd_mep_mask.pbit                    = 0;

            ds_bfd_mep.actual_min_tx_interval       = ((3 >= p_sys_lmep_bfd->desired_min_tx_interval)?
                                                        p_sys_lmep_bfd->desired_min_tx_interval : (p_sys_lmep_bfd->desired_min_tx_interval * 4/ 5));
            ds_bfd_mep_mask.actual_min_tx_interval  = 0;

            p_sys_lmep_bfd->actual_tx_interval = p_sys_lmep_bfd->desired_min_tx_interval;

            ds_bfd_mep.hello_while      = 4;
            ds_bfd_mep_mask.hello_while = 0;

            ds_bfd_mep.local_diag       = 0;
            ds_bfd_mep_mask.local_diag  = 0;

            tbl_entry.data_entry = (uint32*)&ds_bfd_mep;
            tbl_entry.mask_entry = (uint32*)&ds_bfd_mep_mask;
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lmep_index, cmd_bfd_mep_write, &tbl_entry));

            SYS_OAM_DBG_INFO("Up event:\n");
            SYS_OAM_DBG_INFO("Result: ds_bfd_mep.actual_min_tx_interval = %d, ds_bfd_rmep.actual_rx_interval = %d, p_sys_lmep_bfd->actual_tx_interval = %d\n", 
                           ds_bfd_mep.actual_min_tx_interval, ds_bfd_rmep.actual_rx_interval, p_sys_lmep_bfd->actual_tx_interval);
        }
    }
    else if (CTC_OAM_BFD_STATE_DOWN == ds_bfd_mep.local_stat )
    {/*up->down*/
#if (SDK_WORK_PLATFORM == 1)
        if (!drv_greatbelt_io_is_asic())
        {
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, rmep_index, cmd_bfd_rmep_read, &ds_bfd_rmep));
        }
        else
        {
            ds_bfd_mep.actual_min_tx_interval  = 1000;
        }
#else
        ds_bfd_mep.actual_min_tx_interval  = 1000;
#endif

        ds_bfd_mep.desired_min_tx_interval = 1000;
        ds_bfd_mep.pbit = 0;

        ds_bfd_mep_mask.actual_min_tx_interval  = 0;
        ds_bfd_mep_mask.desired_min_tx_interval = 0;
        ds_bfd_mep_mask.pbit = 0;

        tbl_entry.data_entry = (uint32*)&ds_bfd_mep;
        tbl_entry.mask_entry = (uint32*)&ds_bfd_mep_mask;
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, lmep_index, cmd_bfd_mep_write, &tbl_entry));

        /*remote mep actual rx interval*/
        sal_memset(&ds_bfd_rmep_mask, 0xFF, sizeof(ds_bfd_rmep_t));
        ds_bfd_rmep.actual_rx_interval          = 1000;
        ds_bfd_rmep.fbit = 0;
        ds_bfd_rmep_mask.actual_rx_interval     = 0;
        ds_bfd_rmep_mask.fbit                   = 0;

        tbl_entry.data_entry = (uint32*)&ds_bfd_rmep;
        tbl_entry.mask_entry = (uint32*)&ds_bfd_rmep_mask;
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, rmep_index, cmd_bfd_mep_write, &tbl_entry));

    }

    return CTC_E_NONE;

}



int32
_sys_greatbelt_oam_get_mep_info(uint8 lchip, ctc_oam_mep_info_t* mep_info)
{
    uint32 lmep_index   = 0;
    uint32 cmd_eth_mep  = 0;
    uint32 cmd_eth_rmep = 0;

    ctc_oam_mep_type_t mep_type = 0;

    ds_eth_mep_t   ds_eth_mep;
    ds_eth_rmep_t  ds_eth_rmep;
    ds_bfd_mep_t   ds_bfd_mep;
     /*ds_bfd_mep_t   ds_bfd_mep_mask;*/
    ds_bfd_rmep_t  ds_bfd_rmep;
     /*ds_bfd_rmep_t  ds_bfd_rmep_mask;*/

    sys_oam_lmep_com_t* p_sys_lmep_com  = NULL;
    sys_oam_rmep_com_t* p_sys_rmep_com  = NULL;

    sys_oam_lmep_y1731_t* p_sys_lmep_y1731  = NULL;
    sys_oam_rmep_y1731_t* p_sys_rmep_y1731  = NULL;
    sys_oam_lmep_bfd_t*   p_sys_lmep_bfd  = NULL;

    ctc_oam_y1731_lmep_info_t*  p_lmep_y1731_info = NULL;
    ctc_oam_y1731_rmep_info_t*  p_rmep_y1731_info = NULL;
    ctc_oam_bfd_lmep_info_t*    p_lmep_bfd_info = NULL;
    ctc_oam_bfd_rmep_info_t*    p_rmep_bfd_info = NULL;

    uint8 is_remote = 0;
    uint32 tmp[8] = {0, 33, 100, 1000, 10000, 100000, 600000, 6000000};

    CTC_PTR_VALID_CHECK(mep_info);

    cmd_eth_mep     = DRV_IOR(DsEthMep_t,   DRV_ENTRY_FLAG);
    cmd_eth_rmep    = DRV_IOR(DsEthRmep_t,  DRV_ENTRY_FLAG);

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, mep_info->mep_index, cmd_eth_rmep, &ds_eth_rmep));

    mep_info->is_rmep   = ds_eth_rmep.is_remote;

    if(!ds_eth_rmep.active)
    {
        return CTC_E_OAM_INVALID_MEP_INDEX;
    }

    if (ds_eth_rmep.is_remote == 1)
    {
        is_remote = 1;
        p_sys_rmep_com = ctc_vector_get(g_gb_oam_master[lchip]->mep_vec, mep_info->mep_index);

        if (p_sys_rmep_com)
        {
            mep_type = p_sys_rmep_com->p_sys_lmep->p_sys_chan->mep_type;
        }
        else
        {
            return CTC_E_OAM_INVALID_MEP_INDEX;
        }

        if ((CTC_OAM_MEP_TYPE_ETH_1AG == mep_type)
            || (CTC_OAM_MEP_TYPE_ETH_Y1731 == mep_type)
            || (CTC_OAM_MEP_TYPE_MPLS_TP_Y1731 == mep_type))
        {
            p_sys_rmep_y1731 = (sys_oam_rmep_y1731_t*)p_sys_rmep_com;
            p_rmep_y1731_info = &(mep_info->rmep.y1731_rmep);

            if (p_sys_rmep_y1731)
            {
                p_rmep_y1731_info->rmep_id = p_sys_rmep_y1731->rmep_id;
            }

            p_rmep_y1731_info->active           = ds_eth_rmep.active;
            p_rmep_y1731_info->is_p2p_mode      = ds_eth_rmep.eth_oam_p2_p_mode;

            p_rmep_y1731_info->mac_sa[0]        = ds_eth_rmep.rmep_mac_sa_high >> 8;
            p_rmep_y1731_info->mac_sa[1]        = ds_eth_rmep.rmep_mac_sa_high & 0xFF;
            p_rmep_y1731_info->mac_sa[2]        = ds_eth_rmep.rmep_mac_sa_low >> 24;
            p_rmep_y1731_info->mac_sa[3]        = (ds_eth_rmep.rmep_mac_sa_low >> 16) & 0xFF;
            p_rmep_y1731_info->mac_sa[4]        = (ds_eth_rmep.rmep_mac_sa_low >> 8) & 0xFF;
            p_rmep_y1731_info->mac_sa[5]        = ds_eth_rmep.rmep_mac_sa_low & 0xFF;

            p_rmep_y1731_info->first_pkt_rx     = ds_eth_rmep.first_pkt_rx;
            p_rmep_y1731_info->d_loc            = ds_eth_rmep.d_loc;
            p_rmep_y1731_info->d_unexp_period   = ds_eth_rmep.d_unexp_period;
            p_rmep_y1731_info->ma_sa_mismatch   = ds_eth_rmep.rmepmacmismatch;
            p_rmep_y1731_info->mac_status_change = ds_eth_rmep.mac_status_change;
            p_rmep_y1731_info->last_rdi         = ds_eth_rmep.rmep_last_rdi;

            p_rmep_y1731_info->lmep_index = p_rmep_y1731_info->is_p2p_mode ? (mep_info->mep_index - 1) :
                ds_eth_rmep.mep_index;

            p_sys_lmep_y1731 = (sys_oam_lmep_y1731_t*)(p_sys_rmep_com->p_sys_lmep);
            if (p_sys_lmep_y1731->ccm_interval < 8)
            {
                p_rmep_y1731_info->dloc_time = (p_rmep_y1731_info->d_loc) ? 0 : 
                (ds_eth_rmep.rmep_while * tmp[p_sys_lmep_y1731->ccm_interval] / 40);
            }
            if (p_rmep_y1731_info->is_p2p_mode && ds_eth_rmep.is_csf_en)
            {
                /* ccmSeqNum[23:0]/
                   {8'd0,isCsfUseUcDa,dCsf,
                   rxCsfWhile[3:0],rxCsfType[2:0],
                   isCsfTxEn,csfType[2:0],csfWhile[2:0]}
               */
                p_rmep_y1731_info->csf_en       = 1;
                p_rmep_y1731_info->d_csf        = CTC_IS_BIT_SET(ds_eth_rmep.ccm_seq_num0, 14); /*dCSF*/;
                p_rmep_y1731_info->rx_csf_type  = (ds_eth_rmep.ccm_seq_num0 >> 3) & 0x7;
            }

            lmep_index = p_rmep_y1731_info->lmep_index;
        }
        else if ((CTC_OAM_MEP_TYPE_IP_BFD == mep_type)
                    || (CTC_OAM_MEP_TYPE_MPLS_BFD == mep_type)
                    || (CTC_OAM_MEP_TYPE_MPLS_TP_BFD == mep_type))
        {
             /*CTC_ERROR_RETURN(DRV_IOCTL(lchip, mep_info->mep_index, cmd_bfd_rmep, &ds_bfd_rmep));*/
            sal_memcpy(&ds_bfd_rmep, &ds_eth_rmep, sizeof(ds_eth_rmep_t));
            p_rmep_bfd_info = &(mep_info->rmep.bfd_rmep);
            /*{5'd0,maIdLengthType[1:0], maNameIndex[13:0], ccmInterval[2:0],cvWhile[2:0], dMisConWhile[3:0],dMisCon}*/
            p_rmep_bfd_info->actual_rx_interval = ds_bfd_rmep.actual_rx_interval;
            p_rmep_bfd_info->first_pkt_rx       = ds_bfd_rmep.first_pkt_rx;
            p_rmep_bfd_info->mep_en             = ds_bfd_rmep.active;
            p_rmep_bfd_info->required_min_rx_interval = ds_bfd_rmep.required_min_rx_interval;
            if (CTC_OAM_MEP_TYPE_MPLS_TP_BFD == mep_type)
            {
                p_rmep_bfd_info->mis_connect        = CTC_IS_BIT_SET(ds_bfd_rmep.ip_sa, 0);
            }
            p_rmep_bfd_info->remote_detect_mult = ds_bfd_rmep.defect_mult;
            p_rmep_bfd_info->remote_diag        = ds_bfd_rmep.remote_diag;
            p_rmep_bfd_info->remote_discr       = ds_bfd_rmep.remote_disc;
            p_rmep_bfd_info->remote_state       = ds_bfd_rmep.remote_stat;

            p_rmep_bfd_info->lmep_index         = (mep_info->mep_index - 1);
            p_rmep_bfd_info->dloc_time = ds_bfd_rmep.defect_while;
            lmep_index = p_rmep_bfd_info->lmep_index;

        }
    }
    else
    {
        lmep_index = mep_info->mep_index;
    }

    if(is_remote)
    {
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, lmep_index, cmd_eth_mep, &ds_eth_mep));
    }
    else
    {
        sal_memcpy(&ds_eth_mep, &ds_eth_rmep, sizeof(ds_eth_rmep_t));
    }

    if (!ds_eth_mep.active)
    {
        return CTC_E_OAM_INVALID_MEP_INDEX;
    }

    p_sys_lmep_com = ctc_vector_get(g_gb_oam_master[lchip]->mep_vec, lmep_index);
    if (p_sys_lmep_com)
    {
        mep_type = p_sys_lmep_com->p_sys_chan->mep_type;
        mep_info->mep_type = mep_type;
    }
    else
    {
        return CTC_E_OAM_INVALID_MEP_INDEX;
    }

    if ((CTC_OAM_MEP_TYPE_ETH_1AG == mep_type)
        || (CTC_OAM_MEP_TYPE_ETH_Y1731 == mep_type)
        || (CTC_OAM_MEP_TYPE_MPLS_TP_Y1731 == mep_type))
    {
        p_sys_lmep_y1731 = (sys_oam_lmep_y1731_t*)p_sys_lmep_com;
        p_lmep_y1731_info = &(mep_info->lmep.y1731_lmep);

        if (p_sys_lmep_y1731)
        {
            p_lmep_y1731_info->mep_id         = p_sys_lmep_y1731->mep_id;
            p_lmep_y1731_info->ccm_interval   = p_sys_lmep_y1731->ccm_interval;
            p_lmep_y1731_info->level          = p_sys_lmep_y1731->md_level;
        }



        p_lmep_y1731_info->active         = ds_eth_mep.active;
        p_lmep_y1731_info->d_unexp_mep    = ds_eth_mep.d_unexp_mep;
        p_lmep_y1731_info->d_mismerge     = ds_eth_mep.d_mismerge;
        p_lmep_y1731_info->d_meg_lvl      = ds_eth_mep.d_meg_lvl;
        p_lmep_y1731_info->active         = ds_eth_mep.active;
        p_lmep_y1731_info->is_up_mep      = ds_eth_mep.is_up;
        p_lmep_y1731_info->vlan_id        = ds_eth_mep.mep_primary_vid;
        p_lmep_y1731_info->present_rdi    = ds_eth_mep.present_rdi;
        p_lmep_y1731_info->ccm_enable     = ds_eth_mep.cci_en;
    }
    else if ((CTC_OAM_MEP_TYPE_IP_BFD == mep_type)
                    || (CTC_OAM_MEP_TYPE_MPLS_BFD == mep_type)
                    || (CTC_OAM_MEP_TYPE_MPLS_TP_BFD == mep_type))
    {
        p_sys_lmep_bfd = (sys_oam_lmep_bfd_t*)p_sys_lmep_com;
        p_lmep_bfd_info = &(mep_info->lmep.bfd_lmep);

         /*CTC_ERROR_RETURN(DRV_IOCTL(lchip, lmep_index, cmd_bfd_mep, &ds_bfd_mep));*/
        sal_memcpy(&ds_bfd_mep, &ds_eth_mep, sizeof(ds_eth_mep_t));
         /*sal_memset(&ds_bfd_mep_mask, 0xFF, sizeof(ds_bfd_mep_t));*/

        p_lmep_bfd_info->mep_en             = ds_bfd_mep.active;
        p_lmep_bfd_info->cc_enable          = ds_bfd_mep.cci_en;
        p_lmep_bfd_info->loacl_state        = ds_bfd_mep.local_stat;
        p_lmep_bfd_info->local_diag         = ds_bfd_mep.local_diag;
         /*p_lmep_bfd_info->local_detect_mult  =*/
        p_lmep_bfd_info->local_discr        = ds_bfd_mep.local_disc;
        if (p_sys_lmep_bfd)
        {
            p_lmep_bfd_info->actual_tx_interval = p_sys_lmep_bfd->actual_tx_interval;
        }
        
        p_lmep_bfd_info->desired_min_tx_interval = ds_bfd_mep.desired_min_tx_interval;


        if (CTC_OAM_MEP_TYPE_MPLS_TP_BFD == mep_type)
        {
            p_lmep_bfd_info->csf_en             = ds_bfd_mep.is_csf_en;
            p_lmep_bfd_info->d_csf              = CTC_IS_BIT_SET(ds_bfd_mep.bfd_srcport, 6);
            p_lmep_bfd_info->rx_csf_type        = _sys_greatbelt_bfd_csf_convert(lchip, ((ds_bfd_mep.bfd_srcport >> 7) & 0x7), FALSE);
        }

    }

    /*"3'd0: ETH_CCM_MEP
       3'd1: PBT_CCM_MEP
       3'd2: TRILL BFD,
       3'd5: BFD MEP
       3'd6:ACH Y1731 MEP
       3'd7:ACH BFD MEP"*/

    return CTC_E_NONE;
}

int32
_sys_greatbelt_oam_build_defect_event(uint8 lchip, ctc_oam_mep_info_t* p_mep_info, ctc_oam_event_t* p_event)
{
    uint16   entry_idx   = 0;
    uint16  mep_index   = 0;
    uint32* bitmap     = NULL;

    uint8 d_meg_lvl     = 0;
    uint8 d_mismerge    = 0;
    uint8 d_unexp_mep   = 0;
    uint8 present_rdi   = 0;

    uint8 d_loc             = 0;
    uint8 first_pkt_rx      = 0;
    uint8 d_unexp_period    = 0;
    uint8 mac_status_change = 0;
    uint8 ma_sa_mismatch    = 0;
    uint8 last_rdi          = 0;
    uint8 d_csf             = 0;
    uint8 mis_connect       = 0;
    uint8 state             = CTC_OAM_BFD_STATE_ADMIN_DOWN;
    uint8 remote_state      = 0xFF;

    sys_oam_lmep_com_t* p_sys_lmep_com  = NULL;
    sys_oam_rmep_com_t* p_sys_rmep_com  = NULL;

    entry_idx = p_event->valid_entry_num;
    mep_index = p_mep_info->mep_index;

    p_event->oam_event_entry[entry_idx].mep_type    = p_mep_info->mep_type;
    p_event->oam_event_entry[entry_idx].is_remote   = p_mep_info->is_rmep;

    /*common*/
    if (p_mep_info->is_rmep)
    {
        p_sys_rmep_com = ctc_vector_get(g_gb_oam_master[lchip]->mep_vec, mep_index);
        if ((NULL == p_sys_rmep_com)
            || ((0 == p_mep_info->rmep.y1731_rmep.active) && ((CTC_OAM_MEP_TYPE_ETH_1AG == p_mep_info->mep_type)
                                                              || (CTC_OAM_MEP_TYPE_ETH_Y1731 == p_mep_info->mep_type)
                                                              || (CTC_OAM_MEP_TYPE_MPLS_TP_Y1731 == p_mep_info->mep_type)))
            ||((0 == p_mep_info->rmep.bfd_rmep.mep_en) && ((CTC_OAM_MEP_TYPE_IP_BFD == p_mep_info->mep_type)
                                                              || (CTC_OAM_MEP_TYPE_MPLS_BFD == p_mep_info->mep_type)
                                                              || (CTC_OAM_MEP_TYPE_MPLS_TP_BFD == p_mep_info->mep_type))))
        {
            return CTC_E_NONE;
        }



        p_sys_lmep_com = p_sys_rmep_com->p_sys_lmep;
        if ((NULL == p_sys_lmep_com)
            || ((0 == p_mep_info->lmep.y1731_lmep.active) && ((CTC_OAM_MEP_TYPE_ETH_1AG == p_mep_info->mep_type)
                                                              || (CTC_OAM_MEP_TYPE_ETH_Y1731 == p_mep_info->mep_type)
                                                              || (CTC_OAM_MEP_TYPE_MPLS_TP_Y1731 == p_mep_info->mep_type)))
            || ((0 == p_mep_info->lmep.bfd_lmep.mep_en) && ((CTC_OAM_MEP_TYPE_IP_BFD == p_mep_info->mep_type)
                                                              || (CTC_OAM_MEP_TYPE_MPLS_BFD == p_mep_info->mep_type)
                                                              || (CTC_OAM_MEP_TYPE_MPLS_TP_BFD == p_mep_info->mep_type))))
        {
            return CTC_E_NONE;
        }

        p_event->oam_event_entry[entry_idx].lmep_index = p_sys_lmep_com->lmep_index;
        p_event->oam_event_entry[entry_idx].rmep_index = p_sys_rmep_com->rmep_index;

         /*p_event->oam_event_entry[entry_idx].local_endpoint_id   = SYS_OAM_GET_ID_BY_IDX(p_sys_lmep_com->lmep_index);*/
         /*p_event->oam_event_entry[entry_idx].remote_endpoint_id  = SYS_OAM_GET_ID_BY_IDX(p_sys_rmep_com->rmep_index);*/
    }
    else
    {
        p_sys_lmep_com = ctc_vector_get(g_gb_oam_master[lchip]->mep_vec, mep_index);
        if ((NULL == p_sys_lmep_com)
            || ((0 == p_mep_info->lmep.y1731_lmep.active) && ((CTC_OAM_MEP_TYPE_ETH_1AG == p_mep_info->mep_type)
                                                              || (CTC_OAM_MEP_TYPE_ETH_Y1731 == p_mep_info->mep_type)
                                                              || (CTC_OAM_MEP_TYPE_MPLS_TP_Y1731 == p_mep_info->mep_type)))
            || ((0 == p_mep_info->lmep.bfd_lmep.mep_en) && ((CTC_OAM_MEP_TYPE_IP_BFD == p_mep_info->mep_type)
                                                              || (CTC_OAM_MEP_TYPE_MPLS_BFD == p_mep_info->mep_type)
                                                              || (CTC_OAM_MEP_TYPE_MPLS_TP_BFD == p_mep_info->mep_type))))
        {
            return CTC_E_NONE;
        }

        p_event->oam_event_entry[entry_idx].lmep_index = p_sys_lmep_com->lmep_index;
         /*p_event->oam_event_entry[entry_idx].local_endpoint_id   = SYS_OAM_GET_ID_BY_IDX(p_sys_lmep_com->lmep_index);*/
    }

    /*mep type*/
    if (p_sys_lmep_com->p_sys_maid)
    {
        sal_memcpy(p_event->oam_event_entry[entry_idx].maid, p_sys_lmep_com->p_sys_maid->maid, p_sys_lmep_com->p_sys_maid->maid_len);
    }

    /*get key*/
    if (p_sys_lmep_com->p_sys_chan)
    {
        if ((CTC_OAM_MEP_TYPE_ETH_1AG == p_mep_info->mep_type)
            ||(CTC_OAM_MEP_TYPE_ETH_Y1731 == p_mep_info->mep_type))
        {
            p_event->oam_event_entry[entry_idx].oam_key.u.eth.vlan_id   = p_mep_info->lmep.y1731_lmep.vlan_id;
            p_event->oam_event_entry[entry_idx].oam_key.u.eth.md_level  = p_mep_info->lmep.y1731_lmep.level;
            p_event->oam_event_entry[entry_idx].oam_key.u.eth.gport     = ((sys_oam_chan_eth_t*)p_sys_lmep_com->p_sys_chan)->key.gport;
        }
        else if ((CTC_OAM_MEP_TYPE_IP_BFD == p_mep_info->mep_type)
                || (CTC_OAM_MEP_TYPE_MPLS_BFD == p_mep_info->mep_type))
        {
            p_event->oam_event_entry[entry_idx].oam_key.u.bfd.discr = p_mep_info->lmep.bfd_lmep.local_discr;
        }
        else if ((CTC_OAM_MEP_TYPE_MPLS_TP_Y1731 == p_mep_info->mep_type)
                || (CTC_OAM_MEP_TYPE_MPLS_TP_BFD == p_mep_info->mep_type))
        {
            if(((sys_oam_chan_tp_t*)p_sys_lmep_com->p_sys_chan)->key.section_oam)
            {
                CTC_SET_FLAG(p_event->oam_event_entry[entry_idx].oam_key.flag, CTC_OAM_KEY_FLAG_LINK_SECTION_OAM);
            }
            else
            {
                CTC_UNSET_FLAG(p_event->oam_event_entry[entry_idx].oam_key.flag, CTC_OAM_KEY_FLAG_LINK_SECTION_OAM);
            }
            p_event->oam_event_entry[entry_idx].oam_key.u.tp.label      = ((sys_oam_chan_tp_t*)p_sys_lmep_com->p_sys_chan)->key.label;
            p_event->oam_event_entry[entry_idx].oam_key.u.tp.mpls_spaceid    = ((sys_oam_chan_tp_t*)p_sys_lmep_com->p_sys_chan)->key.spaceid;
            p_event->oam_event_entry[entry_idx].oam_key.u.tp.gport_or_l3if_id = ((sys_oam_chan_tp_t*)p_sys_lmep_com->p_sys_chan)->key.gport_l3if_id;
        }
    }


    if ((CTC_OAM_MEP_TYPE_ETH_1AG == p_mep_info->mep_type)
        || (CTC_OAM_MEP_TYPE_ETH_Y1731 == p_mep_info->mep_type)
        || (CTC_OAM_MEP_TYPE_MPLS_TP_Y1731 == p_mep_info->mep_type))
    {
        p_event->oam_event_entry[entry_idx].lmep_id = p_mep_info->lmep.y1731_lmep.mep_id;

        if (p_mep_info->is_rmep)
        {
            p_event->oam_event_entry[entry_idx].rmep_id = p_mep_info->rmep.y1731_rmep.rmep_id;

            d_loc             = p_mep_info->rmep.y1731_rmep.d_loc;
            first_pkt_rx      = p_mep_info->rmep.y1731_rmep.first_pkt_rx;
            d_unexp_period    = p_mep_info->rmep.y1731_rmep.d_unexp_period;
            mac_status_change = p_mep_info->rmep.y1731_rmep.mac_status_change;
            ma_sa_mismatch    = p_mep_info->rmep.y1731_rmep.ma_sa_mismatch;
            last_rdi          = p_mep_info->rmep.y1731_rmep.last_rdi;
            d_csf             = p_mep_info->rmep.y1731_rmep.d_csf;
        }

        d_meg_lvl     = p_mep_info->lmep.y1731_lmep.d_meg_lvl;
        d_mismerge    = p_mep_info->lmep.y1731_lmep.d_mismerge;
        d_unexp_mep   = p_mep_info->lmep.y1731_lmep.d_unexp_mep;
        present_rdi   = p_mep_info->lmep.y1731_lmep.present_rdi;

    }
    else if ((CTC_OAM_MEP_TYPE_IP_BFD == p_mep_info->mep_type)
            || (CTC_OAM_MEP_TYPE_MPLS_BFD == p_mep_info->mep_type)
            || (CTC_OAM_MEP_TYPE_MPLS_TP_BFD == p_mep_info->mep_type))
    {
         /*p_event->oam_event_entry[entry_idx].lmep_id = p_mep_info->lmep.y1731_lmep.mep_id;*/

        if (p_mep_info->is_rmep)
        {
             /*p_event->oam_event_entry[entry_idx].rmep_id = p_mep_info->rmep.y1731_rmep.rmep_id;*/
            first_pkt_rx    = p_mep_info->rmep.bfd_rmep.first_pkt_rx;
            mis_connect     = p_mep_info->rmep.bfd_rmep.mis_connect;
            d_csf           = p_mep_info->lmep.bfd_lmep.d_csf;
            remote_state    = p_mep_info->rmep.bfd_rmep.remote_state;
            p_event->oam_event_entry[entry_idx].remote_diag = p_mep_info->rmep.bfd_rmep.remote_diag;
            p_event->oam_event_entry[entry_idx].remote_state = remote_state;
        }
        state           = p_mep_info->lmep.bfd_lmep.loacl_state;

        p_event->oam_event_entry[entry_idx].local_diag = p_mep_info->lmep.bfd_lmep.local_diag;
        p_event->oam_event_entry[entry_idx].local_state = state;
    }

    /*event*/
    bitmap = &p_event->oam_event_entry[entry_idx].event_bmp;
    d_meg_lvl ? (CTC_SET_FLAG((*bitmap), CTC_OAM_DEFECT_UNEXPECTED_LEVEL)) : (CTC_UNSET_FLAG((*bitmap), CTC_OAM_DEFECT_UNEXPECTED_LEVEL));
    d_mismerge ? (CTC_SET_FLAG((*bitmap), CTC_OAM_DEFECT_MISMERGE)) : (CTC_UNSET_FLAG((*bitmap), CTC_OAM_DEFECT_MISMERGE));
    d_unexp_mep ? (CTC_SET_FLAG((*bitmap), CTC_OAM_DEFECT_UNEXPECTED_MEP)) : (CTC_UNSET_FLAG((*bitmap), CTC_OAM_DEFECT_UNEXPECTED_MEP));
    present_rdi ? (CTC_SET_FLAG((*bitmap), CTC_OAM_DEFECT_EVENT_RDI_TX)) : (CTC_UNSET_FLAG((*bitmap), CTC_OAM_DEFECT_EVENT_RDI_TX));

    if (CTC_OAM_BFD_STATE_DOWN == state)
    {
        CTC_SET_FLAG((*bitmap), CTC_OAM_DEFECT_EVENT_BFD_DOWN);
        CTC_UNSET_FLAG((*bitmap), CTC_OAM_DEFECT_EVENT_BFD_INIT);
        CTC_UNSET_FLAG((*bitmap), CTC_OAM_DEFECT_EVENT_BFD_UP);
    }
    else if (CTC_OAM_BFD_STATE_INIT == state)
    {
        CTC_SET_FLAG((*bitmap), CTC_OAM_DEFECT_EVENT_BFD_INIT);
        CTC_UNSET_FLAG((*bitmap), CTC_OAM_DEFECT_EVENT_BFD_DOWN);
        CTC_UNSET_FLAG((*bitmap), CTC_OAM_DEFECT_EVENT_BFD_UP);
    }
    else if (CTC_OAM_BFD_STATE_UP == state)
    {
        CTC_SET_FLAG((*bitmap), CTC_OAM_DEFECT_EVENT_BFD_UP);
        CTC_UNSET_FLAG((*bitmap), CTC_OAM_DEFECT_EVENT_BFD_INIT);
        CTC_UNSET_FLAG((*bitmap), CTC_OAM_DEFECT_EVENT_BFD_DOWN);
    }

    if (p_mep_info->is_rmep)
    {
        d_loc ? (CTC_SET_FLAG((*bitmap), CTC_OAM_DEFECT_DLOC)) : (CTC_UNSET_FLAG((*bitmap), CTC_OAM_DEFECT_DLOC));
        first_pkt_rx ? (CTC_SET_FLAG((*bitmap), CTC_OAM_DEFECT_EVENT_RX_FIRST_PKT)) : (CTC_UNSET_FLAG((*bitmap), CTC_OAM_DEFECT_EVENT_RX_FIRST_PKT));
        d_unexp_period ? (CTC_SET_FLAG((*bitmap), CTC_OAM_DEFECT_UNEXPECTED_PERIOD)) : (CTC_UNSET_FLAG((*bitmap), CTC_OAM_DEFECT_UNEXPECTED_PERIOD));
        mac_status_change ? (CTC_SET_FLAG((*bitmap), CTC_OAM_DEFECT_MAC_STATUS_CHANGE)) : (CTC_UNSET_FLAG((*bitmap), CTC_OAM_DEFECT_MAC_STATUS_CHANGE));
        ma_sa_mismatch ? (CTC_SET_FLAG((*bitmap), CTC_OAM_DEFECT_SRC_MAC_MISMATCH)) : (CTC_UNSET_FLAG((*bitmap), CTC_OAM_DEFECT_SRC_MAC_MISMATCH));
        last_rdi ? (CTC_SET_FLAG((*bitmap), CTC_OAM_DEFECT_EVENT_RDI_RX)) : (CTC_UNSET_FLAG((*bitmap), CTC_OAM_DEFECT_EVENT_RDI_RX));
        d_csf ? (CTC_SET_FLAG((*bitmap), CTC_OAM_DEFECT_CSF)) : (CTC_UNSET_FLAG((*bitmap), CTC_OAM_DEFECT_CSF));
        mis_connect ? (CTC_SET_FLAG((*bitmap), CTC_OAM_DEFECT_EVENT_BFD_MIS_CONNECT)) : (CTC_UNSET_FLAG((*bitmap), CTC_OAM_DEFECT_EVENT_BFD_MIS_CONNECT));
    }

    p_event->valid_entry_num++;

    return CTC_E_NONE;
}

int32
_sys_greatbelt_oam_set_common_property(uint8 lchip, void* p_oam_property)
{
    uint32 cfg_type = 0;
    uint32 value = 0;
    uint32 cmd = 0;
    uint8  rdi_index        = 0;
    uint8  defect_type      = 0;
    uint8  defect_sub_type  = 0;
    uint8  idx              = 0;
    uint32 dloc_rdi         = 0;

    ctc_oam_com_property_t* p_common_property = NULL;
    oam_rx_proc_ether_ctl_t     oam_rx_proc_ether_ctl;
    oam_update_aps_ctl_t        oam_update_aps_ctl;
    oam_error_defect_ctl_t      oam_error_defect_ctl;

    p_common_property = &(((ctc_oam_property_t*)p_oam_property)->u.common);
    cfg_type    = p_common_property->cfg_type;
    value   = p_common_property->value;

    if (CTC_OAM_COM_PRO_TYPE_TRIG_APS_MSG == cfg_type)
    {
        sal_memset(&oam_update_aps_ctl, 0, sizeof(oam_update_aps_ctl_t));
        cmd = DRV_IOR(OamUpdateApsCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &oam_update_aps_ctl));
        oam_update_aps_ctl.eth_mep_aps_sig_fail_mask    = 0;
        oam_update_aps_ctl.eth_rmep_aps_sig_fail_mask   = 0;

        if (CTC_FLAG_ISSET(value, CTC_OAM_TRIG_APS_MSG_FLAG_INTF_STATE))
        {
            CTC_BIT_SET(oam_update_aps_ctl.eth_rmep_aps_sig_fail_mask, 0);
        }

        if (CTC_FLAG_ISSET(value, CTC_OAM_TRIG_APS_MSG_FLAG_PORT_STATE))
        {
            CTC_BIT_SET(oam_update_aps_ctl.eth_rmep_aps_sig_fail_mask, 1);
        }

        if (CTC_FLAG_ISSET(value, CTC_OAM_TRIG_APS_MSG_FLAG_RDI))
        {
            CTC_BIT_SET(oam_update_aps_ctl.eth_rmep_aps_sig_fail_mask, 2);
        }

        if (CTC_FLAG_ISSET(value, CTC_OAM_TRIG_APS_MSG_FLAG_D_LOC))
        {
            CTC_BIT_SET(oam_update_aps_ctl.eth_rmep_aps_sig_fail_mask, 3);
        }

        if (CTC_FLAG_ISSET(value, CTC_OAM_TRIG_APS_MSG_FLAG_D_UNEXP_PERIOD))
        {
            CTC_BIT_SET(oam_update_aps_ctl.eth_rmep_aps_sig_fail_mask, 4);
        }

        if (CTC_FLAG_ISSET(value, CTC_OAM_TRIG_APS_MSG_FLAG_D_MEG_LVL))
        {
            CTC_BIT_SET(oam_update_aps_ctl.eth_mep_aps_sig_fail_mask, 0);
        }

        if (CTC_FLAG_ISSET(value, CTC_OAM_TRIG_APS_MSG_FLAG_D_MISMERGE))
        {
            CTC_BIT_SET(oam_update_aps_ctl.eth_mep_aps_sig_fail_mask, 1);
        }

        if (CTC_FLAG_ISSET(value, CTC_OAM_TRIG_APS_MSG_FLAG_D_UNEXP_MEP))
        {
            CTC_BIT_SET(oam_update_aps_ctl.eth_mep_aps_sig_fail_mask, 2);
        }

        if (CTC_FLAG_ISSET(value, CTC_OAM_TRIG_APS_MSG_FLAG_BFD_UP_TO_DOWN))
        {
            oam_update_aps_ctl.bfd_rmep_up_to_down = 1;
        }
        else
        {
            oam_update_aps_ctl.bfd_rmep_up_to_down = 0;
        }

        cmd = DRV_IOW(OamUpdateApsCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &oam_update_aps_ctl));

    }
    else if (CTC_OAM_COM_PRO_TYPE_DEFECT_TO_RDI == cfg_type)
    {
        sal_memset(&oam_rx_proc_ether_ctl, 0, sizeof(oam_rx_proc_ether_ctl_t));

        cmd = DRV_IOR(OamRxProcEtherCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &oam_rx_proc_ether_ctl));

        oam_rx_proc_ether_ctl.ether_defect_to_rdi0 = 0;
        oam_rx_proc_ether_ctl.ether_defect_to_rdi1 = 0;

        for (idx = 0; idx < CTC_GREATBELT_OAM_DEFECT_NUM; idx++)
        {
            if (CTC_IS_BIT_SET(value, idx))
            {
                _sys_greatbelt_oam_get_rdi_defect_type(lchip, (1 << idx), &defect_type, &defect_sub_type);

                rdi_index = (defect_type << 3) | defect_sub_type;

                if (rdi_index < 32)
                {
                    CTC_BIT_SET(oam_rx_proc_ether_ctl.ether_defect_to_rdi0, rdi_index);
                }
                else
                {
                    CTC_BIT_SET(oam_rx_proc_ether_ctl.ether_defect_to_rdi1, (rdi_index - 32));
                }
            }
        }

        cmd = DRV_IOW(OamRxProcEtherCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &oam_rx_proc_ether_ctl));

        /* dloc trigger rdi*/
        if (CTC_FLAG_ISSET(value, CTC_OAM_DEFECT_DLOC))
        {
            cmd = DRV_IOW(OamUpdateCtl_t, OamUpdateCtl_GenRdiByDloc_f);
            dloc_rdi = 1;
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &dloc_rdi));
        }
        else
        {
            cmd = DRV_IOW(OamUpdateCtl_t, OamUpdateCtl_GenRdiByDloc_f);
            dloc_rdi = 0;
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &dloc_rdi));
        }
    }
    else if (CTC_OAM_COM_PRO_TYPE_EXCEPTION_TO_CPU == cfg_type)
    {
        cmd = DRV_IOW(OamHeaderEditCtl_t, OamHeaderEditCtl_CpuExceptionEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
    }
    else if (CTC_OAM_COM_PRO_TYPE_DEFECT_TO_CPU == cfg_type)
    {
        sal_memset(&oam_error_defect_ctl, 0, sizeof(oam_error_defect_ctl_t));
        cmd = DRV_IOR(OamErrorDefectCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &oam_error_defect_ctl));

        oam_error_defect_ctl.report_defect_en0 = 0;
        oam_error_defect_ctl.report_defect_en1 = 0;

        for (idx = 0; idx < CTC_GREATBELT_OAM_DEFECT_NUM; idx++)
        {
            if (CTC_IS_BIT_SET(value, idx))
            {
                sys_oam_defect_type_t defect_info;
                uint8 cnt = 0;

                sal_memset(&defect_info, 0, sizeof(sys_oam_defect_type_t));
                _sys_greatbelt_oam_get_defect_type(lchip, (1 << idx), &defect_info);

                for (cnt = 0; cnt < defect_info.entry_num; cnt++)
                {
                    rdi_index = (defect_info.defect_type[cnt] << 3) | defect_info.defect_sub_type[cnt];

                    if (rdi_index < 32)
                    {
                        CTC_BIT_SET(oam_error_defect_ctl.report_defect_en0, rdi_index);
                    }
                    else
                    {
                        CTC_BIT_SET(oam_error_defect_ctl.report_defect_en1, (rdi_index - 32));
                    }
                }
            }
        }

        cmd = DRV_IOW(OamErrorDefectCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &oam_error_defect_ctl));
    }
    else if (CTC_OAM_COM_PRO_TYPE_TIMER_DISABLE == cfg_type)
    {
        value = (value >= 1)? 0 : 1;
        cmd = DRV_IOW(OamUpdateCtl_t, OamUpdateCtl_UpdEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
        cmd = DRV_IOW(OamUpdateCtl_t, OamUpdateCtl_BfdUpdEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
        g_gb_oam_master[lchip]->timer_update_disable = !value;
    }
    else if (CTC_OAM_COM_PRO_TYPE_MASTER_GCHIP == cfg_type)
    {
        SYS_GLOBAL_CHIPID_CHECK(value);
        CTC_ERROR_RETURN(ctc_hash_traverse(g_gb_oam_master[lchip]->chan_hash, (hash_traversal_fn)_sys_greatbelt_oam_lmep_update_master_chip, &value));
    }

    return CTC_E_NONE;
}

int32
_sys_greatbelt_oam_tp_section_init(uint8 lchip, uint8 use_port)
{
    uint32 is_port  = 0;
    uint32 cmd      = 0;

    SYS_OAM_INIT_CHECK(lchip);

    is_port = (1 == use_port) ? 1: 0;
    cmd = DRV_IOW(IpeLookupCtl_t, IpeLookupCtl_MplsSectionOamUsePort_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &is_port));

    cmd = DRV_IOW(EpeAclQosCtl_t, EpeAclQosCtl_MplsSectionOamUsePort_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &is_port));

    cmd = DRV_IOW(OamRxProcEtherCtl_t, OamRxProcEtherCtl_PortbasedSectionOam_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &is_port));

    g_gb_oam_master[lchip]->com_oam_global.tp_section_oam_based_l3if = (1 == use_port) ? 0: 1;

    return CTC_E_NONE;
}


int32
_sys_greatbelt_oam_com_reg_init(uint8 lchip)
{
    uint32 cmd = 0;
    uint8 gchip_id = 0;
    uint32 field_val    = 0;
    uint16 mep_3ms_num  = 0;
    uint32 freq         = 0;

    oam_update_ctl_t        oam_update_ctl;
    oam_ether_cci_ctl_t     oam_ether_cci_ctl;
    oam_update_aps_ctl_t    oam_update_aps_ctl;

    if (g_gb_oam_master[lchip]->no_mep_resource)
    {
        return CTC_E_NONE;
    }

    sal_memset(&oam_update_aps_ctl, 0, sizeof(oam_update_aps_ctl_t));

    CTC_BIT_SET(field_val, CTC_OAM_EXCP_SOME_MAC_STATUS_DEFECT);
    CTC_BIT_SET(field_val, CTC_OAM_EXCP_OPTION_CCM_TLV);
    CTC_BIT_SET(field_val, CTC_OAM_EXCP_SLOW_OAM_TO_CPU);
    CTC_BIT_SET(field_val, CTC_OAM_EXCP_SMAC_MISMATCH);
    CTC_BIT_SET(field_val, CTC_OAM_EXCP_APS_PDU_TO_CPU);
    CTC_BIT_SET(field_val, CTC_OAM_EXCP_DM_TO_CPU);
    CTC_BIT_SET(field_val, CTC_OAM_EXCP_EQUAL_LBR);
    CTC_BIT_SET(field_val, CTC_OAM_EXCP_LM_TO_CPU);
    CTC_BIT_SET(field_val, CTC_OAM_EXCP_ETH_TST_TO_CPU);
    CTC_BIT_SET(field_val, CTC_OAM_EXCP_BIG_INTERVAL_OR_SW_MEP_TO_CPU);
    CTC_BIT_SET(field_val, CTC_OAM_EXCP_MPLS_TP_DLM_TO_CPU);
    CTC_BIT_SET(field_val, CTC_OAM_EXCP_MPLS_TP_DM_DLMDM_TO_CPU);
    CTC_BIT_SET(field_val, CTC_OAM_EXCP_CSF_TO_CPU);
    CTC_BIT_SET(field_val, CTC_OAM_EXCP_MCC_PDU_TO_CPU);
    CTC_BIT_SET(field_val, CTC_OAM_EXCP_EQUAL_LTM_LTR_TO_CPU);
    CTC_BIT_SET(field_val, CTC_OAM_EXCP_LBM_MAC_DA_MEP_ID_CHECK_FAIL);
    CTC_BIT_SET(field_val, CTC_OAM_EXCP_LEARNING_BFD_TO_CPU);
    CTC_BIT_SET(field_val, CTC_OAM_EXCP_BFD_TIMER_NEGOTIATION);
    CTC_BIT_SET(field_val, CTC_OAM_EXCP_SCC_PDU_TO_CPU);

    cmd = DRV_IOW(OamHeaderEditCtl_t, OamHeaderEditCtl_CpuExceptionEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

/*OamEngine Update*/
    freq = sys_greatbelt_get_core_freq(lchip);
    mep_3ms_num = TABLE_MAX_INDEX(DsEthMep_t) - P_COMMON_OAM_MASTER_GLB(lchip).mep_1ms_num;
    sal_memset(&oam_update_ctl, 0, sizeof(oam_update_ctl_t));
    cmd = DRV_IOR(OamUpdateCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &oam_update_ctl));
    oam_update_ctl.upd_en               = 0;
    oam_update_ctl.ccm_min_ptr          = 2;
    if(P_COMMON_OAM_MASTER_GLB(lchip).mep_1ms_num == 0)
    {
        oam_update_ctl.oam_up_phy_num       = (TABLE_MAX_INDEX(DsEthMep_t) - 1) - oam_update_ctl.ccm_min_ptr + 1 - 1;
        oam_update_ctl.upd_interval         = (freq/10 * 8333)/(oam_update_ctl.oam_up_phy_num) - 1;
        oam_update_ctl.ccm_max_ptr          = (freq/10 * 8333)/(oam_update_ctl.upd_interval + 1) + oam_update_ctl.ccm_min_ptr - 1;
    }
    else
    {
        oam_update_ctl.oam_up_phy_num       = (mep_3ms_num - 1) - oam_update_ctl.ccm_min_ptr + 1;
        if (oam_update_ctl.oam_up_phy_num)
        {
            oam_update_ctl.upd_interval         = (freq / 10 * 8333) / (oam_update_ctl.oam_up_phy_num) - 1;
            oam_update_ctl.ccm_max_ptr          = (freq / 10 * 8333) / (oam_update_ctl.upd_interval + 1) + oam_update_ctl.ccm_min_ptr - 1;
        }
    }

    if (P_COMMON_OAM_MASTER_GLB(lchip).mep_1ms_num > 0)
    {
        oam_update_ctl.bfd_upd_en           = 0;
        oam_update_ctl.bfd_minptr           = mep_3ms_num;
        oam_update_ctl.oam_bfd_phy_num      = P_COMMON_OAM_MASTER_GLB(lchip).mep_1ms_num - 1;
        if (oam_update_ctl.oam_bfd_phy_num)
        {
            oam_update_ctl.bfd_upd_interval     = (freq *1000) / (oam_update_ctl.oam_bfd_phy_num) - 1;
            oam_update_ctl.bfd_max_ptr          = (freq *1000) / (oam_update_ctl.bfd_upd_interval + 1) + oam_update_ctl.bfd_minptr - 1;
        }
   }
    oam_update_ctl.sf_fail_while_cfg0       = 1;
    oam_update_ctl.sf_fail_while_cfg1       = 3;

    cmd = DRV_IOW(OamUpdateCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &oam_update_ctl));

    sal_memset(&oam_ether_cci_ctl, 0, sizeof(oam_ether_cci_ctl));
    oam_ether_cci_ctl.quater_cci2_interval = (oam_update_ctl.upd_interval + 1) * 3 *(oam_update_ctl.ccm_max_ptr - oam_update_ctl.ccm_min_ptr + 1);
    oam_ether_cci_ctl.relative_cci3_interval = 9;
    oam_ether_cci_ctl.relative_cci4_interval = 9;
    oam_ether_cci_ctl.relative_cci5_interval = 9;
    oam_ether_cci_ctl.relative_cci6_interval = 5;
    oam_ether_cci_ctl.relative_cci7_interval = 9;
    cmd = DRV_IOW(OamEtherCciCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &oam_ether_cci_ctl));

    /*enable update*/
    field_val = (oam_update_ctl.oam_up_phy_num) ? 1 : 0;
    cmd = DRV_IOW(OamUpdateCtl_t, OamUpdateCtl_UpdEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    if (P_COMMON_OAM_MASTER_GLB(lchip).mep_1ms_num > 0)
    {
        field_val = (oam_update_ctl.oam_bfd_phy_num) ? 1 : 0;
        cmd = DRV_IOW(OamUpdateCtl_t, OamUpdateCtl_BfdUpdEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    }

    cmd = DRV_IOR(OamUpdateApsCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &oam_update_aps_ctl));
    oam_update_aps_ctl.global_aps_en        = 1;
    sys_greatbelt_get_gchip_id(lchip, &gchip_id);
    oam_update_aps_ctl.dest_chip_id         = gchip_id;
    oam_update_aps_ctl.dest_port_id         = SYS_RESERVE_PORT_ID_CPU_OAM;
    sys_greatbelt_get_gchip_id(lchip, &gchip_id);
    oam_update_aps_ctl.src_chip_id          = gchip_id;
    oam_update_aps_ctl.tpid                 = 0x8100;
    oam_update_aps_ctl.vlan                 = 0x3F;
    oam_update_aps_ctl.bfd_rmep_up_to_down  = 1;
    /*
    oam_update_aps_ctl.sig_fail_next_hop_ptr
    oam_update_aps_ctl.eth_mep_aps_sig_fail_mask
    oam_update_aps_ctl.eth_rmep_aps_sig_fail_mask
    */
    cmd = DRV_IOW(OamUpdateApsCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &oam_update_aps_ctl));

    return CTC_E_NONE;

}

int32
sys_greatbelt_oam_tcam_reinit(uint8 lchip, uint8 is_add)
{
    ipe_lookup_ctl_t        ipe_lookup_ctl;
    epe_acl_qos_ctl_t       epe_acl_qos_ctl;
    uint32 cmd      = 0;
    if (is_add)
    {
        sal_memset(&ipe_lookup_ctl, 0, sizeof(ipe_lookup_ctl_t));
        cmd = DRV_IOR(IpeLookupCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_lookup_ctl));
        ipe_lookup_ctl.mpls_section_oam_use_port = (P_COMMON_OAM_MASTER_GLB(lchip).tp_section_oam_based_l3if ? 0 : 1);
        ipe_lookup_ctl.bfd_single_hop_ttl        = 255;
        ipe_lookup_ctl.ach_cc_use_label          = 1;
        ipe_lookup_ctl.ach_cv_use_label          = 1;
        ipe_lookup_ctl.mpls_bfd_use_label        = 0;
        /*set bit for ignore mpls bfd ttl*/
        CTC_BIT_SET(ipe_lookup_ctl.fcoe_lookup_ctl1, 6);

        cmd = DRV_IOW(IpeLookupCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_lookup_ctl));

        sal_memset(&epe_acl_qos_ctl, 0, sizeof(epe_acl_qos_ctl_t));
        cmd = DRV_IOR(EpeAclQosCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_acl_qos_ctl));
        epe_acl_qos_ctl.oam_alert_label1            = 13;
        epe_acl_qos_ctl.mpls_section_oam_use_port   = (P_COMMON_OAM_MASTER_GLB(lchip).tp_section_oam_based_l3if ? 0 : 1);
        epe_acl_qos_ctl.oam_lookup_user_vlan_id = 1;
        cmd = DRV_IOW(EpeAclQosCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_acl_qos_ctl));
    }
    else
    {

    }
    return CTC_E_NONE;
}

int32
_sys_greatbelt_oam_db_init(uint8 lchip, ctc_oam_global_t* p_com_glb)
{
    uint32  fun_num            = 0;
    uint32 chan_entry_num     = 0;
    uint32 mep_entry_num      = 0;
    uint32 ma_entry_num       = 0;
    uint32 ma_name_entry_num  = 0;
    uint32 lm_entry_num       = 0;
    uint32 lm_entry_num0      = 0;
    uint32 lm_entry_num1      = 0;
    uint8  oam_type           = 0;
    uint16 mep_3ms_num        = 0;
    uint8 no_mep_resource = 0;
    int32 ret = CTC_E_NONE;
    sys_greatbelt_opf_t opf   = {0};

    /*************************************************
     *init opf for related tables
     *************************************************/

    CTC_ERROR_RETURN(sys_greatbelt_opf_init(lchip, OPF_OAM_CHAN, 1));
    CTC_ERROR_RETURN(sys_greatbelt_opf_init(lchip, OPF_OAM_MEP_LMEP, 1));
    CTC_ERROR_RETURN(sys_greatbelt_opf_init(lchip, OPF_OAM_MEP_BFD, 1));
    CTC_ERROR_RETURN(sys_greatbelt_opf_init(lchip, OPF_OAM_MA, 1));
    CTC_ERROR_RETURN(sys_greatbelt_opf_init(lchip, OPF_OAM_MA_NAME, 1));
    CTC_ERROR_RETURN(sys_greatbelt_opf_init(lchip, OPF_OAM_LM, 1));


    CTC_ERROR_RETURN(sys_greatbelt_ftm_get_table_entry_num(lchip, DsEthOamChan_t,  &chan_entry_num));
    CTC_ERROR_RETURN(sys_greatbelt_ftm_get_table_entry_num(lchip, DsEthMep_t,  &mep_entry_num));
    CTC_ERROR_RETURN(sys_greatbelt_ftm_get_table_entry_num(lchip, DsMa_t,  &ma_entry_num));
    CTC_ERROR_RETURN(sys_greatbelt_ftm_get_table_entry_num(lchip, DsMaName_t,  &ma_name_entry_num));
    CTC_ERROR_RETURN(sys_greatbelt_ftm_get_table_entry_num(lchip, DsOamLmStats_t,  &lm_entry_num));
    CTC_ERROR_RETURN(sys_greatbelt_ftm_get_table_entry_num(lchip, DsOamLmStats0_t, &lm_entry_num0));
    CTC_ERROR_RETURN(sys_greatbelt_ftm_get_table_entry_num(lchip, DsOamLmStats1_t, &lm_entry_num1));

    lm_entry_num += lm_entry_num0;
    lm_entry_num += lm_entry_num1;
    if ((0 == mep_entry_num) || (0 == ma_entry_num) || (0 == ma_name_entry_num))
    {
        no_mep_resource = 1;
        if (chan_entry_num)
        {
            opf.pool_type = OPF_OAM_CHAN;
            sys_greatbelt_opf_init_offset(lchip, &opf, 0, chan_entry_num);
        }
        else
        {
            return CTC_E_NONE;
        }
        /*no need to alloc MEP resource on line card in Fabric system*/
    }
    else
    {
        opf.pool_index = 0;

        opf.pool_type = OPF_OAM_CHAN;
        CTC_ERROR_RETURN(sys_greatbelt_opf_init_offset(lchip, &opf, 0, chan_entry_num));

        mep_3ms_num = mep_entry_num - p_com_glb->mep_1ms_num;

        opf.pool_type = OPF_OAM_MEP_LMEP;
        CTC_ERROR_RETURN(sys_greatbelt_opf_init_offset(lchip, &opf, 2, (mep_3ms_num - 2)));

        opf.pool_type = OPF_OAM_MEP_BFD;
        CTC_ERROR_RETURN(sys_greatbelt_opf_init_offset(lchip, &opf, mep_3ms_num, (mep_entry_num - mep_3ms_num)));

        opf.pool_type = OPF_OAM_MA;
        CTC_ERROR_RETURN(sys_greatbelt_opf_init_offset(lchip, &opf, 0, ma_entry_num));

        opf.pool_type = OPF_OAM_MA_NAME;
        CTC_ERROR_RETURN(sys_greatbelt_opf_init_offset(lchip, &opf, 0, ma_name_entry_num));

        opf.pool_type = OPF_OAM_LM;
        CTC_ERROR_RETURN(sys_greatbelt_opf_init_offset(lchip, &opf, 0, lm_entry_num));
    }
    /*************************************************
     *init global param
     *************************************************/
    g_gb_oam_master[lchip] = (sys_oam_master_t*)mem_malloc(MEM_OAM_MODULE, sizeof(sys_oam_master_t));
    if (NULL == g_gb_oam_master[lchip])
    {
        goto err;
    }
    sal_memset(g_gb_oam_master[lchip], 0 ,sizeof(sys_oam_master_t));
    g_gb_oam_master[lchip]->no_mep_resource = no_mep_resource;

    fun_num = CTC_OAM_MEP_TYPE_MAX * SYS_OAM_MODULE_MAX * SYS_OAM_OP_MAX;
    g_gb_oam_master[lchip]->p_fun_table = (SYS_OAM_CALL_FUNC_P*)
        mem_malloc(MEM_OAM_MODULE, fun_num * sizeof(SYS_OAM_CALL_FUNC_P));
    if (NULL == g_gb_oam_master[lchip]->p_fun_table)
    {
        goto err;
    }

    sal_memset(g_gb_oam_master[lchip]->p_fun_table, 0, fun_num * sizeof(SYS_OAM_CALL_FUNC_P));


    fun_num = CTC_OAM_MEP_TYPE_MAX * SYS_OAM_CHECK_MAX;
    g_gb_oam_master[lchip]->p_check_fun_table = (SYS_OAM_CHECK_CALL_FUNC_P*)
        mem_malloc(MEM_OAM_MODULE, fun_num * sizeof(SYS_OAM_CHECK_CALL_FUNC_P));
    if (NULL == g_gb_oam_master[lchip]->p_check_fun_table)
    {
        goto err;
    }

    sal_memset(g_gb_oam_master[lchip]->p_check_fun_table, 0, fun_num * sizeof(SYS_OAM_CHECK_CALL_FUNC_P));

    if (!g_gb_oam_master[lchip]->no_mep_resource)
    {
        g_gb_oam_master[lchip]->maid_hash = ctc_hash_create((ma_name_entry_num / (SYS_OAM_MAID_HASH_BLOCK_SIZE * 2)),
                                                            SYS_OAM_MAID_HASH_BLOCK_SIZE,
                                                            (hash_key_fn)_sys_greatbelt_oam_maid_build_key,
                                                            (hash_cmp_fn)_sys_greatbelt_oam_maid_cmp);

        if (NULL == g_gb_oam_master[lchip]->maid_hash)
        {
            goto err;
        }
    }

    g_gb_oam_master[lchip]->chan_hash = ctc_hash_create((chan_entry_num / SYS_OAM_CHAN_HASH_BLOCK_SIZE),
                                              SYS_OAM_CHAN_HASH_BLOCK_SIZE,
                                              (hash_key_fn)_sys_greatbelt_oam_chan_build_key,
                                              (hash_cmp_fn)_sys_greatbelt_oam_chan_cmp);
    if (NULL == g_gb_oam_master[lchip]->chan_hash)
    {
        goto err;
    }

#define SYS_OAM_MEP_VEC_BLOCK_NUM 128

    if (!g_gb_oam_master[lchip]->no_mep_resource)
    {
        g_gb_oam_master[lchip]->mep_vec =
        ctc_vector_init(SYS_OAM_MEP_VEC_BLOCK_NUM, mep_entry_num / SYS_OAM_MEP_VEC_BLOCK_NUM);
        if (NULL == g_gb_oam_master[lchip]->mep_vec)
        {
            goto err;
        }
    }
    if (CTC_E_NONE != sal_mutex_create(&(g_gb_oam_master[lchip]->oam_mutex)))
    {
        goto err;
    }

    if (CTC_E_NONE != sal_sem_create(&g_gb_oam_master[lchip]->bfd_state_sem, 0))
    {
        goto err;
    }

    sal_memcpy(&(g_gb_oam_master[lchip]->com_oam_global), p_com_glb, sizeof(ctc_oam_global_t));


    /*temp for testing !!!!!!*/
    CTC_ERROR_GOTO(_sys_greatbelt_oam_defect_scan_en(lchip, TRUE), ret, err);
    CTC_ERROR_GOTO(_sys_greatbelt_oam_com_reg_init(lchip), ret, err);

    oam_type = CTC_OAM_PROPERTY_TYPE_COMMON;

    SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_GLOB, SYS_OAM_CONFIG) = _sys_greatbelt_oam_set_common_property;

    sys_greatbelt_ftm_tcam_cb_register(lchip, SYS_FTM_TCAM_KEY_OAM, sys_greatbelt_oam_tcam_reinit);
    goto success;

    /*************************************************
     * Error rollback
     *************************************************/

err:

    if (NULL != g_gb_oam_master[lchip]->bfd_state_sem)
    {
        sal_sem_destroy(g_gb_oam_master[lchip]->bfd_state_sem);
        g_gb_oam_master[lchip]->bfd_state_sem = NULL;
    }

    if (NULL != g_gb_oam_master[lchip]->oam_mutex)
    {
        sal_mutex_destroy(g_gb_oam_master[lchip]->oam_mutex);
        g_gb_oam_master[lchip]->oam_mutex = NULL;
    }

    if (NULL != g_gb_oam_master[lchip]->mep_vec)
    {
        ctc_vector_release(g_gb_oam_master[lchip]->mep_vec);
    }

    if (NULL != g_gb_oam_master[lchip]->chan_hash)
    {
        ctc_hash_free(g_gb_oam_master[lchip]->chan_hash);
    }

    if (NULL != g_gb_oam_master[lchip]->maid_hash)
    {
        ctc_hash_free(g_gb_oam_master[lchip]->maid_hash);
    }

    if (NULL != g_gb_oam_master[lchip]->p_fun_table)
    {
        mem_free(g_gb_oam_master[lchip]->p_fun_table);
    }
    if (NULL != g_gb_oam_master[lchip]->p_check_fun_table)
    {
        mem_free(g_gb_oam_master[lchip]->p_check_fun_table);
    }
    if (NULL != g_gb_oam_master[lchip]->index_2_mp_id)
    {
        mem_free(g_gb_oam_master[lchip]->index_2_mp_id);
    }

    if (NULL != g_gb_oam_master[lchip])
    {
        mem_free(g_gb_oam_master[lchip]);
    }

    return CTC_E_NO_MEMORY;

success:
    return CTC_E_NONE;

}

STATIC int32
_sys_greatbelt_oam_db_free_node_data(void* node_data, void* user_data)
{
    mem_free(node_data);

    return CTC_E_NONE;
}


extern sal_timer_t* sys_greatbelt_bfd_pf_timer;
int32
_sys_greatbelt_oam_db_deinit(uint8 lchip)
{
    LCHIP_CHECK(lchip);
    if (NULL == g_gb_oam_master[lchip])
    {
        return CTC_E_NONE;
    }

    if (sys_greatbelt_bfd_pf_timer)
    {
        sal_timer_stop(sys_greatbelt_bfd_pf_timer);
        sal_timer_destroy(sys_greatbelt_bfd_pf_timer);
        sys_greatbelt_bfd_pf_timer = NULL;
    }

    sal_sem_give(g_gb_oam_master[lchip]->bfd_state_sem);
    sal_task_destroy(g_gb_oam_master[lchip]->t_bfd_state);

    /*free mep vector*/
    ctc_vector_traverse(g_gb_oam_master[lchip]->mep_vec, (vector_traversal_fn)_sys_greatbelt_oam_db_free_node_data, NULL);
    ctc_vector_release(g_gb_oam_master[lchip]->mep_vec);

    /*free chan*/
    ctc_hash_traverse(g_gb_oam_master[lchip]->chan_hash, (hash_traversal_fn)_sys_greatbelt_oam_db_free_node_data, NULL);
    ctc_hash_free(g_gb_oam_master[lchip]->chan_hash);

    /*free maid*/
    ctc_hash_traverse(g_gb_oam_master[lchip]->maid_hash, (hash_traversal_fn)_sys_greatbelt_oam_db_free_node_data, NULL);
    ctc_hash_free(g_gb_oam_master[lchip]->maid_hash);

    mem_free(g_gb_oam_master[lchip]->p_check_fun_table);
    mem_free(g_gb_oam_master[lchip]->p_fun_table);
    mem_free(g_gb_oam_master[lchip]->index_2_mp_id);

    sys_greatbelt_opf_deinit(lchip, OPF_OAM_CHAN);
    sys_greatbelt_opf_deinit(lchip, OPF_OAM_MEP_LMEP);
    sys_greatbelt_opf_deinit(lchip, OPF_OAM_MEP_BFD);
    sys_greatbelt_opf_deinit(lchip, OPF_OAM_MA);
    sys_greatbelt_opf_deinit(lchip, OPF_OAM_MA_NAME);
    sys_greatbelt_opf_deinit(lchip, OPF_OAM_LM);

    sal_sem_destroy(g_gb_oam_master[lchip]->bfd_state_sem);
    sal_mutex_destroy(g_gb_oam_master[lchip]->oam_mutex);
    mem_free(g_gb_oam_master[lchip]);

    return CTC_E_NONE;
}


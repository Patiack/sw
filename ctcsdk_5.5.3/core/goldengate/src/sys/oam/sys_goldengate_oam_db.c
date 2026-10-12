
#include "ctc_oam.h"
#include "ctc_error.h"
#include "ctc_common.h"
#include "ctc_linklist.h"
#include "ctc_hash.h"
#include "ctc_packet.h"

#include "sys_goldengate_oam.h"
#include "sys_goldengate_oam_db.h"
#include "sys_goldengate_oam_debug.h"
#include "sys_goldengate_chip.h"
#include "sys_goldengate_opf.h"
#include "sys_goldengate_ftm.h"
#include "sys_goldengate_common.h"
#include "sys_goldengate_port.h"
#include "sys_goldengate_vlan.h"
#include "sys_goldengate_nexthop_api.h"
#include "sys_goldengate_nexthop.h"
#include "sys_goldengate_queue_enq.h"
#include "sys_goldengate_oam_com.h"
#include "../../../../../driver/goldengate/include/drv_io.h"


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
    uint16    l2vpn_oam_id;
    uint8 resv0[2];
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

struct sys_bfd_ip_v6_node_s
{
    uint32  count;
    ipv6_addr_t ipv6;
};
typedef struct sys_bfd_ip_v6_node_s sys_bfd_ip_v6_node_t;

#define SYS_OAM_LM_CMD_WRITE(lchip, lm_index, ds_oam_lm_stats) \
    do { \
            uint32 cmd = 0; \
            cmd = DRV_IOW(DsOamLmStats_t, DRV_ENTRY_FLAG);  \
            DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, lm_index, cmd, &ds_oam_lm_stats)); \
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

extern sys_oam_master_t* g_gg_oam_master[CTC_MAX_LOCAL_CHIP_NUM];
extern int32 sys_goldengate_oam_trpt_set_autogen_ptr(uint8 lchip, uint8 session_id, uint16 lmep_idx);
/***************************************************************
 *
 *  Functions
 *
 ***************************************************************/

#define HASH_KEY "Function Begin"

STATIC uint32
_sys_goldengate_oam_build_hash_key(void* p_data, uint8 length)
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
_sys_goldengate_oam_get_mep_entry_num(uint8 lchip)
{
    uint32 max_mep_index = 0;

    CTC_ERROR_RETURN(sys_goldengate_ftm_query_table_entry_num(lchip, DsEthMep_t,  &max_mep_index));

    return max_mep_index;
}

int32
_sys_goldengate_oam_get_mpls_entry_num(uint8 lchip)
{
    uint32 mpls_index = 0;

    CTC_ERROR_RETURN(sys_goldengate_ftm_query_table_entry_num(lchip, DsMpls_t,  &mpls_index));

    return mpls_index;
}

uint8
_sys_goldengate_bfd_csf_convert(uint8 lchip, uint8 type, bool to_asic)
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
_sys_goldengate_oam_get_nexthop_info(uint8 lchip, uint32 nhid, uint32 b_protection, sys_oam_nhop_info_t* p_nhop_info)
{
    sys_nh_info_dsnh_t nh_info;

    sal_memset(&nh_info, 0, sizeof(sys_nh_info_dsnh_t));


    CTC_PTR_VALID_CHECK(p_nhop_info);

    nh_info.protection_path = b_protection;
    CTC_ERROR_RETURN(sys_goldengate_nh_get_nhinfo(lchip, nhid, &nh_info));

    if (nh_info.drop_pkt)
    {
        p_nhop_info->dest_map = SYS_ENCODE_DESTMAP(nh_info.dest_chipid, SYS_RSV_PORT_DROP_ID);
    }
    else if (nh_info.oam_aps_en)
    {
        p_nhop_info->aps_bridge_en  = 1;
        p_nhop_info->mep_on_tunnel  = nh_info.mep_on_tunnel;
        p_nhop_info->dest_map       = nh_info.oam_aps_group_id;
    }
    else
    {
        if (SYS_NH_TYPE_MCAST == nh_info.nh_entry_type)
        {
            p_nhop_info->dest_map = SYS_ENCODE_MCAST_NON_IPE_DESTMAP(nh_info.dest_id);
        }
        else
        {
            p_nhop_info->dest_map = SYS_ENCODE_DESTMAP(nh_info.dest_chipid, nh_info.dest_id);
        }
    }

    p_nhop_info->nh_entry_type  = nh_info.nh_entry_type;
    p_nhop_info->nexthop_is_8w  = nh_info.nexthop_ext;
    p_nhop_info->dsnh_offset    = nh_info.dsnh_offset;
    p_nhop_info->have_l2edit  = nh_info.have_l2edit;
    p_nhop_info->multi_aps_en = nh_info.re_route;

    return CTC_E_NONE;
}


int32
_sys_goldengate_oam_check_nexthop_type (uint8 lchip, uint32 nhid, uint32 b_protection, uint8 mep_type)
{
    sys_oam_nhop_info_t nhop_info;
    int32 ret = CTC_E_NONE;

    sal_memset(&nhop_info, 0, sizeof(sys_oam_nhop_info_t));
    CTC_ERROR_RETURN(_sys_goldengate_oam_get_nexthop_info(lchip, nhid, b_protection, &nhop_info));

    if(SYS_NH_TYPE_DROP != nhop_info.nh_entry_type)
    {

        /* when GG-GB stacking, GG as master, use tunnel nexthop to encap
        if((CTC_OAM_MEP_TYPE_IP_BFD == mep_type)
            && (SYS_NH_TYPE_IPUC != nhop_info.nh_entry_type))
        {
            ret = CTC_E_NH_INVALID_NHID;
        }
    */

        if(((CTC_OAM_MEP_TYPE_MPLS_TP_BFD == mep_type)
            || (CTC_OAM_MEP_TYPE_MPLS_TP_Y1731 == mep_type)
            || (CTC_OAM_MEP_TYPE_MPLS_BFD == mep_type))
            && ((SYS_NH_TYPE_MPLS != nhop_info.nh_entry_type)
                && (SYS_NH_TYPE_IPUC != nhop_info.nh_entry_type)
                && (SYS_NH_TYPE_MCAST != nhop_info.nh_entry_type)))
        {
            ret = CTC_E_NH_INVALID_NHID;
        }

        if ((SYS_NH_TYPE_IPUC == nhop_info.nh_entry_type) && (nhop_info.have_l2edit))
        {
            ret = CTC_E_NH_INVALID_DSEDIT_PTR;
        }
    }

    return ret;

}

STATIC int8
_sys_goldengate_oam_cal_bitmap_bit(uint8 lchip, uint32 bitmap)
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
_sys_goldengate_oam_get_defect_type(uint8 lchip, uint32 defect, sys_oam_defect_type_t* defect_type)
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

    case CTC_OAM_DEFECT_EVENT_BFD_MIS_CONNECT:
        defect_type->defect_type[0]     = 5;
        defect_type->defect_sub_type[0] = 0;
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
_sys_goldengate_oam_get_rdi_defect_type(uint8 lchip, uint32 defect, uint8* defect_type, uint8* defect_sub_type)
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

int32
sys_goldengate_oam_get_defect_type_config(uint8 lchip ,ctc_oam_defect_t defect, sys_oam_defect_info_t *p_defect)
{
    sys_oam_defect_type_t defect_type_event;
    uint8 defect_type_rdi       = 0;
    uint8 defect_sub_type_rdi   = 0;

    uint8 to_event  = 0;
    uint8 to_rdi    = 0;

    uint32 cmd = 0;
    uint32 rdi_index = 0;
    uint32 tmp[2] = {0};
    uint32 value = 0;

    OamRxProcEtherCtl_m oam_rx_proc_ether_ctl;
    OamErrorDefectCtl_m  oam_error_defect_ctl;

    sal_memset(&defect_type_event, 0, sizeof(sys_oam_defect_type_t));

    _sys_goldengate_oam_get_defect_type(lchip, defect, &defect_type_event);
    if(defect_type_event.entry_num)
    {
        sal_memset(&oam_error_defect_ctl, 0, sizeof(OamErrorDefectCtl_m));
        cmd = DRV_IOR(OamErrorDefectCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &oam_error_defect_ctl));
        rdi_index = (defect_type_event.defect_type[0] << 3) | defect_type_event.defect_sub_type[0];
        GetOamErrorDefectCtl(A, reportDefectEn_f, &oam_error_defect_ctl, tmp);
        if (rdi_index < 32)
        {
           to_event = CTC_IS_BIT_SET(tmp[0], rdi_index);
        }
        else
        {
           to_event = CTC_IS_BIT_SET(tmp[1], (rdi_index - 32));
        }
    }

    _sys_goldengate_oam_get_rdi_defect_type(lchip, defect, &defect_type_rdi, &defect_sub_type_rdi);
    if((0 != defect_type_rdi) || (0 != defect_sub_type_rdi))
    {

        sal_memset(&oam_rx_proc_ether_ctl, 0, sizeof(OamRxProcEtherCtl_m));
        cmd = DRV_IOR(OamRxProcEtherCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &oam_rx_proc_ether_ctl));

        rdi_index = (defect_type_rdi << 3) | defect_sub_type_rdi;

        tmp[0] = GetOamRxProcEtherCtl(V, etherDefectToRdi0_f, &oam_rx_proc_ether_ctl);
        tmp[1] = GetOamRxProcEtherCtl(V, etherDefectToRdi1_f, &oam_rx_proc_ether_ctl);

        if (rdi_index < 32)
        {
            to_rdi = CTC_IS_BIT_SET(tmp[0], rdi_index);
        }
        else
        {
            to_rdi = CTC_IS_BIT_SET(tmp[1], (rdi_index - 32));
        }

        if (CTC_OAM_DEFECT_DLOC == defect)
        {
            cmd = DRV_IOR(OamUpdateCtl_t, OamUpdateCtl_genRdiByDloc_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
            to_rdi = value;
        }

    }


    if (CTC_OAM_DEFECT_MAC_STATUS_CHANGE == defect)
    {
        p_defect->defect_name   = "Mac status";
    }
    else if (CTC_OAM_DEFECT_SRC_MAC_MISMATCH == defect)
    {
        p_defect->defect_name   = "Src Mac Mismatch";
    }
    else if(CTC_OAM_DEFECT_MISMERGE == defect)
    {
        p_defect->defect_name   = "MisMerge";
    }
    else if(CTC_OAM_DEFECT_UNEXPECTED_LEVEL == defect)
    {
        p_defect->defect_name   = "Unexpected level";
    }
    else if(CTC_OAM_DEFECT_UNEXPECTED_MEP == defect)
    {
        p_defect->defect_name = "Unexpected MEP";
    }
    else if(CTC_OAM_DEFECT_UNEXPECTED_PERIOD == defect)
    {
        p_defect->defect_name = "Unexpected period";
    }
    else if(CTC_OAM_DEFECT_DLOC == defect)
    {
        p_defect->defect_name = "Loc";
    }
    else if(CTC_OAM_DEFECT_CSF == defect)
    {
        p_defect->defect_name = "CSF";
    }
    else if(CTC_OAM_DEFECT_EVENT_RX_FIRST_PKT == defect)
    {
        p_defect->defect_name = "1st packet";
    }
    else if(CTC_OAM_DEFECT_EVENT_RDI_RX == defect)
    {
        p_defect->defect_name = "Rx RDI";
    }
    else if(CTC_OAM_DEFECT_EVENT_RDI_TX == defect)
    {
        p_defect->defect_name = "Tx RDI";
    }
    else if(CTC_OAM_DEFECT_EVENT_BFD_DOWN == defect)
    {
        p_defect->defect_name = "BFD Down";
    }
    else if(CTC_OAM_DEFECT_EVENT_BFD_INIT == defect)
    {
        p_defect->defect_name = "BFD Init";
    }
    else if(CTC_OAM_DEFECT_EVENT_BFD_UP == defect)
    {
        p_defect->defect_name = "BFD Up";
    }
    else if(CTC_OAM_DEFECT_EVENT_BFD_MIS_CONNECT == defect)
    {
        p_defect->defect_name = "BFD MisConnect";
    }
    p_defect->to_event      = to_event;
    p_defect->to_rdi        = to_rdi;

    return CTC_E_NONE;
}

#define MAID "Function Begin"

sys_oam_maid_com_t*
_sys_goldengate_oam_maid_lkup(uint8 lchip, ctc_oam_maid_t* p_ctc_maid)
{
    ctc_hash_t* p_maid_hash = NULL;
    sys_oam_maid_com_t* p_sys_maid_db = NULL;
    sys_oam_maid_com_t sys_maid;

    sal_memset(&sys_maid, 0 , sizeof(sys_oam_maid_com_t));
    sys_maid.mep_type = p_ctc_maid->mep_type;
    sys_maid.maid_len = p_ctc_maid->maid_len;
    sal_memcpy(&sys_maid.maid, p_ctc_maid->maid, sys_maid.maid_len);

    p_maid_hash = g_gg_oam_master[lchip]->maid_hash;

    p_sys_maid_db = ctc_hash_lookup(p_maid_hash, &sys_maid);

    return p_sys_maid_db;
}

STATIC int32
_sys_goldengate_oam_maid_cmp(sys_oam_maid_com_t* p_sys_maid,  sys_oam_maid_com_t* p_sys_maid_db)
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
_sys_goldengate_oam_maid_build_key(sys_oam_maid_com_t* p_sys_maid)
{

    sys_oam_maid_hash_key_t maid_hash_key;
    uint8   length = 0;

    sal_memset(&maid_hash_key, 0, sizeof(sys_oam_maid_hash_key_t));

    maid_hash_key.mep_type = p_sys_maid->mep_type;
    sal_memcpy(&maid_hash_key.maid, &p_sys_maid->maid, sizeof(p_sys_maid->maid));
    length = sizeof(maid_hash_key);

    return _sys_goldengate_oam_build_hash_key(&maid_hash_key, length);
}

sys_oam_maid_com_t*
_sys_goldengate_oam_maid_build_node(uint8 lchip, ctc_oam_maid_t* p_maid_param)
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
_sys_goldengate_oam_maid_free_node(uint8 lchip, sys_oam_maid_com_t* p_sys_maid)
{
    mem_free(p_sys_maid);
    p_sys_maid = NULL;

    return CTC_E_NONE;
}


int32
_sys_goldengate_oam_maid_build_index(uint8 lchip, sys_oam_maid_com_t* p_sys_maid)
{
    sys_goldengate_opf_t opf;
    uint8  maid_entry_num = 0;
    uint32 offset         = 0;

    if (g_gg_oam_master[lchip]->no_mep_resource)
    {
        return CTC_E_NONE;
    }

    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
    opf.pool_type  = OPF_OAM_MA_NAME;
    opf.pool_index = 0;

    maid_entry_num = p_sys_maid->maid_entry_num;
    CTC_ERROR_RETURN(sys_goldengate_opf_alloc_offset(lchip, &opf, maid_entry_num, &offset));
    SYS_OAM_TBL_CNT(lchip, SYS_OAM_TBL_MANAME, 1, maid_entry_num);
    p_sys_maid->maid_index = offset;
    SYS_OAM_DBG_INFO("MAID: lchip->%d, index->%d\n", lchip, offset);

    return CTC_E_NONE;
}

int32
_sys_goldengate_oam_maid_free_index(uint8 lchip, sys_oam_maid_com_t* p_sys_maid)
{
    sys_goldengate_opf_t opf;
    uint8  maid_entry_num = 0;
    uint32 offset         = 0;

    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));

    if (g_gg_oam_master[lchip]->no_mep_resource)
    {
        return CTC_E_NONE;
    }

    opf.pool_type  = OPF_OAM_MA_NAME;
    opf.pool_index = 0;

    maid_entry_num = p_sys_maid->maid_entry_num;
    offset = p_sys_maid->maid_index;
    CTC_ERROR_RETURN(sys_goldengate_opf_free_offset(lchip, &opf, maid_entry_num, offset));
    SYS_OAM_TBL_CNT(lchip, SYS_OAM_TBL_MANAME, 0, maid_entry_num);

    return CTC_E_NONE;
}

int32
_sys_goldengate_oam_maid_add_to_db(uint8 lchip, sys_oam_maid_com_t* p_sys_maid)
{
    ctc_hash_insert(g_gg_oam_master[lchip]->maid_hash, p_sys_maid);

    return CTC_E_NONE;
}

int32
_sys_goldengate_oam_maid_del_from_db(uint8 lchip, sys_oam_maid_com_t* p_sys_maid)
{
    ctc_hash_remove(g_gg_oam_master[lchip]->maid_hash, p_sys_maid);

    return CTC_E_NONE;
}

int32
_sys_goldengate_oam_maid_add_to_asic(uint8 lchip, sys_oam_maid_com_t* p_sys_maid)
{
    DsMaName_m ds_ma_name;
    uint32 cmd            = 0;
    uint32 ma_index       = 0;
    uint8  entry_index    = 0;
    uint8  maid_entry_num = 0;
    uint32 maid_field[2] = {0};

    maid_entry_num  = p_sys_maid->maid_entry_num;
    ma_index        = p_sys_maid->maid_index;

    sal_memset(&ds_ma_name, 0, sizeof(DsMaName_m));
    cmd = DRV_IOW(DsMaName_t, DRV_ENTRY_FLAG);

    for (entry_index = 0; entry_index < maid_entry_num; entry_index++)
    {
        maid_field[1] = p_sys_maid->maid[entry_index * 8 + 0] << 24 |
                        p_sys_maid->maid[entry_index * 8 + 1] << 16 |
                        p_sys_maid->maid[entry_index * 8 + 2] << 8 |
                        p_sys_maid->maid[entry_index * 8 + 3] << 0;

        maid_field[0] = p_sys_maid->maid[entry_index * 8 + 4] << 24 |
                        p_sys_maid->maid[entry_index * 8 + 5] << 16 |
                        p_sys_maid->maid[entry_index * 8 + 6] << 8 |
                        p_sys_maid->maid[entry_index * 8 + 7] << 0;

        DRV_SET_FIELD_A(DsMaName_t,DsMaName_u1_g1_maIdUmc_f,&ds_ma_name,maid_field);

        SYS_OAM_DBG_INFO("-----ds_ma_name(maIdx:%d, entryIdx:%d)--------\n", ma_index, entry_index);
        SYS_OAM_DBG_INFO("ds_ma_name.ma_id_umc0:0x%x\n", maid_field[0]);
        SYS_OAM_DBG_INFO("ds_ma_name.ma_id_umc1:0x%x\n", maid_field[1]);

        CTC_ERROR_RETURN(DRV_IOCTL(lchip, ma_index + entry_index, cmd, &ds_ma_name));
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_oam_maid_del_from_asic(uint8 lchip, sys_oam_maid_com_t* p_sys_maid)
{

    DsMaName_m ds_ma_name;
    uint32 cmd              = 0;
    uint32 ma_index         = 0;
    uint8  entry_index      = 0;
    uint8  maid_entry_num   = 0;

    maid_entry_num  = p_sys_maid->maid_entry_num;
    ma_index        = p_sys_maid->maid_index;

    sal_memset(&ds_ma_name, 0, sizeof(DsMaName_m));
    cmd = DRV_IOW(DsMaName_t, DRV_ENTRY_FLAG);

    for (entry_index = 0; entry_index < maid_entry_num; entry_index++)
    {
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, ma_index + entry_index, cmd, &ds_ma_name));
    }

    return CTC_E_NONE;
}





#define CHAN "Function Begin"

sys_oam_chan_com_t*
_sys_goldengate_oam_chan_lkup(uint8 lchip, sys_oam_chan_com_t* p_sys_chan)
{
    ctc_hash_t* p_chan_hash = NULL;
    sys_oam_chan_com_t* p_sys_chan_db = NULL;

    p_chan_hash = g_gg_oam_master[lchip]->chan_hash;

    p_sys_chan_db = ctc_hash_lookup(p_chan_hash, p_sys_chan);

    return p_sys_chan_db;
}

int32
_sys_goldengate_oam_chan_cmp(sys_oam_chan_com_t* p_sys_chan, sys_oam_chan_com_t* p_sys_chan_db)
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
_sys_goldengate_oam_chan_build_key(sys_oam_chan_com_t* p_sys_chan)
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
        chan_hash_key.u.eth.l2vpn_oam_id = ((sys_oam_chan_eth_t*)p_sys_chan)->key.l2vpn_oam_id;
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

    return _sys_goldengate_oam_build_hash_key(&chan_hash_key, length);

}

int32
_sys_goldengate_oam_lm_build_index(uint8 lchip, sys_oam_chan_com_t* p_sys_chan, uint8 is_link_sec, uint8  md_level)
{
    sys_goldengate_opf_t opf;
    uint32 offset           = 0;
    uint8  mep_type         = 0;
    uint8  lm_cos_type      = 0;
    uint16* lm_index_base   = 0;
    uint32  block_size      = 0;
    uint8   cnt             = 0;
    int ret = CTC_E_NONE;
    sys_oam_chan_eth_t* p_eth_chan = NULL;
    sys_oam_chan_tp_t* p_tp_chan  = NULL;
    sys_oam_lmep_com_t* p_sys_lmep_com = NULL;
    sys_oam_lmep_y1731_t sys_oam_lmep_eth;
    DsOamLmStats_m ds_oam_lm_stats;
    uint8 need_alloc_lm = 1;
    uint8 is_ether_service = 0;

    mep_type = p_sys_chan->mep_type;

    if ((CTC_OAM_MEP_TYPE_ETH_1AG == mep_type) || (CTC_OAM_MEP_TYPE_ETH_Y1731 == mep_type))
    {
        p_eth_chan = (sys_oam_chan_eth_t*)p_sys_chan;

        sal_memset(&sys_oam_lmep_eth, 0, sizeof(sys_oam_lmep_y1731_t));
        sys_oam_lmep_eth.md_level = md_level;
        p_sys_lmep_com = _sys_goldengate_oam_lmep_lkup(lchip, p_sys_chan, &sys_oam_lmep_eth.com);
        if (NULL == p_sys_lmep_com)
        {
            return CTC_E_OAM_CHAN_LMEP_NOT_FOUND;
        }
        if (is_link_sec)
        {
            lm_cos_type     = p_eth_chan->link_lm_cos_type;
            lm_index_base   = &p_sys_chan->link_lm_index_base;
        }
        else
        {
            if (p_sys_chan->lm_num >= 3)
            {
                return CTC_E_OAM_LM_NUM_RXCEED;
            }
            lm_cos_type     = p_sys_lmep_com->lm_cos_type;
            lm_index_base   = &p_sys_lmep_com->lm_index_base;
            is_ether_service = 1;
        }

        if ((CTC_OAM_LM_COS_TYPE_ALL_COS == lm_cos_type) || (CTC_OAM_LM_COS_TYPE_SPECIFIED_COS == lm_cos_type))
        {
            block_size = 1;
        }
        else if (CTC_OAM_LM_COS_TYPE_PER_COS == lm_cos_type)
        {
            block_size = 8;
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

    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));



    if (is_ether_service)
    {
        if (!p_eth_chan->lm_index_alloced)
        {
            opf.pool_type  = OPF_OAM_LM_PROFILE;
            opf.pool_index = 0;

            CTC_ERROR_RETURN(sys_goldengate_opf_alloc_offset(lchip, &opf, 1, &offset));
            p_sys_chan->lm_index_base = offset;
            p_eth_chan->lm_index_alloced = 1;
            SYS_OAM_DBG_INFO("LM profile: DsEthLmProfile, index->%d\n", offset);
        }

        if (p_sys_lmep_com->lm_index_alloced)
        {
            need_alloc_lm = 0;
        }
    }

    if (need_alloc_lm)
    {
        opf.pool_type  = OPF_OAM_LM;
        opf.pool_index = 0;

        ret = sys_goldengate_opf_alloc_offset(lchip, &opf, block_size, &offset);
        SYS_OAM_TBL_CNT(lchip, SYS_OAM_TBL_LM, 1, block_size);
        if (CTC_E_NONE != ret)
        {
            if (is_ether_service && (0 == p_sys_chan->lm_num ) && p_eth_chan->lm_index_alloced)
            {
                opf.pool_type  = OPF_OAM_LM_PROFILE;
                opf.pool_index = 0;
                sys_goldengate_opf_free_offset(lchip, &opf, 1, p_sys_chan->lm_index_base );
                p_sys_chan->lm_index_base = 0;
                p_eth_chan->lm_index_alloced = 0;
            }
        }
        else
        {
            *lm_index_base = offset;
            SYS_OAM_DBG_INFO("LM: lchip->%d, index->%d, block_size->%d\n", lchip, offset, block_size);
            for (cnt = 0; cnt < block_size; cnt++)
            {
                sal_memset(&ds_oam_lm_stats, 0, sizeof(DsOamLmStats_m));
                SYS_OAM_LM_CMD_WRITE(lchip, (offset + cnt), ds_oam_lm_stats);
            }
            if (is_ether_service)
            {
                p_sys_lmep_com->lm_index_alloced = 1;
                p_sys_chan->lm_num++;
                SYS_OAM_DBG_INFO("LM number: %d\n", p_sys_chan->lm_num);
            }
        }
    }

    return ret;
}

int32
_sys_goldengate_oam_lm_free_index(uint8 lchip, sys_oam_chan_com_t* p_sys_chan, uint8 is_link_sec, uint8  md_level)
{
    sys_goldengate_opf_t opf;
    uint16 *offset          = NULL;
    uint8  mep_type         = 0;
    uint8  lm_cos_type      = 0;
    uint32  block_size      = 0;
    uint8 need_free_lm_prof = 0;
    uint8 need_free_lm = 1;
    sys_oam_chan_eth_t* p_eth_chan = NULL;
    sys_oam_chan_tp_t* p_tp_chan  = NULL;
    sys_oam_lmep_com_t* p_sys_lmep_com = NULL;
    sys_oam_lmep_y1731_t sys_oam_lmep_eth;

    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));

    mep_type = p_sys_chan->mep_type;

    if ((CTC_OAM_MEP_TYPE_ETH_1AG == mep_type) || (CTC_OAM_MEP_TYPE_ETH_Y1731 == mep_type))
    {
        p_eth_chan = (sys_oam_chan_eth_t*)p_sys_chan;
        sal_memset(&sys_oam_lmep_eth, 0, sizeof(sys_oam_lmep_y1731_t));
        sys_oam_lmep_eth.md_level = md_level;
        p_sys_lmep_com = _sys_goldengate_oam_lmep_lkup(lchip, p_sys_chan, &sys_oam_lmep_eth.com);
        if (NULL == p_sys_lmep_com)
        {
            return CTC_E_OAM_CHAN_LMEP_NOT_FOUND;
        }
        if (is_link_sec)
        {
            lm_cos_type     = p_eth_chan->link_lm_cos_type;
            offset          = &p_sys_chan->link_lm_index_base;
        }
        else
        {
            lm_cos_type     = p_sys_lmep_com->lm_cos_type;
            offset          = &p_sys_lmep_com->lm_index_base;
            if (p_sys_chan->lm_num > 0)
            {
                p_sys_chan->lm_num --;
            }
            if (p_eth_chan->lm_index_alloced)
            {
                need_free_lm_prof = 1;
            }
            if (!p_sys_lmep_com->lm_index_alloced)
            {
                need_free_lm = 0;
            }
            else
            {
                p_sys_lmep_com->lm_index_alloced = 0;
            }
            SYS_OAM_DBG_INFO("LM number: %d\n", p_sys_chan->lm_num);
        }

        if ((CTC_OAM_LM_COS_TYPE_ALL_COS == lm_cos_type) || (CTC_OAM_LM_COS_TYPE_SPECIFIED_COS == lm_cos_type))
        {
            block_size = 1;
        }
        else if (CTC_OAM_LM_COS_TYPE_PER_COS == lm_cos_type)
        {
            block_size = 8;
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



    if (need_free_lm)
    {
        opf.pool_type  = OPF_OAM_LM;
        opf.pool_index = 0;
        CTC_ERROR_RETURN(sys_goldengate_opf_free_offset(lchip, &opf, block_size, (*offset)));
        SYS_OAM_TBL_CNT(lchip, SYS_OAM_TBL_LM, 0, block_size);
        *offset = 0;
    }

    if (need_free_lm_prof && (0 == p_sys_chan->lm_num )) /*free DsEthLmProfile*/
    {
        opf.pool_type  = OPF_OAM_LM_PROFILE;
        opf.pool_index = 0;
        CTC_ERROR_RETURN(sys_goldengate_opf_free_offset(lchip, &opf, 1, p_sys_chan->lm_index_base));
        p_sys_chan->lm_index_base = 0;
        p_eth_chan->lm_index_alloced = 0;
    }

    return CTC_E_NONE;
}


int32
_sys_goldengate_oam_chan_add_to_db(uint8 lchip, sys_oam_chan_com_t* p_sys_chan)
{
    ctc_hash_insert(g_gg_oam_master[lchip]->chan_hash, p_sys_chan);

    return CTC_E_NONE;
}

int32
_sys_goldengate_oam_chan_del_from_db(uint8 lchip, sys_oam_chan_com_t* p_sys_chan)
{
    ctc_hash_remove(g_gg_oam_master[lchip]->chan_hash, p_sys_chan);

    return CTC_E_NONE;
}

#define LMEP "Function Begin"

sys_oam_lmep_com_t*
_sys_goldengate_oam_lmep_lkup(uint8 lchip, sys_oam_chan_com_t* p_sys_chan, sys_oam_lmep_com_t* p_sys_lmep)
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
_sys_goldengate_oam_lmep_build_index(uint8 lchip, sys_oam_lmep_com_t* p_sys_lmep)
{
    sys_goldengate_opf_t opf;
    uint32 offset       = 0;
    uint32 block_size   = 0;
    int32 ret           = CTC_E_NONE;


    if (p_sys_lmep->mep_on_cpu)
    {
        p_sys_lmep->lmep_index = 0x1FFF;
        SYS_OAM_DBG_INFO("LMEP: lchip->%d, index->%d\n", lchip, offset);
        return CTC_E_NONE;
    }


    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));

    if (P_COMMON_OAM_MASTER_GLB(lchip).mep_index_alloc_by_sdk)
    {
        if (g_gg_oam_master[lchip]->no_mep_resource)
        {
            return CTC_E_INVALID_CONFIG;
        }
        p_sys_lmep->lchip = lchip;

        opf.pool_index = 0;
        opf.pool_type  = OPF_OAM_MA;
        CTC_ERROR_RETURN(sys_goldengate_opf_alloc_offset(lchip, &opf, 1, &offset));
        p_sys_lmep->ma_index = offset;

        SYS_OAM_TBL_CNT(lchip, SYS_OAM_TBL_MA, 1, 1);

        SYS_OAM_DBG_INFO("MA  : lchip->%d, index->%d\n", lchip, offset);

        block_size = 2;
        if ((CTC_OAM_MEP_TYPE_MPLS_BFD == p_sys_lmep->p_sys_chan->mep_type)
            || (CTC_OAM_MEP_TYPE_IP_BFD == p_sys_lmep->p_sys_chan->mep_type)
            || (CTC_OAM_MEP_TYPE_MPLS_TP_BFD == p_sys_lmep->p_sys_chan->mep_type))
        {
             /*opf.pool_type  = OPF_OAM_MEP_BFD;*/
            opf.pool_type  = OPF_OAM_MEP_LMEP;
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

        ret = sys_goldengate_opf_alloc_offset(lchip, &opf, block_size, &offset);
        if(CTC_E_NONE != ret)
        {
            opf.pool_type  = OPF_OAM_MA;
            offset         = p_sys_lmep->ma_index;
            p_sys_lmep->ma_index = 0;
            sys_goldengate_opf_free_offset(lchip, &opf, 1, offset);
            SYS_OAM_TBL_CNT(lchip, SYS_OAM_TBL_MA, 0, 1);
            return ret;
        }
        p_sys_lmep->lmep_index = offset;
        SYS_OAM_TBL_CNT(lchip, SYS_OAM_TBL_MEP, 1, block_size);

    }
    else
    {
        if (g_gg_oam_master[lchip]->no_mep_resource)
        {
            p_sys_lmep->ma_index = 0;
            return CTC_E_NONE;
        }
        opf.pool_type  = OPF_OAM_MA;
        opf.pool_index = 0;
        CTC_ERROR_RETURN(sys_goldengate_opf_alloc_offset(lchip, &opf, 1, &offset));
        SYS_OAM_TBL_CNT(lchip, SYS_OAM_TBL_MA, 1, 1);
        p_sys_lmep->ma_index = offset;
        offset = p_sys_lmep->lmep_index;
        SYS_OAM_TBL_CNT(lchip, SYS_OAM_TBL_MEP, 1, 1);
    }

    SYS_OAM_DBG_INFO("LMEP: lchip->%d, index->%d\n", lchip, offset);

    return ret;
}

int32
_sys_goldengate_oam_lmep_free_index(uint8 lchip, sys_oam_lmep_com_t* p_sys_lmep)
{
    sys_goldengate_opf_t opf;
    uint32 offset       = 0;
    uint32 block_size   = 0;

    if (p_sys_lmep->mep_on_cpu)
    {
        p_sys_lmep->lmep_index = 0;
        return CTC_E_NONE;
    }

    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));

    if (P_COMMON_OAM_MASTER_GLB(lchip).mep_index_alloc_by_sdk)
    {
        if (g_gg_oam_master[lchip]->no_mep_resource)
        {
            return CTC_E_INVALID_CONFIG;
        }
        lchip = p_sys_lmep->lchip;
        opf.pool_index = 0;
        offset = p_sys_lmep->ma_index;
        opf.pool_type  = OPF_OAM_MA;
        CTC_ERROR_RETURN(sys_goldengate_opf_free_offset(lchip, &opf, 1, offset));

        SYS_OAM_TBL_CNT(lchip, SYS_OAM_TBL_MA, 0, 1);

        offset      = p_sys_lmep->lmep_index;
        block_size  = 2;

        if ((CTC_OAM_MEP_TYPE_MPLS_BFD == p_sys_lmep->p_sys_chan->mep_type)
            || (CTC_OAM_MEP_TYPE_IP_BFD == p_sys_lmep->p_sys_chan->mep_type)
            || (CTC_OAM_MEP_TYPE_MPLS_TP_BFD == p_sys_lmep->p_sys_chan->mep_type))
        {
             /*opf.pool_type  = OPF_OAM_MEP_BFD;*/
            opf.pool_type  = OPF_OAM_MEP_LMEP;
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
        CTC_ERROR_RETURN(sys_goldengate_opf_free_offset(lchip, &opf, block_size, offset));
        SYS_OAM_TBL_CNT(lchip, SYS_OAM_TBL_MEP, 0, block_size);
    }
    else
    {
        if (g_gg_oam_master[lchip]->no_mep_resource)
        {
            return CTC_E_NONE;
        }
        opf.pool_type  = OPF_OAM_MA;
        opf.pool_index = 0;
        offset = p_sys_lmep->ma_index;
        CTC_ERROR_RETURN(sys_goldengate_opf_free_offset(lchip, &opf, 1, offset));
        SYS_OAM_TBL_CNT(lchip, SYS_OAM_TBL_MA, 0, 1);
        SYS_OAM_TBL_CNT(lchip, SYS_OAM_TBL_MEP, 0, 1);
    }

    return CTC_E_NONE;

}

int32
_sys_goldengate_oam_lmep_add_to_db(uint8 lchip, sys_oam_lmep_com_t* p_sys_lmep)
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
        ctc_vector_add(g_gg_oam_master[lchip]->mep_vec, mep_index, p_sys_lmep);
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_oam_lmep_del_from_db(uint8 lchip, sys_oam_lmep_com_t* p_sys_lmep)
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

    if (!p_sys_lmep->mep_on_cpu)
    {
        ctc_vector_del(g_gg_oam_master[lchip]->mep_vec, mep_index);
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_oam_lmep_add_eth_to_asic(uint8 lchip, sys_oam_lmep_com_t* p_sys_lmep)
{
    sys_oam_chan_eth_t* p_sys_chan_eth = NULL;
    sys_oam_lmep_y1731_t* p_sys_eth_lmep = NULL;
    sys_oam_maid_com_t* p_sys_eth_maid = NULL;
    uint32 cmd        = 0;
    uint32 index      = 0;
    uint32 lmep_flag  = 0;
    uint32 gport      = 0;
    uint8  dest_chip  = 0;
    uint16  dest_id  = 0;
    uint32 destmap = 0;
    uint8 maid_length_type = 0;
    uint16 src_vlan_ptr    = 0;
    DsMa_m ds_ma;
    DsEthMep_m ds_eth_mep;
    DsEthMep_m DsEthMep_mask;
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
    dest_id =    SYS_MAP_CTC_GPORT_TO_DRV_LPORT(gport);
    destmap = SYS_ENCODE_DESTMAP( dest_chip, dest_id );

    sal_memset(&ds_ma, 0, sizeof(DsMa_m));
    sal_memset(&ds_eth_mep, 0, sizeof(DsEthMep_m));
    sal_memset(&DsEthMep_mask, 0, sizeof(DsEthMep_m));

    /* set ma table entry */
    if ((0 == p_sys_chan_eth->key.vlan_id) || p_sys_chan_eth->key.link_oam)
    {
        SetDsMa(V, txUntaggedOam_f      , &ds_ma ,  1);
    }
    else
    {
        SetDsMa(V, txUntaggedOam_f      , &ds_ma ,  0);
    }
    SetDsMa(V, maIdLengthType_f     , &ds_ma , maid_length_type);

    SetDsMa(V, mplsLabelValid_f     , &ds_ma , 0);
    SetDsMa(V, rxOamType_f          , &ds_ma , 1);

    SetDsMa(V, txWithPortStatus_f   , &ds_ma , CTC_FLAG_ISSET(lmep_flag, CTC_OAM_Y1731_LMEP_FLAG_TX_PORT_STATUS));
    SetDsMa(V, txWithSendId_f       , &ds_ma , CTC_FLAG_ISSET(lmep_flag, CTC_OAM_Y1731_LMEP_FLAG_TX_SEND_ID));
    SetDsMa(V, mplsTtl_f            , &ds_ma , 0);
    SetDsMa(V, maNameIndex_f        , &ds_ma ,  p_sys_eth_maid->maid_index);
    SetDsMa(V, priorityIndex_f      , &ds_ma , p_sys_eth_lmep->tx_cos_exp);

    SetDsMa(V, sfFailWhileCfgType_f , &ds_ma , 0);
    SetDsMa(V, apsEn_f              , &ds_ma , CTC_FLAG_ISSET(lmep_flag, CTC_OAM_Y1731_LMEP_FLAG_APS_EN));
    SetDsMa(V, maNameLen_f          , &ds_ma ,  p_sys_eth_maid->maid_len);
    SetDsMa(V, mdLvl_f              , &ds_ma ,  p_sys_eth_lmep->md_level);
    SetDsMa(V, txWithIfStatus_f     , &ds_ma , CTC_FLAG_ISSET(lmep_flag, CTC_OAM_Y1731_LMEP_FLAG_TX_IF_STATUS));

    SetDsMa(V, tunnelApsEn_f        , &ds_ma , 0);
    SetDsMa(V, ipv6Hdr_f            , &ds_ma , 0);
    SetDsMa(V, sfState_f            , &ds_ma , 0);
    SetDsMa(V, csfInterval_f        , &ds_ma , 0);
    SetDsMa(V, protectingPath_f     , &ds_ma , 0);
    SetDsMa(V, csfTimeIdx_f         , &ds_ma , 0);
    SetDsMa(V, packetType_f         , &ds_ma , 0);
    SetDsMa(V, nextHopExt_f         , &ds_ma , 0);

    if (p_sys_chan_eth->key.link_oam)
    {
        uint32 default_vlan;
        CTC_ERROR_RETURN(sys_goldengate_port_get_property(lchip, gport, CTC_PORT_PROP_DEFAULT_VLAN, &default_vlan));
        CTC_ERROR_RETURN(sys_goldengate_vlan_get_vlan_ptr(lchip, default_vlan, &src_vlan_ptr));
        SetDsMa(V, linkoam_f            , &ds_ma , 1);
    }
    else
    {
        src_vlan_ptr = p_sys_chan_eth->key.vlan_id;
    }

    if (p_sys_eth_lmep->is_up) /*up mep*/
    {
        if (p_sys_chan_eth->key.use_fid)
        {
            SetDsMa(V, useVrfidLkup_f       , &ds_ma , 1);
            SetDsMa(V, u1_g2_vrfid_f        , &ds_ma ,  p_sys_chan_eth->key.vlan_id); /*fid*/
        }
        else
        {
            SetDsMa(V, useVrfidLkup_f       , &ds_ma , 0);
            SetDsMa(V, u1_g3_srcVlanPtr_f   , &ds_ma , src_vlan_ptr);  /*vlan ptr*/
        }
    }
    else /*down mep*/
    {
        SetDsMa(V, u1_g3_srcVlanPtr_f   , &ds_ma , src_vlan_ptr); /*vlan ptr*/
    }


SetDsEthMep(V, destMap_f               , &ds_eth_mep , destmap);

SetDsEthMep(V, ccmInterval_f           , &ds_eth_mep ,  p_sys_eth_lmep->ccm_interval);
SetDsMa(V, csfInterval_f   , &ds_ma , p_sys_eth_lmep->ccm_interval);

SetDsEthMep(V, active_f                , &ds_eth_mep , CTC_FLAG_ISSET(lmep_flag, CTC_OAM_Y1731_LMEP_FLAG_MEP_EN));
SetDsEthMep(V, isUp_f                  , &ds_eth_mep , p_sys_eth_lmep->is_up);
SetDsEthMep(V, ethOamP2PMode_f         , &ds_eth_mep , CTC_FLAG_ISSET(lmep_flag, CTC_OAM_Y1731_LMEP_FLAG_P2P_MODE));
SetDsEthMep(V, maIdCheckDisable_f      , &ds_eth_mep , 0);
SetDsEthMep(V, cciEn_f                 , &ds_eth_mep , p_sys_eth_lmep->ccm_en);
SetDsEthMep(V, enablePm_f              , &ds_eth_mep , CTC_FLAG_ISSET(lmep_flag, CTC_OAM_Y1731_LMEP_FLAG_DM_EN));
SetDsEthMep(V, presentTrafficCheckEn_f , &ds_eth_mep , 0);
SetDsEthMep(V, seqNumEn_f              , &ds_eth_mep , CTC_FLAG_ISSET(lmep_flag, CTC_OAM_Y1731_LMEP_FLAG_CCM_SEQ_NUM_EN));
SetDsEthMep(V, shareMacEn_f            , &ds_eth_mep , p_sys_eth_lmep->share_mac);

SetDsEthMep(V, mepId_f                 , &ds_eth_mep , p_sys_eth_lmep->mep_id;);
SetDsEthMep(V, tpidType_f              , &ds_eth_mep , p_sys_eth_lmep->tpid_type);
SetDsEthMep(V, mepPrimaryVid_f         , &ds_eth_mep ,  p_sys_chan_eth->key.vlan_id);
SetDsEthMep(V, maIndex_f               , &ds_eth_mep , p_sys_lmep->ma_index);

SetDsEthMep(V, isRemote_f              , &ds_eth_mep , 0);
SetDsEthMep(V, cciWhile_f              , &ds_eth_mep , 4);
SetDsEthMep(V, dUnexpMepTimer_f        , &ds_eth_mep , 14);
SetDsEthMep(V, dMismergeTimer_f        , &ds_eth_mep , 14);
SetDsEthMep(V, dMegLvlTimer_f          , &ds_eth_mep , 14);
SetDsEthMep(V, isBfd_f                 , &ds_eth_mep , 0);

SetDsEthMep(V, mepType_f               , &ds_eth_mep , 0);
SetDsEthMep(V, sfFailWhile_f           , &ds_eth_mep , 0);
SetDsEthMep(V, learnEn_f               , &ds_eth_mep , 1);
SetDsEthMep(V, apsSignalFailLocal_f    , &ds_eth_mep , 0);

SetDsEthMep(V, autoGenEn_f             , &ds_eth_mep , 0);
SetDsEthMep(V, presentTraffic_f        , &ds_eth_mep , 0);
SetDsEthMep(V, portStatus_f            , &ds_eth_mep , 0);
SetDsEthMep(V, rmepLastRdi_f           , &ds_eth_mep , 0);
SetDsEthMep(V, presentRdi_f            , &ds_eth_mep , 0);
SetDsEthMep(V, dUnexpMep_f             , &ds_eth_mep , 0);
SetDsEthMep(V, dMismerge_f             , &ds_eth_mep , 0);
SetDsEthMep(V, dMegLvl_f               , &ds_eth_mep , 0);
SetDsEthMep(V, portId_f                , &ds_eth_mep , 0);
SetDsEthMep(V, p2pUseUcDa_f            , &ds_eth_mep , 0);
SetDsEthMep(V, autoGenPktPtr_f         , &ds_eth_mep , 0);
SetDsEthMep(V, ccmSeqNum_f             , &ds_eth_mep , 0);
SetDsEthMep(V, apsBridgeEn_f           , &ds_eth_mep , 0);

    index = p_sys_lmep->ma_index;
    cmd = DRV_IOW(DsMa_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_ma));

    index = p_sys_lmep->lmep_index;
    cmd = DRV_IOW(DsEthMep_t, DRV_ENTRY_FLAG);
    tbl_entry.data_entry = (uint32*)&ds_eth_mep;
    tbl_entry.mask_entry = (uint32*)&DsEthMep_mask;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &tbl_entry));

RETURN:

    return CTC_E_NONE;

}

int32
_sys_goldengate_oam_lmep_add_tp_y1731_to_asic(uint8 lchip, sys_oam_lmep_com_t* p_sys_lmep)
{
    sys_oam_chan_tp_t* p_sys_chan_tp = NULL;
    sys_oam_lmep_y1731_t* p_sys_tp_y1731_lmep = NULL;
    sys_oam_maid_com_t* p_sys_tp_y1731_maid = NULL;
    uint32 cmd        = 0;
    uint32 index      = 0;
    uint32 lmep_flag  = 0;
    uint8 maid_length_type = 0;
    DsMa_m ds_ma;
    DsEthMep_m ds_eth_mep;
    DsEthMep_m DsEthMep_mask;
    tbl_entry_t tbl_entry;
    uint32 destmap = 0;

    p_sys_tp_y1731_lmep = (sys_oam_lmep_y1731_t*)p_sys_lmep;
    p_sys_chan_tp       = (sys_oam_chan_tp_t*)p_sys_lmep->p_sys_chan;
    p_sys_tp_y1731_maid = (sys_oam_maid_com_t*)p_sys_lmep->p_sys_maid;

    if (SYS_OAM_NO_MEP_OPERATION(lchip, p_sys_lmep->p_sys_chan->master_chipid))
    {
        goto RETURN;
    }

    lmep_flag = p_sys_tp_y1731_lmep->flag;
    maid_length_type = (p_sys_tp_y1731_maid->maid_entry_num >> 1);

    sal_memset(&ds_ma, 0, sizeof(DsMa_m));
    sal_memset(&ds_eth_mep, 0, sizeof(DsEthMep_m));
    sal_memset(&DsEthMep_mask, 0, sizeof(DsEthMep_m));

    /* set ma table entry */
    SetDsMa(V, txWithPortStatus_f   , &ds_ma , 0);
    SetDsMa(V, txWithSendId_f   , &ds_ma , 0);
    SetDsMa(V, txWithIfStatus_f   , &ds_ma , 0);
    SetDsMa(V, useVrfidLkup_f   , &ds_ma , 0);
    SetDsMa(V, txUntaggedOam_f   , &ds_ma , CTC_FLAG_ISSET(lmep_flag, CTC_OAM_Y1731_LMEP_FLAG_WITHOUT_GAL));
    SetDsMa(V, maIdLengthType_f   , &ds_ma , maid_length_type);

    SetDsEthMep(V, ccmInterval_f   , &ds_eth_mep , p_sys_tp_y1731_lmep->ccm_interval);
    SetDsMa(V, csfInterval_f   , &ds_ma , p_sys_tp_y1731_lmep->ccm_interval);

    SetDsMa(V, rxOamType_f          , &ds_ma , 8);

    SetDsMa(V, mplsTtl_f          , &ds_ma , p_sys_tp_y1731_lmep->mpls_ttl);
    SetDsMa(V, maNameIndex_f          , &ds_ma , p_sys_tp_y1731_maid->maid_index);
    SetDsMa(V, priorityIndex_f          , &ds_ma , p_sys_tp_y1731_lmep->tx_cos_exp);

    SetDsMa(V, sfFailWhileCfgType_f          , &ds_ma , 0);
    SetDsMa(V, apsEn_f              , &ds_ma , CTC_FLAG_ISSET(lmep_flag, CTC_OAM_Y1731_LMEP_FLAG_APS_EN));
    SetDsMa(V, maNameLen_f          , &ds_ma , p_sys_tp_y1731_maid->maid_len);
    SetDsMa(V, mdLvl_f              , &ds_ma ,  7);

    if (CTC_FLAG_ISSET(lmep_flag, CTC_OAM_Y1731_LMEP_FLAG_WITHOUT_GAL))
    {
        SetDsMa(V, packetType_f     , &ds_ma ,  0);
    }
    else
    {
        SetDsMa(V, packetType_f     , &ds_ma ,  2);
    }

    if (p_sys_chan_tp->key.section_oam)
    {
        SetDsMa(V, linkoam_f            , &ds_ma , 1);
        SetDsMa(V, mplsLabelValid_f     , &ds_ma , 0);
    }
    else
    {
        SetDsMa(V, linkoam_f            , &ds_ma , 0);
        SetDsMa(V, mplsLabelValid_f     , &ds_ma , 0xF);
    }

    if (CTC_NH_RESERVED_NHID_FOR_DROP == p_sys_tp_y1731_lmep->nhid)
    {
        uint8  gchip_id = 0;
        SetDsMa(V, nextHopExt_f         , &ds_ma , 0);
        SetDsMa(V, u1_g1_nextHopPtr_f         , &ds_ma , 0);
        sys_goldengate_get_gchip_id(lchip, &gchip_id);
        destmap = SYS_ENCODE_DESTMAP( gchip_id, SYS_RSV_PORT_DROP_ID);
        SetDsEthMep(V, destMap_f           , &ds_eth_mep , destmap );
        SetDsEthMep(V, apsBridgeEn_f           , &ds_eth_mep , 0);
    }
    else
    {
        sys_oam_nhop_info_t  mpls_nhop_info;
        sal_memset(&mpls_nhop_info, 0, sizeof(sys_oam_nhop_info_t));
        _sys_goldengate_oam_get_nexthop_info(lchip, p_sys_tp_y1731_lmep->nhid, CTC_FLAG_ISSET(lmep_flag, CTC_OAM_Y1731_LMEP_FLAG_PROTECTION_PATH), &mpls_nhop_info);
        SetDsMa(V, nextHopExt_f         , &ds_ma , mpls_nhop_info.nexthop_is_8w);
        SetDsMa(V, u1_g1_nextHopPtr_f         , &ds_ma , mpls_nhop_info.dsnh_offset);
        SetDsEthMep(V, destMap_f           , &ds_eth_mep , mpls_nhop_info.dest_map );
        SetDsEthMep(V, apsBridgeEn_f       , &ds_eth_mep , mpls_nhop_info.aps_bridge_en);
        SetDsMa(V, tunnelApsEn_f,           &ds_ma , mpls_nhop_info.mep_on_tunnel);
    }

    /* set mep table entry */
    SetDsEthMep(V, active_f                , &ds_eth_mep , CTC_FLAG_ISSET(lmep_flag, CTC_OAM_Y1731_LMEP_FLAG_MEP_EN));
    SetDsEthMep(V, isUp_f                  , &ds_eth_mep , 0);
    SetDsEthMep(V, ethOamP2PMode_f         , &ds_eth_mep , 1);
    SetDsEthMep(V, maIdCheckDisable_f      , &ds_eth_mep , 0);
    SetDsEthMep(V, cciEn_f                 , &ds_eth_mep , p_sys_tp_y1731_lmep->ccm_en);
    SetDsEthMep(V, enablePm_f              , &ds_eth_mep , CTC_FLAG_ISSET(lmep_flag, CTC_OAM_Y1731_LMEP_FLAG_DM_EN));

    SetDsEthMep(V, presentTrafficCheckEn_f , &ds_eth_mep , 0);
    SetDsEthMep(V, seqNumEn_f              , &ds_eth_mep , 0);
    SetDsEthMep(V, shareMacEn_f            , &ds_eth_mep , 1);

    SetDsEthMep(V, mepId_f                 , &ds_eth_mep , p_sys_tp_y1731_lmep->mep_id;);
    SetDsEthMep(V, tpidType_f              , &ds_eth_mep , 0);
    SetDsEthMep(V, mepPrimaryVid_f         , &ds_eth_mep ,  0);
    SetDsEthMep(V, maIndex_f               , &ds_eth_mep , p_sys_lmep->ma_index);

    SetDsEthMep(V, isRemote_f              , &ds_eth_mep , 0);
    SetDsEthMep(V, cciWhile_f              , &ds_eth_mep , 4);
    SetDsEthMep(V, dUnexpMepTimer_f        , &ds_eth_mep , 14);
    SetDsEthMep(V, dMismergeTimer_f        , &ds_eth_mep , 14);
    SetDsEthMep(V, dMegLvlTimer_f          , &ds_eth_mep , 14);
    SetDsEthMep(V, isBfd_f                 , &ds_eth_mep , 0);

    SetDsEthMep(V, mepType_f               , &ds_eth_mep , 6);
    SetDsEthMep(V, sfFailWhile_f           , &ds_eth_mep , 0);
    SetDsEthMep(V, learnEn_f               , &ds_eth_mep , 0);
    SetDsEthMep(V, apsSignalFailLocal_f    , &ds_eth_mep , 0);

    SetDsEthMep(V, autoGenEn_f             , &ds_eth_mep , 0);
    SetDsEthMep(V, presentTraffic_f        , &ds_eth_mep , 0);
    SetDsEthMep(V, portStatus_f            , &ds_eth_mep , 0);
    SetDsEthMep(V, rmepLastRdi_f           , &ds_eth_mep , 0);
    SetDsEthMep(V, presentRdi_f            , &ds_eth_mep , 0);
    SetDsEthMep(V, dUnexpMep_f             , &ds_eth_mep , 0);
    SetDsEthMep(V, dMismerge_f             , &ds_eth_mep , 0);
    SetDsEthMep(V, dMegLvl_f               , &ds_eth_mep , 0);
    SetDsEthMep(V, portId_f                , &ds_eth_mep , 0);
    SetDsEthMep(V, p2pUseUcDa_f            , &ds_eth_mep , 0);
    SetDsEthMep(V, autoGenPktPtr_f         , &ds_eth_mep , 0);
    SetDsEthMep(V, ccmSeqNum_f             , &ds_eth_mep , 0);


    index = p_sys_lmep->ma_index;
    cmd = DRV_IOW(DsMa_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_ma));

    index = p_sys_lmep->lmep_index;
    cmd = DRV_IOW(DsEthMep_t, DRV_ENTRY_FLAG);
    tbl_entry.data_entry = (uint32*)&ds_eth_mep;
    tbl_entry.mask_entry = (uint32*)&DsEthMep_mask;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &tbl_entry));
RETURN:

    return CTC_E_NONE;

}




int32
_sys_goldengate_oam_lmep_del_from_asic(uint8 lchip, sys_oam_lmep_com_t* p_sys_lmep)
{
    uint32 cmd          = 0;
    uint32 index        = 0;
     /*DsMa_m ds_ma;                    //DS_MA */
     /*DsEthMep_m ds_eth_mep;          // DS_ETH_MEP */
     /*DsEthMep_m DsEthMep_mask;*/
    DsMa_m ds_ma;
    DsEthMep_m ds_eth_mep;
    DsEthMep_m DsEthMep_mask;
    tbl_entry_t tbl_entry;

    if (SYS_OAM_NO_MEP_OPERATION(lchip, p_sys_lmep->p_sys_chan->master_chipid))
    {
        goto RETURN;
    }

    sal_memset(&ds_ma, 0, sizeof(DsMa_m));
    sal_memset(&ds_eth_mep, 0, sizeof(ds_eth_mep));
    sal_memset(&DsEthMep_mask, 0, sizeof(ds_eth_mep));

    index = p_sys_lmep->ma_index;
    cmd = DRV_IOW(DsMa_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_ma));

    index = p_sys_lmep->lmep_index;
    cmd = DRV_IOW(DsEthMep_t, DRV_ENTRY_FLAG);
    tbl_entry.data_entry = (uint32*)&ds_eth_mep;
    tbl_entry.mask_entry = (uint32*)&DsEthMep_mask;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &tbl_entry));

RETURN:

    return CTC_E_NONE;
}



int32
_sys_goldengate_oam_lmep_check_rmep_dloc(uint8 lchip, sys_oam_lmep_com_t* p_sys_lmep)
{
    ctc_slistnode_t* ctc_slistnode = NULL;
    ctc_slist_t* p_rmep_list = NULL;
    sys_oam_rmep_com_t* p_sys_rmep = NULL;
    uint32 cmd = 0;
    uint32 r_index = 0;
    uint32 l_index = 0;
    OamRxProcEtherCtl_m rx_ether_ctl;
    uint32 dloc_rdi = 0;
    uint8 idx = 0;
    uint8 defect_type       = 0;
    uint8 defect_sub_type   = 0;
    uint8 rdi_index         = 0;
    uint32 defect = 0;
    uint8 defect_check[CTC_GOLDENGATE_OAM_DEFECT_NUM] = {0};
    DsEthMep_m    ds_eth_mep;
    DsEthRmep_m   ds_eth_rmep;
    uint32 d_mismerge = 0;
    uint32 d_meg_lvl = 0;
    uint32 d_unexp_mep = 0;
    uint32 d_loc = 0;
    uint32 d_unexp_period = 0;
    uint32 mac_status_change = 0;
    uint32 rmepmacmismatch = 0;
    uint32 ether_defect_to_rdi = 0;

    p_rmep_list = p_sys_lmep->rmep_list;
    l_index = p_sys_lmep->lmep_index;

    if (SYS_OAM_NO_MEP_OPERATION(lchip, p_sys_lmep->p_sys_chan->master_chipid))
    {
        goto RETURN;
    }

    /*1.Get defect to RDI bitmap*/
    cmd = DRV_IOR(OamUpdateCtl_t, OamUpdateCtl_genRdiByDloc_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &dloc_rdi));

    sal_memset(&rx_ether_ctl,   0, sizeof(OamRxProcEtherCtl_m));
    cmd = DRV_IOR(OamRxProcEtherCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &rx_ether_ctl));

    /*2.According bitmap to get defect type*/
    for (idx = 0; idx < CTC_GOLDENGATE_OAM_DEFECT_NUM; idx++)
    {
        defect = (1 << idx);
        _sys_goldengate_oam_get_rdi_defect_type(lchip, (1 << idx), &defect_type, &defect_sub_type);

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
                    ether_defect_to_rdi = GetOamRxProcEtherCtl(V, etherDefectToRdi0_f, &rx_ether_ctl);
                    defect_check[idx] = CTC_IS_BIT_SET(ether_defect_to_rdi,  rdi_index);
                }
                else
                {
                    ether_defect_to_rdi = GetOamRxProcEtherCtl(V, etherDefectToRdi1_f, &rx_ether_ctl);
                    defect_check[idx] = CTC_IS_BIT_SET(ether_defect_to_rdi, (rdi_index - 32));
                }
            }
            break;

        default:
            defect_check[idx] = 0;
            break;
        }
    }

    /*3. check local MEP*/
    sal_memset(&ds_eth_mep,     0, sizeof(DsEthMep_m));
    cmd = DRV_IOR(DsEthMep_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, l_index, cmd, &ds_eth_mep));

    DRV_GET_FIELD_A(DsEthMep_t, DsEthMep_dMismergeTimer_f , &ds_eth_mep, &d_mismerge);
    GetDsEthMep(A, dMegLvl_f, &ds_eth_mep, &d_meg_lvl);
    DRV_GET_FIELD_A(DsEthMep_t, DsEthMep_dUnexpMep_f , &ds_eth_mep, &d_unexp_mep);

    if ((d_mismerge & defect_check[_sys_goldengate_oam_cal_bitmap_bit(lchip, CTC_OAM_DEFECT_MISMERGE)])
        || (d_meg_lvl & defect_check[_sys_goldengate_oam_cal_bitmap_bit(lchip, CTC_OAM_DEFECT_UNEXPECTED_LEVEL)])
        || (d_unexp_mep & defect_check[_sys_goldengate_oam_cal_bitmap_bit(lchip, CTC_OAM_DEFECT_UNEXPECTED_MEP)]))
    {
        SYS_OAM_DBG_INFO("Check Lmep[%d]:  d_mismerge %d,  d_meg_lvl % d ,d_unexp_mep % d\n",
                   l_index, d_mismerge, d_meg_lvl, d_unexp_mep );
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
            sal_memset(&ds_eth_rmep, 0, sizeof(DsEthRmep_m));
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, r_index, cmd, &ds_eth_rmep));
            DRV_GET_FIELD_A(DsEthRmep_t, DsEthRmep_dLoc_f , &ds_eth_rmep, &d_loc);
            GetDsEthRmep(A, dUnexpPeriod_f, &ds_eth_rmep, &d_unexp_period);
            GetDsEthRmep(A, macStatusChange_f, &ds_eth_rmep, &mac_status_change);
            GetDsEthRmep(A, rmepmacmismatch_f,  &ds_eth_rmep, &rmepmacmismatch);

            if ((d_loc & defect_check[_sys_goldengate_oam_cal_bitmap_bit(lchip, CTC_OAM_DEFECT_DLOC)])
                || (d_unexp_period & defect_check[_sys_goldengate_oam_cal_bitmap_bit(lchip, CTC_OAM_DEFECT_UNEXPECTED_PERIOD)])
                || (mac_status_change & defect_check[_sys_goldengate_oam_cal_bitmap_bit(lchip, CTC_OAM_DEFECT_MAC_STATUS_CHANGE)])
                || (rmepmacmismatch & defect_check[_sys_goldengate_oam_cal_bitmap_bit(lchip, CTC_OAM_DEFECT_SRC_MAC_MISMATCH)]))
            {
                SYS_OAM_DBG_INFO("Check Rmep[%d]:  d_loc %d,  d_unexp_period % d ,mac_status_change % d, rmepmacmismatch %d\n",
                           r_index, d_loc, d_unexp_period, mac_status_change,  rmepmacmismatch);
                return CTC_E_OAM_RMEP_D_LOC_PRESENT;
            }
        }
    }
RETURN:
    return CTC_E_NONE;

}

int32
_sys_goldengate_oam_lmep_update_eth_asic(uint8 lchip, sys_oam_lmep_com_t* p_sys_lmep)
{
    sys_oam_lmep_y1731_t* p_sys_lmep_eth  = (sys_oam_lmep_y1731_t*)p_sys_lmep;
    uint32 cmd        = 0;
    uint32 index      = 0;
    uint32 value = 0;
    DsMa_m ds_ma;                    /*DS_MA */
    DsEthMep_m ds_eth_mep;          /* DS_ETH_MEP */
    DsEthRmep_m ds_eth_rmep;
    DsEthMep_m DsEthMep_mask;     /* DS_ETH_MEP */
    DsEthRmep_m DsEthRmep_mask;

    tbl_entry_t tbl_entry;

    sys_oam_rmep_com_t* p_sys_rmep = NULL;

    if (SYS_OAM_NO_MEP_OPERATION(lchip, p_sys_lmep->p_sys_chan->master_chipid))
    {
        goto RETURN;
    }

    sal_memset(&ds_eth_mep, 0, sizeof(ds_eth_mep));
    sal_memset(&DsEthMep_mask, 0xFF, sizeof(ds_eth_mep));
    index = p_sys_lmep->lmep_index;
    cmd = DRV_IOR(DsEthMep_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_eth_mep));

    sal_memset(&ds_ma, 0, sizeof(DsMa_m));
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
        sal_memset(&ds_eth_rmep, 0, sizeof(DsEthRmep_m));
        sal_memset(&DsEthRmep_mask, 0xFF, sizeof(DsEthRmep_m));
        cmd = DRV_IOR(DsEthRmep_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_eth_rmep));
    }

    switch (p_sys_lmep_eth->update_type)
    {
    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_MEP_EN:

        if (CTC_FLAG_ISSET(p_sys_lmep_eth->flag, CTC_OAM_Y1731_LMEP_FLAG_MEP_EN))
        {
            SetDsEthMep(V, active_f                , &ds_eth_mep ,  1);
            SetDsEthMep(V, isRemote_f              , &ds_eth_mep , 0);
            SetDsEthMep(V, cciWhile_f              , &ds_eth_mep , 4);
            SetDsEthMep(V, dUnexpMepTimer_f        , &ds_eth_mep , 14);
            SetDsEthMep(V, dMismergeTimer_f        , &ds_eth_mep , 14);
            SetDsEthMep(V, dMegLvlTimer_f          , &ds_eth_mep , 14);
            SetDsEthMep(V, portStatus_f            , &ds_eth_mep , 0);
            SetDsEthMep(V, presentTraffic_f        , &ds_eth_mep , 0);
            SetDsEthMep(V, presentRdi_f            , &ds_eth_mep , 0);
            SetDsEthMep(V, ccmSeqNum_f            , &ds_eth_mep , 0);
            SetDsEthMep(V, dUnexpMep_f             , &ds_eth_mep , 0);
            SetDsEthMep(V, dMismerge_f             , &ds_eth_mep , 0);
            SetDsEthMep(V, dMegLvl_f               , &ds_eth_mep , 0);
        }
        else
        {
            SetDsEthMep(V, active_f                , &ds_eth_mep ,  0);
        }
        SetDsEthMep(V, active_f                , &DsEthMep_mask ,  0);

        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_CCM_EN:
        if (CTC_FLAG_ISSET(p_sys_lmep_eth->flag, CTC_OAM_Y1731_LMEP_FLAG_P2P_MODE))
        {
            p_sys_rmep = (sys_oam_rmep_com_t*)CTC_SLISTHEAD(p_sys_lmep->rmep_list);
            if (p_sys_rmep)
            {
                cmd = DRV_IOR(DsEthRmep_t, DsEthRmep_isCsfEn_f);
                DRV_IOCTL(lchip, p_sys_rmep->rmep_index, cmd, &value);
                if (value)
                {
                    return CTC_E_INVALID_CONFIG;
                }
            }
        }
        SetDsEthMep(V, cciEn_f                , &ds_eth_mep ,  p_sys_lmep_eth->ccm_en);
        SetDsEthMep(V, cciEn_f                , &DsEthMep_mask ,  0);
        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_CCM_SEQ_NUM_EN:
        SetDsEthMep(V, seqNumEn_f                , &ds_eth_mep ,  CTC_FLAG_ISSET(p_sys_lmep_eth->flag, CTC_OAM_Y1731_LMEP_FLAG_CCM_SEQ_NUM_EN));
        SetDsEthMep(V, seqNumEn_f                , &DsEthMep_mask ,  0);
        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_RDI:
        if (0 == p_sys_lmep_eth->present_rdi)
        {
            CTC_ERROR_RETURN(_sys_goldengate_oam_lmep_check_rmep_dloc(lchip, p_sys_lmep));
        }
        SetDsEthMep(V, presentRdi_f                , &ds_eth_mep ,  p_sys_lmep_eth->present_rdi);
        SetDsEthMep(V, presentRdi_f                , &DsEthMep_mask ,  0);

        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_TX_COS_EXP:
        SetDsMa(V, priorityIndex_f                , &ds_ma ,  p_sys_lmep_eth->tx_cos_exp);
        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_PORT_STATUS:
        SetDsEthMep(V, portStatus_f                , &ds_eth_mep ,  p_sys_lmep_eth->port_status);
        SetDsEthMep(V, portStatus_f                , &DsEthMep_mask ,  0);
        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_LMEP_SF_STATE:
    /*not suppport*/
        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_TX_CSF_EN:
        cmd = DRV_IOR(DsEthMep_t, DsEthMep_cciEn_f);
        DRV_IOCTL(lchip, p_sys_lmep->lmep_index, cmd, &value);
        if (value)
        {
            return CTC_E_INVALID_CONFIG;
        }

        SetDsEthRmep(V, firstPktRx_f           , &ds_eth_rmep , 1);
        SetDsEthRmep(V, firstPktRx_f           , &DsEthRmep_mask ,  0);
        SetDsEthRmep(V, dLoc_f                 , &ds_eth_rmep , 1);
        SetDsEthRmep(V, dLoc_f           , &DsEthRmep_mask ,  0);
        SetDsEthRmep(V, isCsfEn_f, &ds_eth_rmep , 1);
        SetDsEthRmep(V, isCsfEn_f, &DsEthRmep_mask , 0);
        SetDsEthRmep(V, u1_g2_isCsfTxEn_f      , &ds_eth_rmep ,  p_sys_lmep_eth->tx_csf_en);
        SetDsEthRmep(V, u1_g2_isCsfTxEn_f      , &DsEthRmep_mask , 0);
        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_TX_CSF_TYPE:
        SetDsEthRmep(V, u1_g2_csfType_f      , &ds_eth_rmep , p_sys_lmep_eth->tx_csf_type);
        SetDsEthRmep(V, u1_g2_csfType_f      , &DsEthRmep_mask , 0);
        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_TX_CSF_USE_UC_DA:
        SetDsEthRmep(V, u1_g2_isCsfUseUcDa_f   , &ds_eth_rmep ,  p_sys_lmep_eth->tx_csf_use_uc_da );
        SetDsEthRmep(V, u1_g2_isCsfUseUcDa_f   , &DsEthMep_mask , 0);
        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_P2P_TX_USE_UC_DA:
        SetDsEthMep(V, p2pUseUcDa_f                , &ds_eth_mep , p_sys_lmep_eth->ccm_p2p_use_uc_da);
        SetDsEthMep(V, p2pUseUcDa_f                , &DsEthMep_mask ,  0);
        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_DM_EN:
        SetDsEthMep(V, enablePm_f                  , &ds_eth_mep , p_sys_lmep_eth->dm_en);
        SetDsEthMep(V, enablePm_f                  , &DsEthMep_mask ,  0);
        break;
    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_TRPT:
        SetDsEthMep(V, autoGenEn_f                , &ds_eth_mep , p_sys_lmep_eth->trpt_en);
        SetDsEthMep(V, autoGenEn_f                , &DsEthMep_mask ,  0);
        SetDsEthMep(V, autoGenPktPtr_f           , &ds_eth_mep , p_sys_lmep_eth->trpt_session_id);
        SetDsEthMep(V, autoGenPktPtr_f           , &DsEthMep_mask ,  0);
        if (p_sys_lmep_eth->trpt_en)
        {
            sys_goldengate_oam_trpt_set_autogen_ptr(lchip, p_sys_lmep_eth->trpt_session_id, p_sys_lmep->lmep_index);
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
    tbl_entry.mask_entry = (uint32*)&DsEthMep_mask;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &tbl_entry));

    if (CTC_FLAG_ISSET(p_sys_lmep_eth->flag, CTC_OAM_Y1731_LMEP_FLAG_P2P_MODE)
        && ((CTC_OAM_Y1731_LMEP_UPDATE_TYPE_TX_CSF_EN == p_sys_lmep_eth->update_type)
            || (CTC_OAM_Y1731_LMEP_UPDATE_TYPE_TX_CSF_TYPE == p_sys_lmep_eth->update_type)
            || (CTC_OAM_Y1731_LMEP_UPDATE_TYPE_TX_CSF_USE_UC_DA == p_sys_lmep_eth->update_type)))
    {
        index = p_sys_rmep->rmep_index;
        cmd = DRV_IOW(DsEthRmep_t, DRV_ENTRY_FLAG);
        tbl_entry.data_entry = (uint32*)&ds_eth_rmep;
        tbl_entry.mask_entry = (uint32*)&DsEthRmep_mask;
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &tbl_entry));
    }

RETURN:

    return CTC_E_NONE;
}

int32
_sys_goldengate_oam_lmep_update_tp_y1731_asic(uint8 lchip, sys_oam_lmep_com_t* p_sys_lmep)
{
    sys_oam_lmep_y1731_t* p_sys_lmep_tp  = (sys_oam_lmep_y1731_t*)p_sys_lmep;
    uint32 cmd        = 0;
    uint32 index      = 0;
    uint32 value = 0;
    DsMa_m ds_ma;                    /*DS_MA */
    DsEthMep_m ds_eth_mep;          /* DS_ETH_MEP */
    DsEthRmep_m ds_eth_rmep;
    DsEthMep_m DsEthMep_mask;     /* DS_ETH_MEP */
    DsEthRmep_m DsEthRmep_mask;
    tbl_entry_t tbl_entry;
    sys_oam_rmep_com_t* p_sys_rmep = NULL;
    sys_oam_nhop_info_t mpls_nhop_info;

    if (SYS_OAM_NO_MEP_OPERATION(lchip, p_sys_lmep->p_sys_chan->master_chipid))
    {
        goto RETURN;
    }

    sal_memset(&ds_eth_mep, 0, sizeof(DsEthMep_m));
    sal_memset(&DsEthMep_mask, 0xFF, sizeof(DsEthMep_m));
    index = p_sys_lmep->lmep_index;
    cmd = DRV_IOR(DsEthMep_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_eth_mep));

    sal_memset(&ds_ma, 0, sizeof(DsMa_m));
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
        sal_memset(&ds_eth_rmep, 0, sizeof(DsEthRmep_m));
        sal_memset(&DsEthRmep_mask, 0xFF, sizeof(DsEthRmep_m));
        cmd = DRV_IOR(DsEthRmep_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_eth_rmep));
    }

    switch (p_sys_lmep_tp->update_type)
    {
    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_MEP_EN:

        if (CTC_FLAG_ISSET(p_sys_lmep_tp->flag, CTC_OAM_Y1731_LMEP_FLAG_MEP_EN))
        {
            SetDsEthMep(V, active_f                , &ds_eth_mep ,  1);
            SetDsEthMep(V, isRemote_f              , &ds_eth_mep , 0);
            SetDsEthMep(V, cciWhile_f              , &ds_eth_mep , 4);
            SetDsEthMep(V, dUnexpMepTimer_f        , &ds_eth_mep , 14);
            SetDsEthMep(V, dMismergeTimer_f        , &ds_eth_mep , 14);
            SetDsEthMep(V, dMegLvlTimer_f          , &ds_eth_mep , 14);
            SetDsEthMep(V, portStatus_f            , &ds_eth_mep , 0);
            SetDsEthMep(V, presentTraffic_f        , &ds_eth_mep , 0);
            SetDsEthMep(V, presentRdi_f            , &ds_eth_mep , 0);
            SetDsEthMep(V, ccmSeqNum_f            , &ds_eth_mep , 0);
            SetDsEthMep(V, dUnexpMep_f             , &ds_eth_mep , 0);
            SetDsEthMep(V, dMismerge_f             , &ds_eth_mep , 0);
            SetDsEthMep(V, dMegLvl_f               , &ds_eth_mep , 0);
        }
        else
        {
            SetDsEthMep(V, active_f                , &ds_eth_mep ,  0);
        }
        SetDsEthMep(V, active_f                , &DsEthMep_mask ,  0);
        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_CCM_EN:
        p_sys_rmep = (sys_oam_rmep_com_t*)CTC_SLISTHEAD(p_sys_lmep->rmep_list);
        if (p_sys_rmep)
        {
            cmd = DRV_IOR(DsEthRmep_t, DsEthRmep_isCsfEn_f);
            DRV_IOCTL(lchip, p_sys_rmep->rmep_index, cmd, &value);
            if (value)
            {
                return CTC_E_INVALID_CONFIG;
            }
        }
        SetDsEthMep(V, cciEn_f                , &ds_eth_mep ,  p_sys_lmep_tp->ccm_en);
        SetDsEthMep(V, cciEn_f                , &DsEthMep_mask ,  0);
        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_RDI:
        if (0 == p_sys_lmep_tp->present_rdi)
        {
            CTC_ERROR_RETURN(_sys_goldengate_oam_lmep_check_rmep_dloc(lchip, p_sys_lmep));
        }
        SetDsEthMep(V, presentRdi_f                , &ds_eth_mep ,  p_sys_lmep_tp->present_rdi);
        SetDsEthMep(V, presentRdi_f                , &DsEthMep_mask ,  0);
        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_TX_COS_EXP:
        SetDsMa(V, priorityIndex_f                , &ds_ma ,  p_sys_lmep_tp->tx_cos_exp);
        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_LMEP_SF_STATE:
       /*  ds_ma.aps_signal_fail_local           = p_sys_lmep_tp->sf_state;*/
        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_TX_CSF_EN:
        cmd = DRV_IOR(DsEthMep_t, DsEthMep_cciEn_f);
        DRV_IOCTL(lchip, p_sys_lmep->lmep_index, cmd, &value);
        if (value)
        {
            return CTC_E_INVALID_CONFIG;
        }
        SetDsEthRmep(V, firstPktRx_f, &ds_eth_rmep , 1);
        SetDsEthRmep(V, firstPktRx_f, &DsEthRmep_mask , 0);
        SetDsEthRmep(V, dLoc_f, &ds_eth_rmep , 1);
        SetDsEthRmep(V, dLoc_f, &DsEthRmep_mask , 0);
        SetDsEthRmep(V, isCsfEn_f, &ds_eth_rmep , 1);
        SetDsEthRmep(V, isCsfEn_f, &DsEthRmep_mask , 0);
        SetDsEthRmep(V, u1_g2_isCsfTxEn_f, &ds_eth_rmep ,  p_sys_lmep_tp->tx_csf_en);
        SetDsEthRmep(V, u1_g2_isCsfTxEn_f, &DsEthRmep_mask , 0);
        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_TX_CSF_TYPE:
        SetDsEthRmep(V, u1_g2_csfType_f      , &ds_eth_rmep , p_sys_lmep_tp->tx_csf_type);
        SetDsEthRmep(V, u1_g2_csfType_f      , &DsEthRmep_mask , 0);
        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_DM_EN:
        SetDsEthMep(V, enablePm_f                , &ds_eth_mep , p_sys_lmep_tp->dm_en);
        SetDsEthMep(V, enablePm_f                , &DsEthMep_mask ,  0);
        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_NHOP:
        sal_memset(&mpls_nhop_info, 0, sizeof(sys_oam_nhop_info_t));

        _sys_goldengate_oam_get_nexthop_info(lchip, p_sys_lmep_tp->nhid, CTC_FLAG_ISSET(p_sys_lmep_tp->flag, CTC_OAM_Y1731_LMEP_FLAG_PROTECTION_PATH), &mpls_nhop_info);
        SetDsMa(V, nextHopExt_f         , &ds_ma , mpls_nhop_info.nexthop_is_8w);
        SetDsMa(V, u1_g1_nextHopPtr_f         , &ds_ma , mpls_nhop_info.dsnh_offset);
        SetDsEthMep(V, destMap_f           , &ds_eth_mep , mpls_nhop_info.dest_map );
        SetDsEthMep(V, apsBridgeEn_f       , &ds_eth_mep , mpls_nhop_info.aps_bridge_en);
        SetDsEthMep(V, destMap_f           , &DsEthMep_mask , 0 );
        SetDsEthMep(V, apsBridgeEn_f       , &DsEthMep_mask , 0 );
        SetDsMa(V, tunnelApsEn_f,           &ds_ma , mpls_nhop_info.mep_on_tunnel);
        SetDsMa(V, protectingPath_f,        &ds_ma , CTC_FLAG_ISSET(p_sys_lmep_tp->flag, CTC_OAM_Y1731_LMEP_FLAG_PROTECTION_PATH));

        if (!mpls_nhop_info.mep_on_tunnel && mpls_nhop_info.aps_bridge_en && !mpls_nhop_info.multi_aps_en)
        {
            DsApsBridge_m ds_aps_bridge;
            cmd = DRV_IOR(DsApsBridge_t, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, mpls_nhop_info.dest_map, cmd, &ds_aps_bridge);
            SYS_OAM_DBG_INFO("Update dsma aps bridge:%u\n",  mpls_nhop_info.dest_map);
            SetDsMa(V, protectingPath_f, &ds_ma ,  GetDsApsBridge(V, protectingEn_f, &ds_aps_bridge)?1:0);
        }

        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_MEL:
        SetDsMa(V, mdLvl_f              , &ds_ma ,  p_sys_lmep_tp->md_level);
        break;
    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_TRPT:
        SetDsEthMep(V, autoGenEn_f                , &ds_eth_mep , p_sys_lmep_tp->trpt_en);
        SetDsEthMep(V, autoGenEn_f                , &DsEthMep_mask ,  0);
        SetDsEthMep(V, autoGenPktPtr_f           , &ds_eth_mep , p_sys_lmep_tp->trpt_session_id);
        SetDsEthMep(V, autoGenPktPtr_f           , &DsEthMep_mask ,  0);
        if (p_sys_lmep_tp->trpt_en)
        {
            sys_goldengate_oam_trpt_set_autogen_ptr(lchip, p_sys_lmep_tp->trpt_session_id, p_sys_lmep->lmep_index);
        }
        break;
    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_TTL:
        SetDsMa(V, mplsTtl_f, &ds_ma, p_sys_lmep_tp->mpls_ttl);
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
    tbl_entry.mask_entry = (uint32*)&DsEthMep_mask;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &tbl_entry));

    if ((CTC_OAM_Y1731_LMEP_UPDATE_TYPE_TX_CSF_EN == p_sys_lmep_tp->update_type)
        || (CTC_OAM_Y1731_LMEP_UPDATE_TYPE_TX_CSF_TYPE == p_sys_lmep_tp->update_type))
    {
        index = p_sys_rmep->rmep_index;
        cmd = DRV_IOW(DsEthRmep_t, DRV_ENTRY_FLAG);
        tbl_entry.data_entry = (uint32*)&ds_eth_rmep;
        tbl_entry.mask_entry = (uint32*)&DsEthRmep_mask;
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &tbl_entry));
    }

RETURN:

    return CTC_E_NONE;
}

int32
_sys_goldengate_oam_lmep_update_master_chip(sys_oam_chan_com_t* p_sys_chan, uint32* master_chipid)
{
    uint8 mep_type = p_sys_chan->mep_type;
    uint8 lchip = p_sys_chan->lchip;
    uint32 key_index = 0;
    DsEgressXcOamBfdHashKey_m ds_oam_key;

    switch (mep_type)
    {
        case CTC_OAM_MEP_TYPE_IP_BFD:
        case CTC_OAM_MEP_TYPE_MPLS_BFD:
            key_index =    ((sys_oam_chan_bfd_t*)p_sys_chan)->key.com.key_index;
            break;
        case CTC_OAM_MEP_TYPE_MPLS_TP_Y1731:
        case CTC_OAM_MEP_TYPE_MPLS_TP_BFD:
            key_index =    ((sys_oam_chan_tp_t*)p_sys_chan)->key.com.key_index;
            break;
        case CTC_OAM_MEP_TYPE_ETH_1AG:
        case CTC_OAM_MEP_TYPE_ETH_Y1731:
            key_index =    ((sys_oam_chan_eth_t*)p_sys_chan)->key.com.key_index;
            break;
        default:
            break;
    }
    sal_memset(&ds_oam_key, 0, sizeof(ds_oam_key));
    CTC_ERROR_RETURN(sys_goldengate_oam_key_read_io(lchip, DsEgressXcOamBfdHashKey_t, key_index, &ds_oam_key));

    switch (mep_type)
    {
        case CTC_OAM_MEP_TYPE_IP_BFD:
        case CTC_OAM_MEP_TYPE_MPLS_BFD:
            {
                SetDsEgressXcOamBfdHashKey(V, oamDestChipId_f, &ds_oam_key,         *master_chipid);
            }
            break;
        case CTC_OAM_MEP_TYPE_MPLS_TP_Y1731:
        case CTC_OAM_MEP_TYPE_MPLS_TP_BFD:
            {
                if (p_sys_chan->link_oam)
                {
                    SetDsEgressXcOamMplsSectionHashKey(V, oamDestChipId_f, &ds_oam_key, *master_chipid);
                }
                else
                {
                    SetDsEgressXcOamMplsLabelHashKey(V, oamDestChipId_f, &ds_oam_key,           *master_chipid);
                }
            }
            break;
        case CTC_OAM_MEP_TYPE_ETH_1AG:
        case CTC_OAM_MEP_TYPE_ETH_Y1731:
            {
                if (!p_sys_chan->link_oam)
                {
                    SetDsEgressXcOamEthHashKey(V, oamDestChipId_f, &ds_oam_key,         *master_chipid);
                }
            }
            break;
        default:
            break;
    }

    CTC_ERROR_RETURN(sys_goldengate_oam_key_write_io(lchip, DsEgressXcOamBfdHashKey_t, key_index, &ds_oam_key));
    p_sys_chan->master_chipid = *master_chipid;
    return CTC_E_NONE;
}


#define RMEP "Function Begin"

sys_oam_rmep_com_t*
_sys_goldengate_oam_rmep_lkup(uint8 lchip, sys_oam_lmep_com_t* p_sys_lmep,
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
_sys_goldengate_oam_rmep_add_to_db(uint8 lchip, sys_oam_rmep_com_t* p_sys_rmep)
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
    ctc_vector_add(g_gg_oam_master[lchip]->mep_vec, rmep_index, p_sys_rmep);

    return CTC_E_NONE;
}

int32
_sys_goldengate_oam_rmep_del_from_db(uint8 lchip, sys_oam_rmep_com_t* p_sys_rmep)
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

    ctc_vector_del(g_gg_oam_master[lchip]->mep_vec, rmep_index);

    return CTC_E_NONE;
}

int32
_sys_goldengate_oam_rmep_build_index(uint8 lchip, sys_oam_rmep_com_t* p_sys_rmep)
{
    sys_oam_lmep_com_t* p_sys_lmep = NULL;
    sys_goldengate_opf_t opf;
    uint32 offset = 0;

    p_sys_lmep = p_sys_rmep->p_sys_lmep;

    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
    if (P_COMMON_OAM_MASTER_GLB(lchip).mep_index_alloc_by_sdk)
    {
        lchip           = p_sys_lmep->lchip;
        opf.pool_index  = 0;

        if ((CTC_OAM_MEP_TYPE_MPLS_BFD == p_sys_lmep->p_sys_chan->mep_type)
            || (CTC_OAM_MEP_TYPE_IP_BFD == p_sys_lmep->p_sys_chan->mep_type)
            || (CTC_OAM_MEP_TYPE_MPLS_TP_BFD == p_sys_lmep->p_sys_chan->mep_type))
        {
             /*opf.pool_type  = OPF_OAM_MEP_BFD;*/
            opf.pool_type  = OPF_OAM_MEP_LMEP;
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
                CTC_ERROR_RETURN(sys_goldengate_opf_alloc_offset(lchip, &opf, 1, &offset));
                SYS_OAM_TBL_CNT(lchip, SYS_OAM_TBL_MEP, 1, 1);
            }
        }

        p_sys_rmep->rmep_index = offset;
    }
    else
    {
        offset = p_sys_rmep->rmep_index;
        SYS_OAM_TBL_CNT(lchip, SYS_OAM_TBL_MEP, 1, 1);
    }

    SYS_OAM_DBG_INFO("RMEP: lchip->%d, index->%d\n", lchip, offset);

    return CTC_E_NONE;
}

int32
_sys_goldengate_oam_rmep_free_index(uint8 lchip, sys_oam_rmep_com_t* p_sys_rmep)
{
    sys_oam_lmep_com_t* p_sys_lmep = NULL;
    sys_goldengate_opf_t opf;
    uint32 offset = 0;

    p_sys_lmep = p_sys_rmep->p_sys_lmep;

    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
    if (P_COMMON_OAM_MASTER_GLB(lchip).mep_index_alloc_by_sdk)
    {
        lchip = p_sys_lmep->lchip;
        offset = p_sys_rmep->rmep_index;
        opf.pool_index = 0;
        if ((CTC_OAM_MEP_TYPE_MPLS_BFD == p_sys_lmep->p_sys_chan->mep_type)
            || (CTC_OAM_MEP_TYPE_IP_BFD == p_sys_lmep->p_sys_chan->mep_type)
            || (CTC_OAM_MEP_TYPE_MPLS_TP_BFD == p_sys_lmep->p_sys_chan->mep_type))
        {
             /*opf.pool_type  = OPF_OAM_MEP_BFD;*/
            opf.pool_type  = OPF_OAM_MEP_LMEP;
        }
        else
        {
            opf.pool_type  = OPF_OAM_MEP_LMEP;
            if (((CTC_OAM_MEP_TYPE_ETH_1AG == p_sys_lmep->p_sys_chan->mep_type)
                ||(CTC_OAM_MEP_TYPE_ETH_Y1731 == p_sys_lmep->p_sys_chan->mep_type))
                && (!CTC_FLAG_ISSET(((sys_oam_lmep_y1731_t*)p_sys_lmep)->flag, CTC_OAM_Y1731_LMEP_FLAG_P2P_MODE)))
            {
                CTC_ERROR_RETURN(sys_goldengate_opf_free_offset(lchip, &opf, 1, offset));
                SYS_OAM_TBL_CNT(lchip, SYS_OAM_TBL_MEP, 0, 1);
            }
        }

    }
    else
    {
        SYS_OAM_TBL_CNT(lchip, SYS_OAM_TBL_MEP, 0, 1);
    }

    return CTC_E_NONE;

}

int32
_sys_goldengate_oam_rmep_add_eth_to_asic(uint8 lchip, sys_oam_rmep_com_t* p_sys_rmep)
{
    DsEthRmep_m ds_eth_rmep;
    DsEthRmep_m DsEthRmep_mask;
    DsEthMep_m ds_eth_mep;
    DsEthMep_m DsEthMep_mask;
    sys_oam_lmep_com_t* p_sys_lmep = NULL;
    sys_oam_rmep_y1731_t* p_sys_eth_rmep = NULL;
    uint32 cmd          = 0;
    uint32 index        = 0;
    uint32 rmep_flag    = 0;
    uint32 lmep_flag    = 0;
    uint8  tx_csf_type  = 0;
    tbl_entry_t tbl_entry;
    hw_mac_addr_t mac_sa;

    p_sys_lmep     = p_sys_rmep->p_sys_lmep;
    p_sys_eth_rmep = (sys_oam_rmep_y1731_t*)p_sys_rmep;

    if (SYS_OAM_NO_MEP_OPERATION(lchip, p_sys_lmep->p_sys_chan->master_chipid))
    {
        goto RETURN;
    }

    rmep_flag   = p_sys_eth_rmep->flag;
    lmep_flag   = ((sys_oam_lmep_y1731_t*)p_sys_lmep)->flag;
    tx_csf_type = ((sys_oam_lmep_y1731_t*)p_sys_lmep)->tx_csf_type;

    sal_memset(&ds_eth_rmep, 0, sizeof(DsEthRmep_m));
    sal_memset(&DsEthRmep_mask, 0, sizeof(DsEthRmep_m));

     SetDsEthRmep(V, active_f               , &ds_eth_rmep , CTC_FLAG_ISSET(rmep_flag, CTC_OAM_Y1731_RMEP_FLAG_MEP_EN));
     SetDsEthRmep(V, ethOamP2PMode_f        , &ds_eth_rmep , CTC_FLAG_ISSET(lmep_flag, CTC_OAM_Y1731_LMEP_FLAG_P2P_MODE));
     SetDsEthRmep(V, seqNumEn_f             , &ds_eth_rmep , CTC_FLAG_ISSET(rmep_flag, CTC_OAM_Y1731_RMEP_FLAG_CCM_SEQ_NUM_EN));
     SetDsEthRmep(V, macAddrUpdateDisable_f , &ds_eth_rmep , !CTC_FLAG_ISSET(rmep_flag, CTC_OAM_Y1731_RMEP_FLAG_MAC_UPDATE_EN));
     SetDsEthRmep(V, apsSignalFailRemote_f  , &ds_eth_rmep , 0);

     if (g_gg_oam_master[lchip]->timer_update_disable)
     {
         SetDsEthRmep(V, firstPktRx_f           , &ds_eth_rmep , 1);
     }
     else
     {
         SetDsEthRmep(V, firstPktRx_f           , &ds_eth_rmep , 0);
     }
    SetDsEthRmep(V, dLoc_f                 , &ds_eth_rmep , 0);
    if (CTC_FLAG_ISSET(lmep_flag, CTC_OAM_Y1731_LMEP_FLAG_P2P_MODE))
    {
        SetDsEthRmep(V, isCsfEn_f              , &ds_eth_rmep , CTC_FLAG_ISSET(rmep_flag, CTC_OAM_Y1731_RMEP_FLAG_CSF_EN));
        if (CTC_FLAG_ISSET(rmep_flag, CTC_OAM_Y1731_RMEP_FLAG_CSF_EN))
        {
            SetDsEthRmep(V, firstPktRx_f           , &ds_eth_rmep , 1);
            SetDsEthRmep(V, dLoc_f                 , &ds_eth_rmep , 1);
        }

        if (CTC_FLAG_ISSET(lmep_flag, CTC_OAM_Y1731_LMEP_FLAG_TX_CSF_EN))
        {
            SetDsEthRmep(V, u1_g2_isCsfTxEn_f      , &ds_eth_rmep , 1);
            SetDsEthRmep(V, u1_g2_rxCsfType_f      , &ds_eth_rmep , tx_csf_type);
            SetDsEthRmep(V, u1_g2_dCsf_f           , &ds_eth_rmep , 0);
            SetDsEthRmep(V, u1_g2_rxCsfWhile_f     , &ds_eth_rmep , 0);
            SetDsEthRmep(V, u1_g2_isCsfUseUcDa_f   , &ds_eth_rmep , 0);
        }
        else
        {
            SetDsEthRmep(V, u1_g1_ccmSeqNum_f      , &ds_eth_rmep , 0);
        }
    }

    if (CTC_FLAG_ISSET(lmep_flag, CTC_OAM_Y1731_LMEP_FLAG_P2P_MODE))
    {
        SetDsEthRmep(V, u3_g2_mepId_f          , &ds_eth_rmep , p_sys_eth_rmep->rmep_id);
    }
    else
    {
        SetDsEthRmep(V, u3_g1_mepIndex_f       , &ds_eth_rmep , p_sys_rmep->p_sys_lmep->lmep_index);
    }

    SYS_GOLDENGATE_SET_HW_MAC(mac_sa, p_sys_eth_rmep->rmep_mac);
    SetDsEthRmep(A, rmepMacSa_f            , &ds_eth_rmep , mac_sa);

    SetDsEthRmep(V, isRemote_f             , &ds_eth_rmep , 1);
    SetDsEthRmep(V, rmepWhile_f            , &ds_eth_rmep , 14);
    SetDsEthRmep(V, dUnexpPeriodTimer_f    , &ds_eth_rmep , 14);
    SetDsEthRmep(V, rmepLastPortStatus_f   , &ds_eth_rmep , 2);
    SetDsEthRmep(V, rmepLastIntfStatus_f   , &ds_eth_rmep , 1);
    SetDsEthRmep(V, cntShiftWhile_f        , &ds_eth_rmep , 4);

    SetDsEthRmep(V, isBfd_f                , &ds_eth_rmep , 0);
    SetDsEthRmep(V, isUp_f                 , &ds_eth_rmep , 0);
    SetDsEthRmep(V, mepType_f              , &ds_eth_rmep , 0);
    SetDsEthRmep(V, sfFailWhile_f          , &ds_eth_rmep , 0);
    SetDsEthRmep(V, seqNumFailCounter_f    , &ds_eth_rmep , 0);
    SetDsEthRmep(V, rmepLastRdi_f          , &ds_eth_rmep , 0);
    SetDsEthRmep(V, dUnexpPeriod_f         , &ds_eth_rmep , 0);
    SetDsEthRmep(V, expCcmNum_f            , &ds_eth_rmep , 0);

    SetDsEthRmep(V, macStatusChange_f      , &ds_eth_rmep , 0);
    SetDsEthRmep(V, rmepmacmismatch_f      , &ds_eth_rmep , 0);
    SetDsEthRmep(V, sfState_f              , &ds_eth_rmep , 0);

    index = p_sys_rmep->rmep_index;
    cmd = DRV_IOW(DsEthRmep_t, DRV_ENTRY_FLAG);
    tbl_entry.data_entry = (uint32*)&ds_eth_rmep;
    tbl_entry.mask_entry = (uint32*)&DsEthRmep_mask;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &tbl_entry));


    sal_memset(&ds_eth_mep, 0, sizeof(DsEthMep_m));
    sal_memset(&DsEthMep_mask, 0xFF, sizeof(DsEthMep_m));
    index = p_sys_lmep->lmep_index;
    cmd = DRV_IOR(DsEthMep_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_eth_mep));
    SetDsEthMep(V, unknownCcmNum_f, &ds_eth_mep , 0);
    SetDsEthMep(V, unknownCcmNum_f, &DsEthMep_mask , 0);
    cmd = DRV_IOW(DsEthMep_t, DRV_ENTRY_FLAG);
    tbl_entry.data_entry = (uint32*)&ds_eth_mep;
    tbl_entry.mask_entry = (uint32*)&DsEthMep_mask;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &tbl_entry));

RETURN:
    return CTC_E_NONE;
}

int32
_sys_goldengate_oam_rmep_add_tp_y1731_to_asic(uint8 lchip, sys_oam_rmep_com_t* p_sys_rmep)
{
    DsEthRmep_m ds_eth_rmep;
    DsEthRmep_m DsEthRmep_mask;
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

    sal_memset(&ds_eth_rmep, 0, sizeof(DsEthRmep_m));
    sal_memset(&DsEthRmep_mask, 0, sizeof(DsEthRmep_m));

    SetDsEthRmep(V, active_f               , &ds_eth_rmep , CTC_FLAG_ISSET(rmep_flag, CTC_OAM_Y1731_RMEP_FLAG_MEP_EN));
    SetDsEthRmep(V, ethOamP2PMode_f        , &ds_eth_rmep , 1);
    SetDsEthRmep(V, seqNumEn_f             , &ds_eth_rmep , 0);
    SetDsEthRmep(V, macAddrUpdateDisable_f , &ds_eth_rmep , 0);
    SetDsEthRmep(V, apsSignalFailRemote_f  , &ds_eth_rmep , 0);
    SetDsEthRmep(V, mepType_f              , &ds_eth_rmep , 6);

    if (g_gg_oam_master[lchip]->timer_update_disable)
    {
        SetDsEthRmep(V, firstPktRx_f, &ds_eth_rmep , 1);
    }
    else
    {
        SetDsEthRmep(V, firstPktRx_f, &ds_eth_rmep, 0);
    }
    SetDsEthRmep(V, dLoc_f                 , &ds_eth_rmep , 0);
    SetDsEthRmep(V, isCsfEn_f              , &ds_eth_rmep , CTC_FLAG_ISSET(rmep_flag, CTC_OAM_Y1731_RMEP_FLAG_CSF_EN));
    if (CTC_FLAG_ISSET(rmep_flag, CTC_OAM_Y1731_RMEP_FLAG_CSF_EN))
    {
        SetDsEthRmep(V, firstPktRx_f           , &ds_eth_rmep , 1);
        SetDsEthRmep(V, dLoc_f                 , &ds_eth_rmep , 1);
    }
    if (CTC_FLAG_ISSET(lmep_flag, CTC_OAM_Y1731_LMEP_FLAG_TX_CSF_EN))
    {
        SetDsEthRmep(V, u1_g2_isCsfTxEn_f      , &ds_eth_rmep , 1);
        SetDsEthRmep(V, u1_g2_rxCsfType_f      , &ds_eth_rmep , tx_csf_type);
        SetDsEthRmep(V, u1_g2_dCsf_f           , &ds_eth_rmep , 0);
        SetDsEthRmep(V, u1_g2_rxCsfWhile_f     , &ds_eth_rmep , 0);
        SetDsEthRmep(V, u1_g2_isCsfUseUcDa_f   , &ds_eth_rmep , 0);
    }

    SetDsEthRmep(V, u3_g2_mepId_f          , &ds_eth_rmep , p_sys_tp_y1731_rmep->rmep_id);

    SetDsEthRmep(V, isRemote_f             , &ds_eth_rmep , 1);
    SetDsEthRmep(V, rmepWhile_f            , &ds_eth_rmep , 14);
    SetDsEthRmep(V, dUnexpPeriodTimer_f    , &ds_eth_rmep , 14);
    SetDsEthRmep(V, rmepLastPortStatus_f   , &ds_eth_rmep , 0);
    SetDsEthRmep(V, rmepLastIntfStatus_f   , &ds_eth_rmep , 0);
    SetDsEthRmep(V, cntShiftWhile_f        , &ds_eth_rmep , 4);

    SetDsEthRmep(V, isBfd_f                , &ds_eth_rmep , 0);
    SetDsEthRmep(V, isUp_f                 , &ds_eth_rmep , 0);

    SetDsEthRmep(V, sfFailWhile_f          , &ds_eth_rmep , 0);
    SetDsEthRmep(V, seqNumFailCounter_f    , &ds_eth_rmep , 0);
    SetDsEthRmep(V, rmepLastRdi_f          , &ds_eth_rmep , 0);
    SetDsEthRmep(V, dUnexpPeriod_f         , &ds_eth_rmep , 0);
    SetDsEthRmep(V, expCcmNum_f            , &ds_eth_rmep , 0);

    SetDsEthRmep(V, macStatusChange_f      , &ds_eth_rmep , 0);
    SetDsEthRmep(V, rmepmacmismatch_f      , &ds_eth_rmep , 0);
    SetDsEthRmep(V, sfState_f              , &ds_eth_rmep , 0);

    index = p_sys_rmep->rmep_index;
    cmd = DRV_IOW(DsEthRmep_t, DRV_ENTRY_FLAG);
    tbl_entry.data_entry = (uint32*)&ds_eth_rmep;
    tbl_entry.mask_entry = (uint32*)&DsEthRmep_mask;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &tbl_entry));
    RETURN:

    return CTC_E_NONE;
}


extern int32
_sys_goldengate_bfd_free_ipv6_idx(uint8 lchip, uint8 index);

int32
_sys_goldengate_oam_rmep_del_from_asic(uint8 lchip, sys_oam_rmep_com_t* p_sys_rmep)
{
    sys_oam_lmep_com_t* p_sys_lmep = NULL;
    uint32 cmd        = 0;
    uint32 index      = 0;
    DsEthRmep_m ds_eth_rmep;
    DsEthRmep_m DsEthRmep_mask;
    tbl_entry_t tbl_entry;
    uint32 lmep_flag = 0;
     /*sys_bfd_ip_addr_v6_node_t ip_addr_v6_sa;*/

    p_sys_lmep = p_sys_rmep->p_sys_lmep;
    if (SYS_OAM_NO_MEP_OPERATION(lchip, p_sys_rmep->p_sys_lmep->p_sys_chan->master_chipid))
    {
        goto RETURN;
    }

    if ((p_sys_rmep->p_sys_lmep->p_sys_chan->mep_type == CTC_OAM_MEP_TYPE_IP_BFD)
        ||(p_sys_rmep->p_sys_lmep->p_sys_chan->mep_type == CTC_OAM_MEP_TYPE_MPLS_BFD))
    {
        lmep_flag   = ((sys_oam_lmep_bfd_t*)p_sys_lmep)->flag;
        if (CTC_FLAG_ISSET(lmep_flag, CTC_OAM_BFD_LMEP_FLAG_IPV6_ENCAP))
        {
            CTC_ERROR_RETURN(_sys_goldengate_bfd_free_ipv6_idx(lchip, ((sys_oam_lmep_bfd_t*)p_sys_lmep)->ipv6_sa_index));
            CTC_ERROR_RETURN(_sys_goldengate_bfd_free_ipv6_idx(lchip, ((sys_oam_lmep_bfd_t*)p_sys_lmep)->ipv6_da_index));
        }
    }

    sal_memset(&ds_eth_rmep, 0, sizeof(ds_eth_rmep));
    sal_memset(&DsEthRmep_mask, 0, sizeof(ds_eth_rmep));

    index = p_sys_rmep->rmep_index;
    cmd = DRV_IOW(DsEthRmep_t, DRV_ENTRY_FLAG);
    tbl_entry.data_entry = (uint32*)&ds_eth_rmep;
    tbl_entry.mask_entry = (uint32*)&DsEthRmep_mask;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &tbl_entry));
RETURN:

    return CTC_E_NONE;
}

int32
_sys_goldengate_oam_rmep_update_eth_asic(uint8 lchip, sys_oam_rmep_com_t* p_sys_rmep)
{
    sys_oam_rmep_y1731_t* p_sys_rmep_eth  = (sys_oam_rmep_y1731_t*)p_sys_rmep;
    sys_oam_lmep_com_t* p_sys_lmep = NULL;
    uint32 cmd        = 0;
    uint32 index      = 0;
    uint32 cc_en      = 0;
    uint32 tx_csf_en  = 0;
    DsEthRmep_m ds_eth_rmep;
    DsEthRmep_m DsEthRmep_mask;
    tbl_entry_t tbl_entry;

    p_sys_lmep = p_sys_rmep->p_sys_lmep;

    if (SYS_OAM_NO_MEP_OPERATION(lchip, p_sys_lmep->p_sys_chan->master_chipid))
    {
        goto RETURN;
    }

    index = p_sys_rmep->rmep_index;

    sal_memset(&ds_eth_rmep, 0, sizeof(ds_eth_rmep));
    sal_memset(&DsEthRmep_mask, 0xFF, sizeof(ds_eth_rmep));
    cmd = DRV_IOR(DsEthRmep_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_eth_rmep));

    switch (p_sys_rmep_eth->update_type)
    {
    case CTC_OAM_Y1731_RMEP_UPDATE_TYPE_MEP_EN:

        if (CTC_FLAG_ISSET(p_sys_rmep_eth->flag, CTC_OAM_Y1731_RMEP_FLAG_MEP_EN))
        {
            SetDsEthRmep(V, active_f             , &ds_eth_rmep , 1);
            SetDsEthRmep(V, isRemote_f             , &ds_eth_rmep , 1);
            SetDsEthRmep(V, rmepWhile_f            , &ds_eth_rmep , 14);
            SetDsEthRmep(V, dUnexpPeriodTimer_f    , &ds_eth_rmep , 14);
            SetDsEthRmep(V, rmepLastPortStatus_f   , &ds_eth_rmep , 2);
            SetDsEthRmep(V, rmepLastIntfStatus_f   , &ds_eth_rmep , 1);
            SetDsEthRmep(V, cntShiftWhile_f        , &ds_eth_rmep , 4);
            SetDsEthRmep(V, expCcmNum_f            , &ds_eth_rmep , 0);
            SetDsEthRmep(V, dLoc_f                 , &ds_eth_rmep , 0);
            SetDsEthRmep(V, dUnexpPeriod_f         , &ds_eth_rmep , 0);
            SetDsEthRmep(V, rmepLastRdi_f          , &ds_eth_rmep , 0);
            SetDsEthRmep(V, seqNumFailCounter_f    , &ds_eth_rmep , 0);
            SetDsEthRmep(V, sfFailWhile_f          , &ds_eth_rmep , 0);
            SetDsEthRmep(V, macStatusChange_f      , &ds_eth_rmep , 0);
            SetDsEthRmep(V, rmepmacmismatch_f      , &ds_eth_rmep , 0);

        }
        else
        {
            SetDsEthRmep(V, active_f             , &ds_eth_rmep , 0);
        }
        SetDsEthRmep(V, active_f             , &DsEthRmep_mask , 0);
        break;

    case CTC_OAM_Y1731_RMEP_UPDATE_TYPE_CCM_SEQ_NUM_EN:
        SetDsEthRmep(V, seqNumEn_f             , &ds_eth_rmep , CTC_FLAG_ISSET(p_sys_rmep_eth->flag, CTC_OAM_Y1731_RMEP_FLAG_CCM_SEQ_NUM_EN));
        SetDsEthRmep(V, seqNumEn_f             , &DsEthRmep_mask , 0);
        break;

    case CTC_OAM_Y1731_RMEP_UPDATE_TYPE_SEQ_FAIL_CLEAR:
        SetDsEthRmep(V, seqNumFailCounter_f             , &ds_eth_rmep , 0);
        SetDsEthRmep(V, seqNumFailCounter_f             , &DsEthRmep_mask , 0);
        break;

    case CTC_OAM_Y1731_RMEP_UPDATE_TYPE_CSF_EN:
        cmd = DRV_IOR(DsEthMep_t, DsEthMep_cciEn_f);
        DRV_IOCTL(lchip, p_sys_lmep->lmep_index, cmd, &cc_en);
        if (cc_en)
        {
            return CTC_E_INVALID_CONFIG;
        }
        tx_csf_en = GetDsEthRmep(V, u1_g2_isCsfTxEn_f, &ds_eth_rmep);
        if ((tx_csf_en)&&(!p_sys_rmep_eth->csf_en))
        {
            p_sys_rmep_eth->csf_en = 1;
        }

        SetDsEthRmep(V, isCsfEn_f, &ds_eth_rmep , p_sys_rmep_eth->csf_en);
        SetDsEthRmep(V, isCsfEn_f, &DsEthRmep_mask , 0);
        SetDsEthRmep(V, firstPktRx_f, &ds_eth_rmep , 1);
        SetDsEthRmep(V, firstPktRx_f, &DsEthRmep_mask ,  0);
        SetDsEthRmep(V, dLoc_f, &ds_eth_rmep , 1);
        SetDsEthRmep(V, dLoc_f, &DsEthRmep_mask ,  0);
        break;

    case CTC_OAM_Y1731_RMEP_UPDATE_TYPE_DCSF:
        SetDsEthRmep(V, u1_g2_dCsf_f           , &ds_eth_rmep ,  p_sys_rmep_eth->d_csf);
        SetDsEthRmep(V, u1_g2_dCsf_f           , &DsEthRmep_mask ,  0);
        break;

    case CTC_OAM_Y1731_RMEP_UPDATE_TYPE_RMEP_SF_STATE:
        SetDsEthRmep(V, apsSignalFailRemote_f           , &ds_eth_rmep ,  p_sys_rmep_eth->sf_state);
        SetDsEthRmep(V, apsSignalFailRemote_f           , &DsEthRmep_mask ,  0);
        break;

    case CTC_OAM_Y1731_RMEP_UPDATE_TYPE_RMAC_MIS:
        SetDsEthRmep(V, rmepmacmismatch_f           , &ds_eth_rmep ,  p_sys_rmep_eth->src_mac_state);
        SetDsEthRmep(V, rmepmacmismatch_f           , &DsEthRmep_mask ,  0);
        break;

    case CTC_OAM_Y1731_RMEP_UPDATE_TYPE_PORT_INTF_MIS:
        SetDsEthRmep(V, macStatusChange_f           , &ds_eth_rmep ,  p_sys_rmep_eth->port_intf_state);
        SetDsEthRmep(V, macStatusChange_f           , &DsEthRmep_mask ,  0);
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    cmd = DRV_IOW(DsEthRmep_t, DRV_ENTRY_FLAG);
    tbl_entry.data_entry = (uint32*)&ds_eth_rmep;
    tbl_entry.mask_entry = (uint32*)&DsEthRmep_mask;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &tbl_entry));
RETURN:

    return CTC_E_NONE;

}

int32
_sys_goldengate_oam_rmep_update_tp_y1731_asic(uint8 lchip, sys_oam_rmep_com_t* p_sys_rmep)
{
    sys_oam_rmep_y1731_t* p_sys_rmep_tp_y1731  = (sys_oam_rmep_y1731_t*)p_sys_rmep;
    sys_oam_lmep_com_t* p_sys_lmep = NULL;
    uint32 cmd        = 0;
    uint32 index      = 0;
    uint32 cc_en      = 0;
    uint32 tx_csf_en  = 0;
    DsEthRmep_m ds_eth_rmep;
    DsEthRmep_m DsEthRmep_mask;
    tbl_entry_t tbl_entry;

    p_sys_lmep = p_sys_rmep->p_sys_lmep;

    if (SYS_OAM_NO_MEP_OPERATION(lchip, p_sys_lmep->p_sys_chan->master_chipid))
    {
        goto RETURN;
    }

    index = p_sys_rmep->rmep_index;

    sal_memset(&ds_eth_rmep, 0, sizeof(DsEthRmep_m));
    sal_memset(&DsEthRmep_mask, 0xFF, sizeof(DsEthRmep_m));
    cmd = DRV_IOR(DsEthRmep_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_eth_rmep));

    switch (p_sys_rmep_tp_y1731->update_type)
    {
    case CTC_OAM_Y1731_RMEP_UPDATE_TYPE_MEP_EN:

        if (CTC_FLAG_ISSET(p_sys_rmep_tp_y1731->flag, CTC_OAM_Y1731_RMEP_FLAG_MEP_EN))
        {
            SetDsEthRmep(V, active_f             , &ds_eth_rmep , 1);
            SetDsEthRmep(V, rmepWhile_f            , &ds_eth_rmep , 14);
            SetDsEthRmep(V, dUnexpPeriodTimer_f    , &ds_eth_rmep , 14);
            SetDsEthRmep(V, rmepLastPortStatus_f   , &ds_eth_rmep , 0);
            SetDsEthRmep(V, rmepLastIntfStatus_f   , &ds_eth_rmep , 0);
            SetDsEthRmep(V, cntShiftWhile_f        , &ds_eth_rmep , 4);
            SetDsEthRmep(V, expCcmNum_f            , &ds_eth_rmep , 0);
            SetDsEthRmep(V, dLoc_f                 , &ds_eth_rmep , 0);
            SetDsEthRmep(V, dUnexpPeriod_f         , &ds_eth_rmep , 0);
            SetDsEthRmep(V, rmepLastRdi_f          , &ds_eth_rmep , 0);
            SetDsEthRmep(V, seqNumFailCounter_f    , &ds_eth_rmep , 0);
            SetDsEthRmep(V, sfFailWhile_f          , &ds_eth_rmep , 0);
            SetDsEthRmep(V, macStatusChange_f      , &ds_eth_rmep , 0);
            SetDsEthRmep(V, rmepmacmismatch_f      , &ds_eth_rmep , 0);

        }
        else
        {
            SetDsEthRmep(V, active_f             , &ds_eth_rmep , 0);
        }
        SetDsEthRmep(V, active_f             , &DsEthRmep_mask , 0);
        break;

    case CTC_OAM_Y1731_RMEP_UPDATE_TYPE_CSF_EN:
        cmd = DRV_IOR(DsEthMep_t, DsEthMep_cciEn_f);
        DRV_IOCTL(lchip, p_sys_lmep->lmep_index, cmd, &cc_en);
        if (cc_en)
        {
            return CTC_E_INVALID_CONFIG;
        }
        tx_csf_en = GetDsEthRmep(V, u1_g2_isCsfTxEn_f, &ds_eth_rmep);
        if ((tx_csf_en)&&(!p_sys_rmep_tp_y1731->csf_en))
        {
            p_sys_rmep_tp_y1731->csf_en = 1;
        }

        SetDsEthRmep(V, isCsfEn_f, &ds_eth_rmep , p_sys_rmep_tp_y1731->csf_en);
        SetDsEthRmep(V, isCsfEn_f, &DsEthRmep_mask , 0);
        SetDsEthRmep(V, firstPktRx_f, &ds_eth_rmep , 1);
        SetDsEthRmep(V, firstPktRx_f, &DsEthRmep_mask ,  0);
        SetDsEthRmep(V, dLoc_f, &ds_eth_rmep , 1);
        SetDsEthRmep(V, dLoc_f, &DsEthRmep_mask ,  0);
        break;

    case CTC_OAM_Y1731_RMEP_UPDATE_TYPE_DCSF:
        SetDsEthRmep(V, u1_g2_dCsf_f           , &ds_eth_rmep ,  p_sys_rmep_tp_y1731->d_csf);
        SetDsEthRmep(V, u1_g2_dCsf_f           , &DsEthRmep_mask ,  0);
        break;

    case CTC_OAM_Y1731_RMEP_UPDATE_TYPE_RMEP_SF_STATE:
        SetDsEthRmep(V, apsSignalFailRemote_f           , &ds_eth_rmep ,  p_sys_rmep_tp_y1731->sf_state);
        SetDsEthRmep(V, apsSignalFailRemote_f           , &DsEthRmep_mask ,  0);
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    cmd = DRV_IOW(DsEthRmep_t, DRV_ENTRY_FLAG);
    tbl_entry.data_entry = (uint32*)&ds_eth_rmep;
    tbl_entry.mask_entry = (uint32*)&DsEthRmep_mask;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &tbl_entry));
RETURN:

    return CTC_E_NONE;
}

#define OAM_STATS "Function Begin"
int32
sys_goldengate_oam_get_session_type(uint8 lchip, sys_oam_lmep_com_t *p_oam_lmep, uint32 *session_type)
{
    uint32 mep_type = 0;

    sys_oam_lmep_y1731_t* p_sys_lmep_y1731  = NULL;
    sys_oam_lmep_bfd_t* p_sys_lmep_bfd = NULL;

    mep_type = p_oam_lmep->p_sys_chan->mep_type;

    if ((CTC_OAM_MEP_TYPE_ETH_1AG == mep_type)
        || (CTC_OAM_MEP_TYPE_ETH_Y1731 == mep_type))
    {
        p_sys_lmep_y1731 = (sys_oam_lmep_y1731_t*)p_oam_lmep;
        if(CTC_FLAG_ISSET(p_sys_lmep_y1731->flag, CTC_OAM_Y1731_LMEP_FLAG_P2P_MODE))
        {
            *session_type = SYS_OAM_SESSION_Y1731_ETH_P2P;
        }
        else
        {
            *session_type = SYS_OAM_SESSION_Y1731_ETH_P2MP;
        }
    }
    else if (CTC_OAM_MEP_TYPE_MPLS_TP_Y1731 == mep_type)
    {
        if(((sys_oam_chan_tp_t*)(p_oam_lmep->p_sys_chan))->key.section_oam)
        {
            *session_type = SYS_OAM_SESSION_Y1731_TP_SECTION;
        }
        else
        {
            *session_type = SYS_OAM_SESSION_Y1731_TP_MPLS;
        }

    }
    else if(CTC_OAM_MEP_TYPE_IP_BFD == mep_type)
    {
        p_sys_lmep_bfd = (sys_oam_lmep_bfd_t*)p_oam_lmep;
        if (CTC_FLAG_ISSET(p_sys_lmep_bfd->flag, CTC_OAM_BFD_LMEP_FLAG_IPV4_ENCAP))
        {
            *session_type = SYS_OAM_SESSION_BFD_IPv4;
        }
        else if (CTC_FLAG_ISSET(p_sys_lmep_bfd->flag, CTC_OAM_BFD_LMEP_FLAG_IPV6_ENCAP))
        {
            *session_type = SYS_OAM_SESSION_BFD_IPv6;
        }
    }
    else if(CTC_OAM_MEP_TYPE_MPLS_TP_BFD == mep_type)
    {
        if(((sys_oam_chan_tp_t*)(p_oam_lmep->p_sys_chan))->key.section_oam)
        {
            *session_type = SYS_OAM_SESSION_BFD_TP_SECTION;
        }
        else
        {
            *session_type = SYS_OAM_SESSION_BFD_TP_MPLS;
        }
    }
    else if (CTC_OAM_MEP_TYPE_MPLS_BFD == mep_type)
    {
        *session_type = SYS_OAM_SESSION_BFD_MPLS;
    }


    return CTC_E_NONE;
}

int32
sys_goldengate_oam_get_session_num(uint8 lchip)
{
    uint32 used_session = 0;
    uint8  type = 0;
    SYS_OAM_INIT_CHECK(lchip);
    for(type = SYS_OAM_SESSION_Y1731_ETH_P2P; type < SYS_OAM_SESSION_MAX; type ++)
    {
        used_session += g_gg_oam_master[lchip]->oam_session_num[type];
    }
    return used_session;
}

int32
sys_goldengate_oam_ftm_cb(uint8 lchip, sys_ftm_specs_info_t* specs_info)
{
    CTC_PTR_VALID_CHECK(specs_info);
    SYS_OAM_INIT_CHECK(lchip);

    specs_info->used_size = sys_goldengate_oam_get_session_num(lchip);

    return CTC_E_NONE;
}


#define COM "Function Begin"


int32
_sys_goldengate_oam_defect_read_defect_status(uint8 lchip,
                                             void* p_defect)
{
    uint32 cmd = 0;
    uint32 cache_entry_valid = 0;
    uint8 cache_entry_idx  = 0;
    uint8 valid_cache_num = 0;
    uint32 defect_status_index = 0;
    uint32 defect_status = 0;
    OamErrorDefectCtl_m oam_error_defect_ctl;
    DsOamDefectStatus_m ds_oam_defect_status;
    OamDefectCache_m oam_defect_cache;
    ctc_oam_defect_info_t* p_defect_info = NULL;

    CTC_PTR_VALID_CHECK(p_defect);

    p_defect_info = (ctc_oam_defect_info_t*)p_defect;

    sal_memset(&oam_error_defect_ctl, 0, sizeof(OamErrorDefectCtl_m));
    cmd = DRV_IOR(OamErrorDefectCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &oam_error_defect_ctl));

    DRV_GET_FIELD_A(OamErrorDefectCtl_t,OamErrorDefectCtl_cacheEntryValid_f,&oam_error_defect_ctl,&cache_entry_valid);

    if (0 == cache_entry_valid)
    {
        return CTC_E_NONE;
    }

        SYS_OAM_DBG_INFO("cache_entry_valid= 0x%x\n", cache_entry_valid);

    for (cache_entry_idx = 0; cache_entry_idx < CTC_OAM_MAX_ERROR_CACHE_NUM; cache_entry_idx++)
    {
        if (IS_BIT_SET(cache_entry_valid, cache_entry_idx))
        {
            sal_memset(&oam_defect_cache, 0, sizeof(oam_defect_cache));
            cmd = DRV_IOR(OamDefectCache_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, cache_entry_idx, cmd, &oam_defect_cache));
            DRV_GET_FIELD_A(OamDefectCache_t,OamDefectCache_scanPtr_f,&oam_defect_cache,&defect_status_index);

            sal_memset(&ds_oam_defect_status, 0, sizeof(ds_oam_defect_status));
            cmd = DRV_IOR(DsOamDefectStatus_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, defect_status_index, cmd, &ds_oam_defect_status));

            p_defect_info->mep_index_base[valid_cache_num] = defect_status_index;
            DRV_GET_FIELD_A(DsOamDefectStatus_t,DsOamDefectStatus_defectStatus_f,&ds_oam_defect_status,&defect_status);
            p_defect_info->mep_index_bitmap[valid_cache_num++] = defect_status;
            SYS_OAM_DBG_INFO("defect_status_index:%d, defect_status:0x%x\n", defect_status_index, defect_status);
            /*Clear after read the status */
#if (SDK_WORK_PLATFORM == 1)
            if (!drv_goldengate_io_is_asic())
            {
                 /*ds_oam_defect_status.defect_status = 0;*/
                DRV_SET_FIELD_V(DsOamDefectStatus_t,DsOamDefectStatus_defectStatus_f,&ds_oam_defect_status,0);
            }
#endif
            cmd = DRV_IOW(DsOamDefectStatus_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, defect_status_index, cmd, &ds_oam_defect_status));
        }
    }


#if (SDK_WORK_PLATFORM == 1)
    if (!drv_goldengate_io_is_asic())
    {
        cache_entry_valid = 0;
    }
#endif
    cmd = DRV_IOW(OamErrorDefectCtl_t, OamErrorDefectCtl_cacheEntryValid_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &cache_entry_valid));

    p_defect_info->valid_cache_num = valid_cache_num;

    return CTC_E_NONE;
}

int32
_sys_goldengate_oam_defect_scan_en(uint8 lchip, bool enable)
{
    uint32 cmd = 0;
    uint32 field_val = 0;
    uint32 field_report[2] = {0};
    uint32 i = 0;
    DsOamDefectStatus_m ds_oam_defect_status;
    OamErrorDefectCtl_m oam_error_defect_ctl;

    sal_memset(&ds_oam_defect_status, 0xFF, sizeof(DsOamDefectStatus_m));
    sal_memset(&oam_error_defect_ctl, 0, sizeof(OamErrorDefectCtl_m));
#if (SDK_WORK_PLATFORM == 1)
    DRV_SET_FIELD_V(DsOamDefectStatus_t,DsOamDefectStatus_defectStatus_f,&ds_oam_defect_status,0);
#endif
    cmd = DRV_IOW(DsOamDefectStatus_t, DRV_ENTRY_FLAG);
    for (i = 0; i < 256; i++)
    {
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, i, cmd, &ds_oam_defect_status));
    }

    field_val = 0;
    cmd = DRV_IOW(OamErrorDefectCtl_t, OamErrorDefectCtl_minPtr_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    field_val = 0xFF;
    cmd = DRV_IOW(OamErrorDefectCtl_t, OamErrorDefectCtl_maxPtr_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    field_report[0]=0xFFFFFFFF;
    field_report[1]=0xFFFFFFFF;

    cmd = DRV_IOR(OamErrorDefectCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &oam_error_defect_ctl));
    DRV_SET_FIELD_A(OamErrorDefectCtl_t,OamErrorDefectCtl_reportDefectEn_f,&oam_error_defect_ctl,field_report);

    cmd = DRV_IOW(OamErrorDefectCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &oam_error_defect_ctl));

    field_val = 4;
    cmd = DRV_IOW(OamErrorDefectCtl_t, OamErrorDefectCtl_scanDefectInterval_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));


    field_val = enable ? 1 : 0;
    cmd = DRV_IOW(OamErrorDefectCtl_t, OamErrorDefectCtl_scanEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    return CTC_E_NONE;
}

int32
_sys_goldengate_oam_get_stats_info(uint8 lchip, sys_oam_lm_com_t* p_sys_oam_lm, ctc_oam_stats_info_t* p_stat_info)
{

    DsOamLmStats_m ds_oam_lm_stats;
    uint8 cnt = 0;
    uint32 cmd = 0;

    CTC_PTR_VALID_CHECK(p_sys_oam_lm);
    CTC_PTR_VALID_CHECK(p_stat_info);

    SYS_OAM_DBG_INFO("p_sys_oam_lm->lm_cos_type:%d, lm_type:%d, lm_index:%d\n",
       p_sys_oam_lm->lm_cos_type, p_sys_oam_lm->lm_type, p_sys_oam_lm->lm_index);

    p_stat_info->lm_cos_type    = p_sys_oam_lm->lm_cos_type;
    p_stat_info->lm_type        = p_sys_oam_lm->lm_type;
    if ((CTC_OAM_LM_COS_TYPE_ALL_COS == p_sys_oam_lm->lm_cos_type)
        || (CTC_OAM_LM_COS_TYPE_SPECIFIED_COS == p_sys_oam_lm->lm_cos_type))
    {
        sal_memset(&ds_oam_lm_stats, 0, sizeof(DsOamLmStats_m));
        cmd = DRV_IOR(DsOamLmStats_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(p_sys_oam_lm->lchip, p_sys_oam_lm->lm_index, cmd, &ds_oam_lm_stats));

        p_stat_info->lm_info[0].rx_fcb = GetDsOamLmStats(V, rxFcb_r_f, &ds_oam_lm_stats);
        p_stat_info->lm_info[0].rx_fcl = GetDsOamLmStats(V, rxFcl_r_f, &ds_oam_lm_stats);
        p_stat_info->lm_info[0].tx_fcb = GetDsOamLmStats(V, txFcb_r_f, &ds_oam_lm_stats);
        p_stat_info->lm_info[0].tx_fcf = GetDsOamLmStats(V, txFcf_r_f, &ds_oam_lm_stats);
    }
    else
    {
        for (cnt = 0; cnt < CTC_OAM_STATS_COS_NUM; cnt++)
        {
            sal_memset(&ds_oam_lm_stats, 0, sizeof(DsOamLmStats_m));
            cmd = DRV_IOR(DsOamLmStats_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(p_sys_oam_lm->lchip, (p_sys_oam_lm->lm_index + cnt), cmd, &ds_oam_lm_stats));

            p_stat_info->lm_info[cnt].rx_fcb = GetDsOamLmStats(V, rxFcb_r_f, &ds_oam_lm_stats);
            p_stat_info->lm_info[cnt].rx_fcl = GetDsOamLmStats(V, rxFcl_r_f, &ds_oam_lm_stats);
            p_stat_info->lm_info[cnt].tx_fcb = GetDsOamLmStats(V, txFcb_r_f, &ds_oam_lm_stats);
            p_stat_info->lm_info[cnt].tx_fcf = GetDsOamLmStats(V, txFcf_r_f, &ds_oam_lm_stats);
        }
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_oam_get_mep_info(uint8 lchip, ctc_oam_mep_info_t* mep_info)
{
    uint32 lmep_index   = 0;
    uint32 cmd_eth_mep  = 0;
    uint32 cmd_eth_rmep = 0;
    uint32 cmd = 0;
    ctc_oam_mep_type_t mep_type = 0;
    DsEthMep_m   ds_eth_mep;
    DsEthRmep_m  ds_eth_rmep;
    DsBfdMep_m   ds_bfd_mep;
    DsBfdRmep_m  ds_bfd_rmep;
    DsBfdSlowIntervalCfg_m ds_bfd_slow_interval_cfg;
    sys_oam_lmep_com_t* p_sys_lmep_com  = NULL;
    sys_oam_rmep_com_t* p_sys_rmep_com  = NULL;
    sys_oam_lmep_y1731_t* p_sys_lmep_y1731  = NULL;
    sys_oam_rmep_y1731_t* p_sys_rmep_y1731  = NULL;
    ctc_oam_y1731_lmep_info_t*  p_lmep_y1731_info = NULL;
    ctc_oam_y1731_rmep_info_t*  p_rmep_y1731_info = NULL;
    ctc_oam_bfd_lmep_info_t*    p_lmep_bfd_info = NULL;
    ctc_oam_bfd_rmep_info_t*    p_rmep_bfd_info = NULL;
    hw_mac_addr_t             mac_sa   = { 0 };
    uint32 active = 0;
    uint16  ma_index = 0;
    uint32 level = 0;
    uint8 is_remote = 0;
    uint32 tmp[8] = {0, 33, 100, 1000, 10000, 100000, 600000, 6000000};

    CTC_PTR_VALID_CHECK(mep_info);

    sal_memset(&ds_eth_mep, 0, sizeof(DsEthMep_m));
    sal_memset(&ds_eth_rmep, 0, sizeof(DsEthRmep_m));
    sal_memset(&ds_bfd_mep, 0, sizeof(DsBfdMep_m));
    sal_memset(&ds_bfd_rmep, 0, sizeof(DsBfdRmep_m));
    sal_memset(&ds_bfd_slow_interval_cfg, 0, sizeof(DsBfdSlowIntervalCfg_m));

    cmd_eth_mep     = DRV_IOR(DsEthMep_t,   DRV_ENTRY_FLAG);
    cmd_eth_rmep    = DRV_IOR(DsEthRmep_t,  DRV_ENTRY_FLAG);
    if (0x1FFF == mep_info->mep_index)
    {
        return CTC_E_NONE;
    }
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, mep_info->mep_index, cmd_eth_rmep, &ds_eth_rmep));

    mep_info->is_rmep = GetDsEthRmep(V, isRemote_f, &ds_eth_rmep);

    active = GetDsEthRmep(V, active_f, &ds_eth_rmep);
    if (!active)
    {
        return CTC_E_OAM_INVALID_MEP_INDEX;
    }

    if (mep_info->is_rmep == 1)
    {
        p_sys_rmep_com = ctc_vector_get(g_gg_oam_master[lchip]->mep_vec, mep_info->mep_index);

        if (p_sys_rmep_com)
        {
            mep_type = p_sys_rmep_com->p_sys_lmep->p_sys_chan->mep_type;
            SYS_OAM_DBG_INFO("mep_type:%d\n", mep_type);
        }
        else
        {
            return CTC_E_OAM_INVALID_MEP_INDEX;
        }

        is_remote = 1;

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
            else
            {
                return CTC_E_NONE;
            }

            GetDsEthRmep(A, rmepMacSa_f, &ds_eth_rmep, mac_sa);
            SYS_GOLDENGATE_SET_USER_MAC(p_rmep_y1731_info->mac_sa, mac_sa);
            p_rmep_y1731_info->active                  = GetDsEthRmep(V, active_f, &ds_eth_rmep);
            p_rmep_y1731_info->is_p2p_mode      = GetDsEthRmep(V, ethOamP2PMode_f, &ds_eth_rmep);
            p_rmep_y1731_info->first_pkt_rx        = GetDsEthRmep(V, firstPktRx_f, &ds_eth_rmep);
            p_rmep_y1731_info->d_loc                   = GetDsEthRmep(V, dLoc_f, &ds_eth_rmep);
            p_rmep_y1731_info->d_unexp_period   = GetDsEthRmep(V, dUnexpPeriod_f , &ds_eth_rmep);
            p_rmep_y1731_info->ma_sa_mismatch   = GetDsEthRmep(V, rmepmacmismatch_f , &ds_eth_rmep);
            p_rmep_y1731_info->mac_status_change = GetDsEthRmep(V, macStatusChange_f , &ds_eth_rmep);
            p_rmep_y1731_info->last_rdi         = GetDsEthRmep(V, rmepLastRdi_f , &ds_eth_rmep);
            p_rmep_y1731_info->lmep_index = p_rmep_y1731_info->is_p2p_mode ? (mep_info->mep_index - 1) :
                                                                       GetDsEthRmep(V, u3_g1_mepIndex_f  , &ds_eth_rmep);
            p_sys_lmep_y1731 = (sys_oam_lmep_y1731_t*)(p_sys_rmep_com->p_sys_lmep);
            if(p_sys_lmep_y1731->ccm_interval < 8)
            {
                p_rmep_y1731_info->dloc_time = (p_rmep_y1731_info->d_loc) ? 0 :
                (GetDsEthRmep(V, rmepWhile_f , &ds_eth_rmep) * tmp[p_sys_lmep_y1731->ccm_interval] / 40);
            }
            SYS_OAM_DBG_INFO("p_rmep_y1731_info->first_pkt_rx  :%d\n", p_rmep_y1731_info->first_pkt_rx );
            if (p_rmep_y1731_info->is_p2p_mode && GetDsEthRmep(V, isCsfEn_f, &ds_eth_rmep))
            {
                p_rmep_y1731_info->csf_en       = 1;
                p_rmep_y1731_info->d_csf        = GetDsEthRmep(V, u1_g2_dCsf_f,  &ds_eth_rmep);
                p_rmep_y1731_info->rx_csf_type  = GetDsEthRmep(V, u1_g2_rxCsfType_f, &ds_eth_rmep);
                SYS_OAM_DBG_INFO(" p_rmep_y1731_info->d_csf  :%d\n",  p_rmep_y1731_info->d_csf);
            }

            lmep_index = p_rmep_y1731_info->lmep_index;
            SYS_OAM_DBG_INFO("lmep_index :%d, p_rmep_y1731_info->is_p2p_mode:%d\n",
                       lmep_index, p_rmep_y1731_info->is_p2p_mode );

        }
        else if ((CTC_OAM_MEP_TYPE_IP_BFD == mep_type)
                    || (CTC_OAM_MEP_TYPE_MPLS_BFD == mep_type)
                    || (CTC_OAM_MEP_TYPE_MPLS_TP_BFD == mep_type))
        {
            sal_memcpy(&ds_bfd_rmep, &ds_eth_rmep, sizeof(DsEthMep_m));

            p_rmep_bfd_info = &(mep_info->rmep.bfd_rmep);
            p_rmep_bfd_info->actual_rx_interval = GetDsBfdRmep(V, actualRxInterval_f, &ds_bfd_rmep);
            p_rmep_bfd_info->first_pkt_rx = GetDsBfdRmep(V, firstPktRx_f, &ds_bfd_rmep);
            p_rmep_bfd_info->mep_en = GetDsBfdRmep(V, active_f, &ds_bfd_rmep);
            p_rmep_bfd_info->required_min_rx_interval = GetDsBfdRmep(V, requiredMinRxInterval_f, &ds_bfd_rmep);

            SYS_OAM_DBG_INFO("p_rmep_bfd_info->first_pkt_rx  :%d, rmepIndex:%d\n",
                       p_rmep_bfd_info->first_pkt_rx , mep_info->mep_index);

            if (CTC_OAM_MEP_TYPE_MPLS_TP_BFD == mep_type)
            {
                uint32 ip_sa = GetDsBfdRmep(V, u1_g1_ipSa_f, &ds_bfd_rmep);
                p_rmep_bfd_info->mis_connect        = CTC_IS_BIT_SET(ip_sa, 0);
            }

            p_rmep_bfd_info->remote_detect_mult = GetDsBfdRmep(V, defectMult_f, &ds_bfd_rmep);
            p_rmep_bfd_info->remote_diag = GetDsBfdRmep(V, remoteDiag_f, &ds_bfd_rmep);
            p_rmep_bfd_info->remote_discr = GetDsBfdRmep(V, remoteDisc_f, &ds_bfd_rmep);
            p_rmep_bfd_info->remote_state = GetDsBfdRmep(V, remoteStat_f, &ds_bfd_rmep);

            p_rmep_bfd_info->lmep_index  = (mep_info->mep_index - 1);
            p_rmep_bfd_info->dloc_time = GetDsBfdRmep(V, defectWhile_f, &ds_bfd_rmep);
            /*If actual_rx_interval is 4 and BFD timer is 1ms mode, will have deviation*/
            if ((4 == p_rmep_bfd_info->actual_rx_interval)
                && (p_rmep_bfd_info->dloc_time > (GetDsBfdRmep(V, defectWhile_f, &ds_bfd_rmep) * 33 / 40)))
            {
                p_rmep_bfd_info->dloc_time = GetDsBfdRmep(V, defectWhile_f, &ds_bfd_rmep) * 33 / 40;
            }
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
        sal_memcpy(&ds_eth_mep, &ds_eth_rmep, sizeof(DsEthRmep_m));
    }

    active = GetDsEthMep(V, active_f, &ds_eth_mep);
    if (!active)
    {
        return CTC_E_OAM_INVALID_MEP_INDEX;
    }

    p_sys_lmep_com = ctc_vector_get(g_gg_oam_master[lchip]->mep_vec, lmep_index);
    if (p_sys_lmep_com)
    {
        mep_type = p_sys_lmep_com->p_sys_chan->mep_type;
        mep_info->mep_type = mep_type;
        SYS_OAM_DBG_INFO("mep_info->mep_type:%d \n",     mep_info->mep_type );
    }
    else
    {
        return CTC_E_OAM_INVALID_MEP_INDEX;
    }

    if ((CTC_OAM_MEP_TYPE_ETH_1AG == mep_type)
        || (CTC_OAM_MEP_TYPE_ETH_Y1731 == mep_type)
        || (CTC_OAM_MEP_TYPE_MPLS_TP_Y1731 == mep_type))
    {
         /* labter complete for gg eth-oam*/

        p_sys_lmep_y1731 = (sys_oam_lmep_y1731_t*)p_sys_lmep_com;
        p_lmep_y1731_info = &(mep_info->lmep.y1731_lmep);

        if (p_sys_lmep_y1731)
        {
            p_lmep_y1731_info->mep_id         = p_sys_lmep_y1731->mep_id;
            p_lmep_y1731_info->ccm_interval   = p_sys_lmep_y1731->ccm_interval;
        }
        p_lmep_y1731_info->active         = GetDsEthMep(V, active_f, &ds_eth_mep);
        p_lmep_y1731_info->d_unexp_mep    = GetDsEthMep(V, dUnexpMep_f, &ds_eth_mep);
        p_lmep_y1731_info->d_mismerge     = GetDsEthMep(V, dMismerge_f, &ds_eth_mep);
        p_lmep_y1731_info->d_meg_lvl      = GetDsEthMep(V, dMegLvl_f, &ds_eth_mep);
        p_lmep_y1731_info->is_up_mep      = GetDsEthMep(V, isUp_f, &ds_eth_mep);
        p_lmep_y1731_info->vlan_id        = GetDsEthMep(V, mepPrimaryVid_f, &ds_eth_mep);
        p_lmep_y1731_info->present_rdi    = GetDsEthMep(V, presentRdi_f, &ds_eth_mep);
        p_lmep_y1731_info->ccm_enable     = GetDsEthMep(V, cciEn_f, &ds_eth_mep);
        p_lmep_y1731_info->dm_enable     = GetDsEthMep(V, enablePm_f, &ds_eth_mep);
        ma_index = GetDsEthMep(V, maIndex_f, &ds_eth_mep);
        cmd = DRV_IOR(DsMa_t, DsMa_mdLvl_f);
        DRV_IOCTL(lchip, ma_index, cmd, &level);
        p_lmep_y1731_info->level = (level&0xFF);
        SYS_OAM_DBG_INFO("p_lmep_y1731_info->d_meg_lvl  :%d\n", p_lmep_y1731_info->d_meg_lvl );

    }
    else if ((CTC_OAM_MEP_TYPE_IP_BFD == mep_type)
                    || (CTC_OAM_MEP_TYPE_MPLS_BFD == mep_type)
                    || (CTC_OAM_MEP_TYPE_MPLS_TP_BFD == mep_type))
    {
        p_lmep_bfd_info = &(mep_info->lmep.bfd_lmep);

        sal_memcpy(&ds_bfd_mep, &ds_eth_mep, sizeof(DsEthRmep_m));

        p_lmep_bfd_info->mep_en =  GetDsBfdMep(V, active_f, &ds_bfd_mep);
        p_lmep_bfd_info->cc_enable = GetDsBfdMep(V, cciEn_f, &ds_bfd_mep);
        p_lmep_bfd_info->loacl_state = GetDsBfdMep(V, localStat_f, &ds_bfd_mep);
        p_lmep_bfd_info->local_diag = GetDsBfdMep(V, localDiag_f, &ds_bfd_mep);
        p_lmep_bfd_info->local_discr = GetDsBfdMep(V, localDisc_f, &ds_bfd_mep);
        p_lmep_bfd_info->actual_tx_interval = GetDsBfdMep(V, ccTxMode_f, &ds_bfd_mep)?
                           3300 : GetDsBfdMep(V, actualMinTxInterval_f, &ds_bfd_mep);
        p_lmep_bfd_info->desired_min_tx_interval = GetDsBfdMep(V, desiredMinTxInterval_f, &ds_bfd_mep);

        cmd = DRV_IOR(DsBfdRmep_t,  DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, lmep_index + 1, cmd, &ds_bfd_rmep));
        p_lmep_bfd_info->cv_enable = GetDsBfdRmep(V, isCvEn_f, &ds_bfd_rmep);
        p_lmep_bfd_info->local_detect_mult = GetDsBfdRmep(V, localDefectMult_f, &ds_bfd_rmep);
        p_lmep_bfd_info->single_hop = !(GetDsBfdRmep(V, isMultiHop_f, &ds_bfd_rmep));
        if (p_lmep_bfd_info->loacl_state != CTC_OAM_BFD_STATE_UP)
        {
            cmd = DRV_IOR(DsBfdSlowIntervalCfg_t,  DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ds_bfd_slow_interval_cfg));
            p_lmep_bfd_info->actual_tx_interval = GetDsBfdSlowIntervalCfg(V, txSlowInterval_f, &ds_bfd_slow_interval_cfg);
            if (p_rmep_bfd_info)
            {
                p_rmep_bfd_info->actual_rx_interval = GetDsBfdSlowIntervalCfg(V, rxSlowInterval_f, &ds_bfd_slow_interval_cfg);
            }
        }


        if (CTC_OAM_MEP_TYPE_MPLS_TP_BFD == mep_type)
        {
            if ( GetDsBfdMep(V, isCsfEn_f, &ds_bfd_mep))
            {
                p_lmep_bfd_info->csf_en       = 1;
                p_lmep_bfd_info->d_csf        = GetDsBfdMep(V, u2_g2_dCsf_f,  &ds_bfd_mep);
                p_lmep_bfd_info->rx_csf_type  = GetDsBfdMep(V, u2_g2_rxCsfType_f, &ds_bfd_mep);
                SYS_OAM_DBG_INFO(" p_lmep_bfd_info->mep_en :%d,  p_lmep_bfd_info->d_csf  :%d\n",
                           p_lmep_bfd_info->mep_en,
                           p_lmep_bfd_info->d_csf);
            }
            if(p_lmep_bfd_info->cv_enable)
            {
               sal_memcpy(&p_lmep_bfd_info->mep_id, &p_sys_lmep_com->p_sys_maid->maid, p_sys_lmep_com->p_sys_maid->maid_len);
            }
        }
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_oam_build_defect_event(uint8 lchip, ctc_oam_mep_info_t* p_mep_info, ctc_oam_event_t* p_event)
{
    uint16  entry_idx   = 0;
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

    SYS_OAM_DBG_INFO("p_mep_info->is_rmep    :%d, type:%d, active:%d\n",
        p_mep_info->is_rmep, p_mep_info->mep_type, p_mep_info->rmep.y1731_rmep.active);

    /*common*/
    if (p_mep_info->is_rmep)
    {
        p_sys_rmep_com = ctc_vector_get(g_gg_oam_master[lchip]->mep_vec, mep_index );
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

    }
    else
    {
        p_sys_lmep_com = ctc_vector_get(g_gg_oam_master[lchip]->mep_vec, mep_index);
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
            p_event->oam_event_entry[entry_idx].oam_key.u.tp.label  = ((sys_oam_chan_tp_t*)p_sys_lmep_com->p_sys_chan)->key.label;
            p_event->oam_event_entry[entry_idx].oam_key.u.tp.mpls_spaceid    = ((sys_oam_chan_tp_t*)p_sys_lmep_com->p_sys_chan)->key.spaceid;
            p_event->oam_event_entry[entry_idx].oam_key.u.tp.gport_or_l3if_id = ((sys_oam_chan_tp_t*)p_sys_lmep_com->p_sys_chan)->key.gport_l3if_id;
        }
    }


    if ((CTC_OAM_MEP_TYPE_ETH_1AG == p_mep_info->mep_type)
        || (CTC_OAM_MEP_TYPE_ETH_Y1731 == p_mep_info->mep_type)
        || (CTC_OAM_MEP_TYPE_MPLS_TP_Y1731 == p_mep_info->mep_type))
    {
        p_event->oam_event_entry[entry_idx].lmep_id = p_mep_info->lmep.y1731_lmep.mep_id;
            SYS_OAM_DBG_INFO("p_mep_info->is_rme    :%d\n", p_mep_info->is_rmep);
        if (p_mep_info->is_rmep)
        {
            p_event->oam_event_entry[entry_idx].rmep_id = p_mep_info->rmep.y1731_rmep.rmep_id;

            d_loc             = p_mep_info->rmep.y1731_rmep.d_loc;
            first_pkt_rx      = p_mep_info->rmep.y1731_rmep.first_pkt_rx;
            SYS_OAM_DBG_INFO("first_pkt_rx    :%d\n", first_pkt_rx);
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
        if (p_mep_info->is_rmep)
        {
            first_pkt_rx    = p_mep_info->rmep.bfd_rmep.first_pkt_rx;
            mis_connect     = p_mep_info->rmep.bfd_rmep.mis_connect;
            d_csf           = p_mep_info->lmep.bfd_lmep.d_csf;

            remote_state    = p_mep_info->rmep.bfd_rmep.remote_state;
            p_event->oam_event_entry[entry_idx].remote_diag     = p_mep_info->rmep.bfd_rmep.remote_diag;
            p_event->oam_event_entry[entry_idx].remote_state    = remote_state;

        }
        state           = p_mep_info->lmep.bfd_lmep.loacl_state;
        p_event->oam_event_entry[entry_idx].local_diag  = p_mep_info->lmep.bfd_lmep.local_diag;
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
        SYS_OAM_DBG_INFO("bitmap    :0x%x\n", *bitmap);
    }

    p_event->valid_entry_num++;

    return CTC_E_NONE;
}

int32
_sys_goldengate_oam_set_exception_en(uint8 lchip, uint32 exception_en)
{
    uint32 cmd = 0;

    uint32 cpu_excp[2]  = {0};
    OamHeaderEditCtl_m   oam_header_edit_ctl;

    sal_memset(&oam_header_edit_ctl, 0, sizeof(OamHeaderEditCtl_m));

    if (CTC_IS_BIT_SET(exception_en, CTC_OAM_EXCP_INVALID_OAM_PDU))
    {
        CTC_BIT_SET(cpu_excp[0], SYS_OAM_EXCP_ETH_INVALID);
        CTC_BIT_SET(cpu_excp[0], SYS_OAM_EXCP_BFD_INVALID);
    }
    if (CTC_IS_BIT_SET(exception_en, CTC_OAM_EXCP_HIGH_VER_OAM_TO_CPU))
    {
        CTC_BIT_SET(cpu_excp[0], SYS_OAM_EXCP_ETH_HIGH_VER);
    }
    if (CTC_IS_BIT_SET(exception_en, CTC_OAM_EXCP_OPTION_CCM_TLV))
    {
        CTC_BIT_SET(cpu_excp[0], SYS_OAM_EXCP_ETH_TLV);
    }
    if (CTC_IS_BIT_SET(exception_en, CTC_OAM_EXCP_APS_PDU_TO_CPU))
    {
        CTC_BIT_SET(cpu_excp[0], SYS_OAM_EXCP_ETH_APS);
    }
    if (CTC_IS_BIT_SET(exception_en, CTC_OAM_EXCP_DM_TO_CPU))
    {
        CTC_BIT_SET(cpu_excp[0], SYS_OAM_EXCP_ETH_DM);
    }
    if (CTC_IS_BIT_SET(exception_en, CTC_OAM_EXCP_PBT_MM_DEFECT_OAM_PDU))
    {
        CTC_BIT_SET(cpu_excp[0], SYS_OAM_EXCP_PBX_OAM);
    }
    if (CTC_IS_BIT_SET(exception_en, CTC_OAM_EXCP_LM_TO_CPU))
    {
        CTC_BIT_SET(cpu_excp[0], SYS_OAM_EXCP_ETH_LM);
    }
    if (CTC_IS_BIT_SET(exception_en, CTC_OAM_EXCP_ETH_TST_TO_CPU))
    {
        CTC_BIT_SET(cpu_excp[0], SYS_OAM_EXCP_ETH_TST);
    }
    if (CTC_IS_BIT_SET(exception_en, CTC_OAM_EXCP_BIG_INTERVAL_OR_SW_MEP_TO_CPU))
    {
        CTC_BIT_SET(cpu_excp[0], SYS_OAM_EXCP_ETH_BIG_CCM);
        CTC_BIT_SET(cpu_excp[0], SYS_OAM_EXCP_BIG_BFD);
        CTC_BIT_SET(cpu_excp[0], SYS_OAM_EXCP_ETH_LEARN_CCM);
    }
    if (CTC_IS_BIT_SET(exception_en, CTC_OAM_EXCP_MPLS_TP_DLM_TO_CPU))
    {
        CTC_BIT_SET(cpu_excp[0], SYS_OAM_EXCP_TP_DLM);
    }
    if (CTC_IS_BIT_SET(exception_en, CTC_OAM_EXCP_MPLS_TP_DM_DLMDM_TO_CPU))
    {
        CTC_BIT_SET(cpu_excp[0], SYS_OAM_EXCP_TP_DM);
    }
    if (CTC_IS_BIT_SET(exception_en, CTC_OAM_EXCP_CSF_TO_CPU))
    {
        CTC_BIT_SET(cpu_excp[0], SYS_OAM_EXCP_ETH_CSF);
    }
    if (CTC_IS_BIT_SET(exception_en, CTC_OAM_EXCP_MCC_PDU_TO_CPU))
    {
        CTC_BIT_SET(cpu_excp[0], SYS_OAM_EXCP_ETH_MCC);
        CTC_BIT_SET(cpu_excp[0], SYS_OAM_EXCP_TP_MCC);
    }
    if (CTC_IS_BIT_SET(exception_en, CTC_OAM_EXCP_EQUAL_LTM_LTR_TO_CPU))
    {
        CTC_BIT_SET(cpu_excp[0], SYS_OAM_EXCP_ETH_LT);
    }
    if (CTC_IS_BIT_SET(exception_en, CTC_OAM_EXCP_LEARNING_BFD_TO_CPU))
    {
        CTC_BIT_SET(cpu_excp[0], SYS_OAM_EXCP_BFD_LEARN);
    }
    if (CTC_IS_BIT_SET(exception_en, CTC_OAM_EXCP_BFD_TIMER_NEGOTIATION))
    {
        CTC_BIT_SET(cpu_excp[0], SYS_OAM_EXCP_BFD_TIMER_NEG);
    }
    if (CTC_IS_BIT_SET(exception_en, CTC_OAM_EXCP_SCC_PDU_TO_CPU))
    {
        CTC_BIT_SET(cpu_excp[0], SYS_OAM_EXCP_ETH_SCC);
        CTC_BIT_SET(cpu_excp[0], SYS_OAM_EXCP_TP_SCC);
    }
    if (CTC_IS_BIT_SET(exception_en, CTC_OAM_EXCP_ALL_DEFECT))
    {
        CTC_BIT_SET(cpu_excp[0], SYS_OAM_EXCP_ETH_DEFECT);
    }
    if (CTC_IS_BIT_SET(exception_en, CTC_OAM_EXCP_LBM))
    {
        CTC_BIT_SET(cpu_excp[0], SYS_OAM_EXCP_ETH_LB);
    }
    if (CTC_IS_BIT_SET(exception_en, CTC_OAM_EXCP_TP_LBM))
    {
        CTC_BIT_SET(cpu_excp[0], SYS_OAM_EXCP_TP_LB);
    }
    if (CTC_IS_BIT_SET(exception_en, CTC_OAM_EXCP_TP_CSF))
    {
        CTC_BIT_SET(cpu_excp[0], SYS_OAM_EXCP_TP_CSF);
    }
    if (CTC_IS_BIT_SET(exception_en, CTC_OAM_EXCP_TP_FM))
    {
        CTC_BIT_SET(cpu_excp[0], SYS_OAM_EXCP_TP_FM);
    }
    if (CTC_IS_BIT_SET(exception_en, CTC_OAM_EXCP_SM))
    {
        CTC_BIT_SET(cpu_excp[0], SYS_OAM_EXCP_ETH_SM);
    }
    if (CTC_IS_BIT_SET(exception_en, CTC_OAM_EXCP_UNKNOWN_PDU))
    {
        CTC_BIT_SET(cpu_excp[0], SYS_OAM_EXCP_ETH_OTHER);
        CTC_BIT_SET(cpu_excp[0], SYS_OAM_EXCP_BFD_OTHER);
    }
    if (CTC_IS_BIT_SET(exception_en, CTC_OAM_EXCP_BFD_DISC_MISMATCH))
    {
        CTC_BIT_SET(cpu_excp[0], SYS_OAM_EXCP_BFD_DISC_MISMATCH);
    }
    if (CTC_IS_BIT_SET(exception_en, CTC_OAM_EXCP_TP_BFD_CV))
    {
        CTC_BIT_SET(cpu_excp[1], SYS_OAM_EXCP_TP_CV - 32);
    }

    cmd = DRV_IOR(OamHeaderEditCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &oam_header_edit_ctl));

    DRV_SET_FIELD_A(OamHeaderEditCtl_t, OamHeaderEditCtl_cpuExceptionEn_f, &oam_header_edit_ctl, cpu_excp);
    cmd = DRV_IOW(OamHeaderEditCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &oam_header_edit_ctl));

    return CTC_E_NONE;
}

int32
_sys_goldengate_oam_set_common_property(uint8 lchip, void* p_oam_property)
{

    uint32 cfg_type = 0;
    uint32 value = 0;
    uint32 cmd = 0;
    uint8  rdi_index        = 0;
    uint8  defect_type      = 0;
    uint8  defect_sub_type  = 0;
    uint8  idx              = 0;
    uint32 tmp[2] = {0};
    uint32 dloc_rdi         = 0;

    ctc_oam_com_property_t* p_common_property = NULL;
    OamRxProcEtherCtl_m     oam_rx_proc_ether_ctl;
     /*OamUpdateApsCtl_m        oam_update_aps_ctl;*/
    OamErrorDefectCtl_m      oam_error_defect_ctl;

    p_common_property = &(((ctc_oam_property_t*)p_oam_property)->u.common);
    cfg_type    = p_common_property->cfg_type;
    value   = p_common_property->value;

    if (CTC_OAM_COM_PRO_TYPE_TRIG_APS_MSG == cfg_type)
    {
#if 0
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
#endif
    }
    else if (CTC_OAM_COM_PRO_TYPE_DEFECT_TO_RDI == cfg_type)
    {

        sal_memset(&oam_rx_proc_ether_ctl, 0, sizeof(OamRxProcEtherCtl_m));

        cmd = DRV_IOR(OamRxProcEtherCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &oam_rx_proc_ether_ctl));
        for (idx = 0; idx < CTC_GOLDENGATE_OAM_DEFECT_NUM; idx++)
        {
            if (CTC_IS_BIT_SET(value, idx))
            {
                _sys_goldengate_oam_get_rdi_defect_type(lchip, (1 << idx), &defect_type, &defect_sub_type);

                rdi_index = (defect_type << 3) | defect_sub_type;

                if (rdi_index < 32)
                {
                    CTC_BIT_SET(tmp[0], rdi_index);
                }
                else
                {
                    CTC_BIT_SET(tmp[1], (rdi_index - 32));
                }
            }
        }
        SetOamRxProcEtherCtl(V, etherDefectToRdi0_f, &oam_rx_proc_ether_ctl, tmp[0]);
        SetOamRxProcEtherCtl(V, etherDefectToRdi1_f, &oam_rx_proc_ether_ctl, tmp[1]);
        cmd = DRV_IOW(OamRxProcEtherCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &oam_rx_proc_ether_ctl));

        /* dloc trigger rdi*/
        if (CTC_FLAG_ISSET(value, CTC_OAM_DEFECT_DLOC))
        {
            cmd = DRV_IOW(OamUpdateCtl_t, OamUpdateCtl_genRdiByDloc_f);
            dloc_rdi = 1;
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &dloc_rdi));
        }
        else
        {
            cmd = DRV_IOW(OamUpdateCtl_t, OamUpdateCtl_genRdiByDloc_f);
            dloc_rdi = 0;
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &dloc_rdi));
        }
    }
    else if (CTC_OAM_COM_PRO_TYPE_EXCEPTION_TO_CPU == cfg_type)
    {
        CTC_ERROR_RETURN(_sys_goldengate_oam_set_exception_en(lchip, value));
    }
    else if (CTC_OAM_COM_PRO_TYPE_DEFECT_TO_CPU == cfg_type)
    {
        sal_memset(&oam_error_defect_ctl, 0, sizeof(OamErrorDefectCtl_m));
        cmd = DRV_IOR(OamErrorDefectCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &oam_error_defect_ctl));
        for (idx = 0; idx < CTC_GOLDENGATE_OAM_DEFECT_NUM; idx++)
        {
            if (CTC_IS_BIT_SET(value, idx))
            {
                sys_oam_defect_type_t defect_info;
                uint8 cnt = 0;

                sal_memset(&defect_info, 0, sizeof(sys_oam_defect_type_t));
                _sys_goldengate_oam_get_defect_type(lchip, (1 << idx), &defect_info);

                for (cnt = 0; cnt < defect_info.entry_num; cnt++)
                {
                    rdi_index = (defect_info.defect_type[cnt] << 3) | defect_info.defect_sub_type[cnt];

                    if (rdi_index < 32)
                    {
                        CTC_BIT_SET(tmp[0], rdi_index);
                    }
                    else
                    {
                        CTC_BIT_SET(tmp[1], (rdi_index - 32));
                    }
                }
            }
        }
        SetOamErrorDefectCtl(A, reportDefectEn_f, &oam_error_defect_ctl, tmp);
        cmd = DRV_IOW(OamErrorDefectCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &oam_error_defect_ctl));

    }
    else if (CTC_OAM_COM_PRO_TYPE_TIMER_DISABLE == cfg_type)
    {
        value = (value >= 1)? 0 : 1;
        cmd = DRV_IOW(OamUpdateCtl_t, OamUpdateCtl_updEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
        cmd = DRV_IOW(OamUpdateCtl_t, OamUpdateCtl_bfdUpdEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
        g_gg_oam_master[lchip]->timer_update_disable = !value;
    }
    else if (CTC_OAM_COM_PRO_TYPE_MASTER_GCHIP == cfg_type)
    {
        SYS_GLOBAL_CHIPID_CHECK(value);
        CTC_ERROR_RETURN(ctc_hash_traverse(g_gg_oam_master[lchip]->chan_hash, (hash_traversal_fn)_sys_goldengate_oam_lmep_update_master_chip, &value));
    }
    else if (CTC_OAM_COM_PRO_TYPE_SECTION_OAM_PRI == cfg_type)
    {
        uint32 priority = 0;
        uint32 color = 0;

        priority = value&0xFF;
        color = (value>>8)&0xFF;
        CTC_MAX_VALUE_CHECK(priority, SYS_QOS_CLASS_PRIORITY_MAX);
        CTC_MAX_VALUE_CHECK(color, MAX_CTC_QOS_COLOR-1);
        cmd = DRV_IOW(IpeFwdCtl_t, IpeFwdCtl_linkOamPriority_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &priority));
        cmd = DRV_IOW(IpeFwdCtl_t, IpeFwdCtl_linkOamColor_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &color));
    }

    return CTC_E_NONE;
}


int32
_sys_goldengate_oam_com_reg_init(uint8 lchip)
{
    uint32 cmd = 0;
    uint32 cpu_excp[2]  = {0};
    uint16 oam_up_phy_num = 0;
    uint16 upd_interval = 0;
    uint16 ccm_max_ptr = 0;
    uint32 ccm_min_ptr = 0;
    uint16 oam_bfd_phy_num      = 0;
    uint16 bfd_upd_interval     = 0;
    uint16 bfd_min_ptr          = 0;
    uint16 bfd_max_ptr          = 0;
    uint32 quater_cci2_interval = 0;
    uint32 tmp_field = 0;
    uint32 freq         = 0;
    DsOamExcp_m ds_oam_excp;

    OamUpdateCtl_m        oam_update_ctl;
    OamEtherCciCtl_m     oam_ether_cci_ctl;
    OamUpdateApsCtl_m    oam_update_aps_ctl;
    OamHeaderEditCtl_m   oam_header_edit_ctl;
    IpeLookupPortBaseCtl_m ipe_lkup_port_base_ctl;
    EpeAclQosPortBaseCtl_m     epe_acl_qos_port_base_ctl;


    sal_memset(&oam_update_aps_ctl, 0, sizeof(OamUpdateApsCtl_m));
    sal_memset(&oam_header_edit_ctl, 0, sizeof(OamHeaderEditCtl_m));

    sal_memset(&ds_oam_excp, 0, sizeof(DsOamExcp_m));
    sal_memset(&ipe_lkup_port_base_ctl, 0, sizeof(IpeLookupPortBaseCtl_m));
    sal_memset(&epe_acl_qos_port_base_ctl, 0, sizeof(EpeAclQosPortBaseCtl_m));

    SetIpeLookupPortBaseCtl(V, localPhyPortBase_f, &ipe_lkup_port_base_ctl, 256);
    cmd = DRV_IOW(IpeLookupPortBaseCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 1, cmd, &ipe_lkup_port_base_ctl)); /* for slice 1 begin with 256~~~*/

    SetEpeAclQosPortBaseCtl(V, localPhyPortBase_f, &epe_acl_qos_port_base_ctl, 256);
    cmd = DRV_IOW(EpeAclQosPortBaseCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 1, cmd, &epe_acl_qos_port_base_ctl)); /* for slice 1 begin with 256~~~*/

    if (g_gg_oam_master[lchip]->no_mep_resource)
    {
        return CTC_E_NONE;
    }

     /*CTC_BIT_SET(cpu_excp[0], SYS_OAM_EXCP_ETH_INVALID);*/
    CTC_BIT_SET(cpu_excp[0], SYS_OAM_EXCP_ETH_LB);
    CTC_BIT_SET(cpu_excp[0], SYS_OAM_EXCP_ETH_LT);
    CTC_BIT_SET(cpu_excp[0], SYS_OAM_EXCP_ETH_LM);
    CTC_BIT_SET(cpu_excp[0], SYS_OAM_EXCP_ETH_DM);
    CTC_BIT_SET(cpu_excp[0], SYS_OAM_EXCP_ETH_TST);
    CTC_BIT_SET(cpu_excp[0], SYS_OAM_EXCP_ETH_APS);
    CTC_BIT_SET(cpu_excp[0], SYS_OAM_EXCP_ETH_SCC);
    CTC_BIT_SET(cpu_excp[0], SYS_OAM_EXCP_ETH_MCC);
    CTC_BIT_SET(cpu_excp[0], SYS_OAM_EXCP_ETH_CSF);
    CTC_BIT_SET(cpu_excp[0], SYS_OAM_EXCP_ETH_BIG_CCM);
    CTC_BIT_SET(cpu_excp[0], SYS_OAM_EXCP_ETH_LEARN_CCM);
     /*CTC_BIT_SET(cpu_excp[0], SYS_OAM_EXCP_ETH_DEFECT);*/
     /*CTC_BIT_SET(cpu_excp[0], SYS_OAM_EXCP_PBX_OAM);*/
     /*CTC_BIT_SET(cpu_excp[0], SYS_OAM_EXCP_ETH_HIGH_VER);*/
     /*CTC_BIT_SET(cpu_excp[0], SYS_OAM_EXCP_ETH_TLV);*/
     CTC_BIT_SET(cpu_excp[0], SYS_OAM_EXCP_ETH_OTHER);
     /*CTC_BIT_SET(cpu_excp[0], SYS_OAM_EXCP_BFD_INVALID);*/
    CTC_BIT_SET(cpu_excp[0], SYS_OAM_EXCP_BFD_LEARN);
    CTC_BIT_SET(cpu_excp[0], SYS_OAM_EXCP_BIG_BFD);
    CTC_BIT_SET(cpu_excp[0], SYS_OAM_EXCP_BFD_TIMER_NEG);
    CTC_BIT_SET(cpu_excp[0], SYS_OAM_EXCP_TP_SCC);
    CTC_BIT_SET(cpu_excp[0], SYS_OAM_EXCP_TP_MCC);
    CTC_BIT_SET(cpu_excp[0], SYS_OAM_EXCP_TP_CSF);
    CTC_BIT_SET(cpu_excp[0], SYS_OAM_EXCP_TP_LB);
    CTC_BIT_SET(cpu_excp[0], SYS_OAM_EXCP_TP_DLM);
    CTC_BIT_SET(cpu_excp[0], SYS_OAM_EXCP_TP_DM);
    CTC_BIT_SET(cpu_excp[0], SYS_OAM_EXCP_TP_FM);
     /*CTC_BIT_SET(cpu_excp[0], SYS_OAM_EXCP_BFD_OTHER);*/
     /*CTC_BIT_SET(cpu_excp[0], SYS_OAM_EXCP_BFD_DISC_MISMATCH);*/
    CTC_BIT_SET(cpu_excp[0], SYS_OAM_EXCP_ETH_SM);
    CTC_BIT_SET(cpu_excp[1], SYS_OAM_EXCP_TP_CV-32);

    cmd = DRV_IOR(OamHeaderEditCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &oam_header_edit_ctl));
    DRV_SET_FIELD_A(OamHeaderEditCtl_t,OamHeaderEditCtl_cpuExceptionEn_f,&oam_header_edit_ctl,cpu_excp);

    cmd = DRV_IOW(OamHeaderEditCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &oam_header_edit_ctl));

    /*OamEngine Update*/
    freq = sys_goldengate_get_core_freq(lchip, 0);
    sal_memset(&oam_update_ctl, 0, sizeof(OamUpdateCtl_m));
    cmd = DRV_IOR(OamUpdateCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &oam_update_ctl));
    /*CCM Timer*/
    ccm_min_ptr = 0;
    oam_up_phy_num       = TABLE_MAX_INDEX(DsEthMep_t) ;
    upd_interval = (freq/10 * 8333)/oam_up_phy_num - 1;
    ccm_max_ptr = (freq/10 * 8333)/(upd_interval + 1) + ccm_min_ptr - 1;
    SetOamUpdateCtl(V, updEn_f, &oam_update_ctl, 0);
    SetOamUpdateCtl(V, ccmMinPtr_f, &oam_update_ctl, ccm_min_ptr);
    SetOamUpdateCtl(V, oamUpPhyNum_f, &oam_update_ctl, oam_up_phy_num);
    SetOamUpdateCtl(V, updInterval_f, &oam_update_ctl, upd_interval);
    SetOamUpdateCtl(V, ccmMaxPtr_f, &oam_update_ctl, ccm_max_ptr);

    /*BFD Timer*/
    bfd_min_ptr = 0;
    oam_bfd_phy_num       = TABLE_MAX_INDEX(DsEthMep_t) ;
    bfd_upd_interval = (freq *1000) / oam_bfd_phy_num - 1;
    bfd_max_ptr = (freq *1000) / (bfd_upd_interval + 1) + bfd_min_ptr - 1;
    SetOamUpdateCtl(V, bfdUpdEn_f, &oam_update_ctl, 0);
    SetOamUpdateCtl(V, bfdMinptr_f, &oam_update_ctl, bfd_min_ptr);
    SetOamUpdateCtl(V, oamBfdPhyNum_f, &oam_update_ctl, oam_bfd_phy_num);
    SetOamUpdateCtl(V, bfdUpdInterval_f, &oam_update_ctl, bfd_upd_interval);
    SetOamUpdateCtl(V, bfdMaxPtr_f, &oam_update_ctl, bfd_max_ptr);
    SetOamUpdateCtl(V, sfFailWhileCfg0_f, &oam_update_ctl, 1);
    SetOamUpdateCtl(V, sfFailWhileCfg1_f, &oam_update_ctl, 3);
    cmd = DRV_IOW(OamUpdateCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &oam_update_ctl));

    sal_memset(&oam_ether_cci_ctl, 0, sizeof(OamEtherCciCtl_m));
    quater_cci2_interval = (upd_interval + 1) * 3 *(ccm_max_ptr - ccm_min_ptr + 1);
    SetOamEtherCciCtl(V, quaterCci2Interval_f, &oam_ether_cci_ctl, quater_cci2_interval);
    SetOamEtherCciCtl(V, relativeCci3Interval_f, &oam_ether_cci_ctl, 9);
    SetOamEtherCciCtl(V, relativeCci4Interval_f, &oam_ether_cci_ctl, 9);
    SetOamEtherCciCtl(V, relativeCci5Interval_f, &oam_ether_cci_ctl, 9);
    SetOamEtherCciCtl(V, relativeCci6Interval_f, &oam_ether_cci_ctl, 5);
    SetOamEtherCciCtl(V, relativeCci7Interval_f, &oam_ether_cci_ctl, 9);
    cmd = DRV_IOW(OamEtherCciCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &oam_ether_cci_ctl));

    /*enable update*/
    tmp_field = 1;
    cmd = DRV_IOW(OamUpdateCtl_t, OamUpdateCtl_updEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp_field));

    tmp_field = 1;
    cmd = DRV_IOW(OamUpdateCtl_t, OamUpdateCtl_bfdUpdEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp_field));


    cmd = DRV_IOR(OamUpdateApsCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &oam_update_aps_ctl));
    SetOamUpdateApsCtl(V, globalApsEn_f, &oam_update_aps_ctl, 1);
    SetOamUpdateApsCtl(V, srcChipId_f, &oam_update_aps_ctl, lchip);
    SetOamUpdateApsCtl(V, tpid_f, &oam_update_aps_ctl, 0x8100);
    SetOamUpdateApsCtl(V, vlan_f, &oam_update_aps_ctl, 4095);
    SetOamUpdateApsCtl(V, apsEthType_f, &oam_update_aps_ctl, 0x55aa);
    cmd = DRV_IOW(OamUpdateApsCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &oam_update_aps_ctl));

    tmp_field = SYS_QOS_CLASS_PRIORITY_MAX;
    cmd = DRV_IOW(IpeFwdCtl_t, IpeFwdCtl_linkOamPriority_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp_field));
    tmp_field = CTC_QOS_COLOR_GREEN;
    cmd = DRV_IOW(IpeFwdCtl_t, IpeFwdCtl_linkOamColor_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp_field));

    return CTC_E_NONE;
}

int32
sys_goldengate_oam_isr_defect_process(uint8 lchip, void* p_data)
{
    uint8 gchip = 0;
    static ctc_oam_mep_info_t     mep_info;
    static ctc_oam_event_t event;
    sys_oam_defect_param_t*  defect_param = p_data;
    uint8 defect_index      = 0;
    uint8 mep_bit_index     = 0;
    uint32 mep_index_bitmap = 0;
    uint32 mep_index_base   = 0;
    uint32 mep_index        = 0;

    if(p_data==NULL)
    {
        return CTC_E_NONE;
    }

    sal_memset(&mep_info,       0, sizeof(ctc_oam_mep_info_t));

    CTC_ERROR_RETURN(sys_goldengate_get_gchip_id(lchip, &gchip));
    if (g_gg_oam_master[lchip]->error_cache_cb)
    {
        if (1 == defect_param->isr_state)
        {
            sal_memset(&event,          0, sizeof(ctc_oam_event_t));
        }

        for (defect_index = 0; defect_index < defect_param->defect_info.valid_cache_num; defect_index++)
        {
            mep_index_bitmap    = defect_param->defect_info.mep_index_bitmap[defect_index];
            mep_index_base      = defect_param->defect_info.mep_index_base[defect_index];

            for (mep_bit_index = 0; mep_bit_index < 32; mep_bit_index++)
            {
                if (IS_BIT_SET(mep_index_bitmap, mep_bit_index))
                {
                    mep_index = mep_index_base * 32 + mep_bit_index;
                    mep_info.mep_index = mep_index;
                    _sys_goldengate_oam_get_mep_info(lchip, &mep_info);
                    _sys_goldengate_oam_build_defect_event(lchip, &mep_info, &event);

                    if (event.valid_entry_num >= CTC_OAM_EVENT_MAX_ENTRY_NUM)
                    {
                        g_gg_oam_master[lchip]->error_cache_cb(gchip, &event);
                        sal_memset(&event, 0, sizeof(ctc_oam_event_t));
                    }
                }
            }
        }

        if ((3 == defect_param->isr_state) && (event.valid_entry_num > 0))
        {
            g_gg_oam_master[lchip]->error_cache_cb(gchip, &event);
        }
    }

     return CTC_E_NONE;
}

int32
sys_goldengate_oam_set_defect_process_cb(uint8 lchip, SYS_OAM_DEFECT_PROCESS_FUNC cb)
{
    SYS_OAM_INIT_CHECK(lchip);
    g_gg_oam_master[lchip]->defect_process_cb = cb;
    return CTC_E_NONE;
}


int32
sys_goldengate_oam_get_defect_process_cb(uint8 lchip, void** cb)
{
    SYS_OAM_INIT_CHECK(lchip);
    *cb = g_gg_oam_master[lchip]->defect_process_cb;
    return CTC_E_NONE;
}


int32
_sys_goldengate_oam_db_init(uint8 lchip, ctc_oam_global_t* p_com_glb)
{
    uint32  fun_num            = 0;
    uint32 chan_entry_num     = 512;
    uint32 mep_entry_num      = 0;
    uint32 ma_entry_num       = 0;
    uint32 ma_name_entry_num  = 0;
    uint32 lm_entry_num       = 0;
    uint32 lm_profile_entry_num       = 0;
    uint8  oam_type           = 0;
    uint8 no_mep_resource = 0;
    int32 ret = CTC_E_NONE;
    sys_goldengate_opf_t opf   = {0};

    /*************************************************/
    /*          init opf for related tables          */
    /*************************************************/
    LCHIP_CHECK(lchip);

    CTC_ERROR_RETURN(sys_goldengate_opf_init(lchip, OPF_OAM_MEP_LMEP, 1));
    CTC_ERROR_RETURN(sys_goldengate_opf_init(lchip, OPF_OAM_MA, 1));
    CTC_ERROR_RETURN(sys_goldengate_opf_init(lchip, OPF_OAM_MA_NAME, 1));
    CTC_ERROR_RETURN(sys_goldengate_opf_init(lchip, OPF_OAM_LM_PROFILE, 1));
    CTC_ERROR_RETURN(sys_goldengate_opf_init(lchip, OPF_OAM_LM, 1));

    CTC_ERROR_RETURN(sys_goldengate_ftm_query_table_entry_num(lchip, DsEthMep_t,  &mep_entry_num));
    CTC_ERROR_RETURN(sys_goldengate_ftm_query_table_entry_num(lchip, DsMa_t,  &ma_entry_num));
    CTC_ERROR_RETURN(sys_goldengate_ftm_query_table_entry_num(lchip, DsMaName_t,  &ma_name_entry_num));
    CTC_ERROR_RETURN(sys_goldengate_ftm_query_table_entry_num(lchip, DsEthLmProfile_t,  &lm_profile_entry_num));
    CTC_ERROR_RETURN(sys_goldengate_ftm_query_table_entry_num(lchip, DsOamLmStats_t,  &lm_entry_num));

    if ((0 == mep_entry_num) || (0 == ma_entry_num) || (0 == ma_name_entry_num)
        ||(0 == lm_profile_entry_num)||(0 == lm_entry_num))
    {
         /*no need to alloc MEP resource on line card in Fabric system*/
         no_mep_resource = 1;
    }
    else
    {

        opf.pool_index = 0;

        opf.pool_type = OPF_OAM_MEP_LMEP;
        CTC_ERROR_RETURN(sys_goldengate_opf_init_offset(lchip, &opf, 2, mep_entry_num - 2));

        opf.pool_type = OPF_OAM_MA;
        CTC_ERROR_RETURN(sys_goldengate_opf_init_offset(lchip, &opf, 0, ma_entry_num));

        opf.pool_type = OPF_OAM_MA_NAME;
        CTC_ERROR_RETURN(sys_goldengate_opf_init_offset(lchip, &opf, 0, ma_name_entry_num));

        opf.pool_type = OPF_OAM_LM_PROFILE;
        CTC_ERROR_RETURN(sys_goldengate_opf_init_offset(lchip, &opf, 0, lm_profile_entry_num));

        opf.pool_type = OPF_OAM_LM;
        CTC_ERROR_RETURN(sys_goldengate_opf_init_offset(lchip, &opf, 0, lm_entry_num));
    }
    /*************************************************/
    /*               init global param               */
    /*************************************************/
    g_gg_oam_master[lchip] = (sys_oam_master_t*)mem_malloc(MEM_OAM_MODULE, sizeof(sys_oam_master_t));
    if (NULL == g_gg_oam_master[lchip])
    {
        goto err;
    }
    sal_memset(g_gg_oam_master[lchip], 0 ,sizeof(sys_oam_master_t));

    g_gg_oam_master[lchip]->no_mep_resource = no_mep_resource;
    g_gg_oam_master[lchip]->defect_process_cb = sys_goldengate_oam_isr_defect_process;

    /*IPv6 address */
    g_gg_oam_master[lchip]->ipv6_bmp = mem_malloc(MEM_OAM_MODULE,(SYS_OAM_BFD_IPV6_MAX_IPSA_NUM * 1) * sizeof(sys_bfd_ip_v6_node_t));
    if (NULL == g_gg_oam_master[lchip]->ipv6_bmp )
    {
        goto err;
    }
    sal_memset(g_gg_oam_master[lchip]->ipv6_bmp, 0,(SYS_OAM_BFD_IPV6_MAX_IPSA_NUM * 1) * sizeof(sys_bfd_ip_v6_node_t));

    fun_num = CTC_OAM_MEP_TYPE_MAX * SYS_OAM_MODULE_MAX * SYS_OAM_OP_MAX;
    g_gg_oam_master[lchip]->p_fun_table = (SYS_OAM_CALL_FUNC_P*)
        mem_malloc(MEM_OAM_MODULE, fun_num * sizeof(SYS_OAM_CALL_FUNC_P));
    if (NULL == g_gg_oam_master[lchip]->p_fun_table)
    {
        goto err;
    }

    sal_memset(g_gg_oam_master[lchip]->p_fun_table, 0, fun_num * sizeof(SYS_OAM_CALL_FUNC_P));


    fun_num = CTC_OAM_MEP_TYPE_MAX * SYS_OAM_CHECK_MAX;
    g_gg_oam_master[lchip]->p_check_fun_table = (SYS_OAM_CHECK_CALL_FUNC_P*)
        mem_malloc(MEM_OAM_MODULE, fun_num * sizeof(SYS_OAM_CHECK_CALL_FUNC_P));
    if (NULL == g_gg_oam_master[lchip]->p_check_fun_table)
    {
        goto err;
    }

    sal_memset(g_gg_oam_master[lchip]->p_check_fun_table, 0, fun_num * sizeof(SYS_OAM_CHECK_CALL_FUNC_P));

    if (!g_gg_oam_master[lchip]->no_mep_resource)
    {
        g_gg_oam_master[lchip]->maid_hash = ctc_hash_create((ma_name_entry_num / (SYS_OAM_MAID_HASH_BLOCK_SIZE * 2)),
                                                            SYS_OAM_MAID_HASH_BLOCK_SIZE,
                                                            (hash_key_fn)_sys_goldengate_oam_maid_build_key,
                                                            (hash_cmp_fn)_sys_goldengate_oam_maid_cmp);

        if (NULL == g_gg_oam_master[lchip]->maid_hash)
        {
            goto err;
        }
    }
    g_gg_oam_master[lchip]->chan_hash = ctc_hash_create((chan_entry_num / SYS_OAM_CHAN_HASH_BLOCK_SIZE),
                                              SYS_OAM_CHAN_HASH_BLOCK_SIZE,
                                              (hash_key_fn)_sys_goldengate_oam_chan_build_key,
                                              (hash_cmp_fn)_sys_goldengate_oam_chan_cmp);
    if (NULL == g_gg_oam_master[lchip]->chan_hash)
    {
        goto err;
    }

#define SYS_OAM_MEP_VEC_BLOCK_NUM 128
    if (!g_gg_oam_master[lchip]->no_mep_resource)
    {
        g_gg_oam_master[lchip]->mep_vec =
        ctc_vector_init(SYS_OAM_MEP_VEC_BLOCK_NUM, mep_entry_num / SYS_OAM_MEP_VEC_BLOCK_NUM);
        if (NULL == g_gg_oam_master[lchip]->mep_vec)
        {
            goto err;
        }
        SYS_OAM_TBL_CNT(lchip, SYS_OAM_TBL_MEP, 1, 2);
    }
    if (CTC_E_NONE != sal_mutex_create(&(g_gg_oam_master[lchip]->oam_mutex)))
    {
        goto err;
    }

    sal_memcpy(&(g_gg_oam_master[lchip]->com_oam_global), p_com_glb, sizeof(ctc_oam_global_t));

    CTC_ERROR_GOTO(_sys_goldengate_oam_defect_scan_en(lchip, TRUE), ret, err);
    CTC_ERROR_GOTO(_sys_goldengate_oam_com_reg_init(lchip), ret, err);


    oam_type = CTC_OAM_PROPERTY_TYPE_COMMON;

    SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_GLOB, SYS_OAM_CONFIG) = _sys_goldengate_oam_set_common_property;

    sys_goldengate_ftm_register_callback(lchip, CTC_FTM_SPEC_OAM, sys_goldengate_oam_ftm_cb);


    goto success;

    /*************************************************/
    /*               Error rollback                  */
    /*************************************************/

err:

    if (NULL != g_gg_oam_master[lchip]->oam_mutex)
    {
        sal_mutex_destroy(g_gg_oam_master[lchip]->oam_mutex);
        g_gg_oam_master[lchip]->oam_mutex = NULL;
    }

    if (NULL != g_gg_oam_master[lchip]->mep_vec)
    {
        ctc_vector_release(g_gg_oam_master[lchip]->mep_vec);
    }

    if (NULL != g_gg_oam_master[lchip]->chan_hash)
    {
        ctc_hash_free(g_gg_oam_master[lchip]->chan_hash);
    }

    if (NULL != g_gg_oam_master[lchip]->maid_hash)
    {
        ctc_hash_free(g_gg_oam_master[lchip]->maid_hash);
    }

    if (NULL != g_gg_oam_master[lchip]->p_fun_table)
    {
        mem_free(g_gg_oam_master[lchip]->p_fun_table);
    }
    if (NULL != g_gg_oam_master[lchip]->p_check_fun_table)
    {
        mem_free(g_gg_oam_master[lchip]->p_check_fun_table);
    }
    if (NULL != g_gg_oam_master[lchip]->ipv6_bmp)
    {
        mem_free(g_gg_oam_master[lchip]->ipv6_bmp);
    }
    if (NULL != g_gg_oam_master[lchip])
    {
        mem_free(g_gg_oam_master[lchip]);
    }

    return CTC_E_NO_MEMORY;

success:
    return CTC_E_NONE;

}

STATIC int32
_sys_goldengate_oam_db_free_node_data(void* node_data, void* user_data)
{
    mem_free(node_data);

    return CTC_E_NONE;
}

int32
_sys_goldengate_oam_db_deinit(uint8 lchip)
{

    LCHIP_CHECK(lchip);
    if (NULL == g_gg_oam_master[lchip])
    {
        return CTC_E_NONE;
    }

    if (g_gg_oam_master[lchip]->error_cache_cb && (!g_gg_oam_master[lchip]->no_mep_resource))
    {
        mem_free(g_gg_oam_master[lchip]->mep_defect_bitmap);
    }

    /*free chan*/
    ctc_hash_traverse(g_gg_oam_master[lchip]->chan_hash, (hash_traversal_fn)_sys_goldengate_oam_db_free_node_data, NULL);
    ctc_hash_free(g_gg_oam_master[lchip]->chan_hash);

    /*free mep vector*/
    ctc_vector_traverse(g_gg_oam_master[lchip]->mep_vec, (vector_traversal_fn)_sys_goldengate_oam_db_free_node_data, NULL);
    ctc_vector_release(g_gg_oam_master[lchip]->mep_vec);

    /*free maid*/
    ctc_hash_traverse(g_gg_oam_master[lchip]->maid_hash, (hash_traversal_fn)_sys_goldengate_oam_db_free_node_data, NULL);
    ctc_hash_free(g_gg_oam_master[lchip]->maid_hash);

    mem_free(g_gg_oam_master[lchip]->p_check_fun_table);
    mem_free(g_gg_oam_master[lchip]->p_fun_table);
    mem_free(g_gg_oam_master[lchip]->ipv6_bmp);

    sys_goldengate_opf_deinit(lchip, OPF_OAM_MEP_LMEP);
    sys_goldengate_opf_deinit(lchip, OPF_OAM_MA);
    sys_goldengate_opf_deinit(lchip, OPF_OAM_MA_NAME);
    sys_goldengate_opf_deinit(lchip, OPF_OAM_LM_PROFILE);
    sys_goldengate_opf_deinit(lchip, OPF_OAM_LM);

    sal_mutex_destroy(g_gg_oam_master[lchip]->oam_mutex);
    mem_free(g_gg_oam_master[lchip]);

    return CTC_E_NONE;
}


/**
 @file sys_goldengate_nexthop_mpls.c

 @date 2010-01-10

 @version v2.0

 The file contains all nexthop mpls related callback function
*/

/****************************************************************************
 *
* Header Files
*
****************************************************************************/
#include "ctc_const.h"
#include "ctc_error.h"
#include "sys_goldengate_chip.h"
#include "sys_goldengate_nexthop_api.h"
#include "sys_goldengate_nexthop_hw.h"
#include "sys_goldengate_nexthop.h"

#include "sys_goldengate_aps.h"
#include "sys_goldengate_l3if.h"
#include "sys_goldengate_register.h"
#include "sys_goldengate_stats.h"

#include "goldengate/include/drv_lib.h"
#include "goldengate/include/drv_io.h"


#define SYS_NH_DSL3EDIT_MPLS4W_LABEL_NUMBER  2
#define SYS_NH_DSL3EDIT_MPLS8W_LABEL_NUMBER  4
#define SYS_NH_MARTINI_SEQ_TYPE_DISABLE      0
#define SYS_NH_MARTINI_SEQ_TYPE_PER_PW       1
#define SYS_NH_MARTINI_SEQ_TYPE_GLB0         2
#define SYS_NH_MARTINI_SEQ_TYPE_GLB1         3
#define SYS_NH_MPLS_LABEL_LOW2BITS_MASK      0xFFFFC
#define SYS_NH_MARTINI_SEQ_SHIFT             4
#define SYS_NH_MPLS_LABEL_MASK               0xFFFFF
#define SYS_NH_MPLS_LABEL_EXP_SELECT_PACKET  0
#define SYS_NH_MPLS_LABEL_EXP_SELECT_MAP     1
#define SYS_NH_MAX_EXP_VALUE                 7

#define SYS_NH_MPLS_LABLE_PW                 0
#define SYS_NH_MPLS_LABLE_LSP                1
#define SYS_NH_MPLS_LABLE_SPME               2

#define SYS_NH_MPLS_LSP_OP_ADD                1
#define SYS_NH_MPLS_LSP_OP_UPDATE          2


#define SYS_NH_CHECK_MPLS_EXP_VALUE(exp)                    \
    do {                                                        \
        if ((exp) > (7)){return CTC_E_INVALID_EXP_VALUE; }         \
    } while (0);

#define SYS_NH_CHECK_MPLS_LABVEL_VALUE(label)               \
    {                                                        \
        if ((label) > (0xFFFFF)){                                 \
            return CTC_E_INVALID_MPLS_LABEL_VALUE; }              \
    }

/****************************************************************************
 *
* Defines and Macros
*
*****************************************************************************/


STATIC int32
_sys_goldengate_nh_mpls_encode_dsmpls(uint8 lchip, ctc_mpls_nh_label_param_t* p_label,
                                      sys_dsmpls_t*  p_dsmpls,
                                      uint32 stats_id,
                                      uint8 stats_type)
{
    uint32 ttl_index = 0;

    p_dsmpls->l3_rewrite_type = SYS_NH_L3EDIT_TYPE_MPLS_4W;
    p_dsmpls->ds_type = SYS_NH_DS_TYPE_L3EDIT;
    p_dsmpls->oam_en = 0;

    if (CTC_FLAG_ISSET(p_label->lable_flag, CTC_MPLS_NH_LABEL_MAP_TTL))
    {
        p_dsmpls->map_ttl = 1;
        p_dsmpls->map_ttl_mode = 1;     /* only for lsp label */
    }

    if (CTC_FLAG_ISSET(p_label->lable_flag, CTC_MPLS_NH_LABEL_IS_MCAST))
    {
        p_dsmpls->mcast_label = 1;
    }

    if (stats_id)
    {
        CTC_ERROR_RETURN(sys_goldengate_stats_get_statsptr(lchip, stats_id, stats_type, &(p_dsmpls->statsptr)));
    }

    CTC_ERROR_RETURN(sys_goldengate_lkup_ttl_index(lchip, p_label->ttl, &ttl_index));
    p_dsmpls->ttl_index = ttl_index;
    p_dsmpls->label  = (p_label->label & 0xFFFFF);   /*label*/

    switch (p_label->exp_type)
    {
        case CTC_NH_EXP_SELECT_ASSIGN:   /* user cfg exp  */
            p_dsmpls->derive_exp  = 0;
            p_dsmpls->exp = p_label->exp & 0x7;
            break;

        case CTC_NH_EXP_SELECT_MAP:  /*mapped exp */
            p_dsmpls->derive_exp = 1;
            p_dsmpls->exp = SYS_NH_MPLS_LABEL_EXP_SELECT_MAP;
            break;

        case CTC_NH_EXP_SELECT_PACKET:   /*input packet exp  */

            p_dsmpls->derive_exp = 1;
            p_dsmpls->exp = SYS_NH_MPLS_LABEL_EXP_SELECT_PACKET;
            break;

        default:
            break;
    }

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_nh_mpls_encode_cw(uint8 lchip, ctc_mpls_nexthop_push_param_t* p_nh_param_push,
                                  sys_dsmpls_t*  p_dsmpls)
{

    if (!p_nh_param_push->martini_encap_valid)
    {
       return CTC_E_NONE;
    }

	/*GB use fixed CW*/
    if (0 == p_nh_param_push->martini_encap_type)
    {
        p_dsmpls->outer_editptr = 1; /*martini_encap_valid*/
        if (p_nh_param_push->nh_com.opcode == CTC_MPLS_NH_PUSH_OP_L2VPN)
        { /*etree leaf*/
            p_dsmpls->outer_editptr_type = 1; /*use_src_leaf*/
        }
        else
        { /*normal CW*/
            p_dsmpls->outer_editptr_type = 0;
        }
    }
    else if (1 == p_nh_param_push->martini_encap_type)
    {
       p_dsmpls->outer_editptr = 1; /*martini_encap_valid*/
    }

    return CTC_E_NONE;
}



STATIC int32
_sys_goldengate_nh_mpls_build_pw(uint8 lchip, ctc_mpls_nexthop_push_param_t* p_nh_param_push,
                                      sys_nh_param_dsnh_t* dsnh,
                                      sys_nh_db_mpls_tunnel_t* p_mpls_tunnel)
{
    sys_dsmpls_t  dsmpls;
    uint8 label_num                = 0;
    uint32 stats_id                = 0;
    uint8 next_is_spme             = 0;

    sal_memset(&dsmpls, 0, sizeof(sys_dsmpls_t));

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    label_num = p_nh_param_push->label_num;

    if (label_num > 1)
    {
        return CTC_E_NOT_SUPPORT;
    }

    if (label_num == 0)
    {
        return CTC_E_NONE;
    }

    if (p_nh_param_push->stats_valid)
    {
        stats_id = p_nh_param_push->stats_id;
    }

    next_is_spme = CTC_FLAG_ISSET(p_mpls_tunnel->flag, SYS_NH_MPLS_TUNNEL_FLAG_IS_SPME);
    CTC_ERROR_RETURN(
    _sys_goldengate_nh_mpls_encode_dsmpls(lchip, &p_nh_param_push->push_label[0], &dsmpls,
                                          stats_id, CTC_STATS_STATSID_TYPE_NEXTHOP_MPLS_PW));

    CTC_ERROR_RETURN(_sys_goldengate_nh_mpls_encode_cw(lchip, p_nh_param_push, &dsmpls));


    if (p_nh_param_push->nh_com.opcode == CTC_MPLS_NH_PUSH_OP_L2VPN)
    { /*dscp use L2header's cos or L3header's dscp */
        dsmpls.src_dscp_type = 1;   /*if set to 1, L2header's cos*/
    }

    if (dsnh->hvpls)
    {
        dsmpls.logic_dest_port = (dsnh->logic_port & 0x3FFF);
    }


    /*1. Allocate new dsl3edit mpls entry*/
    CTC_ERROR_RETURN(sys_goldengate_nh_offset_alloc(lchip,
                                                   SYS_NH_ENTRY_TYPE_L3EDIT_MPLS, 1,
                                                   &dsnh->l3edit_ptr));

    if (next_is_spme)
    {
        dsmpls.next_editptr_valid = 1;
        dsmpls.outer_editptr = p_mpls_tunnel->lsp_offset[0];
        dsmpls.outer_editptr_type = 1;
    }
    else
    {

    }
    /*2. Write HW table*/
    CTC_ERROR_RETURN(sys_goldengate_nh_write_asic_table(lchip,
                                                       SYS_NH_ENTRY_TYPE_L3EDIT_MPLS,
                                                       dsnh->l3edit_ptr, &dsmpls));
    return CTC_E_NONE;

}

STATIC int32
_sys_goldengate_nh_mpls_build_dsl2edit(uint8 lchip, uint8  protection_spme,
                                       uint8 proecting_lsp,
                                       ctc_mpls_nexthop_tunnel_info_t* p_nh_param,
                                       sys_nh_db_mpls_tunnel_t* p_mpls_tunnel)
{
    sys_nh_db_dsl2editeth4w_t   dsl2edit_4w;
    sys_nh_db_dsl2editeth4w_t*  p_dsl2edit_4w  = &dsl2edit_4w;
    sys_nh_db_dsl2editeth8w_t   dsl2edit_8w;
    sys_nh_db_dsl2editeth8w_t*  p_dsl2edit_8w  = &dsl2edit_8w;
    sys_l3if_prop_t l3if_prop;
    mac_addr_t mac = {0};

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "protection_spme:%d, proecting_lsp:%d\n",
                   protection_spme, proecting_lsp);


    if (!(CTC_IS_CPU_PORT(p_nh_param->oif.gport) || p_nh_param->oif.is_l2_port || SYS_IS_DROP_PORT(p_nh_param->oif.gport)))
    {
        CTC_ERROR_RETURN(sys_goldengate_l3if_get_l3if_info_with_port_and_vlan(lchip, p_nh_param->oif.gport,
                                                                              p_nh_param->oif.vid, &l3if_prop));
    }

    if (p_nh_param->arp_id)
    {
        sys_nh_db_arp_t *p_arp = NULL;
        sys_nh_info_arp_param_t arp_parm;

        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "p_nh_param->arp_id:%d\n", p_nh_param->arp_id);

        sal_memset(&arp_parm, 0, sizeof(sys_nh_info_arp_param_t));
        arp_parm.p_oif          = &p_nh_param->oif;
        arp_parm.nh_entry_type  = SYS_NH_TYPE_MPLS;

        CTC_ERROR_RETURN(sys_goldengate_nh_bind_arp_cb(lchip, &arp_parm, p_nh_param->arp_id));
        CTC_ERROR_RETURN(sys_goldengate_nh_lkup_arp_id(lchip, p_nh_param->arp_id, &p_arp));

        p_mpls_tunnel->l2edit_offset[proecting_lsp][protection_spme] = p_arp->offset;
        p_mpls_tunnel->arp_id[proecting_lsp][protection_spme]        = p_nh_param->arp_id;

    }
    else if(0 == sal_memcmp(p_nh_param->mac_sa, mac, sizeof(mac_addr_t)))
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "p_nh_param->mac:%s\n", sys_goldengate_output_mac(p_nh_param->mac));
        sal_memset(&dsl2edit_4w, 0, sizeof(sys_nh_db_dsl2editeth4w_t));

        if (p_nh_param->oif.vid > 0 && p_nh_param->oif.vid <= CTC_MAX_VLAN_ID)
        {
            dsl2edit_4w.output_vid = p_nh_param->oif.vid;
            dsl2edit_4w.output_vlan_is_svlan = 1;
        }

        sal_memcpy(dsl2edit_4w.mac_da, p_nh_param->mac, sizeof(mac_addr_t));

        CTC_ERROR_RETURN(sys_goldengate_nh_add_route_l2edit(lchip, &p_dsl2edit_4w));

        p_mpls_tunnel->l2edit_offset[proecting_lsp][protection_spme] = p_dsl2edit_4w->offset;
        p_mpls_tunnel->p_dsl2edit_4w[proecting_lsp][protection_spme] = p_dsl2edit_4w;
    }
    else
    {
        /*edit mac sa, must use l2edit8w, for openflow*/
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "p_nh_param->mac:%s\n", sys_goldengate_output_mac(p_nh_param->mac));
        sal_memset(&dsl2edit_8w, 0, sizeof(sys_nh_db_dsl2editeth8w_t));

        if (p_nh_param->oif.vid > 0 && p_nh_param->oif.vid <= CTC_MAX_VLAN_ID)
        {
            dsl2edit_8w.output_vid = p_nh_param->oif.vid;
            dsl2edit_8w.output_vlan_is_svlan = 1;
        }

        sal_memcpy(dsl2edit_8w.mac_da, p_nh_param->mac, sizeof(mac_addr_t));
        sal_memcpy(dsl2edit_8w.mac_sa, p_nh_param->mac_sa, sizeof(mac_addr_t));
        dsl2edit_8w.update_mac_sa = 1;

        CTC_ERROR_RETURN(sys_goldengate_nh_add_route_l2edit_8w(lchip, &p_dsl2edit_8w));

        CTC_SET_FLAG(p_mpls_tunnel->flag , SYS_NH_MPLS_TUNNEL_FLAG_IS_L2EDIT8W);
        p_mpls_tunnel->l2edit_offset[proecting_lsp][protection_spme] = p_dsl2edit_8w->offset;
        p_mpls_tunnel->p_dsl2edit_4w[proecting_lsp][protection_spme] = (sys_nh_db_dsl2editeth4w_t*)p_dsl2edit_8w;
    }

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "l2edit_offset[%d][%d]:%d\n",
                   proecting_lsp, protection_spme,
                   p_mpls_tunnel->l2edit_offset[proecting_lsp][protection_spme]);


    return CTC_E_NONE;

}

STATIC int32
_sys_goldengate_nh_mpls_build_inner_dsl2edit(uint8 lchip,ctc_mpls_nexthop_push_param_t* p_nh_param_push,
                                                            sys_nh_param_dsnh_t* dsnh,
                                                            sys_nh_info_mpls_t* p_nhdb)
{
    sys_nh_db_dsl2editeth8w_t* p_dsl2edit6w = NULL;
    sys_nh_db_dsl2editeth8w_t dsl2edit6w;

    sal_memset(&dsl2edit6w, 0, sizeof(sys_nh_db_dsl2editeth8w_t));

    sal_memcpy(dsl2edit6w.mac_da, p_nh_param_push->nh_com.mac, sizeof(mac_addr_t));
    sal_memcpy(dsl2edit6w.mac_sa, p_nh_param_push->nh_com.mac_sa, sizeof(mac_addr_t));
    dsl2edit6w.update_mac_sa = 1;

    if (CTC_VLAN_EGRESS_EDIT_INSERT_VLAN == p_nh_param_push->nh_com.vlan_info.svlan_edit_type)
    {
        dsl2edit6w.output_vid           = p_nh_param_push->nh_com.vlan_info.output_svid;
        dsl2edit6w.output_vlan_is_svlan = 1;
    }
    p_dsl2edit6w = &dsl2edit6w;
    CTC_ERROR_RETURN(sys_goldengate_nh_add_l2edit_6w(lchip, (sys_nh_db_dsl2editeth8w_t**)&p_dsl2edit6w));
    dsnh->inner_l2edit_ptr = p_dsl2edit6w->offset;
    CTC_SET_FLAG(p_nhdb->hdr.nh_entry_flags , SYS_NH_INFO_FLAG_HAVE_L2EDI_6W);
    p_nhdb->p_dsl2edit_inner = p_dsl2edit6w;

    return CTC_E_NONE;
}
STATIC int32
_sys_goldengate_nh_mpls_build_tunnel(uint8 lchip, uint8  protection_spme,
                                     uint8 proecting_lsp,
                                     uint8 label_type,
                                     ctc_mpls_nexthop_tunnel_info_t* p_tunnel_info,
                                     sys_nh_db_mpls_tunnel_t* p_mpls_tunnel)
{
    sys_dsmpls_t  dsmpls;
    uint8 block_size               = 0;
    uint8 table_type               = 0;
    uint32 offset                  = 0;
    uint32 stats_id                = 0;
    int32 ret                      = CTC_E_NONE;
    ctc_mpls_nh_label_param_t* p_label = 0;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, " proecting_lsp:%d,protection_spme:%d, label_type:%d\n",
                  proecting_lsp,   protection_spme,label_type);


    block_size = 1;
    if (label_type == SYS_NH_MPLS_LABLE_LSP)
    {
        if(CTC_FLAG_ISSET(p_mpls_tunnel->flag, SYS_NH_MPLS_TUNNEL_FLAG_TWO_LAYER_APS)
            &&  protection_spme)
        {
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "2 Level spme protecting path!!\n");
            return CTC_E_NONE;
        }
        p_label = &(p_tunnel_info->tunnel_label[0]);
        if (p_tunnel_info->stats_valid)
        {
            stats_id = p_tunnel_info->stats_id;
        }
        if (CTC_FLAG_ISSET(p_mpls_tunnel->flag, SYS_NH_MPLS_TUNNEL_FLAG_IS_SPME))
        {
            table_type = SYS_NH_ENTRY_TYPE_L3EDIT_SPME;
        }
        else
        {
            table_type = SYS_NH_ENTRY_TYPE_L3EDIT_MPLS;
        }
        offset = p_mpls_tunnel->lsp_offset[proecting_lsp];
    }
    else
    {
        p_label = &(p_tunnel_info->tunnel_label[1]);
        table_type = SYS_NH_ENTRY_TYPE_L3EDIT_SPME;
        if (CTC_FLAG_ISSET(p_mpls_tunnel->flag, SYS_NH_MPLS_TUNNEL_FLAG_TWO_LAYER_APS))
        {
            block_size = 2;
        }
        if(protection_spme)
        {
            if (p_mpls_tunnel->spme_offset[proecting_lsp][0] == 0)
            {
                return CTC_E_INVALID_PARAM;
            }
            offset = p_mpls_tunnel->spme_offset[proecting_lsp][0] + 1;
        }
        else
        {
            offset = p_mpls_tunnel->spme_offset[proecting_lsp][0];
        }
    }

    if (offset == 0)
    {
        CTC_ERROR_RETURN(sys_goldengate_nh_offset_alloc(lchip,
                                                        table_type, block_size,
                                                        &offset));
    }
    sal_memset(&dsmpls , 0, sizeof(sys_dsmpls_t));

    ret = _sys_goldengate_nh_mpls_encode_dsmpls(lchip, p_label, &dsmpls,
                                          stats_id, CTC_STATS_STATSID_TYPE_NEXTHOP_MPLS_LSP);

    /*SPME label*/
    if (label_type == SYS_NH_MPLS_LABLE_SPME)
    {
        dsmpls.outer_editptr = p_mpls_tunnel->l2edit_offset[proecting_lsp][protection_spme];
        dsmpls.next_editptr_valid = 1;
        dsmpls.outer_editptr_type = 0;

        p_mpls_tunnel->flag  |= SYS_NH_MPLS_TUNNEL_FLAG_EXSIT_SPME;
        p_mpls_tunnel->spme_offset[proecting_lsp][protection_spme] = offset;
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "spme_offset[%d][%d]:%d\n",
                       proecting_lsp, protection_spme,
                       p_mpls_tunnel->spme_offset[proecting_lsp][protection_spme]);
    }

    /*LSP label*/
    if (label_type == SYS_NH_MPLS_LABLE_LSP)
    {
        dsmpls.next_editptr_valid = 1;

        if (CTC_FLAG_ISSET(p_mpls_tunnel->flag, SYS_NH_MPLS_TUNNEL_FLAG_EXSIT_SPME))
        {
            dsmpls.outer_editptr = p_mpls_tunnel->spme_offset[proecting_lsp][protection_spme];
            dsmpls.outer_editptr_type = 1;
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "ExsitSpme, spme_offset[%d][%d]:%d\n",
                           proecting_lsp, protection_spme,
                           p_mpls_tunnel->spme_offset[proecting_lsp][protection_spme]);
        }
        else
        {
            dsmpls.outer_editptr = p_mpls_tunnel->l2edit_offset[proecting_lsp][protection_spme];
            dsmpls.outer_editptr_type = 0;
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Direct l2edit_offset[%d][%d]:%d\n",
                           proecting_lsp, protection_spme,
                           p_mpls_tunnel->l2edit_offset[proecting_lsp][protection_spme]);
        }

        p_mpls_tunnel->lsp_offset[proecting_lsp] = offset;
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "lsp_offset[%d]:%d\n",
                       proecting_lsp, p_mpls_tunnel->lsp_offset[proecting_lsp]);
    }

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "label %d\n", dsmpls.label);
    ret = ret?ret:(sys_goldengate_nh_write_asic_table(lchip, table_type, offset, &dsmpls));

    return ret;

}



STATIC int32
_sys_goldengate_nh_mpls_build_dsnh(uint8 lchip, ctc_mpls_nexthop_param_t* p_nh_mpls_param,
                                   uint32 dsnh_offset,
                                   bool working_path,
                                   sys_nh_info_mpls_t* p_nhdb)
{
    ctc_mpls_nexthop_pop_param_t* p_nh_param_pop = NULL;         /**< mpls (asp) pop nexthop */
    ctc_mpls_nexthop_push_param_t* p_nh_param_push = NULL;      /**< mpls push (asp) nexthop */
    ctc_mpls_nh_op_t               nh_opcode = 0;                /**< MPLS nexthop operation code*/
    sys_l3if_prop_t                l3if_prop;
    sys_nh_db_mpls_tunnel_t* p_mpls_tunnel = NULL;
	sys_nh_param_dsnh_t   dsnh_param;
	uint8 gchip = 0;
    uint8 next_is_spme = 0;
    mac_addr_t mac = {0};

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    sal_memset(&dsnh_param, 0, sizeof(sys_nh_param_dsnh_t));
    sal_memset(&l3if_prop, 0, sizeof(l3if_prop));

    /* L2Edit & L3Edit  */
    if (p_nh_mpls_param->nh_prop == CTC_MPLS_NH_PUSH_TYPE)
    {
        p_nh_param_push  = working_path ? (&p_nh_mpls_param->nh_para.nh_param_push) :
                                          (&p_nh_mpls_param->nh_p_para.nh_p_param_push);

        CTC_ERROR_RETURN(sys_goldengate_nh_lkup_mpls_tunnel(lchip, p_nh_param_push->tunnel_id, &p_mpls_tunnel));

        if (p_nh_param_push->nh_com.opcode == CTC_MPLS_NH_PUSH_OP_L2VPN
            && (p_nh_mpls_param->logic_port_valid ||
        CTC_FLAG_ISSET(p_nh_mpls_param->flag, CTC_MPLS_NH_IS_HVPLS)))
        {
            CTC_LOGIC_PORT_RANGE_CHECK(p_nh_mpls_param->logic_port);
            p_nhdb->dest_logic_port = p_nh_mpls_param->logic_port;
            dsnh_param.logic_port = p_nh_mpls_param->logic_port;
            dsnh_param.hvpls = CTC_FLAG_ISSET(p_nh_mpls_param->flag, CTC_MPLS_NH_IS_HVPLS);

            CTC_SET_FLAG(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_LOGIC_PORT);
            if (!CTC_FLAG_ISSET(p_nh_mpls_param->flag, CTC_MPLS_NH_IS_HVPLS))
            {
                CTC_SET_FLAG(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HORIZON_SPLIT_EN);
            }
            else if (!p_nh_mpls_param->logic_port_valid)
            {
                return CTC_E_VPLS_INVALID_VPLSPORT_ID;
            }
        }

        if (!p_mpls_tunnel)
        {
            return CTC_E_NH_NOT_EXIST_TUNNEL_LABEL;
        }

        if (p_nh_mpls_param->aps_en && (p_nh_param_push->label_num != 1))
        {
            return CTC_E_NH_NO_LABEL;
        }
        dsnh_param.p_vlan_info = &p_nh_param_push->nh_com.vlan_info;
        nh_opcode              = p_nh_param_push->nh_com.opcode;

        if (!CTC_FLAG_ISSET(p_mpls_tunnel->flag, SYS_NH_MPLS_TUNNEL_FLAG_APS))
        {
            l3if_prop.l3if_id      = p_mpls_tunnel->l3ifid;

            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "p_mpls_tunnel->l3ifid:%d\n", p_mpls_tunnel->l3ifid);

            CTC_ERROR_RETURN(sys_goldengate_l3if_get_l3if_info(lchip, 1, &l3if_prop));
            dsnh_param.dest_vlan_ptr  = l3if_prop.vlan_ptr;
            gchip = SYS_MAP_CTC_GPORT_TO_GCHIP(p_mpls_tunnel->gport);
        }

    }
    else if (p_nh_mpls_param->nh_prop == CTC_MPLS_NH_POP_TYPE)
    {
        if (!working_path || p_nh_mpls_param->aps_en)
        {
            return CTC_E_FEATURE_NOT_SUPPORT;
        }

        p_nh_param_pop  = working_path ? (&p_nh_mpls_param->nh_para.nh_param_pop)
            : (&p_nh_mpls_param->nh_p_para.nh_p_param_pop);
        nh_opcode       = p_nh_param_pop->nh_com.opcode;

        gchip = SYS_MAP_CTC_GPORT_TO_GCHIP(p_nh_param_pop->nh_com.oif.gport);
        if (!(CTC_IS_CPU_PORT(p_nh_param_pop->nh_com.oif.gport) || p_nh_param_pop->nh_com.oif.is_l2_port || SYS_IS_DROP_PORT(p_nh_param_pop->nh_com.oif.gport)))
        {
            CTC_ERROR_RETURN(sys_goldengate_l3if_get_l3if_info_with_port_and_vlan(lchip, p_nh_param_pop->nh_com.oif.gport, p_nh_param_pop->nh_com.oif.vid, &l3if_prop));
        }
        dsnh_param.dest_vlan_ptr  = l3if_prop.vlan_ptr;
    }

    switch (nh_opcode)
    {
    case CTC_MPLS_NH_PUSH_OP_NONE:
        dsnh_param.dsnh_type  = SYS_NH_PARAM_DSNH_TYPE_MPLS_PUSH_OP_NONE;
        break;

    case CTC_MPLS_NH_PUSH_OP_ROUTE:
        dsnh_param.dsnh_type  = SYS_NH_PARAM_DSNH_TYPE_MPLS_PUSH_OP_ROUTE;
        break;

    case  CTC_MPLS_NH_PUSH_OP_L2VPN:
        dsnh_param.dsnh_type  = SYS_NH_PARAM_DSNH_TYPE_MPLS_PUSH_OP_L2VPN;
        break;

    case CTC_MPLS_NH_PHP:
        dsnh_param.dsnh_type  = SYS_NH_PARAM_DSNH_TYPE_MPLS_PHP;
        break;

    default:
        break;
    }

    if (CTC_FLAG_ISSET(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_EGRESS_EDIT_MODE)
           && (!p_nh_mpls_param->aps_en))
    {
        if (p_nh_mpls_param->nh_prop == CTC_MPLS_NH_POP_TYPE)
        {
            if (SYS_GCHIP_IS_REMOTE(lchip, gchip))
            {
                p_nhdb->gport  = p_nh_param_pop->nh_com.oif.gport;
                goto end;
            }
        }
        else if (p_nh_mpls_param->nh_prop == CTC_MPLS_NH_PUSH_TYPE)
        {
            if (!CTC_FLAG_ISSET(p_mpls_tunnel->flag, SYS_NH_MPLS_TUNNEL_FLAG_APS))
            {
                if (SYS_GCHIP_IS_REMOTE(lchip, gchip))
                {
                    goto end;
                }
            }
        }
    }

    if (p_nh_mpls_param->nh_prop == CTC_MPLS_NH_PUSH_TYPE)
    {
        if (CTC_FLAG_ISSET(p_mpls_tunnel->flag, SYS_NH_MPLS_TUNNEL_FLAG_IS_SPME))
        {
            next_is_spme = 1;
        }
        else
        {
            if (CTC_FLAG_ISSET(p_mpls_tunnel->flag, SYS_NH_MPLS_TUNNEL_FLAG_APS))
            {
                dsnh_param.lspedit_ptr = p_mpls_tunnel->gport;
                CTC_SET_FLAG(dsnh_param.flag, SYS_NH_PARAM_FLAG_L2EDIT_AS_APS_GROUP);
                CTC_SET_FLAG(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_APS_EN);
                SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "APS(ID)p_mpls_tunnel->gport:%d\n", p_mpls_tunnel->gport);
            }
            else
            {
                dsnh_param.lspedit_ptr = p_mpls_tunnel->lsp_offset[0];
                dsnh_param.l2edit_ptr = p_mpls_tunnel->l2edit_offset[0][0];
                SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "p_mpls_tunnel->lsp_offset:%d\n", p_mpls_tunnel->lsp_offset[0]);
            }
        }

        if (p_nh_param_push->martini_encap_valid
            && (1 == p_nh_param_push->martini_encap_type))
        {
            CTC_ERROR_RETURN(sys_goldengate_nh_mpls_add_cw(lchip, p_nh_param_push->seq_num_index, &p_nhdb->cw_index, TRUE));
            dsnh_param.cw_index = p_nhdb->cw_index;
        }

        /*inner l2 edit*/
        if (next_is_spme && p_nh_param_push->label_num)
        {
            CTC_ERROR_RETURN(_sys_goldengate_nh_mpls_build_inner_dsl2edit(lchip, p_nh_param_push, &dsnh_param, p_nhdb));
        }

        /* _2chips */
        CTC_ERROR_RETURN(_sys_goldengate_nh_mpls_build_pw(lchip, p_nh_param_push, &dsnh_param, p_mpls_tunnel));

        if (next_is_spme)
        {
            if (p_nh_param_push->label_num)
            {
                /*
                    1. innner l2 edit
                    2.l3edit
                */
            }
            else
            {
                /*
                    lsp ptr, position jump mode
                */
                dsnh_param.lspedit_ptr = p_mpls_tunnel->lsp_offset[0];
                SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "p_mpls_tunnel->lsp_offset:%d\n", p_mpls_tunnel->lsp_offset[0]);
            }

            dsnh_param.spme_mode = 1;
        }


        if (dsnh_param.l3edit_ptr != 0)
        {
            CTC_SET_FLAG(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L3EDIT);
        }

        if (working_path)
        {
            p_nhdb->working_path.dsl3edit_offset  = dsnh_param.l3edit_ptr;
        }
        else
        {
            p_nhdb->protection_path->dsl3edit_offset = dsnh_param.l3edit_ptr;
        }
    }
    else if (p_nh_mpls_param->nh_prop == CTC_MPLS_NH_POP_TYPE)
    {

        if(p_nh_param_pop->arp_id)
        {
            sys_nh_db_arp_t *p_arp = NULL;
            sys_nh_info_arp_param_t arp_parm;

            sal_memset(&arp_parm, 0, sizeof(sys_nh_info_arp_param_t));
            arp_parm.p_oif          = &p_nh_param_pop->nh_com.oif;
            arp_parm.nh_entry_type  = SYS_NH_TYPE_MPLS;

            CTC_ERROR_RETURN(sys_goldengate_nh_bind_arp_cb(lchip, &arp_parm, p_nh_param_pop->arp_id));
            CTC_ERROR_RETURN(sys_goldengate_nh_lkup_arp_id(lchip, p_nh_param_pop->arp_id, &p_arp));

            dsnh_param.l2edit_ptr   = p_arp->offset;
            p_nhdb->arp_id          =  p_nh_param_pop->arp_id;
        }
        else
        {
            if (0 == sal_memcmp(p_nh_param_pop->nh_com.mac_sa, mac, sizeof(mac_addr_t)))
            {
                sys_nh_db_dsl2editeth4w_t   dsl2edit_4w;
                sys_nh_db_dsl2editeth4w_t* p_dsl2edit_4w  = &dsl2edit_4w;

                sal_memset(&dsl2edit_4w, 0, sizeof(sys_nh_db_dsl2editeth4w_t));

                dsl2edit_4w.offset = 0;
                dsl2edit_4w.ref_cnt = 0;
                if (p_nh_param_pop->nh_com.oif.vid > 0 && p_nh_param_pop->nh_com.oif.vid <= CTC_MAX_VLAN_ID)
                {
                    dsl2edit_4w.output_vid = p_nh_param_pop->nh_com.oif.vid;
                    dsl2edit_4w.output_vlan_is_svlan = 1;
                }

                sal_memcpy(dsl2edit_4w.mac_da, p_nh_param_pop->nh_com.mac, sizeof(mac_addr_t));
                CTC_ERROR_RETURN(sys_goldengate_nh_add_route_l2edit(lchip, &p_dsl2edit_4w));
                dsnh_param.l2edit_ptr = p_dsl2edit_4w->offset;
                p_nhdb->p_dsl2edit = p_dsl2edit_4w;
            }
            else
            {
                sys_nh_db_dsl2editeth8w_t   dsl2edit_8w;
                sys_nh_db_dsl2editeth8w_t*  p_dsl2edit_8w  = &dsl2edit_8w;
                sal_memset(&dsl2edit_8w, 0, sizeof(sys_nh_db_dsl2editeth8w_t));

                if (p_nh_param_pop->nh_com.oif.vid > 0 && p_nh_param_pop->nh_com.oif.vid <= CTC_MAX_VLAN_ID)
                {
                    dsl2edit_8w.output_vid = p_nh_param_pop->nh_com.oif.vid;
                    dsl2edit_8w.output_vlan_is_svlan = 1;
                }

                sal_memcpy(dsl2edit_8w.mac_da, p_nh_param_pop->nh_com.mac, sizeof(mac_addr_t));
                sal_memcpy(dsl2edit_8w.mac_sa, p_nh_param_pop->nh_com.mac_sa, sizeof(mac_addr_t));
                dsl2edit_8w.update_mac_sa = 1;

                CTC_ERROR_RETURN(sys_goldengate_nh_add_route_l2edit_8w(lchip, &p_dsl2edit_8w));

                dsnh_param.l2edit_ptr = p_dsl2edit_8w->offset;
                p_nhdb->p_dsl2edit = (sys_nh_db_dsl2editeth4w_t*)p_dsl2edit_8w;
                p_nhdb->l2edit_8w = 1;
            }
            CTC_SET_FLAG(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L2EDIT);

        }

        CTC_SET_FLAG(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_MPLS_POP_NH);
        p_nhdb->gport  = p_nh_param_pop->nh_com.oif.gport;
        p_nhdb->l3ifid = l3if_prop.l3if_id;

        if (p_nh_param_pop->ttl_mode == CTC_MPLS_TUNNEL_MODE_PIPE)
        { /*ttl use IP header*/
            CTC_SET_FLAG(dsnh_param.flag, SYS_NH_PARAM_FLAG_USE_TTL_FROM_PKT);
            CTC_SET_FLAG(dsnh_param.flag, SYS_NH_PARAM_FLAG_USE_MAPPED_DSCP);
        }
        else if (p_nh_param_pop->ttl_mode == CTC_MPLS_TUNNEL_MODE_UNIFORM)
        { /*ttl use mpls header*/
            CTC_SET_FLAG(dsnh_param.flag, SYS_NH_PARAM_FLAG_USE_MAPPED_DSCP);
        }
        else  /*short pipe*/
        { /*ttl & dscp use IP header*/
            CTC_SET_FLAG(dsnh_param.flag, SYS_NH_PARAM_FLAG_PHP_SHORT_PIPE);
        }

    }

    /*Write DsNexthop to Asic*/

    if (CTC_FLAG_ISSET(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W))
    {
        dsnh_param.dsnh_offset = working_path ? p_nhdb->hdr.dsfwd_info.dsnh_offset :\
                                                p_nhdb->hdr.dsfwd_info.dsnh_offset + 2;
        CTC_ERROR_RETURN(sys_goldengate_nh_write_entry_dsnh8w(lchip, &dsnh_param));
    }
    else
    {
        dsnh_param.dsnh_offset = working_path ? p_nhdb->hdr.dsfwd_info.dsnh_offset : \
                                               p_nhdb->hdr.dsfwd_info.dsnh_offset + 1;
        CTC_ERROR_RETURN(sys_goldengate_nh_write_entry_dsnh4w(lchip, &dsnh_param));
    }

end:
    if (p_nh_mpls_param->nh_prop == CTC_MPLS_NH_PUSH_TYPE && p_mpls_tunnel)
    {
        p_mpls_tunnel->ref_cnt++;
        if (working_path)
        {
            p_nhdb->working_path.p_mpls_tunnel = p_mpls_tunnel;
            p_nhdb->gport  = p_mpls_tunnel->gport;
            p_nhdb->l3ifid = p_mpls_tunnel->l3ifid;
        }
        else
        {
            p_nhdb->protection_path->p_mpls_tunnel = p_mpls_tunnel;
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_nh_free_mpls_nh_resource(uint8 lchip, sys_nh_info_mpls_t* p_nhinfo, uint8 free_tunnel)
{

    /*2. Delete this mpls nexthop resource */
    if (p_nhinfo->cw_index != 0)
    {
        CTC_ERROR_RETURN(sys_goldengate_nh_mpls_add_cw(lchip, 0, &p_nhinfo->cw_index, FALSE));
    }

    if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L3EDIT))
    {
        uint32 offset = 0;
        sys_dsmpls_t dsmpls;

        offset = p_nhinfo->working_path.dsl3edit_offset;
        CTC_ERROR_RETURN(sys_goldengate_nh_offset_free(lchip, SYS_NH_ENTRY_TYPE_L3EDIT_MPLS, 1, offset));
        sal_memset(&dsmpls, 0, sizeof(sys_dsmpls_t));
        CTC_ERROR_RETURN(sys_goldengate_nh_write_asic_table(lchip, SYS_NH_ENTRY_TYPE_L3EDIT_MPLS,
                                                            offset, &dsmpls));

        if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_APS_EN)
            && !CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_APS_SHARE_DSNH)
        && p_nhinfo->protection_path)
        {

            offset = p_nhinfo->protection_path->dsl3edit_offset;
            CTC_ERROR_RETURN(sys_goldengate_nh_offset_free(lchip, SYS_NH_ENTRY_TYPE_L3EDIT_MPLS, 1, offset));
            sal_memset(&dsmpls, 0, sizeof(sys_dsmpls_t));
            CTC_ERROR_RETURN(sys_goldengate_nh_write_asic_table(lchip, SYS_NH_ENTRY_TYPE_L3EDIT_MPLS,
                                                                offset, &dsmpls));

        }


    }

    if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L2EDIT)
        && CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_MPLS_POP_NH))
    {
        if (p_nhinfo->l2edit_8w)
        {
            sys_goldengate_nh_remove_route_l2edit_8w(lchip, (sys_nh_db_dsl2editeth8w_t*)(p_nhinfo->p_dsl2edit));
            p_nhinfo->l2edit_8w = 0;
        }
        else
        {
            sys_goldengate_nh_remove_route_l2edit(lchip, p_nhinfo->p_dsl2edit);
        }
    }

    if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L2EDI_6W))
    {
        sys_goldengate_nh_remove_l2edit_6w(lchip, p_nhinfo->p_dsl2edit_inner);
        CTC_UNSET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L2EDI_6W);
    }

   CTC_UNSET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L3EDIT);

   if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L2EDIT)
    && CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_MPLS_POP_NH))
   {
       CTC_UNSET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L2EDIT);
       CTC_UNSET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_MPLS_POP_NH);
   }

   if (free_tunnel)
   {
       if (p_nhinfo->working_path.p_mpls_tunnel && p_nhinfo->working_path.p_mpls_tunnel->ref_cnt)
       {
           p_nhinfo->working_path.p_mpls_tunnel->ref_cnt--;
       }

       if (p_nhinfo->protection_path)
       {
           if (p_nhinfo->protection_path->p_mpls_tunnel && p_nhinfo->protection_path->p_mpls_tunnel->ref_cnt)
           {
               p_nhinfo->protection_path->p_mpls_tunnel->ref_cnt--;
           }

           mem_free(p_nhinfo->protection_path);
       }
   }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_nh_mpls_add_dsfwd(uint8 lchip, sys_nh_info_com_t* p_com_db)
{
    sys_nh_param_dsfwd_t dsfwd_param;
    sys_nh_info_mpls_t* p_nhdb = (sys_nh_info_mpls_t*)(p_com_db);
    int ret = 0;

    sal_memset(&dsfwd_param, 0, sizeof(sys_nh_param_dsfwd_t));

    dsfwd_param.is_mcast = FALSE;
    if (CTC_FLAG_ISSET(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W))
    {
        dsfwd_param.nexthop_ext = 1;
    }

    if (CTC_FLAG_ISSET(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_APS_EN))
    {
        sys_goldengate_get_gchip_id(lchip, &dsfwd_param.dest_chipid);
        dsfwd_param.dest_id = p_nhdb->aps_group_id & 0x3FF;
        dsfwd_param.aps_type = CTC_APS_BRIDGE;

    }
    else
    {
        dsfwd_param.dest_chipid = SYS_MAP_CTC_GPORT_TO_GCHIP(p_nhdb->gport);
        dsfwd_param.dest_id = SYS_MAP_CTC_GPORT_TO_DRV_LPORT(p_nhdb->gport);
    }
    if (CTC_FLAG_ISSET(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_LEN_ADJUST_EN))
    {
        sys_goldengate_lkup_adjust_len_index(lchip, p_nhdb->hdr.adjust_len, &dsfwd_param.adjust_len_idx);
    }

    /*Build DsFwd Table*/
    if (!CTC_FLAG_ISSET(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD))
    {
        ret = sys_goldengate_nh_offset_alloc(lchip, SYS_NH_ENTRY_TYPE_FWD, 1,
                                             &(p_nhdb->hdr.dsfwd_info.dsfwd_offset));
    }

    dsfwd_param.dsfwd_offset = p_nhdb->hdr.dsfwd_info.dsfwd_offset;
    dsfwd_param.dsnh_offset = p_nhdb->hdr.dsfwd_info.dsnh_offset;
    dsfwd_param.is_egress_edit =  CTC_FLAG_ISSET(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_EGRESS_EDIT_MODE);

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "DsFwd :: DestChipId = %d, DestId = %d"
                                         "DsNexthop Offset = %d, DsNexthopExt = %d\n",
                                          dsfwd_param.dest_chipid, dsfwd_param.dest_id,
                                          dsfwd_param.dsnh_offset, dsfwd_param.nexthop_ext);
    /*Write table*/
    ret = ret ? ret : sys_goldengate_nh_write_entry_dsfwd(lchip, &dsfwd_param);

    if (ret == CTC_E_NONE)
    {
        CTC_SET_FLAG(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD);
    }

    return ret;
}

STATIC int32
_sys_goldengate_nh_free_mpls_nh_resource_by_offset(uint8 lchip, sys_nh_info_mpls_t* p_nhinfo, bool b_pw_aps_switch)
{

    /*2. Delete this mpls nexthop resource */
    if (p_nhinfo->cw_index != 0)
    {
        CTC_ERROR_RETURN(sys_goldengate_nh_mpls_add_cw(lchip, 0, &p_nhinfo->cw_index, FALSE));
    }

    if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L3EDIT))
    {
        uint32 offset = 0;
        sys_dsmpls_t dsmpls;

        offset = p_nhinfo->working_path.dsl3edit_offset;
        CTC_ERROR_RETURN(sys_goldengate_nh_offset_free(lchip, SYS_NH_ENTRY_TYPE_L3EDIT_MPLS, 1, offset));
        sal_memset(&dsmpls, 0, sizeof(sys_dsmpls_t));
        CTC_ERROR_RETURN(sys_goldengate_nh_write_asic_table(lchip, SYS_NH_ENTRY_TYPE_L3EDIT_MPLS,offset, &dsmpls));

        if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_APS_EN)
            && !CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_APS_SHARE_DSNH)
        && p_nhinfo->protection_path)
        {
            offset = p_nhinfo->protection_path->dsl3edit_offset;
            CTC_ERROR_RETURN(sys_goldengate_nh_offset_free(lchip, SYS_NH_ENTRY_TYPE_L3EDIT_MPLS, 1, offset));
            sal_memset(&dsmpls, 0, sizeof(sys_dsmpls_t));
            CTC_ERROR_RETURN(sys_goldengate_nh_write_asic_table(lchip, SYS_NH_ENTRY_TYPE_L3EDIT_MPLS,
                                                                offset, &dsmpls));
        }

    }

    if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L2EDIT)
        && CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_MPLS_POP_NH))
    {
        CTC_ERROR_RETURN(sys_goldengate_nh_remove_route_l2edit(lchip, p_nhinfo->p_dsl2edit));
    }

    if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L2EDI_6W))
    {
        sys_goldengate_nh_remove_l2edit_6w(lchip, p_nhinfo->p_dsl2edit_inner);
    }

    if (b_pw_aps_switch
        && (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_INT_ALLOC_DSNH)))
    {
        if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W))
        {
            CTC_ERROR_RETURN(sys_goldengate_nh_offset_free(lchip, SYS_NH_ENTRY_TYPE_NEXTHOP_8W,
                              p_nhinfo->hdr.dsnh_entry_num, p_nhinfo->hdr.dsfwd_info.dsnh_offset));
        }
        else
        {
            CTC_ERROR_RETURN(sys_goldengate_nh_offset_free(lchip, SYS_NH_ENTRY_TYPE_NEXTHOP_4W,
                               p_nhinfo->hdr.dsnh_entry_num, p_nhinfo->hdr.dsfwd_info.dsnh_offset));
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_mpls_nh_clear_mpls_nh_resource(uint8 lchip, sys_nh_info_mpls_t* p_nhinfo)
{
    if (p_nhinfo->working_path.p_mpls_tunnel && p_nhinfo->working_path.p_mpls_tunnel->ref_cnt)
    {
        p_nhinfo->working_path.p_mpls_tunnel->ref_cnt--;
    }

    if (p_nhinfo->protection_path)
    {
        if (p_nhinfo->protection_path->p_mpls_tunnel && p_nhinfo->protection_path->p_mpls_tunnel->ref_cnt)
        {
            p_nhinfo->protection_path->p_mpls_tunnel->ref_cnt--;
        }

        mem_free(p_nhinfo->protection_path);
        p_nhinfo->protection_path = NULL;
    }

    p_nhinfo->hdr.nh_entry_flags &= (SYS_NH_INFO_FLAG_HAVE_DSFWD|\
                                     SYS_NH_INFO_FLAG_USE_DSNH8W|\
                                     SYS_NH_INFO_FLAG_HAVE_INT_ALLOC_DSNH\
                                     | SYS_NH_INFO_FLAG_EGRESS_EDIT_MODE);

    return CTC_E_NONE;
}

int32
sys_goldengate_nh_create_mpls_cb(uint8 lchip, sys_nh_param_com_t* p_com_nh_para,
                                sys_nh_info_com_t* p_com_db)
{
    sys_nh_param_mpls_t* p_nh_param;
    sys_nh_info_mpls_t* p_nhdb;
    int32 ret = 0;


    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    CTC_PTR_VALID_CHECK(p_com_nh_para);
    CTC_PTR_VALID_CHECK(p_com_db);

    CTC_EQUAL_CHECK(SYS_NH_TYPE_MPLS, p_com_nh_para->hdr.nh_param_type);
    p_nh_param = (sys_nh_param_mpls_t*)(p_com_nh_para);
    p_nhdb = (sys_nh_info_mpls_t*)(p_com_db);

    CTC_PTR_VALID_CHECK(p_nh_param->p_mpls_nh_param);
    if (p_nh_param->p_mpls_nh_param->nh_prop == CTC_MPLS_NH_PUSH_TYPE)
    {
        SYS_NH_CHECK_MPLS_EXP_VALUE(p_nh_param->p_mpls_nh_param->nh_para.nh_param_push.push_label[0].exp);
        SYS_NH_CHECK_MPLS_EXP_VALUE(p_nh_param->p_mpls_nh_param->nh_para.nh_param_push.push_label[1].exp);
        SYS_NH_CHECK_MPLS_EXP_VALUE(p_nh_param->p_mpls_nh_param->nh_p_para.nh_p_param_push.push_label[0].exp);
        SYS_NH_CHECK_MPLS_EXP_VALUE(p_nh_param->p_mpls_nh_param->nh_p_para.nh_p_param_push.push_label[1].exp);

        if(p_nh_param->p_mpls_nh_param->aps_en)
        {
            SYS_NH_CHECK_MPLS_LABVEL_VALUE(p_nh_param->p_mpls_nh_param->nh_para.nh_param_push.push_label[0].label);
            SYS_NH_CHECK_MPLS_LABVEL_VALUE(p_nh_param->p_mpls_nh_param->nh_para.nh_param_push.push_label[1].label);
            SYS_NH_CHECK_MPLS_LABVEL_VALUE(p_nh_param->p_mpls_nh_param->nh_p_para.nh_p_param_push.push_label[0].label);
            SYS_NH_CHECK_MPLS_LABVEL_VALUE(p_nh_param->p_mpls_nh_param->nh_p_para.nh_p_param_push.push_label[1].label);
        }
    }
    CTC_PTR_VALID_CHECK(p_nh_param->p_mpls_nh_param);

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "\n%s()\n", __FUNCTION__);
    p_nhdb->hdr.nh_entry_type = SYS_NH_TYPE_MPLS;


    /*Create unresolved ipuc nh*/
    if (CTC_FLAG_ISSET(p_nh_param->p_mpls_nh_param->flag, CTC_MPLS_NH_IS_UNRSV))
    {
        sys_nh_param_special_t nh_para_spec;

        sal_memset(&nh_para_spec, 0, sizeof(nh_para_spec));
        nh_para_spec.hdr.nh_param_type = SYS_NH_TYPE_UNROV;
        nh_para_spec.hdr.is_internal_nh = TRUE;
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Create unresolved mpls nexthop\n");

        CTC_ERROR_RETURN(sys_goldengate_nh_create_special_cb(lchip, (
                                                                sys_nh_param_com_t*)(&nh_para_spec), (sys_nh_info_com_t*)(p_nhdb)));
        CTC_SET_FLAG(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_UNROV);
        return CTC_E_NONE;
    }


    if (p_nh_param->p_mpls_nh_param->aps_en)
    {
        bool protect_en = FALSE;
        CTC_ERROR_RETURN(sys_goldengate_aps_get_protection(lchip, p_nh_param->p_mpls_nh_param->aps_bridge_group_id, &protect_en));

        /*working path -- PW layer*/
        ret = _sys_goldengate_nh_mpls_build_dsnh(lchip, p_nh_param->p_mpls_nh_param,  p_nh_param->p_mpls_nh_param->dsnh_offset, TRUE, p_nhdb);
        if (ret != CTC_E_NONE)
        {
            goto error_proc;
        }

        /*protection path -- PW layer*/
        p_nhdb->protection_path  = mem_malloc(MEM_NEXTHOP_MODULE, sizeof(sys_nh_info_mpls_edit_info_t));
        if (!p_nhdb->protection_path)
        {
            ret = CTC_E_NO_MEMORY;
            goto error_proc;
        }

        sal_memset(p_nhdb->protection_path, 0, sizeof(sys_nh_info_mpls_edit_info_t));

        ret = _sys_goldengate_nh_mpls_build_dsnh(lchip, p_nh_param->p_mpls_nh_param,  p_nh_param->p_mpls_nh_param->dsnh_offset, FALSE, p_nhdb);
        if (ret != CTC_E_NONE)
        {
            goto error_proc;
        }

        p_nhdb->aps_group_id = p_nh_param->p_mpls_nh_param->aps_bridge_group_id;
        CTC_SET_FLAG(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_APS_EN);
        CTC_UNSET_FLAG(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_APS_SHARE_DSNH);
        ret = ret ? ret : sys_goldengate_aps_set_share_nh(lchip, p_nhdb->aps_group_id, FALSE);

        if (ret != CTC_E_NONE)
        {
            goto error_proc;
        }
    }
    else
    {
        ret = _sys_goldengate_nh_mpls_build_dsnh(lchip, p_nh_param->p_mpls_nh_param,  p_nh_param->p_mpls_nh_param->dsnh_offset, TRUE, p_nhdb);

        if (CTC_FLAG_ISSET(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_APS_EN))
        {
            p_nhdb->aps_group_id = p_nhdb->gport;
            CTC_SET_FLAG(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_APS_SHARE_DSNH);
            ret = ret ? ret : sys_goldengate_aps_set_share_nh(lchip, p_nhdb->aps_group_id, TRUE);
        }

        if (ret != CTC_E_NONE)
        {
            goto error_proc;
        }
    }
	if (p_nh_param->p_mpls_nh_param->adjust_length != 0)
    {
        CTC_SET_FLAG(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_LEN_ADJUST_EN);
        p_nhdb->hdr.adjust_len = p_nh_param->p_mpls_nh_param->adjust_length ;
    }
	/*3.Create Dwfwd*/
    if(!p_nh_param->hdr.have_dsfwd)
    {
      return CTC_E_NONE;
    }
    ret = _sys_goldengate_nh_mpls_add_dsfwd(lchip, p_com_db);

    if (ret != CTC_E_NONE)
    {
        sys_goldengate_nh_offset_free(lchip, SYS_NH_ENTRY_TYPE_FWD, 1,
                                      p_nhdb->hdr.dsfwd_info.dsfwd_offset);
    }
    else
    {
        return CTC_E_NONE;
    }

error_proc:
     _sys_goldengate_nh_free_mpls_nh_resource(lchip, p_nhdb, 0);


    return ret;
}

/**
 @brief Callback function of delete unicast ip nexthop

 @param[in] p_com_db, pointer used to store nexthop data

 @return CTC_E_XXX
 */
int32
sys_goldengate_nh_delete_mpls_cb(uint8 lchip, sys_nh_info_com_t* p_data, uint32* p_nhid)
{
    sys_nh_info_mpls_t* p_nhinfo;
    sys_nh_ref_list_node_t* p_ref_node, * p_tmp_node;
    sys_nh_param_dsfwd_t dsfwd_param;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    CTC_PTR_VALID_CHECK(p_data);
    CTC_EQUAL_CHECK(SYS_NH_TYPE_MPLS, p_data->hdr.nh_entry_type);
    p_nhinfo = (sys_nh_info_mpls_t*)(p_data);

    CTC_ERROR_RETURN(sys_goldengate_nh_update_ecmp_member(lchip, *p_nhid, SYS_NH_CHANGE_TYPE_NH_DELETE));

    /*1. Notify all reference nh, ipuc will be deleted*/
    p_ref_node = p_nhinfo->hdr.p_ref_nh_list;

    while (p_ref_node)
    {
        /*Remove from db*/
        p_tmp_node = p_ref_node;
        p_ref_node = p_ref_node->p_next;
        mem_free(p_tmp_node);
    }

    p_nhinfo->hdr.p_ref_nh_list = NULL;

    if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD))
    {
        /*2. Delete this mpls nexthop*/
        sal_memset(&dsfwd_param, 0, sizeof(sys_nh_param_dsfwd_t));
        dsfwd_param.dsfwd_offset = p_nhinfo->hdr.dsfwd_info.dsfwd_offset;
        dsfwd_param.drop_pkt = 1;
        /*Write table*/
        CTC_ERROR_RETURN(sys_goldengate_nh_write_entry_dsfwd(lchip, &dsfwd_param));
        /*Free DsFwd offset*/
        CTC_ERROR_RETURN(sys_goldengate_nh_offset_free(lchip, \
        SYS_NH_ENTRY_TYPE_FWD, 1, p_nhinfo->hdr.dsfwd_info.dsfwd_offset));
    }
      if (NULL != p_nhinfo->updateAd)
      {
             sys_nh_info_dsnh_t dsnh_info;
             dsnh_info.drop_pkt =1;
             p_nhinfo->updateAd(lchip, p_nhinfo->data, &dsnh_info);
     }

    CTC_ERROR_RETURN(_sys_goldengate_nh_free_mpls_nh_resource(lchip, p_nhinfo, 1));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_nh_update_mpls_fwd_to_spec(uint8 lchip, sys_nh_param_mpls_t* p_nhpara,
                                           sys_nh_info_mpls_t* p_nhinfo)
{

    sys_nh_param_special_t nh_para_spec;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    sal_memset(&nh_para_spec, 0, sizeof(sys_nh_param_special_t));

    nh_para_spec.hdr.have_dsfwd= TRUE;
    nh_para_spec.hdr.nh_param_type = SYS_NH_TYPE_UNROV;
    nh_para_spec.hdr.is_internal_nh = TRUE;

    /*1.2 update dsfwd to unrov nh's dsfwd, write dsfwd*/
    CTC_ERROR_RETURN(sys_goldengate_nh_create_special_cb(lchip,
                         (sys_nh_param_com_t*)(&nh_para_spec), (sys_nh_info_com_t*)(p_nhinfo)));
    CTC_SET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_UNROV);

    CTC_ERROR_RETURN(_sys_goldengate_nh_free_mpls_nh_resource(lchip, p_nhinfo, 1));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_mpls_nh_alloc_dsnh_offset(uint8 lchip, sys_nh_param_mpls_t* p_nhpara,
                                          sys_nh_info_mpls_t* p_nhinfo)
{
    uint32    dsnh_offset = 0;
    ctc_vlan_egress_edit_info_t* p_vlan_info = NULL;

    p_vlan_info = &p_nhpara->p_mpls_nh_param->nh_para.nh_param_push.nh_com.vlan_info;

    dsnh_offset = p_nhpara->p_mpls_nh_param->dsnh_offset;
    if ((p_nhpara->p_mpls_nh_param->nh_prop == CTC_MPLS_NH_PUSH_TYPE) &&
        (p_nhpara->p_mpls_nh_param->nh_para.nh_param_push.nh_com.opcode == CTC_MPLS_NH_PUSH_OP_L2VPN)
        && (p_vlan_info != NULL)
        && ((CTC_FLAG_ISSET(p_vlan_info->edit_flag, CTC_VLAN_EGRESS_EDIT_OUTPUT_SVLAN_TPID_VALID))
             ||((CTC_FLAG_ISSET(p_vlan_info->edit_flag, CTC_VLAN_EGRESS_EDIT_OUPUT_CVID_VALID))
             ||(CTC_FLAG_ISSET(p_vlan_info->edit_flag, CTC_VLAN_EGRESS_EDIT_OUPUT_SVID_VALID)))))
    {
        CTC_SET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W);
    }
    else
    {
        CTC_UNSET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W);
    }

    if (p_nhpara->p_mpls_nh_param->aps_en)
    {
        p_nhinfo->hdr.dsnh_entry_num  = 2;
    }
    else
    {
        p_nhinfo->hdr.dsnh_entry_num  = 1;
    }

    if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W))
    {
        CTC_ERROR_RETURN(sys_goldengate_nh_offset_alloc(lchip, SYS_NH_ENTRY_TYPE_NEXTHOP_8W, p_nhinfo->hdr.dsnh_entry_num, &dsnh_offset));
    }
    else
    {
        CTC_ERROR_RETURN(sys_goldengate_nh_offset_alloc(lchip, SYS_NH_ENTRY_TYPE_NEXTHOP_4W, p_nhinfo->hdr.dsnh_entry_num, &dsnh_offset));

    }

    CTC_SET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_INT_ALLOC_DSNH);

    p_nhinfo->hdr.dsfwd_info.dsnh_offset = dsnh_offset;


    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_mpls_nh_free_dsnh_offset(uint8 lchip, sys_nh_info_mpls_t* p_nhinfo)
{
    uint32    dsnh_offset = 0;
    dsnh_offset = p_nhinfo->hdr.dsfwd_info.dsnh_offset;

    if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W))
    {
        CTC_ERROR_RETURN(sys_goldengate_nh_offset_free(lchip, SYS_NH_ENTRY_TYPE_NEXTHOP_8W, p_nhinfo->hdr.dsnh_entry_num, dsnh_offset));
    }
    else
    {
        CTC_ERROR_RETURN(sys_goldengate_nh_offset_free(lchip, SYS_NH_ENTRY_TYPE_NEXTHOP_4W, p_nhinfo->hdr.dsnh_entry_num, dsnh_offset));
    }

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_nh_update_mpls_fwd_attr(uint8 lchip, sys_nh_param_mpls_t* p_nhpara, sys_nh_info_mpls_t* p_nhinfo)
{
    int32 ret = CTC_E_NONE;
    sys_nh_info_mpls_t nhinfo_tmp;
    sys_nh_info_mpls_edit_info_t protection_path;  /*when pw aps enbale*/

    bool b_old_pw_aps = FALSE;
    bool b_new_pw_aps = FALSE;
    bool b_pw_aps_switch = FALSE;



    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    sal_memcpy(&nhinfo_tmp, p_nhinfo, sizeof(sys_nh_info_mpls_t));
    sal_memcpy(&nhinfo_tmp.hdr.dsfwd_info, &p_nhinfo->hdr.dsfwd_info, sizeof(sys_nh_info_dsfwd_t));



    b_old_pw_aps = (p_nhinfo->protection_path)? TRUE : FALSE;
    b_new_pw_aps = (p_nhpara->p_mpls_nh_param->aps_en)? TRUE : FALSE;
    b_pw_aps_switch = (b_old_pw_aps != b_new_pw_aps)? TRUE : FALSE;


    if (b_pw_aps_switch
        &&(!CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_INT_ALLOC_DSNH))
        && !CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_EGRESS_EDIT_MODE))
    {
        return CTC_E_INVALID_PARAM;
    }


    if(b_old_pw_aps)
    {
       sal_memcpy(&protection_path, p_nhinfo->protection_path, sizeof(sys_nh_info_mpls_edit_info_t));
       nhinfo_tmp.protection_path = &protection_path;
    }

    CTC_ERROR_RETURN(_sys_goldengate_mpls_nh_clear_mpls_nh_resource(lchip, p_nhinfo));

    if (b_pw_aps_switch &&
		!CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_EGRESS_EDIT_MODE))
    {
        CTC_ERROR_GOTO(_sys_goldengate_mpls_nh_alloc_dsnh_offset(lchip, p_nhpara, p_nhinfo), ret, error_proc1);
    }

    p_nhpara->hdr.have_dsfwd = CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD)
                                || (NULL != p_nhinfo->hdr.p_ref_nh_list);

    CTC_ERROR_GOTO(sys_goldengate_nh_create_mpls_cb(lchip, (sys_nh_param_com_t*)p_nhpara, (sys_nh_info_com_t*)p_nhinfo), ret, error_proc2);
    CTC_UNSET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_UNROV);

    CTC_ERROR_RETURN(_sys_goldengate_nh_free_mpls_nh_resource_by_offset(lchip, &nhinfo_tmp,b_pw_aps_switch));


    return CTC_E_NONE;

error_proc2:
    if (b_pw_aps_switch &&
		!CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_EGRESS_EDIT_MODE))
    {
        _sys_goldengate_mpls_nh_free_dsnh_offset(lchip, p_nhinfo);
    }

error_proc1:
    if (nhinfo_tmp.working_path.p_mpls_tunnel)
    {
        nhinfo_tmp.working_path.p_mpls_tunnel->ref_cnt++;
    }
    sal_memcpy(p_nhinfo, &nhinfo_tmp,  sizeof(sys_nh_info_mpls_t));
    sal_memcpy(&p_nhinfo->hdr.dsfwd_info, &nhinfo_tmp.hdr.dsfwd_info, sizeof(sys_nh_info_dsfwd_t));
    if (b_old_pw_aps)
    {
        p_nhinfo->protection_path  = mem_malloc(MEM_NEXTHOP_MODULE, sizeof(sys_nh_info_mpls_edit_info_t));
        if (p_nhinfo->protection_path)
        {
            sal_memcpy(p_nhinfo->protection_path, &protection_path, sizeof(sys_nh_info_mpls_edit_info_t));
            p_nhinfo->protection_path->p_mpls_tunnel->ref_cnt++;
        }
    }
    return ret;

}


/**
 @brief Callback function used to update ipuc nexthop

 @param[in] p_nh_ptr, pointer of ipuc nexthop DB

 @param[in] p_para, member information

 @return CTC_E_XXX
 */
int32
sys_goldengate_nh_update_mpls_cb(uint8 lchip, sys_nh_info_com_t* p_nh_db,
                                sys_nh_param_com_t* p_para)
{
    sys_nh_info_mpls_t* p_nh_info;
    sys_nh_param_mpls_t* p_nh_para;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    CTC_PTR_VALID_CHECK(p_para);
    CTC_PTR_VALID_CHECK(p_nh_db);
    CTC_EQUAL_CHECK(SYS_NH_TYPE_MPLS, p_para->hdr.nh_param_type);
    CTC_EQUAL_CHECK(SYS_NH_TYPE_MPLS, p_nh_db->hdr.nh_entry_type);
    p_nh_info = (sys_nh_info_mpls_t*)(p_nh_db);
    p_nh_para = (sys_nh_param_mpls_t*)(p_para);

    switch (p_nh_para->hdr.change_type)
    {
    case SYS_NH_CHANGE_TYPE_FWD_TO_UNROV:
        CTC_ERROR_RETURN(_sys_goldengate_nh_update_mpls_fwd_to_spec(lchip, p_nh_para, p_nh_info));
		break;

    case SYS_NH_CHANGE_TYPE_UPDATE_FWD_ATTR:
        CTC_ERROR_RETURN(_sys_goldengate_nh_update_mpls_fwd_attr(lchip, p_nh_para, p_nh_info));
	    break;

    case SYS_NH_CHANGE_TYPE_UNROV_TO_FWD:
        if (!CTC_FLAG_ISSET(p_nh_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_UNROV))
        {
            return CTC_E_NH_ISNT_UNROV;
        }
        CTC_ERROR_RETURN(_sys_goldengate_nh_update_mpls_fwd_attr(lchip, p_nh_para, p_nh_info));

        break;
   case SYS_NH_CHANGE_TYPE_ADD_DYNTBL:
        {
  		  if (CTC_FLAG_ISSET(p_nh_db->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD))
            {
                return CTC_E_NONE;
            }
            CTC_ERROR_RETURN(_sys_goldengate_nh_mpls_add_dsfwd(lchip,  p_nh_db));
        }
    	break;
    default:
        return CTC_E_INVALID_PARAM;
    }

	if (p_nh_para->hdr.change_type != SYS_NH_CHANGE_TYPE_ADD_DYNTBL
	 	&&	NULL != p_nh_info->updateAd)
    {
        sys_nh_info_dsnh_t        dsnh_info;
        sal_memset(&dsnh_info, 0, sizeof(sys_nh_info_dsnh_t));
        sys_goldengate_nh_mapping_to_nhinfo(lchip, (sys_nh_info_com_t*)p_nh_info, &dsnh_info);
        p_nh_info->updateAd(lchip, p_nh_info->data, &dsnh_info);
    }
    if (p_nh_info->hdr.p_ref_nh_list)
    {
        CTC_ERROR_RETURN(sys_goldengate_nh_update_ecmp_member(lchip, p_nh_para->hdr.nhid, p_nh_para->hdr.change_type));
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_mpls_nh_update_oam_en(uint8 lchip, uint32 nhid, sys_nh_update_oam_info_t *p_oam_info)
{
    sys_nh_info_com_t* p_nh_com_info;
    sys_nh_info_mpls_t* p_nh_mpls_info;
    DsL3EditMpls3W_m  ds_edit_mpls;
    uint32 cmd = 0;
    uint8 gchip = 0;
    uint32 offset = 0;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    CTC_ERROR_RETURN(sys_goldengate_nh_get_nhinfo_by_nhid(lchip, nhid, (sys_nh_info_com_t**)&p_nh_com_info));

    p_nh_mpls_info = (sys_nh_info_mpls_t*)p_nh_com_info;
    if (!p_nh_mpls_info)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    if (SYS_NH_TYPE_MPLS != p_nh_com_info->hdr.nh_entry_type)
    {
        return CTC_E_NH_INVALID_NHID;
    }

    gchip = CTC_MAP_GPORT_TO_GCHIP(p_nh_mpls_info->gport);

    if (!CTC_FLAG_ISSET(p_nh_mpls_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_APS_EN)
        && CTC_FLAG_ISSET(p_nh_mpls_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_EGRESS_EDIT_MODE))
    {
        if (SYS_GCHIP_IS_REMOTE(lchip, gchip))
        {
            return CTC_E_NONE;;
        }
    }

    if (CTC_FLAG_ISSET(p_nh_mpls_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L3EDIT))
    { /*PW nexthop*/
         SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Update pw lm ,  innerEditPtr = %d\n",
                        p_nh_mpls_info->working_path.dsl3edit_offset);

         cmd = DRV_IOR(DsL3EditMpls3W_t, DRV_ENTRY_FLAG);
         CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_nh_mpls_info->working_path.dsl3edit_offset,
                                    cmd, &ds_edit_mpls));

        if (!p_oam_info->is_protection_path)
        {
          offset = p_nh_mpls_info->working_path.dsl3edit_offset;
        }
        if (p_oam_info->is_protection_path
            && !CTC_FLAG_ISSET(p_nh_mpls_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_APS_SHARE_DSNH)
            &&  CTC_FLAG_ISSET(p_nh_mpls_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_APS_EN))
        {
            offset = p_nh_mpls_info->protection_path->dsl3edit_offset;
        }
    }
    else
    {    /*tunnel with aps,only need consider LSP, Because SPME Nexthop will be regarded as LSP Nexthp.*/
        sys_nh_db_mpls_tunnel_t* p_mpls_tunnel;

        p_mpls_tunnel =  p_nh_mpls_info->working_path.p_mpls_tunnel;
        if (!p_mpls_tunnel)
        {
            return CTC_E_NOT_EXIST;
        }

        if (!CTC_FLAG_ISSET(p_mpls_tunnel->flag, SYS_NH_MPLS_TUNNEL_FLAG_APS)
		  	|| !p_oam_info->is_protection_path)
        {
           offset = p_mpls_tunnel->lsp_offset[0];
       	}
	    else
	   	{
		   offset = p_mpls_tunnel->lsp_offset[1];
		}
	}


    if(offset == 0)
    {  /*invalid nexthop */
         return CTC_E_INVALID_PTR;
    }

    cmd = DRV_IOR(DsL3EditMpls3W_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, offset, cmd, &ds_edit_mpls));
    if (p_oam_info->lm_en)
    {
        SetDsL3EditMpls3W(V, mplsOamIndex_f, &ds_edit_mpls, p_oam_info->oam_mep_index);
    }
    else
    {
        SetDsL3EditMpls3W(V, mplsOamIndex_f, &ds_edit_mpls, 0);
    }

    if (p_oam_info->lock_en)
    {
        SetDsL3EditMpls3W(V, discardType_f,&ds_edit_mpls, 1);
        SetDsL3EditMpls3W(V, discard_f,    &ds_edit_mpls, 1);
    }
    else
    {
        SetDsL3EditMpls3W(V, discardType_f,&ds_edit_mpls, 0);
        SetDsL3EditMpls3W(V, discard_f,    &ds_edit_mpls, 0);
    }
    cmd = DRV_IOW(DsL3EditMpls3W_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, offset, cmd, &ds_edit_mpls));

    return CTC_E_NONE;
}

STATIC int32
sys_goldengate_mpls_nh_tunnel_label_free_resource(uint8 lchip, bool is_2level_p_path,
                                                  sys_nh_db_mpls_tunnel_t* p_mpls_tunnel)
{
    uint8 lsp_loop_end             = 0;
    uint8 smpe_cnt                 = 0;
    uint8 protect_lsp               = 0;
    uint8 protect_spme              = 0;
    uint32 offset                  = 0;
    sys_dsmpls_t dsmpls;
    uint8 table_type               = 0;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    if (CTC_FLAG_ISSET(p_mpls_tunnel->flag, SYS_NH_MPLS_TUNNEL_FLAG_TWO_LAYER_APS))
    {
        protect_lsp = is_2level_p_path;
        lsp_loop_end = protect_lsp + 1;
    }
    else
    {
        protect_lsp = 0;
        lsp_loop_end = CTC_FLAG_ISSET(p_mpls_tunnel->flag, SYS_NH_MPLS_TUNNEL_FLAG_APS) ? 2 : 1;
    }

    for (protect_lsp = 0; protect_lsp < lsp_loop_end; protect_lsp++ )
    {
        /*Free LSP*/
        offset =  p_mpls_tunnel->lsp_offset[protect_lsp];
        if (offset )
        {
            if(CTC_FLAG_ISSET(p_mpls_tunnel->flag, SYS_NH_MPLS_TUNNEL_FLAG_IS_SPME))
            {
                table_type = SYS_NH_ENTRY_TYPE_L3EDIT_SPME;
            }
            else
            {
                table_type = SYS_NH_ENTRY_TYPE_L3EDIT_MPLS;
            }
            CTC_ERROR_RETURN(sys_goldengate_nh_offset_free(lchip, table_type, 1, offset));
            p_mpls_tunnel->lsp_offset[protect_lsp] = 0;
            sal_memset(&dsmpls, 0, sizeof(sys_dsmpls_t));
            CTC_ERROR_RETURN(sys_goldengate_nh_write_asic_table(lchip, table_type,
                                                               offset, &dsmpls));
        }

        smpe_cnt = CTC_FLAG_ISSET(p_mpls_tunnel->flag, SYS_NH_MPLS_TUNNEL_FLAG_TWO_LAYER_APS) ? 2:1;
        for (protect_spme = 0; protect_spme < smpe_cnt; protect_spme++ )
        {
            /*Free SPME*/
            offset = p_mpls_tunnel->spme_offset[protect_lsp][protect_spme];
            if (offset && CTC_FLAG_ISSET(p_mpls_tunnel->flag, SYS_NH_MPLS_TUNNEL_FLAG_EXSIT_SPME))
            {
                CTC_ERROR_RETURN(sys_goldengate_nh_offset_free(lchip, SYS_NH_ENTRY_TYPE_L3EDIT_SPME, 1, offset));
                p_mpls_tunnel->spme_offset[protect_lsp][protect_spme] = 0;
                sal_memset(&dsmpls, 0, sizeof(sys_dsmpls_t));
                CTC_ERROR_RETURN(sys_goldengate_nh_write_asic_table(lchip, SYS_NH_ENTRY_TYPE_L3EDIT_SPME,
                                                                    offset, &dsmpls));
            }
            /*free l2edti*/
            if (p_mpls_tunnel->p_dsl2edit_4w[protect_lsp][protect_spme] && (!p_mpls_tunnel->arp_id[protect_lsp][protect_spme]))
            {
                if (CTC_FLAG_ISSET(p_mpls_tunnel->flag, SYS_NH_MPLS_TUNNEL_FLAG_IS_L2EDIT8W))
                {
                    sys_goldengate_nh_remove_route_l2edit_8w(lchip, (sys_nh_db_dsl2editeth8w_t*)(p_mpls_tunnel->p_dsl2edit_4w[protect_lsp][protect_spme]));
                    p_mpls_tunnel->p_dsl2edit_4w[protect_lsp][protect_spme] = NULL;
                    CTC_UNSET_FLAG(p_mpls_tunnel->flag, SYS_NH_MPLS_TUNNEL_FLAG_IS_L2EDIT8W);
                }
                else
                {
                    sys_goldengate_nh_remove_route_l2edit(lchip, p_mpls_tunnel->p_dsl2edit_4w[protect_lsp][protect_spme]);
                    p_mpls_tunnel->p_dsl2edit_4w[protect_lsp][protect_spme] = NULL;
                }
            }
            /*free arp*/
            if (p_mpls_tunnel->arp_id[protect_lsp][protect_spme])
            {
                p_mpls_tunnel->arp_id[protect_lsp][protect_spme] = 0;
            }
        }
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_mpls_nh_update_aps(uint8 lchip, uint8 loop,
                                  ctc_mpls_nexthop_tunnel_param_t* p_nh_param,
                                  sys_nh_db_mpls_tunnel_t* p_mpls_tunnel)
{
    uint16 update_aps_id = 0;
    uint8 update_is_protect_path = 0;
    uint8 update_ape_en = 0;
    uint16 edit_ptr = 0;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    if (CTC_FLAG_ISSET(p_nh_param->aps_flag, CTC_NH_MPLS_APS_2_LAYER_EN))
    {
        if (!CTC_FLAG_ISSET(p_nh_param->aps_flag, CTC_NH_MPLS_APS_2_LAYER_EN_PROTECTION))
        {   /*working lsp*/
            CTC_ERROR_RETURN(
            sys_goldengate_aps_get_next_group(lchip, p_nh_param->aps_bridge_group_id, &update_aps_id, TRUE));
            p_mpls_tunnel->flag  |= SYS_NH_MPLS_TUNNEL_FLAG_WORKING;
            edit_ptr = p_mpls_tunnel->lsp_offset[0];
            CTC_ERROR_RETURN(sys_goldengate_aps_set_spme_en(lchip, update_aps_id, TRUE));
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Update Lsp working path, apsId:%d, editPtr:%d\n",
                           update_aps_id, edit_ptr);
        }
        else
        {   /*protecting lsp*/
            CTC_ERROR_RETURN(
            sys_goldengate_aps_get_next_group(lchip, p_nh_param->aps_bridge_group_id, &update_aps_id, FALSE));
            p_mpls_tunnel->flag  |= SYS_NH_MPLS_TUNNEL_FLAG_PROTECTION;
            edit_ptr = p_mpls_tunnel->lsp_offset[1];
            CTC_ERROR_RETURN(sys_goldengate_aps_set_spme_en(lchip, update_aps_id, TRUE));
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Update Lsp protecting path, apsId:%d, editPtr:%d\n",
                           update_aps_id, edit_ptr);
        }

        update_ape_en = 1;
        update_is_protect_path = loop;

    }
    else if (CTC_FLAG_ISSET(p_nh_param->aps_flag, CTC_NH_MPLS_APS_EN))
    {
        update_ape_en = 1;
        update_aps_id = p_nh_param->aps_bridge_group_id;
        if (loop == 0)
        {
            update_is_protect_path = 0;
            edit_ptr = p_mpls_tunnel->lsp_offset[0];
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Update Lsp working path, apsId:%d, editPtr:%d\n",
                           update_aps_id, edit_ptr);
        }
        else
        {
            update_is_protect_path = 1;
            edit_ptr = p_mpls_tunnel->lsp_offset[1];
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Update Lsp protecting path, apsId:%d, editPtr:%d\n",
                           update_aps_id, edit_ptr);
        }
    }
    else
    {
        update_ape_en = 0;
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Not need update aps!!!!!!\n");
    }

    /* call aps function to write dsapsbridge l2edit */
    if (update_ape_en)
    {
        CTC_ERROR_RETURN(sys_goldengate_aps_update_tunnel(lchip, update_aps_id, edit_ptr, update_is_protect_path));
    }

  return CTC_E_NONE;

}

/*
ctc_mpls_nexthop_tunnel_param_t :
   if tunnel label have two layer label,taking LSP label & SPME label as a case;
         and if tunnel label have only one layer lable,taking LSP label as a case

    if CTC_NH_MPLS_APS_EN & CTC_NH_MPLS_APS_2_LAYER_EN are set, aps_bridge_group_id is 2-level(Lsp) protection group id
                                    SPME label(w)  (nh_param)
              LSP label(w)------>                                (CTC_NH_MPLS_APS_2_LAYER_EN_PROTECTION isn't set)
                                    SPME label(p)   (nh_p_param)


                                    SPME label(w)   (nh_param)
              LSP label(P)------>                                (CTC_NH_MPLS_APS_2_LAYER_EN_PROTECTION is set)
                                    SPME label(p)   (nh_p_param)

    if CTC_NH_MPLS_APS_EN only is set, there are two case:
     1)aps_bridge_group_id is 2-level(Lsp) protection group id.

              LSP label(w)------> SPME label  (-->nh_param)
              LSP label(P)------> SPME label  (-->nh_p_param)

     2)aps_bridge_group_id is 2-level(Lsp) protection group id.
              LSP label(w) (-->nh_param)
              LSP label(P) (-->nh_p_param)
*/
int32
sys_goldengate_mpls_nh_update_tunnel_label_fwd_attr(uint8 lchip, uint8 is_add,
                                           uint16 tunnel_id,
                                           ctc_mpls_nexthop_tunnel_param_t* p_nh_param)
{
    ctc_mpls_nexthop_tunnel_info_t* p_tunnel_info = NULL;
    sys_nh_db_mpls_tunnel_t* p_mpls_tunnel = NULL;
    int32     ret = 0;
    uint8    loop = 0, loop_cnt = 0;
    sys_l3if_prop_t l3if_prop;
    uint8 protect_lsp = 0,protection_spme;
    bool is_2level_p_path = 0;
    bool free_memory = 0;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    sal_memset(&l3if_prop, 0, sizeof(l3if_prop));

    /* check exp */
    SYS_NH_CHECK_MPLS_EXP_VALUE(p_nh_param->nh_param.tunnel_label[0].exp);
    SYS_NH_CHECK_MPLS_EXP_VALUE(p_nh_param->nh_param.tunnel_label[1].exp);
    SYS_NH_CHECK_MPLS_LABVEL_VALUE(p_nh_param->nh_param.tunnel_label[0].label);
    SYS_NH_CHECK_MPLS_LABVEL_VALUE(p_nh_param->nh_param.tunnel_label[1].label);


    if(p_nh_param->aps_flag)
    {
        SYS_NH_CHECK_MPLS_EXP_VALUE(p_nh_param->nh_p_param.tunnel_label[0].exp);
        SYS_NH_CHECK_MPLS_EXP_VALUE(p_nh_param->nh_p_param.tunnel_label[1].exp);
        SYS_NH_CHECK_MPLS_LABVEL_VALUE(p_nh_param->nh_p_param.tunnel_label[0].label);
        SYS_NH_CHECK_MPLS_LABVEL_VALUE(p_nh_param->nh_p_param.tunnel_label[1].label);
    }

    if (CTC_FLAG_ISSET(p_nh_param->aps_flag, CTC_NH_MPLS_APS_EN))
    {
        SYS_APS_GROUP_ID_CHECK(p_nh_param->aps_bridge_group_id);

        if (p_nh_param->nh_p_param.stats_id)
        {
            uint16 stats_ptr = 0;
            CTC_ERROR_RETURN(sys_goldengate_stats_get_statsptr(lchip, p_nh_param->nh_p_param.stats_id, CTC_STATS_STATSID_TYPE_NEXTHOP_MPLS_LSP, &stats_ptr));
        }
    }


    if (CTC_FLAG_ISSET(p_nh_param->aps_flag, CTC_NH_MPLS_APS_EN)
        && (p_nh_param->nh_param.label_num == 0))
    {
        return CTC_E_NH_NO_LABEL;
    }

    if (CTC_FLAG_ISSET(p_nh_param->aps_flag, CTC_NH_MPLS_APS_EN)
        && (p_nh_param->nh_p_param.label_num == 0))
    {
      sal_memcpy(&p_nh_param->nh_p_param,&p_nh_param->nh_param,sizeof(ctc_mpls_nexthop_tunnel_info_t));
    }

    /*check stats id*/
    if (p_nh_param->nh_param.stats_id)
    {
        uint16 stats_ptr = 0;
        CTC_ERROR_RETURN(sys_goldengate_stats_get_statsptr(lchip, p_nh_param->nh_param.stats_id, CTC_STATS_STATSID_TYPE_NEXTHOP_MPLS_LSP, &stats_ptr));
    }

    if (!CTC_FLAG_ISSET(p_nh_param->aps_flag, CTC_NH_MPLS_APS_EN))
    {
        if(!(CTC_IS_CPU_PORT(p_nh_param->nh_param.oif.gport) || p_nh_param->nh_param.oif.is_l2_port || SYS_IS_DROP_PORT(p_nh_param->nh_param.oif.gport)))
        {
            CTC_ERROR_RETURN(sys_goldengate_l3if_get_l3if_info_with_port_and_vlan(lchip, p_nh_param->nh_param.oif.gport, p_nh_param->nh_param.oif.vid, &l3if_prop));
        }
    }
    else
    {
        bool protect_en = FALSE;
        CTC_ERROR_RETURN(sys_goldengate_aps_get_protection(lchip, p_nh_param->aps_bridge_group_id, &protect_en));
    }

    CTC_ERROR_RETURN(sys_goldengate_nh_lkup_mpls_tunnel(lchip, tunnel_id, &p_mpls_tunnel));

    if (is_add)
    {
        if (p_mpls_tunnel)
        {
            if (!CTC_FLAG_ISSET(p_mpls_tunnel->flag, SYS_NH_MPLS_TUNNEL_FLAG_TWO_LAYER_APS))
            {
                return CTC_E_NH_EXIST;
            }
            else if ((CTC_FLAG_ISSET(p_mpls_tunnel->flag, SYS_NH_MPLS_TUNNEL_FLAG_WORKING) == !CTC_FLAG_ISSET(p_nh_param->aps_flag, CTC_NH_MPLS_APS_2_LAYER_EN_PROTECTION))
                || (CTC_FLAG_ISSET(p_mpls_tunnel->flag, SYS_NH_MPLS_TUNNEL_FLAG_PROTECTION) == CTC_FLAG_ISSET(p_nh_param->aps_flag, CTC_NH_MPLS_APS_2_LAYER_EN_PROTECTION)))
            {
                return CTC_E_NH_EXIST;
            }
        }
        else
        {
            p_mpls_tunnel  = (sys_nh_db_mpls_tunnel_t*)mem_malloc(MEM_NEXTHOP_MODULE, sizeof(sys_nh_db_mpls_tunnel_t));
            if (!p_mpls_tunnel)
            {
                return CTC_E_NO_MEMORY;
            }

            sal_memset(p_mpls_tunnel, 0, sizeof(sys_nh_db_mpls_tunnel_t));
            free_memory = 1;

        }
    }
    else
    {
         /*p_mpls_tunnel->flag = 0;*/
    }

    p_mpls_tunnel->tunnel_id = tunnel_id;

    if (CTC_FLAG_ISSET(p_nh_param->aps_flag, CTC_NH_MPLS_APS_EN))
    {
        CTC_SET_FLAG(p_mpls_tunnel->flag, SYS_NH_MPLS_TUNNEL_FLAG_APS);
    }
    else
    {
        CTC_UNSET_FLAG(p_mpls_tunnel->flag, SYS_NH_MPLS_TUNNEL_FLAG_APS);
    }

    if (CTC_FLAG_ISSET(p_nh_param->aps_flag, CTC_NH_MPLS_APS_2_LAYER_EN))
    {
        CTC_SET_FLAG(p_mpls_tunnel->flag, SYS_NH_MPLS_TUNNEL_FLAG_TWO_LAYER_APS);
        is_2level_p_path = (CTC_FLAG_ISSET(p_nh_param->aps_flag, CTC_NH_MPLS_APS_2_LAYER_EN_PROTECTION));
    }
    else
    {
        CTC_UNSET_FLAG(p_mpls_tunnel->flag, SYS_NH_MPLS_TUNNEL_FLAG_TWO_LAYER_APS);
    }

    if (p_nh_param->nh_param.is_spme)
    {
        p_mpls_tunnel->flag  |= SYS_NH_MPLS_TUNNEL_FLAG_IS_SPME;
    }

    p_tunnel_info = &p_nh_param->nh_param;
    loop_cnt = (CTC_FLAG_ISSET(p_nh_param->aps_flag, CTC_NH_MPLS_APS_EN)) ? 2 : 1;
    loop = 0;

     /*mpls nexthop must use egress chip edit in EDIT_MODE = 1 & 2*/
    if ((SYS_NH_EDIT_MODE() != SYS_NH_IGS_CHIP_EDIT_MODE) &&
        (!CTC_FLAG_ISSET(p_nh_param->aps_flag, CTC_NH_MPLS_APS_EN) ))
    {
        uint8 gchip = 0;
        gchip = CTC_MAP_GPORT_TO_GCHIP(p_nh_param->nh_param.oif.gport);

        if (SYS_GCHIP_IS_REMOTE(lchip, gchip))
        {
            loop_cnt = 0;
        }
    }

    while (loop < loop_cnt)
    {
         if (CTC_FLAG_ISSET(p_nh_param->aps_flag, CTC_NH_MPLS_APS_EN) )
         {
             if (CTC_FLAG_ISSET(p_nh_param->aps_flag, CTC_NH_MPLS_APS_2_LAYER_EN_PROTECTION))
             {
                 protect_lsp = 1;
             }

             if (!CTC_FLAG_ISSET(p_nh_param->aps_flag, CTC_NH_MPLS_APS_2_LAYER_EN) && loop)
             {
                 protect_lsp = 1;
             }
         }

         if (CTC_FLAG_ISSET(p_nh_param->aps_flag, CTC_NH_MPLS_APS_2_LAYER_EN))
         {
             protection_spme = loop;
         }
         else
         {
             protection_spme = 0;
         }

         /*1. Build l2edit(ARP), output mac ===========*/
        if(!is_add)
        {
            /*free l2edti*/
            if (p_mpls_tunnel->p_dsl2edit_4w[protect_lsp][protection_spme] && (!p_mpls_tunnel->arp_id[protect_lsp][protection_spme]))
            {
                if (CTC_FLAG_ISSET(p_mpls_tunnel->flag, SYS_NH_MPLS_TUNNEL_FLAG_IS_L2EDIT8W))
                {
                    sys_goldengate_nh_remove_route_l2edit_8w(lchip, (sys_nh_db_dsl2editeth8w_t*)(p_mpls_tunnel->p_dsl2edit_4w[protect_lsp][protection_spme]));
                    p_mpls_tunnel->p_dsl2edit_4w[protect_lsp][protection_spme] = NULL;
                    CTC_UNSET_FLAG(p_mpls_tunnel->flag, SYS_NH_MPLS_TUNNEL_FLAG_IS_L2EDIT8W);
                }
                else
                {
                    sys_goldengate_nh_remove_route_l2edit(lchip, p_mpls_tunnel->p_dsl2edit_4w[protect_lsp][protection_spme]);
                    p_mpls_tunnel->p_dsl2edit_4w[protect_lsp][protection_spme] = NULL;
                }
            }
        }
        CTC_ERROR_GOTO(_sys_goldengate_nh_mpls_build_dsl2edit(lchip, protection_spme,
                                                               protect_lsp,
                                                               p_tunnel_info,
                                                               p_mpls_tunnel),
                                                    ret, error_proc);
         /*2. Build spme ===========*/
         if (p_tunnel_info->label_num == 2)
         {
             CTC_ERROR_GOTO(_sys_goldengate_nh_mpls_build_tunnel(lchip, protection_spme,
                                                                 protect_lsp,
                                                                 SYS_NH_MPLS_LABLE_SPME,
                                                                 p_tunnel_info,
                                                                 p_mpls_tunnel),
                                                   ret, error_proc);
         }

         /*3. Build lsp ===========*/
         if (p_tunnel_info->label_num >= 1)
         {
            if (p_tunnel_info->is_spme)
            {

            }
            else
            {

            }
             CTC_ERROR_GOTO(_sys_goldengate_nh_mpls_build_tunnel(lchip, protection_spme,
                         protect_lsp,
                         SYS_NH_MPLS_LABLE_LSP,
                         p_tunnel_info,
                         p_mpls_tunnel),
                     ret, error_proc);
         }

         /*3. update aps ===========*/
         if (CTC_FLAG_ISSET(p_nh_param->aps_flag, CTC_NH_MPLS_APS_EN) )
         {
             CTC_ERROR_GOTO(sys_goldengate_mpls_nh_update_aps(lchip, loop,
                                                              p_nh_param,
                                                              p_mpls_tunnel),
                                                  ret, error_proc);
         }
         p_tunnel_info = &p_nh_param->nh_p_param;
         loop++;
    };


    if (CTC_FLAG_ISSET(p_nh_param->aps_flag, CTC_NH_MPLS_APS_EN))
    {
        p_mpls_tunnel->gport = p_nh_param->aps_bridge_group_id;
    }
    else
    {
        p_mpls_tunnel->gport = p_nh_param->nh_param.oif.gport;
        p_mpls_tunnel->l3ifid = l3if_prop.l3if_id;
    }

    p_mpls_tunnel->label_num = p_nh_param->nh_param.label_num;

    CTC_ERROR_GOTO((sys_goldengate_nh_add_mpls_tunnel(lchip, tunnel_id, p_mpls_tunnel)),
                   ret, error_proc);


    return CTC_E_NONE;

  error_proc:
      sys_goldengate_mpls_nh_tunnel_label_free_resource(lchip, is_2level_p_path, p_mpls_tunnel);
      if (free_memory)
      {
          sys_goldengate_nh_remove_mpls_tunnel(lchip, p_mpls_tunnel->tunnel_id);
          mem_free(p_mpls_tunnel);
      }
      return ret;

  }



int32
sys_goldengate_mpls_nh_update_tunnel_mac_fwd_attr(uint8 lchip, bool is_add,
                                           uint16 tunnel_id,
                                           ctc_mpls_nexthop_tunnel_param_t* p_nh_param)
{
    ctc_mpls_nexthop_tunnel_info_t* p_tunnel_info = &p_nh_param->nh_param;
    sys_nh_db_mpls_tunnel_t* p_mpls_tunnel = NULL;
    int32     ret = 0;
    sys_l3if_prop_t l3if_prop;
    bool is_2level_p_path = 0;
    bool free_memory = 0;
    uint8 do_nh_edit = 1;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    sal_memset(&l3if_prop, 0, sizeof(l3if_prop));

    /* check exp */

    if (!(CTC_IS_CPU_PORT(p_nh_param->nh_param.oif.gport) || p_nh_param->nh_param.oif.is_l2_port || SYS_IS_DROP_PORT(p_nh_param->nh_param.oif.gport)))
    {
        CTC_ERROR_RETURN(sys_goldengate_l3if_get_l3if_info_with_port_and_vlan(lchip, p_nh_param->nh_param.oif.gport, p_nh_param->nh_param.oif.vid, &l3if_prop));
    }
    CTC_ERROR_RETURN(sys_goldengate_nh_lkup_mpls_tunnel(lchip, tunnel_id, &p_mpls_tunnel));

    if (is_add)
    {
        if (p_mpls_tunnel)
        {
            return CTC_E_NH_EXIST;

        }
        else
        {
            p_mpls_tunnel  = (sys_nh_db_mpls_tunnel_t*)mem_malloc(MEM_NEXTHOP_MODULE, sizeof(sys_nh_db_mpls_tunnel_t));

            if (!p_mpls_tunnel)
            {
                return CTC_E_NO_MEMORY;
            }

            sal_memset(p_mpls_tunnel, 0, sizeof(sys_nh_db_mpls_tunnel_t));
            free_memory = 1;

        }
        p_mpls_tunnel->tunnel_id = tunnel_id;
    }



    if ((SYS_NH_EDIT_MODE() != SYS_NH_IGS_CHIP_EDIT_MODE) &&
        (!CTC_FLAG_ISSET(p_nh_param->aps_flag, CTC_NH_MPLS_APS_EN) ))
    {
        uint8 gchip = 0;
        gchip = CTC_MAP_GPORT_TO_GCHIP(p_nh_param->nh_param.oif.gport);

        if (SYS_GCHIP_IS_REMOTE(lchip, gchip))
        {
            do_nh_edit = 0;
        }
    }

    if (do_nh_edit)
    {
        /*1. Build l2edit(ARP), output mac ===========*/
        if (!is_add)
        {
            /*free l2edti*/
            if (p_mpls_tunnel->p_dsl2edit_4w[0][0] && (!p_mpls_tunnel->arp_id[0][0]))
            {
                sys_goldengate_nh_remove_route_l2edit(lchip, p_mpls_tunnel->p_dsl2edit_4w[0][0]);
            }
        }

        CTC_ERROR_GOTO(_sys_goldengate_nh_mpls_build_dsl2edit(lchip, 0, 0,
                                                              p_tunnel_info,
                                                              p_mpls_tunnel),
                                                          ret,
                                                          error_proc);
    }

    p_mpls_tunnel->gport = p_nh_param->nh_param.oif.gport;
    p_mpls_tunnel->l3ifid = l3if_prop.l3if_id;

    CTC_ERROR_GOTO((sys_goldengate_nh_add_mpls_tunnel(lchip, tunnel_id, p_mpls_tunnel)),
                   ret, error_proc);

    return CTC_E_NONE;

  error_proc:
      sys_goldengate_mpls_nh_tunnel_label_free_resource(lchip, is_2level_p_path, p_mpls_tunnel);
      if (free_memory)
      {
          sys_goldengate_nh_remove_mpls_tunnel(lchip, p_mpls_tunnel->tunnel_id);
          mem_free(p_mpls_tunnel);
      }
      return ret;

}

int32
sys_goldengate_mpls_nh_create_tunnel_label(uint8 lchip, uint16 tunnel_id,
                                           ctc_mpls_nexthop_tunnel_param_t* p_nh_param)
{
    if ((p_nh_param->nh_param.label_num == 0)
        && (!CTC_FLAG_ISSET(p_nh_param->aps_flag, CTC_NH_MPLS_APS_EN) ||
    (CTC_FLAG_ISSET(p_nh_param->aps_flag, CTC_NH_MPLS_APS_EN) && (p_nh_param->nh_p_param.label_num == 0))))
    {
        CTC_ERROR_RETURN(sys_goldengate_mpls_nh_update_tunnel_mac_fwd_attr(lchip, TRUE, tunnel_id, p_nh_param));
    }
    else
    {
        CTC_ERROR_RETURN(sys_goldengate_mpls_nh_update_tunnel_label_fwd_attr(lchip, TRUE, tunnel_id, p_nh_param));
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_mpls_nh_remove_tunnel_label(uint8 lchip, uint16 tunnel_id)
{

    sys_nh_db_mpls_tunnel_t* p_mpls_tunnel;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    CTC_ERROR_RETURN(sys_goldengate_nh_lkup_mpls_tunnel(lchip, tunnel_id, &p_mpls_tunnel));

    if (p_mpls_tunnel && p_mpls_tunnel->ref_cnt != 0)
    {
        return CTC_E_NH_EXIST_VC_LABEL;
    }

    if (!p_mpls_tunnel)
    {
        return CTC_E_NH_NOT_EXIST;
    }

    if (CTC_FLAG_ISSET(p_mpls_tunnel->flag, SYS_NH_MPLS_TUNNEL_FLAG_TWO_LAYER_APS))
    {
        CTC_ERROR_RETURN(sys_goldengate_mpls_nh_tunnel_label_free_resource(lchip, 1, p_mpls_tunnel));
    }

    CTC_ERROR_RETURN(sys_goldengate_mpls_nh_tunnel_label_free_resource(lchip,  0, p_mpls_tunnel));

    CTC_ERROR_RETURN(sys_goldengate_nh_remove_mpls_tunnel(lchip, p_mpls_tunnel->tunnel_id));
    mem_free(p_mpls_tunnel);

    return CTC_E_NONE;
}


int32
sys_goldengate_mpls_nh_update_tunnel_label(uint8 lchip, uint16 tunnel_id, ctc_mpls_nexthop_tunnel_param_t* p_tunnel_param)
{
      sys_nh_db_mpls_tunnel_t* p_mpls_tunnel = NULL;
      sys_nh_db_mpls_tunnel_t mpls_tunnel;

      sal_memset(&mpls_tunnel, 0, sizeof(sys_nh_db_mpls_tunnel_t));

      CTC_ERROR_RETURN(sys_goldengate_nh_lkup_mpls_tunnel(lchip, tunnel_id, &p_mpls_tunnel));

    if (!p_mpls_tunnel)
    {
        return CTC_E_NH_NOT_EXIST;
    }

    if ((CTC_FLAG_ISSET(p_mpls_tunnel->flag, SYS_NH_MPLS_TUNNEL_FLAG_TWO_LAYER_APS) !=
            CTC_FLAG_ISSET(p_tunnel_param->aps_flag, CTC_NH_MPLS_APS_2_LAYER_EN))
        || (CTC_FLAG_ISSET(p_mpls_tunnel->flag, SYS_NH_MPLS_TUNNEL_FLAG_APS) !=
            CTC_FLAG_ISSET(p_tunnel_param->aps_flag, CTC_NH_MPLS_APS_EN))
        || (CTC_FLAG_ISSET(p_mpls_tunnel->flag, SYS_NH_MPLS_TUNNEL_FLAG_APS)
            && p_tunnel_param->aps_bridge_group_id != p_mpls_tunnel->gport)
        || p_tunnel_param->nh_param.label_num != p_mpls_tunnel->label_num)
    {
        return CTC_E_INVALID_PARAM;
    }

    if (CTC_FLAG_ISSET(p_mpls_tunnel->flag, SYS_NH_MPLS_TUNNEL_FLAG_IS_SPME) !=
            p_tunnel_param->nh_param.is_spme)
    {
        return CTC_E_INVALID_PARAM;
    }

	if((p_tunnel_param->nh_param.label_num == 0)
        && (!CTC_FLAG_ISSET(p_tunnel_param->aps_flag, CTC_NH_MPLS_APS_EN)
          ||(CTC_FLAG_ISSET(p_tunnel_param->aps_flag, CTC_NH_MPLS_APS_EN)
               && (p_tunnel_param->nh_p_param.label_num == 0))))
    {

		CTC_ERROR_RETURN(sys_goldengate_mpls_nh_update_tunnel_mac_fwd_attr(lchip, FALSE,
                                                                           tunnel_id,
                                                                           p_tunnel_param));
    }
    else
    {
		CTC_ERROR_RETURN(sys_goldengate_mpls_nh_update_tunnel_label_fwd_attr(lchip, FALSE,
                                                                           tunnel_id,
                                                                           p_tunnel_param));
	}

      return CTC_E_NONE;

  }


int32
sys_goldengate_mpls_nh_swap_tunnel(uint8 lchip, uint16 old_tunnel_id, uint16 new_tunnel_id)
{
    sys_nh_db_mpls_tunnel_t* p_old_mpls_tunnel;
    sys_nh_db_mpls_tunnel_t* p_new_mpls_tunnel;
    sys_nh_db_mpls_tunnel_t tmp_mpls_tunnel;


    CTC_ERROR_RETURN(sys_goldengate_nh_lkup_mpls_tunnel(lchip, old_tunnel_id, &p_old_mpls_tunnel));
    if (!p_old_mpls_tunnel)
    {
        return CTC_E_NH_NOT_EXIST;
    }

    CTC_ERROR_RETURN(sys_goldengate_nh_lkup_mpls_tunnel(lchip, new_tunnel_id, &p_new_mpls_tunnel));
    if (!p_new_mpls_tunnel)
    {
        return CTC_E_NH_NOT_EXIST;
    }

    sal_memset(&tmp_mpls_tunnel, 0, sizeof(sys_nh_db_mpls_tunnel_t));

     /*swap tunnel soft-table*/
    sal_memcpy(&tmp_mpls_tunnel,p_old_mpls_tunnel, sizeof(sys_nh_db_mpls_tunnel_t));

    sal_memcpy(p_old_mpls_tunnel,p_new_mpls_tunnel, sizeof(sys_nh_db_mpls_tunnel_t));
    p_old_mpls_tunnel->tunnel_id = old_tunnel_id;

    sal_memcpy(p_new_mpls_tunnel,&tmp_mpls_tunnel, sizeof(sys_nh_db_mpls_tunnel_t));
    p_new_mpls_tunnel->tunnel_id = new_tunnel_id;

    p_new_mpls_tunnel->ref_cnt = p_old_mpls_tunnel->ref_cnt;
    p_old_mpls_tunnel->ref_cnt = tmp_mpls_tunnel.ref_cnt;


    p_new_mpls_tunnel->stats_ptr = p_old_mpls_tunnel->stats_ptr;
    p_old_mpls_tunnel->stats_ptr = tmp_mpls_tunnel.stats_ptr;
    p_new_mpls_tunnel->p_stats_ptr = p_old_mpls_tunnel->p_stats_ptr;
    p_old_mpls_tunnel->p_stats_ptr = tmp_mpls_tunnel.p_stats_ptr;


    return CTC_E_NONE;
}



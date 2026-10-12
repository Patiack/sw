/**
 @file sys_greatbelt_nexthop_mpls.c

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
#include "ctc_linklist.h"
#include "ctc_mpls.h"
#include "sys_greatbelt_chip.h"
#include "sys_greatbelt_nexthop_api.h"
#include "sys_greatbelt_nexthop.h"
#include "sys_greatbelt_vlan.h"
#include "sys_greatbelt_aps.h"
#include "sys_greatbelt_scl.h"
#include "sys_greatbelt_mpls.h"
#include "sys_greatbelt_l3if.h"
#include "sys_greatbelt_register.h"
#include "sys_greatbelt_stats.h"
#include "greatbelt/include/drv_io.h"
#include "greatbelt/include/drv_tbl_reg.h"

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
#define SYS_NH_LENGTH_ADJUST_MIDDLE_VALUE    8

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
_sys_greatbelt_nh_mpls_build_dsl3edit(uint8 lchip, ctc_mpls_nexthop_push_param_t* p_nh_param_push,
                                      sys_nh_param_dsnh_t* dsnh)
{
    ds_l3_edit_mpls4_w_t  dsl3edit4w;
    uint8 label_num = 0;
    uint32 ttl_index = 0;

    sal_memset(&dsl3edit4w, 0, sizeof(ds_l3_edit_mpls4_w_t));

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    label_num = p_nh_param_push->label_num;

    if ((label_num > 2) || (dsnh->hvpls && (label_num > 1)))
    {
        return CTC_E_NOT_SUPPORT;
    }

    if (label_num == 0)
    {
        return CTC_E_NONE;
    }

    dsl3edit4w.l3_rewrite_type = SYS_NH_L3EDIT_TYPE_MPLS_4W;
    dsl3edit4w.ds_type   = SYS_NH_DS_TYPE_L2EDIT;
    /*control word*/
    if (p_nh_param_push->martini_encap_valid)
    { /*GB use fixed CW*/
        if (0 == p_nh_param_push->martini_encap_type)
        {
            dsl3edit4w.martini_encap_valid = 1;
            dsl3edit4w.martini_encap_type = 0;
            if (p_nh_param_push->nh_com.opcode == CTC_MPLS_NH_PUSH_OP_L2VPN)
            { /*etree leaf*/
                dsl3edit4w.use_src_leaf = 1;
            }
            else
            { /*normal CW*/
                dsl3edit4w.use_src_leaf = 0;
            }
        }
        else if (1 == p_nh_param_push->martini_encap_type)
        {
            /*
            mpls_cw = (ds_l3_edit_mpls8_w->derive_exp0 << 31)
                          | (ds_l3_edit_mpls8_w->exp0 << 28)
                          | ((ds_l3_edit_mpls8_w->etree_leaf_en) ? (dest_leaf << 27) :
                            ((IS_BIT_SET(ds_l3_edit_mpls8_w->label0, 1) << 27)))    //label0[1]

                          | (ds_l3_edit_mpls8_w->oam_en0 << 26)
                          | (ds_l3_edit_mpls8_w->entropy_label_en0 << 25)

                          | (IS_BIT_SET(ds_l3_edit_mpls8_w->label0, 0) << 24)

                          | (ds_l3_edit_mpls8_w->ttl_index0 << 20)
                          | ((ds_l3_edit_mpls8_w->label0 & 0xc) << 16)              // label0[3:2]
                          | (ds_l3_edit_mpls8_w->map_ttl0 << 17)
                          | ((ds_l3_edit_mpls8_w->mcast_label0) << 16)
                          | ((ds_l3_edit_mpls8_w->label0 >> 4 ) & 0xFFFF);          // label0[11:4]
            */
            dsl3edit4w.martini_encap_valid  = 1;
            dsl3edit4w.martini_encap_type   = 1;

            dsl3edit4w.derive_exp0          = CTC_IS_BIT_SET(p_nh_param_push->seq_num_index, 31);
            dsl3edit4w.exp0                 = (p_nh_param_push->seq_num_index >> 28) & 0x7;
            dsl3edit4w.oam_en0              = CTC_IS_BIT_SET(p_nh_param_push->seq_num_index, 26);
            dsl3edit4w.entropy_label_en0    = CTC_IS_BIT_SET(p_nh_param_push->seq_num_index, 25);
            dsl3edit4w.ttl_index0           = (p_nh_param_push->seq_num_index >> 20) & 0xF;
            dsl3edit4w.map_ttl0             = CTC_IS_BIT_SET(p_nh_param_push->seq_num_index, 17);
            dsl3edit4w.mcast_label0         = CTC_IS_BIT_SET(p_nh_param_push->seq_num_index, 16);
            dsl3edit4w.label0               = ((p_nh_param_push->seq_num_index & 0xFFFF) << 4)
                                                | ((p_nh_param_push->seq_num_index >> 16) & 0xC)
                                                | CTC_IS_BIT_SET(p_nh_param_push->seq_num_index, 27)
                                                | CTC_IS_BIT_SET(p_nh_param_push->seq_num_index, 24);
        }

    }

    if (p_nh_param_push->nh_com.opcode == CTC_MPLS_NH_PUSH_OP_L2VPN)
    { /*dscp use L2header's cos or L3header's dscp */
        dsl3edit4w.src_dscp_type = 1;   /*if set to 1, L2header's cos*/
    }

    if (1 != p_nh_param_push->martini_encap_type)
    {
        /*label 0*/
        switch (p_nh_param_push->push_label[0].exp_type)
        {
        case CTC_NH_EXP_SELECT_ASSIGN:
            dsl3edit4w.derive_exp0 = 0;
            dsl3edit4w.exp0  = p_nh_param_push->push_label[0].exp;
            break;

        case CTC_NH_EXP_SELECT_MAP: /*mapped exp */
            dsl3edit4w.derive_exp0 = 1;
            dsl3edit4w.exp0  = SYS_NH_MPLS_LABEL_EXP_SELECT_MAP;
            break;

        case CTC_NH_EXP_SELECT_PACKET: /*input packet exp  */
            dsl3edit4w.derive_exp0 = 1;
            dsl3edit4w.exp0  = SYS_NH_MPLS_LABEL_EXP_SELECT_PACKET;
            break;

        default:
            break;
        }

        if (CTC_FLAG_ISSET(p_nh_param_push->push_label[0].lable_flag, CTC_MPLS_NH_LABEL_MAP_TTL))
        {
            dsl3edit4w.map_ttl0 = 1;
        }

        ttl_index  = 0;   /*p_nh_param->tunnel_label[1].ttl; */
        CTC_ERROR_RETURN(sys_greatbelt_lkup_ttl_index(lchip, p_nh_param_push->push_label[0].ttl, &ttl_index));

        dsl3edit4w.ttl_index0  = ttl_index;
        dsl3edit4w.label0       = (p_nh_param_push->push_label[0].label & 0xFFFFF);

        /*label 1 -- logic_port_check1*/
        if (dsnh->hvpls)
        {
            dsl3edit4w.label_valid1 = 0;
            dsl3edit4w.mcast_label1   = 1; /* logic_port_check1 */
            dsl3edit4w.label1 = dsnh->logic_port & 0x3FFF;
        }
        else if(2 == label_num)
        {
            dsl3edit4w.label_valid1 = 1;

            switch (p_nh_param_push->push_label[1].exp_type)
            {
            case CTC_NH_EXP_SELECT_ASSIGN:
                dsl3edit4w.derive_exp1 = 0;
                dsl3edit4w.exp1  = p_nh_param_push->push_label[1].exp;
                break;

            case CTC_NH_EXP_SELECT_MAP: /*mapped exp */
                dsl3edit4w.derive_exp1 = 1;
                dsl3edit4w.exp1  = SYS_NH_MPLS_LABEL_EXP_SELECT_MAP;
                break;

            case CTC_NH_EXP_SELECT_PACKET: /*input packet exp  */
                dsl3edit4w.derive_exp1 = 1;
                dsl3edit4w.exp1  = SYS_NH_MPLS_LABEL_EXP_SELECT_PACKET;
                break;

            default:
                break;
            }

            if (CTC_FLAG_ISSET(p_nh_param_push->push_label[1].lable_flag, CTC_MPLS_NH_LABEL_MAP_TTL))
            {
                dsl3edit4w.map_ttl1 = 1;
            }

            ttl_index  = 0;   /*p_nh_param->tunnel_label[1].ttl; */
            CTC_ERROR_RETURN(sys_greatbelt_lkup_ttl_index(lchip, p_nh_param_push->push_label[1].ttl, &ttl_index));

            dsl3edit4w.ttl_index1  = ttl_index;
            dsl3edit4w.label1       = (p_nh_param_push->push_label[1].label & 0xFFFFF);

        }

    }
    else
    {
        dsl3edit4w.label_valid1 = 1;

        /*label 0*/
        switch (p_nh_param_push->push_label[0].exp_type)
        {
        case CTC_NH_EXP_SELECT_ASSIGN:
            dsl3edit4w.derive_exp1 = 0;
            dsl3edit4w.exp1  = p_nh_param_push->push_label[0].exp;
            break;

        case CTC_NH_EXP_SELECT_MAP: /*mapped exp */
            dsl3edit4w.derive_exp1 = 1;
            dsl3edit4w.exp1  = SYS_NH_MPLS_LABEL_EXP_SELECT_MAP;
            break;

        case CTC_NH_EXP_SELECT_PACKET: /*input packet exp  */
            dsl3edit4w.derive_exp1 = 1;
            dsl3edit4w.exp1  = SYS_NH_MPLS_LABEL_EXP_SELECT_PACKET;
            break;

        default:
            break;
        }

        if (CTC_FLAG_ISSET(p_nh_param_push->push_label[0].lable_flag, CTC_MPLS_NH_LABEL_MAP_TTL))
        {
            dsl3edit4w.map_ttl1 = 1;
        }

        ttl_index  = 0;   /*p_nh_param->tunnel_label[1].ttl; */
        CTC_ERROR_RETURN(sys_greatbelt_lkup_ttl_index(lchip, p_nh_param_push->push_label[0].ttl, &ttl_index));

        dsl3edit4w.ttl_index1  = ttl_index;
        dsl3edit4w.label1       = (p_nh_param_push->push_label[0].label & 0xFFFFF);

    }

    dsl3edit4w.ds_type    = 2;

    /*1. Allocate new dsl3edit mpls entry*/
    CTC_ERROR_RETURN(sys_greatbelt_nh_offset_alloc(lchip,
                                                   SYS_NH_ENTRY_TYPE_L3EDIT_MPLS_4W, 1,
                                                   &dsnh->l3edit_ptr));

    /*2. Write HW table*/
    CTC_ERROR_RETURN(sys_greatbelt_nh_write_asic_table(lchip,
                                                       SYS_NH_ENTRY_TYPE_L3EDIT_MPLS_4W,
                                                       dsnh->l3edit_ptr, &dsl3edit4w));
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_nh_mpls_build_l2edit(uint8 lchip, ctc_mpls_nexthop_tunnel_info_t* p_nh_param, uint8 label_num, ds_l2_edit_eth8_w_t* p_ds_l2edit, uint16* stats_ptr)
{

    ds_l3_edit_mpls8_w_t  ds_l3edit_mpls_8w;
    uint32 ttl_index = 0;
    uint8 extra_header_type = 0;
    sys_l3if_prop_t l3if_prop;

    sal_memset(&ds_l3edit_mpls_8w, 0, sizeof(ds_l3_edit_mpls8_w_t));
    *stats_ptr = 0;

    p_ds_l2edit->l2_rewrite_type  = SYS_NH_L2EDIT_TYPE_ETH_8W;
    p_ds_l2edit->mac_da47_32  = p_nh_param->mac[0] << 8 | p_nh_param->mac[1];
    p_ds_l2edit->mac_da29_0   = ((p_nh_param->mac[2] & 0x3F) << 24) | p_nh_param->mac[3] << 16 | p_nh_param->mac[4] << 8 | p_nh_param->mac[5];
    p_ds_l2edit->mac_da31_30  = (p_nh_param->mac[2] >> 6);

    CTC_ERROR_RETURN(sys_greatbelt_l3if_get_l3if_info_with_port_and_vlan(lchip, p_nh_param->oif.gport, p_nh_param->oif.vid, &l3if_prop));

    if((l3if_prop.l3if_type == CTC_L3IF_TYPE_PHY_IF))
    {
        if(p_nh_param->oif.vid  > 0 && p_nh_param->oif.vid <= CTC_MAX_VLAN_ID )
        {
            p_ds_l2edit->output_vlan_id             = p_nh_param->oif.vid;
            p_ds_l2edit->output_vlan_id_is_svlan    = 1;
            p_ds_l2edit->output_vlan_id_valid       = 1;
        }
        else if(0 == p_nh_param->oif.vid)
        {
            p_ds_l2edit->output_vlan_id             = 0xFFF;
        }

    }

    if((0xFF == p_nh_param->mac[0])
        && (0xFF == p_nh_param->mac[1])
        && (0xFF == p_nh_param->mac[2])
        && (0xFF == p_nh_param->mac[3])
        && (0xFF == p_nh_param->mac[4])
        && (0xFF == p_nh_param->mac[5]))
    {
        p_ds_l2edit->ds_type   = SYS_NH_DS_TYPE_DISCARD;
    }
    else
    {
        p_ds_l2edit->ds_type   = SYS_NH_DS_TYPE_L2EDIT;
    }


    extra_header_type = SYS_L2EDIT_EXTRA_HEADER_TYPE_MPLS;
    p_ds_l2edit->extra_header_type0_0  = (extra_header_type & 0x1);
    p_ds_l2edit->extra_header_type1_1  = ((extra_header_type >> 1) & 0x1);

    if (label_num == 2)
    {
        ds_l3edit_mpls_8w.label_valid3 = 1;
        /*because SPME can be exist in SPME + LSP mpls nexthop,so SPME label's oam_en always is 1 */
        ds_l3edit_mpls_8w.oam_en3 = 1;
        /*   ipDa[31:0] / {deriveExp3/statsPtr3[13], exp3[2:0]/statsPtr3[12:10], 1'b0, oamEn3, entropyLabelEn3,
              1'b0, ttlIndex3[3:0]/statsPtr3[9:6], label3[19:0]/{statsPtr3[5:0], logicDestPort3[13:0]} } / macSa[31:0]*/

        if (CTC_FLAG_ISSET(p_nh_param->tunnel_label[1].lable_flag, CTC_MPLS_NH_LABEL_MAP_TTL))
        {
            ds_l3edit_mpls_8w.map_ttl3 = 1;
        }

        if (CTC_FLAG_ISSET(p_nh_param->tunnel_label[1].lable_flag, CTC_MPLS_NH_LABEL_IS_MCAST))
        {
            ds_l3edit_mpls_8w.mcast_label3 = 1;
        }

        CTC_ERROR_RETURN(sys_greatbelt_lkup_ttl_index(lchip, p_nh_param->tunnel_label[1].ttl, &ttl_index));
        ds_l3edit_mpls_8w.ttl_index3 = ttl_index;
        ds_l3edit_mpls_8w.label3  = (p_nh_param->tunnel_label[1].label & 0xFFFFF);   /*label3*/

        switch (p_nh_param->tunnel_label[1].exp_type)
        {
        case CTC_NH_EXP_SELECT_ASSIGN:   /* user cfg exp  */
            ds_l3edit_mpls_8w.derive_exp3  = 0;
            ds_l3edit_mpls_8w.exp3 = p_nh_param->tunnel_label[1].exp & 0x7;
            break;

        case CTC_NH_EXP_SELECT_MAP:  /*mapped exp */
            ds_l3edit_mpls_8w.derive_exp3 = 1;
            ds_l3edit_mpls_8w.exp3 = SYS_NH_MPLS_LABEL_EXP_SELECT_MAP;
            break;

        case CTC_NH_EXP_SELECT_PACKET:   /*input packet exp  */

            ds_l3edit_mpls_8w.derive_exp3 = 1;
            ds_l3edit_mpls_8w.exp3 = SYS_NH_MPLS_LABEL_EXP_SELECT_PACKET;
            break;

        default:
            break;
        }
    }
    if (label_num)
    {
        ds_l3edit_mpls_8w.label_valid2 = 1;
    }

    /*   ipDa[31:0] / {deriveExp3/statsPtr3[13], exp3[2:0]/statsPtr3[12:10], 1'b0, oamEn3, entropyLabelEn3,
          1'b0, ttlIndex3[3:0]/statsPtr3[9:6], label3[19:0]/{statsPtr3[5:0], logicDestPort3[13:0]} } / macSa[31:0]*/

    if (CTC_FLAG_ISSET(p_nh_param->tunnel_label[0].lable_flag, CTC_MPLS_NH_LABEL_MAP_TTL))
    {
        ds_l3edit_mpls_8w.map_ttl2 = 1;
    }

    if (CTC_FLAG_ISSET(p_nh_param->tunnel_label[0].lable_flag, CTC_MPLS_NH_LABEL_IS_MCAST))
    {
        ds_l3edit_mpls_8w.mcast_label2 = 1;
    }

    CTC_ERROR_RETURN(sys_greatbelt_lkup_ttl_index(lchip, p_nh_param->tunnel_label[0].ttl, &ttl_index));
    ds_l3edit_mpls_8w.ttl_index2 = ttl_index;
    ds_l3edit_mpls_8w.label2  = (p_nh_param->tunnel_label[0].label & 0xFFFFF);       /*label3*/

    switch (p_nh_param->tunnel_label[0].exp_type)
    {
    case CTC_NH_EXP_SELECT_ASSIGN:         /* user cfg exp  */
        ds_l3edit_mpls_8w.derive_exp2  = 0;
        ds_l3edit_mpls_8w.exp2 = p_nh_param->tunnel_label[0].exp;
        break;

    case CTC_NH_EXP_SELECT_MAP:       /*input packet exp  */
        ds_l3edit_mpls_8w.derive_exp2 = 1;
        ds_l3edit_mpls_8w.exp2 = SYS_NH_MPLS_LABEL_EXP_SELECT_MAP;
        break;

    case CTC_NH_EXP_SELECT_PACKET:       /*input packet exp  */
        ds_l3edit_mpls_8w.derive_exp2 = 1;
        ds_l3edit_mpls_8w.exp2 = SYS_NH_MPLS_LABEL_EXP_SELECT_PACKET;
        break;

    default:
        break;
    }

    if (label_num == 2 && p_nh_param->stats_valid)
    {
        return CTC_E_NH_LABEL_IS_MISMATCH_WITH_STATS;
    }
    else if (label_num == 1 && p_nh_param->stats_valid)
    {
        /* get statsPtr from stats poll */
        CTC_ERROR_RETURN(sys_greatbelt_stats_get_statsptr
                         (lchip, p_nh_param->stats_id, stats_ptr));
        ds_l3edit_mpls_8w.map_ttl3 = 1;
        ds_l3edit_mpls_8w.label3 |= ((*stats_ptr) & 0x3F) << 14;
        ds_l3edit_mpls_8w.ttl_index3 = ((*stats_ptr) >> 6) & 0xF;
        ds_l3edit_mpls_8w.exp3 = ((*stats_ptr) >> 10) & 0x7;
        ds_l3edit_mpls_8w.derive_exp3 = ((*stats_ptr) >> 13) & 0x1;
    }

    sal_memcpy((uint8*)p_ds_l2edit + sizeof(ds_l3_edit_mpls4_w_t), (uint8*)&ds_l3edit_mpls_8w + sizeof(ds_l3_edit_mpls4_w_t), sizeof(ds_l3_edit_mpls4_w_t));
    p_ds_l2edit->ip_da78_64  |= 1 << 12;        /*extra_header_type: 1 -mpls*/

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_nh_mpls_build_dsnh(uint8 lchip, ctc_mpls_nexthop_param_t* p_nh_mpls_param, uint32 dsnh_offset,
                                  bool working_path, sys_nh_info_mpls_t* p_nhdb)
{

    sys_nh_param_dsnh_t   dsnh_param;
    uint8 gchip = 0;

    ctc_mpls_nexthop_pop_param_t* p_nh_param_pop = NULL;         /**< mpls (asp) pop nexthop */
    ctc_mpls_nexthop_push_param_t* p_nh_param_push = NULL;      /**< mpls push (asp) nexthop */
    ctc_mpls_nh_op_t               nh_opcode = 0;                /**< MPLS nexthop operation code*/
    sys_l3if_prop_t                l3if_prop;
    sys_nh_db_mpls_tunnel_t* p_mpls_tunnel = NULL;

    sal_memset(&dsnh_param, 0, sizeof(sys_nh_param_dsnh_t));
    sal_memset(&l3if_prop, 0, sizeof(sys_l3if_prop_t));

    /* L2Edit & L3Edit  */
    if (p_nh_mpls_param->nh_prop == CTC_MPLS_NH_PUSH_TYPE)
    {
        p_nh_param_push  = working_path ? (&p_nh_mpls_param->nh_para.nh_param_push) : (&p_nh_mpls_param->nh_p_para.nh_p_param_push);

        CTC_ERROR_RETURN(sys_greatbelt_nh_lkup_mpls_tunnel(lchip, p_nh_param_push->tunnel_id, &p_mpls_tunnel));


        if (p_nh_param_push->nh_com.opcode == CTC_MPLS_NH_PUSH_OP_L2VPN
            && (p_nh_mpls_param->logic_port_valid || CTC_FLAG_ISSET(p_nh_mpls_param->flag, CTC_MPLS_NH_IS_HVPLS)))
        {
            CTC_LOGIC_PORT_RANGE_CHECK(p_nh_mpls_param->logic_port);
            p_nhdb->dest_logic_port = p_nh_mpls_param->logic_port;
            dsnh_param.logic_port = p_nh_mpls_param->logic_port;
            dsnh_param.hvpls = CTC_FLAG_ISSET(p_nh_mpls_param->flag, CTC_MPLS_NH_IS_HVPLS);

            CTC_SET_FLAG(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_LOGIC_PORT);

            if(!CTC_FLAG_ISSET(p_nh_mpls_param->flag, CTC_MPLS_NH_IS_HVPLS))
            {
               CTC_SET_FLAG(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HORIZON_SPLIT_EN);
            }
            else if(!p_nh_mpls_param->logic_port_valid)
            {
                return CTC_E_VPLS_INVALID_VPLSPORT_ID;
            }
        }

        if ((p_nh_mpls_param->adjust_length>SYS_NH_LENGTH_ADJUST_MIDDLE_VALUE)
            &&(CTC_MPLS_NH_PUSH_OP_L2VPN != p_nh_param_push->nh_com.opcode))
        {
            CTC_SET_FLAG(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_LENGTH_ADJUST_EN);
        }

        if (!p_mpls_tunnel)
        {
            return CTC_E_NH_NOT_EXIST_TUNNEL_LABEL;
        }

        if (p_nh_mpls_param->aps_en && (p_nh_param_push->label_num == 0))
        {
            return CTC_E_NH_NO_LABEL;
        }
        dsnh_param.p_vlan_info = &p_nh_param_push->nh_com.vlan_info;
        nh_opcode              = p_nh_param_push->nh_com.opcode;

        if (!CTC_FLAG_ISSET(p_mpls_tunnel->flag, SYS_NH_MPLS_TUNNEL_FLAG_APS))
        {
            l3if_prop.l3if_id      = p_mpls_tunnel->l3ifid;
            CTC_ERROR_RETURN(sys_greatbelt_l3if_get_l3if_info(lchip, 1, &l3if_prop));
            dsnh_param.dest_vlan_ptr  = l3if_prop.vlan_ptr;
            gchip = SYS_MAP_GPORT_TO_GCHIP(p_mpls_tunnel->gport);
        }

        p_nhdb->gport  = p_mpls_tunnel->gport;
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

        gchip = SYS_MAP_GPORT_TO_GCHIP(p_nh_param_pop->nh_com.oif.gport);
        CTC_ERROR_RETURN(sys_greatbelt_l3if_get_l3if_info_with_port_and_vlan(lchip, p_nh_param_pop->nh_com.oif.gport, p_nh_param_pop->nh_com.oif.vid, &l3if_prop));

        dsnh_param.dest_vlan_ptr  = l3if_prop.vlan_ptr;
        p_nhdb->gport  = p_nh_param_pop->nh_com.oif.gport;
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


    if (CTC_FLAG_ISSET(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_EGRESS_EDIT_MODE) && !p_nh_mpls_param->aps_en
        && (!p_mpls_tunnel || !CTC_FLAG_ISSET(p_mpls_tunnel->flag, SYS_NH_MPLS_TUNNEL_FLAG_APS)))
    {
        if (SYS_GCHIP_IS_REMOTE(lchip, gchip))
        {
            return CTC_E_NONE;
        }
    }

    if (p_nh_mpls_param->nh_prop == CTC_MPLS_NH_PUSH_TYPE)
    {
        if (CTC_FLAG_ISSET(p_mpls_tunnel->flag, SYS_NH_MPLS_TUNNEL_FLAG_APS))
        {
            dsnh_param.l2edit_ptr = p_mpls_tunnel->gport;
            CTC_SET_FLAG(dsnh_param.flag, SYS_NH_PARAM_FLAG_L2EDIT_AS_APS_GROUP);
            CTC_SET_FLAG(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_APS_EN);
        }
        else
        {
            dsnh_param.l2edit_ptr = p_mpls_tunnel->dsl2edit_offset;
        }

        CTC_ERROR_RETURN(_sys_greatbelt_nh_mpls_build_dsl3edit(lchip, p_nh_param_push, &dsnh_param));

        if (p_nh_param_push->stats_valid)
        {
            /*mpls l2vpn pw stats*/
            CTC_ERROR_RETURN(sys_greatbelt_stats_get_statsptr
                             (lchip, p_nh_param_push->stats_id, &dsnh_param.stats_ptr));
            CTC_SET_FLAG(dsnh_param.flag, SYS_NH_PARAM_FLAG_STATS_EN);
        }

        if (dsnh_param.l3edit_ptr != 0)
        {
            CTC_SET_FLAG(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L3EDIT);
        }

        if (dsnh_param.stats_ptr != 0)
        {
            CTC_SET_FLAG(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_STATS_VALID);
        }

        if (working_path)
        {
            p_nhdb->working_path.dsl3edit_offset  = dsnh_param.l3edit_ptr;
            p_nhdb->working_path.stats_ptr = dsnh_param.stats_ptr;
        }
        else
        {
            p_nhdb->protection_path->dsl3edit_offset  = dsnh_param.l3edit_ptr;
            p_nhdb->protection_path->stats_ptr = dsnh_param.stats_ptr;
        }
    }
    else if (p_nh_mpls_param->nh_prop == CTC_MPLS_NH_POP_TYPE)
    {
        if(p_nh_param_pop->arp_id)
        {
            sys_nh_db_arp_t *p_arp = NULL;
            CTC_ERROR_RETURN(sys_greatbelt_nh_lkup_arp_id(lchip, p_nh_param_pop->arp_id, &p_arp));
            dsnh_param.l2edit_ptr  = p_arp->offset;
        }
        else
        {
            sys_nh_db_dsl2editeth4w_t   dsl2edit_4w;
            sys_nh_db_dsl2editeth4w_t * p_dsl2edit_4w  = &dsl2edit_4w;
            sal_memset(&dsl2edit_4w, 0, sizeof(sys_nh_db_dsl2editeth4w_t));

            dsl2edit_4w.offset = 0;
            dsl2edit_4w.ref_cnt = 0;
            /*pop no need l2 edit to edit vlan id*/
            /*
            dsl2edit_4w.output_vlan_is_svlan =  p_nh_param_pop->output_vlan_is_svlan;
            dsl2edit_4w.output_vid           =  p_nh_param_pop->output_vid;
            */
            if (CTC_L3IF_TYPE_PHY_IF == l3if_prop.l3if_type)
            {
                dsl2edit_4w.phy_if    = 1;
            }
            sal_memcpy(dsl2edit_4w.mac_da, p_nh_param_pop->nh_com.mac, sizeof(mac_addr_t));
            CTC_ERROR_RETURN(sys_greatbelt_nh_add_route_l2edit(lchip, &p_dsl2edit_4w));
            p_nhdb->p_dsl2edit = p_dsl2edit_4w;
            CTC_SET_FLAG(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L2EDIT);
            dsnh_param.l2edit_ptr = p_dsl2edit_4w->offset;
        }

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

        CTC_SET_FLAG(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_MPLS_POP_NH);
    }

    /*Write DsNexthop to Asic*/

    if (CTC_FLAG_ISSET(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W))
    {
        dsnh_param.dsnh_offset = working_path ? p_nhdb->hdr.dsfwd_info.dsnh_offset : p_nhdb->hdr.dsfwd_info.dsnh_offset + 2;
        CTC_ERROR_RETURN(sys_greatbelt_nh_write_entry_dsnh8w(lchip, &dsnh_param));
    }
    else
    {
        dsnh_param.dsnh_offset = working_path ? p_nhdb->hdr.dsfwd_info.dsnh_offset : p_nhdb->hdr.dsfwd_info.dsnh_offset + 1;
        CTC_ERROR_RETURN(sys_greatbelt_nh_write_entry_dsnh4w(lchip, &dsnh_param));
    }


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
    else if (p_nh_mpls_param->nh_prop == CTC_MPLS_NH_POP_TYPE)
    {
        p_nhdb->gport  = p_nh_param_pop->nh_com.oif.gport;
        p_nhdb->l3ifid = l3if_prop.l3if_id;
    }

    return CTC_E_NONE;
}


int32
_sys_greatbelt_nh_free_mpls_nh_resource(uint8 lchip, sys_nh_info_mpls_t* p_nhinfo)
{

    /*2. Delete this mpls nexthop resource */

    if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L3EDIT))
    {
        sys_greatbelt_nh_offset_free(lchip, \
        SYS_NH_ENTRY_TYPE_L3EDIT_MPLS_4W, 1, p_nhinfo->working_path.dsl3edit_offset);
        if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_STATS_VALID))
        {
            p_nhinfo->working_path.stats_ptr = 0;
        }

        if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_APS_EN)
            && !CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_APS_SHARE_DSNH)
        && p_nhinfo->protection_path)
        {
            sys_greatbelt_nh_offset_free(lchip, \
            SYS_NH_ENTRY_TYPE_L3EDIT_MPLS_4W, 1, p_nhinfo->protection_path->dsl3edit_offset);

            if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_STATS_VALID))
            {
                p_nhinfo->protection_path->stats_ptr = 0;
            }

        }


    }

    if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L2EDIT)
        && CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_MPLS_POP_NH))
    {
        sys_greatbelt_nh_remove_route_l2edit(lchip, p_nhinfo->p_dsl2edit);
    }


   CTC_UNSET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L3EDIT);

   if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L2EDIT)
    && CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_MPLS_POP_NH))
   {
       CTC_UNSET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L2EDIT);
       CTC_UNSET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_MPLS_POP_NH);
   }
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

    return CTC_E_NONE;
}

int32
_sys_greatbelt_nh_mpls_add_dsfwd(uint8 lchip, sys_nh_param_com_t* p_com_nh_para, sys_nh_info_com_t* p_com_db)
{
   sys_nh_param_dsfwd_t dsfwd_param;
   sys_nh_param_mpls_t* p_nh_param = (sys_nh_param_mpls_t*)(p_com_nh_para);
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
        dsfwd_param.dest_chipid = SYS_MAP_GPORT_TO_GCHIP(p_nhdb->aps_group_id);
        dsfwd_param.dest_id = p_nhdb->aps_group_id & 0x3FF;
        dsfwd_param.aps_type = CTC_APS_BRIDGE;

    }
    else
    {
        dsfwd_param.dest_chipid = SYS_MAP_GPORT_TO_GCHIP(p_nhdb->gport);
        dsfwd_param.dest_id = CTC_MAP_GPORT_TO_LPORT(p_nhdb->gport);
    }

    if (CTC_FLAG_ISSET(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_LENGTH_ADJUST_EN))
    {
        dsfwd_param.length_adjust_type = 1;
    }
    dsfwd_param.bypass_ingress_edit = CTC_FLAG_ISSET(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_EGRESS_EDIT_MODE);


    /*Build DsFwd Table*/
    if (!CTC_FLAG_ISSET(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD))
    {
        ret = sys_greatbelt_nh_offset_alloc(lchip, SYS_NH_ENTRY_TYPE_FWD, 1,
                                            &(p_nhdb->hdr.dsfwd_info.dsfwd_offset));
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Lchip = %d, DsFwdOffset = %d\n", lchip,
                       (p_nhdb->hdr.dsfwd_info.dsfwd_offset));
    }



    dsfwd_param.dsfwd_offset = p_nhdb->hdr.dsfwd_info.dsfwd_offset;
    dsfwd_param.dsnh_offset = p_nhdb->hdr.dsfwd_info.dsnh_offset;
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "DsFwd(Lchip : %d) :: DestChipId = %d, DestId = %d"
    "DsNexthop Offset = %d, DsNexthopExt = %d\n", lchip,
                       dsfwd_param.dest_chipid, dsfwd_param.dest_id,
                       dsfwd_param.dsnh_offset, dsfwd_param.nexthop_ext);


    if (p_nh_param->p_mpls_nh_param->nh_prop == CTC_MPLS_NH_PUSH_TYPE)
    {
        if (CTC_FLAG_ISSET(p_nh_param->p_mpls_nh_param->nh_para.nh_param_push.nh_com.mpls_nh_flag, CTC_MPLS_NH_FLAG_SERVICE_QUEUE_EN))
        {
            dsfwd_param.service_queue_en = TRUE;
        }
    }

    /*Write table*/
    ret = ret ? ret : sys_greatbelt_nh_write_entry_dsfwd(lchip, &dsfwd_param);



    if (ret == CTC_E_NONE)
    {
        CTC_SET_FLAG(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD);
    }

    return ret;
}
int32
_sys_greatbelt_nh_free_mpls_nh_resource_by_offset(uint8 lchip, sys_nh_info_mpls_t* p_nhinfo, bool b_pw_aps_switch)
{
    if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L3EDIT))
    {
        sys_greatbelt_nh_offset_free(lchip, \
        SYS_NH_ENTRY_TYPE_L3EDIT_MPLS_4W, 1, p_nhinfo->working_path.dsl3edit_offset);

        if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_APS_EN)
            && !CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_APS_SHARE_DSNH)
        && p_nhinfo->protection_path)
        {
            sys_greatbelt_nh_offset_free(lchip, \
            SYS_NH_ENTRY_TYPE_L3EDIT_MPLS_4W, 1, p_nhinfo->protection_path->dsl3edit_offset);
        }
    }

    if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L2EDIT)
        && CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_MPLS_POP_NH))
    {
        sys_greatbelt_nh_remove_route_l2edit(lchip, p_nhinfo->p_dsl2edit);
    }


    if((b_pw_aps_switch)&&(!CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_EGRESS_EDIT_MODE)))
    {
        if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W))
        {
            CTC_ERROR_RETURN(sys_greatbelt_nh_offset_free(lchip, SYS_NH_ENTRY_TYPE_NEXTHOP_8W,
                             p_nhinfo->hdr.dsnh_entry_num, p_nhinfo->hdr.dsfwd_info.dsnh_offset));
        }
        else
        {
            CTC_ERROR_RETURN(sys_greatbelt_nh_offset_free(lchip, SYS_NH_ENTRY_TYPE_NEXTHOP_4W,
                             p_nhinfo->hdr.dsnh_entry_num, p_nhinfo->hdr.dsfwd_info.dsnh_offset));
        }
    }

    return CTC_E_NONE;
}

int32
_sys_greatbelt_mpls_nh_clear_mpls_nh_resource(uint8 lchip, sys_nh_info_mpls_t* p_nhinfo)
{
    uint32 nh_entry_flags = 0;

    if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L3EDIT))
    {
        if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_STATS_VALID))
        {
            p_nhinfo->working_path.stats_ptr = 0;
        }

        if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_APS_EN)
            && !CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_APS_SHARE_DSNH)
        && p_nhinfo->protection_path)
        {

            if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_STATS_VALID))
            {
                p_nhinfo->protection_path->stats_ptr = 0;
            }
        }
    }


    CTC_UNSET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L3EDIT);

    if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L2EDIT)
    && CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_MPLS_POP_NH))
    {
        CTC_UNSET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L2EDIT);
        CTC_UNSET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_MPLS_POP_NH);
    }

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

     nh_entry_flags = p_nhinfo->hdr.nh_entry_flags;


     p_nhinfo->hdr.nh_entry_flags = 0;
    if(CTC_FLAG_ISSET(nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W))
         CTC_SET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W);
    if(CTC_FLAG_ISSET(nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_INT_ALLOC_DSNH))
         CTC_SET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_INT_ALLOC_DSNH);
    if(CTC_FLAG_ISSET(nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD))
         CTC_SET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD);
   if(CTC_FLAG_ISSET(nh_entry_flags, SYS_NH_INFO_FLAG_EGRESS_EDIT_MODE))
         CTC_SET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_EGRESS_EDIT_MODE);
    return CTC_E_NONE;
}


int32
sys_greatbelt_nh_create_mpls_cb(uint8 lchip, sys_nh_param_com_t* p_com_nh_para,
                                sys_nh_info_com_t* p_com_db)
{
    sys_nh_param_mpls_t* p_nh_param;
    sys_nh_info_mpls_t* p_nhdb;
    int32 ret = 0;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

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

        CTC_ERROR_RETURN(sys_greatbelt_nh_create_special_cb(lchip, (
                                                                sys_nh_param_com_t*)(&nh_para_spec), (sys_nh_info_com_t*)(p_nhdb)));
        CTC_SET_FLAG(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_UNROV);
        return CTC_E_NONE;
    }

    if (p_nh_param->p_mpls_nh_param->aps_en)
    {
        bool protect_en = FALSE;
        CTC_ERROR_RETURN(sys_greatbelt_aps_get_aps_bridge(lchip, p_nh_param->p_mpls_nh_param->aps_bridge_group_id, &protect_en));

        /*working path -- PW layer*/
        ret = _sys_greatbelt_nh_mpls_build_dsnh(lchip, p_nh_param->p_mpls_nh_param,  p_nh_param->p_mpls_nh_param->dsnh_offset, TRUE, p_nhdb);
        if (ret != CTC_E_NONE)
        {
            goto error_proc;
        }

		/*protection path -- PW layer*/
        if(!p_nhdb->protection_path)
        {
            p_nhdb->protection_path  = mem_malloc(MEM_NEXTHOP_MODULE, sizeof(sys_nh_info_mpls_edit_info_t));
            if (!p_nhdb->protection_path)
            {
                ret = CTC_E_NO_MEMORY;
                goto error_proc;
            }
        }

        sal_memset(p_nhdb->protection_path, 0, sizeof(sys_nh_info_mpls_edit_info_t));

        ret = _sys_greatbelt_nh_mpls_build_dsnh(lchip, p_nh_param->p_mpls_nh_param,  p_nh_param->p_mpls_nh_param->dsnh_offset, FALSE, p_nhdb);
        if (ret != CTC_E_NONE)
        {
            goto error_proc;
        }

        p_nhdb->aps_group_id = p_nh_param->p_mpls_nh_param->aps_bridge_group_id;
        CTC_SET_FLAG(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_APS_EN);
        CTC_UNSET_FLAG(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_APS_SHARE_DSNH);
        ret = ret ? ret : sys_greatbelt_aps_set_share_nh(lchip, p_nhdb->aps_group_id, FALSE);

        if (ret != CTC_E_NONE)
        {
            goto error_proc;
        }
    }
    else
    {
        ret = _sys_greatbelt_nh_mpls_build_dsnh(lchip, p_nh_param->p_mpls_nh_param,  p_nh_param->p_mpls_nh_param->dsnh_offset, TRUE, p_nhdb);

        if (CTC_FLAG_ISSET(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_APS_EN))
        {
            p_nhdb->aps_group_id = p_nhdb->gport;
            CTC_SET_FLAG(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_APS_SHARE_DSNH);
            ret = ret ? ret : sys_greatbelt_aps_set_share_nh(lchip, p_nhdb->aps_group_id, TRUE);
        }

        if (ret != CTC_E_NONE)
        {
            goto error_proc;
        }
    }

	if (!p_nh_param->hdr.have_dsfwd)
    {
        return ret ;
    }

    ret = _sys_greatbelt_nh_mpls_add_dsfwd(lchip, p_com_nh_para, p_com_db);

    if (ret != CTC_E_NONE)
    {

        sys_greatbelt_nh_offset_free(lchip, SYS_NH_ENTRY_TYPE_FWD, 1,
                                     p_nhdb->hdr.dsfwd_info.dsfwd_offset);

    }
    else
    {
        return CTC_E_NONE;
    }

error_proc:
    _sys_greatbelt_nh_free_mpls_nh_resource(lchip, p_nhdb);


    return ret;
}

/**
 @brief Callback function of delete unicast ip nexthop

 @param[in] p_com_db, pointer used to store nexthop data

 @return CTC_E_XXX
 */
int32
sys_greatbelt_nh_delete_mpls_cb(uint8 lchip, sys_nh_info_com_t* p_data, uint32* p_nhid)
{
    sys_nh_info_mpls_t* p_nhinfo;
    sys_nh_ref_list_node_t* p_ref_node, * p_tmp_node;
    sys_nh_param_dsfwd_t dsfwd_param;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    CTC_PTR_VALID_CHECK(p_data);
    CTC_EQUAL_CHECK(SYS_NH_TYPE_MPLS, p_data->hdr.nh_entry_type);
    p_nhinfo = (sys_nh_info_mpls_t*)(p_data);

    sys_greatbelt_nh_update_ecmp_member(lchip, *p_nhid, SYS_NH_CHANGE_TYPE_NH_DELETE);

    /*1. Notify all reference nh, ipuc will be deleted*/
    p_ref_node = p_nhinfo->hdr.p_ref_nh_list;

    while (p_ref_node)
    {
        /*Remove from db*/
        p_tmp_node = p_ref_node;
        p_ref_node = p_ref_node->p_next;
        mem_free(p_tmp_node);
    }

    /*2. Delete this mpls nexthop*/
    sal_memset(&dsfwd_param, 0, sizeof(sys_nh_param_dsfwd_t));
    if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD))
    {
        dsfwd_param.dsfwd_offset = p_nhinfo->hdr.dsfwd_info.dsfwd_offset;
        dsfwd_param.drop_pkt = 1;
        /*Write table*/
        sys_greatbelt_nh_write_entry_dsfwd(lchip, &dsfwd_param);
        /*Free DsFwd offset*/
        sys_greatbelt_nh_offset_free(lchip, \
        SYS_NH_ENTRY_TYPE_FWD, 1, p_nhinfo->hdr.dsfwd_info.dsfwd_offset);

    }

    _sys_greatbelt_nh_free_mpls_nh_resource(lchip, p_nhinfo);
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_nh_update_mpls_fwd_to_spec(uint8 lchip, sys_nh_param_mpls_t* p_nhpara, sys_nh_info_mpls_t* p_nhinfo)
{

    sys_nh_param_special_t nh_para_spec;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    sal_memset(&nh_para_spec, 0, sizeof(sys_nh_param_special_t));


    nh_para_spec.hdr.have_dsfwd= TRUE;
    nh_para_spec.hdr.nh_param_type = SYS_NH_TYPE_UNROV;
    nh_para_spec.hdr.is_internal_nh = TRUE;



    /*1.2 update dsfwd to unrov nh's dsfwd, write dsfwd*/
    CTC_ERROR_RETURN(sys_greatbelt_nh_create_special_cb(lchip, (
                                                            sys_nh_param_com_t*)(&nh_para_spec), (sys_nh_info_com_t*)(p_nhinfo)));
    CTC_SET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_UNROV);

    _sys_greatbelt_nh_free_mpls_nh_resource(lchip, p_nhinfo);

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_mpls_nh_alloc_dsnh_offset(uint8 lchip, sys_nh_param_mpls_t* p_nhpara, sys_nh_info_mpls_t* p_nhinfo)
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
        CTC_ERROR_RETURN(sys_greatbelt_nh_offset_alloc(lchip, SYS_NH_ENTRY_TYPE_NEXTHOP_8W, p_nhinfo->hdr.dsnh_entry_num, &dsnh_offset));
    }
    else
    {
        CTC_ERROR_RETURN(sys_greatbelt_nh_offset_alloc(lchip, SYS_NH_ENTRY_TYPE_NEXTHOP_4W, p_nhinfo->hdr.dsnh_entry_num, &dsnh_offset));

    }

    CTC_SET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_INT_ALLOC_DSNH);

    p_nhinfo->hdr.dsfwd_info.dsnh_offset = dsnh_offset;

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_mpls_nh_free_dsnh_offset(uint8 lchip, sys_nh_info_mpls_t* p_nhinfo)
{
    uint32    dsnh_offset = 0;
    dsnh_offset = p_nhinfo->hdr.dsfwd_info.dsnh_offset;

    if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W))
    {
        CTC_ERROR_RETURN(sys_greatbelt_nh_offset_free(lchip, SYS_NH_ENTRY_TYPE_NEXTHOP_8W, p_nhinfo->hdr.dsnh_entry_num, dsnh_offset));
    }
    else
    {
        CTC_ERROR_RETURN(sys_greatbelt_nh_offset_free(lchip, SYS_NH_ENTRY_TYPE_NEXTHOP_4W, p_nhinfo->hdr.dsnh_entry_num, dsnh_offset));
    }

    return CTC_E_NONE;
}


STATIC int32
_sys_greatbelt_nh_update_mpls_fwd_attr(uint8 lchip, sys_nh_param_mpls_t* p_nhpara, sys_nh_info_mpls_t* p_nhinfo)
{
    int32 ret = CTC_E_NONE;
    sys_nh_info_mpls_t nhinfo_tmp;
    sys_nh_info_mpls_edit_info_t protection_path;  /*when pw aps enbale*/
    bool b_old_pw_aps = FALSE;
    bool b_new_pw_aps = FALSE;
    bool b_pw_aps_switch = FALSE;


    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);


    sal_memcpy(&nhinfo_tmp, p_nhinfo, sizeof(sys_nh_info_mpls_t));
    sal_memcpy(&nhinfo_tmp.hdr.dsfwd_info, &p_nhinfo->hdr.dsfwd_info, sizeof(sys_nh_info_dsfwd_t));

    b_old_pw_aps = (p_nhinfo->protection_path)? TRUE : FALSE;
    b_new_pw_aps = (p_nhpara->p_mpls_nh_param->aps_en)? TRUE : FALSE;
    b_pw_aps_switch = (b_old_pw_aps!=b_new_pw_aps)? TRUE : FALSE;

    if(b_pw_aps_switch
        &&(!CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_INT_ALLOC_DSNH))
        &&(!CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_EGRESS_EDIT_MODE)))
    {
        return CTC_E_NH_SUPPORT_PW_APS_UPDATE_BY_INT_ALLOC_DSNH;
    }

    if(b_old_pw_aps)
    {
       sal_memcpy(&protection_path, p_nhinfo->protection_path, sizeof(sys_nh_info_mpls_edit_info_t));
       nhinfo_tmp.protection_path = &protection_path;
    }
    CTC_ERROR_RETURN(_sys_greatbelt_mpls_nh_clear_mpls_nh_resource(lchip, p_nhinfo));

    if(b_pw_aps_switch
        &&(!CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_EGRESS_EDIT_MODE)))
    {
        CTC_ERROR_GOTO(_sys_greatbelt_mpls_nh_alloc_dsnh_offset(lchip,  p_nhpara, p_nhinfo), ret, error_proc1);
    }
    p_nhpara->hdr.have_dsfwd = CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD)
                                || (NULL != p_nhinfo->hdr.p_ref_nh_list);
    CTC_ERROR_GOTO(sys_greatbelt_nh_create_mpls_cb(lchip, (sys_nh_param_com_t*)p_nhpara, (sys_nh_info_com_t*)p_nhinfo), ret, error_proc2);
    CTC_UNSET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_UNROV);

    CTC_ERROR_RETURN(_sys_greatbelt_nh_free_mpls_nh_resource_by_offset(lchip, &nhinfo_tmp,b_pw_aps_switch));

    return CTC_E_NONE;

error_proc2:
    if (b_pw_aps_switch
        &&(!CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_EGRESS_EDIT_MODE)))
    {
        _sys_greatbelt_mpls_nh_free_dsnh_offset(lchip, p_nhinfo);
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

int32
_sys_greatbelt_nh_update_mpls_fwd_discard(uint8 lchip, sys_nh_info_mpls_t* p_nhinfo, bool discard)
{
    sys_nh_param_dsfwd_t dsfwd_param;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_UNROV))
    {
        return CTC_E_NH_IS_UNROV;
    }

    /*2. update this mpls nexthop*/
    sal_memset(&dsfwd_param, 0, sizeof(sys_nh_param_dsfwd_t));


    if (discard)
    {
        dsfwd_param.drop_pkt = 1;
        CTC_SET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_DISCARD);
    }
    else
    {
        if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W))
        {
            dsfwd_param.nexthop_ext = 1;
        }

        if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_APS_EN))
        {
            dsfwd_param.dest_chipid = 0;
            dsfwd_param.dest_id = p_nhinfo->aps_group_id & 0x3FF;
            dsfwd_param.aps_type = CTC_APS_BRIDGE;

        }
        else
        {
            dsfwd_param.dest_chipid = SYS_MAP_GPORT_TO_GCHIP(p_nhinfo->gport);
            dsfwd_param.dest_id = CTC_MAP_GPORT_TO_LPORT(p_nhinfo->gport);
        }

        dsfwd_param.dsfwd_offset = p_nhinfo->hdr.dsfwd_info.dsfwd_offset;
        dsfwd_param.dsnh_offset = p_nhinfo->hdr.dsfwd_info.dsnh_offset;
        dsfwd_param.bypass_ingress_edit = CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_EGRESS_EDIT_MODE);
        CTC_UNSET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_DISCARD);
    }

    /*Write table*/
    CTC_ERROR_RETURN(sys_greatbelt_nh_write_entry_dsfwd(lchip, &dsfwd_param));


    return CTC_E_NONE;

}

/**
 @brief Callback function used to update ipuc nexthop

 @param[in] p_nh_ptr, pointer of ipuc nexthop DB

 @param[in] p_para, member information

 @return CTC_E_XXX
 */
int32
sys_greatbelt_nh_update_mpls_cb(uint8 lchip, sys_nh_info_com_t* p_nh_db,
                                sys_nh_param_com_t* p_para)
{
    sys_nh_info_mpls_t* p_nh_info;
    sys_nh_param_mpls_t* p_nh_para;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    CTC_PTR_VALID_CHECK(p_para);
    CTC_PTR_VALID_CHECK(p_nh_db);
    CTC_EQUAL_CHECK(SYS_NH_TYPE_MPLS, p_para->hdr.nh_param_type);
    CTC_EQUAL_CHECK(SYS_NH_TYPE_MPLS, p_nh_db->hdr.nh_entry_type);
    p_nh_info = (sys_nh_info_mpls_t*)(p_nh_db);
    p_nh_para = (sys_nh_param_mpls_t*)(p_para);

    switch (p_nh_para->hdr.change_type)
    {
    case SYS_NH_CHANGE_TYPE_FWD_TO_UNROV:
        CTC_ERROR_RETURN(_sys_greatbelt_nh_update_mpls_fwd_to_spec(lchip, p_nh_para, p_nh_info));
        break;

    case SYS_NH_CHANGE_TYPE_UPDATE_FWD_ATTR:
        CTC_ERROR_RETURN(_sys_greatbelt_nh_update_mpls_fwd_attr(lchip, p_nh_para, p_nh_info));
        break;

    case SYS_NH_CHANGE_TYPE_UNROV_TO_FWD:
        if (!CTC_FLAG_ISSET(p_nh_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_UNROV))
        {
            return CTC_E_NH_ISNT_UNROV;
        }

        CTC_ERROR_RETURN(_sys_greatbelt_nh_update_mpls_fwd_attr(lchip, p_nh_para, p_nh_info));
        break;

    case SYS_NH_CHANGE_TYPE_FWD_TO_DISCARD:
        CTC_ERROR_RETURN(_sys_greatbelt_nh_update_mpls_fwd_discard(lchip, (sys_nh_info_mpls_t*)p_nh_info, TRUE));
        break;

    case SYS_NH_CHANGE_TYPE_DISCARD_TO_FWD:
        CTC_ERROR_RETURN(_sys_greatbelt_nh_update_mpls_fwd_discard(lchip, (sys_nh_info_mpls_t*)p_nh_info, FALSE));
        break;
    case SYS_NH_CHANGE_TYPE_ADD_DYNTBL:
        {
  		  if (CTC_FLAG_ISSET(p_nh_db->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD))
            {
                return CTC_E_NONE;
            }
            _sys_greatbelt_nh_mpls_add_dsfwd(lchip, p_para, p_nh_db);
        }
    	break;
    default:
        return CTC_E_INVALID_PARAM;
    }

    if (p_nh_info->hdr.p_ref_nh_list)
    {
        sys_greatbelt_nh_update_ecmp_member(lchip, p_nh_para->hdr.nhid, p_nh_para->hdr.change_type);
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_mpls_nh_update_oam_en(uint8 lchip, uint32 nhid, uint32 outer_label, uint8 oam_en)
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

        cmd = DRV_IOR(DsL3EditMpls4W_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_nh_mpls_info->working_path.dsl3edit_offset, cmd, &dsl3edit));
        if (dsl3edit.label0 == outer_label)
        {
            dsl3edit.oam_en0 = oam_en ? 1 : 0;
            bFind = TRUE;
            cmd = DRV_IOW(DsL3EditMpls4W_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_nh_mpls_info->working_path.dsl3edit_offset, cmd, &dsl3edit));

        }

        if (!bFind
            && !CTC_FLAG_ISSET(p_nh_mpls_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_APS_SHARE_DSNH)
            && CTC_FLAG_ISSET(p_nh_mpls_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_APS_EN))
        {
            cmd = DRV_IOR(DsL3EditMpls4W_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_nh_mpls_info->protection_path->dsl3edit_offset, cmd, &dsl3edit));
            if (dsl3edit.label0 == outer_label)
            {
                dsl3edit.oam_en0 = oam_en ? 1 : 0;
                bFind = TRUE;
            }

            cmd = DRV_IOW(DsL3EditMpls4W_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_nh_mpls_info->protection_path->dsl3edit_offset, cmd, &dsl3edit));
        }
    }
    else if (!CTC_FLAG_ISSET(p_nh_mpls_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_APS_EN))
    {
        ds_l2_edit_eth8_w_t  ds_l2edit;
        ds_l3_edit_mpls8_w_t  ds_l3edit_mpls_8w;
        if (NULL == p_nh_mpls_info->working_path.p_mpls_tunnel )
        {  /*invalid nexthop */
            return CTC_E_INVALID_PTR;
        }
        if(p_nh_mpls_info->working_path.p_mpls_tunnel->p_dsl2edit)
        {
            return CTC_E_NONE;
        }
        cmd = DRV_IOR(DsL2EditEth8W_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_nh_mpls_info->working_path.p_mpls_tunnel->dsl2edit_offset, cmd, &ds_l2edit));
        sal_memcpy((uint8*)&ds_l3edit_mpls_8w + sizeof(ds_l3_edit_mpls4_w_t), (uint8*)&ds_l2edit + sizeof(ds_l3_edit_mpls4_w_t), sizeof(ds_l3_edit_mpls4_w_t));

        ds_l3edit_mpls_8w.oam_en2 = oam_en ? 1 : 0;
        sal_memcpy((uint8*)&ds_l2edit + sizeof(ds_l3_edit_mpls4_w_t), (uint8*)&ds_l3edit_mpls_8w + sizeof(ds_l3_edit_mpls4_w_t), sizeof(ds_l3_edit_mpls4_w_t));
        cmd = DRV_IOW(DsL2EditEth8W_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_nh_mpls_info->working_path.p_mpls_tunnel->dsl2edit_offset, cmd, &ds_l2edit));
        bFind = TRUE;
    }
    else
    { /*nexthop is tunnel nexthop & tunnel aps enable*/
        ds_l2_edit_eth8_w_t  ds_l2edit;
        ds_l3_edit_mpls8_w_t  ds_l3edit_mpls_8w;
        ds_aps_bridge_t ds_aps_bridge;

        sal_memset(&ds_aps_bridge, 0, sizeof(ds_aps_bridge_t));
        cmd  = DRV_IOR(DsApsBridge_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_nh_mpls_info->aps_group_id, cmd, &ds_aps_bridge));

        /*working path*/
        cmd = DRV_IOR(DsL2EditEth8W_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, ds_aps_bridge.working_l2_edit_ptr, cmd, &ds_l2edit));
        sal_memcpy((uint8*)&ds_l3edit_mpls_8w + sizeof(ds_l3_edit_mpls4_w_t), (uint8*)&ds_l2edit + sizeof(ds_l3_edit_mpls4_w_t), sizeof(ds_l3_edit_mpls4_w_t));

        if (ds_l3edit_mpls_8w.label_valid2 && ds_l3edit_mpls_8w.label2 == outer_label)
        {
            ds_l3edit_mpls_8w.oam_en2 = oam_en ? 1 : 0;
            sal_memcpy((uint8*)&ds_l2edit + sizeof(ds_l3_edit_mpls4_w_t), (uint8*)&ds_l3edit_mpls_8w + sizeof(ds_l3_edit_mpls4_w_t), sizeof(ds_l3_edit_mpls4_w_t));
            cmd = DRV_IOW(DsL2EditEth8W_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, ds_aps_bridge.working_l2_edit_ptr, cmd, &ds_l2edit));

            if (ds_aps_bridge.working_next_aps_bridge_en)
            {
                cmd = DRV_IOR(DsL2EditEth8W_t, DRV_ENTRY_FLAG);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, ds_aps_bridge.working_l2_edit_ptr + 1, cmd, &ds_l2edit));
                sal_memcpy((uint8*)&ds_l3edit_mpls_8w + sizeof(ds_l3_edit_mpls4_w_t), (uint8*)&ds_l2edit + sizeof(ds_l3_edit_mpls4_w_t), sizeof(ds_l3_edit_mpls4_w_t));
                ds_l3edit_mpls_8w.oam_en2 = oam_en ? 1 : 0;

                sal_memcpy((uint8*)&ds_l2edit + sizeof(ds_l3_edit_mpls4_w_t), (uint8*)&ds_l3edit_mpls_8w + sizeof(ds_l3_edit_mpls4_w_t), sizeof(ds_l3_edit_mpls4_w_t));
                cmd = DRV_IOW(DsL2EditEth8W_t, DRV_ENTRY_FLAG);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, ds_aps_bridge.working_l2_edit_ptr + 1, cmd, &ds_l2edit));
            }

            bFind = TRUE;
            return CTC_E_NONE;
        }

        /*protection path*/
        cmd = DRV_IOR(DsL2EditEth8W_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, ds_aps_bridge.protecting_l2_edit_ptr, cmd, &ds_l2edit));
        sal_memcpy((uint8*)&ds_l3edit_mpls_8w + sizeof(ds_l3_edit_mpls4_w_t), (uint8*)&ds_l2edit + sizeof(ds_l3_edit_mpls4_w_t), sizeof(ds_l3_edit_mpls4_w_t));

        if (ds_l3edit_mpls_8w.label_valid2 && ds_l3edit_mpls_8w.label2 == outer_label)
        {
            ds_l3edit_mpls_8w.oam_en2 = oam_en ? 1 : 0;
            sal_memcpy((uint8*)&ds_l2edit + sizeof(ds_l3_edit_mpls4_w_t), (uint8*)&ds_l3edit_mpls_8w + sizeof(ds_l3_edit_mpls4_w_t), sizeof(ds_l3_edit_mpls4_w_t));
            cmd = DRV_IOW(DsL2EditEth8W_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, ds_aps_bridge.protecting_l2_edit_ptr, cmd, &ds_l2edit));

            if (ds_aps_bridge.protecting_next_aps_bridge_en)
            {
                cmd = DRV_IOR(DsL2EditEth8W_t, DRV_ENTRY_FLAG);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, ds_aps_bridge.protecting_l2_edit_ptr + 1, cmd, &ds_l2edit));
                sal_memcpy((uint8*)&ds_l3edit_mpls_8w + sizeof(ds_l3_edit_mpls4_w_t), (uint8*)&ds_l2edit + sizeof(ds_l3_edit_mpls4_w_t), sizeof(ds_l3_edit_mpls4_w_t));
                ds_l3edit_mpls_8w.oam_en2 = oam_en ? 1 : 0;

                sal_memcpy((uint8*)&ds_l2edit + sizeof(ds_l3_edit_mpls4_w_t), (uint8*)&ds_l3edit_mpls_8w + sizeof(ds_l3_edit_mpls4_w_t), sizeof(ds_l3_edit_mpls4_w_t));
                cmd = DRV_IOW(DsL2EditEth8W_t, DRV_ENTRY_FLAG);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, ds_aps_bridge.protecting_l2_edit_ptr + 1, cmd, &ds_l2edit));
            }

            bFind = TRUE;
        }
    }


    if (!bFind)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_mpls_nh_tunnel_label_free_resource(uint8 lchip, uint16 tunnel_id,  sys_nh_db_mpls_tunnel_t* p_mpls_tunnel)
{

    if (CTC_FLAG_ISSET(p_mpls_tunnel->flag, SYS_NH_MPLS_TUNNEL_FLAG_TWO_LAYER_APS))
    {
        sys_greatbelt_nh_offset_free(lchip, \
                                     SYS_NH_ENTRY_TYPE_L2EDIT_ETH_8W, 2, p_mpls_tunnel->dsl2edit_offset);

        sys_greatbelt_nh_offset_free(lchip, \
                                     SYS_NH_ENTRY_TYPE_L2EDIT_ETH_8W, 2, p_mpls_tunnel->p_dsl2edit_offset);

    }
    else
    {
        if(p_mpls_tunnel->p_dsl2edit)
        {
            sys_greatbelt_nh_remove_route_l2edit(lchip, p_mpls_tunnel->p_dsl2edit);
            if (CTC_FLAG_ISSET(p_mpls_tunnel->flag, SYS_NH_MPLS_TUNNEL_FLAG_APS)
                && p_mpls_tunnel->p_p_dsl2edit)
            {
                sys_greatbelt_nh_remove_route_l2edit(lchip, p_mpls_tunnel->p_p_dsl2edit);
                CTC_UNSET_FLAG(p_mpls_tunnel->flag, SYS_NH_MPLS_TUNNEL_FLAG_APS);
            }
        }
        else
        {
            sys_greatbelt_nh_offset_free(lchip, \
                                         SYS_NH_ENTRY_TYPE_L2EDIT_ETH_8W, 1, p_mpls_tunnel->dsl2edit_offset);

            if (p_mpls_tunnel->stats_ptr)
            {
                p_mpls_tunnel->stats_ptr = 0;
            }

            if (CTC_FLAG_ISSET(p_mpls_tunnel->flag, SYS_NH_MPLS_TUNNEL_FLAG_APS))
            {
                sys_greatbelt_nh_offset_free(lchip, \
                                             SYS_NH_ENTRY_TYPE_L2EDIT_ETH_8W, 1, p_mpls_tunnel->p_dsl2edit_offset);

                if (p_mpls_tunnel->p_stats_ptr)
                {
                    p_mpls_tunnel->p_stats_ptr = 0;
                }
            }
        }
    }


    sys_greatbelt_nh_remove_mpls_tunnel(lchip, tunnel_id);
    mem_free(p_mpls_tunnel);
    return CTC_E_NONE;
}



int32
sys_greatbelt_mpls_nh_create_tunnel_mac(uint8 lchip, uint16 tunnel_id, ctc_mpls_nexthop_tunnel_param_t* p_nh_param)
{
    ctc_mpls_nexthop_tunnel_info_t* p_tunnel_info;
    sys_nh_db_mpls_tunnel_t* p_mpls_tunnel;
    int32     ret = 0;
    sys_l3if_prop_t l3if_prop;
    uint8    loop = 0, loop_cnt = 0;

    sys_nh_db_dsl2editeth4w_t dsl2edit_4w;
    sys_nh_db_dsl2editeth4w_t* p_dsl2edit_4w;


    if (CTC_FLAG_ISSET(p_nh_param->aps_flag, CTC_NH_MPLS_APS_EN))
    {
        CTC_APS_GROUP_ID_CHECK(p_nh_param->aps_bridge_group_id);
    }

    /* check label */

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    if (!CTC_FLAG_ISSET(p_nh_param->aps_flag, CTC_NH_MPLS_APS_EN))
    {
        CTC_ERROR_RETURN(sys_greatbelt_l3if_get_l3if_info_with_port_and_vlan(lchip, p_nh_param->nh_param.oif.gport, p_nh_param->nh_param.oif.vid, &l3if_prop));
    }
    else
    {
        bool protect_en = FALSE;
        CTC_ERROR_RETURN(sys_greatbelt_aps_get_aps_bridge(lchip, p_nh_param->aps_bridge_group_id, &protect_en));
    }

    CTC_ERROR_RETURN(sys_greatbelt_nh_lkup_mpls_tunnel(lchip, tunnel_id, &p_mpls_tunnel));

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
    }


    p_mpls_tunnel->tunnel_id = tunnel_id;
    p_tunnel_info = &p_nh_param->nh_param;
    loop_cnt = (CTC_FLAG_ISSET(p_nh_param->aps_flag, CTC_NH_MPLS_APS_EN)) ? 2 : 1;
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
    loop = 0;
    while (loop < loop_cnt)
    {
        sal_memset(&dsl2edit_4w, 0, sizeof(sys_nh_db_dsl2editeth4w_t));
        CTC_ERROR_GOTO(sys_greatbelt_l3if_get_l3if_info_with_port_and_vlan(lchip, p_tunnel_info->oif.gport, p_tunnel_info->oif.vid, &l3if_prop), ret, error_proc);
        if (l3if_prop.l3if_type == CTC_L3IF_TYPE_PHY_IF && p_tunnel_info->oif.vid)
        {
            dsl2edit_4w.output_vid           = p_tunnel_info->oif.vid;
            dsl2edit_4w.output_vlan_is_svlan = 1 ;
        }
        sal_memcpy(&dsl2edit_4w.mac_da, &p_tunnel_info->mac, sizeof(mac_addr_t));
        if (CTC_L3IF_TYPE_PHY_IF == l3if_prop.l3if_type)
        {
            dsl2edit_4w.phy_if    = 1;
        }

        p_dsl2edit_4w = &dsl2edit_4w;
        ret = sys_greatbelt_nh_add_route_l2edit(lchip, (sys_nh_db_dsl2editeth4w_t**)&p_dsl2edit_4w);
        if (ret != CTC_E_NONE)
        {
            goto error_proc;
        }

        if (loop == 0)
        {
            p_mpls_tunnel->dsl2edit_offset =  p_dsl2edit_4w->offset;
            p_mpls_tunnel->p_dsl2edit        = p_dsl2edit_4w;
        }
        else
        {
            p_mpls_tunnel->p_dsl2edit_offset =  p_dsl2edit_4w->offset;
            p_mpls_tunnel->p_p_dsl2edit        = p_dsl2edit_4w;
        }
        if ((CTC_FLAG_ISSET(p_nh_param->aps_flag, CTC_NH_MPLS_APS_EN)))
        { /*0==loop :working path*/
            sys_greatbelt_aps_update_l2edit(lchip, p_nh_param->aps_bridge_group_id, p_dsl2edit_4w->offset, (1 == loop));
        }
        p_tunnel_info = &p_nh_param->nh_p_param;
        loop++;
    };

    if (CTC_FLAG_ISSET(p_nh_param->aps_flag, CTC_NH_MPLS_APS_EN))
    {
        p_mpls_tunnel->gport = p_nh_param->aps_bridge_group_id;
        p_mpls_tunnel->flag  |= SYS_NH_MPLS_TUNNEL_FLAG_APS;
    }
    else
    {
        p_mpls_tunnel->gport = p_nh_param->nh_param.oif.gport;
        p_mpls_tunnel->l3ifid = l3if_prop.l3if_id;
    }

    ret = sys_greatbelt_nh_add_mpls_tunnel(lchip, tunnel_id, p_mpls_tunnel);
    if (ret == CTC_E_NONE)
    {
        return CTC_E_NONE;
    }

error_proc:
    sys_greatbelt_mpls_nh_tunnel_label_free_resource(lchip, tunnel_id, p_mpls_tunnel);
    return ret;
}


int32
sys_greatbelt_mpls_nh_create_tunnel_label_mac(uint8 lchip, uint16 tunnel_id, ctc_mpls_nexthop_tunnel_param_t* p_nh_param)
{
    ctc_mpls_nexthop_tunnel_info_t* p_tunnel_info;
    sys_nh_db_mpls_tunnel_t* p_mpls_tunnel;
    ds_l2_edit_eth8_w_t ds_l2edit;
    uint32   l2edit_offset;
    uint16  next_aps_id = 0;
    int32     ret = 0;
    uint8    loop = 0, loop_cnt = 0;
    sys_l3if_prop_t l3if_prop;
    uint16  stats_ptr = 0;

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
        CTC_APS_GROUP_ID_CHECK(p_nh_param->aps_bridge_group_id);
    }


    sal_memset(&ds_l2edit, 0, sizeof(ds_l2_edit_eth8_w_t));

    if (!CTC_FLAG_ISSET(p_nh_param->aps_flag, CTC_NH_MPLS_APS_EN))
    {
        CTC_ERROR_RETURN(sys_greatbelt_l3if_get_l3if_info_with_port_and_vlan(lchip, p_nh_param->nh_param.oif.gport, p_nh_param->nh_param.oif.vid, &l3if_prop));
    }
    else
    {
        bool protect_en = FALSE;
        CTC_ERROR_RETURN(sys_greatbelt_aps_get_aps_bridge(lchip, p_nh_param->aps_bridge_group_id, &protect_en));
    }

    CTC_ERROR_RETURN(sys_greatbelt_nh_lkup_mpls_tunnel(lchip, tunnel_id, &p_mpls_tunnel));

    if (p_mpls_tunnel)
    {
        if (!CTC_FLAG_ISSET(p_mpls_tunnel->flag, SYS_NH_MPLS_TUNNEL_FLAG_TWO_LAYER_APS))
        {
            return CTC_E_ENTRY_EXIST;
        }
        else if ((CTC_FLAG_ISSET(p_mpls_tunnel->flag, SYS_NH_MPLS_TUNNEL_FLAG_WORKING) == !CTC_FLAG_ISSET(p_nh_param->aps_flag, CTC_NH_MPLS_APS_2_LAYER_EN_PROTECTION))
                 || (CTC_FLAG_ISSET(p_mpls_tunnel->flag, SYS_NH_MPLS_TUNNEL_FLAG_PROTECTION) == CTC_FLAG_ISSET(p_nh_param->aps_flag, CTC_NH_MPLS_APS_2_LAYER_EN_PROTECTION)))
        {
            return CTC_E_ENTRY_EXIST;
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

    }

    p_mpls_tunnel->tunnel_id = tunnel_id;

    l2edit_offset  = 0;

    p_tunnel_info = &p_nh_param->nh_param;
    loop_cnt = (CTC_FLAG_ISSET(p_nh_param->aps_flag, CTC_NH_MPLS_APS_EN)) ? 2 : 1;
    loop = 0;

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
        if (CTC_FLAG_ISSET(p_nh_param->aps_flag, CTC_NH_MPLS_APS_2_LAYER_EN))
        {
            p_mpls_tunnel->flag  |= SYS_NH_MPLS_TUNNEL_FLAG_TWO_LAYER_APS;
            /*  need allocate 2 ds8w ,working path's L2EditPtred 2 and protection path's L2EditPtr can be independent,
            but working path (protection path) need two continuous l2editPtr */
            if (0 == loop)
            {
                ret = sys_greatbelt_nh_offset_alloc_with_multiple(lchip, SYS_NH_ENTRY_TYPE_L2EDIT_ETH_8W, 2, 2, &l2edit_offset);
                if (ret != CTC_E_NONE)
                {
                    goto error_proc;
                }

                if (!CTC_FLAG_ISSET(p_nh_param->aps_flag, CTC_NH_MPLS_APS_2_LAYER_EN_PROTECTION))
                {
                    p_mpls_tunnel->dsl2edit_offset = l2edit_offset;
                    sys_greatbelt_aps_get_next_aps_working_path(lchip, p_nh_param->aps_bridge_group_id, &next_aps_id);
                    p_mpls_tunnel->flag  |= SYS_NH_MPLS_TUNNEL_FLAG_WORKING;
                }
                else
                {
                    p_mpls_tunnel->p_dsl2edit_offset = l2edit_offset;
                    sys_greatbelt_aps_get_next_aps_protecting_path(lchip, p_nh_param->aps_bridge_group_id, &next_aps_id);
                    p_mpls_tunnel->flag  |= SYS_NH_MPLS_TUNNEL_FLAG_PROTECTION;
                }
                sys_greatbelt_aps_set_l2_shift(lchip, next_aps_id, TRUE);
            }
            else
            {
                l2edit_offset += 2;
            }

        }
        else
        { /* only allocate 1 ds8w ,working path and protection path's L2EditPtr can be independent*/
            ret = sys_greatbelt_nh_offset_alloc_with_multiple(lchip, SYS_NH_ENTRY_TYPE_L2EDIT_ETH_8W, 1, 2, &l2edit_offset);
            if (ret != CTC_E_NONE)
            {
                goto error_proc;
            }

            /*working path*/
            if (loop == 0)
            {
                p_mpls_tunnel->dsl2edit_offset = l2edit_offset;
            }
            else
            {
                p_mpls_tunnel->p_dsl2edit_offset = l2edit_offset;
            }
        }

        ret = _sys_greatbelt_nh_mpls_build_l2edit(lchip, p_tunnel_info, p_tunnel_info->label_num, &ds_l2edit, &stats_ptr);
        if (ret != CTC_E_NONE)
        {
            goto error_proc;
        }

        if (loop == 0)
        {   /*working path*/
            p_mpls_tunnel->stats_ptr = stats_ptr;
        }
        else
        {
            p_mpls_tunnel->p_stats_ptr = stats_ptr;
        }

        ret = sys_greatbelt_nh_write_asic_table(lchip, SYS_NH_ENTRY_TYPE_L2EDIT_ETH_8W,
                                                l2edit_offset, &ds_l2edit);
        if (ret != CTC_E_NONE)
        {
            goto error_proc;
        }

        /* call aps function to write dsapsbridge l2edit */
        if (CTC_FLAG_ISSET(p_nh_param->aps_flag, CTC_NH_MPLS_APS_2_LAYER_EN))
        {
            if (0 == loop) /*working tunnel*/
            {
                sys_greatbelt_aps_update_l2edit(lchip, p_nh_param->aps_bridge_group_id, l2edit_offset,
                                                CTC_FLAG_ISSET(p_nh_param->aps_flag, CTC_NH_MPLS_APS_2_LAYER_EN_PROTECTION));
            }
        }
        else if ((CTC_FLAG_ISSET(p_nh_param->aps_flag, CTC_NH_MPLS_APS_EN)))
        { /*0==loop :working path*/
            sys_greatbelt_aps_update_l2edit(lchip, p_nh_param->aps_bridge_group_id, l2edit_offset, (1 == loop));

        }

        p_tunnel_info = &p_nh_param->nh_p_param;
        loop++;
    }


    if (CTC_FLAG_ISSET(p_nh_param->aps_flag, CTC_NH_MPLS_APS_EN))
    {
        p_mpls_tunnel->gport = p_nh_param->aps_bridge_group_id;
        p_mpls_tunnel->flag  |= SYS_NH_MPLS_TUNNEL_FLAG_APS;
    }
    else
    {
        p_mpls_tunnel->gport = p_nh_param->nh_param.oif.gport;
        p_mpls_tunnel->l3ifid = l3if_prop.l3if_id;
    }


    ret = sys_greatbelt_nh_add_mpls_tunnel(lchip, tunnel_id, p_mpls_tunnel);
    if (ret == CTC_E_NONE)
    {
        return CTC_E_NONE;
    }

error_proc:
    sys_greatbelt_mpls_nh_tunnel_label_free_resource(lchip, tunnel_id, p_mpls_tunnel);
    return ret;
}



int32
sys_greatbelt_mpls_nh_create_tunnel_label(uint8 lchip, uint16 tunnel_id, ctc_mpls_nexthop_tunnel_param_t* p_nh_param)
{
    int32 ret = CTC_E_NONE;
    if((p_nh_param->nh_param.label_num == 0)
        && (!CTC_FLAG_ISSET(p_nh_param->aps_flag, CTC_NH_MPLS_APS_EN)||(CTC_FLAG_ISSET(p_nh_param->aps_flag, CTC_NH_MPLS_APS_EN) && (p_nh_param->nh_p_param.label_num == 0))))
    {
        ret = sys_greatbelt_mpls_nh_create_tunnel_mac(lchip, tunnel_id, p_nh_param);
    }
    else
    {
        ret = sys_greatbelt_mpls_nh_create_tunnel_label_mac(lchip, tunnel_id, p_nh_param);
    }

    return ret;
}


int32
sys_greatbelt_mpls_nh_remove_tunnel_label(uint8 lchip, uint16 tunnel_id)
{

    sys_nh_db_mpls_tunnel_t* p_mpls_tunnel;

    CTC_ERROR_RETURN(sys_greatbelt_nh_lkup_mpls_tunnel(lchip, tunnel_id, &p_mpls_tunnel));

    if (p_mpls_tunnel && p_mpls_tunnel->ref_cnt != 0)
    {
        return CTC_E_NH_EXIST_VC_LABEL;
    }

    if (!p_mpls_tunnel)
    {
        return CTC_E_NH_NOT_EXIST;
    }

    sys_greatbelt_mpls_nh_tunnel_label_free_resource(lchip, tunnel_id, p_mpls_tunnel);

    return CTC_E_NONE;
}

int32
sys_greatbelt_mpls_nh_update_tunnel_label_mac(uint8 lchip, uint16 tunnel_id, ctc_mpls_nexthop_tunnel_param_t* p_tunnel_param)
{
    sys_nh_db_mpls_tunnel_t* p_mpls_tunnel;
    ds_l2_edit_eth8_w_t ds_l2edit;
    uint8 loop = 0, loop_cnt = 0;
    ctc_mpls_nexthop_tunnel_info_t* p_tunnel_info;
    uint32   l2edit_offset = 0;
    sys_l3if_prop_t l3if_prop;
    uint16  stats_ptr = 0;



    CTC_ERROR_RETURN(sys_greatbelt_nh_lkup_mpls_tunnel(lchip, tunnel_id, &p_mpls_tunnel));
    if (!p_mpls_tunnel)
    {
        return CTC_E_NH_NOT_EXIST;
    }

    if ((CTC_FLAG_ISSET(p_mpls_tunnel->flag, SYS_NH_MPLS_TUNNEL_FLAG_TWO_LAYER_APS) !=
            CTC_FLAG_ISSET(p_tunnel_param->aps_flag, CTC_NH_MPLS_APS_2_LAYER_EN))
        || (CTC_FLAG_ISSET(p_mpls_tunnel->flag, SYS_NH_MPLS_TUNNEL_FLAG_APS) !=
            CTC_FLAG_ISSET(p_tunnel_param->aps_flag, CTC_NH_MPLS_APS_EN))
        || (CTC_FLAG_ISSET(p_mpls_tunnel->flag, SYS_NH_MPLS_TUNNEL_FLAG_APS)
            && p_tunnel_param->aps_bridge_group_id != p_mpls_tunnel->gport))
    {
        return CTC_E_INVALID_PARAM;
    }

    if (!CTC_FLAG_ISSET(p_tunnel_param->aps_flag, CTC_NH_MPLS_APS_EN))
    {
        CTC_ERROR_RETURN(sys_greatbelt_l3if_get_l3if_info_with_port_and_vlan(lchip, p_tunnel_param->nh_param.oif.gport, p_tunnel_param->nh_param.oif.vid, &l3if_prop));
    }
    else
    {
        bool protect_en = FALSE;
        CTC_ERROR_RETURN(sys_greatbelt_aps_get_aps_bridge(lchip, p_tunnel_param->aps_bridge_group_id, &protect_en));

    }



    p_tunnel_info = &p_tunnel_param->nh_param;
    loop = 0;
    if(CTC_FLAG_ISSET(p_tunnel_param->aps_flag, CTC_NH_MPLS_APS_EN))
    {
        loop_cnt = (p_tunnel_param->nh_param.label_num !=0 ) + (p_tunnel_param->nh_p_param.label_num !=0 ) ;
        if(p_tunnel_param->nh_param.label_num ==0 )
        {
            loop = 1;
            loop_cnt = 2;
            p_tunnel_info = &p_tunnel_param->nh_p_param;
        }

    }
    else
    {
        loop_cnt = 1;
    }


     /*mpls nexthop must use egress chip edit in EDIT_MODE = 1 & 2*/
    if ((SYS_NH_EDIT_MODE() != SYS_NH_IGS_CHIP_EDIT_MODE) &&
        (!CTC_FLAG_ISSET(p_tunnel_param->aps_flag, CTC_NH_MPLS_APS_EN) ))
    {
        uint8 gchip = 0;
        gchip = CTC_MAP_GPORT_TO_GCHIP(p_tunnel_param->nh_param.oif.gport);

        if (SYS_GCHIP_IS_REMOTE(lchip, gchip))
        {
            loop_cnt = 0;
        }
    }


    do
    {
        sal_memset(&ds_l2edit, 0, sizeof(ds_l2_edit_eth8_w_t));

        if (CTC_FLAG_ISSET(p_tunnel_param->aps_flag, CTC_NH_MPLS_APS_2_LAYER_EN))
        {
            /*  need allocate 2 ds8w ,working path's L2EditPtred 2 and protection path's L2EditPtr can be independent,
            but working path (protection path) need two continuous l2editPtr */
            if (0 == loop)
            {
                if (!CTC_FLAG_ISSET(p_tunnel_param->aps_flag, CTC_NH_MPLS_APS_2_LAYER_EN_PROTECTION))
                { /*working path*/
                    l2edit_offset = p_mpls_tunnel->dsl2edit_offset;
                }
                else
                {
                    l2edit_offset = p_mpls_tunnel->p_dsl2edit_offset;
                }
            }
            else
            {
                l2edit_offset += 2;
            }
        }
        else
        {
            if (loop == 0)
            { /*working path*/
                l2edit_offset = p_mpls_tunnel->dsl2edit_offset;
            }
            else
            {
                l2edit_offset = p_mpls_tunnel->p_dsl2edit_offset;
            }
        }

        if (loop == 0)
        {   /*working path*/
            stats_ptr = p_mpls_tunnel->stats_ptr;
            p_mpls_tunnel->stats_ptr = 0;
        }
        else
        {
            stats_ptr = p_mpls_tunnel->p_stats_ptr;
            p_mpls_tunnel->p_stats_ptr = 0;
        }
        if (stats_ptr)
        {
            stats_ptr = 0;
        }

        CTC_ERROR_RETURN(_sys_greatbelt_nh_mpls_build_l2edit(lchip, p_tunnel_info, p_tunnel_info->label_num, &ds_l2edit, &stats_ptr));
        if (loop == 0)
        {   /*working path*/
            p_mpls_tunnel->stats_ptr = stats_ptr;
        }
        else
        {
            p_mpls_tunnel->p_stats_ptr = stats_ptr;
        }
        CTC_ERROR_RETURN(sys_greatbelt_nh_write_asic_table(lchip, SYS_NH_ENTRY_TYPE_L2EDIT_ETH_8W,
                                                           l2edit_offset, &ds_l2edit));

        /* call aps function to write dsapsbridge l2edit */
        if (CTC_FLAG_ISSET(p_tunnel_param->aps_flag, CTC_NH_MPLS_APS_2_LAYER_EN))
        {
            if (0 == loop)
            {
                sys_greatbelt_aps_update_l2edit(lchip, p_tunnel_param->aps_bridge_group_id, l2edit_offset,
                                                CTC_FLAG_ISSET(p_tunnel_param->aps_flag, CTC_NH_MPLS_APS_2_LAYER_EN_PROTECTION));
            }
        }
        else if ((CTC_FLAG_ISSET(p_tunnel_param->aps_flag, CTC_NH_MPLS_APS_EN)))
        {
            sys_greatbelt_aps_update_l2edit(lchip, p_tunnel_param->aps_bridge_group_id, l2edit_offset, (1 == loop));
        }
        p_tunnel_info = &p_tunnel_param->nh_p_param;
        loop++;
    }
    while (loop < loop_cnt);


    if (CTC_FLAG_ISSET(p_tunnel_param->aps_flag, CTC_NH_MPLS_APS_EN))
    {
        p_mpls_tunnel->gport = p_tunnel_param->aps_bridge_group_id;
    }
    else
    {
        p_mpls_tunnel->gport = p_tunnel_param->nh_param.oif.gport;
        p_mpls_tunnel->l3ifid = l3if_prop.l3if_id;
    }

    return CTC_E_NONE;
}



int32
sys_greatbelt_mpls_nh_update_tunnel_mac(uint8 lchip, uint16 tunnel_id, ctc_mpls_nexthop_tunnel_param_t* p_tunnel_param)
{
    ctc_mpls_nexthop_tunnel_info_t* p_tunnel_info;
    sys_nh_db_mpls_tunnel_t* p_mpls_tunnel;
    int32     ret = 0;
    sys_l3if_prop_t l3if_prop;

    sys_nh_db_dsl2editeth4w_t dsl2edit_4w;
    sys_nh_db_dsl2editeth4w_t* p_dsl2edit_4w;
    uint8 do_nh_edit = 1;
    p_dsl2edit_4w = &dsl2edit_4w;

    /* check label */

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    CTC_ERROR_RETURN(sys_greatbelt_l3if_get_l3if_info_with_port_and_vlan(lchip, p_tunnel_param->nh_param.oif.gport, p_tunnel_param->nh_param.oif.vid, &l3if_prop));

    CTC_ERROR_RETURN(sys_greatbelt_nh_lkup_mpls_tunnel(lchip, tunnel_id, &p_mpls_tunnel));

    if (!p_mpls_tunnel)
    {
        return CTC_E_NH_NOT_EXIST;
    }
    p_tunnel_info = &p_tunnel_param->nh_param;



    if ((SYS_NH_EDIT_MODE() != SYS_NH_IGS_CHIP_EDIT_MODE) &&
        (!CTC_FLAG_ISSET(p_tunnel_param->aps_flag, CTC_NH_MPLS_APS_EN) ))
    {
        uint8 gchip = 0;

        gchip = CTC_MAP_GPORT_TO_GCHIP(p_tunnel_param->nh_param.oif.gport);

        if (SYS_GCHIP_IS_REMOTE(lchip, gchip))
        {
            do_nh_edit = 0;
        }
    }

    if (do_nh_edit)
    {
        sal_memset(&dsl2edit_4w, 0, sizeof(sys_nh_db_dsl2editeth4w_t));
        if (l3if_prop.l3if_type == CTC_L3IF_TYPE_PHY_IF && p_tunnel_info->oif.vid)
        {
            dsl2edit_4w.output_vid           = p_tunnel_info->oif.vid;
            dsl2edit_4w.output_vlan_is_svlan = 1 ;
        }

        if (CTC_L3IF_TYPE_PHY_IF == l3if_prop.l3if_type)
        {
            dsl2edit_4w.phy_if    = 1;
        }
        sal_memcpy(&dsl2edit_4w.mac_da, &p_tunnel_info->mac, sizeof(mac_addr_t));


        if (p_mpls_tunnel->p_dsl2edit)
        {
            sys_greatbelt_nh_remove_route_l2edit(lchip, p_mpls_tunnel->p_dsl2edit);
        }
        if (CTC_FLAG_ISSET(p_mpls_tunnel->flag, SYS_NH_MPLS_TUNNEL_FLAG_APS))
        {
            sys_greatbelt_nh_remove_route_l2edit(lchip, p_mpls_tunnel->p_p_dsl2edit);
            CTC_UNSET_FLAG(p_mpls_tunnel->flag, SYS_NH_MPLS_TUNNEL_FLAG_APS);
        }
        p_dsl2edit_4w = &dsl2edit_4w;
        ret = sys_greatbelt_nh_add_route_l2edit(lchip, (sys_nh_db_dsl2editeth4w_t**)&p_dsl2edit_4w);
        if (ret != CTC_E_NONE)
        {
            goto error_proc;
        }
        p_mpls_tunnel->dsl2edit_offset   = p_dsl2edit_4w->offset;
        p_mpls_tunnel->p_dsl2edit        = p_dsl2edit_4w;
    }


    p_mpls_tunnel->gport = p_tunnel_param->nh_param.oif.gport;
    p_mpls_tunnel->l3ifid = l3if_prop.l3if_id;


    if (ret == CTC_E_NONE)
    {
        return CTC_E_NONE;
    }

error_proc:
    sys_greatbelt_mpls_nh_tunnel_label_free_resource(lchip, tunnel_id, p_mpls_tunnel);
    return ret;

}

int32
sys_greatbelt_mpls_nh_update_tunnel_label(uint8 lchip, uint16 tunnel_id, ctc_mpls_nexthop_tunnel_param_t* p_tunnel_param)
{
    int32 ret = CTC_E_NONE;
    if((p_tunnel_param->nh_param.label_num == 0)
        && (!CTC_FLAG_ISSET(p_tunnel_param->aps_flag, CTC_NH_MPLS_APS_EN)||(CTC_FLAG_ISSET(p_tunnel_param->aps_flag, CTC_NH_MPLS_APS_EN) && (p_tunnel_param->nh_p_param.label_num == 0))))
    {
        ret = sys_greatbelt_mpls_nh_update_tunnel_mac(lchip, tunnel_id, p_tunnel_param);
    }
    else
    {
        ret = sys_greatbelt_mpls_nh_update_tunnel_label_mac(lchip, tunnel_id, p_tunnel_param);
    }
    return ret;
}

int32
sys_greatbelt_mpls_nh_swap_tunnel(uint8 lchip, uint16 old_tunnel_id, uint16 new_tunnel_id)
{
    sys_nh_db_mpls_tunnel_t* p_old_mpls_tunnel;
    sys_nh_db_mpls_tunnel_t* p_new_mpls_tunnel;
    sys_nh_db_mpls_tunnel_t tmp_mpls_tunnel;

    CTC_ERROR_RETURN(sys_greatbelt_nh_lkup_mpls_tunnel(lchip, old_tunnel_id, &p_old_mpls_tunnel));
    if (!p_old_mpls_tunnel)
    {
        return CTC_E_NH_NOT_EXIST;
    }

    CTC_ERROR_RETURN(sys_greatbelt_nh_lkup_mpls_tunnel(lchip, new_tunnel_id, &p_new_mpls_tunnel));
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




#include "ctc_const.h"
#include "ctc_error.h"
#include "ctc_common.h"
#include "ctc_packet.h"


#include "sys_goldengate_chip.h"
#include "sys_goldengate_common.h"

#include "sys_goldengate_nexthop_api.h"
#include "sys_goldengate_nexthop_hw.h"
#include "sys_goldengate_nexthop.h"
#include  "sys_goldengate_cpu_reason.h"

#include "goldengate/include/drv_enum.h"
#include "goldengate/include/drv_io.h"


typedef int32 (*sys_goldengate_nh_fun)(uint8, void*, void*);
typedef int32 (*sys_goldengate_nh_update)(uint8, void*);

int32
sys_goldengate_nh_map_dsfwd(uint8 lchip, sys_fwd_t* p_sys_fwd, void *p_ds)
{
    DsFwd_m dsfwd;

    sal_memset(&dsfwd, 0, sizeof(dsfwd));

    CTC_ERROR_RETURN(sys_goldengate_nh_get_asic_table(lchip, SYS_NH_ENTRY_TYPE_FWD,
                                                       p_sys_fwd->offset / 2, &dsfwd));

    if (p_sys_fwd->offset % 2 == 0)
    {
        SetDsFwd(V, array_0_forceBackEn_f,         &dsfwd,   p_sys_fwd->force_back_en);
        SetDsFwd(V, array_0_bypassIngressEdit_f,   &dsfwd,   p_sys_fwd->bypass_igs_edit);
        SetDsFwd(V, array_0_lengthAdjustType_f,    &dsfwd,   p_sys_fwd->adjust_len_idx);
        SetDsFwd(V, array_0_criticalPacket_f,      &dsfwd,   p_sys_fwd->critical_packet);
        SetDsFwd(V, array_0_nextHopExt_f,          &dsfwd,   p_sys_fwd->nexthop_ext);
        SetDsFwd(V, array_0_sendLocalPhyPort_f,    &dsfwd,   p_sys_fwd->lport_en);
        SetDsFwd(V, array_0_apsBridgeEn_f,         &dsfwd,   p_sys_fwd->aps_bridge_en);
        SetDsFwd(V, array_0_destMap_f,             &dsfwd,   p_sys_fwd->dest_map);
        SetDsFwd(V, array_0_nextHopPtr_f,          &dsfwd,   p_sys_fwd->nexthop_ptr);
        SetDsFwd(V, array_0_careLength_f,          &dsfwd,   p_sys_fwd->cut_through_dis);
    }
    else
    {
        SetDsFwd(V, array_1_forceBackEn_f,         &dsfwd,   p_sys_fwd->force_back_en);
        SetDsFwd(V, array_1_bypassIngressEdit_f,   &dsfwd,   p_sys_fwd->bypass_igs_edit);
        SetDsFwd(V, array_1_lengthAdjustType_f,    &dsfwd,   p_sys_fwd->adjust_len_idx);
        SetDsFwd(V, array_1_criticalPacket_f,      &dsfwd,   p_sys_fwd->critical_packet);
        SetDsFwd(V, array_1_nextHopExt_f,          &dsfwd,   p_sys_fwd->nexthop_ext);
        SetDsFwd(V, array_1_sendLocalPhyPort_f,    &dsfwd,   p_sys_fwd->lport_en);
        SetDsFwd(V, array_1_apsBridgeEn_f,         &dsfwd,   p_sys_fwd->aps_bridge_en);
        SetDsFwd(V, array_1_destMap_f,             &dsfwd,   p_sys_fwd->dest_map);
        SetDsFwd(V, array_1_nextHopPtr_f,          &dsfwd,   p_sys_fwd->nexthop_ptr);
        SetDsFwd(V, array_1_careLength_f,          &dsfwd,   p_sys_fwd->cut_through_dis);
    }
    sal_memcpy(p_ds , &dsfwd, sizeof(dsfwd));

    return CTC_E_NONE;

}

int32
sys_goldengate_nh_map_dsnexthop(uint8 lchip, sys_nexthop_t* p_sys_dsnh, void *p_ds)
{
    DsNextHop4W_m dsnh4w;
    DsNextHop8W_m dsnh8w;
    uint32 offset = 0;
    uint8 do_vlan_edit = 0;
     hw_mac_addr_t mac_da;
    ctc_chip_device_info_t device_info;

    CTC_ERROR_RETURN(sys_goldengate_chip_get_device_info(lchip, &device_info));

    sal_memset(&dsnh4w, 0, sizeof(dsnh4w));
    sal_memset(&dsnh8w, 0, sizeof(dsnh8w));

    /*----------mapping dsnexthop4w---------------*/
    if (p_sys_dsnh->is_drop)
    {
        SetDsNextHop4W(V, isMet_f,              &dsnh4w,   1);
    }
    else
    {
        SetDsNextHop4W(V, isMet_f,              &dsnh4w,   0);
    }
    SetDsNextHop4W(V, isNexthop8W_f,        &dsnh4w,   p_sys_dsnh->is_nexthop8_w);
    SetDsNextHop4W(V, shareType_f,          &dsnh4w,   p_sys_dsnh->span_en ? SYS_NH_SHARE_TYPE_SPAN_PACKET : p_sys_dsnh->share_type);
    SetDsNextHop4W(V, apsBridgeEn_f,        &dsnh4w,   p_sys_dsnh->aps_bridge_en);
    SetDsNextHop4W(V, payloadOperation_f,   &dsnh4w,   p_sys_dsnh->payload_operation);
    SetDsNextHop4W(V, destVlanPtr_f,        &dsnh4w,   p_sys_dsnh->dest_vlan_ptr);
    SetDsNextHop4W(V, isLeaf_f,             &dsnh4w,   p_sys_dsnh->is_leaf);
    SetDsNextHop4W(V, mtuCheckEn_f,         &dsnh4w,   p_sys_dsnh->mtu_check_en);
    SetDsNextHop4W(V, svlanTagged_f,        &dsnh4w,   p_sys_dsnh->svlan_tagged);
    SetDsNextHop4W(V, vlanXlateMode_f,      &dsnh4w,   p_sys_dsnh->vlan_xlate_mode);
    SetDsNextHop4W(V, statsPtr6_0_f,        &dsnh4w,   p_sys_dsnh->stats_ptr);
    SetDsNextHop4W(V, statsPtr13_f,         &dsnh4w,   (p_sys_dsnh->stats_ptr >> 13));
    SetDsNextHop4W(V, replaceDscp_f,        &dsnh4w,   p_sys_dsnh->replace_dscp);
    SetDsNextHop4W(V, copyDscp_f,           &dsnh4w,   p_sys_dsnh->copy_dscp);

    switch(p_sys_dsnh->share_type)
    {
    case SYS_NH_SHARE_TYPE_L23EDIT:
        if ( !p_sys_dsnh->is_nexthop8_w)
        {
            SetDsNextHop4W(V, u1_g1_statsPtr12_7_f,        &dsnh4w,   (p_sys_dsnh->stats_ptr >> 7));
            SetDsNextHop4W(V, u1_g1_cwIndex_f,             &dsnh4w,   p_sys_dsnh->cw_index);
            SetDsNextHop4W(V, u1_g1_outerEditLocation_f,   &dsnh4w,   p_sys_dsnh->outer_edit_location);
            SetDsNextHop4W(V, u1_g1_outerEditPtrType_f,    &dsnh4w,   p_sys_dsnh->outer_edit_ptr_type);
            SetDsNextHop4W(V, u1_g1_innerEditPtrType_f,    &dsnh4w,   p_sys_dsnh->inner_edit_ptr_type);
            SetDsNextHop4W(V, u1_g1_outerEditPtr_f,        &dsnh4w,   p_sys_dsnh->outer_edit_ptr);
            SetDsNextHop4W(V, u1_g1_innerEditPtr_f,        &dsnh4w,   p_sys_dsnh->inner_edit_ptr);
        }
        else if (p_sys_dsnh->vlan_xlate_mode == 1)
        {
            do_vlan_edit = 1;
        }
        break;

    case SYS_NH_SHARE_TYPE_VLANTAG:
        do_vlan_edit = 1;
        break;

    case SYS_NH_SHARE_TYPE_MAC_DA:
        if ( !p_sys_dsnh->is_nexthop8_w)
        {
            SYS_GOLDENGATE_SET_HW_MAC(mac_da,  p_sys_dsnh->mac_da)
            SetDsNextHop4W(A, u1_g3_macDa_f,         &dsnh4w,   mac_da);
             /*SetDsNextHop4W(V, u1_g3_macDaMcastMode_f,     &dsnh4w,   0);*/
            SetDsNextHop4W(V, u1_g3_useTtlFromPacket_f,   &dsnh4w,   p_sys_dsnh->use_packet_ttl);
            SetDsNextHop4W(V, u1_g3_ttlNoDecrease_f,         &dsnh4w,   p_sys_dsnh->ttl_no_dec);
        }
        break;

    case SYS_NH_SHARE_TYPE_L2EDIT_VLAN:
        if ( !p_sys_dsnh->is_nexthop8_w)
        {
            SetDsNextHop4W(V, u1_g4_statsPtr12_7_f,         &dsnh4w,   (p_sys_dsnh->stats_ptr >> 7));
            SetDsNextHop4W(V, u1_g4_outerEditPtrValid_f,    &dsnh4w,   p_sys_dsnh->outer_edit_valid);
            SetDsNextHop4W(V, u1_g4_outerEditLocation_f,    &dsnh4w,   p_sys_dsnh->outer_edit_location);
            SetDsNextHop4W(V, u1_g4_outerEditPtrType_f,     &dsnh4w,   p_sys_dsnh->outer_edit_ptr_type);
            SetDsNextHop4W(V, u1_g4_outerEditPtr_f,         &dsnh4w,   p_sys_dsnh->outer_edit_ptr);
            SetDsNextHop4W(V, u1_g4_svlanTagDisable_f,      &dsnh4w,   p_sys_dsnh->svlan_tag_disable);
            SetDsNextHop4W(V, u1_g4_taggedMode_f,           &dsnh4w,   p_sys_dsnh->tagged_mode);
            SetDsNextHop4W(V, u1_g4_outputSvlanIdValid_f,   &dsnh4w,   p_sys_dsnh->output_svlan_id_valid);
            SetDsNextHop4W(V, u1_g4_outputSvlanId_f,        &dsnh4w,   p_sys_dsnh->output_svlan_id);
            SetDsNextHop4W(V, u1_g4_replaceStagCos_f,       &dsnh4w,   p_sys_dsnh->replace_stag_cos);
            SetDsNextHop4W(V, u1_g4_deriveStagCos_f,        &dsnh4w,   p_sys_dsnh->derive_stag_cos);
            SetDsNextHop4W(V, u1_g4_stagCfi_f,              &dsnh4w,   p_sys_dsnh->stag_cfi);
            SetDsNextHop4W(V, u1_g4_stagCos_f,              &dsnh4w,   p_sys_dsnh->stag_cos);
            SetDsNextHop4W(V, u1_g4_svlanTpid_f,            &dsnh4w,   p_sys_dsnh->svlan_tpid);
            SetDsNextHop4W(V, u1_g4_svlanTpidEn_f,          &dsnh4w,   p_sys_dsnh->svlan_tpid_en);
            SetDsNextHop4W(V, u1_g4_useTtlFromPacket_f,     &dsnh4w,   p_sys_dsnh->use_packet_ttl);
            if (device_info.version_id > 1)
            {
                SetDsNextHop4W(V, u1_g4_cosSwap_f,              &dsnh4w,   ((p_sys_dsnh->stag_cos >> 2)&0x1));
            }
            else
            {
                SetDsNextHop4W(V, u1_g4_cosSwap_f,              &dsnh4w,   p_sys_dsnh->cos_swap);
            }

        }
        break;
    default:
        break;
    }

    if (do_vlan_edit)
    {
        SetDsNextHop4W(V, u1_g2_statsPtr12_7_f,         &dsnh4w,   (p_sys_dsnh->stats_ptr >> 7));
        SetDsNextHop4W(V, u1_g2_svlanTagDisable_f,      &dsnh4w,   p_sys_dsnh->svlan_tag_disable);
        SetDsNextHop4W(V, u1_g2_taggedMode_f,           &dsnh4w,   p_sys_dsnh->tagged_mode);
        SetDsNextHop4W(V, u1_g2_outputSvlanIdValid_f,   &dsnh4w,   p_sys_dsnh->output_svlan_id_valid);
        SetDsNextHop4W(V, u1_g2_outputSvlanId_f,        &dsnh4w,   p_sys_dsnh->output_svlan_id);
        SetDsNextHop4W(V, u1_g2_replaceStagCos_f,       &dsnh4w,   p_sys_dsnh->replace_stag_cos);
        SetDsNextHop4W(V, u1_g2_deriveStagCos_f,        &dsnh4w,   p_sys_dsnh->derive_stag_cos);
        SetDsNextHop4W(V, u1_g2_stagCfi_f,              &dsnh4w,   p_sys_dsnh->stag_cfi);
        SetDsNextHop4W(V, u1_g2_stagCos_f,              &dsnh4w,   p_sys_dsnh->stag_cos);
        SetDsNextHop4W(V, u1_g2_svlanTpid_f,            &dsnh4w,   p_sys_dsnh->svlan_tpid);
        SetDsNextHop4W(V, u1_g2_svlanTpidEn_f,          &dsnh4w,   p_sys_dsnh->svlan_tpid_en);
        SetDsNextHop4W(V, u1_g2_cvlanTagDisable_f,      &dsnh4w,   p_sys_dsnh->cvlan_tag_disable);
        SetDsNextHop4W(V, u1_g2_ctagShareMode_f,        &dsnh4w,   p_sys_dsnh->ctag_share_mode);
        SetDsNextHop4W(V, u1_g2_outputCvlanIdValid_f,   &dsnh4w,   p_sys_dsnh->output_cvlan_id_valid);
        SetDsNextHop4W(V, u1_g2_outputCvlanId_f,        &dsnh4w,   p_sys_dsnh->output_cvlan_id);
        SetDsNextHop4W(V, u1_g2_replaceCtagCos_f,       &dsnh4w,   p_sys_dsnh->replace_ctag_cos);
        SetDsNextHop4W(V, u1_g2_copyCtagCos_f,          &dsnh4w,   p_sys_dsnh->copy_ctag_cos);
        SetDsNextHop4W(V, u1_g2_mirrorTagAdd_f,         &dsnh4w,   p_sys_dsnh->mirror_tag_add);
        SetDsNextHop4W(V, u1_g2_useTtlFromPacket_f,     &dsnh4w,   p_sys_dsnh->use_packet_ttl);
        if (device_info.version_id > 1)
        {
            SetDsNextHop4W(V, u1_g2_tpidSwap_f,             &dsnh4w,   p_sys_dsnh->cos_swap);
            SetDsNextHop4W(V, u1_g2_cosSwap_f,              &dsnh4w,   ((p_sys_dsnh->stag_cos >> 2)&0x1));
        }
        else
        {
            SetDsNextHop4W(V, u1_g2_tpidSwap_f,             &dsnh4w,   p_sys_dsnh->tpid_swap);
            SetDsNextHop4W(V, u1_g2_cosSwap_f,              &dsnh4w,   p_sys_dsnh->cos_swap);
        }
    }

    if (p_sys_dsnh->stats_ptr && p_sys_dsnh->is_nexthop8_w)
    {
        SetDsNextHop4W(V, u1_g2_statsPtr12_7_f,         &dsnh4w,   (p_sys_dsnh->stats_ptr >> 7));
    }

    sal_memcpy(p_ds , &dsnh4w, sizeof(dsnh4w));
    /*----------mapping dsnexthop8w---------------*/
    if ( p_sys_dsnh->is_nexthop8_w)
    {
        SetDsNextHop8W(V, nextHopBypass_f,                &dsnh8w,   p_sys_dsnh->next_hop_bypass);
        SetDsNextHop8W(V, stpIdValid_f,                   &dsnh8w,   p_sys_dsnh->stp_id_valid);
        SetDsNextHop8W(V, stpId_f,                        &dsnh8w,   p_sys_dsnh->stp_id);
        SetDsNextHop8W(V, skipTimeInfoUpdate_f,           &dsnh8w,   p_sys_dsnh->keep_igs_ts);
        SetDsNextHop8W(V, reCalculateSymmetricalHash_f,   &dsnh8w,   p_sys_dsnh->xerpsan_en);

        SetDsNextHop8W(V, outerEditHashEn_f,             &dsnh8w,   p_sys_dsnh->hash_num?1:0);
        SetDsNextHop8W(V, outerEditHashNum_f,             &dsnh8w,   p_sys_dsnh->hash_num);
        SetDsNextHop8W(V, innerEditHashEn_f,             &dsnh8w,   p_sys_dsnh->hash_num?1:0);
        SetDsNextHop8W(V, innerEditHashNum_f,             &dsnh8w,   p_sys_dsnh->hash_num);

        SetDsNextHop8W(V, logicPortCheck_f,               &dsnh8w,   p_sys_dsnh->logic_port_check);
        SetDsNextHop8W(V, logicDestPort_f,                &dsnh8w,   p_sys_dsnh->logic_dest_port);
        SetDsNextHop8W(V, outerEditLocation_f,            &dsnh8w,   p_sys_dsnh->outer_edit_location);
        SetDsNextHop8W(V, outerEditPtrType_f,             &dsnh8w,   p_sys_dsnh->outer_edit_ptr_type);
        SetDsNextHop8W(V, outerEditPtr_f,                 &dsnh8w,   p_sys_dsnh->outer_edit_ptr);
        SetDsNextHop8W(V, innerEditPtrType_f,             &dsnh8w,   p_sys_dsnh->inner_edit_ptr_type);
        SetDsNextHop8W(V, innerEditPtr_f,                 &dsnh8w,   p_sys_dsnh->inner_edit_ptr);
        SetDsNextHop8W(V, isVxlanRouteOp_f,               &dsnh8w,   p_sys_dsnh->is_vxlan_route_op);

        SetDsNextHop8W(V, fidValid_f,                     &dsnh8w,   p_sys_dsnh->fid_valid);

        if (p_sys_dsnh->fid_valid)
        {
            SetDsNextHop8W(V, u2_g2_fid_f,                &dsnh8w,   p_sys_dsnh->fid);
        }
        else if (p_sys_dsnh->span_en)
        {
            SetDsNextHop8W(V, u2_g1_globalSpanId_f,      &dsnh8w,   p_sys_dsnh->fid);
        }
        else if (p_sys_dsnh->cw_index)
        {
            SetDsNextHop8W(V, u2_g1_cwIndex_f,            &dsnh8w,   p_sys_dsnh->cw_index);
        }

        sal_memcpy(((uint8*)&dsnh8w), &dsnh4w, sizeof(dsnh4w));
        sal_memcpy(p_ds , &dsnh8w, sizeof(dsnh8w));

        return CTC_E_NONE;
    }

    if ( (p_sys_dsnh->offset >> SYS_NH_DSNH_INTERNAL_SHIFT) == SYS_NH_DSNH_INTERNAL_BASE)
    {
        offset = (p_sys_dsnh->offset & ((1 << SYS_NH_DSNH_INTERNAL_SHIFT) - 1));

        CTC_ERROR_RETURN(sys_goldengate_nh_get_asic_table(lchip, SYS_NH_ENTRY_TYPE_NEXTHOP_INTERNAL,
                                                          offset / 2, &dsnh8w));

        if (offset % 2 == 0)
        {
            sal_memcpy((uint8*)&dsnh8w, &dsnh4w, sizeof(dsnh4w));
        }
        else
        {
            sal_memcpy((uint8*)&dsnh8w + sizeof(dsnh4w), &dsnh4w, sizeof(dsnh4w));
        }

        sal_memcpy(p_ds , &dsnh8w, sizeof(dsnh8w));

        return CTC_E_NONE;
    }

    return CTC_E_NONE;

}

int32
sys_goldengate_nh_update_dsnexthop(uint8 lchip, sys_nexthop_t* p_sys_dsnh)
{
    DsNextHop8W_m dsnh8w;
    uint32 dsnh_offset = 0;

    dsnh_offset = p_sys_dsnh->offset;
    CTC_ERROR_RETURN(sys_goldengate_nh_get_asic_table(lchip, SYS_NH_ENTRY_TYPE_NEXTHOP_8W, dsnh_offset, &dsnh8w));

    if (p_sys_dsnh->update_type == 1)
    {
        if ( p_sys_dsnh->hash_num >= 1)
        {
            SetDsNextHop8W(V, outerEditHashEn_f,      &dsnh8w,   1);
            SetDsNextHop8W(V, outerEditHashNum_f,   &dsnh8w,    p_sys_dsnh->hash_num-1);
            SetDsNextHop8W(V, innerEditHashEn_f,       &dsnh8w,   1);
            SetDsNextHop8W(V, innerEditHashNum_f,    &dsnh8w,    p_sys_dsnh->hash_num-1);
        }
        else
        {
            SetDsNextHop8W(V, outerEditHashEn_f,      &dsnh8w,   0);
            SetDsNextHop8W(V, outerEditHashNum_f,   &dsnh8w,   0);
            SetDsNextHop8W(V, innerEditHashEn_f,       &dsnh8w,   0);
            SetDsNextHop8W(V, innerEditHashNum_f,    &dsnh8w,   0);
        }
    }
    CTC_ERROR_RETURN(sys_goldengate_nh_set_asic_table(lchip, SYS_NH_ENTRY_TYPE_NEXTHOP_8W, dsnh_offset,  &dsnh8w));

    return CTC_E_NONE;
}


int32
sys_goldengate_nh_update_dsnexthop_4w(uint8 lchip, sys_nexthop_t* p_sys_dsnh)
{
    DsNextHop4W_m dsnh4w;
    uint32 dsnh_offset = 0;
    hw_mac_addr_t mac_da;

    dsnh_offset = p_sys_dsnh->offset;
    CTC_ERROR_RETURN(sys_goldengate_nh_get_asic_table(lchip, SYS_NH_ENTRY_TYPE_NEXTHOP_4W, dsnh_offset, &dsnh4w));

    if (p_sys_dsnh->update_type == 1)
    {
        SYS_GOLDENGATE_SET_HW_MAC(mac_da,  p_sys_dsnh->mac_da)
        SetDsNextHop4W(A, u1_g3_macDa_f,         &dsnh4w,   mac_da);
        SetDsNextHop4W(V, isMet_f,              &dsnh4w,   0);
    }
    else if (p_sys_dsnh->update_type == 2)
    {
        SetDsNextHop4W(V, isMet_f,              &dsnh4w,   1);
    }
    CTC_ERROR_RETURN(sys_goldengate_nh_set_asic_table(lchip, SYS_NH_ENTRY_TYPE_NEXTHOP_4W, dsnh_offset,  &dsnh4w));

    return CTC_E_NONE;
}

int32
sys_goldengate_nh_map_dsl2edit(uint8 lchip, sys_dsl2edit_t* p_sys_l2edit, void *p_ds)
{
    DsL2Edit3WOuter_m dsl2edit;
    hw_mac_addr_t mac;

    DsL2Edit6WOuter_m dsl2edit6w;


    sal_memset(&dsl2edit6w, 0, sizeof(dsl2edit6w));

    if (!p_sys_l2edit->dynamic && !p_sys_l2edit->is_6w)
    {
        CTC_ERROR_RETURN(sys_goldengate_nh_get_asic_table(lchip, SYS_NH_ENTRY_TYPE_L2EDIT_ETH_4W,
                                                          p_sys_l2edit->offset / 2, &dsl2edit6w));
    }

    sal_memset(&dsl2edit, 0, sizeof(dsl2edit));
    SYS_GOLDENGATE_SET_HW_MAC(mac,  p_sys_l2edit->mac_da)

    SetDsL2EditEth3W(V, discardType_f,           &dsl2edit,  0);
    SetDsL2EditEth3W(V, discard_f,               &dsl2edit,  0);
    if(SYS_NH_DS_TYPE_DISCARD == p_sys_l2edit->ds_type)
    {
        SetDsL2EditEth3W(V, discard_f,               &dsl2edit,  1);
    }
    SetDsL2EditEth3W(V, dsType_f,                &dsl2edit,  SYS_NH_DS_TYPE_L2EDIT);

    SetDsL2EditEth3W(V, l2RewriteType_f,         &dsl2edit,  p_sys_l2edit->l2_rewrite_type);
    SetDsL2EditEth3W(V, mapCos_f,                &dsl2edit,  0);
    SetDsL2EditEth3W(V, portMacSaEn_f,           &dsl2edit,  0);
    SetDsL2EditEth3W(V, deriveMacFromLabel_f,    &dsl2edit,  0);
    SetDsL2EditEth3W(V, fcoeOverTrillMacSaEn_f,  &dsl2edit,  0);
    if (p_sys_l2edit->is_vlan_valid)
    {
        SetDsL2EditEth3W(V, outputVlanIdIsSvlan_f,   &dsl2edit,  1);
        SetDsL2EditEth3W(V, outputVlanIdValid_f,     &dsl2edit,  1);
    }
    SetDsL2EditEth3W(V, overwriteEtherType_f,    &dsl2edit,  0);
    SetDsL2EditEth3W(V, packetType_f,            &dsl2edit,  0);
    if(p_sys_l2edit->is_vlan_valid)
    {
        SetDsL2EditEth3W(V, outputVlanId_f,          &dsl2edit,  p_sys_l2edit->output_vlan_id);
    }
    else
    {
        SetDsL2EditEth3W(V, outputVlanId_f,          &dsl2edit,  0xFFF);
    }
    SetDsL2EditEth3W(A, macDa_f,                &dsl2edit,  mac);
    SetDsL2EditEth3W(V, deriveFcoeMacSa_f,       &dsl2edit,  0);
    SetDsL2EditEth3W(V, deriveMcastMacForTrill_f,&dsl2edit,  0);
    SetDsL2EditEth3W(V, deriveMcastMacForMpls_f, &dsl2edit,  0);
    SetDsL2EditEth3W(V, deriveMcastMacForIp_f,   &dsl2edit,  p_sys_l2edit->derive_mcast_mac_for_ip);
    SetDsL2EditEth3W(V, deriveFcoeMac_f,         &dsl2edit,  p_sys_l2edit->fpma);
    SetDsL2EditEth3W(V, systemRouterMac_f,       &dsl2edit,  0);


    if (!p_sys_l2edit->dynamic && !p_sys_l2edit->is_6w)
    {
        if (p_sys_l2edit->offset % 2 == 0)
        {
            sal_memcpy((uint8*)&dsl2edit6w, &dsl2edit, sizeof(dsl2edit));
        }
        else
        {
            sal_memcpy((uint8*)&dsl2edit6w + sizeof(dsl2edit), &dsl2edit, sizeof(dsl2edit));
        }
    }
    else
    {
        sal_memcpy((uint8*)&dsl2edit6w, &dsl2edit, sizeof(dsl2edit));
    }

    if (p_sys_l2edit->is_6w)
    {
        SetDsL2EditEth6W(V, spanExtendHeaderCopyHash_f,     &dsl2edit6w,  0);
        SetDsL2EditEth6W(V, spanExtendHeaderValid_f ,       &dsl2edit6w,  0);
        SetDsL2EditEth6W(V, rsv_f ,                         &dsl2edit6w,  p_sys_l2edit->use_port_mac);
        if(p_sys_l2edit->update_mac_sa)
        {
            SYS_GOLDENGATE_SET_HW_MAC(mac,  p_sys_l2edit->mac_sa);
            SetDsL2EditEth6W(A, macSa_f,     &dsl2edit6w,  mac);
            SetDsL2EditEth6W(V, macSaEn_f,  &dsl2edit6w,  1);
        }

        if (p_sys_l2edit->ether_type)
        {
            SetDsL2EditEth6W(V, etherTypeRewriteValid_f,  &dsl2edit6w,  1);
            SetDsL2EditEth6W(V, etherType_f,  &dsl2edit6w,  p_sys_l2edit->ether_type);
        }
        SetDsL2EditEth6W(V, spanExtendHeaderValid_f,  &dsl2edit6w,  p_sys_l2edit->is_span_ext_hdr);
    }

    sal_memcpy(p_ds , &dsl2edit6w, sizeof(dsl2edit6w));

    return CTC_E_NONE;

}

int32
sys_goldengate_nh_map_dsl2edit_inner_swap(uint8 lchip, sys_dsl2edit_t* p_sys_l2edit, void *p_ds)
{
    DsL2Edit3WInner_m dsl2edit;
    hw_mac_addr_t mac_da;

    sal_memset(&dsl2edit, 0, sizeof(dsl2edit));

    SYS_GOLDENGATE_SET_HW_MAC(mac_da,  p_sys_l2edit->mac_da)

    SetDsL2EditInnerSwap(V, discardType_f,           &dsl2edit,  0);
    SetDsL2EditInnerSwap(V, discard_f,               &dsl2edit,  0);
    SetDsL2EditInnerSwap(V, dsType_f,                &dsl2edit,  p_sys_l2edit->ds_type);
    SetDsL2EditInnerSwap(V, l2RewriteType_f,         &dsl2edit,  p_sys_l2edit->l2_rewrite_type);
    SetDsL2EditInnerSwap(A, macDa_f,                 &dsl2edit,  mac_da);
    SetDsL2EditInnerSwap(V, replaceInnerMacDa_f,     &dsl2edit,  1);
    SetDsL2EditInnerSwap(V, vlanId_f,                &dsl2edit,  0);
    SetDsL2EditInnerSwap(V, vTagModifyMode_f,        &dsl2edit,  0);
    if (p_sys_l2edit->strip_inner_vlan)
    {
        SetDsL2EditInnerSwap(V, vTagOp_f,                &dsl2edit,  3);/*del stag*/
    }

    sal_memcpy(p_ds , &dsl2edit, sizeof(dsl2edit));
    return CTC_E_NONE;

}
int32
sys_goldengate_nh_map_dsl2edit_swap(uint8 lchip, void* p_sys_l2edit, void *p_ds)
{
    DsL2EditSwap_m dsl2edit;

    sal_memset(&dsl2edit, 0, sizeof(dsl2edit));


	SetDsL2EditSwap(V, dsType_f,            &dsl2edit, SYS_NH_DS_TYPE_L2EDIT );
	SetDsL2EditSwap(V, l2RewriteType_f ,    &dsl2edit, SYS_NH_L2EDIT_TYPE_MAC_SWAP );
	SetDsL2EditSwap(V, _type_f ,            &dsl2edit,  0);
    SetDsL2EditSwap(V, deriveMcastMac_f,    &dsl2edit,  1);
    SetDsL2EditSwap(V, outputVlanIdValid_f, &dsl2edit,  1);
    SetDsL2EditSwap(V, overwriteEtherType_f,&dsl2edit,  0);

    sal_memcpy(p_ds , &dsl2edit, sizeof(dsl2edit));
    return CTC_E_NONE;

}



int32
sys_goldengate_nh_map_dsl2edit_of6w(uint8 lchip, sys_dsl2edit_of6w_t* p_sys_l2edit, void *p_ds)
{
    DsL2EditOf_m dsl2edit;
    hw_mac_addr_t mac;

    sal_memset(&dsl2edit, 0, sizeof(dsl2edit));

    SetDsL2EditOf(V, discardType_f,           &dsl2edit,  0);
    SetDsL2EditOf(V, discard_f,               &dsl2edit,  0);
    SetDsL2EditOf(V, dsType_f,                &dsl2edit,  SYS_NH_DS_TYPE_L2EDIT);
    SetDsL2EditOf(V, l2RewriteType_f,         &dsl2edit, SYS_NH_L2EDIT_TYPE_OF);

	SYS_GOLDENGATE_SET_HW_MAC(mac,  p_sys_l2edit->p_flex_edit->mac_da);
	SetDsL2EditOf(A, macDa_f,                 &dsl2edit,  mac);
	SYS_GOLDENGATE_SET_HW_MAC(mac, p_sys_l2edit->p_flex_edit->mac_sa);
	SetDsL2EditOf(A, macSa_f,                 &dsl2edit,  mac);

    SetDsL2EditOf(V, etherType_f,     &dsl2edit, p_sys_l2edit->p_flex_edit->ether_type);

    if (p_sys_l2edit->vlan_profile_id)
    {
        SetDsL2EditOf(V, sVlanId_f,                &dsl2edit, p_sys_l2edit->p_flex_edit->new_svid);
        SetDsL2EditOf(V, sCos_f,        &dsl2edit,  p_sys_l2edit->p_flex_edit->new_scos);
        SetDsL2EditOf(V, cVlanId_f,                &dsl2edit, p_sys_l2edit->p_flex_edit->new_cvid);
        SetDsL2EditOf(V, cCos_f,        &dsl2edit,  p_sys_l2edit->p_flex_edit->new_ccos);
        SetDsL2EditOf(V, vlanActionProfileId_f,        &dsl2edit,  p_sys_l2edit->vlan_profile_id);
    }

    if(CTC_FLAG_ISSET(p_sys_l2edit->p_flex_edit->flag,CTC_MISC_NH_FLEX_EDIT_REPLACE_ETHERTYPE))
 	{
      SetDsL2EditOf(V, replaceEtherType_f,     &dsl2edit,1 );
 	}
    if(CTC_FLAG_ISSET(p_sys_l2edit->p_flex_edit->flag,CTC_MISC_NH_FLEX_EDIT_REPLACE_MACDA))
 	{
      SetDsL2EditOf(V, replaceMacDa_f,     &dsl2edit,1 );
 	}
    if(CTC_FLAG_ISSET(p_sys_l2edit->p_flex_edit->flag,CTC_MISC_NH_FLEX_EDIT_REPLACE_MACSA))
 	{
      SetDsL2EditOf(V, replaceMacSa_f,     &dsl2edit,1 );
 	}
    sal_memcpy(p_ds , &dsl2edit, sizeof(dsl2edit));

    return CTC_E_NONE;

}

STATIC int32
_sys_goldengate_nh_map_vlan_tag_op_translate(uint8 lchip, uint8 op_in, uint8* op_out, uint8* mode_out)
{
    if (CTC_VLAN_TAG_OP_REP_OR_ADD == op_in)
    {
        *mode_out = 1;
    }
    else
    {
        *mode_out = 0;
    }

    switch (op_in)
    {
    case CTC_VLAN_TAG_OP_NONE:
    case CTC_VLAN_TAG_OP_VALID:
        *op_out = 0;
        break;
    case CTC_VLAN_TAG_OP_REP:
    case CTC_VLAN_TAG_OP_REP_OR_ADD:
        *op_out = 1;
        break;
    case CTC_VLAN_TAG_OP_ADD:
        *op_out = 2;
        break;
    case CTC_VLAN_TAG_OP_DEL:
        *op_out = 3;
        break;
    }
    return CTC_E_NONE;
}
int32
sys_goldengate_nh_map_dsl2edit_of6w_vlan_profile(uint8 lchip, sys_dsl2edit_vlan_edit_t* p_sys_vlanedit, void *p_ds)
{

    DsOfEditVlanActionProfile_m ds_vlanprofile;
    uint8 tag_action = 0;
    uint8 tag_mode = 0;

    sal_memset(&ds_vlanprofile, 0, sizeof(ds_vlanprofile));

    SetDsOfEditVlanActionProfile(V, cCfiAction_f, &ds_vlanprofile, 0);
    SetDsOfEditVlanActionProfile(V, cCosAction_f, &ds_vlanprofile,  p_sys_vlanedit->ccos_sl);
    _sys_goldengate_nh_map_vlan_tag_op_translate(lchip, p_sys_vlanedit->ctag_op, &tag_action, &tag_mode);
    SetDsOfEditVlanActionProfile(V, cTagAction_f, &ds_vlanprofile,  tag_action);
    SetDsOfEditVlanActionProfile(V, cTagModifyMode_f , &ds_vlanprofile, tag_mode);
    SetDsOfEditVlanActionProfile(V, cVlanIdAction_f , &ds_vlanprofile,  p_sys_vlanedit->cvid_sl);

    SetDsOfEditVlanActionProfile(V, sCfiAction_f, &ds_vlanprofile,  0);
    SetDsOfEditVlanActionProfile(V, sCosAction_f, &ds_vlanprofile, p_sys_vlanedit->scos_sl);
    _sys_goldengate_nh_map_vlan_tag_op_translate(lchip, p_sys_vlanedit->stag_op, &tag_action, &tag_mode);
    SetDsOfEditVlanActionProfile(V, sTagAction_f, &ds_vlanprofile,  tag_action);
    SetDsOfEditVlanActionProfile(V, sTagModifyMode_f , &ds_vlanprofile, tag_mode);
    SetDsOfEditVlanActionProfile(V, sVlanIdAction_f , &ds_vlanprofile, p_sys_vlanedit->svid_sl);

    if ((CTC_VLAN_TAG_OP_ADD == p_sys_vlanedit->ctag_op) && (CTC_VLAN_TAG_OP_ADD == p_sys_vlanedit->stag_op))
    {
        SetDsOfEditVlanActionProfile(V, cTagAddMode_f, &ds_vlanprofile,  1);  /*add cvlan before raw packet vlan*/
    }
    else
    {
        SetDsOfEditVlanActionProfile(V, cTagAddMode_f, &ds_vlanprofile,  0);
    }

    SetDsOfEditVlanActionProfile(V, sVlanTpidIndexEn_f, &ds_vlanprofile, p_sys_vlanedit->new_stpid_en);
    SetDsOfEditVlanActionProfile(V, sVlanTpidIndex_f, &ds_vlanprofile, p_sys_vlanedit->new_stpid_idx);
    sal_memcpy(p_ds , &ds_vlanprofile, sizeof(ds_vlanprofile));

    return CTC_E_NONE;

}

int32
sys_goldengate_nh_map_dsl2edit_loopback(uint8 lchip, sys_dsl2edit_t* p_sys_l2edit, void *p_ds)
{
    DsL2EditLoopback_m dsl2edit;
    sal_memset(&dsl2edit, 0, sizeof(dsl2edit));

    SetDsL2EditLoopback(V, dsType_f,                &dsl2edit,  SYS_NH_DS_TYPE_L2EDIT);
    SetDsL2EditLoopback(V, l2RewriteType_f,         &dsl2edit,  SYS_NH_L2EDIT_TYPE_LOOPBACK);
    SetDsL2EditLoopback(V, lbDestMap_f,         &dsl2edit,  p_sys_l2edit->dest_map);
    SetDsL2EditLoopback(V, lbNextHopPtr_f,         &dsl2edit,  p_sys_l2edit->nexthop_ptr);
    SetDsL2EditLoopback(V, lbNextHopExt_f,         &dsl2edit,  p_sys_l2edit->nexthop_ext);

    sal_memcpy(p_ds , &dsl2edit, sizeof(dsl2edit));
    return CTC_E_NONE;
}

int32
sys_goldengate_nh_map_dsl3edit_of6w(uint8 lchip, sys_dsl3edit_of_t* p_l3edit, void *p_ds)
{
    DsL3EditOf6W_m dsl3edit;
    uint8 flex_tos = 0;

    sal_memset(&dsl3edit, 0, sizeof(dsl3edit));

    SetDsL3EditOf6W(V, editType_f,          &dsl3edit, 0);
    SetDsL3EditOf6W(V, dsType_f,            &dsl3edit, SYS_NH_DS_TYPE_L3EDIT);
    SetDsL3EditOf6W(V, l3RewriteType_f,     &dsl3edit, SYS_NH_L3EDIT_TYPE_OF8W);

    SetDsL3EditOf6W(V, u1_gIpv4_ipDa_f,     &dsl3edit,p_l3edit->ip_da.ipv4);
    SetDsL3EditOf6W(V, u1_gIpv4_ipSa_f,     &dsl3edit,p_l3edit->ip_sa.ipv4);
    SetDsL3EditOf6W(V, u1_gIpv4_protocol_f, &dsl3edit,p_l3edit->protocol);
    SetDsL3EditOf6W(V, u1_gIpv4_ttl_f ,     &dsl3edit,p_l3edit->ttl);

    if (CTC_FLAG_ISSET(p_l3edit->flag, CTC_MISC_NH_FLEX_EDIT_REPLACE_IPSA))
    {
        SetDsL3EditOf6W(V, u1_gIpv4_replaceIpSa_f ,&dsl3edit, 1 );
    }
    if (CTC_FLAG_ISSET(p_l3edit->flag, CTC_MISC_NH_FLEX_EDIT_REPLACE_IPDA))
    {
        SetDsL3EditOf6W(V, u1_gIpv4_replaceIpDa_f ,&dsl3edit, 1 );
    }
    if (CTC_FLAG_ISSET(p_l3edit->flag, CTC_MISC_NH_FLEX_EDIT_SWAP_IP))
    {
        SetDsL3EditOf6W(V, u1_gIpv4_replaceIpDa_f ,&dsl3edit, 1 );
        SetDsL3EditOf6W(V, u1_gIpv4_replaceIpSa_f ,&dsl3edit, 1 );
        SetDsL3EditOf6W(V, u1_gIpv4_copyIpDa_f ,&dsl3edit, 1 );
        SetDsL3EditOf6W(V, u1_gIpv4_copyIpSa_f ,&dsl3edit, 1 );
    }

    if (CTC_FLAG_ISSET(p_l3edit->flag, CTC_MISC_NH_FLEX_EDIT_REPLACE_TTL))
    {
        SetDsL3EditOf6W(V, u1_gIpv4_replaceTtl_f, &dsl3edit, 1 );
    }
    else if(CTC_FLAG_ISSET(p_l3edit->flag, CTC_MISC_NH_FLEX_EDIT_DECREASE_TTL))
    {
        SetDsL3EditOf6W(V, u1_gIpv4_decreaseTtl_f,&dsl3edit, 1 );
    }
    if (CTC_FLAG_ISSET(p_l3edit->flag, CTC_MISC_NH_FLEX_EDIT_REPLACE_ECN))
    {
        SetDsL3EditOf6W(V, u1_gIpv4_replaceEcn_f,&dsl3edit, 1 );
		flex_tos |= (p_l3edit->ecn & 0x3);
    }

    if (p_l3edit->dscp_select == CTC_NH_DSCP_SELECT_ASSIGN )
    {
        SetDsL3EditOf6W(V, u1_gIpv4_replaceDscp_f, &dsl3edit, 1 );
        flex_tos |= ((p_l3edit->dscp_or_tos & 0x3F) << 2);
    }
    else if (p_l3edit->dscp_select == CTC_NH_DSCP_SELECT_MAP )
    {
        SetDsL3EditOf6W(V, u1_gIpv4_mapDscp_f, &dsl3edit,  1);
    }
    else if (p_l3edit->dscp_select == CTC_NH_DSCP_SELECT_NONE)
    {

    }
    else
    {
        return CTC_E_INVALID_PARAM;
    }

	SetDsL3EditOf6W(V, u1_gIpv4_tos_f,&dsl3edit, flex_tos);
    if (CTC_FLAG_ISSET(p_l3edit->flag, CTC_MISC_NH_FLEX_EDIT_REPLACE_PROTOCOL))
    {
        SetDsL3EditOf6W(V, u1_gIpv4_replaceProtocol_f,&dsl3edit, 1 );
    }

    if (p_l3edit->packet_type == CTC_MISC_NH_PACKET_TYPE_ICMP)
    {
        if (CTC_FLAG_ISSET(p_l3edit->flag, CTC_MISC_NH_FLEX_EDIT_REPLACE_ICMP_CODE))
        {
            SetDsL3EditOf6W(V, u1_gIpv4_replaceIcmpv4Code_f,&dsl3edit, 1 );
        }
        if (CTC_FLAG_ISSET(p_l3edit->flag, CTC_MISC_NH_FLEX_EDIT_REPLACE_ICMP_TYPE))
        {
            SetDsL3EditOf6W(V, u1_gIpv4_replaceIcmpv4Type_f,&dsl3edit, 1 );
        }
        SetDsL3EditOf6W(V, u1_gIpv4_l4SourcePort_f ,&dsl3edit, p_l3edit->icmp_type);
        SetDsL3EditOf6W(V, u1_gIpv4_l4DestPort_f , &dsl3edit, p_l3edit->icmp_code);

    }
    else if(p_l3edit->packet_type == CTC_MISC_NH_PACKET_TYPE_GRE)
    {
        if (CTC_FLAG_ISSET(p_l3edit->flag, CTC_MISC_NH_FLEX_EDIT_REPLACE_GRE_KEY))
        {
            SetDsL3EditOf6W(V, u1_gIpv4_replaceGreKey_f,&dsl3edit, 1 );
        }

        SetDsL3EditOf6W(V, u1_gIpv4_l4SourcePort_f ,&dsl3edit, (p_l3edit->gre_key >> 16));
        SetDsL3EditOf6W(V, u1_gIpv4_l4DestPort_f ,&dsl3edit, (p_l3edit->gre_key & 0xFFFF) );
    }
    else if(p_l3edit->packet_type == CTC_MISC_NH_PACKET_TYPE_UDPORTCP)
    {
        if (CTC_FLAG_ISSET(p_l3edit->flag, CTC_MISC_NH_FLEX_EDIT_REPLACE_L4_DST_PORT))
        {
            SetDsL3EditOf6W(V, u1_gIpv4_replaceL4DestPort_f,&dsl3edit, 1 );
        }

        if (CTC_FLAG_ISSET(p_l3edit->flag, CTC_MISC_NH_FLEX_EDIT_REPLACE_L4_SRC_PORT))
        {
            SetDsL3EditOf6W(V, u1_gIpv4_replaceL4SourcePort_f ,&dsl3edit, 1 );
        }

        if (CTC_FLAG_ISSET(p_l3edit->flag, CTC_MISC_NH_FLEX_EDIT_SWAP_L4_PORT))
        {
            SetDsL3EditOf6W(V, u1_gIpv4_replaceL4SourcePort_f ,&dsl3edit, 1 );
            SetDsL3EditOf6W(V, u1_gIpv4_replaceL4DestPort_f,&dsl3edit, 1 );
            SetDsL3EditOf6W(V, u1_gIpv4_copyL4SourcePort_f ,&dsl3edit, 1 );
            SetDsL3EditOf6W(V, u1_gIpv4_copyL4DestPort_f,&dsl3edit, 1 );
        }

        if (CTC_FLAG_ISSET(p_l3edit->flag, CTC_MISC_NH_FLEX_EDIT_REPLACE_TCP_PORT))
        {
            SetDsL3EditOf6W(V, u1_gIpv4_replaceTcpPort_f,&dsl3edit, 1 );
        }
        if (CTC_FLAG_ISSET(p_l3edit->flag, CTC_MISC_NH_FLEX_EDIT_REPLACE_UDP_PORT))
        {
            SetDsL3EditOf6W(V, u1_gIpv4_replaceUdpPort_f,&dsl3edit, 1 );
        }
        SetDsL3EditOf6W(V, u1_gIpv4_l4SourcePort_f ,&dsl3edit, p_l3edit->l4_src_port);
        SetDsL3EditOf6W(V, u1_gIpv4_l4DestPort_f ,&dsl3edit, p_l3edit->l4_dst_port);
    }
    sal_memcpy(p_ds , &dsl3edit, sizeof(dsl3edit));

    return CTC_E_NONE;

}
int32
sys_goldengate_nh_map_dsl3edit_of12w(uint8 lchip, sys_dsl3edit_of_t* p_l3edit, void *p_ds)
{
    DsL3EditOf12W_m dsl3edit;
	uint8 is_have_l4 = 0;
    uint8 flex_tos = 0;
    ipv6_addr_t ipv6;
    hw_mac_addr_t mac;

    sal_memset(&dsl3edit, 0, sizeof(dsl3edit));
    sal_memset(&ipv6, 0, sizeof(ipv6_addr_t));

    SetDsL3EditOf12W(V, dsType_f,       &dsl3edit, SYS_NH_DS_TYPE_L3EDIT);
    SetDsL3EditOf12W(V, l3RewriteType_f,&dsl3edit, SYS_NH_L3EDIT_TYPE_OF16W);

    if (p_l3edit->packet_type == CTC_MISC_NH_PACKET_TYPE_ARP)
    {
        if (CTC_FLAG_ISSET(p_l3edit->flag, CTC_MISC_NH_FLEX_EDIT_REPLACE_ARP_HT))
        {
            SetDsL3EditOf12W(V, u1_gArp_hardwareType_f, &dsl3edit, p_l3edit->arp_ht);
            SetDsL3EditOf12W(V, u1_gArp_replaceHt_f, &dsl3edit,1 );
        }
        if (CTC_FLAG_ISSET(p_l3edit->flag, CTC_MISC_NH_FLEX_EDIT_REPLACE_ARP_HLEN))
        {
            SetDsL3EditOf12W(V, u1_gArp_hardwareAddLen_f, &dsl3edit, p_l3edit->arp_halen);
            SetDsL3EditOf12W(V, u1_gArp_replaceHa_f, &dsl3edit,1);
        }
        if (CTC_FLAG_ISSET(p_l3edit->flag, CTC_MISC_NH_FLEX_EDIT_REPLACE_ARP_OP))
        {
            SetDsL3EditOf12W(V, u1_gArp_opCode_f ,  &dsl3edit, p_l3edit->arp_op);
            SetDsL3EditOf12W(V, u1_gArp_replaceOpCode_f, &dsl3edit, 1);
        }
        if (CTC_FLAG_ISSET(p_l3edit->flag, CTC_MISC_NH_FLEX_EDIT_REPLACE_ARP_PT))
        {
            SetDsL3EditOf12W(V, u1_gArp_protocolType_f, &dsl3edit, p_l3edit->arp_pt);
            SetDsL3EditOf12W(V, u1_gArp_replacePt_f, &dsl3edit,1 );
        }
        if (CTC_FLAG_ISSET(p_l3edit->flag, CTC_MISC_NH_FLEX_EDIT_REPLACE_ARP_PLEN))
        {
            SetDsL3EditOf12W(V, u1_gArp_protocolAddLen_f, &dsl3edit, p_l3edit->arp_palen );
            SetDsL3EditOf12W(V, u1_gArp_replacePa_f, &dsl3edit,1 );
        }
        if (CTC_FLAG_ISSET(p_l3edit->flag, CTC_MISC_NH_FLEX_EDIT_REPLACE_ARP_SHA))
        {
            SYS_GOLDENGATE_SET_HW_MAC(mac, p_l3edit->arp_sha);
            SetDsL3EditOf12W(A, u1_gArp_senderHardwareAdd_f, &dsl3edit, mac);
            SetDsL3EditOf12W(V, u1_gArp_replaceSha_f, &dsl3edit,1 );
        }
        if (CTC_FLAG_ISSET(p_l3edit->flag, CTC_MISC_NH_FLEX_EDIT_REPLACE_ARP_SPA))
        {
            SetDsL3EditOf12W(V, u1_gArp_senderProtocolAdd_f, &dsl3edit, p_l3edit->arp_spa);
            SetDsL3EditOf12W(V, u1_gArp_replaceSpa_f, &dsl3edit, 1);
        }
        if (CTC_FLAG_ISSET(p_l3edit->flag, CTC_MISC_NH_FLEX_EDIT_REPLACE_ARP_THA))
        {
            SYS_GOLDENGATE_SET_HW_MAC(mac, p_l3edit->arp_tha);
            SetDsL3EditOf12W(A, u1_gArp_targetHardwareAdd_f, &dsl3edit, mac);
            SetDsL3EditOf12W(V, u1_gArp_replaceTha_f , &dsl3edit,1 );
        }
        if (CTC_FLAG_ISSET(p_l3edit->flag, CTC_MISC_NH_FLEX_EDIT_REPLACE_ARP_TPA))
        {
            SetDsL3EditOf12W(V, u1_gArp_targetProtocolAdd_f,&dsl3edit, p_l3edit->arp_tpa);
            SetDsL3EditOf12W(V, u1_gArp_replaceTpa_f, &dsl3edit,1 );
        }
        SetDsL3EditOf12W(V, editType_f,               &dsl3edit,  2);
        sal_memcpy(p_ds , &dsl3edit, sizeof(dsl3edit));
        return CTC_E_NONE;
    }
	else if (p_l3edit->packet_type == CTC_MISC_NH_PACKET_TYPE_ICMP)
    {
        if (CTC_FLAG_ISSET(p_l3edit->flag, CTC_MISC_NH_FLEX_EDIT_REPLACE_ICMP_CODE))
        {
            SetDsL3EditOf12W(V, u1_gIpv6Type2_replaceIcmpv6Code_f,     &dsl3edit, 1 );
        }
        if (CTC_FLAG_ISSET(p_l3edit->flag, CTC_MISC_NH_FLEX_EDIT_REPLACE_ICMP_TYPE))
        {
            SetDsL3EditOf12W(V, u1_gIpv6Type2_replaceIcmpv6Type_f,     &dsl3edit, 1 );
        }
        SetDsL3EditOf12W(V, u1_gIpv6Type2_l4SourcePort_f ,  &dsl3edit, p_l3edit->icmp_type);
        SetDsL3EditOf12W(V, u1_gIpv6Type2_l4DestPort_f , &dsl3edit, p_l3edit->icmp_code);
	    is_have_l4 = 1;

    }
    else if(p_l3edit->packet_type == CTC_MISC_NH_PACKET_TYPE_GRE)
    {
        if (CTC_FLAG_ISSET(p_l3edit->flag, CTC_MISC_NH_FLEX_EDIT_REPLACE_GRE_KEY))
        {
            SetDsL3EditOf12W(V, u1_gIpv6Type2_replaceGreKey_f,&dsl3edit, 1 );
        }

        SetDsL3EditOf12W(V, u1_gIpv6Type2_l4SourcePort_f, &dsl3edit,(p_l3edit->gre_key >>16) );
        SetDsL3EditOf12W(V, u1_gIpv6Type2_l4DestPort_f,&dsl3edit, (p_l3edit->gre_key & 0xFFFF) );
		is_have_l4 = 1;
    }
    else if(p_l3edit->packet_type == CTC_MISC_NH_PACKET_TYPE_UDPORTCP)
    {
        if (CTC_FLAG_ISSET(p_l3edit->flag, CTC_MISC_NH_FLEX_EDIT_REPLACE_L4_DST_PORT))
        {
            SetDsL3EditOf12W(V, u1_gIpv6Type2_replaceL4DestPort_f, &dsl3edit, 1 );
        }

        if (CTC_FLAG_ISSET(p_l3edit->flag, CTC_MISC_NH_FLEX_EDIT_REPLACE_L4_SRC_PORT))
        {
            SetDsL3EditOf12W(V, u1_gIpv6Type2_replaceL4SourcePort_f ,&dsl3edit, 1 );
        }

        if (CTC_FLAG_ISSET(p_l3edit->flag, CTC_MISC_NH_FLEX_EDIT_SWAP_L4_PORT))
        {
            SetDsL3EditOf12W(V, u1_gIpv6Type2_replaceL4SourcePort_f ,&dsl3edit, 1 );
            SetDsL3EditOf12W(V, u1_gIpv6Type2_replaceL4DestPort_f, &dsl3edit, 1 );
            SetDsL3EditOf12W(V, u1_gIpv6Type2_copyL4SourcePort_f ,&dsl3edit, 1 );
            SetDsL3EditOf12W(V, u1_gIpv6Type2_copyL4DestPort_f, &dsl3edit, 1 );
        }

        if (CTC_FLAG_ISSET(p_l3edit->flag, CTC_MISC_NH_FLEX_EDIT_REPLACE_TCP_PORT))
        {
            SetDsL3EditOf12W(V, u1_gIpv6Type2_replaceTcpPort_f,&dsl3edit, 1 );
        }
        if (CTC_FLAG_ISSET(p_l3edit->flag, CTC_MISC_NH_FLEX_EDIT_REPLACE_UDP_PORT))
        {
            SetDsL3EditOf12W(V, u1_gIpv6Type2_replaceUdpPort_f,&dsl3edit, 1 );
        }
        SetDsL3EditOf12W(V, u1_gIpv6Type2_l4SourcePort_f ,&dsl3edit, p_l3edit->l4_src_port);
        SetDsL3EditOf12W(V, u1_gIpv6Type2_l4DestPort_f ,&dsl3edit, p_l3edit->l4_dst_port);
       is_have_l4 = 1;
    }
    else if(CTC_FLAG_ISSET(p_l3edit->flag, CTC_MISC_NH_FLEX_EDIT_REPLACE_PROTOCOL))
    {
        is_have_l4 = 1;
    }

    if ((CTC_FLAG_ISSET(p_l3edit->flag, CTC_MISC_NH_FLEX_EDIT_REPLACE_IPDA) &&
        CTC_FLAG_ISSET(p_l3edit->flag, CTC_MISC_NH_FLEX_EDIT_REPLACE_IPSA)))
    {
        if (is_have_l4)
        {
            return CTC_E_FEATURE_NOT_SUPPORT;
        }
    }


  if(is_have_l4)
  {
    CTC_MAX_VALUE_CHECK(p_l3edit->flow_label, 0xFFFFF);
    SetDsL3EditOf12W(V, u1_gIpv6Type2_flowLabel_f , &dsl3edit,p_l3edit->flow_label&0xFFFFF);
    SetDsL3EditOf12W(V, u1_gIpv6Type2_protocol_f ,  &dsl3edit,p_l3edit->protocol);
	SetDsL3EditOf12W(V, u1_gIpv6Type2_ttl_f ,       &dsl3edit,p_l3edit->ttl);

    if (CTC_FLAG_ISSET(p_l3edit->flag, CTC_MISC_NH_FLEX_EDIT_REPLACE_IPDA))
    {
       SetDsL3EditOf12W(V, u1_gIpv6Type2_replaceIpDa_f , &dsl3edit, 1 );
       ipv6[0] = p_l3edit->ip_da.ipv6[3];
       ipv6[1] = p_l3edit->ip_da.ipv6[2];
       ipv6[2] = p_l3edit->ip_da.ipv6[1];
       ipv6[3] = p_l3edit->ip_da.ipv6[0];
	   SetDsL3EditOf12W(A, u1_gIpv6Type2_ip_f,         &dsl3edit,  ipv6);
    }
    else  if (CTC_FLAG_ISSET(p_l3edit->flag, CTC_MISC_NH_FLEX_EDIT_REPLACE_IPSA))
    {
        SetDsL3EditOf12W(V, u1_gIpv6Type2_replaceIpSa_f ,     &dsl3edit, 1 );
        ipv6[0] = p_l3edit->ip_sa.ipv6[3];
        ipv6[1] = p_l3edit->ip_sa.ipv6[2];
        ipv6[2] = p_l3edit->ip_sa.ipv6[1];
        ipv6[3] = p_l3edit->ip_sa.ipv6[0];
		SetDsL3EditOf12W(A, u1_gIpv6Type2_ip_f,         &dsl3edit,  ipv6);
    }


    if (CTC_FLAG_ISSET(p_l3edit->flag, CTC_MISC_NH_FLEX_EDIT_REPLACE_TTL))
    {
        SetDsL3EditOf12W(V, u1_gIpv6Type2_replaceTtl_f,     &dsl3edit, 1 );
    }
    else if(CTC_FLAG_ISSET(p_l3edit->flag, CTC_MISC_NH_FLEX_EDIT_DECREASE_TTL))
    {
        SetDsL3EditOf12W(V, u1_gIpv6Type2_decreaseTtl_f,     &dsl3edit, 1 );
    }
    if (CTC_FLAG_ISSET(p_l3edit->flag, CTC_MISC_NH_FLEX_EDIT_REPLACE_ECN))
    {
        SetDsL3EditOf12W(V, u1_gIpv6Type2_replaceEcn_f,     &dsl3edit, 1 );
		flex_tos |= (p_l3edit->ecn & 0x3);    }

    if (p_l3edit->dscp_select == CTC_NH_DSCP_SELECT_ASSIGN )
    {
        SetDsL3EditOf12W(V, u1_gIpv6Type2_replaceDscp_f ,     &dsl3edit, 1 );
        flex_tos |= ((p_l3edit->dscp_or_tos & 0x3F) << 2);
    }
    else if (p_l3edit->dscp_select == CTC_NH_DSCP_SELECT_MAP )
    {
        SetDsL3EditOf12W(V, u1_gIpv6Type2_mapDscp_f,     &dsl3edit,  1);
    }
    else if (p_l3edit->dscp_select == CTC_NH_DSCP_SELECT_NONE)
    {

    }
    else
    {
        return CTC_E_INVALID_PARAM;
    }

	SetDsL3EditOf12W(V, u1_gIpv6Type2_tos_f,     &dsl3edit, flex_tos);
    if (CTC_FLAG_ISSET(p_l3edit->flag, CTC_MISC_NH_FLEX_EDIT_REPLACE_PROTOCOL))
    {
        SetDsL3EditOf12W(V, u1_gIpv6Type2_replaceProtocol_f,     &dsl3edit, 1 );
    }
    if (CTC_FLAG_ISSET(p_l3edit->flag, CTC_MISC_NH_FLEX_EDIT_REPLACE_FLOW_LABEL))
    {
        SetDsL3EditOf12W(V, u1_gIpv6Type2_replaceFlowLabel_f,     &dsl3edit, 1 );
    }
	SetDsL3EditOf12W(V, editType_f,               &dsl3edit,  1);
  }
  else
  {
    ipv6[0] = p_l3edit->ip_da.ipv6[3];
    ipv6[1] = p_l3edit->ip_da.ipv6[2];
    ipv6[2] = p_l3edit->ip_da.ipv6[1];
    ipv6[3] = p_l3edit->ip_da.ipv6[0];
    SetDsL3EditOf12W(A, u1_gIpv6Type1_ipDa_f,     &dsl3edit,  ipv6);
    ipv6[0] = p_l3edit->ip_sa.ipv6[3];
    ipv6[1] = p_l3edit->ip_sa.ipv6[2];
    ipv6[2] = p_l3edit->ip_sa.ipv6[1];
    ipv6[3] = p_l3edit->ip_sa.ipv6[0];
    SetDsL3EditOf12W(A, u1_gIpv6Type1_ipSa_f,     &dsl3edit,  ipv6);
    CTC_MAX_VALUE_CHECK(p_l3edit->flow_label, 0xFFFFF);
    SetDsL3EditOf12W(V, u1_gIpv6Type1_flowLabel_f ,     &dsl3edit,p_l3edit->flow_label&0xFFFFF);
    SetDsL3EditOf12W(V, u1_gIpv6Type1_ttl_f ,        &dsl3edit,p_l3edit->ttl);

    if (CTC_FLAG_ISSET(p_l3edit->flag, CTC_MISC_NH_FLEX_EDIT_REPLACE_IPDA))
    {
        SetDsL3EditOf12W(V, u1_gIpv6Type1_replaceIpDa_f ,     &dsl3edit, 1 );
    }

    if (CTC_FLAG_ISSET(p_l3edit->flag, CTC_MISC_NH_FLEX_EDIT_REPLACE_IPSA))
    {
        SetDsL3EditOf12W(V, u1_gIpv6Type1_replaceIpSa_f ,     &dsl3edit, 1 );
    }

    if (CTC_FLAG_ISSET(p_l3edit->flag, CTC_MISC_NH_FLEX_EDIT_SWAP_IP))
    {
        SetDsL3EditOf12W(V, u1_gIpv6Type1_replaceIpDa_f ,     &dsl3edit, 1 );
        SetDsL3EditOf12W(V, u1_gIpv6Type1_replaceIpSa_f ,     &dsl3edit, 1 );
        SetDsL3EditOf12W(V, u1_gIpv6Type1_copyIpDa_f ,     &dsl3edit, 1 );
        SetDsL3EditOf12W(V, u1_gIpv6Type1_copyIpSa_f ,     &dsl3edit, 1 );
    }

    if (CTC_FLAG_ISSET(p_l3edit->flag, CTC_MISC_NH_FLEX_EDIT_REPLACE_TTL))
    {
        SetDsL3EditOf12W(V, u1_gIpv6Type1_replaceTtl_f,     &dsl3edit, 1 );
    }
    else if(CTC_FLAG_ISSET(p_l3edit->flag, CTC_MISC_NH_FLEX_EDIT_DECREASE_TTL))
    {
        SetDsL3EditOf12W(V, u1_gIpv6Type1_decreaseTtl_f,     &dsl3edit, 1 );
    }
    if (CTC_FLAG_ISSET(p_l3edit->flag, CTC_MISC_NH_FLEX_EDIT_REPLACE_ECN))
    {
        SetDsL3EditOf12W(V, u1_gIpv6Type1_replaceEcn_f,     &dsl3edit, 1 );
		flex_tos |= (p_l3edit->ecn & 0x3);
	}

    if (p_l3edit->dscp_select == CTC_NH_DSCP_SELECT_ASSIGN )
    {
        SetDsL3EditOf12W(V, u1_gIpv6Type1_replaceDscp_f ,     &dsl3edit, 1 );
        flex_tos |= ((p_l3edit->dscp_or_tos & 0x3F) << 2);
    }
    else if (p_l3edit->dscp_select == CTC_NH_DSCP_SELECT_MAP )
    {
        SetDsL3EditOf12W(V, u1_gIpv6Type1_mapDscp_f,     &dsl3edit,  1);
    }
    else if (p_l3edit->dscp_select == CTC_NH_DSCP_SELECT_NONE)
    {

    }
    else
    {
        return CTC_E_INVALID_PARAM;
    }

	SetDsL3EditOf12W(V, u1_gIpv6Type1_tos_f,     &dsl3edit, flex_tos);
    if (CTC_FLAG_ISSET(p_l3edit->flag, CTC_MISC_NH_FLEX_EDIT_REPLACE_FLOW_LABEL))
    {
        SetDsL3EditOf12W(V, u1_gIpv6Type1_replaceFlowLabel_f,     &dsl3edit, 1 );
    }
	SetDsL3EditOf12W(V, editType_f,               &dsl3edit,  0);
  }
  sal_memcpy(p_ds , &dsl3edit, sizeof(dsl3edit));
  return CTC_E_NONE;
}

int32
sys_goldengate_nh_map_dsmet(uint8 lchip, sys_met_t* p_sys_met, void *p_ds)
{
    DsMetEntry3W_m dsmet3w;
    DsMetEntry6W_m dsmet6w;
    uint32 pbm[3] = {0};

    sal_memset(&dsmet3w, 0, sizeof(dsmet3w));
    sal_memset(&dsmet6w, 0, sizeof(dsmet6w));
    SetDsMetEntry3W(V, isMet_f,                      &dsmet3w,      1);
    SetDsMetEntry3W(V, mcastMode_f,                  &dsmet3w,      p_sys_met->mcast_mode);
    SetDsMetEntry3W(V, nextHopExt_f,                 &dsmet3w,      p_sys_met->next_hop_ext);
    SetDsMetEntry3W(V, currentMetEntryType_f,           &dsmet3w,      p_sys_met->is_6w);
    SetDsMetEntry3W(V, nextMetEntryPtr_f,            &dsmet3w,      p_sys_met->next_met_entry_ptr);
    SetDsMetEntry3W(V, endLocalRep_f,                &dsmet3w,      p_sys_met->end_local_rep);
    SetDsMetEntry3W(V, remoteChip_f,                 &dsmet3w,      p_sys_met->remote_chip);
    SetDsMetEntry3W(V, phyPortCheckDiscard_f,        &dsmet3w,      p_sys_met->phy_port_chk_discard);
    SetDsMetEntry3W(V, isLinkAggregation_f,          &dsmet3w,      p_sys_met->is_agg);
    if (!p_sys_met->mcast_mode)
    {
        SetDsMetEntry3W(V, u1_g2_logicDestPort_f,        &dsmet3w,      p_sys_met->logic_dest_port);
        SetDsMetEntry3W(V, u1_g2_leafCheckEn_f,          &dsmet3w,      p_sys_met->leaf_check_en);
        SetDsMetEntry3W(V, u1_g2_logicPortCheckEn_f,     &dsmet3w,      p_sys_met->logic_port_check_en);
        SetDsMetEntry3W(V, u1_g2_logicPortTypeCheck_f,   &dsmet3w,      p_sys_met->logic_port_type_check);
        SetDsMetEntry3W(V, u1_g2_apsBridgeEn_f,          &dsmet3w,      p_sys_met->aps_bridge_en);
        SetDsMetEntry3W(V, u1_g2_ucastId_f,              &dsmet3w,      p_sys_met->dest_id);
        SetDsMetEntry3W(V, u1_g2_replicationCtl_f,       &dsmet3w,     ((p_sys_met->next_hop_ptr << 5) | p_sys_met->replicate_num));
        SetDsMetEntry3W(V, u1_g2_forceBackEn_f,          &dsmet3w,      p_sys_met->force_back_en);
    }
    else
    {
        pbm[0] = p_sys_met->port_bitmap[0];
        pbm[1] = p_sys_met->port_bitmap[1]&(~(0x1F << (59 - 32)));
        SetDsMetEntry3W(A, u1_g1_portBitmap_f,           &dsmet3w,      pbm);
        SetDsMetEntry3W(V, u1_g1_portBitmapBase_f,       &dsmet3w,      p_sys_met->port_bitmap_base);
    }
    sal_memcpy(p_ds , &dsmet3w, sizeof(dsmet3w));

    /*----------mapping dsmet6w---------------*/
    if ( p_sys_met->is_6w)
    {

        SetDsMetEntry6W(V, nextHopPtr_f,                   &dsmet6w,   p_sys_met->next_hop_ptr);
        pbm[0] = ((p_sys_met->port_bitmap[1]>>(59-32))&0x1F)| ((p_sys_met->port_bitmap[2]&(~(0x1F << (59-32))))<<5);
        pbm[1] = ((p_sys_met->port_bitmap[2]>>(59-32))&0x1F)| ((p_sys_met->port_bitmap[3]&(~(0x1F << (59-32))))<<5);
        pbm[2] = ((p_sys_met->port_bitmap[3]>>(59-32))&0x1F);

        SetDsMetEntry6W(A, portBitmapHigh_f,         &dsmet6w,   pbm);

        sal_memcpy(((uint8*)&dsmet6w), &dsmet3w, sizeof(dsmet3w));
        sal_memcpy(p_ds , &dsmet6w, sizeof(dsmet6w));

    }

    return CTC_E_NONE;

}


int32
sys_goldengate_nh_map_dsmpls(uint8 lchip, sys_dsmpls_t* p_sys_mpls, void *p_ds)
{
    DsL3EditMpls3W_m dsmpls;
    sal_memset(&dsmpls, 0, sizeof(dsmpls));
    SetDsL3EditMpls3W(V, discardType_f,         &dsmpls,   0);
    SetDsL3EditMpls3W(V, discard_f,             &dsmpls,   0);
    SetDsL3EditMpls3W(V, dsType_f,              &dsmpls,   p_sys_mpls->ds_type);
    SetDsL3EditMpls3W(V, l3RewriteType_f,       &dsmpls,   p_sys_mpls->l3_rewrite_type);
    SetDsL3EditMpls3W(V, nextEditPtrValid_f,    &dsmpls,   p_sys_mpls->next_editptr_valid);
    SetDsL3EditMpls3W(V, outerEditPtrType_f,    &dsmpls,   p_sys_mpls->outer_editptr_type);
    SetDsL3EditMpls3W(V, outerEditPtr_f,        &dsmpls,   p_sys_mpls->outer_editptr);
    SetDsL3EditMpls3W(V, logicDestPortHigh_f,   &dsmpls,   p_sys_mpls->logic_dest_port);
    SetDsL3EditMpls3W(V, deriveLabel_f,         &dsmpls,   p_sys_mpls->derive_label);
    SetDsL3EditMpls3W(V, mplsOamIndex_f,        &dsmpls,   p_sys_mpls->mpls_oam_index);
    SetDsL3EditMpls3W(V, labelType_f,           &dsmpls,   p_sys_mpls->label_type);
    SetDsL3EditMpls3W(V, srcDscpType_f,         &dsmpls,   p_sys_mpls->src_dscp_type);
    SetDsL3EditMpls3W(V, mcastLabel_f,          &dsmpls,   p_sys_mpls->mcast_label);
    SetDsL3EditMpls3W(V, mapTtlMode_f,          &dsmpls,   p_sys_mpls->map_ttl_mode);
    SetDsL3EditMpls3W(V, mapTtl_f,              &dsmpls,   p_sys_mpls->map_ttl);
    SetDsL3EditMpls3W(V, statsPtr_f,            &dsmpls,   p_sys_mpls->statsptr);
    SetDsL3EditMpls3W(V, entropyLabelEn_f,      &dsmpls,   p_sys_mpls->entropy_label_en);
    SetDsL3EditMpls3W(V, deriveExp_f,           &dsmpls,   p_sys_mpls->derive_exp);
    SetDsL3EditMpls3W(V, exp_f,                 &dsmpls,   p_sys_mpls->exp);
    SetDsL3EditMpls3W(V, ttlIndex_f,            &dsmpls,   p_sys_mpls->ttl_index);
    SetDsL3EditMpls3W(V, label_f,               &dsmpls,   p_sys_mpls->label);
    sal_memcpy(p_ds , &dsmpls, sizeof(dsmpls));

    return CTC_E_NONE;

}

int32
sys_goldengate_nh_map_tunnelv4(uint8 lchip, sys_dsl3edit_tunnelv4_t* p_sys_tunnel, void *p_ds)
{
    DsL3EditTunnelV4_m dstunnel;

    sal_memset(&dstunnel, 0, sizeof(dstunnel));

    SetDsL3EditTunnelV4(V, discardType_f,           &dstunnel,   0);
    SetDsL3EditTunnelV4(V, discard_f,               &dstunnel,   0);
    SetDsL3EditTunnelV4(V, dsType_f,                &dstunnel,   p_sys_tunnel->ds_type);
    SetDsL3EditTunnelV4(V, l3RewriteType_f,         &dstunnel,   p_sys_tunnel->l3_rewrite_type);
    SetDsL3EditTunnelV4(V, nextEditPtrValid_f,      &dstunnel,   p_sys_tunnel->out_l2edit_valid);
    SetDsL3EditTunnelV4(V, outerEditPtrType_f,      &dstunnel,   0);
    SetDsL3EditTunnelV4(V, outerEditPtr_f,          &dstunnel,   p_sys_tunnel->out_l2edit_valid ? (p_sys_tunnel->l2edit_ptr) : 0);
    SetDsL3EditTunnelV4(V, ipSaValid_f,             &dstunnel,   p_sys_tunnel->ipsa_valid);
    SetDsL3EditTunnelV4(V, mtuSize_f,               &dstunnel,   p_sys_tunnel->mtu_size);
    SetDsL3EditTunnelV4(V, deriveDscp_f,            &dstunnel,   p_sys_tunnel->derive_dscp);
    SetDsL3EditTunnelV4(V, mapTtl_f,                &dstunnel,   p_sys_tunnel->map_ttl);
    SetDsL3EditTunnelV4(V, isatpTunnel_f,           &dstunnel,   p_sys_tunnel->isatp_tunnel);
    SetDsL3EditTunnelV4(V, copyDontFrag_f,          &dstunnel,   p_sys_tunnel->copy_dont_frag);
    SetDsL3EditTunnelV4(V, dontFrag_f,              &dstunnel,   p_sys_tunnel->dont_frag);
    SetDsL3EditTunnelV4(V, dscp_f,                  &dstunnel,   p_sys_tunnel->dscp);
    SetDsL3EditTunnelV4(V, ttl_f,                   &dstunnel,   p_sys_tunnel->ttl);
    SetDsL3EditTunnelV4(V, tunnel6To4Sa_f,          &dstunnel,   p_sys_tunnel->tunnel6_to4_sa);
    SetDsL3EditTunnelV4(V, tunnel6To4Da_f,          &dstunnel,   p_sys_tunnel->tunnel6_to4_da);
    SetDsL3EditTunnelV4(V, mtuCheckEn_f,            &dstunnel,   p_sys_tunnel->mtu_check_en);
    SetDsL3EditTunnelV4(V, innerHeaderValid_f,      &dstunnel,   p_sys_tunnel->inner_header_valid);
    SetDsL3EditTunnelV4(V, innerHeaderType_f,       &dstunnel,   p_sys_tunnel->inner_header_type);
    SetDsL3EditTunnelV4(V, ipDa_f,                  &dstunnel,   p_sys_tunnel->ipda);
    SetDsL3EditTunnelV4(V, ipSaIndex_f,             &dstunnel,   p_sys_tunnel->ipsa_index);
    SetDsL3EditTunnelV4(V, statsPtr_f,              &dstunnel,   p_sys_tunnel->stats_ptr);
    SetDsL3EditTunnelV4(V, spanExtendHeaderValid_f,             &dstunnel,   p_sys_tunnel->xerspan_hdr_en);
    SetDsL3EditTunnelV4(V, spanExtendHeaderCopyHash_f,     &dstunnel,   p_sys_tunnel->xerspan_hdr_with_hash_en);
    SetDsL3EditTunnelV4(V, ipIdentificationType_f,           &dstunnel,   0);
    SetDsL3EditTunnelV4(V, packetLengthType_f,           &dstunnel,   1);


    switch(p_sys_tunnel->share_type)
    {
    case SYS_NH_L3EDIT_TUNNELV4_SHARE_TYPE_GRE:
        SetDsL3EditTunnelV4(V, u1_g2_greKeyHigh_f,      &dstunnel,   (p_sys_tunnel->gre_key >> 16));
        SetDsL3EditTunnelV4(V, u2_g1_greKeyLow_f,       &dstunnel,   (p_sys_tunnel->gre_key & 0xFFFF));
        SetDsL3EditTunnelV4(V, u3_g1_greProtocol_f,     &dstunnel,   p_sys_tunnel->gre_protocol);
        SetDsL3EditTunnelV4(V, u4_g1_greFlags_f,        &dstunnel,   p_sys_tunnel->gre_flags);
        SetDsL3EditTunnelV4(V, u4_g1_greVersion_f,      &dstunnel,   p_sys_tunnel->gre_version);
        SetDsL3EditTunnelV4(V, u4_g1_greAutoIdentifyProtocol_f,      &dstunnel,   p_sys_tunnel->is_gre_auto);
        break;

    case SYS_NH_L3EDIT_TUNNELV4_SHARE_TYPE_6RD:
        SetDsL3EditTunnelV4(V, u4_g4_ipProtocolType_f,  &dstunnel,   p_sys_tunnel->ip_protocol_type);
        SetDsL3EditTunnelV4(V, u2_g3_tunnel6To4Ipv6PrefixLength_f,      &dstunnel,   p_sys_tunnel->tunnel6_to4_da_ipv6_prefixlen);
        SetDsL3EditTunnelV4(V, u2_g3_tunnel6To4Ipv4PrefixLength_f,      &dstunnel,   p_sys_tunnel->tunnel6_to4_da_ipv4_prefixlen);
        SetDsL3EditTunnelV4(V, u2_g3_tunnel6To4SaIpv6PrefixLength_f,    &dstunnel,   p_sys_tunnel->tunnel6_to4_sa_ipv6_prefixlen);
        SetDsL3EditTunnelV4(V, u2_g3_tunnel6To4SaIpv4PrefixLength_f,    &dstunnel,   p_sys_tunnel->tunnel6_to4_sa_ipv4_prefixlen);
        if (p_sys_tunnel->tunnel6_to4_sa)
        {
            SetDsL3EditTunnelV4(V, u3_g3_ipSa6To4High_f,    &dstunnel,   (p_sys_tunnel->ipsa_6to4 >> 16));
            SetDsL3EditTunnelV4(V, u1_g1_ipSa6To4Low_f,     &dstunnel,   (p_sys_tunnel->ipsa_6to4 & 0xFFFF));
        }
        else
        {
            SetDsL3EditTunnelV4(V, u3_g4_ipSaHigh_f,        &dstunnel,   (p_sys_tunnel->ipsa >> 16));
            SetDsL3EditTunnelV4(V, u1_g3_ipSaLow_f,         &dstunnel,   (p_sys_tunnel->ipsa & 0xFFFF));
        }
        break;

    case SYS_NH_L3EDIT_TUNNELV4_SHARE_TYPE_6TO4:
        SetDsL3EditTunnelV4(V, u4_g4_ipProtocolType_f,  &dstunnel,   p_sys_tunnel->ip_protocol_type);
        SetDsL3EditTunnelV4(V, u3_g4_ipSaHigh_f,        &dstunnel,   (p_sys_tunnel->ipsa >> 16));
        SetDsL3EditTunnelV4(V, u1_g3_ipSaLow_f,         &dstunnel,   (p_sys_tunnel->ipsa & 0xFFFF));
        break;

    case SYS_NH_L3EDIT_TUNNELV4_SHARE_TYPE_NVGRE:
        SetDsL3EditTunnelV4(V, u4_g1_nvgreHeaderMode_f, &dstunnel,   1);
        SetDsL3EditTunnelV4(V, u4_g1_isNvgreEncaps_f,   &dstunnel,   1);
        SetDsL3EditTunnelV4(V, u4_g1_greAutoIdentifyProtocol_f,   &dstunnel,   1);
        SetDsL3EditTunnelV4(V, u3_g3_ipSa6To4High_f,    &dstunnel,   (p_sys_tunnel->ipsa >> 16));
        SetDsL3EditTunnelV4(V, u1_g1_ipSa6To4Low_f,     &dstunnel,   (p_sys_tunnel->ipsa & 0xFFFF));
        break;

    case SYS_NH_L3EDIT_TUNNELV4_SHARE_TYPE_VXLAN:
        SetDsL3EditTunnelV4(V, u4_g3_udpTunnelType_f,   &dstunnel,   1);
        SetDsL3EditTunnelV4(V, u3_g3_ipSa6To4High_f,    &dstunnel,   (p_sys_tunnel->ipsa >> 16));
        SetDsL3EditTunnelV4(V, u1_g1_ipSa6To4Low_f,     &dstunnel,   (p_sys_tunnel->ipsa & 0xFFFF));

        SetDsL3EditTunnelV4(V, u4_g3_isEvxlanTunnel_f,   &dstunnel,   p_sys_tunnel->is_evxlan);
        SetDsL3EditTunnelV4(V, u4_g3_evxlanAutoIdentifyProtocol_f,   &dstunnel,   0);
        SetDsL3EditTunnelV4(V, u4_g3_protocolTypeSelIndex_f,   &dstunnel,   p_sys_tunnel->evxlan_protocl_index);

        SetDsL3EditTunnelV4(V, u4_g3_isGeneveTunnel_f,   &dstunnel,   p_sys_tunnel->is_geneve);
        SetDsL3EditTunnelV4(V, u4_g3_geneveAutoIdentifyProtocol_f,   &dstunnel,   p_sys_tunnel->is_geneve);

        SetDsL3EditTunnelV4(V, u4_g3_udpSrcPortCfgEn_f,   &dstunnel,   p_sys_tunnel->udp_src_port_en);
        SetDsL3EditTunnelV4(V, u2_g2_udpDestPort_f,   &dstunnel,   p_sys_tunnel->udp_src_port);

    default:
        break;
    }

    sal_memcpy(p_ds , &dstunnel, sizeof(dstunnel));

    return CTC_E_NONE;
}

int32
sys_goldengate_nh_map_tunnelv6(uint8 lchip, sys_dsl3edit_tunnelv6_t* p_sys_tunnel, void *p_ds)
{
    DsL3EditTunnelV6_m dstunnel;

    sal_memset(&dstunnel, 0, sizeof(dstunnel));

    SetDsL3EditTunnelV6(V, discardType_f,           &dstunnel,   0);
    SetDsL3EditTunnelV6(V, discard_f,               &dstunnel,   0);
    SetDsL3EditTunnelV6(V, dsType_f,                &dstunnel,   p_sys_tunnel->ds_type);
    SetDsL3EditTunnelV6(V, l3RewriteType_f,         &dstunnel,   p_sys_tunnel->l3_rewrite_type);
    SetDsL3EditTunnelV6(V, nextEditPtrValid_f,      &dstunnel,   p_sys_tunnel->out_l2edit_valid);
    SetDsL3EditTunnelV6(V, outerEditPtrType_f,      &dstunnel,   0);
    SetDsL3EditTunnelV6(V, outerEditPtr_f,          &dstunnel,   p_sys_tunnel->out_l2edit_valid ? (p_sys_tunnel->l2edit_ptr) : 0);
    SetDsL3EditTunnelV6(V, mtuCheckEn_f,            &dstunnel,   p_sys_tunnel->mtu_check_en);
    SetDsL3EditTunnelV6(V, mtuSize_f,               &dstunnel,   p_sys_tunnel->mtu_size);
    SetDsL3EditTunnelV6(V, deriveDscp_f,            &dstunnel,   p_sys_tunnel->derive_dscp);
    SetDsL3EditTunnelV6(V, mapTtl_f,                &dstunnel,   p_sys_tunnel->map_ttl);
    SetDsL3EditTunnelV6(V, tos_f,                   &dstunnel,   p_sys_tunnel->tos);
    SetDsL3EditTunnelV6(V, newFlowLabelMode_f,      &dstunnel,   p_sys_tunnel->new_flow_label_mode);
    SetDsL3EditTunnelV6(V, ttl_f,                   &dstunnel,   p_sys_tunnel->ttl);
    SetDsL3EditTunnelV6(V, innerHeaderValid_f,      &dstunnel,   p_sys_tunnel->inner_header_valid);
    SetDsL3EditTunnelV6(V, innerHeaderType_f,       &dstunnel,   p_sys_tunnel->inner_header_type);
    SetDsL3EditTunnelV6(V, flowLabel_f,             &dstunnel,   p_sys_tunnel->flow_label);
    SetDsL3EditTunnelV6(V, newFlowLabelValid_f,     &dstunnel,   p_sys_tunnel->new_flow_label_valid);
    SetDsL3EditTunnelV6(V, ipSaIndex_f,             &dstunnel,   p_sys_tunnel->ipsa_index);
    SetDsL3EditTunnelV6(A, ipDa_f,                  &dstunnel,   p_sys_tunnel->ipda);
    SetDsL3EditTunnelV6(V, statsPtr_f,              &dstunnel,   p_sys_tunnel->stats_ptr);
    SetDsL3EditTunnelV6(V, spanExtendHeaderValid_f,             &dstunnel,   p_sys_tunnel->xerspan_hdr_en);
    SetDsL3EditTunnelV6(V, spanExtendHeaderCopyHash_f,     &dstunnel,   p_sys_tunnel->xerspan_hdr_with_hash_en);
    SetDsL3EditTunnelV6(V, packetLengthType_f,           &dstunnel,   1);

    switch(p_sys_tunnel->share_type)
    {
    case SYS_NH_L3EDIT_TUNNELV6_SHARE_TYPE_GRE:
        SetDsL3EditTunnelV6(V, u2_g1_greKeyHigh_f,      &dstunnel,   (p_sys_tunnel->gre_key >> 16));
        SetDsL3EditTunnelV6(V, u3_g1_greKeyLow_f,       &dstunnel,   (p_sys_tunnel->gre_key & 0xFFFF));
        SetDsL3EditTunnelV6(V, u4_g1_greProtocol_f,     &dstunnel,   p_sys_tunnel->gre_protocol);
        SetDsL3EditTunnelV6(V, u3_g1_greFlags_f,        &dstunnel,   p_sys_tunnel->gre_flags);
        SetDsL3EditTunnelV6(V, u3_g1_greVersion_f,      &dstunnel,   p_sys_tunnel->gre_version);
        SetDsL3EditTunnelV6(V, u3_g1_greAutoIdentifyProtocol_f,      &dstunnel,   p_sys_tunnel->is_gre_auto);
        break;

    case SYS_NH_L3EDIT_TUNNELV6_SHARE_TYPE_NORMAL:
        SetDsL3EditTunnelV6(V, u3_g3_ipProtocolType_f,  &dstunnel,   p_sys_tunnel->ip_protocol_type);
        break;

    case SYS_NH_L3EDIT_TUNNELV6_SHARE_TYPE_NVGRE:
        SetDsL3EditTunnelV6(V, u3_g1_nvgreHeaderMode_f, &dstunnel,   1);
        SetDsL3EditTunnelV6(V, u3_g1_isNvgreEncaps_f,   &dstunnel,   1);
        SetDsL3EditTunnelV6(V, u3_g1_greAutoIdentifyProtocol_f,  &dstunnel,  1);
        break;

    case SYS_NH_L3EDIT_TUNNELV6_SHARE_TYPE_VXLAN:
        SetDsL3EditTunnelV6(V, u3_g2_udpTunnelType_f,   &dstunnel,   3);

        SetDsL3EditTunnelV6(V, u3_g2_isEvxlanTunnel_f,   &dstunnel,   p_sys_tunnel->is_evxlan);
        SetDsL3EditTunnelV6(V, u3_g2_evxlanAutoIdentifyProtocol_f,   &dstunnel,   0);
        SetDsL3EditTunnelV6(V, u3_g2_protocolTypeSelIndex_f,   &dstunnel,   p_sys_tunnel->evxlan_protocl_index);

        SetDsL3EditTunnelV6(V, u3_g2_isGeneveTunnel_f,   &dstunnel,   p_sys_tunnel->is_geneve);
        SetDsL3EditTunnelV6(V, u3_g2_geneveAutoIdentifyProtocol_f,   &dstunnel,   p_sys_tunnel->is_geneve);

        SetDsL3EditTunnelV6(V, u3_g2_udpSrcPortCfgEn_f,   &dstunnel,   p_sys_tunnel->udp_src_port_en);
        SetDsL3EditTunnelV6(V, u2_g2_udpDestPort_f,   &dstunnel,   p_sys_tunnel->udp_src_port);
    default:
        break;
    }

    sal_memcpy(p_ds , &dstunnel, sizeof(dstunnel));

    return CTC_E_NONE;
}

int32
sys_goldengate_nh_map_nat4w(uint8 lchip, sys_dsl3edit_nat4w_t* p_sys_nat, void *p_ds)
{
    DsL3EditNat3W_m dsnat;

    sal_memset(&dsnat, 0, sizeof(dsnat));

    SetDsL3EditNat3W(V, discardType_f,           &dsnat,   0);
    SetDsL3EditNat3W(V, discard_f,               &dsnat,   0);
    SetDsL3EditNat3W(V, dsType_f,                &dsnat,   p_sys_nat->ds_type);
    SetDsL3EditNat3W(V, l3RewriteType_f,         &dsnat,   p_sys_nat->l3_rewrite_type);
    SetDsL3EditNat3W(V, nextEditPtrValid_f,      &dsnat,   0);
    SetDsL3EditNat3W(V, outerEditPtrType_f,      &dsnat,   0);
    SetDsL3EditNat3W(V, outerEditPtr_f,          &dsnat,   0);
    SetDsL3EditNat3W(V, ipDa_f,                  &dsnat,   p_sys_nat->ipda);
    SetDsL3EditNat3W(V, ipDaPrefixLength_f,      &dsnat,   p_sys_nat->ip_da_prefix_length);
    SetDsL3EditNat3W(V, ipv4EmbededMode_f,       &dsnat,   p_sys_nat->ipv4_embeded_mode);
    SetDsL3EditNat3W(V, replaceIpDa_f,           &dsnat,   p_sys_nat->replace_ip_da);
    SetDsL3EditNat3W(V, l4DestPort_f,            &dsnat,   p_sys_nat->l4_dest_port);
    SetDsL3EditNat3W(V, replaceL4DestPort_f,     &dsnat,   p_sys_nat->replace_l4_dest_port);

    sal_memcpy(p_ds , &dsnat, sizeof(dsnat));

    return CTC_E_NONE;
}

int32
sys_goldengate_nh_map_nat8w(uint8 lchip, sys_dsl3edit_nat8w_t* p_sys_nat, void *p_ds)
{
    DsL3EditNat6W_m dsnat;
    uint32 ipda[4] = {0};

    sal_memset(&dsnat, 0, sizeof(dsnat));

    SetDsL3EditNat6W(V, discardType_f,           &dsnat,   0);
    SetDsL3EditNat6W(V, discard_f,               &dsnat,   0);
    SetDsL3EditNat6W(V, dsType_f,                &dsnat,   p_sys_nat->ds_type);
    SetDsL3EditNat6W(V, l3RewriteType_f,         &dsnat,   p_sys_nat->l3_rewrite_type);
    SetDsL3EditNat6W(V, nextEditPtrValid_f,      &dsnat,   0);
    SetDsL3EditNat6W(V, outerEditPtrType_f,      &dsnat,   0);
    SetDsL3EditNat6W(V, outerEditPtr_f,          &dsnat,   0);
    SetDsL3EditNat6W(V, ipDaPrefixLength_f,      &dsnat,   p_sys_nat->ip_da_prefix_length);
    SetDsL3EditNat6W(V, ipv4EmbededMode_f,       &dsnat,   p_sys_nat->ipv4_embeded_mode);

    SetDsL3EditNat6W(A, ipDa_f,       &dsnat,  p_sys_nat->ipda);
    ipda[0] = p_sys_nat->ipda[1] >> 12;
    ipda[0] |= (p_sys_nat->ipda[2] << 20);
    ipda[1] = p_sys_nat->ipda[2] >> 12;
    ipda[1] |= (p_sys_nat->ipda[3] << 20);
    ipda[2] = p_sys_nat->ipda[3] >> 12;
    SetDsL3EditNat6W(A, ipDaHigh_f,       &dsnat,  ipda);

    sal_memcpy(p_ds , &dsnat, sizeof(dsnat));

    return CTC_E_NONE;
}


int32
sys_goldengate_nh_map_trill(uint8 lchip, sys_dsl3edit_trill_t* p_sys_trill, void *p_ds)
{
    DsL3EditTrill_m dstrill;
    sal_memset(&dstrill, 0, sizeof(dstrill));
    SetDsL3EditTrill(V, l3RewriteType_f,         &dstrill,   p_sys_trill->l3_rewrite_type);
    SetDsL3EditTrill(V, version_f,               &dstrill,   0);
    SetDsL3EditTrill(V, ingressNickname_f,       &dstrill,   p_sys_trill->ingress_nickname);
    SetDsL3EditTrill(V, egressNickname_f,        &dstrill,   p_sys_trill->egress_nickname);
    SetDsL3EditTrill(V, hopCount_f,              &dstrill,   p_sys_trill->ttl);
    SetDsL3EditTrill(V, mtuCheckEn_f,            &dstrill,   p_sys_trill->mtu_check_en);
    SetDsL3EditTrill(V, mtuSize_f,               &dstrill,   p_sys_trill->mtu_size);
    SetDsL3EditTrill(V, statsPtr_f,              &dstrill,   p_sys_trill->stats_ptr);

    sal_memcpy(p_ds , &dstrill, sizeof(dstrill));

    return CTC_E_NONE;
}


int32
sys_goldengate_nh_map_ecmp_group(uint8 lchip, sys_dsecmp_group_t* p_sys_ecmp_group, void *p_ds)
{
    DsEcmpGroup_m dsgroup;

    sal_memset(&dsgroup, 0, sizeof(dsgroup));

    SetDsEcmpGroup(V, dlbEn_f,              &dsgroup,   p_sys_ecmp_group->dlb_en);
    SetDsEcmpGroup(V, memberNum_f,          &dsgroup,   p_sys_ecmp_group->member_num);
    SetDsEcmpGroup(V, memberPtrBase_f,      &dsgroup,   p_sys_ecmp_group->member_base);
    SetDsEcmpGroup(V, resilientHashEn_f,    &dsgroup,   p_sys_ecmp_group->resilient_hash_en);
    SetDsEcmpGroup(V, rrEn_f,               &dsgroup,   p_sys_ecmp_group->rr_en);
    SetDsEcmpGroup(V, statsEn_f,            &dsgroup,   p_sys_ecmp_group->stats_valid);

    sal_memcpy(p_ds , &dsgroup, sizeof(dsgroup));

    return CTC_E_NONE;
}

int32
sys_goldengate_nh_map_ecmp_member(uint8 lchip, sys_dsecmp_member_t* p_sys_ecmp_member, void *p_ds)
{
    DsEcmpMember_m dsmember;

    sal_memset(&dsmember, 0, sizeof(dsmember));

    SetDsEcmpMember(V, array_0_valid_f,         &dsmember,   p_sys_ecmp_member->valid0);
    SetDsEcmpMember(V, array_0_dsFwdPtr_f,      &dsmember,   p_sys_ecmp_member->dsfwdptr0);
    SetDsEcmpMember(V, array_0_destChannel_f,   &dsmember,   p_sys_ecmp_member->dest_channel0);

    SetDsEcmpMember(V, array_1_valid_f,         &dsmember,   p_sys_ecmp_member->valid1);
    SetDsEcmpMember(V, array_1_dsFwdPtr_f,      &dsmember,   p_sys_ecmp_member->dsfwdptr1);
    SetDsEcmpMember(V, array_1_destChannel_f,   &dsmember,   p_sys_ecmp_member->dest_channel1);

    SetDsEcmpMember(V, array_2_valid_f,         &dsmember,   p_sys_ecmp_member->valid2);
    SetDsEcmpMember(V, array_2_dsFwdPtr_f,      &dsmember,   p_sys_ecmp_member->dsfwdptr2);
    SetDsEcmpMember(V, array_2_destChannel_f,   &dsmember,   p_sys_ecmp_member->dest_channel2);

    SetDsEcmpMember(V, array_3_valid_f,         &dsmember,   p_sys_ecmp_member->valid3);
    SetDsEcmpMember(V, array_3_dsFwdPtr_f,      &dsmember,   p_sys_ecmp_member->dsfwdptr3);
    SetDsEcmpMember(V, array_3_destChannel_f,   &dsmember,   p_sys_ecmp_member->dest_channel3);

    sal_memcpy(p_ds , &dsmember, sizeof(dsmember));

    return CTC_E_NONE;
}

int32
sys_goldengate_nh_map_ecmp_rr_count(uint8 lchip, sys_dsecmp_rr_t* p_sys_ecmp_rr, void *p_ds)
{
    DsEcmpRrCount_m dsrr;

    sal_memset(&dsrr, 0, sizeof(dsrr));

    SetDsEcmpRrCount(V, randomRrEn_f,   &dsrr,   p_sys_ecmp_rr->random_rr_en);
    SetDsEcmpRrCount(V, memberNum_f,    &dsrr,   p_sys_ecmp_rr->member_num);

    sal_memcpy(p_ds , &dsrr, sizeof(dsrr));

    return CTC_E_NONE;
}

int32
sys_goldengate_nh_write_asic_table(uint8 lchip,
                                  uint8 table_type, uint32 offset, void* value)
{
    uint32 cmd    = 0;
    uint32 tbl_id = 0;
    void *p_ds = value;
    ds_t ds;

	 sys_goldengate_nh_master_t* p_nh_master = NULL;

    CTC_ERROR_RETURN(sys_goldengate_nh_get_nh_master(lchip, &p_nh_master));

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "lchip = %d, table_type = %d, offset = %d\n",
                   lchip, table_type, offset);

    /*Sanity check*/
    CTC_PTR_VALID_CHECK(value);

    if (p_nh_master->nh_table_info_array[table_type].p_func)
    {
        sal_memset(ds, 0, sizeof(ds));
        CTC_ERROR_RETURN((*p_nh_master->nh_table_info_array[table_type].p_func)(lchip, value, &ds));
        p_ds = (void*)ds;
    }

    tbl_id = p_nh_master->nh_table_info_array[table_type].table_id;
    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    if (DRV_E_NONE != DRV_IOCTL(lchip, offset, cmd, p_ds))
    {
        return CTC_E_HW_OP_FAIL;
    }

    return CTC_E_NONE;

}

int32
sys_goldengate_nh_update_asic_table(uint8 lchip,
                                   uint8 table_type, uint32 offset, void* value)
{

	sys_goldengate_nh_master_t* p_nh_master = NULL;

    CTC_ERROR_RETURN(sys_goldengate_nh_get_nh_master(lchip, &p_nh_master));

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "lchip = %d, table_type = %d, offset = %d\n",
                   lchip, table_type, offset);

    /*Sanity check*/
    CTC_PTR_VALID_CHECK(value);

    if (p_nh_master->nh_table_info_array[table_type].p_update)
    {
        (*p_nh_master->nh_table_info_array[table_type].p_update)(lchip, value);
    }

    return CTC_E_NONE;
}



int32
sys_goldengate_nh_get_asic_table(uint8 lchip,
                                uint8 table_type, uint32 offset, void* value)
{

    uint32 cmd;
    uint32 tbl_id;
	sys_goldengate_nh_master_t* p_nh_master = NULL;

    CTC_ERROR_RETURN(sys_goldengate_nh_get_nh_master(lchip, &p_nh_master));
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "lchip = %d, table_type = %d, offset = %d\n",
                   lchip, table_type, offset);

    /*Sanity check*/
    CTC_PTR_VALID_CHECK(value);

    tbl_id = p_nh_master->nh_table_info_array[table_type].table_id;
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    if (DRV_E_NONE != DRV_IOCTL(lchip, offset, cmd, value))
    {
        return CTC_E_HW_OP_FAIL;
    }

    return CTC_E_NONE;

}

int32
sys_goldengate_nh_set_asic_table(uint8 lchip,
                                uint8 table_type, uint32 offset, void* value)
{

    uint32 cmd;
    uint32 tbl_id;
	sys_goldengate_nh_master_t* p_nh_master = NULL;

    CTC_ERROR_RETURN(sys_goldengate_nh_get_nh_master(lchip, &p_nh_master));
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "lchip = %d, table_type = %d, offset = %d\n",
                   lchip, table_type, offset);

    /*Sanity check*/
    CTC_PTR_VALID_CHECK(value);

    tbl_id = p_nh_master->nh_table_info_array[table_type].table_id;
    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    if (DRV_E_NONE != DRV_IOCTL(lchip, offset, cmd, value))
    {
        return CTC_E_HW_OP_FAIL;
    }

    return CTC_E_NONE;

}


int32
sys_goldengate_nh_table_info_init(uint8 lchip)
{
    uint8 idx = 0;
	sys_goldengate_nh_master_t* p_nh_master = NULL;

    CTC_ERROR_RETURN(sys_goldengate_nh_get_nh_master(lchip, &p_nh_master))

     #define SYS_TBL_INFO_FUNC(idx) p_nh_master->nh_table_info_array[idx].p_func
     #define SYS_TBL_INFO_FUNC_UPDATE(idx) p_nh_master->nh_table_info_array[idx].p_update
     #define SYS_TBL_INFO_ID(idx) p_nh_master->nh_table_info_array[idx].table_id
     #define SYS_TBL_INFO_SIZE(idx) p_nh_master->nh_table_info_array[idx].entry_size


    for (idx = SYS_NH_ENTRY_TYPE_NULL; idx < SYS_NH_ENTRY_TYPE_MAX; idx++)
    {
        switch(idx)
        {
        case SYS_NH_ENTRY_TYPE_FWD:
            SYS_TBL_INFO_ID(idx)   = DsFwd_t;
            SYS_TBL_INFO_FUNC(idx) = (sys_goldengate_nh_fun) sys_goldengate_nh_map_dsfwd;
            break;

        case SYS_NH_ENTRY_TYPE_NEXTHOP_4W:
            SYS_TBL_INFO_ID(idx)   = DsNextHop4W_t;
            SYS_TBL_INFO_FUNC(idx) = (sys_goldengate_nh_fun) sys_goldengate_nh_map_dsnexthop;
            SYS_TBL_INFO_FUNC_UPDATE(idx)  =  (sys_goldengate_nh_update) sys_goldengate_nh_update_dsnexthop_4w;
            break;

        case SYS_NH_ENTRY_TYPE_NEXTHOP_8W:

            SYS_TBL_INFO_ID(idx)   = DsNextHop8W_t;
            SYS_TBL_INFO_FUNC(idx) = (sys_goldengate_nh_fun) sys_goldengate_nh_map_dsnexthop;
            SYS_TBL_INFO_FUNC_UPDATE(idx)  =  (sys_goldengate_nh_update) sys_goldengate_nh_update_dsnexthop;
            break;

        case SYS_NH_ENTRY_TYPE_NEXTHOP_INTERNAL:
            SYS_TBL_INFO_ID(idx)   = EpeNextHopInternal_t;
            SYS_TBL_INFO_FUNC(idx) = (sys_goldengate_nh_fun) sys_goldengate_nh_map_dsnexthop;
            break;

        case SYS_NH_ENTRY_TYPE_MET:
            SYS_TBL_INFO_ID(idx)   = DsMetEntry3W_t;
            SYS_TBL_INFO_FUNC(idx) = (sys_goldengate_nh_fun) sys_goldengate_nh_map_dsmet;
            break;

        case SYS_NH_ENTRY_TYPE_MET_6W:
            SYS_TBL_INFO_ID(idx)   = DsMetEntry6W_t;
            SYS_TBL_INFO_FUNC(idx) = (sys_goldengate_nh_fun) sys_goldengate_nh_map_dsmet;
            break;

        case SYS_NH_ENTRY_TYPE_L3EDIT_MPLS:
            SYS_TBL_INFO_ID(idx)   = DsL3EditMpls3W_t;
            SYS_TBL_INFO_FUNC(idx) = (sys_goldengate_nh_fun) sys_goldengate_nh_map_dsmpls;
            break;


        case SYS_NH_ENTRY_TYPE_L3EDIT_SPME:
            SYS_TBL_INFO_ID(idx)   = DsL3Edit3W3rd_t;
            SYS_TBL_INFO_FUNC(idx) = (sys_goldengate_nh_fun) sys_goldengate_nh_map_dsmpls;
            break;

        case SYS_NH_ENTRY_TYPE_L2EDIT_3W:
            SYS_TBL_INFO_ID(idx)   = DsL2EditEth3W_t;
            SYS_TBL_INFO_FUNC(idx) = (sys_goldengate_nh_fun) sys_goldengate_nh_map_dsl2edit;
            break;

        case SYS_NH_ENTRY_TYPE_L2EDIT_6W:
            SYS_TBL_INFO_ID(idx)   = DsL2EditEth6W_t;
            SYS_TBL_INFO_FUNC(idx) = (sys_goldengate_nh_fun) sys_goldengate_nh_map_dsl2edit;
            break;

        case SYS_NH_ENTRY_TYPE_L2EDIT_ETH_4W:
        case SYS_NH_ENTRY_TYPE_L2EDIT_ETH_8W:
            SYS_TBL_INFO_ID(idx)   = DsL2Edit6WOuter_t;
            SYS_TBL_INFO_FUNC(idx) = (sys_goldengate_nh_fun) sys_goldengate_nh_map_dsl2edit;
            break;

        case SYS_NH_ENTRY_TYPE_L2EDIT_INNER_SWAP:
            SYS_TBL_INFO_ID(idx)   = DsL2EditInnerSwap_t;
            SYS_TBL_INFO_FUNC(idx) = (sys_goldengate_nh_fun) sys_goldengate_nh_map_dsl2edit_inner_swap;
            break;
        case SYS_NH_ENTRY_TYPE_L2EDIT_SWAP:
            SYS_TBL_INFO_ID(idx)   = DsL2EditSwap_t;
            SYS_TBL_INFO_FUNC(idx) = (sys_goldengate_nh_fun) sys_goldengate_nh_map_dsl2edit_swap;
            break;
        case SYS_NH_ENTRY_TYPE_L2EDIT_OF_6W:
            SYS_TBL_INFO_ID(idx)   = DsL2EditOf_t;
            SYS_TBL_INFO_FUNC(idx) = (sys_goldengate_nh_fun) sys_goldengate_nh_map_dsl2edit_of6w;
            break;
        case SYS_NH_ENTRY_TYPE_L2EDIT_LPBK:
            SYS_TBL_INFO_ID(idx)   = DsL3EditLoopback_t;
            SYS_TBL_INFO_FUNC(idx) = (sys_goldengate_nh_fun) sys_goldengate_nh_map_dsl2edit_loopback;
            break;
        case SYS_NH_ENTRY_TYPE_L3EDIT_TUNNEL_V4:
            SYS_TBL_INFO_ID(idx)   = DsL3EditTunnelV4_t;
            SYS_TBL_INFO_FUNC(idx) = (sys_goldengate_nh_fun) sys_goldengate_nh_map_tunnelv4;
            break;

        case SYS_NH_ENTRY_TYPE_L3EDIT_TUNNEL_V6:
            SYS_TBL_INFO_ID(idx)   = DsL3EditTunnelV6_t;
            SYS_TBL_INFO_FUNC(idx) = (sys_goldengate_nh_fun) sys_goldengate_nh_map_tunnelv6;
            break;

        case SYS_NH_ENTRY_TYPE_L3EDIT_NAT_4W:
            SYS_TBL_INFO_ID(idx)   = DsL3EditNat3W_t;
            SYS_TBL_INFO_FUNC(idx) = (sys_goldengate_nh_fun) sys_goldengate_nh_map_nat4w;
            break;
        case SYS_NH_ENTRY_TYPE_L3EDIT_NAT_8W:
            SYS_TBL_INFO_ID(idx)   = DsL3EditNat6W_t;
            SYS_TBL_INFO_FUNC(idx) = (sys_goldengate_nh_fun) sys_goldengate_nh_map_nat8w;
            break;
        case SYS_NH_ENTRY_TYPE_L3EDIT_OF_6W:
            SYS_TBL_INFO_ID(idx)   = DsL3EditOf6W_t;
            SYS_TBL_INFO_FUNC(idx) = (sys_goldengate_nh_fun) sys_goldengate_nh_map_dsl3edit_of6w;
            break;

        case SYS_NH_ENTRY_TYPE_L3EDIT_OF_12W:
            SYS_TBL_INFO_ID(idx)   = DsL3EditOf12W_t;
            SYS_TBL_INFO_FUNC(idx) = (sys_goldengate_nh_fun) sys_goldengate_nh_map_dsl3edit_of12w;
            break;
        case SYS_NH_ENTRY_TYPE_ECMP_GROUP:
            SYS_TBL_INFO_ID(idx)   = DsEcmpGroup_t;
            SYS_TBL_INFO_FUNC(idx) = (sys_goldengate_nh_fun) sys_goldengate_nh_map_ecmp_group;
            break;

        case SYS_NH_ENTRY_TYPE_ECMP_MEMBER:
            SYS_TBL_INFO_ID(idx)   = DsEcmpMember_t;
            SYS_TBL_INFO_FUNC(idx) = (sys_goldengate_nh_fun) sys_goldengate_nh_map_ecmp_member;
            break;

        case SYS_NH_ENTRY_TYPE_ECMP_RR_COUNT:
            SYS_TBL_INFO_ID(idx)   = DsEcmpRrCount_t;
            SYS_TBL_INFO_FUNC(idx) = (sys_goldengate_nh_fun) sys_goldengate_nh_map_ecmp_rr_count;
            break;
       case SYS_NH_ENTRY_TYPE_L2EDIT_VLAN_PROFILE:
            SYS_TBL_INFO_ID(idx)   = DsOfEditVlanActionProfile_t;
            SYS_TBL_INFO_FUNC(idx) = (sys_goldengate_nh_fun) sys_goldengate_nh_map_dsl2edit_of6w_vlan_profile;
            break;
       case SYS_NH_ENTRY_TYPE_L3EDIT_TRILL:
            SYS_TBL_INFO_ID(idx)   = DsL3EditTrill_t;
            SYS_TBL_INFO_FUNC(idx) = (sys_goldengate_nh_fun) sys_goldengate_nh_map_trill;
            break;
        default:
            break;

        }

        if (idx == SYS_NH_ENTRY_TYPE_L2EDIT_ETH_4W)
        {
            SYS_TBL_INFO_SIZE(idx) = 1;
        }
        else if(idx == SYS_NH_ENTRY_TYPE_L2EDIT_ETH_8W)
        {
            SYS_TBL_INFO_SIZE(idx) = 2;
        }
        else
        {
            SYS_TBL_INFO_SIZE(idx) = TABLE_ENTRY_SIZE(SYS_TBL_INFO_ID(idx)) / DRV_BYTES_PER_ENTRY;
        }

    }


    return CTC_E_NONE;
}


int32
sys_goldengate_nh_global_cfg_init(uint8 lchip)
{
    int32           cmd = 0;
    uint32          nh_offset = 0;
    uint32          field_value = 0;
    uint32          value = 0;
    ds_t  ds;
    sys_goldengate_nh_master_t* p_nh_master = NULL;
    EpeHdrAdjustCtl_m     epe_hdr_adjust_ctl;


    CTC_ERROR_RETURN(sys_goldengate_nh_get_nh_master(lchip, &p_nh_master))

    sal_memset(ds, 0, sizeof(ds));


    cmd = DRV_IOR(MetFifoCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));

    /*First met always using 6w*/
    SetMetFifoCtl(V, groupIdShiftBit_f, ds, 1);

    sys_goldengate_nh_get_resolved_offset(lchip,
    SYS_NH_RES_OFFSET_TYPE_BRIDGE_NH,
            &nh_offset);
    SetMetFifoCtl(V, portBitmapNextHopPtr_f, ds, nh_offset);

    cmd = DRV_IOW(MetFifoCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));

    /*fatal exception*/
    sys_goldengate_nh_offset_alloc_with_multiple(lchip, SYS_NH_ENTRY_TYPE_FWD, 16, 16, &nh_offset);

    sal_memset(ds, 0, sizeof(ds));
    cmd = DRV_IOR(IpeDsFwdCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));
    SetIpeDsFwdCtl(V, dsFwdIndexBaseFatal_f, ds, (nh_offset >> 4));
    cmd = DRV_IOW(IpeDsFwdCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));
    p_nh_master->fatal_excp_base = nh_offset;

    sal_memset(ds, 0, sizeof(ds));
    cmd = DRV_IOR(DlbEngineCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));

    SetDlbEngineCtl(V, reblanceJitterEnable_f, ds, 1);
    SetDlbEngineCtl(V, reblanceJitterThreshold_f, ds, 256);
    SetDlbEngineCtl(V, flashCrowdMitigationEn_f, ds, 1);
    SetDlbEngineCtl(V, reorderPktIntervalMode_f, ds, 1);    /* per 256 packets do reblance */
    SetDlbEngineCtl(V, rebalanceMode_f, ds, 2);             /* CTC_GLOBAL_ECMP_REBALANCE_MODE_NORMAL */
    SetDlbEngineCtl(V, abruptInsertion_f, ds, 1);           /* must be 1 for spec bug */
    SetDlbEngineCtl(V, chanByteCountShift_f, ds, 0);
    SetDlbEngineCtl(V, rebalanceCongestCheck_f, ds, 1);
    SetDlbEngineCtl(V, rebalanceImpactCheck_f, ds, 1);

    cmd = DRV_IOW(DlbEngineCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));

    /* enable dlb ipg */
    field_value = 0;
    cmd = DRV_IOW(IpeEcmpCtl_t, IpeEcmpCtl_dlbIpgEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    /* enable ecmp rr for all protocol */
    field_value = 0xFFFF;
    cmd = DRV_IOW(IpeEcmpCtl_t, IpeEcmpCtl_l4ProtocolRrEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    /* ecmp dlb enable mode: CTC_GLOBAL_ECMP_DLB_MODE_ALL */
    field_value = 2;
    cmd = DRV_IOW(IpeEcmpCtl_t, IpeEcmpCtl_dlbEnableMode_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    /* Timer1:ecmp dlb flow bandwidth update timer. This flow scan timer must equal to channel bandwidth scan timer,
       it is 100 usec now.  channel bandwidth scan timer refer to _sys_goldengate_qmgr_dlb_init */
    sal_memset(ds, 0, sizeof(ds));
    cmd = DRV_IOR(DlbEngineTimerCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));

    SetDlbEngineTimerCtl(V, flowEntryMinPtr_f, ds, 0);
    SetDlbEngineTimerCtl(V, flowEntryMaxPtr_f, ds, 2047);

    SetDlbEngineTimerCtl(V, chanMaxPtr_f, ds, 127);

    SetDlbEngineTimerCtl(V, flowBandwidthInterval_f, ds, 25);
    SetDlbEngineTimerCtl(V, flowEntryBandwidthMaxPtr_f, ds, 2047);
    SetDlbEngineTimerCtl(V, flowBandwidthUpdate_f, ds, 1);

    SetDlbEngineTimerCtl(V, cfgRefDivBwRstPulse_f, ds, 25);     /* The pulse timer is 4us. Tsunit is 100usec */
    SetDlbEngineTimerCtl(V, cfgResetDivBwRstPulse_f, ds, 0);

    /* Timer2:ecmp flow inactive timer. TsUnit = 30 ms */
    SetDlbEngineTimerCtl(V, flowStateInactiveInterval_f, ds, 20);
    SetDlbEngineTimerCtl(V, flowStateInactiveMaxPtr_f, ds, 2048);
    SetDlbEngineTimerCtl(V, flowStateInactiveUpdateEn_f, ds, 1);
    SetDlbEngineTimerCtl(V, flowInactiveTsThreshold_f, ds, 2);          /* For metro, flow inactive time is 60 ms */

    SetDlbEngineTimerCtl(V, cfgRefDivInactiveRstPulse_f, ds, 7500);     /* TsUnit = 30 ms */
    SetDlbEngineTimerCtl(V, cfgResetDivInactiveRstPulse_f, ds, 0);

    /* Timer3:ecmp flow aging timer. TsUnit = 1 s*/
    SetDlbEngineTimerCtl(V, flowEntryAgingInterval_f, ds, 100);
    SetDlbEngineTimerCtl(V, flowEntryAgingMaxPtr_f, ds, 2047);
    SetDlbEngineTimerCtl(V, flowEntryAgingUpdEn_f, ds, 1);

    SetDlbEngineTimerCtl(V, cfgRefDivAgingRstPulse_f, ds, 250000);      /* TsUnit = 1 s */
    SetDlbEngineTimerCtl(V, cfgResetDivAgingRstPulse_f, ds, 0);

    /* Timer4:flash crowd, Tf = 100us, it need farther less than DRE update period, but cannot get it for the freq */
    SetDlbEngineTimerCtl(V, flashCrowdInterval_f, ds, 25);           /* must big than 73 for 64byte flow */
    SetDlbEngineTimerCtl(V, flashCrowdMaxPtr_f, ds, 2047);
    SetDlbEngineTimerCtl(V, flashCrowdUpdEn_f, ds, 1);

    SetDlbEngineTimerCtl(V, cfgRefDivFlashCrowedRstPulse_f, ds, 25);     /* TsUnit = 100 us */
    SetDlbEngineTimerCtl(V, cfgResetDivFlashCrowedRstPulse_f, ds, 0);

    cmd = DRV_IOW(DlbEngineTimerCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));

    /* Config pulse timer to 4us, low 8 bit is decimal fraction */
    value = 625 << 8;
    cmd = DRV_IOW(RefDivShareDlb_t, RefDivShareDlb_cfgRefDivShareDlbPulse_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));

    value = 0;
    cmd = DRV_IOW(RefDivShareDlb_t, RefDivShareDlb_cfgResetDivShareDlbPulse_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));

    /*For CoSim*/
    sal_memset(ds, 0, sizeof(ds));
    cmd = DRV_IOW(DsL23Edit3W_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));

    cmd = DRV_IOW(EpeHdrAdjustCtl_t, EpeHdrAdjustCtl_dsNextHopInternalBase_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &p_nh_master->internal_nexthop_base));

    cmd = DRV_IOR(EpeNextHopReserved2_t, EpeNextHopReserved0_reserved_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    CTC_BIT_SET(field_value, 0);
    cmd = DRV_IOW(EpeNextHopReserved2_t, EpeNextHopReserved0_reserved_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    /*Epe Head Adjust Ctl init*/
    sal_memset(&epe_hdr_adjust_ctl, 0, sizeof(epe_hdr_adjust_ctl));
    cmd = DRV_IOR(EpeHdrAdjustCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_hdr_adjust_ctl));
    {
        SetEpeHdrAdjustCtl(V, natStackingEn_f, &epe_hdr_adjust_ctl, 1);
    }
    cmd = DRV_IOW(EpeHdrAdjustCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_hdr_adjust_ctl));


    return CTC_E_NONE;
}







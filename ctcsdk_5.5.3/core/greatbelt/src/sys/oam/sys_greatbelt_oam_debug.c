
/**
 @file sys_greatbelt_l2_fdb.c

 @date 2009-10-19

 @version v2.0

The file implement   software simulation for MEP tx
*/
#include "sal.h"
#include "ctc_error.h"
#include "ctc_oam.h"

#include "sys_greatbelt_chip.h"
#include "sys_greatbelt_ftm.h"

#include "sys_greatbelt_oam_db.h"
#include "sys_greatbelt_oam_cfm_db.h"
#include "sys_greatbelt_oam_cfm.h"
#include "sys_greatbelt_oam_debug.h"

#include "greatbelt/include/drv_io.h"
#include "greatbelt/include/drv_enum.h"

struct sys_oam_brief_count_s
{
    uint16 user_channel_count;
    uint16 passive_channel_count;
    uint16 lmep_count;
    uint16 rmep_count;
};
typedef struct sys_oam_brief_count_s sys_oam_brief_count_t;

enum sys_oam_defect_flag
{
    SYS_OAM_DEFECT_FLAG_LOW_CCM_D                = 0x00000001,
    SYS_OAM_DEFECT_FLAG_MAID_MISMATCH_D          = 0x00000002,
    SYS_OAM_DEFECT_FLAG_CCM_INTERVAL_MISMATCH_D  = 0x00000004,
    SYS_OAM_DEFECT_FLAG_RMEP_NOT_FOUND_D         = 0x00000008,
    SYS_OAM_DEFECT_FLAG_DLOC_D                   = 0x00000010,
    SYS_OAM_DEFECT_FLAG_RDI_D                    = 0x00000020,
    SYS_OAM_DEFECT_FLAG_RMEP_SRCMAC_MISMATCH_D   = 0x00000040,
    SYS_OAM_DEFECT_FLAG_MAC_STATUS_D             = 0x00000080,
    SYS_OAM_DEFECT_FLAG_RX_CSF_D                 = 0x00000100,
    SYS_OAM_DEFECT_FLAG_MAX
};

uint32 g_mep_defect_bitmap[2048] = {0};

extern sys_oam_master_t* g_gb_oam_master[CTC_MAX_LOCAL_CHIP_NUM];

/**
 @brief   debug functions for oam tables
*/
int32
sys_greatbelt_oam_dump_maid(uint8 lchip, uint32 maid_index)
{
    uint32 cmd           = 0;
    uint8 maid_entry_num = 0;
    uint8 entry_index    = 0;
    uint8 char_index     = 0;
    ds_ma_name_t ds_ma_name;
    char maid[8];
    SYS_OAM_INIT_CHECK(lchip);

    sal_memset(&ds_ma_name, 0, sizeof(ds_ma_name_t));

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

    if (maid_index > (2048 - maid_entry_num))
    {
        SYS_OAM_DBG_INFO("Maid index out of range, must less than 0x%x\n", 2048 - maid_entry_num);
        return CTC_E_EXCEED_MAX_SIZE;
    }

    SYS_OAM_DBG_DUMP("Maid table, user index: 0x%x\n", maid_index);
    cmd = DRV_IOR(DsMaName_t, DRV_ENTRY_FLAG);

    for (entry_index = 0; entry_index < maid_entry_num; entry_index++)
    {
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, maid_index + entry_index, cmd, &ds_ma_name));
        maid[0] = (ds_ma_name.ma_id_umc0 >> 24) & 0xff;
        maid[1] = (ds_ma_name.ma_id_umc0 >> 16) & 0xff;
        maid[2] = (ds_ma_name.ma_id_umc0 >> 8) & 0xff;
        maid[3] = (ds_ma_name.ma_id_umc0 >> 0) & 0xff;
        maid[4] = (ds_ma_name.ma_id_umc1 >> 24) & 0xff;
        maid[5] = (ds_ma_name.ma_id_umc1 >> 16) & 0xff;
        maid[6] = (ds_ma_name.ma_id_umc1 >> 8) & 0xff;
        maid[7] = (ds_ma_name.ma_id_umc1 >> 0) & 0xff;

        SYS_OAM_DBG_DUMP("    Table index: 0x%x: ", maid_index + entry_index);

        for (char_index = 0; char_index < 8; char_index++)
        {
            SYS_OAM_DBG_DUMP(" %02x", maid[char_index]);
        }

        SYS_OAM_DBG_DUMP("\n");
    }

    return CTC_E_NONE;
}

#if 0
int32
sys_greatbelt_oam_dump_key_and_chan(uint8 lchip, uint32 key_index, uint32 key_type)
{

}

#endif

int32
sys_greatbelt_clear_event_cache(uint8 lchip)
{
    SYS_OAM_INIT_CHECK(lchip);
    if (g_gb_oam_master[lchip]->mep_defect_bitmap)
    {
        sal_memset(&(g_gb_oam_master[lchip]->mep_defect_bitmap), 0, sizeof(g_gb_oam_master[lchip]->mep_defect_bitmap));
    }

    return CTC_E_NONE;
}



int32
sys_greatbelt_oam_dump_ma(uint8 lchip, uint32 ma_index)
{
    uint32 cmd = 0;
    uint32 entry_num = 0;
    ds_ma_t ds_ma;

    SYS_OAM_INIT_CHECK(lchip);

    sal_memset(&ds_ma, 0, sizeof(ds_ma_t));


    CTC_ERROR_RETURN(sys_greatbelt_ftm_get_table_entry_num(lchip, DsMa_t, &entry_num));
    if (ma_index >= entry_num)
    {
        SYS_OAM_DBG_INFO("Key index out of range, must less than 0x%x\n", entry_num);
        return CTC_E_EXCEED_MAX_SIZE;
    }

    cmd = DRV_IOR(DsMa_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, ma_index, cmd, &ds_ma));

    SYS_OAM_DBG_DUMP("MA table index: 0x%x\n", ma_index);

    SYS_OAM_DBG_DUMP("    ma_id_type           : 0x%-4x tx_untagged_oam        : 0x%-4x use_vrfid_lkup    : 0x%-4x ma_name_index       : 0x%-4x\n",
                     ds_ma.ma_id_length_type, ds_ma.tx_untagged_oam, ds_ma.use_vrfid_lkup, ds_ma.ma_name_index);
    SYS_OAM_DBG_DUMP("    aps_en               : 0x%-4x priority_index         : 0x%-4x rx_oam_type       : 0x%-4x ma_id_len           : 0x%-4x\n",
                     ds_ma.aps_en, ds_ma.priority_index, ds_ma.rx_oam_type, ds_ma.ma_name_len);
    SYS_OAM_DBG_DUMP("    ccm_interval         : 0x%-4x tx_with_intf_status    : 0x%-4x md_lvl            : 0x%-4x tx_with_port_status : 0x%-4x\n",
                     ds_ma.ccm_interval, ds_ma.tx_with_if_status, ds_ma.md_lvl, ds_ma.tx_with_port_status);
    SYS_OAM_DBG_DUMP("    aps_signal_fail_local: 0x%-4x sf_fail_while_cfg_type : 0x%-4x mpls_label_valid  : 0x%-4x tx_with_send_id     : 0x%-4x\n",
                     ds_ma.aps_signal_fail_local, ds_ma.sf_fail_while_cfg_type, ds_ma.mpls_label_valid, ds_ma.tx_with_send_id);
    SYS_OAM_DBG_DUMP("    next_hop_ptr         : 0x%-8x\n",
                     ds_ma.next_hop_ptr);

    return CTC_E_NONE;
}

int32
sys_greatbelt_oam_dump_lmep(uint8 lchip, uint32 lmep_index)
{
    uint32 cmd = 0;
    uint32 entry_num = 0;
    ds_eth_mep_t ds_eth_mep; /* DS_ETH_MEP */
    uint32 ccm_seq_num = 0;

    SYS_OAM_INIT_CHECK(lchip);

    sal_memset(&ds_eth_mep, 0, sizeof(ds_eth_mep_t));

    CTC_ERROR_RETURN(sys_greatbelt_ftm_get_table_entry_num(lchip, DsEthMep_t, &entry_num));
    if (lmep_index >= entry_num)
    {
        SYS_OAM_DBG_INFO("Key index out of range, must less than 0x%x\n", entry_num);
        return CTC_E_EXCEED_MAX_SIZE;
    }

    cmd = DRV_IOR(DsEthMep_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lmep_index, cmd, &ds_eth_mep));

    ccm_seq_num = (ds_eth_mep.ccm_seq_num2 << 26) | (ds_eth_mep.ccm_seq_num1 << 20) | ds_eth_mep.ccm_seq_num0;

    SYS_OAM_DBG_DUMP("Lmep table index: 0x%x\n", lmep_index);

    SYS_OAM_DBG_DUMP("    cci_while       : 0x%-8x present_traffic_check_en: 0x%-4x is_bfd         : 0x%-4x enable_pm          : 0x%-4x\n",
                     ds_eth_mep.cci_while, ds_eth_mep.present_traffic_check_en, ds_eth_mep.is_bfd, ds_eth_mep.enable_pm);
    SYS_OAM_DBG_DUMP("    rmep_last_rdi   : 0x%-8x present_rdi             : 0x%-4x tpid_type      : 0x%-4x d_unexp_mep        : 0x%-4x\n",
                     ds_eth_mep.rmep_last_rdi, ds_eth_mep.present_rdi, ds_eth_mep.tpid_type, ds_eth_mep.d_unexp_mep);
    SYS_OAM_DBG_DUMP("    d_mismerge      : 0x%-8x d_meg_lvl               : 0x%-4x port_id        : 0x%-4x d_unexp_mep_timer  : 0x%-4x\n",
                     ds_eth_mep.d_mismerge, ds_eth_mep.d_meg_lvl, ds_eth_mep.port_id, ds_eth_mep.d_unexp_mep_timer);
    SYS_OAM_DBG_DUMP("    d_mismerge_timer: 0x%-8x d_meg_lvl_timer         : 0x%-4x mep_type       : 0x%-4x ccm_sep_num        : 0x%-4x\n",
                     ds_eth_mep.d_mismerge_timer, ds_eth_mep.d_meg_lvl_timer, ds_eth_mep.mep_type, ccm_seq_num);
    SYS_OAM_DBG_DUMP("    active          : 0x%-8x is_remote               : 0x%-4x is_bfd         : 0x%-4x dest_id            : 0x%-4x\n",
                     ds_eth_mep.active, ds_eth_mep.is_remote, ds_eth_mep.is_bfd, ds_eth_mep.dest_id);
    SYS_OAM_DBG_DUMP("    cci_en          : 0x%-8x ma_index                : 0x%-4x share_mac_en   : 0x%-4x present_traffic    : 0x%-4x\n",
                     ds_eth_mep.cci_en, ds_eth_mep.ma_index, ds_eth_mep.share_mac_en, ds_eth_mep.present_traffic);
    SYS_OAM_DBG_DUMP("    seq_num_en      : 0x%-8x is_up                   : 0x%-4x dest_chip      : 0x%-4x ma_id_check_disable: 0x%-4x\n",
                     ds_eth_mep.seq_num_en, ds_eth_mep.is_up, ds_eth_mep.dest_chip, ds_eth_mep.ma_id_check_disable);
    SYS_OAM_DBG_DUMP("    mep_primary_vid : 0x%-8x mep_id                  : 0x%-4x port_status    : 0x%-4x eth_oam_p2_p_mode  : 0x%-4x\n\n",
                     ds_eth_mep.mep_primary_vid, ds_eth_mep.mep_id, ds_eth_mep.port_status, ds_eth_mep.eth_oam_p2_p_mode);

    return CTC_E_NONE;
}

int32
sys_greatbelt_oam_dump_rmep(uint8 lchip, uint32 rmep_index)
{
    uint32 cmd = 0;
    uint32 entry_num = 0;
    ds_eth_rmep_t ds_eth_rmep; /* DS_ETH_RMEP */
    uint32 ccm_seq_num = 0;

    SYS_OAM_INIT_CHECK(lchip);

    sal_memset(&ds_eth_rmep, 0, sizeof(ds_eth_rmep_t));

    CTC_ERROR_RETURN(sys_greatbelt_ftm_get_table_entry_num(lchip, DsEthRmep_t, &entry_num));
    if (rmep_index >= entry_num)
    {
        SYS_OAM_DBG_INFO("Key index out of range, must less than 0x%x\n", entry_num);
        return CTC_E_EXCEED_MAX_SIZE;
    }

    cmd = DRV_IOR(DsEthRmep_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, rmep_index, cmd, &ds_eth_rmep));

    ccm_seq_num = (ds_eth_rmep.ccm_seq_num1 << 24) | ds_eth_rmep.ccm_seq_num0;

    SYS_OAM_DBG_DUMP("Rmep table index: 0x%x\n", rmep_index);

    SYS_OAM_DBG_DUMP("    first_pkt_rx     : 0x%-8x rmep_last_port_status: 0x%-4x seq_num_fail_counter: 0x%-4x mac_addr_update_disable: 0x%-4x\n",
                     ds_eth_rmep.first_pkt_rx, ds_eth_rmep.rmep_last_port_status, ds_eth_rmep.seq_num_fail_counter, ds_eth_rmep.mac_addr_update_disable);
    SYS_OAM_DBG_DUMP("    rmep_while       : 0x%-8x rmep_last_rdi        : 0x%-4x d_unexp_period      : 0x%-4x d_unexp_period_timer   : 0x%-4x\n",
                     ds_eth_rmep.rmep_while, ds_eth_rmep.rmep_last_rdi, ds_eth_rmep.d_unexp_period, ds_eth_rmep.d_unexp_period_timer);
    SYS_OAM_DBG_DUMP("    d_loc            : 0x%-8x exp_ccm_num          : 0x%-4x cnt_shift_while     : 0x%-4x rmep_last_intf_status  : 0x%-4x\n",
                     ds_eth_rmep.d_loc, ds_eth_rmep.exp_ccm_num, ds_eth_rmep.cnt_shift_while, ds_eth_rmep.rmep_last_intf_status);
    SYS_OAM_DBG_DUMP("    ccm_seq_num      : 0x%-8x active               : 0x%-4x is_remote           : 0x%-4x is_bfd                 : 0x%-4x\n",
                     ccm_seq_num, ds_eth_rmep.active, ds_eth_rmep.is_remote, ds_eth_rmep.is_bfd);
    SYS_OAM_DBG_DUMP("    rmep_mac_sa31_to0: 0x%-8x rmep_mac_sa47_to32   : 0x%-4x seq_num_en          : 0x%-4x mep_index              : 0x%-4x\n",
                     ds_eth_rmep.rmep_mac_sa_low, ds_eth_rmep.rmep_mac_sa_high, ds_eth_rmep.seq_num_en, ds_eth_rmep.mep_index);
    SYS_OAM_DBG_DUMP("    rmep_mac_status_changed: 0x%-8x rmep_mac_mismatch   : 0x%-4x \n",
                     ds_eth_rmep.mac_status_change, ds_eth_rmep.rmepmacmismatch);

    return CTC_E_NONE;
}

int32
sys_greatbelt_oam_dump_rmep_key_and_chan(uint8 lchip, uint32 key_index, uint32 key_type)
{

#if 0

#endif
    return CTC_E_NONE;
}

int32
sys_greatbelt_oam_dump_mep(uint8 lchip, ctc_oam_key_t* p_oam_key)
{

    int32 ret       = CTC_E_NONE;
    uint8 mep_type  = 0;
    sys_oam_chan_eth_t* p_sys_chan_eth = NULL;
    sys_oam_lmep_y1731_t* p_sys_lmep_eth = NULL;

    ctc_slistnode_t* ctc_slistnode_l    = NULL;
    ctc_slistnode_t* ctc_slistnode_r    = NULL;
    ctc_slist_t* p_rmep_list            = NULL;
    ctc_slist_t* p_lmep_list            = NULL;
    sys_oam_rmep_com_t* p_sys_rmep_db   = NULL;
    sys_oam_lmep_com_t* p_sys_lmep_db   = NULL;

    SYS_OAM_INIT_CHECK(lchip);

    mep_type = p_oam_key->mep_type;

    if ((CTC_OAM_MEP_TYPE_ETH_1AG == mep_type)
        || (CTC_OAM_MEP_TYPE_ETH_Y1731 == mep_type))
    {
        p_sys_chan_eth = _sys_greatbelt_cfm_chan_lkup(lchip, p_oam_key);
        if (NULL == p_sys_chan_eth)
        {
            ret = CTC_E_OAM_CHAN_ENTRY_NOT_FOUND;
            goto RETURN;
        }

        p_lmep_list = p_sys_chan_eth->com.lmep_list;

        CTC_SLIST_LOOP(p_lmep_list, ctc_slistnode_l)
        {
            p_sys_lmep_db = _ctc_container_of(ctc_slistnode_l, sys_oam_lmep_com_t, head);
            if (NULL == p_sys_lmep_db)
            {
                goto RETURN;
            }

            p_sys_lmep_eth = (sys_oam_lmep_y1731_t*)p_sys_lmep_db;
            SYS_OAM_DBG_DUMP("DsEthMep index: 0x%x, %s MEP, lmep Id %d\n", p_sys_lmep_eth->com.lmep_index, (p_sys_lmep_eth->is_up ? "Up" : "Down")
                             , p_sys_lmep_eth->mep_id);

            p_rmep_list = p_sys_lmep_eth->com.rmep_list;
            if (NULL == p_rmep_list)
            {
                goto RETURN;
            }

            CTC_SLIST_LOOP(p_rmep_list, ctc_slistnode_r)
            {
                p_sys_rmep_db = _ctc_container_of(ctc_slistnode_r, sys_oam_rmep_com_t, head);
                SYS_OAM_DBG_DUMP("DsEthRmep index: 0x%x, Rmep Id %d\n", p_sys_rmep_db->rmep_index, ((sys_oam_rmep_y1731_t*)p_sys_rmep_db)->rmep_id);
            }

        }

    }

RETURN:

    return ret;
}

int32
sys_greatbelt_oam_show_oam_property(uint8 lchip, ctc_oam_property_t* p_prop)
{
    ctc_oam_y1731_prop_t* p_eth_prop = NULL;
    p_eth_prop  = &p_prop->u.y1731;

    SYS_OAM_INIT_CHECK(lchip);

    CTC_ERROR_RETURN(_sys_greatbelt_cfm_get_property(lchip, p_prop));

    switch (p_eth_prop->cfg_type)
    {
    case CTC_OAM_Y1731_CFG_TYPE_PORT_OAM_EN:
        if (CTC_BOTH_DIRECTION != p_eth_prop->dir)
        {
            SYS_OAM_DBG_DUMP("Port  0x%04x %s OAM En %d\n", p_eth_prop->gport,
                             ((p_eth_prop->dir == CTC_INGRESS) ? "Ingress" : "Egress"), p_eth_prop->value);
        }
        else
        {
            p_eth_prop->dir = CTC_INGRESS;
            CTC_ERROR_RETURN(_sys_greatbelt_cfm_get_property(lchip, p_prop));
            SYS_OAM_DBG_DUMP("Port  0x%04x Ingress OAM En %d\n", p_eth_prop->gport, p_eth_prop->value);
            p_eth_prop->dir = CTC_EGRESS;
            CTC_ERROR_RETURN(_sys_greatbelt_cfm_get_property(lchip, p_prop));
            SYS_OAM_DBG_DUMP("Port  0x%04x Egress OAM En %d\n", p_eth_prop->gport, p_eth_prop->value);
        }

        break;

    case CTC_OAM_Y1731_CFG_TYPE_PORT_TUNNEL_EN:
        SYS_OAM_DBG_DUMP("Port  0x%04x  tunnel En %d\n", p_eth_prop->gport, p_eth_prop->value);
        break;

    case CTC_OAM_Y1731_CFG_TYPE_PORT_LM_EN:
        SYS_OAM_DBG_DUMP("Port  0x%04x  LM En %d\n", p_eth_prop->gport, p_eth_prop->value);
        break;

    case CTC_OAM_Y1731_CFG_TYPE_TP_ACH_CHAN_TYPE:
        SYS_OAM_DBG_DUMP("Tp_y1731_ach_chan_type    0x%x\n", p_eth_prop->value);
        break;

    default:
        break;
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_oam_show_oam_status(uint8 lchip)
{

    return CTC_E_NONE;
}

int32
sys_greatbelt_oam_internal_property(uint8 lchip)
{
    SYS_OAM_INIT_CHECK(lchip);

    g_gb_oam_master[lchip]->com_oam_global.mep_index_alloc_by_sdk = 0;

    return CTC_E_NONE;
}

int32
sys_greatbelt_oam_internal_maid_property(uint8 lchip, ctc_oam_maid_len_format_t maid_len)
{
    SYS_OAM_INIT_CHECK(lchip);

    g_gb_oam_master[lchip]->com_oam_global.maid_len_format = maid_len;

    return CTC_E_NONE;
}

int32
_sys_oam_get_defect_name(ctc_oam_mep_info_t* mep_info)
{
    ctc_oam_y1731_lmep_info_t* p_lmep_eth_info = NULL;
    ctc_oam_y1731_rmep_info_t* p_rmep_eth_info = NULL;
    uint8 index = 0;
    uint32 mep_index = 0;
    uint32 mep_defect_bitmap_temp = 0;

    uint32 mep_defect_bitmap_cmp = 0;

    mep_index = mep_info->mep_index;
    p_rmep_eth_info = &(mep_info->rmep.y1731_rmep);

    if (0 == mep_info->is_rmep)
    {

        SYS_OAM_DBG_INFO("  mep_index        : 0x%-4x\n",  mep_index);
        p_lmep_eth_info = &(mep_info->lmep.y1731_lmep);
        if (p_lmep_eth_info->d_mismerge)
        {
            CTC_SET_FLAG(mep_defect_bitmap_temp, SYS_OAM_DEFECT_FLAG_MAID_MISMATCH_D);
        }

        if (p_lmep_eth_info->d_unexp_mep)
        {
            CTC_SET_FLAG(mep_defect_bitmap_temp, SYS_OAM_DEFECT_FLAG_RMEP_NOT_FOUND_D);
        }

        if (p_lmep_eth_info->d_meg_lvl)
        {
            CTC_SET_FLAG(mep_defect_bitmap_temp, SYS_OAM_DEFECT_FLAG_LOW_CCM_D);
        }
    }
    else
    {
        SYS_OAM_DBG_INFO("  rmep_index       : 0x%-4x\n",  mep_index);
        if (p_rmep_eth_info->ma_sa_mismatch)
        {
            CTC_SET_FLAG(mep_defect_bitmap_temp, SYS_OAM_DEFECT_FLAG_RMEP_SRCMAC_MISMATCH_D);
        }

        if (p_rmep_eth_info->mac_status_change)
        {
            CTC_SET_FLAG(mep_defect_bitmap_temp, SYS_OAM_DEFECT_FLAG_MAC_STATUS_D);
        }

        if (p_rmep_eth_info->d_unexp_period)
        {
            CTC_SET_FLAG(mep_defect_bitmap_temp, SYS_OAM_DEFECT_FLAG_CCM_INTERVAL_MISMATCH_D);
        }

        if (p_rmep_eth_info->d_loc)
        {
            CTC_SET_FLAG(mep_defect_bitmap_temp, SYS_OAM_DEFECT_FLAG_DLOC_D);
        }

        if (p_rmep_eth_info->last_rdi)
        {
            CTC_SET_FLAG(mep_defect_bitmap_temp, SYS_OAM_DEFECT_FLAG_RDI_D);
        }

        if (p_rmep_eth_info->d_csf)
        {
            CTC_SET_FLAG(mep_defect_bitmap_temp, SYS_OAM_DEFECT_FLAG_RX_CSF_D);
        }
    }

    mep_defect_bitmap_cmp = mep_defect_bitmap_temp ^ (g_mep_defect_bitmap[mep_index]);

     /*SYS_OAM_DBG_INFO("  mep_defect_bitmap_temp 0x%x\n", mep_defect_bitmap_temp);*/
     /*SYS_OAM_DBG_INFO("  g_mep_defect_bitmap    0x%x\n", g_mep_defect_bitmap[mep_index]);*/
     /*SYS_OAM_DBG_INFO("  mep_defect_bitmap_cmp  0x%x\n", mep_defect_bitmap_cmp);*/

    if (0 == mep_defect_bitmap_cmp)
    {
        SYS_OAM_DBG_DUMP("  defect name: %s\n", "Defect no name");
    }

    for (index = 0; index < 32; index++)
    {
        if (CTC_IS_BIT_SET(mep_defect_bitmap_cmp, index))
        {
            switch (1 << index)
            {
            case SYS_OAM_DEFECT_FLAG_RX_CSF_D:
                if (CTC_FLAG_ISSET(mep_defect_bitmap_temp, SYS_OAM_DEFECT_FLAG_RX_CSF_D))
                {
                    CTC_SET_FLAG(g_mep_defect_bitmap[mep_index], SYS_OAM_DEFECT_FLAG_RX_CSF_D);
                    SYS_OAM_DBG_DUMP("  defect name: %s\n", "CSFDefect set");
                }
                else
                {
                    SYS_OAM_DBG_DUMP("  defect name: %s\n", "CSFDefect: clear");
                }

                break;

            case SYS_OAM_DEFECT_FLAG_LOW_CCM_D:
                if (CTC_FLAG_ISSET(mep_defect_bitmap_temp, SYS_OAM_DEFECT_FLAG_LOW_CCM_D))
                {
                    CTC_SET_FLAG(g_mep_defect_bitmap[mep_index], SYS_OAM_DEFECT_FLAG_LOW_CCM_D);
                    SYS_OAM_DBG_DUMP("  defect name: %s\n", "xconCCMdefect: low ccm");
                }
                else
                {
                    SYS_OAM_DBG_DUMP("  defect name: %s\n", "xconCCMdefect: low ccm clear");
                }

                break;

            case SYS_OAM_DEFECT_FLAG_MAID_MISMATCH_D:
                if (CTC_FLAG_ISSET(mep_defect_bitmap_temp, SYS_OAM_DEFECT_FLAG_MAID_MISMATCH_D))
                {
                    CTC_SET_FLAG(g_mep_defect_bitmap[mep_index], SYS_OAM_DEFECT_FLAG_MAID_MISMATCH_D);
                    SYS_OAM_DBG_DUMP("  defect name: %s\n", "xconCCMdefect: ma id mismatch");
                }
                else
                {
                    SYS_OAM_DBG_DUMP("  defect name: %s\n", "xconCCMdefect: ma id mismatch clear");
                }

                break;

            case SYS_OAM_DEFECT_FLAG_CCM_INTERVAL_MISMATCH_D:
                if (CTC_FLAG_ISSET(mep_defect_bitmap_temp, SYS_OAM_DEFECT_FLAG_CCM_INTERVAL_MISMATCH_D))
                {
                    CTC_SET_FLAG(g_mep_defect_bitmap[mep_index], SYS_OAM_DEFECT_FLAG_CCM_INTERVAL_MISMATCH_D);
                    SYS_OAM_DBG_DUMP("  defect name: %s\n", "errorCCMdefect: ccm interval mismatch");
                }
                else
                {
                    SYS_OAM_DBG_DUMP("  defect name: %s\n", "errorCCMdefect: ccm interval mismatch clear");
                }

                break;

            case SYS_OAM_DEFECT_FLAG_DLOC_D:
                if (CTC_FLAG_ISSET(mep_defect_bitmap_temp, SYS_OAM_DEFECT_FLAG_DLOC_D))
                {
                    CTC_SET_FLAG(g_mep_defect_bitmap[mep_index], SYS_OAM_DEFECT_FLAG_DLOC_D);
                    SYS_OAM_DBG_DUMP("  defect name: %s\n", "SomeRMEPCCMDefect: dloc defect");
                }
                else
                {
                    SYS_OAM_DBG_DUMP("  defect name: %s\n", "SomeRMEPCCMDefect: dloc defect clear");
                }

                break;

            case SYS_OAM_DEFECT_FLAG_RMEP_NOT_FOUND_D:
                if (CTC_FLAG_ISSET(mep_defect_bitmap_temp, SYS_OAM_DEFECT_FLAG_RMEP_NOT_FOUND_D))
                {
                    CTC_SET_FLAG(g_mep_defect_bitmap[mep_index], SYS_OAM_DEFECT_FLAG_RMEP_NOT_FOUND_D);
                    SYS_OAM_DBG_DUMP("  defect name: %s\n", "errorCCMdefect: rmep not found");
                }
                else
                {
                    SYS_OAM_DBG_DUMP("  defect name: %s\n", "errorCCMdefect: rmep not found clear");
                }

                break;

            case SYS_OAM_DEFECT_FLAG_RDI_D:
                if (CTC_FLAG_ISSET(mep_defect_bitmap_temp, SYS_OAM_DEFECT_FLAG_RDI_D))
                {
                    CTC_SET_FLAG(g_mep_defect_bitmap[mep_index], SYS_OAM_DEFECT_FLAG_RDI_D);
                    SYS_OAM_DBG_DUMP("  defect name: %s\n", "SomeRDIdefect: rdi defect");
                }
                else
                {
                    SYS_OAM_DBG_DUMP("  defect name: %s\n", "SomeRDIdefect: rdi defect clear");
                }

                break;

            case SYS_OAM_DEFECT_FLAG_RMEP_SRCMAC_MISMATCH_D:
                if (CTC_FLAG_ISSET(mep_defect_bitmap_temp, SYS_OAM_DEFECT_FLAG_RMEP_SRCMAC_MISMATCH_D))
                {
                    CTC_SET_FLAG(g_mep_defect_bitmap[mep_index], SYS_OAM_DEFECT_FLAG_RMEP_SRCMAC_MISMATCH_D);
                    SYS_OAM_DBG_DUMP("  defect name: %s\n", "RMEP SRC MAC Mismatch");
                }

                break;

            case SYS_OAM_DEFECT_FLAG_MAC_STATUS_D:
                if (CTC_FLAG_ISSET(mep_defect_bitmap_temp, SYS_OAM_DEFECT_FLAG_MAC_STATUS_D))
                {
                    CTC_SET_FLAG(g_mep_defect_bitmap[mep_index], SYS_OAM_DEFECT_FLAG_MAC_STATUS_D);
                    SYS_OAM_DBG_DUMP("  defect name: %s\n", "SomeMACstatusDefect");
                }

                break;

            default:
                SYS_OAM_DBG_DUMP("  defect name: %s\n", "Defect no name");
                break;
            }
        }
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_oam_dump_mep_defect_info(uint8 lchip, uint32 mep_index)
{
    ctc_oam_mep_info_t mep_info;
    uint32 lmep_index = 0;

    sal_memset(&mep_info, 0, sizeof(ctc_oam_mep_info_t));

    mep_info.mep_index = mep_index;

    _sys_greatbelt_oam_get_mep_info(lchip, &mep_info);

    _sys_oam_get_defect_name(&mep_info);

    if (mep_info.is_rmep)
    {
        lmep_index = mep_info.rmep.y1731_rmep.lmep_index;
        mep_info.mep_index = lmep_index;
        mep_info.is_rmep = 0;
        _sys_oam_get_defect_name(&mep_info);
    }

    return CTC_E_NONE;

}

/**
 @brief   report error cache, supposed to be called in timer context
*/
int32
sys_greatbelt_oam_report_error_cache(uint8 lchip)
{
    uint8 defect_index    = 0;
    uint8 mep_bit_index   = 0;
    uint32 mep_index      = 0;
    uint32 mep_index_bitmap = 0;
    uint32 mep_index_base   = 0;
    ctc_oam_defect_info_t defect_info;
    int32 rv = 0;

    sal_memset(&defect_info, 0, sizeof(ctc_oam_defect_info_t));

    rv = _sys_greatbelt_oam_defect_read_defect_status(lchip, &defect_info);
    if (rv < 0)
    {
        SYS_OAM_DBG_INFO("_sys_greatbelt_oam_defect_read_defect_status returned %d\n", rv);
        return rv;
    }

    for (defect_index = 0; defect_index < defect_info.valid_cache_num; defect_index++)
    {
        mep_index_bitmap = defect_info.mep_index_bitmap[defect_index];

        mep_index_base = defect_info.mep_index_base[defect_index];

        for (mep_bit_index = 0; mep_bit_index < 32; mep_bit_index++)
        {
            if (IS_BIT_SET(mep_index_bitmap, mep_bit_index))
            {
                mep_index = mep_index_base * 32 + mep_bit_index;
                sys_greatbelt_oam_dump_mep_defect_info(lchip, mep_index);
            }
        }
    }

    return CTC_E_NONE;
}


int32
sys_greatbelt_oam_bfd_session_up_time(uint8 lchip)
{
#if 0
    uint32 i = 0;
    if (cnt != 0)
    {
        SYS_OAM_DBG_DUMP("index  p cnt f cnt\n");

        for(i = 0; i < cnt; i++)
        {
            SYS_OAM_DBG_DUMP("%d  ,%d ,%d\n", i, g_gb_oam_master[lchip]->p[i], g_gb_oam_master[lchip]->f[i]);
        }

        SYS_OAM_DBG_DUMP("----------+++++++++++++++++\n");
        SYS_OAM_DBG_DUMP("pbit  fbit fbitclear\n");
        for(i = 0; i < 4096; i++)
        {
            SYS_OAM_DBG_DUMP("0x%x  ,0x%x, 0x%x\n", g_gb_oam_master[lchip]->p2f_bit[i], g_gb_oam_master[lchip]->f_bit[i], g_gb_oam_master[lchip]->f_clear_bit[i]);
        }

    }
    else
    {

    }
#endif
    SYS_OAM_INIT_CHECK(lchip);


    SYS_OAM_DBG_DUMP("Total BFD session up cost time is %u:%u\n",
                (g_gb_oam_master[lchip]->tv2.tv_sec - g_gb_oam_master[lchip]->tv1.tv_sec),
                (g_gb_oam_master[lchip]->tv2.tv_usec - g_gb_oam_master[lchip]->tv1.tv_usec));

    g_gb_oam_master[lchip]->tv1.tv_sec    = 0;
    g_gb_oam_master[lchip]->tv1.tv_usec   = 0;

    return CTC_E_NONE;
}


int32
sys_greatbelt_oam_bfd_pf_interval(uint8 lchip, uint32 usec)
{
    SYS_OAM_INIT_CHECK(lchip);
    g_gb_oam_master[lchip]->interval = usec;
    return CTC_E_NONE;
}


int32
sys_greatbelt_oam_bfd_session_state(uint8 lchip, uint32 cnt)
{
    uint32 cmd  = 0;
    int32 ret   = 0;
    uint32 l_index = 0;
    uint32 index = 0;
    uint32 i = 0;

    ds_bfd_rmep_t ds_bfd_rmep;
    ds_bfd_mep_t ds_bfd_mep;

    uint32 up_cnt = 0;
    uint32 down_cnt = 0;

    SYS_OAM_INIT_CHECK(lchip);
    SYS_OAM_DBG_DUMP("Session number is %d \n", cnt);

    cmd = DRV_IOR(OamUpdateCtl_t, OamUpdateCtl_BfdMinptr_f);
    ret = DRV_IOCTL(lchip, 0, cmd, &index);

    for(i =  0; i < cnt; i++)
    {
        l_index = index+ 2*i;

        cmd = DRV_IOR(DsBfdMep_t, DRV_ENTRY_FLAG);
        ret = DRV_IOCTL(lchip, l_index, cmd, &ds_bfd_mep);

        cmd = DRV_IOR(DsBfdRmep_t, DRV_ENTRY_FLAG);
        ret = DRV_IOCTL(lchip, (l_index + 1), cmd, &ds_bfd_rmep);


        if((3 == ds_bfd_mep.local_stat) && (3 == ds_bfd_rmep.remote_stat))
        {
            if((ds_bfd_mep.actual_min_tx_interval != 3)
                || (ds_bfd_rmep.actual_rx_interval != 3))
            {
                SYS_OAM_DBG_DUMP("up index is %d, tx is %d, rx is %d; des tx is %d, req rx is %d\n", l_index,
                    ds_bfd_mep.actual_min_tx_interval, ds_bfd_rmep.actual_rx_interval,
                    ds_bfd_mep.desired_min_tx_interval, ds_bfd_rmep.required_min_rx_interval);
            }
            up_cnt ++;
        }
        else
        {
            if((1 == ds_bfd_mep.local_stat) && (1 == ds_bfd_rmep.remote_stat))
            {
                if((ds_bfd_mep.actual_min_tx_interval != 1000)
                    || (ds_bfd_rmep.actual_rx_interval != 1000))
                {
                    SYS_OAM_DBG_DUMP("down index is %d, tx is %d, rx is %d; des tx is %d, req rx is %d\n", l_index,
                        ds_bfd_mep.actual_min_tx_interval, ds_bfd_rmep.actual_rx_interval,
                        ds_bfd_mep.desired_min_tx_interval, ds_bfd_rmep.required_min_rx_interval);
                }
            }
            SYS_OAM_DBG_DUMP("local index is %d\n", l_index);
            down_cnt++;
        }

        if (ds_bfd_mep.pbit)
        {
            SYS_OAM_DBG_DUMP("P bit local index is %d, \n", l_index);
        }

        if (ds_bfd_rmep.fbit)
        {
            SYS_OAM_DBG_DUMP("F bit local index is %d, \n", l_index);
        }

    }

    SYS_OAM_DBG_DUMP("BFD up session count is %d \n", up_cnt);
    SYS_OAM_DBG_DUMP("BFD down session count is %d \n", down_cnt);

    return ret;
}



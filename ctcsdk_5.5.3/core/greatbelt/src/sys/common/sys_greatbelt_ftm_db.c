/*
 @file sys_greatbelt_ftm_db.c

 @date 2011-11-16

 @version v2.0

 This file provides all sys ftm db function
*/

/****************************************************************************
 *
 * Header Files
 *
 ****************************************************************************/
#include "sal.h"

#include "ctc_const.h"
#include "ctc_error.h"
#include "ctc_debug.h"

#include "sys_greatbelt_chip.h"
#include "sys_greatbelt_ftm.h"
#include "sys_greatbelt_ftm_db.h"

#include "greatbelt/include/drv_io.h"
#include "greatbelt/include/drv_enum.h"

/****************************************************************************
 *
 * Defines and Macros
 *
 ****************************************************************************/

/****************************************************************************
 *
 * Global and Declarations
 *
 ****************************************************************************/

/****************************************************************************
 *
 * Functions
 *
 ****************************************************************************/

enum ip_lookup_mode_e
{
    IP_LKP_MODE_RESERVED,
    IP_LKP_MODE_LPM,
    IP_LKP_MODE_TCAM,
    IP_LKP_MODE_LPM_AND_TCAM
};
typedef enum ip_lookup_mode_e ip_lookup_mode_t;

enum fcoe_lookup_mode_e
{
    FCOE_LKP_MODE_RESERVED,
    FCOE_LKP_MODE_HASH,
    FCOE_LKP_MODE_TCAM,
    FCOE_LKP_MODE_LPM_AND_TCAM
};
typedef enum fcoe_lookup_mode_e fcoe_lookup_mode_e;

enum trill_lookup_mode_e
{
    TRILL_LKP_MODE_RESERVED,
    TRILL_LKP_MODE_HASH,
    TRILL_LKP_MODE_TCAM,
    TRILL_LKP_MODE_LPM_AND_TCAM
};
typedef enum trill_lookup_mode_e trill_lookup_mode_e;

uint32
_sys_greatebelt_ftm_convert_tcam_key_size_cfg(tbls_id_t tcamkey)
{
    uint32 entry_num = TABLE_EXT_INFO_PTR(tcamkey) ? TCAM_KEY_SIZE(tcamkey) / DRV_BYTES_PER_ENTRY : 0;
    uint32 key_size_cfg = 0xF; /* invalid value */

    switch (entry_num)
    {
    case 1:
        key_size_cfg = 0;
        break;

    case 2:
        key_size_cfg = 1;
        break;

    case 4:
        key_size_cfg = 2;
        break;

    case 8:
        key_size_cfg = 3;
        break;

    default:
        key_size_cfg = 0;
        break;
    }

    return key_size_cfg;
}

uint32
_sys_greatebelt_ftm_get_tcam_key_index_shift(tbls_id_t tcamkey)
{
    uint32 key_size = 0;

    key_size = TABLE_EXT_INFO_PTR(tcamkey) ? TCAM_KEY_SIZE(tcamkey) / DRV_BYTES_PER_ENTRY : 0;
    if (!key_size)
    {
        return key_size;
    }

    switch (key_size)
    {
    case 1:
        key_size = 0;
        break;

    case 2:
        key_size = 1;
        break;

    /*640 bit and 320 bit use 320bit index shift*/
    case 4:
    case 8:
        key_size = 2;
        break;

    default:
        break;
    }

    return key_size;
}

uint32
_sys_greatebelt_ftm_get_tcam_key_index_base(tbls_id_t tcamkey)
{
    uint32 hw_data_base = 0, index_base = 0;

    hw_data_base = TABLE_DATA_BASE(tcamkey);
    index_base = (hw_data_base - DRV_INT_TCAM_KEY_DATA_ASIC_BASE) / DRV_BYTES_PER_ENTRY;

    return (index_base >> 6);
}

uint32
_sys_greatebelt_ftm_get_ad_table_base(tbls_id_t ad_table, uint8 is_tcam_ad)
{
    uint32 hw_data_base = 0, index_base = 0;

    hw_data_base = TABLE_DATA_BASE(ad_table);

    if (!TABLE_MAX_INDEX(ad_table))
    {
        return 0;
    }

    if (is_tcam_ad)
    {
        if (DRV_ADDR_IN_TCAMAD_SRAM_8W_RANGE(hw_data_base))
        {
            index_base = (hw_data_base - DRV_INT_TCAM_AD_MEM_8W_BASE) / DRV_ADDR_BYTES_PER_ENTRY;
        }
        else
        {
            index_base = (hw_data_base - DRV_INT_TCAM_AD_MEM_4W_BASE) / DRV_ADDR_BYTES_PER_ENTRY;
        }
    }
    else
    {
        if (DRV_ADDR_IN_DYNAMIC_SRAM_8W_RANGE(hw_data_base))
        {
            index_base = (hw_data_base - DRV_MEMORY0_BASE_8W) / DRV_ADDR_BYTES_PER_ENTRY;
        }
        else
        {
            index_base = (hw_data_base - DRV_MEMORY0_BASE_4W) / DRV_ADDR_BYTES_PER_ENTRY;
        }
    }

    if (index_base % 64)
    {
        SYS_FTM_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%% Attention!! TableId = %d, indexBase mod 64 is not integral!\n", ad_table);
    }

    return (index_base >> 6);
}

STATIC int32
_sys_greatebelt_ftm_init_tcam_userid_ctl_register(uint8 lchip)
{
#define GET_USERID_LKPCTL_CFG1(tcam_key_size, tcam_tbl_id, tcam_sub_tbl_id) (((tcam_key_size) << 5) | ((tcam_tbl_id) << 2) | (tcam_sub_tbl_id))
#define GET_USERID_LKPCTL_CFG2(tcam_tbl_id, tcam_sub_tbl_id) (((tcam_tbl_id) << 2) | (tcam_sub_tbl_id))
#define GET_TUNNELID_LKPCTL_CFG1(tcam_key_size, tcam_tbl_id, tcam_sub_tbl_id) (((tcam_key_size) << 6) | ((tcam_tbl_id) << 3) | (tcam_sub_tbl_id))
#define GET_TUNNELID_LKPCTL_CFG2(tcam_tbl_id, tcam_sub_tbl_id) (((tcam_tbl_id) << 3) | (tcam_sub_tbl_id))

    uint32 cmd = 0;
    ipe_user_id_ctl_t ipe_userid_ctl;
    uint32 tcam_key_size = 0, tcam_tbl_id = 0, tcam_sub_tbl_id = 0;

    sal_memset(&ipe_userid_ctl, 0, sizeof(ipe_userid_ctl));

    cmd = DRV_IOR(IpeUserIdCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_userid_ctl));

    /*********** UserID lookup ctl setting ***********/
    tcam_tbl_id = USERID_TABLEID;
    /* UserId Mac */
    tcam_key_size = _sys_greatebelt_ftm_convert_tcam_key_size_cfg(DsUserIdMacKey_t);
    tcam_sub_tbl_id = USERID_MAC_SUB_TABLEID;
    ipe_userid_ctl.lookup_ctl0 = GET_USERID_LKPCTL_CFG1(tcam_key_size, tcam_tbl_id, tcam_sub_tbl_id);

    /* UserId IPv6 */
    tcam_key_size = _sys_greatebelt_ftm_convert_tcam_key_size_cfg(DsUserIdIpv6Key_t);
    tcam_sub_tbl_id = USERID_IPV6_SUB_TABLEID;
    ipe_userid_ctl.lookup_ctl1 = GET_USERID_LKPCTL_CFG1(tcam_key_size, tcam_tbl_id, tcam_sub_tbl_id);

    /* UserId IPv4 */
    tcam_key_size = _sys_greatebelt_ftm_convert_tcam_key_size_cfg(DsUserIdIpv4Key_t);
    tcam_sub_tbl_id = USERID_IPV4_SUB_TABLEID;
    ipe_userid_ctl.lookup_ctl2 = GET_USERID_LKPCTL_CFG1(tcam_key_size, tcam_tbl_id, tcam_sub_tbl_id);

    /* UserId VLAN */
    tcam_sub_tbl_id = USERID_VLAN_SUB_TABLEID;
    ipe_userid_ctl.lookup_ctl3 = GET_USERID_LKPCTL_CFG2(tcam_tbl_id, tcam_sub_tbl_id);

    /*********** TunnelID lookup ctl setting ***********/
    tcam_tbl_id = TUNNELID_TABLEID;
    /* TunnelId IPv6 */
    tcam_key_size = _sys_greatebelt_ftm_convert_tcam_key_size_cfg(DsTunnelIdIpv6Key_t);
    tcam_sub_tbl_id = TUNNELID_IPV6_SUB_TABLEID;
    ipe_userid_ctl.lookup_ctl4 = GET_TUNNELID_LKPCTL_CFG1(tcam_key_size, tcam_tbl_id, tcam_sub_tbl_id);

    /* TunnelId IPv4 */
    tcam_key_size = _sys_greatebelt_ftm_convert_tcam_key_size_cfg(DsTunnelIdIpv4Key_t);
    tcam_sub_tbl_id = TUNNELID_IPV4_SUB_TABLEID;
    ipe_userid_ctl.lookup_ctl5 = GET_TUNNELID_LKPCTL_CFG1(tcam_key_size, tcam_tbl_id, tcam_sub_tbl_id);

    /* TunnelId PBB  */
    tcam_sub_tbl_id = TUNNELID_PBB_SUB_TABLEID;
    ipe_userid_ctl.lookup_ctl6 = GET_TUNNELID_LKPCTL_CFG2(tcam_tbl_id, tcam_sub_tbl_id);

    /* TunnelId WLAN*/
    tcam_sub_tbl_id = TUNNELID_WLAN_SUB_TABLEID;
    ipe_userid_ctl.lookup_ctl7 = GET_TUNNELID_LKPCTL_CFG2(tcam_tbl_id, tcam_sub_tbl_id);

    /* TunnelId TRILL */
    tcam_sub_tbl_id = TUNNELID_TRILL_SUB_TABLEID;
    ipe_userid_ctl.lookup_ctl8 = GET_TUNNELID_LKPCTL_CFG2(tcam_tbl_id, tcam_sub_tbl_id);

    cmd = DRV_IOW(IpeUserIdCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_userid_ctl));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatebelt_ftm_init_tcam_lookup_ctl_register(uint32 lchip)
{
#define GET_ACL_LKPCTL_CFG(tcam_key_size, tcam_sub_tbl_id) (((tcam_key_size) << 2) | (tcam_sub_tbl_id))
#define GET_IP_LKPCTL_CFG1(lookup_mode, tcam_key_size, tcam_tbl_id, tcam_sub_tbl_id) (((lookup_mode) << 7) | ((tcam_key_size) << 5) | ((tcam_tbl_id) << 2) | (tcam_sub_tbl_id))
#define GET_IP_LKPCTL_CFG2(tcam_key_size, tcam_tbl_id, tcam_sub_tbl_id) (((tcam_key_size) << 5) | ((tcam_tbl_id) << 2) | (tcam_sub_tbl_id))
#define GET_FCOE_TRILL_LKPCTL_CFG(lookup_mode, tcam_tbl_id, tcam_sub_tbl_id) (((lookup_mode) << 5) | ((tcam_tbl_id) << 2) | (tcam_sub_tbl_id))
    uint32 cmd = 0;
    ipe_lookup_ctl_t ipe_lookup_ctl;
    epe_acl_qos_ctl_t epe_acl_ctl;
    uint32 tcam_key_size = 0, tcam_tbl_id = 0, tcam_sub_tbl_id = 0;
    uint8 ip_lookup_mode = 0, fcoe_lookup_mode = 0, trill_lookup_mode = 0;
    uint32 entry_num = 0;

    sal_memset(&epe_acl_ctl, 0, sizeof(epe_acl_ctl));
    sal_memset(&ipe_lookup_ctl, 0, sizeof(ipe_lookup_ctl));

    cmd = DRV_IOR(IpeLookupCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_lookup_ctl));

    /*--------- ACL lookup ctl setting --------*/
    /* ACL IPv6 */
    tcam_key_size = _sys_greatebelt_ftm_convert_tcam_key_size_cfg(DsAclIpv6Key0_t);
    tcam_sub_tbl_id = ACL_IPV6_SUB_TABLEID;
    ipe_lookup_ctl.acl_qos_lookup_ctl0 = GET_ACL_LKPCTL_CFG(tcam_key_size, tcam_sub_tbl_id);
    epe_acl_ctl.acl_qos_lookup_ctl0 = GET_ACL_LKPCTL_CFG(tcam_key_size, tcam_sub_tbl_id);

    /* ACL IPv4 */
    tcam_key_size = _sys_greatebelt_ftm_convert_tcam_key_size_cfg(DsAclIpv4Key0_t);
    tcam_sub_tbl_id = ACL_IPV4_SUB_TABLEID;
    ipe_lookup_ctl.acl_qos_lookup_ctl1 = GET_ACL_LKPCTL_CFG(tcam_key_size, tcam_sub_tbl_id);
    epe_acl_ctl.acl_qos_lookup_ctl1 = GET_ACL_LKPCTL_CFG(tcam_key_size, tcam_sub_tbl_id);

    /* ACL MAC */
    tcam_key_size = _sys_greatebelt_ftm_convert_tcam_key_size_cfg(DsAclMacKey0_t);
    tcam_sub_tbl_id = ACL_MAC_SUB_TABLEID;
    ipe_lookup_ctl.acl_qos_lookup_ctl2 = GET_ACL_LKPCTL_CFG(tcam_key_size, tcam_sub_tbl_id);
    epe_acl_ctl.acl_qos_lookup_ctl2 = GET_ACL_LKPCTL_CFG(tcam_key_size, tcam_sub_tbl_id);

    /* ACL MPLS */
    tcam_key_size = _sys_greatebelt_ftm_convert_tcam_key_size_cfg(DsAclMplsKey0_t);
    tcam_sub_tbl_id = ACL_MPLS_SUB_TABLEID;
    ipe_lookup_ctl.acl_qos_lookup_ctl3 = GET_ACL_LKPCTL_CFG(tcam_key_size, tcam_sub_tbl_id);
    epe_acl_ctl.acl_qos_lookup_ctl3 = GET_ACL_LKPCTL_CFG(tcam_key_size, tcam_sub_tbl_id);

    /*--------- IPv4DA & MAC lookup ctl setting --------*/
    tcam_tbl_id = IPV4DA_MAC_TABLEID;
    /* IPv4UcastDa */
    sys_greatbelt_ftm_get_table_entry_num(lchip, DsIpv4UcastRouteKey_t, &entry_num);
    if(0 == entry_num)
    {
        ip_lookup_mode = IP_LKP_MODE_LPM;
    }
    else
    {
        ip_lookup_mode = IP_LKP_MODE_LPM_AND_TCAM;
    }
    tcam_key_size = _sys_greatebelt_ftm_convert_tcam_key_size_cfg(DsIpv4UcastRouteKey_t);
    tcam_sub_tbl_id = IPV4DA_UCAST_SUB_TABLEID;
    ipe_lookup_ctl.ip_da_lookup_ctl0 = GET_IP_LKPCTL_CFG1(ip_lookup_mode, tcam_key_size, tcam_tbl_id, tcam_sub_tbl_id);

    /* IPv4McastDa */
    ip_lookup_mode = IP_LKP_MODE_LPM;
    tcam_key_size = _sys_greatebelt_ftm_convert_tcam_key_size_cfg(DsIpv4McastRouteKey_t);
    tcam_sub_tbl_id = IPV4DA_MCAST_SUB_TABLEID;
    ipe_lookup_ctl.ip_da_lookup_ctl1 = GET_IP_LKPCTL_CFG1(ip_lookup_mode, tcam_key_size, tcam_tbl_id, tcam_sub_tbl_id);

    /* MacDa */
    tcam_key_size = _sys_greatebelt_ftm_convert_tcam_key_size_cfg(DsMacBridgeKey_t);
    tcam_sub_tbl_id = MAC_DA_SUB_TABLEID;
    ipe_lookup_ctl.mac_da_lookup_ctl = GET_IP_LKPCTL_CFG2(tcam_key_size, tcam_tbl_id, tcam_sub_tbl_id);

    /* MacSa */
    tcam_sub_tbl_id = MAC_SA_SUB_TABLEID;
    ipe_lookup_ctl.mac_sa_lookup_ctl = GET_IP_LKPCTL_CFG2(tcam_key_size, tcam_tbl_id, tcam_sub_tbl_id);

    /* IPv4MacDa */
    tcam_key_size = _sys_greatebelt_ftm_convert_tcam_key_size_cfg(DsMacIpv4Key_t);
    tcam_sub_tbl_id = MAC_IPV4_SUB_TABLEID;
    ipe_lookup_ctl.mac_ipv4_lookup_ctl = GET_IP_LKPCTL_CFG2(tcam_key_size, tcam_tbl_id, tcam_sub_tbl_id);

    /*--------- IPv6DA lookup ctl setting --------*/
    tcam_tbl_id = IPV6DA_TABLEID;

    /* IPv6UcastDa */
    sys_greatbelt_ftm_get_table_entry_num(lchip, DsIpv6UcastRouteKey_t, &entry_num);
    if(0 == entry_num)
    {
        ip_lookup_mode = IP_LKP_MODE_LPM;
    }
    else
    {
        ip_lookup_mode = IP_LKP_MODE_LPM_AND_TCAM;
    }
    tcam_key_size = _sys_greatebelt_ftm_convert_tcam_key_size_cfg(DsIpv6UcastRouteKey_t);
    tcam_sub_tbl_id = IPV6DA_UCAST_SUB_TABLEID;
    ipe_lookup_ctl.ip_da_lookup_ctl2 = GET_IP_LKPCTL_CFG1(ip_lookup_mode, tcam_key_size, tcam_tbl_id, tcam_sub_tbl_id);

    /* IPv6McastDa */
    tcam_key_size = _sys_greatebelt_ftm_convert_tcam_key_size_cfg(DsIpv6McastRouteKey_t);
    tcam_sub_tbl_id = IPV6DA_MCAST_SUB_TABLEID;
    ipe_lookup_ctl.ip_da_lookup_ctl3   = GET_IP_LKPCTL_CFG2(tcam_key_size, tcam_tbl_id, tcam_sub_tbl_id);

    /* IPv6MACDa */
    tcam_key_size = _sys_greatebelt_ftm_convert_tcam_key_size_cfg(DsMacIpv6Key_t);
    tcam_sub_tbl_id = MAC_IPV6_SUB_TABLEID;
    ipe_lookup_ctl.mac_ipv6_lookup_ctl = GET_IP_LKPCTL_CFG2(tcam_key_size, tcam_tbl_id, tcam_sub_tbl_id);


    /*--------- IPv4SA lookup ctl setting --------*/

    /* IPv4Sa RPF table same as ipv4ucast (special!!!) */
    tcam_tbl_id = IPV4DA_MAC_TABLEID;
    ip_lookup_mode = IP_LKP_MODE_LPM_AND_TCAM;
    tcam_key_size = _sys_greatebelt_ftm_convert_tcam_key_size_cfg(DsIpv4RpfKey_t);
    tcam_sub_tbl_id = IPV4SA_RPF_SUB_TABLEID;
    ipe_lookup_ctl.ip_sa_lookup_ctl0 = GET_IP_LKPCTL_CFG1(ip_lookup_mode, tcam_key_size, tcam_tbl_id, tcam_sub_tbl_id);

    tcam_tbl_id = IPV4SA_TABLEID;

    /* IPv4Sa NAT */
    ip_lookup_mode = IP_LKP_MODE_LPM_AND_TCAM;
    tcam_key_size = _sys_greatebelt_ftm_convert_tcam_key_size_cfg(DsIpv4NatKey_t);
    tcam_sub_tbl_id = IPV4SA_NAT_SUB_TABLEID;
    ipe_lookup_ctl.ip_sa_lookup_ctl2 = GET_IP_LKPCTL_CFG1(ip_lookup_mode, tcam_key_size, tcam_tbl_id, tcam_sub_tbl_id);

    /* IPv4Sa PBR */
    tcam_key_size = _sys_greatebelt_ftm_convert_tcam_key_size_cfg(DsIpv4PbrKey_t);
    tcam_sub_tbl_id = IPV4SA_PBR_SUB_TABLEID;
    ipe_lookup_ctl.ip_sa_lookup_ctl4 = GET_IP_LKPCTL_CFG2(tcam_key_size, tcam_tbl_id, tcam_sub_tbl_id);

    /*--------- IPv6SA lookup ctl setting --------*/

    /* IPv6Sa RPF table same as ipv6ucast (special!!!) */
    tcam_tbl_id = IPV6DA_TABLEID;
    ip_lookup_mode = IP_LKP_MODE_LPM_AND_TCAM;
    tcam_key_size = _sys_greatebelt_ftm_convert_tcam_key_size_cfg(DsIpv6RpfKey_t);
    tcam_sub_tbl_id = IPV6SA_RPF_SUB_TABLEID;
    ipe_lookup_ctl.ip_sa_lookup_ctl1 = GET_IP_LKPCTL_CFG1(ip_lookup_mode, tcam_key_size, tcam_tbl_id, tcam_sub_tbl_id);

    tcam_tbl_id = IPV6SA_TABLEID;
    /* IPv6Sa NAT */
    ip_lookup_mode = IP_LKP_MODE_LPM_AND_TCAM;
    tcam_key_size = _sys_greatebelt_ftm_convert_tcam_key_size_cfg(DsIpv6NatKey_t);
    tcam_sub_tbl_id = IPV6SA_NAT_SUB_TABLEID;
    ipe_lookup_ctl.ip_sa_lookup_ctl3 = GET_IP_LKPCTL_CFG1(ip_lookup_mode, tcam_key_size, tcam_tbl_id, tcam_sub_tbl_id);

    /* IPv6Sa PBR */
    tcam_key_size = _sys_greatebelt_ftm_convert_tcam_key_size_cfg(DsIpv6PbrKey_t);
    tcam_sub_tbl_id = IPV6SA_PBR_SUB_TABLEID;
    ipe_lookup_ctl.ip_sa_lookup_ctl5 = GET_IP_LKPCTL_CFG2(tcam_key_size, tcam_tbl_id, tcam_sub_tbl_id);

    /*--------- TRILL/FCoE lookup ctl setting --------*/
    tcam_tbl_id = TRILL_FCOE_TABLEID;
    /* FCoE DA */
    fcoe_lookup_mode = FCOE_LKP_MODE_RESERVED;
    tcam_sub_tbl_id = FCOE_DA_SUB_TABLEID;
    ipe_lookup_ctl.fcoe_lookup_ctl0 = GET_FCOE_TRILL_LKPCTL_CFG(fcoe_lookup_mode, tcam_tbl_id, tcam_sub_tbl_id);

    /* FCoE SA */
    tcam_sub_tbl_id = FCOE_SA_SUB_TABLEID;
    ipe_lookup_ctl.fcoe_lookup_ctl1 = GET_FCOE_TRILL_LKPCTL_CFG(fcoe_lookup_mode, tcam_tbl_id, tcam_sub_tbl_id);

    /* Trill DA Uc */
    trill_lookup_mode = TRILL_LKP_MODE_RESERVED;
    tcam_sub_tbl_id = TRILL_DA_UC_SUB_TABLEID;
    ipe_lookup_ctl.trill_da_lookup_ctl0 = GET_FCOE_TRILL_LKPCTL_CFG(trill_lookup_mode, tcam_tbl_id, tcam_sub_tbl_id);

    /* Trill DA Mc */
    trill_lookup_mode = TRILL_LKP_MODE_RESERVED;
    tcam_sub_tbl_id = TRILL_DA_MC_SUB_TABLEID;
    ipe_lookup_ctl.trill_da_lookup_ctl1 = GET_FCOE_TRILL_LKPCTL_CFG(trill_lookup_mode, tcam_tbl_id, tcam_sub_tbl_id);

    cmd = DRV_IOW(IpeLookupCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_lookup_ctl));

    cmd = DRV_IOW(EpeAclQosCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_acl_ctl));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatebelt_ftm_init_tcam_lookupresult_ctl_register(uint8 lchip)
{
    uint32 cmd = 0;
    tcam_engine_lookup_result_ctl0_t tcam_lkp_rst_ctl0; /* include ACL lookupResult */
    tcam_engine_lookup_result_ctl1_t tcam_lkp_rst_ctl1; /* include UserId & OAM lookupResult */
    tcam_engine_lookup_result_ctl2_t tcam_lkp_rst_ctl2; /* include IP & MAC lookupResult */
    tcam_engine_lookup_result_ctl3_t tcam_lkp_rst_ctl3; /* include FCoE & Trill lookupResult */

    sal_memset(&tcam_lkp_rst_ctl0, 0, sizeof(tcam_lkp_rst_ctl0));
    sal_memset(&tcam_lkp_rst_ctl1, 0, sizeof(tcam_lkp_rst_ctl1));
    sal_memset(&tcam_lkp_rst_ctl2, 0, sizeof(tcam_lkp_rst_ctl2));
    sal_memset(&tcam_lkp_rst_ctl3, 0, sizeof(tcam_lkp_rst_ctl3));

    /*********** ACL lookupResult control setting ***********/
    /* ACL IPv6 key0-key3 */
    tcam_lkp_rst_ctl0.acl0_index_shift0 = _sys_greatebelt_ftm_get_tcam_key_index_shift(DsAclIpv6Key0_t);
    tcam_lkp_rst_ctl0.acl0_index_base0 = _sys_greatebelt_ftm_get_tcam_key_index_base(DsAclIpv6Key0_t);
    tcam_lkp_rst_ctl0.acl0_table_base0 = _sys_greatebelt_ftm_get_ad_table_base(DsIpv6Acl0Tcam_t, TRUE);
    tcam_lkp_rst_ctl0.acl1_index_shift0 = _sys_greatebelt_ftm_get_tcam_key_index_shift(DsAclIpv6Key1_t);
    tcam_lkp_rst_ctl0.acl1_index_base0 = _sys_greatebelt_ftm_get_tcam_key_index_base(DsAclIpv6Key1_t);
    tcam_lkp_rst_ctl0.acl1_table_base0 = _sys_greatebelt_ftm_get_ad_table_base(DsIpv6Acl1Tcam_t, TRUE);

    /* ACL IPv4 key0-key3 */
    tcam_lkp_rst_ctl0.acl0_index_shift1 = _sys_greatebelt_ftm_get_tcam_key_index_shift(DsAclIpv4Key0_t);
    tcam_lkp_rst_ctl0.acl0_index_base1 = _sys_greatebelt_ftm_get_tcam_key_index_base(DsAclIpv4Key0_t);
    tcam_lkp_rst_ctl0.acl0_table_base1 = _sys_greatebelt_ftm_get_ad_table_base(DsIpv4Acl0Tcam_t, TRUE);
    tcam_lkp_rst_ctl0.acl1_index_shift1 = _sys_greatebelt_ftm_get_tcam_key_index_shift(DsAclIpv4Key1_t);
    tcam_lkp_rst_ctl0.acl1_index_base1 = _sys_greatebelt_ftm_get_tcam_key_index_base(DsAclIpv4Key1_t);
    tcam_lkp_rst_ctl0.acl1_table_base1 = _sys_greatebelt_ftm_get_ad_table_base(DsIpv4Acl1Tcam_t, TRUE);
    tcam_lkp_rst_ctl0.acl2_index_shift1 = _sys_greatebelt_ftm_get_tcam_key_index_shift(DsAclIpv4Key2_t);
    tcam_lkp_rst_ctl0.acl2_index_base1 = _sys_greatebelt_ftm_get_tcam_key_index_base(DsAclIpv4Key2_t);
    tcam_lkp_rst_ctl0.acl2_table_base1 = _sys_greatebelt_ftm_get_ad_table_base(DsIpv4Acl2Tcam_t, TRUE);
    tcam_lkp_rst_ctl0.acl3_index_shift1 = _sys_greatebelt_ftm_get_tcam_key_index_shift(DsAclIpv4Key3_t);
    tcam_lkp_rst_ctl0.acl3_index_base1 = _sys_greatebelt_ftm_get_tcam_key_index_base(DsAclIpv4Key3_t);
    tcam_lkp_rst_ctl0.acl3_table_base1 = _sys_greatebelt_ftm_get_ad_table_base(DsIpv4Acl3Tcam_t, TRUE);

    /* ACL Mac key0-key3 */
    tcam_lkp_rst_ctl0.acl0_index_shift2 = _sys_greatebelt_ftm_get_tcam_key_index_shift(DsAclMacKey0_t);
    tcam_lkp_rst_ctl0.acl0_index_base2 = _sys_greatebelt_ftm_get_tcam_key_index_base(DsAclMacKey0_t);
    tcam_lkp_rst_ctl0.acl0_table_base2 = _sys_greatebelt_ftm_get_ad_table_base(DsMacAcl0Tcam_t, TRUE);
    tcam_lkp_rst_ctl0.acl1_index_shift2 = _sys_greatebelt_ftm_get_tcam_key_index_shift(DsAclMacKey1_t);
    tcam_lkp_rst_ctl0.acl1_index_base2 = _sys_greatebelt_ftm_get_tcam_key_index_base(DsAclMacKey1_t);
    tcam_lkp_rst_ctl0.acl1_table_base2 = _sys_greatebelt_ftm_get_ad_table_base(DsMacAcl1Tcam_t, TRUE);
    tcam_lkp_rst_ctl0.acl2_index_shift2 = _sys_greatebelt_ftm_get_tcam_key_index_shift(DsAclMacKey2_t);
    tcam_lkp_rst_ctl0.acl2_index_base2 = _sys_greatebelt_ftm_get_tcam_key_index_base(DsAclMacKey2_t);
    tcam_lkp_rst_ctl0.acl2_table_base2 = _sys_greatebelt_ftm_get_ad_table_base(DsMacAcl2Tcam_t, TRUE);
    tcam_lkp_rst_ctl0.acl3_index_shift2 = _sys_greatebelt_ftm_get_tcam_key_index_shift(DsAclMacKey3_t);
    tcam_lkp_rst_ctl0.acl3_index_base2 = _sys_greatebelt_ftm_get_tcam_key_index_base(DsAclMacKey3_t);
    tcam_lkp_rst_ctl0.acl3_table_base2 = _sys_greatebelt_ftm_get_ad_table_base(DsMacAcl3Tcam_t, TRUE);

    /*********** UserID and OAM ***********/
    /* UserID Mac */
    tcam_lkp_rst_ctl1.user_id_index_shift0 = _sys_greatebelt_ftm_get_tcam_key_index_shift(DsUserIdMacKey_t);
    tcam_lkp_rst_ctl1.user_id_index_base0 = _sys_greatebelt_ftm_get_tcam_key_index_base(DsUserIdMacKey_t);
    tcam_lkp_rst_ctl1.user_id_table_base0 = _sys_greatebelt_ftm_get_ad_table_base(DsUserIdMacTcam_t, TRUE);

    /* UserID IPv6 */
    tcam_lkp_rst_ctl1.user_id_index_shift1 = _sys_greatebelt_ftm_get_tcam_key_index_shift(DsUserIdIpv6Key_t);
    tcam_lkp_rst_ctl1.user_id_index_base1 = _sys_greatebelt_ftm_get_tcam_key_index_base(DsUserIdIpv6Key_t);
    tcam_lkp_rst_ctl1.user_id_table_base1 = _sys_greatebelt_ftm_get_ad_table_base(DsUserIdIpv6Tcam_t, TRUE);

    /* UserID IPv4 */
    tcam_lkp_rst_ctl1.user_id_index_shift2 = _sys_greatebelt_ftm_get_tcam_key_index_shift(DsUserIdIpv4Key_t);
    tcam_lkp_rst_ctl1.user_id_index_base2 = _sys_greatebelt_ftm_get_tcam_key_index_base(DsUserIdIpv4Key_t);
    tcam_lkp_rst_ctl1.user_id_table_base2 = _sys_greatebelt_ftm_get_ad_table_base(DsUserIdIpv4Tcam_t, TRUE);

    /* UserID Vlan */
    tcam_lkp_rst_ctl1.user_id_index_shift3 = _sys_greatebelt_ftm_get_tcam_key_index_shift(DsUserIdVlanKey_t);
    tcam_lkp_rst_ctl1.user_id_index_base3 = _sys_greatebelt_ftm_get_tcam_key_index_base(DsUserIdVlanKey_t);
    tcam_lkp_rst_ctl1.user_id_table_base3 = _sys_greatebelt_ftm_get_ad_table_base(DsUserIdVlanTcam_t, TRUE);

    /* TunnleIpv6 */
    tcam_lkp_rst_ctl1.user_id_index_shift4 = _sys_greatebelt_ftm_get_tcam_key_index_shift(DsTunnelIdIpv6Key_t);
    tcam_lkp_rst_ctl1.user_id_index_base4 = _sys_greatebelt_ftm_get_tcam_key_index_base(DsTunnelIdIpv6Key_t);
    tcam_lkp_rst_ctl1.user_id_table_base4 = _sys_greatebelt_ftm_get_ad_table_base(DsTunnelIdIpv6Tcam_t, TRUE);

    /* TunnleIpv4 */
    tcam_lkp_rst_ctl1.user_id_index_shift5 = _sys_greatebelt_ftm_get_tcam_key_index_shift(DsTunnelIdIpv4Key_t);
    tcam_lkp_rst_ctl1.user_id_index_base5 = _sys_greatebelt_ftm_get_tcam_key_index_base(DsTunnelIdIpv4Key_t);
    tcam_lkp_rst_ctl1.user_id_table_base5 = _sys_greatebelt_ftm_get_ad_table_base(DsTunnelIdIpv4Tcam_t, TRUE);

    /* TunnlePBB */
    tcam_lkp_rst_ctl1.user_id_index_shift6 = _sys_greatebelt_ftm_get_tcam_key_index_shift(DsTunnelIdPbbKey_t);
    tcam_lkp_rst_ctl1.user_id_index_base6 = _sys_greatebelt_ftm_get_tcam_key_index_base(DsTunnelIdPbbKey_t);
    tcam_lkp_rst_ctl1.user_id_table_base6 = _sys_greatebelt_ftm_get_ad_table_base(DsTunnelIdPbbTcam_t, TRUE);

    /* TunnleWlan */
    tcam_lkp_rst_ctl1.user_id_index_shift7 = _sys_greatebelt_ftm_get_tcam_key_index_shift(DsTunnelIdCapwapKey_t);
    tcam_lkp_rst_ctl1.user_id_index_base7 = _sys_greatebelt_ftm_get_tcam_key_index_base(DsTunnelIdCapwapKey_t);
    tcam_lkp_rst_ctl1.user_id_table_base7 = _sys_greatebelt_ftm_get_ad_table_base(DsTunnelIdCapwapTcam_t, TRUE);

    /* TunnleTrill */
    tcam_lkp_rst_ctl1.user_id_index_shift8 = _sys_greatebelt_ftm_get_tcam_key_index_shift(DsTunnelIdTrillKey_t);
    tcam_lkp_rst_ctl1.user_id_index_base8 = _sys_greatebelt_ftm_get_tcam_key_index_base(DsTunnelIdTrillKey_t);
    tcam_lkp_rst_ctl1.user_id_table_base8 = _sys_greatebelt_ftm_get_ad_table_base(DsTunnelIdTrillTcam_t, TRUE);

    /*********** IP and MAC ***********/
    /* IPv4 Uc */
    tcam_lkp_rst_ctl2.ip_da_index_shift0 = _sys_greatebelt_ftm_get_tcam_key_index_shift(DsIpv4UcastRouteKey_t);
    tcam_lkp_rst_ctl2.ip_da_index_base0 = _sys_greatebelt_ftm_get_tcam_key_index_base(DsIpv4UcastRouteKey_t);
    tcam_lkp_rst_ctl2.ip_da_table_base0 = _sys_greatebelt_ftm_get_ad_table_base(DsIpv4UcastDaTcam_t, TRUE);

    /* IPv4 Mc */
    tcam_lkp_rst_ctl2.ip_da_index_shift1 = _sys_greatebelt_ftm_get_tcam_key_index_shift(DsIpv4McastRouteKey_t);
    tcam_lkp_rst_ctl2.ip_da_index_base1 = _sys_greatebelt_ftm_get_tcam_key_index_base(DsIpv4McastRouteKey_t);
    tcam_lkp_rst_ctl2.ip_da_table_base1 = _sys_greatebelt_ftm_get_ad_table_base(DsIpv4McastDaTcam_t, TRUE);

    /* IPv4 Uc RPF */
    tcam_lkp_rst_ctl2.ip_sa_index_shift0 = _sys_greatebelt_ftm_get_tcam_key_index_shift(DsIpv4RpfKey_t);
    tcam_lkp_rst_ctl2.ip_sa_index_base0 = _sys_greatebelt_ftm_get_tcam_key_index_base(DsIpv4RpfKey_t);
    tcam_lkp_rst_ctl2.ip_sa_table_base0 = _sys_greatebelt_ftm_get_ad_table_base(DsIpv4UcastDaTcam_t, TRUE);

    /* IPv4 NAT */
    tcam_lkp_rst_ctl2.ip_sa_index_shift2 = _sys_greatebelt_ftm_get_tcam_key_index_shift(DsIpv4NatKey_t);
    tcam_lkp_rst_ctl2.ip_sa_index_base2 = _sys_greatebelt_ftm_get_tcam_key_index_base(DsIpv4NatKey_t);
    tcam_lkp_rst_ctl2.ip_sa_table_base2 = _sys_greatebelt_ftm_get_ad_table_base(DsIpv4SaNatTcam_t, TRUE);

    /* IPv4 PBR */
    tcam_lkp_rst_ctl2.ip_sa_index_shift4 = _sys_greatebelt_ftm_get_tcam_key_index_shift(DsIpv4PbrKey_t);
    tcam_lkp_rst_ctl2.ip_sa_index_base4 = _sys_greatebelt_ftm_get_tcam_key_index_base(DsIpv4PbrKey_t);
    tcam_lkp_rst_ctl2.ip_sa_table_base4 = _sys_greatebelt_ftm_get_ad_table_base(DsIpv4UcastPbrDualDaTcam_t, TRUE);

    /* IPv6 Uc */
    if (sys_greatbelt_ftm_get_ip_tcam_share_mode(lchip))
    {
        tcam_lkp_rst_ctl2.ip_da_index_shift2 = _sys_greatebelt_ftm_get_tcam_key_index_shift(DsIpv6UcastRouteKey_t)/2;
    }
    else
    {
        tcam_lkp_rst_ctl2.ip_da_index_shift2 = _sys_greatebelt_ftm_get_tcam_key_index_shift(DsIpv6UcastRouteKey_t);
    }
    
    tcam_lkp_rst_ctl2.ip_da_index_base2 = _sys_greatebelt_ftm_get_tcam_key_index_base(DsIpv6UcastRouteKey_t);
    tcam_lkp_rst_ctl2.ip_da_table_base2 = _sys_greatebelt_ftm_get_ad_table_base(DsIpv6UcastDaTcam_t, TRUE);

    /* IPv6 Mc */
    if (sys_greatbelt_ftm_get_ip_tcam_share_mode(lchip))
    {
        tcam_lkp_rst_ctl2.ip_da_index_shift3 = _sys_greatebelt_ftm_get_tcam_key_index_shift(DsIpv6McastRouteKey_t)/2;
    }
    else
    {
        tcam_lkp_rst_ctl2.ip_da_index_shift3 = _sys_greatebelt_ftm_get_tcam_key_index_shift(DsIpv6McastRouteKey_t);
    }
    tcam_lkp_rst_ctl2.ip_da_index_base3 = _sys_greatebelt_ftm_get_tcam_key_index_base(DsIpv6McastRouteKey_t);
    tcam_lkp_rst_ctl2.ip_da_table_base3 = _sys_greatebelt_ftm_get_ad_table_base(DsIpv6McastDaTcam_t, TRUE);

    /* IPv6 RPF */
    if (sys_greatbelt_ftm_get_ip_tcam_share_mode(lchip))
    {
        tcam_lkp_rst_ctl2.ip_sa_index_shift1 = _sys_greatebelt_ftm_get_tcam_key_index_shift(DsIpv6RpfKey_t)/2;
    }
    else
    {
        tcam_lkp_rst_ctl2.ip_sa_index_shift1 = _sys_greatebelt_ftm_get_tcam_key_index_shift(DsIpv6RpfKey_t);
    }
    tcam_lkp_rst_ctl2.ip_sa_index_base1 = _sys_greatebelt_ftm_get_tcam_key_index_base(DsIpv6RpfKey_t);
    tcam_lkp_rst_ctl2.ip_sa_table_base1 = _sys_greatebelt_ftm_get_ad_table_base(DsIpv6UcastDaTcam_t, TRUE);

    /* IPv6 Mc RPF AD Base */
     /* ?? here will refer code, waitting ... ...???*/
     /* tcam_lkp_rst_ctl3.ds_ipv6_mcast_rpf_table_base = _sys_greatebelt_ftm_get_ad_table_base(DsIpv6McastRpf_t, TRUE);*/

    /* IPv6 NAT */
    tcam_lkp_rst_ctl2.ip_sa_index_shift3 = _sys_greatebelt_ftm_get_tcam_key_index_shift(DsIpv6NatKey_t);
    tcam_lkp_rst_ctl2.ip_sa_index_base3 = _sys_greatebelt_ftm_get_tcam_key_index_base(DsIpv6NatKey_t);
    tcam_lkp_rst_ctl2.ip_sa_table_base3 = _sys_greatebelt_ftm_get_ad_table_base(DsIpv6SaNatTcam_t, TRUE);

    /* IPv6 PBR */
    tcam_lkp_rst_ctl2.ip_sa_index_shift5 = _sys_greatebelt_ftm_get_tcam_key_index_shift(DsIpv6PbrKey_t);
    tcam_lkp_rst_ctl2.ip_sa_index_base5 = _sys_greatebelt_ftm_get_tcam_key_index_base(DsIpv6PbrKey_t);
    tcam_lkp_rst_ctl2.ip_sa_table_base5 = _sys_greatebelt_ftm_get_ad_table_base(DsIpv6UcastPbrDualDaTcam_t, TRUE);

    /* MacDa */
    tcam_lkp_rst_ctl2.mac_da_index_shift = _sys_greatebelt_ftm_get_tcam_key_index_shift(DsMacBridgeKey_t);
    tcam_lkp_rst_ctl2.mac_da_index_base = _sys_greatebelt_ftm_get_tcam_key_index_base(DsMacBridgeKey_t);
    tcam_lkp_rst_ctl2.mac_da_table_base = _sys_greatebelt_ftm_get_ad_table_base(DsMacTcam_t, TRUE);

    /* MacSa */
    tcam_lkp_rst_ctl2.mac_sa_index_shift = _sys_greatebelt_ftm_get_tcam_key_index_shift(DsMacBridgeKey_t);
    tcam_lkp_rst_ctl2.mac_sa_index_base = _sys_greatebelt_ftm_get_tcam_key_index_base(DsMacBridgeKey_t);
    tcam_lkp_rst_ctl2.mac_sa_table_base = _sys_greatebelt_ftm_get_ad_table_base(DsMacTcam_t, TRUE);

    /* MacIpv4  share with Ipv4 Mc*/
    tcam_lkp_rst_ctl3.mac_ip_index_shift1 = _sys_greatebelt_ftm_get_tcam_key_index_shift(DsIpv4McastRouteKey_t);
    tcam_lkp_rst_ctl3.mac_ip_index_base1 = _sys_greatebelt_ftm_get_tcam_key_index_base(DsIpv4McastRouteKey_t);
    tcam_lkp_rst_ctl3.mac_ip_table_base1 = _sys_greatebelt_ftm_get_ad_table_base(DsMacIpv4Tcam_t, TRUE);

    /* MacIpv6 */
    if (sys_greatbelt_ftm_get_ip_tcam_share_mode(lchip))
    {
        tcam_lkp_rst_ctl3.mac_ip_index_shift0 = _sys_greatebelt_ftm_get_tcam_key_index_shift(DsMacIpv6Key_t)/2;
    }
    else
    {
        tcam_lkp_rst_ctl3.mac_ip_index_shift0 = _sys_greatebelt_ftm_get_tcam_key_index_shift(DsMacIpv6Key_t);
    }
    tcam_lkp_rst_ctl3.mac_ip_index_base0 = _sys_greatebelt_ftm_get_tcam_key_index_base(DsMacIpv6Key_t);
    tcam_lkp_rst_ctl3.mac_ip_table_base0 = _sys_greatebelt_ftm_get_ad_table_base(DsMacIpv6Tcam_t, TRUE);

    /*********** FCoE and Trill ***********/

    /* FCoE Da */
    tcam_lkp_rst_ctl3.fcoe_index_shift0 = _sys_greatebelt_ftm_get_tcam_key_index_shift(DsFcoeRouteKey_t);
    tcam_lkp_rst_ctl3.fcoe_index_base0 = _sys_greatebelt_ftm_get_tcam_key_index_base(DsFcoeRouteKey_t);
    tcam_lkp_rst_ctl3.fcoe_table_base0 = _sys_greatebelt_ftm_get_ad_table_base(DsFcoeDaTcam_t, TRUE);

    /* FCoE Sa */
    tcam_lkp_rst_ctl3.fcoe_index_shift1 = _sys_greatebelt_ftm_get_tcam_key_index_shift(DsFcoeRpfKey_t);
    tcam_lkp_rst_ctl3.fcoe_index_base1 = _sys_greatebelt_ftm_get_tcam_key_index_base(DsFcoeRpfKey_t);
    tcam_lkp_rst_ctl3.fcoe_table_base1 = _sys_greatebelt_ftm_get_ad_table_base(DsFcoeSaTcam_t, TRUE);

    /* Trill Da Uc */
    tcam_lkp_rst_ctl3.trill_index_shift0 = _sys_greatebelt_ftm_get_tcam_key_index_shift(DsTrillUcastRouteKey_t);
    tcam_lkp_rst_ctl3.trill_index_base0 = _sys_greatebelt_ftm_get_tcam_key_index_base(DsTrillUcastRouteKey_t);
    tcam_lkp_rst_ctl3.trill_table_base0 = _sys_greatebelt_ftm_get_ad_table_base(DsTrillDaUcastTcam_t, TRUE);

    /* Trill Da Mc */
    tcam_lkp_rst_ctl3.trill_index_shift1 = _sys_greatebelt_ftm_get_tcam_key_index_shift(DsTrillMcastRouteKey_t);
    tcam_lkp_rst_ctl3.trill_index_base1 = _sys_greatebelt_ftm_get_tcam_key_index_base(DsTrillMcastRouteKey_t);
    tcam_lkp_rst_ctl3.trill_table_base1 = _sys_greatebelt_ftm_get_ad_table_base(DsTrillDaMcastTcam_t, TRUE);

    cmd = DRV_IOW(TcamEngineLookupResultCtl0_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tcam_lkp_rst_ctl0));

    cmd = DRV_IOW(TcamEngineLookupResultCtl1_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tcam_lkp_rst_ctl1));

    cmd = DRV_IOW(TcamEngineLookupResultCtl2_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tcam_lkp_rst_ctl2));

    cmd = DRV_IOW(TcamEngineLookupResultCtl3_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tcam_lkp_rst_ctl3));

    return CTC_E_NONE;
}


int32
_sys_greatebelt_ftm_init_lkp_register(uint8 lchip)
{
    CTC_ERROR_RETURN(_sys_greatebelt_ftm_init_tcam_userid_ctl_register(lchip));
    CTC_ERROR_RETURN(_sys_greatebelt_ftm_init_tcam_lookup_ctl_register(lchip));
    CTC_ERROR_RETURN(_sys_greatebelt_ftm_init_tcam_lookupresult_ctl_register(lchip));

    return CTC_E_NONE;
}


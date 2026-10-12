/**
 @file sys_greatbelt_pdu.c

 @date 2009-12-30

 @version v2.0

---file comments----
*/

#include "sal.h"
#include "ctc_error.h"
#include "ctc_vector.h"
#include "sys_greatbelt_pdu.h"
#include "sys_greatbelt_chip.h"
#include "sys_greatbelt_port.h"

#include "greatbelt/include/drv_io.h"

/***************************************************************
 *
 *  Defines and Macros
 *
 ***************************************************************/
#define MAX_SYS_PDU_L2PDU_BASED_L2HDR_PTL_ENTRY 16
#define MAX_SYS_PDU_L2PDU_BASED_MACDA_LOW24_ENTRY 9 /*last entry reserved for pause packet identify*/
#define MAX_SYS_PDU_L2PDU_BASED_MACDA_ENTRY 4
#define MAX_SYS_PDU_L2PDU_BASED_L3TYPE_ENTRY 15
#define MAX_SYS_PDU_L2PDU_BASED_BPDU_ENTRY 1

#define MAX_SYS_PDU_L3PDU_BASED_L3HDR_PROTO 8
#define MAX_SYS_PDU_L3PDU_BASED_PORT 8

#define IS_MAC_ALL_ZERO(mac)      (!mac[0] && !mac[1] && !mac[2] && !mac[3] && !mac[4] && !mac[5])
#define IS_MAC_LOW24BIT_ZERO(mac) (!mac[3] && !mac[4] && !mac[5])


/***************************************************************
*
*  Functions
*
***************************************************************/
STATIC int32
_sys_greatbelt_l2pdu_classify_l2pdu_by_l2hdr_proto(uint8 lchip, uint8 index, ctc_pdu_l2pdu_key_t* entry)
{
    uint32 cmd = 0;
    ipe_bpdu_protocol_escape_cam3_t cam3;

    SYS_PDU_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PDU_DBG_PARAM("index:%d\n", index);
    SYS_PDU_DBG_PARAM("ethertype:0x%x\n", entry->l2hdr_proto);

    sal_memset(&cam3, 0, sizeof(ipe_bpdu_protocol_escape_cam3_t));

    if (index >= MAX_SYS_PDU_L2PDU_BASED_L2HDR_PTL_ENTRY)
    {
        return CTC_E_PDU_INVALID_INDEX;
    }

    cam3.cam3_ether_type = entry->l2hdr_proto;

    cmd = DRV_IOW(IpeBpduProtocolEscapeCam3_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &cam3));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l2pdu_get_classified_key_by_l2hdr_proto(uint8 lchip, uint8 index, ctc_pdu_l2pdu_key_t* entry)
{
    uint32 cmd = 0;
    ipe_bpdu_protocol_escape_cam3_t cam3;

    SYS_PDU_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_PDU_DBG_PARAM("index:%d\n", index);

    if (index >= MAX_SYS_PDU_L2PDU_BASED_L2HDR_PTL_ENTRY)
    {
        return CTC_E_PDU_INVALID_INDEX;
    }

    cmd = DRV_IOR(IpeBpduProtocolEscapeCam3_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &cam3));
    entry->l2hdr_proto = cam3.cam3_ether_type;

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l2pdu_classify_l2pdu_by_macda(uint8 lchip, uint8 index, ctc_pdu_l2pdu_key_t* entry)
{
    uint32 cmd = 0;
    int8   index_step = IpeBpduProtocolEscapeCam_MacDaValue1High_f
        - IpeBpduProtocolEscapeCam_MacDaValue0High_f;

    uint32 mac_da_value_bit31_to0_field_id = IpeBpduProtocolEscapeCam_MacDaValue0Low_f;
    uint32 mac_da_value_bit47_to32_field_id = IpeBpduProtocolEscapeCam_MacDaValue0High_f;

    uint32 mac_da_value_bit31_to0 = 0;
    uint32 mac_da_value_bit47_to32 = 0;

    SYS_PDU_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_PDU_DBG_PARAM("index:%d\n", index);
    SYS_PDU_DBG_PARAM("macda:%x %x %x %x %x %x\n", entry->l2pdu_by_mac.mac[0], entry->l2pdu_by_mac.mac[1], \
                      entry->l2pdu_by_mac.mac[2], entry->l2pdu_by_mac.mac[3], entry->l2pdu_by_mac.mac[4], entry->l2pdu_by_mac.mac[5]);

    if (index >= MAX_SYS_PDU_L2PDU_BASED_MACDA_ENTRY)
    {
        return CTC_E_PDU_INVALID_INDEX;
    }

    mac_da_value_bit31_to0_field_id += index_step * index;
    mac_da_value_bit47_to32_field_id += index_step * index;

    mac_da_value_bit31_to0 = entry->l2pdu_by_mac.mac[2] << 24
        | entry->l2pdu_by_mac.mac[3] << 16
        | entry->l2pdu_by_mac.mac[4] << 8
        | entry->l2pdu_by_mac.mac[5];

    mac_da_value_bit47_to32 = entry->l2pdu_by_mac.mac[0] << 8
        | entry->l2pdu_by_mac.mac[1];

    cmd = DRV_IOW(IpeBpduProtocolEscapeCam_t, mac_da_value_bit31_to0_field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &mac_da_value_bit31_to0));

    cmd = DRV_IOW(IpeBpduProtocolEscapeCam_t, mac_da_value_bit47_to32_field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &mac_da_value_bit47_to32));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l2pdu_get_classified_key_by_macda(uint8 lchip, uint8 index, ctc_pdu_l2pdu_key_t* entry)
{
    uint32 cmd = 0;
    int8   index_step = IpeBpduProtocolEscapeCam_MacDaValue1High_f
        - IpeBpduProtocolEscapeCam_MacDaValue0High_f;

    uint32 mac_da_value_bit31_to0 = 0;
    uint32 mac_da_value_bit47_to32 = 0;
    uint32 mac_da_value_bit31_to0_field_id = IpeBpduProtocolEscapeCam_MacDaValue0Low_f;
    uint32 mac_da_value_bit47_to32_field_id = IpeBpduProtocolEscapeCam_MacDaValue0High_f;

    SYS_PDU_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_PDU_DBG_PARAM("index:%d\n", index);

    if (index >= MAX_SYS_PDU_L2PDU_BASED_MACDA_ENTRY)
    {
        return CTC_E_PDU_INVALID_INDEX;
    }

    mac_da_value_bit31_to0_field_id += index_step * index;
    mac_da_value_bit47_to32_field_id += index_step * index;

    cmd = DRV_IOR(IpeBpduProtocolEscapeCam_t, mac_da_value_bit31_to0_field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &mac_da_value_bit31_to0));

    cmd = DRV_IOR(IpeBpduProtocolEscapeCam_t, mac_da_value_bit47_to32_field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &mac_da_value_bit47_to32));

    entry->l2pdu_by_mac.mac[2] = (mac_da_value_bit31_to0 >> 24) & 0xff;
    entry->l2pdu_by_mac.mac[3] = (mac_da_value_bit31_to0 >> 16) & 0xff;
    entry->l2pdu_by_mac.mac[4] = (mac_da_value_bit31_to0 >> 8) & 0xff;
    entry->l2pdu_by_mac.mac[5] = (mac_da_value_bit31_to0) & 0xff;

    entry->l2pdu_by_mac.mac[0] = (mac_da_value_bit47_to32 >> 8) & 0xff;
    entry->l2pdu_by_mac.mac[1] = (mac_da_value_bit47_to32) & 0xff;

    entry->l2pdu_by_mac.mac_mask[2] = 0xff;
    entry->l2pdu_by_mac.mac_mask[3] = 0xff;
    entry->l2pdu_by_mac.mac_mask[4] = 0xff;
    entry->l2pdu_by_mac.mac_mask[5] = 0xff;

    entry->l2pdu_by_mac.mac_mask[0] = 0xff;
    entry->l2pdu_by_mac.mac_mask[1] = 0xff;

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l2pdu_classify_l2pdu_by_macda_low24(uint8 lchip, uint8 index, ctc_pdu_l2pdu_key_t* entry)
{
    uint32 cmd = 0;
    int8   index_step = IpeBpduProtocolEscapeCam2_MacDaMask1_f
        - IpeBpduProtocolEscapeCam2_MacDaMask0_f;
    uint32 cam2_mac_da_mask = 0;
    uint32 cam2_mac_da_value = 0;
    uint32 cam4_mac_da_value = 0;
    uint32 cam2_mac_da_mask_field_id = IpeBpduProtocolEscapeCam2_MacDaMask0_f;
    uint32 cam2_mac_da_value_field_id = IpeBpduProtocolEscapeCam2_MacDaValue0_f;
    uint32 cam4_mac_da_value_field_id = IpeBpduProtocolEscapeCam4_MacDa0_f;

    SYS_PDU_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_PDU_DBG_PARAM("index:%d\n", index);
    SYS_PDU_DBG_PARAM("macda:%x %x %x %x %x %x\n", entry->l2pdu_by_mac.mac[0], entry->l2pdu_by_mac.mac[1], \
                      entry->l2pdu_by_mac.mac[2], entry->l2pdu_by_mac.mac[3], entry->l2pdu_by_mac.mac[4], entry->l2pdu_by_mac.mac[5]);

    if (index >= MAX_SYS_PDU_L2PDU_BASED_MACDA_LOW24_ENTRY)
    {
        return CTC_E_PDU_INVALID_INDEX;
    }

    cam2_mac_da_mask_field_id += index_step * index;
    cam2_mac_da_value_field_id += index_step * index;
    cam4_mac_da_value_field_id += index_step * (index - 2);

    if ((entry->l2pdu_by_mac.mac[0] != 0x01) || (entry->l2pdu_by_mac.mac[1] != 0x80)
        || (entry->l2pdu_by_mac.mac[2] != 0xc2))
    {
        return CTC_E_PDU_INVALID_MACDA;
    }

    if (index < 2)
    {
        if ((entry->l2pdu_by_mac.mac_mask[0] != 0xff) || (entry->l2pdu_by_mac.mac_mask[1] != 0xff)
            || (entry->l2pdu_by_mac.mac_mask[2] != 0xff))
        {
            if ((entry->l2pdu_by_mac.mac_mask[0] == 0) && (entry->l2pdu_by_mac.mac_mask[1] == 0)
                && (entry->l2pdu_by_mac.mac_mask[2] == 0) && (entry->l2pdu_by_mac.mac_mask[3] == 0)
                && (entry->l2pdu_by_mac.mac_mask[4] == 0) && (entry->l2pdu_by_mac.mac_mask[5] == 0))
            {
                cam2_mac_da_mask = 0xffffff;
            }
            else
            {
                return CTC_E_PDU_INVALID_MACDA_MASK;
            }
        }

        if (0xffffff != cam2_mac_da_mask)
        {
            cam2_mac_da_mask = entry->l2pdu_by_mac.mac_mask[3] << 16
                             | entry->l2pdu_by_mac.mac_mask[4] << 8 | entry->l2pdu_by_mac.mac_mask[5];
            cam2_mac_da_mask &= 0xffffff;
        }

        cam2_mac_da_value =  entry->l2pdu_by_mac.mac[3] << 16
            | entry->l2pdu_by_mac.mac[4] << 8 | entry->l2pdu_by_mac.mac[5];
        cam2_mac_da_value &= 0xffffff;
    }
    else
    {
        cam4_mac_da_value =  entry->l2pdu_by_mac.mac[3] << 16
            | entry->l2pdu_by_mac.mac[4] << 8 | entry->l2pdu_by_mac.mac[5];
        cam4_mac_da_value &= 0xffffff;
    }

    if (index < 2)
    {
        cmd = DRV_IOW(IpeBpduProtocolEscapeCam2_t, cam2_mac_da_mask_field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &cam2_mac_da_mask));

        cmd = DRV_IOW(IpeBpduProtocolEscapeCam2_t, cam2_mac_da_value_field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &cam2_mac_da_value));
    }
    else
    {
        cmd = DRV_IOW(IpeBpduProtocolEscapeCam4_t, cam4_mac_da_value_field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &cam4_mac_da_value));
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l2pdu_get_classified_key_by_macda_low24(uint8 lchip, uint8 index, ctc_pdu_l2pdu_key_t* entry)
{
    uint32 cmd = 0;
    int8   index_step = IpeBpduProtocolEscapeCam2_MacDaMask1_f
        - IpeBpduProtocolEscapeCam2_MacDaMask0_f;
    uint32 cam2_mac_da_mask = 0;
    uint32 cam2_mac_da_value = 0;
    uint32 cam4_mac_da_value = 0;
    uint32 cam2_mac_da_mask_field_id = IpeBpduProtocolEscapeCam2_MacDaMask0_f;
    uint32 cam2_mac_da_value_field_id = IpeBpduProtocolEscapeCam2_MacDaValue0_f;
    uint32 cam4_mac_da_value_field_id = IpeBpduProtocolEscapeCam4_MacDa0_f;

    SYS_PDU_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_PDU_DBG_PARAM("index:%d\n", index);

    if (index >= MAX_SYS_PDU_L2PDU_BASED_MACDA_LOW24_ENTRY)
    {
        return CTC_E_PDU_INVALID_INDEX;
    }

    cam2_mac_da_mask_field_id += index_step * index;
    cam2_mac_da_value_field_id += index_step * index;
    cam4_mac_da_value_field_id += index_step * (index - 2);

    entry->l2pdu_by_mac.mac[0] = 0x01;
    entry->l2pdu_by_mac.mac[1] = 0x80;
    entry->l2pdu_by_mac.mac[2] = 0xC2;
    if (index < 2)
    {
        cmd = DRV_IOR(IpeBpduProtocolEscapeCam2_t, cam2_mac_da_mask_field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &cam2_mac_da_mask));

        entry->l2pdu_by_mac.mac_mask[3] = (cam2_mac_da_mask >> 16) & 0xff;
        entry->l2pdu_by_mac.mac_mask[4] = (cam2_mac_da_mask >> 8) & 0xff;
        entry->l2pdu_by_mac.mac_mask[5] = (cam2_mac_da_mask) & 0xff;

        cmd = DRV_IOR(IpeBpduProtocolEscapeCam2_t, cam2_mac_da_value_field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &cam2_mac_da_value));

        entry->l2pdu_by_mac.mac[3] = (cam2_mac_da_value >> 16) & 0xff;
        entry->l2pdu_by_mac.mac[4] = (cam2_mac_da_value >> 8) & 0xff;
        entry->l2pdu_by_mac.mac[5] = (cam2_mac_da_value) & 0xff;
    }
    else
    {
        cmd = DRV_IOR(IpeBpduProtocolEscapeCam4_t, cam4_mac_da_value_field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &cam4_mac_da_value));

        entry->l2pdu_by_mac.mac[3] = (cam4_mac_da_value >> 16) & 0xff;
        entry->l2pdu_by_mac.mac[4] = (cam4_mac_da_value >> 8) & 0xff;
        entry->l2pdu_by_mac.mac[5] = (cam4_mac_da_value) & 0xff;

        entry->l2pdu_by_mac.mac_mask[3] = 0xff;
        entry->l2pdu_by_mac.mac_mask[4] = 0xff;
        entry->l2pdu_by_mac.mac_mask[5] = 0xff;
    }

    return CTC_E_NONE;
}

/**
 @brief  Classify layer2 pdu based on macda,macda-low24 bit, layer2 header protocol
*/
int32
sys_greatbelt_l2pdu_classify_l2pdu(uint8 lchip, ctc_pdu_l2pdu_type_t l2pdu_type, uint8 index,
                                   ctc_pdu_l2pdu_key_t* key)
{
    SYS_LCHIP_CHECK_ACTIVE(lchip);
    CTC_PTR_VALID_CHECK(key);

    SYS_PDU_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_PDU_DBG_PARAM("l2pdu type:%d\n", l2pdu_type);
    SYS_PDU_DBG_PARAM("index:%d\n", index);

    switch (l2pdu_type)
    {
    case CTC_PDU_L2PDU_TYPE_L2HDR_PROTO:
        CTC_ERROR_RETURN(_sys_greatbelt_l2pdu_classify_l2pdu_by_l2hdr_proto(lchip, index, key));
        break;

    case CTC_PDU_L2PDU_TYPE_MACDA:
        CTC_ERROR_RETURN(_sys_greatbelt_l2pdu_classify_l2pdu_by_macda(lchip, index, key));
        break;

    case CTC_PDU_L2PDU_TYPE_MACDA_LOW24:
        CTC_ERROR_RETURN(_sys_greatbelt_l2pdu_classify_l2pdu_by_macda_low24(lchip, index, key));
        break;

    default:
        break;
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_l2pdu_get_classified_key(uint8 lchip, ctc_pdu_l2pdu_type_t l2pdu_type, uint8 index,
                                       ctc_pdu_l2pdu_key_t* key)
{
    SYS_LCHIP_CHECK_ACTIVE(lchip);
    CTC_PTR_VALID_CHECK(key);

    SYS_PDU_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_PDU_DBG_PARAM("l2pdu type:%d\n", l2pdu_type);
    SYS_PDU_DBG_PARAM("index:%d\n", index);

    switch (l2pdu_type)
    {
    case CTC_PDU_L2PDU_TYPE_L2HDR_PROTO:
        CTC_ERROR_RETURN(_sys_greatbelt_l2pdu_get_classified_key_by_l2hdr_proto(lchip, index, key));
        break;

    case CTC_PDU_L2PDU_TYPE_MACDA:
        CTC_ERROR_RETURN(_sys_greatbelt_l2pdu_get_classified_key_by_macda(lchip, index, key));
        break;

    case CTC_PDU_L2PDU_TYPE_MACDA_LOW24:
        CTC_ERROR_RETURN(_sys_greatbelt_l2pdu_get_classified_key_by_macda_low24(lchip, index, key));
        break;

    default:
        break;
    }

    return CTC_E_NONE;
}

int32
_sys_greatbelt_l2pdu_set_global_action_by_l3type(uint8 lchip, uint8 index, ctc_pdu_global_l2pdu_action_t* ctl)
{
    uint32 cmd = 0;
    ipe_bridge_ctl_t bridge_ctl;
    uint32 protocol_exception_sub = 0;
    uint32 protocol_exception_sub_field_id = IpeBridgeCtl_ProtocolExceptionSubIndex0_f + index;

    SYS_PDU_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_PDU_DBG_PARAM("index:%d\n", index);
    SYS_PDU_DBG_PARAM("action index:%d\n", ctl->action_index);
    SYS_PDU_DBG_PARAM("copy to cpu:%d\n", ctl->copy_to_cpu);

    if (index >= MAX_CTC_PARSER_L3_TYPE)
    {
        return CTC_E_PDU_INVALID_INDEX;
    }

    protocol_exception_sub = ctl->action_index;

    sal_memset(&bridge_ctl, 0, sizeof(ipe_bridge_ctl_t));

    cmd = DRV_IOR(IpeBridgeCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &bridge_ctl));

    if (ctl->copy_to_cpu)
    {
        bridge_ctl.protocol_exception |= (1 << index);
    }
    else
    {
        bridge_ctl.protocol_exception &= (~(1 << index));
    }

    cmd = DRV_IOW(IpeBridgeCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &bridge_ctl));

    cmd = DRV_IOW(IpeBridgeCtl_t, protocol_exception_sub_field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &protocol_exception_sub));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l2pdu_get_global_action_by_l3type(uint8 lchip, uint8 index, ctc_pdu_global_l2pdu_action_t* ctl)
{
    uint32 cmd = 0;
    uint32 protocol_exception_sub = 0;
    uint32 protocol_exception_sub_field_id = IpeBridgeCtl_ProtocolExceptionSubIndex0_f + index;
    ipe_bridge_ctl_t bridge_ctl;

    SYS_PDU_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_PDU_DBG_PARAM("index:%d\n", index);

    if (index >= MAX_CTC_PARSER_L3_TYPE)
    {
        return CTC_E_PDU_INVALID_INDEX;
    }

    sal_memset(&bridge_ctl, 0, sizeof(ipe_bridge_ctl_t));

    cmd = DRV_IOR(IpeBridgeCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &bridge_ctl));

    ctl->copy_to_cpu = (bridge_ctl.protocol_exception & (1 << index)) ? 1 : 0;

    cmd = DRV_IOR(IpeBridgeCtl_t, protocol_exception_sub_field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &protocol_exception_sub));

    ctl->action_index = protocol_exception_sub;
    if (ctl->action_index)
    {
        ctl->entry_valid = 1;
    }

    SYS_PDU_DBG_INFO("action index:%d\n", ctl->action_index);
    SYS_PDU_DBG_INFO("copy to cpu:%d\n", ctl->copy_to_cpu);

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l2pdu_set_global_action_of_fixed_l2pdu(uint8 lchip, ctc_pdu_l2pdu_type_t l2pdu_type,
                                                      ctc_pdu_global_l2pdu_action_t* ctl)
{
    uint32 cmd = 0;
    ipe_bpdu_escape_ctl_t escap_ctl;

    SYS_PDU_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_PDU_DBG_PARAM("action index:%d\n", ctl->action_index);
    SYS_PDU_DBG_PARAM("copy to cpu:%d\n", ctl->copy_to_cpu);

    sal_memset(&escap_ctl, 0, sizeof(ipe_bpdu_escape_ctl_t));

    cmd = DRV_IOR(IpeBpduEscapeCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &escap_ctl));

    switch (l2pdu_type)
    {
    case CTC_PDU_L2PDU_TYPE_BPDU:
        escap_ctl.bpdu_exception_en = ctl->entry_valid ? 1 : 0;
        escap_ctl.bpdu_exception_sub_index = ctl->action_index;
        break;

    case CTC_PDU_L2PDU_TYPE_SLOW_PROTO:
        escap_ctl.slow_protocol_exception_en = ctl->entry_valid ? 1 : 0;
        escap_ctl.slow_protocol_exception_sub_index = ctl->action_index;
        break;

    case CTC_PDU_L2PDU_TYPE_EAPOL:
        escap_ctl.eapol_exception_en = ctl->entry_valid ? 1 : 0;
        escap_ctl.eapol_exception_sub_index = ctl->action_index;
        break;

    case CTC_PDU_L2PDU_TYPE_LLDP:
        escap_ctl.lldp_exception_en = ctl->entry_valid ? 1 : 0;
        escap_ctl.lldp_exception_sub_index = ctl->action_index;
        break;

    case CTC_PDU_L2PDU_TYPE_ISIS:
        escap_ctl.l2isis_exception_en = ctl->entry_valid ? 1 : 0;
        escap_ctl.l2isis_exception_sub_index = ctl->action_index;
        break;

    default:
        break;
    }

    cmd = DRV_IOW(IpeBpduEscapeCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &escap_ctl));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l2pdu_get_global_action_of_fixed_l2pdu(uint8 lchip, ctc_pdu_l2pdu_type_t l2pdu_type,
                                                      ctc_pdu_global_l2pdu_action_t* ctl)
{
    uint32 cmd = 0;
    ipe_bpdu_escape_ctl_t escap_ctl;

    SYS_PDU_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    sal_memset(&escap_ctl, 0, sizeof(ipe_bpdu_escape_ctl_t));
    cmd = DRV_IOR(IpeBpduEscapeCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &escap_ctl));

    switch (l2pdu_type)
    {
    case CTC_PDU_L2PDU_TYPE_BPDU:
        ctl->entry_valid = escap_ctl.bpdu_exception_en;
        ctl->action_index = escap_ctl.bpdu_exception_sub_index;
        break;

    case CTC_PDU_L2PDU_TYPE_SLOW_PROTO:
        ctl->entry_valid = escap_ctl.slow_protocol_exception_en;
        ctl->action_index = escap_ctl.slow_protocol_exception_sub_index;
        break;

    case CTC_PDU_L2PDU_TYPE_EAPOL:
        ctl->entry_valid = escap_ctl.eapol_exception_en;
        ctl->action_index = escap_ctl.eapol_exception_sub_index;
        break;

    case CTC_PDU_L2PDU_TYPE_LLDP:
        ctl->entry_valid = escap_ctl.lldp_exception_en;
        ctl->action_index = escap_ctl.lldp_exception_sub_index;
        break;

    case CTC_PDU_L2PDU_TYPE_ISIS:
        ctl->entry_valid = escap_ctl.l2isis_exception_en;
        ctl->action_index = escap_ctl.l2isis_exception_sub_index;
        break;

    default:
        break;
    }

    SYS_PDU_DBG_INFO("action index:%d\n", ctl->action_index);
    SYS_PDU_DBG_INFO("copy to cpu:%d\n", ctl->entry_valid);

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l2pdu_set_global_action_by_l2hdr_proto(uint8 lchip, uint8 index, ctc_pdu_global_l2pdu_action_t* ctl)
{
    uint32 cmd = 0;
    ipe_bpdu_protocol_escape_cam_result3_t cam_result3;

    SYS_PDU_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_PDU_DBG_PARAM("index:%d\n", index);
    SYS_PDU_DBG_PARAM("entry valid:%d\n", ctl->entry_valid);
    SYS_PDU_DBG_PARAM("action index:%d\n", ctl->action_index);

    if (index >= MAX_SYS_PDU_L2PDU_BASED_L2HDR_PTL_ENTRY)
    {
        return CTC_E_PDU_INVALID_INDEX;
    }

    cam_result3.cam3_entry_valid = ctl->entry_valid;
    cam_result3.cam3_exception_sub_index = ctl->action_index;

    cmd = DRV_IOW(IpeBpduProtocolEscapeCamResult3_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &cam_result3));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l2pdu_get_global_action_by_l2hdr_proto(uint8 lchip, uint8 index, ctc_pdu_global_l2pdu_action_t* ctl)
{
    uint32 cmd = 0;
    ipe_bpdu_protocol_escape_cam_result3_t cam_result3;

    SYS_PDU_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_PDU_DBG_PARAM("index:%d\n", index);

    if (index >= MAX_SYS_PDU_L2PDU_BASED_L2HDR_PTL_ENTRY)
    {
        return CTC_E_PDU_INVALID_INDEX;
    }

    cmd = DRV_IOR(IpeBpduProtocolEscapeCamResult3_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &cam_result3));

    ctl->entry_valid = cam_result3.cam3_entry_valid;
    ctl->action_index = cam_result3.cam3_exception_sub_index;

    SYS_PDU_DBG_INFO("entry valid:%d\n", ctl->entry_valid);
    SYS_PDU_DBG_INFO("action index:%d\n", ctl->action_index);

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l2pdu_set_global_action_by_macda(uint8 lchip, uint8 index, ctc_pdu_global_l2pdu_action_t* ctl)
{
    uint32 cmd = 0;
    int8   index_step = IpeBpduProtocolEscapeCamResult_Cam1EntryValid1_f
        - IpeBpduProtocolEscapeCamResult_Cam1EntryValid0_f;

    uint32 entry_valid = 0;
    uint32 exception_sub_index = 0;
    uint32 entry_valid_field_id = IpeBpduProtocolEscapeCamResult_Cam1EntryValid0_f;
    uint32 exception_sub_index_field_id = IpeBpduProtocolEscapeCamResult_Cam1ExceptionSubIndex0_f;

    SYS_PDU_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_PDU_DBG_PARAM("index:%d\n", index);
    SYS_PDU_DBG_PARAM("entry valid:%d\n", ctl->entry_valid);
    SYS_PDU_DBG_PARAM("action index:%d\n", ctl->action_index);

    if (index >= MAX_SYS_PDU_L2PDU_BASED_MACDA_ENTRY)
    {
        return CTC_E_PDU_INVALID_INDEX;
    }

    entry_valid_field_id += index_step * index;
    exception_sub_index_field_id += index_step * index;

    entry_valid = ctl->entry_valid ? 1 : 0;
    exception_sub_index = ctl->action_index;

    cmd = DRV_IOW(IpeBpduProtocolEscapeCamResult_t, entry_valid_field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &entry_valid));

    cmd = DRV_IOW(IpeBpduProtocolEscapeCamResult_t, exception_sub_index_field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &exception_sub_index));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l2pdu_get_global_action_by_macda(uint8 lchip, uint8 index, ctc_pdu_global_l2pdu_action_t* ctl)
{
    uint32 cmd = 0;
    int8   index_step = IpeBpduProtocolEscapeCamResult_Cam1EntryValid1_f
        - IpeBpduProtocolEscapeCamResult_Cam1EntryValid0_f;
    uint32 entry_valid = 0;
    uint32 exception_sub_index = 0;
    uint32 entry_valid_field_id = IpeBpduProtocolEscapeCamResult_Cam1EntryValid0_f;
    uint32 exception_sub_index_field_id = IpeBpduProtocolEscapeCamResult_Cam1ExceptionSubIndex0_f;

    SYS_PDU_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_PDU_DBG_PARAM("index:%d\n", index);

    if (index >= MAX_SYS_PDU_L2PDU_BASED_MACDA_ENTRY)
    {
        return CTC_E_PDU_INVALID_INDEX;
    }

    entry_valid_field_id += index_step * index;
    exception_sub_index_field_id += index_step * index;

    cmd = DRV_IOR(IpeBpduProtocolEscapeCamResult_t, entry_valid_field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &entry_valid));

    cmd = DRV_IOR(IpeBpduProtocolEscapeCamResult_t, exception_sub_index_field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &exception_sub_index));

    ctl->entry_valid = entry_valid;
    ctl->action_index = exception_sub_index;

    SYS_PDU_DBG_INFO("entry valid:%d\n", ctl->entry_valid);
    SYS_PDU_DBG_INFO("action index:%d\n", ctl->action_index);

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l2pdu_set_global_action_by_macda_low24(uint8 lchip, uint8 index, ctc_pdu_global_l2pdu_action_t* ctl)
{
    uint32 cmd = 0;
    int8   index_step = IpeBpduProtocolEscapeCamResult2_Cam2ExceptionSubIndex1_f
        - IpeBpduProtocolEscapeCamResult2_Cam2ExceptionSubIndex0_f;
    uint32 entry_valid = 0;
    uint32 exception_sub_index = 0;
    uint32 cam2_entry_valid_field_id = IpeBpduProtocolEscapeCamResult2_Cam2EntryValid0_f;
    uint32 cam2_exception_sub_index_field_id = IpeBpduProtocolEscapeCamResult2_Cam2ExceptionSubIndex0_f;

    ipe_bpdu_protocol_escape_cam_result4_t cam_result4;

    SYS_PDU_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_PDU_DBG_PARAM("index:%d\n", index);
    SYS_PDU_DBG_PARAM("entry valid:%d\n", ctl->entry_valid);
    SYS_PDU_DBG_PARAM("action index:%d\n", ctl->action_index);

    if (index >= MAX_SYS_PDU_L2PDU_BASED_MACDA_LOW24_ENTRY)
    {
        return CTC_E_PDU_INVALID_INDEX;
    }

    entry_valid = ctl->entry_valid ? 1 : 0;
    exception_sub_index = ctl->action_index;

    cam2_entry_valid_field_id += index_step * index;
    cam2_exception_sub_index_field_id += index_step * index;

    cam_result4.cam4_entry_valid = ctl->entry_valid ? 1 : 0;
    cam_result4.cam4_exception_sub_index = ctl->action_index;

    if (index < 2)
    {
        cmd = DRV_IOW(IpeBpduProtocolEscapeCamResult2_t, cam2_entry_valid_field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &entry_valid));

        cmd = DRV_IOW(IpeBpduProtocolEscapeCamResult2_t, cam2_exception_sub_index_field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &exception_sub_index));
    }
    else
    {
        cmd = DRV_IOW(IpeBpduProtocolEscapeCamResult4_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, (index-2), cmd, &cam_result4));
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l2pdu_get_global_action_by_macda_low24(uint8 lchip, uint8 index, ctc_pdu_global_l2pdu_action_t* ctl)
{
    uint32 cmd = 0;
    int8   index_step = IpeBpduProtocolEscapeCamResult2_Cam2ExceptionSubIndex1_f
        - IpeBpduProtocolEscapeCamResult2_Cam2ExceptionSubIndex0_f;
    uint32 entry_valid = 0;
    uint32 exception_sub_index = 0;
    uint32 entry_valid_field_id = IpeBpduProtocolEscapeCamResult2_Cam2EntryValid0_f;
    uint32 exception_sub_index_field_id = IpeBpduProtocolEscapeCamResult2_Cam2ExceptionSubIndex0_f;
    ipe_bpdu_protocol_escape_cam_result4_t cam_result4;

    SYS_PDU_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_PDU_DBG_PARAM("index:%d\n", index);

    if (index >= MAX_SYS_PDU_L2PDU_BASED_MACDA_LOW24_ENTRY)
    {
        return CTC_E_PDU_INVALID_INDEX;
    }

    entry_valid_field_id += index_step * index;
    exception_sub_index_field_id += index_step * index;

    if (index < 2)
    {
        cmd = DRV_IOR(IpeBpduProtocolEscapeCamResult2_t, entry_valid_field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &entry_valid));

        cmd = DRV_IOR(IpeBpduProtocolEscapeCamResult2_t, exception_sub_index_field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &exception_sub_index));

        ctl->entry_valid = entry_valid;
        ctl->action_index = exception_sub_index;
    }
    else
    {
        cmd = DRV_IOR(IpeBpduProtocolEscapeCamResult4_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, (index - 2), cmd, &cam_result4));

        ctl->entry_valid = cam_result4.cam4_entry_valid;
        ctl->action_index = cam_result4.cam4_exception_sub_index;
    }

    SYS_PDU_DBG_INFO("entry valid:%d\n", ctl->entry_valid);
    SYS_PDU_DBG_INFO("action index:%d\n", ctl->action_index);

    return CTC_E_NONE;
}

/**
 @brief  Set layer2 pdu global property
*/
int32
sys_greatbelt_l2pdu_set_global_action(uint8 lchip, ctc_pdu_l2pdu_type_t l2pdu_type, uint8 index,
                                      ctc_pdu_global_l2pdu_action_t* action)
{
    SYS_LCHIP_CHECK_ACTIVE(lchip);
    CTC_PTR_VALID_CHECK(action);

    SYS_PDU_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_PDU_DBG_PARAM("l2pdu type:%d\n", l2pdu_type);
    SYS_PDU_DBG_PARAM("index:%d\n", index);

    /*layer3 type pdu can not per port control, action index: 32-53*/
    if (CTC_PDU_L2PDU_TYPE_L3TYPE == l2pdu_type)
    {
#if 0
        if (action->action_index > MAX_SYS_L2PDU_USER_DEFINE_ACTION_INDEX ||
            action->action_index < (MAX_SYS_L2PDU_PER_PORT_ACTION_INDEX + 1))
        {
            return CTC_E_PDU_INVALID_ACTION_INDEX;
        }

        /*the index is equal the layer3 type value*/
        CTC_ERROR_RETURN(_sys_greatbelt_l2pdu_set_global_action_by_l3type(lchip, index, action));
#endif
        return CTC_E_FEATURE_NOT_SUPPORT;
    }
    else
    {
        if (action->action_index > MAX_SYS_L2PDU_PER_PORT_ACTION_INDEX)
        {
            return CTC_E_PDU_INVALID_ACTION_INDEX;
        }

        switch (l2pdu_type)
        {
        case CTC_PDU_L2PDU_TYPE_BPDU:
        case CTC_PDU_L2PDU_TYPE_SLOW_PROTO:
        case CTC_PDU_L2PDU_TYPE_EAPOL:
        case CTC_PDU_L2PDU_TYPE_LLDP:
        case CTC_PDU_L2PDU_TYPE_ISIS:
            CTC_ERROR_RETURN(_sys_greatbelt_l2pdu_set_global_action_of_fixed_l2pdu(lchip, l2pdu_type, action));
            break;

        case CTC_PDU_L2PDU_TYPE_L2HDR_PROTO:
            CTC_ERROR_RETURN(_sys_greatbelt_l2pdu_set_global_action_by_l2hdr_proto(lchip, index, action));
            break;

        case CTC_PDU_L2PDU_TYPE_MACDA:
            CTC_ERROR_RETURN(_sys_greatbelt_l2pdu_set_global_action_by_macda(lchip, index, action));
            break;

        case CTC_PDU_L2PDU_TYPE_MACDA_LOW24:
            CTC_ERROR_RETURN(_sys_greatbelt_l2pdu_set_global_action_by_macda_low24(lchip, index, action));
            break;

        default:
            break;
        }
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_l2pdu_get_global_action(uint8 lchip, ctc_pdu_l2pdu_type_t l2pdu_type, uint8 index,
                                      ctc_pdu_global_l2pdu_action_t* action)
{
    SYS_LCHIP_CHECK_ACTIVE(lchip);
    CTC_PTR_VALID_CHECK(action);

    SYS_PDU_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_PDU_DBG_PARAM("l2pdu type:%d\n", l2pdu_type);
    SYS_PDU_DBG_PARAM("index:%d\n", index);

    switch (l2pdu_type)
    {
    case CTC_PDU_L2PDU_TYPE_L2HDR_PROTO:
        CTC_ERROR_RETURN(_sys_greatbelt_l2pdu_get_global_action_by_l2hdr_proto(lchip, index, action));
        break;

    case CTC_PDU_L2PDU_TYPE_MACDA:
        CTC_ERROR_RETURN(_sys_greatbelt_l2pdu_get_global_action_by_macda(lchip, index, action));
        break;

    case CTC_PDU_L2PDU_TYPE_MACDA_LOW24:
        CTC_ERROR_RETURN(_sys_greatbelt_l2pdu_get_global_action_by_macda_low24(lchip, index, action));
        break;

    case CTC_PDU_L2PDU_TYPE_L3TYPE:
        CTC_ERROR_RETURN(_sys_greatbelt_l2pdu_get_global_action_by_l3type(lchip, index, action));
        break;

    case CTC_PDU_L2PDU_TYPE_BPDU:
    case CTC_PDU_L2PDU_TYPE_SLOW_PROTO:
    case CTC_PDU_L2PDU_TYPE_EAPOL:
    case CTC_PDU_L2PDU_TYPE_LLDP:
    case CTC_PDU_L2PDU_TYPE_ISIS:
        CTC_ERROR_RETURN(_sys_greatbelt_l2pdu_get_global_action_of_fixed_l2pdu(lchip, l2pdu_type, action));
        break;

    default:
        break;
    }

    return CTC_E_NONE;
}

/**
 @brief  Per port control layer2 pdu action
*/
int32
sys_greatbelt_l2pdu_set_port_action(uint8 lchip, uint16 gport, uint8 action_index,
                                    ctc_pdu_port_l2pdu_action_t action)
{
    uint32 excp2_en = 0;
    uint32 excp2_discard = 0;

    SYS_LCHIP_CHECK_ACTIVE(lchip);
    SYS_PDU_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_PDU_DBG_PARAM("gport:%x\n", gport);
    SYS_PDU_DBG_PARAM("action index:%d\n", action_index);
    SYS_PDU_DBG_PARAM("action:%d\n", action);

    if (action_index > MAX_SYS_L2PDU_PER_PORT_ACTION_INDEX)
    {
        return CTC_E_PDU_INVALID_ACTION_INDEX;
    }

    switch (action)
    {
    case CTC_PDU_L2PDU_ACTION_TYPE_REDIRECT_TO_CPU:
        /*excption2En set, exception2Discard set*/
        CTC_ERROR_RETURN(sys_greatbelt_port_get_internal_property(lchip, gport, SYS_PORT_PROP_EXCEPTION_EN, &excp2_en));
        CTC_SET_FLAG(excp2_en, (1 << action_index));
        CTC_ERROR_RETURN(sys_greatbelt_port_set_internal_property(lchip, gport, SYS_PORT_PROP_EXCEPTION_EN, excp2_en));

        CTC_ERROR_RETURN(sys_greatbelt_port_get_internal_property(lchip, gport, SYS_PORT_PROP_EXCEPTION_DISCARD, &excp2_discard));
        CTC_SET_FLAG(excp2_discard, (1 << action_index));
        CTC_ERROR_RETURN(sys_greatbelt_port_set_internal_property(lchip, gport, SYS_PORT_PROP_EXCEPTION_DISCARD, excp2_discard));
        break;

    case CTC_PDU_L2PDU_ACTION_TYPE_COPY_TO_CPU:
        /*excption2En set , exception2Discard clear*/
        CTC_ERROR_RETURN(sys_greatbelt_port_get_internal_property(lchip, gport, SYS_PORT_PROP_EXCEPTION_EN, &excp2_en));
        CTC_SET_FLAG(excp2_en, (1 << action_index));
        CTC_ERROR_RETURN(sys_greatbelt_port_set_internal_property(lchip, gport, SYS_PORT_PROP_EXCEPTION_EN, excp2_en));

        CTC_ERROR_RETURN(sys_greatbelt_port_get_internal_property(lchip, gport, SYS_PORT_PROP_EXCEPTION_DISCARD, &excp2_discard));
        CTC_UNSET_FLAG(excp2_discard, (1 << action_index));
        CTC_ERROR_RETURN(sys_greatbelt_port_set_internal_property(lchip, gport, SYS_PORT_PROP_EXCEPTION_DISCARD, excp2_discard));
        break;

    case CTC_PDU_L2PDU_ACTION_TYPE_FWD:
        /*excption2En clear , exception2Discard clear*/
        CTC_ERROR_RETURN(sys_greatbelt_port_get_internal_property(lchip, gport, SYS_PORT_PROP_EXCEPTION_EN, &excp2_en));
        CTC_UNSET_FLAG(excp2_en, (1 << action_index));
        CTC_ERROR_RETURN(sys_greatbelt_port_set_internal_property(lchip, gport, SYS_PORT_PROP_EXCEPTION_EN, excp2_en));

        CTC_ERROR_RETURN(sys_greatbelt_port_get_internal_property(lchip, gport, SYS_PORT_PROP_EXCEPTION_DISCARD, &excp2_discard));
        CTC_UNSET_FLAG(excp2_discard, (1 << action_index));
        CTC_ERROR_RETURN(sys_greatbelt_port_set_internal_property(lchip, gport, SYS_PORT_PROP_EXCEPTION_DISCARD, excp2_discard));
        break;

    case CTC_PDU_L2PDU_ACTION_TYPE_DISCARD:
        /*excption2En clear , exception2Discard set*/
        CTC_ERROR_RETURN(sys_greatbelt_port_get_internal_property(lchip, gport, SYS_PORT_PROP_EXCEPTION_EN, &excp2_en));
        CTC_UNSET_FLAG(excp2_en, (1 << action_index));
        CTC_ERROR_RETURN(sys_greatbelt_port_set_internal_property(lchip, gport, SYS_PORT_PROP_EXCEPTION_EN, excp2_en));

        CTC_ERROR_RETURN(sys_greatbelt_port_get_internal_property(lchip, gport, SYS_PORT_PROP_EXCEPTION_DISCARD, &excp2_discard));
        CTC_SET_FLAG(excp2_discard, (1 << action_index));
        CTC_ERROR_RETURN(sys_greatbelt_port_set_internal_property(lchip, gport, SYS_PORT_PROP_EXCEPTION_DISCARD, excp2_discard));
        break;

    default:
        return CTC_E_INVALID_PARAM;
        break;
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_l2pdu_get_port_action(uint8 lchip, uint16 gport, uint8 action_index,
                                    ctc_pdu_port_l2pdu_action_t* action)
{
    uint32 excp2_en = 0;
    uint32 excp2_discard = 0;

    uint8 excp2_en_flag = 0;
    uint8 excp2_discard_flag = 0;

    SYS_LCHIP_CHECK_ACTIVE(lchip);
    SYS_PDU_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_PDU_DBG_PARAM("gport:%x\n", gport);
    SYS_PDU_DBG_PARAM("action index:%d\n", action_index);

    if (action_index > MAX_SYS_L2PDU_PER_PORT_ACTION_INDEX)
    {
        return CTC_E_PDU_INVALID_ACTION_INDEX;
    }

    /*excption2En set, exception2Discard set*/
    CTC_ERROR_RETURN(sys_greatbelt_port_get_internal_property(lchip, gport, SYS_PORT_PROP_EXCEPTION_EN, &excp2_en));
    if (CTC_FLAG_ISSET(excp2_en, (1 << action_index)))
    {
        excp2_en_flag = 1;
    }

    CTC_ERROR_RETURN(sys_greatbelt_port_get_internal_property(lchip, gport, SYS_PORT_PROP_EXCEPTION_DISCARD, &excp2_discard));
    if (CTC_FLAG_ISSET(excp2_discard, (1 << action_index)))
    {
        excp2_discard_flag = 1;
    }

    if (excp2_en_flag && excp2_discard_flag)
    {
        *action = CTC_PDU_L2PDU_ACTION_TYPE_REDIRECT_TO_CPU;
    }
    else if (excp2_en_flag)
    {
        *action = CTC_PDU_L2PDU_ACTION_TYPE_COPY_TO_CPU;
    }
    else if (excp2_discard_flag)
    {
        *action = CTC_PDU_L2PDU_ACTION_TYPE_DISCARD;
    }
    else
    {
        *action = CTC_PDU_L2PDU_ACTION_TYPE_FWD;
    }

    SYS_PDU_DBG_INFO("action:%d\n", *action);

    return CTC_E_NONE;
}

int32
sys_greatbelt_l2pdu_show_port_action(uint8 lchip, uint16 gport)
{
    uint8 action_index = 0;
    uint32 excp2_en = 0;
    uint32 excp2_discard = 0;
    uint8 excp2_en_flag = 0;
    uint8 excp2_discard_flag = 0;

    SYS_LCHIP_CHECK_ACTIVE(lchip);
    /*excption2En set, exception2Discard set*/
    CTC_ERROR_RETURN(sys_greatbelt_port_get_internal_property(lchip, gport, SYS_PORT_PROP_EXCEPTION_EN, &excp2_en));
    CTC_ERROR_RETURN(sys_greatbelt_port_get_internal_property(lchip, gport, SYS_PORT_PROP_EXCEPTION_DISCARD, &excp2_discard));

    SYS_PDU_DBG_DUMP("Abbreviate: C(copy to cpu), D(discard), R(redirect to cpu)\n");
    SYS_PDU_DBG_DUMP("------------------------------------\n");
    SYS_PDU_DBG_DUMP("  Action-index              Action  \n");
    SYS_PDU_DBG_DUMP("------------------------------------\n");

    for (action_index = 0; action_index < 32; action_index++)
    {
        excp2_en_flag = CTC_FLAG_ISSET(excp2_en, (1 << action_index));
        excp2_discard_flag = CTC_FLAG_ISSET(excp2_discard, (1 << action_index));
        if (excp2_en_flag && excp2_discard_flag)
        {
            SYS_PDU_DBG_DUMP("  %-27d%s\n", action_index, "R");
        }
        else if (excp2_en_flag)
        {
            SYS_PDU_DBG_DUMP("  %-27d%s\n", action_index, "C");
        }
        else if (excp2_discard_flag)
        {
            SYS_PDU_DBG_DUMP("  %-27d%s\n", action_index, "D");
        }
        else
        {
            SYS_PDU_DBG_DUMP("  %-27d%s\n", action_index, "-");
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l3pdu_classify_l3pdu_by_l3hdr_proto(uint8 lchip, uint8 index, ctc_pdu_l3pdu_key_t* entry)
{
    uint32 cmd = 0;
    int8   index_step = IpeException3Cam_L3HeaderProtocol1_f
        - IpeException3Cam_L3HeaderProtocol0_f;
    uint32 l3_header_protocol = 0;
    uint32 l3_header_protocol_id = IpeException3Cam_L3HeaderProtocol0_f;

    SYS_PDU_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PDU_DBG_PARAM("index:%d\n", index);
    SYS_PDU_DBG_PARAM("l3 header protocol:%d\n", entry->l3hdr_proto);

    if (index >= MAX_SYS_PDU_L3PDU_BASED_L3HDR_PROTO)
    {
        return CTC_E_PDU_INVALID_INDEX;
    }

    l3_header_protocol_id += index_step * index;
    l3_header_protocol = entry->l3hdr_proto;

    cmd = DRV_IOW(IpeException3Cam_t, l3_header_protocol_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &l3_header_protocol));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l3pdu_get_classified_key_by_l3hdr_proto(uint8 lchip, uint8 index, ctc_pdu_l3pdu_key_t* entry)
{
    uint32 cmd = 0;
    int8   index_step = IpeException3Cam_L3HeaderProtocol1_f
        - IpeException3Cam_L3HeaderProtocol0_f;
    uint32 l3_header_protocol = 0;
    uint32 l3_header_protocol_id = IpeException3Cam_L3HeaderProtocol0_f;

    SYS_PDU_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PDU_DBG_PARAM("index:%d\n", index);

    if (index >= MAX_SYS_PDU_L3PDU_BASED_L3HDR_PROTO)
    {
        return CTC_E_PDU_INVALID_INDEX;
    }

    l3_header_protocol_id += index_step * index;

    cmd = DRV_IOR(IpeException3Cam_t, l3_header_protocol_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &l3_header_protocol));

    entry->l3hdr_proto = l3_header_protocol;

    SYS_PDU_DBG_INFO("l3 header protocol:%d\n", entry->l3hdr_proto);

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l3pdu_classify_l3pdu_by_layer4_port(uint8 lchip, uint8 index, ctc_pdu_l3pdu_key_t* entry)
{
    uint32 cmd = 0;
    int8   index_step = IpeException3Cam2_IsUdpMask1_f
        - IpeException3Cam2_IsUdpMask0_f;
    uint32 dest_port = 0;
    uint32 is_tcp_mask = 0;
    uint32 is_tcp_value = 0;
    uint32 is_udp_mask = 0;
    uint32 is_udp_value = 0;
    uint32 dest_port_field_id = IpeException3Cam2_DestPort0_f;
    uint32 is_tcp_mask_field_id = IpeException3Cam2_IsTcpMask0_f;
    uint32 is_tcp_value_field_id = IpeException3Cam2_IsTcpValue0_f;
    uint32 is_udp_mask_field_id = IpeException3Cam2_IsUdpMask0_f;
    uint32 is_udp_value_field_id = IpeException3Cam2_IsUdpValue0_f;

    SYS_PDU_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_PDU_DBG_PARAM("index:%d\n", index);
    SYS_PDU_DBG_PARAM("dest port:%d  is tcp value:%d is udp value:%d\n", dest_port, entry->l3pdu_by_port.is_tcp, entry->l3pdu_by_port.is_udp);

    if (index >= MAX_SYS_PDU_L3PDU_BASED_PORT)
    {
        return CTC_E_PDU_INVALID_INDEX;
    }

    if (entry->l3pdu_by_port.is_tcp && entry->l3pdu_by_port.is_udp)
    {
        return CTC_E_INVALID_PARAM;
    }

    dest_port_field_id += index_step * index;
    is_tcp_mask_field_id += index_step * index;
    is_tcp_value_field_id += index_step * index;
    is_udp_mask_field_id += index_step * index;
    is_udp_value_field_id += index_step * index;

    dest_port = entry->l3pdu_by_port.dest_port;
    is_tcp_mask = 1;
    is_tcp_value = entry->l3pdu_by_port.is_tcp ? 1 : 0;
    is_udp_mask = 1;
    is_udp_value = entry->l3pdu_by_port.is_udp ? 1 : 0;

    cmd = DRV_IOW(IpeException3Cam2_t, dest_port_field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &dest_port));

    cmd = DRV_IOW(IpeException3Cam2_t, is_tcp_mask_field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &is_tcp_mask));

    cmd = DRV_IOW(IpeException3Cam2_t, is_tcp_value_field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &is_tcp_value));

    cmd = DRV_IOW(IpeException3Cam2_t, is_udp_mask_field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &is_udp_mask));

    cmd = DRV_IOW(IpeException3Cam2_t, is_udp_value_field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &is_udp_value));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l3pdu_get_classified_key_by_layer4_port(uint8 lchip, uint8 index, ctc_pdu_l3pdu_key_t* entry)
{
    uint32 cmd = 0;
    int8   index_step = IpeException3Cam2_IsUdpMask1_f
        - IpeException3Cam2_IsUdpMask0_f;
    uint32 dest_port = 0;
    uint32 is_tcp = 0;
    uint32 is_udp = 0;
    uint32 dest_port_field_id = IpeException3Cam2_DestPort0_f;
    uint32 is_tcp_field_id = IpeException3Cam2_IsTcpValue0_f;
    uint32 is_udp_field_id = IpeException3Cam2_IsUdpValue0_f;

    SYS_PDU_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_PDU_DBG_PARAM("index:%d\n", index);

    if (index >= MAX_SYS_PDU_L3PDU_BASED_PORT)
    {
        return CTC_E_PDU_INVALID_INDEX;
    }

    dest_port_field_id += index_step * index;
    is_tcp_field_id += index_step * index;
    is_udp_field_id += index_step * index;

    cmd = DRV_IOR(IpeException3Cam2_t, dest_port_field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &dest_port));

    cmd = DRV_IOR(IpeException3Cam2_t, is_tcp_field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &is_tcp));

    cmd = DRV_IOR(IpeException3Cam2_t, is_udp_field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &is_udp));

    entry->l3pdu_by_port.dest_port = dest_port;
    entry->l3pdu_by_port.is_tcp = is_tcp;
    entry->l3pdu_by_port.is_udp = is_udp;

    SYS_PDU_DBG_INFO("dest port:%d  is tcp value:%d is udp value:%d\n", dest_port, entry->l3pdu_by_port.is_tcp, entry->l3pdu_by_port.is_udp);

    return CTC_E_NONE;
}

/**
 @brief  Classify layer3 pdu based on layer3 header protocol, layer4 dest port
*/
int32
sys_greatbelt_l3pdu_classify_l3pdu(uint8 lchip, ctc_pdu_l3pdu_type_t l3pdu_type, uint8 index,
                                   ctc_pdu_l3pdu_key_t* key)
{
    SYS_LCHIP_CHECK_ACTIVE(lchip);
    CTC_PTR_VALID_CHECK(key);

    SYS_PDU_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_PDU_DBG_PARAM("l3pdu type:%d\n", l3pdu_type);
    SYS_PDU_DBG_PARAM("index:%d\n", index);

    switch (l3pdu_type)
    {
    case CTC_PDU_L3PDU_TYPE_L3HDR_PROTO:
        CTC_ERROR_RETURN(_sys_greatbelt_l3pdu_classify_l3pdu_by_l3hdr_proto(lchip, index, key));
        break;

    case CTC_PDU_L3PDU_TYPE_LAYER4_PORT:
        CTC_ERROR_RETURN(_sys_greatbelt_l3pdu_classify_l3pdu_by_layer4_port(lchip, index, key));
        break;

    default:
        break;
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_l3pdu_get_classified_key(uint8 lchip, ctc_pdu_l3pdu_type_t l3pdu_type, uint8 index, ctc_pdu_l3pdu_key_t* key)
{
    SYS_LCHIP_CHECK_ACTIVE(lchip);
    CTC_PTR_VALID_CHECK(key);

    SYS_PDU_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_PDU_DBG_PARAM("l3pdu type:%d\n", l3pdu_type);
    SYS_PDU_DBG_PARAM("index:%d\n", index);

    switch (l3pdu_type)
    {
    case CTC_PDU_L3PDU_TYPE_L3HDR_PROTO:
        CTC_ERROR_RETURN(_sys_greatbelt_l3pdu_get_classified_key_by_l3hdr_proto(lchip, index, key));
        break;

    case CTC_PDU_L3PDU_TYPE_LAYER4_PORT:
        CTC_ERROR_RETURN(_sys_greatbelt_l3pdu_get_classified_key_by_layer4_port(lchip, index, key));
        break;

    default:
        break;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l3pdu_set_global_action_of_fixed_l3pdu(uint8 lchip, ctc_pdu_l3pdu_type_t l3pdu_type,
                                                      ctc_pdu_global_l3pdu_action_t* ctl)
{
    uint32 cmd = 0;
    ipe_exception3_ctl_t ipe_exception3_ctl;

    SYS_PDU_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_PDU_DBG_PARAM("action index:%d\n", ctl->action_index);
    SYS_PDU_DBG_PARAM("entry valid:%d\n", ctl->entry_valid);

    sal_memset(&ipe_exception3_ctl, 0, sizeof(ipe_exception3_ctl_t));

    cmd = DRV_IOR(IpeException3Ctl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_exception3_ctl));

    switch (l3pdu_type)
    {
    case CTC_PDU_L3PDU_TYPE_OSPF:
        ipe_exception3_ctl.ospf_exception_en = ctl->entry_valid ? 1 : 0;
        ipe_exception3_ctl.ospf_exception_sub_index = ctl->action_index;
        break;

    case CTC_PDU_L3PDU_TYPE_PIM:
        ipe_exception3_ctl.pim_exception_en = ctl->entry_valid ? 1 : 0;
        ipe_exception3_ctl.pim_exception_sub_index = ctl->action_index;
        break;

    case CTC_PDU_L3PDU_TYPE_VRRP:
        ipe_exception3_ctl.vrrp_exception_en = ctl->entry_valid ? 1 : 0;
        ipe_exception3_ctl.vrrp_exception_sub_index = ctl->action_index;
        break;

    case CTC_PDU_L3PDU_TYPE_RSVP:
        ipe_exception3_ctl.rsvp_exception_en = ctl->entry_valid ? 1 : 0;
        ipe_exception3_ctl.rsvp_exception_sub_index = ctl->action_index;
        break;

    case CTC_PDU_L3PDU_TYPE_RIP:
        ipe_exception3_ctl.rip_exception_en = ctl->entry_valid ? 1 : 0;
        ipe_exception3_ctl.rip_exception_sub_index = ctl->action_index;
        break;

    case CTC_PDU_L3PDU_TYPE_BGP:
        ipe_exception3_ctl.bgp_exception_en = ctl->entry_valid ? 1 : 0;
        ipe_exception3_ctl.bgp_exception_sub_index = ctl->action_index;
        break;

    case CTC_PDU_L3PDU_TYPE_MSDP:
        ipe_exception3_ctl.msdp_exception_en = ctl->entry_valid ? 1 : 0;
        ipe_exception3_ctl.msdp_exception_sub_index = ctl->action_index;
        break;

    case CTC_PDU_L3PDU_TYPE_LDP:
        ipe_exception3_ctl.ldp_exception_en = ctl->entry_valid ? 1 : 0;
        ipe_exception3_ctl.ldp_exception_sub_index = ctl->action_index;
        break;

    default:
        break;
    }

    cmd = DRV_IOW(IpeException3Ctl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_exception3_ctl));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l3pdu_get_global_action_of_fixed_l3pdu(uint8 lchip, ctc_pdu_l3pdu_type_t l3pdu_type,
                                                      ctc_pdu_global_l3pdu_action_t* ctl)
{
    uint32 cmd = 0;
    ipe_exception3_ctl_t ipe_exception3_ctl;

    SYS_PDU_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    sal_memset(&ipe_exception3_ctl, 0, sizeof(ipe_exception3_ctl_t));
    cmd = DRV_IOR(IpeException3Ctl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_exception3_ctl));

    switch (l3pdu_type)
    {
    case CTC_PDU_L3PDU_TYPE_OSPF:
        ctl->entry_valid = ipe_exception3_ctl.ospf_exception_en;
        ctl->action_index = ipe_exception3_ctl.ospf_exception_sub_index;
        break;

    case CTC_PDU_L3PDU_TYPE_PIM:
        ctl->entry_valid = ipe_exception3_ctl.pim_exception_en;
        ctl->action_index = ipe_exception3_ctl.pim_exception_sub_index;
        break;

    case CTC_PDU_L3PDU_TYPE_VRRP:
        ctl->entry_valid = ipe_exception3_ctl.vrrp_exception_en;
        ctl->action_index = ipe_exception3_ctl.vrrp_exception_sub_index;
        break;

    case CTC_PDU_L3PDU_TYPE_RSVP:
        ctl->entry_valid = ipe_exception3_ctl.rsvp_exception_en;
        ctl->action_index = ipe_exception3_ctl.rsvp_exception_sub_index;
        break;

    case CTC_PDU_L3PDU_TYPE_RIP:
        ctl->entry_valid = ipe_exception3_ctl.rip_exception_en;
        ctl->action_index = ipe_exception3_ctl.rip_exception_sub_index;
        break;

    case CTC_PDU_L3PDU_TYPE_BGP:
        ctl->entry_valid = ipe_exception3_ctl.bgp_exception_en;
        ctl->action_index = ipe_exception3_ctl.bgp_exception_sub_index;
        break;

    case CTC_PDU_L3PDU_TYPE_MSDP:
        ctl->entry_valid = ipe_exception3_ctl.msdp_exception_en;
        ctl->action_index = ipe_exception3_ctl.msdp_exception_sub_index;
        break;

    case CTC_PDU_L3PDU_TYPE_LDP:
        ctl->entry_valid = ipe_exception3_ctl.ldp_exception_en;
        ctl->action_index = ipe_exception3_ctl.ldp_exception_sub_index;
        break;

    default:
        break;
    }

    SYS_PDU_DBG_INFO("action index:%d\n", ctl->action_index);
    SYS_PDU_DBG_INFO("entry valid:%d\n", ctl->entry_valid);

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l3pdu_set_global_action_by_l3hdr_proto(uint8 lchip, uint8 index, ctc_pdu_global_l3pdu_action_t* ctl)
{
    uint32 cmd = 0;
    uint32 entry_valid = 0;
    uint32 exception_sub_index = 0;
    int8   index_step = IpeException3Cam_CamEntryValid1_f
        - IpeException3Cam_CamEntryValid0_f;
    uint32 entry_valid_field_id = IpeException3Cam_CamEntryValid0_f;
    uint32 exception_sub_index_field_id = IpeException3Cam_CamExceptionSubIndex0_f;

    SYS_PDU_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_PDU_DBG_PARAM("index:%d entry valid:%d  action index:%d\n", index, ctl->entry_valid, ctl->action_index);

    if (index >= MAX_SYS_PDU_L3PDU_BASED_L3HDR_PROTO)
    {
        return CTC_E_PDU_INVALID_INDEX;
    }

    entry_valid = ctl->entry_valid ? 1 : 0;
    exception_sub_index = ctl->action_index;

    entry_valid_field_id += index_step * index;
    exception_sub_index_field_id += index_step * index;

    cmd = DRV_IOW(IpeException3Cam_t, entry_valid_field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &entry_valid));

    cmd = DRV_IOW(IpeException3Cam_t, exception_sub_index_field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &exception_sub_index));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l3pdu_get_global_action_by_l3hdr_proto(uint8 lchip, uint8 index, ctc_pdu_global_l3pdu_action_t* ctl)
{
    uint32 cmd = 0;
    uint32 entry_valid = 0;
    uint32 exception_sub_index = 0;
    int8   index_step = IpeException3Cam_CamEntryValid1_f
        - IpeException3Cam_CamEntryValid0_f;
    uint32 entry_valid_field_id = IpeException3Cam_CamEntryValid0_f;
    uint32 exception_sub_index_field_id = IpeException3Cam_CamExceptionSubIndex0_f;

    SYS_PDU_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    if (index >= MAX_SYS_PDU_L3PDU_BASED_L3HDR_PROTO)
    {
        return CTC_E_PDU_INVALID_INDEX;
    }

    entry_valid_field_id += index_step * index;
    exception_sub_index_field_id += index_step * index;

    cmd = DRV_IOR(IpeException3Cam_t, entry_valid_field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &entry_valid));

    cmd = DRV_IOR(IpeException3Cam_t, exception_sub_index_field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &exception_sub_index));

    ctl->entry_valid = entry_valid;
    ctl->action_index = exception_sub_index;

    SYS_PDU_DBG_INFO("index:%d entry valid:%d  action index:%d\n", index, ctl->entry_valid, ctl->action_index);

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l3pdu_set_global_action_by_layer4_port(uint8 lchip, uint8 index, ctc_pdu_global_l3pdu_action_t* ctl)
{
    uint32 cmd = 0;
    int8   index_step = IpeException3Cam2_Cam2EntryValid1_f
        - IpeException3Cam2_Cam2EntryValid0_f;
    uint32 entry_valid = 0;
    uint32 exception_sub_index = 0;
    uint32 entry_valid_field_id = IpeException3Cam2_Cam2EntryValid0_f;
    uint32 exception_sub_index_field_id = IpeException3Cam2_Cam2ExceptionSubIndex0_f;

    SYS_PDU_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_PDU_DBG_PARAM("index:%d entry valid:%d  action index:%d\n", index, ctl->entry_valid, ctl->action_index);

    if (index >= MAX_SYS_PDU_L3PDU_BASED_PORT)
    {
        return CTC_E_PDU_INVALID_INDEX;
    }

    entry_valid = ctl->entry_valid ? 1 : 0;
    exception_sub_index = ctl->action_index;

    entry_valid_field_id += index_step * index;
    exception_sub_index_field_id += index_step * index;

    cmd = DRV_IOW(IpeException3Cam2_t, entry_valid_field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &entry_valid));

    cmd = DRV_IOW(IpeException3Cam2_t, exception_sub_index_field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &exception_sub_index));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l3pdu_get_global_action_by_layer4_port(uint8 lchip, uint8 index, ctc_pdu_global_l3pdu_action_t* ctl)
{
    uint32 cmd = 0;
    int8   index_step = IpeException3Cam2_Cam2EntryValid1_f
        - IpeException3Cam2_Cam2EntryValid0_f;
    uint32 entry_valid = 0;
    uint32 exception_sub_index = 0;
    uint32 entry_valid_field_id = IpeException3Cam2_Cam2EntryValid0_f;
    uint32 exception_sub_index_field_id = IpeException3Cam2_Cam2ExceptionSubIndex0_f;

    SYS_PDU_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    if (index >= MAX_SYS_PDU_L3PDU_BASED_PORT)
    {
        return CTC_E_PDU_INVALID_INDEX;
    }

    entry_valid_field_id += index_step * index;
    exception_sub_index_field_id += index_step * index;

    cmd = DRV_IOR(IpeException3Cam2_t, entry_valid_field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &entry_valid));

    cmd = DRV_IOR(IpeException3Cam2_t, exception_sub_index_field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &exception_sub_index));

    ctl->entry_valid = entry_valid;
    ctl->action_index = exception_sub_index;

    SYS_PDU_DBG_INFO("index:%d entry valid:%d  action index:%d\n", index, ctl->entry_valid, ctl->action_index);

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l3pdu_set_global_action_by_layer4_type(uint8 lchip, uint8 index, ctc_pdu_global_l3pdu_action_t* ctl)
{
    SYS_PDU_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    return CTC_E_NOT_SUPPORT;
}

STATIC int32
_sys_greatbelt_l3pdu_get_global_action_by_layer4_type(uint8 lchip, uint8 index, ctc_pdu_global_l3pdu_action_t* ctl)
{
    SYS_PDU_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    return CTC_E_NOT_SUPPORT;
}

/**
 @brief  Set layer3 pdu global property
*/
int32
sys_greatbelt_l3pdu_set_global_action(uint8 lchip, ctc_pdu_l3pdu_type_t l3pdu_type, uint8 index, ctc_pdu_global_l3pdu_action_t* action)
{
    SYS_LCHIP_CHECK_ACTIVE(lchip);
    CTC_PTR_VALID_CHECK(action);

    SYS_PDU_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_PDU_DBG_PARAM("l3pdu type:%d\n", l3pdu_type);
    SYS_PDU_DBG_PARAM("index:%d\n", index);

    if (action->action_index > MAX_SYS_L3PDU_PER_L3IF_ACTION_INDEX)
    {
        return CTC_E_PDU_INVALID_ACTION_INDEX;
    }

    switch (l3pdu_type)
    {
    case CTC_PDU_L3PDU_TYPE_OSPF:
    case CTC_PDU_L3PDU_TYPE_PIM:
    case CTC_PDU_L3PDU_TYPE_VRRP:
    case CTC_PDU_L3PDU_TYPE_RSVP:
    case CTC_PDU_L3PDU_TYPE_RIP:
    case CTC_PDU_L3PDU_TYPE_BGP:
    case CTC_PDU_L3PDU_TYPE_MSDP:
    case CTC_PDU_L3PDU_TYPE_LDP:
        CTC_ERROR_RETURN(_sys_greatbelt_l3pdu_set_global_action_of_fixed_l3pdu(lchip, l3pdu_type, action));
        break;

    case CTC_PDU_L3PDU_TYPE_L3HDR_PROTO:
        CTC_ERROR_RETURN(_sys_greatbelt_l3pdu_set_global_action_by_l3hdr_proto(lchip, index, action));
        break;

    case CTC_PDU_L3PDU_TYPE_LAYER4_PORT:
        CTC_ERROR_RETURN(_sys_greatbelt_l3pdu_set_global_action_by_layer4_port(lchip, index, action));
        break;

    case CTC_PDU_L3PDU_TYPE_LAYER4_TYPE:
        CTC_ERROR_RETURN(_sys_greatbelt_l3pdu_set_global_action_by_layer4_type(lchip, index, action));
        break;

    default:
        break;
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_l3pdu_get_global_action(uint8 lchip, ctc_pdu_l3pdu_type_t l3pdu_type, uint8 index, ctc_pdu_global_l3pdu_action_t* action)
{
    SYS_LCHIP_CHECK_ACTIVE(lchip);
    SYS_PDU_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_PDU_DBG_PARAM("l3pdu type:%d\n", l3pdu_type);
    SYS_PDU_DBG_PARAM("index:%d\n", index);

    switch (l3pdu_type)
    {
    case CTC_PDU_L3PDU_TYPE_OSPF:
    case CTC_PDU_L3PDU_TYPE_PIM:
    case CTC_PDU_L3PDU_TYPE_VRRP:
    case CTC_PDU_L3PDU_TYPE_RSVP:
    case CTC_PDU_L3PDU_TYPE_RIP:
    case CTC_PDU_L3PDU_TYPE_BGP:
    case CTC_PDU_L3PDU_TYPE_MSDP:
    case CTC_PDU_L3PDU_TYPE_LDP:
        CTC_ERROR_RETURN(_sys_greatbelt_l3pdu_get_global_action_of_fixed_l3pdu(lchip, l3pdu_type, action));
        break;

    case CTC_PDU_L3PDU_TYPE_L3HDR_PROTO:
        CTC_ERROR_RETURN(_sys_greatbelt_l3pdu_get_global_action_by_l3hdr_proto(lchip, index, action));
        break;

    case CTC_PDU_L3PDU_TYPE_LAYER4_PORT:
        CTC_ERROR_RETURN(_sys_greatbelt_l3pdu_get_global_action_by_layer4_port(lchip, index, action));
        break;

    case CTC_PDU_L3PDU_TYPE_LAYER4_TYPE:
        CTC_ERROR_RETURN(_sys_greatbelt_l3pdu_get_global_action_by_layer4_type(lchip, index, action));
        break;

    default:
        break;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l2pdu_exception_enable_init(uint8 lchip)
{
    uint32 cmd = 0;
    ipe_bpdu_escape_ctl_t ipe_bpdu_escape_ctl;

    sal_memset(&ipe_bpdu_escape_ctl, 0, sizeof(ipe_bpdu_escape_ctl_t));

    ipe_bpdu_escape_ctl.bpdu_escape_en                 = 0xF;

    /* BPDU, SLOW_PROTOCOL, EAPOL, LLDP, L2ISIS, EFM, FIP auto identified */
    ipe_bpdu_escape_ctl.bpdu_exception_en                 = 1;
    ipe_bpdu_escape_ctl.bpdu_exception_sub_index      = CTC_L2PDU_ACTION_INDEX_BPDU;
    ipe_bpdu_escape_ctl.lldp_exception_en                   = 1;
    ipe_bpdu_escape_ctl.lldp_exception_sub_index        = CTC_L2PDU_ACTION_INDEX_LLDP;
    ipe_bpdu_escape_ctl.eapol_exception_en                = 1;
    ipe_bpdu_escape_ctl.eapol_exception_sub_index         = CTC_L2PDU_ACTION_INDEX_EAPOL;
    ipe_bpdu_escape_ctl.slow_protocol_exception_en        = 1;
    ipe_bpdu_escape_ctl.slow_protocol_exception_sub_index = CTC_L2PDU_ACTION_INDEX_SLOW_PROTO;
    ipe_bpdu_escape_ctl.l2isis_exception_en            = 1;
    ipe_bpdu_escape_ctl.l2isis_exception_sub_index = CTC_L2PDU_ACTION_INDEX_ISIS;

    cmd = DRV_IOW(IpeBpduEscapeCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_bpdu_escape_ctl));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l3pdu_exception_enable_init(uint8 lchip)
{
    uint32 cmd = 0;
    ipe_exception3_ctl_t ipe_exception3_ctl;

    sal_memset(&ipe_exception3_ctl, 0, sizeof(ipe_exception3_ctl_t));

    ipe_exception3_ctl.exception_cam_en         = 1;
    ipe_exception3_ctl.exception_cam_en2        = 1;

    /* RIP, OSPF, BGP, PIM, VRRP, RSVP, MSDP, LDP auto identified */
    ipe_exception3_ctl.bgp_exception_en         = 1;
    ipe_exception3_ctl.bgp_exception_sub_index  = CTC_L3PDU_ACTION_INDEX_BGP;
    ipe_exception3_ctl.ldp_exception_en         = 1;
    ipe_exception3_ctl.ldp_exception_sub_index  = CTC_L3PDU_ACTION_INDEX_LDP;
    ipe_exception3_ctl.msdp_exception_en        = 1;
    ipe_exception3_ctl.msdp_exception_sub_index = CTC_L3PDU_ACTION_INDEX_MSDP;
    ipe_exception3_ctl.ospf_exception_en        = 1;
    ipe_exception3_ctl.ospf_exception_sub_index = CTC_L3PDU_ACTION_INDEX_OSPF;
    ipe_exception3_ctl.pim_exception_en         = 1;
    ipe_exception3_ctl.pim_exception_sub_index  = CTC_L3PDU_ACTION_INDEX_PIM;
    ipe_exception3_ctl.rip_exception_en         = 1;
    ipe_exception3_ctl.rip_exception_sub_index  = CTC_L3PDU_ACTION_INDEX_RIP;
    ipe_exception3_ctl.rsvp_exception_en        = 1;
    ipe_exception3_ctl.rsvp_exception_sub_index = CTC_L3PDU_ACTION_INDEX_RSVP;
    ipe_exception3_ctl.vrrp_exception_en        = 1;
    ipe_exception3_ctl.vrrp_exception_sub_index = CTC_L3PDU_ACTION_INDEX_VRRP;

    cmd = DRV_IOW(IpeException3Ctl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_exception3_ctl));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l2pdu_cam_init(uint8 lchip)
{
    int32 cmd_cam, cmd_rst, cmd_cam2, cmd_rst2, cmd_cam3, cmd_rst3, cmd_cam4, cmd_rst4;
    ipe_bpdu_protocol_escape_cam_t cam;
    ipe_bpdu_protocol_escape_cam2_t cam2;
    ipe_bpdu_protocol_escape_cam3_t cam3;
    ipe_bpdu_protocol_escape_cam4_t cam4;
    ipe_bpdu_protocol_escape_cam_result_t cam_rst;
    ipe_bpdu_protocol_escape_cam_result2_t cam2_rst;
    ipe_bpdu_protocol_escape_cam_result3_t cam3_rst;
    ipe_bpdu_protocol_escape_cam_result4_t cam4_rst;
    ipe_bpdu_protocol_escape_cam_result4_t cam_result4;
    uint32 cmd = 0;
    uint32 cam4_mac_da_value = 0;
   
    sal_memset(&cam, 0, sizeof(ipe_bpdu_protocol_escape_cam_t));
    sal_memset(&cam2, 0, sizeof(ipe_bpdu_protocol_escape_cam2_t));
    sal_memset(&cam3, 0, sizeof(ipe_bpdu_protocol_escape_cam3_t));
    sal_memset(&cam4, 0, sizeof(ipe_bpdu_protocol_escape_cam4_t));

    sal_memset(&cam_rst, 0, sizeof(ipe_bpdu_protocol_escape_cam_result_t));
    sal_memset(&cam2_rst, 0, sizeof(ipe_bpdu_protocol_escape_cam_result2_t));
    sal_memset(&cam3_rst, 0, sizeof(ipe_bpdu_protocol_escape_cam_result3_t));
    sal_memset(&cam4_rst, 0, sizeof(ipe_bpdu_protocol_escape_cam_result4_t));

    cmd_cam = DRV_IOW(IpeBpduProtocolEscapeCam_t, DRV_ENTRY_FLAG);
    cmd_rst = DRV_IOW(IpeBpduProtocolEscapeCamResult_t, DRV_ENTRY_FLAG);

    cmd_cam2 = DRV_IOW(IpeBpduProtocolEscapeCam2_t, DRV_ENTRY_FLAG);
    cmd_rst2 = DRV_IOW(IpeBpduProtocolEscapeCamResult2_t, DRV_ENTRY_FLAG);

    cmd_cam3 = DRV_IOW(IpeBpduProtocolEscapeCam3_t, DRV_ENTRY_FLAG);
    cmd_rst3 = DRV_IOW(IpeBpduProtocolEscapeCamResult3_t, DRV_ENTRY_FLAG);

    cmd_cam4 = DRV_IOW(IpeBpduProtocolEscapeCam4_t, DRV_ENTRY_FLAG);
    cmd_rst4 = DRV_IOW(IpeBpduProtocolEscapeCamResult4_t, DRV_ENTRY_FLAG);

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd_cam, &cam));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd_rst, &cam_rst));

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd_cam2, &cam2));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd_rst2, &cam2_rst));

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd_cam3, &cam3));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd_rst3, &cam3_rst));

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd_cam4, &cam4));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd_rst4, &cam4_rst));

    /*add pdu entry to identify pause packet*/
    cam_result4.cam4_entry_valid = 1;
    cam_result4.cam4_exception_sub_index = 31;
    cmd = DRV_IOW(IpeBpduProtocolEscapeCamResult4_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 7, cmd, &cam_result4));
        
    cam4_mac_da_value =  0x01;
    cmd = DRV_IOW(IpeBpduProtocolEscapeCam4_t, IpeBpduProtocolEscapeCam4_MacDa7_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &cam4_mac_da_value));
    
    return CTC_E_NONE;

}

STATIC int32
_sys_greatbelt_l3pdu_cam_init(uint8 lchip)
{
    int32 cmd_cam, cmd_cam2;
    ipe_exception3_cam_t cam;
    ipe_exception3_cam2_t cam2;

    sal_memset(&cam, 0, sizeof(ipe_exception3_cam_t));
    sal_memset(&cam2, 0, sizeof(ipe_exception3_cam2_t));

    cmd_cam = DRV_IOW(IpeException3Cam_t, DRV_ENTRY_FLAG);
    cmd_cam2 = DRV_IOW(IpeException3Cam2_t, DRV_ENTRY_FLAG);

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd_cam, &cam));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd_cam2, &cam2));

    return CTC_E_NONE;
}

/**
 @brief init pdu module
*/
int32
sys_greatbelt_pdu_init(uint8 lchip)
{
    LCHIP_CHECK(lchip);
    CTC_ERROR_RETURN(_sys_greatbelt_l2pdu_exception_enable_init(lchip));

    CTC_ERROR_RETURN(_sys_greatbelt_l2pdu_cam_init(lchip));

    CTC_ERROR_RETURN(_sys_greatbelt_l3pdu_exception_enable_init(lchip));

    CTC_ERROR_RETURN(_sys_greatbelt_l3pdu_cam_init(lchip));

    return CTC_E_NONE;
}

int32
sys_greatbelt_pdu_deinit(uint8 lchip)
{
    LCHIP_CHECK(lchip);
    return CTC_E_NONE;
}

#define ___________PDU_OUTER_FUNCTION___________
int32 _sys_greatbelt_pdu_show_l2pdu_static_cfg(uint8 lchip,uint8 l2pdu_type)
{
  ctc_pdu_global_l2pdu_action_t  action;
  char* column[4] = {0};
  sal_memset(&action, 0, sizeof(ctc_pdu_global_l2pdu_action_t));
  switch(l2pdu_type)
  {
  case CTC_PDU_L2PDU_TYPE_BPDU:           
      column[0] = "BPDU"; /*Type*/
      column[1] = "0180.c200.0000";    /*MACDA*/
      column[2] = "-";    /*EthType*/
        break;
  case CTC_PDU_L2PDU_TYPE_SLOW_PROTO:   
      column[0] = "Slow Protocol";
      column[1] = "-";
      column[2] = "0x8809";
       break;                                                 
  case CTC_PDU_L2PDU_TYPE_EAPOL:  
      column[0] = "EAPOL";
      column[1] = "-";
      column[2] = "0x888E";
       break;   
  case CTC_PDU_L2PDU_TYPE_LLDP:                    
      column[0] = "LLDP"; /*Type*/
      column[1] = "-";    /*MACDA*/
      column[2] = "0x88CC";    /*EthType*/
      break;
  case CTC_PDU_L2PDU_TYPE_EFM_OAM:          
      column[0] = "EFM_OAM"; /*Type*/
      column[1] = "0180.c200.0000";    /*MACDA*/
      column[2] = "0x8809";    /*EthType*/
       break; 

   default :
       return CTC_E_NONE;
  }
  _sys_greatbelt_l2pdu_get_global_action_of_fixed_l2pdu(lchip,l2pdu_type,&action);
  CTC_DEBUG_OUT(pdu, pdu, PDU_SYS, CTC_DEBUG_LEVEL_DUMP, "%-16s%-17s%-13s%-15d%-5s\n",
     column[0],column[1], column[2], action.action_index,action.entry_valid?"True":"False");      
 return CTC_E_NONE;
}

int32 _sys_greatbelt_pdu_show_l2pdu_dynamic_cfg(uint8 lchip, uint8 l2pdu_type)
{
    ctc_pdu_l2pdu_key_t l2pdu_key;
    ctc_pdu_global_l2pdu_action_t pdu_action;  
    int32 ret = 0;
    uint8 idx = 0;
    char* column[4] = {0};
    char  str[32] = {0};

    column[0] = "-"; /*Type*/
    column[1] = "-";    /*MACDA*/
    column[2] = "-";    /*EthType*/
    /*max value :MAX_SYS_PDU_L2PDU_BASED_L2HDR_PTL_ENTRY*/   
    for (idx = 0; idx < MAX_SYS_PDU_L2PDU_BASED_L2HDR_PTL_ENTRY ; idx++)
    {
        ret = sys_greatbelt_l2pdu_get_classified_key( lchip,l2pdu_type, idx, &l2pdu_key);     
        ret = ret ? ret: sys_greatbelt_l2pdu_get_global_action(lchip,l2pdu_type, idx, &pdu_action);
        if (ret != CTC_E_NONE)
        {
            continue;
        }
        switch(l2pdu_type)
        {
        case CTC_PDU_L2PDU_TYPE_L2HDR_PROTO:           
            if (pdu_action.entry_valid)
            {
                sal_sprintf(str, "0x%-4x", l2pdu_key.l2hdr_proto);
                column[2] = str;
                column[0] = "User-defined";
            }
            else
            {
                column[0] = "None-defined";
                column[2] = "x";
            }
            break;
        case CTC_PDU_L2PDU_TYPE_MACDA:   
        
            if (pdu_action.entry_valid)
            {
                column[1] = sys_greatbelt_output_mac(l2pdu_key.l2pdu_by_mac.mac);
                column[0] = "User-defined";
            }
            else
            {
                column[0] = "None-defined";
                column[1] = "xxxx.xxxx.xxxx";
            }
            break;
            break;                                                 
        case CTC_PDU_L2PDU_TYPE_MACDA_LOW24:  
            if (pdu_action.entry_valid)
            {
                column[1] = sys_greatbelt_output_mac(l2pdu_key.l2pdu_by_mac.mac);              
                column[0] = "User-defined";
            }
            else
            {
                column[0] = "None-defined";
                column[1] = "0180.C2xx.xxxx";
            }
            break;       
        default :
            return CTC_E_NONE;
        }
        CTC_DEBUG_OUT(pdu, pdu, PDU_SYS, CTC_DEBUG_LEVEL_DUMP, "%-16s%-17s%-13s%-15d%-5s\n",
                      column[0] , column[1], column[2], pdu_action.action_index, pdu_action.entry_valid?"True":"False");      
    }     
    return CTC_E_NONE;
}
int32
sys_greatbelt_pdu_show_l2pdu_cfg(uint8 lchip)
{
    uint32 idx = 0;

    LCHIP_CHECK(lchip);
    SYS_PDU_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "L2pdu:\n");
    SYS_PDU_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Note: for MAC address the token \"x\" used as an indication\n");
    SYS_PDU_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "      of a max hex value 0xF that user can configure.\n");
     SYS_PDU_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "------------------------------------------------------------------------------\n");
    CTC_DEBUG_OUT(pdu, pdu, PDU_SYS, CTC_DEBUG_LEVEL_DUMP, "%-16s%-17s%-13s%-15s%-5s\n",
     "Type","MAC-DA", " Ether-type", "Action-index","Valid"); 

    SYS_PDU_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "------------------------------------------------------------------------------\n");

    for (idx = 0; idx < MAX_CTC_PDU_L2PDU_TYPE; idx++)
    {
        if ((CTC_PDU_L2PDU_TYPE_L3TYPE == idx) || (CTC_PDU_L2PDU_TYPE_L2HDR_PROTO == idx)
           || (CTC_PDU_L2PDU_TYPE_MACDA == idx) || (CTC_PDU_L2PDU_TYPE_MACDA_LOW24 == idx)
           )
        {
           continue;
        }
        else
        {
          _sys_greatbelt_pdu_show_l2pdu_static_cfg(lchip,idx);
        }
    }
    
    for (idx = 0; idx < MAX_CTC_PDU_L2PDU_TYPE; idx++)
    {
        if ((CTC_PDU_L2PDU_TYPE_L3TYPE == idx) || (CTC_PDU_L2PDU_TYPE_L2HDR_PROTO == idx)
           || (CTC_PDU_L2PDU_TYPE_MACDA == idx) || (CTC_PDU_L2PDU_TYPE_MACDA_LOW24 == idx)
           )
        {
          _sys_greatbelt_pdu_show_l2pdu_dynamic_cfg(lchip,idx);
        }
    }
    return CTC_E_NONE;
}
int32 _sys_greatbelt_pdu_show_l3pdu_static_cfg(uint8 lchip,uint8 l3pdu_type)
{
  ctc_pdu_global_l3pdu_action_t  action;
  char* column[5] = {0};  
  sal_memset(&action, 0, sizeof(ctc_pdu_global_l3pdu_action_t));
  switch(l3pdu_type)
  {
  case CTC_PDU_L3PDU_TYPE_RSVP:               /**< [GB.GG.D2] Layer3 protocol = 46:  static classify by protocol.*/
      column[0] = "RSVP"; /*Type*/
      column[1] = "-";    /*L3 Type*/
      column[2] = "-";    /*IPDA*/
      column[3] = "46";   /*Protocol*/
      column[4] = "-";    /*L4 Port*/
      break;
  case CTC_PDU_L3PDU_TYPE_RIP:                /**< [GB.GG.D2] UDP:  for GB DestPort = 520 or DestPort = 521:  for GG DestPort = 520:  static classify by upd port.*/
      column[0] = "RIP";
      column[1] = "-";
      column[2] = "-";
      column[3] = "-";
      column[4] = "520"; 
      break;                                                 
  case CTC_PDU_L3PDU_TYPE_MSDP:               /**< [GB.GG.D2] TCP:  DestPort = 639:  static classify by upd port.*/
      column[0] = "MSDP";
      column[1] = "-";
      column[2] = "-";
      column[3] = "-";
      column[4] = "520"; 
      break;   
  case CTC_PDU_L3PDU_TYPE_LDP:                /**< [GB.GG.D2] TCP or UDP:  DestPort = 646:  other ldp destport pdu classify
                                                   by CTC_PDU_L3PDU_TYPE_LAYER4_PORT or forwarding to cpu.*/
      column[0] = "LDP"; /*Type*/
      column[1] = "-";    /*L3 Type*/
      column[2] = "-";    /*IPDA*/
      column[3] = "-";   /*Protocol*/
      column[4] = "646";    /*L4 Port*/
      break;
  case CTC_PDU_L3PDU_TYPE_OSPF:           /**< [GG.D2] IPv4:  layer3 protocol = 89:  static classify by protocol.*/
      column[0] = "OSPF"; /*Type*/
      column[1] = "IPv4|IPv6";    /*L3 Type*/
      column[2] = "-";    /*IPDA*/
      column[3] = "89";   /*Protocol*/
      column[4] = "-";    /*L4 Port*/
      break;
  case CTC_PDU_L3PDU_TYPE_PIM:            /**< [GG.D2] IPv4:  layer3 protocol = 103:  static classify by protocol.*/
      column[0] = "PIM"; /*Type*/
      column[1] = "IPv4|IPv6";    /*L3 Type*/
      column[2] = "-";    /*IPDA*/
      column[3] = "103";   /*Protocol*/
      column[4] = "-";    /*L4 Port*/
      break; 
  case CTC_PDU_L3PDU_TYPE_VRRP:           /**< [GG.D2] IPv4:  layer3 protocol = 112:  static classify by protocol.*/
      column[0] = "VRRP"; /*Type*/
      column[1] = "IPv4|IPv6";    /*L3 Type*/
      column[2] = "-";    /*IPDA*/
      column[3] = "112";   /*Protocol*/
      column[4] = "-";    /*L4 Port*/
      break;
 
  case CTC_PDU_L3PDU_TYPE_BGP:            /**< [GG.D2] IPv4:  TCP:  DestPort = 179:  static classify by protocol.*/
      column[0] = "BGP"; /*Type*/
      column[1] = "IPv4|IPv6";    /*L3 Type*/
      column[2] = "-";    /*IPDA*/
      column[3] = "-";   /*Protocol*/
      column[4] = "179";    /*L4 Port*/
      break;  
  case CTC_PDU_L3PDU_TYPE_ARP:                /**< [GG.D2] Ether type = 0x0806*/
     column[0] = "ARP"; /*Type*/
      column[1] = "ARP";    /*L3 Type*/
      column[2] = "-";    /*IPDA*/
      column[3] = "-";   /*Protocol*/
      column[4] = "-";    /*L4 Port*/
      break;  
   default :
       return CTC_E_NONE;
  }
  _sys_greatbelt_l3pdu_get_global_action_of_fixed_l3pdu(lchip,l3pdu_type,&action);
  CTC_DEBUG_OUT(pdu, pdu, PDU_SYS, CTC_DEBUG_LEVEL_DUMP, "%-18s%-14s%-21s%-11s%-14s%-15d%-5s\n",
                      column[0],column[1], column[2],column[3], column[4], action.action_index,action.entry_valid?"True":"False"); 

 
 
  return CTC_E_NONE;
}
int32 _sys_greatbelt_pdu_show_l3pdu_dynamic_cfg(uint8 lchip, uint8 l3pdu_type)
{
    ctc_pdu_l3pdu_key_t l3pdu_key;
    ctc_pdu_global_l3pdu_action_t pdu_action;  
    int32 ret = 0;
    uint8 idx = 0;
    char* column[5] = {0};
    char  str[32] = {0};;
    column[0] = "-"; /*Type*/
    column[1] = "-";    /*L3 Type*/
    column[2] = "-";    /*IPDA*/
    column[3] = "-";   /*Protocol*/
    column[4] = "-";    /*L4 Port*/

     /*max value :MAX_SYS_PDU_L3PDU_BASED_L3HDR_PROTO*/
    for (idx = 0; idx < MAX_SYS_PDU_L3PDU_BASED_L3HDR_PROTO ; idx++)
    {
        ret = sys_greatbelt_l3pdu_get_classified_key( lchip, l3pdu_type, idx, &l3pdu_key);     
        ret = ret ? ret: sys_greatbelt_l3pdu_get_global_action(lchip, l3pdu_type, idx, &pdu_action);
        if (ret != CTC_E_NONE)
        {
            continue;
        }
        switch(l3pdu_type)
        {
        case CTC_PDU_L3PDU_TYPE_L3HDR_PROTO:           
            if (pdu_action.entry_valid)
            {
                sal_sprintf(str, "%d", l3pdu_key.l3hdr_proto);
                column[3] = str;
                column[0] = "User-defined";
            }
            else
            {
                column[0] = "None-defined";
                column[3] = "x";
            }
            break;                                                     
        case CTC_PDU_L3PDU_TYPE_LAYER4_PORT:  
            if (pdu_action.entry_valid)
            {
                sal_sprintf(str, "%d", l3pdu_key.l3pdu_by_port.dest_port);
                column[4] = str;
                column[0] = "User-defined";
            }
            else
            {
                column[0] = "None-defined";
                column[4] = "x";
            }
            break; 
         default :
            return CTC_E_NONE;
        }       
        CTC_DEBUG_OUT(pdu, pdu, PDU_SYS, CTC_DEBUG_LEVEL_DUMP, "%-18s%-14s%-21s%-11s%-14s%-15d%-5s\n",
                      column[0], column[1], column[2], column[3], column[4], pdu_action.action_index, pdu_action.entry_valid?"True":"False"); 
     
    }     
    return CTC_E_NONE;
}

int32
sys_greatbelt_pdu_show_l3pdu_cfg(uint8 lchip)
{

    uint32 index = 0;

    LCHIP_CHECK(lchip);
  
    /*l3pdu based on l3-header proto*/
    SYS_PDU_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "L3pdu:\n");
    SYS_PDU_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Note: for IPv4 address the token \"x\" used as an indication\n");
    SYS_PDU_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "      of a max value 255 that user can configure.\n");
    SYS_PDU_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "      for IPv6 address the token \"x\" means a max hex value 0xF.\n");
    SYS_PDU_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "---------------------------------------------------------------------------------------------------\n");

    CTC_DEBUG_OUT(pdu, pdu, PDU_SYS, CTC_DEBUG_LEVEL_DUMP, "%-18s%-14s%-21s%-11s%-14s%-15s%-5s\n",
                        "Type","Layer3-Type", "IP-DA","Protocol", "Layer4-port","Action-index","Valid"); 

    SYS_PDU_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "---------------------------------------------------------------------------------------------------\n");

    for (index = 0; index < MAX_CTC_PDU_L3PDU_TYPE ; index++)
    {
        if ((CTC_PDU_L3PDU_TYPE_L3HDR_PROTO == index) || (CTC_PDU_L3PDU_TYPE_LAYER3_IPDA == index)
           || (CTC_PDU_L3PDU_TYPE_LAYER4_PORT == index) || (CTC_PDU_L3PDU_TYPE_LAYER4_TYPE == index) )
        {
          continue;
        }
        else
        {
          _sys_greatbelt_pdu_show_l3pdu_static_cfg(lchip,index);
        }
  
    }
    for (index = 0; index < MAX_CTC_PDU_L3PDU_TYPE ; index++)
    {
        if ((CTC_PDU_L3PDU_TYPE_L3HDR_PROTO == index) || (CTC_PDU_L3PDU_TYPE_LAYER3_IPDA == index)
           || (CTC_PDU_L3PDU_TYPE_LAYER4_PORT == index) || (CTC_PDU_L3PDU_TYPE_LAYER4_TYPE == index) )
        {
         _sys_greatbelt_pdu_show_l3pdu_dynamic_cfg(lchip,index);
        }  
    }
    return CTC_E_NONE;
}

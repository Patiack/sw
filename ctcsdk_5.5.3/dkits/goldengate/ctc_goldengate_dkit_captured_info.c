#include "goldengate/include/drv_lib.h"
#include "ctc_cli.h"
#include "ctc_dkit.h"
#include "ctc_goldengate_dkit.h"
#include "ctc_goldengate_dkit_drv.h"
#include "ctc_goldengate_dkit_discard.h"
#include "ctc_goldengate_dkit_discard_type.h"
#include "ctc_goldengate_dkit_path.h"
#include "ctc_goldengate_dkit_captured_info.h"


#define DRV_TCAM_2_BASE        (DRV_TCAM_KEY0_MAX_ENTRY_NUM+DRV_TCAM_KEY1_MAX_ENTRY_NUM)
#define DRV_TCAM_3_BASE        (DRV_TCAM_2_BASE + DRV_TCAM_KEY2_MAX_ENTRY_NUM)
#define DRV_TCAM_4_BASE        (DRV_TCAM_2_BASE+DRV_TCAM_KEY2_MAX_ENTRY_NUM+DRV_TCAM_KEY3_MAX_ENTRY_NUM)
#define DRV_TCAM_5_BASE        (DRV_TCAM_4_BASE+DRV_TCAM_KEY4_MAX_ENTRY_NUM)
#define DRV_TCAM_6_BASE        (DRV_TCAM_5_BASE+DRV_TCAM_KEY5_MAX_ENTRY_NUM)
#define DRV_TCAM_COUPLE_BASE        (DRV_TCAM_6_BASE+DRV_TCAM_KEY6_MAX_ENTRY_NUM)


#define CTC_DKIT_CAPTURED_HASH_HIT "HASH lookup hit"
#define CTC_DKIT_CAPTURED_TCAM_HIT "TCAM lookup hit"
#define CTC_DKIT_CAPTURED_NOT_HIT "Not hit any entry!!!"
#define CTC_DKIT_CAPTURED_CONFLICT "HASH lookup conflict"
#define CTC_DKIT_CAPTURED_DEFAULT "HASH lookup hit default entry"

struct ctc_dkit_captured_path_info_s
{
      uint8 slice_ipe;
      uint8 slice_epe;
      uint8 is_ipe_captured;
      uint8 is_bsr_captured;

      uint8 is_epe_captured;
      uint8 discard;
      uint8 exception;
      uint8 detail;
      uint32 discard_type;
      uint32 exception_index;
      uint32 exception_sub_index;
      uint8 rx_oam;
      uint8 lchip;

};
typedef struct ctc_dkit_captured_path_info_s ctc_dkit_captured_path_info_t;


STATIC int32
_ctc_goldengate_dkit_captured_path_discard_process(ctc_dkit_captured_path_info_t* p_info,
                                        uint32 discard, uint32 discard_type, uint32 module)
{
    if (1 == discard)
    {
        p_info->discard = 1;
        p_info->discard_type = discard_type;

        if (CTC_DKIT_IPE == module)/* ipe */
        {
            CTC_DKIT_PATH_PRINT("%-15s:%s\n", "Drop!!!", ctc_goldengate_dkit_get_reason_desc(discard_type + CTC_DKIT_IPE_USERID_BINDING_DIS));
        }
        else if (CTC_DKIT_EPE == module)
        {
            CTC_DKIT_PATH_PRINT("%-15s:%s\n", "Drop!!!", ctc_goldengate_dkit_get_reason_desc(discard_type + CTC_DKIT_EPE_HDR_ADJ_DESTID_DIS));
        }
    }
    return CLI_SUCCESS;
}

STATIC int32
_ctc_goldengate_dkit_captured_path_exception_process(ctc_dkit_captured_path_info_t* p_info,
                                          uint32 exception, uint32 exception_index, uint32 exception_sub_index)
{
    char* exception_desc[8] =
    {
    "SCL exception",    /* IPEEXCEPTIONINDEX_USERID_EXCEPTION       CBit(3,'h',"0", 1)*/
    "Protocol vlan exception",  /* IPEEXCEPTIONINDEX_PROTOCOL_VLAN_EXCEPTION CBit(3,'h',"1", 1)*/
    "Bridge exception", /* IPEEXCEPTIONINDEX_BRIDGE_EXCEPTION       CBit(3,'h',"2", 1)*/
    "Route exception", /* IPEEXCEPTIONINDEX_ROUTE_EXCEPTION        CBit(3,'h',"3", 1)*/
    "ICMP exception", /* IPEEXCEPTIONINDEX_ICMP_REDIRECT_EXCEPTION CBit(3,'h',"4", 1)*/
    "Learning cache full exception", /* IPEEXCEPTIONINDEX_LEARNING_CACHE_FULL_EXCEPTION CBit(3,'h',"5", 1)*/
    "Mcast RPF check fail exception", /* IPEEXCEPTIONINDEX_ROUTE_MULTICAST_RPFFAILURE_EXCEPTION CBit(3,'h',"6", 1)*/
    "Other exception" /* IPEEXCEPTIONINDEX_OTHER_EXCEPTION        CBit(3,'h',"7", 1)*/
    };

    if (1 == exception)
    {
        p_info->exception = 1;
        p_info->exception_index = exception_index;
        p_info->exception_sub_index = exception_sub_index;

        CTC_DKIT_PATH_PRINT("%-15s:%s!!!\n", "Exception",exception_desc[exception_index]);
        CTC_DKIT_PATH_PRINT("%-15s:%d\n", "Excep index", exception_index);
        CTC_DKIT_PATH_PRINT("%-15s:%d\n", "Excep sub index", exception_sub_index);
    }
    return CLI_SUCCESS;
}

STATIC int32
_ctc_goldengate_dkit_captured_path_ipe_exception_check(ctc_dkit_captured_path_info_t* p_info)
{
    uint8 slice = p_info->slice_ipe;
    uint8 lchip = p_info->lchip;
    uint32 cmd = 0;

    /*DbgIpeForwardingInfo*/
    uint32 fwd_exceptionEn = 0;
    uint32 fwd_exceptionIndex = 0;
    uint32 fwd_exceptionSubIndex = 0;
    DbgIpeForwardingInfo_m dbg_ipe_forwarding_info;


    /*DbgIpeForwardingInfo*/
    sal_memset(&dbg_ipe_forwarding_info, 0, sizeof(dbg_ipe_forwarding_info));
    cmd = DRV_IOR(DbgIpeForwardingInfo_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, slice, cmd, &dbg_ipe_forwarding_info);

    GetDbgIpeForwardingInfo(A, exceptionEn_f, &dbg_ipe_forwarding_info, &fwd_exceptionEn);
    GetDbgIpeForwardingInfo(A, exceptionIndex_f, &dbg_ipe_forwarding_info, &fwd_exceptionIndex);
    GetDbgIpeForwardingInfo(A, exceptionSubIndex_f, &dbg_ipe_forwarding_info, &fwd_exceptionSubIndex);

    _ctc_goldengate_dkit_captured_path_exception_process(p_info, fwd_exceptionEn, fwd_exceptionIndex, fwd_exceptionSubIndex);

    return CLI_SUCCESS;
}

STATIC char*
_ctc_goldengate_dkit_captured_get_igr_scl_key_desc(uint32 key_type)
{
    switch (key_type)
    {
        case USERIDHASHTYPE_DISABLE:
            return "";
        case USERIDHASHTYPE_DOUBLEVLANPORT:
            return "DsUserIdDoubleVlanPortHashKey";
        case USERIDHASHTYPE_SVLANPORT:
            return "DsUserIdSvlanPortHashKey";
        case USERIDHASHTYPE_CVLANPORT:
            return "DsUserIdCvlanPortHashKey";
        case USERIDHASHTYPE_SVLANCOSPORT:
            return "DsUserIdSvlanCosPortHashKey";
        case USERIDHASHTYPE_CVLANCOSPORT:
            return "DsUserIdCvlanCosPortHashKey";
        case USERIDHASHTYPE_MACPORT:
            return "DsUserIdMacPortHashKey";
        case USERIDHASHTYPE_IPV4PORT:
            return "DsUserIdIpv4PortHashKey";
        case USERIDHASHTYPE_MAC:
            return "DsUserIdMacHashKey";
        case USERIDHASHTYPE_IPV4SA:
            return "DsUserIdIpv4SaHashKey";
        case USERIDHASHTYPE_PORT:
            return "DsUserIdPortHashKey";
        case USERIDHASHTYPE_SVLANMACSA:
            return "DsUserIdSvlanMacSaHashKey";
        case USERIDHASHTYPE_SVLAN:
            return "DsUserIdSvlanHashKey";
        case USERIDHASHTYPE_IPV6SA:
            return "DsUserIdIpv6SaHashKey";
        case USERIDHASHTYPE_IPV6PORT:
            return "DsUserIdIpv6PortHashKey";
        case USERIDHASHTYPE_TUNNELIPV4:
            return "DsUserIdTunnelIpv4HashKey";
        case USERIDHASHTYPE_TUNNELIPV4GREKEY:
            return "DsUserIdTunnelIpv4GreKeyHashKey";
        case USERIDHASHTYPE_TUNNELIPV4UDP:
            return "DsUserIdTunnelIpv4UdpHashKey";
        case USERIDHASHTYPE_TUNNELPBB:
            return "DsUserIdTunnelPbbHashKey";
        case USERIDHASHTYPE_TUNNELTRILLUCRPF:
            return "DsUserIdTunnelTrillUcRpfHashKey";
        case USERIDHASHTYPE_TUNNELTRILLUCDECAP:
            return "DsUserIdTunnelTrillUcDecapHashKey";
        case USERIDHASHTYPE_TUNNELTRILLMCRPF:
            return "DsUserIdTunnelTrillMcRpfHashKey";
        case USERIDHASHTYPE_TUNNELTRILLMCDECAP:
            return "DsUserIdTunnelTrillMcDecapHashKey";
        case USERIDHASHTYPE_TUNNELTRILLMCADJ:
            return "DsUserIdTunnelTrillMcAdjHashKey";
        case USERIDHASHTYPE_TUNNELIPV4RPF:
            return "DsUserIdTunnelIpv4RpfHashKey";
        case USERIDHASHTYPE_TUNNELIPV4UCVXLANMODE0:
            return "DsUserIdTunnelIpv4UcVxlanMode0HashKey";
        case USERIDHASHTYPE_TUNNELIPV4UCVXLANMODE1:
            return "DsUserIdTunnelIpv4UcVxlanMode1HashKey";
        case USERIDHASHTYPE_TUNNELIPV6UCVXLANMODE0:
            return "DsUserIdTunnelIpv6UcVxlanMode0HashKey";
        case USERIDHASHTYPE_TUNNELIPV6UCVXLANMODE1:
            return "DsUserIdTunnelIpv6UcVxlanMode1HashKey";
        case USERIDHASHTYPE_TUNNELIPV4UCNVGREMODE0:
            return "DsUserIdTunnelIpv4UcNvgreMode0HashKey";
        case USERIDHASHTYPE_TUNNELIPV4UCNVGREMODE1:
            return "DsUserIdTunnelIpv4UcNvgreMode1HashKey";
        case USERIDHASHTYPE_TUNNELIPV6UCNVGREMODE0:
            return "DsUserIdTunnelIpv6UcNvgreMode0HashKey";
        case USERIDHASHTYPE_TUNNELIPV6UCNVGREMODE1:
            return "DsUserIdTunnelIpv6UcNvgreMode1HashKey";
        case USERIDHASHTYPE_TUNNELIPV4MCVXLANMODE0:
            return "DsUserIdTunnelIpv4McVxlanMode0HashKey";
        case USERIDHASHTYPE_TUNNELIPV4VXLANMODE1:
            return "DsUserIdTunnelIpv4VxlanMode1HashKey";
        case USERIDHASHTYPE_TUNNELIPV6MCVXLANMODE0:
            return "DsUserIdTunnelIpv6McVxlanMode0HashKey";
        case USERIDHASHTYPE_TUNNELIPV6MCVXLANMODE1:
            return "DsUserIdTunnelIpv6McVxlanMode1HashKey";
        case USERIDHASHTYPE_TUNNELIPV4MCNVGREMODE0:
            return "DsUserIdTunnelIpv4McNvgreMode0HashKey";
        case USERIDHASHTYPE_TUNNELIPV4NVGREMODE1:
            return "DsUserIdTunnelIpv4NvgreMode1HashKey";
        case USERIDHASHTYPE_TUNNELIPV6MCNVGREMODE0:
            return "DsUserIdTunnelIpv6McNvgreMode0HashKey";
        case USERIDHASHTYPE_TUNNELIPV6MCNVGREMODE1:
            return "DsUserIdTunnelIpv6McNvgreMode1HashKey";
        case USERIDHASHTYPE_TUNNELIPV4DA:
            return "DsUserIdTunnelIpv4DaHashKey";
        case USERIDHASHTYPE_TUNNELMPLS:
            return "DsUserIdTunnelMplsHashKey";
        case USERIDHASHTYPE_SCLFLOWL2:
            return "DsUserIdSclFlowL2HashKey";
        default:
            return "";
    }
}

STATIC int32
_ctc_goldengate_dkit_captured_is_hit(ctc_dkit_captured_path_info_t* path_info)
{
    uint32 cmd = 0;
    uint32 value = 0;
    DbgIpeForwardingInfo_m dbg_ipe_forwarding_info;
    DbgEpeHdrAdjInfo_m dbg_epe_hdr_adj_info;
    ctc_dkit_captured_path_info_t* p_info = (ctc_dkit_captured_path_info_t*)path_info;
    uint8 lchip = path_info->lchip;

    if (NULL == p_info)
    {
        return CLI_ERROR;
    }

    sal_memset(&dbg_ipe_forwarding_info, 0, sizeof(dbg_ipe_forwarding_info));
    sal_memset(&dbg_epe_hdr_adj_info, 0, sizeof(dbg_epe_hdr_adj_info));

    /*1. IPE*/
    cmd = DRV_IOR(DbgIpeForwardingInfo_t, DbgIpeForwardingInfo_valid_f);
    DRV_IOCTL(lchip, 0, cmd, &value);
    if (value)
    {
        p_info->slice_ipe = 0;
        p_info->is_ipe_captured = 1;
    }
    else
    {
        cmd = DRV_IOR(DbgIpeForwardingInfo_t, DbgIpeForwardingInfo_valid_f);
        DRV_IOCTL(lchip, 1, cmd, &value);
        if (value)
        {
            p_info->slice_ipe = 1;
            p_info->is_ipe_captured = 1;
        }
    }
    if (p_info->is_ipe_captured)
    {

        cmd = DRV_IOR(DbgIpeOamInfo_t, DbgIpeOamInfo_tempRxOamType_f);
        DRV_IOCTL(lchip, p_info->slice_ipe, cmd, &value);
        p_info->rx_oam = 1;
    }

    /*2. EPE*/
    cmd = DRV_IOR(DbgEpeHdrAdjInfo_t, DbgEpeHdrAdjInfo_valid_f);
    DRV_IOCTL(lchip, 0, cmd, &value);
    if (value)
    {
        p_info->slice_epe = 0;
        p_info->is_epe_captured = 1;
    }
    else
    {
        cmd = DRV_IOR(DbgEpeHdrAdjInfo_t, DbgEpeHdrAdjInfo_valid_f);
        DRV_IOCTL(lchip, 1, cmd, &value);
        if (value)
        {
            p_info->slice_epe = 1;
            p_info->is_epe_captured = 1;
        }
    }

    /*3. BSR*/
    cmd = DRV_IOR(DbgFwdBufStoreInfo_t, DbgFwdBufStoreInfo_valid_f);
    DRV_IOCTL(lchip, 0, cmd, &value);
    if (value)
    {
        p_info->is_bsr_captured = 1;
    }

    return CLI_SUCCESS;
}


STATIC int32
_ctc_goldengate_dkit_captured_path_ipe_parser(ctc_dkit_captured_path_info_t* p_info)
{
    return CLI_SUCCESS;
}

STATIC int32
_sys_goldengate_dkit_captured_path_scl_map_key_index(uint8 lchip, uint8 block_id, uint32 key_index)
{
    uint8  step = 0;
    uint32 cmd = 0;
    uint32 scl_key_type = 0;
    uint32 table_id = 0;
    uint32 alloc_bitmap = 0;
    uint32 couple_mode = 0;
    tbl_entry_t tcam_entry;
    DsUserId0MacKey160_m tcam_key;
    DsUserId0MacKey160_m tcam_mask;


    drv_goldengate_get_dynamic_ram_couple_mode(&couple_mode);

    tcam_entry.data_entry = (uint32*)(&tcam_key);
    tcam_entry.mask_entry = (uint32*)(&tcam_mask);


    table_id = DsUserId0MacKey160_t + block_id;

    if (0 == block_id)
    {
        /*IPE SCL 0*/
        cmd = DRV_IOR(FlowTcamLookupCtl_t, FlowTcamLookupCtl_userId0Bitmap_f);
        DRV_IOCTL(lchip, 0, cmd, &alloc_bitmap);

        if (key_index < DRV_TCAM_COUPLE_BASE/2)
        {
            if (key_index < DRV_TCAM_5_BASE/2)
            {
                /*hit tcam 3*/
                key_index = key_index - DRV_TCAM_3_BASE / 2;
            }
            else if (key_index < DRV_TCAM_6_BASE/2)
            {
                /*hit tcam 5*/
                key_index = key_index - DRV_TCAM_5_BASE / 2 +
                                    (IS_BIT_SET(alloc_bitmap, 0)) * (DRV_TCAM_KEY3_MAX_ENTRY_NUM / 2) * (couple_mode+1);
            }
            else
            {
                /*hit tcam 6*/
                key_index = key_index - DRV_TCAM_6_BASE / 2 +
                                    (IS_BIT_SET(alloc_bitmap, 0)) * (DRV_TCAM_KEY3_MAX_ENTRY_NUM / 2) * (couple_mode+1) +
                                    (IS_BIT_SET(alloc_bitmap, 1)) * (DRV_TCAM_KEY5_MAX_ENTRY_NUM / 2) * (couple_mode+1);
            }
        }
        else       /*hit couple slice 1 tcam*/
        {
            key_index = key_index - DRV_TCAM_COUPLE_BASE/2;

            if (key_index < DRV_TCAM_5_BASE/2)
            {
                /*hit tcam 3*/
                key_index = key_index - DRV_TCAM_3_BASE / 2 + DRV_TCAM_KEY3_MAX_ENTRY_NUM / 2;
            }
            else if (key_index < DRV_TCAM_6_BASE/2)
            {
                /*hit tcam 5*/
                key_index = key_index - DRV_TCAM_5_BASE / 2 + DRV_TCAM_KEY5_MAX_ENTRY_NUM / 2 +
                                    (IS_BIT_SET(alloc_bitmap, 0)) * DRV_TCAM_KEY3_MAX_ENTRY_NUM;
            }
            else
            {
                /*hit tcam 6*/
                key_index = key_index - DRV_TCAM_6_BASE / 2 + DRV_TCAM_KEY6_MAX_ENTRY_NUM / 2 +
                                    (IS_BIT_SET(alloc_bitmap, 0)) * DRV_TCAM_KEY3_MAX_ENTRY_NUM +
                                    (IS_BIT_SET(alloc_bitmap, 1)) * DRV_TCAM_KEY5_MAX_ENTRY_NUM;
            }
        }
    }
    else if (1 == block_id)
    {
        /*IPE SCL 1, hit tcam 0 */
        if (key_index >= DRV_TCAM_COUPLE_BASE/2)
        {
            key_index = key_index - DRV_TCAM_COUPLE_BASE/2 + DRV_TCAM_KEY0_MAX_ENTRY_NUM / 2;
        }
    }

    cmd = DRV_IOR(table_id, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, key_index, cmd, &tcam_entry);
    GetDsUserId0MacKey160(A, aclQosKeyType_f, &tcam_key, &scl_key_type);

    switch(scl_key_type)
    {
        case DRV_TCAMKEYTYPE_MACKEY160:
        case DRV_TCAMKEYTYPE_L3KEY160:
            step = 1;
            break;
        case DRV_TCAMKEYTYPE_MACL3KEY320:
            step = 2;
            break;
        case DRV_TCAMKEYTYPE_IPV6KEY640:
            step = 4;
            break;
        default:
            step = 1;
            break;
    }

    return (key_index/step);
}


STATIC int32
_ctc_goldengate_dkit_captured_path_ipe_scl(ctc_dkit_captured_path_info_t* p_info)
{
    uint8 slice = p_info->slice_ipe;
    uint8 lchip = p_info->lchip;
    uint32 cmd = 0;
    uint32 key_index = 0;
    char* acl_key_type_desc[4] = {"TCAML2KEY", "TCAML2L3KEY", "TCAML3KEY", "TCAMVLANKEY"};

    /*DbgIpeUserIdInfo*/
    uint32 scl_exceptionEn = 0;
    uint32 scl_exceptionIndex = 0;
    uint32 scl_exceptionSubIndex = 0;
    uint32 scl_localPhyPort = 0;
    uint32 scl_discard = 0;
    uint32 scl_discardType = 0;
    uint32 scl_bypassAll = 0;
    uint32 scl_dsFwdPtrValid = 0;
    uint32 scl_dsFwdPtr = 0;
    uint32 scl_userIdHash1Type = 0;
    uint32 scl_userIdHash2Type = 0;
    uint32 scl_flowFieldSel = 0;
    uint32 scl_sclKeyType1 = 0;
    uint32 scl_sclKeyType2 = 0;
    uint32 scl_userIdTcam1En = 0;
    uint32 scl_userIdTcam2En = 0;
    uint32 scl_userIdTcam1Type = 0;
    uint32 scl_userIdTcam2Type = 0;
    uint32 scl_sclFlowHashEn = 0;
    uint32 scl_valid = 0;
    DbgIpeUserIdInfo_m dbg_ipe_user_id_info;
    /*DbgUserIdHashEngineForUserId0Info*/
    uint32 hash0_lookupResultValid = 0;
    uint32 hash0_defaultEntryValid = 0;
    uint32 hash0_keyIndex = 0;
    uint32 hash0_adIndex = 0;
    uint32 hash0_hashConflict = 0;
    uint32 hash0_lookupResultValid0 = 0;
    uint32 hash0_keyIndex0 = 0;
    uint32 hash0_lookupResultValid1 = 0;
    uint32 hash0_keyIndex1 = 0;
    uint32 hash0_camLookupResultValid = 0;
    uint32 hash0_keyIndexCam = 0;
    uint32 hash0_valid = 0;
    DbgUserIdHashEngineForUserId0Info_m dbg_user_id_hash_engine_for_user_id0_info;
    /*DbgUserIdHashEngineForUserId1Info*/
    uint32 hash1_lookupResultValid = 0;
    uint32 hash1_defaultEntryValid = 0;
    uint32 hash1_keyIndex = 0;
    uint32 hash1_adIndex = 0;
    uint32 hash1_hashConflict = 0;
    uint32 hash1_lookupResultValid0 = 0;
    uint32 hash1_keyIndex0 = 0;
    uint32 hash1_lookupResultValid1 = 0;
    uint32 hash1_keyIndex1 = 0;
    uint32 hash1_camLookupResultValid = 0;
    uint32 hash1_keyIndexCam = 0;
    uint32 hash1_valid = 0;
    DbgUserIdHashEngineForUserId1Info_m dbg_user_id_hash_engine_for_user_id1_info;
    /*DbgFlowTcamEngineUserIdInfo*/
    uint32 tcam_userId0TcamResultValid = 0;
    uint32 tcam_userId0TcamIndex = 0;
    uint32 tcam_userId1TcamResultValid = 0;
    uint32 tcam_userId1TcamIndex = 0;
    uint32 tcam_valid = 0;
    DbgFlowTcamEngineUserIdInfo_m dbg_flow_tcam_engine_user_id_info;

    /*DbgIpeUserIdInfo*/
    sal_memset(&dbg_ipe_user_id_info, 0, sizeof(dbg_ipe_user_id_info));
    cmd = DRV_IOR(DbgIpeUserIdInfo_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, slice, cmd, &dbg_ipe_user_id_info);
    GetDbgIpeUserIdInfo(A, localPhyPort_f, &dbg_ipe_user_id_info, &scl_localPhyPort);
    GetDbgIpeUserIdInfo(A, exceptionEn_f, &dbg_ipe_user_id_info, &scl_exceptionEn);
    GetDbgIpeUserIdInfo(A, exceptionIndex_f, &dbg_ipe_user_id_info, &scl_exceptionIndex);
    GetDbgIpeUserIdInfo(A, exceptionSubIndex_f, &dbg_ipe_user_id_info, &scl_exceptionSubIndex);
    GetDbgIpeUserIdInfo(A, discard_f, &dbg_ipe_user_id_info, &scl_discard);
    GetDbgIpeUserIdInfo(A, discardType_f, &dbg_ipe_user_id_info, &scl_discardType);
    GetDbgIpeUserIdInfo(A, bypassAll_f, &dbg_ipe_user_id_info, &scl_bypassAll);
    GetDbgIpeUserIdInfo(A, dsFwdPtrValid_f, &dbg_ipe_user_id_info, &scl_dsFwdPtrValid);
    GetDbgIpeUserIdInfo(A, dsFwdPtr_f, &dbg_ipe_user_id_info, &scl_dsFwdPtr);
    GetDbgIpeUserIdInfo(A, userIdHash1Type_f, &dbg_ipe_user_id_info, &scl_userIdHash1Type);
    GetDbgIpeUserIdInfo(A, userIdHash2Type_f, &dbg_ipe_user_id_info, &scl_userIdHash2Type);
    GetDbgIpeUserIdInfo(A, flowFieldSel_f, &dbg_ipe_user_id_info, &scl_flowFieldSel);
    GetDbgIpeUserIdInfo(A, sclKeyType1_f, &dbg_ipe_user_id_info, &scl_sclKeyType1);
    GetDbgIpeUserIdInfo(A, sclKeyType2_f, &dbg_ipe_user_id_info, &scl_sclKeyType2);
    GetDbgIpeUserIdInfo(A, userIdTcam1En_f, &dbg_ipe_user_id_info, &scl_userIdTcam1En);
    GetDbgIpeUserIdInfo(A, userIdTcam2En_f, &dbg_ipe_user_id_info, &scl_userIdTcam2En);
    GetDbgIpeUserIdInfo(A, userIdTcam1Type_f, &dbg_ipe_user_id_info, &scl_userIdTcam1Type);
    GetDbgIpeUserIdInfo(A, userIdTcam2Type_f, &dbg_ipe_user_id_info, &scl_userIdTcam2Type);
    GetDbgIpeUserIdInfo(A, sclFlowHashEn_f, &dbg_ipe_user_id_info, &scl_sclFlowHashEn);
    GetDbgIpeUserIdInfo(A, valid_f, &dbg_ipe_user_id_info, &scl_valid);
    /*DbgUserIdHashEngineForUserId0Info*/
    sal_memset(&dbg_user_id_hash_engine_for_user_id0_info, 0, sizeof(dbg_user_id_hash_engine_for_user_id0_info));
    cmd = DRV_IOR(DbgUserIdHashEngineForUserId0Info_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, slice, cmd, &dbg_user_id_hash_engine_for_user_id0_info);
    GetDbgUserIdHashEngineForUserId0Info(A, lookupResultValid_f, &dbg_user_id_hash_engine_for_user_id0_info, &hash0_lookupResultValid);
    GetDbgUserIdHashEngineForUserId0Info(A, defaultEntryValid_f, &dbg_user_id_hash_engine_for_user_id0_info, &hash0_defaultEntryValid);
    GetDbgUserIdHashEngineForUserId0Info(A, keyIndex_f, &dbg_user_id_hash_engine_for_user_id0_info, &hash0_keyIndex);
    GetDbgUserIdHashEngineForUserId0Info(A, adIndex_f, &dbg_user_id_hash_engine_for_user_id0_info, &hash0_adIndex);
    GetDbgUserIdHashEngineForUserId0Info(A, hashConflict_f, &dbg_user_id_hash_engine_for_user_id0_info, &hash0_hashConflict);
    GetDbgUserIdHashEngineForUserId0Info(A, lookupResultValid0_f, &dbg_user_id_hash_engine_for_user_id0_info, &hash0_lookupResultValid0);
    GetDbgUserIdHashEngineForUserId0Info(A, keyIndex0_f, &dbg_user_id_hash_engine_for_user_id0_info, &hash0_keyIndex0);
    GetDbgUserIdHashEngineForUserId0Info(A, lookupResultValid1_f, &dbg_user_id_hash_engine_for_user_id0_info, &hash0_lookupResultValid1);
    GetDbgUserIdHashEngineForUserId0Info(A, keyIndex1_f, &dbg_user_id_hash_engine_for_user_id0_info, &hash0_keyIndex1);
    GetDbgUserIdHashEngineForUserId0Info(A, camLookupResultValid_f, &dbg_user_id_hash_engine_for_user_id0_info, &hash0_camLookupResultValid);
    GetDbgUserIdHashEngineForUserId0Info(A, keyIndexCam_f, &dbg_user_id_hash_engine_for_user_id0_info, &hash0_keyIndexCam);
    GetDbgUserIdHashEngineForUserId0Info(A, valid_f, &dbg_user_id_hash_engine_for_user_id0_info, &hash0_valid);
    /*DbgUserIdHashEngineForUserId1Info*/
    sal_memset(&dbg_user_id_hash_engine_for_user_id1_info, 0, sizeof(dbg_user_id_hash_engine_for_user_id1_info));
    cmd = DRV_IOR(DbgUserIdHashEngineForUserId1Info_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, slice, cmd, &dbg_user_id_hash_engine_for_user_id1_info);
    GetDbgUserIdHashEngineForUserId1Info(A, lookupResultValid_f, &dbg_user_id_hash_engine_for_user_id1_info, &hash1_lookupResultValid);
    GetDbgUserIdHashEngineForUserId1Info(A, defaultEntryValid_f, &dbg_user_id_hash_engine_for_user_id1_info, &hash1_defaultEntryValid);
    GetDbgUserIdHashEngineForUserId1Info(A, keyIndex_f, &dbg_user_id_hash_engine_for_user_id1_info, &hash1_keyIndex);
    GetDbgUserIdHashEngineForUserId1Info(A, adIndex_f, &dbg_user_id_hash_engine_for_user_id1_info, &hash1_adIndex);
    GetDbgUserIdHashEngineForUserId1Info(A, hashConflict_f, &dbg_user_id_hash_engine_for_user_id1_info, &hash1_hashConflict);
    GetDbgUserIdHashEngineForUserId1Info(A, lookupResultValid0_f, &dbg_user_id_hash_engine_for_user_id1_info, &hash1_lookupResultValid0);
    GetDbgUserIdHashEngineForUserId1Info(A, keyIndex0_f, &dbg_user_id_hash_engine_for_user_id1_info, &hash1_keyIndex0);
    GetDbgUserIdHashEngineForUserId1Info(A, lookupResultValid1_f, &dbg_user_id_hash_engine_for_user_id1_info, &hash1_lookupResultValid1);
    GetDbgUserIdHashEngineForUserId1Info(A, keyIndex1_f, &dbg_user_id_hash_engine_for_user_id1_info, &hash1_keyIndex1);
    GetDbgUserIdHashEngineForUserId1Info(A, camLookupResultValid_f, &dbg_user_id_hash_engine_for_user_id1_info, &hash1_camLookupResultValid);
    GetDbgUserIdHashEngineForUserId1Info(A, keyIndexCam_f, &dbg_user_id_hash_engine_for_user_id1_info, &hash1_keyIndexCam);
    GetDbgUserIdHashEngineForUserId1Info(A, valid_f, &dbg_user_id_hash_engine_for_user_id1_info, &hash1_valid);
    /*DbgFlowTcamEngineUserIdInfo*/
    sal_memset(&dbg_flow_tcam_engine_user_id_info, 0, sizeof(dbg_flow_tcam_engine_user_id_info));
    cmd = DRV_IOR(DbgFlowTcamEngineUserIdInfo_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &dbg_flow_tcam_engine_user_id_info);
    GetDbgFlowTcamEngineUserIdInfo(A, userId0TcamResultValid_f, &dbg_flow_tcam_engine_user_id_info, &tcam_userId0TcamResultValid);
    GetDbgFlowTcamEngineUserIdInfo(A, userId0TcamIndex_f, &dbg_flow_tcam_engine_user_id_info, &tcam_userId0TcamIndex);
    GetDbgFlowTcamEngineUserIdInfo(A, userId1TcamResultValid_f, &dbg_flow_tcam_engine_user_id_info, &tcam_userId1TcamResultValid);
    GetDbgFlowTcamEngineUserIdInfo(A, userId1TcamIndex_f, &dbg_flow_tcam_engine_user_id_info, &tcam_userId1TcamIndex);
    GetDbgFlowTcamEngineUserIdInfo(A, valid_f, &dbg_flow_tcam_engine_user_id_info, &tcam_valid);

    if (0 == p_info->detail)
    {
        goto Discard;
    }

    if(scl_valid)
    {
        if (scl_userIdHash1Type || scl_userIdHash2Type || scl_userIdTcam1En || scl_userIdTcam2En)
        {
            CTC_DKIT_PRINT("SCL Process:\n");
            /*hash0*/
            if (scl_userIdHash1Type && hash0_valid)
            {
                CTC_DKIT_PATH_PRINT("%-15s\n", "HASH0 lookup--->");
                if (hash0_hashConflict)
                {
                    CTC_DKIT_PATH_PRINT("%-15s:%s\n", "result", CTC_DKIT_CAPTURED_CONFLICT);
                    CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "key index", hash0_keyIndex);
                    CTC_DKIT_PATH_PRINT("%-15s:%s\n", "key name", _ctc_goldengate_dkit_captured_get_igr_scl_key_desc(scl_userIdHash1Type));
                }
                else if (hash0_defaultEntryValid)
                {
                    CTC_DKIT_PATH_PRINT("%-15s:%s\n", "result", CTC_DKIT_CAPTURED_DEFAULT);
                }
                else if (hash0_lookupResultValid)
                {
                    CTC_DKIT_PATH_PRINT("%-15s:%s\n", "result", CTC_DKIT_CAPTURED_HASH_HIT);
                }
                else
                {
                    CTC_DKIT_PATH_PRINT("%-15s:%s\n", "result", CTC_DKIT_CAPTURED_NOT_HIT);
                }
                if (hash0_defaultEntryValid || hash0_lookupResultValid)
                {
                    CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "key index", hash0_keyIndex);
                    CTC_DKIT_PATH_PRINT("%-15s:%s\n", "key name", _ctc_goldengate_dkit_captured_get_igr_scl_key_desc(scl_userIdHash1Type));
                    CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "AD index", hash0_adIndex/2);
                    if (scl_sclFlowHashEn)
                    {
                        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "AD name", "DsSclFlow");
                    }
                    else if (scl_userIdHash1Type && (scl_userIdHash1Type < USERIDHASHTYPE_TUNNELIPV4))
                    {
                        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "AD name", "DsUserId");
                    }
                    else if (scl_userIdHash1Type >= USERIDHASHTYPE_TUNNELIPV4)
                    {
                        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "AD name", "DsTunnelId");
                    }
                }
            }
            /*hash1*/
            if (scl_userIdHash2Type && hash1_valid)
            {
                CTC_DKIT_PATH_PRINT("%-15s\n", "HASH1 lookup--->");
                if (hash1_hashConflict)
                {
                    CTC_DKIT_PATH_PRINT("%-15s:%s\n", "result", CTC_DKIT_CAPTURED_CONFLICT);
                    CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "key index", hash1_keyIndex);
                    CTC_DKIT_PATH_PRINT("%-15s:%s\n", "key name", _ctc_goldengate_dkit_captured_get_igr_scl_key_desc(scl_userIdHash2Type));
                }
                else if (hash1_defaultEntryValid)
                {
                    CTC_DKIT_PATH_PRINT("%-15s:%s\n", "result", CTC_DKIT_CAPTURED_DEFAULT);
                }
                else if (hash1_lookupResultValid)
                {
                    CTC_DKIT_PATH_PRINT("%-15s:%s\n", "result", CTC_DKIT_CAPTURED_HASH_HIT);
                }
                else
                {
                    CTC_DKIT_PATH_PRINT("%-15s:%s\n", "result", CTC_DKIT_CAPTURED_NOT_HIT);
                }
                if (hash1_defaultEntryValid || hash1_lookupResultValid)
                {
                    CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "key index", hash1_keyIndex);
                    CTC_DKIT_PATH_PRINT("%-15s:%s\n", "key name", _ctc_goldengate_dkit_captured_get_igr_scl_key_desc(scl_userIdHash2Type));
                    CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "AD index", hash1_adIndex/2);
                    if (scl_userIdHash2Type && (scl_userIdHash2Type < USERIDHASHTYPE_TUNNELIPV4))
                    {
                        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "AD name", "DsUserId");
                    }
                    else if (scl_userIdHash2Type >= USERIDHASHTYPE_TUNNELIPV4)
                    {
                        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "AD name", "DsTunnelId");
                    }
                }
            }
            /*tcam0*/
            if (scl_userIdTcam1En && tcam_valid)
            {
                CTC_DKIT_PATH_PRINT("%-15s\n", "TCAM0 lookup--->");
                CTC_DKIT_PATH_PRINT("%-15s:%s\n", "key type", acl_key_type_desc[scl_userIdTcam1Type]);
                if (tcam_userId0TcamResultValid)
                {
                    CTC_DKIT_PATH_PRINT("%-15s:%s\n", "result", CTC_DKIT_CAPTURED_TCAM_HIT);
                    key_index = _sys_goldengate_dkit_captured_path_scl_map_key_index(lchip, 0, tcam_userId0TcamIndex);
                    CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "key index", key_index);
                }
                else
                {
                    CTC_DKIT_PATH_PRINT("%-15s:%s\n", "result", CTC_DKIT_CAPTURED_NOT_HIT);
                }
            }
            /*tcam1*/
            if (scl_userIdTcam2En && tcam_valid)
            {
                CTC_DKIT_PATH_PRINT("%-15s\n", "TCAM1 lookup--->");
                CTC_DKIT_PATH_PRINT("%-15s:%s\n", "key type", acl_key_type_desc[scl_userIdTcam2Type]);
                if (tcam_userId1TcamResultValid)
                {
                    CTC_DKIT_PATH_PRINT("%-15s:%s\n", "result", CTC_DKIT_CAPTURED_TCAM_HIT);
                    key_index = _sys_goldengate_dkit_captured_path_scl_map_key_index(lchip, 1, tcam_userId0TcamIndex);
                    CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "key index", key_index);
                }
                else
                {
                    CTC_DKIT_PATH_PRINT("%-15s:%s\n", "result", CTC_DKIT_CAPTURED_NOT_HIT);
                }
            }
        }
    }

Discard:
    _ctc_goldengate_dkit_captured_path_discard_process(p_info, scl_discard, scl_discardType, CTC_DKIT_IPE);
    _ctc_goldengate_dkit_captured_path_exception_process(p_info, scl_exceptionEn, scl_exceptionIndex, scl_exceptionSubIndex);

    return CLI_SUCCESS;
}

STATIC int32
_ctc_goldengate_dkit_captured_path_ipe_interface(ctc_dkit_captured_path_info_t* p_info)
{
    uint8 slice = p_info->slice_ipe;
    uint8 lchip = p_info->lchip;
    uint32 cmd = 0;
    uint16 gport = 0;
    uint8 gchip = 0;
    uint32 value = 0;
    uint32 bridge_en = 0;
    uint32 route_en = 0;
    uint32 mpls_en = 0;
    char* vlan_action[4] = {"None", "Replace", "Add","Delete"};

    /* DbgIpeUserIdInfo */
    uint32 localPhyPort = 0;
    uint32 globalSrcPort = 0;
    uint32 outerVlanIsCVlan = 0;
    /* DbgIpeIntfMapperInfo */
    uint32 vlanId = 0;
    uint32 isLoop = 0;
    uint32 isPortMac = 0;
    uint32 sourceCos = 0;
    uint32 sourceCfi = 0;
    uint32 ctagCos = 0;
    uint32 ctagCfi = 0;
    uint32 svlanTagOperationValid = 0;
    uint32 cvlanTagOperationValid = 0;
    uint32 classifyStagCos = 0;
    uint32 classifyStagCfi = 0;
    uint32 classifyCtagCos = 0;
    uint32 classifyCtagCfi = 0;
    uint32 vlanPtr = 0;
    uint32 interfaceId = 0;
    uint32 isRouterMac = 0;
    uint32 discard = 0;
    uint32 discardType = 0;
    uint32 bypassAll = 0;
    uint32 exceptionEn = 0;
    uint32 exceptionIndex = 0;
    uint32 exceptionSubIndex = 0;
    uint32 dsFwdPtrValid = 0;
    uint32 dsFwdPtr = 0;
    uint32 protocolVlanEn = 0;
    uint32 tempBypassAll = 0;
    uint32 routeAllPacket = 0;
    uint32 valid = 0;
    DbgIpeUserIdInfo_m dbg_ipe_user_id_info;
    DbgIpeIntfMapperInfo_m dbg_ipe_intf_mapper_info;

    CTC_DKIT_PRINT("Interface Process:\n");

    sal_memset(&dbg_ipe_user_id_info, 0, sizeof(dbg_ipe_user_id_info));
    cmd = DRV_IOR(DbgIpeUserIdInfo_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, slice, cmd, &dbg_ipe_user_id_info);
    GetDbgIpeUserIdInfo(A, localPhyPort_f, &dbg_ipe_user_id_info, &localPhyPort);
    GetDbgIpeUserIdInfo(A, globalSrcPort_f, &dbg_ipe_user_id_info, &globalSrcPort);
    GetDbgIpeUserIdInfo(A, outerVlanIsCVlan_f, &dbg_ipe_user_id_info, &outerVlanIsCVlan);
    GetDbgIpeUserIdInfo(A, discard_f, &dbg_ipe_user_id_info, &discard);
    GetDbgIpeUserIdInfo(A, discardType_f, &dbg_ipe_user_id_info, &discardType);
    GetDbgIpeUserIdInfo(A, exceptionEn_f, &dbg_ipe_user_id_info, &exceptionEn);
    GetDbgIpeUserIdInfo(A, exceptionIndex_f, &dbg_ipe_user_id_info, &exceptionIndex);
    GetDbgIpeUserIdInfo(A, exceptionSubIndex_f, &dbg_ipe_user_id_info, &exceptionSubIndex);
    GetDbgIpeUserIdInfo(A, valid_f, &dbg_ipe_user_id_info, &valid);

    if (valid)
    {
        if (CTC_DKIT_DRV_GPORT_IS_LINKAGG_PORT(globalSrcPort))
        {

            CTC_DKIT_PATH_PRINT("%-15s:%d\n", "linkagg group", CTC_DKIT_DRV_GPORT_TO_LINKAGG_ID(globalSrcPort));
        }
        CTC_DKIT_GET_GCHIP(lchip, gchip);
        gport = CTC_DKIT_DRV_LPORT_TO_CTC_GPORT(gchip, (localPhyPort + slice*CTC_DKIT_ONE_SLICE_PORT_NUM));
        CTC_DKIT_PATH_PRINT("%-15s:0x%04x\n", "source gport", gport);
    }

    sal_memset(&dbg_ipe_intf_mapper_info, 0, sizeof(dbg_ipe_intf_mapper_info));
    cmd = DRV_IOR(DbgIpeIntfMapperInfo_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, slice, cmd, &dbg_ipe_intf_mapper_info);
    GetDbgIpeIntfMapperInfo(A, vlanId_f, &dbg_ipe_intf_mapper_info, &vlanId);
    GetDbgIpeIntfMapperInfo(A, isLoop_f, &dbg_ipe_intf_mapper_info, &isLoop);
    GetDbgIpeIntfMapperInfo(A, isPortMac_f, &dbg_ipe_intf_mapper_info, &isPortMac);
    GetDbgIpeIntfMapperInfo(A, sourceCos_f, &dbg_ipe_intf_mapper_info, &sourceCos);
    GetDbgIpeIntfMapperInfo(A, sourceCfi_f, &dbg_ipe_intf_mapper_info, &sourceCfi);
    GetDbgIpeIntfMapperInfo(A, ctagCos_f, &dbg_ipe_intf_mapper_info, &ctagCos);
    GetDbgIpeIntfMapperInfo(A, ctagCfi_f, &dbg_ipe_intf_mapper_info, &ctagCfi);
    GetDbgIpeIntfMapperInfo(A, svlanTagOperationValid_f, &dbg_ipe_intf_mapper_info, &svlanTagOperationValid);
    GetDbgIpeIntfMapperInfo(A, cvlanTagOperationValid_f, &dbg_ipe_intf_mapper_info, &cvlanTagOperationValid);
    GetDbgIpeIntfMapperInfo(A, classifyStagCos_f, &dbg_ipe_intf_mapper_info, &classifyStagCos);
    GetDbgIpeIntfMapperInfo(A, classifyStagCfi_f, &dbg_ipe_intf_mapper_info, &classifyStagCfi);
    GetDbgIpeIntfMapperInfo(A, classifyCtagCos_f, &dbg_ipe_intf_mapper_info, &classifyCtagCos);
    GetDbgIpeIntfMapperInfo(A, classifyCtagCfi_f, &dbg_ipe_intf_mapper_info, &classifyCtagCfi);
    GetDbgIpeIntfMapperInfo(A, vlanPtr_f, &dbg_ipe_intf_mapper_info, &vlanPtr);
    GetDbgIpeIntfMapperInfo(A, interfaceId_f, &dbg_ipe_intf_mapper_info, &interfaceId);
    GetDbgIpeIntfMapperInfo(A, isRouterMac_f, &dbg_ipe_intf_mapper_info, &isRouterMac);
    GetDbgIpeIntfMapperInfo(A, discard_f, &dbg_ipe_intf_mapper_info, &discard);
    GetDbgIpeIntfMapperInfo(A, discardType_f, &dbg_ipe_intf_mapper_info, &discardType);
    GetDbgIpeIntfMapperInfo(A, bypassAll_f, &dbg_ipe_intf_mapper_info, &bypassAll);
    GetDbgIpeIntfMapperInfo(A, exceptionEn_f, &dbg_ipe_intf_mapper_info, &exceptionEn);
    GetDbgIpeIntfMapperInfo(A, exceptionIndex_f, &dbg_ipe_intf_mapper_info, &exceptionIndex);
    GetDbgIpeIntfMapperInfo(A, exceptionSubIndex_f, &dbg_ipe_intf_mapper_info, &exceptionSubIndex);
    GetDbgIpeIntfMapperInfo(A, dsFwdPtrValid_f, &dbg_ipe_intf_mapper_info, &dsFwdPtrValid);
    GetDbgIpeIntfMapperInfo(A, dsFwdPtr_f, &dbg_ipe_intf_mapper_info, &dsFwdPtr);
    GetDbgIpeIntfMapperInfo(A, protocolVlanEn_f, &dbg_ipe_intf_mapper_info, &protocolVlanEn);
    GetDbgIpeIntfMapperInfo(A, tempBypassAll_f, &dbg_ipe_intf_mapper_info, &tempBypassAll);
    GetDbgIpeIntfMapperInfo(A, routeAllPacket_f, &dbg_ipe_intf_mapper_info, &routeAllPacket);
    GetDbgIpeIntfMapperInfo(A, valid_f, &dbg_ipe_intf_mapper_info, &valid);
    if (valid)
    {
        if (vlanPtr < 4096)
        {
            CTC_DKIT_PATH_PRINT("%-15s:%d\n", "vlan ptr", vlanPtr);
            CTC_DKIT_PATH_PRINT("%-15s:%s\n", "vlan domain", outerVlanIsCVlan?"cvlan domain":"svlan domain");
        }
        if (interfaceId)
        {
            CTC_DKIT_PATH_PRINT("%-15s:%d\n", "l3 intf id", interfaceId);
            cmd = DRV_IOR(DsSrcInterface_t, DsSrcInterface_vrfId_f);
            DRV_IOCTL(lchip, interfaceId, cmd, &value);
            CTC_DKIT_PATH_PRINT("%-15s:%d\n", "intf vrfid", value);
            CTC_DKIT_PATH_PRINT("%-15s:%s\n", "route mac", isRouterMac?"YES":"NO");
        }
    }
    /*bridge en*/
    cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_bridgeEn_f);
    DRV_IOCTL(lchip, (localPhyPort + slice*CTC_DKIT_ONE_SLICE_PORT_NUM), cmd, &bridge_en);
    if (vlanPtr < 4096)
    {
        cmd = DRV_IOR(DsVlan_t, DsVlan_bridgeDisable_f);
        DRV_IOCTL(lchip, vlanPtr, cmd, &value);
    }
    bridge_en = bridge_en && (!value);
    CTC_DKIT_PATH_PRINT("%-15s:%s\n", "bridge port", bridge_en?"YES":"NO");
    /*route en*/
    cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_routedPort_f);
    DRV_IOCTL(lchip, (localPhyPort + slice*CTC_DKIT_ONE_SLICE_PORT_NUM), cmd, &route_en);
    CTC_DKIT_PATH_PRINT("%-15s:%s\n", "route port", route_en?"YES":"NO");

    /*mpls en*/
    if (interfaceId)
    {
        cmd = DRV_IOR(DsSrcInterface_t, DsSrcInterface_mplsEn_f);
        DRV_IOCTL(lchip, interfaceId, cmd, &mpls_en);
        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "MPLS port", mpls_en? "YES" : "NO");
    }

    if(p_info->is_bsr_captured)
    {
        /*DbgFwdBufStoreInfo*/
        uint32 valid = 0;
        uint32 operationType = 0;
        uint32 svlanTagOperationValid = 0;
        uint32 sTagAction = 0;
        uint32 sourceCos = 0;
        uint32 sourceCfi = 0;
        uint32 svlanTpidIndex = 0;
        uint32 srcVlanId = 0;
        uint32 cvlanTagOperationValid = 0;
        uint32 cTagAction = 0;
        uint32 srcCtagCos = 0;
        uint32 srcCtagCfi = 0;
        uint32 srcCvlanId = 0;

        uint32 svlanTpid = 0;
        uint32 cvlanTagTpid = 0;

        DbgFwdBufStoreInfo_m dbg_fwd_buf_store_info;
        sal_memset(&dbg_fwd_buf_store_info, 0, sizeof(dbg_fwd_buf_store_info));
        cmd = DRV_IOR(DbgFwdBufStoreInfo_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_fwd_buf_store_info);
        GetDbgFwdBufStoreInfo(A, valid_f, &dbg_fwd_buf_store_info, &valid);
        GetDbgFwdBufStoreInfo(A, operationType_f, &dbg_fwd_buf_store_info, &operationType);
        GetDbgFwdBufStoreInfo(A, svlanTagOperationValid_f, &dbg_fwd_buf_store_info, &svlanTagOperationValid);
        GetDbgFwdBufStoreInfo(A, stagAction_f, &dbg_fwd_buf_store_info, &sTagAction);
        GetDbgFwdBufStoreInfo(A, sourceCos_f, &dbg_fwd_buf_store_info, &sourceCos);
        GetDbgFwdBufStoreInfo(A, sourceCfi_f, &dbg_fwd_buf_store_info, &sourceCfi);
        GetDbgFwdBufStoreInfo(A, svlanTpidIndex_f, &dbg_fwd_buf_store_info, &svlanTpidIndex);
        GetDbgFwdBufStoreInfo(A, srcVlanId_f, &dbg_fwd_buf_store_info, &srcVlanId);
        GetDbgFwdBufStoreInfo(A, u1_normal_cvlanTagOperationValid_f, &dbg_fwd_buf_store_info, &cvlanTagOperationValid);
        GetDbgFwdBufStoreInfo(A, u1_normal_cTagAction_f, &dbg_fwd_buf_store_info, &cTagAction);
        GetDbgFwdBufStoreInfo(A, u1_normal_srcCtagCos_f, &dbg_fwd_buf_store_info, &srcCtagCos);
        GetDbgFwdBufStoreInfo(A, u1_normal_srcCtagCfi_f, &dbg_fwd_buf_store_info, &srcCtagCfi);
        GetDbgFwdBufStoreInfo(A, u1_normal_srcCvlanId_f, &dbg_fwd_buf_store_info, &srcCvlanId);

        if (valid)
        {

             if ((svlanTagOperationValid && sTagAction) || (cvlanTagOperationValid && cTagAction&&(OPERATIONTYPE_NORMAL == operationType)))
             {
                 CTC_DKIT_PATH_PRINT("%-15s\n", "vlan edit--->");
             }
            if (svlanTagOperationValid)
            {
                uint32 field_id[4] = {EpeHdrAdjustCtl_array_0_svlanTagTpid_f,EpeHdrAdjustCtl_array_1_svlanTagTpid_f,
                                                       EpeHdrAdjustCtl_array_2_svlanTagTpid_f,EpeHdrAdjustCtl_array_3_svlanTagTpid_f};
                cmd = DRV_IOR(EpeHdrAdjustCtl_t, field_id[svlanTpidIndex]);
                DRV_IOCTL(lchip, 0, cmd, &svlanTpid);
                CTC_DKIT_PATH_PRINT("%-15s:%s\n", "svlan edit", vlan_action[sTagAction]);
                if ((VTAGACTIONTYPE_MODIFY == sTagAction) || (VTAGACTIONTYPE_ADD == sTagAction))
                {
                    CTC_DKIT_PATH_PRINT("%-15s:0x%04x\n", "new TPID", svlanTpid);
                    CTC_DKIT_PATH_PRINT("%-15s:%d\n", "new COS", sourceCos);
                    CTC_DKIT_PATH_PRINT("%-15s:%d\n", "new CFI", sourceCfi);
                    CTC_DKIT_PATH_PRINT("%-15s:%d\n", "new vlan id", srcVlanId);
                }
            }
            if((cvlanTagOperationValid)&&(OPERATIONTYPE_NORMAL == operationType))
            {
                cmd = DRV_IOR(EpeHdrAdjustCtl_t, EpeHdrAdjustCtl_cvlanTagTpid_f);
                DRV_IOCTL(lchip, 0, cmd, &cvlanTagTpid);
                CTC_DKIT_PATH_PRINT("%-15s:%s\n", "cvlan edit", vlan_action[cTagAction]);
                if ((VTAGACTIONTYPE_MODIFY == cTagAction) || (VTAGACTIONTYPE_ADD == cTagAction))
                {
                    CTC_DKIT_PATH_PRINT("%-15s:0x%04x\n", "new TPID", cvlanTagTpid);
                    CTC_DKIT_PATH_PRINT("%-15s:%d\n", "new COS", srcCtagCos);
                    CTC_DKIT_PATH_PRINT("%-15s:%d\n", "new CFI", srcCtagCfi);
                    CTC_DKIT_PATH_PRINT("%-15s:%d\n", "new vlan id", srcCvlanId);
                }
            }
        }
    }

    _ctc_goldengate_dkit_captured_path_discard_process(p_info, discard, discardType, CTC_DKIT_IPE);
    _ctc_goldengate_dkit_captured_path_exception_process(p_info, exceptionEn, exceptionIndex, exceptionSubIndex);

    return CLI_SUCCESS;
}

STATIC int32
_ctc_goldengate_dkit_captured_path_ipe_tunnel(ctc_dkit_captured_path_info_t* p_info)
{
    uint8 slice = p_info->slice_ipe;
    uint8 lchip = p_info->lchip;
    uint32 cmd = 0;
    /*DbgUserIdHashEngineForMpls0Info*/
    uint32 mpls0_lookupResultValid = 0;
    uint32 mpls0_defaultEntryValid = 0;
    uint32 mpls0_keyIndex = 0;
    uint32 mpls0_adIndex = 0;
    uint32 mpls0_hashConflict = 0;
    uint32 mpls0_CAMlookupResultValid = 0;
    uint32 mpls0_keyIndexCam = 0;
    uint32 mpls0_valid = 0;
    DbgUserIdHashEngineForMpls0Info_m dbg_user_id_hash_engine_for_mpls0_info;
    /*DbgUserIdHashEngineForMpls1Info*/
    uint32 mpls1_lookupResultValid = 0;
    uint32 mpls1_defaultEntryValid = 0;
    uint32 mpls1_keyIndex = 0;
    uint32 mpls1_adIndex = 0;
    uint32 mpls1_hashConflict = 0;
    uint32 mpls1_CAMlookupResultValid = 0;
    uint32 mpls1_keyIndexCam = 0;
    uint32 mpls1_valid = 0;
    DbgUserIdHashEngineForMpls1Info_m dbg_user_id_hash_engine_for_mpls1_info;
    /*DbgUserIdHashEngineForMpls2Info*/
    uint32 mpls2_lookupResultValid = 0;
    uint32 mpls2_defaultEntryValid = 0;
    uint32 mpls2_keyIndex = 0;
    uint32 mpls2_adIndex = 0;
    uint32 mpls2_hashConflict = 0;
    uint32 mpls2_CAMlookupResultValid = 0;
    uint32 mpls2_keyIndexCam = 0;
    uint32 mpls2_valid = 0;
    DbgUserIdHashEngineForMpls2Info_m dbg_user_id_hash_engine_for_mpls2_info;
    /*DbgIpeMplsDecapInfo*/
    uint32 tunnel_tunnelDecap1 = 0;
    uint32 tunnel_tunnelDecap2 = 0;
    uint32 tunnel_ipMartianAddress = 0;
    uint32 tunnel_labelNum = 0;
    uint32 tunnel_isMplsSwitched = 0;
    uint32 tunnel_contextLabelExist = 0;
    uint32 tunnel_innerPacketLookup = 0;
    uint32 tunnel_forceParser = 0;
    uint32 tunnel_secondParserForEcmp = 0;
    uint32 tunnel_isDecap = 0;
    uint32 tunnel_srcDscp = 0;
    uint32 tunnel_mplsLabelOutOfRange = 0;
    uint32 tunnel_useEntropyLabelHash = 0;
    uint32 tunnel_rxOamType = 0;
    uint32 tunnel_packetTtl = 0;
    uint32 tunnel_isLeaf = 0;
    uint32 tunnel_logicPortType = 0;
    uint32 tunnel_serviceAclQosEn = 0;
    uint32 tunnel_payloadPacketType = 0;
    uint32 tunnel_dsFwdPtrValid = 0;
    uint32 tunnel_nextHopPtrValid = 0;
    uint32 tunnel_isPtpTunnel = 0;
    uint32 tunnel_ptpDeepParser = 0;
    uint32 tunnel_ptpExtraLabelNum = 0;
    uint32 tunnel_dsFwdPtr = 0;
    uint32 tunnel_discard = 0;
    uint32 tunnel_discardType = 0;
    uint32 tunnel_bypassAll = 0;
    uint32 tunnel_exceptionEn = 0;
    uint32 tunnel_exceptionIndex = 0;
    uint32 tunnel_exceptionSubIndex = 0;
    uint32 tunnel_fatalExceptionValid = 0;
    uint32 tunnel_fatalException = 0;
    uint32 tunnel_innerParserOffset = 0;
    uint32 tunnel_innerParserPacketType = 0;
    uint32 tunnel_mplsLmEn0 = 0;
    uint32 tunnel_mplsLmEn1 = 0;
    uint32 tunnel_mplsLmEn2 = 0;
    uint32 tunnel_firstLabel = 0;
    uint32 tunnel_valid = 0;
    DbgIpeMplsDecapInfo_m dbg_ipe_mpls_decap_info;

    /*DbgUserIdHashEngineForMpls0Info*/
    sal_memset(&dbg_user_id_hash_engine_for_mpls0_info, 0, sizeof(dbg_user_id_hash_engine_for_mpls0_info));
    cmd = DRV_IOR(DbgUserIdHashEngineForMpls0Info_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, slice, cmd, &dbg_user_id_hash_engine_for_mpls0_info);
    GetDbgUserIdHashEngineForMpls0Info(A, lookupResultValid_f, &dbg_user_id_hash_engine_for_mpls0_info, &mpls0_lookupResultValid );
    GetDbgUserIdHashEngineForMpls0Info(A, defaultEntryValid_f, &dbg_user_id_hash_engine_for_mpls0_info, &mpls0_defaultEntryValid );
    GetDbgUserIdHashEngineForMpls0Info(A, keyIndex_f, &dbg_user_id_hash_engine_for_mpls0_info, &mpls0_keyIndex);
    GetDbgUserIdHashEngineForMpls0Info(A, adIndex_f, &dbg_user_id_hash_engine_for_mpls0_info, &mpls0_adIndex);
    GetDbgUserIdHashEngineForMpls0Info(A, hashConflict_f, &dbg_user_id_hash_engine_for_mpls0_info, &mpls0_hashConflict);
    GetDbgUserIdHashEngineForMpls0Info(A, camLookupResultValid_f, &dbg_user_id_hash_engine_for_mpls0_info, &mpls0_CAMlookupResultValid);
    GetDbgUserIdHashEngineForMpls0Info(A, keyIndexCam_f, &dbg_user_id_hash_engine_for_mpls0_info, &mpls0_keyIndexCam);
    GetDbgUserIdHashEngineForMpls0Info(A, valid_f, &dbg_user_id_hash_engine_for_mpls0_info, &mpls0_valid);
    /*DbgUserIdHashEngineForMpls1Info*/
    sal_memset(&dbg_user_id_hash_engine_for_mpls1_info, 0, sizeof(dbg_user_id_hash_engine_for_mpls1_info));
    cmd = DRV_IOR(DbgUserIdHashEngineForMpls1Info_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, slice, cmd, &dbg_user_id_hash_engine_for_mpls1_info);
    GetDbgUserIdHashEngineForMpls1Info(A, lookupResultValid_f, &dbg_user_id_hash_engine_for_mpls1_info, &mpls1_lookupResultValid );
    GetDbgUserIdHashEngineForMpls1Info(A, defaultEntryValid_f, &dbg_user_id_hash_engine_for_mpls1_info, &mpls1_defaultEntryValid );
    GetDbgUserIdHashEngineForMpls1Info(A, keyIndex_f, &dbg_user_id_hash_engine_for_mpls1_info, &mpls1_keyIndex);
    GetDbgUserIdHashEngineForMpls1Info(A, adIndex_f, &dbg_user_id_hash_engine_for_mpls1_info, &mpls1_adIndex);
    GetDbgUserIdHashEngineForMpls1Info(A, hashConflict_f, &dbg_user_id_hash_engine_for_mpls1_info, &mpls1_hashConflict);
    GetDbgUserIdHashEngineForMpls1Info(A, camLookupResultValid_f, &dbg_user_id_hash_engine_for_mpls1_info, &mpls1_CAMlookupResultValid);
    GetDbgUserIdHashEngineForMpls1Info(A, keyIndexCam_f, &dbg_user_id_hash_engine_for_mpls1_info, &mpls1_keyIndexCam);
    GetDbgUserIdHashEngineForMpls1Info(A, valid_f, &dbg_user_id_hash_engine_for_mpls1_info, &mpls1_valid);
    /*DbgUserIdHashEngineForMpls2Info*/
    sal_memset(&dbg_user_id_hash_engine_for_mpls2_info, 0, sizeof(dbg_user_id_hash_engine_for_mpls2_info));
    cmd = DRV_IOR(DbgUserIdHashEngineForMpls2Info_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, slice, cmd, &dbg_user_id_hash_engine_for_mpls2_info);
    GetDbgUserIdHashEngineForMpls2Info(A, lookupResultValid_f, &dbg_user_id_hash_engine_for_mpls2_info, &mpls2_lookupResultValid );
    GetDbgUserIdHashEngineForMpls2Info(A, defaultEntryValid_f, &dbg_user_id_hash_engine_for_mpls2_info, &mpls2_defaultEntryValid );
    GetDbgUserIdHashEngineForMpls2Info(A, keyIndex_f, &dbg_user_id_hash_engine_for_mpls2_info, &mpls2_keyIndex);
    GetDbgUserIdHashEngineForMpls2Info(A, adIndex_f, &dbg_user_id_hash_engine_for_mpls2_info, &mpls2_adIndex);
    GetDbgUserIdHashEngineForMpls2Info(A, hashConflict_f, &dbg_user_id_hash_engine_for_mpls2_info, &mpls2_hashConflict);
    GetDbgUserIdHashEngineForMpls2Info(A, camLookupResultValid_f, &dbg_user_id_hash_engine_for_mpls2_info, &mpls2_CAMlookupResultValid);
    GetDbgUserIdHashEngineForMpls2Info(A, keyIndexCam_f, &dbg_user_id_hash_engine_for_mpls2_info, &mpls2_keyIndexCam);
    GetDbgUserIdHashEngineForMpls2Info(A, valid_f, &dbg_user_id_hash_engine_for_mpls2_info, &mpls2_valid);
    /*DbgIpeMplsDecapInfo*/
    sal_memset(&dbg_ipe_mpls_decap_info, 0, sizeof(dbg_ipe_mpls_decap_info));
    cmd = DRV_IOR(DbgIpeMplsDecapInfo_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, slice, cmd, &dbg_ipe_mpls_decap_info);
    GetDbgIpeMplsDecapInfo(A, tunnelDecap1_f, &dbg_ipe_mpls_decap_info, &tunnel_tunnelDecap1);
    GetDbgIpeMplsDecapInfo(A, tunnelDecap2_f, &dbg_ipe_mpls_decap_info, &tunnel_tunnelDecap2);
    GetDbgIpeMplsDecapInfo(A, ipMartianAddress_f, &dbg_ipe_mpls_decap_info, &tunnel_ipMartianAddress);
    GetDbgIpeMplsDecapInfo(A, labelNum_f, &dbg_ipe_mpls_decap_info, &tunnel_labelNum);
    GetDbgIpeMplsDecapInfo(A, isMplsSwitched_f, &dbg_ipe_mpls_decap_info, &tunnel_isMplsSwitched);
    GetDbgIpeMplsDecapInfo(A, contextLabelExist_f, &dbg_ipe_mpls_decap_info, &tunnel_contextLabelExist);
    GetDbgIpeMplsDecapInfo(A, innerPacketLookup_f, &dbg_ipe_mpls_decap_info, &tunnel_innerPacketLookup);
    GetDbgIpeMplsDecapInfo(A, forceParser_f, &dbg_ipe_mpls_decap_info, &tunnel_forceParser);
    GetDbgIpeMplsDecapInfo(A, secondParserForEcmp_f, &dbg_ipe_mpls_decap_info, &tunnel_secondParserForEcmp);
    GetDbgIpeMplsDecapInfo(A, isDecap_f, &dbg_ipe_mpls_decap_info, &tunnel_isDecap);
    GetDbgIpeMplsDecapInfo(A, srcDscp_f, &dbg_ipe_mpls_decap_info, &tunnel_srcDscp);
    GetDbgIpeMplsDecapInfo(A, mplsLabelOutOfRange_f, &dbg_ipe_mpls_decap_info, &tunnel_mplsLabelOutOfRange);
    GetDbgIpeMplsDecapInfo(A, useEntropyLabelHash_f, &dbg_ipe_mpls_decap_info, &tunnel_useEntropyLabelHash);
    GetDbgIpeMplsDecapInfo(A, rxOamType_f, &dbg_ipe_mpls_decap_info, &tunnel_rxOamType);
    GetDbgIpeMplsDecapInfo(A, packetTtl_f, &dbg_ipe_mpls_decap_info, &tunnel_packetTtl);
    GetDbgIpeMplsDecapInfo(A, isLeaf_f, &dbg_ipe_mpls_decap_info, &tunnel_isLeaf);
    GetDbgIpeMplsDecapInfo(A, logicPortType_f, &dbg_ipe_mpls_decap_info, &tunnel_logicPortType);
    GetDbgIpeMplsDecapInfo(A, serviceAclQosEn_f, &dbg_ipe_mpls_decap_info, &tunnel_serviceAclQosEn);
    GetDbgIpeMplsDecapInfo(A, payloadPacketType_f, &dbg_ipe_mpls_decap_info, &tunnel_payloadPacketType);
    GetDbgIpeMplsDecapInfo(A, dsFwdPtrValid_f, &dbg_ipe_mpls_decap_info, &tunnel_dsFwdPtrValid);
    GetDbgIpeMplsDecapInfo(A, nextHopPtrValid_f, &dbg_ipe_mpls_decap_info, &tunnel_nextHopPtrValid);
    GetDbgIpeMplsDecapInfo(A, isPtpTunnel_f, &dbg_ipe_mpls_decap_info, &tunnel_isPtpTunnel);
    GetDbgIpeMplsDecapInfo(A, ptpDeepParser_f, &dbg_ipe_mpls_decap_info, &tunnel_ptpDeepParser);
    GetDbgIpeMplsDecapInfo(A, ptpExtraLabelNum_f, &dbg_ipe_mpls_decap_info, &tunnel_ptpExtraLabelNum);
    GetDbgIpeMplsDecapInfo(A, dsFwdPtr_f, &dbg_ipe_mpls_decap_info, &tunnel_dsFwdPtr);
    GetDbgIpeMplsDecapInfo(A, discard_f, &dbg_ipe_mpls_decap_info, &tunnel_discard);
    GetDbgIpeMplsDecapInfo(A, discardType_f, &dbg_ipe_mpls_decap_info, &tunnel_discardType);
    GetDbgIpeMplsDecapInfo(A, bypassAll_f, &dbg_ipe_mpls_decap_info, &tunnel_bypassAll);
    GetDbgIpeMplsDecapInfo(A, exceptionEn_f, &dbg_ipe_mpls_decap_info, &tunnel_exceptionEn);
    GetDbgIpeMplsDecapInfo(A, exceptionIndex_f, &dbg_ipe_mpls_decap_info, &tunnel_exceptionIndex);
    GetDbgIpeMplsDecapInfo(A, exceptionSubIndex_f, &dbg_ipe_mpls_decap_info, &tunnel_exceptionSubIndex);
    GetDbgIpeMplsDecapInfo(A, fatalExceptionValid_f, &dbg_ipe_mpls_decap_info, &tunnel_fatalExceptionValid);
    GetDbgIpeMplsDecapInfo(A, fatalException_f, &dbg_ipe_mpls_decap_info, &tunnel_fatalException);
    GetDbgIpeMplsDecapInfo(A, innerParserOffset_f, &dbg_ipe_mpls_decap_info, &tunnel_innerParserOffset);
    GetDbgIpeMplsDecapInfo(A, innerParserPacketType_f, &dbg_ipe_mpls_decap_info, &tunnel_innerParserPacketType);
    GetDbgIpeMplsDecapInfo(A, mplsLmEn0_f, &dbg_ipe_mpls_decap_info, &tunnel_mplsLmEn0);
    GetDbgIpeMplsDecapInfo(A, mplsLmEn1_f, &dbg_ipe_mpls_decap_info, &tunnel_mplsLmEn1);
    GetDbgIpeMplsDecapInfo(A, mplsLmEn2_f, &dbg_ipe_mpls_decap_info, &tunnel_mplsLmEn2);
    GetDbgIpeMplsDecapInfo(A, firstLabel_f, &dbg_ipe_mpls_decap_info, &tunnel_firstLabel);
    GetDbgIpeMplsDecapInfo(A, valid_f, &dbg_ipe_mpls_decap_info, &tunnel_valid);

    if (0 == p_info->detail)
    {
        goto Discard;
    }

    if (tunnel_valid)
    {
        if (tunnel_tunnelDecap1 || tunnel_tunnelDecap2)
        {
            CTC_DKIT_PRINT("Tunnel Process:\n");
            if (tunnel_innerPacketLookup)
            {
                CTC_DKIT_PATH_PRINT("%-15s:%s\n", "forward by", "inner packet");
            }
            else
            {
                CTC_DKIT_PATH_PRINT("%-15s:%s\n", "forward by", "tunnel");
            }
        }
        else if (tunnel_isMplsSwitched)
        {
            uint8 i = 0;
            uint32 mpls_valid[3] = {mpls0_valid, mpls1_valid, mpls2_valid};
            uint32 mpls_key_index[3] = {mpls0_keyIndex, mpls1_keyIndex, mpls2_keyIndex};
            uint32 mpls_ad_index[3] = {mpls0_adIndex, mpls1_adIndex, mpls2_adIndex};
            uint32 mpls_conflict[3] = {mpls0_hashConflict, mpls1_hashConflict, mpls2_hashConflict};
            uint32 mpls_default[3] = {mpls0_defaultEntryValid, mpls1_defaultEntryValid, mpls2_defaultEntryValid};
            uint32 mpls_result_valid[3] = {mpls0_lookupResultValid, mpls1_lookupResultValid, mpls2_lookupResultValid};
            CTC_DKIT_PRINT("MPLS Process:\n");
            for (i = 0; i < 3; i++)
            {
                if (mpls_valid[i])
                {
                    if (0 == i)
                    {
                        CTC_DKIT_PATH_PRINT("%-15s\n", "1st label lookup--->");
                    }
                    else if (1 == i)
                    {
                        CTC_DKIT_PATH_PRINT("%-15s\n", "2nd label lookup--->");
                    }
                    else if (2 == i)
                    {
                        CTC_DKIT_PATH_PRINT("%-15s\n", "3th label lookup--->");
                    }
                    if (mpls_conflict[i])
                    {
                        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "result", CTC_DKIT_CAPTURED_CONFLICT);
                        CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "key index", mpls_key_index[i]);
                        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "key name", "DsUserIdTunnelMplsHashKey");
                    }
                    else if (mpls_default[i])
                    {
                        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "result", CTC_DKIT_CAPTURED_DEFAULT);
                        CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "key index", mpls_key_index[i]);
                        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "key name", "DsUserIdTunnelMplsHashKey");
                        CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "AD index", (mpls_ad_index[i] / 2));
                        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "AD name", "DsMpls");
                        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "pop lable", "YES");
                    }
                    else if (mpls_result_valid[i])
                    {
                        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "result", CTC_DKIT_CAPTURED_HASH_HIT);
                        CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "key index", mpls_key_index[i]);
                        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "key name", "DsUserIdTunnelMplsHashKey");
                        CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "AD index", (mpls_ad_index[i] / 2));
                        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "AD name", "DsMpls");
                        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "pop lable", "YES");
                    }
                    else
                    {
                        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "result", CTC_DKIT_CAPTURED_NOT_HIT);
                    }
                }
            }
            if(tunnel_innerPacketLookup)
            {
                CTC_DKIT_PATH_PRINT("%-15s:%s\n", "forward by", "inner packet");
            }
            else
            {
                CTC_DKIT_PATH_PRINT("%-15s:%s\n", "forward by", "MPLS label");
            }
        }
    }

Discard:
    _ctc_goldengate_dkit_captured_path_discard_process(p_info, tunnel_discard, tunnel_discardType, CTC_DKIT_IPE);
    _ctc_goldengate_dkit_captured_path_exception_process(p_info, tunnel_exceptionEn, tunnel_exceptionIndex, tunnel_exceptionSubIndex);

    return CLI_SUCCESS;
}

STATIC int32
_sys_goldengate_dkit_captured_path_acl_map_key_index(uint8 lchip, uint8 dir, uint8 block_id, uint32 key_index)
{
    uint8  step = 0;
    uint32 cmd = 0;
    uint32 acl_key_type = 0;
    uint32 table_id = 0;
    uint32 alloc_bitmap = 0;
    uint32 couple_mode = 0;
    tbl_entry_t tcam_entry;
    DsAclQosL3Key160Ingr0_m tcam_key;
    DsAclQosL3Key160Ingr0_m tcam_mask;


    drv_goldengate_get_dynamic_ram_couple_mode(&couple_mode);

    tcam_entry.data_entry = (uint32*)(&tcam_key);
    tcam_entry.mask_entry = (uint32*)(&tcam_mask);


    if(1 == dir)      /*1: egress*/
    {
        table_id = DsAclQosL3Key160Egr0_t;
        /*EPE ACL 0*/
        cmd = DRV_IOR(FlowTcamLookupCtl_t, FlowTcamLookupCtl_egressAcl0Bitmap_f);
        DRV_IOCTL(lchip, 0, cmd, &alloc_bitmap);

        if (key_index < DRV_TCAM_COUPLE_BASE/2)
        {
            if (key_index < DRV_TCAM_5_BASE/2)
            {
                /*hit tcam 4*/
                key_index = key_index - DRV_TCAM_4_BASE / 2;
            }
            else if (key_index < DRV_TCAM_6_BASE/2)
            {
                /*hit tcam 5*/
                key_index = key_index - DRV_TCAM_5_BASE / 2 +
                                    (IS_BIT_SET(alloc_bitmap, 0)) * (DRV_TCAM_KEY4_MAX_ENTRY_NUM / 2) * (couple_mode+1);
            }
            else
            {
                /*hit tcam 6*/
                key_index = key_index - DRV_TCAM_6_BASE / 2 +
                                     (IS_BIT_SET(alloc_bitmap, 0)) * (DRV_TCAM_KEY4_MAX_ENTRY_NUM / 2) * (couple_mode+1) +
                                    (IS_BIT_SET(alloc_bitmap, 1)) * (DRV_TCAM_KEY5_MAX_ENTRY_NUM / 2) * (couple_mode+1);
            }
        }
        else       /*hit couple slice 1 tcam*/
        {
            key_index = key_index - DRV_TCAM_COUPLE_BASE/2;

            if (key_index < DRV_TCAM_5_BASE/2)
            {
                /*hit tcam 4*/
                key_index = key_index - DRV_TCAM_4_BASE / 2 + DRV_TCAM_KEY4_MAX_ENTRY_NUM / 2;
            }
            else if (key_index < DRV_TCAM_6_BASE/2)
            {
                /*hit tcam 5*/
                key_index = key_index - DRV_TCAM_5_BASE / 2 + DRV_TCAM_KEY5_MAX_ENTRY_NUM / 2 +
                                    (IS_BIT_SET(alloc_bitmap, 0)) * DRV_TCAM_KEY4_MAX_ENTRY_NUM;
            }
            else
            {
                /*hit tcam 6*/
                key_index = key_index - DRV_TCAM_6_BASE / 2 + DRV_TCAM_KEY6_MAX_ENTRY_NUM / 2 +
                                     (IS_BIT_SET(alloc_bitmap, 0)) * DRV_TCAM_KEY4_MAX_ENTRY_NUM +
                                    (IS_BIT_SET(alloc_bitmap, 1)) * DRV_TCAM_KEY5_MAX_ENTRY_NUM;
            }
        }
    }
    else                /*0: ingress*/
    {
        table_id = DsAclQosL3Key160Ingr0_t + block_id;

        if (0 == block_id)
        {
            /*IPE ACL 0*/
            cmd = DRV_IOR(FlowTcamLookupCtl_t, FlowTcamLookupCtl_ingressAcl0Bitmap_f);
            DRV_IOCTL(lchip, 0, cmd, &alloc_bitmap);

            if (key_index < DRV_TCAM_COUPLE_BASE/2)
            {
                if (key_index < DRV_TCAM_2_BASE/2)
                {
                    /*hit tcam 1*/
                    key_index = key_index - DRV_TCAM_KEY0_MAX_ENTRY_NUM / 2;
                }
                else if (key_index < DRV_TCAM_5_BASE/2)
                {
                    /*hit tcam 2*/
                    key_index = key_index - DRV_TCAM_2_BASE / 2 +
                                        (IS_BIT_SET(alloc_bitmap, 0)) * (DRV_TCAM_KEY1_MAX_ENTRY_NUM / 2) * (couple_mode+1);
                }
                else if (key_index < DRV_TCAM_6_BASE/2)
                {
                    /*hit tcam 5*/
                    key_index = key_index - DRV_TCAM_5_BASE / 2 +
                                        (IS_BIT_SET(alloc_bitmap, 0)) * (DRV_TCAM_KEY1_MAX_ENTRY_NUM / 2) * (couple_mode+1) +
                                        (IS_BIT_SET(alloc_bitmap, 1)) * (DRV_TCAM_KEY2_MAX_ENTRY_NUM / 2) * (couple_mode+1);
                }
                else
                {
                    /*hit tcam 6*/
                    key_index = key_index - DRV_TCAM_6_BASE / 2 +
                                         (IS_BIT_SET(alloc_bitmap, 0)) * (DRV_TCAM_KEY1_MAX_ENTRY_NUM / 2) * (couple_mode+1) +
                                        (IS_BIT_SET(alloc_bitmap, 1)) * (DRV_TCAM_KEY2_MAX_ENTRY_NUM / 2) * (couple_mode+1) +
                                        (IS_BIT_SET(alloc_bitmap, 2)) * (DRV_TCAM_KEY5_MAX_ENTRY_NUM / 2) * (couple_mode+1);
                }
            }
            else       /*hit couple slice 1 tcam*/
            {
                key_index = key_index - DRV_TCAM_COUPLE_BASE/2;

                if (key_index < DRV_TCAM_2_BASE/2)
                {
                    /*hit tcam 1*/
                    key_index = key_index - DRV_TCAM_KEY0_MAX_ENTRY_NUM / 2 + DRV_TCAM_KEY1_MAX_ENTRY_NUM / 2;
                }
                else if (key_index < DRV_TCAM_5_BASE/2)
                {
                    /*hit tcam 2*/
                    key_index = key_index - DRV_TCAM_2_BASE / 2 + DRV_TCAM_KEY2_MAX_ENTRY_NUM / 2 +
                                        (IS_BIT_SET(alloc_bitmap, 0)) * DRV_TCAM_KEY1_MAX_ENTRY_NUM;
                }
                else if (key_index < DRV_TCAM_6_BASE/2)
                {
                    /*hit tcam 5*/
                    key_index = key_index - DRV_TCAM_5_BASE / 2 + DRV_TCAM_KEY5_MAX_ENTRY_NUM / 2 +
                                        (IS_BIT_SET(alloc_bitmap, 0)) * DRV_TCAM_KEY1_MAX_ENTRY_NUM +
                                        (IS_BIT_SET(alloc_bitmap, 1)) * DRV_TCAM_KEY2_MAX_ENTRY_NUM;
                }
                else
                {
                    /*hit tcam 6*/
                    key_index = key_index - DRV_TCAM_6_BASE / 2 + DRV_TCAM_KEY6_MAX_ENTRY_NUM / 2 +
                                         (IS_BIT_SET(alloc_bitmap, 0)) * DRV_TCAM_KEY1_MAX_ENTRY_NUM +
                                        (IS_BIT_SET(alloc_bitmap, 1)) * DRV_TCAM_KEY2_MAX_ENTRY_NUM +
                                        (IS_BIT_SET(alloc_bitmap, 2)) * DRV_TCAM_KEY5_MAX_ENTRY_NUM;
                }
            }
        }
        else if (1 == block_id)
        {
            /*IPE ACL 1*/
            cmd = DRV_IOR(FlowTcamLookupCtl_t, FlowTcamLookupCtl_ingressAcl1Bitmap_f);
            DRV_IOCTL(lchip, 0, cmd, &alloc_bitmap);

            if (key_index < DRV_TCAM_COUPLE_BASE/2)
            {
                if (key_index < DRV_TCAM_6_BASE/2)
                {
                    /*hit tcam 5*/
                    key_index = key_index - DRV_TCAM_5_BASE / 2;
                }
                else
                {
                    /*hit tcam 6*/
                    key_index = key_index - DRV_TCAM_6_BASE / 2 +
                                         (IS_BIT_SET(alloc_bitmap, 0)) * (DRV_TCAM_KEY5_MAX_ENTRY_NUM / 2) * (couple_mode+1);
                }
            }
            else       /*hit couple slice 1 tcam*/
            {
                key_index = key_index - DRV_TCAM_COUPLE_BASE/2;

                if (key_index < DRV_TCAM_6_BASE/2)
                {
                    /*hit tcam 5*/
                    key_index = key_index - DRV_TCAM_5_BASE / 2 + DRV_TCAM_KEY5_MAX_ENTRY_NUM / 2;
                }
                else
                {
                    /*hit tcam 6*/
                    key_index = key_index - DRV_TCAM_6_BASE / 2 + DRV_TCAM_KEY6_MAX_ENTRY_NUM / 2 +
                                         (IS_BIT_SET(alloc_bitmap, 0)) * DRV_TCAM_KEY5_MAX_ENTRY_NUM;
                }
            }
        }
        else if (2 == block_id)
        {
            /*IPE ACL 2, hit tcam 2 */
            if (key_index < DRV_TCAM_COUPLE_BASE/2)
            {
                key_index = key_index - DRV_TCAM_2_BASE / 2;
            }
            else
            {
                key_index = key_index - DRV_TCAM_COUPLE_BASE/2 - DRV_TCAM_2_BASE / 2 + DRV_TCAM_KEY2_MAX_ENTRY_NUM / 2;
            }

        }
        else if (3 == block_id)
        {
            /*IPE ACL 3, hit tcam 1*/
            if ((0 == couple_mode) || (key_index < DRV_TCAM_COUPLE_BASE/2))
            {
                key_index = key_index - DRV_TCAM_KEY0_MAX_ENTRY_NUM / 2;
            }
            else
            {
                key_index = key_index - DRV_TCAM_COUPLE_BASE/2 - DRV_TCAM_KEY0_MAX_ENTRY_NUM / 2 + DRV_TCAM_KEY1_MAX_ENTRY_NUM / 2;
            }
        }
    }

    cmd = DRV_IOR(table_id, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, key_index, cmd, &tcam_entry);
    GetDsAclQosL3Key160Ingr0(A, aclQosKeyType_f, &tcam_key, &acl_key_type);

    switch(acl_key_type)
    {
        case DRV_TCAMKEYTYPE_MACKEY160:
        case DRV_TCAMKEYTYPE_L3KEY160:
            step = 1;
            break;
        case DRV_TCAMKEYTYPE_MACL3KEY320:
            step = 2;
            break;
        case DRV_TCAMKEYTYPE_IPV6KEY640:
            step = 4;
            break;
        default:
            step = 1;
            break;
    }

    return (key_index/step);
}

STATIC int32
_ctc_goldengate_dkit_captured_path_ipe_acl(ctc_dkit_captured_path_info_t* p_info)
{
    uint8 slice = p_info->slice_ipe;
    uint8 lchip = p_info->lchip;
    uint32 cmd = 0;
    uint8 i = 0;
    uint32 temp_key_index = 0;

    /*DbgFlowTcamEngineIpeAclInfo*/
    uint32 tcam_ingressAcl0TcamResultValid = 0;
    uint32 tcam_ingressAcl0TcamIndex = 0;
    uint32 tcam_ingressAcl1TcamResultValid = 0;
    uint32 tcam_ingressAcl1TcamIndex = 0;
    uint32 tcam_ingressAcl2TcamResultValid = 0;
    uint32 tcam_ingressAcl2TcamIndex = 0;
    uint32 tcam_ingressAcl3TcamResultValid = 0;
    uint32 tcam_ingressAcl3TcamIndex = 0;
    uint32 tcam_valid = 0;
    DbgFlowTcamEngineIpeAclInfo_m dbg_flow_tcam_engine_ipe_acl_info;
    /*DbgFlowHashEngineInfo*/
    uint32 hash_hashKeyType = 0;
    uint32 hash_flowFieldSel = 0;
    uint32 hash_flowL2KeyUseCvlan = 0;
    uint32 hash_lookupResultValid = 0;
    uint32 hash_hashConflict = 0;
    uint32 hash_keyIndex = 0;
    uint32 hash_dsAdIndex = 0;
    uint32 hash_valid = 0;
    DbgFlowHashEngineInfo_m dbg_flow_hash_engine_info;
    /*DbgIpeAclLkpInfo*/
    uint32 acl_lkp_secondParserForEcmp = 0;
    uint32 acl_lkp_innerVtagCheckMode = 0;
    uint32 acl_lkp_isDecap = 0;
    uint32 acl_lkp_isMplsSwitched = 0;
    uint32 acl_lkp_ptpIndex = 0;
    uint32 acl_lkp_innerVsiIdValid = 0;
    uint32 acl_lkp_innerLogicSrcPortValid = 0;
    uint32 acl_lkp_bridgePacket = 0;
    uint32 acl_lkp_ipRoutedPacket = 0;
    uint32 acl_lkp_serviceAclQosEn = 0;
    uint32 acl_lkp_aclUseBitmap = 0;
    uint32 acl_lkp_overwritePortAclFlowTcam = 0;
    uint32 acl_lkp_aclFlowTcamEn = 0;
    uint32 acl_lkp_aclQosUseOuterInfo = 0;
    uint32 acl_lkp_isNvgre = 0;
    uint32 acl_lkp_aclUsePIVlan = 0;
    uint32 acl_lkp_aclFlowHashType = 0;
    uint32 acl_lkp_aclFlowHashFieldSel = 0;
    uint32 acl_lkp_ipfixHashType = 0;
    uint32 acl_lkp_ipfixHashFieldSel = 0;
    uint32 acl_lkp_aclEn0 = 0;
    uint32 acl_lkp_aclLookupType0 = 0;
    uint32 acl_lkp_aclLabel0 = 0;
    uint32 acl_lkp_aclEn1 = 0;
    uint32 acl_lkp_aclLookupType1 = 0;
    uint32 acl_lkp_aclLabel1 = 0;
    uint32 acl_lkp_aclEn2 = 0;
    uint32 acl_lkp_aclLookupType2 = 0;
    uint32 acl_lkp_aclLabel2 = 0;
    uint32 acl_lkp_aclEn3 = 0;
    uint32 acl_lkp_aclLookupType3 = 0;
    uint32 acl_lkp_aclLabel3 = 0;
    uint32 acl_lkp_valid = 0;
    DbgIpeAclLkpInfo_m dbg_ipe_acl_lkp_info;
    /*DbgIpeAclProcInfo*/
    uint32 acl_qosPolicy = 0;
    uint32 acl_userPriorityValid = 0;
    uint32 acl_payloadOffset = 0;
    uint32 acl_dsFwdPtrValid = 0;
    uint32 acl_nextHopPtrValid = 0;
    uint32 acl_ecmpGroupId = 0;
    uint32 acl_discard = 0;
    uint32 acl_discardType = 0;
    uint32 acl_dsFwdPtr = 0;
    uint32 acl_exceptionEn = 0;
    uint32 acl_exceptionIndex = 0;
    uint32 acl_exceptionSubIndex = 0;
    uint32 acl_denyBridge = 0;
    uint32 acl_denyLearning = 0;
    uint32 acl_denyRoute = 0;
    uint32 acl_flowForceBridge = 0;
    uint32 acl_flowForceLearning = 0;
    uint32 acl_flowForceRoute = 0;
    uint32 acl_postcardEn = 0;
    uint32 acl_forceBackEn = 0;
    uint32 acl_flowPolicerPtr = 0;
    uint32 acl_aggFlowPolicerPtr = 0;
    uint32 acl_aclLogEn0 = 0;
    uint32 acl_aclLogId0 = 0;
    uint32 acl_aclLogEn1 = 0;
    uint32 acl_aclLogId1 = 0;
    uint32 acl_aclLogEn2 = 0;
    uint32 acl_aclLogId2 = 0;
    uint32 acl_aclLogEn3 = 0;
    uint32 acl_aclLogId3 = 0;
    uint32 acl_svlanTagOperationValid = 0;
    uint32 acl_cvlanTagOperationValid = 0;
    uint32 acl_metadataType = 0;
    uint32 acl_metadata = 0;
    uint32 acl_flowPriorityValid = 0;
    uint32 acl_flowColorValid = 0;
    uint32 acl_valid = 0;
    DbgIpeAclProcInfo_m dbg_ipe_acl_proc_info;

    /*DbgFlowTcamEngineIpeAclInfo*/
    sal_memset(&dbg_flow_tcam_engine_ipe_acl_info, 0, sizeof(dbg_flow_tcam_engine_ipe_acl_info));
    cmd = DRV_IOR(DbgFlowTcamEngineIpeAclInfo_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &dbg_flow_tcam_engine_ipe_acl_info);
    GetDbgFlowTcamEngineIpeAclInfo(A, ingressAcl0TcamResultValid_f, &dbg_flow_tcam_engine_ipe_acl_info, &tcam_ingressAcl0TcamResultValid);
    GetDbgFlowTcamEngineIpeAclInfo(A, ingressAcl0TcamIndex_f, &dbg_flow_tcam_engine_ipe_acl_info, &tcam_ingressAcl0TcamIndex);
    GetDbgFlowTcamEngineIpeAclInfo(A, ingressAcl1TcamResultValid_f, &dbg_flow_tcam_engine_ipe_acl_info, &tcam_ingressAcl1TcamResultValid);
    GetDbgFlowTcamEngineIpeAclInfo(A, ingressAcl1TcamIndex_f, &dbg_flow_tcam_engine_ipe_acl_info, &tcam_ingressAcl1TcamIndex);
    GetDbgFlowTcamEngineIpeAclInfo(A, ingressAcl2TcamResultValid_f, &dbg_flow_tcam_engine_ipe_acl_info, &tcam_ingressAcl2TcamResultValid);
    GetDbgFlowTcamEngineIpeAclInfo(A, ingressAcl2TcamIndex_f, &dbg_flow_tcam_engine_ipe_acl_info, &tcam_ingressAcl2TcamIndex);
    GetDbgFlowTcamEngineIpeAclInfo(A, ingressAcl3TcamResultValid_f, &dbg_flow_tcam_engine_ipe_acl_info, &tcam_ingressAcl3TcamResultValid);
    GetDbgFlowTcamEngineIpeAclInfo(A, ingressAcl3TcamIndex_f, &dbg_flow_tcam_engine_ipe_acl_info, &tcam_ingressAcl3TcamIndex);
    GetDbgFlowTcamEngineIpeAclInfo(A, valid_f, &dbg_flow_tcam_engine_ipe_acl_info, &tcam_valid);
    /*DbgFlowHashEngineInfo*/
    sal_memset(&dbg_flow_hash_engine_info, 0, sizeof(dbg_flow_hash_engine_info));
    cmd = DRV_IOR(DbgFlowHashEngineInfo_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, slice, cmd, &dbg_flow_hash_engine_info);
    GetDbgFlowHashEngineInfo(A, hashKeyType_f, &dbg_flow_hash_engine_info, &hash_hashKeyType);
    GetDbgFlowHashEngineInfo(A, flowFieldSel_f, &dbg_flow_hash_engine_info, &hash_flowFieldSel);
    GetDbgFlowHashEngineInfo(A, flowL2KeyUseCvlan_f, &dbg_flow_hash_engine_info, &hash_flowL2KeyUseCvlan);
    GetDbgFlowHashEngineInfo(A, lookupResultValid_f, &dbg_flow_hash_engine_info, &hash_lookupResultValid);
    GetDbgFlowHashEngineInfo(A, hashConflict_f, &dbg_flow_hash_engine_info, &hash_hashConflict);
    GetDbgFlowHashEngineInfo(A, keyIndex_f, &dbg_flow_hash_engine_info, &hash_keyIndex);
    GetDbgFlowHashEngineInfo(A, dsAdIndex_f, &dbg_flow_hash_engine_info, &hash_dsAdIndex);
    GetDbgFlowHashEngineInfo(A, valid_f, &dbg_flow_hash_engine_info, &hash_valid);
    /*DbgIpeAclLkpInfo*/
    sal_memset(&dbg_ipe_acl_lkp_info, 0, sizeof(dbg_ipe_acl_lkp_info));
    cmd = DRV_IOR(DbgIpeAclLkpInfo_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, slice, cmd, &dbg_ipe_acl_lkp_info);
    GetDbgIpeAclLkpInfo(A, secondParserForEcmp_f, &dbg_ipe_acl_lkp_info, &acl_lkp_secondParserForEcmp);
    GetDbgIpeAclLkpInfo(A, innerVtagCheckMode_f, &dbg_ipe_acl_lkp_info, &acl_lkp_innerVtagCheckMode);
    GetDbgIpeAclLkpInfo(A, isDecap_f, &dbg_ipe_acl_lkp_info, &acl_lkp_isDecap);
    GetDbgIpeAclLkpInfo(A, isMplsSwitched_f, &dbg_ipe_acl_lkp_info, &acl_lkp_isMplsSwitched);
    GetDbgIpeAclLkpInfo(A, ptpIndex_f, &dbg_ipe_acl_lkp_info, &acl_lkp_ptpIndex);
    GetDbgIpeAclLkpInfo(A, innerVsiIdValid_f, &dbg_ipe_acl_lkp_info, &acl_lkp_innerVsiIdValid);
    GetDbgIpeAclLkpInfo(A, innerLogicSrcPortValid_f, &dbg_ipe_acl_lkp_info, &acl_lkp_innerLogicSrcPortValid);
    GetDbgIpeAclLkpInfo(A, bridgePacket_f, &dbg_ipe_acl_lkp_info, &acl_lkp_bridgePacket);
    GetDbgIpeAclLkpInfo(A, ipRoutedPacket_f, &dbg_ipe_acl_lkp_info, &acl_lkp_ipRoutedPacket);
    GetDbgIpeAclLkpInfo(A, serviceAclQosEn_f, &dbg_ipe_acl_lkp_info, &acl_lkp_serviceAclQosEn);
    GetDbgIpeAclLkpInfo(A, aclUseBitmap_f, &dbg_ipe_acl_lkp_info, &acl_lkp_aclUseBitmap);
    GetDbgIpeAclLkpInfo(A, overwritePortAclFlowTcam_f, &dbg_ipe_acl_lkp_info, &acl_lkp_overwritePortAclFlowTcam);
    GetDbgIpeAclLkpInfo(A, aclFlowTcamEn_f, &dbg_ipe_acl_lkp_info, &acl_lkp_aclFlowTcamEn);
    GetDbgIpeAclLkpInfo(A, aclQosUseOuterInfo_f, &dbg_ipe_acl_lkp_info, &acl_lkp_aclQosUseOuterInfo);
    GetDbgIpeAclLkpInfo(A, isNvgre_f, &dbg_ipe_acl_lkp_info, &acl_lkp_isNvgre);
    GetDbgIpeAclLkpInfo(A, aclUsePIVlan_f, &dbg_ipe_acl_lkp_info, &acl_lkp_aclUsePIVlan);
    GetDbgIpeAclLkpInfo(A, aclFlowHashType_f, &dbg_ipe_acl_lkp_info, &acl_lkp_aclFlowHashType);
    GetDbgIpeAclLkpInfo(A, aclFlowHashFieldSel_f, &dbg_ipe_acl_lkp_info, &acl_lkp_aclFlowHashFieldSel);
    GetDbgIpeAclLkpInfo(A, ipfixHashType_f, &dbg_ipe_acl_lkp_info, &acl_lkp_ipfixHashType);
    GetDbgIpeAclLkpInfo(A, ipfixHashFieldSel_f, &dbg_ipe_acl_lkp_info, &acl_lkp_ipfixHashFieldSel);
    GetDbgIpeAclLkpInfo(A, aclEn0_f, &dbg_ipe_acl_lkp_info, &acl_lkp_aclEn0);
    GetDbgIpeAclLkpInfo(A, aclLookupType0_f, &dbg_ipe_acl_lkp_info, &acl_lkp_aclLookupType0);
    GetDbgIpeAclLkpInfo(A, aclLabel0_f, &dbg_ipe_acl_lkp_info, &acl_lkp_aclLabel0);
    GetDbgIpeAclLkpInfo(A, aclEn1_f, &dbg_ipe_acl_lkp_info, &acl_lkp_aclEn1);
    GetDbgIpeAclLkpInfo(A, aclLookupType1_f, &dbg_ipe_acl_lkp_info, &acl_lkp_aclLookupType1);
    GetDbgIpeAclLkpInfo(A, aclLabel1_f, &dbg_ipe_acl_lkp_info, &acl_lkp_aclLabel1);
    GetDbgIpeAclLkpInfo(A, aclEn2_f, &dbg_ipe_acl_lkp_info, &acl_lkp_aclEn2);
    GetDbgIpeAclLkpInfo(A, aclLookupType2_f, &dbg_ipe_acl_lkp_info, &acl_lkp_aclLookupType2);
    GetDbgIpeAclLkpInfo(A, aclLabel2_f, &dbg_ipe_acl_lkp_info, &acl_lkp_aclLabel2);
    GetDbgIpeAclLkpInfo(A, aclEn3_f, &dbg_ipe_acl_lkp_info, &acl_lkp_aclEn3);
    GetDbgIpeAclLkpInfo(A, aclLookupType3_f, &dbg_ipe_acl_lkp_info, &acl_lkp_aclLookupType3);
    GetDbgIpeAclLkpInfo(A, aclLabel3_f, &dbg_ipe_acl_lkp_info, &acl_lkp_aclLabel3);
    GetDbgIpeAclLkpInfo(A, valid_f, &dbg_ipe_acl_lkp_info, &acl_lkp_valid);
     /*DbgIpeAclProcInfo*/
    sal_memset(&dbg_ipe_acl_proc_info, 0, sizeof(dbg_ipe_acl_proc_info));
    cmd = DRV_IOR(DbgIpeAclProcInfo_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, slice, cmd, &dbg_ipe_acl_proc_info);
    GetDbgIpeAclProcInfo(A, qosPolicy_f, &dbg_ipe_acl_proc_info, &acl_qosPolicy);
    GetDbgIpeAclProcInfo(A, userPriorityValid_f, &dbg_ipe_acl_proc_info, &acl_userPriorityValid);
    GetDbgIpeAclProcInfo(A, payloadOffset_f, &dbg_ipe_acl_proc_info, &acl_payloadOffset);
    GetDbgIpeAclProcInfo(A, dsFwdPtrValid_f, &dbg_ipe_acl_proc_info, &acl_dsFwdPtrValid);
    GetDbgIpeAclProcInfo(A, nextHopPtrValid_f, &dbg_ipe_acl_proc_info, &acl_nextHopPtrValid);
    GetDbgIpeAclProcInfo(A, ecmpGroupId_f, &dbg_ipe_acl_proc_info, &acl_ecmpGroupId);
    GetDbgIpeAclProcInfo(A, discard_f, &dbg_ipe_acl_proc_info, &acl_discard);
    GetDbgIpeAclProcInfo(A, discardType_f, &dbg_ipe_acl_proc_info, &acl_discardType);
    GetDbgIpeAclProcInfo(A, dsFwdPtr_f, &dbg_ipe_acl_proc_info, &acl_dsFwdPtr);
    GetDbgIpeAclProcInfo(A, exceptionEn_f, &dbg_ipe_acl_proc_info, &acl_exceptionEn);
    GetDbgIpeAclProcInfo(A, exceptionIndex_f, &dbg_ipe_acl_proc_info, &acl_exceptionIndex);
    GetDbgIpeAclProcInfo(A, exceptionSubIndex_f, &dbg_ipe_acl_proc_info, &acl_exceptionSubIndex);
    GetDbgIpeAclProcInfo(A, denyBridge_f, &dbg_ipe_acl_proc_info, &acl_denyBridge);
    GetDbgIpeAclProcInfo(A, denyLearning_f, &dbg_ipe_acl_proc_info, &acl_denyLearning);
    GetDbgIpeAclProcInfo(A, denyRoute_f, &dbg_ipe_acl_proc_info, &acl_denyRoute);
    GetDbgIpeAclProcInfo(A, flowForceBridge_f, &dbg_ipe_acl_proc_info, &acl_flowForceBridge);
    GetDbgIpeAclProcInfo(A, flowForceLearning_f, &dbg_ipe_acl_proc_info, &acl_flowForceLearning);
    GetDbgIpeAclProcInfo(A, flowForceRoute_f, &dbg_ipe_acl_proc_info, &acl_flowForceRoute);
    GetDbgIpeAclProcInfo(A, postcardEn_f, &dbg_ipe_acl_proc_info, &acl_postcardEn);
    GetDbgIpeAclProcInfo(A, forceBackEn_f, &dbg_ipe_acl_proc_info, &acl_forceBackEn);
    GetDbgIpeAclProcInfo(A, flowPolicerPtr_f, &dbg_ipe_acl_proc_info, &acl_flowPolicerPtr);
    GetDbgIpeAclProcInfo(A, aggFlowPolicerPtr_f, &dbg_ipe_acl_proc_info, &acl_aggFlowPolicerPtr);
    GetDbgIpeAclProcInfo(A, aclLogEn0_f, &dbg_ipe_acl_proc_info, &acl_aclLogEn0);
    GetDbgIpeAclProcInfo(A, aclLogId0_f, &dbg_ipe_acl_proc_info, &acl_aclLogId0);
    GetDbgIpeAclProcInfo(A, aclLogEn1_f, &dbg_ipe_acl_proc_info, &acl_aclLogEn1);
    GetDbgIpeAclProcInfo(A, aclLogId1_f, &dbg_ipe_acl_proc_info, &acl_aclLogId1);
    GetDbgIpeAclProcInfo(A, aclLogEn2_f, &dbg_ipe_acl_proc_info, &acl_aclLogEn2);
    GetDbgIpeAclProcInfo(A, aclLogId2_f, &dbg_ipe_acl_proc_info, &acl_aclLogId2);
    GetDbgIpeAclProcInfo(A, aclLogEn3_f, &dbg_ipe_acl_proc_info, &acl_aclLogEn3);
    GetDbgIpeAclProcInfo(A, aclLogId3_f, &dbg_ipe_acl_proc_info, &acl_aclLogId3);
    GetDbgIpeAclProcInfo(A, svlanTagOperationValid_f, &dbg_ipe_acl_proc_info, &acl_svlanTagOperationValid);
    GetDbgIpeAclProcInfo(A, cvlanTagOperationValid_f, &dbg_ipe_acl_proc_info, &acl_cvlanTagOperationValid);
    GetDbgIpeAclProcInfo(A, metadataType_f, &dbg_ipe_acl_proc_info, &acl_metadataType);
    GetDbgIpeAclProcInfo(A, metadata_f, &dbg_ipe_acl_proc_info, &acl_metadata);
    GetDbgIpeAclProcInfo(A, flowPriorityValid_f, &dbg_ipe_acl_proc_info, &acl_flowPriorityValid);
    GetDbgIpeAclProcInfo(A, flowColorValid_f, &dbg_ipe_acl_proc_info, &acl_flowColorValid);
    GetDbgIpeAclProcInfo(A, valid_f, &dbg_ipe_acl_proc_info, &acl_valid);

    if (0 == p_info->detail)
    {
        goto Discard;
    }

    if (acl_lkp_aclEn0 || acl_lkp_aclEn1 || acl_lkp_aclEn2 || acl_lkp_aclEn3 || hash_valid)
    {
        CTC_DKIT_PRINT("ACL Process:\n");

        /*tcam*/
        if (tcam_valid)
        {
            uint32 acl_lkp_type[4] ={acl_lkp_aclLookupType0,acl_lkp_aclLookupType1,acl_lkp_aclLookupType2,acl_lkp_aclLookupType3} ;
            char* acl_key_type_desc[4] = {"TCAML2KEY", "TCAML2L3KEY", "TCAML3KEY", "TCAMVLANKEY"};
            uint32 acl_en[4] = {acl_lkp_aclEn0, acl_lkp_aclEn1, acl_lkp_aclEn2, acl_lkp_aclEn3};
            char* lookup_result[4] = {"TCAM0 lookup--->", "TCAM1 lookup--->", "TCAM2 lookup--->", "TCAM3 lookup--->"};
            uint32 result_valid[4] = {tcam_ingressAcl0TcamResultValid, tcam_ingressAcl1TcamResultValid,
                                                                tcam_ingressAcl2TcamResultValid, tcam_ingressAcl3TcamResultValid};
            uint32 tcam_index[4] = {tcam_ingressAcl0TcamIndex, tcam_ingressAcl1TcamIndex, tcam_ingressAcl2TcamIndex, tcam_ingressAcl3TcamIndex};

            for (i = 0; i < 4 ; i++)
            {
                if (acl_en[i])
                {
                    CTC_DKIT_PATH_PRINT("%-15s\n", lookup_result[i]);
                    CTC_DKIT_PATH_PRINT("%-15s:%s\n", "key type", acl_key_type_desc[acl_lkp_type[i]]);
                    if (result_valid[i])
                    {
                        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "result", CTC_DKIT_CAPTURED_TCAM_HIT);
                        temp_key_index = _sys_goldengate_dkit_captured_path_acl_map_key_index(lchip, 0, i, tcam_index[i]);
                        CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "key index", temp_key_index);
                    }
                    else
                    {
                        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "result", CTC_DKIT_CAPTURED_NOT_HIT);
                    }
                }
            }
        }

        /*hash*/
        if (hash_valid)
        {
            CTC_DKIT_PATH_PRINT("%-15s\n", "HASH lookup--->");
            if (hash_hashConflict)
            {
                CTC_DKIT_PATH_PRINT("%-15s:%s\n", "result", CTC_DKIT_CAPTURED_CONFLICT);
                CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "key index", hash_keyIndex);
                CTC_DKIT_PATH_PRINT("%-15s:%s\n", "key name", "");
            }
            else if (hash_lookupResultValid)
            {
                CTC_DKIT_PATH_PRINT("%-15s:%s\n", "result", CTC_DKIT_CAPTURED_HASH_HIT);
                CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "key index", hash_keyIndex);
                CTC_DKIT_PATH_PRINT("%-15s:%s\n", "key name", "");
                CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "AD index", hash_dsAdIndex);
                CTC_DKIT_PATH_PRINT("%-15s:%s\n", "AD name", "DsAcl");
            }
            else
            {
                CTC_DKIT_PATH_PRINT("%-15s:%s\n", "result", CTC_DKIT_CAPTURED_NOT_HIT);
            }

        }
    }

Discard:
    _ctc_goldengate_dkit_captured_path_discard_process(p_info, acl_discard, acl_discardType, CTC_DKIT_IPE);
    _ctc_goldengate_dkit_captured_path_exception_process(p_info, acl_exceptionEn, acl_exceptionIndex, acl_exceptionSubIndex);

    return CLI_SUCCESS;
}

STATIC int32
_ctc_goldengate_dkit_captured_path_ipe_forward(ctc_dkit_captured_path_info_t* p_info)
{
    uint8 slice = p_info->slice_ipe;
    uint8 lchip = p_info->lchip;
    uint32 cmd = 0;
    uint32 key_index = 0;
    uint32 ad_index = 0;
    uint32 lpm_type0 = 0;
    uint32 lpm_type1 = 0;
    uint32 discard = 0;
    uint32 discard_type = 0;
    uint32 exceptionEn = 0;
    uint32 exceptionIndex = 0;
    uint32 exceptionSubIndex = 0;
    uint32 ds_ip_da_idx = 0;
    char* key_name = NULL;
    char* ad_name = NULL;
    char* result = NULL;
    uint32 defaultEntryBase = 0;
    drv_fib_acc_in_t  fib_acc_in;
    drv_fib_acc_out_t fib_acc_out;
    drv_cpu_acc_in_t  cpu_acc_in;
    drv_cpu_acc_out_t cpu_acc_out;
    /*DbgFibLkpEngineMacDaHashInfo*/
    uint32 macda_blackHoleMacDaResultValid = 0;
    uint32 macda_blackHoleHitMacDaKeyIndex = 0;
    uint32 macda_macDaHashResultValid = 0;
    uint32 macda_macDaHashLookupConflict = 0;
    uint32 macda_pending = 0;
    uint32 macda_hitHashKeyIndexMacDa = 0;
    uint32 macda_valid = 0;
    DbgFibLkpEngineMacDaHashInfo_m dbg_fib_lkp_engine_mac_da_hash_info;
    /*DbgFibLkpEnginel3DaHashInfo*/
    uint32 l3Da_l3DaHashResultValid = 0;
    uint32 l3Da_hitHashKeyIndexl3Da = 0;
    uint32 l3Da_valid = 0;
    DbgFibLkpEnginel3DaHashInfo_m dbg_fib_lkp_enginel3_da_hash_info;
    /*DbgFibLkpEnginel3DaLpmInfo*/
    uint32 l3Da_lpm_l3DaTcamResultValid= 0;
    uint32 l3Da_lpm_l3DaResultTcamEntryIndex= 0;
    uint32 l3Da_lpm_ipDaPipeline1DebugIndex= 0;
    uint32 l3Da_lpm_ipDaPipeline2DebugIndex= 0;
    uint32 l3Da_lpm_valid= 0;
    DbgFibLkpEnginel3DaLpmInfo_m dbg_fib_lkp_enginel3_da_lpm_info;
    /*DbgFibLkpEnginel3SaHashInfo*/
    uint32 l3Sa_l3SaHashResultValid = 0;
    uint32 l3Sa_hitHashKeyIndexl3Sa = 0;
    uint32 l3Sa_valid = 0;
    DbgFibLkpEnginel3SaHashInfo_m dbg_fib_lkp_enginel3_sa_hash_info;
    /*DbgFibLkpEnginePbrNatTcamInfo*/
    uint32 pbr_natPbrTcamResultValid = 0;
    uint32 pbr_natPbrResultTcamEntryIndex = 0;
    uint32 pbr_valid = 0;
    DbgFibLkpEnginePbrNatTcamInfo_m dbg_fib_lkp_engine_pbr_nat_tcam_info;
    /*DbgIpeLkpMgrInfo*/
    uint32 lkp_exceptionIndex = 0;
    uint32 lkp_exceptionSubIndex = 0;
    uint32 lkp_macDaLookupEn = 0;
    uint32 lkp_exceptionEn = 0;
    DbgIpeLkpMgrInfo_m dbg_ipe_lkp_mgr_info;
    /*DbgIpeIpRoutingInfo*/
    uint32 route_l3DaResultValid = 0;
    uint32 route_l3SaResultValid = 0;
    uint32 route_l3DaLookupEn = 0;
    uint32 route_l3SaLookupEn = 0;
    uint32 route_isIpv4Ucast = 0;
    uint32 route_isIpv4UcastNat = 0;
    uint32 route_isIpv4Pbr = 0;
    uint32 route_isIpv6Ucast = 0;
    uint32 route_isIpv6UcastNat = 0;
    uint32 route_isIpv6Pbr = 0;
    uint32 route_discard = 0;
    uint32 route_discardType = 0;
    uint32 route_exceptionEn = 0;
    uint32 route_exceptionIndex = 0;
    uint32 route_exceptionSubIndex = 0;
    uint32 route_valid = 0;
    DbgIpeIpRoutingInfo_m dbg_ipe_ip_routing_info;
    /*DbgIpeFcoeInfo*/
    uint32 fcoe_isFcoe = 0;
    uint32 fcoe_isFcoeRpf = 0;
    uint32 fcoe_l3DaResultValid = 0;
    uint32 fcoe_l3SaResultValid = 0;
    uint32 fcoe_l3DaLookupEn = 0;
    uint32 fcoe_l3SaLookupEn = 0;
    uint32 fcoe_valid = 0;
    DbgIpeFcoeInfo_m dbg_ipe_fcoe_info;
    /*DbgIpeTrillInfo*/
    uint32 trill_l3DaResultValid = 0;
    uint32 trill_isTrillUcast = 0;
    uint32 trill_isTrillMcast = 0;
    uint32 trill_valid = 0;
    DbgIpeTrillInfo_m dbg_ipe_trill_info;
    /*DbgIpeMacBridgingInfo*/
    uint32 bridge_macDaResultValid = 0;
    uint32 bridge_macDaDefaultEntryValid = 0;
    uint32 bridge_discard = 0;
    uint32 bridge_discardType = 0;
    uint32 bridge_exceptionEn = 0;
    uint32 bridge_exceptionIndex = 0;
    uint32 bridge_exceptionSubIndex = 0;
    uint32 bridge_bridgePacket = 0;
    uint32 bridge_valid = 0;
    DbgIpeMacBridgingInfo_m dbg_ipe_mac_bridging_info;
    /*DbgIpeMacLearningInfo*/
    uint32 lrn_macSaDefaultEntryValid = 0;
    uint32 lrn_macSaHashConflict = 0;
    uint32 lrn_macSaResultValid = 0;
    uint32 lrn_macSaLookupEn = 0;
    uint32 lrn_fid = 0;
    uint32 lrn_learningSrcPort = 0;
    uint32 lrn_isGlobalSrcPort = 0;
    uint32 lrn_learningEn = 0;
    uint32 lrn_discard = 0;
    uint32 lrn_discardType = 0;
    uint32 lrn_valid = 0;
    DbgIpeMacLearningInfo_m dbg_ipe_mac_learning_info;

    /*DbgFwdBufStoreInfo*/
    uint32 hdr_valid = 0;
    uint32 hdr_fid = 0;
    DbgFwdBufStoreInfo_m dbg_fwd_buf_store_info;

    sal_memset(&fib_acc_in, 0, sizeof(fib_acc_in));
    sal_memset(&fib_acc_out, 0, sizeof(fib_acc_out));
    sal_memset(&cpu_acc_in, 0, sizeof(drv_cpu_acc_in_t));
    sal_memset(&cpu_acc_out, 0, sizeof(drv_cpu_acc_out_t));

    /*DbgFibLkpEngineMacDaHashInfo*/
    sal_memset(&dbg_fib_lkp_engine_mac_da_hash_info, 0, sizeof(dbg_fib_lkp_engine_mac_da_hash_info));
    cmd = DRV_IOR(DbgFibLkpEngineMacDaHashInfo_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, slice, cmd, &dbg_fib_lkp_engine_mac_da_hash_info);
    GetDbgFibLkpEngineMacDaHashInfo(A, blackHoleMacDaResultValid_f, &dbg_fib_lkp_engine_mac_da_hash_info, &macda_blackHoleMacDaResultValid);
    GetDbgFibLkpEngineMacDaHashInfo(A, blackHoleHitMacDaKeyIndex_f, &dbg_fib_lkp_engine_mac_da_hash_info, &macda_blackHoleHitMacDaKeyIndex);
    GetDbgFibLkpEngineMacDaHashInfo(A, macDaHashResultValid_f, &dbg_fib_lkp_engine_mac_da_hash_info, &macda_macDaHashResultValid);
    GetDbgFibLkpEngineMacDaHashInfo(A, macDaHashLookupConflict_f, &dbg_fib_lkp_engine_mac_da_hash_info, &macda_macDaHashLookupConflict);
    GetDbgFibLkpEngineMacDaHashInfo(A, pending_f, &dbg_fib_lkp_engine_mac_da_hash_info, &macda_pending);
    GetDbgFibLkpEngineMacDaHashInfo(A, hitHashKeyIndexMacDa_f, &dbg_fib_lkp_engine_mac_da_hash_info, &macda_hitHashKeyIndexMacDa);
    GetDbgFibLkpEngineMacDaHashInfo(A, valid_f, &dbg_fib_lkp_engine_mac_da_hash_info, &macda_valid);
    /*DbgFibLkpEnginel3DaHashInfo*/
    sal_memset(&dbg_fib_lkp_enginel3_da_hash_info, 0, sizeof(dbg_fib_lkp_enginel3_da_hash_info));
    cmd = DRV_IOR(DbgFibLkpEnginel3DaHashInfo_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, slice, cmd, &dbg_fib_lkp_enginel3_da_hash_info);
    GetDbgFibLkpEnginel3DaHashInfo(A, l3DaHashResultValid_f, &dbg_fib_lkp_enginel3_da_hash_info, &l3Da_l3DaHashResultValid);
    GetDbgFibLkpEnginel3DaHashInfo(A, hitHashKeyIndexl3Da_f, &dbg_fib_lkp_enginel3_da_hash_info, &l3Da_hitHashKeyIndexl3Da);
    GetDbgFibLkpEnginel3DaHashInfo(A, valid_f, &dbg_fib_lkp_enginel3_da_hash_info, &l3Da_valid);
    /*DbgFibLkpEnginel3DaLpmInfo*/
    sal_memset(&dbg_fib_lkp_enginel3_da_lpm_info, 0, sizeof(dbg_fib_lkp_enginel3_da_lpm_info));
    cmd = DRV_IOR(DbgFibLkpEnginel3DaLpmInfo_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, slice, cmd, &dbg_fib_lkp_enginel3_da_lpm_info);
    GetDbgFibLkpEnginel3DaLpmInfo(A, l3DaTcamResultValid_f, &dbg_fib_lkp_enginel3_da_lpm_info, &l3Da_lpm_l3DaTcamResultValid);
    GetDbgFibLkpEnginel3DaLpmInfo(A, l3DaResultTcamEntryIndex_f, &dbg_fib_lkp_enginel3_da_lpm_info, &l3Da_lpm_l3DaResultTcamEntryIndex);
    GetDbgFibLkpEnginel3DaLpmInfo(A, ipDaPipeline1DebugIndex_f, &dbg_fib_lkp_enginel3_da_lpm_info, &l3Da_lpm_ipDaPipeline1DebugIndex);
    GetDbgFibLkpEnginel3DaLpmInfo(A, ipDaPipeline2DebugIndex_f, &dbg_fib_lkp_enginel3_da_lpm_info, &l3Da_lpm_ipDaPipeline2DebugIndex);
    GetDbgFibLkpEnginel3DaLpmInfo(A, valid_f, &dbg_fib_lkp_enginel3_da_lpm_info, &l3Da_lpm_valid);
    /*DbgFibLkpEnginel3SaHashInfo*/
    sal_memset(&dbg_fib_lkp_enginel3_sa_hash_info, 0, sizeof(dbg_fib_lkp_enginel3_sa_hash_info));
    cmd = DRV_IOR(DbgFibLkpEnginel3SaHashInfo_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, slice, cmd, &dbg_fib_lkp_enginel3_sa_hash_info);
    GetDbgFibLkpEnginel3SaHashInfo(A, l3SaHashResultValid_f, &dbg_fib_lkp_enginel3_sa_hash_info, &l3Sa_l3SaHashResultValid);
    GetDbgFibLkpEnginel3SaHashInfo(A, hitHashKeyIndexl3Sa_f, &dbg_fib_lkp_enginel3_sa_hash_info, &l3Sa_hitHashKeyIndexl3Sa);
    GetDbgFibLkpEnginel3SaHashInfo(A, valid_f, &dbg_fib_lkp_enginel3_sa_hash_info, &l3Sa_valid);
    /*DbgFibLkpEnginePbrNatTcamInfo*/
    sal_memset(&dbg_fib_lkp_engine_pbr_nat_tcam_info, 0, sizeof(dbg_fib_lkp_engine_pbr_nat_tcam_info));
    cmd = DRV_IOR(DbgFibLkpEnginePbrNatTcamInfo_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, slice, cmd, &dbg_fib_lkp_engine_pbr_nat_tcam_info);
    GetDbgFibLkpEnginePbrNatTcamInfo(A, natPbrTcamResultValid_f, &dbg_fib_lkp_engine_pbr_nat_tcam_info, &pbr_natPbrTcamResultValid);
    GetDbgFibLkpEnginePbrNatTcamInfo(A, natPbrResultTcamEntryIndex_f, &dbg_fib_lkp_engine_pbr_nat_tcam_info, &pbr_natPbrResultTcamEntryIndex);
    GetDbgFibLkpEnginePbrNatTcamInfo(A, valid_f, &dbg_fib_lkp_engine_pbr_nat_tcam_info, &pbr_valid);
    /*DbgIpeLkpMgrInfo*/
    sal_memset(&dbg_ipe_lkp_mgr_info, 0, sizeof(dbg_ipe_lkp_mgr_info));
    cmd = DRV_IOR(DbgIpeLkpMgrInfo_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, slice, cmd, &dbg_ipe_lkp_mgr_info);
    GetDbgIpeLkpMgrInfo(A, exceptionIndex_f, &dbg_ipe_lkp_mgr_info, &lkp_exceptionIndex);
    GetDbgIpeLkpMgrInfo(A, exceptionSubIndex_f, &dbg_ipe_lkp_mgr_info, &lkp_exceptionSubIndex);
    GetDbgIpeLkpMgrInfo(A, macDaLookupEn_f, &dbg_ipe_lkp_mgr_info, &lkp_macDaLookupEn);
    GetDbgIpeLkpMgrInfo(A, exceptionEn_f, &dbg_ipe_lkp_mgr_info, &lkp_exceptionEn);
    /*DbgIpeIpRoutingInfo*/
    sal_memset(&dbg_ipe_ip_routing_info, 0, sizeof(dbg_ipe_ip_routing_info));
    cmd = DRV_IOR(DbgIpeIpRoutingInfo_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, slice, cmd, &dbg_ipe_ip_routing_info);
    GetDbgIpeIpRoutingInfo(A, l3DaResultValid_f, &dbg_ipe_ip_routing_info, &route_l3DaResultValid);
    GetDbgIpeIpRoutingInfo(A, l3SaResultValid_f, &dbg_ipe_ip_routing_info, &route_l3SaResultValid);
    GetDbgIpeIpRoutingInfo(A, l3DaLookupEn_f, &dbg_ipe_ip_routing_info, &route_l3DaLookupEn);
    GetDbgIpeIpRoutingInfo(A, l3SaLookupEn_f, &dbg_ipe_ip_routing_info, &route_l3SaLookupEn);
    GetDbgIpeIpRoutingInfo(A, isIpv4Ucast_f, &dbg_ipe_ip_routing_info, &route_isIpv4Ucast);
    GetDbgIpeIpRoutingInfo(A, isIpv4UcastNat_f, &dbg_ipe_ip_routing_info, &route_isIpv4UcastNat);
    GetDbgIpeIpRoutingInfo(A, isIpv4Pbr_f, &dbg_ipe_ip_routing_info, &route_isIpv4Pbr);
    GetDbgIpeIpRoutingInfo(A, isIpv6Ucast_f, &dbg_ipe_ip_routing_info, &route_isIpv6Ucast);
    GetDbgIpeIpRoutingInfo(A, isIpv6UcastNat_f, &dbg_ipe_ip_routing_info, &route_isIpv6UcastNat);
    GetDbgIpeIpRoutingInfo(A, isIpv6Pbr_f, &dbg_ipe_ip_routing_info, &route_isIpv6Pbr);
    GetDbgIpeIpRoutingInfo(A, discard_f, &dbg_ipe_ip_routing_info, &route_discard);
    GetDbgIpeIpRoutingInfo(A, discardType_f, &dbg_ipe_ip_routing_info, &route_discardType);
    GetDbgIpeIpRoutingInfo(A, exceptionEn_f, &dbg_ipe_ip_routing_info, &route_exceptionEn);
    GetDbgIpeIpRoutingInfo(A, exceptionIndex_f, &dbg_ipe_ip_routing_info, &route_exceptionIndex);
    GetDbgIpeIpRoutingInfo(A, exceptionSubIndex_f, &dbg_ipe_ip_routing_info, &route_exceptionSubIndex);
    GetDbgIpeIpRoutingInfo(A, valid_f, &dbg_ipe_ip_routing_info, &route_valid);
    /*DbgIpeFcoeInfo*/
    sal_memset(&dbg_ipe_fcoe_info, 0, sizeof(dbg_ipe_fcoe_info));
    cmd = DRV_IOR(DbgIpeFcoeInfo_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, slice, cmd, &dbg_ipe_fcoe_info);
    GetDbgIpeFcoeInfo(A, isFcoe_f, &dbg_ipe_fcoe_info, &fcoe_isFcoe);
    GetDbgIpeFcoeInfo(A, isFcoeRpf_f, &dbg_ipe_fcoe_info, &fcoe_isFcoeRpf);
    GetDbgIpeFcoeInfo(A, l3DaResultValid_f, &dbg_ipe_fcoe_info, &fcoe_l3DaResultValid);
    GetDbgIpeFcoeInfo(A, l3SaResultValid_f, &dbg_ipe_fcoe_info, &fcoe_l3SaResultValid);
    GetDbgIpeFcoeInfo(A, l3DaLookupEn_f, &dbg_ipe_fcoe_info, &fcoe_l3DaLookupEn);
    GetDbgIpeFcoeInfo(A, l3SaLookupEn_f, &dbg_ipe_fcoe_info, &fcoe_l3SaLookupEn);
    GetDbgIpeFcoeInfo(A, valid_f, &dbg_ipe_fcoe_info, &fcoe_valid);
    /*DbgIpeTrillInfo*/
    sal_memset(&dbg_ipe_trill_info, 0, sizeof(dbg_ipe_trill_info));
    cmd = DRV_IOR(DbgIpeTrillInfo_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, slice, cmd, &dbg_ipe_trill_info);
    GetDbgIpeTrillInfo(A, l3DaResultValid_f, &dbg_ipe_trill_info, &trill_l3DaResultValid);
    GetDbgIpeTrillInfo(A, isTrillUcast_f, &dbg_ipe_trill_info, &trill_isTrillUcast);
    GetDbgIpeTrillInfo(A, isTrillMcast_f, &dbg_ipe_trill_info, &trill_isTrillMcast);
    GetDbgIpeTrillInfo(A, valid_f, &dbg_ipe_trill_info, &trill_valid);
    /*DbgIpeMacBridgingInfo*/
    sal_memset(&dbg_ipe_mac_bridging_info, 0, sizeof(dbg_ipe_mac_bridging_info));
    cmd = DRV_IOR(DbgIpeMacBridgingInfo_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, slice, cmd, &dbg_ipe_mac_bridging_info);
    GetDbgIpeMacBridgingInfo(A, macDaResultValid_f, &dbg_ipe_mac_bridging_info, &bridge_macDaResultValid);
    GetDbgIpeMacBridgingInfo(A, macDaDefaultEntryValid_f, &dbg_ipe_mac_bridging_info, &bridge_macDaDefaultEntryValid);
    GetDbgIpeMacBridgingInfo(A, discard_f, &dbg_ipe_mac_bridging_info, &bridge_discard);
    GetDbgIpeMacBridgingInfo(A, discardType_f, &dbg_ipe_mac_bridging_info, &bridge_discardType);
    GetDbgIpeMacBridgingInfo(A, exceptionEn_f, &dbg_ipe_mac_bridging_info, &bridge_exceptionEn);
    GetDbgIpeMacBridgingInfo(A, exceptionIndex_f, &dbg_ipe_mac_bridging_info, &bridge_exceptionIndex);
    GetDbgIpeMacBridgingInfo(A, exceptionSubIndex_f, &dbg_ipe_mac_bridging_info, &bridge_exceptionSubIndex);
    GetDbgIpeMacBridgingInfo(A, bridgePacket_f, &dbg_ipe_mac_bridging_info, &bridge_bridgePacket);
    GetDbgIpeMacBridgingInfo(A, valid_f, &dbg_ipe_mac_bridging_info, &bridge_valid);
    /*DbgIpeMacLearningInfo*/
    sal_memset(&dbg_ipe_mac_learning_info, 0, sizeof(dbg_ipe_mac_learning_info));
    cmd = DRV_IOR(DbgIpeMacLearningInfo_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, slice, cmd, &dbg_ipe_mac_learning_info);
    GetDbgIpeMacLearningInfo(A, macSaDefaultEntryValid_f, &dbg_ipe_mac_learning_info, &lrn_macSaDefaultEntryValid);
    GetDbgIpeMacLearningInfo(A, macSaHashConflict_f, &dbg_ipe_mac_learning_info, &lrn_macSaHashConflict);
    GetDbgIpeMacLearningInfo(A, macSaResultValid_f, &dbg_ipe_mac_learning_info, &lrn_macSaResultValid);
    GetDbgIpeMacLearningInfo(A, macSaLookupEn_f, &dbg_ipe_mac_learning_info, &lrn_macSaLookupEn);
    GetDbgIpeMacLearningInfo(A, fid_f, &dbg_ipe_mac_learning_info, &lrn_fid);
    GetDbgIpeMacLearningInfo(A, learningSrcPort_f, &dbg_ipe_mac_learning_info, &lrn_learningSrcPort);
    GetDbgIpeMacLearningInfo(A, isGlobalSrcPort_f, &dbg_ipe_mac_learning_info, &lrn_isGlobalSrcPort);
    GetDbgIpeMacLearningInfo(A, learningEn_f, &dbg_ipe_mac_learning_info, &lrn_learningEn);
    GetDbgIpeMacLearningInfo(A, discard_f, &dbg_ipe_mac_learning_info, &lrn_discard);
    GetDbgIpeMacLearningInfo(A, discardType_f, &dbg_ipe_mac_learning_info, &lrn_discardType);
    GetDbgIpeMacLearningInfo(A, valid_f, &dbg_ipe_mac_learning_info, &lrn_valid);

    if (0 == p_info->detail)
    {
        goto Discard;
    }

    /*route process*/
    if (route_valid
        && (0 == trill_isTrillUcast) && (0 == trill_isTrillMcast)
        && (0 == fcoe_isFcoe) && (0 == fcoe_isFcoeRpf))
    {
        if (route_l3DaLookupEn)
        {
            CTC_DKIT_PRINT("Route Process:\n");
            CTC_DKIT_PATH_PRINT("%-15s\n", "IP DA lookup--->");
            if (route_l3DaResultValid)
            {
                if ((!l3Da_lpm_l3DaTcamResultValid && l3Da_lpm_valid) || (!l3Da_lpm_valid))/*hash result*/
                {
                    CTC_DKIT_PATH_PRINT("%-15s:%s\n", "result", CTC_DKIT_CAPTURED_HASH_HIT);
#if 0 /* GG bug */
                    if (l3Da_l3DaHashResultValid)
                    {
                        CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "key index", l3Da_hitHashKeyIndexl3Da);

                        fib_acc_in.rw.key_index = l3Da_hitHashKeyIndexl3Da;
                        if (route_isIpv4Ucast || route_isIpv4Mcast)/* FIBDAKEYTYPE_IPV4UCAST or FIBDAKEYTYPE_IPV4MCAST */
                        {
                            fib_acc_in.rw.tbl_id = DsFibHost0Ipv4HashKey_t;
                            drv_goldengate_fib_acc(0, DRV_FIB_ACC_READ_FIB0_BY_IDX, &fib_acc_in, &fib_acc_out);
                            drv_goldengate_get_field(DsFibHost0Ipv4HashKey_t, DsFibHost0Ipv4HashKey_dsAdIndex_f, fib_acc_out.read.data, &ad_index);
                            key_name = "DsFibHost0Ipv4HashKey";
                        }
                        else if ((!route_isIpv4Ucast) && (!route_isIpv4Mcast) && route_isIpv6Ucast)/* FIBDAKEYTYPE_IPV6UCAST */
                        {
                            fib_acc_in.rw.tbl_id = DsFibHost0Ipv6UcastHashKey_t;
                            drv_goldengate_fib_acc(0, DRV_FIB_ACC_READ_FIB0_BY_IDX, &fib_acc_in, &fib_acc_out);
                            drv_goldengate_get_field(DsFibHost0Ipv6UcastHashKey_t, DsFibHost0Ipv6UcastHashKey_dsAdIndex_f, fib_acc_out.read.data, &ad_index);
                            key_name = "DsFibHost0Ipv6UcastHashKey";
                        }
                        else if ((!route_isIpv4Ucast) && (!route_isIpv4Mcast) && (!route_isIpv6Ucast) && route_isIpv6Mcast)/* FIBDAKEYTYPE_IPV6MCAST */
                        {
                            fib_acc_in.rw.tbl_id = DsFibHost0MacIpv6McastHashKey_t;
                            drv_goldengate_fib_acc(0, DRV_FIB_ACC_READ_FIB0_BY_IDX, &fib_acc_in, &fib_acc_out);
                            drv_goldengate_get_field(DsFibHost0MacIpv6McastHashKey_t, DsFibHost0MacIpv6McastHashKey_dsAdIndex_f, fib_acc_out.read.data, &ad_index);
                            key_name = "DsFibHost0MacIpv6McastHashKey";
                        }
                        else
                        {
                            /* TODO, PBR... */
                        }
                        CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "key index", key_index);
                        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "key name", l3Da_hitHashKeyIndexl3Da);
                        CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "AD index", ad_index);
                        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "AD name", "DsIpDa\n");
                    }
#endif
                }
                else if (l3Da_lpm_l3DaTcamResultValid && l3Da_lpm_valid)/*lpm result*/
                {

                    CTC_DKIT_PATH_PRINT("%-15s:%s\n", "result", "LPM tcam lookup hit");
                    key_index = l3Da_lpm_l3DaResultTcamEntryIndex;
                    if (route_isIpv4Ucast)
                    {
                        key_name = "DsLpmTcamIpv440Key";
                        ad_name = "DsLpmTcamIpv4Route40Ad";
                        cmd = DRV_IOR(DsLpmTcamIpv4Route40Ad_t, DsLpmTcamIpv4Route40Ad_nexthop_f);
                    }
                    else if (route_isIpv6Ucast)
                    {
                        key_name = "DsLpmTcamIpv6160Key0";
                        ad_name = "DsLpmTcamIpv4Route160Ad";
                        cmd = DRV_IOR(DsLpmTcamIpv4Route160Ad_t, DsLpmTcamIpv4Route160Ad_nexthop_f);
                    }
                    if (route_isIpv4Ucast)
                    {
                        CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "key index", key_index);
                        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "key name", key_name);
                        CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "tcam AD index", key_index);
                        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "tcam AD name", ad_name);
                        DRV_IOCTL(lchip, key_index, cmd, &ds_ip_da_idx);
                        CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "AD index", ds_ip_da_idx);
                        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "AD name", "DsIpDa\n");
                    }

                    if (l3Da_lpm_ipDaPipeline1DebugIndex != 0x3FFFF)
                    {
                        key_index = l3Da_lpm_ipDaPipeline1DebugIndex;
                        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "result", "LPM pipeline1 lookup hit");
                        cmd = DRV_IOR(DsLpmLookupKey_t, DsLpmLookupKey_type0_f);
                        DRV_IOCTL(lchip, key_index, cmd, &lpm_type0);
                        cmd = DRV_IOR(DsLpmLookupKey_t, DsLpmLookupKey_type1_f);
                        DRV_IOCTL(lchip, key_index, cmd, &lpm_type1);
                        if (lpm_type0 == 1)
                        {
                            cmd = DRV_IOR(DsLpmLookupKey_t, DsLpmLookupKey_nexthop0_f);
                            DRV_IOCTL(lchip, key_index, cmd, &ad_index);
                        }
                        else if (lpm_type1 == 1)
                        {
                            cmd = DRV_IOR(DsLpmLookupKey_t, DsLpmLookupKey_nexthop1_f);
                            DRV_IOCTL(lchip, key_index, cmd, &ad_index);
                        }
                        key_name = "DsLpmLookupKey";
                        CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "key index", key_index);
                        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "key name", key_name);
                        CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "AD index", ad_index);
                        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "AD name", "DsIpDa\n");
                    }
                    if (l3Da_lpm_ipDaPipeline2DebugIndex != 0x3FFFF)
                    {
                        key_index = l3Da_lpm_ipDaPipeline2DebugIndex;
                        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "result", "LPM pipeline2 lookup hit");
                        cmd = DRV_IOR(DsLpmLookupKey_t, DsLpmLookupKey_type0_f);
                        DRV_IOCTL(lchip, key_index, cmd, &lpm_type0);
                        cmd = DRV_IOR(DsLpmLookupKey_t, DsLpmLookupKey_type1_f);
                        DRV_IOCTL(lchip, key_index, cmd, &lpm_type1);
                        if (lpm_type0 == 1)
                        {
                            cmd = DRV_IOR(DsLpmLookupKey_t, DsLpmLookupKey_nexthop0_f);
                            DRV_IOCTL(lchip, key_index, cmd, &ad_index);
                        }
                        else if (lpm_type1 == 1)
                        {
                            cmd = DRV_IOR(DsLpmLookupKey_t, DsLpmLookupKey_nexthop1_f);
                            DRV_IOCTL(lchip, key_index, cmd, &ad_index);
                        }
                        key_name = "DsLpmLookupKey";
                        CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "key index", key_index);
                        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "key name", key_name);
                        CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "AD index", ad_index);
                        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "AD name", "DsIpDa\n");
                    }
                }
            }
            else
            {
                CTC_DKIT_PATH_PRINT("%-15s:%s\n", "result", CTC_DKIT_CAPTURED_NOT_HIT);
            }

        }

        if(route_l3SaLookupEn)
        {
            CTC_DKIT_PATH_PRINT("%-15s\n", "IP SA lookup--->");
            if (route_l3SaResultValid)
            {
                if (l3Sa_l3SaHashResultValid)
                {
                    if (route_isIpv4UcastNat)
                    {
                        result = "IPv4 Nat HASH lookup hit";
                        key_name = "DsFibHost1Ipv4NatSaPortHashKey";
                    }
                    else if (route_isIpv6UcastNat)
                    {
                        result = "IPv6 Nat HASH lookup hit";
                        key_name = "DsFibHost1Ipv6NatSaPortHashKey";
                    }
                    else
                    {
                        result = CTC_DKIT_CAPTURED_HASH_HIT;
                    }

                    CTC_DKIT_PATH_PRINT("%-15s:%s\n", "result", result);
                    if (route_isIpv4UcastNat)
                    {
                        CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "key index", l3Sa_hitHashKeyIndexl3Sa);
                        CTC_DKIT_PATH_PRINT("%-15s:%s\n\n", "key name", key_name);
                    }
                }

                if (pbr_valid && pbr_natPbrTcamResultValid)
                {
                    if (route_isIpv4Pbr)
                    {
                        result = "IPv4 PBR Tcam lookup hit";
                        key_name = "DsLpmTcamIpv4Pbr160Key";
                        ad_name = "DsLpmTcamIpv4Pbr160Ad";
                    }
                    else if (route_isIpv6Pbr)
                    {
                        result = "IPv6 PBR Tcam lookup hit";
                        key_name = "DsLpmTcamIpv6320Key";
                        ad_name = "DsLpmTcamIpv6Pbr320Ad";
                    }
                    else if (route_isIpv4UcastNat)
                    {
                        result = "IPv4 Nat Tcam lookup hit";
                        key_name = "DsLpmTcamIpv4NAT160Key";
                        ad_name = "DsLpmTcamIpv4Nat160Ad";
                    }
                    else if (route_isIpv6UcastNat)
                    {
                        result = "IPv6 Nat Tcam lookup hit";
                        key_name = "DsLpmTcamIpv6160Key1";
                        ad_name = "DsLpmTcamIpv6Nat160Ad";
                    }
                    CTC_DKIT_PATH_PRINT("%-15s:%s\n", "result", result);
                    if (route_isIpv4Pbr || route_isIpv4UcastNat)
                    {
                        CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "key index", pbr_natPbrResultTcamEntryIndex);
                        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "key name", key_name);
                        CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "tcam AD index", pbr_natPbrResultTcamEntryIndex);
                        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "tcam AD name", ad_name);
                    }
                }
            }
            else
            {
                CTC_DKIT_PATH_PRINT("%-15s:%s\n", "result", CTC_DKIT_CAPTURED_NOT_HIT);
            }

        }

    }

    /*fcoe process*/
    if (fcoe_valid && ((fcoe_isFcoe && fcoe_l3DaLookupEn) || (fcoe_isFcoeRpf && fcoe_l3SaLookupEn)))
    {
        CTC_DKIT_PRINT("FCoE Process:\n");
        if (fcoe_isFcoe && fcoe_l3DaLookupEn)
        {
            CTC_DKIT_PATH_PRINT("%-15s\n", "FCoE DA lookup--->");
            if (fcoe_l3DaResultValid)
            {
                fib_acc_in.rw.key_index = macda_hitHashKeyIndexMacDa;
                fib_acc_in.rw.tbl_id = DsFibHost0FcoeHashKey_t;
                drv_goldengate_fib_acc(lchip, DRV_FIB_ACC_READ_FIB0_BY_IDX, &fib_acc_in, &fib_acc_out);
                GetDsFibHost0FcoeHashKey(A, dsAdIndex_f, fib_acc_out.read.data, &ad_index);
                CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "key index", macda_hitHashKeyIndexMacDa);
                CTC_DKIT_PATH_PRINT("%-15s:%s\n", "key name", "DsFibHost0FcoeHashKey");
                CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "AD index", ad_index);
                CTC_DKIT_PATH_PRINT("%-15s:%s\n", "AD name", "DsFcoeDa");
            }
            else
            {
                CTC_DKIT_PATH_PRINT("%-15s:%s\n", "result", CTC_DKIT_CAPTURED_NOT_HIT);
            }
        }
        if (fcoe_isFcoeRpf && fcoe_l3SaLookupEn)
        {
            CTC_DKIT_PATH_PRINT("%-15s\n", "FCoE SA lookup--->");
            if (fcoe_l3SaResultValid)
            {
                cpu_acc_in.tbl_id = DsFibHost1FcoeRpfHashKey_t;
                cpu_acc_in.key_index = l3Sa_hitHashKeyIndexl3Sa;
                drv_goldengate_cpu_acc(lchip, DRV_CPU_READ_ACC_FIB_HOST1_BY_IDX, &cpu_acc_in, &cpu_acc_out);
                GetDsFibHost1FcoeRpfHashKey(A, dsAdIndex_f, cpu_acc_out.data, &ad_index);
                CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "key index", l3Sa_hitHashKeyIndexl3Sa);
                CTC_DKIT_PATH_PRINT("%-15s:%s\n", "key name", "DsFibHost1FcoeRpfHashKey");
                CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "AD index", ad_index);
                CTC_DKIT_PATH_PRINT("%-15s:%s\n", "AD name", "DsFcoeSa");
            }
            else
            {
                CTC_DKIT_PATH_PRINT("%-15s:%s\n", "result", CTC_DKIT_CAPTURED_NOT_HIT);
            }
        }
    }


    /*trill process*/
    if (trill_valid)
    {
        if (trill_l3DaResultValid && (trill_isTrillUcast || trill_isTrillMcast))
        {
            CTC_DKIT_PRINT("TRILL Process:\n");
            if ((macda_macDaHashResultValid)||(l3Da_l3DaHashResultValid))
            {
                CTC_DKIT_PATH_PRINT("%-15s:%s\n", "result", CTC_DKIT_CAPTURED_HASH_HIT);
                if (macda_macDaHashResultValid)
                {
                    CTC_DKIT_PATH_PRINT("%-15s:%s\n", "TRILL Mcast", trill_isTrillUcast? "NO" : "YES");
                    fib_acc_in.rw.key_index = macda_hitHashKeyIndexMacDa;
                    fib_acc_in.rw.tbl_id = DsFibHost0TrillHashKey_t;
                    drv_goldengate_fib_acc(lchip, DRV_FIB_ACC_READ_FIB0_BY_IDX, &fib_acc_in, &fib_acc_out);
                    GetDsFibHost0TrillHashKey(A, dsAdIndex_f, fib_acc_out.read.data, &ad_index);
                    key_name = "DsFibHost0TrillHashKey";
                    key_index = macda_hitHashKeyIndexMacDa;
                }
                else if (trill_isTrillMcast && l3Da_l3DaHashResultValid)
                {
                    CTC_DKIT_PATH_PRINT("%-15s:%s\n", "TRILL Mcast", "YES");
                    cpu_acc_in.tbl_id = DsFibHost1TrillMcastVlanHashKey_t;
                    cpu_acc_in.key_index = l3Da_hitHashKeyIndexl3Da;
                    drv_goldengate_cpu_acc(lchip, DRV_CPU_READ_ACC_FIB_HOST1_BY_IDX, &cpu_acc_in, &cpu_acc_out);
                    GetDsFibHost1TrillMcastVlanHashKey(A, dsAdIndex_f, cpu_acc_out.data, &ad_index);
                    key_name = "DsFibHost1TrillMcastVlanHashKey";
                    key_index = l3Da_hitHashKeyIndexl3Da;
                }
                CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "key index", key_index);
                CTC_DKIT_PATH_PRINT("%-15s:%s\n", "key name", key_name);
                CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "AD index", ad_index);
                CTC_DKIT_PATH_PRINT("%-15s:%s\n", "AD name", "DsTrillDa");
            }
            else
            {
                CTC_DKIT_PATH_PRINT("%-15s:%s\n", "result", "No process");
            }
        }
    }



    /*bridge process*/
    if (bridge_valid)
    {
        if (p_info->is_bsr_captured)
        {
            sal_memset(&dbg_fwd_buf_store_info, 0, sizeof(dbg_fwd_buf_store_info));
            cmd = DRV_IOR(DbgFwdBufStoreInfo_t, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &dbg_fwd_buf_store_info);
            GetDbgFwdBufStoreInfo(A, valid_f, &dbg_fwd_buf_store_info, &hdr_valid);
            GetDbgFwdBufStoreInfo(A, fid_f, &dbg_fwd_buf_store_info, &hdr_fid);
        }

        if (lkp_macDaLookupEn && bridge_bridgePacket)
        {
            CTC_DKIT_PRINT("Bridge Process:\n");
            CTC_DKIT_PATH_PRINT("%-15s\n", "MAC DA lookup--->");
            if (bridge_macDaResultValid)
            {
                 if (hdr_valid)
                 {
                     CTC_DKIT_PATH_PRINT("%-15s:%d\n", "FID", hdr_fid);
                 }
                if (macda_blackHoleMacDaResultValid)
                {
                    CTC_DKIT_PATH_PRINT("%-15s:%s\n", "result", "hit black hole entry");
                    CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "key index", macda_blackHoleHitMacDaKeyIndex);
                    CTC_DKIT_PATH_PRINT("%-15s:%s\n", "key name", "DsFibMacBlackHoleHashKey");
                    cmd = DRV_IOR(DsFibMacBlackHoleHashKey_t, DsFibMacBlackHoleHashKey_dsAdIndex_f);
                    DRV_IOCTL(lchip, macda_blackHoleHitMacDaKeyIndex, cmd, &ad_index);
                    CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "AD index", ad_index);
                    CTC_DKIT_PATH_PRINT("%-15s:%s\n", "AD name", "DsMac");
                }
                else if (bridge_macDaDefaultEntryValid)
                {
                    cmd = DRV_IOR(FibEngineLookupResultCtl_t, FibEngineLookupResultCtl_gMacDaLookupResultCtl_defaultEntryBase_f);
                    DRV_IOCTL(lchip, 0, cmd, &defaultEntryBase);
                    CTC_DKIT_PATH_PRINT("%-15s:%s\n", "result", CTC_DKIT_CAPTURED_DEFAULT);
                    CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "AD index", (hdr_fid + (defaultEntryBase << 8)));
                    CTC_DKIT_PATH_PRINT("%-15s:%s\n", "AD name", "DsMac");
                }
                else if (macda_valid && macda_macDaHashResultValid)
                {
                    CTC_DKIT_PATH_PRINT("%-15s:%s\n", "result", CTC_DKIT_CAPTURED_HASH_HIT);

                    CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "key index", macda_hitHashKeyIndexMacDa);
                    CTC_DKIT_PATH_PRINT("%-15s:%s\n", "key name", "DsFibHost0MacHashKey");
                    fib_acc_in.rw.key_index = macda_hitHashKeyIndexMacDa;
                    fib_acc_in.rw.tbl_id = DsFibHost0MacHashKey_t;
                    drv_goldengate_fib_acc(lchip, DRV_FIB_ACC_READ_FIB0_BY_IDX, &fib_acc_in, &fib_acc_out);
                    GetDsFibHost0MacHashKey(A, dsAdIndex_f, fib_acc_out.read.data, &ad_index);
                    CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "AD index", ad_index);
                    CTC_DKIT_PATH_PRINT("%-15s:%s\n", "AD name", "DsMac");
                }
                else
                {
                    CTC_DKIT_PATH_PRINT("%-15s:%s\n", "result", CTC_DKIT_CAPTURED_NOT_HIT);
                }
            }
            else
            {
                CTC_DKIT_PATH_PRINT("%-15s:%s\n", "result", CTC_DKIT_CAPTURED_NOT_HIT);
            }
        }

        if (lrn_valid && lrn_macSaLookupEn&&bridge_bridgePacket)
        {
            CTC_DKIT_PATH_PRINT("%-15s\n", "MAC SA lookup--->");
            if (hdr_valid)
            {
                CTC_DKIT_PATH_PRINT("%-15s:%d\n", "FID", hdr_fid);
            }
            if (lrn_macSaDefaultEntryValid)
            {
                CTC_DKIT_PATH_PRINT("%-15s:%s\n", "result", CTC_DKIT_CAPTURED_DEFAULT);
            }
            else if (lrn_macSaHashConflict)
            {
                CTC_DKIT_PATH_PRINT("%-15s:%s\n", "result", CTC_DKIT_CAPTURED_CONFLICT);
            }
            else if (lrn_macSaResultValid)
            {
                CTC_DKIT_PATH_PRINT("%-15s:%s\n", "result", CTC_DKIT_CAPTURED_HASH_HIT);
            }
            else
            {
                CTC_DKIT_PATH_PRINT("%-15s:%s\n", "result", CTC_DKIT_CAPTURED_NOT_HIT);
            }
            if (lrn_learningEn)
            {
                CTC_DKIT_PATH_PRINT("%-15s\n", "MAC SA learning--->");
                CTC_DKIT_PATH_PRINT("%-15s:%d\n", "FID", lrn_fid);
                if (!lrn_isGlobalSrcPort)
                {
                    CTC_DKIT_PATH_PRINT("%-15s:%d\n", "logic port", lrn_learningSrcPort);
                }
                else
                {
                    CTC_DKIT_PATH_PRINT("%-15s:0x%04x\n", "source gport", CTC_DKIT_DRV_GPORT_TO_CTC_GPORT(lrn_learningSrcPort));
                }
            }
        }

    }

    if (route_discard)
    {
        discard = 1;
        discard_type = route_discardType;
    }
    else if (bridge_discard)
    {
        discard = 1;
        discard_type = bridge_discardType;
    }
    else if (lrn_discard)
    {
        discard = 1;
        discard_type = lrn_discardType;
    }

    if (lkp_exceptionEn)
    {
        exceptionEn = 1;
        exceptionIndex = lkp_exceptionIndex;
        exceptionSubIndex = lkp_exceptionSubIndex;
    }
    else if (route_exceptionEn)
    {
        exceptionEn = 1;
        exceptionIndex = route_exceptionIndex;
        exceptionSubIndex = route_exceptionSubIndex;
    }
    else if (bridge_exceptionEn)
    {
        exceptionEn = 1;
        exceptionIndex = bridge_exceptionIndex;
        exceptionSubIndex = bridge_exceptionSubIndex;
    }

Discard:
    _ctc_goldengate_dkit_captured_path_discard_process(p_info, discard, discard_type, CTC_DKIT_IPE);
    _ctc_goldengate_dkit_captured_path_exception_process(p_info, exceptionEn, exceptionIndex, exceptionSubIndex);

    return CLI_SUCCESS;
}

STATIC int32
_ctc_goldengate_dkit_captured_path_ipe_oam(ctc_dkit_captured_path_info_t* p_info)
{
    uint8 slice = p_info->slice_ipe;
    uint8 lchip = p_info->lchip;
    uint32 cmd = 0;
    uint8 i = 0;
    char* lookup_result[4] = {"ETHER lookup--->", "BFD lookup--->", "TP lookup--->", "Section lookup--->"};
    char* key_name[4] = {"DsEgressXcOamEthHashKey", "DsEgressXcOamBfdHashKey",
                                             "DsEgressXcOamMplsLabelHashKey", "DsEgressXcOamMplsSectionHashKey"};
    /*DbgEgrXcOamHashEngineFromIpeOam0Info*/
    uint32 hash0_resultValid = 0;
    uint32 hash0_egressXcOamHashConflict = 0;
    uint32 hash0_keyIndex = 0;
    uint32 hash0_valid = 0;
    DbgEgrXcOamHashEngineFromIpeOam0Info_m dbg_egr_xc_oam_hash_engine_from_ipe_oam0_info;
    /*DbgEgrXcOamHashEngineFromIpeOam1Info*/
    uint32 hash1_resultValid = 0;
    uint32 hash1_egressXcOamHashConflict = 0;
    uint32 hash1_keyIndex = 0;
    uint32 hash1_valid = 0;
    DbgEgrXcOamHashEngineFromIpeOam1Info_m dbg_egr_xc_oam_hash_engine_from_ipe_oam1_info;
    /*DbgEgrXcOamHashEngineFromIpeOam2Info*/
    uint32 hash2_resultValid = 0;
    uint32 hash2_egressXcOamHashConflict = 0;
    uint32 hash2_keyIndex = 0;
    uint32 hash2_valid = 0;
    DbgEgrXcOamHashEngineFromIpeOam2Info_m dbg_egr_xc_oam_hash_engine_from_ipe_oam2_info;
    /*DbgIpeLkpMgrInfo*/
    uint32 lkp_oamUseFid = 0;
    uint32 lkp_mplsLmValid = 0;
    uint32 lkp_mplsSectionLmValid = 0;
    uint32 lkp_ingressOamKeyType0 = 0;
    uint32 lkp_ingressOamKeyType1 = 0;
    uint32 lkp_ingressOamKeyType2 = 0;
    uint32 lkp_valid = 0;
    DbgIpeLkpMgrInfo_m dbg_ipe_lkp_mgr_info;
    /*DbgIpeOamInfo*/
    uint32 oam_oamLookupEn = 0;
    uint32 oam_lmStatsEn0 = 0;
    uint32 oam_lmStatsEn1 = 0;
    uint32 oam_lmStatsEn2 = 0;
    uint32 oam_lmStatsIndex = 0;
    uint32 oam_lmStatsPtr0 = 0;
    uint32 oam_lmStatsPtr1 = 0;
    uint32 oam_lmStatsPtr2 = 0;
    uint32 oam_etherLmValid = 0;
    uint32 oam_mplsLmValid = 0;
    uint32 oam_oamDestChipId = 0;
    uint32 oam_mepIndex = 0;
    uint32 oam_mepEn = 0;
    uint32 oam_mipEn = 0;
    uint32 oam_tempRxOamType = 0;
    uint32 oam_valid = 0;
    DbgIpeOamInfo_m dbg_ipe_oam_info;
    uint8 key_type[3] = {lkp_ingressOamKeyType0, lkp_ingressOamKeyType1, lkp_ingressOamKeyType2};
    uint8 hash_valid[3] = {hash0_valid, hash1_valid, hash2_valid};
    uint8 confict[3] = {hash0_egressXcOamHashConflict, hash1_egressXcOamHashConflict, hash2_egressXcOamHashConflict};
    uint32 key_index[3] = {hash0_keyIndex, hash1_keyIndex, hash2_keyIndex};
    uint8 hash_resultValid[3] = {hash0_resultValid,hash1_resultValid,hash2_resultValid};

    /*DbgEgrXcOamHashEngineFromIpeOam0Info*/
    sal_memset(&dbg_egr_xc_oam_hash_engine_from_ipe_oam0_info, 0, sizeof(dbg_egr_xc_oam_hash_engine_from_ipe_oam0_info));
    cmd = DRV_IOR(DbgEgrXcOamHashEngineFromIpeOam0Info_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, slice, cmd, &dbg_egr_xc_oam_hash_engine_from_ipe_oam0_info);
    GetDbgEgrXcOamHashEngineFromIpeOam0Info(A, resultValid_f, &dbg_egr_xc_oam_hash_engine_from_ipe_oam0_info, &hash0_resultValid);
    GetDbgEgrXcOamHashEngineFromIpeOam0Info(A, egressXcOamHashConflict_f, &dbg_egr_xc_oam_hash_engine_from_ipe_oam0_info, &hash0_egressXcOamHashConflict);
    GetDbgEgrXcOamHashEngineFromIpeOam0Info(A, keyIndex_f, &dbg_egr_xc_oam_hash_engine_from_ipe_oam0_info, &hash0_keyIndex);
    GetDbgEgrXcOamHashEngineFromIpeOam0Info(A, valid_f, &dbg_egr_xc_oam_hash_engine_from_ipe_oam0_info, &hash0_valid);
    /*DbgEgrXcOamHashEngineFromIpeOam1Info*/
    sal_memset(&dbg_egr_xc_oam_hash_engine_from_ipe_oam1_info, 0, sizeof(dbg_egr_xc_oam_hash_engine_from_ipe_oam1_info));
    cmd = DRV_IOR(DbgEgrXcOamHashEngineFromIpeOam1Info_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, slice, cmd, &dbg_egr_xc_oam_hash_engine_from_ipe_oam1_info);
    GetDbgEgrXcOamHashEngineFromIpeOam1Info(A, resultValid_f, &dbg_egr_xc_oam_hash_engine_from_ipe_oam1_info, &hash1_resultValid);
    GetDbgEgrXcOamHashEngineFromIpeOam1Info(A, egressXcOamHashConflict_f, &dbg_egr_xc_oam_hash_engine_from_ipe_oam1_info, &hash1_egressXcOamHashConflict);
    GetDbgEgrXcOamHashEngineFromIpeOam1Info(A, keyIndex_f, &dbg_egr_xc_oam_hash_engine_from_ipe_oam1_info, &hash1_keyIndex);
    GetDbgEgrXcOamHashEngineFromIpeOam1Info(A, valid_f, &dbg_egr_xc_oam_hash_engine_from_ipe_oam1_info, &hash1_valid);
    /*DbgEgrXcOamHashEngineFromIpeOam2Info*/
    sal_memset(&dbg_egr_xc_oam_hash_engine_from_ipe_oam2_info, 0, sizeof(dbg_egr_xc_oam_hash_engine_from_ipe_oam2_info));
    cmd = DRV_IOR(DbgEgrXcOamHashEngineFromIpeOam2Info_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, slice, cmd, &dbg_egr_xc_oam_hash_engine_from_ipe_oam2_info);
    GetDbgEgrXcOamHashEngineFromIpeOam2Info(A, resultValid_f, &dbg_egr_xc_oam_hash_engine_from_ipe_oam2_info, &hash2_resultValid);
    GetDbgEgrXcOamHashEngineFromIpeOam2Info(A, egressXcOamHashConflict_f, &dbg_egr_xc_oam_hash_engine_from_ipe_oam2_info, &hash2_egressXcOamHashConflict);
    GetDbgEgrXcOamHashEngineFromIpeOam2Info(A, keyIndex_f, &dbg_egr_xc_oam_hash_engine_from_ipe_oam2_info, &hash2_keyIndex);
    GetDbgEgrXcOamHashEngineFromIpeOam2Info(A, valid_f, &dbg_egr_xc_oam_hash_engine_from_ipe_oam2_info, &hash2_valid);
    /*DbgIpeLkpMgrInfo*/
    sal_memset(&dbg_ipe_lkp_mgr_info, 0, sizeof(dbg_ipe_lkp_mgr_info));
    cmd = DRV_IOR(DbgIpeLkpMgrInfo_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, slice, cmd, &dbg_ipe_lkp_mgr_info);
    GetDbgIpeLkpMgrInfo(A, oamUseFid_f, &dbg_ipe_lkp_mgr_info, &lkp_oamUseFid);
    GetDbgIpeLkpMgrInfo(A, mplsLmValid_f, &dbg_ipe_lkp_mgr_info, &lkp_mplsLmValid);
    GetDbgIpeLkpMgrInfo(A, mplsSectionLmValid_f, &dbg_ipe_lkp_mgr_info, &lkp_mplsSectionLmValid);
    GetDbgIpeLkpMgrInfo(A, ingressOamKeyType0_f, &dbg_ipe_lkp_mgr_info, &lkp_ingressOamKeyType0);
    GetDbgIpeLkpMgrInfo(A, ingressOamKeyType1_f, &dbg_ipe_lkp_mgr_info, &lkp_ingressOamKeyType1);
    GetDbgIpeLkpMgrInfo(A, ingressOamKeyType2_f, &dbg_ipe_lkp_mgr_info, &lkp_ingressOamKeyType2);
    GetDbgIpeLkpMgrInfo(A, valid_f, &dbg_ipe_lkp_mgr_info, &lkp_valid);
    /*DbgIpeOamInfo*/
    sal_memset(&dbg_ipe_oam_info, 0, sizeof(dbg_ipe_oam_info));
    cmd = DRV_IOR(DbgIpeOamInfo_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, slice, cmd, &dbg_ipe_oam_info);
    GetDbgIpeOamInfo(A, oamLookupEn_f, &dbg_ipe_oam_info, &oam_oamLookupEn);
    GetDbgIpeOamInfo(A, lmStatsEn0_f, &dbg_ipe_oam_info, &oam_lmStatsEn0);
    GetDbgIpeOamInfo(A, lmStatsEn1_f, &dbg_ipe_oam_info, &oam_lmStatsEn1);
    GetDbgIpeOamInfo(A, lmStatsEn2_f, &dbg_ipe_oam_info, &oam_lmStatsEn2);
    GetDbgIpeOamInfo(A, lmStatsIndex_f, &dbg_ipe_oam_info, &oam_lmStatsIndex);
    GetDbgIpeOamInfo(A, lmStatsPtr0_f, &dbg_ipe_oam_info, &oam_lmStatsPtr0);
    GetDbgIpeOamInfo(A, lmStatsPtr1_f, &dbg_ipe_oam_info, &oam_lmStatsPtr1);
    GetDbgIpeOamInfo(A, lmStatsPtr2_f, &dbg_ipe_oam_info, &oam_lmStatsPtr2);
    GetDbgIpeOamInfo(A, etherLmValid_f, &dbg_ipe_oam_info, &oam_etherLmValid);
    GetDbgIpeOamInfo(A, mplsLmValid_f, &dbg_ipe_oam_info, &oam_mplsLmValid);
    GetDbgIpeOamInfo(A, oamDestChipId_f, &dbg_ipe_oam_info, &oam_oamDestChipId);
    GetDbgIpeOamInfo(A, mepIndex_f, &dbg_ipe_oam_info, &oam_mepIndex);
    GetDbgIpeOamInfo(A, mepEn_f, &dbg_ipe_oam_info, &oam_mepEn);
    GetDbgIpeOamInfo(A, mipEn_f, &dbg_ipe_oam_info, &oam_mipEn);
    GetDbgIpeOamInfo(A, tempRxOamType_f, &dbg_ipe_oam_info, &oam_tempRxOamType);
    GetDbgIpeOamInfo(A, valid_f, &dbg_ipe_oam_info, &oam_valid);

    if (0 == p_info->detail)
    {
        goto Discard;
    }

    if (lkp_ingressOamKeyType0 || lkp_ingressOamKeyType1 || lkp_ingressOamKeyType2)
    {

        CTC_DKIT_PRINT("OAM Process:\n");

        for (i = 0; i < 3; i++)
        {
            if (key_type[i])
            {
                CTC_DKIT_PATH_PRINT("%s%d %s\n", "hash",i,lookup_result[key_type[i] - EGRESSXCOAMHASHTYPE_ETH]);
                if (hash_valid[i])
                {
                    if (confict[i])
                    {
                        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "result", CTC_DKIT_CAPTURED_CONFLICT);
                        CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "key index", key_index[i]);
                        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "key name", key_name[key_type[i] - EGRESSXCOAMHASHTYPE_ETH]);
                    }
                    else if (hash_resultValid[i])
                    {
                        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "result", CTC_DKIT_CAPTURED_HASH_HIT);
                        CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "key index", key_index[i]);
                        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "key name", key_name[key_type[i] - EGRESSXCOAMHASHTYPE_ETH]);
                    }
                    else
                    {
                        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "result", CTC_DKIT_CAPTURED_NOT_HIT);
                    }
                }
            }
        }
    }

    if (oam_valid)
    {
        if (hash0_resultValid || hash1_resultValid || hash2_resultValid)
        {
            if (oam_mepEn)
            {
                CTC_DKIT_PATH_PRINT("%-15s:%s\n", "hit MEP", "YES");
                CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "mep index", oam_mepIndex);
                CTC_DKIT_PATH_PRINT("%-15s:%d\n", "Master chip id", oam_oamDestChipId);
            }
            if (oam_mipEn)
            {
                CTC_DKIT_PATH_PRINT("%-15s:%s\n", "hit MIP", "YES");
                CTC_DKIT_PATH_PRINT("%-15s:%d\n", "Master chip id", oam_oamDestChipId);
            }
            if (oam_tempRxOamType)
            {
                char* oam_type[RXOAMTYPE_TRILLBFD] =
                {
                    "ETHER OAM", "IP BFD", "PBT OAM", "PBBBSI", "PBBBV",
                                                                                        "MPLS OAM", "MPLS BFD", "ACH OAM", "RSV", "TRILL BFD"};
                    CTC_DKIT_PATH_PRINT("%-15s:%s\n", "OAM type", oam_type[oam_tempRxOamType - 1]);
                }
                if (oam_mplsLmValid || oam_etherLmValid)
                {
                    CTC_DKIT_PATH_PRINT("%-15s:%s 0x%x\n", "read LM from", "DsOamLmStats", oam_lmStatsIndex);
                }
                if (oam_lmStatsEn0)
                {
                    CTC_DKIT_PATH_PRINT("%-15s:%s 0x%x\n", "write LM to", "DsOamLmStats", oam_lmStatsPtr0);
                }
                if (oam_lmStatsEn1)
                {
                    CTC_DKIT_PATH_PRINT("%-15s:%s 0x%x\n", "write LM to", "DsOamLmStats", oam_lmStatsPtr1);
                }
                if (oam_lmStatsEn2)
                {
                    CTC_DKIT_PATH_PRINT("%-15s:%s 0x%x\n", "write LM to", "DsOamLmStats", oam_lmStatsPtr2);
                }
            }
        }

Discard:

    return CLI_SUCCESS;
}

STATIC int32
 _ctc_goldengate_dkit_captured_path_ipe_destination(ctc_dkit_captured_path_info_t* p_info)
 {
     uint8 slice = p_info->slice_ipe;
     uint8 lchip = p_info->lchip;
     uint32 cmd = 0;
     uint8 is_mcast = 0;
     uint8 is_link_agg = 0;
     uint16 dest_port = 0;
     uint32 rx_oam_type = 0;
     uint8 dest_chip = 0;
     uint16 gport = 0;
     uint32 dsNextHopInternalBase = 0;
     uint8 fwd_to_cpu = 0;
     /*DbgIpeEcmpProcInfo*/
     uint32 ecmp_ecmpGroupId = 0;
     uint32 ecmp_ecmpEn = 0;
     uint32 ecmp_valid = 0;
     DbgIpeEcmpProcInfo_m dbg_ipe_ecmp_proc_info;
     /*DbgIpeForwardingInfo*/
     uint32 fwd_fwdStatsValid = 0;
     uint32 fwd_ingressHeaderValid = 0;
     uint32 fwd_timestampValid = 0;
     uint32 fwd_isL2Ptp = 0;
     uint32 fwd_isL4Ptp = 0;
     uint32 fwd_ptpUpdateResidenceTime = 0;
     uint32 fwd_muxDestinationValid = 0;
     uint32 fwd_isNewElephant = 0;
     uint32 fwd_srcPortStatsEn = 0;
     uint32 fwd_acl0StatsValid = 0;
     uint32 fwd_acl1StatsValid = 0;
     uint32 fwd_acl2StatsValid = 0;
     uint32 fwd_acl3StatsValid = 0;
     uint32 fwd_ifStatsValid = 0;
     uint32 fwd_ipfixFlowLookupEn = 0;
     uint32 fwd_shareType = 0;
     uint32 fwd_discardType = 0;
     uint32 fwd_exceptionEn = 0;
     uint32 fwd_exceptionIndex = 0;
     uint32 fwd_exceptionSubIndex = 0;
     uint32 fwd_fatalExceptionValid = 0;
     uint32 fwd_fatalException = 0;
     uint32 fwd_apsSelectValid0 = 0;
     uint32 fwd_apsSelectGroupId0 = 0;
     uint32 fwd_apsSelectValid1 = 0;
     uint32 fwd_apsSelectGroupId1 = 0;
     uint32 fwd_discard = 0;
     uint32 fwd_destMap = 0;
     uint32 fwd_apsBridgeEn = 0;
     uint32 fwd_nextHopExt = 0;
     uint32 fwd_nextHopIndex = 0;
     uint32 fwd_headerHash = 0;
     uint32 fwd_valid = 0;
     DbgIpeForwardingInfo_m dbg_ipe_forwarding_info;

     /*DbgIpeEcmpProcInfo*/
     sal_memset(&dbg_ipe_ecmp_proc_info, 0, sizeof(dbg_ipe_ecmp_proc_info));
     cmd = DRV_IOR(DbgIpeEcmpProcInfo_t, DRV_ENTRY_FLAG);
     DRV_IOCTL(lchip, slice, cmd, &dbg_ipe_ecmp_proc_info);
     GetDbgIpeEcmpProcInfo(A, ecmpGroupId_f, &dbg_ipe_ecmp_proc_info, &ecmp_ecmpGroupId);
     GetDbgIpeEcmpProcInfo(A, ecmpEn_f, &dbg_ipe_ecmp_proc_info, &ecmp_ecmpEn);
     GetDbgIpeEcmpProcInfo(A, valid_f, &dbg_ipe_ecmp_proc_info, &ecmp_valid);
     /*DbgIpeForwardingInfo*/
     sal_memset(&dbg_ipe_forwarding_info, 0, sizeof(dbg_ipe_forwarding_info));
     cmd = DRV_IOR(DbgIpeForwardingInfo_t, DRV_ENTRY_FLAG);
     DRV_IOCTL(lchip, slice, cmd, &dbg_ipe_forwarding_info);
     GetDbgIpeForwardingInfo(A, fwdStatsValid_f, &dbg_ipe_forwarding_info, &fwd_fwdStatsValid);
     GetDbgIpeForwardingInfo(A, ingressHeaderValid_f, &dbg_ipe_forwarding_info, &fwd_ingressHeaderValid);
     GetDbgIpeForwardingInfo(A, timestampValid_f, &dbg_ipe_forwarding_info, &fwd_timestampValid);
     GetDbgIpeForwardingInfo(A, isL2Ptp_f, &dbg_ipe_forwarding_info, &fwd_isL2Ptp);
     GetDbgIpeForwardingInfo(A, isL4Ptp_f, &dbg_ipe_forwarding_info, &fwd_isL4Ptp);
     GetDbgIpeForwardingInfo(A, ptpUpdateResidenceTime_f, &dbg_ipe_forwarding_info, &fwd_ptpUpdateResidenceTime);
     GetDbgIpeForwardingInfo(A, muxDestinationValid_f, &dbg_ipe_forwarding_info, &fwd_muxDestinationValid);
     GetDbgIpeForwardingInfo(A, isNewElephant_f, &dbg_ipe_forwarding_info, &fwd_isNewElephant);
     GetDbgIpeForwardingInfo(A, srcPortStatsEn_f, &dbg_ipe_forwarding_info, &fwd_srcPortStatsEn);
     GetDbgIpeForwardingInfo(A, acl0StatsValid_f, &dbg_ipe_forwarding_info, &fwd_acl0StatsValid);
     GetDbgIpeForwardingInfo(A, acl1StatsValid_f, &dbg_ipe_forwarding_info, &fwd_acl1StatsValid);
     GetDbgIpeForwardingInfo(A, acl2StatsValid_f, &dbg_ipe_forwarding_info, &fwd_acl2StatsValid);
     GetDbgIpeForwardingInfo(A, acl3StatsValid_f, &dbg_ipe_forwarding_info, &fwd_acl3StatsValid);
     GetDbgIpeForwardingInfo(A, ifStatsValid_f, &dbg_ipe_forwarding_info, &fwd_ifStatsValid);
     GetDbgIpeForwardingInfo(A, ipfixFlowLookupEn_f, &dbg_ipe_forwarding_info, &fwd_ipfixFlowLookupEn);
     GetDbgIpeForwardingInfo(A, shareType_f, &dbg_ipe_forwarding_info, &fwd_shareType);
     GetDbgIpeForwardingInfo(A, discardType_f, &dbg_ipe_forwarding_info, &fwd_discardType);
     GetDbgIpeForwardingInfo(A, exceptionEn_f, &dbg_ipe_forwarding_info, &fwd_exceptionEn);
     GetDbgIpeForwardingInfo(A, exceptionIndex_f, &dbg_ipe_forwarding_info, &fwd_exceptionIndex);
     GetDbgIpeForwardingInfo(A, exceptionSubIndex_f, &dbg_ipe_forwarding_info, &fwd_exceptionSubIndex);
     GetDbgIpeForwardingInfo(A, fatalExceptionValid_f, &dbg_ipe_forwarding_info, &fwd_fatalExceptionValid);
     GetDbgIpeForwardingInfo(A, fatalException_f, &dbg_ipe_forwarding_info, &fwd_fatalException);
     GetDbgIpeForwardingInfo(A, apsSelectValid0_f, &dbg_ipe_forwarding_info, &fwd_apsSelectValid0);
     GetDbgIpeForwardingInfo(A, apsSelectGroupId0_f, &dbg_ipe_forwarding_info, &fwd_apsSelectGroupId0);
     GetDbgIpeForwardingInfo(A, apsSelectValid1_f, &dbg_ipe_forwarding_info, &fwd_apsSelectValid1);
     GetDbgIpeForwardingInfo(A, apsSelectGroupId1_f, &dbg_ipe_forwarding_info, &fwd_apsSelectGroupId1);
     GetDbgIpeForwardingInfo(A, discard_f, &dbg_ipe_forwarding_info, &fwd_discard);
     GetDbgIpeForwardingInfo(A, destMap_f, &dbg_ipe_forwarding_info, &fwd_destMap);
     GetDbgIpeForwardingInfo(A, apsBridgeEn_f, &dbg_ipe_forwarding_info, &fwd_apsBridgeEn);
     GetDbgIpeForwardingInfo(A, nextHopExt_f, &dbg_ipe_forwarding_info, &fwd_nextHopExt);
     GetDbgIpeForwardingInfo(A, nextHopIndex_f, &dbg_ipe_forwarding_info, &fwd_nextHopIndex);
     GetDbgIpeForwardingInfo(A, headerHash_f, &dbg_ipe_forwarding_info, &fwd_headerHash);
     GetDbgIpeForwardingInfo(A, valid_f, &dbg_ipe_forwarding_info, &fwd_valid);

     cmd = DRV_IOR(DbgIpeOamInfo_t, DbgIpeOamInfo_tempRxOamType_f);
     DRV_IOCTL(lchip, slice, cmd, &rx_oam_type);

     if (fwd_valid)
     {
         CTC_DKIT_PRINT("Destination Process:\n");
         if (ecmp_valid && ecmp_ecmpEn && ecmp_ecmpGroupId)
         {
             CTC_DKIT_PATH_PRINT("%-15s:%d\n", "do ecmp, group", ecmp_ecmpGroupId);
         }
         if (fwd_apsBridgeEn)
         {
             CTC_DKIT_PATH_PRINT("%-15s:%s\n", "APS bridge", "YES");
         }
         else
         {
             if (fwd_apsSelectValid0)
             {
                 CTC_DKIT_PATH_PRINT("%-15s:%s\n", "APS selector0", "YES");
                 CTC_DKIT_PATH_PRINT("%-15s:%d\n", "APS group", fwd_apsSelectGroupId0);
             }
             if (fwd_apsSelectValid1)
             {
                 CTC_DKIT_PATH_PRINT("%-15s:%s\n", "APS selector2", "YES");
                 CTC_DKIT_PATH_PRINT("%-15s:%d\n", "APS group", fwd_apsSelectGroupId1);
             }
         }
         if (!fwd_discard)
         {
             if (rx_oam_type)
             {
                 CTC_DKIT_PATH_PRINT("%-15s:%s\n", "destination", "OAM engine");
             }
             else
             {
                 is_mcast = IS_BIT_SET(fwd_destMap, 18);
                 fwd_to_cpu = IS_BIT_SET(fwd_destMap, 9);
                 dest_port = fwd_destMap&0xFFFF;
                 if (0x3FFFF == fwd_nextHopIndex)
                 {
                     CTC_DKIT_PATH_PRINT("%-15s:%s\n", "Drop!!!", "next hop is invalid, nextHopPtr=0x3FFFF");
                 }
                 else if (is_mcast)
                 {
                     CTC_DKIT_PATH_PRINT("%-15s:%d\n", "do mcast, group", fwd_destMap&0xFFFF);
                 }
                 else
                 {
                     dest_chip = (fwd_destMap >> 12)&0x1F;
                     is_link_agg = (dest_chip == 0x1F);
                     if (fwd_to_cpu)
                     {
                         CTC_DKIT_PATH_PRINT("%-15s:%s\n", "destination", "CPU");
                     }
                     else
                     {
                         if (is_link_agg)
                         {
                             CTC_DKIT_PATH_PRINT("%-15s:%d\n", "do linkagg, group", fwd_destMap&CTC_DKIT_DRV_LINKAGGID_MASK);
                         }
                         else
                         {
                             gport = CTC_DKIT_DRV_LPORT_TO_CTC_GPORT(dest_chip, dest_port&0x1FF);
                             CTC_DKIT_PATH_PRINT("%-15s:0x%04x\n", "dest gport", gport);
                         }

                         cmd = DRV_IOR(EpeHdrAdjustCtl_t, EpeHdrAdjustCtl_dsNextHopInternalBase_f);
                         DRV_IOCTL(lchip, 0, cmd, &dsNextHopInternalBase);
                         CTC_DKIT_PATH_PRINT("%-15s\n", "nexthop info--->");
                         CTC_DKIT_PATH_PRINT("%-15s:%s\n", "is 8Wnhp", fwd_nextHopExt? "YES" : "NO");
                         if (((fwd_nextHopIndex >> 4)&0x3FFF) != dsNextHopInternalBase)
                         {
                             CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "nexthop index", fwd_nextHopIndex);
                             CTC_DKIT_PATH_PRINT("%-15s:%s\n", "table name", fwd_nextHopExt? "DsNextHop8W" : "DsNextHop4W");
                         }
                         else
                         {
                             CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "nexthop index", (fwd_nextHopIndex&0xF));
                             CTC_DKIT_PATH_PRINT("%-15s:%s\n", "table name", "EpeNextHopInternal");
                         }
                     }
                 }
             }
         }
         else
         {
             _ctc_goldengate_dkit_captured_path_discard_process(p_info, fwd_discard, fwd_discardType, CTC_DKIT_IPE);
             if ((fwd_discard) && (CTC_DKIT_IPE_FATAL_EXCEP_DIS == (fwd_discardType+CTC_DKIT_IPE_USERID_BINDING_DIS)))
             {
                 if (fwd_fatalExceptionValid)
                 {
                     CTC_DKIT_PATH_PRINT("%-15s:%s\n", "Fatal exception",
                                         ctc_goldengate_dkit_get_sub_reason_desc(fwd_fatalException + CTC_DKIT_SUB_IPE_FATAL_UC_IP_HDR_ERROR_OR_MARTION_ADDR));
                 }
             }
         }
         _ctc_goldengate_dkit_captured_path_exception_process(p_info, fwd_exceptionEn, fwd_exceptionIndex, fwd_exceptionSubIndex);
     }

     return CLI_SUCCESS;
 }

STATIC int32
_ctc_goldengate_dkit_captured_path_ipe(ctc_dkit_captured_path_info_t* p_info)
{

    CTC_DKIT_PRINT("\nINGRESS PROCESS  \n-----------------------------------\n");

    /*1. Parser*/
    if (!p_info->discard)
    {
        _ctc_goldengate_dkit_captured_path_ipe_parser(p_info);
    }
    /*2. SCL*/
    if ((!p_info->discard)||(p_info->rx_oam))
    {
        _ctc_goldengate_dkit_captured_path_ipe_scl(p_info);
    }
    /*3. Interface*/
    if ((!p_info->discard)||(p_info->rx_oam))
    {
        _ctc_goldengate_dkit_captured_path_ipe_interface(p_info);
    }
    /*4. Tunnel*/
    if ((!p_info->discard)||(p_info->rx_oam))
    {
        _ctc_goldengate_dkit_captured_path_ipe_tunnel(p_info);
    }
    /*5. ACL*/
    if ((!p_info->discard)||(p_info->rx_oam))
    {
        _ctc_goldengate_dkit_captured_path_ipe_acl(p_info);
    }
    /*6. Forward*/
    if ((!p_info->discard)||(p_info->rx_oam))
    {
        _ctc_goldengate_dkit_captured_path_ipe_forward(p_info);
    }
    /*7. OAM*/
    if ((!p_info->discard)||(p_info->rx_oam))
    {
        _ctc_goldengate_dkit_captured_path_ipe_oam(p_info);
    }
    /*8. Destination*/
    if ((!p_info->discard)||(p_info->rx_oam))
    {
        _ctc_goldengate_dkit_captured_path_ipe_destination(p_info);
    }

    if ((p_info->discard) && (!p_info->exception)&&(!p_info->rx_oam))/*check exception when discard but no exception*/
    {
        _ctc_goldengate_dkit_captured_path_ipe_exception_check(p_info);
    }

    if (p_info->discard && (0 == p_info->detail))
    {
        CTC_DKIT_PRINT("Packet Drop at IPE:\n");
        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "reason", ctc_goldengate_dkit_get_reason_desc(p_info->discard_type + CTC_DKIT_IPE_USERID_BINDING_DIS));
    }

    return CLI_SUCCESS;
}

STATIC int32
_ctc_goldengate_dkit_captured_path_bsr_mcast(ctc_dkit_captured_path_info_t* p_info)
{
    uint32 cmd = 0;
    uint32 ret = CLI_SUCCESS;
    uint32 local_chip = 0;
    uint32 dest_map = 0;
    uint32 groupIdShiftBit = 0;
    uint32 remaining_rcd = 0xFFF;
    uint32 end_replication = 0;
    uint32 dsMetEntryBase = 0;
    uint32 metEntryPtr =0;
    uint32 dsNextHopInternalBase = 0;
    uint8 lchip = p_info->lchip;
    /*DbgFwdMetFifoInfo*/
    uint32 met_doMet = 0;
    uint32 met_valid = 0;
    uint32 met_enqueueDiscard = 0;
    /*DsMetEntry*/
    uint32 currentMetEntryType = 0;
    uint32 isMet = 0;
    uint32 nextMetEntryPtr = 0;
    uint32 endLocalRep = 0;
    uint32  isLinkAggregation = 0;
    uint32  remoteChip = 0;
    uint32  replicationCtl = 0;
    uint32  ucastId = 0;
    uint32   nexthopExt = 0;
    uint32  nexthopPtr = 0;
    uint32 mcastMode = 0;
    uint32 apsBridgeEn = 0;
    uint8 port_bitmap[8] = {0};
    uint8 port_bitmap_high[9] = {0};
    uint8 port_bitmap_low[8] = {0};


    DsMetEntry6W_m ds_met_entry6_w;
    DsMetEntry_m ds_met_entry;
    DbgFwdMetFifoInfo_m dbg_fwd_met_fifo_info;
    DbgFwdBufStoreInfo_m dbg_fwd_buf_store_info;

    sal_memset(&ds_met_entry6_w, 0, sizeof(ds_met_entry6_w));
    sal_memset(&ds_met_entry, 0, sizeof(ds_met_entry));


    /*DbgFwdMetFifoInfo*/
    sal_memset(&dbg_fwd_met_fifo_info, 0, sizeof(dbg_fwd_met_fifo_info));
    cmd = DRV_IOR(DbgFwdMetFifoInfo_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &dbg_fwd_met_fifo_info);
    GetDbgFwdMetFifoInfo(A, valid_f, &dbg_fwd_met_fifo_info, &met_valid);
    GetDbgFwdMetFifoInfo(A, doMet_f, &dbg_fwd_met_fifo_info, &met_doMet);
    GetDbgFwdMetFifoInfo(A, enqueueDiscard_f, &dbg_fwd_met_fifo_info, &met_enqueueDiscard);
    /*DbgFwdBufStoreInfo*/
    sal_memset(&dbg_fwd_buf_store_info, 0, sizeof(dbg_fwd_buf_store_info));
    cmd = DRV_IOR(DbgFwdBufStoreInfo_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &dbg_fwd_buf_store_info);
    GetDbgFwdBufStoreInfo(A, destMap_f, &dbg_fwd_buf_store_info, &dest_map);

    cmd = DRV_IOR(EpeHdrAdjustCtl_t, EpeHdrAdjustCtl_dsNextHopInternalBase_f);
    DRV_IOCTL(lchip, 0, cmd, &dsNextHopInternalBase);

    if (met_valid && met_doMet)
    {
        CTC_DKIT_PRINT("Multicast Process:\n");
        CTC_DKIT_PATH_PRINT("%-15s:%d\n", "mcast group", dest_map&0xFFFF);

        cmd = DRV_IOR(MetFifoCtl_t, MetFifoCtl_chipId_f);
        DRV_IOCTL(lchip, 0, cmd, &local_chip);
        cmd = DRV_IOR(MetFifoCtl_t, MetFifoCtl_dsMetEntryBase_f);
        DRV_IOCTL(lchip, 0, cmd, &dsMetEntryBase);
        cmd = DRV_IOR(MetFifoCtl_t, MetFifoCtl_groupIdShiftBit_f);
        DRV_IOCTL(lchip, 0, cmd, &groupIdShiftBit);
        nextMetEntryPtr = (dest_map&0xFFFF) << groupIdShiftBit;
        for (;;)
        {
            metEntryPtr =  nextMetEntryPtr + dsMetEntryBase;
            CTC_DKIT_PATH_PRINT("%-15s\n", "DsMetEntry--->");
            CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "index", metEntryPtr);
            cmd = DRV_IOR(DsMetEntry6W_t, DRV_ENTRY_FLAG);
            ret = DRV_IOCTL(lchip, (metEntryPtr&0x1FFFE), cmd, &ds_met_entry6_w);
            if (ret != DRV_E_NONE)
            {
                return CLI_ERROR;
            }
            GetDsMetEntry6W(A, currentMetEntryType_f, &ds_met_entry6_w, &currentMetEntryType);
            CTC_DKIT_PATH_PRINT("%-15s:%s\n", "table name", currentMetEntryType? "DsMetEntry6W" : "DsMetEntry3W");
            if (currentMetEntryType)
            {
                sal_memcpy((uint8*)&ds_met_entry, (uint8*)&ds_met_entry6_w, DS_MET_ENTRY6_W_BYTES);
            }
            else
            {
                if (IS_BIT_SET(metEntryPtr, 0))
                {
                    sal_memcpy((uint8*)&ds_met_entry, ((uint8 *)&ds_met_entry6_w) + DS_MET_ENTRY3_W_BYTES, DS_MET_ENTRY3_W_BYTES);
                }
                else
                {
                    sal_memcpy((uint8*)&ds_met_entry, ((uint8 *)&ds_met_entry6_w), DS_MET_ENTRY6_W_BYTES);
                }
                sal_memset(((uint8*)&ds_met_entry) + DS_MET_ENTRY3_W_BYTES, 0, DS_MET_ENTRY3_W_BYTES);
            }
            GetDsMetEntry(A, isMet_f, &ds_met_entry, &isMet);
            GetDsMetEntry(A, nextMetEntryPtr_f, &ds_met_entry, &nextMetEntryPtr);
            GetDsMetEntry(A, endLocalRep_f, &ds_met_entry, &endLocalRep);
            GetDsMetEntry(A, isLinkAggregation_f, &ds_met_entry, &isLinkAggregation);
            GetDsMetEntry(A, remoteChip_f, &ds_met_entry, &remoteChip);
            GetDsMetEntry(A, u1_g2_replicationCtl_f, &ds_met_entry, &replicationCtl);
            GetDsMetEntry(A, u1_g2_ucastId_f, &ds_met_entry, &ucastId);
            GetDsMetEntry(A, nextHopExt_f, &ds_met_entry, &nexthopExt);
            GetDsMetEntry(A, nextHopPtr_f, &ds_met_entry, &nexthopPtr);
            GetDsMetEntry(A, mcastMode_f, &ds_met_entry, &mcastMode);
            GetDsMetEntry(A, u1_g2_apsBridgeEn_f, &ds_met_entry, &apsBridgeEn);

            if(remoteChip)
            {
                CTC_DKIT_PATH_PRINT("%-15s:%s\n", "to remote chip", "YES");
            }
            else
            {
                if (!mcastMode && apsBridgeEn)
                {
                    CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "APS bridge", "YES");
                    CTC_DKIT_PATH_PRINT("%-15s:%d\n", "APS group", ucastId);
                }

                if (!mcastMode)
                {
                    CTC_DKIT_PATH_PRINT("%-15s:%s\n", "met mode", "link list");
                    if (isLinkAggregation)
                    {
                        CTC_DKIT_PATH_PRINT("%-15s:%d\n", "do linkagg, group", ucastId&CTC_DKIT_DRV_LINKAGGID_MASK);
                    }
                    else
                    {
                        CTC_DKIT_PATH_PRINT("%-15s:0x%04x\n", "dest gport",
                                                                      CTC_DKIT_DRV_LPORT_TO_CTC_GPORT(local_chip, ucastId&0x1FF));
                    }
                    CTC_DKIT_PATH_PRINT("%-15s:%d\n", "replication cnt", replicationCtl&0x1F);

                    CTC_DKIT_PATH_PRINT("%-15s:%s\n", "is 8Wnhp", nexthopExt? "YES" : "NO");
                    if ((((replicationCtl >> 5) >> 4)&0x3FFF) != dsNextHopInternalBase)
                    {
                        CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "nexthop index", (replicationCtl >> 5));
                        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "table name", nexthopExt? "DsNextHop8W" : "DsNextHop4W");
                    }
                    else
                    {
                        CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "nexthop index", (((replicationCtl >> 5)&0xF) >> 1));
                        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "table name", "EpeNextHopInternal");
                    }
                }
                else
                {
                    GetDsMetEntry(A, portBitmapHigh_f, &ds_met_entry, &port_bitmap_high);
                    GetDsMetEntry(A, u1_g1_portBitmap_f, &ds_met_entry, &port_bitmap_low);
                    CTC_DKIT_PATH_PRINT("%-15s:%s\n", "met mode", "port bitmap");
                    if (isLinkAggregation)
                    {
                        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "do linkagg", "YES");
                    }
                    else
                    {
                        sal_memcpy(port_bitmap, port_bitmap_low, 8);
                        port_bitmap[7] |= (port_bitmap_high[0] << 3);
                        CTC_DKIT_PATH_PRINT("%-15s:", "dest port63~0");
                        CTC_DKIT_PRINT("0x%02x", port_bitmap[7]);
                        CTC_DKIT_PRINT("%02x", port_bitmap[6]);
                        CTC_DKIT_PRINT("%02x", port_bitmap[5]);
                        CTC_DKIT_PRINT("%02x", port_bitmap[4]);
                        CTC_DKIT_PRINT("%02x", port_bitmap[3]);
                        CTC_DKIT_PRINT("%02x", port_bitmap[2]);
                        CTC_DKIT_PRINT("%02x", port_bitmap[1]);
                        CTC_DKIT_PRINT("%02x\n", port_bitmap[0]);
                        sal_memset(port_bitmap, 0 , 8);
                        port_bitmap[0] = (port_bitmap_high[1] << 3) + (port_bitmap_high[0] >> 5);
                        port_bitmap[1] = (port_bitmap_high[2] << 3) + (port_bitmap_high[1] >> 5);
                        port_bitmap[2] = (port_bitmap_high[3] << 3) + (port_bitmap_high[2] >> 5);
                        port_bitmap[3] = (port_bitmap_high[4] << 3) + (port_bitmap_high[3] >> 5);
                        port_bitmap[4] = (port_bitmap_high[5] << 3) + (port_bitmap_high[4] >> 5);
                        port_bitmap[5] = (port_bitmap_high[6] << 3) + (port_bitmap_high[5] >> 5);
                        port_bitmap[6] = (port_bitmap_high[7] << 3) + (port_bitmap_high[6] >> 5);
                        port_bitmap[7] = (port_bitmap_high[8] << 3) + (port_bitmap_high[7] >> 5);
                        CTC_DKIT_PATH_PRINT("%-15s:", "dest port125~64");
                        CTC_DKIT_PRINT("0x%02x", port_bitmap[7]);
                        CTC_DKIT_PRINT("%02x", port_bitmap[6]);
                        CTC_DKIT_PRINT("%02x", port_bitmap[5]);
                        CTC_DKIT_PRINT("%02x", port_bitmap[4]);
                        CTC_DKIT_PRINT("%02x", port_bitmap[3]);
                        CTC_DKIT_PRINT("%02x", port_bitmap[2]);
                        CTC_DKIT_PRINT("%02x", port_bitmap[1]);
                        CTC_DKIT_PRINT("%02x\n", port_bitmap[0]);

                        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "is 8wnhp", nexthopExt? "YES" : "NO");
                        if (((nexthopPtr >> 4)&0x3FFF) != dsNextHopInternalBase)
                        {
                            CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "nexthop index", nexthopPtr);
                            CTC_DKIT_PATH_PRINT("%-15s:%s\n", "table name", nexthopExt? "DsNextHop8W" : "DsNextHop4W");
                        }
                        else
                        {
                            CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "nexthop index", ((nexthopPtr &0xF) >> 1));
                            CTC_DKIT_PATH_PRINT("%-15s:%s\n", "table name", "EpeNextHopInternal");
                        }
                    }
                }
            }
            end_replication = (0xFFFF == nextMetEntryPtr) || (1 == remaining_rcd) || (!isMet);
            remaining_rcd--;

            if (end_replication)
            {
                break;
            }
        }
    }
    if (met_enqueueDiscard)
    {
        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "Drop!!!", "Drop at METFIFO");
    }

    return CLI_SUCCESS;
}

STATIC int32
_ctc_goldengate_dkit_captured_path_bsr_linkagg(ctc_dkit_captured_path_info_t* p_info)
{
    return CLI_SUCCESS;
}

STATIC int32
_ctc_goldengate_dkit_captured_path_bsr_queue(ctc_dkit_captured_path_info_t* p_info)
{
    uint32 cmd = 0;
    uint32 queue_number = 0;
    uint32 queueNumBase = 0;
    uint8 lchip = p_info->lchip;
    /*DbgFwdQueMgrInfo*/
    uint32 qmgr_noLinkaggSwitch = 0;
    uint32 qmgr_translateLinkAggregation = 0;
    uint32 qmgr_rrEn = 0;
    uint32 qmgr_noLinkAggregationMemberDiscard = 0;
    uint32 qmgr_linkAggPortRemapEn = 0;
    uint32 qmgr_sgmacSelectEn = 0;
    uint32 qmgr_queueNumGenCtl = 0;
    uint32 qmgr_shiftQueueNum = 0;
    uint32 qmgr_mcastLinkAggregationDiscard = 0;
    uint32 qmgr_stackingDiscard = 0;
    uint32 qmgr_replicationCtl = 0;
    uint32 qmgr_valid = 0;
    DbgFwdQueMgrInfo_m dbg_fwd_que_mgr_info;

    /*DbgQMgrEnqInfo*/
    uint32 enq_queId = 0;
    uint32 enq_basedOnCellCnt = 0;
    uint32 enq_repCntBase = 0;
    uint32 enq_replicationCnt = 0;
    uint32 enq_c2cPacket = 0;
    uint32 enq_criticalPacket = 0;
    uint32 enq_wredDropMode = 0;
    uint32 enq_needEcnProc = 0;
    uint32 enq_dropPri = 0;
    uint32 enq_netPktAdjIndex = 0;
    uint32 enq_statsUpdEn = 0;
    uint32 enq_resvCh0 = 0;
    uint32 enq_resvCh1 = 0;
    uint32 enq_forceNoDrop = 0;
    uint32 enq_mappedTc = 0;
    uint32 enq_mappedSc = 0;
    uint32 enq_queMinProfId = 0;
    uint32 enq_queDropProfExt = 0;
    uint32 enq_chanIdIsNet = 0;
    uint32 enq_cond1 = 0;
    uint32 enq_cond2 = 0;
    uint32 enq_cond3 = 0;
    uint32 enq_cond4 = 0;
    uint32 enq_cond5 = 0;
    uint32 enq_cond6 = 0;
    uint32 enq_cond7 = 0;
    uint32 enq_cond8 = 0;
    uint32 enq_cond9 = 0;
    uint32 enq_cond10 = 0;
    uint32 enq_currentCongest = 0;
    uint32 enq_isLowTc = 0;
    uint32 enq_isHighCongest = 0;
    uint32 enq_cond11 = 0;
    uint32 enq_discardingDrop = 0;
    uint32 enq_rsvDrop = 0;
    uint32 enq_enqueueDrop = 0;
    uint32 enq_linkListDrop = 0;
    uint32 enq_rcdUpdateEn = 0;
    uint32 enq_pktReleaseEn = 0;
    uint32 enq_resrcCntUpd = 0;
    uint32 enq_bufCell = 0;
    uint32 enq_skipEnqueue = 0;
    uint32 enq_adjustPktLen = 0;
    uint32 enq_valid = 0;
    DbgQMgrEnqInfo_m  dbg_q_mgr_enq_info;


    sal_memset(&dbg_fwd_que_mgr_info, 0, sizeof(dbg_fwd_que_mgr_info));
    cmd = DRV_IOR(DbgFwdQueMgrInfo_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &dbg_fwd_que_mgr_info);
    GetDbgFwdQueMgrInfo(A, noLinkaggSwitch_f, &dbg_fwd_que_mgr_info, &qmgr_noLinkaggSwitch);
    GetDbgFwdQueMgrInfo(A, translateLinkAggregation_f, &dbg_fwd_que_mgr_info, &qmgr_translateLinkAggregation);
    GetDbgFwdQueMgrInfo(A, rrEn_f, &dbg_fwd_que_mgr_info, &qmgr_rrEn);
    GetDbgFwdQueMgrInfo(A, noLinkAggregationMemberDiscard_f, &dbg_fwd_que_mgr_info, &qmgr_noLinkAggregationMemberDiscard);
    GetDbgFwdQueMgrInfo(A, linkAggPortRemapEn_f, &dbg_fwd_que_mgr_info, &qmgr_linkAggPortRemapEn);
    GetDbgFwdQueMgrInfo(A, sgmacSelectEn_f, &dbg_fwd_que_mgr_info, &qmgr_sgmacSelectEn);
    GetDbgFwdQueMgrInfo(A, queueNumGenCtl_f, &dbg_fwd_que_mgr_info, &qmgr_queueNumGenCtl);
    GetDbgFwdQueMgrInfo(A, shiftQueueNum_f, &dbg_fwd_que_mgr_info, &qmgr_shiftQueueNum);
    GetDbgFwdQueMgrInfo(A, mcastLinkAggregationDiscard_f, &dbg_fwd_que_mgr_info, &qmgr_mcastLinkAggregationDiscard);
    GetDbgFwdQueMgrInfo(A, stackingDiscard_f, &dbg_fwd_que_mgr_info, &qmgr_stackingDiscard);
    GetDbgFwdQueMgrInfo(A, replicationCtl_f, &dbg_fwd_que_mgr_info, &qmgr_replicationCtl);
    GetDbgFwdQueMgrInfo(A, valid_f, &dbg_fwd_que_mgr_info, &qmgr_valid);

    sal_memset(&dbg_q_mgr_enq_info, 0, sizeof(dbg_q_mgr_enq_info));
    cmd = DRV_IOR(DbgQMgrEnqInfo_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &dbg_q_mgr_enq_info);
    GetDbgQMgrEnqInfo(A, queId_f, &dbg_q_mgr_enq_info, &enq_queId);
    GetDbgQMgrEnqInfo(A, basedOnCellCnt_f, &dbg_q_mgr_enq_info, &enq_basedOnCellCnt);
    GetDbgQMgrEnqInfo(A, repCntBase_f, &dbg_q_mgr_enq_info, &enq_repCntBase);
    GetDbgQMgrEnqInfo(A, replicationCnt_f, &dbg_q_mgr_enq_info, &enq_replicationCnt);
    GetDbgQMgrEnqInfo(A, c2cPacket_f, &dbg_q_mgr_enq_info, &enq_c2cPacket);
    GetDbgQMgrEnqInfo(A, criticalPacket_f, &dbg_q_mgr_enq_info, &enq_criticalPacket);
    GetDbgQMgrEnqInfo(A, wredDropMode_f, &dbg_q_mgr_enq_info, &enq_wredDropMode);
    GetDbgQMgrEnqInfo(A, needEcnProc_f, &dbg_q_mgr_enq_info, &enq_needEcnProc);
    GetDbgQMgrEnqInfo(A, dropPri_f, &dbg_q_mgr_enq_info, &enq_dropPri);
    GetDbgQMgrEnqInfo(A, netPktAdjIndex_f, &dbg_q_mgr_enq_info, &enq_netPktAdjIndex);
    GetDbgQMgrEnqInfo(A, statsUpdEn_f, &dbg_q_mgr_enq_info, &enq_statsUpdEn);
    GetDbgQMgrEnqInfo(A, resvCh0_f, &dbg_q_mgr_enq_info, &enq_resvCh0);
    GetDbgQMgrEnqInfo(A, resvCh1_f, &dbg_q_mgr_enq_info, &enq_resvCh1);
    GetDbgQMgrEnqInfo(A, forceNoDrop_f, &dbg_q_mgr_enq_info, &enq_forceNoDrop);
    GetDbgQMgrEnqInfo(A, mappedTc_f, &dbg_q_mgr_enq_info, &enq_mappedTc);
    GetDbgQMgrEnqInfo(A, mappedSc_f, &dbg_q_mgr_enq_info, &enq_mappedSc);
    GetDbgQMgrEnqInfo(A, queMinProfId_f, &dbg_q_mgr_enq_info, &enq_queMinProfId);
    GetDbgQMgrEnqInfo(A, queDropProfExt_f, &dbg_q_mgr_enq_info, &enq_queDropProfExt);
    GetDbgQMgrEnqInfo(A, chanIdIsNet_f, &dbg_q_mgr_enq_info, &enq_chanIdIsNet);
    GetDbgQMgrEnqInfo(A, cond1_f, &dbg_q_mgr_enq_info, &enq_cond1);
    GetDbgQMgrEnqInfo(A, cond2_f, &dbg_q_mgr_enq_info, &enq_cond2);
    GetDbgQMgrEnqInfo(A, cond3_f, &dbg_q_mgr_enq_info, &enq_cond3);
    GetDbgQMgrEnqInfo(A, cond4_f, &dbg_q_mgr_enq_info, &enq_cond4);
    GetDbgQMgrEnqInfo(A, cond5_f, &dbg_q_mgr_enq_info, &enq_cond5);
    GetDbgQMgrEnqInfo(A, cond6_f, &dbg_q_mgr_enq_info, &enq_cond6);
    GetDbgQMgrEnqInfo(A, cond7_f, &dbg_q_mgr_enq_info, &enq_cond7);
    GetDbgQMgrEnqInfo(A, cond8_f, &dbg_q_mgr_enq_info, &enq_cond8);
    GetDbgQMgrEnqInfo(A, cond9_f, &dbg_q_mgr_enq_info, &enq_cond9);
    GetDbgQMgrEnqInfo(A, cond10_f, &dbg_q_mgr_enq_info, &enq_cond10);
    GetDbgQMgrEnqInfo(A, currentCongest_f, &dbg_q_mgr_enq_info, &enq_currentCongest);
    GetDbgQMgrEnqInfo(A, isLowTc_f, &dbg_q_mgr_enq_info, &enq_isLowTc);
    GetDbgQMgrEnqInfo(A, isHighCongest_f, &dbg_q_mgr_enq_info, &enq_isHighCongest);
    GetDbgQMgrEnqInfo(A, cond11_f, &dbg_q_mgr_enq_info, &enq_cond11);
    GetDbgQMgrEnqInfo(A, discardingDrop_f, &dbg_q_mgr_enq_info, &enq_discardingDrop);
    GetDbgQMgrEnqInfo(A, rsvDrop_f, &dbg_q_mgr_enq_info, &enq_rsvDrop);
    GetDbgQMgrEnqInfo(A, enqueueDrop_f, &dbg_q_mgr_enq_info, &enq_enqueueDrop);
    GetDbgQMgrEnqInfo(A, linkListDrop_f, &dbg_q_mgr_enq_info, &enq_linkListDrop);
    GetDbgQMgrEnqInfo(A, rcdUpdateEn_f, &dbg_q_mgr_enq_info, &enq_rcdUpdateEn);
    GetDbgQMgrEnqInfo(A, pktReleaseEn_f, &dbg_q_mgr_enq_info, &enq_pktReleaseEn);
    GetDbgQMgrEnqInfo(A, resrcCntUpd_f, &dbg_q_mgr_enq_info, &enq_resrcCntUpd);
    GetDbgQMgrEnqInfo(A, bufCell_f, &dbg_q_mgr_enq_info, &enq_bufCell);
    GetDbgQMgrEnqInfo(A, skipEnqueue_f, &dbg_q_mgr_enq_info, &enq_skipEnqueue);
    GetDbgQMgrEnqInfo(A, adjustPktLen_f, &dbg_q_mgr_enq_info, &enq_adjustPktLen);
    GetDbgQMgrEnqInfo(A, valid_f, &dbg_q_mgr_enq_info, &enq_valid);


    if (qmgr_valid)
    {
        CTC_DKIT_PRINT("Queue Process:\n");

        if (qmgr_noLinkAggregationMemberDiscard)
        {
            CTC_DKIT_PATH_PRINT("%-15s:%s\n", "Drop!!!", "port linkagg no member");
        }
        else if (qmgr_mcastLinkAggregationDiscard)
        {
            CTC_DKIT_PATH_PRINT("%-15s:%s\n", "Drop!!!", "mcast linkagg no member");
        }
        else if (qmgr_stackingDiscard)
        {
            CTC_DKIT_PATH_PRINT("%-15s:%s\n", "Drop!!!", "stacking discard");
        }
        else
        {
            cmd = DRV_IOR(DsQueueNumGenCtl_t, DsQueueNumGenCtl_queueNumBase_f);
            DRV_IOCTL(lchip, qmgr_queueNumGenCtl, cmd, &queueNumBase);
            queue_number = qmgr_shiftQueueNum + queueNumBase;

            if (enq_valid)
            {
                queue_number = enq_queId;
            }
            CTC_DKIT_PATH_PRINT("%-15s:%d\n", "queue id", queue_number);
        }

        if (enq_valid)
        {
            if (enq_enqueueDrop || enq_rsvDrop || enq_linkListDrop || enq_discardingDrop)
            {
                CTC_DKIT_PATH_PRINT("%-15s:%s\n", "Drop!!!", "enqueue discard");
            }
        }

    }

    return CLI_SUCCESS;
}

STATIC int32
_ctc_goldengate_dkit_captured_path_bsr(ctc_dkit_captured_path_info_t* p_info)
{
    CTC_DKIT_PRINT("\nRELAY PROCESS    \n-----------------------------------\n");

    /*1. Mcast*/
    if ((!p_info->discard) && (p_info->detail))
    {
        _ctc_goldengate_dkit_captured_path_bsr_mcast(p_info);
    }
    /*2. Linkagg*/
    if ((!p_info->discard) && (p_info->detail))
    {
        _ctc_goldengate_dkit_captured_path_bsr_linkagg(p_info);
    }
    /*3. Queue*/
    if ((!p_info->discard) && (p_info->detail))
    {
        _ctc_goldengate_dkit_captured_path_bsr_queue(p_info);
    }

    return CLI_SUCCESS;
}

STATIC int32
_ctc_goldengate_dkit_captured_path_epe_parser(ctc_dkit_captured_path_info_t* p_info)
{
    return CLI_SUCCESS;
}

STATIC int32
_ctc_goldengate_dkit_captured_path_epe_scl(ctc_dkit_captured_path_info_t* p_info)
{
     uint8 slice = p_info->slice_epe;
     uint8 lchip = p_info->lchip;
     uint32 cmd = 0;
     char* key_name[11] =
     {
         "None", "DsEgressXcOamDoubleVlanPortHashKey", "DsEgressXcOamSvlanPortHashKey", "DsEgressXcOamCvlanPortHashKey",
          "DsEgressXcOamSvlanCosPortHashKey", "DsEgressXcOamCvlanCosPortHashKey", "DsEgressXcOamPortVlanCrossHashKey",
          "DsEgressXcOamPortCrossHashKey", "DsEgressXcOamPortHashKey", "DsEgressXcOamSvlanPortMacHashKey",
          "DsEgressXcOamTunnelPbbHashKey"
     };

     /*DbgEgrXcOamHash0EngineFromEpeNhpInfo*/
     uint32 hash0_resultValid = 0;
     uint32 hash0_defaultEntryValid = 0;
     uint32 hash0_egressXcOamHashConflict = 0;
     uint32 hash0_keyIndex = 0;
     uint32 hash0_valid = 0;
     DbgEgrXcOamHash0EngineFromEpeNhpInfo_m dbg_egr_xc_oam_hash0_engine_from_epe_nhp_info;
     /*DbgEgrXcOamHash1EngineFromEpeNhpInfo*/
     uint32 hash1_resultValid = 0;
     uint32 hash1_defaultEntryValid = 0;
     uint32 hash1_egressXcOamHashConflict = 0;
     uint32 hash1_keyIndex = 0;
     uint32 hash1_valid = 0;
     DbgEgrXcOamHash1EngineFromEpeNhpInfo_m dbg_egr_xc_oam_hash1_engine_from_epe_nhp_info;
     /*DbgEpeNextHopInfo*/
     uint32 scl_discard = 0;
     uint32 scl_discardType = 0;
     uint32 scl_lookup1Valid = 0;
     uint32 scl_vlanHash1Type = 0;
     uint32 scl_lookup2Valid = 0;
     uint32 scl_vlanHash2Type = 0;
     uint32 scl_vlanXlateResult1Valid = 0;
     uint32 scl_vlanXlateResult2Valid = 0;
     uint32 scl_hashPort = 0;
     uint32 scl_vlanXlateVlanRangeValid = 0;
     uint32 scl_vlanXlateVlanRangeMax = 0;
     uint32 scl_aclVlanRangeValid = 0;
     uint32 scl_aclVlanRangeMax = 0;
     uint32 scl_valid = 0;
     DbgEpeNextHopInfo_m dbg_epe_next_hop_info;

     /*DbgEgrXcOamHash0EngineFromEpeNhpInfo*/
     sal_memset(&dbg_egr_xc_oam_hash0_engine_from_epe_nhp_info, 0, sizeof(dbg_egr_xc_oam_hash0_engine_from_epe_nhp_info));
     cmd = DRV_IOR(DbgEgrXcOamHash0EngineFromEpeNhpInfo_t, DRV_ENTRY_FLAG);
     DRV_IOCTL(lchip, slice, cmd, &dbg_egr_xc_oam_hash0_engine_from_epe_nhp_info);
     GetDbgEgrXcOamHash0EngineFromEpeNhpInfo(A, resultValid_f, &dbg_egr_xc_oam_hash0_engine_from_epe_nhp_info, &hash0_resultValid);
     GetDbgEgrXcOamHash0EngineFromEpeNhpInfo(A, defaultEntryValid_f, &dbg_egr_xc_oam_hash0_engine_from_epe_nhp_info, &hash0_defaultEntryValid);
     GetDbgEgrXcOamHash0EngineFromEpeNhpInfo(A, egressXcOamHashConflict_f, &dbg_egr_xc_oam_hash0_engine_from_epe_nhp_info, &hash0_egressXcOamHashConflict);
     GetDbgEgrXcOamHash0EngineFromEpeNhpInfo(A, keyIndex_f, &dbg_egr_xc_oam_hash0_engine_from_epe_nhp_info, &hash0_keyIndex);
     GetDbgEgrXcOamHash0EngineFromEpeNhpInfo(A, valid_f, &dbg_egr_xc_oam_hash0_engine_from_epe_nhp_info, &hash0_valid);

     /*DbgEgrXcOamHash0EngineFromEpeNhpInfo*/
     sal_memset(&dbg_egr_xc_oam_hash0_engine_from_epe_nhp_info, 0, sizeof(dbg_egr_xc_oam_hash0_engine_from_epe_nhp_info));
     cmd = DRV_IOR(DbgEgrXcOamHash0EngineFromEpeNhpInfo_t, DRV_ENTRY_FLAG);
     DRV_IOCTL(lchip, slice, cmd, &dbg_egr_xc_oam_hash0_engine_from_epe_nhp_info);
     GetDbgEgrXcOamHash0EngineFromEpeNhpInfo(A, resultValid_f, &dbg_egr_xc_oam_hash0_engine_from_epe_nhp_info, &hash0_resultValid);
     GetDbgEgrXcOamHash0EngineFromEpeNhpInfo(A, defaultEntryValid_f, &dbg_egr_xc_oam_hash0_engine_from_epe_nhp_info, &hash0_defaultEntryValid);
     GetDbgEgrXcOamHash0EngineFromEpeNhpInfo(A, egressXcOamHashConflict_f, &dbg_egr_xc_oam_hash0_engine_from_epe_nhp_info, &hash0_egressXcOamHashConflict);
     GetDbgEgrXcOamHash0EngineFromEpeNhpInfo(A, keyIndex_f, &dbg_egr_xc_oam_hash0_engine_from_epe_nhp_info, &hash0_keyIndex);
     GetDbgEgrXcOamHash0EngineFromEpeNhpInfo(A, valid_f, &dbg_egr_xc_oam_hash0_engine_from_epe_nhp_info, &hash0_valid);
     /*DbgEgrXcOamHash1EngineFromEpeNhpInfo*/
     sal_memset(&dbg_egr_xc_oam_hash1_engine_from_epe_nhp_info, 0, sizeof(dbg_egr_xc_oam_hash1_engine_from_epe_nhp_info));
     cmd = DRV_IOR(DbgEgrXcOamHash1EngineFromEpeNhpInfo_t, DRV_ENTRY_FLAG);
     DRV_IOCTL(lchip, slice, cmd, &dbg_egr_xc_oam_hash1_engine_from_epe_nhp_info);
     GetDbgEgrXcOamHash1EngineFromEpeNhpInfo(A, resultValid_f, &dbg_egr_xc_oam_hash1_engine_from_epe_nhp_info, &hash1_resultValid);
     GetDbgEgrXcOamHash1EngineFromEpeNhpInfo(A, defaultEntryValid_f, &dbg_egr_xc_oam_hash1_engine_from_epe_nhp_info, &hash1_defaultEntryValid);
     GetDbgEgrXcOamHash1EngineFromEpeNhpInfo(A, egressXcOamHashConflict_f, &dbg_egr_xc_oam_hash1_engine_from_epe_nhp_info, &hash1_egressXcOamHashConflict);
     GetDbgEgrXcOamHash1EngineFromEpeNhpInfo(A, keyIndex_f, &dbg_egr_xc_oam_hash1_engine_from_epe_nhp_info, &hash1_keyIndex);
     GetDbgEgrXcOamHash1EngineFromEpeNhpInfo(A, valid_f, &dbg_egr_xc_oam_hash1_engine_from_epe_nhp_info, &hash1_valid);
     /*DbgEpeNextHopInfo*/
     sal_memset(&dbg_epe_next_hop_info, 0, sizeof(dbg_epe_next_hop_info));
     cmd = DRV_IOR(DbgEpeNextHopInfo_t, DRV_ENTRY_FLAG);
     DRV_IOCTL(lchip, slice, cmd, &dbg_epe_next_hop_info);
     GetDbgEpeNextHopInfo(A, discard_f, &dbg_epe_next_hop_info, &scl_discard);
     GetDbgEpeNextHopInfo(A, discardType_f, &dbg_epe_next_hop_info, &scl_discardType);
     GetDbgEpeNextHopInfo(A, lookup1Valid_f, &dbg_epe_next_hop_info, &scl_lookup1Valid);
     GetDbgEpeNextHopInfo(A, vlanHash1Type_f, &dbg_epe_next_hop_info, &scl_vlanHash1Type);
     GetDbgEpeNextHopInfo(A, lookup2Valid_f, &dbg_epe_next_hop_info, &scl_lookup2Valid);
     GetDbgEpeNextHopInfo(A, vlanHash2Type_f, &dbg_epe_next_hop_info, &scl_vlanHash2Type);
     GetDbgEpeNextHopInfo(A, vlanXlateResult1Valid_f, &dbg_epe_next_hop_info, &scl_vlanXlateResult1Valid);
     GetDbgEpeNextHopInfo(A, vlanXlateResult2Valid_f, &dbg_epe_next_hop_info, &scl_vlanXlateResult2Valid);
     GetDbgEpeNextHopInfo(A, hashPort_f, &dbg_epe_next_hop_info, &scl_hashPort);
     GetDbgEpeNextHopInfo(A, vlanXlateVlanRangeValid_f, &dbg_epe_next_hop_info, &scl_vlanXlateVlanRangeValid);
     GetDbgEpeNextHopInfo(A, vlanXlateVlanRangeMax_f, &dbg_epe_next_hop_info, &scl_vlanXlateVlanRangeMax);
     GetDbgEpeNextHopInfo(A, aclVlanRangeValid_f, &dbg_epe_next_hop_info, &scl_aclVlanRangeValid);
     GetDbgEpeNextHopInfo(A, aclVlanRangeMax_f, &dbg_epe_next_hop_info, &scl_aclVlanRangeMax);
     GetDbgEpeNextHopInfo(A, valid_f, &dbg_epe_next_hop_info, &scl_valid);

    if (0 == p_info->detail)
    {
        goto Discard;
    }

     if (scl_valid)
     {
         if (hash0_valid || hash1_valid)
         {
             CTC_DKIT_PRINT("SCL Process:\n");
             if (scl_lookup1Valid)
             {
                 CTC_DKIT_PATH_PRINT("%-15s\n", "HASH0 lookup--->");
                 if (hash0_defaultEntryValid)
                 {
                     CTC_DKIT_PATH_PRINT("%-15s:%s\n", "result", CTC_DKIT_CAPTURED_DEFAULT);
                     CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "key index", hash0_keyIndex);
                     CTC_DKIT_PATH_PRINT("%-15s:%s\n", "key name", key_name[scl_vlanHash1Type]);
                 }
                 else if (hash1_egressXcOamHashConflict)
                 {
                     CTC_DKIT_PATH_PRINT("%-15s:%s\n", "result", CTC_DKIT_CAPTURED_CONFLICT);
                     CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "key index", hash0_keyIndex);
                     CTC_DKIT_PATH_PRINT("%-15s:%s\n", "key name", "");
                 }
                 else if (hash0_resultValid)
                 {
                     CTC_DKIT_PATH_PRINT("%-15s:%s\n", "result", CTC_DKIT_CAPTURED_HASH_HIT);
                     CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "key index", hash0_keyIndex);
                     CTC_DKIT_PATH_PRINT("%-15s:%s\n", "key name", key_name[scl_vlanHash1Type]);
                 }
                 else
                 {
                     CTC_DKIT_PATH_PRINT("%-15s:%s\n", "result", CTC_DKIT_CAPTURED_NOT_HIT);
                 }
             }

             if (scl_lookup2Valid)
             {
                 CTC_DKIT_PATH_PRINT("%-15s\n", "HASH1 lookup--->");
                 if (hash1_defaultEntryValid)
                 {
                     CTC_DKIT_PATH_PRINT("%-15s:%s\n", "result", CTC_DKIT_CAPTURED_DEFAULT);
                     CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "key index", hash1_keyIndex);
                     CTC_DKIT_PATH_PRINT("%-15s:%s\n", "key name", key_name[scl_vlanHash2Type]);
                 }
                 else if (hash1_egressXcOamHashConflict)
                 {
                     CTC_DKIT_PATH_PRINT("%-15s:%s\n", "result", CTC_DKIT_CAPTURED_CONFLICT);
                     CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "key index", hash1_keyIndex);
                     CTC_DKIT_PATH_PRINT("%-15s:%s\n", "key name", key_name[scl_vlanHash2Type]);
                 }
                 else if (hash1_resultValid)
                 {
                     CTC_DKIT_PATH_PRINT("%-15s:%s\n", "result", CTC_DKIT_CAPTURED_HASH_HIT);
                     CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "key index", hash1_keyIndex);
                     CTC_DKIT_PATH_PRINT("%-15s:%s\n", "key name", key_name[scl_vlanHash2Type]);
                 }
                 else
                 {
                     CTC_DKIT_PATH_PRINT("%-15s:%s\n", "result", CTC_DKIT_CAPTURED_NOT_HIT);
                 }
             }
         }
     }

Discard:
          /*_ctc_goldengate_dkit_captured_path_discard_process(p_info, scl_discard, scl_discardType, CTC_DKIT_EPE);*/

    return CLI_SUCCESS;
}
STATIC int32
_ctc_goldengate_dkit_captured_path_epe_interface(ctc_dkit_captured_path_info_t* p_info)
{
    uint8 slice = p_info->slice_epe;
    uint8 lchip = p_info->lchip;
    uint32 cmd = 0;
    uint32 localPhyPort = 0;
    uint32 interfaceId = 0;
    uint32 destVlanPtr = 0;
    uint32 globalDestPort = 0;
    uint16 gport = 0;
    uint8 gchip = 0;
    uint32 valid = 0;
    uint32 packetOffset = 0;
    uint32 discard = 0;
    uint32 discard_type = 0;

    CTC_DKIT_PRINT("Interface Process:\n");

    cmd = DRV_IOR(DbgEpeHdrAdjInfo_t, DbgEpeHdrAdjInfo_valid_f);
    DRV_IOCTL(lchip, slice, cmd, &valid);
    if (valid)
    {
        cmd = DRV_IOR(DbgEpeHdrAdjInfo_t, DbgEpeHdrAdjInfo_localPhyPort_f);
        DRV_IOCTL(lchip, slice, cmd, &localPhyPort);
        cmd = DRV_IOR(DsDestPort_t, DsDestPort_globalDestPort_f);
        DRV_IOCTL(lchip, (localPhyPort + slice*CTC_DKIT_ONE_SLICE_PORT_NUM), cmd, &globalDestPort);
        if (CTC_DKIT_DRV_GPORT_IS_LINKAGG_PORT(globalDestPort))
        {
            CTC_DKIT_PATH_PRINT("%-15s:%d\n", "linkagg group", CTC_DKIT_DRV_GPORT_TO_LINKAGG_ID(globalDestPort));
        }
        CTC_DKIT_GET_GCHIP(lchip, gchip);
        gport = CTC_DKIT_DRV_LPORT_TO_CTC_GPORT(gchip, (localPhyPort + slice*CTC_DKIT_ONE_SLICE_PORT_NUM));
        CTC_DKIT_PATH_PRINT("%-15s:0x%04x\n", "dest gport", gport);

        cmd = DRV_IOR(DbgEpeHdrAdjInfo_t, DbgEpeHdrAdjInfo_packetOffset_f);
        DRV_IOCTL(lchip, slice, cmd, &packetOffset);
        if (packetOffset)
        {
            CTC_DKIT_PATH_PRINT("%-15s:%d bytes\n", "strip packet", packetOffset);
        }
    }

    cmd = DRV_IOR(DbgEpeNextHopInfo_t, DbgEpeNextHopInfo_valid_f);
    DRV_IOCTL(lchip, slice, cmd, &valid);
    if (valid)
    {
        cmd = DRV_IOR(DbgEpeNextHopInfo_t, DbgEpeNextHopInfo_destVlanPtr_f);
        DRV_IOCTL(lchip, slice, cmd, &destVlanPtr);
        cmd = DRV_IOR(DbgEpeNextHopInfo_t, DbgEpeNextHopInfo_interfaceId_f);
        DRV_IOCTL(lchip, slice, cmd, &interfaceId);
        if (destVlanPtr < 4096)
        {
            CTC_DKIT_PATH_PRINT("%-15s:%d\n", "vlan ptr", destVlanPtr);
        }
        if (interfaceId)
        {
            CTC_DKIT_PATH_PRINT("%-15s:%d\n", "l3 intf id", interfaceId);
        }
    }

    cmd = DRV_IOR(DbgEpeHdrAdjInfo_t, DbgEpeHdrAdjInfo_discard_f);
    DRV_IOCTL(lchip, slice, cmd, &discard);
    cmd = DRV_IOR(DbgEpeHdrAdjInfo_t, DbgEpeHdrAdjInfo_discardType_f);
    DRV_IOCTL(lchip, slice, cmd, &discard_type);
    if (!discard)
    {
        cmd = DRV_IOR(DbgEpeNextHopInfo_t, DbgEpeNextHopInfo_discard_f);
        DRV_IOCTL(lchip, slice, cmd, &discard);
        cmd = DRV_IOR(DbgEpeNextHopInfo_t, DbgEpeNextHopInfo_discardType_f);
        DRV_IOCTL(lchip, slice, cmd, &discard_type);
    }
    _ctc_goldengate_dkit_captured_path_discard_process(p_info, discard, discard_type, CTC_DKIT_EPE);

    return CLI_SUCCESS;
}
STATIC int32
_ctc_goldengate_dkit_captured_path_epe_acl(ctc_dkit_captured_path_info_t* p_info)
{
     uint8 slice = p_info->slice_epe;
     uint8 lchip = p_info->lchip;
     uint32 cmd = 0;
     uint32 temp_key_index = 0;
     char* acl_key_type_desc[4] = {"TCAML2KEY", "TCAML2L3KEY", "TCAML3KEY", "TCAMVLANKEY"};
     /*DbgFlowTcamEngineEpeAclInfo*/
     uint32 tcam_egressAcl0TcamResultValid = 0;
     uint32 tcam_egressAcl0TcamIndex = 0;
     uint32 tcam_valid = 0;
     DbgFlowTcamEngineEpeAclInfo_m dbg_flow_tcam_engine_epe_acl_info;
     /*DbgEpeAclInfo*/
     uint32 acl_discard = 0;
     uint32 acl_discardType = 0;
     uint32 acl_exceptionEn = 0;
     uint32 acl_exceptionIndex = 0;
     uint32 acl_aclEn = 0;
     uint32 acl_aclType = 0;
     uint32 acl_aclLabel = 0;
     uint32 acl_aclLookupType = 0;
     uint32 acl_sclKeyType = 0;
     uint32 acl_flowHasKeyType = 0;
     uint32 acl_flowHashFieldSel = 0;
     uint32 acl_valid = 0;
     DbgEpeAclInfo_m dbg_epe_acl_info;

     /*DbgFlowTcamEngineEpeAclInfo*/
     sal_memset(&dbg_flow_tcam_engine_epe_acl_info, 0, sizeof(dbg_flow_tcam_engine_epe_acl_info));
     cmd = DRV_IOR(DbgFlowTcamEngineEpeAclInfo_t, DRV_ENTRY_FLAG);
     DRV_IOCTL(lchip, 0, cmd, &dbg_flow_tcam_engine_epe_acl_info);
     GetDbgFlowTcamEngineEpeAclInfo(A, egressAcl0TcamResultValid_f, &dbg_flow_tcam_engine_epe_acl_info, &tcam_egressAcl0TcamResultValid);
     GetDbgFlowTcamEngineEpeAclInfo(A, egressAcl0TcamIndex_f, &dbg_flow_tcam_engine_epe_acl_info, &tcam_egressAcl0TcamIndex);
     GetDbgFlowTcamEngineEpeAclInfo(A, valid_f, &dbg_flow_tcam_engine_epe_acl_info, &tcam_valid);
     /*DbgEpeAclInfo*/
     sal_memset(&dbg_epe_acl_info, 0, sizeof(dbg_epe_acl_info));
     cmd = DRV_IOR(DbgEpeAclInfo_t, DRV_ENTRY_FLAG);
     DRV_IOCTL(lchip, slice, cmd, &dbg_epe_acl_info);
     GetDbgEpeAclInfo(A, discard_f, &dbg_epe_acl_info, &acl_discard);
     GetDbgEpeAclInfo(A, discardType_f, &dbg_epe_acl_info, &acl_discardType);
     GetDbgEpeAclInfo(A, exceptionEn_f, &dbg_epe_acl_info, &acl_exceptionEn);
     GetDbgEpeAclInfo(A, exceptionIndex_f, &dbg_epe_acl_info, &acl_exceptionIndex);
     GetDbgEpeAclInfo(A, aclEn_f, &dbg_epe_acl_info, &acl_aclEn);
     GetDbgEpeAclInfo(A, aclType_f, &dbg_epe_acl_info, &acl_aclType);
     GetDbgEpeAclInfo(A, aclLabel_f, &dbg_epe_acl_info, &acl_aclLabel);
     GetDbgEpeAclInfo(A, aclLookupType_f, &dbg_epe_acl_info, &acl_aclLookupType);
     GetDbgEpeAclInfo(A, sclKeyType_f, &dbg_epe_acl_info, &acl_sclKeyType);
     GetDbgEpeAclInfo(A, flowHasKeyType_f, &dbg_epe_acl_info, &acl_flowHasKeyType);
     GetDbgEpeAclInfo(A, flowHashFieldSel_f, &dbg_epe_acl_info, &acl_flowHashFieldSel);
     GetDbgEpeAclInfo(A, valid_f, &dbg_epe_acl_info, &acl_valid);

     if (0 == p_info->detail)
     {
         goto Discard;
     }

     if (acl_aclEn)
     {
         CTC_DKIT_PRINT("ACL Process:\n");
         CTC_DKIT_PATH_PRINT("%-15s\n", "TCAM0 lookup--->");
         CTC_DKIT_PATH_PRINT("%-15s:%s\n", "key type", acl_key_type_desc[acl_aclLookupType]);
         if (tcam_egressAcl0TcamResultValid)
         {
             CTC_DKIT_PATH_PRINT("%-15s:%s\n", "result", CTC_DKIT_CAPTURED_TCAM_HIT);
             temp_key_index = _sys_goldengate_dkit_captured_path_acl_map_key_index(lchip, 1, 0, tcam_egressAcl0TcamIndex);
             CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "key index", temp_key_index);
         }
         else
         {
             CTC_DKIT_PATH_PRINT("%-15s:%s\n", "result", CTC_DKIT_CAPTURED_NOT_HIT);
         }
     }

Discard:
     if (acl_discard)
     {
         if ((acl_discardType + CTC_DKIT_EPE_HDR_ADJ_DESTID_DIS) == CTC_DKIT_EPE_DS_ACL_DIS)
         {
             _ctc_goldengate_dkit_captured_path_discard_process(p_info, acl_discard, acl_discardType, CTC_DKIT_EPE);
         }
     }

     if (acl_exceptionEn)
     {
         CTC_DKIT_PATH_PRINT("%-15s:%s!!!\n", "Exception", "To CPU");
     }

    return CLI_SUCCESS;
}
STATIC int32
_ctc_goldengate_dkit_captured_path_epe_oam(ctc_dkit_captured_path_info_t* p_info)
{
    uint8 slice = p_info->slice_epe;
    uint8 lchip = p_info->lchip;
    uint32 cmd = 0;
    uint8 i = 0;
    char* lookup_result[4] = {"ETHER lookup--->", "BFD lookup--->", "TP lookup--->", "Section lookup--->"};
    char* key_name[4] = {"DsEgressXcOamEthHashKey", "DsEgressXcOamBfdHashKey",
                                             "DsEgressXcOamMplsLabelHashKey", "DsEgressXcOamMplsSectionHashKey"};

    /*DbgEgrXcOamHashEngineFromEpeOam0Info*/
    uint32 hash0_resultValid = 0;
    uint32 hash0_egressXcOamHashConflict = 0;
    uint32 hash0_keyIndex = 0;
    uint32 hash0_valid = 0;
    DbgEgrXcOamHashEngineFromEpeOam0Info_m dbg_egr_xc_oam_hash_engine_from_epe_oam0_info;
    /*DbgEgrXcOamHashEngineFromEpeOam1Info*/
    uint32 hash1_resultValid = 0;
    uint32 hash1_egressXcOamHashConflict = 0;
    uint32 hash1_keyIndex = 0;
    uint32 hash1_valid = 0;
    DbgEgrXcOamHashEngineFromEpeOam1Info_m dbg_egr_xc_oam_hash_engine_from_epe_oam1_info;
    /*DbgEgrXcOamHashEngineFromEpeOam2Info*/
    uint32 hash2_resultValid = 0;
    uint32 hash2_egressXcOamHashConflict = 0;
    uint32 hash2_keyIndex = 0;
    uint32 hash2_valid = 0;
    DbgEgrXcOamHashEngineFromEpeOam2Info_m dbg_egr_xc_oam_hash_engine_from_epe_oam2_info;
    /*DbgEpeAclInfo*/
    uint32 lkp_egressXcOamKeyType0 = 0;
    uint32 lkp_egressXcOamKeyType1 = 0;
    uint32 lkp_egressXcOamKeyType2 = 0;
    uint32 lkp_valid = 0;
    DbgEpeAclInfo_m dbg_epe_acl_info;
    /*DbgEpeOamInfo*/
    uint32 oam_lmStatsEn0 = 0;
    uint32 oam_lmStatsEn1 = 0;
    uint32 oam_lmStatsEn2 = 0;
    uint32 oam_lmStatsIndex = 0;
    uint32 oam_lmStatsPtr0 = 0;
    uint32 oam_lmStatsPtr1 = 0;
    uint32 oam_lmStatsPtr2 = 0;
    uint32 oam_etherLmValid = 0;
    uint32 oam_mplsLmValid = 0;
    uint32 oam_oamDestChipId = 0;
    uint32 oam_mepIndex = 0;
    uint32 oam_mepEn = 0;
    uint32 oam_mipEn = 0;
    uint32 oam_tempRxOamType = 0;
    uint32 oam_valid = 0;
    DbgEpeOamInfo_m dbg_epe_oam_info;

    /*DbgEpeHdrEditInfo*/
    uint32 hdr_valid = 0;
    uint32 hdr_shareType = 0;
    uint32 hdr_loopbackEn = 0;

    uint8 key_type[3] = {lkp_egressXcOamKeyType0, lkp_egressXcOamKeyType1, lkp_egressXcOamKeyType2};
    uint8 hash_valid[3] = {hash0_valid, hash1_valid, hash2_valid};
    uint8 confict[3] = {hash0_egressXcOamHashConflict, hash1_egressXcOamHashConflict, hash2_egressXcOamHashConflict};
    uint32 key_index[3] = {hash0_keyIndex, hash1_keyIndex, hash2_keyIndex};
    uint8 hash_resultValid[3] = {hash0_resultValid,hash1_resultValid,hash2_resultValid};

    /*DbgEgrXcOamHashEngineFromEpeOam0Info*/
    sal_memset(&dbg_egr_xc_oam_hash_engine_from_epe_oam0_info, 0, sizeof(dbg_egr_xc_oam_hash_engine_from_epe_oam0_info));
    cmd = DRV_IOR(DbgEgrXcOamHashEngineFromEpeOam0Info_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, slice, cmd, &dbg_egr_xc_oam_hash_engine_from_epe_oam0_info);
    GetDbgEgrXcOamHashEngineFromEpeOam0Info(A, resultValid_f, &dbg_egr_xc_oam_hash_engine_from_epe_oam0_info, &hash0_resultValid);
    GetDbgEgrXcOamHashEngineFromEpeOam0Info(A, egressXcOamHashConflict_f, &dbg_egr_xc_oam_hash_engine_from_epe_oam0_info, &hash0_egressXcOamHashConflict);
    GetDbgEgrXcOamHashEngineFromEpeOam0Info(A, keyIndex_f, &dbg_egr_xc_oam_hash_engine_from_epe_oam0_info, &hash0_keyIndex);
    GetDbgEgrXcOamHashEngineFromEpeOam0Info(A, valid_f, &dbg_egr_xc_oam_hash_engine_from_epe_oam0_info, &hash0_valid);
    /*DbgEgrXcOamHashEngineFromEpeOam1Info*/
    sal_memset(&dbg_egr_xc_oam_hash_engine_from_epe_oam1_info, 0, sizeof(dbg_egr_xc_oam_hash_engine_from_epe_oam1_info));
    cmd = DRV_IOR(DbgEgrXcOamHashEngineFromEpeOam1Info_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, slice, cmd, &dbg_egr_xc_oam_hash_engine_from_epe_oam1_info);
    GetDbgEgrXcOamHashEngineFromEpeOam1Info(A, resultValid_f, &dbg_egr_xc_oam_hash_engine_from_epe_oam1_info, &hash1_resultValid);
    GetDbgEgrXcOamHashEngineFromEpeOam1Info(A, egressXcOamHashConflict_f, &dbg_egr_xc_oam_hash_engine_from_epe_oam1_info, &hash1_egressXcOamHashConflict);
    GetDbgEgrXcOamHashEngineFromEpeOam1Info(A, keyIndex_f, &dbg_egr_xc_oam_hash_engine_from_epe_oam1_info, &hash1_keyIndex);
    GetDbgEgrXcOamHashEngineFromEpeOam1Info(A, valid_f, &dbg_egr_xc_oam_hash_engine_from_epe_oam1_info, &hash1_valid);
    /*DbgEgrXcOamHashEngineFromEpeOam2Info*/
    sal_memset(&dbg_egr_xc_oam_hash_engine_from_epe_oam2_info, 0, sizeof(dbg_egr_xc_oam_hash_engine_from_epe_oam2_info));
    cmd = DRV_IOR(DbgEgrXcOamHashEngineFromEpeOam2Info_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, slice, cmd, &dbg_egr_xc_oam_hash_engine_from_epe_oam2_info);
    GetDbgEgrXcOamHashEngineFromEpeOam2Info(A, resultValid_f, &dbg_egr_xc_oam_hash_engine_from_epe_oam2_info, &hash2_resultValid);
    GetDbgEgrXcOamHashEngineFromEpeOam2Info(A, egressXcOamHashConflict_f, &dbg_egr_xc_oam_hash_engine_from_epe_oam2_info, &hash2_egressXcOamHashConflict);
    GetDbgEgrXcOamHashEngineFromEpeOam2Info(A, keyIndex_f, &dbg_egr_xc_oam_hash_engine_from_epe_oam2_info, &hash2_keyIndex);
    GetDbgEgrXcOamHashEngineFromEpeOam2Info(A, valid_f, &dbg_egr_xc_oam_hash_engine_from_epe_oam2_info, &hash2_valid);
    /*DbgEpeAclInfo*/
    sal_memset(&dbg_epe_acl_info, 0, sizeof(dbg_epe_acl_info));
    cmd = DRV_IOR(DbgEpeAclInfo_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, slice, cmd, &dbg_epe_acl_info);
    GetDbgEpeAclInfo(A, egressXcOamKeyType0_f, &dbg_epe_acl_info, &lkp_egressXcOamKeyType0);
    GetDbgEpeAclInfo(A, egressXcOamKeyType1_f, &dbg_epe_acl_info, &lkp_egressXcOamKeyType1);
    GetDbgEpeAclInfo(A, egressXcOamKeyType2_f, &dbg_epe_acl_info, &lkp_egressXcOamKeyType2);
    GetDbgEpeAclInfo(A, valid_f, &dbg_epe_acl_info, &lkp_valid);
    /*DbgEpeOamInfo*/
    sal_memset(&dbg_epe_oam_info, 0, sizeof(dbg_epe_oam_info));
    cmd = DRV_IOR(DbgEpeOamInfo_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, slice, cmd, &dbg_epe_oam_info);
    GetDbgEpeOamInfo(A, lmStatsEn0_f, &dbg_epe_oam_info, &oam_lmStatsEn0);
    GetDbgEpeOamInfo(A, lmStatsEn1_f, &dbg_epe_oam_info, &oam_lmStatsEn1);
    GetDbgEpeOamInfo(A, lmStatsEn2_f, &dbg_epe_oam_info, &oam_lmStatsEn2);
    GetDbgEpeOamInfo(A, lmStatsIndex_f, &dbg_epe_oam_info, &oam_lmStatsIndex);
    GetDbgEpeOamInfo(A, lmStatsPtr0_f, &dbg_epe_oam_info, &oam_lmStatsPtr0);
    GetDbgEpeOamInfo(A, lmStatsPtr1_f, &dbg_epe_oam_info, &oam_lmStatsPtr1);
    GetDbgEpeOamInfo(A, lmStatsPtr2_f, &dbg_epe_oam_info, &oam_lmStatsPtr2);
    GetDbgEpeOamInfo(A, etherLmValid_f, &dbg_epe_oam_info, &oam_etherLmValid);
    GetDbgEpeOamInfo(A, mplsLmValid_f, &dbg_epe_oam_info, &oam_mplsLmValid);
    GetDbgEpeOamInfo(A, oamDestChipId_f, &dbg_epe_oam_info, &oam_oamDestChipId);
    GetDbgEpeOamInfo(A, mepIndex_f, &dbg_epe_oam_info, &oam_mepIndex);
    GetDbgEpeOamInfo(A, mepEn_f, &dbg_epe_oam_info, &oam_mepEn);
    GetDbgEpeOamInfo(A, mipEn_f, &dbg_epe_oam_info, &oam_mipEn);
    GetDbgEpeOamInfo(A, tempRxOamType_f, &dbg_epe_oam_info, &oam_tempRxOamType);
    GetDbgEpeOamInfo(A, valid_f, &dbg_epe_oam_info, &oam_valid);

    /*DbgEpeHdrEditInfo*/
    cmd = DRV_IOR(DbgEpeHdrEditInfo_t, DbgEpeHdrEditInfo_valid_f);
    DRV_IOCTL(lchip, slice, cmd, &hdr_valid);
    cmd = DRV_IOR(DbgEpeHdrEditInfo_t, DbgEpeHdrEditInfo_shareType_f);
    DRV_IOCTL(lchip, slice, cmd, &hdr_shareType);
    cmd = DRV_IOR(DbgEpeHdrEditInfo_t, DbgEpeHdrEditInfo_loopbackEn_f);
    DRV_IOCTL(lchip, slice, cmd, &hdr_loopbackEn);

    if (0 == p_info->detail)
    {
        goto Discard;
    }

    if (lkp_egressXcOamKeyType0 || lkp_egressXcOamKeyType1 || lkp_egressXcOamKeyType2)
    {

        CTC_DKIT_PRINT("OAM Process:\n");

        for (i = 0; i < 3; i++)
        {
            if (key_type[i])
            {
                CTC_DKIT_PATH_PRINT("%s%d %s\n", "hash",i,lookup_result[key_type[i] - EGRESSXCOAMHASHTYPE_ETH]);
                if (hash_valid[i])
                {
                    if (confict[i])
                    {
                        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "result", CTC_DKIT_CAPTURED_CONFLICT);
                        CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "key index", key_index[i]);
                        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "key name", key_name[key_type[i] - EGRESSXCOAMHASHTYPE_ETH]);
                    }
                    else if (hash_resultValid[i])
                    {
                        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "result", CTC_DKIT_CAPTURED_HASH_HIT);
                        CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "key index", key_index[i]);
                        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "key name", key_name[key_type[i] - EGRESSXCOAMHASHTYPE_ETH]);
                    }
                    else
                    {
                        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "result", CTC_DKIT_CAPTURED_NOT_HIT);
                    }
                }
            }
        }
    }

    if (oam_valid)
    {
        if (hash0_resultValid || hash1_resultValid || hash2_resultValid)
        {

            if (oam_mepEn)
            {
                CTC_DKIT_PATH_PRINT("%-15s:%s\n", "hit MEP", "YES");
                CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "mep index", oam_mepIndex);
                CTC_DKIT_PATH_PRINT("%-15s:%d\n", "Master chip id", oam_oamDestChipId);
            }
            if (oam_mipEn)
            {
                CTC_DKIT_PATH_PRINT("%-15s:%s\n", "hit MIP", "YES");
                CTC_DKIT_PATH_PRINT("%-15s:%d\n", "Master chip id", oam_oamDestChipId);
            }
            if (oam_tempRxOamType)
            {
                char* oam_type[RXOAMTYPE_TRILLBFD] =
                {
                    "ETHER OAM", "IP BFD", "PBT OAM", "PBBBSI", "PBBBV",
                                                                                      "MPLS OAM", "MPLS BFD", "ACH OAM", "RSV", "TRILL BFD"};
                    CTC_DKIT_PATH_PRINT("%-15s:%s\n", "OAM type", oam_type[oam_tempRxOamType - 1]);
                }
                if (oam_mplsLmValid || oam_etherLmValid)
                {
                    CTC_DKIT_PATH_PRINT("%-15s:%s 0x%x\n", "read LM from", "DsOamLmStats", oam_lmStatsIndex);
                }
                if (oam_lmStatsEn0)
                {
                    CTC_DKIT_PATH_PRINT("%-15s:%s 0x%x\n", "write LM to", "DsOamLmStats", oam_lmStatsPtr0);
                }
                if (oam_lmStatsEn1)
                {
                    CTC_DKIT_PATH_PRINT("%-15s:%s 0x%x\n", "write LM to", "DsOamLmStats", oam_lmStatsPtr1);
                }
                if (oam_lmStatsEn2)
                {
                    CTC_DKIT_PATH_PRINT("%-15s:%s 0x%x\n", "write LM to", "DsOamLmStats", oam_lmStatsPtr2);
                }
            }
        }

    if ((hdr_valid) && (SHARETYPE_OAM == hdr_shareType))
    {
        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "destination", "OAM engine");
    }

Discard:

    return CLI_SUCCESS;
}
STATIC int32
_ctc_goldengate_dkit_captured_path_epe_edit(ctc_dkit_captured_path_info_t* p_info)
{
    uint8 slice = p_info->slice_epe;
    uint8 lchip = p_info->lchip;
    uint32 cmd = 0;
    uint32 value = 0;
    uint32 next_edit_ptr = 0;
    uint32 next_edit_type = 0;
    uint32 dsNextHopInternalBase = 0;
    uint32 nhpShareType = 0;
    uint32 discard = 0;
    uint32 discard_type = 0;
    uint8 do_l2_edit = 0;
    uint32 l2_edit_ptr = 0;
    uint8 l2_individual_memory = 0;
    uint32 dest_map = 0;
    uint8 no_l3_edit = 1;
    uint8 no_l2_edit = 1;
    uint32 nhpOuterEditPtrType = 0;

    hw_mac_addr_t hw_mac = {0};
    mac_addr_t mac_da = {0};
    DsNextHop8W_m ds_next_hop8_w;
    DsNextHop4W_m ds_next_hop4_w;
    DsL2EditEth3W_m ds_l2_edit_eth3_w;
    DsL3EditMpls3W_m ds_l3_edit_mpls3_w;
    DsL2Edit6WOuter_m ds_l2_edit6_w_outer;
    DsL2EditSwap_m ds_l2_edit_swap;
    DsL2EditInnerSwap_m ds_l2_edit_inner_swap;
    char* l3_edit[12] =
    {
        "NONE edit--->",
        "MPLS edit--->",
        "RESERVED edit--->",
        "NAT edit--->",
        "NAT edit--->",
        "TUNNELV4 edit--->",
        "TUNNELV6 edit--->",
        "L3FLEX edit--->",
        "OPEN FLOW edit--->",
        "OPEN FLOW edit--->",
        "LOOPBACK edit--->",
        "TRILL edit--->"
    };
    char* l3_edit_name[12] =
    {
        " ",
        "DsL3EditMpls3W",
        " ",
        "DsL3EditNat3W",
        "DsL3EditNat6W",
        "DsL3EditTunnelV4",
        "DsL3EditTunnelV6",
        "DsL3EditFlex",
        "DsL3EditOf6W",
        "DsL3EditOf12W",
        "DsL3EditLoopback",
        "DsL3EditTrill"
    };
    char* vlan_action[4] = {"None", "Replace", "Add","Delete"};

    /*DbgEpeHdrAdjInfo*/
    uint32 adj_channelId = 0;
    uint32 adj_packetHeaderEn = 0;
    uint32 adj_packetHeaderEnEgress = 0;
    uint32 adj_bypassAll = 0;
    uint32 adj_discard = 0;
    uint32 adj_discardType = 0;
    uint32 adj_microBurstValid = 0;
    uint32 adj_isSendtoCpuEn = 0;
    uint32 adj_packetLengthIsTrue = 0;
    uint32 adj_localPhyPort = 0;
    uint32 adj_nextHopPtr = 0;
    uint32 adj_parserOffset = 0;
    uint32 adj_packetLengthAdjustType = 0;
    uint32 adj_packetLengthAdjust = 0;
    uint32 adj_sTagAction = 0;
    uint32 adj_cTagAction = 0;
    uint32 adj_packetOffset = 0;
    uint32 adj_aclDscpValid = 0;
    uint32 adj_aclDscp = 0;
    uint32 adj_newIpChecksum = 0;
    uint32 adj_isidValid = 0;
    uint32 adj_muxLengthType = 0;
    uint32 adj_cvlanTagOperationValid = 0;
    uint32 adj_svlanTagOperationValid = 0;
    uint32 adj_debugOpType = 0;
    uint32 adj_valid = 0;
    DbgEpeHdrAdjInfo_m dbg_epe_hdr_adj_info;
    /*DbgEpeNextHopInfo*/
    uint32 nhp_discard = 0;
    uint32 nhp_discardType = 0;
    uint32 nhp_payloadOperation = 0;
    uint32 nhp_editBypass = 0;
    uint32 nhp_innerEditHashEn = 0;
    uint32 nhp_outerEditHashEn = 0;
    uint32 nhp_etherOamDiscard = 0;
    uint32 nhp_isVxlanRouteOp = 0;
    uint32 nhp_exceptionEn = 0;
    uint32 nhp_exceptionIndex = 0;
    uint32 nhp_portReflectiveDiscard = 0;
    uint32 nhp_interfaceId = 0;
    uint32 nhp_destVlanPtr = 0;
    uint32 nhp_mcast = 0;
    uint32 nhp_routeNoL2Edit = 0;
    uint32 nhp_outputSvlanIdValid = 0;
    uint32 nhp_outputSvlanId = 0;
    uint32 nhp_logicDestPort = 0;
    uint32 nhp_outputCvlanIdValid = 0;
    uint32 nhp_outputCvlanId = 0;
    uint32 nhp__priority = 0;
    uint32 nhp_color = 0;
    uint32 nhp_l3EditDisable = 0;
    uint32 nhp_l2EditDisable = 0;
    uint32 nhp_isEnd = 0;
    uint32 nhp_bypassAll = 0;
    uint32 nhp_nhpIs8w = 0;
    uint32 nhp_portIsolateValid = 0;
    uint32 nhp_lookup1Valid = 0;
    uint32 nhp_vlanHash1Type = 0;
    uint32 nhp_lookup2Valid = 0;
    uint32 nhp_vlanHash2Type = 0;
    uint32 nhp_vlanXlateResult1Valid = 0;
    uint32 nhp_vlanXlateResult2Valid = 0;
    uint32 nhp_hashPort = 0;
    uint32 nhp_isLabel = 0;
    uint32 nhp_svlanId = 0;
    uint32 nhp_stagCos = 0;
    uint32 nhp_cvlanId = 0;
    uint32 nhp_ctagCos = 0;
    uint32 nhp_vlanXlateVlanRangeValid = 0;
    uint32 nhp_vlanXlateVlanRangeMax = 0;
    uint32 nhp_aclVlanRangeValid = 0;
    uint32 nhp_aclVlanRangeMax = 0;
    uint32 nhp_innerEditPtrOffset = 0;
    uint32 nhp_outerEditPtrOffset = 0;
    uint32 nhp_qosDomain = 0;
    uint32 nhp_valid = 0;
    DbgEpeNextHopInfo_m dbg_epe_next_hop_info;
    /*DbgEpePayLoadInfo*/
    uint32 pld_discard = 0;
    uint32 pld_discardType = 0;
    uint32 pld_exceptionEn = 0;
    uint32 pld_exceptionIndex = 0;
    uint32 pld_shareType = 0;
    uint32 pld_fid = 0;
    uint32 pld_payloadOperation = 0;
    uint32 pld_packetLengthAdjustType = 0;
    uint32 pld_packetLengthAdjust = 0;
    uint32 pld_newTtlValid = 0;
    uint32 pld_newTtl = 0;
    uint32 pld_newDscpValid = 0;
    uint32 pld_replaceDscp = 0;
    uint32 pld_useOamTtlOrExp = 0;
    uint32 pld_copyDscp = 0;
    uint32 pld_newDscp = 0;
    uint32 pld_vlanXlateMode = 0;
    uint32 pld_svlanTagOperation = 0;
    uint32 pld_l2NewSvlanTag = 0;
    uint32 pld_cvlanTagOperation = 0;
    uint32 pld_l2NewCvlanTag = 0;
    uint32 pld_stripOffset = 0;
    uint32 pld_mirroredPacket = 0;
    uint32 pld_isL2Ptp = 0;
    uint32 pld_isL4Ptp = 0;
    uint32 pld_valid = 0;
    DbgEpePayLoadInfo_m dbg_epe_pay_load_info;
    /*DbgEpeEgressEditInfo*/
    uint32 edit_discard = 0;
    uint32 edit_discardType = 0;
    uint32 edit_nhpOuterEditLocation = 0;
    uint32 edit_innerEditExist = 0;
    uint32 edit_innerEditPtrType = 0;
    uint32 edit_nhpInnerEditPtr = 0;
    uint32 edit_outerEditPipe1Exist = 0;
    uint32 edit_outerEditPipe1Type = 0;
    uint32 edit_nhpOuterEditPtr = 0;
    uint32 edit_outerEditPipe2Exist = 0;
    uint32 edit_outerEditPipe3Exist = 0;
    uint32 edit_outerEditPipe3Type = 0;
    uint32 edit_l3RewriteType = 0;
    uint32 edit_l3EditMplsPwExist = 0;
    uint32 edit_l3EditMplsLspExist = 0;
    uint32 edit_l3EditMplsSpmeExist = 0;
    uint32 edit_innerL2RewriteType = 0;
    uint32 edit_l2RewriteType = 0;
    uint32 edit_valid = 0;
    DbgEpeEgressEditInfo_m dbg_epe_egress_edit_info;
    /*DbgEpeInnerL2EditInfo*/
    uint32 innerL2_valid = 0;
    uint32 innerL2_isInnerL2EthSwapOp = 0;
    uint32 innerL2_isInnerL2EthAddOp = 0;
    uint32 innerL2_isInnerDsLiteOp = 0;
    DbgEpeInnerL2EditInfo_m dbg_epe_inner_l2_edit_info;
    /*DbgEpeOuterL2EditInfo*/
    uint32 l2edit_l2EditDisable = 0;
    uint32 l2edit_routeNoL2Edit = 0;
    uint32 l2edit_destMuxPortType = 0;
    uint32 l2edit_newMacSaValid = 0;
    uint32 l2edit_newMacDaValid = 0;
    uint32 l2edit_portMacSaEn = 0;
    uint32 l2edit_loopbackEn = 0;
    uint32 l2edit_svlanTagOperation = 0;
    uint32 l2edit_cvlanTagOperation = 0;
    uint32 l2edit_l2RewriteType = 0;
    uint32 l2edit_isEnd = 0;
    uint32 l2edit_len = 0;
    uint32 l2edit_valid = 0;
    DbgEpeOuterL2EditInfo_m dbg_epe_outer_l2_edit_info;
    /*DbgEpeL3EditInfo*/
    uint32 l3edit_l3EditDisable = 0;
    uint32 l3edit_loopbackEn = 0;
    uint32 l3edit_l3RewriteType = 0;
    uint32 l3edit_label0Valid = 0;
    uint32 l3edit_label1Valid = 0;
    uint32 l3edit_label2Valid = 0;
    uint32 l3edit_isLspLabelEntropy0 = 0;
    uint32 l3edit_isLspLabelEntropy1 = 0;
    uint32 l3edit_isLspLabelEntropy2 = 0;
    uint32 l3edit_isPwLabelEntropy0 = 0;
    uint32 l3edit_isPwLabelEntropy1 = 0;
    uint32 l3edit_isPwLabelEntropy2 = 0;
    uint32 l3edit_cwLabelValid = 0;
    uint32 l3edit_flexEditL3Type = 0;
    uint32 l3edit_flexEditL4Type = 0;
    uint32 l3edit_isEnd = 0;
    uint32 l3edit_len = 0;
    uint32 l3edit_valid = 0;
    DbgEpeL3EditInfo_m dbg_epe_l3_edit_info;

    /*DbgEpeHdrEditInfo*/
    uint32 hdr_valid = 0;
    uint32 hdr_stripOffset = 0;

    cmd = DRV_IOR(DbgEpeHdrEditInfo_t, DbgEpeHdrEditInfo_valid_f);
    DRV_IOCTL(lchip, slice, cmd, &hdr_valid);
    cmd = DRV_IOR(DbgEpeHdrEditInfo_t, DbgEpeHdrEditInfo_stripOffset_f);
    DRV_IOCTL(lchip, slice, cmd, &hdr_stripOffset);

    sal_memset(&ds_next_hop8_w, 0, sizeof(ds_next_hop8_w));
    sal_memset(&ds_next_hop4_w, 0, sizeof(ds_next_hop4_w));
    sal_memset(&ds_l2_edit_eth3_w, 0, sizeof(ds_l2_edit_eth3_w));
    sal_memset(&ds_l3_edit_mpls3_w, 0, sizeof(ds_l3_edit_mpls3_w));
    sal_memset(&ds_l2_edit6_w_outer, 0, sizeof(ds_l2_edit6_w_outer));
    sal_memset(&ds_l2_edit_swap, 0, sizeof(ds_l2_edit_swap));
    sal_memset(&ds_l2_edit_inner_swap, 0, sizeof(ds_l2_edit_inner_swap));

    /*DbgEpeHdrAdjInfo*/
    sal_memset(&dbg_epe_hdr_adj_info, 0, sizeof(dbg_epe_hdr_adj_info));
    cmd = DRV_IOR(DbgEpeHdrAdjInfo_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, slice, cmd, &dbg_epe_hdr_adj_info);
    GetDbgEpeHdrAdjInfo(A, channelId_f, &dbg_epe_hdr_adj_info, &adj_channelId);
    GetDbgEpeHdrAdjInfo(A, packetHeaderEn_f, &dbg_epe_hdr_adj_info, &adj_packetHeaderEn);
    GetDbgEpeHdrAdjInfo(A, packetHeaderEnEgress_f, &dbg_epe_hdr_adj_info, &adj_packetHeaderEnEgress);
    GetDbgEpeHdrAdjInfo(A, bypassAll_f, &dbg_epe_hdr_adj_info, &adj_bypassAll);
    GetDbgEpeHdrAdjInfo(A, discard_f, &dbg_epe_hdr_adj_info, &adj_discard);
    GetDbgEpeHdrAdjInfo(A, discardType_f, &dbg_epe_hdr_adj_info, &adj_discardType);
    GetDbgEpeHdrAdjInfo(A, microBurstValid_f, &dbg_epe_hdr_adj_info, &adj_microBurstValid);
    GetDbgEpeHdrAdjInfo(A, isSendtoCpuEn_f, &dbg_epe_hdr_adj_info, &adj_isSendtoCpuEn);
    GetDbgEpeHdrAdjInfo(A, packetLengthIsTrue_f, &dbg_epe_hdr_adj_info, &adj_packetLengthIsTrue);
    GetDbgEpeHdrAdjInfo(A, localPhyPort_f, &dbg_epe_hdr_adj_info, &adj_localPhyPort);
    GetDbgEpeHdrAdjInfo(A, nextHopPtr_f, &dbg_epe_hdr_adj_info, &adj_nextHopPtr);
    GetDbgEpeHdrAdjInfo(A, parserOffset_f, &dbg_epe_hdr_adj_info, &adj_parserOffset);
    GetDbgEpeHdrAdjInfo(A, packetLengthAdjustType_f, &dbg_epe_hdr_adj_info, &adj_packetLengthAdjustType);
    GetDbgEpeHdrAdjInfo(A, packetLengthAdjust_f, &dbg_epe_hdr_adj_info, &adj_packetLengthAdjust);
    GetDbgEpeHdrAdjInfo(A, sTagAction_f, &dbg_epe_hdr_adj_info, &adj_sTagAction);
    GetDbgEpeHdrAdjInfo(A, cTagAction_f, &dbg_epe_hdr_adj_info, &adj_cTagAction);
    GetDbgEpeHdrAdjInfo(A, packetOffset_f, &dbg_epe_hdr_adj_info, &adj_packetOffset);
    GetDbgEpeHdrAdjInfo(A, aclDscpValid_f, &dbg_epe_hdr_adj_info, &adj_aclDscpValid);
    GetDbgEpeHdrAdjInfo(A, aclDscp_f, &dbg_epe_hdr_adj_info, &adj_aclDscp);
    GetDbgEpeHdrAdjInfo(A, newIpChecksum_f, &dbg_epe_hdr_adj_info, &adj_newIpChecksum);
    GetDbgEpeHdrAdjInfo(A, isidValid_f, &dbg_epe_hdr_adj_info, &adj_isidValid);
    GetDbgEpeHdrAdjInfo(A, muxLengthType_f, &dbg_epe_hdr_adj_info, &adj_muxLengthType);
    GetDbgEpeHdrAdjInfo(A, cvlanTagOperationValid_f, &dbg_epe_hdr_adj_info, &adj_cvlanTagOperationValid);
    GetDbgEpeHdrAdjInfo(A, svlanTagOperationValid_f, &dbg_epe_hdr_adj_info, &adj_svlanTagOperationValid);
    GetDbgEpeHdrAdjInfo(A, debugOpType_f, &dbg_epe_hdr_adj_info, &adj_debugOpType);
    GetDbgEpeHdrAdjInfo(A, valid_f, &dbg_epe_hdr_adj_info, &adj_valid);
    /*DbgEpeNextHopInfo*/
    sal_memset(&dbg_epe_next_hop_info, 0, sizeof(dbg_epe_next_hop_info));
    cmd = DRV_IOR(DbgEpeNextHopInfo_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, slice, cmd, &dbg_epe_next_hop_info);
    GetDbgEpeNextHopInfo(A, discard_f, &dbg_epe_next_hop_info, &nhp_discard);
    GetDbgEpeNextHopInfo(A, discardType_f, &dbg_epe_next_hop_info, &nhp_discardType);
    GetDbgEpeNextHopInfo(A, payloadOperation_f, &dbg_epe_next_hop_info, &nhp_payloadOperation);
    GetDbgEpeNextHopInfo(A, editBypass_f, &dbg_epe_next_hop_info, &nhp_editBypass);
    GetDbgEpeNextHopInfo(A, innerEditHashEn_f, &dbg_epe_next_hop_info, &nhp_innerEditHashEn);
    GetDbgEpeNextHopInfo(A, outerEditHashEn_f, &dbg_epe_next_hop_info, &nhp_outerEditHashEn);
    GetDbgEpeNextHopInfo(A, etherOamDiscard_f, &dbg_epe_next_hop_info, &nhp_etherOamDiscard);
    GetDbgEpeNextHopInfo(A, isVxlanRouteOp_f, &dbg_epe_next_hop_info, &nhp_isVxlanRouteOp);
    GetDbgEpeNextHopInfo(A, exceptionEn_f, &dbg_epe_next_hop_info, &nhp_exceptionEn);
    GetDbgEpeNextHopInfo(A, exceptionIndex_f, &dbg_epe_next_hop_info, &nhp_exceptionIndex);
    GetDbgEpeNextHopInfo(A, portReflectiveDiscard_f, &dbg_epe_next_hop_info, &nhp_portReflectiveDiscard);
    GetDbgEpeNextHopInfo(A, interfaceId_f, &dbg_epe_next_hop_info, &nhp_interfaceId);
    GetDbgEpeNextHopInfo(A, destVlanPtr_f, &dbg_epe_next_hop_info, &nhp_destVlanPtr);
    GetDbgEpeNextHopInfo(A, mcast_f, &dbg_epe_next_hop_info, &nhp_mcast);
    GetDbgEpeNextHopInfo(A, routeNoL2Edit_f, &dbg_epe_next_hop_info, &nhp_routeNoL2Edit);
    GetDbgEpeNextHopInfo(A, outputSvlanIdValid_f, &dbg_epe_next_hop_info, &nhp_outputSvlanIdValid);
    GetDbgEpeNextHopInfo(A, outputSvlanId_f, &dbg_epe_next_hop_info, &nhp_outputSvlanId);
    GetDbgEpeNextHopInfo(A, logicDestPort_f, &dbg_epe_next_hop_info, &nhp_logicDestPort);
    GetDbgEpeNextHopInfo(A, outputCvlanIdValid_f, &dbg_epe_next_hop_info, &nhp_outputCvlanIdValid);
    GetDbgEpeNextHopInfo(A, outputCvlanId_f, &dbg_epe_next_hop_info, &nhp_outputCvlanId);
    GetDbgEpeNextHopInfo(A, _priority_f, &dbg_epe_next_hop_info, &nhp__priority);
    GetDbgEpeNextHopInfo(A, color_f, &dbg_epe_next_hop_info, &nhp_color);
    GetDbgEpeNextHopInfo(A, l3EditDisable_f, &dbg_epe_next_hop_info, &nhp_l3EditDisable);
    GetDbgEpeNextHopInfo(A, l2EditDisable_f, &dbg_epe_next_hop_info, &nhp_l2EditDisable);
    GetDbgEpeNextHopInfo(A, isEnd_f, &dbg_epe_next_hop_info, &nhp_isEnd);
    GetDbgEpeNextHopInfo(A, bypassAll_f, &dbg_epe_next_hop_info, &nhp_bypassAll);
    GetDbgEpeNextHopInfo(A, nhpIs8w_f, &dbg_epe_next_hop_info, &nhp_nhpIs8w);
    GetDbgEpeNextHopInfo(A, portIsolateValid_f, &dbg_epe_next_hop_info, &nhp_portIsolateValid);
    GetDbgEpeNextHopInfo(A, lookup1Valid_f, &dbg_epe_next_hop_info, &nhp_lookup1Valid);
    GetDbgEpeNextHopInfo(A, vlanHash1Type_f, &dbg_epe_next_hop_info, &nhp_vlanHash1Type);
    GetDbgEpeNextHopInfo(A, lookup2Valid_f, &dbg_epe_next_hop_info, &nhp_lookup2Valid);
    GetDbgEpeNextHopInfo(A, vlanHash2Type_f, &dbg_epe_next_hop_info, &nhp_vlanHash2Type);
    GetDbgEpeNextHopInfo(A, vlanXlateResult1Valid_f, &dbg_epe_next_hop_info, &nhp_vlanXlateResult1Valid);
    GetDbgEpeNextHopInfo(A, vlanXlateResult2Valid_f, &dbg_epe_next_hop_info, &nhp_vlanXlateResult2Valid);
    GetDbgEpeNextHopInfo(A, hashPort_f, &dbg_epe_next_hop_info, &nhp_hashPort);
    GetDbgEpeNextHopInfo(A, isLabel_f, &dbg_epe_next_hop_info, &nhp_isLabel);
    GetDbgEpeNextHopInfo(A, svlanId_f, &dbg_epe_next_hop_info, &nhp_svlanId);
    GetDbgEpeNextHopInfo(A, stagCos_f, &dbg_epe_next_hop_info, &nhp_stagCos);
    GetDbgEpeNextHopInfo(A, cvlanId_f, &dbg_epe_next_hop_info, &nhp_cvlanId);
    GetDbgEpeNextHopInfo(A, ctagCos_f, &dbg_epe_next_hop_info, &nhp_ctagCos);
    GetDbgEpeNextHopInfo(A, vlanXlateVlanRangeValid_f, &dbg_epe_next_hop_info, &nhp_vlanXlateVlanRangeValid);
    GetDbgEpeNextHopInfo(A, vlanXlateVlanRangeMax_f, &dbg_epe_next_hop_info, &nhp_vlanXlateVlanRangeMax);
    GetDbgEpeNextHopInfo(A, aclVlanRangeValid_f, &dbg_epe_next_hop_info, &nhp_aclVlanRangeValid);
    GetDbgEpeNextHopInfo(A, aclVlanRangeMax_f, &dbg_epe_next_hop_info, &nhp_aclVlanRangeMax);
    GetDbgEpeNextHopInfo(A, innerEditPtrOffset_f, &dbg_epe_next_hop_info, &nhp_innerEditPtrOffset);
    GetDbgEpeNextHopInfo(A, outerEditPtrOffset_f, &dbg_epe_next_hop_info, &nhp_outerEditPtrOffset);
    GetDbgEpeNextHopInfo(A, qosDomain_f, &dbg_epe_next_hop_info, &nhp_qosDomain);
    GetDbgEpeNextHopInfo(A, valid_f, &dbg_epe_next_hop_info, &nhp_valid);
    /*DbgEpePayLoadInfo*/
    sal_memset(&dbg_epe_pay_load_info, 0, sizeof(dbg_epe_pay_load_info));
    cmd = DRV_IOR(DbgEpePayLoadInfo_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, slice, cmd, &dbg_epe_pay_load_info);
    GetDbgEpePayLoadInfo(A, discard_f, &dbg_epe_pay_load_info, &pld_discard);
    GetDbgEpePayLoadInfo(A, discardType_f, &dbg_epe_pay_load_info, &pld_discardType);
    GetDbgEpePayLoadInfo(A, exceptionEn_f, &dbg_epe_pay_load_info, &pld_exceptionEn);
    GetDbgEpePayLoadInfo(A, exceptionIndex_f, &dbg_epe_pay_load_info, &pld_exceptionIndex);
    GetDbgEpePayLoadInfo(A, shareType_f, &dbg_epe_pay_load_info, &pld_shareType);
    GetDbgEpePayLoadInfo(A, fid_f, &dbg_epe_pay_load_info, &pld_fid);
    GetDbgEpePayLoadInfo(A, payloadOperation_f, &dbg_epe_pay_load_info, &pld_payloadOperation);
    GetDbgEpePayLoadInfo(A, packetLengthAdjustType_f, &dbg_epe_pay_load_info, &pld_packetLengthAdjustType);
    GetDbgEpePayLoadInfo(A, packetLengthAdjust_f, &dbg_epe_pay_load_info, &pld_packetLengthAdjust);
    GetDbgEpePayLoadInfo(A, newTtlValid_f, &dbg_epe_pay_load_info, &pld_newTtlValid);
    GetDbgEpePayLoadInfo(A, newTtl_f, &dbg_epe_pay_load_info, &pld_newTtl);
    GetDbgEpePayLoadInfo(A, newDscpValid_f, &dbg_epe_pay_load_info, &pld_newDscpValid);
    GetDbgEpePayLoadInfo(A, replaceDscp_f, &dbg_epe_pay_load_info, &pld_replaceDscp);
    GetDbgEpePayLoadInfo(A, useOamTtlOrExp_f, &dbg_epe_pay_load_info, &pld_useOamTtlOrExp);
    GetDbgEpePayLoadInfo(A, copyDscp_f, &dbg_epe_pay_load_info, &pld_copyDscp);
    GetDbgEpePayLoadInfo(A, newDscp_f, &dbg_epe_pay_load_info, &pld_newDscp);
    GetDbgEpePayLoadInfo(A, vlanXlateMode_f, &dbg_epe_pay_load_info, &pld_vlanXlateMode);
    GetDbgEpePayLoadInfo(A, svlanTagOperation_f, &dbg_epe_pay_load_info, &pld_svlanTagOperation);
    GetDbgEpePayLoadInfo(A, l2NewSvlanTag_f, &dbg_epe_pay_load_info, &pld_l2NewSvlanTag);
    GetDbgEpePayLoadInfo(A, cvlanTagOperation_f, &dbg_epe_pay_load_info, &pld_cvlanTagOperation);
    GetDbgEpePayLoadInfo(A, l2NewCvlanTag_f, &dbg_epe_pay_load_info, &pld_l2NewCvlanTag);
    GetDbgEpePayLoadInfo(A, stripOffset_f, &dbg_epe_pay_load_info, &pld_stripOffset);
    GetDbgEpePayLoadInfo(A, mirroredPacket_f, &dbg_epe_pay_load_info, &pld_mirroredPacket);
    GetDbgEpePayLoadInfo(A, isL2Ptp_f, &dbg_epe_pay_load_info, &pld_isL2Ptp);
    GetDbgEpePayLoadInfo(A, isL4Ptp_f, &dbg_epe_pay_load_info, &pld_isL4Ptp);
    GetDbgEpePayLoadInfo(A, valid_f, &dbg_epe_pay_load_info, &pld_valid);
    /*DbgEpeEgressEditInfo*/
    sal_memset(&dbg_epe_egress_edit_info, 0, sizeof(dbg_epe_egress_edit_info));
    cmd = DRV_IOR(DbgEpeEgressEditInfo_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, slice, cmd, &dbg_epe_egress_edit_info);
    GetDbgEpeEgressEditInfo(A, discard_f, &dbg_epe_egress_edit_info, &edit_discard);
    GetDbgEpeEgressEditInfo(A, discardType_f, &dbg_epe_egress_edit_info, &edit_discardType);
    GetDbgEpeEgressEditInfo(A, nhpOuterEditLocation_f, &dbg_epe_egress_edit_info, &edit_nhpOuterEditLocation);
    GetDbgEpeEgressEditInfo(A, innerEditExist_f, &dbg_epe_egress_edit_info, &edit_innerEditExist);
    GetDbgEpeEgressEditInfo(A, innerEditPtrType_f, &dbg_epe_egress_edit_info, &edit_innerEditPtrType);
    GetDbgEpeEgressEditInfo(A, nhpInnerEditPtr_f, &dbg_epe_egress_edit_info, &edit_nhpInnerEditPtr);
    GetDbgEpeEgressEditInfo(A, outerEditPipe1Exist_f, &dbg_epe_egress_edit_info, &edit_outerEditPipe1Exist);
    GetDbgEpeEgressEditInfo(A, outerEditPipe1Type_f, &dbg_epe_egress_edit_info, &edit_outerEditPipe1Type);
    GetDbgEpeEgressEditInfo(A, nhpOuterEditPtr_f, &dbg_epe_egress_edit_info, &edit_nhpOuterEditPtr);
    GetDbgEpeEgressEditInfo(A, outerEditPipe2Exist_f, &dbg_epe_egress_edit_info, &edit_outerEditPipe2Exist);
    GetDbgEpeEgressEditInfo(A, outerEditPipe3Exist_f, &dbg_epe_egress_edit_info, &edit_outerEditPipe3Exist);
    GetDbgEpeEgressEditInfo(A, outerEditPipe3Type_f, &dbg_epe_egress_edit_info, &edit_outerEditPipe3Type);
    GetDbgEpeEgressEditInfo(A, l3RewriteType_f, &dbg_epe_egress_edit_info, &edit_l3RewriteType);
    GetDbgEpeEgressEditInfo(A, l3EditMplsPwExist_f, &dbg_epe_egress_edit_info, &edit_l3EditMplsPwExist);
    GetDbgEpeEgressEditInfo(A, l3EditMplsLspExist_f, &dbg_epe_egress_edit_info, &edit_l3EditMplsLspExist);
    GetDbgEpeEgressEditInfo(A, l3EditMplsSpmeExist_f, &dbg_epe_egress_edit_info, &edit_l3EditMplsSpmeExist);
    GetDbgEpeEgressEditInfo(A, innerL2RewriteType_f, &dbg_epe_egress_edit_info, &edit_innerL2RewriteType);
    GetDbgEpeEgressEditInfo(A, l2RewriteType_f, &dbg_epe_egress_edit_info, &edit_l2RewriteType);
    GetDbgEpeEgressEditInfo(A, valid_f, &dbg_epe_egress_edit_info, &edit_valid);
    /*DbgEpeInnerL2EditInfo*/
    sal_memset(&dbg_epe_inner_l2_edit_info, 0, sizeof(dbg_epe_inner_l2_edit_info));
    cmd = DRV_IOR(DbgEpeInnerL2EditInfo_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, slice, cmd, &dbg_epe_inner_l2_edit_info);
    GetDbgEpeInnerL2EditInfo(A, valid_f, &dbg_epe_inner_l2_edit_info, &innerL2_valid);
    GetDbgEpeInnerL2EditInfo(A, isInnerL2EthSwapOp_f, &dbg_epe_inner_l2_edit_info, &innerL2_isInnerL2EthSwapOp);
    GetDbgEpeInnerL2EditInfo(A, isInnerL2EthAddOp_f, &dbg_epe_inner_l2_edit_info, &innerL2_isInnerL2EthAddOp);
    GetDbgEpeInnerL2EditInfo(A, isInnerDsLiteOp_f, &dbg_epe_inner_l2_edit_info, &innerL2_isInnerDsLiteOp);
    /*DbgEpeOuterL2EditInfo*/
    sal_memset(&dbg_epe_outer_l2_edit_info, 0, sizeof(dbg_epe_outer_l2_edit_info));
    cmd = DRV_IOR(DbgEpeOuterL2EditInfo_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, slice, cmd, &dbg_epe_outer_l2_edit_info);
    GetDbgEpeOuterL2EditInfo(A, l2EditDisable_f, &dbg_epe_outer_l2_edit_info, &l2edit_l2EditDisable);
    GetDbgEpeOuterL2EditInfo(A, routeNoL2Edit_f, &dbg_epe_outer_l2_edit_info, &l2edit_routeNoL2Edit);
    GetDbgEpeOuterL2EditInfo(A, destMuxPortType_f, &dbg_epe_outer_l2_edit_info, &l2edit_destMuxPortType);
    GetDbgEpeOuterL2EditInfo(A, newMacSaValid_f, &dbg_epe_outer_l2_edit_info, &l2edit_newMacSaValid);
    GetDbgEpeOuterL2EditInfo(A, newMacDaValid_f, &dbg_epe_outer_l2_edit_info, &l2edit_newMacDaValid);
    GetDbgEpeOuterL2EditInfo(A, portMacSaEn_f, &dbg_epe_outer_l2_edit_info, &l2edit_portMacSaEn);
    GetDbgEpeOuterL2EditInfo(A, loopbackEn_f, &dbg_epe_outer_l2_edit_info, &l2edit_loopbackEn);
    GetDbgEpeOuterL2EditInfo(A, svlanTagOperation_f, &dbg_epe_outer_l2_edit_info, &l2edit_svlanTagOperation);
    GetDbgEpeOuterL2EditInfo(A, cvlanTagOperation_f, &dbg_epe_outer_l2_edit_info, &l2edit_cvlanTagOperation);
    GetDbgEpeOuterL2EditInfo(A, l2RewriteType_f, &dbg_epe_outer_l2_edit_info, &l2edit_l2RewriteType);
    GetDbgEpeOuterL2EditInfo(A, isEnd_f, &dbg_epe_outer_l2_edit_info, &l2edit_isEnd);
    GetDbgEpeOuterL2EditInfo(A, len_f, &dbg_epe_outer_l2_edit_info, &l2edit_len);
    GetDbgEpeOuterL2EditInfo(A, valid_f, &dbg_epe_outer_l2_edit_info, &l2edit_valid);
    /*DbgEpeL3EditInfo*/
    sal_memset(&dbg_epe_l3_edit_info, 0, sizeof(dbg_epe_l3_edit_info));
    cmd = DRV_IOR(DbgEpeL3EditInfo_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, slice, cmd, &dbg_epe_l3_edit_info);
    GetDbgEpeL3EditInfo(A, l3EditDisable_f, &dbg_epe_l3_edit_info, &l3edit_l3EditDisable);
    GetDbgEpeL3EditInfo(A, loopbackEn_f, &dbg_epe_l3_edit_info, &l3edit_loopbackEn);
    GetDbgEpeL3EditInfo(A, l3RewriteType_f, &dbg_epe_l3_edit_info, &l3edit_l3RewriteType);
    GetDbgEpeL3EditInfo(A, label0Valid_f, &dbg_epe_l3_edit_info, &l3edit_label0Valid);
    GetDbgEpeL3EditInfo(A, label1Valid_f, &dbg_epe_l3_edit_info, &l3edit_label1Valid);
    GetDbgEpeL3EditInfo(A, label2Valid_f, &dbg_epe_l3_edit_info, &l3edit_label2Valid);
    GetDbgEpeL3EditInfo(A, isLspLabelEntropy0_f, &dbg_epe_l3_edit_info, &l3edit_isLspLabelEntropy0);
    GetDbgEpeL3EditInfo(A, isLspLabelEntropy1_f, &dbg_epe_l3_edit_info, &l3edit_isLspLabelEntropy1);
    GetDbgEpeL3EditInfo(A, isLspLabelEntropy2_f, &dbg_epe_l3_edit_info, &l3edit_isLspLabelEntropy2);
    GetDbgEpeL3EditInfo(A, isPwLabelEntropy0_f, &dbg_epe_l3_edit_info, &l3edit_isPwLabelEntropy0);
    GetDbgEpeL3EditInfo(A, isPwLabelEntropy1_f, &dbg_epe_l3_edit_info, &l3edit_isPwLabelEntropy1);
    GetDbgEpeL3EditInfo(A, isPwLabelEntropy2_f, &dbg_epe_l3_edit_info, &l3edit_isPwLabelEntropy2);
    GetDbgEpeL3EditInfo(A, cwLabelValid_f, &dbg_epe_l3_edit_info, &l3edit_cwLabelValid);
    GetDbgEpeL3EditInfo(A, flexEditL3Type_f, &dbg_epe_l3_edit_info, &l3edit_flexEditL3Type);
    GetDbgEpeL3EditInfo(A, flexEditL4Type_f, &dbg_epe_l3_edit_info, &l3edit_flexEditL4Type);
    GetDbgEpeL3EditInfo(A, isEnd_f, &dbg_epe_l3_edit_info, &l3edit_isEnd);
    GetDbgEpeL3EditInfo(A, len_f, &dbg_epe_l3_edit_info, &l3edit_len);
    GetDbgEpeL3EditInfo(A, valid_f, &dbg_epe_l3_edit_info, &l3edit_valid);

    if (0 == p_info->detail)
    {
        goto Discard;
    }

    CTC_DKIT_PRINT("Edit Process:\n");
    if (hdr_valid&&hdr_stripOffset)
    {
        CTC_DKIT_PATH_PRINT("%-15s:%d bytes\n", "strip packet", hdr_stripOffset);
    }

    if (pld_valid)
    {

        CTC_DKIT_PATH_PRINT("%-15s:%d\n", "pld Operation", pld_payloadOperation);
        if (pld_svlanTagOperation || pld_cvlanTagOperation)
        {
            CTC_DKIT_PATH_PRINT("%-15s\n", "vlan edit on pld operation--->");
            no_l2_edit = 0;
            if (pld_svlanTagOperation)
            {
                CTC_DKIT_PATH_PRINT("%-15s:%s\n", "svlan edit", vlan_action[pld_svlanTagOperation]);
                if ((VTAGACTIONTYPE_MODIFY == pld_svlanTagOperation) || (VTAGACTIONTYPE_ADD == pld_svlanTagOperation))
                {
                    CTC_DKIT_PATH_PRINT("%-15s:%d\n", "new COS", (pld_l2NewSvlanTag >> 13)&0x7);
                    CTC_DKIT_PATH_PRINT("%-15s:%d\n", "new CFI", (pld_l2NewSvlanTag >> 12)&0x1);
                    CTC_DKIT_PATH_PRINT("%-15s:%d\n", "new vlan id", pld_l2NewSvlanTag&0xFFF);
                }
            }
            if (pld_cvlanTagOperation)
            {
                CTC_DKIT_PATH_PRINT("%-15s:%s\n", "cvlan edit", vlan_action[pld_cvlanTagOperation]);
                if ((VTAGACTIONTYPE_MODIFY == pld_cvlanTagOperation) || (VTAGACTIONTYPE_ADD == pld_cvlanTagOperation))
                {
                    CTC_DKIT_PATH_PRINT("%-15s:%d\n", "new COS", (pld_l2NewCvlanTag >> 13)&0x7);
                    CTC_DKIT_PATH_PRINT("%-15s:%d\n", "new CFI", (pld_l2NewCvlanTag >> 12)&0x1);
                    CTC_DKIT_PATH_PRINT("%-15s:%d\n", "new vlan id", pld_l2NewCvlanTag&0xFFF);
                }
            }
        }
        if (pld_newTtlValid)
        {
            CTC_DKIT_PATH_PRINT("%-15s:%d\n", "new TTL", pld_newTtl);
        }
        if (pld_newDscpValid)
        {
            CTC_DKIT_PATH_PRINT("%-15s:%d\n", "new DSCP", pld_newDscp);
        }
    }

    cmd = DRV_IOR(EpeHdrAdjustCtl_t, EpeHdrAdjustCtl_dsNextHopInternalBase_f);
    DRV_IOCTL(lchip, 0, cmd, &dsNextHopInternalBase);

    if ((adj_nextHopPtr >> 4) != dsNextHopInternalBase)
    {
        cmd = DRV_IOR(DsNextHop8W_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, (adj_nextHopPtr >> 1) << 1, cmd, &ds_next_hop8_w);

        if (!nhp_nhpIs8w)
        {
            if (IS_BIT_SET((adj_nextHopPtr), 0))
            {
                sal_memcpy((uint8*)&ds_next_hop4_w, (uint8*)&ds_next_hop8_w + DS_NEXT_HOP4_W_BYTES, DS_NEXT_HOP4_W_BYTES);
            }
            else
            {
                sal_memcpy((uint8*)&ds_next_hop4_w, (uint8*)&ds_next_hop8_w , DS_NEXT_HOP4_W_BYTES);
            }
        }
    }
    else
    {
        cmd = DRV_IOR(EpeNextHopInternal_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, ((adj_nextHopPtr&0xF) >> 1), cmd, &ds_next_hop8_w);
        if (!nhp_nhpIs8w)
        {
            if (IS_BIT_SET((adj_nextHopPtr&0xF), 0))
            {
                sal_memcpy((uint8*)&ds_next_hop4_w, (uint8*)&ds_next_hop8_w + DS_NEXT_HOP4_W_BYTES, DS_NEXT_HOP4_W_BYTES);
            }
            else
            {
                sal_memcpy((uint8*)&ds_next_hop4_w, (uint8*)&ds_next_hop8_w , DS_NEXT_HOP4_W_BYTES);
            }
        }
    }


    if (nhp_nhpIs8w)
    {
        GetDsNextHop8W(A, shareType_f, &ds_next_hop8_w, &nhpShareType);
    }
    else
    {
        GetDsNextHop4W(A, shareType_f, &ds_next_hop4_w, &nhpShareType);
    }

    if ((PAYLOADOPERATION_ROUTE_COMPACT == pld_payloadOperation)
        ||((PAYLOADOPERATION_NONE == pld_payloadOperation)&&nhp_routeNoL2Edit))
    {
        cmd = DRV_IOR(DbgFwdBufStoreInfo_t, DbgFwdBufStoreInfo_destMap_f);
        DRV_IOCTL(lchip, 0, cmd, &dest_map);
        if (!IS_BIT_SET(dest_map, 17))
        {
            GetDsNextHop4W(A, u1_g3_macDa_f, &ds_next_hop4_w, hw_mac);
            CTC_DKIT_SET_USER_MAC(mac_da, hw_mac);
            if (IS_BIT_SET(mac_da[0], 0))/* mcast mode */
            {

            }
            else
            {
                CTC_DKIT_PATH_PRINT("%-15s\n", "MAC edit--->");
                CTC_DKIT_PATH_PRINT("%-15s:%02X%02X.%02X%02X.%02X%02X\n", "new MAC DA",
                                    mac_da[0], mac_da[1], mac_da[2], mac_da[3], mac_da[4], mac_da[5]);
            }
            no_l2_edit = 0;
        }
    }
    else if ((NHPSHARETYPE_L2EDIT_PLUS_L3EDIT_OP == nhpShareType) || (nhp_nhpIs8w))
    {
        if (nhp_nhpIs8w)
        {
            GetDsNextHop8W(A, outerEditPtrType_f, &ds_next_hop8_w, &nhpOuterEditPtrType);
        }
        else
        {
            GetDsNextHop4W(A, u1_g1_outerEditPtrType_f, &ds_next_hop4_w, &nhpOuterEditPtrType);
        }
        /*inner edit*/
        if (edit_valid && edit_innerEditExist && (!edit_innerEditPtrType))/* inner for l2edit */
        {
            if (innerL2_valid)
            {
                CTC_DKIT_PATH_PRINT("%-15s\n", "Inner MAC edit--->");
                CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "l2edit index", edit_nhpInnerEditPtr);
                if (innerL2_isInnerL2EthSwapOp)
                {
                    uint32 replaceInnerMacDa;
                    CTC_DKIT_PATH_PRINT("%-15s:%s\n", "table name", "DsL2EditInnerSwap");
                    cmd = DRV_IOR(DsL2EditInnerSwap_t, DRV_ENTRY_FLAG);
                    DRV_IOCTL(lchip, edit_nhpInnerEditPtr, cmd, &ds_l2_edit_inner_swap);
                    GetDsL2EditInnerSwap(A, replaceInnerMacDa_f, &ds_l2_edit_inner_swap, &replaceInnerMacDa);
                    if (replaceInnerMacDa)
                    {
                        GetDsL2EditInnerSwap(A, macDa_f, &ds_l2_edit_inner_swap, hw_mac);
                        CTC_DKIT_SET_USER_MAC(mac_da, hw_mac);
                        CTC_DKIT_PATH_PRINT("%-15s:%02X%02X.%02X%02X.%02X%02X\n", "new MAC DA",
                                            mac_da[0], mac_da[1], mac_da[2], mac_da[3], mac_da[4], mac_da[5]);
                    }
                }
                else if (innerL2_isInnerL2EthAddOp)
                {
                    if (L2REWRITETYPE_ETH8W == edit_innerL2RewriteType)
                    {
                        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "table name", "DsL2EditEth6W");
                    }
                    else if (L2REWRITETYPE_ETH4W == edit_innerL2RewriteType)
                    {
                        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "table name", "DsL2EditEth3W");
                    }
                }
                else if (innerL2_isInnerDsLiteOp)
                {
                    CTC_DKIT_PATH_PRINT("%-15s:%s\n", "table name", "DsL2EditDsLite");
                }
            }
            else if (l2edit_valid)
            {
                do_l2_edit = 1;
                l2_edit_ptr = edit_nhpInnerEditPtr;
                l2_individual_memory = 0;
            }
            no_l2_edit = 0;
        }
        else if (edit_valid && edit_innerEditExist && (edit_innerEditPtrType))/* inner for l3edit */
        {
            if (l3edit_valid)
            {
                if (L3REWRITETYPE_MPLS4W == l3edit_l3RewriteType)
                {
                    cmd = DRV_IOR(DsL3EditMpls3W_t, DRV_ENTRY_FLAG);
                    DRV_IOCTL(lchip, edit_nhpInnerEditPtr, cmd, &ds_l3_edit_mpls3_w);
                    GetDsL3EditMpls3W(A, label_f, &ds_l3_edit_mpls3_w, &value);
                    CTC_DKIT_PATH_PRINT("%-15s\n", "MPLS edit--->");
                    CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "l3edit index", edit_nhpInnerEditPtr);
                    CTC_DKIT_PATH_PRINT("%-15s:%s\n", "table name", "DsL3EditMpls3W");
                    CTC_DKIT_PATH_PRINT("%-15s:%d\n", "new label", value);
                    no_l3_edit = 0;
                }
                else if (L3REWRITETYPE_NONE != l3edit_l3RewriteType)
                {
                    CTC_DKIT_PATH_PRINT("%-15s\n", l3_edit[l3edit_l3RewriteType]);
                    CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "l3edit index", edit_nhpInnerEditPtr);
                    CTC_DKIT_PATH_PRINT("%-15s:%s\n", "table name", l3_edit_name[l3edit_l3RewriteType]);
                    no_l3_edit = 0;
                }
            }
        }

        /*outer pipeline1*/
        if (edit_valid && edit_outerEditPipe1Exist)
        {
            if (l3edit_valid && nhpOuterEditPtrType)/* l3edit */
            {
                if (edit_nhpOuterEditLocation)/* individual memory */
                {
                    /* no use */
                }
                else/* share memory */
                {
                    cmd = DRV_IOR(DsL3EditMpls3W_t, DRV_ENTRY_FLAG);
                    DRV_IOCTL(lchip, edit_nhpOuterEditPtr, cmd, &ds_l3_edit_mpls3_w);
                    /* outerEditPtr and outerEditPtrType are in the same position in all l3edit, refer to DsL3Edit3WTemplate  */
                    GetDsL3EditMpls3W(A, outerEditPtr_f, &ds_l3_edit_mpls3_w, &next_edit_ptr);
                    GetDsL3EditMpls3W(A, outerEditPtrType_f, &ds_l3_edit_mpls3_w, &next_edit_type);
                    if (L3REWRITETYPE_MPLS4W == l3edit_l3RewriteType)
                    {
                        GetDsL3EditMpls3W(A, label_f, &ds_l3_edit_mpls3_w, &value);
                        CTC_DKIT_PATH_PRINT("%-15s\n", "MPLS edit--->");
                        CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "l3edit index", edit_nhpOuterEditPtr);
                        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "table name", "DsL3EditMpls3W");
                        CTC_DKIT_PATH_PRINT("%-15s:%d\n", "new label", value);
                        no_l3_edit = 0;
                    }
                    else if (L3REWRITETYPE_NONE != l3edit_l3RewriteType)
                    {
                        CTC_DKIT_PATH_PRINT("%-15s\n", l3_edit[l3edit_l3RewriteType]);
                        CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "l3edit index", edit_nhpOuterEditPtr);
                        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "table name", l3_edit_name[l3edit_l3RewriteType]);
                        no_l3_edit = 0;
                    }
                }
            }
            else if (l2edit_valid)/* l2edit */
            {
                do_l2_edit = 1;
                l2_edit_ptr = edit_nhpOuterEditPtr;
                l2_individual_memory = edit_nhpOuterEditLocation? 1 : 0;
            }
        }
        /*outer pipeline2*/
        if (edit_valid && edit_outerEditPipe2Exist)/* outer pipeline2, individual memory for SPME */
        {
            if (l3edit_valid && next_edit_type)/* l3edit */
            {
                cmd = DRV_IOR(DsL3Edit3W3rd_t, DRV_ENTRY_FLAG);
                DRV_IOCTL(lchip, next_edit_ptr, cmd, &ds_l3_edit_mpls3_w);
                GetDsL3EditMpls3W(A, label_f, &ds_l3_edit_mpls3_w, &value);
                CTC_DKIT_PATH_PRINT("%-15s\n", "MPLS edit--->");
                CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "l3edit index", next_edit_ptr);
                CTC_DKIT_PATH_PRINT("%-15s:%s\n", "table name", "DsL3Edit3W3rd");
                CTC_DKIT_PATH_PRINT("%-15s:%d\n", "new label", value);

                GetDsL3EditMpls3W(A, outerEditPtr_f, &ds_l3_edit_mpls3_w, &next_edit_ptr);
                GetDsL3EditMpls3W(A, outerEditPtrType_f, &ds_l3_edit_mpls3_w, &next_edit_type);
                no_l3_edit = 0;
            }
            else if (l2edit_valid)/* l2edit */
            {
                do_l2_edit = 1;
                if (!edit_outerEditPipe1Exist)
                {
                    l2_edit_ptr = edit_nhpOuterEditPtr;
                }
                else
                {
                    l2_edit_ptr = next_edit_ptr;
                }
                l2_individual_memory = 1;
            }
        }
        /*outer pipeline3*/
        if (l2edit_valid && edit_valid && edit_outerEditPipe3Exist)/* outer pipeline3, individual memory for l2edit */
        {
            do_l2_edit = 1;
            if (!edit_outerEditPipe1Exist)
            {
                l2_edit_ptr = edit_nhpOuterEditPtr;
            }
            else
            {
                l2_edit_ptr = next_edit_ptr;
            }
            l2_individual_memory = 1;
        }


    }
    else if (NHPSHARETYPE_L2EDIT_PLUS_STAG_OP == nhpShareType)
    {
        if (l2edit_valid)/* l2edit */
        {
            do_l2_edit = 1;
            l2_edit_ptr = edit_nhpOuterEditPtr;
            l2_individual_memory = edit_nhpOuterEditLocation? 1 : 0;
        }
    }

    if (do_l2_edit)
    {
        if (l2_individual_memory)/* individual memory */
        {
            cmd = DRV_IOR(DsL2Edit6WOuter_t, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, (l2_edit_ptr >> 1), cmd, &ds_l2_edit6_w_outer);
            if (L2REWRITETYPE_ETH4W == l2edit_l2RewriteType)
            {
                if (l2_edit_ptr % 2 == 0)
                {
                    sal_memcpy((uint8*)&ds_l2_edit_eth3_w, (uint8*)&ds_l2_edit6_w_outer, sizeof(ds_l2_edit_eth3_w));
                }
                else
                {
                    sal_memcpy((uint8*)&ds_l2_edit_eth3_w, (uint8*)&ds_l2_edit6_w_outer + sizeof(ds_l2_edit_eth3_w), sizeof(ds_l2_edit_eth3_w));
                }
            }
            else if (L2REWRITETYPE_MAC_SWAP == l2edit_l2RewriteType)
            {
                sal_memcpy((uint8*)&ds_l2_edit_swap, (uint8*)&ds_l2_edit6_w_outer, sizeof(ds_l2_edit_swap));
            }
        }
        else/* share memory */
        {
            if (L2REWRITETYPE_ETH4W == l2edit_l2RewriteType)
            {
                cmd = DRV_IOR(DsL2EditEth3W_t, DRV_ENTRY_FLAG);
                DRV_IOCTL(lchip, l2_edit_ptr, cmd, &ds_l2_edit_eth3_w);

            }
            else if (L2REWRITETYPE_MAC_SWAP == l2edit_l2RewriteType)
            {
                cmd = DRV_IOR(DsL2EditSwap_t, DRV_ENTRY_FLAG);
                DRV_IOCTL(lchip, l2_edit_ptr, cmd, &ds_l2_edit_swap);
            }
        }

        if (L2REWRITETYPE_ETH4W == l2edit_l2RewriteType)
        {
            GetDsL2EditEth3W(A, macDa_f, &ds_l2_edit_eth3_w, hw_mac);
            CTC_DKIT_SET_USER_MAC(mac_da, hw_mac);
            CTC_DKIT_PATH_PRINT("%-15s\n", "MAC edit--->");
            CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "l2edit index", l2_individual_memory?(l2_edit_ptr/2):l2_edit_ptr);
            CTC_DKIT_PATH_PRINT("%-15s:%s\n", "table name", l2_individual_memory? "DsL2Edit6WOuter" : "DsL2EditEth3W");
            CTC_DKIT_PATH_PRINT("%-15s:%02X%02X.%02X%02X.%02X%02X\n", "new MAC DA",
                                mac_da[0], mac_da[1], mac_da[2], mac_da[3], mac_da[4], mac_da[5]);
            no_l2_edit = 0;
        }
        else if (L2REWRITETYPE_MAC_SWAP == l2edit_l2RewriteType)
        {
            uint32 deriveMcastMac;
            uint32 _type = 0;
            uint32 outputVlanIdValid = 0;
            uint32 overwriteEtherType = 0;
            GetDsL2EditSwap(A, deriveMcastMac_f, &ds_l2_edit_swap, &deriveMcastMac);
            GetDsL2EditSwap(A, _type_f, &ds_l2_edit_swap, &_type);
            GetDsL2EditSwap(A, outputVlanIdValid_f, &ds_l2_edit_swap, &outputVlanIdValid);
            GetDsL2EditSwap(A, overwriteEtherType_f, &ds_l2_edit_swap, &overwriteEtherType);

            CTC_DKIT_PATH_PRINT("%-15s\n", "MAC swap--->");
            CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "l2edit index", l2_individual_memory?(l2_edit_ptr/2):l2_edit_ptr);
            CTC_DKIT_PATH_PRINT("%-15s:%s\n", "table name", l2_individual_memory? "DsL2Edit6WOuter" : "DsL2EditSwap");
            if (deriveMcastMac)
            {
                if (_type)
                {
                    GetDsL2EditSwap(A, macDa_f, &ds_l2_edit_eth3_w, hw_mac);
                    CTC_DKIT_SET_USER_MAC(mac_da, hw_mac);
                    CTC_DKIT_PATH_PRINT("%-15s:%02X%02X.%02X%02X.%02X%02X\n", "new MAC DA",
                                        mac_da[0], mac_da[1], mac_da[2], mac_da[3], mac_da[4], mac_da[5]);
                }
                else
                {
                    CTC_DKIT_PATH_PRINT("%-15s:%s\n", "new MAC DA", "MAC SA of the packet");
                }
            }
            if (outputVlanIdValid)
            {
                if (overwriteEtherType)
                {
                    GetDsL2EditSwap(A, macSa_f, &ds_l2_edit_eth3_w, hw_mac);
                    CTC_DKIT_SET_USER_MAC(mac_da, hw_mac);
                    CTC_DKIT_PATH_PRINT("%-15s:%02X%02X.%02X%02X.%02X%02X\n", "new MAC SA",
                                        mac_da[0], mac_da[1], mac_da[2], mac_da[3], mac_da[4], mac_da[5]);
                }
                else
                {
                    CTC_DKIT_PATH_PRINT("%-15s:%s\n", "new MAC SA", "MAC DA of the packet");
                }
            }
            no_l2_edit = 0;
        }
        else if (L2REWRITETYPE_NONE != l2edit_l2RewriteType)
        {
            CTC_DKIT_PATH_PRINT("%-15s\n", "L2edit--->");
            CTC_DKIT_PATH_PRINT("%-15s:%d\n", "L2edit type", l2edit_l2RewriteType);
            CTC_DKIT_PATH_PRINT("%-15s:0x%x\n", "l2edit index", l2_edit_ptr);
            no_l2_edit = 0;
        }
    }


    if (no_l3_edit)
    {
        CTC_DKIT_PATH_PRINT("%-15s\n", "not do L3 edit!");
    }
    if (no_l2_edit)
    {
        CTC_DKIT_PATH_PRINT("%-15s\n", "not do L2 edit!");
    }

Discard:
    if (nhp_discard)
    {
        discard = 1;
        discard_type = nhp_discardType;
    }
    else if (pld_discard)
    {
        discard = 1;
        discard_type = pld_discardType;
    }
    else if (edit_discard)
    {
        discard = 1;
        discard_type = edit_discardType;
    }
    _ctc_goldengate_dkit_captured_path_discard_process(p_info, discard, discard_type, CTC_DKIT_EPE);

    return CLI_SUCCESS;
}

STATIC int32
_ctc_goldengate_dkit_captured_path_epe(ctc_dkit_captured_path_info_t* p_info)
{
    CTC_DKIT_PRINT("\nEGRESS PROCESS   \n-----------------------------------\n");

    /*1. Parser*/
    if (!p_info->discard)
    {
        _ctc_goldengate_dkit_captured_path_epe_parser(p_info);
    }
    /*2. SCL*/
    if (!p_info->discard)
    {
        _ctc_goldengate_dkit_captured_path_epe_scl(p_info);
    }
    /*3. Interface*/
    if (!p_info->discard)
    {
        _ctc_goldengate_dkit_captured_path_epe_interface(p_info);
    }
    /*4. ACL*/
    if (!p_info->discard)
    {
        _ctc_goldengate_dkit_captured_path_epe_acl(p_info);
    }
    /*5. OAM*/
    if (!p_info->discard)
    {
        _ctc_goldengate_dkit_captured_path_epe_oam(p_info);
    }
    /*6. Edit*/
    if (!p_info->discard)
    {
        _ctc_goldengate_dkit_captured_path_epe_edit(p_info);
    }

    if (p_info->discard && (0 == p_info->detail))
    {
        CTC_DKIT_PRINT("Packet Drop at EPE:\n");
        CTC_DKIT_PATH_PRINT("%-15s:%s\n", "reason", ctc_goldengate_dkit_get_reason_desc(p_info->discard_type + CTC_DKIT_EPE_HDR_ADJ_DESTID_DIS));
    }

    return CLI_SUCCESS;
}

int32
ctc_goldengate_dkit_captured_path_process(void* p_para)
{
    ctc_dkit_path_para_t* p_path_para = (ctc_dkit_path_para_t*)p_para;
    ctc_dkit_captured_path_info_t path_info ;
    ctc_dkit_captured_t ctc_dkit_captured;

    DKITS_PTR_VALID_CHECK(p_para);
    if (p_path_para->captured_session)
    {
        CTC_DKIT_PRINT("invalid session id!\n");
        return CLI_ERROR;
    }

    sal_memset(&path_info, 0, sizeof(ctc_dkit_captured_path_info_t));
    sal_memset(&ctc_dkit_captured, 0, sizeof(ctc_dkit_captured_t));

    path_info.lchip = p_path_para->lchip;
    _ctc_goldengate_dkit_captured_is_hit( &path_info);

    path_info.detail = p_path_para->detail;

    if (path_info.is_ipe_captured | path_info.is_bsr_captured | path_info.is_epe_captured)
    {
        if ((path_info.is_ipe_captured) && (!path_info.discard))
        {
            _ctc_goldengate_dkit_captured_path_ipe(&path_info);
        }
        if ((path_info.is_bsr_captured) && (!path_info.discard) && (path_info.detail))
        {
            _ctc_goldengate_dkit_captured_path_bsr(&path_info);
        }
        if ((path_info.is_epe_captured) && (!path_info.discard))
        {
            _ctc_goldengate_dkit_captured_path_epe(&path_info);
        }

        ctc_dkit_captured.captured_session = p_path_para->captured_session;
        ctc_dkit_captured.flag = p_path_para->flag;
        ctc_goldengate_dkit_captured_clear((void*)(&ctc_dkit_captured));
    }
    else
    {
        CTC_DKIT_PRINT("Not Captured Flow!\n");
    }

    return CLI_SUCCESS;
}


int32
ctc_goldengate_dkit_captured_clear(void* p_para)
{
    ctc_dkit_captured_t* p_captured_para = (ctc_dkit_captured_t*)p_para;
    uint32 flag = 0;
    uint32 cmd = 0;
    uint32 value = 0;
    uint8 lchip = 0;
    DbgFwdBufRetrvInfo_m dbg_fwd_buf_retrv_info;
    DbgFwdBufStoreInfo_m dbg_fwd_buf_store_info;
    DbgEgrXcOamHash0EngineFromEpeNhpInfo_m dbg_egr_xc_oam_hash0_engine_from_epe_nhp_info;
    DbgEgrXcOamHash1EngineFromEpeNhpInfo_m dbg_egr_xc_oam_hash1_engine_from_epe_nhp_info;
    DbgEgrXcOamHashEngineFromEpeOam0Info_m dbg_egr_xc_oam_hash_engine_from_epe_oam0_info;
    DbgEgrXcOamHashEngineFromEpeOam1Info_m dbg_egr_xc_oam_hash_engine_from_epe_oam1_info;
    DbgEgrXcOamHashEngineFromEpeOam2Info_m dbg_egr_xc_oam_hash_engine_from_epe_oam2_info;
    DbgEgrXcOamHashEngineFromIpeOam0Info_m dbg_egr_xc_oam_hash_engine_from_ipe_oam0_info;
    DbgEgrXcOamHashEngineFromIpeOam1Info_m dbg_egr_xc_oam_hash_engine_from_ipe_oam1_info;
    DbgEgrXcOamHashEngineFromIpeOam2Info_m dbg_egr_xc_oam_hash_engine_from_ipe_oam2_info;
    DbgEpeAclInfo_m dbg_epe_acl_info;
    DbgEpeClassificationInfo_m dbg_epe_classification_info;
    DbgEpeOamInfo_m dbg_epe_oam_info;
    DbgEpeHdrAdjInfo_m dbg_epe_hdr_adj_info;
    DbgEpeHdrEditInfo_m dbg_epe_hdr_edit_info;
    DbgEpeEgressEditInfo_m dbg_epe_egress_edit_info;
    DbgEpeInnerL2EditInfo_m dbg_epe_inner_l2_edit_info;
    DbgEpeL3EditInfo_m dbg_epe_l3_edit_info;
    DbgEpeOuterL2EditInfo_m dbg_epe_outer_l2_edit_info;
    DbgEpePayLoadInfo_m dbg_epe_pay_load_info;
    DbgEpeNextHopInfo_m dbg_epe_next_hop_info;
    DbgFibLkpEngineMacDaHashInfo_m dbg_fib_lkp_engine_mac_da_hash_info;
    DbgFibLkpEngineMacSaHashInfo_m dbg_fib_lkp_engine_mac_sa_hash_info;
    DbgFibLkpEnginePbrNatTcamInfo_m dbg_fib_lkp_engine_pbr_nat_tcam_info;
    DbgFibLkpEnginel3DaHashInfo_m dbg_fib_lkp_enginel3_da_hash_info;
    DbgFibLkpEnginel3DaLpmInfo_m dbg_fib_lkp_enginel3_da_lpm_info;
    DbgFibLkpEnginel3SaHashInfo_m dbg_fib_lkp_enginel3_sa_hash_info;
    DbgFibLkpEnginel3SaLpmInfo_m dbg_fib_lkp_enginel3_sa_lpm_info;
    DbgIpfixAccEgrInfo_m dbg_ipfix_acc_egr_info;
    DbgIpfixAccIngInfo_m dbg_ipfix_acc_ing_info;
    DbgFlowHashEngineInfo_m dbg_flow_hash_engine_info;
    DbgFlowTcamEngineEpeAclInfo_m dbg_flow_tcam_engine_epe_acl_info;
    DbgFlowTcamEngineIpeAclInfo_m dbg_flow_tcam_engine_ipe_acl_info;
    DbgFlowTcamEngineUserIdInfo_m dbg_flow_tcam_engine_user_id_info;
    DbgIpeForwardingInfo_m dbg_ipe_forwarding_info;
    DbgIpeIntfMapperInfo_m dbg_ipe_intf_mapper_info;
    DbgIpeMplsDecapInfo_m dbg_ipe_mpls_decap_info;
    DbgIpeUserIdInfo_m dbg_ipe_user_id_info;
    DbgIpeAclLkpInfo_m dbg_ipe_acl_lkp_info;
    DbgIpeAclProcInfo_m dbg_ipe_acl_proc_info;
    DbgIpeLkpMgrInfo_m dbg_ipe_lkp_mgr_info;
    DbgIpeClsInfo_m dbg_ipe_cls_info;
    DbgIpeEcmpProcInfo_m dbg_ipe_ecmp_proc_info;
    DbgIpeFcoeInfo_m dbg_ipe_fcoe_info;
    DbgIpeIpRoutingInfo_m dbg_ipe_ip_routing_info;
    DbgIpeMacBridgingInfo_m dbg_ipe_mac_bridging_info;
    DbgIpeMacLearningInfo_m dbg_ipe_mac_learning_info;
    DbgIpeOamInfo_m dbg_ipe_oam_info;
    DbgIpeTrillInfo_m dbg_ipe_trill_info;
    DbgLearningEngineInfo_m dbg_learning_engine_info;
    DbgFwdMetFifoInfo_m dbg_fwd_met_fifo_info;
    DbgOamHdrEdit_m dbg_oam_hdr_edit;
    DbgOamHdrAdj_m dbg_oam_hdr_adj;
    DbgOamDefectMsg_m dbg_oam_defect_msg;
    DbgOamDefectProc_m dbg_oam_defect_proc;
    DbgOamRxProc_m dbg_oam_rx_proc;
    DbgPolicerEngineEgrInfo_m dbg_policer_engine_egr_info;
    DbgPolicerEngineIngInfo_m dbg_policer_engine_ing_info;
    DbgFwdQueMgrInfo_m dbg_fwd_que_mgr_info;
    DbgQMgrEnqInfo_m dbg_q_mgr_enq_info;
    DbgDlbEngineInfo_m dbg_dlb_engine_info;
    DbgEfdEngineInfo_m dbg_efd_engine_info;
    DbgStormCtlEngineInfo_m dbg_storm_ctl_engine_info;
    DbgUserIdHashEngineForMpls0Info_m dbg_user_id_hash_engine_for_mpls0_info;
    DbgUserIdHashEngineForMpls1Info_m dbg_user_id_hash_engine_for_mpls1_info;
    DbgUserIdHashEngineForMpls2Info_m dbg_user_id_hash_engine_for_mpls2_info;
    DbgUserIdHashEngineForUserId0Info_m dbg_user_id_hash_engine_for_user_id0_info;
    DbgUserIdHashEngineForUserId1Info_m dbg_user_id_hash_engine_for_user_id1_info;


    DKITS_PTR_VALID_CHECK(p_para);
    lchip = p_captured_para->lchip;
    CTC_DKIT_LCHIP_CHECK(lchip);
    if (p_captured_para->captured_session)
    {
        CTC_DKIT_PRINT("invalid session id!\n");
        return CLI_ERROR;
    }
    flag = p_captured_para->flag;

    if ((CTC_DKIT_CAPTURED_CLEAR_RESULT == flag) || (CTC_DKIT_CAPTURED_CLEAR_ALL == flag))
    {
        value = 1;
        cmd = DRV_IOW(DebugSessionStatusCtl_t, DebugSessionStatusCtl_isFree_f);
        DRV_IOCTL(lchip, 0, cmd, &value);

        value = 1;
        cmd = DRV_IOW(EpeHdrAdjustChanCtl_t, EpeHdrAdjustChanCtl_debugIsFree_f);
        DRV_IOCTL(lchip, 0, cmd, &value);
        DRV_IOCTL(lchip, 1, cmd, &value);

        sal_memset(&dbg_fwd_buf_retrv_info, 0, sizeof(dbg_fwd_buf_retrv_info));
        sal_memset(&dbg_fwd_buf_store_info, 0, sizeof(dbg_fwd_buf_store_info));
        sal_memset(&dbg_egr_xc_oam_hash0_engine_from_epe_nhp_info, 0, sizeof(dbg_egr_xc_oam_hash0_engine_from_epe_nhp_info));
        sal_memset(&dbg_egr_xc_oam_hash1_engine_from_epe_nhp_info, 0, sizeof(dbg_egr_xc_oam_hash1_engine_from_epe_nhp_info));
        sal_memset(&dbg_egr_xc_oam_hash_engine_from_epe_oam0_info, 0, sizeof(dbg_egr_xc_oam_hash_engine_from_epe_oam0_info));
        sal_memset(&dbg_egr_xc_oam_hash_engine_from_epe_oam1_info, 0, sizeof(dbg_egr_xc_oam_hash_engine_from_epe_oam1_info));
        sal_memset(&dbg_egr_xc_oam_hash_engine_from_epe_oam2_info, 0, sizeof(dbg_egr_xc_oam_hash_engine_from_epe_oam2_info));
        sal_memset(&dbg_egr_xc_oam_hash_engine_from_ipe_oam0_info, 0, sizeof(dbg_egr_xc_oam_hash_engine_from_ipe_oam0_info));
        sal_memset(&dbg_egr_xc_oam_hash_engine_from_ipe_oam1_info, 0, sizeof(dbg_egr_xc_oam_hash_engine_from_ipe_oam1_info));
        sal_memset(&dbg_egr_xc_oam_hash_engine_from_ipe_oam2_info, 0, sizeof(dbg_egr_xc_oam_hash_engine_from_ipe_oam2_info));
        sal_memset(&dbg_epe_acl_info, 0, sizeof(dbg_epe_acl_info));
        sal_memset(&dbg_epe_classification_info, 0, sizeof(dbg_epe_classification_info));
        sal_memset(&dbg_epe_oam_info, 0, sizeof(dbg_epe_oam_info));
        sal_memset(&dbg_epe_hdr_adj_info, 0, sizeof(dbg_epe_hdr_adj_info));
        sal_memset(&dbg_epe_hdr_edit_info, 0, sizeof(dbg_epe_hdr_edit_info));
        sal_memset(&dbg_epe_egress_edit_info, 0, sizeof(dbg_epe_egress_edit_info));
        sal_memset(&dbg_epe_inner_l2_edit_info, 0, sizeof(dbg_epe_inner_l2_edit_info));
        sal_memset(&dbg_epe_l3_edit_info, 0, sizeof(dbg_epe_l3_edit_info));
        sal_memset(&dbg_epe_outer_l2_edit_info, 0, sizeof(dbg_epe_outer_l2_edit_info));
        sal_memset(&dbg_epe_pay_load_info, 0, sizeof(dbg_epe_pay_load_info));
        sal_memset(&dbg_epe_next_hop_info, 0, sizeof(dbg_epe_next_hop_info));
        sal_memset(&dbg_fib_lkp_engine_mac_da_hash_info, 0, sizeof(dbg_fib_lkp_engine_mac_da_hash_info));
        sal_memset(&dbg_fib_lkp_engine_mac_sa_hash_info, 0, sizeof(dbg_fib_lkp_engine_mac_sa_hash_info));
        sal_memset(&dbg_fib_lkp_engine_pbr_nat_tcam_info, 0, sizeof(dbg_fib_lkp_engine_pbr_nat_tcam_info));
        sal_memset(&dbg_fib_lkp_enginel3_da_hash_info, 0, sizeof(dbg_fib_lkp_enginel3_da_hash_info));
        sal_memset(&dbg_fib_lkp_enginel3_da_lpm_info, 0, sizeof(dbg_fib_lkp_enginel3_da_lpm_info));
        sal_memset(&dbg_fib_lkp_enginel3_sa_hash_info, 0, sizeof(dbg_fib_lkp_enginel3_sa_hash_info));
        sal_memset(&dbg_fib_lkp_enginel3_sa_lpm_info, 0, sizeof(dbg_fib_lkp_enginel3_sa_lpm_info));
        sal_memset(&dbg_ipfix_acc_egr_info, 0, sizeof(dbg_ipfix_acc_egr_info));
        sal_memset(&dbg_ipfix_acc_ing_info, 0, sizeof(dbg_ipfix_acc_ing_info));
        sal_memset(&dbg_flow_hash_engine_info, 0, sizeof(dbg_flow_hash_engine_info));
        sal_memset(&dbg_flow_tcam_engine_epe_acl_info, 0, sizeof(dbg_flow_tcam_engine_epe_acl_info));
        sal_memset(&dbg_flow_tcam_engine_ipe_acl_info, 0, sizeof(dbg_flow_tcam_engine_ipe_acl_info));
        sal_memset(&dbg_flow_tcam_engine_user_id_info, 0, sizeof(dbg_flow_tcam_engine_user_id_info));
        sal_memset(&dbg_ipe_forwarding_info, 0, sizeof(dbg_ipe_forwarding_info));
        sal_memset(&dbg_ipe_intf_mapper_info, 0, sizeof(dbg_ipe_intf_mapper_info));
        sal_memset(&dbg_ipe_mpls_decap_info, 0, sizeof(dbg_ipe_mpls_decap_info));
        sal_memset(&dbg_ipe_user_id_info, 0, sizeof(dbg_ipe_user_id_info));
        sal_memset(&dbg_ipe_acl_lkp_info, 0, sizeof(dbg_ipe_acl_lkp_info));
        sal_memset(&dbg_ipe_acl_proc_info, 0, sizeof(dbg_ipe_acl_proc_info));
        sal_memset(&dbg_ipe_lkp_mgr_info, 0, sizeof(dbg_ipe_lkp_mgr_info));
        sal_memset(&dbg_ipe_cls_info, 0, sizeof(dbg_ipe_cls_info));
        sal_memset(&dbg_ipe_ecmp_proc_info, 0, sizeof(dbg_ipe_ecmp_proc_info));
        sal_memset(&dbg_ipe_fcoe_info, 0, sizeof(dbg_ipe_fcoe_info));
        sal_memset(&dbg_ipe_ip_routing_info, 0, sizeof(dbg_ipe_ip_routing_info));
        sal_memset(&dbg_ipe_mac_bridging_info, 0, sizeof(dbg_ipe_mac_bridging_info));
        sal_memset(&dbg_ipe_mac_learning_info, 0, sizeof(dbg_ipe_mac_learning_info));
        sal_memset(&dbg_ipe_oam_info, 0, sizeof(dbg_ipe_oam_info));
        sal_memset(&dbg_ipe_trill_info, 0, sizeof(dbg_ipe_trill_info));
        sal_memset(&dbg_learning_engine_info, 0, sizeof(dbg_learning_engine_info));
        sal_memset(&dbg_fwd_met_fifo_info, 0, sizeof(dbg_fwd_met_fifo_info));
        sal_memset(&dbg_oam_hdr_edit, 0, sizeof(dbg_oam_hdr_edit));
        sal_memset(&dbg_oam_hdr_adj, 0, sizeof(dbg_oam_hdr_adj));
        sal_memset(&dbg_oam_defect_msg, 0, sizeof(dbg_oam_defect_msg));
        sal_memset(&dbg_oam_defect_proc, 0, sizeof(dbg_oam_defect_proc));
        sal_memset(&dbg_oam_rx_proc, 0, sizeof(dbg_oam_rx_proc));
        sal_memset(&dbg_policer_engine_egr_info, 0, sizeof(dbg_policer_engine_egr_info));
        sal_memset(&dbg_policer_engine_ing_info, 0, sizeof(dbg_policer_engine_ing_info));
        sal_memset(&dbg_fwd_que_mgr_info, 0, sizeof(dbg_fwd_que_mgr_info));
        sal_memset(&dbg_q_mgr_enq_info, 0, sizeof(dbg_q_mgr_enq_info));
        sal_memset(&dbg_dlb_engine_info, 0, sizeof(dbg_dlb_engine_info));
        sal_memset(&dbg_efd_engine_info, 0, sizeof(dbg_efd_engine_info));
        sal_memset(&dbg_storm_ctl_engine_info, 0, sizeof(dbg_storm_ctl_engine_info));
        sal_memset(&dbg_user_id_hash_engine_for_mpls0_info, 0, sizeof(dbg_user_id_hash_engine_for_mpls0_info));
        sal_memset(&dbg_user_id_hash_engine_for_mpls1_info, 0, sizeof(dbg_user_id_hash_engine_for_mpls1_info));
        sal_memset(&dbg_user_id_hash_engine_for_mpls2_info, 0, sizeof(dbg_user_id_hash_engine_for_mpls2_info));
        sal_memset(&dbg_user_id_hash_engine_for_user_id0_info, 0, sizeof(dbg_user_id_hash_engine_for_user_id0_info));
        sal_memset(&dbg_user_id_hash_engine_for_user_id1_info, 0, sizeof(dbg_user_id_hash_engine_for_user_id1_info));

        cmd = DRV_IOW(DbgDlbEngineInfo_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_dlb_engine_info);
        /* DRV_IOCTL(lchip, 1, cmd, &dbg_dlb_engine_info);//share */
        cmd = DRV_IOW(DbgEfdEngineInfo_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_efd_engine_info);
        /* DRV_IOCTL(lchip, 1, cmd, &dbg_efd_engine_info);//share */
        cmd = DRV_IOW(DbgEgrXcOamHash0EngineFromEpeNhpInfo_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_egr_xc_oam_hash0_engine_from_epe_nhp_info);
        DRV_IOCTL(lchip, 1, cmd, &dbg_egr_xc_oam_hash0_engine_from_epe_nhp_info);/* cascade */
        cmd = DRV_IOW(DbgEgrXcOamHash1EngineFromEpeNhpInfo_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_egr_xc_oam_hash1_engine_from_epe_nhp_info);
        DRV_IOCTL(lchip, 1, cmd, &dbg_egr_xc_oam_hash1_engine_from_epe_nhp_info);/* cascade */
        cmd = DRV_IOW(DbgEgrXcOamHashEngineFromEpeOam0Info_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_egr_xc_oam_hash_engine_from_epe_oam0_info);
        DRV_IOCTL(lchip, 1, cmd, &dbg_egr_xc_oam_hash_engine_from_epe_oam0_info);/* cascade */
        cmd = DRV_IOW(DbgEgrXcOamHashEngineFromEpeOam1Info_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_egr_xc_oam_hash_engine_from_epe_oam1_info);
        DRV_IOCTL(lchip, 1, cmd, &dbg_egr_xc_oam_hash_engine_from_epe_oam1_info);/* cascade */
        cmd = DRV_IOW(DbgEgrXcOamHashEngineFromEpeOam2Info_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_egr_xc_oam_hash_engine_from_epe_oam2_info);
        DRV_IOCTL(lchip, 1, cmd, &dbg_egr_xc_oam_hash_engine_from_epe_oam2_info);/* cascade */
        cmd = DRV_IOW(DbgEgrXcOamHashEngineFromIpeOam0Info_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_egr_xc_oam_hash_engine_from_ipe_oam0_info);
        DRV_IOCTL(lchip, 1, cmd, &dbg_egr_xc_oam_hash_engine_from_ipe_oam0_info);/* cascade */
        cmd = DRV_IOW(DbgEgrXcOamHashEngineFromIpeOam1Info_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_egr_xc_oam_hash_engine_from_ipe_oam1_info);
        DRV_IOCTL(lchip, 1, cmd, &dbg_egr_xc_oam_hash_engine_from_ipe_oam1_info);/* cascade */
        cmd = DRV_IOW(DbgEgrXcOamHashEngineFromIpeOam2Info_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_egr_xc_oam_hash_engine_from_ipe_oam2_info);
        DRV_IOCTL(lchip, 1, cmd, &dbg_egr_xc_oam_hash_engine_from_ipe_oam2_info);/* cascade */
        cmd = DRV_IOW(DbgEpeAclInfo_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_epe_acl_info);
        DRV_IOCTL(lchip, 1, cmd, &dbg_epe_acl_info);/* cascade */
        cmd = DRV_IOW(DbgEpeClassificationInfo_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_epe_classification_info);
        DRV_IOCTL(lchip, 1, cmd, &dbg_epe_classification_info);/* Cascade */
        cmd = DRV_IOW(DbgEpeEgressEditInfo_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_epe_egress_edit_info);
        DRV_IOCTL(lchip, 1, cmd, &dbg_epe_egress_edit_info);/* cascade */
        cmd = DRV_IOW(DbgEpeHdrAdjInfo_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_epe_hdr_adj_info);
        DRV_IOCTL(lchip, 1, cmd, &dbg_epe_hdr_adj_info);/* cascade */
        cmd = DRV_IOW(DbgEpeHdrEditInfo_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_epe_hdr_edit_info);
        DRV_IOCTL(lchip, 1, cmd, &dbg_epe_hdr_edit_info);/* cascade */
        cmd = DRV_IOW(DbgEpeInnerL2EditInfo_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_epe_inner_l2_edit_info);
        DRV_IOCTL(lchip, 1, cmd, &dbg_epe_inner_l2_edit_info);/* cascade */
        cmd = DRV_IOW(DbgEpeL3EditInfo_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_epe_l3_edit_info);
        DRV_IOCTL(lchip, 1, cmd, &dbg_epe_l3_edit_info);/* cascade */
        cmd = DRV_IOW(DbgEpeNextHopInfo_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_epe_next_hop_info);
        DRV_IOCTL(lchip, 1, cmd, &dbg_epe_next_hop_info);/* cascade */
        cmd = DRV_IOW(DbgEpeOamInfo_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_epe_oam_info);
        DRV_IOCTL(lchip, 1, cmd, &dbg_epe_oam_info);/* cascade */
        cmd = DRV_IOW(DbgEpeOuterL2EditInfo_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_epe_outer_l2_edit_info);
        DRV_IOCTL(lchip, 1, cmd, &dbg_epe_outer_l2_edit_info);/* cascade */
        cmd = DRV_IOW(DbgEpePayLoadInfo_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_epe_pay_load_info);
        DRV_IOCTL(lchip, 1, cmd, &dbg_epe_pay_load_info);/* cascade */
        cmd = DRV_IOW(DbgFibLkpEngineMacDaHashInfo_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_fib_lkp_engine_mac_da_hash_info);
        DRV_IOCTL(lchip, 1, cmd, &dbg_fib_lkp_engine_mac_da_hash_info);/* cascade */
        cmd = DRV_IOW(DbgFibLkpEngineMacSaHashInfo_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_fib_lkp_engine_mac_sa_hash_info);
        DRV_IOCTL(lchip, 1, cmd, &dbg_fib_lkp_engine_mac_sa_hash_info);/* cascade */
        cmd = DRV_IOW(DbgFibLkpEnginePbrNatTcamInfo_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_fib_lkp_engine_pbr_nat_tcam_info);
        DRV_IOCTL(lchip, 1, cmd, &dbg_fib_lkp_engine_pbr_nat_tcam_info);/* cascade */
        cmd = DRV_IOW(DbgFibLkpEnginel3DaHashInfo_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_fib_lkp_enginel3_da_hash_info);
        DRV_IOCTL(lchip, 1, cmd, &dbg_fib_lkp_enginel3_da_hash_info);/* cascade */
        cmd = DRV_IOW(DbgFibLkpEnginel3DaLpmInfo_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_fib_lkp_enginel3_da_lpm_info);
        DRV_IOCTL(lchip, 1, cmd, &dbg_fib_lkp_enginel3_da_lpm_info);/* cascade */
        cmd = DRV_IOW(DbgFibLkpEnginel3SaHashInfo_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_fib_lkp_enginel3_sa_hash_info);
        DRV_IOCTL(lchip, 1, cmd, &dbg_fib_lkp_enginel3_sa_hash_info);/* cascade */
        cmd = DRV_IOW(DbgFibLkpEnginel3SaLpmInfo_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_fib_lkp_enginel3_sa_lpm_info);
        DRV_IOCTL(lchip, 1, cmd, &dbg_fib_lkp_enginel3_sa_lpm_info);/* cascade */
        cmd = DRV_IOW(DbgFlowHashEngineInfo_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_flow_hash_engine_info);
        DRV_IOCTL(lchip, 1, cmd, &dbg_flow_hash_engine_info);/* cascade */
        cmd = DRV_IOW(DbgFlowTcamEngineEpeAclInfo_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_flow_tcam_engine_epe_acl_info);
        /* DRV_IOCTL(lchip, 1, cmd, &dbg_flow_tcam_engine_epe_acl_info);//share */
        cmd = DRV_IOW(DbgFlowTcamEngineIpeAclInfo_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_flow_tcam_engine_ipe_acl_info);
        /* DRV_IOCTL(lchip, 1, cmd, &dbg_flow_tcam_engine_ipe_acl_info);//share */
        cmd = DRV_IOW(DbgFlowTcamEngineUserIdInfo_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_flow_tcam_engine_user_id_info);
        /* DRV_IOCTL(lchip, 1, cmd, &dbg_flow_tcam_engine_user_id_info);//share */
        cmd = DRV_IOW(DbgFwdBufRetrvInfo_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_fwd_buf_retrv_info);
        DRV_IOCTL(lchip, 1, cmd, &dbg_fwd_buf_retrv_info);/* cascade */
        cmd = DRV_IOW(DbgFwdBufStoreInfo_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_fwd_buf_store_info);
        /* DRV_IOCTL(lchip, 1, cmd, &dbg_fwd_buf_store_info);//share */
        cmd = DRV_IOW(DbgFwdMetFifoInfo_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_fwd_met_fifo_info);
        /* DRV_IOCTL(lchip, 1, cmd, &dbg_fwd_met_fifo_info);//share */
        cmd = DRV_IOW(DbgFwdQueMgrInfo_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_fwd_que_mgr_info);
        /* DRV_IOCTL(lchip, 1, cmd, &dbg_fwd_que_mgr_info);//share */
        cmd = DRV_IOW(DbgIpeAclLkpInfo_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_ipe_acl_lkp_info);
        DRV_IOCTL(lchip, 1, cmd, &dbg_ipe_acl_lkp_info);/* cascade */
        cmd = DRV_IOW(DbgIpeAclProcInfo_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_ipe_acl_proc_info);
        DRV_IOCTL(lchip, 1, cmd, &dbg_ipe_acl_proc_info);/* cascade */
        cmd = DRV_IOW(DbgIpeClsInfo_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_ipe_cls_info);
        DRV_IOCTL(lchip, 1, cmd, &dbg_ipe_cls_info);/* Cascade */
        cmd = DRV_IOW(DbgIpeEcmpProcInfo_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_ipe_ecmp_proc_info);
        DRV_IOCTL(lchip, 1, cmd, &dbg_ipe_ecmp_proc_info);/* cascade */
        cmd = DRV_IOW(DbgIpeFcoeInfo_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_ipe_fcoe_info);
        DRV_IOCTL(lchip, 1, cmd, &dbg_ipe_fcoe_info);/* cascade */
        cmd = DRV_IOW(DbgIpeForwardingInfo_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_ipe_forwarding_info);
        DRV_IOCTL(lchip, 1, cmd, &dbg_ipe_forwarding_info);/* cascade */
        cmd = DRV_IOW(DbgIpeIntfMapperInfo_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_ipe_intf_mapper_info);
        DRV_IOCTL(lchip, 1, cmd, &dbg_ipe_intf_mapper_info);/* cascade */
        cmd = DRV_IOW(DbgIpeIpRoutingInfo_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_ipe_ip_routing_info);
        DRV_IOCTL(lchip, 1, cmd, &dbg_ipe_ip_routing_info);/* cascade */
        cmd = DRV_IOW(DbgIpeLkpMgrInfo_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_ipe_lkp_mgr_info);
        DRV_IOCTL(lchip, 1, cmd, &dbg_ipe_lkp_mgr_info);/* cascade */
        cmd = DRV_IOW(DbgIpeMacBridgingInfo_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_ipe_mac_bridging_info);
        DRV_IOCTL(lchip, 1, cmd, &dbg_ipe_mac_bridging_info);/* cascade */
        cmd = DRV_IOW(DbgIpeMacLearningInfo_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_ipe_mac_learning_info);
        DRV_IOCTL(lchip, 1, cmd, &dbg_ipe_mac_learning_info);/* cascade */
        cmd = DRV_IOW(DbgIpeMplsDecapInfo_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_ipe_mpls_decap_info);
        DRV_IOCTL(lchip, 1, cmd, &dbg_ipe_mpls_decap_info);/* cascade */
        cmd = DRV_IOW(DbgIpeOamInfo_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_ipe_oam_info);
        DRV_IOCTL(lchip, 1, cmd, &dbg_ipe_oam_info);/* cascade */
        cmd = DRV_IOW(DbgIpeTrillInfo_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_ipe_trill_info);
        DRV_IOCTL(lchip, 1, cmd, &dbg_ipe_trill_info);/* cascade */
        cmd = DRV_IOW(DbgIpeUserIdInfo_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_ipe_user_id_info);
        DRV_IOCTL(lchip, 1, cmd, &dbg_ipe_user_id_info);/* cascade */
        cmd = DRV_IOW(DbgIpfixAccEgrInfo_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_ipfix_acc_egr_info);
        /* DRV_IOCTL(lchip, 1, cmd, &dbg_ipfix_acc_egr_info);//share */
        cmd = DRV_IOW(DbgIpfixAccIngInfo_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_ipfix_acc_ing_info);
        /* DRV_IOCTL(lchip, 1, cmd, &dbg_ipfix_acc_ing_info);//share */
        cmd = DRV_IOW(DbgLearningEngineInfo_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_learning_engine_info);
        DRV_IOCTL(lchip, 1, cmd, &dbg_learning_engine_info);/* cascade */
        cmd = DRV_IOW(DbgOamDefectMsg_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_oam_defect_msg);
        /* DRV_IOCTL(lchip, 1, cmd, &dbg_oam_defect_msg);//share */
        cmd = DRV_IOW(DbgOamDefectProc_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_oam_defect_proc);
        /* DRV_IOCTL(lchip, 1, cmd, &dbg_oam_defect_proc);//share */
        cmd = DRV_IOW(DbgOamHdrAdj_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_oam_hdr_adj);
        /* DRV_IOCTL(lchip, 1, cmd, &dbg_oam_hdr_adj);//share */
        cmd = DRV_IOW(DbgOamHdrEdit_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_oam_hdr_edit);
        /* DRV_IOCTL(lchip, 1, cmd, &dbg_oam_hdr_edit);//share */
        cmd = DRV_IOW(DbgOamRxProc_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_oam_rx_proc);
        /* DRV_IOCTL(lchip, 1, cmd, &dbg_oam_rx_proc);//share */
        cmd = DRV_IOW(DbgPolicerEngineEgrInfo_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_policer_engine_egr_info);
        /* DRV_IOCTL(lchip, 1, cmd, &dbg_policer_engine_egr_info);//share */
        cmd = DRV_IOW(DbgPolicerEngineIngInfo_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_policer_engine_ing_info);
        /* DRV_IOCTL(lchip, 1, cmd, &dbg_policer_engine_ing_info);//share */
        cmd = DRV_IOW(DbgQMgrEnqInfo_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_q_mgr_enq_info);
        /* DRV_IOCTL(lchip, 1, cmd, &dbg_q_mgr_enq_info);//share */
        cmd = DRV_IOW(DbgStormCtlEngineInfo_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_storm_ctl_engine_info);
        /* DRV_IOCTL(lchip, 1, cmd, &dbg_storm_ctl_engine_info);//share */
        cmd = DRV_IOW(DbgUserIdHashEngineForMpls0Info_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_user_id_hash_engine_for_mpls0_info);
        DRV_IOCTL(lchip, 1, cmd, &dbg_user_id_hash_engine_for_mpls0_info);/* cascade */
        cmd = DRV_IOW(DbgUserIdHashEngineForMpls1Info_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_user_id_hash_engine_for_mpls1_info);
        DRV_IOCTL(lchip, 1, cmd, &dbg_user_id_hash_engine_for_mpls1_info);/* cascade */
        cmd = DRV_IOW(DbgUserIdHashEngineForMpls2Info_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_user_id_hash_engine_for_mpls2_info);
        DRV_IOCTL(lchip, 1, cmd, &dbg_user_id_hash_engine_for_mpls2_info);/* cascade */
        cmd = DRV_IOW(DbgUserIdHashEngineForUserId0Info_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_user_id_hash_engine_for_user_id0_info);
        DRV_IOCTL(lchip, 1, cmd, &dbg_user_id_hash_engine_for_user_id0_info);/* cascade */
        cmd = DRV_IOW(DbgUserIdHashEngineForUserId1Info_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &dbg_user_id_hash_engine_for_user_id1_info);
        DRV_IOCTL(lchip, 1, cmd, &dbg_user_id_hash_engine_for_user_id1_info);/* cascade */


    }

    if ((CTC_DKIT_CAPTURED_CLEAR_FLOW == flag) || (CTC_DKIT_CAPTURED_CLEAR_ALL == flag))
    {
        value = 0;
        cmd = DRV_IOW(IpeUserIdFlowCtl_t, IpeUserIdFlowCtl_flowEn_f);
        DRV_IOCTL(lchip, 0, cmd, &value);
        DRV_IOCTL(lchip, 1, cmd, &value);
    }

    return CLI_SUCCESS;
}


int32
ctc_goldengate_dkit_captured_install_flow(void* p_para)
{
    ctc_dkit_captured_t* p_captured_para = (ctc_dkit_captured_t*)p_para;
    uint32 value = 0;
    uint32 cmd = 0;
    parser_result_t parser_result;
    parser_result_t parser_result_mask;
    uint8 lchip = 0;

    DKITS_PTR_VALID_CHECK(p_para);
    lchip = p_captured_para->lchip;
    CTC_DKIT_LCHIP_CHECK(lchip);

    sal_memset(&parser_result, 0, sizeof(parser_result_t));
    sal_memset(&parser_result_mask, 0, sizeof(parser_result_t));

    if (p_captured_para->captured_session)
    {
        CTC_DKIT_PRINT("invalid session id!\n");
        return CLI_ERROR;
    }
    p_captured_para->flag = CTC_DKIT_CAPTURED_CLEAR_ALL;
    ctc_goldengate_dkit_captured_clear(p_captured_para);

    /*1. enable debug session*/
    value = 1;
    cmd = DRV_IOW(DebugSessionStatusCtl_t, DebugSessionStatusCtl_isFree_f);
    DRV_IOCTL(lchip, 0, cmd, &value);
    cmd = DRV_IOW(EpeHdrAdjustChanCtl_t, EpeHdrAdjustChanCtl_debugIsFree_f);
    DRV_IOCTL(lchip, 0, cmd, &value);
    DRV_IOCTL(lchip, 1, cmd, &value);

    if(1 == p_captured_para->flow_info_mask.port)
    {
        cmd = DRV_IOW(IpeUserIdFlowCtl_t, IpeUserIdFlowCtl_flowEn_f);
        if (CTC_DKIT_CTC_GPORT_TO_DRV_LPORT(p_captured_para->flow_info.port) >= CTC_DKIT_ONE_SLICE_PORT_NUM)
        {
            value = 0;
            DRV_IOCTL(lchip, 0, cmd, &value);/*slice0 disable*/
            value = 1;
            DRV_IOCTL(lchip, 1, cmd, &value);/*slice1 enable*/
        }
        else
        {
            value = 1;
            DRV_IOCTL(lchip, 0, cmd, &value);/*slice0 enable*/
            value = 0;
            DRV_IOCTL(lchip, 1, cmd, &value);/*slice1 disable*/
        }
        parser_result.local_phy_port = CTC_DKIT_CTC_GPORT_TO_DRV_LPORT(p_captured_para->flow_info.port)&0xFF;
        parser_result_mask.local_phy_port = 0xFF;
    }
    else
    {
        cmd = DRV_IOW(IpeUserIdFlowCtl_t, IpeUserIdFlowCtl_flowEn_f);
        value = 1;
        DRV_IOCTL(lchip, 0, cmd, &value);/*slice0 enable*/
        DRV_IOCTL(lchip, 1, cmd, &value);/*slice1 enable*/
    }

    /*2. store flow by parser result*/
    if (1 == p_captured_para->flow_info_mask.mac_da[0])
    {
        parser_result.mac_da10_0 = ((p_captured_para->flow_info.mac_da[4] << 8) + p_captured_para->flow_info.mac_da[5])&0x7FF;
        parser_result.mac_da42_11 = (p_captured_para->flow_info.mac_da[0] << 29)
                                                           + (p_captured_para->flow_info.mac_da[1] << 21)
                                                           + (p_captured_para->flow_info.mac_da[2] << 13)
                                                           + (p_captured_para->flow_info.mac_da[3] << 5)
                                                           + (p_captured_para->flow_info.mac_da[4] >> 3);
        parser_result.mac_da47_43 = (p_captured_para->flow_info.mac_da[0] >> 3) ;

        parser_result_mask.mac_da10_0 = 0x7FF;
        parser_result_mask.mac_da42_11 = 0xFFFF;
        parser_result_mask.mac_da47_43 = 0x1F;

    }
    if (1 == p_captured_para->flow_info_mask.mac_sa[0])
    {
        parser_result.mac_sa26_0 = (p_captured_para->flow_info.mac_sa[2]<<24)
                                                           +(p_captured_para->flow_info.mac_sa[3]<<16)
                                                           +(p_captured_para->flow_info.mac_sa[4]<<8)
                                                           + p_captured_para->flow_info.mac_sa[5];

        parser_result.mac_sa47_27 =  (p_captured_para->flow_info.mac_sa[0]<<13)
                                                             +(p_captured_para->flow_info.mac_sa[1]<<5)
                                                             +(p_captured_para->flow_info.mac_sa[2]>>3);

        parser_result_mask.mac_sa26_0 = 0x7FFFFFF;
        parser_result_mask.mac_sa47_27 = 0x1FFFFF;
    }
    if (1 == p_captured_para->flow_info_mask.svlan_id)
    {
        parser_result.svlan_id4_0 = (p_captured_para->flow_info.svlan_id)&0x1F;
        parser_result.svlan_id11_5 = ((p_captured_para->flow_info.svlan_id) >> 5)&0x3F;
        parser_result_mask.svlan_id4_0 = 0x1F;
        parser_result_mask.svlan_id11_5 = 0x3F;
    }
    if (1 == p_captured_para->flow_info_mask.cvlan_id)
    {
        parser_result.cvlan_id = (p_captured_para->flow_info.cvlan_id)&0xFFF;
        parser_result_mask.cvlan_id = 0xFFF;
    }
    if (1 == p_captured_para->flow_info_mask.ether_type)
    {
        parser_result.ether_type = p_captured_para->flow_info.ether_type;
        parser_result_mask.ether_type = 0xFFFF;
    }
    if (1 == p_captured_para->flow_info_mask.l3_type)
    {
        switch (p_captured_para->flow_info.l3_type)
        {
        case CTC_DKIT_CAPTURED_IPV4:
            if (1 == p_captured_para->flow_info_mask.u_l3.ipv4.ip_da)
            {
                parser_result.u_l3_dest_g_ipv6_ip_da12_0 += p_captured_para->flow_info.u_l3.ipv4.ip_da&0x1FFF;
                parser_result.u_l3_dest_g_ipv6_ip_da44_13 += (p_captured_para->flow_info.u_l3.ipv4.ip_da >> 13)&0x7FFFF;
                parser_result_mask.u_l3_dest_g_ipv6_ip_da12_0 += 0x1FFF;
                parser_result_mask.u_l3_dest_g_ipv6_ip_da44_13 += 0x7FFFF;
            }

            if (1 == p_captured_para->flow_info_mask.u_l3.ipv4.ip_sa)
            {
                parser_result.u_l3_source_g_ipv6_ip_sa12_0 += p_captured_para->flow_info.u_l3.ipv4.ip_sa&0x1FFF;
                parser_result.u_l3_source_g_ipv6_ip_sa44_13 += (p_captured_para->flow_info.u_l3.ipv4.ip_sa >> 13)&0x7FFFF;
                parser_result_mask.u_l3_source_g_ipv6_ip_sa12_0 += 0x1FFF;
                parser_result_mask.u_l3_source_g_ipv6_ip_sa44_13 += 0x7FFFF;
            }
            if (1 == p_captured_para->flow_info_mask.u_l3.ipv4.ip_check_sum)
            {
                parser_result.u_l3_source_g_ipv6_ip_sa44_13 += (p_captured_para->flow_info.u_l3.ipv4.ip_check_sum&0x1FFF) << 19;
                parser_result.u_l3_source_g_ipv6_ip_sa76_45 += (p_captured_para->flow_info.u_l3.ipv4.ip_check_sum >> 13)&0x7;
                parser_result_mask.u_l3_source_g_ipv6_ip_sa44_13 += 0x1FFF << 19;
                parser_result_mask.u_l3_source_g_ipv6_ip_sa76_45 += 0x7;
            }
            break;
        case CTC_DKIT_CAPTURED_IPV6:
            if (1 == p_captured_para->flow_info_mask.u_l3.ipv6.ip_da[0])
            {
                parser_result.u_l3_dest_g_ipv6_ip_da12_0 += p_captured_para->flow_info.u_l3.ipv6.ip_da[3]&0x1FFF;
                parser_result.u_l3_dest_g_ipv6_ip_da44_13 +=  (p_captured_para->flow_info.u_l3.ipv6.ip_da[3] >> 13)
                                                                                                    + (p_captured_para->flow_info.u_l3.ipv6.ip_da[2] <<  19);
                parser_result.u_l3_dest_g_ipv6_ip_da76_45 +=  (p_captured_para->flow_info.u_l3.ipv6.ip_da[2] >> 13)
                                                                                                    + (p_captured_para->flow_info.u_l3.ipv6.ip_da[1] <<  19);
                parser_result.u_l3_dest_g_ipv6_ip_da108_77 +=  (p_captured_para->flow_info.u_l3.ipv6.ip_da[1] >> 13)
                                                                                                    + (p_captured_para->flow_info.u_l3.ipv6.ip_da[0] <<  19);
                parser_result.u_l3_dest_g_ipv6_ip_da127_109 += (p_captured_para->flow_info.u_l3.ipv6.ip_da[0] >>13);

                parser_result_mask.u_l3_dest_g_ipv6_ip_da12_0 += 0x1FFF;
                parser_result_mask.u_l3_dest_g_ipv6_ip_da44_13 +=  0xFFFFFFFF;
                parser_result_mask.u_l3_dest_g_ipv6_ip_da76_45 +=  0xFFFFFFFF;
                parser_result_mask.u_l3_dest_g_ipv6_ip_da108_77 +=  0xFFFFFFFF;
                parser_result_mask.u_l3_dest_g_ipv6_ip_da127_109 += 0x7FFFF;
            }
            if (1 == p_captured_para->flow_info_mask.u_l3.ipv6.ip_sa[0])
            {
                parser_result.u_l3_source_g_ipv6_ip_sa12_0 += p_captured_para->flow_info.u_l3.ipv6.ip_sa[3]&0x1FFF;
                parser_result.u_l3_source_g_ipv6_ip_sa44_13 +=  (p_captured_para->flow_info.u_l3.ipv6.ip_sa[3] >> 13)
                                                                                                    + (p_captured_para->flow_info.u_l3.ipv6.ip_sa[2] <<  19);
                parser_result.u_l3_source_g_ipv6_ip_sa76_45 +=  (p_captured_para->flow_info.u_l3.ipv6.ip_sa[2] >> 13)
                                                                                                    + (p_captured_para->flow_info.u_l3.ipv6.ip_sa[1] <<  19);
                parser_result.u_l3_source_g_ipv6_ip_sa108_77 +=  (p_captured_para->flow_info.u_l3.ipv6.ip_sa[1] >> 13)
                                                                                                    + (p_captured_para->flow_info.u_l3.ipv6.ip_sa[0] <<  19);
                parser_result.u_l3_source_g_ipv6_ip_sa127_109 += (p_captured_para->flow_info.u_l3.ipv6.ip_sa[0] >>13);

                parser_result_mask.u_l3_source_g_ipv6_ip_sa12_0 += 0x1FFF;
                parser_result_mask.u_l3_source_g_ipv6_ip_sa44_13 +=  0xFFFFFFFF;
                parser_result_mask.u_l3_source_g_ipv6_ip_sa76_45 +=  0xFFFFFFFF;
                parser_result_mask.u_l3_source_g_ipv6_ip_sa108_77 +=  0xFFFFFFFF;
                parser_result_mask.u_l3_source_g_ipv6_ip_sa127_109 += 0x7FFFF;
            }
            break;
        case CTC_DKIT_CAPTURED_MPLS:
            if (1 == p_captured_para->flow_info_mask.u_l3.mpls.mpls_label0)
            {
                parser_result.u_l3_dest_g_ipv6_ip_da127_109 += (p_captured_para->flow_info.u_l3.mpls.mpls_label0 >> 13);
                parser_result.u_l3_dest_g_ipv6_ip_da108_77 +=  (p_captured_para->flow_info.u_l3.mpls.mpls_label0 <<  19);
                parser_result_mask.u_l3_dest_g_ipv6_ip_da127_109 += 0x7FFFF;
                parser_result_mask.u_l3_dest_g_ipv6_ip_da108_77 +=  0x1FFF<<19;
            }
            if (1 == p_captured_para->flow_info_mask.u_l3.mpls.mpls_label1)
            {
                parser_result.u_l3_dest_g_ipv6_ip_da108_77 +=  (p_captured_para->flow_info.u_l3.mpls.mpls_label1 >> 13);
                parser_result.u_l3_dest_g_ipv6_ip_da76_45 +=  (p_captured_para->flow_info.u_l3.mpls.mpls_label1 <<  19);
                parser_result_mask.u_l3_dest_g_ipv6_ip_da108_77 += 0x7FFFF;
                parser_result_mask.u_l3_dest_g_ipv6_ip_da76_45 +=  0x1FFF<<19;
            }
            if (1 == p_captured_para->flow_info_mask.u_l3.mpls.mpls_label2)
            {
                parser_result.u_l3_dest_g_ipv6_ip_da76_45 +=  (p_captured_para->flow_info.u_l3.mpls.mpls_label2 >> 13);
                parser_result.u_l3_dest_g_ipv6_ip_da44_13 +=  (p_captured_para->flow_info.u_l3.mpls.mpls_label2 <<  19);
                parser_result_mask.u_l3_dest_g_ipv6_ip_da76_45 += 0x7FFFF;
                parser_result_mask.u_l3_dest_g_ipv6_ip_da44_13 +=  0x1FFF<<19;
            }
            if (1 == p_captured_para->flow_info_mask.u_l3.mpls.mpls_label3)
            {
                parser_result.u_l3_dest_g_ipv6_ip_da44_13 +=  (p_captured_para->flow_info.u_l3.mpls.mpls_label3 >> 13);
                parser_result.u_l3_dest_g_ipv6_ip_da12_0 +=  (p_captured_para->flow_info.u_l3.mpls.mpls_label3 & 0x1FFF);
                parser_result_mask.u_l3_dest_g_ipv6_ip_da44_13 += 0x7FFFF;
                parser_result_mask.u_l3_dest_g_ipv6_ip_da12_0 +=  0x1FFF;
            }
            break;
        case CTC_DKIT_CAPTURED_SLOW_PROTOCOL:
            if (1 == p_captured_para->flow_info_mask.u_l3.slow_proto.sub_type)
            {
                parser_result.u_l3_dest_g_ipv6_ip_da44_13 += p_captured_para->flow_info.u_l3.slow_proto.sub_type << 11;
                parser_result_mask.u_l3_dest_g_ipv6_ip_da44_13 += 0xFF << 11;
            }
            if (1 == p_captured_para->flow_info_mask.u_l3.slow_proto.flags)
            {
                parser_result.u_l3_dest_g_ipv6_ip_da44_13 += p_captured_para->flow_info.u_l3.slow_proto.flags >> 5;
                parser_result_mask.u_l3_dest_g_ipv6_ip_da44_13 += 0xFF >> 5;
                parser_result.u_l3_dest_g_ipv6_ip_da12_0 += (p_captured_para->flow_info.u_l3.slow_proto.flags&0x1F) << 8;
                parser_result_mask.u_l3_dest_g_ipv6_ip_da12_0 += 0x1F << 8;
            }
            if (1 == p_captured_para->flow_info_mask.u_l3.slow_proto.code)
            {
                parser_result.u_l3_dest_g_ipv6_ip_da12_0 += p_captured_para->flow_info.u_l3.slow_proto.code;
                parser_result_mask.u_l3_dest_g_ipv6_ip_da12_0 += 0xFF;
            }
            break;
        case CTC_DKIT_CAPTURED_ETHOAM:
            if (1 == p_captured_para->flow_info_mask.u_l3.ether_oam.level)
            {
                parser_result.u_l3_dest_g_ipv6_ip_da44_13 += p_captured_para->flow_info.u_l3.ether_oam.level;
                parser_result_mask.u_l3_dest_g_ipv6_ip_da44_13 += 0x7;
            }
            if (1 == p_captured_para->flow_info_mask.u_l3.ether_oam.version)
            {
                parser_result.u_l3_dest_g_ipv6_ip_da12_0 += p_captured_para->flow_info.u_l3.ether_oam.version << 8;
                parser_result_mask.u_l3_dest_g_ipv6_ip_da12_0 += 0x1F << 8;
            }
            if (1 == p_captured_para->flow_info_mask.u_l3.ether_oam.opcode)
            {
                parser_result.u_l3_dest_g_ipv6_ip_da12_0 += p_captured_para->flow_info.u_l3.ether_oam.opcode;
                parser_result_mask.u_l3_dest_g_ipv6_ip_da12_0 += 0xFF;
            }
            break;
        default:
            break;
        }
    }

    cmd = DRV_IOW(IpeUserIdFlowCam_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &parser_result);/* entry 0 is data */
    DRV_IOCTL(lchip, 1, cmd, &parser_result_mask);/* entry 0 is mask */

    return CLI_SUCCESS;
}


int32
ctc_goldengate_dkit_captured_get_discard_sub_reason(ctc_dkit_discard_para_t* p_para, ctc_dkit_discard_info_t* p_discard_info)
{
    DKITS_PTR_VALID_CHECK(p_discard_info);
    DKITS_PTR_VALID_CHECK(p_para);
    p_discard_info->discard_reason_captured.reason_id = CTC_DKIT_SUB_DISCARD_INVALIED;


    return CLI_SUCCESS;
}




#include "ctc_error.h"
#include "ctc_debug.h"
#include "ctc_hash.h"
#include "ctc_linklist.h"

#include "ctc_qos.h"
#include "ctc_packet.h"
#include "ctc_stats.h"
#include "sys_usw_diag.h"
#include "sys_usw_linkagg.h"
#include "sys_usw_nexthop_api.h"
#include "sys_usw_mchip.h"
#include "sys_usw_stats_api.h"
#include "usw/include/drv_common.h"

extern int32
sys_usw_dump_db_register_cb(uint8 lchip, uint8 module, CTC_DUMP_MASTER_FUNC fn);

typedef struct sys_usw_diag_drop_pkt_node_s
{
    ctc_slistnode_t head;

    uint16 lport;
    uint16 reason;
    uint32 hash_seed;
    sal_time_t old_timestamp;
    sal_time_t timestamp;
    uint32 pkt_id;
    uint32 len;
    uint8* buf;
}sys_usw_diag_drop_pkt_node_t;

typedef struct sys_usw_diag_drop_info_s
{
    /* key */
    uint16 lport;  /* 0xFFFF means system drop stats */
    uint16 reason;

    /* data */
    uint32 count;/*drop-count*/
}sys_usw_diag_drop_info_t;

struct sys_usw_diag_lb_dist_s
{
    uint16    group_id;
    uint16    flag;
    uint32    member_num;
    ctc_diag_lb_info_t* info;
};
typedef struct sys_usw_diag_lb_dist_s sys_usw_diag_lb_dist_t;

sys_usw_diag_t* p_usw_diag_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};

#define DIAG_LOCK \
    if (p_usw_diag_master[lchip]->diag_mutex) sal_mutex_lock(p_usw_diag_master[lchip]->diag_mutex)
#define DIAG_UNLOCK \
    if (p_usw_diag_master[lchip]->diag_mutex) sal_mutex_unlock(p_usw_diag_master[lchip]->diag_mutex)

#define SYS_DIAG_DBG_OUT(level, FMT, ...) \
    CTC_DEBUG_OUT(diag, diag, DIAG_SYS, level, FMT, ## __VA_ARGS__)

#define SYS_USW_DIAG_INIT_CHECK(lchip)  \
    do{                                 \
        LCHIP_CHECK(lchip);         \
        SYS_LCHIP_CHECK_ACTIVE(lchip); \
        if (p_usw_diag_master[lchip] == NULL)    \
        { SYS_DIAG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, " Feature not initialized \n");\
          return CTC_E_NOT_INIT; } \
      }while(0)

extern int32
sys_usw_qos_set_queue(uint8 lchip, ctc_qos_queue_cfg_t* p_que_cfg);
extern fields_t*
_drv_find_field(uint8 lchip, tbls_id_t tbl_id, fld_id_t field_id);
extern int32
sys_usw_qos_set_shape(uint8 lchip, ctc_qos_shape_t* p_shape);

extern int32
sys_usw_packet_tx(uint8 lchip, ctc_pkt_tx_t* p_pkt_tx);

extern int32
sys_usw_packet_rx_register(uint8 lchip, ctc_pkt_rx_register_t* p_register);

extern int32
sys_usw_packet_rx_unregister(uint8 lchip, ctc_pkt_rx_register_t* p_register);

int32
sys_usw_diag_drop_pkt_sync(ctc_pkt_rx_t* p_pkt_rx);

extern int32
sys_usw_oam_get_used_key_count(uint8 lchip, uint32* count);
extern int32
sys_usw_ipuc_get_tcam_memusage(uint8 lchip, uint8 sub_type, uint32* total_size, uint32* used_size);
extern int32
sys_usw_ftm_process_callback(uint8 lchip, uint32 spec_type, sys_ftm_specs_info_t* specs_info);
extern int32
sys_usw_aps_ftm_cb(uint8 lchip, uint32 *entry_num);
extern int32
sys_usw_acl_get_resource_by_priority(uint8 lchip, uint8 priority, uint8 dir, uint32*total, uint32* used);
extern int32
sys_usw_scl_get_resource_by_priority(uint8 lchip, uint8 priority, uint32*total, uint32* used);
STATIC int32
_sys_usw_diag_tcam_io(uint8 lchip, uint32 index, uint32 cmd, void* val)
{
    uint32 tbl_id = 0;
    uint32 new_index = 0;

    tbl_id = DRV_IOC_MEMID(cmd);

    new_index = index;

    drv_usw_ftm_map_tcam_index(lchip, tbl_id, index, &new_index);
    return DRV_IOCTL(lchip, new_index, cmd, val);
}

STATIC int32
_sys_usw_diag_table_str_to_upper(char* str)
{
    uint32  i = 0;
    /* convert the alphabets to upper case before comparison */
    for (i = 0; i < sal_strlen((char*)str); i++)
    {
        if (sal_isalpha((int)str[i]) && sal_islower((int)str[i]))
        {
            str[i] = sal_toupper(str[i]);
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_usw_diag_table_is_hash_key(uint8 lchip, uint32 tbl_id)
{
    uint8 is_hash_key = 0;

    switch (tbl_id)
    {
    case DsFibHost0MacHashKey_t:
        is_hash_key = 1;
        break;

    case DsFibHost0FcoeHashKey_t:
    case DsFibHost0Ipv4HashKey_t:
    case DsFibHost0Ipv6McastHashKey_t:
    case DsFibHost0Ipv6UcastHashKey_t:
    case DsFibHost0MacIpv6McastHashKey_t:
    case DsFibHost0TrillHashKey_t:
        is_hash_key = 1;
        break;

    case DsFibHost1FcoeRpfHashKey_t:
    case DsFibHost1Ipv4McastHashKey_t:
    case DsFibHost1Ipv4NatDaPortHashKey_t:
    case DsFibHost1Ipv4NatSaPortHashKey_t:
    case DsFibHost1Ipv6McastHashKey_t:
    case DsFibHost1Ipv6NatDaPortHashKey_t:
    case DsFibHost1Ipv6NatSaPortHashKey_t:
    case DsFibHost1MacIpv4McastHashKey_t:
    case DsFibHost1MacIpv6McastHashKey_t:
    case DsFibHost1TrillMcastVlanHashKey_t:
        is_hash_key = 1;
        break;

    case DsEgressXcOamBfdHashKey_t:
    case DsEgressXcOamCvlanCosPortHashKey_t:
    case DsEgressXcOamCvlanPortHashKey_t:
    case DsEgressXcOamDoubleVlanPortHashKey_t:
    case DsEgressXcOamEthHashKey_t:
    case DsEgressXcOamMplsLabelHashKey_t:
    case DsEgressXcOamMplsSectionHashKey_t:
    case DsEgressXcOamPortCrossHashKey_t:
    case DsEgressXcOamPortHashKey_t:
    case DsEgressXcOamPortVlanCrossHashKey_t:
    case DsEgressXcOamRmepHashKey_t:
    case DsEgressXcOamSvlanCosPortHashKey_t:
    case DsEgressXcOamSvlanPortHashKey_t:
    case DsEgressXcOamSvlanPortMacHashKey_t:
    case DsEgressXcOamTunnelPbbHashKey_t:
        is_hash_key = 1;
        break;

    case DsFlowL2HashKey_t:
    case DsFlowL2L3HashKey_t:
    case DsFlowL3Ipv4HashKey_t:
    case DsFlowL3Ipv6HashKey_t:
    case DsFlowL3MplsHashKey_t:
        is_hash_key = 1;
        break;

    case DsUserIdCapwapMacDaForwardHashKey_t:
    case DsUserIdCapwapStaStatusHashKey_t:
    case DsUserIdCapwapStaStatusMcHashKey_t:
    case DsUserIdCapwapVlanForwardHashKey_t:
    case DsUserIdCvlanCosPortHashKey_t:
    case DsUserIdCvlanPortHashKey_t:
    case DsUserIdDoubleVlanPortHashKey_t:
    case DsUserIdEcidNameSpaceHashKey_t:
    case DsUserIdIngEcidNameSpaceHashKey_t:
    case DsUserIdIpv4PortHashKey_t:
    case DsUserIdIpv4SaHashKey_t:
    case DsUserIdIpv6PortHashKey_t:
    case DsUserIdIpv6SaHashKey_t:
    case DsUserIdMacHashKey_t:
    case DsUserIdMacPortHashKey_t:
    case DsUserIdPortHashKey_t:
    case DsUserIdSclFlowL2HashKey_t:
    case DsUserIdSvlanCosPortHashKey_t:
    case DsUserIdSvlanHashKey_t:
    case DsUserIdSvlanMacSaHashKey_t:
    case DsUserIdSvlanPortHashKey_t:
    case DsUserIdTunnelCapwapRmacHashKey_t:
    case DsUserIdTunnelCapwapRmacRidHashKey_t:
    case DsUserIdTunnelIpv4CapwapHashKey_t:
    case DsUserIdTunnelIpv4DaHashKey_t:
    case DsUserIdTunnelIpv4GreKeyHashKey_t:
    case DsUserIdTunnelIpv4HashKey_t:
    case DsUserIdTunnelIpv4McNvgreMode0HashKey_t:
    case DsUserIdTunnelIpv4McVxlanMode0HashKey_t:
    case DsUserIdTunnelIpv4NvgreMode1HashKey_t:
    case DsUserIdTunnelIpv4RpfHashKey_t:
    case DsUserIdTunnelIpv4UcNvgreMode0HashKey_t:
    case DsUserIdTunnelIpv4UcNvgreMode1HashKey_t:
    case DsUserIdTunnelIpv4UcVxlanMode0HashKey_t:
    case DsUserIdTunnelIpv4UcVxlanMode1HashKey_t:
    case DsUserIdTunnelIpv4UdpHashKey_t:
    case DsUserIdTunnelIpv4VxlanMode1HashKey_t:
    case DsUserIdTunnelIpv6CapwapHashKey_t:
    case DsUserIdTunnelIpv6McNvgreMode0HashKey_t:
    case DsUserIdTunnelIpv6McNvgreMode1HashKey_t:
    case DsUserIdTunnelIpv6McVxlanMode0HashKey_t:
    case DsUserIdTunnelIpv6McVxlanMode1HashKey_t:
    case DsUserIdTunnelIpv6UcNvgreMode0HashKey_t:
    case DsUserIdTunnelIpv6UcNvgreMode1HashKey_t:
    case DsUserIdTunnelIpv6UcVxlanMode0HashKey_t:
    case DsUserIdTunnelIpv6UcVxlanMode1HashKey_t:
    case DsUserIdTunnelIpv6DaHashKey_t:
    case DsUserIdTunnelIpv6GreKeyHashKey_t:
    case DsUserIdTunnelIpv6HashKey_t:
    case DsUserIdTunnelIpv6UdpHashKey_t:
    case DsUserIdTunnelMplsHashKey_t:
    case DsUserIdTunnelPbbHashKey_t:
    case DsUserIdTunnelTrillMcAdjHashKey_t:
    case DsUserIdTunnelTrillMcDecapHashKey_t:
    case DsUserIdTunnelTrillMcRpfHashKey_t:
    case DsUserIdTunnelTrillUcDecapHashKey_t:
    case DsUserIdTunnelTrillUcRpfHashKey_t:
        /**< [TM] */
    case DsUserId1CapwapMacDaForwardHashKey_t:
    case DsUserId1CapwapStaStatusHashKey_t:
    case DsUserId1CapwapStaStatusMcHashKey_t:
    case DsUserId1CapwapVlanForwardHashKey_t:
    case DsUserId1CvlanCosPortHashKey_t:
    case DsUserId1CvlanPortHashKey_t:
    case DsUserId1DoubleVlanPortHashKey_t:
    case DsUserId1EcidNameSpaceHashKey_t:
    case DsUserId1IngEcidNameSpaceHashKey_t:
    case DsUserId1Ipv4PortHashKey_t:
    case DsUserId1Ipv4SaHashKey_t:
    case DsUserId1Ipv6PortHashKey_t:
    case DsUserId1Ipv6SaHashKey_t:
    case DsUserId1MacHashKey_t:
    case DsUserId1MacPortHashKey_t:
    case DsUserId1PortHashKey_t:
    case DsUserId1SclFlowL2HashKey_t:
    case DsUserId1SvlanCosPortHashKey_t:
    case DsUserId1SvlanHashKey_t:
    case DsUserId1SvlanMacSaHashKey_t:
    case DsUserId1SvlanPortHashKey_t:
    case DsUserId1TunnelCapwapRmacHashKey_t:
    case DsUserId1TunnelCapwapRmacRidHashKey_t:
    case DsUserId1TunnelIpv4CapwapHashKey_t:
    case DsUserId1TunnelIpv4DaHashKey_t:
    case DsUserId1TunnelIpv4GreKeyHashKey_t:
    case DsUserId1TunnelIpv4HashKey_t:
    case DsUserId1TunnelIpv4McNvgreMode0HashKey_t:
    case DsUserId1TunnelIpv4McVxlanMode0HashKey_t:
    case DsUserId1TunnelIpv4NvgreMode1HashKey_t:
    case DsUserId1TunnelIpv4RpfHashKey_t:
    case DsUserId1TunnelIpv4UcNvgreMode0HashKey_t:
    case DsUserId1TunnelIpv4UcNvgreMode1HashKey_t:
    case DsUserId1TunnelIpv4UcVxlanMode0HashKey_t:
    case DsUserId1TunnelIpv4UcVxlanMode1HashKey_t:
    case DsUserId1TunnelIpv4UdpHashKey_t:
    case DsUserId1TunnelIpv4VxlanMode1HashKey_t:
    case DsUserId1TunnelIpv6CapwapHashKey_t:
    case DsUserId1TunnelIpv6McNvgreMode0HashKey_t:
    case DsUserId1TunnelIpv6McNvgreMode1HashKey_t:
    case DsUserId1TunnelIpv6McVxlanMode0HashKey_t:
    case DsUserId1TunnelIpv6McVxlanMode1HashKey_t:
    case DsUserId1TunnelIpv6UcNvgreMode0HashKey_t:
    case DsUserId1TunnelIpv6UcNvgreMode1HashKey_t:
    case DsUserId1TunnelIpv6UcVxlanMode0HashKey_t:
    case DsUserId1TunnelIpv6UcVxlanMode1HashKey_t:
    case DsUserId1TunnelIpv6DaHashKey_t:
    case DsUserId1TunnelIpv6GreKeyHashKey_t:
    case DsUserId1TunnelIpv6HashKey_t:
    case DsUserId1TunnelIpv6UdpHashKey_t:
    case DsUserId1TunnelTrillMcAdjHashKey_t:
    case DsUserId1TunnelTrillMcDecapHashKey_t:
    case DsUserId1TunnelTrillMcRpfHashKey_t:
    case DsUserId1TunnelTrillUcDecapHashKey_t:
    case DsUserId1TunnelTrillUcRpfHashKey_t:
        is_hash_key = 1;
        break;

    case DsIpfixL2HashKey_t:
    case DsIpfixL2L3HashKey_t:
    case DsIpfixL3Ipv4HashKey_t:
    case DsIpfixL3Ipv6HashKey_t:
    case DsIpfixL3MplsHashKey_t:
    case DsIpfixSessionRecord_t:
    case DsIpfixL2HashKey0_t:
    case DsIpfixL2L3HashKey0_t:
    case DsIpfixL3Ipv4HashKey0_t:
    case DsIpfixL3Ipv6HashKey0_t:
    case DsIpfixL3MplsHashKey0_t:
    case DsIpfixSessionRecord0_t:
    case DsIpfixL2HashKey1_t:
    case DsIpfixL2L3HashKey1_t:
    case DsIpfixL3Ipv4HashKey1_t:
    case DsIpfixL3Ipv6HashKey1_t:
    case DsIpfixL3MplsHashKey1_t:
    case DsIpfixSessionRecord1_t:
        is_hash_key = 1;
        break;

    case DsMplsLabelHashKey_t:
        is_hash_key = 1;
        break;

    case DsGemPortHashKey_t:
        is_hash_key = 1;
        break;

    default:
        is_hash_key = 0;
    }
    return is_hash_key;
}

STATIC int
_sys_usw_diag_table_acc_read(uint8 lchip, uint32 tbl_id, uint32 index, void *p_data)
{
    int ret = CTC_E_NONE;

    drv_acc_in_t  in;
    drv_acc_out_t out;

    sal_memset(&in, 0, sizeof(in));
    sal_memset(&out, 0, sizeof(out));

    in.type = DRV_ACC_TYPE_LOOKUP;
    in.tbl_id = tbl_id;
    in.index = index;
    in.op_type = DRV_ACC_OP_BY_INDEX;
    ret = drv_acc_api(lchip, &in, &out);

    sal_memcpy((uint8*)p_data, (uint8*)(out.data), TABLE_ENTRY_SIZE(lchip, tbl_id));

    return ret;
}

STATIC int32
_sys_usw_diag_table_acc_write(uint8 lchip, uint32 tbl_id, uint32 index, void *p_data)
{
    int ret = CTC_E_NONE;

    drv_acc_in_t  in;
    drv_acc_out_t out;

    sal_memset(&in, 0, sizeof(in));
    sal_memset(&out, 0, sizeof(out));

    in.type = DRV_ACC_TYPE_ADD;
    in.tbl_id = tbl_id;
    in.data = p_data;
    in.index = index;
    in.op_type = DRV_ACC_OP_BY_INDEX;
    ret = drv_acc_api(lchip, &in, &out);

    return ret;
}

uint32
_sys_usw_diag_drop_hash_make(sys_usw_diag_drop_info_t* node)
{
    return ctc_hash_caculate(sizeof(node->lport) + sizeof(node->reason), &(node->lport));
}

uint32
_sys_usw_diag_dist_hash_make(sys_usw_diag_lb_dist_t* node)
{
    return ctc_hash_caculate(sizeof(node->group_id)+sizeof(node->flag), &(node->group_id));
}

bool
_sys_usw_diag_drop_hash_cmp(sys_usw_diag_drop_info_t* stored_node, sys_usw_diag_drop_info_t* lkup_node)
{
    if (!stored_node || !lkup_node)
    {
        return FALSE;
    }
    if ((stored_node->lport == lkup_node->lport)
        && (stored_node->reason == lkup_node->reason))
    {
        return TRUE;
    }
    return FALSE;
}

bool
_sys_usw_diag_dist_hash_cmp(sys_usw_diag_lb_dist_t* stored_node, sys_usw_diag_lb_dist_t* lkup_node)
{
    if (!stored_node || !lkup_node)
    {
        return FALSE;
    }
    if ((stored_node->group_id == lkup_node->group_id)
        && (stored_node->flag == lkup_node->flag))
    {
        return TRUE;
    }
    return FALSE;
}

STATIC int32
_sys_usw_diag_free_drop_hash_data(void* node_data, void* user_data)
{
    sys_usw_diag_drop_info_t* info = (sys_usw_diag_drop_info_t*)node_data;
    if (info)
    {
        mem_free(info);
    }
    return 1;
}

STATIC int32
_sys_usw_diag_free_dist_hash_data(void* node_data, void* user_data)
{
    sys_usw_diag_lb_dist_t* info = (sys_usw_diag_lb_dist_t*)node_data;
    if (info)
    {
        if(info->info)
        {
            mem_free(info->info);
        }
        mem_free(info);
    }
    return CTC_E_NONE;
}

STATIC sys_usw_diag_drop_info_t*
_sys_usw_diag_drop_hash_lkup(uint8 lchip, uint16 lport, uint16 reason)
{
    sys_usw_diag_drop_info_t info;
    sal_memset(&info, 0, sizeof(sys_usw_diag_drop_info_t));
    info.lport = lport;
    info.reason = reason;
    return ctc_hash_lookup(p_usw_diag_master[lchip]->drop_hash, &info);
}

STATIC sys_usw_diag_lb_dist_t*
_sys_usw_diag_dist_hash_lkup(uint8 lchip, uint16 group_id, uint16 flag)
{
    sys_usw_diag_lb_dist_t info;
    sal_memset(&info, 0, sizeof(sys_usw_diag_lb_dist_t));
    info.group_id = group_id;
    info.flag = flag;
    return ctc_hash_lookup(p_usw_diag_master[lchip]->lb_dist, &info);
}

STATIC int32
_sys_usw_diag_drop_hash_add(uint8 lchip, sys_usw_diag_drop_info_t* info)
{
    sys_usw_diag_drop_info_t* node = NULL;

    node = ctc_hash_lookup(p_usw_diag_master[lchip]->drop_hash, info);
    if (NULL == node)
    {
        node = mem_malloc(MEM_DIAG_MODULE, sizeof(sys_usw_diag_drop_info_t));
        if (NULL == node)
        {
            SYS_DIAG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "Add Drop Hash Info Fail. No Memory!\n");
            return CTC_E_NO_MEMORY;
        }
        sal_memcpy(node, info,sizeof(sys_usw_diag_drop_info_t));
        if (NULL == ctc_hash_insert(p_usw_diag_master[lchip]->drop_hash, node))
        {
            mem_free(node);
            SYS_DIAG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "Insert Drop Hash Info Fail. No Memory!\n");
            return CTC_E_NO_MEMORY;
        }
    }
    else
    {
        sal_memcpy(node, info,sizeof(sys_usw_diag_drop_info_t));
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_usw_diag_dist_hash_add(uint8 lchip, sys_usw_diag_lb_dist_t* info)
{
    if (NULL == ctc_hash_insert(p_usw_diag_master[lchip]->lb_dist, info))
    {
        return CTC_E_NO_MEMORY;
    }

    return CTC_E_NONE;
}
STATIC int32
_sys_usw_diag_drop_hash_remove(uint8 lchip, sys_usw_diag_drop_info_t* info)
{
    sys_usw_diag_drop_info_t* info_out = ctc_hash_remove(p_usw_diag_master[lchip]->drop_hash, (void*)info);
    if (NULL == info_out)
    {
        SYS_DIAG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "Remove Drop Hash Info Fail. Entry not exist!\n");
        return CTC_E_NOT_EXIST;
    }
    mem_free(info);
    return CTC_E_NONE;
}

STATIC int32
_sys_usw_diag_dist_hash_remove(uint8 lchip, sys_usw_diag_lb_dist_t* info)
{
    sys_usw_diag_lb_dist_t* info_out = ctc_hash_remove(p_usw_diag_master[lchip]->lb_dist, (void*)info);
    if (NULL == info_out)
    {
        SYS_DIAG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "Remove Drop Hash Info Fail. Entry not exist!\n");
        return CTC_E_NOT_EXIST;
    }
    if(info->info)
    {
       mem_free(info->info);
    }
    mem_free(info);
    return CTC_E_NONE;
}
int32
sys_usw_diag_drop_hash_update_count(uint8 lchip, uint16 lport, uint16 reason, uint32 count)
{
    sys_usw_diag_drop_info_t info_temp;
    sys_usw_diag_drop_info_t* info = NULL;
    if (count == 0)
    {
        return CTC_E_NONE;
    }
    info = _sys_usw_diag_drop_hash_lkup(lchip, lport, reason);
    if (info != NULL)
    {
        sal_memcpy(&info_temp, info, sizeof(sys_usw_diag_drop_info_t));
        info_temp.count += count;
    }
    else
    {
        sal_memset(&info_temp, 0, sizeof(sys_usw_diag_drop_info_t));
        info_temp.lport = lport;
        info_temp.reason = reason;
        info_temp.count = count;
    }
    CTC_ERROR_RETURN(_sys_usw_diag_drop_hash_add(lchip, &info_temp));
    return CTC_E_NONE;
}

STATIC int32
_sys_usw_diag_drop_pkt_clear_buf(uint8 lchip)
{
    ctc_slistnode_t* node = NULL;
    ctc_slistnode_t* next_node = NULL;

    CTC_SLIST_LOOP_DEL(p_usw_diag_master[lchip]->drop_pkt_list, node, next_node)
    {
        ctc_slist_delete_node(p_usw_diag_master[lchip]->drop_pkt_list, node);
        mem_free(((sys_usw_diag_drop_pkt_node_t*)node)->buf);
        mem_free(node);
    }
    return CTC_E_NONE;
}
STATIC int32
_sys_usw_diag_pkt_trace_watch_key_check(uint8 lchip, ctc_diag_pkt_trace_t* p_trace)
{
    if ((p_trace->mode != CTC_DIAG_TRACE_MODE_USER)
        && (p_trace->mode != CTC_DIAG_TRACE_MODE_NETWORK))
    {
        SYS_DIAG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "Packet Trace Mode Error! \n");
        return CTC_E_INVALID_PARAM;
    }

    if (p_trace->watch_point >= CTC_DIAG_TRACE_POINT_MAX)
    {
        SYS_DIAG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "Watch Point Error! \n");
        return CTC_E_INVALID_PARAM;
    }

    if (CTC_BMP_ISSET(p_trace->watch_key.key_mask_bmp, CTC_DIAG_WATCH_KEY_MASK_CHANNEL)
        && (p_trace->watch_key.channel >= MCHIP_CAP(SYS_CAP_CHANNEL_NUM)))
    {
        SYS_DIAG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "Watch Key Channel Invalid! \n");
        return CTC_E_INVALID_PARAM;
    }
    if (CTC_BMP_ISSET(p_trace->watch_key.key_mask_bmp, CTC_DIAG_WATCH_KEY_MASK_SRC_LPORT)
        && (p_trace->watch_key.src_lport >= MCHIP_CAP(SYS_CAP_SPEC_MAX_PHY_PORT_NUM)))
    {
        SYS_DIAG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "Watch Key SrcLport Invalid! \n");
        return CTC_E_INVALID_PARAM;
    }

    if (CTC_BMP_ISSET(p_trace->watch_key.key_mask_bmp, CTC_DIAG_WATCH_KEY_MASK_MC_GROUP)
        && CTC_BMP_ISSET(p_trace->watch_key.key_mask_bmp, CTC_DIAG_WATCH_KEY_MASK_DST_GPORT))
    {
        SYS_DIAG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "Watch Key DstGport & Mcast Group Should not Coexist! \n");
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

uint8
_sys_usw_diag_pkt_trace_decode_pkt_id(uint8 lchip, uint32 pkt_id, ctc_pkt_tx_t*  pkt_tx)
{
    uint8 find = 0;
    uint8 gchip = 0;
    ctc_slistnode_t* node = NULL;
    sys_usw_diag_drop_pkt_node_t* drop_node = NULL;

    if (!pkt_id)
    {
        return 0;
    }
    sys_usw_get_gchip_id(lchip, &gchip);

    CTC_SLIST_LOOP(p_usw_diag_master[lchip]->drop_pkt_list, node)
    {
        drop_node = (sys_usw_diag_drop_pkt_node_t*)node;
        if (pkt_id == drop_node->pkt_id)
        {
            find = 1;
            pkt_tx->skb.data = drop_node->buf;
            pkt_tx->skb.len = drop_node->len;
            pkt_tx->tx_info.src_port = CTC_MAP_LPORT_TO_GPORT(gchip, drop_node->lport);
            pkt_tx->tx_info.dest_gport = CTC_MAP_LPORT_TO_GPORT(gchip, drop_node->lport);
            break;
        }
    }

    if (!find)
    {
        SYS_DIAG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "Not Found PacketId:%d \n", pkt_id);
    }
    return find;
}


STATIC int32
_sys_usw_diag_trigger_pkt_trace(uint8 lchip, ctc_diag_pkt_trace_t* p_trace)
{
    int32 ret = CTC_E_NONE;
    ctc_pkt_tx_t*  pkt_tx = NULL;

    if (MCHIP_API(lchip)->diag_set_dbg_session)
    {
        CTC_ERROR_RETURN(MCHIP_API(lchip)->diag_set_dbg_session(lchip, p_trace));
    }

    if (p_trace->mode == CTC_DIAG_TRACE_MODE_USER)
    {
        /* Packet Tx */
        pkt_tx = mem_malloc(MEM_DIAG_MODULE, sizeof(ctc_pkt_tx_t));
        if (NULL == pkt_tx)
        {
            SYS_DIAG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "No memory! \n");
            return CTC_E_NO_MEMORY;
        }
        SYS_DIAG_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Packet Trace Mode: User! Len: %d Src_port:0x%.4x\n", p_trace->pkt.user.len, p_trace->pkt.user.src_port);
        sal_memset(pkt_tx, 0, sizeof(ctc_pkt_tx_t));
        pkt_tx->lchip = lchip;
        pkt_tx->mode = CTC_PKT_MODE_DMA;
        if (0 == _sys_usw_diag_pkt_trace_decode_pkt_id(lchip, p_trace->pkt.user.pkt_id, pkt_tx))
        {
            pkt_tx->skb.data = p_trace->pkt.user.pkt_buf;
            pkt_tx->skb.len = p_trace->pkt.user.len;
            pkt_tx->tx_info.src_port = p_trace->pkt.user.src_port;
            pkt_tx->tx_info.dest_gport = p_trace->pkt.user.src_port;
        }
        pkt_tx->tx_info.flags = CTC_PKT_FLAG_SRC_PORT_VALID | CTC_PKT_FLAG_INGRESS_MODE | CTC_PKT_FLAG_DIAG_PKT;
        ret = sys_usw_packet_tx(lchip, pkt_tx);
        mem_free(pkt_tx);
        return ret;
    }
    else if (MCHIP_API(lchip)->diag_set_dbg_pkt)
    {
        /* Network Key */
        SYS_DIAG_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Packet Trace Mode: Network!\n");
        CTC_ERROR_RETURN(MCHIP_API(lchip)->diag_set_dbg_pkt(lchip, p_trace));
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_usw_diag_drop_hash_traverse_cb(sys_usw_diag_drop_info_t* info, ctc_diag_drop_t* p_drop)
{
    CTC_PTR_VALID_CHECK(info);
    CTC_PTR_VALID_CHECK(p_drop);

    if (p_drop->oper_type == CTC_DIAG_DROP_OPER_TYPE_GET_PORT_BMP)
    {
        if (info->lport != SYS_DIAG_DROP_COUNT_SYSTEM)
        {
            CTC_BMP_SET(p_drop->u.port_bmp, info->lport);
        }
    }
    else if (p_drop->oper_type == CTC_DIAG_DROP_OPER_TYPE_GET_REASON_BMP)
    {
        if (p_drop->lport == info->lport)
        {
            CTC_BMP_SET(p_drop->u.reason_bmp, info->reason);
        }
    }
    return 0;
}

STATIC int32
_sys_usw_diag_get_drop_info(uint8 lchip, ctc_diag_drop_t* p_drop)
{
    uint32 cp_len = 0;
    uint32 loop = 0;
    uint32 clear_cnt = 0;
    sys_usw_diag_drop_pkt_node_t* pkt_node = NULL;
    ctc_slistnode_t* node = NULL;
    ctc_slistnode_t* next_node = NULL;
    sys_usw_diag_drop_info_t* info = NULL;


    SYS_DIAG_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Oper_type:%d buffer_clear:%d\n", p_drop->oper_type, p_drop->with_clear);

    if (p_drop->oper_type > CTC_DIAG_DROP_OPER_TYPE_GET_DETAIL_INFO)
    {
        SYS_DIAG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "Oper_type Invalid! \n");
        return CTC_E_INTR_INVALID_PARAM;
    }
    if (p_drop->oper_type != CTC_DIAG_DROP_OPER_TYPE_GET_PORT_BMP)
    {
        if ((p_drop->lport >= MCHIP_CAP(SYS_CAP_SPEC_MAX_PHY_PORT_NUM))
            && (p_drop->lport != SYS_DIAG_DROP_COUNT_SYSTEM))
        {
            SYS_DIAG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "Lport Invalid! \n");
            return CTC_E_INVALID_CONFIG;
        }
    }
    switch (p_drop->oper_type)
    {
        case CTC_DIAG_DROP_OPER_TYPE_GET_PORT_BMP:
        case CTC_DIAG_DROP_OPER_TYPE_GET_REASON_BMP:
            if (MCHIP_API(lchip)->diag_get_drop_info)
            {
                CTC_ERROR_RETURN(MCHIP_API(lchip)->diag_get_drop_info(lchip, (void*)p_drop));
            }
            ctc_hash_traverse(p_usw_diag_master[lchip]->drop_hash, (hash_traversal_fn)_sys_usw_diag_drop_hash_traverse_cb, p_drop);
            break;
        case CTC_DIAG_DROP_OPER_TYPE_GET_DETAIL_INFO:
            if (MCHIP_API(lchip)->diag_get_drop_info)
            {
                CTC_ERROR_RETURN(MCHIP_API(lchip)->diag_get_drop_info(lchip, (void*)p_drop));
            }
            info = _sys_usw_diag_drop_hash_lkup(lchip, p_drop->lport, p_drop->reason);
            if (NULL == info)
            {
                SYS_DIAG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "Buffer is not exist! lport=%d reason=%d \n", p_drop->lport, p_drop->reason);
                return CTC_E_NOT_EXIST;
            }

            p_drop->u.info.real_buffer_count = 0;
            p_drop->u.info.count = info->count;

            CTC_SLIST_LOOP_DEL(p_usw_diag_master[lchip]->drop_pkt_list, node, next_node)
            {
                pkt_node =  _ctc_container_of(node, sys_usw_diag_drop_pkt_node_t, head);
                if ((pkt_node->lport != p_drop->lport) || (pkt_node->reason != p_drop->reason))
                {
                    continue;
                }
                if ((0 == p_drop->u.info.buffer_count)
                    || (NULL == p_drop->u.info.buffer)
                    || (loop >= p_drop->u.info.buffer_count))
                {
                    loop++;
                    continue;
                }

                p_drop->u.info.buffer[loop].real_len = pkt_node->len;
                p_drop->u.info.buffer[loop].pkt_id = pkt_node->pkt_id;

                cp_len = (pkt_node->len > p_drop->u.info.buffer[loop].len) ?
                                            p_drop->u.info.buffer[loop].len : pkt_node->len;
                sal_memcpy(&p_drop->u.info.buffer[loop].timestamp, &pkt_node->timestamp, sizeof(sal_time_t));
                sal_memcpy(p_drop->u.info.buffer[loop].pkt_buf, pkt_node->buf, cp_len * sizeof(uint8));
                if (p_drop->with_clear
                    && (p_drop->u.info.buffer[loop].len >= pkt_node->len))
                {
                    ctc_slist_delete_node(p_usw_diag_master[lchip]->drop_pkt_list, node);
                    mem_free(pkt_node->buf);
                    mem_free(pkt_node);
                    clear_cnt++;
                }
                loop++;
            }
            p_drop->u.info.real_buffer_count = loop;
            if (p_drop->with_clear && (clear_cnt == loop))
            {
                /* clear hash data */
                _sys_usw_diag_drop_hash_remove(lchip, info);
            }
            break;
        default:
            break;
    }
    return CTC_E_NONE;
}

STATIC void
_sys_usw_diag_drop_pkt_report(uint8 lchip, ctc_pkt_rx_t* p_pkt_rx, uint32 pkt_id, sal_time_t timestamp)
{
    uint16 lport = 0;
    uint16 reason = 0;
    uint32 pkt_len = 0;
    uint32 hdr_len = 0;
    uint32 report_len = 0;
    ctc_diag_drop_pkt_report_cb report_cb;


    lport = p_pkt_rx->rx_info.lport;
    if (p_pkt_rx->rx_info.ttl & 0x40)
    {
        /*EPE*/
        reason = (p_pkt_rx->rx_info.ttl & 0x3F)  + SYS_DIAG_DROP_REASON_BASE_EPE;
    }
    else
    {
        /*IPE*/
        reason = p_pkt_rx->rx_info.ttl;
    }

    hdr_len = p_pkt_rx->eth_hdr_len + p_pkt_rx->pkt_hdr_len + p_pkt_rx->stk_hdr_len;
    pkt_len = p_pkt_rx->pkt_len - hdr_len - 4; /*without CRC Length (4 bytes)*/
    if (p_usw_diag_master[lchip]->drop_pkt_list)
    {
        report_len = (pkt_len > p_usw_diag_master[lchip]->drop_pkt_store_len) ? p_usw_diag_master[lchip]->drop_pkt_store_len : pkt_len;
    }
    else
    {
        report_len = pkt_len;
    }

    /* Packet Report */
    report_cb = p_usw_diag_master[lchip]->drop_pkt_report_cb;
    if (report_cb)
    {
        uint8 gchip = 0;
        int32 ret = CTC_E_NONE;
        ctc_diag_drop_t diag_drop;
        ctc_diag_drop_info_buffer_t pkt_buf;

        sal_memset(&diag_drop, 0, sizeof(diag_drop));
        sal_memset(&pkt_buf, 0, sizeof(pkt_buf));
        sys_usw_get_gchip_id(lchip, &gchip);
        pkt_buf.len = report_len;
        pkt_buf.real_len = report_len;
        pkt_buf.pkt_buf = p_pkt_rx->pkt_buf[0].data + hdr_len;
        pkt_buf.pkt_id = p_usw_diag_master[lchip]->drop_pkt_id;
        pkt_buf.timestamp = timestamp;
        diag_drop.lport = lport;
        diag_drop.reason = reason;
        diag_drop.oper_type = CTC_DIAG_DROP_OPER_TYPE_GET_DETAIL_INFO;
        if (MCHIP_API(lchip)->diag_get_drop_info)
        {
            MCHIP_API(lchip)->diag_get_drop_info(lchip, &diag_drop);
        }

        DIAG_UNLOCK;
        ret = report_cb(CTC_MAP_LPORT_TO_GPORT(gchip, lport), reason, diag_drop.u.info.reason_desc, &pkt_buf);
        DIAG_LOCK;
        if (ret < 0)
        {
            SYS_DIAG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "Drop Packet Report Error, error=%d!\n", ret);
        }
    }
}

STATIC int32
_sys_usw_diag_drop_pkt_sync(uint8 lchip, ctc_pkt_rx_t* p_pkt_rx)
{
    uint16 lport = 0;
    uint16 reason = 0;
    uint32 hash_seed = 0;
    uint32 pkt_len = 0;
    uint32 hdr_len = 0;
    uint32 store_len = 0;
    uint32 hash_calc_len = 0;
    sal_time_t timestamp;
    ctc_slistnode_t*       node = NULL;
    sys_usw_diag_drop_pkt_node_t* pkt_node = NULL;

    if (!p_pkt_rx->buf_count)
    {
        return CTC_E_NONE;
    }

    sal_time(&timestamp);
    if (NULL == p_usw_diag_master[lchip]->drop_pkt_list)
    {
        p_usw_diag_master[lchip]->drop_pkt_id++;
        if (p_usw_diag_master[lchip]->drop_pkt_id == 0)
        {
            p_usw_diag_master[lchip]->drop_pkt_id++;
        }
        /*Report Packet*/
        _sys_usw_diag_drop_pkt_report(lchip, p_pkt_rx, p_usw_diag_master[lchip]->drop_pkt_id, timestamp);
        return CTC_E_NONE;
    }

    lport = p_pkt_rx->rx_info.lport;
    if (p_pkt_rx->rx_info.ttl & 0x40)
    {
        /*EPE*/
        reason = (p_pkt_rx->rx_info.ttl & 0x3F)  + SYS_DIAG_DROP_REASON_BASE_EPE;
    }
    else
    {
        /*IPE*/
        reason = p_pkt_rx->rx_info.ttl;
    }

    hdr_len = p_pkt_rx->eth_hdr_len + p_pkt_rx->pkt_hdr_len + p_pkt_rx->stk_hdr_len;
    pkt_len = p_pkt_rx->pkt_len - hdr_len - 4; /*without CRC Length (4 bytes)*/
    store_len = (pkt_len > p_usw_diag_master[lchip]->drop_pkt_store_len) ? p_usw_diag_master[lchip]->drop_pkt_store_len : pkt_len;

    /* hash key : packet */
    hash_calc_len = (store_len > p_usw_diag_master[lchip]->drop_pkt_hash_len) ? p_usw_diag_master[lchip]->drop_pkt_hash_len : store_len;
    hash_seed = ctc_hash_caculate(hash_calc_len, (void*)(p_pkt_rx->pkt_buf[0].data + hdr_len));
    CTC_SLIST_LOOP(p_usw_diag_master[lchip]->drop_pkt_list, node)
    {
        pkt_node = (sys_usw_diag_drop_pkt_node_t*)node;
        if ((hash_seed == pkt_node->hash_seed)
            && (store_len == pkt_node->len)
            && (lport == pkt_node->lport)
            && (reason == pkt_node->reason))
        {
            /*If the same drop packet time Interval >= 10min, report again*/
            if ((timestamp - pkt_node->old_timestamp) >= 10*60)
            {
                _sys_usw_diag_drop_pkt_report(lchip, p_pkt_rx, pkt_node->pkt_id, timestamp);
                /*Update Timestamp*/
                pkt_node->old_timestamp = timestamp;
            }
            pkt_node->timestamp = timestamp;
            /* means find the same packet */
            return CTC_E_NONE;
        }
    }

    p_usw_diag_master[lchip]->drop_pkt_id++;
    if (p_usw_diag_master[lchip]->drop_pkt_id == 0)
    {
        p_usw_diag_master[lchip]->drop_pkt_id++;
    }
    /*Drop Report, New Report*/
    _sys_usw_diag_drop_pkt_report(lchip, p_pkt_rx, p_usw_diag_master[lchip]->drop_pkt_id, timestamp);

    if ((p_usw_diag_master[lchip]->drop_pkt_list->count >= p_usw_diag_master[lchip]->drop_pkt_store_cnt)
        && (!p_usw_diag_master[lchip]->drop_pkt_overwrite))
    {
        /* buffer exceed, and would not cover the oldest buffer */
        return CTC_E_NONE;
    }
    pkt_node = mem_malloc(MEM_DIAG_MODULE, sizeof(sys_usw_diag_drop_pkt_node_t));
    if (NULL == pkt_node)
    {
        SYS_DIAG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "Drop Packet Sync Error, No Memory!\n");
        return CTC_E_NO_MEMORY;
    }
    sal_memset(pkt_node, 0, sizeof(sys_usw_diag_drop_pkt_node_t));
    pkt_node->hash_seed = hash_seed;
    pkt_node->lport = lport;
    pkt_node->reason = reason;
    pkt_node->len = store_len;
    pkt_node->pkt_id = p_usw_diag_master[lchip]->drop_pkt_id;
    pkt_node->timestamp = timestamp;
    pkt_node->old_timestamp = timestamp;
    pkt_node->buf = mem_malloc(MEM_DIAG_MODULE, store_len);
    if (NULL == pkt_node->buf)
    {
        mem_free(pkt_node);
        SYS_DIAG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "Drop Packet Sync Error, No Memory!\n");
        return CTC_E_NO_MEMORY;
    }
    sal_memcpy(pkt_node->buf, p_pkt_rx->pkt_buf[0].data + hdr_len, store_len);
    if (p_usw_diag_master[lchip]->drop_pkt_list->count >= p_usw_diag_master[lchip]->drop_pkt_store_cnt)
    {
        sys_usw_diag_drop_pkt_node_t* node_temp = NULL;
        node = p_usw_diag_master[lchip]->drop_pkt_list->head;
        node_temp = (sys_usw_diag_drop_pkt_node_t*)node;
        /* buffer exceed, and cover the oldest buffer */
        ctc_slist_delete_node(p_usw_diag_master[lchip]->drop_pkt_list, node);
        /* free hash data */
        mem_free(node_temp->buf);
        mem_free(node_temp);
    }
    ctc_slist_add_tail(p_usw_diag_master[lchip]->drop_pkt_list, &pkt_node->head);

    return CTC_E_NONE;
}

STATIC int32
_sys_usw_diag_drop_pkt_redirect_tocpu_en(uint8 lchip, uint8 enable)
{
    ctc_qos_shape_t shape;
    ctc_qos_queue_cfg_t que_cfg;
    uint32 value = 0;
    uint32 cmd = 0;

    sal_memset(&que_cfg, 0, sizeof(ctc_qos_queue_cfg_t));
    sal_memset(&shape, 0, sizeof(ctc_qos_shape_t));

    if (enable)
    {
        /*IPE*/
        value = 0x1c;/* all discard*/
        cmd = DRV_IOW(IpeFwdCtl_t, IpeFwdCtl_discardPacketLogType_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
        value = 1;
        cmd = DRV_IOW(IpeFwdCtl_t, IpeFwdCtl_discardPacketLogEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
        /*EPE*/
        value = 1;/* all discard, Tsingma = 1 */
        cmd = DRV_IOW(EpeHeaderEditCtl_t, EpeHeaderEditCtl_discardPacketLogType_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
        value = 1;
        cmd = DRV_IOW(EpeHeaderEditCtl_t, EpeHeaderEditCtl_discardPacketLogEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
        value = 1;
        cmd = DRV_IOW(IpeFwdCtl_t, IpeFwdCtl_dropPktLogWithDiscardType_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
        cmd = DRV_IOW(EpeHeaderEditCtl_t, EpeHeaderEditCtl_dropPktLogWithDiscardType_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
        cmd = DRV_IOR(EpeHeaderEditCtl_t, EpeHeaderEditCtl_logOnDiscard_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
        /* Set discardPacketLogEn */
        CTC_BIT_SET(value, 8);
        cmd = DRV_IOW(EpeHeaderEditCtl_t, EpeHeaderEditCtl_logOnDiscard_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));

        /*CTC_PKT_CPU_REASON_DIAG_DISCARD_PKT dest-to Local-CPU*/
        que_cfg.type = CTC_QOS_QUEUE_CFG_QUEUE_REASON_DEST;
        que_cfg.value.reason_dest.cpu_reason = CTC_PKT_CPU_REASON_DIAG_DISCARD_PKT;
        que_cfg.value.reason_dest.dest_type = CTC_PKT_CPU_REASON_TO_LOCAL_CPU;
        CTC_ERROR_RETURN(sys_usw_qos_set_queue(lchip, &que_cfg));

        /* Shape */
        shape.type = CTC_QOS_SHAPE_QUEUE;
        shape.shape.queue_shape.enable = 1;
        shape.shape.queue_shape.queue.queue_type = CTC_QUEUE_TYPE_EXCP_CPU;
        shape.shape.queue_shape.pir = 1000;/* 1M */
        shape.shape.queue_shape.pbs = 0xFFFFFFFF;
        shape.shape.queue_shape.queue.cpu_reason = CTC_PKT_CPU_REASON_DIAG_DISCARD_PKT;
        CTC_ERROR_RETURN(sys_usw_qos_set_shape(lchip, &shape));
    }
    else
    {
        /*IPE*/
        cmd = DRV_IOW(IpeFwdCtl_t, IpeFwdCtl_discardPacketLogType_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
        cmd = DRV_IOW(IpeFwdCtl_t, IpeFwdCtl_discardPacketLogEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
        /*EPE*/
        cmd = DRV_IOW(EpeHeaderEditCtl_t, EpeHeaderEditCtl_discardPacketLogType_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
        cmd = DRV_IOW(EpeHeaderEditCtl_t, EpeHeaderEditCtl_discardPacketLogEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
        cmd = DRV_IOW(IpeFwdCtl_t, IpeFwdCtl_dropPktLogWithDiscardType_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
        cmd = DRV_IOW(EpeHeaderEditCtl_t, EpeHeaderEditCtl_dropPktLogWithDiscardType_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
        cmd = DRV_IOR(EpeHeaderEditCtl_t, EpeHeaderEditCtl_logOnDiscard_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
        /* Set discardPacketLogEn */
        CTC_BIT_UNSET(value, 8);
        cmd = DRV_IOW(EpeHeaderEditCtl_t, EpeHeaderEditCtl_logOnDiscard_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));

        shape.type = CTC_QOS_SHAPE_QUEUE;
        shape.shape.queue_shape.enable = 0;
        shape.shape.queue_shape.queue.queue_type = CTC_QUEUE_TYPE_EXCP_CPU;
        shape.shape.queue_shape.queue.cpu_reason = CTC_PKT_CPU_REASON_DIAG_DISCARD_PKT;
        CTC_ERROR_RETURN(sys_usw_qos_set_shape(lchip, &shape));

        que_cfg.type = CTC_QOS_QUEUE_CFG_QUEUE_REASON_DEST;
        que_cfg.value.reason_dest.cpu_reason = CTC_PKT_CPU_REASON_DIAG_DISCARD_PKT;
        que_cfg.value.reason_dest.dest_type = CTC_PKT_CPU_REASON_TO_DROP;
        CTC_ERROR_RETURN(sys_usw_qos_set_queue(lchip, &que_cfg));
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_usw_diag_drop_pkt_set_config(uint8 lchip, ctc_diag_drop_pkt_config_t* p_cfg)
{
    uint8 enable = 0;
    uint8 disable = 0;

    if (p_cfg->flags & CTC_DIAG_DROP_PKT_HASH_CALC_LEN)
    {
        p_usw_diag_master[lchip]->drop_pkt_hash_len = p_cfg->hash_calc_len;
    }
    if (p_cfg->flags & CTC_DIAG_DROP_PKT_STORE_LEN)
    {
        p_usw_diag_master[lchip]->drop_pkt_store_len = p_cfg->storage_len;
    }
    if (p_cfg->flags & CTC_DIAG_DROP_PKT_STORE_CNT)
    {
        p_usw_diag_master[lchip]->drop_pkt_store_cnt = p_cfg->storage_cnt;
    }
    if (p_cfg->flags & CTC_DIAG_DROP_PKT_OVERWRITE)
    {
        p_usw_diag_master[lchip]->drop_pkt_overwrite = p_cfg->overwrite ? 1 : 0;
    }
    if (p_cfg->flags & CTC_DIAG_DROP_PKT_CLEAR)
    {
        _sys_usw_diag_drop_pkt_clear_buf(lchip);
    }

    if (p_cfg->flags & CTC_DIAG_DROP_PKT_STORE_EN)
    {
        if (p_cfg->storage_en)
        {
            if (p_usw_diag_master[lchip]->drop_pkt_list)
            {
               goto report;
            }
            p_usw_diag_master[lchip]->drop_pkt_list = ctc_slist_new();
            if (NULL == p_usw_diag_master[lchip]->drop_pkt_list)
            {
                SYS_DIAG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "No Memory! \n");
                return CTC_E_NO_MEMORY;
            }
            if (p_usw_diag_master[lchip]->drop_pkt_report_cb == NULL)
            {
                int32 ret = 0;
                ret = _sys_usw_diag_drop_pkt_redirect_tocpu_en(lchip, 1);
                if (ret < 0)
                {
                    ctc_slist_free(p_usw_diag_master[lchip]->drop_pkt_list);
                }
                enable = 1;
            }
        }
        else if (p_usw_diag_master[lchip]->drop_pkt_list)
        {
            _sys_usw_diag_drop_pkt_clear_buf(lchip);
            ctc_slist_free(p_usw_diag_master[lchip]->drop_pkt_list);
            p_usw_diag_master[lchip]->drop_pkt_list = NULL;
            if (p_usw_diag_master[lchip]->drop_pkt_report_cb == NULL)
            {
                CTC_ERROR_RETURN(_sys_usw_diag_drop_pkt_redirect_tocpu_en(lchip, 0));
                disable = 1;
            }
        }
    }

report:
    if (p_cfg->flags & CTC_DIAG_DROP_PKT_REPORT_CALLBACK)
    {
        if (p_usw_diag_master[lchip]->drop_pkt_list == NULL)
        {
            if (p_cfg->report_cb && !enable)
            {
                CTC_ERROR_RETURN(_sys_usw_diag_drop_pkt_redirect_tocpu_en(lchip, 1));
            }
            else if (!p_cfg->report_cb && !disable && p_usw_diag_master[lchip]->drop_pkt_report_cb)
            {
                CTC_ERROR_RETURN(_sys_usw_diag_drop_pkt_redirect_tocpu_en(lchip, 0));
            }
        }
        p_usw_diag_master[lchip]->drop_pkt_report_cb = p_cfg->report_cb;
    }
    return CTC_E_NONE;
}

STATIC int32
_sys_usw_diag_drop_pkt_get_config(uint8 lchip, ctc_diag_drop_pkt_config_t* p_cfg)
{
    p_cfg->hash_calc_len = p_usw_diag_master[lchip]->drop_pkt_hash_len;
    CTC_SET_FLAG(p_cfg->flags, CTC_DIAG_DROP_PKT_HASH_CALC_LEN);
    p_cfg->storage_len = p_usw_diag_master[lchip]->drop_pkt_store_len;
    CTC_SET_FLAG(p_cfg->flags, CTC_DIAG_DROP_PKT_STORE_LEN);
    p_cfg->storage_cnt = p_usw_diag_master[lchip]->drop_pkt_store_cnt;
    CTC_SET_FLAG(p_cfg->flags, CTC_DIAG_DROP_PKT_STORE_CNT);
    p_cfg->overwrite = p_usw_diag_master[lchip]->drop_pkt_overwrite ? TRUE : FALSE;
    CTC_SET_FLAG(p_cfg->flags, CTC_DIAG_DROP_PKT_OVERWRITE);
    p_cfg->storage_en = p_usw_diag_master[lchip]->drop_pkt_list ? TRUE : FALSE;
    CTC_SET_FLAG(p_cfg->flags, CTC_DIAG_DROP_PKT_STORE_EN);
    p_cfg->report_cb = p_usw_diag_master[lchip]->drop_pkt_report_cb;
    CTC_SET_FLAG(p_cfg->flags, CTC_DIAG_DROP_PKT_REPORT_CALLBACK);
    return CTC_E_NONE;
}

STATIC int32
_sys_usw_diag_set_property(uint8 lchip,ctc_diag_property_t prop, void* p_value)
{
    switch (prop)
    {
        case CTC_DIAG_PROP_DROP_PKT_CONFIG:
            CTC_ERROR_RETURN(_sys_usw_diag_drop_pkt_set_config(lchip, (ctc_diag_drop_pkt_config_t*)p_value));
        default:
            break;
    }
    return CTC_E_NONE;
}

STATIC int32
_sys_usw_diag_get_property(uint8 lchip,ctc_diag_property_t prop, void* p_value)
{
    switch (prop)
    {
        case CTC_DIAG_PROP_DROP_PKT_CONFIG:
            CTC_ERROR_RETURN(_sys_usw_diag_drop_pkt_get_config(lchip, (ctc_diag_drop_pkt_config_t*)p_value));
        default:
            break;
    }
    return CTC_E_NONE;
}

STATIC int32
_sys_usw_diag_drop_hash_traverse_dump(sys_usw_diag_drop_info_t* info, sys_traverse_t*  p_para)
{
    CTC_PTR_VALID_CHECK(p_para)
    CTC_PTR_VALID_CHECK(info);
    SYS_DUMP_DB_LOG((sal_file_t)p_para->data, "%.4d   %.4d    %10d\n", info->lport, info->reason, info->count);
    return 0;
}

STATIC int32
_sys_usw_diag_dump_db(uint8 lchip, sal_file_t p_f,ctc_global_dump_db_t* p_dump_param)
{
    sys_traverse_t  para;

    SYS_USW_DIAG_INIT_CHECK(lchip);
    DIAG_LOCK;
    SYS_DUMP_DB_LOG(p_f, "%s\n", "# DIAG");
    SYS_DUMP_DB_LOG(p_f, "%s\n", "{");
    SYS_DUMP_DB_LOG(p_f, "%s\n", "Master config:");
    SYS_DUMP_DB_LOG(p_f, "%s\n", "----------------------------------------------------------------");
    SYS_DUMP_DB_LOG(p_f, "%-36s: %s\n","Storage drop packet", p_usw_diag_master[lchip]->drop_pkt_list ? "Enable" : "Disable");
    SYS_DUMP_DB_LOG(p_f, "%-36s: %s\n","Report drop packet", p_usw_diag_master[lchip]->drop_pkt_report_cb ? "Enable" : "Disable");
    SYS_DUMP_DB_LOG(p_f, "%-36s: %s\n","Cover exceed", p_usw_diag_master[lchip]->drop_pkt_overwrite ? "Enable" : "Disable");
    SYS_DUMP_DB_LOG(p_f, "%-36s: %u\n","Hash calc length", p_usw_diag_master[lchip]->drop_pkt_hash_len);
    SYS_DUMP_DB_LOG(p_f, "%-36s: %u\n","Storage packet count", p_usw_diag_master[lchip]->drop_pkt_store_cnt);
    SYS_DUMP_DB_LOG(p_f, "%-36s: %u\n","Storage packet length", p_usw_diag_master[lchip]->drop_pkt_store_len);
    SYS_DUMP_DB_LOG(p_f, "%s\n", "----------------------------------------------------------------");

    if (p_usw_diag_master[lchip]->drop_hash->count)
    {
        SYS_DUMP_DB_LOG(p_f, "\n");
        SYS_DUMP_DB_LOG(p_f, "%s\n", "##Drop Statistics");
        SYS_DUMP_DB_LOG(p_f, "%-5s  %-6s  %-10s\n", "Lport", "Reason", "Statistics");
        SYS_DUMP_DB_LOG(p_f, "%s\n", "----------------------------");
        sal_memset(&para, 0, sizeof(sys_traverse_t));
        para.data = (void*)p_f;
        ctc_hash_traverse(p_usw_diag_master[lchip]->drop_hash, (hash_traversal_fn)_sys_usw_diag_drop_hash_traverse_dump, &para);
        SYS_DUMP_DB_LOG(p_f, "%s\n", "----------------------------");
    }
    if (p_usw_diag_master[lchip]->drop_pkt_list && p_usw_diag_master[lchip]->drop_pkt_list->count)
    {
        uint32 loop = 0;
        uint32 index = 0;
        ctc_slistnode_t* node = NULL;
        sys_usw_diag_drop_pkt_node_t* pkt_node = NULL;
        SYS_DUMP_DB_LOG(p_f, "\n");
        SYS_DUMP_DB_LOG(p_f, "%s\n", "##Drop Packet");
        SYS_DUMP_DB_LOG(p_f, "%s\n", "------------------------------------------------------");
        CTC_SLIST_LOOP(p_usw_diag_master[lchip]->drop_pkt_list, node)
        {
            pkt_node = (sys_usw_diag_drop_pkt_node_t*)node;
            SYS_DUMP_DB_LOG(p_f,"%-20s:%d\n", "Index", loop);
            SYS_DUMP_DB_LOG(p_f,"%-20s:%d\n", "Lport", pkt_node->lport);
            SYS_DUMP_DB_LOG(p_f,"%-20s:%d\n", "Reason", pkt_node->reason);
            SYS_DUMP_DB_LOG(p_f,"%-20s:0x%x\n", "Hashseed", pkt_node->hash_seed);
            SYS_DUMP_DB_LOG(p_f,"%-20s:%s", "Timestamp", sal_ctime(&pkt_node->timestamp));
            SYS_DUMP_DB_LOG(p_f,"%-20s:%s", "Old_Timestamp", sal_ctime(&pkt_node->old_timestamp));
            SYS_DUMP_DB_LOG(p_f, "%s\n", "-------------------------------------------------");
            for (index = 0; index < pkt_node->len; index++)
            {
                if (index % 16 == 0)
                {
                    SYS_DUMP_DB_LOG(p_f, "0x%.4x:  ", index / 16 * 16);
                }
                SYS_DUMP_DB_LOG(p_f, "%.2x", pkt_node->buf[index]);
                if (index % 2)
                {
                    SYS_DUMP_DB_LOG(p_f," ");
                }
                if ((index + 1) % 16 == 0)
                {
                    SYS_DUMP_DB_LOG(p_f,"\n");
                }
            }
            SYS_DUMP_DB_LOG(p_f, "\n\n");
            loop++;
        }
    }
    DIAG_UNLOCK;
    SYS_DUMP_DB_LOG(p_f, "%s\n", "}");
    return CTC_E_NONE;
}

STATIC int32
_sys_usw_diag_read_tbl(uint8 lchip, ctc_diag_tbl_t *p_para)
{
    char* tbl_name = NULL;
    uint32 tbl_id = 0;
    int32 ret = CTC_E_NONE;
    uint32 cmd = 0;
    tbl_entry_t entry;
    uint32* data_entry = NULL;
    uint32* mask_entry = NULL;
    int32 i = 0;
    uint32 index = 0;
    uint32 index_temp = 0;
    uint32 is_half = 0;
    fields_t* fld_ptr = NULL;
    tables_info_t* tbl_ptr = NULL;
    uint32 entry_num = 0;
    uint32 field_id = 0;
    uint32 loop = 0;
    char fld_name[CTC_DIAG_TBL_NAME_SIZE] = {0};

    data_entry = (uint32*)sal_malloc(MAX_ENTRY_WORD * sizeof(uint32));
    if(NULL == data_entry)
    {
        SYS_DIAG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "%% No memory\n");
        return CTC_E_NO_MEMORY;
    }

    mask_entry = (uint32*)sal_malloc(MAX_ENTRY_WORD * sizeof(uint32));
    if(NULL == mask_entry)
    {
        SYS_DIAG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "%% No memory\n");
        sal_free(data_entry);
        return CTC_E_NO_MEMORY;
    }
    sal_memset(data_entry, 0, MAX_ENTRY_WORD * sizeof(uint32));
    sal_memset(mask_entry, 0, MAX_ENTRY_WORD * sizeof(uint32));
    tbl_name = (char*)p_para->tbl_str;

    if (drv_usw_get_tbl_id_by_string(lchip, &tbl_id, tbl_name)
        || (0 == TABLE_MAX_INDEX(lchip, tbl_id) && ((DsL2Edit3WOuter_t != tbl_id && DsL3Edit3W3rd_t != tbl_id))))
    {
        SYS_DIAG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR,"%% Not found %s\n", tbl_name);
        ret = CTC_E_NOT_EXIST;
        goto clean;
    }
    if(p_para->index >= TABLE_MAX_INDEX(lchip, tbl_id))
    {
        SYS_DIAG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR,"%% the index %d exceed max index %d\n", p_para->index,TABLE_MAX_INDEX(lchip, tbl_id));
        ret = CTC_E_INVALID_PARAM;
        goto clean;
    }
    tbl_ptr = TABLE_INFO_PTR(lchip, tbl_id);
    entry_num = tbl_ptr->field_num;
    loop = (tbl_ptr->field_num > p_para->entry_num)? p_para->entry_num: tbl_ptr->field_num;
    for (i = 0; i < loop; i++)
    {
        index = p_para->index;
        if(sal_strlen(p_para->info[i].str) == 0)
        {
            fld_ptr = &(tbl_ptr->ptr_fields[i]);
            sal_strncpy(fld_name, fld_ptr->ptr_field_name, CTC_DIAG_TBL_NAME_SIZE);
            sal_strncpy(p_para->info[i].str, fld_ptr->ptr_field_name, CTC_DIAG_TBL_NAME_SIZE);
        }
        else
        {
            sal_strncpy(fld_name, p_para->info[i].str, CTC_DIAG_TBL_NAME_SIZE);
        }
        CTC_ERROR_GOTO(drv_usw_get_field_id_by_string(lchip, tbl_id, &field_id, fld_name),ret,clean);
        fld_ptr = _drv_find_field(lchip,tbl_id, field_id);
        p_para->info[i].field_len = fld_ptr->bits;
        /* only support 128bit field*/
        if(fld_ptr->bits > 128)
        {
            continue;
        }
        if ((DRV_TABLE_TYPE_TCAM == drv_usw_get_table_type(lchip, tbl_id))
        || (DRV_TABLE_TYPE_LPM_TCAM_IP == drv_usw_get_table_type(lchip, tbl_id))
        || (DRV_TABLE_TYPE_LPM_TCAM_NAT == drv_usw_get_table_type(lchip, tbl_id))
        || (DRV_TABLE_TYPE_STATIC_TCAM_KEY == drv_usw_get_table_type(lchip, tbl_id)))
        {
            entry.data_entry = data_entry;
            entry.mask_entry = mask_entry;
            cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
            if((DRV_TABLE_TYPE_LPM_TCAM_IP == drv_usw_get_table_type(lchip, tbl_id)) ||
                (DRV_TABLE_TYPE_LPM_TCAM_NAT == drv_usw_get_table_type(lchip, tbl_id)))
            {
                _sys_usw_diag_tcam_io(lchip, index, cmd, &entry);
            }
            else
            {
                DRV_IOCTL(lchip, index, cmd, &entry);
            }
            drv_get_field(lchip, tbl_id, field_id, data_entry, p_para->info[i].value);
            drv_get_field(lchip, tbl_id, field_id, mask_entry, p_para->info[i].mask);
        }
        else
        {
            if (_sys_usw_diag_table_is_hash_key(lchip, tbl_id))
            {
                ret =  _sys_usw_diag_table_acc_read(lchip, tbl_id, index, (void*)(data_entry));/*merge table and cam*/
            }
            else
            {
                uint32 tbl_id_tmp = tbl_id;

                if (DsFwdHalf_t == tbl_id_tmp)
                {
                    tbl_id_tmp = DsFwd_t;
                }

                if ((DsIpfixSessionRecord_t == tbl_id_tmp) || (DsFwd_t == tbl_id_tmp) )
                {
                    index_temp = index / 2;
                }
                else
                {
                    index_temp = index;
                }

                if (DsL2Edit3WOuter_t == tbl_id)
                {
                    tbl_id_tmp = DsL2Edit6WOuter_t;
                    index_temp = index/2;
                }

                if (DsL3Edit3W3rd_t == tbl_id)
                {
                    tbl_id_tmp = DsL3Edit6W3rd_t;
                    index_temp = index/2;
                }

                cmd = DRV_IOR(tbl_id_tmp, DRV_ENTRY_FLAG);
                ret = DRV_IOCTL(lchip, index_temp, cmd, data_entry);
            }
            if (CTC_E_NONE == ret)
            {
                if (DsFwd_t == tbl_id)
                {
                    drv_get_field(lchip, DsFwd_t, DsFwd_isHalf_f, data_entry, (uint32*)&is_half);
                }
                if ((DsFwdHalf_t == tbl_id) || ((DsFwd_t == tbl_id)&&is_half))
                {
                    DsFwdHalf_m dsfwd_half;

                    if (index % 2 == 0)
                    {
                        drv_get_field(lchip, DsFwdDualHalf_t, DsFwdDualHalf_g_0_dsFwdHalf_f,   data_entry,   (uint32*)&dsfwd_half);
                    }
                    else
                    {
                        drv_get_field(lchip, DsFwdDualHalf_t, DsFwdDualHalf_g_1_dsFwdHalf_f,   data_entry,   (uint32*)&dsfwd_half);
                    }

                    sal_memcpy(data_entry, &dsfwd_half, sizeof(dsfwd_half));
                }
                drv_get_field(lchip, tbl_id, field_id, data_entry, p_para->info[i].value);
            }
        }
    }
    p_para->entry_num = entry_num;
    if (ret < 0)
    {
        SYS_DIAG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "%% Read index %d failed %d\n", index, ret);
    }
clean:
    sal_free(data_entry);
    sal_free(mask_entry);

    return ret;
}

STATIC int32
_sys_usw_diag_write_tbl(uint8 lchip, ctc_diag_tbl_t *p_para)
{
    char* reg_name = NULL;
    int32 index = 0;
    uint32 index_temp = 0;
    uint32 reg_tbl_id = 0;
    uint32 input_tbl_id = 0;
    uint32 tmp_reg_tbl_id = 0;
    uint32 field_value = 0;
    uint32* mask = NULL;
    uint32* value = NULL;
    uint32 cmd = 0;
    int32 ret = 0;
    fld_id_t fld_id = 0;
    tbl_entry_t field_entry;
    DsL2EditEth3W_m ds_l2_edit_eth3_w;
    DsL2Edit6WOuter_m ds_l2_edit6_w_outer;
    DsL3Edit3W3rd_m ds_mpls_3w;
    DsL3Edit6W3rd_m ds_mpls_6w;
    DsFwdHalf_m dsfwd_half;
    uint32 is_half = 0;
    uint32 loop = 0;

    mask = (uint32*)sal_malloc(MAX_ENTRY_WORD * sizeof(uint32));
    value = (uint32*)sal_malloc(MAX_ENTRY_WORD * sizeof(uint32));

    if (!mask || !value)
    {
        ret = CTC_E_NO_MEMORY;
        goto WRITE_TABLE_ERROR;
    }

    sal_memset(mask, 0, MAX_ENTRY_WORD * sizeof(uint32));
    sal_memset(value, 0, MAX_ENTRY_WORD * sizeof(uint32));

    index_temp = p_para->index;
    reg_name = (char*)p_para->tbl_str;

    if (DRV_E_NONE != drv_usw_get_tbl_id_by_string(lchip, &reg_tbl_id, reg_name)
        || (0 == TABLE_MAX_INDEX(lchip, reg_tbl_id) && ((DsL2Edit3WOuter_t != reg_tbl_id && DsL3Edit3W3rd_t != reg_tbl_id))))
    {
        SYS_DIAG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR,"%% Not found %s\n", reg_name);
        ret = CTC_E_NOT_EXIST;
        goto WRITE_TABLE_ERROR;
    }

    if(p_para->index >= TABLE_MAX_INDEX(lchip, reg_tbl_id))
    {
        SYS_DIAG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR,"%% the index %d exceed max index %d\n", p_para->index,TABLE_MAX_INDEX(lchip, reg_tbl_id));
        ret = CTC_E_INVALID_PARAM;
        goto WRITE_TABLE_ERROR;
    }

    if ((reg_tbl_id) <= 0 || (reg_tbl_id) >= MaxTblId_t)
    {
        ret = CTC_E_NOT_EXIST;
        goto WRITE_TABLE_ERROR;
    }


    if (DsL2Edit3WOuter_t == reg_tbl_id)
    {
        tmp_reg_tbl_id = DsL2EditEth3W_t;
    }
    else if (DsL3Edit3W3rd_t == reg_tbl_id)
    {
        tmp_reg_tbl_id = DsL3EditMpls3W_t;
    }
    else
    {
        tmp_reg_tbl_id = reg_tbl_id;
    }
    if ((DsIpfixSessionRecord_t == reg_tbl_id))
    {
        index = index_temp / 2;
    }
    else if ((DsFwd_t == reg_tbl_id) || (DsFwdHalf_t == reg_tbl_id))
    {
        index = index_temp / 2;
        reg_tbl_id = DsFwd_t;
    }
    else if (DsL2Edit3WOuter_t == reg_tbl_id)
    {
        index = index_temp / 2;
        reg_tbl_id = DsL2Edit6WOuter_t;
        input_tbl_id = DsL2Edit3WOuter_t;
    }
    else if (DsL3Edit3W3rd_t == reg_tbl_id)
    {
        index = index_temp / 2;
        reg_tbl_id = DsL3Edit6W3rd_t;
        input_tbl_id = DsL3Edit3W3rd_t;
    }
    else
    {
        index = index_temp;
    }
    /*1.read firstly*/
    DRV_TBL_EMPTY_CHECK(0, reg_tbl_id);

    cmd = DRV_IOR(reg_tbl_id, DRV_ENTRY_FLAG);
    if (DRV_TABLE_TYPE_TCAM == drv_usw_get_table_type(lchip, reg_tbl_id)
        || DRV_TABLE_TYPE_LPM_TCAM_IP == drv_usw_get_table_type(lchip, reg_tbl_id)
        || DRV_TABLE_TYPE_STATIC_TCAM_KEY == drv_usw_get_table_type(lchip, reg_tbl_id)
        || DRV_TABLE_TYPE_LPM_TCAM_NAT == drv_usw_get_table_type(lchip, reg_tbl_id))
    {
        field_entry.data_entry = value;
        field_entry.mask_entry = mask;
        if((DRV_TABLE_TYPE_LPM_TCAM_IP == drv_usw_get_table_type(lchip, reg_tbl_id)) ||
                (DRV_TABLE_TYPE_LPM_TCAM_NAT == drv_usw_get_table_type(lchip, reg_tbl_id)))
        {
            ret = _sys_usw_diag_tcam_io(lchip, index, cmd, &field_entry);
        }
        else
        {
            ret = DRV_IOCTL(lchip, index, cmd, &field_entry);
        }
    }
    else
    {
        if (_sys_usw_diag_table_is_hash_key(lchip, reg_tbl_id))
        {
            _sys_usw_diag_table_acc_read(lchip, reg_tbl_id, index_temp, (void*)(value));
        }
        else
        {
            ret = DRV_IOCTL(lchip, index, cmd, value);
            if (DsFwd_t == reg_tbl_id)
            {
                drv_get_field(lchip, DsFwd_t, DsFwd_isHalf_f, value, (uint32*)&is_half);
            }
            if (DsL2Edit3WOuter_t == input_tbl_id)
            {
                sal_memset(&ds_l2_edit_eth3_w, 0, sizeof(ds_l2_edit_eth3_w));
                sal_memset(&ds_l2_edit6_w_outer, 0, sizeof(ds_l2_edit6_w_outer));
                sal_memcpy((uint8*)&ds_l2_edit6_w_outer, (uint8*)value, sizeof(ds_l2_edit6_w_outer));
                if ((index_temp&0x01))
                {
                    sal_memcpy((uint8*)&ds_l2_edit_eth3_w, (uint8*)&ds_l2_edit6_w_outer + sizeof(ds_l2_edit_eth3_w), sizeof(ds_l2_edit_eth3_w));
                }
                else
                {
                    sal_memcpy((uint8*)&ds_l2_edit_eth3_w, (uint8*)&ds_l2_edit6_w_outer, sizeof(ds_l2_edit_eth3_w));
                }
            }
            else if (DsL3Edit3W3rd_t == input_tbl_id)
            {
                sal_memset(&ds_mpls_3w, 0, sizeof(ds_mpls_3w));
                sal_memset(&ds_mpls_6w, 0, sizeof(ds_mpls_6w));
                sal_memcpy((uint8*)&ds_mpls_6w, (uint8*)value, sizeof(ds_mpls_6w));
                if ((index_temp&0x01))
                {
                    sal_memcpy((uint8*)&ds_mpls_3w, (uint8*)&ds_mpls_6w + sizeof(ds_mpls_3w), sizeof(ds_mpls_3w));
                }
                else
                {
                    sal_memcpy((uint8*)&ds_mpls_3w, (uint8*)&ds_mpls_6w, sizeof(ds_mpls_3w));
                }
            }
            else if ((DsFwd_t == reg_tbl_id) && is_half)
            {
                if (index_temp % 2 == 0)
                {
                    drv_get_field(lchip, DsFwdDualHalf_t, DsFwdDualHalf_g_0_dsFwdHalf_f, value, (uint32*)&dsfwd_half);
                }
                else
                {
                    drv_get_field(lchip, DsFwdDualHalf_t, DsFwdDualHalf_g_1_dsFwdHalf_f, value, (uint32*)&dsfwd_half);
                }
            }
        }
    }
    /*2.write secondly*/
    for(loop = 0; loop < p_para->entry_num; loop ++)
    {
        if (CTC_E_NONE != drv_usw_get_field_id_by_string(lchip, tmp_reg_tbl_id, &fld_id, p_para->info[loop].str))
        {
            SYS_DIAG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR,"%% Not found %s\n", p_para->info[loop].str);
            continue;
        }

        SYS_DIAG_DBG_OUT(CTC_DEBUG_LEVEL_INFO,"Write %s %d %s\n", reg_name, index_temp, p_para->info[loop].str);
        cmd = DRV_IOW(reg_tbl_id, DRV_ENTRY_FLAG);
        if ((DRV_TABLE_TYPE_TCAM == drv_usw_get_table_type(lchip, reg_tbl_id)
            || DRV_TABLE_TYPE_LPM_TCAM_IP == drv_usw_get_table_type(lchip, reg_tbl_id)
            || DRV_TABLE_TYPE_LPM_TCAM_NAT == drv_usw_get_table_type(lchip, reg_tbl_id)
            || DRV_TABLE_TYPE_STATIC_TCAM_KEY == drv_usw_get_table_type(lchip, reg_tbl_id)))
        {
            drv_set_field(lchip, reg_tbl_id, fld_id, field_entry.data_entry, p_para->info[loop].value);
            drv_set_field(lchip, reg_tbl_id, fld_id, field_entry.mask_entry, p_para->info[loop].mask);
            if((DRV_TABLE_TYPE_LPM_TCAM_IP == drv_usw_get_table_type(lchip, reg_tbl_id)) ||
                    (DRV_TABLE_TYPE_LPM_TCAM_NAT == drv_usw_get_table_type(lchip, reg_tbl_id)))
            {
                ret = _sys_usw_diag_tcam_io(lchip, index, cmd, &field_entry);
            }
            else
            {
                ret = DRV_IOCTL(lchip, index, cmd, &field_entry);
            }
        }
        else
        {
            if (DsL2Edit3WOuter_t == input_tbl_id)
            {
                input_tbl_id = DsL2EditEth3W_t; /*drv only parse DsL2EditEth3W_t, other than DsL2Edit3WOuter_t*/
                drv_set_field(lchip, input_tbl_id, fld_id, (uint8*)&ds_l2_edit_eth3_w, p_para->info[loop].value);
                if ((index_temp&0x01))
                {
                    sal_memcpy((uint8*)&ds_l2_edit6_w_outer + sizeof(ds_l2_edit_eth3_w), (uint8*)&ds_l2_edit_eth3_w, sizeof(ds_l2_edit_eth3_w));
                }
                else
                {
                    sal_memcpy((uint8*)&ds_l2_edit6_w_outer, (uint8*)&ds_l2_edit_eth3_w, sizeof(ds_l2_edit_eth3_w));
                }
                sal_memcpy((uint8*)value, (uint8*)&ds_l2_edit6_w_outer, sizeof(ds_l2_edit6_w_outer));
            }
            else if (DsL3Edit3W3rd_t == input_tbl_id)
            {
                input_tbl_id = DsL3EditMpls3W_t;
                drv_set_field(lchip, input_tbl_id, fld_id, (uint8*)&ds_mpls_3w, p_para->info[loop].value);
                if ((index_temp&0x01))
                {
                    sal_memcpy((uint8*)&ds_mpls_6w + sizeof(ds_mpls_3w), (uint8*)&ds_mpls_3w, sizeof(ds_mpls_3w));
                }
                else
                {
                    sal_memcpy((uint8*)&ds_mpls_6w, (uint8*)&ds_mpls_3w, sizeof(ds_mpls_3w));
                }
                sal_memcpy((uint8*)value, (uint8*)&ds_mpls_6w, sizeof(ds_mpls_6w));
            }
            else if (DsFwd_t == reg_tbl_id && is_half)
            {
                drv_set_field(lchip, DsFwdHalf_t, fld_id, (uint8*)&dsfwd_half, p_para->info[loop].value);
                if (index_temp % 2 == 0)
                {
                    drv_set_field(lchip, DsFwdDualHalf_t, DsFwdDualHalf_g_0_dsFwdHalf_f, (uint8*)value, (uint32*)&dsfwd_half);
                }
                else
                {
                    drv_set_field(lchip, DsFwdDualHalf_t, DsFwdDualHalf_g_1_dsFwdHalf_f, (uint8*)value, (uint32*)&dsfwd_half);
                }
            }
            else
            {
                drv_set_field(lchip, reg_tbl_id, fld_id, value, p_para->info[loop].value);
            }
            if (_sys_usw_diag_table_is_hash_key(lchip, reg_tbl_id))
            {
                _sys_usw_diag_table_acc_write(lchip, reg_tbl_id, index_temp, (void*)(value));
            }
            else
            {
                if (((DsEthMep_t == reg_tbl_id) || (DsEthRmep_t == reg_tbl_id) || (DsBfdMep_t == reg_tbl_id) || (DsBfdRmep_t == reg_tbl_id)))
                {
                    sal_memset(mask, 0xFF, MAX_ENTRY_WORD * sizeof(uint32));
                    field_value = 0;
                    drv_set_field(lchip, reg_tbl_id, fld_id, mask, &field_value);
                    field_entry.data_entry = value;
                    field_entry.mask_entry = mask;
                    ret = DRV_IOCTL(lchip, index, cmd, &field_entry);
                }
                else
                {
                    ret = DRV_IOCTL(lchip, index, cmd, value);
                }
            }
        }
    }
    if (ret < 0)
    {
        SYS_DIAG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR,"%% Write %s index %d failed %d\n",  "tbl-reg", index_temp, ret);
    }

WRITE_TABLE_ERROR:
    if (mask)
    {
        sal_free(mask);
    }
    if (value)
    {
        sal_free(value);
    }

    return ret;
}
STATIC int32
_sys_usw_diag_list_tbl(uint8 lchip, ctc_diag_tbl_t *p_para)
{
    uint16 fld_idx = 0;
    fields_t* fld_ptr = NULL;
    uint32  *ids = NULL;
    char*    upper_name = NULL;
    char*    upper_name1 = NULL;
    tables_info_t* tbl_ptr = NULL;
    uint32  i = 0;
    uint32  id_index = 0;
    char* p_char = NULL;
    int32 ret = CTC_E_NONE;
    uint32 loop = 0;
    uint32 index_num = 0;

    ids = (uint32 *)sal_malloc(SYS_DIAG_LIST_MAX_NUM * sizeof(uint32));
    if(ids == NULL)
    {
        SYS_DIAG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "%% No memory\n");
        return CTC_E_NO_MEMORY;
    }
    upper_name = (char*)sal_malloc(CTC_DIAG_TBL_NAME_SIZE * sizeof(char));
    if(upper_name == NULL)
    {
        SYS_DIAG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "%% No memory\n");
        ret = CTC_E_NO_MEMORY;
        goto clean;
    }
    upper_name1 = (char*)sal_malloc(CTC_DIAG_TBL_NAME_SIZE * sizeof(char));
    if(upper_name1 == NULL)
    {
        SYS_DIAG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "%% No memory\n");
        ret = CTC_E_NO_MEMORY;
        goto clean;
    }

    sal_memset(ids, 0, SYS_DIAG_LIST_MAX_NUM * sizeof(uint32));
    sal_memset(upper_name, 0, CTC_DIAG_TBL_NAME_SIZE * sizeof(char));
    sal_memset(upper_name1, 0, CTC_DIAG_TBL_NAME_SIZE * sizeof(char));

    sal_strncpy((char*)upper_name, (char*)p_para->tbl_str, CTC_DIAG_TBL_NAME_SIZE);
    _sys_usw_diag_table_str_to_upper(upper_name);

    for (i = 0; i < MaxTblId_t; i++)
    {
        drv_get_table_property(lchip, DRV_TABLE_PROP_GET_NAME, i, 0, (char*)upper_name1);
        _sys_usw_diag_table_str_to_upper(upper_name1);

        p_char = (char*)sal_strstr((char*)upper_name1, (char*)upper_name);
        if (p_char)
        {
            if (0 == TABLE_MAX_INDEX(lchip, i))
            {
                continue;
            }

            if (sal_strlen((char*)upper_name1) == sal_strlen((char*)upper_name))
            {
                /* list full matched */
                tbl_ptr = TABLE_INFO_PTR(lchip, i);
                loop = (tbl_ptr->field_num > p_para->entry_num)? p_para->entry_num : tbl_ptr->field_num;
                index_num = TABLE_MAX_INDEX(lchip, i);
                index_num = ((i == DsFwd_t) || (i == DsFwdHalf_t))? index_num*2: index_num;
                for (fld_idx = 0; fld_idx < loop; fld_idx++)
                {
                    fld_ptr = &(tbl_ptr->ptr_fields[fld_idx]);
                    sal_strncpy(p_para->info[fld_idx].str, fld_ptr->ptr_field_name, CTC_DIAG_TBL_NAME_SIZE);
                }
                p_para->entry_num = tbl_ptr->field_num;
                p_para->index = index_num;
                goto clean;
            }
            else
            {
                ids[id_index] = i;
                id_index++;
            }

            if (id_index >= SYS_DIAG_LIST_MAX_NUM)
            {
                break;
            }
        }
    }

    if (id_index == 0)
    {
        SYS_DIAG_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "%% Not found %s \n", p_para->tbl_str);
        ret = CTC_E_NOT_EXIST;
        goto clean;
    }
    loop = (p_para->entry_num > id_index) ? id_index : p_para->entry_num;
    for (i = 0; i < loop; i++)
    {
        /* list matched */
        drv_get_table_property(lchip, DRV_TABLE_PROP_GET_NAME, ids[i], 0, p_para->info[i].str);
    }
    p_para->entry_num = id_index;

clean:
    if(NULL != ids)
    {
        sal_free(ids);
    }
    if(NULL != upper_name)
    {
        sal_free(upper_name);
    }
    if(NULL != upper_name1)
    {
        sal_free(upper_name1);
    }

    return ret;
}
STATIC int32
_sys_usw_diag_tbl_control(uint8 lchip, ctc_diag_tbl_t *p_para)
{
    switch (p_para->type)
    {
        case CTC_DIAG_TBL_OP_LIST:
            CTC_ERROR_RETURN(_sys_usw_diag_list_tbl(lchip, p_para));
            break;
        case CTC_DIAG_TBL_OP_READ:
            CTC_ERROR_RETURN(_sys_usw_diag_read_tbl(lchip, p_para));
            break;
        case CTC_DIAG_TBL_OP_WRITE:
            CTC_ERROR_RETURN(_sys_usw_diag_write_tbl(lchip, p_para));
            break;
        default:
            break;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_usw_diag_set_lb_distribution(uint8 lchip, ctc_diag_lb_dist_t* p_para)
{
    uint16 port_cnt = 0;
    uint32* p_gport = NULL;
    int32 ret = 0;
    uint32 gport[64] = {0};
    uint16 loop = 0;
    uint16 loop1 = 0;
    uint16 count = 0;
    uint8 gchip = 0;
    sys_usw_diag_lb_dist_t* p_lb_info = NULL;
    sys_usw_diag_lb_dist_t lb_info = {0};
    ctc_mac_stats_t mac_stats;
    uint8 channel_id = 0;
    sys_nh_info_dsnh_t nhinfo = {0};
    sys_nh_info_dsnh_t nhinfo_tmp = {0};
    ctc_linkagg_group_t linkagg_grp = {0};

    lb_info.group_id =  p_para->group_id;
    lb_info.flag = p_para->flag;
    p_lb_info = _sys_usw_diag_dist_hash_lkup(lchip, lb_info.group_id, lb_info.flag);
    if (NULL != p_lb_info)
    {
        SYS_DIAG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, " Entry already exist \n");
        return CTC_E_EXIST;
    }
    p_lb_info = mem_malloc(MEM_DIAG_MODULE, sizeof(sys_usw_diag_lb_dist_t));
    if (NULL == p_lb_info)
    {
        SYS_DIAG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, " No Memory! \n");
        return CTC_E_NO_MEMORY;
    }
    sal_memset(p_lb_info,0,sizeof(sys_usw_diag_lb_dist_t));
    if(CTC_FLAG_ISSET(p_para->flag, CTC_DIAG_LB_DIST_IS_LINKAGG))
    {
        linkagg_grp.tid = p_para->group_id;
        CTC_ERROR_GOTO(sys_usw_linkagg_get_group_info(lchip, &linkagg_grp),ret,error);
        if((CTC_FLAG_ISSET(p_para->flag, CTC_DIAG_LB_DIST_IS_DYNAMIC) && linkagg_grp.linkagg_mode != CTC_LINKAGG_MODE_DLB) ||
           (!CTC_FLAG_ISSET(p_para->flag, CTC_DIAG_LB_DIST_IS_DYNAMIC) && linkagg_grp.linkagg_mode != CTC_LINKAGG_MODE_STATIC))
        {
            SYS_DIAG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, " The linkagg mode is not right! \n");
            ret = CTC_E_INVALID_PARAM;
            goto error;
        }
        p_gport = (uint32*)mem_malloc(MEM_DIAG_MODULE, (sizeof(uint32)*linkagg_grp.member_num));
        if (NULL == p_gport)
        {
            SYS_DIAG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, " No Memory! \n");
            ret = CTC_E_NO_MEMORY;
            goto error;
        }
        sal_memset(p_gport,0,(sizeof(uint32)*linkagg_grp.member_num));
        CTC_ERROR_GOTO(sys_usw_linkagg_show_ports(lchip, p_para->group_id, p_gport, &port_cnt),ret,error);
        /* clear the same gport and remote gport*/
        for(loop = 0; loop < port_cnt; loop ++)
        {
            if(p_gport[loop] != 0xffffffff)
            {
                gchip = SYS_MAP_CTC_GPORT_TO_GCHIP(p_gport[loop]);
                SYS_GLOBAL_CHIPID_CHECK(gchip);
                if (FALSE == sys_usw_chip_is_local(lchip, gchip))
                {
                    p_gport[loop] = 0xffffffff;
                }
            }
            for(loop1 = loop+1; loop1 < port_cnt; loop1 ++)
            {
                if(p_gport[loop1] == p_gport[loop])
                {
                    p_gport[loop1] = 0xffffffff;
                }
            }
        }
        for(loop = 0; loop < port_cnt; loop ++)
        {
            if(p_gport[loop] != 0xffffffff)
            {
                if(count == 64)
                {
                    ret = CTC_E_INVALID_PARAM;
                    goto error;
                }
                gport[count] = p_gport[loop];
                count ++;
            }
        }
        mem_free(p_gport);
        p_gport = NULL;
        p_lb_info->info = mem_malloc(MEM_DIAG_MODULE, sizeof(ctc_diag_lb_info_t) * count);
        if (NULL == p_lb_info->info)
        {
            SYS_DIAG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, " No Memory! \n");
            ret = CTC_E_NO_MEMORY;
            goto error;
        }
        sal_memset(p_lb_info->info,0,sizeof(ctc_diag_lb_info_t) * count);
        if(!CTC_FLAG_ISSET(p_para->flag, CTC_DIAG_LB_DIST_IS_DYNAMIC))
        {
            for(loop = 0; loop < count; loop ++)
            {
                p_lb_info->info[loop].gport = gport[loop];
                sal_memset(&mac_stats, 0, sizeof(ctc_mac_stats_t));
                mac_stats.stats_mode = CTC_STATS_MODE_DETAIL;
                CTC_ERROR_GOTO(sys_usw_mac_stats_get_tx_stats(lchip, gport[loop], &mac_stats),ret,error);
                p_lb_info->info[loop].pkts = mac_stats.u.stats_detail.stats.tx_stats.good_ucast_pkts + mac_stats.u.stats_detail.stats.tx_stats.good_mcast_pkts +
                                        mac_stats.u.stats_detail.stats.tx_stats.good_bcast_pkts;
                p_lb_info->info[loop].bytes = mac_stats.u.stats_detail.stats.tx_stats.good_ucast_bytes + mac_stats.u.stats_detail.stats.tx_stats.good_mcast_bytes +
                                        mac_stats.u.stats_detail.stats.tx_stats.good_bcast_bytes;
            }
        }
        else
        {
            for(loop = 0; loop < count; loop ++)
            {
                channel_id = SYS_GET_CHANNEL_ID(lchip, gport[loop]);
                if (channel_id >= MCHIP_CAP(SYS_CAP_NETWORK_CHANNEL_NUM) )
                {
                    continue;
                }
                p_lb_info->info[loop].gport = gport[loop];
            }
        }
    }
    else
    {
        CTC_ERROR_GOTO(sys_usw_nh_get_nhinfo(lchip, p_para->group_id, &nhinfo, 0),ret, error);
        if(nhinfo.ecmp_valid == 0)
        {
            SYS_DIAG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, " Ecmp group id not exist \n");
            return CTC_E_NOT_EXIST;
        }
        count = nhinfo.valid_cnt;
        for(loop = 0; loop < count; loop ++)
        {
            CTC_ERROR_GOTO(sys_usw_nh_get_nhinfo(lchip, nhinfo.nh_array[loop], &nhinfo_tmp, 0),ret, error);
            gport[loop] = nhinfo_tmp.gport;
        }

        p_lb_info->info = mem_malloc(MEM_DIAG_MODULE, sizeof(ctc_diag_lb_info_t) * count);
        if (NULL == p_lb_info->info)
        {
            SYS_DIAG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, " No Memory! \n");
            ret = CTC_E_NO_MEMORY;
            goto error;
        }
        sal_memset(p_lb_info->info,0,sizeof(ctc_diag_lb_info_t) * count);
        if(CTC_FLAG_ISSET(p_para->flag, CTC_DIAG_LB_DIST_IS_DYNAMIC))
        {
            for(loop = 0; loop < count; loop ++)
            {
                channel_id = SYS_GET_CHANNEL_ID(lchip, gport[loop]);
                if (channel_id >= MCHIP_CAP(SYS_CAP_NETWORK_CHANNEL_NUM) )
                {
                    continue;
                }
                p_lb_info->info[loop].gport = gport[loop];
            }
        }
        else
        {
            ret = CTC_E_NOT_SUPPORT;
            goto error;
        }
    }
    p_para->member_num = count;
    p_lb_info->member_num = count;
    p_lb_info->group_id = p_para->group_id;
    p_lb_info->flag = p_para->flag;
    CTC_ERROR_GOTO(_sys_usw_diag_dist_hash_add(lchip, p_lb_info),ret,error);

    return CTC_E_NONE;
error:
    if(p_gport)
    {
        mem_free(p_gport);
    }
    if(p_lb_info->info)
    {
        mem_free(p_lb_info->info);
    }
    if(p_lb_info)
    {
        mem_free(p_lb_info);
    }
    return ret;
}

STATIC int32
_sys_usw_diag_get_lb_distribution(uint8 lchip, ctc_diag_lb_dist_t* p_para)
{
    uint16 loop = 0;
    uint32 cmd = 0;
    uint32 value = 0;
    sys_usw_diag_lb_dist_t* p_lb_info = NULL;
    sys_usw_diag_lb_dist_t lb_info = {0};
    ctc_mac_stats_t mac_stats;
    uint8 channel_id = 0;
    uint32 member_num = 0;

    lb_info.group_id =  p_para->group_id;
    lb_info.flag = p_para->flag;
    p_lb_info = _sys_usw_diag_dist_hash_lkup(lchip, lb_info.group_id, lb_info.flag);
    if(p_lb_info == NULL)
    {
        SYS_DIAG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, " Entry not exist \n");
        return CTC_E_NOT_EXIST;
    }
    member_num = (p_para->member_num > p_lb_info->member_num) ? p_lb_info->member_num: p_para->member_num;
    if(CTC_FLAG_ISSET(p_para->flag, CTC_DIAG_LB_DIST_IS_LINKAGG))
    {
        if(!CTC_FLAG_ISSET(p_para->flag, CTC_DIAG_LB_DIST_IS_DYNAMIC))
        {
            for(loop = 0; loop < member_num; loop ++)
            {
                p_para->info[loop].gport = p_lb_info->info[loop].gport;
                sal_memset(&mac_stats, 0, sizeof(ctc_mac_stats_t));
                mac_stats.stats_mode = CTC_STATS_MODE_DETAIL;
                CTC_ERROR_RETURN(sys_usw_mac_stats_get_tx_stats(lchip, p_lb_info->info[loop].gport, &mac_stats));
                p_para->info[loop].pkts = mac_stats.u.stats_detail.stats.tx_stats.good_ucast_pkts + mac_stats.u.stats_detail.stats.tx_stats.good_mcast_pkts +
                                        mac_stats.u.stats_detail.stats.tx_stats.good_bcast_pkts - p_lb_info->info[loop].pkts;
                p_para->info[loop].bytes = mac_stats.u.stats_detail.stats.tx_stats.good_ucast_bytes + mac_stats.u.stats_detail.stats.tx_stats.good_mcast_bytes +
                                        mac_stats.u.stats_detail.stats.tx_stats.good_bcast_bytes - p_lb_info->info[loop].bytes;
            }
        }
        else
        {
            for(loop = 0; loop < member_num; loop ++)
            {
                channel_id = SYS_GET_CHANNEL_ID(lchip, p_lb_info->info[loop].gport);
                if (channel_id >= MCHIP_CAP(SYS_CAP_NETWORK_CHANNEL_NUM) )
                {
                    continue;
                }
                cmd = DRV_IOR(DsDlbChanCurrentLoad_t, DsDlbChanCurrentLoad_byteCount_f);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel_id, cmd, &value));
                p_para->info[loop].gport = p_lb_info->info[loop].gport;
                p_para->info[loop].bytes = value;
            }
        }
    }
    else
    {
        if(CTC_FLAG_ISSET(p_para->flag, CTC_DIAG_LB_DIST_IS_DYNAMIC))
        {
            for(loop = 0; loop < member_num; loop ++)
            {
                channel_id = SYS_GET_CHANNEL_ID(lchip, p_lb_info->info[loop].gport);
                if (channel_id >= MCHIP_CAP(SYS_CAP_NETWORK_CHANNEL_NUM) )
                {
                    continue;
                }
                cmd = DRV_IOR(DsDlbChanCurrentLoad_t, DsDlbChanCurrentLoad_byteCount_f);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel_id, cmd, &value));
                p_para->info[loop].gport = p_lb_info->info[loop].gport;
                p_para->info[loop].bytes = value;
            }
        }
    }
    p_para->member_num = p_lb_info->member_num;
    CTC_ERROR_RETURN(_sys_usw_diag_dist_hash_remove(lchip, p_lb_info));

    return CTC_E_NONE;
}


STATIC int32
_sys_usw_diag_set_drop_pkt_cb(uint8 lchip, ctc_diag_drop_pkt_config_t* p_cfg)
{
    uint8 is_register = 0;
    ctc_pkt_rx_register_t rx_register;
    sal_memset(&rx_register, 0, sizeof(ctc_pkt_rx_register_t));
    rx_register.cb = sys_usw_diag_drop_pkt_sync;
    CTC_BMP_SET(rx_register.reason_bmp, CTC_PKT_CPU_REASON_DIAG_DISCARD_PKT);

    if (p_cfg->flags & CTC_DIAG_DROP_PKT_STORE_EN)
    {
        if (p_usw_diag_master[lchip]->drop_pkt_report_cb == NULL)
        {
            if (p_cfg->storage_en)
            {
                CTC_ERROR_RETURN(sys_usw_packet_rx_register(lchip, &rx_register));
                is_register = 1;
            }
            else if (!((p_cfg->flags & CTC_DIAG_DROP_PKT_REPORT_CALLBACK) && p_cfg->report_cb))
            {
                CTC_ERROR_RETURN(sys_usw_packet_rx_unregister(lchip, &rx_register));
            }
        }
    }

    if (p_cfg->flags & CTC_DIAG_DROP_PKT_REPORT_CALLBACK)
    {
        if (p_usw_diag_master[lchip]->drop_pkt_list == NULL)
        {
            if (p_cfg->report_cb && !is_register)
            {
                CTC_ERROR_RETURN(sys_usw_packet_rx_register(lchip, &rx_register));
            }
            else if (!((p_cfg->flags & CTC_DIAG_DROP_PKT_STORE_EN) && p_cfg->storage_en))
            {
                CTC_ERROR_RETURN(sys_usw_packet_rx_unregister(lchip, &rx_register));
            }
        }
    }
    return CTC_E_NONE;
}

int32
_sys_usw_diag_get_mem_usage(uint8 lchip, ctc_diag_mem_usage_t* p_para)
{
    sys_ftm_specs_info_t specs_info = {0};
    uint32 entry_num = 0;
    uint32 entry_num1 = 0;
    uint32 cam_num = 0;
    int32 ret = CTC_E_NONE;
    uint32 used_size = 0;
    uint8 loop = 0;
    uint32 edit_name[][2] =
        {
            {DsFwd_t,SYS_NH_ENTRY_TYPE_FWD},
            {DsMetEntry3W_t,SYS_NH_ENTRY_TYPE_MET},
            {DsNextHop4W_t,SYS_NH_ENTRY_TYPE_NEXTHOP_4W},
            {DsL2EditEth3W_t,SYS_NH_ENTRY_TYPE_L2EDIT_INNER_3W},
            {DsL2EditEth3W_t,SYS_NH_ENTRY_TYPE_L3EDIT_FLEX},
            {0,SYS_NH_ENTRY_TYPE_L3EDIT_SPME},
            {0,SYS_NH_ENTRY_TYPE_L2EDIT_OUTER_4W},
        };

    switch(p_para->type)
    {
        case CTC_DIAG_TYPE_SRAM_MAC:
            drv_usw_ftm_get_entry_num(lchip, DsFibHost0MacHashKey_t, &entry_num);
            cam_num = FLOW_HASH_CAM_NUM;
            ret = sys_usw_ftm_process_callback(lchip,CTC_FTM_SPEC_MAC,&specs_info);
            p_para->total_size = (entry_num+cam_num)*85;
            p_para->used_size = specs_info.used_size*85;
            break;

        case CTC_DIAG_TYPE_SRAM_IP_HOST:
            CTC_MAX_VALUE_CHECK(p_para->sub_type,CTC_DIAG_SUB_TYPE_IPV6);
            drv_usw_ftm_get_entry_num(lchip, DsFibHost0Ipv4HashKey_t, &entry_num);
            cam_num = FIB_HOST0_CAM_NUM;
            if(p_para->sub_type == 0)
            {
                specs_info.type = 0;
                ret = sys_usw_ftm_process_callback(lchip,CTC_FTM_SPEC_IPUC_HOST,&specs_info);
                used_size = specs_info.used_size+used_size;
                specs_info.type = 1;
                ret = sys_usw_ftm_process_callback(lchip,CTC_FTM_SPEC_IPUC_HOST,&specs_info);
                used_size = specs_info.used_size+used_size;
            }
            else
            {
                specs_info.type = p_para->sub_type - 1;
                ret = sys_usw_ftm_process_callback(lchip,CTC_FTM_SPEC_IPUC_HOST,&specs_info);
                used_size = specs_info.used_size+used_size;
            }
            p_para->total_size = (entry_num+cam_num)*85;
            p_para->used_size = used_size*85;
            break;

        case CTC_DIAG_TYPE_SRAM_IP_LPM:
            CTC_MAX_VALUE_CHECK(p_para->sub_type,CTC_DIAG_SUB_TYPE_IPV6);
            drv_usw_ftm_get_entry_num(lchip, DsNeoLpmIpv4Bit32Snake_t, &entry_num);
            entry_num = entry_num*12/4;
            if(p_para->sub_type == 0)
            {
                specs_info.type = 0;
                ret = sys_usw_ftm_process_callback(lchip,CTC_FTM_SPEC_IPUC_LPM,&specs_info);
                used_size = specs_info.used_size+used_size;
                specs_info.type = 1;
                ret = sys_usw_ftm_process_callback(lchip,CTC_FTM_SPEC_IPUC_LPM,&specs_info);
                used_size = specs_info.used_size+used_size;
            }
            else
            {
                specs_info.type = p_para->sub_type - 1;
                ret = sys_usw_ftm_process_callback(lchip,CTC_FTM_SPEC_IPUC_LPM,&specs_info);
                used_size = specs_info.used_size+used_size;
            }
            p_para->total_size = entry_num*56;
            p_para->used_size = used_size*56;
            break;
        case CTC_DIAG_TYPE_SRAM_SCL:
            CTC_MAX_VALUE_CHECK(p_para->sub_type,CTC_DIAG_SUB_TYPE_TUNNEL_DECP);
            drv_usw_ftm_get_entry_num(lchip, DsUserIdMacHashKey_t, &entry_num);
            drv_usw_ftm_get_entry_num(lchip, DsUserId1MacHashKey_t, &entry_num1);
            entry_num += entry_num1;
            if(p_para->sub_type == 0)
            {
                ret = sys_usw_ftm_process_callback(lchip,CTC_FTM_SPEC_SCL,&specs_info);
                used_size += specs_info.used_size;
                ret = sys_usw_ftm_process_callback(lchip,CTC_FTM_SPEC_SCL_FLOW,&specs_info);
                used_size += specs_info.used_size;
                ret = sys_usw_ftm_process_callback(lchip,CTC_FTM_SPEC_TUNNEL,&specs_info);
                used_size += specs_info.used_size;
                specs_info.used_size = used_size;
            }
            else if(p_para->sub_type == CTC_DIAG_SUB_TYPE_SCL)
            {
                ret = sys_usw_ftm_process_callback(lchip,CTC_FTM_SPEC_SCL,&specs_info);
            }
            else if (p_para->sub_type == CTC_DIAG_SUB_TYPE_SCL_FLOW)
            {
                ret = sys_usw_ftm_process_callback(lchip,CTC_FTM_SPEC_SCL_FLOW,&specs_info);
            }
            else
            {
                ret = sys_usw_ftm_process_callback(lchip,CTC_FTM_SPEC_TUNNEL,&specs_info);
            }
            p_para->total_size = entry_num*85;
            p_para->used_size = specs_info.used_size*85;
            break;

        case CTC_DIAG_TYPE_SRAM_ACL:
            drv_usw_ftm_get_entry_num(lchip, DsFlowL2HashKey_t, &entry_num);
            cam_num = FLOW_HASH_CAM_NUM;
            ret = sys_usw_ftm_process_callback(lchip,CTC_FTM_SPEC_ACL_FLOW,&specs_info);
            p_para->total_size = (entry_num+cam_num)*170;
            p_para->used_size = specs_info.used_size*170;
            break;

        case CTC_DIAG_TYPE_SRAM_MPLS:
            drv_usw_ftm_get_entry_num(lchip, DsMplsLabelHashKey_t, &entry_num);
            cam_num = 64;
            ret = sys_usw_ftm_process_callback(lchip,CTC_FTM_SPEC_MPLS,&specs_info);
            p_para->total_size = (entry_num+cam_num)*34;
            p_para->used_size = specs_info.used_size*34;
            break;

        case CTC_DIAG_TYPE_SRAM_OAM_MEP:
            drv_usw_ftm_get_entry_num(lchip, DsBfdMep_t, &entry_num);
            ret = sys_usw_ftm_process_callback(lchip,CTC_FTM_SPEC_OAM,&specs_info);
            p_para->total_size = entry_num*170;
            p_para->used_size = specs_info.used_size*170*2;
            break;

        case CTC_DIAG_TYPE_SRAM_OAM_KEY:
            drv_usw_ftm_get_entry_num(lchip, DsBfdMep_t, &entry_num);
            sys_usw_oam_get_used_key_count(lchip,&specs_info.used_size);
            p_para->total_size = entry_num*170;
            p_para->used_size = specs_info.used_size*170;
            break;

        case CTC_DIAG_TYPE_SRAM_APS:
            sys_usw_aps_ftm_cb(lchip, &specs_info.used_size);
            drv_usw_ftm_get_entry_num(lchip, DsApsBridge_t,&entry_num);
            p_para->total_size = (entry_num+cam_num)*163;
            p_para->used_size = specs_info.used_size*163;
            break;

        case CTC_DIAG_TYPE_TCAM_INGRESS_ACL:
            CTC_MAX_VALUE_CHECK(p_para->sub_type,7);
            sys_usw_acl_get_resource_by_priority(lchip,p_para->sub_type,0,&p_para->total_size,&p_para->used_size);
            p_para->total_size = p_para->total_size * 80;
            p_para->used_size = p_para->used_size * 80;
            break;

        case CTC_DIAG_TYPE_TCAM_EGRESS_ACL:
            CTC_MAX_VALUE_CHECK(p_para->sub_type,2);
            sys_usw_acl_get_resource_by_priority(lchip,p_para->sub_type,1,&p_para->total_size,&p_para->used_size);
            p_para->total_size = p_para->total_size * 80;
            p_para->used_size = p_para->used_size * 80;
            break;

        case CTC_DIAG_TYPE_TCAM_SCL:
            CTC_MAX_VALUE_CHECK(p_para->sub_type,3);
            sys_usw_scl_get_resource_by_priority(lchip, p_para->sub_type, &p_para->total_size, &p_para->used_size);
            p_para->total_size = p_para->total_size * 80;
            p_para->used_size = p_para->used_size * 80;
            break;

        case CTC_DIAG_TYPE_TCAM_LPM:
            CTC_MAX_VALUE_CHECK(p_para->sub_type,CTC_DIAG_SUB_TYPE_IPV6);
            sys_usw_ipuc_get_tcam_memusage(lchip,p_para->sub_type,&p_para->total_size,&p_para->used_size);
            break;

        case CTC_DIAG_TYPE_SRAM_NEXTHOP:
            CTC_MAX_VALUE_CHECK(p_para->sub_type,CTC_DIAG_SUB_TYPE_OUT_EDIT);
            if(p_para->sub_type == 0)
            {
                for(loop = 0; loop < 7; loop ++)
                {
                    sys_usw_nh_get_nh_resource(lchip, edit_name[loop][1],&used_size);
                    specs_info.used_size += used_size;
                    if(loop < 4)
                    {
                        drv_usw_ftm_get_entry_num(lchip, edit_name[loop][0], &entry_num1);
                        if(loop == 0)
                        {
                            entry_num += entry_num1*2;
                        }
                        else
                        {
                            entry_num += entry_num1;
                        }
                    }
                }
                entry_num += 2048;
            }
            else if(p_para->sub_type == CTC_DIAG_SUB_TYPE_SPME || p_para->sub_type == CTC_DIAG_SUB_TYPE_OUT_EDIT)
            {
                entry_num = 1024;
                sys_usw_nh_get_nh_resource(lchip, edit_name[p_para->sub_type][1],&specs_info.used_size);
            }
            else if(p_para->sub_type == CTC_DIAG_SUB_TYPE_EDIT)
            {
                drv_usw_ftm_get_entry_num(lchip, edit_name[3][0], &entry_num);
                sys_usw_nh_get_nh_resource(lchip, edit_name[3][1],&used_size);
                specs_info.used_size = specs_info.used_size + used_size;
                sys_usw_nh_get_nh_resource(lchip, edit_name[4][1],&used_size);
                specs_info.used_size = specs_info.used_size + used_size;
            }
            else if(p_para->sub_type == CTC_DIAG_SUB_TYPE_FWD)
            {
                drv_usw_ftm_get_entry_num(lchip, edit_name[p_para->sub_type-1][0], &entry_num);
                entry_num = entry_num * 2;
                sys_usw_nh_get_nh_resource(lchip, edit_name[p_para->sub_type-1][1],&specs_info.used_size);
            }
            else
            {
                drv_usw_ftm_get_entry_num(lchip, edit_name[p_para->sub_type-1][0], &entry_num);
                sys_usw_nh_get_nh_resource(lchip, edit_name[p_para->sub_type-1][1],&specs_info.used_size);
            }
            p_para->total_size = entry_num*90;
            p_para->used_size = specs_info.used_size*90;
            break;

        default:
            return CTC_E_INVALID_PARAM;
    }

    return ret;
}

#define ______INTERNAL_API______

int32
sys_usw_diag_drop_pkt_sync(ctc_pkt_rx_t* p_pkt_rx)
{
    int32 ret = CTC_E_NONE;
    uint8 lchip = 0;
    CTC_PTR_VALID_CHECK(p_pkt_rx);
    lchip = p_pkt_rx->lchip;
    SYS_USW_DIAG_INIT_CHECK(lchip);
    DIAG_LOCK;
    ret = _sys_usw_diag_drop_pkt_sync(lchip, p_pkt_rx);
    DIAG_UNLOCK;
    return ret;
}


#define ______SYS_API______

int32
sys_usw_diag_trigger_pkt_trace(uint8 lchip, ctc_diag_pkt_trace_t* p_trace)
{
    int32 ret = CTC_E_NONE;
    SYS_USW_DIAG_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_trace);
    CTC_ERROR_RETURN(_sys_usw_diag_pkt_trace_watch_key_check(lchip, p_trace));
    DIAG_LOCK;
    ret = _sys_usw_diag_trigger_pkt_trace(lchip, p_trace);
    DIAG_UNLOCK;
    return ret;

}

int32
sys_usw_diag_get_pkt_trace(uint8 lchip, ctc_diag_pkt_trace_result_t* p_rslt)
{
    int32 ret = CTC_E_NONE;

    SYS_USW_DIAG_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_rslt);

    DIAG_LOCK;
    if (MCHIP_API(lchip)->diag_get_pkt_trace)
    {
        ret = MCHIP_API(lchip)->diag_get_pkt_trace(lchip, (void*)p_rslt);
    }
    DIAG_UNLOCK;
    return ret;
}

int32
sys_usw_diag_get_drop_info(uint8 lchip, ctc_diag_drop_t* p_drop)
{
    int32 ret = CTC_E_NONE;
    SYS_USW_DIAG_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_drop);
    DIAG_LOCK;
    ret = _sys_usw_diag_get_drop_info(lchip, p_drop);
    DIAG_UNLOCK;
    return ret;
}

int32
sys_usw_diag_set_property(uint8 lchip, ctc_diag_property_t prop, void* p_value)
{
    int32 ret = CTC_E_NONE;
    SYS_USW_DIAG_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_value);
    if (prop >= CTC_DIAG_PROP_MAX)
    {
        return CTC_E_INVALID_PARAM;
    }

    if (prop == CTC_DIAG_PROP_DROP_PKT_CONFIG)
    {
        _sys_usw_diag_set_drop_pkt_cb(lchip, (ctc_diag_drop_pkt_config_t*)p_value);
    }
    DIAG_LOCK;
    ret = _sys_usw_diag_set_property(lchip, prop, p_value);
    DIAG_UNLOCK;
    return ret;
}

int32
sys_usw_diag_get_property(uint8 lchip, ctc_diag_property_t prop, void* p_value)
{
    int32 ret = CTC_E_NONE;
    SYS_USW_DIAG_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_value);
    if (prop >= CTC_DIAG_PROP_MAX)
    {
        return CTC_E_INVALID_PARAM;
    }
    DIAG_LOCK;
    ret = _sys_usw_diag_get_property(lchip, prop, p_value);
    DIAG_UNLOCK;
    return ret;
}

int32
sys_usw_diag_tbl_control(uint8 lchip, ctc_diag_tbl_t *p_para)
{
    int32 ret = CTC_E_NONE;
    SYS_USW_DIAG_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_para);
    CTC_PTR_VALID_CHECK(p_para->info);
    if (p_para->type >= CTC_DIAG_TBL_OP_MAX || p_para->entry_num == 0 || sal_strlen(p_para->tbl_str) == 0)
    {
        return CTC_E_INVALID_PARAM;
    }
    DIAG_LOCK;
    ret = _sys_usw_diag_tbl_control(lchip, p_para);
    DIAG_UNLOCK;
    return ret;
}

int32
sys_usw_diag_set_lb_distribution(uint8 lchip, ctc_diag_lb_dist_t* p_para)
{
    int32 ret = CTC_E_NONE;
    SYS_USW_DIAG_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_para);
    DIAG_LOCK;
    ret = _sys_usw_diag_set_lb_distribution(lchip, p_para);
    DIAG_UNLOCK;
    return ret;
}

int32
sys_usw_diag_get_lb_distribution(uint8 lchip, ctc_diag_lb_dist_t* p_para)
{
    int32 ret = CTC_E_NONE;
    SYS_USW_DIAG_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_para);
    CTC_PTR_VALID_CHECK(p_para->info);
    CTC_MIN_VALUE_CHECK(p_para->member_num, 1);
    DIAG_LOCK;
    ret = _sys_usw_diag_get_lb_distribution(lchip, p_para);
    DIAG_UNLOCK;
    return ret;
}

int32
sys_usw_diag_get_mem_usage(uint8 lchip, ctc_diag_mem_usage_t* p_para)
{
    int32 ret = CTC_E_NONE;
    uint32 total_size = 0;
    uint32 used_size = 0;
    uint8 loop = 0;
    uint8 loop1 = 0;
    uint8 loop_num[4] = {1,8,3,4};
    SYS_USW_DIAG_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_para);
    DIAG_LOCK;
    if(p_para->type == CTC_DIAG_TYPE_SRAM_ALL)
    {
        for(loop = CTC_DIAG_TYPE_SRAM_MAC;loop < CTC_DIAG_TYPE_SRAM_ALL; loop ++)
        {
            p_para->type = loop;
            p_para->total_size = 0;
            p_para->used_size = 0;
            _sys_usw_diag_get_mem_usage(lchip, p_para);
            total_size += p_para->total_size;
            used_size += p_para->used_size;
        }
        p_para->total_size = total_size;
        p_para->used_size = used_size;
        p_para->type = CTC_DIAG_TYPE_SRAM_ALL;
    }
    else if(p_para->type == CTC_DIAG_TYPE_TCAM_ALL)
    {
        for(loop = CTC_DIAG_TYPE_TCAM_LPM;loop < CTC_DIAG_TYPE_TCAM_ALL; loop ++)
        {
            for(loop1 = 0; loop1 < loop_num[loop-CTC_DIAG_TYPE_TCAM_LPM]; loop1 ++)
            {
                p_para->type = loop;
                p_para->sub_type = loop1;
                p_para->total_size = 0;
                p_para->used_size = 0;
                _sys_usw_diag_get_mem_usage(lchip, p_para);
                total_size += p_para->total_size;
                used_size += p_para->used_size;
            }
        }
        p_para->total_size = total_size;
        p_para->used_size = used_size;
        p_para->sub_type = 0;
        p_para->type = CTC_DIAG_TYPE_TCAM_ALL;
    }
    else
    {
        ret = _sys_usw_diag_get_mem_usage(lchip, p_para);
    }
    DIAG_UNLOCK;
    return ret;
}
int32
sys_usw_diag_init(uint8 lchip, void* init_cfg)
{
    int32 ret = CTC_E_NONE;

    if (p_usw_diag_master[lchip])
    {
        return CTC_E_NONE;
    }
    p_usw_diag_master[lchip] = (sys_usw_diag_t*)mem_malloc(MEM_DIAG_MODULE, sizeof(sys_usw_diag_t));
    if ( NULL == p_usw_diag_master[lchip] )
    {
        SYS_DIAG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "Diag Init Error. No Memory! \n");
        return CTC_E_NO_MEMORY;
    }
    sal_memset(p_usw_diag_master[lchip], 0, sizeof(sys_usw_diag_t));

    ret = sal_mutex_create(&(p_usw_diag_master[lchip]->diag_mutex));
    if (ret || !(p_usw_diag_master[lchip]->diag_mutex))
    {
        SYS_DIAG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, " No resource in ASIC. Mutex Create Failed! \n");
        ret = CTC_E_NO_RESOURCE;
        goto error_back;
    }
    p_usw_diag_master[lchip]->drop_hash = ctc_hash_create(64, 16,
                                        (hash_key_fn)_sys_usw_diag_drop_hash_make,
                                        (hash_cmp_fn)_sys_usw_diag_drop_hash_cmp);
    if (NULL == p_usw_diag_master[lchip]->drop_hash)
    {
        SYS_DIAG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "Diag Init Error. No Memory! \n");
        ret = CTC_E_NO_MEMORY;
        goto error_back;
    }

    p_usw_diag_master[lchip]->lb_dist = ctc_hash_create((MCHIP_CAP(SYS_CAP_LINKAGG_GROUP_NUM)+MCHIP_CAP(SYS_CAP_NH_ECMP_GROUP_ID_NUM)+1)/32 , 32,
                                        (hash_key_fn)_sys_usw_diag_dist_hash_make,
                                        (hash_cmp_fn)_sys_usw_diag_dist_hash_cmp);
    if (NULL == p_usw_diag_master[lchip]->lb_dist)
    {
        SYS_DIAG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "Diag Init Error. No Memory! \n");
        ret = CTC_E_NO_MEMORY;
        goto error_back;
    }

    p_usw_diag_master[lchip]->drop_pkt_overwrite = 1;
    p_usw_diag_master[lchip]->drop_pkt_hash_len = 32;
    p_usw_diag_master[lchip]->drop_pkt_store_cnt = 128;
    p_usw_diag_master[lchip]->drop_pkt_store_len = 256;
    CTC_ERROR_RETURN(sys_usw_dump_db_register_cb(lchip, CTC_FEATURE_DIAG, _sys_usw_diag_dump_db));
    return CTC_E_NONE;

error_back:
    sys_usw_diag_deinit(lchip);
    return ret;
}

int32
sys_usw_diag_deinit(uint8 lchip)
{
    LCHIP_CHECK(lchip);
    if (NULL == p_usw_diag_master[lchip])
    {
        return CTC_E_NONE;
    }

    if (p_usw_diag_master[lchip]->drop_hash)
    {
        ctc_hash_traverse_remove(p_usw_diag_master[lchip]->drop_hash, (hash_traversal_fn)_sys_usw_diag_free_drop_hash_data, NULL);
        ctc_hash_free(p_usw_diag_master[lchip]->drop_hash);
    }
    if (p_usw_diag_master[lchip]->lb_dist)
    {
        ctc_hash_traverse_remove(p_usw_diag_master[lchip]->lb_dist, (hash_traversal_fn)_sys_usw_diag_free_dist_hash_data, NULL);
        ctc_hash_free(p_usw_diag_master[lchip]->lb_dist);
    }
    if (p_usw_diag_master[lchip]->diag_mutex)
    {
        sal_mutex_destroy(p_usw_diag_master[lchip]->diag_mutex);
    }
    _sys_usw_diag_drop_pkt_clear_buf(lchip);
    ctc_slist_free(p_usw_diag_master[lchip]->drop_pkt_list);
    mem_free(p_usw_diag_master[lchip]);
    return CTC_E_NONE;
}


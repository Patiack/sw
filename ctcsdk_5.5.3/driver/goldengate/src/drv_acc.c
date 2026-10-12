/**
  @file drv_acc.c

  @date 2010-02-26

  @version v5.1

  The file implement driver acc IOCTL defines and macros
*/

#include "goldengate/include/drv_lib.h"
#include "goldengate/include/drv_ftm.h"
#include "goldengate/include/drv_chip_agent.h"
/**********************************************************************************
              Define Gloabal var, Typedef, define and Data Structure
***********************************************************************************/
extern drv_io_t g_drv_gg_io_master;
extern bool  gg_client_init_done;

extern sal_mutex_t * gg_fib_acc_mutex;
extern sal_mutex_t * gg_cpu_acc_mutex;
extern sal_mutex_t * gg_ipfix_acc_mutex;

#define FIB_ACC_LOCK         sal_mutex_lock(gg_fib_acc_mutex)
#define FIB_ACC_UNLOCK       sal_mutex_unlock(gg_fib_acc_mutex)
#define CPU_ACC_LOCK         sal_mutex_lock(gg_cpu_acc_mutex)
#define CPU_ACC_UNLOCK       sal_mutex_unlock(gg_cpu_acc_mutex)
#define IPFIX_ACC_LOCK       sal_mutex_lock(gg_ipfix_acc_mutex)
#define IPFIX_ACC_UNLOCK     sal_mutex_unlock(gg_ipfix_acc_mutex)



#define SINGLE_KEY_BYTE           DS_FIB_HOST0_MAC_HASH_KEY_BYTES
#define DOUBLE_KEY_BYTE           DS_FIB_HOST0_IPV6_UCAST_HASH_KEY_BYTES
#define QUAD_KEY_BYTE             DS_FIB_HOST0_HASH_CAM_BYTES

typedef struct
{
    uint32 host1_cam_bitmap;
    DsFibHost1HashCam_m host1_cam[8];
} drv_acc_t;
drv_acc_t g_gg_drv_acc[MAX_LOCAL_CHIP_NUM] = {{0}};

typedef int32 (* DRV_FIB_ACC_PREPARE_FN_t)(drv_fib_acc_in_t* in, uint32* fib_acc_cpu_req);
typedef int32 (*DRV_FIB_ACC_GET_RESULT_FN_t)(uint32* fib_acc_cpu_rlt, drv_fib_acc_out_t* out);

/**********************************************************************************
                      Function interfaces realization
***********************************************************************************/
#define  _DRV_FIB_ACC_M_
STATIC int32
_drv_goldengate_get_length_by_tbl_id(uint32 tbl_id, uint32* length_mode, uint32* length, uint32* combined_length)
{
    uint32          _length_mode    = 0;
    uint32          _length         = 0;
    uint32          entry_size      = 0;
    uint32          _c_length       = 0;

    entry_size  = TABLE_ENTRY_SIZE(tbl_id) / 12;

    switch (entry_size)
    {
    case 1:
        _length_mode = HASH_KEY_LENGTH_MODE_SINGLE;
        _length = SINGLE_KEY_BYTE;
        _c_length = DRV_HASH_85BIT_KEY_LENGTH;
        break;

    case 2:
        _length_mode = HASH_KEY_LENGTH_MODE_DOUBLE;
        _length = DOUBLE_KEY_BYTE;
        _c_length = DRV_HASH_170BIT_KEY_LENGTH;
        break;

    case 4:
        _length_mode= HASH_KEY_LENGTH_MODE_QUAD;
        _length = QUAD_KEY_BYTE;
        _c_length = DRV_HASH_340BIT_KEY_LENGTH;
        break;
    default:
        return DRV_E_INVALID_TBL;
    }

    *length_mode     = _length_mode;
    *length          = _length;
    *combined_length = _c_length;

    return DRV_E_NONE;
}

static INLINE int8
_drv_goldengate_fib_acc_is_double_key(uint32 valid, uint32 hash_type)
{
    if (valid == 0)
        return 0;

    if ((hash_type == FIBHOST0PRIMARYHASHTYPE_IPV6MCAST) ||
        (hash_type == FIBHOST0PRIMARYHASHTYPE_IPV6UCAST) ||
        (hash_type == FIBHOST0PRIMARYHASHTYPE_MACIPV6MCAST))
        return 1;
    return 0;
}

STATIC int32
_drv_goldengate_fib_acc_read_key(uint8 chip_id, uint32 key_index, uint32 * data, uint32 * key_length_mode)
{
    uint32            single_key                                                  = DsFibHost0MacHashKey_t;
    uint32            double_key                                                  = DsFibHost0Ipv6McastHashKey_t;
    uint32            single_key_info[DS_FIB_HOST0_MAC_HASH_KEY_BYTES / 4]        = { 0 };
    uint32            double_key_info[DS_FIB_HOST0_IPV6_UCAST_HASH_KEY_BYTES / 4] = { 0 };
    uint32            valid                                                       = 0;
    uint32            hash_type                                                   = 0;
    uint8             is_double                                                   = 0;
    drv_fib_acc_in_t  acc_read_in;
    drv_fib_acc_out_t acc_read_out;

    sal_memset(data, 0, sizeof(drv_fib_acc_rw_data_t));
    sal_memset(single_key_info, 0, sizeof(single_key_info));
    sal_memset(&acc_read_in, 0, sizeof(drv_fib_acc_in_t));
    sal_memset(&acc_read_out, 0, sizeof(drv_fib_acc_out_t));

    acc_read_in.rw.tbl_id    = single_key;
    acc_read_in.rw.key_index = key_index;
    DRV_IF_ERROR_RETURN(drv_goldengate_fib_acc_no_lock(chip_id, DRV_FIB_ACC_READ_FIB0_BY_IDX, &acc_read_in, &acc_read_out));
    sal_memcpy(single_key_info, acc_read_out.read.data, sizeof(single_key_info));

    DRV_IOR_FIELD(single_key, DsFibHost0MacHashKey_valid_f, &valid, single_key_info);
    DRV_IOR_FIELD(single_key, DsFibHost0MacHashKey_hashType_f, &hash_type, single_key_info);

    is_double = _drv_goldengate_fib_acc_is_double_key(valid, hash_type);
    if (is_double)
    {
        acc_read_in.rw.tbl_id    = double_key;
        acc_read_in.rw.key_index = key_index;
        DRV_IF_ERROR_RETURN(drv_goldengate_fib_acc_no_lock(chip_id, DRV_FIB_ACC_READ_FIB0_BY_IDX, &acc_read_in, &acc_read_out));
#if (SDK_WORK_PLATFORM == 1)
        sal_memcpy((data + 2 * 3), acc_read_out.read.data, sizeof(double_key_info));
#else
        sal_memcpy((data + 0 * 3), acc_read_out.read.data, sizeof(double_key_info));
#endif
        *key_length_mode = 1;
    }
    else
    {
        acc_read_in.rw.tbl_id    = single_key;
        acc_read_in.rw.key_index = key_index;
        DRV_IF_ERROR_RETURN(drv_goldengate_fib_acc_no_lock(chip_id, DRV_FIB_ACC_READ_FIB0_BY_IDX, &acc_read_in, &acc_read_out));
#if (SDK_WORK_PLATFORM == 1)
        sal_memcpy((data + 3 * 3), acc_read_out.read.data, sizeof(single_key_info));
#else
        sal_memcpy((data + 0 * 3), acc_read_out.read.data, sizeof(single_key_info));
#endif
        acc_read_in.rw.tbl_id    = single_key;
        acc_read_in.rw.key_index = key_index + 1;
        DRV_IF_ERROR_RETURN(drv_goldengate_fib_acc_no_lock(chip_id, DRV_FIB_ACC_READ_FIB0_BY_IDX, &acc_read_in, &acc_read_out));
#if (SDK_WORK_PLATFORM == 1)
        sal_memcpy((data + 2 * 3), acc_read_out.read.data, sizeof(single_key_info));
#else
        sal_memcpy((data + 1 * 3), acc_read_out.read.data, sizeof(single_key_info));
#endif

        *key_length_mode = 0;
    }

    key_length_mode++;

    acc_read_in.rw.tbl_id    = single_key;
    acc_read_in.rw.key_index = key_index + 2;
    DRV_IF_ERROR_RETURN(drv_goldengate_fib_acc_no_lock(chip_id, DRV_FIB_ACC_READ_FIB0_BY_IDX, &acc_read_in, &acc_read_out));
    sal_memcpy(single_key_info, acc_read_out.read.data, sizeof(single_key_info));

    DRV_IOR_FIELD(single_key, DsFibHost0MacHashKey_valid_f, &valid, single_key_info);
    DRV_IOR_FIELD(single_key, DsFibHost0MacHashKey_hashType_f, &hash_type, single_key_info);

    is_double = _drv_goldengate_fib_acc_is_double_key(valid, hash_type);
    if (is_double)
    {
        acc_read_in.rw.tbl_id    = double_key;
        acc_read_in.rw.key_index = key_index + 2;
        DRV_IF_ERROR_RETURN(drv_goldengate_fib_acc_no_lock(chip_id, DRV_FIB_ACC_READ_FIB0_BY_IDX, &acc_read_in, &acc_read_out));
#if (SDK_WORK_PLATFORM == 1)
        sal_memcpy((data + 0 * 3), acc_read_out.read.data, sizeof(double_key_info));
#else
        sal_memcpy((data + 2 * 3), acc_read_out.read.data, sizeof(double_key_info));
#endif
        *key_length_mode = 1;
    }
    else
    {
        acc_read_in.rw.tbl_id    = single_key;
        acc_read_in.rw.key_index = key_index + 2;
        DRV_IF_ERROR_RETURN(drv_goldengate_fib_acc_no_lock(chip_id, DRV_FIB_ACC_READ_FIB0_BY_IDX, &acc_read_in, &acc_read_out));
#if (SDK_WORK_PLATFORM == 1)
        sal_memcpy((data + 1 * 3), acc_read_out.read.data, sizeof(single_key_info));
#else
        sal_memcpy((data + 2 * 3), acc_read_out.read.data, sizeof(single_key_info));
#endif
        acc_read_in.rw.tbl_id    = single_key;
        acc_read_in.rw.key_index = key_index + 3;
        drv_goldengate_fib_acc_no_lock(chip_id, DRV_FIB_ACC_READ_FIB0_BY_IDX, &acc_read_in, &acc_read_out);
#if (SDK_WORK_PLATFORM == 1)
        sal_memcpy((data + 0 * 3), acc_read_out.read.data, sizeof(single_key_info));
#else
        sal_memcpy((data + 3 * 3), acc_read_out.read.data, sizeof(single_key_info));
#endif
        *key_length_mode = 0;
    }
    return DRV_E_NONE;
}

STATIC int32
_drv_goldengate_fib_acc_prepare_write_mac_by_idx(drv_fib_acc_in_t* in, uint32* fib_acc_cpu_req)
{
    SetFibHashKeyCpuReq(A, cpuKeyMac_f, fib_acc_cpu_req, in->mac.mac);
    SetFibHashKeyCpuReq(V, cpuKeyVrfId_f, fib_acc_cpu_req, in->mac.fid);
    SetFibHashKeyCpuReq(V, cpuKeyDsAdIndex_f, fib_acc_cpu_req, in->mac.ad_index);
    SetFibHashKeyCpuReq(V, cpuKeyIndex_f, fib_acc_cpu_req, in->mac.key_index);
    return DRV_E_NONE;
}

STATIC int32
_drv_goldengate_fib_acc_prepare_write_mac_by_key(drv_fib_acc_in_t* in, uint32* fib_acc_cpu_req)
{
    uint32 key_index       = 0;
    uint32 static_count_en = 0;
    if (0 != (in->mac.flag & DRV_FIB_ACC_PORT_MAC_LIMIT_EN))
    {
        key_index |= 1 << 14;
    }
    if (0 != (in->mac.flag & DRV_FIB_ACC_VLAN_MAC_LIMIT_EN))
    {
        key_index |= 1 << 15;
    }
    if (0 != (in->mac.flag & DRV_FIB_ACC_DYNAMIC_MAC_LIMIT_EN))
    {
        key_index |= 1 << 16;
    }
    if (0 != (in->mac.flag & DRV_FIB_ACC_PROFILE_MAC_LIMIT_EN))
    {
        key_index |= 1 << 17;
    }
    if (0 != (in->mac.flag & DRV_FIB_ACC_CPU_KNOWN_ORG_PORT))
    {
        key_index |= 1 << 18;
        key_index |= in->mac.original_port & 0x3FFF;
    }
    if (0 != (in->mac.flag & DRV_FIB_ACC_STATIC_COUNT_EN))
    {
        static_count_en = 1;
    }

    SetFibHashKeyCpuReq(A, cpuKeyMac_f, fib_acc_cpu_req, in->mac.mac);
    SetFibHashKeyCpuReq(V, staticCountEn_f, fib_acc_cpu_req, static_count_en);
    SetFibHashKeyCpuReq(V, cpuKeyVrfId_f, fib_acc_cpu_req, in->mac.fid);
    SetFibHashKeyCpuReq(V, cpuKeyDsAdIndex_f, fib_acc_cpu_req, in->mac.ad_index);
    SetFibHashKeyCpuReq(V, hashKeyCpuReqPort_f, fib_acc_cpu_req, in->mac.learning_port);
    SetFibHashKeyCpuReq(V, cpuKeyIndex_f, fib_acc_cpu_req, key_index);
    SetFibHashKeyCpuReq(V, cpuKeyDelType_f, fib_acc_cpu_req, in->mac.aging_timer);
    SetFibHashKeyCpuReq(V, cpuKeyPortDelMode_f, fib_acc_cpu_req, in->mac.hw_aging_en);
    return DRV_E_NONE;
}

STATIC int32
_drv_goldengate_fib_acc_prepare_del_mac_by_idx(drv_fib_acc_in_t* in, uint32* fib_acc_cpu_req)
{
    SetFibHashKeyCpuReq(V, cpuKeyIndex_f, fib_acc_cpu_req, in->mac.key_index);
    return DRV_E_NONE;
}

STATIC int32
_drv_goldengate_fib_acc_prepare_del_mac_by_key(drv_fib_acc_in_t* in, uint32* fib_acc_cpu_req)
{
    uint32 key_index = 0;
    if (0 != (in->mac.flag & DRV_FIB_ACC_PORT_MAC_LIMIT_EN))
    {
        key_index |= 1 << 14;
    }
    if (0 != (in->mac.flag & DRV_FIB_ACC_VLAN_MAC_LIMIT_EN))
    {
        key_index |= 1 << 15;
    }
    if (0 != (in->mac.flag & DRV_FIB_ACC_DYNAMIC_MAC_LIMIT_EN))
    {
        key_index |= 1 << 16;
    }
    if (0 != (in->mac.flag & DRV_FIB_ACC_PROFILE_MAC_LIMIT_EN))
    {
        key_index |= 1 << 17;
    }
    if (0 != (in->mac.flag & DRV_FIB_ACC_CPU_KNOWN_ORG_PORT))
    {
        key_index |= 1 << 18;
    }

    SetFibHashKeyCpuReq(A, cpuKeyMac_f, fib_acc_cpu_req, in->mac.mac);
    SetFibHashKeyCpuReq(V, cpuKeyVrfId_f, fib_acc_cpu_req, in->mac.fid);
    SetFibHashKeyCpuReq(V, hashKeyCpuReqPort_f, fib_acc_cpu_req, in->mac.original_port);
    SetFibHashKeyCpuReq(V, cpuKeyIndex_f, fib_acc_cpu_req, key_index);
    return DRV_E_NONE;
}


STATIC int32
_drv_goldengate_fib_acc_prepare_lkp_mac(drv_fib_acc_in_t* in, uint32* fib_acc_cpu_req)
{
    SetFibHashKeyCpuReq(V, cpuKeyVrfId_f, fib_acc_cpu_req, in->mac.fid);
    SetFibHashKeyCpuReq(A, cpuKeyMac_f, fib_acc_cpu_req, in->mac.mac);
    return DRV_E_NONE;
}


STATIC int32
_drv_goldengate_fib_acc_prepare_flush_mac_by_port_vlan(drv_fib_acc_in_t* in, uint32* fib_acc_cpu_req)
{
    uint32 key_index = 0;
    uint32 del_type  = 0;
    uint32 del_mode  = 0;
    if ((0 != (in->mac_flush.flag & DRV_FIB_ACC_FLUSH_BY_PORT))
        && (0 != (in->mac_flush.flag & DRV_FIB_ACC_FLUSH_BY_VSI)))
    {    /*delete by port+vsi*/
        del_type = 0;
    }
    else if (0 != (in->mac_flush.flag & DRV_FIB_ACC_FLUSH_BY_PORT))
    {    /*delete by port*/
        del_type = 1;
    }
    else if (0 != (in->mac_flush.flag & DRV_FIB_ACC_FLUSH_BY_VSI))
    {    /*delete by vsi*/
        del_type = 2;
    }

    if (0 != (in->mac_flush.flag & DRV_FIB_ACC_FLUSH_APS))
    {
        key_index |= 1 << 3;
    }

    if (0 != (in->mac_flush.flag & DRV_FIB_ACC_FLUSH_LOGIC_PORT))
    {
        del_mode = 1;
    }


    if (0 != (in->mac_flush.flag & DRV_FIB_ACC_FLUSH_STATIC))
    {
        key_index |= 1;
    }
    if (0 != (in->mac_flush.flag & DRV_FIB_ACC_FLUSH_DYNAMIC))
    {
        key_index |= 1 << 1;
    }
    if (0 != (in->mac_flush.flag & DRV_FIB_ACC_FLUSH_MCAST))
    {
        key_index |= 1 << 2;
    }
    if (0 != (in->mac_flush.flag & DRV_FIB_ACC_FLUSH_DEC_STATIC_PORT_LIMIT))
    {
        key_index |= 1 << 4;
    }
    if (0 != (in->mac_flush.flag & DRV_FIB_ACC_FLUSH_DEC_DYNAMIC_PORT_LIMIT))
    {
        key_index |= 1 << 5;
    }
    if (0 != (in->mac_flush.flag & DRV_FIB_ACC_FLUSH_DEC_STATIC_VLAN_LIMIT))
    {
        key_index |= 1 << 6;
    }
    if (0 != (in->mac_flush.flag & DRV_FIB_ACC_FLUSH_DEC_DYNAMIC_VLAN_LIMIT))
    {
        key_index |= 1 << 7;
    }
    if (0 != (in->mac_flush.flag & DRV_FIB_ACC_FLUSH_DEC_DYNAMIC_LIMIT))
    {
        key_index |= 1 << 8;
    }
    if (0 != (in->mac_flush.flag & DRV_FIB_ACC_FLUSH_DEC_SYSTEM_LIMIT))
    {
        key_index |= 1 << 9;
    }
    SetFibHashKeyCpuReq(V, cpuKeyVrfId_f, fib_acc_cpu_req, in->mac_flush.fid);
    SetFibHashKeyCpuReq(V, cpuKeyDelType_f, fib_acc_cpu_req, del_type);
    SetFibHashKeyCpuReq(V, cpuKeyIndex_f, fib_acc_cpu_req, key_index);
    SetFibHashKeyCpuReq(V, cpuKeyPortDelMode_f, fib_acc_cpu_req, del_mode);
    SetFibHashKeyCpuReq(V, hashKeyCpuReqPort_f, fib_acc_cpu_req, in->mac_flush.port);
    return DRV_E_NONE;
}


STATIC int32
_drv_goldengate_fib_acc_prepare_flush_mac_all(drv_fib_acc_in_t* in, uint32* fib_acc_cpu_req)
{
    uint32 key_index = 0;
    if (0 != (in->mac_flush.flag & DRV_FIB_ACC_FLUSH_STATIC))
    {
        key_index |= 1;
    }
    if (0 != (in->mac_flush.flag & DRV_FIB_ACC_FLUSH_DYNAMIC))
    {
        key_index |= 1 << 1;
    }
    if (0 != (in->mac_flush.flag & DRV_FIB_ACC_FLUSH_MCAST))
    {
        key_index |= 1 << 2;
    }
    if (0 != (in->mac_flush.flag & DRV_FIB_ACC_FLUSH_DEC_STATIC_PORT_LIMIT))
    {
        key_index |= 1 << 4;
    }
    if (0 != (in->mac_flush.flag & DRV_FIB_ACC_FLUSH_DEC_DYNAMIC_PORT_LIMIT))
    {
        key_index |= 1 << 5;
    }
    if (0 != (in->mac_flush.flag & DRV_FIB_ACC_FLUSH_DEC_STATIC_VLAN_LIMIT))
    {
        key_index |= 1 << 6;
    }
    if (0 != (in->mac_flush.flag & DRV_FIB_ACC_FLUSH_DEC_DYNAMIC_VLAN_LIMIT))
    {
        key_index |= 1 << 7;
    }
    if (0 != (in->mac_flush.flag & DRV_FIB_ACC_FLUSH_DEC_DYNAMIC_LIMIT))
    {
        key_index |= 1 << 8;
    }
    if (0 != (in->mac_flush.flag & DRV_FIB_ACC_FLUSH_DEC_SYSTEM_LIMIT))
    {
        key_index |= 1 << 9;
    }
    SetFibHashKeyCpuReq(V, cpuKeyIndex_f, fib_acc_cpu_req, key_index);
    return DRV_E_NONE;
}


STATIC int32
_drv_goldengate_fib_acc_prepare_flush_mac_by_mac_port(drv_fib_acc_in_t* in, uint32* fib_acc_cpu_req)
{
    uint32 key_index = 0;
    uint32 del_type  = 0;
    uint32 del_mode  = 0;

    if ((0 != (in->mac_flush.flag & DRV_FIB_ACC_FLUSH_BY_PORT)))
    {    /*delete by mac+port*/
        del_type = 2;
    }
    else
    {    /*delete by mac*/
        del_type = 1;
    }

    if (0 != (in->mac_flush.flag & DRV_FIB_ACC_FLUSH_LOGIC_PORT))
    {
        del_mode = 1;
    }

    if (0 != (in->mac_flush.flag & DRV_FIB_ACC_FLUSH_STATIC))
    {
        key_index |= 1 << 0;
    }
    if (0 != (in->mac_flush.flag & DRV_FIB_ACC_FLUSH_DYNAMIC))
    {
        key_index |= 1 << 1;
    }
    if (0 != (in->mac_flush.flag & DRV_FIB_ACC_FLUSH_DEC_STATIC_PORT_LIMIT))
    {
        key_index |= 1 << 4;
    }
    if (0 != (in->mac_flush.flag & DRV_FIB_ACC_FLUSH_DEC_DYNAMIC_PORT_LIMIT))
    {
        key_index |= 1 << 5;
    }
    if (0 != (in->mac_flush.flag & DRV_FIB_ACC_FLUSH_DEC_STATIC_VLAN_LIMIT))
    {
        key_index |= 1 << 6;
    }
    if (0 != (in->mac_flush.flag & DRV_FIB_ACC_FLUSH_DEC_DYNAMIC_VLAN_LIMIT))
    {
        key_index |= 1 << 7;
    }
    if (0 != (in->mac_flush.flag & DRV_FIB_ACC_FLUSH_DEC_DYNAMIC_LIMIT))
    {
        key_index |= 1 << 8;
    }
    if (0 != (in->mac_flush.flag & DRV_FIB_ACC_FLUSH_DEC_SYSTEM_LIMIT))
    {
        key_index |= 1 << 9;
    }
    SetFibHashKeyCpuReq(A, cpuKeyMac_f, fib_acc_cpu_req, in->mac_flush.mac);
    SetFibHashKeyCpuReq(V, cpuKeyVrfId_f, fib_acc_cpu_req, in->mac_flush.fid);
    SetFibHashKeyCpuReq(V, cpuKeyDelType_f, fib_acc_cpu_req, del_type);
    SetFibHashKeyCpuReq(V, cpuKeyIndex_f, fib_acc_cpu_req, key_index);
    SetFibHashKeyCpuReq(V, cpuKeyPortDelMode_f, fib_acc_cpu_req, del_mode);
    SetFibHashKeyCpuReq(V, hashKeyCpuReqPort_f, fib_acc_cpu_req, in->mac_flush.port);
    return DRV_E_NONE;
}

STATIC int32
_drv_goldengate_fib_acc_prepare_lkp_fib0(drv_fib_acc_in_t* in, uint32* fib_acc_cpu_req)
{
    drv_fib_acc_fib_data_t fib_data;
    hw_mac_addr_t          hw_mac     = { 0 };
    uint32                 key_length = 0;
    uint32                 tbl_id     = 0;

    DRV_PTR_VALID_CHECK(in->fib.data);
    sal_memset(&fib_data, 0, sizeof(drv_fib_acc_fib_data_t));

    hw_mac[0] = in->fib.key_type;
    if ((in->fib.key_type == FIBHOST0PRIMARYHASHTYPE_FCOE) || (in->fib.key_type == FIBHOST0PRIMARYHASHTYPE_IPV4)
        || (in->fib.key_type == FIBHOST0PRIMARYHASHTYPE_MAC) || (in->fib.key_type == FIBHOST0PRIMARYHASHTYPE_TRILL))
    {
        hw_mac[0] |= HASH_KEY_LENGTH_MODE_SINGLE << 3;
        key_length = DRV_HASH_85BIT_KEY_LENGTH;
        tbl_id     = DsFibHost0MacHashKey_t;
    }
    else
    {
        hw_mac[0] |= HASH_KEY_LENGTH_MODE_DOUBLE << 3;
        key_length = DRV_HASH_170BIT_KEY_LENGTH;
        tbl_id     = DsFibHost0MacIpv6McastHashKey_t;
    }

    drv_goldengate_model_hash_combined_key((uint8 *) fib_data, (uint8 *) in->fib.data, key_length, tbl_id);
    drv_goldengate_model_hash_mask_key((uint8 *) fib_data, (uint8 *) fib_data, HASH_MODULE_FIB_HOST0, in->fib.key_type);

    SetFibHashKeyCpuReq(A, cpuKeyMac_f, fib_acc_cpu_req, hw_mac);
    SetFibHashKeyCpuReq(A, writeData_f, fib_acc_cpu_req, fib_data);

    return DRV_E_NONE;
}


STATIC int32
_drv_goldengate_fib_acc_prepare_write_fib0_by_key(drv_fib_acc_in_t* in, uint32* fib_acc_cpu_req)
{
    drv_fib_acc_fib_data_t fib_data;
    hw_mac_addr_t          hw_mac     = { 0 };
    uint32                 key_length = 0;
    uint32                 tbl_id     = 0;

    DRV_PTR_VALID_CHECK(in->fib.data);
    sal_memset(&fib_data, 0, sizeof(drv_fib_acc_fib_data_t));

    hw_mac[0] = in->fib.key_type;
    if ((in->fib.key_type == FIBHOST0PRIMARYHASHTYPE_FCOE) || (in->fib.key_type == FIBHOST0PRIMARYHASHTYPE_IPV4)
        || (in->fib.key_type == FIBHOST0PRIMARYHASHTYPE_MAC) || (in->fib.key_type == FIBHOST0PRIMARYHASHTYPE_TRILL))
    {
        hw_mac[0] |= HASH_KEY_LENGTH_MODE_SINGLE << 3;
        key_length = DRV_HASH_85BIT_KEY_LENGTH;
        tbl_id     = DsFibHost0MacHashKey_t;
    }
    else
    {
        hw_mac[0] |= HASH_KEY_LENGTH_MODE_DOUBLE << 3;
        key_length = DRV_HASH_170BIT_KEY_LENGTH;
        tbl_id     = DsFibHost0MacIpv6McastHashKey_t;
    }

    drv_goldengate_model_hash_combined_key((uint8 *) fib_data, (uint8 *) in->fib.data, key_length, tbl_id);

    SetFibHashKeyCpuReq(V, cpuKeyPortDelMode_f, fib_acc_cpu_req, in->fib.overwrite_en);
    SetFibHashKeyCpuReq(A, cpuKeyMac_f, fib_acc_cpu_req, hw_mac);
    SetFibHashKeyCpuReq(A, writeData_f, fib_acc_cpu_req, fib_data);

    return DRV_E_NONE;
}

STATIC int32
_drv_goldengate_fib_acc_prepare_read_fib0_by_idx(drv_fib_acc_in_t* in, uint32* fib_acc_cpu_req)
{
    uint32        length_mode= 0;
    uint32        length = 0;
    uint32        c_length = 0;
    uint32        count      = 0;
    hw_mac_addr_t hw_mac     = { 0 };

    hw_mac[0] = in->rw.key_index;
    count     = (in->rw.key_index % 4) / 2;
    DRV_IF_ERROR_RETURN(_drv_goldengate_get_length_by_tbl_id(in->rw.tbl_id, &length_mode, &length,&c_length));


    SetFibHashKeyCpuReq(A, cpuKeyMac_f, fib_acc_cpu_req, hw_mac);

    /* only cmodel need spec and rtl do not need */
    if (1 == count)
    {
        SetFibHashKeyCpuReq(V, staticCountEn_f, fib_acc_cpu_req, length_mode);
    }
    else
    {
        SetFibHashKeyCpuReq(V, cpuKeyPortDelMode_f, fib_acc_cpu_req, length_mode);
    }
    return DRV_E_NONE;
}


STATIC int32
_drv_goldengate_fib_acc_prepare_write_fib0_by_idx(uint8 chip_id, drv_fib_acc_in_t* in, uint32* fib_acc_cpu_req)
{
    drv_fib_acc_rw_data_t rw_data;
    drv_fib_acc_rw_data_t tmp_rw_data;
    drv_fib_acc_in_t      acc_read_in;
    drv_fib_acc_out_t     acc_read_out;
    uint32                key_length_mode[2] = { 0 };

    uint32                del_mode   = 0;
    uint32                count      = 0;
    uint32                key_length = 0;
    uint32                tbl_id     = 0;
    uint32                key_index  = 0;
    uint8                 shift      = 0;

    hw_mac_addr_t         hw_mac = { 0 };

    DRV_PTR_VALID_CHECK(in->rw.data);

    sal_memset(&rw_data, 0, sizeof(drv_fib_acc_rw_data_t));
    sal_memset(&tmp_rw_data, 0, sizeof(drv_fib_acc_rw_data_t));
    sal_memset(&acc_read_in, 0, sizeof(drv_fib_acc_in_t));
    sal_memset(&acc_read_out, 0, sizeof(drv_fib_acc_out_t));


    hw_mac[0] = in->rw.key_index;
    del_mode  = TABLE_ENTRY_SIZE(in->rw.tbl_id) / 12;
    count     = (in->rw.key_index % 4) / 2;

    switch (del_mode)
    {
    case 1:
        key_length_mode[count] = HASH_KEY_LENGTH_MODE_SINGLE;
        key_length             = SINGLE_KEY_BYTE;
        break;

    case 2:
        key_length_mode[count] = HASH_KEY_LENGTH_MODE_DOUBLE;
        key_length             = SINGLE_KEY_BYTE * 2;
        if (in->rw.key_index % 2 != 0)
        {
            return DRV_E_INVALID_INDEX;
        }
        break;

    case 4:
        key_length_mode[count] = HASH_KEY_LENGTH_MODE_QUAD;
        key_length             = SINGLE_KEY_BYTE * 4;
        if (in->rw.key_index % 4 != 0)
        {
            return DRV_E_INVALID_INDEX;
        }
        break;
    default:
        return DRV_E_INVALID_TBL;
    }

    del_mode  = key_length_mode[count];
    key_index = in->rw.key_index - (in->rw.key_index % 4);

    if (in->rw.key_index < FIB_HOST0_CAM_NUM)
    {
        shift                    = (in->rw.key_index % 4);
        acc_read_in.rw.tbl_id    = DsFibHost0HashCam_t;
        acc_read_in.rw.key_index = in->rw.key_index - (in->rw.key_index % 4);
        DRV_IF_ERROR_RETURN(drv_goldengate_fib_acc_no_lock(chip_id, DRV_FIB_ACC_READ_FIB0_BY_IDX, &acc_read_in, &acc_read_out));
        sal_memcpy(tmp_rw_data, acc_read_out.read.data, sizeof(drv_fib_acc_rw_data_t));
        sal_memcpy(((uint32 *) tmp_rw_data + shift * 3), in->rw.data, key_length);
    }
    else
    {
        DRV_IF_ERROR_RETURN(_drv_goldengate_fib_acc_read_key(chip_id, key_index, (uint32 *) tmp_rw_data, key_length_mode));
        key_length_mode[count] = del_mode;

        if (HASH_KEY_LENGTH_MODE_DOUBLE == key_length_mode[count])
        {
#if (SDK_WORK_PLATFORM == 1)
            shift = 1 - (in->rw.key_index % 4) / 2;
#else
            shift = (in->rw.key_index % 4) / 2;
#endif
            sal_memcpy(((uint32 *) tmp_rw_data + shift * 6), in->rw.data, key_length);
        }
        else
        {
#if (SDK_WORK_PLATFORM == 1)
            shift = 3 - (in->rw.key_index % 4);
#else
            shift = (in->rw.key_index % 4);
#endif
            sal_memcpy(((uint32 *) tmp_rw_data + shift * 3), in->rw.data, key_length);
        }
    }

    tbl_id     = DsFibHost0HashCam_t;
    key_length = DRV_HASH_170BIT_KEY_LENGTH * 2;
    drv_goldengate_model_hash_combined_key((uint8 *) rw_data, (uint8 *) tmp_rw_data, key_length, tbl_id);

    SetFibHashKeyCpuReq(A, cpuKeyMac_f, fib_acc_cpu_req, hw_mac);
    SetFibHashKeyCpuReq(A, writeData_f, fib_acc_cpu_req, rw_data);

    /* only cmodel need spec and rtl do not need */
    SetFibHashKeyCpuReq(V, cpuKeyPortDelMode_f, fib_acc_cpu_req, key_length_mode[0]);
    SetFibHashKeyCpuReq(V, staticCountEn_f, fib_acc_cpu_req, key_length_mode[1]);
    return DRV_E_NONE;
}


STATIC int32
_drv_goldengate_fib_acc_prepare_update_mac_limit(drv_fib_acc_in_t* in, uint32* fib_acc_cpu_req)
{
    hw_mac_addr_t hw_mac       = { 0 };
    uint32        del_type     = 0;
    uint32        cpu_req_port = 0;

    if (in->mac_limit.type == DRV_FIB_ACC_UPDATE_PORT_VLAN_MAC_LIMIT)
    {
        del_type      = 1;
        cpu_req_port  = in->mac_limit.mac_securiy_index;
        cpu_req_port |= (in->mac_limit.mac_securiy_array_index << 8);

        SetFibHashKeyCpuReq(V, cpuKeyVrfId_f, fib_acc_cpu_req, in->mac_limit.mac_limit_index);
    }
    else if (in->mac_limit.type == DRV_FIB_ACC_UPDATE_DYNAMIC_MAC_LIMIT)
    {
        del_type = 2;
    }
    else if (in->mac_limit.type == DRV_FIB_ACC_UPDATE_PROFILE_MAC_LIMIT)
    {
        del_type = 3;
    }

    cpu_req_port |= (in->mac_limit.is_decrease << 13);

    hw_mac[0]  = in->mac_limit.mac_limit_update_en;
    hw_mac[0] |= (in->mac_limit.mac_security_update_en << 1);

    SetFibHashKeyCpuReq(V, hashKeyCpuReqPort_f, fib_acc_cpu_req, cpu_req_port);
    SetFibHashKeyCpuReq(V, cpuKeyIndex_f, fib_acc_cpu_req, in->mac_limit.update_counter);
    SetFibHashKeyCpuReq(V, cpuKeyPortDelMode_f, fib_acc_cpu_req, in->mac_limit.security_status);
    SetFibHashKeyCpuReq(V, cpuKeyDelType_f, fib_acc_cpu_req, del_type);
    SetFibHashKeyCpuReq(A, cpuKeyMac_f, fib_acc_cpu_req, hw_mac);
    return DRV_E_NONE;
}


STATIC int32
_drv_goldengate_fib_acc_prepare_read_mac_limit(drv_fib_acc_in_t* in, uint32* fib_acc_cpu_req)
{
    uint32 mac_limit_type = 0;
    uint32        cpu_req_port = 0;
    if (in->mac_limit.type == DRV_FIB_ACC_UPDATE_PORT_VLAN_MAC_LIMIT)
    {
        mac_limit_type = 1;
        cpu_req_port   = in->mac_limit.mac_securiy_index;
        cpu_req_port  |= (in->mac_limit.mac_securiy_array_index << 8);

        SetFibHashKeyCpuReq(V, cpuKeyVrfId_f, fib_acc_cpu_req, in->mac_limit.mac_limit_index);
        SetFibHashKeyCpuReq(V, hashKeyCpuReqPort_f, fib_acc_cpu_req, cpu_req_port);
    }
    else if (in->mac_limit.type == DRV_FIB_ACC_UPDATE_DYNAMIC_MAC_LIMIT)
    {
        mac_limit_type = 2;
    }
    else if (in->mac_limit.type == DRV_FIB_ACC_UPDATE_PROFILE_MAC_LIMIT)
    {
        mac_limit_type = 3;
    }
    SetFibHashKeyCpuReq(V, cpuKeyDelType_f, fib_acc_cpu_req, mac_limit_type);
    return DRV_E_NONE;
}


STATIC int32
_drv_goldengate_fib_acc_prepare_write_mac_limit(drv_fib_acc_in_t* in, uint32* fib_acc_cpu_req)
{
    uint32 del_type = 0;
    if (in->mac_limit.type == DRV_FIB_ACC_UPDATE_PORT_VLAN_MAC_LIMIT)
    {
        del_type = 1;

        SetFibHashKeyCpuReq(V, cpuKeyVrfId_f, fib_acc_cpu_req, in->mac_limit.mac_limit_index);
    }
    else if (in->mac_limit.type == DRV_FIB_ACC_UPDATE_DYNAMIC_MAC_LIMIT)
    {
        del_type = 2;
    }
    else if (in->mac_limit.type == DRV_FIB_ACC_UPDATE_PROFILE_MAC_LIMIT)
    {
        del_type = 3;
    }

    SetFibHashKeyCpuReq(V, cpuKeyIndex_f, fib_acc_cpu_req, in->mac_limit.update_counter);
    SetFibHashKeyCpuReq(V, cpuKeyDelType_f, fib_acc_cpu_req, del_type);

    return DRV_E_NONE;
}




STATIC int32
_drv_goldengate_fib_acc_prepare_dump_mac_by_port_vlan(drv_fib_acc_in_t* in, uint32* fib_acc_cpu_req)
{
    uint32        del_type  = 0;
    uint32        del_mode  = 0;
    uint32        key_index = 0;
    hw_mac_addr_t hw_mac    = { 0 };

    hw_mac[0] = (in->dump.min_index & 0x7FFFF) |
                ((in->dump.max_index & 0x1f) << 19) |
                (((in->dump.max_index >> 5) & 0xff) << 24);

    hw_mac[1] = (in->dump.max_index >> 13) & 0x3F;

    key_index |= 1<<4;
    if ((0 != (in->dump.flag & DRV_FIB_ACC_DUMP_BY_PORT))
        && (0 != (in->dump.flag & DRV_FIB_ACC_DUMP_BY_VSI)))
    {    /*dump by port+vsi*/
        del_type = 0;
    }
    else if (0 != (in->dump.flag & DRV_FIB_ACC_DUMP_BY_PORT))
    {    /*dump by port*/
        del_type = 1;
    }
    else if (0 != (in->dump.flag & DRV_FIB_ACC_DUMP_BY_VSI))
    {    /*dump by vsi*/
        del_type = 2;
    }

    if (0 != (in->dump.flag & DRV_FIB_ACC_DUMP_LOGIC_PORT))
    {
        del_mode = 1;
    }

    if (0 != (in->dump.flag & DRV_FIB_ACC_DUMP_STATIC))
    {
        key_index |= 1;
    }
    if (0 != (in->dump.flag & DRV_FIB_ACC_DUMP_DYNAMIC))
    {
        key_index |= 1 << 1;
    }
    if (0 != (in->dump.flag & DRV_FIB_ACC_DUMP_MCAST))
    {
        key_index |= 1 << 2;
    }

    SetFibHashKeyCpuReq(V, cpuKeyVrfId_f, fib_acc_cpu_req, in->dump.fid);
    SetFibHashKeyCpuReq(V, cpuKeyDelType_f, fib_acc_cpu_req, del_type);
    SetFibHashKeyCpuReq(V, cpuKeyIndex_f, fib_acc_cpu_req, key_index);
    SetFibHashKeyCpuReq(V, cpuKeyPortDelMode_f, fib_acc_cpu_req, del_mode);
    SetFibHashKeyCpuReq(V, hashKeyCpuReqPort_f, fib_acc_cpu_req, in->dump.port);
    SetFibHashKeyCpuReq(A, cpuKeyMac_f, fib_acc_cpu_req, hw_mac);
    SetFibHashKeyCpuReq(V, cpuKeyDsAdIndex_f, fib_acc_cpu_req, in->dump.threshold_cnt);
    return DRV_E_NONE;
}


STATIC int32
_drv_goldengate_fib_acc_prepare_dump_mac_all(drv_fib_acc_in_t* in, uint32* fib_acc_cpu_req)
{
    hw_mac_addr_t hw_mac    = { 0 };
    uint32        key_index = 0;

    hw_mac[0] = (in->dump.min_index & 0x7FFFF) |
                ((in->dump.max_index & 0x1f) << 19) |
                (((in->dump.max_index >> 5) & 0xff) << 24);

    hw_mac[1] = (in->dump.max_index >> 13) & 0x3F;

    key_index |= 1<<4;
    if (0 != (in->dump.flag & DRV_FIB_ACC_DUMP_STATIC))
    {
        key_index |= 1;
    }
    if (0 != (in->dump.flag & DRV_FIB_ACC_DUMP_DYNAMIC))
    {
        key_index |= 1 << 1;
    }
    if (0 != (in->dump.flag & DRV_FIB_ACC_DUMP_MCAST))
    {
        key_index |= 1 << 2;
    }
    SetFibHashKeyCpuReq(V, cpuKeyPortDelMode_f, fib_acc_cpu_req, 1);  /*means dump all*/
    SetFibHashKeyCpuReq(V, cpuKeyIndex_f, fib_acc_cpu_req, key_index);
    SetFibHashKeyCpuReq(A, cpuKeyMac_f, fib_acc_cpu_req, hw_mac);
    SetFibHashKeyCpuReq(V, cpuKeyDsAdIndex_f, fib_acc_cpu_req, in->dump.threshold_cnt);
    return DRV_E_NONE;
}



STATIC int32
_drv_goldengate_fib_acc_get_result_lkp_mac(uint32* fib_acc_cpu_rlt, drv_fib_acc_out_t* out)
{
    out->mac.hit       = GetFibHashKeyCpuResult(V, hashCpuKeyHit_f, fib_acc_cpu_rlt);
    out->mac.key_index = GetFibHashKeyCpuResult(V, hashCpuLuIndex_f, fib_acc_cpu_rlt);
    out->mac.conflict  = GetFibHashKeyCpuResult(V, hashCpuConflictValid_f, fib_acc_cpu_rlt);
    out->mac.ad_index  = GetFibHashKeyCpuResult(V, hashCpuDsIndex_f, fib_acc_cpu_rlt);
    out->mac.pending   = GetFibHashKeyCpuResult(V, pending_f, fib_acc_cpu_rlt);
    out->mac.free      = ((!out->mac.hit) && (!out->mac.conflict));
    return DRV_E_NONE;
}

STATIC int32
_drv_goldengate_fib_acc_get_result_write_mac_by_key(uint32* fib_acc_cpu_rlt, drv_fib_acc_out_t* out)
{
    out->mac.key_index = GetFibHashKeyCpuResult(V, hashCpuLuIndex_f, fib_acc_cpu_rlt);
    out->mac.pending   = GetFibHashKeyCpuResult(V, pending_f, fib_acc_cpu_rlt);
    out->mac.conflict  = GetFibHashKeyCpuResult(V, conflict_f, fib_acc_cpu_rlt);

    return DRV_E_NONE;
}

STATIC int32
_drv_goldengate_fib_acc_get_result_del_mac_by_key(uint32* fib_acc_cpu_rlt, drv_fib_acc_out_t* out)
{
    out->mac.key_index = GetFibHashKeyCpuResult(V, hashCpuLuIndex_f, fib_acc_cpu_rlt);
    out->mac.hit       = GetFibHashKeyCpuResult(V, hashCpuKeyHit_f, fib_acc_cpu_rlt);
    out->mac.conflict  = GetFibHashKeyCpuResult(V, conflict_f, fib_acc_cpu_rlt);
    out->mac.free      = ((!out->mac.hit) && (!out->mac.conflict));

    return DRV_E_NONE;
}
STATIC int32
_drv_goldengate_fib_acc_get_result_lkp_fib0(uint32* fib_acc_cpu_rlt, drv_fib_acc_out_t* out)
{
    out->fib.hit       = GetFibHashKeyCpuResult(V, hashCpuKeyHit_f, fib_acc_cpu_rlt);
    out->fib.key_index = GetFibHashKeyCpuResult(V, hashCpuLuIndex_f, fib_acc_cpu_rlt);
    out->fib.conflict  = GetFibHashKeyCpuResult(V, hashCpuConflictValid_f, fib_acc_cpu_rlt);
    out->fib.ad_index  = GetFibHashKeyCpuResult(V, hashCpuDsIndex_f, fib_acc_cpu_rlt);
    out->fib.free      = ((!out->mac.hit) && (!out->mac.conflict));
    return DRV_E_NONE;
}

STATIC int32
_drv_goldengate_fib_acc_get_result_read_fib0_by_idx(drv_fib_acc_in_t* in, uint32* fib_acc_cpu_rlt, drv_fib_acc_out_t* out)
{
    uint8         shift = 0;
    uint32        length_mode= 0;
    uint32        length = 0;
    uint32        c_length = 0;
    drv_fib_acc_rw_data_t rw_data;
    drv_fib_acc_rw_data_t tmp_rw_data;

    sal_memset(&rw_data, 0, sizeof(rw_data));
    sal_memset(&tmp_rw_data, 0, sizeof(tmp_rw_data));

    GetFibHashKeyCpuResult(A, readData_f, fib_acc_cpu_rlt, rw_data);

    drv_goldengate_model_hash_un_combined_key((uint8 *) tmp_rw_data, (uint8 *) rw_data, DRV_HASH_340BIT_KEY_LENGTH, DsFibHost0HashCam_t);


    DRV_IF_ERROR_RETURN(_drv_goldengate_get_length_by_tbl_id(in->rw.tbl_id, &length_mode, &length,&c_length));


    /* asic only read 340 key drv need to adjust the key */
    if (in->rw.key_index < FIB_HOST0_CAM_NUM)
    {
        shift = (in->rw.key_index % 4);
        sal_memcpy(out->read.data, ((uint32 *) tmp_rw_data + shift * 3), length);
    }
    else
    {
        if (HASH_KEY_LENGTH_MODE_DOUBLE == length_mode)
        {
#if (SDK_WORK_PLATFORM == 1)
            shift = 1 - (in->rw.key_index % 4) / 2;
#else
            shift = (in->rw.key_index % 4) / 2;
#endif
            sal_memcpy(out->read.data, ((uint32 *) tmp_rw_data + shift * 6), length);
        }
        else
        {
#if (SDK_WORK_PLATFORM == 1)
            shift = 3 - (in->rw.key_index % 4);
#else
            shift = (in->rw.key_index % 4);
#endif
            sal_memcpy(out->read.data, ((uint32 *) tmp_rw_data + shift * 3), length);
        }
    }

    return DRV_E_NONE;
}
STATIC int32
_drv_goldengate_fib_acc_get_result_write_fib0_by_key(uint32* fib_acc_cpu_rlt, drv_fib_acc_out_t* out)
{
    out->fib.key_index = GetFibHashKeyCpuResult(V, hashCpuLuIndex_f, fib_acc_cpu_rlt);
    out->fib.conflict  = GetFibHashKeyCpuResult(V, conflict_f, fib_acc_cpu_rlt);

    return DRV_E_NONE;
}

STATIC int32
_drv_goldengate_fib_acc_get_result_read_mac_limit(uint32* fib_acc_cpu_rlt, drv_fib_acc_out_t* out)
{
    out->mac_limit.counter         = GetFibHashKeyCpuResult(V, hashCpuLuIndex_f, fib_acc_cpu_rlt);
    out->mac_limit.security_status = GetFibHashKeyCpuResult(V, hashCpuKeyHit_f, fib_acc_cpu_rlt);
    return DRV_E_NONE;
}

STATIC int32
_drv_goldengate_fib_acc_get_result_dump_mac_all(uint32* fib_acc_cpu_rlt, drv_fib_acc_out_t* out)
{
    uint32 confict = 0;
    uint32 pending = 0;

    confict = GetFibHashKeyCpuResult(V, conflict_f, fib_acc_cpu_rlt);
    pending = GetFibHashKeyCpuResult(V, pending_f, fib_acc_cpu_rlt);

    out->dump.last_index = GetFibHashKeyCpuResult(V, hashCpuLuIndex_f, fib_acc_cpu_rlt);
    out->dump.dma_full   = (confict && (!pending));
    out->dump.continuing = (!confict && (pending));
    out->dump.count      = GetFibHashKeyCpuResult(V, hashCpuDsIndex_f, fib_acc_cpu_rlt);
    return DRV_E_NONE;
}

typedef struct
{
    DRV_FIB_ACC_PREPARE_FN_t fn;
    uint32                   req_type;
}drv_fib_acc_prepare_t;

STATIC int32
drv_goldengate_fib_acc_prepare(uint8 chip_id, uint8 acc_type, drv_fib_acc_in_t* in, uint32* fib_acc_cpu_req)
{
    uint32 cmd = 0;
    FibHashKeyCpuReq_m fib_req;

    drv_fib_acc_prepare_t prepare[] =
    {
        {_drv_goldengate_fib_acc_prepare_write_mac_by_idx, 0},        /*DRV_FIB_ACC_WRITE_MAC_BY_IDX      */
        {_drv_goldengate_fib_acc_prepare_write_mac_by_key, 1},        /*DRV_FIB_ACC_WRITE_MAC_BY_KEY      */
        {_drv_goldengate_fib_acc_prepare_del_mac_by_idx,   2},        /*DRV_FIB_ACC_DEL_MAC_BY_IDX        */
        {_drv_goldengate_fib_acc_prepare_del_mac_by_key,   3},        /*DRV_FIB_ACC_DEL_MAC_BY_KEY        */
        {_drv_goldengate_fib_acc_prepare_lkp_mac,          6},        /*DRV_FIB_ACC_LKP_MAC               */
        {_drv_goldengate_fib_acc_prepare_flush_mac_by_port_vlan,4},   /*DRV_FIB_ACC_FLUSH_MAC_BY_PORT_VLAN*/
        {_drv_goldengate_fib_acc_prepare_flush_mac_all,    5},        /*DRV_FIB_ACC_FLUSH_MAC_ALL         */
        {_drv_goldengate_fib_acc_prepare_flush_mac_by_mac_port, 14},  /*DRV_FIB_ACC_FLUSH_MAC_BY_MAC_PORT */
        {_drv_goldengate_fib_acc_prepare_lkp_fib0,         7},        /*DRV_FIB_ACC_LKP_FIB0              */
        {_drv_goldengate_fib_acc_prepare_write_fib0_by_key,10},       /*DRV_FIB_ACC_WRITE_FIB0_BY_KEY     */
        {_drv_goldengate_fib_acc_prepare_read_fib0_by_idx, 8},        /*DRV_FIB_ACC_READ_FIB0_BY_IDX      */
        {NULL                                             ,9},        /*DRV_FIB_ACC_WRITE_FIB0_BY_IDX     */
        {_drv_goldengate_fib_acc_prepare_update_mac_limit, 11},        /*DRV_FIB_ACC_UPDATE_MAC_LIMIT      */
        {_drv_goldengate_fib_acc_prepare_read_mac_limit,   12},        /*DRV_FIB_ACC_READ_MAC_LIMIT        */
        {_drv_goldengate_fib_acc_prepare_write_mac_limit,  13},        /*DRV_FIB_ACC_WRITE_MAC_LIMIT       */
        {_drv_goldengate_fib_acc_prepare_dump_mac_by_port_vlan,15},    /*DRV_FIB_ACC_DUMP_MAC_BY_PORT_VLAN */
        {_drv_goldengate_fib_acc_prepare_dump_mac_all,      5}         /*DRV_FIB_ACC_DUMP_MAC_ALL          */
    };

    /* check valid bit clear */
    cmd = DRV_IOR(FibHashKeyCpuReq_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, &fib_req));
    if (GetFibHashKeyCpuReq(V,hashKeyCpuReqValid_f,&fib_req))
    {
        return DRV_E_OCCPANCY;
    }

    SetFibHashKeyCpuReq(V, hashKeyCpuReqType_f, fib_acc_cpu_req, prepare[acc_type].req_type);
    SetFibHashKeyCpuReq(V, hashKeyCpuReqValid_f, fib_acc_cpu_req, 1);

    if (DRV_FIB_ACC_WRITE_FIB0_BY_IDX == acc_type)
    {
        return _drv_goldengate_fib_acc_prepare_write_fib0_by_idx(chip_id, in, fib_acc_cpu_req);
    }
    else
    {
        return prepare[acc_type].fn(in, fib_acc_cpu_req);
    }
}

STATIC int32
drv_goldengate_fib_acc_process(uint8 chip_id, uint32* i_fib_acc_cpu_req, uint32* o_fib_acc_cpu_rlt)
{

    DRV_PTR_VALID_CHECK(i_fib_acc_cpu_req);
    DRV_PTR_VALID_CHECK(o_fib_acc_cpu_rlt);

    DRV_PTR_VALID_CHECK(drv_gg_io_api.drv_fib_acc_process);

    DRV_IF_ERROR_RETURN(drv_gg_io_api.drv_fib_acc_process(chip_id, i_fib_acc_cpu_req,o_fib_acc_cpu_rlt, 1));



    return DRV_E_NONE;
}



STATIC int32
drv_goldengate_fib_acc_get_result(uint8 acc_type, drv_fib_acc_in_t* in, uint32* fib_acc_cpu_result, drv_fib_acc_out_t* out)
{
    int32 ret = 0;

    DRV_FIB_ACC_GET_RESULT_FN_t get_result[] =
    {
        NULL,                                                 /*DRV_FIB_ACC_WRITE_MAC_BY_IDX      */
        _drv_goldengate_fib_acc_get_result_write_mac_by_key,  /*DRV_FIB_ACC_WRITE_MAC_BY_KEY      */
        NULL,                                                 /*DRV_FIB_ACC_DEL_MAC_BY_IDX        */
        _drv_goldengate_fib_acc_get_result_del_mac_by_key,    /*DRV_FIB_ACC_DEL_MAC_BY_KEY        */
        _drv_goldengate_fib_acc_get_result_lkp_mac,           /*DRV_FIB_ACC_LKP_MAC               */
        NULL,                                                 /*DRV_FIB_ACC_FLUSH_MAC_BY_PORT_VLAN*/
        NULL,                                                 /*DRV_FIB_ACC_FLUSH_MAC_ALL         */
        NULL,                                                 /*DRV_FIB_ACC_FLUSH_MAC_BY_MAC_PORT */
        _drv_goldengate_fib_acc_get_result_lkp_fib0,          /*DRV_FIB_ACC_LKP_FIB0              */
        _drv_goldengate_fib_acc_get_result_write_fib0_by_key, /*DRV_FIB_ACC_WRITE_FIB0_BY_KEY     */
        NULL,                                                 /*DRV_FIB_ACC_READ_FIB0_BY_IDX      */
        NULL,                                                 /*DRV_FIB_ACC_WRITE_FIB0_BY_IDX     */
        NULL,                                                 /*DRV_FIB_ACC_UPDATE_MAC_LIMIT      */
        _drv_goldengate_fib_acc_get_result_read_mac_limit,    /*DRV_FIB_ACC_READ_MAC_LIMIT        */
        NULL,                                                 /*DRV_FIB_ACC_WRITE_MAC_LIMIT       */
        _drv_goldengate_fib_acc_get_result_dump_mac_all,      /*DRV_FIB_ACC_DUMP_MAC_BY_PORT_VLAN */
        _drv_goldengate_fib_acc_get_result_dump_mac_all       /*DRV_FIB_ACC_DUMP_MAC_ALL          */
    };

    DRV_PTR_VALID_CHECK(out);
    sal_memset(out, 0, sizeof(drv_fib_acc_out_t));

    if (DRV_FIB_ACC_READ_FIB0_BY_IDX == acc_type)
    {
        ret = _drv_goldengate_fib_acc_get_result_read_fib0_by_idx(in, fib_acc_cpu_result, out);
    }
    else if (get_result[acc_type])
    {
        ret = get_result[acc_type](fib_acc_cpu_result, out);
    }

    return ret;
}

/*
STATIC int32
drv_goldengate_fib_acc_cam_result(uint8 acc_type, drv_fib_acc_in_t* in,  drv_fib_acc_out_t* out)
{

    uint32 cmd = 0;
    uint32 bitmap = 0;
    uint8 is_del  = 0;


    is_del = in.fib.is_del;
    // check fib acc bitmap
    cmd = DRV_IOR(FibAccelerationCtl_t, FibAccelerationCtl_camDisableBitmap_f);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(0, 0, cmd, &bitmap));

    //bit
    //bit[0]: mac
    //bit[1]: ipv4 uc host
    //bit[2]: ipv4 l2 mc
    //bit[3]: ipv4 mc
    //bit[4]: ipv6 uc hot
    //bit[5]: ipv6 l2 mc
    //bit[6]: ipv6 mc
    //bit[7]: fcoe
    //bit[8]: trill
    //
    CLEAR_BIT(bitmap, 1);
    CLEAR_BIT(bitmap, 4);
    CLEAR_BIT(bitmap, 7);
    CLEAR_BIT(bitmap, 8);
    if (0 == bitmap)
    {//both mac and ip use cam

    }
    else
    {
        if(out.fib.conflict)
        out.fib.key_index

        if(out.fib.conflict)
        {

        }
    }

    return DRV_E_NONE;
}
*/

int32
drv_goldengate_fib_acc(uint8 chip_id, uint8 acc_type, drv_fib_acc_in_t* in, drv_fib_acc_out_t* out)
{
    FibHashKeyCpuResult_m fib_acc_cpu_rlt;
    FibHashKeyCpuReq_m    fib_acc_cpu_req;

    sal_memset(&fib_acc_cpu_rlt, 0 ,sizeof(FibHashKeyCpuResult_m));
    sal_memset(&fib_acc_cpu_req, 0 ,sizeof(FibHashKeyCpuReq_m));

    if (g_drv_gg_io_master.chip_agent_cb&&(TRUE == gg_client_init_done))
    {
        chip_io_para_t para;
        para.op = CHIP_IO_OP_FIB_ACC;
        para.u.fib_acc.chip_id = chip_id;
        para.u.fib_acc.acc_type = acc_type;
        para.u.fib_acc.in = in;
        para.u.fib_acc.out = out;
        return g_drv_gg_io_master.chip_agent_cb(0, &para);
    }
    else if(DRV_CHIP_AGT_MODE_CLIENT == drv_goldengate_chip_agent_mode())
    {
        return 0;
    }

    FIB_ACC_LOCK;
    DRV_IF_ERROR_RETURN_WITH_UNLOCK(drv_goldengate_fib_acc_prepare(chip_id, acc_type, in, (uint32*)&fib_acc_cpu_req), gg_fib_acc_mutex);
    DRV_IF_ERROR_RETURN_WITH_UNLOCK(drv_goldengate_fib_acc_process(chip_id, (uint32*)&fib_acc_cpu_req, (uint32*)&fib_acc_cpu_rlt), gg_fib_acc_mutex);
    DRV_IF_ERROR_RETURN_WITH_UNLOCK(drv_goldengate_fib_acc_get_result(acc_type, in, (uint32*)&fib_acc_cpu_rlt,out), gg_fib_acc_mutex);
    FIB_ACC_UNLOCK;

    return DRV_E_NONE;
}

int32
drv_goldengate_fib_acc_no_lock(uint8 chip_id, uint8 acc_type, drv_fib_acc_in_t* in, drv_fib_acc_out_t* out)
{
    FibHashKeyCpuResult_m fib_acc_cpu_rlt;
    FibHashKeyCpuReq_m    fib_acc_cpu_req;

    sal_memset(&fib_acc_cpu_rlt, 0 ,sizeof(FibHashKeyCpuResult_m));
    sal_memset(&fib_acc_cpu_req, 0 ,sizeof(FibHashKeyCpuReq_m));

    DRV_IF_ERROR_RETURN(drv_goldengate_fib_acc_prepare(chip_id, acc_type, in, (uint32*)&fib_acc_cpu_req));
    DRV_IF_ERROR_RETURN(drv_goldengate_fib_acc_process(chip_id, (uint32*)&fib_acc_cpu_req, (uint32*)&fib_acc_cpu_rlt));
    DRV_IF_ERROR_RETURN(drv_goldengate_fib_acc_get_result(acc_type, in, (uint32*)&fib_acc_cpu_rlt,out));

    return DRV_E_NONE;
}


#define  _DRV_CPU_ACC_M_

typedef uint32 max_ds_t[MAX_ENTRY_BYTE/4];

typedef struct
{
    max_ds_t req;
    max_ds_t req1;
    max_ds_t rlt;

    uint32 req_type;
    uint32 cam_index;
    uint32 cam_step;
    uint32 cam_tbl;
    uint32 by_key;
    uint8  big_type;
    uint8  small_type;
    uint8  need_out;

    uint32 length_mode;
    uint32 length;
    uint32 key_index;
    uint32 tbl_id;
    uint8 key_data[MAX_ENTRY_BYTE];

}drv_cpu_acc_prepare_info_t;


typedef int32 (* DRV_CPU_ACC_PREPARE_FN_t)(uint8, drv_cpu_acc_in_t*, drv_cpu_acc_prepare_info_t*);


#define CPU_ACC_SMALL_TYPE_BY_KEY_LKUP     0
#define CPU_ACC_SMALL_TYPE_BY_KEY_ALLOC    1
#define CPU_ACC_SMALL_TYPE_BY_KEY_WRITE    2
#define CPU_ACC_SMALL_TYPE_BY_KEY_DEL      3
#define CPU_ACC_SMALL_TYPE_BY_IDX_WRITE    4
#define CPU_ACC_SMALL_TYPE_BY_IDX_DEL      5
#define CPU_ACC_SMALL_TYPE_BY_IDX_READ     6
#define CPU_ACC_SMALL_TYPE_NUM             7

#define CPU_ACC_BIG_TYPE_FIB_HOST1         0
#define CPU_ACC_BIG_TYPE_USER_ID           1
#define CPU_ACC_BIG_TYPE_XC_OAM            2
#define CPU_ACC_BIG_TYPE_FLOW_HASH         3


STATIC int32
_drv_goldengate_cpu_acc_prepare_fib1(uint8 small_type, drv_cpu_acc_in_t* in, drv_cpu_acc_prepare_info_t* p_out)
{
    uint32 data[MAX_ENTRY_BYTE / 4] = { 0 };

    uint32 length_mode = 0;
    uint32 length      = 0;
    uint32 c_length    = 0;
    uint32 tbl_id      = in->tbl_id;

    _drv_goldengate_get_length_by_tbl_id(tbl_id, &length_mode, &length,&c_length);
    if (small_type == CPU_ACC_SMALL_TYPE_BY_KEY_ALLOC ||
        small_type == CPU_ACC_SMALL_TYPE_BY_KEY_LKUP ||
        small_type == CPU_ACC_SMALL_TYPE_BY_KEY_WRITE ||
        small_type == CPU_ACC_SMALL_TYPE_BY_KEY_DEL)
    {
        drv_goldengate_model_hash_combined_key((uint8 *) data, (uint8 *) in->data, c_length, tbl_id);
        drv_goldengate_model_hash_mask_key((uint8 *) data, (uint8 *) data, HASH_MODULE_FIB_HOST1, tbl_id);
        _drv_goldengate_hash_lookup_convert_ds_endian(tbl_id,(uint8 *)data);
        SetFibHost1CpuLookupReq(V, cpuLookupReqValid_f, &p_out->req, 1);
        SetFibHost1CpuLookupReq(A, data_f, &p_out->req, data);
        SetFibHost1CpuLookupReq(V, lengthMode_f, &p_out->req, length_mode);

        p_out->req_type = DRV_CPU_LOOKUP_ACC_FIB_HOST1;
        p_out->by_key   = 1;
        sal_memcpy(&p_out->req1, in->data, length);
        sal_memcpy(p_out->key_data, data, length);

    }

    p_out->cam_index   = FIB_HOST1_CAM_NUM;
    p_out->cam_step    = DS_FIB_HOST1_HASH_CAM_BYTES / SINGLE_KEY_BYTE;
    p_out->cam_tbl     = DsFibHost1HashCam_t;
    p_out->length_mode = length_mode;
    p_out->length      = length;
    p_out->tbl_id = tbl_id;

    return DRV_E_NONE;
}

STATIC int32
_drv_goldengate_cpu_acc_prepare_userid(uint8 small_type, drv_cpu_acc_in_t* in, drv_cpu_acc_prepare_info_t* p_out)
{
    uint32 data[MAX_ENTRY_BYTE / 4] = { 0 };

    uint32 length_mode = 0;
    uint32 length      = 0;
    uint32 c_length    = 0;
    uint32 tbl_id      = in->tbl_id;

    _drv_goldengate_get_length_by_tbl_id(tbl_id, &length_mode, &length,&c_length);
    if (small_type == CPU_ACC_SMALL_TYPE_BY_KEY_ALLOC ||
        small_type == CPU_ACC_SMALL_TYPE_BY_KEY_LKUP ||
        small_type == CPU_ACC_SMALL_TYPE_BY_KEY_WRITE ||
        small_type == CPU_ACC_SMALL_TYPE_BY_KEY_DEL)
    {
        drv_goldengate_model_hash_combined_key((uint8 *) data, (uint8 *) in->data, c_length, tbl_id);
        drv_goldengate_model_hash_mask_key((uint8 *) data, (uint8 *) data, HASH_MODULE_USERID, tbl_id);
        SetUserIdCpuLookupReq(V, cpuLookupReqValid_f, &p_out->req, 1);
        SetUserIdCpuLookupReq(A, data_f, &p_out->req, data);
        SetUserIdCpuLookupReq(V, lengthMode_f, &p_out->req, length_mode);

        p_out->req_type = DRV_CPU_LOOKUP_ACC_USER_ID;
        p_out->by_key   = 1;
    }


    p_out->cam_index   = USER_ID_HASH_CAM_NUM;
    p_out->cam_step    = DS_USER_ID_HASH_CAM_BYTES / SINGLE_KEY_BYTE;
    p_out->cam_tbl     = DsUserIdHashCam_t;
    p_out->length_mode = length_mode;
    p_out->length      = length;
    return DRV_E_NONE;
}


STATIC int32
_drv_goldengate_cpu_acc_prepare_xcoam(uint8 small_type, drv_cpu_acc_in_t* in, drv_cpu_acc_prepare_info_t* p_out)
{
    uint32 data[MAX_ENTRY_BYTE / 4] = { 0 };

    uint32 length      = 0;
    uint32 length_mode = 0;
    uint32 c_length    = 0;
    uint32 tbl_id      = in->tbl_id;

    _drv_goldengate_get_length_by_tbl_id(tbl_id, &length_mode, &length,&c_length);

    if (small_type == CPU_ACC_SMALL_TYPE_BY_KEY_ALLOC ||
        small_type == CPU_ACC_SMALL_TYPE_BY_KEY_LKUP ||
        small_type == CPU_ACC_SMALL_TYPE_BY_KEY_WRITE ||
        small_type == CPU_ACC_SMALL_TYPE_BY_KEY_DEL)
    {
        drv_goldengate_model_hash_combined_key((uint8 *) data, (uint8 *) in->data, DRV_HASH_85BIT_KEY_LENGTH, tbl_id);
        drv_goldengate_model_hash_mask_key((uint8 *) data, (uint8 *) data, HASH_MODULE_EGRESS_XC, tbl_id);
        SetEgressXcOamCpuLookupReq(V, cpuLookupReqValid_f, &p_out->req, 1);
        SetEgressXcOamCpuLookupReq(A, data_f, &p_out->req, data);

        p_out->req_type = DRV_CPU_LOOKUP_ACC_XC_OAM;
        p_out->by_key   = 1;
    }


    p_out->cam_index   = XC_OAM_HASH_CAM_NUM;
    p_out->cam_step    = DS_EGRESS_XC_OAM_HASH_CAM_BYTES / SINGLE_KEY_BYTE;
    p_out->cam_tbl     = DsEgressXcOamHashCam_t;
    p_out->length_mode = length_mode;
    p_out->length      = length;
    return DRV_E_NONE;
}



STATIC int32
_drv_goldengate_cpu_acc_prepare_flow(uint8 small_type, drv_cpu_acc_in_t* in, drv_cpu_acc_prepare_info_t* p_out)
{
    uint32 data[MAX_ENTRY_BYTE / 4] = { 0 };

    uint32 length_mode = 0;
    uint32 length      = 0;
    uint32 c_length    = 0;
    uint32 tbl_id      = in->tbl_id;

    _drv_goldengate_get_length_by_tbl_id(tbl_id, &length_mode, &length,&c_length);
    if (small_type == CPU_ACC_SMALL_TYPE_BY_KEY_ALLOC ||
        small_type == CPU_ACC_SMALL_TYPE_BY_KEY_LKUP ||
        small_type == CPU_ACC_SMALL_TYPE_BY_KEY_WRITE ||
        small_type == CPU_ACC_SMALL_TYPE_BY_KEY_DEL)
    {
        drv_goldengate_model_hash_combined_key((uint8 *) data, (uint8 *) in->data, c_length, tbl_id);
        drv_goldengate_model_hash_mask_key((uint8 *) data, (uint8 *) data, HASH_MODULE_FLOW, tbl_id);
        SetFlowHashCpuLookupReq0(V, cpuLookupReqValid_f, &p_out->req, 1);
        SetFlowHashCpuLookupReq1(A, data_f, &p_out->req1, data);
        SetFlowHashCpuLookupReq1(V, lengthMode_f, &p_out->req1, length_mode);

        p_out->req_type = DRV_CPU_LOOKUP_ACC_FLOW_HASH;
        p_out->by_key   = 1;
    }

    p_out->cam_index   = FLOW_HASH_CAM_NUM;
    p_out->cam_step    = DS_FLOW_HASH_CAM_BYTES / SINGLE_KEY_BYTE;
    p_out->cam_tbl     = DsFlowHashCam_t;
    p_out->length_mode = length_mode;
    p_out->length      = length;
    return DRV_E_NONE;
}



int32
_drv_goldengate_cpu_acc_host1_save_key_to_cam(uint8 chip_id, void* hw_key, uint32 key_length, uint32 key_index, uint32 cam_index, uint8 is_add)
{
    sal_memcpy(&(g_gg_drv_acc[chip_id].host1_cam[cam_index]), hw_key, sizeof(DsFibHost1HashCam_m));
    if (is_add)
    {
        SET_BIT(g_gg_drv_acc[chip_id].host1_cam_bitmap, key_index);
        if (DOUBLE_KEY_BYTE == key_length)
        {
            SET_BIT(g_gg_drv_acc[chip_id].host1_cam_bitmap, key_index + 1);
        }
    }
    else
    {
        CLEAR_BIT(g_gg_drv_acc[chip_id].host1_cam_bitmap, key_index);
        if (DOUBLE_KEY_BYTE == key_length)
        {
            CLEAR_BIT(g_gg_drv_acc[chip_id].host1_cam_bitmap, key_index + 1);
        }
    }

    return DRV_E_NONE;
}

int32
_drv_goldengate_cpu_acc_host1_compare_cam_key(uint8 chip_id, uint32* key_index, drv_cpu_acc_prepare_info_t* info, uint8* hit, uint8* free)
{
    uint32 i = 0;
    uint8 is_bit_set = 0;
    uint8 is_free = 0;
    uint32 index = 0;
    uint8 is_double = 0;
    uint8 step = 0;
    uint8 step_length = 0;
    uint8* hw_key = NULL;

    is_double = (DOUBLE_KEY_BYTE == info->length)? 1 : 0;
    step = is_double? 2 : 1;
    step_length = info->length / step;
    for (i = 0; i < info->cam_index; i += step)
    {
        is_bit_set = 0;
        if (is_double)
        {
            if ((IS_BIT_SET(g_gg_drv_acc[chip_id].host1_cam_bitmap, i))
                && (IS_BIT_SET(g_gg_drv_acc[chip_id].host1_cam_bitmap, i + 1)))
            {
                is_bit_set = 1;
            }
        }
        else
        {
            if (IS_BIT_SET(g_gg_drv_acc[chip_id].host1_cam_bitmap, i))
            {
                is_bit_set = 1;
            }
        }

        if (is_bit_set)
        {
            hw_key = ((uint8*)&(g_gg_drv_acc[chip_id].host1_cam[i / (info->cam_step)])) + ((i % (info->cam_step))*step_length);
            SetDsFibHost1Ipv4McastHashKey(V, dsAdIndex_f, hw_key, 0); /*dsAdIndex is at the same positon in the host1 key*/
            SetDsFibHost1Ipv4McastHashKey(V, dsAdIndex_f, info->req1, 0); /*dsAdIndex is at the same positon in the host1 key*/
            if (!sal_memcmp(hw_key, info->req1, info->length))
            {
                *hit = 1;
                *key_index = i;
                return DRV_E_NONE;
            }
        }
        else if(0 == is_free)
        {
            if (is_double)
            {
                if (!(IS_BIT_SET(g_gg_drv_acc[chip_id].host1_cam_bitmap, i))
                    && !(IS_BIT_SET(g_gg_drv_acc[chip_id].host1_cam_bitmap, i + 1)))
                {
                    index = i;
                    is_free = 1;
                }
            }
            else
            {
                index = i;
                is_free = 1;
            }
        }
    }
    *free = is_free;
    *key_index = index;
    *hit = 0;
    return DRV_E_NONE;
}

int32
_drv_goldengate_cpu_acc_host1_cmp_hash_hw_table(void* hw_key, void* key_para, uint32 entry_size, uint8* hit, uint8* free)
{
    uint32 valid = 0;
    uint32 valid0 = 0;
    uint32 valid1 = 0;

    if (SINGLE_KEY_BYTE == entry_size)
    {
        valid   = GetDsFibHost1Ipv4McastHashKey(V, valid_f, hw_key);
    }
    else if (DOUBLE_KEY_BYTE == entry_size)
    {
        valid0   = GetDsFibHost1Ipv6McastHashKey(V, valid0_f, hw_key);
        valid1   = GetDsFibHost1Ipv6McastHashKey(V, valid1_f, hw_key);
        valid = (valid0 && valid1)? 1:0;
    }

    if (valid)
    {
        SetDsFibHost1Ipv4McastHashKey(V, dsAdIndex_f, hw_key, 0); /*dsAdIndex is at the same positon in the host1 key*/
        SetDsFibHost1Ipv4McastHashKey(V, dsAdIndex_f, key_para, 0); /*dsAdIndex is at the same positon in the host1 key*/
        if (!sal_memcmp(hw_key, key_para, entry_size))
        {
            *hit = 1;
        }
    }
    else if ((0 == valid0) && (0 == valid1))
    {
        *free = 1;
    }

    return DRV_E_NONE;
}

int32
_drv_goldengate_cpu_acc_host1_compare_hash_key(uint8 chip_id, uint32* bucket_index, drv_cpu_acc_prepare_info_t* info, uint8* hit, uint8* free)
{
    DsFibHost1MacIpv6McastHashKey_m ds_6w;
    DsFibHost1Ipv4McastHashKey_m ds_3w;
    uint32 cmd = 0;
    uint32 entry_size = 0;
    uint8 hit0 = 0, free0 = 0;
    uint8 hit1 = 0, free1 = 0;
    uint8 hit2 = 0, free2 = 0;
    uint8 hit3 = 0, free3 = 0;

    *hit = 0;
    entry_size = info->length;
    sal_memset(&ds_6w, 0, sizeof(DsFibHost1MacIpv6McastHashKey_m));
    sal_memset(&ds_3w, 0, sizeof(DsFibHost1Ipv4McastHashKey_m));

    cmd = DRV_IOR(DsFibHost1MacIpv6McastHashKey_t, DRV_ENTRY_FLAG); /*read 6w*/
    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, *bucket_index, cmd, &ds_6w));
    if (SINGLE_KEY_BYTE == entry_size)/*3w*/
    {
        sal_memcpy(&ds_3w, &ds_6w, entry_size);
        _drv_goldengate_cpu_acc_host1_cmp_hash_hw_table(&ds_3w, &(info->req1), entry_size, &hit0, &free0);
        if (hit0)
        {
            *bucket_index +=0;
            *hit = 1;
            return DRV_E_NONE;
        }

        sal_memcpy(&ds_3w, ((uint8*)&ds_6w) + entry_size, entry_size);
        _drv_goldengate_cpu_acc_host1_cmp_hash_hw_table(&ds_3w, &(info->req1), entry_size, &hit1, &free1);
        if (hit1)
        {
            *bucket_index +=1;
            *hit = 1;
            return DRV_E_NONE;
        }

        cmd = DRV_IOR(DsFibHost1MacIpv6McastHashKey_t, DRV_ENTRY_FLAG); /*read 6w*/
        DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, *bucket_index + 2, cmd, &ds_6w));
        sal_memcpy(&ds_3w, &ds_6w, entry_size);
        _drv_goldengate_cpu_acc_host1_cmp_hash_hw_table(&ds_3w, &(info->req1), entry_size, &hit2, &free2);
        if (hit2)
        {
            *bucket_index +=2;
            *hit = 1;
            return DRV_E_NONE;
        }

        sal_memcpy(&ds_3w, ((uint8*)&ds_6w) + entry_size, entry_size);
        _drv_goldengate_cpu_acc_host1_cmp_hash_hw_table(&ds_3w, &(info->req1), entry_size, &hit3, &free3);
        if (hit3)
        {
            *bucket_index +=3;
            *hit = 1;
            return DRV_E_NONE;
        }
    }
    else if (DOUBLE_KEY_BYTE == entry_size)/*6w*/
    {
        _drv_goldengate_cpu_acc_host1_cmp_hash_hw_table(&ds_6w, &(info->req1), entry_size, &hit0, &free0);
        if (hit0)
        {
            *bucket_index += 0;
            *hit = 1;
            return DRV_E_NONE;
        }

        cmd = DRV_IOR(DsFibHost1MacIpv6McastHashKey_t, DRV_ENTRY_FLAG); /*read 6w*/
        DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, *bucket_index + 2, cmd, &ds_6w));
        _drv_goldengate_cpu_acc_host1_cmp_hash_hw_table(&ds_6w, &(info->req1), entry_size, &hit2, &free2);
        if (hit2)
        {
            *bucket_index += 2;
            *hit = 1;
            return DRV_E_NONE;
        }
    }

    *free = 1;
    if (free0)
    {
        *bucket_index += 0;
    }
    else if (free1)
    {
        *bucket_index += 1;
    }
    else if (free2)
    {
        *bucket_index += 2;
    }
    else if (free3)
    {
        *bucket_index += 3;
    }
    else
    {
        *free = 0;
    }

    return DRV_E_NONE;
}

STATIC int32
_drv_goldengate_cpu_acc_host1_compare_key(uint8 chip_id,uint32 hash_index, drv_cpu_acc_prepare_info_t* info)
{
    uint32 key_index = 0;
    uint32 cam_index = 0;
    uint8 hit_ram = 0;
    uint8 free_ram = 0;
    uint8 hit_cam = 0;
    uint8 free_cam = 0;
    uint8 conflict = 0;
    uint8 hit = 0;

    /*1. compare hw table for ram*/
    _drv_goldengate_cpu_acc_host1_compare_hash_key(chip_id, &hash_index, info, &hit_ram, &free_ram);

    /*2. compare sw table for cam*/
    if (!hit_ram)
    {
        _drv_goldengate_cpu_acc_host1_compare_cam_key(chip_id, &cam_index, info, &hit_cam, &free_cam);
    }

    /*3. get result*/
    hit = hit_ram || hit_cam;
    if (hit)
    {
        key_index = hit_ram? (hash_index + info->cam_index) : cam_index;
    }
    else if (free_ram || free_cam)
    {
        key_index = free_ram? (hash_index + info->cam_index) : cam_index;
    }
    else
    {
        conflict = 1;
    }
    SetFibHost1CpuLookupResult(V, lookupResultValid_f, &info->rlt, hit);
    SetFibHost1CpuLookupResult(V, resultIndex_f, &info->rlt, key_index);
    SetFibHost1CpuLookupResult(V, conflict_f, &info->rlt, conflict);

    return DRV_E_NONE;
}

int32
_drv_goldengate_cpu_acc_host1_calculate_index(key_lookup_info_t* key_info)
{
    key_lookup_result_t  key_result;
    uint32 bucket_num = 4;/* bucket depth, refer to fib_host1_level*/
    uint32 key_index_base = 0;/*only one level*/
    sal_memset(&key_result, 0, sizeof(key_lookup_result_t));
    _drv_goldengate_hash_calculate_index(key_info, &key_result);
    return key_index_base + key_result.bucket_index*bucket_num;
}

STATIC int32
_drv_goldengate_cpu_acc_host1_prepare_lkup_info(key_lookup_info_t* key_info, drv_cpu_acc_prepare_info_t* info)
{
    drv_goldengate_ftm_get_host1_poly_type(&(key_info->polynomial), &(key_info->poly_len), &(key_info->type));
    key_info->key_data = info->key_data;
    key_info->crc_bits = 170;/*always 170 for host1, refer to gg_hash_key_length*/
    return DRV_E_NONE;
}

STATIC int32
_drv_goldengate_cpu_acc_host1_lkup(uint8 chip_id, drv_cpu_acc_prepare_info_t* info)
{
    key_lookup_info_t key_info;
    uint32 hash_index = 0;
    sal_memset(&key_info, 0, sizeof(key_lookup_info_t));
    /*1. prepare hash para*/
    _drv_goldengate_cpu_acc_host1_prepare_lkup_info(&key_info, info);

    /*2. calculate hash index*/
    hash_index = _drv_goldengate_cpu_acc_host1_calculate_index(&key_info);

    /*3. read table and compare result*/
    _drv_goldengate_cpu_acc_host1_compare_key(chip_id, hash_index, info);

    return DRV_E_NONE;
}

STATIC int32
drv_goldengate_host0_fdb_calculate_index(uint8 chip_id, drv_cpu_acc_in_t* in, drv_cpu_acc_out_t* out)
{
#define HASH_LEVEL_NUM 5

    drv_fib_acc_in_t  host0_in;
    drv_fib_acc_out_t host0_out;
    key_lookup_info_t key_info;
    uint8 level = 0;
    uint8 index = 0;
    uint32 cmd = 0;
    uint32 bucket_num = 4;/*only for fib_host0*/
    key_lookup_result_t  key_result;
    uint32 rslt_index = 0;
    FibHost0HashLookupCtl_m hash_lookup_ctl;
    uint8 key[MAX_ENTRY_BYTE] = {0};
    uint8 poly_type[HASH_LEVEL_NUM] = {0};
    uint8 level_en[HASH_LEVEL_NUM] = {0};
    uint32 index_base[HASH_LEVEL_NUM] = {0};
    uint32 key_index = 0;
    uint32 hw_hashtype = 0;
    uint32 crc[][3]= { /*refer to fib_host0_crc*/
         {HASH_CRC, 0x00000005, 11},
         {HASH_CRC, 0x00000003, 12},
         {HASH_CRC, 0x0000001b, 13},
         {HASH_CRC, 0x00000027, 13},
         {HASH_CRC, 0x000000c3, 13},
         {HASH_CRC, 0x00000143, 13},
         {HASH_CRC, 0x0000002b, 14},
         {HASH_CRC, 0x00000039, 14},
         {HASH_CRC, 0x000000a9, 14},
         {HASH_CRC, 0x00000113, 14},
         {HASH_XOR, 16        , 0},
      };

    if (in->tbl_id != DsFibHost0MacHashKey_t)
    {
        return DRV_E_INVALID_TBL;
    }

    sal_memset(&hash_lookup_ctl, 0, sizeof(FibHost0HashLookupCtl_m));
    cmd = DRV_IOR(FibHost0HashLookupCtl_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, &hash_lookup_ctl));
    level_en[0] = GetFibHost0HashLookupCtl(V, fibHost0Level0HashEn_f, &hash_lookup_ctl);
    level_en[1] = GetFibHost0HashLookupCtl(V, fibHost0Level1HashEn_f, &hash_lookup_ctl);
    level_en[2] = GetFibHost0HashLookupCtl(V, fibHost0Level2HashEn_f, &hash_lookup_ctl);
    level_en[3] = GetFibHost0HashLookupCtl(V, fibHost0Level3HashEn_f, &hash_lookup_ctl);
    level_en[4] = GetFibHost0HashLookupCtl(V, fibHost0Level4HashEn_f, &hash_lookup_ctl);
    index_base[1] = GetFibHost0HashLookupCtl(V, fibHost0Level1IndexBase_f, &hash_lookup_ctl);
    index_base[2] = GetFibHost0HashLookupCtl(V, fibHost0Level2IndexBase_f, &hash_lookup_ctl);
    index_base[3] = GetFibHost0HashLookupCtl(V, fibHost0Level3IndexBase_f, &hash_lookup_ctl);
    index_base[4] = GetFibHost0HashLookupCtl(V, fibHost0Level4IndexBase_f, &hash_lookup_ctl);
    poly_type[0] = GetFibHost0HashLookupCtl(V, fibHost0Level0HashType_f, &hash_lookup_ctl);
    poly_type[1] = GetFibHost0HashLookupCtl(V, fibHost0Level1HashType_f, &hash_lookup_ctl);
    poly_type[2] = GetFibHost0HashLookupCtl(V, fibHost0Level2HashType_f, &hash_lookup_ctl);
    poly_type[3] = GetFibHost0HashLookupCtl(V, fibHost0Level3HashType_f, &hash_lookup_ctl);
    poly_type[4] = GetFibHost0HashLookupCtl(V, fibHost0Level4HashType_f, &hash_lookup_ctl);


    sal_memset(&key_info, 0, sizeof(key_lookup_info_t));
    key_info.key_data = key;
    sal_memcpy(key_info.key_data, (uint8*)in->data, sizeof(DsFibHost0MacHashKey_m));
    key_info.crc_bits = 170;/*always 170 for host0, refer to gg_hash_key_length*/
    key_info.key_bits = 85;

    host0_in.rw.tbl_id    = DsFibHost0MacHashKey_t;
    for (level = 0; level < HASH_LEVEL_NUM; level++)
    {
        if (level_en[level] == 0)
        {
            continue;
        }
        sal_memset(&key_result, 0, sizeof(key_lookup_result_t));
        key_info.type       = crc[poly_type[level]][0];
        key_info.polynomial = crc[poly_type[level]][1];
        key_info.poly_len   = crc[poly_type[level]][2];

        _drv_goldengate_hash_calculate_index(&key_info, &key_result);
        rslt_index = index_base[level] + key_result.bucket_index * bucket_num + 32;
        for (index = 0; index < bucket_num; index++)
        {
            key_index = (rslt_index & 0xFFFFFFFC) | index;

             /*check if hashtype*/
            host0_in.rw.key_index = key_index ;
            DRV_IF_ERROR_RETURN(drv_goldengate_fib_acc(chip_id, DRV_FIB_ACC_READ_FIB0_BY_IDX, &host0_in, &host0_out));
            hw_hashtype = GetDsFibHost0MacHashKey(V, hashType_f, &host0_out.read.data);
            if (hw_hashtype != 4)
            {
                continue;
            }

            out->data[out->hit++] = key_index;
        }
    }

    return DRV_E_NONE;
}



STATIC int32
_drv_goldengate_cpu_acc_rw_by_index(uint8 chip_id, uint32 tbl_id,
                         drv_cpu_acc_prepare_info_t* info,
                         void* data,
                         uint32* write_data)
{
    uint32 cmdr                        = 0;
    uint32 cmdw                        = 0;
    uint32 index                       = 0;
    uint32 step                        = 0;
    uint32 cam_key[MAX_ENTRY_BYTE / 4] = { 0 };
    uint8 valid = 0;

    if (info->key_index < info->cam_index)
    {
        index = info->key_index / info->cam_step;
        step  = info->key_index % info->cam_step;

        cmdr = DRV_IOR(info->cam_tbl, DRV_ENTRY_FLAG);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, index, cmdr, cam_key));
        if (write_data)
        {
            sal_memcpy(&(cam_key[(SINGLE_KEY_BYTE / 4) * step]), write_data, info->length);
            cmdw = DRV_IOW(info->cam_tbl, DRV_ENTRY_FLAG);
            DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, index, cmdw, cam_key));
            if (CPU_ACC_BIG_TYPE_FIB_HOST1 == info->big_type)/*save cam info to sw for host1*/
            {
                if (SINGLE_KEY_BYTE == info->length)
                {
                    valid = GetDsFibHost1Ipv4McastHashKey(V, valid_f, write_data);
                }
                else
                {
                    valid = GetDsFibHost1Ipv6McastHashKey(V, valid0_f, write_data);
                }
                _drv_goldengate_cpu_acc_host1_save_key_to_cam(chip_id, &cam_key, info->length, info->key_index, index, valid);
            }
        }
        else
        {
            sal_memcpy(data, &(cam_key[(SINGLE_KEY_BYTE / 4) * step]), info->length);
        }
    }
    else
    {
        if (write_data)
        {
            cmdw = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
            DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, (info->key_index - info->cam_index), cmdw, write_data));
        }
        else
        {
            cmdr = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
            DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, (info->key_index - info->cam_index), cmdr, data));
        }
    }

    return DRV_E_NONE;
}


int32
drv_goldengate_cpu_acc_prepare(uint8 acc_type, drv_cpu_acc_in_t* in, drv_cpu_acc_prepare_info_t* info)
{
    uint8 big_type   = acc_type / CPU_ACC_SMALL_TYPE_NUM;
    uint8 small_type = acc_type % CPU_ACC_SMALL_TYPE_NUM;
    uint8 req_valid = 0;

    DRV_CPU_ACC_PREPARE_FN_t prepare[] =
    {
        _drv_goldengate_cpu_acc_prepare_fib1,
        _drv_goldengate_cpu_acc_prepare_userid,
        _drv_goldengate_cpu_acc_prepare_xcoam,
        _drv_goldengate_cpu_acc_prepare_flow
    };

    info->big_type   = big_type;
    info->small_type = small_type;
    info->need_out = (small_type != CPU_ACC_SMALL_TYPE_BY_IDX_WRITE) &&
                     (small_type != CPU_ACC_SMALL_TYPE_BY_IDX_DEL);

    DRV_IF_ERROR_RETURN(drv_goldengate_chip_cpu_acc_get_valid(acc_type, &req_valid));
    if (req_valid)
    {
        return DRV_E_OCCPANCY;
    }

    return prepare[big_type](small_type, in, info);
}


int32
drv_goldengate_cpu_acc_asic_lkup(uint8 chip_id, drv_cpu_acc_prepare_info_t* info)
{

    DRV_PTR_VALID_CHECK(info);

    DRV_PTR_VALID_CHECK(drv_gg_io_api.drv_cpu_acc_asic_lkup);

    if (CPU_ACC_BIG_TYPE_FIB_HOST1 == info->big_type)/*host1 lookup by sw*/
    {
        DRV_IF_ERROR_RETURN(_drv_goldengate_cpu_acc_host1_lkup(chip_id, info));
    }
    else
    {
        DRV_IF_ERROR_RETURN(drv_gg_io_api.drv_cpu_acc_asic_lkup(chip_id, info->req_type, &(info->req), &(info->req1), &(info->rlt)));
    }

    return DRV_E_NONE;
}

int32
drv_goldengate_cpu_acc_get_result(uint8 chip_id, drv_cpu_acc_prepare_info_t* info, drv_cpu_acc_out_t* out)
{

    if (info->big_type == CPU_ACC_BIG_TYPE_FIB_HOST1)
    {
        out->hit       = GetFibHost1CpuLookupResult(V, lookupResultValid_f, &info->rlt);
        out->key_index = GetFibHost1CpuLookupResult(V, resultIndex_f, &info->rlt);
        out->conflict  = GetFibHost1CpuLookupResult(V, conflict_f, &info->rlt);
    }
    else if (info->big_type == CPU_ACC_BIG_TYPE_USER_ID)
    {
        out->hit       = GetUserIdCpuLookupResult(V, lookupResultValid_f, &info->rlt);
        out->key_index = GetUserIdCpuLookupResult(V, resultIndex_f, &info->rlt);
        out->conflict  = GetUserIdCpuLookupResult(V, conflict_f, &info->rlt);
    }
    else if (info->big_type == CPU_ACC_BIG_TYPE_XC_OAM)
    {
        out->hit       = GetEgressXcOamCpuLookupResult(V, lookupResultValid_f, &info->rlt);
        out->key_index = GetEgressXcOamCpuLookupResult(V, resultIndex_f, &info->rlt);
        out->conflict  = GetEgressXcOamCpuLookupResult(V, conflict_f, &info->rlt);
    }
    else if (info->big_type == CPU_ACC_BIG_TYPE_FLOW_HASH)
    {
        out->hit       = GetFlowHashCpuLookupResult(V, lookupResultValid_f, &info->rlt);
        out->key_index = GetFlowHashCpuLookupResult(V, resultIndex_f, &info->rlt);
        out->conflict  = GetFlowHashCpuLookupResult(V, conflict_f, &info->rlt);
    }
    out->free = !out->hit   && !out->conflict;
    return DRV_E_NONE;
}


int32
drv_goldengate_cpu_acc_read_write(uint8 chip_id, drv_cpu_acc_in_t* in, drv_cpu_acc_prepare_info_t* info, drv_cpu_acc_out_t* out)
{
    uint32 write_data[MAX_ENTRY_BYTE / 4] = { 0 };
    uint32 * p_data                       = NULL;
    uint32 offset                         = 0;
    int32 ret                             = DRV_E_NONE;

    uint8  write_by_key = ((CPU_ACC_SMALL_TYPE_BY_KEY_WRITE == info->small_type) && (out->hit || out->free));
    uint8  write_by_idx = (CPU_ACC_SMALL_TYPE_BY_IDX_WRITE == info->small_type);

    uint8  del_by_key = ((CPU_ACC_SMALL_TYPE_BY_KEY_DEL == info->small_type) && (out->hit));
    uint8  del_by_idx = (CPU_ACC_SMALL_TYPE_BY_IDX_DEL == info->small_type);

    uint8  alloc_by_key = (CPU_ACC_SMALL_TYPE_BY_KEY_ALLOC == info->small_type);
    uint8  read_by_idx  = (CPU_ACC_SMALL_TYPE_BY_IDX_READ == info->small_type);
    uint8  do_rw = 0;

    if (write_by_key || write_by_idx)
    {
        do_rw =1;
        p_data = in->data;
    }
    else if (del_by_key || del_by_idx)
    {
        do_rw =1;
        sal_memset(&write_data, 0, MAX_ENTRY_BYTE);
        p_data = write_data;
    }
    else if (alloc_by_key)
    {
        do_rw =1;
        sal_memset(&write_data, 0xFF, MAX_ENTRY_BYTE);
        p_data = write_data;
    }
    else if (read_by_idx)
    {
        do_rw =1;
        p_data = NULL;
    }

    /*cam process*/
    if((info->key_index < info->cam_index))
    {
        if((CPU_ACC_BIG_TYPE_FIB_HOST1 == info->big_type)
            || (CPU_ACC_BIG_TYPE_USER_ID == info->big_type)
            || (CPU_ACC_BIG_TYPE_XC_OAM == info->big_type))
        {

            if((CPU_ACC_SMALL_TYPE_BY_KEY_WRITE == info->small_type)
                || (CPU_ACC_SMALL_TYPE_BY_KEY_ALLOC == info->small_type))
            {
                /*if is resev for oam, return conflict*/
                if((CPU_ACC_BIG_TYPE_XC_OAM == info->big_type)
                    &&(CPU_ACC_SMALL_TYPE_BY_KEY_ALLOC == info->small_type))
                {
                    if((XC_OAM_HASH_CAM_NUM - 1) == info->key_index)
                    {
                        out->conflict = 1;
                        return DRV_E_NONE;
                    }
                }

                ret = drv_goldengate_ftm_alloc_cam_offset(in->tbl_id, &offset);
                if (DRV_E_NONE == ret)
                {
                    out->key_index  = offset;
                    info->key_index = offset;
                }
                else if (DRV_E_HASH_CONFLICT == ret)
                {
                    out->conflict = 1;
                    return DRV_E_NONE;
                }
            }

            if(CPU_ACC_SMALL_TYPE_BY_KEY_LKUP == info->small_type)
            {
                /*if is resev for oam, return conflict*/
                if((CPU_ACC_BIG_TYPE_XC_OAM == info->big_type)
                    &&(CPU_ACC_SMALL_TYPE_BY_KEY_LKUP == info->small_type))
                {
                    if((XC_OAM_HASH_CAM_NUM - 1) == info->key_index)
                    {
                        out->conflict = 1;
                        return DRV_E_NONE;
                    }
                }
            }
            else if(((CPU_ACC_SMALL_TYPE_BY_KEY_DEL == info->small_type)&& out->hit)
                    || (CPU_ACC_SMALL_TYPE_BY_IDX_DEL == info->small_type))
            {
                offset = info->key_index;
                ret = drv_goldengate_ftm_free_cam_offset(in->tbl_id, offset);
            }
        }
    }

    return do_rw ? _drv_goldengate_cpu_acc_rw_by_index(chip_id, in->tbl_id, info, &out->data, p_data) : DRV_E_NONE;
}

int32
drv_goldengate_cpu_acc(uint8 chip_id, uint8 acc_type, drv_cpu_acc_in_t* in, drv_cpu_acc_out_t* out)
{
    uint8  ecc_en = 0;
    uint32 data[32] = {0};
    tbl_entry_t tbl_entry;
    drv_cpu_acc_prepare_info_t info;

    DRV_PTR_VALID_CHECK(in);

    sal_memset(&info, 0, sizeof(info));

    if (g_drv_gg_io_master.chip_agent_cb&&(TRUE == gg_client_init_done))
    {
        chip_io_para_t para;
        para.op = CHIP_IO_OP_CPU_ACC;
        para.u.cpu_acc.chip_id = chip_id;
        para.u.cpu_acc.acc_type = acc_type;
        para.u.cpu_acc.in = in;
        para.u.cpu_acc.out = out;
        return g_drv_gg_io_master.chip_agent_cb(0, &para);
    }
    else if(DRV_CHIP_AGT_MODE_CLIENT == drv_goldengate_chip_agent_mode())
    {
        return 0;
    }

    CPU_ACC_LOCK;

    if (acc_type == DRV_CPU_CALC_ACC_FDB_INDEX)
    {
        DRV_IF_ERROR_RETURN_WITH_UNLOCK(drv_goldengate_host0_fdb_calculate_index(chip_id, in, out), gg_cpu_acc_mutex);
        CPU_ACC_UNLOCK;
        return DRV_E_NONE;
    }
    DRV_IF_ERROR_RETURN_WITH_UNLOCK(drv_goldengate_cpu_acc_prepare(acc_type, in, &info), gg_cpu_acc_mutex);
    if (info.need_out)
    {
        if (NULL == out)
        {
            CPU_ACC_UNLOCK;
            return DRV_E_INVALID_PTR;
        }
    }

    if (info.by_key)
    {
        DRV_IF_ERROR_RETURN_WITH_UNLOCK(drv_goldengate_cpu_acc_asic_lkup(chip_id, &info), gg_cpu_acc_mutex);
        DRV_IF_ERROR_RETURN_WITH_UNLOCK(drv_goldengate_cpu_acc_get_result(chip_id,  &info, out), gg_cpu_acc_mutex);
        info.key_index = out->key_index;
    }
    else
    {
        info.key_index = in->key_index;
    }

    DRV_IF_ERROR_RETURN_WITH_UNLOCK(drv_goldengate_cpu_acc_read_write(chip_id, in, &info, out), gg_cpu_acc_mutex);

    ecc_en = drv_goldengate_ecc_recover_get_enable();

    if (ecc_en && (in->key_index > info.cam_index))
    {
        tbl_entry.data_entry = NULL;
        tbl_entry.mask_entry = NULL;

        if (((DRV_CPU_ADD_ACC_FIB_HOST1 == acc_type) && (0 == out->conflict))
            || (DRV_CPU_ADD_ACC_FIB_HOST1_BY_IDX == acc_type)
            || (DRV_CPU_ADD_ACC_USER_ID_BY_IDX == acc_type)
            || (DRV_CPU_ADD_ACC_XC_OAM_BY_IDX == acc_type)
            || (DRV_CPU_ADD_ACC_FLOW_HASH_BY_IDX == acc_type))
        {
            tbl_entry.data_entry = in->data;
        }
        else if (((DRV_CPU_DEL_ACC_FIB_HOST1 == acc_type) && (0 == out->conflict))
                || (DRV_CPU_DEL_ACC_FIB_HOST1_BY_IDX == acc_type)
                || (DRV_CPU_DEL_ACC_USER_ID_BY_IDX == acc_type)
                || (DRV_CPU_DEL_ACC_XC_OAM_BY_IDX == acc_type)
                || (DRV_CPU_DEL_ACC_FLOW_HASH_BY_IDX == acc_type))
        {
            tbl_entry.data_entry = data;
        }

        if (NULL != tbl_entry.data_entry)
        {
            drv_goldengate_ecc_recover_store(chip_id, in->tbl_id, info.key_index - info.cam_index, &tbl_entry);
        }
    }

    CPU_ACC_UNLOCK;
    return DRV_E_NONE;
}


#define  _DRV_IPFIX_ACC_M_
int32
drv_goldengate_ipfix_acc_prepare(uint8 acc_type, drv_ipfix_acc_in_t* in, void* cpu_req)
{
    uint32 key_length = (in->type) ? DRV_HASH_340BIT_KEY_LENGTH : DRV_HASH_170BIT_KEY_LENGTH;
    uint32 data[MAX_ENTRY_BYTE / 4] = { 0 };
    uint32 cmd = 0;

    /* check valid bit clear */
    cmd = DRV_IOR(IpfixHashAdCpuReq_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(0, 0, cmd, data));
    if (GetIpfixHashAdCpuReq(V, cpuReqValid_f, data))
    {
        return DRV_E_OCCPANCY;
    }

    switch (acc_type)
    {
    case DRV_IPFIX_ACC_WRITE_KEY_BY_IDX:
        drv_goldengate_model_hash_combined_key((uint8 *) data, (uint8 *) in->data, key_length, in->tbl_id);

        SetIpfixHashAdCpuReq(V, cpuReqType_f, cpu_req, DRV_IPFIX_REQ_WRITE_BY_IDX);
        SetIpfixHashAdCpuReq(V, cpuReqValid_f, cpu_req, 1);
        SetIpfixHashAdCpuReq(V, isAdMemOperation_f, cpu_req, 0);
        SetIpfixHashAdCpuReq(V, u1_gWriteByIndex_cpuIndex_f, cpu_req, in->index);
        SetIpfixHashAdCpuReq(V, u1_gWriteByIndex_dataType_f, cpu_req, in->type);
        SetIpfixHashAdCpuReq(A, u1_gWriteByIndex_data_f, cpu_req, (uint32 *) data);
        break;

    case DRV_IPFIX_ACC_WRITE_AD_BY_IDX:
        drv_goldengate_model_hash_combined_key((uint8 *) data, (uint8 *) in->data, key_length, in->tbl_id);

        SetIpfixHashAdCpuReq(V, cpuReqType_f, cpu_req, DRV_IPFIX_REQ_WRITE_BY_IDX);
        SetIpfixHashAdCpuReq(V, cpuReqValid_f, cpu_req, 1);
        SetIpfixHashAdCpuReq(V, isAdMemOperation_f, cpu_req, 1);
        SetIpfixHashAdCpuReq(V, u1_gWriteByIndex_cpuIndex_f, cpu_req, in->index);
        SetIpfixHashAdCpuReq(V, u1_gWriteByIndex_dataType_f, cpu_req, in->type);
        SetIpfixHashAdCpuReq(A, u1_gWriteByIndex_data_f, cpu_req, (uint32 *) data);
        break;

    case DRV_IPFIX_ACC_WRITE_BY_KEY:
        drv_goldengate_model_hash_combined_key((uint8 *) data, (uint8 *) in->data, key_length, in->tbl_id);

        SetIpfixHashAdCpuReq(V, cpuReqType_f, cpu_req, DRV_IPFIX_REQ_WRITE_BY_KEY);
        SetIpfixHashAdCpuReq(V, cpuReqValid_f, cpu_req, 1);
        SetIpfixHashAdCpuReq(V, isAdMemOperation_f, cpu_req, 0);
        SetIpfixHashAdCpuReq(V, u1_gWriteByKey_dataType_f, cpu_req,in->type);
        SetIpfixHashAdCpuReq(A, u1_gWriteByKey_data_f, cpu_req, (uint32 *) data);
        break;

    case DRV_IPFIX_ACC_LKP_BY_KEY:
        drv_goldengate_model_hash_combined_key((uint8 *) data, (uint8 *) in->data, key_length, in->tbl_id);

        SetIpfixHashAdCpuReq(V, cpuReqType_f, cpu_req, DRV_IPFIX_REQ_LKP_BY_KEY);
        SetIpfixHashAdCpuReq(V, cpuReqValid_f, cpu_req, 1);
        SetIpfixHashAdCpuReq(V, isAdMemOperation_f, cpu_req, 0);
        SetIpfixHashAdCpuReq(V, u1_gLookupByKey_dataType_f, cpu_req, in->type);
        SetIpfixHashAdCpuReq(A, u1_gLookupByKey_data_f, cpu_req, (uint32 *) data);
        break;

    case DRV_IPFIX_ACC_LKP_KEY_BY_IDX:
        SetIpfixHashAdCpuReq(V, cpuReqType_f, cpu_req, DRV_IPFIX_REQ_LKP_BY_IDX);
        SetIpfixHashAdCpuReq(V, cpuReqValid_f, cpu_req, 1);
        SetIpfixHashAdCpuReq(V, isAdMemOperation_f, cpu_req, 0);
        SetIpfixHashAdCpuReq(V, u1_gReadByIndex_cpuIndex_f, cpu_req, in->index);
        SetIpfixHashAdCpuReq(V, u1_gReadByIndex_dataType_f, cpu_req, in->type);
        break;

    case DRV_IPFIX_ACC_LKP_AD_BY_IDX:
        SetIpfixHashAdCpuReq(V, cpuReqType_f, cpu_req, DRV_IPFIX_REQ_LKP_BY_IDX);
        SetIpfixHashAdCpuReq(V, cpuReqValid_f, cpu_req, 1);
        SetIpfixHashAdCpuReq(V, isAdMemOperation_f, cpu_req, 1);
        SetIpfixHashAdCpuReq(V, u1_gReadByIndex_cpuIndex_f, cpu_req, in->index);
        SetIpfixHashAdCpuReq(V, u1_gReadByIndex_dataType_f, cpu_req, in->type);
        break;

    case DRV_IPFIX_ACC_DEL_KEY_BY_IDX:
        sal_memset((uint8 *) data, 0, MAX_ENTRY_BYTE);
        SetIpfixHashAdCpuReq(V, cpuReqType_f, cpu_req, DRV_IPFIX_REQ_WRITE_BY_IDX);
        SetIpfixHashAdCpuReq(V, cpuReqValid_f, cpu_req, 1);
        SetIpfixHashAdCpuReq(V, isAdMemOperation_f, cpu_req, 0);
        SetIpfixHashAdCpuReq(V, u1_gWriteByIndex_cpuIndex_f, cpu_req, in->index);
        SetIpfixHashAdCpuReq(V, u1_gWriteByIndex_dataType_f, cpu_req, in->type);
        SetIpfixHashAdCpuReq(A, u1_gWriteByIndex_data_f, cpu_req, (uint32 *) data);
        break;

    case DRV_IPFIX_ACC_DEL_AD_BY_IDX:
        sal_memset((uint8 *) data, 0, MAX_ENTRY_BYTE);
        SetIpfixHashAdCpuReq(V, cpuReqType_f, cpu_req, DRV_IPFIX_REQ_WRITE_BY_IDX);
        SetIpfixHashAdCpuReq(V, cpuReqValid_f, cpu_req, 1);
        SetIpfixHashAdCpuReq(V, isAdMemOperation_f, cpu_req, 1);
        SetIpfixHashAdCpuReq(V, u1_gWriteByIndex_cpuIndex_f, cpu_req, in->index);
        SetIpfixHashAdCpuReq(V, u1_gWriteByIndex_dataType_f, cpu_req, in->type);
        SetIpfixHashAdCpuReq(A, u1_gWriteByIndex_data_f, cpu_req, (uint32 *) data);
        break;
    case DRV_IPFIX_ACC_DEL_BY_KEY:
        sal_memset((uint8 *) data, 0, MAX_ENTRY_BYTE);
        SetIpfixHashAdCpuReq(V, cpuReqType_f, cpu_req, DRV_IPFIX_REQ_WRITE_BY_KEY);
        SetIpfixHashAdCpuReq(V, cpuReqValid_f, cpu_req, 1);
        SetIpfixHashAdCpuReq(V, isAdMemOperation_f, cpu_req, 0);
        SetIpfixHashAdCpuReq(V, u1_gWriteByKey_dataType_f, cpu_req, in->type);
        SetIpfixHashAdCpuReq(A, u1_gWriteByKey_data_f, cpu_req, (uint32 *) data);
        break;
    default:
        return DRV_E_INVAILD_TYPE;
    }

    return DRV_E_NONE;
}

int32
drv_goldengate_ipfix_acc_process(uint8 chip_id, void* i_ipfix_req, void* o_ipfix_result)
{

    DRV_PTR_VALID_CHECK(i_ipfix_req);
    DRV_PTR_VALID_CHECK(o_ipfix_result);

    DRV_PTR_VALID_CHECK(drv_gg_io_api.drv_ipfix_acc_process);

    DRV_IF_ERROR_RETURN(drv_gg_io_api.drv_ipfix_acc_process(chip_id, i_ipfix_req, o_ipfix_result));


    return DRV_E_NONE;
}

int32
drv_goldengate_ipfix_acc_get_result(uint8 acc_type, drv_ipfix_acc_in_t* in, void* req_result, drv_ipfix_acc_out_t* out)
{
    uint32                     data[MAX_ENTRY_BYTE / 4] = { 0 };
    uint32                     ret_idx     = 0;
    uint32                     is_conflict = 0;
    uint32                     idx_invalid = 0;
    uint32 length_mode = 0;
    uint32 length = 0;
    uint32 c_length = 0;

    is_conflict = GetIpfixHashAdCpuResult(V, conflictValid_f, req_result);
    idx_invalid = GetIpfixHashAdCpuResult(V, invalidIndex_f, req_result);
    ret_idx     = GetIpfixHashAdCpuResult(V, lookupReturnIndex_f, req_result);

    DRV_IF_ERROR_RETURN(_drv_goldengate_get_length_by_tbl_id(in->tbl_id, &length_mode, &length,&c_length));

    out->conflict = is_conflict;

    if (DRV_IPFIX_ACC_LKP_BY_KEY == acc_type)
    {
        out->key_index = ret_idx;
        out->hit       = idx_invalid;
        out->free      = !out->hit && !out->conflict;
    }
    else
    {
        out->invalid_idx = idx_invalid;
    }

    if ((DRV_IPFIX_ACC_LKP_KEY_BY_IDX == acc_type) || (DRV_IPFIX_ACC_LKP_AD_BY_IDX == acc_type))
    {
        DRV_IOR_FIELD(IpfixHashAdCpuResult_t, IpfixHashAdCpuResult_readData_f, (uint32 *) data, req_result);
        drv_goldengate_model_hash_un_combined_key((uint8 *) out->data, (uint8 *) data, c_length, in->tbl_id);
    }

    return DRV_E_NONE;
}

int32
drv_goldengate_ipfix_acc(uint8 chip_id, uint8 acc_type, drv_ipfix_acc_in_t* in, drv_ipfix_acc_out_t* out)
{
    IpfixHashAdCpuReq_m    cpu_req;
    IpfixHashAdCpuResult_m req_result;

    if (((DRV_IPFIX_ACC_WRITE_AD_BY_IDX == acc_type)
        || (DRV_IPFIX_ACC_LKP_AD_BY_IDX == acc_type)
         || (DRV_IPFIX_ACC_DEL_AD_BY_IDX == acc_type)) && (in->type))
    {
        return DRV_E_INVAILD_TYPE;
    }

    if (g_drv_gg_io_master.chip_agent_cb&&(TRUE == gg_client_init_done))
    {
        chip_io_para_t para;
        para.op = CHIP_IO_OP_IPFIX_ACC;
        para.u.ipfix_acc.chip_id = chip_id;
        para.u.ipfix_acc.acc_type = acc_type;
        para.u.ipfix_acc.in = in;
        para.u.ipfix_acc.out = out;
        return g_drv_gg_io_master.chip_agent_cb(0, &para);
    }
    else if(DRV_CHIP_AGT_MODE_CLIENT == drv_goldengate_chip_agent_mode())
    {
        return 0;
    }

    sal_memset(&cpu_req, 0, sizeof(IpfixHashAdCpuReq_m));
    sal_memset(&req_result, 0, sizeof(IpfixHashAdCpuResult_m));

    IPFIX_ACC_LOCK;

    DRV_IF_ERROR_RETURN_WITH_UNLOCK(drv_goldengate_ipfix_acc_prepare(acc_type, in, &cpu_req), gg_ipfix_acc_mutex);
    DRV_IF_ERROR_RETURN_WITH_UNLOCK(drv_goldengate_ipfix_acc_process(chip_id, (uint32*) &cpu_req, (uint32*)&req_result), gg_ipfix_acc_mutex);
    DRV_IF_ERROR_RETURN_WITH_UNLOCK(drv_goldengate_ipfix_acc_get_result(acc_type, in, &req_result, out), gg_ipfix_acc_mutex);

    IPFIX_ACC_UNLOCK;

    return DRV_E_NONE;
}



/**
 @file ctc_goldengate_stats.c

 @date 2009-12-22

 @version v2.0

*/

/****************************************************************************
 *
* Header Files
*
****************************************************************************/
#include "sal.h"
#include "ctc_error.h"
#include "ctc_goldengate_stats.h"
#include "sys_goldengate_stats.h"
#include "sys_goldengate_chip.h"
/****************************************************************************
 *
* Defines and Macros
*
*****************************************************************************/

/****************************************************************************
 *
* Global and Declaration
*
*****************************************************************************/
/****************************************************************************
 *
* Function
*
*****************************************************************************/

int32
ctc_goldengate_stats_init(uint8 lchip, void* stats_global_cfg)
{
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;
    ctc_stats_global_cfg_t stats_cfg;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_STATS);
    LCHIP_CHECK(lchip);
    sal_memset(&stats_cfg, 0, sizeof(stats_cfg));

    if(stats_global_cfg == NULL)
    {
        /* stats init */
        stats_cfg.stats_bitmap = CTC_STATS_ECMP_STATS;

       stats_global_cfg = &stats_cfg;
    }

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_goldengate_stats_init(lchip, (ctc_stats_global_cfg_t*)stats_global_cfg));
    }

    return CTC_E_NONE;
}

int32
ctc_goldengate_stats_deinit(uint8 lchip)
{
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_STATS);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_goldengate_stats_deinit(lchip));
    }

    return CTC_E_NONE;
}

int32
ctc_goldengate_stats_set_mac_stats_cfg(uint8 lchip, uint32 gport, ctc_mac_stats_prop_type_t mac_stats_prop_type, ctc_mac_stats_property_t prop_data)
{

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_STATS);
    LCHIP_CHECK(lchip);
    SYS_MAP_GPORT_TO_LCHIP(gport, lchip);
    switch (mac_stats_prop_type)
    {
        case CTC_STATS_PACKET_LENGTH_MTU1:
            CTC_ERROR_RETURN(sys_goldengate_stats_set_mac_packet_length_mtu1(lchip, gport, prop_data.data.length));
            break;

        case CTC_STATS_PACKET_LENGTH_MTU2:
            CTC_ERROR_RETURN(sys_goldengate_stats_set_mac_packet_length_mtu2(lchip, gport, prop_data.data.length));
            break;

        default:
            return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

int32
ctc_goldengate_stats_get_mac_stats_cfg(uint8 lchip, uint32 gport, ctc_mac_stats_prop_type_t mac_stats_prop_type, ctc_mac_stats_property_t* p_prop_data)
{

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_STATS);
    LCHIP_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_prop_data);

    SYS_MAP_GPORT_TO_LCHIP(gport, lchip);
    switch (mac_stats_prop_type)
    {
        case CTC_STATS_PACKET_LENGTH_MTU1:
            CTC_ERROR_RETURN(sys_goldengate_stats_get_mac_packet_length_mtu1(lchip, gport, &(p_prop_data->data.length)));
            break;

        case CTC_STATS_PACKET_LENGTH_MTU2:
            CTC_ERROR_RETURN(sys_goldengate_stats_get_mac_packet_length_mtu2(lchip, gport, &(p_prop_data->data.length)));
            break;

        default:
            return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

int32
ctc_goldengate_stats_get_mac_stats(uint8 lchip, uint32 gport, ctc_mac_stats_dir_t dir, ctc_mac_stats_t* p_stats)
{

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_STATS);
    LCHIP_CHECK(lchip);
    SYS_MAP_GPORT_TO_LCHIP(gport, lchip);
    switch (dir)
    {
        case CTC_STATS_MAC_STATS_RX:
            CTC_ERROR_RETURN(sys_goldengate_stats_get_mac_rx_stats(lchip, gport, p_stats));
            break;

        case CTC_STATS_MAC_STATS_TX:
            CTC_ERROR_RETURN(sys_goldengate_stats_get_mac_tx_stats(lchip, gport, p_stats));
            break;

        default:
            return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

int32
ctc_goldengate_stats_clear_mac_stats(uint8 lchip, uint32 gport, ctc_mac_stats_dir_t dir)
{

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_STATS);
    LCHIP_CHECK(lchip);
    SYS_MAP_GPORT_TO_LCHIP(gport, lchip);
    switch (dir)
    {
        case CTC_STATS_MAC_STATS_RX:
            CTC_ERROR_RETURN(sys_goldengate_stats_clear_mac_rx_stats(lchip, gport));
            break;

        case CTC_STATS_MAC_STATS_TX:
            CTC_ERROR_RETURN(sys_goldengate_stats_clear_mac_tx_stats(lchip, gport));
            break;

        default:
            return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

int32
ctc_goldengate_stats_get_cpu_mac_stats(uint8 lchip, uint32 gport, ctc_cpu_mac_stats_t* p_stats)
{

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_STATS);
    LCHIP_CHECK(lchip);
    SYS_MAP_GPORT_TO_LCHIP(gport, lchip);
    CTC_ERROR_RETURN(sys_goldengate_stats_get_cpu_mac_stats(lchip, gport, p_stats));

    return CTC_E_NONE;
}

int32
ctc_goldengate_stats_clear_cpu_mac_stats(uint8 lchip, uint32 gport)
{

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_STATS);
    LCHIP_CHECK(lchip);
    SYS_MAP_GPORT_TO_LCHIP(gport, lchip);
    CTC_ERROR_RETURN(sys_goldengate_stats_clear_cpu_mac_stats(lchip, gport));

    return CTC_E_NONE;
}

int32
ctc_goldengate_stats_set_drop_packet_stats_en(uint8 lchip, ctc_stats_discard_t bitmap, bool enable)
{
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_STATS);
    LCHIP_CHECK(lchip);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_goldengate_stats_set_drop_packet_stats_en(lchip, bitmap, enable));
    }

    return CTC_E_NONE;
}

int32
ctc_goldengate_stats_get_drop_packet_stats_en(uint8 lchip, ctc_stats_discard_t bitmap, bool* enable)
{

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_STATS);
    LCHIP_CHECK(lchip);
    CTC_ERROR_RETURN(sys_goldengate_stats_get_port_log_discard_stats_enable(lchip, bitmap, enable));

    return CTC_E_NONE;
}

int32
ctc_goldengate_stats_get_port_log_stats(uint8 lchip, uint32 gport, ctc_direction_t dir, ctc_stats_basic_t* p_stats)
{

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_STATS);
    LCHIP_CHECK(lchip);
    SYS_MAP_GPORT_TO_LCHIP(gport, lchip);
    switch (dir)
    {
        case CTC_INGRESS:
            CTC_ERROR_RETURN(sys_goldengate_stats_get_igs_port_log_stats(lchip, gport, p_stats));
            break;

        case CTC_EGRESS:
            CTC_ERROR_RETURN(sys_goldengate_stats_get_egs_port_log_stats(lchip, gport, p_stats));
            break;

        default:
            return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

int32
ctc_goldengate_stats_clear_port_log_stats(uint8 lchip, uint32 gport, ctc_direction_t dir)
{

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_STATS);
    LCHIP_CHECK(lchip);
    CTC_MAX_VALUE_CHECK(dir, CTC_BOTH_DIRECTION);

    SYS_MAP_GPORT_TO_LCHIP(gport, lchip);

    if ((CTC_INGRESS == dir) || (CTC_BOTH_DIRECTION == dir))
    {
        CTC_ERROR_RETURN(sys_goldengate_stats_clear_igs_port_log_stats(lchip, gport));
    }

    if ((CTC_EGRESS == dir) || (CTC_BOTH_DIRECTION == dir))
    {
        CTC_ERROR_RETURN(sys_goldengate_stats_clear_egs_port_log_stats(lchip, gport));
    }

    return CTC_E_NONE;
}

int32
ctc_goldengate_stats_set_global_cfg(uint8 lchip, ctc_stats_property_param_t stats_param, ctc_stats_property_t stats_prop)
{
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_STATS);
    LCHIP_CHECK(lchip);
    switch (stats_param.prop_type)
    {
        case CTC_STATS_PROPERTY_SATURATE:
            CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
            {
                CTC_ERROR_RETURN(sys_goldengate_stats_set_saturate_en(lchip, stats_param.stats_type, stats_prop.data.enable));
            }
            break;

        case CTC_STATS_PROPERTY_HOLD:
            CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
            {
                CTC_ERROR_RETURN(sys_goldengate_stats_set_hold_en(lchip, stats_param.stats_type, stats_prop.data.enable));
            }
            break;

        case CTC_STATS_PROPERTY_PKT_CNT_THREASHOLD:
        case CTC_STATS_PROPERTY_BYTE_CNT_THREASHOLD:
        case CTC_STATS_PROPERTY_FIFO_DEPTH_THREASHOLD:
            return CTC_E_FEATURE_NOT_SUPPORT;

        default:
            return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

int32
ctc_goldengate_stats_get_global_cfg(uint8 lchip, ctc_stats_property_param_t stats_param, ctc_stats_property_t* p_stats_prop)
{

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_STATS);
    LCHIP_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_stats_prop);
    switch (stats_param.prop_type)
    {
        case CTC_STATS_PROPERTY_SATURATE:
            CTC_ERROR_RETURN(sys_goldengate_stats_get_saturate_en(lchip, stats_param.stats_type, &(p_stats_prop->data.enable)));
            break;

        case CTC_STATS_PROPERTY_HOLD:
            CTC_ERROR_RETURN(sys_goldengate_stats_get_hold_en(lchip, stats_param.stats_type, &(p_stats_prop->data.enable)));
            break;

        case CTC_STATS_PROPERTY_PKT_CNT_THREASHOLD:
        case CTC_STATS_PROPERTY_BYTE_CNT_THREASHOLD:
        case CTC_STATS_PROPERTY_FIFO_DEPTH_THREASHOLD:
            return CTC_E_FEATURE_NOT_SUPPORT;

        default:
            return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

int32
ctc_goldengate_stats_create_statsid(uint8 lchip, ctc_stats_statsid_t* statsid)
{
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;
    int32 ret = CTC_E_NONE;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_STATS);
    LCHIP_CHECK(lchip);
    CTC_PTR_VALID_CHECK(statsid);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_FOREACH_ERROR_RETURN(CTC_E_ENTRY_EXIST,
            ret,
            sys_goldengate_stats_create_statsid(lchip, statsid));
    }

    /*rollback if error exist*/
    CTC_FOREACH_ROLLBACK(lchip_start,lchip_end)
    {
        sys_goldengate_stats_destroy_statsid(lchip,statsid->stats_id);
    }

    return ret;
}

int32
ctc_goldengate_stats_destroy_statsid(uint8 lchip, uint32 stats_id)
{
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;
    int32 ret = CTC_E_NONE;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_STATS);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_FOREACH_ERROR_RETURN(CTC_E_ENTRY_NOT_EXIST,
            ret,
            sys_goldengate_stats_destroy_statsid(lchip, stats_id));
    }

    return ret;
}

int32
ctc_goldengate_stats_get_stats(uint8 lchip, uint32 stats_id, ctc_stats_basic_t* p_stats)
{
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;
    ctc_stats_basic_t  stats;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_STATS);
    LCHIP_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_stats);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        sal_memset(&stats, 0, sizeof(stats));
        CTC_ERROR_RETURN(sys_goldengate_stats_get_stats(lchip, stats_id, &stats));
        p_stats->byte_count += stats.byte_count;
        p_stats->packet_count += stats.packet_count;
    }

    return CTC_E_NONE;
}

int32
ctc_goldengate_stats_clear_stats(uint8 lchip, uint32 stats_id)
{
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_STATS);
    LCHIP_CHECK(lchip);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_goldengate_stats_clear_stats(lchip, stats_id));
    }

    return CTC_E_NONE;
}

int32
ctc_goldengate_stats_intr_callback_func(uint8 lchip, uint8* p_gchip)
{

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_STATS);
    LCHIP_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_gchip);
    SYS_MAP_GCHIP_TO_LCHIP(*p_gchip, lchip);
    CTC_ERROR_RETURN(sys_goldengate_stats_intr_callback_func(lchip, p_gchip));

    return CTC_E_NONE;
}


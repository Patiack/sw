#include "ctc_pump.h"
#include "ctcs_api.h"

#define BUFFER_CELL_UNITS 288

#define PUMP_MAP_DIR(dir) (((dir) == CTC_INGRESS) ? CTC_PUMP_DIR_IGS : CTC_PUMP_DIR_EGS)
#define SDK_MAP_DIR(dir) (((dir) == CTC_PUMP_DIR_IGS) ? CTC_INGRESS : CTC_EGRESS)

#define TIMESTAMP_COMPARE(CPU, ASIC) ((CPU > ASIC) ? (((CPU - ASIC) >= 24*3600) ? 1 : 0) : ((((ASIC - CPU) >= 24*3600) ? 1 : 0)))

#define MONITOR_BURST_THRD 400 /*burst threshold*/


static sal_time_t g_pump_monitor_time = 0;
static uint64 g_pump_monitor_time_base = 0;

#define PUMP_MONITOR_TIME_DECREASE(a, b) ((a > b) ? (a-b) : (b-a))

uint32
ctc_pump_sdk_get_max_port_num(uint8 lchip)
{
    int32 ret = 0;
    uint32 capability[CTC_GLOBAL_CAPABILITY_MAX] = {0};
    ret = ctcs_global_ctl_get(lchip, CTC_GLOBAL_CHIP_CAPABILITY, capability);
    if (ret < 0)
    {
        PUMP_LOG(CTC_PUMP_LOG_ERROR, "ret=%d!",ret);
        return 0;
    }
    return capability[CTC_GLOBAL_CAPABILITY_MAX_PHY_PORT_NUM];
}


uint32
ctc_pump_sdk_get_network_port_num(uint8 lchip)
{
    uint32 max_port_num = 0;
    uint32 idx = 0;
    uint32 num = 0;
    uint8 gchip = 0;
    bool enable;
    int32 ret = 0;

    ctcs_get_gchip_id(lchip, &gchip);
    max_port_num = ctc_pump_sdk_get_max_port_num(lchip);
    for (idx = 0; idx < max_port_num; idx++)
    {
        ret = ctcs_port_get_mac_en(lchip, CTC_MAP_LPORT_TO_GPORT(gchip, idx), &enable);
        if (ret < 0)
        {
            continue;
        }
        num++;
    }
    return num;
}



int32
ctc_pump_sdk_get_lchip(uint8 gchip_id, uint8* lchip_id)
{
    int32 ret = 0;
    uint8 gchip = 0;
    uint8 lchip = 0;
    uint8 lchip_num = 0;

    if (NULL == lchip_id)
    {
        PUMP_LOG(CTC_PUMP_LOG_ERROR, "NULL PTR!");
        return -1;
    }

    ret = ctcs_get_local_chip_num(lchip, &lchip_num);
    if (ret < 0)
    {
        PUMP_LOG(CTC_PUMP_LOG_ERROR, "ret=%d!", ret);
        return -1;
    }
    for (lchip = 0; lchip < lchip_num; lchip++)
    {
        ret = ctcs_get_gchip_id(lchip, &gchip);
        if (ret < 0)
        {
            PUMP_LOG(CTC_PUMP_LOG_ERROR, "lchip=%d ret=%d!", lchip, ret);
            return -1;
        }
        if (gchip == gchip_id)
        {
            *lchip_id = lchip;
            return 0;
        }
    }
    PUMP_LOG(CTC_PUMP_LOG_ERROR, "gchip not found!");
    return -1;
}

int32
ctc_pump_sdk_get_mburst_thrd(uint8 lchip, uint32* thrd)
{
    int32 ret = 0;
    uint8 idx = 0;
    ctc_monitor_glb_cfg_t glb_cfg;

    if (NULL == thrd)
    {
        PUMP_LOG(CTC_PUMP_LOG_ERROR, "NULL PTR!");
        return -1;
    }
    sal_memset(&glb_cfg, 0, sizeof(glb_cfg));


    glb_cfg.cfg_type = CTC_MONITOR_GLB_CONFIG_MBURST_THRD;
    for (idx = 0; idx < CTC_PUMP_LATENCY_LEVEL; idx++)
    {
        glb_cfg.u.mburst_thrd.level = idx;
        ret = ctcs_monitor_get_global_config(lchip, &glb_cfg);
        if (ret < 0)
        {
            PUMP_LOG(CTC_PUMP_LOG_ERROR, "level=%d ret=%d!", idx, ret);
            continue;
        }
        thrd[idx] = glb_cfg.u.mburst_thrd.threshold;
    }
    return 0;
}

int32
ctc_pump_sdk_get_latency_thrd(uint8 lchip, uint32* thrd)
{
    int32 ret = 0;
    uint8 idx = 0;
    ctc_monitor_glb_cfg_t glb_cfg;

    if (NULL == thrd)
    {
        PUMP_LOG(CTC_PUMP_LOG_ERROR, "NULL PTR!");
        return -1;
    }
    sal_memset(&glb_cfg, 0, sizeof(glb_cfg));

    glb_cfg.cfg_type = CTC_MONITOR_GLB_CONFIG_LATENCY_THRD;
    for (idx = 0; idx < CTC_PUMP_LATENCY_LEVEL; idx++)
    {
        glb_cfg.u.thrd.level = idx;
        ret = ctcs_monitor_get_global_config(lchip, &glb_cfg);
        if (ret < 0)
        {
            PUMP_LOG(CTC_PUMP_LOG_ERROR, "level=%d ret=%d!", idx, ret);
            continue;
        }
        thrd[idx] = glb_cfg.u.thrd.threshold;
    }
    return 0;
}


void
ctc_pump_sdk_push_data(uint8 lchip, ctc_pump_func_data_t* push_data)
{
    ctc_pump_push_cb fn = ctc_pump_push_get_cb(lchip);
    int ret = 0;
    if (fn && push_data && push_data->count)
    {
        ret = (*fn)(push_data);
        if (ret)
        {
            PUMP_LOG(CTC_PUMP_LOG_ERROR, "ret=%d, switchId=%d, type:%d, count=%d", ret, push_data->switch_id, push_data->type, push_data->count);
        }
    }
}


#define PKT_DECODE(V, N, L) \
do { \
    sal_memcpy((V), (*N), (L)); \
    *N += (L); \
} while (0)

#define PKT_DECODE_EMPTY(N, L) \
    *N += (L); \

#define CONVERT_U16(DST, SRC)\
        DST = (((SRC) & 0xFF) << 8) + ((SRC) >> 8);
void
ctc_pump_sdk_pkt_parse_ip(uint32 len, uint8* p_buf, ctc_pump_pkt_info_t* pkt_info)
{
    uint8 ip_proto = 0;
    uint16 val = 0;
    uint8** pn = NULL;
    if (len == 0
        || NULL == p_buf
        || NULL == pkt_info)
    {
        return;
    }
    pn = &p_buf;
    if (pkt_info->protocol == 0x86dd)
    {
         /*Ipv6*/
        PKT_DECODE_EMPTY(pn, 6);
        PKT_DECODE(&ip_proto, pn, 1);  /* Next Header */
        PKT_DECODE_EMPTY(pn, 1);
        PKT_DECODE(&pkt_info->ip_sa.v6, pn, 16);
        PKT_DECODE(&pkt_info->ip_da.v6, pn, 16);
        pkt_info->masks |= (CTC_PUMP_PKT_MASK_IP6DA | CTC_PUMP_PKT_MASK_IP6SA);
    }
    else
    {
        /*Ipv4 proto, + 9*/
        PKT_DECODE_EMPTY(pn, 9);
        PKT_DECODE(&ip_proto, pn, 1);  /* IP Protocol */
        PKT_DECODE_EMPTY(pn, 2);
        /*Ipv4 , + 12*/
        PKT_DECODE(&pkt_info->ip_sa.v4, pn, 4);
        PKT_DECODE(&pkt_info->ip_da.v4, pn, 4);
        pkt_info->masks |= (CTC_PUMP_PKT_MASK_IPDA | CTC_PUMP_PKT_MASK_IPSA);
    }
    /*tcp 6, udp 17*/
    if (ip_proto == 6
        || ip_proto == 17)
    {
        /*srcport, dstport*/
        PKT_DECODE(&val, pn, 2);
        CONVERT_U16(pkt_info->src_port, val);
        PKT_DECODE(&val, pn, 2);
        CONVERT_U16(pkt_info->dst_port, val);
        pkt_info->masks |= (CTC_PUMP_PKT_MASK_SRCPORT | CTC_PUMP_PKT_MASK_DSTPORT);
    }
}

void
ctc_pump_sdk_pkt_parse_eth(uint32 len, uint8* p_buf, ctc_pump_pkt_info_t* pkt_info)
{
    uint8** pn = NULL;
    uint32 idx = 0;
    uint16 val = 0;
    uint16 protocol = 0;
    if (len == 0
        || NULL == p_buf
        || NULL == pkt_info)
    {
        return;
    }
    pn = &p_buf;
    PKT_DECODE(pkt_info->mac_da, pn, 6);
    PKT_DECODE(pkt_info->mac_sa, pn, 6);
    pkt_info->masks |= (CTC_PUMP_PKT_MASK_MACDA | CTC_PUMP_PKT_MASK_MACSA);
    do
    {
        PKT_DECODE(&val, pn, 2);
        protocol = ((val & 0xFF) << 8) + (val >> 8);
        if (protocol
            && (protocol != 0x8100))
        {
            break;
        }
        PKT_DECODE_EMPTY(pn, 2);
        idx++;
        if (idx >= 3)
        {
            break;
        }
    }while((len - idx * 4) > 0);

    if ((protocol & 0xF800) && (protocol != 0x8100))
    {
        pkt_info->protocol = protocol;
        pkt_info->masks |= CTC_PUMP_PKT_MASK_PROTO;
    }

    if (pkt_info->protocol == 0x0800
        || pkt_info->protocol == 0x86dd)
    {
        /* Ipv4 || Ipv6 */
        ctc_pump_sdk_pkt_parse_ip(len, *pn, pkt_info);
    }
}

#define ______SDK_API_Redirect______

int32
ctc_pump_sdk_monitor_msg_event(ctc_monitor_msg_t* msg, void* userdata)
{
    uint8 lchip = 0;
    ctc_pump_func_data_t push_data;
    ctc_pump_latency_t lat_event;
    ctc_pump_buffer_t buf_event;
    sal_time_t cpu_time;

    if (NULL == msg)
    {
        PUMP_LOG(CTC_PUMP_LOG_ERROR, "NULL PTR!");
        return -1;
    }

    sal_memset(&push_data, 0, sizeof(push_data));
    switch (msg->monitor_type)
    {
        case CTC_MONITOR_BUFFER:
            sal_memset(&buf_event, 0, sizeof(buf_event));
            push_data.type = CTC_PUMP_FUNC_REPORT_BUFFER_EVENT;
            buf_event.masks = CTC_PUMP_BUF_TYPE | CTC_PUMP_BUF_TOTAL_BYTES;
            switch(msg->u.buffer_event.buffer_type)
            {
                case CTC_MONITOR_BUFFER_PORT:
                    buf_event.type = CTC_PUMP_BUFFER_PORT;
                    buf_event.port = msg->u.buffer_event.gport;
                    buf_event.total_bytes = (msg->u.buffer_event.uc_cnt + msg->u.buffer_event.mc_cnt) * BUFFER_CELL_UNITS;
                    buf_event.masks |= CTC_PUMP_BUF_PORT;
                    ctc_pump_sdk_get_lchip(CTC_MAP_GPORT_TO_GCHIP(buf_event.port), &lchip);
                    break;
                case CTC_MONITOR_BUFFER_SC:
                    buf_event.type = CTC_PUMP_BUFFER_POOL;
                    buf_event.pool = msg->u.buffer_event.sc;
                    buf_event.total_bytes = msg->u.buffer_event.buffer_cnt * BUFFER_CELL_UNITS;
                    buf_event.masks |= CTC_PUMP_BUF_POOL;
                    break;
                case CTC_MONITOR_BUFFER_TOTAL:
                    buf_event.type = CTC_PUMP_BUFFER_TOTAL;
                    buf_event.total_bytes = msg->u.buffer_event.buffer_cnt * BUFFER_CELL_UNITS;
                    break;
                case CTC_MONITOR_BUFFER_C2C:
                    buf_event.type = CTC_PUMP_BUFFER_C2C;
                    buf_event.total_bytes = msg->u.buffer_event.buffer_cnt * BUFFER_CELL_UNITS;
                    break;
                default:
                    return -1;
            }
            push_data.data = &buf_event;
            break;
        case CTC_MONITOR_LATENCY:
            sal_memset(&lat_event, 0, sizeof(lat_event));
            push_data.type = CTC_PUMP_FUNC_REPORT_LATENCY_EVENT;
            lat_event.port = msg->u.latency_event.gport;
            lat_event.level = msg->u.latency_event.level;
            lat_event.nanoseconds = msg->u.latency_event.latency;
            ctc_pump_sdk_get_lchip(CTC_MAP_GPORT_TO_GCHIP(lat_event.port), &lchip);
            ctc_pump_sdk_get_mburst_thrd(lchip, lat_event.thrd);
            lat_event.masks = CTC_PUMP_LAT_PORT | CTC_PUMP_LAT_LEVEL | CTC_PUMP_LAT_NS;
            push_data.data = &lat_event;
            break;
        default:
            return -1;
    }
    push_data.count = 1;
    sal_time(&cpu_time);

    msg->timestamp = msg->timestamp/1000000000L;/*Nanoseconds covert to seconds*/

    if (TIMESTAMP_COMPARE(cpu_time, msg->timestamp))
    {
        push_data.timestamp = g_pump_monitor_time + PUMP_MONITOR_TIME_DECREASE(msg->timestamp, g_pump_monitor_time_base);
    }
    else
    {
        sal_memcpy(&push_data.timestamp, &msg->timestamp, sizeof(sal_time_t));
    }
    if (ctc_pump_func_is_enable(lchip, push_data.type))
    {
        ctc_pump_sdk_push_data(lchip, &push_data);
    }

    return 0;
}

int32
ctc_pump_sdk_monitor_msg_stats(ctc_monitor_msg_t* msg, void* userdata)
{
    uint8 lchip = 0;
    ctc_pump_func_data_t push_data;
    ctc_pump_latency_t lat_stats;
    ctc_pump_buffer_t buf_stats;
    sal_time_t cpu_time;
    if (NULL == msg)
    {
        PUMP_LOG(CTC_PUMP_LOG_ERROR, "NULL PTR!");
        return -1;
    }

    return 0;

    sal_memset(&push_data, 0, sizeof(push_data));
    switch (msg->monitor_type)
    {
        case CTC_MONITOR_BUFFER:
            sal_memset(&buf_stats, 0, sizeof(buf_stats));
            push_data.type = CTC_PUMP_FUNC_REPORT_BUFFER_STATS;
            buf_stats.dir = PUMP_MAP_DIR(msg->u.buffer_stats.dir);
            buf_stats.total_bytes = msg->u.buffer_stats.buffer_cnt * BUFFER_CELL_UNITS;
            buf_stats.masks = CTC_PUMP_BUF_TYPE | CTC_PUMP_BUF_DIR | CTC_PUMP_BUF_TOTAL_BYTES;

            switch(msg->u.buffer_event.buffer_type)
            {
                case CTC_MONITOR_BUFFER_PORT:
                    buf_stats.type = CTC_PUMP_BUFFER_PORT;
                    buf_stats.port = msg->u.buffer_stats.gport;
                    buf_stats.masks |= CTC_PUMP_BUF_PORT;
                    break;
                case CTC_MONITOR_BUFFER_SC:
                    buf_stats.type = CTC_PUMP_BUFFER_POOL;
                    buf_stats.pool = msg->u.buffer_stats.sc;
                    buf_stats.masks |= CTC_PUMP_BUF_POOL;
                    break;
                case CTC_MONITOR_BUFFER_TOTAL:
                   buf_stats.type |= CTC_PUMP_BUFFER_TOTAL;
                    break;
                default:
                    return -1;
            }
            push_data.data = &buf_stats;
            break;
        case CTC_MONITOR_LATENCY:
            sal_memset(&lat_stats, 0, sizeof(lat_stats));
            push_data.type = CTC_PUMP_FUNC_REPORT_LATENCY_STATS;
            lat_stats.port = msg->u.latency_stats.gport;
            sal_memcpy(&lat_stats.count, msg->u.latency_stats.threshold_cnt, CTC_PUMP_LATENCY_LEVEL * sizeof(uint32));
            ctc_pump_sdk_get_latency_thrd(lchip, lat_stats.thrd);
            lat_stats.masks = CTC_PUMP_LAT_PORT | CTC_PUMP_LAT_COUNT | CTC_PUMP_LAT_THRD;
            push_data.data = &lat_stats;
            break;
        default:
            return -1;
    }
    push_data.count = 1;
    sal_time(&cpu_time);
    msg->timestamp = msg->timestamp/1000000000L;/*Nanoseconds covert to seconds*/
    if (TIMESTAMP_COMPARE(cpu_time, msg->timestamp))
    {
        push_data.timestamp = g_pump_monitor_time + PUMP_MONITOR_TIME_DECREASE(msg->timestamp, g_pump_monitor_time_base);
    }
    else
    {
        sal_memcpy(&push_data.timestamp, &msg->timestamp, sizeof(sal_time_t));
    }

    ctc_pump_sdk_get_lchip(msg->gchip, &lchip);
    if (ctc_pump_func_is_enable(lchip, push_data.type))
    {
        ctc_pump_sdk_push_data(lchip, &push_data);
    }

    return 0;
}


int32
ctc_pump_sdk_monitor_msg_mburst(ctc_monitor_msg_t* msg, void* userdata)
{
    uint8 lchip = 0;
    ctc_pump_func_data_t push_data;
    ctc_pump_mburst_t  mburst;
    sal_time_t cpu_time;
    if (NULL == msg)
    {
        PUMP_LOG(CTC_PUMP_LOG_ERROR, "NULL PTR!");
        return -1;
    }

    sal_memset(&mburst, 0, sizeof(mburst));
    mburst.port = msg->u.mburst_stats.gport;
    sal_memcpy(mburst.count, msg->u.mburst_stats.threshold_cnt, CTC_PUMP_LATENCY_LEVEL * sizeof(uint32));
    ctc_pump_sdk_get_mburst_thrd(0, mburst.thrd);
    mburst.masks = CTC_PUMP_LAT_PORT | CTC_PUMP_LAT_COUNT | CTC_PUMP_LAT_THRD;
    push_data.type = CTC_PUMP_FUNC_REPORT_MBURST_EVENT;
    push_data.count = 1;
    push_data.data = &mburst;
    sal_time(&cpu_time);
    msg->timestamp = msg->timestamp/1000000000L;/*Nanoseconds covert to seconds*/
    if (TIMESTAMP_COMPARE(cpu_time, msg->timestamp))
    {
        push_data.timestamp = g_pump_monitor_time + PUMP_MONITOR_TIME_DECREASE(msg->timestamp, g_pump_monitor_time_base);
    }
    else
    {
        sal_memcpy(&push_data.timestamp, &msg->timestamp, sizeof(sal_time_t));
    }
    ctc_pump_sdk_get_lchip(msg->gchip, &lchip);
    if (ctc_pump_func_is_enable(lchip, push_data.type))
    {
        ctc_pump_sdk_push_data(lchip, &push_data);
    }

    return 0;
}

void
ctc_pump_sdk_monitor_cb(ctc_monitor_data_t* info, void* userdata)
{
    uint32 msg_idx = 0;

    if (NULL == info)
    {
        PUMP_LOG(CTC_PUMP_LOG_ERROR, "NULL PTR!");
        return;
    }

    while (msg_idx < info->msg_num)
    {
        if (g_pump_monitor_time_base == 0)
        {
            g_pump_monitor_time_base = info->p_msg[msg_idx].timestamp / 1000000000L;
        }
        switch (info->p_msg[msg_idx].msg_type)
        {
            case CTC_MONITOR_MSG_EVENT:
                ctc_pump_sdk_monitor_msg_event(&(info->p_msg[msg_idx]), userdata);
                break;
            case CTC_MONITOR_MSG_STATS:
                /*ctc_pump_sdk_monitor_msg_stats(&(info->p_msg[msg_idx]), userdata);*/
                break;
            case CTC_MONITOR_MSG_MBURST_STATS:
                ctc_pump_sdk_monitor_msg_mburst(&(info->p_msg[msg_idx]), userdata);
                break;
            default:
                break;
        }
        msg_idx++;
        /*sal_task_sleep(1);*/
    }
}

int32
ctc_pump_sdk_drop_info_cb(uint32 gport, uint16 reason, char* desc, ctc_diag_drop_info_buffer_t* pkt_buf)
{
    int32 ret = 0;
    uint8 lchip = 0;
    uint8 idx = 0;
    uint8 reason_str = 0;
    ctc_pump_drop_info_t drop_report;
    ctc_pump_func_data_t push_data;
    if (NULL == pkt_buf)
    {
        PUMP_LOG(CTC_PUMP_LOG_ERROR, "NULL PTR!");
        return -1;
    }

    PUMP_LOG(CTC_PUMP_LOG_INFO, "drop port:0x%x reason:%d", gport, reason);
    sal_memset(&push_data, 0, sizeof(push_data));
    sal_memset(&drop_report, 0, sizeof(drop_report));

    ret = ctc_pump_sdk_get_lchip(CTC_MAP_GPORT_TO_GCHIP(gport), &lchip);
    if (ret)
    {
        PUMP_LOG(CTC_PUMP_LOG_ERROR, "ret=%d!", ret);
    }
    drop_report.pkt_id = pkt_buf->pkt_id;
    drop_report.port = gport;
    drop_report.pkt_len = pkt_buf->len;
    drop_report.pkt_buf = pkt_buf->pkt_buf;
    while (idx < CTC_DIAG_REASON_DESC_LEN)
    {
        if (desc[idx] == ',')
        {
            reason_str = 1;
            if (desc[idx+1] == ' ')
            {
                idx++;
            }
        }
        if (reason_str)
        {
            drop_report.reason[reason_str-1] = desc[idx];
            reason_str++;
        }
        idx++;
    }
    ctc_pump_sdk_pkt_parse_eth(pkt_buf->len, pkt_buf->pkt_buf, &drop_report.info);
    push_data.type = CTC_PUMP_FUNC_REPORT_DROP_INFO;
    push_data.count = 1;
    sal_memcpy(&push_data.timestamp,&pkt_buf->timestamp, sizeof(push_data.timestamp));
    push_data.data = &drop_report;
    ctc_pump_sdk_push_data(lchip, &push_data);

    return 0;
}


int32
ctc_pump_sdk_pkt_report_cb(ctc_pkt_rx_t* p_pkt_rx)
{
    uint32 hdr_len = 0;
    ctc_pump_efd_info_t efd_report;
    ctc_pump_drop_info_t drop_report;
    ctc_pump_func_data_t push_data;
    uint8* pkt_buf = NULL;
    uint8 pkt_len = 0;
    ctc_pump_pkt_info_t* info = NULL;
    if (NULL == p_pkt_rx)
    {
        PUMP_LOG(CTC_PUMP_LOG_ERROR, "NULL PTR!");
        return -1;
    }

    sal_memset(&push_data, 0, sizeof(push_data));
    sal_memset(&efd_report, 0, sizeof(efd_report));
    sal_memset(&drop_report, 0, sizeof(drop_report));

    hdr_len = p_pkt_rx->eth_hdr_len + p_pkt_rx->pkt_hdr_len + p_pkt_rx->stk_hdr_len;
    pkt_len = p_pkt_rx->pkt_len - hdr_len - 4; /*without CRC Length (4 bytes)*/
    pkt_buf = p_pkt_rx->pkt_buf[0].data + hdr_len;

    switch (p_pkt_rx->rx_info.reason)
    {
        case CTC_PKT_CPU_REASON_QUEUE_DROP_PKT:
            push_data.type = CTC_PUMP_FUNC_REPORT_DROP_INFO;
            drop_report.pkt_len = pkt_len;
            drop_report.pkt_buf = pkt_buf;
            drop_report.port = p_pkt_rx->rx_info.src_port;
            sal_strcat(drop_report.reason, "Enqueue Drop");
            info = &drop_report.info;
            push_data.data = &drop_report;
            break;
        case CTC_PKT_CPU_REASON_NEW_ELEPHANT_FLOW:
            push_data.type = CTC_PUMP_FUNC_REPORT_EFD;
            efd_report.pkt_len = pkt_len;
            efd_report.pkt_buf = pkt_buf;
            efd_report.port = p_pkt_rx->rx_info.src_port;
            info = &efd_report.info;
            push_data.data = &efd_report;
            break;
        default:
            return -1;
    }
    if (p_pkt_rx->rx_info.packet_type == CTC_PARSER_PKT_TYPE_ETHERNET)
    {
        ctc_pump_sdk_pkt_parse_eth(pkt_len, pkt_buf, info);
    }
    else
    {
        ctc_pump_sdk_pkt_parse_ip(pkt_len, pkt_buf, info);
    }

    PUMP_LOG(CTC_PUMP_LOG_INFO, "reason:%d proto:0x%.4x len:%d", p_pkt_rx->rx_info.reason, info->protocol, pkt_len);
    push_data.count = 1;
    sal_time(&push_data.timestamp);
    ctc_pump_sdk_push_data(p_pkt_rx->lchip, &push_data);
    return 0;
}


int32
_ctc_pump_sdk_query_temperature(uint8 lchip, uint32* temp)
{
    int32 ret = ctcs_get_chip_sensor(lchip, CTC_CHIP_SENSOR_TEMP, temp);
    if (ret < 0)
    {
        PUMP_LOG(CTC_PUMP_LOG_ERROR, "ret=%d!",ret);
        return -1;
    }
    return 0;
}

int32
_ctc_pump_sdk_query_voltage(uint8 lchip, uint32* voltage)
{
    int32 ret = ctcs_get_chip_sensor(lchip, CTC_CHIP_SENSOR_VOLTAGE, voltage);
    if (ret < 0)
    {
        PUMP_LOG(CTC_PUMP_LOG_ERROR, "ret=%d!",ret);
        return -1;
    }
    return 0;
}

int32
_ctc_pump_sdk_query_mac_stats(uint8 lchip, ctc_pump_mac_stats_t* stats)
{
    int32 ret = 0;
    ctc_mac_stats_t mac_stats;
    if (NULL == stats)
    {
        PUMP_LOG(CTC_PUMP_LOG_ERROR, "NULL PTR!");
        return -1;
    }
    sal_memset(&mac_stats, 0, sizeof(mac_stats));
    ret = ctcs_stats_get_mac_stats(lchip, stats->port, CTC_STATS_MAC_STATS_RX, &mac_stats);
    if (ret < 0)
    {
        PUMP_LOG(CTC_PUMP_LOG_ERROR, "port:0x%x ret=%d!",stats->port, ret);
        return -1;
    }
    stats->rx_pkts = mac_stats.u.stats_plus.stats.rx_stats_plus.all_pkts;
    stats->rx_bytes = mac_stats.u.stats_plus.stats.rx_stats_plus.all_octets;
    sal_memset(&mac_stats, 0, sizeof(mac_stats));
    ret = ctcs_stats_get_mac_stats(lchip, stats->port, CTC_STATS_MAC_STATS_TX, &mac_stats);
    if (ret < 0)
    {
        PUMP_LOG(CTC_PUMP_LOG_ERROR, "port:0x%x ret=%d!",stats->port, ret);
        return -1;
    }
    stats->tx_pkts = mac_stats.u.stats_plus.stats.tx_stats_plus.all_pkts;
    stats->tx_bytes = mac_stats.u.stats_plus.stats.tx_stats_plus.all_octets;


    sal_memset(&mac_stats, 0, sizeof(mac_stats));
    mac_stats.stats_mode = CTC_STATS_MODE_DETAIL;
    ret = ctcs_stats_get_mac_stats(lchip, stats->port, CTC_STATS_MAC_STATS_RX, &mac_stats);
    if (ret < 0)
    {
        PUMP_LOG(CTC_PUMP_LOG_ERROR, "port:0x%x ret=%d!",stats->port, ret);
        return -1;
    }
    stats->rx_mac_overrun_pkts = mac_stats.u.stats_detail.stats.rx_stats.mac_overrun_pkts;
    stats->rx_fcs_error_pkts = mac_stats.u.stats_detail.stats.rx_stats.fcs_error_pkts;
    stats->rx_bad_63_pkts = mac_stats.u.stats_detail.stats.rx_stats.bad_63_pkts;
    stats->rx_bad_1519_pkts = mac_stats.u.stats_detail.stats.rx_stats.bad_1519_pkts;
    stats->rx_bad_jumbo_pkts = mac_stats.u.stats_detail.stats.rx_stats.bad_jumbo_pkts;
    sal_memset(&mac_stats, 0, sizeof(mac_stats));
    mac_stats.stats_mode = CTC_STATS_MODE_DETAIL;
    ret = ctcs_stats_get_mac_stats(lchip, stats->port, CTC_STATS_MAC_STATS_TX, &mac_stats);
    if (ret < 0)
    {
        PUMP_LOG(CTC_PUMP_LOG_ERROR, "port:0x%x ret=%d!",stats->port, ret);
        return -1;
    }
    stats->tx_pkts_63 = mac_stats.u.stats_detail.stats.tx_stats.pkts_63;
    stats->tx_jumbo_pkts = mac_stats.u.stats_detail.stats.tx_stats.jumbo_pkts;
    stats->tx_mac_underrun_pkts = mac_stats.u.stats_detail.stats.tx_stats.mac_underrun_pkts;
    stats->tx_fcs_error_pkts = mac_stats.u.stats_detail.stats.tx_stats.fcs_error_pkts;
    return 0;
}

int32
_ctc_pump_sdk_query_mburst_stats(uint8 lchip, ctc_pump_mburst_t* stats)
{
    int32 ret = 0;
    uint8 level = 0;
    ctc_monitor_config_t cfg;

    if (NULL == stats)
    {
        PUMP_LOG(CTC_PUMP_LOG_ERROR, "NULL PTR!");
        return -1;
    }

    sal_memset(&cfg, 0, sizeof(cfg));
    cfg.monitor_type = CTC_MONITOR_BUFFER;
    cfg.cfg_type = CTC_MONITOR_RETRIEVE_MBURST_STATS;
    cfg.gport = stats->port;
    for (level = 0; level < CTC_PUMP_LATENCY_LEVEL; level++)
    {
        cfg.level = level;
        ret = ctcs_monitor_get_config(lchip, &cfg);
        if (ret < 0)
        {
            PUMP_LOG(CTC_PUMP_LOG_ERROR, "level:%d port:0x%x ret=%d!",level,cfg.gport, ret);
            return -1;
        }
        stats->count[level] = cfg.value;
    }

    /* Obtain Level Threshold*/
    ctc_pump_sdk_get_mburst_thrd(lchip, stats->thrd);
    stats->masks |= CTC_PUMP_LAT_THRD | CTC_PUMP_LAT_COUNT;
    return 0;
}

int32
_ctc_pump_sdk_query_buffer_watermark(uint8 lchip, ctc_pump_buffer_t* watermark)
{
    int32 ret = 0;
    ctc_monitor_watermark_t wm;

    if (NULL == watermark)
    {
        PUMP_LOG(CTC_PUMP_LOG_ERROR, "NULL PTR!");
        return -1;
    }

    sal_memset(&wm, 0, sizeof(wm));
    wm.monitor_type = CTC_MONITOR_BUFFER;
    wm.u.buffer.dir = SDK_MAP_DIR(watermark->dir);
    switch (watermark->type)
    {
        case CTC_PUMP_BUFFER_TOTAL:
            wm.u.buffer.buffer_type = CTC_MONITOR_BUFFER_TOTAL;
            break;
        case CTC_PUMP_BUFFER_POOL:
            wm.u.buffer.buffer_type = CTC_MONITOR_BUFFER_SC;
            wm.u.buffer.sc = watermark->pool;
            break;
        case CTC_PUMP_BUFFER_PORT:
            wm.gport = watermark->port;
            wm.u.buffer.buffer_type = CTC_MONITOR_BUFFER_PORT;
            break;
        default:
            return -1;
    }
    ret = ctcs_monitor_get_watermark(lchip, &wm);
    if (ret < 0)
    {
        return -1;
    }
    watermark->total_bytes = wm.u.buffer.max_total_cnt * BUFFER_CELL_UNITS;
    watermark->masks |= CTC_PUMP_BUF_TOTAL_BYTES;

    ret = ctcs_monitor_clear_watermark(lchip, &wm);
    if (ret < 0)
    {
        return -1;
    }
    return 0;
}

int32
_ctc_pump_sdk_query_latency_watermark(uint8 lchip, ctc_pump_latency_t* watermark)
{
    int32 ret = 0;
    ctc_monitor_watermark_t wm;

    if (NULL == watermark)
    {
        PUMP_LOG(CTC_PUMP_LOG_ERROR, "NULL PTR!");
        return -1;
    }

    sal_memset(&wm, 0, sizeof(wm));
    wm.monitor_type = CTC_MONITOR_LATENCY;
    wm.gport = watermark->port;
    ret = ctcs_monitor_get_watermark(lchip, &wm);
    if (ret < 0)
    {
        PUMP_LOG(CTC_PUMP_LOG_ERROR, "ret=%d!", ret);
        return -1;
    }
    watermark->nanoseconds = wm.u.latency.max_latency;
    /*Masks*/
    watermark->masks |= CTC_PUMP_LAT_NS;

    ret = ctcs_monitor_clear_watermark(lchip, &wm);
    if (ret < 0)
    {
        PUMP_LOG(CTC_PUMP_LOG_ERROR, "ret=%d!", ret);
        return -1;
    }
    return 0;
}

#define ______PUMP_API______

void
ctc_pump_sdk_query_buffer_watermark(uint8 lchip)
{
    uint8 gchip = 0;
    uint32 gport = 0;
    int32 ret = 0;
    uint8 dir = 0;
    uint32 idx = 0;
    uint32 max_port_num = 0;
    uint32 network_port_num = 0;
    bool mac_en = FALSE;
    uint32 watermark_size = 0;
    uint8 pool = 0;
    ctc_pump_buffer_t* watermark = NULL;
    ctc_pump_func_data_t push_data;

    max_port_num = ctc_pump_sdk_get_max_port_num(lchip);
    network_port_num = ctc_pump_sdk_get_network_port_num(lchip);

    watermark_size = 2 * (network_port_num + 5) * sizeof(ctc_pump_buffer_t); /*Ingress + Egress: total + sc(0-3) + port(max_port_num)*/
    watermark = (ctc_pump_buffer_t*)sal_malloc(watermark_size);
    if (NULL == watermark)
    {
        PUMP_LOG(CTC_PUMP_LOG_ERROR, "No Memory!");
        return;
    }
    sal_memset(watermark, 0, watermark_size);
    sal_memset(&push_data, 0, sizeof(push_data));
    ctcs_get_gchip_id(lchip, &gchip);
    push_data.type = CTC_PUMP_FUNC_QUERY_BUFFER_WATERMARK;
    push_data.data = watermark;

    for (pool = 0; pool < 4; pool++)
    {
        watermark[push_data.count].type = CTC_PUMP_BUFFER_POOL;
        watermark[push_data.count].pool = pool;
        watermark[push_data.count].masks = CTC_PUMP_BUF_TYPE | CTC_PUMP_BUF_DIR | CTC_PUMP_BUF_POOL;
        watermark[push_data.count].dir = CTC_PUMP_DIR_EGS;
        ret = _ctc_pump_sdk_query_buffer_watermark(lchip, &watermark[push_data.count]);
        if (ret)
        {
            continue;
        }
        push_data.count++;
    }

    for (dir = CTC_PUMP_DIR_IGS; dir <= CTC_PUMP_DIR_EGS; dir++)
    {
        watermark[push_data.count].type = CTC_PUMP_BUFFER_TOTAL;
        watermark[push_data.count].masks = CTC_PUMP_BUF_TYPE | CTC_PUMP_BUF_DIR;
        watermark[push_data.count].dir = dir;
        ret = _ctc_pump_sdk_query_buffer_watermark(lchip, &watermark[push_data.count]);
        if (ret)
        {
            PUMP_LOG(CTC_PUMP_LOG_ERROR, "dir=%d ret=%d", dir,  ret);
        }
        else
        {
            push_data.count++;
        }

        for (idx = 0; idx < max_port_num; idx++)
        {
            gport = CTC_MAP_LPORT_TO_GPORT(gchip, idx);
            ret = ctcs_port_get_mac_en(lchip, gport, &mac_en);
            if (ret < 0)
            {
                continue;
            }
            if (FALSE == mac_en)
            {
                continue;
            }
            watermark[push_data.count].type = CTC_PUMP_BUFFER_PORT;
            watermark[push_data.count].port = gport;
            watermark[push_data.count].masks = CTC_PUMP_BUF_TYPE | CTC_PUMP_BUF_PORT | CTC_PUMP_BUF_DIR;
            watermark[push_data.count].dir = dir;
            ret = _ctc_pump_sdk_query_buffer_watermark(lchip, &watermark[push_data.count]);
            if (ret)
            {
                continue;
            }
            push_data.count++;
            if (push_data.count >= network_port_num)
            {
                continue;
            }
        }
    }
    sal_time(&push_data.timestamp);
    push_data.switch_id = gchip;
    ctc_pump_sdk_push_data(lchip, &push_data);
    sal_free(watermark);
    return;
}

void
ctc_pump_sdk_query_latency_watermark(uint8 lchip)
{
    uint8 gchip = 0;
    uint32 gport = 0;
    int32 ret = 0;
    uint32 idx = 0;
    uint32 max_port_num = 0;
    uint32 network_port_num = 0;
    bool mac_en = FALSE;
    ctc_pump_latency_t* watermark = NULL;
    ctc_pump_func_data_t push_data;

    network_port_num = ctc_pump_sdk_get_network_port_num(lchip);
    max_port_num = ctc_pump_sdk_get_max_port_num(lchip);
    watermark = (ctc_pump_latency_t*)sal_malloc(network_port_num * sizeof(ctc_pump_latency_t));
    if (NULL == watermark)
    {
        PUMP_LOG(CTC_PUMP_LOG_ERROR, "No Memory!");
        return;
    }
    sal_memset(watermark, 0, network_port_num * sizeof(ctc_pump_latency_t));
    sal_memset(&push_data, 0, sizeof(push_data));
    ctcs_get_gchip_id(lchip, &gchip);

    push_data.type = CTC_PUMP_FUNC_QUERY_LATENCY_WATERMARK;
    push_data.data = watermark;

    for (idx = 0; idx < max_port_num; idx++)
    {
        gport = CTC_MAP_LPORT_TO_GPORT(gchip, idx);
        ret = ctcs_port_get_mac_en(lchip, gport, &mac_en);
        if (ret < 0)
        {
            continue;
        }
        if (FALSE == mac_en)
        {
            continue;
        }
        watermark[push_data.count].port = gport;
        watermark[push_data.count].masks = CTC_PUMP_LAT_PORT;
        ret = _ctc_pump_sdk_query_latency_watermark(lchip, &watermark[push_data.count]);
        if (ret)
        {
            continue;
        }
        push_data.count++;
        if (push_data.count >= network_port_num)
        {
            break;
        }
    }
    sal_time(&push_data.timestamp);
    push_data.switch_id = gchip;
    ctc_pump_sdk_push_data(lchip, &push_data);
    sal_free(watermark);
    return;
}

void
ctc_pump_sdk_query_mac_stats(uint8 lchip)
{
    uint8 gchip = 0;
    uint32 gport = 0;
    int32 ret = 0;
    uint32 idx = 0;
    uint32 max_port_num = 0;
    uint32 network_port_num = 0;
    bool mac_en = FALSE;
    ctc_pump_mac_stats_t* stats = NULL;
    ctc_pump_func_data_t push_data;

    network_port_num = ctc_pump_sdk_get_network_port_num(lchip);
    max_port_num = ctc_pump_sdk_get_max_port_num(lchip);
    stats = (ctc_pump_mac_stats_t*)sal_malloc(network_port_num * sizeof(ctc_pump_mac_stats_t));
    if (NULL == stats)
    {
        PUMP_LOG(CTC_PUMP_LOG_ERROR, "No Memory!");
        return;
    }
    sal_memset(stats, 0, network_port_num * sizeof(ctc_pump_mac_stats_t));
    sal_memset(&push_data, 0, sizeof(push_data));
    ctcs_get_gchip_id(lchip, &gchip);


    for (idx = 0; idx < max_port_num; idx++)
    {
        gport = CTC_MAP_LPORT_TO_GPORT(gchip, idx);
        ret = ctcs_port_get_mac_en(lchip, gport, &mac_en);
        if (ret < 0)
        {
            continue;
        }
        if (FALSE == mac_en)
        {
            continue;
        }
        stats[push_data.count].port = gport;
        ret = _ctc_pump_sdk_query_mac_stats(lchip, &stats[push_data.count]);
        if (ret)
        {
            continue;
        }
        push_data.count++;
        if (push_data.count >= network_port_num)
        {
            break;
        }
    }
    push_data.type = CTC_PUMP_FUNC_QUERY_MAC_STATS;
    push_data.data = stats;
    sal_time(&push_data.timestamp);
    push_data.switch_id = gchip;
    ctc_pump_sdk_push_data(lchip, &push_data);

    sal_free(stats);
    return;
}

void
ctc_pump_sdk_query_mburst_stats(uint8 lchip)
{
    uint8 gchip = 0;
    uint32 gport = 0;
    int32 ret = 0;
    uint32 idx = 0;
    uint32 max_port_num = 0;
    uint32 network_port_num = 0;
    bool mac_en = FALSE;
    ctc_pump_mburst_t* stats = NULL;
    ctc_pump_func_data_t push_data;

    network_port_num = ctc_pump_sdk_get_network_port_num(lchip);
    max_port_num = ctc_pump_sdk_get_max_port_num(lchip);
    stats = (ctc_pump_mburst_t*)sal_malloc(network_port_num * sizeof(ctc_pump_mburst_t));
    if (NULL == stats)
    {
        PUMP_LOG(CTC_PUMP_LOG_ERROR, "No Memory!");
        return;
    }
    sal_memset(stats, 0, network_port_num * sizeof(ctc_pump_mburst_t));
    sal_memset(&push_data, 0, sizeof(push_data));
    ctcs_get_gchip_id(lchip, &gchip);

    push_data.type = CTC_PUMP_FUNC_QUERY_MBURST_STATS;
    push_data.data = stats;

    for (idx = 0; idx < max_port_num; idx++)
    {
        gport = CTC_MAP_LPORT_TO_GPORT(gchip, idx);
        ret = ctcs_port_get_mac_en(lchip, gport, &mac_en);
        if (ret < 0)
        {
            continue;
        }
        if (FALSE == mac_en)
        {
            continue;
        }
        stats[push_data.count].port = gport;
        ret = _ctc_pump_sdk_query_mburst_stats(lchip, &stats[push_data.count]);
        if (ret)
        {
            continue;
        }
        push_data.count++;
        if (push_data.count >= network_port_num)
        {
            break;
        }
    }

    sal_time(&push_data.timestamp);
    push_data.switch_id = gchip;
    ctc_pump_sdk_push_data(lchip, &push_data);

    sal_free(stats);
    return;
}

void
ctc_pump_sdk_query_buffer_stats(uint8 lchip)
{
    uint8 gchip = 0;
    uint32 gport = 0;
    int32 ret = 0;
    uint32 idx = 0;
    uint8 sub_q = 0;
    uint32 network_port_num = 0;
    uint32 max_port_num = 0;
    bool mac_en = FALSE;
    uint32 data_size = 0;
    ctc_pump_buffer_t* stats = NULL;
    ctc_qos_resrc_pool_stats_t sdk_stats;
    ctc_pump_func_data_t push_data;

    max_port_num = ctc_pump_sdk_get_max_port_num(lchip);
    network_port_num = ctc_pump_sdk_get_network_port_num(lchip);
    data_size = 9 * network_port_num + 5;
    stats = (ctc_pump_buffer_t*)sal_malloc(data_size * sizeof(ctc_pump_buffer_t));
    if (NULL == stats)
    {
        PUMP_LOG(CTC_PUMP_LOG_ERROR, "No Memory!");
        return;
    }
    sal_memset(stats, 0, data_size * sizeof(ctc_pump_buffer_t));
    sal_memset(&push_data, 0, sizeof(push_data));
    sal_memset(&sdk_stats, 0, sizeof(sdk_stats));
    ctcs_get_gchip_id(lchip, &gchip);

    push_data.type = CTC_PUMP_FUNC_REPORT_BUFFER_STATS;
    push_data.data = stats;

    sdk_stats.type = CTC_QOS_RESRC_STATS_EGS_TOTAL_COUNT;
    /*total*/
    ret = ctcs_qos_query_pool_stats(lchip, &sdk_stats);
    if (ret == 0)
    {
        stats[push_data.count].dir = CTC_PUMP_DIR_EGS;
        stats[push_data.count].total_bytes = sdk_stats.count * BUFFER_CELL_UNITS;
        stats[push_data.count].type = CTC_PUMP_BUFFER_TOTAL;
        stats[push_data.count].masks = CTC_PUMP_BUF_TYPE | CTC_PUMP_BUF_DIR | CTC_PUMP_BUF_TOTAL_BYTES;
        push_data.count++;
    }
    /*Pool*/
    sdk_stats.type = CTC_QOS_RESRC_STATS_EGS_POOL_COUNT;
    for (idx = 0; idx < 4; idx++)
    {
        sdk_stats.pool = idx;
        ret = ctcs_qos_query_pool_stats(lchip, &sdk_stats);
        if (ret < 0)
        {
            continue;
        }
        stats[push_data.count].dir = CTC_PUMP_DIR_EGS;
        stats[push_data.count].pool = idx;
        stats[push_data.count].total_bytes = sdk_stats.count * BUFFER_CELL_UNITS;
        stats[push_data.count].type = CTC_PUMP_BUFFER_POOL;
        stats[push_data.count].masks = CTC_PUMP_BUF_TYPE | CTC_PUMP_BUF_DIR | CTC_PUMP_BUF_POOL | CTC_PUMP_BUF_TOTAL_BYTES;
        push_data.count++;
    }

    for (idx = 0; idx < max_port_num; idx++)
    {
        gport = CTC_MAP_LPORT_TO_GPORT(gchip, idx);
        ret = ctcs_port_get_mac_en(lchip, gport, &mac_en);
        if (ret < 0)
        {
            continue;
        }
        if (FALSE == mac_en)
        {
            continue;
        }

        /*Port*/
        sdk_stats.type = CTC_QOS_RESRC_STATS_EGS_PORT_COUNT;
        sdk_stats.gport = gport;
        ret = ctcs_qos_query_pool_stats(lchip, &sdk_stats);
        if (ret == 0)
        {
            stats[push_data.count].port = gport;
            stats[push_data.count].dir = CTC_PUMP_DIR_EGS;
            stats[push_data.count].total_bytes = sdk_stats.count * BUFFER_CELL_UNITS;
            stats[push_data.count].type = CTC_PUMP_BUFFER_PORT;
            stats[push_data.count].masks = CTC_PUMP_BUF_TYPE | CTC_PUMP_BUF_DIR | CTC_PUMP_BUF_PORT | CTC_PUMP_BUF_TOTAL_BYTES;
            push_data.count++;
        }

        /*Queue*/
        sdk_stats.type = CTC_QOS_RESRC_STATS_QUEUE_COUNT;
        sdk_stats.gport = gport;
        sdk_stats.queue.gport = gport;
        sdk_stats.queue.queue_type = CTC_QUEUE_TYPE_NETWORK_EGRESS;
        for (sub_q = 0; sub_q < 8; sub_q++)
        {
            sdk_stats.queue.queue_id = sub_q;
            ret = ctcs_qos_query_pool_stats(lchip, &sdk_stats);
            if (ret)
            {
                continue;
            }
            stats[push_data.count].port = gport;
            stats[push_data.count].queue = idx * 8 + sub_q;
            stats[push_data.count].sub_queue = sub_q;
            stats[push_data.count].dir = CTC_PUMP_DIR_EGS;
            stats[push_data.count].total_bytes = sdk_stats.count * BUFFER_CELL_UNITS;
            stats[push_data.count].type = CTC_PUMP_BUFFER_QUEUE;
            stats[push_data.count].masks = CTC_PUMP_BUF_TYPE | CTC_PUMP_BUF_DIR | CTC_PUMP_BUF_PORT | CTC_PUMP_BUF_QUEUE | CTC_PUMP_BUF_SUB_QUEUE | CTC_PUMP_BUF_TOTAL_BYTES;
            push_data.count++;
            if (push_data.count >= data_size)
            {
                continue;
            }
        }
    }

    sal_time(&push_data.timestamp);
    push_data.switch_id = gchip;
    ctc_pump_sdk_push_data(lchip, &push_data);
    sal_free(stats);
    return;
}

void
ctc_pump_sdk_query_latency_stats(uint8 lchip)
{
    uint8 gchip = 0;
    uint32 gport = 0;
    int32 ret = 0;
    uint32 idx = 0;
    uint8 level = 0;
    uint32 network_port_num = 0;
    uint32 max_port_num = 0;
    bool mac_en = FALSE;
    ctc_pump_latency_t* stats = NULL;
    ctc_pump_func_data_t push_data;
    ctc_monitor_config_t mon_cfg;

    max_port_num = ctc_pump_sdk_get_max_port_num(lchip);
    network_port_num = ctc_pump_sdk_get_network_port_num(lchip);
    stats = (ctc_pump_latency_t*)sal_malloc(network_port_num * sizeof(ctc_pump_latency_t));
    if (NULL == stats)
    {
        PUMP_LOG(CTC_PUMP_LOG_ERROR, "No Memory!");
        return;
    }
    sal_memset(&mon_cfg, 0, sizeof(mon_cfg));
    sal_memset(stats, 0,  network_port_num * sizeof(ctc_pump_latency_t));
    sal_memset(&push_data, 0, sizeof(push_data));
    ctcs_get_gchip_id(lchip, &gchip);

    push_data.type = CTC_PUMP_FUNC_REPORT_LATENCY_STATS;
    push_data.data = stats;

    mon_cfg.cfg_type = CTC_MONITOR_RETRIEVE_LATENCY_STATS;
    mon_cfg.monitor_type = CTC_MONITOR_LATENCY;

    for (idx = 0; idx < max_port_num; idx++)
    {
        gport = CTC_MAP_LPORT_TO_GPORT(gchip, idx);
        ret = ctcs_port_get_mac_en(lchip, gport, &mac_en);
        if (ret < 0)
        {
            continue;
        }
        if (FALSE == mac_en)
        {
            continue;
        }
        mon_cfg.gport = gport;
        for (level = 0; level < CTC_PUMP_LATENCY_LEVEL; level++)
        {
            mon_cfg.level = level;
            ret = ctcs_monitor_get_config(lchip, &mon_cfg);
            if (ret < 0)
            {
                continue;
            }
            stats[push_data.count].count[level] = mon_cfg.value;
        }
        stats[push_data.count].port = gport;
        ctc_pump_sdk_get_latency_thrd(lchip, stats[push_data.count].thrd);
        stats[push_data.count].masks = CTC_PUMP_LAT_PORT | CTC_PUMP_LAT_COUNT | CTC_PUMP_LAT_THRD;
        push_data.count++;
        if (push_data.count >= network_port_num)
        {
            continue;
        }
    }
    sal_time(&push_data.timestamp);
    push_data.switch_id = gchip;
    ctc_pump_sdk_push_data(lchip, &push_data);
    sal_free(stats);
    return;
}

void
ctc_pump_sdk_query_temperature(uint8 lchip)
{
    int32 ret = 0;
    uint32 temp = 0;
    ctc_pump_func_data_t push_data;

    sal_memset(&push_data, 0, sizeof(push_data));
    push_data.type = CTC_PUMP_FUNC_QUERY_TEMPERATURE;
    ret = _ctc_pump_sdk_query_temperature(lchip, &temp);
    if (ret)
    {
        return;
    }
    sal_time(&push_data.timestamp);
    push_data.count = 1;
    push_data.data = &temp;
    ctc_pump_sdk_push_data(lchip, &push_data);

    return;
}

void
ctc_pump_sdk_query_voltage(uint8 lchip)
{
    int32 ret = 0;
    uint8 gchip = 0;
    uint32 voltage = 0;
    ctc_pump_func_data_t push_data;

    ctcs_get_gchip_id(lchip, &gchip);
    sal_memset(&push_data, 0, sizeof(push_data));
    push_data.type = CTC_PUMP_FUNC_QUERY_VOLTAGE;
    ret = _ctc_pump_sdk_query_voltage(lchip, &voltage);
    if (ret)
    {
        return;
    }
    sal_time(&push_data.timestamp);
    push_data.count = 1;
    push_data.data = &voltage;
    push_data.switch_id = gchip;
    ctc_pump_sdk_push_data(lchip, &push_data);

    return;
}

ctc_pump_func
ctc_pump_get_query_func(ctc_pump_func_type_t func_type)
{
    ctc_pump_func func = NULL;
    if (func_type >= CTC_PUMP_FUNC_MAX)
    {
        PUMP_LOG(CTC_PUMP_LOG_ERROR, "func_type=%d Exceed Max Value!", func_type);
        return NULL;
    }

    switch (func_type)
    {
        case CTC_PUMP_FUNC_QUERY_TEMPERATURE:
            func = ctc_pump_sdk_query_temperature;
            break;
        case CTC_PUMP_FUNC_QUERY_VOLTAGE:
            func = ctc_pump_sdk_query_voltage;
            break;
        case CTC_PUMP_FUNC_QUERY_MAC_STATS:
            func = ctc_pump_sdk_query_mac_stats;
            break;
        case CTC_PUMP_FUNC_QUERY_MBURST_STATS:
            func = ctc_pump_sdk_query_mburst_stats;
            break;
        case CTC_PUMP_FUNC_QUERY_BUFFER_WATERMARK:
            func = ctc_pump_sdk_query_buffer_watermark;
            break;
        case CTC_PUMP_FUNC_QUERY_LATENCY_WATERMARK:
            func = ctc_pump_sdk_query_latency_watermark;
            break;
        case CTC_PUMP_FUNC_REPORT_BUFFER_STATS:
            func = ctc_pump_sdk_query_buffer_stats;
            break;
        case CTC_PUMP_FUNC_REPORT_LATENCY_STATS:
            func = ctc_pump_sdk_query_latency_stats;
            break;
        default:
            break;
    }
    return func;
}

int32
ctc_pump_feature_init(uint8 lchip)
{
    uint32 idx = 0;
    int32 ret = 0;
    uint8 gchip = 0;
    uint32 max_port_num = 0;
    ctc_diag_drop_pkt_config_t drop_cfg;
    ctc_monitor_config_t monitor_cfg;
    ctc_pkt_rx_register_t rx_register;

    ctcs_get_gchip_id(lchip, &gchip);
    max_port_num = ctc_pump_sdk_get_max_port_num(lchip);

    sal_memset(&drop_cfg, 0, sizeof(drop_cfg));
    if (ctc_pump_func_is_enable(lchip, CTC_PUMP_FUNC_REPORT_DROP_INFO))
    {
        ctc_qos_glb_cfg_t qos_cfg;
        ctcs_diag_init(lchip, NULL);
        /*drop info report cb*/
        drop_cfg.flags = CTC_DIAG_DROP_PKT_REPORT_CALLBACK | CTC_DIAG_DROP_PKT_STORE_EN;
        drop_cfg.report_cb = ctc_pump_sdk_drop_info_cb;
        drop_cfg.storage_en = 1;
        ret = ctcs_diag_set_property(lchip, CTC_DIAG_PROP_DROP_PKT_CONFIG, &drop_cfg);
        if (ret < 0)
        {
            PUMP_LOG(CTC_PUMP_LOG_ERROR, "ret=%d!", ret);
        }
        sal_memset(&rx_register, 0, sizeof(rx_register));
        rx_register.cb = ctc_pump_sdk_pkt_report_cb;
        CTC_BMP_SET(rx_register.reason_bmp, CTC_PKT_CPU_REASON_QUEUE_DROP_PKT);
        ret = ctcs_packet_rx_register(lchip, &rx_register);
        if (ret < 0)
        {
            PUMP_LOG(CTC_PUMP_LOG_ERROR, "queue drop callback register error, ret=%d!", ret);
        }

        sal_memset(&qos_cfg, 0, sizeof(qos_cfg));
        qos_cfg.cfg_type = CTC_QOS_GLB_CFG_QUEUE_DROP_MONITOR_EN;
        qos_cfg.u.drop_monitor.enable = 1;
        qos_cfg.u.drop_monitor.dst_gport = 0x10000000;
        for (idx = 0; idx < max_port_num; idx++)
        {
            qos_cfg.u.drop_monitor.src_gport = CTC_MAP_LPORT_TO_GPORT(gchip, idx);
            ctcs_qos_set_global_config(lchip, &qos_cfg);
        }
    }
    /* monitor cb */
    ctcs_monitor_register_cb(lchip, ctc_pump_sdk_monitor_cb, NULL);

    sal_memset(&monitor_cfg, 0, sizeof(monitor_cfg));

    /* Buffer Inform Enable */
    monitor_cfg.cfg_type = CTC_MONITOR_CONFIG_MON_INFORM_EN;
    if (ctc_pump_func_is_enable(lchip, CTC_PUMP_FUNC_REPORT_BUFFER_EVENT)
        || ctc_pump_func_is_enable(lchip, CTC_PUMP_FUNC_REPORT_MBURST_EVENT)
        || ctc_pump_func_is_enable(lchip, CTC_PUMP_FUNC_QUERY_MBURST_STATS))
    {
        monitor_cfg.monitor_type = CTC_MONITOR_BUFFER;
        monitor_cfg.value = 1;
        ret = ctcs_monitor_set_config(lchip, &monitor_cfg);
        if (ret < 0)
        {
            PUMP_LOG(CTC_PUMP_LOG_ERROR, "ret=%d!", ret);
        }
    }


    /* latency Inform Enable */

    if (ctc_pump_func_is_enable(lchip, CTC_PUMP_FUNC_REPORT_LATENCY_EVENT))
    {
        for (idx = 0; idx < max_port_num; idx++)
        {
            monitor_cfg.monitor_type = CTC_MONITOR_LATENCY;
            monitor_cfg.value = 1;
            monitor_cfg.buffer_type = CTC_MONITOR_BUFFER_PORT;
            monitor_cfg.gport = CTC_MAP_LPORT_TO_GPORT(gchip, idx);
            monitor_cfg.cfg_type = CTC_MONITOR_CONFIG_MON_INFORM_EN;
            ret = ctcs_monitor_set_config(lchip, &monitor_cfg);
            if (ret < 0)
            {
            }
            /*Mburst Threshold*/
            monitor_cfg.cfg_type = CTC_MONITOR_CONFIG_MON_INFORM_MAX;
            monitor_cfg.value = 500;
            ret = ctcs_monitor_set_config(lchip, &monitor_cfg);
            if (ret < 0)
            {
            }
            /*Mburst Threshold*/
            monitor_cfg.cfg_type = CTC_MONITOR_CONFIG_MON_INFORM_MIN;
            monitor_cfg.value = 100;
            ret = ctcs_monitor_set_config(lchip, &monitor_cfg);
            if (ret < 0)
            {
            }
        }
    }
    sal_memset(&monitor_cfg, 0, sizeof(monitor_cfg));

    if (ctc_pump_func_is_enable(lchip, CTC_PUMP_FUNC_QUERY_LATENCY_WATERMARK))
    {
        ctc_monitor_glb_cfg_t glb_cfg;
        sal_memset(&glb_cfg, 0, sizeof(glb_cfg));
        glb_cfg.cfg_type = CTC_MONITOR_GLB_CONFIG_LATENCY_THRD;
        glb_cfg.u.thrd.level = 7;
        glb_cfg.u.thrd.threshold = 1000000;
        ctcs_monitor_set_global_config(lchip, &glb_cfg);
        glb_cfg.u.thrd.level = 6;
        glb_cfg.u.thrd.threshold = 50000;
        ctcs_monitor_set_global_config(lchip, &glb_cfg);
        glb_cfg.u.thrd.level = 5;
        glb_cfg.u.thrd.threshold = 10000;
        ctcs_monitor_set_global_config(lchip, &glb_cfg);
        glb_cfg.u.thrd.level = 4;
        glb_cfg.u.thrd.threshold = 5000;
        ctcs_monitor_set_global_config(lchip, &glb_cfg);
    }

    if (ctc_pump_func_is_enable(lchip, CTC_PUMP_FUNC_REPORT_BUFFER_STATS)
        || ctc_pump_func_is_enable(lchip, CTC_PUMP_FUNC_REPORT_MBURST_EVENT)
        || ctc_pump_func_is_enable(lchip, CTC_PUMP_FUNC_QUERY_MBURST_STATS))
    {
        monitor_cfg.dir = CTC_EGRESS;
        monitor_cfg.cfg_type = CTC_MONITOR_CONFIG_MON_INTERVAL;
        monitor_cfg.monitor_type = CTC_MONITOR_BUFFER;
        monitor_cfg.value = 100;
        ret = ctcs_monitor_set_config(lchip, &monitor_cfg);
        if (ret < 0)
        {
            PUMP_LOG(CTC_PUMP_LOG_ERROR, "ret=%d!", ret);
            monitor_cfg.value = 1000;
            ctcs_monitor_set_config(lchip, &monitor_cfg);
        }

        monitor_cfg.cfg_type = CTC_MONITOR_CONFIG_MON_SCAN_EN;
        monitor_cfg.monitor_type = CTC_MONITOR_BUFFER;
        monitor_cfg.buffer_type = CTC_MONITOR_BUFFER_SC;
        monitor_cfg.value = 1;
        ret = ctcs_monitor_set_config(lchip, &monitor_cfg);
        if (ret < 0)
        {
        }

        monitor_cfg.buffer_type = CTC_MONITOR_BUFFER_TOTAL;
        monitor_cfg.value = 1;
        ret = ctcs_monitor_set_config(lchip, &monitor_cfg);
        if (ret < 0)
        {
        }

        for (idx = 0; idx < max_port_num; idx++)
        {
            /*buffer scan*/
            monitor_cfg.cfg_type = CTC_MONITOR_CONFIG_MON_SCAN_EN;
            monitor_cfg.monitor_type = CTC_MONITOR_BUFFER;
            monitor_cfg.buffer_type = CTC_MONITOR_BUFFER_PORT;
            monitor_cfg.value = 1;
            monitor_cfg.gport = CTC_MAP_LPORT_TO_GPORT(gchip, idx);
            ret = ctcs_monitor_set_config(lchip, &monitor_cfg);
            if (ret < 0)
            {
            }
            /*Mburst Threshold*/
            monitor_cfg.cfg_type = CTC_MONITOR_CONFIG_MON_INFORM_MAX;
            monitor_cfg.monitor_type = CTC_MONITOR_BUFFER;
            monitor_cfg.buffer_type = CTC_MONITOR_BUFFER_PORT;
            monitor_cfg.value = MONITOR_BURST_THRD; /* busrt threshold */
            monitor_cfg.gport = CTC_MAP_LPORT_TO_GPORT(gchip, idx);
            ret = ctcs_monitor_set_config(lchip, &monitor_cfg);
            if (ret < 0)
            {
            }
            /*Mburst Threshold*/
            monitor_cfg.cfg_type = CTC_MONITOR_CONFIG_MON_INFORM_MIN;
            monitor_cfg.value = 10; /* busrt threshold */
            ret = ctcs_monitor_set_config(lchip, &monitor_cfg);
            if (ret < 0)
            {
            }
        }
    }

    if (ctc_pump_func_is_enable(lchip, CTC_PUMP_FUNC_REPORT_LATENCY_STATS))
    {
        monitor_cfg.cfg_type = CTC_MONITOR_CONFIG_MON_INTERVAL;
        monitor_cfg.monitor_type = CTC_MONITOR_LATENCY;
        monitor_cfg.value = 100;
        ret = ctcs_monitor_set_config(lchip, &monitor_cfg);
        if (ret < 0)
        {
            PUMP_LOG(CTC_PUMP_LOG_ERROR, "ret=%d!", ret);
            monitor_cfg.value = 1000;
            ctcs_monitor_set_config(lchip, &monitor_cfg);
        }
        monitor_cfg.cfg_type = CTC_MONITOR_CONFIG_MON_SCAN_EN;
        monitor_cfg.monitor_type = CTC_MONITOR_LATENCY;
        monitor_cfg.buffer_type = CTC_MONITOR_BUFFER_PORT;
        monitor_cfg.value = 1;
        for (idx = 0; idx < max_port_num; idx++)
        {
            monitor_cfg.gport = CTC_MAP_LPORT_TO_GPORT(gchip, idx);
            ret = ctcs_monitor_set_config(lchip, &monitor_cfg);
            if (ret < 0)
            {
            }
        }
    }
    if (ctc_pump_func_is_enable(lchip, CTC_PUMP_FUNC_REPORT_EFD))
    {
        bool value = TRUE;
        sal_memset(&rx_register, 0, sizeof(rx_register));
        rx_register.cb = ctc_pump_sdk_pkt_report_cb;
        CTC_BMP_SET(rx_register.reason_bmp, CTC_PKT_CPU_REASON_NEW_ELEPHANT_FLOW);

        ret = ctcs_packet_rx_register(lchip, &rx_register);
        if (ret < 0)
        {
            PUMP_LOG(CTC_PUMP_LOG_ERROR, "efd callback register error, ret=%d!", ret);
        }

        ret = ctcs_efd_set_global_ctl(lchip, CTC_EFD_GLOBAL_EFD_EN, &value);
        if (ret < 0)
        {
            PUMP_LOG(CTC_PUMP_LOG_ERROR, "efd enable error, ret=%d!", ret);
        }
        for (idx = 0; idx < max_port_num; idx++)
        {
            ret = ctcs_port_set_property(lchip, CTC_MAP_LPORT_TO_GPORT(gchip, idx), CTC_PORT_PROP_EFD_EN, 1);
            if (ret < 0)
            {
            }
        }
    }

    sal_time(&g_pump_monitor_time);

    return 0;
}


int32
ctc_pump_feature_deinit(uint8 lchip)
{
    int32 ret = 0;
    bool value = FALSE;
    ctc_pkt_rx_register_t rx_register;
    ctc_diag_drop_pkt_config_t drop_cfg;
    ctc_qos_glb_cfg_t qos_cfg;
    uint32 idx = 0;
    uint8 gchip = 0;
    uint32 max_port_num = 0;

    ctcs_get_gchip_id(lchip, &gchip);
    max_port_num = ctc_pump_sdk_get_max_port_num(lchip);

    g_pump_monitor_time = 0;
    g_pump_monitor_time_base = 0;

    /*drop info report cb*/
    sal_memset(&drop_cfg, 0, sizeof(drop_cfg));
    ret = ctcs_diag_get_property(lchip, CTC_DIAG_PROP_DROP_PKT_CONFIG, &drop_cfg);
    if (ret < 0)
    {
        PUMP_LOG(CTC_PUMP_LOG_ERROR, "ret=%d!", ret);
    }
    if ((drop_cfg.flags & CTC_DIAG_DROP_PKT_REPORT_CALLBACK)
        && (drop_cfg.report_cb == ctc_pump_sdk_drop_info_cb))
    {
        drop_cfg.flags = CTC_DIAG_DROP_PKT_REPORT_CALLBACK;
        drop_cfg.report_cb = NULL;
        ret = ctcs_diag_set_property(lchip, CTC_DIAG_PROP_DROP_PKT_CONFIG, &drop_cfg);
        if (ret < 0)
        {
            PUMP_LOG(CTC_PUMP_LOG_ERROR, "ret=%d!", ret);
        }
    }

    sal_memset(&qos_cfg, 0, sizeof(qos_cfg));
    qos_cfg.cfg_type = CTC_QOS_GLB_CFG_QUEUE_DROP_MONITOR_EN;
    qos_cfg.u.drop_monitor.enable = 0;
    qos_cfg.u.drop_monitor.dst_gport = 0x10000000;
    for (idx = 0; idx < max_port_num; idx++)
    {
        qos_cfg.u.drop_monitor.src_gport = CTC_MAP_LPORT_TO_GPORT(gchip, idx);
        ctcs_qos_set_global_config(lchip, &qos_cfg);
    }

    sal_memset(&rx_register, 0, sizeof(rx_register));
    rx_register.cb = ctc_pump_sdk_pkt_report_cb;
    CTC_BMP_SET(rx_register.reason_bmp, CTC_PKT_CPU_REASON_NEW_ELEPHANT_FLOW);
    CTC_BMP_SET(rx_register.reason_bmp, CTC_PKT_CPU_REASON_QUEUE_DROP_PKT);

    ret = ctcs_packet_rx_unregister(lchip, &rx_register);
    if (ret < 0)
    {
        PUMP_LOG(CTC_PUMP_LOG_ERROR, "efd callback unregister error, ret=%d!", ret);
    }

    ret = ctcs_efd_set_global_ctl(lchip, CTC_EFD_GLOBAL_EFD_EN, &value);
    if (ret < 0)
    {
        PUMP_LOG(CTC_PUMP_LOG_ERROR, "efd disable error, ret=%d!", ret);
    }

    /* monitor cb */
    ctcs_monitor_register_cb(lchip, NULL, NULL);

    return 0;
}


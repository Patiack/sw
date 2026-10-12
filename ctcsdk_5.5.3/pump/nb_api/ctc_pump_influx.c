#include "ctc_pump.h"
#include "ctc_influxc.h"
#include "ctc_pump_db.h"


#define NANOSECONDS_CONVERSION 1000000000L
#define INFLUX_MEASUREMENT_MEMSIZE  256


int32  g_pump_time_adjust = 0;

/* UTC */
#define TIMESTAMP_UTC(T) ((T + (g_pump_time_adjust * 3600)) * 1000000000L)

ctc_pump_db_cfg_t g_influxc;

int32
ctc_pump_influx_insert_data(char* measurement)
{
    int ret = 0;
    ctc_influxc_client_t* client= NULL;
    if (!measurement)
    {
        return -1;
    }
    client = ctc_influxc_client_new(g_influxc.host, g_influxc.user, g_influxc.passwd, g_influxc.db, g_influxc.ssl);
    ret = ctc_influxc_insert_measurement(client, measurement);
    if (ret >= 300)
    {
        PUMP_LOG(CTC_PUMP_LOG_ERROR, "ret=%d!", ret);
    }
    ctc_influxc_client_free(client);
    return 0;
}

#define POST_MAC_STATS(V, STR) \
do{\
    if (V)\
    {\
    sal_snprintf(temp, 64, ",%s=%"PRIu64, STR, V);\
    sal_strcat(measurement, temp);\
    }\
}while(0)\

int32
ctc_pump_influx_post_template(uint32 type,
                                        uint32  switch_id,
                                        sal_time_t  tm,
                                        void*   p_val,
                                        uint32  p_val_ofst,
                                        char*   measurement)
{
    char* str_name = "null";
    char* post_str = NULL;
    char temp[64] = {0};
    uint64 timestamp = (uint64)tm;
    if (!measurement || !p_val)
    {
        PUMP_LOG(CTC_PUMP_LOG_ERROR, "NULL PTR!");
        return -1;
    }

    switch (type)
    {
        case CTC_PUMP_FUNC_QUERY_TEMPERATURE:
            sal_snprintf(measurement, INFLUX_MEASUREMENT_MEMSIZE,
                "temperature,switchId=%u temperature=%u %"PRIu64,
                switch_id, ((uint32*)p_val)[p_val_ofst], TIMESTAMP_UTC(timestamp));
            break;
        case CTC_PUMP_FUNC_QUERY_VOLTAGE:
            sal_snprintf(measurement, INFLUX_MEASUREMENT_MEMSIZE,
                "voltage,switchId=%u voltage=%u %"PRIu64,
                switch_id, ((uint32*)p_val)[p_val_ofst], TIMESTAMP_UTC(timestamp));
            break;
        case CTC_PUMP_FUNC_QUERY_MAC_STATS:
            {
                ctc_pump_mac_stats_t* mac_stats = &((ctc_pump_mac_stats_t*)p_val)[p_val_ofst];
                sal_snprintf(measurement, INFLUX_MEASUREMENT_MEMSIZE,
                    "mac_stats,switchId=%u,port=0x%x\
                    rxPkts=%"PRIu64",rxBytes=%"PRIu64",txPkts=%"PRIu64",txBytes=%"PRIu64,
                    switch_id, mac_stats->port,
                    mac_stats->rx_pkts, mac_stats->rx_bytes, mac_stats->tx_pkts, mac_stats->tx_bytes);
                POST_MAC_STATS(mac_stats->rx_fcs_error_pkts, "rxFcsErrorPkts");
                POST_MAC_STATS(mac_stats->rx_mac_overrun_pkts, "rxMacOverrunPkts");
                POST_MAC_STATS(mac_stats->rx_bad_63_pkts, "rxBad63Pkts");
                POST_MAC_STATS(mac_stats->rx_bad_1519_pkts, "rxBad1519Pkts");
                POST_MAC_STATS(mac_stats->rx_bad_jumbo_pkts, "rxBadJumoPkts");
                POST_MAC_STATS(mac_stats->tx_pkts_63, "tx63Pkts");
                POST_MAC_STATS(mac_stats->tx_jumbo_pkts, "txJumboPkts");
                POST_MAC_STATS(mac_stats->tx_mac_underrun_pkts, "txMacOverrunPkts");
                POST_MAC_STATS(mac_stats->tx_fcs_error_pkts, "txFcsErrorPkts");
                sal_snprintf(temp, 64, " %"PRIu64, TIMESTAMP_UTC(timestamp));
                sal_strcat(measurement, temp);
            }
            break;
        case CTC_PUMP_FUNC_REPORT_LATENCY_EVENT:
        case CTC_PUMP_FUNC_QUERY_LATENCY_WATERMARK:
            {
                ctc_pump_latency_t* latency = &((ctc_pump_latency_t*)p_val)[p_val_ofst];
                post_str = (type == CTC_PUMP_FUNC_REPORT_LATENCY_EVENT)
                    ? "%s,switchId=%u port=\"0x%x\",nanoseconds=%"PRIu64" %"PRIu64
                    : "%s,switchId=%u,port=0x%x nanoseconds=%"PRIu64" %"PRIu64;

                if (type == CTC_PUMP_FUNC_REPORT_LATENCY_EVENT)
                {
                    str_name = "latency_event";
                }
                else
                {
                    str_name = "latency_watermark";
                }

                sal_snprintf(measurement, INFLUX_MEASUREMENT_MEMSIZE,
                    post_str,
                    str_name, switch_id, latency->port, latency->nanoseconds,
                    TIMESTAMP_UTC(timestamp));
            }
            break;
        case CTC_PUMP_FUNC_REPORT_BUFFER_EVENT:
        case CTC_PUMP_FUNC_QUERY_BUFFER_WATERMARK:
        case CTC_PUMP_FUNC_REPORT_BUFFER_STATS:
            {
                char* str_pool[4] = {"Default", "NoneDrop", "Span", "Control"};
                char* str_dir[2] = {"ingress", "egress"};
                ctc_pump_buffer_t* buffer = &((ctc_pump_buffer_t*)p_val)[p_val_ofst];
                uint8 dir = buffer->dir;

                if (type == CTC_PUMP_FUNC_QUERY_BUFFER_WATERMARK)
                {
                    str_name = "buffer_watermark";
                }
                else if (type == CTC_PUMP_FUNC_REPORT_BUFFER_STATS)
                {
                    str_name = "buffer_stats";
                }
                else
                {
                    str_name = "buffer_event";
                    dir = 1;
                }

                if (buffer->type == CTC_PUMP_BUFFER_TOTAL)
                {
                    post_str = (type == CTC_PUMP_FUNC_REPORT_BUFFER_EVENT)
                                ? "%s,switchId=%u type=\"total\",dir=\"%s\",total_bytes=%"PRIu64" %"PRIu64
                                : "%s,switchId=%u,type=total,dir=%s total_bytes=%"PRIu64" %"PRIu64;
                    sal_snprintf(measurement, INFLUX_MEASUREMENT_MEMSIZE,
                        post_str,
                        str_name, switch_id, str_dir[buffer->dir],
                        buffer->total_bytes,
                        TIMESTAMP_UTC(timestamp));
                }
                else if (buffer->type == CTC_PUMP_BUFFER_POOL)
                {
                    post_str = (type == CTC_PUMP_FUNC_REPORT_BUFFER_EVENT)
                                ? "%s,switchId=%u type=\"pool\",dir=\"%s\",pool=\"%s\",total_bytes=%"PRIu64" %"PRIu64
                                : "%s,switchId=%u,type=pool,dir=%s,pool=%s total_bytes=%"PRIu64" %"PRIu64;
                    sal_snprintf(measurement, INFLUX_MEASUREMENT_MEMSIZE,
                        post_str,
                        str_name, switch_id, str_dir[dir], str_pool[buffer->pool],
                        buffer->total_bytes,
                        TIMESTAMP_UTC(timestamp));
                }
                else if (buffer->type == CTC_PUMP_BUFFER_PORT)
                {
                    post_str = (type == CTC_PUMP_FUNC_REPORT_BUFFER_EVENT)
                                ? "%s,switchId=%u type=\"port\",dir=\"%s\",port=\"0x%x\",total_bytes=%"PRIu64" %"PRIu64
                                : "%s,switchId=%u,type=port,dir=%s,port=0x%x total_bytes=%"PRIu64" %"PRIu64;
                    sal_snprintf(measurement, INFLUX_MEASUREMENT_MEMSIZE,
                        post_str,
                        str_name, switch_id, str_dir[dir], buffer->port,
                        buffer->total_bytes,
                        TIMESTAMP_UTC(timestamp));
                }
                else if (buffer->type == CTC_PUMP_BUFFER_QUEUE)
                {
                    post_str = (type == CTC_PUMP_FUNC_REPORT_BUFFER_EVENT)
                                ? "%s,switchId=%u type=\"queue\",dir=\"%s\",port=\"0x%x\",queue=%u,sub_queue=%u,total_bytes=%"PRIu64" %"PRIu64
                                : "%s,switchId=%u,type=queue,dir=%s,port=0x%x,queue=%u,sub_queue=%u total_bytes=%"PRIu64" %"PRIu64;
                    sal_snprintf(measurement, INFLUX_MEASUREMENT_MEMSIZE,
                        post_str,
                        str_name, switch_id, str_dir[dir], buffer->port, buffer->queue, buffer->sub_queue,
                        buffer->total_bytes,
                        TIMESTAMP_UTC(timestamp));
                }
                else
                {
                    return -1;
                }
            }
            break;
        case CTC_PUMP_FUNC_QUERY_MBURST_STATS:
        case CTC_PUMP_FUNC_REPORT_LATENCY_STATS:
        case CTC_PUMP_FUNC_REPORT_MBURST_EVENT:
            {
                uint8 idx = 0;
                char str_temp[INFLUX_MEASUREMENT_MEMSIZE] = {0};
                ctc_pump_latency_t* stats = &((ctc_pump_latency_t*)p_val)[p_val_ofst];

                if (type == CTC_PUMP_FUNC_QUERY_MBURST_STATS)
                {
                    str_name = "mburst_stats";
                }
                else if (type == CTC_PUMP_FUNC_REPORT_LATENCY_STATS)
                {
                    str_name = "latency_stats";
                }
                else
                {
                    str_name = "mburst_event";
                }
                post_str = (type == CTC_PUMP_FUNC_REPORT_MBURST_EVENT)
                            ? "%s,switchId=%u port=\"0x%x\",thrd=\"%u-%u\",count=%u %"PRIu64"\n"
                            : "%s,switchId=%u,port=0x%x,thrd=%u-%u count=%u %"PRIu64"\n";
                /*Level 0-6*/
                for (idx = 0; idx < CTC_PUMP_LATENCY_LEVEL - 1; idx++)
                {
                    sal_snprintf(str_temp, INFLUX_MEASUREMENT_MEMSIZE,
                        post_str,
                        str_name, switch_id, stats->port,stats->thrd[idx],stats->thrd[idx + 1]-1,stats->count[idx],
                        TIMESTAMP_UTC(timestamp));

                    sal_strcat(measurement, str_temp);
                }
                /*Level 7*/
                post_str = (type == CTC_PUMP_FUNC_REPORT_MBURST_EVENT)
                            ? "%s,switchId=%u port=\"0x%x\",thrd=\"%u-#\",count=%u %"PRIu64
                            : "%s,switchId=%u,port=0x%x,thrd=%u-# count=%u %"PRIu64;
                sal_snprintf(str_temp, INFLUX_MEASUREMENT_MEMSIZE,
                    post_str,
                    str_name, switch_id, stats->port, stats->thrd[idx],stats->count[idx],
                    TIMESTAMP_UTC(timestamp));
                sal_strcat(measurement, str_temp);
            }
            break;
        default:
            return -1;
    }

    return 0;
}

int32
ctc_pump_influx_send_data(ctc_pump_func_data_t* data)
{
    int32 ret = 0;
    uint8  delta = 1;
    uint32 idx = 0;
    char* temp  = NULL;
    char* measurement = NULL;

    if (NULL == data)
    {
        PUMP_LOG(CTC_PUMP_LOG_ERROR, "NULL PTR!");
        return -1;
    }
    /*
    PUMP_LOG(CTC_PUMP_LOG_INFO, "type:%d count: %d timestamp:%s",data->type, data->count, ctime(&data->timestamp));
    */
    if ((data->type == CTC_PUMP_FUNC_QUERY_MBURST_STATS)
        || (data->type == CTC_PUMP_FUNC_REPORT_LATENCY_STATS)
        || (data->type == CTC_PUMP_FUNC_REPORT_MBURST_EVENT))
    {
        delta = CTC_PUMP_LATENCY_LEVEL;
    }
    temp = sal_malloc(delta * INFLUX_MEASUREMENT_MEMSIZE);
    if (NULL == temp)
    {
        PUMP_LOG(CTC_PUMP_LOG_ERROR, "No Memory!");
        return -1;
    }

    measurement = sal_malloc(delta * data->count * INFLUX_MEASUREMENT_MEMSIZE);
    if (NULL == measurement)
    {
        PUMP_LOG(CTC_PUMP_LOG_ERROR, "No Memory!");
        sal_free(temp);
        return -1;
    }
    sal_memset(measurement, 0, delta * data->count * INFLUX_MEASUREMENT_MEMSIZE);

    while (idx < data->count)
    {
        if (idx && !ret)
        {
            sal_strcat(measurement, "\n");
        }
        sal_memset(temp, 0, delta * INFLUX_MEASUREMENT_MEMSIZE);
        ret = ctc_pump_influx_post_template(data->type, data->switch_id, data->timestamp,
                                      data->data, idx, temp);
        if (ret)
        {
            idx++;
            continue;
        }
        sal_strcat(measurement, temp);
        idx++;
        sal_task_sleep(2);
    }

    /*Write database*/
    ctc_pump_influx_insert_data(measurement);
    sal_free(measurement);
    sal_free(temp);
    return 0;
}


int32
ctc_pump_influx_post_pkt_buf(char* measurement, uint32 pkt_len, uint8* buf)
{
    char* pkt_buf = NULL;
    uint32 pkt_idx = 0;
    char temp[INFLUX_MEASUREMENT_MEMSIZE] = {0};

    for (pkt_idx = 0; buf && (pkt_idx < pkt_len); pkt_idx++)
    {
        if (pkt_idx == 0)
        {
            pkt_buf = sal_malloc(2 * pkt_len+1);
            if (!pkt_buf)
            {
                PUMP_LOG(CTC_PUMP_LOG_ERROR, "No Memory!");
                return -1;
            }
            sal_memset(pkt_buf, 0, 2 * pkt_len+1);
        }
        sal_snprintf((char*)pkt_buf + pkt_idx * 2 , 2 * pkt_len+1, "%.2x", buf[pkt_idx]);
    }

    sal_strcat(measurement, temp);
    if (pkt_buf)
    {
        sal_strcat(measurement, ",packets=\"");
        sal_strcat(measurement, (char*)pkt_buf);
        sal_strcat(measurement, "\"");
        sal_free(pkt_buf);
    }

    return 0;
}

void
ctc_pump_influx_post_pkt_info(char* measurement, ctc_pump_pkt_info_t* info)
{
    char buf[44] = {0};
    char temp[INFLUX_MEASUREMENT_MEMSIZE] = {0};

    if (info->masks & CTC_PUMP_PKT_MASK_MACDA)
    {
        sal_snprintf(temp, INFLUX_MEASUREMENT_MEMSIZE, ",DestMAC=\"%.2x:%.2x:%.2x:%.2x:%.2x:%.2x\"",
                                                        info->mac_da[0],
                                                        info->mac_da[1],
                                                        info->mac_da[2],
                                                        info->mac_da[3],
                                                        info->mac_da[4],
                                                        info->mac_da[5]);
        sal_strcat(measurement, temp);
    }
    if (info->masks & CTC_PUMP_PKT_MASK_MACSA)
    {
        sal_snprintf(temp, INFLUX_MEASUREMENT_MEMSIZE, ",SourceMAC=\"%.2x:%.2x:%.2x:%.2x:%.2x:%.2x\"",
                                                        info->mac_sa[0],
                                                        info->mac_sa[1],
                                                        info->mac_sa[2],
                                                        info->mac_sa[3],
                                                        info->mac_sa[4],
                                                        info->mac_sa[5]);
        sal_strcat(measurement, temp);
    }
    if (info->masks & CTC_PUMP_PKT_MASK_PROTO)
    {
        sal_snprintf(temp, INFLUX_MEASUREMENT_MEMSIZE, ",Protocol=\"0x%.4x\"",info->protocol);
        sal_strcat(measurement, temp);
    }
    if (info->masks & CTC_PUMP_PKT_MASK_IPDA)
    {
        sal_inet_ntop(AF_INET, &info->ip_da.v4, buf, 44);
        sal_snprintf(temp, INFLUX_MEASUREMENT_MEMSIZE, ",DestIP=\"%s\"",buf);
        sal_strcat(measurement, temp);
    }
    if (info->masks & CTC_PUMP_PKT_MASK_IPSA)
    {
        sal_inet_ntop(AF_INET, &info->ip_sa.v4, buf, 44);
        sal_snprintf(temp, INFLUX_MEASUREMENT_MEMSIZE, ",SourceIP=\"%s\"",buf);
        sal_strcat(measurement, temp);
    }
    if (info->masks & CTC_PUMP_PKT_MASK_IP6DA)
    {
        sal_inet_ntop(AF_INET6, info->ip_da.v6, buf, 44);
        sal_snprintf(temp, INFLUX_MEASUREMENT_MEMSIZE, ",DestIP=\"%s\"",buf);
        sal_strcat(measurement, temp);
    }
    if (info->masks & CTC_PUMP_PKT_MASK_IP6SA)
    {
        sal_inet_ntop(AF_INET6, info->ip_sa.v6, buf, 44);
        sal_snprintf(temp, INFLUX_MEASUREMENT_MEMSIZE, ",SourceIP=\"%s\"",buf);
        sal_strcat(measurement, temp);
    }
    if (info->masks & CTC_PUMP_PKT_MASK_SRCPORT)
    {
        sal_snprintf(temp, INFLUX_MEASUREMENT_MEMSIZE, ",SourcePort=%d", info->src_port);
        sal_strcat(measurement, temp);
    }
    if (info->masks & CTC_PUMP_PKT_MASK_DSTPORT)
    {
        sal_snprintf(temp, INFLUX_MEASUREMENT_MEMSIZE, ",DestPort=%d", info->dst_port);
        sal_strcat(measurement, temp);
    }
}


int32
ctc_pump_influx_send_pkt_info(ctc_pump_func_data_t* data)
{
    uint32 idx = 0;
    uint32 total_len = 0;
    ctc_pump_drop_info_t* drop = NULL;
    ctc_pump_efd_info_t* efd = NULL;
    ctc_pump_pkt_info_t* pkt_info = NULL;
    char* measurement = NULL;
    uint8* pkt_buf = NULL;
    uint32 pkt_len = 0;
    char temp[INFLUX_MEASUREMENT_MEMSIZE] = {0};

    if ((NULL == data) || (NULL == data->data) || (0 == data->count))
    {
        PUMP_LOG(CTC_PUMP_LOG_ERROR, "NULL PTR!");
        return -1;
    }

    if (data->type != CTC_PUMP_FUNC_REPORT_DROP_INFO
        && data->type != CTC_PUMP_FUNC_REPORT_EFD)
    {
        return -1;
    }
    drop = (ctc_pump_drop_info_t*)data->data;
    efd = (ctc_pump_efd_info_t*)data->data;


    while (idx < data->count)
    {
        pkt_len = (data->type == CTC_PUMP_FUNC_REPORT_DROP_INFO) ? drop[idx].pkt_len : efd[idx].pkt_len;
        total_len += ((INFLUX_MEASUREMENT_MEMSIZE + pkt_len) * 2);
        idx++;
    }
    measurement = sal_malloc(total_len);
    if (NULL == measurement)
    {
        PUMP_LOG(CTC_PUMP_LOG_ERROR, "No Memory!");
        return -1;
    }
    sal_memset(measurement, 0, total_len);

    idx = 0;
    while (idx < data->count)
    {
        if (idx)
        {
            sal_strcat(measurement, "\n");
        }
        if (data->type == CTC_PUMP_FUNC_REPORT_DROP_INFO)
        {
            sal_snprintf(temp, INFLUX_MEASUREMENT_MEMSIZE,
                        "drop_info,switchId=%d netwrokPort=\"0x%x\",packetId=%d,reason=\"%s\"",
                        data->switch_id, drop[idx].port, drop[idx].pkt_id, drop[idx].reason);

            pkt_info =  &drop[idx].info;
        }
        else
        {
            sal_snprintf(temp, INFLUX_MEASUREMENT_MEMSIZE,
                        "efd_info,switchId=%d netwrokPort=\"0x%x\"",
                        data->switch_id, efd[idx].port);

             pkt_info =  &efd[idx].info;
        }

        sal_strcat(measurement, temp);

        ctc_pump_influx_post_pkt_info(measurement, pkt_info);
        pkt_buf = (data->type == CTC_PUMP_FUNC_REPORT_DROP_INFO) ? drop[idx].pkt_buf : efd[idx].pkt_buf;

        ctc_pump_influx_post_pkt_buf(measurement, pkt_len, pkt_buf);
        sal_snprintf(temp, INFLUX_MEASUREMENT_MEMSIZE," %"PRIu64, (uint64)TIMESTAMP_UTC(data->timestamp));
        sal_strcat(measurement, temp);
        idx++;
        sal_task_sleep(2);
    }
    /*PUMP_LOG(CTC_PUMP_LOG_ERROR, "measurement:%s", measurement);*/
    /*Write database*/
    ctc_pump_influx_insert_data(measurement);
    sal_free(measurement);
    return 0;
}




int32
ctc_pump_influx_create_db(void)
{
    int ret = 0;
    ctc_influxc_client_t* client = ctc_influxc_client_new(g_influxc.host, g_influxc.user, g_influxc.passwd, g_influxc.db, g_influxc.ssl);
    ret = ctc_influxc_create_database(client);
    if (ret >= 300)
    {
        PUMP_LOG(CTC_PUMP_LOG_ERROR, "ret=%d",ret);
    }
    ctc_influxc_client_free(client);
    PUMP_LOG(CTC_PUMP_LOG_INFO, "db_name:%s", g_influxc.db);
    return 0;
}

int32
ctc_pump_influx_create_user(void)
{
    int ret = 0;
    ctc_influxc_client_t* client = ctc_influxc_client_new(g_influxc.host, g_influxc.user, g_influxc.passwd, g_influxc.db, g_influxc.ssl);
    ret = ctc_influxc_create_user(client);
    if (ret >= 300)
    {
        PUMP_LOG(CTC_PUMP_LOG_ERROR, "ret=%d",ret);
    }
    ctc_influxc_client_free(client);
    PUMP_LOG(CTC_PUMP_LOG_INFO, "host:%s user:%s ssl:%d",g_influxc.host, g_influxc.user, g_influxc.ssl);
    return 0;
}

int32
ctc_pump_influx_create_policy()
{
    int ret = 0;
    ctc_influxc_client_t* client = NULL;
    client = ctc_influxc_client_new(g_influxc.host, g_influxc.user, g_influxc.passwd, g_influxc.db, g_influxc.ssl);
    ret = ctc_influxc_create_policy(client, "autogen", g_influxc.policy, 1, 1);
    if (ret >= 300)
    {
        PUMP_LOG(CTC_PUMP_LOG_ERROR, "ret=%d",ret);
    }
    ctc_influxc_client_free(client);
    PUMP_LOG(CTC_PUMP_LOG_INFO, "Create Policy: autogen %s",g_influxc.policy);
    return 0;
}


#define ______API______
/*
sal_mutex_t*  p_db_mutex = NULL;
*/
int32
_ctc_pump_db_api(ctc_pump_func_data_t* data)
{
    if (NULL == data)
    {
        PUMP_LOG(CTC_PUMP_LOG_ERROR, "NULL PTR!");
        return -1;
    }

    switch (data->type)
    {
        case CTC_PUMP_FUNC_QUERY_TEMPERATURE:
        case CTC_PUMP_FUNC_QUERY_VOLTAGE:
        case CTC_PUMP_FUNC_QUERY_MAC_STATS:
        case CTC_PUMP_FUNC_QUERY_MBURST_STATS:
        case CTC_PUMP_FUNC_QUERY_BUFFER_WATERMARK:
        case CTC_PUMP_FUNC_QUERY_LATENCY_WATERMARK:
        case CTC_PUMP_FUNC_REPORT_BUFFER_STATS:
        case CTC_PUMP_FUNC_REPORT_LATENCY_STATS:
        case CTC_PUMP_FUNC_REPORT_BUFFER_EVENT:
        case CTC_PUMP_FUNC_REPORT_LATENCY_EVENT:
        case CTC_PUMP_FUNC_REPORT_MBURST_EVENT:
            return ctc_pump_influx_send_data(data);
        case CTC_PUMP_FUNC_REPORT_DROP_INFO:
        case CTC_PUMP_FUNC_REPORT_EFD:
            return ctc_pump_influx_send_pkt_info(data);
        default:
            PUMP_LOG(CTC_PUMP_LOG_ERROR, "data_type=%d!",data->type);
            return -1;
    }

    return 0;
}

int32
ctc_pump_db_api(ctc_pump_func_data_t* data)
{
    return _ctc_pump_db_api(data);
}


int32
ctc_pump_db_init(ctc_pump_db_cfg_t* cfg)
{
    if (NULL == cfg)
    {
        PUMP_LOG(CTC_PUMP_LOG_ERROR, "NULL PTR!");
        return -1;
    }

    sal_memcpy(&g_influxc, cfg, sizeof(ctc_pump_db_cfg_t));
    /*Create mutex*/
    #if 0
    ret = sal_mutex_create(&p_db_mutex);
    if (ret || !p_db_mutex)
    {
        PUMP_LOG(CTC_PUMP_LOG_ERROR, "ret=%d!", ret);
        return -1;
    }
    #endif
    /*Create User and Password*/
    ctc_pump_influx_create_user();
    /*Create DB*/
    ctc_pump_influx_create_db();
    /*Create Retention Policy*/
    ctc_pump_influx_create_policy();
    return 0;
}

int32
ctc_pump_db_deinit(void)
{
    return 0;
}

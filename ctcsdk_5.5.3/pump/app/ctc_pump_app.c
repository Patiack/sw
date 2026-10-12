#include "ctc_pump.h"
#include "ctc_pump_app.h"
#include "ctc_pump_db.h"

#define PUMP_APP_CFG_FILE "pump_cfg.cfg"

#define PUMP_CFG_HOST    "host"
#define PUMP_CFG_DB_NAME "db_name"
#define PUMP_CFG_USER    "user_name"
#define PUMP_CFG_PASSWD  "password"
#define PUMP_CFG_POLICY  "data_policy"
#define PUMP_CFG_SSL     "https_enabled"

#define PUMP_CFG_LOG_EN    "log_enabled"
#define PUMP_CFG_TIME_ADJ    "time_adjust"

#define PUMP_CFG_TEMPERATURE    "temperature"
#define PUMP_CFG_VOLTAGE        "voltage"
#define PUMP_CFG_MAC_STATS      "mac_stats"
#define PUMP_CFG_BUFFER_STATS   "buffer_stats"
#define PUMP_CFG_LATENCY_STATS  "latency_stats"
#define PUMP_CFG_MBURST_STATS   "mburst_stats"
#define PUMP_CFG_BUFFER_WATERMARK   "buffer_watermark"
#define PUMP_CFG_LATENCY_WATERMARK  "latency_watermark"
#define PUMP_CFG_MBURST_EVENT   "mburst_event"
#define PUMP_CFG_BUFFER_EVENT   "buffer_event"
#define PUMP_CFG_LATENCY_EVENT  "latency_event"
#define PUMP_CFG_DROP_REPORT    "drop_report"
#define PUMP_CFG_EFD_REPORT    "efd_report"

#define PUMP_BIT_SET(flag, bit)      ((flag) = (flag) | (1 << (bit)))

extern int g_pump_time_adjust;

int32
ctc_pump_app_parse_file_line(char* line, ctc_pump_config_t* pump_cfg, ctc_pump_db_cfg_t* db_cfg)
{
    char  str_start = 0;
    char  str_end = 0;
    char  val_valid = 0;
    char  str_key[64] = {0};
    char  str_val1[64] = {0};
    char  str_val2[64] = {0};
    int32 str_idx = 0;
    int32 str_idx1 = 0;
    int32 str_idx2 = 0;
    int32 pos = 0;
    int32 idx = 0;
    char* str_func[] = {/*care the sequence, order by ctc_pump_func_type_t*/
                PUMP_CFG_TEMPERATURE,
                PUMP_CFG_VOLTAGE,
                PUMP_CFG_MAC_STATS,
                PUMP_CFG_MBURST_STATS,
                PUMP_CFG_BUFFER_WATERMARK,
                PUMP_CFG_LATENCY_WATERMARK,

                PUMP_CFG_BUFFER_STATS,
                PUMP_CFG_LATENCY_STATS,
                PUMP_CFG_BUFFER_EVENT,
                PUMP_CFG_LATENCY_EVENT,
                PUMP_CFG_MBURST_EVENT,
                PUMP_CFG_DROP_REPORT,
                PUMP_CFG_EFD_REPORT,
                };

    if (!line || !pump_cfg || !db_cfg)
    {
        return -1;
    }

    /* '#' means notes */
    if (line[0] == '#')
    {
        return 0;
    }
    while (pos < strlen(line))
    {
        if (line[pos] == ' ')
        {
            pos++;
            continue;
        }

        if (line[pos] == ']')
        {
            str_end++;
        }
        if (line[pos] == '=')
        {
            val_valid = 1;
        }

        if (str_end < str_start)
        {
            if (str_start == 1)
            {
                str_key[str_idx++] = line[pos];
            }
            else if (val_valid && (str_start == 2))
            {
                str_val1[str_idx1++] = line[pos];
            }
            else if (val_valid && (str_start == 3))
            {
                str_val2[str_idx2++] = line[pos];
            }
        }

        if (line[pos] == '[')
        {
            str_start++;
        }
        pos++;
    }

    if (!strcmp(str_key, PUMP_CFG_HOST))
    {
        strncpy(db_cfg->host, str_val1, DB_CFG_MAX_STR_LEN);
    }
    if (!strcmp(str_key, PUMP_CFG_DB_NAME))
    {
        strncpy(db_cfg->db, str_val1, DB_CFG_MAX_STR_LEN);
    }
    if (!strcmp(str_key, PUMP_CFG_USER))
    {
        strncpy(db_cfg->user, str_val1, DB_CFG_MAX_STR_LEN);
    }
    if (!strcmp(str_key, PUMP_CFG_PASSWD))
    {
        strncpy(db_cfg->passwd, str_val1, DB_CFG_MAX_STR_LEN);
    }
    if (!strcmp(str_key, PUMP_CFG_POLICY))
    {
        strncpy(db_cfg->policy, str_val1, DB_CFG_MAX_STR_LEN);
    }
    if (!strcmp(str_key, PUMP_CFG_SSL))
    {
        if (!strcmp(str_val1, "false") || !strcmp(str_val1, "FALSE"))
        {
            db_cfg->ssl = 0;
        }
        if (!strcmp(str_val1, "true") || !strcmp(str_val1, "TRUE"))
        {
            db_cfg->ssl = 1;
        }
    }
    if (!strcmp(str_key, PUMP_CFG_LOG_EN))
    {
        if (!strcmp(str_val1, "true") || !strcmp(str_val1, "TRUE"))
        {
            g_pump_log_en = 1;
        }
    }
    if (!strcmp(str_key, PUMP_CFG_TIME_ADJ))
    {
        if (str_idx1)
        {
            sscanf(str_val1, "%d", &g_pump_time_adjust);
        }
    }

    for (idx = 0; idx < CTC_PUMP_FUNC_MAX; idx++)
    {
        if (strcmp(str_key, str_func[idx]))
        {
            continue;
        }
        if (!strcmp(str_val1, "true") || !strcmp(str_val1, "TRUE"))
        {
            PUMP_BIT_SET(pump_cfg->func_bmp[0], idx);
            if (str_idx2)
            {
                sscanf(str_val2, "%d", &pump_cfg->interval[idx]);
            }
        }
    }

    return 0;
}

int32
ctc_pump_app_parse_cfg_file(const char* file_name, ctc_pump_config_t* pump_cfg, ctc_pump_db_cfg_t* db_cfg)
{
    char line[128] = {0};
    sal_file_t local_fp = NULL;

    if (!file_name || !pump_cfg || !db_cfg)
    {
        return -1;
    }

    local_fp = sal_fopen(file_name, "r");
    if (NULL == local_fp)
    {
        printf("Open %s Failed!\n", file_name);
        return -1;
    }
    while (NULL != sal_fgets(line, 128, local_fp))
    {
        ctc_pump_app_parse_file_line(line, pump_cfg, db_cfg);
    }
    return 0;
}


int32
ctc_pump_app_init(uint8 lchip)
{
    int32 ret = 0;
    ctc_pump_config_t cfg;
    ctc_pump_db_cfg_t db_cfg;

    memset(&cfg, 0, sizeof(ctc_pump_config_t));

    /* Parse pump_cfg.cfg */
    ret = ctc_pump_app_parse_cfg_file(PUMP_APP_CFG_FILE, &cfg, &db_cfg);
    if (ret < 0)
    {
        return -1;
    }
    /*DB Init (NB-API)*/
    ctc_pump_db_init(&db_cfg);
    cfg.push_func = ctc_pump_db_api;
    /*Pump Init (SB-API)*/
    ctc_pump_init(lchip, &cfg);

    return 0;
}


int32
ctc_pump_app_deinit(uint8 lchip)
{
    int32 ret = 0;
    ret = ctc_pump_deinit(lchip);
    if (ret < 0)
    {
        return -1;
    }
    ret = ctc_pump_db_deinit();
    if (ret < 0)
    {
        return -1;
    }
    return 0;
}



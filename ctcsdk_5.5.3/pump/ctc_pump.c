#include "ctc_pump.h"
#include "ctc_linklist.h"


/* log file max size 5M */
#define PUMP_LOG_FILE_SIZE  (5 * 1024 * 1024)
#define PUMP_BMP_ISSET(bmp, offset) (((bmp)[offset/32] & (1 << ((offset) % 32))) ? 1 : 0)
#define PUMP_BMP_UNSET(bmp, offset) ((bmp)[offset/32] &= (~(1 << ((offset) % 32))))


typedef struct ctc_pump_master_s
{
    ctc_slistnode_t head;
    uint8 lchip;
    ctc_pump_config_t cfg;
    uint32 cur_interval[CTC_PUMP_FUNC_MAX];
    sal_timer_t* timerId;
}ctc_pump_master_t;

/*ctc_pump_master_t*/
ctc_slist_t* p_pump_slist = NULL;

char g_pump_log_en = 0;
static char g_pump_log_file[64] = "./pump_log.txt";


extern ctc_pump_func
ctc_pump_get_query_func(ctc_pump_func_type_t func_type);

extern int32
ctc_pump_feature_init(uint8 lchip);
extern int32
ctc_pump_feature_deinit(uint8 lchip);

/*Log handler function*/
void
ctc_pump_log_post(int lvl, char* pre_str, char* format, ...)
{
    char buf[512];
    int rc=0;
    sal_file_t fp;
    va_list ap;
    char* _str_log[] = {"ERROR", "INFO"};/*order by ctc_pump_log_level_e*/
    char timeString[100] = { 0 };
    sal_time_t logtime;
    struct tm *timeinfo;
    uint64 log_file_size = 0;

    if (lvl >= CTC_PUMP_LOG_MAX)
    {
        return;
    }

    va_start(ap, format);
#ifdef SDK_IN_VXWORKS
    rc = vsprintf(buf, format, ap);
#else
    rc = vsnprintf(buf, sizeof(buf), format, ap);
#endif
    va_end(ap);
    if (rc < 0)
    {
        return;
    }

#if defined(_SAL_LINUX_KM)
{
    fp = sal_fopen(g_pump_log_file, "r");
    if (NULL == fp)
    {
        return;
    }
    sal_fseek(fp, 0, SEEK_END);
    log_file_size = sal_ftell(fp);
    sal_fclose(fp);
}
#else
{
    struct stat file_stat;
    stat(g_pump_log_file, &file_stat);
    log_file_size = file_stat.st_size;
}
#endif

    fp = sal_fopen(g_pump_log_file, (log_file_size >= PUMP_LOG_FILE_SIZE) ? "wb+":"a");
    if (NULL == fp)
    {
        return;
    }

    sal_time(&logtime);

#if defined(_SAL_LINUX_UM)
    timeinfo = localtime(&logtime);
    strftime(timeString, 100, "%Y-%m-%d %H:%M:%S", timeinfo);
#else
{
    struct rtc_time tm_tmp;
    rtc_time_to_tm(logtime, &tm_tmp);
    sal_snprintf(timeString,
                 sizeof(timeString),
                 "%4d-%02d-%02d %02d:%02d:%02d",
                 1900 + tm_tmp.tm_year,
                 tm_tmp.tm_mon,
                 tm_tmp.tm_mday,
                 tm_tmp.tm_hour,
                 tm_tmp.tm_min,
                 tm_tmp.tm_sec);
}
#endif

    sal_fprintf(fp, "[%s][%s]%s%s\n", _str_log[lvl], timeString, pre_str, buf);
    sal_fclose(fp);
}


#define ______PUMP_HANDLE_FUNC______


void
ctc_pump_query_buffer_watermark(uint8 lchip)
{
    ctc_pump_func func = ctc_pump_get_query_func(CTC_PUMP_FUNC_QUERY_BUFFER_WATERMARK);
    func(lchip);
    return;
}

void
ctc_pump_query_latency_watermark(uint8 lchip)
{
    ctc_pump_func func = ctc_pump_get_query_func(CTC_PUMP_FUNC_QUERY_LATENCY_WATERMARK);
    func(lchip);
    return;
}

void
ctc_pump_query_mac_stats(uint8 lchip)
{
    ctc_pump_func func = ctc_pump_get_query_func(CTC_PUMP_FUNC_QUERY_MAC_STATS);
    func(lchip);
    return;
}

void
ctc_pump_query_mburst_stats(uint8 lchip)
{
    ctc_pump_func func = ctc_pump_get_query_func(CTC_PUMP_FUNC_QUERY_MBURST_STATS);
    func(lchip);
    return;
}

void
ctc_pump_query_buffer_stats(uint8 lchip)
{
    ctc_pump_func func = ctc_pump_get_query_func(CTC_PUMP_FUNC_REPORT_BUFFER_STATS);
    func(lchip);
    return;
}

void
ctc_pump_query_latency_stats(uint8 lchip)
{
    ctc_pump_func func = ctc_pump_get_query_func(CTC_PUMP_FUNC_REPORT_LATENCY_STATS);
    func(lchip);
    return;
}

void
ctc_pump_query_temperature(uint8 lchip)
{
    ctc_pump_func func = ctc_pump_get_query_func(CTC_PUMP_FUNC_QUERY_TEMPERATURE);
    func(lchip);
    return;
}

void
ctc_pump_query_voltage(uint8 lchip)
{
    ctc_pump_func func = ctc_pump_get_query_func(CTC_PUMP_FUNC_QUERY_VOLTAGE);
    func(lchip);
    return;
}


int32
ctc_pump_handler(union sigval sigval)
{
    uint32 func_type = 0;
    ctc_pump_master_t* master = (ctc_pump_master_t*)sigval.sival_ptr;
    ctc_pump_func func[CTC_PUMP_FUNC_MAX] = {NULL};

    if (NULL == master)
    {
        PUMP_LOG(CTC_PUMP_LOG_ERROR, "master is NULL!");
        return -1;
    }

    func[CTC_PUMP_FUNC_QUERY_TEMPERATURE] = ctc_pump_query_temperature;
    func[CTC_PUMP_FUNC_QUERY_VOLTAGE] = ctc_pump_query_voltage;
    func[CTC_PUMP_FUNC_QUERY_MAC_STATS] = ctc_pump_query_mac_stats;
    func[CTC_PUMP_FUNC_QUERY_MBURST_STATS] = ctc_pump_query_mburst_stats;
    func[CTC_PUMP_FUNC_QUERY_BUFFER_WATERMARK] = ctc_pump_query_buffer_watermark;
    func[CTC_PUMP_FUNC_QUERY_LATENCY_WATERMARK] = ctc_pump_query_latency_watermark;
    func[CTC_PUMP_FUNC_REPORT_BUFFER_STATS] = ctc_pump_query_buffer_stats;
    func[CTC_PUMP_FUNC_REPORT_LATENCY_STATS] = ctc_pump_query_latency_stats;

    for (func_type = CTC_PUMP_FUNC_QUERY_TEMPERATURE; func_type < CTC_PUMP_FUNC_MAX; func_type++)
    {
        if ((NULL == func[func_type])
            || !PUMP_BMP_ISSET(master->cfg.func_bmp, func_type))
        {
            continue;
        }
        if ((master->cur_interval[func_type] + 1) == master->cfg.interval[func_type])
        {
            func[func_type](master->lchip);
            master->cur_interval[func_type] = 0;
        }
        else
        {
            master->cur_interval[func_type]++;
        }
        sal_task_sleep(1);
    }
    return 0;
}


ctc_pump_push_cb
ctc_pump_push_get_cb(uint8 lchip)
{
    ctc_slistnode_t* node = NULL;
    CTC_SLIST_LOOP(p_pump_slist, node)
    {
        if (lchip == ((ctc_pump_master_t*)node)->lchip)
        {
            return ((ctc_pump_master_t*)node)->cfg.push_func;
        }
    }
    PUMP_LOG(CTC_PUMP_LOG_ERROR, "lchip=%d, Not Found Push Callback Function!", lchip);
    return NULL;
}

bool
ctc_pump_func_is_enable(uint8 lchip, ctc_pump_func_type_t func_type)
{
    ctc_slistnode_t* node = NULL;
    CTC_SLIST_LOOP(p_pump_slist, node)
    {
        if ((lchip == ((ctc_pump_master_t*)node)->lchip)
            && PUMP_BMP_ISSET(((ctc_pump_master_t*)node)->cfg.func_bmp, func_type))
        {
            return TRUE;
        }
    }
    return FALSE;
}


uint32
ctc_pump_func_interval(uint8 lchip, ctc_pump_func_type_t func_type)
{
    ctc_slistnode_t* node = NULL;
    CTC_SLIST_LOOP(p_pump_slist, node)
    {
        if ((lchip == ((ctc_pump_master_t*)node)->lchip)
            && PUMP_BMP_ISSET(((ctc_pump_master_t*)node)->cfg.func_bmp, func_type))
        {
            return ((ctc_pump_master_t*)node)->cfg.interval[func_type];
        }
    }
    return 0;
}

#if 0
int32
ctc_pump_timer_set(timer_t timerId,int timeInMilliSec)
{
  int32 ret = 0;
  struct itimerspec timerVal;

  timerVal.it_value.tv_sec = timeInMilliSec / 1000;
  timerVal.it_value.tv_nsec = (long)(timeInMilliSec % 1000) * 1000000L;
  timerVal.it_interval.tv_sec = timerVal.it_value.tv_sec;
  timerVal.it_interval.tv_nsec = timerVal.it_value.tv_nsec;

  /*Timer is set*/
  if (timer_settime(timerId, 0, &timerVal, NULL) != 0)
  {
    PUMP_LOG(CTC_PUMP_LOG_ERROR, "timer_settime error!");
    ret = -1;
  }
  return ret;
}

/* Base timer 1s*/
#define CTC_PUMP_BASE_TIMER 1000

int32
ctc_pump_timer_add(void * handler, void * param)
{
    struct sigevent sigTime;
    struct timespec monotonic_time;
    clockid_t clock;
    ctc_pump_master_t* master = (ctc_pump_master_t*)param;

    if (!handler
        || !param)
    {
        PUMP_LOG(CTC_PUMP_LOG_ERROR, "NULL PTR!");
        return -1;
    }

    sal_memset(&sigTime, 0,sizeof(struct sigevent));
    sigTime.sigev_notify = SIGEV_THREAD;
    sigTime.sigev_notify_attributes = NULL;
    sigTime.sigev_notify_function = handler;
    sigTime.sigev_value.sival_ptr = param;

    if (!clock_gettime(CLOCK_MONOTONIC, &monotonic_time))
    {
        clock = CLOCK_MONOTONIC;
    }
    else
    {
        clock = CLOCK_REALTIME;
    }
    if (timer_create(clock, &sigTime, &master->timerId) == 0)
    {
        ctc_pump_timer_set(master->timerId, CTC_PUMP_BASE_TIMER);
    }
    else
    {
        PUMP_LOG(CTC_PUMP_LOG_ERROR, "timer_create error!");
    }


   return 0;
}

int32
ctc_pump_timer_delete(void * param)
{
    ctc_pump_master_t* master = (ctc_pump_master_t*)param;

    if (!param)
    {
        PUMP_LOG(CTC_PUMP_LOG_ERROR, "NULL PTR!");
        return -1;
    }
    return timer_delete(master->timerId);
}
#endif

/* Base timer 1s*/
#define CTC_PUMP_BASE_TIMER 1000

int32
ctc_pump_timer_add(void * handler, void * param)
{
    ctc_pump_master_t* master = (ctc_pump_master_t*)param;
    if (!param || !handler)
    {
        PUMP_LOG(CTC_PUMP_LOG_ERROR, "NULL PTR!");
        return -1;
    }
    if (master->timerId)
    {
        PUMP_LOG(CTC_PUMP_LOG_ERROR, "Timer Already Create!");
        return -1;
    }
    sal_timer_create(&master->timerId, handler, param);
    sal_timer_start(master->timerId, CTC_PUMP_BASE_TIMER);

    return 0;
}

void
ctc_pump_timer_delete(void * param)
{
    ctc_pump_master_t* master = (ctc_pump_master_t*)param;

    if (!param)
    {
        PUMP_LOG(CTC_PUMP_LOG_ERROR, "NULL PTR!");
        return;
    }
    sal_timer_stop(master->timerId);
    sal_timer_destroy(master->timerId);
}

int32
ctc_pump_init(uint8 lchip, ctc_pump_config_t* cfg)
{
    ctc_pump_master_t* master = NULL;
    ctc_slistnode_t* node = NULL;
    int32 ret = 0;
    uint32 func_type = 0;

    PUMP_LOG(CTC_PUMP_LOG_INFO, "lchip=%d",lchip);

    if (NULL == cfg)
    {
        PUMP_LOG(CTC_PUMP_LOG_ERROR, "NULL PTR!");
        return -1;
    }

    CTC_SLIST_LOOP(p_pump_slist, node)
    {
        if (lchip == ((ctc_pump_master_t*)node)->lchip)
        {
            PUMP_LOG(CTC_PUMP_LOG_ERROR, "lchip=%d, Already Init!", lchip);
            return -1;
        }
    }

    master = (ctc_pump_master_t*)sal_malloc(sizeof(ctc_pump_master_t));
    if (NULL == master)
    {
        PUMP_LOG(CTC_PUMP_LOG_ERROR, "No Memory!");
        return -1;
    }

    p_pump_slist = ctc_slist_new();
    if (NULL == p_pump_slist)
    {
        PUMP_LOG(CTC_PUMP_LOG_ERROR, "No Memory!");
        sal_free(master);
        return -1;
    }
    sal_memset(master, 0, sizeof(ctc_pump_master_t));
    ctc_slist_add_tail(p_pump_slist, &(master->head));
    master->lchip = lchip;
    sal_memcpy(&master->cfg, cfg, sizeof(ctc_pump_config_t));


    for (func_type = CTC_PUMP_FUNC_QUERY_TEMPERATURE; func_type < CTC_PUMP_FUNC_MAX; func_type++)
    {
        if (!PUMP_BMP_ISSET(master->cfg.func_bmp, func_type))
        {
            continue;
        }
        if (master->cfg.interval[func_type] == 0)
        {
            master->cfg.interval[func_type] = 5;
        }
        master->cur_interval[func_type] = sal_rand() % master->cfg.interval[func_type];
    }

    ctc_pump_timer_add(ctc_pump_handler, (void*)master);

    ret = ctc_pump_feature_init(lchip);
    if (ret)
    {
        PUMP_LOG(CTC_PUMP_LOG_ERROR, "ret=%d, Feature Init Failed!", ret);
    }
    return 0;
}


int32
ctc_pump_deinit(uint8 lchip)
{
    uint32 func_type = 0;
    uint8 lchip_valid = 0;
    ctc_pump_master_t* master = NULL;
    ctc_slistnode_t* node = NULL;
    int32 ret = 0;

    PUMP_LOG(CTC_PUMP_LOG_INFO, "deinit lchip=%d",lchip);

    CTC_SLIST_LOOP(p_pump_slist, node)
    {
        master = (ctc_pump_master_t*)node;
        if (lchip == master->lchip)
        {
            lchip_valid = 1;
            break;
        }
    }
    if (!lchip_valid)
    {
        PUMP_LOG(CTC_PUMP_LOG_ERROR, "lchip=%d, Not Init.",lchip);
        return -1;
    }
    for (func_type = CTC_PUMP_FUNC_QUERY_TEMPERATURE; func_type < CTC_PUMP_FUNC_MAX; func_type++)
    {
        PUMP_BMP_UNSET(master->cfg.func_bmp, func_type);
        master->cfg.interval[func_type] = 0;
    }

    ctc_pump_timer_delete((void*)master);

    ret = ctc_pump_feature_deinit(lchip);
    if (ret)
    {
        PUMP_LOG(CTC_PUMP_LOG_ERROR, "ret=%d, Feature Deinit Failed!", ret);
    }

    sal_task_sleep(CTC_PUMP_BASE_TIMER);
    ctc_slist_delete_node(p_pump_slist, &(master->head));
    sal_free(master);
    master = NULL;

    if (p_pump_slist->count == 0)
    {
        ctc_slist_free(p_pump_slist);
        p_pump_slist= NULL;
    }

    return 0;
}



/**
 @file ctc_influxc.h

 @date 2019-6-6

 @version v1.0

 This file define the pump database config structures
*/

#ifndef _CTC_PUMP_DB_H
#define _CTC_PUMP_DB_H
#ifdef __cplusplus
extern "C" {
#endif

#define DB_CFG_MAX_STR_LEN 32

typedef struct ctc_pump_db_cfg_s
{
    char host[DB_CFG_MAX_STR_LEN];
    char user[DB_CFG_MAX_STR_LEN];
    char passwd[DB_CFG_MAX_STR_LEN];
    char db[DB_CFG_MAX_STR_LEN];
    char ssl;
    char policy[DB_CFG_MAX_STR_LEN];
}ctc_pump_db_cfg_t;


extern int32
ctc_pump_db_init(ctc_pump_db_cfg_t* cfg);

extern int32
ctc_pump_db_deinit(void);

extern int32
ctc_pump_db_api(ctc_pump_func_data_t* data);

#ifdef __cplusplus
}
#endif

#endif



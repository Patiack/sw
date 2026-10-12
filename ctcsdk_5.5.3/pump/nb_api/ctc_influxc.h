/**
 @file ctc_influxc.h

 @date 2019-6-6

 @version v1.0

 This file define the influxdb-c structures and APIs
*/

#ifndef _CTC_INFLUXC_H
#define _CTC_INFLUXC_H
#ifdef __cplusplus
extern "C" {
#endif

#include "sal.h"

typedef struct ctc_influxc_client_s
{
    char* schema;
    char* host;
    char* uname;
    char* password;
    char* dbname;
    char ssl;
}ctc_influxc_client_t;

extern ctc_influxc_client_t*
ctc_influxc_client_new(char* host,char* uname,char* password,char* dbname,char ssl);

extern void
ctc_influxc_client_free(ctc_influxc_client_t* client);

extern int
ctc_influxc_create_database(ctc_influxc_client_t* client);

extern int
ctc_influxc_insert_measurement(ctc_influxc_client_t* client,char* measurement);

extern int
ctc_influxc_create_policy(ctc_influxc_client_t* client,
                                char* policy_name, char* time, char is_default, char is_update);

extern int
ctc_influxc_delete_policy(ctc_influxc_client_t* client, char* policy_name);

extern int
ctc_influxc_create_user(ctc_influxc_client_t* client);

extern int
ctc_influxc_delete_user(ctc_influxc_client_t* client);

#ifdef __cplusplus
}
#endif

#endif


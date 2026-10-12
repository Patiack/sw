/**
 @file ctc_pump_app.h

 @date 2019-6-6

 @version v1.0

 This file define the pump app structures and APIs
*/
#ifndef _CTC_PUMP_APP_H
#define _CTC_PUMP_APP_H
#ifdef __cplusplus
extern "C" {
#endif


extern int32
ctc_pump_app_init(uint8 lchip);
extern int32
ctc_pump_app_deinit(uint8 lchip);


#ifdef __cplusplus
}
#endif

#endif


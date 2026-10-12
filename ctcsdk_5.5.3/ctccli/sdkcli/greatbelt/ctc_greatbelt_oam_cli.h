/**
 @file ctc_greatbelt_oam_cli.h

 @date 2012-11-20

 @version v2.0

 This file declares extern function for greatbelt oam cli module

*/

#ifndef _CTC_GREATBELT_OAM_CLI_H_
#define _CTC_GREATBELT_OAM_CLI_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "sal.h"
#include "ctc_cli.h"
#include "ctc_cli_common.h"

extern int32
ctc_greatbelt_oam_cli_init(void);

extern int32
ctc_greatbelt_oam_cli_deinit(void);

#ifdef __cplusplus
}
#endif

#endif


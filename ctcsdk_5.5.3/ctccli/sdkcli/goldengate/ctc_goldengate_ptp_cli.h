/**
 @file ctc_ptp_cli.h

 @date 2010-06-09

 @version v2.0

 This file declares extern function for ptp cli module

*/

#ifndef _CTC_GOLDENGATE_PTP_CLI_H
#define _CTC_GOLDENGATE_PTP_CLI_H
#ifdef __cplusplus
extern "C" {
#endif
#include "sal.h"
#include "ctc_cli.h"

/**
 @brief  Initialize sdk ptp module CLIs
*/
extern int32
ctc_goldengate_ptp_cli_init(void);

#ifdef __cplusplus
}
#endif

#endif  /* _CTC_PTP_CLI_H */


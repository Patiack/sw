/**
 @file ctc_ptp_cli.h

 @date 2010-06-09

 @version v2.0

 This file declares extern function for ptp cli module

*/

#ifndef _CTC_GOLDENGATE_IPFIX_CLI_H
#define _CTC_GOLDENGATE_IPFIX_CLI_H
#ifdef __cplusplus
extern "C" {
#endif
#include "sal.h"
#include "ctc_cli.h"

/**
 @brief  Initialize sdk ipfix module CLIs
*/
extern int32
ctc_goldengate_ipfix_cli_init(void);

extern int32
ctc_goldengate_ipfix_cli_deinit(void);

#ifdef __cplusplus
}
#endif

#endif  /* _CTC_GOLDENGATE_CLI_H */


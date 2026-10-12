/**
 *  @file ctc_greatbel_port_cli.h
 *
 *   @date 2012-04-06
 *
 *    @version v2.0
 *
 *    ---file comments----
 *    */

#ifndef _CTC_GREATBELT_PORT_CLI_H
#define _CTC_GREATBELT_PORT_CLI_H
#ifdef __cplusplus
extern "C" {
#endif
#include "sal.h"
#include "ctc_cli.h"
#include "ctc_cli_common.h"

/**
 *  @brief  Initialize greatbelt sdk port module CLIs, for greatbelt only
 *
 *   @param[in/out]
 *
 *    @return CTC_E_XXX
 *
 *    */
extern int32
ctc_greatbelt_port_cli_init(void);

extern int32
ctc_greatbelt_port_cli_deinit(void);

#ifdef __cplusplus
}
#endif

#endif  /* _CTC_GREATBELT_PORT_CLI_H */


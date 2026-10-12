/**
 *  @file ctc_greatbel_pdu_cli.h
 *
 *   @date 2012-8-18
 *
 *    @version v2.0
 *
 *    ---file comments----
 *    */

#ifndef _CTC_GREATBELT_PDU_CLI_H
#define _CTC_GREATBELT_PDU_CLI_H
#ifdef __cplusplus
extern "C" {
#endif
#include "sal.h"
#include "ctc_cli.h"
#include "ctc_cli_common.h"

/**
 *  @brief  Initialize sdk pdu module CLIs, for greatbelt only
 *
 *   @param[in/out]
 *
 *    @return CTC_E_XXX
 *
 *    */
extern int32
ctc_greatbelt_pdu_cli_init(void);

extern int32
ctc_greatbelt_pdu_cli_deinit(void);

#ifdef __cplusplus
}
#endif

#endif  /* _CTC_GREATBELT_PDU_CLI_H */


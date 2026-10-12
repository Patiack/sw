/**
 @file ctc_greatbelt_chip_cli.h

 @date 2012-3-20

 @version v2.0

---file comments----
*/

#ifndef _CTC_GREATBELT_CHIP_CLI_H
#define _CTC_GREATBELT_CHIP_CLI_H
#ifdef __cplusplus
extern "C" {
#endif
#include "sal.h"
#include "ctc_cli.h"
#include "ctc_cli_common.h"

/**
 @brief  Initialize sdk chip module CLIs

 @param[in/out]   cli_tree     CLI tree

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_chip_cli_init(void);

extern int32
ctc_greatbelt_chip_cli_deinit(void);

#ifdef __cplusplus
}
#endif

#endif  /* _CTC_GREATBELT_CHIP_CLI_H */


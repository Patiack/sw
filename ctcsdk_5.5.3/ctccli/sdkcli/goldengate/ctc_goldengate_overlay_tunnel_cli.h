/**
 @file ctc_goldengate_overlay_tunnel_cli.h

 @date 2013-11-10

 @version v2.0

---file comments----
*/

#ifndef _CTC_GOLDENGATE_OVERLAY_TUNNEL_CLI_H
#define _CTC_GOLDENGATE_OVERLAY_TUNNEL_CLI_H
#ifdef __cplusplus
extern "C" {
#endif

#include "sal.h"
#include "ctc_cli.h"

/**
 @brief  Initialize sdk overlay tunnel module CLIs

 @param[in/out]   cli_tree     CLI tree

 @return CTC_E_XXX

*/
extern int32
ctc_goldengate_overlay_tunnel_cli_init(void);

extern int32
ctc_goldengate_overlay_tunnel_cli_deinit(void);

#ifdef __cplusplus
}
#endif

#endif  /* _CTC_IPUC_CLI_H */


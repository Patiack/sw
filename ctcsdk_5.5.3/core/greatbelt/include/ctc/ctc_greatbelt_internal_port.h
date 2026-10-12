/**
 @file ctc_greatbelt_internal_port.h

 @author  Copyright (C) 2011 Centec Networks Inc.  All rights reserved.

 @date 2010-03-29

 @version v2.0

 The CTC internal port module provide mechanism to facilitate complex applications which not be
 supported in normal packet procedure. Depend on applications such as ipswap and macswap,
 mcast mirror or L2VPN interconvert L3VPN, the application may need iloop, eloop or both
 to satisfy requirement.
 \b
 For example, mirror to mcast destination by configure mcast nexthop on iloop port.
 Ip encapsulate tunnel header and route by new tunnel header. After add tunnel header on egress procedure,
 eloop to route internal port to reroute by outer tunnel header.
 \b
 In order to avoid modify sdk code to realize these known or unknown applications, it is
 necessary to separate internal port loop mechanism and special applications. Beside to specify
 loopback nexthop and set loopback port property, there are no change to existing sdk code
 for uncertainty.
 \b

 According to applications, there are three internal port type:
 \d Iloop internal port
 \d Eloop internal port
 \d Discard internal port
 \d Forward internal port

 \b
 \p
 The module provides APIs to support internal port:
 \p

 \d Alloc internal port by ctc_alloc_internal_port()
 \d Set internal port by ctc_set_internal_port()
 \d Free internal port by ctc_free_internal_port()

 \S ctc_internal_port.h:ctc_internal_port_type_t
 \S ctc_internal_port.h:ctc_internal_port_assign_para_t

*/

#ifndef _CTC_GREATBELT_INTERNAL_PORT_H_
#define _CTC_GREATBELT_INTERNAL_PORT_H_
#ifdef __cplusplus
extern "C" {
#endif

/****************************************************************
 *
 * Header Files
 *
 ***************************************************************/
#include "sal.h"
#include "ctc_internal_port.h"

/****************************************************************************
 *
* Function
*
*****************************************************************************/
/**
 @addtogroup internal_port INTERNAL_PORT
 @{
*/

/**
 @brief     Set internal port for special usage

 @param[in] lchip    local chip id

 @param[in] port_assign  Internal port assign parameters

 @remark    N/A

 @return    CTC_E_XXX

*/
extern int32
ctc_greatbelt_set_internal_port(uint8 lchip, ctc_internal_port_assign_para_t* port_assign);

/**
 @brief     Allocate internal port for special usage

 @param[in] lchip    local chip id

 @param[in] port_assign  Internal port assign parameters

 @remark    N/A

 @return    CTC_E_XXX

*/
extern int32
ctc_greatbelt_alloc_internal_port(uint8 lchip, ctc_internal_port_assign_para_t* port_assign);

/**
 @brief     Release internal port.

 @param[in] lchip    local chip id

 @param[in] port_assign  Internal port assign parameters

 @remark    N/A

 @return    CTC_E_XXX

*/
extern int32
ctc_greatbelt_free_internal_port(uint8 lchip, ctc_internal_port_assign_para_t* port_assign);

/**
 @brief     Initialize internal port.

 @param[in] lchip    local chip id

 @param[in] p_global_cfg  point to init global configure

 @remark    N/A

 @return    CTC_E_XXX

*/
extern int32
ctc_greatbelt_internal_port_init(uint8 lchip, void* p_global_cfg);

/**
 @brief De-Initialize internal_port module

 @param[in] lchip    local chip id

 @remark  User can de-initialize the internal_port configuration

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_internal_port_deinit(uint8 lchip);

/**@} end of @addtogroup internal_port INTERNAL_PORT  */

#ifdef __cplusplus
}
#endif

#endif


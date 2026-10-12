/**
 @file ctc_greatbelt_linkagg.h

 @author  Copyright (C) 2011 Centec Networks Inc.  All rights reserved.

 @date 2009-10-19

 @version v2.0


 Link Aggregation is a method by which many Ethernet ports can be bundled together to form a linkagg. The linkagg
 is regarded as one logical link, and is useful when a high bandwidth and/ or redundancy between switches are required.
 All ports in a linkagg must be configured with the same speed and the transmission mode for each linkagg member port
 must be set to full-duplex.
 \p

 Linkagg can provide a number of valuable features:
 1.Incremental bandwidth based on demand.
 2.Link redundancy. In the case of a linkagg port failure, the port with the failure is removed from the linkagg group.
 3.High-bandwidth load balancing.
 \p

 GreatBelt supports up to 64 link aggregation group. In one group, member number can be any one of 1-16, 32, 64, 128 and 256.
 But totally, the memberNum * groupNum is up to 1024. But if you want to use DLB function, the group number you can configure
 is 32, which means 64 group mode is not support when DLB support.
 \p

 Dynamic Load Balance(DLB) is used to choose the  minimum load of bandwidth among linkagg members as the egress. When using this
 function must configure correct timer for DLB according hardware and request, which will be done in sys_greatbelt_linkagg_init() interface.
 By default, SDK configure 5s as the timer interval for DLB switch.
 Notice: When linkagg is in DLB mode, the member number per group is 8, regardless of group mode configured.
 \p

 Port Selection Criteria (PSC) are used to spread the traffic across the linkagg's member ports by different hash value,
 hash key can be macsa, macda, vlan, ipsa, ipda and others.
 \p

*/

#ifndef _CTC_GREATBELT_LINKAGG_H
#define _CTC_GREATBELT_LINKAGG_H
#ifdef __cplusplus
extern "C" {
#endif

/***************************************************************
 *
 * Header Files
 *
 ***************************************************************/

#include "ctc_linkagg.h"

/***************************************************************
 *
 *  Defines and Macros
 *
 ***************************************************************/
/***************************************************************
 *
 *  Functions
 *
 ***************************************************************/
/**
 @addtogroup linkagg LINKAGG
 @{
*/

/**
 @brief The function is to init the linkagg module

 @param[in] lchip    local chip id

 @param[in]  linkagg_global_cfg linkagg global config information

 @remark  Initialize the linkagg module, including global variable, soft table, asic table, etc.
                This function has to be called before any other linkagg functions.
                LinkAgg group num can set by user according to the parameter linkagg_global_cfg
\p
                group num can be change :
\d              CTC_LINKAGG_MODE_4:  4 groups
\d              CTC_LINKAGG_MODE_8:  8 groups
\d              CTC_LINKAGG_MODE_16: 16 groups
\d              CTC_LINKAGG_MODE_32: 32 groups
\d              CTC_LINKAGG_MODE_64: 64 groups

\p
                GreatBelts supports up to 64 link aggregation group. In one group, member number can be any one of
                1-16, 32, 64, 128 and 256. But totally, the memberNum * groupNum is up to 1024.
                When linkagg_global_cfg is NULL, linkagg will use default global config to initialize sdk.
                Notice: When config CTC_LINKAGG_MODE_64,  dlb is not support in this mode.
                If configure dlb mode, member number is 8 regardless of group mode configured.
\p
                By default:
                LinkAgg group num is 32 (CTC_LINKAGG_MODE_32).

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_linkagg_init(uint8 lchip, void* linkagg_global_cfg);

/**
 @brief De-Initialize linkagg module

 @param[in] lchip    local chip id

 @remark  User can de-initialize the linkagg configuration

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_linkagg_deinit(uint8 lchip);

/**
 @brief The function is to create one linkagg

 @param[in] lchip    local chip id

 @param[in] p_linkagg_grp linkagg group information

 @remark  Create one linkagg with linkagg mode and group id.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_linkagg_create(uint8 lchip, ctc_linkagg_group_t* p_linkagg_grp);

/**
 @brief The function is to delete one linkagg

 @param[in] lchip    local chip id

 @param[in] tid the linkagg id which to be removed

 @remark  Delete one linkagg with linkagg id is the parameter tid.
                Removes all current linkagg member ports from the linkagg group and marks
                the linkagg ID as invalid (available for reuse).

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_linkagg_destroy(uint8 lchip, uint8 tid);

/**
 @brief The function is to add a port to linkagg

 @param[in] lchip    local chip id

 @param[in] tid the linkagg id wanted to operate

 @param[in] gport global port of the member port which to added

 @remark  Add a member port to a exist linkagg group with the given tid and gport.
\p
          The specified linkagg group must be one returned from ctc_linkagg_create().
\d        If the port is already exist in linkagg group, add member port operation will fail.
\d        If the linkagg already has members, they will be retained.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_linkagg_add_port(uint8 lchip, uint8 tid, uint32 gport);

/**
 @brief The function is to remove the port from linkagg

 @param[in] lchip    local chip id

 @param[in] tid the linkagg id wanted to operate

 @param[in] gport global port of the member port which to be removed

 @remark  Remove a member port from a exist linkagg group with the given tid and gport.
                The specified linkagg group must be one returned from ctc_linkagg_create().
\p
                The port wanted to remove must be a member of the linkagg.
\d        If the port is not exist in linkagg group, add member port operation will fail.
\d        If the linkagg has members, they will be remove.


 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_linkagg_remove_port(uint8 lchip, uint8 tid, uint32 gport);

/**
 @brief The function is to replace ports for linkagg

 @param[in] lchip    local chip id

 @param[in] tid the linkagg id wanted to operate

 @param[in] gport global port of the member ports which wanted to replace

 @param[in] mem_num number of the member ports wanted to replace

 @remark  Replace linkagg member port.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_linkagg_replace_ports(uint8 lchip, uint8 tid, uint32* gport, uint16 mem_num);

/**
 @brief The function is to get the a local member port of linkagg

 @param[in] lchip_id    local chip id

 @param[in] tid the linkagg id wanted to operate

 @param[out] p_gport the pointer point to the local member port, will be NULL if none

 @param[out] local_cnt number of local port

 @remark  Get a local member port of a exist linkagg group with the given tid.
                The specified linkagg group must be one returned from ctc_linkagg_create().

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_linkagg_get_1st_local_port(uint8 lchip_id, uint8 tid, uint32* p_gport, uint16* local_cnt);

/**
 @brief The function is to get the member ports from a linkagg

 @param[in] lchip    local chip id

 @param[in] tid the linkagg id wanted to operate

 @param[out] p_gports a global member ports list of linkagg group

 @param[out] cnt       the number of linkagg member

 @remark  Get the member ports of a exist linkagg group with the given tid.
                The specified linkagg group must be one returned from ctc_linkagg_create().
                The member ports  list of linkagg group is stored in array pointer p_gports.
                The p_gports memory must be allocated by user and the array num is 1024/groupNum.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_linkagg_get_member_ports(uint8 lchip, uint8 tid, uint32* p_gports, uint16* cnt);

/**
 @brief The function is to set member of linkagg hash key

 @param[in] lchip    local chip id

 @param[in] psc the port selection criteria of linkagg hash

 @remark  Set the global linkagg's Port Selection Criteria (PSC), members of linkagg hash key included.
                Assigns a particular load balancing hash algorithm.


 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_linkagg_set_psc(uint8 lchip, ctc_linkagg_psc_t* psc);

/**
 @brief The function is to get member of linkagg hash key

 @param[in] lchip    local chip id

 @param[out] psc the port selection criteria of linkagg hash

 @remark  Get the global linkagg's Port Selection Criteria (PSC), members of linkagg hash key included.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_linkagg_get_psc(uint8 lchip, ctc_linkagg_psc_t* psc);

/**
 @brief The function is to get max member num of linkagg group

 @param[in] lchip    local chip id

 @param[out] max_num the max member num of linkagg group

 @remark  Get the max member num of linkagg group

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_linkagg_get_max_mem_num(uint8 lchip, uint16* max_num);

/**@} end of @addgroup linkagg */

#ifdef __cplusplus
}
#endif

#endif


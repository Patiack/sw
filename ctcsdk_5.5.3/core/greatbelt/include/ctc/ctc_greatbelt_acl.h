/**
    @file ctc_greatbelt_acl.h

    @author  Copyright (C) 2011 Centec Networks Inc.  All rights reserved.

    @date 2012-10-19

    @version v2.0

    The CTC ACL module provides access to the Access control list features. ACL
    can be used to classify packets based on predefined protocol fields from Layer 2
    through Layer 4.
\b
    Based on these classifications, various actions can be taken, such as:
\p
\d        Discarding the packet
\d        Sending the packet to the CPU
\d        Sending the packet to a mirror port
\d        Sending the packet on a different CoS queue
\d        Changing the ToS, VLAN or other fields
\b
    There are 4 acl tables: Mac, Ipv4, MPLS, Ipv6.
    At most, Mac has 4 blocks, Ipv4 and MPLS share 4 blocks, Ipv6 has 2 blocks.
    At default, asic_merge_mac_ip is set, which means l2 packets and ipv4 packets
    will all go against ipv4-entry. In this case, the entry number of  Mac's each
    block should be set to zero.
\b
    This default configuration can be modified through ctc_acl_init. In this routine,
    users can set:
\p
\d    asic_merge_mac_ip   -- if set, l2 packets will also go against Ipv4-entry.
                             Otherwise, l2 packets will go against Mac-entry.
\d    ftm_merge_mac_ip    -- if set, Mac and Ipv4(Mpls) will share same blocks.
\d    mac_entry_num[4]    -- entry number of each Mac block.
\d    ipv4_entry_num[4]   -- entry number of each Ipv4(Mpls) block.
\d    ipv6_entry_num[2]   -- entry number of each Ipv6 block.

\b
    Group-id:        This represents a acl group which has many acl entries. One group can have Mac Ipv4 Ipv6 mixed.
\p
    Group-priority:  This represents a acl block, since Mac/Ipv4(Mpls)/Ipv6 has
                     its own blocks, some priority may not be suited for some entry-type.
                     Ex. a group with priority = 2 (priority starts at 0), then
                     a Ipv6-entry ( 2 blocks at most ) can not add to this group. An error
                     will be returned.
                     The priority IS block_id, the naming comes from that the actions
                     from block_id=0 take higher priority than block_id=1 when these block
                     have conflict actions.
\p
    Entry-id:        This represents a acl entry, and is a global concept. In other words,
                     the entry-id is unique for all acl groups.
\p
    Entry-priority:  This represents a entry priority within a block.
                     The bigger the higher priority. Different entry-type has
                     nothing to compare.
\b
                     Ex. group id=0, group prio=0. There are 3 entries:
\p
\d                        entry id=0, entry prio=10, type= mac
\d                        entry id=1, entry prio=20, type= mac
\d                        entry id=2, entry prio=20, type= ipv4
\d                        The priority of eid1 > eid0.
\d                        The priority of eid2 has nothing to do with eid0.

*/

#ifndef _CTC_GREATBELT_ACL_H
#define _CTC_GREATBELT_ACL_H
#ifdef __cplusplus
extern "C" {
#endif

/***************************************************************
 *
 * Header Files
 *
 ***************************************************************/
#include "ctc_const.h"
#include "ctc_parser.h"
#include "ctc_acl.h"
#include "ctc_stats.h"

/**
 @addtogroup acl ACL
 @{
*/

/**
 @brief     Init acl module.

 @param[in] lchip    local chip id

 @param[in] acl_global_cfg      Init parameter

 @remark    Init acl module.

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_acl_init(uint8 lchip, ctc_acl_global_cfg_t* acl_global_cfg);

/**
 @brief De-Initialize acl module

 @param[in] lchip    local chip id

 @remark  User can de-initialize the acl configuration

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_acl_deinit(uint8 lchip);

/**
 @brief     Create a acl group.

 @param[in] lchip    local chip id

 @param[in] group_id      ACL group ID
 @param[in] group_info    ACL group info

 @remark    Creates a acl group with a specified priority.

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_acl_create_group(uint8 lchip, uint32 group_id, ctc_acl_group_info_t* group_info);

/**
 @brief     Destroy a acl group.

 @param[in] lchip    local chip id

 @param[in] group_id      ACL group ID

 @remark    Destroy a acl group.
            All entries that uses this group must have been removed before calling this routine,
            or the call will fail.

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_acl_destroy_group(uint8 lchip, uint32 group_id);

/**
 @brief     Install all entries of a acl group into the hardware tables.

 @param[in] lchip    local chip id

 @param[in] group_id      ACL group ID
 @param[in] group_info    ACL group info

 @remark    Installs a group of entries into the hardware tables.
            Will silently reinstall entries already in the hardware tables.
            If the group is empty, nothing is installed but the group_info is saved.
            If group_info is null, group_info is the same as previoust install_group or it's a hash group.
            Group Info care not type, gchip, priority.
            Only after the group_info saved, ctc_acl_install_entry() will work.



 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_acl_install_group(uint8 lchip, uint32 group_id, ctc_acl_group_info_t* group_info);

/**
 @brief     Uninstall all entries of a acl group from the hardware table.

 @param[in] lchip    local chip id

 @param[in] group_id      ACL group ID

 @remark    Uninstall all entries of a group from the hardware tables.
            Will silently ignore entries that are not in the hardware tables.
            This does not destroy the group or remove its entries,
            it only uninstalls them from the hardware tables.
            Remove an entry by using  ctc_acl_remove_entry(),
            and destroy a group by using ctc_acl_destroy_group().

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_acl_uninstall_group(uint8 lchip, uint32 group_id);

/**
 @brief      Get group_info of a acl group.

 @param[in] lchip    local chip id

 @param[in]  group_id            ACL group ID
 @param[out] group_info          ACL group information

 @remark     Get group_info of a acl group.

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_acl_get_group_info(uint8 lchip, uint32 group_id, ctc_acl_group_info_t* group_info);

/**
 @brief     Add an acl entry to a specified acl group.

 @param[in] lchip    local chip id

 @param[in] group_id      ACL group ID
 @param[in] acl_entry     ACL entry info.

 @remark    Add an acl entry to a specified acl group.
            The entry contains: key, action, entry_id, priority, refer to ctc_acl_entry_t.
            Remove an entry by using ctc_acl_remove_entry().
            It only add entry to the software table, install to hardware table
            by using ctc_acl_install_entry().
            PS. vlan edit is supported only for ingress acl.
            If acl entry is hash entry, you must install this entry immediately when you add a entry.

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_acl_add_entry(uint8 lchip, uint32 group_id, ctc_acl_entry_t* acl_entry);

/**
 @brief     Remove an acl entry.

 @param[in] lchip    local chip id

 @param[in] entry_id      ACL entry ID

 @remark    Remove an acl entry from software table,
            uninstall from hardware table by calling ctc_acl_uninstall_entry().

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_acl_remove_entry(uint8 lchip, uint32 entry_id);

/**
 @brief     Installs an entry into the hardware tables.

 @param[in] lchip    local chip id

 @param[in] entry_id      ACL entry ID

 @remark    Installs an entry into the hardware tables.
            Must add an entry to software table first, by calling ctc_acl_add_entry().

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_acl_install_entry(uint8 lchip, uint32 entry_id);

/**
 @brief     Uninstall an acl entry from the hardware tables.

 @param[in] lchip    local chip id

 @param[in] entry_id      ACL entry ID

 @remark    Uninstall an acl entry from the hardware tables.
            The object of this entry is still in software table, thus can install again.

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_acl_uninstall_entry(uint8 lchip, uint32 entry_id);

/**
 @brief     Remove all acl entries of a acl group.

 @param[in] lchip    local chip id

 @param[in] group_id      ACL group ID

 @remark    Remove all acl entries of a acl group from software table, must uninstall all
            entries first.
            Uninstall one entry from hardware table by using ctc_acl_uninstall_entry().
            Uninstall a whole group from hardware table by using ctc_acl_uninstall_group().

 @return    CTC_E_XXX
*/
extern int32
ctc_greatbelt_acl_remove_all_entry(uint8 lchip, uint32 group_id);

/**
 @brief     set an entry priority.

 @param[in] lchip    local chip id

 @param[in] entry_id      ACL entry ID

 @param[in] priority      ACL entry priority

 @remark    Sets the priority of an entry.
            Entries with higher priority values take precedence over entries with lower values.
            Since TCAM lookups start at low indexes, precedence within a physical block is the
            reverse of this.
            The effect of this is that entries with the greatest priority will have the lowest TCAM index.
            Entries should have only positive priority values. If 2 entry priority set equally,
            the latter is lower than former.
            Currently there are 1 predefined cases:
            FPA_PRIORITY_LOWEST  - Lowest possible priority (ALSO is the default value)

@return CTC_E_XXX
*/
extern int32
ctc_greatbelt_acl_set_entry_priority(uint8 lchip, uint32 entry_id, uint32 priority);

/**
 @brief      Get an array of entry IDs in use in a group

 @param[in] lchip    local chip id

 @param[in|out]  query     Pointer to query structure.

 @remark     Fills an array with the entry IDs for the specified group. This should first be called with an  entry_size  of 0
to get the number of entries to be returned, so that an appropriately-sized array can be allocated.

@return CTC_E_XXX
*/
extern int32
ctc_greatbelt_acl_get_multi_entry(uint8 lchip, ctc_acl_query_t* query);

/**
 @brief      Update acl action

 @param[in] lchip    local chip id

 @param[in]  entry_id     ACL entry need to update action
 @param[in]  action       New action to overwrite

 @remark     Update an ace's action, this action will overwrite the old ace's action.
             If the entry is installed, new action will be installed too.
             PS. vlan edit is supported only for ingress acl.

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_acl_update_action(uint8 lchip, uint32 entry_id, ctc_acl_action_t* action);

/**
 @brief      Create a copy of an existing acl entry

 @param[in] lchip    local chip id

 @param[in]  copy_entry     Pointer to copy_entry struct

 @remark     Creates a copy of an existing acl entry to a sepcific group.
             The dst entry's priority is source entry's  priority.

 @return CTC_E_XXX
*/

extern int32
ctc_greatbelt_acl_copy_entry(uint8 lchip, ctc_acl_copy_entry_t* copy_entry);

/**@} end of @addgroup   */

#ifdef __cplusplus
}
#endif

#endif


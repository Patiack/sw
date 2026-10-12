#if 1
/**
    @file ctc_greatbelt_scl.h

    @author  Copyright (C) 2011 Centec Networks Inc.  All rights reserved.

    @date 2013-02-21

    @version v2.0

Overview refer acl module.
*/

#ifndef _CTC_GREATBELT_SCL_H
#define _CTC_GREATBELT_SCL_H
#ifdef __cplusplus
extern "C" {
#endif

/***************************************************************
 *
 * Header Files
 *
 ***************************************************************/
#include "ctc_const.h"
#include "ctc_scl.h"
#include "ctc_stats.h"

/**
 @addtogroup scl SCL
 @{
*/

/**
 @brief     Init scl module.

 @param[in] lchip    local chip id

 @param[in] scl_global_cfg      Init parameter

 @remark    Init scl module.

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_scl_init(uint8 lchip, void* scl_global_cfg);

/**
 @brief De-Initialize scl module

 @param[in] lchip    local chip id

 @remark  User can de-initialize the scl configuration

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_scl_deinit(uint8 lchip);

/**
 @brief     Create a scl group.

 @param[in] lchip    local chip id

 @param[in] group_id      SCL group ID

 @param[in] group_info      SCL group information

 @remark    Creates a scl group with a specified priority.

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_scl_create_group(uint8 lchip, uint32 group_id, ctc_scl_group_info_t* group_info);

/**
 @brief     Destroy a scl group.

 @param[in] lchip    local chip id

 @param[in] group_id      SCL group ID

 @remark    Destroy a scl group.
            All entries that uses this group must have been removed before calling this routine,
            or the call will fail.

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_scl_destroy_group(uint8 lchip, uint32 group_id);

/**
 @brief     Install all entries of a scl group into the hardware tables.

 @param[in] lchip    local chip id

 @param[in] group_id      SCL group ID
 @param[in] group_info    SCL group info

 @remark    Installs a group of entries into the hardware tables.
            Will silently reinstall entries already in the hardware tables.
            If the group is empty, nothing is installed but the group_info is saved.
            Only after the group_info saved, ctc_scl_install_entry() will work.
            If group info is NULL, just install entries that are only in software table.



 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_scl_install_group(uint8 lchip, uint32 group_id, ctc_scl_group_info_t* group_info);

/**
 @brief     Uninstall all entries of a scl group from the hardware table.

 @param[in] lchip    local chip id

 @param[in] group_id      SCL group ID

 @remark    Uninstall all entries of a group from the hardware tables.
            Will silently ignore entries that are not in the hardware tables.
            This does not destroy the group or remove its entries,
            it only uninstalls them from the hardware tables.
            Remove an entry by using  ctc_scl_remove_entry(),
            and destroy a group by using ctc_scl_destroy_group().

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_scl_uninstall_group(uint8 lchip, uint32 group_id);

/**
 @brief      Get group_info of a scl group.

 @param[in] lchip    local chip id

 @param[in]  group_id            SCL group ID
 @param[out] group_info          SCL group information

 @remark     Get group_info of a scl group.

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_scl_get_group_info(uint8 lchip, uint32 group_id, ctc_scl_group_info_t* group_info);

/**
 @brief     Add an scl entry to a specified scl group.

 @param[in] lchip    local chip id

 @param[in] group_id      SCL group ID
 @param[in] scl_entry     SCL entry info.

 @remark    Add an scl entry to a specified scl group.
            The entry cantains: key, action, entry_id, priority, refer to ctc_scl_entry_t.
            Remove an entry by using ctc_scl_remove_entry().
            It only add entry to the software table, install to hardware table
            by using ctc_scl_install_entry().
            If scl entry is hash entry, you must install this entry immediately when you add a entry.

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_scl_add_entry(uint8 lchip, uint32 group_id, ctc_scl_entry_t* scl_entry);

/**
 @brief     Remove an scl entry.

 @param[in] lchip    local chip id

 @param[in] entry_id      SCL entry ID

 @remark    Remove an scl entry from software table,
            uninstall from hardware table by calling ctc_scl_uninstall_entry().

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_scl_remove_entry(uint8 lchip, uint32 entry_id);

/**
 @brief     Installs an entry into the hardware tables.

 @param[in] lchip    local chip id

 @param[in] entry_id      SCL entry ID

 @remark    Installs an entry into the hardware tables.
            Must add an entry to software tablse first, by calling ctc_scl_add_entry().

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_scl_install_entry(uint8 lchip, uint32 entry_id);

/**
 @brief     Uninstall an scl entry from the hardware tables.

 @param[in] lchip    local chip id

 @param[in] entry_id      SCL entry ID

 @remark    Uninstall an scl entry from the hardware tables.
            The object of this entry is still in software table, thus can install again.

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_scl_uninstall_entry(uint8 lchip, uint32 entry_id);

/**
 @brief     Remove all scl entries of a scl group.

 @param[in] lchip    local chip id

 @param[in] group_id      SCL group ID

 @remark    Remove all scl entries of a scl group from software table, must uninstall all
            entries first.
            Uninstall one entry from hardware table by using ctc_scl_uninstall_entry().
            Uninstall a whole group from hardware table by using ctc_scl_uninstall_group().

 @return    CTC_E_XXX
*/
extern int32
ctc_greatbelt_scl_remove_all_entry(uint8 lchip, uint32 group_id);

/**
 @brief     set an entry priority.

 @param[in] lchip    local chip id

 @param[in] entry_id      SCL entry ID

 @param[in] priority      SCL entry priority

 @remark    Sets the priority of an entry.
            Entries with higher priority values take precedence over entries with lower values.
            Since TCAM lookups start at low indexes, precedence within a physical block is the
            reverse of this.
            The effect of this is that entries with the greatest priority will have the lowest TCAM index.
            Entries should have only positive priority values. If 2 entry priority set equally,
            the latter is lower than former.
            Currently there are 1 predefined cases:
            FPA_PRIORITY_LOWEST  - Lowest possible priority (ALSO is the default value)
            Hash entry cannot set priority.

@return CTC_E_XXX
*/
extern int32
ctc_greatbelt_scl_set_entry_priority(uint8 lchip, uint32 entry_id, uint32 priority);

/**
 @brief      Get an array of entry IDs in use in a group

 @param[in] lchip    local chip id

 @param[in|out]  query     Pointer to query structure.

 @remark     Fills an array with the entry IDs for the specified group. This should first be called with an  entry_size  of 0
to get the number of entries to be returned, so that an appropriately-sized array can be allocated.

@return CTC_E_XXX
*/
extern int32
ctc_greatbelt_scl_get_multi_entry(uint8 lchip, ctc_scl_query_t* query);

/**
 @brief      Update scl action

 @param[in] lchip    local chip id

 @param[in]  entry_id     SCL entry need to update action
 @param[in]  action       New action to overwrite

 @remark     Update an ace's action, this action will overwrite the old ace's action.
             If the entry is installed, new action will be installed too.

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_scl_update_action(uint8 lchip, uint32 entry_id, ctc_scl_action_t* action);

/**
 @brief      Set scl default action

 @param[in] lchip    local chip id

 @param[in]  def_action       New default action

 @remark     Update scl action on specific gport. This action will overwrite the old action, and it
             will be installed to hardware immediately.

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_scl_set_default_action(uint8 lchip, ctc_scl_default_action_t* def_action);

/**
 @brief      Create a copy of an existing scl entry

 @param[in] lchip    local chip id

 @param[in]  copy_entry     Pointer to copy_entry struct

 @remark     Creates a copy of an existing scl entry to a sepcific group.
             The dst entry's priority is a default priority.

 @return CTC_E_XXX
*/

extern int32
ctc_greatbelt_scl_copy_entry(uint8 lchip, ctc_scl_copy_entry_t* copy_entry);

/**@} end of @addgroup   */

#ifdef __cplusplus
}
#endif

#endif

#endif

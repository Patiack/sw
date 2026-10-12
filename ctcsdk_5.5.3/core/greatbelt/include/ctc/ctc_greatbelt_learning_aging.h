/**
 @file ctc_greatbelt_learning_aging.h

 @date 2010-3-16

 @version v2.0

MAC Learning \p
MAC learning will use Fid + MacSa as lookup key for regular bridge and globalSrcPort
is in the associated data. To support more flexible advanced features, CTC5160 provides
an option to use Fid + MacSa as the key and logicSrcPort is in the associated data.\p
CTC5160 support two learning mode:\p
 1)Hardware learning \p
    Hardware learning is controlled by both MAC and Port. Only if both DsMac.fastLearningEn
    and DsSrcPort.fastLeaningEn are set, hardware learning will be enabled.\p

 2)Hardware assist for software learning.\p

The whole learning process can be divided into two stages.\p
1) Decide if the MAC should be learnt, which is independent of whether hardware or software learning is used.
2) When Learn the MAC
  (1)For hardware learning, chip will directly write the data into DsMacHashKey. The associated DsMac should be programmed
     in advanced. Per globaSrcPort or logicSrcPort there is an adIndex indexed into DsMac table.
  (2)For hardware assisted software learning, chip will write lots of necessary information, such as MACSA, fid, port into
      a 16©\depth learning cache, when the number in the learning cache exceeds a threshold, CPU will be interrupted. Then CPU
      needs to read the learning cache. Generally, the threshold should be set to 0 so that chip will always notify CPU for
      each new MAC. If cache is full, CTC5160 optionally can send the packet to CPU through packet path.

Aging \p
CTC5160 Aging table contains three different types:DsUserId(SCL)/DsIpDa(IPMC))/DsMac(L2Uc/L2MC).
The same as MAC learning, both hardware assisted software aging and hardware aging are supported.
DsUserId/DsIpDa corresponding Key will use hardware assisted software aging; For DsMac,if DsMAC
corresponding Key stored in TCAM,it must be hardware assisted software aging; else it can use
Hardware aging by DsMac.fastLearningEn.\p

CTC5160 supports Aging via DsAging table. Each entry of DsAging table is 32 bits. Each MAC entry has
a corresponding bit in this table. This bit is used to indicate if the entry should be aged, which
agingStatus is.Global aging timer is running infinitely until it¡¯s stopped by software. In each
aging period, the whole DsAging table will be scanned and each agingStatus bit will be checked.
In each aging scan period, the each agingStatus bit will be reset to 0 if it¡¯s 1 in this round.
So in next round, if this bit is still 0:\p

1) Hardware aging:\p
   The corresponding entry will be removed and and pushed into FibLearnFifoInfo_t table is used
   to notify the CPU by DMA mechanism;\p
2) Hardware assisted software aging:\p
   The corresponding Aghing Ptr will be pushed into 16©\depth Aging Fifo table, when the number
   in the Aging Fifoexceeds a threshold,CPU will be interrupted. Then CPU  needs to read the
   Aging Fifo,By default, the threshold should be set to 1 so that chip will always notify CPU
   for each new entry.\p
*/

#ifndef CTC_GREATBELT_LEARNING_AGING_H_
#define CTC_GREATBELT_LEARNING_AGING_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "sal.h"
#include "ctc_learning_aging.h"

/**
 @addtogroup learning_aging  LEARNING_AGING
 @{
*/

/**
 @brief This function is to set the learning action and cache threshold

 @param[in] lchip    local chip id

 @param[in] p_learning_action    learning action and cache threshold

 @remark  Set Learning action and learning cache threshold.

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_set_learning_action(uint8 lchip, ctc_learning_action_info_t* p_learning_action);

/**
 @brief This function is to get the learning action and cache threshold

 @param[in] lchip    local chip id

 @param[out] p_learning_action    learning action and cache threshold

 @remark  Get Learning action and learning cache threshold.

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_get_learning_action(uint8 lchip, ctc_learning_action_info_t* p_learning_action);

/**
 @brief This function is to get valid entry bitmap for learning cache.

 @param[in] lchip    local chip id

 @param[out] entry_vld_bitmap    learning cache entry valid bit

 @remark  Get valid entry bitmap for learning cache.

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_learning_get_cache_entry_valid_bitmap(uint8 lchip, uint16* entry_vld_bitmap);

/**
 @brief This function is to get learning cache data

 @param[in] lchip    local chip id

 @param[in] entry_vld_bitmap   16 learning cache entry valid bit

 @param[out] l2_lc             use store the cache contents

 @remark  Get learning cache information.

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_learning_read_learning_cache(uint8 lchip, uint16 entry_vld_bitmap, ctc_learning_cache_t* l2_lc);

/**
 @brief This function is clear learning cache

 @param[in] lchip    local chip id

 @param[in] entry_vld_bitmap    16 learning cache entry valid bit

 @remark  Clear learning cache.

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_learning_clear_learning_cache(uint8 lchip, uint16 entry_vld_bitmap);

/**
 @brief This function is to set the aging properties

 @param[in] lchip    local chip id

 @param[in] tbl_type      aging type type
 @param[in] aging_prop    aging property type
 @param[in] value         aging action value

 @remark  Set the aging properties

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_aging_set_property(uint8 lchip, ctc_aging_table_type_t  tbl_type, ctc_aging_prop_t aging_prop, uint32 value);

/**
 @brief This function is to get the aging properties

 @param[in] lchip    local chip id

 @param[in] tbl_type      aging type type
 @param[in] aging_prop    aging property type
 @param[out] value         point to aging action value

 @remark  Get the aging properties

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_aging_get_property(uint8 lchip, ctc_aging_table_type_t  tbl_type, ctc_aging_prop_t aging_prop, uint32* value);

/**
 @brief This function is to read aging fifo data, aging isr function

 @param[in] lchip    local chip id

 @param[out]   fifo_info         use to store those read aging fifo data

 @remark  Get the aging Fifo information


 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_aging_read_aging_fifo(uint8 lchip, ctc_aging_fifo_info_t* fifo_info);

/**
 @brief This function is to initialize the learning and aging modules

 @param[in] lchip    local chip id

 @param[in]     global_cfg   point to global configure

 @remark  Initialize the learning and aging modules.

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_learning_aging_init(uint8 lchip, void* global_cfg);

/**
 @brief De-Initialize learning_aging module

 @param[in] lchip    local chip id

 @remark  User can de-initialize the learning_aging configuration

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_learning_aging_deinit(uint8 lchip);

/**@} end of @addtogroup learning_aging  LEARNING_AGING*/

#ifdef __cplusplus
}
#endif

#endif


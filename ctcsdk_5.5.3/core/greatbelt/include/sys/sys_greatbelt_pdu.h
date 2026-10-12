/**
 @file sys_greatbelt_pdu.h

 @date 2009-12-31

 @version v2.0

---file comments----
*/

#ifndef _SYS_GREATBELT_PDU_H
#define _SYS_GREATBELT_PDU_H
#ifdef __cplusplus
extern "C" {
#endif

#include "sal.h"
#include "ctc_linklist.h"
#include "ctc_const.h"
#include "ctc_pdu.h"
#include "ctc_parser.h"
#include "ctc_debug.h"

/**********************************************************************************
                        Defines and Macros
***********************************************************************************/

#define SYS_PDU_DBG_OUT(level, FMT, ...) \
    do { \
        CTC_DEBUG_OUT(pdu, pdu, PDU_SYS, level, FMT, ##__VA_ARGS__); \
    } while (0);

#define SYS_PDU_DBG_PARAM(FMT, ...) SYS_PDU_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, FMT, ##__VA_ARGS__)
#define SYS_PDU_DBG_INFO(FMT, ...) SYS_PDU_DBG_OUT(CTC_DEBUG_LEVEL_INFO, FMT, ##__VA_ARGS__)

#define SYS_PDU_FORM_MAC(mac, byte0, byte1, byte2, byte3, byte4, byte5)      \
    {                                                                            \
        mac = byte0;                                                             \
        mac = (mac << 8) | byte1;                                                \
        mac = (mac << 8) | byte2;                                                \
        mac = (mac << 8) | byte3;                                                \
        mac = (mac << 8) | byte4;                                                \
        mac = (mac << 8) | byte5;                                                \
    }

#define SYS_PDU_DBG_DUMP(FMT, ...)                                             \
    {                                                                               \
        CTC_DEBUG_OUT(pdu, pdu, PDU_SYS, CTC_DEBUG_LEVEL_DUMP, FMT, ##__VA_ARGS__); \
    }

#define SYS_PDU_L2PDU_DUMP(COLUMN)                                                                  \
    {                                                                                               \
        CTC_DEBUG_OUT(pdu, pdu, PDU_SYS, CTC_DEBUG_LEVEL_DUMP, "%s   %s   %s   %s   %s\n",\
                      COLUMN[0], COLUMN[1], COLUMN[2], COLUMN[3], COLUMN[4]); \
    }

#define SYS_PDU_L3PDU_DUMP(COLUMN)                                                                  \
    {                                                                                               \
        CTC_DEBUG_OUT(pdu, pdu, PDU_SYS, CTC_DEBUG_LEVEL_DUMP, "%s   %s   %s   %s   %s   %s\n",\
                      COLUMN[0], COLUMN[1], COLUMN[2], COLUMN[3], COLUMN[4], COLUMN[5]); \
    }

#define SYS_L2PDU_PER_PORT_ACTION_INDEX_RSV_MACDA_TO_CPU    63
#define MIN_SYS_L2PDU_PER_PORT_ACTION_INDEX  0
#define MAX_SYS_L2PDU_PER_PORT_ACTION_INDEX  30   /*last action index reserved for pause identify*/
#define MAX_SYS_L2PDU_USER_DEFINE_ACTION_INDEX 53

#define SYS_L3PDU_PER_L3IF_ACTION_INDEX_RSV_IPDA_TO_CPU     63
#define MIN_SYS_L3PDU_PER_L3IF_ACTION_INDEX  0
#define MAX_SYS_L3PDU_PER_L3IF_ACTION_INDEX  31

/**
 @brief  Classify layer2 pdu based on macda,macda-low24 bit, layer2 header protocol
*/
extern int32
sys_greatbelt_l2pdu_classify_l2pdu(uint8 lchip, ctc_pdu_l2pdu_type_t l2pdu_type, uint8 index,
                                   ctc_pdu_l2pdu_key_t* key);
/**
 @brief  Set layer2 pdu global property
*/
extern int32
sys_greatbelt_l2pdu_set_global_action(uint8 lchip, ctc_pdu_l2pdu_type_t l2pdu_type, uint8 index,
                                      ctc_pdu_global_l2pdu_action_t* action);

extern int32
sys_greatbelt_l2pdu_get_global_action(uint8 lchip, ctc_pdu_l2pdu_type_t l2pdu_type, uint8 index,
                                      ctc_pdu_global_l2pdu_action_t* action);
extern int32
sys_greatbelt_l2pdu_get_classified_key(uint8 lchip, ctc_pdu_l2pdu_type_t l2pdu_type, uint8 index,
                                       ctc_pdu_l2pdu_key_t* key);
extern int32
sys_greatbelt_pdu_show_l2pdu_cfg(uint8 lchip);

extern int32
sys_greatbelt_l2pdu_show_port_action(uint8 lchip, uint16 gport);

extern int32
sys_greatbelt_l3pdu_get_classified_key(uint8 lchip, ctc_pdu_l3pdu_type_t l3pdu_type, uint8 index,
                                       ctc_pdu_l3pdu_key_t* key);
extern int32
sys_greatbelt_l3pdu_get_global_action(uint8 lchip, ctc_pdu_l3pdu_type_t l3pdu_type, uint8 index,
                                      ctc_pdu_global_l3pdu_action_t* action);

/**
 @brief  Per port control layer2 pdu action
*/
extern int32
sys_greatbelt_l2pdu_set_port_action(uint8 lchip, uint16 gport, uint8 action_index,
                                    ctc_pdu_port_l2pdu_action_t action);
extern int32
sys_greatbelt_l2pdu_get_port_action(uint8 lchip, uint16 gport, uint8 action_index,
                                    ctc_pdu_port_l2pdu_action_t* action);
/**
 @brief  Classify layer3 pdu based on layer3 header protocol, layer4 dest port
*/
extern int32
sys_greatbelt_l3pdu_classify_l3pdu(uint8 lchip, ctc_pdu_l3pdu_type_t l3pdu_type, uint8 index,
                                   ctc_pdu_l3pdu_key_t* key);
/**
 @brief  Set layer3 pdu global property
*/
extern int32
sys_greatbelt_l3pdu_set_global_action(uint8 lchip, ctc_pdu_l3pdu_type_t l3pdu_type, uint8 index,
                                      ctc_pdu_global_l3pdu_action_t* action);
/**
 @brief  Show layer2 pdu global cfg
*/
extern int32
sys_greatbelt_pdu_show_l2pdu_cfg(uint8 lchip);

/**
 @brief  Show layer3 pdu global cfg
*/
extern int32
sys_greatbelt_pdu_show_l3pdu_cfg(uint8 lchip);

/**
 @brief init pdu module
*/
extern int32
sys_greatbelt_pdu_init(uint8 lchip);

/**
 @brief De-Initialize pdu module
*/
extern int32
sys_greatbelt_pdu_deinit(uint8 lchip);

#ifdef __cplusplus
}
#endif

#endif


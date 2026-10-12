/****************************************************************************
 * @date 2015-01-30  The file contains all parity error fatal interrupt resume realization.
 *
 * Copyright:    (c)2015 Centec Networks Inc.  All rights reserved.
 *
 * Modify History:
 * Revision:     V0.1
 * Date:         2015-01-30
 * Reason:       Create for GreatBelt v2.5.x
 ****************************************************************************/

#ifndef _DRV_GB_ECC_RECOVER_H
#define _DRV_GB_ECC_RECOVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "sal.h"

#define DRV_PE_MAX_ENTRY_WORD                32

#define  DRV_ECC_INTR_IgrCondDisProfId          0
#define  DRV_ECC_INTR_IgrPortTcMinProfId        5
#define  DRV_ECC_INTR_IgrPortTcPriMap           8
#define  DRV_ECC_INTR_IgrPortTcThrdProfId       3
#define  DRV_ECC_INTR_IgrPortTcThrdProfile      1
#define  DRV_ECC_INTR_IgrPortThrdProfile        2
#define  DRV_ECC_INTR_IgrPriToTcMap             9

#define  DRV_ECC_INTR_Edram0                   17
#define  DRV_ECC_INTR_Edram1                   18
#define  DRV_ECC_INTR_Edram2                   19
#define  DRV_ECC_INTR_Edram3                   20
#define  DRV_ECC_INTR_Edram4                   21
#define  DRV_ECC_INTR_Edram5                   22
#define  DRV_ECC_INTR_Edram6                   14
#define  DRV_ECC_INTR_Edram7                   16
#define  DRV_ECC_INTR_Edram8                   15

#define  DRV_ECC_INTR_PacketHeaderEditTunnel    2

#define  DRV_ECC_INTR_Ipv6NatPrefix             1

#define  DRV_ECC_INTR_DestChannel               6
#define  DRV_ECC_INTR_DestInterface             0
#define  DRV_ECC_INTR_DestPort                  3
#define  DRV_ECC_INTR_EgressVlanRangeProfile    2
#define  DRV_ECC_INTR_EpeEditPriorityMap        7

#define  DRV_ECC_INTR_IpeFwdDsQcnRam            3

#define  DRV_ECC_INTR_PhyPort                   1

#define  DRV_ECC_INTR_MplsCtl                   0
#define  DRV_ECC_INTR_PhyPortExt                11
#define  DRV_ECC_INTR_RouterMac                 10
#define  DRV_ECC_INTR_SrcInterface              9
#define  DRV_ECC_INTR_SrcPort                   8
#define  DRV_ECC_INTR_VlanActionProfile         5
#define  DRV_ECC_INTR_VlanRangeProfile          1

#define  DRV_ECC_INTR_EcmpGroup                 4
#define  DRV_ECC_INTR_Rpf                       8
#define  DRV_ECC_INTR_SrcChannel                6
#define  DRV_ECC_INTR_IpeClassificationDscpMap  7

#define  DRV_ECC_INTR_ApsBridge                 0

#define  DRV_ECC_INTR_ChannelizeIngFc           2
#define  DRV_ECC_INTR_ChannelizeMode            0
#define  DRV_ECC_INTR_PauseTimerMemRd           5

#define  DRV_ECC_INTR_EgrResrcCtl               5
#define  DRV_ECC_INTR_HeadHashMod              22
#define  DRV_ECC_INTR_LinkAggregateMember      15
#define  DRV_ECC_INTR_LinkAggregateMemberSet   21
#define  DRV_ECC_INTR_LinkAggregateSgmacMember 13
#define  DRV_ECC_INTR_LinkAggregationPort       0
#define  DRV_ECC_INTR_QueThrdProfile           19
#define  DRV_ECC_INTR_QueueHashKey              3
#define  DRV_ECC_INTR_QueueNumGenCtl           20
#define  DRV_ECC_INTR_QueueSelectMap           17
#define  DRV_ECC_INTR_SgmacHeadHashMod          6

#define  DRV_ECC_INTR_StpState                  2
#define  DRV_ECC_INTR_Vlan                      0
#define  DRV_ECC_INTR_VlanProfile               1

#define  DRV_ECC_INTR_DsTcamAd0                 3
#define  DRV_ECC_INTR_DsTcamAd1                 4
#define  DRV_ECC_INTR_DsTcamAd2                 5
#define  DRV_ECC_INTR_DsTcamAd3                 6

#define  DRV_ECC_INTR_PolicerControl            1
#define  DRV_ECC_INTR_PolicerProfile0           3
#define  DRV_ECC_INTR_PolicerProfile1           4
#define  DRV_ECC_INTR_PolicerProfile2           5
#define  DRV_ECC_INTR_PolicerProfile3           6

#define  DRV_ECC_INTR_lpmTcamAdMem              6

#define  DRV_ECC_INTR_ChanShpProfile            9
#define  DRV_ECC_INTR_GrpShpProfile             11
#define  DRV_ECC_INTR_GrpShpWfqCtl              33
#define  DRV_ECC_INTR_QueMap                    14
#define  DRV_ECC_INTR_QueShpCtl                  8
#define  DRV_ECC_INTR_QueShpProfile             16
#define  DRV_ECC_INTR_QueShpWfqCtl              17

#define  DRV_ECC_INTR_BufRetrvExcp               0

typedef int32 (* drv_ftm_check_fn)(uint8 lchip, uint8 ram_id, uint32 ram_offset, uint8 *b_recover);

struct drv_ecc_recover_global_cfg_s
{
    drv_ftm_check_fn ftm_check_cb;
};
typedef struct drv_ecc_recover_global_cfg_s drv_ecc_recover_global_cfg_t;

struct drv_ecc_intr_param_s
{
    tbls_id_t  intr_tblid;       /**< interrupt tbl id */
    uint32*    p_bmp;            /**< bitmap of interrupt status */
    uint16     total_bit_num;    /**< total bit num */
    uint16     valid_bit_count;  /**< valid bit count */
};
typedef struct drv_ecc_intr_param_s drv_ecc_intr_param_t;

typedef int32 (* drv_ecc_event_fn)(uint8 gchip, void* p_data);

/**
 @brief The function write tbl data to ecc error recover memory for resume
*/
extern void
drv_greatbelt_ecc_recover_store(uint8 lchip, tbls_id_t tbl_id, uint32 tbl_idx, uint32* p_data);

/**
 @brief The function recover the ecc error memory with handle info
*/
extern int32
drv_greatbelt_ecc_recover_restore(uint8 lchip, drv_ecc_intr_param_t* p_intr_param);

/**
 @brief The function show parity error recover backup memory value
*/
extern int32
drv_greatbelt_ecc_recover_show_status(uint8 lchip, uint32 is_all);

/**
 @brief The function init chip's mapping memory for ecc error recover
*/
extern int32
drv_greatbelt_ecc_recover_init(uint8 lchip, drv_ecc_recover_global_cfg_t* p_cfg);

/**
 @brief The function provide user register ecc event monitor
*/
extern int32
drv_greatbelt_ecc_recover_register_event_cb(drv_ecc_event_fn cb);

#ifdef __cplusplus
}
#endif

#endif


/**
 @file sys_greatbelt_internal_port.h

 @author  Copyright (C) 2011 Centec Networks Inc.  All rights reserved.

 @date 2010-03-29

 @version v2.0

 The file defines macro, data structure, and function for internal port
*/

#ifndef _SYS_GREATBELT_INTERNAL_PORT_H_
#define _SYS_GREATBELT_INTERNAL_PORT_H_
#ifdef __cplusplus
extern "C" {
#endif

/****************************************************************
 *
 * Header Files
 *
 ***************************************************************/
#include "ctc_const.h"
#include "ctc_internal_port.h"

/*********************************************************************
 *
 * Macro
 *
 *********************************************************************/

enum sys_internal_port_type_e
 {
    SYS_INTERNAL_PORT_TYPE_MIN,
    SYS_INTERNAL_PORT_TYPE_ILOOP = SYS_INTERNAL_PORT_TYPE_MIN , /* iloop */
    SYS_INTERNAL_PORT_TYPE_CPU,
    SYS_INTERNAL_PORT_TYPE_DMA, /* dma */
    SYS_INTERNAL_PORT_TYPE_OAM, /* oam */
    SYS_INTERNAL_PORT_TYPE_INLK, /* interlaken */

    SYS_INTERNAL_PORT_TYPE_IP_TUNNEL, /* eloop */
    SYS_INTERNAL_PORT_TYPE_MIRROR,    /* iloop */
    SYS_INTERNAL_PORT_TYPE_MPLS_BFD,    /* eloop */
    SYS_INTERNAL_PORT_TYPE_PW_VCCV_BFD,  /* eloop */
    SYS_INTERNAL_PORT_TYPE_DROP,  /* discard */
    SYS_INTERNAL_PORT_TYPE_CPU_REMOTE,  /* remote cpu */
    SYS_INTERNAL_PORT_TYPE_CPU_OAM,     /* OAM to CPU */
    SYS_INTERNAL_PORT_TYPE_MAX
};
typedef enum sys_internal_port_type_e sys_internal_port_type_t;

enum sys_internal_port_dir_e
{
    SYS_INTERNAL_PORT_DIR_IGS,      /* iloop */
    SYS_INTERNAL_PORT_DIR_EGS,      /* eloop */
    SYS_INTERNAL_PORT_DIR_MAX
};
typedef enum sys_internal_port_dir_e sys_internal_port_dir_t;

struct sys_gb_inter_port_master_s
{
    uint32 is_used[SYS_INTERNAL_PORT_DIR_MAX][SYS_GB_MAX_PORT_NUM_PER_CHIP / BITS_NUM_OF_WORD];    /*bitmap for internal port*/
    uint16 inter_port[SYS_INTERNAL_PORT_TYPE_MAX];
    sal_mutex_t* mutex;
};
typedef struct sys_gb_inter_port_master_s sys_gb_inter_port_master_t;

/**************************************************************F*******
 *
 * Function
 *
 *********************************************************************/

/**
 @brief Set internal port for special usage, for example, I-Loop, E-Loop.
*/
extern int32
sys_greatbelt_internal_port_set(uint8 lchip, ctc_internal_port_assign_para_t* port_assign);

/**
 @brief Allocate internal port for special usage, for example, I-Loop, E-Loop.
*/
extern int32
sys_greatbelt_internal_port_allocate(uint8 lchip, ctc_internal_port_assign_para_t* port_assign);

/**
 @brief Release internal port.
*/
extern int32
sys_greatbelt_internal_port_release(uint8 lchip, ctc_internal_port_assign_para_t* port_assign);

extern int32
sys_greatbelt_internal_port_get_rsv_port(uint8 lchip, sys_internal_port_type_t type, uint8* lport);

/**
 @brief Internal port initialization.
*/
extern int32
sys_greatbelt_internal_port_init(uint8 lchip);

/**
 @brief De-Initialize internal_port module
*/
extern int32
sys_greatbelt_internal_port_deinit(uint8 lchip);

#ifdef __cplusplus
}
#endif

#endif


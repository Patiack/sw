/**
 @file sys_goldengate_bpe.h

 @date 2013-05-28

 @version v2.0

 The file define APIs and types use in sys layer
*/
#ifndef _SYS_goldengate_BPE_H
#define _SYS_goldengate_BPE_H
#ifdef __cplusplus
extern "C" {
#endif
/***************************************************************
 *
 * Header Files
 *
 ***************************************************************/
#include "sal.h"
#include "ctc_const.h"
#include "ctc_vector.h"

#include "ctc_bpe.h"

#define SYS_BPE_DBG_OUT(level, FMT, ...) \
    do { \
        CTC_DEBUG_OUT(bpe, bpe, BPE_SYS, level, FMT, ##__VA_ARGS__); \
    } while (0);

struct sys_bpe_port_attr_s
{
    uint8 port_type;  /*refer to sys_datapath_lport_type_t */
    uint8 mac_id;
    uint8 chan_id;
    uint8 speed_mode;  /*refer to ctc_port_speed_t, only usefull for network port*/
    uint8 slice_id;
    uint8 cal_index;  /*Only usefull for network port, means calendar entry index*/
    uint8 need_ext;
    uint8 pcs_mode; /*refer to ctc_chip_serdes_mode_t*/

    uint8 serdes_id;
    uint8 rsv[3];
};
typedef struct sys_bpe_port_attr_s sys_bpe_port_attr_t;

struct sys_bpe_master_s
{
    uint8  is_port_extender;
    uint8  enq_mode;   /*0: enqueue by channel, 16queue; 1: enqueue by destid, bep mode; 2: enqueue by destid, 8queue*/
    uint32 pe_uc_dsfwd_base;
    uint32 pe_mc_dsfwd_base;

    uint16 max_extend_num;
    uint8 use_logic_port;
    uint8 rsv;

    sys_bpe_port_attr_t* port_attr;
};
typedef struct sys_bpe_master_s sys_bpe_master_t;

enum bpe_igs_muxtype_e
{
    BPE_IGS_MUXTYPE_NOMUX = 0,
    BPE_IGS_MUXTYPE_MUXDEMUX = 1,
    BPE_IGS_MUXTYPE_EVB = 2,
    BPE_IGS_MUXTYPE_CB_DOWNLINK = 3,
    BPE_IGS_MUXTYPE_PE_DOWNLINK_WITH_CASCADE_PORT = 4,
    BPE_IGS_MUXTYPE_PE_UPLINK = 5,
    BPE_IGS_MUXTYPE_NUM
};
typedef enum bpe_igs_muxtype_e bpe_igs_muxtype_t;

enum bpe_egs_muxtype_e
{
    BPE_EGS_MUXTYPE_NOMUX,
    BPE_EGS_MUXTYPE_MUXDEMUX = 2,
    BPE_EGS_MUXTYPE_LOOPBACK_ENCODE,
    BPE_EGS_MUXTYPE_EVB,
    BPE_EGS_MUXTYPE_CB_CASCADE,
    BPE_EGS_MUXTYPE_UPSTREAM,
    BPE_EGS_MUXTYPE_NUM
};
typedef enum bpe_egs_muxtype_e bpe_egs_muxtype_t;

extern int32
sys_goldengate_bpe_init(uint8 lchip, void* bpe_global_cfg);

/**
 @brief De-Initialize bpe module
*/
extern int32
sys_goldengate_bpe_deinit(uint8 lchip);
extern int32
sys_goldengate_bpe_set_intlk_en(uint8 lchip, bool enable);
extern int32
sys_goldengate_bpe_get_intlk_en(uint8 lchip, bool* enable);
extern int32
sys_goldengate_bpe_set_port_extender(uint8 lchip, uint16 gport, ctc_bpe_extender_t* p_extender);
extern int32
sys_goldengate_bpe_get_port_extender(uint8 lchip, uint16 gport, bool* enable);
extern int32
sys_goldengate_bpe_get_port_extend_mcast_en(uint8 lchip, uint8 *p_enable);
extern int32
sys_goldengate_bpe_set_port_extend_mcast_en(uint8 lchip, uint8 enable);
#ifdef __cplusplus
}
#endif

#endif


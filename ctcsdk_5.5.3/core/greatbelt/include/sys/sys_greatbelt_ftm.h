/**
 @file sys_greatbelt_ftm.h

 @date 2011-11-16

 @version v2.0

 alloc memory and offset
*/

#ifndef _SYS_GREAT_BELT_FTM_H
#define _SYS_GREAT_BELT_FTM_H
#ifdef __cplusplus
extern "C" {
#endif
#include "ctc_ftm.h"

#include "sys_greatbelt_ftm_db.h"

#define SYS_FTM_DS_STATS_HW_INDIRECT_ADDR 0x1600000

#define SYS_FTM_DBG_OUT(level, FMT, ...) \
    do { \
        CTC_DEBUG_OUT(ftm, ftm, FTM_SYS, level, FMT, ##__VA_ARGS__); \
    } while (0)

enum sys_ftm_dyn_entry_type_e
{
    SYS_FTM_DYN_ENTRY_GLB_MET,
    SYS_FTM_DYN_ENTRY_VSI,
    SYS_FTM_DYN_ENTRY_ECMP,
    SYS_FTM_DYN_ENTRY_LOGIC_MET,
    SYS_FTM_DYN_ENTRY_MAX
};
typedef enum sys_ftm_dyn_entry_type_e sys_ftm_dyn_entry_type_t;

enum sys_ftm_hash_key_e
{
    SYS_FTM_TCAM_KEY_SCL,
    SYS_FTM_TCAM_KEY_ACL,
    SYS_FTM_TCAM_KEY_FDB,
    SYS_FTM_TCAM_KEY_IPMC,
    SYS_FTM_TCAM_KEY_IPUC,
    SYS_FTM_TCAM_KEY_SECURITY,
    SYS_FTM_TCAM_KEY_VLAN_CLASS,
    SYS_FTM_TCAM_KEY_OAM,
    SYS_FTM_TCAM_KEY_MAX
};
typedef int32 (* p_sys_ftm_tcam_cb_t)(uint8 lchip, uint8 is_add);

extern int32
sys_greatbelt_ftm_get_dyn_entry_size(uint8 lchip, sys_ftm_dyn_entry_type_t type,
                                     uint32* p_size);

extern int32
sys_greatbelt_ftm_lkp_register_init(uint8 lchip);

extern int32
sys_greatbelt_ftm_get_table_entry_num(uint8 lchip, uint32 table_id, uint32* entry_num);

extern int32
sys_greatbelt_ftm_get_dynamic_table_info(uint8 lchip, uint32 tbl_id,
                                         uint8* dyn_tbl_idx,
                                         uint32* entry_enum,
                                         uint32* nh_glb_entry_num);

extern  int32
sys_greatbelt_ftm_alloc_table_offset(uint8 lchip, uint32 table_id, uint8 dir, uint32 block_size, uint32* offset);

extern  int32
sys_greatbelt_ftm_free_table_offset(uint8 lchip, uint32 table_id, uint8 dir, uint32 block_size, uint32 offset);

extern int32
sys_greatbelt_ftm_mem_alloc(uint8 lchip, ctc_ftm_profile_info_t* profile_info);

extern int32
sys_greatbelt_ftm_mem_free(uint8 lchip);

extern int32
sys_greatbelt_ftm_show_alloc_info(uint8 lchip);

extern int32
sys_greatbelt_ftm_set_mpls_label_offset(uint8 lchip, uint8 mpls_block_id, uint32 offset, uint32 size);

extern int32
sys_greatbelt_ftm_check_tbl_recover(uint8 lchip, uint8 ram_id, uint32 ram_offset, uint8 *b_recover);

extern int32
sys_greatbelt_ftm_tcam_cb_register(uint8 lchip, uint8 type, p_sys_ftm_tcam_cb_t cb);

extern uint8
sys_greatbelt_ftm_get_ip_tcam_share_mode(uint8 lchip);

extern uint16
sys_greatbelt_ftm_get_ipuc_v4_number(uint8 lchip);

extern uint16
sys_greatbelt_ftm_get_ipmc_v4_number(uint8 lchip);

extern int32
sys_greatbelt_ftm_get_entry_num_by_type(uint8 lchip, ctc_ftm_key_type_t key_type, uint32* entry_num);

#ifdef __cplusplus
}
#endif

#endif


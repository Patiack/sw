/**
 @file drv_chip_io.h

 @date 2011-10-09

 @version v4.28.2

 The file contains all driver I/O interface realization
*/

#ifndef _DRV_CHIP_IO_H
#define _DRV_CHIP_IO_H

#include "sal.h"
#include "greatbelt/include/drv_enum.h"
#include "greatbelt/include/drv_common.h"

/**
 @brief Embeded Tcam lkp process
  Only use for the show forwarding tools
*/
extern int32
drv_greatbelt_chip_tcam_lookup(uint8 chip_id_offset, uint8* key, int32 keysize,
                     bool dual_lkp, uint32* result_index);

#if 0
/* zhouw note */
/**
 @brief Real sram indirect operation I/O
*/
int32
drv_greatbelt_chip_indirect_sram_tbl_ioctl(uint8 chip_id_offset, uint32 index,
                                 uint32 cmd, void* val);

/**
 @brief add hash entry after lkp operation on real chip
*/
extern int32
drv_greatbelt_chip_hash_key_add_entry(uint8 chip_id_offset, void* add_para);

/**
 @brief delete hash entry according to detailed key value on real chip
*/
extern int32
drv_greatbelt_chip_hash_key_del_entry_by_key(uint8 chip_id_offset, void* del_para);

/**
 @brief delete hash entry according to hash index on real chip
*/
extern int32
drv_greatbelt_chip_hash_key_del_entry_by_index(uint8 chip_id_offset, void* del_para);

/**
 @brief Hash lookup I/O control API on real chip
*/
extern int32
drv_greatbelt_chip_hash_lookup(uint8 chip_id_offset,
                     uint32* key,
                     hash_ds_ctl_cpu_key_status_t* hash_cpu_status,
                     cpu_req_hash_key_type_e cpu_hashkey_type,
                     ip_hash_rst_info_t* ip_hash_rst_info);
#endif

/**
 @brief write register field data to a memory location on chip
*/
extern int32
drv_greatbelt_chip_register_field_write(uint8 chip_id_offset, tbls_id_t tbl_id,
                            uint32 fld_id, uint32* data);

/**
 @brief write register data to a sram memory location on real chip
*/
extern int32
drv_greatbelt_chip_sram_tbl_write(uint8 chip_id_offset, tbls_id_t tbl_id,
                        uint32 index, uint32* data);

/**
 @brief write register data to a sram memory location on real chip
*/
extern int32
drv_greatbelt_chip_sram_tbl_read(uint8 chip_id_offset, tbls_id_t tbl_id,
                       uint32 index, uint32* data);

/**
 @brief sram write I/O control API (write data into one address) on real chip
*/
extern int32
drv_greatbelt_chip_write_sram_entry(uint8 chip_id_offset, uintptr addr,
                          uint32* data, int32 len);

/**
 @brief sram read I/O control API (read data from one address) on real chip
*/
extern int32
drv_greatbelt_chip_read_sram_entry(uint8 chip_id_offset, uintptr addr,
                         uint32* data, int32 len);

/**
 @brief write real tcam interface
*/
extern int32
drv_greatbelt_chip_tcam_tbl_write(uint8 chip_id_offset, tbls_id_t tbl_id,
                        uint32 index, tbl_entry_t* entry);

/**
 @brief read real tcam interface
*/
extern int32
drv_greatbelt_chip_tcam_tbl_read(uint8 chip_id_offset, tbls_id_t tbl_id,
                       uint32 index, tbl_entry_t* entry);

/**
 @brief remove real tcam entry interface
*/
extern int32
drv_greatbelt_chip_tcam_tbl_remove(uint8 chip_id_offset, tbls_id_t tbl_id, uint32 index);

/**
 @brief tcam mutex init
*/
extern int32
drv_greatbelt_chip_tcam_mutex_init(uint8 chip_id_offset);

#endif


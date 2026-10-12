/**
  @file drv_io.h

  @date 2010-02-26

  @version v5.1

  The file implement driver IOCTL defines and macros
*/

#ifndef _DRV_IO_H_
#define _DRV_IO_H_

/*#include "greatbelt/include/drv_tbl_reg.h"*/
#include "greatbelt/include/drv_enum.h"
#include "greatbelt/include/drv_common.h"
#include "greatbelt/include/drv_chip_info.h"

/**********************************************************************************
              Define Typedef, define and Data Structure
***********************************************************************************/
#define DRV_IOCTL           drv_greatbelt_ioctl_api
#define DRV_TCAM_TBL_REMOVE drv_greatbelt_tcam_tbl_remove
#define DRV_HASH_KEY_IOCTL  drv_greatbelt_hash_key_ioctl
#define DRV_HASH_LOOKUP     drv_greatbelt_hash_lookup

typedef enum chip_io_op_s
{
    CHIP_IO_OP_IOCTL,
    CHIP_IO_OP_TCAM_REMOVE,
    CHIP_IO_OP_HASH_KEY_IOCTL,
    CHIP_IO_OP_HASH_LOOKUP,
} chip_io_op_t;

typedef struct chip_io_para_s
{
    chip_io_op_t op;
    union
    {
        struct
        {
            uint8 chip_id;
            uint32 index;
            uint32 cmd;
            void* val;
        } ioctl;

        struct
        {
            uint8 chip_id;
            tbls_id_t tbl_id;
            uint32 index;
        } tcam_remove;

        struct
        {
            void* p_para;
            hash_op_type_t op_type;
        } hash_key_ioctl;

        struct
        {
            lookup_info_t* info;
            lookup_result_t* result;
        } hash_lookup;
    } u;
} chip_io_para_t;

typedef int32 (* DRV_IO_AGENT_CALLBACK)(void*);

#define DRV_IO_DATA_SIZE                16
#define DRV_IO_DEBUG_CACHE_SIZE         256

/* Driver IO entry */
typedef struct
{
    sal_systime_t   tv;             /* timestamp */
    chip_io_para_t  io;             /* IO parameter */
    uint8 data[DRV_IO_DATA_SIZE];   /* used to store content of pointer in IO parameter */
} drv_io_debug_cache_entry_t;

/* Driver IO cache */
typedef struct
{
    uint32          count;
    uint32          index;
    drv_io_debug_cache_entry_t entry[DRV_IO_DEBUG_CACHE_SIZE];
} drv_io_debug_cache_info_t;

typedef struct
{
    DRV_IO_AGENT_CALLBACK       chip_agent_cb;  /* chip_agent callback */
    DRV_IO_AGENT_CALLBACK       debug_cb;       /* debug callback */
    uint32                      debug_stats[CHIP_IO_OP_HASH_LOOKUP + 1];
    uint32                      debug_action;
    drv_io_debug_cache_info_t   cache;
} drv_io_t;

/**********************************************************************************
                      Define API function interfaces
***********************************************************************************/
/**
 @brief the table I/O control API, callback by SDK with statistics and debug
*/
extern int32
drv_greatbelt_ioctl_api(uint8 chip_id, int32 index, uint32 cmd, void* val);

/**
 @brief the table I/O control API
*/
extern int32
drv_greatbelt_ioctl(uint8 chip_id, int32 index, uint32 cmd, void* val);

/**
 @brief remove tcam entry interface according to key id and index
*/
extern int32
drv_greatbelt_tcam_tbl_remove(uint8 chip_id, tbls_id_t tbl_id, uint32 index);

/**
 @brief hash driver I/O interface include write and delete operation
*/
extern int32
drv_greatbelt_hash_key_ioctl(void*, hash_op_type_t, void*);

/**
 @brief hash lookup
*/
extern int32
drv_greatbelt_hash_lookup(lookup_info_t*, lookup_result_t*);

/**
 @brief get acl lookup bitmap
*/
extern int32
drv_greatbelt_get_acl_lookup_bitmap(uint8 chip_id, uint8 lookup_index, uint32* lookup_bitmap);

/**
 @brief set acl lookup bitmap
*/
extern int32
drv_greatbelt_set_acl_lookup_bitmap(uint8 chip_id, uint8 lookup_index, uint32 lookup_bitmap);

/**
 @brief judge the IO is real ASIC for EADP
*/
extern int32
drv_greatbelt_io_is_asic(void);

/**
 @brief init drv IO
*/
int32
drv_greatbelt_io_init(void);

#endif /*end of _DRV_IO_H*/


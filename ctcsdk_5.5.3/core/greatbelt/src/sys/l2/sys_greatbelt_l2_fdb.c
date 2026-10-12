/**
   @file sys_greatbelt_l2_fdb.c

   @date 2009-10-19

   @version v2.0

   The file implement   FDB/L2 unicast /mac filtering /mac security/L2 mcast functions
 */

#include "sal.h"
#include "ctc_const.h"
#include "ctc_hash.h"
#include "ctc_vector.h"
#include "ctc_linklist.h"
#include "ctc_error.h"

#include "ctc_l2.h"
#include "ctc_dma.h"
#include "sys_greatbelt_common.h"
#include "sys_greatbelt_chip.h"
#include "sys_greatbelt_port.h"
#include "sys_greatbelt_opf.h"
#include "sys_greatbelt_ftm.h"
#include "sys_greatbelt_dma.h"
#include "sys_greatbelt_nexthop_api.h"
#include "sys_greatbelt_nexthop.h"
#include "sys_greatbelt_learning_aging.h"
#include "sys_greatbelt_aps.h"
#include "sys_greatbelt_l2_fdb.h"
#include "sys_greatbelt_register.h"
#include "sys_greatbelt_security.h"
#include "sys_greatbelt_fdb_sort.h"

#include "greatbelt/include/drv_lib.h"

#define FAIL(r)                      ((r) < 0)
#define FDB_ERR_RET_UL(r)            CTC_ERROR_RETURN_WITH_UNLOCK((r), pl2_gb_master[lchip]->l2_mutex)
#define FDB_SET_MAC(d, s)            sal_memcpy(d, s, sizeof(mac_addr_t))
#define FDB_SET_HW_MAC(dest, src)    FDB_SET_MAC(dest, src)

#define SYS_FDB_DISCARD_FWD_PTR    0xFFFF
#define DISCARD_PORT               0xFFFF
#define SYS_L2_MAX_FID             0xFFFF

#define SYS_FDB_FLUSH_SLEEP(cnt)         \
{                                        \
    cnt++;                               \
    if (cnt >= 1024)                     \
    {                                    \
        sal_task_sleep(1);               \
        cnt = 0;                         \
    }                                    \
}

#define SYS_L2_FDB_FLUSH_COUNT_SET(flush_fdb_cnt_per_loop)  \
    {                                                       \
        flush_fdb_cnt_per_loop = pl2_gb_master[lchip]->cfg_flush_cnt; \
    }

#define SYS_L2_FDB_FLUSH_COUNT_PROCESS(flush_fdb_cnt_per_loop) \
    {                                                          \
        if ((flush_fdb_cnt_per_loop) > 0)                      \
        {                                                      \
            (flush_fdb_cnt_per_loop)--;                        \
            if (0 == (flush_fdb_cnt_per_loop))                 \
            {                                                  \
                return CTC_E_FDB_OPERATION_PAUSE;              \
            }                                                  \
        }                                                      \
    }


#define L2_LOCK \
    if (pl2_gb_master[lchip]->l2_mutex) sal_mutex_lock(pl2_gb_master[lchip]->l2_mutex)

#define L2_UNLOCK \
    if (pl2_gb_master[lchip]->l2_mutex) sal_mutex_unlock(pl2_gb_master[lchip]->l2_mutex)

#define FIB_ACC_LOCK \
    if (pl2_gb_master[lchip]->fib_acc_mutex) sal_mutex_lock(pl2_gb_master[lchip]->fib_acc_mutex)

#define FIB_ACC_UNLOCK \
    if (pl2_gb_master[lchip]->fib_acc_mutex) sal_mutex_unlock(pl2_gb_master[lchip]->fib_acc_mutex)

#define SYS_L2_INIT_CHECK(lchip)        \
    do{                              \
        SYS_LCHIP_CHECK_ACTIVE(lchip); \
        if (pl2_gb_master[lchip] == NULL)    \
        { return CTC_E_NOT_INIT; } \
    }while(0)


#define SYS_FDB_DBG_OUT(level, FMT, ...) \
    CTC_DEBUG_OUT(l2, fdb, L2_FDB_SYS, level, FMT, ## __VA_ARGS__)

#define SYS_FDB_DBG_FUNC() \
    SYS_FDB_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "  %% FUNC: %s()\n", __FUNCTION__)

#define SYS_FDB_DBG_INFO(FMT, ...) \
    SYS_FDB_DBG_OUT(CTC_DEBUG_LEVEL_INFO, FMT, ## __VA_ARGS__)

#define SYS_FDB_DBG_PARAM(FMT, ...) \
    SYS_FDB_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, FMT, ## __VA_ARGS__)

#define SYS_FDB_DBG_ERROR(FMT, ...) \
    SYS_FDB_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, FMT, ## __VA_ARGS__)

#define SYS_FDB_DBG_DUMP(FMT, ...) \
    SYS_FDB_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, FMT, ## __VA_ARGS__)


#define SYS_L2_DUMP_SYS_INFO(psys)                                        \
    {                                                                     \
        SYS_FDB_DBG_INFO("@@@@@ MAC[%s]  FID[%d]\n"                       \
                         "     gport:0x%x,   key_index:0x%x, nhid:0x%x\n" \
                         "     ad_index:0x%x\n",                          \
                         sys_greatbelt_output_mac((psys)->mac), (psys)->fid,        \
                         (psys)->gport, (psys)->key_index, (psys)->nhid,  \
                         (psys)->ad_index);                               \
    }

#define SYS_VSI_PARAM_MAX    8

typedef ds_mac_hash_key_t   ds_key_t;

/****************************************************************************
 *
 * Global and Declaration
 *
 *****************************************************************************/
typedef struct
{
    mac_addr_t mac;
    uint16     fid;
}sys_l2_key_t;

typedef struct
{
    mac_addr_t mac;
    mac_addr_t mask;
}sys_l2_tcam_key_t;

typedef struct
{
    sys_l2_tcam_key_t    key;
    uint32               key_index;
    sys_l2_flag_t        flag;
}sys_l2_tcam_t;


typedef struct
{
    /* keep u0 and flag at top!!! for the need of caculate hash. */
    union
    {
        uint32 fwdptr; /*for uc. becasue it's convenient.*/
        uint32 nhid;   /*for mc, def. because they don't have fwdptr.*/
    }u0;
    sys_l2_flag_ad_t flag;

    uint16        gport; /* for unicast. */
    uint32        index;
}sys_l2_ad_spool_t;


struct sys_l2_node_s
{
    sys_l2_key_t   key;
    uint32         key_index;       /**< key key_index for ucast and mcast, adptr->index for default entry*/
    sys_l2_ad_spool_t*  adptr;
    sys_l2_flag_node_t  flag;
    ctc_listnode_t * port_listnode; /**< in ucast FDB entry, the pointer is point to port listnode,else ,
                                       the pointer is point to NULL*/
    ctc_listnode_t * vlan_listnode; /**< in ucast FDB entry, the pointer is point to vlan listnode,
                                       else  the pointer is point to NULL */
};
typedef struct sys_l2_node_s   sys_l2_node_t;

struct sys_l2_vport_2_nhid_s
{
    uint32 nhid;
    uint32 ad_index;
};
typedef struct sys_l2_vport_2_nhid_s   sys_l2_vport_2_nhid_t;

struct sys_l2_mcast_node_s
{
    uint32 nhid;
    uint32 key_index;
};
typedef struct sys_l2_mcast_node_s   sys_l2_mcast_node_t;


/**
   @brief stored in gport_vec.
 */
typedef struct
{
    ctc_linklist_t* port_list;       /**< store l2_node*/
    uint32        dynamic_count;
    uint32        local_dynamic_count;
}sys_l2_port_node_t;

/**
   @brief stored in vlan_fdb_list.
 */
typedef struct
{
    ctc_linklist_t * fid_list;
    uint32         dynamic_count;
    uint32         local_dynamic_count;
    uint16         fid;
    uint16         flag;         /**<ctc_l2_dft_vlan_flag_t */
    uint32         nhid;
    uint16         mc_gid;
    uint8          create;       /* create by default entry */
    uint8          unknown_mcast_tocpu;
}sys_l2_fid_node_t;


/* proved neccsary. like init default entry. */
typedef struct
{
    mac_addr_t    mac;
    mac_addr_t    mac_mask; /* for system mac*/

    uint16        fid;
    uint16        gport;
    uint16        ecmp_num;
    sys_l2_flag_t flag;

    uint32        fwd_ptr;
    uint8         dsfwd_valid;

    uint16        mc_gid;
    uint8         with_nh;       /**< used for build ad_index with nexthop */
    uint8         revert_default;
    uint32        nhid;          /**< use it directly if with_nh is true */

    uint32        fid_flag;      /**< ctc_l2_dft_vlan_flag_t   */


    uint32        key_index;
    uint32        ad_index;
    uint8         key_valid;
}sys_l2_info_t;


typedef struct
{
    sys_l2_node_t          l2_node;
    uint32                 query_flag;
    uint32                 count;
    ctc_l2_fdb_query_rst_t * query_rst;
    sys_l2_detail_t        * fdb_detail_rst;
    uint8 lchip;             /**< for tranvase speific lchip*/
}sys_l2_mac_cb_t;

/**
   @brief flush information when flush all
 */
typedef struct
{
    uint8  flush_flag;
    uint8  lchip;/**< for tranvase speific lchip*/
    uint32 flush_fdb_cnt_per_loop;  /**< flush flush_fdb_cnt_per_loop entries one time if flush_fdb_cnt_per_loop>0,
                                        flush all entries if flush_fdb_cnt_per_loop=0 */
    uint16 sleep_cnt;
}sys_l2_flush_t;

typedef struct
{
    uint32 key_index[10];
    uint8  index_cnt;
    uint8  rsv[3];
}sys_l2_calc_index_t;

/* for dump FDB HW table by DMA */
#define SYS_L2_MAC_KEY_DWORD_SIZE       3
#define SYS_L2_DMA_READ_LOOP_COUNT      10
#define SYS_L2_MAC_KEY_DMA_READ_COUNT   500

sys_l2_master_t* pl2_gb_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};

#define  _ENTRY_


#define CONTINUE_IF_FLAG_NOT_MATCH(query_flag, flag_ad, flag_node) \
    switch (query_flag)                              \
    {                                                \
    case CTC_L2_FDB_ENTRY_STATIC:                    \
        if (!flag_ad.is_static)                         \
        {                                            \
            continue;                                \
        }                                            \
        break;                                       \
                                                     \
    case  CTC_L2_FDB_ENTRY_DYNAMIC:                  \
        if (flag_ad.is_static)                          \
        {                                            \
            continue;                                \
        }                                            \
        break;                                       \
                                                     \
    case CTC_L2_FDB_ENTRY_LOCAL_DYNAMIC:             \
        if (flag_node.remote_dynamic || flag_ad.is_static)   \
        {                                            \
            continue;                                \
        }                                            \
        break;                                       \
                                                     \
    case CTC_L2_FDB_ENTRY_ALL:                       \
    default:                                         \
        break;                                       \
    }


#define RETURN_IF_FLAG_NOT_MATCH(query_flag, flag_ad, flag_node) \
    switch (query_flag)                            \
    {                                              \
    case CTC_L2_FDB_ENTRY_STATIC:                  \
        if (!flag_ad.is_static)                       \
        {                                          \
            return 0;                              \
        }                                          \
        break;                                     \
                                                   \
    case  CTC_L2_FDB_ENTRY_DYNAMIC:                \
        if (flag_ad.is_static)                        \
        {                                          \
            return 0;                              \
        }                                          \
        break;                                     \
                                                   \
    case CTC_L2_FDB_ENTRY_LOCAL_DYNAMIC:           \
        if (flag_node.remote_dynamic || flag_ad.is_static) \
        {                                          \
            return 0;                              \
        }                                          \
        break;                                     \
                                                   \
    case CTC_L2_FDB_ENTRY_ALL:                     \
    default:                                       \
        break;                                     \
    }


#define UTILITY
#define GET_FID_NODE          0
#define GET_FID_LIST          1

#define L2_COUNT_GLOBAL       0
#define L2_COUNT_PORT_LIST    1
#define L2_COUNT_FID_LIST     2

#define L2_TYPE_UC            0
#define L2_TYPE_MC            1
#define L2_TYPE_DF            2
#define L2_TYPE_TCAM          3

#define DEFAULT_ENTRY_INDEX(fid)            (pl2_gb_master[lchip]->dft_entry_base + (fid))
#define IS_DEFAULT_ENTRY_INDEX(ad_index)    ((ad_index) > pl2_gb_master[lchip]->dft_entry_base)

#define SYS_L2_DECODE_QUERY_FID(index)   (((index) >> 18) & 0x3FFF)
#define SYS_L2_DECODE_QUERY_IDX(index)   ((index) & 0x3FFFF)
#define SYS_L2_ENCODE_QUERY_IDX(fid, index)   (((fid) << 18)|(index))
STATIC int32
_sys_greatbelt_l2_free_dsmac_index(uint8 lchip, sys_l2_info_t* psys, sys_l2_ad_spool_t* pa, sys_l2_flag_node_t* pn, uint8* freed);

/****************************************************************************
 *
 * Function
 *
 *****************************************************************************/

STATIC int32
_sys_get_dsmac_by_index(uint8 lchip, uint32 ad_index, void* p_ad_out)
{
    uint32 cmd = 0;

    cmd = DRV_IOR(DsMac_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, ad_index, cmd, p_ad_out));
    SYS_FDB_DBG_INFO("Read dsmac, ad_index:0x%x\n", ad_index);

    return CTC_E_NONE;
}

typedef struct
{
    uint32 key_index;
    uint32 ad_index;

    uint8  conflict;
    uint8  hit;
    uint8  pending;   /* mac limit reach*/
    uint8  rsv0;
}mac_lookup_result_t;


#if 1
union drv_fib_acc_in_u
{
    struct
    {
        mac_addr_t mac;       /* by key*/
        uint16     fid;       /* by key */
        uint32     key_index; /* by index */
        uint32     ad_index;  /* add */
        uint8      rsv0[2];
    } mac;                    /*add del lookup*/

    struct
    {
        uint16     flag; /* flush type by port or vsi or port+vsi and static or dynamic and so on drv_fib_acc_flush_flag_t*/
        uint16     rsv0;

        uint16     fid;
        uint16     port; /* gport or logic port */

        mac_addr_t mac;  /* flush with mac*/
    } mac_flush;         /* flush */

    struct
    {
        uint8  key_length_mode; /* only for simulatiom, hardware do not need */
        uint8  rsv0[3];
        uint32 key_index;
        void   * data;
    } rw;
};
typedef union drv_fib_acc_in_u   drv_fib_acc_in_t;

union drv_fib_acc_out_u
{
    struct
    {
        uint32 key_index;
        uint32 ad_index;

        uint8  conflict;
        uint8  hit;
        uint8  free;
    }
    mac;

    struct
    {
        ds_key_t data;
    } read;
};
typedef union drv_fib_acc_out_u   drv_fib_acc_out_t;

/* add for fib acc start */
enum drv_fib_acc_type_e
{
    DRV_FIB_ACC_WRITE_MAC_BY_IDX,
    DRV_FIB_ACC_WRITE_MAC_BY_KEY,
    DRV_FIB_ACC_DEL_MAC_BY_IDX,
    DRV_FIB_ACC_DEL_MAC_BY_KEY,
    DRV_FIB_ACC_LKP_MAC,
    DRV_FIB_ACC_FLUSH_MAC_BY_PORT_VLAN,
    DRV_FIB_ACC_READ_FIB0_BY_IDX,
    DRV_FIB_ACC_TYPE_NUM
};
typedef enum drv_fib_acc_type_e   drv_fib_acc_type_t;

enum drv_fib_acc_flush_flag_e
{
    DRV_FIB_ACC_FLUSH_BY_PORT    =  (0x1),
    DRV_FIB_ACC_FLUSH_BY_VSI     =  (0x1 << 1),
    DRV_FIB_ACC_FLUSH_STATIC     =  (0x1 << 2),
    DRV_FIB_ACC_FLUSH_DYNAMIC    =  (0x1 << 3),
    DRV_FIB_ACC_FLUSH_MCAST      =  (0x1 << 4),
    DRV_FIB_ACC_FLUSH_LOGIC_PORT =  (0x1 << 5),
    DRV_FIB_ACC_FLUSH_FLAG_NUM
};
typedef enum drv_fib_acc_flush_flag_e   drv_fib_acc_flush_flag_t;

#define SYS_L2_REG_SYN_ADD_DB_CB(lchip, add) pl2_gb_master[lchip]->sync_##add##_db = _sys_greatbelt_l2_add_indrect;
#define SYS_L2_REG_SYN_DEL_DB_CB(lchip, remove) pl2_gb_master[lchip]->sync_##remove##_db = _sys_greatbelt_l2_remove_indrect;

STATIC int32
drv_fib_cam_io(uint8 lchip, uint32 key_index,  ds_mac_hash_key_t* ds_mac_hash_key, uint8 b_read)
{
    uint32            cmd = 0;
    dynamic_ds_fdb_lookup_index_cam_t dynamic_ds_fdb_lookup_index_cam;

    SYS_FDB_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "%s cam key, key_index:0x%x\n", b_read ? "read" : "write", key_index);

    sal_memset(&dynamic_ds_fdb_lookup_index_cam, 0, sizeof(dynamic_ds_fdb_lookup_index_cam_t));

    cmd = DRV_IOR(DynamicDsFdbLookupIndexCam_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, key_index / 2, cmd, &dynamic_ds_fdb_lookup_index_cam));
    if(b_read)
    {
        if (key_index % 2)
        {
            sal_memcpy(ds_mac_hash_key, (uint8*)&dynamic_ds_fdb_lookup_index_cam + sizeof(ds_3word_hash_key_t),
                         sizeof(ds_3word_hash_key_t));
        }
        else
        {
            sal_memcpy(ds_mac_hash_key, (uint8*)&dynamic_ds_fdb_lookup_index_cam, sizeof(ds_3word_hash_key_t));
        }
    }
    else
    {
        if (key_index % 2)
        {
            sal_memcpy((uint8*)&dynamic_ds_fdb_lookup_index_cam + sizeof(ds_3word_hash_key_t),
                       ds_mac_hash_key,  sizeof(ds_3word_hash_key_t));
        }
        else
        {
            sal_memcpy((uint8*)&dynamic_ds_fdb_lookup_index_cam, ds_mac_hash_key, sizeof(ds_3word_hash_key_t));
        }

        cmd = DRV_IOW(DynamicDsFdbLookupIndexCam_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, key_index / 2, cmd, &dynamic_ds_fdb_lookup_index_cam));
    }

    return CTC_E_NONE;
}

STATIC int32
drv_fib_key_get(uint8 lchip, uint32 key_index,  ds_mac_hash_key_t* ds_mac_hash_key)
{
    uint32  cmd     = 0;

    if(key_index < pl2_gb_master[lchip]->hash_base)
    {
        drv_fib_cam_io(lchip, key_index,  ds_mac_hash_key, 1);
    }
    else
    {
        cmd = DRV_IOR(DsMacHashKey_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, key_index - pl2_gb_master[lchip]->hash_base, cmd, ds_mac_hash_key));

    }

    return CTC_E_NONE;

}

#if (SDK_WORK_PLATFORM == 0)
/*Hw*/
STATIC int32
drv_fib_acc(uint8 lchip, uint8 acc_type, drv_fib_acc_in_t* fib_acc_in, drv_fib_acc_out_t* fib_acc_out)
{
    uint32                    cmd   = 0;
    uint32                    trig  = 0;
    uint8                     loop  = 0;
    uint8                     done  = 0;
    uint8                     count = 0;
    int32                     ret   = CTC_E_NONE;
    uint8                     acc   = 1;

    fib_hash_key_cpu_req_t    fib_hash_key_cpu_req;
    fib_hash_key_cpu_req_t    fib_hash_key_cpu_req_old;
    fib_hash_key_cpu_result_t fib_hash_key_cpu_result;

    sal_memset(&fib_hash_key_cpu_req, 0, sizeof(fib_hash_key_cpu_req_t));
    sal_memset(&fib_hash_key_cpu_result, 0, sizeof(fib_hash_key_cpu_result_t));

    switch (acc_type)
    {
    /* maybe need to add flush by port +vid */
    case DRV_FIB_ACC_FLUSH_MAC_BY_PORT_VLAN:
        fib_hash_key_cpu_req.hash_key_cpu_req_type = 0x4;
        {
            if (CTC_FLAG_ISSET(fib_acc_in->mac_flush.flag, DRV_FIB_ACC_FLUSH_BY_VSI))
            {
                fib_hash_key_cpu_req.cpu_key_del_type = 0x2;
                fib_hash_key_cpu_req.cpu_key_vrf_id   = fib_acc_in->mac_flush.fid;
            }

            if (CTC_FLAG_ISSET(fib_acc_in->mac_flush.flag, DRV_FIB_ACC_FLUSH_BY_PORT))
            {
                fib_hash_key_cpu_req.cpu_key_del_type      = 0x1;
                fib_hash_key_cpu_req.hash_key_cpu_req_port = fib_acc_in->mac_flush.port;
                if (CTC_FLAG_ISSET(fib_acc_in->mac_flush.flag, DRV_FIB_ACC_FLUSH_LOGIC_PORT))
                {    /*logic port*/
                    fib_hash_key_cpu_req.cpu_key_port_del_mode = 1;
                }
                else
                {    /*global port*/
                    fib_hash_key_cpu_req.cpu_key_port_del_mode = 0;
                }
            }

            if (CTC_FLAG_ISSET(fib_acc_in->mac_flush.flag, DRV_FIB_ACC_FLUSH_BY_PORT)
                && CTC_FLAG_ISSET(fib_acc_in->mac_flush.flag, DRV_FIB_ACC_FLUSH_BY_VSI))
            {
                fib_hash_key_cpu_req.cpu_key_del_type = 0;
            }

            if CTC_FLAG_ISSET(fib_acc_in->mac_flush.flag, DRV_FIB_ACC_FLUSH_STATIC)
            {
                CTC_BIT_SET(fib_hash_key_cpu_req.cpu_key_index, 0);
            }

            if CTC_FLAG_ISSET(fib_acc_in->mac_flush.flag, DRV_FIB_ACC_FLUSH_DYNAMIC)
            {
                CTC_BIT_SET(fib_hash_key_cpu_req.cpu_key_index, 1);
            }

            if CTC_FLAG_ISSET(fib_acc_in->mac_flush.flag, DRV_FIB_ACC_FLUSH_MCAST)
            {
                CTC_BIT_SET(fib_hash_key_cpu_req.cpu_key_index, 2);
            }
        }
        break;

    case DRV_FIB_ACC_DEL_MAC_BY_KEY:
        fib_hash_key_cpu_req.hash_key_cpu_req_type = 0x3;
        fib_hash_key_cpu_req.cpu_key_vrf_id        = fib_acc_in->mac.fid;
        fib_hash_key_cpu_req.cpu_key_mac47_32      = (fib_acc_in->mac.mac[0] << 8) + fib_acc_in->mac.mac[1];
        fib_hash_key_cpu_req.cpu_key_mac31_0       = (fib_acc_in->mac.mac[2] << 24) + (fib_acc_in->mac.mac[3] << 16) + (fib_acc_in->mac.mac[4] << 8) + fib_acc_in->mac.mac[5];
        break;

    case DRV_FIB_ACC_DEL_MAC_BY_IDX:
        fib_hash_key_cpu_req.hash_key_cpu_req_type = 0x2;
        fib_hash_key_cpu_req.cpu_key_index         = fib_acc_in->mac.key_index;
        break;

    case DRV_FIB_ACC_WRITE_MAC_BY_IDX:
        fib_hash_key_cpu_req.hash_key_cpu_req_type = 0;
        fib_hash_key_cpu_req.cpu_key_vrf_id        = fib_acc_in->mac.fid;
        fib_hash_key_cpu_req.cpu_key_mac47_32      = (fib_acc_in->mac.mac[0] << 8) + fib_acc_in->mac.mac[1];
        fib_hash_key_cpu_req.cpu_key_mac31_0       = (fib_acc_in->mac.mac[2] << 24) + (fib_acc_in->mac.mac[3] << 16) + (fib_acc_in->mac.mac[4] << 8) + fib_acc_in->mac.mac[5];
        fib_hash_key_cpu_req.cpu_key_ds_ad_index   = fib_acc_in->mac.ad_index;
        fib_hash_key_cpu_req.cpu_key_index         = fib_acc_in->mac.key_index;
        break;

    case DRV_FIB_ACC_WRITE_MAC_BY_KEY:
        fib_hash_key_cpu_req.hash_key_cpu_req_type = 1;
        fib_hash_key_cpu_req.cpu_key_vrf_id        = fib_acc_in->mac.fid;
        fib_hash_key_cpu_req.cpu_key_mac47_32      = (fib_acc_in->mac.mac[0] << 8) + fib_acc_in->mac.mac[1];
        fib_hash_key_cpu_req.cpu_key_mac31_0       = (fib_acc_in->mac.mac[2] << 24) + (fib_acc_in->mac.mac[3] << 16) + (fib_acc_in->mac.mac[4] << 8) + fib_acc_in->mac.mac[5];
        fib_hash_key_cpu_req.cpu_key_ds_ad_index   = fib_acc_in->mac.ad_index;
        break;

    case DRV_FIB_ACC_LKP_MAC:

        fib_hash_key_cpu_req.hash_key_cpu_req_type = 6;
        fib_hash_key_cpu_req.cpu_key_vrf_id        = fib_acc_in->mac.fid;
        fib_hash_key_cpu_req.cpu_key_mac47_32      = (fib_acc_in->mac.mac[0] << 8) + fib_acc_in->mac.mac[1];
        fib_hash_key_cpu_req.cpu_key_mac31_0       = (fib_acc_in->mac.mac[2] << 24) + (fib_acc_in->mac.mac[3] << 16) + (fib_acc_in->mac.mac[4] << 8) + fib_acc_in->mac.mac[5];
        break;

    case DRV_FIB_ACC_READ_FIB0_BY_IDX:
        drv_fib_key_get(lchip, fib_acc_in->rw.key_index, &(fib_acc_out->read.data));
        acc = 0;
        break;

    default:
        acc = 0;
        break;
    }

    if(acc)
    {
        /**/
        FIB_ACC_LOCK;

        /* check valid bit clear */
        cmd = DRV_IOR(FibHashKeyCpuReq_t, DRV_ENTRY_FLAG);
        CTC_ERROR_GOTO(DRV_IOCTL(lchip, 0, cmd, &fib_hash_key_cpu_req_old), ret, error_0);
        if ( 1 == fib_hash_key_cpu_req_old.hash_key_cpu_req_valid )
        {
            ret = CTC_E_DRV_FAIL;
            goto error_0;
        }

        cmd = DRV_IOW(FibHashKeyCpuReq_t, DRV_ENTRY_FLAG);
        CTC_ERROR_GOTO(DRV_IOCTL(lchip, 0, cmd, &fib_hash_key_cpu_req), ret, error_0);

        trig = 1;
        cmd  = DRV_IOW(FibHashKeyCpuReq_t, FibHashKeyCpuReq_HashKeyCpuReqValid_f);
        CTC_ERROR_GOTO(DRV_IOCTL(lchip, 0, cmd, &trig), ret, error_0);

        /*wait done*/
        cmd = DRV_IOR(FibHashKeyCpuResult_t, DRV_ENTRY_FLAG);
        for (loop = 0; loop < 20; loop++)
        {
            for (count = 0; count < 150; count++)
            {
                DRV_IOCTL(lchip, 0, cmd, &fib_hash_key_cpu_result);
                if (fib_hash_key_cpu_result.hash_cpu_key_done)
                {
                    done = 1;
                    break;
                }
            }

            if (done)
            {
                break;
            }
            else
            {
                sal_task_sleep(1);
            }
        }

        FIB_ACC_UNLOCK;

        fib_acc_out->mac.key_index = fib_hash_key_cpu_result.hash_cpu_lu_index;
        fib_acc_out->mac.ad_index  = fib_hash_key_cpu_result.hash_cpu_ds_index;
        fib_acc_out->mac.hit       = fib_hash_key_cpu_result.hash_cpu_key_hit;
        fib_acc_out->mac.free      = fib_hash_key_cpu_result.hash_cpu_free_valid;
        fib_acc_out->mac.conflict  = !fib_acc_out->mac.free && !fib_acc_out->mac.hit;

        if (done)
        {
            ret = CTC_E_NONE;
        }
        else
        {
            SYS_FDB_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "FIB ACC NOT DONE!\r\n");
            ret = CTC_E_DRV_FAIL;
        }
    }
    else
    {
        ret = CTC_E_NONE;
    }

    return ret;
error_0:
    FIB_ACC_UNLOCK;
    return ret;
}
#else



STATIC int32
drv_fib_acc(uint8 lchip, uint8 acc_type, drv_fib_acc_in_t* fib_acc_in, drv_fib_acc_out_t* fib_acc_out)
{
    uint32            cmd = 0;
    lookup_info_t     lookup_info;
    lookup_result_t   fib_result;
    ds_mac_hash_key_t ds_mac_hash_key;

    sal_memset(&ds_mac_hash_key, 0, sizeof(ds_mac_hash_key_t));
    sal_memset(&lookup_info, 0, sizeof(lookup_info_t));
    sal_memset(&fib_result, 0, sizeof(lookup_result_t));

    lookup_info.chip_id     = lchip;
    lookup_info.hash_module = HASH_MODULE_FIB;
    lookup_info.hash_type   = FIB_HASH_TYPE_MAC;
    lookup_info.p_ds_key    = &ds_mac_hash_key;

    switch (acc_type)
    {
    case DRV_FIB_ACC_DEL_MAC_BY_KEY:
        ds_mac_hash_key.mapped_mac47_32 = (fib_acc_in->mac.mac[0] << 8) + fib_acc_in->mac.mac[1];
        ds_mac_hash_key.mapped_mac31_0  = (fib_acc_in->mac.mac[2] << 24) + (fib_acc_in->mac.mac[3] << 16) + (fib_acc_in->mac.mac[4] << 8) + fib_acc_in->mac.mac[5];
        ds_mac_hash_key.is_mac_hash     = TRUE;
        ds_mac_hash_key.vsi_id          = fib_acc_in->mac.fid;
        ds_mac_hash_key.valid           = TRUE;

        /* get index of Key */
        CTC_ERROR_RETURN(DRV_HASH_LOOKUP(&lookup_info, &fib_result));
        if (fib_result.valid)
        {
            sal_memset(&ds_mac_hash_key, 0, sizeof(ds_mac_hash_key_t));

            if (fib_result.key_index < pl2_gb_master[lchip]->hash_base)
            {
                drv_fib_cam_io(lchip, fib_result.key_index,  &ds_mac_hash_key, 0);
            }
            else
            {
                cmd = DRV_IOW(DsMacHashKey_t, DRV_ENTRY_FLAG);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, (fib_result.key_index - pl2_gb_master[lchip]->hash_base), cmd, &ds_mac_hash_key));
            }
        }

        break;

    case DRV_FIB_ACC_WRITE_MAC_BY_KEY:
        ds_mac_hash_key.mapped_mac47_32 = (fib_acc_in->mac.mac[0] << 8) + fib_acc_in->mac.mac[1];
        ds_mac_hash_key.mapped_mac31_0  = (fib_acc_in->mac.mac[2] << 24) + (fib_acc_in->mac.mac[3] << 16) + (fib_acc_in->mac.mac[4] << 8) + fib_acc_in->mac.mac[5];
        ds_mac_hash_key.is_mac_hash     = TRUE;
        ds_mac_hash_key.vsi_id          = fib_acc_in->mac.fid;
        ds_mac_hash_key.valid           = TRUE;

        /* get index of Key */
        CTC_ERROR_RETURN(DRV_HASH_LOOKUP(&lookup_info, &fib_result));
        if (fib_result.free  || fib_result.valid)
        {
            if (pl2_gb_master[lchip]->ad_bits_type)
            {
                ds_mac_hash_key.ds_ad_index2_2  = (fib_acc_in->mac.ad_index >> 2) & 0x1;
                ds_mac_hash_key.ds_ad_index1_0  = (fib_acc_in->mac.ad_index & 0x3);
                ds_mac_hash_key.ds_ad_index14_3 = (fib_acc_in->mac.ad_index >> 3);
                ds_mac_hash_key.vsi_id =  ((fib_acc_in->mac.ad_index>>15) << 13)
                                        + ((fib_acc_in->mac.fid) & 0x1FFF);
            }
            else
            {
                ds_mac_hash_key.ds_ad_index2_2  = (fib_acc_in->mac.ad_index >> 2) & 0x1;
                ds_mac_hash_key.ds_ad_index1_0  = (fib_acc_in->mac.ad_index & 0x3);
                ds_mac_hash_key.ds_ad_index14_3 = (fib_acc_in->mac.ad_index >> 3);
                ds_mac_hash_key.vsi_id =  (fib_acc_in->mac.fid) & 0x3FFF;
            }

            if (fib_result.key_index < pl2_gb_master[lchip]->hash_base)
            {
                drv_fib_cam_io(lchip, fib_result.key_index,  &ds_mac_hash_key, 0);
            }
            else
            {
                cmd = DRV_IOW(DsMacHashKey_t, DRV_ENTRY_FLAG);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, fib_result.key_index - pl2_gb_master[lchip]->hash_base, cmd, &ds_mac_hash_key));
            }
        }
        break;

    case DRV_FIB_ACC_WRITE_MAC_BY_IDX:
        ds_mac_hash_key.valid = TRUE;
        ds_mac_hash_key.is_mac_hash = TRUE;
        ds_mac_hash_key.vsi_id = fib_acc_in->mac.fid;
        ds_mac_hash_key.ds_ad_index2_2  = (fib_acc_in->mac.ad_index >> 2) & 0x1;
        ds_mac_hash_key.ds_ad_index1_0  = (fib_acc_in->mac.ad_index & 0x3);
        ds_mac_hash_key.ds_ad_index14_3 = (fib_acc_in->mac.ad_index >> 3);
        ds_mac_hash_key.mapped_mac31_0  = (fib_acc_in->mac.mac[2] << 24) + (fib_acc_in->mac.mac[3] << 16) + (fib_acc_in->mac.mac[4] << 8) + fib_acc_in->mac.mac[5];
        ds_mac_hash_key.mapped_mac47_32 = (fib_acc_in->mac.mac[0] << 8) + fib_acc_in->mac.mac[1];

        if (pl2_gb_master[lchip]->ad_bits_type)
        {
            ds_mac_hash_key.vsi_id =  ((fib_acc_in->mac.ad_index>>15) << 13)
                                    + ((fib_acc_in->mac.fid) & 0x1FFF);
        }
        else
        {
            ds_mac_hash_key.vsi_id =  (fib_acc_in->mac.fid) & 0x3FFF;
        }

        if (fib_acc_in->mac.key_index < pl2_gb_master[lchip]->hash_base)
        {
            drv_fib_cam_io(lchip, fib_acc_in->mac.key_index,  &ds_mac_hash_key, 0);
        }
        else
        {
            cmd = DRV_IOW(DsMacHashKey_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, fib_acc_in->mac.key_index - pl2_gb_master[lchip]->hash_base, cmd, &ds_mac_hash_key));
        }

        fib_result.valid  = 1;
        fib_result.free = 1;
        break;

    case DRV_FIB_ACC_LKP_MAC:

        ds_mac_hash_key.mapped_mac47_32 = (fib_acc_in->mac.mac[0] << 8) + fib_acc_in->mac.mac[1];
        ds_mac_hash_key.mapped_mac31_0  = (fib_acc_in->mac.mac[2] << 24) + (fib_acc_in->mac.mac[3] << 16) + (fib_acc_in->mac.mac[4] << 8) + fib_acc_in->mac.mac[5];
        ds_mac_hash_key.is_mac_hash     = TRUE;
        ds_mac_hash_key.vsi_id          = fib_acc_in->mac.fid;
        ds_mac_hash_key.valid           = TRUE;
        /* get index of Key */
        CTC_ERROR_RETURN(DRV_HASH_LOOKUP(&lookup_info, &fib_result));
        break;

    case DRV_FIB_ACC_READ_FIB0_BY_IDX:
    {
        drv_fib_key_get(lchip, fib_acc_in->rw.key_index, &(fib_acc_out->read.data));
    }
    break;

    default:

        break;
    }

    if (DRV_FIB_ACC_READ_FIB0_BY_IDX != acc_type)
    {
        fib_acc_out->mac.key_index = fib_result.key_index;
        fib_acc_out->mac.ad_index  = fib_result.ad_index;
        fib_acc_out->mac.hit       = fib_result.valid;
        fib_acc_out->mac.free      = fib_result.free;
        fib_acc_out->mac.conflict  = !fib_acc_out->mac.free && !fib_acc_out->mac.hit;
    }


    return CTC_E_NONE;
}
#endif
#endif

STATIC int32
_sys_greatbelt_l2_acc_lookup_mac_fid(uint8 lchip, mac_addr_t mac, uint16 fid, mac_lookup_result_t* pr)
{
    drv_fib_acc_in_t  in;
    drv_fib_acc_out_t out;

    sal_memset(&in, 0, sizeof(in));
    sal_memset(&out, 0, sizeof(out));

    in.mac.fid = fid;
    FDB_SET_HW_MAC(in.mac.mac, mac);
    CTC_ERROR_RETURN(drv_fib_acc(lchip, DRV_FIB_ACC_LKP_MAC, &in, &out));

    pr->ad_index  = out.mac.ad_index;
    pr->key_index = out.mac.key_index;
    pr->hit       = out.mac.hit;
    pr->conflict  = out.mac.conflict;

    return CTC_E_NONE;
}


STATIC int32
_sys_greatbelt_l2_fdb_calc_index(uint8 lchip, mac_addr_t mac, uint16 fid, sys_l2_calc_index_t* p_index)
{
    uint8 level_index = 0;
    uint32 cmd = 0;
    dynamic_ds_fdb_hash_ctl_t dynamic_ds_fdb_hash_ctl;
    dynamic_ds_fdb_hash_index_cam_t dynamic_ds_fdb_hash_index_cam;
    uint8  polynomial_index[FIB_HASH_LEVEL_NUM] = {0};
    uint8  bucket_num[FIB_HASH_LEVEL_NUM] = {0};
    uint8  mac_num[FIB_HASH_LEVEL_NUM] = {0};
    key_info_t key_info;
    key_result_t key_result;
    uint8 hash_key[20] = {0};
    uint8 index = 0;
    uint32 key_index = 0;
    drv_fib_acc_in_t  in;
    drv_fib_acc_out_t out;

    CTC_PTR_VALID_CHECK(p_index);
    SYS_L2UC_FID_CHECK(fid);

    sal_memset(&in, 0 ,sizeof(drv_fib_acc_in_t));
    sal_memset(&out, 0 ,sizeof(drv_fib_acc_out_t));
    sal_memset(&key_info, 0, sizeof(key_info));
    sal_memset(&key_result, 0, sizeof(key_result));
    sal_memset(&dynamic_ds_fdb_hash_ctl, 0, sizeof(dynamic_ds_fdb_hash_ctl));
    sal_memset(&dynamic_ds_fdb_hash_index_cam, 0, sizeof(dynamic_ds_fdb_hash_index_cam_t));

    cmd = DRV_IOR(DynamicDsFdbHashIndexCam_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &dynamic_ds_fdb_hash_index_cam));

    polynomial_index[0] = dynamic_ds_fdb_hash_index_cam.level0_bucket_index;
    polynomial_index[1] = dynamic_ds_fdb_hash_index_cam.level1_bucket_index;
    polynomial_index[2] = dynamic_ds_fdb_hash_index_cam.level2_bucket_index;
    polynomial_index[3] = dynamic_ds_fdb_hash_index_cam.level3_bucket_index;
    polynomial_index[4] = dynamic_ds_fdb_hash_index_cam.level4_bucket_index;

    bucket_num[0] = dynamic_ds_fdb_hash_index_cam.level0_bucket_num;
    bucket_num[1] = dynamic_ds_fdb_hash_index_cam.level1_bucket_num;
    bucket_num[2] = dynamic_ds_fdb_hash_index_cam.level2_bucket_num;
    bucket_num[3] = dynamic_ds_fdb_hash_index_cam.level3_bucket_num;
    bucket_num[4] = dynamic_ds_fdb_hash_index_cam.level4_bucket_num;

    mac_num[0] = dynamic_ds_fdb_hash_index_cam.mac_num0;
    mac_num[1] = dynamic_ds_fdb_hash_index_cam.mac_num1;
    mac_num[2] = dynamic_ds_fdb_hash_index_cam.mac_num2;
    mac_num[3] = dynamic_ds_fdb_hash_index_cam.mac_num3;
    mac_num[4] = dynamic_ds_fdb_hash_index_cam.mac_num4;

    /*data convert*/
    hash_key[11] = 0x06;
    hash_key[12] = (fid >> 8) & 0x3F;
    hash_key[13] = fid & 0xFF;
    hash_key[14] = mac[0];
    hash_key[15] = mac[1];
    hash_key[16] = mac[2];
    hash_key[17] = mac[3];
    hash_key[18] = mac[4];
    hash_key[19] = mac[5];

    key_info.hash_module = HASH_MODULE_FIB;
    key_info.key_length = FIB_CRC_DATA_WIDTH;
    key_info.p_hash_key = hash_key;

    p_index->index_cnt = 0;

    cmd = DRV_IOR(DynamicDsFdbHashCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &dynamic_ds_fdb_hash_ctl));

    for (level_index = FIB_HASH_LEVEL0; level_index <= FIB_HASH_LEVEL4; level_index++)
    {
        if (!IS_BIT_SET(dynamic_ds_fdb_hash_ctl.mac_level_bitmap, level_index))
        {
            continue;
        }

        key_info.polynomial_index = polynomial_index[level_index];
        CTC_ERROR_RETURN(drv_greatbelt_hash_calculate_index(&key_info, &key_result));

        for (index = BUCKET_ENTRY_INDEX0; index <= BUCKET_ENTRY_INDEX1; index++)
        {
            key_index = (mac_num[level_index] << 13) + ((key_result.bucket_index >> (bucket_num[level_index])) << 1)
                + index + pl2_gb_master[lchip]->hash_base;
            in.rw.key_index = key_index;
            drv_fib_acc(lchip, DRV_FIB_ACC_READ_FIB0_BY_IDX, &in, &out);
            if (out.read.data.is_mac_hash)
            {
                p_index->key_index[p_index->index_cnt++] = key_index;
            }
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l2_decode_dskey(uint8 lchip, sys_l2_info_t* psys, ds_key_t* p_dskey)
{

    psys->fid    = p_dskey->vsi_id;
    psys->mac[0] = (p_dskey->mapped_mac47_32 >> 8) & 0xFF;
    psys->mac[1] = (p_dskey->mapped_mac47_32) & 0xFF;
    psys->mac[2] = (p_dskey->mapped_mac31_0 >> 24) & 0xFF;
    psys->mac[3] = (p_dskey->mapped_mac31_0 >> 16) & 0xFF;
    psys->mac[4] = (p_dskey->mapped_mac31_0 >> 8) & 0xFF;
    psys->mac[5] = (p_dskey->mapped_mac31_0) & 0xFF;

    psys->ad_index = ((p_dskey->ds_ad_index14_3 << 3) |
                      (p_dskey->ds_ad_index2_2 << 2)  |
                      (p_dskey->ds_ad_index1_0));
    psys->key_valid = p_dskey->valid;

    if (pl2_gb_master[lchip]->ad_bits_type) /* ad take 1 bit from fid*/
    {
        psys->fid      &= 0x1FFF;
        psys->ad_index |= ((p_dskey->vsi_id >> 13) << 15);
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l2_decode_dsmac(uint8 lchip, sys_l2_info_t* psys, ds_mac_t* p_dsmac, uint32 ad_index)
{
    uint8 xgpon_en = 0;
    uint32 ad_dest_map = 0;

    CTC_PTR_VALID_CHECK(psys);
    CTC_PTR_VALID_CHECK(p_dsmac);

    psys->ad_index = ad_index;
    psys->flag.flag_ad.is_static = !p_dsmac->src_mismatch_learn_en && !p_dsmac->learn_en && !p_dsmac->aging_valid;

    psys->flag.flag_ad.aging_disable = !p_dsmac->aging_valid;

    psys->flag.flag_ad.bind_port     = p_dsmac->src_mismatch_discard;
    psys->flag.flag_ad.ucast_discard = p_dsmac->ucast_discard;
    psys->flag.flag_ad.logic_port    = p_dsmac->learn_source;
    if (psys->flag.flag_ad.logic_port)
    {
        psys->gport = p_dsmac->global_src_port;
    }
    else
    {
        psys->gport = SYS_GREATBELT_GPORT14_TO_GPORT13(p_dsmac->global_src_port);
    }
    psys->fwd_ptr             = p_dsmac->ds_fwd_ptr;
    psys->flag.flag_ad.protocol_entry = p_dsmac->protocol_exception_en;
    psys->flag.flag_ad.copy_to_cpu    = p_dsmac->mac_da_exception_en;
    psys->flag.flag_ad.src_discard    = p_dsmac->src_discard;
    psys->flag.flag_ad.ptp_entry      = p_dsmac->self_address;

    /* | tid| gport| vport| default|*/
    if (ad_index >= pl2_gb_master[lchip]->base_trunk && ad_index < pl2_gb_master[lchip]->dft_entry_base)
    {
        psys->flag.flag_node.rsv_ad_index = 1;
    }
    else if (ad_index == pl2_gb_master[lchip]->ad_index_drop)
    {
        psys->flag.flag_ad.drop = 1;
        psys->flag.flag_node.rsv_ad_index = 1;
    }
    else if ( !p_dsmac->next_hop_ptr_valid && (0xFFFF == p_dsmac->ds_fwd_ptr))
    {
        psys->flag.flag_ad.drop = 1;
    }

    if (p_dsmac->learn_en && !p_dsmac->mac_known)
    {
        psys->flag.flag_node.type_default = 1;
    }

    ad_dest_map = (p_dsmac->esp_id10_9 << 20)                      /* adDestMap[21:20] */
                         | (p_dsmac->tunnel_packet_type << 17)            /* adDestMap[19:17] */
                         | (p_dsmac->conversation_check_en << 16)         /* adDestMap[16:16] */
                         | (p_dsmac->is_conversation << 15)               /* adDestMap[15:15] */
                         | (p_dsmac->mcast_discard << 14)                 /* adDestMap[14:14] */
                         | (p_dsmac->equal_cost_path_num3_3 << 12)        /* adDestMap[12:13] */
                         | ((p_dsmac->equal_cost_path_num2_0 >> 2) << 11) /* adDestMap[11:11] */
                         | (p_dsmac->ucast_discard << 10)                 /* adDestMap[10:10] */
                         | (p_dsmac->aps_select_protecting_path << 9)     /* adDestMap[9:9] */
                         | (p_dsmac->esp_id8_0);                          /* adDestMap[8:0] */
    /*if mcast is drop, nexhtop ptr valid is 0*/
    if ((psys->flag.flag_ad.is_static) && (p_dsmac->next_hop_ptr_valid || psys->flag.flag_ad.drop))
    {
        if (1 == (ad_dest_map >> 21))
        {
            psys->flag.flag_node.type_l2mc = 1;
            psys->mc_gid         = (ad_dest_map &0xFFFF);
        }
    }

    sys_greatbelt_global_get_xgpon_en(lchip, &xgpon_en);
    if (xgpon_en && (p_dsmac->next_hop_ptr_valid))
    {
        psys->gport = SYS_GREATBELT_DESTMAP_TO_GPORT(ad_dest_map);
    }

    psys->flag.flag_ad.src_discard_to_cpu = p_dsmac->src_discard && p_dsmac->mac_sa_exception_en;

/*
    at current nexthop, below is not supported. but it may require to support in the future.
    uint32 nhid = 0;
    _sys_get_nhid_by_fwdptr(psys->fwd_ptr, &nhid);
    if (!_sys_is_nhid_basic(nhid))
    {
        psys->with_nh         = 1;
        psys->nhid            = nhid;
    }
 */

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l2_encode_dsmac(uint8 lchip, ds_mac_t* p_dsmac, sys_l2_info_t* psys)
{
    uint8  gchip               = 0;
    uint8  gchip1              = 0;
    uint16 group_id            = 0;
    uint8  equal_cost_path_num = 0;
    uint32 cmd                 = 0;
    uint32 value               = 0;

    CTC_PTR_VALID_CHECK(psys);
    CTC_PTR_VALID_CHECK(p_dsmac);

    sal_memset(p_dsmac, 0, sizeof(ds_mac_t));

    if (!psys->flag.flag_node.type_l2mc && !psys->flag.flag_node.type_default)
    {
        if (psys->with_nh)
        {
            if (psys->flag.flag_node.ecmp_valid)
            {
                SYS_FDB_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Ecmp number is %d\r\n", psys->ecmp_num);
                equal_cost_path_num             = psys->ecmp_num - 1;
                p_dsmac->equal_cost_path_num2_0 = (equal_cost_path_num & 0x7);
                p_dsmac->equal_cost_path_num3_3 = CTC_IS_BIT_SET(equal_cost_path_num, 3);
            }
        }

        p_dsmac->source_port_check_en = 1;

        if (psys->flag.flag_node.white_list)
        {
            p_dsmac->src_mismatch_learn_en = 1;
            p_dsmac->aging_valid           = 0;
            p_dsmac->aging_index           = 0;
        }
        else if (!psys->flag.flag_ad.is_static && (psys->gport != DISCARD_PORT))
        {
            p_dsmac->src_mismatch_learn_en = 1;
            p_dsmac->aging_valid           = 1;
            p_dsmac->aging_index           = 0;
        }
        if(psys->flag.flag_ad.aging_disable)
        {
            p_dsmac->aging_valid           = 0;
        }

        if(!psys->flag.flag_ad.logic_port)
        {
            gchip1 = SYS_MAP_GPORT_TO_GCHIP(psys->gport);
            sys_greatbelt_get_gchip_id(lchip, &gchip);
        }

        /*for logic port and linkagg, aging is set by aging flag,
            for gport aging disable flag en when gport is not chip*/
        if (psys->flag.flag_ad.aging_disable
            && ((gchip != gchip1) || (psys->flag.flag_ad.logic_port)
                || CTC_IS_LINKAGG_PORT(psys->gport) || (psys->with_nh)))
        {
            p_dsmac->aging_valid           = 0;
            p_dsmac->aging_index           = 0;
        }


        if (psys->flag.flag_ad.bind_port)
        {
            p_dsmac->src_mismatch_discard = 1;
        }

        if (psys->flag.flag_ad.ucast_discard)
        {
            p_dsmac->ucast_discard = 1;
        }

        if (psys->flag.flag_ad.logic_port)
        {
            SYS_FDB_DBG_INFO(" DsMac.logic_port 0x%x\n", psys->gport);
            p_dsmac->learn_source    = 1;
            p_dsmac->global_src_port = psys->gport;
        }
        else
        {
            SYS_FDB_DBG_INFO(" DsMac.phy_port 0x%x\n", psys->gport);
            p_dsmac->learn_source    = 0;
            p_dsmac->global_src_port = SYS_GREATBELT_GPORT13_TO_GPORT14(psys->gport);
        }
        p_dsmac->ds_fwd_ptr = psys->fwd_ptr;
        p_dsmac->mac_known = 1;

#if 0
        if (p_mac_da->next_hop_ptr_valid)
        {
            pkt_info->ds_fwd_ptr_valid = FALSE;
            pkt_info->next_hop_ptr_valid = TRUE;
            pkt_info->ad_next_hop_ptr = p_mac_da->ds_fwd_ptr;                       /* used as adNextHopPtr */
            pkt_info->ad_dest_map = (p_mac_da->esp_id10_9 << 20)                    /* used as adDestMap[21:20] */
                                    | (p_mac_da->tunnel_packet_type << 17)          /* used as adDestMap[19:17] */
                                    | (p_mac_da->conversation_check_en << 16)       /* used as adDestMap[16:16] */
                                    | (p_mac_da->is_conversation << 15)             /* used as adDestMap[15:15] */
                                    | (p_mac_da->mcast_discard << 14)       /* used as adDestMap[14:14] */
                                    | (p_mac_da->equal_cost_path_num3_3 << 12)         /* used as adDestMap[12:12] */
                                    | ((p_mac_da->equal_cost_path_num2_0 >>2) << 11)         /* used as adDestMap[11:11] */
                                    | (p_mac_da->ucast_discard << 10)               /* used as adDestMap[10:10] */
                                    | (p_mac_da->aps_select_protecting_path << 9)   /* used as adDestMap[9:9] */
                                    | (p_mac_da->esp_id8_0);                        /* used as adDestMap[8:0] */
            pkt_info->ad_length_adjust_type = p_mac_da->payload_select;             /* used as adLengthAdjustType */
            pkt_info->ad_critical_packet = p_mac_da->stp_check_disable;             /* used as adCriticalPacket */
            pkt_info->ad_next_hop_ext = p_mac_da->bridge_aps_select_en;             /* used as adNextHopExt */
            pkt_info->ad_send_local_phy_port = p_mac_da->src_discard;               /* used as adSendLocalPhyPort */
            pkt_info->ad_aps_type = (p_mac_da->ad_aps_type1 << 1) | p_mac_da->priority_path_en; /* used as adApsType0 */
            pkt_info->ad_speed = p_mac_da->exception_sub_index;                     /* used as adSpeed */
        }
#endif

        if (!psys->dsfwd_valid)
        {
            sys_nh_info_dsnh_t nh_info;
            uint16   dest_id = 0;
            sal_memset(&nh_info, 0, sizeof(sys_nh_info_dsnh_t));
            CTC_ERROR_RETURN(sys_greatbelt_nh_get_nhinfo(lchip, psys->nhid, &nh_info));
            dest_id = nh_info.dest_id;

            if(!CTC_IS_LINKAGG_PORT(psys->gport))
            {
                 /*sys_greatbelt_get_gchip_id(lchip, &gchip);*/
                gchip = nh_info.dest_chipid;
            }
            else
            {
                gchip = 0x1f;
            }
            p_dsmac->next_hop_ptr_valid = 1;
            p_dsmac->ds_fwd_ptr         = nh_info.dsnh_offset;               /*nexthopPtr[15:0]*/

            /* adApsType0 */
            if(nh_info.aps_en)
            {
                p_dsmac->ad_aps_type1 = CTC_APS_BRIDGE >> 1;
                p_dsmac->priority_path_en = CTC_APS_BRIDGE & 0x1;
            }

            /* dest_id -->destmap[21:0]: ucast,destChipId[4:0],destId[15:0] */
            p_dsmac->esp_id10_9                 = 0x0 << 1;                        /*adDestMap[21]*/
            p_dsmac->esp_id10_9                |= (gchip >> 4) & 0x1;              /*adDestMap[20],destchip[4]*/
            p_dsmac->tunnel_packet_type         = (gchip >> 1) & 0x7;              /*adDestMap[19:17],destchip[3:1]*/
            p_dsmac->conversation_check_en      = gchip & 0x1;                     /*adDestMap[16],destchip[0]*/
            p_dsmac->is_conversation            = (dest_id >> 15) & 0x1;          /*adDestMap[15],destId[15]*/
            p_dsmac->mcast_discard              = (dest_id >> 14) & 0x1;          /*adDestMap[14],destId[14]*/
            p_dsmac->equal_cost_path_num3_3     = (dest_id >> 12) & 0x3;          /*adDestMap[12],destId[12:13]*/
            p_dsmac->equal_cost_path_num2_0    &= 3;
            p_dsmac->equal_cost_path_num2_0    |= (((dest_id >> 11) & 0x1) << 2); /*adDestMap[11],destId[11]*/
            p_dsmac->ucast_discard              = (dest_id >> 10) & 0x1;          /*adDestMap[10],destId[10]*/
            p_dsmac->aps_select_protecting_path = (dest_id >> 9) & 0x1;           /*adDestMap[9],destId[9]*/
            p_dsmac->esp_id8_0                  = dest_id & 0x1FF;                /*adDestMap[8:0],destId[8:0]*/

            p_dsmac->bridge_aps_select_en = nh_info.nexthop_ext;             /*adNextHopExt */


            /* speed[1:0] */
            if(sys_greatbelt_get_cut_through_en(lchip))
            {
                uint16 gport = 0;
                uint32 dest_map = 0;
                uint8 tmp_speed = 0;

                sys_greatbelt_get_cut_through_speed(lchip, gport);

                if(!nh_info.aps_en && (CTC_LINKAGG_CHIPID!= nh_info.dest_chipid))
                {
                    dest_map = SYS_NH_ENCODE_DESTMAP(0, nh_info.dest_chipid, nh_info.dest_id);
                    gport = SYS_GREATBELT_DESTMAP_TO_GPORT(dest_map);
                    tmp_speed = sys_greatbelt_get_cut_through_speed(lchip, gport);
                }
                else
                {
                    tmp_speed   = 0;
                }

                p_dsmac->exception_sub_index |= tmp_speed&0x3;
            }

            SYS_FDB_DBG_INFO("gchip=%d, dest_id=0x%x\n", gchip, dest_id);
        }
    }
    else if (psys->flag.flag_node.type_default && psys->revert_default)
    {
        p_dsmac->ds_fwd_ptr            = SYS_FDB_DISCARD_FWD_PTR;
        p_dsmac->protocol_exception_en = 1;
        p_dsmac->storm_ctl_en          = 1;
    }
    else if (psys->flag.flag_node.type_default && !psys->revert_default)
    {
        if (CTC_FLAG_ISSET(psys->fid_flag, CTC_L2_DFT_VLAN_FLAG_USE_LOGIC_PORT))
        {
            p_dsmac->learn_source = 1;
        }

        if (CTC_FLAG_ISSET(psys->fid_flag, CTC_L2_DFT_VLAN_FLAG_PROTOCOL_EXCP_TOCPU))
        {
            p_dsmac->protocol_exception_en = 1;
        }

        p_dsmac->learn_en  = 1;
        if (CTC_FLAG_ISSET(psys->fid_flag, CTC_L2_DFT_VLAN_FLAG_LEARNING_DISABLE))
        {
            p_dsmac->learn_en  = 0;
        }
        if (CTC_FLAG_ISSET(psys->fid_flag, CTC_L2_DFT_VLAN_FLAG_VLAN_MAC_AUTH))
        {
            p_dsmac->src_discard  = 1;
            cmd = DRV_IOR(DsMac_t, DsMac_MacSaExceptionEn_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, psys->ad_index, cmd, &value));
            p_dsmac->mac_sa_exception_en = value;
        }
        if (CTC_FLAG_ISSET(psys->fid_flag, CTC_L2_DFT_VLAN_FLAG_PORT_MAC_AUTH))
        {
            p_dsmac->src_mismatch_discard  = 1;
            p_dsmac->global_src_port = SYS_RESERVE_PORT_ID_CPU;
        }

        p_dsmac->mac_known = 0;
        if (!psys->dsfwd_valid)
        {
            group_id = psys->mc_gid;
            sys_greatbelt_get_gchip_id(lchip, &gchip);
            p_dsmac->next_hop_ptr_valid = 1;
            p_dsmac->ds_fwd_ptr         = 0;                                       /*nexthopPtr[15:0]*/

            /* l2mc_grp_Id -->destmap[21:0]: mcast,destChipId[4:0],destId[15:0] */
            p_dsmac->esp_id10_9                 = 0x1 << 1;                        /*adDestMap[21]*/
            p_dsmac->esp_id10_9                |= (gchip >> 4) & 0x1;              /*adDestMap[20],destchip[4]*/
            p_dsmac->tunnel_packet_type         = (gchip >> 1) & 0x7;              /*adDestMap[19:17],destchip[3:1]*/
            p_dsmac->conversation_check_en      = gchip & 0x1;                     /*adDestMap[16],destchip[0]*/
            p_dsmac->is_conversation            = (group_id >> 15) & 0x1;          /*adDestMap[15],destId[15]*/
            p_dsmac->mcast_discard              = (group_id >> 14) & 0x1;          /*adDestMap[14],destId[14]*/
            p_dsmac->equal_cost_path_num3_3     = (group_id >> 12) & 0x3;          /*adDestMap[12],destId[12:13]*/
            p_dsmac->equal_cost_path_num2_0    &= 3;
            p_dsmac->equal_cost_path_num2_0    |= (((group_id >> 11) & 0x1) << 2); /*adDestMap[11],destId[11]*/
            p_dsmac->ucast_discard              = (group_id >> 10) & 0x1;          /*adDestMap[10],destId[10]*/
            p_dsmac->aps_select_protecting_path = (group_id >> 9) & 0x1;           /*adDestMap[9],destId[9]*/
            p_dsmac->esp_id8_0                  = group_id & 0x1FF;                /*adDestMap[8:0],destId[8:0]*/

            if(sys_greatbelt_get_cut_through_en(lchip))
            {/*mcast set to 10G*/
                p_dsmac->equal_cost_path_num2_0    |= 3;
            }

            SYS_FDB_DBG_INFO("gchip=%d, group_id=0x%x\n", gchip, group_id);
        }
        else
        {
            p_dsmac->ds_fwd_ptr         = psys->fwd_ptr;
            p_dsmac->next_hop_ptr_valid = 0;
            p_dsmac->ucast_discard      = psys->flag.flag_ad.ucast_discard;
            p_dsmac->mcast_discard      = psys->flag.flag_ad.mcast_discard;
        }
    }
    else  /* (psys->flag.type_l2mc)*/
    {
        p_dsmac->mac_known = 1;

        if (psys->flag.flag_ad.ptp_entry)
        {
            p_dsmac->self_address = 1;
        }

        group_id = psys->mc_gid;
        sys_greatbelt_get_gchip_id(lchip, &gchip);
        p_dsmac->next_hop_ptr_valid = 1;
        p_dsmac->ds_fwd_ptr         = 0; /*nexthopPtr[15:0]*/

        /* l2mc_grp_Id -->destmap[21:0]: mcast,destChipId[4:0],destId[15:0] */
        p_dsmac->esp_id10_9                 = 0x1 << 1;                        /*adDestMap[21]*/
        p_dsmac->esp_id10_9                |= (gchip >> 4) & 0x1;              /*adDestMap[20],destchip[4]*/
        p_dsmac->tunnel_packet_type         = (gchip >> 1) & 0x7;              /*adDestMap[19:17],destchip[3:1]*/
        p_dsmac->conversation_check_en      = gchip & 0x1;                     /*adDestMap[16],destchip[0]*/
        p_dsmac->is_conversation            = (group_id >> 15) & 0x1;          /*adDestMap[15],destId[15]*/
        p_dsmac->mcast_discard              = (group_id >> 14) & 0x1;          /*adDestMap[14],destId[14]*/
        p_dsmac->equal_cost_path_num3_3     = (group_id >> 12) & 0x3;          /*adDestMap[12],destId[12:13]*/
        p_dsmac->equal_cost_path_num2_0    &= 3;
        p_dsmac->equal_cost_path_num2_0    |= (((group_id >> 11) & 0x1) << 2); /*adDestMap[11],destId[11]*/
        p_dsmac->ucast_discard              = (group_id >> 10) & 0x1;          /*adDestMap[10],destId[10]*/
        p_dsmac->aps_select_protecting_path = (group_id >> 9) & 0x1;           /*adDestMap[9],destId[9]*/
        p_dsmac->esp_id8_0                  = group_id & 0x1FF;                /*adDestMap[8:0],destId[8:0]*/

        if(sys_greatbelt_get_cut_through_en(lchip))
        {/*mcast set to 10G*/
            p_dsmac->equal_cost_path_num2_0    |= 3;
        }
        SYS_FDB_DBG_INFO("gchip=%d, group_id=0x%x\n", gchip, group_id);
    }

    p_dsmac->fast_learning_en = 1;

    if (psys->flag.flag_ad.drop)
    {
        p_dsmac->next_hop_ptr_valid = 0;
        p_dsmac->ds_fwd_ptr         = SYS_FDB_DISCARD_FWD_PTR;
    }

    if (psys->flag.flag_ad.src_discard)
    {
        p_dsmac->src_discard         = 1;
        p_dsmac->mac_sa_exception_en = 0;
    }

    if (psys->flag.flag_ad.src_discard_to_cpu)
    {
        p_dsmac->src_discard         = 1;
        p_dsmac->mac_sa_exception_en = 1;
    }

    if (psys->flag.flag_ad.copy_to_cpu)
    {
        p_dsmac->mac_da_exception_en = 1;
        /* Reserved for normal MACDA copy_to_cpu */
        /* #define SYS_L2PDU_PER_PORT_ACTION_INDEX_RSV_MACDA_TO_CPU 63 (-60)*/
        p_dsmac->exception_sub_index = 3;
    }

    if (psys->flag.flag_ad.protocol_entry)
    {
        p_dsmac->protocol_exception_en = 1;
    }

    p_dsmac->storm_ctl_en = 1;

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l2_map_sys_to_sw(uint8 lchip, sys_l2_info_t * psys, sys_l2_node_t * pn, sys_l2_ad_spool_t* pa)
{
    if (pn) /* no map key */
    {
        pn->key.fid = psys->fid;
        FDB_SET_MAC(pn->key.mac, psys->mac);
    }

    pa->gport     = psys->gport;
    pa->flag = psys->flag.flag_ad;
    pa->u0.nhid   = psys->nhid;
     /*pa->u0.fwdptr = psys->fwd_ptr;*/

    return CTC_E_NONE;
}

void*
_sys_greatbelt_l2_fid_node_lkup(uint8 lchip, uint16 fid, uint8 type)
{
    sys_l2_fid_node_t* p_lkup;

    p_lkup = ctc_vector_get(pl2_gb_master[lchip]->fid_vec, fid);

    if (GET_FID_NODE == type)
    {
        return p_lkup;
    }
    else if (GET_FID_LIST == type)
    {
        return (p_lkup) ? (p_lkup->fid_list) : NULL;
    }

    return NULL;
}


STATIC sys_l2_node_t*
_sys_greatbelt_l2_fdb_hash_lkup(uint8 lchip, mac_addr_t mac, uint16 fid)
{
    sys_l2_node_t node;

    sal_memset(&node, 0, sizeof(node));

    node.key.fid = fid;
    FDB_SET_MAC(node.key.mac, mac);

    return ctc_hash_lookup(pl2_gb_master[lchip]->fdb_hash, &node);
}


STATIC int32
_sys_greatbelt_l2_unmap_flag(uint8 lchip, uint32* ctc_out, sys_l2_flag_t* p_flag)
{
    uint32 ctc_flag = 0;

    if (p_flag->flag_ad.is_static)
    {
        CTC_SET_FLAG(ctc_flag, CTC_L2_FLAG_IS_STATIC);
    }
    if (p_flag->flag_node.remote_dynamic)
    {
        CTC_SET_FLAG(ctc_flag, CTC_L2_FLAG_REMOTE_DYNAMIC);
    }
    if (p_flag->flag_ad.drop)
    {
        CTC_SET_FLAG(ctc_flag, CTC_L2_FLAG_DISCARD);
    }
    if (p_flag->flag_ad.ptp_entry)
    {
        CTC_SET_FLAG(ctc_flag, CTC_L2_FLAG_PTP_ENTRY);
    }
    if (p_flag->flag_node.system_mac)
    {
        CTC_SET_FLAG(ctc_flag, CTC_L2_FLAG_SYSTEM_RSV);
    }
    if (p_flag->flag_ad.copy_to_cpu)
    {
        CTC_SET_FLAG(ctc_flag, CTC_L2_FLAG_COPY_TO_CPU);
    }
    if (p_flag->flag_ad.protocol_entry)
    {
        CTC_SET_FLAG(ctc_flag, CTC_L2_FLAG_PROTOCOL_ENTRY);
    }
    if (p_flag->flag_ad.ucast_discard)
    {
        CTC_SET_FLAG(ctc_flag, CTC_L2_FLAG_UCAST_DISCARD);
    }
    if (p_flag->flag_ad.bind_port)
    {
        CTC_SET_FLAG(ctc_flag, CTC_L2_FLAG_BIND_PORT);
    }
    if (p_flag->flag_ad.src_discard)
    {
        CTC_SET_FLAG(ctc_flag, CTC_L2_FLAG_SRC_DISCARD);
    }
    if (p_flag->flag_ad.src_discard_to_cpu)
    {
        CTC_SET_FLAG(ctc_flag, CTC_L2_FLAG_SRC_DISCARD_TOCPU);
    }
    if (p_flag->flag_node.white_list)
    {
        CTC_SET_FLAG(ctc_flag, CTC_L2_FLAG_WHITE_LIST_ENTRY);
    }
    if (p_flag->flag_ad.aging_disable)
    {
        CTC_SET_FLAG(ctc_flag, CTC_L2_FLAG_AGING_DISABLE);
    }

    *ctc_out = ctc_flag;
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l2_map_flag(uint8 lchip, uint8 type, void* l2, sys_l2_flag_t* p_flag)
{
    SYS_FDB_DBG_FUNC();

    if ((L2_TYPE_UC == type) || (L2_TYPE_TCAM == type))
    {
        ctc_l2_addr_t     * l2_addr  = (ctc_l2_addr_t *) l2;
        sys_l2_fid_node_t * fid_node = NULL;
        uint32            ctc_flag   = l2_addr->flag;

        if (CTC_FLAG_ISSET(ctc_flag, CTC_L2_FLAG_IS_STATIC)
            && CTC_FLAG_ISSET(ctc_flag, CTC_L2_FLAG_REMOTE_DYNAMIC))
        {
            return CTC_E_INVALID_PARAM;
        }

        if (CTC_FLAG_ISSET(ctc_flag, CTC_L2_FLAG_IS_STATIC)
            && CTC_FLAG_ISSET(ctc_flag, CTC_L2_FLAG_AGING_DISABLE))
        {
            return CTC_E_INVALID_PARAM;
        }

        if (L2_TYPE_TCAM == type)
        {
            p_flag->flag_ad.is_static          = 1;
            p_flag->flag_node.is_tcam            = 1;
        }
        p_flag->flag_ad.is_static          = (CTC_FLAG_ISSET(ctc_flag, CTC_L2_FLAG_IS_STATIC));
        p_flag->flag_node.remote_dynamic     = (CTC_FLAG_ISSET(ctc_flag, CTC_L2_FLAG_REMOTE_DYNAMIC));
        p_flag->flag_ad.drop               = (CTC_FLAG_ISSET(ctc_flag, CTC_L2_FLAG_DISCARD));
        p_flag->flag_ad.ptp_entry          = (CTC_FLAG_ISSET(ctc_flag, CTC_L2_FLAG_PTP_ENTRY));
        p_flag->flag_node.system_mac         = (CTC_FLAG_ISSET(ctc_flag, CTC_L2_FLAG_SYSTEM_RSV));
        p_flag->flag_ad.copy_to_cpu        = (CTC_FLAG_ISSET(ctc_flag, CTC_L2_FLAG_COPY_TO_CPU));
        p_flag->flag_ad.protocol_entry     = (CTC_FLAG_ISSET(ctc_flag, CTC_L2_FLAG_PROTOCOL_ENTRY));
        p_flag->flag_ad.ucast_discard      = (CTC_FLAG_ISSET(ctc_flag, CTC_L2_FLAG_UCAST_DISCARD));
        p_flag->flag_ad.bind_port          = (CTC_FLAG_ISSET(ctc_flag, CTC_L2_FLAG_BIND_PORT));
        p_flag->flag_ad.src_discard        = (CTC_FLAG_ISSET(ctc_flag, CTC_L2_FLAG_SRC_DISCARD));
        p_flag->flag_ad.src_discard_to_cpu = (CTC_FLAG_ISSET(ctc_flag, CTC_L2_FLAG_SRC_DISCARD_TOCPU));
        p_flag->flag_node.white_list         = (CTC_FLAG_ISSET(ctc_flag, CTC_L2_FLAG_WHITE_LIST_ENTRY));
        p_flag->flag_ad.aging_disable      = (CTC_FLAG_ISSET(ctc_flag, CTC_L2_FLAG_AGING_DISABLE));

        fid_node = _sys_greatbelt_l2_fid_node_lkup(lchip, l2_addr->fid, GET_FID_NODE);

        if (fid_node)
        {
            p_flag->flag_ad.logic_port = (CTC_FLAG_ISSET(fid_node->flag, CTC_L2_DFT_VLAN_FLAG_USE_LOGIC_PORT));
        }
    }
    else if (L2_TYPE_MC == type)
    {
        ctc_l2_mcast_addr_t* l2_addr = (ctc_l2_mcast_addr_t *) l2;
        uint32             ctc_flag  = l2_addr->flag;


        /* mc support these action.*/
        CTC_UNSET_FLAG(ctc_flag, CTC_L2_FLAG_DISCARD);
        CTC_UNSET_FLAG(ctc_flag, CTC_L2_FLAG_SRC_DISCARD);
        CTC_UNSET_FLAG(ctc_flag, CTC_L2_FLAG_SRC_DISCARD_TOCPU);
        CTC_UNSET_FLAG(ctc_flag, CTC_L2_FLAG_COPY_TO_CPU);
        CTC_UNSET_FLAG(ctc_flag, CTC_L2_FLAG_PROTOCOL_ENTRY);
        CTC_UNSET_FLAG(ctc_flag, CTC_L2_FLAG_PTP_ENTRY);
        if (ctc_flag)
        {
            return CTC_E_INVALID_PARAM;
        }

        ctc_flag                   = l2_addr->flag;
        p_flag->flag_ad.is_static          = 1;
        p_flag->flag_ad.drop               = (CTC_FLAG_ISSET(ctc_flag, CTC_L2_FLAG_DISCARD));
        p_flag->flag_ad.ptp_entry          = (CTC_FLAG_ISSET(ctc_flag, CTC_L2_FLAG_PTP_ENTRY));
        p_flag->flag_ad.copy_to_cpu        = (CTC_FLAG_ISSET(ctc_flag, CTC_L2_FLAG_COPY_TO_CPU));
        p_flag->flag_ad.protocol_entry     = (CTC_FLAG_ISSET(ctc_flag, CTC_L2_FLAG_PROTOCOL_ENTRY));
        p_flag->flag_ad.src_discard        = (CTC_FLAG_ISSET(ctc_flag, CTC_L2_FLAG_SRC_DISCARD));
        p_flag->flag_ad.src_discard_to_cpu = (CTC_FLAG_ISSET(ctc_flag, CTC_L2_FLAG_SRC_DISCARD_TOCPU));
    }
    return CTC_E_NONE;
}


STATIC int32
_sys_greatbelt_l2_flush_by_hw(uint8 lchip, ctc_l2_flush_fdb_t* p_flush);

/* ret < 0 , traverse stop*/
STATIC int32
_sys_greatbelt_l2_fid_vec_cb(sys_l2_fid_node_t* fid, uint32 index, void* flush_info)
{
    sys_l2_flush_t *p_flush_info = flush_info;
    uint8 lchip = 0;
    ctc_l2_flush_fdb_t flush;

   CTC_PTR_VALID_CHECK(p_flush_info);

    lchip = p_flush_info->lchip;

    sal_memset(&flush, 0, sizeof(ctc_l2_flush_fdb_t));
    flush.flush_type = CTC_L2_FDB_ENTRY_OP_BY_VID;
    flush.flush_flag = p_flush_info->flush_flag;
    flush.fid        = index;

    return _sys_greatbelt_l2_flush_by_hw(lchip, &flush);
}

STATIC int32
_sys_greatbelt_l2_flush_by_hw(uint8 lchip, ctc_l2_flush_fdb_t* p_flush)
{
    drv_fib_acc_in_t  acc_in;
    drv_fib_acc_out_t acc_out;
    uint8             fib_acc_type = 0;
    sal_memset(&acc_in, 0, sizeof(acc_in));
    sal_memset(&acc_out, 0, sizeof(acc_out));

    switch (p_flush->flush_type)
    {
    /* maybe need to add flush by port +vid */

    case CTC_L2_FDB_ENTRY_OP_BY_VID:
        fib_acc_type          = DRV_FIB_ACC_FLUSH_MAC_BY_PORT_VLAN;
        acc_in.mac_flush.fid  = p_flush->fid;
        acc_in.mac_flush.flag = DRV_FIB_ACC_FLUSH_BY_VSI;

        break;

    case CTC_L2_FDB_ENTRY_OP_BY_PORT:
        fib_acc_type          = DRV_FIB_ACC_FLUSH_MAC_BY_PORT_VLAN;
        acc_in.mac_flush.flag = DRV_FIB_ACC_FLUSH_BY_PORT;
        if (p_flush->use_logic_port)
        {
            acc_in.mac_flush.flag |= DRV_FIB_ACC_FLUSH_LOGIC_PORT;
            acc_in.mac_flush.port = p_flush->gport;
        }
        else
        {
            acc_in.mac_flush.port = SYS_GREATBELT_GPORT13_TO_GPORT14(p_flush->gport);
        }
        break;

    case CTC_L2_FDB_ENTRY_OP_BY_PORT_VLAN:
        fib_acc_type          = DRV_FIB_ACC_FLUSH_MAC_BY_PORT_VLAN;
        acc_in.mac_flush.fid  = p_flush->fid;

        acc_in.mac_flush.flag = DRV_FIB_ACC_FLUSH_BY_VSI | DRV_FIB_ACC_FLUSH_BY_PORT;
        if (p_flush->use_logic_port)
        {
            acc_in.mac_flush.flag |= DRV_FIB_ACC_FLUSH_LOGIC_PORT;
            acc_in.mac_flush.port = p_flush->gport;
        }
        else
        {
            acc_in.mac_flush.port = SYS_GREATBELT_GPORT13_TO_GPORT14(p_flush->gport);
        }
        break;

    case CTC_L2_FDB_ENTRY_OP_BY_MAC_VLAN:
        fib_acc_type = DRV_FIB_ACC_DEL_MAC_BY_KEY;
        FDB_SET_HW_MAC(acc_in.mac.mac, p_flush->mac);
        acc_in.mac.fid = p_flush->fid;
        break;

    case CTC_L2_FDB_ENTRY_OP_ALL:
     {
        sys_l2_flush_t flush_info;

        /*flush by all is flush all fid no need fib acc anymore, so return*/
        flush_info.flush_flag = p_flush->flush_flag;
        flush_info.lchip = lchip;
        ctc_vector_traverse2(pl2_gb_master[lchip]->fid_vec, 0,
            (vector_traversal_fn2)_sys_greatbelt_l2_fid_vec_cb,
            &flush_info);
        return CTC_E_NONE;
    }
    default:
        return CTC_E_NOT_SUPPORT;
    }

    if (DRV_FIB_ACC_FLUSH_MAC_BY_PORT_VLAN == fib_acc_type)
    {
        switch (p_flush->flush_flag)
        {
        case CTC_L2_FDB_ENTRY_STATIC:
            acc_in.mac_flush.flag |= DRV_FIB_ACC_FLUSH_STATIC;
            break;

        case CTC_L2_FDB_ENTRY_DYNAMIC:
            acc_in.mac_flush.flag |= DRV_FIB_ACC_FLUSH_DYNAMIC;
            break;

        case CTC_L2_FDB_ENTRY_ALL:
            acc_in.mac_flush.flag |= DRV_FIB_ACC_FLUSH_STATIC + DRV_FIB_ACC_FLUSH_DYNAMIC;
            break;

        default:
            return CTC_E_NOT_SUPPORT;
        }
    }


        CTC_ERROR_RETURN(drv_fib_acc(lchip, fib_acc_type, &acc_in, &acc_out));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l2_delete_hw_by_mac_fid(uint8 lchip, mac_addr_t mac, uint16 fid)
{
    /*remove fail not return for syn with system*/
    ctc_l2_flush_fdb_t flush_fdb;
    sal_memset(&flush_fdb, 0, sizeof(ctc_l2_flush_fdb_t));
    FDB_SET_MAC(flush_fdb.mac, mac);
    flush_fdb.fid        = fid;
    flush_fdb.flush_type = CTC_L2_FDB_ENTRY_OP_BY_MAC_VLAN;
    flush_fdb.flush_flag = CTC_L2_FDB_ENTRY_ALL;
    return _sys_greatbelt_l2_flush_by_hw(lchip, &flush_fdb);
}

STATIC void
_sys_greatbelt_l2_update_count(uint8 lchip, sys_l2_flag_ad_t* pflag_a, sys_l2_flag_node_t* pflag_n, int32 do_increase, uint8 type, void* data)
{
    uint32* p_dynamic_count       = NULL;
    uint32* p_local_dynamic_count = NULL;

    if (L2_COUNT_GLOBAL == type)
    {
        uint32 key_index = *(uint32 *) data;
        p_dynamic_count       = &(pl2_gb_master[lchip]->dynamic_count);
        p_local_dynamic_count = &(pl2_gb_master[lchip]->local_dynamic_count);

        pl2_gb_master[lchip]->total_count += do_increase;
        if (pflag_n->type_l2mc)
        {
            pl2_gb_master[lchip]->l2mc_count += do_increase;
        }
        if (key_index < pl2_gb_master[lchip]->hash_base)
        {
            pl2_gb_master[lchip]->cam_count += do_increase;
        }
    }
    else if (L2_COUNT_PORT_LIST == type) /* mc won't get to this */
    {
        p_dynamic_count       = &(((sys_l2_port_node_t *) data)->dynamic_count);
        p_local_dynamic_count = &(((sys_l2_port_node_t *) data)->local_dynamic_count);
    }
    else if (L2_COUNT_FID_LIST == type) /* mc won't get to this */
    {
        p_dynamic_count       = &(((sys_l2_fid_node_t *) data)->dynamic_count);
        p_local_dynamic_count = &(((sys_l2_fid_node_t *) data)->local_dynamic_count);
    }

    if (!pflag_n->type_l2mc && !pflag_a->is_static)
    {
        *p_dynamic_count += (do_increase);
        if (!pflag_n->remote_dynamic)
        {
            *p_local_dynamic_count += (do_increase);
        }
    }

    return;
}

/* based on node, so require has_sw = 1*/
STATIC int32
_sys_greatbelt_l2_map_sw_to_entry(uint8 lchip, sys_l2_node_t* p_l2_node,
                                  ctc_l2_fdb_query_rst_t* pr,
                                  sys_l2_detail_t * pi,
                                  uint32* count)
{
    uint32 index = *count;
    sys_l2_flag_t flag;

    sal_memset(&flag, 0, sizeof(sys_l2_flag_t));
    flag.flag_ad = p_l2_node->adptr->flag;
    flag.flag_node = p_l2_node->flag;
    (pr->buffer[index]).is_logic_port = p_l2_node->adptr->flag.logic_port;
    (pr->buffer[index]).fid   = p_l2_node->key.fid;
    (pr->buffer[index]).gport = p_l2_node->adptr->gport;

    _sys_greatbelt_l2_unmap_flag(lchip, &pr->buffer[index].flag, &flag);

    FDB_SET_MAC((pr->buffer[index]).mac, p_l2_node->key.mac);

    if (pi)
    {
        pi[index].key_index = p_l2_node->key_index - pl2_gb_master[lchip]->hash_base;
        pi[index].ad_index  = p_l2_node->adptr->index;
        pi[index].flag.flag_ad = p_l2_node->adptr->flag;
        pi[index].flag.flag_node = p_l2_node->flag;
    }

    (*count)++;
    return CTC_E_NONE;
}


STATIC int32
_sys_greatbelt_l2_get_entry_by_list(uint8 lchip, ctc_linklist_t * fdb_list,
                                    uint8 query_flag,
                                    ctc_l2_fdb_query_rst_t * pr,
                                    sys_l2_detail_t * pi,
                                    uint8 extra_port,
                                    uint16 gport,
                                    uint32* all_index)
{
    ctc_listnode_t * listnode  = NULL;
    uint32         start_idx   = 0;
    uint32         count       = 0;
    uint32         max_count   = pr->buffer_len / sizeof(ctc_l2_addr_t);
    sys_l2_node_t  * p_l2_node = NULL;

    if (all_index)
    {
        count = *all_index;
    }

    listnode = CTC_LISTHEAD(fdb_list);
    while (listnode != NULL && start_idx < pr->start_index)
    {
        CTC_NEXTNODE(listnode);
        start_idx++;
    }

    for (start_idx = pr->start_index;
         listnode && (count < max_count);
         CTC_NEXTNODE(listnode), start_idx++)
    {
        p_l2_node = listnode->data;
        if (extra_port && (p_l2_node->adptr->gport != gport))
        {
            continue;
        }
        CONTINUE_IF_FLAG_NOT_MATCH(query_flag, p_l2_node->adptr->flag, p_l2_node->flag)
        _sys_greatbelt_l2_map_sw_to_entry(lchip, p_l2_node, pr, pi, &count);
    }

    pr->next_query_index = start_idx;

    if (listnode == NULL)
    {
        pr->is_end = 1;
    }

    return count;
}

STATIC int32
_sys_greatbelt_l2_get_entry_by_port(uint8 lchip, uint8 query_flag,
                                    uint16 gport,
                                    uint8 use_logic_port,
                                    ctc_l2_fdb_query_rst_t* pr,
                                    sys_l2_detail_t* pi)
{
    ctc_linklist_t    * fdb_list      = NULL;
    sys_l2_port_node_t* port_fdb_node = NULL;

    pr->is_end = 0;

    if (!use_logic_port)
    {
        port_fdb_node = ctc_vector_get(pl2_gb_master[lchip]->gport_vec, SYS_MAP_CTC_GPORT_TO_DRV_GPORT(gport));
        if (NULL == port_fdb_node || NULL == port_fdb_node->port_list)
        {
            pr->is_end = 1;
            return 0;
        }

        fdb_list = port_fdb_node->port_list;
    }
    else
    {
        port_fdb_node = ctc_vector_get(pl2_gb_master[lchip]->vport_vec, gport);
        if (NULL == port_fdb_node || NULL == port_fdb_node->port_list)
        {
            pr->is_end = 1;
            return 0;
        }

        fdb_list = port_fdb_node->port_list;
    }
    return _sys_greatbelt_l2_get_entry_by_list(lchip, fdb_list, query_flag, pr, pi, 0, 0, 0);
}

STATIC uint32
_sys_greatbelt_l2_get_entry_by_fid(uint8 lchip, uint8 query_flag,
                                   uint16 fid,
                                   ctc_l2_fdb_query_rst_t* query_rst,
                                   sys_l2_detail_t* fdb_detail_info)
{
    ctc_linklist_t* fdb_list = NULL;
 /*    uint32        max_count   = query_rst->buffer_len / sizeof(ctc_l2_addr_t);*/

    query_rst->is_end = FALSE;
    if (fid > pl2_gb_master[lchip]->cfg_max_fid)
    {
        query_rst->is_end = TRUE;
        return 0;
    }

    fdb_list = _sys_greatbelt_l2_fid_node_lkup(lchip, fid, GET_FID_LIST);

    if (NULL == fdb_list)
    {
        query_rst->is_end = TRUE;
        return 0;
    }

    return _sys_greatbelt_l2_get_entry_by_list(lchip, fdb_list, query_flag, query_rst, fdb_detail_info, 0, 0, 0);
}

STATIC int32
_sys_greatbelt_l2_get_entry_by_port_fid(uint8 lchip, uint8 query_flag,
                                        uint16 gport,
                                        uint16 fid,
                                        uint8 use_logic_port,
                                        ctc_l2_fdb_query_rst_t* pr,
                                        sys_l2_detail_t* pi)
{
    ctc_linklist_t* fdb_list = NULL;
 /*    uint32        max_count   = pr->buffer_len / sizeof(ctc_l2_addr_t);*/


    fdb_list = _sys_greatbelt_l2_fid_node_lkup(lchip, fid, GET_FID_LIST);

    if (NULL == fdb_list)
    {
        pr->is_end = TRUE;
        return 0;
    }

    return _sys_greatbelt_l2_get_entry_by_list(lchip, fdb_list, query_flag, pr, pi, 1, gport, 0);
}


STATIC int32
_sys_greatbelt_l2_get_entry_by_mac_cb(sys_l2_node_t* p_l2_node,
                                      sys_l2_mac_cb_t* mac_cb)
{
    uint8 lchip = 0;
    uint32                 max_index   = 0;
    uint32                 start_index = 0;
    ctc_l2_fdb_query_rst_t * pr        = mac_cb->query_rst;
    sys_l2_detail_t        * pi        = mac_cb->fdb_detail_rst;

    if (NULL == p_l2_node || NULL == mac_cb)
    {
        return 0;
    }
    lchip = mac_cb->lchip;

    start_index = pr->start_index;
    max_index   = pr->buffer_len / sizeof(ctc_l2_addr_t);

    if (mac_cb->count >= max_index)
    {
        return -1;
    }

    if (sal_memcmp(mac_cb->l2_node.key.mac, p_l2_node->key.mac, CTC_ETH_ADDR_LEN))
    {
        return 0;
    }

    if (pr->next_query_index++ < start_index)
    {
        return 0;
    }

    RETURN_IF_FLAG_NOT_MATCH(mac_cb->query_flag, p_l2_node->adptr->flag, p_l2_node->flag);
    _sys_greatbelt_l2_map_sw_to_entry(lchip, p_l2_node, pr, pi, &(mac_cb->count));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l2_get_entry_by_mac(uint8 lchip, uint8 query_flag,
                                   mac_addr_t mac,
                                   ctc_l2_fdb_query_rst_t* query_rst,
                                   sys_l2_detail_t* fdb_detail_info)
{
    sys_l2_mac_cb_t   mac_hash_info;
    int32             ret = 0;
    hash_traversal_fn fun = NULL;

    sal_memset(&mac_hash_info, 0, sizeof(sys_l2_mac_cb_t));
    FDB_SET_MAC(mac_hash_info.l2_node.key.mac, mac);
    mac_hash_info.query_flag     = query_flag;
    mac_hash_info.query_rst      = query_rst;
    mac_hash_info.fdb_detail_rst = fdb_detail_info;
    query_rst->next_query_index  = 0;
    mac_hash_info.lchip = lchip;

    fun = (hash_traversal_fn) _sys_greatbelt_l2_get_entry_by_mac_cb;

    ret = ctc_hash_traverse2(pl2_gb_master[lchip]->mac_hash, fun, &(mac_hash_info));
    if (ret > 0)
    {
        query_rst->is_end = 1;
    }

    return mac_hash_info.count;
}


STATIC int32
_sys_greatbelt_l2_get_entry_by_mac_fid(uint8 lchip, uint8 query_flag,
                                       mac_addr_t mac,
                                       uint16 fid,
                                       ctc_l2_fdb_query_rst_t* pr,
                                       sys_l2_detail_t* pi)
{
    sys_l2_node_t       * p_l2_node = NULL;
    uint32              count       = 0;
    ds_mac_t            ds_mac;
    mac_lookup_result_t rslt;

 /*    max_count = pr->buffer_len / sizeof(ctc_l2_addr_t);*/

    /* fid = 0xffff use for sys mac in tcam, so use fdb_hash to store */
    if (DONTCARE_FID == fid)
    {
        /*lookup black hole hash.*/
        p_l2_node = _sys_greatbelt_l2_fdb_hash_lkup(lchip, mac, fid);

        if (p_l2_node)
        {
            RETURN_IF_FLAG_NOT_MATCH(query_flag, p_l2_node->adptr->flag, p_l2_node->flag);
            _sys_greatbelt_l2_map_sw_to_entry(lchip, p_l2_node, pr, pi, &count);
        }

        return count;
    }

/* l2uc, l2mc. */
    CTC_ERROR_RETURN(_sys_greatbelt_l2_acc_lookup_mac_fid(lchip, mac, fid, &rslt));

    if (rslt.hit)
    {
        sys_l2_info_t sys_info;
        sal_memset(&sys_info, 0, sizeof(sys_l2_info_t));
        _sys_get_dsmac_by_index(lchip, rslt.ad_index, &ds_mac);
        _sys_greatbelt_l2_decode_dsmac(lchip, &sys_info, &ds_mac, rslt.ad_index);

        if (pl2_gb_master[lchip]->cfg_has_sw)
        {
            p_l2_node = _sys_greatbelt_l2_fdb_hash_lkup(lchip, mac, fid);
            sys_info.flag.flag_node = p_l2_node->flag;
        }

        RETURN_IF_FLAG_NOT_MATCH(query_flag, sys_info.flag.flag_ad, sys_info.flag.flag_node);

        (pr->buffer[0]).is_logic_port = sys_info.flag.flag_ad.logic_port;
        (pr->buffer[0]).fid   = fid;
        (pr->buffer[0]).gport = sys_info.gport;

        if (sys_info.flag.flag_ad.is_static)
        {
            pr->buffer[0].flag = CTC_L2_FLAG_IS_STATIC;
        }

        FDB_SET_MAC((pr->buffer[0]).mac, mac);

        if (pi)
        {
            pi[0].key_index = rslt.key_index;
            pi[0].ad_index  = rslt.ad_index;
            sal_memcpy(&pi[0].flag, &sys_info.flag, sizeof(sys_l2_flag_t));
        }

        count++;
    }

    pr->is_end = 1;

    return count;
}


typedef struct
{
    uint32                 query_flag;
    uint32                 count;
    uint32                 fid;
    ctc_l2_fdb_query_rst_t * pr;
    sys_l2_detail_t        * pi;
    uint8                  lchip; /**< for tranvase speific lchip*/
    uint8                  rev[3];

}sys_l2_all_cb_t;

STATIC int32
_sys_greatbelt_l2_get_entry_by_all_cb(sys_l2_fid_node_t* pf, uint32 fid, sys_l2_all_cb_t* all_cb)
{
    ctc_l2_fdb_query_rst_t * query_rst       = all_cb->pr;
    sys_l2_detail_t        * fdb_detail_info = all_cb->pi;
    ctc_linklist_t         * fdb_list        = NULL;
    uint32                  max_count        = 0;
    uint8                   lchip            = 0;

    if (NULL == pf || NULL == all_cb)
    {
        return 0;
    }

    max_count = query_rst->buffer_len / sizeof(ctc_l2_addr_t);
    lchip = all_cb->lchip;
    fdb_list = pf->fid_list;
    all_cb->fid   = fid;
    all_cb->count = _sys_greatbelt_l2_get_entry_by_list(lchip, fdb_list, all_cb->query_flag, query_rst, fdb_detail_info, 0, 0, &all_cb->count);

    /*If traverse to next fid, should clear start index*/
    query_rst->start_index = 0;


    if(max_count == all_cb->count)
    {
        return -1;
    }

    return 0;
}

STATIC int32
_sys_greatbelt_l2_get_entry_by_all(uint8 lchip, uint8 query_flag,
                                   ctc_l2_fdb_query_rst_t* pr,
                                   sys_l2_detail_t* pi)
{
 /*    uint32          max_count      = pr->buffer_len / sizeof(ctc_l2_addr_t);*/
    sys_l2_all_cb_t all_cb;
    int32 ret  = 0;

    uint32 fid = 0;
    pr->is_end = 0;

    sal_memset(&all_cb, 0, sizeof(all_cb));
    all_cb.pr         = pr;
    all_cb.pi         = pi;
    all_cb.query_flag = query_flag;
    all_cb.lchip      = lchip;

    fid             =   SYS_L2_DECODE_QUERY_FID(pr->start_index);
    pr->start_index =   SYS_L2_DECODE_QUERY_IDX(pr->start_index);

    ret = ctc_vector_traverse2(pl2_gb_master[lchip]->fid_vec,
                                    fid,
                                    (vector_traversal_fn2) _sys_greatbelt_l2_get_entry_by_all_cb,
                                    &all_cb);

    if(ret < 0)
    {
        pr->next_query_index = SYS_L2_ENCODE_QUERY_IDX(all_cb.fid, pr->next_query_index);
        pr->is_end = 0;
    }
    else
    {
        pr->is_end = 1;
    }



    /*ctc_vector_traverse(pl2_gb_master[lchip]->fid_vec,
                        (vector_traversal_fn) _sys_greatbelt_l2_get_entry_by_all_cb,
                        &all_cb);
    */
    return all_cb.count;
}


/**
   @brief Query fdb enery according to specified query condition

 */
STATIC int32
_sys_greatbelt_l2_query_flush_check(uint8 lchip, void* p, uint8 is_flush)
{
    uint8  type           = 0;
    uint16 fid            = 0;
    uint16 gport          = 0;
    uint8  use_logic_port = 0;
    if (is_flush)
    {
        ctc_l2_flush_fdb_t* pf = (ctc_l2_flush_fdb_t *) p;
        type           = pf->flush_type;
        fid            = pf->fid;
        use_logic_port = pf->use_logic_port;
        gport          = pf->gport;
    }
    else
    {
        ctc_l2_fdb_query_t* pq = (ctc_l2_fdb_query_t *) p;
        type           = pq->query_type;
        fid            = pq->fid;
        use_logic_port = pq->use_logic_port;
        gport          = pq->gport;
    }

    if ((type == CTC_L2_FDB_ENTRY_OP_BY_VID) ||
        (type == CTC_L2_FDB_ENTRY_OP_BY_PORT_VLAN) ||
        (type == CTC_L2_FDB_ENTRY_OP_BY_MAC_VLAN))
    {
        SYS_L2UC_FID_CHECK(fid); /* allow 0xFFFF */
    }

    if ((type == CTC_L2_FDB_ENTRY_OP_BY_PORT) ||
        (type == CTC_L2_FDB_ENTRY_OP_BY_PORT_VLAN))
    {
        if (!use_logic_port)
        {
            CTC_ERROR_RETURN(sys_greatbelt_port_dest_gport_check(lchip, gport));
        }
        else
        {
            SYS_LOGIC_PORT_CHECK(gport);
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l2_get_hash_blk_left_count(uint8 lchip, uint32 start_index, int32 *left)
{
    uint32 i;
    uint32 in_range = 0;

    for (i = 0; i < pl2_gb_master[lchip]->hash_block_count; i++)
    {
        if (start_index < pl2_gb_master[lchip]->hash_index_array[i])
        {
             *left = pl2_gb_master[lchip]->hash_index_array[i] - start_index;
             in_range = 1;
             break;
        }
    }

    if (!in_range)
    {
        return CTC_E_EXCEED_MAX_SIZE;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l2_hw_entry_filter(uint8 lchip, ctc_l2_fdb_query_t* pq, ctc_l2_addr_t* l2_addr, sys_l2_flag_t* sys_flag)
{
    uint8 gchip;
    uint8 fdb_gchip;

    switch (pq->query_type)
    {
    case CTC_L2_FDB_ENTRY_OP_BY_VID:
        if (pq->fid != l2_addr->fid)
        {
            return CTC_E_ENTRY_NOT_EXIST;
        }
        break;

    case CTC_L2_FDB_ENTRY_OP_BY_PORT:
        if (pq->gport != l2_addr->gport)
        {
            return CTC_E_ENTRY_NOT_EXIST;
        }
        break;

    case CTC_L2_FDB_ENTRY_OP_BY_PORT_VLAN:
        if ((pq->gport != l2_addr->gport) || (pq->fid != l2_addr->fid))
        {
            return CTC_E_ENTRY_NOT_EXIST;
        }
        break;

    case CTC_L2_FDB_ENTRY_OP_BY_MAC_VLAN:
        if ((sal_memcmp(pq->mac, l2_addr->mac, 6)) || (pq->fid != l2_addr->fid))
        {
            return CTC_E_ENTRY_NOT_EXIST;
        }
        break;

    case CTC_L2_FDB_ENTRY_OP_BY_MAC:
        if (sal_memcmp(pq->mac, l2_addr->mac, 6))
        {
            return CTC_E_ENTRY_NOT_EXIST;
        }
        break;

    case CTC_L2_FDB_ENTRY_OP_ALL:
        break;

    default:
        return CTC_E_ENTRY_NOT_EXIST;
    }

    switch (pq->query_flag)
    {
    case CTC_L2_FDB_ENTRY_STATIC:
        if (!CTC_FLAG_ISSET(l2_addr->flag, CTC_L2_FLAG_IS_STATIC))
        {
            return CTC_E_ENTRY_NOT_EXIST;
        }
        break;

    case  CTC_L2_FDB_ENTRY_DYNAMIC:
        if (CTC_FLAG_ISSET(l2_addr->flag, CTC_L2_FLAG_IS_STATIC))
        {
            return CTC_E_ENTRY_NOT_EXIST;
        }
        break;

    case CTC_L2_FDB_ENTRY_LOCAL_DYNAMIC:
        /* local FDB includes: phy_port with local gchip_id / linkagg / logic_port */
        if (CTC_FLAG_ISSET(l2_addr->flag, CTC_L2_FLAG_IS_STATIC))
        {
            return CTC_E_ENTRY_NOT_EXIST;
        }
        if (CTC_IS_LINKAGG_PORT(l2_addr->gport))
        {
            break;          /* local FDB */
        }
        if (sys_flag->flag_ad.logic_port)
        {
            break;          /* local FDB */
        }
        fdb_gchip = CTC_MAP_GPORT_TO_GCHIP(l2_addr->gport);

            sys_greatbelt_get_gchip_id(lchip, &gchip);
            if (fdb_gchip == gchip)
            {
                return CTC_E_NONE;  /* local FDB */
            }

        return CTC_E_ENTRY_NOT_EXIST;

    case CTC_L2_FDB_ENTRY_ALL:
    case CTC_L2_FDB_ENTRY_MCAST:
    default:
        break;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l2_get_hw_entry_by_cam(uint8 lchip, ctc_l2_fdb_query_t* pq,
                            ctc_l2_fdb_query_rst_t* pr,
                            sys_l2_detail_t* pi)
{
    uint32 max_count = pr->buffer_len / sizeof(ctc_l2_addr_t);
    uint32 count = 0;
    int32 ret = 0;
    ctc_l2_addr_t l2_addr;
    sys_l2_detail_t l2_detail;
    uint32 key_index = 0;

    key_index = pr->start_index;
    SYS_FDB_DBG_INFO("enter HW FDB CAM get, start key_index: %d, max_count: %d\n", key_index, max_count);

    for ( key_index = pr->start_index; key_index < pl2_gb_master[lchip]->hash_base; key_index++)
    {
        sal_memset(&l2_addr, 0, sizeof(ctc_l2_addr_t));
        ret = sys_greatbelt_l2_get_fdb_by_index(lchip, key_index, pq->use_logic_port, &l2_addr, &l2_detail);
        if (ret == 0)
        {
            ret = _sys_greatbelt_l2_hw_entry_filter(lchip, pq, &l2_addr, &l2_detail.flag);
            if (ret < 0)
            {
                continue;
            }
            sal_memcpy(&pr->buffer[count], &l2_addr, sizeof(l2_addr));
            if (pi)
            {
                sal_memcpy(&pi[count], &l2_detail, sizeof(sys_l2_detail_t));
            }
            count++;
            if (count >= max_count)
            {
                key_index++;
                break;
            }
        }
    }

    pq->count = count;
    pr->next_query_index = key_index;

    SYS_FDB_DBG_INFO("exit HW FDB CAM get, next_query_index: %d, count: %d\n", pr->next_query_index, count);
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l2_get_hw_entry_by_dma_hash(uint8 lchip, ctc_l2_fdb_query_t* pq,
                            ctc_l2_fdb_query_rst_t* pr,
                            sys_l2_detail_t* pi)
{
    uint32 max_count = pr->buffer_len / sizeof(ctc_l2_addr_t);
    ctc_l2_addr_t* l2_addr_base = (ctc_l2_addr_t*)pr->buffer;
    sys_l2_detail_t* l2_detail_base = (sys_l2_detail_t*)pi;
    int32 ret = 0;
    uint32 i = 0;
    uint32 count = 0;
    uint32 key_index = 0;
    uint32 hash_index = 0;
    uint32 loop = 0;
    int32 left = 0;
    uint32 cmd = 0;
    ds_mac_t dsmac;
    ds_key_t *p_mac_key = NULL;
    uint32* p_read_buf = NULL;
    ctc_dma_tbl_rw_t tbl_cfg;
    sys_l2_info_t sys;
    ctc_l2_addr_t* p_l2_addr = NULL;
    sys_l2_detail_t* p_l2_detail = NULL;
    ds_fwd_t       ds_fwd;

    sal_memset(&tbl_cfg, 0, sizeof(ctc_dma_tbl_rw_t));
    sal_memset(&sys, 0, sizeof(sys_l2_info_t));
    p_read_buf = mem_malloc(MEM_FDB_MODULE, 1000 * SYS_L2_MAC_KEY_DWORD_SIZE *sizeof(uint32));
    if (NULL == p_read_buf)
    {
        return CTC_E_NO_MEMORY;
    }

    tbl_cfg.buffer = p_read_buf;
    tbl_cfg.entry_len = SYS_L2_MAC_KEY_DWORD_SIZE * DRV_BYTES_PER_WORD;
    tbl_cfg.rflag = 1;


    key_index = pr->start_index;
    hash_index = key_index - pl2_gb_master[lchip]->hash_base;
    SYS_FDB_DBG_INFO("enter HW FDB HASH get, start key_index: %d, hash_index: %d, max_count: %d\n", key_index, hash_index, max_count);

    for (loop = 0; loop < SYS_L2_DMA_READ_LOOP_COUNT; loop++)
    {
        ret = _sys_greatbelt_l2_get_hash_blk_left_count(lchip, hash_index, &left);
        if (ret < 0)
        {
            pr->is_end = 1;
            break;
        }

        tbl_cfg.entry_num = (left > SYS_L2_MAC_KEY_DMA_READ_COUNT) ? SYS_L2_MAC_KEY_DMA_READ_COUNT : left;
        ret = drv_greatbelt_table_get_hw_addr(DsMacHashKey_t, hash_index, &tbl_cfg.tbl_addr);
        CTC_ERROR_GOTO(sys_greatbelt_dma_rw_table(lchip, &tbl_cfg),ret,mem_err);
        SYS_FDB_DBG_INFO("DMA read hash_index range: [%d, %d], count: %d\n", hash_index, hash_index+tbl_cfg.entry_num-1, tbl_cfg.entry_num);
        for (i = 0; i < tbl_cfg.entry_num; i++)
        {
            p_mac_key = (ds_key_t*)(p_read_buf + (tbl_cfg.entry_len/DRV_BYTES_PER_WORD) * i);
            hash_index++;
            if (p_mac_key->valid)
            {
                p_l2_addr = &l2_addr_base[count];
                if (l2_detail_base)
                {
                    p_l2_detail = &l2_detail_base[count];
                }
                _sys_greatbelt_l2_decode_dskey(lchip, &sys, p_mac_key);

                /* get mac, fid */
                p_l2_addr->fid = sys.fid;
                FDB_SET_MAC(p_l2_addr->mac, sys.mac);

                /* decode and get gport, flag */
                _sys_get_dsmac_by_index(lchip, sys.ad_index, &dsmac);
                _sys_greatbelt_l2_decode_dsmac(lchip, &sys, &dsmac, sys.ad_index);

                p_l2_addr->gport = sys.gport;
                if (sys.flag.flag_ad.logic_port)
                {
                    p_l2_addr->is_logic_port = 1;
                    if (!pq->use_logic_port)
                    {
                        sal_memset(&ds_fwd, 0, sizeof(ds_fwd_t));
                        cmd = DRV_IOR(DsFwd_t, DRV_ENTRY_FLAG);
                        CTC_ERROR_RETURN(DRV_IOCTL(lchip, sys.fwd_ptr, cmd, &ds_fwd));
                        if ((0 != ds_fwd.aps_type) || (CTC_IS_BIT_SET(ds_fwd.dest_map, 21)))
                        {
                            p_l2_addr->gport = 0xFFFF;
                        }
                        else
                        {
                            p_l2_addr->gport = SYS_GREATBELT_DESTMAP_TO_GPORT(ds_fwd.dest_map);
                        }
                    }
                }
                _sys_greatbelt_l2_unmap_flag(lchip, &p_l2_addr->flag, &sys.flag);

                ret = _sys_greatbelt_l2_hw_entry_filter(lchip, pq, p_l2_addr, &sys.flag);
                if (ret < 0)
                {
                    continue;
                }
                p_l2_addr->gport = sys.gport;
                if (p_l2_detail)
                {
                    /* hash_index has been ++ */
                    p_l2_detail->key_index = (hash_index - 1) + pl2_gb_master[lchip]->hash_base;
                    p_l2_detail->ad_index = sys.ad_index;
                    p_l2_detail->flag = sys.flag;
                }

                count++;
                if (count >= max_count)
                {
                    break;
                }
            }
        }

        if (count >= max_count)
        {
            break;
        }
    }

    pr->next_query_index = hash_index + pl2_gb_master[lchip]->hash_base;
    pq->count = count;

    mem_free(p_read_buf);

    SYS_FDB_DBG_INFO("exit HW FDB HASH get, next_query_index: %d, is_end: %d, count: %d\n", pr->next_query_index, pr->is_end, count);
    return CTC_E_NONE;

mem_err:

    mem_free(p_read_buf);
    return ret;
}

STATIC int32
_sys_greatbelt_l2_get_hw_entry_by_dma(uint8 lchip, ctc_l2_fdb_query_t* pq,
                            ctc_l2_fdb_query_rst_t* pr,
                            sys_l2_detail_t* pi)
{
    if (pr->start_index < pl2_gb_master[lchip]->hash_base)
    {
        return _sys_greatbelt_l2_get_hw_entry_by_cam(lchip, pq, pr, pi);
    }
    else
    {
        return _sys_greatbelt_l2_get_hw_entry_by_dma_hash(lchip, pq, pr, pi);
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l2_get_hw_entry(uint8 lchip, ctc_l2_fdb_query_t* pq,
                            ctc_l2_fdb_query_rst_t* pr,
                            sys_l2_detail_t* pi)
{
    uint32 max_count = pr->buffer_len / sizeof(ctc_l2_addr_t);
    uint32 index = 0;
    uint32 count = 0;
    uint32 loop = 0;
    int32 ret = 0;
    ctc_l2_addr_t l2_addr;
    sys_l2_detail_t l2_detail;

    index = pr->start_index;
    SYS_FDB_DBG_INFO("enter HW FDB HASH get, start key_index: %d, max_count: %d\n", index, max_count);

    for (loop = 0; loop < SYS_L2_DMA_READ_LOOP_COUNT * SYS_L2_MAC_KEY_DMA_READ_COUNT; loop++)
    {
        if (index >= (pl2_gb_master[lchip]->hash_base + pl2_gb_master[lchip]->pure_hash_num))
        {
            pr->is_end = 1;
            break;
        }
        sal_memset(&l2_addr, 0, sizeof(ctc_l2_addr_t));
        ret = sys_greatbelt_l2_get_fdb_by_index(lchip, index, pq->use_logic_port, &l2_addr, &l2_detail);

        index++;
        if (ret == 0)
        {
            ret = _sys_greatbelt_l2_hw_entry_filter(lchip, pq, &l2_addr, &l2_detail.flag);
            if (ret < 0)
            {
                continue;
            }

            sal_memcpy(&pr->buffer[count], &l2_addr, sizeof(l2_addr));
            if (pi)
            {
                sal_memcpy(&pi[count], &l2_detail, sizeof(sys_l2_detail_t));
            }
            count++;
            if (count >= max_count)
            {
                break;
            }
        }
    }

    pq->count = count;
    pr->next_query_index = index;

    SYS_FDB_DBG_INFO("exit HW FDB HASH get, next_query_index: %d, is_end: %d, count: %d\n", pr->next_query_index, pr->is_end, count);
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l2_get_entry(uint8 lchip, ctc_l2_fdb_query_t* pq,
                            ctc_l2_fdb_query_rst_t* pr,
                            sys_l2_detail_t* pi)
{
    uint8 flag;
    uint8 is_vport;

    if (!pl2_gb_master[lchip]->cfg_has_sw && (CTC_L2_FDB_ENTRY_OP_BY_MAC_VLAN != pq->query_type))
    {
        return CTC_E_FDB_NO_SW_TABLE_NO_QUARY;
    }

    flag     = pq->query_flag;
    is_vport = pq->use_logic_port;

    CTC_ERROR_RETURN(_sys_greatbelt_l2_query_flush_check(lchip, pq, 0));


    pq->count = 0;

    L2_LOCK;

    switch (pq->query_type)
    {
    case CTC_L2_FDB_ENTRY_OP_BY_VID: /* sw only*/
        pq->count = _sys_greatbelt_l2_get_entry_by_fid(lchip, flag, pq->fid, pr, pi);
        break;

    case CTC_L2_FDB_ENTRY_OP_BY_PORT: /* sw only*/
        pq->count = _sys_greatbelt_l2_get_entry_by_port(lchip, flag, pq->gport, is_vport, pr, pi);
        break;

    case CTC_L2_FDB_ENTRY_OP_BY_PORT_VLAN:/* sw only*/
        pq->count = _sys_greatbelt_l2_get_entry_by_port_fid(lchip, flag, pq->gport, pq->fid, is_vport, pr, pi);
        break;

    case CTC_L2_FDB_ENTRY_OP_BY_MAC_VLAN:/* sw hw */
        pq->count = _sys_greatbelt_l2_get_entry_by_mac_fid(lchip, flag, pq->mac, pq->fid, pr, pi);
        break;

    case CTC_L2_FDB_ENTRY_OP_BY_MAC:/* sw only*/
        pq->count = _sys_greatbelt_l2_get_entry_by_mac(lchip, flag, pq->mac, pr, pi);
        break;

    case CTC_L2_FDB_ENTRY_OP_ALL:/* sw only*/
        pq->count = _sys_greatbelt_l2_get_entry_by_all(lchip, flag, pr, pi);
        break;

    default:
        pq->count = 0;
        break;
    }
    if (0 == pq->count)
    {
        pr->is_end = TRUE;
    }

    L2_UNLOCK;

    return CTC_E_NONE;
}

typedef struct
{
    uint32                 count;
    ctc_l2_fdb_query_rst_t * pr;
    ctc_l2_fdb_query_t     * pq;
    sys_l2_detail_t        * pi;
    uint8                  lchip; /**< for tranvase speific lchip*/
    uint8                  rev[3];

}sys_l2_dump_mcast_cb_t;

STATIC int32
_sys_greatbelt_l2_get_mcast_entry_cb(sys_l2_mcast_node_t* p_node, sys_l2_dump_mcast_cb_t* all_cb)
{
    ctc_l2_fdb_query_rst_t * pr              = NULL;
    ctc_l2_fdb_query_t     * pq              = NULL;
    sys_l2_detail_t        * detail_info     = NULL;
    uint32                  count            = 0;
    uint8                   lchip            = 0;
    uint32                  key_index        = 0;
    uint32                  max_count        = 0;
    ctc_l2_addr_t           l2_addr;
    int32                   ret = 0;
    sys_l2_detail_t         l2_detail;

    if (NULL == p_node || NULL == all_cb)
    {
        return 0;
    }
    pr              = all_cb->pr;
    pq              = all_cb->pq;
    detail_info     = all_cb->pi;
    sal_memset(&l2_addr, 0, sizeof(ctc_l2_addr_t));
    sal_memset(&l2_detail, 0, sizeof(sys_l2_detail_t));
    max_count = pr->buffer_len / sizeof(ctc_l2_addr_t);
    lchip = all_cb->lchip;
    key_index = p_node->key_index;
    all_cb->count++;
    if (all_cb->count <= pr->start_index)
    {
        return 0;
    }

    ret = sys_greatbelt_l2_get_fdb_by_index(lchip, key_index, 1, &l2_addr, &l2_detail);
    if (ret == 0)
    {
        ret = _sys_greatbelt_l2_hw_entry_filter(lchip, pq, &l2_addr, &l2_detail.flag);
        if (ret < 0)
        {
            return 0;
        }

        count = pq->count;
        sal_memcpy(&pr->buffer[count], &l2_addr, sizeof(l2_addr));
        if (detail_info)
        {
            sal_memcpy(&detail_info[count], &l2_detail, sizeof(sys_l2_detail_t));
        }
        pq->count++;
    }
    if (pq->count >= max_count)
    {
        return -1;
    }

    return 0;
}

STATIC int32
_sys_greatbelt_l2_get_mcast_entry(uint8 lchip, ctc_l2_fdb_query_t* pq,
                               ctc_l2_fdb_query_rst_t* pr, sys_l2_detail_t* pi)
{
    sys_l2_dump_mcast_cb_t all_cb;
    int32 ret  = 0;
    pr->is_end = 0;
    pq->count  = 0;

    L2_LOCK;
    sal_memset(&all_cb, 0, sizeof(all_cb));
    all_cb.pr         = pr;
    all_cb.pq         = pq;
    all_cb.pi         = pi;
    all_cb.lchip      = lchip;

    pr->start_index =   SYS_L2_DECODE_QUERY_IDX(pr->start_index);
    ret = ctc_vector_traverse(pl2_gb_master[lchip]->mcast2nh_vec,
                                    (vector_traversal_fn) _sys_greatbelt_l2_get_mcast_entry_cb,
                                    &all_cb);


    if(ret < 0)
    {
        pr->next_query_index = all_cb.count;
        pr->is_end = 0;
    }
    else
    {
        pr->is_end = 1;
    }

    if (pq->count == 0)
    {
        pr->is_end = 1;
    }
    L2_UNLOCK;
    return CTC_E_NONE;

}

STATIC int32
_sys_greatbelt_l2_get_fdb_conflict(uint8 lchip, ctc_l2_fdb_query_t* pq,
                                ctc_l2_fdb_query_rst_t* pr, sys_l2_detail_t* pi)
{
    int32 ret = 0;
    uint8 index = 0;
    uint16 buf_cnt = pr->buffer_len/sizeof(ctc_l2_addr_t);
    uint16 cf_max = 0;
    uint16 cf_count = 0;
    uint32 key_index = 0;
    sys_l2_calc_index_t calc_index;
    sys_l2_detail_t* l2_detail = NULL;
    ctc_l2_addr_t* cf_addr = NULL;
    sys_l2_detail_t* l2_detail_temp = NULL;
    ctc_l2_addr_t* cf_addr_temp = NULL;
    SYS_L2_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(pq);
    CTC_PTR_VALID_CHECK(pr);

    SYS_L2UC_FID_CHECK(pq->fid);

    if (pq->fid == DONTCARE_FID)
    {
        return CTC_E_NOT_SUPPORT;
    }
    pq->count = 0;

    sal_memset(&calc_index, 0, sizeof(sys_l2_calc_index_t));
    CTC_ERROR_RETURN(_sys_greatbelt_l2_fdb_calc_index(lchip, pq->mac, pq->fid, &calc_index));
    cf_max = calc_index.index_cnt + pl2_gb_master[lchip]->hash_base;
    cf_addr = (ctc_l2_addr_t*)mem_malloc(MEM_FDB_MODULE, cf_max * sizeof(ctc_l2_addr_t));
    if (cf_addr == NULL)
    {
        return CTC_E_NO_MEMORY;
    }
    sal_memset(cf_addr, 0, cf_max * sizeof(ctc_l2_addr_t));

    l2_detail = (sys_l2_detail_t*)mem_malloc(MEM_FDB_MODULE, cf_max * sizeof(sys_l2_detail_t));
    if (l2_detail == NULL)
    {
        mem_free(cf_addr);
        return CTC_E_NO_MEMORY;
    }
    sal_memset(l2_detail, 0, cf_max * sizeof(sys_l2_detail_t));

    l2_detail_temp = l2_detail;
    cf_addr_temp = cf_addr;

    for (index = 0; index < cf_max; index++)
    {
        key_index = (index >= calc_index.index_cnt) ? (index - calc_index.index_cnt) : calc_index.key_index[index];
        ret = sys_greatbelt_l2_get_fdb_by_index(lchip, key_index, 0, cf_addr_temp, l2_detail_temp);
        if (ret < 0)
        {
            if (ret == CTC_E_ENTRY_NOT_EXIST)
            {
                ret = CTC_E_NONE;
                 /*goto error_return;*/
                continue;
            }
            else
            {
                goto error_return;
            }
        }
        cf_addr_temp++;
        l2_detail_temp++;
        cf_count++;
    }

    pr->next_query_index = pr->start_index + buf_cnt;
    if (pr->next_query_index >= cf_count)
    {
        pr->next_query_index = cf_count;
        pr->is_end = 1;
    }

    if (pr->start_index >= cf_count)
    {
        goto error_return;
    }
    pq->count = pr->next_query_index - pr->start_index;
    sal_memcpy(pr->buffer, cf_addr + pr->start_index, pq->count * sizeof(ctc_l2_addr_t));

    if (pi)
    {
        sal_memcpy(pi, l2_detail + pr->start_index, pq->count * sizeof(sys_l2_detail_t));
    }

    mem_free(cf_addr);
    mem_free(l2_detail);
    return CTC_E_NONE;

error_return:
    pr->is_end = 1;
    mem_free(cf_addr);
    mem_free(l2_detail);
    return ret;
}

int32
sys_greatbelt_l2_get_fdb_entry(uint8 lchip, ctc_l2_fdb_query_t* pq,
                               ctc_l2_fdb_query_rst_t* pr)
{
    SYS_L2_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(pq);

    if ((pq->query_flag == CTC_L2_FDB_ENTRY_MCAST) && !pl2_gb_master[lchip]->trie_sort_en)
    {
        if ((pq->query_type == CTC_L2_FDB_ENTRY_OP_BY_PORT)
            || (pq->query_type == CTC_L2_FDB_ENTRY_OP_BY_PORT_VLAN))
        {
            return CTC_E_NOT_SUPPORT;
        }
        return _sys_greatbelt_l2_get_mcast_entry(lchip, pq, pr, NULL);
    }
    if (pq->query_flag == CTC_L2_FDB_ENTRY_CONFLICT)
    {
        if (pq->query_type != CTC_L2_FDB_ENTRY_OP_BY_MAC_VLAN)
        {
            return CTC_E_NOT_SUPPORT;
        }
        return _sys_greatbelt_l2_get_fdb_conflict(lchip, pq, pr, NULL);
    }

    if (pq->query_hw)
    {
        if (pl2_gb_master[lchip]->get_by_dma)
        {
            return _sys_greatbelt_l2_get_hw_entry_by_dma(lchip, pq, pr, NULL);
        }
        else
        {
            return _sys_greatbelt_l2_get_hw_entry(lchip, pq, pr, NULL);
        }
    }
    else
    {
        return _sys_greatbelt_l2_get_entry(lchip, pq, pr, NULL);
    }

}

int32
sys_greatbelt_l2_fdb_dump_all(uint8 lchip, ctc_l2_fdb_query_t* pq,
                               ctc_l2_fdb_query_rst_t* pr)
{
    SYS_FDB_DBG_FUNC();
    CTC_PTR_VALID_CHECK(pq);
    CTC_PTR_VALID_CHECK(pr);
    pq->query_flag = CTC_L2_FDB_ENTRY_MCAST;  /** means include ucast and mcast*/
    pq->query_type = CTC_L2_FDB_ENTRY_OP_ALL;
    pq->query_hw = 1;

    return sys_greatbelt_l2_get_fdb_entry(lchip, pq, pr);
}


int32
sys_greatbelt_l2_get_fdb_entry_detail(uint8 lchip, ctc_l2_fdb_query_t* pq,
                                      ctc_l2_fdb_query_rst_t* pr,
                                      sys_l2_detail_t* pi)
{
    SYS_L2_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(pq);

    if (pq->query_flag == CTC_L2_FDB_ENTRY_MCAST)
    {
        if ((pq->query_type == CTC_L2_FDB_ENTRY_OP_BY_PORT)
            || (pq->query_type == CTC_L2_FDB_ENTRY_OP_BY_PORT_VLAN))
        {
            return CTC_E_NOT_SUPPORT;
        }
        return _sys_greatbelt_l2_get_mcast_entry(lchip, pq, pr, pi);
    }
    if (pq->query_flag == CTC_L2_FDB_ENTRY_CONFLICT)
    {
        if (pq->query_type != CTC_L2_FDB_ENTRY_OP_BY_MAC_VLAN)
        {
            return CTC_E_NOT_SUPPORT;
        }
        return _sys_greatbelt_l2_get_fdb_conflict(lchip, pq, pr, pi);
    }

    if (pq->query_hw)
    {
        if (pl2_gb_master[lchip]->get_by_dma)
        {
            return _sys_greatbelt_l2_get_hw_entry_by_dma(lchip, pq, pr, pi);
        }
        else
        {
            return _sys_greatbelt_l2_get_hw_entry(lchip, pq, pr, pi);
        }
    }
    else
    {
        return _sys_greatbelt_l2_get_entry(lchip, pq, pr, pi);
    }
}


#define _FDB_COUNT_
/**
 * get some type fdb count by specified port,not include the default entry num
 */
uint32
_sys_greatbelt_l2_get_count_by_port(uint8 lchip, uint8 query_flag, uint16 gport, uint8 use_logic_port)
{
    uint32            count       = 0;
    sys_l2_port_node_t* port_node = NULL;

    if (!use_logic_port)
    {
        port_node = ctc_vector_get(pl2_gb_master[lchip]->gport_vec, SYS_MAP_CTC_GPORT_TO_DRV_GPORT(gport));
        if (NULL == port_node || NULL == port_node->port_list)
        {
            return 0;
        }
    }
    else
    {
        port_node = ctc_vector_get(pl2_gb_master[lchip]->vport_vec, gport);
        if (NULL == port_node || NULL == port_node->port_list)
        {
            return 0;
        }
    }

    switch (query_flag)
    {
    case CTC_L2_FDB_ENTRY_STATIC:
        count = CTC_LISTCOUNT(port_node->port_list) - port_node->dynamic_count;
        break;

    case  CTC_L2_FDB_ENTRY_DYNAMIC:
        count = port_node->dynamic_count;
        break;

    case CTC_L2_FDB_ENTRY_LOCAL_DYNAMIC:
        count = port_node->local_dynamic_count;
        break;

    case CTC_L2_FDB_ENTRY_ALL:
        count = CTC_LISTCOUNT(port_node->port_list);
        break;

    default:
        break;
    }

    return count;
}

/**
 * get some type fdb count by specified vid,not include the default entry num
 */
uint32
_sys_greatbelt_l2_get_count_by_fid(uint8 lchip, uint8 query_flag, uint16 fid)
{
    uint32           count      = 0;
    sys_l2_fid_node_t* fid_node = NULL;

    fid_node = _sys_greatbelt_l2_fid_node_lkup(lchip, fid, GET_FID_NODE);
    if (NULL == fid_node || NULL == fid_node->fid_list)
    {
        return 0;
    }

    switch (query_flag)
    {
    case CTC_L2_FDB_ENTRY_STATIC:
        count = CTC_LISTCOUNT(fid_node->fid_list) - fid_node->dynamic_count;
        break;

    case  CTC_L2_FDB_ENTRY_DYNAMIC:
        count = fid_node->dynamic_count;
        break;

    case CTC_L2_FDB_ENTRY_LOCAL_DYNAMIC:
        count = fid_node->local_dynamic_count;
        break;
    case CTC_L2_FDB_ENTRY_ALL:
        count = CTC_LISTCOUNT(fid_node->fid_list);

    default:
        break;
    }

    return count;
}

STATIC int32
_sys_greatbelt_l2_get_count_by_port_fid(uint8 lchip, uint8 query_flag, uint16 gport, uint16 fid, uint8 use_logic_port)
{
    uint32            count       = 0;
    ctc_listnode_t    * listnode  = NULL;
    ctc_linklist_t    * fdb_list  = NULL;
    sys_l2_node_t     * p_l2_node = NULL;
    sys_l2_port_node_t* port_node = NULL;

    if (!use_logic_port)
    {
        port_node = ctc_vector_get(pl2_gb_master[lchip]->gport_vec, SYS_MAP_CTC_GPORT_TO_DRV_GPORT(gport));
        if (NULL == port_node || NULL == port_node->port_list)
        {
            return 0;
        }
    }
    else
    {
        port_node = ctc_vector_get(pl2_gb_master[lchip]->vport_vec, gport);
        if (NULL == port_node || NULL == port_node->port_list)
        {
            return 0;
        }
    }

    fdb_list = port_node->port_list;

    switch (query_flag)
    {
    case CTC_L2_FDB_ENTRY_STATIC:
        CTC_LIST_LOOP(fdb_list, p_l2_node, listnode)
        {
            if (p_l2_node->adptr->flag.is_static && (p_l2_node->key.fid == fid))
            {
                count++;
            }
        }

        break;

    case  CTC_L2_FDB_ENTRY_DYNAMIC:
        CTC_LIST_LOOP(fdb_list, p_l2_node, listnode)
        {
            if (!p_l2_node->adptr->flag.is_static && (p_l2_node->key.fid == fid))
            {
                count++;
            }
        }
        break;

    case CTC_L2_FDB_ENTRY_LOCAL_DYNAMIC:
        CTC_LIST_LOOP(fdb_list, p_l2_node, listnode)
        {
            if (!p_l2_node->flag.remote_dynamic &&
                !p_l2_node->adptr->flag.is_static
                && (p_l2_node->key.fid == fid))
            {
                count++;
            }
        }
        break;

    case CTC_L2_FDB_ENTRY_ALL:
        CTC_LIST_LOOP(fdb_list, p_l2_node, listnode)
        {
            if (p_l2_node->key.fid == fid)
            {
                count++;
            }
        }
        break;

    default:
        break;
    }

    return count;
}

/* prameter 1 is node stored in hash.
   prameter 2 is self-defined struct.*/
STATIC int32
_sys_greatbelt_l2_get_count_by_mac_cb(sys_l2_node_t* p_l2_node, sys_l2_mac_cb_t* mac_hash_info)
{
    uint8 match = 0;

    if (NULL == p_l2_node || NULL == mac_hash_info)
    {
        return 0;
    }

    if (sal_memcmp(mac_hash_info->l2_node.key.mac, p_l2_node->key.mac, CTC_ETH_ADDR_LEN))
    {
        match = 0;
    }
    else
    {
        switch (mac_hash_info->query_flag)
        {
        case CTC_L2_FDB_ENTRY_STATIC:
            match = p_l2_node->adptr->flag.is_static;
            break;

        case  CTC_L2_FDB_ENTRY_DYNAMIC:
            match = !p_l2_node->adptr->flag.is_static;
            break;

        case CTC_L2_FDB_ENTRY_LOCAL_DYNAMIC:
            match = (!p_l2_node->adptr->flag.is_static && !p_l2_node->flag.remote_dynamic);
            break;

        case CTC_L2_FDB_ENTRY_ALL:
            match = 1;
            break;

        default:
            break;
        }
    }

    if (match)
    {
        mac_hash_info->count++;
    }
    return 0;
}

STATIC int32
_sys_greatbelt_l2_get_count_by_mac(uint8 lchip, uint8 query_flag, mac_addr_t mac)
{
    sys_l2_mac_cb_t mac_hash_info;

    sal_memset(&mac_hash_info, 0, sizeof(sys_l2_mac_cb_t));
    sal_memcpy(mac_hash_info.l2_node.key.mac, mac, sizeof(mac_addr_t));
    mac_hash_info.query_flag = query_flag;

    mac_hash_info.lchip = lchip;
    ctc_hash_traverse2(pl2_gb_master[lchip]->mac_hash,
                       (hash_traversal_fn) _sys_greatbelt_l2_get_count_by_mac_cb,
                       &(mac_hash_info));

    return mac_hash_info.count;
}

STATIC int32
_sys_greatbelt_l2_get_count_by_mac_fid(uint8 lchip, uint8 query_flag, mac_addr_t mac, uint16 fid)
{
    sys_l2_node_t * p_l2_node = NULL;
    uint8         count       = 0;

    p_l2_node = _sys_greatbelt_l2_fdb_hash_lkup(lchip, mac, fid);

    if (!p_l2_node)
    {
        count = 0;
    }
    else
    {
        switch (query_flag)
        {
        case  CTC_L2_FDB_ENTRY_STATIC:
            count = p_l2_node->adptr->flag.is_static;
            break;

        case CTC_L2_FDB_ENTRY_DYNAMIC:
            count = !p_l2_node->adptr->flag.is_static;
            break;

        case CTC_L2_FDB_ENTRY_LOCAL_DYNAMIC:
            count = (!p_l2_node->adptr->flag.is_static && !p_l2_node->flag.remote_dynamic);
            break;

        case CTC_L2_FDB_ENTRY_ALL:
            count = 1;
            break;

        default:
            break;
        }
    }

    return count;
}

/**
   @brief only count unicast FDB entries
 */
uint32
_sys_greatbelt_l2_get_count_by_all(uint8 lchip, uint8 query_flag)
{
    uint32 count = 0;

    switch (query_flag)
    {
    case  CTC_L2_FDB_ENTRY_DYNAMIC:
        count = pl2_gb_master[lchip]->dynamic_count;
        return count;

    case CTC_L2_FDB_ENTRY_LOCAL_DYNAMIC:
        count = pl2_gb_master[lchip]->local_dynamic_count;
        return count;

    case CTC_L2_FDB_ENTRY_STATIC:
        count = pl2_gb_master[lchip]->total_count - pl2_gb_master[lchip]->dynamic_count;
        return count;

    case CTC_L2_FDB_ENTRY_ALL:
        count = pl2_gb_master[lchip]->total_count;
        return count;

    default:
        break;
    }

    return count;
}

/**
 * get some type fdb count, not include the default entry num
 */
int32
sys_greatbelt_l2_get_fdb_count(uint8 lchip, ctc_l2_fdb_query_t* pq)
{
    SYS_L2_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(pq);
    CTC_MAX_VALUE_CHECK(pq->query_flag, MAX_CTC_L2_FDB_ENTRY_OP - 1);

    CTC_ERROR_RETURN(_sys_greatbelt_l2_query_flush_check(lchip, pq, 0));

    if (!pl2_gb_master[lchip]->cfg_has_sw && (CTC_L2_FDB_ENTRY_OP_BY_MAC_VLAN != pq->query_type))
    {
        return CTC_E_FDB_NO_SW_TABLE_NO_QUARY;
    }

    pq->count = 0;

    L2_LOCK;
    switch (pq->query_type)
    {
    case CTC_L2_FDB_ENTRY_OP_BY_VID:
        pq->count = _sys_greatbelt_l2_get_count_by_fid(lchip, pq->query_flag, pq->fid);
        break;

    case CTC_L2_FDB_ENTRY_OP_BY_PORT:

        pq->count = _sys_greatbelt_l2_get_count_by_port(lchip, pq->query_flag, pq->gport, pq->use_logic_port);
        break;

    case CTC_L2_FDB_ENTRY_OP_BY_PORT_VLAN:
        pq->count = _sys_greatbelt_l2_get_count_by_port_fid(lchip, pq->query_flag, pq->gport, pq->fid, pq->use_logic_port);
        break;

    case CTC_L2_FDB_ENTRY_OP_BY_MAC_VLAN:
        pq->count = _sys_greatbelt_l2_get_count_by_mac_fid(lchip, pq->query_flag, pq->mac, pq->fid);
        break;

    case CTC_L2_FDB_ENTRY_OP_BY_MAC:
        pq->count = _sys_greatbelt_l2_get_count_by_mac(lchip, pq->query_flag, pq->mac);
        break;

    case CTC_L2_FDB_ENTRY_OP_ALL:
        pq->count = _sys_greatbelt_l2_get_count_by_all(lchip, pq->query_flag);
        break;

    default:
        L2_UNLOCK;
        return CTC_E_INVALID_PARAM;
    }
    L2_UNLOCK;

    return CTC_E_NONE;
}


#define  _SW_ADD_REMOVE_

STATIC int32
_sys_greatbelt_l2_spool_remove(uint8 lchip, sys_l2_ad_spool_t* pa, uint8* freed)
{
    sys_l2_ad_spool_t * p_del = NULL;
    int32       ret     = CTC_E_NONE;

    SYS_FDB_DBG_FUNC();
    CTC_PTR_VALID_CHECK(pa);
     /*SYS_FDB_DBG_ERROR("dsfwdptr =%d\n", pa->u0.fwdptr);*/
    SYS_FDB_DBG_ERROR("NHop id =%d\n", pa->u0.nhid);

    p_del = ctc_spool_lookup(pl2_gb_master[lchip]->ad_spool, pa);
    if (!p_del)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    ret = ctc_spool_remove(pl2_gb_master[lchip]->ad_spool, pa, NULL);

    if (ret == CTC_SPOOL_E_OPERATE_MEMORY)
    {
        CTC_ERROR_RETURN(sys_greatbelt_ftm_free_table_offset(lchip, DsMac_t, 1, 1, p_del->index));
        SYS_FDB_DBG_INFO("remove from spool, free ad_index: 0x%x\n", p_del->index);
        if (freed)
        {
            *freed = 1;
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l2_spool_add(uint8 lchip, sys_l2_info_t* psys,
                            sys_l2_ad_spool_t* pa_old,
                            sys_l2_ad_spool_t* pa_new,
                            sys_l2_ad_spool_t** pa_get,
                            uint32* ad_index)
{
    int32 ret = 0;

    SYS_FDB_DBG_FUNC();
    CTC_PTR_VALID_CHECK(psys);

    ret = ctc_spool_add(pl2_gb_master[lchip]->ad_spool, pa_new, NULL, pa_get);

    if (FAIL(ret))
    {
        return CTC_E_SPOOL_ADD_UPDATE_FAILED;
    }
    else
    {
        if (ret == CTC_SPOOL_E_OPERATE_MEMORY)
        {
            /* allocate new ad index */
            ret = sys_greatbelt_ftm_alloc_table_offset(lchip, DsMac_t, 1, 1, ad_index);
            if (FAIL(ret))
            {
                ctc_spool_remove(pl2_gb_master[lchip]->ad_spool, *pa_get, NULL);
                return ret;
            }

            pa_new->index = *ad_index; /*set ad_index to new pa*/
        }
        else
        {
            *ad_index = (*pa_get)->index; /* get old_adindex*/
        }
        SYS_FDB_DBG_INFO("build new hash ucast adindex:0x%x\n", *ad_index);
    }

    return CTC_E_NONE;
}

/* for unicast and mcast.
 * ad index will set back to psys.
 */
STATIC int32
_sys_greatbelt_l2_build_dsmac_index(uint8 lchip, sys_l2_info_t* psys,
                                    sys_l2_ad_spool_t* pa_old,
                                    sys_l2_ad_spool_t* pa_new,
                                    sys_l2_ad_spool_t** pa_get)
{
    int32  ret      = 0;
    uint32 ad_index = 0;

    CTC_PTR_VALID_CHECK(psys);
    SYS_FDB_DBG_FUNC();

    if (!psys->flag.flag_node.rsv_ad_index)
    {
        ret = _sys_greatbelt_l2_spool_add(lchip, psys, pa_old, pa_new, pa_get, &ad_index);
        psys->ad_index = ad_index;
    }
    else
    {
        if (pl2_gb_master[lchip]->cfg_has_sw)
        {
            *pa_get = pa_new;  /* reserve index. so pa_get = pa_new. */
        }
    }

    return ret;
}


/* has_sw = 1 : pa != NULL
 * has_sw = 0 : pa = NULL
 */
STATIC int32
_sys_greatbelt_l2_free_dsmac_index(uint8 lchip, sys_l2_info_t* psys, sys_l2_ad_spool_t* pa, sys_l2_flag_node_t* pn, uint8* freed)
{
    int32  ret = CTC_E_NONE;
    uint8  is_rsv;

    if (freed)
    {
        *freed = 0;
    }

    if (pa && pn)
    {
        is_rsv   = pn->rsv_ad_index;
    }
    else
    {
        CTC_PTR_VALID_CHECK(psys);
        is_rsv   = psys->flag.flag_node.rsv_ad_index;
    }

    /* for discard fdb */
    if (!is_rsv)
    {
        ret = _sys_greatbelt_l2_spool_remove(lchip, pa, freed);
    }
    else if (freed)
    {
        *freed = 1;
    }


    return ret;
}

int32
sys_greatbelt_l2_hw_sync_add_db(uint8 lchip, ctc_l2_addr_t* l2_addr, uint32 nhid, uint32 index)
{
    pl2_gb_master[lchip]->sync_add_db(lchip, l2_addr, nhid, index);
    return CTC_E_NONE;
}

int32
sys_greatbelt_l2_hw_sync_remove_db(uint8 lchip, ctc_l2_addr_t* l2_addr)
{
    pl2_gb_master[lchip]->sync_remove_db(lchip, l2_addr);
    return CTC_E_NONE;
}

int32
sys_greatbelt_l2_hw_sync_alloc_ad_idx(uint8 lchip, uint32* ad_index)
{
    return sys_greatbelt_ftm_alloc_table_offset(lchip, DsMac_t, 1, 1, ad_index);
}

int32
sys_greatbelt_l2_hw_sync_free_ad_idx(uint8 lchip, uint32 ad_index)
{
    return sys_greatbelt_ftm_free_table_offset(lchip, DsMac_t, 1, 1, ad_index);
}

/* normal fdb hash opearation*/
STATIC int32
_sys_greatbelt_l2_fdb_hash_add(uint8 lchip, sys_l2_node_t* p_fdb_node)
{
    if (NULL == ctc_hash_insert(pl2_gb_master[lchip]->fdb_hash, p_fdb_node))
    {
        return CTC_E_FDB_HASH_INSERT_FAILED;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l2_fdb_hash_remove(uint8 lchip, mac_addr_t mac, uint16 fid)
{
    sys_l2_node_t node;

    sal_memset(&node, 0, sizeof(node));
    node.key.fid = fid;
    FDB_SET_MAC(node.key.mac, mac);

    if (!ctc_hash_remove(pl2_gb_master[lchip]->fdb_hash, &node))
    {
        return CTC_E_FDB_HASH_REMOVE_FAILED;
    }
    return CTC_E_NONE;
}

/*fdb fid entry hash opearation*/
uint32
_sys_greatbelt_l2_mac_hash_make(sys_l2_node_t* backet)
{
    return ctc_hash_caculate(CTC_ETH_ADDR_LEN, backet->key.mac);
}

/*fdb mac entry hash opearation*/
STATIC int32
_sys_greatbelt_l2_mac_hash_add(uint8 lchip, sys_l2_node_t* p_fdb_node)
{
    if (NULL == ctc_hash_insert(pl2_gb_master[lchip]->mac_hash, p_fdb_node))
    {
        return CTC_E_FDB_HASH_INSERT_FAILED;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l2_mac_hash_remove(uint8 lchip, sys_l2_node_t* p_fdb_node)
{
    if (NULL == ctc_hash_remove(pl2_gb_master[lchip]->mac_hash, p_fdb_node))
    {
        return CTC_E_FDB_HASH_REMOVE_FAILED;
    }

    return CTC_E_NONE;
}


uint32
_sys_greatbelt_l2_tcam_hash_make(sys_l2_tcam_t* node)
{
    return ctc_hash_caculate(sizeof(sys_l2_tcam_key_t), &(node->key));
}

bool
_sys_greatbelt_l2_tcam_hash_compare(sys_l2_tcam_t* stored_node, sys_l2_tcam_t* lkup_node)
{
    if (!stored_node || !lkup_node)
    {
        return TRUE;
    }

    if (!sal_memcmp(&stored_node->key, &lkup_node->key, sizeof(sys_l2_tcam_key_t)))
    {
        return TRUE;
    }

    return FALSE;
}


uint32
_sys_greatbelt_l2_fdb_hash_make(sys_l2_node_t* node)
{
    return ctc_hash_caculate(sizeof(sys_l2_key_t), &(node->key));
}

bool
_sys_greatbelt_l2_fdb_hash_compare(sys_l2_node_t* stored_node, sys_l2_node_t* lkup_node)
{
    if (!stored_node || !lkup_node)
    {
        return TRUE;
    }

    if (!sal_memcmp(stored_node->key.mac, lkup_node->key.mac, CTC_ETH_ADDR_LEN)
        && (stored_node->key.fid == lkup_node->key.fid))
    {
        return TRUE;
    }

    return FALSE;
}

/* share pool for uc only. and us distinguished by fwdptr */
uint32
_sys_greatbelt_l2_ad_spool_hash_make(sys_l2_ad_spool_t* p_ad)
{
    return ctc_hash_caculate((sizeof(p_ad->u0.nhid) + sizeof(uint16) + sizeof(uint16)),
                             &(p_ad->u0.nhid));
}

int
_sys_greatbelt_l2_ad_spool_hash_cmp(sys_l2_ad_spool_t* p_ad1, sys_l2_ad_spool_t* p_ad2)
{
    SYS_FDB_DBG_FUNC();

    if (!p_ad1 || !p_ad2)
    {
        return FALSE;
    }

    /*
    if ((p_ad1->u0.fwdptr == p_ad2->u0.fwdptr)
        && !sal_memcmp(&p_ad1->flag, &p_ad2->flag, sizeof(uint16)))
    {
        return TRUE;
    }
    */

    if ((p_ad1->u0.nhid == p_ad2->u0.nhid)
        && !sal_memcmp(&p_ad1->flag, &p_ad2->flag, sizeof(uint16))
        && (p_ad1->gport == p_ad2->gport))
    {
        return TRUE;
    }

    return FALSE;
}


STATIC sys_l2_tcam_t*
_sys_greatbelt_l2_tcam_hash_lookup(uint8 lchip, mac_addr_t mac, mac_addr_t mac_mask)
{
    sys_l2_tcam_t tcam;

    sal_memset(&tcam, 0, sizeof(tcam));

    FDB_SET_MAC(tcam.key.mac, mac);
    FDB_SET_MAC(tcam.key.mask, mac_mask);

    return ctc_hash_lookup(pl2_gb_master[lchip]->tcam_hash, &tcam);
}

STATIC int32
_sys_greatbelt_l2_tcam_hash_add(uint8 lchip, sys_l2_tcam_t * p_tcam_node)
{
    if (NULL == ctc_hash_insert(pl2_gb_master[lchip]->tcam_hash, p_tcam_node))
    {
        return CTC_E_FDB_HASH_INSERT_FAILED;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l2_tcam_hash_remove(uint8 lchip, sys_l2_tcam_t * p_tcam_node)
{
    if (NULL == ctc_hash_remove(pl2_gb_master[lchip]->tcam_hash, p_tcam_node))
    {
        return CTC_E_FDB_HASH_REMOVE_FAILED;
    }

    return CTC_E_NONE;
}


STATIC int32
_sys_greatbelt_l2_port_list_add(uint8 lchip, sys_l2_node_t* p_l2_node)
{
    int32             ret    = 0;
    sys_l2_port_node_t* port = NULL;
    ctc_vector_t      * pv   = NULL;
    uint16 temp_gport = 0;

    CTC_PTR_VALID_CHECK(p_l2_node);


    if (!p_l2_node->adptr->flag.logic_port)
    {
        if (DISCARD_PORT == p_l2_node->adptr->gport)
        {
            return CTC_E_NONE;
        }
        pv = pl2_gb_master[lchip]->gport_vec;
        temp_gport = SYS_MAP_CTC_GPORT_TO_DRV_GPORT(p_l2_node->adptr->gport);
    }
    else
    {
        pv = pl2_gb_master[lchip]->vport_vec;
        temp_gport = p_l2_node->adptr->gport;
    }

    port = ctc_vector_get(pv, temp_gport);
    if (NULL == port)
    {
        MALLOC_ZERO(MEM_FDB_MODULE, port, sizeof(sys_l2_port_node_t));
        if (NULL == port)
        {
            CTC_ERROR_GOTO(CTC_E_NO_MEMORY, ret, error_0);
        }

        port->port_list = ctc_list_new();
        if (NULL == port->port_list)
        {
            CTC_ERROR_GOTO(CTC_E_NO_MEMORY, ret, error_1);
        }

        ctc_vector_add(pv, temp_gport, port);
    }

    p_l2_node->port_listnode = ctc_listnode_add_tail(port->port_list, p_l2_node);
    if (NULL == p_l2_node->port_listnode)
    {
        CTC_ERROR_GOTO(CTC_E_NO_MEMORY, ret, error_2);
    }

    _sys_greatbelt_l2_update_count(lchip, &p_l2_node->adptr->flag, &p_l2_node->flag, 1, L2_COUNT_PORT_LIST, port);

    return CTC_E_NONE;

 error_2:
    ctc_list_delete(port->port_list);
 error_1:
    mem_free(port);
 error_0:
    return ret;
}

STATIC int32
_sys_greatbelt_l2_port_list_remove(uint8 lchip, sys_l2_node_t* p_l2_node)
{
    ctc_linklist_t    * fdb_list      = NULL;
    sys_l2_port_node_t* port_fdb_node = NULL;
    CTC_PTR_VALID_CHECK(p_l2_node);
    CTC_PTR_VALID_CHECK(p_l2_node->port_listnode);

    if (!p_l2_node->adptr->flag.logic_port)
    {
        if (DISCARD_PORT == p_l2_node->adptr->gport)
        {
            return CTC_E_NONE;
        }
        port_fdb_node = ctc_vector_get(pl2_gb_master[lchip]->gport_vec, SYS_MAP_CTC_GPORT_TO_DRV_GPORT(p_l2_node->adptr->gport));
    }
    else
    {
        port_fdb_node = ctc_vector_get(pl2_gb_master[lchip]->vport_vec, p_l2_node->adptr->gport);
    }

    if (!port_fdb_node || !port_fdb_node->port_list)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }
    fdb_list = port_fdb_node->port_list;

    _sys_greatbelt_l2_update_count(lchip, &p_l2_node->adptr->flag, &p_l2_node->flag, -1, L2_COUNT_PORT_LIST, port_fdb_node);

    ctc_listnode_delete_node(fdb_list, p_l2_node->port_listnode);
    p_l2_node->port_listnode = NULL;

    /* Assert adindex by port not exist in ad spool*/

    if (0 == CTC_LISTCOUNT(fdb_list))
    {
        ctc_list_free(fdb_list);
        mem_free(port_fdb_node);
        if (!p_l2_node->adptr->flag.logic_port)
        {
            ctc_vector_del(pl2_gb_master[lchip]->gport_vec, SYS_MAP_CTC_GPORT_TO_DRV_GPORT(p_l2_node->adptr->gport));
        }
        else
        {
            ctc_vector_del(pl2_gb_master[lchip]->vport_vec, p_l2_node->adptr->gport);
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l2_fid_list_add(uint8 lchip, sys_l2_node_t* p_l2_node)
{
    sys_l2_fid_node_t* fid = NULL;
    int32            ret   = 0;

    CTC_PTR_VALID_CHECK(p_l2_node);

    if (p_l2_node->key.fid == SYS_L2_MAX_FID)
    {
        return CTC_E_NONE;
    }

    fid = _sys_greatbelt_l2_fid_node_lkup(lchip, p_l2_node->key.fid, GET_FID_NODE);
    if (NULL == fid) /* for new implementation. this logic will never hit.*/
    {
        MALLOC_ZERO(MEM_FDB_MODULE, fid, sizeof(sys_l2_fid_node_t));
        if (NULL == fid)
        {
            CTC_ERROR_GOTO(CTC_E_NO_MEMORY, ret, error_0);
        }

        fid->fid_list = ctc_list_new();
        if (NULL == fid->fid_list)
        {
            CTC_ERROR_GOTO(CTC_E_NO_MEMORY, ret, error_1);
        }

        fid->fid = p_l2_node->key.fid;
        ctc_vector_add(pl2_gb_master[lchip]->fid_vec, p_l2_node->key.fid, fid);
    }
    p_l2_node->vlan_listnode = ctc_listnode_add_tail(fid->fid_list, p_l2_node);
    if (NULL == p_l2_node->vlan_listnode)
    {
        CTC_ERROR_GOTO(CTC_E_NO_MEMORY, ret, error_2);
    }

    _sys_greatbelt_l2_update_count(lchip, &p_l2_node->adptr->flag, &p_l2_node->flag, 1, L2_COUNT_FID_LIST, fid);

    return CTC_E_NONE;
 error_2:
    ctc_list_delete(fid->fid_list);
 error_1:
    mem_free(fid);
 error_0:
    return ret;
}

STATIC int32
_sys_greatbelt_l2_fid_list_remove(uint8 lchip, sys_l2_node_t* p_l2_node)
{
    sys_l2_fid_node_t* fid_node = NULL;
    CTC_PTR_VALID_CHECK(p_l2_node);
    CTC_PTR_VALID_CHECK(p_l2_node->vlan_listnode);

    if (p_l2_node->key.fid == SYS_L2_MAX_FID)
    {
        return CTC_E_NONE;
    }

    fid_node = _sys_greatbelt_l2_fid_node_lkup(lchip, p_l2_node->key.fid, GET_FID_NODE);
    if (NULL == fid_node)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    _sys_greatbelt_l2_update_count(lchip, &p_l2_node->adptr->flag, &p_l2_node->flag, -1, L2_COUNT_FID_LIST, fid_node);

    ctc_listnode_delete_node(fid_node->fid_list, p_l2_node->vlan_listnode);
    p_l2_node->vlan_listnode = NULL;

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l2_mc2nhop_add(uint8 lchip, uint16 l2mc_grp_id, uint32 nhp_id)
{
    sys_l2_mcast_node_t *p_node = NULL;

    p_node = ctc_vector_get(pl2_gb_master[lchip]->mcast2nh_vec, l2mc_grp_id);
    if (NULL == p_node)
    {
        MALLOC_ZERO(MEM_FDB_MODULE, p_node, sizeof(sys_l2_mcast_node_t));
        if (NULL == p_node)
        {
            return CTC_E_NO_MEMORY;
        }

        p_node->nhid = nhp_id;
        ctc_vector_add(pl2_gb_master[lchip]->mcast2nh_vec, l2mc_grp_id, p_node);
    }
    else
    {
        p_node->nhid = nhp_id;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l2_mc2nhop_remove(uint8 lchip, uint16 l2mc_grp_id)
{
    sys_l2_mcast_node_t *p_node = NULL;

    p_node = ctc_vector_get(pl2_gb_master[lchip]->mcast2nh_vec, l2mc_grp_id);
    if (NULL == p_node)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }
    else
    {
        ctc_vector_del(pl2_gb_master[lchip]->mcast2nh_vec, l2mc_grp_id);
        mem_free(p_node);
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l2_get_nhop_by_mc_grp_id(uint8 lchip, uint16 l2mc_grp_id, uint32* p_nhp_id)
{
    sys_l2_mcast_node_t *p_node = NULL;

    p_node = ctc_vector_get(pl2_gb_master[lchip]->mcast2nh_vec, l2mc_grp_id);
    if (NULL == p_node)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }
    else
    {
       *p_nhp_id = p_node->nhid;
    }

    return CTC_E_NONE;
}


#define _FDB_FLUSH_
STATIC int32
_sys_greatbelt_l2uc_remove_sw(uint8 lchip, sys_l2_node_t* pn)
{
    _sys_greatbelt_l2_port_list_remove(lchip, pn);
    _sys_greatbelt_l2_fid_list_remove(lchip, pn);
    _sys_greatbelt_l2_mac_hash_remove(lchip, pn);
    _sys_greatbelt_l2_fdb_hash_remove(lchip, pn->key.mac, pn->key.fid);
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l2uc_remove_sw_for_flush(uint8 lchip, sys_l2_node_t* pn)
{
    uint8 freed = 0;
    _sys_greatbelt_l2uc_remove_sw(lchip, pn);
    _sys_greatbelt_l2_free_dsmac_index(lchip, NULL, pn->adptr, &pn->flag, &freed);
    _sys_greatbelt_l2_update_count(lchip, &pn->adptr->flag, &pn->flag, -1, L2_COUNT_GLOBAL, &pn->key_index);
    if(freed)
    {
        mem_free(pn->adptr);
    }
    mem_free(pn);
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l2uc_remove_sw_for_flush_traverse(uint8 lchip, sys_l2_node_t* pn)
{
    uint8 freed = 0;
    _sys_greatbelt_l2_port_list_remove(lchip, pn);
    _sys_greatbelt_l2_fid_list_remove(lchip, pn);
    _sys_greatbelt_l2_mac_hash_remove(lchip, pn);
    _sys_greatbelt_l2_free_dsmac_index(lchip, NULL, pn->adptr, &pn->flag, &freed);
    _sys_greatbelt_l2_update_count(lchip, &pn->adptr->flag, &pn->flag, -1, L2_COUNT_GLOBAL, &pn->key_index);
    if(freed)
    {
        mem_free(pn->adptr);
    }
    mem_free(pn);
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l2_flush_fdb_cb(sys_l2_node_t* p_l2_node, sys_l2_flush_t* pf)
{
    uint8 lchip = 0;

    CTC_PTR_VALID_CHECK(p_l2_node);
    CTC_PTR_VALID_CHECK(pf);
    lchip = pf->lchip;

    if (p_l2_node->flag.system_mac ||
        p_l2_node->flag.type_l2mc)
    {
        return 0;
    }

    RETURN_IF_FLAG_NOT_MATCH(pf->flush_flag, p_l2_node->adptr->flag, p_l2_node->flag);

    if (0 == pf->flush_fdb_cnt_per_loop)
    {
        return 0;
    }
    CTC_ERROR_RETURN(_sys_greatbelt_l2_delete_hw_by_mac_fid(lchip, p_l2_node->key.mac, p_l2_node->key.fid));
    _sys_greatbelt_l2uc_remove_sw_for_flush_traverse(lchip, p_l2_node);

    if ((pf->flush_fdb_cnt_per_loop) > 0)
    {
        --pf->flush_fdb_cnt_per_loop;
    }

    SYS_FDB_FLUSH_SLEEP(pf->sleep_cnt);

    return 1;
}

STATIC int32
_sys_greatbelt_l2_flush_by_all(uint8 lchip, uint8 flush_flag)
{
    sys_l2_flush_t flush_info;

    sal_memset(&flush_info, 0, sizeof(flush_info));
    flush_info.flush_fdb_cnt_per_loop
                          = pl2_gb_master[lchip]->cfg_flush_cnt ? pl2_gb_master[lchip]->cfg_flush_cnt : CTC_MAX_UINT32_VALUE;
    flush_info.flush_flag = flush_flag;
    flush_info.lchip = lchip;

    ctc_hash_traverse_remove(pl2_gb_master[lchip]->fdb_hash,
                             (hash_traversal_fn) _sys_greatbelt_l2_flush_fdb_cb,
                             &flush_info);

    if (0 == pl2_gb_master[lchip]->cfg_flush_cnt || flush_info.flush_fdb_cnt_per_loop > 0)
    {
        return CTC_E_NONE;
    }

    return CTC_E_FDB_OPERATION_PAUSE;
}

/**
   @brief flush fdb entry by port
 */
STATIC int32
_sys_greatbelt_l2_flush_by_port(uint8 lchip, uint16 gport, uint32 flush_flag, uint8 use_logic_port)
{
    ctc_listnode_t     * next_node            = NULL;
    ctc_listnode_t     * node                 = NULL;
    ctc_linklist_t     * fdb_list             = NULL;
    sys_l2_node_t      * p_l2_node            = NULL;
    sys_l2_port_node_t * port                 = NULL;
    int32              ret                    = 0;
    uint32             flush_fdb_cnt_per_loop = 0;
    uint16             sleep_cnt = 0;

    if (!use_logic_port)
    {
        port = ctc_vector_get(pl2_gb_master[lchip]->gport_vec, SYS_MAP_CTC_GPORT_TO_DRV_GPORT(gport));
    }
    else
    {
        port = ctc_vector_get(pl2_gb_master[lchip]->vport_vec, gport);
    }

    if (!port || !port->port_list)
    {
        return CTC_E_NONE;
    }

    fdb_list = port->port_list;

    SYS_L2_FDB_FLUSH_COUNT_SET(flush_fdb_cnt_per_loop);
    CTC_LIST_LOOP_DEL(fdb_list, p_l2_node, node, next_node)
    {
        if (p_l2_node->flag.system_mac)
        {
            continue;
        }
        CONTINUE_IF_FLAG_NOT_MATCH(flush_flag, p_l2_node->adptr->flag, p_l2_node->flag);

        CTC_ERROR_RETURN(_sys_greatbelt_l2_delete_hw_by_mac_fid(lchip, p_l2_node->key.mac, p_l2_node->key.fid));
        _sys_greatbelt_l2uc_remove_sw_for_flush(lchip, p_l2_node);

        SYS_L2_FDB_FLUSH_COUNT_PROCESS(flush_fdb_cnt_per_loop);

        SYS_FDB_FLUSH_SLEEP(sleep_cnt);
    }

    return ret;
}

/**
   @brief flush fdb entry by port+vlan
 */
STATIC int32
_sys_greatbelt_l2_flush_by_port_fid(uint8 lchip, uint16 gport, uint16 fid, uint32 flush_flag, uint8 use_logic_port)
{
    ctc_listnode_t * next_node = NULL;
    ctc_listnode_t * node      = NULL;
    ctc_linklist_t * fdb_list  = NULL;
    sys_l2_node_t  * p_l2_node = NULL;
    int32          ret         = 0;
    uint32         flush_fdb_cnt_per_loop;
    uint16         sleep_cnt = 0;

    SYS_L2_FDB_FLUSH_COUNT_SET(flush_fdb_cnt_per_loop);

    fdb_list = _sys_greatbelt_l2_fid_node_lkup(lchip, fid, GET_FID_LIST);

    CTC_LIST_LOOP_DEL(fdb_list, p_l2_node, node, next_node)
    {
        if (p_l2_node->flag.system_mac)
        {
            continue;
        }
        if (use_logic_port && (!p_l2_node->adptr->flag.logic_port))
        {
            continue;
        }
        CONTINUE_IF_FLAG_NOT_MATCH(flush_flag, p_l2_node->adptr->flag, p_l2_node->flag);

        if (p_l2_node->adptr->gport != gport)
        {
            continue;
        }

        CTC_ERROR_RETURN(_sys_greatbelt_l2_delete_hw_by_mac_fid(lchip, p_l2_node->key.mac, p_l2_node->key.fid));
        _sys_greatbelt_l2uc_remove_sw_for_flush(lchip, p_l2_node);

        SYS_L2_FDB_FLUSH_COUNT_PROCESS(flush_fdb_cnt_per_loop);

        SYS_FDB_FLUSH_SLEEP(sleep_cnt);
    }

    return ret;
}

/**
   @brief flush fdb entry by mac+vlan
 */
STATIC int32
_sys_greatbelt_l2_flush_by_mac_fid(uint8 lchip, mac_addr_t mac, uint16 fid, uint32 flush_flag)
{
    sys_l2_node_t * p_l2_node = NULL;

    p_l2_node = _sys_greatbelt_l2_fdb_hash_lkup(lchip, mac, fid);
    if (NULL == p_l2_node)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    if (p_l2_node->flag.system_mac)
    {
        return CTC_E_NONE;
    }

    switch (flush_flag)
    {
    case CTC_L2_FDB_ENTRY_STATIC:
        if (!p_l2_node->adptr->flag.is_static)
        {
            return CTC_E_ENTRY_NOT_EXIST;
        }
        break;

    case  CTC_L2_FDB_ENTRY_DYNAMIC:
        if (p_l2_node->adptr->flag.is_static)
        {
            return CTC_E_ENTRY_NOT_EXIST;
        }
        break;

    case CTC_L2_FDB_ENTRY_LOCAL_DYNAMIC:
        if (p_l2_node->flag.remote_dynamic || p_l2_node->adptr->flag.is_static)
        {
            return CTC_E_ENTRY_NOT_EXIST;
        }
        break;

    case CTC_L2_FDB_ENTRY_ALL:
    default:
        break;
    }

    CTC_ERROR_RETURN(_sys_greatbelt_l2_delete_hw_by_mac_fid(lchip, p_l2_node->key.mac, p_l2_node->key.fid));
    _sys_greatbelt_l2uc_remove_sw_for_flush(lchip, p_l2_node);

    return CTC_E_NONE;
}

/**
   @brief flush fdb entry by vlan
 */
STATIC int32
_sys_greatbelt_l2_flush_by_fid(uint8 lchip, uint16 fid, uint32 flush_flag)
{
    ctc_listnode_t * next_node = NULL;
    ctc_listnode_t * node      = NULL;
    ctc_linklist_t * fdb_list  = NULL;
    sys_l2_node_t  * p_l2_node = NULL;
    int32          ret         = 0;
    uint32         flush_fdb_cnt_per_loop;
    uint16         sleep_cnt = 0;

    SYS_L2_FDB_FLUSH_COUNT_SET(flush_fdb_cnt_per_loop);

    fdb_list = _sys_greatbelt_l2_fid_node_lkup(lchip, fid, GET_FID_LIST);

    CTC_LIST_LOOP_DEL(fdb_list, p_l2_node, node, next_node)
    {
        if (p_l2_node->flag.system_mac)
        {
            continue;
        }
        CONTINUE_IF_FLAG_NOT_MATCH(flush_flag, p_l2_node->adptr->flag, p_l2_node->flag);

        CTC_ERROR_RETURN(_sys_greatbelt_l2_delete_hw_by_mac_fid(lchip, p_l2_node->key.mac, p_l2_node->key.fid));
        _sys_greatbelt_l2uc_remove_sw_for_flush(lchip, p_l2_node);

        SYS_L2_FDB_FLUSH_COUNT_PROCESS(flush_fdb_cnt_per_loop);

        SYS_FDB_FLUSH_SLEEP(sleep_cnt);
    }

    return ret;
}

/**
   @brief flush fdb entry by mac
 */
STATIC int32
_sys_greatbelt_l2_flush_by_mac_cb(sys_l2_node_t* p_l2_node, sys_l2_mac_cb_t* mac_hash_info)
{
    uint8 lchip = 0;

    if (NULL == p_l2_node || NULL == mac_hash_info)
    {
        return 0;
    }
    lchip = mac_hash_info->lchip;
    if (sal_memcmp(mac_hash_info->l2_node.key.mac, p_l2_node->key.mac, CTC_ETH_ADDR_LEN))
    {
        return 0;
    }

    if (p_l2_node->flag.system_mac)
    {
        return 0;
    }
    RETURN_IF_FLAG_NOT_MATCH(mac_hash_info->query_flag, p_l2_node->adptr->flag, p_l2_node->flag)

    CTC_ERROR_RETURN(_sys_greatbelt_l2_delete_hw_by_mac_fid(lchip, p_l2_node->key.mac, p_l2_node->key.fid));
    _sys_greatbelt_l2uc_remove_sw_for_flush(lchip, p_l2_node);

    return 0;
}

/**
   @brief flush fdb entry by mac
 */
STATIC int32
_sys_greatbelt_l2_flush_by_mac(uint8 lchip, mac_addr_t mac, uint8 flush_flag)
{
    sys_l2_mac_cb_t   mac_hash_info;
    hash_traversal_fn fun = NULL;

    sal_memset(&mac_hash_info, 0, sizeof(sys_l2_mac_cb_t));
    FDB_SET_MAC(mac_hash_info.l2_node.key.mac, mac);
    mac_hash_info.query_flag = flush_flag;
    mac_hash_info.lchip = lchip;

    fun = (hash_traversal_fn) _sys_greatbelt_l2_flush_by_mac_cb;

    ctc_hash_traverse2_remove(pl2_gb_master[lchip]->mac_hash, fun, &(mac_hash_info));

    return CTC_E_NONE;
}
STATIC int32
_sys_greatbelt_l2_flush_by_sw(uint8 lchip, ctc_l2_flush_fdb_t* pf)
{
    int32   ret = CTC_E_NONE;

    L2_LOCK;
    switch (pf->flush_type)
    {
    case CTC_L2_FDB_ENTRY_OP_BY_VID:
        ret = _sys_greatbelt_l2_flush_by_fid(lchip, pf->fid, pf->flush_flag);
        break;

    case CTC_L2_FDB_ENTRY_OP_BY_PORT:
        ret = _sys_greatbelt_l2_flush_by_port(lchip, pf->gport, pf->flush_flag, pf->use_logic_port);
        break;

    case CTC_L2_FDB_ENTRY_OP_BY_PORT_VLAN:
        ret = _sys_greatbelt_l2_flush_by_port_fid(lchip, pf->gport, pf->fid, pf->flush_flag, pf->use_logic_port);
        break;

    case CTC_L2_FDB_ENTRY_OP_BY_MAC_VLAN:
        ret = _sys_greatbelt_l2_flush_by_mac_fid(lchip, pf->mac, pf->fid, pf->flush_flag);
        break;

    case CTC_L2_FDB_ENTRY_OP_BY_MAC:
        ret = _sys_greatbelt_l2_flush_by_mac(lchip, pf->mac, pf->flush_flag);
        break;

    case CTC_L2_FDB_ENTRY_OP_ALL:
        ret = _sys_greatbelt_l2_flush_by_all(lchip, pf->flush_flag);
        break;

    default:
        ret = CTC_E_INVALID_PARAM;
        break;
    }

    L2_UNLOCK;

    return ret;
}

int32
sys_greatbelt_l2_flush_fdb(uint8 lchip, ctc_l2_flush_fdb_t* pf)
{
    int32                      ret             = CTC_E_NONE;
    ctc_learning_action_info_t learning_action = { 0 };

    SYS_L2_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(pf);
    CTC_ERROR_RETURN(_sys_greatbelt_l2_query_flush_check(lchip, pf, 1));

    if (pf->use_logic_port
    && ((pf->flush_type != CTC_L2_FDB_ENTRY_OP_BY_PORT)
    && (pf->flush_type != CTC_L2_FDB_ENTRY_OP_BY_PORT_VLAN)))
    {
        return CTC_E_FEATURE_NOT_SUPPORT;
    }

    if (pl2_gb_master[lchip]->cfg_has_sw && pl2_gb_master[lchip]->cfg_hw_learn) /* disable learn*/
    {
        learning_action.action = CTC_LEARNING_ACTION_MAC_TABLE_FULL;
        learning_action.value  = 1;
        CTC_ERROR_RETURN(sys_greatbelt_learning_set_action(lchip, &learning_action));
    }

    ret = _sys_greatbelt_l2_flush_by_sw(lchip, pf);
    if ((ret < 0) && (!pl2_gb_master[lchip]->cfg_has_sw))
    {
        ret = CTC_E_NONE;
    }

    if (!pl2_gb_master[lchip]->cfg_has_sw)
    {
        if (CTC_L2_FDB_ENTRY_LOCAL_DYNAMIC == pf->flush_flag)
        {
            return CTC_E_FEATURE_NOT_SUPPORT;
        }

        ret = _sys_greatbelt_l2_flush_by_hw(lchip, pf);
    }

    if (pl2_gb_master[lchip]->cfg_has_sw && pl2_gb_master[lchip]->cfg_hw_learn) /* enable learn */
    {
        learning_action.action = CTC_LEARNING_ACTION_MAC_TABLE_FULL;
        learning_action.value  = 0;
        CTC_ERROR_RETURN(sys_greatbelt_learning_set_action(lchip, &learning_action));
    }

    return ret;
}

int32
sys_greatbelt_l2_set_dsmac_for_linkagg_hw_learning(uint8 lchip, uint16 gport, bool b_add)
{
    uint32 fwd_offset;
    uint16          dest_id = 0;
    ds_mac_t        ds_mac;
    uint32          cmd         = 0;
    uint32          ds_mac_base = 0;
    sys_l2_info_t   sys;

    sal_memset(&ds_mac, 0, sizeof(ds_mac_t));
    sal_memset(&sys, 0, sizeof(sys));

    dest_id = CTC_MAP_GPORT_TO_LPORT(gport);

    ds_mac_base = (pl2_gb_master[lchip]->base_trunk);

    if (b_add)
    {
        CTC_ERROR_RETURN(sys_greatbelt_brguc_get_dsfwd_offset(lchip, gport, CTC_NH_PARAM_BRGUC_SUB_TYPE_BASIC, &fwd_offset));
        sys.fwd_ptr =  fwd_offset;
        sys.gport          = gport;
        sys.flag.flag_ad.is_static = 0;    /* src_mismatch_learn_en, aging_valid*/
        sys.dsfwd_valid = 1;
        /* write Ad */
        cmd = DRV_IOW(DsMac_t, DRV_ENTRY_FLAG);

            CTC_ERROR_RETURN(_sys_greatbelt_l2_encode_dsmac(lchip, &ds_mac, &sys));
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, ds_mac_base + dest_id, cmd, &ds_mac));

    }

    /* no need consider delete. actually the dsmac should be set at the initial process.
     * the reason not doing that is because linkagg nexthop is not created yet, back then.
     * Unless nexthop reserved linkagg nexthop, fdb cannot do it at the initial process.
     */

    return CTC_E_NONE;
}

int32
sys_greatbelt_l2_flush_fid(uint8 lchip, uint16 fid, uint8 enable)
{
    uint32  cmd_fib    = 0;
    uint32  cmd_ipe    = 0;
    fib_engine_lookup_result_ctl_t fib_engine_lookup_result_ctl;
    ipe_lookup_ctl_t ipe_lookup_ctl;

    SYS_L2UC_FID_CHECK(fid);

    sal_memset(&fib_engine_lookup_result_ctl, 0, sizeof(fib_engine_lookup_result_ctl_t));
    sal_memset(&ipe_lookup_ctl, 0, sizeof(ipe_lookup_ctl_t));

    cmd_fib = DRV_IOR(FibEngineLookupResultCtl_t, DRV_ENTRY_FLAG);
    cmd_ipe = DRV_IOR(IpeLookupCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd_fib, &fib_engine_lookup_result_ctl));
    fib_engine_lookup_result_ctl.fdb_flooding_vsi_en = enable ? 1 : 0;
    fib_engine_lookup_result_ctl.fdb_flooding_vsi = enable? fid : 0;

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd_ipe, &ipe_lookup_ctl));
    ipe_lookup_ctl.fdb_flush_vsi_learning_disable = enable ? 0 : 1;
    ipe_lookup_ctl.fdb_flush_vsi = enable? fid : 0;

    cmd_fib = DRV_IOW(FibEngineLookupResultCtl_t, DRV_ENTRY_FLAG);
    cmd_ipe = DRV_IOW(IpeLookupCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd_fib, &fib_engine_lookup_result_ctl));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd_ipe, &ipe_lookup_ctl));
    return CTC_E_NONE;
}


#define _FDB_HW_
STATIC int32
_sys_greatbelt_l2_build_tcam_key(uint8 lchip, mac_addr_t mac, mac_addr_t mac_mask, tbl_entry_t* mackey)
{
    ds_mac_bridge_key_t* data = (ds_mac_bridge_key_t*)mackey->data_entry;
    ds_mac_bridge_key_t* mask = (ds_mac_bridge_key_t*)mackey->mask_entry;

    sal_memset(data, 0, sizeof(ds_mac_bridge_key_t));
    sal_memset(mask, 0, sizeof(ds_mac_bridge_key_t));

    data->mapped_mac_da31_0 = mac[5] | (mac[4] << 8) | (mac[3] << 16) | (mac[2] << 24);
    data->mapped_mac_da47_32= (mac[0] << 8) | mac[1];

    mask->mapped_mac_da31_0 = mac_mask[5] | (mac_mask[4] << 8) | (mac_mask[3] << 16) | (mac_mask[2] << 24);
    mask->mapped_mac_da47_32= (mac_mask[0] << 8) | mac_mask[1];

    mask->vsi_id = 0; /* don't care fid */

    data->table_id0 = IPV4DA_MAC_TABLEID;
    data->table_id1 = IPV4DA_MAC_TABLEID;
    data->sub_table_id0 = MAC_DA_SUB_TABLEID;
    data->sub_table_id1 = MAC_DA_SUB_TABLEID;
    mask->table_id0 = 0x7;
    mask->table_id1 = 0x7;
    mask->sub_table_id0 = 0x3;
    mask->sub_table_id1 = 0x3;

    return CTC_E_NONE;
}


STATIC int32
_sys_greatbelt_l2_write_hw(uint8 lchip, sys_l2_info_t* psys)
{
    uint32   cmd         = 0;
    uint8    domain_type = 0;
    tbl_entry_t         tcam_key;
    ds_mac_bridge_key_t mackey_data;
    ds_mac_bridge_key_t mackey_mask;
    ds_mac_t ds_mac;

    SYS_FDB_DBG_FUNC();
    SYS_FDB_DBG_INFO("Write Hw \n");

    /* write Ad */
    CTC_ERROR_RETURN(_sys_greatbelt_l2_encode_dsmac(lchip, &ds_mac, psys));
    if (!psys->flag.flag_node.rsv_ad_index)
    {
        if(psys->flag.flag_node.is_tcam)
        {
            cmd = DRV_IOW(DsMacTcam_t, DRV_ENTRY_FLAG);
        }
        else
        {
            cmd = DRV_IOW(DsMac_t, DRV_ENTRY_FLAG);
        }
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, psys->ad_index, cmd, &ds_mac));
    }
    if(psys->flag.flag_ad.aging_disable)
    {
        cmd = DRV_IOW(DsMac_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, psys->ad_index, cmd, &ds_mac));
    }

    if (psys->flag.flag_node.is_tcam) /* tcam */
    {
        sal_memset(&tcam_key, 0 ,sizeof(tcam_key));
        tcam_key.data_entry = (uint32*)&mackey_data;
        tcam_key.mask_entry = (uint32*)&mackey_mask;
        _sys_greatbelt_l2_build_tcam_key(lchip, psys->mac, psys->mac_mask, &tcam_key);
        cmd = DRV_IOW(DsMacBridgeKey_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, psys->key_index, cmd, &tcam_key));
    }
    else if (!psys->flag.flag_node.type_default) /* uc or mc */
    {
        drv_fib_acc_in_t  in;
        drv_fib_acc_out_t out;

        sal_memset(&in, 0, sizeof(in));
        sal_memset(&out, 0, sizeof(out));
        /* write Key */
        FDB_SET_HW_MAC(in.mac.mac, psys->mac);
        in.mac.fid      = psys->fid;
        in.mac.ad_index = psys->ad_index;

        CTC_ERROR_RETURN(drv_fib_acc(lchip, DRV_FIB_ACC_WRITE_MAC_BY_KEY, &in, &out));
        if (out.mac.conflict)
        {
            return CTC_E_FDB_HASH_CONFLICT;
        }
        psys->key_index = out.mac.key_index;
        if (!(psys->flag.flag_ad.is_static))
        {
            domain_type = SYS_AGING_MAC_HASH;
            CTC_ERROR_RETURN(sys_greatbelt_aging_set_aging_status(lchip, domain_type, psys->key_index, TRUE));
        }
    }

    SYS_FDB_DBG_INFO(" HW: MAC:%s, FID:%d, AdIndex:%d, KeyIndex:%d\n",
                     sys_greatbelt_output_mac(psys->mac), psys->fid, psys->ad_index, psys->key_index);

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l2_write_dskey(uint8 lchip, sys_l2_info_t* psys)
{
    uint32   cmd         = 0;
    uint8    domain_type = 0;
    tbl_entry_t         tcam_key;
    ds_mac_bridge_key_t mackey_data;
    ds_mac_bridge_key_t mackey_mask;

    SYS_FDB_DBG_FUNC();
    SYS_FDB_DBG_INFO("Write Dskey \n");

    if (psys->flag.flag_node.is_tcam) /* tcam */
    {
        sal_memset(&tcam_key, 0 ,sizeof(tcam_key));
        tcam_key.data_entry = (uint32*)&mackey_data;
        tcam_key.mask_entry = (uint32*)&mackey_mask;
        _sys_greatbelt_l2_build_tcam_key(lchip, psys->mac, psys->mac_mask, &tcam_key);
        cmd = DRV_IOW(DsMacBridgeKey_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, psys->key_index, cmd, &tcam_key));
    }
    else if (!psys->flag.flag_node.type_default) /* uc or mc */
    {
        drv_fib_acc_in_t  in;
        drv_fib_acc_out_t out;

        sal_memset(&in, 0, sizeof(in));
        sal_memset(&out, 0, sizeof(out));
        /* write Key */
        FDB_SET_HW_MAC(in.mac.mac, psys->mac);
        in.mac.fid      = psys->fid;
        in.mac.ad_index = psys->ad_index;

        CTC_ERROR_RETURN(drv_fib_acc(lchip, DRV_FIB_ACC_WRITE_MAC_BY_KEY, &in, &out));
        if (out.mac.conflict)
        {
            return CTC_E_FDB_HASH_CONFLICT;
        }
        psys->key_index = out.mac.key_index;
        if (!(psys->flag.flag_ad.is_static))
        {
            domain_type = SYS_AGING_MAC_HASH;
            CTC_ERROR_RETURN(sys_greatbelt_aging_set_aging_status(lchip, domain_type, psys->key_index, TRUE));
        }
    }

    SYS_FDB_DBG_INFO(" HW: MAC:%s, FID:%d, AdIndex:%d, KeyIndex:%d\n",
                     sys_greatbelt_output_mac(psys->mac), psys->fid, psys->ad_index, psys->key_index);

    return CTC_E_NONE;
}

/**
   @brief add new fdb entry
 */
STATIC int32
_sys_greatbelt_l2uc_add_sw(uint8 lchip, sys_l2_node_t* p_l2_node)
{
    int32 ret = CTC_E_NONE;

    CTC_ERROR_GOTO(_sys_greatbelt_l2_port_list_add(lchip, p_l2_node), ret, error_4);
    CTC_ERROR_GOTO(_sys_greatbelt_l2_fid_list_add(lchip, p_l2_node), ret, error_3);
    CTC_ERROR_GOTO(_sys_greatbelt_l2_fdb_hash_add(lchip, p_l2_node), ret, error_2);
    CTC_ERROR_GOTO(_sys_greatbelt_l2_mac_hash_add(lchip, p_l2_node), ret, error_1);
    return ret;

 error_1:
    _sys_greatbelt_l2_fdb_hash_remove(lchip, p_l2_node->key.mac, p_l2_node->key.fid);
 error_2:
    _sys_greatbelt_l2_fid_list_remove(lchip, p_l2_node);
 error_3:
    _sys_greatbelt_l2_port_list_remove(lchip, p_l2_node);
 error_4:
    return ret;
}

STATIC int32
_sys_greatbelt_l2uc_update_sw(uint8 lchip, sys_l2_ad_spool_t* src_ad, sys_l2_ad_spool_t* dst_ad, sys_l2_node_t* pn_new)
{
    pn_new->adptr = src_ad;
    /* must do it first. otherwise, the src_ad port_listnode will be missing.*/
    _sys_greatbelt_l2_port_list_remove(lchip, pn_new);
    _sys_greatbelt_l2_fid_list_remove(lchip, pn_new);

    pn_new->adptr = dst_ad; /*recover it back.*/
    CTC_ERROR_RETURN(_sys_greatbelt_l2_port_list_add(lchip, pn_new));
    CTC_ERROR_RETURN(_sys_greatbelt_l2_fid_list_add(lchip, pn_new));

    return CTC_E_NONE;
}

int32
sys_greatbelt_l2_dump_l2master(uint8 lchip)
{
    SYS_L2_INIT_CHECK(lchip);
    SYS_FDB_DBG_DUMP("sw_hash_tbl_size           0x%x \n" \
                     "hash_base                  0x%x \n" \
                     "pure_hash_num              0x%x \n" \
                     "total_count                0x%x \n" \
                     "dynmac_count               0x%x \n" \
                     "local_dynmac_count         0x%x \n" \
                     "ad_index_drop              0x%x \n" \
                     "dft_entry_base             0x%x \n" \
                     "max_fid_value              0x%x \n" \
                     "flush_fdb_cnt_per_loop     0x%x \n" \
                     "hw_learn_en                0x%x \n" \
                     "has_sw_table               0x%x \n",
                     pl2_gb_master[lchip]->extra_sw_size,
                     pl2_gb_master[lchip]->hash_base,
                     pl2_gb_master[lchip]->pure_hash_num,
                     pl2_gb_master[lchip]->total_count,
                     pl2_gb_master[lchip]->dynamic_count,
                     pl2_gb_master[lchip]->local_dynamic_count,
                     pl2_gb_master[lchip]->ad_index_drop,
                     pl2_gb_master[lchip]->dft_entry_base,
                     pl2_gb_master[lchip]->cfg_max_fid,
                     pl2_gb_master[lchip]->cfg_flush_cnt,
                     pl2_gb_master[lchip]->cfg_hw_learn,
                     pl2_gb_master[lchip]->cfg_has_sw);

    SYS_FDB_DBG_DUMP("hw_aging_en                0x%x \n" , pl2_gb_master[lchip]->cfg_hw_learn);
    if (!pl2_gb_master[lchip]->vp_alloc_ad_en)
    {
        SYS_FDB_DBG_DUMP("base_vport                 0x%x \n" , pl2_gb_master[lchip]->base_vport);
    }
    SYS_FDB_DBG_DUMP("base_gport                 0x%x \n" \
                     "base_trunk                 0x%x \n" \
                     "logic_port_num             0x%x \n",
                     pl2_gb_master[lchip]->base_gport,   \
                     pl2_gb_master[lchip]->base_trunk,   \
                     pl2_gb_master[lchip]->cfg_vport_num);

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l2_get_reserved_index(uint8 lchip, sys_l2_info_t* psys)
{
    int32  ret              = CTC_E_NONE;
    uint8  is_local_chip    = 0;
    uint8  is_local_dynamic = 0;
    uint8  is_trunk         = 0;
    uint8  is_phy_port      = 0;
    uint8  is_logic_port    = 0;
    uint8  is_binding       = 0;
    uint32 ad_index         = 0;
    uint8  other_flag    = 0;
    uint8 gchip_id          = 0;

    psys->flag.flag_node.rsv_ad_index = 1;
    other_flag += psys->flag.flag_ad.src_discard;
    other_flag += psys->flag.flag_ad.src_discard_to_cpu;
    other_flag += psys->flag.flag_ad.logic_port;
    other_flag += psys->flag.flag_ad.copy_to_cpu;
    other_flag += psys->flag.flag_ad.protocol_entry;
    other_flag += psys->flag.flag_ad.bind_port;
    other_flag += psys->flag.flag_ad.ptp_entry;
    other_flag += psys->flag.flag_ad.aging_disable;

    /* for discard fdb */
    if (psys->flag.flag_ad.drop && (0 == other_flag))
    {
        ad_index = pl2_gb_master[lchip]->ad_index_drop;
        SYS_FDB_DBG_INFO("build drop ucast ad_index:0x%x\n", ad_index);
        goto END;
    }

    gchip_id = CTC_MAP_GPORT_TO_GCHIP(psys->gport);
    is_local_chip = sys_greatbelt_chip_is_local(lchip, gchip_id)
                    || CTC_IS_LINKAGG_PORT(psys->gport);

    is_local_dynamic = (is_local_chip && !psys->flag.flag_ad.is_static && !psys->flag.flag_ad.bind_port);

    is_logic_port = psys->flag.flag_ad.logic_port;
    is_trunk      = !is_logic_port && CTC_IS_LINKAGG_PORT(psys->gport);
    is_phy_port   = !is_logic_port && !CTC_IS_LINKAGG_PORT(psys->gport);

    if (is_logic_port)
    {
        uint32 nhid;
        ret        = sys_greatbelt_l2_get_nhid_by_logic_port(lchip, psys->gport, &nhid);
        is_binding = !FAIL(ret);
    }

    if (is_local_dynamic && is_trunk && !psys->with_nh)
    {
        ad_index = pl2_gb_master[lchip]->base_trunk +
                   CTC_MAP_GPORT_TO_LPORT(psys->gport); /* linkagg */
    }
    else if (is_local_dynamic && is_phy_port && !psys->with_nh)
    {
        ad_index = pl2_gb_master[lchip]->base_gport +
                   CTC_MAP_GPORT_TO_LPORT(psys->gport) ; /* gport */
    }
    else if (!psys->flag.flag_ad.is_static && !psys->flag.flag_ad.bind_port && is_logic_port && is_binding)
    {
        if (pl2_gb_master[lchip]->vp_alloc_ad_en)
        {
            sys_l2_vport_2_nhid_t *p_vp_node = NULL;
            p_vp_node = ctc_vector_get(pl2_gb_master[lchip]->vport2nh_vec, psys->gport);
            ad_index = p_vp_node->ad_index;
        }
        else
        {
            ad_index = pl2_gb_master[lchip]->base_vport + psys->gport;  /* logic port */
        }

    }
    else if (psys->flag.flag_node.remote_dynamic && !is_local_chip && pl2_gb_master[lchip]->rchip_ad_rsv[gchip_id])
    {
        uint16  lport = CTC_MAP_GPORT_TO_LPORT(psys->gport);
        uint8   slice_en = (pl2_gb_master[lchip]->rchip_ad_rsv[gchip_id] >> 14) & 0x3;
        uint16  rsv_num = pl2_gb_master[lchip]->rchip_ad_rsv[gchip_id] & 0x3FFF;
        uint32  ad_base = pl2_gb_master[lchip]->base_rchip[gchip_id];

        if (slice_en == 1)
        {
            /*slice 0*/
            if (lport >= rsv_num)
            {
                return CTC_E_INVALID_PARAM;
            }
            ad_index = lport + ad_base;
        }
        else if (slice_en == 3)
        {
            if (((lport >= 256) && (lport >= (256 + rsv_num/2)))
                || ((lport < 256) && (lport >= rsv_num/2)))
            {
                return CTC_E_INVALID_PARAM;
            }
            ad_index = ((lport >= 256) ? (lport - 256 + rsv_num/2) : lport) + ad_base;
        }
        else if (slice_en == 2)
        {
            /*slice 1*/
            if (((lport&0xFF) >= rsv_num) || (lport < 256))
            {
                return CTC_E_INVALID_PARAM;
            }
            ad_index = lport - 256 + ad_base;
        }
        else
        {
            return CTC_E_INVALID_PARAM;
        }
    }
    else
    {
        psys->flag.flag_node.rsv_ad_index = 0;
    }
    if (psys->flag.flag_ad.aging_disable)
    {
        psys->flag.flag_node.rsv_ad_index = 0;
    }

END:
    psys->ad_index = ad_index;
    return CTC_E_NONE;
}


STATIC int32
_sys_greatbelt_l2_map_ctc_to_sys(uint8 lchip, uint8 type, void * l2, sys_l2_info_t * psys,
                                 uint8 with_nh, uint32 nhid)
{
    sys_nh_info_dsnh_t nh_info;
    sys_nh_brguc_nhid_info_t brguc_nhid_info;
    uint8 is_cpu_nh = 0;
    int32 ret = CTC_E_NONE;

    CTC_ERROR_RETURN(_sys_greatbelt_l2_map_flag(lchip, type, l2, &psys->flag));
    sal_memset(&brguc_nhid_info, 0, sizeof(sys_nh_brguc_nhid_info_t));
    sal_memset(&nh_info, 0, sizeof(sys_nh_info_dsnh_t));


    if ((L2_TYPE_UC == type) || (L2_TYPE_TCAM == type))
    {
        ctc_l2_addr_t   * l2_addr = (ctc_l2_addr_t *) l2;
        uint16          gport     = 0;
        uint32 fwd_offset =  0;

        if (!with_nh)
        {
            /* if not set discard, get dsfwdptr of port */
            if (!CTC_FLAG_ISSET(l2_addr->flag, CTC_L2_FLAG_DISCARD))
            {
                ret = sys_greatbelt_brguc_get_nhid(lchip, l2_addr->gport, &brguc_nhid_info);
                if (ret < 0)
                {
                    sys_greatbelt_brguc_nh_create(lchip, l2_addr->gport, CTC_NH_PARAM_BRGUC_SUB_TYPE_BASIC);
                    sys_greatbelt_brguc_get_nhid(lchip, l2_addr->gport, &brguc_nhid_info);
                }
                CTC_ERROR_RETURN(sys_greatbelt_brguc_get_dsfwd_offset(lchip, l2_addr->gport, CTC_NH_PARAM_BRGUC_SUB_TYPE_BASIC, &fwd_offset));
                gport = l2_addr->gport;

                psys->nhid    = brguc_nhid_info.brguc_nhid;

            }
            else
            {
                    fwd_offset = SYS_FDB_DISCARD_FWD_PTR;
                gport        = DISCARD_PORT;
            }
            psys->dsfwd_valid  = 1;
        }
        else /* with nexthop */
        {

            psys->with_nh = 1;
            psys->nhid    = nhid;

            CTC_ERROR_RETURN(sys_greatbelt_nh_is_cpu_nhid(lchip, nhid, &is_cpu_nh));
            if (!psys->flag.flag_ad.logic_port)
            {
                CTC_ERROR_RETURN(sys_greatbelt_nh_get_nhinfo(lchip, nhid, &nh_info));
                psys->flag.flag_node.ecmp_valid = nh_info.ecmp_valid;
                psys->ecmp_num        = nh_info.ecmp_num;
                gport = l2_addr->gport;
                if(nh_info.ecmp_valid || nh_info.is_mcast || nh_info.aps_en)
                {
                    gport = l2_addr->gport;
                }
                else if (psys->flag.flag_node.white_list)
                {
                    gport = SYS_RESERVE_PORT_ID_CPU;

                }
                else if (is_cpu_nh)
                {
                    gport = SYS_RESERVE_PORT_ID_CPU;
                }
                else
                {
                    uint8 aps_brg_en;
                    CTC_ERROR_RETURN(sys_greatbelt_nh_get_port(lchip, nhid, &aps_brg_en, &gport));
                }
            }
            else
            {   /* is logic port.
                 * logic port and nhid both get from ctc,
                 * and we don't check if they are binded. We trust user at this.
                 */
                gport = l2_addr->gport;
            }

            if(psys->flag.flag_ad.copy_to_cpu||nh_info.dsfwd_valid|| (nh_info.merge_dsfwd != 1))
            {
                psys->dsfwd_valid  = 1;
                CTC_ERROR_RETURN(sys_greatbelt_nh_get_dsfwd_offset(lchip, nhid, &fwd_offset));
            }
            /* fid = 0xffff use for sys mac in tcam, so use fdb_hash to store */
        }

        /* map hw entry */
        psys->fid = l2_addr->fid;
        FDB_SET_MAC(psys->mac, l2_addr->mac);
        if (L2_TYPE_TCAM == type)
        {
            if (!l2_addr->mask_valid)
            {
                sal_memset(psys->mac_mask, 0xFF, sizeof(mac_addr_t));
            }
            else
            {
                FDB_SET_MAC(psys->mac_mask, l2_addr->mask);
            }
        }

        psys->gport   = gport;
        psys->fwd_ptr =  fwd_offset;

        /* set gport back */
        l2_addr->gport = gport;

        if(L2_TYPE_TCAM != type)
        {
            CTC_ERROR_RETURN(_sys_greatbelt_l2_get_reserved_index(lchip, psys));
        }

        SYS_FDB_DBG_INFO("psys  gport 0x%x\n", psys->gport);
    }
    else if (L2_TYPE_MC == type)  /* mc*/
    {
        ctc_l2_mcast_addr_t * l2mc_addr = (ctc_l2_mcast_addr_t *) l2;

        /* map hw entry */
        psys->fid = l2mc_addr->fid;
        FDB_SET_MAC(psys->mac, l2mc_addr->mac);
        psys->flag.flag_ad.is_static = 1;
        psys->flag.flag_node.type_l2mc = 1;
        psys->mc_gid         = l2mc_addr->l2mc_grp_id;
    }
    else if (L2_TYPE_DF == type)  /*df*/
    {
        ctc_l2dflt_addr_t * l2df_addr = (ctc_l2dflt_addr_t *) l2;
        /* map hw entry */
        psys->fid               = l2df_addr->fid;
        psys->flag.flag_ad.is_static    = 1;
        psys->flag.flag_node.type_default = 1;
        psys->mc_gid            = l2df_addr->l2mc_grp_id;
        psys->fid_flag          = l2df_addr->flag;
        if (CTC_FLAG_ISSET(l2df_addr->flag, CTC_L2_DFT_VLAN_FLAG_VLAN_MAC_AUTH))
        {
            /* vlan mac-auth need */
            psys->dsfwd_valid        = 1;
        }
        if (CTC_FLAG_ISSET(l2df_addr->flag, CTC_L2_DFT_VLAN_FLAG_USE_LOGIC_PORT))
        {
            /* logic port application, we have to suppport drop. thus use fwdptr.*/
            psys->dsfwd_valid        = 1;
            psys->flag.flag_ad.ucast_discard = CTC_FLAG_ISSET(l2df_addr->flag, CTC_L2_DFT_VLAN_FLAG_UNKOWN_UCAST_DROP);
            psys->flag.flag_ad.mcast_discard = CTC_FLAG_ISSET(l2df_addr->flag, CTC_L2_DFT_VLAN_FLAG_UNKOWN_MCAST_DROP);

        }
        else /* other application, that's normal fdb. we support drop on vlan. thus use nexthop_ptr. */
        {
            if (CTC_FLAG_ISSET(l2df_addr->flag, CTC_L2_DFT_VLAN_FLAG_UNKOWN_UCAST_DROP)
                || CTC_FLAG_ISSET(l2df_addr->flag, CTC_L2_DFT_VLAN_FLAG_UNKOWN_MCAST_DROP))
            {
                return CTC_E_FEATURE_NOT_SUPPORT;
            }
        }

    }


    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l2_add_tcam(uint8 lchip, ctc_l2_addr_t* l2_addr, uint8 with_nh, uint32 nhid)
{
    int32          ret    = 0;
    uint32         offset = 0;
    sys_l2_tcam_t* p_tcam = NULL;
    sys_l2_info_t  sys;

    if (!pl2_gb_master[lchip]->tcam_num)
    {
        return CTC_E_NO_RESOURCE;
    }

    sal_memset(&sys, 0, sizeof(sys_l2_info_t));
    CTC_ERROR_RETURN(_sys_greatbelt_l2_map_ctc_to_sys(lchip, L2_TYPE_TCAM, l2_addr, &sys, with_nh, nhid));

    /* 1.  lookup tcam_hash */
    p_tcam = _sys_greatbelt_l2_tcam_hash_lookup(lchip, sys.mac, sys.mac_mask);
    if (!p_tcam)  /* add*/
    {
        MALLOC_ZERO(MEM_FDB_MODULE, p_tcam, sizeof(sys_l2_tcam_t));
        if (!p_tcam)
        {
            ret = CTC_E_NO_MEMORY;
            goto error_0;
        }
        sal_memset(p_tcam, 0, sizeof(sys_l2_tcam_t));
        CTC_ERROR_GOTO(sys_greatbelt_ftm_alloc_table_offset(lchip, DsMacBridgeKey_t, 0, 1, &offset), ret, error_1);

            sys.key_index  = offset;

        sys.ad_index  = offset;

        /* write sw */
        FDB_SET_MAC(p_tcam->key.mac , sys.mac);
        FDB_SET_MAC(p_tcam->key.mask, sys.mac_mask);
        p_tcam->flag     = sys.flag;
        p_tcam->key_index= offset;
        CTC_ERROR_GOTO(_sys_greatbelt_l2_tcam_hash_add(lchip, p_tcam), ret, error_2);

        /* write hw */

            CTC_ERROR_GOTO(_sys_greatbelt_l2_write_hw(lchip, &sys), ret, error_3);

    }
    else  /*update*/
    {
        /* update sw. system not support forward. thus should not update port list */
        p_tcam->flag     = sys.flag;
        /* write hw */

            CTC_ERROR_GOTO(_sys_greatbelt_l2_write_hw(lchip, &sys), ret, error_0);

    }

    return CTC_E_NONE;

error_3:
    _sys_greatbelt_l2_tcam_hash_remove(lchip, p_tcam);
error_2:
    sys_greatbelt_ftm_free_table_offset(lchip, DsMacBridgeKey_t, 0, 1, offset);
error_1:
    mem_free(p_tcam);
error_0:
    return ret;
}

STATIC int32
_sys_greatbelt_l2_remove_tcam(uint8 lchip, ctc_l2_addr_t* l2_addr)
{
    sys_l2_tcam_t* p_tcam = NULL;
    sys_l2_info_t  sys;

    FDB_SET_MAC(sys.mac, l2_addr->mac);
    if (!l2_addr->mask_valid)
    {
        sal_memset(sys.mac_mask, 0xFF, sizeof(mac_addr_t));
    }
    else
    {
        FDB_SET_MAC(sys.mac_mask, l2_addr->mask);
    }

    p_tcam = _sys_greatbelt_l2_tcam_hash_lookup(lchip, sys.mac, sys.mac_mask);
    if (p_tcam)
    {

            CTC_ERROR_RETURN(DRV_TCAM_TBL_REMOVE(lchip, DsMacBridgeKey_t, p_tcam->key_index));

        sys_greatbelt_ftm_free_table_offset(lchip, DsMacBridgeKey_t, 0, 1, p_tcam->key_index);
        _sys_greatbelt_l2_tcam_hash_remove(lchip, p_tcam);
    }
    else
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    return CTC_E_NONE;

}

/**
   @brief add fdb entry
 */
int32
sys_greatbelt_l2_add_fdb(uint8 lchip, ctc_l2_addr_t* l2_addr, uint8 with_nh, uint32 nhid)
{
    sys_l2_info_t       new_sys;
    sys_l2_node_t       * p_l2_node = NULL;
    int32               ret         = 0;
     /*uint8               chip_id     = 0;*/
    sys_l2_ad_spool_t         * p_old_ad  = NULL;
    sys_l2_flag_node_t     p_old_flag_n;
    sys_l2_fid_node_t   * fid    = NULL;
    uint32              fid_flag = 0;
    mac_lookup_result_t result;
    sys_l2_ad_spool_t         * pa_new = NULL;
    sys_l2_ad_spool_t         * pa_get = NULL;
    sys_l2_node_t       * pn_new = NULL;
    sys_l2_info_t       old_sys;
    uint32              temp_flag = 0;
    uint8               freed = 0;
    uint8               need_has_sw = 0;
    uint8               is_overwrite = 0;

    SYS_L2_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(l2_addr);
    SYS_L2UC_FID_CHECK(l2_addr->fid);

    sal_memset(&new_sys, 0, sizeof(sys_l2_info_t));
    sal_memset(&old_sys, 0, sizeof(sys_l2_info_t));
    SYS_FDB_DBG_FUNC();
    SYS_FDB_DBG_PARAM("mac:%s ,fid :%d ,flag:0x%x, gport:0x%x\n",
                      sys_greatbelt_output_mac(l2_addr->mac), l2_addr->fid, l2_addr->flag, l2_addr->gport);

    if (l2_addr->fid ==  DONTCARE_FID) /* is_tcam is set when system mac. and system mac can update */
    {
        return _sys_greatbelt_l2_add_tcam(lchip, l2_addr, with_nh, nhid);
    }

     /* 1. check flag supported*/
    if (!CTC_FLAG_ISSET(l2_addr->flag, CTC_L2_FLAG_IS_STATIC))
    {
        temp_flag = l2_addr->flag;
        CTC_UNSET_FLAG(temp_flag, CTC_L2_FLAG_AGING_DISABLE);
        CTC_UNSET_FLAG(temp_flag, CTC_L2_FLAG_REMOTE_DYNAMIC);
        CTC_UNSET_FLAG(temp_flag, CTC_L2_FLAG_BIND_PORT);
        CTC_UNSET_FLAG(temp_flag, CTC_L2_FLAG_WHITE_LIST_ENTRY);
        if (temp_flag) /* dynamic entry has other flag */
        {
            return CTC_E_INVALID_PARAM;
        }
    }

     /* 2. check parameter*/
    if (!with_nh && !CTC_FLAG_ISSET(l2_addr->flag, CTC_L2_FLAG_DISCARD))
    {
        CTC_ERROR_RETURN(sys_greatbelt_port_dest_gport_check(lchip, l2_addr->gport));
    }

    L2_LOCK;
     /* 3. check fid created*/
    fid      = _sys_greatbelt_l2_fid_node_lkup(lchip, l2_addr->fid, GET_FID_NODE);
    fid_flag = (fid) ? fid->flag : 0;

    if (!with_nh && (CTC_FLAG_ISSET(fid_flag, CTC_L2_DFT_VLAN_FLAG_USE_LOGIC_PORT)
        || CTC_FLAG_ISSET(l2_addr->flag, CTC_L2_FLAG_WHITE_LIST_ENTRY)))
    {
        ret = CTC_E_INVALID_PARAM;
        goto error_0;
    }

    if (with_nh)
    {
        if (CTC_FLAG_ISSET(fid_flag, CTC_L2_DFT_VLAN_FLAG_USE_LOGIC_PORT))
        {
            SYS_LOGIC_PORT_CHECK_WITH_UNLOCK(l2_addr->gport);
        }
        /* nexthop module doesn't provide check nhid. */
    }

     /* 4. map ctc to sys*/
    CTC_ERROR_GOTO(_sys_greatbelt_l2_map_ctc_to_sys(lchip, L2_TYPE_UC, l2_addr, &new_sys, with_nh, nhid), ret, error_0);

     /*need to save SW for all unreserved ad_index when enable hw learning mode*/
    need_has_sw = pl2_gb_master[lchip]->cfg_has_sw ? 1 : (!new_sys.flag.flag_node.rsv_ad_index);

     /* 5. lookup hw, check result*/
    CTC_ERROR_GOTO(_sys_greatbelt_l2_acc_lookup_mac_fid(lchip, l2_addr->mac, l2_addr->fid, &result), ret, error_0);
    if (result.conflict)
    {

        ret =  CTC_E_FDB_HASH_CONFLICT;
        goto error_0;
    }

    if (need_has_sw)
    {
        p_l2_node = _sys_greatbelt_l2_fdb_hash_lkup(lchip, l2_addr->mac, l2_addr->fid);
        if (p_l2_node != NULL)
        {
            is_overwrite = 1;
        }
    }

     /* 6. hit check if update is ok.*/
    if (result.hit)
    {
        ds_mac_t ds_mac;
        _sys_get_dsmac_by_index(lchip, result.ad_index, &ds_mac);
        _sys_greatbelt_l2_decode_dsmac(lchip, &old_sys, &ds_mac, result.ad_index);

        /* l2 mc shall not ovewrite */
        if (old_sys.flag.flag_node.type_l2mc)
        {
            ret = CTC_E_FDB_MCAST_ENTRY_EXIST;
            goto error_0;
        }

        /* dynamic cannot rewrite static */
        if (!new_sys.flag.flag_ad.is_static && old_sys.flag.flag_ad.is_static)
        {
            ret = CTC_E_ENTRY_EXIST;
            goto error_0;
        }
    }

     /* 7. add/update sw*/
    if (need_has_sw)
    {
        /*update or add, both need malloc new ad. */
        MALLOC_ZERO(MEM_FDB_MODULE, pa_new, sizeof(sys_l2_ad_spool_t));
        if (!pa_new)
        {
            ret = CTC_E_NO_MEMORY;
            goto error_0;
        }
        sal_memset(pa_new, 0, sizeof(sys_l2_ad_spool_t));

        if (!is_overwrite)  /*free hit. malloc */
        {
            MALLOC_ZERO(MEM_FDB_MODULE, pn_new, sizeof(sys_l2_node_t));
            if (!pn_new)
            {
                ret = CTC_E_NO_MEMORY;
                goto error_1;
            }

            p_l2_node = pn_new;

            _sys_greatbelt_l2_map_sys_to_sw(lchip, &new_sys, p_l2_node, pa_new);
            CTC_ERROR_GOTO(_sys_greatbelt_l2_build_dsmac_index
                               (lchip, &new_sys, NULL, pa_new, &pa_get), ret, error_2);
            p_l2_node->adptr = pa_get;
            p_l2_node->flag = new_sys.flag.flag_node;

            if (pa_new != pa_get) /* means get an old. */
            {
                mem_free(pa_new);
            }

            CTC_ERROR_GOTO(_sys_greatbelt_l2uc_add_sw(lchip, p_l2_node), ret, error_3);

            p_l2_node->adptr->index = new_sys.ad_index;
        }
        else  /* result hit. update */
        {
            /* lookup old node. must success.  */
            p_l2_node = _sys_greatbelt_l2_fdb_hash_lkup(lchip, l2_addr->mac, l2_addr->fid);
            if (!p_l2_node)
            {
                ret = CTC_E_INVALID_PTR;
                goto error_1;
            }

            /* flag that only in sw */
            old_sys.flag.flag_node.remote_dynamic = p_l2_node->flag.remote_dynamic;
            sal_memset(&p_old_flag_n, 0, sizeof(sys_l2_flag_node_t));
            p_old_ad = p_l2_node->adptr;
            p_old_flag_n = p_l2_node->flag;
            /*
             * update ucast fdb entry, only need to update
             * ad_index, port list and spool node,
             * because only flag, gport, and ad_index in l2_node may be changed.
             */

            /* map l2 node */
            _sys_greatbelt_l2_map_sys_to_sw(lchip, &new_sys, NULL, pa_new);
            CTC_ERROR_GOTO(_sys_greatbelt_l2_build_dsmac_index
                               (lchip, &new_sys, p_l2_node->adptr, pa_new, &pa_get), ret, error_1);

            if (pa_new != pa_get) /* means get an old. */
            {
                mem_free(pa_new);
            }
            CTC_ERROR_GOTO(_sys_greatbelt_l2uc_update_sw(lchip, p_old_ad, pa_get, p_l2_node), ret, error_3);
            p_l2_node->adptr        = pa_get;
            p_l2_node->flag         = new_sys.flag.flag_node;
            p_l2_node->adptr->index = new_sys.ad_index;
        }
    }
    SYS_L2_DUMP_SYS_INFO(&new_sys);

    /* write hw */
    CTC_ERROR_GOTO(_sys_greatbelt_l2_write_hw(lchip, &new_sys), ret, error_4);

    /* do at last. because this process is irreversible. */
    if (is_overwrite && need_has_sw)
    {
        /* free old index*/
        _sys_greatbelt_l2_free_dsmac_index(lchip, NULL, p_old_ad, &p_old_flag_n, &freed);
        if (freed)
        {
            mem_free(p_old_ad);
        }
    }

     /* update sw index*/
    if (p_l2_node)
    {
        sal_memcpy(&p_l2_node->key_index, &new_sys.key_index, sizeof(new_sys.key_index));
    }

    if (need_has_sw)
    {
        if (!is_overwrite)  /* add*/
        {
            _sys_greatbelt_l2_update_count(lchip, &new_sys.flag.flag_ad, &new_sys.flag.flag_node, 1, L2_COUNT_GLOBAL, &new_sys.key_index);
        }
        else  /* update*/
        {
            if (!old_sys.flag.flag_ad.is_static && new_sys.flag.flag_ad.is_static)  /* dynamic -> static */
            {
                pl2_gb_master[lchip]->dynamic_count -- ;
                if (!old_sys.flag.flag_node.remote_dynamic)
                {
                    pl2_gb_master[lchip]->local_dynamic_count -- ;
                }
            }
            else if((!old_sys.flag.flag_ad.is_static && !new_sys.flag.flag_ad.is_static)&&
                (!old_sys.flag.flag_node.remote_dynamic && new_sys.flag.flag_node.remote_dynamic)) /* local_dynamic -> remote dynamic*/
            {
                pl2_gb_master[lchip]->local_dynamic_count--;
            }
            else if((!old_sys.flag.flag_ad.is_static && !new_sys.flag.flag_ad.is_static)&&
                (old_sys.flag.flag_node.remote_dynamic && !new_sys.flag.flag_node.remote_dynamic))/* remote dynamic -> local dynamic*/
            {
                pl2_gb_master[lchip]->local_dynamic_count++;
            }
        }
    }

    L2_UNLOCK;
    return ret;

 error_4:
    if (p_l2_node)       /* has sw*/
    {
        if (is_overwrite)  /* update roll back*/
        {
            _sys_greatbelt_l2uc_update_sw(lchip, pa_get, p_old_ad, p_l2_node);
        }
        else  /* remove*/
        {
            _sys_greatbelt_l2uc_remove_sw(lchip, p_l2_node);
        }
    }
 error_3:
    if (p_l2_node)
    {
        _sys_greatbelt_l2_free_dsmac_index(lchip, NULL, pa_get, &p_l2_node->flag, NULL);/* error process don't care freed*/
    }
 error_2:
    mem_free(pn_new);
 error_1:
    mem_free(pa_new);
 error_0:
    L2_UNLOCK;
    return ret;
}



int32
sys_greatbelt_l2_remove_fdb(uint8 lchip, ctc_l2_addr_t* l2_addr)
{
    ds_mac_t            dsmac;

    uint8               freed = 0;
    sys_l2_info_t       sys;
    mac_lookup_result_t rslt;
    uint8               has_sw = 0;
    sys_l2_node_t*      p_l2_node = NULL;

    SYS_L2_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(l2_addr);
    SYS_L2UC_FID_CHECK(l2_addr->fid);

    SYS_FDB_DBG_FUNC();
    SYS_FDB_DBG_PARAM("mac: %s,fid: %d  \n",
                      sys_greatbelt_output_mac(l2_addr->mac),
                      l2_addr->fid);

    sal_memset(&sys, 0, sizeof(sys));

    if (l2_addr->fid ==  DONTCARE_FID)
    {
        return _sys_greatbelt_l2_remove_tcam(lchip, l2_addr);
    }

    CTC_ERROR_RETURN(_sys_greatbelt_l2_acc_lookup_mac_fid(lchip, l2_addr->mac, l2_addr->fid, &rslt));

    if (rslt.hit)
    {
        _sys_get_dsmac_by_index(lchip, rslt.ad_index, &dsmac);
        _sys_greatbelt_l2_decode_dsmac(lchip, &sys, &dsmac, rslt.ad_index);
    }
    else
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    /* remove hw entry */
    CTC_ERROR_RETURN(_sys_greatbelt_l2_delete_hw_by_mac_fid(lchip, l2_addr->mac, l2_addr->fid));
    SYS_FDB_DBG_INFO("Remove ucast fdb: key_index:0x%x, ad_index:0x%x\n",
                     rslt.key_index, rslt.ad_index);

    L2_LOCK;

    /* must lookup success. */
    p_l2_node = _sys_greatbelt_l2_fdb_hash_lkup(lchip, l2_addr->mac, l2_addr->fid);
    if(p_l2_node)
    {
        has_sw = 1;
        _sys_greatbelt_l2uc_remove_sw(lchip, p_l2_node);
        _sys_greatbelt_l2_free_dsmac_index(lchip, NULL, p_l2_node->adptr, &p_l2_node->flag, &freed);
        if (freed)
        {
            mem_free(p_l2_node->adptr);
        }
        mem_free(p_l2_node);
    }
    else if (pl2_gb_master[lchip]->cfg_has_sw)
    {
        L2_UNLOCK;
        return CTC_E_ENTRY_NOT_EXIST;
    }
    if ((pl2_gb_master[lchip]->total_count != 0) && has_sw)
    {
        _sys_greatbelt_l2_update_count(lchip, &sys.flag.flag_ad, &sys.flag.flag_node, -1, L2_COUNT_GLOBAL, &rslt.key_index);
    }

    L2_UNLOCK;

    return CTC_E_NONE;
}

int32
sys_greatbelt_l2_fdb_set_entry_hit(uint8 lchip, ctc_l2_addr_t* l2_addr, uint8 hit)
{
    mac_lookup_result_t rslt;

    SYS_FDB_DBG_FUNC();
    SYS_L2_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(l2_addr);
    SYS_L2UC_FID_CHECK(l2_addr->fid);
    sal_memset(&rslt, 0, sizeof(mac_lookup_result_t));

    CTC_ERROR_RETURN(_sys_greatbelt_l2_acc_lookup_mac_fid(lchip, l2_addr->mac, l2_addr->fid, &rslt));
    if (!rslt.hit)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    CTC_ERROR_RETURN(sys_greatbelt_aging_set_aging_status(lchip, SYS_AGING_MAC_HASH, rslt.key_index, hit));

    return CTC_E_NONE;
}

int32
sys_greatbelt_l2_fdb_get_entry_hit(uint8 lchip, ctc_l2_addr_t* l2_addr, uint8* hit)
{
    mac_lookup_result_t rslt;

    SYS_FDB_DBG_FUNC();
    SYS_L2_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(l2_addr);
    SYS_L2UC_FID_CHECK(l2_addr->fid);
    CTC_PTR_VALID_CHECK(hit);
    sal_memset(&rslt, 0, sizeof(mac_lookup_result_t));

    CTC_ERROR_RETURN(_sys_greatbelt_l2_acc_lookup_mac_fid(lchip, l2_addr->mac, l2_addr->fid, &rslt));
    if (!rslt.hit)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }
    CTC_ERROR_RETURN(sys_greatbelt_aging_get_aging_status(lchip, 4, rslt.key_index, hit));

    return CTC_E_NONE;
}

int32
_sys_greatbelt_l2_replace_match_fdb(uint8 lchip, ctc_l2_replace_t* p_replace)
{
    uint32             match_key_index = 0;
    uint8              match_hit = 0;
    mac_lookup_result_t match_result = {0};
    ctc_l2_addr_t*     l2_addr = &p_replace->l2_addr;
    ctc_l2_addr_t*     match_addr = &p_replace->match_addr;
    uint8              with_nh = (p_replace->nh_id ? 1 : 0);
    uint32             nh_id   = p_replace->nh_id;
    sys_l2_fid_node_t   * fid    = NULL;
    uint32              fid_flag = 0;
    uint8               need_has_sw  = 0;
    int32               ret = 0;
    sys_l2_info_t       new_sys;
    sys_l2_info_t       old_sys;
    sys_l2_ad_spool_t   * pa_new = NULL;
    sys_l2_ad_spool_t   * pa_get = NULL;
    sys_l2_node_t       * pn_new = NULL;
    sys_l2_node_t       * p_l2_node = NULL;
    sys_l2_node_t       * p_match_node = NULL;
    ds_mac_t            old_dsmac;

    SYS_FDB_DBG_FUNC();

    sal_memset(&new_sys, 0, sizeof(sys_l2_info_t));
    sal_memset(&old_sys, 0, sizeof(sys_l2_info_t));

    if (l2_addr->fid ==  DONTCARE_FID) /* is_tcam is set when system mac. and system mac can update */
    {
        return CTC_E_NOT_SUPPORT;
    }

     /*  check flag supported*/
    if (!CTC_FLAG_ISSET(l2_addr->flag, CTC_L2_FLAG_IS_STATIC))
    {
        uint32 temp_flag;
        temp_flag = l2_addr->flag;
        CTC_UNSET_FLAG(temp_flag, CTC_L2_FLAG_AGING_DISABLE);
        CTC_UNSET_FLAG(temp_flag, CTC_L2_FLAG_REMOTE_DYNAMIC);
        CTC_UNSET_FLAG(temp_flag, CTC_L2_FLAG_BIND_PORT);
        CTC_UNSET_FLAG(temp_flag, CTC_L2_FLAG_WHITE_LIST_ENTRY);
        if (temp_flag) /* dynamic entry has other flag */
        {
            return CTC_E_INVALID_PARAM;
        }
    }

     /*  check parameter*/
    if (!with_nh && !CTC_FLAG_ISSET(l2_addr->flag, CTC_L2_FLAG_DISCARD))
    {
        CTC_ERROR_RETURN(sys_greatbelt_port_dest_gport_check(lchip, l2_addr->gport));
    }

    L2_LOCK;
    CTC_ERROR_GOTO(_sys_greatbelt_l2_acc_lookup_mac_fid
        (lchip, match_addr->mac, match_addr->fid, &match_result), ret, error_0);
    if (match_result.hit)
    {
        uint8 cf_max = 0;
        uint8 index = 0;
        uint32 key_index = 0;
        sys_l2_calc_index_t calc_index;
        sal_memset(&old_dsmac, 0, sizeof(ds_mac_t));
        sal_memset(&calc_index, 0, sizeof(sys_l2_calc_index_t));
        CTC_ERROR_GOTO(_sys_get_dsmac_by_index(lchip, match_result.ad_index, &old_dsmac), ret, error_0);
        _sys_greatbelt_l2_decode_dsmac(lchip, &old_sys, &old_dsmac, match_result.ad_index);

        match_key_index = match_result.key_index;

         /*check the hash equal*/
        CTC_ERROR_GOTO(_sys_greatbelt_l2_fdb_calc_index(lchip, l2_addr->mac, l2_addr->fid, &calc_index), ret, error_0);
        cf_max = calc_index.index_cnt + pl2_gb_master[lchip]->hash_base;
        for (index = 0; index < cf_max; index++)
        {
            key_index = (index >= calc_index.index_cnt) ? (index - calc_index.index_cnt) : calc_index.key_index[index];
            if (match_key_index == key_index)
            {
                match_hit = 1;
                break;
            }
        }
        if (!match_hit)
        {
            ret = CTC_E_INVALID_CONFIG;
            goto error_0;
        }

        p_match_node = _sys_greatbelt_l2_fdb_hash_lkup(lchip, match_addr->mac, match_addr->fid);
        if (p_match_node)
        {
            sal_memcpy(&old_sys.flag.flag_node, &p_match_node->flag, sizeof(sys_l2_flag_node_t));
        }
    }

     /* check fid created*/
    fid      = _sys_greatbelt_l2_fid_node_lkup(lchip, l2_addr->fid, GET_FID_NODE);
    fid_flag = (fid) ? fid->flag : 0;

    if (!with_nh && (CTC_FLAG_ISSET(fid_flag, CTC_L2_DFT_VLAN_FLAG_USE_LOGIC_PORT)))
    {
        ret = CTC_E_INVALID_PARAM;
        goto error_0;
    }

    if (CTC_FLAG_ISSET(fid_flag, CTC_L2_DFT_VLAN_FLAG_USE_LOGIC_PORT)
        && (l2_addr->gport >= pl2_gb_master[lchip]->cfg_vport_num))
    {
        ret = CTC_E_INVALID_LOGIC_PORT;
        goto error_0;
    }

     /* map ctc to sys*/
    CTC_ERROR_GOTO(_sys_greatbelt_l2_map_ctc_to_sys(lchip, L2_TYPE_UC, l2_addr, &new_sys, with_nh, nh_id), ret, error_0);

     /*need to save SW for all unreserved ad_index when enable hw learning mode*/
    need_has_sw = pl2_gb_master[lchip]->cfg_has_sw ? 1 : (!new_sys.flag.flag_node.rsv_ad_index);

     /* add new sw*/
    if (need_has_sw)
    {
        /*update or add, both need malloc new ad. */
        MALLOC_ZERO(MEM_FDB_MODULE, pa_new, sizeof(sys_l2_ad_spool_t));
        if (!pa_new)
        {
            ret = CTC_E_NO_MEMORY;
            goto error_0;
        }


        MALLOC_ZERO(MEM_FDB_MODULE, pn_new, sizeof(sys_l2_node_t));
        if (!pn_new)
        {
            ret = CTC_E_NO_MEMORY;
            goto error_1;
        }

        p_l2_node = pn_new;

        _sys_greatbelt_l2_map_sys_to_sw(lchip, &new_sys, p_l2_node, pa_new);
        CTC_ERROR_GOTO(_sys_greatbelt_l2_build_dsmac_index
                           (lchip, &new_sys, NULL, pa_new, &pa_get), ret, error_2);
        p_l2_node->adptr = pa_get;
        p_l2_node->flag = new_sys.flag.flag_node;

        if (pa_new != pa_get) /* means get an old. */
        {
            mem_free(pa_new);
        }

        CTC_ERROR_GOTO(_sys_greatbelt_l2uc_add_sw(lchip, p_l2_node), ret, error_3);

        p_l2_node->adptr->index= new_sys.ad_index;

    }

    if (match_hit)
    {
        /* remove old hw entry */
        CTC_ERROR_GOTO(_sys_greatbelt_l2_delete_hw_by_mac_fid
                        (lchip, match_addr->mac, match_addr->fid), ret, error_4);
        SYS_FDB_DBG_INFO("Remove ucast fdb: key_index:0x%x, ad_index:0x%x\n",
                         match_result.key_index, match_result.ad_index);
    }

    CTC_ERROR_GOTO(_sys_greatbelt_l2_write_hw(lchip, &new_sys), ret, error_5);
    SYS_L2_DUMP_SYS_INFO(&new_sys);

    if (p_l2_node)
    {
        p_l2_node->key_index = new_sys.key_index;
    }

     /*remove old sw*/
    if (need_has_sw)
    {
        p_match_node = _sys_greatbelt_l2_fdb_hash_lkup(lchip, match_addr->mac, match_addr->fid);
        if(p_match_node)
        {
            uint8 freed = 0;
            _sys_greatbelt_l2uc_remove_sw(lchip, p_match_node);
            _sys_greatbelt_l2_free_dsmac_index(lchip, NULL, p_match_node->adptr, &p_match_node->flag, &freed);
            if (freed)
            {
                mem_free(p_match_node->adptr);
            }
            mem_free(p_match_node);
            _sys_greatbelt_l2_update_count(lchip, &old_sys.flag.flag_ad,&old_sys.flag.flag_node, -1, L2_COUNT_GLOBAL, &match_key_index);
        }
         /*update count*/
        _sys_greatbelt_l2_update_count(lchip, &new_sys.flag.flag_ad, &new_sys.flag.flag_node, 1, L2_COUNT_GLOBAL, &new_sys.key_index);
    }

    SYS_FDB_DBG_INFO("replace match hit:%d\n", match_hit);
    L2_UNLOCK;
    return CTC_E_NONE;

 error_5:
    if (match_hit)
    {
        uint32 cmd = 0;
        FDB_SET_MAC(old_sys.mac, match_addr->mac);
        old_sys.fid = match_addr->fid;
        cmd = DRV_IOW(DsMac_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, old_sys.ad_index, cmd, &old_dsmac));
        _sys_greatbelt_l2_write_dskey(lchip, &old_sys);
        if (p_match_node)
        {
            p_match_node->key_index = old_sys.key_index;
        }
    }

error_4:
    if (p_l2_node)
    {
        _sys_greatbelt_l2uc_remove_sw(lchip, p_l2_node);
    }
 error_3:
    if (p_l2_node)
    {
        _sys_greatbelt_l2_free_dsmac_index(lchip, NULL, pa_get, &p_l2_node->flag, NULL);/* error process don't care freed*/
    }
 error_2:
    if (pn_new)
    {
        mem_free(pn_new);
    }
 error_1:
    if (pa_new)
    {
        mem_free(pa_new);
    }
 error_0:
    L2_UNLOCK;
    return ret;
}

int32
sys_greatbelt_l2_replace_fdb(uint8 lchip, ctc_l2_replace_t* p_replace)
{
    mac_lookup_result_t result = {0};

    SYS_L2_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_replace);
    SYS_FDB_DBG_FUNC();

    SYS_FDB_DBG_PARAM("mac:%s ,fid :%d, flag:0x%x, gport:0x%x\n",
                      sys_greatbelt_output_mac(p_replace->l2_addr.mac), p_replace->l2_addr.fid,
                      p_replace->l2_addr.flag, p_replace->l2_addr.gport);
    SYS_FDB_DBG_PARAM("match-mac:%s, match-fid:%d\n",
                      sys_greatbelt_output_mac(p_replace->match_addr.mac), p_replace->match_addr.fid);

    if ((p_replace->l2_addr.fid == DONTCARE_FID) || (p_replace->match_addr.fid == DONTCARE_FID))
    {
        return CTC_E_NOT_SUPPORT;
    }

    CTC_ERROR_RETURN(_sys_greatbelt_l2_acc_lookup_mac_fid
        (lchip, p_replace->l2_addr.mac, p_replace->l2_addr.fid, &result));
    if (result.hit)
    {
        CTC_ERROR_RETURN(sys_greatbelt_l2_add_fdb(lchip, &p_replace->l2_addr, p_replace->nh_id ? 1 : 0, p_replace->nh_id));
    }
    else
    {
        CTC_ERROR_RETURN(_sys_greatbelt_l2_replace_match_fdb(lchip, p_replace));
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l2_add_indrect(uint8 lchip, ctc_l2_addr_t* l2_addr, uint32 nhid, uint32 index)
{
    drv_fib_acc_in_t  in;
    drv_fib_acc_out_t out;
    sys_l2_info_t sys;
    ds_mac_t ds_mac;
    uint32 cmd = 0;
    uint32 ad_index =0;

    SYS_L2_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(l2_addr);
    SYS_L2UC_FID_CHECK(l2_addr->fid);

    SYS_FDB_DBG_FUNC();
    SYS_FDB_DBG_PARAM("mac:%s ,fid :%d ,flag:0x%x, gport:0x%x\n",
                      sys_greatbelt_output_mac(l2_addr->mac), l2_addr->fid, l2_addr->flag, l2_addr->gport);

    sal_memset(&sys, 0, sizeof(sys_l2_info_t));
    sal_memset(&ds_mac, 0, sizeof(ds_mac_t));
    sys.nhid= nhid;
    sys.flag.flag_ad.logic_port = 1;
    sys.gport = l2_addr->gport;
    if (CTC_FLAG_ISSET(l2_addr->flag, CTC_L2_FLAG_AGING_DISABLE))
    {
        sys.flag.flag_ad.aging_disable = 1;
    }
    SYS_FDB_DBG_INFO("logic_port:0x%x, nhid:0x%x\n", l2_addr->gport, nhid);

    cmd = DRV_IOW(DsMac_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(_sys_greatbelt_l2_encode_dsmac(lchip, &ds_mac, &sys));
    ad_index = index;

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, ad_index, cmd, &ds_mac));

    sal_memset(&in, 0, sizeof(in));
    sal_memset(&out, 0, sizeof(out));

    FDB_SET_HW_MAC(in.mac.mac, l2_addr->mac);
    in.mac.fid      = l2_addr->fid;
    in.mac.ad_index = ad_index;

    CTC_ERROR_RETURN(drv_fib_acc(lchip, DRV_FIB_ACC_WRITE_MAC_BY_KEY, &in, &out));
    if (out.mac.conflict)
    {
        return CTC_E_FDB_HASH_CONFLICT;
    }

    CTC_ERROR_RETURN(sys_greatbelt_aging_set_aging_status(lchip, SYS_AGING_MAC_HASH, out.mac.key_index, TRUE));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l2_remove_indrect(uint8 lchip, ctc_l2_addr_t* l2_addr)
{
    mac_lookup_result_t rslt;

    SYS_L2_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(l2_addr);
    SYS_L2UC_FID_CHECK(l2_addr->fid);

    SYS_FDB_DBG_FUNC();
    SYS_FDB_DBG_PARAM("mac: %s,fid: %d  \n",
                      sys_greatbelt_output_mac(l2_addr->mac),
                      l2_addr->fid);

    CTC_ERROR_RETURN(_sys_greatbelt_l2_acc_lookup_mac_fid(lchip, l2_addr->mac, l2_addr->fid, &rslt));

    if (!rslt.hit)
    {
       return CTC_E_ENTRY_NOT_EXIST;
    }

    CTC_ERROR_RETURN(_sys_greatbelt_l2_delete_hw_by_mac_fid(lchip, l2_addr->mac, l2_addr->fid));
    SYS_FDB_DBG_INFO("sync  ucast fdb: key_index:0x%x, ad_index:0x%x\n", rslt.key_index, rslt.ad_index);

    return CTC_E_NONE;
}

#define _REMOVE_GET_BY_INDEX_
int32
sys_greatbelt_l2_remove_fdb_by_index(uint8 lchip, uint32 index)
{
    sys_l2_node_t l2_node_tmp;
    ctc_l2_addr_t l2_addr;

    SYS_L2_INIT_CHECK(lchip);
    sal_memset(&l2_node_tmp, 0, sizeof(l2_node_tmp));

    CTC_ERROR_RETURN(sys_greatbelt_l2_get_fdb_by_index(lchip, index,  1, &l2_addr, NULL));
    CTC_ERROR_RETURN(sys_greatbelt_l2_remove_fdb(lchip, &l2_addr));

    return CTC_E_NONE;
}


int32
sys_greatbelt_l2_get_fdb_by_index(uint8 lchip, uint32 index, uint8 use_logic_port, ctc_l2_addr_t* l2_addr, sys_l2_detail_t* l2_detail)
{
    sys_l2_info_t     sys;
    ds_mac_t          dsmac;
    uint32            cmd = 0;
    ds_fwd_t          ds_fwd;
    drv_fib_acc_in_t  in;
    drv_fib_acc_out_t out;

    SYS_L2_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(l2_addr);

    sal_memset(&in, 0 ,sizeof(drv_fib_acc_in_t));
    sal_memset(&out, 0 ,sizeof(drv_fib_acc_out_t));
    sal_memset(&sys, 0, sizeof(sys_l2_info_t));
    in.rw.key_index = index;
    drv_fib_acc(lchip, DRV_FIB_ACC_READ_FIB0_BY_IDX, &in, &out);

    _sys_greatbelt_l2_decode_dskey(lchip, &sys, &out.read.data);

    if (!sys.key_valid)  /*not valid*/
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }
    /* get mac, fid */
    l2_addr->fid = sys.fid;
    FDB_SET_MAC(l2_addr->mac, sys.mac);

    /* decode and get gport, flag */
    _sys_get_dsmac_by_index(lchip, sys.ad_index, &dsmac);
    _sys_greatbelt_l2_decode_dsmac(lchip, &sys, &dsmac, sys.ad_index);

    l2_addr->gport = sys.gport;
    if (sys.flag.flag_ad.logic_port)
    {
        l2_addr->is_logic_port = 1;
        if (!use_logic_port)
        {
            if (sys.dsfwd_valid)
            {
                sal_memset(&ds_fwd, 0, sizeof(ds_fwd_t));
                cmd = DRV_IOR(DsFwd_t, DRV_ENTRY_FLAG);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, sys.fwd_ptr, cmd, &ds_fwd));
                if ((0 != ds_fwd.aps_type) || (CTC_IS_BIT_SET(ds_fwd.dest_map, 21)))
                {
                    l2_addr->gport = 0xFFFF;
                }
                else
                {
                    l2_addr->gport = SYS_GREATBELT_DESTMAP_TO_GPORT(ds_fwd.dest_map);
                }
            }
            else
            {
                l2_addr->gport = sys.gport;
            }
        }
    }
    _sys_greatbelt_l2_unmap_flag(lchip, &l2_addr->flag, &sys.flag);

    if (l2_detail)
    {
        l2_detail->key_index = index;
        l2_detail->ad_index = sys.ad_index;
        l2_detail->flag = sys.flag;
    }
    return CTC_E_NONE;
}

#define _HW_SYNC_UP_

/**
   @brief get gport by adindex, for hardware learning/aging
 */
STATIC int32
_sys_greatbelt_l2_fdb_get_gport_by_adindex(uint8 lchip, uint16 ad_index, uint16* port, uint8* is_logic)
{
    uint16 lport = 0;

    if ((ad_index < pl2_gb_master[lchip]->base_gport) && ((ad_index + 1) > pl2_gb_master[lchip]->base_trunk))
    {
        /* linkagg port */
        lport     = ad_index - pl2_gb_master[lchip]->base_trunk;
        *port     = CTC_MAP_LPORT_TO_GPORT(0x1f, lport);
        *is_logic = 0;
        SYS_FDB_DBG_INFO("Get port = %x  is linkagg port\n", *port);
    }
    else if ((ad_index < pl2_gb_master[lchip]->base_vport) && ((ad_index + 1) > pl2_gb_master[lchip]->base_gport))
    {
        /* gport port */
        lport     = ad_index - pl2_gb_master[lchip]->base_gport;
        *port     = CTC_MAP_LPORT_TO_GPORT(0x0, lport);
        *is_logic = 0;
        SYS_FDB_DBG_INFO("Get port = %x  is gport\n", *port);
    }
    else if ((ad_index < (pl2_gb_master[lchip]->base_vport + pl2_gb_master[lchip]->cfg_vport_num))
             && ((ad_index + 1) > pl2_gb_master[lchip]->base_vport)
             && (pl2_gb_master[lchip]->cfg_hw_learn))
    {
        /* logic-port */
        *port     = ad_index - pl2_gb_master[lchip]->base_vport;
        *is_logic = 1;
        SYS_FDB_DBG_INFO("Get port = %x  is logic-port\n", *port);
    }
    else
    {
        /* error */
        SYS_FDB_DBG_ERROR("_sys_greatbelt_l2_fdb_get_gport_by_adindex failed!!!   ad_index = %x\n", ad_index);
    }



    return CTC_E_NONE;
}


/**
   @brief update soft tables after hardware learn aging
 */
int32
sys_greatbelt_l2_sync_hw_learn_aging_info(uint8 lchip, void* p_learn_fifo)
{
    int32                    ret         = CTC_E_NONE;
    uint8                    index       = 0;
    sys_l2_node_t            * p_l2_node = NULL;
    ds_mac_t                 dsmac;
    sys_l2_info_t            sys;
    ctc_learn_aging_info_t   info;
    uint8                    is_logic = 0;
    uint8               freed = 0;
    ctc_hw_learn_aging_fifo_t* p_fifo = (ctc_hw_learn_aging_fifo_t *) p_learn_fifo;

    SYS_FDB_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_fifo);
    SYS_FDB_DBG_INFO("Hw total_num  %d\n", p_fifo->entry_num);
    sal_memset(&sys, 0, sizeof(sys_l2_info_t));
    for (index = 0; index < p_fifo->entry_num; index++)
    {
        sal_memcpy(&info, &(p_fifo->learn_info[index]), sizeof(ctc_learn_aging_info_t));
        SYS_FDB_DBG_INFO("   index  %d\n", index);
        SYS_FDB_DBG_INFO("   mac [%s] \n"          \
                         "   fid [%d] \n"          \
                         "   damac_index [0x%x]\n" \
                         "   is_mac_hash [%d]\n"   \
                         "   valid     [%d]\n"     \
                         "   is_aging  [%d]\n",
                         sys_greatbelt_output_mac(info.mac),
                         info.vsi_id,
                         info.damac_index,
                         info.is_mac_hash,
                         info.valid,
                         info.is_aging);

        if (p_fifo->learn_info[index].is_aging == 1) /* aging */
        {
            _sys_get_dsmac_by_index(lchip, info.damac_index, &dsmac);
            _sys_greatbelt_l2_decode_dsmac(lchip, &sys, &dsmac, info.damac_index);

            L2_LOCK;
            if (pl2_gb_master[lchip]->cfg_has_sw)
            {
                p_l2_node = _sys_greatbelt_l2_fdb_hash_lkup(lchip, info.mac, info.vsi_id);
                if (!p_l2_node)
                {
                    SYS_FDB_DBG_INFO(" lookup sw failed!!\n");
                    L2_UNLOCK;
                    continue;
                }

                FDB_ERR_RET_UL(_sys_greatbelt_l2uc_remove_sw(lchip, p_l2_node));
                FDB_ERR_RET_UL(_sys_greatbelt_l2_free_dsmac_index(lchip, &sys, NULL, NULL, &freed));

                if(freed)
                {
                    mem_free(p_l2_node->adptr);
                }
                mem_free(p_l2_node);
            }
            _sys_greatbelt_l2_update_count(lchip, &sys.flag.flag_ad, &sys.flag.flag_node, -1, L2_COUNT_GLOBAL, &info.key_index);
            L2_UNLOCK;
        }
        else  /* learning */
        {
            if (((info.vsi_id) != 0) && ((info.vsi_id) != 0xFFFF)
                && (!pl2_gb_master[lchip] || ((info.vsi_id) >= pl2_gb_master[lchip]->cfg_max_fid)))
            {
                SYS_FDB_DBG_INFO("learning fid check failed ! ---continue---next one!\n");
                continue;
            }

            L2_LOCK;
            if (pl2_gb_master[lchip]->cfg_has_sw)
            {
                p_l2_node = _sys_greatbelt_l2_fdb_hash_lkup(lchip, info.mac, info.vsi_id);
                if (p_l2_node) /* update  */
                {
                    /* update learning port */
                    ret = _sys_greatbelt_l2_port_list_remove(lchip, p_l2_node);
                    if (ret < 0)
                    {
                        L2_UNLOCK;
                        continue;
                    }

                    SYS_FDB_DBG_INFO("HW learning found old one: \n");
                    FDB_ERR_RET_UL(_sys_greatbelt_l2_fdb_get_gport_by_adindex(lchip, p_fifo->learn_info[index].damac_index, &(p_l2_node->adptr->gport), &is_logic));
                    p_l2_node->key.fid = p_fifo->learn_info[index].vsi_id;
                    FDB_SET_MAC(p_l2_node->key.mac, p_fifo->learn_info[index].mac);
                    p_l2_node->key_index = p_fifo->learn_info[index].key_index;
                    p_l2_node->adptr->index = p_fifo->learn_info[index].damac_index;
                    p_l2_node->adptr->index = p_l2_node->adptr->index;
                    if (is_logic)
                    {
                        p_l2_node->adptr->flag.logic_port = 1;
                    }

                    FDB_ERR_RET_UL(_sys_greatbelt_l2_port_list_add(lchip, p_l2_node));
                }
                else
                {
                    p_l2_node = (sys_l2_node_t *) mem_malloc(MEM_FDB_MODULE, sizeof(sys_l2_node_t));
                    if (NULL == p_l2_node)
                    {
                        L2_UNLOCK;
                        return CTC_E_NO_MEMORY;
                    }
                    sal_memset(p_l2_node, 0, sizeof(sys_l2_node_t));

                    FDB_ERR_RET_UL(_sys_greatbelt_l2_fdb_get_gport_by_adindex(lchip, p_fifo->learn_info[index].damac_index, &(p_l2_node->adptr->gport), &is_logic));
                    p_l2_node->key.fid = p_fifo->learn_info[index].vsi_id;
                    FDB_SET_MAC(p_l2_node->key.mac, p_fifo->learn_info[index].mac);
                    p_l2_node->key_index = p_fifo->learn_info[index].key_index;
                    p_l2_node->adptr->index = p_fifo->learn_info[index].damac_index;
                    p_l2_node->adptr->index = p_l2_node->adptr->index;
                    if (is_logic)
                    {
                        p_l2_node->adptr->flag.logic_port = 1;
                    }
                    FDB_ERR_RET_UL(_sys_greatbelt_l2uc_add_sw(lchip, p_l2_node));
                }
            }
            _sys_greatbelt_l2_update_count(lchip, &sys.flag.flag_ad, &sys.flag.flag_node, 1, L2_COUNT_GLOBAL, &info.key_index);
            L2_UNLOCK;
        }
    }

    return ret;
}



#define _MC_DEFAULT_


STATIC int32
_sys_greatbelt_l2_update_nh(uint8 lchip, uint32 nhid, uint8 with_nh, uint32 member, uint8 is_add, bool remote_chip, uint8 is_assign)
{
    int32                      ret        = 0;
    uint8                      aps_brg_en = 0;
    uint16                     dest_id    = 0;

    uint32                     m_nhid     = member;
    uint16                     m_gport    = member;
    uint8                      xgpon_en   = 0;

    sys_nh_param_mcast_group_t nh_mc;
    sal_memset(&nh_mc, 0, sizeof(nh_mc));

    nh_mc.nhid        = nhid;
    nh_mc.dsfwd_valid = 0;
    nh_mc.opcode      = is_add ? SYS_NH_PARAM_MCAST_ADD_MEMBER :
                        SYS_NH_PARAM_MCAST_DEL_MEMBER;

    L2_LOCK;
    if (with_nh)
    {
        CTC_ERROR_GOTO(sys_greatbelt_nh_get_port(lchip, m_nhid, &aps_brg_en, &dest_id), ret, error_0);

        nh_mc.mem_info.ref_nhid             = m_nhid;

        if (aps_brg_en && is_assign)
        {
            L2_UNLOCK;
            return CTC_E_FEATURE_NOT_SUPPORT;
        }
        if (is_assign)
        {
            /*Assign mode, dest id using user assign*/
            dest_id                    = m_gport;
            nh_mc.mem_info.member_type = SYS_NH_PARAM_MCAST_MEM_LOCAL_WITH_NH_DESTID;
        }
        else
        {
            nh_mc.mem_info.member_type = aps_brg_en ? SYS_NH_PARAM_BRGMC_MEM_LOCAL_WITH_APS_BRIDGE :
                                          SYS_NH_PARAM_MCAST_MEM_LOCAL_WITH_NH;
        }

    }
    else
    {
        dest_id                    = m_gport;
        nh_mc.mem_info.is_linkagg  = 0;
        nh_mc.mem_info.member_type = remote_chip? SYS_NH_PARAM_MCAST_MEM_REMOTE : SYS_NH_PARAM_BRGMC_MEM_LOCAL;
    }

    if (aps_brg_en) /* dest_id is aps group id */
    {
        nh_mc.mem_info.destid = dest_id;
    }
    else if(remote_chip)
    {
        nh_mc.mem_info.destid = CTC_MAP_GPORT_TO_LPORT(dest_id);
    }
    else /* dest_id is gport or linkagg */
    {
        CTC_ERROR_RETURN_WITH_UNLOCK(sys_greatbelt_port_dest_gport_check(lchip, dest_id), pl2_gb_master[lchip]->l2_mutex);

        /* trunkid for linkagg, lport for gport */
        nh_mc.mem_info.destid = CTC_MAP_GPORT_TO_LPORT(dest_id);
    }

    if (!remote_chip && (aps_brg_en || CTC_IS_LINKAGG_PORT(dest_id) || SYS_IS_LOOP_PORT(dest_id)))
    {
        nh_mc.mem_info.is_linkagg = CTC_IS_LINKAGG_PORT(dest_id);

            CTC_ERROR_GOTO(sys_greatbelt_mcast_nh_update(lchip, &nh_mc), ret, error_1);
    }
    else
    {
        sys_greatbelt_global_get_xgpon_en(lchip, &xgpon_en);
        if ((!xgpon_en) && (FALSE == sys_greatbelt_chip_is_local(lchip, CTC_MAP_GPORT_TO_GCHIP(dest_id))))
        {
            ret = CTC_E_NONE;
            goto error_0;
        }
        nh_mc.mem_info.is_linkagg = 0;
        CTC_ERROR_GOTO(sys_greatbelt_mcast_nh_update(lchip, &nh_mc), ret, error_1);
    }

    L2_UNLOCK;
    return CTC_E_NONE;

 error_1:
    ret = is_add ? CTC_E_FDB_L2MCAST_ADD_MEMBER_FAILED : CTC_E_FDB_L2MCAST_MEMBER_INVALID;
 error_0:
    L2_UNLOCK;
    return ret;
}


STATIC int32
_sys_greatbelt_l2_update_l2mc_nh(uint8 lchip, ctc_l2_mcast_addr_t* l2mc_addr, uint8 is_add)
{
    uint32          member = 0;
    mac_lookup_result_t rslt;
    ds_mac_t            dsmac;
    uint32              nhp_id = 0;
    sys_l2_info_t       sys;
    uint8               xgpon_en = 0;
    uint8               aps_brg_en = 0;
    uint16              dest_id    = 0;
    uint8 is_assign = 0;

    SYS_L2_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(l2mc_addr);
    SYS_L2_FID_CHECK(l2mc_addr->fid);

    SYS_FDB_DBG_FUNC();
    SYS_FDB_DBG_PARAM("mac:%s,fid :%d, member:%d \n",
                      sys_greatbelt_output_mac(l2mc_addr->mac),
                      l2mc_addr->fid, ((l2mc_addr->with_nh) ? l2mc_addr->member.nh_id : l2mc_addr->member.mem_port));

    sal_memset(&sys, 0, sizeof(sys_l2_info_t));
    CTC_ERROR_RETURN(_sys_greatbelt_l2_acc_lookup_mac_fid(lchip, l2mc_addr->mac, l2mc_addr->fid, &rslt));

    if (rslt.hit)
    {
        _sys_get_dsmac_by_index(lchip, rslt.ad_index, &dsmac);
        _sys_greatbelt_l2_decode_dsmac(lchip, &sys, &dsmac, rslt.ad_index);
    }
    else
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    is_assign = CTC_FLAG_ISSET(l2mc_addr->flag, CTC_L2_FLAG_ASSIGN_OUTPUT_PORT);
    if (is_assign && !(l2mc_addr->with_nh))
    {
        return CTC_E_INVALID_PARAM;
    }

    L2_LOCK;
    if(sys.flag.flag_node.type_l2mc)
    {
        _sys_greatbelt_l2_get_nhop_by_mc_grp_id(lchip, sys.mc_gid, &nhp_id);
    }
    else
    {
        FDB_ERR_RET_UL(CTC_E_ENTRY_NOT_EXIST);
    }
    L2_UNLOCK;

    sys_greatbelt_global_get_xgpon_en(lchip, &xgpon_en);
    if (xgpon_en)
    {
        if (l2mc_addr->with_nh)
        {
            CTC_ERROR_RETURN(sys_greatbelt_nh_get_port(lchip, l2mc_addr->member.nh_id, &aps_brg_en, &dest_id));
            l2mc_addr->with_nh = 0;
            l2mc_addr->member.mem_port = dest_id;
        }
    }

    member = l2mc_addr->with_nh? l2mc_addr->member.nh_id: l2mc_addr->member.mem_port;
    return _sys_greatbelt_l2_update_nh(lchip, nhp_id, l2mc_addr->with_nh,
                                       member, is_add, l2mc_addr->remote_chip, is_assign);
}


STATIC int32
_sys_greatbelt_l2_update_default_nh(uint8 lchip, ctc_l2dflt_addr_t* l2dflt_addr, uint8 is_add)
{
    sys_l2_fid_node_t * p_fid_node = NULL;
    uint32              member     = 0;
    uint8  is_assign = 0;

    SYS_L2_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(l2dflt_addr);
    SYS_L2_FID_CHECK(l2dflt_addr->fid);

    SYS_FDB_DBG_FUNC();
    SYS_FDB_DBG_PARAM("fid :%d \n", l2dflt_addr->fid);
    SYS_FDB_DBG_PARAM("member port:0x%x\n", l2dflt_addr->member.mem_port);

    is_assign = CTC_FLAG_ISSET(l2dflt_addr->flag, CTC_L2_DFT_VLAN_FLAG_ASSIGN_OUTPUT_PORT);
    if (is_assign && !(l2dflt_addr->with_nh))
    {
        return CTC_E_INVALID_PARAM;
    }

    L2_LOCK;
    p_fid_node = _sys_greatbelt_l2_fid_node_lkup(lchip, l2dflt_addr->fid, GET_FID_NODE);
    if (NULL == p_fid_node)
    {
        FDB_ERR_RET_UL(CTC_E_ENTRY_NOT_EXIST);
    }
    L2_UNLOCK;

    member = l2dflt_addr->with_nh? l2dflt_addr->member.nh_id: l2dflt_addr->member.mem_port;
    return _sys_greatbelt_l2_update_nh(lchip, p_fid_node->nhid, l2dflt_addr->with_nh,
                                       member, is_add, l2dflt_addr->remote_chip, is_assign);
}

STATIC int32
_sys_greatbelt_l2mcast_remove_all_member(uint8 lchip, ctc_l2_mcast_addr_t* l2mc_addr)
{
    return CTC_E_NONE;
}

int32
sys_greatbelt_l2mcast_add_member(uint8 lchip, ctc_l2_mcast_addr_t* l2mc_addr)
{
    return _sys_greatbelt_l2_update_l2mc_nh(lchip, l2mc_addr, 1);
}
int32
sys_greatbelt_l2mcast_remove_member(uint8 lchip, ctc_l2_mcast_addr_t* l2mc_addr)
{
    return _sys_greatbelt_l2_update_l2mc_nh(lchip, l2mc_addr, 0);
}

int32
sys_greatbelt_l2mcast_add_addr(uint8 lchip, ctc_l2_mcast_addr_t* l2mc_addr)
{
    sys_nh_param_mcast_group_t nh_mcast_group;

    int32                      ret     = CTC_E_NONE;
    mac_lookup_result_t        result;
    sys_l2_info_t              new_sys;
    sys_l2_mcast_node_t *p_mc_node = NULL;

    SYS_L2_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(l2mc_addr);

    SYS_L2_FID_CHECK(l2mc_addr->fid);
    SYS_L2_GID_CHECK(l2mc_addr->l2mc_grp_id);

    SYS_FDB_DBG_FUNC();
    SYS_FDB_DBG_PARAM("mac:%s, fid :%d, l2mc_group_id: %d  \n",
                      sys_greatbelt_output_mac(l2mc_addr->mac),
                      l2mc_addr->fid,
                      l2mc_addr->l2mc_grp_id);

    sal_memset(&new_sys, 0, sizeof(sys_l2_info_t));
    sal_memset(&nh_mcast_group, 0, sizeof(sys_nh_param_mcast_group_t));

    CTC_ERROR_RETURN(sys_greatbelt_nh_check_max_glb_met_offset(lchip, l2mc_addr->l2mc_grp_id));

     /* 1. lookup hw, check result*/
    CTC_ERROR_RETURN(_sys_greatbelt_l2_acc_lookup_mac_fid(lchip, l2mc_addr->mac, l2mc_addr->fid, &result));
    if (result.conflict)
    {
        return CTC_E_FDB_HASH_CONFLICT;
    }

    if (result.hit)
    {
        return CTC_E_ENTRY_EXIST;
    }

     /* 2. map ctc to sys*/
    CTC_ERROR_RETURN(_sys_greatbelt_l2_map_ctc_to_sys(lchip, L2_TYPE_MC, l2mc_addr, &new_sys, 0, 0));

    L2_LOCK;


     /* 1. create mcast group*/
    CTC_ERROR_GOTO(sys_greatbelt_mcast_nh_create(lchip, l2mc_addr->l2mc_grp_id, &nh_mcast_group), ret, error_0);

     /* 2. add sw. always add for l2mc, because it's unique.*/

    CTC_ERROR_GOTO(sys_greatbelt_ftm_alloc_table_offset(lchip, DsMac_t, 1, 1, &new_sys.ad_index), ret, error_1);

    CTC_ERROR_GOTO(_sys_greatbelt_l2_mc2nhop_add(lchip, l2mc_addr->l2mc_grp_id, nh_mcast_group.nhid),ret, error_2);

     /*4. write hw*/

        new_sys.fwd_ptr = nh_mcast_group.fwd_offset;
        CTC_ERROR_GOTO(_sys_greatbelt_l2_write_hw(lchip, &new_sys), ret, error_3);

        SYS_FDB_DBG_INFO("Add mcast fdb: key_index:0x%x, ad_index:0x%x, nexthop_id:0x%x, ds_fwd_offset:0x%x\n",
                          new_sys.key_index, (uint32) new_sys.ad_index, nh_mcast_group.nhid, new_sys.fwd_ptr);

     /*update key_index*/
    p_mc_node = ctc_vector_get(pl2_gb_master[lchip]->mcast2nh_vec, l2mc_addr->l2mc_grp_id);
    p_mc_node->key_index = new_sys.key_index;


     /* 5. update index*/
    _sys_greatbelt_l2_update_count(lchip, &new_sys.flag.flag_ad, &new_sys.flag.flag_node, 1, L2_COUNT_GLOBAL, &new_sys.key_index);

    L2_UNLOCK;
    return ret;

 error_3:
    _sys_greatbelt_l2_mc2nhop_remove(lchip, l2mc_addr->l2mc_grp_id);
 error_2:
    sys_greatbelt_ftm_free_table_offset(lchip, DsMac_t, 1, 1, new_sys.ad_index);
 error_1:
    sys_greatbelt_mcast_nh_delete(lchip, nh_mcast_group.nhid);
 error_0:
    L2_UNLOCK;
    return ret;
}


int32
sys_greatbelt_l2mcast_remove_addr(uint8 lchip, ctc_l2_mcast_addr_t* l2mc_addr)
{
    sys_l2_info_t       sys;
    ds_mac_t            dsmac;
    mac_lookup_result_t rslt;
    uint32 nhp_id = 0;

    SYS_L2_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(l2mc_addr);
    SYS_L2_FID_CHECK(l2mc_addr->fid);

    SYS_FDB_DBG_FUNC();
    SYS_FDB_DBG_PARAM("mac:%s,fid :%d  \n",
                      sys_greatbelt_output_mac(l2mc_addr->mac),
                      l2mc_addr->fid);



    sal_memset(&sys, 0, sizeof(sys_l2_info_t));

    if (CTC_FLAG_ISSET(l2mc_addr->flag, CTC_L2_FLAG_KEEP_EMPTY_ENTRY))
    {
        return _sys_greatbelt_l2mcast_remove_all_member(lchip, l2mc_addr);
    }

    CTC_ERROR_RETURN(_sys_greatbelt_l2_acc_lookup_mac_fid(lchip, l2mc_addr->mac, l2mc_addr->fid, &rslt));

    if (rslt.hit)
    {
        _sys_get_dsmac_by_index(lchip, rslt.ad_index, &dsmac);
        _sys_greatbelt_l2_decode_dsmac(lchip, &sys, &dsmac, rslt.ad_index);
    }
    else
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    /* remove hw entry */
    CTC_ERROR_RETURN(_sys_greatbelt_l2_delete_hw_by_mac_fid(lchip, l2mc_addr->mac, l2mc_addr->fid));
    SYS_FDB_DBG_INFO("Remove mcast fdb: key_index:0x%x, ad_index:0x%x\n",
                    rslt.key_index, rslt.ad_index);
    L2_LOCK;

    if(sys.flag.flag_node.type_l2mc)
    {
        _sys_greatbelt_l2_get_nhop_by_mc_grp_id(lchip, sys.mc_gid, &nhp_id);
    }
    else
    {
        L2_UNLOCK;
        return CTC_E_ENTRY_NOT_EXIST;
    }
    /* must lookup success. */
    if(0 != nhp_id)
    {
        /* remove sw entry */
        sys_greatbelt_mcast_nh_delete(lchip, nhp_id);
        _sys_greatbelt_l2_mc2nhop_remove(lchip, sys.mc_gid);
        sys_greatbelt_ftm_free_table_offset(lchip, DsMac_t, 1, 1, rslt.ad_index);
    }
    else
    {
        L2_UNLOCK;
        return CTC_E_ENTRY_NOT_EXIST;
    }

    _sys_greatbelt_l2_update_count(lchip, &sys.flag.flag_ad, &sys.flag.flag_node, -1, L2_COUNT_GLOBAL, &rslt.key_index);

    L2_UNLOCK;

    return CTC_E_NONE;
}

int32
sys_greatbelt_l2_add_port_to_default_entry(uint8 lchip, ctc_l2dflt_addr_t* l2dflt_addr)
{
    return _sys_greatbelt_l2_update_default_nh(lchip, l2dflt_addr, 1);
}

int32
sys_greatbelt_l2_remove_port_from_default_entry(uint8 lchip, ctc_l2dflt_addr_t* l2dflt_addr)
{
    return _sys_greatbelt_l2_update_default_nh(lchip, l2dflt_addr, 0);
}

int32
sys_greatbelt_l2_add_default_entry(uint8 lchip, ctc_l2dflt_addr_t* l2dflt_addr)
{
    sys_l2_info_t              sys;
    int32                      ret     = CTC_E_NONE;
    uint8                      chip_id = 0;
    sys_nh_param_mcast_group_t nh_mcast_group;
    sys_l2_fid_node_t          * p_fid_node = NULL;

    SYS_L2_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(l2dflt_addr);
    SYS_L2_FID_CHECK(l2dflt_addr->fid);


    sal_memset(&sys, 0, sizeof(sys_l2_info_t));
    sal_memset(&nh_mcast_group, 0, sizeof(sys_nh_param_mcast_group_t));
    SYS_FDB_DBG_FUNC();
    SYS_FDB_DBG_PARAM("fid :%d ,l2mc_grp_id:%d \n", l2dflt_addr->fid, l2dflt_addr->l2mc_grp_id);

    CTC_ERROR_RETURN(_sys_greatbelt_l2_map_ctc_to_sys(lchip, L2_TYPE_DF, l2dflt_addr, &sys, 0, 0));

    L2_LOCK;
    sys.ad_index = DEFAULT_ENTRY_INDEX(l2dflt_addr->fid);
    SYS_FDB_DBG_INFO("build hash mcast ad_index:0x%x\n", sys.ad_index);


    p_fid_node = _sys_greatbelt_l2_fid_node_lkup(lchip, l2dflt_addr->fid, GET_FID_NODE);
    if ((NULL == p_fid_node) || (!p_fid_node->create)) /* node not really created */
    {
        /* 1,create mcast group */
        nh_mcast_group.dsfwd_valid = sys.dsfwd_valid;
        CTC_ERROR_GOTO(sys_greatbelt_mcast_nh_create(lchip, l2dflt_addr->l2mc_grp_id, &nh_mcast_group), ret, error_0);
        if (sys.dsfwd_valid)
        {
             /*sys.fwd_ptr = nh_mcast_group.fwd_offset[0]; // assume fwd_offset is same on both chip */
            sys.fwd_ptr = nh_mcast_group.fwd_offset;
        }

        if (NULL == p_fid_node)
        {
            MALLOC_ZERO(MEM_FDB_MODULE, p_fid_node, sizeof(sys_l2_fid_node_t));
            if (!p_fid_node)
            {
                ret = CTC_E_NO_MEMORY;
                goto error_1;
            }

            p_fid_node->fid_list = ctc_list_new();
            if (NULL == p_fid_node->fid_list)
            {
                ret = CTC_E_NO_MEMORY;
                goto error_2;
            }
        }

        /* map fid node.*/
        p_fid_node->fid    = l2dflt_addr->fid;
        p_fid_node->flag   = l2dflt_addr->flag;
        p_fid_node->nhid   = nh_mcast_group.nhid;
        p_fid_node->mc_gid = l2dflt_addr->l2mc_grp_id;
        p_fid_node->create = 1;
        ret                = ctc_vector_add(pl2_gb_master[lchip]->fid_vec, l2dflt_addr->fid, p_fid_node);
        if (FAIL(ret))
        {
            goto error_3;
        }
    }
    else
    {
        ret = CTC_E_NONE;  /* already created. */
        goto error_0;
    }

    /* write hw */
        ret = _sys_greatbelt_l2_write_hw(lchip, &sys);
        if (FAIL(ret))
        {
            goto error_4;
        }

        SYS_FDB_DBG_INFO("Add default entry: chip_id:%d, ad_index:0x%x, group_id: 0x%x, " \
                         "nexthop_id:0x%x\n", chip_id, sys.ad_index,
                         sys.mc_gid, sys.nhid);

    pl2_gb_master[lchip]->def_count++;
    L2_UNLOCK;
    return CTC_E_NONE;

 error_4:
    ctc_vector_del(pl2_gb_master[lchip]->fid_vec, l2dflt_addr->fid);
 error_3:
    ctc_list_delete(p_fid_node->fid_list);
 error_2:
    mem_free(p_fid_node);
 error_1:
    sys_greatbelt_mcast_nh_delete(lchip, sys.nhid);
 error_0:
    L2_UNLOCK;
    return ret;
}

int32
sys_greatbelt_l2_default_entry_set_mac_auth(uint8 lchip, uint16 fid, bool enable)
{
    uint16 ad_index = 0;
    uint32 cmd = 0;
    uint32 value = (enable ? 1 : 0 );

    SYS_L2_INIT_CHECK(lchip);
    ad_index = DEFAULT_ENTRY_INDEX(fid);
    cmd = DRV_IOW(DsMac_t, DsMac_MacSaExceptionEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, ad_index, cmd, &value));

    return CTC_E_NONE;
}

int32
sys_greatbelt_l2_default_entry_get_mac_auth(uint8 lchip, uint16 fid, bool* enable)
{
    uint16 ad_index = 0;
    uint32 cmd = 0;
    uint32  value = 0;

    SYS_L2_INIT_CHECK(lchip);
    ad_index = DEFAULT_ENTRY_INDEX(fid);
    cmd = DRV_IOR(DsMac_t, DsMac_MacSaExceptionEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, ad_index, cmd, &value));
    *enable = value ? TRUE : FALSE;

    return CTC_E_NONE;
}

int32
sys_greatbelt_l2_remove_default_entry(uint8 lchip, ctc_l2dflt_addr_t* l2dflt_addr)
{
    uint8             chip_id      = 0;
    sys_l2_fid_node_t * p_fid_node = NULL;
    sys_l2_info_t     sys;
    uint8             is_remove    = 0;
    int32             ret = 0;

    SYS_L2_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(l2dflt_addr);
    SYS_L2_FID_CHECK(l2dflt_addr->fid);

    sal_memset(&sys, 0, sizeof(sys_l2_info_t));
    SYS_FDB_DBG_FUNC();
    SYS_FDB_DBG_PARAM("fid :%d  \n", l2dflt_addr->fid);

    L2_LOCK;

    p_fid_node = _sys_greatbelt_l2_fid_node_lkup(lchip, l2dflt_addr->fid, GET_FID_NODE);
    if (NULL == p_fid_node)
    {
        ret = CTC_E_ENTRY_NOT_EXIST;
        goto error_0;
    }

    sys.ad_index          = DEFAULT_ENTRY_INDEX(l2dflt_addr->fid);
    sys.flag.flag_node.type_default = 1;
    sys.revert_default    = 1;

    /* remove hw */
        CTC_ERROR_GOTO(_sys_greatbelt_l2_write_hw(lchip, &sys), ret, error_0);
        SYS_FDB_DBG_INFO("Remove default entry: chip_id:%d, ad_index:0x%x nexthop_id 0x%x\n" \
                         , chip_id, DEFAULT_ENTRY_INDEX(p_fid_node->fid), p_fid_node->nhid);

    sys_greatbelt_mcast_nh_delete(lchip, p_fid_node->nhid);
    is_remove = p_fid_node->create;
    p_fid_node->create = 0; /* vlan default removed */

    /* remove sw */
    /* we trust users on this. This routine must be called before flush by fid. */
    if (0 == CTC_LISTCOUNT(p_fid_node->fid_list))
    {
        ctc_list_free(p_fid_node->fid_list);
        ctc_vector_del(pl2_gb_master[lchip]->fid_vec, p_fid_node->fid);
        mem_free(p_fid_node);
    }


    if (is_remove)
    {
        pl2_gb_master[lchip]->def_count--;
    }

 error_0:
    L2_UNLOCK;
    return ret;
}

/**
   @brief set vlan default entry's operation for unknown multicast or unicast.
 */
int32
sys_greatbelt_l2_set_default_entry_features(uint8 lchip, ctc_l2dflt_addr_t* l2dflt_addr)
{
    sys_l2_fid_node_t* fid   = NULL;
    int32            ret     = 0;
 /*    uint8            chip_id = 0;*/
    sys_l2_info_t    sys;
    uint32 dsfwd_offset = 0;

    SYS_L2_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(l2dflt_addr);
    SYS_L2_FID_CHECK(l2dflt_addr->fid);

    SYS_FDB_DBG_FUNC();
    SYS_FDB_DBG_PARAM("fid :%d\n", l2dflt_addr->fid);
    sal_memset(&sys, 0, sizeof(sys_l2_info_t));

    L2_LOCK;

    fid = _sys_greatbelt_l2_fid_node_lkup(lchip, l2dflt_addr->fid, GET_FID_NODE);
    if (NULL == fid)
    {
        ret = CTC_E_FDB_DEFAULT_ENTRY_NOT_EXIST;
        goto error_0;
    }

    fid->flag = l2dflt_addr->flag;
    l2dflt_addr->l2mc_grp_id = fid->mc_gid;
    L2_UNLOCK;
    CTC_ERROR_RETURN(_sys_greatbelt_l2_map_ctc_to_sys(lchip, L2_TYPE_DF, l2dflt_addr, &sys, 0, 0));
    sys.ad_index          = DEFAULT_ENTRY_INDEX(l2dflt_addr->fid);

    if(sys.dsfwd_valid)
    {
        /* 1,create mcast group */
        CTC_ERROR_RETURN(sys_greatbelt_nh_get_dsfwd_offset(lchip, fid->nhid, &dsfwd_offset));
        sys.fwd_ptr =  dsfwd_offset;
    }
    else
    {
        sys_greatbelt_nh_free_dsfwd_offset(lchip, fid->nhid);
    }

        CTC_ERROR_RETURN(_sys_greatbelt_l2_write_hw(lchip, &sys));

    return CTC_E_NONE;

 error_0:
    L2_UNLOCK;
    return ret;
}

/**
   @brief get vlan default entry's operation for unknown multicast or unicast.
 */
int32
sys_greatbelt_l2_get_default_entry_features(uint8 lchip, ctc_l2dflt_addr_t* l2dflt_addr)
{
    sys_l2_fid_node_t* p_fid_node = NULL;
    int32            ret          = 0;
    ds_mac_t         macda;

    SYS_L2_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(l2dflt_addr);
    SYS_L2_FID_CHECK(l2dflt_addr->fid);

    SYS_FDB_DBG_FUNC();
    SYS_FDB_DBG_PARAM("fid :%d\n", l2dflt_addr->fid);
    sal_memset(&macda, 0, sizeof(ds_mac_t));

    L2_LOCK;

    p_fid_node = _sys_greatbelt_l2_fid_node_lkup(lchip, l2dflt_addr->fid, GET_FID_NODE);
    if (NULL == p_fid_node)
    {
        ret = CTC_E_FDB_DEFAULT_ENTRY_NOT_EXIST;
        goto error_0;
    }

    l2dflt_addr->flag        = p_fid_node->flag;
    l2dflt_addr->fid         = p_fid_node->fid;
    l2dflt_addr->l2mc_grp_id = p_fid_node->mc_gid;

 error_0:
    L2_UNLOCK;
    return ret;
}


int32
sys_greatbelt_l2_set_unknown_mcast_tocpu(uint8 lchip, uint16 fid, uint32 value)
{
    sys_l2_fid_node_t* fid_node   = NULL;

    SYS_L2_INIT_CHECK(lchip);
    SYS_L2_FID_CHECK(fid);

    SYS_FDB_DBG_FUNC();
    SYS_FDB_DBG_PARAM("fid :%d\n", fid);

    L2_LOCK;

    fid_node = _sys_greatbelt_l2_fid_node_lkup(lchip, fid, GET_FID_NODE);
    if (NULL == fid_node)
    {
        L2_UNLOCK;
        return CTC_E_FDB_DEFAULT_ENTRY_NOT_EXIST;
    }

    fid_node->unknown_mcast_tocpu = value ? 1 : 0;
    L2_UNLOCK;

    return CTC_E_NONE;
}

int32
_sys_greatbelt_l2_get_unknown_mcast_tocpu(uint8 lchip, uint16 fid, uint32* value)
{
    sys_l2_fid_node_t* fid_node = NULL;

    SYS_L2_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(value);

    SYS_FDB_DBG_FUNC();

    fid_node = _sys_greatbelt_l2_fid_node_lkup(lchip, fid, GET_FID_NODE);
    *value = (NULL == fid_node) ? 0 : (fid_node->unknown_mcast_tocpu ? 1 : 0);

    return CTC_E_NONE;
}


int32
sys_greatbelt_l2_get_unknown_mcast_tocpu(uint8 lchip, uint16 fid, uint32* value)
{
    SYS_L2_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(value);

    SYS_FDB_DBG_FUNC();
    SYS_FDB_DBG_PARAM("fid :%d\n", fid);

    L2_LOCK;
    CTC_ERROR_RETURN_WITH_UNLOCK(_sys_greatbelt_l2_get_unknown_mcast_tocpu(lchip, fid, value), pl2_gb_master[lchip]->l2_mutex);
    L2_UNLOCK;

    return CTC_E_NONE;
}




#define _FDB_SHOW_


int32
sys_greatbelt_l2_show_status(uint8 lchip)
{
    SYS_L2_INIT_CHECK(lchip);
    if (!pl2_gb_master[lchip]->cfg_has_sw)
    {
        return CTC_E_NONE;
    }

    SYS_FDB_DBG_DUMP("-------------------------L2 FDB Status---------------------\n");
    SYS_FDB_DBG_DUMP("%-30s:%u \n","Hash table size(Entry Num)", pl2_gb_master[lchip]->pure_hash_num);
    SYS_FDB_DBG_DUMP("%-30s:%u \n","Total count(uc + mc)", pl2_gb_master[lchip]->total_count);
    SYS_FDB_DBG_DUMP("%-30s:%u \n","Default entry count(def)", pl2_gb_master[lchip]->def_count);
    SYS_FDB_DBG_DUMP("%-30s:%u \n","Local dynamic count(uc)", pl2_gb_master[lchip]->local_dynamic_count);
    SYS_FDB_DBG_DUMP("%-30s:%u \n","Dynamic count(uc)", pl2_gb_master[lchip]->dynamic_count);
    SYS_FDB_DBG_DUMP("%-30s:%u \n","Static count(uc + mc)", pl2_gb_master[lchip]->total_count - pl2_gb_master[lchip]->dynamic_count);
    SYS_FDB_DBG_DUMP("%-30s:%u \n","L2MC count(mc)", pl2_gb_master[lchip]->l2mc_count);
    SYS_FDB_DBG_DUMP("%-30s:%u \n","Max FID value",pl2_gb_master[lchip]->cfg_max_fid);
    SYS_FDB_DBG_DUMP("%-30s:%u \n","Have SW table",pl2_gb_master[lchip]->cfg_has_sw);
    SYS_FDB_DBG_DUMP("%-30s:%u \n","HW learning",pl2_gb_master[lchip]->cfg_hw_learn);
    SYS_FDB_DBG_DUMP("%-30s:%u \n","Logic port number",pl2_gb_master[lchip]->cfg_vport_num);


    return CTC_E_NONE;
}

int32
sys_greatbelt_l2_dump_default_entry(uint8 lchip, ctc_l2dflt_addr_t* l2dflt_addr)
{
    sys_l2_fid_node_t* p_fid_node = NULL;
    SYS_L2_INIT_CHECK(lchip);

    p_fid_node = _sys_greatbelt_l2_fid_node_lkup(lchip, l2dflt_addr->fid, GET_FID_NODE);
    if (NULL == p_fid_node)
    {
        CTC_ERROR_RETURN(CTC_E_ENTRY_NOT_EXIST);
    }

    sys_greatbelt_nh_dump(lchip, p_fid_node->nhid, FALSE);
    return CTC_E_NONE;
}

int32
sys_greatbelt_l2_dump_l2mc_entry(uint8 lchip, ctc_l2_mcast_addr_t* l2mc_addr)
{
    mac_lookup_result_t rslt;
    sys_l2_info_t       sys;
    ds_mac_t            dsmac;

    uint32              nhp_id = 0;

    SYS_L2_INIT_CHECK(lchip);
    sal_memset(&rslt, 0, sizeof(mac_lookup_result_t));
    sal_memset(&dsmac, 0, sizeof(ds_mac_t));
    sal_memset(&sys, 0, sizeof(sys_l2_info_t));
    CTC_ERROR_RETURN(_sys_greatbelt_l2_acc_lookup_mac_fid(lchip, l2mc_addr->mac, l2mc_addr->fid, &rslt));

    if (rslt.hit)
    {
        _sys_get_dsmac_by_index(lchip, rslt.ad_index, &dsmac);
        _sys_greatbelt_l2_decode_dsmac(lchip, &sys, &dsmac, rslt.ad_index);
    }
    else
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }
    L2_LOCK;

    if(sys.flag.flag_node.type_l2mc)
    {
        _sys_greatbelt_l2_get_nhop_by_mc_grp_id(lchip, sys.mc_gid, &nhp_id);
    }
    else
    {
        L2_UNLOCK;
        return CTC_E_ENTRY_NOT_EXIST;
    }
    L2_UNLOCK;

    sys_greatbelt_nh_dump(lchip, nhp_id, FALSE);
    return CTC_E_NONE;

}

STATIC int32
_sys_greatbelt_l2_unbind_nhid_by_logic_port(uint8 lchip, uint16 logic_port)
{
    sys_l2_vport_2_nhid_t *p_node;

    p_node = ctc_vector_get(pl2_gb_master[lchip]->vport2nh_vec, logic_port);
    if(p_node)
    {
        if (pl2_gb_master[lchip]->vp_alloc_ad_en)
        {
            sys_greatbelt_ftm_free_table_offset(lchip, DsMac_t, 1, 1, p_node->ad_index);
        }

        ctc_vector_del(pl2_gb_master[lchip]->vport2nh_vec, logic_port);
        mem_free(p_node);
    }
    else
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }
    return CTC_E_NONE;

}

STATIC int32
_sys_greatbelt_l2_bind_nhid_by_logic_port(uint8 lchip, uint16 logic_port, uint32 nhp_id)
{
    sys_l2_info_t         sys;
    sys_l2_vport_2_nhid_t *p_node;
    uint32       fwd_offset;
    ds_mac_t              ds_mac;
    int32                 cmd     = 0;
    int32                 ret = 0;
    uint8                is_new_node = 0;
    uint32               old_nh_id = 0;

    p_node = ctc_vector_get(pl2_gb_master[lchip]->vport2nh_vec, logic_port);
    if (NULL == p_node)
    {
        MALLOC_ZERO(MEM_FDB_MODULE, p_node, sizeof(sys_l2_vport_2_nhid_t));
        if (NULL == p_node)
        {
            return CTC_E_NO_MEMORY;
        }

        if (pl2_gb_master[lchip]->vp_alloc_ad_en)
        {
            ret = sys_greatbelt_ftm_alloc_table_offset(lchip, DsMac_t, 1, 1, &p_node->ad_index);
            if (ret < 0)
            {
                mem_free(p_node);
                return ret;
            }

            SYS_FDB_DBG_INFO("alloc ad index:0x%x\n", p_node->ad_index);
        }
        else
        {
            p_node->ad_index = pl2_gb_master[lchip]->base_vport + logic_port;
        }
        is_new_node = 1;
        p_node->nhid = nhp_id;
        ctc_vector_add(pl2_gb_master[lchip]->vport2nh_vec, logic_port, p_node);
    }
    else
    {
        old_nh_id = p_node->nhid;
        p_node->nhid = nhp_id;
    }

    /* write binding logic-port dsmac */
    CTC_ERROR_GOTO(sys_greatbelt_nh_get_dsfwd_offset(lchip, nhp_id, &fwd_offset), ret, error_return);

    sal_memset(&sys, 0, sizeof(sys_l2_info_t));
    sal_memset(&ds_mac, 0, sizeof(ds_mac_t));
    sys.gport           = logic_port;
    sys.flag.flag_ad.logic_port = 1;
    sys.dsfwd_valid     = 1;

    SYS_FDB_DBG_INFO("HW learn: logic_port:0x%x, fwd_ptr:0x%x\n", logic_port, fwd_offset);

    cmd = DRV_IOW(DsMac_t, DRV_ENTRY_FLAG);
    sys.fwd_ptr = fwd_offset;
    CTC_ERROR_GOTO(_sys_greatbelt_l2_encode_dsmac(lchip, &ds_mac, &sys), ret, error_return);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, p_node->ad_index, cmd, &ds_mac), ret, error_return);
    return CTC_E_NONE;

error_return:
    if(is_new_node)
    {
        ctc_vector_del(pl2_gb_master[lchip]->vport2nh_vec, logic_port);
        sys_greatbelt_ftm_free_table_offset(lchip, DsMac_t, 1, 1, p_node->ad_index);
        mem_free(p_node);
    }
    else
    {
        p_node->nhid = old_nh_id;
    }
    return ret;
}

int32
sys_greatbelt_l2_set_nhid_by_logic_port(uint8 lchip, uint16 logic_port, uint32 nhp_id)
{
    SYS_LOGIC_PORT_CHECK(logic_port);
    if(0 == nhp_id)
    {
        CTC_ERROR_RETURN(_sys_greatbelt_l2_unbind_nhid_by_logic_port(lchip, logic_port));
    }
    else
    {
        CTC_ERROR_RETURN(_sys_greatbelt_l2_bind_nhid_by_logic_port(lchip, logic_port, nhp_id));
    }
    return CTC_E_NONE;
}

int32
sys_greatbelt_l2_get_nhid_by_logic_port(uint8 lchip, uint16 logic_port, uint32 *nhp_id)
{
    sys_l2_vport_2_nhid_t *p_node;
    p_node = ctc_vector_get(pl2_gb_master[lchip]->vport2nh_vec, logic_port);

    if (NULL == p_node)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }
    else
    {
        *nhp_id = p_node->nhid;
    }

    return CTC_E_NONE;
}


#define FDB_INDEX_VEC_B         2048
#define FDB_GPORT_VEC_T         (31 * 512 + SYS_GB_MAX_LINKAGG_GROUP_NUM)
#define FDB_GPORT_VEC_B         64
#define FDB_LOGIC_PORT_VEC_B    64
#define FDB_MAC_HASH_B          1024
#define FDB_MAC_FID_HASH_B      64
#define FDB_TCAM_HASH_B         64
#define FDB_DFT_HASH_T          (4 * 1024)
#define FDB_DFT_HASH_B          1024
#define FDB_MCAST_VEC_B         1024

/* get MAC HASH KEY memory information form driver */
STATIC int32
_sys_greatbelt_l2_init_hash_mem_info(uint8 lchip)
{
    uint32 tbl_id = DsMacHashKey_t;
    uint32 blk_id;
    uint32 valid_blk_count = 0;
    uint32 index_array[MAX_DRV_BLOCK_NUM];

    sal_memset(index_array, 0, sizeof(index_array));

    for (blk_id = 0; blk_id < MAX_DRV_BLOCK_NUM; blk_id++)
    {
        if (!IS_BIT_SET(DYNAMIC_BITMAP(tbl_id), blk_id))
        {
            continue;
        }
        index_array[valid_blk_count] = DYNAMIC_START_INDEX(tbl_id, blk_id) + DYNAMIC_ENTRY_NUM(tbl_id, blk_id);
        valid_blk_count++;
    }

    pl2_gb_master[lchip]->hash_index_array = mem_malloc(MEM_FDB_MODULE, sizeof(uint32)*valid_blk_count);
    sal_memcpy(pl2_gb_master[lchip]->hash_index_array, index_array, sizeof(uint32)*valid_blk_count);
    pl2_gb_master[lchip]->hash_block_count = valid_blk_count;

    return CTC_E_NONE;
}

/* sw1 always exist:
    default entry, black hole .
    STATIC fdb, mcast fdb */
STATIC int32
_sys_greatbelt_l2_init_sw(uint8 lchip)
{
    uint32 size = 0;

    if (pl2_gb_master[lchip]->tcam_num)
    {
        pl2_gb_master[lchip]->tcam_hash = ctc_hash_create(CTC_VEC_BLOCK_NUM(pl2_gb_master[lchip]->tcam_num, FDB_TCAM_HASH_B),
                                           FDB_TCAM_HASH_B,
                                           (hash_key_fn) _sys_greatbelt_l2_tcam_hash_make,
                                           (hash_cmp_fn) _sys_greatbelt_l2_tcam_hash_compare);
        if (!pl2_gb_master[lchip]->tcam_hash)
        {
            return CTC_E_NO_MEMORY;
        }
    }


    pl2_gb_master[lchip]->fid_vec = ctc_vector_init(CTC_VEC_BLOCK_NUM(pl2_gb_master[lchip]->cfg_max_fid + 1, FDB_DFT_HASH_B),
                                          FDB_DFT_HASH_B);

    if (!pl2_gb_master[lchip]->fid_vec)
    {
        return CTC_E_NO_MEMORY;
    }

    if (pl2_gb_master[lchip]->cfg_vport_num)
    {
        pl2_gb_master[lchip]->vport2nh_vec = ctc_vector_init(CTC_VEC_BLOCK_NUM(pl2_gb_master[lchip]->cfg_vport_num, FDB_LOGIC_PORT_VEC_B),
                                                   FDB_LOGIC_PORT_VEC_B);
        if (!pl2_gb_master[lchip]->vport2nh_vec)
        {
            return CTC_E_NO_MEMORY;
        }
    }

    CTC_ERROR_RETURN(sys_greatbelt_ftm_get_dyn_entry_size(lchip, SYS_FTM_DYN_ENTRY_GLB_MET, &size));

    pl2_gb_master[lchip]->mcast2nh_vec = ctc_vector_init(CTC_VEC_BLOCK_NUM(size, FDB_MCAST_VEC_B),
                                               FDB_MCAST_VEC_B);
    if (!pl2_gb_master[lchip]->mcast2nh_vec)
    {
        return CTC_E_NO_MEMORY;
    }

    SYS_L2_REG_SYN_ADD_DB_CB(lchip, add);
    SYS_L2_REG_SYN_DEL_DB_CB(lchip, remove);

    return CTC_E_NONE;
}

/* sw0 only when sw exist*/
STATIC int32
_sys_greatbelt_l2_init_extra_sw(uint8 lchip)
{
    uint32 ds_mac_size = 0;
    ctc_spool_t spool;

    /* alloc fdb mac hash size  */

    CTC_ERROR_RETURN(sys_greatbelt_ftm_get_table_entry_num(lchip, DsMac_t, &ds_mac_size));

    sal_memset(&spool, 0, sizeof(ctc_spool_t));
    spool.lchip = lchip;
    spool.block_num = CTC_VEC_BLOCK_NUM(ds_mac_size, FDB_MAC_FID_HASH_B);
    spool.block_size = FDB_MAC_FID_HASH_B;
    spool.max_count = ds_mac_size;
    spool.user_data_size = sizeof(sys_l2_ad_spool_t);
    spool.spool_key = (hash_key_fn)_sys_greatbelt_l2_ad_spool_hash_make;
    spool.spool_cmp = (hash_cmp_fn)_sys_greatbelt_l2_ad_spool_hash_cmp;
    pl2_gb_master[lchip]->ad_spool = ctc_spool_create(&spool);

    pl2_gb_master[lchip]->fdb_hash = ctc_hash_create(CTC_VEC_BLOCK_NUM(pl2_gb_master[lchip]->extra_sw_size, FDB_MAC_FID_HASH_B),
                                           FDB_MAC_FID_HASH_B,
                                           (hash_key_fn) _sys_greatbelt_l2_fdb_hash_make,
                                           (hash_cmp_fn) _sys_greatbelt_l2_fdb_hash_compare);

    pl2_gb_master[lchip]->mac_hash = ctc_hash_create(CTC_VEC_BLOCK_NUM(pl2_gb_master[lchip]->extra_sw_size, FDB_MAC_HASH_B),
                                           FDB_MAC_HASH_B,
                                           (hash_key_fn) _sys_greatbelt_l2_mac_hash_make,
                                           (hash_cmp_fn) _sys_greatbelt_l2_fdb_hash_compare);

    /* only for ucast */
    pl2_gb_master[lchip]->gport_vec = ctc_vector_init(CTC_VEC_BLOCK_NUM(FDB_GPORT_VEC_T, FDB_GPORT_VEC_B),
                                            FDB_GPORT_VEC_B);

    if (pl2_gb_master[lchip]->cfg_vport_num)
    {
        pl2_gb_master[lchip]->vport_vec = ctc_vector_init(CTC_VEC_BLOCK_NUM(pl2_gb_master[lchip]->cfg_vport_num, FDB_LOGIC_PORT_VEC_B),
                                                FDB_LOGIC_PORT_VEC_B);

        if (!pl2_gb_master[lchip]->vport_vec)
        {
            return CTC_E_NO_MEMORY;
        }
    }

    if (!pl2_gb_master[lchip]->fdb_hash ||
        !pl2_gb_master[lchip]->mac_hash ||
        !pl2_gb_master[lchip]->gport_vec)
    {
        return CTC_E_NO_MEMORY;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l2_init_default_entry(uint8 lchip, uint16 default_entry_num)
{
    uint32                         cmd          = 0;
    uint32                         start_offset = 0;
    uint32                         fld_value    = 0;
    uint16                         fid          = 0;
    sys_l2_info_t                  sys;
    fib_engine_lookup_result_ctl_t fib_engine_lookup_result_ctl;

    /* get the start offset of DaMac */
    CTC_ERROR_RETURN(sys_greatbelt_ftm_alloc_table_offset(lchip, DsMac_t, 1, default_entry_num, &start_offset));
    pl2_gb_master[lchip]->dft_entry_base = start_offset;

    /* bit[16]:agingEn, bit[15:6]:defaultEntryBase[15:6], bit[5]:defaultEntryEn */
    fld_value = (((pl2_gb_master[lchip]->dft_entry_base >> 6) & 0x3FF) << 1) | 1;

    sal_memset(&fib_engine_lookup_result_ctl, 0, sizeof(fib_engine_lookup_result_ctl));

    cmd = DRV_IOR(FibEngineLookupResultCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &fib_engine_lookup_result_ctl));

    fib_engine_lookup_result_ctl.mac_da_lookup_result_ctl |= fld_value;
    fib_engine_lookup_result_ctl.mac_sa_lookup_result_ctl |= fld_value;
        cmd = DRV_IOW(FibEngineLookupResultCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &fib_engine_lookup_result_ctl));
    SYS_FDB_DBG_INFO("dft_entry_base = 0x%x \n", pl2_gb_master[lchip]->dft_entry_base);


    sal_memset(&sys, 0, sizeof(sys));
    sys.flag.flag_node.type_default = 1;
    sys.revert_default    = 1;
        for (fid = 0; fid <= pl2_gb_master[lchip]->cfg_max_fid; fid++)
        {
            sys.ad_index = DEFAULT_ENTRY_INDEX(fid);
            CTC_ERROR_RETURN(_sys_greatbelt_l2_write_hw(lchip, &sys));
        }

        /* write drop index */
        sys.ad_index = pl2_gb_master[lchip]->ad_index_drop;
        CTC_ERROR_RETURN(_sys_greatbelt_l2_write_hw(lchip, &sys));
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l2_set_global_cfg(uint8 lchip, ctc_l2_fdb_global_cfg_t* pcfg)
{
    CTC_PTR_VALID_CHECK(pcfg);

    /* allocate logic_port_2_nh vec, only used for logic_port use hw learning */
    /* set the num of logic port */
    pl2_gb_master[lchip]->cfg_vport_num = pcfg->logic_port_num;
    pl2_gb_master[lchip]->cfg_hw_learn  = pcfg->hw_learn_en;
    pl2_gb_master[lchip]->init_hw_state = pcfg->hw_learn_en;
    pl2_gb_master[lchip]->cfg_has_sw    = !pl2_gb_master[lchip]->cfg_hw_learn;
    pl2_gb_master[lchip]->cfg_flush_cnt = pcfg->flush_fdb_cnt_per_loop;
    pl2_gb_master[lchip]->trie_sort_en  = pcfg->trie_sort_en;
    if (pl2_gb_master[lchip]->trie_sort_en)
    {
        sys_greatbelt_fdb_sort_register_fdb_entry_cb(lchip, sys_greatbelt_l2_fdb_dump_all);
        sys_greatbelt_fdb_sort_register_trie_sort_cb(lchip, sys_greatbelt_l2_fdb_trie_sort_en);
        sys_greatbelt_fdb_sort_register_mac_limit_cb(lchip, sys_greatbelt_mac_security_learn_limit_handler);
    }
    if (pcfg->default_entry_rsv_num)
    {
        pl2_gb_master[lchip]->cfg_max_fid = pcfg->default_entry_rsv_num - 1;
    }
    else
    {
        pl2_gb_master[lchip]->cfg_max_fid = 5 * 1024 - 1;     /*default num 5K*/
    }

    /*init hash base*/
    pl2_gb_master[lchip]->hash_base = 16;

    /* add default entry */
    CTC_ERROR_RETURN(_sys_greatbelt_l2_init_default_entry(lchip, pl2_gb_master[lchip]->cfg_max_fid + 1));

    if (pl2_gb_master[lchip]->cfg_hw_learn)
    {
        pl2_gb_master[lchip]->vp_alloc_ad_en = 0;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l2_set_register(uint8 lchip)
{
    uint32                          cmd         = 0;
    uint32                          field_value = 0;
    fib_engine_lookup_result_ctl1_t fib_engine_lookup_result_ctl1;
    fib_engine_lookup_result_ctl_t fib_engine_lookup_result_ctl;

    cmd         = DRV_IOR(FibEngineLookupResultCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &fib_engine_lookup_result_ctl));
    pl2_gb_master[lchip]->ad_bits_type = fib_engine_lookup_result_ctl.ad_bits_type;

    sal_memset(&fib_engine_lookup_result_ctl1, 0, sizeof(fib_engine_lookup_result_ctl1));

    if (pl2_gb_master[lchip]->cfg_hw_learn)
    {
        fib_engine_lookup_result_ctl1.ds_mac_base2 = pl2_gb_master[lchip]->base_trunk;
        fib_engine_lookup_result_ctl1.ds_mac_base1 = pl2_gb_master[lchip]->base_gport;
        fib_engine_lookup_result_ctl1.ds_mac_base0 = pl2_gb_master[lchip]->base_vport;
    }
        cmd = DRV_IOW(FibEngineLookupResultCtl1_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &fib_engine_lookup_result_ctl1));

    /**/
     /*field_value = (1 == pl2_gb_master[lchip]->cfg_has_sw) ? 0: 1;*/
    field_value = 0;
    cmd         = DRV_IOW(FibEngineLookupResultCtl_t, FibEngineLookupResultCtl_FastLearningIgnoreFifoFull_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    /* for hw_aging gb alwasy set 1 */
    field_value = pl2_gb_master[lchip]->cfg_hw_learn;
    cmd         = DRV_IOW(FibEngineLookupResultCtl_t, FibEngineLookupResultCtl_ForceHwAging_f);

        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    cmd = DRV_IOW(IpeAgingCtl_t, IpeAgingCtl_HwAgingEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l2_reserve_port_dsmac(uint8 lchip, uint8 gchip, uint32 base, uint32 rsv_num)
{
    uint32        cmd   = 0;
    ds_mac_t      macda;
    uint16        dest_port = 0;
    sys_l2_info_t sys;
    uint32        fwd_offset = 0;
    uint8         index   = 0;
    uint32        drv_lport = 0;
    uint8         slice_en = (pl2_gb_master[lchip]->rchip_ad_rsv[gchip] >> 14) & 0x3;

    sal_memset(&macda, 0, sizeof(ds_mac_t));
    sal_memset(&sys, 0, sizeof(sys_l2_info_t));

    cmd = DRV_IOW(DsMac_t, DRV_ENTRY_FLAG);

    for (index = 0; index < rsv_num; index++)
    {
        if ((rsv_num == SYS_GB_MAX_PORT_NUM_PER_CHIP) || (slice_en == 0) || (slice_en == 1))
        {
            drv_lport = index;
        }
        else if (slice_en == 2)
        {
            drv_lport = index + 256;
        }
        else if (slice_en == 3)
        {
            drv_lport = (index >= rsv_num/2) ? (256 + index - rsv_num/2) : index;
        }

        dest_port = CTC_MAP_LPORT_TO_GPORT(gchip, drv_lport);
        sys_greatbelt_brguc_get_dsfwd_offset(lchip, dest_port, CTC_NH_PARAM_BRGUC_SUB_TYPE_BASIC, &fwd_offset);
        sys.gport   = dest_port;
        sys.fwd_ptr = fwd_offset;
        sys.dsfwd_valid = 1;

        SYS_FDB_DBG_INFO("HW learn: dest_port:0x%x, fwd_ptr:0x%x\n", dest_port, sys.fwd_ptr);
        CTC_ERROR_RETURN(_sys_greatbelt_l2_encode_dsmac(lchip, &macda, &sys));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, base + index, cmd, &macda));
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l2_reserve_ad_index(uint8 lchip)
{
    uint32 rsv_ad_num = 0;
    uint32 offset     = 0;
    uint8 gchip = 0;

    rsv_ad_num = SYS_GB_MAX_LINKAGG_GROUP_NUM + SYS_GB_MAX_PORT_NUM_PER_CHIP;

    if (pl2_gb_master[lchip]->cfg_hw_learn)
    {
        SYS_FDB_DBG_DUMP("fdb hw learning enable\n");

         /*only hw learning should reserve AD for logic port*/
        rsv_ad_num += pl2_gb_master[lchip]->cfg_vport_num;
    }
    CTC_ERROR_RETURN(sys_greatbelt_ftm_alloc_table_offset(lchip, DsMac_t, 1, rsv_ad_num, &offset));
    SYS_FDB_DBG_INFO(" logic_port_num: 0x%x  rsv_ad_num:0x%x  offset:0x%x\n",
                     pl2_gb_master[lchip]->cfg_vport_num, rsv_ad_num, offset);

    if (pl2_gb_master[lchip]->cfg_hw_learn)
    {
        pl2_gb_master[lchip]->base_vport = offset + SYS_GB_MAX_LINKAGG_GROUP_NUM + SYS_GB_MAX_PORT_NUM_PER_CHIP;
    }
    pl2_gb_master[lchip]->base_gport = offset + SYS_GB_MAX_LINKAGG_GROUP_NUM;
    pl2_gb_master[lchip]->base_trunk = offset;
    sys_greatbelt_get_gchip_id(lchip, &gchip);
    CTC_ERROR_RETURN(_sys_greatbelt_l2_reserve_port_dsmac(lchip, gchip, pl2_gb_master[lchip]->base_gport, SYS_GB_MAX_PORT_NUM_PER_CHIP));

    return CTC_E_NONE;
}

/* for stacking
*  slice_en,means reserve for different slice : 1-slice0, 2-slice1, 3-slice0&slice1
**/
int32
sys_greatbelt_l2_reserve_rchip_ad_index(uint8 lchip, uint8 gchip, uint16 max_port_num, uint8 slice_en)
{
    uint32 rsv_ad_num = 0;
    uint32 offset     = 0;

    SYS_GLOBAL_CHIPID_CHECK(gchip);

    /*Only sw learning need reserve */
    if (pl2_gb_master[lchip]->cfg_hw_learn)
    {
        return CTC_E_NONE;
    }

    /*if gchip is local, return */
    if (sys_greatbelt_chip_is_local(lchip, gchip))
    {
        return CTC_E_NONE;
    }

    rsv_ad_num = max_port_num;
    CTC_ERROR_RETURN(sys_greatbelt_ftm_alloc_table_offset(lchip, DsMac_t, 1, rsv_ad_num, &offset));

    /*bit14--slice0, bit15--slice1*/
    pl2_gb_master[lchip]->rchip_ad_rsv[gchip] = (slice_en & 0x3) << 14;

    CTC_ERROR_RETURN(_sys_greatbelt_l2_reserve_port_dsmac(lchip, gchip, offset, rsv_ad_num));

    L2_LOCK;
    pl2_gb_master[lchip]->base_rchip[gchip] = offset;
    pl2_gb_master[lchip]->rchip_ad_rsv[gchip] += rsv_ad_num;
    L2_UNLOCK;
    SYS_FDB_DBG_INFO("DsMac rsv num for rchip:%d, base:0x%x\n",rsv_ad_num,offset);
    return CTC_E_NONE;
}

/* for stacking */
int32
sys_greatbelt_l2_free_rchip_ad_index(uint8 lchip, uint8 gchip)
{
    uint32 rsv_ad_num = 0;
    uint32 offset     = 0;
    int32  ret        = 0;

    SYS_GLOBAL_CHIPID_CHECK(gchip);

    /*Only sw learning need reserve */
    if (pl2_gb_master[lchip]->cfg_hw_learn)
    {
        return CTC_E_NONE;
    }

    /*if gchip is local, return */
    if (sys_greatbelt_chip_is_local(lchip, gchip))
    {
        return CTC_E_NONE;
    }

    if ((pl2_gb_master[lchip]->rchip_ad_rsv[gchip] & 0x3FFF) == 0)
    {
        return CTC_E_NONE;
    }
    L2_LOCK;
    offset = pl2_gb_master[lchip]->base_rchip[gchip];
    rsv_ad_num = (pl2_gb_master[lchip]->rchip_ad_rsv[gchip]&0x3FFF);
    CTC_ERROR_GOTO(sys_greatbelt_ftm_free_table_offset(lchip, DsMac_t, 1, rsv_ad_num, offset),ret, error_0);
    pl2_gb_master[lchip]->rchip_ad_rsv[gchip] = 0;
    L2_UNLOCK;
    return CTC_E_NONE;
error_0:
    L2_UNLOCK;
    return CTC_E_NONE;

}


/* type refer to sys_l2_station_move_op_type_t */
int32
sys_greatbelt_l2_set_station_move(uint8 lchip, uint16 gport, uint8 type, uint32 value)
{
    uint16 lport = 0;
    uint32 cmd = 0;
    uint32 ad_index = 0;
    uint32 field_val = 0;
    ds_mac_t macda;
    uint8  gchip = 0;
    uint8  is_local_chip = 0;

    SYS_L2_INIT_CHECK(lchip);

    if (MAX_SYS_L2_STATION_MOVE_OP <= type)
    {
        return CTC_E_INVALID_PARAM;
    }

    CTC_GLOBAL_PORT_CHECK(gport);

    sal_memset(&macda, 0, sizeof(ds_mac_t));

    if (CTC_IS_LINKAGG_PORT(gport))
    {
        field_val = gport;
    }
    else
    {
        gchip = CTC_MAP_GPORT_TO_GCHIP(gport);
        CTC_GLOBAL_CHIPID_CHECK(gchip);
        lport = CTC_MAP_GPORT_TO_LPORT(gport);
        is_local_chip = sys_greatbelt_chip_is_local(lchip, gchip);
        if (is_local_chip)
        {
            cmd = DRV_IOR(DsPhyPortExt_t, DsPhyPortExt_GlobalSrcPort_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));
        }
    }

    if (SYS_DRV_IS_LINKAGG_PORT(field_val) || CTC_IS_LINKAGG_PORT(gport))
    {
        ad_index = pl2_gb_master[lchip]->base_trunk + (field_val & CTC_LINKAGGID_MASK); /* linkagg */
    }
    else if (is_local_chip)
    {

        ad_index = pl2_gb_master[lchip]->base_gport + lport; /* gport */
    }
    else if (pl2_gb_master[lchip]->rchip_ad_rsv[gchip])
    {
        uint8   slice_en = (pl2_gb_master[lchip]->rchip_ad_rsv[gchip] >> 14) & 0x3;
        uint16  rsv_num = pl2_gb_master[lchip]->rchip_ad_rsv[gchip] & 0x3FFF;
        uint32  ad_base = pl2_gb_master[lchip]->base_rchip[gchip];

        if (slice_en == 1)
        {
            /*slice 0*/
            if (lport >= rsv_num)
            {
                return CTC_E_INVALID_PARAM;
            }
            ad_index = lport + ad_base;
        }
        else if (slice_en == 3)
        {
            if (((lport >= 256) && (lport >= (256 + rsv_num/2)))
                || ((lport < 256) && (lport >= rsv_num/2)))
            {
                return CTC_E_INVALID_PARAM;
            }
            ad_index = ((lport >= 256) ? (lport - 256 + rsv_num/2) : lport) + ad_base;
        }
        else if (slice_en == 2)
        {
            /*slice 1*/
            if (((lport&0xFF) >= rsv_num) || (lport < 256))
            {
                return CTC_E_INVALID_PARAM;
            }
            ad_index = lport - 256 + ad_base;
        }
        else
        {
            return CTC_E_INVALID_PARAM;
        }
    }
    else
    {
        return CTC_E_NONE;
    }


    /* update ad */
    cmd = DRV_IOR(DsMac_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, ad_index, cmd, &macda));

    if (SYS_L2_STATION_MOVE_OP_PRIORITY == type)
    {
        macda.src_mismatch_learn_en = (value == 0) ? 1 : 0;
    }
    else if (SYS_L2_STATION_MOVE_OP_ACTION == type)
    {
        macda.src_mismatch_discard = (value != CTC_PORT_STATION_MOVE_ACTION_TYPE_FWD) ? 1 : 0;
    }

    cmd = DRV_IOW(DsMac_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, ad_index, cmd, &macda));

    return CTC_E_NONE;
}

/* type refer to sys_l2_station_move_op_type_t */
int32
sys_greatbelt_l2_get_station_move(uint8 lchip, uint16 gport, uint8 type, uint32* value)
{
    uint16 lport = 0;
    uint32 cmd = 0;
    uint32 field_val = 0;
    uint32 ad_index = 0;
    ds_mac_t macda;

    SYS_L2_INIT_CHECK(lchip);

    if (MAX_SYS_L2_STATION_MOVE_OP <= type)
    {
        return CTC_E_INVALID_PARAM;
    }

    sal_memset(&macda, 0, sizeof(ds_mac_t));

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    cmd = DRV_IOR(DsPhyPortExt_t, DsPhyPortExt_GlobalSrcPort_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));

    if (SYS_DRV_IS_LINKAGG_PORT(field_val))
    {
        ad_index = pl2_gb_master[lchip]->base_trunk + (field_val & SYS_DRV_LOCAL_PORT_MASK); /* linkagg */
    }
    else
    {
        SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);
        ad_index = pl2_gb_master[lchip]->base_gport + lport; /* gport */
    }

    cmd = DRV_IOR(DsMac_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, ad_index, cmd, &macda));

    if (SYS_L2_STATION_MOVE_OP_PRIORITY == type)
    {
        *value = macda.src_mismatch_learn_en ? 0 : 1;
    }
    else if (SYS_L2_STATION_MOVE_OP_ACTION == type)
    {
        *value = macda.src_mismatch_discard ? CTC_PORT_STATION_MOVE_ACTION_TYPE_DISCARD : CTC_PORT_STATION_MOVE_ACTION_TYPE_FWD;
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_l2_set_hw_learn(uint8 lchip, uint8 hw_en)
{
    uint16 lport    = 0;
    uint32 cmd      = 0;
    uint32 value    = 0;
    SYS_L2_INIT_CHECK(lchip);
    if (pl2_gb_master[lchip]->total_count)
    {
        return CTC_E_FDB_ONLY_SW_LEARN;
    }
    if ((1 == pl2_gb_master[lchip]->init_hw_state) && (0 == hw_en))
    {
        return CTC_E_FDB_ONLY_HW_LEARN;
    }

    value = (1 == hw_en) ? 1:0;
    cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_FastLearningEn_f);
        for (lport = 0; lport < SYS_GB_MAX_PORT_NUM_PER_CHIP; lport++)
        {
            DRV_IOCTL(lchip, lport, cmd, &value);
        }

    pl2_gb_master[lchip]->cfg_hw_learn  = hw_en;
    pl2_gb_master[lchip]->cfg_has_sw    = !pl2_gb_master[lchip]->cfg_hw_learn;

    return _sys_greatbelt_l2_set_register(lchip);

}

int32
sys_greatbelt_l2_fdb_tcam_reinit(uint8 lchip, uint8 is_add)
{
    uint32                         cmd          = 0;
    uint32                         field_val    = 0;
     fib_engine_lookup_result_ctl_t fib_engine_lookup_result_ctl;
    if (is_add)
    {
        field_val = (((pl2_gb_master[lchip]->dft_entry_base >> 6) & 0x3FF) << 1) | 1;
        sal_memset(&fib_engine_lookup_result_ctl, 0, sizeof(fib_engine_lookup_result_ctl));
        cmd = DRV_IOR(FibEngineLookupResultCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &fib_engine_lookup_result_ctl));
        fib_engine_lookup_result_ctl.mac_da_lookup_result_ctl |= field_val;
        fib_engine_lookup_result_ctl.mac_sa_lookup_result_ctl |= field_val;
        cmd = DRV_IOW(FibEngineLookupResultCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &fib_engine_lookup_result_ctl));
        CTC_ERROR_RETURN(_sys_greatbelt_l2_set_register(lchip));

        /*Hash Mac Sa AgingEn*/
        field_val = 0;
        cmd = DRV_IOR(FibEngineLookupResultCtl_t, FibEngineLookupResultCtl_MacSaLookupResultCtl_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        field_val |= (1 << 11);
        cmd = DRV_IOW(FibEngineLookupResultCtl_t, FibEngineLookupResultCtl_MacSaLookupResultCtl_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

        /*Hash IPv4 mcast da AgingEn*/
        field_val = 0;
        cmd = DRV_IOR(FibEngineLookupResultCtl_t, FibEngineLookupResultCtl_IpDaLookupResultCtl1_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        field_val |= (1 << 11);
        cmd = DRV_IOW(FibEngineLookupResultCtl_t, FibEngineLookupResultCtl_IpDaLookupResultCtl1_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

        /*Hash IPv6 mcast da AgingEn*/
        field_val = 0;
        cmd = DRV_IOR(FibEngineLookupResultCtl_t, FibEngineLookupResultCtl_IpDaLookupResultCtl3_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        field_val |= (1 << 11);
        cmd = DRV_IOW(FibEngineLookupResultCtl_t, FibEngineLookupResultCtl_IpDaLookupResultCtl3_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    }



    return CTC_E_NONE;
}


int32
sys_greatbelt_l2_fdb_init(uint8 lchip, void* l2_fdb_global_cfg)
{
    uint32 hash_entry_num = 0;
    uint32 tcam_entry_num = 0;
    int32  ret            = 0;
    drv_work_platform_type_t platform_type;

    if (pl2_gb_master[lchip]) /*already init */
    {
        return CTC_E_NONE;
    }

    /* get num of Hash Key */
    sys_greatbelt_ftm_get_table_entry_num(lchip, DsMacHashKey_t, &hash_entry_num);
    sys_greatbelt_ftm_get_table_entry_num(lchip, DsMacBridgeKey_t, &tcam_entry_num);
    if (0 == hash_entry_num) /* no hash no fdb */
    {
        return CTC_E_FDB_NO_RESOURCE;
    }

    MALLOC_ZERO(MEM_FDB_MODULE, pl2_gb_master[lchip], sizeof(sys_l2_master_t));
    if (!pl2_gb_master[lchip])
    {
        return CTC_E_NO_MEMORY;
    }

    ret = sal_mutex_create(&(pl2_gb_master[lchip]->l2_mutex));
    if (ret || !(pl2_gb_master[lchip]->l2_mutex))
    {
        return CTC_E_NO_RESOURCE;
    }

    ret = sal_mutex_create(&(pl2_gb_master[lchip]->fib_acc_mutex));
    if (ret || !(pl2_gb_master[lchip]->fib_acc_mutex))
    {
        return CTC_E_NO_RESOURCE;
    }

    drv_greatbelt_get_platform_type(&platform_type);
    pl2_gb_master[lchip]->get_by_dma = (platform_type == HW_PLATFORM) ? 1 : 0;
    pl2_gb_master[lchip]->pure_hash_num = hash_entry_num;
    pl2_gb_master[lchip]->tcam_num      = tcam_entry_num;

     /*if set, indicate logic port alloc ad_index by opf when bind nexthop,only supported in SW learning mode*/
    pl2_gb_master[lchip]->vp_alloc_ad_en = 0;

    CTC_ERROR_RETURN(sys_greatbelt_ftm_alloc_table_offset(lchip, DsMac_t, 0, 1, &(pl2_gb_master[lchip]->ad_index_drop)));

    CTC_ERROR_RETURN(_sys_greatbelt_l2_set_global_cfg(lchip, l2_fdb_global_cfg));
    CTC_ERROR_RETURN(_sys_greatbelt_l2_reserve_ad_index(lchip));

    pl2_gb_master[lchip]->extra_sw_size = pl2_gb_master[lchip]->pure_hash_num + pl2_gb_master[lchip]->hash_base;
    CTC_ERROR_RETURN(_sys_greatbelt_l2_init_extra_sw(lchip));

    CTC_ERROR_RETURN(_sys_greatbelt_l2_init_sw(lchip));
    CTC_ERROR_RETURN(_sys_greatbelt_l2_init_hash_mem_info(lchip));
    CTC_ERROR_RETURN(sys_greatbelt_l2_set_hw_learn(lchip, pl2_gb_master[lchip]->cfg_hw_learn));

    /* set chip_capability */
    CTC_ERROR_RETURN(sys_greatbelt_global_set_chip_capability(lchip, CTC_GLOBAL_CAPABILITY_LOGIC_PORT_NUM, pl2_gb_master[lchip]->cfg_vport_num));
    CTC_ERROR_RETURN(sys_greatbelt_global_set_chip_capability(lchip, CTC_GLOBAL_CAPABILITY_MAX_FID, pl2_gb_master[lchip]->cfg_max_fid));
    CTC_ERROR_RETURN(sys_greatbelt_global_set_chip_capability(lchip, CTC_GLOBAL_CAPABILITY_BLACK_HOLE_ENTRY_NUM, pl2_gb_master[lchip]->tcam_num));


    sys_greatbelt_ftm_tcam_cb_register(lchip, SYS_FTM_TCAM_KEY_FDB, sys_greatbelt_l2_fdb_tcam_reinit);
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l2_fdb_free_node_data(void* node_data, void* user_data)
{
    sys_l2_fid_node_t* p_fid_node = NULL;

    /*release fid vector list*/
    if (user_data)
    {
        p_fid_node = (sys_l2_fid_node_t*)node_data;
        ctc_list_delete(p_fid_node->fid_list);
    }

    mem_free(node_data);

    return CTC_E_NONE;
}

int32
sys_greatbelt_l2_fdb_deinit(uint8 lchip)
{
    LCHIP_CHECK(lchip);
    if (NULL == pl2_gb_master[lchip])
    {
        return CTC_E_NONE;
    }

    if (pl2_gb_master[lchip]->cfg_has_sw)
    {
        /*free ad data*/
        ctc_spool_free(pl2_gb_master[lchip]->ad_spool);
        /*free fdb hash data*/
        ctc_hash_traverse(pl2_gb_master[lchip]->fdb_hash, (hash_traversal_fn)_sys_greatbelt_l2_fdb_free_node_data, NULL);
        ctc_hash_free(pl2_gb_master[lchip]->fdb_hash);
        /*free mac data*/
        /*ctc_hash_traverse(pl2_gb_master[lchip]->mac_hash, (hash_traversal_fn)_sys_greatbelt_l2_fdb_free_node_data, NULL);*/
        ctc_hash_free(pl2_gb_master[lchip]->mac_hash);
        /*free gport data*/
        ctc_vector_traverse(pl2_gb_master[lchip]->gport_vec, (vector_traversal_fn)_sys_greatbelt_l2_fdb_free_node_data, NULL);
        ctc_vector_release(pl2_gb_master[lchip]->gport_vec);
        if (pl2_gb_master[lchip]->cfg_vport_num)
        {
            /*free vport data*/
            ctc_vector_traverse(pl2_gb_master[lchip]->vport_vec, (vector_traversal_fn)_sys_greatbelt_l2_fdb_free_node_data, NULL);
            ctc_vector_release(pl2_gb_master[lchip]->vport_vec);
        }
    }

    if (pl2_gb_master[lchip]->tcam_num)
    {
        /*free tcam data*/
        ctc_hash_traverse(pl2_gb_master[lchip]->tcam_hash, (hash_traversal_fn)_sys_greatbelt_l2_fdb_free_node_data, NULL);
        ctc_hash_free(pl2_gb_master[lchip]->tcam_hash);
    }

    /*free fid data*/
    ctc_vector_traverse(pl2_gb_master[lchip]->fid_vec, (vector_traversal_fn)_sys_greatbelt_l2_fdb_free_node_data, pl2_gb_master[lchip]->fid_vec);
    ctc_vector_release(pl2_gb_master[lchip]->fid_vec);
    if (pl2_gb_master[lchip]->cfg_vport_num)
    {
        /*free vport2nh data*/
        ctc_vector_traverse(pl2_gb_master[lchip]->vport2nh_vec, (vector_traversal_fn)_sys_greatbelt_l2_fdb_free_node_data, NULL);
        ctc_vector_release(pl2_gb_master[lchip]->vport2nh_vec);
    }
    /*free mcast2nh data*/
    ctc_vector_traverse(pl2_gb_master[lchip]->mcast2nh_vec, (vector_traversal_fn)_sys_greatbelt_l2_fdb_free_node_data, NULL);
    ctc_vector_release(pl2_gb_master[lchip]->mcast2nh_vec);

    mem_free(pl2_gb_master[lchip]->hash_index_array);

    sal_mutex_destroy(pl2_gb_master[lchip]->l2_mutex);
    sal_mutex_destroy(pl2_gb_master[lchip]->fib_acc_mutex);
    mem_free(pl2_gb_master[lchip]);

    return CTC_E_NONE;
}

bool
sys_greatbelt_l2_fdb_trie_sort_en(uint8 lchip)
{
    if (NULL == pl2_gb_master[lchip])
    {
        return FALSE;
    }
    if (pl2_gb_master[lchip]->trie_sort_en)
    {
        return TRUE;
    }

    return FALSE;
}
/**internal test**/
int32
sys_greatbelt_l2_fdb_set_trie_sort(uint8 lchip, uint8 enable)
{
    SYS_L2_INIT_CHECK(lchip);

    pl2_gb_master[lchip]->trie_sort_en = enable;

    return CTC_E_NONE;
}



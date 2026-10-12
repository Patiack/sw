/**
   @file sys_greatbelt_l2_fdb.h

   @date 2013-07-19

   @version v2.0

   The file defines Macro, stored data structure for Normal FDB module
 */
#ifndef _SYS_GREATBELT_L2_FDB_H_
#define _SYS_GREATBELT_L2_FDB_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "sal.h"
#include "ctc_const.h"
#include "ctc_hash.h"
#include "ctc_vector.h"
#include "ctc_linklist.h"
#include "ctc_spool.h"
#include "ctc_l2.h"

typedef struct
{
    uint16  is_static          : 1,  /*ad*/
            src_discard        : 1,  /*ad*/
            drop               : 1,  /*ad*/
            src_discard_to_cpu : 1,  /*ad*/
            logic_port         : 1,  /*ad*/
            copy_to_cpu        : 1,  /*ad*/
            protocol_entry     : 1,  /*ad*/
            bind_port          : 1,  /*ad*/
            ptp_entry          : 1,  /*ad*/
            ucast_discard      : 1,  /*ad*/
            mcast_discard      : 1,  /*ad*/
            aging_disable      : 1,  /*ad*/
            rsv0               : 4;

    uint16  rsv1;
}sys_l2_flag_ad_t;

typedef struct
{
    /* below are not include hash compare */
    uint32  remote_dynamic     : 1,  /*not ad*/
            ecmp_valid         : 1,  /*not ad*/
            is_tcam            : 1,  /*not ad*/
            rsv_ad_index       : 1,  /*not ad*/
            type_default       : 1,  /*not ad*/
            type_l2mc          : 1,  /*not ad*/
            system_mac         : 1,  /*not ad*/
            white_list         : 1,  /*not ad*/
            rsv1               : 24;
}sys_l2_flag_node_t;

typedef struct
{
    sys_l2_flag_ad_t flag_ad;
    sys_l2_flag_node_t flag_node;
}sys_l2_flag_t;

typedef struct
{
    sys_l2_flag_t flag;      /**< t's the sys_l2_node_flag_t value */
    uint32        key_index; /**< mac key index */
    uint32        ad_index;  /**< mac ad index */
}sys_l2_detail_t;

enum sys_l2_station_move_op_type_e
{
    SYS_L2_STATION_MOVE_OP_PRIORITY,
    SYS_L2_STATION_MOVE_OP_ACTION,
    MAX_SYS_L2_STATION_MOVE_OP
};
typedef enum sys_l2_station_move_op_type_e sys_l2_station_move_op_type_t;

#define SYS_LOGIC_PORT_CHECK(logic_port)           \
    if ((logic_port) >= pl2_gb_master[lchip]->cfg_vport_num) \
    { return CTC_E_INVALID_LOGIC_PORT; }

#define SYS_LOGIC_PORT_CHECK_WITH_UNLOCK(logic_port)           \
    if ((logic_port) >= pl2_gb_master[lchip]->cfg_vport_num) \
    {         \
        L2_UNLOCK;  \
        return CTC_E_INVALID_LOGIC_PORT; \
    }

#define DONTCARE_FID    0xFFFF
#define REGULAR_INVALID_FID(fid)    ((fid == 0)||(!pl2_gb_master[lchip] || ((fid) > pl2_gb_master[lchip]->cfg_max_fid)))
#define L2UC_INVALID_FID(fid)       (DONTCARE_FID != fid) && REGULAR_INVALID_FID(fid)

#define SYS_L2_GID_CHECK(fid)


#define SYS_L2_FID_CHECK(fid)         \
    if (REGULAR_INVALID_FID(fid))     \
    {                                 \
        return CTC_E_FDB_INVALID_FID; \
    }

#define SYS_L2UC_FID_CHECK(fid)       \
    if (L2UC_INVALID_FID(fid))     \
    {                                 \
        return CTC_E_FDB_INVALID_FID; \
    }

struct sys_l2_master_s
{
    uint32      extra_sw_size;        /**< dynamic fdb size. 0 or (hash + cam)*/
    uint32      pure_hash_num;       /**< pure hash entry number, exclude cam. */
    uint32      tcam_num;            /**< tcam entry number.*/
    uint32      dynamic_count;       /**< count of dynamic entry in soft hash table */
    uint32      local_dynamic_count; /**< count of local dynamic entry in soft hash table */
    uint32      l2mc_count;          /**< multicast entry count */
    uint32      def_count;           /**< default entry count */
    uint32      total_count;         /**< total: uc_static + uc_dynamic + mc + cam */
    uint32      cam_count;           /**< count of local dynamic entry in soft hash table */
    uint32      ad_index_drop;       /**< all Ad share it if drop */
    uint32      dft_entry_base;      /**< index base of default entry */

    uint16      hash_base;           /**< offset from cam*/
    uint16      tcam_base;           /**< offset from tcam*/

    /*** soft tabls to store fdb node ***/
    ctc_spool_t * ad_spool;            /**< ds mac spool */
    ctc_hash_t  * fdb_hash;            /**< node is sys_l2_node_t, key is mac and fid */
    ctc_hash_t  * mac_hash;            /**< node is sys_l2_node_t, key is mac */
    ctc_vector_t* fid_vec;             /**< node is sys_l2_fid_node_t, key is fid or fid+port */
    ctc_vector_t* gport_vec;           /**< node is sys_l2_port_node_t, index is tid or phy port */
    ctc_vector_t* vport_vec;           /**< node is sys_l2_port_node_t, index is logic port*/
    ctc_vector_t* vport2nh_vec;        /**< node is sys_l2_vport_2_nhid_t, index is logic port */
    ctc_hash_t  * tcam_hash;           /**< node is sys_l2_tcam_t, key is mac, mac_mask */
    ctc_vector_t* mcast2nh_vec;        /**< node is sys_l2_mcast_node_t, index is mcast group ID */

    sal_mutex_t * l2_mutex;            /**< mutex for fdb */
    sal_mutex_t * fib_acc_mutex;       /**< mutex for fib acc */

    uint8       ad_bits_type;
    uint8       init_hw_state;
    uint8       get_by_dma;         /**< get HW table by DMA */

    uint32     *hash_index_array;   /**< MAC HASH KEY memory index array */
    uint32      hash_block_count;   /**< MAC HASH KEY memory block count */

    uint32      base_vport;
    uint32      base_gport;
    uint32      base_trunk;

    uint32      cfg_vport_num;      /**< num of logic port */
    uint32      cfg_flush_cnt;      /**< delete all entries if cfg_flush_cnt=0*/
    uint8       cfg_hw_learn;       /**< support hw learning. but not force all do hw learning. */
    uint8       cfg_has_sw;         /**< maintain the smallset sw */
    uint16      cfg_max_fid;        /**< CTC_MAX_FID_ID, 16383=0x3fff*/
    uint8       trie_sort_en;
    uint32      base_rchip[SYS_GB_MAX_GCHIP_CHIP_ID+1];
    uint16      rchip_ad_rsv[SYS_GB_MAX_GCHIP_CHIP_ID+1];
    uint8       vp_alloc_ad_en;     /**< if set, indicate logic port alloc ad_index by opf when bind nexthop,only supported in SW learning mode*/

    int32       (*sync_add_db)(uint8 lchip, ctc_l2_addr_t* l2_addr, uint32 nhid, uint32 index);
    int32       (*sync_remove_db)(uint8 lchip, ctc_l2_addr_t* l2_addr);
};
typedef struct sys_l2_master_s   sys_l2_master_t;


extern sys_l2_master_t* pl2_gb_master[CTC_MAX_LOCAL_CHIP_NUM];
/**********************************************************************************
                      Define API function interfaces
 ***********************************************************************************/

extern int32
sys_greatbelt_l2_fdb_init(uint8 lchip, void* l2_fdb_global_cfg);

/**
 @brief De-Initialize l2 module
*/
extern int32
sys_greatbelt_l2_fdb_deinit(uint8 lchip);

int32
sys_greatbelt_l2_set_default_entry_features(uint8 lchip, ctc_l2dflt_addr_t* l2dflt_addr);

int32
sys_greatbelt_l2_get_default_entry_features(uint8 lchip, ctc_l2dflt_addr_t* l2dflt_addr);

extern int32
sys_greatbelt_l2_get_fdb_count(uint8 lchip, ctc_l2_fdb_query_t* pQuery);

extern int32
sys_greatbelt_l2_get_fdb_entry(uint8 lchip, ctc_l2_fdb_query_t* pQuery,
                                ctc_l2_fdb_query_rst_t* query_rst);

extern int32
sys_greatbelt_l2_get_fdb_entry_by_mac(uint8 lchip, ctc_l2_fdb_query_t* pQuery,
                                       ctc_l2_fdb_query_rst_t* query_rst);

extern int32
sys_greatbelt_l2_add_fdb(uint8 lchip, ctc_l2_addr_t* l2_addr, uint8 with_nh, uint32 nhid);

extern int32
sys_greatbelt_l2_remove_fdb(uint8 lchip, ctc_l2_addr_t* l2_addr);

extern int32
sys_greatbelt_l2_remove_fdb_by_index(uint8 lchip, uint32 index);

extern int32
sys_greatbelt_l2_get_fdb_by_index(uint8 lchip, uint32 index, uint8 use_logic_port, ctc_l2_addr_t* l2_addr, sys_l2_detail_t* l2_detail);

extern int32
sys_greatbelt_l2_flush_fdb(uint8 lchip, ctc_l2_flush_fdb_t* pFlush);

extern int32
sys_greatbelt_l2_add_default_entry(uint8 lchip, ctc_l2dflt_addr_t* l2dflt_addr);

extern int32
sys_greatbelt_l2_remove_default_entry(uint8 lchip, ctc_l2dflt_addr_t* l2dflt_addr);

extern int32
sys_greatbelt_l2_add_port_to_default_entry(uint8 lchip, ctc_l2dflt_addr_t* l2dflt_addr);

extern int32
sys_greatbelt_l2_remove_port_from_default_entry(uint8 lchip, ctc_l2dflt_addr_t* l2dflt_addr);

extern int32
sys_greatbelt_l2mcast_add_addr(uint8 lchip, ctc_l2_mcast_addr_t* l2mc_addr);

extern int32
sys_greatbelt_l2mcast_remove_addr(uint8 lchip, ctc_l2_mcast_addr_t* l2mc_addr);

extern int32
sys_greatbelt_l2mcast_add_member(uint8 lchip, ctc_l2_mcast_addr_t* l2mc_addr);

extern int32
sys_greatbelt_l2mcast_remove_member(uint8 lchip, ctc_l2_mcast_addr_t* l2mc_addr);

extern int32
sys_greatbelt_l2_show_status(uint8 lchip);

extern int32
sys_greatbelt_l2_set_nhid_by_logic_port(uint8 lchip, uint16 logic_port, uint32 nhp_id);

extern int32
sys_greatbelt_l2_get_nhid_by_logic_port(uint8 lchip, uint16 logic_port, uint32 *nhp_id);

extern int32
sys_greatbelt_l2_sync_hw_learn_aging_info(uint8 lchip, void* p_learn_fifo);

extern int32
sys_greatbelt_l2_set_dsmac_for_linkagg_hw_learning(uint8 lchip, uint16 gport, bool b_add);

extern int32
sys_greatbelt_l2_set_fid_property(uint8 lchip, uint16 fid_id, uint32 property, uint32 value);

extern int32
sys_greatbelt_l2_get_fid_property(uint8 lchip, uint16 fid_id, uint32 property, uint32* value);

extern int32
sys_greatbelt_l2_set_l2_master(uint8 lchip, int8 flush_by_hw, int8 aging_by_hw);

extern int32
sys_greatbelt_l2_set_station_move(uint8 lchip, uint16 gport, uint8 type, uint32 value);

extern int32
sys_greatbelt_l2_get_station_move(uint8 lchip, uint16 gport, uint8 type, uint32* value);
extern bool
sys_greatbelt_l2_fdb_trie_sort_en(uint8 lchip);

extern int32
sys_greatbelt_l2_set_unknown_mcast_tocpu(uint8 lchip, uint16 fid, uint32 value);

extern int32
sys_greatbelt_l2_get_unknown_mcast_tocpu(uint8 lchip, uint16 fid, uint32* value);
extern int32
sys_greatbelt_l2_default_entry_set_mac_auth(uint8 lchip, uint16 fid, bool enable);
extern int32
sys_greatbelt_l2_default_entry_get_mac_auth(uint8 lchip, uint16 fid, bool* enable);

extern int32
sys_greatbelt_l2_fdb_set_entry_hit(uint8 lchip, ctc_l2_addr_t* l2_addr, uint8 hit);
extern int32
sys_greatbelt_l2_fdb_get_entry_hit(uint8 lchip, ctc_l2_addr_t* l2_addr, uint8* hit);

extern int32
sys_greatbelt_l2_replace_fdb(uint8 lchip, ctc_l2_replace_t* p_replace);

extern int32
sys_greatbelt_l2_reserve_rchip_ad_index(uint8 lchip, uint8 gchip, uint16 max_port_num, uint8 slice_en);

extern int32
sys_greatbelt_l2_free_rchip_ad_index(uint8 lchip, uint8 gchip);

#ifdef __cplusplus
}
#endif
#endif /*end of _SYS_GREATBELT_L2_FDB_H_*/



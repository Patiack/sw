/**
   @file sys_goldengate_l2_fdb.h

   @date 2013-07-19

   @version v2.0

   The file defines Macro, stored data structure for Normal FDB module
 */
#ifndef _SYS_GOLDENGATE_L2_FDB_H_
#define _SYS_GOLDENGATE_L2_FDB_H_
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
            self_address       : 1,
            rsv0               : 7;
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
            aps_valid          : 1,  /*not ad*/
            white_list         : 1, /*not ad*/
            mc_nh              : 1,  /*not ad, for Uc using  mcast nexthop*/
            aging_disable      : 1,  /*not ad, disable aging*/
            is_ecmp_intf       : 1,  /*not ad, disable aging*/
            limit_exempt       : 1,
            rsv1               : 19;

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
    uint32        pending;
}sys_l2_detail_t;

enum sys_l2_station_move_op_type_e
{
    SYS_L2_STATION_MOVE_OP_PRIORITY,
    SYS_L2_STATION_MOVE_OP_ACTION,
    MAX_SYS_L2_STATION_MOVE_OP
};
typedef enum sys_l2_station_move_op_type_e sys_l2_station_move_op_type_t;

#define L2_LOCK \
    if (pl2_gg_master[lchip]->l2_mutex) sal_mutex_lock(pl2_gg_master[lchip]->l2_mutex)

#define L2_UNLOCK \
    if (pl2_gg_master[lchip]->l2_mutex) sal_mutex_unlock(pl2_gg_master[lchip]->l2_mutex)

#define L2_DUMP_LOCK \
    if (pl2_gg_master[lchip]->l2_dump_mutex) sal_mutex_lock(pl2_gg_master[lchip]->l2_dump_mutex)

#define L2_DUMP_UNLOCK \
    if (pl2_gg_master[lchip]->l2_dump_mutex) sal_mutex_unlock(pl2_gg_master[lchip]->l2_dump_mutex)

#define SYS_L2_INIT_CHECK(lchip)             \
    do {                                \
        SYS_LCHIP_CHECK_ACTIVE(lchip);       \
        if (NULL == pl2_gg_master[lchip])  \
        { return CTC_E_NOT_INIT; }      \
    } while(0)

#define SYS_L2_INDEX_CHECK(index)           \
    if ((index) >= (pl2_gg_master[lchip]->pure_hash_num+SYS_L2_FDB_CAM_NUM)) \
    { return CTC_E_INVALID_PARAM; }


#define SYS_LOGIC_PORT_CHECK(logic_port)           \
    if ((logic_port) >= pl2_gg_master[lchip]->cfg_vport_num) \
    { return CTC_E_INVALID_LOGIC_PORT; }

#define SYS_LOGIC_PORT_CHECK_WITH_UNLOCK(logic_port)           \
    if ((logic_port) >= pl2_gg_master[lchip]->cfg_vport_num) \
    {         \
        L2_UNLOCK;  \
        return CTC_E_INVALID_LOGIC_PORT; \
    }

#define DONTCARE_FID    0xFFFF
#define REGULAR_INVALID_FID(fid)    ((fid == 0)||(!pl2_gg_master[lchip] || ((fid) > pl2_gg_master[lchip]->cfg_max_fid)))
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

#define IS_MCAST_DESTMAP(dm) ((dm >> 18) & 0x1)

struct sys_l2_master_s
{
    uint32      extra_sw_size;        /**< dynamic fdb size. 0 or (hash + cam)*/
    uint32      pure_hash_num;       /**< pure hash entry number, exclude cam. */
    uint32      tcam_num;            /**< black hole cam entry number.*/
    uint32      dynamic_count;       /**< count of dynamic entry in soft hash table */
    uint32      local_dynamic_count; /**< count of local dynamic entry in soft hash table */
    uint32      l2mc_count;          /**< multicast entry count */
    uint32      def_count;           /**< default entry count */
    uint32      total_count;         /**< total: uc_static + uc_dynamic + mc + cam */
    uint32      cam_count;           /**< count of local dynamic entry in soft hash table */
    uint32      ad_index_drop;       /**< all Ad share it if drop */
    uint32      dft_entry_base;      /**< index base of default entry */
    uint32      alloc_ad_cnt;         /**< alloced ad count*/

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
    sal_mutex_t * l2_dump_mutex;       /**< mutex for dump fdb */

    uint8       rsvd[2];
    uint8       static_fdb_limit;
    uint8       init_hw_state;

    uint32      base_vport;
    uint32      base_gport;
    uint32      base_trunk;
    uint32      base_rchip[SYS_GG_MAX_GCHIP_CHIP_ID+1];
    uint16      rchip_ad_rsv[SYS_GG_MAX_GCHIP_CHIP_ID+1];

    uint32      cfg_vport_num;      /**< num of logic port */
    uint32      cfg_flush_cnt;      /**< delete all entries if cfg_flush_cnt=0*/
    uint8       cfg_hw_learn;       /**< support hw learning. but not force all do hw learning. */
    uint8       cfg_has_sw;         /**< maintain the smallset sw */
    uint16      cfg_max_fid;        /**< CTC_MAX_FID_ID, 16383=0x3fff*/
    uint8       trie_sort_en;
    uint32      unknown_mcast_tocpu_nhid;
    uint8       vp_alloc_ad_en;    /**< if set, indicate logic port alloc ad_index by opf when bind nexthop,only supported in SW learning mode*/
};
typedef struct sys_l2_master_s   sys_l2_master_t;


struct mc_mem_s
{
    uint8 is_assign;
    uint8 remote_chip;
    uint16   mem_port;
    uint32   nh_id;
} ;
typedef struct mc_mem_s   mc_mem_t;

typedef struct
{
    uint32                 query_flag;
    uint32                 count;
    uint32                 fid;
    ctc_l2_fdb_query_rst_t * pr;
    sys_l2_detail_t        * pi;
    uint8                  lchip; /**< for tranvase speific lchip*/
}sys_l2_all_cb_t;

typedef struct
{
    uint32 key_index;
    uint32 ad_index;

    uint8  conflict;
    uint8  hit;
    uint8  pending;   /* mac limit reach*/
    uint8  rsv0;
}mac_lookup_result_t;

typedef struct
{
    mac_addr_t mac;
    uint16     fid;
}sys_l2_key_t;

typedef struct
{
    mac_addr_t mac;
}sys_l2_tcam_key_t;

typedef struct
{
    sys_l2_tcam_key_t    key;
    uint32               key_index;
    uint32               ad_index;
    sys_l2_flag_t        flag;
}sys_l2_tcam_t;

typedef struct
{
    /* keep u0 and flag at top!!! for the need of caculate hash. */
    uint32 nhid;   /*for mc, def. because they don't have fwdptr.*/
    uint16        gport; /* from user define for source port. */
    uint16        dest_gport; /* for dest port. */
    sys_l2_flag_ad_t flag;

    uint32        index;
}sys_l2_ad_spool_t;

struct sys_l2_node_s
{
    sys_l2_key_t   key;
    uint32         key_index;       /**< key key_index for ucast and mcast, adptr->index for default entry*/
    sys_l2_ad_spool_t    * adptr;
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
    uint32 ad_idx;
};
typedef struct sys_l2_vport_2_nhid_s   sys_l2_vport_2_nhid_t;

struct sys_l2_mcast_node_s
{
    uint32 nhid;
    uint32 key_index;
    uint32 ref_cnt;
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
    uint8          share_grp_en; /* for remove default entry, if set, not remove nh*/
    uint8          unknown_mcast_tocpu;
}sys_l2_fid_node_t;


/* proved neccsary. like init default entry. */
typedef struct
{
    mac_addr_t    mac;
    uint16        fid;
    uint32        gport;         /**<used for source port for learning*/
    sys_l2_flag_t flag;

    uint32        fwd_ptr;

    uint16        mc_gid;
    uint8         with_nh;       /**< used for build ad_index with nexthop */
    uint8         revert_default;

    uint32        nhid;          /**< use it directly if with_nh is true */
    uint32        dsnh_offset;
    uint8         merge_dsfwd;
    uint8         dest_chipid;
    uint16        dest_id;     /**< used to construct destmap, drv port, for dest port*/

    uint32        fid_flag;      /**< ctc_l2_dft_vlan_flag_t   */


    uint32        key_index;
    uint32        ad_index;
    uint8         key_valid;
    uint8         pending;
    uint8         is_exist;
    uint8         share_grp_en;
    uint32        old_ad_index;
}sys_l2_info_t;


typedef struct
{
    sys_l2_node_t          l2_node;
    uint32                 query_flag;
    uint32                 count;
    ctc_l2_fdb_query_rst_t * query_rst;
    sys_l2_detail_t        * fdb_detail_rst;
    uint8                  lchip; /**< for tranvase speific lchip*/
}sys_l2_mac_cb_t;

/**
   @brief flush information when flush all
 */
typedef struct
{
    uint8  flush_flag;
    uint8  lchip;                   /**< for tranvase speific lchip*/
    uint32 flush_fdb_cnt_per_loop;  /**< flush flush_fdb_cnt_per_loop entries one time if flush_fdb_cnt_per_loop>0,
                                        flush all entries if flush_fdb_cnt_per_loop=0 */
    uint16 sleep_cnt;
}sys_l2_flush_t;

extern sys_l2_master_t* pl2_gg_master[CTC_MAX_LOCAL_CHIP_NUM];
/**********************************************************************************
                      Define API function interfaces
 ***********************************************************************************/

extern int32
sys_goldengate_l2_fdb_init(uint8 lchip, void* l2_fdb_global_cfg);

/**
 @brief De-Initialize l2 module
*/
extern int32
sys_goldengate_l2_fdb_deinit(uint8 lchip);

int32
sys_goldengate_l2_set_default_entry_features(uint8 lchip, ctc_l2dflt_addr_t* l2dflt_addr);

int32
sys_goldengate_l2_get_default_entry_features(uint8 lchip, ctc_l2dflt_addr_t* l2dflt_addr);

extern int32
sys_goldengate_l2_get_fdb_count(uint8 lchip, ctc_l2_fdb_query_t* pQuery);

extern int32
sys_goldengate_l2_get_fdb_entry(uint8 lchip, ctc_l2_fdb_query_t* pQuery,
                                ctc_l2_fdb_query_rst_t* query_rst);

extern int32
sys_goldengate_l2_add_fdb(uint8 lchip, ctc_l2_addr_t* l2_addr, uint8 with_nh, uint32 nhid);

extern int32
sys_goldengate_l2_remove_fdb(uint8 lchip, ctc_l2_addr_t* l2_addr);

extern int32
sys_goldengate_l2_remove_fdb_by_index(uint8 lchip, uint32 index);

extern int32
sys_goldengate_l2_get_fdb_by_index(uint8 lchip, uint32 index, ctc_l2_addr_t* l2_addr, sys_l2_detail_t* l2_detail);

extern int32
sys_goldengate_l2_flush_fdb(uint8 lchip, ctc_l2_flush_fdb_t* pFlush);

extern int32
sys_goldengate_l2_add_default_entry(uint8 lchip, ctc_l2dflt_addr_t* l2dflt_addr);

extern int32
sys_goldengate_l2_remove_default_entry(uint8 lchip, ctc_l2dflt_addr_t* l2dflt_addr);

extern int32
sys_goldengate_l2_add_port_to_default_entry(uint8 lchip, ctc_l2dflt_addr_t* l2dflt_addr);

extern int32
sys_goldengate_l2_remove_port_from_default_entry(uint8 lchip, ctc_l2dflt_addr_t* l2dflt_addr);

extern int32
sys_goldengate_l2mcast_add_addr(uint8 lchip, ctc_l2_mcast_addr_t* l2mc_addr);

extern int32
sys_goldengate_l2mcast_remove_addr(uint8 lchip, ctc_l2_mcast_addr_t* l2mc_addr);

extern int32
sys_goldengate_l2mcast_add_member(uint8 lchip, ctc_l2_mcast_addr_t* l2mc_addr);

extern int32
sys_goldengate_l2mcast_remove_member(uint8 lchip, ctc_l2_mcast_addr_t* l2mc_addr);

extern int32
sys_goldengate_l2_show_status(uint8 lchip);

extern int32
sys_goldengate_l2_set_nhid_by_logic_port(uint8 lchip, uint16 logic_port, uint32 nhp_id);

extern int32
sys_goldengate_l2_get_nhid_by_logic_port(uint8 lchip, uint16 logic_port, uint32 *nhp_id);

extern int32
sys_goldengate_l2_sync_hw_info(uint8 lchip, void* pf);

extern int32
sys_goldengate_l2_set_dsmac_for_linkagg_hw_learning(uint8 lchip, uint16 gport, bool b_add);

extern int32
sys_goldengate_l2_set_dsmac_for_bpe(uint8 lchip, uint16 gport, bool b_add);

extern int32
sys_goldengate_l2_set_fid_property(uint8 lchip, uint16 fid_id, uint32 property, uint32 value);

extern int32
sys_goldengate_l2_get_fid_property(uint8 lchip, uint16 fid_id, uint32 property, uint32* value);

extern int32
sys_goldengate_l2_set_port_security(uint8 lchip, uint16 gport, uint8 enable, uint8 is_logic);

extern int32
sys_goldengate_l2_set_station_move(uint8 lchip, uint16 gport, uint8 type, uint32 value);

extern int32
sys_goldengate_l2_get_station_move(uint8 lchip, uint16 gport, uint8 type, uint32* value);

extern int32
sys_goldengate_l2_set_default_entry_features(uint8 lchip, ctc_l2dflt_addr_t* l2dflt_addr);

extern int32
sys_goldengate_l2_reserve_rchip_ad_index(uint8 lchip, uint8 gchip, uint16 max_port_num, uint8 slice_en);
extern int32
sys_goldengate_l2_remove_pending_fdb(uint8 lchip, mac_addr_t mac, uint16 fid, uint32 ad_idx);
extern bool
sys_goldengate_l2_fdb_trie_sort_en(uint8 lchip);
extern int32
sys_goldengate_l2_default_entry_set_mac_auth(uint8 lchip, uint16 fid, bool enable);
extern int32
sys_goldengate_l2_default_entry_get_mac_auth(uint8 lchip, uint16 fid, bool* enable);
extern int32
sys_goldengate_l2_fdb_set_entry_hit(uint8 lchip, ctc_l2_addr_t* l2_addr, uint8 hit);
extern int32
sys_goldengate_l2_fdb_get_entry_hit(uint8 lchip, ctc_l2_addr_t* l2_addr, uint8* hit);

extern int32
sys_goldengate_l2_replace_fdb(uint8 lchip, ctc_l2_replace_t* p_replace);

#ifdef __cplusplus
}
#endif
#endif /*end of _SYS_GOLDENGATE_L2_FDB_H_*/


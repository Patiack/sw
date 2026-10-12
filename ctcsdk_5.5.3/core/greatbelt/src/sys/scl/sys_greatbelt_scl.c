/**
   @file sys_greatbelt_scl.c

   @date 20011-11-19

   @version v2.0

   The file contains all scl APIs of sys layer

 */
/****************************************************************************
*
* Header Files
*
****************************************************************************/
#include "sal.h"

#include "ctc_error.h"
#include "ctc_hash.h"
#include "ctc_spool.h"
#include "ctc_const.h"
#include "ctc_vlan.h"
#include "ctc_qos.h"
#include "ctc_linklist.h"

#include "sys_greatbelt_scl.h"
#include "sys_greatbelt_ftm.h"
#include "sys_greatbelt_common.h"
#include "sys_greatbelt_learning_aging.h"
#include "sys_greatbelt_chip.h"
#include "sys_greatbelt_port.h"
#include "sys_greatbelt_opf.h"
#include "sys_greatbelt_stats.h"
#include "sys_greatbelt_l2_fdb.h"
#include "sys_greatbelt_nexthop_api.h"
#include "sys_greatbelt_nexthop.h"
#include "sys_greatbelt_qos_policer.h"
#include "sys_greatbelt_queue_enq.h"

#include "greatbelt/include/drv_lib.h"

/****************************************************************************
 *
 * Defines and Macros
 *
 *****************************************************************************/



#if (HOST_IS_LE == 0)
typedef union sys_scl_alter_u
{
    uint32 ip_sa : 32;
    struct
    {
        uint32 stag_cfi : 1;
        uint32 stag_cos : 3;
        uint32 svlan_id : 12;
        uint32 ctag_cfi : 1;
        uint32 ctag_cos : 3;
        uint32 cvlan_id : 12;
    } u8;
} sys_scl_alter_t;

typedef struct
{
    uint32 aps_select_protecting_path : 1;
    uint32 igmp_snoop_en              : 1;
    uint32 service_policer_valid      : 1;
    uint32 aps_select_valid           : 1;
    uint32 rsv0                       : 1;
    uint32 aps_select_group_id        : 11;

    uint32 rsv1                       : 16;
} binding_data_h_t;

typedef struct
{
    uint32 vsi_learning_disable     : 1;
    uint32 mac_security_vsi_discard : 1;
    uint32 logic_src_port           : 14;
    uint32 logic_port_type          : 1;
    uint32 service_acl_qos_en       : 1;
    uint32 service_policer_mode     : 1;
    uint32 flow_policer_ptr         : 13;
}binding_data_l_t;


#else /*UML*/
typedef union sys_scl_alter_u
{
    uint32 ip_sa : 32;
    struct
    {
        uint32 cvlan_id : 12;
        uint32 ctag_cos : 3;
        uint32 ctag_cfi : 1;
        uint32 svlan_id : 12;
        uint32 stag_cos : 3;
        uint32 stag_cfi : 1;
    } u8;
} sys_scl_alter_t;

typedef struct
{
    uint32 aps_select_group_id        : 11;
    uint32 rsv0                       : 1;
    uint32 aps_select_valid           : 1;
    uint32 service_policer_valid      : 1;
    uint32 igmp_snoop_en              : 1;
    uint32 aps_select_protecting_path : 1;

    uint32 rsv1                       : 16;
} binding_data_h_t;

typedef struct
{
    uint32 flow_policer_ptr         : 13;
    uint32 service_policer_mode     : 1;
    uint32 service_acl_qos_en       : 1;
    uint32 logic_port_type          : 1;
    uint32 logic_src_port           : 14;
    uint32 mac_security_vsi_discard : 1;
    uint32 vsi_learning_disable     : 1;
}binding_data_l_t;
#endif




/* KEEP CHIP_NUM related field below !! Below are fields not in hash caculation. */
typedef struct
{
    uint16 profile_id;
    uint32 offset;
    uint16 service_policer_ptr;
    uint16 stats_ptr;
}sys_scl_sw_igs_action_chip_t;


/* KEEP CHIP_NUM related field below !! Below are fields not in hash caculation. */
typedef struct
{
    uint16 stats_ptr;
}sys_scl_sw_egs_action_chip_t;


typedef struct
{
    /*chip related property only this. so compare no need use 2*/
    uint16 profile_id;
}sys_scl_sw_vlan_edit_chip_t;



typedef struct
{
    uint32 stag_op     : 3,
           svid_sl     : 2,
           scos_sl     : 2,
           scfi_sl     : 2,
           ctag_op     : 3,
           cvid_sl     : 2,
           ccos_sl     : 2,
           ccfi_sl     : 2,
           vlan_domain : 2,
           tpid_index  : 2,
           rsv         : 10;
    sys_scl_sw_vlan_edit_chip_t chip;
    uint8 lchip;
}sys_scl_sw_vlan_edit_t;


typedef struct
{
    uint32 flag;     /* the same as ctc flag */
    uint32 sub_flag; /* the same as ctc sub_flag */

    uint32 fid_type                  : 1;
    uint32 oam_tunnel_en             : 1;
    uint32 priority                  : 6; /*8*/
    uint32 ds_fwd_ptr_valid          : 1;
    uint32 binding_en                : 1;
    uint32 binding_mac_sa            : 1;
    uint32 is_leaf                   : 1;
    uint32 user_id_exception_en      : 1;
    uint32 bypass_all                : 1;
    uint32 src_queue_select          : 1;
    uint32 deny_bridge               : 1; /*16*/
    uint32 user_vlan_ptr             : 13;
    uint32 roaming_state             : 2;
    uint32 vlan_action_profile_valid : 1; /*32*/

    uint32 mac_isolated_group_en     : 1;
    uint32 aging_valid               : 1;
    uint32 rsv0                      : 2;
    uint32 cvlan_tag_operation_valid : 1;
    uint32 svlan_tag_operation_valid : 1;
    uint32                           : 2;
    uint32                           : 8;  /*16*/
    uint32 service_policer_id        : 16; /*32*/

    uint32 fid                       : 16; /*16*/
    uint32 color                     : 2;
    uint32                           : 14; /*32*/

    uint32 nh_id;
    uint32 stats_id;
    uint32 service_id;

    uint32 svid : 12;
    uint32 scos : 3;
    uint32 scfi : 1;                 /*16*/
    uint32 cvid : 12;
    uint32 ccos : 3;
    uint32 ccfi : 1;                 /*32*/

    union
    {
        uint16           data;
        binding_data_h_t bds;                   /* binding data struture */
        uint16           mac_isolated_group_id; /* 6 bits */
    }bind_h;

    union
    {
        uint32           data;
        binding_data_l_t bds;       /* binding data struture */
    }bind_l;



    sys_scl_sw_igs_action_chip_t chip;

    sys_scl_sw_vlan_edit_t       * vlan_edit; /* keep at tail, so won't involve hash cmp, hash make */
}sys_scl_sw_igs_action_t;

typedef struct
{
    uint32                       flag; /* the same as ctc flag */

    uint32                       stag_op : 3;
    uint32                       svid_sl : 2;
    uint32                       scos_sl : 2;
    uint32                       scfi_sl : 2;
    uint32                       ctag_op : 3;
    uint32                       cvid_sl : 2;
    uint32                       ccos_sl : 2;
    uint32                       ccfi_sl : 2;
    uint32                       color   : 2;
    uint32                       priority : 6;
    uint32                       rsv0    : 6;

    uint32                       svid    : 12;
    uint32                       scos    : 3;
    uint32                       scfi    : 1;
    uint32                       cvid    : 12;
    uint32                       ccos    : 3;
    uint32                       ccfi    : 1;

    uint32 stats_id;
    uint8  block_pkt_type;
    uint8  tpid_index;
    uint8  rsv[2];
    sys_scl_sw_egs_action_chip_t chip;
} sys_scl_sw_egs_action_t;


typedef union
{
    sys_scl_sw_igs_action_t ia;
    sys_scl_sw_egs_action_t ea;
    sys_scl_tunnel_action_t ta;    /* sw struct is same as sys struct */
}sys_scl_sw_action_union_t;


typedef struct
{
    uint32                   ad_index; /* not in hash compare*/
    uint32                   action_flag; /*original action flag in vlan mapping struct*/
    uint8                     type;
    uint8                     ref_cnt;  /* ad ref count, for free*/
    uint8                     rsv;
    uint8                     lchip;
    sys_scl_sw_action_union_t u0;

}sys_scl_sw_action_t;

#define __SYS_SCL_TCAM_KEY__


typedef  struct
{
    uint32     flag;

    uint32     l2_type       : 4;
    uint32     l2_type_mask  : 4;
    uint32     l3_type       : 4;
    uint32     l3_type_mask  : 4;
    uint32     rsv0          : 16;

    uint32     svlan         : 12;
    uint32     stag_cos      : 3;
    uint32     stag_cfi      : 1;
    uint32     cvlan         : 12;
    uint32     ctag_cos      : 3;
    uint32     ctag_cfi      : 1;

    uint32     svlan_mask    : 12;
    uint32     stag_cos_mask : 3;
    uint32     cvlan_mask    : 12;
    uint32     ctag_cos_mask : 3;
    uint32     rsv1          : 2;

    mac_addr_t mac_da;
    mac_addr_t mac_da_mask;
    mac_addr_t mac_sa;
    mac_addr_t mac_sa_mask;
} sys_scl_sw_tcam_key_mac_t;

typedef  struct
{
    uint32 flag;

    uint32 svlan         : 12;
    uint32 stag_cos      : 3;
    uint32 stag_cfi      : 1;
    uint32 cvlan         : 12;
    uint32 ctag_cos      : 3;
    uint32 ctag_cfi      : 1;

    uint32 svlan_mask    : 12;
    uint32 stag_cos_mask : 3;
    uint32 cvlan_mask    : 12;
    uint32 ctag_cos_mask : 3;
    uint32 stag_valid    : 1;
    uint32 ctag_valid    : 1;
    uint32 customer_id;
    uint32 customer_id_mask;
    uint32 gport;
    uint32 gport_mask;
} sys_scl_sw_tcam_key_vlan_t;

typedef  struct
{
    uint32     flag;
    uint32     sub_flag;

    uint32     svlan              : 12;
    uint32     stag_cos           : 3;
    uint32     stag_cfi           : 1;
    uint32     cvlan              : 12;
    uint32     ctag_cos           : 3;
    uint32     ctag_cfi           : 1;

    uint32     svlan_mask         : 12;
    uint32     stag_cos_mask      : 3;
    uint32     cvlan_mask         : 12;
    uint32     ctag_cos_mask      : 3;
    uint32     rsv0               : 2;


    uint32     routed_packet      : 1;
    uint32     ip_option          : 1;
    uint32     frag_info          : 2;
    uint32     frag_info_mask     : 2;
    uint32     dscp               : 6;
    uint32     dscp_mask          : 6;
    uint32     ip_hdr_err         : 1;
    uint32     rsv1               : 13;

    uint32     l2_type            : 4;
    uint32     l2_type_mask       : 4;
    uint32     l3_type            : 4;
    uint32     l3_type_mask       : 4;
    uint32     l4_type            : 4;
    uint32     l4_type_mask       : 4;
    uint32     is_tcp             : 1; /* sw only */
    uint32     is_udp             : 1; /* sw only */
    uint32     flag_is_tcp        : 1; /* sw only */
    uint32     flag_is_udp        : 1; /* sw only */
    uint32     flag_l4info_mapped : 1; /* sw only */
    uint32     rsv2               : 3;

    uint16     l4info_mapped;        /* sw only */
    uint16     l4info_mapped_mask;   /* sw only */

    uint32     l4_src_port      : 16;
    uint32     l4_src_port_mask : 16;
    uint32     l4_dst_port      : 16;
    uint32     l4_dst_port_mask : 16;

    mac_addr_t mac_da;
    mac_addr_t mac_da_mask;
    mac_addr_t mac_sa;
    mac_addr_t mac_sa_mask;

    uint32     ip_sa;
    uint32     ip_sa_mask;

    uint32     ip_da;
    uint32     ip_da_mask;

    uint16     eth_type;
    uint16     eth_type_mask;
} sys_scl_sw_tcam_key_ipv4_t;

typedef  struct
{
    uint32      flag;
    uint32      sub_flag;

    uint32      svlan              : 12;
    uint32      stag_cos           : 3;
    uint32      stag_cfi           : 1;
    uint32      cvlan              : 12;
    uint32      ctag_cos           : 3;
    uint32      ctag_cfi           : 1;

    uint32      svlan_mask         : 12;
    uint32      stag_cos_mask      : 3;
    uint32      cvlan_mask         : 12;
    uint32      ctag_cos_mask      : 3;
    uint32      rsv0               : 2;

    uint32      dscp               : 6;
    uint32      dscp_mask          : 6;
    uint32      flow_label         : 20;

    uint32      l2_type            : 4;
    uint32      l2_type_mask       : 4;
    uint32      l3_type            : 4;
    uint32      l3_type_mask       : 4;
    uint32      l4_type            : 4;
    uint32      l4_type_mask       : 4;
    uint32      frag_info          : 2;
    uint32      frag_info_mask     : 2;
    uint32      ip_option          : 1;
    uint32      rsv1               : 3;

    uint32      l4_src_port        : 16;
    uint32      l4_src_port_mask   : 16;
    uint32      l4_dst_port        : 16;
    uint32      l4_dst_port_mask   : 16;

    uint32      flow_label_mask    : 20;
    uint32      is_tcp             : 1; /* sw only */
    uint32      is_udp             : 1; /* sw only */
    uint32      flag_is_tcp        : 1; /* sw only */
    uint32      flag_is_udp        : 1; /* sw only */
    uint32      flag_l4info_mapped : 1; /* sw only */
    uint32      rsv2               : 7;

    uint16      l4info_mapped;       /* sw only */
    uint16      l4info_mapped_mask;  /* sw only */


    uint16      eth_type;
    uint16      eth_type_mask;

    mac_addr_t  mac_da;
    mac_addr_t  mac_da_mask;
    mac_addr_t  mac_sa;
    mac_addr_t  mac_sa_mask;

    ipv6_addr_t ip_sa;
    ipv6_addr_t ip_sa_mask;

    ipv6_addr_t ip_da;
    ipv6_addr_t ip_da_mask;
} sys_scl_sw_tcam_key_ipv6_t;



#define __SYS_SCL_HASH_KEY__
/*for port | default */
typedef struct
{
    uint16 gport;
    uint8  gport_is_classid :1;
    uint8  gport_is_logic   :1;
    uint8  rsv0             :6;
    uint8  dir;
}sys_scl_sw_hash_key_port_t;

typedef struct
{
    uint32 gport            : 14;
    uint32 vid              : 12;
    uint32 dir              : 1;
    uint32 gport_is_classid : 1;
    uint32 gport_is_logic   : 1;
    uint32 rsv0             : 3;
} sys_scl_sw_hash_key_vlan_t;

typedef struct
{
    uint32 gport            : 14;
    uint32 svid             : 12;
    uint32 ccos             : 3;
    uint32 scos             : 3;

    uint32 cvid             : 12;
    uint32 dir              : 1;
    uint32 gport_is_classid : 1;
    uint32 gport_is_logic   : 1;
    uint32 rsv0             : 17;
} sys_scl_sw_hash_key_vtag_t;

typedef struct
{
    uint32 use_macda        : 1;
    uint32 gport_is_classid : 1;
    uint32 gport            : 14;
    uint32 macsa_h          : 16;

    uint32 macsa_l;
} sys_scl_sw_hash_key_mac_t;

typedef struct
{
    uint32 ip_sa;

    uint32 gport            : 14;
    uint32 gport_is_classid : 1;
    uint32 rsv0             : 17;
}sys_scl_sw_hash_key_ipv4_t;

typedef struct
{
    uint32 global_src_port  : 14;
    uint32 global_dest_port : 14;
    uint32 rsv0             : 4;

    uint32 vid              : 12;
    uint32 rsv1             : 20;
}sys_scl_sw_hash_key_cross_t;

typedef struct
{
    ipv6_addr_t ip_sa;
}sys_scl_sw_hash_key_ipv6_t;

typedef struct
{
    uint32 gport   : 14;
    uint32 vid     : 12;
    uint32 cos     : 3;
    uint32 rsv0    : 3;

    uint32 macsa_h : 16;
    uint32 macda_h : 16;
    uint32 macsa_l;
    uint32 macda_l;
}sys_scl_sw_hash_key_l2_t;


#define __SYS_SCL_KEY__


typedef union
{
    sys_scl_sw_hash_key_port_t         port;
    sys_scl_sw_hash_key_vlan_t         vlan;
    sys_scl_sw_hash_key_vtag_t         vtag;
    sys_scl_sw_hash_key_mac_t          mac;
    sys_scl_sw_hash_key_ipv4_t         ipv4;
    sys_scl_sw_hash_key_cross_t        cross;
    sys_scl_sw_hash_key_ipv6_t         ipv6;
    sys_scl_sw_hash_key_l2_t           l2;
    sys_scl_hash_tunnel_ipv4_key_t     tnnl_ipv4;      /* sw key is same as sys key*/
    sys_scl_hash_tunnel_ipv4_gre_key_t tnnl_ipv4_gre;  /* sw key is same as sys key*/
    sys_scl_hash_tunnel_ipv4_rpf_key_t tnnl_ipv4_rpf;  /* sw key is same as sys key*/
}sys_scl_sw_hash_key_union_t;


typedef struct
{
    sys_scl_sw_hash_key_union_t u0;
}sys_scl_sw_hash_key_t;

typedef union
{
    sys_scl_sw_tcam_key_mac_t      mac_key;
    sys_scl_sw_tcam_key_vlan_t     vlan_key;
    sys_scl_sw_tcam_key_ipv4_t     ipv4_key;
    sys_scl_sw_tcam_key_ipv6_t     ipv6_key;
    sys_scl_tcam_tunnel_ipv4_key_t tnnl_ipv4_key; /* sw key is same as sys key*/
    sys_scl_tcam_tunnel_ipv6_key_t tnnl_ipv6_key; /* sw key is same as sys key*/
    sys_scl_sw_hash_key_t          hash;
}sys_scl_sw_key_union_t;

typedef struct
{
    uint8                  type; /* sys_scl_key_type_t */
    uint8                  conv_type;
    sys_scl_sw_key_union_t u0;
}sys_scl_sw_key_t;


#define __SYS_SCL_SW_NODE__


typedef struct
{
    uint16 gport;
    uint8 type;
     /*uint8 lchip;        // 0xFF means 2 chips. */
    uint8 port_class_id;
}sys_scl_sw_group_info_t;


typedef struct
{
    uint32                  group_id; /* keep group_id top, never move it.*/

    uint8                   block_id; /* block the group belong*/
    uint8                   flag;     /* group flag*/
    uint8                   rsv0[2];

    uint32                  entry_count;


    ctc_slist_t             * entry_list;   /* a list that link entries belong to this group */
    sys_scl_sw_group_info_t group_info;     /* group info */
} sys_scl_sw_group_t;


typedef struct
{
    ctc_fpa_block_t fpab;
}sys_scl_sw_block_t;

typedef struct
{
    ctc_slistnode_t    head;                /* keep head top!! */
    ctc_fpa_entry_t    fpae;                /* keep fpae at second place!! */


    sys_scl_sw_group_t * group;                /* pointer to group node*/
    sys_scl_sw_action_t* action;               /* pointer to action node*/

    sys_scl_sw_key_t   key;                    /* keep key at tail !! */
}sys_scl_sw_entry_t;

#define NORMAL_AD_SHARE_POOL     0
#define DEFAULT_AD_SHARE_POOL    1

 /*#define REF_COUNT(data, lchip)           ctc_spool_get_refcnt(scl_master[lchip]->ad_spool[lchip], data)*/
#define SCL_INNER_ENTRY_ID(eid)          ((eid >= SYS_SCL_MIN_INNER_ENTRY_ID) && (eid <= SYS_SCL_MAX_INNER_ENTRY_ID))

#define SCL_GET_TABLE_ENTYR_NUM(t, n)    CTC_ERROR_RETURN(sys_greatbelt_ftm_get_table_entry_num(lchip, t, n))


/* convert type */
#define SYS_SCL_CONV_NONE     0
#define SYS_SCL_CONV_SHORT    1
#define SYS_SCL_CONV_LONG     2


/* used by show only */
#define SYS_SCL_SHOW_IN_HEX   0xFFFFF


struct sys_scl_cb_info_s
{
    uint8  do_remove;
    uint32 all_key_cnt;
    uint32 hash_key_cnt;
    uint32 flush_count;
};
typedef struct sys_scl_cb_info_s   sys_scl_cb_info_t;


/****************************************************************
*
* Global and Declaration
*
****************************************************************/
#define SYS_SCL_VLAN_ACTION_RESERVE_NUM    63      /* include 0. */


/*software hash size, determined by scl owner.*/
#define SCL_HASH_BLOCK_SIZE            1024
#define VLAN_ACTION_SIZE_PER_BUCKET    16

struct sys_scl_key_small_short_s  /* 3 */
{
    uint32 global_src_port   : 14;
    uint32 radio_mac47_32    : 16;
    uint32 is_label          : 1;
    uint32 rsv0              : 1;

    uint32 ip_sa             : 32;

    uint32 rid               : 5;      /*wlan*/
    uint32 user_id_hash_type : 5;
    /*valid*/
    uint32 g_src_port_valid  : 1;
    uint32 radio_mac_valid   : 1;
    uint32 rid_valid         : 1;
    uint32 ip_sa_valid       : 1;
    uint32 derection         : 1;
    uint32 rsv1              : 17;
};
typedef struct sys_scl_key_small_short_s   sys_scl_key_small_short_t;

struct sys_scl_key_small_long_s
{
    uint32 udp_dest_port15_10 : 6;
    uint32 layer4_type        : 4;
    uint32 udp_dest_port9_0   : 10;
    uint32 table_id1          : 2;
    uint32 table_id0          : 2;
    uint32 is_user_id1        : 1;
    uint32 user_id_hash_type1 : 5;
    uint32 direction          : 1;
    uint32 is_label           : 1;

    uint32 udp_src_port       : 16;
    uint32 mac_sa47_32        : 16;

    uint32 ip_da              : 32;
    uint32 ip_sa              : 32;

    uint32 global_src_port    : 14;
    uint32 is_user_id0        : 1;
    uint32 user_id_hash_type0 : 5;
    uint32 rid                : 5;                                               /* 25 */
    /*valid*/
    uint32 gre_key_valid      : 1;
    uint32 rsv0               : 6;
};
typedef struct sys_scl_key_small_long_s   sys_scl_key_small_long_t;

typedef enum sys_scl_error_label_1_e
{
    OUTPUT_INDEX_NEW_TCAM,
    OUTPUT_INDEX_OLD_TCAM,
    OUTPUT_INDEX_NEW_HASH,
    OUTPUT_INDEX_OLD_HASH
} sys_scl_error_label_1_t;

/****************************************************************
*
* Functions
*
****************************************************************/

#define ___________SCL_INNER_FUNCTION________________________

#define __4_SCL_HW__


#define __________________________TOTAL_NEW_______________
/**
   @file sys_greatbelt_scl.c

   @date 2012-09-19

   @version v2.0

 */

/****************************************************************************
*
* Header Files
*
****************************************************************************/

#define SYS_SCL_ENTRY_OP_FLAG_ADD       1
#define SYS_SCL_ENTRY_OP_FLAG_DELETE    2

#define SYS_SCL_TCAM_ALLOC_NUM          6    /* mac + vlan + ipv4 + ipv6 + tnnl_v4 + tnnl_v6 */
#define SYS_SCL_TCAM_RAW_NUM            6    /* mac + vlan + ipv4 + ipv6 + tnnl_v4 + tnnl_v6 */
#define SYS_SCL_IS_DEFAULT(key_type)    ((key_type == SYS_SCL_KEY_PORT_DEFAULT_INGRESS) || (key_type == SYS_SCL_KEY_PORT_DEFAULT_EGRESS))



typedef struct
{
    uint32  rsv0;
}
sys_scl_register_t;


struct sys_scl_master_s
{
    ctc_hash_t         * group;                               /* Hash table save group by gid.*/
    ctc_fpa_t          * fpa;                                 /* Fast priority arrangement. no array. */
    ctc_hash_t         * entry;                               /* Hash table save hash|tcam entry by eid */
    ctc_hash_t         * entry_by_key;                        /* Hash table save hash entry by key */
    ctc_spool_t        * ad_spool[2]; /* Share pool save hash action & default action  */
    ctc_spool_t        * vlan_edit_spool;                     /* Share pool save vlan edit  */
    sys_scl_sw_block_t block[SYS_SCL_TCAM_RAW_NUM];
    ctc_linklist_t     * group_list[3];                       /* outer tcam , outer hash, inner */

    uint32             max_entry_priority;

    uint8              alloc_id[SYS_SCL_KEY_NUM];
     /*uint8              block_num_max;          // max number of block: 4 */

    uint8              tcam_alloc_num;          /* Allocation number of entry key*/
    uint8              entry_size[SYS_SCL_KEY_NUM];
    uint8              key_size[SYS_SCL_KEY_NUM];
    uint8              hash_igs_action_size;
    uint8              hash_egs_action_size;
    uint8              hash_tunnel_action_size;
    uint8              vlan_edit_size;

    sys_scl_register_t scl_register;

    uint16             hash_count;
    uint16             conflict_count;
    uint16             big_tcam_count;
    uint16             small_tcam_count;

    uint32             def_eid[CTC_BOTH_DIRECTION][SYS_SCL_DEFAULT_ENTRY_PORT_NUM];
    sal_mutex_t        * mutex;
};
typedef struct sys_scl_master_s   sys_scl_master_t;
sys_scl_master_t* scl_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};


#define SYS_SCL_DBG_OUT(level, FMT, ...)    CTC_DEBUG_OUT(scl, scl, SCL_SYS, level, FMT, ## __VA_ARGS__)
#define SYS_SCL_DBG_DUMP(FMT, ...)          SYS_SCL_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, FMT, ## __VA_ARGS__)
#define SYS_SCL_DBG_PARAM(FMT, ...)         SYS_SCL_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, FMT, ## __VA_ARGS__)
#define SYS_SCL_DBG_INFO(FMT, ...)          SYS_SCL_DBG_OUT(CTC_DEBUG_LEVEL_INFO, FMT, ## __VA_ARGS__)
#define SYS_SCL_DBG_ERROR(FMT, ...)         SYS_SCL_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, FMT, ## __VA_ARGS__)
#define SYS_SCL_DBG_FUNC()                  SYS_SCL_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "  %% FUNC: %s()\n", __FUNCTION__)

#define SYS_SCL_INIT_CHECK(lchip)         \
    do {                             \
        SYS_LCHIP_CHECK_ACTIVE(lchip);    \
        if (NULL == scl_master[lchip]) { \
            return CTC_E_NOT_INIT; } \
    } while(0)

#define SYS_SCL_CREATE_LOCK(lchip)                          \
    do                                                      \
    {                                                       \
        sal_mutex_create(&scl_master[lchip]->mutex);        \
        if (NULL == scl_master[lchip]->mutex)               \
        {                                                   \
            CTC_ERROR_RETURN(CTC_E_FAIL_CREATE_MUTEX);      \
        }                                                   \
    } while (0)

#define SYS_SCL_LOCK(lchip) \
    sal_mutex_lock(scl_master[lchip]->mutex)

#define SYS_SCL_UNLOCK(lchip) \
    sal_mutex_unlock(scl_master[lchip]->mutex)

#define CTC_ERROR_RETURN_SCL_UNLOCK(op) \
    do \
    { \
        int32 rv = (op); \
        if (rv < 0) \
        { \
            sal_mutex_unlock(scl_master[lchip]->mutex); \
            return rv; \
        } \
    } while (0)

/* input rsv group id , return failed */
#define SYS_SCL_IS_RSV_GROUP(gid) \
    ((gid) > CTC_SCL_GROUP_ID_MAX_NORMAL && (gid) <= SYS_SCL_GROUP_ID_MAX)

#define SYS_SCL_IS_RSV_TCAM_GROUP(gid)                         \
    ((SYS_SCL_GROUP_ID_INNER_TCAM_IP_TUNNEL == (gid))          \
     || (SYS_SCL_GROUP_ID_INNER_DEFT_IP_SRC_GUARD == (gid))    \
     || (SYS_SCL_GROUP_ID_INNER_TCAM_VLAN_CLASS_MAC == (gid))  \
     || (SYS_SCL_GROUP_ID_INNER_TCAM_VLAN_CLASS_IPV4 == (gid)) \
     || (SYS_SCL_GROUP_ID_INNER_TCAM_VLAN_CLASS_IPV6 == (gid)) \
     || (SYS_SCL_GROUP_ID_INNER_DEFT_VLAN_CLASS == (gid))      \
     || (SYS_SCL_GROUP_ID_INNER_CUSTOMER_ID == (gid))      \
     || (SYS_SCL_GROUP_ID_INNER_TCAM_VLAN_MAPPING_BASE <= (gid)))

#define SYS_SCL_IS_TCAM_GROUP(gid)    (!SYS_SCL_IS_RSV_GROUP(gid) || SYS_SCL_IS_RSV_TCAM_GROUP(gid))


#define SYS_SCL_CHECK_OUTER_GROUP_ID(gid)        \
    {                                            \
        if ((gid) > CTC_SCL_GROUP_ID_HASH_MAX) { \
            return CTC_E_SCL_GROUP_ID; }         \
    }


#define SYS_SCL_CHECK_OUTER_NORMAL_GROUP_ID(gid)   \
    {                                              \
        if ((gid) > CTC_SCL_GROUP_ID_MAX_NORMAL) { \
            return CTC_E_SCL_GROUP_ID; }           \
    }

#define SYS_SCL_CHECK_GROUP_TYPE(type)        \
    {                                         \
        if (type >= CTC_SCL_GROUP_TYPE_MAX) { \
            return CTC_E_SCL_GROUP_TYPE; }    \
    }

#define SYS_SCL_CHECK_GROUP_PRIO(priority)           \
    {                                                \
        if (priority >= SCL_BLOCK_ID_NUM) { \
            return CTC_E_SCL_GROUP_PRIORITY; }       \
    }

#define SYS_SCL_CHECK_ENTRY_ID(eid)            \
    {                                          \
        if (eid > CTC_SCL_MAX_USER_ENTRY_ID) { \
            return CTC_E_INVALID_PARAM; }      \
    }

#define SYS_SCL_CHECK_ENTRY_PRIO(priority)

#define SYS_SCL_SERVICE_ID_CHECK(sid)      \
    {                                      \
        if (sid == 0) {                    \
            return CTC_E_SCL_SERVICE_ID; } \
    }

#define SYS_SCL_CHECK_GROUP_UNEXIST(pg)     \
    {                                       \
        if (!pg)                            \
        {                                   \
            SYS_SCL_UNLOCK(lchip);          \
            return CTC_E_SCL_GROUP_UNEXIST; \
        }                                   \
    }

#define SYS_SCL_SET_MAC_ARRAY(mac, dest_h, dest_l) \
    {                                              \
        mac[0] = (dest_h >> 8) & 0xFF;             \
        mac[1] = (dest_h) & 0xFF;                  \
        mac[2] = (dest_l >> 24) & 0xFF;            \
        mac[3] = (dest_l >> 16) & 0xFF;            \
        mac[4] = (dest_l >> 8) & 0xFF;             \
        mac[5] = (dest_l) & 0xFF;                  \
    }


#define SYS_SCL_SET_MAC_HIGH(dest_h, src)    \
    {                                        \
        dest_h = ((src[0] << 8) | (src[1])); \
    }

#define SYS_SCL_SET_MAC_LOW(dest_l, src)                                       \
    {                                                                          \
        dest_l = ((src[2] << 24) | (src[3] << 16) | (src[4] << 8) | (src[5])); \
    }

#define SYS_SCL_VLAN_ID_CHECK(vlan_id)           \
    {                                            \
        if ((vlan_id) > CTC_MAX_VLAN_ID) {       \
            return CTC_E_VLAN_INVALID_VLAN_ID; } \
    }                                            \

#define SCL_ENTRY_IS_TCAM(type)                                                                              \
    ((SYS_SCL_KEY_TCAM_VLAN == type) || (SYS_SCL_KEY_TCAM_MAC == type) || (SYS_SCL_KEY_TCAM_IPV4 == type) || \
     (SYS_SCL_KEY_TCAM_IPV6 == type) || (SYS_SCL_KEY_TCAM_TUNNEL_IPV4 == type) || (SYS_SCL_KEY_TCAM_TUNNEL_IPV6 == type))

#define SCL_ENTRY_IS_SMALL_TCAM(conv_type)  ((SYS_SCL_CONV_SHORT == conv_type) || (SYS_SCL_CONV_LONG== conv_type))




#define SCL_PRINT_ENTRY_ROW(eid)               SYS_SCL_DBG_DUMP("\n>>entry-id %u : \n", (eid))
#define SCL_PRINT_GROUP_ROW(gid)               SYS_SCL_DBG_DUMP("\n>>group-id %u (first in goes last, installed goes tail) :\n", (gid))
#define SCL_PRINT_PRIO_ROW(prio)               SYS_SCL_DBG_DUMP("\n>>group-prio %u (sort by block_idx ) :\n", (prio))
#define SCL_PRINT_TYPE_ROW(type)               SYS_SCL_DBG_DUMP("\n>>type %u (sort by block_idx ) :\n", (type))
#define SCL_PRINT_ALL_SORT_SEP_BY_ROW(type)    SYS_SCL_DBG_DUMP("\n>>Separate by %s. \n", (type) ? "group" : "priority")

#define SCL_PRINT_COUNT(n)                     SYS_SCL_DBG_DUMP("NO.%04d  ", n)
#define SYS_SCL_ALL_KEY     SYS_SCL_KEY_NUM
#define FIB_KEY_TYPE_SCL    0x1F
#define SYS_SCL_GPORT13_TO_GPORT14(gport)      ((((gport >> 8) << 9)) | (gport & 0xFF))
#define SYS_SCL_GPORT14_TO_GPORT13(gport)      ((((gport >> 9) << 8)) | (gport & 0xFF))

#define SCL_GET_TABLE_ENTYR_NUM(t, n)          CTC_ERROR_RETURN(sys_greatbelt_ftm_get_table_entry_num(lchip, t, n))

typedef struct
{
    uint16 class_id_data;
    uint16 class_id_mask;
    uint16 gport;
    uint8  group_type;
}drv_scl_group_info_t;

typedef struct
{
    uint16             count;
    uint8              detail;
    uint8              rsv;
    sys_scl_key_type_t key_type;
} _scl_cb_para_t;

#define FAIL(ret)              (ret < 0)

#define SCL_OUTER_ENTRY(pe)    ((sys_scl_sw_entry_t *) ((uint8 *) pe - sizeof(ctc_slistnode_t)))

#define ___________SCL_INNER_FUNCTION________________________



#define ___________SCL_TRAVERSE_ALL_________________________

int32
sys_greatbelt_scl_get_hash_count(uint8 lchip)
{

    SYS_SCL_INIT_CHECK(lchip) ;
    SYS_SCL_DBG_FUNC();
        SYS_SCL_DBG_DUMP("Dump lchip%d:\n", lchip);
        SYS_SCL_DBG_DUMP("  conflict count:     %4u ( couting hash conflicting times )\n", scl_master[lchip]->conflict_count);
        SYS_SCL_DBG_DUMP("  hash count:         %4u\n", scl_master[lchip]->hash_count);
        SYS_SCL_DBG_DUMP("  small tcam count:   %4u\n", scl_master[lchip]->small_tcam_count);
        SYS_SCL_DBG_DUMP("  big tcam count:     %4u\n", scl_master[lchip]->big_tcam_count);

    return CTC_E_NONE;
}


#define __SCL_GET_FROM_SW__

/* only hash will call this fuction
 * pe must not NULL
 * pg can be NULL
 */
STATIC int32
_sys_greatbelt_scl_get_nodes_by_key(uint8 lchip, sys_scl_sw_entry_t*  pe_in,
                                    sys_scl_sw_entry_t** pe,
                                    sys_scl_sw_group_t** pg)
{
    sys_scl_sw_entry_t* pe_lkup = NULL;
    sys_scl_sw_group_t* pg_lkup = NULL;

    CTC_PTR_VALID_CHECK(pe_in);
    CTC_PTR_VALID_CHECK(pe);

    SYS_SCL_DBG_FUNC();

    pe_lkup = ctc_hash_lookup(scl_master[lchip]->entry_by_key, pe_in);
    if (pe_lkup)
    {
        pg_lkup = pe_lkup->group;
    }

    *pe = pe_lkup;
    if (pg)
    {
        *pg = pg_lkup;
    }

    return CTC_E_NONE;
}

/*
 * get sys entry node by entry id
 * pg, pb if be NULL, won't care.
 * pe cannot be NULL.
 */
STATIC int32
_sys_greatbelt_scl_get_nodes_by_eid(uint8 lchip, uint32 eid,
                                    sys_scl_sw_entry_t** pe,
                                    sys_scl_sw_group_t** pg,
                                    sys_scl_sw_block_t** pb)
{
    sys_scl_sw_entry_t sys_entry;
    sys_scl_sw_entry_t * pe_lkup = NULL;
    sys_scl_sw_group_t * pg_lkup = NULL;
    sys_scl_sw_block_t * pb_lkup = NULL;
    uint8              alloc_id  = 0;

    CTC_PTR_VALID_CHECK(pe);
    SYS_SCL_DBG_FUNC();


    sal_memset(&sys_entry, 0, sizeof(sys_scl_sw_entry_t));
    sys_entry.fpae.entry_id = eid;
    sys_entry.fpae.lchip = lchip;
    pe_lkup = ctc_hash_lookup(scl_master[lchip]->entry, &sys_entry);

    if (pe_lkup)
    {
        pg_lkup = pe_lkup->group;

        /* get block */
        if (SCL_ENTRY_IS_TCAM(pe_lkup->key.type))
        {
            alloc_id = scl_master[lchip]->alloc_id[pe_lkup->key.type];
            pb_lkup  = &scl_master[lchip]->block[alloc_id];
        }
    }


    *pe = pe_lkup;
    if (pg)
    {
        *pg = pg_lkup;
    }
    if (pb)
    {
        *pb = pb_lkup;
    }

    return CTC_E_NONE;
}

/*
 * get sys group node by group id
 */
STATIC int32
_sys_greatbelt_scl_get_group_by_gid(uint8 lchip, uint32 gid, sys_scl_sw_group_t** sys_group_out)
{
    sys_scl_sw_group_t * p_sys_group_lkup = NULL;
    sys_scl_sw_group_t sys_group;

    CTC_PTR_VALID_CHECK(sys_group_out);
    SYS_SCL_DBG_FUNC();

    sal_memset(&sys_group, 0, sizeof(sys_scl_sw_group_t));
    sys_group.group_id = gid;

    p_sys_group_lkup = ctc_hash_lookup(scl_master[lchip]->group, &sys_group);

    *sys_group_out = p_sys_group_lkup;

    return CTC_E_NONE;
}


/* is_hash can be NULL */
/*static int32
_sys_greatbelt_scl_get_chip_by_pe(sys_scl_sw_entry_t* pe,
                                  uint8* lchip,
                                  uint8* lchip_num,
                                  uint8* is_hash)
{
    uint8 lchip_stored;
    CTC_PTR_VALID_CHECK(lchip);
    CTC_PTR_VALID_CHECK(lchip_num);
    if (is_hash)
    {
        *is_hash = !(SCL_ENTRY_IS_TCAM(pe->key.type));
    }
    if (SCL_ENTRY_IS_TCAM(pe->key.type))
    {
        lchip_stored = pe->group->group_info.lchip;
    }
    else
    {
        lchip_stored = pe->key.u0.hash.lchip;
    }

    if (0xFF == lchip_stored)
    {
        *lchip     = 0;
        *lchip_num = scl_master[*lchip]->lchip_num;
    }
    else
    {
        *lchip     = lchip_stored;
        *lchip_num = (*lchip + 1);
    }

    return CTC_E_NONE;
}*/



/*
 * below is build hw struct
 */
#define __SCL_OPERATE_ON_HW__

/*
 * build driver group info based on sys group info
 */
STATIC int32
_sys_greatbelt_scl_build_ds_group_info(uint8 lchip, sys_scl_sw_group_info_t* pg_sys, drv_scl_group_info_t* pg_drv)
{
    CTC_PTR_VALID_CHECK(pg_drv);
    CTC_PTR_VALID_CHECK(pg_sys);

    SYS_SCL_DBG_FUNC();

    pg_drv->group_type = pg_sys->type;

    switch (pg_sys->type)
    {
    case     CTC_SCL_GROUP_TYPE_HASH: /* won't use it anyway.*/
    case     CTC_SCL_GROUP_TYPE_GLOBAL:
        pg_drv->class_id_data = 0;
        pg_drv->class_id_mask = 0;
        break;
    case     CTC_SCL_GROUP_TYPE_PORT:
        pg_drv->gport = pg_sys->gport;
        break;

    case     CTC_SCL_GROUP_TYPE_PORT_CLASS:
        pg_drv->class_id_data = pg_sys->port_class_id;
        pg_drv->class_id_mask = 0x3F;
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}


/* internal convert only for little tcam */
STATIC int32
_sys_greatbelt_scl_key_convert_to_short(uint8 lchip, sys_scl_sw_key_t* p_key_in,
                                        sys_scl_key_small_short_t* p_short)
{
    sys_scl_alter_t scl_alter_union;

    CTC_DEBUG_ADD_STUB_CTC(NULL);
    CTC_PTR_VALID_CHECK(p_key_in);
    CTC_PTR_VALID_CHECK(p_short);
    SYS_SCL_DBG_FUNC();

    sal_memset(&scl_alter_union, 0, sizeof(sys_scl_alter_t));
    sal_memset(p_short, 0, sizeof(sys_scl_key_small_short_t));

    switch (p_key_in->type)
    {
    case SYS_SCL_KEY_HASH_PORT:
        p_short->global_src_port   = p_key_in->u0.hash.u0.port.gport;
        p_short->is_label          = p_key_in->u0.hash.u0.port.gport_is_classid;
        p_short->user_id_hash_type = USERID_KEY_TYPE_PORT;
        p_short->derection         = p_key_in->u0.hash.u0.port.dir;
        p_short->g_src_port_valid  = 1;
        break;

    case SYS_SCL_KEY_HASH_PORT_CVLAN:
        p_short->global_src_port    = p_key_in->u0.hash.u0.vlan.gport;
        scl_alter_union.u8.cvlan_id = p_key_in->u0.hash.u0.vlan.vid;
        p_short->ip_sa              = scl_alter_union.ip_sa;
        p_short->is_label           = p_key_in->u0.hash.u0.vlan.gport_is_classid;
        p_short->user_id_hash_type  = USERID_KEY_TYPE_CVLAN;
        p_short->derection          = p_key_in->u0.hash.u0.vlan.dir;
        p_short->ip_sa_valid        = 1;
        p_short->g_src_port_valid   = 1;
        break;

    case SYS_SCL_KEY_HASH_PORT_SVLAN:
        p_short->global_src_port    = p_key_in->u0.hash.u0.vlan.gport;
        scl_alter_union.u8.svlan_id = p_key_in->u0.hash.u0.vlan.vid;
        p_short->ip_sa              = scl_alter_union.ip_sa;
        p_short->is_label           = p_key_in->u0.hash.u0.vlan.gport_is_classid;
        p_short->user_id_hash_type  = USERID_KEY_TYPE_SVLAN;
        p_short->derection          = p_key_in->u0.hash.u0.vlan.dir;
        p_short->ip_sa_valid        = 1;
        p_short->g_src_port_valid   = 1;
        break;

    case SYS_SCL_KEY_HASH_PORT_2VLAN:
        p_short->global_src_port    = p_key_in->u0.hash.u0.vtag.gport;
        scl_alter_union.u8.svlan_id = p_key_in->u0.hash.u0.vtag.svid;
        scl_alter_union.u8.cvlan_id = p_key_in->u0.hash.u0.vtag.cvid;
        p_short->ip_sa              = scl_alter_union.ip_sa;
        p_short->is_label           = p_key_in->u0.hash.u0.vtag.gport_is_classid;
        p_short->user_id_hash_type  = USERID_KEY_TYPE_TWO_VLAN;
        p_short->derection          = CTC_EGRESS;
        p_short->ip_sa_valid        = 1;
        p_short->g_src_port_valid   = 1;
        break;

    case SYS_SCL_KEY_HASH_PORT_CVLAN_COS:
        p_short->global_src_port    = p_key_in->u0.hash.u0.vtag.gport;
        scl_alter_union.u8.cvlan_id = p_key_in->u0.hash.u0.vtag.cvid;
        scl_alter_union.u8.ctag_cos = p_key_in->u0.hash.u0.vtag.ccos;
        p_short->ip_sa              = scl_alter_union.ip_sa;
        p_short->is_label           = p_key_in->u0.hash.u0.vtag.gport_is_classid;
        p_short->user_id_hash_type  = USERID_KEY_TYPE_CVLAN_COS;
        p_short->derection          = CTC_EGRESS;
        p_short->ip_sa_valid        = 1;
        p_short->g_src_port_valid   = 1;
        break;

    case SYS_SCL_KEY_HASH_PORT_SVLAN_COS:
        p_short->global_src_port    = p_key_in->u0.hash.u0.vtag.gport;
        scl_alter_union.u8.svlan_id = p_key_in->u0.hash.u0.vtag.svid;
        scl_alter_union.u8.stag_cos = p_key_in->u0.hash.u0.vtag.scos;
        p_short->ip_sa              = scl_alter_union.ip_sa;
        p_short->is_label           = p_key_in->u0.hash.u0.vtag.gport_is_classid;
        p_short->user_id_hash_type  = USERID_KEY_TYPE_SVLAN_COS;
        p_short->derection          = CTC_EGRESS;
        p_short->ip_sa_valid        = 1;
        p_short->g_src_port_valid   = 1;
        break;

    case SYS_SCL_KEY_HASH_TUNNEL_IPV4_RPF:
        p_short->ip_sa       = p_key_in->u0.hash.u0.tnnl_ipv4_rpf.ip_sa;
        p_short->ip_sa_valid = 1;
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_scl_key_convert_to_long(uint8 lchip, sys_scl_sw_key_t* p_key_in,
                                       sys_scl_key_small_long_t* p_long)
{
    CTC_DEBUG_ADD_STUB_CTC(NULL);
    CTC_PTR_VALID_CHECK(p_key_in);
    CTC_PTR_VALID_CHECK(p_long);
    SYS_SCL_DBG_FUNC();

    sal_memset(p_long, 0, sizeof(sys_scl_key_small_long_t));

    switch (p_key_in->type)
    {
    case SYS_SCL_KEY_HASH_TUNNEL_IPV4:
        p_long->ip_sa       = p_key_in->u0.hash.u0.tnnl_ipv4.ip_sa;
        p_long->ip_da       = p_key_in->u0.hash.u0.tnnl_ipv4.ip_da;
        p_long->layer4_type = p_key_in->u0.hash.u0.tnnl_ipv4.l4_type;
        break;

    case SYS_SCL_KEY_HASH_TUNNEL_IPV4_GRE:
        p_long->ip_sa              = p_key_in->u0.hash.u0.tnnl_ipv4_gre.ip_sa;
        p_long->ip_da              = p_key_in->u0.hash.u0.tnnl_ipv4_gre.ip_da;
        p_long->layer4_type        = p_key_in->u0.hash.u0.tnnl_ipv4_gre.l4_type;
        p_long->udp_dest_port15_10 = (p_key_in->u0.hash.u0.tnnl_ipv4_gre.gre_key >> 10) & 0x3F;
        p_long->udp_dest_port9_0   = p_key_in->u0.hash.u0.tnnl_ipv4_gre.gre_key & 0x3FF;
        p_long->udp_src_port       = (p_key_in->u0.hash.u0.tnnl_ipv4_gre.gre_key >> 16) & 0xFFFF;
        p_long->gre_key_valid      = 1;
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}


STATIC int32
_sys_greatbelt_scl_build_cap_short(uint8 lchip, tbl_entry_t* p_scl_vlankey, sys_scl_sw_key_t* p_key)
{
    ds_fib_user_id80_key_t    * p_data = NULL;
    ds_fib_user_id80_key_t    * p_mask = NULL;

    sys_scl_key_small_short_t small_short;

    CTC_DEBUG_ADD_STUB_CTC(NULL);

    CTC_PTR_VALID_CHECK(p_scl_vlankey);
    CTC_PTR_VALID_CHECK(p_key);

    p_data = (ds_fib_user_id80_key_t *) p_scl_vlankey->data_entry;
    p_mask = (ds_fib_user_id80_key_t *) p_scl_vlankey->mask_entry;
    CTC_PTR_VALID_CHECK(p_data);
    CTC_PTR_VALID_CHECK(p_mask);

    SYS_SCL_DBG_FUNC();

    CTC_ERROR_RETURN(_sys_greatbelt_scl_key_convert_to_short(lchip, p_key, &small_short));

    p_data->table_id          = 0;
    p_data->rsv_0             = 0;
    p_data->rsv_1             = 0;
    p_data->is_user_id        = 1;
    p_data->is_label          = small_short.is_label;          /* this field must be set. when convert. */
    p_data->direction         = small_short.derection;
    p_data->user_id_hash_type = small_short.user_id_hash_type; /*this field must be set. when convert.*/

    p_mask->table_id          = 0x3;
    p_mask->rsv_0             = 0;
    p_mask->rsv_1             = 0;
    p_mask->is_user_id        = 0x1;
    p_mask->is_label          = 0x1;
    p_mask->direction         = 0x1;
    p_mask->user_id_hash_type = 0x1F;

    if (small_short.g_src_port_valid)
    {
        p_data->global_src_port = SYS_SCL_GPORT13_TO_GPORT14(small_short.global_src_port);
        p_mask->global_src_port = 0x3FFF;
    }

    if (small_short.radio_mac_valid)
    {
        p_data->radio_mac47_32 = small_short.radio_mac47_32;
        p_mask->radio_mac47_32 = 0xFFFF;
    }

    if (small_short.rid_valid)
    {
        p_data->rid = small_short.rid;
        p_mask->rid = 0x1F;
    }

    if (small_short.ip_sa_valid)
    {
        p_data->ip_sa = small_short.ip_sa;
        p_mask->ip_sa = 0xFFFFFFFF;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_scl_build_cap_long(uint8 lchip, tbl_entry_t* p_long_key, sys_scl_sw_key_t* p_key)
{
    ds_fib_user_id160_key_t  * p_data = NULL;
    ds_fib_user_id160_key_t  * p_mask = NULL;

    sys_scl_key_small_long_t small_long;

    CTC_DEBUG_ADD_STUB_CTC(NULL);
    CTC_PTR_VALID_CHECK(p_long_key);
    CTC_PTR_VALID_CHECK(p_key);

    p_data = (ds_fib_user_id160_key_t *) p_long_key->data_entry;
    p_mask = (ds_fib_user_id160_key_t *) p_long_key->mask_entry;
    CTC_PTR_VALID_CHECK(p_data);
    CTC_PTR_VALID_CHECK(p_mask);

    SYS_SCL_DBG_FUNC();

    CTC_ERROR_RETURN(_sys_greatbelt_scl_key_convert_to_long(lchip, p_key, &small_long));

    p_data->table_id0          = 0;
    p_data->table_id1          = 0;
    p_data->user_id_hash_type0 = small_long.user_id_hash_type0;
    p_data->user_id_hash_type1 = small_long.user_id_hash_type0;
    p_data->is_user_id0        = TRUE;
    p_data->is_user_id1        = TRUE;

    p_mask->table_id0          = 0x3;
    p_mask->table_id1          = 0x3;
    p_mask->user_id_hash_type0 = 0x1F;
    p_mask->user_id_hash_type1 = 0x1F;
    p_mask->is_user_id0        = 0x1;
    p_mask->is_user_id1        = 0x1;

    p_data->ip_sa       = small_long.ip_sa;
    p_data->ip_da       = small_long.ip_da;
    p_data->layer4_type = small_long.layer4_type;
    p_mask->ip_sa       = 0xFFFFFFFF;
    p_mask->ip_da       = 0xFFFFFFFF;
    p_mask->layer4_type = 0xF;

    if (small_long.gre_key_valid)
    {
        p_data->udp_dest_port15_10 = small_long.udp_dest_port15_10;
        p_data->udp_dest_port9_0   = small_long.udp_dest_port9_0;
        p_data->udp_src_port       = small_long.udp_src_port;
        p_mask->udp_dest_port15_10 = 0x3F;
        p_mask->udp_dest_port9_0   = 0x3FF;
        p_mask->udp_src_port       = 0xFFFF;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_scl_build_hash_2vlan(uint8 lchip, sys_scl_sw_key_t* psys, void* pu32, uint16 index)
{
    uint16                           gport   = 0;
    sys_scl_sw_hash_key_vtag_t       * pvtag = &(psys->u0.hash.u0.vtag);
    ds_user_id_double_vlan_hash_key_t* pds   = (ds_user_id_double_vlan_hash_key_t *) pu32;

    CTC_PTR_VALID_CHECK(pds);
    sal_memset(pds, 0, sizeof(ds_user_id_double_vlan_hash_key_t));

    pds->hash_type = USERID_KEY_TYPE_TWO_VLAN;
    pds->valid     = 1;
    pds->is_label  = pvtag->gport_is_classid;
    pds->direction = pvtag->dir;

    if(pds->is_label || pvtag->gport_is_logic)
    {
        gport = pvtag->gport;
    }
    else
    {
        gport = SYS_SCL_GPORT13_TO_GPORT14(pvtag->gport);
    }

    pds->global_src_port12_0  = gport & 0x1fff;
    pds->global_src_port13_13 = gport >> 13;

    pds->cvlan_id0_0  = pvtag->cvid & 0x1;
    pds->cvlan_id11_1 = pvtag->cvid >> 1;
    pds->svlan_id     = pvtag->svid;
    pds->ad_index     = index;
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_scl_build_hash_port(uint8 lchip, sys_scl_sw_key_t* psys, void* pu32, uint16 index)
{
    uint16                    gport   = 0;
    sys_scl_sw_hash_key_port_t* pport = &(psys->u0.hash.u0.port);
    ds_user_id_port_hash_key_t* pds   = (ds_user_id_port_hash_key_t *) pu32;

    CTC_PTR_VALID_CHECK(pds);
    sal_memset(pds, 0, sizeof(ds_user_id_port_hash_key_t));

    pds->hash_type         = USERID_KEY_TYPE_PORT;
    pds->valid             = 1;
    pds->is_label          = pport->gport_is_classid;
    pds->direction         = pport->dir;
    if(pds->is_label || pport->gport_is_logic)
    {
        gport                  = pport->gport;
    }
    else
    {
        gport                  = SYS_SCL_GPORT13_TO_GPORT14(pport->gport);
    }
    pds->global_src_port   = gport & 0x1fff;
    pds->global_src_port13 = gport >> 13;

    pds->ad_index = index;
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_scl_build_hash_cvlan(uint8 lchip, sys_scl_sw_key_t* psys, void* pu32, uint16 index)
{
    uint16                     gport   = 0;
    sys_scl_sw_hash_key_vlan_t * pvlan = &(psys->u0.hash.u0.vlan);
    ds_user_id_cvlan_hash_key_t* pds   = (ds_user_id_cvlan_hash_key_t *) pu32;

    CTC_PTR_VALID_CHECK(pds);
    sal_memset(pds, 0, sizeof(*pds));

    pds->hash_type         = USERID_KEY_TYPE_CVLAN;
    pds->valid             = 1;
    pds->is_label          = pvlan->gport_is_classid;
    pds->direction         = pvlan->dir;

    if(pds->is_label || pvlan->gport_is_logic)
    {
        gport = pvlan->gport;
    }
    else
    {
        gport = SYS_SCL_GPORT13_TO_GPORT14(pvlan->gport);
    }

    pds->global_src_port   = gport & 0x1fff;
    pds->global_src_port13 = gport >> 13;
    pds->cvlan_id0         = pvlan->vid & 0x1;
    pds->cvlan_id11_1      = pvlan->vid >> 1;

    pds->ad_index = index;
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_scl_build_hash_svlan(uint8 lchip, sys_scl_sw_key_t* psys, void* pu32, uint16 index)
{
    uint16                     gport   = 0;
    sys_scl_sw_hash_key_vlan_t * pvlan = &(psys->u0.hash.u0.vlan);
    ds_user_id_svlan_hash_key_t* pds   = (ds_user_id_svlan_hash_key_t *) pu32;

    CTC_PTR_VALID_CHECK(pds);
    sal_memset(pds, 0, sizeof(*pds));

    pds->hash_type         = USERID_KEY_TYPE_SVLAN;
    pds->valid             = 1;
    pds->is_label          = pvlan->gport_is_classid;
    pds->direction         = pvlan->dir;

    if(pds->is_label || pvlan->gport_is_logic)
    {
        gport = pvlan->gport;
    }
    else
    {
        gport = SYS_SCL_GPORT13_TO_GPORT14(pvlan->gport);
    }

    pds->global_src_port   = gport & 0x1fff;
    pds->global_src_port13 = gport >> 13;
    pds->svlan_id          = pvlan->vid;
    pds->ad_index          = index;
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_scl_build_hash_cvlan_cos(uint8 lchip, sys_scl_sw_key_t* psys, void* pu32, uint16 index)
{
    uint16                         gport   = 0;
    sys_scl_sw_hash_key_vtag_t     * pvtag = &(psys->u0.hash.u0.vtag);
    ds_user_id_cvlan_cos_hash_key_t* pds   = (ds_user_id_cvlan_cos_hash_key_t *) pu32;

    CTC_PTR_VALID_CHECK(pds);
    sal_memset(pds, 0, sizeof(*pds));
    pds->hash_type         = USERID_KEY_TYPE_CVLAN_COS;
    pds->valid             = 1;
    pds->is_label          = pvtag->gport_is_classid;
    pds->direction         = pvtag->dir;

    if(pds->is_label || pvtag->gport_is_logic)
    {
        gport = pvtag->gport;
    }
    else
    {
        gport = SYS_SCL_GPORT13_TO_GPORT14(pvtag->gport);
    }

    pds->global_src_port   = gport & 0x1fff;
    pds->global_src_port13 = gport >> 13;
    pds->cvlan_id0         = pvtag->cvid & 0x1;
    pds->cvlan_id11_1      = pvtag->cvid >> 1;
    pds->ctag_cos          = pvtag->ccos;

    pds->ad_index = index;
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_scl_build_hash_svlan_cos(uint8 lchip, sys_scl_sw_key_t* psys, void* pu32, uint16 index)
{
    uint16                         gport   = 0;
    sys_scl_sw_hash_key_vtag_t     * pvtag = &(psys->u0.hash.u0.vtag);
    ds_user_id_svlan_cos_hash_key_t* pds   = (ds_user_id_svlan_cos_hash_key_t *) pu32;

    CTC_PTR_VALID_CHECK(pds);
    sal_memset(pds, 0, sizeof(*pds));

    pds->hash_type         = USERID_KEY_TYPE_SVLAN_COS;
    pds->valid             = 1;
    pds->is_label          = pvtag->gport_is_classid;
    pds->direction         = pvtag->dir;

    if(pds->is_label || pvtag->gport_is_logic)
    {
        gport = pvtag->gport;
    }
    else
    {
        gport = SYS_SCL_GPORT13_TO_GPORT14(pvtag->gport);
    }

    pds->global_src_port   = gport & 0x1fff;
    pds->global_src_port13 = gport >> 13;
    pds->svlan_id          = pvtag->svid;
    pds->stag_cos          = pvtag->scos;

    pds->ad_index = index;
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_scl_build_hash_ipv6(uint8 lchip, sys_scl_sw_key_t* psys, void* pu32, uint16 index)
{
    sys_scl_sw_hash_key_ipv6_t* pipv6 = &(psys->u0.hash.u0.ipv6);
    ds_user_id_ipv6_hash_key_t* pds   = (ds_user_id_ipv6_hash_key_t *) pu32;

    CTC_PTR_VALID_CHECK(pds);
    sal_memset(pds, 0, sizeof(*pds));

    pds->ip_sa127_117 = (pipv6->ip_sa[0] >> 21) & 0x7FF;
    pds->ip_sa116_90  = ((pipv6->ip_sa[0] & 0x1FFFFF) << 6)
                        | ((pipv6->ip_sa[1] >> 26) & 0x3F);
    pds->ip_sa89_58 = ((pipv6->ip_sa[1] & 0x3FFFFFF) << 6)
                      | ((pipv6->ip_sa[2] >> 26) & 0x3F);
    pds->ip_sa57_46 = (pipv6->ip_sa[2] >> 14) & 0xFFF;
    pds->ip_sa45_44 = (pipv6->ip_sa[2] >> 12) & 0x3;
    pds->ip_sa43_33 = (pipv6->ip_sa[2] >> 1) & 0x7FF;
    pds->ip_sa32    = pipv6->ip_sa[2] & 0x1;
    pds->ip_sa31_0  = pipv6->ip_sa[3];
    pds->hash_type0 = USERID_KEY_TYPE_IPV6_SA;
    pds->hash_type1 = USERID_KEY_TYPE_IPV6_SA;
    pds->valid0     = 1;
    pds->valid1     = 1;
    pds->ad_index   = index;
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_scl_build_hash_mac(uint8 lchip, sys_scl_sw_key_t* psys, void* pu32, uint16 index)
{
    sys_scl_sw_hash_key_mac_t* pmac = &(psys->u0.hash.u0.mac);
    ds_user_id_mac_hash_key_t* pds  = (ds_user_id_mac_hash_key_t *) pu32;

    CTC_PTR_VALID_CHECK(pds);
    sal_memset(pds, 0, sizeof(*pds));

    pds->hash_type   = USERID_KEY_TYPE_MAC_SA;
    pds->valid       = 1;
    pds->mac_sa47_44 = (pmac->macsa_h >> 12) & 0xF;
    pds->mac_sa43    = (pmac->macsa_h >> 11) & 0x1;
    pds->mac_sa42_32 = pmac->macsa_h & 0x7FF;
    pds->mac_sa31_0  = pmac->macsa_l;
    pds->is_mac_da   = pmac->use_macda;

    pds->ad_index = index;
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_scl_build_hash_port_mac(uint8 lchip, sys_scl_sw_key_t* psys, void* pu32, uint16 index)
{
    uint16                        gport  = 0;
    sys_scl_sw_hash_key_mac_t     * pmac = &(psys->u0.hash.u0.mac);
    ds_user_id_mac_port_hash_key_t* pds  = (ds_user_id_mac_port_hash_key_t *) pu32;

    CTC_PTR_VALID_CHECK(pds);
    sal_memset(pds, 0, sizeof(*pds));

    pds->hash_type         = USERID_KEY_TYPE_PORT_MAC_SA;
    pds->hash_type1        = USERID_KEY_TYPE_PORT_MAC_SA;
    pds->valid             = 1;
    pds->valid1            = 1;
    pds->is_label          = pmac->gport_is_classid;
    gport                  = SYS_SCL_GPORT13_TO_GPORT14(pmac->gport);
    pds->global_src_port   = gport & 0x1fff;
    pds->global_src_port13 = gport >> 13;
    pds->is_mac_da         = pmac->use_macda;
    pds->mac_sa_high       = pmac->macsa_h;
    pds->mac_sa_low        = pmac->macsa_l;
    pds->ad_index          = index;
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_scl_build_hash_port_ipv4(uint8 lchip, sys_scl_sw_key_t* psys, void* pu32, uint16 index)
{
    uint16                         gport   = 0;
    sys_scl_sw_hash_key_ipv4_t     * pipv4 = &(psys->u0.hash.u0.ipv4);
    ds_user_id_ipv4_port_hash_key_t* pds   = (ds_user_id_ipv4_port_hash_key_t *) pu32;
    ds_user_id_port_cross_hash_key_t* pds_cross   = (ds_user_id_port_cross_hash_key_t *) pu32;

    if (pipv4->rsv0) /*xcoam*/
    {
        CTC_PTR_VALID_CHECK(pds_cross);
        sal_memset(pds_cross, 0, sizeof(*pds_cross));

        pds_cross->hash_type         = USERID_KEY_TYPE_PORT_IPV4_SA;
        pds_cross->valid             = 1;
        gport                  = SYS_SCL_GPORT13_TO_GPORT14(pipv4->gport);
        pds_cross->global_src_port  =  gport&0x1fff;
        pds_cross->global_src_port13 = (gport >> 13);

        gport                  = SYS_SCL_GPORT13_TO_GPORT14(pipv4->ip_sa);
        pds_cross->global_dest_port = gport;
        pds_cross->is_label          = pipv4->gport_is_classid;
        pds_cross->direction         = 1;
        pds_cross->ad_index          = index;
    }
    else
    {
        CTC_PTR_VALID_CHECK(pds);
        sal_memset(pds, 0, sizeof(*pds));

        pds->hash_type         = USERID_KEY_TYPE_PORT_IPV4_SA;
        pds->valid             = 1;
        gport                  = SYS_SCL_GPORT13_TO_GPORT14(pipv4->gport);
        pds->global_src_port0  = gport & 0x1;
        pds->global_src_port1  = (gport & 0x1fff) >> 1;
        pds->global_src_port13 = gport >> 13;
        pds->is_label          = pipv4->gport_is_classid;
        pds->ip_sa             = pipv4->ip_sa;
        pds->ad_index          = index;
    }
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_scl_build_hash_ipv4(uint8 lchip, sys_scl_sw_key_t* psys, void* pu32, uint16 index)
{
    sys_scl_sw_hash_key_ipv4_t* pipv4 = &(psys->u0.hash.u0.ipv4);
    ds_user_id_ipv4_hash_key_t* pds   = (ds_user_id_ipv4_hash_key_t *) pu32;

    CTC_PTR_VALID_CHECK(pds);
    sal_memset(pds, 0, sizeof(*pds));

    pds->hash_type = USERID_KEY_TYPE_IPV4_SA;
    pds->valid     = 1;
    pds->ip_sa     = pipv4->ip_sa;
    pds->ad_index  = index;
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_scl_build_hash_l2(uint8 lchip, sys_scl_sw_key_t* psys, void* pu32, uint16 index)
{
    uint16                  gport = 0;
    sys_scl_sw_hash_key_l2_t* pl2 = &(psys->u0.hash.u0.l2);
    ds_user_id_l2_hash_key_t* pds = (ds_user_id_l2_hash_key_t *) pu32;

    CTC_PTR_VALID_CHECK(pds);
    sal_memset(pds, 0, sizeof(*pds));

    pds->hash_type0           = USERID_KEY_TYPE_L2;
    pds->hash_type1           = USERID_KEY_TYPE_L2;
    pds->valid0               = 1;
    pds->valid1               = 1;
    gport                     = SYS_SCL_GPORT13_TO_GPORT14(pl2->gport);
    pds->global_src_port10_0  = gport & 0x7FF;
    pds->global_src_port13_11 = gport >> 11;
    pds->cos                  = pl2->cos;
    pds->mac_da31_0           = pl2->macda_l;
    pds->mac_da47_32          = pl2->macda_h;
    pds->mac_sa31_0           = pl2->macsa_l;
    pds->mac_sa40_32          = pl2->macsa_h & 0x1FF;
    pds->mac_sa47_41          = (pl2->macsa_h >> 9) & 0x7F;
    pds->vlan_id              = pl2->vid;
    pds->ad_index             = index;
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_scl_build_hash_tunnel_ipv4(uint8 lchip, sys_scl_sw_key_t* psys, void* pu32, uint16 index)
{
    sys_scl_hash_tunnel_ipv4_key_t* pt4 = &(psys->u0.hash.u0.tnnl_ipv4);
    ds_tunnel_id_ipv4_hash_key_t  * pds = (ds_tunnel_id_ipv4_hash_key_t *) pu32;

    CTC_PTR_VALID_CHECK(pds);
    sal_memset(pds, 0, sizeof(*pds));

    pds->hash_type0     = USERID_KEY_TYPE_IPV4_TUNNEL;
    pds->hash_type1     = USERID_KEY_TYPE_IPV4_TUNNEL;
    pds->valid0         = 1;
    pds->valid1         = 1;
    pds->udp_dest_port  = 0;
    pds->udp_src_port_l = 0;
    pds->ip_da          = pt4->ip_da;
    pds->udp_src_port_h = 0;
    pds->layer4_type    = pt4->l4_type;
    pds->ip_sa          = pt4->ip_sa;

    pds->ad_index = index;
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_scl_build_hash_tunnel_ipv4_gre(uint8 lchip, sys_scl_sw_key_t* psys, void* pu32, uint16 index)
{
    sys_scl_hash_tunnel_ipv4_gre_key_t* pgre = &(psys->u0.hash.u0.tnnl_ipv4_gre);
    ds_tunnel_id_ipv4_hash_key_t      * pds  = (ds_tunnel_id_ipv4_hash_key_t *) pu32;

    CTC_PTR_VALID_CHECK(pds);
    sal_memset(pds, 0, sizeof(*pds));

    pds->hash_type0     = USERID_KEY_TYPE_IPV4_GRE_KEY;
    pds->hash_type1     = USERID_KEY_TYPE_IPV4_GRE_KEY;
    pds->valid0         = 1;
    pds->valid1         = 1;
    pds->udp_dest_port  = ((pgre->gre_key >> 16) & 0xFFFF);
    pds->udp_src_port_l = (pgre->gre_key & 0x7FF);
    pds->ip_da          = pgre->ip_da;
    pds->udp_src_port_h = ((pgre->gre_key >> 11) & 0x1F);
    pds->layer4_type    = pgre->l4_type;
    pds->ip_sa          = pgre->ip_sa;

    pds->ad_index = index;
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_scl_build_hash_tunnel_ipv4_rpf(uint8 lchip, sys_scl_sw_key_t* psys, void* pu32, uint16 index)
{
    sys_scl_hash_tunnel_ipv4_rpf_key_t* prpf = &(psys->u0.hash.u0.tnnl_ipv4_rpf);
    ds_tunnel_id_ipv4_rpf_hash_key_t  * pds  = (ds_tunnel_id_ipv4_rpf_hash_key_t *) pu32;

    CTC_PTR_VALID_CHECK(pds);
    sal_memset(pds, 0, sizeof(*pds));

    pds->hash_type = USERID_KEY_TYPE_IPV4_RPF;
    pds->valid     = 1;
    pds->ip_sa     = prpf->ip_sa;

    pds->ad_index = index;
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_scl_build_tcam_vlan_key(uint8 lchip, drv_scl_group_info_t* p_info, tbl_entry_t* p_vlan, sys_scl_sw_key_t* p_key)
{
    uint32               flag     = 0;
    ds_user_id_vlan_key_t* p_data = NULL;
    ds_user_id_vlan_key_t* p_mask = NULL;

    CTC_PTR_VALID_CHECK(p_info);
    CTC_PTR_VALID_CHECK(p_vlan);
    CTC_PTR_VALID_CHECK(p_key);

    SYS_SCL_DBG_FUNC();

    p_data = (ds_user_id_vlan_key_t *) p_vlan->data_entry;
    p_mask = (ds_user_id_vlan_key_t *) p_vlan->mask_entry;
    flag   = p_key->u0.vlan_key.flag;

    if (CTC_SCL_GROUP_TYPE_PORT== p_info->group_type)
    {
        p_data->global_src_port = SYS_SCL_GPORT13_TO_GPORT14(p_info->gport);
        p_mask->global_src_port = 0x3FFF;

    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_VLAN_KEY_FLAG_SVLAN))
    {
        p_data->svlan_id = p_key->u0.vlan_key.svlan;
        p_mask->svlan_id = p_key->u0.vlan_key.svlan_mask;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_VLAN_KEY_FLAG_CVLAN))
    {
        p_data->customer_id |= p_key->u0.vlan_key.cvlan << 15;
        p_mask->customer_id |= p_key->u0.vlan_key.cvlan_mask << 15;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_VLAN_KEY_FLAG_CTAG_COS))
    {
        p_data->customer_id |= (p_key->u0.vlan_key.ctag_cos << 5);
        p_mask->customer_id |= (p_key->u0.vlan_key.ctag_cos_mask << 5);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_VLAN_KEY_FLAG_CTAG_CFI))
    {
        p_data->customer_id |= (p_key->u0.vlan_key.ctag_cfi << 4);
        p_mask->customer_id |= (0x1 << 4);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_VLAN_KEY_FLAG_STAG_COS))
    {
        p_data->customer_id |= (p_key->u0.vlan_key.stag_cos << 1);
        p_mask->customer_id |= (p_key->u0.vlan_key.stag_cos_mask << 1);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_VLAN_KEY_FLAG_STAG_CFI))
    {
        p_data->customer_id |= p_key->u0.vlan_key.stag_cfi;
        p_mask->customer_id |= 0x1;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_VLAN_KEY_FLAG_CTAG_VALID))
    {
        p_data->cvlan_id_valid = p_key->u0.vlan_key.ctag_valid;
        p_mask->cvlan_id_valid = 0x1;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_VLAN_KEY_FLAG_STAG_VALID))
    {
        p_data->svlan_id_valid = p_key->u0.vlan_key.stag_valid;
        p_mask->svlan_id_valid = 0x1;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_VLAN_KEY_FLAG_CUSTOMER_ID))
    {
        /* only care about high 20bit for PW label */
        p_data->customer_id = p_key->u0.vlan_key.customer_id;
        p_mask->customer_id = p_key->u0.vlan_key.customer_id_mask;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_VLAN_KEY_FLAG_GPORT))
    {
        p_data->global_src_port = SYS_SCL_GPORT13_TO_GPORT14(p_key->u0.vlan_key.gport);
        p_mask->global_src_port = p_key->u0.vlan_key.gport_mask;
    }

    p_data->user_id_label = p_info->class_id_data;
    p_mask->user_id_label = p_info->class_id_mask;

    p_data->is_acl_qos_key = 0;
    p_data->table_id       = USERID_TABLEID;
    p_data->sub_table_id   = USERID_VLAN_SUB_TABLEID;

    p_mask->is_acl_qos_key = 0x1;
    p_mask->table_id       = 0x7;
    p_mask->sub_table_id   = 0x3;
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_scl_build_tcam_mac_key(uint8 lchip, drv_scl_group_info_t* p_info, tbl_entry_t* p_mac, sys_scl_sw_key_t* p_key)
{
    uint32              flag;
    ds_user_id_mac_key_t* p_data = NULL;
    ds_user_id_mac_key_t* p_mask = NULL;

    CTC_PTR_VALID_CHECK(p_info);
    CTC_PTR_VALID_CHECK(p_mac);
    CTC_PTR_VALID_CHECK(p_key);

    SYS_SCL_DBG_FUNC();

    p_data = (ds_user_id_mac_key_t *) p_mac->data_entry;
    p_mask = (ds_user_id_mac_key_t *) p_mac->mask_entry;
    flag   = p_key->u0.mac_key.flag;

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_MAC_KEY_FLAG_MAC_DA))
    {
        SYS_SCL_SET_MAC_HIGH(p_data->mac_da47_32, p_key->u0.mac_key.mac_da);
        SYS_SCL_SET_MAC_LOW(p_data->mac_da31_0, p_key->u0.mac_key.mac_da);

        SYS_SCL_SET_MAC_HIGH(p_mask->mac_da47_32, p_key->u0.mac_key.mac_da_mask);
        SYS_SCL_SET_MAC_LOW(p_mask->mac_da31_0, p_key->u0.mac_key.mac_da_mask);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_MAC_KEY_FLAG_MAC_SA))
    {
        SYS_SCL_SET_MAC_HIGH(p_data->mac_sa47_32, p_key->u0.mac_key.mac_sa);
        SYS_SCL_SET_MAC_LOW(p_data->mac_sa31_0, p_key->u0.mac_key.mac_sa);

        SYS_SCL_SET_MAC_HIGH(p_mask->mac_sa47_32, p_key->u0.mac_key.mac_sa_mask);
        SYS_SCL_SET_MAC_LOW(p_mask->mac_sa31_0, p_key->u0.mac_key.mac_sa_mask);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_MAC_KEY_FLAG_L2_TYPE))
    {
        p_data->layer2_type = p_key->u0.mac_key.l2_type;
        p_mask->layer2_type = p_key->u0.mac_key.l2_type_mask;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_MAC_KEY_FLAG_L3_TYPE))
    {
        p_data->layer3_type = p_key->u0.mac_key.l3_type;
        p_mask->layer3_type = p_key->u0.mac_key.l3_type_mask;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_MAC_KEY_FLAG_CVLAN))
    {
        p_data->vlan_id2_hi  = (p_key->u0.mac_key.cvlan >> 8) & 0xF;
        p_data->vlan_id2_low = (p_key->u0.mac_key.cvlan & 0xFF);
        p_mask->vlan_id2_hi  = (p_key->u0.mac_key.cvlan_mask >> 8) & 0xF;
        p_mask->vlan_id2_low = (p_key->u0.mac_key.cvlan_mask & 0xFF);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_MAC_KEY_FLAG_SVLAN))
    {
        p_data->vlan_id1 = p_key->u0.mac_key.svlan;
        p_mask->vlan_id1 = p_key->u0.mac_key.svlan_mask;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_MAC_KEY_FLAG_STAG_COS))
    {
        p_data->cos1 = p_key->u0.mac_key.stag_cos;
        p_mask->cos1 = p_key->u0.mac_key.stag_cos_mask;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_MAC_KEY_FLAG_CTAG_COS))
    {
        p_data->cos2 = p_key->u0.mac_key.ctag_cos;
        p_mask->cos2 = p_key->u0.mac_key.ctag_cos_mask;
    }


    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_MAC_KEY_FLAG_STAG_CFI))
    {
        p_data->cfi1 = p_key->u0.mac_key.stag_cfi;
        p_mask->cfi1 = 0x1;
    }

    p_data->user_id_label = p_info->class_id_data;
    p_mask->user_id_label = p_info->class_id_mask;

    p_data->is_acl_qos_key0 = 0;
    p_data->is_acl_qos_key1 = 0;
    p_data->table_id0       = USERID_TABLEID;
    p_data->table_id1       = USERID_TABLEID;
    p_data->sub_table_id0   = USERID_MAC_SUB_TABLEID;
    p_data->sub_table_id1   = USERID_MAC_SUB_TABLEID;

    p_mask->is_acl_qos_key0 = 0x1;
    p_mask->is_acl_qos_key1 = 0x1;
    p_mask->table_id0       = 0x7;
    p_mask->table_id1       = 0x7;
    p_mask->sub_table_id0   = 0x3;
    p_mask->sub_table_id1   = 0x3;

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_scl_build_tcam_ipv4_key(uint8 lchip, drv_scl_group_info_t* p_info, tbl_entry_t* p_ipv4, sys_scl_sw_key_t* p_key)
{
    uint32               flag;
    uint32               sub_flag;
    ds_user_id_ipv4_key_t* p_data = NULL;
    ds_user_id_ipv4_key_t* p_mask = NULL;

    CTC_PTR_VALID_CHECK(p_info);
    CTC_PTR_VALID_CHECK(p_ipv4);
    CTC_PTR_VALID_CHECK(p_key);

    SYS_SCL_DBG_FUNC();

    p_data   = (ds_user_id_ipv4_key_t *) p_ipv4->data_entry;
    p_mask   = (ds_user_id_ipv4_key_t *) p_ipv4->mask_entry;
    flag     = p_key->u0.ipv4_key.flag;
    sub_flag = p_key->u0.ipv4_key.sub_flag;


    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_MAC_DA))
    {
        SYS_SCL_SET_MAC_HIGH(p_data->mac_da_hi, p_key->u0.ipv4_key.mac_da);
        SYS_SCL_SET_MAC_LOW(p_data->mac_da_low, p_key->u0.ipv4_key.mac_da);

        SYS_SCL_SET_MAC_HIGH(p_mask->mac_da_hi, p_key->u0.ipv4_key.mac_da_mask);
        SYS_SCL_SET_MAC_LOW(p_mask->mac_da_low, p_key->u0.ipv4_key.mac_da_mask);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_MAC_SA))
    {
        SYS_SCL_SET_MAC_HIGH(p_data->mac_sa_hi, p_key->u0.ipv4_key.mac_sa);
        SYS_SCL_SET_MAC_LOW(p_data->mac_sa_low, p_key->u0.ipv4_key.mac_sa);

        SYS_SCL_SET_MAC_HIGH(p_mask->mac_sa_hi, p_key->u0.ipv4_key.mac_sa_mask);
        SYS_SCL_SET_MAC_LOW(p_mask->mac_sa_low, p_key->u0.ipv4_key.mac_sa_mask);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_IP_DA))
    {
        p_data->ip_da = p_key->u0.ipv4_key.ip_da;
        p_mask->ip_da = p_key->u0.ipv4_key.ip_da_mask;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_IP_SA))
    {
        p_data->ip_sa = p_key->u0.ipv4_key.ip_sa;
        p_mask->ip_sa = p_key->u0.ipv4_key.ip_sa_mask;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_ETH_TYPE))
    {
        p_data->ether_type_hi  = p_key->u0.ipv4_key.eth_type >> 8;
        p_data->ether_type_low = p_key->u0.ipv4_key.eth_type & 0xFF;

        p_mask->ether_type_hi  = p_key->u0.ipv4_key.eth_type_mask >> 8;
        p_mask->ether_type_low = p_key->u0.ipv4_key.eth_type_mask & 0xFF;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_L2_TYPE))
    {
        p_data->layer2_type = p_key->u0.ipv4_key.l2_type;
        p_mask->layer2_type = p_key->u0.ipv4_key.l2_type_mask;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_CVLAN))
    {
        p_data->cvlan_id = p_key->u0.ipv4_key.cvlan;
        p_mask->cvlan_id = p_key->u0.ipv4_key.cvlan_mask;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_CTAG_COS))
    {
        p_data->ctag_cos = p_key->u0.ipv4_key.ctag_cos;
        p_mask->ctag_cos = p_key->u0.ipv4_key.ctag_cos_mask;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_CTAG_CFI))
    {
        p_data->ctag_cfi = p_key->u0.ipv4_key.ctag_cfi;
        p_mask->ctag_cfi = 0x1;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_SVLAN))
    {
        p_data->svlan_id = p_key->u0.ipv4_key.svlan;
        p_mask->svlan_id = p_key->u0.ipv4_key.svlan_mask;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_STAG_COS))
    {
        p_data->stag_cos = p_key->u0.ipv4_key.stag_cos;
        p_mask->stag_cos = p_key->u0.ipv4_key.stag_cos_mask;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_STAG_CFI))
    {
        p_data->stag_cfi = p_key->u0.ipv4_key.stag_cfi;
        p_mask->stag_cfi = 0x1;
    }

    if ((CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_DSCP))
        || (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_PRECEDENCE)))
    {
        p_data->dscp = p_key->u0.ipv4_key.dscp;
        p_mask->dscp = p_key->u0.ipv4_key.dscp_mask;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_IP_FRAG))
    {
        p_data->frag_info = p_key->u0.ipv4_key.frag_info;
        p_mask->frag_info = p_key->u0.ipv4_key.frag_info_mask;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_IP_OPTION))
    {
        p_data->ip_options = p_key->u0.ipv4_key.ip_option;
        p_mask->ip_options = 0x1;
    }

    /*
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_IP_HDR_ERR))
    {
        p_data->ip_header_error = p_key->u0.ipv4_key.ip_hdr_err;
        p_mask->ip_header_error = 0x1;
    }
    */

    /*ipv4key.routed_packet always = 0*/

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_L3_TYPE))
    {
        p_data->layer3_type = p_key->u0.ipv4_key.l3_type;
        p_mask->layer3_type = p_key->u0.ipv4_key.l3_type_mask;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_L4_TYPE))
    {
        p_data->layer4_type = p_key->u0.ipv4_key.l4_type;
        p_mask->layer4_type = p_key->u0.ipv4_key.l4_type_mask;
    }

    if (p_key->u0.ipv4_key.flag_is_tcp)
    {
        p_data->is_tcp = p_key->u0.ipv4_key.is_tcp;
        p_mask->is_tcp = 0x1;
    }

    if (p_key->u0.ipv4_key.flag_is_udp)
    {
        p_data->is_udp = p_key->u0.ipv4_key.is_udp;
        p_mask->is_udp = 0x1;
    }

    if (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_L4_SRC_PORT))
    {
        p_data->l4_source_port = p_key->u0.ipv4_key.l4_src_port;
        p_mask->l4_source_port = p_key->u0.ipv4_key.l4_src_port_mask;
    }

    if (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_L4_DST_PORT))
    {
        p_data->l4_dest_port = p_key->u0.ipv4_key.l4_dst_port;
        p_mask->l4_dest_port = p_key->u0.ipv4_key.l4_dst_port_mask;
    }

    /*this field's mask TBD*/
    if (p_key->u0.ipv4_key.flag_l4info_mapped)
    {
        p_data->l4_info_mapped = p_key->u0.ipv4_key.l4info_mapped;
        p_mask->l4_info_mapped = p_key->u0.ipv4_key.l4info_mapped_mask;
    }

    p_data->user_id_label = p_info->class_id_data;
    p_mask->user_id_label = p_info->class_id_mask;

    p_data->is_acl_qos_key0 = 0;
    p_data->is_acl_qos_key1 = 0;
    p_data->is_acl_qos_key2 = 0;
    p_data->is_acl_qos_key3 = 0;
    p_data->table_id0       = USERID_TABLEID;
    p_data->table_id1       = USERID_TABLEID;
    p_data->table_id2       = USERID_TABLEID;
    p_data->table_id3       = USERID_TABLEID;
    p_data->sub_table_id0   = USERID_IPV4_SUB_TABLEID;
    p_data->sub_table_id1   = USERID_IPV4_SUB_TABLEID;
    p_data->sub_table_id2   = USERID_IPV4_SUB_TABLEID;
    p_data->sub_table_id3   = USERID_IPV4_SUB_TABLEID;

    p_mask->is_acl_qos_key0 = 0x1;
    p_mask->is_acl_qos_key1 = 0x1;
    p_mask->is_acl_qos_key2 = 0x1;
    p_mask->is_acl_qos_key3 = 0x1;
    p_mask->table_id0       = 0x7;
    p_mask->table_id1       = 0x7;
    p_mask->table_id2       = 0x7;
    p_mask->table_id3       = 0x7;
    p_mask->sub_table_id0   = 0x3;
    p_mask->sub_table_id1   = 0x3;
    p_mask->sub_table_id2   = 0x3;
    p_mask->sub_table_id3   = 0x3;

    return CTC_E_NONE;
}


STATIC int32
_sys_greatbelt_scl_build_tcam_ipv6_key(uint8 lchip, drv_scl_group_info_t* p_info, tbl_entry_t* p_ipv6, sys_scl_sw_key_t* p_key)
{
    uint32               flag;
    ds_user_id_ipv6_key_t* p_data = NULL;
    ds_user_id_ipv6_key_t* p_mask = NULL;

    CTC_PTR_VALID_CHECK(p_info);
    CTC_PTR_VALID_CHECK(p_ipv6);
    CTC_PTR_VALID_CHECK(p_key);

    SYS_SCL_DBG_FUNC();

    p_data = (ds_user_id_ipv6_key_t *) p_ipv6->data_entry;
    p_mask = (ds_user_id_ipv6_key_t *) p_ipv6->mask_entry;
    flag   = p_key->u0.ipv6_key.flag;


    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_MAC_DA))
    {
        SYS_SCL_SET_MAC_HIGH(p_data->mac_da47_32, p_key->u0.ipv6_key.mac_da);
        SYS_SCL_SET_MAC_LOW(p_data->mac_da31_0, p_key->u0.ipv6_key.mac_da);

        SYS_SCL_SET_MAC_HIGH(p_mask->mac_da47_32, p_key->u0.ipv6_key.mac_da_mask);
        SYS_SCL_SET_MAC_LOW(p_mask->mac_da31_0, p_key->u0.ipv6_key.mac_da_mask);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_MAC_SA))
    {
        SYS_SCL_SET_MAC_HIGH(p_data->mac_sa47_32, p_key->u0.ipv6_key.mac_sa);
        SYS_SCL_SET_MAC_LOW(p_data->mac_sa31_0, p_key->u0.ipv6_key.mac_sa);

        SYS_SCL_SET_MAC_HIGH(p_mask->mac_sa47_32, p_key->u0.ipv6_key.mac_sa_mask);
        SYS_SCL_SET_MAC_LOW(p_mask->mac_sa31_0, p_key->u0.ipv6_key.mac_sa_mask);
    }


    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_ETH_TYPE))
    {
        p_data->ether_type15_8 = p_key->u0.ipv6_key.eth_type >> 8;
        p_data->ether_type7_0  = p_key->u0.ipv6_key.eth_type & 0xFF;
        p_mask->ether_type15_8 = p_key->u0.ipv6_key.eth_type_mask >> 8;
        p_mask->ether_type7_0  = p_key->u0.ipv6_key.eth_type_mask & 0xFF;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_IP_DA))
    {
        p_data->ip_da31_0 = p_key->u0.ipv6_key.ip_da[3];
        p_mask->ip_da31_0 = p_key->u0.ipv6_key.ip_da_mask[3];

        p_data->ip_da63_32 = p_key->u0.ipv6_key.ip_da[2];
        p_mask->ip_da63_32 = p_key->u0.ipv6_key.ip_da_mask[2];

        p_data->ip_da71_64 = (p_key->u0.ipv6_key.ip_da[1]) & 0xFF;
        p_mask->ip_da71_64 = (p_key->u0.ipv6_key.ip_da_mask[1]) & 0xFF;

        p_data->ip_da103_72 = (p_key->u0.ipv6_key.ip_da[1] >> 8)
                              | ((p_key->u0.ipv6_key.ip_da[0] & 0xFF) << 24);
        p_mask->ip_da103_72 = (p_key->u0.ipv6_key.ip_da_mask[1] >> 8)
                              | ((p_key->u0.ipv6_key.ip_da_mask[0] & 0xFF) << 24);

        p_data->ip_da127_104 = (p_key->u0.ipv6_key.ip_da[0] >> 8);
        p_mask->ip_da127_104 = (p_key->u0.ipv6_key.ip_da_mask[0] >> 8);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_IP_SA))
    {
        p_data->ip_sa31_0 = p_key->u0.ipv6_key.ip_sa[3];
        p_mask->ip_sa31_0 = p_key->u0.ipv6_key.ip_sa_mask[3];

        p_data->ip_sa63_32 = p_key->u0.ipv6_key.ip_sa[2];
        p_mask->ip_sa63_32 = p_key->u0.ipv6_key.ip_sa_mask[2];

        p_data->ip_sa71_64 = (p_key->u0.ipv6_key.ip_sa[1]) & 0xFF;
        p_mask->ip_sa71_64 = (p_key->u0.ipv6_key.ip_sa_mask[1]) & 0xFF;

        p_data->ip_sa103_72 = (p_key->u0.ipv6_key.ip_sa[1] >> 8)
                              | ((p_key->u0.ipv6_key.ip_sa[0] & 0xFF) << 24);
        p_mask->ip_sa103_72 = (p_key->u0.ipv6_key.ip_sa_mask[1] >> 8)
                              | ((p_key->u0.ipv6_key.ip_sa_mask[0] & 0xFF) << 24);

        p_data->ip_sa127_104 = (p_key->u0.ipv6_key.ip_sa[0] >> 8);
        p_mask->ip_sa127_104 = (p_key->u0.ipv6_key.ip_sa_mask[0] >> 8);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_L2_TYPE))
    {
        p_data->layer2_type = p_key->u0.ipv6_key.l2_type;
        p_mask->layer2_type = p_key->u0.ipv6_key.l2_type_mask;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_CVLAN))
    {
        p_data->cvlan_id11_10 = p_key->u0.ipv6_key.cvlan >> 10;
        p_data->cvlan_id9_0   = p_key->u0.ipv6_key.cvlan & 0x3ff;
        p_mask->cvlan_id11_10 = p_key->u0.ipv6_key.cvlan_mask >> 10;
        p_mask->cvlan_id9_0   = p_key->u0.ipv6_key.cvlan_mask & 0x3ff;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_CTAG_COS))
    {
        p_data->ctag_cos = p_key->u0.ipv6_key.ctag_cos;
        p_mask->ctag_cos = p_key->u0.ipv6_key.ctag_cos_mask;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_CTAG_CFI))
    {
        p_data->ctag_cfi = p_key->u0.ipv6_key.ctag_cfi;
        p_mask->ctag_cfi = 0x1;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_SVLAN))
    {
        p_data->svlan_id = p_key->u0.ipv6_key.svlan;
        p_mask->svlan_id = p_key->u0.ipv6_key.svlan_mask;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_STAG_COS))
    {
        p_data->stag_cos = p_key->u0.ipv6_key.stag_cos;
        p_mask->stag_cos = p_key->u0.ipv6_key.stag_cos_mask;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_STAG_CFI))
    {
        p_data->stag_cfi = p_key->u0.ipv6_key.stag_cfi;
        p_mask->stag_cfi = 0x1;
    }

    if ((CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_DSCP))
        || (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_PRECEDENCE)))
    {
        p_data->dscp = p_key->u0.ipv6_key.dscp;
        p_mask->dscp = p_key->u0.ipv6_key.dscp_mask;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_IP_FRAG))
    {
        p_data->frag_info = p_key->u0.ipv6_key.frag_info;
        p_mask->frag_info = p_key->u0.ipv6_key.frag_info_mask;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_FLOW_LABEL))
    {
        p_data->ipv6_flow_label = p_key->u0.ipv6_key.flow_label;
        p_mask->ipv6_flow_label = p_key->u0.ipv6_key.flow_label_mask;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_IP_OPTION))
    {
        p_data->ip_options = p_key->u0.ipv6_key.ip_option;
        p_mask->ip_options = 0x1;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_L3_TYPE))
    {
        p_data->layer3_type = p_key->u0.ipv6_key.l3_type;
        p_mask->layer3_type = p_key->u0.ipv6_key.l3_type_mask;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_L4_TYPE))
    {
        p_data->layer4_type = p_key->u0.ipv6_key.l4_type;
        p_mask->layer4_type = p_key->u0.ipv6_key.l4_type_mask;
    }

    if (p_key->u0.ipv6_key.flag_is_tcp)
    {
        p_data->is_tcp = p_key->u0.ipv6_key.is_tcp;
        p_mask->is_tcp = 0x1;
    }

    if (p_key->u0.ipv6_key.flag_is_udp)
    {
        p_data->is_udp = p_key->u0.ipv6_key.is_udp;
        p_mask->is_udp = 0x1;
    }

    if (CTC_FLAG_ISSET(p_key->u0.ipv6_key.sub_flag, CTC_SCL_TCAM_IPV6_KEY_SUB_FLAG_L4_SRC_PORT))
    {
        p_data->l4_source_port = p_key->u0.ipv6_key.l4_src_port;
        p_mask->l4_source_port = p_key->u0.ipv6_key.l4_src_port_mask;
    }

    if (CTC_FLAG_ISSET(p_key->u0.ipv6_key.sub_flag, CTC_SCL_TCAM_IPV6_KEY_SUB_FLAG_L4_DST_PORT))
    {
        p_data->l4_dest_port = p_key->u0.ipv6_key.l4_dst_port;
        p_mask->l4_dest_port = p_key->u0.ipv6_key.l4_dst_port_mask;
    }

    if (p_key->u0.ipv6_key.flag_l4info_mapped)
    {
        p_data->l4_info_mapped = p_key->u0.ipv6_key.l4info_mapped;
        p_mask->l4_info_mapped = p_key->u0.ipv6_key.l4info_mapped_mask;
    }

    p_data->user_id_label = p_info->class_id_data;
    p_mask->user_id_label = p_info->class_id_mask;

    p_data->is_acl_qos_key0 = 0;
    p_data->is_acl_qos_key1 = 0;
    p_data->is_acl_qos_key2 = 0;
    p_data->is_acl_qos_key3 = 0;
    p_data->is_acl_qos_key4 = 0;
    p_data->is_acl_qos_key5 = 0;
    p_data->is_acl_qos_key6 = 0;
    p_data->is_acl_qos_key7 = 0;
    p_data->table_id0       = USERID_TABLEID;
    p_data->table_id1       = USERID_TABLEID;
    p_data->table_id2       = USERID_TABLEID;
    p_data->table_id3       = USERID_TABLEID;
    p_data->table_id4       = USERID_TABLEID;
    p_data->table_id5       = USERID_TABLEID;
    p_data->table_id6       = USERID_TABLEID;
    p_data->table_id7       = USERID_TABLEID;
    p_data->sub_table_id0   = USERID_IPV6_SUB_TABLEID;
    p_data->sub_table_id1   = USERID_IPV6_SUB_TABLEID;
    p_data->sub_table_id2   = USERID_IPV6_SUB_TABLEID;
    p_data->sub_table_id3   = USERID_IPV6_SUB_TABLEID;
    p_data->sub_table_id4   = USERID_IPV6_SUB_TABLEID;
    p_data->sub_table_id5   = USERID_IPV6_SUB_TABLEID;
    p_data->sub_table_id6   = USERID_IPV6_SUB_TABLEID;
    p_data->sub_table_id7   = USERID_IPV6_SUB_TABLEID;

    p_mask->is_acl_qos_key0 = 0x1;
    p_mask->is_acl_qos_key1 = 0x1;
    p_mask->is_acl_qos_key2 = 0x1;
    p_mask->is_acl_qos_key3 = 0x1;
    p_mask->is_acl_qos_key4 = 0x1;
    p_mask->is_acl_qos_key5 = 0x1;
    p_mask->is_acl_qos_key6 = 0x1;
    p_mask->is_acl_qos_key7 = 0x1;
    p_mask->table_id0       = 0x7;
    p_mask->table_id1       = 0x7;
    p_mask->table_id2       = 0x7;
    p_mask->table_id3       = 0x7;
    p_mask->table_id4       = 0x7;
    p_mask->table_id5       = 0x7;
    p_mask->table_id6       = 0x7;
    p_mask->table_id7       = 0x7;
    p_mask->sub_table_id0   = 0x3;
    p_mask->sub_table_id1   = 0x3;
    p_mask->sub_table_id2   = 0x3;
    p_mask->sub_table_id3   = 0x3;
    p_mask->sub_table_id4   = 0x3;
    p_mask->sub_table_id5   = 0x3;
    p_mask->sub_table_id6   = 0x3;
    p_mask->sub_table_id7   = 0x3;

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_scl_build_tcam_tunnel_ipv4(uint8 lchip, tbl_entry_t* p_tipv4, sys_scl_sw_key_t* p_key_in)
{
    ds_tunnel_id_ipv4_key_t* p_data = NULL;
    ds_tunnel_id_ipv4_key_t* p_mask = NULL;

    CTC_DEBUG_ADD_STUB_CTC(NULL);
    CTC_PTR_VALID_CHECK(p_tipv4);
    CTC_PTR_VALID_CHECK(p_key_in);

    SYS_SCL_DBG_FUNC();

    p_data = (ds_tunnel_id_ipv4_key_t *) p_tipv4->data_entry;
    p_mask = (ds_tunnel_id_ipv4_key_t *) p_tipv4->mask_entry;

    p_data->is_acl_qos_key0 = FALSE;
    p_data->is_acl_qos_key1 = FALSE;
    p_data->table_id0       = TUNNELID_TABLEID;
    p_data->table_id1       = TUNNELID_TABLEID;
    p_data->sub_table_id0   = TUNNELID_IPV4_SUB_TABLEID;
    p_data->sub_table_id1   = TUNNELID_IPV4_SUB_TABLEID;

    p_mask->is_acl_qos_key0 = 0x1;
    p_mask->is_acl_qos_key1 = 0x1;
    p_mask->table_id0       = 0x7;
    p_mask->table_id1       = 0x7;
    p_mask->sub_table_id0   = 0x3;
    p_mask->sub_table_id1   = 0x3;

    p_data->ip_da       = p_key_in->u0.tnnl_ipv4_key.ipv4da;
    p_data->layer4_type = p_key_in->u0.tnnl_ipv4_key.l4type;

    p_mask->ip_da       = 0xFFFFFFFF;
    p_mask->layer4_type = 0xF;

    if (CTC_FLAG_ISSET(p_key_in->u0.tnnl_ipv4_key.flag, SYS_SCL_TUNNEL_IPSA_VALID))
    {
        p_data->ip_sa = p_key_in->u0.tnnl_ipv4_key.ipv4sa;
        p_mask->ip_sa = 0xFFFFFFFF;
    }

    if (p_key_in->u0.tnnl_ipv4_key.l4type == CTC_PARSER_L4_TYPE_GRE)
    {
        p_data->gre_key = CTC_FLAG_ISSET(p_key_in->u0.tnnl_ipv4_key.flag, SYS_SCL_TUNNEL_GRE_KEY_VALID) ?
                          p_key_in->u0.tnnl_ipv4_key.gre_key : 0;
        p_mask->gre_key = 0xFFFFFFFF;
    }

    p_data->l4_source_port = 0;
    p_data->l4_dest_port   = 0;
    p_data->frag_info      = 0;
    p_mask->frag_info      = 0x3;  /* won't decap fragment packet */

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_scl_build_tcam_tunnel_ipv6(uint8 lchip, tbl_entry_t* p_tipv6, sys_scl_sw_key_t* p_key_in)
{
    ds_tunnel_id_ipv6_key_t* p_data = NULL;
    ds_tunnel_id_ipv6_key_t* p_mask = NULL;

    CTC_DEBUG_ADD_STUB_CTC(NULL);
    CTC_PTR_VALID_CHECK(p_tipv6);
    CTC_PTR_VALID_CHECK(p_key_in);

    SYS_SCL_DBG_FUNC();

    p_data = (ds_tunnel_id_ipv6_key_t *) p_tipv6->data_entry;
    p_mask = (ds_tunnel_id_ipv6_key_t *) p_tipv6->mask_entry;

    p_data->is_acl_qos_key0 = FALSE;
    p_data->is_acl_qos_key1 = FALSE;
    p_data->is_acl_qos_key2 = FALSE;
    p_data->is_acl_qos_key3 = FALSE;
    p_data->is_acl_qos_key4 = FALSE;
    p_data->is_acl_qos_key5 = FALSE;
    p_data->is_acl_qos_key6 = FALSE;
    p_data->is_acl_qos_key7 = FALSE;
    p_data->table_id0       = TUNNELID_TABLEID;
    p_data->table_id1       = TUNNELID_TABLEID;
    p_data->table_id2       = TUNNELID_TABLEID;
    p_data->table_id3       = TUNNELID_TABLEID;
    p_data->table_id4       = TUNNELID_TABLEID;
    p_data->table_id5       = TUNNELID_TABLEID;
    p_data->table_id6       = TUNNELID_TABLEID;
    p_data->table_id7       = TUNNELID_TABLEID;
    p_data->sub_table_id0   = TUNNELID_IPV6_SUB_TABLEID;
    p_data->sub_table_id1   = TUNNELID_IPV6_SUB_TABLEID;
    p_data->sub_table_id2   = TUNNELID_IPV6_SUB_TABLEID;
    p_data->sub_table_id3   = TUNNELID_IPV6_SUB_TABLEID;
    p_data->sub_table_id4   = TUNNELID_IPV6_SUB_TABLEID;
    p_data->sub_table_id5   = TUNNELID_IPV6_SUB_TABLEID;
    p_data->sub_table_id6   = TUNNELID_IPV6_SUB_TABLEID;
    p_data->sub_table_id7   = TUNNELID_IPV6_SUB_TABLEID;

    p_mask->is_acl_qos_key0 = 0x1;
    p_mask->is_acl_qos_key1 = 0x1;
    p_mask->is_acl_qos_key2 = 0x1;
    p_mask->is_acl_qos_key3 = 0x1;
    p_mask->is_acl_qos_key4 = 0x1;
    p_mask->is_acl_qos_key5 = 0x1;
    p_mask->is_acl_qos_key6 = 0x1;
    p_mask->is_acl_qos_key7 = 0x1;
    p_mask->table_id0       = 0x7;
    p_mask->table_id1       = 0x7;
    p_mask->table_id2       = 0x7;
    p_mask->table_id3       = 0x7;
    p_mask->table_id4       = 0x7;
    p_mask->table_id5       = 0x7;
    p_mask->table_id6       = 0x7;
    p_mask->table_id7       = 0x7;
    p_mask->sub_table_id0   = 0x3;
    p_mask->sub_table_id1   = 0x3;
    p_mask->sub_table_id2   = 0x3;
    p_mask->sub_table_id3   = 0x3;
    p_mask->sub_table_id4   = 0x3;
    p_mask->sub_table_id5   = 0x3;
    p_mask->sub_table_id6   = 0x3;
    p_mask->sub_table_id7   = 0x3;

    if (CTC_FLAG_ISSET(p_key_in->u0.tnnl_ipv6_key.flag, SYS_SCL_TUNNEL_IPSA_VALID))
    {
        p_data->ip_sa31_0   = p_key_in->u0.tnnl_ipv6_key.ipv6_sa[0];
        p_data->ip_sa63_32  = p_key_in->u0.tnnl_ipv6_key.ipv6_sa[1];
        p_data->ip_sa95_64  = p_key_in->u0.tnnl_ipv6_key.ipv6_sa[2];
        p_data->ip_sa127_96 = p_key_in->u0.tnnl_ipv6_key.ipv6_sa[3];
        p_mask->ip_sa31_0   = 0xFFFFFFFF;
        p_mask->ip_sa63_32  = 0xFFFFFFFF;
        p_mask->ip_sa95_64  = 0xFFFFFFFF;
        p_mask->ip_sa127_96 = 0xFFFFFFFF;
    }

    p_data->ip_da31_0   = p_key_in->u0.tnnl_ipv6_key.ipv6_da[0];
    p_data->ip_da63_32  = p_key_in->u0.tnnl_ipv6_key.ipv6_da[1];
    p_data->ip_da95_64  = p_key_in->u0.tnnl_ipv6_key.ipv6_da[2];
    p_data->ip_da127_96 = p_key_in->u0.tnnl_ipv6_key.ipv6_da[3];
    p_mask->ip_da31_0   = 0xFFFFFFFF;
    p_mask->ip_da63_32  = 0xFFFFFFFF;
    p_mask->ip_da95_64  = 0xFFFFFFFF;
    p_mask->ip_da127_96 = 0xFFFFFFFF;

    p_data->layer4_type = p_key_in->u0.tnnl_ipv6_key.l4type;
    p_mask->layer4_type = 0xF;

    p_data->user_id_label   = 0;
    p_data->vlan_ptr        = 0;
    p_data->frag_info       = 0;
    p_mask->frag_info       = 0x3;     /* won't decap fragment packet */
    p_data->ip_options      = 0;
    p_data->ip_header_error = 0;
    p_data->ipv6_flow_label = 0;
    p_data->l4_info_mapped  = 0;
    p_data->layer3_type     = 0;
    p_data->dscp            = 0;
    p_data->is_udp          = 0;
    p_data->is_tcp          = 0;
    p_data->mac_sa47_32     = 0;
    p_data->mac_sa31_0      = 0;
    p_data->layer2_type     = 0;
    p_data->mac_da47_32     = 0;
    p_data->mac_da31_0      = 0;
    p_data->ether_type      = 0;
    p_data->svlan_id        = 0;
    p_data->stag_cfi        = 0;
    p_data->stag_cos        = 0;
    p_data->cvlan_id        = 0;
    p_data->ctag_cos        = 0;
    p_data->ctag_cfi        = 0;

    if (CTC_FLAG_ISSET(p_key_in->u0.tnnl_ipv6_key.flag, SYS_SCL_TUNNEL_GRE_KEY_VALID))
    {
        p_data->l4_source_port = p_key_in->u0.tnnl_ipv6_key.gre_key & 0xFFFF;
        p_data->l4_des_port    = (p_key_in->u0.tnnl_ipv6_key.gre_key >> 16) & 0xFFFF;
        p_mask->l4_source_port = 0xFFFF;
        p_mask->l4_des_port    = 0xFFFF;
    }
    else
    {
        p_data->l4_source_port = 0;
        p_data->l4_des_port    = 0;
        p_mask->l4_source_port = 0xFFFF;
        p_mask->l4_des_port    = 0xFFFF;
    }

    return CTC_E_NONE;
}


STATIC int32
_sys_greatbelt_scl_vlan_tag_op_translate(uint8 lchip, uint8 op_in, uint8* op_out, uint8* mode_out)
{
    if (CTC_VLAN_TAG_OP_REP_OR_ADD == op_in)
    {
        *mode_out = 1;
    }
    else
    {
        *mode_out = 0;
    }

    switch (op_in)
    {
    case CTC_VLAN_TAG_OP_NONE:
    case CTC_VLAN_TAG_OP_VALID:
        *op_out = 0;
        break;
    case CTC_VLAN_TAG_OP_REP:
    case CTC_VLAN_TAG_OP_REP_OR_ADD:
        *op_out = 1;
        break;
    case CTC_VLAN_TAG_OP_ADD:
        *op_out = 2;
        break;
    case CTC_VLAN_TAG_OP_DEL:
        *op_out = 3;
        break;
    }
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_scl_build_egs_action(uint8 lchip, ds_vlan_xlate_t* p_ds, sys_scl_sw_egs_action_t* p_action)
{
    uint32 flag;
    CTC_PTR_VALID_CHECK(p_action);
    CTC_PTR_VALID_CHECK(p_ds);

    SYS_SCL_DBG_FUNC();

    flag = p_action->flag;


    if (CTC_FLAG_ISSET(flag, CTC_SCL_EGS_ACTION_FLAG_DISCARD))
    {
        p_ds->ds_fwd_ptr_valid = 1;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_EGS_ACTION_FLAG_STATS))
    {
        p_ds->stats_ptr = p_action->chip.stats_ptr;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_EGS_ACTION_FLAG_VLAN_EDIT))
    {
        uint8 op;
        uint8 mo;
        p_ds->user_svlan_id    = p_action->svid;
        p_ds->user_scos        = p_action->scos;
        p_ds->user_scfi        = p_action->scfi;
        p_ds->user_cvlan_id    = p_action->cvid;
        p_ds->user_ccos        = p_action->ccos;
        p_ds->user_ccfi        = p_action->ccfi;
        p_ds->s_vlan_id_action = p_action->svid_sl;
        p_ds->c_vlan_id_action = p_action->cvid_sl;
        p_ds->s_cos_action     = p_action->scos_sl;
        p_ds->c_cos_action     = p_action->ccos_sl;

        _sys_greatbelt_scl_vlan_tag_op_translate(lchip, p_action->stag_op, &op, &mo);
        p_ds->s_tag_action     = op;
        p_ds->stag_modify_mode = mo;
        _sys_greatbelt_scl_vlan_tag_op_translate(lchip, p_action->ctag_op, &op, &mo);
        p_ds->c_tag_action     = op;
        p_ds->ctag_modify_mode = mo;

        p_ds->c_cfi_action = p_action->ccfi_sl;
        p_ds->s_cfi_action = p_action->scfi_sl;

        p_ds->cvlan_xlate_valid = p_action->ctag_op ? 1 : 0;
        p_ds->svlan_xlate_valid = p_action->stag_op ? 1 : 0;
        p_ds->svlan_tpid_index_en = 1;
        p_ds->svlan_tpid_index = p_action->tpid_index;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_EGS_ACTION_FLAG_PRIORITY_AND_COLOR))
    {
        p_ds->user_priority_en = 1;
        p_ds->user_color    = p_action->color;
        p_ds->user_priority = p_action->priority;
    }


    if (p_action->block_pkt_type)
    {
        p_ds->vlan_xlate_port_isolate_en = 1;

        if (CTC_FLAG_ISSET(p_action->block_pkt_type, CTC_SCL_BLOCKING_UNKNOW_UCAST))
        {
            p_ds->discard_unknown_ucast = 1;
        }

        if (CTC_FLAG_ISSET(p_action->block_pkt_type, CTC_SCL_BLOCKING_UNKNOW_MCAST))
        {
            p_ds->discard_unknown_mcast = 1;
        }

        if (CTC_FLAG_ISSET(p_action->block_pkt_type, CTC_SCL_BLOCKING_KNOW_UCAST))
        {
            p_ds->discard_known_ucast = 1;
        }

        if (CTC_FLAG_ISSET(p_action->block_pkt_type, CTC_SCL_BLOCKING_KNOW_MCAST))
        {
            p_ds->discard_known_mcast = 1;
        }
        if (CTC_FLAG_ISSET(p_action->block_pkt_type, CTC_SCL_BLOCKING_BCAST))
        {
            p_ds->discard_bcast = 1;
        }

    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_scl_build_vlan_edit(uint8 lchip, ds_vlan_action_profile_t* p_ds_edit, sys_scl_sw_vlan_edit_t* p_vlan_edit)
{
    uint8 op;
    uint8 mo;

    _sys_greatbelt_scl_vlan_tag_op_translate(lchip, p_vlan_edit->stag_op, &op, &mo);
    p_ds_edit->s_tag_action     = op;
    p_ds_edit->stag_modify_mode = mo;
    _sys_greatbelt_scl_vlan_tag_op_translate(lchip, p_vlan_edit->ctag_op, &op, &mo);
    p_ds_edit->c_tag_action     = op;
    p_ds_edit->ctag_modify_mode = mo;

    p_ds_edit->s_vlan_id_action = p_vlan_edit->svid_sl;
    p_ds_edit->s_cos_action     = p_vlan_edit->scos_sl;
    p_ds_edit->s_cfi_action     = p_vlan_edit->scfi_sl;

    p_ds_edit->c_vlan_id_action = p_vlan_edit->cvid_sl;
    p_ds_edit->c_cos_action     = p_vlan_edit->ccos_sl;
    p_ds_edit->c_cfi_action     = p_vlan_edit->ccfi_sl;
    p_ds_edit->svlan_tpid_index_en = 1;
    p_ds_edit->svlan_tpid_index = p_vlan_edit->tpid_index;

    switch (p_vlan_edit->vlan_domain)
    {
    case CTC_SCL_VLAN_DOMAIN_SVLAN:
        p_ds_edit->outer_vlan_status = 2;
        break;
    case CTC_SCL_VLAN_DOMAIN_CVLAN:
        p_ds_edit->outer_vlan_status = 1;
        break;
    case CTC_SCL_VLAN_DOMAIN_UNCHANGE:
        p_ds_edit->outer_vlan_status = 0;
        break;
    default:
        break;
    }

    return CTC_E_NONE;
}




STATIC int32
_sys_greatbelt_scl_build_igs_action(uint8 lchip,
                                    ds_user_id_t* p_ds,
                                    ds_vlan_action_profile_t* p_ds_edit,
                                    sys_scl_sw_igs_action_t* p_action)
{
    uint32 flag;
    uint32 sub_flag;

    CTC_PTR_VALID_CHECK(p_action);
    CTC_PTR_VALID_CHECK(p_ds);

    SYS_SCL_DBG_FUNC();
    flag     = p_action->flag;
    sub_flag = p_action->sub_flag;


    if (CTC_FLAG_ISSET(flag, CTC_SCL_IGS_ACTION_FLAG_DISCARD))
    {
        p_ds->ds_fwd_ptr_valid = 1;
        p_ds->fid              = 0xFFFF;
    }


    if (CTC_FLAG_ISSET(flag, CTC_SCL_IGS_ACTION_FLAG_ETREE_LEAF))
    {
        p_ds->is_leaf = 1;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_IGS_ACTION_FLAG_STATS))
    {
        p_ds->stats_ptr = p_action->chip.stats_ptr;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_IGS_ACTION_FLAG_PRIORITY_AND_COLOR))
    {
        p_ds->user_color    = p_action->color;
        p_ds->user_priority = p_action->priority;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_IGS_ACTION_FLAG_COPY_TO_CPU))
    {
        p_ds->user_id_exception_en = 1;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_IGS_ACTION_FLAG_AGING))
    {
        p_ds->aging_valid = 1;
        p_ds->aging_index = SYS_AGING_TIMER_INDEX_SERVICE;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_IGS_ACTION_FLAG_FID)
        ||CTC_FLAG_ISSET(flag, CTC_SCL_IGS_ACTION_FLAG_VRFID))
    {
        p_ds->ds_fwd_ptr_valid = 0;
        p_ds->fid              = p_action->fid;
        p_ds->fid_type         = p_action->fid_type;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_IGS_ACTION_FLAG_VLAN_EDIT))
    {
        p_ds->user_ccfi              = p_action->ccfi;
        p_ds->user_ccos              = p_action->ccos;
        p_ds->user_cvlan_id          = p_action->cvid;
        p_ds->user_scfi              = p_action->scfi;
        p_ds->user_scos              = p_action->scos;
        p_ds->user_svlan_id          = p_action->svid;
        p_ds->vlan_action_profile_id = p_action->chip.profile_id;

        p_ds->svlan_tag_operation_valid = p_action->vlan_edit->stag_op ? 1 : 0;
        p_ds->cvlan_tag_operation_valid = p_action->vlan_edit->ctag_op ? 1 : 0;

        p_ds->vlan_action_profile_valid = p_action->vlan_action_profile_valid;

        _sys_greatbelt_scl_build_vlan_edit(lchip, p_ds_edit, p_action->vlan_edit);
    }

    {   /* binding_data */
        if (CTC_FLAG_ISSET(flag, CTC_SCL_IGS_ACTION_FLAG_USER_VLANPTR))
        {
            p_ds->user_vlan_ptr = p_action->user_vlan_ptr;

            if (0 == p_ds->user_vlan_ptr)
            {
                p_ds->user_vlan_ptr  = SYS_SCL_BY_PASS_VLAN_PTR;
            }
        }

        if (CTC_FLAG_ISSET(flag, CTC_SCL_IGS_ACTION_FLAG_VPLS))
        {
            p_ds->binding_en        = p_action->binding_en;
            p_ds->binding_mac_sa    = p_action->binding_mac_sa;
            p_ds->binding_data_low  = p_action->bind_l.data;
            p_ds->binding_data_high = p_action->bind_h.data;
            p_ds->user_vlan_ptr     = p_action->user_vlan_ptr;
        }

        if (CTC_FLAG_ISSET(flag, CTC_SCL_IGS_ACTION_FLAG_APS))
        {
            p_ds->binding_en        = p_action->binding_en;
            p_ds->binding_mac_sa    = p_action->binding_mac_sa;
            p_ds->binding_data_low  = p_action->bind_l.data;
            p_ds->binding_data_high = p_action->bind_h.data;
            p_ds->user_vlan_ptr     = p_action->user_vlan_ptr;
        }

        if (CTC_FLAG_ISSET(flag, CTC_SCL_IGS_ACTION_FLAG_IGMP_SNOOPING))
        {
            p_ds->binding_en        = p_action->binding_en;
            p_ds->binding_mac_sa    = p_action->binding_mac_sa;
            p_ds->binding_data_high = p_action->bind_h.data;
        }

        if (CTC_FLAG_ISSET(flag, CTC_SCL_IGS_ACTION_FLAG_BINDING_EN))
        {
            p_ds->binding_en        = p_action->binding_en;
            p_ds->binding_mac_sa    = p_action->binding_mac_sa;
            p_ds->binding_data_low  = p_action->bind_l.data;
            p_ds->binding_data_high = p_action->bind_h.data;
        }

        if (CTC_FLAG_ISSET(flag, CTC_SCL_IGS_ACTION_FLAG_REDIRECT))
        {
            p_ds->ds_fwd_ptr_valid = 1;
            p_ds->bypass_all       = p_action->bypass_all;
            p_ds->fid              = p_action->chip.offset;
            p_ds->user_vlan_ptr    = p_action->user_vlan_ptr;
            p_ds->deny_bridge      = 1;
            /* vsi learning disable*/
            p_ds->binding_data_low = p_action->bind_l.data;
        }

        if (CTC_FLAG_ISSET(flag, CTC_SCL_IGS_ACTION_FLAG_LOGIC_PORT))
        {
            p_ds->binding_en       = p_action->binding_en;
            p_ds->binding_mac_sa   = p_action->binding_mac_sa;
            p_ds->binding_data_low = p_action->bind_l.data;
        }

        if (CTC_FLAG_ISSET(sub_flag, CTC_SCL_IGS_ACTION_SUB_FLAG_SERVICE_ACL_EN))
        {
            p_ds->binding_en       = p_action->binding_en;
            p_ds->binding_mac_sa   = p_action->binding_mac_sa;
            p_ds->binding_data_low = p_action->bind_l.data;
        }

        /* must put this in the end. */
        if (p_action->service_policer_id)
        {
            p_ds->binding_en     = p_action->binding_en;
            p_ds->binding_mac_sa = p_action->binding_mac_sa;
            /* flow_policer_ptr at low 13bist*/
            p_ds->binding_data_low |= p_action->chip.service_policer_ptr & 0x1FFF;
            p_ds->binding_data_high = p_action->bind_h.data;
        }

        if (CTC_FLAG_ISSET(sub_flag, CTC_SCL_IGS_ACTION_SUB_FLAG_SERVICE_QUEUE_EN))
        {
            p_ds->src_queue_select = p_action->src_queue_select;
        }
    }



    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_scl_build_tunnel_action(uint8 lchip, ds_tunnel_id_t* p_ad, sys_scl_tunnel_action_t* p_action)
{
    CTC_PTR_VALID_CHECK(p_action);
    CTC_PTR_VALID_CHECK(p_ad);

    SYS_SCL_DBG_FUNC();

    p_ad->src_queue_select           = p_action->src_queue_select;
    p_ad->trill_bfd_echo_en          = p_action->trill_bfd_echo_en;
    p_ad->tunnel_id_exception_en     = p_action->tunnel_id_exception_en;
    p_ad->is_tunnel                  = p_action->is_tunnel;
    p_ad->binding_mac_sa             = p_action->binding_mac_sa;
    p_ad->binding_en                 = p_action->binding_en;
    p_ad->isid_valid                 = p_action->isid_valid;
    p_ad->igmp_snoop_en              = p_action->igmp_snoop_en;
    p_ad->service_acl_qos_en         = p_action->service_acl_qos_en;
    p_ad->logic_port_type            = p_action->logic_port_type;
    p_ad->tunnel_rpf_check_request   = p_action->tunnel_rpf_check_request;
    p_ad->rpf_check_en               = p_action->rpf_check_en;
    p_ad->pip_bypass_learning        = p_action->pip_bypass_learning;
    p_ad->trill_bfd_en               = p_action->trill_bfd_en;
    p_ad->trill_ttl_check_en         = p_action->trill_ttl_check_en;
    p_ad->isatap_check_en            = p_action->isatap_check_en;
    p_ad->tunnel_gre_options         = p_action->tunnel_gre_options;
    p_ad->ttl_check_en               = p_action->ttl_check_en;
    p_ad->use_default_vlan_tag       = p_action->use_default_vlan_tag;
    p_ad->tunnel_payload_offset      = p_action->tunnel_payload_offset;
    p_ad->tunnel_packet_type         = p_action->tunnel_packet_type;
    p_ad->mac_security_vsi_discard   = p_action->mac_security_vsi_discard;
    p_ad->vsi_learning_disable       = p_action->vsi_learning_disable;
    p_ad->trill_option_escape_en     = p_action->trill_option_escape_en;
    p_ad->svlan_tpid_index           = p_action->svlan_tpid_index;
    p_ad->capwap_tunnel_type         = p_action->wlan_tunnel_type;
    p_ad->ds_fwd_ptr_valid           = p_action->ds_fwd_ptr_valid;
    p_ad->acl_qos_use_outer_info     = p_action->acl_qos_use_outer_info;
    p_ad->outer_vlan_is_cvlan        = p_action->outer_vlan_is_cvlan;
    p_ad->inner_packet_lookup        = p_action->inner_packet_lookup;
    p_ad->fid_type                   = p_action->fid_type;
    p_ad->deny_bridge                = p_action->deny_bridge;
    p_ad->deny_learning              = p_action->deny_learning;
    p_ad->aging_index                = p_action->aging_index;
    p_ad->aging_valid                = p_action->aging_valid;
    p_ad->pbb_mcast_decap            = p_action->pbb_mcast_decap;
    p_ad->tunnel_ptp_en              = p_action->tunnel_ptp_en;
    p_ad->stats_ptr                  = p_action->chip.stats_ptr;
    p_ad->pbb_outer_learning_enable  = p_action->pbb_outer_learning_enable;
    p_ad->pbb_outer_learning_disable = p_action->pbb_outer_learning_disable;
    p_ad->fid                        = p_action->fid;
    p_ad->route_disable              = p_action->route_disable;
    p_ad->esadi_check_en             = p_action->esadi_check_en;
    p_ad->exception_sub_index        = p_action->exception_sub_index;
    p_ad->trill_channel_en           = p_action->trill_channel_en;
    p_ad->trill_decap_without_loop   = p_action->trill_decap_without_loop;
    p_ad->trill_multi_rpf_check      = p_action->trill_multi_rpf_check;
    p_ad->user_default_cfi           = p_action->user_default_cfi;
    p_ad->user_default_cos           = p_action->user_default_cos;
    p_ad->ttl_update                 = p_action->ttl_update;
    p_ad->isid                       = p_action->isid;

    if (p_action->binding_en || p_action->rpf_check_en)
    {
        p_ad->binding_data_high = p_action->binding_data_high.binding_data;
        p_ad->binding_data_low  = p_action->binding_data_low.binding_data;
    }
    else
    {
        p_ad->binding_data_high = \
            (p_action->binding_data_high.aps_select_protecting_path << 15)
            | (p_action->binding_data_high.service_policer_valid << 14)
            | (p_action->binding_data_high.aps_select_valid << 13)
            | (p_action->binding_data_high.aps_select_group_id << 1)
            | (p_action->binding_data_high.service_policer_mode);

        /*binding data [31:0]*/
        p_ad->binding_data_low = \
            (p_action->binding_data_low.logic_src_port << 15)
            | (p_action->chip.policer_ptr);
    }

    return CTC_E_NONE;
}




STATIC int32
_sys_greatbelt_scl_convert_type(uint8 lchip, uint8 key_type, uint8 act_type, uint8* conv_type)
{
    if (act_type == SYS_SCL_ACTION_EGRESS)
    {
        switch (key_type)
        {
        case SYS_SCL_KEY_HASH_PORT:
        case SYS_SCL_KEY_HASH_PORT_CVLAN:
        case SYS_SCL_KEY_HASH_PORT_SVLAN:
        case SYS_SCL_KEY_HASH_PORT_2VLAN:
        case SYS_SCL_KEY_HASH_PORT_CVLAN_COS:
        case SYS_SCL_KEY_HASH_PORT_SVLAN_COS:
            *conv_type = SYS_SCL_CONV_SHORT;
            break;
        default:
            return CTC_E_UNEXPECT;
        }
    }
    else
    {
        return CTC_E_SCL_HASH_CONFLICT;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_scl_get_table_id(uint8 lchip, uint8 convert_type, uint8 key_type, uint8 act_type, uint32* key_id, uint32* act_id)
{
    CTC_PTR_VALID_CHECK(key_id);
    CTC_PTR_VALID_CHECK(act_id);

    *key_id = 0;
    *act_id = 0;
    switch (act_type)
    {
    case SYS_SCL_ACTION_INGRESS:
        *act_id = DsUserId_t;
        break;
    case SYS_SCL_ACTION_EGRESS:
        *act_id = DsVlanXlate_t;
        break;
    case SYS_SCL_ACTION_TUNNEL:
        *act_id = DsTunnelId_t;
        break;
    default:
        return CTC_E_INVALID_PARAM;
    }

    if (convert_type == SYS_SCL_CONV_SHORT)
    {
        *key_id = DsFibUserId80Key_t;
        *act_id = LpmTcamAdMem_t;
    }
    else if (convert_type == SYS_SCL_CONV_LONG)
    {
        *key_id = DsFibUserId160Key_t;
        *act_id = LpmTcamAdMem_t;
    }
    else if (convert_type == SYS_SCL_CONV_NONE)
    {
        switch (key_type)
        {
        case SYS_SCL_KEY_HASH_PORT_2VLAN:
            *key_id = DsUserIdDoubleVlanHashKey_t;
            break;

        case SYS_SCL_KEY_HASH_PORT:
            *key_id = DsUserIdPortHashKey_t;
            break;

        case SYS_SCL_KEY_HASH_PORT_CVLAN:
            *key_id = DsUserIdCvlanHashKey_t;
            break;

        case SYS_SCL_KEY_HASH_PORT_SVLAN:
            *key_id = DsUserIdSvlanHashKey_t;
            break;

        case SYS_SCL_KEY_HASH_PORT_CVLAN_COS:
            *key_id = DsUserIdCvlanCosHashKey_t;
            break;

        case SYS_SCL_KEY_HASH_PORT_SVLAN_COS:
            *key_id = DsUserIdSvlanCosHashKey_t;
            break;

        case SYS_SCL_KEY_HASH_MAC:
            *key_id = DsUserIdMacHashKey_t;
            break;

        case SYS_SCL_KEY_HASH_PORT_MAC:
            *key_id = DsUserIdMacPortHashKey_t;
            break;

        case SYS_SCL_KEY_HASH_IPV4:
            *key_id = DsUserIdIpv4HashKey_t;
            break;

        case SYS_SCL_KEY_HASH_PORT_IPV4:
            *key_id = DsUserIdIpv4PortHashKey_t;
            break;

        case SYS_SCL_KEY_HASH_IPV6:
            *key_id = DsUserIdIpv6HashKey_t;
            break;

        case SYS_SCL_KEY_HASH_L2:
            *key_id = DsUserIdL2HashKey_t;
            break;

        case SYS_SCL_KEY_HASH_TUNNEL_IPV4:
        case SYS_SCL_KEY_HASH_TUNNEL_IPV4_GRE:
            *key_id = DsTunnelIdIpv4HashKey_t;
            break;

        case SYS_SCL_KEY_HASH_TUNNEL_IPV4_RPF:
            *key_id = DsTunnelIdIpv4RpfHashKey_t;
            break;
        case SYS_SCL_KEY_TCAM_MAC:
            *key_id = DsUserIdMacKey_t;
            *act_id = DsUserIdMacTcam_t;
            break;

        case SYS_SCL_KEY_TCAM_VLAN:
            *key_id = DsUserIdVlanKey_t;
            *act_id = DsUserIdVlanTcam_t;
            break;

        case SYS_SCL_KEY_TCAM_IPV4:
            *key_id = DsUserIdIpv4Key_t;
            *act_id = DsUserIdIpv4Tcam_t;
            break;

        case SYS_SCL_KEY_TCAM_IPV6:
            *key_id = DsUserIdIpv6Key_t;
            *act_id = DsUserIdIpv6Tcam_t;
            break;


        case SYS_SCL_KEY_TCAM_TUNNEL_IPV4:
            *key_id = DsTunnelIdIpv4Key_t;
            *act_id = DsTunnelIdIpv4Tcam_t;
            break;

        case SYS_SCL_KEY_TCAM_TUNNEL_IPV6:
            *key_id = DsTunnelIdIpv6Key_t;
            *act_id = DsTunnelIdIpv6Tcam_t;
            break;

        case SYS_SCL_KEY_PORT_DEFAULT_INGRESS:
            *act_id = DsUserIdIngressDefault_t;
            break;
        case SYS_SCL_KEY_PORT_DEFAULT_EGRESS:
            *act_id = DsUserIdEgressDefault_t;
            break;

        default:
            return CTC_E_INVALID_PARAM;
        }
    }
    else
    {
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}


STATIC int32
_sys_greatbelt_scl_add_hw(uint8 lchip, uint32 eid)
{
    uint32               key_index;
    uint32               ad_index;

    uint32               cmd_ds  = 0;
    uint32               cmd_key = 0;

    tbl_entry_t          tcam_key;

    uint32               key_id;
    uint32               act_id;
    uint8                conv_type;
    uint8                key_type;
    uint8                act_type;

    uint32               key[12]       = { 0 };
    uint32               ds[12]        = { 0 };
    ds_vlan_action_profile_t               ds_edit;
    uint32               tcam_data[24] = { 0 };
    uint32               tcam_mask[24] = { 0 };

    sys_scl_sw_group_t   * pg         = NULL;
    sys_scl_sw_entry_t   * pe         = NULL;
    sys_scl_sw_key_t     * pk         = NULL;
    sys_scl_sw_action_t  * pa         = NULL;
    void                 * p_key_void = key;
    drv_scl_group_info_t drv_info;
    sal_memset(&drv_info, 0, sizeof(drv_scl_group_info_t));
    sal_memset(&ds_edit, 0, sizeof(ds_vlan_action_profile_t));
    SYS_SCL_DBG_FUNC();

    tcam_key.data_entry = (uint32 *) &tcam_data;
    tcam_key.mask_entry = (uint32 *) &tcam_mask;

    /* get sys entry */
    _sys_greatbelt_scl_get_nodes_by_eid(lchip, eid, &pe, &pg, NULL);
    if(!pe)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }
    if(!pg)
    {
        return CTC_E_SCL_GROUP_UNEXIST;
    }
    conv_type = pe->key.conv_type;
    key_type  = pe->key.type;
    act_type  = pe->action->type;
    pk        = &pe->key;
    pa        = pe->action;

    /* get block_id, ad_index */
    key_index = pe->fpae.offset_a;

    /* build drv group_info */
    CTC_ERROR_RETURN(_sys_greatbelt_scl_build_ds_group_info(lchip, &(pg->group_info), &drv_info));

    if ((SCL_ENTRY_IS_TCAM(key_type)) || (SYS_SCL_IS_DEFAULT(key_type)) || SCL_ENTRY_IS_SMALL_TCAM(conv_type)) /* default index also get from key_index */
    {
        ad_index = key_index;
    }
    else
    {
        ad_index = pe->action->ad_index;
    }


    if (conv_type == SYS_SCL_CONV_SHORT)
    {
        CTC_ERROR_RETURN(_sys_greatbelt_scl_build_cap_short(lchip, &tcam_key, pk));
        p_key_void = &tcam_key;
    }
    else if (conv_type == SYS_SCL_CONV_LONG)
    {
        CTC_ERROR_RETURN(_sys_greatbelt_scl_build_cap_long(lchip, &tcam_key, pk));
        p_key_void = &tcam_key;
    }
    else    /* no convert */
    {
        switch (key_type)
        {
        case SYS_SCL_KEY_HASH_PORT_2VLAN:
            _sys_greatbelt_scl_build_hash_2vlan(lchip, pk, key, ad_index);
            break;

        case SYS_SCL_KEY_HASH_PORT:
            _sys_greatbelt_scl_build_hash_port(lchip, pk, key, ad_index);
            break;

        case SYS_SCL_KEY_HASH_PORT_CVLAN:
            _sys_greatbelt_scl_build_hash_cvlan(lchip, pk, key, ad_index);
            break;

        case SYS_SCL_KEY_HASH_PORT_SVLAN:
            _sys_greatbelt_scl_build_hash_svlan(lchip, pk, key, ad_index);
            break;

        case SYS_SCL_KEY_HASH_PORT_CVLAN_COS:
            _sys_greatbelt_scl_build_hash_cvlan_cos(lchip, pk, key, ad_index);
            break;

        case SYS_SCL_KEY_HASH_PORT_SVLAN_COS:
            _sys_greatbelt_scl_build_hash_svlan_cos(lchip, pk, key, ad_index);
            break;

        case SYS_SCL_KEY_HASH_MAC:
            _sys_greatbelt_scl_build_hash_mac(lchip, pk, key, ad_index);
            break;

        case SYS_SCL_KEY_HASH_PORT_MAC:
            _sys_greatbelt_scl_build_hash_port_mac(lchip, pk, key, ad_index);
            break;

        case SYS_SCL_KEY_HASH_IPV4:
            _sys_greatbelt_scl_build_hash_ipv4(lchip, pk, key, ad_index);
            break;

        case SYS_SCL_KEY_HASH_PORT_IPV4:
            _sys_greatbelt_scl_build_hash_port_ipv4(lchip, pk, key, ad_index);
            break;

        case SYS_SCL_KEY_HASH_IPV6:
            _sys_greatbelt_scl_build_hash_ipv6(lchip, pk, key, ad_index);
            break;

        case SYS_SCL_KEY_HASH_L2:
            _sys_greatbelt_scl_build_hash_l2(lchip, pk, key, ad_index);
            break;

        case SYS_SCL_KEY_HASH_TUNNEL_IPV4:
            _sys_greatbelt_scl_build_hash_tunnel_ipv4(lchip, pk, key, ad_index);
            break;

        case SYS_SCL_KEY_HASH_TUNNEL_IPV4_GRE:
            _sys_greatbelt_scl_build_hash_tunnel_ipv4_gre(lchip, pk, key, ad_index);
            break;

        case SYS_SCL_KEY_HASH_TUNNEL_IPV4_RPF:
            _sys_greatbelt_scl_build_hash_tunnel_ipv4_rpf(lchip, pk, key, ad_index);
            break;

        case SYS_SCL_KEY_TCAM_MAC:
            CTC_ERROR_RETURN(_sys_greatbelt_scl_build_tcam_mac_key(lchip, &drv_info, &tcam_key, pk));
            p_key_void = &tcam_key;
            break;

        case SYS_SCL_KEY_TCAM_VLAN:
            CTC_ERROR_RETURN(_sys_greatbelt_scl_build_tcam_vlan_key(lchip, &drv_info, &tcam_key, pk));
            p_key_void = &tcam_key;
            break;

        case SYS_SCL_KEY_TCAM_IPV4:
            CTC_ERROR_RETURN(_sys_greatbelt_scl_build_tcam_ipv4_key(lchip, &drv_info, &tcam_key, pk));
            p_key_void = &tcam_key;
            break;

        case SYS_SCL_KEY_TCAM_IPV6:
            CTC_ERROR_RETURN(_sys_greatbelt_scl_build_tcam_ipv6_key(lchip, &drv_info, &tcam_key, pk));
            p_key_void = &tcam_key;
            break;

        case SYS_SCL_KEY_TCAM_TUNNEL_IPV4:
            CTC_ERROR_RETURN(_sys_greatbelt_scl_build_tcam_tunnel_ipv4(lchip, &tcam_key, pk));
            p_key_void = &tcam_key;
            break;

        case SYS_SCL_KEY_TCAM_TUNNEL_IPV6:
            CTC_ERROR_RETURN(_sys_greatbelt_scl_build_tcam_tunnel_ipv6(lchip, &tcam_key, pk));
            p_key_void = &tcam_key;
            break;
        case SYS_SCL_KEY_PORT_DEFAULT_EGRESS:
        case SYS_SCL_KEY_PORT_DEFAULT_INGRESS:
            break;
        default:
            return CTC_E_INVALID_PARAM;
        }
    }



    switch (act_type)
    {
    case SYS_SCL_ACTION_INGRESS:
        CTC_ERROR_RETURN(_sys_greatbelt_scl_build_igs_action
                             (lchip, (ds_user_id_t *) ds, (ds_vlan_action_profile_t *) &ds_edit, &pa->u0.ia));
        break;
    case SYS_SCL_ACTION_EGRESS:
        CTC_ERROR_RETURN(_sys_greatbelt_scl_build_egs_action
                             (lchip, (ds_vlan_xlate_t *) ds, &pa->u0.ea));
        break;
    case SYS_SCL_ACTION_TUNNEL:
        CTC_ERROR_RETURN(_sys_greatbelt_scl_build_tunnel_action
                             (lchip, (ds_tunnel_id_t *) ds, &pa->u0.ta));
        break;
    }

    if (CTC_FLAG_ISSET(pa->u0.ia.flag, CTC_SCL_IGS_ACTION_FLAG_VLAN_EDIT))
    {
        uint32 cmd_edit = DRV_IOW(DsVlanActionProfile_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, pa->u0.ia.chip.profile_id, cmd_edit, &ds_edit));
    }

    _sys_greatbelt_scl_get_table_id(lchip, conv_type, key_type, act_type, &key_id, &act_id);


    cmd_ds = DRV_IOW(act_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, ad_index, cmd_ds, ds));

    if ((SYS_SCL_KEY_PORT_DEFAULT_EGRESS != key_type)
        && (SYS_SCL_KEY_PORT_DEFAULT_INGRESS != key_type))
    {
        cmd_key = DRV_IOW(key_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, key_index, cmd_key, p_key_void));
    }

    {   /* set agint status */
        uint8 aging_type;

        if (conv_type)
        {
            aging_type = SYS_AGING_TCAM_256;
        }
        else if (SCL_ENTRY_IS_TCAM(key_type))
        {
            aging_type = SYS_AGING_TCAM_8k;
        }
        else
        {
            aging_type = SYS_AGING_USERID_HASH;
        }


        if (((SYS_SCL_ACTION_INGRESS == act_type) && CTC_FLAG_ISSET(pa->u0.ia.flag, CTC_SCL_IGS_ACTION_FLAG_AGING))
            || ((SYS_SCL_ACTION_EGRESS == act_type) && CTC_FLAG_ISSET(pa->u0.ea.flag, CTC_SCL_EGS_ACTION_FLAG_AGING)))
        {
            CTC_ERROR_RETURN(sys_greatbelt_aging_set_aging_status(lchip, aging_type,
                                                                  key_index, TRUE));
        }
        else
        {
            CTC_ERROR_RETURN(sys_greatbelt_aging_set_aging_status(lchip, aging_type,
                                                                  key_index, FALSE));
        }
    }




/*test delete at last*/
#if 0
    if (SYS_SCL_CONV_SHORT == pk->convert_type)
    {
        ds_vlan_xlate_t tmp;
        cmd_ds = DRV_IOR(LpmTcamAdMem_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, ad_index, cmd_ds, (void *) &tmp));

        SYS_SCL_DBG_DUMP("LpmTcamAdMem_t  KEY_INDEX    = %d: \n", key_index);
        SYS_SCL_DBG_DUMP("LpmTcamAdMem_t  AD_INDEX     = %d: \n", ad_index);

        SYS_SCL_DBG_DUMP("user_id_exception_en       = %d \n", tmp.user_id_exception_en);
        SYS_SCL_DBG_DUMP("ctag_modify_mode           = %d \n", tmp.ctag_modify_mode);
        SYS_SCL_DBG_DUMP("stag_modify_mode           = %d \n", tmp.stag_modify_mode);
        SYS_SCL_DBG_DUMP("ds_fwd_ptr_valid           = %d \n", tmp.ds_fwd_ptr_valid);
        SYS_SCL_DBG_DUMP("user_priority              = %d \n", tmp.user_priority);
        SYS_SCL_DBG_DUMP("user_priority_en           = %d \n", tmp.user_priority_en);
        SYS_SCL_DBG_DUMP("s_cfi_action               = %d \n", tmp.s_cfi_action);
        SYS_SCL_DBG_DUMP("c_cfi_action               = %d \n", tmp.c_cfi_action);
        SYS_SCL_DBG_DUMP("svlan_xlate_valid          = %d \n", tmp.svlan_xlate_valid);
        SYS_SCL_DBG_DUMP("cvlan_xlate_valid          = %d \n", tmp.cvlan_xlate_valid);
        SYS_SCL_DBG_DUMP("ctag_add_mode              = %d \n", tmp.ctag_add_mode);
        SYS_SCL_DBG_DUMP("user_vlan_ptr              = %d \n", tmp.user_vlan_ptr);
        SYS_SCL_DBG_DUMP("svlan_tpid_index_en        = %d \n", tmp.svlan_tpid_index_en);
        SYS_SCL_DBG_DUMP("new_itag                   = %d \n", tmp.new_itag);
        SYS_SCL_DBG_DUMP("isid_valid                 = %d \n", tmp.isid_valid);
        SYS_SCL_DBG_DUMP("svlan_tpid_index           = %d \n", tmp.svlan_tpid_index);
        SYS_SCL_DBG_DUMP("c_cos_action               = %d \n", tmp.c_cos_action);
        SYS_SCL_DBG_DUMP("s_cos_action               = %d \n", tmp.s_cos_action);
        SYS_SCL_DBG_DUMP("c_vlan_id_action           = %d \n", tmp.c_vlan_id_action);
        SYS_SCL_DBG_DUMP("s_vlan_id_action           = %d \n", tmp.s_vlan_id_action);
        SYS_SCL_DBG_DUMP("aging_index                = %d \n", tmp.aging_index);
        SYS_SCL_DBG_DUMP("aging_valid                = %d \n", tmp.aging_valid);
        SYS_SCL_DBG_DUMP("vlan_xlate_port_isolate_en = %d \n", tmp.vlan_xlate_port_isolate_en);
        SYS_SCL_DBG_DUMP("c_tag_action               = %d \n", tmp.c_tag_action);
        SYS_SCL_DBG_DUMP("s_tag_action               = %d \n", tmp.s_tag_action);
        SYS_SCL_DBG_DUMP("user_ccfi                  = %d \n", tmp.user_ccfi);
        SYS_SCL_DBG_DUMP("user_ccos                  = %d \n", tmp.user_ccos);
        SYS_SCL_DBG_DUMP("user_cvlan_id              = %d \n", tmp.user_cvlan_id);
        SYS_SCL_DBG_DUMP("user_scfi                  = %d \n", tmp.user_scfi);
        SYS_SCL_DBG_DUMP("user_scos                  = %d \n", tmp.user_scos);
        SYS_SCL_DBG_DUMP("user_svlan_id              = %d \n", tmp.user_svlan_id);
    }
#endif
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_scl_set_hash_valid(uint8 lchip,uint8 key_type ,uint32 key_index,uint8 valid)
{
    uint32  cmd        = 0;
    uint32  field_val  = 0;

    field_val = valid;

    switch(key_type)
    {
        case SYS_SCL_KEY_HASH_PORT_2VLAN:
            cmd = DRV_IOW(DsUserIdDoubleVlanHashKey_t, DsUserIdDoubleVlanHashKey_Valid_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, key_index , cmd, &field_val));
            break;

        case SYS_SCL_KEY_HASH_PORT:
            cmd = DRV_IOW(DsUserIdPortHashKey_t, DsUserIdPortHashKey_Valid_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, key_index , cmd, &field_val));
            break;

        case SYS_SCL_KEY_HASH_PORT_CVLAN:
            cmd = DRV_IOW(DsUserIdCvlanHashKey_t, DsUserIdCvlanHashKey_Valid_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, key_index , cmd, &field_val));
            break;

        case SYS_SCL_KEY_HASH_PORT_SVLAN:
            cmd = DRV_IOW(DsUserIdSvlanHashKey_t, DsUserIdSvlanHashKey_Valid_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, key_index , cmd, &field_val));
            break;

        case SYS_SCL_KEY_HASH_PORT_CVLAN_COS:
            cmd = DRV_IOW(DsUserIdCvlanCosHashKey_t, DsUserIdCvlanCosHashKey_Valid_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, key_index , cmd, &field_val));
            break;

        case SYS_SCL_KEY_HASH_PORT_SVLAN_COS:
            cmd = DRV_IOW(DsUserIdSvlanCosHashKey_t, DsUserIdSvlanCosHashKey_Valid_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, key_index , cmd, &field_val));
            break;

        case SYS_SCL_KEY_HASH_MAC:
            cmd = DRV_IOW(DsUserIdMacHashKey_t, DsUserIdMacHashKey_Valid_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, key_index , cmd, &field_val));
            break;

        case SYS_SCL_KEY_HASH_PORT_MAC:
            cmd = DRV_IOW(DsUserIdMacPortHashKey_t, DsUserIdMacPortHashKey_Valid_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, key_index , cmd, &field_val));
            break;

        case SYS_SCL_KEY_HASH_IPV4:
            cmd = DRV_IOW(DsUserIdIpv4HashKey_t, DsUserIdIpv4HashKey_Valid_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, key_index , cmd, &field_val));
            break;

        case SYS_SCL_KEY_HASH_PORT_IPV4:
            cmd = DRV_IOW(DsUserIdIpv4PortHashKey_t, DsUserIdIpv4PortHashKey_Valid_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, key_index , cmd, &field_val));
            break;

        case SYS_SCL_KEY_HASH_IPV6:
            cmd = DRV_IOW(DsUserIdIpv6HashKey_t, DsUserIdIpv6HashKey_Valid0_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, key_index , cmd, &field_val));
            cmd = DRV_IOW(DsUserIdIpv6HashKey_t, DsUserIdIpv6HashKey_Valid1_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, key_index , cmd, &field_val));
            break;

        case SYS_SCL_KEY_HASH_L2:
            cmd = DRV_IOW(DsUserIdL2HashKey_t, DsUserIdL2HashKey_Valid0_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, key_index , cmd, &field_val));
            cmd = DRV_IOW(DsUserIdL2HashKey_t, DsUserIdL2HashKey_Valid1_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, key_index , cmd, &field_val));
            break;

        case SYS_SCL_KEY_HASH_TUNNEL_IPV4:
        case SYS_SCL_KEY_HASH_TUNNEL_IPV4_GRE:
            cmd = DRV_IOW(DsTunnelIdIpv4HashKey_t, DsTunnelIdIpv4HashKey_Valid0_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, key_index , cmd, &field_val));
            cmd = DRV_IOW(DsTunnelIdIpv4HashKey_t, DsTunnelIdIpv4HashKey_Valid1_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, key_index , cmd, &field_val));
            break;

        case SYS_SCL_KEY_HASH_TUNNEL_IPV4_RPF:
            cmd = DRV_IOW(DsTunnelIdIpv4RpfHashKey_t, DsTunnelIdIpv4RpfHashKey_Valid_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, key_index , cmd, &field_val));
            break;
    }
    return CTC_E_NONE;
}
/*delete key is enough*/
STATIC int32
_sys_greatbelt_scl_remove_hw(uint8 lchip, uint32 eid)
{
    uint32                 key_index = 0;
    uint32                 key_id        = 0;
    uint32                 act_id        = 0;

    sys_scl_sw_group_t     * pg = NULL;
    sys_scl_sw_entry_t     * pe = NULL;

    /* 10 is enough */
    uint32 mm[MAX_ENTRY_WORD] = { 0 };

    SYS_SCL_DBG_FUNC();
    SYS_SCL_DBG_PARAM("  %% PARAM: eid: %u \n", eid);

    /* get sys entry */
    _sys_greatbelt_scl_get_nodes_by_eid(lchip, eid, &pe, &pg, NULL);
    if(!pe)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }
    if(!pg)
    {
        return CTC_E_SCL_GROUP_UNEXIST;
    }

    /* get block_id, key_index */
    key_index = pe->fpae.offset_a;

    /* get group_info */
    CTC_ERROR_RETURN(_sys_greatbelt_scl_get_table_id(lchip, pe->key.conv_type, pe->key.type, pe->action->type, &key_id, &act_id));

    if (SCL_ENTRY_IS_TCAM(pe->key.type) || (pe->key.conv_type))
    {
        /* tcam entry need delete action too */
        CTC_ERROR_RETURN(DRV_TCAM_TBL_REMOVE(lchip, key_id, key_index));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, key_index, DRV_IOW(act_id, DRV_ENTRY_FLAG), mm));
    }
    else
    {
        /*remove key*/
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, key_index, DRV_IOW(key_id, DRV_ENTRY_FLAG), mm));

        /*keep hash key valid */
        CTC_ERROR_RETURN(_sys_greatbelt_scl_set_hash_valid(lchip,pe->key.type,key_index,1));
    }
    return CTC_E_NONE;
}


/*
 * install entry to hardware table
 */
STATIC int32
_sys_greatbelt_scl_install_entry(uint8 lchip, uint32 eid, uint8 flag, uint8 move_sw)
{

    sys_scl_sw_group_t* pg = NULL;
    sys_scl_sw_entry_t* pe = NULL;

    SYS_SCL_DBG_FUNC();
    SYS_SCL_DBG_PARAM("  %% PARAM: eid %u \n", eid);

    /* get sys entry */
    _sys_greatbelt_scl_get_nodes_by_eid(lchip, eid, &pe, &pg, NULL);
    if(!pe)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }
    if(!pg)
    {
        return CTC_E_SCL_GROUP_UNEXIST;
    }

    if (SYS_SCL_ENTRY_OP_FLAG_ADD == flag)
    {
#if 0
        if (FPA_ENTRY_FLAG_INSTALLED == pe->fpae.flag)
        {   /* already installed */
            return CTC_E_NONE;
        }
#endif         /* _if0_*/

            CTC_ERROR_RETURN(_sys_greatbelt_scl_add_hw(lchip, eid));
        pe->fpae.flag = FPA_ENTRY_FLAG_INSTALLED;

        if (move_sw)
        {
            /* move to tail */
            ctc_slist_delete_node(pg->entry_list, &(pe->head));
            ctc_slist_add_tail(pg->entry_list, &(pe->head));
        }
    }
    else if (SYS_SCL_ENTRY_OP_FLAG_DELETE == flag)
    {
#if 0
        if (FPA_ENTRY_FLAG_UNINSTALLED == pe->fpae.flag)
        {   /* already uninstalled */
            return CTC_E_NONE;
        }
#endif         /* _if0_*/

            CTC_ERROR_RETURN(_sys_greatbelt_scl_remove_hw(lchip, eid));
        pe->fpae.flag = FPA_ENTRY_FLAG_UNINSTALLED;

        if (move_sw)
        {
            /* move to head */
            ctc_slist_delete_node(pg->entry_list, &(pe->head));
            ctc_slist_add_head(pg->entry_list, &(pe->head));
        }
    }

    return CTC_E_NONE;
}

#define __SCL_FPA_CALLBACK__

STATIC int32
_sys_greatbelt_scl_get_block_by_pe_fpa(uint8 lchip, ctc_fpa_entry_t* pe, ctc_fpa_block_t** pb)
{
    uint8             ftm;
    sys_scl_sw_entry_t* entry;

    CTC_PTR_VALID_CHECK(pe);
    CTC_PTR_VALID_CHECK(pb);

    *pb = NULL;

    entry = SCL_OUTER_ENTRY(pe);
    CTC_PTR_VALID_CHECK(entry->group);

    ftm = scl_master[lchip]->alloc_id[entry->key.type];


    *pb = &(scl_master[lchip]->block[ftm].fpab);

    return CTC_E_NONE;
}


/*
 * move entry in hardware table to an new index.
 */
STATIC int32
_sys_greatbelt_scl_entry_move_hw_fpa(uint8 lchip,
                                     ctc_fpa_entry_t* pe,
                                     int32 tcam_idx_new)
{
    int32 tcam_idx_old = pe->offset_a;
    uint32 eid;


    CTC_PTR_VALID_CHECK(pe);
    SYS_SCL_DBG_FUNC();

    eid = pe->entry_id;

    /* add first */
    pe->offset_a = tcam_idx_new;
    CTC_ERROR_RETURN(_sys_greatbelt_scl_add_hw(lchip, eid));

    /* then delete */
    pe->offset_a = tcam_idx_old;
    CTC_ERROR_RETURN(_sys_greatbelt_scl_remove_hw(lchip, eid));

    /* set new_index */
    pe->offset_a = tcam_idx_new;

    return CTC_E_NONE;
}

/*
 * below is build sw struct
 */
#define __SCL_HASH_CALLBACK__


STATIC uint32
_sys_greatbelt_scl_hash_make_group(sys_scl_sw_group_t* pg)
{
    return pg->group_id;
}

STATIC bool
_sys_greatbelt_scl_hash_compare_group(sys_scl_sw_group_t* pg0, sys_scl_sw_group_t* pg1)
{
    return(pg0->group_id == pg1->group_id);
}

STATIC uint32
_sys_greatbelt_scl_hash_make_entry(sys_scl_sw_entry_t* pe)
{
    return pe->fpae.entry_id;
}

STATIC bool
_sys_greatbelt_scl_hash_compare_entry(sys_scl_sw_entry_t* pe0, sys_scl_sw_entry_t* pe1)
{
    return(pe0->fpae.entry_id == pe1->fpae.entry_id);
}

STATIC uint32
_sys_greatbelt_scl_hash_make_entry_by_key(sys_scl_sw_entry_t* pe)
{
    uint32 size;
    uint8  * k;
    uint8 lchip = 0;
    CTC_PTR_VALID_CHECK(pe);
    lchip = pe->fpae.lchip;
    size = scl_master[lchip]->key_size[pe->key.type];
    if (SCL_ENTRY_IS_TCAM(pe->key.type))
    {
        k = (uint8 *) (&pe->key.u0);
    }
    else
    {
        k = (uint8 *) (&pe->key.u0.hash.u0);
    }
    return ctc_hash_caculate(size, k);
}
STATIC bool
_sys_greatbelt_scl_hash_compare_entry_by_key(sys_scl_sw_entry_t* pe0, sys_scl_sw_entry_t* pe1)
{
    uint8 lchip = 0;

    if (!pe0 || !pe1)
    {
        return FALSE;
    }

    if ((pe0->key.type != pe1->key.type) ||
        (pe0->group->group_info.type != pe1->group->group_info.type))
    {
        return FALSE;
    }

    lchip = pe0->fpae.lchip;

    if (SCL_ENTRY_IS_TCAM(pe0->key.type))
    {
        if (!sal_memcmp(&pe0->key.u0, &pe1->key.u0, scl_master[lchip]->key_size[pe0->key.type]))
        {
            if (CTC_SCL_GROUP_TYPE_PORT_CLASS == pe0->group->group_info.type)
            {   /* for ip src guard. vlan class default entry. the key is the same, but class_id is not. */
                if (pe0->group->group_info.port_class_id == pe1->group->group_info.port_class_id)
                {
                    return TRUE;
                }
            }
            else if ((CTC_SCL_GROUP_TYPE_PORT == pe0->group->group_info.type) || (CTC_SCL_GROUP_TYPE_LOGIC_PORT == pe0->group->group_info.type))
            {   /* for vlan mapping flex entry. the key is the same, but port is not. */
                if (pe0->group->group_info.gport == pe1->group->group_info.gport)
                {
                    return TRUE;
                }
            }
            else
            {
                return TRUE;
            }
        }
    }
    else /* hash */
    {
        if (!sal_memcmp(&pe0->key.u0.hash.u0, &pe1->key.u0.hash.u0, scl_master[lchip]->key_size[pe0->key.type]))
        {
            return TRUE;
        }
    }

    return FALSE;
}
STATIC uint32
_sys_greatbelt_scl_hash_make_action(sys_scl_sw_action_t* pa)
{
    uint32 size = 0;
    uint8  * k;
    uint8 lchip = 0;
    CTC_PTR_VALID_CHECK(pa);
    lchip = pa->lchip;
    k = (uint8 *) (&pa->u0);
    switch (pa->type)
    {
    case SYS_SCL_ACTION_INGRESS:
        size = scl_master[lchip]->hash_igs_action_size;
        break;
    case SYS_SCL_ACTION_EGRESS:
        size = scl_master[lchip]->hash_egs_action_size;
        break;
    case SYS_SCL_ACTION_TUNNEL:
        size = scl_master[lchip]->hash_tunnel_action_size;
        break;
    }
    return ctc_hash_caculate(size, k);
}

STATIC bool
_sys_greatbelt_scl_hash_compare_action(sys_scl_sw_action_t* pa0, sys_scl_sw_action_t* pa1)
{
    uint8 lchip = 0;

    if (!pa0 || !pa1)
    {
        return FALSE;
    }

    if (pa0->type != pa1->type)
    {
        return FALSE;
    }
    lchip = pa0->lchip;
    switch (pa0->type)
    {
    case SYS_SCL_ACTION_INGRESS:
        if ((pa0->u0.ia.chip.profile_id == pa1->u0.ia.chip.profile_id)
            && (pa0->u0.ia.chip.offset == pa1->u0.ia.chip.offset)
            && (pa0->u0.ia.chip.service_policer_ptr == pa1->u0.ia.chip.service_policer_ptr)
            && (pa0->u0.ia.chip.stats_ptr == pa1->u0.ia.chip.stats_ptr)
            && (!sal_memcmp(&pa0->u0.ia, &pa1->u0.ia, scl_master[lchip]->hash_igs_action_size)))
        {
            return TRUE;
        }
        break;
    case SYS_SCL_ACTION_EGRESS:
        if ((pa0->u0.ea.chip.stats_ptr == pa1->u0.ea.chip.stats_ptr)
            && (!sal_memcmp(&pa0->u0.ea, &pa1->u0.ea, scl_master[lchip]->hash_egs_action_size)))
        {
            return TRUE;
        }
        break;
    case SYS_SCL_ACTION_TUNNEL:
        if (!sal_memcmp(&pa0->u0.ta, &pa1->u0.ta, scl_master[lchip]->hash_tunnel_action_size))
        {
            return TRUE;
        }
        break;
    }


    return FALSE;
}


/* equal return true */
/*static bool
_sys_greatbelt_scl_hash_compare_action1(sys_scl_sw_action_t* pa0, sys_scl_sw_action_t* pa1)
{
    uint8 lchip = 0;
    lchip = pa0->lchip;
    if (!pa0 || !pa1)
    {
        return FALSE;
    }

    if (pa0->type != pa1->type)
    {
        return FALSE;
    }
    switch (pa0->type)
    {
    case SYS_SCL_ACTION_INGRESS:
        if ((pa0->u0.ia.chip.profile_id == pa1->u0.ia.chip.profile_id)
            && (pa0->u0.ia.chip.offset == pa1->u0.ia.chip.offset)
            && (pa0->u0.ia.chip.service_policer_ptr == pa1->u0.ia.chip.service_policer_ptr)
            && (pa0->u0.ia.chip.stats_ptr == pa1->u0.ia.chip.stats_ptr)
            && (!sal_memcmp(&pa0->u0.ia, &pa1->u0.ia, scl_master[lchip]->hash_igs_action_size)))
        {
            return TRUE;
        }
        break;
    case SYS_SCL_ACTION_EGRESS:
        if ((pa0->u0.ea.chip.stats_ptr == pa1->u0.ea.chip.stats_ptr)
            && (!sal_memcmp(&pa0->u0.ea, &pa1->u0.ea, scl_master[lchip]->hash_egs_action_size)))
        {
            return TRUE;
        }
        break;
    case SYS_SCL_ACTION_TUNNEL:
        if (!sal_memcmp(&pa0->u0.ta, &pa1->u0.ta, scl_master[lchip]->hash_tunnel_action_size))
        {
            return TRUE;
        }
        break;
    }


    return FALSE;
}*/


/* if vlan edit in bucket equals */
STATIC bool
_sys_greatbelt_scl_hash_compare_vlan_edit(sys_scl_sw_vlan_edit_t* pv0,
                                          sys_scl_sw_vlan_edit_t* pv1)
{
    uint32 size = 0;
    uint8 lchip = 0;
    if (!pv0 || !pv1)
    {
        return FALSE;
    }
    lchip = pv0->lchip;
    size = scl_master[lchip]->vlan_edit_size;
    if (!sal_memcmp(pv0, pv1, size))
    {
        return TRUE;
    }

    return FALSE;
}

STATIC uint32
_sys_greatbelt_scl_hash_make_vlan_edit(sys_scl_sw_vlan_edit_t* pv)
{
    uint32 size = 0;
    uint8  lchip = 0;
    uint8  * k = NULL;
    CTC_PTR_VALID_CHECK(pv);

    lchip = pv->lchip;

    size = scl_master[lchip]->vlan_edit_size;
    k    = (uint8 *) pv;

    return ctc_hash_caculate(size, k);
}

#define __SCL_MAP_UNMAP__


STATIC int32
_sys_greatbelt_scl_get_ip_frag(uint8 lchip, uint8 ctc_ip_frag, uint8* frag_info, uint8* frag_info_mask)
{
    switch (ctc_ip_frag)
    {
    case CTC_IP_FRAG_NON:
        /* Non fragmented */
        *frag_info      = 0;
        *frag_info_mask = 3;
        break;

    case CTC_IP_FRAG_FIRST:
        /* Fragmented, and first fragment */
        *frag_info      = 1;
        *frag_info_mask = 3;
        break;

    case CTC_IP_FRAG_NON_OR_FIRST:
        /* Non fragmented OR Fragmented and first fragment */
        *frag_info      = 0;
        *frag_info_mask = 2;     /* mask frag_info as 0x */
        break;

    case CTC_IP_FRAG_SMALL:
        /* Small fragment */
        *frag_info      = 2;
        *frag_info_mask = 3;
        break;

    case CTC_IP_FRAG_NOT_FIRST:
        /* Not first fragment (Fragmented of cause) */
        *frag_info      = 3;
        *frag_info_mask = 3;
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}



STATIC int32
_sys_greatbelt_scl_check_vlan_edit(uint8 lchip, ctc_scl_vlan_edit_t*   p_sys, uint8* do_nothing)
{
    /* check tag op */
    CTC_MAX_VALUE_CHECK(p_sys->vlan_domain, CTC_SCL_VLAN_DOMAIN_MAX - 1);
    CTC_MAX_VALUE_CHECK(p_sys->stag_op, CTC_VLAN_TAG_OP_MAX - 1);
    CTC_MAX_VALUE_CHECK(p_sys->ctag_op, CTC_VLAN_TAG_OP_MAX - 1);

    if ((CTC_VLAN_TAG_OP_NONE == p_sys->stag_op)
        && (CTC_VLAN_TAG_OP_NONE == p_sys->ctag_op))
    {
        *do_nothing = 1;
        return CTC_E_NONE;
    }

    *do_nothing = 0;

    if ((CTC_VLAN_TAG_OP_ADD == p_sys->stag_op) \
        || (CTC_VLAN_TAG_OP_REP == p_sys->stag_op)
        || (CTC_VLAN_TAG_OP_REP_OR_ADD == p_sys->stag_op))
    {
        /* check stag select */
        if ((p_sys->svid_sl >= CTC_VLAN_TAG_SL_MAX)    \
            || (p_sys->scos_sl >= CTC_VLAN_TAG_SL_MAX) \
            || (p_sys->scfi_sl >= CTC_VLAN_TAG_SL_MAX))
        {
            return CTC_E_SCL_VLAN_EDIT_INVALID;
        }
        if (CTC_VLAN_TAG_SL_NEW == p_sys->svid_sl)
        {
            if (p_sys->svid_new > CTC_MAX_VLAN_ID)
            {
                return CTC_E_VLAN_INVALID_VLAN_ID;
            }
        }
        if (CTC_VLAN_TAG_SL_NEW == p_sys->scos_sl)
        {
            CTC_COS_RANGE_CHECK(p_sys->scos_new);
        }
        if (CTC_VLAN_TAG_SL_NEW == p_sys->scfi_sl)
        {
            CTC_MAX_VALUE_CHECK(p_sys->scfi_new, 1);
        }
    }

    if ((CTC_VLAN_TAG_OP_ADD == p_sys->ctag_op) \
        || (CTC_VLAN_TAG_OP_REP == p_sys->ctag_op)
        || (CTC_VLAN_TAG_OP_REP_OR_ADD == p_sys->ctag_op))
    {
        /* check ctag select */
        if ((p_sys->cvid_sl >= CTC_VLAN_TAG_SL_MAX)    \
            || (p_sys->ccos_sl >= CTC_VLAN_TAG_SL_MAX) \
            || (p_sys->ccfi_sl >= CTC_VLAN_TAG_SL_MAX))
        {
            return CTC_E_SCL_VLAN_EDIT_INVALID;
        }
        if (CTC_VLAN_TAG_SL_NEW == p_sys->cvid_sl)
        {
            if (p_sys->cvid_new > CTC_MAX_VLAN_ID)
            {
                return CTC_E_VLAN_INVALID_VLAN_ID;
            }
        }
        if (CTC_VLAN_TAG_SL_NEW == p_sys->ccos_sl)
        {
            CTC_COS_RANGE_CHECK(p_sys->ccos_new);
        }
        if (CTC_VLAN_TAG_SL_NEW == p_sys->ccfi_sl)
        {
            CTC_MAX_VALUE_CHECK(p_sys->ccfi_new, 1);
        }
    }
    CTC_MAX_VALUE_CHECK(p_sys->tpid_index, 3);

    return CTC_E_NONE;
}


/*
 * p_sys_action_old be  NULL for add.
 *                  not NULL for update.
 */
STATIC int32
_sys_greatbelt_scl_map_service_queue_action(uint8 lchip, sys_scl_action_t* action,
                                    sys_scl_sw_action_t* p_sys_action,
                                    sys_scl_sw_action_t* p_sys_action_old)
{
    uint32 old_flag = 0;
    uint32 new_flag  = 0;
    uint32 new_sub_flag = 0;

    CTC_PTR_VALID_CHECK(p_sys_action);
    CTC_PTR_VALID_CHECK(action);


    switch (p_sys_action->type)
    {
    case SYS_SCL_ACTION_INGRESS:
        if (p_sys_action_old)
        {
            old_flag = p_sys_action_old->u0.ia.flag;
        }

        new_flag = action->u.igs_action.flag;
        new_sub_flag = action->u.igs_action.sub_flag;

        /*service queue enable*/
        if(CTC_FLAG_ISSET(new_sub_flag, CTC_SCL_IGS_ACTION_SUB_FLAG_SERVICE_QUEUE_EN))
        {
            p_sys_action->u0.ia.src_queue_select = TRUE;
        }
        else
        {
            p_sys_action->u0.ia.src_queue_select = FALSE;
        }

        /*bind service logic port and alloc service queue*/
        /*bind new service_id*/
        if (CTC_FLAG_ISSET(new_flag, CTC_SCL_IGS_ACTION_FLAG_SERVICE_ID))
        {
            CTC_NOT_EQUAL_CHECK(action->u.igs_action.service_id, 0);
            p_sys_action->u0.ia.service_id = action->u.igs_action.service_id;
            if (!CTC_FLAG_ISSET(new_flag, CTC_SCL_IGS_ACTION_FLAG_LOGIC_PORT))
            {   /* service queue, service acl. need bind logic port */
                return CTC_E_INVALID_PARAM;
            }
            CTC_ERROR_RETURN(sys_greatbelt_qos_bind_service_logic_port(lchip, action->u.igs_action.service_id,
                                                                       action->u.igs_action.logic_port.logic_port));
        }

        /*unbind old service_id*/
        if (CTC_FLAG_ISSET(old_flag, CTC_SCL_IGS_ACTION_FLAG_SERVICE_ID) &&
            (action->u.igs_action.service_id != p_sys_action_old->u0.ia.service_id ||
            action->u.igs_action.logic_port.logic_port != p_sys_action_old->u0.ia.bind_l.bds.logic_src_port))
        {
            CTC_ERROR_RETURN(sys_greatbelt_qos_unbind_service_logic_port(lchip, p_sys_action_old->u0.ia.service_id,
                                                                         p_sys_action_old->u0.ia.bind_l.bds.logic_src_port));
        }


        break;
    case SYS_SCL_ACTION_EGRESS:
        break;
    case SYS_SCL_ACTION_TUNNEL: /* tunnel not support stats*/
        break;
    default:
        return CTC_E_INVALID_PARAM;
    }



    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_scl_map_igs_action(uint8 lchip,
                                  ctc_scl_igs_action_t* p_ctc_action,
                                  sys_scl_sw_igs_action_t* pia,
                                  sys_scl_sw_vlan_edit_t* pv)
{
    uint16          policer_ptr;
    uint32          flag;
    uint32 fwd_offset = 0;
    uint8 policer_type = 0;
    sys_nh_info_com_t* p_nhinfo = NULL;
    sys_nh_info_mcast_t* p_mcast_info = NULL;

    CTC_PTR_VALID_CHECK(p_ctc_action);
    CTC_PTR_VALID_CHECK(pia);

    SYS_SCL_DBG_FUNC();

    /* do disrad, deny bridge, copy to copy, aging when install */

    if ((CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_IGS_ACTION_FLAG_FID)
            + CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_IGS_ACTION_FLAG_REDIRECT)
            + CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_IGS_ACTION_FLAG_VRFID)) >= 2)
    {
        return CTC_E_SCL_FLAG_COLLISION;
    }

    if ((CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_IGS_ACTION_FLAG_FID)
            + CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_IGS_ACTION_FLAG_DISCARD)
            + CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_IGS_ACTION_FLAG_VRFID)) >= 2)
    {
        return CTC_E_SCL_FLAG_COLLISION;
    }

    /* bind en collision */
    if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_IGS_ACTION_FLAG_BINDING_EN))
    {
        if ((CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_IGS_ACTION_FLAG_APS))
            || (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_IGS_ACTION_FLAG_VPLS))
            || (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_IGS_ACTION_FLAG_REDIRECT))
            || (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_IGS_ACTION_FLAG_LOGIC_PORT))
            || (CTC_FLAG_ISSET(p_ctc_action->sub_flag, CTC_SCL_IGS_ACTION_SUB_FLAG_SERVICE_ACL_EN)))
        {
            return CTC_E_SCL_FLAG_COLLISION;
        }
    }

    /* user vlan ptr collision */
    if(((CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_IGS_ACTION_FLAG_USER_VLANPTR))
            + (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_IGS_ACTION_FLAG_APS))) >= 2)
    {
        return CTC_E_SCL_FLAG_COLLISION;
    }

    if (((CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_IGS_ACTION_FLAG_VPLS))
         + ((CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_IGS_ACTION_FLAG_APS)) && (SYS_SCL_MPLS_DECAP_VLAN_PTR != p_ctc_action->user_vlanptr))
         + (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_IGS_ACTION_FLAG_REDIRECT))) >= 2)
    {
        return CTC_E_SCL_FLAG_COLLISION;
    }



    flag = p_ctc_action->flag;

    /* stats do outside */
    if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_IGS_ACTION_FLAG_STATS))
    {
        pia->stats_id = p_ctc_action->stats_id;
            CTC_ERROR_RETURN(sys_greatbelt_stats_get_statsptr
                                 (lchip, pia->stats_id, &pia->chip.stats_ptr));
    }
    else
    {
        pia->stats_id = 0;
            pia->chip.stats_ptr = 0;
    }


    /* service flow policer */
    if (p_ctc_action->policer_id)
    {
        pia->service_policer_id = p_ctc_action->policer_id;
        policer_type = CTC_QOS_POLICER_TYPE_FLOW;
    }
    else if(CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_IGS_ACTION_FLAG_SERVICE_ID)
           && CTC_FLAG_ISSET(p_ctc_action->sub_flag, CTC_SCL_IGS_ACTION_SUB_FLAG_SERVICE_POLICER_EN))
    {
        CTC_NOT_EQUAL_CHECK(p_ctc_action->service_id, 0);
        pia->service_policer_id = p_ctc_action->service_id;
        policer_type = CTC_QOS_POLICER_TYPE_SERVICE;
    }

    if (pia->service_policer_id)
    {
        CTC_ERROR_RETURN(sys_greatbelt_qos_policer_index_get(lchip,
                                                             policer_type,
                                                             pia->service_policer_id,
                                                             &policer_ptr));

        if ((policer_ptr > 0x1FFF) || (policer_ptr == 0))
        {
            return CTC_E_INVALID_PARAM;
        }

        pia->chip.service_policer_ptr = policer_ptr;

        if (CTC_FLAG_ISSET(p_ctc_action->sub_flag, CTC_SCL_IGS_ACTION_SUB_FLAG_SERVICE_POLICER_EN))
        {
            pia->bind_h.bds.service_policer_valid = 1;
        }
    }

#if 0
    if (CTC_FLAG_ISSET(p_ctc_action->sub_flag, CTC_SCL_IGS_ACTION_SUB_FLAG_SERVICE_ACL_EN))
    {
        if ((CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_IGS_ACTION_FLAG_LOGIC_PORT))
            && (p_ctc_action->service_id != p_ctc_action->logic_port.logic_port))
        {   /* limitation: servieid must == logic port */
            return CTC_E_INVALID_PARAM;
        }
        else
        {
            CTC_SET_FLAG(p_ctc_action->flag, CTC_SCL_IGS_ACTION_FLAG_LOGIC_PORT);
            p_ctc_action->logic_port.logic_port = p_ctc_action->service_id;
        }
        pia->binding_en                    = 0;
        pia->binding_mac_sa                = 0;
        pia->bind_l.bds.service_acl_qos_en = 1;
    }
#endif


    if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_IGS_ACTION_FLAG_LOGIC_PORT)
        && CTC_FLAG_ISSET(p_ctc_action->sub_flag, CTC_SCL_IGS_ACTION_SUB_FLAG_SERVICE_ACL_EN))
    {
        p_ctc_action->logic_port.logic_port = p_ctc_action->logic_port.logic_port;
        pia->binding_en                    = 0;
        pia->binding_mac_sa                = 0;
        pia->bind_l.bds.service_acl_qos_en = 1;
    }


    /* priority + color */
    if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_IGS_ACTION_FLAG_PRIORITY_AND_COLOR))
    {
        /* valid check */
        CTC_NOT_ZERO_CHECK(p_ctc_action->color);
        CTC_MAX_VALUE_CHECK(p_ctc_action->color, MAX_CTC_QOS_COLOR - 1);
        CTC_MAX_VALUE_CHECK(p_ctc_action->priority, SYS_QOS_CLASS_PRIORITY_MAX);

        pia->priority = p_ctc_action->priority;
        pia->color    = p_ctc_action->color;
    }


    if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_IGS_ACTION_FLAG_FID))
    {
        SYS_L2_FID_CHECK(p_ctc_action->fid);
        pia->fid      = p_ctc_action->fid;
        pia->fid_type = 1;
    }

    if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_IGS_ACTION_FLAG_VRFID))
    {
        pia->fid      = p_ctc_action->vrf_id;
        pia->fid_type = 0;
    }


    if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_IGS_ACTION_FLAG_USER_VLANPTR))
    {
        pia->user_vlan_ptr = p_ctc_action->user_vlanptr;
    }

    {
        if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_IGS_ACTION_FLAG_VPLS))
        {
            pia->binding_en     = 0;
            pia->binding_mac_sa = 0;
            if(0 == p_ctc_action->user_vlanptr)
            {
                pia->user_vlan_ptr  = SYS_SCL_BY_PASS_VLAN_PTR;
            }

            CTC_MAX_VALUE_CHECK(p_ctc_action->vpls.learning_en, 1);
            CTC_MAX_VALUE_CHECK(p_ctc_action->vpls.mac_limit_en, 1);
            pia->bind_l.bds.vsi_learning_disable     = p_ctc_action->vpls.learning_en ? 0 : 1;
            pia->bind_l.bds.mac_security_vsi_discard = p_ctc_action->vpls.mac_limit_en;
        }

        if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_IGS_ACTION_FLAG_APS))
        {
            pia->binding_en     = 0;
            pia->binding_mac_sa = 0;
            if (p_ctc_action->aps.protected_vlan_valid)
            {
                CTC_VLAN_RANGE_CHECK(p_ctc_action->aps.protected_vlan);
            }

            /* used by mpls decap with iloop */
            if (SYS_SCL_MPLS_DECAP_VLAN_PTR == p_ctc_action->user_vlanptr)
            {
                pia->user_vlan_ptr   = SYS_SCL_BY_PASS_VLAN_PTR;
            }
            else
            {
                pia->user_vlan_ptr   = p_ctc_action->aps.protected_vlan;
            }

            pia->bind_h.bds.aps_select_valid = 1;

            CTC_APS_GROUP_ID_CHECK(p_ctc_action->aps.aps_select_group_id);
            CTC_MAX_VALUE_CHECK(p_ctc_action->aps.is_working_path, 1);
            pia->bind_h.bds.aps_select_group_id        = p_ctc_action->aps.aps_select_group_id;
            pia->bind_h.bds.aps_select_protecting_path = p_ctc_action->aps.is_working_path;
        }

        if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_IGS_ACTION_FLAG_IGMP_SNOOPING))
        {
            pia->binding_en     = 0;
            pia->binding_mac_sa = 0;
            pia->bind_h.bds.igmp_snoop_en = 1;
        }

        if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_IGS_ACTION_FLAG_REDIRECT))
        {
            pia->nh_id = p_ctc_action->nh_id;

            CTC_ERROR_RETURN(sys_greatbelt_nh_get_nhinfo_by_nhid(lchip, pia->nh_id, &p_nhinfo));
            CTC_PTR_VALID_CHECK(p_nhinfo);

            if (SYS_NH_TYPE_MCAST == p_nhinfo->hdr.nh_entry_type)
            {
                p_mcast_info = (sys_nh_info_mcast_t*)p_nhinfo;
                pia->bypass_all = p_mcast_info->is_mirror ? 1 : 0;
            }

            CTC_ERROR_RETURN(sys_greatbelt_nh_get_dsfwd_offset(lchip, pia->nh_id, &fwd_offset));
                CTC_NOT_ZERO_CHECK(fwd_offset);
                pia->chip.offset = fwd_offset;
            pia->binding_en                      = 0;
            pia->binding_mac_sa                  = 0;
            if(0 == p_ctc_action->user_vlanptr)
            {
                pia->user_vlan_ptr                   = SYS_SCL_BY_PASS_VLAN_PTR;
            }

            pia->bind_l.bds.vsi_learning_disable = 1;
        }

        if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_IGS_ACTION_FLAG_LOGIC_PORT))
        {
            SYS_LOGIC_PORT_CHECK(p_ctc_action->logic_port.logic_port);
            CTC_MAX_VALUE_CHECK(p_ctc_action->logic_port.logic_port_type, 1);

            pia->binding_en                 = 0;
            pia->binding_mac_sa             = 0;
            pia->bind_l.bds.logic_src_port  = p_ctc_action->logic_port.logic_port;
            pia->bind_l.bds.logic_port_type = p_ctc_action->logic_port.logic_port_type;
        }


        if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_IGS_ACTION_FLAG_BINDING_EN))
        {
            pia->binding_en = 1;
            switch (p_ctc_action->bind.type)
            {
            case CTC_SCL_BIND_TYPE_PORT:
                CTC_GLOBAL_PORT_CHECK(p_ctc_action->bind.gport);
                pia->bind_h.data |= SYS_SCL_GPORT13_TO_GPORT14(p_ctc_action->bind.gport) & 0x3FFF;
                break;
            case CTC_SCL_BIND_TYPE_VLAN:
                CTC_VLAN_RANGE_CHECK(p_ctc_action->bind.vlan_id);
                pia->bind_h.data |= (p_ctc_action->bind.vlan_id & 0xFFF) << 4;
                pia->bind_l.data |= 1;
                break;
            case CTC_SCL_BIND_TYPE_IPV4SA:
                pia->bind_h.data |= ((p_ctc_action->bind.ipv4_sa >> 28) & 0xF);
                pia->bind_l.data |= ((p_ctc_action->bind.ipv4_sa & 0xFFFFFFF) << 4);
                pia->bind_l.data |= 2;
                break;
            case CTC_SCL_BIND_TYPE_IPV4SA_VLAN:
                pia->bind_h.data |= (p_ctc_action->bind.vlan_id & 0xFFF) << 4;
                pia->bind_h.data |= ((p_ctc_action->bind.ipv4_sa >> 28) & 0xF);
                pia->bind_l.data |= ((p_ctc_action->bind.ipv4_sa & 0xFFFFFFF) << 4);
                pia->bind_l.data |= 0x3;
                break;
            case CTC_SCL_BIND_TYPE_MACSA:
                pia->binding_mac_sa = 1;
                SYS_SCL_SET_MAC_HIGH(pia->bind_h.data, p_ctc_action->bind.mac_sa);
                SYS_SCL_SET_MAC_LOW(pia->bind_l.data, p_ctc_action->bind.mac_sa);
                break;
            default:
                return CTC_E_INVALID_PARAM;
            }
        }
    }


    /* map vlan edit */
    {
        uint8 no_vlan_edit = 1;
        if (CTC_FLAG_ISSET(flag, CTC_SCL_IGS_ACTION_FLAG_VLAN_EDIT))
        {
            CTC_ERROR_RETURN(_sys_greatbelt_scl_check_vlan_edit(lchip, &p_ctc_action->vlan_edit, &no_vlan_edit));
        }

        if (no_vlan_edit && (CTC_SCL_VLAN_DOMAIN_UNCHANGE == p_ctc_action->vlan_edit.vlan_domain))
        {
            CTC_UNSET_FLAG(flag, CTC_SCL_IGS_ACTION_FLAG_VLAN_EDIT);
        }
        else
        {
            CTC_SET_FLAG(flag, CTC_SCL_IGS_ACTION_FLAG_VLAN_EDIT);

            pv->vlan_domain = p_ctc_action->vlan_edit.vlan_domain;

            if (!no_vlan_edit)
            {
                pv->stag_op = p_ctc_action->vlan_edit.stag_op;
                pv->ctag_op = p_ctc_action->vlan_edit.ctag_op;
                pv->svid_sl = p_ctc_action->vlan_edit.svid_sl;
                pv->scos_sl = p_ctc_action->vlan_edit.scos_sl;
                pv->scfi_sl = p_ctc_action->vlan_edit.scfi_sl;
                pv->cvid_sl = p_ctc_action->vlan_edit.cvid_sl;
                pv->ccos_sl = p_ctc_action->vlan_edit.ccos_sl;
                pv->ccfi_sl = p_ctc_action->vlan_edit.ccfi_sl;
                pv->tpid_index = p_ctc_action->vlan_edit.tpid_index;

                pia->svid = p_ctc_action->vlan_edit.svid_new;
                pia->scos = p_ctc_action->vlan_edit.scos_new;
                pia->scfi = p_ctc_action->vlan_edit.scfi_new;
                pia->cvid = p_ctc_action->vlan_edit.cvid_new;
                pia->ccos = p_ctc_action->vlan_edit.ccos_new;
                pia->ccfi = p_ctc_action->vlan_edit.ccfi_new;
            }
        }
    }

    pia->flag     = flag;
    pia->sub_flag = p_ctc_action->sub_flag;

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_scl_map_egs_action(uint8 lchip,
                                  ctc_scl_egs_action_t* p_ctc_action,
                                  sys_scl_sw_egs_action_t* p_sys_action)
{
    uint32 flag = 0;
    CTC_PTR_VALID_CHECK(p_ctc_action);
    CTC_PTR_VALID_CHECK(p_sys_action);

    SYS_SCL_DBG_FUNC();

    flag = p_ctc_action->flag;

    /* do stats outside */
    if (CTC_FLAG_ISSET(flag, CTC_SCL_EGS_ACTION_FLAG_STATS))
    {
        p_sys_action->stats_id = p_ctc_action->stats_id;
            CTC_ERROR_RETURN(sys_greatbelt_stats_get_statsptr
                                 (lchip, p_sys_action->stats_id, &p_sys_action->chip.stats_ptr));
    }
    else
    {
        p_sys_action->stats_id = 0;
            p_sys_action->chip.stats_ptr = 0;
    }
    /* do discard when install*/
    /* do aging when install*/


    /* vlan edit */
    if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_EGS_ACTION_FLAG_VLAN_EDIT))
    {
        uint8 do_nothing;
        CTC_ERROR_RETURN(_sys_greatbelt_scl_check_vlan_edit(lchip, &p_ctc_action->vlan_edit, &do_nothing));

        if (do_nothing)
        {
            CTC_UNSET_FLAG(flag, CTC_SCL_EGS_ACTION_FLAG_VLAN_EDIT);
        }
        else
        {
            p_sys_action->stag_op = p_ctc_action->vlan_edit.stag_op;
            p_sys_action->ctag_op = p_ctc_action->vlan_edit.ctag_op;
            p_sys_action->svid_sl = p_ctc_action->vlan_edit.svid_sl;
            p_sys_action->scos_sl = p_ctc_action->vlan_edit.scos_sl;
            p_sys_action->scfi_sl = p_ctc_action->vlan_edit.scfi_sl;
            p_sys_action->cvid_sl = p_ctc_action->vlan_edit.cvid_sl;
            p_sys_action->ccos_sl = p_ctc_action->vlan_edit.ccos_sl;
            p_sys_action->ccfi_sl = p_ctc_action->vlan_edit.ccfi_sl;

            p_sys_action->svid = p_ctc_action->vlan_edit.svid_new;
            p_sys_action->scos = p_ctc_action->vlan_edit.scos_new;
            p_sys_action->scfi = p_ctc_action->vlan_edit.scfi_new;
            p_sys_action->cvid = p_ctc_action->vlan_edit.cvid_new;
            p_sys_action->ccos = p_ctc_action->vlan_edit.ccos_new;
            p_sys_action->ccfi = p_ctc_action->vlan_edit.ccfi_new;
            p_sys_action->tpid_index = p_ctc_action->vlan_edit.tpid_index;
        }
    }

    /* priority + color */
    if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_EGS_ACTION_FLAG_PRIORITY_AND_COLOR))
    {
        /* valid check */
        CTC_NOT_ZERO_CHECK(p_ctc_action->color);
        CTC_MAX_VALUE_CHECK(p_ctc_action->color, MAX_CTC_QOS_COLOR - 1);
        CTC_MAX_VALUE_CHECK(p_ctc_action->priority, SYS_QOS_CLASS_PRIORITY_MAX);

        p_sys_action->priority = p_ctc_action->priority;
        p_sys_action->color    = p_ctc_action->color;
    }

    p_sys_action->flag = flag;
    p_sys_action->block_pkt_type = p_ctc_action->block_pkt_type;


    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_scl_map_tunnel_action(uint8 lchip, sys_scl_tunnel_action_t* p_ctc_action,
                                     sys_scl_tunnel_action_t* p_sys_action)
{
    CTC_PTR_VALID_CHECK(p_ctc_action);
    CTC_PTR_VALID_CHECK(p_sys_action);

    /* just memory copy. tunnel action is internal */
    sal_memcpy(p_sys_action, p_ctc_action, sizeof(sys_scl_tunnel_action_t));
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_scl_map_action(uint8 lchip,
                              sys_scl_action_t* p_ctc_action,
                              sys_scl_sw_action_t* p_sys_action,
                              sys_scl_sw_vlan_edit_t* p_sw_vlan_edit)
{
    CTC_PTR_VALID_CHECK(p_ctc_action);
    CTC_PTR_VALID_CHECK(p_sys_action);

    SYS_SCL_DBG_FUNC();

    switch (p_ctc_action->type)
    {
    case SYS_SCL_ACTION_INGRESS:
        CTC_ERROR_RETURN(_sys_greatbelt_scl_map_igs_action(lchip, &p_ctc_action->u.igs_action, &p_sys_action->u0.ia, p_sw_vlan_edit));
        break;

    case SYS_SCL_ACTION_EGRESS:
        CTC_ERROR_RETURN(_sys_greatbelt_scl_map_egs_action(lchip, &p_ctc_action->u.egs_action, &p_sys_action->u0.ea));
        break;

    case SYS_SCL_ACTION_TUNNEL:
        CTC_ERROR_RETURN(_sys_greatbelt_scl_map_tunnel_action(lchip, &p_ctc_action->u.tunnel_action, &p_sys_action->u0.ta));
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_scl_map_tcam_mac_key(uint8 lchip, ctc_scl_tcam_mac_key_t* p_ctc_key,
                                    sys_scl_sw_tcam_key_mac_t* p_sys_key)
{
    uint32 flag = 0;

    CTC_PTR_VALID_CHECK(p_ctc_key);
    CTC_PTR_VALID_CHECK(p_sys_key);

    SYS_SCL_DBG_FUNC();

    flag = p_ctc_key->flag;

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_MAC_KEY_FLAG_CTAG_CFI))
    {
        return CTC_E_FEATURE_NOT_SUPPORT;
    }

    /* mac da */
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_MAC_KEY_FLAG_MAC_DA))
    {
        sal_memcpy(p_sys_key->mac_da, p_ctc_key->mac_da, CTC_ETH_ADDR_LEN);
        sal_memcpy(p_sys_key->mac_da_mask, p_ctc_key->mac_da_mask, CTC_ETH_ADDR_LEN);
    }

    /* mac sa */
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_MAC_KEY_FLAG_MAC_SA))
    {
        sal_memcpy(p_sys_key->mac_sa, p_ctc_key->mac_sa, CTC_ETH_ADDR_LEN);
        sal_memcpy(p_sys_key->mac_sa_mask, p_ctc_key->mac_sa_mask, CTC_ETH_ADDR_LEN);
    }

    /* cvlan */
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_MAC_KEY_FLAG_CVLAN))
    {
        SYS_SCL_VLAN_ID_CHECK(p_ctc_key->cvlan);
        p_sys_key->cvlan      = p_ctc_key->cvlan;
        p_sys_key->cvlan_mask = p_ctc_key->cvlan_mask & 0xFFF;
    }

    /* ctag cos */
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_MAC_KEY_FLAG_CTAG_COS))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->ctag_cos, 0x7);
        p_sys_key->ctag_cos      = p_ctc_key->ctag_cos;
        p_sys_key->ctag_cos_mask = (p_ctc_key->ctag_cos_mask) & 0x7;
    }

    /* ctag cfi */
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_MAC_KEY_FLAG_CTAG_CFI))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->ctag_cfi, 0x1);
        p_sys_key->ctag_cfi = p_ctc_key->ctag_cfi;
    }

    /* svlan */
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_MAC_KEY_FLAG_SVLAN))
    {
        SYS_SCL_VLAN_ID_CHECK(p_ctc_key->svlan);
        p_sys_key->svlan      = p_ctc_key->svlan;
        p_sys_key->svlan_mask = p_ctc_key->svlan_mask & 0xFFF;
    }

    /* stag cos */
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_MAC_KEY_FLAG_STAG_COS))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->stag_cos, 0x7);
        p_sys_key->stag_cos      = p_ctc_key->stag_cos;
        p_sys_key->stag_cos_mask = (p_ctc_key->stag_cos_mask) & 0x7;
    }

    /* stag cfi */
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_MAC_KEY_FLAG_STAG_CFI))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->stag_cfi, 0x1);
        p_sys_key->stag_cfi = p_ctc_key->stag_cfi;
    }

    /* l2 type */
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_MAC_KEY_FLAG_L2_TYPE))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->l2_type, 0xF);
        p_sys_key->l2_type      = p_ctc_key->l2_type;
        p_sys_key->l2_type_mask = p_ctc_key->l2_type_mask;
    }

    /* l3 type */
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_MAC_KEY_FLAG_L3_TYPE))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->l3_type, 0xF);
        p_sys_key->l3_type      = p_ctc_key->l3_type;
        p_sys_key->l3_type_mask = p_ctc_key->l3_type_mask;
    }


    p_sys_key->flag = flag;
    return CTC_E_NONE;
}


STATIC int32
_sys_greatbelt_scl_map_tcam_ipv4_key(uint8 lchip, ctc_scl_tcam_ipv4_key_t* p_ctc_key,
                                     sys_scl_sw_tcam_key_ipv4_t* p_sys_key)
{
    uint32 flag     = 0;
    uint32 sub_flag = 0;

    CTC_PTR_VALID_CHECK(p_ctc_key);
    CTC_PTR_VALID_CHECK(p_sys_key);

    SYS_SCL_DBG_FUNC();

    flag     = p_ctc_key->flag;
    sub_flag = p_ctc_key->sub_flag;

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_DSCP)
        && CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_PRECEDENCE))
    {
        return CTC_E_SCL_FLAG_COLLISION;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_IP_DA))
    {
        p_sys_key->ip_da      = p_ctc_key->ip_da;
        p_sys_key->ip_da_mask = p_ctc_key->ip_da_mask;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_IP_SA))
    {
        p_sys_key->ip_sa      = p_ctc_key->ip_sa;
        p_sys_key->ip_sa_mask = p_ctc_key->ip_sa_mask;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_ETH_TYPE))
    {
        p_sys_key->eth_type      = p_ctc_key->eth_type;
        p_sys_key->eth_type_mask = p_ctc_key->eth_type_mask;
    }

    /* layer 4 information */
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_L4_PROTOCOL))
    {
        CTC_SET_FLAG(sub_flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_L4_PROTOCOL);
        if ((0xFF == p_ctc_key->l4_protocol_mask) &&
            (SYS_SCL_IPV4_ICMP == p_ctc_key->l4_protocol))        /* ICMP */
        {
            p_sys_key->flag_l4info_mapped = 1;
            /* l4_info_mapped for non-tcp non-udp packet, it's l4_type | l3_header_protocol */
            p_sys_key->l4info_mapped      = p_ctc_key->l4_protocol;
            p_sys_key->l4info_mapped_mask = p_ctc_key->l4_protocol_mask;

            p_sys_key->is_tcp      = 0;
            p_sys_key->flag_is_tcp = 1;

            p_sys_key->is_udp      = 0;
            p_sys_key->flag_is_udp = 1;

            /* l4_src_port for flex-l4 (including icmp), it's type|code */

            /* icmp type */
            if (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_ICMP_TYPE))
            {
                CTC_SET_FLAG(sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_L4_SRC_PORT);
                p_sys_key->l4_src_port      |= (p_ctc_key->icmp_type) << 8;
                p_sys_key->l4_src_port_mask |= (p_ctc_key->icmp_type_mask) << 8;
            }

            /* icmp code */
            if (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_ICMP_CODE))
            {
                CTC_SET_FLAG(sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_L4_SRC_PORT);
                p_sys_key->l4_src_port      |= p_ctc_key->icmp_code & 0x00FF;
                p_sys_key->l4_src_port_mask |= (p_ctc_key->icmp_code_mask) & 0x00FF;
            }
        }
        else if ((0xFF == p_ctc_key->l4_protocol_mask)
                 && (SYS_SCL_IPV4_IGMP == p_ctc_key->l4_protocol)) /* IGMP */
        {
            /* l4_info_mapped for non-tcp non-udp packet, it's l4_type | l3_header_protocol */
            p_sys_key->flag_l4info_mapped = 1;
            p_sys_key->l4info_mapped      = p_ctc_key->l4_protocol;
            p_sys_key->l4info_mapped_mask = p_ctc_key->l4_protocol_mask;

            p_sys_key->is_tcp      = 0;
            p_sys_key->flag_is_tcp = 1;

            p_sys_key->is_udp      = 0;
            p_sys_key->flag_is_udp = 1;


            /* l4_src_port for flex-l4 (including igmp), it's type|..*/
            if (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_IGMP_TYPE))
            {
                CTC_SET_FLAG(sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_L4_SRC_PORT);
                p_sys_key->l4_src_port      |= (p_ctc_key->igmp_type << 8) & 0xFF00;
                p_sys_key->l4_src_port_mask |= (p_ctc_key->igmp_type_mask << 8) & 0xFF00;
            }
        }
        else if ((0xFF == p_ctc_key->l4_protocol_mask)
                 && ((SYS_SCL_TCP == p_ctc_key->l4_protocol)
                     || (SYS_SCL_UDP == p_ctc_key->l4_protocol)))   /* TCP or UDP */
        {
            if (SYS_SCL_TCP == p_ctc_key->l4_protocol)
            {   /* TCP */
                p_sys_key->flag_is_tcp = 1;
                p_sys_key->is_tcp      = 1;
            }
            else
            {   /* UDP */
                p_sys_key->flag_is_udp = 1;
                p_sys_key->is_udp      = 1;
            }

            if (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_L4_SRC_PORT))
            {
                CTC_SET_FLAG(sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_L4_SRC_PORT);
                p_sys_key->l4_src_port      = p_ctc_key->l4_src_port;
                p_sys_key->l4_src_port_mask = p_ctc_key->l4_src_port_mask;
            }

            if (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_L4_DST_PORT))
            {
                CTC_SET_FLAG(sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_L4_DST_PORT);
                p_sys_key->l4_dst_port      = p_ctc_key->l4_dst_port;
                p_sys_key->l4_dst_port_mask = p_ctc_key->l4_dst_port_mask;
            }
        }
        else    /* other layer 4 protocol type */
        {
            p_sys_key->flag_l4info_mapped  = 1;
            p_sys_key->l4info_mapped      |= p_ctc_key->l4_protocol;
            p_sys_key->l4info_mapped_mask |= p_ctc_key->l4_protocol_mask;

            p_sys_key->flag_is_tcp = 1;
            p_sys_key->is_tcp      = 0;

            p_sys_key->flag_is_udp = 1;
            p_sys_key->is_udp      = 0;
        }
    }

    /* ip fragment */
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_IP_FRAG))
    {
        uint8 d;
        uint8 m;
        CTC_ERROR_RETURN(_sys_greatbelt_scl_get_ip_frag(lchip, p_ctc_key->ip_frag, &d, &m));
        p_sys_key->frag_info      = d;
        p_sys_key->frag_info_mask = m;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_IP_OPTION))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->ip_option, 1);
        p_sys_key->ip_option = p_ctc_key->ip_option;
    }

    /*
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_IP_HDR_ERR))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->ip_hdr_err, 1);
        p_sys_key->ip_hdr_err = p_ctc_key->ip_hdr_err;
    }
    */

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_MAC_DA))
    {
        sal_memcpy(p_sys_key->mac_da, p_ctc_key->mac_da, CTC_ETH_ADDR_LEN);
        sal_memcpy(p_sys_key->mac_da_mask, p_ctc_key->mac_da_mask, CTC_ETH_ADDR_LEN);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_MAC_SA))
    {
        sal_memcpy(p_sys_key->mac_sa, p_ctc_key->mac_sa, CTC_ETH_ADDR_LEN);
        sal_memcpy(p_sys_key->mac_sa_mask, p_ctc_key->mac_sa_mask, CTC_ETH_ADDR_LEN);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_CVLAN))
    {
        SYS_SCL_VLAN_ID_CHECK(p_ctc_key->cvlan);
        p_sys_key->cvlan      = p_ctc_key->cvlan;
        p_sys_key->cvlan_mask = p_ctc_key->cvlan_mask & 0xFFF;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_CTAG_COS))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->ctag_cos, 0x7);
        p_sys_key->ctag_cos      = p_ctc_key->ctag_cos;
        p_sys_key->ctag_cos_mask = (p_ctc_key->ctag_cos_mask) & 0x7;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_CTAG_CFI))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->ctag_cfi, 0x1);
        p_sys_key->ctag_cfi = p_ctc_key->ctag_cfi;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_SVLAN))
    {
        SYS_SCL_VLAN_ID_CHECK(p_ctc_key->svlan);
        p_sys_key->svlan      = p_ctc_key->svlan;
        p_sys_key->svlan_mask = p_ctc_key->svlan_mask & 0xFFF;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_STAG_COS))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->stag_cos, 0x7);
        p_sys_key->stag_cos      = p_ctc_key->stag_cos;
        p_sys_key->stag_cos_mask = (p_ctc_key->stag_cos_mask) & 0x7;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_STAG_CFI))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->stag_cfi, 0x1);
        p_sys_key->stag_cfi = p_ctc_key->stag_cfi;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_L2_TYPE))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->l2_type, 0xF);
        p_sys_key->l2_type      = p_ctc_key->l2_type;
        p_sys_key->l2_type_mask = (p_ctc_key->l2_type_mask) & 0xF;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_L3_TYPE))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->l3_type, 0xF);
        p_sys_key->l3_type      = p_ctc_key->l3_type;
        p_sys_key->l3_type_mask = (p_ctc_key->l3_type_mask) & 0xF;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_L4_TYPE))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->l4_type, 0xF);
        p_sys_key->l4_type      = p_ctc_key->l4_type;
        p_sys_key->l4_type_mask = (p_ctc_key->l4_type_mask) & 0xF;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_DSCP))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->dscp, 0x3F);
        p_sys_key->dscp      = p_ctc_key->dscp;
        p_sys_key->dscp_mask = (p_ctc_key->dscp_mask) & 0x3F;
    }
    else if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_PRECEDENCE))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->dscp, 7);

        p_sys_key->dscp      = (p_ctc_key->dscp) << 3;
        p_sys_key->dscp_mask = (p_ctc_key->dscp_mask & 0x7) << 3;
#if 0
        p_sys_key->dscp_mask = 0x38; /* mask = 111000, that is 0x38 */
#endif /* _if0_ */
    }


    p_sys_key->flag     = flag;
    p_sys_key->sub_flag = sub_flag;

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_scl_map_tcam_vlan_key(uint8 lchip, ctc_scl_tcam_vlan_key_t* p_ctc_key,
                                     sys_scl_sw_tcam_key_vlan_t* p_sys_key)
{
    uint32 flag = 0;

    CTC_PTR_VALID_CHECK(p_ctc_key);
    CTC_PTR_VALID_CHECK(p_sys_key);

    SYS_SCL_DBG_FUNC();

    flag = p_ctc_key->flag;

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_VLAN_KEY_FLAG_CVLAN) \
        && CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_VLAN_KEY_FLAG_CTAG_COS) \
        && CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_VLAN_KEY_FLAG_CTAG_CFI) \
        && CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_VLAN_KEY_FLAG_STAG_COS) \
        && CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_VLAN_KEY_FLAG_STAG_CFI) \
        && CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_VLAN_KEY_FLAG_CUSTOMER_ID))
    {
        return CTC_E_INVALID_PARAM;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_VLAN_KEY_FLAG_CUSTOMER_ID))
    {
        p_sys_key->customer_id = p_ctc_key->customer_id;
        p_sys_key->customer_id_mask = p_ctc_key->customer_id_mask;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_VLAN_KEY_FLAG_GPORT))
    {
        p_sys_key->gport = p_ctc_key->gport;
        p_sys_key->gport_mask = p_ctc_key->gport_mask;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_VLAN_KEY_FLAG_CVLAN))
    {
        SYS_SCL_VLAN_ID_CHECK(p_ctc_key->cvlan);
        p_sys_key->cvlan      = p_ctc_key->cvlan;
        p_sys_key->cvlan_mask = p_ctc_key->cvlan_mask & 0xFFF;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_VLAN_KEY_FLAG_CTAG_COS))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->ctag_cos, 0x7);
        p_sys_key->ctag_cos      = p_ctc_key->ctag_cos;
        p_sys_key->ctag_cos_mask = (p_ctc_key->ctag_cos_mask) & 0x7;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_VLAN_KEY_FLAG_CTAG_CFI))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->ctag_cfi, 0x1);
        p_sys_key->ctag_cfi = p_ctc_key->ctag_cfi;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_VLAN_KEY_FLAG_SVLAN))
    {
        SYS_SCL_VLAN_ID_CHECK(p_ctc_key->svlan);
        p_sys_key->svlan      = p_ctc_key->svlan;
        p_sys_key->svlan_mask = p_ctc_key->svlan_mask & 0xFFF;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_VLAN_KEY_FLAG_STAG_COS))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->stag_cos, 0x7);
        p_sys_key->stag_cos      = p_ctc_key->stag_cos;
        p_sys_key->stag_cos_mask = (p_ctc_key->stag_cos_mask) & 0x7;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_VLAN_KEY_FLAG_STAG_CFI))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->stag_cfi, 0x1);
        p_sys_key->stag_cfi = p_ctc_key->stag_cfi;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_VLAN_KEY_FLAG_CTAG_VALID))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->stag_valid, 0x1);
        p_sys_key->stag_valid = p_ctc_key->stag_valid;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_VLAN_KEY_FLAG_STAG_VALID))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->ctag_valid, 0x1);
        p_sys_key->ctag_valid = p_ctc_key->ctag_valid;
    }

    p_sys_key->flag = flag;

    return CTC_E_NONE;
}



STATIC int32
_sys_greatbelt_scl_map_tcam_ipv6_key(uint8 lchip, ctc_scl_tcam_ipv6_key_t* p_ctc_key,
                                     sys_scl_sw_tcam_key_ipv6_t* p_sys_key)
{
    uint32 flag     = 0;
    uint32 sub_flag = 0;

    CTC_PTR_VALID_CHECK(p_ctc_key);
    CTC_PTR_VALID_CHECK(p_sys_key);

    SYS_SCL_DBG_FUNC();

    flag     = p_ctc_key->flag;
    sub_flag = p_ctc_key->sub_flag;

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_DSCP)
        && CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_PRECEDENCE))
    {
        return CTC_E_SCL_FLAG_COLLISION;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_IP_DA))
    {
        sal_memcpy(p_sys_key->ip_da, p_ctc_key->ip_da, sizeof(ipv6_addr_t));
        sal_memcpy(p_sys_key->ip_da_mask, p_ctc_key->ip_da_mask, sizeof(ipv6_addr_t));
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_IP_SA))
    {
        sal_memcpy(p_sys_key->ip_sa, p_ctc_key->ip_sa, sizeof(ipv6_addr_t));
        sal_memcpy(p_sys_key->ip_sa_mask, p_ctc_key->ip_sa_mask, sizeof(ipv6_addr_t));
    }

    /* layer 4 information */
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_L4_PROTOCOL))
    {
        CTC_SET_FLAG(sub_flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_L4_PROTOCOL);
        if ((0xFF == p_ctc_key->l4_protocol_mask)
            && (SYS_SCL_IPV6_ICMP == p_ctc_key->l4_protocol))        /* ICMP */
        {
            p_sys_key->flag_l4info_mapped = 1;
            p_sys_key->l4info_mapped      = p_ctc_key->l4_protocol;
            p_sys_key->l4info_mapped_mask = p_ctc_key->l4_protocol_mask;

            p_sys_key->flag_is_tcp = 1;
            p_sys_key->is_tcp      = 0;

            p_sys_key->flag_is_udp = 1;
            p_sys_key->is_udp      = 0;

            /* icmp type */
            if (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV6_KEY_SUB_FLAG_ICMP_TYPE))
            {
                CTC_SET_FLAG(sub_flag, CTC_SCL_TCAM_IPV6_KEY_SUB_FLAG_L4_SRC_PORT);
                p_sys_key->l4_src_port       = (p_ctc_key->icmp_type << 8) & 0xFF00;
                p_sys_key->l4_src_port_mask |= (p_ctc_key->icmp_type_mask << 8) & 0xFF00;
            }

            /* icmp code */
            if (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV6_KEY_SUB_FLAG_ICMP_CODE))
            {
                CTC_SET_FLAG(sub_flag, CTC_SCL_TCAM_IPV6_KEY_SUB_FLAG_L4_SRC_PORT);
                p_sys_key->l4_src_port      |= p_ctc_key->icmp_code & 0x00FF;
                p_sys_key->l4_src_port_mask |= p_ctc_key->icmp_code_mask & 0x00FF;
            }
        }
        else if ((0xFF == p_ctc_key->l4_protocol_mask)
                 && ((SYS_SCL_TCP == p_ctc_key->l4_protocol)
                     || (SYS_SCL_UDP == p_ctc_key->l4_protocol))) /* TCP or UDP */
        {
            if (SYS_SCL_TCP == p_ctc_key->l4_protocol)
            {   /* TCP */
                p_sys_key->flag_is_tcp = 1;
                p_sys_key->is_tcp      = 1;
            }
            else
            {   /* UDP */
                p_sys_key->flag_is_udp = 1;
                p_sys_key->is_udp      = 1;
            }

            if (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV6_KEY_SUB_FLAG_L4_SRC_PORT))
            {
                p_sys_key->l4_src_port      = p_ctc_key->l4_src_port;
                p_sys_key->l4_src_port_mask = p_ctc_key->l4_src_port_mask;
            }

            if (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV6_KEY_SUB_FLAG_L4_DST_PORT))
            {
                p_sys_key->l4_dst_port      = p_ctc_key->l4_dst_port;
                p_sys_key->l4_dst_port_mask = p_ctc_key->l4_dst_port_mask;
            }
        }
        else    /* other layer 4 protocol type */
        {
            p_sys_key->flag_l4info_mapped = 1;
            p_sys_key->l4info_mapped      = p_ctc_key->l4_protocol;
            p_sys_key->l4info_mapped_mask = p_ctc_key->l4_protocol_mask;

            p_sys_key->flag_is_tcp = 1;
            p_sys_key->is_tcp      = 0;

            p_sys_key->flag_is_udp = 1;
            p_sys_key->is_udp      = 0;
        }
    }

    /* ip fragment */
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_IP_FRAG))
    {
        uint8 d;
        uint8 m;
        CTC_ERROR_RETURN(_sys_greatbelt_scl_get_ip_frag(lchip, p_ctc_key->ip_frag, &d, &m));
        p_sys_key->frag_info      = d;
        p_sys_key->frag_info_mask = m;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_DSCP))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->dscp, 0x3F);
        p_sys_key->dscp      = p_ctc_key->dscp;
        p_sys_key->dscp_mask = p_ctc_key->dscp_mask;
    }
    else if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_PRECEDENCE))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->dscp, 7);
        p_sys_key->dscp      = (p_ctc_key->dscp) << 3;
        p_sys_key->dscp_mask = (p_ctc_key->dscp_mask & 0x7) << 3;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_IP_OPTION))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->ip_option, 1);
        p_sys_key->ip_option = p_ctc_key->ip_option;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_FLOW_LABEL))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->flow_label, 0xFFFFF);
        p_sys_key->flow_label      = p_ctc_key->flow_label;
        p_sys_key->flow_label_mask = p_ctc_key->flow_label_mask & 0xFFFFF;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_MAC_DA))
    {
        sal_memcpy(p_sys_key->mac_da, p_ctc_key->mac_da, CTC_ETH_ADDR_LEN);
        sal_memcpy(p_sys_key->mac_da_mask, p_ctc_key->mac_da_mask, CTC_ETH_ADDR_LEN);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_MAC_SA))
    {
        sal_memcpy(p_sys_key->mac_sa, p_ctc_key->mac_sa, CTC_ETH_ADDR_LEN);
        sal_memcpy(p_sys_key->mac_sa_mask, p_ctc_key->mac_sa_mask, CTC_ETH_ADDR_LEN);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_CVLAN))
    {
        SYS_SCL_VLAN_ID_CHECK(p_ctc_key->cvlan);
        p_sys_key->cvlan      = p_ctc_key->cvlan;
        p_sys_key->cvlan_mask = p_ctc_key->cvlan_mask & 0xFFF;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_CTAG_COS))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->ctag_cos, 0x7);
        p_sys_key->ctag_cos      = p_ctc_key->ctag_cos;
        p_sys_key->ctag_cos_mask = (p_ctc_key->ctag_cos_mask) & 0x7;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_CTAG_CFI))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->ctag_cfi, 0x1);
        p_sys_key->ctag_cfi = p_ctc_key->ctag_cfi;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_SVLAN))
    {
        SYS_SCL_VLAN_ID_CHECK(p_ctc_key->svlan);
        p_sys_key->svlan      = p_ctc_key->svlan;
        p_sys_key->svlan_mask = p_ctc_key->svlan_mask & 0xFFF;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_STAG_COS))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->stag_cos, 0x7);
        p_sys_key->stag_cos      = p_ctc_key->stag_cos;
        p_sys_key->stag_cos_mask = (p_ctc_key->stag_cos_mask) & 0x7;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_STAG_CFI))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->stag_cfi, 0x1);
        p_sys_key->stag_cfi = p_ctc_key->stag_cfi;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_ETH_TYPE))
    {
        p_sys_key->eth_type      = p_ctc_key->eth_type;
        p_sys_key->eth_type_mask = p_ctc_key->eth_type_mask;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_L2_TYPE))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->l2_type, 0xF);
        p_sys_key->l2_type      = p_ctc_key->l2_type;
        p_sys_key->l2_type_mask = p_ctc_key->l2_type_mask;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_L3_TYPE))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->l3_type, 0xF);
        p_sys_key->l3_type      = p_ctc_key->l3_type;
        p_sys_key->l3_type_mask = p_ctc_key->l3_type_mask;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_L4_TYPE))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->l4_type, 0xF);
        p_sys_key->l4_type      = p_ctc_key->l4_type;
        p_sys_key->l4_type_mask = p_ctc_key->l4_type_mask;
    }

    p_sys_key->flag     = flag;
    p_sys_key->sub_flag = sub_flag;

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_scl_map_hash_port_key(uint8 lchip, ctc_scl_hash_port_key_t* p_ctc_key,
                                     sys_scl_sw_hash_key_port_t* p_sys_key)
{
    CTC_PTR_VALID_CHECK(p_ctc_key);
    CTC_PTR_VALID_CHECK(p_sys_key);


    /* dir */
    CTC_MAX_VALUE_CHECK(p_ctc_key->dir, CTC_BOTH_DIRECTION - 1);
    p_sys_key->dir = p_ctc_key->dir;

    /* gport is group */
    CTC_MAX_VALUE_CHECK(p_ctc_key->gport_type, 2);
    p_sys_key->gport_is_classid = (CTC_SCL_GPROT_TYPE_PORT_CLASS == p_ctc_key->gport_type);
    p_sys_key->gport_is_logic   = (2 == p_ctc_key->gport_type);

    if (!p_ctc_key->gport_type)
    {
        /* gport */
        CTC_GLOBAL_PORT_CHECK(p_ctc_key->gport);
    }
    else if (CTC_SCL_GPROT_TYPE_PORT_CLASS == p_ctc_key->gport_type)
    {
        SYS_SCL_PORT_CLASS_ID_CHECK(p_ctc_key->gport);
    }
    else
    {/*logic port*/
        SYS_LOGIC_PORT_CHECK(p_ctc_key->gport);
    }
    p_sys_key->gport = p_ctc_key->gport;

    return CTC_E_NONE;
}
STATIC int32
_sys_greatbelt_scl_map_hash_port_cvlan_key(uint8 lchip, ctc_scl_hash_port_cvlan_key_t* p_ctc_key,
                                           sys_scl_sw_hash_key_vlan_t* p_sys_key)
{
    CTC_PTR_VALID_CHECK(p_ctc_key);
    CTC_PTR_VALID_CHECK(p_sys_key);

    /* gport is group */
    CTC_MAX_VALUE_CHECK(p_ctc_key->gport_type, 2);
    p_sys_key->gport_is_classid = (CTC_SCL_GPROT_TYPE_PORT_CLASS == p_ctc_key->gport_type);
    p_sys_key->gport_is_logic   = (2 == p_ctc_key->gport_type);

    if (!p_ctc_key->gport_type)
    {
        /* gport */
        CTC_GLOBAL_PORT_CHECK(p_ctc_key->gport);
    }
    else if (CTC_SCL_GPROT_TYPE_PORT_CLASS == p_ctc_key->gport_type)
    {
        SYS_SCL_PORT_CLASS_ID_CHECK(p_ctc_key->gport);
    }
    else
    {/*logic port*/
        SYS_LOGIC_PORT_CHECK(p_ctc_key->gport);
    }
    p_sys_key->gport = p_ctc_key->gport;


    /* vid */
    SYS_SCL_VLAN_ID_CHECK(p_ctc_key->cvlan);
    p_sys_key->vid = p_ctc_key->cvlan;

    /* dir */
    CTC_MAX_VALUE_CHECK(p_ctc_key->dir, CTC_BOTH_DIRECTION - 1);
    p_sys_key->dir = p_ctc_key->dir;

    return CTC_E_NONE;
}
STATIC int32
_sys_greatbelt_scl_map_hash_port_svlan_key(uint8 lchip, ctc_scl_hash_port_svlan_key_t* p_ctc_key,
                                           sys_scl_sw_hash_key_vlan_t* p_sys_key)
{
    CTC_PTR_VALID_CHECK(p_ctc_key);
    CTC_PTR_VALID_CHECK(p_sys_key);

    /* gport is group */
    CTC_MAX_VALUE_CHECK(p_ctc_key->gport_type, 2);
    p_sys_key->gport_is_classid = (CTC_SCL_GPROT_TYPE_PORT_CLASS == p_ctc_key->gport_type);
    p_sys_key->gport_is_logic   = (2 == p_ctc_key->gport_type);
    if (!p_ctc_key->gport_type)
    {
        /* gport */
        CTC_GLOBAL_PORT_CHECK(p_ctc_key->gport);
    }
    else if (CTC_SCL_GPROT_TYPE_PORT_CLASS == p_ctc_key->gport_type)
    {
        SYS_SCL_PORT_CLASS_ID_CHECK(p_ctc_key->gport);
    }
    else
    {/*logic port*/
        SYS_LOGIC_PORT_CHECK(p_ctc_key->gport);
    }
    p_sys_key->gport = p_ctc_key->gport;


    /* vid */
    SYS_SCL_VLAN_ID_CHECK(p_ctc_key->svlan);
    p_sys_key->vid = p_ctc_key->svlan;

    /* dir */
    CTC_MAX_VALUE_CHECK(p_ctc_key->dir, CTC_BOTH_DIRECTION - 1);
    p_sys_key->dir = p_ctc_key->dir;

    return CTC_E_NONE;
}
STATIC int32
_sys_greatbelt_scl_map_hash_port_2vlan_key(uint8 lchip, ctc_scl_hash_port_2vlan_key_t* p_ctc_key,
                                           sys_scl_sw_hash_key_vtag_t* p_sys_key)
{
    CTC_PTR_VALID_CHECK(p_ctc_key);
    CTC_PTR_VALID_CHECK(p_sys_key);

    /* gport is group */
    CTC_MAX_VALUE_CHECK(p_ctc_key->gport_type, 2);
    p_sys_key->gport_is_classid = (CTC_SCL_GPROT_TYPE_PORT_CLASS == p_ctc_key->gport_type);
    p_sys_key->gport_is_logic   = (2 == p_ctc_key->gport_type);

    if (!p_ctc_key->gport_type)
    {
        /* gport */
        CTC_GLOBAL_PORT_CHECK(p_ctc_key->gport);
    }
    else if (CTC_SCL_GPROT_TYPE_PORT_CLASS == p_ctc_key->gport_type)
    {
        SYS_SCL_PORT_CLASS_ID_CHECK(p_ctc_key->gport);
    }
    else
    {/*logic port*/
        SYS_LOGIC_PORT_CHECK(p_ctc_key->gport);
    }
    p_sys_key->gport = p_ctc_key->gport;

    /* vid */
    SYS_SCL_VLAN_ID_CHECK(p_ctc_key->svlan);
    SYS_SCL_VLAN_ID_CHECK(p_ctc_key->cvlan);
    p_sys_key->svid = p_ctc_key->svlan;
    p_sys_key->cvid = p_ctc_key->cvlan;

    /* dir */
    CTC_MAX_VALUE_CHECK(p_ctc_key->dir, CTC_BOTH_DIRECTION - 1);
    p_sys_key->dir = p_ctc_key->dir;

    return CTC_E_NONE;
}
STATIC int32
_sys_greatbelt_scl_map_hash_port_cvlan_cos_key(uint8 lchip, ctc_scl_hash_port_cvlan_cos_key_t* p_ctc_key,
                                               sys_scl_sw_hash_key_vtag_t* p_sys_key)
{
    CTC_PTR_VALID_CHECK(p_ctc_key);
    CTC_PTR_VALID_CHECK(p_sys_key);

    /* gport is group */
    CTC_MAX_VALUE_CHECK(p_ctc_key->gport_type, 2);
    p_sys_key->gport_is_classid = (CTC_SCL_GPROT_TYPE_PORT_CLASS == p_ctc_key->gport_type);
    p_sys_key->gport_is_logic   = (2 == p_ctc_key->gport_type);

    if (!p_ctc_key->gport_type)
    {
        /* gport */
        CTC_GLOBAL_PORT_CHECK(p_ctc_key->gport);
    }
    else if (CTC_SCL_GPROT_TYPE_PORT_CLASS == p_ctc_key->gport_type)
    {
        SYS_SCL_PORT_CLASS_ID_CHECK(p_ctc_key->gport);
    }
    else
    {/*logic port*/
        SYS_LOGIC_PORT_CHECK(p_ctc_key->gport);
    }
    p_sys_key->gport = p_ctc_key->gport;

    /* vid */
    SYS_SCL_VLAN_ID_CHECK(p_ctc_key->cvlan);
    p_sys_key->cvid = p_ctc_key->cvlan;

    /* cos */
    CTC_MAX_VALUE_CHECK(p_ctc_key->ctag_cos, 0x7);
    p_sys_key->ccos = p_ctc_key->ctag_cos;

    /* dir */
    CTC_MAX_VALUE_CHECK(p_ctc_key->dir, CTC_BOTH_DIRECTION - 1);
    p_sys_key->dir = p_ctc_key->dir;

    return CTC_E_NONE;
}
STATIC int32
_sys_greatbelt_scl_map_hash_port_svlan_cos_key(uint8 lchip, ctc_scl_hash_port_svlan_cos_key_t* p_ctc_key,
                                               sys_scl_sw_hash_key_vtag_t* p_sys_key)
{
    CTC_PTR_VALID_CHECK(p_ctc_key);
    CTC_PTR_VALID_CHECK(p_sys_key);

    /* gport is group */
    CTC_MAX_VALUE_CHECK(p_ctc_key->gport_type, 2);
    p_sys_key->gport_is_classid = (CTC_SCL_GPROT_TYPE_PORT_CLASS == p_ctc_key->gport_type);
    p_sys_key->gport_is_logic   = (2 == p_ctc_key->gport_type);
    if (!p_ctc_key->gport_type)
    {
        /* gport */
        CTC_GLOBAL_PORT_CHECK(p_ctc_key->gport);
    }
    else if (CTC_SCL_GPROT_TYPE_PORT_CLASS == p_ctc_key->gport_type)
    {
        SYS_SCL_PORT_CLASS_ID_CHECK(p_ctc_key->gport);
    }
    else
    {/*logic port*/
        SYS_LOGIC_PORT_CHECK(p_ctc_key->gport);
    }
    p_sys_key->gport = p_ctc_key->gport;

    /* vid */
    SYS_SCL_VLAN_ID_CHECK(p_ctc_key->svlan);
    p_sys_key->svid = p_ctc_key->svlan;

    /* cos */
    CTC_MAX_VALUE_CHECK(p_ctc_key->stag_cos, 0x7);
    p_sys_key->scos = p_ctc_key->stag_cos;

    /* dir */
    CTC_MAX_VALUE_CHECK(p_ctc_key->dir, CTC_BOTH_DIRECTION - 1);
    p_sys_key->dir = p_ctc_key->dir;

    return CTC_E_NONE;
}
STATIC int32
_sys_greatbelt_scl_map_hash_mac_key(uint8 lchip, ctc_scl_hash_mac_key_t* p_ctc_key,
                                    sys_scl_sw_hash_key_mac_t* p_sys_key)
{
    CTC_PTR_VALID_CHECK(p_ctc_key);
    CTC_PTR_VALID_CHECK(p_sys_key);

    /* mac */
    SYS_SCL_SET_MAC_HIGH(p_sys_key->macsa_h, p_ctc_key->mac);
    SYS_SCL_SET_MAC_LOW(p_sys_key->macsa_l, p_ctc_key->mac);

    /* mac is da */
    CTC_MAX_VALUE_CHECK(p_ctc_key->mac_is_da, 1);
    p_sys_key->use_macda = p_ctc_key->mac_is_da;

    return CTC_E_NONE;
}
STATIC int32
_sys_greatbelt_scl_map_hash_port_mac_key(uint8 lchip, ctc_scl_hash_port_mac_key_t* p_ctc_key,
                                         sys_scl_sw_hash_key_mac_t* p_sys_key)
{
    CTC_PTR_VALID_CHECK(p_ctc_key);
    CTC_PTR_VALID_CHECK(p_sys_key);

    /* gport is group */
    CTC_MAX_VALUE_CHECK(p_ctc_key->gport_type, 1);
    p_sys_key->gport_is_classid = p_ctc_key->gport_type;

    if (!p_ctc_key->gport_type)
    {
        /* gport */
        CTC_GLOBAL_PORT_CHECK(p_ctc_key->gport);
    }
    else
    {
        SYS_SCL_PORT_CLASS_ID_CHECK(p_ctc_key->gport);
    }
    p_sys_key->gport = p_ctc_key->gport;

    /* mac */
    SYS_SCL_SET_MAC_HIGH(p_sys_key->macsa_h, p_ctc_key->mac);
    SYS_SCL_SET_MAC_LOW(p_sys_key->macsa_l, p_ctc_key->mac);

    /* mac is da */
    CTC_MAX_VALUE_CHECK(p_ctc_key->mac_is_da, 1);
    p_sys_key->use_macda = p_ctc_key->mac_is_da;

    return CTC_E_NONE;
}



STATIC int32
_sys_greatbelt_scl_map_hash_ipv4_key(uint8 lchip, ctc_scl_hash_ipv4_key_t* p_ctc_key,
                                     sys_scl_sw_hash_key_ipv4_t* p_sys_key)
{
    CTC_PTR_VALID_CHECK(p_ctc_key);
    CTC_PTR_VALID_CHECK(p_sys_key);

    /* ip_sa */
    p_sys_key->ip_sa = p_ctc_key->ip_sa;

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_scl_map_hash_port_ipv4_key(uint8 lchip, ctc_scl_hash_port_ipv4_key_t* p_ctc_key,
                                          sys_scl_sw_hash_key_ipv4_t* p_sys_key)
{
    CTC_PTR_VALID_CHECK(p_ctc_key);
    CTC_PTR_VALID_CHECK(p_sys_key);

    /* ip_sa */
    p_sys_key->ip_sa = p_ctc_key->ip_sa;

    /* gport is group */
    CTC_MAX_VALUE_CHECK(p_ctc_key->gport_type, 1);
    p_sys_key->gport_is_classid = p_ctc_key->gport_type;

    if (!p_ctc_key->gport_type)
    {
        /* gport */
        CTC_GLOBAL_PORT_CHECK(p_ctc_key->gport);
    }
    else
    {
        SYS_SCL_PORT_CLASS_ID_CHECK(p_ctc_key->gport);
    }
    p_sys_key->gport = p_ctc_key->gport;
    p_sys_key->rsv0 = p_ctc_key->rsv0;

    return CTC_E_NONE;
}
STATIC int32
_sys_greatbelt_scl_map_hash_ipv6_key(uint8 lchip, ctc_scl_hash_ipv6_key_t* p_ctc_key,
                                     sys_scl_sw_hash_key_ipv6_t* p_sys_key)
{
    CTC_PTR_VALID_CHECK(p_ctc_key);
    CTC_PTR_VALID_CHECK(p_sys_key);

    /* ip sa */
    sal_memcpy(p_sys_key->ip_sa, p_ctc_key->ip_sa, sizeof(ipv6_addr_t));

    return CTC_E_NONE;
}


STATIC int32
_sys_greatbelt_scl_map_hash_l2_key(uint8 lchip, ctc_scl_hash_l2_key_t* p_ctc_key,
                                   sys_scl_sw_hash_key_l2_t* p_sys_key)
{
    uint32  cmd               = 0;
    user_id_hash_lookup_ctl_t user_id_hash_lookup_ctl;

    sal_memset(&user_id_hash_lookup_ctl, 0, sizeof(user_id_hash_lookup_ctl));
    cmd = DRV_IOR(UserIdHashLookupCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &user_id_hash_lookup_ctl));


    CTC_PTR_VALID_CHECK(p_ctc_key);
    CTC_PTR_VALID_CHECK(p_sys_key);

    /* cos */
    if (user_id_hash_lookup_ctl.l2_cos_en == 1)
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->cos, 7);

        p_sys_key->cos = p_ctc_key->cos;
    }
    else
    {
        p_sys_key->cos = 0;
    }

    /* mac */
    SYS_SCL_SET_MAC_HIGH(p_sys_key->macsa_h, p_ctc_key->mac_sa);
    SYS_SCL_SET_MAC_LOW(p_sys_key->macsa_l, p_ctc_key->mac_sa);

    SYS_SCL_SET_MAC_HIGH(p_sys_key->macda_h, p_ctc_key->mac_da);
    SYS_SCL_SET_MAC_LOW(p_sys_key->macda_l, p_ctc_key->mac_da);

    /* vlan id */
    SYS_SCL_VLAN_ID_CHECK(p_ctc_key->vlan_id);
    p_sys_key->vid = p_ctc_key->vlan_id;

    /* gport */
    CTC_GLOBAL_PORT_CHECK(p_ctc_key->gport);
    p_sys_key->gport = p_ctc_key->gport;

    return CTC_E_NONE;
}

/* Tunnel key just memory copy */

STATIC int32
_sys_greatbelt_scl_map_tcam_tunnel_ipv4_key(uint8 lchip, sys_scl_tcam_tunnel_ipv4_key_t* p_ctc_key,
                                            sys_scl_tcam_tunnel_ipv4_key_t* p_sys_key)
{
    CTC_PTR_VALID_CHECK(p_ctc_key);
    CTC_PTR_VALID_CHECK(p_sys_key);

    sal_memcpy(p_sys_key, p_ctc_key, sizeof(sys_scl_tcam_tunnel_ipv4_key_t));

    return CTC_E_NONE;
}
STATIC int32
_sys_greatbelt_scl_map_tcam_tunnel_ipv6_key(uint8 lchip, sys_scl_tcam_tunnel_ipv6_key_t* p_ctc_key,
                                            sys_scl_tcam_tunnel_ipv6_key_t* p_sys_key)
{
    CTC_PTR_VALID_CHECK(p_ctc_key);
    CTC_PTR_VALID_CHECK(p_sys_key);

    sal_memcpy(p_sys_key, p_ctc_key, sizeof(sys_scl_tcam_tunnel_ipv6_key_t));

    return CTC_E_NONE;
}
STATIC int32
_sys_greatbelt_scl_map_hash_tunnel_ipv4_key(uint8 lchip, sys_scl_hash_tunnel_ipv4_key_t* p_ctc_key,
                                            sys_scl_hash_tunnel_ipv4_key_t* p_sys_key)
{
    CTC_PTR_VALID_CHECK(p_ctc_key);
    CTC_PTR_VALID_CHECK(p_sys_key);

    sal_memcpy(p_sys_key, p_ctc_key, sizeof(sys_scl_hash_tunnel_ipv4_key_t));

    return CTC_E_NONE;
}
STATIC int32
_sys_greatbelt_scl_map_hash_tunnel_ipv4_gre_key(uint8 lchip, sys_scl_hash_tunnel_ipv4_gre_key_t* p_ctc_key,
                                                sys_scl_hash_tunnel_ipv4_gre_key_t* p_sys_key)
{
    CTC_PTR_VALID_CHECK(p_ctc_key);
    CTC_PTR_VALID_CHECK(p_sys_key);

    sal_memcpy(p_sys_key, p_ctc_key, sizeof(sys_scl_hash_tunnel_ipv4_gre_key_t));

    return CTC_E_NONE;
}
STATIC int32
_sys_greatbelt_scl_map_hash_tunnel_ipv4_rpf_key(uint8 lchip, sys_scl_hash_tunnel_ipv4_rpf_key_t* p_ctc_key,
                                                sys_scl_hash_tunnel_ipv4_rpf_key_t* p_sys_key)
{
    CTC_PTR_VALID_CHECK(p_ctc_key);
    CTC_PTR_VALID_CHECK(p_sys_key);

    sal_memcpy(p_sys_key, p_ctc_key, sizeof(sys_scl_hash_tunnel_ipv4_rpf_key_t));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_scl_map_port_default_key(uint8 lchip, sys_scl_port_default_key_t* p_ctc_key,
                                        sys_scl_sw_hash_key_port_t* p_sys_key)
{
    uint8 gchip = 0;
    uint8 lport = 0;

    CTC_PTR_VALID_CHECK(p_ctc_key);
    CTC_PTR_VALID_CHECK(p_sys_key);

    gchip = CTC_MAP_GPORT_TO_GCHIP(p_ctc_key->gport);
    lport = CTC_MAP_GPORT_TO_LPORT(p_ctc_key->gport);
    if (!sys_greatbelt_chip_is_local(lchip, gchip))
    {
        return CTC_E_NONE;
    }
    if (lport >= SYS_SCL_DEFAULT_ENTRY_PORT_NUM)
    {
        return CTC_E_SCL_INVALID_DEFAULT_PORT;
    }

    p_sys_key->gport = p_ctc_key->gport;

    return CTC_E_NONE;
}

/*
 * get sys key based on ctc key
 */
STATIC int32
_sys_greatbelt_scl_map_key(uint8 lchip, sys_scl_key_t* p_ctc_key,
                           sys_scl_sw_key_t* p_sys_key)
{

    CTC_PTR_VALID_CHECK(p_ctc_key);
    CTC_PTR_VALID_CHECK(p_sys_key);

    SYS_SCL_DBG_FUNC();

    p_sys_key->type = p_ctc_key->type;

    switch (p_ctc_key->type)
    {
    case SYS_SCL_KEY_TCAM_MAC:
        CTC_ERROR_RETURN(
            _sys_greatbelt_scl_map_tcam_mac_key(lchip, &p_ctc_key->u.tcam_mac_key, &p_sys_key->u0.mac_key));
        break;

    case SYS_SCL_KEY_TCAM_IPV4:
        CTC_ERROR_RETURN(
            _sys_greatbelt_scl_map_tcam_ipv4_key(lchip, &p_ctc_key->u.tcam_ipv4_key, &p_sys_key->u0.ipv4_key));
        break;

    case SYS_SCL_KEY_TCAM_VLAN:
        CTC_ERROR_RETURN(
            _sys_greatbelt_scl_map_tcam_vlan_key(lchip, &p_ctc_key->u.tcam_vlan_key, &p_sys_key->u0.vlan_key));
        break;

    case SYS_SCL_KEY_TCAM_IPV6:
        CTC_ERROR_RETURN(
            _sys_greatbelt_scl_map_tcam_ipv6_key(lchip, &p_ctc_key->u.tcam_ipv6_key, &p_sys_key->u0.ipv6_key));
        break;

    case SYS_SCL_KEY_HASH_MAC:
        CTC_ERROR_RETURN(
            _sys_greatbelt_scl_map_hash_mac_key(lchip, &p_ctc_key->u.hash_mac_key, &p_sys_key->u0.hash.u0.mac));
        break;

    case SYS_SCL_KEY_HASH_IPV4:
        CTC_ERROR_RETURN(
            _sys_greatbelt_scl_map_hash_ipv4_key(lchip, &p_ctc_key->u.hash_ipv4_key, &p_sys_key->u0.hash.u0.ipv4));
        break;

    case SYS_SCL_KEY_TCAM_TUNNEL_IPV4:
        CTC_ERROR_RETURN(
            _sys_greatbelt_scl_map_tcam_tunnel_ipv4_key(lchip, &p_ctc_key->u.tcam_tunnel_ipv4_key, &p_sys_key->u0.tnnl_ipv4_key));
        break;
    case SYS_SCL_KEY_TCAM_TUNNEL_IPV6:
        CTC_ERROR_RETURN(
            _sys_greatbelt_scl_map_tcam_tunnel_ipv6_key(lchip, &p_ctc_key->u.tcam_tunnel_ipv6_key, &p_sys_key->u0.tnnl_ipv6_key));
        break;
    case SYS_SCL_KEY_HASH_PORT:
        CTC_ERROR_RETURN(
            _sys_greatbelt_scl_map_hash_port_key(lchip, &p_ctc_key->u.hash_port_key, &p_sys_key->u0.hash.u0.port));
        break;
    case SYS_SCL_KEY_HASH_PORT_CVLAN:
        CTC_ERROR_RETURN(
            _sys_greatbelt_scl_map_hash_port_cvlan_key(lchip, &p_ctc_key->u.hash_port_cvlan_key, &p_sys_key->u0.hash.u0.vlan));
        break;
    case SYS_SCL_KEY_HASH_PORT_SVLAN:
        CTC_ERROR_RETURN(
            _sys_greatbelt_scl_map_hash_port_svlan_key(lchip, &p_ctc_key->u.hash_port_svlan_key, &p_sys_key->u0.hash.u0.vlan));
        break;
    case SYS_SCL_KEY_HASH_PORT_2VLAN:
        CTC_ERROR_RETURN(
            _sys_greatbelt_scl_map_hash_port_2vlan_key(lchip, &p_ctc_key->u.hash_port_2vlan_key, &p_sys_key->u0.hash.u0.vtag));
        break;
    case SYS_SCL_KEY_HASH_PORT_CVLAN_COS:
        CTC_ERROR_RETURN(
            _sys_greatbelt_scl_map_hash_port_cvlan_cos_key(lchip, &p_ctc_key->u.hash_port_cvlan_cos_key, &p_sys_key->u0.hash.u0.vtag));
        break;
    case SYS_SCL_KEY_HASH_PORT_SVLAN_COS:
        CTC_ERROR_RETURN(
            _sys_greatbelt_scl_map_hash_port_svlan_cos_key(lchip, &p_ctc_key->u.hash_port_svlan_cos_key, &p_sys_key->u0.hash.u0.vtag));
        break;
    case SYS_SCL_KEY_HASH_PORT_MAC:
        CTC_ERROR_RETURN(
            _sys_greatbelt_scl_map_hash_port_mac_key(lchip, &p_ctc_key->u.hash_port_mac_key, &p_sys_key->u0.hash.u0.mac));
        break;
    case SYS_SCL_KEY_HASH_PORT_IPV4:
        CTC_ERROR_RETURN(
            _sys_greatbelt_scl_map_hash_port_ipv4_key(lchip, &p_ctc_key->u.hash_port_ipv4_key, &p_sys_key->u0.hash.u0.ipv4));
        break;
    case SYS_SCL_KEY_HASH_IPV6:
        CTC_ERROR_RETURN(
            _sys_greatbelt_scl_map_hash_ipv6_key(lchip, &p_ctc_key->u.hash_ipv6_key, &p_sys_key->u0.hash.u0.ipv6));
        break;
    case SYS_SCL_KEY_HASH_L2:
        CTC_ERROR_RETURN(
            _sys_greatbelt_scl_map_hash_l2_key(lchip, &p_ctc_key->u.hash_l2_key, &p_sys_key->u0.hash.u0.l2));
        break;
    case SYS_SCL_KEY_HASH_TUNNEL_IPV4:
        CTC_ERROR_RETURN(
            _sys_greatbelt_scl_map_hash_tunnel_ipv4_key(lchip, &p_ctc_key->u.hash_tunnel_ipv4_key, &p_sys_key->u0.hash.u0.tnnl_ipv4));
        break;
    case SYS_SCL_KEY_HASH_TUNNEL_IPV4_GRE:
        CTC_ERROR_RETURN(
            _sys_greatbelt_scl_map_hash_tunnel_ipv4_gre_key(lchip, &p_ctc_key->u.hash_tunnel_ipv4_gre_key, &p_sys_key->u0.hash.u0.tnnl_ipv4_gre));
        break;
    case SYS_SCL_KEY_HASH_TUNNEL_IPV4_RPF:
        CTC_ERROR_RETURN(
            _sys_greatbelt_scl_map_hash_tunnel_ipv4_rpf_key(lchip, &p_ctc_key->u.hash_tunnel_ipv4_rpf_key, &p_sys_key->u0.hash.u0.tnnl_ipv4_rpf));
        break;
    case SYS_SCL_KEY_PORT_DEFAULT_INGRESS:
    case SYS_SCL_KEY_PORT_DEFAULT_EGRESS:
        CTC_ERROR_RETURN(
            _sys_greatbelt_scl_map_port_default_key(lchip, &p_ctc_key->u.port_default_key, &p_sys_key->u0.hash.u0.port));
        break;

    default:
        return CTC_E_SCL_INVALID_KEY_TYPE;
    }

    return CTC_E_NONE;
}

/*
 * check group info if it is valid.
 */
STATIC int32
_sys_greatbelt_scl_check_group_info(uint8 lchip, ctc_scl_group_info_t* pinfo, uint8 type, uint8 is_create)
{
    CTC_PTR_VALID_CHECK(pinfo);
    SYS_SCL_DBG_FUNC();

    CTC_MAX_VALUE_CHECK(pinfo->type, CTC_SCL_GROUP_TYPE_MAX - 1);

    if (is_create) /* create will check gchip. */
    {
        /* if ((CTC_SCL_GROUP_TYPE_PORT_CLASS == type) && (pinfo->lchip >= scl_master[lchip]->lchip_num))*/
        /* {*/
        /*     return CTC_E_INVALID_PARAM;*/
        /* }*/
        SYS_SCL_CHECK_GROUP_PRIO(pinfo->priority);
    }

    if (CTC_SCL_GROUP_TYPE_PORT_CLASS == type)
    {
        SYS_SCL_PORT_CLASS_ID_CHECK(pinfo->un.port_class_id);
    }

    return CTC_E_NONE;
}

/*
 * check if install group info is right, and if it is new.
 */
STATIC bool
_sys_greatbelt_scl_is_group_info_new(uint8 lchip, sys_scl_sw_group_info_t* sys,
                                     ctc_scl_group_info_t* ctc)
{
    uint8 equal = 0;
    switch (ctc->type)
    {
    case  CTC_SCL_GROUP_TYPE_HASH:
    case  CTC_SCL_GROUP_TYPE_GLOBAL: /* hash port global group always is not new */
        equal = 1;
        break;
    case  CTC_SCL_GROUP_TYPE_PORT:
        equal = (ctc->un.gport == sys->gport);
        break;
    case  CTC_SCL_GROUP_TYPE_PORT_CLASS:
        equal = (ctc->un.port_class_id == sys->port_class_id);
        break;
    default:
        return CTC_E_INVALID_PARAM;
    }

    if (!equal)
    {
        return TRUE;
    }

    return FALSE;
}

/*
 * get sys group info based on ctc group info
 */
STATIC int32
_sys_greatbelt_scl_map_group_info(uint8 lchip, sys_scl_sw_group_info_t* sys, ctc_scl_group_info_t* ctc, uint8 is_create)
{
    if (is_create) /* create group care type, priority, gchip*/
    {
        sys->type = ctc->type;
    }

    if (sys->type == CTC_SCL_GROUP_TYPE_PORT)
    {
        CTC_GLOBAL_PORT_CHECK(ctc->un.gport)
        sys->gport = ctc->un.gport;
    }
    else if (sys->type == CTC_SCL_GROUP_TYPE_LOGIC_PORT)
    {
        SYS_LOGIC_PORT_CHECK(ctc->un.logic_port);
        sys->gport = ctc->un.logic_port;
    }

    sys->port_class_id = ctc->un.port_class_id;

    return CTC_E_NONE;
}


/*
 * get ctc group info based on sys group info
 */
STATIC int32
_sys_greatbelt_scl_unmap_group_info(uint8 lchip, ctc_scl_group_info_t* ctc, sys_scl_sw_group_info_t* sys)
{
    ctc->type          = sys->type;
    ctc->lchip         = lchip;
    ctc->un.port_class_id = sys->port_class_id;

    return CTC_E_NONE;
}


/*
 * remove accessory property
 */
STATIC int32
_sys_greatbelt_scl_remove_accessory_property(uint8 lchip, sys_scl_sw_entry_t* pe, sys_scl_sw_action_t* pa)
{

    CTC_PTR_VALID_CHECK(pe);
    CTC_PTR_VALID_CHECK(pa);

    /* remove statsptr */
    switch (pa->type)
    {
    case SYS_SCL_ACTION_INGRESS:
    {
        sys_scl_sw_igs_action_t* pia = NULL;
        pia = &pa->u0.ia;
        if (CTC_FLAG_ISSET(pia->flag, CTC_SCL_IGS_ACTION_FLAG_STATS))
        {
                pia->chip.stats_ptr = 0;
        }


        if (CTC_FLAG_ISSET(pia->flag, CTC_SCL_IGS_ACTION_FLAG_SERVICE_ID))
        {

            CTC_ERROR_RETURN(sys_greatbelt_qos_unbind_service_logic_port(lchip, pia->service_id,
                                                                         pia->bind_l.bds.logic_src_port));
        }

    }
    break;
    case SYS_SCL_ACTION_EGRESS:
    {
        sys_scl_sw_egs_action_t* pea = NULL;
        pea = &pa->u0.ea;
        if (CTC_FLAG_ISSET(pea->flag, CTC_SCL_EGS_ACTION_FLAG_STATS))
        {
                pea->chip.stats_ptr = 0;
        }
    }
    break;
    default:
        break;
    }

    return CTC_E_NONE;
}

#define  __SCL_BUILD_FREE_INDEX__





/* build small tcam index by using ftm */
STATIC int32
_sys_greatbelt_scl_small_tcam_index_build(uint8 lchip, uint8 conv_type, uint32* p_ckey_index_out)
{
    uint32 value_32 = 0;

    SYS_SCL_DBG_FUNC();

    if (SYS_SCL_CONV_SHORT == conv_type)
    {
        CTC_ERROR_RETURN(sys_greatbelt_ftm_alloc_table_offset(lchip, DsFibUserId80Key_t, 0, 1, &value_32));
    }
    else if (SYS_SCL_CONV_LONG == conv_type)
    {
        CTC_ERROR_RETURN(sys_greatbelt_ftm_alloc_table_offset(lchip, DsFibUserId160Key_t, 0, 1, &value_32));
    }

    /* small tcam couting */
    scl_master[lchip]->small_tcam_count++;

    *p_ckey_index_out = value_32 & 0xFFFF;
    return CTC_E_NONE;
}

/* free small tcam index by using ftm */
STATIC int32
_sys_greatbelt_scl_small_tcam_index_free(uint8 lchip, uint8 conv_type, uint32 ckey_index_in)
{
    SYS_SCL_DBG_FUNC();

    if (SYS_SCL_CONV_SHORT == conv_type)
    {
        CTC_ERROR_RETURN(sys_greatbelt_ftm_free_table_offset(lchip, DsFibUserId80Key_t, 0, 1, ckey_index_in));
    }
    else if (SYS_SCL_CONV_LONG == conv_type)
    {
        CTC_ERROR_RETURN(sys_greatbelt_ftm_free_table_offset(lchip, DsFibUserId160Key_t, 0, 1, ckey_index_in));
    }
    /* small tcam couting */
    scl_master[lchip]->small_tcam_count--;

    return CTC_E_NONE;
}

/*----reserv 64 patterns-----*/
STATIC int32
_sys_greatbelt_scl_vlan_edit_index_build(uint8 lchip, uint16* p_profile_id)
{
    sys_greatbelt_opf_t opf;
    uint32              value_32 = 0;

    SYS_SCL_DBG_FUNC();

    sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));
    opf.pool_index = 0;
    opf.pool_type  = OPF_SCL_VLAN_ACTION;

    CTC_ERROR_RETURN(sys_greatbelt_opf_alloc_offset(lchip, &opf, 1, &value_32));
    *p_profile_id = value_32 & 0xFFFF;

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_scl_vlan_edit_index_free(uint8 lchip, uint16 iv_edit_index_free)
{
    sys_greatbelt_opf_t opf;

    SYS_SCL_DBG_FUNC();

    sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));
    opf.pool_index = 0;
    opf.pool_type  = OPF_SCL_VLAN_ACTION;

    CTC_ERROR_RETURN(sys_greatbelt_opf_free_offset(lchip, &opf, 1, (uint32) iv_edit_index_free));

    return CTC_E_NONE;
}



/* free index of HASH ad */
STATIC int32
_sys_greatbelt_scl_free_hash_ad_index(uint8 lchip, uint32 ad_index)
{
    sys_greatbelt_opf_t opf;

    SYS_SCL_DBG_FUNC();

    sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));
    opf.pool_index = 0;
    opf.pool_type  = OPF_SCL_AD;

    CTC_ERROR_RETURN(sys_greatbelt_opf_free_offset(lchip, &opf, 1, (uint32) ad_index));

    return CTC_E_NONE;
}



/* build index of HASH ad */
STATIC int32
_sys_greatbelt_scl_build_hash_ad_index(uint8 lchip, uint32* p_ad_index)
{
    sys_greatbelt_opf_t opf;
    uint32              value_32 = 0;

    SYS_SCL_DBG_FUNC();

    sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));
    opf.pool_index = 0;
    opf.pool_type  = OPF_SCL_AD;

    CTC_ERROR_RETURN(sys_greatbelt_opf_alloc_offset(lchip, &opf, 1, &value_32));
    *p_ad_index = value_32;

    return CTC_E_NONE;
}


STATIC int32
_sys_greatbelt_scl_asic_hash_lkup(uint8 lchip, sys_scl_sw_entry_t* pe, lookup_result_t* p_lookup_result)
{
    lookup_info_t   lookup_info;
    uint32          key[MAX_ENTRY_WORD] = { 0 };
    sys_scl_sw_key_t* psys              = NULL;

    CTC_PTR_VALID_CHECK(pe);
    CTC_PTR_VALID_CHECK(p_lookup_result);
    SYS_SCL_DBG_FUNC();

    sal_memset(&lookup_info, 0, sizeof(lookup_info_t));
    sal_memset(p_lookup_result, 0, sizeof(lookup_result_t));

    /* only chip 0 */
    lookup_info.chip_id     = lchip;
    lookup_info.hash_module = HASH_MODULE_USERID;

    psys = &pe->key;
    switch (pe->key.type)
    {
    case SYS_SCL_KEY_HASH_PORT_2VLAN:
        _sys_greatbelt_scl_build_hash_2vlan(lchip, psys, key, 0);
        lookup_info.hash_type = USERID_HASH_TYPE_TWO_VLAN;
        break;

    case SYS_SCL_KEY_HASH_PORT:
        _sys_greatbelt_scl_build_hash_port(lchip, psys, key, 0);
        lookup_info.hash_type = USERID_HASH_TYPE_PORT;
        break;

    case SYS_SCL_KEY_HASH_PORT_CVLAN:
        _sys_greatbelt_scl_build_hash_cvlan(lchip, psys, key, 0);
        lookup_info.hash_type = USERID_HASH_TYPE_CVLAN;
        break;

    case SYS_SCL_KEY_HASH_PORT_SVLAN:
        _sys_greatbelt_scl_build_hash_svlan(lchip, psys, key, 0);
        lookup_info.hash_type = USERID_HASH_TYPE_SVLAN;
        break;

    case SYS_SCL_KEY_HASH_PORT_CVLAN_COS:
        _sys_greatbelt_scl_build_hash_cvlan_cos(lchip, psys, key, 0);
        lookup_info.hash_type = USERID_HASH_TYPE_CVLAN_COS;
        break;

    case SYS_SCL_KEY_HASH_PORT_SVLAN_COS:
        _sys_greatbelt_scl_build_hash_svlan_cos(lchip, psys, key, 0);
        lookup_info.hash_type = USERID_HASH_TYPE_SVLAN_COS;
        break;

    case SYS_SCL_KEY_HASH_MAC:
        _sys_greatbelt_scl_build_hash_mac(lchip, psys, key, 0);
        lookup_info.hash_type = USERID_HASH_TYPE_MAC_SA;
        break;

    case SYS_SCL_KEY_HASH_PORT_MAC:
        _sys_greatbelt_scl_build_hash_port_mac(lchip, psys, key, 0);
        lookup_info.hash_type = USERID_HASH_TYPE_PORT_MAC_SA;
        break;

    case SYS_SCL_KEY_HASH_IPV4:
        _sys_greatbelt_scl_build_hash_ipv4(lchip, psys, key, 0);
        lookup_info.hash_type = USERID_HASH_TYPE_IPV4_SA;
        break;

    case SYS_SCL_KEY_HASH_PORT_IPV4:
        _sys_greatbelt_scl_build_hash_port_ipv4(lchip, psys, key, 0);
        lookup_info.hash_type = USERID_HASH_TYPE_PORT_IPV4_SA;
        break;

    case SYS_SCL_KEY_HASH_IPV6:
        _sys_greatbelt_scl_build_hash_ipv6(lchip, psys, key, 0);
        lookup_info.hash_type = USERID_HASH_TYPE_IPV6_SA;
        break;

    case SYS_SCL_KEY_HASH_L2:
        _sys_greatbelt_scl_build_hash_l2(lchip, psys, key, 0);
        lookup_info.hash_type = USERID_HASH_TYPE_L2;
        break;

    case SYS_SCL_KEY_HASH_TUNNEL_IPV4:
        _sys_greatbelt_scl_build_hash_tunnel_ipv4(lchip, psys, key, 0);
        lookup_info.hash_type = USERID_HASH_TYPE_IPV4_TUNNEL;
        break;

    case SYS_SCL_KEY_HASH_TUNNEL_IPV4_GRE:
        _sys_greatbelt_scl_build_hash_tunnel_ipv4_gre(lchip, psys, key, 0);
        lookup_info.hash_type = USERID_HASH_TYPE_IPV4_GRE_KEY;
        break;

    case SYS_SCL_KEY_HASH_TUNNEL_IPV4_RPF:
        _sys_greatbelt_scl_build_hash_tunnel_ipv4_rpf(lchip, psys, key, 0);
        lookup_info.hash_type = USERID_HASH_TYPE_IPV4_RPF;
        break;
    case SYS_SCL_KEY_PORT_DEFAULT_EGRESS:
    case SYS_SCL_KEY_PORT_DEFAULT_INGRESS:
    {
        uint8 lport = 0;
        lport                      = CTC_MAP_GPORT_TO_LPORT(psys->u0.hash.u0.port.gport);
        p_lookup_result->valid     = 1;
        p_lookup_result->key_index = lport;
        return CTC_E_NONE;;
    }

    default:
        return CTC_E_INVALID_PARAM;
    }

    lookup_info.p_ds_key = key;

    /* get index of Key */
    CTC_ERROR_RETURN(DRV_HASH_LOOKUP(&lookup_info, p_lookup_result));

    return CTC_E_NONE;
}



#define  __SCL_ESSENTIAL_API__


STATIC int32
_sys_greatbelt_scl_hash_ad_spool_add(uint8 lchip, sys_scl_sw_entry_t* pe,
                                     sys_scl_sw_action_t* pa,
                                     sys_scl_sw_action_t** pa_out)
{
    sys_scl_sw_action_t* pa_new = NULL;
    sys_scl_sw_action_t* pa_get = NULL; /* get from share pool*/
    int32              ret      = 0;

    uint8              array_idx = NORMAL_AD_SHARE_POOL;

    /* malloc sys action */
    MALLOC_ZERO(MEM_SCL_MODULE, pa_new, sizeof(sys_scl_sw_action_t));
    if (!pa_new)
    {
        return CTC_E_NO_MEMORY;
    }
    sal_memcpy(pa_new, pa, sizeof(sys_scl_sw_action_t));
    pa_new->lchip = lchip;

    if (SYS_SCL_IS_DEFAULT(pe->key.type))
    {
        array_idx = DEFAULT_AD_SHARE_POOL;
    }

        /* -- set action ptr -- */
        ret = ctc_spool_add(scl_master[lchip]->ad_spool[array_idx], pa_new, NULL, &pa_get);

        if (ret < 0)
        {
            ret = CTC_E_SPOOL_ADD_UPDATE_FAILED;
            goto cleanup;
        }
        else if (CTC_SPOOL_E_OPERATE_MEMORY == ret)
        {
            if (NORMAL_AD_SHARE_POOL == array_idx)
            {
                uint32 ad_index_get;

                ret = _sys_greatbelt_scl_build_hash_ad_index(lchip, &ad_index_get);
                if (ret < 0)
                {
                    ctc_spool_remove(scl_master[lchip]->ad_spool[array_idx], pa_new, NULL);
                    goto cleanup;
                }

                pa_get->ad_index = ad_index_get;
            }
            pa_get->ref_cnt++;
        }
        else if (CTC_SPOOL_E_OPERATE_REFCNT == ret)
        {
                mem_free(pa_new);
        }

    *pa_out = pa_get;

    return CTC_E_NONE;

 cleanup:
    mem_free(pa_new);
    return ret;
}

STATIC int32
_sys_greatbelt_scl_hash_ad_spool_remove(uint8 lchip, sys_scl_sw_entry_t* pe,
                                        sys_scl_sw_action_t* pa)

{
    uint32             index_free = 0;

    int32              ret       = 0;
    uint8              array_idx = NORMAL_AD_SHARE_POOL;

    sys_scl_sw_action_t* pa_lkup;

    if (SYS_SCL_IS_DEFAULT(pe->key.type))
    {
        array_idx = DEFAULT_AD_SHARE_POOL;
    }
        ret = ctc_spool_remove(scl_master[lchip]->ad_spool[array_idx], pa, &pa_lkup);

        if (ret < 0)
        {
            return CTC_E_SPOOL_REMOVE_FAILED;
        }
        else if (ret == CTC_SPOOL_E_OPERATE_MEMORY)
        {
            if (NORMAL_AD_SHARE_POOL == array_idx)
            {
                index_free = pa_lkup->ad_index;

                CTC_ERROR_RETURN(_sys_greatbelt_scl_free_hash_ad_index(lchip, index_free));

                SYS_SCL_DBG_INFO("  %% INFO: scl action removed: index = %d\n", index_free);
            }
                mem_free(pa_lkup);
        }


    return CTC_E_NONE;
}

/* pv_out can be NULL.*/
STATIC int32
_sys_greatbelt_scl_hash_vlan_edit_spool_add(uint8 lchip, sys_scl_sw_entry_t* pe,
                                            sys_scl_sw_vlan_edit_t* pv,
                                            sys_scl_sw_vlan_edit_t** pv_out)
{
    sys_scl_sw_vlan_edit_t* pv_new = NULL;
    sys_scl_sw_vlan_edit_t* pv_get = NULL; /* get from share pool*/
    int32                 ret      = 0;

    /* malloc sys action */
    MALLOC_ZERO(MEM_SCL_MODULE, pv_new, sizeof(sys_scl_sw_vlan_edit_t));
    if (!pv_new)
    {
        return CTC_E_NO_MEMORY;
    }
    sal_memcpy(pv_new, pv, sizeof(sys_scl_sw_vlan_edit_t));
    pv_new->lchip = lchip;

    /* -- set vlan edit ptr -- */
    ret = ctc_spool_add(scl_master[lchip]->vlan_edit_spool, pv_new, NULL, &pv_get);

    if (ret < 0)
    {
        ret = CTC_E_SPOOL_ADD_UPDATE_FAILED;
        goto cleanup;
    }
    else if (CTC_SPOOL_E_OPERATE_MEMORY == ret)
    {
        uint16 profile_id = 0;
            ret                            = (ret < 0) ? ret : _sys_greatbelt_scl_vlan_edit_index_build(lchip, &profile_id);
            pv_get->chip.profile_id = profile_id;
        if (ret < 0)
        {
            ctc_spool_remove(scl_master[lchip]->vlan_edit_spool, pv_new, NULL);
            goto cleanup;
        }
    }
    else if (CTC_SPOOL_E_OPERATE_REFCNT == ret)
    {
        mem_free(pv_new);
    }

    if (pv_out)
    {
        *pv_out = pv_get;
    }

    return CTC_E_NONE;

 cleanup:
    mem_free(pv_new);
    return ret;
}

STATIC int32
_sys_greatbelt_scl_hash_vlan_edit_spool_remove(uint8 lchip, sys_scl_sw_entry_t* pe,
                                               sys_scl_sw_vlan_edit_t* pv)

{
    uint16                index_free = 0;
    int32                 ret = 0;
    sys_scl_sw_vlan_edit_t* pv_lkup;

    ret = ctc_spool_remove(scl_master[lchip]->vlan_edit_spool, pv, &pv_lkup);

    if (ret < 0)
    {
        return CTC_E_SPOOL_REMOVE_FAILED;
    }
    else if (ret == CTC_SPOOL_E_OPERATE_MEMORY)
    {
            index_free = pv_lkup->chip.profile_id;
            CTC_ERROR_RETURN(_sys_greatbelt_scl_vlan_edit_index_free(lchip, index_free));
        mem_free(pv_lkup);
    }

    return CTC_E_NONE;
}



STATIC int32
_sys_greatbelt_scl_hash_key_hash_add(uint8 lchip, sys_scl_sw_entry_t* pe,
                                     sys_scl_sw_entry_t** pe_out, uint8 act_type)
{
    int32              ret;
    uint32             block_index = 0 ;
    sys_scl_sw_entry_t* pe_new                            = NULL;
    lookup_result_t   lookup_result;

    sal_memset(&lookup_result, 0, sizeof(lookup_result_t));

    /* cacluate asic hash index */
        /* ASIC hash lookup, check if hash conflict */
        CTC_ERROR_RETURN(_sys_greatbelt_scl_asic_hash_lkup(lchip, pe, &lookup_result));

        if (!lookup_result.free && !lookup_result.valid)
        {
            uint8 conv_type = 0;

            /* key can be converted */
            ret = (_sys_greatbelt_scl_convert_type(lchip, pe->key.type, act_type, &conv_type));
            ret = (ret < 0) ? ret : (_sys_greatbelt_scl_small_tcam_index_build
                                         (lchip, conv_type, &block_index));

            pe->key.conv_type = conv_type;
            /* change error code */
            if (ret < 0)
            {
                return CTC_E_SCL_HASH_CONFLICT;
            }
        }
        else
        {
            uint32 key_id = 0;
            uint32 act_id = 0;
            uint32 mm[MAX_ENTRY_WORD] = { 0 };

            scl_master[lchip]->hash_count++;
            block_index  = lookup_result.key_index;

            CTC_ERROR_RETURN(_sys_greatbelt_scl_get_table_id(lchip, pe->key.conv_type, pe->key.type,act_type, &key_id, &act_id));

            if ((SYS_SCL_KEY_PORT_DEFAULT_EGRESS != pe->key.type)
            && (SYS_SCL_KEY_PORT_DEFAULT_INGRESS != pe->key.type))
            {
                 /* key*/
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, block_index, DRV_IOW(key_id, DRV_ENTRY_FLAG), mm));

                /*keep hash key valid */
                CTC_ERROR_RETURN(_sys_greatbelt_scl_set_hash_valid(lchip,pe->key.type,block_index,1));
            }
        }

    /* malloc sys entry */
    MALLOC_ZERO(MEM_SCL_MODULE, pe_new, scl_master[lchip]->entry_size[pe->key.type]);
    if (!pe_new)
    {
        return CTC_E_NO_MEMORY;
    }
    sal_memcpy(pe_new, pe, scl_master[lchip]->entry_size[pe->key.type]);
    pe_new->fpae.lchip = lchip;

        /* set block index */
        pe_new->fpae.offset_a = block_index;

    *pe_out = pe_new;
    return CTC_E_NONE;

}




STATIC int32
_sys_greatbelt_scl_hash_key_hash_remove(uint8 lchip, sys_scl_sw_entry_t* pe_del)
{

    /* free index */
        if (pe_del->key.conv_type)
        {
            CTC_ERROR_RETURN(_sys_greatbelt_scl_small_tcam_index_free
                                 (lchip, pe_del->key.conv_type, (uint32)pe_del->fpae.offset_a));
        }
        else
        {
            scl_master[lchip]->hash_count--;
        }

    /* mem free*/
    mem_free(pe_del);
    return CTC_E_NONE;
}



STATIC int32
_sys_greatbelt_scl_tcam_entry_add(uint8 lchip, sys_scl_sw_group_t* pg,
                                  sys_scl_sw_entry_t* pe,
                                  sys_scl_sw_action_t* pa,
                                  sys_scl_sw_entry_t** pe_out,
                                  sys_scl_sw_action_t** pa_out)
{
    uint8              alloc_id;
    int32              ret;
    sys_scl_sw_entry_t * pe_new                            = NULL;
    sys_scl_sw_action_t* pa_new                            = NULL;
    sys_scl_sw_block_t * pb                                = NULL;
    int32             block_index = 0;

    /* malloc sys entry */
    MALLOC_ZERO(MEM_SCL_MODULE, pe_new, scl_master[lchip]->entry_size[pe->key.type]);
    if (!pe_new)
    {
        return CTC_E_NO_MEMORY;
    }
    sal_memcpy(pe_new, pe, scl_master[lchip]->entry_size[pe->key.type]);
    pe_new->fpae.lchip = lchip;

    /* malloc sys action */
    MALLOC_ZERO(MEM_SCL_MODULE, pa_new, sizeof(sys_scl_sw_action_t));
    if (!pa_new)
    {
        mem_free(pe_new);
        return CTC_E_NO_MEMORY;
    }
    sal_memcpy(pa_new, pa, sizeof(sys_scl_sw_action_t));
    pa_new->lchip = lchip;

    /* get block index. */
    alloc_id = scl_master[lchip]->alloc_id[pe->key.type];
    pb       = &scl_master[lchip]->block[alloc_id];
        CTC_ERROR_GOTO(fpa_greatbelt_alloc_offset(scl_master[lchip]->fpa, &pb->fpab,
                                      pe->fpae.priority, &block_index), ret, cleanup);
        {   /* move to fpa */
            /* add to block */
            pb->fpab.entries[block_index] = &pe_new->fpae;

            /* free_count-- */
            (pb->fpab.free_count)--;

            /* set block index */
            pe_new->fpae.offset_a = block_index;

            fpa_greatbelt_reorder(scl_master[lchip]->fpa, &pb->fpab);
        }

    *pe_out = pe_new;
    *pa_out = pa_new;

    return CTC_E_NONE;

 cleanup:
    mem_free(pe_new);
    mem_free(pa_new);
    return ret;
}




STATIC int32
_sys_greatbelt_scl_tcam_entry_remove(uint8 lchip, sys_scl_sw_entry_t* pe_lkup,
                                     sys_scl_sw_block_t* pb_lkup)
{
    int32 block_index = 0;

    /* remove from block */
    block_index = pe_lkup->fpae.offset_a;

        CTC_ERROR_RETURN(fpa_greatbelt_free_offset(scl_master[lchip]->fpa, &pb_lkup->fpab, block_index));
    mem_free(pe_lkup->action);
    mem_free(pe_lkup);

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_scl_small_ad_add(uint8 lchip, sys_scl_sw_action_t* pa,
                                sys_scl_sw_action_t** pa_out)
{
    sys_scl_sw_action_t* pa_new                            = NULL;

    /* malloc sys action */
    MALLOC_ZERO(MEM_SCL_MODULE, pa_new, sizeof(sys_scl_sw_action_t));
    if (!pa_new)
    {
        return CTC_E_NO_MEMORY;
    }
    sal_memcpy(pa_new, pa, sizeof(sys_scl_sw_action_t));

    *pa_out = pa_new;

    return CTC_E_NONE;
}




STATIC int32
_sys_greatbelt_scl_small_ad_remove(uint8 lchip, sys_scl_sw_action_t* pa_del)
{
    mem_free(pa_del);
    return CTC_E_NONE;
}


/*
 * pe, pg, pb are lookup result.
 */
int32
_sys_greatbelt_scl_remove_entry(uint8 lchip, uint32 entry_id, uint8 inner)
{
    sys_scl_sw_entry_t* pe_lkup = NULL;
    sys_scl_sw_group_t* pg_lkup = NULL;
    sys_scl_sw_block_t* pb_lkup = NULL;

    SYS_SCL_DBG_FUNC();

    _sys_greatbelt_scl_get_nodes_by_eid(lchip, entry_id, &pe_lkup, &pg_lkup, &pb_lkup);
    if (!pe_lkup || !pg_lkup)  /* for hash, pb is NULL*/
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    if (FPA_ENTRY_FLAG_INSTALLED == pe_lkup->fpae.flag)
    {
        return CTC_E_SCL_ENTRY_INSTALLED;
    }

    if ((SYS_SCL_ACTION_INGRESS == pe_lkup->action->type) &&
        (pe_lkup->action->u0.ia.vlan_edit))
    {
        CTC_ERROR_RETURN(_sys_greatbelt_scl_hash_vlan_edit_spool_remove
                             (lchip, pe_lkup, pe_lkup->action->u0.ia.vlan_edit));
    }

    /* remove from group */
    ctc_slist_delete_node(pg_lkup->entry_list, &(pe_lkup->head));

    /* remove from hash */
    ctc_hash_remove(scl_master[lchip]->entry, pe_lkup);
    if (SCL_INNER_ENTRY_ID(pe_lkup->fpae.entry_id) || !SCL_ENTRY_IS_TCAM(pe_lkup->key.type))
    {
        /* add to hash by key */
        ctc_hash_remove(scl_master[lchip]->entry_by_key, pe_lkup);
    }

    /* remove accessory property */
    CTC_ERROR_RETURN(_sys_greatbelt_scl_remove_accessory_property(lchip, pe_lkup, pe_lkup->action));

    if (SCL_ENTRY_IS_TCAM(pe_lkup->key.type))
    {
        CTC_ERROR_RETURN(_sys_greatbelt_scl_tcam_entry_remove(lchip, pe_lkup, pb_lkup));
    }
    else
    {
        if (!SCL_ENTRY_IS_SMALL_TCAM(pe_lkup->key.conv_type)) /* hash*/
        {
            CTC_ERROR_RETURN(_sys_greatbelt_scl_hash_ad_spool_remove(lchip, pe_lkup, pe_lkup->action));
        }
        else /* small tcam */
        {
            CTC_ERROR_RETURN(_sys_greatbelt_scl_small_ad_remove(lchip, pe_lkup->action));
        }
        /*remove hash key valid */
        CTC_ERROR_RETURN(_sys_greatbelt_scl_set_hash_valid(lchip,pe_lkup->key.type,pe_lkup->fpae.offset_a,0));

        CTC_ERROR_RETURN(_sys_greatbelt_scl_hash_key_hash_remove(lchip, pe_lkup));
    }

    (pg_lkup->entry_count)--;

    if (inner)
    {
        sys_greatbelt_opf_t opf;
        /* sdk assigned, recycle */
        sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));
        opf.pool_index = 0; /* care not chip */
        opf.pool_type  = OPF_SCL_ENTRY_ID;

        sys_greatbelt_opf_free_offset(lchip, &opf, 1, (entry_id - SYS_SCL_MIN_INNER_ENTRY_ID));
    }

    return CTC_E_NONE;
}



/*
 * add scl entry to software table
 * pe pa is all ready
 *
 */
STATIC int32
_sys_greatbelt_scl_add_entry(uint8 lchip, uint32 group_id,
                             sys_scl_sw_entry_t* pe,
                             sys_scl_sw_action_t* pa,
                             sys_scl_sw_vlan_edit_t* pv)
{
    int32                 ret      = 0;
    sys_scl_sw_entry_t    * pe_get = NULL;
    sys_scl_sw_action_t   * pa_get = NULL; /* get from share pool*/
    sys_scl_sw_vlan_edit_t* pv_get = NULL; /* get from share pool*/
    sys_scl_sw_group_t* pg = NULL;

    SYS_SCL_DBG_FUNC();

    _sys_greatbelt_scl_get_group_by_gid(lchip, group_id, &pg);
    if (!pg)
    {
        return CTC_E_SCL_GROUP_UNEXIST;
    }

    if (SCL_ENTRY_IS_TCAM(pe->key.type))
    {
        /* 1. vlan edit. error roll back key.*/
        if ((SYS_SCL_ACTION_INGRESS == pa->type)
            && (CTC_FLAG_ISSET(pa->u0.ia.flag, CTC_SCL_IGS_ACTION_FLAG_VLAN_EDIT)))
        {
            ret = (_sys_greatbelt_scl_hash_vlan_edit_spool_add(lchip, pe, pv, &pv_get));
            if FAIL(ret)
            {
                return ret;
            }

            /* 3. action. error roll back vlan edit and key*/
            if (pv_get) /* complete action with vlan edit*/
            {
                pa->u0.ia.vlan_edit                 = pv_get;
                pa->u0.ia.vlan_action_profile_valid = 1;
                    pa->u0.ia.chip.profile_id = pv_get->chip.profile_id;
            }
        }


        ret = (_sys_greatbelt_scl_tcam_entry_add(lchip, pg, pe, pa, &pe_get, &pa_get));
        if (FAIL(ret))
        {
            if (pv_get)
            {
                _sys_greatbelt_scl_hash_vlan_edit_spool_remove(lchip, pe, pv_get);
            }
            return ret;
        }
    }
    else
    {
        /* 1. key . error just return */
        CTC_ERROR_RETURN(_sys_greatbelt_scl_hash_key_hash_add(lchip, pe, &pe_get, pa->type));

        /* 2. vlan edit. error roll back key.*/
        if ((SYS_SCL_ACTION_INGRESS == pa->type)
            && (CTC_FLAG_ISSET(pa->u0.ia.flag, CTC_SCL_IGS_ACTION_FLAG_VLAN_EDIT)))
        {
            ret = (_sys_greatbelt_scl_hash_vlan_edit_spool_add(lchip, pe, pv, &pv_get));
            if FAIL(ret)
            {
                _sys_greatbelt_scl_hash_key_hash_remove(lchip, pe_get);
                return ret;
            }

            /* 3. action. error roll back vlan edit and key*/
            if (pv_get) /* complete action with vlan edit*/
            {
                pa->u0.ia.vlan_edit                 = pv_get;
                pa->u0.ia.vlan_action_profile_valid = 1;
                    pa->u0.ia.chip.profile_id = pv_get->chip.profile_id;
            }
        }

        if(!SCL_ENTRY_IS_SMALL_TCAM(pe->key.conv_type)) /* hash */
        {
            ret = (_sys_greatbelt_scl_hash_ad_spool_add(lchip, pe, pa, &pa_get));
            if FAIL(ret)
            {
                _sys_greatbelt_scl_hash_key_hash_remove(lchip, pe_get);
                if (pv_get)
                {
                    _sys_greatbelt_scl_hash_vlan_edit_spool_remove(lchip, pe_get, pv_get);
                }
                return ret;
            }
        }
        else /* small tcam */
        {
            ret = (_sys_greatbelt_scl_small_ad_add(lchip, pa, &pa_get));
            if FAIL(ret)
            {
                _sys_greatbelt_scl_hash_key_hash_remove(lchip, pe_get);
                if (pv_get)
                {
                    _sys_greatbelt_scl_hash_vlan_edit_spool_remove(lchip, pe_get, pv_get);
                }
                return ret;
            }
        }

    }

    /* -- set action ptr -- */
    pe_get->action = pa_get;

    /* add to hash */
    ctc_hash_insert(scl_master[lchip]->entry, pe_get);
    if (SCL_INNER_ENTRY_ID(pe->fpae.entry_id) || !SCL_ENTRY_IS_TCAM(pe->key.type))
    {
        /* add to hash by key */
        ctc_hash_insert(scl_master[lchip]->entry_by_key, pe_get);
    }

    /* add to group */
    ctc_slist_add_head(pg->entry_list, &(pe_get->head));

    /* mark flag */
    pe_get->fpae.flag = FPA_ENTRY_FLAG_UNINSTALLED;

    (pg->entry_count)++;

    return CTC_E_NONE;
}

#define __SCL_DUMP_INFO__
#define SCL_DUMP_VLAN_INFO(flag, pk, TYPE)                                          \
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_ ## TYPE ## _KEY_FLAG_CVLAN))             \
    {                                                                               \
        SYS_SCL_DBG_DUMP("  c-vlan 0x%x  mask 0x%0x\n", pk->cvlan, pk->cvlan_mask); \
    }                                                                               \
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_ ## TYPE ## _KEY_FLAG_CTAG_COS))          \
    {                                                                               \
        SYS_SCL_DBG_DUMP("  c-cos %u  mask %u\n", pk->ctag_cos, pk->ctag_cos_mask); \
    }                                                                               \
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_ ## TYPE ## _KEY_FLAG_CTAG_CFI))          \
    {                                                                               \
        SYS_SCL_DBG_DUMP("  c-cfi %u\n", pk->ctag_cfi);                             \
    }                                                                               \
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_ ## TYPE ## _KEY_FLAG_SVLAN))             \
    {                                                                               \
        SYS_SCL_DBG_DUMP("  s-vlan 0x%x  mask 0x%0x\n", pk->svlan, pk->svlan_mask); \
    }                                                                               \
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_ ## TYPE ## _KEY_FLAG_STAG_COS))          \
    {                                                                               \
        SYS_SCL_DBG_DUMP("  s-cos %u  mask %u\n", pk->stag_cos, pk->stag_cos_mask); \
    }                                                                               \
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_ ## TYPE ## _KEY_FLAG_STAG_CFI))          \
    {                                                                               \
        SYS_SCL_DBG_DUMP("  s-cfi %u\n", pk->stag_cfi);                             \
    }

#define SCL_DUMP_MAC_INFO(flag, pk, TYPE)                                                 \
    SYS_SCL_DBG_DUMP("  mac-sa");                                                         \
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_ ## TYPE ## _KEY_FLAG_MAC_SA))                  \
    {                                                                                     \
        if ((0xFFFF == *(uint16 *) &pk->mac_sa_mask[0]) &&                                \
            (0xFFFFFFFF == *(uint32 *) &pk->mac_sa_mask[2]))                              \
        {                                                                                 \
            SYS_SCL_DBG_DUMP(" host %02x%02x.%02x%02x.%02x%02x\n",                        \
                             pk->mac_sa[0], pk->mac_sa[1], pk->mac_sa[2],                 \
                             pk->mac_sa[3], pk->mac_sa[4], pk->mac_sa[5]);                \
        }                                                                                 \
        else if ((0 == *(uint16 *) &pk->mac_sa_mask[0]) &&                                \
                 (0 == *(uint32 *) &pk->mac_sa_mask[2]))                                  \
        {                                                                                 \
            SYS_SCL_DBG_DUMP(" any\n");                                                   \
        }                                                                                 \
        else                                                                              \
        {                                                                                 \
            SYS_SCL_DBG_DUMP(" %02x%02x.%02x%02x.%02x%02x %02x%02x.%02x%02x.%02x%02x\n",  \
                             pk->mac_sa[0], pk->mac_sa[1], pk->mac_sa[2],                 \
                             pk->mac_sa[3], pk->mac_sa[4], pk->mac_sa[5],                 \
                             pk->mac_sa_mask[0], pk->mac_sa_mask[1], pk->mac_sa_mask[2],  \
                             pk->mac_sa_mask[3], pk->mac_sa_mask[4], pk->mac_sa_mask[5]); \
        }                                                                                 \
    }                                                                                     \
    else                                                                                  \
    {                                                                                     \
        SYS_SCL_DBG_DUMP(" any\n");                                                       \
    }                                                                                     \
                                                                                          \
    SYS_SCL_DBG_DUMP("  mac-da");                                                         \
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_ ## TYPE ## _KEY_FLAG_MAC_DA))                  \
    {                                                                                     \
        if ((0xFFFF == *(uint16 *) &pk->mac_da_mask[0]) &&                                \
            (0xFFFFFFFF == *(uint32 *) &pk->mac_da_mask[2]))                              \
        {                                                                                 \
            SYS_SCL_DBG_DUMP(" host %02x%02x.%02x%02x.%02x%02x\n",                        \
                             pk->mac_da[0], pk->mac_da[1], pk->mac_da[2],                 \
                             pk->mac_da[3], pk->mac_da[4], pk->mac_da[5]);                \
        }                                                                                 \
        else if ((0 == *(uint16 *) &pk->mac_da_mask[0]) &&                                \
                 (0 == *(uint32 *) &pk->mac_da_mask[2]))                                  \
        {                                                                                 \
            SYS_SCL_DBG_DUMP(" any\n");                                                   \
        }                                                                                 \
        else                                                                              \
        {                                                                                 \
            SYS_SCL_DBG_DUMP(" %02x%02x.%02x%02x.%02x%02x %02x%02x.%02x%02x.%02x%02x\n",  \
                             pk->mac_da[0], pk->mac_da[1], pk->mac_da[2],                 \
                             pk->mac_da[3], pk->mac_da[4], pk->mac_da[5],                 \
                             pk->mac_da_mask[0], pk->mac_da_mask[1], pk->mac_da_mask[2],  \
                             pk->mac_da_mask[3], pk->mac_da_mask[4], pk->mac_da_mask[5]); \
        }                                                                                 \
    }                                                                                     \
    else                                                                                  \
    {                                                                                     \
        SYS_SCL_DBG_DUMP(" any\n");                                                       \
    }

#define SCL_DUMP_L2_L3_TYPE(flag, pk, TYPE)                                         \
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_ ## TYPE ## _KEY_FLAG_L3_TYPE))           \
    {                                                                               \
        SYS_SCL_DBG_DUMP("  l3_type %u  mask %u\n", pk->l3_type, pk->l3_type_mask); \
    }                                                                               \
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_ ## TYPE ## _KEY_FLAG_L2_TYPE))           \
    {                                                                               \
        SYS_SCL_DBG_DUMP("  l2_type %u  mask %u\n", pk->l2_type, pk->l2_type_mask); \
    }

#define SCL_DUMP_L3_INFO(flag, pk, TYPE)                                                   \
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_ ## TYPE ## _KEY_FLAG_L4_TYPE))                  \
    {                                                                                      \
        SYS_SCL_DBG_DUMP("  l4_type %u  mask %u\n", pk->l4_type, pk->l4_type_mask);        \
    }                                                                                      \
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_ ## TYPE ## _KEY_FLAG_ETH_TYPE))                 \
    {                                                                                      \
        SYS_SCL_DBG_DUMP("  eth-type 0x%x  mask 0x%x\n", pk->eth_type, pk->eth_type_mask); \
    }                                                                                      \
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_ ## TYPE ## _KEY_FLAG_DSCP) ||                   \
        CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_ ## TYPE ## _KEY_FLAG_PRECEDENCE))               \
    {                                                                                      \
        SYS_SCL_DBG_DUMP("  dscp %u  mask %u\n", pk->dscp, pk->dscp_mask);                 \
    }                                                                                      \
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_ ## TYPE ## _KEY_FLAG_IP_FRAG))                  \
    {                                                                                      \
        SYS_SCL_DBG_DUMP("  frag-info %u  mask %u\n", pk->frag_info,                       \
                         pk->frag_info_mask);                                              \
    }                                                                                      \
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_ ## TYPE ## _KEY_FLAG_IP_OPTION))                \
    {                                                                                      \
        SYS_SCL_DBG_DUMP("  ip-option %u\n", pk->ip_option);                               \
    }


#define SCL_DUMP_L4_PROTOCOL(flag, pk, TYPE)                                                   \
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_L4_PROTOCOL))                          \
    {                                                                                          \
        if (pk->flag_l4info_mapped)                                                            \
        {                                                                                      \
            if ((0xFF == pk->l4info_mapped_mask)                                               \
                && (SYS_SCL_IPV6_ICMP == pk->l4info_mapped))        /* ICMP */                 \
            {                                                                                  \
                SYS_SCL_DBG_DUMP("  L4 Protocol = Icmp\n");                                    \
                /* icmp type & code*/                                                          \
                if (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_L4_SRC_PORT))      \
                {                                                                              \
                    SYS_SCL_DBG_DUMP("  icmp type:0x%x  mask:0x%x\n",                          \
                                     (pk->l4_src_port >> 8), (pk->l4_src_port_mask >> 8));     \
                    SYS_SCL_DBG_DUMP("  icmp code:0x%x  mask:0x%x\n",                          \
                                     (pk->l4_src_port & 0xFF), (pk->l4_src_port_mask & 0xFF)); \
                }                                                                              \
            }                                                                                  \
            else    /* other layer 4 protocol type */                                          \
            {                                                                                  \
                SYS_SCL_DBG_DUMP("  L4 Protocol = 0x%x  mask:0x%x\n",                          \
                                 pk->l4info_mapped, pk->l4info_mapped_mask);                   \
            }                                                                                  \
        }                                                                                      \
        else                                                                                   \
        {                                                                                      \
            if (pk->flag_is_tcp)                                                               \
            {                                                                                  \
                SYS_SCL_DBG_DUMP("  L4 Protocol = TCP\n");                                     \
            }                                                                                  \
            if (pk->flag_is_udp)                                                               \
            {                                                                                  \
                SYS_SCL_DBG_DUMP("  L4 Protocol = UDP\n");                                     \
            }                                                                                  \
                                                                                               \
            if (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_L4_SRC_PORT))          \
            {                                                                                  \
                SYS_SCL_DBG_DUMP("  l4 src port:0x%x  mask:0x%x\n",                            \
                                 (pk->l4_src_port), (pk->l4_src_port_mask));                   \
            }                                                                                  \
            if (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_L4_DST_PORT))          \
            {                                                                                  \
                SYS_SCL_DBG_DUMP("  l4 dst port:0x%x  mask:0x%x\n",                            \
                                 (pk->l4_dst_port), (pk->l4_dst_port_mask));                   \
            }                                                                                  \
                                                                                               \
        }                                                                                      \
    }




/*
 * show mac entry
 */
STATIC int32
_sys_greatbelt_scl_show_tcam_mac(uint8 lchip, sys_scl_sw_tcam_key_mac_t* pk)
{
    uint32 flag = pk->flag;

    SCL_DUMP_VLAN_INFO(flag, pk, MAC);
    SCL_DUMP_MAC_INFO(flag, pk, MAC);
    SCL_DUMP_L2_L3_TYPE(flag, pk, MAC);

    return CTC_E_NONE;
}

/*
 * show ipv4 entry
 */
STATIC int32
_sys_greatbelt_scl_show_tcam_ipv4(uint8 lchip, sys_scl_sw_tcam_key_ipv4_t* pk)
{
    uint32 flag     = pk->flag;
    uint32 sub_flag = pk->sub_flag;

    {
        uint32 addr;
        char   ip_addr[16];
        SYS_SCL_DBG_DUMP("  ip-sa  ");
        if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_IP_SA))
        {
            if (0xFFFFFFFF == pk->ip_sa_mask)
            {
                SYS_SCL_DBG_DUMP("host");
                addr = sal_ntohl(pk->ip_sa);
                sal_inet_ntop(AF_INET, &addr, ip_addr, sizeof(ip_addr));
                SYS_SCL_DBG_DUMP(" %s\n", ip_addr);
            }
            else if (0 == pk->ip_sa_mask)
            {
                SYS_SCL_DBG_DUMP("any\n");
            }
            else
            {
                addr = sal_ntohl(pk->ip_sa);
                sal_inet_ntop(AF_INET, &addr, ip_addr, sizeof(ip_addr));
                SYS_SCL_DBG_DUMP("%s", ip_addr);
                addr = sal_ntohl(pk->ip_sa_mask);
                sal_inet_ntop(AF_INET, &addr, ip_addr, sizeof(ip_addr));
                SYS_SCL_DBG_DUMP("  %s\n", ip_addr);
            }
        }
        else
        {
            SYS_SCL_DBG_DUMP("any\n");
        }

        SYS_SCL_DBG_DUMP("  ip-da  ");
        if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_IP_DA))
        {
            if (0xFFFFFFFF == pk->ip_da_mask)
            {
                SYS_SCL_DBG_DUMP("host");
                addr = sal_ntohl(pk->ip_da);
                sal_inet_ntop(AF_INET, &addr, ip_addr, sizeof(ip_addr));
                SYS_SCL_DBG_DUMP(" %s\n", ip_addr);
            }
            else if (0 == pk->ip_da_mask)
            {
                SYS_SCL_DBG_DUMP("any\n");
            }
            else
            {
                addr = sal_ntohl(pk->ip_da);
                sal_inet_ntop(AF_INET, &addr, ip_addr, sizeof(ip_addr));
                SYS_SCL_DBG_DUMP("%s", ip_addr);
                addr = sal_ntohl(pk->ip_da_mask);
                sal_inet_ntop(AF_INET, &addr, ip_addr, sizeof(ip_addr));
                SYS_SCL_DBG_DUMP("  %s\n", ip_addr);
            }
        }
        else
        {
            SYS_SCL_DBG_DUMP("any\n");
        }
    }

    SCL_DUMP_VLAN_INFO(flag, pk, IPV4);
    SCL_DUMP_MAC_INFO(flag, pk, IPV4);
    SCL_DUMP_L2_L3_TYPE(flag, pk, IPV4);
    SCL_DUMP_L3_INFO(flag, pk, IPV4);
    SCL_DUMP_L4_PROTOCOL(flag, pk, IPV4);

    return CTC_E_NONE;
}

/*
 * show ipv6 entry
 */
STATIC int32
_sys_greatbelt_scl_show_tcam_ipv6(uint8 lchip, sys_scl_sw_tcam_key_ipv6_t* pk)
{
    uint32 flag     = pk->flag;
    uint32 sub_flag = pk->sub_flag;

    {
        char        buf[CTC_IPV6_ADDR_STR_LEN];
        ipv6_addr_t ipv6_addr;
        sal_memset(buf, 0, CTC_IPV6_ADDR_STR_LEN);
        sal_memset(ipv6_addr, 0, sizeof(ipv6_addr_t));
        SYS_SCL_DBG_DUMP("  ip-sa  ");
        if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_IP_SA))
        {
            if ((0xFFFFFFFF == pk->ip_sa_mask[0]) && (0xFFFFFFFF == pk->ip_sa_mask[1])
                && (0xFFFFFFFF == pk->ip_sa_mask[2]) && (0xFFFFFFFF == pk->ip_sa_mask[3]))
            {
                SYS_SCL_DBG_DUMP("host ");

                ipv6_addr[0] = sal_htonl(pk->ip_sa[0]);
                ipv6_addr[1] = sal_htonl(pk->ip_sa[1]);
                ipv6_addr[2] = sal_htonl(pk->ip_sa[2]);
                ipv6_addr[3] = sal_htonl(pk->ip_sa[3]);

                sal_inet_ntop(AF_INET6, ipv6_addr, buf, CTC_IPV6_ADDR_STR_LEN);
                SYS_SCL_DBG_DUMP("%20s\n", buf);
            }
            else if ((0 == pk->ip_sa_mask[0]) && (0 == pk->ip_sa_mask[1])
                     && (0 == pk->ip_sa_mask[2]) && (0 == pk->ip_sa_mask[3]))
            {
                SYS_SCL_DBG_DUMP("any\n");
            }
            else
            {
                ipv6_addr[0] = sal_htonl(pk->ip_sa[0]);
                ipv6_addr[1] = sal_htonl(pk->ip_sa[1]);
                ipv6_addr[2] = sal_htonl(pk->ip_sa[2]);
                ipv6_addr[3] = sal_htonl(pk->ip_sa[3]);
                sal_inet_ntop(AF_INET6, ipv6_addr, buf, CTC_IPV6_ADDR_STR_LEN);
                SYS_SCL_DBG_DUMP("%20s ", buf);

                ipv6_addr[0] = sal_htonl(pk->ip_sa_mask[0]);
                ipv6_addr[1] = sal_htonl(pk->ip_sa_mask[1]);
                ipv6_addr[2] = sal_htonl(pk->ip_sa_mask[2]);
                ipv6_addr[3] = sal_htonl(pk->ip_sa_mask[3]);

                sal_inet_ntop(AF_INET6, ipv6_addr, buf, CTC_IPV6_ADDR_STR_LEN);
                SYS_SCL_DBG_DUMP("%20s\n", buf);
            }
        }
        else
        {
            SYS_SCL_DBG_DUMP("any\n");
        }

        sal_memset(buf, 0, CTC_IPV6_ADDR_STR_LEN);

        SYS_SCL_DBG_DUMP("  ip-da  ");
        if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_IP_DA))
        {
            if ((0xFFFFFFFF == pk->ip_da_mask[0]) && (0xFFFFFFFF == pk->ip_da_mask[1])
                && (0xFFFFFFFF == pk->ip_da_mask[2]) && (0xFFFFFFFF == pk->ip_da_mask[3]))
            {
                SYS_SCL_DBG_DUMP("host ");

                ipv6_addr[0] = sal_htonl(pk->ip_da[0]);
                ipv6_addr[1] = sal_htonl(pk->ip_da[1]);
                ipv6_addr[2] = sal_htonl(pk->ip_da[2]);
                ipv6_addr[3] = sal_htonl(pk->ip_da[3]);

                sal_inet_ntop(AF_INET6, ipv6_addr, buf, CTC_IPV6_ADDR_STR_LEN);
                SYS_SCL_DBG_DUMP("%20s\n", buf);
            }
            else if ((0 == pk->ip_da_mask[0]) && (0 == pk->ip_da_mask[1])
                     && (0 == pk->ip_da_mask[2]) && (0 == pk->ip_da_mask[3]))
            {
                SYS_SCL_DBG_DUMP("any\n");
            }
            else
            {
                ipv6_addr[0] = sal_htonl(pk->ip_da[0]);
                ipv6_addr[1] = sal_htonl(pk->ip_da[1]);
                ipv6_addr[2] = sal_htonl(pk->ip_da[2]);
                ipv6_addr[3] = sal_htonl(pk->ip_da[3]);
                sal_inet_ntop(AF_INET6, ipv6_addr, buf, CTC_IPV6_ADDR_STR_LEN);
                SYS_SCL_DBG_DUMP("%20s ", buf);

                ipv6_addr[0] = sal_htonl(pk->ip_da_mask[0]);
                ipv6_addr[1] = sal_htonl(pk->ip_da_mask[1]);
                ipv6_addr[2] = sal_htonl(pk->ip_da_mask[2]);
                ipv6_addr[3] = sal_htonl(pk->ip_da_mask[3]);

                sal_inet_ntop(AF_INET6, ipv6_addr, buf, CTC_IPV6_ADDR_STR_LEN);
                SYS_SCL_DBG_DUMP("%20s\n", buf);
            }
        }
        else
        {
            SYS_SCL_DBG_DUMP("any\n");
        }
    }
    SCL_DUMP_VLAN_INFO(flag, pk, IPV6);
    SCL_DUMP_MAC_INFO(flag, pk, IPV6);
    SCL_DUMP_L2_L3_TYPE(flag, pk, IPV6);
    SCL_DUMP_L3_INFO(flag, pk, IPV6);
    SCL_DUMP_L4_PROTOCOL(flag, pk, IPV6);

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_FLOW_LABEL))
    {
        SYS_SCL_DBG_DUMP("  flow-label 0x%x flow-label 0x%x\n",
                         pk->flow_label, pk->flow_label_mask);
    }

    return CTC_E_NONE;
}


/*
 * show mpls entry
 */
STATIC int32
_sys_greatbelt_scl_show_tcam_vlan(uint8 lchip, sys_scl_sw_tcam_key_vlan_t* pk)
{
    uint32 flag = pk->flag;


    SCL_DUMP_VLAN_INFO(flag, pk, VLAN);

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_VLAN_KEY_FLAG_CTAG_VALID))
    {
        SYS_SCL_DBG_DUMP("  Ctag valid: 0x%x\n", pk->ctag_valid);
    }
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_VLAN_KEY_FLAG_STAG_VALID))
    {
        SYS_SCL_DBG_DUMP("  Stag valid: 0x%x\n", pk->stag_valid);
    }

    return CTC_E_NONE;
}

/*
 * show scl action
 */
STATIC int32
_sys_greatbelt_scl_show_egs_action(uint8 lchip, sys_scl_sw_egs_action_t* pea)
{
    char* tag_op[CTC_VLAN_TAG_OP_MAX];
    char* tag_sl[CTC_VLAN_TAG_SL_MAX];
    tag_op[CTC_VLAN_TAG_OP_NONE]       = "None";
    tag_op[CTC_VLAN_TAG_OP_REP_OR_ADD] = "Replace OR Add";
    tag_op[CTC_VLAN_TAG_OP_ADD]        = "Add";
    tag_op[CTC_VLAN_TAG_OP_DEL]        = "Del";
    tag_op[CTC_VLAN_TAG_OP_REP]        = "Replace";
    tag_op[CTC_VLAN_TAG_OP_VALID]      = "Valid";

    tag_sl[CTC_VLAN_TAG_SL_AS_PARSE]    = "As parse   ";
    tag_sl[CTC_VLAN_TAG_SL_ALTERNATIVE] = "Alternative";
    tag_sl[CTC_VLAN_TAG_SL_NEW]         = "New        ";
    tag_sl[CTC_VLAN_TAG_SL_DEFAULT]     = "Default    ";

    CTC_PTR_VALID_CHECK(pea);

    SYS_SCL_DBG_DUMP("SCL Egress action:\n");
    SYS_SCL_DBG_DUMP("---------------------------------:\n");


    if (CTC_FLAG_ISSET(pea->flag, CTC_SCL_EGS_ACTION_FLAG_DISCARD))
    {
        SYS_SCL_DBG_DUMP("  discard\n");
    }

    if (CTC_FLAG_ISSET(pea->flag, CTC_SCL_EGS_ACTION_FLAG_STATS))
    {
        SYS_SCL_DBG_DUMP("  stats\n");
    }

    if (CTC_FLAG_ISSET(pea->flag, CTC_SCL_EGS_ACTION_FLAG_AGING))
    {
        SYS_SCL_DBG_DUMP("  aging\n");
    }

    if (CTC_FLAG_ISSET(pea->flag, CTC_SCL_EGS_ACTION_FLAG_PRIORITY_AND_COLOR))
    {
        SYS_SCL_DBG_DUMP("  priority %u color %u\n", pea->priority, pea->color);
    }

    if (CTC_FLAG_ISSET(pea->flag, CTC_SCL_EGS_ACTION_FLAG_VLAN_EDIT))
    {
        if (pea->stag_op != CTC_VLAN_TAG_OP_NONE)
        {
            SYS_SCL_DBG_DUMP("  stag-op:  %s\n", tag_op[pea->stag_op]);
            if (pea->stag_op != CTC_VLAN_TAG_OP_DEL)
            {
                SYS_SCL_DBG_DUMP("  svid-sl:  %s;  new-svid:  %d\n", tag_sl[pea->svid_sl], pea->svid);
                SYS_SCL_DBG_DUMP("  scos-sl:  %s;  new-scos:  %d\n", tag_sl[pea->scos_sl], pea->scos);
                SYS_SCL_DBG_DUMP("  scfi-sl:  %s;  new-scfi:  %d\n", tag_sl[pea->scfi_sl], pea->scfi);
            }
        }
        if (pea->ctag_op != CTC_VLAN_TAG_OP_NONE)
        {
            SYS_SCL_DBG_DUMP("  ctag-op:  %s\n", tag_op[pea->ctag_op]);
            if (pea->ctag_op != CTC_VLAN_TAG_OP_DEL)
            {
                SYS_SCL_DBG_DUMP("  cvid-sl:  %s;  new-cvid:  %d\n", tag_sl[pea->cvid_sl], pea->cvid);
                SYS_SCL_DBG_DUMP("  ccos-sl:  %s;  new-ccos:  %d\n", tag_sl[pea->ccos_sl], pea->ccos);
                SYS_SCL_DBG_DUMP("  ccfi-sl:  %s;  new-ccfi:  %d\n", tag_sl[pea->ccfi_sl], pea->ccfi);
            }
        }
    }
    return CTC_E_NONE;
}

/*
 * show scl action
 */
STATIC int32
_sys_greatbelt_scl_show_igs_action(uint8 lchip, sys_scl_sw_igs_action_t* pia)
{
    char  * tag_op[CTC_VLAN_TAG_OP_MAX];
    char  * tag_sl[CTC_VLAN_TAG_SL_MAX];
    tag_op[CTC_VLAN_TAG_OP_NONE]       = "None";
    tag_op[CTC_VLAN_TAG_OP_REP_OR_ADD] = "Replace OR Add";
    tag_op[CTC_VLAN_TAG_OP_ADD]        = "Add";
    tag_op[CTC_VLAN_TAG_OP_DEL]        = "Del";
    tag_op[CTC_VLAN_TAG_OP_REP]        = "Replace";
    tag_op[CTC_VLAN_TAG_OP_VALID]      = "Valid";

    tag_sl[CTC_VLAN_TAG_SL_AS_PARSE]    = "As parse   ";
    tag_sl[CTC_VLAN_TAG_SL_ALTERNATIVE] = "Alternative";
    tag_sl[CTC_VLAN_TAG_SL_NEW]         = "New        ";
    tag_sl[CTC_VLAN_TAG_SL_DEFAULT]     = "Default    ";

    CTC_PTR_VALID_CHECK(pia);

    SYS_SCL_DBG_DUMP("SCL ingress action:\n");
    SYS_SCL_DBG_DUMP("-------------------------------------------------\n");

    if (CTC_FLAG_ISSET(pia->flag, CTC_SCL_IGS_ACTION_FLAG_DISCARD))
    {
        SYS_SCL_DBG_DUMP("  discard\n");
    }

    if (CTC_FLAG_ISSET(pia->flag, CTC_SCL_IGS_ACTION_FLAG_STATS))
    {
        SYS_SCL_DBG_DUMP("  stats\n");
    }

    if (CTC_FLAG_ISSET(pia->flag, CTC_SCL_IGS_ACTION_FLAG_PRIORITY_AND_COLOR))
    {
        SYS_SCL_DBG_DUMP("  priority %u color %u\n", pia->priority, pia->color);
    }

    if (CTC_FLAG_ISSET(pia->sub_flag, CTC_SCL_IGS_ACTION_SUB_FLAG_SERVICE_POLICER_EN))
    {
        SYS_SCL_DBG_DUMP("  service policer-id %u\n", pia->service_policer_id);
    }

    if (CTC_FLAG_ISSET(pia->sub_flag, CTC_SCL_IGS_ACTION_SUB_FLAG_SERVICE_ACL_EN))
    {
        SYS_SCL_DBG_DUMP("  service-acl enable\n");
    }

    if (CTC_FLAG_ISSET(pia->sub_flag, CTC_SCL_IGS_ACTION_SUB_FLAG_SERVICE_QUEUE_EN))
    {
        SYS_SCL_DBG_DUMP("  service-queue enable\n");
    }

    if (CTC_FLAG_ISSET(pia->flag, CTC_SCL_IGS_ACTION_FLAG_COPY_TO_CPU))
    {
        SYS_SCL_DBG_DUMP("  copy-to-cpu\n");
    }

    if (CTC_FLAG_ISSET(pia->flag, CTC_SCL_IGS_ACTION_FLAG_REDIRECT))
    {
        SYS_SCL_DBG_DUMP("  redirect nhid %u\n", pia->nh_id);
    }

    if (CTC_FLAG_ISSET(pia->flag, CTC_SCL_IGS_ACTION_FLAG_AGING))
    {
        SYS_SCL_DBG_DUMP("  aging\n");
    }

    if (CTC_FLAG_ISSET(pia->flag, CTC_SCL_IGS_ACTION_FLAG_FID))
    {
        SYS_SCL_DBG_DUMP("  Fid %u\n", pia->fid);
    }

    if (CTC_FLAG_ISSET(pia->flag, CTC_SCL_IGS_ACTION_FLAG_VRFID))
    {
        SYS_SCL_DBG_DUMP("  Vrf id %u\n", pia->fid);
    }

    if (CTC_FLAG_ISSET(pia->flag, CTC_SCL_IGS_ACTION_FLAG_VLAN_EDIT) && (pia->vlan_edit))
    {
        if (pia->vlan_edit->stag_op != CTC_VLAN_TAG_OP_NONE)
        {
            SYS_SCL_DBG_DUMP("  stag-op:  %s\n", tag_op[pia->vlan_edit->stag_op]);
            if (pia->vlan_edit->stag_op != CTC_VLAN_TAG_OP_DEL)
            {
                SYS_SCL_DBG_DUMP("  svid-sl:  %s;  new-svid:  %d\n", tag_sl[pia->vlan_edit->svid_sl], pia->svid);
                SYS_SCL_DBG_DUMP("  scos-sl:  %s;  new-scos:  %d\n", tag_sl[pia->vlan_edit->scos_sl], pia->scos);
                SYS_SCL_DBG_DUMP("  scfi-sl:  %s;  new-scfi:  %d\n", tag_sl[pia->vlan_edit->scfi_sl], pia->scfi);
            }
        }
        if (pia->vlan_edit->ctag_op != CTC_VLAN_TAG_OP_NONE)
        {
            SYS_SCL_DBG_DUMP("  ctag-op:  %s\n", tag_op[pia->vlan_edit->ctag_op]);
            if (pia->vlan_edit->ctag_op != CTC_VLAN_TAG_OP_DEL)
            {
                SYS_SCL_DBG_DUMP("  cvid-sl:  %s;  new-cvid:  %d\n", tag_sl[pia->vlan_edit->cvid_sl], pia->cvid);
                SYS_SCL_DBG_DUMP("  ccos-sl:  %s;  new-ccos:  %d\n", tag_sl[pia->vlan_edit->ccos_sl], pia->ccos);
                SYS_SCL_DBG_DUMP("  ccfi-sl:  %s;  new-ccfi:  %d\n", tag_sl[pia->vlan_edit->ccfi_sl], pia->ccfi);
            }
        }
            SYS_SCL_DBG_DUMP("  [vlan action] profile id[%d] %d \n", lchip, pia->vlan_edit->chip.profile_id);
    }

    if (CTC_FLAG_ISSET(pia->flag, CTC_SCL_IGS_ACTION_FLAG_LOGIC_PORT))
    {
        SYS_SCL_DBG_DUMP("  logic_port:%d  logic_port_type:%d\n", pia->bind_l.bds.logic_src_port, pia->bind_l.bds.logic_port_type);
    }


    if (CTC_FLAG_ISSET(pia->flag, CTC_SCL_IGS_ACTION_FLAG_BINDING_EN))
    {
        SYS_SCL_DBG_DUMP("  binding:");
        if (pia->binding_mac_sa)
        {
            SYS_SCL_DBG_DUMP("  mac-sa:%02x%02x.%02x%02x.%02x%02x\n",
                             (pia->bind_h.data >> 8) & 0xFF, pia->bind_h.data & 0xFF, (pia->bind_l.data >> 24) & 0xFF,
                             (pia->bind_l.data >> 16) & 0xFF, (pia->bind_l.data >> 8) & 0xFF, pia->bind_l.data & 0xFF);
        }
        else
        {
            uint32 ip_sa = 0;
            uint32 addr;
            char   ip_addr[16];

            switch (pia->bind_l.data & 0x3)
            {
            case 0:
                SYS_SCL_DBG_DUMP("  Port:0x%X\n", SYS_SCL_GPORT14_TO_GPORT13((pia->bind_h.data) & 0x3FFF));
                break;
            case 1:
                SYS_SCL_DBG_DUMP("  Vlan:%u\n", (pia->bind_h.data >> 4) & 0x3FF);
                break;
            case 2:
                SYS_SCL_DBG_DUMP("  IPv4sa:");

                ip_sa = (((pia->bind_h.data & 0xF) << 28) | (pia->bind_l.data >> 4));
                addr  = sal_ntohl(ip_sa);
                sal_inet_ntop(AF_INET, &addr, ip_addr, sizeof(ip_addr));
                SYS_SCL_DBG_DUMP(" %s\n", ip_addr);
                break;
            case 3:
                SYS_SCL_DBG_DUMP("  Vlan:%u, IPv4sa:", (pia->bind_h.data >> 4) & 0x3FF);

                ip_sa = (((pia->bind_h.data & 0xF) << 28) | (pia->bind_l.data >> 4));
                addr  = sal_ntohl(ip_sa);
                sal_inet_ntop(AF_INET, &addr, ip_addr, sizeof(ip_addr));
                SYS_SCL_DBG_DUMP(" %s\n", ip_addr);
                break;
            }
        }
    }

    if (CTC_FLAG_ISSET(pia->flag, CTC_SCL_IGS_ACTION_FLAG_USER_VLANPTR))
    {
        SYS_SCL_DBG_DUMP("  user-vlan-ptr:%u\n", pia->user_vlan_ptr);
    }

    if (CTC_FLAG_ISSET(pia->flag, CTC_SCL_IGS_ACTION_FLAG_VPLS))
    {
        SYS_SCL_DBG_DUMP("  [vpls] vsi-learning-en:%d    mac-limit-en:%d\n",
                         pia->bind_l.bds.vsi_learning_disable ? 0 : 1, pia->bind_l.bds.mac_security_vsi_discard);
    }

    if (CTC_FLAG_ISSET(pia->flag, CTC_SCL_IGS_ACTION_FLAG_APS))
    {
        SYS_SCL_DBG_DUMP("  [aps] protected-vlan:%d    aps_select_group_id:%d   is_working_path:%d\n",
                         pia->user_vlan_ptr, pia->bind_h.bds.aps_select_group_id, pia->bind_h.bds.aps_select_protecting_path);
    }

    if (CTC_FLAG_ISSET(pia->flag, CTC_SCL_IGS_ACTION_FLAG_ETREE_LEAF))
    {
        SYS_SCL_DBG_DUMP("  Etree Leaf\n");
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_scl_show_hash_port(uint8 lchip, sys_scl_sw_hash_key_port_t* pk)
{
    SYS_SCL_DBG_DUMP("  %-5s:0x%x  dir:%d  \n", pk->gport_is_classid ? ("Class") : ("Gport"), pk->gport, pk->dir);
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_scl_show_hash_port_cvlan(uint8 lchip, sys_scl_sw_hash_key_vlan_t* pk)
{
    SYS_SCL_DBG_DUMP("  %-5s:0x%x  dir:%d  Cvlan:%d\n", pk->gport_is_classid ? ("Class") : ("Gport"), pk->gport, pk->dir, pk->vid);
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_scl_show_hash_port_svlan(uint8 lchip, sys_scl_sw_hash_key_vlan_t* pk)
{
    SYS_SCL_DBG_DUMP("  %-5s:0x%x  dir:%d  Svlan:%d\n", pk->gport_is_classid ? ("Class") : ("Gport"), pk->gport, pk->dir, pk->vid);
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_scl_show_hash_port_2vlan(uint8 lchip, sys_scl_sw_hash_key_vtag_t* pk)
{
    SYS_SCL_DBG_DUMP("  %-5s:0x%x  dir:%d  Svlan:%d Cvlan:%d\n", pk->gport_is_classid ? ("Class") : ("Gport"), pk->gport, pk->dir, pk->svid, pk->cvid);
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_scl_show_hash_port_cvlan_cos(uint8 lchip, sys_scl_sw_hash_key_vtag_t* pk)
{
    SYS_SCL_DBG_DUMP("  %-5s:0x%x  dir:%d  Cvlan:%d Ccos:%d\n", pk->gport_is_classid ? ("Class") : ("Gport"), pk->gport, pk->dir, pk->cvid, pk->ccos);
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_scl_show_hash_port_svlan_cos(uint8 lchip, sys_scl_sw_hash_key_vtag_t* pk)
{
    SYS_SCL_DBG_DUMP("  %-5s:0x%x  dir:%d  Svlan:%d Scos:%d\n", pk->gport_is_classid ? ("Class") : ("Gport"), pk->gport, pk->dir, pk->svid, pk->scos);
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_scl_show_hash_port_mac(uint8 lchip, sys_scl_sw_hash_key_mac_t* pk)
{
    mac_addr_t mac;
    SYS_SCL_DBG_DUMP("  %-5s:0x%x  %-5s:", pk->gport_is_classid ? ("Class") : ("Gport"), pk->gport, pk->use_macda ? "MacDa" : "MacSa");

    SYS_SCL_SET_MAC_ARRAY(mac, pk->macsa_h, pk->macsa_l);
    SYS_SCL_DBG_DUMP(" %02x%02x.%02x%02x.%02x%02x\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_scl_show_hash_mac(uint8 lchip, sys_scl_sw_hash_key_mac_t* pk)
{
    mac_addr_t mac;
    SYS_SCL_DBG_DUMP("  %-5s:", pk->use_macda ? "MacDa" : "MacSa");

    SYS_SCL_SET_MAC_ARRAY(mac, pk->macsa_h, pk->macsa_l);
    SYS_SCL_DBG_DUMP(" %02x%02x.%02x%02x.%02x%02x\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_scl_show_hash_port_ipv4(uint8 lchip, sys_scl_sw_hash_key_ipv4_t* pk)
{
    uint32 addr;
    char   ip_addr[16];

    SYS_SCL_DBG_DUMP("  %-5s:0x%x  Ipv4-sa:", pk->gport_is_classid ? ("Class") : ("Gport"), pk->gport);
    addr = sal_ntohl(pk->ip_sa);
    sal_inet_ntop(AF_INET, &addr, ip_addr, sizeof(ip_addr));
    SYS_SCL_DBG_DUMP(" %s\n", ip_addr);

    return CTC_E_NONE;
}


STATIC int32
_sys_greatbelt_scl_show_hash_ipv4(uint8 lchip, sys_scl_sw_hash_key_ipv4_t* pk)
{
    uint32 addr;
    char   ip_addr[16];

    SYS_SCL_DBG_DUMP("  Ipv4-sa:");
    addr = sal_ntohl(pk->ip_sa);
    sal_inet_ntop(AF_INET, &addr, ip_addr, sizeof(ip_addr));
    SYS_SCL_DBG_DUMP(" %s\n", ip_addr);

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_scl_show_hash_ipv6(uint8 lchip, sys_scl_sw_hash_key_ipv6_t* pk)
{
    char        buf[CTC_IPV6_ADDR_STR_LEN];
    ipv6_addr_t ipv6_addr;
    sal_memset(buf, 0, CTC_IPV6_ADDR_STR_LEN);
    sal_memset(ipv6_addr, 0, sizeof(ipv6_addr_t));

    ipv6_addr[0] = sal_htonl(pk->ip_sa[0]);
    ipv6_addr[1] = sal_htonl(pk->ip_sa[1]);
    ipv6_addr[2] = sal_htonl(pk->ip_sa[2]);
    ipv6_addr[3] = sal_htonl(pk->ip_sa[3]);

    sal_inet_ntop(AF_INET6, ipv6_addr, buf, CTC_IPV6_ADDR_STR_LEN);
    SYS_SCL_DBG_DUMP("  Ipv6-sa:%20s\n", buf);
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_scl_show_hash_l2(uint8 lchip, sys_scl_sw_hash_key_l2_t* pk)
{
    mac_addr_t macsa;
    mac_addr_t macda;
    SYS_SCL_SET_MAC_ARRAY(macsa, pk->macsa_h, pk->macsa_l);
    SYS_SCL_SET_MAC_ARRAY(macda, pk->macda_h, pk->macda_l);


    SYS_SCL_DBG_DUMP("  Gport:0x%x  Vlan:%d Cos:%d", pk->gport, pk->vid, pk->cos);

    SYS_SCL_DBG_DUMP("Mac-Sa %02x%02x.%02x%02x.%02x%02x", macsa[0], macsa[1], macsa[2], macsa[3], macsa[4], macsa[5]);
    SYS_SCL_DBG_DUMP("Mac-Da %02x%02x.%02x%02x.%02x%02x\n", macda[0], macda[1], macda[2], macda[3], macda[4], macda[5]);
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_scl_show_port_default_ingress(uint8 lchip, sys_scl_sw_hash_key_port_t* pk)
{
    SYS_SCL_DBG_DUMP("  %-5s:0x%x  [Ingress] \n", pk->gport_is_classid ? ("Class") : ("Gport"), pk->gport);
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_scl_show_port_default_egress(uint8 lchip, sys_scl_sw_hash_key_port_t* pk)
{
    SYS_SCL_DBG_DUMP("  %-5s:0x%x  [Egress] \n", pk->gport_is_classid ? ("Class") : ("Gport"), pk->gport);
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_scl_show_hash_tunnel_ipv4(uint8 lchip, sys_scl_hash_tunnel_ipv4_key_t* pk)
{
    CTC_PTR_VALID_CHECK(pk);

    SYS_SCL_DBG_DUMP("SCL hash tunnel ipv4 key:\n");
    SYS_SCL_DBG_DUMP("---------------------------------:\n");

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_scl_show_hash_tunnel_ipv4_gre(uint8 lchip, sys_scl_hash_tunnel_ipv4_gre_key_t* pk)
{
    CTC_PTR_VALID_CHECK(pk);

    SYS_SCL_DBG_DUMP("SCL hash tunnel ipv4 gre key:\n");
    SYS_SCL_DBG_DUMP("---------------------------------:\n");

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_scl_show_hash_tunnel_ipv4_rpf(uint8 lchip, sys_scl_hash_tunnel_ipv4_rpf_key_t* pk)
{
    CTC_PTR_VALID_CHECK(pk);

    SYS_SCL_DBG_DUMP("SCL hash tunnel ipv4 rpf key:\n");
    SYS_SCL_DBG_DUMP("---------------------------------:\n");

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_scl_show_tcam_tunnel_ipv4(uint8 lchip, sys_scl_tcam_tunnel_ipv4_key_t* pk)
{
    CTC_PTR_VALID_CHECK(pk);

    SYS_SCL_DBG_DUMP("SCL tcam tunnel ipv4 key:\n");
    SYS_SCL_DBG_DUMP("---------------------------------:\n");

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_scl_show_tcam_tunnel_ipv6(uint8 lchip, sys_scl_tcam_tunnel_ipv6_key_t* pk)
{
    CTC_PTR_VALID_CHECK(pk);

    SYS_SCL_DBG_DUMP("SCL tcam tunnel ipv6 key:\n");
    SYS_SCL_DBG_DUMP("---------------------------------:\n");

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_scl_show_tunnel_action(uint8 lchip, sys_scl_tunnel_action_t* pta)
{
    CTC_PTR_VALID_CHECK(pta);

    SYS_SCL_DBG_DUMP("SCL tunnel action:\n");
    SYS_SCL_DBG_DUMP("---------------------------------:\n");

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_scl_show_entry_detail(uint8 lchip, sys_scl_sw_entry_t* pe)
{
    char  * act_name = NULL;

    CTC_PTR_VALID_CHECK(pe);

    SYS_SCL_DBG_DUMP("  Key Name: ");
    switch (pe->key.type)
    {
    case SYS_SCL_KEY_TCAM_VLAN:
        SYS_SCL_DBG_DUMP("DsUserIdVlanKey\n");
        act_name = ("DsUserIdVlanTcam\n");
        _sys_greatbelt_scl_show_tcam_vlan(lchip, &pe->key.u0.vlan_key);
        break;
    case SYS_SCL_KEY_TCAM_MAC:
        SYS_SCL_DBG_DUMP("DsUserIdMacKey\n");
        act_name = ("DsUserIdMacTcam\n");
        _sys_greatbelt_scl_show_tcam_mac(lchip, &pe->key.u0.mac_key);
        break;
    case SYS_SCL_KEY_TCAM_IPV4:
        SYS_SCL_DBG_DUMP("DsUserIdIpv4Key\n");
        act_name = ("DsUserIdIpv4Tcam\n");
        _sys_greatbelt_scl_show_tcam_ipv4(lchip, &pe->key.u0.ipv4_key);
        break;
    case SYS_SCL_KEY_TCAM_IPV6:
        SYS_SCL_DBG_DUMP("DsUserIdIpv6Key\n");
        act_name = ("DsUserIdIpv6Tcam\n");
        _sys_greatbelt_scl_show_tcam_ipv6(lchip, &pe->key.u0.ipv6_key);
        break;
    case SYS_SCL_KEY_TCAM_TUNNEL_IPV4:
        SYS_SCL_DBG_DUMP("DsTunnelIdIpv4Key\n");
        act_name = ("DsTunnelIdIpv4Tcam\n");
        _sys_greatbelt_scl_show_tcam_tunnel_ipv4(lchip, &pe->key.u0.tnnl_ipv4_key);
        break;
    case SYS_SCL_KEY_TCAM_TUNNEL_IPV6:
        SYS_SCL_DBG_DUMP("DsTunnelIdIpv6Key\n");
        act_name = ("DsTunnelIdIpv6Tcam\n");
        _sys_greatbelt_scl_show_tcam_tunnel_ipv6(lchip, &pe->key.u0.tnnl_ipv6_key);
        break;
    case SYS_SCL_KEY_HASH_PORT:
        SYS_SCL_DBG_DUMP("DsUserIdPortHashKey\n");
        _sys_greatbelt_scl_show_hash_port(lchip, &pe->key.u0.hash.u0.port);
        break;
    case SYS_SCL_KEY_HASH_PORT_CVLAN:
        SYS_SCL_DBG_DUMP("DsUserIdCvlanHashKey\n");
        _sys_greatbelt_scl_show_hash_port_cvlan(lchip, &pe->key.u0.hash.u0.vlan);
        break;
    case SYS_SCL_KEY_HASH_PORT_SVLAN:
        SYS_SCL_DBG_DUMP("DsUserIdSvlanHashKey\n");
        _sys_greatbelt_scl_show_hash_port_svlan(lchip, &pe->key.u0.hash.u0.vlan);
        break;
    case SYS_SCL_KEY_HASH_PORT_2VLAN:
        SYS_SCL_DBG_DUMP("DsUserIdDoubleVlanHashKey\n");
        _sys_greatbelt_scl_show_hash_port_2vlan(lchip, &pe->key.u0.hash.u0.vtag);
        break;
    case SYS_SCL_KEY_HASH_PORT_CVLAN_COS:
        SYS_SCL_DBG_DUMP("DsUserIdCvlanCosHashKey\n");
        _sys_greatbelt_scl_show_hash_port_cvlan_cos(lchip, &pe->key.u0.hash.u0.vtag);
        break;
    case SYS_SCL_KEY_HASH_PORT_SVLAN_COS:
        SYS_SCL_DBG_DUMP("DsUserIdSvlanCosHashKey\n");
        _sys_greatbelt_scl_show_hash_port_svlan_cos(lchip, &pe->key.u0.hash.u0.vtag);
        break;
    case SYS_SCL_KEY_HASH_MAC:
        SYS_SCL_DBG_DUMP("DsUserIdMacHashKey\n");
        _sys_greatbelt_scl_show_hash_mac(lchip, &pe->key.u0.hash.u0.mac);
        break;
    case SYS_SCL_KEY_HASH_PORT_MAC:
        SYS_SCL_DBG_DUMP("DsUserIdMacPortHashKey\n");
        _sys_greatbelt_scl_show_hash_port_mac(lchip, &pe->key.u0.hash.u0.mac);
        break;
    case SYS_SCL_KEY_HASH_IPV4:
        SYS_SCL_DBG_DUMP("DsUserIdIpv4HashKey\n");
        _sys_greatbelt_scl_show_hash_ipv4(lchip, &pe->key.u0.hash.u0.ipv4);
        break;
    case SYS_SCL_KEY_HASH_PORT_IPV4:
        SYS_SCL_DBG_DUMP("DsUserIdIpv4PortHashKey\n");
        _sys_greatbelt_scl_show_hash_port_ipv4(lchip, &pe->key.u0.hash.u0.ipv4);
        break;
    case SYS_SCL_KEY_HASH_IPV6:
        SYS_SCL_DBG_DUMP("DsUserIdIpv6HashKey\n");
        _sys_greatbelt_scl_show_hash_ipv6(lchip, &pe->key.u0.hash.u0.ipv6);
        break;
    case SYS_SCL_KEY_HASH_L2:
        SYS_SCL_DBG_DUMP("DsUserIdL2HashKey\n");
        _sys_greatbelt_scl_show_hash_l2(lchip, &pe->key.u0.hash.u0.l2);
        break;
    case SYS_SCL_KEY_HASH_TUNNEL_IPV4:
        SYS_SCL_DBG_DUMP("DsTunnelIdIpv4HashKey\n");
        _sys_greatbelt_scl_show_hash_tunnel_ipv4(lchip, &pe->key.u0.hash.u0.tnnl_ipv4);
        break;
    case SYS_SCL_KEY_HASH_TUNNEL_IPV4_GRE:
        SYS_SCL_DBG_DUMP("DsTunnelIdIpv4HashKey\n");
        _sys_greatbelt_scl_show_hash_tunnel_ipv4_gre(lchip, &pe->key.u0.hash.u0.tnnl_ipv4_gre);
        break;
    case SYS_SCL_KEY_HASH_TUNNEL_IPV4_RPF:
        SYS_SCL_DBG_DUMP("DsTunnelIdIpv4RpfHashKey\n");
        _sys_greatbelt_scl_show_hash_tunnel_ipv4_rpf(lchip, &pe->key.u0.hash.u0.tnnl_ipv4_rpf);
        break;
    case SYS_SCL_KEY_PORT_DEFAULT_INGRESS:
        SYS_SCL_DBG_DUMP("XXXXX\n");
        act_name = ("DsUserIdIngressDefault\n");
        _sys_greatbelt_scl_show_port_default_ingress(lchip, &pe->key.u0.hash.u0.port);
        break;
    case SYS_SCL_KEY_PORT_DEFAULT_EGRESS:
        SYS_SCL_DBG_DUMP("XXXXX\n");
        act_name = ("DsUserIdEgressDefault\n");
        _sys_greatbelt_scl_show_port_default_egress(lchip, &pe->key.u0.hash.u0.port);
        break;
    default:
        break;
    }

    if ((SCL_ENTRY_IS_TCAM(pe->key.type)) || (SYS_SCL_IS_DEFAULT(pe->key.type)) || SCL_ENTRY_IS_SMALL_TCAM(pe->key.type)) /* default index also get from key_index */
    {
        SYS_SCL_DBG_DUMP("  ad_index %d\n", pe->fpae.offset_a);
    }
    else
    {
        SYS_SCL_DBG_DUMP("  ad_index %d\n", pe->action->ad_index);
    }

    switch (pe->action->type)
    {
    case SYS_SCL_ACTION_INGRESS:
        if (!act_name)
        {
            act_name = "DsUserId\n";
        }
        _sys_greatbelt_scl_show_igs_action(lchip, &pe->action->u0.ia);
        break;
    case SYS_SCL_ACTION_EGRESS:
        if (!act_name)
        {
            act_name = "DsVlanXlate\n";
        }
        _sys_greatbelt_scl_show_egs_action(lchip, &pe->action->u0.ea);
        break;
    case SYS_SCL_ACTION_TUNNEL:
        if (!act_name)
        {
            act_name = "DsTunnelId\n";
        }
        _sys_greatbelt_scl_show_tunnel_action(lchip, &pe->action->u0.ta);
        break;
    default:
        break;
    }
    SYS_SCL_DBG_DUMP("  Action Name: %s\n", act_name);

    return CTC_E_NONE;
}


#define SCL_PRINT_CHIP(lchip)    SYS_SCL_DBG_DUMP("++++++++++++++++ lchip %d ++++++++++++++++\n", lchip);


STATIC char*
_sys_greatbelt_scl_get_type(uint8 lchip, uint8 key_type)
{
    switch (key_type)
    {
    case SYS_SCL_KEY_TCAM_VLAN:
        return "tcam_vlan";
    case SYS_SCL_KEY_TCAM_MAC:
        return "tcam_mac";
    case SYS_SCL_KEY_TCAM_IPV4:
        return "tcam_ipv4";
    case SYS_SCL_KEY_TCAM_IPV6:
        return "tcam_ipv6";
    case SYS_SCL_KEY_TCAM_TUNNEL_IPV4:
        return "tcam_tunnel_ipv4";
    case SYS_SCL_KEY_TCAM_TUNNEL_IPV6:
        return "tcam_tunnel_ipv6";
    case SYS_SCL_KEY_HASH_PORT:
        return "hash_port";
    case SYS_SCL_KEY_HASH_PORT_CVLAN:
        return "hash_port_cvlan";
    case SYS_SCL_KEY_HASH_PORT_SVLAN:
        return "hash_port_svlan";
    case SYS_SCL_KEY_HASH_PORT_2VLAN:
        return "hash_port_2vlan";
    case SYS_SCL_KEY_HASH_PORT_CVLAN_COS:
        return "hash_port_cvlan_cos";
    case SYS_SCL_KEY_HASH_PORT_SVLAN_COS:
        return "hash_port_svlan_cos";
    case SYS_SCL_KEY_HASH_MAC:
        return "hash_mac";
    case SYS_SCL_KEY_HASH_PORT_MAC:
        return "hash_port_mac";
    case SYS_SCL_KEY_HASH_IPV4:
        return "hash_ipv4";
    case SYS_SCL_KEY_HASH_PORT_IPV4:
        return "hash_port_ipv4";
    case SYS_SCL_KEY_HASH_IPV6:
        return "hash_ipv6";
    case SYS_SCL_KEY_HASH_L2:
        return "hash_l2";
    case SYS_SCL_KEY_HASH_TUNNEL_IPV4:
        return "hash_tunnel_ipv4";
    case SYS_SCL_KEY_HASH_TUNNEL_IPV4_GRE:
        return "hash_tunnel_ipv4_gre";
    case SYS_SCL_KEY_HASH_TUNNEL_IPV4_RPF:
        return "hash_tunnel_ipv4_rpf";
    case SYS_SCL_KEY_PORT_DEFAULT_INGRESS:
        return "port_default_ingress";
    case SYS_SCL_KEY_PORT_DEFAULT_EGRESS:
        return "port_default_egress";
    default:
        return "XXXXXX";
    }
}
/*
 * show scl entry to a specific entry with specific key type
 */
STATIC int32
_sys_greatbelt_scl_show_entry(uint8 lchip, sys_scl_sw_entry_t* pe, uint16* p_cnt, sys_scl_key_type_t key_type, uint8 detail)
{
    sys_scl_sw_group_t* pg;
    char              * type_name;

    CTC_PTR_VALID_CHECK(pe);

    pg = pe->group;
    if(!pg)
    {
        return CTC_E_SCL_GROUP_UNEXIST;
    }

    if ((SYS_SCL_ALL_KEY != key_type)
        && (pe->key.type != key_type))
    {
        return CTC_E_NONE;
    }

    if (detail)
    {
        SYS_SCL_DBG_DUMP("No.%04d  ", *p_cnt);
    }
    else
    {
        SYS_SCL_DBG_DUMP("%04d  ", *p_cnt);
    }
    (*p_cnt)++;

    type_name = _sys_greatbelt_scl_get_type(lchip, pe->key.type);

   /*  if (lchip_num == lchip + CTC_MAX_LOCAL_CHIP_NUM) // on 2chips . entry prio 2chips equal*/
 /*{*/
        if (detail)
        {
            SYS_SCL_DBG_DUMP("eid=0x%08x gid=0x%08x HW=%3s e_prio[0]=%-4u index[0]=%-4d index[1]=%-4d (%s)\n",
                         pe->fpae.entry_id, pg->group_id, (pe->fpae.flag) ? "yes" : "no ",
                         pe->fpae.priority, pe->fpae.offset_a, pe->fpae.offset_a, type_name);
        }
        else
        {
            if (SYS_SCL_SHOW_IN_HEX <= pe->fpae.entry_id)
            {
                SYS_SCL_DBG_DUMP("0x%08x   ",pe->fpae.entry_id);
            }
            else
            {
                SYS_SCL_DBG_DUMP("%-8u     ",pe->fpae.entry_id);
            }

            if (SYS_SCL_SHOW_IN_HEX <= pg->group_id)
            {
                SYS_SCL_DBG_DUMP("0x%-8x   ",pg->group_id);
            }
            else
            {
                SYS_SCL_DBG_DUMP("%-8u     ",pg->group_id);
            }
            SYS_SCL_DBG_DUMP("%s    %-4u        %-4d          %-4d         %s\n",
                         (pe->fpae.flag) ? "yes" : "no ",pe->fpae.priority, pe->fpae.offset_a,
                         pe->fpae.offset_a, type_name);
        }
    /* }*/


    if (detail)
    {
        _sys_greatbelt_scl_show_entry_detail(lchip, pe);
    }

    return CTC_E_NONE;
}

/*
 * show scl entriy by entry id
 */
STATIC int32
_sys_greatbelt_scl_show_entry_by_entry_id(uint8 lchip, uint32 eid, sys_scl_key_type_t key_type, uint8 detail)
{
    sys_scl_sw_entry_t* pe = NULL;

    uint16            count = 0;

    SYS_SCL_LOCK(lchip);
    _sys_greatbelt_scl_get_nodes_by_eid(lchip, eid, &pe, NULL, NULL);
    if (!pe)
    {
        SYS_SCL_UNLOCK(lchip);
        return CTC_E_ENTRY_NOT_EXIST;
    }

     /*SCL_PRINT_ENTRY_ROW(eid);*/

    CTC_ERROR_RETURN_SCL_UNLOCK(_sys_greatbelt_scl_show_entry(lchip, pe, &count, key_type, detail));
    SYS_SCL_UNLOCK(lchip);

    return CTC_E_NONE;
}

/*
 * show scl entries in a group with specific key type
 */
STATIC int32
_sys_greatbelt_scl_show_entry_in_group(uint8 lchip, sys_scl_sw_group_t* pg, uint16* p_cnt, sys_scl_key_type_t key_type, uint8 detail)
{
    struct ctc_slistnode_s* pe;

    CTC_PTR_VALID_CHECK(pg);

    CTC_SLIST_LOOP(pg->entry_list, pe)
    {
        _sys_greatbelt_scl_show_entry(lchip, (sys_scl_sw_entry_t *) pe, p_cnt, key_type, detail);
    }

    return CTC_E_NONE;
}

/*
 * show scl entries by group id with specific key type
 */
STATIC int32
_sys_greatbelt_scl_show_entry_by_group_id(uint8 lchip, uint32 gid, sys_scl_key_type_t key_type, uint8 detail)
{
    uint16            count = 0;
    sys_scl_sw_group_t* pg  = NULL;

    SYS_SCL_LOCK(lchip);
    _sys_greatbelt_scl_get_group_by_gid(lchip, gid, &pg);
    SYS_SCL_CHECK_GROUP_UNEXIST(pg);

     /*SCL_PRINT_GROUP_ROW(gid);*/

    CTC_ERROR_RETURN_SCL_UNLOCK(_sys_greatbelt_scl_show_entry_in_group(lchip, pg, &count, key_type, detail));
    SYS_SCL_UNLOCK(lchip);

    return CTC_E_NONE;
}

/*
 * show scl entries in tcam sort by index
 */
STATIC int32
_sys_greatbelt_scl_show_tcam_entries(uint8 lchip, uint8 param, sys_scl_key_type_t key_type, uint8 detail)
{
    uint16            count = 0;
    uint8             alloc_id;
    sys_scl_sw_block_t* pb;
    count = 0;
    SCL_PRINT_CHIP(lchip);

    SYS_SCL_LOCK(lchip);
    for (alloc_id = 0; alloc_id < scl_master[lchip]->tcam_alloc_num; alloc_id++)
    {
        pb = &scl_master[lchip]->block[alloc_id];
        if (pb->fpab.free_count != pb->fpab.entry_count) /*not empty*/
        {
            ctc_fpa_entry_t* pe;
            uint16         block_idx;

            for (block_idx = 0; block_idx < pb->fpab.entry_count; block_idx++)
            {
                pe = pb->fpab.entries[block_idx];

                if (pe)
                {
                    CTC_ERROR_RETURN_SCL_UNLOCK(_sys_greatbelt_scl_show_entry(lchip, SCL_OUTER_ENTRY(pe), &count, key_type, detail));
                }
            }
        }
    }
    SYS_SCL_UNLOCK(lchip);

    return CTC_E_NONE;
}



STATIC int32
_sys_greatbelt_scl_show_entry_all(uint8 lchip, sys_scl_key_type_t key_type, uint8 detail)
{
    uint8              idx;
    _scl_cb_para_t     para;
    struct ctc_listnode* node;
    sys_scl_sw_group_t * pg;

    sal_memset(&para, 0, sizeof(_scl_cb_para_t));
    para.detail   = detail;
    para.key_type = key_type;

    SYS_SCL_LOCK(lchip);
    for (idx = 0; idx < 3; idx++)
    {
        CTC_LIST_LOOP(scl_master[lchip]->group_list[idx], pg, node)
        {
            CTC_ERROR_RETURN_SCL_UNLOCK(_sys_greatbelt_scl_show_entry_in_group
                                 (lchip, pg, &(para.count), para.key_type, para.detail));
        }
    }
    SYS_SCL_UNLOCK(lchip);

    return CTC_E_NONE;
}


int32
sys_greatbelt_scl_show_entry(uint8 lchip, uint8 type, uint32 param, sys_scl_key_type_t key_type, uint8 detail)
{
    /* SYS_SCL_KEY_NUM represents all type*/
    SYS_SCL_INIT_CHECK(lchip);
    CTC_MAX_VALUE_CHECK(type, SYS_SCL_KEY_NUM);

    SYS_SCL_DBG_DUMP("No.   ENTRY_ID     GROUP_ID     HW     ENTRY_PRI   INDEX[0]      INDEX[1]     TYPE\n");
    switch (type)
    {
    case 0:
        CTC_ERROR_RETURN(_sys_greatbelt_scl_show_entry_all(lchip, key_type, detail));
        SYS_SCL_DBG_DUMP("\n");
        break;

    case 1:
        CTC_ERROR_RETURN(_sys_greatbelt_scl_show_entry_by_entry_id(lchip, param, key_type, detail));
        SYS_SCL_DBG_DUMP("\n");
        break;

    case 2:
        CTC_ERROR_RETURN(_sys_greatbelt_scl_show_entry_by_group_id(lchip, param, key_type, detail));
        SYS_SCL_DBG_DUMP("\n");
        break;

    case 3:
        CTC_ERROR_RETURN(_sys_greatbelt_scl_show_tcam_entries(lchip, param, key_type, detail));
        SYS_SCL_DBG_DUMP("\n");
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}


STATIC int32
_sys_greatbelt_scl_show_priority_status(uint8 lchip)
{
    uint8 vlan;
    uint8 mac;
    uint8 ipv4;
    uint8 ipv6;
    uint8 t_ipv4;
    uint8 t_ipv6;

    vlan   = scl_master[lchip]->alloc_id[SYS_SCL_KEY_TCAM_VLAN];
    mac    = scl_master[lchip]->alloc_id[SYS_SCL_KEY_TCAM_MAC];
    ipv4   = scl_master[lchip]->alloc_id[SYS_SCL_KEY_TCAM_IPV4];
    ipv6   = scl_master[lchip]->alloc_id[SYS_SCL_KEY_TCAM_IPV6];
    t_ipv4 = scl_master[lchip]->alloc_id[SYS_SCL_KEY_TCAM_TUNNEL_IPV4];
    t_ipv6 = scl_master[lchip]->alloc_id[SYS_SCL_KEY_TCAM_TUNNEL_IPV6];

    SYS_SCL_DBG_DUMP("Tcam Status (lchip %d):\n", lchip);
    SYS_SCL_DBG_DUMP("Type      Vlan    Mac   Ipv4   Ipv6   Tunnel-Ipv4   Tunnel-Ipv6\n");
    SYS_SCL_DBG_DUMP("----------------------------------------------------------------------------\n");
    SYS_SCL_LOCK(lchip);
    SYS_SCL_DBG_DUMP("Used/All  %d/%d  %d/%d  %d/%d  %d/%d     %d/%d           %d/%d\n",
                     scl_master[lchip]->block[vlan].fpab.entry_count - scl_master[lchip]->block[vlan].fpab.free_count,
                     scl_master[lchip]->block[vlan].fpab.entry_count,
                     scl_master[lchip]->block[mac].fpab.entry_count - scl_master[lchip]->block[mac].fpab.free_count,
                     scl_master[lchip]->block[mac].fpab.entry_count,
                     scl_master[lchip]->block[ipv4].fpab.entry_count - scl_master[lchip]->block[ipv4].fpab.free_count,
                     scl_master[lchip]->block[ipv4].fpab.entry_count,
                     scl_master[lchip]->block[ipv6].fpab.entry_count - scl_master[lchip]->block[ipv6].fpab.free_count,
                     scl_master[lchip]->block[ipv6].fpab.entry_count,
                     scl_master[lchip]->block[t_ipv4].fpab.entry_count - scl_master[lchip]->block[t_ipv4].fpab.free_count,
                     scl_master[lchip]->block[t_ipv4].fpab.entry_count,
                     scl_master[lchip]->block[t_ipv6].fpab.entry_count - scl_master[lchip]->block[t_ipv6].fpab.free_count,
                     scl_master[lchip]->block[t_ipv6].fpab.entry_count);
    SYS_SCL_UNLOCK(lchip);

    SYS_SCL_DBG_DUMP("\n");
    return CTC_E_NONE;
}


typedef struct
{
    uint32            group_id;
    char              * name;
    sys_scl_sw_group_t* pg;
    uint32            count;
} sys_scl_rsv_group_info_t;

STATIC int32
_sys_greatbelt_scl_show_entry_status(uint8 lchip)
{
    uint8                    group_num1 = 0;
    uint8                    group_num2 = 0;
    uint8                    idx        = 0;
    uint32                   count      = 0;

    sys_scl_sw_group_t       * pg;
    struct ctc_listnode      * node;

    sys_scl_rsv_group_info_t rsv_group1[] =
    {
        { CTC_SCL_GROUP_ID_HASH_PORT,           "outer_hash_port          ", NULL, 0 },
        { CTC_SCL_GROUP_ID_HASH_PORT_CVLAN,     "outer_hash_port_cvlan    ", NULL, 0 },
        { CTC_SCL_GROUP_ID_HASH_PORT_SVLAN,     "outer_hash_port_svlan    ", NULL, 0 },
        { CTC_SCL_GROUP_ID_HASH_PORT_2VLAN,     "outer_hash_port_2vlan    ", NULL, 0 },
        { CTC_SCL_GROUP_ID_HASH_PORT_CVLAN_COS, "outer_hash_port_cvlan_cos", NULL, 0 },
        { CTC_SCL_GROUP_ID_HASH_PORT_SVLAN_COS, "outer_hash_port_svlan_cos", NULL, 0 },
        { CTC_SCL_GROUP_ID_HASH_MAC,            "outer_hash_mac           ", NULL, 0 },
        { CTC_SCL_GROUP_ID_HASH_PORT_MAC,       "outer_hash_port_mac      ", NULL, 0 },
        { CTC_SCL_GROUP_ID_HASH_IPV4,           "outer_hash_ipv4          ", NULL, 0 },
        { CTC_SCL_GROUP_ID_HASH_PORT_IPV4,      "outer_hash_port_ipv4     ", NULL, 0 },
        { CTC_SCL_GROUP_ID_HASH_IPV6,           "outer_hash_ipv6          ", NULL, 0 },
        { CTC_SCL_GROUP_ID_HASH_L2,             "outer_hash_l2            ", NULL, 0 },
    };

    sys_scl_rsv_group_info_t rsv_group2[] =
    {
        { SYS_SCL_GROUP_ID_INNER_HASH_IP_TUNNEL,        "inner_hash_ip_tunnel       ", NULL, 0 },
        { SYS_SCL_GROUP_ID_INNER_TCAM_IP_TUNNEL,        "inner_tcam_ip_tunnel       ", NULL, 0 },
        { SYS_SCL_GROUP_ID_INNER_HASH_IP_SRC_GUARD,     "inner_hash_ip_src_guard    ", NULL, 0 },
        { SYS_SCL_GROUP_ID_INNER_DEFT_IP_SRC_GUARD,     "inner_deft_ip_src_guard    ", NULL, 0 },
        { SYS_SCL_GROUP_ID_INNER_HASH_VLAN_MAPPING_IGS, "inner_hash_vlan_mapping_igs", NULL, 0 },
        { SYS_SCL_GROUP_ID_INNER_HASH_VLAN_MAPPING_EGS, "inner_hash_vlan_mapping_egs", NULL, 0 },
        { SYS_SCL_GROUP_ID_INNER_DEFT_VLAN_MAPPING_IGS, "inner_deft_vlan_mapping_igs", NULL, 0 },
        { SYS_SCL_GROUP_ID_INNER_DEFT_VLAN_MAPPING_EGS, "inner_deft_vlan_mapping_egs", NULL, 0 },
        { SYS_SCL_GROUP_ID_INNER_HASH_VLAN_CLASS_MAC,   "inner_hash_vlan_class_mac  ", NULL, 0 },
        { SYS_SCL_GROUP_ID_INNER_HASH_VLAN_CLASS_IPV4,  "inner_hash_vlan_class_ipv4 ", NULL, 0 },
        { SYS_SCL_GROUP_ID_INNER_HASH_VLAN_CLASS_IPV6,  "inner_hash_vlan_class_ipv6 ", NULL, 0 },
        { SYS_SCL_GROUP_ID_INNER_TCAM_VLAN_CLASS_MAC,   "inner_tcam_vlan_class_mac  ", NULL, 0 },
        { SYS_SCL_GROUP_ID_INNER_TCAM_VLAN_CLASS_IPV4,  "inner_tcam_vlan_class_ipv4 ", NULL, 0 },
        { SYS_SCL_GROUP_ID_INNER_TCAM_VLAN_CLASS_IPV6,  "inner_tcam_vlan_class_ipv6 ", NULL, 0 },
        { SYS_SCL_GROUP_ID_INNER_DEFT_VLAN_CLASS,       "inner_deft_vlan_class      ", NULL, 0 },
    };

    group_num1 = sizeof(rsv_group1) / sizeof(sys_scl_rsv_group_info_t);
    group_num2 = sizeof(rsv_group2) / sizeof(sys_scl_rsv_group_info_t);


    SYS_SCL_DBG_DUMP("Entry Status :\n");
    SYS_SCL_DBG_DUMP("----------------------------------------------------------------------------\n");


    SYS_SCL_DBG_DUMP("No     GROUP_ID      ENTRY_COUNT\n");
    count = 0;
    SYS_SCL_LOCK(lchip);
    CTC_LIST_LOOP(scl_master[lchip]->group_list[0], pg, node)
    {
        count++;
        SYS_SCL_DBG_DUMP("%03u    0x%08X    %u\n", count, pg->group_id,
                         pg->entry_count);
    }

    SYS_SCL_DBG_DUMP("Outer tcam group count :%u \n\n", count);

    SYS_SCL_DBG_DUMP("----------------------------------------------------------------------------\n");
    SYS_SCL_DBG_DUMP("No     GROUP_ID      NAME                          ENTRY_COUNT\n");
    count = 0;
    for (idx = 0; idx < group_num1; idx++)
    {
        count++;
        _sys_greatbelt_scl_get_group_by_gid(lchip, rsv_group1[idx].group_id, &rsv_group1[idx].pg);
        SYS_SCL_DBG_DUMP("%03u    0x%0X    %-26s    %u \n", count, rsv_group1[idx].group_id,
                         rsv_group1[idx].name, rsv_group1[idx].pg->entry_count);
    }
    SYS_SCL_DBG_DUMP("Outer hash group count :%u \n\n", count);

    SYS_SCL_DBG_DUMP("----------------------------------------------------------------------------\n");
    SYS_SCL_DBG_DUMP("No     GROUP_ID      NAME                          ENTRY_COUNT\n");
    count = 0;
    for (idx = 0; idx < group_num2; idx++)
    {
        count++;
        _sys_greatbelt_scl_get_group_by_gid(lchip, rsv_group2[idx].group_id, &rsv_group2[idx].pg);
        SYS_SCL_DBG_DUMP("%03u    0x%0X    %-26s   %u \n", count, rsv_group2[idx].group_id,
                         rsv_group2[idx].name, rsv_group2[idx].pg->entry_count);
    }
    SYS_SCL_UNLOCK(lchip);

    SYS_SCL_DBG_DUMP("Inner group count :%u \n", count);

    return CTC_E_NONE;
}


int32
sys_greatbelt_scl_show_status(uint8 lchip)
{
    SYS_SCL_INIT_CHECK(lchip) ;

    SYS_SCL_DBG_DUMP("================= SCL Overall Status ==================\n");

        SYS_SCL_DBG_DUMP("\n");
        _sys_greatbelt_scl_show_priority_status(lchip);
    _sys_greatbelt_scl_show_entry_status(lchip);

    return CTC_E_NONE;
}




#define ___________SCL_OUTER_FUNCTION________________________

#define _1_SCL_GROUP_

/*
 * create an scl group.
 * For init api, group_info can be NULL.
 * For out using, group_info must not be NULL.
 *
 */
int32
sys_greatbelt_scl_create_group(uint8 lchip, uint32 group_id, ctc_scl_group_info_t* group_info, uint8 inner)
{
    sys_scl_sw_group_t* pg_new = NULL;
    sys_scl_sw_group_t* pg     = NULL;
    int32             ret;

    SYS_SCL_INIT_CHECK(lchip) ;
    if (!inner)
    {
        SYS_SCL_CHECK_OUTER_NORMAL_GROUP_ID(group_id);
    }
    /* inner group self assured. */

    SYS_SCL_DBG_FUNC();
    SYS_SCL_DBG_PARAM("  %% PARAM: group_id: %u \n", group_id);

    /*
     *  group_id is uint32.
     *  #1 check block_num from p_scl_master. precedence cannot bigger than block_num.
     *  #2 malloc a sys_scl_sw_group_t, add to hash based on group_id.
     */

    SYS_SCL_LOCK(lchip);
    /* check if group exist */
    _sys_greatbelt_scl_get_group_by_gid(lchip, group_id, &pg);
    if (pg)
    {
        SYS_SCL_UNLOCK(lchip);
        return CTC_E_SCL_GROUP_EXIST;
    }
    SYS_SCL_UNLOCK(lchip);

    /* malloc an empty group */

    MALLOC_ZERO(MEM_SCL_MODULE, pg_new, sizeof(sys_scl_sw_group_t));
    if (!pg_new)
    {
        return CTC_E_NO_MEMORY;
    }

    if (SYS_SCL_IS_TCAM_GROUP(group_id))
    {
        if (!group_info)
        {
            ret = CTC_E_INVALID_PTR;
            goto cleanup;
        }
        if (!inner)
        {
            CTC_ERROR_GOTO(_sys_greatbelt_scl_check_group_info(lchip, group_info, group_info->type, 1), ret, cleanup);
        }
    }

    CTC_ERROR_GOTO(_sys_greatbelt_scl_map_group_info(lchip, &pg_new->group_info, group_info, 1), ret, cleanup);

    pg_new->group_id   = group_id;
    pg_new->entry_list = ctc_slist_new();


    SYS_SCL_LOCK(lchip);
    /* add to hash */
    ctc_hash_insert(scl_master[lchip]->group, pg_new);

    if (!inner) /* outer tcam */
    {
        ctc_listnode_add_sort(scl_master[lchip]->group_list[0], pg_new);
    }
    else if (group_id <= CTC_SCL_GROUP_ID_HASH_MAX) /* outer hash */
    {
        ctc_listnode_add_sort(scl_master[lchip]->group_list[1], pg_new);
    }
    else /* inner */
    {
        ctc_listnode_add_sort(scl_master[lchip]->group_list[2], pg_new);
    }
    SYS_SCL_UNLOCK(lchip);

    return CTC_E_NONE;
 cleanup:
    mem_free(pg_new);
    return ret;
}

/*
 * destroy an empty group.
 */
int32
sys_greatbelt_scl_destroy_group(uint8 lchip, uint32 group_id, uint8 inner)
{
    sys_scl_sw_group_t* pg = NULL;

    SYS_SCL_INIT_CHECK(lchip) ;
    if (!inner)
    {
        SYS_SCL_CHECK_OUTER_NORMAL_GROUP_ID(group_id);
    }
    /* inner user won't destroy group */

    SYS_SCL_DBG_FUNC();
    SYS_SCL_DBG_PARAM("  %% PARAM: group_id: %u \n", group_id);

    /*
     * #1 check if entry all removed.
     * #2 remove from hash. free sys_scl_sw_group_t.
     */

    SYS_SCL_LOCK(lchip);
    /* check if group exist */
    _sys_greatbelt_scl_get_group_by_gid(lchip, group_id, &pg);
    SYS_SCL_CHECK_GROUP_UNEXIST(pg);

    /* check if all entry removed */
    if (0 != pg->entry_list->count)
    {
        SYS_SCL_UNLOCK(lchip);
        return CTC_E_SCL_GROUP_NOT_EMPTY;
    }

    ctc_hash_remove(scl_master[lchip]->group, pg);
    if (!inner) /* outer tcam */
    {
        ctc_listnode_delete(scl_master[lchip]->group_list[0], pg);
    }
    else if (group_id <= CTC_SCL_GROUP_ID_HASH_MAX) /* outer hash */
    {
        ctc_listnode_delete(scl_master[lchip]->group_list[1], pg);
    }
    else /* inner */
    {
        ctc_listnode_delete(scl_master[lchip]->group_list[2], pg);
    }
    SYS_SCL_UNLOCK(lchip);

    /* free slist */
    ctc_slist_free(pg->entry_list);

    /* free pg */
    mem_free(pg);

    return CTC_E_NONE;
}

/*
 * install a group (empty or NOT) to hardware table
 */
int32
sys_greatbelt_scl_install_group(uint8 lchip, uint32 group_id, ctc_scl_group_info_t* group_info, uint8 inner)
{
    sys_scl_sw_group_t    * pg        = NULL;
    uint8                 install_all = 0;
    struct ctc_slistnode_s* pe;

    uint32                eid = 0;

    SYS_SCL_INIT_CHECK(lchip) ;

    SYS_SCL_DBG_FUNC();
    SYS_SCL_DBG_PARAM("  %% PARAM: gid %u \n", group_id);

    if (!inner)
    {
        SYS_SCL_CHECK_OUTER_GROUP_ID(group_id);
    }

    SYS_SCL_LOCK(lchip);
    /* get group node */
    _sys_greatbelt_scl_get_group_by_gid(lchip, group_id, &pg);
    SYS_SCL_CHECK_GROUP_UNEXIST(pg);

    if (group_info) /* group_info != NULL*/
    {
        bool is_new;

        /* if group_info is new, rewrite all entries */
        is_new = _sys_greatbelt_scl_is_group_info_new(lchip, &pg->group_info, group_info);
        if (is_new)
        {
            install_all = 1;
        }
        else
        {
            install_all = 0;
        }
    }
    else    /*group info is NULL */
    {
        install_all = 0;
    }


    if (install_all) /* if install_all, group_info is not NULL */
    {                /* traverse all install */
        CTC_ERROR_RETURN_SCL_UNLOCK(_sys_greatbelt_scl_check_group_info(lchip, group_info, pg->group_info.type, 0));

        CTC_ERROR_RETURN_SCL_UNLOCK(_sys_greatbelt_scl_map_group_info(lchip, &pg->group_info, group_info, 0));

        CTC_SLIST_LOOP(pg->entry_list, pe)
        {
            eid = ((sys_scl_sw_entry_t *) pe)->fpae.entry_id;
            _sys_greatbelt_scl_install_entry(lchip, eid, SYS_SCL_ENTRY_OP_FLAG_ADD, 0);
        }
    }
    else
    {   /* traverse, stop at first installed entry.*/
        if (pg->entry_list)
        {
            for (pe = pg->entry_list->head;
                 pe && (((sys_scl_sw_entry_t *) pe)->fpae.flag != FPA_ENTRY_FLAG_INSTALLED);
                 pe = pe->next)
            {
                eid = ((sys_scl_sw_entry_t *) pe)->fpae.entry_id;
                _sys_greatbelt_scl_install_entry(lchip, eid, SYS_SCL_ENTRY_OP_FLAG_ADD, 0);
            }
        }
    }
    SYS_SCL_UNLOCK(lchip);

    return CTC_E_NONE;
}

/*
 * uninstall a group (empty or NOT) from hardware table
 */
int32
sys_greatbelt_scl_uninstall_group(uint8 lchip, uint32 group_id, uint8 inner)
{
    sys_scl_sw_group_t    * pg = NULL;
    struct ctc_slistnode_s* pe = NULL;

    uint32                eid = 0;

    SYS_SCL_INIT_CHECK(lchip) ;

    SYS_SCL_DBG_FUNC();
    SYS_SCL_DBG_PARAM("  %% PARAM: gid %u \n", group_id);

    if (!inner)
    {
        SYS_SCL_CHECK_OUTER_GROUP_ID(group_id);
    }

    SYS_SCL_LOCK(lchip);
    /* get group node */
    _sys_greatbelt_scl_get_group_by_gid(lchip, group_id, &pg);
    SYS_SCL_CHECK_GROUP_UNEXIST(pg);

    CTC_SLIST_LOOP(pg->entry_list, pe)
    {
        eid = ((sys_scl_sw_entry_t *) pe)->fpae.entry_id;
        CTC_ERROR_RETURN_SCL_UNLOCK(_sys_greatbelt_scl_install_entry(lchip, eid, SYS_SCL_ENTRY_OP_FLAG_DELETE, 0));
    }
    SYS_SCL_UNLOCK(lchip);

    return CTC_E_NONE;
}

/*
 * get group info by group id. NOT For hash group.
 */
int32
sys_greatbelt_scl_get_group_info(uint8 lchip, uint32 group_id, ctc_scl_group_info_t* group_info, uint8 inner)
{
    sys_scl_sw_group_info_t* pinfo = NULL;  /* sys group info */
    sys_scl_sw_group_t     * pg    = NULL;

    SYS_SCL_INIT_CHECK(lchip) ;

    if (!inner)
    {
        SYS_SCL_CHECK_OUTER_GROUP_ID(group_id);
    }

    CTC_PTR_VALID_CHECK(group_info);

    SYS_SCL_DBG_FUNC();
    SYS_SCL_DBG_PARAM("  %% PARAM: gid: %u \n", group_id);

    SYS_SCL_LOCK(lchip);
    _sys_greatbelt_scl_get_group_by_gid(lchip, group_id, &pg);
    SYS_SCL_CHECK_GROUP_UNEXIST(pg);

    pinfo = &(pg->group_info);

    /* get ctc group info based on pinfo (sys group info) */
    CTC_ERROR_RETURN_SCL_UNLOCK(_sys_greatbelt_scl_unmap_group_info(lchip, group_info, pinfo));
    SYS_SCL_UNLOCK(lchip);

    return CTC_E_NONE;
}

#define _2_SCL_ENTRY_

/*
 * install entry to hardware table
 */
int32
sys_greatbelt_scl_install_entry(uint8 lchip, uint32 eid, uint8 inner)
{
    SYS_SCL_INIT_CHECK(lchip) ;

    SYS_SCL_DBG_FUNC();
    SYS_SCL_DBG_PARAM("  %% PARAM: eid %u \n", eid);
    if (!inner)
    {
        SYS_SCL_CHECK_ENTRY_ID(eid);
    }

    SYS_SCL_LOCK(lchip);
    CTC_ERROR_RETURN_SCL_UNLOCK(_sys_greatbelt_scl_install_entry(lchip, eid, SYS_SCL_ENTRY_OP_FLAG_ADD, 1));
    SYS_SCL_UNLOCK(lchip);

    return CTC_E_NONE;
}


/*
 * uninstall entry from hardware table
 */
int32
sys_greatbelt_scl_uninstall_entry(uint8 lchip, uint32 eid, uint8 inner)
{
    SYS_SCL_INIT_CHECK(lchip) ;

    SYS_SCL_DBG_FUNC();
    SYS_SCL_DBG_PARAM("  %% PARAM: eid %u \n", eid);

    if (!inner)
    {
        SYS_SCL_CHECK_ENTRY_ID(eid);
    }

    SYS_SCL_LOCK(lchip);
    CTC_ERROR_RETURN_SCL_UNLOCK(_sys_greatbelt_scl_install_entry(lchip, eid, SYS_SCL_ENTRY_OP_FLAG_DELETE, 1));
    SYS_SCL_UNLOCK(lchip);

    return CTC_E_NONE;
}


/* get entry id by key */
int32
sys_greatbelt_scl_get_entry_id(uint8 lchip, sys_scl_entry_t* scl_entry, uint32 gid)
{
    sys_scl_sw_entry_t e_tmp;
    sys_scl_sw_entry_t * pe_lkup;
    sys_scl_sw_group_t * pg = NULL;

    CTC_PTR_VALID_CHECK(scl_entry);

    sal_memset(&e_tmp, 0, sizeof(e_tmp));

    CTC_ERROR_RETURN(_sys_greatbelt_scl_map_key(lchip, &scl_entry->key, &e_tmp.key));

    SYS_SCL_LOCK(lchip);
    CTC_ERROR_RETURN_SCL_UNLOCK(_sys_greatbelt_scl_get_group_by_gid(lchip, gid, &pg));
    SYS_SCL_CHECK_GROUP_UNEXIST(pg);

    /* must set group */
    e_tmp.group = pg;
    e_tmp.fpae.lchip = lchip;
    _sys_greatbelt_scl_get_nodes_by_key(lchip, &e_tmp, &pe_lkup, NULL);
    if (!pe_lkup) /* hash's key must be unique */
    {
        SYS_SCL_UNLOCK(lchip);
        return CTC_E_ENTRY_NOT_EXIST;
    }
    scl_entry->entry_id = pe_lkup->fpae.entry_id;
    SYS_SCL_UNLOCK(lchip);

    return CTC_E_NONE;
}

int32
sys_greatbelt_scl_get_entry(uint8 lchip, sys_scl_entry_t* scl_entry, uint32 gid)
{
    CTC_PTR_VALID_CHECK(scl_entry);

    CTC_ERROR_RETURN(sys_greatbelt_scl_get_entry_id(lchip, scl_entry, gid));
    CTC_ERROR_RETURN(sys_greatbelt_scl_get_action(lchip, scl_entry->entry_id, &(scl_entry->action), 1));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_scl_check_action_type(uint8 lchip, sys_scl_sw_key_t* key, uint32 action_type)
{
    uint8 match     = 0;
    uint8 dir_valid = 0;
    uint8 dir       = 0;
    switch (key->type)
    {
    case SYS_SCL_KEY_TCAM_VLAN:
    case SYS_SCL_KEY_TCAM_MAC:
    case SYS_SCL_KEY_TCAM_IPV4:
    case SYS_SCL_KEY_TCAM_IPV6:
    case SYS_SCL_KEY_HASH_MAC:
    case SYS_SCL_KEY_HASH_PORT_MAC:
    case SYS_SCL_KEY_HASH_IPV4:
    case SYS_SCL_KEY_HASH_IPV6:
    case SYS_SCL_KEY_HASH_L2:
    case SYS_SCL_KEY_PORT_DEFAULT_INGRESS:
        match = (SYS_SCL_ACTION_INGRESS == action_type);
        break;
    case SYS_SCL_KEY_HASH_PORT_IPV4:
        dir       = key->u0.hash.u0.ipv4.rsv0;
        dir_valid = 1;
        break;

    case SYS_SCL_KEY_HASH_PORT:
        dir       = key->u0.hash.u0.port.dir;
        dir_valid = 1;
        break;
    case SYS_SCL_KEY_HASH_PORT_CVLAN:
    case SYS_SCL_KEY_HASH_PORT_SVLAN:
        dir       = key->u0.hash.u0.vlan.dir;
        dir_valid = 1;
        break;
    case SYS_SCL_KEY_HASH_PORT_2VLAN:
    case SYS_SCL_KEY_HASH_PORT_CVLAN_COS:
    case SYS_SCL_KEY_HASH_PORT_SVLAN_COS:
        dir       = key->u0.hash.u0.vtag.dir;
        dir_valid = 1;
        break;
    case SYS_SCL_KEY_TCAM_TUNNEL_IPV4:
    case SYS_SCL_KEY_TCAM_TUNNEL_IPV6:
    case SYS_SCL_KEY_HASH_TUNNEL_IPV4:
    case SYS_SCL_KEY_HASH_TUNNEL_IPV4_GRE:
    case SYS_SCL_KEY_HASH_TUNNEL_IPV4_RPF:
        match = (SYS_SCL_ACTION_TUNNEL == action_type);
        break;
    case SYS_SCL_KEY_PORT_DEFAULT_EGRESS:
        match = (SYS_SCL_ACTION_EGRESS == action_type);
        break;
    default:
        return CTC_E_INVALID_PARAM;
    }

    if (dir_valid)
    {
        CTC_MAX_VALUE_CHECK(dir, CTC_BOTH_DIRECTION - 1);
        match = (((dir == CTC_INGRESS) && (action_type == SYS_SCL_ACTION_INGRESS))
                 || ((dir == CTC_EGRESS) && (action_type == SYS_SCL_ACTION_EGRESS)));
    }

    if (!match)
    {
        return CTC_E_SCL_KEY_ACTION_TYPE_NOT_MATCH;
    }

    return CTC_E_NONE;
}


int32
sys_greatbelt_scl_add_entry(uint8 lchip, uint32 group_id, sys_scl_entry_t* scl_entry, uint8 inner)
{
    sys_scl_sw_group_t     * pg      = NULL;
    sys_scl_sw_entry_t     * pe_lkup = NULL;

    sys_scl_sw_entry_t     e_tmp;
    sys_scl_sw_action_t       a_tmp;
    sys_scl_sw_vlan_edit_t   v_tmp;
    int32                  ret = 0;

    SYS_SCL_INIT_CHECK(lchip) ;
    CTC_PTR_VALID_CHECK(scl_entry);

    SYS_SCL_DBG_FUNC();
    SYS_SCL_DBG_PARAM("  %% PARAM: eid %u -- key_type %u\n",
                      scl_entry->entry_id, scl_entry->key.type);

    SYS_SCL_LOCK(lchip);
    if (inner)
    {
        sys_greatbelt_opf_t opf;
        uint32              entry_id = 0;

        sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));
        opf.pool_index = 0; /* care not chip */
        opf.pool_type  = OPF_SCL_ENTRY_ID;

        CTC_ERROR_RETURN(sys_greatbelt_opf_alloc_offset(lchip, &opf, 1, &entry_id));
        scl_entry->entry_id = entry_id + SYS_SCL_MIN_INNER_ENTRY_ID;
    }
    else
    {
        SYS_SCL_CHECK_OUTER_GROUP_ID(group_id);
        SYS_SCL_CHECK_ENTRY_ID(scl_entry->entry_id);
        _sys_greatbelt_scl_get_nodes_by_eid(lchip, scl_entry->entry_id, &pe_lkup, NULL, NULL);
        if (pe_lkup)
        {
            SYS_SCL_UNLOCK(lchip);
            return CTC_E_ENTRY_EXIST;
        }
    }

    /* check sys group */
    _sys_greatbelt_scl_get_group_by_gid(lchip, group_id, &pg);
    SYS_SCL_CHECK_GROUP_UNEXIST(pg);
    /* group_type is port, but not vlan tcam key, return error */
    if ((CTC_SCL_GROUP_TYPE_PORT == pg->group_info.type) &&
        (SYS_SCL_KEY_TCAM_VLAN != scl_entry->key.type))
    {
        SYS_SCL_UNLOCK(lchip);
        return CTC_E_INVALID_PARAM;
    }
    SYS_SCL_UNLOCK(lchip);

    sal_memset(&e_tmp, 0, sizeof(e_tmp));
    sal_memset(&a_tmp, 0, sizeof(a_tmp));
    sal_memset(&v_tmp, 0, sizeof(v_tmp));
    e_tmp.fpae.lchip = lchip;
    a_tmp.lchip = lchip;
    v_tmp.lchip = lchip;

    if (inner == 0)
    {
        uint8   match = 1;
        switch (scl_entry->key.type)
        {
        case SYS_SCL_KEY_HASH_IPV4:
            match = (group_id == CTC_SCL_GROUP_ID_HASH_IPV4);
            break;
        case SYS_SCL_KEY_HASH_IPV6:
            match = (group_id == CTC_SCL_GROUP_ID_HASH_IPV6);
            break;
        case SYS_SCL_KEY_HASH_L2:
            match = (group_id == CTC_SCL_GROUP_ID_HASH_L2);
            break;
        case SYS_SCL_KEY_HASH_MAC:
            match = (group_id == CTC_SCL_GROUP_ID_HASH_MAC);
            break;
        case SYS_SCL_KEY_HASH_PORT:
            match = (group_id == CTC_SCL_GROUP_ID_HASH_PORT);
            break;
        case SYS_SCL_KEY_HASH_PORT_2VLAN:
            match = (group_id == CTC_SCL_GROUP_ID_HASH_PORT_2VLAN) ;
            break;
        case SYS_SCL_KEY_HASH_PORT_CVLAN:
            match = (group_id == CTC_SCL_GROUP_ID_HASH_PORT_CVLAN) ;
            break;
        case SYS_SCL_KEY_HASH_PORT_CVLAN_COS:
            match = (group_id == CTC_SCL_GROUP_ID_HASH_PORT_CVLAN_COS);
            break;
        case SYS_SCL_KEY_HASH_PORT_IPV4:
            match = (group_id == CTC_SCL_GROUP_ID_HASH_PORT_IPV4);
            break;
        case SYS_SCL_KEY_HASH_PORT_MAC:
            match = (group_id == CTC_SCL_GROUP_ID_HASH_PORT_MAC);
            break;
        case SYS_SCL_KEY_HASH_PORT_SVLAN:
            match = (group_id == CTC_SCL_GROUP_ID_HASH_PORT_SVLAN);
            break;
        case SYS_SCL_KEY_HASH_PORT_SVLAN_COS:
            match = (group_id == CTC_SCL_GROUP_ID_HASH_PORT_SVLAN_COS);
            break;
        default:
            break;
        }

        if (!match)
        {
            return CTC_E_INVALID_PARAM;
        }
    }


    /* build sys entry action, key*/
    CTC_ERROR_RETURN(_sys_greatbelt_scl_map_key(lchip, &scl_entry->key, &e_tmp.key));

    /* get lchip lchip_num */
    e_tmp.group = pg; /* get chip need this */

    a_tmp.type = scl_entry->action.type;
    a_tmp.action_flag = scl_entry->action.action_flag;
    CTC_ERROR_RETURN(_sys_greatbelt_scl_check_action_type(lchip, &e_tmp.key, scl_entry->action.type));
    CTC_ERROR_RETURN(_sys_greatbelt_scl_map_service_queue_action(lchip, &scl_entry->action, &a_tmp, NULL));
    CTC_ERROR_RETURN(_sys_greatbelt_scl_map_action(lchip, &scl_entry->action, &a_tmp, &v_tmp));

    /* build sys entry inner field */
    e_tmp.fpae.entry_id    = scl_entry->entry_id;
    e_tmp.head.next        = NULL;
    if (scl_entry->priority_valid)
    {
        e_tmp.fpae.priority = scl_entry->priority; /* meaningless for HASH */
    }
    else
    {
        e_tmp.fpae.priority = FPA_PRIORITY_DEFAULT; /* meaningless for HASH */
    }

    /* set lchip --> key and action */

     SYS_SCL_LOCK(lchip);
    /* assure inner entry, and outer hash entry is unique. */
    if (SCL_INNER_ENTRY_ID(scl_entry->entry_id) || !SCL_ENTRY_IS_TCAM(scl_entry->key.type))
    {
        _sys_greatbelt_scl_get_nodes_by_key(lchip, &e_tmp, &pe_lkup, NULL);
        if (pe_lkup) /* must be unique */
        {
            SYS_SCL_UNLOCK(lchip);
            return CTC_E_ENTRY_EXIST;
        }
    }

    if (!SCL_ENTRY_IS_TCAM(scl_entry->key.type)) /* hash entry */
    {
        /* hash check +1*/
        if (SYS_SCL_IS_TCAM_GROUP(group_id)) /* hash entry only in hash group */
        {
            SYS_SCL_UNLOCK(lchip);
            return CTC_E_SCL_ADD_HASH_ENTRY_TO_WRONG_GROUP;
        }
    }
    else /* tcam entry */
    {
        /* tcam check +1 */
        if (!SYS_SCL_IS_TCAM_GROUP(group_id)) /* tcam entry only in tcam group */
        {
            SYS_SCL_UNLOCK(lchip);
            return CTC_E_SCL_ADD_TCAM_ENTRY_TO_WRONG_GROUP;
        }
    }

    CTC_ERROR_GOTO(_sys_greatbelt_scl_add_entry(lchip, group_id, &e_tmp, &a_tmp, &v_tmp), ret, cleanup);
    SYS_SCL_UNLOCK(lchip);
    return CTC_E_NONE;

 cleanup:
    _sys_greatbelt_scl_remove_accessory_property(lchip, &e_tmp, &a_tmp);
    SYS_SCL_UNLOCK(lchip);
    return ret;
}


/*
 * remove entry from software table
 */
int32
sys_greatbelt_scl_remove_entry(uint8 lchip, uint32 entry_id, uint8 inner)
{
    SYS_SCL_INIT_CHECK(lchip) ;

    SYS_SCL_DBG_FUNC();
    SYS_SCL_DBG_PARAM("  %% PARAM: eid %u \n", entry_id);

    if (!inner)
    {
        SYS_SCL_CHECK_ENTRY_ID(entry_id);
    }

    SYS_SCL_LOCK(lchip);
    CTC_ERROR_RETURN_SCL_UNLOCK(_sys_greatbelt_scl_remove_entry(lchip, entry_id, inner));
    SYS_SCL_UNLOCK(lchip);


    return CTC_E_NONE;
}

/*
 * remove all entries from a group
 */
int32
sys_greatbelt_scl_remove_all_entry(uint8 lchip, uint32 group_id, uint8 inner)
{
    sys_scl_sw_group_t    * pg      = NULL;
    struct ctc_slistnode_s* pe      = NULL;
    struct ctc_slistnode_s* pe_next = NULL;
    uint32                eid       = 0;

    SYS_SCL_INIT_CHECK(lchip) ;
    if (!inner)
    {
        SYS_SCL_CHECK_OUTER_GROUP_ID(group_id);
    }


    SYS_SCL_DBG_FUNC();
    SYS_SCL_DBG_PARAM("  %% PARAM: gid %u \n", group_id);

    SYS_SCL_LOCK(lchip);
    /* get group node */
    _sys_greatbelt_scl_get_group_by_gid(lchip, group_id, &pg);
    SYS_SCL_CHECK_GROUP_UNEXIST(pg);

    /* check if all uninstalled */
    CTC_SLIST_LOOP(pg->entry_list, pe)
    {
        if (FPA_ENTRY_FLAG_INSTALLED ==
            ((sys_scl_sw_entry_t *) pe)->fpae.flag)
        {
            SYS_SCL_UNLOCK(lchip);
            return CTC_E_SCL_ENTRY_INSTALLED;
        }
    }

    CTC_SLIST_LOOP_DEL(pg->entry_list, pe, pe_next)
    {
        eid = ((sys_scl_sw_entry_t *) pe)->fpae.entry_id;
        /* no stop to keep consitent */
        _sys_greatbelt_scl_remove_entry(lchip, eid, inner);
    }
    SYS_SCL_UNLOCK(lchip);

    return CTC_E_NONE;
}



/*
 * set priority of entry
 */
int32
_sys_greatbelt_scl_set_entry_priority(uint8 lchip, uint32 entry_id, uint32 priority, uint8 inner)
{
    sys_scl_sw_entry_t* pe = NULL;
    sys_scl_sw_group_t* pg = NULL;
    sys_scl_sw_block_t* pb = NULL;

    /* get sys entry */
    _sys_greatbelt_scl_get_nodes_by_eid(lchip, entry_id, &pe, &pg, &pb);
    if(!pe)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }
    if(!pg)
    {
        return CTC_E_SCL_GROUP_UNEXIST;
    }

    if (!SCL_ENTRY_IS_TCAM(pe->key.type)) /* hash entry */
    {
        return CTC_E_SCL_HASH_ENTRY_NO_PRIORITY;
    }

    /* tcam entry check pb */
    CTC_PTR_VALID_CHECK(pb);
        CTC_ERROR_RETURN(fpa_greatbelt_set_entry_prio(scl_master[lchip]->fpa, &pe->fpae, &pb->fpab, priority));
    return CTC_E_NONE;
}

int32
sys_greatbelt_scl_set_entry_priority(uint8 lchip, uint32 entry_id, uint32 priority, uint8 inner)
{
    SYS_SCL_INIT_CHECK(lchip) ;

    if (!inner)
    {
        SYS_SCL_CHECK_ENTRY_ID(entry_id);
    }

    SYS_SCL_CHECK_ENTRY_PRIO(priority);

    SYS_SCL_DBG_FUNC();
    SYS_SCL_DBG_PARAM("  %% PARAM: eid %u -- prio %u \n", entry_id, priority);


    SYS_SCL_LOCK(lchip);
    CTC_ERROR_RETURN_SCL_UNLOCK(_sys_greatbelt_scl_set_entry_priority(lchip, entry_id, priority, inner));
    SYS_SCL_UNLOCK(lchip);

    return CTC_E_NONE;
}

/*
 * get multiple entries
 */
int32
sys_greatbelt_scl_get_multi_entry(uint8 lchip, ctc_scl_query_t* query, uint8 inner)
{
    uint32                entry_index = 0;
    sys_scl_sw_group_t    * pg        = NULL;
    struct ctc_slistnode_s* pe        = NULL;

    SYS_SCL_INIT_CHECK(lchip) ;
    CTC_PTR_VALID_CHECK(query);
    if (!inner)
    {
        SYS_SCL_CHECK_OUTER_GROUP_ID(query->group_id);
    }

    SYS_SCL_DBG_FUNC();
    SYS_SCL_DBG_PARAM("  %% PARAM: gid %u -- entry_sz %u\n", query->group_id, query->entry_size);

    SYS_SCL_LOCK(lchip);
    /* get group node */
    _sys_greatbelt_scl_get_group_by_gid(lchip, query->group_id, &pg);
    SYS_SCL_CHECK_GROUP_UNEXIST(pg);

    if (query->entry_size == 0)
    {
        query->entry_count = pg->entry_count;
    }
    else
    {
        uint32* p_array = query->entry_array;
        if (NULL == p_array)
        {
            SYS_SCL_UNLOCK(lchip);
            return CTC_E_INVALID_PTR;
        }

        if (query->entry_size > pg->entry_count)
        {
            query->entry_size = pg->entry_count;
        }

        CTC_SLIST_LOOP(pg->entry_list, pe)
        {
            *p_array = ((sys_scl_sw_entry_t *) pe)->fpae.entry_id;
            p_array++;
            entry_index++;
            if (entry_index == query->entry_size)
            {
                break;
            }
        }

        query->entry_count = query->entry_size;
    }
    SYS_SCL_UNLOCK(lchip);

    return CTC_E_NONE;
}

int32
sys_greatbelt_scl_copy_entry(uint8 lchip, ctc_scl_copy_entry_t* copy_entry, uint8 inner)
{
    sys_scl_sw_entry_t * pe_src = NULL;

    sys_scl_sw_entry_t * pe_dst    = NULL;
    sys_scl_sw_action_t* pa_dst    = NULL;
    sys_scl_sw_block_t * pb_dst    = NULL;
    sys_scl_sw_group_t * pg_dst    = NULL;
    int32              block_index = 0;
    uint8              asic_type   = 0;
    int32              ret         = 0;
    uint8              is_hash;

    SYS_SCL_INIT_CHECK(lchip) ;
    CTC_PTR_VALID_CHECK(copy_entry);
    if (!inner)
    {
        SYS_SCL_CHECK_ENTRY_ID(copy_entry->dst_entry_id);
        SYS_SCL_CHECK_ENTRY_ID(copy_entry->src_entry_id);
        SYS_SCL_CHECK_OUTER_GROUP_ID(copy_entry->dst_group_id);
    }
    SYS_SCL_DBG_FUNC();

    SYS_SCL_LOCK(lchip);
    /* check src entry */
    _sys_greatbelt_scl_get_nodes_by_eid(lchip, copy_entry->src_entry_id, &pe_src, NULL, NULL);
    if (!pe_src)
    {
        SYS_SCL_UNLOCK(lchip);
        return CTC_E_ENTRY_NOT_EXIST;
    }

    is_hash = !(SCL_ENTRY_IS_TCAM(pe_src->key.type));
    if (is_hash) /* hash entry not support copy */
    {
        SYS_SCL_UNLOCK(lchip);
        return CTC_E_SCL_HASH_ENTRY_UNSUPPORT_COPY;
    }

    /* check dst entry */
    _sys_greatbelt_scl_get_nodes_by_eid(lchip, copy_entry->dst_entry_id, &pe_dst, NULL, NULL);
    if (pe_dst)
    {
        SYS_SCL_UNLOCK(lchip);
        return CTC_E_ENTRY_EXIST;
    }

    _sys_greatbelt_scl_get_group_by_gid(lchip, copy_entry->dst_group_id, &pg_dst);
    SYS_SCL_CHECK_GROUP_UNEXIST(pg_dst);


    if (pg_dst->group_info.type == CTC_SCL_GROUP_TYPE_HASH)
    {
        SYS_SCL_UNLOCK(lchip);
        return CTC_E_INVALID_PARAM;
    }

    MALLOC_ZERO(MEM_SCL_MODULE, pe_dst, scl_master[lchip]->entry_size[pe_src->key.type]);
    if (!pe_dst)
    {
        SYS_SCL_UNLOCK(lchip);
        return CTC_E_NO_MEMORY;
    }

    MALLOC_ZERO(MEM_SCL_MODULE, pa_dst, sizeof(sys_scl_sw_action_t));
    if (!pa_dst)
    {
        SYS_SCL_UNLOCK(lchip);
        mem_free(pe_dst);
        return CTC_E_NO_MEMORY;
    }

    /* copy entry. if old entry has vlan edit. use it, no need to malloc.
                   hash not support copy, tcam ad is unique, so must malloc.*/

    sal_memcpy(pe_dst, pe_src, scl_master[lchip]->entry_size[pe_src->key.type]);
    sal_memcpy(pa_dst, pe_src->action, sizeof(sys_scl_sw_action_t));
    pe_dst->action = pa_dst;


    if ((SYS_SCL_ACTION_INGRESS == pe_src->action->type)
        && (CTC_FLAG_ISSET(pe_src->action->u0.ia.flag, CTC_SCL_IGS_ACTION_FLAG_VLAN_EDIT)))
    {   /* vlan edit share pool ++ */
     /* pv_get be NULL, */
        CTC_ERROR_GOTO(_sys_greatbelt_scl_hash_vlan_edit_spool_add(lchip, pe_dst, pe_dst->action->u0.ia.vlan_edit, NULL), ret, cleanup);
    }

    pe_dst->fpae.entry_id    = copy_entry->dst_entry_id;
    pe_dst->fpae.priority = pe_src->fpae.priority;
    pe_dst->fpae.lchip = pe_src->fpae.lchip;
     /*pe_dst->fpae.priority = pe_src->fpae.priority;*/
    pe_dst->group            = pg_dst;
    pe_dst->head.next        = NULL;

    asic_type = scl_master[lchip]->alloc_id[pe_dst->key.type];
    pb_dst    = &scl_master[lchip]->block[asic_type];
    if (NULL == pb_dst)
    {
        ret = CTC_E_INVALID_PTR;
        goto cleanup;
    }

    /* get block index, based on priority */
        CTC_ERROR_GOTO(fpa_greatbelt_alloc_offset(scl_master[lchip]->fpa, &(pb_dst->fpab),
        pe_src->fpae.priority, &block_index), ret, cleanup);
        pe_dst->fpae.offset_a = block_index;

    /* add to hash */
    ctc_hash_insert(scl_master[lchip]->entry, pe_dst);
    if (SCL_INNER_ENTRY_ID(pe_dst->fpae.entry_id) || !SCL_ENTRY_IS_TCAM(pe_dst->key.type))
    {
        ctc_hash_insert(scl_master[lchip]->entry_by_key, pe_dst);
    }

    /* add to group */
    ctc_slist_add_head(pg_dst->entry_list, &(pe_dst->head));

    /* mark flag */
    pe_dst->fpae.flag = FPA_ENTRY_FLAG_UNINSTALLED;
        /* add to block */
        pb_dst->fpab.entries[block_index] = &pe_dst->fpae;

        /* free_count-- */
        (pb_dst->fpab.free_count)--;

        /* set new priority */
        CTC_ERROR_RETURN(_sys_greatbelt_scl_set_entry_priority(lchip, copy_entry->dst_entry_id, pe_src->fpae.priority, inner));

    (pg_dst->entry_count)++;
    SYS_SCL_UNLOCK(lchip);

    return CTC_E_NONE;

 cleanup:
    SYS_SCL_UNLOCK(lchip);
    mem_free(pa_dst);
    mem_free(pe_dst);
    return ret;
}




int32
sys_greatbelt_scl_update_action(uint8 lchip, uint32 entry_id, sys_scl_action_t* action, uint8 inner)
{
    sys_scl_sw_entry_t     * pe_old    = NULL;
    sys_scl_sw_action_t    new_action;
    sys_scl_sw_vlan_edit_t new_vlan_edit;

    int32                  ret = 0;
    sys_scl_sw_action_t    * pa_get = NULL;
    sys_scl_sw_vlan_edit_t * pv_get = NULL;

    SYS_SCL_INIT_CHECK(lchip) ;
    CTC_PTR_VALID_CHECK(action);
    SYS_SCL_DBG_FUNC();

    if (!inner)
    {
        SYS_SCL_CHECK_ENTRY_ID(entry_id);
    }

    sal_memset(&new_action, 0, sizeof(new_action));
    sal_memset(&new_vlan_edit, 0, sizeof(new_vlan_edit));

    new_action.type = action->type;
    new_action.action_flag = action->action_flag;

    SYS_SCL_LOCK(lchip);
    /* lookup */
    _sys_greatbelt_scl_get_nodes_by_eid(lchip, entry_id, &pe_old, NULL, NULL);
    if (!pe_old)
    {
        SYS_SCL_UNLOCK(lchip);
        return CTC_E_ENTRY_NOT_EXIST;
    }

    CTC_ERROR_RETURN_SCL_UNLOCK(_sys_greatbelt_scl_check_action_type(lchip, &pe_old->key, action->type));
    CTC_ERROR_RETURN_SCL_UNLOCK(_sys_greatbelt_scl_map_service_queue_action(lchip, action, &new_action, pe_old->action));

    CTC_ERROR_RETURN_SCL_UNLOCK(_sys_greatbelt_scl_map_action(lchip, action, &new_action, &new_vlan_edit));

    if (SCL_ENTRY_IS_TCAM(pe_old->key.type))
    {
        /* 1. add vlan edit*/
        if ((SYS_SCL_ACTION_INGRESS == action->type)
            && (CTC_FLAG_ISSET(new_action.u0.ia.flag, CTC_SCL_IGS_ACTION_FLAG_VLAN_EDIT)))
        {
            CTC_ERROR_RETURN_SCL_UNLOCK(_sys_greatbelt_scl_hash_vlan_edit_spool_add(lchip, pe_old, &new_vlan_edit, &pv_get));

            if (pv_get)
            {
                new_action.u0.ia.vlan_edit                 = pv_get;
                new_action.u0.ia.vlan_action_profile_valid = 1;
                    new_action.u0.ia.chip.profile_id = pv_get->chip.profile_id;
            }
        }

        /* if old action has vlan edit. delete it. */
        if ((SYS_SCL_ACTION_INGRESS == pe_old->action->type)
            && (CTC_FLAG_ISSET(pe_old->action->u0.ia.flag, CTC_SCL_IGS_ACTION_FLAG_VLAN_EDIT)))
        {
            CTC_ERROR_RETURN_SCL_UNLOCK(_sys_greatbelt_scl_hash_vlan_edit_spool_remove(lchip, pe_old, pe_old->action->u0.ia.vlan_edit));
        }

        sal_memcpy(pe_old->action, &new_action, sizeof(sys_scl_sw_action_t));
    }
    else
    {
        /* 1. add vlan edit*/
        if ((SYS_SCL_ACTION_INGRESS == action->type)
            && (CTC_FLAG_ISSET(new_action.u0.ia.flag, CTC_SCL_IGS_ACTION_FLAG_VLAN_EDIT)))
        {
            CTC_ERROR_RETURN_SCL_UNLOCK(_sys_greatbelt_scl_hash_vlan_edit_spool_add(lchip, pe_old, &new_vlan_edit, &pv_get));

            if (pv_get)
            {
                new_action.u0.ia.vlan_edit                 = pv_get;
                new_action.u0.ia.vlan_action_profile_valid = 1;
                    new_action.u0.ia.chip.profile_id = pv_get->chip.profile_id;
            }
        }

        /* 1. add ad */
        if(!SCL_ENTRY_IS_SMALL_TCAM(pe_old->key.conv_type)) /* hash */
        {
            ret = (_sys_greatbelt_scl_hash_ad_spool_add(lchip, pe_old, &new_action, &pa_get));
            if FAIL(ret)
            {
                if (pv_get)
                {
                    _sys_greatbelt_scl_hash_vlan_edit_spool_remove(lchip, pe_old, pv_get);
                }
                SYS_SCL_UNLOCK(lchip);
                return ret;
            }
        }
        else /* small tcam */
        {
            ret = (_sys_greatbelt_scl_small_ad_add(lchip, &new_action, &pa_get));
            if FAIL(ret)
            {
                if (pv_get)
                {
                    _sys_greatbelt_scl_hash_vlan_edit_spool_remove(lchip, pe_old, pv_get);
                }
                SYS_SCL_UNLOCK(lchip);
                return ret;
            }
        }


        /* if old action has vlan edit. delete it. */
        if ((SYS_SCL_ACTION_INGRESS == pe_old->action->type)
            && (CTC_FLAG_ISSET(pe_old->action->u0.ia.flag, CTC_SCL_IGS_ACTION_FLAG_VLAN_EDIT)))
        {
            CTC_ERROR_GOTO(_sys_greatbelt_scl_hash_vlan_edit_spool_remove(lchip, pe_old, pe_old->action->u0.ia.vlan_edit), ret, error);
        }

        /* old action must be delete */
        if(!SCL_ENTRY_IS_SMALL_TCAM(pe_old->key.conv_type)) /* hash */
        {
            CTC_ERROR_GOTO(_sys_greatbelt_scl_hash_ad_spool_remove(lchip, pe_old, pe_old->action), ret, error);
        }
        else
        {
            CTC_ERROR_GOTO(_sys_greatbelt_scl_small_ad_remove(lchip, pe_old->action), ret, error);
        }

        pe_old->action = pa_get;
    }

    if (pe_old->fpae.flag == FPA_ENTRY_FLAG_INSTALLED)
    {
            CTC_ERROR_RETURN_SCL_UNLOCK(_sys_greatbelt_scl_add_hw(lchip, entry_id));
    }
    SYS_SCL_UNLOCK(lchip);

    return CTC_E_NONE;
error:
    SYS_SCL_UNLOCK(lchip);
    if (pa_get)
    {
        mem_free(pa_get);
    }
    return ret;
}

/*
 * set scl register
 */
STATIC int32
_sys_greatbelt_scl_set_register(uint8 lchip, sys_scl_register_t* p_reg)
{
    uint32  cmd               = 0;
    user_id_hash_lookup_ctl_t user_id_hash_lookup_ctl;

    sal_memset(&user_id_hash_lookup_ctl, 0, sizeof(user_id_hash_lookup_ctl));
    cmd = DRV_IOR(UserIdHashLookupCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &user_id_hash_lookup_ctl));

    user_id_hash_lookup_ctl.l2_cos_en = 0;
    user_id_hash_lookup_ctl.l2_mac_da_en = 1;
    user_id_hash_lookup_ctl.l2_mac_sa_en = 1;
    user_id_hash_lookup_ctl.l2_src_port_en = 1;
    user_id_hash_lookup_ctl.l2_vlan_id_en = 1;
    cmd = DRV_IOW(UserIdHashLookupCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &user_id_hash_lookup_ctl));
    return CTC_E_NONE;
}


/*
 * init scl register
 */
STATIC int32
_sys_greatbelt_scl_init_register(uint8 lchip, void* scl_global_cfg)
{
/*    CTC_PTR_VALID_CHECK(scl_global_cfg);*/

    CTC_ERROR_RETURN(_sys_greatbelt_scl_set_register(lchip, NULL));
    return CTC_E_NONE;
}


STATIC int32
_sys_greatbelt_scl_create_rsv_group(uint8 lchip)
{
    ctc_scl_group_info_t global_group;
    ctc_scl_group_info_t hash_group;
    ctc_scl_group_info_t class_group;
    sal_memset(&global_group, 0, sizeof(global_group));
    sal_memset(&hash_group, 0, sizeof(hash_group));
    sal_memset(&class_group, 0, sizeof(class_group));

    global_group.type = CTC_SCL_GROUP_TYPE_GLOBAL;
    hash_group.type   = CTC_SCL_GROUP_TYPE_HASH;
    class_group.type  = CTC_SCL_GROUP_TYPE_PORT_CLASS;
    class_group.lchip = lchip; /* this is not for outer api, just for internal. Outer will check lchip, inner won't.  */

    CTC_ERROR_RETURN(sys_greatbelt_scl_create_group(lchip, CTC_SCL_GROUP_ID_HASH_PORT, &hash_group, 1));
    CTC_ERROR_RETURN(sys_greatbelt_scl_create_group(lchip, CTC_SCL_GROUP_ID_HASH_PORT_CVLAN, &hash_group, 1));
    CTC_ERROR_RETURN(sys_greatbelt_scl_create_group(lchip, CTC_SCL_GROUP_ID_HASH_PORT_SVLAN, &hash_group, 1));
    CTC_ERROR_RETURN(sys_greatbelt_scl_create_group(lchip, CTC_SCL_GROUP_ID_HASH_PORT_2VLAN, &hash_group, 1));
    CTC_ERROR_RETURN(sys_greatbelt_scl_create_group(lchip, CTC_SCL_GROUP_ID_HASH_PORT_CVLAN_COS, &hash_group, 1));
    CTC_ERROR_RETURN(sys_greatbelt_scl_create_group(lchip, CTC_SCL_GROUP_ID_HASH_PORT_SVLAN_COS, &hash_group, 1));
    CTC_ERROR_RETURN(sys_greatbelt_scl_create_group(lchip, CTC_SCL_GROUP_ID_HASH_MAC, &hash_group, 1));
    CTC_ERROR_RETURN(sys_greatbelt_scl_create_group(lchip, CTC_SCL_GROUP_ID_HASH_PORT_MAC, &hash_group, 1));
    CTC_ERROR_RETURN(sys_greatbelt_scl_create_group(lchip, CTC_SCL_GROUP_ID_HASH_IPV4, &hash_group, 1));
    CTC_ERROR_RETURN(sys_greatbelt_scl_create_group(lchip, CTC_SCL_GROUP_ID_HASH_PORT_IPV4, &hash_group, 1));
    CTC_ERROR_RETURN(sys_greatbelt_scl_create_group(lchip, CTC_SCL_GROUP_ID_HASH_IPV6, &hash_group, 1));
    CTC_ERROR_RETURN(sys_greatbelt_scl_create_group(lchip, CTC_SCL_GROUP_ID_HASH_L2, &hash_group, 1));

    /* ip tunnel hash & tcam*/
    CTC_ERROR_RETURN(sys_greatbelt_scl_create_group(lchip, SYS_SCL_GROUP_ID_INNER_HASH_IP_TUNNEL, &hash_group, 1));
    CTC_ERROR_RETURN(sys_greatbelt_scl_create_group(lchip, SYS_SCL_GROUP_ID_INNER_TCAM_IP_TUNNEL, &global_group, 1));

    /* ip src guard hash & default(use tcam)*/
    CTC_ERROR_RETURN(sys_greatbelt_scl_create_group(lchip, SYS_SCL_GROUP_ID_INNER_HASH_IP_SRC_GUARD, &hash_group, 1));
    class_group.un.port_class_id = SYS_SCL_LABEL_RESERVE_FOR_IPSG;
    CTC_ERROR_RETURN(sys_greatbelt_scl_create_group(lchip, SYS_SCL_GROUP_ID_INNER_DEFT_IP_SRC_GUARD, &class_group, 1));

    /* vlan mapping hash & default(use hash)*/
    CTC_ERROR_RETURN(sys_greatbelt_scl_create_group(lchip, SYS_SCL_GROUP_ID_INNER_HASH_VLAN_MAPPING_IGS, &hash_group, 1));
    CTC_ERROR_RETURN(sys_greatbelt_scl_create_group(lchip, SYS_SCL_GROUP_ID_INNER_DEFT_VLAN_MAPPING_IGS, &hash_group, 1));
    CTC_ERROR_RETURN(sys_greatbelt_scl_create_group(lchip, SYS_SCL_GROUP_ID_INNER_HASH_VLAN_MAPPING_EGS, &hash_group, 1));
    CTC_ERROR_RETURN(sys_greatbelt_scl_create_group(lchip, SYS_SCL_GROUP_ID_INNER_DEFT_VLAN_MAPPING_EGS, &hash_group, 1));

    /* vlan class hash */
    CTC_ERROR_RETURN(sys_greatbelt_scl_create_group(lchip, SYS_SCL_GROUP_ID_INNER_HASH_VLAN_CLASS_MAC, &hash_group, 1));
    CTC_ERROR_RETURN(sys_greatbelt_scl_create_group(lchip, SYS_SCL_GROUP_ID_INNER_HASH_VLAN_CLASS_IPV4, &hash_group, 1));
    CTC_ERROR_RETURN(sys_greatbelt_scl_create_group(lchip, SYS_SCL_GROUP_ID_INNER_HASH_VLAN_CLASS_IPV6, &hash_group, 1));
    /* vlan class tcam */
    CTC_ERROR_RETURN(sys_greatbelt_scl_create_group(lchip, SYS_SCL_GROUP_ID_INNER_TCAM_VLAN_CLASS_MAC, &global_group, 1));
    CTC_ERROR_RETURN(sys_greatbelt_scl_create_group(lchip, SYS_SCL_GROUP_ID_INNER_TCAM_VLAN_CLASS_IPV4, &global_group, 1));
    CTC_ERROR_RETURN(sys_greatbelt_scl_create_group(lchip, SYS_SCL_GROUP_ID_INNER_TCAM_VLAN_CLASS_IPV6, &global_group, 1));
    /* vlan class default(use tcam) */
    class_group.un.port_class_id = SYS_SCL_LABEL_RESERVE_FOR_VLAN_CLASS;
    CTC_ERROR_RETURN(sys_greatbelt_scl_create_group(lchip, SYS_SCL_GROUP_ID_INNER_DEFT_VLAN_CLASS, &class_group, 1));

    /* customer id is the key, tcam for vpls/vpws */
    CTC_ERROR_RETURN(sys_greatbelt_scl_create_group(lchip, SYS_SCL_GROUP_ID_INNER_CUSTOMER_ID, &global_group, 1));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_scl_opf_init(uint8 lchip, uint8 opf_type)
{
    uint32              entry_num    = 0;
    uint32              start_offset = 0;
    sys_greatbelt_opf_t opf;

    sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));

    if (OPF_SCL_AD == opf_type)
    {
        /* This opf for DsUserId_t DsTunnelId_t DsVlanXlate_t */
        CTC_ERROR_RETURN(sys_greatbelt_ftm_get_table_entry_num(lchip, DsUserId_t, &entry_num));
    }
    else if (OPF_SCL_VLAN_ACTION == opf_type)
    {
        CTC_ERROR_RETURN(sys_greatbelt_ftm_get_table_entry_num(lchip, DsVlanActionProfile_t, &entry_num));
        start_offset = SYS_SCL_VLAN_ACTION_RESERVE_NUM;
        entry_num   -= SYS_SCL_VLAN_ACTION_RESERVE_NUM;
    }
    else if (OPF_SCL_ENTRY_ID == opf_type)
    {
        start_offset = 0;
        entry_num    = SYS_SCL_MAX_INNER_ENTRY_ID - SYS_SCL_MIN_INNER_ENTRY_ID + 1;
    }

    if (entry_num)
    {
        CTC_ERROR_RETURN(sys_greatbelt_opf_init(lchip, opf_type, 1));
            opf.pool_index = 0;
            opf.pool_type  = opf_type;
            CTC_ERROR_RETURN(sys_greatbelt_opf_init_offset(lchip, &opf, start_offset, entry_num));
    }

    return CTC_E_NONE;
}


STATIC int32
_sys_greatbelt_scl_vlan_edit_static_add(uint8 lchip, sys_scl_sw_vlan_edit_t* p_vlan_edit, uint16* p_prof_idx)
{
    sys_scl_sw_vlan_edit_t   * p_vlan_edit_new = NULL;
    uint32                   cmd               = 0;
    int32                    ret               = 0;
    ds_vlan_action_profile_t ds_edit;

    SYS_SCL_DBG_FUNC();

    sal_memset(&ds_edit, 0, sizeof(ds_edit));

    #if 0
    {
        char* tag_op[CTC_VLAN_TAG_OP_MAX];
        char* tag_sl[CTC_VLAN_TAG_SL_MAX];
        tag_op[CTC_VLAN_TAG_OP_NONE]       = "None";
        tag_op[CTC_VLAN_TAG_OP_REP_OR_ADD] = "Replace OR Add";
        tag_op[CTC_VLAN_TAG_OP_ADD]        = "Add";
        tag_op[CTC_VLAN_TAG_OP_DEL]        = "Del";
        tag_op[CTC_VLAN_TAG_OP_REP]        = "Replace";

        tag_sl[CTC_VLAN_TAG_SL_AS_PARSE]    = "As parse   ";
        tag_sl[CTC_VLAN_TAG_SL_ALTERNATIVE] = "Alternative";
        tag_sl[CTC_VLAN_TAG_SL_NEW]         = "New        ";


        SYS_SCL_DBG_DUMP("  --------------------- profile_id:  %d\n", *p_prof_idx);

        SYS_SCL_DBG_DUMP("  stag-op:  %s\n", tag_op[p_vlan_edit->stag_op]);
        SYS_SCL_DBG_DUMP("  svid-sl:  %s\n", tag_sl[p_vlan_edit->svid_sl]);
        SYS_SCL_DBG_DUMP("  scos-sl:  %s\n", tag_sl[p_vlan_edit->scos_sl]);
        SYS_SCL_DBG_DUMP("  scfi-sl:  %s\n", tag_sl[p_vlan_edit->scfi_sl]);
        SYS_SCL_DBG_DUMP("  ctag-op:  %s\n", tag_op[p_vlan_edit->ctag_op]);
        SYS_SCL_DBG_DUMP("  cvid-sl:  %s\n", tag_sl[p_vlan_edit->cvid_sl]);
        SYS_SCL_DBG_DUMP("  ccos-sl:  %s\n", tag_sl[p_vlan_edit->ccos_sl]);
        SYS_SCL_DBG_DUMP("  ccfi-sl:  %s\n", tag_sl[p_vlan_edit->ccfi_sl]);
    }
    #endif

    MALLOC_ZERO(MEM_SCL_MODULE, p_vlan_edit_new, sizeof(sys_scl_sw_vlan_edit_t));
    if (NULL == p_vlan_edit_new)
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memcpy(p_vlan_edit_new, p_vlan_edit, sizeof(sys_scl_sw_vlan_edit_t));
    p_vlan_edit_new->lchip = lchip;
    _sys_greatbelt_scl_build_vlan_edit(lchip, &ds_edit, p_vlan_edit);
        p_vlan_edit_new->chip.profile_id = *p_prof_idx;
        if (ctc_spool_static_add(scl_master[lchip]->vlan_edit_spool, p_vlan_edit_new) < 0)
        {
            ret = CTC_E_NO_MEMORY;
            goto cleanup;
        }

        cmd = DRV_IOW(DsVlanActionProfile_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_vlan_edit_new->chip.profile_id, cmd, &ds_edit));
    *p_prof_idx = *p_prof_idx + 1;

    return CTC_E_NONE;

 cleanup:
    mem_free(p_vlan_edit_new);
    return ret;
}

STATIC int32
_sys_greatbelt_scl_stag_edit_template_build(uint8 lchip, sys_scl_sw_vlan_edit_t* p_vlan_edit, uint16* p_prof_idx)
{
    uint8 stag_op = 0;

    SYS_SCL_DBG_FUNC();

    for (stag_op = CTC_VLAN_TAG_OP_NONE; stag_op < CTC_VLAN_TAG_OP_MAX; stag_op++)
    {
        p_vlan_edit->stag_op = stag_op;

        switch (stag_op)
        {
        case CTC_VLAN_TAG_OP_REP:         /* template has no replace */
        case CTC_VLAN_TAG_OP_VALID:       /* template has no valid */
            continue;
        case CTC_VLAN_TAG_OP_REP_OR_ADD:
        case CTC_VLAN_TAG_OP_ADD:
        {
            p_vlan_edit->svid_sl = CTC_VLAN_TAG_SL_NEW;
            p_vlan_edit->scos_sl = CTC_VLAN_TAG_SL_NEW;
            CTC_ERROR_RETURN(_sys_greatbelt_scl_vlan_edit_static_add(lchip, p_vlan_edit, p_prof_idx));

            p_vlan_edit->svid_sl = CTC_VLAN_TAG_SL_NEW;
            p_vlan_edit->scos_sl = CTC_VLAN_TAG_SL_AS_PARSE;
            CTC_ERROR_RETURN(_sys_greatbelt_scl_vlan_edit_static_add(lchip, p_vlan_edit, p_prof_idx));

            p_vlan_edit->svid_sl = CTC_VLAN_TAG_SL_AS_PARSE;
            p_vlan_edit->scos_sl = CTC_VLAN_TAG_SL_AS_PARSE;
            CTC_ERROR_RETURN(_sys_greatbelt_scl_vlan_edit_static_add(lchip, p_vlan_edit, p_prof_idx));

            p_vlan_edit->svid_sl = CTC_VLAN_TAG_SL_AS_PARSE;
            p_vlan_edit->scos_sl = CTC_VLAN_TAG_SL_NEW;
            CTC_ERROR_RETURN(_sys_greatbelt_scl_vlan_edit_static_add(lchip, p_vlan_edit, p_prof_idx));
        }
        break;
        case CTC_VLAN_TAG_OP_NONE:
        case CTC_VLAN_TAG_OP_DEL:
            p_vlan_edit->svid_sl = CTC_VLAN_TAG_SL_AS_PARSE;
            p_vlan_edit->scos_sl = CTC_VLAN_TAG_SL_AS_PARSE;
            CTC_ERROR_RETURN(_sys_greatbelt_scl_vlan_edit_static_add(lchip, p_vlan_edit, p_prof_idx));
            break;
        }
    }

    return CTC_E_NONE;
}


STATIC int32
_sys_greatbelt_rsv_vlan_edit(uint8 lchip)
{
    uint16                 prof_idx = 0;
    uint8                  ctag_op  = 0;

    sys_scl_sw_vlan_edit_t vlan_edit;
    sal_memset(&vlan_edit, 0, sizeof(vlan_edit));

    vlan_edit.vlan_domain = CTC_SCL_VLAN_DOMAIN_SVLAN;
    for (ctag_op = CTC_VLAN_TAG_OP_NONE; ctag_op < CTC_VLAN_TAG_OP_MAX; ctag_op++)
    {
        vlan_edit.ctag_op = ctag_op;

        switch (ctag_op)
        {
        case CTC_VLAN_TAG_OP_ADD:           /* template has no append ctag */
        case CTC_VLAN_TAG_OP_REP:           /* template has no replace */
        case CTC_VLAN_TAG_OP_VALID:         /* template has no valid */
            continue;
        case CTC_VLAN_TAG_OP_REP_OR_ADD:
        {
            vlan_edit.cvid_sl = CTC_VLAN_TAG_SL_NEW;
            vlan_edit.ccos_sl = CTC_VLAN_TAG_SL_NEW;
            CTC_ERROR_RETURN(_sys_greatbelt_scl_stag_edit_template_build(lchip, &vlan_edit, &prof_idx));

            vlan_edit.cvid_sl = CTC_VLAN_TAG_SL_NEW;
            vlan_edit.ccos_sl = CTC_VLAN_TAG_SL_AS_PARSE;
            CTC_ERROR_RETURN(_sys_greatbelt_scl_stag_edit_template_build(lchip, &vlan_edit, &prof_idx));

            vlan_edit.cvid_sl = CTC_VLAN_TAG_SL_AS_PARSE;
            vlan_edit.ccos_sl = CTC_VLAN_TAG_SL_AS_PARSE;
            CTC_ERROR_RETURN(_sys_greatbelt_scl_stag_edit_template_build(lchip, &vlan_edit, &prof_idx));

            vlan_edit.cvid_sl = CTC_VLAN_TAG_SL_AS_PARSE;
            vlan_edit.ccos_sl = CTC_VLAN_TAG_SL_NEW;
            CTC_ERROR_RETURN(_sys_greatbelt_scl_stag_edit_template_build(lchip, &vlan_edit, &prof_idx));
        }
        break;
        case CTC_VLAN_TAG_OP_NONE:
        case CTC_VLAN_TAG_OP_DEL:
            vlan_edit.cvid_sl = CTC_VLAN_TAG_SL_AS_PARSE;
            vlan_edit.ccos_sl = CTC_VLAN_TAG_SL_AS_PARSE;
            CTC_ERROR_RETURN(_sys_greatbelt_scl_stag_edit_template_build(lchip, &vlan_edit, &prof_idx));
            break;
        }
    }

    /*for swap*/
    vlan_edit.ctag_op = CTC_VLAN_TAG_OP_REP_OR_ADD;
    vlan_edit.stag_op = CTC_VLAN_TAG_OP_REP_OR_ADD;

    vlan_edit.cvid_sl = CTC_VLAN_TAG_SL_ALTERNATIVE;
    vlan_edit.ccos_sl = CTC_VLAN_TAG_SL_ALTERNATIVE;
    vlan_edit.svid_sl = CTC_VLAN_TAG_SL_ALTERNATIVE;
    vlan_edit.scos_sl = CTC_VLAN_TAG_SL_ALTERNATIVE;
    CTC_ERROR_RETURN(_sys_greatbelt_scl_vlan_edit_static_add(lchip, &vlan_edit, &prof_idx));

    vlan_edit.cvid_sl = CTC_VLAN_TAG_SL_ALTERNATIVE;
    vlan_edit.ccos_sl = CTC_VLAN_TAG_SL_AS_PARSE;
    vlan_edit.svid_sl = CTC_VLAN_TAG_SL_ALTERNATIVE;
    vlan_edit.scos_sl = CTC_VLAN_TAG_SL_AS_PARSE;
    CTC_ERROR_RETURN(_sys_greatbelt_scl_vlan_edit_static_add(lchip, &vlan_edit, &prof_idx));

    vlan_edit.cvid_sl = CTC_VLAN_TAG_SL_AS_PARSE;
    vlan_edit.ccos_sl = CTC_VLAN_TAG_SL_ALTERNATIVE;
    vlan_edit.svid_sl = CTC_VLAN_TAG_SL_AS_PARSE;
    vlan_edit.scos_sl = CTC_VLAN_TAG_SL_ALTERNATIVE;
    CTC_ERROR_RETURN(_sys_greatbelt_scl_vlan_edit_static_add(lchip, &vlan_edit, &prof_idx));

    return CTC_E_NONE;
}

/* hash default entry : do nothing.
 *  64 igs_default_entry + 64 egs_default_entry
 *  called only once  --init().
 ****/
STATIC int32
_sys_greatbelt_scl_sys_default_entry_add(uint8 lchip)
{
    ds_user_id_t    ids;     /* ingress scl ds */
    ds_vlan_xlate_t eds;     /*  egress scl ds */
    ds_tunnel_id_t  tds;     /* tunnel ds */

    uint32          cmd      = 0;
    uint16          index;

    uint32          i_num = 0; /*ingress default entry num*/
    uint32          e_num = 0; /*egress  default entry num*/
    uint32          t_num = 0; /*tunnel  default entry num*/

    SYS_SCL_DBG_FUNC();

    SCL_GET_TABLE_ENTYR_NUM(DsUserIdIngressDefault_t, &i_num);
    SCL_GET_TABLE_ENTYR_NUM(DsUserIdEgressDefault_t, &e_num);
    SCL_GET_TABLE_ENTYR_NUM(DsTunnelIdDefault_t, &t_num);

    sal_memset(&ids, 0, sizeof(ds_user_id_t));
    sal_memset(&eds, 0, sizeof(ds_vlan_xlate_t));
    sal_memset(&tds, 0, sizeof(ds_tunnel_id_t));


    /* write igs default entry */
        for (index = 0; index < i_num; index++)
        {
            /*do nothing*/
            ids.binding_data_high = 0x7FF;

            cmd = DRV_IOW(DsUserIdIngressDefault_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ids));
        }

        for (index = 0; index < e_num; index++)
        {
            /*do nothing */
            cmd = DRV_IOW(DsUserIdEgressDefault_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &eds));
        }

        for (index = 0; index < t_num; index++)
        {
            /*do nothing */
            cmd = DRV_IOW(DsTunnelIdDefault_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &tds));
        }

    return CTC_E_NONE;
}


STATIC int32
_sys_greatbelt_scl_group_compare(sys_scl_sw_group_t* p0, sys_scl_sw_group_t* p1)
{
    return((p0->group_id) - (p1->group_id));
}


STATIC bool
_sys_greatbelt_scl_cmp_port_dir(uint8 lchip, sys_scl_sw_key_t* pk, uint16 user_port, uint8 user_dir)
{
    uint16 gport = 0;
    uint8  dir   = 0;
    switch (pk->type)
    {
    case SYS_SCL_KEY_HASH_PORT:
        gport = pk->u0.hash.u0.port.gport;
        dir   = pk->u0.hash.u0.port.dir;
        break;
    case SYS_SCL_KEY_HASH_PORT_CVLAN:
    case SYS_SCL_KEY_HASH_PORT_SVLAN:
        gport = pk->u0.hash.u0.vlan.gport;
        dir   = pk->u0.hash.u0.vlan.dir;
        break;
    case SYS_SCL_KEY_HASH_PORT_2VLAN:
    case SYS_SCL_KEY_HASH_PORT_CVLAN_COS:
    case SYS_SCL_KEY_HASH_PORT_SVLAN_COS:
        gport = pk->u0.hash.u0.vtag.gport;
        dir   = pk->u0.hash.u0.vtag.dir;
        break;
    default:
        return FALSE;
    }

    if ((user_port == gport) && (dir == user_dir))
    {
        return TRUE;
    }

    return FALSE;
}

int32
sys_greatbelt_scl_remove_all_inner_hash_vlan(uint8 lchip, uint16 gport, uint8 dir)
{
    sys_scl_sw_group_t    * pg      = NULL;
    struct ctc_slistnode_s* pe      = NULL;
    struct ctc_slistnode_s* pe_next = NULL;
    sys_scl_sw_entry_t    * pe2     = NULL;
    uint32                eid       = 0;


    SYS_SCL_LOCK(lchip);
    if (CTC_INGRESS == dir)
    {
        _sys_greatbelt_scl_get_group_by_gid(lchip, SYS_SCL_GROUP_ID_INNER_HASH_VLAN_MAPPING_IGS, &pg);
    }
    else
    {
        _sys_greatbelt_scl_get_group_by_gid(lchip, SYS_SCL_GROUP_ID_INNER_HASH_VLAN_MAPPING_EGS, &pg);
    }

    SYS_SCL_CHECK_GROUP_UNEXIST(pg);

    CTC_SLIST_LOOP_DEL(pg->entry_list, pe, pe_next)
    {
        pe2 = (sys_scl_sw_entry_t *) pe;

        eid = pe2->fpae.entry_id;
        if (_sys_greatbelt_scl_cmp_port_dir(lchip, &pe2->key, gport, dir))
        {
            SYS_SCL_UNLOCK(lchip);
            sys_greatbelt_scl_uninstall_entry(lchip, eid, 1);
            sys_greatbelt_scl_remove_entry(lchip, eid, 1);
            SYS_SCL_LOCK(lchip);
        }
    }
    SYS_SCL_UNLOCK(lchip);

    return CTC_E_NONE;
}


#define DEF_EID(lchip, dir, lport)    scl_master[lchip]->def_eid[dir][lport]

int32
sys_greatbelt_scl_set_default_action(uint8 lchip, uint16 gport, sys_scl_action_t* action)
{
    uint8 gchip = 0;
    uint8 lport = 0;
    uint8 dir   = 0;

    SYS_SCL_INIT_CHECK(lchip) ;
    SYS_SCL_DBG_FUNC();
    CTC_PTR_VALID_CHECK(action);

    gchip = CTC_MAP_GPORT_TO_GCHIP(gport);
    lport = CTC_MAP_GPORT_TO_LPORT(gport);
    if (!sys_greatbelt_chip_is_local(lchip, gchip))
    {
        return CTC_E_NONE;
    }
    if (lport >= SYS_SCL_DEFAULT_ENTRY_PORT_NUM)
    {
        return CTC_E_INVALID_PARAM;
    }

    if (SYS_SCL_ACTION_INGRESS == action->type)
    {
        dir = CTC_INGRESS;
    }
    else if (SYS_SCL_ACTION_EGRESS == action->type)
    {
        dir = CTC_EGRESS;
    }
    else
    {
        return CTC_E_INVALID_PARAM;
    }


    CTC_ERROR_RETURN(sys_greatbelt_scl_update_action(lchip, DEF_EID(lchip, dir, lport), action, 1));

    return CTC_E_NONE;
}

int32
sys_greatbelt_scl_get_default_action(uint8 lchip, uint16 gport, sys_scl_action_t* action)
{
    uint8 gchip = 0;
    uint8 lport = 0;
    uint8 dir   = 0;

    SYS_SCL_INIT_CHECK(lchip) ;
    SYS_SCL_DBG_FUNC();
    CTC_PTR_VALID_CHECK(action);

    gchip = CTC_MAP_GPORT_TO_GCHIP(gport);
    lport = CTC_MAP_GPORT_TO_LPORT(gport);
    if (!sys_greatbelt_chip_is_local(lchip, gchip))
    {
        return CTC_E_NONE;
    }
    if (lport >= SYS_SCL_DEFAULT_ENTRY_PORT_NUM)
    {
        return CTC_E_INVALID_PARAM;
    }

    if (SYS_SCL_ACTION_INGRESS == action->type)
    {
        dir = CTC_INGRESS;
    }
    else if (SYS_SCL_ACTION_EGRESS == action->type)
    {
        dir = CTC_EGRESS;
    }
    else
    {
        return CTC_E_INVALID_PARAM;
    }


    CTC_ERROR_RETURN(sys_greatbelt_scl_get_action(lchip, DEF_EID(lchip, dir, lport), action, 1));

    return CTC_E_NONE;
}





STATIC int32
_sys_greatbelt_scl_init_default_entry(uint8 lchip)
{
    sys_scl_entry_t scl;
    uint8           lport;
    uint8           gchip    = 0;
    uint8           dir      = 0;
    uint8           key_type = 0;
    uint8           act_type = 0;
    uint32          gid      = 0;

    SYS_SCL_INIT_CHECK(lchip) ;

    sal_memset(&scl, 0, sizeof(scl));
        sys_greatbelt_get_gchip_id(lchip, &gchip);
        for (dir = CTC_INGRESS; dir < CTC_BOTH_DIRECTION; dir++)
        {
            key_type = (CTC_INGRESS == dir) ? SYS_SCL_KEY_PORT_DEFAULT_INGRESS : SYS_SCL_KEY_PORT_DEFAULT_EGRESS;
            act_type = (CTC_INGRESS == dir) ? CTC_SCL_ACTION_INGRESS : CTC_SCL_ACTION_EGRESS;
            gid      = (CTC_INGRESS == dir) ? SYS_SCL_GROUP_ID_INNER_DEFT_VLAN_MAPPING_IGS : SYS_SCL_GROUP_ID_INNER_DEFT_VLAN_MAPPING_EGS;
            for (lport = 0; lport < SYS_SCL_DEFAULT_ENTRY_PORT_NUM; lport++)
            {
                scl.key.type                     = key_type;
                scl.key.u.port_default_key.gport = CTC_MAP_LPORT_TO_GPORT(gchip, lport);

                scl.action.type = act_type;

                /* do nothing */

                CTC_ERROR_RETURN(sys_greatbelt_scl_add_entry(lchip, gid, &scl, 1));
                scl_master[lchip]->def_eid[dir][lport] = scl.entry_id;
            }
        }
    CTC_ERROR_RETURN(sys_greatbelt_scl_install_group(lchip, SYS_SCL_GROUP_ID_INNER_DEFT_VLAN_MAPPING_IGS, NULL, 1));
    CTC_ERROR_RETURN(sys_greatbelt_scl_install_group(lchip, SYS_SCL_GROUP_ID_INNER_DEFT_VLAN_MAPPING_EGS, NULL, 1));

    return CTC_E_NONE;
}


int32
sys_greatbelt_scl_tcam_reinit(uint8 lchip, uint8 is_add)
{
    if (is_add)
    {
        CTC_ERROR_RETURN(_sys_greatbelt_scl_init_register(lchip, NULL));
    }
    else
    {

    }

    return CTC_E_NONE;
}
/*
 * init scl module
 */
int32
sys_greatbelt_scl_init(uint8 lchip, void* scl_global_cfg)
{
    uint8       s0    = 0;
    uint8       s1    = 0;
    uint8       idx1  = 0;

    uint32      size                        = 0;
    uint32      pb_sz[SYS_SCL_TCAM_RAW_NUM] = { 0 };
    uint32      hash_num                    = 0;
    uint32      ad_entry_size               = 0;
    uint32      vedit_entry_size            = 0;
    uint8       array_idx                   = 0;
    ctc_spool_t spool                       = { 0 };

    /* check init */
    if (scl_master[lchip])
    {
        return CTC_E_NONE;
    }

     /* CTC_PTR_VALID_CHECK(scl_global_cfg);*/

    /* init block_sz */
    CTC_ERROR_RETURN(sys_greatbelt_ftm_get_table_entry_num(lchip, DsUserIdVlanKey_t, &pb_sz[0]));
    CTC_ERROR_RETURN(sys_greatbelt_ftm_get_table_entry_num(lchip, DsUserIdMacKey_t, &pb_sz[1]));
    CTC_ERROR_RETURN(sys_greatbelt_ftm_get_table_entry_num(lchip, DsUserIdIpv4Key_t, &pb_sz[2]));
    CTC_ERROR_RETURN(sys_greatbelt_ftm_get_table_entry_num(lchip, DsUserIdIpv6Key_t, &pb_sz[3]));
    CTC_ERROR_RETURN(sys_greatbelt_ftm_get_table_entry_num(lchip, DsTunnelIdIpv4Key_t, &pb_sz[4]));
    CTC_ERROR_RETURN(sys_greatbelt_ftm_get_table_entry_num(lchip, DsTunnelIdIpv6Key_t, &pb_sz[5]));

    CTC_ERROR_RETURN(sys_greatbelt_ftm_get_table_entry_num(lchip, DsUserIdPortHashKey_t, &hash_num));

    if (0 == (pb_sz[0] + pb_sz[1] + pb_sz[2] + pb_sz[3] + pb_sz[4] + pb_sz[5] + hash_num))
    {   /* no resources */
        return CTC_E_NONE;
    }

    /* malloc master */
    MALLOC_ZERO(MEM_SCL_MODULE, scl_master[lchip], sizeof(sys_scl_master_t));
    if (NULL == scl_master[lchip])
    {
        return CTC_E_NO_MEMORY;
    }

#define SYS_SCL_GROUP_HASH_BLOCK_SIZE           32
#define SYS_SCL_ENTRY_HASH_BLOCK_SIZE           32
#define SYS_SCL_ENTRY_HASH_BLOCK_SIZE_BY_KEY    1024
    scl_master[lchip]->group = ctc_hash_create(1,
                                        SYS_SCL_GROUP_HASH_BLOCK_SIZE,
                                        (hash_key_fn) _sys_greatbelt_scl_hash_make_group,
                                        (hash_cmp_fn) _sys_greatbelt_scl_hash_compare_group);


    /* init asic_key_type */
    scl_master[lchip]->tcam_alloc_num = SYS_SCL_TCAM_ALLOC_NUM;


    scl_master[lchip]->alloc_id[SYS_SCL_KEY_TCAM_VLAN]        = 0;
    scl_master[lchip]->alloc_id[SYS_SCL_KEY_TCAM_MAC]         = 1;
    scl_master[lchip]->alloc_id[SYS_SCL_KEY_TCAM_IPV4]        = 2;
    scl_master[lchip]->alloc_id[SYS_SCL_KEY_TCAM_IPV6]        = 3;
    scl_master[lchip]->alloc_id[SYS_SCL_KEY_TCAM_TUNNEL_IPV4] = 4;
    scl_master[lchip]->alloc_id[SYS_SCL_KEY_TCAM_TUNNEL_IPV6] = 5;

    /* init max priority */
    scl_master[lchip]->max_entry_priority = 0xFFFFFFFF;


    /* init lchip num */

    /* init sort algor / hash table /share pool */
    {
        scl_master[lchip]->fpa = fpa_greatbelt_create(_sys_greatbelt_scl_get_block_by_pe_fpa,
                                     _sys_greatbelt_scl_entry_move_hw_fpa,
                                     sizeof(ctc_slistnode_t));
        /* init hash table :
         *    instead of using one linklist, scl use 32 linklist.
         *    hash caculatition is pretty fast, just label_id % hash_size
         */


        CTC_ERROR_RETURN(sys_greatbelt_ftm_get_table_entry_num(lchip, DsUserId_t, &ad_entry_size));
        CTC_ERROR_RETURN(sys_greatbelt_ftm_get_table_entry_num(lchip, DsVlanActionProfile_t, &vedit_entry_size));

        scl_master[lchip]->entry = ctc_hash_create(1,
                                            SYS_SCL_ENTRY_HASH_BLOCK_SIZE,
                                            (hash_key_fn) _sys_greatbelt_scl_hash_make_entry,
                                            (hash_cmp_fn) _sys_greatbelt_scl_hash_compare_entry);

        scl_master[lchip]->entry_by_key = ctc_hash_create(1,
                                                   SYS_SCL_ENTRY_HASH_BLOCK_SIZE,
                                                   (hash_key_fn) _sys_greatbelt_scl_hash_make_entry_by_key,
                                                   (hash_cmp_fn) _sys_greatbelt_scl_hash_compare_entry_by_key);

        sal_memset(&spool, 0, sizeof(ctc_spool_t));
        spool.lchip = lchip;
        spool.block_num = 1;
        spool.block_size = vedit_entry_size / VLAN_ACTION_SIZE_PER_BUCKET;
        spool.max_count = vedit_entry_size;
        spool.user_data_size = sizeof(sys_scl_sw_vlan_edit_t);
        spool.spool_key = (hash_key_fn)_sys_greatbelt_scl_hash_make_vlan_edit;
        spool.spool_cmp = (hash_cmp_fn)_sys_greatbelt_scl_hash_compare_vlan_edit;
        scl_master[lchip]->vlan_edit_spool = ctc_spool_create(&spool);
        for (array_idx = NORMAL_AD_SHARE_POOL; array_idx <= DEFAULT_AD_SHARE_POOL; array_idx++)
        {
                sal_memset(&spool, 0, sizeof(ctc_spool_t));
                spool.lchip = lchip;
                spool.block_num = 1;
                spool.block_size = SYS_SCL_ENTRY_HASH_BLOCK_SIZE_BY_KEY;
                spool.max_count = ad_entry_size;
                spool.user_data_size = sizeof(sys_scl_sw_action_t);
                spool.spool_key = (hash_key_fn)_sys_greatbelt_scl_hash_make_action;
                spool.spool_cmp = (hash_cmp_fn)_sys_greatbelt_scl_hash_compare_action;;
                scl_master[lchip]->ad_spool[array_idx] = ctc_spool_create(&spool);

                if (!scl_master[lchip]->ad_spool[array_idx])
                {
                    goto ERROR_FREE_MEM0;
                }
        }
        /* init each block */
        for (idx1 = 0; idx1 < scl_master[lchip]->tcam_alloc_num; idx1++)
        {
                scl_master[lchip]->block[idx1].fpab.lchip       = lchip;
                scl_master[lchip]->block[idx1].fpab.entry_count = (uint16) pb_sz[idx1];
                scl_master[lchip]->block[idx1].fpab.free_count  = (uint16) pb_sz[idx1];
                size                                            = sizeof(sys_scl_sw_entry_t*) * pb_sz[idx1];
                MALLOC_ZERO(MEM_SCL_MODULE, scl_master[lchip]->block[idx1].fpab.entries, size);
                if (NULL == scl_master[lchip]->block[idx1].fpab.entries && size)
                {
                    goto ERROR_FREE_MEM0;
                }
        }

        scl_master[lchip]->group_list[0] = ctc_list_create((ctc_list_cmp_cb_t) _sys_greatbelt_scl_group_compare, NULL);
        scl_master[lchip]->group_list[1] = ctc_list_create((ctc_list_cmp_cb_t) _sys_greatbelt_scl_group_compare, NULL);
        scl_master[lchip]->group_list[2] = ctc_list_create((ctc_list_cmp_cb_t) _sys_greatbelt_scl_group_compare, NULL);

        if (!(scl_master[lchip]->fpa &&
              scl_master[lchip]->entry &&
              scl_master[lchip]->entry_by_key &&
              scl_master[lchip]->vlan_edit_spool))
        {
            goto ERROR_FREE_MEM0;
        }
    }


    /* init entry size */
    {
        s0 = sizeof(sys_scl_sw_entry_t) - sizeof(sys_scl_sw_key_union_t);
        s1 = sizeof(sys_scl_sw_hash_key_t) - sizeof(sys_scl_sw_hash_key_union_t);


        scl_master[lchip]->entry_size[SYS_SCL_KEY_TCAM_VLAN           ] = s0 + sizeof(sys_scl_sw_tcam_key_vlan_t);
        scl_master[lchip]->entry_size[SYS_SCL_KEY_TCAM_MAC            ] = s0 + sizeof(sys_scl_sw_tcam_key_mac_t);
        scl_master[lchip]->entry_size[SYS_SCL_KEY_TCAM_IPV4           ] = s0 + sizeof(sys_scl_sw_tcam_key_ipv4_t);
        scl_master[lchip]->entry_size[SYS_SCL_KEY_TCAM_IPV6           ] = s0 + sizeof(sys_scl_sw_tcam_key_ipv6_t);
        scl_master[lchip]->entry_size[SYS_SCL_KEY_TCAM_TUNNEL_IPV4    ] = s0 + sizeof(sys_scl_tcam_tunnel_ipv4_key_t);
        scl_master[lchip]->entry_size[SYS_SCL_KEY_TCAM_TUNNEL_IPV6    ] = s0 + sizeof(sys_scl_tcam_tunnel_ipv6_key_t);
        scl_master[lchip]->entry_size[SYS_SCL_KEY_HASH_PORT           ] = s0 + s1 + sizeof(sys_scl_sw_hash_key_port_t);
        scl_master[lchip]->entry_size[SYS_SCL_KEY_HASH_PORT_CVLAN     ] = s0 + s1 + sizeof(sys_scl_sw_hash_key_vlan_t);
        scl_master[lchip]->entry_size[SYS_SCL_KEY_HASH_PORT_SVLAN     ] = s0 + s1 + sizeof(sys_scl_sw_hash_key_vlan_t);
        scl_master[lchip]->entry_size[SYS_SCL_KEY_HASH_PORT_2VLAN     ] = s0 + s1 + sizeof(sys_scl_sw_hash_key_vtag_t);
        scl_master[lchip]->entry_size[SYS_SCL_KEY_HASH_PORT_CVLAN_COS ] = s0 + s1 + sizeof(sys_scl_sw_hash_key_vtag_t);
        scl_master[lchip]->entry_size[SYS_SCL_KEY_HASH_PORT_SVLAN_COS ] = s0 + s1 + sizeof(sys_scl_sw_hash_key_vtag_t);
        scl_master[lchip]->entry_size[SYS_SCL_KEY_HASH_MAC            ] = s0 + s1 + sizeof(sys_scl_sw_hash_key_mac_t);
        scl_master[lchip]->entry_size[SYS_SCL_KEY_HASH_PORT_MAC       ] = s0 + s1 + sizeof(sys_scl_sw_hash_key_mac_t);
        scl_master[lchip]->entry_size[SYS_SCL_KEY_HASH_IPV4           ] = s0 + s1 + sizeof(sys_scl_sw_hash_key_ipv4_t);
        scl_master[lchip]->entry_size[SYS_SCL_KEY_HASH_PORT_IPV4      ] = s0 + s1 + sizeof(sys_scl_sw_hash_key_ipv4_t);
        scl_master[lchip]->entry_size[SYS_SCL_KEY_HASH_IPV6           ] = s0 + s1 + sizeof(sys_scl_sw_hash_key_ipv6_t);
        scl_master[lchip]->entry_size[SYS_SCL_KEY_HASH_L2             ] = s0 + s1 + sizeof(sys_scl_sw_hash_key_l2_t);
        scl_master[lchip]->entry_size[SYS_SCL_KEY_HASH_TUNNEL_IPV4    ] = s0 + s1 + sizeof(sys_scl_hash_tunnel_ipv4_key_t);
        scl_master[lchip]->entry_size[SYS_SCL_KEY_HASH_TUNNEL_IPV4_GRE] = s0 + s1 + sizeof(sys_scl_hash_tunnel_ipv4_gre_key_t);
        scl_master[lchip]->entry_size[SYS_SCL_KEY_HASH_TUNNEL_IPV4_RPF] = s0 + s1 + sizeof(sys_scl_hash_tunnel_ipv4_rpf_key_t);
        scl_master[lchip]->entry_size[SYS_SCL_KEY_PORT_DEFAULT_INGRESS] = s0 + s1 + sizeof(sys_scl_sw_hash_key_port_t);
        scl_master[lchip]->entry_size[SYS_SCL_KEY_PORT_DEFAULT_EGRESS ] = s0 + s1 + sizeof(sys_scl_sw_hash_key_port_t);

        scl_master[lchip]->key_size[SYS_SCL_KEY_TCAM_VLAN           ] = sizeof(sys_scl_sw_tcam_key_vlan_t);
        scl_master[lchip]->key_size[SYS_SCL_KEY_TCAM_MAC            ] = sizeof(sys_scl_sw_tcam_key_mac_t);
        scl_master[lchip]->key_size[SYS_SCL_KEY_TCAM_IPV4           ] = sizeof(sys_scl_sw_tcam_key_ipv4_t);
        scl_master[lchip]->key_size[SYS_SCL_KEY_TCAM_IPV6           ] = sizeof(sys_scl_sw_tcam_key_ipv6_t);
        scl_master[lchip]->key_size[SYS_SCL_KEY_TCAM_TUNNEL_IPV4    ] = sizeof(sys_scl_tcam_tunnel_ipv4_key_t);
        scl_master[lchip]->key_size[SYS_SCL_KEY_TCAM_TUNNEL_IPV6    ] = sizeof(sys_scl_tcam_tunnel_ipv6_key_t);
        scl_master[lchip]->key_size[SYS_SCL_KEY_HASH_PORT           ] = sizeof(sys_scl_sw_hash_key_port_t);
        scl_master[lchip]->key_size[SYS_SCL_KEY_HASH_PORT_CVLAN     ] = sizeof(sys_scl_sw_hash_key_vlan_t);
        scl_master[lchip]->key_size[SYS_SCL_KEY_HASH_PORT_SVLAN     ] = sizeof(sys_scl_sw_hash_key_vlan_t);
        scl_master[lchip]->key_size[SYS_SCL_KEY_HASH_PORT_2VLAN     ] = sizeof(sys_scl_sw_hash_key_vtag_t);
        scl_master[lchip]->key_size[SYS_SCL_KEY_HASH_PORT_CVLAN_COS ] = sizeof(sys_scl_sw_hash_key_vtag_t);
        scl_master[lchip]->key_size[SYS_SCL_KEY_HASH_PORT_SVLAN_COS ] = sizeof(sys_scl_sw_hash_key_vtag_t);
        scl_master[lchip]->key_size[SYS_SCL_KEY_HASH_MAC            ] = sizeof(sys_scl_sw_hash_key_mac_t);
        scl_master[lchip]->key_size[SYS_SCL_KEY_HASH_PORT_MAC       ] = sizeof(sys_scl_sw_hash_key_mac_t);
        scl_master[lchip]->key_size[SYS_SCL_KEY_HASH_IPV4           ] = sizeof(sys_scl_sw_hash_key_ipv4_t);
        scl_master[lchip]->key_size[SYS_SCL_KEY_HASH_PORT_IPV4      ] = sizeof(sys_scl_sw_hash_key_ipv4_t);
        scl_master[lchip]->key_size[SYS_SCL_KEY_HASH_IPV6           ] = sizeof(sys_scl_sw_hash_key_ipv6_t);
        scl_master[lchip]->key_size[SYS_SCL_KEY_HASH_L2             ] = sizeof(sys_scl_sw_hash_key_l2_t);
        scl_master[lchip]->key_size[SYS_SCL_KEY_HASH_TUNNEL_IPV4    ] = sizeof(sys_scl_hash_tunnel_ipv4_key_t);
        scl_master[lchip]->key_size[SYS_SCL_KEY_HASH_TUNNEL_IPV4_GRE] = sizeof(sys_scl_hash_tunnel_ipv4_gre_key_t);
        scl_master[lchip]->key_size[SYS_SCL_KEY_HASH_TUNNEL_IPV4_RPF] = sizeof(sys_scl_hash_tunnel_ipv4_rpf_key_t);
        scl_master[lchip]->key_size[SYS_SCL_KEY_PORT_DEFAULT_INGRESS] = sizeof(sys_scl_sw_hash_key_port_t);
        scl_master[lchip]->key_size[SYS_SCL_KEY_PORT_DEFAULT_EGRESS ] = sizeof(sys_scl_sw_hash_key_port_t);

        scl_master[lchip]->hash_igs_action_size =
            sizeof(sys_scl_sw_igs_action_t) - sizeof(sys_scl_sw_igs_action_chip_t) - sizeof(sys_scl_sw_vlan_edit_t*);

        scl_master[lchip]->hash_egs_action_size =
            sizeof(sys_scl_sw_egs_action_t) - sizeof(sys_scl_sw_egs_action_chip_t);

        scl_master[lchip]->hash_tunnel_action_size = sizeof(sys_scl_tunnel_action_t);

        scl_master[lchip]->vlan_edit_size =4;
    }
    CTC_ERROR_RETURN(_sys_greatbelt_scl_init_register(lchip, NULL));

    SYS_SCL_CREATE_LOCK(lchip);
    CTC_ERROR_RETURN(_sys_greatbelt_scl_create_rsv_group(lchip));

    CTC_ERROR_RETURN(_sys_greatbelt_scl_opf_init(lchip, OPF_SCL_AD));
    CTC_ERROR_RETURN(_sys_greatbelt_scl_opf_init(lchip, OPF_SCL_VLAN_ACTION));
    CTC_ERROR_RETURN(_sys_greatbelt_scl_opf_init(lchip, OPF_SCL_ENTRY_ID));

    CTC_ERROR_RETURN(_sys_greatbelt_rsv_vlan_edit(lchip));

    CTC_ERROR_RETURN(_sys_greatbelt_scl_sys_default_entry_add(lchip));
    CTC_ERROR_RETURN(_sys_greatbelt_scl_init_default_entry(lchip));
    sys_greatbelt_ftm_tcam_cb_register(lchip, SYS_FTM_TCAM_KEY_SCL, sys_greatbelt_scl_tcam_reinit);
    return CTC_E_NONE;


 ERROR_FREE_MEM0:

    return CTC_E_SCL_INIT;
}

STATIC int32
_sys_greatbelt_scl_free_node_data(void* node_data, void* user_data)
{
    uint32 free_entry_hash = 0;
    sys_scl_sw_entry_t* pe = NULL;

    if (user_data)
    {
        free_entry_hash = *(uint32*)user_data;
        if (1 == free_entry_hash)
        {
            pe = (sys_scl_sw_entry_t*)node_data;
            if (SCL_ENTRY_IS_TCAM(pe->key.type) || SCL_ENTRY_IS_SMALL_TCAM(pe->key.conv_type))
            {
                mem_free(pe->action);
            }
        }
    }
    mem_free(node_data);

    return CTC_E_NONE;
}

int32
sys_greatbelt_scl_deinit(uint8 lchip)
{
    uint8 array_idx = 0;
    uint8 idx = 0;
    ctc_listnode_t     * next_node            = NULL;
    ctc_listnode_t     * node                 = NULL;
    sys_scl_sw_group_t * pg                   = NULL;
    uint32 free_entry_hash = 1;

    LCHIP_CHECK(lchip);
    if (NULL == scl_master[lchip])
    {
        return CTC_E_NONE;
    }

    sys_greatbelt_opf_deinit(lchip, OPF_SCL_AD);
    sys_greatbelt_opf_deinit(lchip, OPF_SCL_VLAN_ACTION);
    sys_greatbelt_opf_deinit(lchip, OPF_SCL_ENTRY_ID);

    for (idx = 0; idx < SYS_SCL_TCAM_ALLOC_NUM; idx++)
    {
        mem_free(scl_master[lchip]->block[idx].fpab.entries);
    }

    /*free scl entry*/
    ctc_hash_traverse(scl_master[lchip]->entry, (hash_traversal_fn)_sys_greatbelt_scl_free_node_data, &free_entry_hash);
    ctc_hash_free(scl_master[lchip]->entry);
    ctc_hash_free(scl_master[lchip]->entry_by_key);

    /*free ad*/
    for (array_idx = NORMAL_AD_SHARE_POOL; array_idx <= DEFAULT_AD_SHARE_POOL; array_idx++)
    {
        ctc_spool_free(scl_master[lchip]->ad_spool[array_idx]);
    }

    /*free vlan edit spool*/
    ctc_spool_free(scl_master[lchip]->vlan_edit_spool);

    /*free scl sw group*/
    for (idx=0; idx< 3; idx++)
    {
        CTC_LIST_LOOP_DEL(scl_master[lchip]->group_list[idx], pg, node, next_node)
        {
            ctc_listnode_delete(scl_master[lchip]->group_list[idx], pg);
            mem_free(pg->entry_list);
            mem_free(pg);
        }
        ctc_list_free(scl_master[lchip]->group_list[idx]);
    }
    /*free scl group*/
    /*ctc_hash_traverse(p_scl_master[lchip]->group, (hash_traversal_fn)_sys_greatbelt_scl_free_node_data, NULL);*/
    ctc_hash_free(scl_master[lchip]->group);

    fpa_greatbelt_free(scl_master[lchip]->fpa);

    sal_mutex_destroy(scl_master[lchip]->mutex);
    mem_free(scl_master[lchip]);

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_scl_unmap_egs_action(uint8 lchip, ctc_scl_egs_action_t*       pc,
                                    sys_scl_sw_egs_action_t*    pea)
{
    uint32 flag;

    flag = pea->flag;

    if (CTC_FLAG_ISSET(flag, CTC_SCL_EGS_ACTION_FLAG_VLAN_EDIT))
    {
        pc->vlan_edit.svid_new = pea->svid;
        pc->vlan_edit.scos_new = pea->scos;
        pc->vlan_edit.scfi_new = pea->scfi;
        pc->vlan_edit.cvid_new = pea->cvid;
        pc->vlan_edit.ccos_new = pea->ccos;
        pc->vlan_edit.ccfi_new = pea->ccfi;

        pc->vlan_edit.stag_op = pea->stag_op;
        pc->vlan_edit.ctag_op = pea->ctag_op;
        pc->vlan_edit.svid_sl = pea->svid_sl;
        pc->vlan_edit.scos_sl = pea->scos_sl;
        pc->vlan_edit.scfi_sl = pea->scfi_sl;
        pc->vlan_edit.cvid_sl = pea->cvid_sl;
        pc->vlan_edit.ccos_sl = pea->ccos_sl;
        pc->vlan_edit.ccfi_sl = pea->ccfi_sl;
        pc->vlan_edit.tpid_index = pea->tpid_index;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_EGS_ACTION_FLAG_PRIORITY_AND_COLOR))
    {
        pc->color    = pea->color;
        pc->priority = pea->priority;
    }

    pc->flag = flag;

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_scl_unmap_igs_action(uint8 lchip, ctc_scl_igs_action_t* pc,
                                    sys_scl_sw_igs_action_t* pia)
{
    uint32 flag;

    flag     = pia->flag;

    if (CTC_FLAG_ISSET(flag, CTC_SCL_IGS_ACTION_FLAG_FID))
    {
        pc->fid = pia->fid;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_IGS_ACTION_FLAG_VRFID))
    {
        pc->vrf_id = pia->fid;
    }

    if (CTC_FLAG_ISSET(pia->flag, CTC_SCL_IGS_ACTION_FLAG_SERVICE_ID))
    {
        pc->service_id = pia->service_id;
    }

    if (CTC_FLAG_ISSET(pia->sub_flag, CTC_SCL_IGS_ACTION_SUB_FLAG_SERVICE_POLICER_EN))
    {
        pc->policer_id = pia->service_policer_id;
    }
    /* other 2 sub flag only need unmap flag. */

    if (CTC_FLAG_ISSET(flag, CTC_SCL_IGS_ACTION_FLAG_STATS))
    {
        pc->stats_id = pia->stats_id;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_IGS_ACTION_FLAG_LOGIC_PORT))
    {
        pc->logic_port.logic_port      = pia->bind_l.bds.logic_src_port;
        pc->logic_port.logic_port_type = pia->bind_l.bds.logic_port_type;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_IGS_ACTION_FLAG_REDIRECT))
    {
        pc->nh_id = pia->nh_id;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_IGS_ACTION_FLAG_PRIORITY_AND_COLOR))
    {
        pc->color    = pia->color;
        pc->priority = pia->priority;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_IGS_ACTION_FLAG_USER_VLANPTR))
    {
        pc->user_vlanptr = pia->user_vlan_ptr;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_IGS_ACTION_FLAG_BINDING_EN))
    {
        uint8 type = 0;
        if (pia->binding_mac_sa)
        {
            type = CTC_SCL_BIND_TYPE_MACSA;
        }
        else
        {
            switch (pia->bind_l.data & 0x3)
            {
            case 0:
                type           = CTC_SCL_BIND_TYPE_PORT;
                pc->bind.gport = SYS_SCL_GPORT14_TO_GPORT13((pia->bind_h.data) & 0x3FFF);
                break;
            case 1:
                type             = CTC_SCL_BIND_TYPE_VLAN;
                pc->bind.vlan_id = (pia->bind_h.data >> 4) & 0x3FF;
                break;
            case 2:
                type             = CTC_SCL_BIND_TYPE_IPV4SA;
                pc->bind.ipv4_sa = (((pia->bind_h.data & 0xF) << 28) | (pia->bind_l.data >> 4));
                break;
            case 3:
                pc->bind.vlan_id = (pia->bind_h.data >> 4) & 0x3FF;
                pc->bind.ipv4_sa = (((pia->bind_h.data & 0xF) << 28) | (pia->bind_l.data >> 4));
                type             = CTC_SCL_BIND_TYPE_IPV4SA_VLAN;
                break;
            }
        }

        pc->bind.type = type;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_IGS_ACTION_FLAG_APS))
    {
        pc->aps.aps_select_group_id  = pia->bind_h.bds.aps_select_group_id;
        pc->aps.is_working_path      = pia->bind_h.bds.aps_select_protecting_path;
        pc->aps.protected_vlan       = pia->user_vlan_ptr;
        pc->aps.protected_vlan_valid = (pia->user_vlan_ptr) ? 1 : 0;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_IGS_ACTION_FLAG_VPLS))
    {
        pc->vpls.learning_en  = pia->bind_l.bds.vsi_learning_disable;
        pc->vpls.mac_limit_en = pia->bind_l.bds.mac_security_vsi_discard;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_IGS_ACTION_FLAG_VLAN_EDIT))
    {
        pc->vlan_edit.svid_new = pia->svid;
        pc->vlan_edit.scos_new = pia->scos;
        pc->vlan_edit.scfi_new = pia->scfi;
        pc->vlan_edit.cvid_new = pia->cvid;
        pc->vlan_edit.ccos_new = pia->ccos;
        pc->vlan_edit.ccfi_new = pia->ccfi;

        if (pia->vlan_edit)
        {
            pc->vlan_edit.vlan_domain = pia->vlan_edit->vlan_domain;
            pc->vlan_edit.tpid_index = pia->vlan_edit->tpid_index;

            pc->vlan_edit.stag_op = pia->vlan_edit->stag_op;
            pc->vlan_edit.ctag_op = pia->vlan_edit->ctag_op;
            pc->vlan_edit.svid_sl = pia->vlan_edit->svid_sl;
            pc->vlan_edit.scos_sl = pia->vlan_edit->scos_sl;
            pc->vlan_edit.scfi_sl = pia->vlan_edit->scfi_sl;
            pc->vlan_edit.cvid_sl = pia->vlan_edit->cvid_sl;
            pc->vlan_edit.ccos_sl = pia->vlan_edit->ccos_sl;
            pc->vlan_edit.ccfi_sl = pia->vlan_edit->ccfi_sl;
        }
    }

    pc->flag = flag;
    pc->sub_flag = pia->sub_flag;

    return CTC_E_NONE;
}


int32
sys_greatbelt_scl_get_action(uint8 lchip, uint32 entry_id, sys_scl_action_t* action, uint8 inner)
{
    sys_scl_sw_entry_t* pe = NULL;

    SYS_SCL_INIT_CHECK(lchip) ;

    SYS_SCL_DBG_FUNC();
    SYS_SCL_DBG_PARAM("  %% PARAM: entry_id %u \n", entry_id);
    if (!inner)
    {
        SYS_SCL_CHECK_ENTRY_ID(entry_id);
    }

    SYS_SCL_LOCK(lchip);
    /* get sys entry */
    _sys_greatbelt_scl_get_nodes_by_eid(lchip, entry_id, &pe, NULL, NULL);
    if(!pe)
    {
        SYS_SCL_UNLOCK(lchip);
        return CTC_E_ENTRY_NOT_EXIST;
    }

    action->type = pe->action->type;
    action->action_flag = pe->action->action_flag;
    switch (pe->action->type)
    {
    case SYS_SCL_ACTION_INGRESS:
        CTC_ERROR_RETURN_SCL_UNLOCK(_sys_greatbelt_scl_unmap_igs_action(lchip, &action->u.igs_action, &pe->action->u0.ia));
        break;

    case SYS_SCL_ACTION_EGRESS:
        CTC_ERROR_RETURN_SCL_UNLOCK(_sys_greatbelt_scl_unmap_egs_action(lchip, &action->u.egs_action, &pe->action->u0.ea));
        break;

    case SYS_SCL_ACTION_TUNNEL: /* no requirement to support for now. */
        break;

    default:
        break;
    }
    SYS_SCL_UNLOCK(lchip);

    return CTC_E_NONE;
}





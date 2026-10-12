#ifndef _CTC_GREATBELT_DKIT_DRV_H_
#define _CTC_GREATBELT_DKIT_DRV_H_


#ifdef __cplusplus
extern "C" {
#endif


#include "sal.h"
#define DKIT_BUS_MAX_CLUSTER_NUM 4
#define DKIT_MAX_CHIP_NUM 2

#define DKIT_MODULE_GET_INFOPTR(mod_id)        (&gb_dkit_modules_list[mod_id])
#define DKIT_MODULE_GET_INFO(mod_id)           (gb_dkit_modules_list[mod_id])




#define DKIT_BUS_GET_CLUSTER_NUM(bus_id)           (gb_dkit_bus_list[bus_id].cluster_num)
#define DKIT_BUS_GET_INFOPTR(bus_id, cluster_id)   (&gb_dkit_bus_list[bus_id].cluster[cluster_id])
#define DKIT_BUS_GET_INFO(bus_id)                  (gb_dkit_bus_list[bus_id])
#define DKIT_BUS_GET_NAME(bus_id)                  (DKIT_BUS_GET_INFO(bus_id).bus_name)
#define DKIT_BUS_GET_MODULE(bus_id)                (DKIT_BUS_GET_INFO(bus_id).module_name)
#define DKIT_BUS_GET_NAME(bus_id)                  (DKIT_BUS_GET_INFO(bus_id).bus_name)
#define DKIT_BUS_GET_HELP(bus_id)                  (DKIT_BUS_GET_INFO(bus_id).help_info)
#define DKIT_BUS_GET_FIFO_ID(bus_id, cluster_id)   (gb_dkit_bus_list[bus_id].cluster[cluster_id].bus_fifo_id)

enum dkit_mod_id_e
{
    BufRetrv_m,                                                 /*         */
    BufStore_m,                                                 /*       1 */
    CpuMac_m,                                                   /*       2 */
    DSANDTCAM_m,                                                /*       3 */
    DsAging_m,                                                  /*       4 */
    DynamicDs_m,                                                /*       5 */
    ELoop_m,                                                    /*       6 */
    EpeAclQos_m,                                                /*       7 */
    EpeCla_m,                                                   /*       8 */
    EpeHdrAdj_m,                                                /*       9 */
    EpeHdrEdit_m,                                               /*      10 */
    EpeHdrProc_m,                                               /*      11 */
    EpeNextHop_m,                                               /*      12 */
    EpeOam_m,                                                   /*      13 */
    GlobalStats_m,                                              /*      14 */
    GreatbeltSup_m,                                             /*      15 */
    HashDs_m,                                                   /*      16 */
    I2CMaster_m,                                                /*      17 */
    IntLkIntf_m,                                                /*      18 */
    IpeFib_m,                                                   /*      19 */
    IpeForward_m,                                               /*      20 */
    IpeHdrAdj_m,                                                /*      21 */
    IpeIntfMap_m,                                               /*      22 */
    IpeLkupMgr_m,                                               /*      23 */
    IpePktProc_m,                                               /*      24 */
    MacLedDriver_m,                                             /*      25 */
    MacMux_m,                                                   /*      26 */
    Mdio_m,                                                     /*      27 */
    MetFifo_m,                                                  /*      28 */
    NetRx_m,                                                    /*      29 */
    NetTx_m,                                                    /*      30 */
    OamAutoGenPkt_m,                                            /*      31 */
    OamFwd_m,                                                   /*      32 */
    OamParser_m,                                                /*      33 */
    OamProc_m,                                                  /*      34 */
    OobFc_m,                                                    /*      35 */
    Parser_m,                                                   /*      36 */
    PbCtl_m,                                                    /*      37 */
    PciExpCore_m,                                               /*      38 */
    Policing_m,                                                 /*      39 */
    PtpEngine_m,                                                /*      40 */
    PtpFrc_m,                                                   /*      41 */
    QMgrDeq_m,                                                  /*      42 */
    QMgrEnq_m,                                                  /*      43 */
    QMgrQueEntry_m,                                             /*      44 */
    Qsgmii_m,                                                   /*      45 */
    QuadMac_m,                                                  /*      46 */
    QuadPcs_m,                                                  /*      47 */
    Sgmac_m,                                                    /*      48 */
    SharedDs_m,                                                 /*      49 */
    TcamCtlInt_m,                                               /*      50 */
    TcamDs_m,                                                   /*      51 */
    _DsFib_m,                                                   /*      52 */
    _DynamicDsMem_m,                                            /*      53 */
    _FwdMessage_m,                                              /*      54 */
    _HashDsMem_m,                                               /*      55 */
    _TcamDsMem_m,                                               /*      56 */
    Mod_IPE,                                                    /*      57 */
    Mod_EPE,                                                    /*      58 */
    Mod_BSR,                                                    /*      59 */
    Mod_SHARE,                                                  /*      60 */
    Mod_MISC,                                                   /*      61 */
    Mod_MAC,                                                    /*      62 */
    Mod_OAM,                                                    /*      63 */
    Mod_ALL,                                                    /*      64 */

    MaxModId_m,
};
typedef enum dkit_mod_id_e dkit_mod_id_t;

struct cluster_s
{
    char* help_info;
    tbls_id_t bus_fifo_id;
    uint32 max_index;

    uint16 entry_size;
    uint16 num_fields;
    fields_t* per_fields;
};
typedef struct cluster_s cluster_t;

struct dkit_bus_s
{
    char* module_name;
    char* bus_name;
    uint8 cluster_num;

    cluster_t cluster[DKIT_BUS_MAX_CLUSTER_NUM];
};
typedef struct dkit_bus_s dkit_bus_t;

/* module data stucture */
struct dkit_modules_s
{
    char* master_name;
    char* module_name;
    uint32 inst_num;
    uint32 sub_num;
    tbls_id_t* reg_list_id;
    uint32 id_num;
};
typedef struct dkit_modules_s dkit_modules_t;

struct dkit_debug_stats_s
{
    char* master_name;
    char* module_name;
    char* tbl_name;
};
typedef struct dkit_debug_stats_s dkit_debug_stats_t;

#if (HOST_IS_LE == 0)
struct bs_pb_hdr_bus_s
{
    uint32 rsv                       :27;
    uint32 packet_length_valid       :1;
    uint32 source_port15_14          :2;
    uint32 packet_offset_6_7         :2;

    uint32 packet_offset_0_5         :6;
    uint32 priority                  :6;
    uint32 packet_type               :3;
    uint32 source_cos                :3;
    uint32 src_queue_select          :1;
    uint32 header_hash2_0            :3;
    uint32 logic_port_type           :1;
    uint32 src_ctag_offset_type      :1;
    uint32 source_port_6_13          :8;

    uint32 source_port_0_5           :6;
    uint32 src_vlan_id               :12;
    uint32 color                     :2;
    uint32 bridge_operation          :1;
    uint32 next_hop_ptr_6_16         :11;

    uint32 next_hop_ptr_0_5          :6;
    uint32 critical_packet           :1;
    uint32 rxtx_fcl22_17             :6;
    uint32 flow                      :8;
    uint32 ttl                       :8;
    uint32 from_fabric               :1;
    uint32 bypass_ingress_edit       :1;
    uint32 source_port_extender      :1;

    uint32 loopback_discard          :1;
    uint32 source_port_isolate_id    :6;
    uint32 pbb_src_port_type         :3;
    uint32 svlan_tag_operation_valid :1;
    uint32 source_cfi                :1;
    uint32 non_crc                   :1;
    uint32 from_cpu_or_oam           :1;
    uint32 svlan_tpid_index          :2;
    uint32 stag_action               :2;
    uint32 src_svlan_id_valid        :1;
    uint32 src_cvlan_id_valid        :1;
    uint32 src_cvlan_id              :12;

    uint32 src_vlan_ptr              :16;
    uint32 fid                       :16;

    uint32 logic_src_port            :16;
    uint32 rxtx_fcl3                 :1;
    uint32 cut_through               :1;
    uint32 rxtx_fcl2_1               :2;
    uint32 mux_length_type           :2;
    uint32 oam_tunnel_en             :1;
    uint32 rxtx_fcl0                 :1;
    uint32 operation_type            :3;
    uint32 header_hash7_3            :5;

    uint32 ip_sa                     :32;
};
typedef struct bs_pb_hdr_bus_s bs_pb_hdr_bus_t;

struct ds_chan_info_ram_s
{
    uint32 rsv                      :8;
    uint32 state                    :1;
    uint32 entry_offset             :2;
    uint32 buffer_count             :6;
    uint32 head_buffer_ptr          :14;
    uint32 prev_buffer_ptr_13       :1;

    uint32 prev_buffer_ptr_0_12     :13;
    uint32 met_fifo_select          :2;
    uint32 resource_group_id        :9;
    uint32 tail_buffer_ptr_6_13     :8;

    uint32 tail_buffer_ptr_0_5      :6;
    uint32 mcast_rcd                :1;
    uint32 packet_length            :14;
    uint32 logic_port_type          :1;
    uint32 payload_offset           :8;
    uint32 fid_12_13                :2;

    uint32 fid_0_11                 :12;
    uint32 src_queue_select         :1;
    uint32 is_leaf                  :1;
    uint32 from_fabric              :1;
    uint32 length_adjust_type       :1;
    uint32 critical_packet          :1;
    uint32 dest_id_discard          :1;
    uint32 local_switching          :1;
    uint32 egress_exception         :1;
    uint32 exception_packet_type    :3;
    uint32 exception_sub_index      :6;
    uint32 ecn_en                   :1;
    uint32 rx_oam                   :1;
    uint32 next_hop_ext             :1;

    uint32 ecn_aware                :1;
    uint32 color                    :2;
    uint32 rx_oam_type              :4;
    uint32 l2_span_id               :2;
    uint32 l3_span_id               :2;
    uint32 acl_log_id               :8;
    uint32 header_hash              :8;
    uint32 old_dest_map_17_21       :5;

    uint32 old_dest_map_0_16        :17;
    uint32 priority                 :6;
    uint32 oam_dest_chip_id         :5;
    uint32 operation_type           :3;
    uint32 service_id_13            :1;

    uint32 service_id_0_12          :13;
    uint32 source_port              :16;
    uint32 flow_id_1_3              :3;

    uint32 flow_id_0                :1;
    uint32 c2c_check_disable        :1;
    uint32 exception_number         :3;
    uint32 exception_vector         :9;
    uint32 exception_from_sgmac     :1;
    uint32 next_hop_ptr             :17;
};
typedef struct ds_chan_info_ram_s ds_chan_info_ram_t;

struct epe_hdr_adj_brg_hdr_ds_s
{
    uint32 rsv                      :10;
    uint32 ecc                      :22;

    ms_packet_header_t              ms_packet_header;
};
typedef struct epe_hdr_adj_brg_hdr_ds_s  epe_hdr_adj_brg_hdr_ds_t;

#else

struct bs_pb_hdr_bus_s
{
    uint32 packet_offset_6_7         :2;
    uint32 source_port15_14          :2;
    uint32 packet_length_valid       :1;
    uint32 rsv                       :27;

    uint32 source_port_6_13          :8;
    uint32 src_ctag_offset_type      :1;
    uint32 logic_port_type           :1;
    uint32 header_hash2_0            :3;
    uint32 src_queue_select          :1;
    uint32 source_cos                :3;
    uint32 packet_type               :3;
    uint32 priority                  :6;
    uint32 packet_offset_0_5         :6;

    uint32 next_hop_ptr_6_16         :11;
    uint32 bridge_operation          :1;
    uint32 color                     :2;
    uint32 src_vlan_id               :12;
    uint32 source_port_0_5           :6;

    uint32 source_port_extender      :1;
    uint32 bypass_ingress_edit       :1;
    uint32 from_fabric               :1;
    uint32 ttl                       :8;
    uint32 flow                      :8;
    uint32 rxtx_fcl22_17             :6;
    uint32 critical_packet           :1;
    uint32 next_hop_ptr_0_5          :6;

    uint32 src_cvlan_id              :12;
    uint32 src_cvlan_id_valid        :1;
    uint32 src_svlan_id_valid        :1;
    uint32 stag_action               :2;
    uint32 svlan_tpid_index          :2;
    uint32 from_cpu_or_oam           :1;
    uint32 non_crc                   :1;
    uint32 source_cfi                :1;
    uint32 svlan_tag_operation_valid :1;
    uint32 pbb_src_port_type         :3;
    uint32 source_port_isolate_id    :6;
    uint32 loopback_discard          :1;

    uint32 fid                       :16;
    uint32 src_vlan_ptr              :16;

    uint32 header_hash7_3            :5;
    uint32 operation_type            :3;
    uint32 rxtx_fcl0                 :1;
    uint32 oam_tunnel_en             :1;
    uint32 mux_length_type           :2;
    uint32 rxtx_fcl2_1               :2;
    uint32 cut_through               :1;
    uint32 rxtx_fcl3                 :1;
    uint32 logic_src_port            :16;

    uint32 ip_sa                     :32;
};
typedef struct bs_pb_hdr_bus_s bs_pb_hdr_bus_t;

struct ds_chan_info_ram_s
{
    uint32 prev_buffer_ptr_13       :1;
    uint32 head_buffer_ptr          :14;
    uint32 buffer_count             :6;
    uint32 entry_offset             :2;
    uint32 state                    :1;
    uint32 rsv                      :8;

    uint32 tail_buffer_ptr_6_13     :8;
    uint32 resource_group_id        :9;
    uint32 met_fifo_select          :2;
    uint32 prev_buffer_ptr_0_12     :13;

    uint32 fid_12_13                :2;
    uint32 payload_offset           :8;
    uint32 logic_port_type          :1;
    uint32 packet_length            :14;
    uint32 mcast_rcd                :1;
    uint32 tail_buffer_ptr_0_5      :6;

    uint32 next_hop_ext             :1;
    uint32 rx_oam                   :1;
    uint32 ecn_en                   :1;
    uint32 exception_sub_index      :6;
    uint32 exception_packet_type    :3;
    uint32 egress_exception         :1;
    uint32 local_switching          :1;
    uint32 dest_id_discard          :1;
    uint32 critical_packet          :1;
    uint32 length_adjust_type       :1;
    uint32 from_fabric              :1;
    uint32 is_leaf                  :1;
    uint32 src_queue_select         :1;
    uint32 fid_0_11                 :12;

    uint32 old_dest_map_17_21       :5;
    uint32 header_hash              :8;
    uint32 acl_log_id               :8;
    uint32 l3_span_id               :2;
    uint32 l2_span_id               :2;
    uint32 rx_oam_type              :4;
    uint32 color                    :2;
    uint32 ecn_aware                :1;

    uint32 service_id_13            :1;
    uint32 operation_type           :3;
    uint32 oam_dest_chip_id         :5;
    uint32 priority                 :6;
    uint32 old_dest_map_0_16        :17;

    uint32 flow_id_1_3              :3;
    uint32 source_port              :16;
    uint32 service_id_0_12          :13;

    uint32 next_hop_ptr             :17;
    uint32 exception_from_sgmac     :1;
    uint32 exception_vector         :9;
    uint32 exception_number         :3;
    uint32 c2c_check_disable        :1;
    uint32 flow_id_0                :1;
};
typedef struct ds_chan_info_ram_s ds_chan_info_ram_t;

struct epe_hdr_adj_brg_hdr_ds_s
{
    uint32 ecc                      :22;
    uint32 rsv                      :10;

    ms_packet_header_t              ms_packet_header;
};
typedef struct epe_hdr_adj_brg_hdr_ds_s  epe_hdr_adj_brg_hdr_ds_t;

#endif

extern dkit_bus_t gb_dkit_bus_list[];
extern dkit_modules_t gb_dkit_modules_list[MaxModId_m];

extern int32
ctc_greatbelt_dkit_drv_register(void);

extern int32
ctc_greatbelt_dkit_drv_read_bus_field(char* field_name, uint32 bus_id, uint32 index, void* value);

extern uint32
ctc_greatbelt_dkit_drv_get_bus_list_num(void);

extern bool
ctc_greatbelt_dkit_drv_get_tbl_id_by_bus_name(char *str, uint32 *tbl_id);

extern bool
ctc_greatbelt_dkit_drv_get_module_id_by_string(char *str, uint32 *id);

#ifdef __cplusplus
}
#endif

#endif



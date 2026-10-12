#ifndef _SYS_GREATBELT_PARSER_IO_H_
#define _SYS_GREATBELT_PARSER_IO_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "sal.h"
#include "ctc_parser.h"
/*
#include "greatbelt/include/drv_io.h"
*/
#include "greatbelt/include/drv_io.h"

extern int32
sys_greatbelt_parser_io_set_parser_pbb_ctl_entry(uint8 lchip, parser_pbb_ctl_t* parser_pbb_ctl);

extern int32
sys_greatbelt_parser_io_get_parser_pbb_ctl_entry(uint8 lchip, parser_pbb_ctl_t* parser_pbb_ctl);

extern int32
sys_greatbelt_parser_io_set_parser_packet_type_table_entry(uint8 lchip, parser_packet_type_map_t* parser_packet_type_table);

extern int32
sys_greatbelt_parser_io_get_parser_packet_type_table_entry(uint8 lchip, parser_packet_type_map_t* parser_packet_type_table);

extern int32
sys_greatbelt_parser_io_set_parser_ethernet_ctl_entry(uint8 lchip, parser_ethernet_ctl_t* parser_ethernet_ctl);

extern int32
sys_greatbelt_parser_io_get_parser_ethernet_ctl_entry(uint8 lchip, parser_ethernet_ctl_t* parser_ethernet_ctl);

extern int32
sys_greatbelt_parser_io_set_cvlan_tpid(uint8 lchip, uint32 cvlan_tpid);

extern int32
sys_greatbelt_parser_io_get_cvlan_tpid(uint8 lchip, uint32* cvlan_tpid);

extern int32
sys_greatbelt_parser_io_set_max_length_field(uint8 lchip, uint32 max_length_field);

extern int32
sys_greatbelt_parser_io_get_max_length_field(uint8 lchip, uint32* max_length_field);

extern int32
sys_greatbelt_parser_io_set_i_tag_tpid(uint8 lchip, uint32 i_tag_tpid);

extern int32
sys_greatbelt_parser_io_get_i_tag_tpid(uint8 lchip, uint32* i_tag_tpid);

extern int32
sys_greatbelt_parser_io_set_bvlan_tpid(uint8 lchip, uint32 bvlan_tpid);

extern int32
sys_greatbelt_parser_io_get_bvlan_tpid(uint8 lchip, uint32* bvlan_tpid);

extern int32
sys_greatbelt_parser_io_set_cn_tag_tpid(uint8 lchip, uint32 cn_tag_tpid);

extern int32
sys_greatbelt_parser_io_get_cn_tag_tpid(uint8 lchip, uint32* cn_tag_tpid);

extern int32
sys_greatbelt_parser_io_set_vlan_parsing_num(uint8 lchip, uint32 vlan_parsing_num);

extern int32
sys_greatbelt_parser_io_get_vlan_parsing_num(uint8 lchip, uint32* vlan_parsing_num);

extern int32
sys_greatbelt_parser_io_set_svlan_tpid0(uint8 lchip, uint32 svlan_tpid0);

extern int32
sys_greatbelt_parser_io_get_svlan_tpid0(uint8 lchip, uint32* svlan_tpid0);

extern int32
sys_greatbelt_parser_io_set_svlan_tpid1(uint8 lchip, uint32 svlan_tpid1);

extern int32
sys_greatbelt_parser_io_get_svlan_tpid1(uint8 lchip, uint32* svlan_tpid1);

extern int32
sys_greatbelt_parser_io_set_svlan_tpid2(uint8 lchip, uint32 svlan_tpid2);

extern int32
sys_greatbelt_parser_io_get_svlan_tpid2(uint8 lchip, uint32* svlan_tpid2);

extern int32
sys_greatbelt_parser_io_set_svlan_tpid3(uint8 lchip, uint32 svlan_tpid3);

extern int32
sys_greatbelt_parser_io_get_svlan_tpid3(uint8 lchip, uint32* svlan_tpid3);

extern int32
sys_greatbelt_parser_io_set_parser_layer2_protocol_cam_valid_entry(uint8 lchip, parser_layer2_protocol_cam_valid_t* parser_layer2_protocol_cam_valid);

extern int32
sys_greatbelt_parser_io_get_parser_layer2_protocol_cam_valid_entry(uint8 lchip, parser_layer2_protocol_cam_valid_t* parser_layer2_protocol_cam_valid);

extern int32
sys_greatbelt_parser_io_set_parser_layer2_protocol_cam_entry(uint8 lchip, parser_layer2_protocol_cam_t* parser_layer2_protocol_cam);

extern int32
sys_greatbelt_parser_io_get_parser_layer2_protocol_cam_entry(uint8 lchip, parser_layer2_protocol_cam_t* parser_layer2_protocol_cam);

extern int32
sys_greatbelt_parser_io_set_parser_layer2_flex_ctl_entry(uint8 lchip, parser_layer2_flex_ctl_t* parser_layer2_flex_ctl);

extern int32
sys_greatbelt_parser_io_get_parser_layer2_flex_ctl_entry(uint8 lchip, parser_layer2_flex_ctl_t* parser_layer2_flex_ctl);

extern int32
sys_greatbelt_parser_io_set_parser_mpls_ctl_entry(uint8 lchip, parser_mpls_ctl_t* parser_mpls_ctl);

extern int32
sys_greatbelt_parser_io_get_parser_mpls_ctl_entry(uint8 lchip, parser_mpls_ctl_t* parser_mpls_ctl);

extern int32
sys_greatbelt_parser_io_set_parser_fcoe_ctl_entry(uint8 lchip, parser_fcoe_ctl_t* parser_fcoe_ctl);

extern int32
sys_greatbelt_parser_io_get_parser_fcoe_ctl_entry(uint8 lchip, parser_fcoe_ctl_t* parser_fcoe_ctl);

extern int32
sys_greatbelt_parser_io_set_parser_trill_ctl_entry(uint8 lchip, parser_trill_ctl_t* parser_trill_ctl);

extern int32
sys_greatbelt_parser_io_get_parser_trill_ctl_entry(uint8 lchip, parser_trill_ctl_t* parser_trill_ctl);

extern int32
sys_greatbelt_parser_io_set_parser_ip_hash_ctl_entry(uint8 lchip, parser_ip_hash_ctl_t* parser_ip_hash_ctl);

extern int32
sys_greatbelt_parser_io_get_parser_ip_hash_ctl_entry(uint8 lchip, parser_ip_hash_ctl_t* parser_ip_hash_ctl);

extern int32
sys_greatbelt_parser_io_set_parser_layer3_flex_ctl_entry(uint8 lchip, parser_layer3_flex_ctl_t* parser_layer3_flex_ctl);

extern int32
sys_greatbelt_parser_io_get_parser_layer3_flex_ctl_entry(uint8 lchip, parser_layer3_flex_ctl_t* parser_layer3_flex_ctl);

extern int32
sys_greatbelt_parser_io_set_parser_layer3_protocol_cam_valid_entry(uint8 lchip, parser_layer3_protocol_cam_valid_t* parser_layer3_protocol_cam_valid);

extern int32
sys_greatbelt_parser_io_get_parser_layer3_protocol_cam_valid_entry(uint8 lchip, parser_layer3_protocol_cam_valid_t* parser_layer3_protocol_cam_valid);

extern int32
sys_greatbelt_parser_io_set_parser_layer3_protocol_cam_entry(uint8 lchip, parser_layer3_protocol_cam_t* parser_layer3_protocol_cam);

extern int32
sys_greatbelt_parser_io_get_parser_layer3_protocol_cam_entry(uint8 lchip, parser_layer3_protocol_cam_t* parser_layer3_protocol_cam);

extern int32
sys_greatbelt_parser_io_set_parser_layer3_protocol_ctl_entry(uint8 lchip, parser_layer3_protocol_ctl_t* parser_layer3_protocol_ctl);

extern int32
sys_greatbelt_parser_io_set_parser_l3_hash_ctl_entry(uint8 lchip, parser_layer3_hash_ctl_t* parser_l3_hash_ctl);

extern int32
sys_greatbelt_parser_io_get_parser_l3_hash_ctl_entry(uint8 lchip, parser_layer3_hash_ctl_t* parser_l3_hash_ctl);

extern int32
sys_greatbelt_parser_io_set_parser_l4_hash_ctl_entry(uint8 lchip, parser_layer4_hash_ctl_t* parser_l4_hash_ctl);

extern int32
sys_greatbelt_parser_io_get_parser_l4_hash_ctl_entry(uint8 lchip, parser_layer4_hash_ctl_t* parser_l4_hash_ctl);

extern int32
sys_greatbelt_parser_io_set_parser_layer4_flag_op_ctl_entry(uint8 lchip, parser_layer4_flag_op_ctl_t* parser_layer4_flag_op_ctl);

extern int32
sys_greatbelt_parser_io_get_parser_layer4_flag_op_ctl_entry(uint8 lchip, parser_layer4_flag_op_ctl_t* parser_layer4_flag_op_ctl);

extern int32
sys_greatbelt_parser_io_set_parser_layer4_port_op_sel_entry(uint8 lchip, parser_layer4_port_op_sel_t* parser_layer4_port_op_sel);

extern int32
sys_greatbelt_parser_io_get_parser_layer4_port_op_sel_entry(uint8 lchip, parser_layer4_port_op_sel_t* parser_layer4_port_op_sel);

extern int32
sys_greatbelt_parser_io_set_parser_layer4_port_op_ctl_entry(uint8 lchip, parser_layer4_port_op_ctl_t* parser_layer4_port_op_ctl);

extern int32
sys_greatbelt_parser_io_get_parser_layer4_port_op_ctl_entry(uint8 lchip, parser_layer4_port_op_ctl_t* parser_layer4_port_op_ctl);

extern int32
sys_greatbelt_parser_io_set_parser_layer4_flex_ctl_entry(uint8 lchip, parser_layer4_flex_ctl_t* parser_layer4_flex_ctl);

extern int32
sys_greatbelt_parser_io_get_parser_layer4_flex_ctl_entry(uint8 lchip, parser_layer4_flex_ctl_t* parser_layer4_flex_ctl);

extern int32
sys_greatbelt_parser_io_set_parser_layer4_app_ctl_entry(uint8 lchip, parser_layer4_app_ctl_t* parser_layer4_app_ctl);

extern int32
sys_greatbelt_parser_io_get_parser_layer4_app_ctl_entry(uint8 lchip, parser_layer4_app_ctl_t* parser_layer4_app_ctl);

extern int32
sys_greatbelt_parser_io_set_parser_layer4_app_data_ctl_entry(uint8 lchip, parser_layer4_app_data_ctl_t* parser_layer4_app_data_ctl);

extern int32
sys_greatbelt_parser_io_get_parser_layer4_app_data_ctl_entry(uint8 lchip, parser_layer4_app_data_ctl_t* parser_layer4_app_data_ctl);

extern int32
sys_greatbelt_parser_io_set_parser_layer4_ach_ctl_entry(uint8 lchip, parser_layer4_ach_ctl_t* parser_layer4_ach_ctl);

#ifdef __cplusplus
}
#endif

#endif


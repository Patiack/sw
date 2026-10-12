/*
   Copyright (C) 2017 Shenzhen Hored Technology Inc.
*/

#ifndef __HSL_RT_H__
#define __HSL_RT_H__

#ifdef  __cplusplus
extern "C" {
#endif

#include "hsl_prefix.h"
#include "hsl_table.h"
#include "hsl_if.h"
#include "hsl_nh.h"

#define RT_INFO_BUFF_LEN 512

typedef struct hsl_nh_entry_list_s
{
    hsl_nh_entry_t *nh;
    struct hsl_nh_entry_list_s *next;
}hsl_nh_entry_list_t;

typedef struct hsl_prefix_info_s
{
    u_int32_t nh_num;
    hsl_nh_entry_list_t *nh_list;
}hsl_prefix_info_t;

extern int hsl_add_connected_rt(hsl_prefix_t *prefix, struct hsl_if_s *ifp);
extern int hsl_del_connected_rt(hsl_prefix_t *prefix, struct hsl_if_s *ifp);
extern int hsl_add_rt(hsl_prefix_t *prefix, hsl_prefix_t *nexthop,
		      u_int32_t ifindex, u_int32_t is_mpls, u_int32_t mpls_label);
extern int hsl_del_rt(hsl_prefix_t *prefix, hsl_prefix_t *nexthop,
		      u_int32_t ifindex);
extern int hsl_add_rt_to_nh(struct hsl_route_node *rn, struct hsl_nh_entry_s *nh);
extern int hsl_del_rt_from_nh(struct hsl_route_node *rn, struct hsl_nh_entry_s *nh);
extern int hsl_add_nh_to_rt(struct hsl_route_node *rn, struct hsl_nh_entry_s *nh);
extern void hsl_del_nh_from_rt(struct hsl_route_node *rn, struct hsl_nh_entry_s *nh);


#ifdef  __cplusplus
   }
#endif

#endif /* __HSL_RT_H__ */


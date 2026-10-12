/*
   Copyright (C) 2017 Shenzhen Hored Technology Inc.
*/

#ifndef __HSL_L3_H__
#define __HSL_L3_H__

#ifdef  __cplusplus
extern "C" {
#endif

#include "hsl_prefix.h"
#include "hsl_table.h"
#include "hsl_nh.h"


#if defined(HAVE_L3)
extern int hsl_l3_add_null_prefix(struct hsl_route_node *rn, struct hsl_nh_entry_s *nh);
extern int hsl_l3_add_prefix(struct hsl_route_node *rn, struct hsl_nh_entry_s *nh);
extern int hsl_l3_del_prefix(struct hsl_route_node *rn, struct hsl_nh_entry_s *nh);
extern int hsl_l3_add_nh(struct hsl_nh_entry_s *nh);
extern int hsl_l3_del_nh(struct hsl_nh_entry_s *nh);
#endif /* HAVE_L3 */


#ifdef  __cplusplus
}
#endif

#endif /* __HSL_L3_H__ */

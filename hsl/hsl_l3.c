/*
   Copyright (C) 2017 Shenzhen Hored Technology Inc.
*/

#include "hsl.h"
#include "hsl_l3.h"
#include "hw_l3.h"


#if defined(HAVE_L3)

int hsl_l3_add_null_prefix(struct hsl_route_node *rn, struct hsl_nh_entry_s *nh)
{
    if(rn == NULL)
    {
        return RUNOS_OK;
    }
    if(rn->p.family == 0)
    {
        return RUNOS_OK;
    }
    return hw_l3_add_null_prefix(rn, nh);
}

int hsl_l3_add_prefix(struct hsl_route_node *rn, struct hsl_nh_entry_s *nh)
{
    return hw_l3_add_prefix(rn, nh);
}

int hsl_l3_del_prefix(struct hsl_route_node *rn, struct hsl_nh_entry_s *nh)
{
    return hw_l3_del_prefix(rn, nh);
}

int hsl_l3_add_nh(struct hsl_nh_entry_s *nh)
{
    return hw_l3_add_nh(nh);
}

int hsl_l3_del_nh(struct hsl_nh_entry_s *nh)
{
    return hw_l3_del_nh(nh);
}

#endif /* HAVE_L3 */

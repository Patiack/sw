/*
   Copyright (C) 2017 Shenzhen Hored Technology Inc.
*/

#ifndef __HSL_ARP_H__
#define __HSL_ARP_H__

#ifdef  __cplusplus
extern "C" {
#endif

#include "runos_eth.h"
#include "runos_ip.h"
#include "hsl_nh.h"
#include "hsl_if.h"

#define HSL_ARP_TIMER_STEP      10

#define HSL_ARP_AGING_TIME(ifp) (ifp->l3_intf.arp_aging_time)

#define HSL_ARP_PROBE_MAX_COUNT  3

int hsl_arp_del_by_ifindex(u_int32_t ifindex);
int hsl_handle_arp(struct hsl_if_s *ifp, struct runos_eth_hdr_s *eth, struct runos_arp_hdr_s *arp, u_int32_t l2_ifindex);
int hsl_arp_probe(hsl_nh_entry_t *nh);

#ifdef  __cplusplus
   }
#endif
   
#endif /* __HSL_ARP_H__ */
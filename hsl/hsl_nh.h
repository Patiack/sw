#ifndef __HSL_NH_H__
#define __HSL_NH_H__

#ifdef  __cplusplus
extern "C" {
#endif

#include "hsl_table.h"
#include "hsl_if.h"
#include "zebra/arp_types.h"

#define HSL_NH_ENTRY_STATIC     (1 << 0)
#define HSL_NH_ENTRY_DYNAMIC    (1 << 1)
#define HSL_NH_ENTRY_IN_HW      (1 << 2)
#define HSL_NH_ENTRY_INAVALID   (1 << 3)

#define HSL_NH_TYPE_IP          0
#define HSL_NH_TYPE_IPV6        1

#define HSL_NH_MAX_NUM          ARP_MAX_NUM
#define HSL_NH6_MAX_NUM         ND_MAX_NUM

#define HSL_NH_SHOW_MAX         10

#define FOR_EACH_NH(nh, rn) \
            for (nh = (rn)->info; nh; nh = (nh)->next)
#define FOR_EACH_NH_SAFE(nh, nh_next, rn) \
            for (nh = (rn)->info; nh && (nh_next = (nh)->next, 1); nh = nh_next)

typedef struct hsl_nh_entry_s
{
    u_int32_t ifindex;
    u_int32_t l2_ifindex;
    u_int32_t ori_l2_ifindex;
    u_int32_t id;
    umac_t mac;
    u_int8_t flags;
    u_int8_t type;
    u_int32_t tree_count;
    struct hsl_avl_tree_s *prefix_tree; 

    u_int32_t timeout;
	u_int8_t probe_count;

    /* MPLS: ingress push label for BGP-LU route nexthop */
    u_int32_t mpls_label;
    u_int32_t is_mpls;

    struct hsl_route_node *rn;
    struct hsl_nh_entry_s *next;
}hsl_nh_entry_t;

void nh_lock(void);
void nh_unlock(void);

int hsl_nh_clear(struct hsl_route_table *table, hsl_prefix_t *p, u_int32_t ifindex);
int hsl_nh_get(u_int32_t ip, void *buf, u_int32_t ifindex, u_int32_t flags);
int hsl_nh_get_next(u_int32_t ip, void *entry, u_int32_t ifindex, u_int32_t flags);

int hsl_nh_del_static(u_int32_t ifindex, u_int32_t ip);
int hsl_nh_add_static(u_int32_t ifindex, u_int32_t ip, umac_t mac);
int hsl_nh_add (hsl_prefix_t *prefix, u_int32_t ifindex, u_int32_t l2_ifindex, u_int8_t *mac, u_int8_t flags, u_int8_t type);
int hsl_nh_del (hsl_prefix_t *prefix, u_int32_t ifindex, u_int32_t nh_type);

void hsl_nh_sync_aging_time(void);
void hsl_nh_aging(void);
void hsl_update_nh_prefix(struct hsl_nh_entry_s *nh);
void hsl_update_nh_prefix_delete(struct hsl_nh_entry_s *nh);
int hsl_nh_is_full(hsl_if_t *ifp);

#if defined(HAVE_IPV6)
int hsl_nh6_add_static(u_int32_t ifindex, runos_ipv6_t *ip6, umac_t mac);
int hsl_nh6_del_static(u_int32_t ifindex, runos_ipv6_t *ip6);
void hsl_nh6_aging(void);
int hsl_nh6_get_next(runos_ipv6_t *ip6, void *entry, u_int32_t ifindex, u_int32_t flags);
int hsl_nh6_get(runos_ipv6_t *ip6, void *entry, u_int32_t ifindex, u_int32_t flags);

int hsl_nh6_is_full(hsl_if_t *ifp);
#endif

extern hsl_nh_entry_t * hsl_rt_get_nh(u_int32_t ifindex, hsl_prefix_t *prefix, u_int32_t flag);
extern hsl_nh_entry_t *hsl_rt_lookup_nh__(u_int32_t ifindex, hsl_prefix_t *prefix);
extern bool hsl_rt_lookup_nh(u_int32_t ifindex, hsl_prefix_t *prefix);

extern int _hsl_nh_del(hsl_nh_entry_t *nh);
extern int hsl_nh_port_down(u_int32_t ifindex);

#ifdef HAVE_MLAG
extern int hsl_nh_mlag_update(u_int32_t l2_ifindex, bool up);
#endif

#ifdef  __cplusplus
    }
#endif

#endif /* __HSL_NH_H__ */

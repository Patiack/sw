/*
 * HSL VRF Management API Header
 *
 * This header defines the interface for managing VRF (Virtual Routing and
 * Forwarding) instances in the Hardware Switch Layer. It provides
 * functions to create, delete, and manage routes within VRF contexts,
 * with support for MPLS L3VPN services.
 *
 * The wire-format structures (struct ctc_hal_msg_route_vrf) mirror the
 * definitions in zebra/ctcdplane/ctc_dplane.h - keep both in sync.
 */

#ifndef _HSL_RT_VRF_H_
#define _HSL_RT_VRF_H_

#include "hsl.h"
#include "hal_msg.h"
#include <linux/types.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <linux/list.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------
 * HAL command codes for the VRF route message (HAL_MODULE_ROUTE).
 * Must match zebra/ctcdplane/ctc_dplane.h (guarded, values are
 * module-local: 17/18 are free in the ROUTE module space).
 * ------------------------------------------------------------------ */
#ifndef HAL_MSG_RT_VRF_ADD
#define HAL_MSG_RT_VRF_ADD  17   /* VRF route add/update */
#define HAL_MSG_RT_VRF_DEL  18   /* VRF route delete */
#endif

/* MPLS label type (guarded against redefinition) */
#ifndef mpls_label_t
typedef uint32_t mpls_label_t;
#endif

/*
 * HSL nexthop descriptor passed to the VRF route APIs.
 * gate is in network byte order; family selects the union member
 * (AF_INET -> gate.ipv4, AF_INET6 -> gate.ipv6). MPLS labels are in
 * network byte order (matching the existing hw_mpls.c / hsl_add_rt
 * convention). node anchors a per-route copy on a cache list
 * (hsl_rt_vrf.c / hsl_mpls_vrf.c); stack/array instances never use it.
 */
struct hsl_nexthop {
	uint32_t ifindex;       /* HSL-mapped output interface index */
	uint8_t  family;        /* AF_INET / AF_INET6: selects gate union */
	uint8_t  is_mpls;       /* 1: push out_label on egress */
	uint8_t  pad[2];
	union {
		struct in_addr  ipv4; /* AF_INET  nexthop (network byte order) */
		struct in6_addr ipv6; /* AF_INET6 nexthop (network byte order) */
	} gate;
	mpls_label_t out_label; /* outgoing MPLS label (network byte order) */
	struct list_head node;  /* cache-list node */
};

/* ------------------------------------------------------------------
 * HAL wire format for the VRF route message.
 * Must stay layout-identical with zebra/ctcdplane/ctc_dplane.h.
 * ------------------------------------------------------------------ */
#define CTC_HAL_MSG_MAX_NH_VRF  8

struct ctc_hal_msg_nexthop_vrf {
	uint32_t ifindex;      /* output interface index (HSL-mapped) */
	union {
		uint32_t ipv4;   /* AF_INET  nexthop (network byte order) */
		uint8_t  ipv6[16]; /* AF_INET6 nexthop (network byte order bytes) */
	} gate;
	uint8_t  type;         /* nexthop type (FRR NEXTHOP_TYPE_*) */
	uint8_t  flags;        /* nexthop flags (FRR NEXTHOP_FLAG_*) */
	uint8_t  is_mpls;      /* 1: push out_label on egress */
	uint8_t  gate_af;      /* AF_INET / AF_INET6: selects gate union */
	uint32_t out_label;    /* outgoing MPLS label (network byte order) */
};

_Static_assert(sizeof(struct ctc_hal_msg_nexthop_vrf) == 28,
	       "VRF nexthop structure size mismatch");

struct ctc_hal_msg_route_vrf {
	uint32_t vrf_id;            /* VRF identifier */
	uint8_t  af;                /* AF_INET / AF_INET6 */
	uint8_t  prefixlen;         /* prefix length */
	uint8_t  pad1[2];
	union {
		uint32_t ipv4;       /* AF_INET  prefix (network byte order) */
		uint8_t  ipv6[16];   /* AF_INET6 prefix (network byte order bytes) */
	} prefix;                  /* destination prefix, chosen by af */
	union {
		uint32_t ipv4;       /* AF_INET  source prefix (network byte order) */
		uint8_t  ipv6[16];   /* AF_INET6 source prefix (network byte order bytes) */
	} src_prefix;              /* source prefix, all-zero if none */
	uint8_t  proto_type;        /* CTC_PROTO_* */
	uint8_t  distance;          /* administrative distance */
	uint16_t metric;            /* route metric */

	/* MPLS label stack for L3VPN routes: [transport, vpn, ...] */
	uint8_t  label_stack_depth; /* 0-3 */
	uint8_t  pad2[3];
	uint32_t label_stack[3];    /* network byte order */

	/* Nexthop array */
	uint8_t  nh_count;
	uint8_t  pad3[3];
	struct ctc_hal_msg_nexthop_vrf nh[CTC_HAL_MSG_MAX_NH_VRF];
};

_Static_assert(sizeof(struct ctc_hal_msg_route_vrf) == 288,
	       "Route VRF message size must be 288 bytes to match ctc_dplane.c");

/**
 * VRF instance management functions
 */

/**
 * Create a VRF instance in hardware
 *
 * @param vrf_id   VRF identifier (must be unique)
 * @param name     VRF name (optional, can be NULL)
 * @return         0 on success, negative error code on failure
 *
 * This function creates a new VRF instance in the hardware forwarding tables.
 * The VRF ID is used to isolate routing tables for different tenants or
 * services. For L3VPN, each customer site typically gets its own VRF.
 */
int hsl_vrf_create(uint32_t vrf_id, const char *name);

/**
 * Delete a VRF instance from hardware
 *
 * @param vrf_id   VRF identifier to delete
 * @return         0 on success, negative error code on failure
 *
 * This function removes a VRF instance and all associated routes from
 * the hardware. All routes in the VRF must be deleted before the VRF
 * itself can be deleted.
 */
int hsl_vrf_delete(uint32_t vrf_id);

/**
 * Check if a VRF instance exists
 *
 * @param vrf_id   VRF identifier to check
 * @return         1 if VRF exists, 0 if not found, negative on error
 */
int hsl_vrf_lookup(uint32_t vrf_id);

/**
 * Flush all routes from a VRF
 *
 * @param vrf_id   VRF identifier
 * @return         0 on success, negative error code on failure
 *
 * This function removes all routes from the specified VRF but keeps
 * the VRF instance itself intact. Useful for VRF reconfiguration.
 */
int hsl_vrf_flush_routes(uint32_t vrf_id);

/**
 * VRF-aware route operations
 */

/**
 * Add a route to a specific VRF with optional MPLS label support
 *
 * @param vrf_id        VRF identifier
 * @param af            Address family (AF_INET or AF_INET6)
 * @param prefix        Destination prefix
 * @param prefixlen     Prefix length
 * @param nh_list       Array of nexthops
 * @param nh_count      Number of nexthops
 * @param label_stack   MPLS label stack (can be NULL for non-MPLS routes)
 * @param label_depth   Number of labels in stack (0 if no labels)
 * @return              0 on success, negative error code on failure
 *
 * This function installs a route in the specified VRF's forwarding table.
 * For L3VPN routes, the label_stack parameter carries the MPLS labels
 * that should be pushed onto packets matching this route.
 *
 * Label stack order: [transport_label, vpn_label, ...]
 * - transport_label: Used for core network forwarding (LDP/RSVP-TE assigned)
 * - vpn_label: Identifies the destination VRF at the egress PE
 */
int hsl_route_add_vrf(uint32_t vrf_id, uint8_t af,
		      const void *prefix, uint8_t prefixlen,
		      const struct hsl_nexthop *nh_list, uint8_t nh_count,
		      const mpls_label_t *label_stack, uint8_t label_depth);

/**
 * Delete a route from a specific VRF
 *
 * @param vrf_id        VRF identifier
 * @param af            Address family
 * @param prefix        Destination prefix
 * @param prefixlen     Prefix length
 * @return              0 on success, negative error code on failure
 */
int hsl_route_del_vrf(uint32_t vrf_id, uint8_t af,
		      const void *prefix, uint8_t prefixlen);

/**
 * Query route information in a VRF
 *
 * @param vrf_id        VRF identifier
 * @param af            Address family
 * @param prefix        Destination prefix
 * @param prefixlen     Prefix length
 * @param nh_list       Output: array of nexthops (caller allocates)
 * @param nh_capacity   Size of nh_list array
 * @param label_stack   Output: MPLS label stack (can be NULL)
 * @param label_depth   Output: number of labels
 * @return              Number of nexthops found, negative on error
 */
int hsl_route_query_vrf(uint32_t vrf_id, uint8_t af,
			const void *prefix, uint8_t prefixlen,
			struct hsl_nexthop *nh_list, uint8_t nh_capacity,
			mpls_label_t *label_stack, uint8_t *label_depth);

/**
 * Get statistics for a VRF
 *
 * @param vrf_id        VRF identifier
 * @param route_count   Output: number of routes in VRF
 * @param byte_count    Output: total bytes forwarded through VRF
 * @param packet_count  Output: total packets forwarded through VRF
 * @return              0 on success, negative error code on failure
 */
int hsl_vrf_get_stats(uint32_t vrf_id, uint32_t *route_count,
		      uint64_t *byte_count, uint64_t *packet_count);

/**
 * Decode a HAL message for the HAL_MODULE_ROUTE module and dispatch it.
 * Called by hsl_rt_msg() for HAL_MSG_RT_VRF_ADD / HAL_MSG_RT_VRF_DEL.
 *
 * @param msg   HAL message (HSL_MSG_DATA points to ctc_hal_msg_route_vrf)
 * @return      0 on success, negative error code on failure
 */
int hsl_rt_vrf_msg(hsl_msg_t *msg);

#ifdef __cplusplus
}
#endif

#endif /* _HSL_RT_VRF_H_ */

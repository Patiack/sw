/*
 * HSL MPLS LSP Management API Header for VRF/L3VPN
 * 
 * This header defines the interface for managing MPLS Label Switched Paths
 * (LSPs) in VRF contexts. It provides functions to create, delete, and manage
 * ILM (Incoming Label Map) entries for L3VPN services.
 */

#ifndef _HSL_MPLS_VRF_H_
#define _HSL_MPLS_VRF_H_

/* Reuse mpls_label_t / struct hsl_nexthop / hsl_msg_t definitions */
#include "hsl_rt_vrf.h"
#include <linux/types.h>
#include <linux/in.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * MPLS operation types
 */
#define HSL_MPLS_OP_PUSH    1   /* Push label onto stack */
#define HSL_MPLS_OP_POP     2   /* Pop top label from stack */
#define HSL_MPLS_OP_SWAP    3   /* Swap incoming label with outgoing label */

/* ------------------------------------------------------------------
 * HAL command codes for the VRF LSP message (HAL_MODULE_MPLS).
 * Must match zebra/ctcdplane/ctc_dplane.h (guarded, values are
 * module-local: 17/18 are free in the MPLS module space).
 * ------------------------------------------------------------------ */
#ifndef HAL_MSG_MPLS_LSP_VRF_ADD
#define HAL_MSG_MPLS_LSP_VRF_ADD  40   /* VRF LSP (ILM) add */
#define HAL_MSG_MPLS_LSP_VRF_DEL  41   /* VRF LSP (ILM) delete */
#endif

/**
 * MPLS LSP operations for VRF
 */

/**
 * Add MPLS LSP entry for a specific VRF
 * 
 * @param vrf_id        VRF identifier
 * @param in_label      Incoming label (ILM lookup key)
 * @param out_label     Outgoing label (after swap/push)
 * @param op_type       Operation type (PUSH/POP/SWAP)
 * @param nh_list       Array of nexthops
 * @param nh_count      Number of nexthops
 * @return              0 on success, negative error code on failure
 * 
 * This function creates an ILM (Incoming Label Map) entry that maps an
 * incoming MPLS label to forwarding actions. For L3VPN:
 * - PUSH: Used at ingress PE to push VPN label onto packet
 * - SWAP: Used at P routers to swap transport labels
 * - POP: Used at egress PE to remove VPN label and forward to VRF
 */
int hsl_mpls_lsp_add_vrf(uint32_t vrf_id, mpls_label_t in_label,
                         mpls_label_t out_label, uint8_t op_type,
                         const struct hsl_nexthop *nh_list, uint8_t nh_count);

/**
 * Delete MPLS LSP entry from a VRF
 * 
 * @param vrf_id        VRF identifier
 * @param in_label      Incoming label to delete
 * @return              0 on success, negative error code on failure
 */
int hsl_mpls_lsp_del_vrf(uint32_t vrf_id, mpls_label_t in_label);

/**
 * Check if an MPLS LSP entry exists
 * 
 * @param vrf_id        VRF identifier
 * @param in_label      Incoming label to check
 * @return              1 if LSP exists, 0 if not found, negative on error
 */
int hsl_mpls_lsp_lookup(uint32_t vrf_id, mpls_label_t in_label);

/**
 * Query MPLS LSP information
 * 
 * @param vrf_id        VRF identifier
 * @param in_label      Incoming label
 * @param out_label     Output: outgoing label
 * @param op_type       Output: operation type
 * @param nh_list       Output: array of nexthops (caller allocates)
 * @param nh_capacity   Size of nh_list array
 * @return              Number of nexthops found, negative on error
 */
int hsl_mpls_lsp_query(uint32_t vrf_id, mpls_label_t in_label,
                       mpls_label_t *out_label, uint8_t *op_type,
                       struct hsl_nexthop *nh_list, uint8_t nh_capacity);

/**
 * MPLS FEC (Forwarding Equivalence Class) binding for VPN routes
 */

/**
 * Bind MPLS label to FEC for VPN routes
 * 
 * @param vrf_id        VRF identifier
 * @param prefix        Destination prefix (FEC)
 * @param prefixlen     Prefix length
 * @param label         MPLS label to bind
 * @return              0 on success, negative error code on failure
 * 
 * This function associates an MPLS label with a destination prefix (FEC).
 * In BGP L3VPN, this is used to advertise the VPN label to remote PEs via
 * MP-BGP. When packets arrive with this label, they are forwarded according
 * to the bound FEC.
 */
int hsl_mpls_fec_bind_vrf(uint32_t vrf_id, const struct in_addr *prefix,
                          uint8_t prefixlen, mpls_label_t label);

/**
 * Unbind MPLS label from FEC
 * 
 * @param vrf_id        VRF identifier
 * @param prefix        Destination prefix
 * @param prefixlen     Prefix length
 * @return              0 on success, negative error code on failure
 */
int hsl_mpls_fec_unbind_vrf(uint32_t vrf_id, const struct in_addr *prefix,
                            uint8_t prefixlen);

/**
 * Query FEC-to-label binding
 * 
 * @param vrf_id        VRF identifier
 * @param prefix        Destination prefix
 * @param prefixlen     Prefix length
 * @param label         Output: bound MPLS label
 * @return              0 if binding found, -ENOENT if not found, negative on error
 */
int hsl_mpls_fec_query(uint32_t vrf_id, const struct in_addr *prefix,
                       uint8_t prefixlen, mpls_label_t *label);

/**
 * Flush all MPLS LSP entries from a VRF
 * 
 * @param vrf_id        VRF identifier
 * @return              0 on success, negative error code on failure
 */
int hsl_mpls_lsp_flush_vrf(uint32_t vrf_id);

/**
 * Get MPLS statistics for a VRF
 * 
 * @param vrf_id            VRF identifier
 * @param lsp_count         Output: number of LSP entries
 * @param fec_count         Output: number of FEC bindings
 * @param byte_count        Output: total bytes forwarded via MPLS
 * @param packet_count      Output: total packets forwarded via MPLS
 * @return                  0 on success, negative error code on failure
 */
int hsl_mpls_get_stats(uint32_t vrf_id, uint32_t *lsp_count,
                       uint32_t *fec_count, uint64_t *byte_count,
                       uint64_t *packet_count);

/* ------------------------------------------------------------------
 * HAL wire format for the VRF LSP (ILM) message.
 * Must stay layout-identical with zebra/ctcdplane/ctc_dplane.h.
 * ------------------------------------------------------------------ */
#define CTC_HAL_MSG_MAX_LSP_NH_VRF  4

struct ctc_hal_msg_mpls_lsp_nh_vrf {
	uint32_t ifindex;      /* output interface index */
	uint32_t gate;         /* nexthop IPv4 (network byte order) */
	uint32_t out_label;    /* label to push/swap to */
	uint8_t  is_mpls;      /* 1: MPLS nexthop */
	uint8_t  pad[3];
};

_Static_assert(sizeof(struct ctc_hal_msg_mpls_lsp_nh_vrf) == 16,
	       "VRF LSP nexthop size mismatch");

struct ctc_hal_msg_mpls_lsp_vrf {
	uint32_t vrf_id;            /* VRF identifier */
	uint32_t in_label;          /* incoming label (ILM lookup key) */
	uint32_t out_label;         /* outgoing label after swap */
	uint8_t  op_type;           /* HSL_MPLS_OP_* (PUSH/POP/SWAP) */
	uint8_t  nh_count;
	uint8_t  pad[2];
	struct ctc_hal_msg_mpls_lsp_nh_vrf nh[CTC_HAL_MSG_MAX_LSP_NH_VRF];
};

_Static_assert(sizeof(struct ctc_hal_msg_mpls_lsp_vrf) == 80,
	       "VRF LSP message size mismatch");

/**
 * Decode a HAL message for the HAL_MODULE_MPLS module and dispatch it.
 * Called by hsl_mpls_msg() for HAL_MSG_MPLS_LSP_VRF_ADD / _DEL.
 *
 * @param msg   HAL message (HSL_MSG_DATA points to ctc_hal_msg_mpls_lsp_vrf)
 * @return      0 on success, negative error code on failure
 */
int hsl_mpls_vrf_msg(hsl_msg_t *msg);

#ifdef __cplusplus
}
#endif

#endif /* _HSL_MPLS_VRF_H_ */

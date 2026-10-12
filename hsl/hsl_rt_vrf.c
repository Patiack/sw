/*
 * HSL VRF Management API Implementation
 *
 * This file implements VRF (Virtual Routing and Forwarding) management
 * functions for the Hardware Abstraction Layer. It provides software
 * caching and hardware interaction for VRF-aware routing with MPLS L3VPN
 * support.
 */

#include "hsl_rt_vrf.h"
#include "hsl_nh.h"
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <net/net_namespace.h>

/* ------------------------------------------------------------------
 * hw_l3 VRF API 前置声明（hw 层无 .h 头文件，与 hsl_mpls.c 模式一致）
 * ------------------------------------------------------------------ */
extern int hw_l3_vrf_create(uint32_t vrf_id, const char *name);
extern int hw_l3_vrf_delete(uint32_t vrf_id);
extern int hw_l3_vrf_lookup(uint32_t vrf_id);
extern int hw_l3_route_add_vrf(uint32_t vrf_id, uint8_t af,
			       const void *prefix, uint8_t prefixlen,
			       const struct hsl_nexthop *nh_list, uint8_t nh_count,
			       const mpls_label_t *label_stack, uint8_t label_depth);
extern int hw_l3_route_del_vrf(uint32_t vrf_id, uint8_t af,
			       const void *prefix, uint8_t prefixlen);

/* hsl_mpls_vrf.c: flush MPLS LSPs / FECs of a VRF (defined in
 * hsl_mpls_vrf.c, declared here to avoid pulling in its header). */
extern int hsl_mpls_lsp_flush_vrf(uint32_t vrf_id);

/* Mutex for VRF operations */
static DEFINE_MUTEX(hsl_vrf_mutex);

/**
 * Software cache for VRF route entries
 *
 * This structure maintains a local copy of routes installed in hardware
 * for debugging, verification, and fast lookup purposes.
 */
struct hsl_vrf_route_entry {
    uint32_t vrf_id;              /* VRF identifier */
    uint8_t af;                   /* Address family */
    union {
        struct in_addr  ipv4;     /* AF_INET  destination prefix */
        struct in6_addr ipv6;     /* AF_INET6 destination prefix */
    } prefix;                     /* Destination prefix (network byte order) */
    uint8_t prefixlen;            /* Prefix length */
    struct list_head nh_list;     /* List of nexthops */
    mpls_label_t label_stack[3];  /* MPLS label stack (max 3) */
    uint8_t label_depth;          /* Number of labels in stack */
    struct list_head node;        /* List node for global route list */
};

/* 前缀字节等值比较（按 af：v4 4B / v6 16B，均为网络序）。a/b 指向原始
 * 4/16 字节。返回 1 相等。 */
static int hsl_vrf_prefix_eq(uint8_t af, const void *a, const void *b)
{
    size_t len = (af == AF_INET6) ? sizeof(struct in6_addr)
                                  : sizeof(struct in_addr);
    return memcmp(a, b, len) == 0;
}

/* Global list of all VRF routes */
static LIST_HEAD(hsl_vrf_route_list);

/* VRF instance counter for statistics */
static uint32_t hsl_vrf_count = 0;

/**
 * Create a VRF instance in hardware
 */
int hsl_vrf_create(uint32_t vrf_id, const char *name)
{
    int ret;
    char vrf_name[64];

    mutex_lock(&hsl_vrf_mutex);

    /* Check if VRF already exists */
    if (hsl_vrf_lookup(vrf_id)) {
        HSL_LOG_WARN("VRF %u already exists", vrf_id);
        mutex_unlock(&hsl_vrf_mutex);
        return 0; /* Already exists - treat as success */
    }

    /* Prepare VRF name */
    if (name) {
        snprintf(vrf_name, sizeof(vrf_name), "%s", name);
    } else {
        snprintf(vrf_name, sizeof(vrf_name), "vrf%u", vrf_id);
    }

    /* Call HW layer to create VRF in chip */
    ret = hw_l3_vrf_create(vrf_id, vrf_name);
    if (ret < 0) {
        HSL_LOG_ERR("Failed to create VRF %u (%s) in hardware: ret=%d",
                    vrf_id, vrf_name, ret);
        mutex_unlock(&hsl_vrf_mutex);
        return ret;
    }

    hsl_vrf_count++;

    HSL_LOG_INFO("VRF %u (%s) created successfully (total VRFs: %u)",
                 vrf_id, vrf_name, hsl_vrf_count);

    mutex_unlock(&hsl_vrf_mutex);
    return 0;
}

/**
 * Delete a VRF instance from hardware
 */
int hsl_vrf_delete(uint32_t vrf_id)
{
    int ret;

    mutex_lock(&hsl_vrf_mutex);

    /* Flush all routes in this VRF first（本函数已持锁，走内部版避免
     * 对 hsl_vrf_mutex 二次加锁自死锁） */
    ret = hsl_vrf_flush_routes_locked(vrf_id);
    if (ret < 0) {
        HSL_LOG_WARN("Failed to flush routes from VRF %u before deletion", vrf_id);
        /* Continue with VRF deletion anyway */
    }

    /* Flush all MPLS LSPs / FECs of this VRF (L3VPN). The VRF is
     * being deleted; leaving them behind would leak hardware ILM /
     * push nexthop resources, and the labels may be reused later
     * (stale ILM would hijack new LSPs). Lock order:
     * hsl_vrf_mutex -> hsl_mpls_vrf_mutex -> hw_mpls_vrf_mutex;
     * no cycle. */
    ret = hsl_mpls_lsp_flush_vrf(vrf_id);
    if (ret < 0) {
        HSL_LOG_WARN("Failed to flush MPLS LSPs from VRF %u before deletion", vrf_id);
        /* Continue with VRF deletion anyway */
    }

    /* Verify VRF exists */
    if (!hsl_vrf_lookup(vrf_id)) {
        HSL_LOG_WARN("VRF %u does not exist", vrf_id);
        mutex_unlock(&hsl_vrf_mutex);
        return -ENOENT;
    }

    /* Call HW layer to delete VRF */
    ret = hw_l3_vrf_delete(vrf_id);
    if (ret < 0) {
        HSL_LOG_ERR("Failed to delete VRF %u from hardware: ret=%d", vrf_id, ret);
        mutex_unlock(&hsl_vrf_mutex);
        return ret;
    }

    if (hsl_vrf_count > 0)
        hsl_vrf_count--;

    HSL_LOG_INFO("VRF %u deleted successfully (remaining VRFs: %u)",
                 vrf_id, hsl_vrf_count);

    mutex_unlock(&hsl_vrf_mutex);
    return 0;
}

/**
 * Check if a VRF instance exists
 */
int hsl_vrf_lookup(uint32_t vrf_id)
{
    int ret;

    /* Query HW layer to check if VRF exists */
    ret = hw_l3_vrf_lookup(vrf_id);

    return ret;
}

/* 内部版：假定调用方已持有 hsl_vrf_mutex（hsl_vrf_delete 持锁后经此入口
 * 调用）。切勿在持锁状态调用公开版 hsl_vrf_flush_routes() —— 那会对同一
 * 把非递归内核 mutex 二次加锁而自死锁。 */
static int hsl_vrf_flush_routes_locked(uint32_t vrf_id)
{
    struct hsl_vrf_route_entry *entry, *tmp;
    int count = 0;
    int ret = 0;

    list_for_each_entry_safe(entry, tmp, &hsl_vrf_route_list, node) {
        if (entry->vrf_id == vrf_id) {
            /* Delete from hardware */
            ret = hw_l3_route_del_vrf(entry->vrf_id, entry->af,
                                     &entry->prefix, entry->prefixlen);
            if (ret < 0) {
                if (entry->af == AF_INET6)
                    HSL_LOG_ERR("Failed to delete route %pI6/%u from VRF %u during flush",
                               &entry->prefix.ipv6, entry->prefixlen, vrf_id);
                else
                    HSL_LOG_ERR("Failed to delete route %pI4/%u from VRF %u during flush",
                               &entry->prefix.ipv4, entry->prefixlen, vrf_id);
                /* Continue flushing other routes */
            }

            /* Free nexthops */
            struct hsl_nexthop *nh, *nh_tmp;
            list_for_each_entry_safe(nh, nh_tmp, &entry->nh_list, node) {
                list_del(&nh->node);
                kfree(nh);
            }

            list_del(&entry->node);
            kfree(entry);
            count++;
        }
    }

    HSL_LOG_INFO("Flushed %d routes from VRF %u", count, vrf_id);
    return 0;
}

/**
 * Flush all routes from a VRF（公开入口：自行加锁后转内部版）
 */
int hsl_vrf_flush_routes(uint32_t vrf_id)
{
    int ret;

    mutex_lock(&hsl_vrf_mutex);
    ret = hsl_vrf_flush_routes_locked(vrf_id);
    mutex_unlock(&hsl_vrf_mutex);
    return ret;
}

/**
 * Add a route to a specific VRF with MPLS label support
 */
int hsl_route_add_vrf(uint32_t vrf_id, uint8_t af,
                      const void *prefix, uint8_t prefixlen,
                      const struct hsl_nexthop *nh_list, uint8_t nh_count,
                      const mpls_label_t *label_stack, uint8_t label_depth)
{
    struct hsl_vrf_route_entry *entry;
    struct hsl_nexthop *nh;
    int ret;
    int i;

    if (!prefix || !nh_list || nh_count == 0) {
        HSL_LOG_ERR("Invalid parameters for VRF route add");
        return -EINVAL;
    }

    if (af != AF_INET && af != AF_INET6) {
        HSL_LOG_ERR("Unsupported address family %u", af);
        return -EAFNOSUPPORT;
    }

    mutex_lock(&hsl_vrf_mutex);

    /* Verify VRF exists */
    if (!hsl_vrf_lookup(vrf_id)) {
        HSL_LOG_ERR("VRF %u does not exist", vrf_id);
        mutex_unlock(&hsl_vrf_mutex);
        return -ENOENT;
    }

    /* 幂等 replace：同 (vrf_id, af, prefix, prefixlen) 已存在时先释放旧软件
     * 缓存条目再重建 —— 与 hw 层 hw_l3_route_add_vrf 的整表替换语义一致。
     * zebra 的路由 UPDATE（下一跳集合变化）以 HAL_MSG_RT_VRF_ADD 重复到达
     * 同一前缀，hw 的 IPUC 会按新 nexthop 覆盖旧 nhid；若这里也保留多个
     * 缓存条目，删除/flush 只清第一条、查询只回第一条，余下的成为泄漏的
     * stale 条目。这里不调用 hw_l3_route_del_vrf()：hw add 自身即
     * add-or-replace，单独先删一遍只是多余的一次芯片写。 */
    {
        struct hsl_vrf_route_entry *old, *tmp;

        list_for_each_entry_safe(old, tmp, &hsl_vrf_route_list, node) {
            if (old->vrf_id != vrf_id || old->af != af ||
                !hsl_vrf_prefix_eq(af, &old->prefix, prefix) ||
                old->prefixlen != prefixlen)
                continue;
            struct hsl_nexthop *nh_old, *nh_tmp;
            list_for_each_entry_safe(nh_old, nh_tmp, &old->nh_list, node) {
                list_del(&nh_old->node);
                kfree(nh_old);
            }
            list_del(&old->node);
            kfree(old);
            break;
        }
    }

    /* Allocate software cache entry */
    entry = kzalloc(sizeof(*entry), GFP_KERNEL);
    if (!entry) {
        HSL_LOG_ERR("Failed to allocate memory for VRF route entry");
        mutex_unlock(&hsl_vrf_mutex);
        return -ENOMEM;
    }

    entry->vrf_id = vrf_id;
    entry->af = af;
    if (af == AF_INET6)
        memcpy(&entry->prefix.ipv6, prefix, sizeof(entry->prefix.ipv6));
    else
        memcpy(&entry->prefix.ipv4, prefix, sizeof(entry->prefix.ipv4));
    entry->prefixlen = prefixlen;
    INIT_LIST_HEAD(&entry->nh_list);

    /* Copy nexthops */
    for (i = 0; i < nh_count; i++) {
        nh = kzalloc(sizeof(*nh), GFP_KERNEL);
        if (!nh) {
            HSL_LOG_ERR("Failed to allocate nexthop %d", i);
            /* Cleanup on error */
            struct hsl_nexthop *cleanup_nh, *tmp;
            list_for_each_entry_safe(cleanup_nh, tmp, &entry->nh_list, node) {
                list_del(&cleanup_nh->node);
                kfree(cleanup_nh);
            }
            kfree(entry);
            mutex_unlock(&hsl_vrf_mutex);
            return -ENOMEM;
        }
        *nh = nh_list[i];
        list_add_tail(&nh->node, &entry->nh_list);
    }

    /* Copy label stack */
    if (label_stack && label_depth > 0) {
        entry->label_depth = (label_depth > 3) ? 3 : label_depth;
        memcpy(entry->label_stack, label_stack,
               entry->label_depth * sizeof(mpls_label_t));
    } else {
        entry->label_depth = 0;
    }

    /* Call HW layer to install route */
    ret = hw_l3_route_add_vrf(vrf_id, af, prefix, prefixlen,
                              nh_list, nh_count,
                              label_stack, label_depth);
    if (ret < 0) {
        if (af == AF_INET6)
            HSL_LOG_ERR("Failed to add VRF route %pI6/%u to VRF %u: ret=%d",
                        prefix, prefixlen, vrf_id, ret);
        else
            HSL_LOG_ERR("Failed to add VRF route %pI4/%u to VRF %u: ret=%d",
                        prefix, prefixlen, vrf_id, ret);

        /* Cleanup on error */
        struct hsl_nexthop *cleanup_nh, *tmp;
        list_for_each_entry_safe(cleanup_nh, tmp, &entry->nh_list, node) {
            list_del(&cleanup_nh->node);
            kfree(cleanup_nh);
        }
        kfree(entry);
        mutex_unlock(&hsl_vrf_mutex);
        return ret;
    }

    /* Add to software cache */
    list_add_tail(&entry->node, &hsl_vrf_route_list);

    if (af == AF_INET6)
        HSL_LOG_DBG("VRF route added: vrf=%u prefix=%pI6/%u nh_count=%u labels=%u",
                    vrf_id, prefix, prefixlen, nh_count, label_depth);
    else
        HSL_LOG_DBG("VRF route added: vrf=%u prefix=%pI4/%u nh_count=%u labels=%u",
                    vrf_id, prefix, prefixlen, nh_count, label_depth);

    mutex_unlock(&hsl_vrf_mutex);
    return 0;
}

/**
 * Delete a route from a specific VRF
 */
int hsl_route_del_vrf(uint32_t vrf_id, uint8_t af,
                      const void *prefix, uint8_t prefixlen)
{
    struct hsl_vrf_route_entry *entry, *tmp;
    int ret;

    if (!prefix) {
        return -EINVAL;
    }

    mutex_lock(&hsl_vrf_mutex);

    /* Find and remove from software cache */
    list_for_each_entry_safe(entry, tmp, &hsl_vrf_route_list, node) {
        if (entry->vrf_id == vrf_id &&
            entry->af == af &&
            hsl_vrf_prefix_eq(af, &entry->prefix, prefix) &&
            entry->prefixlen == prefixlen) {

            /* Remove nexthops */
            struct hsl_nexthop *nh, *nh_tmp;
            list_for_each_entry_safe(nh, nh_tmp, &entry->nh_list, node) {
                list_del(&nh->node);
                kfree(nh);
            }

            list_del(&entry->node);
            kfree(entry);
            break;
        }
    }

    /* Call HW layer to delete route */
    ret = hw_l3_route_del_vrf(vrf_id, af, prefix, prefixlen);
    if (ret < 0) {
        if (af == AF_INET6)
            HSL_LOG_ERR("Failed to delete VRF route %pI6/%u from VRF %u: ret=%d",
                        prefix, prefixlen, vrf_id, ret);
        else
            HSL_LOG_ERR("Failed to delete VRF route %pI4/%u from VRF %u: ret=%d",
                        prefix, prefixlen, vrf_id, ret);
        mutex_unlock(&hsl_vrf_mutex);
        return ret;
    }

    if (af == AF_INET6)
        HSL_LOG_DBG("VRF route deleted: vrf=%u prefix=%pI6/%u",
                    vrf_id, prefix, prefixlen);
    else
        HSL_LOG_DBG("VRF route deleted: vrf=%u prefix=%pI4/%u",
                    vrf_id, prefix, prefixlen);

    mutex_unlock(&hsl_vrf_mutex);
    return 0;
}

/**
 * Query route information in a VRF
 */
int hsl_route_query_vrf(uint32_t vrf_id, uint8_t af,
                        const void *prefix, uint8_t prefixlen,
                        struct hsl_nexthop *nh_list, uint8_t nh_capacity,
                        mpls_label_t *label_stack, uint8_t *label_depth)
{
    struct hsl_vrf_route_entry *entry;
    int nh_count = 0;

    if (!prefix || !nh_list) {
        return -EINVAL;
    }

    mutex_lock(&hsl_vrf_mutex);

    /* Search for route in software cache */
    list_for_each_entry(entry, &hsl_vrf_route_list, node) {
        if (entry->vrf_id == vrf_id &&
            entry->af == af &&
            hsl_vrf_prefix_eq(af, &entry->prefix, prefix) &&
            entry->prefixlen == prefixlen) {

            /* Copy nexthops */
            struct hsl_nexthop *nh;
            list_for_each_entry(nh, &entry->nh_list, node) {
                if (nh_count >= nh_capacity) {
                    HSL_LOG_WARN("Nexthop buffer too small (capacity=%u)", nh_capacity);
                    break;
                }
                nh_list[nh_count++] = *nh;
            }

            /* Copy label stack if requested */
            if (label_stack && label_depth) {
                *label_depth = entry->label_depth;
                memcpy(label_stack, entry->label_stack,
                       entry->label_depth * sizeof(mpls_label_t));
            }

            mutex_unlock(&hsl_vrf_mutex);
            return nh_count;
        }
    }

    mutex_unlock(&hsl_vrf_mutex);
    return -ENOENT; /* Route not found */
}

/**
 * Get statistics for a VRF
 */
int hsl_vrf_get_stats(uint32_t vrf_id, uint32_t *route_count,
                      uint64_t *byte_count, uint64_t *packet_count)
{
    struct hsl_vrf_route_entry *entry;
    uint32_t count = 0;

    mutex_lock(&hsl_vrf_mutex);

    /* Count routes in this VRF */
    list_for_each_entry(entry, &hsl_vrf_route_list, node) {
        if (entry->vrf_id == vrf_id) {
            count++;
        }
    }

    if (route_count)
        *route_count = count;

    /* TODO: Query hardware for byte/packet counters */
    if (byte_count)
        *byte_count = 0;
    if (packet_count)
        *packet_count = 0;

    mutex_unlock(&hsl_vrf_mutex);
    return 0;
}

/**
 * Decode a HAL message for the HAL_MODULE_ROUTE module and dispatch it.
 * Called by hsl_rt_msg() for HAL_MSG_RT_VRF_ADD / HAL_MSG_RT_VRF_DEL.
 *
 * Wire fields (prefix/gate/labels) are in network byte order, matching
 * the existing hw_mpls.c convention; they are passed through unchanged.
 */
int hsl_rt_vrf_msg(hsl_msg_t *msg)
{
    struct ctc_hal_msg_route_vrf *rt;
    struct hsl_nexthop nhs[CTC_HAL_MSG_MAX_NH_VRF] = {0};
    mpls_label_t label_stack[3] = {0};
    uint8_t label_depth = 0;
    int nh_count;
    int i;
    int ret;

    if (!msg)
        return -EINVAL;

    rt = HSL_MSG_DATA(msg);
    if (!rt)
        return -EINVAL;

    /* Convert nexthops. gate_af is per-nexthop: AF_INET6 routes can egress
     * through an AF_INET gateway (v6 payload over IPv4 core) or vice versa,
     * so the gateway family is not implied by rt->af. gate stays network
     * byte order. */
    nh_count = rt->nh_count;
    if (nh_count > CTC_HAL_MSG_MAX_NH_VRF)
        nh_count = CTC_HAL_MSG_MAX_NH_VRF;
    for (i = 0; i < nh_count; i++) {
        nhs[i].ifindex   = rt->nh[i].ifindex;
        nhs[i].is_mpls   = rt->nh[i].is_mpls;
        nhs[i].out_label = rt->nh[i].out_label;
        if (rt->nh[i].gate_af == AF_INET6) {
            memcpy(&nhs[i].gate.ipv6, rt->nh[i].gate.ipv6,
                   sizeof(nhs[i].gate.ipv6));
            nhs[i].family = AF_INET6;
        } else {
            nhs[i].gate.ipv4.s_addr = rt->nh[i].gate.ipv4;
            nhs[i].family = AF_INET; /* gate_af 0/AF_INET 均按 v4 承载 */
        }
    }

    /* Convert label stack (stays network byte order) */
    if (rt->label_stack_depth > 0) {
        label_depth = rt->label_stack_depth;
        if (label_depth > 3)
            label_depth = 3;
        for (i = 0; i < label_depth; i++)
            label_stack[i] = rt->label_stack[i];
    }

    if (msg->cmd == HAL_MSG_RT_VRF_ADD) {
        ret = hsl_route_add_vrf(rt->vrf_id, rt->af,
                                (const void *)&rt->prefix,
                                rt->prefixlen,
                                nhs, (uint8_t)nh_count,
                                label_stack, label_depth);
    } else {
        ret = hsl_route_del_vrf(rt->vrf_id, rt->af,
                                (const void *)&rt->prefix,
                                rt->prefixlen);
    }

    return ret;
}

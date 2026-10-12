/*
 * hsl_mpls_vrf.c -- HSL MPLS LSP / FEC 管理（VRF / L3VPN 感知）
 *
 * 本文件实现 VRF 上下文中的 MPLS ILM（Incoming Label Map）与
 * FEC（Forwarding Equivalence Class）管理：
 *  - 软件缓存维护 LSP/FEC 表项（调试、查询、一致性校验用）
 *  - 通过 hw_mpls_* VRF API 前置声明下发到 hw 层（Centec SDK）
 *  - hsl_mpls_vrf_msg() 负责解包 HAL_MODULE_MPLS 模块的 VRF LSP 消息
 *
 * 标签字节序约定：与现有 hw_mpls.c / hsl_mpls.c 一致，均为网络字节序。
 */

#include "hsl_mpls_vrf.h"
#include "hsl_nh.h"
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/list.h>

/* ------------------------------------------------------------------
 * hw_mpls VRF API 前置声明（hw 层无 .h 头文件，与 hsl_mpls.c 模式一致）
 * ------------------------------------------------------------------ */
extern int hw_mpls_lsp_add_vrf(uint32_t vrf_id, uint32_t in_label,
			       uint32_t out_label, uint8_t op_type,
			       const struct hsl_nexthop *nh_list,
			       uint8_t nh_count);
extern int hw_mpls_lsp_del_vrf(uint32_t vrf_id, uint32_t in_label);
extern int hw_mpls_fec_bind_vrf(uint32_t vrf_id,
				const struct in_addr *prefix,
				uint8_t prefixlen, uint32_t label);
extern int hw_mpls_fec_unbind_vrf(uint32_t vrf_id,
				  const struct in_addr *prefix,
				  uint8_t prefixlen);

/* Mutex for MPLS VRF operations */
static DEFINE_MUTEX(hsl_mpls_vrf_mutex);

/* 非法标签（网络字节序）：0 保留 */
#define HSL_MPLS_LABEL_INVALID  0

/**
 * Software cache for MPLS LSP entries
 */
struct hsl_mpls_lsp_entry {
    uint32_t vrf_id;              /* VRF identifier */
    mpls_label_t in_label;        /* incoming label (network byte order) */
    mpls_label_t out_label;       /* outgoing label (network byte order) */
    uint8_t op_type;              /* HSL_MPLS_OP_* */
    struct list_head nh_list;     /* list of struct hsl_nexthop */
    struct list_head node;        /* global LSP list node */
};

/**
 * Software cache for FEC-to-label bindings
 */
struct hsl_mpls_fec_entry {
    uint32_t vrf_id;              /* VRF identifier */
    struct in_addr prefix;        /* destination prefix */
    uint8_t prefixlen;            /* prefix length */
    mpls_label_t label;           /* bound label (network byte order) */
    struct list_head node;        /* global FEC list node */
};

/* Global lists */
static LIST_HEAD(hsl_mpls_lsp_list);
static LIST_HEAD(hsl_mpls_fec_list);

/**
 * Add MPLS LSP entry for a specific VRF (ILM)
 */
int hsl_mpls_lsp_add_vrf(uint32_t vrf_id, mpls_label_t in_label,
                         mpls_label_t out_label, uint8_t op_type,
                         const struct hsl_nexthop *nh_list, uint8_t nh_count)
{
    struct hsl_mpls_lsp_entry *entry;
    struct hsl_nexthop *nh;
    int ret;
    int i;

    if (!nh_list || nh_count == 0) {
        HSL_LOG_ERR("Invalid parameters for VRF LSP add");
        return -EINVAL;
    }

    if (in_label == HSL_MPLS_LABEL_INVALID) {
        HSL_LOG_ERR("Invalid MPLS label %u", in_label);
        return -EINVAL;
    }

    mutex_lock(&hsl_mpls_vrf_mutex);

    /* 幂等 replace：同 (vrf_id, in_label) 已存在时先删旧（软件缓存 + hw），
     * 再按新信息重建 —— 与 hw 层 hw_mpls_lsp_add_vrf 的 replace 语义一致。
     * zebra 对每-VRF 出口标签 LSP 的 INSTALL/UPDATE 都以 VRF_ADD 到达且为
     * 全量语义（无增量差异），此处必须容忍重复，否则重复 ADD 会被 -EEXIST
     * 挡在 hw 之外。
     *
     * 注意：不能调用 hsl_mpls_lsp_lookup() —— 它会对 hsl_mpls_vrf_mutex
     * 再次加锁，而本函数已持锁（内核 mutex 非递归），会自死锁。此前该路径
     * 从无发送者、从未被真实触发，这个死锁一直潜伏到发送者接入为止。 */
    {
        struct hsl_mpls_lsp_entry *old, *tmp;
        struct hsl_nexthop *nh_old, *nh_tmp;

        list_for_each_entry_safe(old, tmp, &hsl_mpls_lsp_list, node) {
            if (old->vrf_id != vrf_id || old->in_label != in_label)
                continue;
            list_for_each_entry_safe(nh_old, nh_tmp, &old->nh_list, node) {
                list_del(&nh_old->node);
                kfree(nh_old);
            }
            list_del(&old->node);
            kfree(old);
            hw_mpls_lsp_del_vrf(vrf_id, in_label);
            HSL_LOG_DBG("LSP replaced: vrf=%u in_label=%u", vrf_id, in_label);
            break;
        }
    }

    /* Allocate software cache entry */
    entry = kzalloc(sizeof(*entry), GFP_KERNEL);
    if (!entry) {
        mutex_unlock(&hsl_mpls_vrf_mutex);
        return -ENOMEM;
    }

    entry->vrf_id = vrf_id;
    entry->in_label = in_label;
    entry->out_label = out_label;
    entry->op_type = op_type;
    INIT_LIST_HEAD(&entry->nh_list);

    /* Copy nexthops */
    for (i = 0; i < nh_count; i++) {
        nh = kzalloc(sizeof(*nh), GFP_KERNEL);
        if (!nh) {
            struct hsl_nexthop *tmp;
            list_for_each_entry_safe(nh, tmp, &entry->nh_list, node) {
                list_del(&nh->node);
                kfree(nh);
            }
            kfree(entry);
            mutex_unlock(&hsl_mpls_vrf_mutex);
            return -ENOMEM;
        }
        *nh = nh_list[i];
        list_add_tail(&nh->node, &entry->nh_list);
    }

    /* Call HW layer to install LSP */
    ret = hw_mpls_lsp_add_vrf(vrf_id, in_label, out_label, op_type,
                              nh_list, nh_count);
    if (ret < 0) {
        HSL_LOG_ERR("Failed to add LSP: vrf=%u in_label=%u out_label=%u: ret=%d",
                    vrf_id, in_label, out_label, ret);
        struct hsl_nexthop *tmp;
        list_for_each_entry_safe(nh, tmp, &entry->nh_list, node) {
            list_del(&nh->node);
            kfree(nh);
        }
        kfree(entry);
        mutex_unlock(&hsl_mpls_vrf_mutex);
        return ret;
    }

    /* Add to software cache */
    list_add_tail(&entry->node, &hsl_mpls_lsp_list);

    HSL_LOG_DBG("LSP added: vrf=%u in_label=%u out_label=%u op=%u",
                vrf_id, in_label, out_label, op_type);

    mutex_unlock(&hsl_mpls_vrf_mutex);
    return 0;
}

/**
 * Delete MPLS LSP entry from a VRF
 */
int hsl_mpls_lsp_del_vrf(uint32_t vrf_id, mpls_label_t in_label)
{
    struct hsl_mpls_lsp_entry *entry, *tmp;
    struct hsl_nexthop *nh, *nh_tmp;
    int ret;

    mutex_lock(&hsl_mpls_vrf_mutex);

    /* Find and remove from software cache */
    list_for_each_entry_safe(entry, tmp, &hsl_mpls_lsp_list, node) {
        if (entry->vrf_id == vrf_id && entry->in_label == in_label) {
            list_for_each_entry_safe(nh, nh_tmp, &entry->nh_list, node) {
                list_del(&nh->node);
                kfree(nh);
            }
            list_del(&entry->node);
            kfree(entry);
            break;
        }
    }

    /* Call HW layer to delete LSP */
    ret = hw_mpls_lsp_del_vrf(vrf_id, in_label);
    if (ret < 0) {
        HSL_LOG_ERR("Failed to delete LSP: vrf=%u in_label=%u: ret=%d",
                    vrf_id, in_label, ret);
        mutex_unlock(&hsl_mpls_vrf_mutex);
        return ret;
    }

    HSL_LOG_DBG("LSP deleted: vrf=%u in_label=%u", vrf_id, in_label);

    mutex_unlock(&hsl_mpls_vrf_mutex);
    return 0;
}

/**
 * Check if an MPLS LSP entry exists
 */
int hsl_mpls_lsp_lookup(uint32_t vrf_id, mpls_label_t in_label)
{
    struct hsl_mpls_lsp_entry *entry;
    int found = 0;

    mutex_lock(&hsl_mpls_vrf_mutex);
    list_for_each_entry(entry, &hsl_mpls_lsp_list, node) {
        if (entry->vrf_id == vrf_id && entry->in_label == in_label) {
            found = 1;
            break;
        }
    }
    mutex_unlock(&hsl_mpls_vrf_mutex);

    return found;
}

/**
 * Query MPLS LSP information
 */
int hsl_mpls_lsp_query(uint32_t vrf_id, mpls_label_t in_label,
                       mpls_label_t *out_label, uint8_t *op_type,
                       struct hsl_nexthop *nh_list, uint8_t nh_capacity)
{
    struct hsl_mpls_lsp_entry *entry;
    struct hsl_nexthop *nh;
    int nh_count = 0;

    if (!nh_list) {
        return -EINVAL;
    }

    mutex_lock(&hsl_mpls_vrf_mutex);

    list_for_each_entry(entry, &hsl_mpls_lsp_list, node) {
        if (entry->vrf_id == vrf_id && entry->in_label == in_label) {
            if (out_label)
                *out_label = entry->out_label;
            if (op_type)
                *op_type = entry->op_type;

            list_for_each_entry(nh, &entry->nh_list, node) {
                if (nh_count >= nh_capacity) {
                    HSL_LOG_WARN("Nexthop buffer too small (capacity=%u)",
                                 nh_capacity);
                    break;
                }
                nh_list[nh_count++] = *nh;
            }

            mutex_unlock(&hsl_mpls_vrf_mutex);
            return nh_count;
        }
    }

    mutex_unlock(&hsl_mpls_vrf_mutex);
    return -ENOENT;
}

/**
 * Bind MPLS label to FEC for VPN routes
 */
int hsl_mpls_fec_bind_vrf(uint32_t vrf_id, const struct in_addr *prefix,
                          uint8_t prefixlen, mpls_label_t label)
{
    struct hsl_mpls_fec_entry *entry;
    int ret;

    if (!prefix || label == HSL_MPLS_LABEL_INVALID) {
        return -EINVAL;
    }

    mutex_lock(&hsl_mpls_vrf_mutex);

    /* Check for duplicate binding */
    list_for_each_entry(entry, &hsl_mpls_fec_list, node) {
        if (entry->vrf_id == vrf_id &&
            entry->prefix.s_addr == prefix->s_addr &&
            entry->prefixlen == prefixlen) {
            HSL_LOG_WARN("FEC already bound: vrf=%u prefix=%pI4/%u",
                         vrf_id, prefix, prefixlen);
            mutex_unlock(&hsl_mpls_vrf_mutex);
            return -EEXIST;
        }
    }

    /* Allocate cache entry */
    entry = kzalloc(sizeof(*entry), GFP_KERNEL);
    if (!entry) {
        mutex_unlock(&hsl_mpls_vrf_mutex);
        return -ENOMEM;
    }

    entry->vrf_id = vrf_id;
    entry->prefix = *prefix;
    entry->prefixlen = prefixlen;
    entry->label = label;

    /* Call HW layer to bind FEC to label */
    ret = hw_mpls_fec_bind_vrf(vrf_id, prefix, prefixlen, label);
    if (ret < 0) {
        HSL_LOG_ERR("Failed to bind FEC: vrf=%u prefix=%pI4/%u label=%u: ret=%d",
                    vrf_id, prefix, prefixlen, label, ret);
        kfree(entry);
        mutex_unlock(&hsl_mpls_vrf_mutex);
        return ret;
    }

    list_add_tail(&entry->node, &hsl_mpls_fec_list);

    HSL_LOG_DBG("FEC bound: vrf=%u prefix=%pI4/%u label=%u",
                vrf_id, prefix, prefixlen, label);

    mutex_unlock(&hsl_mpls_vrf_mutex);
    return 0;
}

/**
 * Unbind MPLS label from FEC
 */
int hsl_mpls_fec_unbind_vrf(uint32_t vrf_id, const struct in_addr *prefix,
                            uint8_t prefixlen)
{
    struct hsl_mpls_fec_entry *entry, *tmp;
    int ret;

    if (!prefix) {
        return -EINVAL;
    }

    mutex_lock(&hsl_mpls_vrf_mutex);

    /* Remove from software cache */
    list_for_each_entry_safe(entry, tmp, &hsl_mpls_fec_list, node) {
        if (entry->vrf_id == vrf_id &&
            entry->prefix.s_addr == prefix->s_addr &&
            entry->prefixlen == prefixlen) {
            list_del(&entry->node);
            kfree(entry);
            break;
        }
    }

    ret = hw_mpls_fec_unbind_vrf(vrf_id, prefix, prefixlen);
    if (ret < 0) {
        HSL_LOG_ERR("Failed to unbind FEC: vrf=%u prefix=%pI4/%u: ret=%d",
                    vrf_id, prefix, prefixlen, ret);
        mutex_unlock(&hsl_mpls_vrf_mutex);
        return ret;
    }

    HSL_LOG_DBG("FEC unbound: vrf=%u prefix=%pI4/%u",
                vrf_id, prefix, prefixlen);

    mutex_unlock(&hsl_mpls_vrf_mutex);
    return 0;
}

/**
 * Query FEC-to-label binding
 */
int hsl_mpls_fec_query(uint32_t vrf_id, const struct in_addr *prefix,
                       uint8_t prefixlen, mpls_label_t *label)
{
    struct hsl_mpls_fec_entry *entry;
    int ret = -ENOENT;

    if (!prefix || !label) {
        return -EINVAL;
    }

    mutex_lock(&hsl_mpls_vrf_mutex);
    list_for_each_entry(entry, &hsl_mpls_fec_list, node) {
        if (entry->vrf_id == vrf_id &&
            entry->prefix.s_addr == prefix->s_addr &&
            entry->prefixlen == prefixlen) {
            *label = entry->label;
            ret = 0;
            break;
        }
    }
    mutex_unlock(&hsl_mpls_vrf_mutex);

    return ret;
}

/**
 * Flush all MPLS LSP entries from a VRF
 */
int hsl_mpls_lsp_flush_vrf(uint32_t vrf_id)
{
    struct hsl_mpls_lsp_entry *entry, *tmp;
    struct hsl_mpls_fec_entry *fec, *fec_tmp;
    struct hsl_nexthop *nh, *nh_tmp;
    int lsp_count = 0;
    int fec_count = 0;

    mutex_lock(&hsl_mpls_vrf_mutex);

    /* Flush LSPs */
    list_for_each_entry_safe(entry, tmp, &hsl_mpls_lsp_list, node) {
        if (entry->vrf_id == vrf_id) {
            hw_mpls_lsp_del_vrf(entry->vrf_id, entry->in_label);
            list_for_each_entry_safe(nh, nh_tmp, &entry->nh_list, node) {
                list_del(&nh->node);
                kfree(nh);
            }
            list_del(&entry->node);
            kfree(entry);
            lsp_count++;
        }
    }

    /* Flush FEC bindings */
    list_for_each_entry_safe(fec, fec_tmp, &hsl_mpls_fec_list, node) {
        if (fec->vrf_id == vrf_id) {
            hw_mpls_fec_unbind_vrf(fec->vrf_id, &fec->prefix, fec->prefixlen);
            list_del(&fec->node);
            kfree(fec);
            fec_count++;
        }
    }

    HSL_LOG_INFO("Flushed %d LSPs and %d FECs from VRF %u",
                 lsp_count, fec_count, vrf_id);

    mutex_unlock(&hsl_mpls_vrf_mutex);
    return 0;
}

/**
 * Get MPLS statistics for a VRF
 */
int hsl_mpls_get_stats(uint32_t vrf_id, uint32_t *lsp_count,
                       uint32_t *fec_count, uint64_t *byte_count,
                       uint64_t *packet_count)
{
    struct hsl_mpls_lsp_entry *entry;
    struct hsl_mpls_fec_entry *fec;
    uint32_t lsp = 0, fec_cnt = 0;

    mutex_lock(&hsl_mpls_vrf_mutex);

    list_for_each_entry(entry, &hsl_mpls_lsp_list, node) {
        if (entry->vrf_id == vrf_id)
            lsp++;
    }
    list_for_each_entry(fec, &hsl_mpls_fec_list, node) {
        if (fec->vrf_id == vrf_id)
            fec_cnt++;
    }

    if (lsp_count)
        *lsp_count = lsp;
    if (fec_count)
        *fec_count = fec_cnt;

    /* TODO: Query hardware for byte/packet counters */
    if (byte_count)
        *byte_count = 0;
    if (packet_count)
        *packet_count = 0;

    mutex_unlock(&hsl_mpls_vrf_mutex);
    return 0;
}

/**
 * Decode a HAL message for the HAL_MODULE_MPLS module and dispatch it.
 * Called by hsl_mpls_msg() for HAL_MSG_MPLS_LSP_VRF_ADD / _DEL.
 */
int hsl_mpls_vrf_msg(hsl_msg_t *msg)
{
    struct ctc_hal_msg_mpls_lsp_vrf *lsp;
    struct hsl_nexthop nhs[CTC_HAL_MSG_MAX_LSP_NH_VRF] = {0};
    int nh_count;
    int i;
    int ret;

    if (!msg)
        return -EINVAL;

    lsp = HSL_MSG_DATA(msg);
    if (!lsp)
        return -EINVAL;

    /* Convert nexthops (gate/labels stay network byte order) */
    nh_count = lsp->nh_count;
    if (nh_count > CTC_HAL_MSG_MAX_LSP_NH_VRF)
        nh_count = CTC_HAL_MSG_MAX_LSP_NH_VRF;
    for (i = 0; i < nh_count; i++) {
        nhs[i].ifindex   = lsp->nh[i].ifindex;
        nhs[i].gate.ipv4.s_addr = lsp->nh[i].gate;
        nhs[i].family    = AF_INET;   /* per-VRF 标签 LSP 出路径恒 v4 gate */
        nhs[i].is_mpls   = lsp->nh[i].is_mpls;
        nhs[i].out_label = lsp->nh[i].out_label;
    }

    if (msg->cmd == HAL_MSG_MPLS_LSP_VRF_ADD) {
        ret = hsl_mpls_lsp_add_vrf(lsp->vrf_id, lsp->in_label,
                                   lsp->out_label, lsp->op_type,
                                   nhs, (uint8_t)nh_count);
    } else {
        ret = hsl_mpls_lsp_del_vrf(lsp->vrf_id, lsp->in_label);
    }

    return ret;
}

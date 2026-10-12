/*
   Copyright (C) 2017 Shenzhen Hored Technology Inc.
*/

#include "hsl.h"
#include "hsl_avl.h"
#include "hsl_rt.h"
#include "hsl_l3.h"
#include "hsl_module.h"
#include "hal_msg.h"
#include <def_route.h>
#include "hw.h"
#include "hw_rt.h"
#include "hw_l3.h"
#include "hsl_rt_vrf.h"

#include <linux/netdevice.h>
#include <linux/string.h>
#include <net/net_namespace.h>

/* lo 接口（内核 loopback）直连路由的 TOCPU 下发/删除：HSL 接口
 * 表中无 lo，路由不进 nh 表，直接以 CONNECT+TOCPU 表项写入芯片。 */
extern int hw_add_loopback_rt(hsl_prefix_t *prefix);
extern int hw_del_loopback_rt(hsl_prefix_t *prefix);

#if defined(HAVE_L3)

struct hsl_route_table *g_hsl_rt = NULL;

struct hsl_route_node *hsl_get_rt_by_prefix(hsl_prefix_t *prefix)
{
    struct hsl_route_node *rn;

    rn = hsl_route_node_get(g_hsl_rt, prefix);

    return rn;
}

struct hsl_route_node *hsl_lookup_rt_by_prefix(hsl_prefix_t *prefix)
{
    return hsl_route_node_lookup(g_hsl_rt, prefix);
}

static void hsl_rt_free(struct hsl_route_node *rn)
{
    hsl_nh_entry_list_t *nh_list, *next;

    nh_list = rn->info;
    while (nh_list) {
        next = nh_list->next;
        hsl_free(nh_list, HSL_MEM_GENERAL);
        nh_list = next;
    }
    rn->info = NULL;

    hsl_route_unlock_node(rn);
}


int hsl_add_rt(hsl_prefix_t *prefix, hsl_prefix_t *nexthop, u_int32_t ifindex,
		u_int32_t is_mpls, u_int32_t mpls_label)
{
    struct hsl_route_node *rn;
    hsl_nh_entry_t *nh;

    rn = hsl_get_rt_by_prefix( prefix);
    if(rn == NULL)
    {
        return RUNOS_MEMORY;
    }
    
	/*���ö�̬ѧϰ·������*/
    nh_lock();
	nh = hsl_rt_get_nh(ifindex, nexthop, 1);
    if(nh == NULL)
    {
        nh_unlock();
        hsl_route_unlock_node(rn);
        return RUNOS_MEMORY;
    }

    /* MPLS: 标签状态按 FEC 记录在 rn 上。nh 条目按
     * (ifindex, nexthop) 共享，写在 nh 上会被其他路由的消息
     * 覆盖（实测全 17 / 全 IP 污染），这里按 FEC 独立记录。 */
    rn->is_mpls = is_mpls;
    rn->mpls_label = mpls_label;

    hsl_add_rt_to_nh(rn, nh);
    hsl_add_nh_to_rt(rn, nh);
    hsl_l3_add_prefix(rn, nh);
    nh_unlock();
    hsl_route_unlock_node(rn);

    return RUNOS_OK;
}

int hsl_del_rt(hsl_prefix_t *prefix, hsl_prefix_t *nexthop, u_int32_t ifindex)
{
    struct hsl_route_node *rn;
    hsl_nh_entry_t *nh;

    rn = hsl_lookup_rt_by_prefix( prefix);
    if(rn == NULL)
    {
        return RUNOS_NOT_FOUND;
    }

    nh_lock();
    nh = hsl_rt_lookup_nh__(ifindex, nexthop);
    if(nh == NULL)
    {
        nh_unlock();
        hsl_route_unlock_node(rn);
        hsl_rt_free(rn);
        return RUNOS_NOT_FOUND;
    }

    hsl_del_rt_from_nh(rn, nh);
    hsl_del_nh_from_rt(rn, nh);
    hsl_l3_del_prefix(rn, nh);
    nh_unlock();
    hsl_route_unlock_node(rn);
    /* free rt here */
    hsl_rt_free(rn);

    return RUNOS_OK;
}

/* for test */
void hsl_show_rt(void)
{
    struct hsl_route_node *rn;
    hsl_prefix_info_t  *info;
    hsl_nh_entry_list_t *nh_list;
    
    printk(KERN_ERR "IP          MAC             IF\n");
    printk(KERN_ERR "----------------------------------------\n");
    
    for(rn=hsl_route_top(g_hsl_rt); rn; rn=hsl_route_next(rn))
    {
        printk(KERN_ERR "0x%08X  ", rn->p.u.prefix4);
        
        if (!(info = rn->info))
        {
            printk("\n");
            continue;
        }

        for(nh_list  = info->nh_list; nh_list; nh_list = nh_list->next)
        {
            printk("%02x-%02x-%02x-%02x-%02x-%02x  ", 
                nh_list->nh->mac[0], nh_list->nh->mac[1], nh_list->nh->mac[2], 
                nh_list->nh->mac[3], nh_list->nh->mac[4], nh_list->nh->mac[5]);
            printk("0x%08X\n", nh_list->nh->ifindex);
            break;  /* only loop one time */
        }
    }
    
    printk(KERN_ERR "----------------------------------------\n");
}

int hsl_add_connected_rt(hsl_prefix_t *prefix, struct hsl_if_s *ifp)
{
    struct hsl_route_node *rn;
    hsl_nh_entry_t *nh;
    hsl_prefix_t tmp_prefix;

#if 0
#ifdef HAVE_IPV6
	/* For IPV6_MAX_PREFIXLEN, add to host table since for easyrider box,
	   prefix table do not allow IPV6_MAX_PREFIXLEN route to be added. */
	if (prefix->family == AF_INET6 && prefix->prefixlen == IPV6_MAX_PREFIXLEN)
	{
    	return hw_rt_add_l3host(prefix, ifp);
	}
#endif
#endif

    tmp_prefix = *prefix;
    hsl_apply_mask(&tmp_prefix);
    rn = hsl_get_rt_by_prefix( &tmp_prefix);
    if(rn == NULL)
    {
        return RUNOS_ERR;
    }

    nh_lock();
    nh = hsl_rt_get_nh(ifp->ifindex, prefix, 0);
    if(nh != NULL)
    {
        hsl_add_rt_to_nh(rn, nh);
        hsl_add_nh_to_rt(rn, nh);
    }
    nh_unlock();
    hsl_route_unlock_node(rn);

    hw_add_connected_rt(prefix, ifp);

    return RUNOS_OK;
}

int hsl_del_connected_rt(hsl_prefix_t *prefix, struct hsl_if_s *ifp)
{
    struct hsl_route_node *rn;
    hsl_nh_entry_t *nh;
    hsl_prefix_t tmp_prefix;

#if 0
#ifdef HAVE_IPV6
	/* For IPV6_MAX_PREFIXLEN, add to host table since for easyrider box,
	   prefix table do not allow IPV6_MAX_PREFIXLEN route to be added. */
	if (prefix->family == AF_INET6 && prefix->prefixlen == IPV6_MAX_PREFIXLEN)
	{
		return hw_rt_del_l3host( prefix, ifp);
	}
#endif
#endif	
    tmp_prefix = *prefix;
    hsl_apply_mask(&tmp_prefix);
    rn = hsl_route_node_lookup(g_hsl_rt, &tmp_prefix);
    if(rn == NULL)
    {
        return RUNOS_ERR;
    }

    nh_lock();
    nh = hsl_rt_lookup_nh__(ifp->ifindex, prefix);
    if(nh != NULL)
    {
        hsl_del_rt_from_nh(rn, nh);
        hsl_del_nh_from_rt(rn, nh);
		hw_del_connected_rt(prefix,ifp);
    }
    nh_unlock();
    hsl_route_unlock_node(rn);
    /* free rt here */
    hsl_rt_free(rn);
	
    return RUNOS_OK;
}

int hsl_add_rt_to_nh(struct hsl_route_node *rn, struct hsl_nh_entry_s *nh)
{
    if(nh->prefix_tree)
    {
        hsl_avl_insert(nh->prefix_tree, rn);
        nh->tree_count++;
    }
    else
    {
        return RUNOS_ERR;
    }

    return RUNOS_OK;
}

int hsl_del_rt_from_nh(struct hsl_route_node *rn, struct hsl_nh_entry_s *nh)
{
    if(nh->prefix_tree)
    {
        hsl_avl_delete(nh->prefix_tree, rn);
        nh->tree_count--;
    }
    else
    {
        return RUNOS_ERR;
    }

    return RUNOS_OK;
}

int hsl_add_nh_to_rt(struct hsl_route_node *rn, struct hsl_nh_entry_s *nh)
{
    hsl_nh_entry_list_t *nh_list;

    nh_list = rn->info;
    while (nh_list) {
        if (nh_list->nh == nh) {
            return RUNOS_EXISTS;
        }
        nh_list = nh_list->next;
    }

    nh_list = hsl_malloc(sizeof(hsl_nh_entry_list_t), HSL_MEM_GENERAL);
    if(nh_list == NULL)
    {
        return RUNOS_MEMORY;
    }
    nh_list->nh = nh;
    nh_list->next = rn->info;
    rn->info = nh_list;

    return RUNOS_OK;
}

void hsl_del_nh_from_rt(struct hsl_route_node *rn, struct hsl_nh_entry_s *nh)
{
    hsl_prefix_info_t *info;
    hsl_nh_entry_list_t *nh_list, *prev = NULL;

    if (rn == NULL || nh == NULL) {
        return;
    }

    info = rn->info;
    if (info == NULL) {
        return;
    }

    for (nh_list = info->nh_list; nh_list; nh_list = nh_list->next) {
        if (nh_list->nh == nh) {
            if (prev == NULL) {
                info->nh_list = nh_list->next;
            } else {
                prev->next = nh_list->next;
            }

            hsl_free(nh_list, HSL_MEM_GENERAL);

            if (info->nh_num > 0) {
                info->nh_num--;
            }

            return;
        }
        prev = nh_list;
    }

}

/* 判断内核 ifindex 是否对应 lo 接口。HSL 接口表只同步
 * 物理口/vlanif（vlanif 为 0x06000000|vid 编码，查询必然为空），
 * 不含 lo；lo 的直连路由须按本机地址语义以 TOCPU 表项下发。
 *
 * 双重确认：flags 带 IFF_LOOPBACK 且接口名为 "lo"。仅凭
 * IFF_LOOPBACK 会把 dummy/tap 等其他 loopback 类虚拟设备误判
 * 为 lo，导致其直连路由被 TOCPU 化且不进 HSL 路由表，破坏
 * 正常转发。 */
static int hsl_rt_ifindex_is_loopback(u_int32_t ifindex)
{
    struct net_device *dev;
    int ret = 0;

    rcu_read_lock();
    dev = dev_get_by_index_rcu(&init_net, ifindex);
    if (dev && (dev->flags & IFF_LOOPBACK) &&
        strcmp(dev->name, "lo") == 0)
        ret = 1;
    rcu_read_unlock();

    return ret;
}


/* 查找 HSL 接口结构（根据 ifindex） */
static struct hsl_if_s *hsl_if_lookup_by_index(uint32_t ifindex)
{
    /* 假设 HSL 有接口哈希表，可以通过 ifindex 查找。
     * 具体实现可能为：return hsl_if_lookup(ifindex); 
     * 请根据实际 HSL 接口管理 API 调整。 */
    extern struct hsl_if_s *hsl_if_lookup(uint32_t ifindex); /* 可能定义在 hsl_if.h */
    return hsl_if_lookup(ifindex);
}

/* 处理接口 VRF 绑定消息 */
static int hsl_intf_vrf_update(struct ctc_hal_msg_intf_vrf *msg)
{
    struct hsl_if_s *ifp;
    int ret = 0;

    ifp = hsl_if_lookup_by_index(msg->ifindex);
    if (!ifp && !msg->is_vrf_dev) {
        printk(KERN_ERR "HSL: intf_vrf_update: ifindex %u not found\n", msg->ifindex);
        return -ENODEV;
    }

    if (msg->op_type == 1) { /* 删除绑定 */
        /* 若该接口之前绑定到某个 VRF，则移回默认 VRF（vrf_id=0） */
        if (ifp->vrf_id != 0) {
            /* 如果是 VRF 设备本身，经 hsl_vrf_delete() 拆除：先清空该 VRF
             * 的路由/LSP/FEC 软件缓存与芯片表项（否则 VRF 复用后旧前缀、
             * 旧 VC 标签残留会劫持新转发），再删硬件 VRF 实例。此前直连
             * hw_l3_vrf_delete() 只删硬件侧，软件缓存与 hsl_vrf_count 全残留。 */
            if (msg->is_vrf_dev) {
                ret = hsl_vrf_delete(ifp->vrf_id);
                if (ret == -ENOENT) {
                    /* VRF 实例本就不存在（创建消息从未到达或 hw 侧已被清空），
                     * 无残留可清，按成功处理。 */
                    ret = 0;
                }
                if (ret) {
                    printk(KERN_ERR "HSL: hsl_vrf_delete(%u) failed\n", ifp->vrf_id);
                    return ret;
                }
            } else {
                /* 普通接口解除 VRF 绑定 */
                ret = hw_l3_if_vrf_bind(msg->ifindex, 0); /* 绑定到默认 VRF */
                if (ret) {
                    printk(KERN_ERR "HSL: hw_l3_if_vrf_bind(%u, 0) failed\n", msg->ifindex);
                    return ret;
                }
            }
            ifp->vrf_id = 0;
        }
        return 0;
    }

    /* op_type == 0: 添加或更新绑定 */
    if (msg->is_vrf_dev) {
        /* 这是一个 VRF 设备（如 CUST1），经 hsl_vrf_create() 创建硬件 VRF
         * 实例：内部补默认名 "vrf%u" 并维护 hsl_vrf_count，重复到达幂等返回 0。
         * 注意：不能直连 hw_l3_vrf_create() —— 它是 (vrf_id, name) 双参；
         * 原先的单参调用既无原型可见（implicit declaration）又缺 name 实参，
         * 属未定义行为，编译/运行都会出问题。 */
        ret = hsl_vrf_create(msg->vrf_id, NULL);
        if (ret) {
            printk(KERN_ERR "HSL: hsl_vrf_create(%u) failed\n", msg->vrf_id);
            return ret;
        }
        /* 注意：VRF 设备本身没有物理端口，只是逻辑容器，不需要绑定 ifindex。
         * 但某些芯片可能要求 VRF 也有对应的端口组，视硬件而定。 */
        ifp->vrf_id = msg->vrf_id;
    } else {
        /* 普通三层接口（如 vlanif）绑定到 VRF */
        ret = hw_l3_if_vrf_bind(msg->ifindex, msg->vrf_id);
        if (ret) {
            printk(KERN_ERR "HSL: hw_l3_if_vrf_bind(%u, %u) failed\n",
                   msg->ifindex, msg->vrf_id);
            return ret;
        }
        ifp->vrf_id = msg->vrf_id;
    }

    return 0;
}

int hsl_rt_msg(hsl_msg_t *msg)
{
    int ret = RUNOS_OK;
    hal_msg_rt_t *rt = HSL_MSG_DATA(msg);
    hsl_prefix_t prefix;
    hsl_prefix_t nh;
    int num;
    int is_base;

    /* 仅基础 IPv4/IPv6 路由消息（hal_msg_rt_t 载荷，byte0=family）需按 family
     * 解前缀。VRF 路由 / 接口-VRF 绑定消息的载荷不是 hal_msg_rt_t：byte0 是
     * vrf_id / ifindex 的低字节，在此按 family 判读会把绝大多数这类消息在
     * 进入 switch 前误判为非法 family 而丢弃。 */
    is_base = (msg->cmd == HAL_MSG_RT_IPV4_ADD ||
               msg->cmd == HAL_MSG_RT_IPV4_DEL
#if defined(HAVE_IPV6)
               || msg->cmd == HAL_MSG_RT_IPV6_ADD ||
               msg->cmd == HAL_MSG_RT_IPV6_DEL
#endif
               );

    if (is_base && rt->family == AF_INET)
    {
        prefix.family = AF_INET;
        nh.family = AF_INET;
        prefix.u.prefix4 = rt->u.ip;
        nh.prefixlen = 32;		
    }
#ifdef HAVE_IPV6
    else if (is_base && rt->family == AF_INET6)
    {
        prefix.family = AF_INET6;
        nh.family = AF_INET6;
        memcpy(&prefix.u.prefix6, &rt->u.ip6, sizeof(runos_ipv6_t));
        nh.prefixlen = 128;
    }
#endif
    else if (is_base)
    {
        runos_printf("%s: Invalid ip family\n", __FUNCTION__);
        ret = RUNOS_PARAM;
        hsl_msg_output(msg, ret, NULL, 0);
        return ret;
    }
    
    if (is_base)
        prefix.prefixlen = rt->prefixlen;
    
    switch(msg->cmd)
    {
        case HAL_MSG_RT_IPV4_ADD:
            for(num=0; num<rt->count; num++)
            {
                if (hsl_rt_ifindex_is_loopback(rt->nh[num].ifindex))
                {
                    /* lo 直连路由：HSL 接口表中无 lo，
                     * 直接下发 TOCPU 表项 */
                    ret += hw_add_loopback_rt(&prefix);
                    continue;
                }
                nh.u.prefix4 = rt->nh[num].u.ip;
                ret += hsl_add_rt(&prefix, &nh, rt->nh[num].ifindex,
                                  rt->nh[num].is_mpls, rt->nh[num].mpls_label);
            }
            break;

            case HAL_MSG_RT_IPV4_DEL:
            for(num=0; num<rt->count; num++)
            {
                if (hsl_rt_ifindex_is_loopback(rt->nh[num].ifindex))
                {
                    ret += hw_del_loopback_rt(&prefix);
                    continue;
                }
                nh.u.prefix4 = rt->nh[num].u.ip;
                ret += hsl_del_rt(&prefix, &nh, rt->nh[num].ifindex);
            }
            break;
            
#if defined(HAVE_IPV6)
        case HAL_MSG_RT_IPV6_ADD:
            for(num=0; num<rt->count; num++)
            {
                if (hsl_rt_ifindex_is_loopback(rt->nh[num].ifindex))
                {
                    ret += hw_add_loopback_rt(&prefix);
                    continue;
                }
                memcpy(&nh.u.prefix6, &rt->nh[num].u.ip6, sizeof(runos_ipv6_t));
                ret += hsl_add_rt(&prefix, &nh, rt->nh[num].ifindex,
                                  rt->nh[num].is_mpls, rt->nh[num].mpls_label);
            }
            break;
            
        case HAL_MSG_RT_IPV6_DEL:
            for(num=0; num<rt->count; num++)
            {
                if (hsl_rt_ifindex_is_loopback(rt->nh[num].ifindex))
                {
                    ret += hw_del_loopback_rt(&prefix);
                    continue;
                }
                memcpy(&nh.u.prefix6, &rt->nh[num].u.ip6, sizeof(runos_ipv6_t));
                ret += hsl_del_rt(&prefix, &nh, rt->nh[num].ifindex);
            }
            break;
#endif

        case HAL_MSG_RT_VRF_ADD:
        case HAL_MSG_RT_VRF_DEL:
            /* VRF 路由消息：解包 ctc_hal_msg_route_vrf 并下发 */
            ret = hsl_rt_vrf_msg(msg);
            break;

        case HAL_MSG_INTF_VRF_UPDATE:
        {
            struct ctc_hal_msg_intf_vrf *vrf_msg;
            if (msg->input_len < sizeof(*vrf_msg)) {
                ret = RUNOS_PARAM;
                break;
            }
            vrf_msg = (struct ctc_hal_msg_intf_vrf *)msg->input;
            ret = hsl_intf_vrf_update(vrf_msg);
            break;
        }
    
        default:
            ret = RUNOS_PARAM;
            break;
    }
    hsl_msg_output(msg, ret, NULL, 0);
    
    return ret;
}

int hsl_rt_init(void)
{
    g_hsl_rt = hsl_route_table_init();
    
    if(g_hsl_rt == NULL)
    {
        return RUNOS_MEMORY;
    }

    hsl_cmd_module_register(HAL_MODULE_ROUTE, hsl_rt_msg);

	hw_l3_init();
	
    return RUNOS_OK;
}

#endif /* HAVE_L3 */

#include "hsl.h"
#include "hsl_avl.h"
#include "hsl_table.h"
#include "hsl_if.h"
#include "hsl_nh.h"
#include "hsl_l3.h"
#include "hsl_arp.h"
#include <def_arp.h>
#if defined(HAVE_IPV6)
#include "hsl_nd.h"
#include <def_nd.h>
#include <linux/in6.h>   /* struct in6_addr（VRF ND 钩子） */
#endif
#include "zebra/arp_types.h"
#include "runos_util.h"
#include "hal_msg.h"
#include "hsl_vlan.h"

#ifdef HAVE_MLAG
#include "hsl_mlag.h"
#endif
#include "hsl_trunk.h"
#include "hsl_port.h"
#include "runos_trunk.h"
#include <linux/mutex.h>
#include <net/arp.h>

#if defined(HAVE_L3)

/* ARP learning hook: upgrade MPLS ILMs parked on a nexthop whose
 * ARP just resolved (defined in hw/hw_mpls.c). */
extern int hw_mpls_arp_resolved(u_int32_t ifindex, u_int32_t nexthop);
/* ARP removal / port-down hook: roll back MPLS ILMs and VRF routes
 * parked on this nexthop to TOCPU placeholder (hw/hw_mpls.c,
 * hw/hw_l3.c). */
extern void hw_mpls_arp_del(u_int32_t ifindex, u_int32_t nexthop);
extern void hw_l3_vrf_arp_del(u_int32_t ifindex, u_int32_t nexthop);
#if defined(HAVE_IPV6)
/* ND hooks: IPv6 twins of the ARP hooks above (defined in hw/hw_l3.c).
 * Upgrade / roll back VRF routes parked on an IPv6 (ND-resolved)
 * gateway. nexthop is struct in6_addr in network byte order, byte-layout
 * identical to hsl_prefix_t.u.prefix6. */
extern void hw_l3_vrf_nd_resolved(u_int32_t ifindex,
				  const struct in6_addr *nexthop);
extern void hw_l3_vrf_nd_del(u_int32_t ifindex,
			     const struct in6_addr *nexthop);
#endif

struct hsl_route_table *g_hsl_nh = NULL;
struct hsl_route_table *g_hsl_nh6 = NULL;
u_int32_t g_nh_num = 0;
u_int32_t g_nh_num_max = HSL_NH_MAX_NUM;
#if defined(HAVE_IPV6)
u_int32_t g_nh6_num = 0;
u_int32_t g_nh6_num_max = HSL_NH6_MAX_NUM;
#endif

DEFINE_MUTEX(nh_mutex_lock);

void nh_lock(void)
{
    mutex_lock(&nh_mutex_lock);
}

void nh_unlock(void)
{
    mutex_unlock(&nh_mutex_lock);
}

int hsl_nh_is_full(hsl_if_t *ifp)
{
    if (g_nh_num >= g_nh_num_max)
    {
        return 1;
    }

    if(ifp && (ifp->nh_num > ifp->nh_num_max))
    {
        return 1;
    }

    return 0;
}

#if defined(HAVE_IPV6)
int hsl_nh6_is_full(hsl_if_t *ifp)
{
    if (g_nh6_num >= g_nh6_num_max)
    {
        return 1;
    }

    if(ifp && (ifp->nh6_num > ifp->nh6_num_max))
    {
        return 1;
    }

    return 0;
}
#endif

int hsl_nh_num_plus(hsl_if_t *ifp, u_int8_t type)
{
	if(type == HSL_NH_TYPE_IP)
	{
	    g_nh_num++;
	    ifp->nh_num++;
	}
#if defined(HAVE_IPV6)
	else if(type == HSL_NH_TYPE_IPV6)
	{	
		g_nh6_num++;
		ifp->nh6_num++;
	}
#endif

    return 0;
}

int hsl_nh_num_sub(hsl_if_t *ifp, u_int8_t type)
{
	if(type == HSL_NH_TYPE_IP)
	{
		if(g_nh_num > 0)
	    	g_nh_num--;
		
		if(ifp->nh_num > 0)	
	    	ifp->nh_num--;		
	}
#if defined(HAVE_IPV6)
	else if(type == HSL_NH_TYPE_IPV6)
	{
		if(g_nh6_num > 0)
			g_nh6_num--;
		
		if(ifp->nh6_num > 0)
			ifp->nh6_num--;		
	}
#endif

    return 0;
}


static void hsl_nh_add_rt_info (struct hsl_route_node *rn, struct hsl_nh_entry_s *nh)
{
    if( rn->info == NULL)
    {
        rn->info = nh;
        nh->next  = NULL;
        nh->rn    = rn;
    }
    else
    {
        nh->next  = (struct hsl_nh_entry_s *)rn->info;
        rn->info = nh;
        nh->rn    = rn;
    }

    return;
}

static void hsl_nh_del_rt_info (struct hsl_route_node *rn, struct hsl_nh_entry_s *nh)
{
    hsl_nh_entry_t *tmp;
    
    if (rn->info == nh)
    {
        rn->info = nh->next;
        nh->next  = NULL;
    }

    FOR_EACH_NH(tmp, rn)
    {
        if (nh == tmp->next)
        {
            tmp->next = nh->next;
            nh->next = NULL;
            break;
        }
    }

    return;
}

static int hsl_nh_prefix_ptr_cmp (void *date1, void *date2)
{
    void *p1 =  date1;
    void *p2 =  date2;
    
    if (p1 > p2)
    {
        return 1;
    }
    else if (p1 < p2)
    {
        return -1;
    }
    else
    {
        return 0;
    }
}

static struct hsl_nh_entry_s *hsl_create_nh_entry (void)
{
    struct hsl_nh_entry_s *nh;
    int ret;

    nh = hsl_malloc (sizeof (struct hsl_nh_entry_s), HSL_MEM_GENERAL);
    if (!nh)
    {
        return NULL;
    }

    ret = hsl_avl_create (&nh->prefix_tree, 0, hsl_nh_prefix_ptr_cmp);
    if (ret < 0)
    {
        hsl_free (nh, HSL_MEM_GENERAL);
        return NULL;
    }

    return nh;
}

static void hsl_del_nh_entry(hsl_nh_entry_t *nh)
{
    if(nh->prefix_tree)
    {
        hsl_avl_tree_free(&nh->prefix_tree, NULL);
    }
    hsl_free(nh, HSL_MEM_GENERAL);

    return;
}

void hsl_update_nh_prefix(struct hsl_nh_entry_s *nh)
{
    struct hsl_avl_node_s *node;
    struct hsl_route_node *rn;

    for(node = hsl_avl_top(nh->prefix_tree); node; node = hsl_avl_next(node))
    {
        rn = node->info;
        if(!rn)
        {
            continue;
        }

        /* MPLS 压标签路由（BGP-LU/LDP ingress push）不在此重发：
         * push 的 oif 在 hw_mpls_nh_create 时已固定，ARP 刷新
         * 不会改变它；普通路由的 IPUC 仍按原逻辑刷新（ARP 更新
         * MAC/端口后需要重建 IPUC）。判断用 rn 上的 per-FEC
         * 记录——共享 nh 的 is_mpls 会被其他路由的消息覆盖，
         * 不可信（实测全 17 / 全 IP 污染）。 */
        if (rn->is_mpls)
            continue;

        hsl_l3_add_prefix(rn, nh);
    }
}

void hsl_update_nh_prefix_delete(struct hsl_nh_entry_s *nh)
{
    struct hsl_avl_node_s *node;
    struct hsl_route_node *rn;

    if (NULL == nh)
    {
        return;
    }

    if (NULL == nh->prefix_tree)
    {
        return;
    }

    for(node = hsl_avl_top(nh->prefix_tree); node; node = hsl_avl_next(node))
    {
        rn = node->info;		
        if(!rn)
        {
            continue;
        }
        
        hsl_l3_add_null_prefix(rn, nh);
    }
}

static u_int32_t _hsl_nh_add (struct hsl_route_node *rn, u_int32_t ifindex, u_int32_t l2_ifindex, char *mac, u_int8_t flags, u_int8_t type)
{
    u_int32_t ret;
    struct hsl_nh_entry_s *nh = NULL;
    struct hsl_nh_entry_s *nh_tmp = NULL;
    hsl_if_t *ifp = hsl_get_if_by_ifindex(ifindex);
	u_int32_t aging_time;

    if(ifp == NULL)
    {
        return RUNOS_PORT;
    }

	if(type == HSL_NH_TYPE_IP)
	{
		aging_time = HSL_ARP_AGING_TIME(ifp);
	}
#if defined(HAVE_IPV6)
	else if(type == HSL_NH_TYPE_IPV6)
	{
		aging_time = HSL_ND_AGING_TIME(ifp);
	}
#endif
	else
	{
		return RUNOS_PARAM;
	}

    if (rn->info)
    {
        FOR_EACH_NH(nh, rn)
        {
            if ((flags & HSL_NH_ENTRY_DYNAMIC) && (nh->flags & HSL_NH_ENTRY_STATIC))
            {
                if(!(nh->flags & HSL_NH_ENTRY_INAVALID))
                {
                    return RUNOS_ERR;
                }
                
                /* ip/mac bind */
                if(!memcmp (nh->mac, mac, RUNOS_MAC_LEN))
                {                
                    return RUNOS_ERR;
                }
            }

            if ((nh->ifindex == ifindex) &&
                nh->type == type && (!memcmp (nh->mac, mac, RUNOS_MAC_LEN) ||
                (nh->mac[0] == 0 && nh->mac[1] == 0 && nh->mac[2] == 0 &&
                nh->mac[3] == 0 && nh->mac[4] == 0 && nh->mac[5] == 0)))
            {
                nh->timeout = aging_time;
				nh->probe_count = HSL_ARP_PROBE_MAX_COUNT;
				
				if(flags == HSL_NH_ENTRY_STATIC)
				{
					nh->flags &= ~HSL_NH_ENTRY_DYNAMIC;
				}
                nh->flags |= flags;
                nh_tmp = nh;
                break;
            }
        }

        nh = nh_tmp;

#if 0
        /* delete from bcm if not same */
        if(nh && CHECK_FLAG(nh->flags, HSL_NH_ENTRY_IN_HW))
        {
            hsl_nh_num_sub(ifp, nh->type);
            hsl_l3_del_nh(nh);
			UNSET_FLAG(nh->flags, HSL_NH_ENTRY_IN_HW);
        }
#endif
    }

    /* new nexthop entry */
    if (nh == NULL)
    {
        nh = hsl_create_nh_entry ();
        if (!nh)
        {
            return RUNOS_MEMORY;
        }
        nh->flags = flags;
        hsl_nh_add_rt_info (rn, nh);	
		hsl_nh_num_plus(ifp, type);
    }

    nh->ifindex = ifindex;
    nh->l2_ifindex = l2_ifindex;
    nh->ori_l2_ifindex = l2_ifindex;
    nh->type = type;
	if(flags == HSL_NH_ENTRY_STATIC)
	{
		nh->flags &= ~HSL_NH_ENTRY_DYNAMIC;
	}
    nh->flags |= flags;
    nh->timeout = aging_time;	
	nh->probe_count = HSL_ARP_PROBE_MAX_COUNT;
    memcpy (nh->mac, mac, RUNOS_MAC_LEN);

    ret = hsl_l3_add_nh(nh);
    if (ret == RUNOS_OK)
    {
		SET_FLAG(nh->flags, HSL_NH_ENTRY_IN_HW);
    }

    return ret;
}

int hsl_nh_add(hsl_prefix_t *prefix, u_int32_t ifindex, u_int32_t l2_ifindex, u_int8_t *mac, u_int8_t flags, u_int8_t type)
{
    struct hsl_route_node *rn;
    int ret;

	if(HSL_DEBUG_CHECK(HSL_DBG_L3))
    {   
		printk("%s %d ifindex %#x l2 ifindex %#x mac %02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx flags 0x%x\n",__FUNCTION__,__LINE__,ifindex,l2_ifindex,
            mac[0],mac[1],mac[2],mac[3],mac[4],mac[5],flags);
        
        if(type == HSL_NH_TYPE_IP)
            hsl_prefix_print("ipv4",prefix);

        else if(type == HSL_NH_TYPE_IPV6)
            printk("ipv6: %x:%x:%x:%x\n",ntohl(prefix->u.prefix6.word[0]),ntohl(prefix->u.prefix6.word[1]),
                                            ntohl(prefix->u.prefix6.word[2]),ntohl(prefix->u.prefix6.word[3]));
    }

    if (type == HSL_NH_TYPE_IP)
    {
        rn = hsl_route_node_get (g_hsl_nh, prefix);				
    }
#if defined(HAVE_IPV6)
    else if(type == HSL_NH_TYPE_IPV6)
    {
        rn = hsl_route_node_get (g_hsl_nh6, prefix);	
    }
#endif
    else
    {
        return RUNOS_PARAM;
    }

    if (!rn)
    {
        runos_printf("Failed to alloc route table\n");
        return RUNOS_MEMORY;
    }

    mutex_lock(&nh_mutex_lock);
    ret = _hsl_nh_add(rn, ifindex, l2_ifindex, mac, flags, type);
    mutex_unlock(&nh_mutex_lock);
    hsl_route_unlock_node(rn);

    /* ARP learned: upgrade MPLS ILMs parked on this nexthop.  Called
     * after both locks are released because hw_mpls_arp_resolved()
     * re-enters the route-node table (hw_mpls_nh_lookup), which
     * would deadlock on the node still held by _hsl_nh_add(). */
    if (ret == RUNOS_OK && prefix->family == AF_INET)
        hw_mpls_arp_resolved(ifindex, ntohl(prefix->u.prefix4));
#if defined(HAVE_IPV6)
    /* ND learned: IPv6 twin — upgrade VRF routes parked on this v6
     * nexthop (hw_l3_vrf_nd_resolved).  Same post-unlock placement as
     * the ARP hook above for the same lock-order reason. */
    if (ret == RUNOS_OK && prefix->family == AF_INET6)
        hw_l3_vrf_nd_resolved(ifindex,
                              (const struct in6_addr *)&prefix->u.prefix6);
#endif

    return ret;
}

int hsl_nh_arp_del(u_int32_t ip, hsl_if_t *pif)
{
    struct net_device *dev;

    dev = pif->l3_intf.net_dev;
    if (!dev) {
        return EINVAL;
    }

    return arp_invalidate(dev, ip);
}

int _hsl_nh_del(hsl_nh_entry_t *nh)
{
    hsl_if_t *ifp = hsl_get_if_by_ifindex(nh->ifindex);

    if (ifp == NULL)
    {
        return RUNOS_PORT;
    }
    
    if (CHECK_FLAG(nh->flags, HSL_NH_ENTRY_IN_HW))
    {
        hsl_l3_del_nh(nh);
        UNSET_FLAG(nh->flags, HSL_NH_ENTRY_IN_HW);
    }

	hsl_nh_num_sub(ifp, nh->type);

    if (nh->tree_count == 0)
    {
        hsl_nh_del_rt_info(nh->rn, nh);
        if (nh->rn->info == NULL)
        {
            hsl_nh_arp_del(nh->rn->p.u.prefix4, ifp);
            hsl_route_unlock_node(nh->rn);
		}
        hsl_del_nh_entry(nh);
    }
    else
    {
        memset(nh->mac, 0, RUNOS_MAC_LEN);
    }

    /* ARP removed / port down: roll back MPLS ILMs (hw_mpls_arp_del)
     * and VRF routes (hw_l3_vrf_arp_del) parked on this nexthop to
     * TOCPU placeholder, so traffic is punted to CPU instead of being
     * forwarded with a stale DMAC (blackhole). Called with
     * nh_mutex_lock held - these functions only touch hw caches / SDK,
     * never the HSL nh table, so no lock-order issue. IPv4 branch for
     * MPLS / v4-gate VRF routes, IPv6 branch for ND / v6-gate VRF
     * routes (hw_l3_vrf_nd_del; no v6 MPLS push ILM in this scope). */
    if (nh->rn != NULL && nh->rn->p.family == AF_INET)
    {
        u_int32_t ip = ntohl(nh->rn->p.u.prefix4);

        hw_mpls_arp_del(nh->ifindex, ip);
        hw_l3_vrf_arp_del(nh->ifindex, ip);
    }
#if defined(HAVE_IPV6)
    else if (nh->rn != NULL && nh->rn->p.family == AF_INET6)
    {
        hw_l3_vrf_nd_del(nh->ifindex,
                         (const struct in6_addr *)&nh->rn->p.u.prefix6);
    }
#endif

    return RUNOS_OK;
}

int hsl_nh_del (hsl_prefix_t *prefix, u_int32_t ifindex, u_int32_t nh_type)
{
    struct hsl_route_node *rn;
    struct hsl_route_table *rt;
    hsl_nh_entry_t *nh;    
    vid_t vid = 0;
    int ret = 0;
    if(prefix->family == AF_INET)
    {
        rt = g_hsl_nh;
    }
#if defined(HAVE_IPV6)
    else if(prefix->family == AF_INET6)
    {
        rt = g_hsl_nh6;
    }
#endif
    else
    {
        return RUNOS_PARAM;
    }
    
    mutex_lock(&nh_mutex_lock);
    rn = hsl_route_node_lookup(rt, prefix);
    if (rn == NULL)
    {
        ret = RUNOS_NOT_FOUND;
        goto out_unlock;
    }
    
    FOR_EACH_NH(nh, rn)
    {
        if(UIF_GET_TYPE(ifindex) != PORT_TYPE_VLANIF)
        {
            ret = hsl_vlan_port_pvid_get(ifindex, &vid);
            if((nh->type == nh_type) && (UIF_GET_VLAN(nh->ifindex) == vid) && (ret == RUNOS_OK))
            {
                break;
            }
        }
        else if(nh->type == nh_type && nh->ifindex == ifindex)
        {
            break;
        }
    }
    hsl_route_unlock_node(rn);
    
    if (nh == NULL)
    {
        ret = RUNOS_NOT_FOUND;
        goto out_unlock;
    }

    ret = _hsl_nh_del(nh);
out_unlock:
    mutex_unlock(&nh_mutex_lock);
    return ret;
}

#ifdef HAVE_MLAG
int _hsl_nh_mlag_update(struct hsl_route_table *rt, u_int32_t l2_ifindex, bool up)
{
    const struct hsl_mlag_config_t *mc;
    struct hsl_route_node *rn, *rn_next;
    hsl_nh_entry_t *nh, *nh_next;
    int ret;
    u_int8_t trunk_id;
    bool move_to_peerlink;

    mc = hsl_mlag_config_get();
    if (!mc->enable) {
        return -1;
    }

    if (!up && !mc->peerlink.linkup) {
        return -1;
    }

    move_to_peerlink = false;
    if (UIF_IFINDEX_IS_TRUNK(l2_ifindex)) {
        trunk_id = UIF_GET_TRUNK_ID(l2_ifindex);
        if (trunk_id >= LAG_ID_MIN && trunk_id <= LAG_ID_MAX) {
            move_to_peerlink = (mc->config_lag_bmp & (1 << (trunk_id - 1))) != 0;
        }
    }

    if (HSL_DEBUG_CHECK(HSL_DBG_MLAG)) {
        printk("%s:%d for l2 ifindex %#x run, is up %hhu\n", __FUNCTION__, __LINE__, l2_ifindex, up);
    }

    FOR_EACH_RN_SAFE(rn, rn_next, rt)
    {
        if (HSL_DEBUG_CHECK(HSL_DBG_MLAG)) {
            char buf[64];
            hsl_prefix2str(&rn->p, buf, sizeof(buf));
            printk("%s:%d hsl nh update for mlag ip prefix %s\n",
                __FUNCTION__, __LINE__, buf);
        }
        FOR_EACH_NH_SAFE(nh, nh_next, rn)
        {
            if (HSL_DEBUG_CHECK(HSL_DBG_MLAG)) {
                printk("nh l2 ifindex %#x flags %#x\n", nh->l2_ifindex, nh->flags);
            }
            if (nh->flags & HSL_NH_ENTRY_INAVALID) {
                continue;
            }
            if (!(nh->flags & HSL_NH_ENTRY_STATIC))
            {
                if (!up)
                {
                    if (l2_ifindex == nh->l2_ifindex) {
                        if (move_to_peerlink) {
                            if (HSL_DEBUG_CHECK(HSL_DBG_MLAG)) {
                                printk("arp nexthop mac %02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx move from l2 ifindex %#x to peerlink l2 ifindex %#x\n",
                                    nh->mac[0], nh->mac[1], nh->mac[2], nh->mac[3], nh->mac[4], nh->mac[5], nh->l2_ifindex, mc->peerlink.ifindex);
                            }
                            nh->ori_l2_ifindex = nh->l2_ifindex;
                            nh->l2_ifindex = mc->peerlink.ifindex;
                            ret = hsl_l3_add_nh(nh);
                            if (ret == RUNOS_OK)
                            {
                                SET_FLAG(nh->flags, HSL_NH_ENTRY_IN_HW);
                            }
                        }
                    }
                } else {
                    if (nh->l2_ifindex == mc->peerlink.ifindex &&
                        nh->ori_l2_ifindex == l2_ifindex) {
                        if (HSL_DEBUG_CHECK(HSL_DBG_MLAG)) {
                            printk("arp nexthop mac %02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx move from peerlink ifindex %#x to l2 ifindex %#x\n",
                                nh->mac[0], nh->mac[1], nh->mac[2], nh->mac[3], nh->mac[4], nh->mac[5], nh->l2_ifindex, nh->ori_l2_ifindex);
                        }
                        nh->l2_ifindex = nh->ori_l2_ifindex;
                        ret = hsl_l3_add_nh(nh);
                        if (ret == RUNOS_OK)
                        {
                            SET_FLAG(nh->flags, HSL_NH_ENTRY_IN_HW);
                        }
                    }
                }
            }
        }
    }

    return 0;
}

int hsl_nh_mlag_update(u_int32_t l2_ifindex, bool up)
{
    mutex_lock(&nh_mutex_lock);
    _hsl_nh_mlag_update(g_hsl_nh, l2_ifindex, up);
#ifdef HAVE_IPV6
    _hsl_nh_mlag_update(g_hsl_nh6, l2_ifindex, up);
#endif
    mutex_unlock(&nh_mutex_lock);
    return 0;
}
#endif

int _hsl_nh_port_down(struct hsl_route_table *rt, u_int32_t l2_ifindex)
{
    struct hsl_route_node *rn, *rn_next;
    hsl_nh_entry_t *nh, *nh_next;
    int count;

    count = 0;
    if (!rt) {
        return 0;
    }

    FOR_EACH_RN_SAFE(rn, rn_next, rt)
    {
        FOR_EACH_NH_SAFE(nh, nh_next, rn)
        {
            if (nh->flags & HSL_NH_ENTRY_INAVALID) {
                continue;
            }
            if (l2_ifindex == 0 || l2_ifindex == nh->l2_ifindex)
            {
                if (!(nh->flags & HSL_NH_ENTRY_STATIC))
                {
                    _hsl_nh_del(nh);
                    count++;
                }
            }
        }
    }

    return count;
}

int hsl_nh_port_down(u_int32_t l2_ifindex)
{
    int count;

    mutex_lock(&nh_mutex_lock);
    count = _hsl_nh_port_down(g_hsl_nh, l2_ifindex);
#ifdef HAVE_IPV6
    count += _hsl_nh_port_down(g_hsl_nh6, l2_ifindex);
#endif
    mutex_unlock(&nh_mutex_lock);

    return count;
}

int hsl_nh_clear(struct hsl_route_table *table, hsl_prefix_t *p, u_int32_t ifindex)
{
    struct hsl_route_node *rn, *rn_next;
    hsl_nh_entry_t *nh, *nh_next;

    mutex_lock(&nh_mutex_lock);
    FOR_EACH_RN_SAFE(rn, rn_next, table)
    {
        if (p && hsl_prefix_same(p, &rn->p))
        {
            continue;
        }

        FOR_EACH_NH_SAFE(nh, nh_next, rn)
        {
            if (ifindex && ifindex != nh->ifindex)
            {
                continue;
            }
            
            if (nh->flags & (HSL_NH_ENTRY_STATIC | HSL_NH_ENTRY_DYNAMIC))
            {
                _hsl_nh_del( nh);
            }
        }
    }
    mutex_unlock(&nh_mutex_lock);
    
    return RUNOS_OK;
}

hsl_nh_entry_t * hsl_rt_get_nh(u_int32_t ifindex, hsl_prefix_t *prefix, u_int32_t flag)
{
    hsl_nh_entry_t *nh;
    hsl_nh_entry_t *exist_nh = NULL;
    struct hsl_route_node* rn;
    struct hsl_route_table *table;
    hsl_if_t *ifp = hsl_get_if_by_ifindex(ifindex);
	u_int32_t aging_time;

	if(ifp == NULL)
	{
		return NULL;
	}
	
	if(prefix->family == AF_INET)
	{
		table = g_hsl_nh;
		aging_time = HSL_ARP_AGING_TIME(ifp);
	}
#if defined(HAVE_IPV6)
	else if(prefix->family == AF_INET6)
	{
		table = g_hsl_nh6;
		aging_time = HSL_ND_AGING_TIME(ifp);
	}
#endif
    else
    {
        return NULL;
    }

    rn = hsl_route_node_get(table, prefix);
    if(rn == NULL)
    {
        return NULL;
    }
    
    FOR_EACH_NH(nh, rn)
    {
        if(!exist_nh)
        {
            exist_nh = nh;
        }
        
        if(nh->ifindex == ifindex)
        {
            break;
        }
    }

    hsl_route_unlock_node(rn);
    
    if(nh == NULL)
    {
        nh = hsl_create_nh_entry();
        if(nh == NULL)
        {
            return NULL;
        }
        nh->ifindex = ifindex;

		if( RUNOS_TRUE == flag)
		{
			nh->flags = HSL_NH_ENTRY_DYNAMIC;
			nh->timeout = aging_time;		
			nh->probe_count = HSL_ARP_PROBE_MAX_COUNT;
		}
		else
		{
        	nh->flags = HSL_NH_ENTRY_STATIC | HSL_NH_ENTRY_INAVALID;
        }

        if(exist_nh)
        {
            memcpy(nh->mac, exist_nh->mac, RUNOS_MAC_LEN);
        }

        hsl_nh_add_rt_info(rn, nh);
    }
    return nh;
}

hsl_nh_entry_t *hsl_rt_lookup_nh__(u_int32_t ifindex, hsl_prefix_t *prefix)
{
    hsl_nh_entry_t *nh;
    struct hsl_route_node* rn = NULL;
    struct hsl_route_table *table;

    if(prefix->family == AF_INET)
    {
        table = g_hsl_nh;
    }
#if defined(HAVE_IPV6)
    else if(prefix->family == AF_INET6)
    {
        table = g_hsl_nh6;
    }
#endif
    else
    {
        return NULL;
    }

    rn = hsl_route_node_lookup(table, prefix);
    if(rn == NULL)
    {
        return NULL;
    }
    
    if(ifindex == UIF_INVALID_INDEX)
    {
        hsl_route_unlock_node(rn);
        return (hsl_nh_entry_t *)rn->info;        
    }

    FOR_EACH_NH(nh, rn)
    {
        if(nh->ifindex == ifindex)
        {
            break;
        }
    }

    hsl_route_unlock_node(rn);

    return nh;
}

bool hsl_rt_lookup_nh(u_int32_t ifindex, hsl_prefix_t *prefix)
{
    bool found;

    mutex_lock(&nh_mutex_lock);
    found = hsl_rt_lookup_nh__(ifindex, prefix) != NULL;
    mutex_unlock(&nh_mutex_lock);

    return found;
}

static void hsl_nh_to_arp_msg(hal_msg_arp_t *arp, hsl_nh_entry_t *nh)
{
    hsl_if_t *ifp;
    
    arp->ifindex = nh->ifindex;
    arp->l2_ifindex = nh->l2_ifindex;
    arp->flags = nh->flags;
    memcpy(arp->mac, nh->mac, RUNOS_MAC_LEN);
    ifp = hsl_get_if_by_ifindex(nh->ifindex);
    if(ifp != NULL)
    {
        arp->age = (nh->timeout) * HSL_ARP_TIMER_STEP;
    }
}

int hsl_nh_get(u_int32_t ip, void *entry, u_int32_t ifindex, u_int32_t flags)
{
    struct hsl_route_node *rn;
    hsl_nh_entry_t *nh;
    int num = 0;
    hal_msg_arp_t *arp = entry;    
    struct hsl_prefix_s p;

    if(ip)
    {
        p.family = AF_INET;
        p.prefixlen = IPV4_MAX_BITLEN;
        p.u.prefix4 = ip;
        rn = hsl_route_node_get(g_hsl_nh, &p);

        mutex_lock(&nh_mutex_lock);
        FOR_EACH_NH(nh, rn)
        {
            /* 显示所有ip地址对应的ARP */
            if(nh->flags & HSL_NH_ENTRY_INAVALID)
            {
                continue;
            }

            if ((nh->flags & flags) && ((ifindex == UIF_IFINDEX_INVALID) || (nh->ifindex == ifindex)))
            {
                arp[num].ip = rn->p.u.prefix4;
                hsl_nh_to_arp_msg(&arp[num], nh);
                num++;
                if(num >= HAL_MSG_ND_MAX_NUM)
                {
                    break;
                }
            }
        }
        mutex_unlock(&nh_mutex_lock);
        hsl_route_unlock_node(rn);
        
        return num;
    }

    mutex_lock(&nh_mutex_lock);
    FOR_EACH_RN(rn, g_hsl_nh)
    {
        FOR_EACH_NH(nh, rn)
        {
            /* 显示所有ip地址对应的ARP */
            if(nh->flags & HSL_NH_ENTRY_INAVALID)
            {
                continue;
            }

            if ((nh->flags & flags) && ((ifindex == UIF_IFINDEX_INVALID) || (nh->ifindex == ifindex)))
            {
                arp[num].ip = rn->p.u.prefix4;
                hsl_nh_to_arp_msg(&arp[num], nh);
                num++;
                if(num >= HAL_MSG_ND_MAX_NUM)
                {
                    hsl_route_unlock_node(rn);
                    goto out_unlock;
                }
            }
        }
    }

out_unlock:
    mutex_unlock(&nh_mutex_lock);
    
    return num;
}

int hsl_nh_get_next(u_int32_t ip, void *entry, u_int32_t ifindex, u_int32_t flags)
{
    struct hsl_route_node *rn;
    hsl_nh_entry_t *nh;
    int num = 0;
    hal_msg_arp_t *arp = entry;
    struct hsl_prefix_s p;
    
    p.family = AF_INET;
    p.prefixlen = IPV4_MAX_BITLEN;
    p.u.prefix4 = ip;
    rn = hsl_route_node_get(g_hsl_nh, &p);
    
    mutex_lock(&nh_mutex_lock);
    FOR_EACH_RN_CONTINUE(rn)
    {
        FOR_EACH_NH(nh, rn)
        {
            /* 显示所有ip地址对应的ARP */
            if(nh->flags & HSL_NH_ENTRY_INAVALID)
            {
                continue;
            }

            arp[num].ip = rn->p.u.prefix4;
            if ((nh->flags & flags) && ((ifindex == UIF_IFINDEX_INVALID) || (nh->ifindex == ifindex)))
            {
                arp[num].ip = rn->p.u.prefix4;
                hsl_nh_to_arp_msg(&arp[num], nh);
                num++;
                if(num >= HAL_MSG_ND_MAX_NUM)
                {
                    hsl_route_unlock_node(rn);
                    goto out_unlock;
                }
            }
        }
    }

out_unlock:
    mutex_unlock(&nh_mutex_lock);
    
    return num;
}

int hsl_nh_add_static(u_int32_t ifindex, u_int32_t ip, umac_t mac)
{
    hsl_prefix_t p;

    p.family = AF_INET;
    p.prefixlen = IPV4_MAX_BITLEN;
    p.u.prefix4 = ip;
    return hsl_nh_add(&p, ifindex, 0, mac, HSL_NH_ENTRY_STATIC, HSL_NH_TYPE_IP);
}

int hsl_nh_del_static(u_int32_t ifindex, u_int32_t ip)
{
    hsl_prefix_t p;
    
    p.family = AF_INET;
    p.prefixlen = IPV4_MAX_BITLEN;
    p.u.prefix4 = ip;
    
    hsl_nh_del(&p,ifindex, HSL_NH_TYPE_IP);
    return RUNOS_OK;
}

void hsl_nh_sync_aging_time(void)
{
    struct hsl_route_node *rn;
    hsl_nh_entry_t *nh;
    hsl_if_t *ifp;
	
    mutex_lock(&nh_mutex_lock);
    FOR_EACH_RN(rn, g_hsl_nh)
    {
        FOR_EACH_NH(nh, rn)
        {
			ifp = hsl_get_if_by_ifindex(nh->ifindex);
		    if(ifp == NULL)
		    {
				continue;
		    }
			
            if((nh->flags & HSL_NH_ENTRY_DYNAMIC))
            {
				if(nh->timeout > HSL_ARP_AGING_TIME(ifp))
				{
					nh->timeout = HSL_ARP_AGING_TIME(ifp);
				}
            }
        }
    }
    mutex_unlock(&nh_mutex_lock);
}

void hsl_nh_aging(void)
{
    struct hsl_route_node *rn, *rn_next;
    hsl_nh_entry_t *nh, *nh_next;

    mutex_lock(&nh_mutex_lock);
    FOR_EACH_RN_SAFE(rn, rn_next, g_hsl_nh)
    {
        FOR_EACH_NH_SAFE(nh, nh_next, rn)
        {
            if((nh->flags & HSL_NH_ENTRY_DYNAMIC))
            {
                if(nh->timeout == 0)
                {
				   if(nh->probe_count != 0)
				   	{				   	
					  nh->probe_count--;
					  hsl_arp_probe(nh);
				   	}
				   else
					 _hsl_nh_del(nh); 		 
                }
				else
					nh->timeout--;
            }
        }
    }
    mutex_unlock(&nh_mutex_lock);
}

void hsl_nh6_aging(void)
{
    struct hsl_route_node *rn, *rn_next;
    hsl_nh_entry_t *nh, *nh_next;

    mutex_lock(&nh_mutex_lock);
    FOR_EACH_RN_SAFE(rn, rn_next, g_hsl_nh6)
    {
        FOR_EACH_NH_SAFE(nh, nh_next, rn)
        {
            if((nh->flags & HSL_NH_ENTRY_DYNAMIC))
            {
                if(nh->timeout == 0)
                {
					if(nh->probe_count != 0)
					{
						nh->probe_count--;						
						hsl_nd_probe(nh);
					}
					else
						_hsl_nh_del(nh);		  
                }
				else
					nh->timeout --;
            }
        }
    }
    mutex_unlock(&nh_mutex_lock);
}

int hsl_nh_init(void)
{
    g_hsl_nh = hsl_route_table_init();
    if(g_hsl_nh == NULL)
    {
        runos_printf("%s: Faild to init nh\n", __FUNCTION__);
        return RUNOS_ERR;
    }
    
#if defined(HAVE_IPV6)
    g_hsl_nh6  = hsl_route_table_init();
    if(g_hsl_nh6 == NULL)
    {
        runos_printf("%s: Faild to init nh6\n", __FUNCTION__);
        return RUNOS_ERR;
    }
#endif

    return RUNOS_OK;
}

#endif /* HAVE_L3 */

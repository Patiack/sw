/*
   Copyright (C) 2017 Shenzhen Hored Technology Inc.
*/

#include "hsl.h"
#include "zebra/arp_types.h"
#include "hsl_sem.h"
#include "hsl_module.h"
#include "hsl_avl.h"
#include "hsl_if.h"
#include "hsl_prefix.h"
#include "hsl_nh.h"
#include "hsl_arp.h"
#include "hal_msg.h"
#include "hsl_pkt.h"
#include "hsl_mlag.h"
#include <def_arp.h>
#include "hsl_mlag_pkt.h"
#include "hsl_mlag_arp.h"

#if defined(HAVE_L3)

struct hsl_arp_s
{
    struct task_struct *thread;
    u_int32_t loop;
    hsl_sem_t sem;
    struct timer_list timer;
}g_hsl_arp;

extern struct hsl_route_table *g_hsl_nh;

static u_int32_t hsl_arp_add(u_int32_t ip, umac_t mac, u_int32_t ifindex)
{
    u_int32_t ret = 0;
    
    ret = hsl_nh_add_static(ifindex, ip, mac);
    HSL_IF_ERR_RETURN(ret);

    return RUNOS_OK;
}

static u_int32_t hsl_arp_del(u_int32_t ip, u_int32_t ifindex)
{
    u_int32_t ret = 0;
    
    ret = hsl_nh_del_static(ifindex, ip);
    HSL_IF_ERR_RETURN(ret);

	return RUNOS_OK;
}

int hsl_arp_del_by_ifindex(u_int32_t ifindex)
{
    return hsl_nh_clear(g_hsl_nh, NULL, ifindex);
}

static int hsl_arp_get(u_int32_t ip, void *entry, u_int32_t ifindex, u_int32_t flags)
{
    return hsl_nh_get(ip, entry, ifindex, flags);
}

static int hsl_arp_get_next(u_int32_t ip, void *entry, u_int32_t ifindex, u_int32_t flags)
{
    return hsl_nh_get_next(ip, entry, ifindex, flags);
}

extern struct hsl_avl_tree_s *g_hsl_if_entry;
static int hsl_arp_set_aging_time(u_int32_t ifindex, u_int32_t time)
{
    hsl_if_t *ifp;
    
    if(ifindex == 0)
    {
        struct hsl_avl_node_s *node;

        for(node = hsl_avl_top(g_hsl_if_entry); node; node = hsl_avl_next(node))
        {
            ifp = HSL_AVL_NODE_INFO(node);
            if(ifp == NULL)
                continue;
            HSL_ARP_AGING_TIME(ifp) = time/HSL_ARP_TIMER_STEP;
        }

		hsl_nh_sync_aging_time();
    }
    else
    {
        ifp = hsl_get_if_by_ifindex( ifindex);
        if(ifp == NULL)
            return RUNOS_PORT;
        HSL_ARP_AGING_TIME(ifp) = time/HSL_ARP_TIMER_STEP;

		hsl_nh_sync_aging_time();
    }

    return RUNOS_OK;
}

extern u_int32_t g_nh_num_max;
static int hsl_arp_set_num(u_int32_t ifindex, u_int32_t num)
{
    hsl_if_t *ifp;
    
    if(ifindex == 0)
    {
        g_nh_num_max = num;
    }
    else
    {
        ifp = hsl_get_if_by_ifindex( ifindex);
        if(ifp == NULL)
            return RUNOS_PORT;
        ifp->nh_num_max = num;
    }

    return RUNOS_OK;
}

int hsl_arp_probe(hsl_nh_entry_t *nh)
{	
    hsl_if_t *ifp = hsl_get_if_by_ifindex(nh->ifindex);
	struct hsl_route_node *rn = nh->rn;
    struct hsl_prefix_list_s *plist;
    struct hsl_prefix_s pfx;
    struct hsl_prefix_s p;

    if (ifp == NULL)
    {
        return RUNOS_PORT;
    }

    p.family = AF_INET;
    p.prefixlen = IPV4_MAX_BITLEN;
    p.u.prefix4 = rn->p.u.prefix4;
	
    for (plist = ifp->l3_intf.addr; plist; plist = plist->next)
    {
        pfx = plist->prefix;	
        if (hsl_prefix_match (&pfx, &p))
        {
            if(HSL_DEBUG_CHECK(HSL_DBG_L3))
            {
                hsl_prefix_print("arp probe src ipv4",&pfx);
                hsl_prefix_print("dst ipv4",&p);
            }
           
        	hsl_arp_tx(ifp, p.u.prefix4, pfx.u.prefix4, NULL, ifp->l3_intf.mac, NULL, 0x0001);
        }
    }
	
	return RUNOS_OK;	
}

int hsl_arp_msg(hsl_msg_t *msg)
{
    int ret    = RUNOS_OK;
    hal_msg_arp_t *p = HSL_MSG_DATA(msg);
    
    switch(msg->cmd)
    {
        case HAL_MSG_ARP_ADD:
            ret = hsl_arp_add( p->ip, p->mac, p->ifindex);
            break;
            
        case HAL_MSG_ARP_DEL:
            ret = hsl_arp_del( p->ip, p->ifindex);
            break;
            
        case HAL_MSG_ARP_GET:
            {
                u_int32_t num;
                char buf[512];
                num = hsl_arp_get(p->ip, buf, p->ifindex, p->flags); 
                hsl_msg_output(msg, RUNOS_OK, buf, sizeof(hal_msg_arp_t)*num);
                return RUNOS_OK;
            }
            
        case HAL_MSG_ARP_GET_NEXT:
            {
                u_int32_t num;
                char buf[512];
                num = hsl_arp_get_next(p->ip, buf, p->ifindex, p->flags); 
                hsl_msg_output(msg, RUNOS_OK, buf, sizeof(hal_msg_arp_t)*num);
                return RUNOS_OK;
            }
            
        case HAL_MSG_ARP_AGE:
            ret = hsl_arp_set_aging_time(p->ifindex, p->age);
            break;
            
        case HAL_MSG_ARP_CLEAR:
            ret = hsl_arp_del_by_ifindex(p->ifindex);
            break;
            
        case HAL_MSG_ARP_NUM:
            ret = hsl_arp_set_num(p->ifindex, p->age);
            break;
            
        default:
            ret = RUNOS_PARAM;
    }

    hsl_msg_output(msg, ret, NULL, 0);
    
    return ret;
}

static int hsl_arp_timer_handler(void *context)
{
    struct hsl_arp_s *p = (struct hsl_arp_s *)context;
    
    while(p->loop)
    {
        hsl_sem_take(p->sem);
        hsl_nh_aging();
    }

    return 0;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,14,0)
static void hsl_arp_timer(unsigned long context)
{
    struct hsl_arp_s *p = (struct hsl_arp_s *)context;
    
    hsl_sem_give(p->sem);
    p->timer.expires = jiffies + (HSL_ARP_TIMER_STEP * HZ);
    add_timer(&p->timer);

    return;
}
#else
static void hsl_arp_timer(struct timer_list *t)
{
    struct hsl_arp_s *p = from_timer(p, t, timer);  
	
	p->timer.expires = jiffies + (HSL_ARP_TIMER_STEP * HZ);
    add_timer(&p->timer);
	
    return;
}
#endif

int hsl_arp_init(void)
{
    struct hsl_arp_s *p = &g_hsl_arp;

    p->loop = 1;
    p->sem = hsl_sem_create(0);
    p->thread = kthread_create(hsl_arp_timer_handler, (void *)&g_hsl_arp, "arp_aging");
    if(IS_ERR(p->thread))
    {
        runos_printf("Faild to create arp thread\n");
        p->thread = NULL;
        return RUNOS_ERR;
    }
    wake_up_process(p->thread);

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,14,0)
    init_timer(&p->timer);
    p->timer.expires = jiffies + (HSL_ARP_TIMER_STEP * HZ);
    p->timer.data = (unsigned long)&g_hsl_arp;
    p->timer.function = &hsl_arp_timer;
#else
	p->timer.expires = jiffies + (HSL_ARP_TIMER_STEP * HZ);
	timer_setup(&p->timer,hsl_arp_timer,0);
#endif
    add_timer(&p->timer);

    hsl_cmd_module_register(HAL_MODULE_ARP, hsl_arp_msg);

    return RUNOS_OK;
}

int hsl_arp_exit(void)
{
    hsl_nh_clear(g_hsl_nh, NULL, 0);
    del_timer_sync(&g_hsl_arp.timer);
    
    g_hsl_arp.loop = 0;
    hsl_sem_give(g_hsl_arp.sem);
    hsl_sem_destroy(g_hsl_arp.sem);
    return RUNOS_OK;
}

int hsl_handle_arp(struct hsl_if_s *ifp, struct runos_eth_hdr_s *eth, struct runos_arp_hdr_s *arp, u_int32_t l2_ifindex)
{
    struct hsl_prefix_s p;
    u_int32_t src_ip;
    int found = 0;
    struct hsl_prefix_list_s *plist;
    struct hsl_prefix_s pfx;
    u_int32_t   mask;

    if (hsl_nh_is_full(ifp))
    {
        return RUNOS_FULL;
    }
	
    if ((arp->ar_op == ARP_OP_REPLY) && memcmp(eth->dmac, ifp->l3_intf.mac, RUNOS_MAC_LEN) != 0)
    {
        return RUNOS_PARAM;
    }

    if ((arp->ar_op == ARP_OP_REQUEST) && !RUNOS_MAC_IS_BROADCAST (eth->dmac) &&
         memcmp (eth->dmac, ifp->l3_intf.mac, RUNOS_MAC_LEN) != 0)
    {
        return RUNOS_PARAM;
    }

    /* source mac consistency check */
    if (memcmp (eth->smac, (char*)arp->ar_sha, RUNOS_MAC_LEN) != 0)
    {
        return RUNOS_ERR;
    }

    src_ip = RUNOS_8TO32(arp->ar_sip);
 
    p.family = AF_INET;
    p.prefixlen = IPV4_MAX_BITLEN;
    p.u.prefix4 = src_ip;
		
    for (plist = ifp->l3_intf.addr; plist; plist = plist->next)
    {
        pfx = plist->prefix;

        if (hsl_prefix_match (&pfx, &p))
        {
            hsl_masklen2ip (pfx.prefixlen, &mask);
            if (((p.u.prefix4 & mask) != src_ip) && ((p.u.prefix4 | (~mask)) != src_ip))
            {           
                found = 1;
            }

            /* if the vlanif port ip and arp message source ip are the same, they will not be issued to the chip */
            if (pfx.u.prefix4 == src_ip) {
                found = 0;
                break;
            }
        }
    }

    if (!found)
    {
        return RUNOS_PARAM;
    }

    if (HSL_DEBUG_CHECK(HSL_DBG_L3)) {
        printk("hsl add nh ifindex %#x l2 ifindex %#x ip %#x sha %02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx\n",
            ifp->ifindex, l2_ifindex, src_ip, arp->ar_sha[0],arp->ar_sha[1], arp->ar_sha[2], arp->ar_sha[3], 
            arp->ar_sha[4], arp->ar_sha[5]);
    }
    return hsl_nh_add(&p, ifp->ifindex, l2_ifindex, arp->ar_sha, HSL_NH_ENTRY_DYNAMIC, HSL_NH_TYPE_IP);
}
#endif /* HAVE_L3 */
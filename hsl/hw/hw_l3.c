#include "hw.h"
#include "hw_chip.h"
#include "hw_port.h"
#include "hsl_nh.h"
#include "hsl_port.h"
#include "hsl_trunk.h"
#include "hsl_vlan.h"
#include "hsl_lib.h"
#include "hw_stack.h"
#include "hw_mac.h"
#include "hw_trap.h"
#include "hsl_rt_vrf.h"

#include <linux/mutex.h> /* DEFINE_MUTEX（与 hw_mpls.c 一致） */
#include <linux/in6.h>   /* struct in6_addr（ND 钩子） */

#include "sal.h"
#include "ctc_l3if.h"
#include "ctc_api.h"
#include "ctcs_api.h"
#include "ctc_error.h"
#include "ctc_debug.h"

#define RUNOS_NEXTHOP_IPUC_ID_MIN 3
#define RUNOS_NEXTHOP_IPUC_ID_MAX 4096

/* hw_mpls.c: BGP-LU labeled route push nexthop management */
extern int hw_nh_add_route_push(uint32_t label, uint32_t nexthop,
				uint32_t ifindex, uint8_t *mac, uint32_t *p_nhid);
extern int hw_nh_del_route_push(uint32_t label, uint32_t ifindex);
extern int hw_nh_add_route_push_stack(const uint32_t *labels, uint8_t label_num,
				      uint32_t nexthop, uint32_t ifindex,
				      uint8_t *mac, uint32_t *p_nhid);
extern int hw_nh_del_route_push_stack(const uint32_t *labels, uint8_t label_num,
				      uint32_t ifindex);
/* hw_mpls.c: ECMP 组（多路径 VRF 路由 / 多路径 LSP） */
extern int hw_mpls_ecmp_create(const uint32_t *members,
			       const uint8_t *member_is_push, uint8_t num,
			       uint32_t *p_nhid);
extern void hw_mpls_ecmp_release(uint32_t nhid);
extern int hw_mpls_nh_id_in_use(uint32_t nhid);

static uint16 hw_l3_vlanid_to_l3ifid(uint16 vlanid)
{
 	//CTC_VLAN_RANGE_CHECK(vlanid) 
    /* 4093,4094 special used for EPEDISCARDTYPE_DEST_VLAN_PTR_DIS */
	if(vlanid == 4093 || vlanid == 4094 || vlanid == 4095)
		return vlanid + 3;
	else
		return vlanid;	
}

static int hw_l3_nexthop_find(uint8 unit, ctc_ip_nh_param_t nh_param, uint32* nhid)
{
	uint32 i;
	ctc_nh_info_t out;
	int32 ret;
    ctc_ip_nh_param_t ipuc_nh;
	uint32 nhid_nouse = 0;

	for(i = RUNOS_NEXTHOP_IPUC_ID_MIN; i <= RUNOS_NEXTHOP_IPUC_ID_MAX; i++)
	{	
		sal_memset(&out, 0, sizeof(ctc_nh_info_t));
		sal_memset(&ipuc_nh, 0, sizeof(ctc_ip_nh_param_t));
		out.p_nh_param = &ipuc_nh;
		ret = ctcs_nh_get_nh_info(unit, i, &out);
		if(ret == CTC_E_NONE)
		{
			if(out.nh_type == CTC_NH_TYPE_IPUC && ipuc_nh.oif.vid == nh_param.oif.vid &&
				sal_memcmp(ipuc_nh.mac, nh_param.mac, RUNOS_MAC_LEN) == 0)
			{
				*nhid = i;
				return RUNOS_OK;
			}
		}
		else
		{
			/* 避开 MPLS push / ECMP 组已占用的 nhid：
			 * ctcs_nh_get_nh_info 查不到 MPLS 类型 nexthop，
			 * 不加此检查会重复分配同一 nhid 覆盖 push
			 * （hw_mpls_nh_id_in_use 为尽力检查，见 hw_mpls.c） */
			if(nhid_nouse == 0 && !hw_mpls_nh_id_in_use(i))
				nhid_nouse = i;
		}
		
	}

	if(nhid_nouse != 0)
	{
	    *nhid = nhid_nouse;
		return RUNOS_EMPTY;
	}
	else
	{
		*nhid = 0;
		return RUNOS_FULL;
	}
}

int hw_l3_add_null_prefix(struct hsl_route_node *rn, struct hsl_nh_entry_s *nh)
{
	int unit;
    ctc_ipuc_param_t new;
    ctc_ipuc_param_t old;
	int32 retval;

	memset(&old, 0, sizeof(ctc_ipuc_param_t));
	memset(&new, 0, sizeof(ctc_ipuc_param_t));

	old.vrf_id = 0;
	old.masklen = rn->p.prefixlen;	
	old.nh_id = nh->id;

	new.nh_id = CTC_NH_RESERVED_NHID_FOR_TOCPU;
	new.route_flag = CTC_IPUC_FLAG_CONNECT;
    new.vrf_id = 0;
	new.masklen = rn->p.prefixlen;

	if (rn->p.family == AF_INET)
	{
		new.ip.ipv4 = old.ip.ipv4 = ntohl(rn->p.u.prefix4); 	
		new.ip_ver = old.ip_ver = CTC_IP_VER_4;
				
		if(HSL_DEBUG_CHECK(HSL_DBG_BCM))
			hsl_prefix_print("add netroute to cpu ipv4", &(rn->p));
	}

#ifdef HAVE_IPV6
	else if (rn->p.family == AF_INET6)
	{
		new.ip.ipv6[0] = old.ip.ipv6[0] = ntohl(rn->p.u.prefix6.word[0]);
		new.ip.ipv6[1] = old.ip.ipv6[1] = ntohl(rn->p.u.prefix6.word[1]);
		new.ip.ipv6[2] = old.ip.ipv6[2] = ntohl(rn->p.u.prefix6.word[2]);	
		new.ip.ipv6[3] = old.ip.ipv6[3] = ntohl(rn->p.u.prefix6.word[3]);
		new.ip_ver = old.ip_ver = CTC_IP_VER_6;	
				
		if(HSL_DEBUG_CHECK(HSL_DBG_BCM))
			printk("add netroute to cpu ipv6 %x:%x:%x:%x\n",new.ip.ipv6[0],new.ip.ipv6[1],new.ip.ipv6[2],new.ip.ipv6[3]);     
	}
#endif

	FOREACH_UNIT(unit)
	{
		retval = ctcs_ipuc_get(unit, &old);
		if(retval == CTC_E_NOT_EXIST)
		{			
			if(HSL_DEBUG_CHECK(HSL_DBG_BCM))
				printk("ipuc not exist and add ipuc\n");
			
			HW_ERR_RETURN(ctcs_ipuc_add(unit, &new));	
		}
		/* 改下一跳：ctcs_ipuc_add() 命中已有表项时 SDK 会原地更新
		 * （_sys_usw_ipuc_update() -> nalpm_update，只改 nexthop 字段），
		 * 不要先 remove 再 add：ALPM 的 delete/re-add 会让 item 重新
		 * 分配 sram_idx（整条 item 搬家），没有必要 */
		else if(retval == CTC_E_NONE && new.nh_id != old.nh_id )
		{
			if(HSL_DEBUG_CHECK(HSL_DBG_BCM))
				printk("ipuc change old nhid %d to new nhid %d (in place)\n",old.nh_id, new.nh_id);

			HW_ERR_RETURN(ctcs_ipuc_add(unit, &new));
		}
	}

	return RUNOS_OK;
}

int hw_l3_add_prefix(struct hsl_route_node *rn, struct hsl_nh_entry_s *nh)
{
	int unit;
    ctc_ipuc_param_t new;		
    ctc_ipuc_param_t old;		
	hsl_if_t *ifp = hsl_get_if_by_ifindex(nh->ifindex);
	uint32 nhId;
	int32 retval;

	memset(&new, 0, sizeof(ctc_ipuc_param_t)); 
	memset(&old, 0, sizeof(ctc_ipuc_param_t));
	new.route_flag = CTC_IPUC_FLAG_CONNECT;
    new.vrf_id = 0;
	new.masklen = rn->p.prefixlen;

	if(nh->mac[0] == 0x0 && nh->mac[1] == 0x0 &&
		nh->mac[2] == 0x0 && nh->mac[3] == 0x0 &&
		nh->mac[4] == 0x0 && nh->mac[5] == 0x0)	
	{	
		new.nh_id = CTC_NH_RESERVED_NHID_FOR_TOCPU;
		nhId = new.nh_id;
	}
	else if (rn->is_mpls && rn->mpls_label != 0)
	{
		/* BGP-LU: labeled route needs an MPLS push nexthop.
		 * The IPUC points at the push nexthop so the chip
		 * pushes the label and forwards (PUSH_OP_ROUTE).
		 * 标签来源用 rn 上的 per-FEC 记录（共享 nh 的字段会被
		 * 其他路由的消息覆盖）。 */
		uint32_t mpls_nhid = 0;

		/* nexthop 必须是 /32 下一跳 IP（nh->rn->p）：hw_mpls_oif_fill
		 * 用它查 HSL nh 表得到 l2_ifindex（真实物理出端口）。原实现
		 * 误传路由前缀（rn->p），查表 miss 后 fallback 到 MAC 反查
		 * （hw_mac_get_port），可能命中错误的 FDB 端口，导致 MPLS
		 * tunnel 的 DESTPORT 指向错误端口（实测 200.0/24 压标签路由
		 * 的 tunnel DESTPORT=0x0014，报文从错误端口发出被丢弃）。 */
		if (hw_nh_add_route_push(rn->mpls_label,
					 ntohl(nh->rn->p.u.prefix4),
					 nh->ifindex, nh->mac,
					 &mpls_nhid) == RUNOS_OK)
		{
			new.nh_id = mpls_nhid;
			nhId = mpls_nhid;
			printk("hw_l3: labeled route prefix=0x%x/%u -> push nhid=%u label=%u nexthop=0x%x ifindex=0x%x\n",
			       ntohl(rn->p.u.prefix4), rn->p.prefixlen,
			       mpls_nhid, rn->mpls_label,
			       ntohl(nh->rn->p.u.prefix4),
			       nh->ifindex);
		}
		else
		{
			printk("hw_l3: labeled route prefix=0x%x/%u push nh create FAILED label=%u ifindex=0x%x, fallback TOCPU\n",
			       ntohl(rn->p.u.prefix4), rn->p.prefixlen,
			       rn->mpls_label, nh->ifindex);
			new.nh_id = CTC_NH_RESERVED_NHID_FOR_TOCPU;
			nhId = new.nh_id;
		}
	}
	else
	{
	    new.nh_id = nh->id;	
		nhId = new.nh_id;
	}
			
	if (rn->p.family == AF_INET)
	{
		new.ip.ipv4 = ntohl(rn->p.u.prefix4);			
		new.ip_ver = CTC_IP_VER_4;	
		if(HSL_DEBUG_CHECK(HSL_DBG_BCM))
			hsl_prefix_print("add netroute ipv4", &(rn->p));
	}
#ifdef HAVE_IPV6
	else if (rn->p.family == AF_INET6)
	{
		new.ip.ipv6[0] = ntohl(rn->p.u.prefix6.word[0]);
		new.ip.ipv6[1] = ntohl(rn->p.u.prefix6.word[1]);
		new.ip.ipv6[2] = ntohl(rn->p.u.prefix6.word[2]);	
		new.ip.ipv6[3] = ntohl(rn->p.u.prefix6.word[3]);
		new.ip_ver = CTC_IP_VER_6;
		
		if(HSL_DEBUG_CHECK(HSL_DBG_BCM))
			printk("add netroute ipv6 %x:%x:%x:%x\n",new.ip.ipv6[0],new.ip.ipv6[1],new.ip.ipv6[2],new.ip.ipv6[3]);       
	}
#endif

	FOREACH_UNIT(unit)
	{
        memcpy(&old, &new, sizeof(ctc_ipuc_param_t));
		retval = ctcs_ipuc_get(unit, &old);
		if(retval == CTC_E_NOT_EXIST)
		{
			if(HSL_DEBUG_CHECK(HSL_DBG_BCM))
				printk("ipuc not exist and add nhid %d\n",new.nh_id);
			
			HW_ERR_RETURN(ctcs_ipuc_add(unit, &new));	
		}
		/* 改下一跳：同上，原地更新即可，不 remove */
		else if(retval == CTC_E_NONE && old.nh_id != nhId )
		{
			if(HSL_DEBUG_CHECK(HSL_DBG_BCM))
				printk("ipuc not same and update in place old nhid %d new nhid %d\n",old.nh_id, nhId);

			new.nh_id = nhId;
			HW_ERR_RETURN(ctcs_ipuc_add(unit, &new));
		}

	}

	return RUNOS_OK;
}

int hw_l3_del_prefix(struct hsl_route_node *rn, struct hsl_nh_entry_s *nh)
{
	int unit;
    ctc_ipuc_param_t route;
	uint32 nhId = nh->id;
	
	/* Release the BGP-LU MPLS push nexthop for a labeled route.
	 * 标签来源用 rn 上的 per-FEC 记录（共享 nh 的字段会被其他
	 * 路由的消息覆盖，用缓存残留标签删除会删错对象）。 */
	if (rn->is_mpls && rn->mpls_label != 0)
		hw_nh_del_route_push(rn->mpls_label, nh->ifindex);

	memset(&route, 0, sizeof(ctc_ipuc_param_t));
    route.vrf_id = 0;
	route.nh_id= nhId;
	route.masklen = rn->p.prefixlen;	

	if (rn->p.family == AF_INET)
	{
		route.ip.ipv4 = ntohl(rn->p.u.prefix4);		
		route.ip_ver = CTC_IP_VER_4;
		
		if(HSL_DEBUG_CHECK(HSL_DBG_BCM))
			hsl_prefix_print("del netoute ipv4",&(rn->p));
	}	
#ifdef HAVE_IPV6
	else if (rn->p.family == AF_INET6)
	{
		route.ip.ipv6[0] = ntohl(rn->p.u.prefix6.word[0]);
		route.ip.ipv6[1] = ntohl(rn->p.u.prefix6.word[1]);
		route.ip.ipv6[2] = ntohl(rn->p.u.prefix6.word[2]);	
		route.ip.ipv6[3] = ntohl(rn->p.u.prefix6.word[3]);
		route.ip_ver = CTC_IP_VER_6;	
		
		if(HSL_DEBUG_CHECK(HSL_DBG_BCM))
			printk("del netroute ipv6: %x:%x:%x:%x\n",route.ip.ipv6[0],route.ip.ipv6[1],route.ip.ipv6[2],route.ip.ipv6[3]);  
	}
#endif

	FOREACH_UNIT(unit)
	{
		if(ctcs_ipuc_get(unit, &route) == CTC_E_NONE)
		{
			if(HSL_DEBUG_CHECK(HSL_DBG_BCM))
				printk("remove ipuc nhid %d\n",route.nh_id);
			
			HW_ERR_RETURN(ctcs_ipuc_remove(unit, &route));
		}
	}
	
	return RUNOS_OK;
}

int hw_l3_add_nh(struct hsl_nh_entry_s *nh)
{
	hsl_if_t *ifp = hsl_get_if_by_ifindex(nh->ifindex);
	struct hsl_prefix_s* prefix = &(nh->rn->p);		
	ctc_ipuc_param_t new;
	ctc_ipuc_param_t old;
	int unit;
    ctc_ip_nh_param_t nh_param = {0};
	ctc_nh_info_t nh_info;
    uint32 nhId = 0;
    u_int16_t vid;
	u_int32_t ifindex;
    hw_lport_t hport;
	int retval;

	if(ifp == NULL)
	{
		return RUNOS_PORT;
	}

    vid = ifp->l3_intf.vid;	
	nh_param.dsnh_offset = 0;	
    memcpy(&nh_param.mac, nh->mac, RUNOS_MAC_LEN);	
	nh_param.oif.vid = vid;

	if(HSL_DEBUG_CHECK(HSL_DBG_L3))
		printk("%s %d vid %d mac %x:%x:%x:%x:%x:%x\n",__FUNCTION__,__LINE__,vid,nh_param.mac[0],nh_param.mac[1],
			nh_param.mac[2],nh_param.mac[3],nh_param.mac[4],nh_param.mac[5]);

	memset(&new, 0, sizeof(ctc_ipuc_param_t));
	new.route_flag |= CTC_IPUC_FLAG_NEIGHBOR;
	new.vrf_id = 0;

	if(prefix->family == AF_INET)
	{
		new.ip.ipv4 = ntohl(prefix->u.prefix4);
		new.ip_ver = CTC_IP_VER_4;
		new.masklen = 32;
		
		if(HSL_DEBUG_CHECK(HSL_DBG_L3))
	 		hsl_prefix_print("ipv4",prefix);
	}
#if defined(HAVE_IPV6)
	else if(prefix->family == AF_INET6)
	{
		new.ip.ipv6[0] = ntohl(prefix->u.prefix6.word[0]);
		new.ip.ipv6[1] = ntohl(prefix->u.prefix6.word[1]);
		new.ip.ipv6[2] = ntohl(prefix->u.prefix6.word[2]);	
		new.ip.ipv6[3] = ntohl(prefix->u.prefix6.word[3]);
		new.ip_ver = CTC_IP_VER_6;		
		new.masklen = 128;
		
		if(HSL_DEBUG_CHECK(HSL_DBG_L3))
			printk("ipv6: %x:%x:%x:%x\n",new.ip.ipv6[0],new.ip.ipv6[1],new.ip.ipv6[2],new.ip.ipv6[3]);
	}
#endif

	FOREACH_UNIT(unit)
	{
		retval = hw_l3_nexthop_find(unit, nh_param, &nhId);
		if(retval == RUNOS_FULL)
		{
			printk("ERROR! HW Nethop is full\n");
			return retval;
		}

		if(HSL_DEBUG_CHECK(HSL_DBG_L3))
			printk("nhId %d is empty\n",nhId);
		
		if (nh->l2_ifindex > 0 &&
			((UIF_IFINDEX_IS_TRUNK(nh->l2_ifindex) &&
			hsl_trunk_is_running(UIF_GET_TRUNK_ID(nh->l2_ifindex))) ||
			((!UIF_IFINDEX_IS_TRUNK(nh->l2_ifindex) &&
			hsl_port_is_running(nh->l2_ifindex))))) {
			ifindex = nh->l2_ifindex;
		} else {
			if(hw_mac_get_port(vid, nh_param.mac, &ifindex) != RUNOS_OK) // no found
			{	
				if(HSL_DEBUG_CHECK(HSL_DBG_L3))
					printk("add nethop mac not found\n");
				
				return RUNOS_NOT_FOUND;
			}
			if (ifindex == UIF_IFINDEX_INVALID) {
				return RUNOS_NOT_FOUND;
			}
			nh->ori_l2_ifindex = nh->l2_ifindex = ifindex;
		}

		if (UIF_IFINDEX_IS_TRUNK(ifindex))
		{
			nh_param.oif.gport = CTC_MAP_TID_TO_GPORT(HW_TRUNK_ID(UIF_GET_TRUNK_ID(ifindex)));
		}
		else
		{
			ifindex2hport(ifindex, &hport);
			nh_param.oif.gport = hport.port;
		}

		if(retval == RUNOS_EMPTY)
		{
			if(HSL_DEBUG_CHECK(HSL_DBG_L3))
				printk("add nexthop ifindex %u port %d\n", ifindex, nh_param.oif.gport);
			retval = ctcs_nh_add_ipuc(unit, nhId, &nh_param);
			if(retval == CTC_E_NONE)
			{
				//还需要考虑该mac设备ipv6的ipuc删除后，删除了nexthop，但是ipv4的ipuc还指向旧的nexthop，需要删除
				memcpy(&old, &new, sizeof(ctc_ipuc_param_t));
				retval = ctcs_ipuc_get(unit, &old);
				if(retval == CTC_E_NONE)
				{
					if( old.nh_id == nhId)
					{
						retval = ctcs_ipuc_remove(unit, &old);
						if (retval != CTC_E_NONE)
							printk("hw_l3: ipuc remove stale nhid=%u FAILED ret=%d\n",
							       old.nh_id, retval);
					}
				}
			}
		} else {
			memset(&nh_info, 0, sizeof(nh_info));
			if (ctcs_nh_get_nh_info(unit, nhId, &nh_info) == CTC_E_NONE) {
				if (nh_info.gport != nh_param.oif.gport) {
					if (HSL_DEBUG_CHECK(HSL_DBG_L3)) {
						printk("nh id %u gport is %#x update gport is %#x\n", nhId, nh_info.gport, nh_param.oif.gport);
					}
					HW_ERR_RETURN(ctcs_nh_update_ipuc(unit, nhId, &nh_param));
				}
			} else {
				HW_ERR_RETURN(ctcs_nh_update_ipuc(unit, nhId, &nh_param));
			}
		}

        memcpy(&old, &new, sizeof(ctc_ipuc_param_t));

		new.nh_id = nhId;
		retval = ctcs_ipuc_get(unit, &old);
		if(retval == CTC_E_NOT_EXIST)
		{	
			if(HSL_DEBUG_CHECK(HSL_DBG_L3))
				printk("add ipuc nhID %d\n",new.nh_id);
			
			HW_ERR_RETURN(ctcs_ipuc_add(unit, &new)); 
			
		}											
		/* 改下一跳：原地更新即可（SDK 内部走 _sys_usw_ipuc_update()），
		 * 不 remove —— 理由见 hw_l3_add_prefix */
        else if(retval == CTC_E_NONE)
        {
            if( old.nh_id != new.nh_id)
            {
    			if(HSL_DEBUG_CHECK(HSL_DBG_L3))
                    printk("ipuc not same and update in place old nhid %d new nhid %d\n",old.nh_id, new.nh_id);

                retval = ctcs_ipuc_add(unit, &new);
                if (retval != CTC_E_NONE) {
                    /* 原地更新失败时旧表项保持不动（不会掉进 default） */
                    printk("hw_l3: ipuc update FAILED old_nhid=%u new_nhid=%u ret=%d\n",
                           old.nh_id, new.nh_id, retval);
                    return RUNOS_ERR;
                }
            }
        }
	}

	if(nhId == 0)
		return RUNOS_ERR;
	
	nh->id = nhId;	
	hsl_update_nh_prefix(nh);
	return RUNOS_OK;
}

int hw_l3_del_nh(struct hsl_nh_entry_s *nh)
{
	int unit;
	hsl_if_t *ifp = hsl_get_if_by_ifindex(nh->ifindex);
	struct hsl_prefix_s* prefix = &(nh->rn->p);
	ctc_ipuc_param_t host;
    ctc_ip_nh_param_t nh_param = {0};	
    uint32 nhId = 0;
    u_int16_t vid;
	u_int32_t ifindex;
    hw_lport_t hport;
	int retval;

	if(ifp == NULL)
	{
		return RUNOS_PORT;
	}

    vid = ifp->l3_intf.vid;	
	nh_param.dsnh_offset = 0;
    memcpy(&nh_param.mac, nh->mac, RUNOS_MAC_LEN);
	nh_param.oif.vid = vid;

	memset(&host,0,sizeof(ctc_ipuc_param_t));
	host.vrf_id = 0;
	host.masklen = prefix->prefixlen;
	host.nh_id = nh->id;

	if(HSL_DEBUG_CHECK(HSL_DBG_BCM))
		printk("%s %d vid %d mac %x:%x:%x:%x:%x:%x nID %d\n",__FUNCTION__,__LINE__,vid,nh_param.mac[0],nh_param.mac[1],
			nh_param.mac[2],nh_param.mac[3],nh_param.mac[4],nh_param.mac[5],nh->id);
	
	if(prefix->family == AF_INET)
	{
		host.ip.ipv4 = ntohl(prefix->u.prefix4);			
		host.ip_ver = CTC_IP_VER_4;
		
		if(HSL_DEBUG_CHECK(HSL_DBG_BCM))
	 		hsl_prefix_print("ipv4",prefix);
	}
#if defined(HAVE_IPV6)
	else if(prefix->family == AF_INET6)
	{
		host.ip_ver = CTC_IP_VER_6;
		host.ip.ipv6[0] = ntohl(prefix->u.prefix6.word[0]);
		host.ip.ipv6[1] = ntohl(prefix->u.prefix6.word[1]);
		host.ip.ipv6[2] = ntohl(prefix->u.prefix6.word[2]); 
		host.ip.ipv6[3] = ntohl(prefix->u.prefix6.word[3]);	
		
		if(HSL_DEBUG_CHECK(HSL_DBG_BCM))
			printk("ipv6: %x:%x:%x:%x\n",host.ip.ipv6[0],host.ip.ipv6[1],host.ip.ipv6[2],host.ip.ipv6[3]);      
	}
#endif

	FOREACH_UNIT(unit)
	{
	#if 0
		retval = ctcs_ipuc_get(unit, &host);
		if( retval == CTC_E_NONE)
		{
			if(HSL_DEBUG_CHECK(HSL_DBG_BCM))
				printk("ipuc remove\n");
			
			HW_ERR_RETURN(ctcs_ipuc_remove(unit, &host));
		}
    #else
        if(HSL_DEBUG_CHECK(HSL_DBG_BCM))
            printk("ipuc remove\n");
        
        ctcs_ipuc_remove(unit, &host);
    #endif
		retval = hw_l3_nexthop_find(unit, nh_param, &nhId);
		if(retval == RUNOS_OK)
		{				
			if(HSL_DEBUG_CHECK(HSL_DBG_BCM))
				printk("nhid found and remove nethop\n");
			
			HW_ERR_RETURN(ctcs_nh_remove_ipuc(unit, nhId));
		}		
	}

	if(nhId == 0)
		return RUNOS_ERR;
	
	nh->id = nhId;	
    hsl_update_nh_prefix_delete(nh);
    return RUNOS_OK;
}

int hw_l3_intf_create(hsl_if_t *pif)
{
	int unit;
	ctc_l3if_router_mac_t router_mac;
	uint16 l3if_id;
	ctc_l3if_t intf;
	ctc_l3if_property_t l3if_prop;

	memset(&intf, 0, sizeof(ctc_l3if_t));
	intf.l3if_type = CTC_L3IF_TYPE_VLAN_IF;
	intf.vlan_id = pif->l3_intf.vid;
	l3if_id = hw_l3_vlanid_to_l3ifid(intf.vlan_id);

	if(l3if_id <= 0)
	{
		printk(KERN_ERR"[%s][%d] vlan id invalid\r\n", __FUNCTION__, __LINE__);
		return RUNOS_PARAM;	
	}

	if ((4093 == intf.vlan_id) || (4094 == intf.vlan_id)){
		printk("create l3if by vid 4093 or 4094 is not support!\n");
		return 	RUNOS_PARAM;
	}

	memset(&router_mac, 0, sizeof(router_mac));
	router_mac.num = 1;
	memcpy(router_mac.mac[0], pif->l3_intf.mac, RUNOS_MAC_LEN);

	FOREACH_UNIT(unit)
	{
		HW_ERR_RETURN(ctcs_l3if_create(unit, l3if_id, &intf));			
		HW_ERR_RETURN(ctcs_l3if_set_interface_router_mac(unit, l3if_id, router_mac));

		l3if_prop = CTC_L3IF_PROP_ROUTE_EN;	
		HW_ERR_RETURN(ctcs_l3if_set_property(unit, l3if_id, l3if_prop, TRUE));
	
		l3if_prop = CTC_L3IF_PROP_IPV4_UCAST;	
		HW_ERR_RETURN(ctcs_l3if_set_property(unit, l3if_id, l3if_prop, TRUE));

		/* MPLS 报文（ether-type 0x8847）入口识别：不设置该属性时
		 * 芯片不执行 ILM 查表，MPLS 帧被当普通 L2 未知单播洪泛
		 * （实测 pkt-trace：mpls-port=NO → dest Mcast Group，R2
		 * 收到 label 19 后洪泛到 VLAN 10 内，转发链断裂）。SAI
		 * 创建 RIF 时无条件设置 CTC_L3IF_PROP_MPLS_EN=1，这里
		 * 保持一致，所有 vlanif 均支持 MPLS 入口识别。 */
		l3if_prop = CTC_L3IF_PROP_MPLS_EN;	
		HW_ERR_RETURN(ctcs_l3if_set_property(unit, l3if_id, l3if_prop, TRUE));

		/* VRF 查找模式：VRF-en=0 时该接口的 L3 查找忽略"报文自带的 vrf"，
		 * L3VPN 解封装报文因此用接口 vrf(0) 去查，落到 vrf 0 的 TCAM 行 8188
		 * 而非 VPN 的 8189 → 默认路由丢弃。实测解封装级已给出 vrfId=8
		 * (read DbgIpeMplsDecapInfo 0)、ILM fid=8 也正确，唯一为 0 的就是这一位。
		 * SDK 自建 rchain 对所有接口（含 vrf 0）都设 VRF-en=1
		 * (sys_usw_l3if.c:1596-1612)，隧道保留接口亦然
		 * (sys_usw_internal_port.c:361)，与 MPLS_EN 同理，这里保持一致。 */
		l3if_prop = CTC_L3IF_PROP_VRF_EN;	
		HW_ERR_RETURN(ctcs_l3if_set_property(unit, l3if_id, l3if_prop, TRUE));

#if defined(HAVE_IPV6)
		l3if_prop = CTC_L3IF_PROP_IPV6_UCAST;	
		HW_ERR_RETURN(ctcs_l3if_set_property(unit, l3if_id, l3if_prop, TRUE));
#endif	
		HW_ERR_RETURN(hw_l3_pkt_trap_to_cpu_l3if(unit, l3if_id, RUNOS_L3_TRAP_TYPE_ARP, RUNOS_TRAP_ACTION_COPY_TO_CPU));
		HW_ERR_RETURN(hw_l3_pkt_trap_to_cpu_l3if(unit, l3if_id, RUNOS_L3_TRAP_TYPE_LDP, RUNOS_TRAP_ACTION_COPY_TO_CPU));
	}

    pif->l3_intf.l3if[0] = l3if_id;

    return RUNOS_OK;
}

int hw_l3_intf_delete(hsl_if_t *pif)
{
	int unit;
	uint16 l3if_id;
	ctc_l3if_t intf;
	ctc_l3if_router_mac_t router_mac;

	memset(&intf, 0, sizeof(ctc_l3if_t));
	intf.l3if_type = CTC_L3IF_TYPE_VLAN_IF;
	intf.vlan_id = pif->l3_intf.vid;
	l3if_id = hw_l3_vlanid_to_l3ifid(intf.vlan_id);

	if(l3if_id <= 0)
	{
		printk(KERN_ERR"[%s][%d] vlan id invalid\r\n", __FUNCTION__, __LINE__);
		return RUNOS_PARAM;	
	}

	memset(&router_mac, 0, sizeof(router_mac));
	router_mac.num = 1;
	
    FOREACH_UNIT(unit)
	{	
		HW_ERR_RETURN(hw_l3_pkt_trap_to_cpu_l3if(unit, l3if_id, RUNOS_L3_TRAP_TYPE_ARP, RUNOS_TRAP_ACTION_FWD));
		HW_ERR_RETURN(hw_l3_pkt_trap_to_cpu_l3if(unit, l3if_id, RUNOS_L3_TRAP_TYPE_LDP, RUNOS_TRAP_ACTION_FWD));
		HW_ERR_RETURN(ctcs_l3if_set_interface_router_mac(unit, l3if_id, router_mac));
   		HW_ERR_RETURN(ctcs_l3if_destory(unit, l3if_id, &intf));		
    }
	
    return RUNOS_OK;
}

int hw_l3_set_mac_address(hsl_if_t *pif, void *addr)
{
	hw_unit_t unit;
	ctc_l3if_router_mac_t router_mac;

	memset(&router_mac, 0, sizeof(router_mac));
	router_mac.num = 1;
	memcpy(router_mac.mac[0], addr, RUNOS_MAC_LEN);

	FOREACH_UNIT(unit) {
		HW_ERR_RETURN(ctcs_l3if_set_interface_router_mac(unit, pif->l3_intf.l3if[0], router_mac));
	}

	return 0;
}


/* ==================================================================
 * L3 VPN：VRF 硬件抽象层
 *
 * Centec SDK 无显式 VRF API，VRF 通过 ctc_ipuc_param_t.vrf_id
 * 隐式隔离（现有代码全部显式置 0 = 默认 VRF）。本模块：
 *   - 软件 VRF 表登记/注销/查询（FRR VRF ID 与 SDK vrf_id 1:1 映射）
 *   - VRF 路由按 (sdk_vrf, ip, masklen) 写入 IPUC；带标签路由复用
 *     hw_mpls.c 的 push nexthop（BGP-LU 模式，单标签）
 *   - VRF 路由软件缓存：del 消息只有 (vrf_id, af, prefix, prefixlen)，
 *     必须靠缓存还原 out_label/ifindex/nhid 才能正确释放硬件资源
 * ================================================================== */

#define HW_L3_VRF_MAX        256
#define HW_L3_VRF_ROUTE_MAX  1024
#define HW_L3_VRF_ID_MAX     4095
#define HW_L3_VRF_NH_MAX     8    /* 与 CTC_HAL_MSG_MAX_NH_VRF 一致 */

struct hw_l3_vrf_entry {
	uint32_t vrf_id;      /* FRR VRF ID */
	char     name[32];
	uint8_t  used;
};

static struct hw_l3_vrf_entry g_hw_l3_vrf[HW_L3_VRF_MAX];

struct hw_l3_vrf_route_entry {
	uint32_t vrf_id;      /* FRR VRF ID */
	uint8_t  af;          /* AF_INET / AF_INET6 */
	uint8_t  prefix[16];  /* 网络序原样（v4：低 4B；v6：16B） */
	uint8_t  prefixlen;
	uint32_t out_label;   /* 最外层标签（网络序）；0 = 非标签路由 */
	uint32_t ifindex;     /* 标签路由出接口 */
	uint32_t nhid;        /* IPUC 指向的 nexthop ID（单路径或 ECMP 组） */
	uint8_t  placeholder; /* 1：TOCPU 占位，等待 ARP/ND 解析后升级 */
	uint8_t  is_ecmp;     /* nhid 是 ECMP 组（成员为 push/IPUC nexthop） */
	uint8_t  nh_count;    /* nhs[] 有效条目数 */
	uint8_t  label_depth; /* label_stack[] 有效层数（0..3） */
	mpls_label_t label_stack[3]; /* 网络序，外层在前（消息原样） */
	struct hsl_nexthop nhs[HW_L3_VRF_NH_MAX]; /* 原始路径（del / ARP
						    * 升级重发用） */
	uint8_t  used;
};

static struct hw_l3_vrf_route_entry g_hw_l3_vrf_route[HW_L3_VRF_ROUTE_MAX];

static DEFINE_MUTEX(hw_l3_vrf_mutex);

/* FRR VRF ID -> SDK IPUC vrf_id（1:1 映射，0 = 默认 VRF） */
static int hw_l3_vrf_id_to_sdk(uint32_t vrf_id)
{
	if (vrf_id > HW_L3_VRF_ID_MAX)
		return -1;
	return (int)vrf_id;
}

static struct hw_l3_vrf_entry *hw_l3_vrf_find(uint32_t vrf_id)
{
	int i;

	for (i = 0; i < HW_L3_VRF_MAX; i++) {
		if (g_hw_l3_vrf[i].used && g_hw_l3_vrf[i].vrf_id == vrf_id)
			return &g_hw_l3_vrf[i];
	}
	return NULL;
}

/* 前缀键字节数（网络序原样：v4 4B / v6 16B） */
static uint8_t hw_l3_vrf_prefix_len(uint8_t af)
{
	return (af == AF_INET6) ? 16 : 4;
}

static struct hw_l3_vrf_route_entry *hw_l3_vrf_route_find(uint32_t vrf_id,
							  const void *prefix,
							  uint8_t prefixlen,
							  uint8_t af)
{
	int i;

	for (i = 0; i < HW_L3_VRF_ROUTE_MAX; i++) {
		if (g_hw_l3_vrf_route[i].used &&
		    g_hw_l3_vrf_route[i].vrf_id == vrf_id &&
		    g_hw_l3_vrf_route[i].af == af &&
		    g_hw_l3_vrf_route[i].prefixlen == prefixlen &&
		    memcmp(g_hw_l3_vrf_route[i].prefix, prefix,
			   hw_l3_vrf_prefix_len(af)) == 0)
			return &g_hw_l3_vrf_route[i];
	}
	return NULL;
}

/* 网关是否为空（无地址）：v6 比 16B 全 0，v4 比 s_addr==0 */
static int hw_l3_vrf_gate_is_zero(const struct hsl_nexthop *nh)
{
	static const uint8_t zero16[16];

	if (nh->family == AF_INET6)
		return memcmp(&nh->gate.ipv6, zero16, sizeof(nh->gate.ipv6)) == 0;
	return nh->gate.ipv4.s_addr == 0;
}

/* 由网关组 hsl_prefix_t 探测键（v4 /32 或 v6 /128，family 决定查表） */
static void hw_l3_vrf_nhprefix(const struct hsl_nexthop *nh, hsl_prefix_t *p)
{
	if (nh->family == AF_INET6) {
		p->family = AF_INET6;
		p->prefixlen = 128;
		memcpy(&p->u.prefix6, &nh->gate.ipv6, sizeof(nh->gate.ipv6));
	} else {
		p->family = AF_INET;
		p->prefixlen = 32;
		p->u.prefix4 = nh->gate.ipv4.s_addr;
	}
}

/* 网关 MAC 探测：查 HSL nh 表（v4/v6 各一张）得到 MAC。
 * 返回 1 = 已解析并回填 mac，0 = 未解析/无网关。 */
static int hw_l3_vrf_probe_mac(const struct hsl_nexthop *nh,
			       uint8_t mac[RUNOS_MAC_LEN])
{
	hsl_prefix_t nhprefix;
	hsl_nh_entry_t *hnh;

	if (hw_l3_vrf_gate_is_zero(nh))
		return 0;
	hw_l3_vrf_nhprefix(nh, &nhprefix);
	hnh = hsl_rt_lookup_nh__(nh->ifindex, &nhprefix);
	if (hnh != NULL &&
	    (hnh->mac[0] | hnh->mac[1] | hnh->mac[2] |
	     hnh->mac[3] | hnh->mac[4] | hnh->mac[5])) {
		memcpy(mac, hnh->mac, RUNOS_MAC_LEN);
		return 1;
	}
	return 0;
}

/* 前缀 printk 化（prefix 网络序原样：v4 低 4B / v6 16B），
 * 供错误/诊断 printk 复用，避免每处按 af 拆两行。 */
#define HW_L3_VRF_PFMT(af)  ((af) == AF_INET6 ? "%pI6" : "%pI4")
#define HW_L3_VRF_PARG(pre) ((const void *)(pre))

/* 回滚本次 add 已创建的 push / ECMP 绑定（IPUC 邻接 nexthop 全局
 * 复用，不释放）。is_ecmp=1 时释放 ECMP 组；否则逐个释放 push 成员。 */
static void hw_l3_vrf_undo_binding(uint8_t is_ecmp, uint32_t nhid,
				   const uint32_t *labels, uint8_t label_num,
				   const uint8_t *member_is_push,
				   const uint32_t *member_ifindex,
				   uint8_t member_num)
{
	int j;

	if (is_ecmp) {
		hw_mpls_ecmp_release(nhid);
		return;
	}
	for (j = 0; j < member_num; j++) {
		if (member_is_push[j])
			hw_nh_del_route_push_stack(labels, label_num,
						   member_ifindex[j]);
	}
}

/* 释放缓存条目持有的 push / ECMP 绑定（del / ARP 回退 / 更新复用）。
 * IPUC 邻接 nhid 全局复用，不释放。 */
static void hw_l3_vrf_route_release_binding(struct hw_l3_vrf_route_entry *entry)
{
	if (entry->is_ecmp) {
		hw_mpls_ecmp_release(entry->nhid);
	} else if (entry->out_label != 0 && entry->ifindex != 0) {
		hw_nh_del_route_push_stack(entry->label_stack,
					   entry->label_depth > 0 ?
					   entry->label_depth : 1,
					   entry->ifindex);
	}
}

int hw_l3_vrf_create(uint32_t vrf_id, const char *name)
{
	struct hw_l3_vrf_entry *entry;
	int i;

	if (hw_l3_vrf_id_to_sdk(vrf_id) < 0)
		return RUNOS_PARAM;

	mutex_lock(&hw_l3_vrf_mutex);

	if (hw_l3_vrf_find(vrf_id) != NULL) {
		/* 幂等：已存在视为成功 */
		mutex_unlock(&hw_l3_vrf_mutex);
		return RUNOS_OK;
	}

	entry = NULL;
	for (i = 0; i < HW_L3_VRF_MAX; i++) {
		if (!g_hw_l3_vrf[i].used) {
			entry = &g_hw_l3_vrf[i];
			break;
		}
	}
	if (entry == NULL) {
		mutex_unlock(&hw_l3_vrf_mutex);
		return RUNOS_FULL;
	}

	memset(entry, 0, sizeof(*entry));
	entry->vrf_id = vrf_id;
	if (name != NULL)
		snprintf(entry->name, sizeof(entry->name), "%s", name);
	else
		snprintf(entry->name, sizeof(entry->name), "vrf%u", vrf_id);
	entry->used = 1;

	mutex_unlock(&hw_l3_vrf_mutex);

	printk("hw_l3: VRF %u (%s) created sdk_vrf=%d\n",
	       vrf_id, entry->name, hw_l3_vrf_id_to_sdk(vrf_id));
	return RUNOS_OK;
}

int hw_l3_vrf_delete(uint32_t vrf_id)
{
	struct hw_l3_vrf_entry *entry;

	mutex_lock(&hw_l3_vrf_mutex);

	entry = hw_l3_vrf_find(vrf_id);
	if (entry == NULL) {
		/* 幂等 */
		mutex_unlock(&hw_l3_vrf_mutex);
		return RUNOS_OK;
	}

	memset(entry, 0, sizeof(*entry));

	mutex_unlock(&hw_l3_vrf_mutex);

	printk("hw_l3: VRF %u deleted\n", vrf_id);
	return RUNOS_OK;
}

int hw_l3_vrf_lookup(uint32_t vrf_id)
{
	struct hw_l3_vrf_entry *entry;
	int ret = 0;

	if (hw_l3_vrf_id_to_sdk(vrf_id) < 0)
		return 0;

	mutex_lock(&hw_l3_vrf_mutex);
	entry = hw_l3_vrf_find(vrf_id);
	if (entry != NULL)
		ret = 1;
	mutex_unlock(&hw_l3_vrf_mutex);

	return ret;
}

int hw_l3_if_vrf_bind(uint32_t ifindex, uint32_t vrf_id)
{
    hsl_if_t *ifp;
    uint16_t vid, l3if_id;
    uint8_t unit;
    int ret = 0;

    ifp = hsl_get_if_by_ifindex(ifindex);
    if (!ifp) {
        printk(KERN_ERR "hw_l3: ifindex %u not found\n", ifindex);
        return RUNOS_PORT;
    }

    vid = ifp->l3_intf.vid;
    l3if_id = hw_l3_vlanid_to_l3ifid(vid);
    if (l3if_id <= 0) {
        printk(KERN_ERR "hw_l3: invalid vid %u for ifindex %u\n", vid, ifindex);
        return RUNOS_PARAM;
    }

    if (HSL_DEBUG_CHECK(HSL_DBG_L3)) {
        printk("hw_l3: bind ifindex %u (l3if %u) to vrf %u\n",
               ifindex, l3if_id, vrf_id);
    }

    FOREACH_UNIT(unit) {
        ret = ctcs_l3if_set_property(unit, l3if_id,
                                     CTC_L3IF_PROP_VRF_EN,
                                     (vrf_id != 0) ? 1 : 0);
        if (ret < 0) {
            printk(KERN_ERR "hw_l3: set VRF_EN failed on unit %u, l3if %u, ret %d\n",
                   unit, l3if_id, ret);
            goto out;
        }

        ret = ctcs_l3if_set_property(unit, l3if_id,
                                     CTC_L3IF_PROP_VRF_ID,
                                     vrf_id);
        if (ret < 0) {
            printk(KERN_ERR "hw_l3: set VRF_ID failed on unit %u, l3if %u, ret %d\n",
                   unit, l3if_id, ret);
            goto out;
        }

        /* MPLS enable: enable only when VRF is not default (L3VPN) */
        ret = ctcs_l3if_set_property(unit, l3if_id,
                                     CTC_L3IF_PROP_MPLS_EN,
                                     (vrf_id != 0) ? 1 : 0);
        if (ret < 0) {
            printk(KERN_ERR "hw_l3: set MPLS_EN failed on unit %u, l3if %u, ret %d\n",
                   unit, l3if_id, ret);
            goto out;
        }
    }

    /* Update software interface vrf_id */
    ifp->vrf_id = vrf_id;

out:
    return ret;
}

int hw_l3_route_add_vrf(uint32_t vrf_id, uint8_t af,
			const void *prefix, uint8_t prefixlen,
			const struct hsl_nexthop *nh_list, uint8_t nh_count,
			const mpls_label_t *label_stack, uint8_t label_depth)
{
	int unit;
	int sdk_vrf;
	int retval = RUNOS_OK;
	int i, j;
	uint32_t nhid = 0;
	uint8_t mac[RUNOS_MAC_LEN] = {0};
	uint8_t mac_valid = 0;
	uint32_t out_label = 0;
	uint32_t out_ifindex = 0;
	uint8_t placeholder = 0;
	uint8_t is_ecmp = 0;
	uint8_t member_num = 0;
	uint32_t member_nhids[HW_L3_VRF_NH_MAX];
	uint8_t  member_is_push[HW_L3_VRF_NH_MAX];
	uint32_t member_ifindex[HW_L3_VRF_NH_MAX];
	uint8_t  all_resolved = 1;
	uint8_t  label_num = 0;
	uint32_t labels[3] = {0};
	uint8_t  saved_nh_count;
	struct hsl_nexthop local_nhs[HW_L3_VRF_NH_MAX];
	hsl_prefix_t nhprefix;
	hsl_nh_entry_t *hnh = NULL;
	ctc_ipuc_param_t new;
	ctc_ipuc_param_t old;
	struct hw_l3_vrf_route_entry *entry;
	int ipuc_idx = 0;
	/* 第 k 个 unit 上的表项是本次新加的（bit k）；原地更新的不算，
	 * 回滚时不能删，否则该 unit 的前缀会彻底没有表项 */
	uint32_t ipuc_new_mask = 0;

	/* ---------- 入口参数 ---------- */
	if (prefix == NULL || nh_list == NULL || nh_count == 0) {
		printk("hw_l3: vrf [trace] ENTER bad-param vrf=%u af=%u prefix=%p nh_list=%p nh_count=%u\n",
		       vrf_id, af, prefix, nh_list, nh_count);
		return RUNOS_PARAM;
	}
	if (af != AF_INET && af != AF_INET6) {
		printk("hw_l3: vrf [trace] ENTER bad-af vrf=%u af=%u\n",
		       vrf_id, af);
		return RUNOS_PARAM;
	}

	if (nh_count > HW_L3_VRF_NH_MAX) {
		printk("hw_l3: vrf [trace] nh_count=%u clamped to %u vrf=%u\n",
		       nh_count, HW_L3_VRF_NH_MAX, vrf_id);
		nh_count = HW_L3_VRF_NH_MAX;
	}
	memcpy(local_nhs, nh_list, nh_count * sizeof(struct hsl_nexthop));

	sdk_vrf = hw_l3_vrf_id_to_sdk(vrf_id);
	if (sdk_vrf < 0) {
		printk("hw_l3: vrf [trace] vrf_id_to_sdk FAILED vrf=%u\n", vrf_id);
		return RUNOS_PARAM;
	}

	if (af == AF_INET) {
		printk("hw_l3: vrf [trace] ENTER vrf=%u sdk_vrf=%d af=INET prefix=%pI4/%u nh_count=%u label_stack=%p label_depth=%u\n",
		       vrf_id, sdk_vrf, HW_L3_VRF_PARG(prefix), prefixlen,
		       nh_count, label_stack, label_depth);
	} else {
		printk("hw_l3: vrf [trace] ENTER vrf=%u sdk_vrf=%d af=INET6 prefix=%pI6/%u nh_count=%u label_stack=%p label_depth=%u\n",
		       vrf_id, sdk_vrf, HW_L3_VRF_PARG(prefix), prefixlen,
		       nh_count, label_stack, label_depth);
	}

	label_num = (label_stack != NULL && label_depth > 0) ? label_depth : 0;
	if (label_num > 3)
		label_num = 3;
	if (label_num > 0)
		memcpy(labels, label_stack, label_num * sizeof(uint32_t));
	printk("hw_l3: vrf [trace] label_num=%u labels=[%u,%u,%u] vrf=%u\n",
	       label_num, labels[0], labels[1], labels[2], vrf_id);

	mutex_lock(&hw_l3_vrf_mutex);

	/* ---------- 第一遍：MAC 解析 ---------- */
	saved_nh_count = nh_count;
	memset(member_nhids, 0, sizeof(member_nhids));
	memset(member_is_push, 0, sizeof(member_is_push));
	memset(member_ifindex, 0, sizeof(member_ifindex));
	for (i = 0; i < nh_count; i++) {
		const struct hsl_nexthop *nh = &local_nhs[i];

		if (hw_l3_vrf_gate_is_zero(nh)) {
			printk("hw_l3: vrf [trace] pass1[%d] gate_is_zero, skip vrf=%u ifindex=0x%x\n",
			       i, vrf_id, nh->ifindex);
			continue;
		}
		mac_valid = 0;
		memset(mac, 0, sizeof(mac));
		mac_valid = hw_l3_vrf_probe_mac(nh, mac);
		if (!mac_valid) {
			printk("hw_l3: vrf [trace] pass1[%d] probe_mac UNRESOLVED vrf=%u ifindex=0x%x -> placeholder\n",
			       i, vrf_id, nh->ifindex);
			all_resolved = 0;
			break;
		}
		printk("hw_l3: vrf [trace] pass1[%d] probe_mac OK vrf=%u ifindex=0x%x mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
		       i, vrf_id, nh->ifindex,
		       mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	}
	printk("hw_l3: vrf [trace] pass1 done all_resolved=%u vrf=%u\n",
	       all_resolved, vrf_id);

	/* ---------- 第二遍：逐路径创建成员 ---------- */
	if (all_resolved) {
		for (i = 0; i < nh_count; i++) {
			const struct hsl_nexthop *nh = &local_nhs[i];
			uint32_t tmp_nhid = 0;
			uint32_t nh_label = 0;
			uint8_t is_push = 0;
			int nh_ok = 0;

			if (hw_l3_vrf_gate_is_zero(nh)) {
				printk("hw_l3: vrf [trace] pass2[%d] gate_is_zero, skip vrf=%u\n",
				       i, vrf_id);
				continue;
			}

			if (label_num > 0)
				nh_label = labels[0];
			else if (nh->is_mpls)
				nh_label = nh->out_label;

			printk("hw_l3: vrf [trace] pass2[%d] start vrf=%u ifindex=0x%x is_mpls=%u nh_label=%u family=%u\n",
			       i, vrf_id, nh->ifindex, nh->is_mpls,
			       nh_label, nh->family);

			mac_valid = 0;
			hnh = NULL;
			if (!hw_l3_vrf_gate_is_zero(nh)) {
				hw_l3_vrf_nhprefix(nh, &nhprefix);
				hnh = hsl_rt_lookup_nh__(nh->ifindex, &nhprefix);
				if (hnh != NULL &&
				    (hnh->mac[0] | hnh->mac[1] | hnh->mac[2] |
				     hnh->mac[3] | hnh->mac[4] | hnh->mac[5])) {
					memcpy(mac, hnh->mac, RUNOS_MAC_LEN);
					mac_valid = 1;
				}
			}
			printk("hw_l3: vrf [trace] pass2[%d] hnh=%p mac_valid=%u l2_ifindex=0x%x vrf=%u\n",
			       i, hnh, mac_valid,
			       hnh ? hnh->l2_ifindex : 0, vrf_id);

			if (nh_label != 0) {
				if (nh->family == AF_INET6) {
					printk("hw_l3: vrf [trace] pass2[%d] v6-gate labeled unsupported vrf=%u label=%u ifindex=0x%x\n",
					       i, vrf_id, nh_label, nh->ifindex);
				} else if (mac_valid &&
					   hw_nh_add_route_push_stack(
						   labels, label_num,
						   ntohl(nh->gate.ipv4.s_addr),
						   nh->ifindex, mac,
						   &tmp_nhid) == RUNOS_OK) {
					is_push = 1;
					nh_ok = 1;
					printk("hw_l3: vrf [trace] pass2[%d] push_nh OK vrf=%u nhid=%u gate=%pI4 label=%u ifindex=0x%x\n",
					       i, vrf_id, tmp_nhid,
					       &nh->gate.ipv4, nh_label, nh->ifindex);
				} else {
					printk("hw_l3: vrf [trace] pass2[%d] push_nh FAILED vrf=%u label=%u ifindex=0x%x mac_valid=%u\n",
					       i, vrf_id, nh_label, nh->ifindex, mac_valid);
				}
			} else if (!mac_valid) {
				printk("hw_l3: vrf [trace] pass2[%d] plain nh but mac UNRESOLVED -> placeholder vrf=%u ifindex=0x%x\n",
				       i, vrf_id, nh->ifindex);
				nh_ok = 0;
			} else {
				do {
					hsl_if_t *ifp = hsl_get_if_by_ifindex(nh->ifindex);
					ctc_ip_nh_param_t nh_param;
					u_int16_t vid;
					u_int32_t ifindex;
					hw_lport_t hport;
					int unit_ok = 1;

					if (ifp == NULL) {
						printk("hw_l3: vrf [trace] pass2[%d] hsl_get_if_by_ifindex NULL vrf=%u ifindex=0x%x\n",
						       i, vrf_id, nh->ifindex);
						break;
					}

					vid = ifp->l3_intf.vid;
					memset(&nh_param, 0, sizeof(nh_param));
					nh_param.dsnh_offset = 0;
					memcpy(&nh_param.mac, mac, RUNOS_MAC_LEN);
					nh_param.oif.vid = vid;

					if (hnh->l2_ifindex > 0 &&
					    hsl_port_is_running(hnh->l2_ifindex)) {
						ifindex = hnh->l2_ifindex;
						printk("hw_l3: vrf [trace] pass2[%d] use l2_ifindex=0x%x vrf=%u\n",
						       i, ifindex, vrf_id);
					} else {
						if (hw_mac_get_port(vid, mac, &ifindex) != RUNOS_OK ||
						    ifindex == UIF_IFINDEX_INVALID) {
							printk("hw_l3: vrf [trace] pass2[%d] hw_mac_get_port FAILED vid=%u mac=%02x:%02x:%02x:%02x:%02x:%02x vrf=%u\n",
							       i, vid,
							       mac[0], mac[1], mac[2],
							       mac[3], mac[4], mac[5],
							       vrf_id);
							break;
						}
						printk("hw_l3: vrf [trace] pass2[%d] hw_mac_get_port OK ifindex=0x%x vrf=%u\n",
						       i, ifindex, vrf_id);
					}

					if (UIF_IFINDEX_IS_TRUNK(ifindex)) {
						nh_param.oif.gport =
						    CTC_MAP_TID_TO_GPORT(HW_TRUNK_ID(UIF_GET_TRUNK_ID(ifindex)));
						printk("hw_l3: vrf [trace] pass2[%d] trunk ifindex=0x%x tid=%u gport=0x%x vrf=%u\n",
						       i, ifindex, UIF_GET_TRUNK_ID(ifindex),
						       nh_param.oif.gport, vrf_id);
					} else {
						ifindex2hport(ifindex, &hport);
						nh_param.oif.gport = hport.port;
						printk("hw_l3: vrf [trace] pass2[%d] port ifindex=0x%x hport=0x%x vrf=%u\n",
						       i, ifindex, nh_param.oif.gport, vrf_id);
					}

					printk("hw_l3: vrf [trace] pass2[%d] ipuc-nh param vid=%u mac=%02x:%02x:%02x:%02x:%02x:%02x gport=0x%x vrf=%u\n",
					       i, vid,
					       mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
					       nh_param.oif.gport, vrf_id);

					FOREACH_UNIT(unit)
					{
						retval = hw_l3_nexthop_find(unit, nh_param, &tmp_nhid);
						printk("hw_l3: vrf [trace] pass2[%d] unit=%d nexthop_find ret=%d nhid=%u vrf=%u\n",
						       i, unit, retval, tmp_nhid, vrf_id);
						if (retval == RUNOS_FULL) {
							printk("hw_l3: vrf [trace] pass2[%d] unit=%d nexthop FULL vrf=%u\n",
							       i, unit, vrf_id);
							unit_ok = 0;
							break;
						}
						if (retval == RUNOS_EMPTY &&
						    ctcs_nh_add_ipuc(unit, tmp_nhid, &nh_param) != CTC_E_NONE) {
							printk("hw_l3: vrf [trace] pass2[%d] unit=%d nh_add_ipuc FAILED nhid=%u vrf=%u\n",
							       i, unit, tmp_nhid, vrf_id);
							unit_ok = 0;
							break;
						}
						if (retval == RUNOS_EMPTY)
							printk("hw_l3: vrf [trace] pass2[%d] unit=%d nh_add_ipuc OK nhid=%u vrf=%u\n",
							       i, unit, tmp_nhid, vrf_id);
					}
					if (!unit_ok)
						break;
					nh_ok = 1;
				} while (0);
			}

			if (nh_ok) {
				member_nhids[member_num] = tmp_nhid;
				member_is_push[member_num] = is_push;
				member_ifindex[member_num] = nh->ifindex;
				member_num++;
				if (is_push) {
					out_label = nh_label;
					out_ifindex = nh->ifindex;
				}
				printk("hw_l3: vrf [trace] pass2[%d] member added idx=%u nhid=%u is_push=%u ifindex=0x%x vrf=%u\n",
				       i, member_num - 1, tmp_nhid, is_push,
				       nh->ifindex, vrf_id);
			} else {
				for (j = 0; j < member_num; j++) {
					if (member_is_push[j])
						hw_nh_del_route_push_stack(
							labels, label_num,
							member_ifindex[j]);
				}
				printk("hw_l3: vrf [trace] pass2[%d] member FAILED, rollback %u members -> placeholder vrf=%u\n",
				       i, member_num, vrf_id);
				member_num = 0;
				placeholder = 1;
				break;
			}
		}
		printk("hw_l3: vrf [trace] pass2 done placeholder=%u member_num=%u vrf=%u\n",
		       placeholder, member_num, vrf_id);

		if (!placeholder) {
			if (member_num > 1) {
				printk("hw_l3: vrf [trace] ECMP create start member_num=%u vrf=%u\n",
				       member_num, vrf_id);
				if (hw_mpls_ecmp_create(member_nhids,
							member_is_push,
							member_num,
							&nhid) == RUNOS_OK) {
					is_ecmp = 1;
					printk("hw_l3: vrf [trace] ECMP create OK nhid=%u vrf=%u\n",
					       nhid, vrf_id);
				} else {
					if (af == AF_INET)
						printk("hw_l3: vrf ECMP create FAILED vrf=%u prefix=%pI4/%u\n",
						       vrf_id, HW_L3_VRF_PARG(prefix), prefixlen);
					else
						printk("hw_l3: vrf ECMP create FAILED vrf=%u prefix=%pI6/%u\n",
						       vrf_id, HW_L3_VRF_PARG(prefix), prefixlen);
					for (j = 0; j < member_num; j++) {
						if (member_is_push[j])
							hw_nh_del_route_push_stack(
								labels, label_num,
								member_ifindex[j]);
					}
					member_num = 0;
					placeholder = 1;
				}
			} else if (member_num == 1) {
				nhid = member_nhids[0];
				printk("hw_l3: vrf [trace] single-path nhid=%u vrf=%u\n",
				       nhid, vrf_id);
			} else {
				printk("hw_l3: vrf [trace] all_resolved but no member (unexpected) -> placeholder vrf=%u\n",
				       vrf_id);
				placeholder = 1;
			}
		}
	} else {
		placeholder = 1;
		printk("hw_l3: vrf [trace] some nh unresolved -> placeholder vrf=%u\n",
		       vrf_id);
	}

	if (placeholder) {
		nhid = CTC_NH_RESERVED_NHID_FOR_TOCPU;
		printk("hw_l3: vrf [trace] use placeholder nhid=%u vrf=%u\n",
		       nhid, vrf_id);
	} else if (nhid == 0) {
		nhid = CTC_NH_RESERVED_NHID_FOR_TOCPU;
		printk("hw_l3: vrf [trace] nhid==0 defensive -> placeholder nhid=%u vrf=%u\n",
		       nhid, vrf_id);
	}

	/* ---------- 构造 IPUC ---------- */
	memset(&new, 0, sizeof(ctc_ipuc_param_t));
	new.vrf_id = (uint32_t)sdk_vrf;
	new.nh_id = nhid;
	new.route_flag = CTC_IPUC_FLAG_CONNECT;
	new.masklen = prefixlen;
	if (af == AF_INET6) {
		const uint32_t *w = (const uint32_t *)prefix;

		new.ip.ipv6[0] = ntohl(w[0]);
		new.ip.ipv6[1] = ntohl(w[1]);
		new.ip.ipv6[2] = ntohl(w[2]);
		new.ip.ipv6[3] = ntohl(w[3]);
		new.ip_ver = CTC_IP_VER_6;
	} else {
		new.ip.ipv4 = ntohl(*(const uint32_t *)prefix);
		new.ip_ver = CTC_IP_VER_4;
	}
	printk("hw_l3: vrf [trace] IPUC new vrf_id=%u nh_id=%u masklen=%u ip_ver=%u vrf=%u\n",
	       new.vrf_id, new.nh_id, new.masklen, new.ip_ver, vrf_id);

	FOREACH_UNIT(unit)
	{
		ipuc_idx++;
		printk("hw_l3: vrf [trace] unit=%d ipuc_get start (idx=%d) vrf=%u\n",
		       unit, ipuc_idx, vrf_id);

		memcpy(&old, &new, sizeof(ctc_ipuc_param_t));
		retval = ctcs_ipuc_get(unit, &old);
		if (retval != CTC_E_NONE && retval != CTC_E_NOT_EXIST) {
			if (af == AF_INET)
				printk("hw_l3: vrf ipuc get FAILED vrf=%u prefix=%pI4/%u unit=%d ret=%d\n",
				       vrf_id, HW_L3_VRF_PARG(prefix), prefixlen, unit, retval);
			else
				printk("hw_l3: vrf ipuc get FAILED vrf=%u prefix=%pI6/%u unit=%d ret=%d\n",
				       vrf_id, HW_L3_VRF_PARG(prefix), prefixlen, unit, retval);
			goto ipuc_fail;
		}
		if (retval == CTC_E_NOT_EXIST) {
			printk("hw_l3: vrf [trace] unit=%d ipuc NOT_EXIST -> add nhid=%u vrf=%u\n",
			       unit, new.nh_id, vrf_id);
			retval = ctcs_ipuc_add(unit, &new);
			if (retval != CTC_E_NONE) {
				if (af == AF_INET)
					printk("hw_l3: vrf ipuc add FAILED vrf=%u prefix=%pI4/%u ret=%d\n",
					       vrf_id, HW_L3_VRF_PARG(prefix), prefixlen, retval);
				else
					printk("hw_l3: vrf ipuc add FAILED vrf=%u prefix=%pI6/%u ret=%d\n",
					       vrf_id, HW_L3_VRF_PARG(prefix), prefixlen, retval);
				goto ipuc_fail;
			}
			printk("hw_l3: vrf [trace] unit=%d ipuc add OK nhid=%u vrf=%u\n",
			       unit, new.nh_id, vrf_id);
			ipuc_new_mask |= (1u << (ipuc_idx - 1));
		} else if (old.nh_id != new.nh_id) {
			/* 原地更新下一跳：ctcs_ipuc_add() 命中已有表项时，SDK 走
			 * _sys_usw_ipuc_update() -> sys_tsingma_nalpm_update()
			 * -> _sys_nalpm_write_sram(is_update=1)，只改写该 snake
			 * entry 的 nexthop 字段，ipAddr/maskLen 保持不变。
			 * 绝不能先 ctcs_ipuc_remove() 再 add：ALPM 的 delete/re-add
			 * 会释放并重新分配 item 的 sram_idx（实测 pointer 4 -> 8，
			 * 即整条 item 搬家），这颗 N-ALPM 的实现对这套序列并不幂等；
			 * 而且一旦 add 失败就先留下一个黑洞。原地更新失败时旧表项
			 * 保持不动，比"先删后加"安全得多。 */
			printk("hw_l3: vrf [trace] unit=%d ipuc EXIST old_nhid=%u new_nhid=%u -> update in place vrf=%u\n",
			       unit, old.nh_id, new.nh_id, vrf_id);
			retval = ctcs_ipuc_add(unit, &new);
			if (retval != CTC_E_NONE) {
				if (af == AF_INET)
					printk("hw_l3: vrf ipuc update FAILED vrf=%u prefix=%pI4/%u unit=%d old_nhid=%u new_nhid=%u ret=%d\n",
					       vrf_id, HW_L3_VRF_PARG(prefix), prefixlen,
					       unit, old.nh_id, new.nh_id, retval);
				else
					printk("hw_l3: vrf ipuc update FAILED vrf=%u prefix=%pI6/%u unit=%d old_nhid=%u new_nhid=%u ret=%d\n",
					       vrf_id, HW_L3_VRF_PARG(prefix), prefixlen,
					       unit, old.nh_id, new.nh_id, retval);
				goto ipuc_fail;
			}
			printk("hw_l3: vrf [trace] unit=%d ipuc update OK old_nhid=%u new_nhid=%u vrf=%u\n",
			       unit, old.nh_id, new.nh_id, vrf_id);
		} else {
			printk("hw_l3: vrf [trace] unit=%d ipuc EXIST same nhid=%u, skip vrf=%u\n",
			       unit, old.nh_id, vrf_id);
		}
	}

	/* ---------- 软件缓存更新 ---------- */
	entry = hw_l3_vrf_route_find(vrf_id, prefix, prefixlen, af);
	if (entry != NULL) {
		printk("hw_l3: vrf [trace] cache HIT vrf=%u entry=%p used=%u is_ecmp=%u out_label=%u ifindex=0x%x vrf=%u\n",
		       vrf_id, entry, entry->used, entry->is_ecmp,
		       entry->out_label, entry->ifindex, vrf_id);
		if (entry->is_ecmp ||
		    (entry->out_label != 0 && entry->ifindex != 0)) {
			printk("hw_l3: vrf [trace] cache release old binding vrf=%u\n", vrf_id);
			hw_l3_vrf_route_release_binding(entry);
		}
	} else {
		for (i = 0; i < HW_L3_VRF_ROUTE_MAX; i++) {
			if (!g_hw_l3_vrf_route[i].used) {
				entry = &g_hw_l3_vrf_route[i];
				break;
			}
		}
		if (entry != NULL)
			printk("hw_l3: vrf [trace] cache MISS, alloc slot=%d vrf=%u\n",
			       (int)(entry - g_hw_l3_vrf_route), vrf_id);
	}
	if (entry == NULL) {
		if (af == AF_INET)
			printk("hw_l3: vrf route cache FULL vrf=%u prefix=%pI4%u, rollback\n",
			       vrf_id, HW_L3_VRF_PARG(prefix), prefixlen);
		else
			printk("hw_l3: vrf route cache FULL vrf=%u prefix=%pI6/%u, rollback\n",
			       vrf_id, HW_L3_VRF_PARG(prefix), prefixlen);
		hw_l3_vrf_undo_binding(is_ecmp, nhid, labels, label_num,
				      member_is_push, member_ifindex,
				      member_num);
		memset(&old, 0, sizeof(old));
		old.vrf_id = (uint32_t)sdk_vrf;
		old.nh_id = nhid;
		old.masklen = prefixlen;
		if (af == AF_INET6) {
			const uint32_t *w = (const uint32_t *)prefix;

			old.ip.ipv6[0] = ntohl(w[0]);
			old.ip.ipv6[1] = ntohl(w[1]);
			old.ip.ipv6[2] = ntohl(w[2]);
			old.ip.ipv6[3] = ntohl(w[3]);
			old.ip_ver = CTC_IP_VER_6;
		} else {
			old.ip.ipv4 = ntohl(*(const uint32_t *)prefix);
			old.ip_ver = CTC_IP_VER_4;
		}
		FOREACH_UNIT(unit)
		{
			if (ctcs_ipuc_get(unit, &old) == CTC_E_NONE) {
				if (ctcs_ipuc_remove(unit, &old) != CTC_E_NONE) {
					if (af == AF_INET)
						printk("hw_l3: cache FULL rollback: ipuc remove FAILED unit=%d vrf=%u prefix=%pI4/%u nhid=%u（芯片残留表项指向已释放的绑定）\n",
						       unit, vrf_id,
						       HW_L3_VRF_PARG(prefix),
						       prefixlen, nhid);
					else
						printk("hw_l3: cache FULL rollback: ipuc remove FAILED unit=%d vrf=%u prefix=%pI6/%u nhid=%u（芯片残留表项指向已释放的绑定）\n",
						       unit, vrf_id,
						       HW_L3_VRF_PARG(prefix),
						       prefixlen, nhid);
				} else {
					printk("hw_l3: vrf [trace] cache FULL rollback: ipuc remove OK unit=%d nhid=%u vrf=%u\n",
					       unit, nhid, vrf_id);
				}
			}
		}
		retval = RUNOS_FULL;
		goto out;
	}

	memset(entry, 0, sizeof(*entry));
	entry->vrf_id = vrf_id;
	memcpy(entry->prefix, prefix, hw_l3_vrf_prefix_len(af));
	entry->prefixlen = prefixlen;
	entry->af = af;
	entry->out_label = out_label;
	entry->ifindex = out_ifindex;
	entry->nhid = nhid;
	entry->placeholder = placeholder;
	entry->is_ecmp = is_ecmp;
	entry->nh_count = saved_nh_count;
	entry->label_depth = label_num;
	if (label_num > 0)
		memcpy(entry->label_stack, labels, label_num * sizeof(uint32_t));
	memcpy(entry->nhs, local_nhs, saved_nh_count * sizeof(struct hsl_nexthop));
	entry->used = 1;

	printk("hw_l3: vrf [trace] cache updated slot=%d vrf=%u nhid=%u is_ecmp=%u placeholder=%u out_label=%u out_ifindex=0x%x nh_count=%u label_depth=%u\n",
	       (int)(entry - g_hw_l3_vrf_route), vrf_id, nhid, is_ecmp,
	       placeholder, out_label, out_ifindex, saved_nh_count, label_num);

	retval = RUNOS_OK;
	goto out;

ipuc_fail:
	{
		int k = 0;
		int fail_unit = unit;

		printk("hw_l3: vrf [trace] ipuc_fail enter fail_unit=%d ipuc_idx=%d vrf=%u\n",
		       fail_unit, ipuc_idx, vrf_id);

		FOREACH_UNIT(unit)
		{
			ctc_ipuc_param_t del;

			if (k >= ipuc_idx)
				break;
			if (!(ipuc_new_mask & (1u << k))) {
				/* 这个 unit 上是原地更新，表项本来就在：删掉
				 * 会让该 unit 的前缀彻底没有表项（掉进 default
				 * -> DROP），跳过 */
				printk("hw_l3: vrf [trace] ipuc rollback skip unit=%d (pre-existing, updated in place) vrf=%u\n",
				       unit, vrf_id);
				k++;
				continue;
			}
			memcpy(&del, &new, sizeof(del));
			if (ctcs_ipuc_get(unit, &del) == CTC_E_NONE &&
			    del.nh_id == new.nh_id) {
				if (ctcs_ipuc_remove(unit, &del) != CTC_E_NONE) {
					if (af == AF_INET)
						printk("hw_l3: vrf ipuc rollback remove FAILED unit=%d vrf=%u prefix=%pI4/%u nhid=%u（芯片残留表项）\n",
						       unit, vrf_id,
						       HW_L3_VRF_PARG(prefix),
						       prefixlen, del.nh_id);
					else
						printk("hw_l3: vrf ipuc rollback remove FAILED unit=%d vrf=%u prefix=%pI6/%u nhid=%u（芯片残留表项）\n",
						       unit, vrf_id,
						       HW_L3_VRF_PARG(prefix),
						       prefixlen, del.nh_id);
				} else {
					printk("hw_l3: vrf [trace] ipuc rollback remove OK unit=%d nhid=%u vrf=%u\n",
					       unit, del.nh_id, vrf_id);
				}
			} else {
				printk("hw_l3: vrf [trace] ipuc rollback skip unit=%d (get!=OK or nhid mismatch) vrf=%u\n",
				       unit, vrf_id);
			}
			k++;
		}

		/* 原地更新失败时旧表项仍在硬件里（SDK 的 _sys_usw_ipuc_update()
		 * 会 restore old binding），无需再写回 prev */
	}

	hw_l3_vrf_undo_binding(is_ecmp, nhid, labels, label_num,
			      member_is_push, member_ifindex, member_num);
	printk("hw_l3: vrf [trace] ipuc_fail undo_binding done is_ecmp=%u nhid=%u member_num=%u vrf=%u ret=ERR\n",
	       is_ecmp, nhid, member_num, vrf_id);
	retval = RUNOS_ERR;

out:
	printk("hw_l3: vrf [trace] EXIT vrf=%u sdk_vrf=%d af=%u prefixlen=%u nhid=%u is_ecmp=%u placeholder=%u member_num=%u retval=%d\n",
	       vrf_id, sdk_vrf, af, prefixlen, nhid, is_ecmp, placeholder,
	       member_num, retval);
	mutex_unlock(&hw_l3_vrf_mutex);
	return retval;
}

int hw_l3_route_del_vrf(uint32_t vrf_id, uint8_t af,
			const void *prefix, uint8_t prefixlen)
{
	int unit;
	int sdk_vrf;
	int retval = RUNOS_OK;
	struct hw_l3_vrf_route_entry *entry;
	ctc_ipuc_param_t route;
	int removed = 0;

	if (prefix == NULL)
		return RUNOS_PARAM;
	if (af != AF_INET && af != AF_INET6)
		return RUNOS_PARAM;	/* 仅 IPv4/IPv6 */

	sdk_vrf = hw_l3_vrf_id_to_sdk(vrf_id);
	if (sdk_vrf < 0)
		return RUNOS_PARAM;

	mutex_lock(&hw_l3_vrf_mutex);

	entry = hw_l3_vrf_route_find(vrf_id, prefix, prefixlen, af);
	if (entry == NULL) {
		printk("hw_l3: vrf route del cache MISS vrf=%u prefix=" HW_L3_VRF_PFMT(af) "/%u\n",
		       vrf_id, HW_L3_VRF_PARG(prefix), prefixlen);
		retval = RUNOS_NOT_FOUND;
		goto out;
	}

	memset(&route, 0, sizeof(ctc_ipuc_param_t));
	route.vrf_id = (uint32_t)sdk_vrf;
	route.nh_id = entry->nhid;
	route.masklen = prefixlen;
	if (af == AF_INET6) {
		const uint32_t *w = (const uint32_t *)prefix;

		route.ip.ipv6[0] = ntohl(w[0]);
		route.ip.ipv6[1] = ntohl(w[1]);
		route.ip.ipv6[2] = ntohl(w[2]);
		route.ip.ipv6[3] = ntohl(w[3]);
		route.ip_ver = CTC_IP_VER_6;
	} else {
		route.ip.ipv4 = ntohl(*(const uint32_t *)prefix);
		route.ip_ver = CTC_IP_VER_4;
	}

	/* 先摘 IPUC，再释放绑定。顺序不能反：反过来的话，从释放到摘除
	 * 之间芯片仍指向已释放的 push / ECMP DsFwd（悬空指针）。 */
	FOREACH_UNIT(unit)
	{
		if (ctcs_ipuc_get(unit, &route) == CTC_E_NONE) {
			if (ctcs_ipuc_remove(unit, &route) != CTC_E_NONE)
				printk("hw_l3: vrf ipuc remove FAILED vrf=%u prefix=" HW_L3_VRF_PFMT(af) "/%u unit=%d nhid=%u\n",
				       vrf_id, HW_L3_VRF_PARG(prefix), prefixlen,
				       unit, route.nh_id);
			else
				removed++;
		} else {
			/* 芯片里本就没有这条（对同一键 get 不到），等价于
			 * 摘除成功 */
			removed++;
		}
	}

	/* 硬件没摘干净就不释放绑定，否则留下指向已释放绑定的表项。
	 * 软件缓存照清（芯片侧的问题由上面那条 printk 暴露）。 */
	if (removed > 0)
		hw_l3_vrf_route_release_binding(entry);

	memset(entry, 0, sizeof(*entry));
	retval = RUNOS_OK;

out:
	mutex_unlock(&hw_l3_vrf_mutex);
	return retval;
}

/* ------------------------------------------------------------------
 * VRF 路由 ARP/ND 占位升级 / 回退钩子
 *
 * 占位机制（与 hw_mpls.c 的 ILM 占位一致）：VRF 路由的任一路径
 * MAC 未解析时，IPUC 绑定 TOCPU 上送 CPU，原始路径保存在缓存
 * （entry->nhs / label_stack）；ARP（v4 网关）/ ND（v6 网关）解析
 * 后 *_resolved() 重发 hw_l3_route_add_vrf() 升级为真实 nexthop；
 * 删除/端口 down 时 *_del() 回退到 TOCPU（释放 push/ECMP 绑定）。
 * 两族钩子都由 HSL NH 模块在邻居事件上下文调用（与 ILM 消息处理
 * 同一串行化上下文，参见 hw_mpls.c）。
 *
 * 匹配/解析按“成员自身网关族”进行：v6 前缀路由可走 v4 网关（v6
 * 载荷 over v4 核），故 ARP 升级可能命中 af==AF_INET6 的条目，反之
 * ND 升级命中 af==AF_INET 条目；重发整条路由时前缀键按条目 af 取。
 * ------------------------------------------------------------------ */

/* 邻居事件成员匹配器：返回 1 表示该成员对应本次解析/撤销事件 */
typedef int (*hw_l3_vrf_nh_event_match_t)(const struct hsl_nexthop *nh,
					  uint32_t ifindex, const void *key);

/* ARP 键（host-order u32）匹配 v4 网关成员 */
static int hw_l3_vrf_arp_match(const struct hsl_nexthop *nh, uint32_t ifindex,
			       const void *key)
{
	if (nh->family != AF_INET)
		return 0;
	return nh->ifindex == ifindex &&
	       ntohl(nh->gate.ipv4.s_addr) == *(const uint32_t *)key;
}

/* ND 键（in6_addr 网络序）匹配 v6 网关成员：16B memcmp */
static int hw_l3_vrf_nd_match(const struct hsl_nexthop *nh, uint32_t ifindex,
			      const void *key)
{
	if (nh->family != AF_INET6)
		return 0;
	return nh->ifindex == ifindex &&
	       memcmp(&nh->gate.ipv6, key, sizeof(nh->gate.ipv6)) == 0;
}

/* ARP/ND 解析升级共用 worker。两阶段：
 *   1) 锁内扫描：找 placeholder 且“存在成员 == 事件键”且“全部有网关
 *      路径 MAC 已解析”的条目（直连无网关成员视为已解析，与 add 的
 *      成员判定一致，保证重发必成功）；
 *   2) 锁外逐个重发 hw_l3_route_add_vrf() 重建真实 nexthop（该函数
 *      内部自己加 hw_l3_vrf_mutex，不能在锁内调用）。
 * 重发后若条目仍占位（push/ECMP 资源失败）则放弃本次事件，避免死
 * 循环——后续邻居事件或路由更新会再次触发。 */
static void hw_l3_vrf_resolve_upgrade(uint32_t ifindex,
				      hw_l3_vrf_nh_event_match_t match,
				      const void *key, const char *who)
{
	struct hw_l3_vrf_route_entry *entry;
	uint8_t prefix[16];
	uint32_t vrf_id;
	uint8_t af;
	uint8_t prefixlen;
	uint8_t mac[RUNOS_MAC_LEN];
	int i, j, k;
	int resolved;
	int ret;

	while (1) {
		entry = NULL;
		mutex_lock(&hw_l3_vrf_mutex);
		for (i = 0; i < HW_L3_VRF_ROUTE_MAX; i++) {
			struct hw_l3_vrf_route_entry *e = &g_hw_l3_vrf_route[i];

			if (!e->used || !e->placeholder || e->nh_count == 0)
				continue;
			for (j = 0; j < e->nh_count; j++) {
				if (!match(&e->nhs[j], ifindex, key))
					continue;
				/* 该条目全部路径都已解析才升级（与 add 的
				 * 占位条件一致：任一路径未解析则整体占位） */
				resolved = 1;
				for (k = 0; k < e->nh_count; k++) {
					if (hw_l3_vrf_gate_is_zero(&e->nhs[k]))
						continue; /* 直连无网关 */
					memset(mac, 0, sizeof(mac));
					if (!hw_l3_vrf_probe_mac(&e->nhs[k], mac)) {
						resolved = 0;
						break;
					}
				}
				if (resolved) {
					entry = e;
					break;
				}
			}
			if (entry != NULL)
				break;
		}
		if (entry == NULL) {
			mutex_unlock(&hw_l3_vrf_mutex);
			break;
		}

		/* 拷贝重发所需信息后释放锁（HSL 消息处理串行化，
		 * 锁外不会并发改写该条目） */
		vrf_id = entry->vrf_id;
		af = entry->af;
		memcpy(prefix, entry->prefix, sizeof(prefix));
		prefixlen = entry->prefixlen;
		mutex_unlock(&hw_l3_vrf_mutex);

		/* 锁外重发：用缓存保存的原始路径（nhs[] 网络序
		 * gate + label_stack 原样，与消息参数一致） */
		ret = hw_l3_route_add_vrf(vrf_id, af, prefix, prefixlen,
					  entry->nhs, entry->nh_count,
					  entry->label_stack,
					  entry->label_depth);
		if (ret != RUNOS_OK) {
			printk("hw_l3: vrf %s upgrade resend FAILED vrf=%u prefix=" HW_L3_VRF_PFMT(af) "/%u ret=%d\n",
			       who, vrf_id, HW_L3_VRF_PARG(prefix), prefixlen, ret);
			break;
		}

		/* 复查：仍占位（push/ECMP 资源失败）则放弃本次事件 */
		mutex_lock(&hw_l3_vrf_mutex);
		entry = hw_l3_vrf_route_find(vrf_id, prefix, prefixlen, af);
		resolved = (entry != NULL && !entry->placeholder);
		mutex_unlock(&hw_l3_vrf_mutex);
		if (!resolved)
			break;
		/* 升级成功：继续处理下一个匹配条目 */
	}
}

/* ARP 学习钩子（由 hw_mpls_arp_resolved 调用）：nexthop 为 host-order u32 */
void hw_l3_vrf_arp_resolved(uint32_t ifindex, uint32_t nexthop)
{
	hw_l3_vrf_resolve_upgrade(ifindex, hw_l3_vrf_arp_match,
				  &nexthop, "arp");
}

/* ND 学习钩子（由 hw_l3_vrf_nd_resolved 上层邻居处理调用）：
 * nexthop 为 in6_addr 网络序原样 */
void hw_l3_vrf_nd_resolved(uint32_t ifindex, const struct in6_addr *nexthop)
{
	hw_l3_vrf_resolve_upgrade(ifindex, hw_l3_vrf_nd_match,
				  nexthop, "nd");
}

/* ARP/ND 撤销共用 worker：nexthop 邻居表项删除或端口 down（MAC 失效）
 * 时，把依赖它的非占位 VRF 路由回退到 TOCPU，释放 push / ECMP 绑定，
 * 避免继续用失效 DMAC 转发形成黑洞。路径信息保留在缓存中，解析事件
 * 重新发生后由 hw_l3_vrf_resolve_upgrade() 升级。与 hw_mpls_arp_del
 * 同一事件、同一串行化上下文。 */
static void hw_l3_vrf_rollback(uint32_t ifindex,
			       hw_l3_vrf_nh_event_match_t match,
			       const void *key)
{
	int unit;
	int i, j;
	int ret;
	int switched, total;
	int sdk_vrf;
	uint8_t af;
	ctc_ipuc_param_t new;
	ctc_ipuc_param_t old;
	struct hw_l3_vrf_route_entry *entry;

	mutex_lock(&hw_l3_vrf_mutex);

	for (i = 0; i < HW_L3_VRF_ROUTE_MAX; i++) {
		int matched = 0;

		entry = &g_hw_l3_vrf_route[i];
		if (!entry->used || entry->placeholder)
			continue;

		for (j = 0; j < entry->nh_count; j++) {
			if (match(&entry->nhs[j], ifindex, key)) {
				matched = 1;
				break;
			}
		}
		if (!matched)
			continue;

		/* IPUC 切回 TOCPU 上送（按条目前缀族编码）。
		 * 注意：release_binding 已挪到切换成功之后（见下），
		 * 否则从释放到切换之间芯片指向已释放的 DsFwd。 */
		af = entry->af;
		sdk_vrf = hw_l3_vrf_id_to_sdk(entry->vrf_id);
		if (sdk_vrf < 0) {
			/* VRF 实例已不存在（VRF 删除的消息先于 ARP 撤销
			 * 到达）：负值强转成 uint32 会拿 0xFFFFFFFF 这个
			 * 野键去找表项 —— 既切不掉真表项，又会把条目误标
			 * 成占位。跳过本条，保持原状等后续事件。 */
			printk("hw_l3: ARP rollback: vrf=%u has no sdk mapping, skip\n",
			       entry->vrf_id);
			continue;
		}
		memset(&new, 0, sizeof(ctc_ipuc_param_t));
		new.vrf_id = (uint32_t)sdk_vrf;
		new.nh_id = CTC_NH_RESERVED_NHID_FOR_TOCPU;
		new.route_flag = CTC_IPUC_FLAG_CONNECT;
		new.masklen = entry->prefixlen;
		if (af == AF_INET6) {
			const uint32_t *w = (const uint32_t *)entry->prefix;

			new.ip.ipv6[0] = ntohl(w[0]);
			new.ip.ipv6[1] = ntohl(w[1]);
			new.ip.ipv6[2] = ntohl(w[2]);
			new.ip.ipv6[3] = ntohl(w[3]);
			new.ip_ver = CTC_IP_VER_6;
		} else {
			new.ip.ipv4 = ntohl(*(const uint32_t *)entry->prefix);
			new.ip_ver = CTC_IP_VER_4;
		}

		switched = 0;
		total = 0;
		FOREACH_UNIT(unit)
		{
			memcpy(&old, &new, sizeof(ctc_ipuc_param_t));
			old.nh_id = entry->nhid;
			ret = ctcs_ipuc_get(unit, &old);
			total++;
			if (ret == CTC_E_NONE && old.nh_id != new.nh_id) {
				/* 原地更新（不 remove）：失败时旧表项仍在硬件里，
				 * 不会把前缀打成黑洞 */
				ret = ctcs_ipuc_add(unit, &new);
				if (ret != CTC_E_NONE) {
					printk("hw_l3: ARP rollback: switch to TOCPU FAILED unit=%d vrf=%u prefixlen=%u nhid=%u ret=%d\n",
					       unit, entry->vrf_id,
					       entry->prefixlen, old.nh_id, ret);
					continue;
				}
			}
			switched++;
		}

		if (switched != total) {
			/* 有 unit 没切过去：不动软件状态、不释放绑定。
			 * 宁可继续用一个已失效 DMAC（黑洞，ARP 重解析后可
			 * 恢复），也不能留下指向已释放绑定的悬空表项
			 * （不可恢复）。 */
			printk("hw_l3: ARP rollback PARTIAL vrf=%u prefixlen=%u: %d/%d unit switched, keep binding and state\n",
			       entry->vrf_id, entry->prefixlen, switched, total);
			continue;
		}

		/* 全部 unit 都切成功了：现在才释放 push / ECMP 绑定
		 * （IPUC 邻接复用不释放） */
		hw_l3_vrf_route_release_binding(entry);

		entry->nhid = CTC_NH_RESERVED_NHID_FOR_TOCPU;
		entry->is_ecmp = 0;
		entry->out_label = 0;
		entry->ifindex = 0;
		entry->placeholder = 1;
		/* nhs[] / label_stack 保留：解析事件后升级 */
	}

	mutex_unlock(&hw_l3_vrf_mutex);
}

/* ARP 撤销钩子（由 _hsl_nh_del 调用）：nexthop 为 host-order u32 */
void hw_l3_vrf_arp_del(uint32_t ifindex, uint32_t nexthop)
{
	hw_l3_vrf_rollback(ifindex, hw_l3_vrf_arp_match, &nexthop);
}

/* ND 撤销钩子（由 hsl NH 模块 v6 邻居删除路径调用）：
 * nexthop 为 in6_addr 网络序原样 */
void hw_l3_vrf_nd_del(uint32_t ifindex, const struct in6_addr *nexthop)
{
	hw_l3_vrf_rollback(ifindex, hw_l3_vrf_nd_match, nexthop);
}

int hw_l3_init(void)
{
	int unit;
	ctc_ipuc_global_property_t global_prop;
	umac_t mac;

	memset(&global_prop, 0, sizeof(ctc_ipuc_global_property_t));
	global_prop.valid_flag |= CTC_IP_GLB_PROP_V4_MARTIAN_CHECK_EN;
	global_prop.v4_martian_check_en = 1;
	
#if defined(HAVE_IPV6)
	global_prop.valid_flag |= CTC_IP_GLB_PROP_V6_MARTIAN_CHECK_EN;
	global_prop.v6_martian_check_en = 1;
#endif

	hsl_get_base_mac(mac);

    FOREACH_UNIT(unit)
	{ 	
		HW_ERR_RETURN(ctcs_ipuc_set_global_property(unit, &global_prop));
		HW_ERR_RETURN(ctcs_l3if_set_router_mac(unit, mac));
		HW_ERR_RETURN(hw_l3_ptk_trap_to_cpu_global(unit, RUNOS_L3_TRAP_TYPE_ARP, RUNOS_TRAP_ACTION_COPY_TO_CPU));
		HW_ERR_RETURN(hw_l3_ptk_trap_to_cpu_global(unit, RUNOS_L3_TRAP_TYPE_LDP, RUNOS_TRAP_ACTION_COPY_TO_CPU));
    }

    return RUNOS_OK;
}


int hw_l3_exit(void)
{	
    return RUNOS_OK;
}

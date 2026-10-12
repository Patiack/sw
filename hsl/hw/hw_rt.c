#include "hw.h"
#include "hw_chip.h"
#include "hsl_prefix.h"
#include "hsl_if.h"
#include "hsl_table.h"
#include "hw_rt.h"
#include "hsl_nh.h"
#include "hsl_rt.h"
#include "hsl_lib.h"
#include "sal.h"
#include "ctc_l3if.h"
#include "ctc_api.h"
#include "ctcs_api.h"
#include "ctc_error.h"
#include "ctc_debug.h"

int hw_rt_add_l3host(hsl_prefix_t *prefix, hsl_if_t *ifp)
{
#if 0
	rtk_l3_host_t host;
	int unit;
	hsl_nh_entry_t *nh;

	rtk_l3_host_t_init(&host);

	host.vrf_id = 0;

	if(prefix->family == AF_INET)
	{
		host.ip_addr.ipv4 = ntohl(prefix->u.prefix4);
	}
#if defined(HAVE_IPV6)
	else if(prefix->family == AF_INET6)
	{
		memcpy(host.ip_addr.ipv6.octet, prefix->u.prefix6.word, IPV6_ADDR_LEN);
		host.flags |= RTK_L3_FLAG_IPV6;		
	}
#endif

	host.fwd_act = RTK_L3_HOST_ACT_FORWARD;
	host.flags |= RTK_L3_FLAG_WITH_NEXTHOP;

	nh = hsl_rt_lookup_nh(ifp->ifindex,prefix);

	if(!nh)
	{
		host.nexthop.intf_id = ifp->l3_intf.l3if;	
		memcpy(&host.nexthop.mac_addr, nh->mac, sizeof(rtk_mac_t));
		
		FOREACH_UNIT(unit)
		{
			HW_ERR_RETURN(rtk_l3_host_add(unit, &host));
		}
		return RUNOS_OK;
	}
#endif
	return RUNOS_ERR;
}

int hw_rt_del_l3host(hsl_prefix_t *prefix, hsl_if_t *ifp)
{
#if 0
	rtk_l3_host_t host;
	int unit;

	rtk_l3_host_t_init(&host);

	host.vrf_id = 0;

	if(prefix->family == AF_INET)
	{
		host.ip_addr.ipv4 = ntohl(prefix->u.prefix4);
	}
#if defined(HAVE_IPV6)
	else if(prefix->family == AF_INET6)
	{
		memcpy(host.ip_addr.ipv6.octet, prefix->u.prefix6.word, IPV6_ADDR_LEN);
		host.flags |= RTK_L3_FLAG_IPV6;		
	}
#endif
	
	FOREACH_UNIT(unit)
	{
		HW_ERR_RETURN(rtk_l3_host_del(unit, &host));
	}
#endif
	return RUNOS_OK;
}


int hw_add_connected_rt(hsl_prefix_t *prefix, struct hsl_if_s *ifp)
{
	ctc_ipuc_param_t host = {0};
    ctc_ipuc_param_t route = {0};
	int unit;

	memset(&host,0,sizeof(ctc_ipuc_param_t));
	host.nh_id = CTC_NH_RESERVED_NHID_FOR_TOCPU;
	host.route_flag |= CTC_IPUC_FLAG_SELF_ADDRESS;
    host.vrf_id = 0;
	
	/*create lpm route*/
	memset(&route, 0, sizeof(ctc_ipuc_param_t));
	route.nh_id = CTC_NH_RESERVED_NHID_FOR_TOCPU;
	route.route_flag = CTC_IPUC_FLAG_CONNECT;
    route.vrf_id = 0;

	if(prefix->family == AF_INET)
	{
		host.ip.ipv4 = ntohl(prefix->u.prefix4);		
		host.masklen = 32;
		host.ip_ver = CTC_IP_VER_4;

		if(prefix->prefixlen != IPV4_MAX_PREFIXLEN)
		{		
			route.masklen = prefix->prefixlen;		
			route.ip.ipv4 = ntohl(prefix->u.prefix4);
			route.ip_ver = CTC_IP_VER_4;
		}	
	}
#if defined(HAVE_IPV6)
	else if(prefix->family == AF_INET6)
	{
		host.ip.ipv6[0] = ntohl(prefix->u.prefix6.word[0]);
		host.ip.ipv6[1] = ntohl(prefix->u.prefix6.word[1]);
		host.ip.ipv6[2] = ntohl(prefix->u.prefix6.word[2]);	
		host.ip.ipv6[3] = ntohl(prefix->u.prefix6.word[3]);
		
		host.masklen = 128;
		host.ip_ver = CTC_IP_VER_6;

		if(prefix->prefixlen != IPV6_MAX_PREFIXLEN)
		{		
			route.masklen = prefix->prefixlen;		
			route.ip_ver = CTC_IP_VER_6;
			route.ip.ipv6[0] = ntohl(prefix->u.prefix6.word[0]);
			route.ip.ipv6[1] = ntohl(prefix->u.prefix6.word[1]);
			route.ip.ipv6[2] = ntohl(prefix->u.prefix6.word[2]);	
			route.ip.ipv6[3] = ntohl(prefix->u.prefix6.word[3]);			
		}
	}
#endif

	FOREACH_UNIT(unit)
	{
		//防止ctc中存在动态ipuc表项，导致此下发不成功
		//if(ctcs_ipuc_get(unit, &host) == CTC_E_NOT_EXIST)
			HW_ERR_RETURN(ctcs_ipuc_add(unit, &host));
		
		if(ctcs_ipuc_get(unit, &route) == CTC_E_NOT_EXIST)
			HW_ERR_RETURN(ctcs_ipuc_add(unit, &route));
		
	}

    return RUNOS_OK;
}

/* lo 接口（内核 loopback）直连路由：HSL 接口表中无 lo，无法走
 * hw_add_connected_rt 的接口地址路径，这里按本机地址语义直接下发。
 *
 * 与 vlanif 接口地址保持一致的表项模式：/32（或 /128）下发
 * SELF_ADDRESS host 表项（本机地址，martian 检查识别后上送 CPU 并
 * 注入内核），非 /32 时额外下发 CONNECT 网段表项。不能只用 CONNECT
 * TOCPU——那是 MPLS ARP 占位的“流量中断”语义，报文上送后不会注入
 * 内核，以 lo 为 transport 的协议会话（如 LDP）将无法建立。 */
int hw_add_loopback_rt(hsl_prefix_t *prefix)
{
	ctc_ipuc_param_t host = {0};
	ctc_ipuc_param_t route = {0};
	int unit;

	host.nh_id = CTC_NH_RESERVED_NHID_FOR_TOCPU;
	host.route_flag |= CTC_IPUC_FLAG_SELF_ADDRESS;
	host.vrf_id = 0;
	host.masklen = (prefix->family == AF_INET) ? IPV4_MAX_PREFIXLEN
												: IPV6_MAX_PREFIXLEN;

	route.nh_id = CTC_NH_RESERVED_NHID_FOR_TOCPU;
	route.route_flag = CTC_IPUC_FLAG_CONNECT;
	route.vrf_id = 0;
	route.masklen = prefix->prefixlen;

	if (prefix->family == AF_INET) {
		host.ip.ipv4 = ntohl(prefix->u.prefix4);
		host.ip_ver = CTC_IP_VER_4;

		if (prefix->prefixlen != IPV4_MAX_PREFIXLEN) {
			route.ip.ipv4 = ntohl(prefix->u.prefix4);
			route.ip_ver = CTC_IP_VER_4;
		}
	}
#if defined(HAVE_IPV6)
	else if (prefix->family == AF_INET6) {
		host.ip.ipv6[0] = ntohl(prefix->u.prefix6.word[0]);
		host.ip.ipv6[1] = ntohl(prefix->u.prefix6.word[1]);
		host.ip.ipv6[2] = ntohl(prefix->u.prefix6.word[2]);
		host.ip.ipv6[3] = ntohl(prefix->u.prefix6.word[3]);
		host.ip_ver = CTC_IP_VER_6;

		if (prefix->prefixlen != IPV6_MAX_PREFIXLEN) {
			route.ip.ipv6[0] = ntohl(prefix->u.prefix6.word[0]);
			route.ip.ipv6[1] = ntohl(prefix->u.prefix6.word[1]);
			route.ip.ipv6[2] = ntohl(prefix->u.prefix6.word[2]);
			route.ip.ipv6[3] = ntohl(prefix->u.prefix6.word[3]);
			route.ip_ver = CTC_IP_VER_6;
		}
	}
#endif

	FOREACH_UNIT(unit)
	{
		/* 已存在同 key 表项则不重复下发 */
		if (ctcs_ipuc_get(unit, &host) == CTC_E_NOT_EXIST)
			HW_ERR_RETURN(ctcs_ipuc_add(unit, &host));

		if (prefix->prefixlen != (prefix->family == AF_INET
									? IPV4_MAX_PREFIXLEN
									: IPV6_MAX_PREFIXLEN))
			if (ctcs_ipuc_get(unit, &route) == CTC_E_NOT_EXIST)
				HW_ERR_RETURN(ctcs_ipuc_add(unit, &route));
	}

	return RUNOS_OK;
}

int hw_del_loopback_rt(hsl_prefix_t *prefix)
{
	ctc_ipuc_param_t host = {0};
	ctc_ipuc_param_t route = {0};
	int unit;

	host.nh_id = CTC_NH_RESERVED_NHID_FOR_TOCPU;
	host.vrf_id = 0;
	host.masklen = (prefix->family == AF_INET) ? IPV4_MAX_PREFIXLEN
												: IPV6_MAX_PREFIXLEN;

	route.nh_id = CTC_NH_RESERVED_NHID_FOR_TOCPU;
	route.vrf_id = 0;
	route.masklen = prefix->prefixlen;

	if (prefix->family == AF_INET) {
		host.ip.ipv4 = ntohl(prefix->u.prefix4);
		host.ip_ver = CTC_IP_VER_4;

		if (prefix->prefixlen != IPV4_MAX_PREFIXLEN) {
			route.ip.ipv4 = ntohl(prefix->u.prefix4);
			route.ip_ver = CTC_IP_VER_4;
		}
	}
#if defined(HAVE_IPV6)
	else if (prefix->family == AF_INET6) {
		host.ip.ipv6[0] = ntohl(prefix->u.prefix6.word[0]);
		host.ip.ipv6[1] = ntohl(prefix->u.prefix6.word[1]);
		host.ip.ipv6[2] = ntohl(prefix->u.prefix6.word[2]);
		host.ip.ipv6[3] = ntohl(prefix->u.prefix6.word[3]);
		host.ip_ver = CTC_IP_VER_6;

		if (prefix->prefixlen != IPV6_MAX_PREFIXLEN) {
			route.ip.ipv6[0] = ntohl(prefix->u.prefix6.word[0]);
			route.ip.ipv6[1] = ntohl(prefix->u.prefix6.word[1]);
			route.ip.ipv6[2] = ntohl(prefix->u.prefix6.word[2]);
			route.ip.ipv6[3] = ntohl(prefix->u.prefix6.word[3]);
			route.ip_ver = CTC_IP_VER_6;
		}
	}
#endif

	FOREACH_UNIT(unit)
	{
		if (ctcs_ipuc_get(unit, &host) == CTC_E_NONE)
			HW_ERR_RETURN(ctcs_ipuc_remove(unit, &host));

		if (prefix->prefixlen != (prefix->family == AF_INET
									? IPV4_MAX_PREFIXLEN
									: IPV6_MAX_PREFIXLEN))
			if (ctcs_ipuc_get(unit, &route) == CTC_E_NONE)
				HW_ERR_RETURN(ctcs_ipuc_remove(unit, &route));
	}

	return RUNOS_OK;
}

int hw_del_connected_rt(hsl_prefix_t *prefix, struct hsl_if_s *ifp)
{
	ctc_ipuc_param_t host;
    ctc_ipuc_param_t route;
	int unit;

	memset(&host,0,sizeof(ctc_ipuc_param_t));
	host.nh_id = CTC_NH_RESERVED_NHID_FOR_TOCPU;
    host.vrf_id = 0;

	/*create lpm route*/
	memset(&route, 0, sizeof(ctc_ipuc_param_t));
	route.nh_id = CTC_NH_RESERVED_NHID_FOR_TOCPU;
    route.vrf_id = 0;

	if(prefix->family == AF_INET)
	{
		host.ip.ipv4 = ntohl(prefix->u.prefix4);		
		host.masklen = 32;
		host.ip_ver = CTC_IP_VER_4;

		if(prefix->prefixlen != IPV4_MAX_PREFIXLEN)
		{		
			route.masklen = prefix->prefixlen;		
			route.ip.ipv4 = ntohl(prefix->u.prefix4);
			route.ip_ver = CTC_IP_VER_4;
		}	
	}
#if defined(HAVE_IPV6)
	else if(prefix->family == AF_INET6)
	{		
		host.ip.ipv6[0] = ntohl(prefix->u.prefix6.word[0]);
		host.ip.ipv6[1] = ntohl(prefix->u.prefix6.word[1]);
		host.ip.ipv6[2] = ntohl(prefix->u.prefix6.word[2]);	
		host.ip.ipv6[3] = ntohl(prefix->u.prefix6.word[3]);
		host.masklen = 128;
		host.ip_ver = CTC_IP_VER_6;

		if(prefix->prefixlen != IPV6_MAX_PREFIXLEN)
		{		
			route.masklen = prefix->prefixlen;		
			route.ip_ver = CTC_IP_VER_6;		
			route.ip.ipv6[0] = ntohl(prefix->u.prefix6.word[0]);
			route.ip.ipv6[1] = ntohl(prefix->u.prefix6.word[1]);
			route.ip.ipv6[2] = ntohl(prefix->u.prefix6.word[2]);	
			route.ip.ipv6[3] = ntohl(prefix->u.prefix6.word[3]);
		}
	}
#endif

	FOREACH_UNIT(unit)
	{
		if(ctcs_ipuc_get(unit, &host) == CTC_E_NONE)
			HW_ERR_RETURN(ctcs_ipuc_remove(unit, &host));
		
		if(ctcs_ipuc_get(unit, &route) == CTC_E_NONE)
			HW_ERR_RETURN(ctcs_ipuc_remove(unit, &route));
		
	}
	
    return RUNOS_OK;
}

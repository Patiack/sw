/**
 @file sys_greatbelt_ftm_db.h

 @date 2011-11-16

 @version v2.0

 alloc memory and offset
*/

#ifndef _SYS_GREAT_BELT_FTM_DB_H
#define _SYS_GREAT_BELT_FTM_DB_H
#ifdef __cplusplus
extern "C" {
#endif

/*----- UserId Key table ID -----*/
#define USERID_TABLEID               0
/* UserId sub table ID */
#define USERID_VLAN_SUB_TABLEID      0
#define USERID_MAC_SUB_TABLEID       1
#define USERID_IPV4_SUB_TABLEID      2
#define USERID_IPV6_SUB_TABLEID      3

/*----- Tunnel Key table ID -----*/
#define TUNNELID_TABLEID             1
/* TunnelId sub table ID */
#define TUNNELID_IPV4_SUB_TABLEID    0
#define TUNNELID_IPV6_SUB_TABLEID    1
#define TUNNELID_PBB_SUB_TABLEID     2
#define TUNNELID_WLAN_SUB_TABLEID  3
#define TUNNELID_TRILL_SUB_TABLEID   4

/*----- TRILL/FCoE Key table ID -----*/
#define TRILL_FCOE_TABLEID           2
/* TRILL/FCoE sub table ID */
#define FCOE_DA_SUB_TABLEID          0
#define FCOE_SA_SUB_TABLEID          1
#define TRILL_DA_UC_SUB_TABLEID      2
#define TRILL_DA_MC_SUB_TABLEID      3

/*----- IPv4DA/MAC Key table ID -----*/
#define IPV4DA_MAC_TABLEID           3
/* IPv4DA/MAC sub table ID */
#define MAC_DA_SUB_TABLEID           0
#define MAC_SA_SUB_TABLEID           0
#define IPV4DA_UCAST_SUB_TABLEID     1
#define IPV4SA_RPF_SUB_TABLEID       1
#define IPV4DA_MCAST_SUB_TABLEID     2
#define MAC_IPV4_SUB_TABLEID         3

/*----- IPv4SA Key table ID -----*/
#define IPV4SA_TABLEID               4
/* IPv4SA sub table ID */
#define IPV4SA_NAT_SUB_TABLEID       0
#define IPV4SA_PBR_SUB_TABLEID       1

/*----- IPv6DA Key table ID -----*/
#define IPV6DA_TABLEID           5
/* IPv6DA sub table ID */
#define IPV6DA_UCAST_SUB_TABLEID     0
#define IPV6SA_RPF_SUB_TABLEID       0
#define IPV6DA_MCAST_SUB_TABLEID     1
#define MAC_IPV6_SUB_TABLEID         2

/*----- IPv6SA Key table ID -----*/
#define IPV6SA_TABLEID               6
/* IPv6SA sub table ID */
#define IPV6SA_NAT_SUB_TABLEID       0
#define IPV6SA_PBR_SUB_TABLEID       1

/*----- OAM Key table ID -----*/
#define OAM_TABLEID                  7
/* OAM sub table ID */
#define OAM_ETHER_SUB_TABLEID        0
#define OAM_MPLS_SUB_TABLEID         1
#define OAM_BFD_SUB_TABLEID          2
#define OAM_PBT_SUB_TABLEID          3
#define OAM_ETHERRMEP_SUB_TABLEID    4

/*----- ACL Key table ID -----*/
/* use field isAcl */
/* ACL sub table ID */
#define ACL_MAC_SUB_TABLEID          0
/* ACL IPv4 key table ID */
#define ACL_IPV4_SUB_TABLEID         1
/* ACL MPLS key table ID */
#define ACL_MPLS_SUB_TABLEID         2
/* ACL IPv6 key table ID */
#define ACL_IPV6_SUB_TABLEID         3

extern int32
_sys_greatebelt_ftm_init_lkp_register(uint8 lchip);

#ifdef __cplusplus
}
#endif

#endif


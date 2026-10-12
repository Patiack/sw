/****************************************************************************
 * drv_hash_db.c : Store all GreatBelt hash key's property information.
 *
 * Copyright: (c)2011 Centec Networks Inc.  All rights reserved.
 *
 * Modify History:
 * Revision:     V2.0.
 * Author:       ZhouW
 * Date:         2011-05-12.
 * Reason:       Create for GreatBelt v2.0.
 *
 * Revision:     V4.2.1
 * Revisor:      XuZx
 * Date:         2011-07-07.
 * Reason:       Revise for GreatBelt v4.2.1
 *
 * Revision:     V4.5.1
 * Revisor:      XuZx
 * Date:         2011-07-20.
 * Reason:       Revise for GreatBelt v4.5.1
 *
 * Revision:     V4.29.3
 * Revisor:      XuZx
 * Date:         2011-10-11.
 * Reason:       Revise for GreatBelt v4.29.3
 *
 * Revision:     V5.6.0
 * Revisor:      XuZx
 * Date:         2012-01-07.
 * Reason:       Revise for GreatBelt v5.6.0
 ****************************************************************************/
#include "greatbelt/include/drv_lib.h"

hash_key_property_t lpm_hash_key_property_table[] =
{
    /* IPv4 16, DsLpmIpv4Hash16Key */
    {   /* Table ID*/
        DsLpmIpv4Hash16Key_t,
        /* Compared field ID */
        {
            DsLpmIpv4Hash16Key_HashType_f,
            DsLpmIpv4Hash16Key_Valid_f,
            DsLpmIpv4Hash16Key_VrfId_f,
            DsLpmIpv4Hash16Key_Ip_f,
            MaxField_f,
        },
        /* Entry valid field ID */
        {
            DsLpmIpv4Hash16Key_Valid_f,
            MaxField_f
        },
        /* AD field ID */
        {
            DsLpmIpv4Hash16Key_Pointer17_f,
            DsLpmIpv4Hash16Key_Pointer16_f,
            DsLpmIpv4Hash16Key_Nexthop_f,
            MaxField_f
        },
        /* compare key mask */
        {
            {0x7C, 0x0, 0x3FFFFFFF}
        },
        /* Index Mask field ID */
        {
            DsLpmIpv4Hash16Key_IndexMask_f,
            MaxField_f
        },
        /* Node Type field ID */
        {
            DsLpmIpv4Hash16Key_Type_f,
            MaxField_f
        },
    },

    /* IPv4 (S, G), DsLpmIpv4McastHash64Key */
    {   /* Table ID*/
        DsLpmIpv4McastHash64Key_t,
        /* Compared field ID */
        {
            DsLpmIpv4McastHash64Key_HashType0_f,
            DsLpmIpv4McastHash64Key_Valid0_f,
            DsLpmIpv4McastHash64Key_VrfId_f,
            DsLpmIpv4McastHash64Key_IpSa_f,
            DsLpmIpv4McastHash64Key_HashType1_f,
            DsLpmIpv4McastHash64Key_Valid1_f,
            DsLpmIpv4McastHash64Key_IpDa_f,
            DsLpmIpv4McastHash64Key_IsMac_f,
            MaxField_f,
        },
        /* Entry valid field ID */
        {
            DsLpmIpv4McastHash64Key_Valid0_f,
            DsLpmIpv4McastHash64Key_Valid1_f,
            MaxField_f
        },
        /* AD field ID */
        {
            DsLpmIpv4McastHash64Key_Pointer17_f,
            DsLpmIpv4McastHash64Key_Pointer16_f,
            DsLpmIpv4McastHash64Key_Nexthop_f,
            MaxField_f
        },
        /* compare key mask */
        {
            {0x7C, 0x3FFF, 0xFFFFFFFF},
            {0x7E, 0x0, 0xFFFFFFFF}
        },
        /* Index Mask field ID */
        {
            MaxField_f
        },
        /* Node Type field ID */
        {
            MaxField_f
        },
    },

    /* IPv6 Low32 DsLpmIpv6Hash32LowKey */
    {   /* Table ID*/
        DsLpmIpv6Hash32LowKey_t,
        /* Compared field ID */
        {
            DsLpmIpv6Hash32LowKey_HashType0_f,
            DsLpmIpv6Hash32LowKey_IpDa31_0_f,
            DsLpmIpv6Hash32LowKey_Mid_f,
            DsLpmIpv6Hash32LowKey_Pointer2_0_f,
            DsLpmIpv6Hash32LowKey_Pointer4_3_f,
            DsLpmIpv6Hash32LowKey_Pointer12_5_f,
            DsLpmIpv6Hash32LowKey_Valid0_f,
            MaxField_f,
        },
        /* Entry valid field ID */
        {
            DsLpmIpv6Hash32LowKey_Valid0_f,
            MaxField_f
        },
        /* AD field ID */
        {
            DsLpmIpv6Hash32LowKey_Nexthop_f,
            MaxField_f
        },
        /* compare key mask */
        {
            {0x7FFF, 0xFFFF, 0xFFFFFFFF},
        },
        /* Index Mask field ID */
        {MaxField_f},
        /* Node Type field ID */
        {
            MaxField_f
        },
    },

    /* IPv6 Mid32 DsLpmIpv6Hash32MidKey */
    {   /* Table ID*/
        DsLpmIpv6Hash32MidKey_t,
        /* Compared field ID */
        {
            DsLpmIpv6Hash32MidKey_HashType_f,
            DsLpmIpv6Hash32MidKey_IpDa_f,
            DsLpmIpv6Hash32MidKey_Valid_f,
            MaxField_f
        },
        /* Entry valid field ID */
        {
            DsLpmIpv6Hash32MidKey_Valid_f,
            MaxField_f
        },
        /* AD field ID */
        {
            DsLpmIpv6Hash32MidKey_Pointer17_17_f,
            DsLpmIpv6Hash32MidKey_Pointer16_f,
            DsLpmIpv6Hash32MidKey_Nexthop_f,
            MaxField_f,
        },
        /* compare key mask */
        {
            {0x7C, 0x0, 0xFFFFFFFF},
        },
        /* Index Mask field ID */
        {MaxField_f},
        /* Node Type field ID */
        {MaxField_f}
    },

    /* Ipv4 8, DsLpmIpv4Hash8Key */
    {   /* Table ID*/
        DsLpmIpv4Hash8Key_t,
        /* Compared field ID */
        {
            DsLpmIpv4Hash8Key_HashType_f,
            DsLpmIpv4Hash8Key_Valid_f,
            DsLpmIpv4Hash8Key_VrfId_f,
            DsLpmIpv4Hash8Key_Ip_f,
            MaxField_f,
        },
        /* Entry valid field ID */
        {
            DsLpmIpv4Hash8Key_Valid_f,
            MaxField_f
        },
        /* AD field ID */
        {
            DsLpmIpv4Hash8Key_Pointer17_f,
            DsLpmIpv4Hash8Key_Pointer16_f,
            DsLpmIpv4Hash8Key_Nexthop_f,
            MaxField_f
        },
        /* compare key mask */
        {
            {0x7C, 0x0, 0x3FFF00FF}
        },
        /* Index Mask field ID */
        {
            DsLpmIpv4Hash8Key_IndexMask_f,
            MaxField_f
        },
        /* Node Type field ID */
        {
            DsLpmIpv4Hash8Key_Type_f,
            MaxField_f
        },
    },

    /* IPV4 (*,G) DsLpmIpv4McastHash32Key */
    {   /* Table ID*/
        DsLpmIpv4McastHash32Key_t,
        /* Compared field ID */
        {
            DsLpmIpv4McastHash32Key_HashType_f,
            DsLpmIpv4McastHash32Key_Valid_f,
            DsLpmIpv4McastHash32Key_VrfId_f,
            DsLpmIpv4McastHash32Key_IpDa_f,
            DsLpmIpv4McastHash32Key_IsMac_f,
            MaxField_f
        },
        /* Entry valid field ID */
        {
            DsLpmIpv4McastHash32Key_Valid_f,
            MaxField_f
        },
        /* AD field ID */
        {
            DsLpmIpv4McastHash32Key_Pointer17_f,
            DsLpmIpv4McastHash32Key_Pointer16_f,
            DsLpmIpv4McastHash32Key_Nexthop_f,
            MaxField_f
        },
        /* compare key mask */
        {
            {0x7E, 0x3FFF, 0xFFFFFFFF}
        },
        /* Index Mask field ID */
        {MaxField_f},
        /* Node Type field ID */
        {
            MaxField_f
        },
    },

    /* IPv6 HIGH32, DsLpmIpv6Hash32HighKey */
    {   /* Table ID*/
        DsLpmIpv6Hash32HighKey_t,
        /* Compared field ID */
        {
            DsLpmIpv6Hash32HighKey_HashType_f,
            DsLpmIpv6Hash32HighKey_Ip_f,
            DsLpmIpv6Hash32HighKey_Valid_f,
            DsLpmIpv6Hash32HighKey_VrfId0_f,
            DsLpmIpv6Hash32HighKey_VrfId13_8_f,
            MaxField_f,
        },
        /* Entry valid field ID */
        {
            DsLpmIpv6Hash32HighKey_Valid_f,
            MaxField_f
        },
        /* AD field ID */
        {
            DsLpmIpv6Hash32HighKey_Pointer17_17_f,
            DsLpmIpv6Hash32HighKey_Pointer16_f,
            DsLpmIpv6Hash32HighKey_Nexthop_f,
            MaxField_f
        },
        /* compare key mask */
        {
            {0x1FFC, 0xFF00, 0xFFFFFFFF}
        },
        /* Index Mask field ID */
        {
            DsLpmIpv6Hash32HighKey_IndexMask_f,
            MaxField_f
        },
        /* Node Type field ID */
        {
            DsLpmIpv6Hash32HighKey_Type_f,
            MaxField_f
        },
    },
    /* IPv6 (S, G)1, DsLpmIpv6McastHash1Key */
    {   /* Table ID*/
        DsLpmIpv6McastHash1Key_t,
        /* Compared field ID */
        {
            DsLpmIpv6McastHash1Key_Hash0Result1_0_f,
            DsLpmIpv6McastHash1Key_Hash0Result2_2_f,
            DsLpmIpv6McastHash1Key_Hash0Result10_3_f,
            DsLpmIpv6McastHash1Key_HashType0_f,
            DsLpmIpv6McastHash1Key_HashType1_f,
            DsLpmIpv6McastHash1Key_IpSa31_0_f,
            DsLpmIpv6McastHash1Key_IpSa47_32_f,
            DsLpmIpv6McastHash1Key_IpSa55_48_f,
            DsLpmIpv6McastHash1Key_IpSa87_56_f,
            DsLpmIpv6McastHash1Key_IpSa117_88_f,
            DsLpmIpv6McastHash1Key_IpSaPrefixIndex1_0_f,
            DsLpmIpv6McastHash1Key_IpSaPrefixIndex2_2_f,
            DsLpmIpv6McastHash1Key_Valid0_f,
            DsLpmIpv6McastHash1Key_Valid1_f,
            MaxField_f
        },
        /* Entry valid field ID */
        {
            DsLpmIpv6McastHash1Key_Valid0_f,
            DsLpmIpv6McastHash1Key_Valid1_f,
            MaxField_f
        },
        /* AD field ID */
        {
            DsLpmIpv6McastHash1Key_Nexthop_f,
            MaxField_f,
        },
        /* compare key mask */
        {
            {0x7FFF, 0xFFFFFFFF, 0xFFFFFFFF},
            {0x7FFF, 0xFFFF, 0xFFFFFFFF}
        },
        /* Index Mask field ID */
        {
            MaxField_f
        },
        /* Node Type field ID */
        {
            MaxField_f
        }
    },

    /* IPv6 (S, G)0, DsLpmIpv6McastHash0Key */
    {   /* Table ID*/
        DsLpmIpv6McastHash0Key_t,
        /* Compared field ID */
        {
            DsLpmIpv6McastHash0Key_HashType0_f,
            DsLpmIpv6McastHash0Key_HashType1_f,
            DsLpmIpv6McastHash0Key_IpDa31_0_f,
            DsLpmIpv6McastHash0Key_IpDa47_32_f,
            DsLpmIpv6McastHash0Key_IpDa55_48_f,
            DsLpmIpv6McastHash0Key_IpDa109_88_f,
            DsLpmIpv6McastHash0Key_IpDa111_110_f,
            DsLpmIpv6McastHash0Key_IpDa119_112_f,
            DsLpmIpv6McastHash0Key_IpDa87_56_f,
            DsLpmIpv6McastHash0Key_Valid0_f,
            DsLpmIpv6McastHash0Key_Valid1_f,
            DsLpmIpv6McastHash0Key_VrfId0_0_f,
            DsLpmIpv6McastHash0Key_VrfId10_1_f,
            DsLpmIpv6McastHash0Key_IsMac_f,
            MaxField_f
        },
        /* Entry valid field ID */
        {
            DsLpmIpv6McastHash0Key_Valid0_f,
            DsLpmIpv6McastHash0Key_Valid1_f,
            MaxField_f
        },
        /* AD field ID */
        {
            DsLpmIpv6McastHash0Key_Nexthop_f,
            MaxField_f
        },
        /* compare key mask */
        {
            {0x7FFF, 0xFFFFFFFF, 0xFFFFFFFF},
            {0x7FFF, 0xFFFF, 0xFFFFFFFF}
        },
        /* Index Mask field ID */
        {
            MaxField_f
        },
        /* Node Type field ID */
        {
            MaxField_f
        }
    },

    /* IPv6 (X, G)0, DsLpmIpv6McastHash0Key */
    {   /* Table ID*/
        DsLpmIpv6McastHash0Key_t,
        /* Compared field ID */
        {
            DsLpmIpv6McastHash0Key_HashType0_f,
            DsLpmIpv6McastHash0Key_HashType1_f,
            DsLpmIpv6McastHash0Key_IpDa31_0_f,
            DsLpmIpv6McastHash0Key_IpDa47_32_f,
            DsLpmIpv6McastHash0Key_IpDa55_48_f,
            DsLpmIpv6McastHash0Key_IpDa109_88_f,
            DsLpmIpv6McastHash0Key_IpDa111_110_f,
            DsLpmIpv6McastHash0Key_IpDa119_112_f,
            DsLpmIpv6McastHash0Key_IpDa87_56_f,
            DsLpmIpv6McastHash0Key_IsMac_f,
            DsLpmIpv6McastHash0Key_Valid0_f,
            DsLpmIpv6McastHash0Key_Valid1_f,
            DsLpmIpv6McastHash0Key_VrfId0_0_f,
            DsLpmIpv6McastHash0Key_VrfId10_1_f,
            MaxField_f
        },
        /* Entry valid field ID */
        {
            DsLpmIpv6McastHash0Key_Valid0_f,
            DsLpmIpv6McastHash0Key_Valid1_f,
            MaxField_f
        },
        /* AD field ID */
        {
            DsLpmIpv6McastHash0Key_Nexthop_f,
            MaxField_f
        },
        /* compare key mask */
        {
            {0x7FFF, 0xFFFFFFFF, 0xFFFFFFFF},
            {0x7FFF, 0xFFFF, 0xFFFFFFFF}
        },
        /* Index Mask field ID */
        {
            MaxField_f
        },
        /* Node Type field ID */
        {
            MaxField_f
        }
    },

    /* IPv4 NAT SA+PORT, DsLpmIpv4NatSaPortHashKey */
    {   /* Table ID*/
        DsLpmIpv4NatSaPortHashKey_t,
        /* Compared field ID */
        {
            DsLpmIpv4NatSaPortHashKey_L4SourcePort15_8_f,
            DsLpmIpv4NatSaPortHashKey_HashType_f,
            DsLpmIpv4NatSaPortHashKey_Valid_f,
            DsLpmIpv4NatSaPortHashKey_VrfId_f,
            DsLpmIpv4NatSaPortHashKey_L4SourcePort7_0_f,
            DsLpmIpv4NatSaPortHashKey_IpSa_f,
            DsLpmIpv4NatSaPortHashKey_L4PortType_f,
            MaxField_f,
        },
        /* Entry valid field ID */
        {
            DsLpmIpv4NatSaPortHashKey_Valid_f,
            MaxField_f
        },
        /* AD field ID */
        {
            DsLpmIpv4NatSaPortHashKey_Nexthop_f,
            MaxField_f
        },
        /* compare key mask */
        {
            {0x7FFF, 0xFFFF, 0xFFFFFFFF}
        },
        /* Index Mask field ID */
        {MaxField_f},
        /* Node Type field ID */
        {
            MaxField_f
        },
    },

    /* IPv4 NAT SA, DsLpmIpv4NatSaHashKey */
    {   /* Table ID*/
        DsLpmIpv4NatSaHashKey_t,
        /* Compared field ID */
        {
            DsLpmIpv4NatSaHashKey_VrfId1_f,
            DsLpmIpv4NatSaHashKey_HashType_f,
            DsLpmIpv4NatSaHashKey_Valid_f,
            DsLpmIpv4NatSaHashKey_VrfId0_f,
            DsLpmIpv4NatSaHashKey_IpSa_f,
            MaxField_f,
        },
        /* Entry valid field ID */
        {
            DsLpmIpv4NatSaHashKey_Valid_f,
            MaxField_f
        },
        /* AD field ID */
        {
            DsLpmIpv4NatSaHashKey_Nexthop_f,
            MaxField_f
        },
        /* compare key mask */
        {
            {0x1FFC, 0xFF00, 0xFFFFFFFF}
        },
        /* Index Mask field ID */
        {MaxField_f},
        /* Node Type field ID */
        {
            DsLpmIpv4NatSaHashKey_Type_f,
            MaxField_f
        },
    },

    /* IPv6 NAT SA+PORT DsLpmIpv6NatSaPortHashKey_t */
    {   /* Table ID*/
        DsLpmIpv6NatSaPortHashKey_t,
        /* Compared field ID */
        {
            DsLpmIpv6NatSaPortHashKey_L4SourcePort15_8_f,
            DsLpmIpv6NatSaPortHashKey_HashType0_f,
            DsLpmIpv6NatSaPortHashKey_Valid0_f,
            DsLpmIpv6NatSaPortHashKey_IpSaPrefixIndex_f,
            DsLpmIpv6NatSaPortHashKey_IpSa111_80_f,
            DsLpmIpv6NatSaPortHashKey_IpSa79_48_f,
            DsLpmIpv6NatSaPortHashKey_L4SourcePort7_0_f,
            DsLpmIpv6NatSaPortHashKey_HashType1_f,
            DsLpmIpv6NatSaPortHashKey_Valid1_f,
            DsLpmIpv6NatSaPortHashKey_IpSa47_32_f,
            DsLpmIpv6NatSaPortHashKey_IpSa31_0_f,
            DsLpmIpv6NatSaPortHashKey_L4PortType_f,
            MaxField_f
        },
        /* Entry valid field ID */
        {
            DsLpmIpv6NatSaPortHashKey_Valid0_f,
            DsLpmIpv6NatSaPortHashKey_Valid1_f,
            MaxField_f
        },
        /* AD field ID */
        {
            DsLpmIpv6NatSaPortHashKey_Nexthop_f,
            MaxField_f
        },
        /* compare key mask */
        {
            {0x7FFF, 0xFFFFFFFF, 0xFFFFFFFF},
            {0x7FFF, 0xFFFF, 0xFFFFFFFF}
        },
        /* Index Mask field ID */
        {
            MaxField_f
        },
        /* Node Type field ID */
        {
            MaxField_f
        },
    },

    /* IPv6 NAT SA, DsLpmIpv6NatSaHashKey */
    {   /* Table ID*/
        DsLpmIpv6NatSaHashKey_t,
        /* Compared field ID */
        {
            DsLpmIpv6NatSaHashKey_IpSa127_120_f,
            DsLpmIpv6NatSaHashKey_HashType0_f,
            DsLpmIpv6NatSaHashKey_Valid0_f,
            DsLpmIpv6NatSaHashKey_IpSa111_80_f,
            DsLpmIpv6NatSaHashKey_IpSa79_48_f,
            DsLpmIpv6NatSaHashKey_IpSa119_112_f,
            DsLpmIpv6NatSaHashKey_HashType1_f,
            DsLpmIpv6NatSaHashKey_Valid1_f,
            DsLpmIpv6NatSaHashKey_IpSa47_32_f,
            DsLpmIpv6NatSaHashKey_IpSa31_0_f,
            MaxField_f
        },
        /* Entry valid field ID */
        {
            DsLpmIpv6NatSaHashKey_Valid0_f,
            DsLpmIpv6NatSaHashKey_Valid1_f,
            MaxField_f
        },
        /* AD field ID */
        {
            DsLpmIpv6NatSaHashKey_Nexthop_f,
            MaxField_f
        },
        /* compare key mask */
        {
            {0x7FFC, 0xFFFFFFFF, 0xFFFFFFFF},
            {0x7FFC, 0xFFFF, 0xFFFFFFFF}
        },
        /* Index Mask field ID */
        {
            MaxField_f
        },
        /* Node Type field ID */
        {
            DsLpmIpv6NatSaHashKey_Type_f,
            MaxField_f
        },
    },

    /* IPv4 NAT DA+PORT DsLpmIpv4NatDaPortHashKey */
    {   /* Table ID*/
        DsLpmIpv4NatDaPortHashKey_t,
        /* Compared field ID */
        {
            DsLpmIpv4NatDaPortHashKey_L4DestPort15_8_f,
            DsLpmIpv4NatDaPortHashKey_HashType_f,
            DsLpmIpv4NatDaPortHashKey_Valid_f,
            DsLpmIpv4NatDaPortHashKey_VrfId7_0_f,
            DsLpmIpv4NatDaPortHashKey_L4DestPort7_0_f,
            DsLpmIpv4NatDaPortHashKey_IpDa_f,
            DsLpmIpv4NatDaPortHashKey_L4PortType_f,
            MaxField_f
        },
        /* Entry valid field ID */
        {
            DsLpmIpv4NatDaPortHashKey_Valid_f,
            MaxField_f
        },
        /* AD field ID */
        {
            DsLpmIpv4NatDaPortHashKey_Nexthop_f,
            MaxField_f
        },
        /* compare key mask */
        {
            {0x7FFF, 0xFFFF, 0xFFFFFFFF}
        },
        /* Index Mask field ID */
        {
            MaxField_f
        },
        /* Node Type field ID */
        {
            MaxField_f
        },
    },

    /* IPv6 NAT DA+PORT, DsLpmIpv6NatDaPortHashKey */
    {   /* Table ID*/
        DsLpmIpv6NatDaPortHashKey_t,
        /* Compared field ID */
        {
            DsLpmIpv6NatDaPortHashKey_L4DestPort15_8_f,
            DsLpmIpv6NatDaPortHashKey_HashType0_f,
            DsLpmIpv6NatDaPortHashKey_Valid0_f,
            DsLpmIpv6NatDaPortHashKey_IpDaPrefixIndex_f,
            DsLpmIpv6NatDaPortHashKey_IpDa111_80_f,
            DsLpmIpv6NatDaPortHashKey_IpDa79_48_f,
            DsLpmIpv6NatDaPortHashKey_L4DestPort7_0_f,
            DsLpmIpv6NatDaPortHashKey_HashType1_f,
            DsLpmIpv6NatDaPortHashKey_Valid1_f,
            DsLpmIpv6NatDaPortHashKey_IpDa47_32_f,
            DsLpmIpv6NatDaPortHashKey_IpDa31_0_f,
            DsLpmIpv6NatDaPortHashKey_L4PortType_f,
            MaxField_f
        },
        /* Entry valid field ID */
        {
            DsLpmIpv6NatDaPortHashKey_Valid0_f,
            DsLpmIpv6NatDaPortHashKey_Valid1_f,
            MaxField_f,
        },
        /* AD field ID */
        {
            DsLpmIpv6NatDaPortHashKey_Nexthop_f,
            MaxField_f,
        },
        /* compare key mask */
        {
            {0x7FFF, 0xFFFFFFFF, 0xFFFFFFFF},
            {0x7FFF, 0xFFFF, 0xFFFFFFFF}
        },
        /* Index Mask field ID */
        {
            MaxField_f
        },
        /* Node Type field ID */
        {
            MaxField_f
        },
    }
};

/* All UserID hash key property database (include userID, TunnelID, Oam hash key) */
hash_key_property_t userid_hash_key_property_table[] =
{
    /* USERID_HASH_TYPE_TWO_VLAN */
    /* TWOVLAN DsUserIdDoubleVlanHashKey */
    {
        /* table id*/
        DsUserIdDoubleVlanHashKey_t,
        /* compared field id */
        {
            DsUserIdDoubleVlanHashKey_Valid_f,
            DsUserIdDoubleVlanHashKey_GlobalSrcPort13_13_f,
            DsUserIdDoubleVlanHashKey_Direction_f,
            DsUserIdDoubleVlanHashKey_HashType_f,
            DsUserIdDoubleVlanHashKey_CvlanId0_0_f,
            DsUserIdDoubleVlanHashKey_CvlanId11_1_f,
            DsUserIdDoubleVlanHashKey_GlobalSrcPort12_0_f,
            DsUserIdDoubleVlanHashKey_IsLabel_f,
            DsUserIdDoubleVlanHashKey_SvlanId_f,
            MaxField_f,
        },
        /* entry valid field id */
        {
            DsUserIdDoubleVlanHashKey_Valid_f,
            MaxField_f
        },
        /* ad field id */
        {
            DsUserIdDoubleVlanHashKey_AdIndex_f,
            MaxField_f
        },
        /* compare key mask */
        {
            {0x7, 0xFFFF0000, 0xFFFFFFE0}
        },
        {MaxField_f},
        {MaxField_f}
    },

    /* USERID_HASH_TYPE_SVLAN */
    /* SVLAN DsUserIdSvlanHashKey */
    {
        /* table id*/
        DsUserIdSvlanHashKey_t,
        /* compared field id */
        {
            DsUserIdSvlanHashKey_Valid_f,
            DsUserIdSvlanHashKey_GlobalSrcPort13_f,
            DsUserIdSvlanHashKey_Direction_f,
            DsUserIdSvlanHashKey_HashType_f,
            DsUserIdSvlanHashKey_GlobalSrcPort_f,
            DsUserIdSvlanHashKey_IsLabel_f,
            DsUserIdSvlanHashKey_SvlanId_f,
            MaxField_f,
        },
        /* entry valid field id */
        {
            DsUserIdSvlanHashKey_Valid_f,
            MaxField_f
        },
        /* ad field id */
        {
            DsUserIdSvlanHashKey_AdIndex_f,
            MaxField_f
        },
        /* compare key mask */
        {
            {0x7, 0xF8000000, 0xFFFFFFC0}
        },
        {MaxField_f},
        {MaxField_f}
    },

    /* USERID_HASH_TYPE_CVLAN */
    /* CVLAN DsUserIdCvlanHashKey */
    {
        /* table id*/
        DsUserIdCvlanHashKey_t,
        /* compared field id */
        {
            DsUserIdCvlanHashKey_Valid_f,
            DsUserIdCvlanHashKey_GlobalSrcPort13_f,
            DsUserIdCvlanHashKey_Direction_f,
            DsUserIdCvlanHashKey_HashType_f,
            DsUserIdCvlanHashKey_CvlanId0_f,
            DsUserIdCvlanHashKey_CvlanId11_1_f,
            DsUserIdCvlanHashKey_GlobalSrcPort_f,
            DsUserIdCvlanHashKey_IsLabel_f,
            MaxField_f,
        },
        /* entry valid field id */
        {
            DsUserIdCvlanHashKey_Valid_f,
            MaxField_f
        },
        /* ad field id */
        {
            DsUserIdCvlanHashKey_AdIndex_f,
            MaxField_f
        },
        /* compare key mask */
        {
            {0x7, 0xFFFF0000, 0xFFFE0000}
        },
        {MaxField_f},
        {MaxField_f}
    },

    /* USERID_HASH_TYPE_SVLAN_COS */
    /* SVLANCOS DsUserIdSvlanCosHashKey */
    {
        /* table id*/
        DsUserIdSvlanCosHashKey_t,
        /* compared field id */
        {
            DsUserIdSvlanCosHashKey_Valid_f,
            DsUserIdSvlanCosHashKey_GlobalSrcPort13_f,
            DsUserIdSvlanCosHashKey_Direction_f,
            DsUserIdSvlanCosHashKey_HashType_f,
            DsUserIdSvlanCosHashKey_GlobalSrcPort_f,
            DsUserIdSvlanCosHashKey_IsLabel_f,
            DsUserIdSvlanCosHashKey_SvlanId_f,
            DsUserIdSvlanCosHashKey_StagCos_f,
            MaxField_f,
        },
        /* entry valid field id */
        {
            DsUserIdSvlanCosHashKey_Valid_f,
            MaxField_f
        },
        /* ad field id */
        {
            DsUserIdSvlanCosHashKey_AdIndex_f,
            MaxField_f
        },
        /* compare key mask */
        {
            {0x7, 0xF8000000, 0xFFFFFFF8}
        },
        {MaxField_f},
        {MaxField_f}
    },

    /* USERID_HASH_TYPE_CVLAN_COS */
    /* CVLANCOS DsUserIdCvlanCosHashKey */
    {
        /* table id*/
        DsUserIdCvlanCosHashKey_t,
        /* compared field id */
        {
            DsUserIdCvlanCosHashKey_Valid_f,
            DsUserIdCvlanCosHashKey_GlobalSrcPort13_f,
            DsUserIdCvlanCosHashKey_Direction_f,
            DsUserIdCvlanCosHashKey_HashType_f,
            DsUserIdCvlanCosHashKey_CvlanId0_f,
            DsUserIdCvlanCosHashKey_CvlanId11_1_f,
            DsUserIdCvlanCosHashKey_GlobalSrcPort_f,
            DsUserIdCvlanCosHashKey_IsLabel_f,
            DsUserIdCvlanCosHashKey_CtagCos_f,
            MaxField_f,
        },
        /* entry valid field id */
        {
            DsUserIdCvlanCosHashKey_Valid_f,
            MaxField_f
        },
        /* ad field id */
        {
            DsUserIdCvlanCosHashKey_AdIndex_f,
            MaxField_f
        },
        /* compare key mask */
        {
            {0x7, 0xFFFF0000, 0xFFFE0007}
        },
        {MaxField_f},
        {MaxField_f}
    },

    /* USERID_HASH_TYPE_MAC_SA */
    /* MACSA DsUserIdMacHashKey */
    {
        /* table id*/
        DsUserIdMacHashKey_t,
        /* compared field id */
        {
            DsUserIdMacHashKey_Valid_f,
            DsUserIdMacHashKey_IsMacDa_f,
            DsUserIdMacHashKey_HashType_f,
            DsUserIdMacHashKey_MacSa31_0_f,
            DsUserIdMacHashKey_MacSa42_32_f,
            DsUserIdMacHashKey_MacSa43_f,
            DsUserIdMacHashKey_MacSa47_44_f,
            MaxField_f,
        },
        /* entry valid field id */
        {
            DsUserIdMacHashKey_Valid_f,
            MaxField_f
        },
        /* ad field id */
        {
            DsUserIdMacHashKey_AdIndex_f,
            MaxField_f
        },
        /* compare key mask */
        {
            {0x7F, 0xFFFF0000, 0xFFFFFFFF}
        },
        {MaxField_f},
        {MaxField_f}
    },

    /* USERID_HASH_TYPE_PORT_MAC_SA */
    /* PORTMACSA DsUserIdMacPortHashKey */
    {
        /* table id*/
        DsUserIdMacPortHashKey_t,
        /* compared field id */
        {
            DsUserIdMacPortHashKey_GlobalSrcPort13_f,
            DsUserIdMacPortHashKey_GlobalSrcPort_f,
            DsUserIdMacPortHashKey_HashType1_f,
            DsUserIdMacPortHashKey_HashType_f,
            DsUserIdMacPortHashKey_IsLabel_f,
            DsUserIdMacPortHashKey_IsMacDa_f,
            DsUserIdMacPortHashKey_MacSaHigh_f,
            DsUserIdMacPortHashKey_MacSaLow_f,
            DsUserIdMacPortHashKey_Valid_f,
            DsUserIdMacPortHashKey_Valid1_f,
            MaxField_f
        },
        /* entry valid field id */
        {
            DsUserIdMacPortHashKey_Valid_f,
            DsUserIdMacPortHashKey_Valid1_f,
            MaxField_f
        },
        /* ad field id */
        {
            DsUserIdMacPortHashKey_AdIndex_f,
            MaxField_f
        },
        /* compare key mask  */
        {
            {0x6, 0xF800FFFF, 0x07FFE000},
            {0x5, 0xF8000000, 0xFFFFFFFF}
        },
        {MaxField_f},
        {MaxField_f}
    },

    /* USERID_HASH_TYPE_IPV4_SA */
    /* IPSA DsUserIdIpv4HashKey */
    {
        /* table id*/
        DsUserIdIpv4HashKey_t,
        /* compared field id */
        {
            DsUserIdIpv4HashKey_Valid_f,
            DsUserIdIpv4HashKey_Direction_f,
            DsUserIdIpv4HashKey_HashType_f,
            DsUserIdIpv4HashKey_IpSa_f,
            MaxField_f,
        },
        /* entry valid field id */
        {
            DsUserIdIpv4HashKey_Valid_f,
            MaxField_f
        },
        /* ad field id */
        {
            DsUserIdIpv4HashKey_AdIndex_f,
            MaxField_f
        },
        /* compare key mask */
        {
            {0x5, 0xF8000000, 0xFFFFFFFF}
        },
        {MaxField_f},
        {MaxField_f}
    },

    /* USERID_HASH_TYPE_PORT_VLAN_CROSS */
    /* PORTVLANCROSS DsUserIdPortVlanCrossHashKey */
    {
        /* table id */
        DsUserIdPortVlanCrossHashKey_t,
        /* compared field id */
        {
            DsUserIdPortVlanCrossHashKey_Direction_f,
            DsUserIdPortVlanCrossHashKey_GlobalDestPort_f,
            DsUserIdPortVlanCrossHashKey_GlobalSrcPort12_0_f,
            DsUserIdPortVlanCrossHashKey_GlobalSrcPort13_13_f,
            DsUserIdPortVlanCrossHashKey_HashType_f,
            DsUserIdPortVlanCrossHashKey_IsLabel_f,
            DsUserIdPortVlanCrossHashKey_Valid_f,
            DsUserIdPortVlanCrossHashKey_VlanId0_0_f,
            DsUserIdPortVlanCrossHashKey_VlanId11_1_f,
            MaxField_f
        },
        /* entry valid field id */
        {
            DsUserIdPortVlanCrossHashKey_Valid_f,
            MaxField_f
        },
        /* ad field id */
        {
            DsUserIdPortVlanCrossHashKey_AdIndex_f,
            MaxField_f
        },
        /* compare key mask */
        {
            {0x7, 0xFFFF0000, 0xFFFE3FFF}
        },
        {MaxField_f},
        {MaxField_f}
    },

    /* USERID_HASH_TYPE_PORT_IPV4_SA */
    /* PORTIPSA DsUserIdIpv4PortHashKey */
    {
        /* table id*/
        DsUserIdIpv4PortHashKey_t,
        /* compared field id */
        {
            DsUserIdIpv4PortHashKey_GlobalSrcPort1_f,
            DsUserIdIpv4PortHashKey_Valid_f,
            DsUserIdIpv4PortHashKey_GlobalSrcPort13_f,
            DsUserIdIpv4PortHashKey_HashType_f,
            DsUserIdIpv4PortHashKey_GlobalSrcPort0_f,
            DsUserIdIpv4PortHashKey_IsLabel_f,
            DsUserIdIpv4PortHashKey_IpSa_f,
            DsUserIdIpv4PortHashKey_Direction_f,
            MaxField_f,
        },
        /* entry valid field id */
        {
            DsUserIdIpv4PortHashKey_Valid_f,
            MaxField_f
        },
        /* ad field id */
        {
            DsUserIdIpv4PortHashKey_AdIndex_f,
            MaxField_f
        },
        /* compare key mask */
        {
            {0x7FFF, 0xFC010000, 0xFFFFFFFF}
        },
        {MaxField_f},
        {MaxField_f}
    },

    /* USERID_HASH_TYPE_PORT_CROSS */
    /* PORTCROSS DsUserIdPortCrossHashKey */
    {
        /* table id*/
        DsUserIdPortCrossHashKey_t,
        /* compared field id */
        {
            DsUserIdPortCrossHashKey_Direction_f,
            DsUserIdPortCrossHashKey_Valid_f,
            DsUserIdPortCrossHashKey_GlobalDestPort_f,
            DsUserIdPortCrossHashKey_GlobalSrcPort_f,
            DsUserIdPortCrossHashKey_GlobalSrcPort13_f,
            DsUserIdPortCrossHashKey_HashType_f,
            DsUserIdPortCrossHashKey_IsLabel_f,
            MaxField_f,
        },
        /* entry valid field id */
        {
            DsUserIdPortCrossHashKey_Valid_f,
            MaxField_f
        },
        /* ad field id */
        {
            DsUserIdPortCrossHashKey_AdIndex_f,
            MaxField_f
        },
        /* compare key mask */
        {
            {0x7, 0xF8000000, 0xFFFC3FFF}
        },
        {MaxField_f},
        {MaxField_f}
    },

    /* USERID_HASH_TYPE_PORT */
    /* PORTCROSS DsUserIdPortHashKey */
    {
        /* table id*/
        DsUserIdPortHashKey_t,
        /* compared field id */
        {
            DsUserIdPortHashKey_Direction_f,
            DsUserIdPortHashKey_GlobalSrcPort_f,
            DsUserIdPortHashKey_GlobalSrcPort13_f,
            DsUserIdPortHashKey_HashType_f,
            DsUserIdPortHashKey_IsLabel_f,
            DsUserIdPortHashKey_Valid_f,
            MaxField_f,
        },
        /* entry valid field id */
        {
            DsUserIdPortHashKey_Valid_f,
            MaxField_f
        },
        /* ad field id */
        {
            DsUserIdPortHashKey_AdIndex_f,
            MaxField_f
        },
        /* compare key mask */
        {
            {0x7, 0xF8000000, 0xFFFC0000}
        },
        {MaxField_f},
        {MaxField_f}
    },

    /* USERID_HASH_TYPE_L2 */
    /* L2 DsUserIdL2HashKey */
    {
        /* table id */
        DsUserIdL2HashKey_t,
        /* compared field id */
        {
            DsUserIdL2HashKey_Cos_f,
            DsUserIdL2HashKey_GlobalSrcPort10_0_f,
            DsUserIdL2HashKey_GlobalSrcPort13_11_f,
            DsUserIdL2HashKey_HashType0_f,
            DsUserIdL2HashKey_HashType1_f,
            DsUserIdL2HashKey_MacDa31_0_f,
            DsUserIdL2HashKey_MacDa47_32_f,
            DsUserIdL2HashKey_MacSa31_0_f,
            DsUserIdL2HashKey_MacSa40_32_f,
            DsUserIdL2HashKey_MacSa47_41_f,
            DsUserIdL2HashKey_VlanId_f,
            DsUserIdL2HashKey_Valid0_f,
            DsUserIdL2HashKey_Valid1_f,
            MaxField_f
        },
        /* entry valid field id */
        {
            DsUserIdL2HashKey_Valid1_f,
            DsUserIdL2HashKey_Valid0_f,
            MaxField_f
        },
        /* ad field id */
        {
            DsUserIdL2HashKey_AdIndex_f,
            MaxField_f
        },
        /* compare key mask  */
        {
            {0x7FFC, 0xFF7FFFFF, 0xFFFFFFFF},
            {0x7FFC, 0xFFFF0000, 0xFFFFFFFF}
        },
        {MaxField_f},
        {MaxField_f}
    },

    /* USERID_HASH_TYPE_IPV6_SA */
    /* IPv6SA 160bit DsUserIdIpv6HashKey */
    {
        /* table id */
        DsUserIdIpv6HashKey_t,
        /* compared field id */
        {
            DsUserIdIpv6HashKey_HashType0_f,
            DsUserIdIpv6HashKey_HashType1_f,
            DsUserIdIpv6HashKey_IpSa31_0_f,
            DsUserIdIpv6HashKey_IpSa32_f,
            DsUserIdIpv6HashKey_IpSa43_33_f,
            DsUserIdIpv6HashKey_IpSa45_44_f,
            DsUserIdIpv6HashKey_IpSa57_46_f,
            DsUserIdIpv6HashKey_IpSa89_58_f,
            DsUserIdIpv6HashKey_IpSa116_90_f,
            DsUserIdIpv6HashKey_IpSa127_117_f,
            DsUserIdIpv6HashKey_Valid0_f,
            DsUserIdIpv6HashKey_Valid1_f,
            MaxField_f
        },
        /* entry valid field id */
        {
            DsUserIdIpv6HashKey_Valid1_f,
            DsUserIdIpv6HashKey_Valid0_f,
            MaxField_f
        },
        /* ad field id */
        {
            DsUserIdIpv6HashKey_AdIndex_f,
            MaxField_f
        },
        /* compare key mask  */
        {
            {0x3FFD, 0xFFFFFFFF, 0xFFFFFFFF},
            {0x7FFF, 0xFFFF0000, 0xFFFFFFFF}
        },
        {MaxField_f},
        {MaxField_f}
    },

    /* USERID_HASH_TYPE_PBB */
    /* PBB DsTunnelIdPbbHashKey */
    {
        /* table id*/
        DsTunnelIdPbbHashKey_t,
        /* compared field id */
        {
            DsTunnelIdPbbHashKey_Valid_f,
            DsTunnelIdPbbHashKey_GlobalSrcPort13_f,
            DsTunnelIdPbbHashKey_Direction_f,
            DsTunnelIdPbbHashKey_HashType_f,
            DsTunnelIdPbbHashKey_IsLabel_f,
            DsTunnelIdPbbHashKey_IsidHigh_f,
            DsTunnelIdPbbHashKey_GlobalSrcPort_f,
            DsTunnelIdPbbHashKey_IsidLow_f,
            MaxField_f,
        },
        /* entry valid field id */
        {
            DsTunnelIdPbbHashKey_Valid_f,
            MaxField_f
        },
        /* ad field id */
        {
            DsTunnelIdPbbHashKey_AdIndex_f,
            MaxField_f
        },
        /* compare key mask */
        {
            {0x7, 0xFFE00000, 0xFFFFFFFF}
        },
        {MaxField_f},
        {MaxField_f}
    },

    /* USERID_HASH_TYPE_IPV4_TUNNEL */
    /* IPv4TUNNEL DsTunnelIdIpv4HashKey */
    {
        /* table id*/
        DsTunnelIdIpv4HashKey_t,
        /* compared field id */
        {
            DsTunnelIdIpv4HashKey_Valid0_f,
            DsTunnelIdIpv4HashKey_HashType0_f,
            DsTunnelIdIpv4HashKey_IpDa_f,
            DsTunnelIdIpv4HashKey_Valid1_f,
            DsTunnelIdIpv4HashKey_HashType1_f,
            DsTunnelIdIpv4HashKey_Layer4Type_f,
            DsTunnelIdIpv4HashKey_IpSa_f,
            MaxField_f,
        },
        /* entry valid field id */
        {
            DsTunnelIdIpv4HashKey_Valid0_f,
            DsTunnelIdIpv4HashKey_Valid1_f,
            MaxField_f
        },
        /* ad field id */
        {
            DsTunnelIdIpv4HashKey_AdIndex_f,
            MaxField_f
        },
        /* compare key mask */
        {
            {0x4, 0xF8000000, 0xFFFFFFFF},
            {0x4, 0xFF800000, 0xFFFFFFFF}
        },
        {MaxField_f},
        {MaxField_f}
    },

    /* USERID_HASH_TYPE_IPV4_GRE_KEY */
    /* IPv4GREKEY DsTunnelIdIpv4HashKey */
    {
        /* table id*/
        DsTunnelIdIpv4HashKey_t,
        /* compared field id */
        {
            DsTunnelIdIpv4HashKey_Valid0_f,
            DsTunnelIdIpv4HashKey_HashType0_f,
            DsTunnelIdIpv4HashKey_IpDa_f,
            DsTunnelIdIpv4HashKey_Valid1_f,
            DsTunnelIdIpv4HashKey_HashType1_f,
            DsTunnelIdIpv4HashKey_Layer4Type_f,
            DsTunnelIdIpv4HashKey_IpSa_f,
            DsTunnelIdIpv4HashKey_UdpDestPort_f,
            DsTunnelIdIpv4HashKey_UdpSrcPortH_f,
            DsTunnelIdIpv4HashKey_UdpSrcPortL_f,
            MaxField_f,
        },
        /* entry valid field id */
        {
            DsTunnelIdIpv4HashKey_Valid0_f,
            DsTunnelIdIpv4HashKey_Valid1_f,
            MaxField_f
        },
        /* ad field id */
        {
            DsTunnelIdIpv4HashKey_AdIndex_f,
            MaxField_f
        },
        /* compare key mask */
        {
            {0x4, 0xffffffff, 0xffffffff},
            {0x4, 0xfffc0000, 0xffffffff}
        },
        {MaxField_f},
        {MaxField_f}
    },

    /* USERID_HASH_TYPE_IPV4_UDP */
    /* IPv4UDP DsTunnelIdIpv4HashKey_t */
    {
        /* table id*/
        DsTunnelIdIpv4HashKey_t,
        /* compared field id */
        {
            DsTunnelIdIpv4HashKey_Valid0_f,
            DsTunnelIdIpv4HashKey_HashType0_f,
            DsTunnelIdIpv4HashKey_UdpSrcPortL_f,
            DsTunnelIdIpv4HashKey_UdpDestPort_f,
            DsTunnelIdIpv4HashKey_IpDa_f,
            DsTunnelIdIpv4HashKey_Valid1_f,
            DsTunnelIdIpv4HashKey_HashType1_f,
            DsTunnelIdIpv4HashKey_Layer4Type_f,
            DsTunnelIdIpv4HashKey_UdpSrcPortH_f,
            DsTunnelIdIpv4HashKey_IpSa_f,
            MaxField_f
        },
        /* entry valid field id */
        {
            DsTunnelIdIpv4HashKey_Valid0_f,
            DsTunnelIdIpv4HashKey_Valid1_f,
            MaxField_f
        },
        /* ad field id */
        {
            DsTunnelIdIpv4HashKey_AdIndex_f,
            MaxField_f
        },
        /* compare key mask */
        {
            {0x4, 0xffffffff, 0xffffffff},
            {0x4, 0xfffc0000, 0xffffffff}
        },
        {MaxField_f},
        {MaxField_f}
    },

    /* USERID_HASH_TYPE_CAPWAP */
    /* CAPWAP DsTunnelIdCapwapHashKey */
    {
        /* table id*/
        DsTunnelIdCapwapHashKey_t,
        /* compared field id */
        {
            DsTunnelIdCapwapHashKey_RadioMac31_0_f,
            DsTunnelIdCapwapHashKey_RadioMac37_32_f,
            DsTunnelIdCapwapHashKey_RadioMac38_f,
            DsTunnelIdCapwapHashKey_RadioMac47_39_f,
            DsTunnelIdCapwapHashKey_Valid_f,
            DsTunnelIdCapwapHashKey_HashType_f,
            DsTunnelIdCapwapHashKey_Rid_f,
            MaxField_f,
        },
        /* entry valid field id */
        {
            DsTunnelIdCapwapHashKey_Valid_f,
            MaxField_f
        },
        /* ad field id */
        {
            DsTunnelIdCapwapHashKey_AdIndex_f,
            MaxField_f
        },
        /* compare key mask */
        {
            {0xFFD, 0xFFFF0000, 0xFFFFFFFF}
        },
        {MaxField_f},
        {MaxField_f}
    },

    /* USERID_HASH_TYPE_TRILL_UC_RPF */
    /* TRILLUCRPF DsTunnelIdTrillUcRpfHashKey */
    {
        /* table id*/
        DsTunnelIdTrillUcRpfHashKey_t,
        /* compared field id */
        {
            DsTunnelIdTrillUcRpfHashKey_IngressNicknameHigh_f,
            DsTunnelIdTrillUcRpfHashKey_Valid_f,
            DsTunnelIdTrillUcRpfHashKey_HashType_f,
            DsTunnelIdTrillUcRpfHashKey_IngressNicknameLow_f,
            MaxField_f,
        },
        /* entry valid field id */
        {
            DsTunnelIdTrillUcRpfHashKey_Valid_f,
            MaxField_f
        },
        /* ad field id */
        {
            DsTunnelIdTrillUcRpfHashKey_AdIndex_f,
            MaxField_f
        },
        /* compare key mask */
        {
            {0xFC, 0xFFFF0000, 0x00000000}
        },
        {MaxField_f},
        {MaxField_f}
    },

    /* USERID_HASH_TYPE_TRILL_MC_RPF */
    /* TRILLMCRPF */
    {
        /* table id*/
        DsTunnelIdTrillMcRpfHashKey_t,
        /* compared field id */
        {
            DsTunnelIdTrillMcRpfHashKey_IngressNicknameHigh_f,
            DsTunnelIdTrillMcRpfHashKey_Valid_f,
            DsTunnelIdTrillMcRpfHashKey_HashType_f,
            DsTunnelIdTrillMcRpfHashKey_IngressNicknameLow_f,
            DsTunnelIdTrillMcRpfHashKey_EgressNickname_f,
            MaxField_f,
        },
        /* entry valid field id */
        {
            DsTunnelIdTrillMcRpfHashKey_Valid_f,
            MaxField_f
        },
        /* ad field id */
        {
            DsTunnelIdTrillMcRpfHashKey_AdIndex_f,
            MaxField_f
        },
        /* compare key mask */
        {
            {0xFC, 0xFFFF0000, 0xFFFF}
        },
        {MaxField_f},
        {MaxField_f}
    },

    /* USERID_HASH_TYPE_TRILL_MC_ADJ */
    /* TRILLMCADJ */
    {
        /* table id*/
        DsTunnelIdTrillMcAdjCheckHashKey_t,
        /* compared field id */
        {
            DsTunnelIdTrillMcAdjCheckHashKey_Valid_f,
            DsTunnelIdTrillMcAdjCheckHashKey_GlobalSrcPort13_f,
            DsTunnelIdTrillMcAdjCheckHashKey_HashType_f,
            DsTunnelIdTrillMcAdjCheckHashKey_GlobalSrcPort_f,
            DsTunnelIdTrillMcAdjCheckHashKey_IsLabel_f,
            DsTunnelIdTrillMcAdjCheckHashKey_EgressNickname_f,
            MaxField_f,
        },
        /* entry valid field id */
        {
            DsTunnelIdTrillMcAdjCheckHashKey_Valid_f,
            MaxField_f
        },
        /* ad field id */
        {
            DsTunnelIdTrillMcAdjCheckHashKey_AdIndex_f,
            MaxField_f
        },
        /* compare key mask */
        {
            {0x6, 0xF8000000, 0xFFFCFFFF}
        },
        {MaxField_f},
        {MaxField_f}
    },

    /* USERID_HASH_TYPE_TRILL_UC */
    /* TRILLUC */
    {
        /* table id*/
        DsTunnelIdTrillUcDecapHashKey_t,
        /* compared field id */
        {
            DsTunnelIdTrillUcDecapHashKey_IngressNicknameHigh_f,
            DsTunnelIdTrillUcDecapHashKey_Valid_f,
            DsTunnelIdTrillUcDecapHashKey_HashType_f,
            DsTunnelIdTrillUcDecapHashKey_IngressNicknameLow_f,
            DsTunnelIdTrillUcDecapHashKey_EgressNickname_f,
            MaxField_f,
        },
        /* entry valid field id */
        {
            DsTunnelIdTrillUcDecapHashKey_Valid_f,
            MaxField_f
        },
        /* ad field id */
        {
            DsTunnelIdTrillUcDecapHashKey_AdIndex_f,
            MaxField_f
        },
        /* compare key mask */
        {
            {0xFC, 0xFFFF0000, 0xFFFF}
        },
        {MaxField_f},
        {MaxField_f}
    },

    /* USERID_HASH_TYPE_TRILL_MC */
    /* TRILLMC */
    {
        /* table id*/
        DsTunnelIdTrillMcDecapHashKey_t,
        /* compared field id */
        {
            DsTunnelIdTrillMcDecapHashKey_IngressNicknameHigh_f,
            DsTunnelIdTrillMcDecapHashKey_Valid_f,
            DsTunnelIdTrillMcDecapHashKey_HashType_f,
            DsTunnelIdTrillMcDecapHashKey_IngressNicknameLow_f,
            DsTunnelIdTrillMcDecapHashKey_EgressNickname_f,
            MaxField_f,
        },
        /* entry valid field id */
        {
            DsTunnelIdTrillMcDecapHashKey_Valid_f,
            MaxField_f
        },
        /* ad field id */
        {
            DsTunnelIdTrillMcDecapHashKey_AdIndex_f,
            MaxField_f
        },
        /* compare key mask */
        {
            {0xFC, 0xFFFF0000, 0xFFFF}
        },
        {MaxField_f},
        {MaxField_f}
    },

    /* USERID_HASH_TYPE_IPV4_RPF */
    /* IPv4RPF */
    {
        /* table id*/
        DsTunnelIdIpv4RpfHashKey_t,
        /* compared field id */
        {
            DsTunnelIdIpv4RpfHashKey_Valid_f,
            DsTunnelIdIpv4RpfHashKey_HashType_f,
            DsTunnelIdIpv4RpfHashKey_IpSa_f,
            MaxField_f,
        },
        /* entry valid field id */
        {
            DsTunnelIdIpv4RpfHashKey_Valid_f,
            MaxField_f
        },
        /* ad field id */
        {
            DsTunnelIdIpv4RpfHashKey_AdIndex_f,
            MaxField_f
        },
        /* compare key mask */
        {
            {0x4, 0xF8000000, 0xFFFFFFFF}
        },
        {MaxField_f},
        {MaxField_f}
    },

    /* USERID_HASH_TYPE_MPLS_SECTION_OAM */
    /* MPLSSECTIONOAM */
    {
        /* table id*/
        DsMplsOamLabelHashKey_t,
        /* compared field id */
        {
            DsMplsOamLabelHashKey_Valid_f,
            DsMplsOamLabelHashKey_HashType_f,
            DsMplsOamLabelHashKey_MplsLabel_f,
            MaxField_f,
        },
        /* entry valid field id */
        {
            DsMplsOamLabelHashKey_Valid_f,
            MaxField_f
        },
        /* ad field id */
        {
            DsMplsOamLabelHashKey_AdIndex_f,
            MaxField_f
        },
        /* compare key mask */
        {
            {0x4, 0xF8000000, 0x3FF}
        },
        {MaxField_f},
        {MaxField_f}
    },

    /* USERID_HASH_TYPE_PBT_OAM */
    /* PBTOAM DsPbtOamHashKey */
    {
        /* table id*/
        DsPbtOamHashKey_t,
        /* compared field id */
        {
            DsPbtOamHashKey_Valid_f,
            DsPbtOamHashKey_HashType_f,
            DsPbtOamHashKey_MacSa31_0_f,
            DsPbtOamHashKey_MacSa32_f,
            DsPbtOamHashKey_MacSa43_33_f,
            DsPbtOamHashKey_MacSa45_44_f,
            DsPbtOamHashKey_MacSa47_46_f,
            DsPbtOamHashKey_VrfId_f,
            MaxField_f,
        },
        /* entry valid field id */
        {
            DsPbtOamHashKey_Valid_f,
            MaxField_f
        },
        /* ad field id */
        {
            DsPbtOamHashKey_AdIndex_f,
            MaxField_f
        },
        /* compare key mask */
        {
            {0x7FFF, 0xFFFF0000, 0xFFFFFFFF}
        },
        {MaxField_f},
        {MaxField_f}
    },

    /* USERID_HASH_TYPE_MPLS_LABEL_OAM */
    /* MPLSLABELOAM DsMplsOamLabelHashKey */
    {
        /* table id*/
        DsMplsOamLabelHashKey_t,
        /* compared field id */
        {
            DsMplsOamLabelHashKey_Valid_f,
            DsMplsOamLabelHashKey_HashType_f,
            DsMplsOamLabelHashKey_MplsLabelSpace_f,
            DsMplsOamLabelHashKey_MplsLabel_f,
            MaxField_f
        },
        /* entry valid field id */
        {
            DsMplsOamLabelHashKey_Valid_f,
            MaxField_f
        },
        /* ad field id */
        {
            DsMplsOamLabelHashKey_AdIndex_f,
            MaxField_f
        },
        /* compare key mask */
        {
            {0x4, 0xF8000000, 0xFF0FFFFF}
        },
        {MaxField_f},
        {MaxField_f}
    },

    /* USERID_HASH_TYPE_BFD_OAM */
    /* BFDOAM DsBfdOamHashKey */
    {
        /* table id*/
        DsBfdOamHashKey_t,
        /* compared field id */
        {
            DsBfdOamHashKey_Valid_f,
            DsBfdOamHashKey_HashType_f,
            DsBfdOamHashKey_MyDiscriminator_f,
            MaxField_f
        },
        /* entry valid field id */
        {
            DsBfdOamHashKey_Valid_f,
            MaxField_f
        },
        /* ad field id */
        {
            DsBfdOamHashKey_AdIndex_f,
            MaxField_f
        },
        /* compare key mask */
        {
            {0x4, 0xF8000000, 0xFFFFFFFF}
        },
        {MaxField_f},
        {MaxField_f}
    },

    /* USERID_HASH_TYPE_ETHER_FID_OAM */
    /* ETHERFIDOAM DsEthOamHashKey */
    {
        /* table id*/
        DsEthOamHashKey_t,
        /* compared field id */
        {
            DsEthOamHashKey_Valid_f,
            DsEthOamHashKey_IsFid_f,
            DsEthOamHashKey_HashType_f,
            DsEthOamHashKey_GlobalSrcPort_f,
            DsEthOamHashKey_GlobalSrcPort13_f,
            DsEthOamHashKey_VlanId_f,
            MaxField_f,
        },
        /* entry valid field id */
        {
            DsEthOamHashKey_Valid_f,
            MaxField_f
        },
        /* ad field id */
        {
            DsEthOamHashKey_AdIndex_f,
            MaxField_f
        },
        /* compare key mask */
        {
            {0x7, 0xF8000000, 0x1FFF3FFF}
        },
        {MaxField_f},
        {MaxField_f}
    },

    /* USERID_HASH_TYPE_ETHER_VLAN_OAM */
    /* ETHERVLANOAM DsEthOamHashKey */
    {
        /* table id*/
        DsEthOamHashKey_t,
        /* compared field id */
        {
            DsEthOamHashKey_Valid_f,
            DsEthOamHashKey_GlobalSrcPort13_f,
            DsEthOamHashKey_IsFid_f,
            DsEthOamHashKey_HashType_f,
            DsEthOamHashKey_GlobalSrcPort_f,
            DsEthOamHashKey_VlanId_f,
            MaxField_f
        },
        /* entry valid field id */
        {
            DsEthOamHashKey_Valid_f,
            MaxField_f
        },
        /* ad field id */
        {
            DsEthOamHashKey_AdIndex_f,
            MaxField_f
        },
        /* compare key mask */
        {
            {0x7, 0xF8000000, 0x1FFF3FFF}
        },
        {MaxField_f},
        {MaxField_f}
    },

    /* USERID_HASH_TYPE_ETHER_RMEP */
    /* ETHERRMEP DsEthOamRmepHashKey */
    {
        /* table id*/
        DsEthOamRmepHashKey_t,
        /* compared field id */
        {
            DsEthOamRmepHashKey_Valid_f,
            DsEthOamRmepHashKey_HashType_f,
            DsEthOamRmepHashKey_RmepId_f,
            DsEthOamRmepHashKey_MepIndex_f,
            MaxField_f,
        },
        /* entry valid field id */
        {
            DsEthOamRmepHashKey_Valid_f,
            MaxField_f
        },
        /* ad field id */
        {
            DsEthOamRmepHashKey_AdIndex_f,
            MaxField_f
        },
        /* compare key mask */
        {
            {0x4, 0xF8000000, 0x3FFFFFF}
        },
        {MaxField_f},
        {MaxField_f}
    }
};

/* All FIB hash key property database(include Mac, FcoE, Trill hash key) */
hash_key_property_t fib_hash_key_property_table[] =
{
    /*  MAC DA, DsMacHashKey */
    {   /* Table ID*/
        DsMacHashKey_t,
        /* Compared field ID */
        {
            DsMacHashKey_Valid_f,
            DsMacHashKey_IsMacHash_f,
            DsMacHashKey_VsiId_f,
            DsMacHashKey_MappedMac47_32_f,
            DsMacHashKey_MappedMac31_0_f,
            MaxField_f
        },
        /* Entry valid field ID */
        {
            DsMacHashKey_Valid_f,
            MaxField_f
        },
        /* AD field ID */
        {
            DsMacHashKey_DsAdIndex14_3_f,
            DsMacHashKey_DsAdIndex2_2_f,
            DsMacHashKey_DsAdIndex1_0_f,
            MaxField_f
        },
        /* compare key mask */
        {
            {0x6, 0x3FFFFFFF, 0xFFFFFFFF}
        },
        {MaxField_f},
        {MaxField_f}
    },

    /* FCOE, DsFcoeHashKey */
    {   /* Table ID*/
        DsFcoeHashKey_t,
        /* Compared field ID */
        {
            DsFcoeHashKey_HashTypeHigh_f,
            DsFcoeHashKey_Valid_f,
            DsFcoeHashKey_IsMacHash_f,
            DsFcoeHashKey_HashTypeLow_f,
            DsFcoeHashKey_VsiId_f,
            DsFcoeHashKey_FcoeDid_f,
            MaxField_f
        },
        /* Entry valid field ID */
        {
            DsFcoeHashKey_Valid_f,
            MaxField_f
        },
        /* AD field ID */
        {
            DsFcoeHashKey_DsAdIndex15_f,
            DsFcoeHashKey_DsAdIndex14_6_f,
            DsFcoeHashKey_DsAdIndex5_0_f,
            MaxField_f
        },
        /* compare key mask */
        {
            {0x3F, 0x3FFF0000, 0xFFFFFF}
        },
        {MaxField_f},
        {MaxField_f}
    },

    /* FCOE, DsFcoeRpfHashKey */
    {   /* Table ID*/
        DsFcoeRpfHashKey_t,
        /* Compared field ID */
        {
            DsFcoeRpfHashKey_HashTypeHigh_f,
            DsFcoeRpfHashKey_Valid_f,
            DsFcoeRpfHashKey_IsMacHash_f,
            DsFcoeRpfHashKey_HashTypeLow_f,
            DsFcoeRpfHashKey_VsiId_f,
            DsFcoeRpfHashKey_FcoeSid_f,
            MaxField_f
        },
        /* Entry valid field ID */
        {
            DsFcoeRpfHashKey_Valid_f,
            MaxField_f
        },
        /* AD field ID */
        {
            DsFcoeRpfHashKey_DsAdIndex15_f,
            DsFcoeRpfHashKey_DsAdIndex14_6_f,
            DsFcoeRpfHashKey_DsAdIndex5_0_f,
            MaxField_f
        },
        /* compare key mask */
        {
            {0x3F, 0x3FFF0000, 0xFFFFFF}
        },
        {MaxField_f},
        {MaxField_f}
    },

    /* TRILL Ucast, DsTrillUcastHashKey */
    {   /* Table ID*/
        DsTrillUcastHashKey_t,
        /* Compared field ID */
        {
            DsTrillUcastHashKey_HashTypeHigh_f,
            DsTrillUcastHashKey_Valid_f,
            DsTrillUcastHashKey_IsMacHash_f,
            DsTrillUcastHashKey_HashTypeLow_f,
            DsTrillUcastHashKey_EgressNickname_f,
            MaxField_f
        },
        /* Entry valid field ID */
        {
            DsTrillUcastHashKey_Valid_f,
            MaxField_f
        },
        /* AD field ID */
        {
            DsTrillUcastHashKey_DsAdIndex15_f,
            DsTrillUcastHashKey_DsAdIndex14_6_f,
            DsTrillUcastHashKey_DsAdIndex5_0_f,
            MaxField_f
        },
        /* compare key mask */
        {
            {0x3F, 0x0, 0xFFFF}
        },
        {MaxField_f},
        {MaxField_f}
    },

    /* TRILL Mcast, DsTrillMcastHashKey */
    {   /* Table ID*/
        DsTrillMcastHashKey_t,
        /* Compared field ID */
        {
            DsTrillMcastHashKey_HashTypeHigh_f,
            DsTrillMcastHashKey_Valid_f,
            DsTrillMcastHashKey_IsMacHash_f,
            DsTrillMcastHashKey_HashTypeLow_f,
            DsTrillMcastHashKey_EgressNickname_f,
            MaxField_f
        },
        /* Entry valid field ID */
        {
            DsTrillMcastHashKey_Valid_f,
            MaxField_f
        },
        /* AD field ID */
        {
            DsTrillMcastHashKey_DsAdIndex15_f,
            DsTrillMcastHashKey_DsAdIndex14_6_f,
            DsTrillMcastHashKey_DsAdIndex5_0_f,
            MaxField_f
        },
        /* compare key mask */
        {
            {0x3F, 0x0, 0xFFFF}
        },
        {MaxField_f},
        {MaxField_f}
    },

    /* TRILL Mcast VLAN, DsTrillMcastVlanHashKey */
    {   /* Table ID*/
        DsTrillMcastVlanHashKey_t,
        /* Compared field ID */
        {
            DsTrillMcastVlanHashKey_HashTypeHigh_f,
            DsTrillMcastVlanHashKey_Valid_f,
            DsTrillMcastVlanHashKey_IsMacHash_f,
            DsTrillMcastVlanHashKey_HashTypeLow_f,
            DsTrillMcastVlanHashKey_VlanId_f,
            DsTrillMcastVlanHashKey_EgressNickname_f,
            MaxField_f
        },
        /* Entry valid field ID */
        {
            DsTrillMcastVlanHashKey_Valid_f,
            MaxField_f
        },
        /* AD field ID */
        {
            DsTrillMcastVlanHashKey_DsAdIndex15_f,
            DsTrillMcastVlanHashKey_DsAdIndex14_6_f,
            DsTrillMcastVlanHashKey_DsAdIndex5_0_f,
            MaxField_f
        },
        /* compare key mask */
        {
            {0x3F, 0x0FFF0000, 0xFFFF}
        },
        {MaxField_f},
        {MaxField_f}
    },

    /* ACL, DsAclQosMacHashKey */
    {   /* Table ID*/
        DsAclQosMacHashKey_t,
        /* Compared field ID */
        {
            DsAclQosMacHashKey_Cos_f,
            DsAclQosMacHashKey_EtherType11_0_f,
            DsAclQosMacHashKey_EtherType15_12_f,
            DsAclQosMacHashKey_GlobalSrcPort10_0_f,
            DsAclQosMacHashKey_GlobalSrcPort13_11_f,
            DsAclQosMacHashKey_HashType0High_f,
            DsAclQosMacHashKey_HashType0Low_f,
            DsAclQosMacHashKey_HashType1High_f,
            DsAclQosMacHashKey_HashType1Low_f,
            DsAclQosMacHashKey_IsIpv4Key_f,
            DsAclQosMacHashKey_IsLogicPort_f,
            DsAclQosMacHashKey_IsMacHash0_f,
            DsAclQosMacHashKey_IsMacHash1_f,
            DsAclQosMacHashKey_MacDa31_0_f,
            DsAclQosMacHashKey_MacDa47_32_f,
            DsAclQosMacHashKey_Valid0_f,
            DsAclQosMacHashKey_Valid1_f,
            DsAclQosMacHashKey_VlanId_f,
            MaxField_f
        },
        /* Entry valid field ID */
        {
            DsAclQosMacHashKey_Valid0_f,
            DsAclQosMacHashKey_Valid1_f,
            MaxField_f
        },
        /* AD field ID */
        {
            DsAclQosMacHashKey_DsAdIndex15_f,
            DsAclQosMacHashKey_DsAdIndex14_7_f,
            DsAclQosMacHashKey_DsAdIndex6_0_f,
            MaxField_f
        },
        /* compare key mask */
        {
            {0x3FF,  0x7FFFFFFF, 0x0000FFFF},
            {0x403F, 0x07FF0000, 0xFFFFFFFF}
        },
        {MaxField_f},
        {MaxField_f}
    },

    /* ACL, DsAclQosIpv4HashKey */
    {   /* Table ID*/
        DsAclQosIpv4HashKey_t,
        /* Compared field ID */
        {
            DsAclQosIpv4HashKey_Dscp_f,
            DsAclQosIpv4HashKey_GlobalSrcPort2_0_f,
            DsAclQosIpv4HashKey_GlobalSrcPort13_3_f,
            DsAclQosIpv4HashKey_HashType0High_f,
            DsAclQosIpv4HashKey_HashType0Low_f,
            DsAclQosIpv4HashKey_HashType1High_f,
            DsAclQosIpv4HashKey_HashType1Low_f,
            DsAclQosIpv4HashKey_IpDa_f,
            DsAclQosIpv4HashKey_IpSa_f,
            DsAclQosIpv4HashKey_IsArpKey_f,
            DsAclQosIpv4HashKey_IsIpv4Key_f,
            DsAclQosIpv4HashKey_IsLogicPort_f,
            DsAclQosIpv4HashKey_IsMacHash0_f,
            DsAclQosIpv4HashKey_IsMacHash1_f,
            DsAclQosIpv4HashKey_L4DestPort8_0_f,
            DsAclQosIpv4HashKey_L4DestPort12_9_f,
            DsAclQosIpv4HashKey_L4DestPort13_f,
            DsAclQosIpv4HashKey_L4DestPort15_14_f,
            DsAclQosIpv4HashKey_L4SourcePort_f,
            DsAclQosIpv4HashKey_Layer3HeaderProtocol_f,
            DsAclQosIpv4HashKey_Valid0_f,
            DsAclQosIpv4HashKey_Valid1_f,
            MaxField_f
        },
        /* Entry valid field ID */
        {
            DsAclQosIpv4HashKey_Valid0_f,
            DsAclQosIpv4HashKey_Valid1_f,
            MaxField_f
        },
        /* AD field ID */
        {
            DsAclQosIpv4HashKey_DsAdIndex15_f,
            DsAclQosIpv4HashKey_DsAdIndex14_7_f,
            DsAclQosIpv4HashKey_DsAdIndex6_0_f,
            MaxField_f
        },
        /* compare key mask */
        {
            {0x3F3F, 0xFFFFFFFF, 0xFFFFFFFF},
            {0x403F, 0x7FFFFF80, 0xFFFFFFFF}
        },
        {MaxField_f},
        {MaxField_f}
    }
};

/* All Queue hash key property database */
hash_key_property_t queue_hash_key_property_table[] =
{
    /* DsQueueHashKey_t */
    {
        /* Table ID*/
        DsQueueHashKey_t,
        /* Compared field ID0 */
        {
            DsQueueHashKey_HashType0_f,
            DsQueueHashKey_Sgmac0_f,
            DsQueueHashKey_DestQueue0_f,
            DsQueueHashKey_Valid0_f,
            DsQueueHashKey_ChannelId0_f,
            DsQueueHashKey_Mcast0_f,
            DsQueueHashKey_DestChipId0_f,
            DsQueueHashKey_ServiceId0_f,
            DsQueueHashKey_Fid0_f,
            MaxField_f
        },
        /* Entry valid field ID0-1 */
        {
            DsQueueHashKey_Valid0_f,
            MaxField_f
        },
        /* AD field ID */
        {
            DsQueueHashKey_QueueBase0_f,
            MaxField_f
        },
        /* compare key mask */
        {
            {0x7F3F, 0xFFFFBF3F, 0x3FFF3FFF},
        },
        {MaxField_f},
        {MaxField_f}
    }
};

hash_key_property_t* p_hash_key_property[] = {userid_hash_key_property_table, fib_hash_key_property_table,
                                              lpm_hash_key_property_table, queue_hash_key_property_table};


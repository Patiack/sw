/**
 @file sys_greatbelt_vlan_classification.c

 @date 2009-12-30

 @version v2.0
*/

#include "ctc_debug.h"
#include "ctc_error.h"
#include "ctc_vector.h"
#include "sys_greatbelt_ftm.h"
#include "sys_greatbelt_vlan_classification.h"
#include "sys_greatbelt_scl.h"
#include "sys_greatbelt_chip.h"

#include "greatbelt/include/drv_lib.h"
/******************************************************************************
*
*   Macros, Globals and Defines
*
*******************************************************************************/
#define IS_MAC_ZERO(mac)   (0 == (mac[0] + mac[1] + mac[2] + mac[3] + mac[4] + mac[5]))
#define IS_MAC_ALLF(mac) \
    ((0xFF == mac[0]) && (0xFF == mac[1]) && (0xFF == mac[2]) \
     && (0xFF == mac[3]) && (0xFF == mac[4]) && (0xFF == mac[5]))
#define IS_IPV6_ZERO(ip)   (0 == (ip[0] + ip[1] + ip[2] + ip[3]))
#define IS_IPV6_ALLF(ip)   ((0xFFFFFFFF == ip[0]) && (0xFFFFFFFF == ip[1]) && (0xFFFFFFFF == ip[2]) && (0xFFFFFFFF == ip[3]))

struct sys_vlan_class_protocol_vlan_s
{
    uint16 vlan_id;
    uint8 cos;
    uint8 cos_valid;
};
typedef struct sys_vlan_class_protocol_vlan_s sys_vlan_class_protocol_vlan_t;

typedef struct
{
    uint16  ether_type[8];
    uint32  mac_eid;
    uint32  ipv4_eid;
    uint32  ipv6_eid;
    sal_mutex_t* mutex;
}sys_vlan_class_master_t;

static sys_vlan_class_master_t* vlan_class_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};


#define SYS_VLAN_CLASS_INIT_CHECK(lchip)           \
    do {                                      \
        SYS_LCHIP_CHECK_ACTIVE(lchip);             \
        if (NULL == vlan_class_master[lchip]) \
        {                                     \
            return CTC_E_NOT_INIT;            \
        }                                     \
    } while (0)                               \

#define SYS_VLAN_CLASS_CREATE_LOCK(lchip)                       \
    do                                                          \
    {                                                           \
        sal_mutex_create(&vlan_class_master[lchip]->mutex);     \
        if (NULL == vlan_class_master[lchip]->mutex)            \
        {                                                       \
            CTC_ERROR_RETURN(CTC_E_FAIL_CREATE_MUTEX);          \
        }                                                       \
    } while (0)

#define SYS_VLAN_CLASS_LOCK(lchip) \
    sal_mutex_lock(vlan_class_master[lchip]->mutex)

#define SYS_VLAN_CLASS_UNLOCK(lchip) \
    sal_mutex_unlock(vlan_class_master[lchip]->mutex)

#define CTC_ERROR_RETURN_VLAN_CLASS_UNLOCK(op) \
    do \
    { \
        int32 rv = (op); \
        if (rv < 0) \
        { \
            sal_mutex_unlock(vlan_class_master[lchip]->mutex); \
            return rv; \
        } \
    } while (0)

#define SYS_VLAN_CLASS_DBG_OUT(level, FMT, ...)                                         \
    do                                                                                  \
    {                                                                                   \
        CTC_DEBUG_OUT(vlan, vlan_class, VLAN_CLASS_SYS, level, FMT, ##__VA_ARGS__);     \
    } while (0)

#define SYS_VLAN_CLASS_DBG_FUNC() \
    do                                                                                  \
    {                                                                                   \
        SYS_VLAN_CLASS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, " %% FUNC: %s()\n", __FUNCTION__); \
    } while (0)

#define SYS_VLAN_CLASS_DBG_INFO(FMT, ...) \
    do                                                                                  \
    {                                                                                   \
        SYS_VLAN_CLASS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, FMT, ##__VA_ARGS__);               \
    } while (0)

#define SYS_VLAN_CLASS_DBG_ERROR(FMT, ...) \
    do                                                                                  \
    {                                                                                   \
        SYS_VLAN_CLASS_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, FMT, ##__VA_ARGS__);              \
    } while (0)

#define SYS_VLAN_CLASS_DBG_PRAM(FMT, ...) \
    do                                                                                  \
    {                                                                                   \
        SYS_VLAN_CLASS_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, FMT, ##__VA_ARGS__);              \
    } while (0)

#define SYS_VLAN_CLASS_SET_MAC_HIGH(dest_h, src)                     \
    do                                                                   \
    {                                                                    \
        dest_h = ((src[0] << 8) | (src[1]));                                  \
    } while (0)                                                            \


#define SYS_VLAN_CLASS_SET_MAC_LOW(dest_l, src)                      \
    do                                                                   \
    {                                                                    \
        dest_l = ((src[2] << 24) | (src[3] << 16) | (src[4] << 8) | (src[5])); \
    } while (0)                                                            \


#define SYS_VLAN_CLASS_IS_ALL_ONE(mask, is_all_one)     \
    do                                                  \
    {                                                   \
        int i = 0;                                      \
        for (i = 0; i < sizeof(mac_addr_t); i++)        \
        {                                               \
            if (0xFF != mask[i])                        \
            {                                           \
                is_all_one = FALSE;                     \
                break;                                  \
            }                                           \
        }                                               \
        is_all_one = TRUE;                              \
    } while (0)


#define SYS_VLAN_CLASS_IS_ALL_ZERO(mask, is_all_zero)   \
    do                                                  \
    {                                                   \
        int i = 0;                                      \
        for (i = 0; i < sizeof(mac_addr_t); i++)        \
        {                                               \
            if (0 != mask[i])                           \
            {                                           \
                is_all_zero = FALSE;                    \
                break;                                  \
            }                                           \
        }                                               \
        is_all_zero = TRUE;                             \
    } while (0)


#define SYS_VLAN_CLASS_INFO_MAC(str, mac)               \
    do                                                  \
    {                                                   \
        SYS_VLAN_CLASS_DBG_INFO("  %% INFO:  %s  :%02X%02X-%02X%02X-%02X%02X\n", \
            str, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]); \
    } while (0)

#define SYS_VLAN_CLASS_INFO_IPV4(str, ipv4sa)           \
    do                                                  \
    {                                                   \
        SYS_VLAN_CLASS_DBG_INFO("  %% INFO:  %s :%d.%d.%d.%d\n", \
            str, (ipv4sa >> 24) & 0xFF, (ipv4sa >> 16) & 0xFF, (ipv4sa >> 8) & 0xFF, ipv4sa & 0xFF); \
    } while (0)


#define SYS_VLAN_CLASS_INFO_IPV6(str, ipv6sa)           \
    do                                                  \
    {                                                   \
    } while (0)


/******************************************************************************
*
*   Functions
*
*******************************************************************************/

/******************************************************************************
*
*   protocol vlan
*******************************************************************************/
STATIC int32
_sys_greatbelt_protocol_vlan_add(uint8 lchip, uint16 ether_type, sys_vlan_class_protocol_vlan_t* pro_vlan)
{
    uint32 cmd = 0;
    ds_protocol_vlan_t ds_protocol_vlan;
    bool free = FALSE;
    uint8 first_free = 0;
    uint8 idx = 0;

    SYS_VLAN_CLASS_DBG_FUNC();

    sal_memset(&ds_protocol_vlan, 0, sizeof(ds_protocol_vlan_t));

    SYS_VLAN_CLASS_LOCK(lchip);
    for (idx = 0; idx < 8; idx++)
    {

        if (((0 == vlan_class_master[lchip]->ether_type[idx])
             || (ether_type == vlan_class_master[lchip]->ether_type[idx])) && (free == FALSE))
        {
            first_free = idx;
            free = TRUE;
        }
        else if ((ether_type == vlan_class_master[lchip]->ether_type[idx]) && (free == TRUE))
        {
            first_free = idx;
            break;
        }
    }
    SYS_VLAN_CLASS_UNLOCK(lchip);

    if (!free)
    {
        return CTC_E_VLAN_CLASS_PROTOCOL_VLAN_FULL;
    }
    else
    {
        SYS_VLAN_CLASS_LOCK(lchip);
        vlan_class_master[lchip]->ether_type[first_free] = ether_type;
        SYS_VLAN_CLASS_UNLOCK(lchip);

        cmd = DRV_IOR(DsProtocolVlan_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ds_protocol_vlan));

        switch (first_free & 0x7)
        {
        case 0:
            ds_protocol_vlan.protocol_vlan_id_valid0  = 1;
            ds_protocol_vlan.protocol_vlan_id0        = pro_vlan->vlan_id;
            ds_protocol_vlan.protocol_cos0            = pro_vlan->cos;
            ds_protocol_vlan.protocol_cos_valid0      = pro_vlan->cos_valid;
            ds_protocol_vlan.protocol_ether_type0     = ether_type;
            ds_protocol_vlan.protocol_replace_tag_en0 = 0;
            break;

        case 1:
            ds_protocol_vlan.protocol_vlan_id_valid1  = 1;
            ds_protocol_vlan.protocol_vlan_id1        = pro_vlan->vlan_id;
            ds_protocol_vlan.protocol_cos1            = pro_vlan->cos;
            ds_protocol_vlan.protocol_cos_valid1      = pro_vlan->cos_valid;
            ds_protocol_vlan.protocol_ether_type1     = ether_type;
            ds_protocol_vlan.protocol_replace_tag_en1 = 0;
            break;

        case 2:
            ds_protocol_vlan.protocol_vlan_id_valid2  = 1;
            ds_protocol_vlan.protocol_vlan_id2        = pro_vlan->vlan_id;
            ds_protocol_vlan.protocol_cos2            = pro_vlan->cos;
            ds_protocol_vlan.protocol_cos_valid2      = pro_vlan->cos_valid;
            ds_protocol_vlan.protocol_ether_type2     = ether_type;
            ds_protocol_vlan.protocol_replace_tag_en2 = 0;
            break;

        case 3:
            ds_protocol_vlan.protocol_vlan_id_valid3  = 1;
            ds_protocol_vlan.protocol_vlan_id3        = pro_vlan->vlan_id;
            ds_protocol_vlan.protocol_cos3            = pro_vlan->cos;
            ds_protocol_vlan.protocol_cos_valid3      = pro_vlan->cos_valid;
            ds_protocol_vlan.protocol_ether_type3     = ether_type;
            ds_protocol_vlan.protocol_replace_tag_en3 = 0;
            break;

        case 4:
            ds_protocol_vlan.protocol_vlan_id_valid4  = 1;
            ds_protocol_vlan.protocol_vlan_id4        = pro_vlan->vlan_id;
            ds_protocol_vlan.protocol_cos4            = pro_vlan->cos;
            ds_protocol_vlan.protocol_cos_valid4      = pro_vlan->cos_valid;
            ds_protocol_vlan.protocol_ether_type4     = ether_type;
            ds_protocol_vlan.protocol_replace_tag_en4 = 0;
            break;

        case 5:
            ds_protocol_vlan.protocol_vlan_id_valid5  = 1;
            ds_protocol_vlan.protocol_vlan_id5        = pro_vlan->vlan_id;
            ds_protocol_vlan.protocol_cos5            = pro_vlan->cos;
            ds_protocol_vlan.protocol_cos_valid5      = pro_vlan->cos_valid;
            ds_protocol_vlan.protocol_ether_type5     = ether_type;
            ds_protocol_vlan.protocol_replace_tag_en5 = 0;
            break;

        case 6:
            ds_protocol_vlan.protocol_vlan_id_valid6  = 1;
            ds_protocol_vlan.protocol_vlan_id6        = pro_vlan->vlan_id;
            ds_protocol_vlan.protocol_cos6            = pro_vlan->cos;
            ds_protocol_vlan.protocol_cos_valid6      = pro_vlan->cos_valid;
            ds_protocol_vlan.protocol_ether_type6     = ether_type;
            ds_protocol_vlan.protocol_replace_tag_en6 = 0;
            break;

        case 7:
            ds_protocol_vlan.protocol_vlan_id_valid7  = 1;
            ds_protocol_vlan.protocol_vlan_id7        = pro_vlan->vlan_id;
            ds_protocol_vlan.protocol_cos7            = pro_vlan->cos;
            ds_protocol_vlan.protocol_cos_valid7      = pro_vlan->cos_valid;
            ds_protocol_vlan.protocol_ether_type7     = ether_type;
            ds_protocol_vlan.protocol_replace_tag_en7 = 0;
            break;

        default:
            break;
        }

        cmd = DRV_IOW(DsProtocolVlan_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ds_protocol_vlan));
    }

    return CTC_E_NONE;
}

int32
_sys_greatbelt_protocol_vlan_remove(uint8 lchip, uint16 ether_type)
{

    uint32 cmd = 0;
    uint8 index = 0;
    ds_protocol_vlan_t ds_protocol_vlan;

    SYS_VLAN_CLASS_DBG_FUNC();

    SYS_VLAN_CLASS_LOCK(lchip);
    for (index = 0; index < 8; index++)
    {
        if (ether_type == vlan_class_master[lchip]->ether_type[index])
        {
            break;
        }
    }
    SYS_VLAN_CLASS_UNLOCK(lchip);

    if (index >= 8)
    {
        SYS_VLAN_CLASS_DBG_INFO("  %% INFO: ether_type :%d none-exist.\n", ether_type);
        return CTC_E_ENTRY_NOT_EXIST;
    }

    sal_memset(&ds_protocol_vlan, 0, sizeof(ds_protocol_vlan));
    cmd = DRV_IOR(DsProtocolVlan_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ds_protocol_vlan));

    switch (index & 0x7)
    {
    case 0:
        ds_protocol_vlan.protocol_vlan_id_valid0  = 0;
        ds_protocol_vlan.protocol_vlan_id0        = 0;
        ds_protocol_vlan.protocol_cos0            = 0;
        ds_protocol_vlan.protocol_cos_valid0      = 0;
        ds_protocol_vlan.protocol_ether_type0     = 0;
        ds_protocol_vlan.protocol_replace_tag_en0 = 0;
        break;

    case 1:
        ds_protocol_vlan.protocol_vlan_id_valid1  = 0;
        ds_protocol_vlan.protocol_vlan_id1        = 0;
        ds_protocol_vlan.protocol_cos1            = 0;
        ds_protocol_vlan.protocol_cos_valid1      = 0;
        ds_protocol_vlan.protocol_ether_type1     = 0;
        ds_protocol_vlan.protocol_replace_tag_en1 = 0;
        break;

    case 2:
        ds_protocol_vlan.protocol_vlan_id_valid2  = 0;
        ds_protocol_vlan.protocol_vlan_id2        = 0;
        ds_protocol_vlan.protocol_cos2            = 0;
        ds_protocol_vlan.protocol_cos_valid2      = 0;
        ds_protocol_vlan.protocol_ether_type2     = 0;
        ds_protocol_vlan.protocol_replace_tag_en2 = 0;
        break;

    case 3:
        ds_protocol_vlan.protocol_vlan_id_valid3  = 0;
        ds_protocol_vlan.protocol_vlan_id3        = 0;
        ds_protocol_vlan.protocol_cos3            = 0;
        ds_protocol_vlan.protocol_cos_valid3      = 0;
        ds_protocol_vlan.protocol_ether_type3     = 0;
        ds_protocol_vlan.protocol_replace_tag_en3 = 0;
        break;

    case 4:
        ds_protocol_vlan.protocol_vlan_id_valid4  = 0;
        ds_protocol_vlan.protocol_vlan_id4        = 0;
        ds_protocol_vlan.protocol_cos4            = 0;
        ds_protocol_vlan.protocol_cos_valid4      = 0;
        ds_protocol_vlan.protocol_ether_type4     = 0;
        ds_protocol_vlan.protocol_replace_tag_en4 = 0;
        break;

    case 5:
        ds_protocol_vlan.protocol_vlan_id_valid5  = 0;
        ds_protocol_vlan.protocol_vlan_id5        = 0;
        ds_protocol_vlan.protocol_cos5            = 0;
        ds_protocol_vlan.protocol_cos_valid5      = 0;
        ds_protocol_vlan.protocol_ether_type5     = 0;
        ds_protocol_vlan.protocol_replace_tag_en5 = 0;
        break;

    case 6:
        ds_protocol_vlan.protocol_vlan_id_valid6  = 0;
        ds_protocol_vlan.protocol_vlan_id6        = 0;
        ds_protocol_vlan.protocol_cos6            = 0;
        ds_protocol_vlan.protocol_cos_valid6      = 0;
        ds_protocol_vlan.protocol_ether_type6     = 0;
        ds_protocol_vlan.protocol_replace_tag_en6 = 0;
        break;

    case 7:
        ds_protocol_vlan.protocol_vlan_id_valid7  = 0;
        ds_protocol_vlan.protocol_vlan_id7        = 0;
        ds_protocol_vlan.protocol_cos7            = 0;
        ds_protocol_vlan.protocol_cos_valid7      = 0;
        ds_protocol_vlan.protocol_ether_type7     = 0;
        ds_protocol_vlan.protocol_replace_tag_en7 = 0;
        break;

    default:
        break;
    }

    cmd = DRV_IOW(DsProtocolVlan_t, DRV_ENTRY_FLAG);

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ds_protocol_vlan));

    SYS_VLAN_CLASS_LOCK(lchip);
    vlan_class_master[lchip]->ether_type[index] = 0;
    SYS_VLAN_CLASS_UNLOCK(lchip);

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_vlan_class_build_key(uint8 lchip, ctc_vlan_class_t* p_vlan_class_in, sys_scl_entry_t* ctc, uint8* need_again)
{
    uint8       is_tcp = 0;
    uint8       is_udp = 0;

    CTC_PTR_VALID_CHECK(p_vlan_class_in);
    CTC_PTR_VALID_CHECK(ctc);

    switch (p_vlan_class_in->type)
    {
    case CTC_VLAN_CLASS_MAC:
        if (IS_MAC_ZERO(p_vlan_class_in->vlan_class.mac))
        {
            return CTC_E_INVALID_PARAM;
        }

        SYS_VLAN_CLASS_INFO_MAC("mac", p_vlan_class_in->vlan_class.mac);
        ctc->key.u.hash_port_mac_key.mac_is_da = (p_vlan_class_in->flag & CTC_VLAN_CLASS_FLAG_USE_MACDA) ? 1 : 0;
        sal_memcpy(ctc->key.u.hash_port_mac_key.mac, p_vlan_class_in->vlan_class.mac, sizeof(mac_addr_t));
        ctc->key.type  = SYS_SCL_KEY_HASH_MAC;

        break;

    case CTC_VLAN_CLASS_IPV4:
        if (!(p_vlan_class_in->flag & CTC_VLAN_CLASS_FLAG_USE_FLEX))
        {
            SYS_VLAN_CLASS_INFO_IPV4("ipv4sa", p_vlan_class_in->vlan_class.ipv4_sa);
            ctc->key.u.hash_port_ipv4_key.ip_sa = p_vlan_class_in->vlan_class.ipv4_sa;
            /* -1 vlan id */
            /* dir  key_type  chip_id*/

            ctc->key.type  = SYS_SCL_KEY_HASH_IPV4;
        }
        else
        {
            /* ipv4 SA*/
            SYS_VLAN_CLASS_INFO_IPV4("ipv4_sa", p_vlan_class_in->vlan_class.flex_ipv4.ipv4_sa);
            CTC_SET_FLAG(ctc->key.u.tcam_ipv4_key.flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_IP_SA);
            ctc->key.u.tcam_ipv4_key.ip_sa = p_vlan_class_in->vlan_class.flex_ipv4.ipv4_sa;
            ctc->key.u.tcam_ipv4_key.ip_sa_mask = p_vlan_class_in->vlan_class.flex_ipv4.ipv4_sa_mask;

            /* ipv4 DA*/
            SYS_VLAN_CLASS_INFO_IPV4("ipv4_da", p_vlan_class_in->vlan_class.flex_ipv4.ipv4_da);
            CTC_SET_FLAG(ctc->key.u.tcam_ipv4_key.flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_IP_DA);
            ctc->key.u.tcam_ipv4_key.ip_da = p_vlan_class_in->vlan_class.flex_ipv4.ipv4_da;
            ctc->key.u.tcam_ipv4_key.ip_da_mask = p_vlan_class_in->vlan_class.flex_ipv4.ipv4_da_mask;


            if (!IS_MAC_ZERO(p_vlan_class_in->vlan_class.flex_ipv4.macsa))
            {
                /* MAC SA*/
                SYS_VLAN_CLASS_INFO_MAC("macsa", p_vlan_class_in->vlan_class.flex_ipv4.macsa);

                CTC_SET_FLAG(ctc->key.u.tcam_ipv4_key.flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_MAC_SA);
                sal_memcpy(ctc->key.u.tcam_ipv4_key.mac_sa, p_vlan_class_in->vlan_class.flex_ipv4.macsa, sizeof(mac_addr_t));
                sal_memset(ctc->key.u.tcam_ipv4_key.mac_sa_mask, 0xFF, sizeof(mac_addr_t));
            }

            if (!IS_MAC_ZERO(p_vlan_class_in->vlan_class.flex_ipv4.macda))
            {
                /* MAC DA*/
                SYS_VLAN_CLASS_INFO_MAC("macda", p_vlan_class_in->vlan_class.flex_ipv4.macda);

                CTC_SET_FLAG(ctc->key.u.tcam_ipv4_key.flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_MAC_DA);
                sal_memcpy(ctc->key.u.tcam_ipv4_key.mac_da, p_vlan_class_in->vlan_class.flex_ipv4.macda, sizeof(mac_addr_t));
                sal_memset(ctc->key.u.tcam_ipv4_key.mac_da_mask, 0xFF, sizeof(mac_addr_t));
            }

            CTC_MAX_VALUE_CHECK(p_vlan_class_in->vlan_class.flex_ipv4.l3_type, MAX_CTC_PARSER_L3_TYPE - 1);

            if (0 != p_vlan_class_in->vlan_class.flex_ipv4.l3_type)
            {
                SYS_VLAN_CLASS_DBG_INFO("  %% INFO:  l3type                       :%d\n",
                                        p_vlan_class_in->vlan_class.flex_ipv4.l3_type);
                CTC_SET_FLAG(ctc->key.u.tcam_ipv4_key.flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_L3_TYPE);
                ctc->key.u.tcam_ipv4_key.l3_type = p_vlan_class_in->vlan_class.flex_ipv4.l3_type;
                ctc->key.u.tcam_ipv4_key.l3_type_mask = 0xFF;
            }

            CTC_MAX_VALUE_CHECK(p_vlan_class_in->vlan_class.flex_ipv4.l4_type, MAX_CTC_PARSER_L4_TYPE - 1);

            if (CTC_PARSER_L4_TYPE_TCP == p_vlan_class_in->vlan_class.flex_ipv4.l4_type)
            {
                is_tcp = 1;
                SYS_VLAN_CLASS_DBG_INFO("  %% INFO:  l4type                       :tcp\n");
                CTC_SET_FLAG(ctc->key.u.tcam_ipv4_key.flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_L4_TYPE);
                CTC_SET_FLAG(ctc->key.u.tcam_ipv4_key.flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_L4_PROTOCOL);
                ctc->key.u.tcam_ipv4_key.l4_protocol = SYS_SCL_TCP;
                ctc->key.u.tcam_ipv4_key.l4_protocol_mask = 0xFF;

                ctc->key.u.tcam_ipv4_key.l4_type = CTC_PARSER_L4_TYPE_TCP;
                ctc->key.u.tcam_ipv4_key.l4_type_mask = 0xFF;
            }
            else if (CTC_PARSER_L4_TYPE_UDP == p_vlan_class_in->vlan_class.flex_ipv4.l4_type)
            {
                is_udp = 1;
                SYS_VLAN_CLASS_DBG_INFO("  %% INFO:  l4type                       :udp\n");
                CTC_SET_FLAG(ctc->key.u.tcam_ipv4_key.flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_L4_TYPE);
                CTC_SET_FLAG(ctc->key.u.tcam_ipv4_key.flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_L4_PROTOCOL);

                ctc->key.u.tcam_ipv4_key.l4_protocol = SYS_SCL_UDP;
                ctc->key.u.tcam_ipv4_key.l4_protocol_mask = 0xFF;

                ctc->key.u.tcam_ipv4_key.l4_type = CTC_PARSER_L4_TYPE_UDP;
                ctc->key.u.tcam_ipv4_key.l4_type_mask = 0xFF;
            }
            else if (CTC_PARSER_L4_TYPE_NONE != p_vlan_class_in->vlan_class.flex_ipv4.l4_type)
            {
                CTC_SET_FLAG(ctc->key.u.tcam_ipv4_key.flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_L4_TYPE);

                ctc->key.u.tcam_ipv4_key.l4_type = p_vlan_class_in->vlan_class.flex_ipv4.l4_type;
                ctc->key.u.tcam_ipv4_key.l4_type_mask = 0xFF;
            }


            if (0 != p_vlan_class_in->vlan_class.flex_ipv4.l4src_port)
            {
                SYS_VLAN_CLASS_DBG_INFO("  %% INFO:  l4srcport                    :%d\n",
                                        p_vlan_class_in->vlan_class.flex_ipv4.l4src_port);
                CTC_SET_FLAG(ctc->key.u.tcam_ipv4_key.flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_L4_PROTOCOL);
                CTC_SET_FLAG(ctc->key.u.tcam_ipv4_key.sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_L4_SRC_PORT);
                if (is_tcp)
                {
                    ctc->key.u.tcam_ipv4_key.l4_protocol = SYS_SCL_TCP;
                    ctc->key.u.tcam_ipv4_key.l4_protocol_mask = 0xFF;
                }
                else if (is_udp)
                {
                    ctc->key.u.tcam_ipv4_key.l4_protocol = SYS_SCL_UDP;
                    ctc->key.u.tcam_ipv4_key.l4_protocol_mask = 0xFF;
                }
                else
                {
                    if (0 == *need_again)
                    {
                        ctc->key.u.tcam_ipv4_key.l4_protocol = SYS_SCL_TCP;
                        ctc->key.u.tcam_ipv4_key.l4_protocol_mask = 0xFF;
                        *need_again = 1;
                    }
                    else
                    {
                        ctc->key.u.tcam_ipv4_key.l4_protocol = SYS_SCL_UDP;
                        ctc->key.u.tcam_ipv4_key.l4_protocol_mask = 0xFF;
                        *need_again = 0;
                    }
                }
                ctc->key.u.tcam_ipv4_key.l4_src_port = p_vlan_class_in->vlan_class.flex_ipv4.l4src_port;
                ctc->key.u.tcam_ipv4_key.l4_src_port_mask = 0xFFFF;
            }

            if (0 != p_vlan_class_in->vlan_class.flex_ipv4.l4dest_port)
            {
                SYS_VLAN_CLASS_DBG_INFO("  %% INFO:  l4destport                   :%d\n",
                                        p_vlan_class_in->vlan_class.flex_ipv4.l4dest_port);
                CTC_SET_FLAG(ctc->key.u.tcam_ipv4_key.flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_L4_PROTOCOL);
                CTC_SET_FLAG(ctc->key.u.tcam_ipv4_key.sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_L4_DST_PORT);
                if (is_tcp)
                {
                    ctc->key.u.tcam_ipv4_key.l4_protocol = SYS_SCL_TCP;
                    ctc->key.u.tcam_ipv4_key.l4_protocol_mask = 0xFF;
                }
                else if (is_udp)
                {
                    ctc->key.u.tcam_ipv4_key.l4_protocol = SYS_SCL_UDP;
                    ctc->key.u.tcam_ipv4_key.l4_protocol_mask = 0xFF;
                }
                else
                {
                    if (0 == *need_again)
                    {
                        ctc->key.u.tcam_ipv4_key.l4_protocol = SYS_SCL_TCP;
                        ctc->key.u.tcam_ipv4_key.l4_protocol_mask = 0xFF;
                        *need_again = 1;
                    }
                    else
                    {
                        ctc->key.u.tcam_ipv4_key.l4_protocol = SYS_SCL_UDP;
                        ctc->key.u.tcam_ipv4_key.l4_protocol_mask = 0xFF;
                        *need_again = 0;
                    }
                }
                ctc->key.u.tcam_ipv4_key.l4_dst_port = p_vlan_class_in->vlan_class.flex_ipv4.l4dest_port;
                ctc->key.u.tcam_ipv4_key.l4_dst_port_mask = 0xFFFF;
            }

            ctc->key.type  = SYS_SCL_KEY_TCAM_IPV4;
        }

        break;

    case CTC_VLAN_CLASS_IPV6:
        if (!(p_vlan_class_in->flag & CTC_VLAN_CLASS_FLAG_USE_FLEX))
        {
            SYS_VLAN_CLASS_INFO_IPV6("ipv6sa", p_vlan_class_in->vlan_class.ipv6_sa);
            sal_memcpy((ctc->key.u.hash_ipv6_key.ip_sa),
                       (p_vlan_class_in->vlan_class.ipv6_sa), sizeof(ipv6_addr_t));

            ctc->key.type  = SYS_SCL_KEY_HASH_IPV6;
        }
        else
        {
            /* ipv6 SA*/
            CTC_SET_FLAG(ctc->key.u.tcam_ipv6_key.flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_IP_SA);

            sal_memcpy(ctc->key.u.tcam_ipv6_key.ip_sa,
                       (p_vlan_class_in->vlan_class.flex_ipv6.ipv6_sa), sizeof(ipv6_addr_t));
            sal_memcpy(ctc->key.u.tcam_ipv6_key.ip_sa_mask,
                       (p_vlan_class_in->vlan_class.flex_ipv6.ipv6_sa_mask), sizeof(ipv6_addr_t));

            /* ipv6 DA*/
            CTC_SET_FLAG(ctc->key.u.tcam_ipv6_key.flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_IP_DA);

            sal_memcpy(ctc->key.u.tcam_ipv6_key.ip_da,
                       (p_vlan_class_in->vlan_class.flex_ipv6.ipv6_da), sizeof(ipv6_addr_t));
            sal_memcpy(ctc->key.u.tcam_ipv6_key.ip_da_mask,
                       (p_vlan_class_in->vlan_class.flex_ipv6.ipv6_da_mask), sizeof(ipv6_addr_t));

            if (!IS_MAC_ZERO(p_vlan_class_in->vlan_class.flex_ipv6.macsa))
            {
                /* MAC SA*/
                SYS_VLAN_CLASS_INFO_MAC("macsa", p_vlan_class_in->vlan_class.flex_ipv6.macsa);

                CTC_SET_FLAG(ctc->key.u.tcam_ipv6_key.flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_MAC_SA);
                sal_memcpy(ctc->key.u.tcam_ipv6_key.mac_sa, p_vlan_class_in->vlan_class.flex_ipv6.macsa, sizeof(mac_addr_t));
                sal_memset(ctc->key.u.tcam_ipv6_key.mac_sa_mask, 0xFF, sizeof(mac_addr_t));
            }

            if (!IS_MAC_ZERO(p_vlan_class_in->vlan_class.flex_ipv6.macda))
            {
                /* MAC DA*/
                SYS_VLAN_CLASS_INFO_MAC("macda", p_vlan_class_in->vlan_class.flex_ipv6.macda);

                CTC_SET_FLAG(ctc->key.u.tcam_ipv6_key.flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_MAC_DA);
                sal_memcpy(ctc->key.u.tcam_ipv6_key.mac_da, p_vlan_class_in->vlan_class.flex_ipv6.macda, sizeof(mac_addr_t));
                sal_memset(ctc->key.u.tcam_ipv6_key.mac_da_mask, 0xFF, sizeof(mac_addr_t));
            }

            if (0 != p_vlan_class_in->vlan_class.flex_ipv6.l4src_port)
            {
                CTC_SET_FLAG(ctc->key.u.tcam_ipv6_key.flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_L4_PROTOCOL);
                CTC_SET_FLAG(ctc->key.u.tcam_ipv6_key.sub_flag, CTC_SCL_TCAM_IPV6_KEY_SUB_FLAG_L4_SRC_PORT);
                ctc->key.u.tcam_ipv6_key.l4_protocol = SYS_SCL_TCP;
                ctc->key.u.tcam_ipv6_key.l4_protocol_mask = 0xFF;
                ctc->key.u.tcam_ipv6_key.l4_src_port = p_vlan_class_in->vlan_class.flex_ipv6.l4src_port;
                ctc->key.u.tcam_ipv6_key.l4_src_port_mask = 0xFFFF;
            }

            if (0 != p_vlan_class_in->vlan_class.flex_ipv6.l4dest_port)
            {
                CTC_SET_FLAG(ctc->key.u.tcam_ipv6_key.flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_L4_PROTOCOL);
                CTC_SET_FLAG(ctc->key.u.tcam_ipv6_key.sub_flag, CTC_SCL_TCAM_IPV6_KEY_SUB_FLAG_L4_DST_PORT);
                ctc->key.u.tcam_ipv6_key.l4_protocol = SYS_SCL_TCP;
                ctc->key.u.tcam_ipv6_key.l4_protocol_mask = 0xFF;
                ctc->key.u.tcam_ipv6_key.l4_dst_port = p_vlan_class_in->vlan_class.flex_ipv6.l4dest_port;
                ctc->key.u.tcam_ipv6_key.l4_dst_port_mask = 0xFFFF;
            }

            CTC_MAX_VALUE_CHECK(p_vlan_class_in->vlan_class.flex_ipv6.l4_type, MAX_CTC_PARSER_L4_TYPE - 1);

            CTC_SET_FLAG(ctc->key.u.tcam_ipv6_key.flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_L4_TYPE);

            ctc->key.u.tcam_ipv6_key.l4_type = p_vlan_class_in->vlan_class.flex_ipv6.l4_type;
            ctc->key.u.tcam_ipv6_key.l4_type_mask = 0xFF;

            ctc->key.type  = SYS_SCL_KEY_TCAM_IPV6;
        }

        break;

    default:
        return CTC_E_VLAN_CLASS_INVALID_TYPE;
    }

    return CTC_E_NONE;
}


STATIC uint32
_sys_greatbelt_vlan_class_get_scl_gid(uint8 lchip, uint8 type)
{
    uint32 gid = 0;
    switch (type)
    {
    case SYS_SCL_KEY_HASH_MAC:
        gid = SYS_SCL_GROUP_ID_INNER_HASH_VLAN_CLASS_MAC;
        break;
    case SYS_SCL_KEY_HASH_IPV4:
        gid = SYS_SCL_GROUP_ID_INNER_HASH_VLAN_CLASS_IPV4;
        break;
    case SYS_SCL_KEY_HASH_IPV6:
        gid = SYS_SCL_GROUP_ID_INNER_HASH_VLAN_CLASS_IPV6;
        break;
    case SYS_SCL_KEY_TCAM_MAC:
        gid = SYS_SCL_GROUP_ID_INNER_TCAM_VLAN_CLASS_MAC;
        break;
    case SYS_SCL_KEY_TCAM_IPV4:
        gid = SYS_SCL_GROUP_ID_INNER_TCAM_VLAN_CLASS_IPV4;
        break;
    case SYS_SCL_KEY_TCAM_IPV6:
        gid = SYS_SCL_GROUP_ID_INNER_TCAM_VLAN_CLASS_IPV6;
        break;
    default:
        break;
    }

    return gid;
}

int32
sys_greatbelt_vlan_add_vlan_class(uint8 lchip, ctc_vlan_class_t* p_vlan_class)
{
    sys_scl_entry_t scl;
    sys_vlan_class_protocol_vlan_t pro_vlan;

    SYS_VLAN_CLASS_DBG_FUNC();
    SYS_VLAN_CLASS_DBG_INFO("  %% INFO: ------vlan class----------\n");
    SYS_VLAN_CLASS_DBG_INFO("  %% INFO:  type                         :%d\n", p_vlan_class->type);

    SYS_VLAN_CLASS_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_vlan_class);
    CTC_VLAN_RANGE_CHECK(p_vlan_class->vlan_id);

    sal_memset(&scl, 0, sizeof(sys_scl_entry_t));

    if (p_vlan_class->flag & CTC_VLAN_CLASS_FLAG_OUTPUT_COS)
    {
        CTC_COS_RANGE_CHECK(p_vlan_class->cos);
    }

    if (CTC_VLAN_CLASS_PROTOCOL == p_vlan_class->type)
    {
        sal_memset(&pro_vlan, 0, sizeof(sys_vlan_class_protocol_vlan_t));
        pro_vlan.vlan_id        = p_vlan_class->vlan_id;
        pro_vlan.cos            = p_vlan_class->cos;
        pro_vlan.cos_valid      = CTC_FLAG_ISSET(p_vlan_class->flag, CTC_VLAN_CLASS_FLAG_OUTPUT_COS);

        CTC_ERROR_RETURN(_sys_greatbelt_protocol_vlan_add(lchip, p_vlan_class->vlan_class.ether_type, &pro_vlan));
    }
    else
    {
        uint32 gid = 0;
        uint8  need_again = 0;

        /*vlan class action*/

        scl.action.type = SYS_SCL_ACTION_INGRESS;
        CTC_SET_FLAG(scl.action.u.igs_action.flag, CTC_SCL_IGS_ACTION_FLAG_VLAN_EDIT);
        scl.action.u.igs_action.vlan_edit.svid_new = p_vlan_class->vlan_id;
        scl.action.u.igs_action.vlan_edit.svid_sl  = CTC_VLAN_TAG_SL_NEW;
        scl.action.u.igs_action.vlan_edit.stag_op  = CTC_VLAN_TAG_OP_REP_OR_ADD;

        if (p_vlan_class->flag & CTC_VLAN_CLASS_FLAG_OUTPUT_COS)
        {
            scl.action.u.igs_action.vlan_edit.scos_new = p_vlan_class->cos;
            scl.action.u.igs_action.vlan_edit.scos_sl  = CTC_VLAN_TAG_SL_NEW;
        }


        CTC_ERROR_RETURN(_sys_greatbelt_vlan_class_build_key(lchip, p_vlan_class, &scl, &need_again));

        gid = _sys_greatbelt_vlan_class_get_scl_gid(lchip, scl.key.type);
        CTC_ERROR_RETURN(sys_greatbelt_scl_add_entry(lchip, gid, &scl, 1));
        CTC_ERROR_RETURN(sys_greatbelt_scl_install_entry(lchip, scl.entry_id, 1));
        if (need_again)
        {
            CTC_ERROR_RETURN(_sys_greatbelt_vlan_class_build_key(lchip, p_vlan_class, &scl, &need_again));
            CTC_ERROR_RETURN(sys_greatbelt_scl_add_entry(lchip, gid, &scl, 1));
            CTC_ERROR_RETURN(sys_greatbelt_scl_install_entry(lchip, scl.entry_id, 1));
        }
    }

    SYS_VLAN_CLASS_DBG_INFO("  %% INFO: %-30s:%d\n", ">>>>>>>>>>>>>>>>>>>>>>vlan", p_vlan_class->vlan_id);

    return CTC_E_NONE;
}

int32
sys_greatbelt_vlan_remove_vlan_class(uint8 lchip, ctc_vlan_class_t* p_vlan_class)
{
    sys_scl_entry_t scl;
    uint8           need_again = 0;
    uint32          gid     = 0;

    SYS_VLAN_CLASS_DBG_FUNC();
    SYS_VLAN_CLASS_DBG_INFO("  %% INFO: ------vlan class----------\n");
    SYS_VLAN_CLASS_DBG_INFO("  %% INFO:  type                         :%d\n", p_vlan_class->type);

    SYS_VLAN_CLASS_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_vlan_class);

    sal_memset(&scl, 0, sizeof(sys_scl_entry_t));

    if (CTC_VLAN_CLASS_PROTOCOL == p_vlan_class->type)
    {
        CTC_ERROR_RETURN(_sys_greatbelt_protocol_vlan_remove(lchip, p_vlan_class->vlan_class.ether_type));
    }
    else
    {
        CTC_ERROR_RETURN(_sys_greatbelt_vlan_class_build_key(lchip, p_vlan_class, &scl, &need_again));

        gid = _sys_greatbelt_vlan_class_get_scl_gid(lchip, scl.key.type);
        CTC_ERROR_RETURN(sys_greatbelt_scl_get_entry_id(lchip, &scl, gid));
        CTC_ERROR_RETURN(sys_greatbelt_scl_uninstall_entry(lchip, scl.entry_id, 1));
        CTC_ERROR_RETURN(sys_greatbelt_scl_remove_entry(lchip, scl.entry_id, 1));
        if (need_again)
        {
            CTC_ERROR_RETURN(_sys_greatbelt_vlan_class_build_key(lchip, p_vlan_class, &scl, &need_again));

            CTC_ERROR_RETURN(sys_greatbelt_scl_get_entry_id(lchip, &scl, gid));
            CTC_ERROR_RETURN(sys_greatbelt_scl_uninstall_entry(lchip, scl.entry_id, 1));
            CTC_ERROR_RETURN(sys_greatbelt_scl_remove_entry(lchip, scl.entry_id, 1));
        }
    }

    return CTC_E_NONE;
}


int32
sys_greatbelt_vlan_remove_all_vlan_class(uint8 lchip, ctc_vlan_class_type_t type)
{
    int32 ret = 0;
    uint32 entry_num = 0;
    uint8 index = 0;
    uint32 cmd = 0;
    ds_protocol_vlan_t ds_pro_vlan;
    uint8 pro_vlan_idx = 0;

    SYS_VLAN_CLASS_INIT_CHECK(lchip);

    switch (type)
    {
    case CTC_VLAN_CLASS_MAC:
        ret = sys_greatbelt_scl_uninstall_group(lchip, SYS_SCL_GROUP_ID_INNER_HASH_VLAN_CLASS_MAC, 1);
        ret = (ret<0) ? ret : sys_greatbelt_scl_uninstall_group(lchip, SYS_SCL_GROUP_ID_INNER_TCAM_VLAN_CLASS_MAC, 1);
        ret = (ret<0) ? ret : sys_greatbelt_scl_remove_all_entry(lchip, SYS_SCL_GROUP_ID_INNER_HASH_VLAN_CLASS_MAC, 1);
        ret = (ret<0) ? ret : sys_greatbelt_scl_remove_all_entry(lchip, SYS_SCL_GROUP_ID_INNER_TCAM_VLAN_CLASS_MAC, 1);

        break;

    case CTC_VLAN_CLASS_IPV4:
        ret = sys_greatbelt_scl_uninstall_group(lchip, SYS_SCL_GROUP_ID_INNER_HASH_VLAN_CLASS_IPV4, 1);
        ret = (ret<0) ? ret : sys_greatbelt_scl_uninstall_group(lchip, SYS_SCL_GROUP_ID_INNER_TCAM_VLAN_CLASS_IPV4, 1);
        ret = (ret<0) ? ret : sys_greatbelt_scl_remove_all_entry(lchip, SYS_SCL_GROUP_ID_INNER_HASH_VLAN_CLASS_IPV4, 1);
        ret = (ret<0) ? ret : sys_greatbelt_scl_remove_all_entry(lchip, SYS_SCL_GROUP_ID_INNER_TCAM_VLAN_CLASS_IPV4, 1);

        break;

    case CTC_VLAN_CLASS_IPV6:
        ret = sys_greatbelt_scl_uninstall_group(lchip, SYS_SCL_GROUP_ID_INNER_HASH_VLAN_CLASS_IPV6, 1);
        ret = (ret<0) ? ret : sys_greatbelt_scl_uninstall_group(lchip, SYS_SCL_GROUP_ID_INNER_TCAM_VLAN_CLASS_IPV6, 1);
        ret = (ret<0) ? ret : sys_greatbelt_scl_remove_all_entry(lchip, SYS_SCL_GROUP_ID_INNER_HASH_VLAN_CLASS_IPV6, 1);
        ret = (ret<0) ? ret : sys_greatbelt_scl_remove_all_entry(lchip, SYS_SCL_GROUP_ID_INNER_TCAM_VLAN_CLASS_IPV6, 1);

        break;

    case CTC_VLAN_CLASS_PROTOCOL:
        sys_greatbelt_ftm_get_table_entry_num(lchip, DsProtocolVlan_t, &entry_num);

        for (pro_vlan_idx = 0; pro_vlan_idx < entry_num; pro_vlan_idx++)
        {
            sal_memset(&ds_pro_vlan, 0, sizeof(ds_protocol_vlan_t));
            cmd = DRV_IOW(DsProtocolVlan_t, DRV_ENTRY_FLAG);
            ret = ret ? ret : DRV_IOCTL(lchip, pro_vlan_idx, cmd, &ds_pro_vlan);
        }

        SYS_VLAN_CLASS_LOCK(lchip);
        for (index = 0; index < 8; index++)
        {
            vlan_class_master[lchip]->ether_type[index] = 0;
        }
        SYS_VLAN_CLASS_UNLOCK(lchip);

        break;

    default:
        return CTC_E_VLAN_CLASS_INVALID_TYPE;
    }
    return ret;
}



STATIC int32
_sys_greatbelt_vlan_class_build_default_action(uint8 lchip, ctc_scl_igs_action_t* pa, ctc_vlan_miss_t* p_def_action)
{

    switch (p_def_action->flag)
    {
    case CTC_VLAN_MISS_ACTION_DO_NOTHING:
        break;

    case CTC_VLAN_MISS_ACTION_TO_CPU:
        CTC_SET_FLAG(pa->flag, CTC_SCL_IGS_ACTION_FLAG_COPY_TO_CPU);
        CTC_SET_FLAG(pa->flag, CTC_SCL_IGS_ACTION_FLAG_DISCARD);
        break;


    case CTC_VLAN_MISS_ACTION_DISCARD:
        CTC_SET_FLAG(pa->flag, CTC_SCL_IGS_ACTION_FLAG_DISCARD);
        break;

    case CTC_VLAN_MISS_ACTION_OUTPUT_VLANPTR:
        CTC_SET_FLAG(pa->flag, CTC_SCL_IGS_ACTION_FLAG_USER_VLANPTR);
        pa->user_vlanptr    = p_def_action->user_vlanptr;
        break;

    default:
        return CTC_E_INVALID_PARAM;

    }

    return CTC_E_NONE;
}

/**
 * vlan class default only support 3 type: mac-sa ipv4-sa ipv6-sa.
 */
int32
sys_greatbelt_vlan_add_default_vlan_class(uint8 lchip, ctc_vlan_class_type_t class_type, ctc_vlan_miss_t* p_def_action)
{
    sys_scl_action_t action;
    uint32          eid;

    SYS_VLAN_CLASS_INIT_CHECK(lchip);
    sal_memset(&action, 0, sizeof(action));

    switch (class_type)
    {
        case CTC_VLAN_CLASS_MAC:
            eid = vlan_class_master[lchip]->mac_eid;
            break;
        case CTC_VLAN_CLASS_IPV4:
            eid = vlan_class_master[lchip]->ipv4_eid;
            break;
        case CTC_VLAN_CLASS_IPV6:
            eid = vlan_class_master[lchip]->ipv6_eid;
            break;
        case CTC_VLAN_CLASS_PROTOCOL:
            SYS_VLAN_CLASS_DBG_ERROR("  %% ERRO: Vlan class default entry only supports Mac Ipv4 Ipv6.\n");
            return CTC_E_FEATURE_NOT_SUPPORT;

        default:
            return CTC_E_INVALID_PARAM;
    }

    if (0 == eid)
    {
        return CTC_E_NO_RESOURCE;
    }

    CTC_ERROR_RETURN(_sys_greatbelt_vlan_class_build_default_action(lchip, &action.u.igs_action, p_def_action));
    CTC_ERROR_RETURN(sys_greatbelt_scl_update_action(lchip, eid, &action, 1));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_vlan_init_default_vlan_class(uint8 lchip)
{
    int32           ret= 0;
    sys_scl_entry_t scl;
    uint32 table_size = 0;

    sal_memset(&scl, 0, sizeof(scl));

    /* if add entry failed, indicates no resources. thus make eid = 0 */

    CTC_ERROR_RETURN(sys_greatbelt_ftm_get_table_entry_num(lchip, DsUserIdMacKey_t, &table_size));
    if (table_size != 0)
    {
        scl.key.type = SYS_SCL_KEY_TCAM_MAC;
        ret = sys_greatbelt_scl_add_entry(lchip, SYS_SCL_GROUP_ID_INNER_DEFT_VLAN_CLASS, &scl, 1);
        if (CTC_E_NONE == ret)
        {
            vlan_class_master[lchip]->mac_eid = scl.entry_id;
            CTC_ERROR_RETURN(sys_greatbelt_scl_set_entry_priority(lchip, scl.entry_id, 0, 1));
        }
    }

    CTC_ERROR_RETURN(sys_greatbelt_ftm_get_table_entry_num(lchip, DsUserIdIpv4Key_t, &table_size));
    if (table_size != 0)
    {
        scl.key.type = SYS_SCL_KEY_TCAM_IPV4;
        ret = sys_greatbelt_scl_add_entry(lchip, SYS_SCL_GROUP_ID_INNER_DEFT_VLAN_CLASS, &scl, 1);
        if (CTC_E_NONE == ret)
        {
            vlan_class_master[lchip]->ipv4_eid = scl.entry_id;
            CTC_ERROR_RETURN(sys_greatbelt_scl_set_entry_priority(lchip, scl.entry_id, 0, 1));
        }
    }

    CTC_ERROR_RETURN(sys_greatbelt_ftm_get_table_entry_num(lchip, DsUserIdIpv6Key_t, &table_size));
    if (table_size != 0)
    {
        scl.key.type = SYS_SCL_KEY_TCAM_IPV6;
        ret = sys_greatbelt_scl_add_entry(lchip, SYS_SCL_GROUP_ID_INNER_DEFT_VLAN_CLASS, &scl, 1);
        if (CTC_E_NONE == ret)
        {
            vlan_class_master[lchip]->ipv6_eid = scl.entry_id;
            CTC_ERROR_RETURN(sys_greatbelt_scl_set_entry_priority(lchip, scl.entry_id, 0, 1));
        }
    }

    CTC_ERROR_RETURN(sys_greatbelt_scl_install_group(lchip, SYS_SCL_GROUP_ID_INNER_DEFT_VLAN_CLASS, NULL, 1));

    return CTC_E_NONE;

}

STATIC int32
_sys_greatbelt_vlan_remove_default_vlan_class(uint8 lchip)
{
    CTC_ERROR_RETURN(sys_greatbelt_scl_uninstall_entry(lchip, vlan_class_master[lchip]->mac_eid, 1));
    CTC_ERROR_RETURN(sys_greatbelt_scl_remove_entry(lchip, vlan_class_master[lchip]->mac_eid, 1));
    vlan_class_master[lchip]->mac_eid = 0;

    CTC_ERROR_RETURN(sys_greatbelt_scl_uninstall_entry(lchip, vlan_class_master[lchip]->ipv4_eid, 1));
    CTC_ERROR_RETURN(sys_greatbelt_scl_remove_entry(lchip, vlan_class_master[lchip]->ipv4_eid, 1));
    vlan_class_master[lchip]->ipv4_eid = 0;

    CTC_ERROR_RETURN(sys_greatbelt_scl_uninstall_entry(lchip, vlan_class_master[lchip]->ipv6_eid, 1));
    CTC_ERROR_RETURN(sys_greatbelt_scl_remove_entry(lchip, vlan_class_master[lchip]->ipv6_eid, 1));
    vlan_class_master[lchip]->ipv6_eid = 0;

    return CTC_E_NONE;
}


STATIC int32
_sys_greatbelt_vlan_get_default_vlan_class(uint8 lchip, ctc_vlan_class_type_t class_type)
{
    sys_scl_action_t action;
    uint32          eid;

    sal_memset(&action, 0, sizeof(action));
    SYS_VLAN_CLASS_INIT_CHECK(lchip);

    switch (class_type)
    {
    case CTC_VLAN_CLASS_MAC:
        eid = vlan_class_master[lchip]->mac_eid;
        SYS_VLAN_CLASS_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Mac Base         ");
        break;

    case CTC_VLAN_CLASS_IPV4:
        eid = vlan_class_master[lchip]->ipv4_eid;
        SYS_VLAN_CLASS_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "IPv4 Base        ");
        break;

    case CTC_VLAN_CLASS_IPV6:
        eid = vlan_class_master[lchip]->ipv6_eid;
        SYS_VLAN_CLASS_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "IPv6 Base        ");
        break;

    case CTC_VLAN_CLASS_PROTOCOL:
        SYS_VLAN_CLASS_DBG_ERROR("  %% ERRO: Vlan class default entry only supports Mac Ipv4 Ipv6.\n");
        return CTC_E_FEATURE_NOT_SUPPORT;

    default:
        return CTC_E_INVALID_PARAM;
    }

    if (0 == eid)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    CTC_ERROR_RETURN(sys_greatbelt_scl_get_action(lchip, eid, &action, 1));

    if (CTC_FLAG_ISSET(action.u.igs_action.flag, CTC_SCL_IGS_ACTION_FLAG_COPY_TO_CPU))
    {
        SYS_VLAN_CLASS_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "FWD_TO_CPU\n");
    }
    else if (CTC_FLAG_ISSET(action.u.igs_action.flag, CTC_SCL_IGS_ACTION_FLAG_DISCARD))
    {
        SYS_VLAN_CLASS_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "DISCARD\n");
    }
    else if (CTC_FLAG_ISSET(action.u.igs_action.flag, CTC_SCL_IGS_ACTION_FLAG_USER_VLANPTR))
    {
        SYS_VLAN_CLASS_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "USER_VLANPTR\n");
    }
    else
    {
        SYS_VLAN_CLASS_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "FWD\n");
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_vlan_show_default_vlan_class(uint8 lchip)
{
    ctc_vlan_class_type_t class_type;
    SYS_VLAN_CLASS_INIT_CHECK(lchip);

    SYS_VLAN_CLASS_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "TYPE            Default-Action\n");
    SYS_VLAN_CLASS_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "------------------------------\n");
    for (class_type = CTC_VLAN_CLASS_MAC; class_type <= CTC_VLAN_CLASS_IPV6; class_type++)
    {
        CTC_ERROR_RETURN(_sys_greatbelt_vlan_get_default_vlan_class(lchip, class_type));

    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_vlan_remove_default_vlan_class(uint8 lchip, ctc_vlan_class_type_t class_type)
{
    sys_scl_action_t action;
    uint32          eid;

    SYS_VLAN_CLASS_INIT_CHECK(lchip);
    /* do nothing */
    sal_memset(&action, 0, sizeof(action));

    switch (class_type)
    {
        case CTC_VLAN_CLASS_MAC:
            eid = vlan_class_master[lchip]->mac_eid;
            break;
        case CTC_VLAN_CLASS_IPV4:
            eid = vlan_class_master[lchip]->ipv4_eid;
            break;
        case CTC_VLAN_CLASS_IPV6:
            eid = vlan_class_master[lchip]->ipv6_eid;
            break;
        case CTC_VLAN_CLASS_PROTOCOL:
            SYS_VLAN_CLASS_DBG_ERROR("  %% ERRO: Vlan class default entry only supports Mac Ipv4 Ipv6.\n");
            return CTC_E_FEATURE_NOT_SUPPORT;

        default:
            return CTC_E_INVALID_PARAM;
    }

    if (0 == eid)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    CTC_ERROR_RETURN(sys_greatbelt_scl_update_action(lchip, eid, &action, 1));

    return CTC_E_NONE;
}

int32
sys_greatbelt_vlan_class_tcam_reinit(uint8 lchip, uint8 is_add)
{
    if (is_add)
    {
        CTC_ERROR_RETURN(_sys_greatbelt_vlan_init_default_vlan_class(lchip));
    }
    else
    {
        CTC_ERROR_RETURN(_sys_greatbelt_vlan_remove_default_vlan_class(lchip));
    }
    return CTC_E_NONE;
}

int32
sys_greatbelt_vlan_class_init(uint8 lchip, void* vlan_global_cfg)
{


    LCHIP_CHECK(lchip);
    if (vlan_class_master[lchip])
    {
        return CTC_E_NONE;
    }

    CTC_PTR_VALID_CHECK(vlan_global_cfg);
    MALLOC_ZERO(MEM_VLAN_MODULE, vlan_class_master[lchip], sizeof(sys_vlan_class_master_t));
    if (!vlan_class_master[lchip])
    {
        return CTC_E_NO_MEMORY;
    }

    SYS_VLAN_CLASS_CREATE_LOCK(lchip);
    CTC_ERROR_RETURN(_sys_greatbelt_vlan_init_default_vlan_class(lchip));

    sys_greatbelt_ftm_tcam_cb_register(lchip, SYS_FTM_TCAM_KEY_VLAN_CLASS, sys_greatbelt_vlan_class_tcam_reinit);

    return CTC_E_NONE;

}

int32
sys_greatbelt_vlan_class_deinit(uint8 lchip)
{
    LCHIP_CHECK(lchip);
    if (NULL == vlan_class_master[lchip])
    {
        return CTC_E_NONE;
    }

    sal_mutex_destroy(vlan_class_master[lchip]->mutex);
    mem_free(vlan_class_master[lchip]);

    return CTC_E_NONE;
}


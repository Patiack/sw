/**
 @file sys_goldengate_aclqos_policer.c

 @date 2009-10-16

 @version v2.0

*/

/****************************************************************************
  *
  * Header Files
  *
  ****************************************************************************/
#include "ctc_error.h"
#include "ctc_const.h"
#include "ctc_macro.h"
#include "ctc_debug.h"

#include "sys_goldengate_chip.h"
#include "sys_goldengate_qos_class.h"
#include "sys_goldengate_queue_enq.h"

#include "goldengate/include/drv_enum.h"
#include "goldengate/include/drv_io.h"


/****************************************************************************
  *
  * Defines and Macros
  *
  ****************************************************************************/

#define SYS_QOS_CLASS_DOMAIN_MAX 7
#define SYS_QOS_CLASS_COLOR_MAX (MAX_CTC_QOS_COLOR - 1)
#define SYS_QOS_CLASS_COS_MAX 7
#define SYS_QOS_CLASS_CFI_MAX 1
#define SYS_QOS_CLASS_IP_PREC_MAX 7
#define SYS_QOS_CLASS_DSCP_MAX 63
#define SYS_QOS_CLASS_ECN_MAX 3
#define SYS_QOS_CLASS_EXP_MAX 7

#define SYS_QOS_CLASS_DBG_OUT(level, FMT, ...) \
    do { \
        CTC_DEBUG_OUT(qos, class, QOS_CLASS_SYS, level, FMT, ##__VA_ARGS__); \
    } while (0);

#define SYS_QOS_CLASS_DBG_FUNC()           SYS_QOS_CLASS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__)
#define SYS_QOS_CLASS_DBG_INFO(FMT, ...)  SYS_QOS_CLASS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, FMT, ##__VA_ARGS__)
#define SYS_QOS_CLASS_DBG_ERROR(FMT, ...) SYS_QOS_CLASS_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, FMT, ##__VA_ARGS__)
#define SYS_QOS_CLASS_DBG_PARAM(FMT, ...) SYS_QOS_CLASS_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, FMT, ##__VA_ARGS__)
#define SYS_QOS_CLASS_DBG_DUMP(FMT, ...)  SYS_QOS_CLASS_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,  FMT, ##__VA_ARGS__)

/****************************************************************************
  *
  * Functions
  *
  ****************************************************************************/

/**
 @brief set cos + cfi -> priority + color mapping table for the given domain
*/
int32
sys_goldengate_qos_set_igs_cos_map_table(uint8 lchip, uint8 domain,
                                        uint8 cos,
                                        uint8 cfi,
                                        uint8 priority,
                                        uint8 color)
{

    uint32 field_id_priority = 0;
    uint32 field_id_color    = 0;
    uint32 tbl_index         = 0;
    uint32 field_index       = 0;
    uint32 cmd               = 0;
    uint32 field_val         = 0;

    SYS_QOS_CLASS_DBG_FUNC();

    CTC_MAX_VALUE_CHECK(domain, SYS_QOS_CLASS_DOMAIN_MAX);
    CTC_MAX_VALUE_CHECK(cos, SYS_QOS_CLASS_COS_MAX);
    CTC_MAX_VALUE_CHECK(cfi, SYS_QOS_CLASS_CFI_MAX);
    CTC_MAX_VALUE_CHECK(priority, SYS_QOS_CLASS_PRIORITY_MAX);
    CTC_MAX_VALUE_CHECK(color, SYS_QOS_CLASS_COLOR_MAX);

    tbl_index = (domain << 4) | (cos << 1) | cfi;

    field_index = tbl_index % 4;

    field_id_priority = IpeClassificationCosMap_array_0__priority_f + field_index*2;
    field_id_color    = IpeClassificationCosMap_array_0_color_f + field_index*2;

    cmd = DRV_IOW(IpeClassificationCosMap_t, field_id_priority);
    field_val = priority;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, tbl_index / 4, cmd, &field_val));

    cmd = DRV_IOW(IpeClassificationCosMap_t, field_id_color);
    field_val = color;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, tbl_index / 4, cmd, &field_val));


    SYS_QOS_CLASS_DBG_INFO("cos + cfi -> priority + color map, domain = %d, cos = %d, cfi = %d, priority = %d, color = %d\n",
                           domain, cos, cfi, priority, color);


    return CTC_E_NONE;
}

/**
 @brief get cos -> priority + color mapping table for the given domain
*/
int32
sys_goldengate_qos_get_igs_cos_map_table(uint8 lchip, uint8 domain,
                                        uint8 cos,
                                        uint8 cfi,
                                        uint8* priority,
                                        uint8* color)
{

    uint32 field_id_priority = 0;
    uint32 field_id_color    = 0;
    uint32 tbl_index         = 0;
    uint32 field_index       = 0;
    uint32 cmd               = 0;
    uint32 field_val         = 0;


    SYS_QOS_CLASS_DBG_FUNC();

    CTC_MAX_VALUE_CHECK(domain, SYS_QOS_CLASS_DOMAIN_MAX);
    CTC_MAX_VALUE_CHECK(cos, SYS_QOS_CLASS_COS_MAX);
    CTC_MAX_VALUE_CHECK(cfi, SYS_QOS_CLASS_CFI_MAX);

    tbl_index = (domain << 4) | (cos << 1) | cfi;

    field_index = tbl_index % 4;

    field_id_priority = IpeClassificationCosMap_array_0__priority_f + field_index*2;
    field_id_color    = IpeClassificationCosMap_array_0_color_f + field_index*2;


    cmd = DRV_IOR(IpeClassificationCosMap_t, field_id_priority);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, tbl_index / 4, cmd, &field_val));
    *priority = field_val;

    cmd = DRV_IOR(IpeClassificationCosMap_t, field_id_color);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, tbl_index / 4, cmd, &field_val));
    *color = field_val;

    return CTC_E_NONE;

}

/**
 @brief set ip precedence -> priority + color mapping table for the given domain
*/
int32
sys_goldengate_qos_set_igs_ip_prec_map_table(uint8 lchip, uint8 domain,
                                            uint8 ip_prec,
                                            uint8 priority,
                                            uint8 color)
{

    uint32 field_id_priority = 0;
    uint32 field_id_color    = 0;
    uint32 tbl_index         = 0;
    uint32 field_index       = 0;
    uint32 cmd               = 0;
    uint32 field_val         = 0;

    SYS_QOS_CLASS_DBG_FUNC();

    CTC_MAX_VALUE_CHECK(domain, SYS_QOS_CLASS_DOMAIN_MAX);
    CTC_MAX_VALUE_CHECK(ip_prec, SYS_QOS_CLASS_IP_PREC_MAX);
    CTC_MAX_VALUE_CHECK(priority, SYS_QOS_CLASS_PRIORITY_MAX);
    CTC_MAX_VALUE_CHECK(color, SYS_QOS_CLASS_COLOR_MAX);

    tbl_index = (domain << 3) | ip_prec;

    field_index = tbl_index % 4;

    field_id_priority = IpeClassificationPrecedenceMap_array_0__priority_f + field_index*2;
    field_id_color    = IpeClassificationPrecedenceMap_array_0_color_f + field_index*2;

    cmd = DRV_IOW(IpeClassificationPrecedenceMap_t, field_id_priority);
    field_val = priority;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, tbl_index / 4, cmd, &field_val));

    cmd = DRV_IOW(IpeClassificationPrecedenceMap_t, field_id_color);
    field_val = color;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, tbl_index / 4, cmd, &field_val));


    SYS_QOS_CLASS_DBG_INFO("ip-prec -> priority + color map, domain = %d, ip_prec = %d, priority = %d, color = %d\n",
                           domain, ip_prec, priority, color);


    return CTC_E_NONE;
}

/**
 @brief get ip precedence -> priority + color mapping table for the given domain
*/
int32
sys_goldengate_qos_get_igs_ip_prec_map_table(uint8 lchip, uint8 domain,
                                            uint8 ip_prec,
                                            uint8* priority,
                                            uint8* color)
{

    uint32 field_id_priority = 0;
    uint32 field_id_color    = 0;
    uint32 tbl_index         = 0;
    uint32 field_index       = 0;
    uint32 cmd               = 0;
    uint32 field_val         = 0;


    SYS_QOS_CLASS_DBG_FUNC();

    CTC_MAX_VALUE_CHECK(domain, SYS_QOS_CLASS_DOMAIN_MAX);
    CTC_MAX_VALUE_CHECK(ip_prec, SYS_QOS_CLASS_IP_PREC_MAX);

    tbl_index = (domain << 3) | ip_prec;

    field_index = tbl_index % 4;

    field_id_priority = IpeClassificationPrecedenceMap_array_0__priority_f + field_index*2;
    field_id_color    = IpeClassificationPrecedenceMap_array_0_color_f + field_index*2;

    cmd = DRV_IOR(IpeClassificationPrecedenceMap_t, field_id_priority);

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, tbl_index / 4, cmd, &field_val));
    *priority = field_val;

    cmd = DRV_IOR(IpeClassificationPrecedenceMap_t, field_id_color);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, tbl_index / 4, cmd, &field_val));
    *color = field_val;


    return CTC_E_NONE;
}

/**
 @brief set dscp -> priority + color mapping table for the given domain
*/
int32
sys_goldengate_qos_set_igs_dscp_map_table(uint8 lchip, uint8 domain,
                                         uint8 dscp,
                                         uint8 ecn,
                                         uint8 priority,
                                         uint8 color)
{

    uint32 field_id_priority = 0;
    uint32 field_id_color    = 0;
    uint32 tbl_index         = 0;
    uint32 field_index       = 0;
    uint32 cmd               = 0;
    uint32 field_val         = 0;

    SYS_QOS_CLASS_DBG_FUNC();

    CTC_MAX_VALUE_CHECK(domain, SYS_QOS_CLASS_DOMAIN_MAX);
    CTC_MAX_VALUE_CHECK(dscp, SYS_QOS_CLASS_DSCP_MAX);
    CTC_MAX_VALUE_CHECK(ecn, SYS_QOS_CLASS_ECN_MAX);
    CTC_MAX_VALUE_CHECK(priority, SYS_QOS_CLASS_PRIORITY_MAX);
    CTC_MAX_VALUE_CHECK(color, SYS_QOS_CLASS_COLOR_MAX);

    tbl_index = (domain << 8) | (dscp << 2) | ecn;

    field_index = tbl_index % 4;

    field_id_priority = IpeClassificationDscpMap_array_0__priority_f + field_index*2;
    field_id_color    = IpeClassificationDscpMap_array_0_color_f + field_index*2;


    cmd = DRV_IOW(IpeClassificationDscpMap_t, field_id_priority);
    field_val = priority;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, tbl_index / 4, cmd, &field_val));

    cmd = DRV_IOW(IpeClassificationDscpMap_t, field_id_color);
    field_val = color;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, tbl_index / 4, cmd, &field_val));


    SYS_QOS_CLASS_DBG_INFO("dscp -> priority + color map, domain = %d, dscp = %d, priority = %d, color = %d\n",
                           domain, dscp, priority, color);


    return CTC_E_NONE;
}

/**
 @brief get dscp -> priority + color mapping table for the given domain
*/
int32
sys_goldengate_qos_get_igs_dscp_map_table(uint8 lchip, uint8 domain,
                                         uint8 dscp,
                                         uint8 ecn,
                                         uint8* priority,
                                         uint8* color)
{

    uint32 field_id_priority = 0;
    uint32 field_id_color    = 0;
    uint32 tbl_index         = 0;
    uint32 field_index       = 0;
    uint32 cmd               = 0;
    uint32 field_val         = 0;


    SYS_QOS_CLASS_DBG_FUNC();

    CTC_MAX_VALUE_CHECK(domain, SYS_QOS_CLASS_DOMAIN_MAX);
    CTC_MAX_VALUE_CHECK(dscp, SYS_QOS_CLASS_DSCP_MAX);
    CTC_MAX_VALUE_CHECK(ecn, SYS_QOS_CLASS_ECN_MAX);

    tbl_index = (domain << 8) | (dscp << 2) | ecn;

    field_index = tbl_index % 4;

  field_id_priority = IpeClassificationDscpMap_array_0__priority_f + field_index*2;
    field_id_color    = IpeClassificationDscpMap_array_0_color_f + field_index*2;

    cmd = DRV_IOR(IpeClassificationDscpMap_t, field_id_priority);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, tbl_index / 4, cmd, &field_val));
    *priority = field_val;

    cmd = DRV_IOR(IpeClassificationDscpMap_t, field_id_color);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, tbl_index / 4, cmd, &field_val));
    *color = field_val;

    return CTC_E_NONE;

}

/**
 @brief set mpls exp -> priority + color mapping table for the given domain
*/
int32
sys_goldengate_qos_set_igs_exp_map_table(uint8 lchip, uint8 domain,
                                        uint8 exp,
                                        uint8 priority,
                                        uint8 color)
{

    uint32 field_id_priority = 0;
    uint32 field_id_color    = 0;
    uint32 tbl_index         = 0;
    uint32 field_index       = 0;
    uint32 cmd               = 0;
    uint32 field_val         = 0;

    SYS_QOS_CLASS_DBG_FUNC();

    CTC_MAX_VALUE_CHECK(domain, SYS_QOS_CLASS_DOMAIN_MAX);
    CTC_MAX_VALUE_CHECK(exp, SYS_QOS_CLASS_EXP_MAX);
    CTC_MAX_VALUE_CHECK(priority, SYS_QOS_CLASS_PRIORITY_MAX);
    CTC_MAX_VALUE_CHECK(color, SYS_QOS_CLASS_COLOR_MAX);

    tbl_index = 0;

    field_index = (domain << 3) | exp;


    field_id_priority = IpeMplsExpMap_array_0_priority_f + field_index*2;
    field_id_color    = IpeMplsExpMap_array_0_color_f + field_index*2;

    field_val = priority;
    cmd = DRV_IOW(IpeMplsExpMap_t, field_id_priority);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, tbl_index, cmd, &field_val));

    field_val = color;
    cmd = DRV_IOW(IpeMplsExpMap_t, field_id_color);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, tbl_index, cmd, &field_val));


    SYS_QOS_CLASS_DBG_INFO("exp -> priority + color map, domain = %d, exp = %d, priority = %d, color = %d\n",
                           domain, exp, priority, color);


    return CTC_E_NONE;
}

/**
 @brief get mpls exp -> priority + color mapping table for the given domain
*/
int32
sys_goldengate_qos_get_igs_exp_map_table(uint8 lchip, uint8 domain,
                                        uint8 exp,
                                        uint8* priority,
                                        uint8* color)
{

    uint32 field_id_priority = 0;
    uint32 field_id_color    = 0;
    uint32 tbl_index         = 0;
    uint32 field_index       = 0;
    uint32 cmd               = 0;
    uint32 field_val         = 0;


    SYS_QOS_CLASS_DBG_FUNC();

    CTC_MAX_VALUE_CHECK(domain, SYS_QOS_CLASS_DOMAIN_MAX);
    CTC_MAX_VALUE_CHECK(exp, SYS_QOS_CLASS_EXP_MAX);

    tbl_index = 0;

    field_index = (domain << 3) | exp;

    field_id_priority = IpeMplsExpMap_array_0_priority_f + field_index*2;
    field_id_color    = IpeMplsExpMap_array_0_color_f + field_index*2;
    cmd = DRV_IOR(IpeMplsExpMap_t, field_id_priority);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, tbl_index, cmd, &field_val));
    *priority = field_val;

    cmd = DRV_IOR(IpeMplsExpMap_t, field_id_color);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, tbl_index, cmd, &field_val));
    *color = field_val;

    return CTC_E_NONE;
}

/**
 @brief set priority + color -> cos mapping table for the given domain
*/
int32
sys_goldengate_qos_set_egs_cos_map_table(uint8 lchip, uint8 domain,
                                        uint8 priority,
                                        uint8 color,
                                        uint8 cos,
                                        uint8 cfi)
{

    uint32 field_id_cos = 0;
    uint32 field_id_cfi = 0;
    uint32 tbl_index    = 0;
    uint32 cmd_cos      = 0;
    uint32 cmd_cfi      = 0;
    uint32 field_val    = 0;

    SYS_QOS_CLASS_DBG_FUNC();

    CTC_MAX_VALUE_CHECK(domain, SYS_QOS_CLASS_DOMAIN_MAX);
    CTC_MAX_VALUE_CHECK(priority, SYS_QOS_CLASS_PRIORITY_MAX);
    CTC_MAX_VALUE_CHECK(color, SYS_QOS_CLASS_COLOR_MAX);
    CTC_MAX_VALUE_CHECK(cos, SYS_QOS_CLASS_COS_MAX);
    CTC_MAX_VALUE_CHECK(cfi, SYS_QOS_CLASS_CFI_MAX);

    tbl_index = (domain << 8) | (priority << 2) | color;

    field_id_cos = EpeEditPriorityMap_cos_f;
    field_id_cfi = EpeEditPriorityMap_cfi_f;

    cmd_cos = DRV_IOW(EpeEditPriorityMap_t, field_id_cos);
    cmd_cfi = DRV_IOW(EpeEditPriorityMap_t, field_id_cfi);


    field_val = cos;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, tbl_index, cmd_cos, &field_val));

    field_val = cfi;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, tbl_index, cmd_cfi, &field_val));


    SYS_QOS_CLASS_DBG_INFO("priority + color -> cos map, domain = %d, priority = %d, color = %d, cos = %d, cfi = %d\n",
                           domain, priority, color, cos, cfi);


    return CTC_E_NONE;
}

/**
 @brief get priority + color -> cos mapping table for the given domain
*/
int32
sys_goldengate_qos_get_egs_cos_map_table(uint8 lchip, uint8 domain,
                                        uint8 priority,
                                        uint8 color,
                                        uint8* cos,
                                        uint8* cfi)
{

    uint32 field_id_cos = 0;
    uint32 field_id_cfi = 0;
    uint32 tbl_index    = 0;
    uint32 cmd_cos      = 0;
    uint32 cmd_cfi      = 0;
    uint32 field_val    = 0;


    SYS_QOS_CLASS_DBG_FUNC();

    CTC_MAX_VALUE_CHECK(domain, SYS_QOS_CLASS_DOMAIN_MAX);
    CTC_MAX_VALUE_CHECK(priority, SYS_QOS_CLASS_PRIORITY_MAX);
    CTC_MAX_VALUE_CHECK(color, SYS_QOS_CLASS_COLOR_MAX);

    tbl_index = (domain << 8) | (priority << 2) | color;

    field_id_cos = EpeEditPriorityMap_cos_f;
    field_id_cfi = EpeEditPriorityMap_cfi_f;

    cmd_cos = DRV_IOR(EpeEditPriorityMap_t, field_id_cos);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, tbl_index, cmd_cos, &field_val));
    *cos = field_val;

    cmd_cfi = DRV_IOR(EpeEditPriorityMap_t, field_id_cfi);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, tbl_index, cmd_cfi, &field_val));
    *cfi = field_val;


    return CTC_E_NONE;
}

/**
 @brief set priority + color -> dscp mapping table for the given domain
*/
int32
sys_goldengate_qos_set_egs_dscp_map_table(uint8 lchip, uint8 domain,
                                         uint8 priority,
                                         uint8 color,
                                         uint8 dscp)
{

    uint32 field_id  = 0;
    uint32 tbl_index = 0;
    uint32 cmd       = 0;
    uint32 field_val = 0;

    SYS_QOS_CLASS_DBG_FUNC();

    CTC_MAX_VALUE_CHECK(domain, SYS_QOS_CLASS_DOMAIN_MAX);
    CTC_MAX_VALUE_CHECK(priority, SYS_QOS_CLASS_PRIORITY_MAX);
    CTC_MAX_VALUE_CHECK(color, SYS_QOS_CLASS_COLOR_MAX);
    CTC_MAX_VALUE_CHECK(dscp, SYS_QOS_CLASS_DSCP_MAX);

    tbl_index = (domain << 8) | (priority << 2) | color;

    field_id = EpeEditPriorityMap_dscp_f;


    cmd = DRV_IOW(EpeEditPriorityMap_t, field_id);
    field_val = dscp;

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, tbl_index, cmd, &field_val));


    SYS_QOS_CLASS_DBG_INFO("priority + color -> dscp map, domain = %d, priority = %d, color = %d, dscp = %d\n",
                           domain, priority, color, dscp);

    return CTC_E_NONE;
}

/**
 @brief get priority + color -> dscp mapping table for the given domain
*/
int32
sys_goldengate_qos_get_egs_dscp_map_table(uint8 lchip, uint8 domain,
                                         uint8 priority,
                                         uint8 color,
                                         uint8* dscp)
{

    uint32 field_id  = 0;
    uint32 tbl_index = 0;
    uint32 cmd       = 0;
    uint32 field_val = 0;


    SYS_QOS_CLASS_DBG_FUNC();

    CTC_MAX_VALUE_CHECK(domain, SYS_QOS_CLASS_DOMAIN_MAX);
    CTC_MAX_VALUE_CHECK(priority, SYS_QOS_CLASS_PRIORITY_MAX);
    CTC_MAX_VALUE_CHECK(color, SYS_QOS_CLASS_COLOR_MAX);

    tbl_index = (domain << 8) | (priority << 2) | color;
    field_id = EpeEditPriorityMap_dscp_f;

    cmd = DRV_IOR(EpeEditPriorityMap_t, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, tbl_index, cmd, &field_val));
    *dscp = field_val;

    return CTC_E_NONE;
}

/**
 @brief set priority + color -> mpls exp mapping table for the given domain
*/
int32
sys_goldengate_qos_set_egs_exp_map_table(uint8 lchip, uint8 domain,
                                        uint8 priority,
                                        uint8 color,
                                        uint8 exp)
{

    uint32 field_id  = 0;
    uint32 tbl_index = 0;
    uint32 cmd       = 0;
    uint32 field_val = 0;

    SYS_QOS_CLASS_DBG_FUNC();

    CTC_MAX_VALUE_CHECK(domain, SYS_QOS_CLASS_DOMAIN_MAX);
    CTC_MAX_VALUE_CHECK(priority, SYS_QOS_CLASS_PRIORITY_MAX);
    CTC_MAX_VALUE_CHECK(color, SYS_QOS_CLASS_COLOR_MAX);
    CTC_MAX_VALUE_CHECK(exp, SYS_QOS_CLASS_EXP_MAX);

    tbl_index = (domain << 8) | (priority << 2) | color;


    field_id = EpeEditPriorityMap_exp_f;
    cmd = DRV_IOW(EpeEditPriorityMap_t, field_id);
    field_val = exp;


    CTC_ERROR_RETURN(DRV_IOCTL(lchip, tbl_index, cmd, &field_val));

    SYS_QOS_CLASS_DBG_INFO("priority + color -> exp map, domain = %d, priority = %d, color = %d, exp = %d\n",
                           domain, priority, color, exp);


    return CTC_E_NONE;
}

/**
 @brief get priority + color -> mpls exp mapping table for the given domain
*/
int32
sys_goldengate_qos_get_egs_exp_map_table(uint8 lchip, uint8 domain,
                                        uint8 priority,
                                        uint8 color,
                                        uint8* exp)
{

    uint32 field_id  = 0;
    uint32 tbl_index = 0;
    uint32 cmd       = 0;
    uint32 field_val = 0;


    SYS_QOS_CLASS_DBG_FUNC();

    CTC_MAX_VALUE_CHECK(domain, SYS_QOS_CLASS_DOMAIN_MAX);
    CTC_MAX_VALUE_CHECK(priority, SYS_QOS_CLASS_PRIORITY_MAX);
    CTC_MAX_VALUE_CHECK(color, SYS_QOS_CLASS_COLOR_MAX);

    tbl_index = (domain << 8) | (priority << 2) | color;

    field_id = EpeEditPriorityMap_exp_f;

    cmd = DRV_IOR(EpeEditPriorityMap_t, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, tbl_index, cmd, &field_val));
    *exp = field_val;


    return CTC_E_NONE;
}

extern int32
sys_goldengate_qos_domain_map_set(uint8 lchip, ctc_qos_domain_map_t* p_domain_map)
{
    uint8 domain   = 0;
    uint8 cos      = 0;
    uint8 dscp     = 0;
    uint8 ecn      = 0;
    uint8 ip_prec  = 0;
    uint8 exp      = 0;
    uint8 cfi      = 0;
    uint8 priority = 0;
    uint8 color    = 0;

    SYS_QOS_CLASS_DBG_FUNC();

    domain   = p_domain_map->domain_id;
    priority = p_domain_map->priority;
    color    = p_domain_map->color;

    switch (p_domain_map->type)
    {
    case CTC_QOS_DOMAIN_MAP_IGS_COS_TO_PRI_COLOR:
        cos = p_domain_map->hdr_pri.dot1p.cos;
        cfi = p_domain_map->hdr_pri.dot1p.dei;
        CTC_ERROR_RETURN(sys_goldengate_qos_set_igs_cos_map_table(lchip, domain,
                                                                 cos,
                                                                 cfi,
                                                                 priority,
                                                                 color));

        break;

    case CTC_QOS_DOMAIN_MAP_IGS_DSCP_TO_PRI_COLOR:
        dscp = p_domain_map->hdr_pri.tos.dscp;
        ecn = p_domain_map->hdr_pri.tos.ecn;
        CTC_ERROR_RETURN(sys_goldengate_qos_set_igs_dscp_map_table(lchip, domain,
                                                                  dscp,
                                                                  ecn,
                                                                  priority,
                                                                  color));

        break;

    case CTC_QOS_DOMAIN_MAP_IGS_IP_PREC_TO_PRI_COLOR:
        ip_prec = p_domain_map->hdr_pri.ip_prec;
        CTC_ERROR_RETURN(sys_goldengate_qos_set_igs_ip_prec_map_table(lchip, domain,
                                                                     ip_prec,
                                                                     priority,
                                                                     color));

        break;

    case CTC_QOS_DOMAIN_MAP_IGS_EXP_TO_PRI_COLOR:
        exp = p_domain_map->hdr_pri.exp;
        CTC_ERROR_RETURN(sys_goldengate_qos_set_igs_exp_map_table(lchip, domain,
                                                                 exp,
                                                                 priority,
                                                                 color));

        break;

    case CTC_QOS_DOMAIN_MAP_EGS_PRI_COLOR_TO_COS:
        cos = p_domain_map->hdr_pri.dot1p.cos;
        cfi = p_domain_map->hdr_pri.dot1p.dei;
        CTC_ERROR_RETURN(sys_goldengate_qos_set_egs_cos_map_table(lchip, domain,
                                                                 priority,
                                                                 color,
                                                                 cos,
                                                                 cfi));
        break;

    case CTC_QOS_DOMAIN_MAP_EGS_PRI_COLOR_TO_DSCP:
        dscp = p_domain_map->hdr_pri.tos.dscp;
        CTC_ERROR_RETURN(sys_goldengate_qos_set_egs_dscp_map_table(lchip, domain,
                                                                  priority,
                                                                  color,
                                                                  dscp));
        break;

    case CTC_QOS_DOMAIN_MAP_EGS_PRI_COLOR_TO_EXP:
        exp = p_domain_map->hdr_pri.exp;
        CTC_ERROR_RETURN(sys_goldengate_qos_set_egs_exp_map_table(lchip, domain,
                                                                 priority,
                                                                 color,
                                                                 exp));

        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_qos_domain_map_get(uint8 lchip, ctc_qos_domain_map_t* p_domain_map)
{
    uint8 domain   = 0;
    uint8 cos      = 0;
    uint8 dscp     = 0;
    uint8 ecn      = 0;
    uint8 ip_prec  = 0;
    uint8 exp      = 0;
    uint8 cfi      = 0;
    uint8 priority = 0;
    uint8 color    = 0;

    SYS_QOS_CLASS_DBG_FUNC();

    domain = p_domain_map->domain_id;

    switch (p_domain_map->type)
    {
    case CTC_QOS_DOMAIN_MAP_IGS_COS_TO_PRI_COLOR:
        cos = p_domain_map->hdr_pri.dot1p.cos;
        cfi = p_domain_map->hdr_pri.dot1p.dei;
        CTC_ERROR_RETURN(sys_goldengate_qos_get_igs_cos_map_table(lchip, domain,
                                                                 cos,
                                                                 cfi,
                                                                 &priority,
                                                                 &color));
        p_domain_map->priority = priority;
        p_domain_map->color    = color;
        break;

    case CTC_QOS_DOMAIN_MAP_IGS_DSCP_TO_PRI_COLOR:
        dscp = p_domain_map->hdr_pri.tos.dscp;
        ecn = p_domain_map->hdr_pri.tos.ecn;
        CTC_ERROR_RETURN(sys_goldengate_qos_get_igs_dscp_map_table(lchip, domain,
                                                                  dscp,
                                                                  ecn,
                                                                  &priority,
                                                                  &color));
        p_domain_map->priority = priority;
        p_domain_map->color    = color;
        break;

    case CTC_QOS_DOMAIN_MAP_IGS_IP_PREC_TO_PRI_COLOR:
        ip_prec = p_domain_map->hdr_pri.ip_prec;
        CTC_ERROR_RETURN(sys_goldengate_qos_get_igs_ip_prec_map_table(lchip, domain,
                                                                     ip_prec,
                                                                     &priority,
                                                                     &color));
        p_domain_map->priority = priority;
        p_domain_map->color    = color;

        break;

    case CTC_QOS_DOMAIN_MAP_IGS_EXP_TO_PRI_COLOR:
        exp = p_domain_map->hdr_pri.exp;
        CTC_ERROR_RETURN(sys_goldengate_qos_get_igs_exp_map_table(lchip, domain,
                                                                 exp,
                                                                 &priority,
                                                                 &color));
        p_domain_map->priority = priority;
        p_domain_map->color    = color;

        break;

    case CTC_QOS_DOMAIN_MAP_EGS_PRI_COLOR_TO_COS:

        priority = p_domain_map->priority;
        color    = p_domain_map->color;
        CTC_ERROR_RETURN(sys_goldengate_qos_get_egs_cos_map_table(lchip, domain,
                                                                 priority,
                                                                 color,
                                                                 &cos,
                                                                 &cfi));

        p_domain_map->hdr_pri.dot1p.cos = cos;
        p_domain_map->hdr_pri.dot1p.dei = cfi;

        break;

    case CTC_QOS_DOMAIN_MAP_EGS_PRI_COLOR_TO_DSCP:
        priority = p_domain_map->priority;
        color    = p_domain_map->color;
        CTC_ERROR_RETURN(sys_goldengate_qos_get_egs_dscp_map_table(lchip, domain,
                                                                  priority,
                                                                  color,
                                                                  &dscp));
        p_domain_map->hdr_pri.tos.dscp = dscp;

        break;

    case CTC_QOS_DOMAIN_MAP_EGS_PRI_COLOR_TO_EXP:
        priority = p_domain_map->priority;
        color    = p_domain_map->color;
        CTC_ERROR_RETURN(sys_goldengate_qos_get_egs_exp_map_table(lchip, domain,
                                                                 priority,
                                                                 color,
                                                                 &exp));
        p_domain_map->hdr_pri.exp = exp;

        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_qos_domain_map_default(uint8 lchip)
{
    uint8 color    = 0;
    uint8 priority = 0;
    uint8 value    = 0;
    uint8 domain   = 0;
    uint8 i        = 0;
    uint32 cmd     = 0;
    DsPriorityMap_m ds_priority_map;

    SYS_QOS_CLASS_DBG_FUNC();
    LCHIP_CHECK(lchip);
    
    sal_memset(&ds_priority_map, 0, sizeof(DsPriorityMap_m));
    for (domain = 0; domain < 8; domain++)
    {
        color = CTC_QOS_COLOR_GREEN;

        /* init ingress cos + cfi -> priority + color mapping table */
        for (value = 0; value < 8; value++)
        {
            priority = value * ((p_gg_queue_master[lchip]->priority_mode)? 2:8);
            CTC_ERROR_RETURN(sys_goldengate_qos_set_igs_cos_map_table(lchip, domain,
                                                                     value,
                                                                     0,
                                                                     priority,
                                                                     color));

            priority = value * ((p_gg_queue_master[lchip]->priority_mode)? 2:8);
            CTC_ERROR_RETURN(sys_goldengate_qos_set_igs_cos_map_table(lchip, domain,
                                                                     value,
                                                                     1,
                                                                     priority,
                                                                     color));
        }

        /* init ingress ip prec -> priority + color mapping table */
        for (value = 0; value < 8; value++)
        {
            priority = value * ((p_gg_queue_master[lchip]->priority_mode)? 2:8);
            CTC_ERROR_RETURN(sys_goldengate_qos_set_igs_ip_prec_map_table(lchip, domain,
                                                                         value,
                                                                         priority,
                                                                         color));
        }

        /* init ingress dscp -> priority + color mapping table */
        for (value = 0; value < 64; value++)
        {
            priority = value / ((p_gg_queue_master[lchip]->priority_mode)? 4:1);

            for (i = 0; i < 4; i++)
            {
                CTC_ERROR_RETURN(sys_goldengate_qos_set_igs_dscp_map_table(lchip, domain,
                                                                          value,
                                                                          i,
                                                                          priority,
                                                                          color));
            }
        }

        /* init ingress mpls exp -> priority + color mapping table */
        for (value = 0; value < 8; value++)
        {
            priority = value * ((p_gg_queue_master[lchip]->priority_mode)? 2:8);
            CTC_ERROR_RETURN(sys_goldengate_qos_set_igs_exp_map_table(lchip, domain,
                                                                     value,
                                                                     priority,
                                                                     color));
        }
    }

    for (domain = 0; domain < 8; domain++)
    {

        for (priority = 0; priority <= SYS_QOS_CLASS_PRIORITY_MAX; priority++)
        {
            for (color = CTC_QOS_COLOR_NONE; color < MAX_CTC_QOS_COLOR; color++)
            {
                /* init egress priority + color -> cos mapping table */
                value = priority / ((p_gg_queue_master[lchip]->priority_mode)? 2:8);
                CTC_ERROR_RETURN(sys_goldengate_qos_set_egs_cos_map_table(lchip, domain,
                                                                         priority,
                                                                         color,
                                                                         value,
                                                                         0));

                /* init egress priority + color -> dscp mapping table */
                value = priority * ((p_gg_queue_master[lchip]->priority_mode)? 4:1);
                CTC_ERROR_RETURN(sys_goldengate_qos_set_egs_dscp_map_table(lchip, domain,
                                                                          priority,
                                                                          color,
                                                                          value));

                /* init egress priority + color -> exp mapping table */
                value = priority / ((p_gg_queue_master[lchip]->priority_mode)? 2:8);
                CTC_ERROR_RETURN(sys_goldengate_qos_set_egs_exp_map_table(lchip, domain,
                                                                         priority,
                                                                         color,
                                                                         value));
            }
        }
    }

    cmd = DRV_IOW(DsPriorityMap_t, DRV_ENTRY_FLAG);
    for (value = 0; value < 8; value++)
    {
        SetDsPriorityMap(V, color_f, &ds_priority_map, 3);
        SetDsPriorityMap(V, cos_f, &ds_priority_map, value);
        SetDsPriorityMap(V, _priority_f, &ds_priority_map, value*((p_gg_queue_master[lchip]->priority_mode)? 2:8));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, value, cmd, &ds_priority_map));
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_qos_domain_map_dump(uint8 lchip, uint8 domain, uint8 type)
{
    uint8 color    = 0;
    uint8 priority = 0;
    uint8 value    = 0;
    uint8 cfi      = 0;
    uint8 i        = 0;

    LCHIP_CHECK(lchip);
    CTC_MAX_VALUE_CHECK(domain, SYS_QOS_CLASS_DOMAIN_MAX);

    switch (type)
    {
    case CTC_QOS_DOMAIN_MAP_IGS_COS_TO_PRI_COLOR:
        {
            /* ingress cos + cfi -> priority + color mapping table */
            SYS_QOS_CLASS_DBG_DUMP("ingress cos + dei -> priority + color mapping table\n");
            SYS_QOS_CLASS_DBG_DUMP("-----------------------------------------------------\n");

            for (value = 0; value < 8; value++)
            {
                CTC_ERROR_RETURN(sys_goldengate_qos_get_igs_cos_map_table(lchip, domain,
                                                                         value,
                                                                         0,
                                                                         &priority,
                                                                         &color));
                SYS_QOS_CLASS_DBG_DUMP("   cos(%d)  +  dei(%d) -> priority(%d)  + color(%d)\n",
                                       value, 0, priority, color);

                CTC_ERROR_RETURN(sys_goldengate_qos_get_igs_cos_map_table(lchip, domain,
                                                                         value,
                                                                         1,
                                                                         &priority,
                                                                         &color));
                SYS_QOS_CLASS_DBG_DUMP("   cos(%d)  +  dei(%d) -> priority(%d)  + color(%d)\n",
                                       value, 1, priority, color);

            }
        }
        break;

    case CTC_QOS_DOMAIN_MAP_IGS_IP_PREC_TO_PRI_COLOR:
        {
            /*ingress ip prec -> priority + color mapping table */
            SYS_QOS_CLASS_DBG_DUMP("ingress ip_prec -> priority + color mapping table\n");
            SYS_QOS_CLASS_DBG_DUMP("-----------------------------------------------------\n");

            for (value = 0; value < 8; value++)
            {
                CTC_ERROR_RETURN(sys_goldengate_qos_get_igs_ip_prec_map_table(lchip, domain,
                                                                             value,
                                                                             &priority,
                                                                             &color));

                SYS_QOS_CLASS_DBG_DUMP("    ip_prec(%d) -> priority(%d)  + color(%d)\n",
                                       value, priority, color);
            }
        }
        break;

    case CTC_QOS_DOMAIN_MAP_IGS_DSCP_TO_PRI_COLOR:
        {
            /* ingress dscp -> priority + color mapping table */
            SYS_QOS_CLASS_DBG_DUMP("ingress dscp + ecn -> priority + color mapping table\n");
            SYS_QOS_CLASS_DBG_DUMP("-----------------------------------------------------\n");

            for (value = 0; value < 64; value++)
            {
                for (i = 0; i < 4; i++)
                {
                    CTC_ERROR_RETURN(sys_goldengate_qos_get_igs_dscp_map_table(lchip, domain,
                                                                              value,
                                                                              i,
                                                                              &priority,
                                                                              &color));
                    SYS_QOS_CLASS_DBG_DUMP("    dscp(%d) + ecn(%d) -> priority(%d)  + color(%d)\n",
                                           value, i, priority, color);

                }
            }
        }

        break;

    case CTC_QOS_DOMAIN_MAP_IGS_EXP_TO_PRI_COLOR:
        {
            /* ingress mpls exp -> priority + color mapping table */
            SYS_QOS_CLASS_DBG_DUMP("ingress exp -> priority + color mapping table\n");
            SYS_QOS_CLASS_DBG_DUMP("-----------------------------------------------------\n");

            for (value = 0; value < 8; value++)
            {
                CTC_ERROR_RETURN(sys_goldengate_qos_get_igs_exp_map_table(lchip, domain,
                                                                         value,
                                                                         &priority,
                                                                         &color));

                SYS_QOS_CLASS_DBG_DUMP("    exp(%d) -> priority(%d)  + color(%d)\n",
                                       value, priority, color);
            }
        }

        break;

    case CTC_QOS_DOMAIN_MAP_EGS_PRI_COLOR_TO_COS:
        {
            /* egress priority + color -> cos mapping table */
            SYS_QOS_CLASS_DBG_DUMP("egress  priority + color -> cos  mapping table\n");
            SYS_QOS_CLASS_DBG_DUMP("-----------------------------------------------------\n");

            for (priority = 0; priority <= SYS_QOS_CLASS_PRIORITY_MAX; priority++)
            {
                for (color = CTC_QOS_COLOR_NONE; color < MAX_CTC_QOS_COLOR; color++)
                {

                    CTC_ERROR_RETURN(sys_goldengate_qos_get_egs_cos_map_table(lchip, domain,
                                                                             priority,
                                                                             color,
                                                                             &value,
                                                                             &cfi));

                    SYS_QOS_CLASS_DBG_DUMP(" priority(%d)  + color(%d)  -> cos(%d) + dei(%d)\n",
                                           priority, color, value, cfi);

                }
            }
        }

        break;

    case CTC_QOS_DOMAIN_MAP_EGS_PRI_COLOR_TO_DSCP:
        {
            /* init egress priority + color -> dscp mapping table */
            SYS_QOS_CLASS_DBG_DUMP("egress  priority + color -> dscp  mapping table\n");
            SYS_QOS_CLASS_DBG_DUMP("-----------------------------------------------------\n");

            for (priority = 0; priority <= SYS_QOS_CLASS_PRIORITY_MAX; priority++)
            {
                for (color = CTC_QOS_COLOR_NONE; color < MAX_CTC_QOS_COLOR; color++)
                {

                    CTC_ERROR_RETURN(sys_goldengate_qos_get_egs_dscp_map_table(lchip, domain,
                                                                              priority,
                                                                              color,
                                                                              &value));
                    SYS_QOS_CLASS_DBG_DUMP(" priority(%d)  + color(%d)  -> dscp(%d)\n",
                                           priority, color, value);

                }
            }
        }

        break;

    case CTC_QOS_DOMAIN_MAP_EGS_PRI_COLOR_TO_EXP:
        {
            /* init egress priority + color -> exp mapping table */
            SYS_QOS_CLASS_DBG_DUMP("egress  priority + color -> exp  mapping table\n");
            SYS_QOS_CLASS_DBG_DUMP("-----------------------------------------------------\n");

            for (priority = 0; priority <= SYS_QOS_CLASS_PRIORITY_MAX; priority++)
            {
                for (color = CTC_QOS_COLOR_NONE; color < MAX_CTC_QOS_COLOR; color++)
                {

                    CTC_ERROR_RETURN(sys_goldengate_qos_get_egs_exp_map_table(lchip, domain,
                                                                             priority,
                                                                             color,
                                                                             &value));
                    SYS_QOS_CLASS_DBG_DUMP(" priority(%d)  + color(%d)  -> exp(%d)\n",
                                           priority, color, value);

                }
            }
        }
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

/**
 @brief init qos classification mapping tables
*/
int32
sys_goldengate_qos_class_init(uint8 lchip)
{
    CTC_ERROR_RETURN(sys_goldengate_qos_domain_map_default(lchip));
    return CTC_E_NONE;
}


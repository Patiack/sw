/**
 @file ctc_goldengate_learning_aging.c

 @date 2010-3-16

 @version v2.0

---file comments----
*/

/****************************************************************************
*
* Header Files
*
****************************************************************************/
#include "ctc_const.h"
#include "ctc_error.h"

#include "ctc_learning_aging.h"
#include "sys_goldengate_learning_aging.h"
#include "sys_goldengate_chip.h"

/***************************************
 Learning Module's HUMBER API Interfaces
*****************************************/

int32
ctc_goldengate_set_learning_action(uint8 lchip, ctc_learning_action_info_t* p_learning_action)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_L2);
    LCHIP_CHECK(lchip);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_goldengate_learning_set_action(lchip, p_learning_action));
    }

    return CTC_E_NONE;
}

int32
ctc_goldengate_get_learning_action(uint8 lchip, ctc_learning_action_info_t* p_learning_action)
{

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_L2);
    LCHIP_CHECK(lchip);
    CTC_ERROR_RETURN(sys_goldengate_learning_get_action(lchip, p_learning_action));

    return CTC_E_NONE;
}

/***************************************
 Aging Module's HUMBER API Interfaces
*****************************************/

int32
ctc_goldengate_aging_set_property(uint8 lchip, ctc_aging_table_type_t  tbl_type, ctc_aging_prop_t aging_prop, uint32 value)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_L2);
    LCHIP_CHECK(lchip);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_goldengate_aging_set_property(lchip, tbl_type, aging_prop, value));
    }

    return CTC_E_NONE;
}

int32
ctc_goldengate_aging_get_property(uint8 lchip, ctc_aging_table_type_t  tbl_type, ctc_aging_prop_t aging_prop, uint32* value)
{

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_L2);
    LCHIP_CHECK(lchip);
    CTC_ERROR_RETURN(sys_goldengate_aging_get_property(lchip, tbl_type, aging_prop, value));

    return CTC_E_NONE;
}

int32
ctc_goldengate_learning_aging_init(uint8 lchip, void* global_cfg)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    ctc_learn_aging_global_cfg_t cfg;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_L2);
    LCHIP_CHECK(lchip);
    if (global_cfg == NULL)
    {
        sal_memset(&cfg, 0, sizeof(cfg));
        cfg.hw_mac_aging_en = 1;
        cfg.hw_mac_learn_en = 1;
        cfg.scl_aging_en    = 0;
        cfg.hw_scl_aging_en = 0;
        global_cfg = &cfg;
    }

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_goldengate_learning_aging_init(lchip, (ctc_learn_aging_global_cfg_t*)global_cfg));
    }

    return CTC_E_NONE;
}

int32
ctc_goldengate_learning_aging_deinit(uint8 lchip)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_L2);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_goldengate_learning_aging_deinit(lchip));
    }

    return CTC_E_NONE;
}


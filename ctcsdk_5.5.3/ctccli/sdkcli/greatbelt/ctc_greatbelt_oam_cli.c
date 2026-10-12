/**
 @file ctc_greatbelt_oam_cli.c

 @date 2012-11-20

 @version v2.0

 This file defines functions for greatbelt oam cli module

*/

/****************************************************************
 *
 * Header Files
 *
 ***************************************************************/
#include "sal.h"
#include "ctc_cli.h"
#include "ctc_api.h"
#include "ctcs_api.h"
#include "ctc_greatbelt_oam_cli.h"
#include "ctc_oam.h"
#include "ctc_error.h"
#include "ctc_debug.h"
#include "ctc_cli_common.h"
#include "sys_greatbelt_oam_debug.h"
#include "sys_greatbelt_oam_db.h"

#define CTC_CLI_OAM_CFM_DUMP_DESC "Dump registers or tables"

extern int32
sys_greatbelt_oam_trpt_show_state(uint8 lchip, uint8 gchip);
extern int32
sys_greatbelt_parser_packet_type_ip(uint8 lchip, uint8 en);

extern int32
sys_greatbelt_cfm_add_sevice_queue_lmep_for_lm_dm(uint8 lchip, ctc_oam_lmep_t* p_lmep, ctc_oam_lmep_t* p_lmep_service);
extern int32
sys_greatbelt_cfm_update_sevice_queue_lmep_for_lm_dm(uint8 lchip, ctc_oam_lmep_t* p_lmep);
extern int32
sys_greatbelt_cfm_remove_service_queue_lmep_for_lm_dm(uint8 lchip, ctc_oam_lmep_t* p_lmep);


extern ctc_oam_lmep_t* p_oam_lmep;

extern sys_oam_master_t* g_oam_master[CTC_MAX_LOCAL_CHIP_NUM];
/*********************************************************************
* Internal debug clis
*********************************************************************/
/**
 @brief  debug functions
*/
#define M_OAM_DUMP ""
#define CTC_OAM_TRPT_MAX_PKTHDR_LEN 64

extern int32
sys_greatbelt_clear_event_cache(uint8 lchip);
CTC_CLI(ctc_cli_gb_oam_cfm_dump_maid,
        ctc_cli_gb_oam_cfm_dump_maid_cmd,
        "oam cfm dump maid local-chipid CHIP_ID maid-index MAID_INDEX",
        CTC_CLI_OAM_M_STR,
        CTC_CLI_OAM_CFM_M_STR,
        CTC_CLI_OAM_CFM_DUMP_DESC,
        "MAID table",
        CTC_CLI_CHIP_DESC,
        CTC_CLI_CHIP_ID_DESC,
        "MAID table index",
        "<0- max 1023 for 16bytes, 340 for 48byts>")
{
    int32  ret   = CLI_SUCCESS;
    uint32 index = 0;
    uint8  lchip = 0;

    CTC_CLI_GET_UINT8_RANGE("local chip id", lchip, argv[0], 0, CTC_MAX_UINT8_VALUE);
    CTC_CLI_GET_UINT32_RANGE("maid table index", index, argv[1], 0, CTC_MAX_UINT32_VALUE);

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_greatbelt_oam_dump_maid(lchip, index);
    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;
}

CTC_CLI(ctc_cli_gb_oam_cfm_dump_ma,
        ctc_cli_gb_oam_cfm_dump_ma_cmd,
        "oam cfm dump ma local-chipid CHIP_ID ma-index MA_INDEX",
        CTC_CLI_OAM_M_STR,
        CTC_CLI_OAM_CFM_M_STR,
        CTC_CLI_OAM_CFM_DUMP_DESC,
        "MA table",
        CTC_CLI_CHIP_DESC,
        CTC_CLI_CHIP_ID_DESC,
        "MA table index",
        "<0-2047>")
{
    int32  ret   = CLI_SUCCESS;
    uint32 index = 0;
    uint8  lchip = 0;

    CTC_CLI_GET_UINT8_RANGE("local chip id", lchip, argv[0], 0, CTC_MAX_UINT8_VALUE);
    CTC_CLI_GET_UINT32_RANGE("ma table index", index, argv[1], 0, CTC_MAX_UINT32_VALUE);

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_greatbelt_oam_dump_ma(lchip, index);
    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;
}

CTC_CLI(ctc_cli_gb_oam_cfm_dump_lmep,
        ctc_cli_gb_oam_cfm_dump_lmep_cmd,
        "oam cfm dump lmep local-chipid CHIP_ID lmep-index MEP_INDEX",
        CTC_CLI_OAM_M_STR,
        CTC_CLI_OAM_CFM_M_STR,
        CTC_CLI_OAM_CFM_DUMP_DESC,
        "Local MEP table",
        CTC_CLI_CHIP_DESC,
        CTC_CLI_CHIP_ID_DESC,
        "Local MEP table index",
        "<0-2047>")
{
    int32   ret  = CLI_SUCCESS;
    uint32 index = 0;
    uint8  lchip = 0;

    CTC_CLI_GET_UINT8_RANGE("local chip id", lchip, argv[0], 0, CTC_MAX_UINT8_VALUE);
    CTC_CLI_GET_UINT32_RANGE("Mep table index", index, argv[1], 0, CTC_MAX_UINT32_VALUE);

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_greatbelt_oam_dump_lmep(lchip, index);
    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;
}

CTC_CLI(ctc_cli_gb_oam_cfm_dump_rmep,
        ctc_cli_gb_oam_cfm_dump_rmep_cmd,
        "oam cfm dump rmep local-chipid CHIP_ID rmep-index MEP_INDEX",
        CTC_CLI_OAM_M_STR,
        CTC_CLI_OAM_CFM_M_STR,
        CTC_CLI_OAM_CFM_DUMP_DESC,
        "Remote MEP table",
        CTC_CLI_CHIP_DESC,
        CTC_CLI_CHIP_ID_DESC,
        "Remote MEP table index",
        "<0-2047>")
{
    int32   ret  = CLI_SUCCESS;
    uint32 index = 0;
    uint8  lchip = 0;

    CTC_CLI_GET_UINT8_RANGE("local chip id", lchip, argv[0], 0, CTC_MAX_UINT8_VALUE);
    CTC_CLI_GET_UINT32_RANGE("RMep table index", index, argv[1], 0, CTC_MAX_UINT32_VALUE);

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_greatbelt_oam_dump_rmep(lchip, index);
    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;
}

CTC_CLI(ctc_cli_gb_oam_cfm_dump_mep_index,
        ctc_cli_gb_oam_cfm_dump_mep_index_cmd,
        "oam cfm dump mep index local-chipid CHIP_ID",
        CTC_CLI_OAM_M_STR,
        CTC_CLI_OAM_CFM_M_STR,
        CTC_CLI_OAM_CFM_DUMP_DESC,
        "MEP table",
        "MEP table index",
        CTC_CLI_CHIP_DESC,
        CTC_CLI_CHIP_ID_DESC)
{
    int32  ret      = CLI_SUCCESS;
    uint8  lchip    = 0;
    ctc_oam_key_t oam_key;

    sal_memset(&oam_key, 0, sizeof(ctc_oam_key_t));

    CTC_CLI_GET_UINT8_RANGE("local chip id", lchip, argv[0], 0, CTC_MAX_UINT8_VALUE);

    sal_memcpy(&oam_key, &p_oam_lmep->key, sizeof(ctc_oam_key_t));

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_greatbelt_oam_dump_mep(lchip, &oam_key);
    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;
}

CTC_CLI(ctc_cli_gb_oam_cfm_show_port_oam_info,
        ctc_cli_gb_oam_cfm_show_port_oam_info_cmd,
        "show oam port-info port GPORT_ID (lchip LCHIP|)",
        "Show OAM Information",
        "OAM Information",
        "OAM information in Port",
        CTC_CLI_GPORT_DESC,
        CTC_CLI_GPHYPORT_ID_DESC,
        CTC_CLI_CHIP_DESC,
        CTC_CLI_CHIP_ID_DESC)
{
    int32  ret      = CLI_SUCCESS;
    uint16 gport    = 0;
    uint8 lchip = 0;
    uint8 index = 0;
    ctc_oam_property_t ctc_oam_property;

    sal_memset(&ctc_oam_property, 0, sizeof(ctc_oam_property_t));

    CTC_CLI_GET_UINT16_RANGE("gport", gport, argv[0], 0, CTC_MAX_UINT16_VALUE);

    ctc_oam_property.oam_pro_type       = CTC_OAM_PROPERTY_TYPE_Y1731;
    ctc_oam_property.u.y1731.gport    = gport;
    ctc_oam_property.u.y1731.cfg_type = CTC_OAM_Y1731_CFG_TYPE_PORT_OAM_EN;
    ctc_oam_property.u.y1731.dir      = CTC_BOTH_DIRECTION;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8_RANGE("lchip", lchip, argv[index + 1], 0, CTC_MAX_UINT8_VALUE);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_greatbelt_oam_show_oam_property(lchip, &ctc_oam_property);
    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    ctc_oam_property.u.y1731.cfg_type = CTC_OAM_Y1731_CFG_TYPE_PORT_TUNNEL_EN;

    ret = sys_greatbelt_oam_show_oam_property(lchip, &ctc_oam_property);
    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    ctc_oam_property.u.y1731.cfg_type = CTC_OAM_Y1731_CFG_TYPE_PORT_LM_EN;

    ret = sys_greatbelt_oam_show_oam_property(lchip, &ctc_oam_property);
    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;
}

#define M_OAM_INTERNAL ""
CTC_CLI(ctc_cli_gb_oam_set_mep_index_by_sw,
        ctc_cli_gb_oam_set_mep_index_by_sw_cmd,
        "oam mep-index-alloc system (lchip LCHIP|)",
        CTC_CLI_OAM_M_STR,
        "OAM MEP index alloc mode",
        "OAM MEP index alloc mode by system",
         CTC_CLI_CHIP_DESC,
        CTC_CLI_CHIP_ID_DESC)
{
    int32  ret = CLI_SUCCESS;
    uint8 lchip = 0;
    uint8 index = 0;
    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8_RANGE("lchip", lchip, argv[index + 1], 0, CTC_MAX_UINT8_VALUE);
    }
    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_greatbelt_oam_internal_property(lchip);
    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;
}

CTC_CLI(ctc_cli_gb_oam_set_mep_maid_len,
        ctc_cli_gb_oam_set_mep_maid_len_cmd,
        "oam maid-len ( byte-16 | byte-32 | byte-48 ) (lchip LCHIP|)",
        CTC_CLI_OAM_M_STR,
        "OAM MAID length",
        "Maid len 16bytes",
        "Maid len 32bytes",
        "Maid len 48bytes",
        CTC_CLI_CHIP_DESC,
        CTC_CLI_CHIP_ID_DESC)
{
    int32  ret = CLI_SUCCESS;
    uint8 lchip = 0;
    uint8 index = 0;
    ctc_oam_maid_len_format_t maid_len = 0;

    if (0 == sal_memcmp(argv[0], "byte-16", 7))
    {
        maid_len = CTC_OAM_MAID_LEN_16BYTES;
    }
    else if (0 == sal_memcmp(argv[0], "byte-32", 7))
    {
        maid_len = CTC_OAM_MAID_LEN_32BYTES;
    }
    else if (0 == sal_memcmp(argv[0], "byte-48", 7))
    {
        maid_len = CTC_OAM_MAID_LEN_48BYTES;
    }
    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8_RANGE("lchip", lchip, argv[index + 1], 0, CTC_MAX_UINT8_VALUE);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_greatbelt_oam_internal_maid_property(lchip, maid_len);
    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;
}


CTC_CLI(ctc_cli_gb_oam_clear_error_cache,
        ctc_cli_gb_oam_clear_error_cache_cmd,
        "oam event-cache clear (lchip LCHIP|)",
        CTC_CLI_OAM_M_STR,
        "OAM event",
        "Clear OAM event",
        CTC_CLI_CHIP_DESC,
        CTC_CLI_CHIP_ID_DESC)
{
    uint8 lchip = 0;
    uint8 index = 0;
    int32 ret = CLI_SUCCESS;
    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8_RANGE("lchip", lchip, argv[index + 1], 0, CTC_MAX_UINT8_VALUE);
    }
    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_greatbelt_clear_event_cache(lchip);
    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }
    return CLI_SUCCESS;
}

extern int32
_sys_greatbelt_oam_tp_section_init(uint8 lchip, uint8 use_port);

CTC_CLI(ctc_cli_gb_oam_tp_section_oam,
        ctc_cli_gb_oam_tp_section_oam_cmd,
        "oam tp section (port | interface) (lchip LCHIP|)",
        CTC_CLI_OAM_M_STR,
        "MPLS TP",
        "Section OAM",
        "Section OAM use port",
        "Section OAM use interface",
        CTC_CLI_CHIP_DESC,
        CTC_CLI_CHIP_ID_DESC)
{
    uint8 is_port   = 0;
    uint8 lchip     = 0;
    uint8 index = 0;
    int32  ret      = CLI_SUCCESS;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8_RANGE("lchip", lchip, argv[index + 1], 0, CTC_MAX_UINT8_VALUE);
    }

    if(0 == sal_memcmp(argv[0], "port", 4))
    {
        is_port = 1;
    }


    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = _sys_greatbelt_oam_tp_section_init(lchip, is_port);
    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}


#define M_OAM_TIMER ""

#define MODEL_OAM                   0   /* same to CHIP_AGT_MODEL_TYPE_OAM */
#define MODEL_OAM_CMD_CACHE_TIMER   0
#define MODEL_OAM_CMD_UPDATE_TIMER  1
#define MODEL_OAM_CMD_UPDATE_MEP    2

extern uint32
chip_agent_get_mode(void);

int32
ctc_oam_model_cmd_tx(uint32 cmd, uint32 para1, uint32 para2, uint32 para3, uint32 para4);
int32
ctc_oam_model_cmd_rx(uint32 type, uint32* param, uint32 param_count);

extern int32
cm_oam_scan_defect_timer(uint32 chip_id, uint32 scan_times);

extern int32
sys_greatbelt_oam_report_error_cache(uint8 lchip);

CTC_CLI(ctc_cli_gb_oam_update_error_cache_timer_enable,
        ctc_cli_gb_oam_update_error_cache_timer_enable_cmd,
        "oam cfm error-cache-timer (enable| disable)",
        CTC_CLI_OAM_M_STR,
        CTC_CLI_OAM_CFM_M_STR,
        "OAM error cache timer",
        "Enable timer",
        "Disable timer")
{
    int32 ret = CLI_SUCCESS;
    bool enable = FALSE;

    enable = enable;
    if (0 == sal_memcmp(argv[0], "e", 1))
    {
        enable = TRUE;
    }
    else
    {
        enable = FALSE;
    }

    if (1 != chip_agent_get_mode()) /* CHIP_AGT_MODE_CLIENT */
    {
         /*ret = ctc_oam_cfm_run_error_cache_timer(enable);*/
    }
    else
    {
         /*ret = ctc_oam_model_cmd_tx(MODEL_OAM_CMD_CACHE_TIMER, enable, 0, 0, 0);*/
    }

    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

#define SYS_OAM_CCM_TIMER   1000  /*1s*/

/* mep tx related clis */
extern int32 cm_oam_update_ccm_timer(uint32 chip_id, uint32 min_ptr, uint32 max_ptr, uint32 update_times);
extern int32 cm_oam_cfm_run_error_cache_timer(bool enable);
extern uint32 cm_oam_cfm_run_ccm(bool enable);

CTC_CLI(ctc_cli_gb_oam_update_update_timer_enable,
        ctc_cli_gb_oam_update_update_timer_enable_cmd,
        "oam cfm update-timer (enable| disable)",
        CTC_CLI_OAM_M_STR,
        CTC_CLI_OAM_CFM_M_STR,
        "OAM update timer",
        "Enable update timer",
        "Disable update timer")
{
    int32 ret = CLI_SUCCESS;
    bool enable = FALSE;

    enable = enable;
    if (0 == sal_memcmp(argv[0], "e", 1))
    {
        enable = TRUE;
    }
    else
    {
        enable = FALSE;
    }
    if (1 != chip_agent_get_mode()) /* CHIP_AGT_MODE_CLIENT */
    {
         /*ret = ctc_oam_cfm_run_ccm(enable);*/
    }
    else
    {
         /*ret = ctc_oam_model_cmd_tx(MODEL_OAM_CMD_UPDATE_TIMER, enable, 0, 0, 0);*/
    }

    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gb_oam_update_fsm,
        cli_oam_cfm_update_fs_cmd,
        "oam cfm update-mep local-chipid CHIP_ID mep-index MIN_MEP_INDEX MAX_MEP_INDEX",
        CTC_CLI_OAM_M_STR,
        CTC_CLI_OAM_CFM_M_STR,
        "Update MEP",
        CTC_CLI_CHIP_DESC,
        CTC_CLI_CHIP_ID_DESC,
        "MEP index",
        "Min MEP index <2-2047>",
        "Max MEP index <2-2047>")
{

    int32 ret = CLI_SUCCESS;
    uint32 chip_id = 0;
    uint32 min_mep_index = 0;
    uint32 max_mep_index = 0;
    chip_id = chip_id;
    min_mep_index = min_mep_index;
    max_mep_index = max_mep_index;

    CTC_CLI_GET_UINT32_RANGE("Local chip id", chip_id, argv[0], 0, CTC_MAX_UINT32_VALUE);
    CTC_CLI_GET_UINT32_RANGE("Min MEP index", min_mep_index, argv[1], 0, CTC_MAX_UINT32_VALUE);
    CTC_CLI_GET_UINT32_RANGE("Max MEP index", max_mep_index, argv[2], 0, CTC_MAX_UINT32_VALUE);

    if (1 != chip_agent_get_mode()) /* CHIP_AGT_MODE_CLIENT */
    {
#if (SDK_WORK_PLATFORM == 1 && SDK_WORK_ENV == 1)  /*simulation */
        ret = cm_oam_update_ccm_timer(chip_id, min_mep_index, max_mep_index, 14);
#endif
    }
    else
    {
         /*ret = ctc_oam_model_cmd_tx(MODEL_OAM_CMD_UPDATE_MEP, chip_id, min_mep_index, max_mep_index, 14);*/
    }

    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

int32
ctc_oam_model_cmd_tx(uint32 cmd, uint32 para1, uint32 para2, uint32 para3, uint32 para4)
{
    uint32 param_count = 0;
    uint32 param[5];

    sal_memset(param, 0, sizeof(param));

    param_count = param_count;
    switch (cmd)
    {
    case MODEL_OAM_CMD_CACHE_TIMER:
        param_count = 2;
        param[0] = cmd;
        param[1] = para1;
        break;

    case MODEL_OAM_CMD_UPDATE_TIMER:
        param_count = 2;
        param[0] = cmd;
        param[1] = para1;
        break;

    case MODEL_OAM_CMD_UPDATE_MEP:
        param_count = 5;
        param[0] = cmd;
        param[1] = para1;
        param[2] = para2;
        param[3] = para3;
        param[4] = para4;
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    return 0;
}

int32
ctc_oam_model_cmd_rx(uint32 type, uint32* param, uint32 param_count)
{
    uint32 cmd = 0;

    cmd = param[0];

    switch (cmd)
    {
    case MODEL_OAM_CMD_CACHE_TIMER:
#if (SDK_WORK_PLATFORM == 1 && SDK_WORK_ENV == 1)  /*simulation */
        return cm_oam_cfm_run_error_cache_timer(param[1]);
#endif
    case MODEL_OAM_CMD_UPDATE_TIMER:
#if (SDK_WORK_PLATFORM == 1 && SDK_WORK_ENV == 1)  /*simulation */
        return cm_oam_cfm_run_ccm(param[1]);
#endif
    case MODEL_OAM_CMD_UPDATE_MEP:
#if (SDK_WORK_PLATFORM == 1 && SDK_WORK_ENV == 1)  /*simulation */
        return cm_oam_update_ccm_timer(param[1], param[2], param[3], param[4]);
#endif

    default:
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}





CTC_CLI(ctc_cli_gb_oam_show_bfd_up_time,
        ctc_cli_gb_oam_show_bfd_up_time_cmd,
        "show bfd up time (lchip LCHIP|)",
        CTC_CLI_SHOW_STR
        "BFD",
        "UP state",
        "Time",
        CTC_CLI_CHIP_DESC,
        CTC_CLI_CHIP_ID_DESC)
{
    int32 ret = CLI_SUCCESS;
    uint8 lchip = 0;
    uint8 index = 0;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8_RANGE("lchip", lchip, argv[index + 1], 0, CTC_MAX_UINT8_VALUE);
    }
    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_greatbelt_oam_bfd_session_up_time(lchip);
    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s\n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gb_oam_pf_interval,
        ctc_cli_gb_oam_pf_interval_cmd,
        "bfd pf interval INTERVAL (lchip LCHIP|)",
        "BFD",
        "Pf",
        "Interval",
        "Interval value, us",
        CTC_CLI_CHIP_DESC,
        CTC_CLI_CHIP_ID_DESC)
{
    int32 ret = CLI_SUCCESS;
    uint8 lchip = 0;
    uint8 index = 0;
    uint32 interval   = 0;

    CTC_CLI_GET_UINT32_RANGE("Interval", interval, argv[0], 0, CTC_MAX_UINT32_VALUE);
    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8_RANGE("lchip", lchip, argv[index + 1], 0, CTC_MAX_UINT8_VALUE);
    }
    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_greatbelt_oam_bfd_pf_interval(lchip, interval);
    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s\n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}


CTC_CLI(ctc_cli_gb_oam_show_bfd_session_state,
        ctc_cli_gb_oam_show_bfd_session_state_cmd,
        "show bfd session state num NUM (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        "BFD",
        "Session",
        "State",
        "Session number",
        "Session number value",
        CTC_CLI_CHIP_DESC,
        CTC_CLI_CHIP_ID_DESC)
{
    int32 ret = CLI_SUCCESS;
    uint32 session_number   = 0;
    uint8 lchip = 0;
    uint8 index = 0;
    CTC_CLI_GET_UINT32_RANGE("Session Number", session_number, argv[0], 0, CTC_MAX_UINT32_VALUE);
    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8_RANGE("lchip", lchip, argv[index + 1], 0, CTC_MAX_UINT8_VALUE);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_greatbelt_oam_bfd_session_state(lchip, session_number);
    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s\n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}


CTC_CLI(ctc_cli_oam_bfd_show_mep_info_with_key,
        ctc_cli_oam_bfd_show_mep_info_with_key_cmd,
        "show bfd info my-discr MY_DISCR \
        (ip-bfd | mpls-bfd | tp-bfd { (section-oam (gport GPORT_ID |ifid IFID) | (space SPACEID |) mpls-in-label IN_LABEL)})",
        CTC_CLI_SHOW_STR,
        "BFD",
        "BFD information",
        "My Discriminator",
        "My Discriminator value, <0~4294967295>",
        "IP BFD OAM option",
        "MPLS BFD OAM option",
        "MPLS TP BFD OAM option",
        "Is MPLS TP section oam",
        CTC_CLI_GPORT_DESC,
        CTC_CLI_GPORT_ID_DESC,
        CTC_CLI_L3IF_ID_DESC,
        CTC_CLI_L3IF_ID_RANGE_DESC,
        "Mpls label space",
        "Label space id, <0-255>, space 0 is platform space",
        "Mpls label",
        "Label, <0-1048575>")
{
    int32 ret   = CLI_SUCCESS;
    uint8 index = 0;

    ctc_oam_mep_info_with_key_t  oam_mep_info;

    /* default configure */
    sal_memset(&oam_mep_info, 0, sizeof(ctc_oam_mep_info_with_key_t));

    index = CTC_CLI_GET_ARGC_INDEX("ip-bfd");
    if (0xFF != index)
    {
        oam_mep_info.key.mep_type = CTC_OAM_MEP_TYPE_IP_BFD;
        CTC_CLI_GET_UINT32_RANGE("my discr", oam_mep_info.key.u.bfd.discr, argv[0], 0, CTC_MAX_UINT32_VALUE);
    }

    index = CTC_CLI_GET_ARGC_INDEX("mpls-bfd");
    if (0xFF != index)
    {
        oam_mep_info.key.mep_type = CTC_OAM_MEP_TYPE_MPLS_BFD;
        CTC_CLI_GET_UINT32_RANGE("my discr", oam_mep_info.key.u.bfd.discr, argv[0], 0, CTC_MAX_UINT32_VALUE);
    }

    index = CTC_CLI_GET_ARGC_INDEX("tp-bfd");
    if (0xFF != index)
    {
        oam_mep_info.key.mep_type = CTC_OAM_MEP_TYPE_MPLS_TP_BFD;

        index = CTC_CLI_GET_ARGC_INDEX("section-oam");

        if (0xFF != index)
        {
            CTC_SET_FLAG(oam_mep_info.key.flag, CTC_OAM_KEY_FLAG_LINK_SECTION_OAM);
        }

        index = CTC_CLI_GET_ARGC_INDEX("gport");
        if (index != 0xff)
        {
            CTC_CLI_GET_UINT16_RANGE("gport", oam_mep_info.key.u.tp.gport_or_l3if_id, argv[index + 1], 0, CTC_MAX_UINT16_VALUE);
        }

        index = CTC_CLI_GET_ARGC_INDEX("ifid");
        if (index != 0xff)
        {
            CTC_CLI_GET_UINT16_RANGE("IFID", oam_mep_info.key.u.tp.gport_or_l3if_id, argv[index + 1], 0, CTC_MAX_UINT16_VALUE);
        }

        index = CTC_CLI_GET_ARGC_INDEX("space");
        if (0xFF != index)
        {
            CTC_CLI_GET_UINT8_RANGE("MPLS label space", oam_mep_info.key.u.tp.mpls_spaceid , argv[index + 1], 0, CTC_MAX_UINT8_VALUE);
        }

        index = CTC_CLI_GET_ARGC_INDEX("mpls-in-label");
        if (0xFF != index)
        {
            CTC_CLI_GET_UINT32_RANGE("mpls label", oam_mep_info.key.u.tp.label, argv[index + 1], 0, CTC_MAX_UINT32_VALUE);
        }
    }

    if (g_ctcs_api_en)
    {

        ret = ctcs_oam_get_mep_info_with_key(g_api_lchip, &oam_mep_info);
    }
    else
    {
        ret = ctc_oam_get_mep_info_with_key(&oam_mep_info);
    }


    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

     /*oam_mep_info.rmep.bfd_rmep.lmep_index*/

    ctc_cli_out("Local BFD information: \n");
    ctc_cli_out("Autucal Tx interval: %-9d, Desired Min Tx interval: %-9d\n",
        oam_mep_info.lmep.bfd_lmep.actual_tx_interval, oam_mep_info.lmep.bfd_lmep.desired_min_tx_interval);
    ctc_cli_out("CC En:               %-9d, MEP En:                  %-9d\n",
        oam_mep_info.lmep.bfd_lmep.cc_enable, oam_mep_info.lmep.bfd_lmep.mep_en);

    ctc_cli_out("State:               %-9d, Detect mult:             %-9d\n",
        oam_mep_info.lmep.bfd_lmep.loacl_state, oam_mep_info.lmep.bfd_lmep.local_detect_mult);
    ctc_cli_out("Discr:               %-9u, Diag:                    %-9d,              Single Hop: %-9d\n",
        oam_mep_info.lmep.bfd_lmep.local_discr, oam_mep_info.lmep.bfd_lmep.local_diag, oam_mep_info.lmep.bfd_lmep.single_hop);

    ctc_cli_out("\nRemote BFD information: \n");
    ctc_cli_out("Autucal Rx interval: %-9d, Required Min Rx interval:%-9d\n",
        oam_mep_info.rmep.bfd_rmep.actual_rx_interval, oam_mep_info.rmep.bfd_rmep.required_min_rx_interval);
    ctc_cli_out("1st Pkt:             %-9d, MEP En:                  %-9d\n",
        oam_mep_info.rmep.bfd_rmep.first_pkt_rx, oam_mep_info.rmep.bfd_rmep.mep_en);

    ctc_cli_out("State:               %-9d, Detect mult:             %-9d\n",
        oam_mep_info.rmep.bfd_rmep.remote_state, oam_mep_info.rmep.bfd_rmep.remote_detect_mult);
    ctc_cli_out("Discr:               %-9u, Diag:                    %-9d\n",
        oam_mep_info.rmep.bfd_rmep.remote_discr, oam_mep_info.rmep.bfd_rmep.remote_diag);

    return ret;
}

CTC_CLI(ctc_cli_gb_oam_trpt_show_state,
        ctc_cli_gb_oam_trpt_show_state_cmd,
        "show oam trpt state (gchip GCHIP|) (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        CTC_CLI_OAM_M_STR,
        CTC_CLI_TRPT_M_STR,
        "State",
        "gchip",
        "gchip id",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret = CLI_SUCCESS;
    uint8 index = 0;
    uint8 gchip = 0;
    uint8 lchip = 0;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    index = CTC_CLI_GET_ARGC_INDEX("gchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8_RANGE("gchip", gchip, argv[index + 1], 0, CTC_MAX_UINT8_VALUE);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_greatbelt_oam_trpt_show_state(lchip, gchip);
    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}


CTC_CLI(ctc_cli_oam_bfd_parse_ip,
        ctc_cli_oam_bfd_parse_ip_cmd,
        "oam ip-bfd parser (enable | disable) (lchip LCHIP|)",
        "OAM",
        "IP bfd",
        "Parser",
        "Enable",
        "Disable",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret       = CLI_SUCCESS;
    uint8 en = 0;
    uint8 lchip = 0;
    uint8 index = 0;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    if (0 == sal_memcmp(argv[0], "e", 1))
    {
        en = 1;
    }
    else
    {
        en = 0;
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_greatbelt_parser_packet_type_ip(lchip, en);
    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;
}

CTC_CLI(ctc_cli_oam_cfm_service_q_mep,
        ctc_cli_oam_cfm_service_q_mep_cmd,
        "service-queue-local-mep (add|update|remove) (gport GPORT_ID) (lchip LCHIP|)",
        "Local MEP for LM/DM in service queue mode",
        "Add entry",
        "Update entry",
        "remove entry",
        CTC_CLI_GPORT_DESC,
        CTC_CLI_GPHYPORT_ID_DESC,
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret       = CLI_SUCCESS;
    uint8 index = 0;
    uint8 lchip = 0;
    ctc_oam_lmep_t lmep;
    ctc_oam_lmep_t lmep_service;

    sal_memset(&lmep, 0, sizeof(ctc_oam_lmep_t));
    sal_memset(&lmep_service, 0, sizeof(ctc_oam_lmep_t));
    sal_memcpy(&lmep, p_oam_lmep, sizeof(ctc_oam_lmep_t));
    sal_memcpy(&lmep_service, p_oam_lmep, sizeof(ctc_oam_lmep_t));

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }
    index = CTC_CLI_GET_ARGC_INDEX("gport");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT16_RANGE("gport", lmep_service.key.u.eth.gport, argv[index + 1], 0, CTC_MAX_UINT16_VALUE);
    }


    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    index = CTC_CLI_GET_ARGC_INDEX("add");
    if (0xFF != index)
    {
        ret = sys_greatbelt_cfm_add_sevice_queue_lmep_for_lm_dm(lchip, &lmep, &lmep_service);
    }
    index = CTC_CLI_GET_ARGC_INDEX("update");
    if (0xFF != index)
    {
        ret = sys_greatbelt_cfm_update_sevice_queue_lmep_for_lm_dm(lchip, &lmep);
    }
    index = CTC_CLI_GET_ARGC_INDEX("remove");
    if (0xFF != index)
    {
        ret = sys_greatbelt_cfm_remove_service_queue_lmep_for_lm_dm(lchip, &lmep);
    }

    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;
}

CTC_CLI(ctc_cli_oam_cfm_show_mep_info_with_key,
        ctc_cli_oam_cfm_show_mep_info_with_key_cmd,
        "show y1731 info (rmep-id RMEP_ID md-level LEVEL|)",
        CTC_CLI_SHOW_STR,
        "Y1731",
        "Y1731 information",
        "Rmep id",
        "Rmep id value",
        "Md level",
        "Level <0-7>")
{
    int32 ret   = CLI_SUCCESS;
    uint8 index = 0;

    ctc_oam_mep_info_with_key_t  oam_mep_info;
    sal_memset(&oam_mep_info, 0, sizeof(ctc_oam_mep_info_with_key_t));
    sal_memcpy(&oam_mep_info.key, &p_oam_lmep->key, sizeof(ctc_oam_key_t));

    index = CTC_CLI_GET_ARGC_INDEX("rmep-id");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT16("rmep-id", oam_mep_info.rmep_id, argv[index + 1]);
    }

    index = CTC_CLI_GET_ARGC_INDEX("md-level");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("Md level", oam_mep_info.key.u.eth.md_level, argv[index + 1]);
    }


    if (g_ctcs_api_en)
    {

        ret = ctcs_oam_get_mep_info_with_key(g_api_lchip, &oam_mep_info);
    }
    else
    {
        ret = ctc_oam_get_mep_info_with_key(&oam_mep_info);
    }

    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    ctc_cli_out("Local y1731 information: \n");
    ctc_cli_out("-------------------------------------------------------------------------------\n");

    ctc_cli_out("Local mep id:              %-9d\n",
                oam_mep_info.lmep.y1731_lmep.mep_id);

    ctc_cli_out("CCM interval:              %-9d, CCM enable:                %-9d\n",
                oam_mep_info.lmep.y1731_lmep.ccm_interval, oam_mep_info.lmep.y1731_lmep.ccm_enable);
    ctc_cli_out("DM enable:                 %-9d\n", oam_mep_info.lmep.y1731_lmep.dm_enable);

    ctc_cli_out("Unexp mep defect:          %-9d, Mismerge defect:           %-9d\n",
                oam_mep_info.lmep.y1731_lmep.d_unexp_mep, oam_mep_info.lmep.y1731_lmep.d_mismerge);

    ctc_cli_out("Xcon defect:               %-9u\n", oam_mep_info.lmep.y1731_lmep.d_meg_lvl);

    ctc_cli_out("Vlan id:                   %-9d, MD level:                  %-9d\n",
                oam_mep_info.lmep.y1731_lmep.vlan_id, oam_mep_info.lmep.y1731_lmep.level);
    ctc_cli_out("-------------------------------------------------------------------------------\n");


    ctc_cli_out("\nRemote y1731 information: \n");
    ctc_cli_out("-------------------------------------------------------------------------------\n");

    ctc_cli_out("Remote mep id:             %-9d\n",
                oam_mep_info.rmep.y1731_rmep.rmep_id);

    ctc_cli_out("1st Pkt:                   %-9d, Local mep table index:     %-9d\n",
                oam_mep_info.rmep.y1731_rmep.first_pkt_rx, oam_mep_info.rmep.y1731_rmep.lmep_index);

    ctc_cli_out("Last rdi value:            %-9d, Dloc defect:               %-9d\n",
                oam_mep_info.rmep.y1731_rmep.last_rdi, oam_mep_info.rmep.y1731_rmep.d_loc);

    ctc_cli_out("Unexpect ccm period defect:%-9u, CSF enable:                %-9d\n",
                oam_mep_info.rmep.y1731_rmep.d_unexp_period, oam_mep_info.rmep.y1731_rmep.csf_en);

    ctc_cli_out("CSF defect:                %-9u, Rx CSF type from remote:   %-9d\n",
                oam_mep_info.rmep.y1731_rmep.d_csf, oam_mep_info.rmep.y1731_rmep.rx_csf_type);

    ctc_cli_out("-------------------------------------------------------------------------------\n");


    return ret;
}




int32
ctc_greatbelt_oam_cli_init(void)
{
    install_element(CTC_SDK_MODE, &ctc_cli_gb_oam_cfm_dump_maid_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gb_oam_cfm_dump_ma_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gb_oam_cfm_dump_lmep_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gb_oam_cfm_dump_rmep_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gb_oam_cfm_show_port_oam_info_cmd);

    install_element(CTC_SDK_OAM_CHAN_MODE, &ctc_cli_gb_oam_cfm_dump_mep_index_cmd);

    install_element(CTC_INTERNAL_MODE, &ctc_cli_gb_oam_set_mep_index_by_sw_cmd);
    install_element(CTC_INTERNAL_MODE, &ctc_cli_gb_oam_set_mep_maid_len_cmd);
    install_element(CTC_INTERNAL_MODE, &ctc_cli_gb_oam_clear_error_cache_cmd);
    install_element(CTC_INTERNAL_MODE, &ctc_cli_gb_oam_tp_section_oam_cmd);
#if 1
    install_element(CTC_SDK_MODE, &ctc_cli_gb_oam_update_error_cache_timer_enable_cmd);
    install_element(CTC_INTERNAL_MODE, &cli_oam_cfm_update_fs_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gb_oam_update_update_timer_enable_cmd);

#endif

    install_element(CTC_SDK_MODE, &ctc_cli_gb_oam_show_bfd_up_time_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gb_oam_pf_interval_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gb_oam_show_bfd_session_state_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_oam_bfd_show_mep_info_with_key_cmd);
    install_element(CTC_SDK_OAM_CHAN_MODE, &ctc_cli_oam_cfm_show_mep_info_with_key_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gb_oam_trpt_show_state_cmd);

    install_element(CTC_INTERNAL_MODE, &ctc_cli_oam_bfd_parse_ip_cmd);

    install_element(CTC_SDK_OAM_CHAN_MODE, &ctc_cli_oam_cfm_service_q_mep_cmd);

    return 0;
}

int32
ctc_greatbelt_oam_cli_deinit(void)
{
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_oam_cfm_dump_maid_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_oam_cfm_dump_ma_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_oam_cfm_dump_lmep_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_oam_cfm_dump_rmep_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_oam_cfm_show_port_oam_info_cmd);

    uninstall_element(CTC_SDK_OAM_CHAN_MODE, &ctc_cli_gb_oam_cfm_dump_mep_index_cmd);

    uninstall_element(CTC_INTERNAL_MODE, &ctc_cli_gb_oam_set_mep_index_by_sw_cmd);
    uninstall_element(CTC_INTERNAL_MODE, &ctc_cli_gb_oam_set_mep_maid_len_cmd);
    uninstall_element(CTC_INTERNAL_MODE, &ctc_cli_gb_oam_clear_error_cache_cmd);
    uninstall_element(CTC_INTERNAL_MODE, &ctc_cli_gb_oam_tp_section_oam_cmd);
#if 1
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_oam_update_error_cache_timer_enable_cmd);
    uninstall_element(CTC_INTERNAL_MODE, &cli_oam_cfm_update_fs_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_oam_update_update_timer_enable_cmd);

#endif

    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_oam_show_bfd_up_time_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_oam_pf_interval_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_oam_show_bfd_session_state_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_oam_bfd_show_mep_info_with_key_cmd);
    uninstall_element(CTC_SDK_OAM_CHAN_MODE, &ctc_cli_oam_cfm_show_mep_info_with_key_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_oam_trpt_show_state_cmd);

    uninstall_element(CTC_INTERNAL_MODE, &ctc_cli_oam_bfd_parse_ip_cmd);

    uninstall_element(CTC_SDK_OAM_CHAN_MODE, &ctc_cli_oam_cfm_service_q_mep_cmd);

    return 0;
}


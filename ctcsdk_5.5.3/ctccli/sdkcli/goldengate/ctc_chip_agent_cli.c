/**
 @file ctc_chip_agent_goldengate_cli.c

 @date 2010-7-9

 @version v2.0

---file comments----
*/


#ifdef CHIP_AGENT

#include "ctc_cli.h"
#include "ctc_cli_common.h"
#include "ctc_error.h"
#include "ctc_api.h"
#include "ctcs_api.h"
#include "ctc_debug.h"
#include "ctc_chip_agent_cli.h"

#define EAPD_PACKET_MODE_FIXED      0
#define EAPD_PACKET_MODE_INCR       1
#define EAPD_PACKET_MODE_RANDOM     2

extern char*
chip_agent_error_str(int32 error);
extern int32
chip_agent_client_set_ctrl_en(uint32 module, uint32 flag, uint32 enable);
extern int32
chip_agent_client_get_ctrl_en(uint32 module, uint32* flag);
extern int32
chip_agent_client_set_sim_dbg_action(uint32 flag);
extern int32
chip_agent_server_set_sim_dbg_action(uint32 flag);
extern int32
chip_agent_client_get_sim_dbg_action(uint32* action);
extern int32
chip_agent_sock_client_init(char* address, uint32 port, uint32 server_id);
extern int32
chip_agent_clear_stats();
extern int32
chip_agent_clear_drvio_cache();
extern int32
chip_agent_client_set_pkt_rx_print_en(uint32 enable);
extern int32
chip_agent_show();
extern int32
chip_agent_show_drvio_cache(uint32 dir, uint32 last_count, uint32 detail);
extern int32
chip_agent_show_error();
extern int32
swemu_sys_board_set_debug_net(uint32 flags, uint32 is_set);
extern uint32
swemu_sys_board_get_debug_net();
extern int32
swemu_sys_board_show_stats();
extern int32
swemu_sys_board_clear_stats();
extern uint32
chip_agent_get_mode();
extern char*
chip_agent_sim_dbg_action_str(uint32 action);
extern uint32
chip_agent_server_get_sim_chip_model();
extern uint32
chip_agent_server_get_sim_dbg_action();

CTC_CLI(ctc_cli_gg_chip_agent_debug_io_on,
        ctc_cli_gg_chip_agent_debug_io_on_cmd,
        "debug chip-agent driver-io (socket|code|all)",
        CTC_CLI_DEBUG_STR,
        CTC_CLI_CHIP_AGT_STR,
        "Driver IO",
        "Socket",
        "Encode and Decode",
        "Port proxy",
        "All")
{
    uint32 typeenum = 0;
    uint32 enable = TRUE;
    uint8 level = CTC_DEBUG_LEVEL_INFO;

    if (0 == sal_memcmp(argv[0], "socket", 6))
    {
        typeenum = CHIP_AGT_SOCKET;
    }
    else if (0 == sal_memcmp(argv[0], "code", 4))
    {
        typeenum = CHIP_AGT_CODE;
    }
    else
    {
        typeenum = 0xFF;
    }

    if (typeenum == 0xFF)
    {
        ctc_debug_set_flag("chipagent", "chipagent", CHIP_AGT_SOCKET, level, enable);
        ctc_debug_set_flag("chipagent", "chipagent", CHIP_AGT_CODE, level, enable);
    }
    else
    {
        ctc_debug_set_flag("chipagent", "chipagent", typeenum, level, enable);
    }

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gg_chip_agent_debug_io_off,
        ctc_cli_gg_chip_agent_debug_io_off_cmd,
        "no debug chip-agent driver-io (socket|code|all)",
        CTC_CLI_NO_STR,
        CTC_CLI_DEBUG_STR,
        CTC_CLI_CHIP_AGT_STR,
        "Driver IO",
        "Socket",
        "Encode and Decode",
        "All")
{
    uint32 typeenum = 0;
    uint32 enable = FALSE;
    uint8 level = CTC_DEBUG_LEVEL_INFO;

    if (0 == sal_memcmp(argv[0], "socket", 6))
    {
        typeenum = CHIP_AGT_SOCKET;
    }
    else if (0 == sal_memcmp(argv[0], "code", 4))
    {
        typeenum = CHIP_AGT_CODE;
    }
    else
    {
        typeenum = 0xFF;
    }

    if (typeenum == 0xFF)
    {
        ctc_debug_set_flag("chipagent", "chipagent", CHIP_AGT_SOCKET, level, enable);
        ctc_debug_set_flag("chipagent", "chipagent", CHIP_AGT_CODE, level, enable);
    }
    else
    {
        ctc_debug_set_flag("chipagent", "chipagent", typeenum, level, enable);
    }

    return CLI_SUCCESS;
}

#define CHIP_AGT_E_SOCK_NOT_CONNECT     -19987

CTC_CLI(ctc_cli_gg_chip_agent_show_debug,
        ctc_cli_gg_chip_agent_show_debug_cmd,
        "show debug chip-agent",
        CTC_CLI_SHOW_STR,
        CTC_CLI_DEBUG_STR,
        CTC_CLI_CHIP_AGT_STR)
{
#ifdef NEVER
    uint8 level = CTC_DEBUG_LEVEL_INFO;
    uint32 module = 0;
    uint32 flag = 0;
    int32 ret = 0;

    ctc_cli_out("##### Driver IO Debug #####\n");
    ctc_cli_out("%-20s %-10s\n", "Socket",
                (ctc_debug_get_flag("chipagent", "chipagent", CHIP_AGT_SOCKET, &level)) ? "on" : "off", ctc_cli_get_debug_desc(level));
    ctc_cli_out("%-20s %-10s\n", "Code",
                (ctc_debug_get_flag("chipagent", "chipagent", CHIP_AGT_CODE, &level)) ? "on" : "off", ctc_cli_get_debug_desc(level));

#if (SDK_WORK_PLATFORM == 1 && SDK_WORK_ENV == 1)  /*simulation */
    if (2 == chip_agent_get_mode())
    {
        ctc_cli_out("\n");
        ctc_cli_out("##### Local Simulator Debug #####\n");
        flag = swemu_sys_board_get_debug_net();
        ctc_cli_out("%-20s %-10s\n", "Packet IO", (flag & 0x01) ? "on" : "off");

        flag = chip_agent_server_get_sim_chip_model();
        ctc_cli_out("\n");
        ctc_cli_out("%-20s %-10s\n", "Chip - Forward Path", (flag & 0x01) ? "on" : "off");
        ctc_cli_out("%-20s %-10s\n", "Chip - Parser Info", (flag & 0x02) ? "on" : "off");
        ctc_cli_out("%-20s %-10s\n", "Chip - Discard Type", (flag & 0x04) ? "on" : "off");
        ctc_cli_out("%-20s %-10s\n", "Chip - Lookup Key", (flag & 0x08) ? "on" : "off");
        ctc_cli_out("%-20s %-10s\n", "Chip - Key Info", (flag & 0x10) ? "on" : "off");
        ctc_cli_out("%-20s %-10s\n", "Chip - Table Index", (flag & 0x20) ? "on" : "off");
        ctc_cli_out("%-20s %-10s\n", "Chip - Qmgr Header", (flag & 0x40) ? "on" : "off");
        ctc_cli_out("%-20s %-10s\n", "Chip - Qmgr Message", (flag & 0x80) ? "on" : "off");
        ctc_cli_out("%-20s %-10s\n", "Chip - Qmgr Linkagg", (flag & 0x100) ? "on" : "off");

        flag = chip_agent_server_get_sim_dbg_action();
        ctc_cli_out("\n");
        ctc_cli_out("%-20s %-10s\n", "Log Action", chip_agent_sim_dbg_action_str(flag));
    }

#endif

    if (1 == chip_agent_get_mode())
    {
         /* show simulator's debug*/
        module = 0; /* CHIP_AGT_CTRL_DBG_NET */
        ret = chip_agent_client_get_ctrl_en(module, &flag);
        if (ret < 0)
        {
            if (CHIP_AGT_E_SOCK_NOT_CONNECT == ret)
            {
                return CLI_SUCCESS;
            }
            else
            {
                ctc_cli_out("%% %s\n", chip_agent_error_str(ret));
                return CLI_ERROR;
            }
        }

        ctc_cli_out("\n");
        ctc_cli_out("##### Remote Simulator Debug #####\n");
        ctc_cli_out("%-20s %-10s\n", "Packet IO", (flag & 0x01) ? "on" : "off");

        module = 1; /* CHIP_AGT_CTRL_DBG_CHIP */
        ret = chip_agent_client_get_ctrl_en(module, &flag);
        if (ret < 0)
        {
            if (CHIP_AGT_E_SOCK_NOT_CONNECT == ret)
            {
                return CLI_SUCCESS;
            }
            else
            {
                ctc_cli_out("%% %s\n", chip_agent_error_str(ret));
                return CLI_ERROR;
            }
        }

        ctc_cli_out("\n");
        ctc_cli_out("%-20s %-10s\n", "Chip - Forward Path", (flag & 0x01) ? "on" : "off");
        ctc_cli_out("%-20s %-10s\n", "Chip - Parser Info", (flag & 0x02) ? "on" : "off");
        ctc_cli_out("%-20s %-10s\n", "Chip - Discard Type", (flag & 0x04) ? "on" : "off");
        ctc_cli_out("%-20s %-10s\n", "Chip - Lookup Key", (flag & 0x08) ? "on" : "off");
        ctc_cli_out("%-20s %-10s\n", "Chip - Key Info", (flag & 0x10) ? "on" : "off");
        ctc_cli_out("%-20s %-10s\n", "Chip - Table Index", (flag & 0x20) ? "on" : "off");
        ctc_cli_out("%-20s %-10s\n", "Chip - Qmgr Header", (flag & 0x40) ? "on" : "off");
        ctc_cli_out("%-20s %-10s\n", "Chip - Qmgr Message", (flag & 0x80) ? "on" : "off");
        ctc_cli_out("%-20s %-10s\n", "Chip - Qmgr Linkagg", (flag & 0x100) ? "on" : "off");

        ret = chip_agent_client_get_sim_dbg_action(&flag);
        if (ret < 0)
        {
            if (CHIP_AGT_E_SOCK_NOT_CONNECT == ret)
            {
                return CLI_SUCCESS;
            }
            else
            {
                ctc_cli_out("%% %s\n", chip_agent_error_str(ret));
                return CLI_ERROR;
            }
        }

        ctc_cli_out("\n");
        ctc_cli_out("%-20s %-10s\n", "Log Action", chip_agent_sim_dbg_action_str(flag));
    }
#endif /* NEVER */

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gg_chip_agent_debug_simulator_on,
        ctc_cli_gg_chip_agent_debug_simulator_on_cmd,
        "debug chip-agent simulator packet-io",
        CTC_CLI_DEBUG_STR,
        CTC_CLI_CHIP_AGT_STR,
        "Simulator",
        "Packet IO")
{
    uint32 type = 0;
    uint32 flag = 0;
    int32 ret = 0;

    if (1 != chip_agent_get_mode()) /* CHIP_AGT_MODE_CLIENT */
    {
        ctc_cli_out("%% Only valid for EADP client\n");
        return CLI_ERROR;
    }

    type = 0; /* CHIP_AGT_CTRL_DBG_NET */
    flag = 0x01;    /* SWEMU_BOARD_DEBUG_PKT_RAW */
    ret = chip_agent_client_set_ctrl_en(type, flag, TRUE);
    if (ret < 0)
    {
        ctc_cli_out("%% %s\n", chip_agent_error_str(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gg_chip_agent_debug_simulator_off,
        ctc_cli_gg_chip_agent_debug_simulator_off_cmd,
        "no debug chip-agent simulator packet-io",
        CTC_CLI_NO_STR,
        CTC_CLI_DEBUG_STR,
        CTC_CLI_CHIP_AGT_STR,
        "Simulator",
        "Packet IO",
        "All simulator modules")
{
    uint32 type = 0;
    uint32 flag = 0;
    int32 ret = 0;

    if (1 != chip_agent_get_mode()) /* CHIP_AGT_MODE_CLIENT */
    {
        ctc_cli_out("%% Only valid for EADP client\n");
        return CLI_ERROR;
    }

    type = 0;       /* CHIP_AGT_CTRL_DBG_NET */
    flag = 0x01;    /* SWEMU_BOARD_DEBUG_PKT_RAW */
    ret = chip_agent_client_set_ctrl_en(type, flag, FALSE);
    if (ret < 0)
    {
        ctc_cli_out("%% %s\n", chip_agent_error_str(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gg_chip_agent_debug_sim_chip_model_on,
        ctc_cli_gg_chip_agent_debug_sim_chip_model_on_cmd,
        "debug chip-agent simulator chip-model (all | {fwd-path | parser-info | key-info | lookup-key | tbl-ptr | discard-type | qmgr-msg | qmgr-hdr | qmgr-linkagg | aging})",
        CTC_CLI_DEBUG_STR,
        CTC_CLI_CHIP_AGT_STR,
        "Simulator",
        "Chip model",
        "All debug",
        "Forward path",
        "Parser",
        "Process Key",
        "Lookup Key",
        "Table pointer",
        "Discard reason",
        "Queue manager message",
        "Queue manager packet header",
        "Queue manager link aggregation",
        "Aging Timer")
{
    uint8 index = 0;
    uint32 type = 0;
    uint32 flag = 0;
    int32 ret = 0;

    if (1 != chip_agent_get_mode()) /* CHIP_AGT_MODE_CLIENT */
    {
        ctc_cli_out("%% Only valid for EADP client\n");
        return CLI_ERROR;
    }

    if (0xFF != index)
    {
        index = CTC_CLI_GET_ARGC_INDEX("fwd-path");
        if (0xFF != index)
        {
            flag |= 0x01;   /* CTC_CMODEL_DEBUG_FLAG_FWD_PATH */
        }

        index = CTC_CLI_GET_ARGC_INDEX("parser-info");
        if (0xFF != index)
        {
            flag |= 0x02;   /* CTC_CMODEL_DEBUG_FLAG_PARSER_INFO */
        }

        index = CTC_CLI_GET_ARGC_INDEX("key-info");
        if (0xFF != index)
        {
            flag |= 0x10;   /* CTC_CMODEL_DEBUG_FLAG_KEY_INFO */
        }

        index = CTC_CLI_GET_ARGC_INDEX("lookup-key");
        if (0xFF != index)
        {
            flag |= 0x08;   /* CTC_CMODEL_DEBUG_FLAG_LOOKUP_KEY */
        }

        index = CTC_CLI_GET_ARGC_INDEX("tbl-ptr");
        if (0xFF != index)
        {
            flag |= 0x20;   /* CTC_CMODEL_DEBUG_FLAG_TABLE_PTR */
        }

        index = CTC_CLI_GET_ARGC_INDEX("discard-type");
        if (0xFF != index)
        {
            flag |= 0x04;   /* CTC_CMODEL_DEBUG_FLAG_DISCARD_TYPE */
        }

        index = CTC_CLI_GET_ARGC_INDEX("qmgr-hdr");
        if (0xFF != index)
        {
            flag |= 0x40;   /* CTC_CMODEL_DEBUG_FLAG_QMGT_HDR */
        }

        index = CTC_CLI_GET_ARGC_INDEX("qmgr-msg");
        if (0xFF != index)
        {
            flag |= 0x80;   /* CTC_CMODEL_DEBUG_FLAG_QMGT_MSG */
        }

        index = CTC_CLI_GET_ARGC_INDEX("qmgr-linkagg");
        if (0xFF != index)
        {
            flag |= 0x100;   /* CTC_CMODEL_DEBUG_FLAG_QMGT_LINKAGG */
        }

        index = CTC_CLI_GET_ARGC_INDEX("aging");
        if (0xFF != index)
        {
            flag |= 0x200;   /* CTC_CMODEL_DEBUG_FLAG_AGING */
        }

        index = CTC_CLI_GET_ARGC_INDEX("all");
        if (0xFF != index)
        {
            flag |= 0x3FF;
        }
    }

    type = 1;       /* CHIP_AGT_CTRL_DBG_CHIP */
    ret = chip_agent_client_set_ctrl_en(type, flag, TRUE);

    return ret;
}

CTC_CLI(ctc_cli_gg_chip_agent_debug_sim_chip_model_off,
        ctc_cli_gg_chip_agent_debug_sim_chip_model_off_cmd,
        "no debug chip-agent simulator chip-model (all | {fwd-path | parser-info | key-info | lookup-key | tbl-ptr | discard-type | qmgr-msg | qmgr-hdr | qmgr-linkagg | aging})",
        CTC_CLI_NO_STR,
        CTC_CLI_DEBUG_STR,
        CTC_CLI_CHIP_AGT_STR,
        "Simulator",
        "Chip model",
        "All debug",
        "Forward path",
        "Parser",
        "Process Key",
        "Lookup Key",
        "Table pointer",
        "Discard reason",
        "Queue manager message",
        "Queue manager packet header",
        "Queue manager link aggregation",
        "Aging Timer")
{
    uint8 index = 0;
    uint32 type = 0;
    uint32 flag = 0;
    int32 ret = 0;

    if (1 != chip_agent_get_mode()) /* CHIP_AGT_MODE_CLIENT */
    {
        ctc_cli_out("%% Only valid for EADP client\n");
        return CLI_ERROR;
    }

    if (0xFF != index)
    {
        index = CTC_CLI_GET_ARGC_INDEX("fwd-path");
        if (0xFF != index)
        {
            flag |= 0x01;   /* CTC_CMODEL_DEBUG_FLAG_FWD_PATH */
        }

        index = CTC_CLI_GET_ARGC_INDEX("parser-info");
        if (0xFF != index)
        {
            flag |= 0x02;   /* CTC_CMODEL_DEBUG_FLAG_PARSER_INFO */
        }

        index = CTC_CLI_GET_ARGC_INDEX("key-info");
        if (0xFF != index)
        {
            flag |= 0x10;   /* CTC_CMODEL_DEBUG_FLAG_KEY_INFO */
        }

        index = CTC_CLI_GET_ARGC_INDEX("lookup-key");
        if (0xFF != index)
        {
            flag |= 0x08;   /* CTC_CMODEL_DEBUG_FLAG_LOOKUP_KEY */
        }

        index = CTC_CLI_GET_ARGC_INDEX("tbl-ptr");
        if (0xFF != index)
        {
            flag |= 0x20;   /* CTC_CMODEL_DEBUG_FLAG_TABLE_PTR */
        }

        index = CTC_CLI_GET_ARGC_INDEX("discard-type");
        if (0xFF != index)
        {
            flag |= 0x04;   /* CTC_CMODEL_DEBUG_FLAG_DISCARD_TYPE */
        }

        index = CTC_CLI_GET_ARGC_INDEX("qmgr-hdr");
        if (0xFF != index)
        {
            flag |= 0x40;   /* CTC_CMODEL_DEBUG_FLAG_QMGT_HDR */
        }

        index = CTC_CLI_GET_ARGC_INDEX("qmgr-msg");
        if (0xFF != index)
        {
            flag |= 0x80;   /* CTC_CMODEL_DEBUG_FLAG_QMGT_MSG */
        }

        index = CTC_CLI_GET_ARGC_INDEX("qmgr-linkagg");
        if (0xFF != index)
        {
            flag |= 0x100;   /* CTC_CMODEL_DEBUG_FLAG_QMGT_LINKAGG */
        }

        index = CTC_CLI_GET_ARGC_INDEX("aging");
        if (0xFF != index)
        {
            flag |= 0x200;   /* CTC_CMODEL_DEBUG_FLAG_AGING */
        }

        index = CTC_CLI_GET_ARGC_INDEX("all");
        if (0xFF != index)
        {
            flag |= 0x3FF;
        }
    }

    type = 1;       /* CHIP_AGT_CTRL_DBG_CHIP */
    ret = chip_agent_client_set_ctrl_en(type, flag, FALSE);

    return ret;
}

CTC_CLI(ctc_cli_gg_chip_agent_set_sim_action,
        ctc_cli_gg_chip_agent_set_sim_action_cmd,
        "chip-agent simulator-log (start (server-print|client-print|client-file) | stop)",
        CTC_CLI_CHIP_AGT_STR,
        "Simulator log action",
        "Start log",
        "Log server print",
        "Log client print",
        "Log client file",
        "Stop log")
{
    uint32 action = 0;
    int32 ret = 0;

    if (0 == sal_memcmp(argv[0], "start", 5))
    {
        if (0 == sal_strcmp(argv[1], "server-print"))
        {
            action = 1; /* CHIP_AGT_SIM_DBG_ACT_LOCAL_PRINTF */
        }
        else if (0 == sal_strcmp(argv[1], "client-print"))
        {
            action = 2; /* CHIP_AGT_SIM_DBG_ACT_REMOTE_PRINTF */
        }
        else
        {
            action = 3; /* CHIP_AGT_SIM_DBG_ACT_REMOTE_FILE */
        }
    }

    if (0 == sal_memcmp(argv[0], "stop", 4))
    {
        action = 0; /* STOP */
    }

    if (1 == chip_agent_get_mode()) /* CHIP_AGT_MODE_CLIENT */
    {
        ret = chip_agent_client_set_sim_dbg_action(action);
    }
    else if (2 == chip_agent_get_mode()) /* CHIP_AGT_MODE_SERVER */
    {
        ret = chip_agent_server_set_sim_dbg_action(action);
    }
    else
    {
        ctc_cli_out("%% Only valid for EADP client and server\n");
        return CLI_ERROR;
    }

    if (ret < 0)
    {
        ctc_cli_out("%% %s\n", chip_agent_error_str(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

#if (SDK_WORK_PLATFORM == 1 && SDK_WORK_ENV == 1)  /*simulation */
CTC_CLI(ctc_cli_gg_chip_agent_dump_packet,
        ctc_cli_gg_chip_agent_dump_packet_cmd,
        "show chip-agent stats packet-io",
        "Show",
        CTC_CLI_CHIP_AGT_STR,
        "Statistics",
        "Packet IO")
{
#ifdef NEVER
    if (2 != chip_agent_get_mode()) /* CHIP_AGT_MODE_SERVER */
    {
        ctc_cli_out("%% Only valid for EADP server\n");
        return CLI_ERROR;
    }

    swemu_sys_board_show_stats();
#endif /* NEVER */
    return CLI_SUCCESS;
}

#endif

CTC_CLI(ctc_cli_gg_chip_agent_serverip,
        ctc_cli_gg_chip_agent_serverip_cmd,
        "chip-agent server-id ID server-ip A.B.C.D (port <1024-65535>|)",
        CTC_CLI_CHIP_AGT_STR,
        "server id number",
        "ID",
        "Configured server IP address",
        "Server IP address",
        "Configured TCP port",
        "TCP port number")
{
    int32 ret = 0;
    struct sal_sockaddr_in addr;
    uint32 port = 0;
    uint8 server_id = 0;
    uint8 idx =0;
    uint8 server_ip_idx = 0;

    idx = CTC_CLI_GET_ARGC_INDEX("server-id");
    if (0xFF != idx)
    {
        CTC_CLI_GET_UINT8_RANGE("server-id", server_id, argv[idx+1], 0, CTC_MAX_UINT8_VALUE)
    }

    idx = CTC_CLI_GET_ARGC_INDEX("server-ip");
    if (0xFF != idx)
    {
        server_ip_idx = idx + 1;
        ret = sal_inet_pton(AF_INET, argv[idx+1], &addr.sin_addr);
        if (ret == 0)
        {
            ctc_cli_out("Invalid IP address: %s\n");
            return -1;
        }
    }

    idx = CTC_CLI_GET_ARGC_INDEX("port");
    if (0xFF != idx)
    {
        CTC_CLI_GET_INTEGER("port", port, argv[idx+1]);
    }

    ret = chip_agent_sock_client_init(argv[server_ip_idx], port, server_id);
    if (ret < 0)
    {
        ctc_cli_out("%% %s\n", chip_agent_error_str(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gg_chip_agent_dump,
        ctc_cli_gg_chip_agent_dump_cmd,
        "show chip-agent",
        "Show",
        CTC_CLI_CHIP_AGT_STR)
{
    chip_agent_show();
    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gg_chip_agent_dump_drvio,
        ctc_cli_gg_chip_agent_dump_drvio_cmd,
        "show chip-agent cache driver-io (both|tx|rx) (<1-64>|) (detail|)",
        "Show",
        CTC_CLI_CHIP_AGT_STR,
        "Cache",
        "Driver IO",
        "Both direction",
        "TX direction",
        "RX direction",
        "Last count",
        "Detail information")
{
    uint32 dir = 0;
    uint32 last_count = 64;
    uint32 detail = FALSE;

    if (0 == sal_memcmp(argv[0], "tx", 2))
    {
        CTC_SET_FLAG(dir, 0x1);
    }
    else if (0 == sal_memcmp(argv[0], "rx", 2))
    {
        CTC_SET_FLAG(dir, 0x2);
    }
    else
    {
        CTC_SET_FLAG(dir, (0x1 | 0x2));
    }

    if (argc > 1)
    {
        if (argc > 2)
        {
            CTC_CLI_GET_INTEGER_RANGE("Last Count", last_count, argv[1], 1, 64);
            detail = TRUE;
        }
        else
        {
            if (0 == sal_memcmp(argv[1], "detail", 6))
            {
                detail = TRUE;
            }
            else
            {
                CTC_CLI_GET_INTEGER_RANGE("Last Count", last_count, argv[1], 1, 64);
            }
        }
    }
    else
    {
        last_count = 64;
        detail = FALSE;
    }

    chip_agent_show_drvio_cache(dir, last_count, detail);
    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gg_chip_agent_dump_error,
        ctc_cli_gg_chip_agent_dump_error_cmd,
        "show chip-agent error",
        "Show",
        CTC_CLI_CHIP_AGT_STR,
        "Error information")
{
    chip_agent_show_error();
    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gg_chip_agent_clear,
        ctc_cli_gg_chip_agent_clear_cmd,
        "clear chip-agent (stats (driver-io|packet-io|) | cache)",
        CTC_CLI_CLEAR_STR,
        CTC_CLI_CHIP_AGT_STR,
        "Statistics",
        "Driver IO",
        "Packet IO",
        "Cache of Driver IO")
{
#ifdef NEVER
    if (0 == sal_memcmp(argv[0], "stats", 5))
    {
        if (argc > 1)
        {
            if (0 == sal_memcmp(argv[1], "dri", 3))
            {
                chip_agent_clear_stats();
            }
            else
            {
#if (SDK_WORK_PLATFORM == 1 && SDK_WORK_ENV == 1)  /*simulation */
                swemu_sys_board_clear_stats();
#endif
            }
        }
        else
        {
            chip_agent_clear_stats();
#if (SDK_WORK_PLATFORM == 1 && SDK_WORK_ENV == 1)  /*simulation */
            swemu_sys_board_clear_stats();
#endif
        }
    }
    else if (0 == sal_memcmp(argv[0], "cache", 5))
    {
        chip_agent_clear_drvio_cache();
    }
#endif /* NEVER */

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gg_chip_agent_pkt_rx_print,
        ctc_cli_gg_chip_agent_pkt_rx_print_cmd,
        "chip-agent packet cpu-rx-print",
        CTC_CLI_CHIP_AGT_STR,
        "Packet",
        "Print CPU RX packet")
{
    int32 ret = 0;

    if (1 != chip_agent_get_mode()) /* CHIP_AGT_MODE_CLIENT */
    {
        ctc_cli_out("%% Only valid for EADP client\n");
        return CLI_ERROR;
    }

    ret = chip_agent_client_set_pkt_rx_print_en(TRUE);
    if (ret < 0)
    {
        ctc_cli_out("%% %s\n", chip_agent_error_str(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gg_chip_agent_pkt_rx_print_off,
        ctc_cli_gg_chip_agent_pkt_rx_print_off_cmd,
        "no chip-agent packet cpu-rx-print",
        CTC_CLI_NO_STR,
        CTC_CLI_CHIP_AGT_STR,
        "Packet",
        "Print CPU RX packet")
{
    int32 ret = 0;

    if (1 != chip_agent_get_mode()) /* CHIP_AGT_MODE_CLIENT */
    {
        ctc_cli_out("%% Only valid for EADP client\n");
        return CLI_ERROR;
    }

    ret = chip_agent_client_set_pkt_rx_print_en(FALSE);
    if (ret < 0)
    {
        ctc_cli_out("%% %s\n", chip_agent_error_str(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gg_chip_agent_send_pkt,
        ctc_cli_gg_chip_agent_send_pkt_cmd,
        "chip-agent packet port <0-47> length <64-1024> HEADER payload-mode (fixed <0-255> | increment (<0-254> <2-256>|) | random)",
        CTC_CLI_CHIP_AGT_STR,
        "Packet",
        "Configure port",
        "Port number",
        "Configure packet length",
        "Packet length",
        "Header of packet with HEX format",
        "Configure payload mode",
        "Fixed mode",
        "Fixed value",
        "Increment mode",
        "Increment base",
        "Increment repeat",
        "Random mode")
{
    uint32 tmp = 0;
    uint8 pkt[1500];
    uint8* data = NULL;
    uint32 hdr_str_len  = 0;
    uint32 hdr_len = 0;
    uint32 len = 0;
    uint32 port = 0;
    uint32 mode = 0;
    int32 val = 0;
    uint32 repeat = 0;
    uint32 i = 0;
    int32 base = 0;
    int32 arg_base = 3;

    if (1 != chip_agent_get_mode()) /* CHIP_AGT_MODE_CLIENT */
    {
        ctc_cli_out("%% Only valid for EADP client\n");
        return CLI_ERROR;
    }

    sal_memset(pkt, 0, sizeof(pkt));
    CTC_CLI_GET_INTEGER_RANGE("Port", port, argv[0], 0, 47);
    CTC_CLI_GET_INTEGER_RANGE("Length", len, argv[1], 64, 1024);

    hdr_str_len = sal_strlen(argv[2]);
    if (hdr_str_len % 2)
    {
        ctc_cli_out("%% header's length must be even\n");
        return CLI_ERROR;
    }

    for (i = 0; i < hdr_str_len; i++)
    {
        if ((*(argv[2] + i) <= '9' && *(argv[2] + i) >= '0')
            || (*(argv[2] + i) <= 'f' && *(argv[2] + i) >= 'a')
            || (*(argv[2] + i) <= 'F' && *(argv[2] + i) >= 'A'))
        {
            /*do nothing*/
        }
        else
        {
            ctc_cli_out("%% header's character must be '0-9,a-f,A-F'\n");
            return CLI_ERROR;
        }
    }

    hdr_len = hdr_str_len / 2;

    for (i = 0; i < hdr_len; i++)
    {
        sal_sscanf(argv[2] + (i * 2), "%2x", &tmp);
        pkt[i] = tmp;
    }

    if (0 == sal_memcmp(argv[arg_base], "fixed", 5))
    {
        CTC_CLI_GET_INTEGER_RANGE("Value", val, argv[arg_base + 1], 0, 255);
        mode = EAPD_PACKET_MODE_FIXED;
    }
    else if (0 == sal_memcmp(argv[arg_base], "incre", 5))
    {
        mode = EAPD_PACKET_MODE_INCR;
        if (argc > (arg_base + 2))
        {
            CTC_CLI_GET_INTEGER_RANGE("Repeat", base, argv[arg_base + 1], 0, 254);
            CTC_CLI_GET_INTEGER_RANGE("Repeat", repeat, argv[arg_base + 2], 2, 256);
            if (base + repeat > 256)
            {
                ctc_cli_out("%% base + repeat should be no more than 256\n");
                return CLI_ERROR;
            }
        }
        else
        {
            repeat = 255;
        }
    }
    else
    {
        mode = EAPD_PACKET_MODE_RANDOM;
    }

    data = pkt + hdr_len;

    switch (mode)
    {
    case EAPD_PACKET_MODE_FIXED:
        sal_memset(data, val, len);
        break;

    case EAPD_PACKET_MODE_INCR:
        val = base - 1;

        for (i = 0; i < len; i++)
        {
            if (val >= (repeat + base - 1))
            {
                val = base;
            }
            else
            {
                val++;
            }

            data[i] = val;
        }

        break;

    case EAPD_PACKET_MODE_RANDOM:

        for (i = 0; i < len; i++)
        {
            data[i] = rand() % 0xFF;
        }

        break;

    default:
        break;
    }

     /*chip_agent_client_send_pkt(pkt, len, port);*/
    return CLI_SUCCESS;
}

int32
ctc_goldengate_chip_agent_cli_init(void)
{
    install_element(CTC_SDK_MODE, &ctc_cli_gg_chip_agent_debug_io_on_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gg_chip_agent_debug_io_off_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gg_chip_agent_debug_simulator_on_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gg_chip_agent_debug_simulator_off_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gg_chip_agent_debug_sim_chip_model_on_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gg_chip_agent_debug_sim_chip_model_off_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gg_chip_agent_set_sim_action_cmd);
#if (SDK_WORK_PLATFORM == 1 && SDK_WORK_ENV == 1)  /*simulation */
    install_element(CTC_SDK_MODE, &ctc_cli_gg_chip_agent_dump_packet_cmd);
#endif
    install_element(CTC_SDK_MODE, &ctc_cli_gg_chip_agent_show_debug_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gg_chip_agent_serverip_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gg_chip_agent_dump_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gg_chip_agent_dump_drvio_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gg_chip_agent_dump_error_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gg_chip_agent_clear_cmd);
     /*install_element(CTC_SDK_MODE, &ctc_cli_gg_chip_agent_send_pkt_cmd);*/
    install_element(CTC_SDK_MODE, &ctc_cli_gg_chip_agent_pkt_rx_print_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gg_chip_agent_pkt_rx_print_off_cmd);

    return CLI_SUCCESS;
}

int32
ctc_goldengate_chip_agent_cli_deinit(void)
{
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_chip_agent_debug_io_on_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_chip_agent_debug_io_off_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_chip_agent_debug_simulator_on_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_chip_agent_debug_simulator_off_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_chip_agent_debug_sim_chip_model_on_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_chip_agent_debug_sim_chip_model_off_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_chip_agent_set_sim_action_cmd);
#if (SDK_WORK_PLATFORM == 1 && SDK_WORK_ENV == 1)  /*simulation */
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_chip_agent_dump_packet_cmd);
#endif
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_chip_agent_show_debug_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_chip_agent_serverip_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_chip_agent_dump_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_chip_agent_dump_drvio_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_chip_agent_dump_error_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_chip_agent_clear_cmd);
     /*install_element(CTC_SDK_MODE, &ctc_cli_gg_chip_agent_send_pkt_cmd);*/
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_chip_agent_pkt_rx_print_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_chip_agent_pkt_rx_print_off_cmd);

    return CLI_SUCCESS;
}

#endif


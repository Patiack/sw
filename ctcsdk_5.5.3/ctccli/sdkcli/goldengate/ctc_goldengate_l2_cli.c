/**
 @file ctc_greatbel_l2_cli.c

 @date 2011-11-25

 @version v2.0

---file comments----
*/

/****************************************************************
 *
 * Header Files
 *
 ***************************************************************/
#include "sal.h"
#include "ctc_cli.h"
#include "ctc_cli_common.h"
#include "ctc_goldengate_l2_cli.h"
#include "ctc_api.h"
#include "ctcs_api.h"
#include "ctc_l2.h"
#include "ctc_error.h"
#include "ctc_debug.h"

#define CTC_L2_FDB_QUERY_ENTRY_NUM_DETAIL  100

extern int32 sys_goldengate_l2_show_status(uint8 lchip);
extern int32 sys_goldengate_l2_dump_default_entry(uint8 lchip, ctc_l2dflt_addr_t* l2dflt_addr);
extern int32 sys_goldengate_l2_dump_l2mc_entry(uint8 lchip, ctc_l2_mcast_addr_t* l2mc_addr);
extern int32 sys_goldengate_l2_flush_by_hw(uint8 lchip, ctc_l2_flush_fdb_t* p_flush, uint8 mac_limit_en);
extern int32 sys_goldengate_l2_dump_all_default_entry(uint8 lchip);
extern int32 sys_goldengate_l2_set_hw_learn(uint8 lchip, uint8 hw_en);
extern int32 sys_goldengate_l2_set_static_mac_limit(uint8 lchip, uint8 static_limit_en);
extern int32 sys_goldengate_l2_dump_fdb_entry_detail(uint8 lchip, ctc_l2_fdb_query_t* pq, uint16 entry_cnt, uint8 need_check, void* p_file);
extern int32 sys_goldengate_l2_dump_l2master(uint8 lchip);
extern int32 sys_goldengate_learning_aging_set_hw_sync(uint8 lchip, uint8 b_sync);
extern int32 sys_goldengate_fdb_sort_set_dump_task_time(uint8 lchip, uint32 time);
extern int32 sys_goldengate_l2_fdb_set_trie_sort(uint8 lchip, uint8 enable);

sal_task_t* ctc_goldengate_l2_write_fdb_detail_to_file = NULL;
void
ctc_goldengate_write_fdb_detail_to_file(void* user_param)
{
    sal_file_t fp = NULL;
    static ctc_l2_write_info_para_t write_entry_para;
    ctc_l2_fdb_query_t* query = NULL;
    uint16 entry_num = 0;
    uint8 lchip = 0;

    sal_memset(&write_entry_para, 0, sizeof(ctc_l2_write_info_para_t));

    sal_memcpy(&write_entry_para, user_param, sizeof(ctc_l2_write_info_para_t));
    mem_free(user_param);
    query = (ctc_l2_fdb_query_t*)write_entry_para.pdata;

    entry_num = (query->query_hw) ? CTC_L2_FDB_QUERY_ENTRY_NUM_DETAIL : 10;

    fp = sal_fopen((char*)write_entry_para.file, "w+");
    if (NULL == fp)
    {
        mem_free(write_entry_para.pdata);
        return;
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    sys_goldengate_l2_dump_fdb_entry_detail(lchip, query, entry_num, 1, (void*)fp);
    sal_fclose(fp);
    mem_free(write_entry_para.pdata);
    return;
}

CTC_CLI(ctc_cli_gg_l2_show_fdb_status,
        ctc_cli_gg_l2_show_fdb_status_cmd,
        "show l2 fdb status (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        CTC_CLI_L2_M_STR,
        CTC_CLI_FDB_DESC,
        "FDB Status",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{

    int32 ret  = CLI_SUCCESS;
    uint8 lchip = 0;
    uint8 index = 0;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_goldengate_l2_show_status(lchip);
    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;

}

CTC_CLI(ctc_cli_gg_l2_show_dflt,
        ctc_cli_gg_l2_show_dflt_cmd,
        "show l2 vlan-default-entry fid FID (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        CTC_CLI_L2_M_STR,
        "Vlan default entry",
        CTC_CLI_VLAN_DESC,
        CTC_CLI_FID_ID_DESC,
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret  = CLI_SUCCESS;
    uint8 lchip = 0;
    uint16 fid = 0;
    ctc_l2dflt_addr_t l2dflt_addr;
    uint8 index = 0;

    sal_memset(&l2dflt_addr, 0, sizeof(ctc_l2dflt_addr_t));

    CTC_CLI_GET_UINT16_RANGE("FID", fid, argv[0], 0, CTC_MAX_UINT16_VALUE);

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    l2dflt_addr.fid = fid;

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_goldengate_l2_dump_default_entry(lchip, &l2dflt_addr);
    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;
}

CTC_CLI(ctc_cli_gg_l2_show_dflt_all,
        ctc_cli_gg_l2_show_dflt_all_cmd,
        "show l2 vlan-default-entry all (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        CTC_CLI_L2_M_STR,
        "Vlan default entry",
        "All",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret  = CLI_SUCCESS;
    uint8 lchip = 0;
    uint8 index = 0;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_goldengate_l2_dump_all_default_entry(lchip);
    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;
}

CTC_CLI(ctc_cli_gg_l2_show_mcast,
        ctc_cli_gg_l2_show_mcast_cmd,
        "show l2 mcast mac MAC fid FID (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        CTC_CLI_L2_M_STR,
        "Mcast entry",
        CTC_CLI_MAC_DESC,
        CTC_CLI_MAC_FORMAT,
        CTC_CLI_VLAN_DESC,
        CTC_CLI_FID_ID_DESC,
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret  = CLI_SUCCESS;
    uint8 lchip = 0;
    ctc_l2_mcast_addr_t l2mc_addr;
    uint8 index = 0;

    /*1) parse MAC address */
    CTC_CLI_GET_MAC_ADDRESS("mac address", l2mc_addr.mac, argv[0]);

    CTC_CLI_GET_UINT16_RANGE("forwarding id", l2mc_addr.fid, argv[1], 0, CTC_MAX_UINT16_VALUE);

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_goldengate_l2_dump_l2mc_entry(lchip, &l2mc_addr);
    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;

}


CTC_CLI(ctc_cli_gg_l2_show_fdb_detail,
        ctc_cli_gg_l2_show_fdb_detail_cmd,
        "show l2 fdb detail by (mac MAC|mac-fid MAC FID|port GPORT_ID|fid FID|port-fid GPORT_ID FID|all ) ((static|dynamic|local-dynamic|mcast|conflict|all)|)(hw |)(entry-file FILE_NAME|) (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        CTC_CLI_L2_M_STR,
        CTC_CLI_FDB_DESC,
        "FDB detail information",
        "Query condition",
        CTC_CLI_MAC_DESC,
        CTC_CLI_MAC_FORMAT,
        "MAC+Fid",
        CTC_CLI_MAC_FORMAT,
        CTC_CLI_FID_ID_DESC,
        CTC_CLI_GPORT_DESC,
        CTC_CLI_GPORT_ID_DESC,
        CTC_CLI_FID_DESC,
        CTC_CLI_FID_ID_DESC,
        "Port+Fid",
        CTC_CLI_GPORT_ID_DESC,
        CTC_CLI_FID_ID_DESC,
        "All condition",
        "Static fdb table",
        "All dynamic fdb table",
        "Local chip dynamic fdb table",
        "Mcast entry",
        "Conflict fdb table",
        "Static and dynamic",
        "Dump by Hardware",
        "File store fdb entry",
        "File name",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{

    int32 ret  = CLI_SUCCESS;
    uint8 lchip = 0;
    uint8 index = 0;
    ctc_l2_fdb_query_t query;
    ctc_l2_write_info_para_t* write_entry_para = NULL;
    uint8 is_hw = 0;
    uint16 entry_cnt = 0;
    char buffer[SAL_TASK_MAX_NAME_LEN] = {0};

    sal_memset(&query, 0, sizeof(ctc_l2_fdb_query_t));

    query.query_type = CTC_L2_FDB_ENTRY_OP_ALL;
    query.query_flag = CTC_L2_FDB_ENTRY_ALL;

    /*1) parse MAC address */
     /*index = ctc_cli_get_prefix_item(&argv[0], 1, "mac", 3);*/
    index = CTC_CLI_GET_ARGC_INDEX("mac");
    if (index != 0xFF)
    {
        query.query_type = CTC_L2_FDB_ENTRY_OP_BY_MAC;

        if (sal_sscanf(argv[index + 1], "%4hx.%4hx.%4hx", (unsigned short*)&query.mac[0],
                       (unsigned short*)&query.mac[2], (unsigned short*)&query.mac[4]) != 3)
        {
            ctc_cli_out("%% Unable to translate mac address %s\n", argv[index + 1]);
            return CLI_ERROR;
        }

        /* Convert to network order */
        *(unsigned short*)&query.mac[0] = sal_htons(*(unsigned short*)&query.mac[0]);
        *(unsigned short*)&query.mac[2] = sal_htons(*(unsigned short*)&query.mac[2]);
        *(unsigned short*)&query.mac[4] = sal_htons(*(unsigned short*)&query.mac[4]);
    }

	/* parse MAC+FID address */
	index = CTC_CLI_GET_ARGC_INDEX("mac-fid");
	if (index != 0xFF)
    {
        query.query_type = CTC_L2_FDB_ENTRY_OP_BY_MAC_VLAN;

        if (sal_sscanf(argv[index + 1], "%4hx.%4hx.%4hx", (unsigned short*)&query.mac[0],
                       (unsigned short*)&query.mac[2], (unsigned short*)&query.mac[4]) != 3)
        {
            ctc_cli_out("%% Unable to translate mac address %s\n", argv[index + 1]);
            return CLI_ERROR;
        }

        /* Convert to network order */
        *(unsigned short*)&query.mac[0] = sal_htons(*(unsigned short*)&query.mac[0]);
        *(unsigned short*)&query.mac[2] = sal_htons(*(unsigned short*)&query.mac[2]);
        *(unsigned short*)&query.mac[4] = sal_htons(*(unsigned short*)&query.mac[4]);

        CTC_CLI_GET_UINT16_RANGE("forwarding id", query.fid, argv[index + 2], 0, CTC_MAX_UINT16_VALUE);
    }

    /*2) parse PORT address */
    index = ctc_cli_get_prefix_item(&argv[0], 1, "port", 4);
    if (index != 0xFF)
    {
        query.query_type = CTC_L2_FDB_ENTRY_OP_BY_PORT;
        CTC_CLI_GET_UINT16_RANGE("gport", query.gport, argv[index + 1], 0, CTC_MAX_UINT16_VALUE);
    }

    /*3) parse fid address*/
    index = ctc_cli_get_prefix_item(&argv[0], 1, "fid", 3);
    if (index != 0xFF)
    {
        query.query_type = CTC_L2_FDB_ENTRY_OP_BY_VID;
        CTC_CLI_GET_UINT16_RANGE("forwarding id", query.fid, argv[index + 1], 0, CTC_MAX_UINT16_VALUE);
    }

    /*4)port+fid*/
    index = CTC_CLI_GET_ARGC_INDEX("port-fid");
    if (index != 0xFF)
    {
        query.query_type = CTC_L2_FDB_ENTRY_OP_BY_PORT_VLAN;
        CTC_CLI_GET_UINT16_RANGE("gport", query.gport, argv[index + 1], 0, CTC_MAX_UINT16_VALUE);
        CTC_CLI_GET_UINT16_RANGE("fid", query.fid, argv[index + 2], 0, CTC_MAX_UINT16_VALUE);
    }

    /*5) parse static|dynamic */
    index = CTC_CLI_GET_ARGC_INDEX("static");
    if (index != 0xFF)
    {
        query.query_flag = CTC_L2_FDB_ENTRY_STATIC;
    }

    index = CTC_CLI_GET_ARGC_INDEX("dynamic");
    if (index != 0xFF)
    {
        query.query_flag = CTC_L2_FDB_ENTRY_DYNAMIC;
    }

    index = CTC_CLI_GET_ARGC_INDEX("local-dynamic");
    if (index != 0xFF)
    {
        query.query_flag = CTC_L2_FDB_ENTRY_LOCAL_DYNAMIC;
    }

    index = CTC_CLI_GET_ARGC_INDEX("mcast");
    if (index != 0xFF)
    {
        query.query_flag = CTC_L2_FDB_ENTRY_MCAST;
    }
    index = CTC_CLI_GET_ARGC_INDEX("conflict");
    if (index != 0xFF)
    {
        query.query_flag = CTC_L2_FDB_ENTRY_CONFLICT;
    }

    index = CTC_CLI_GET_ARGC_INDEX("hw");
    if (index != 0xFF)
    {
        is_hw = 1;
        query.query_hw = 1;
    }

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    index = CTC_CLI_GET_ARGC_INDEX("entry-file");
    if (index != 0xFF)
    {
        MALLOC_ZERO(MEM_CLI_MODULE, write_entry_para, sizeof(ctc_l2_write_info_para_t));
        sal_strcpy((char*)&write_entry_para->file, argv[index + 1]);
    }

    if (write_entry_para && *write_entry_para->file)
    {
        /* free memery will be done in ctc_goldengate_write_fdb_detail_to_file */
        write_entry_para->pdata = (ctc_l2_fdb_query_t*)mem_malloc(MEM_CLI_MODULE, sizeof(ctc_l2_fdb_query_t));
        if (NULL == write_entry_para->pdata)
        {
            mem_free(write_entry_para);
            return CLI_ERROR;
        }

        sal_memcpy(write_entry_para->pdata, &query, sizeof(ctc_l2_fdb_query_t));
        sal_sprintf(buffer, "ctcFdbWFl-%d", lchip);
        if (0 != sal_task_create(&ctc_goldengate_l2_write_fdb_detail_to_file,
                                 buffer,
                                 0, SAL_TASK_PRIO_DEF, ctc_goldengate_write_fdb_detail_to_file, write_entry_para))
        {
            sal_task_destroy(ctc_goldengate_l2_write_fdb_detail_to_file);
            mem_free(write_entry_para->pdata);
            mem_free(write_entry_para);
            return CLI_ERROR;
        }

    }
    else
    {
        entry_cnt = is_hw?CTC_L2_FDB_QUERY_ENTRY_NUM_DETAIL:10;

        lchip = g_ctcs_api_en?g_api_lchip:lchip;
        ret = sys_goldengate_l2_dump_fdb_entry_detail(lchip, &query, entry_cnt, 1, NULL);
        if (ret < 0)
        {
            ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
            if (write_entry_para)
            {
                mem_free(write_entry_para);
            }
            return CLI_ERROR;
        }
    }

    if (write_entry_para)
    {
        mem_free(write_entry_para);
    }

    return ret;

}


CTC_CLI(ctc_cli_gg_l2_show_master,
        ctc_cli_gg_l2_show_master_cmd,
        "show l2 fdb-master (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        CTC_CLI_L2_M_STR,
        "master data",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret  = CLI_SUCCESS;
    uint8 lchip = 0;
    uint8 index = 0;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_goldengate_l2_dump_l2master(lchip);
    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;

}
CTC_CLI(ctc_cli_gg_l2_flush_fdb,
        ctc_cli_gg_l2_flush_fdb_cmd,
        "l2 fdb flush by (port GPORT_ID|fid FID|port-fid GPORT_ID FID|all) (is-logic-port|) (static|dynamic|all) hw (lchip LCHIP|)",
        CTC_CLI_L2_M_STR,
        CTC_CLI_FDB_DESC,
        "Flush fdb",
        "Query condition",
        CTC_CLI_GPORT_DESC,
        CTC_CLI_GPORT_ID_DESC,
        CTC_CLI_FID_DESC,
        CTC_CLI_FID_ID_DESC,
        "Port+FID",
        CTC_CLI_GPORT_ID_DESC,
        CTC_CLI_FID_ID_DESC,
        "All condition",
        "Is logic port",
        "Static fdb table",
        "All dynamic fdb table",
        "Static and dynamic",
        "Hw mode",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)

{
    uint8 index   = 0;
    int32    ret  = CLI_SUCCESS;
    uint8 lchip = 0;
    ctc_l2_flush_fdb_t Flush;

    sal_memset(&Flush, 0, sizeof(ctc_l2_flush_fdb_t));

    Flush.flush_type = CTC_L2_FDB_ENTRY_OP_ALL;
    Flush.flush_flag = CTC_L2_FDB_ENTRY_ALL;

    /*1) parse MAC address */

    index = CTC_CLI_GET_ARGC_INDEX("mac");
    if (index != 0xFF)
    {
        Flush.flush_type = CTC_L2_FDB_ENTRY_OP_BY_MAC;
        index++;
        CTC_CLI_GET_MAC_ADDRESS("mac address", Flush.mac, argv[index]);

    }

    /*2) parse PORT address */

    index = CTC_CLI_GET_ARGC_INDEX("port");
    if (index != 0xFF)
    {
        Flush.flush_type = CTC_L2_FDB_ENTRY_OP_BY_PORT;
        index++;
        CTC_CLI_GET_UINT16_RANGE("gport", Flush.gport, argv[index], 0, CTC_MAX_UINT16_VALUE);
    }

    /*3)parse fid */

    index = CTC_CLI_GET_ARGC_INDEX("fid");
    if (index != 0xFF)
    {
        Flush.flush_type = CTC_L2_FDB_ENTRY_OP_BY_VID;
        index++;
        CTC_CLI_GET_UINT16_RANGE("vlan id",  Flush.fid, argv[index], 0, CTC_MAX_UINT16_VALUE);
    }

    /*4)port+fid*/
    index = CTC_CLI_GET_ARGC_INDEX("port-fid");
    if (index != 0xFF)
    {
        Flush.flush_type = CTC_L2_FDB_ENTRY_OP_BY_PORT_VLAN;
        CTC_CLI_GET_UINT16_RANGE("gport", Flush.gport, argv[index + 1], 0, CTC_MAX_UINT16_VALUE);
        CTC_CLI_GET_UINT16_RANGE("fid", Flush.fid, argv[index + 2], 0, CTC_MAX_UINT16_VALUE);
    }

    /*5) parse static|dynamic */
    index = CTC_CLI_GET_ARGC_INDEX("static");
    if (index != 0xFF)
    {
        Flush.flush_flag = CTC_L2_FDB_ENTRY_STATIC;
    }

    index = CTC_CLI_GET_ARGC_INDEX("dynamic");
    if (index != 0xFF)
    {
        Flush.flush_flag = CTC_L2_FDB_ENTRY_DYNAMIC;
    }

    index = CTC_CLI_GET_ARGC_INDEX("local-dynamic");
    if (index != 0xFF)
    {
        Flush.flush_flag = CTC_L2_FDB_ENTRY_LOCAL_DYNAMIC;
    }

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_goldengate_l2_flush_by_hw(lchip, &Flush, 1);
    if (ret < 0)
    {
        ctc_cli_out("%% %s \n",  ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;

}

CTC_CLI(ctc_cli_gg_l2_show_fdb_detail_hw,
        ctc_cli_gg_l2_show_fdb_detail_hw_cmd,
        "show l2 fdb detail by (mac MAC|mac-fid MAC FID|port GPORT_ID|fid FID|port-fid GPORT_ID FID|all ) ((static|dynamic|local-dynamic|conflict|all)|) (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        CTC_CLI_L2_M_STR,
        CTC_CLI_FDB_DESC,
        "FDB detail information",
        "Query condition",
        CTC_CLI_MAC_DESC,
        CTC_CLI_MAC_FORMAT,
        "MAC+Fid",
        CTC_CLI_MAC_FORMAT,
        CTC_CLI_FID_ID_DESC,
        CTC_CLI_GPORT_DESC,
        CTC_CLI_GPORT_ID_DESC,
        CTC_CLI_FID_DESC,
        CTC_CLI_FID_ID_DESC,
        "Port+Fid",
        CTC_CLI_GPORT_ID_DESC,
        CTC_CLI_FID_ID_DESC,
        "All condition",
        "Static fdb table",
        "All dynamic fdb table",
        "Local chip dynamic fdb table",
        "Conflict fdb table",
        "Static and dynamic",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{

    int32 ret  = CLI_SUCCESS;
    uint8 lchip = 0;
    uint8 index = 0;
    ctc_l2_fdb_query_t query;
    uint16 entry_cnt = 0;

    sal_memset(&query, 0, sizeof(ctc_l2_fdb_query_t));

    query.query_type = CTC_L2_FDB_ENTRY_OP_ALL;
    query.query_flag = CTC_L2_FDB_ENTRY_ALL;

    /*1) parse MAC address */
     /*index = ctc_cli_get_prefix_item(&argv[0], 1, "mac", 3);*/
    index = CTC_CLI_GET_ARGC_INDEX("mac");
    if (index != 0xFF)
    {
        query.query_type = CTC_L2_FDB_ENTRY_OP_BY_MAC;

        if (sal_sscanf(argv[index + 1], "%4hx.%4hx.%4hx", (unsigned short*)&query.mac[0],
                       (unsigned short*)&query.mac[2], (unsigned short*)&query.mac[4]) != 3)
        {
            ctc_cli_out("%% Unable to translate mac address %s\n", argv[index + 1]);
            return CLI_ERROR;
        }

        /* Convert to network order */
        *(unsigned short*)&query.mac[0] = sal_htons(*(unsigned short*)&query.mac[0]);
        *(unsigned short*)&query.mac[2] = sal_htons(*(unsigned short*)&query.mac[2]);
        *(unsigned short*)&query.mac[4] = sal_htons(*(unsigned short*)&query.mac[4]);
    }

	/* parse MAC+FID address */
	index = CTC_CLI_GET_ARGC_INDEX("mac-fid");
	if (index != 0xFF)
    {
        query.query_type = CTC_L2_FDB_ENTRY_OP_BY_MAC_VLAN;

        if (sal_sscanf(argv[index + 1], "%4hx.%4hx.%4hx", (unsigned short*)&query.mac[0],
                       (unsigned short*)&query.mac[2], (unsigned short*)&query.mac[4]) != 3)
        {
            ctc_cli_out("%% Unable to translate mac address %s\n", argv[index + 1]);
            return CLI_ERROR;
        }

        /* Convert to network order */
        *(unsigned short*)&query.mac[0] = sal_htons(*(unsigned short*)&query.mac[0]);
        *(unsigned short*)&query.mac[2] = sal_htons(*(unsigned short*)&query.mac[2]);
        *(unsigned short*)&query.mac[4] = sal_htons(*(unsigned short*)&query.mac[4]);

        CTC_CLI_GET_UINT16_RANGE("forwarding id", query.fid, argv[index + 2], 0, CTC_MAX_UINT16_VALUE);
    }

    /*2) parse PORT address */
    index = ctc_cli_get_prefix_item(&argv[0], 1, "port", 4);
    if (index != 0xFF)
    {
        query.query_type = CTC_L2_FDB_ENTRY_OP_BY_PORT;
        CTC_CLI_GET_UINT16_RANGE("gport", query.gport, argv[index + 1], 0, CTC_MAX_UINT16_VALUE);
    }

    /*3) parse fid address*/
    index = ctc_cli_get_prefix_item(&argv[0], 1, "fid", 3);
    if (index != 0xFF)
    {
        query.query_type = CTC_L2_FDB_ENTRY_OP_BY_VID;
        CTC_CLI_GET_UINT16_RANGE("forwarding id", query.fid, argv[index + 1], 0, CTC_MAX_UINT16_VALUE);
    }

    /*4)port+fid*/
    index = CTC_CLI_GET_ARGC_INDEX("port-fid");
    if (index != 0xFF)
    {
        query.query_type = CTC_L2_FDB_ENTRY_OP_BY_PORT_VLAN;
        CTC_CLI_GET_UINT16_RANGE("gport", query.gport, argv[index + 1], 0, CTC_MAX_UINT16_VALUE);
        CTC_CLI_GET_UINT16_RANGE("fid", query.fid, argv[index + 2], 0, CTC_MAX_UINT16_VALUE);
    }

    /*5) parse static|dynamic */
    index = CTC_CLI_GET_ARGC_INDEX("static");
    if (index != 0xFF)
    {
        query.query_flag = CTC_L2_FDB_ENTRY_STATIC;
    }

    index = CTC_CLI_GET_ARGC_INDEX("dynamic");
    if (index != 0xFF)
    {
        query.query_flag = CTC_L2_FDB_ENTRY_DYNAMIC;
    }

    index = CTC_CLI_GET_ARGC_INDEX("local-dynamic");
    if (index != 0xFF)
    {
        query.query_flag = CTC_L2_FDB_ENTRY_LOCAL_DYNAMIC;
    }
    index = CTC_CLI_GET_ARGC_INDEX("conflict");
    if (index != 0xFF)
    {
        query.query_flag = CTC_L2_FDB_ENTRY_CONFLICT;
    }

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    query.query_hw = 1;
    entry_cnt = 100;


    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_goldengate_l2_dump_fdb_entry_detail(lchip, &query, entry_cnt, 0, NULL);
    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;

}
CTC_CLI(ctc_cli_gg_l2_fdb_hw_learn,
        ctc_cli_gg_l2_fdb_hw_learn_cmd,
        "l2 fdb hw-learn (enable | disable) (lchip LCHIP|)",
        CTC_CLI_L2_M_STR,
        CTC_CLI_FDB_DESC,
        "Force MAC Learning to Hw-learning",
        CTC_CLI_ENABLE,
        CTC_CLI_DISABLE,
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret  = CLI_SUCCESS;
    uint8 lchip = 0;
    uint8 hw_en = 0;
    uint8 index = 0xFF;

    index = CTC_CLI_GET_ARGC_INDEX("enable");
    if( 0xFF != index )
    {
        hw_en = 1;
    }

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_goldengate_l2_set_hw_learn(lchip, hw_en);
    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;

}


CTC_CLI(ctc_cli_gg_l2_fdb_misc,
        ctc_cli_gg_l2_fdb_misc_cmd,
        "l2 fdb (static-mac-limit | hw-learn-sync) (enable | disable) (lchip LCHIP|)",
        CTC_CLI_L2_M_STR,
        CTC_CLI_FDB_DESC,
        "Static mac limit",
        "Hardware Mac learning sync",
        CTC_CLI_ENABLE,
        CTC_CLI_DISABLE,
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret  = CLI_SUCCESS;
    uint8 lchip = 0;
    uint8 static_fdb_limit = 0;
    uint8 b_enable = 0;
    uint8 index = 0xFF;

    index = CTC_CLI_GET_ARGC_INDEX("static-mac-limit");
    if( 0xFF != index )
    {
        static_fdb_limit = 1;
    }

    index = CTC_CLI_GET_ARGC_INDEX("enable");
    if( 0xFF != index )
    {
        b_enable = 1;
    }

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    if (static_fdb_limit)
    {
        ret = sys_goldengate_l2_set_static_mac_limit(lchip, b_enable);
    }
    else
    {
        ret = sys_goldengate_learning_aging_set_hw_sync(lchip, b_enable);
    }

    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;

}

CTC_CLI(ctc_cli_gg_l2_fdb_set_dump_task_time,
        ctc_cli_gg_l2_fdb_set_dump_task_time_cmd,
        "l2 fdb dump-time TIME (lchip LCHIP|)",
        CTC_CLI_L2_M_STR,
        CTC_CLI_FDB_DESC,
        "dump entries time",
        "granularity: 10s, minimum 10s",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    uint8 lchip = 0;
    uint32 time = 0;
    uint8 index = 0xFF;

    CTC_CLI_GET_UINT32("dump-time", time, argv[0]);

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;

    sys_goldengate_fdb_sort_set_dump_task_time(lchip, time);

    return CLI_SUCCESS;

}

CTC_CLI(ctc_cli_gg_l2_fdb_trie_sort,
        ctc_cli_gg_l2_fdb_trie_sort_cmd,
        "l2 fdb trie-sort (enable |disable) (lchip LCHIP|)",
        CTC_CLI_L2_M_STR,
        CTC_CLI_FDB_DESC,
        "Trie sort",
        "enable",
        "disable",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    uint8 lchip = 0;
    uint8 enable = 0;
    uint8 index = 0xFF;

    index = CTC_CLI_GET_ARGC_INDEX("enable");
    if (0xFF != index)
    {
        enable = 1;
    }
    else
    {
        enable = 0;
    }

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;

    sys_goldengate_l2_fdb_set_trie_sort(lchip, enable);

    return CLI_SUCCESS;

}


int32
ctc_goldengate_l2_cli_init(void)
{

    install_element(CTC_SDK_MODE, &ctc_cli_gg_l2_show_fdb_status_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gg_l2_show_dflt_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gg_l2_show_dflt_all_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gg_l2_show_mcast_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gg_l2_show_fdb_detail_cmd);
    install_element(CTC_INTERNAL_MODE, &ctc_cli_gg_l2_flush_fdb_cmd);
    install_element(CTC_INTERNAL_MODE, &ctc_cli_gg_l2_show_master_cmd);
    install_element(CTC_INTERNAL_MODE, &ctc_cli_gg_l2_show_fdb_detail_hw_cmd);
    install_element(CTC_INTERNAL_MODE, &ctc_cli_gg_l2_fdb_hw_learn_cmd);
    install_element(CTC_INTERNAL_MODE, &ctc_cli_gg_l2_fdb_misc_cmd);
    install_element(CTC_INTERNAL_MODE, &ctc_cli_gg_l2_fdb_set_dump_task_time_cmd);
    install_element(CTC_INTERNAL_MODE, &ctc_cli_gg_l2_fdb_trie_sort_cmd);


    return 0;
}

int32
ctc_goldengate_l2_cli_deinit(void)
{

    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_l2_show_fdb_status_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_l2_show_dflt_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_l2_show_dflt_all_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_l2_show_mcast_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_l2_show_fdb_detail_cmd);
    uninstall_element(CTC_INTERNAL_MODE, &ctc_cli_gg_l2_flush_fdb_cmd);
    uninstall_element(CTC_INTERNAL_MODE, &ctc_cli_gg_l2_show_master_cmd);
    uninstall_element(CTC_INTERNAL_MODE, &ctc_cli_gg_l2_show_fdb_detail_hw_cmd);
    uninstall_element(CTC_INTERNAL_MODE, &ctc_cli_gg_l2_fdb_hw_learn_cmd);
    uninstall_element(CTC_INTERNAL_MODE, &ctc_cli_gg_l2_fdb_misc_cmd);


    return 0;
}


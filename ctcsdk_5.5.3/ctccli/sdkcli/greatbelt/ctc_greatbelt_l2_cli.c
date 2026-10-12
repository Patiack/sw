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
#include "ctc_greatbelt_l2_cli.h"
#include "ctc_api.h"
#include "ctcs_api.h"
#include "ctc_l2.h"
#include "ctc_error.h"
#include "ctc_debug.h"
#include "sys_greatbelt_chip.h"
#include "sys_greatbelt_l2_fdb.h"

#define CTC_L2_FDB_QUERY_COUNT      500
#define CTC_L2_FDB_QUERY_INTERVAL   10

extern int32
sys_greatbelt_show_l2_fdb_status(void);
extern int32
sys_greatbelt_l2_dump_default_entry(uint8 lchip, ctc_l2dflt_addr_t* l2dflt_addr);
extern int32
sys_greatbelt_l2_dump_l2mc_entry(uint8 lchip, ctc_l2_mcast_addr_t* l2mc_addr);

extern int32
sys_greatbelt_aging_get_aging_status(uint8 lchip, uint32 domain_type, uint32 key_index, uint8 * status);

extern int32
sys_greatbelt_l2_get_fdb_entry_detail(uint8 lchip, ctc_l2_fdb_query_t* pQuery,  ctc_l2_fdb_query_rst_t* query_rst, sys_l2_detail_t* fdb_detail_info);

sal_task_t* ctc_greatbelt_l2_write_fdb_detail_to_file = NULL;
sal_task_t* ctc_greatbelt_l2_write_fdb_hw_to_file = NULL;

extern int32
sys_greatbelt_l2_dump_l2master(uint8 lchip);
extern int32
sys_greatbelt_l2_set_l2_master(uint8 lchip, int8 flush_by_hw, int8 aging_by_hw);

extern int32
sys_greatbelt_l2_set_hw_learn(uint8 lchip, uint8 hw_en);

extern int32
sys_greatbelt_fdb_sort_set_dump_task_time(uint8 lchip, uint32 time);

extern int32
sys_greatbelt_fdb_sort_set_mac_limit_en(uint8 lchip, uint8 limit_en, uint8 is_remove);
extern  int32
sys_greatbelt_l2_fdb_set_trie_sort(uint8 lchip, uint8 enable);
void
ctc_greatbelt_write_fdb_detail_to_file(void* user_param)
{
    int32 ret = 0;
    uint8 index = 0;
    uint32 total_count = 0;
    uint8 lchip = 0;
    sal_file_t fp = NULL;
    ctc_l2_fdb_query_rst_t query_rst;
    ctc_l2_write_info_para_t write_entry_para;
    sys_l2_detail_t* fdb_detail_info;
    uint16 lport = 0;
    char* char_buffer = NULL;
    ctc_l2_fdb_query_t* query = NULL;

    sal_memset(&query_rst, 0, sizeof(ctc_l2_fdb_query_rst_t));
    sal_memset(&write_entry_para, 0, sizeof(ctc_l2_write_info_para_t));

    /* alloc memory for detail struct */
    fdb_detail_info = (sys_l2_detail_t*)mem_malloc(MEM_CLI_MODULE, sizeof(sys_l2_detail_t) * CTC_L2_FDB_QUERY_COUNT);
    if (NULL == fdb_detail_info)
    {
        ctc_cli_out("%% Alloc  memory  failed \n");
        return;
    }

    sal_memset(fdb_detail_info, 0, sizeof(sys_l2_detail_t));

    sal_memcpy(&write_entry_para, user_param, sizeof(ctc_l2_write_info_para_t));
    query = (ctc_l2_fdb_query_t*)write_entry_para.pdata;

    query_rst.buffer_len = sizeof(ctc_l2_addr_t) * CTC_L2_FDB_QUERY_COUNT;
    query_rst.buffer = (ctc_l2_addr_t*)mem_malloc(MEM_CLI_MODULE, query_rst.buffer_len);
    if (NULL == query_rst.buffer)
    {
        ctc_cli_out("%% Alloc  memory  failed \n");
        mem_free(fdb_detail_info);
        fdb_detail_info = NULL;
        mem_free(write_entry_para.pdata);
        write_entry_para.pdata = NULL;
        return;
    }

    fp = sal_fopen((char*)write_entry_para.file, "w+");
    if (NULL == fp)
    {
        mem_free(fdb_detail_info);
        fdb_detail_info = NULL;
        mem_free(query_rst.buffer);
        query_rst.buffer = NULL;
        mem_free(write_entry_para.pdata);
        write_entry_para.pdata = NULL;
        return;
    }

    char_buffer = (char*)mem_malloc(MEM_CLI_MODULE, 128);
    if (NULL == char_buffer)
    {
        mem_free(fdb_detail_info);
        fdb_detail_info = NULL;
        mem_free(query_rst.buffer);
        query_rst.buffer = NULL;
        mem_free(write_entry_para.pdata);
        write_entry_para.pdata = NULL;
        ctc_cli_out("%% Alloc  memory  failed \n");
        sal_fclose(fp);
        fp = NULL;
        return;
    }

    ctc_cli_out("Please read entry-file after seeing \"FDB entry have been writen!\"\n");
    sal_fprintf(fp, "%8s  %8s  %8s  %11s    %4s   %4s  %6s  %6s\n", "KEY-INDEX", "AD-INDEX", "MAC", "FID", "GPORT", "LPORT", "Static", "HASH/TCAM");
    sal_fprintf(fp, "------------------------------------------------------------------------------\n");

    do
    {
        query_rst.start_index = query_rst.next_query_index;
        lchip = g_ctcs_api_en?g_api_lchip:lchip;
        ret = sys_greatbelt_l2_get_fdb_entry_detail(lchip, query, &query_rst, fdb_detail_info);
        if (ret < 0)
        {
            ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
            mem_free(query_rst.buffer);
            query_rst.buffer = NULL;
            mem_free(fdb_detail_info);
            fdb_detail_info = NULL;
            mem_free(char_buffer);
            char_buffer = NULL;
            mem_free(write_entry_para.pdata);
            write_entry_para.pdata = NULL;
            sal_fclose(fp);
            fp = NULL;
            return;
        }

        for (index = 0; index < query->count; index++)
        {
            sal_sprintf(char_buffer, "0x%-8x", fdb_detail_info[index].key_index);
            sal_sprintf((char_buffer + sal_strlen(char_buffer)), "0x%-8x", fdb_detail_info[index].ad_index);
            sal_sprintf((char_buffer + sal_strlen(char_buffer)), "%.4x.%.4x.%.4x%4s ", sal_ntohs(*(unsigned short*)&query_rst.buffer[index].mac[0]),
                        sal_ntohs(*(unsigned short*)&query_rst.buffer[index].mac[2]),
                        sal_ntohs(*(unsigned short*)&query_rst.buffer[index].mac[4]),
                        " ");

            sal_sprintf((char_buffer + sal_strlen(char_buffer)), "%.4d  ", query_rst.buffer[index].fid);

            sal_sprintf((char_buffer + sal_strlen(char_buffer)), "0x%.4x  ", query_rst.buffer[index].gport);
            lport = query_rst.buffer[index].gport;
            sal_sprintf((char_buffer + sal_strlen(char_buffer)), "0x%.4x    ", (lport & 0x00ff));
            sal_sprintf((char_buffer + sal_strlen(char_buffer)), "%3s     ", (fdb_detail_info[index].flag.flag_ad.is_static) ? "Yes" : "No");
            sal_sprintf((char_buffer + sal_strlen(char_buffer)), "%s\n", (fdb_detail_info[index].flag.flag_node.is_tcam) ? "Tcam" : "Hash");
            sal_fprintf(fp, "%s", char_buffer);
            sal_memset(&fdb_detail_info[index], 0, sizeof(sys_l2_detail_t));
        }

        total_count += query->count;
        sal_task_sleep(CTC_L2_FDB_QUERY_INTERVAL);

    }
    while (query_rst.is_end == 0);

    sal_fprintf(fp, "-------------------------------------------\n");
    sal_fprintf(fp, "Total Entry Num: %d\n", total_count);

    sal_fclose(fp);
    ctc_cli_out("FDB entry have been writen\n");

    mem_free(query_rst.buffer);
    query_rst.buffer = NULL;
    mem_free(char_buffer);
    char_buffer = NULL;
    mem_free(fdb_detail_info);
    fdb_detail_info = NULL;
    mem_free(write_entry_para.pdata);
    write_entry_para.pdata = NULL;

}

void
ctc_greatbelt_write_fdb_hw_to_file(void* user_param)
{
    int32 ret = 0;
    uint32 index = 0;
    uint32 total_count = 0;
    sal_file_t fp = NULL;
    ctc_l2_fdb_query_rst_t query_rst;
    ctc_l2_write_info_para_t write_entry_para;
    char char_buffer[128];
    ctc_l2_fdb_query_t* query = NULL;

    sal_memset(&query_rst, 0, sizeof(ctc_l2_fdb_query_rst_t));
    sal_memset(&write_entry_para, 0, sizeof(ctc_l2_write_info_para_t));

    sal_memcpy(&write_entry_para, user_param, sizeof(ctc_l2_write_info_para_t));
    query = (ctc_l2_fdb_query_t*)write_entry_para.pdata;

    query_rst.buffer_len = sizeof(ctc_l2_addr_t) * CTC_L2_FDB_QUERY_COUNT;
    query_rst.buffer = (ctc_l2_addr_t*)mem_malloc(MEM_CLI_MODULE, query_rst.buffer_len);
    if (NULL == query_rst.buffer)
    {
        mem_free(write_entry_para.pdata);
        write_entry_para.pdata = NULL;
        ctc_cli_out("%% Alloc  memory  failed \n");
        return;
    }

    fp = sal_fopen((char*)write_entry_para.file, "w+");
    if (NULL == fp)
    {
        mem_free(query_rst.buffer);
        query_rst.buffer = NULL;
        mem_free(write_entry_para.pdata);
        write_entry_para.pdata = NULL;
        return;
    }

    ctc_cli_out("Please read entry-file after seeing \"FDB entry have been writen!\"\n");
    sal_fprintf(fp, "FDB HW table\n");
    sal_fprintf(fp, "%-15s %-5s %-6s %-7s\n", "MAC", "FID", "GPORT", "Static");
    sal_fprintf(fp, "------------------------------------\n");

    do
    {
        query_rst.start_index = query_rst.next_query_index;
        if(g_ctcs_api_en)
        {
            ret = ctcs_l2_get_fdb_entry(g_api_lchip, query, &query_rst);
        }
        else
        {
            ret = ctc_l2_get_fdb_entry(query, &query_rst);
        }
        if (ret < 0)
        {
            ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
            mem_free(query_rst.buffer);
            query_rst.buffer = NULL;
            mem_free(write_entry_para.pdata);
            write_entry_para.pdata = NULL;
            sal_fclose(fp);
            fp = NULL;
            return;
        }

        for (index = 0; index < query->count; index++)
        {
            sal_sprintf(char_buffer, "%.4x.%.4x.%.4x  %-5d 0x%.4x %-7s\n",
                sal_ntohs(*(unsigned short*)&query_rst.buffer[index].mac[0]),
                sal_ntohs(*(unsigned short*)&query_rst.buffer[index].mac[2]),
                sal_ntohs(*(unsigned short*)&query_rst.buffer[index].mac[4]),
                query_rst.buffer[index].fid,
                query_rst.buffer[index].gport,
                (query_rst.buffer[index].flag & CTC_L2_FLAG_IS_STATIC) ? "Yes" : "No"
                );
            sal_fprintf(fp, "%s", char_buffer);
            sal_memset(&query_rst.buffer[index], 0, sizeof(ctc_l2_addr_t));
        }

        total_count += query->count;
        sal_task_sleep(CTC_L2_FDB_QUERY_INTERVAL);

    }
    while (query_rst.is_end == 0);



    sal_fprintf(fp, "------------------------------------\n");
    sal_fprintf(fp, "Total Entry Num: %d\n", total_count);

    sal_fclose(fp);
    ctc_cli_out("FDB entry have been writen\n");

    mem_free(query_rst.buffer);
    query_rst.buffer = NULL;
    mem_free(write_entry_para.pdata);
    write_entry_para.pdata = NULL;
    mem_free(user_param);
}

void
ctc_greatbelt_write_fdb_hw_detail_to_file(void* user_param)
{
    int32 ret = 0;
    uint32 index = 0;
    uint32 total_count = 0;
    uint8 lchip = 0;
    sal_file_t fp = NULL;
    ctc_l2_fdb_query_rst_t query_rst;
    ctc_l2_write_info_para_t write_entry_para;
    sys_l2_detail_t* detail_info = NULL;
    uint16 lport = 0;
    char char_buffer[128];
    ctc_l2_fdb_query_t* query = NULL;

    sal_memset(&query_rst, 0, sizeof(ctc_l2_fdb_query_rst_t));
    sal_memset(&write_entry_para, 0, sizeof(ctc_l2_write_info_para_t));

    /* alloc memory for detail struct */
    detail_info = (sys_l2_detail_t*)mem_malloc(MEM_CLI_MODULE, sizeof(sys_l2_detail_t) * CTC_L2_FDB_QUERY_COUNT);
    if (NULL == detail_info)
    {
        ctc_cli_out("%% Alloc  memory  failed \n");
        return;
    }

    sal_memset(detail_info, 0, sizeof(sys_l2_detail_t));

    sal_memcpy(&write_entry_para, user_param, sizeof(ctc_l2_write_info_para_t));
    query = (ctc_l2_fdb_query_t*)write_entry_para.pdata;

    query_rst.buffer_len = sizeof(ctc_l2_addr_t) * CTC_L2_FDB_QUERY_COUNT;
    query_rst.buffer = (ctc_l2_addr_t*)mem_malloc(MEM_CLI_MODULE, query_rst.buffer_len);
    if (NULL == query_rst.buffer)
    {
        ctc_cli_out("%% Alloc  memory  failed \n");
        mem_free(detail_info);
        detail_info = NULL;
        mem_free(write_entry_para.pdata);
        write_entry_para.pdata = NULL;
        return;
    }

    fp = sal_fopen((char*)write_entry_para.file, "w+");
    if ((NULL == fp))
    {
        mem_free(detail_info);
        detail_info = NULL;
        mem_free(query_rst.buffer);
        query_rst.buffer = NULL;
        mem_free(write_entry_para.pdata);
        write_entry_para.pdata = NULL;
        return;
    }

    ctc_cli_out("Please read entry-file after seeing \"FDB entry have been writen!\"\n");
    sal_fprintf(fp, "FDB HW table\n");
    sal_fprintf(fp, "%-9s %-8s %-15s %-5s %-6s %-6s %-7s %-8s\n", "KEY-INDEX", "AD-INDEX", "MAC", "FID", "GPORT", "LPORT", "Static", "HASH/TCAM");
    sal_fprintf(fp, "------------------------------------------------------------------------\n");

    do
    {
        query_rst.start_index = query_rst.next_query_index;
        lchip = g_ctcs_api_en?g_api_lchip:lchip;
        ret = sys_greatbelt_l2_get_fdb_entry_detail(lchip, query, &query_rst, detail_info);
        if (ret < 0)
        {
            ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
            mem_free(query_rst.buffer);
            query_rst.buffer = NULL;
            mem_free(detail_info);
            detail_info = NULL;
            mem_free(write_entry_para.pdata);
            write_entry_para.pdata = NULL;
            sal_fclose(fp);
            fp = NULL;
            return;
        }

        for (index = 0; index < query->count; index++)
        {
            sal_memset(char_buffer, 0, sizeof(char_buffer));
            lport = query_rst.buffer[index].gport;
            sal_sprintf(char_buffer, "%-9d %-8d %.4x.%.4x.%.4x  %-5d 0x%.4x 0x%.4x %-7s %-8s\n",
                detail_info[index].key_index,
                detail_info[index].ad_index,
                sal_ntohs(*(unsigned short*)&query_rst.buffer[index].mac[0]),
                sal_ntohs(*(unsigned short*)&query_rst.buffer[index].mac[2]),
                sal_ntohs(*(unsigned short*)&query_rst.buffer[index].mac[4]),
                query_rst.buffer[index].fid,
                query_rst.buffer[index].gport,
                (lport & 0x00ff),
                (detail_info[index].flag.flag_ad.is_static) ? "Yes" : "No",
                (detail_info[index].flag.flag_node.is_tcam) ? "Tcam" : "Hash"
                );
            sal_fprintf(fp, "%s", char_buffer);
            sal_memset(&detail_info[index], 0, sizeof(sys_l2_detail_t));
        }

        total_count += query->count;
        sal_task_sleep(CTC_L2_FDB_QUERY_INTERVAL);
    }
    while (query_rst.is_end == 0);

    sal_fprintf(fp, "------------------------------------------------------------------------\n");
    sal_fprintf(fp, "Total Entry Num: %d\n", total_count);

    sal_fclose(fp);
    ctc_cli_out("FDB entry have been writen\n");

    mem_free(query_rst.buffer);
    query_rst.buffer = NULL;
    mem_free(detail_info);
    detail_info = NULL;
    mem_free(write_entry_para.pdata);
    write_entry_para.pdata = NULL;
    mem_free(user_param);
}

CTC_CLI(ctc_cli_gb_l2_show_fdb_status,
        ctc_cli_gb_l2_show_fdb_status_cmd,
        "show l2 fdb status (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        CTC_CLI_L2_M_STR,
        CTC_CLI_FDB_DESC,
        "FDB Status"
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
    ret = sys_greatbelt_l2_show_status(lchip);
    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;
}

CTC_CLI(ctc_cli_gb_l2_show_dflt,
        ctc_cli_gb_l2_show_dflt_cmd,
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
    uint16 fid = 0;
    uint8 lchip = 0;
    uint8 index = 0;
    ctc_l2dflt_addr_t l2dflt_addr;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;

    sal_memset(&l2dflt_addr, 0, sizeof(ctc_l2dflt_addr_t));

    CTC_CLI_GET_UINT16_RANGE("FID", fid, argv[0], 0, CTC_MAX_UINT16_VALUE);

    l2dflt_addr.fid = fid;

    ret = sys_greatbelt_l2_dump_default_entry(lchip, &l2dflt_addr);
    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;
}
CTC_CLI(ctc_cli_gb_l2_show_mcast,
        ctc_cli_gb_l2_show_mcast_cmd,
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
    uint8 index = 0;
    ctc_l2_mcast_addr_t l2mc_addr;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;

    /*1) parse MAC address */
    CTC_CLI_GET_MAC_ADDRESS("mac address", l2mc_addr.mac, argv[0]);

    CTC_CLI_GET_UINT16_RANGE("forwarding id", l2mc_addr.fid, argv[1], 0, CTC_MAX_UINT16_VALUE);

    ret = sys_greatbelt_l2_dump_l2mc_entry(lchip, &l2mc_addr);
    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;

}

CTC_CLI(ctc_cli_gb_l2_show_fdb_hw,
        ctc_cli_gb_l2_show_fdb_hw_cmd,
        "show l2 fdb hw (entry|detail) by (mac MAC|mac-fid MAC FID|port GPORT_ID|fid FID|port-fid GPORT_ID FID|all ) (static|) (dynamic|) (local-dynamic|) (mcast|)(conflict|)(all|)(entry-file FILE_NAME|) (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        CTC_CLI_L2_M_STR,
        CTC_CLI_FDB_DESC,
        "HW table",
        "FDB entry information",
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
        "File store fdb entry",
        "File name",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{

    int32 ret  = CLI_SUCCESS;
    uint32 index = 0;
    ctc_l2_fdb_query_t query;
    ctc_l2_fdb_query_rst_t query_rst;
    sys_l2_detail_t* detail_info =NULL;
    ctc_l2_write_info_para_t *write_entry_para = NULL;
    uint32 total_count = 0;
    uint16 lport = 0;
    uint32 detail = 0;
    uint8 hit = 0;
    uint8 lchip = 0;
    char buffer[SAL_TASK_MAX_NAME_LEN]={0};

    sal_memset(&query, 0, sizeof(ctc_l2_fdb_query_t));
    query.query_hw = 1;
    query.query_type = CTC_L2_FDB_ENTRY_OP_ALL;
    query.query_flag = CTC_L2_FDB_ENTRY_ALL;

    sal_memset(&query_rst, 0, sizeof(ctc_l2_fdb_query_rst_t));

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;

    index = CTC_CLI_GET_ARGC_INDEX("detail");
    if (index != 0xFF)
    {
        detail = 1;
    }

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
    index = ctc_cli_get_prefix_item(&argv[0], 2, "port", 4);
    if (index != 0xFF)
    {
        query.query_type = CTC_L2_FDB_ENTRY_OP_BY_PORT;
        CTC_CLI_GET_UINT16_RANGE("gport", query.gport, argv[index + 1], 0, CTC_MAX_UINT16_VALUE);
    }

    /*3) parse fid address*/
    index = ctc_cli_get_prefix_item(&argv[0], 2, "fid", 3);
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

    write_entry_para = (ctc_l2_write_info_para_t*)mem_malloc(MEM_CLI_MODULE, sizeof(ctc_l2_write_info_para_t));
    if (NULL == write_entry_para)
    {
        return CLI_ERROR;
    }
    sal_memset(write_entry_para, 0, sizeof(ctc_l2_write_info_para_t));
    index = CTC_CLI_GET_ARGC_INDEX("entry-file");
    if (index != 0xFF)
    {
        sal_strcpy((char*)&write_entry_para->file, argv[index + 1]);
    }

    if (*write_entry_para->file)
    {
        /* free memery will be done in ctc_greatbelt_write_fdb_detail_to_file */
        write_entry_para->pdata = (ctc_l2_fdb_query_t*)mem_malloc(MEM_CLI_MODULE, sizeof(ctc_l2_fdb_query_t));
        if (NULL == write_entry_para->pdata)
        {
            mem_free(write_entry_para);
            return CLI_ERROR;
        }

        sal_memcpy(write_entry_para->pdata, &query, sizeof(ctc_l2_fdb_query_t));
        if (detail)
        {
            sal_sprintf(buffer, "ctcFdbHwWFl-%d", lchip);
            if (0 != sal_task_create(&ctc_greatbelt_l2_write_fdb_hw_to_file,
                                     buffer,
                                     0, SAL_TASK_PRIO_DEF, ctc_greatbelt_write_fdb_hw_detail_to_file, write_entry_para))
            {
                sal_task_destroy(ctc_greatbelt_l2_write_fdb_hw_to_file);
                mem_free(write_entry_para);
                return CTC_E_NOT_INIT;
            }
        }
        else
        {
            sal_sprintf(buffer, "ctcFdbHwWFl-%d", lchip);
            if (0 != sal_task_create(&ctc_greatbelt_l2_write_fdb_hw_to_file,
                                     buffer,
                                     0, SAL_TASK_PRIO_DEF, ctc_greatbelt_write_fdb_hw_to_file, write_entry_para))
            {
                sal_task_destroy(ctc_greatbelt_l2_write_fdb_hw_to_file);
                mem_free(write_entry_para);
                return CTC_E_NOT_INIT;
            }
        }
    }
    else
    {
        query_rst.buffer_len = sizeof(ctc_l2_addr_t) * CTC_L2_FDB_QUERY_COUNT;
        query_rst.buffer = (ctc_l2_addr_t*)mem_malloc(MEM_CLI_MODULE, query_rst.buffer_len);
        if (NULL == query_rst.buffer)
        {
            ctc_cli_out("%% Alloc  memory  failed \n");
            mem_free(write_entry_para);
            return CLI_ERROR;
        }

        /* alloc memory for detail struct */
        detail_info = (sys_l2_detail_t*)mem_malloc(MEM_CLI_MODULE, sizeof(sys_l2_detail_t) * CTC_L2_FDB_QUERY_COUNT);
        if (NULL == detail_info)
        {
            mem_free(query_rst.buffer);
            query_rst.buffer = NULL;
            ctc_cli_out("%% Alloc  memory  failed \n");
            mem_free(write_entry_para);
            return CLI_ERROR;
        }

        sal_memset(query_rst.buffer, 0, query_rst.buffer_len);
        sal_memset(detail_info, 0, sizeof(sys_l2_detail_t) * CTC_L2_FDB_QUERY_COUNT);

        if (detail)
        {
            ctc_cli_out("FDB HW table\n");
            ctc_cli_out("%-9s %-8s %-15s %-5s %-6s %-6s %-7s %-6s %-7s\n", "KEY-INDEX", "AD-INDEX", "MAC", "FID", "GPORT", "LPORT", "Static", "Hit", "HASH/TCAM");
            ctc_cli_out("------------------------------------------------------------------------------\n");

            do
            {
                query_rst.start_index = query_rst.next_query_index;

                ret = sys_greatbelt_l2_get_fdb_entry_detail(lchip, &query, &query_rst, detail_info);
                if (ret < 0)
                {
                    ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
                    mem_free(query_rst.buffer);
                    query_rst.buffer = NULL;
                    mem_free(detail_info);
                    detail_info = NULL;
                    mem_free(write_entry_para);
                    return CLI_ERROR;
                }

                for (index = 0; index < query.count; index++)
                {
                    sys_greatbelt_aging_get_aging_status(lchip, 4, detail_info[index].key_index, &hit);
                    lport = query_rst.buffer[index].gport;
                    ctc_cli_out("%-9d %-8d %.4x.%.4x.%.4x  %-5d 0x%.4x 0x%.4x %-7s %-6s %-7s\n",
                        detail_info[index].key_index,
                        detail_info[index].ad_index,
                        sal_ntohs(*(unsigned short*)&query_rst.buffer[index].mac[0]),
                        sal_ntohs(*(unsigned short*)&query_rst.buffer[index].mac[2]),
                        sal_ntohs(*(unsigned short*)&query_rst.buffer[index].mac[4]),
                        query_rst.buffer[index].fid,
                        query_rst.buffer[index].gport,
                        (lport & 0x00ff),
                        (detail_info[index].flag.flag_ad.is_static) ? "Yes" : "No",
                        (hit) ? "Y" : "N",
                        (detail_info[index].flag.flag_node.is_tcam) ? "Tcam" : "Hash"
                        );
                    sal_memset(&detail_info[index], 0, sizeof(sys_l2_detail_t));
                }

                total_count += query.count;
                sal_task_sleep(CTC_L2_FDB_QUERY_INTERVAL);

            }
            while (query_rst.is_end == 0);

            ctc_cli_out("------------------------------------------------------------------------------\n");
            ctc_cli_out("Total Entry Num: %d\n", total_count);
        }
        else
        {
            ctc_cli_out("FDB HW table\n");
            ctc_cli_out("%-15s %-5s %-6s %-7s\n", "MAC", "FID", "GPORT", "Static");
            ctc_cli_out("------------------------------------\n");

            do
            {
                query_rst.start_index = query_rst.next_query_index;

                ret = sys_greatbelt_l2_get_fdb_entry(lchip, &query, &query_rst);
                if (ret < 0)
                {
                    ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
                    mem_free(query_rst.buffer);
                    query_rst.buffer = NULL;
                    mem_free(detail_info);
                    detail_info = NULL;
                    mem_free(write_entry_para);
                    return CLI_ERROR;
                }

                for (index = 0; index < query.count; index++)
                {
                    ctc_cli_out("%.4x.%.4x.%.4x  %-5d 0x%.4x %-7s\n",
                        sal_ntohs(*(unsigned short*)&query_rst.buffer[index].mac[0]),
                        sal_ntohs(*(unsigned short*)&query_rst.buffer[index].mac[2]),
                        sal_ntohs(*(unsigned short*)&query_rst.buffer[index].mac[4]),
                        query_rst.buffer[index].fid,
                        query_rst.buffer[index].gport,
                        (query_rst.buffer[index].flag & CTC_L2_FLAG_IS_STATIC) ? "Yes" : "No"
                        );
                    sal_memset(&query_rst.buffer[index], 0, sizeof(ctc_l2_addr_t));
                }

                total_count += query.count;
                sal_task_sleep(CTC_L2_FDB_QUERY_INTERVAL);

            }
            while (query_rst.is_end == 0);

            ctc_cli_out("------------------------------------\n");
            ctc_cli_out("Total Entry Num: %d\n", total_count);
        }

        mem_free(query_rst.buffer);
        query_rst.buffer = NULL;
        mem_free(detail_info);
        detail_info = NULL;
        mem_free(write_entry_para);
    }

    return ret;

}

CTC_CLI(ctc_cli_gb_l2_show_fdb_detail,
        ctc_cli_gb_l2_show_fdb_detail_cmd,
        "show l2 fdb detail by (mac MAC|mac-fid MAC FID|port GPORT_ID|fid FID|port-fid GPORT_ID FID|all ) (static|) (dynamic|) (local-dynamic|) (mcast|) (conflict) (all|)(entry-file FILE_NAME|) (lchip LCHIP|)",
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
        "File store fdb entry",
        "File name",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{

    int32 ret  = CLI_SUCCESS;
    uint8 index = 0;
    uint8 lchip = 0;
    ctc_l2_fdb_query_t query;
    ctc_l2_fdb_query_rst_t query_rst;
    sys_l2_detail_t* detail_info = NULL;
    ctc_l2_write_info_para_t write_entry_para;
    uint32 total_count = 0;
    uint16 lport = 0;
    uint8 hit = 0;
    uint16 index1 = 0;
    char buffer[SAL_TASK_MAX_NAME_LEN] = {0};

    sal_memset(&query, 0, sizeof(ctc_l2_fdb_query_t));
    query.query_hw = 0;
    query.query_type = CTC_L2_FDB_ENTRY_OP_ALL;
    query.query_flag = CTC_L2_FDB_ENTRY_ALL;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;

    sal_memset(&query_rst, 0, sizeof(ctc_l2_fdb_query_rst_t));
    sal_memset(&write_entry_para, 0, sizeof(ctc_l2_write_info_para_t));

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

    index = CTC_CLI_GET_ARGC_INDEX("entry-file");
    if (index != 0xFF)
    {
        sal_strcpy((char*)&write_entry_para.file, argv[index + 1]);
    }

    if (*write_entry_para.file)
    {
        /* free memery will be done in ctc_greatbelt_write_fdb_detail_to_file */
        write_entry_para.pdata = (ctc_l2_fdb_query_t*)mem_malloc(MEM_CLI_MODULE, sizeof(ctc_l2_fdb_query_t));
        if (NULL == write_entry_para.pdata)
        {
            return CLI_ERROR;
        }

        sal_memcpy(write_entry_para.pdata, &query, sizeof(ctc_l2_fdb_query_t));
        sal_sprintf(buffer, "ctcFdbWFl-%d", lchip);
        if (0 != sal_task_create(&ctc_greatbelt_l2_write_fdb_detail_to_file,
                                 buffer,
                                 0, SAL_TASK_PRIO_DEF, ctc_greatbelt_write_fdb_detail_to_file, &write_entry_para))
        {
            sal_task_destroy(ctc_greatbelt_l2_write_fdb_detail_to_file);
            return CTC_E_NOT_INIT;
        }
    }
    else
    {
        query_rst.buffer_len = sizeof(ctc_l2_addr_t) * CTC_L2_FDB_QUERY_COUNT;
        query_rst.buffer = (ctc_l2_addr_t*)mem_malloc(MEM_CLI_MODULE, query_rst.buffer_len);
        if (NULL == query_rst.buffer)
        {
            ctc_cli_out("%% Alloc  memory  failed \n");
            return CLI_ERROR;
        }

        /* alloc memory for detail struct */
        detail_info = (sys_l2_detail_t*)mem_malloc(MEM_CLI_MODULE, sizeof(sys_l2_detail_t) * CTC_L2_FDB_QUERY_COUNT);
        if (NULL == detail_info)
        {
            mem_free(query_rst.buffer);
            query_rst.buffer = NULL;
            ctc_cli_out("%% Alloc  memory  failed \n");
            return CLI_ERROR;
        }

        sal_memset(query_rst.buffer, 0, query_rst.buffer_len);
        sal_memset(detail_info, 0, sizeof(sys_l2_detail_t) * CTC_L2_FDB_QUERY_COUNT);

        ctc_cli_out("%8s  %8s  %8s  %11s    %4s   %4s   %6s    %-6s %6s\n", "KEY-INDEX", "AD-INDEX", "MAC", "FID", "GPORT", "LPORT", "Static", "Hit", "HASH/TCAM");
        ctc_cli_out("------------------------------------------------------------------------------------\n");

        do
        {
            query_rst.start_index = query_rst.next_query_index;

            ret = sys_greatbelt_l2_get_fdb_entry_detail(lchip, &query, &query_rst, detail_info);
            if (ret < 0)
            {
                ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
                mem_free(query_rst.buffer);
                query_rst.buffer = NULL;
                mem_free(detail_info);
                detail_info = NULL;
                return CLI_ERROR;
            }

            for (index1 = 0; index1 < query.count; index1++)
            {
                sys_greatbelt_aging_get_aging_status(lchip, 4, (detail_info[index1].key_index + 16), &hit);
                ctc_cli_out("0x%-8x", detail_info[index1].key_index);
                ctc_cli_out("0x%-8x", detail_info[index1].ad_index);
                ctc_cli_out("%.4x.%.4x.%.4x%4s ", sal_ntohs(*(unsigned short*)&query_rst.buffer[index1].mac[0]),
                            sal_ntohs(*(unsigned short*)&query_rst.buffer[index1].mac[2]),
                            sal_ntohs(*(unsigned short*)&query_rst.buffer[index1].mac[4]),
                            " ");

                ctc_cli_out("%.4d  ", query_rst.buffer[index1].fid);
                ctc_cli_out("0x%.4x  ", query_rst.buffer[index1].gport);
                lport = query_rst.buffer[index1].gport;
                ctc_cli_out("0x%.4x    ", (lport & 0x00ff));
                ctc_cli_out("%3s     ", (detail_info[index1].flag.flag_ad.is_static) ? "Yes" : "No");
                ctc_cli_out("%3s     ", (hit) ? "Y" : "N");
                ctc_cli_out("%s\n", (detail_info[index1].flag.flag_node.is_tcam) ? "Tcam" : "Hash");

                sal_memset(&detail_info[index1], 0, sizeof(sys_l2_detail_t));
            }

            total_count += query.count;
            sal_task_sleep(CTC_L2_FDB_QUERY_INTERVAL);

        }
        while (query_rst.is_end == 0);

        ctc_cli_out("------------------------------------------------------------------------------------\n");
        ctc_cli_out("Total Entry Num: %d\n", total_count);

        mem_free(query_rst.buffer);
        query_rst.buffer = NULL;
        mem_free(detail_info);
        detail_info = NULL;
    }

    return ret;

}


CTC_CLI(ctc_cli_gb_l2_show_master,
        ctc_cli_gb_l2_show_master_cmd,
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

    ret = sys_greatbelt_l2_dump_l2master(lchip);
    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;

}

CTC_CLI(ctc_cli_gb_l2_hw_en,
        ctc_cli_gb_l2_hw_en_cmd,
        "l2 fdb hw-learn (enable | disable) (lchip LCHIP|)",
        CTC_CLI_L2_M_STR,
        CTC_CLI_FDB_DESC,
        "Hard ware learning",
        "Enable",
        "Disable",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret  = CLI_SUCCESS;
    uint8 hw_en = 0;
    uint8 lchip = 0;
    uint8    index = 0xFF;

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

    ret = sys_greatbelt_l2_set_hw_learn(lchip, hw_en);
    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;

}

CTC_CLI(ctc_cli_gb_l2_fdb_set_dump_task_time,
        ctc_cli_gb_l2_fdb_set_dump_task_time_cmd,
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

    sys_greatbelt_fdb_sort_set_dump_task_time(lchip, time);

    return CLI_SUCCESS;

}


CTC_CLI(ctc_cli_gb_l2_fdb_set_mac_limit,
        ctc_cli_gb_l2_fdb_set_mac_limit_cmd,
        "l2 fdb mac-limit (enable (remove-fdb|)|disable) (lchip LCHIP|)",
        CTC_CLI_L2_M_STR,
        CTC_CLI_FDB_DESC,
        "mac limit if remove fdb",
        "enable",
        "disable",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    uint8 lchip = 0;
    uint8 is_remove = 0;
    uint8 limit_en = 0;
    uint8 index = 0xFF;

    index = CTC_CLI_GET_ARGC_INDEX("enable");
    if (0xFF != index)
    {
        limit_en = 1;
    }
    else
    {
        limit_en = 0;
    }

    index = CTC_CLI_GET_ARGC_INDEX("remove-fdb");
    if (0xFF != index)
    {
        is_remove = 1;
    }
    else
    {
        is_remove = 0;
    }

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;

    sys_greatbelt_fdb_sort_set_mac_limit_en(lchip, limit_en, is_remove);

    return CLI_SUCCESS;

}

CTC_CLI(ctc_cli_gb_l2_fdb_trie_sort,
        ctc_cli_gb_l2_fdb_trie_sort_cmd,
        "l2 fdb trie-sort (enable |disable) (lchip LCHIP|)",
        CTC_CLI_L2_M_STR,
        CTC_CLI_FDB_DESC,
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

    sys_greatbelt_l2_fdb_set_trie_sort(lchip, enable);

    return CLI_SUCCESS;

}


int32
ctc_greatbelt_l2_cli_init(void)
{

    install_element(CTC_SDK_MODE, &ctc_cli_gb_l2_show_fdb_status_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gb_l2_show_dflt_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gb_l2_show_mcast_cmd);
    install_element(CTC_INTERNAL_MODE, &ctc_cli_gb_l2_show_fdb_detail_cmd);
    install_element(CTC_INTERNAL_MODE, &ctc_cli_gb_l2_show_master_cmd);

    install_element(CTC_INTERNAL_MODE, &ctc_cli_gb_l2_hw_en_cmd);
    install_element(CTC_INTERNAL_MODE, &ctc_cli_gb_l2_fdb_set_dump_task_time_cmd);
    install_element(CTC_INTERNAL_MODE, &ctc_cli_gb_l2_fdb_set_mac_limit_cmd);
    install_element(CTC_INTERNAL_MODE, &ctc_cli_gb_l2_fdb_trie_sort_cmd);

    install_element(CTC_SDK_MODE, &ctc_cli_gb_l2_show_fdb_hw_cmd);  /* read FDB HW table */
    return 0;
}

int32
ctc_greatbelt_l2_cli_deinit(void)
{

    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_l2_show_fdb_status_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_l2_show_dflt_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_l2_show_mcast_cmd);
    uninstall_element(CTC_INTERNAL_MODE, &ctc_cli_gb_l2_show_fdb_detail_cmd);
    uninstall_element(CTC_INTERNAL_MODE, &ctc_cli_gb_l2_show_master_cmd);

    uninstall_element(CTC_INTERNAL_MODE, &ctc_cli_gb_l2_hw_en_cmd);

    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_l2_show_fdb_hw_cmd);  /* read FDB HW table */
    return 0;
}


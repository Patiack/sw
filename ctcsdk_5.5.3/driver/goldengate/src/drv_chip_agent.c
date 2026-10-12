#include "sal_types.h"
#include "goldengate/include/drv_lib.h"
#include "goldengate/include/drv_chip_agent.h"

extern drv_chip_agent_mode_t g_gg_chip_agent_mode;
drv_chip_agent_t g_drv_gg_chip_agent_master;
bool g_gg_eadp_drv_debug_en = FALSE;

drv_chip_agent_mode_t g_gg_chip_agent_mode = DRV_CHIP_AGT_MODE_NONE;

char*
_goldengate_chip_agent_io_op_str(chip_io_op_t op)
{
    switch (op)
    {
    case CHIP_IO_OP_IOCTL:
        return "ioctl";

    case CHIP_IO_OP_TCAM_REMOVE:
        return "tcam remove";

    case CHIP_IO_OP_HASH_KEY_IOCTL:
        return "hash ioctl";

    case CHIP_IO_OP_HASH_LOOKUP:
        return "hash lookup";

    default:
        return "invalid";
    }
}

char*
_goldengate_chip_agent_cmd_action_str(uint32 action)
{
    switch (action)
    {
    case DRV_IOC_READ:
        return "R";

    case DRV_IOC_WRITE:
        return "W";

    default:
        return "invalid";
    }
}

char*
_chip_agent_dma_op_str(uint32 op)
{
    switch (op)
    {
    case CHIP_AGENT_DMA_INFO_TYPE_LEARN:
        return "FDB Learning";

    case CHIP_AGENT_DMA_INFO_TYPE_HASHDUMP:
        return "HASH DUMP";

    case CHIP_AGENT_DMA_INFO_TYPE_IPFIX:
        return "IPFIX";

    case CHIP_AGENT_DMA_INFO_TYPE_SDC:
        return "SDC";

    case CHIP_AGENT_DMA_INFO_TYPE_MONITOR:
        return "MONITOR";

    default:
        return "invalid";
    }
}

char*
_goldengate_chip_agent_hash_op_str(hash_op_type_t op)
{
    switch (op)
    {
    case HASH_OP_TP_ADD_BY_KEY:
        return "add by key";

    case HASH_OP_TP_DEL_BY_KEY:
        return "del by key";

    case HASH_OP_TP_ADD_BY_INDEX:
        return "add by index";

    case HASH_OP_TP_DEL_BY_INDEX:
        return "del by index";

    default:
        return "invalid";
    }
}

char*
_goldengate_chip_agent_cmd_fib_acc_type_str(uint32 action)
{
    switch (action)
    {
    case DRV_FIB_ACC_WRITE_MAC_BY_IDX:
        return "DRV_FIB_ACC_WRITE_MAC_BY_IDX";

    case DRV_FIB_ACC_WRITE_MAC_BY_KEY:
        return "DRV_FIB_ACC_WRITE_MAC_BY_KEY";

    case DRV_FIB_ACC_DEL_MAC_BY_IDX:
        return "DRV_FIB_ACC_DEL_MAC_BY_IDX";

    case DRV_FIB_ACC_DEL_MAC_BY_KEY:
        return "DRV_FIB_ACC_DEL_MAC_BY_KEY";

    case DRV_FIB_ACC_LKP_MAC:
        return "DRV_FIB_ACC_LKP_MAC";

    case DRV_FIB_ACC_FLUSH_MAC_BY_PORT_VLAN:
        return "DRV_FIB_ACC_FLUSH_MAC_BY_PORT_VLAN";

    case DRV_FIB_ACC_FLUSH_MAC_ALL:
        return "DRV_FIB_ACC_FLUSH_MAC_ALL";

    case DRV_FIB_ACC_FLUSH_MAC_BY_MAC_PORT:
        return "DRV_FIB_ACC_FLUSH_MAC_BY_MAC_PORT";

    case DRV_FIB_ACC_LKP_FIB0:
        return "DRV_FIB_ACC_LKP_FIB0";

    case DRV_FIB_ACC_WRITE_FIB0_BY_KEY:
        return "DRV_FIB_ACC_WRITE_FIB0_BY_KEY";

    case DRV_FIB_ACC_READ_FIB0_BY_IDX:
        return "DRV_FIB_ACC_READ_FIB0_BY_IDX";

    case DRV_FIB_ACC_WRITE_FIB0_BY_IDX:
        return "DRV_FIB_ACC_WRITE_FIB0_BY_IDX";

    case DRV_FIB_ACC_UPDATE_MAC_LIMIT:
        return "DRV_FIB_ACC_UPDATE_MAC_LIMIT";

    case DRV_FIB_ACC_READ_MAC_LIMIT:
        return "DRV_FIB_ACC_READ_MAC_LIMIT";

    case DRV_FIB_ACC_WRITE_MAC_LIMIT:
        return "DRV_FIB_ACC_WRITE_MAC_LIMIT";

    case DRV_FIB_ACC_DUMP_MAC_BY_PORT_VLAN:
        return "DRV_FIB_ACC_DUMP_MAC_BY_PORT_VLAN";

    case DRV_FIB_ACC_DUMP_MAC_ALL:
        return "DRV_FIB_ACC_DUMP_MAC_ALL";

    default:
        return "invalid";
    }
}


char*
_goldengate_chip_agent_cmd_cpu_acc_type_str(uint32 action)
{

    switch (action)
    {
    case DRV_CPU_LOOKUP_ACC_FIB_HOST1:
        return     "DRV_CPU_LOOKUP_ACC_FIB_HOST1";

    case DRV_CPU_ALLOC_ACC_FIB_HOST1:
        return     "DRV_CPU_ALLOC_ACC_FIB_HOST1";

    case DRV_CPU_ADD_ACC_FIB_HOST1:
        return     "DRV_CPU_ADD_ACC_FIB_HOST1";

    case DRV_CPU_DEL_ACC_FIB_HOST1:
        return     "DRV_CPU_DEL_ACC_FIB_HOST1";

    case DRV_CPU_ADD_ACC_FIB_HOST1_BY_IDX:
        return     "DRV_CPU_ADD_ACC_FIB_HOST1_BY_IDX";

    case DRV_CPU_DEL_ACC_FIB_HOST1_BY_IDX:
        return     "DRV_CPU_DEL_ACC_FIB_HOST1_BY_IDX";

    case DRV_CPU_READ_ACC_FIB_HOST1_BY_IDX:
        return     "DRV_CPU_READ_ACC_FIB_HOST1_BY_IDX";

    case DRV_CPU_LOOKUP_ACC_USER_ID:
        return     "DRV_CPU_LOOKUP_ACC_USER_ID";

    case DRV_CPU_ALLOC_ACC_USER_ID:
        return     "DRV_CPU_ALLOC_ACC_USER_ID";

    case DRV_CPU_ADD_ACC_USER_ID:
        return     "DRV_CPU_ADD_ACC_USER_ID";

    case DRV_CPU_DEL_ACC_USER_ID:
        return     "DRV_CPU_DEL_ACC_USER_ID";

    case DRV_CPU_ADD_ACC_USER_ID_BY_IDX:
        return     "DRV_CPU_ADD_ACC_USER_ID_BY_IDX";

    case DRV_CPU_DEL_ACC_USER_ID_BY_IDX:
        return     "DRV_CPU_DEL_ACC_USER_ID_BY_IDX";

    case DRV_CPU_READ_ACC_USER_ID_BY_IDX:
        return     "DRV_CPU_READ_ACC_USER_ID_BY_IDX";

    case DRV_CPU_LOOKUP_ACC_XC_OAM:
        return     "DRV_CPU_LOOKUP_ACC_XC_OAM";

    case DRV_CPU_ALLOC_ACC_XC_OAM:
        return     "DRV_CPU_ALLOC_ACC_XC_OAM";

    case DRV_CPU_ADD_ACC_XC_OAM:
        return     "DRV_CPU_ADD_ACC_XC_OAM";

    case DRV_CPU_DEL_ACC_XC_OAM:
        return     "DRV_CPU_DEL_ACC_XC_OAM";

    case DRV_CPU_ADD_ACC_XC_OAM_BY_IDX:
        return     "DRV_CPU_ADD_ACC_XC_OAM_BY_IDX";

    case DRV_CPU_DEL_ACC_XC_OAM_BY_IDX:
        return     "DRV_CPU_DEL_ACC_XC_OAM_BY_IDX";

    case DRV_CPU_READ_ACC_XC_OAM_BY_IDX:
        return     "DRV_CPU_READ_ACC_XC_OAM_BY_IDX";

    case DRV_CPU_LOOKUP_ACC_FLOW_HASH:
        return     "DRV_CPU_LOOKUP_ACC_FLOW_HASH";

    case DRV_CPU_ALLOC_ACC_FLOW_HASH:
        return     "DRV_CPU_ALLOC_ACC_FLOW_HASH";

    case DRV_CPU_ADD_ACC_FLOW_HASH:
        return     "DRV_CPU_ADD_ACC_FLOW_HASH";

    case DRV_CPU_DEL_ACC_FLOW_HASH:
        return     "DRV_CPU_DEL_ACC_FLOW_HASH";

    case DRV_CPU_ADD_ACC_FLOW_HASH_BY_IDX:
        return     "DRV_CPU_ADD_ACC_FLOW_HASH_BY_IDX";

    case DRV_CPU_DEL_ACC_FLOW_HASH_BY_IDX:
        return     "DRV_CPU_DEL_ACC_FLOW_HASH_BY_IDX";

    case DRV_CPU_READ_ACC_FLOW_HASH_BY_IDX:
        return     "DRV_CPU_READ_ACC_FLOW_HASH_BY_IDX";

    default:
        return "invalid";
    }
}


char*
_goldengate_chip_agent_cmd_ipfix_acc_type_str(uint32 action)
{
    switch (action)
    {
    case DRV_IPFIX_ACC_WRITE_KEY_BY_IDX:
        return "DRV_IPFIX_ACC_WRITE_KEY_BY_IDX";

    case  DRV_IPFIX_ACC_WRITE_AD_BY_IDX:
        return "DRV_IPFIX_ACC_WRITE_AD_BY_IDX";

    case  DRV_IPFIX_ACC_WRITE_BY_KEY:
        return "DRV_IPFIX_ACC_WRITE_BY_KEY";

    case DRV_IPFIX_ACC_LKP_BY_KEY:
        return "DRV_IPFIX_ACC_LKP_BY_KEY";

    case DRV_IPFIX_ACC_LKP_KEY_BY_IDX:
        return "DRV_IPFIX_ACC_LKP_KEY_BY_IDX";

    case DRV_IPFIX_ACC_LKP_AD_BY_IDX:
        return "DRV_IPFIX_ACC_LKP_AD_BY_IDX";

    case DRV_IPFIX_ACC_DEL_KEY_BY_IDX:
        return "DRV_IPFIX_ACC_DEL_KEY_BY_IDX";

    case  DRV_IPFIX_ACC_DEL_AD_BY_IDX:
        return "DRV_IPFIX_ACC_DEL_AD_BY_IDX";

    case  DRV_IPFIX_ACC_DEL_BY_KEY:
        return "DRV_IPFIX_ACC_DEL_BY_KEY";

    default:
        return "invalid";
    }
}

int32
drv_goldengate_chip_agent_mode(void)
{
    return g_gg_chip_agent_mode;
}

/* define cb for chip agent  */
int32
drv_goldengate_register_intr_dispatch_cb(DRV_EADP_INTR_DISPATCH_CB cb)
{
    g_drv_gg_chip_agent_master.eadp_intr_dispatch_cb = cb;
    return DRV_E_NONE;
}

int32
drv_goldengate_register_pkt_rx_cb(DRV_EADP_PKT_RX_CB cb)
{
    g_drv_gg_chip_agent_master.eadp_pkt_rx_cb = cb;
    return DRV_E_NONE;
}


#define  _CHIP_AGENT_ENCODE_DECODE
int32
_goldengate_chip_agent_encode_init(uint8* buf, chip_agent_msg_init_para_t* para, uint32* req_len)
{
    int32 pkt_size_left = CHIP_AGT_BUF_SIZE;
    int32* size = NULL;
    uint8** pnt = NULL;
    uint8* start = NULL;
    uint8* end = NULL;

    size = &pkt_size_left;
    start = buf;
    pnt = &buf;

    ENCODE_PUTC(para->profile_type);
    ENCODE_PUTC(para->endian);
    ENCODE_PUTC(para->ver_len);
    ENCODE_PUTC(para->date_len);
    ENCODE_PUTL(para->is_asic);
    ENCODE_PUTL(para->sim_debug_action);
    ENCODE_PUTL(para->sim_packet_debug);
    ENCODE_PUTL(para->ret);

    if (para->ver_len)
    {
        ENCODE_PUT(para->ver, para->ver_len);
    }

    if (para->date_len)
    {
        ENCODE_PUT(para->date, para->date_len);
    }

    end = *pnt;
    *req_len = end - start;

    DRV_EADP_DBG_INFO(g_gg_eadp_drv_debug_en, "encode Init,profile_type %d,LE %d,is_asic %d,debug_action %d,packet_debug %d,ret %d,ver %s,date %s,ver_len %d,date_len %d\n",
                    para->profile_type, para->endian, para->is_asic, para->sim_debug_action, para->sim_packet_debug, para->ret, para->ver, para->date, para->ver_len, para->date_len);

    return DRV_CHIP_AGT_E_NONE;
}

int32
_goldengate_chip_agent_decode_init(uint8* buf, chip_agent_msg_init_para_t* para, uint32 len)
{
    int32 pkt_size_left = len;
    int32* size = NULL;
    uint8** pnt = NULL;

    size = &pkt_size_left;
    pnt = &buf;

    if (len == 0)
    {
        return DRV_CHIP_AGT_E_DECODE_ZERO_LEN;
    }

    /* encode IO header */
    DECODE_GETC(para->profile_type);
    DECODE_GETC(para->endian);
    DECODE_GETC(para->ver_len);
    DECODE_GETC(para->date_len);
    DECODE_GETL(para->is_asic);
    DECODE_GETL(para->sim_debug_action);
    DECODE_GETL(para->sim_packet_debug);
    DECODE_GETL(para->ret);

    if (para->ver_len)
    {
        DECODE_GET(para->ver, para->ver_len);
    }

    if (para->date_len)
    {
        DECODE_GET(para->date, para->date_len);
    }

    if (*size != 0)
    {
        return DRV_CHIP_AGT_E_MSG_LEN;
    }

    DRV_EADP_DBG_INFO(g_gg_eadp_drv_debug_en, "decode Init,profile_type %d,LE %d,is_asic %d,debug_action %d,packet_debug %d,ret %d,ver %s,date %s,ver_len %d,date_len %d\n",
                  para->profile_type, para->endian, para->is_asic, para->sim_debug_action, para->sim_packet_debug, para->ret, para->ver, para->date, para->ver_len, para->date_len);

    return DRV_CHIP_AGT_E_NONE;
}

int32
_goldengate_chip_agent_encode_model(uint8* buf, chip_agent_msg_model_para_t* para, uint32* req_len)
{
    int32 pkt_size_left = CHIP_AGT_BUF_SIZE;
    int32* size = NULL;
    uint8** pnt = NULL;
    uint8* start = NULL;
    uint8* end = NULL;

    size = &pkt_size_left;
    start = buf;
    pnt = &buf;

    ENCODE_PUTL(para->type);
    ENCODE_PUTL(para->param_count);
    ENCODE_PUTL(para->param[0]);
    ENCODE_PUTL(para->param[1]);
    ENCODE_PUTL(para->param[2]);
    ENCODE_PUTL(para->param[3]);
    ENCODE_PUTL(para->param[4]);
    ENCODE_PUTL(para->ret);

    end = *pnt;
    *req_len = end - start;

    DRV_EADP_DBG_INFO(g_gg_eadp_drv_debug_en, "encode Model,type %d,count %d,param %d,%d,%d,%d,%d\n",
                  para->type, para->param_count, para->param[0], para->param[1], para->param[2], para->param[3], para->param[4]);

    return DRV_CHIP_AGT_E_NONE;
}

int32
_goldengate_chip_agent_decode_model(uint8* buf, chip_agent_msg_model_para_t* para, uint32 len)
{
    int32 pkt_size_left = len;
    int32* size = NULL;
    uint8** pnt = NULL;

    size = &pkt_size_left;
    pnt = &buf;

    if (len == 0)
    {
        return DRV_CHIP_AGT_E_DECODE_ZERO_LEN;
    }

    DECODE_GETL(para->type);
    DECODE_GETL(para->param_count);
    DECODE_GETL(para->param[0]);
    DECODE_GETL(para->param[1]);
    DECODE_GETL(para->param[2]);
    DECODE_GETL(para->param[3]);
    DECODE_GETL(para->param[4]);
    DECODE_GETL(para->ret);

    if (*size != 0)
    {
        return DRV_CHIP_AGT_E_MSG_LEN;
    }

    DRV_EADP_DBG_INFO(g_gg_eadp_drv_debug_en, "decode Model,type %d,count %d,param %d,%d,%d,%d,%d\n",
                  para->type, para->param_count, para->param[0], para->param[1], para->param[2], para->param[3], para->param[4]);

    return DRV_CHIP_AGT_E_NONE;
}

int32
_goldengate_chip_agent_encode_ctrl(uint8* buf, chip_agent_msg_ctrl_para_t* para, uint32* req_len)
{
    int32 pkt_size_left = CHIP_AGT_BUF_SIZE;
    int32* size = NULL;
    uint8** pnt = NULL;
    uint8* start = NULL;
    uint8* end = NULL;

    size = &pkt_size_left;
    start = buf;
    pnt = &buf;

    ENCODE_PUTL(para->action);
    ENCODE_PUTL(para->type);
    ENCODE_PUTL(para->flag);
    ENCODE_PUTL(para->enable);
    ENCODE_PUTL(para->ret);

    end = *pnt;
    *req_len = end - start;

    DRV_EADP_DBG_INFO(g_gg_eadp_drv_debug_en, "encode Control,action %d,type %d,flag 0x%04x,enable %d,ret %d\n",
                  para->action, para->type, para->flag, para->enable, para->ret);

    return DRV_CHIP_AGT_E_NONE;
}

int32
_goldengate_chip_agent_decode_ctrl(uint8* buf, chip_agent_msg_ctrl_para_t* para, uint32 len)
{
    int32 pkt_size_left = len;
    int32* size = NULL;
    uint8** pnt = NULL;

    size = &pkt_size_left;
    pnt = &buf;

    if (len == 0)
    {
        return DRV_CHIP_AGT_E_DECODE_ZERO_LEN;
    }

    DECODE_GETL(para->action);
    DECODE_GETL(para->type);
    DECODE_GETL(para->flag);
    DECODE_GETL(para->enable);
    DECODE_GETL(para->ret);

    if (*size != 0)
    {
        return DRV_CHIP_AGT_E_MSG_LEN;
    }

    DRV_EADP_DBG_INFO(g_gg_eadp_drv_debug_en, "decode Control,action %d,type %d,flag 0x%04x,enable %d,ret %d\n",
                  para->action, para->type, para->flag, para->enable, para->ret);

    return DRV_CHIP_AGT_E_NONE;
}

int32
_goldengate_chip_agent_encode_pkt(uint8* buf, chip_agent_msg_pkt_para_t* para, uint32* req_len)
{
    int32 pkt_size_left = CHIP_AGT_BUF_SIZE;
    int32* size = NULL;
    uint8** pnt = NULL;
    uint8* start = NULL;
    uint8* end = NULL;

    size = &pkt_size_left;
    start = buf;
    pnt = &buf;

    ENCODE_PUTL(para->pkt_len);
    ENCODE_PUTL(para->pkt_mode);

    if (para->pkt_len)
    {
        ENCODE_PUT(para->pkt, para->pkt_len);
    }

    end = *pnt;
    *req_len = end - start;

    DRV_EADP_DBG_INFO(g_gg_eadp_drv_debug_en, "encode Packet,len %d,mode %d\n", para->pkt_len, para->pkt_mode);

    return DRV_CHIP_AGT_E_NONE;
}

int32
_goldengate_chip_agent_decode_pkt(uint8* buf, chip_agent_msg_pkt_para_t* para, uint32 len)
{
    int32 pkt_size_left = len;
    int32* size = NULL;
    uint8** pnt = NULL;

    size = &pkt_size_left;
    pnt = &buf;

    if (len == 0)
    {
        return DRV_CHIP_AGT_E_DECODE_ZERO_LEN;
    }

    DECODE_GETL(para->pkt_len);
    DECODE_GETL(para->pkt_mode);

    if (para->pkt_len)
    {
        DECODE_GET(para->pkt, para->pkt_len);
    }

    if (*size != 0)
    {
        return DRV_CHIP_AGT_E_MSG_LEN;
    }

    DRV_EADP_DBG_INFO(g_gg_eadp_drv_debug_en, "decode Packet,len %d,mode %d\n", para->pkt_len, para->pkt_mode);

    return DRV_CHIP_AGT_E_NONE;
}

int32
_chip_agent_encode_rx_pkt(uint8* buf, chip_agent_msg_pkt_rx_para_t* para, uint32* req_len)
{
    int32 pkt_size_left = CHIP_AGT_BUF_SIZE;
    int32* size = NULL;
    uint8** pnt = NULL;
    uint8* start = NULL;
    uint8* end = NULL;

    size = &pkt_size_left;
    start = buf;
    pnt = &buf;

    ENCODE_PUTL(para->pkt_len);
    ENCODE_PUTL(para->pkt_mode);
    ENCODE_PUTL(para->dma_chan);
    ENCODE_PUTL(para->buf_count);
    ENCODE_PUTL(para->pkt_buf_len);

    if (para->pkt_len)
    {
        ENCODE_PUT(para->pkt, para->pkt_len);
    }

    end = *pnt;
    *req_len = end - start;

    DRV_EADP_DBG_INFO(g_gg_eadp_drv_debug_en, "encode Packet,len %d,mode %d\n", para->pkt_len, para->pkt_mode);

    return DRV_CHIP_AGT_E_NONE;
}

int32
_chip_agent_decode_rx_pkt(uint8* buf, chip_agent_msg_pkt_rx_para_t* para, uint32 len)
{
    int32 pkt_size_left = len;
    int32* size = NULL;
    uint8** pnt = NULL;

    size = &pkt_size_left;
    pnt = &buf;

    if (len == 0)
    {
        return DRV_CHIP_AGT_E_DECODE_ZERO_LEN;
    }

    DECODE_GETL(para->pkt_len);
    DECODE_GETL(para->pkt_mode);
    DECODE_GETL(para->dma_chan);
    DECODE_GETL(para->buf_count);
    DECODE_GETL(para->pkt_buf_len);

    if (para->pkt_len)
    {
        DECODE_GET(para->pkt, para->pkt_len);
    }

    if (*size != 0)
    {
        return DRV_CHIP_AGT_E_MSG_LEN;
    }

    DRV_EADP_DBG_INFO(g_gg_eadp_drv_debug_en, "decode Packet,len %d,mode %d\n", para->pkt_len, para->pkt_mode);

    return DRV_CHIP_AGT_E_NONE;
}


int32
_goldengate_chip_agent_encode_log(uint8* buf, chip_agent_msg_log_para_t* para, uint32* req_len)
{
    int32 pkt_size_left = CHIP_AGT_BUF_SIZE;
    int32* size = NULL;
    uint8** pnt = NULL;
    uint8* start = NULL;
    uint8* end = NULL;

    size = &pkt_size_left;
    start = buf;
    pnt = &buf;

    ENCODE_PUTL(para->len);

    if (para->len)
    {
        ENCODE_PUT(para->data, para->len);
    }

    end = *pnt;
    *req_len = end - start;

    DRV_EADP_DBG_INFO(g_gg_eadp_drv_debug_en, "encode Log,len %d\n", para->len);

    return DRV_CHIP_AGT_E_NONE;
}

int32
_goldengate_chip_agent_decode_log(uint8* buf, chip_agent_msg_log_para_t* para, uint32 len)
{
    int32 pkt_size_left = len;
    int32* size = NULL;
    uint8** pnt = NULL;

    size = &pkt_size_left;
    pnt = &buf;

    if (len == 0)
    {
        return DRV_CHIP_AGT_E_DECODE_ZERO_LEN;
    }

    DECODE_GETL(para->len);

    if (para->len)
    {
        DECODE_GET(para->data, para->len);
    }

    if (*size != 0)
    {
        return DRV_CHIP_AGT_E_MSG_LEN;
    }

    DRV_EADP_DBG_INFO(g_gg_eadp_drv_debug_en, "decode Log,len %d\n", para->len);

    return DRV_CHIP_AGT_E_NONE;
}

int32
_goldengate_chip_agent_encode_interrupt(uint8* buf, chip_agent_msg_interrupt_para_t* para, uint32* req_len)
{
    int32 pkt_size_left = CHIP_AGT_BUF_SIZE;
    int32* size = NULL;
    uint8** pnt = NULL;
    uint8* start = NULL;
    uint8* end = NULL;

    size = &pkt_size_left;
    start = buf;
    pnt = &buf;

    ENCODE_PUTL(para->group);

    end = *pnt;
    *req_len = end - start;

    DRV_EADP_DBG_INFO(g_gg_eadp_drv_debug_en, "encode Interrupt, group %d\n", para->group);

    return DRV_CHIP_AGT_E_NONE;
}

int32
_goldengate_chip_agent_decode_interrupt(uint8* buf, chip_agent_msg_interrupt_para_t* para, uint32 len)
{
    int32 pkt_size_left = len;
    int32* size = NULL;
    uint8** pnt = NULL;

    size = &pkt_size_left;
    pnt = &buf;

    if (len == 0)
    {
        return DRV_CHIP_AGT_E_DECODE_ZERO_LEN;
    }

    DECODE_GETL(para->group);

    if (*size != 0)
    {
        return DRV_CHIP_AGT_E_MSG_LEN;
    }

    DRV_EADP_DBG_INFO(g_gg_eadp_drv_debug_en, "decode Interrupt, group %d\n", para->group);

    return DRV_CHIP_AGT_E_NONE;
}


int32
_chip_agent_encode_oam_isr(uint8* buf, chip_agent_msg_interrupt_oam_t* para, uint32* req_len)
{
    int32 pkt_size_left = CHIP_AGT_BUF_SIZE;
    int32* size = NULL;
    uint8** pnt = NULL;
    uint8* start = NULL;
    uint8* end = NULL;

    size = &pkt_size_left;
    start = buf;
    pnt = &buf;

    ENCODE_PUTL(para->lchip);
    ENCODE_PUT(para->buf, MAX_DMA_NUM*4);

    end = *pnt;
    *req_len = end - start;

    DRV_EADP_DBG_INFO(g_gg_eadp_drv_debug_en, "encode oam defect Interrupt, gchip %d\n", para->lchip);

    return DRV_CHIP_AGT_E_NONE;
}

int32
_chip_agent_decode_oam_isr(uint8* buf, chip_agent_msg_interrupt_oam_t* para, uint32 len)
{
    int32 pkt_size_left = len;
    int32* size = NULL;
    uint8** pnt = NULL;

    size = &pkt_size_left;
    pnt = &buf;

    if (len == 0)
    {
        return DRV_CHIP_AGT_E_DECODE_ZERO_LEN;
    }

    DECODE_GETL(para->lchip);
    CHIP_AGT_DECODE_BUF(para->buf, MAX_DMA_NUM*4);

    if (*size != 0)
    {
        return DRV_CHIP_AGT_E_MSG_LEN;
    }

    DRV_EADP_DBG_INFO(g_gg_eadp_drv_debug_en, "decode oam defect Interrupt, gchip %d\n", para->lchip);

    return DRV_CHIP_AGT_E_NONE;
}


int32
_goldengate_chip_agent_encode_io_ioctl(uint8* buf, chip_agent_msg_io_para_t* para, uint32* req_len)
{
    int32 pkt_size_left = CHIP_AGT_BUF_SIZE;
    int32* size = NULL;
    uint8** pnt = NULL;
    uint8* start = NULL;
    uint8* end = NULL;

    size = &pkt_size_left;
    start = buf;
    pnt = &buf;

    para->hdr.len = sizeof(chip_agent_msg_io_para_t) - sizeof(para->val) - sizeof(para->mask) + para->val_len + para->mask_len;
    /* encode IO header */
    ENCODE_PUTC(para->hdr.op);
    ENCODE_PUTC(para->hdr.chip_id);
    ENCODE_PUTW(para->hdr.len);
    ENCODE_PUTL(para->index);
    ENCODE_PUTL(para->cmd);
    ENCODE_PUTL(para->fld_val);
    ENCODE_PUTL(para->ret);
    ENCODE_PUTL(para->val_len);
    ENCODE_PUTL(para->mask_len);

    if (para->val_len)
    {
        ENCODE_PUT(para->val, para->val_len);
    }

    if (para->mask_len)
    {
        ENCODE_PUT(para->mask, para->mask_len);
    }

    DRV_EADP_DBG_INFO(g_gg_eadp_drv_debug_en, "encode IO %s,chip %d,len %d,index %d,fld_val %d,cmd 0x%08X(%s,%d,%d),ret %d,len(%d, %d)\n",
                 _goldengate_chip_agent_io_op_str(para->hdr.op), para->hdr.chip_id, para->hdr.len, para->index, para->fld_val, para->cmd,
                 _goldengate_chip_agent_cmd_action_str(DRV_IOC_OP(para->cmd)), DRV_IOC_MEMID(para->cmd), DRV_IOC_FIELDID(para->cmd),
                  para->ret, para->val_len, para->mask_len);
    end = *pnt;
    *req_len = end - start;
    return DRV_CHIP_AGT_E_NONE;
}

int32
_goldengate_chip_agent_decode_io_header(uint8* buf, chip_agent_msg_io_hdr_t* hdr, uint32 len)
{
    int32 pkt_size_left = len;
    int32* size = NULL;
    uint8** pnt = NULL;

    size = &pkt_size_left;
    pnt = &buf;

    if (len == 0)
    {
        return DRV_CHIP_AGT_E_DECODE_ZERO_LEN;
    }

    DECODE_GETC(hdr->op);
    DECODE_GETC(hdr->chip_id);
    DECODE_GETW(hdr->len);
    return DRV_CHIP_AGT_E_NONE;
}

int32
_goldengate_chip_agent_decode_io_ioctl(uint8* buf, chip_agent_msg_io_para_t* para, uint32 len)
{
    int32 pkt_size_left = len;
    int32* size = NULL;
    uint8** pnt = NULL;

    size = &pkt_size_left;
    pnt = &buf;

    if (len == 0)
    {
        return DRV_CHIP_AGT_E_DECODE_ZERO_LEN;
    }

    /* encode IO header */
    DECODE_GETC(para->hdr.op);
    DECODE_GETC(para->hdr.chip_id);
    DECODE_GETW(para->hdr.len);
    DECODE_GETL(para->index);
    DECODE_GETL(para->cmd);
    DECODE_GETL(para->fld_val);
    DECODE_GETL(para->ret);
    DECODE_GETL(para->val_len);
    DECODE_GETL(para->mask_len);
    CHIP_AGT_DECODE_BUF(para->val, para->val_len);
    CHIP_AGT_DECODE_BUF(para->mask, para->mask_len);

    DRV_EADP_DBG_INFO(g_gg_eadp_drv_debug_en, "decode IO %s,chip %d,len %d,index %d,fld_val %d,cmd 0x%08X(%s,%d,%d),ret %d,len(%d, %d)\n",
                 _goldengate_chip_agent_io_op_str(para->hdr.op), para->hdr.chip_id, para->hdr.len, para->index, para->fld_val, para->cmd,
                _goldengate_chip_agent_cmd_action_str(DRV_IOC_OP(para->cmd)), DRV_IOC_MEMID(para->cmd), DRV_IOC_FIELDID(para->cmd),
                 para->ret, para->val_len, para->mask_len);

    if (*size != 0)
    {
        return DRV_CHIP_AGT_E_MSG_LEN;
    }

    return DRV_CHIP_AGT_E_NONE;
}

int32
_goldengate_chip_agent_encode_io_hash_ioctl(uint8* buf, chip_agent_msg_hash_ioctl_para_t* para, uint32* req_len)
{
    int32 pkt_size_left = CHIP_AGT_BUF_SIZE;
    int32* size = NULL;
    uint8** pnt = NULL;
    uint8* start = NULL;
    uint8* end = NULL;

    size = &pkt_size_left;
    start = buf;
    pnt = &buf;

    para->hdr.len = sizeof(chip_agent_msg_hash_ioctl_para_t) - sizeof(para->val) + para->val_len;
    /* encode hash IO header */
    ENCODE_PUTC(para->hdr.op);
    ENCODE_PUTC(para->hdr.chip_id);
    ENCODE_PUTW(para->hdr.len);
    ENCODE_PUTL(para->hash_op);
    ENCODE_PUTL(para->index);
    ENCODE_PUTL(para->table_id);
    ENCODE_PUTL(para->hash_module);
    ENCODE_PUTL(para->hash_type);
    ENCODE_PUTL(para->ret);
    ENCODE_PUTL(para->val_len);

    if (para->val_len)
    {
        ENCODE_PUT(para->val, para->val_len);
    }

    DRV_EADP_DBG_INFO(g_gg_eadp_drv_debug_en, "encode IO %s,chip %d,len %d,hash %s,index %d,table %d,hash_module %d,hash_type %d,ret %d,len %d\n",
                 _goldengate_chip_agent_io_op_str(para->hdr.op), para->hdr.chip_id, para->hdr.len, _goldengate_chip_agent_hash_op_str(para->hash_op),
                 para->index, para->table_id, para->hash_module, para->hash_type, para->ret, para->val_len);

    end = *pnt;
    *req_len = end - start;
    return DRV_CHIP_AGT_E_NONE;
}

int32
_goldengate_chip_agent_decode_io_hash_ioctl(uint8* buf, chip_agent_msg_hash_ioctl_para_t* para, uint32 len)
{
    int32 pkt_size_left = len;
    int32* size = NULL;
    uint8** pnt = NULL;

    size = &pkt_size_left;
    pnt = &buf;

    if (len == 0)
    {
        return DRV_CHIP_AGT_E_DECODE_ZERO_LEN;
    }

    /* decode hash IO header */
    DECODE_GETC(para->hdr.op);
    DECODE_GETC(para->hdr.chip_id);
    DECODE_GETW(para->hdr.len);
    DECODE_GETL(para->hash_op);
    DECODE_GETL(para->index);
    DECODE_GETL(para->table_id);
    DECODE_GETL(para->hash_module);
    DECODE_GETL(para->hash_type);
    DECODE_GETL(para->ret);
    DECODE_GETL(para->val_len);
    CHIP_AGT_DECODE_BUF(para->val, para->val_len);

    DRV_EADP_DBG_INFO(g_gg_eadp_drv_debug_en, "decode IO %s,chip %d,len %d,hash %s,index %d,table %d,hash_module %d,hash_type %d,ret %d,len %d\n",
                 _goldengate_chip_agent_io_op_str(para->hdr.op), para->hdr.chip_id, para->hdr.len, _goldengate_chip_agent_hash_op_str(para->hash_op),
                 para->index, para->table_id, para->hash_module, para->hash_type, para->ret, para->val_len);

    if (*size != 0)
    {
        return DRV_CHIP_AGT_E_MSG_LEN;
    }

    return DRV_CHIP_AGT_E_NONE;
}


int32
_goldengate_chip_agent_encode_io_fib_acc_ioctl(uint8* buf, chip_agent_msg_fib_acc_t* para, uint32* req_len)
{
    int32 pkt_size_left = CHIP_AGT_BUF_SIZE;
    int32* size = NULL;
    uint8** pnt = NULL;
    uint8* start = NULL;
    uint8* end = NULL;

    size = &pkt_size_left;
    start = buf;
    pnt = &buf;

    para->hdr.len = sizeof(chip_agent_msg_fib_acc_t);
    /* encode hash IO header */
    ENCODE_PUTC(para->hdr.op);
    ENCODE_PUTC(para->hdr.chip_id);
    ENCODE_PUTW(para->hdr.len);
    ENCODE_PUTL(para->acc_op_type);
    ENCODE_PUTL(para->ret);

    ENCODE_PUT(para->acc_buf_in, MAX_ACC_BUF_IN*4);
    ENCODE_PUT(para->acc_buf_out, MAX_ACC_BUF_OUT*4);

    DRV_EADP_DBG_INFO(g_gg_eadp_drv_debug_en, "encode IO[%s],chip[%d],len[%d],acc_op_type[%s]\n",
                 _goldengate_chip_agent_io_op_str(para->hdr.op), para->hdr.chip_id, para->hdr.len,
                 _goldengate_chip_agent_cmd_fib_acc_type_str(para->acc_op_type));

    end = *pnt;
    *req_len = end - start;
    return DRV_CHIP_AGT_E_NONE;
}


int32
_goldengate_chip_agent_decode_io_fib_acc_ioctl(uint8* buf, chip_agent_msg_fib_acc_t* para, uint32 len)
{
    int32 pkt_size_left = len;
    int32* size = NULL;
    uint8** pnt = NULL;

    size = &pkt_size_left;
    pnt = &buf;

    if (len == 0)
    {
        return DRV_CHIP_AGT_E_DECODE_ZERO_LEN;
    }

    /* decode hash IO header */
    DECODE_GETC(para->hdr.op);
    DECODE_GETC(para->hdr.chip_id);
    DECODE_GETW(para->hdr.len);
    DECODE_GETL(para->acc_op_type);
    DECODE_GETL(para->ret);

    CHIP_AGT_DECODE_BUF(para->acc_buf_in, MAX_ACC_BUF_IN*4);
    CHIP_AGT_DECODE_BUF(para->acc_buf_out, MAX_ACC_BUF_OUT*4);


    DRV_EADP_DBG_INFO(g_gg_eadp_drv_debug_en, "decode IO[%s],chip[%d],len[%d],acc_op_type[%s]\n",
                 _goldengate_chip_agent_io_op_str(para->hdr.op), para->hdr.chip_id, para->hdr.len,
                 _goldengate_chip_agent_cmd_cpu_acc_type_str(para->acc_op_type));

    if (*size != 0)
    {
        return DRV_CHIP_AGT_E_MSG_LEN;
    }

    return DRV_CHIP_AGT_E_NONE;
}



int32
_goldengate_chip_agent_encode_io_dma_dump_ioctl(uint8* buf, chip_agent_msg_dma_dump_t* para, uint32* req_len)
{
    int32 pkt_size_left = CHIP_AGT_BUF_SIZE;
    int32* size = NULL;
    uint8** pnt = NULL;
    uint8* start = NULL;
    uint8* end = NULL;

    size = &pkt_size_left;
    start = buf;
    pnt = &buf;

    para->hdr.len = sizeof(chip_agent_msg_dma_dump_t);
    /* encode hash IO header */
    ENCODE_PUTC(para->hdr.op);
    ENCODE_PUTC(para->hdr.chip_id);
    ENCODE_PUTW(para->hdr.len);
    ENCODE_PUTL(para->ret);
    ENCODE_PUTW(para->threshold);
    ENCODE_PUTW(para->entry_num);

    if (para->threshold)
    {
        ENCODE_PUT(para->acc_buf_out, MAX_DMA_NUM*4);
    }

    DRV_EADP_DBG_INFO(g_gg_eadp_drv_debug_en, "encode IO[%s],chip[%d],len[%d],ret[%d],threshold[%d],entry_num[%d]\n",
                  _goldengate_chip_agent_io_op_str(para->hdr.op), para->hdr.chip_id, para->hdr.len, para->ret,
                   para->threshold, para->entry_num);

    end = *pnt;
    *req_len = end - start;
    return DRV_CHIP_AGT_E_NONE;
}


int32
_goldengate_chip_agent_decode_io_dma_dump_ioctl(uint8* buf, chip_agent_msg_dma_dump_t* para, uint32 len)
{
    int32 pkt_size_left = len;
    int32* size = NULL;
    uint8** pnt = NULL;

    size = &pkt_size_left;
    pnt = &buf;

    if (len == 0)
    {
        return DRV_CHIP_AGT_E_DECODE_ZERO_LEN;
    }

    /* decode hash IO header */
    DECODE_GETC(para->hdr.op);
    DECODE_GETC(para->hdr.chip_id);
    DECODE_GETW(para->hdr.len);
    DECODE_GETL(para->ret);
    DECODE_GETW(para->threshold);
    DECODE_GETW(para->entry_num);

    /*threshold is 0 means clear dma channel data, donot need data, so para->acc_buf_out is NULL*/
    if (para->threshold)
    {
        CHIP_AGT_DECODE_BUF(para->acc_buf_out, MAX_DMA_NUM*4);
    }

    DRV_EADP_DBG_INFO(g_gg_eadp_drv_debug_en, "decode IO[%s],chip[%d],len[%d],ret[%d],threshold[%d],entry_num[%d]\n",
                  _goldengate_chip_agent_io_op_str(para->hdr.op), para->hdr.chip_id, para->hdr.len, para->ret,
                   para->threshold, para->entry_num);

    if (*size != 0)
    {
        return DRV_CHIP_AGT_E_MSG_LEN;
    }

    return DRV_CHIP_AGT_E_NONE;
}

int32
_goldengate_chip_agent_encode_io_dma_rw_ioctl(uint8* buf, chip_agent_msg_dma_rw_t* para, uint32* req_len)
{
    int32 pkt_size_left = CHIP_AGT_BUF_SIZE;
    int32* size = NULL;
    uint8** pnt = NULL;
    uint8* start = NULL;
    uint8* end = NULL;

    size = &pkt_size_left;
    start = buf;
    pnt = &buf;

    para->hdr.len = sizeof(chip_agent_msg_dma_rw_t);
    /* encode hash IO header */
    ENCODE_PUTC(para->hdr.op);
    ENCODE_PUTC(para->hdr.chip_id);
    ENCODE_PUTW(para->hdr.len);
    ENCODE_PUTL(para->ret);
    ENCODE_PUTL(para->tbl_addr);
    ENCODE_PUTW(para->entry_len);
    ENCODE_PUTW(para->entry_num);
    ENCODE_PUTC(para->rflag);
    ENCODE_PUTC(para->is_pause);
    ENCODE_PUT(para->buf_out, (para->entry_len)*(para->entry_num));

    DRV_EADP_DBG_INFO(g_gg_eadp_drv_debug_en, "encode IO[%s],chip[%d],len[%d],ret[%d],tbl_addr[0x%x],entry_len[%d],entry_num[%d],rflag[%d],is_pause[%d]\n",
                  _goldengate_chip_agent_io_op_str(para->hdr.op), para->hdr.chip_id, para->hdr.len, para->ret,
                   para->tbl_addr, para->entry_len, para->entry_num, para->rflag, para->is_pause);

    end = *pnt;
    *req_len = end - start;
    return DRV_CHIP_AGT_E_NONE;
}

int32
_goldengate_chip_agent_decode_io_dma_rw_ioctl(uint8* buf, chip_agent_msg_dma_rw_t* para, uint32 len)
{
    int32 pkt_size_left = len;
    int32* size = NULL;
    uint8** pnt = NULL;

    size = &pkt_size_left;
    pnt = &buf;

    if (len == 0)
    {
        return DRV_CHIP_AGT_E_DECODE_ZERO_LEN;
    }

    /* decode hash IO header */
    DECODE_GETC(para->hdr.op);
    DECODE_GETC(para->hdr.chip_id);
    DECODE_GETW(para->hdr.len);
    DECODE_GETL(para->ret);
    DECODE_GETL(para->tbl_addr);
    DECODE_GETW(para->entry_len);
    DECODE_GETW(para->entry_num);
    DECODE_GETC(para->rflag);
    DECODE_GETC(para->is_pause);


    CHIP_AGT_DECODE_BUF(para->buf_out, (para->entry_len)*(para->entry_num));


    DRV_EADP_DBG_INFO(g_gg_eadp_drv_debug_en, "decode IO[%s],chip[%d],len[%d],ret[%d],tbl_addr[0x%x],entry_len[%d],entry_num[%d],rflag[%d],is_pause[%d]\n",
                  _goldengate_chip_agent_io_op_str(para->hdr.op), para->hdr.chip_id, para->hdr.len, para->ret,
                   para->tbl_addr, para->entry_len, para->entry_num, para->rflag, para->is_pause);

    if (*size != 0)
    {
        return DRV_CHIP_AGT_E_MSG_LEN;
    }

    return DRV_CHIP_AGT_E_NONE;
}

int32
_goldengate_chip_agent_encode_io_hash_lookup(uint8* buf, chip_agent_msg_hash_lookup_para_t* para, uint32* req_len)
{
    int32 pkt_size_left = CHIP_AGT_BUF_SIZE;
    int32* size = NULL;
    uint8** pnt = NULL;
    uint8* start = NULL;
    uint8* end = NULL;

    size = &pkt_size_left;
    start = buf;
    pnt = &buf;

    para->hdr.len = sizeof(chip_agent_msg_hash_lookup_para_t) - sizeof(para->val) + para->val_len;

    ENCODE_PUTC(para->hdr.op);
    ENCODE_PUTC(para->hdr.chip_id);
    ENCODE_PUTW(para->hdr.len);
    ENCODE_PUTL(para->key_index);
    ENCODE_PUTL(para->ad_index);
    ENCODE_PUTC(para->hash_module);
    ENCODE_PUTC(para->hash_type);
    ENCODE_PUTC(para->valid);
    ENCODE_PUTC(para->free);
    ENCODE_PUTC(para->conflict);
    ENCODE_PUT_EMPTY(3);
    ENCODE_PUTL(para->ret);
    ENCODE_PUTL(para->val_len);

    if (para->val_len)
    {
        ENCODE_PUT(para->val, para->val_len);
    }

    DRV_EADP_DBG_INFO(g_gg_eadp_drv_debug_en, "encode IO %s,chip %d,len %d,key_index %d,ad_index %d,"
                 "hash_module %d,hash_type %d,valid %d,para->free %d,conflict %d,ret %d,len %d\n",
                 _goldengate_chip_agent_io_op_str(para->hdr.op), para->hdr.chip_id, para->hdr.len, para->key_index, para->ad_index,
                  para->hash_module, para->hash_type, para->valid, para->free, para->conflict, para->ret, para->val_len);

    end = *pnt;
    *req_len = end - start;
    return DRV_CHIP_AGT_E_NONE;
}

int32
_goldengate_chip_agent_decode_io_hash_lookup(uint8* buf, chip_agent_msg_hash_lookup_para_t* para, uint32 len)
{
    int32 pkt_size_left = len;
    int32* size = NULL;
    uint8** pnt = NULL;

    size = &pkt_size_left;
    pnt = &buf;

    if (len == 0)
    {
        return DRV_CHIP_AGT_E_DECODE_ZERO_LEN;
    }

    DECODE_GETC(para->hdr.op);
    DECODE_GETC(para->hdr.chip_id);
    DECODE_GETW(para->hdr.len);
    DECODE_GETL(para->key_index);
    DECODE_GETL(para->ad_index);
    DECODE_GETC(para->hash_module);
    DECODE_GETC(para->hash_type);
    DECODE_GETC(para->valid);
    DECODE_GETC(para->free);
    DECODE_GETC(para->conflict);
    DECODE_GET_EMPTY(3);
    DECODE_GETL(para->ret);
    DECODE_GETL(para->val_len);
    CHIP_AGT_DECODE_BUF(para->val, para->val_len);

    DRV_EADP_DBG_INFO(g_gg_eadp_drv_debug_en, "decode IO %s,chip %d,len %d,key_index %d,ad_index %d,"
                 "hash_module %d,hash_type %d,valid %d,para->free %d,conflict %d,ret %d,len %d\n",
                 _goldengate_chip_agent_io_op_str(para->hdr.op), para->hdr.chip_id, para->hdr.len, para->key_index, para->ad_index,
                 para->hash_module, para->hash_type, para->valid, para->free, para->conflict, para->ret, para->val_len);

    if (*size != 0)
    {
        return DRV_CHIP_AGT_E_MSG_LEN;
    }

    return DRV_CHIP_AGT_E_NONE;
}

int32
_goldengate_chip_agent_encode_io_tcam_remove(uint8* buf, chip_agent_msg_tcam_remove_para_t* para, uint32* req_len)
{
    int32 pkt_size_left = CHIP_AGT_BUF_SIZE;
    int32* size = NULL;
    uint8** pnt = NULL;
    uint8* start = NULL;
    uint8* end = NULL;

    size = &pkt_size_left;
    start = buf;
    pnt = &buf;

    para->hdr.len = sizeof(chip_agent_msg_tcam_remove_para_t);
    ENCODE_PUTC(para->hdr.op);
    ENCODE_PUTC(para->hdr.chip_id);
    ENCODE_PUTW(para->hdr.len);
    ENCODE_PUTL(para->index);
    ENCODE_PUTL(para->table_id);
    ENCODE_PUTL(para->ret);

    DRV_EADP_DBG_INFO(g_gg_eadp_drv_debug_en, "encode IO %s,chip %d,len %d,index %d,table %d,ret %d\n",
                 _goldengate_chip_agent_io_op_str(para->hdr.op), para->hdr.chip_id, para->hdr.len, para->index, para->table_id, para->ret);

    end = *pnt;
    *req_len = end - start;
    return DRV_CHIP_AGT_E_NONE;
}

int32
_goldengate_chip_agent_decode_io_tcam_remove(uint8* buf, chip_agent_msg_tcam_remove_para_t* para, uint32 len)
{
    int32 pkt_size_left = len;
    int32* size = NULL;
    uint8** pnt = NULL;

    size = &pkt_size_left;
    pnt = &buf;

    if (len == 0)
    {
        return DRV_CHIP_AGT_E_DECODE_ZERO_LEN;
    }

    DECODE_GETC(para->hdr.op);
    DECODE_GETC(para->hdr.chip_id);
    DECODE_GETW(para->hdr.len);
    DECODE_GETL(para->index);
    DECODE_GETL(para->table_id);
    DECODE_GETL(para->ret);

    DRV_EADP_DBG_INFO(g_gg_eadp_drv_debug_en, "decode IO %s,chip %d,len %d,index %d,table %d,ret %d\n",
                 _goldengate_chip_agent_io_op_str(para->hdr.op), para->hdr.chip_id, para->hdr.len, para->index, para->table_id, para->ret);

    if (*size != 0)
    {
        return DRV_CHIP_AGT_E_MSG_LEN;
    }

    return DRV_CHIP_AGT_E_NONE;
}

int32
_goldengate_chip_agent_encode_dma(uint8* buf, chip_agent_msg_dma_para_t* para, uint32* req_len)
{
    int32 pkt_size_left = CHIP_AGT_BUF_SIZE;
    int32* size = NULL;
    uint8** pnt = NULL;
    uint8* start = NULL;
    uint8* end = NULL;
     /* int32 i = 0;*/


    size = &pkt_size_left;
    start = buf;
    pnt = &buf;

    /* encode DMA header */
    ENCODE_PUTC(para->op);
    ENCODE_PUTC(para->chip_id);
    ENCODE_PUTW(para->num);

    ENCODE_PUT(para->data_buf, MAX_DMA_NUM*4);
    ENCODE_PUT(para->data_ext, MAX_DMA_EXT_NUM*4);

#if 0
    for (i = 0; i < para->num; i++)
    {
        ENCODE_PUTL(p_info[i].key_index);
        ENCODE_PUT(p_info[i].mac, 6);
        ENCODE_PUTW(p_info[i].vsi_id);
        ENCODE_PUTW(p_info[i].damac_index);
        ENCODE_PUTC(p_info[i].is_mac_hash);
        ENCODE_PUTC(p_info[i].valid);
        ENCODE_PUTC(p_info[i].is_aging);

        /* DRV_CHIP_AGT_DBG_CODE(DRV_CHIP_AGT_STR "aging %d, keyindex %d, adindex %d, vsi %d, MAC %02X%02X.%02X%02X.%02X%02X\n",
            p_info[i].is_aging,
            p_info[i].key_index,
            p_info[i].damac_index,
            p_info[i].vsi_id,
            p_info[i].mac[0],
            p_info[i].mac[1],
            p_info[i].mac[2],
            p_info[i].mac[3],
            p_info[i].mac[4],
            p_info[i].mac[5]);*/
    }
#endif

    DRV_EADP_DBG_INFO(g_gg_eadp_drv_debug_en, "encode DMA %s,chip %d,num %d\n",
                  _chip_agent_dma_op_str(para->op), para->chip_id, para->num);
    end = *pnt;
    *req_len = end - start;

    return DRV_CHIP_AGT_E_NONE;
}

int32
_goldengate_chip_agent_decode_dma(uint8* buf, chip_agent_msg_dma_para_t* para, uint32 len)
{
    int32 pkt_size_left = len;
    int32* size = NULL;
    uint8** pnt = NULL;
     /* int32 i = 0;*/
     /* drv_learn_aging_info_t* p_info = (drv_goldengate_learn_aging_info_t*)para->val;*/


    size = &pkt_size_left;
    pnt = &buf;

    if (len == 0)
    {
        return DRV_CHIP_AGT_E_DECODE_ZERO_LEN;
    }

    /* encode DMA header */
    DECODE_GETC(para->op);
    DECODE_GETC(para->chip_id);
    DECODE_GETW(para->num);

    CHIP_AGT_DECODE_BUF(para->data_buf, MAX_DMA_NUM*4);
    CHIP_AGT_DECODE_BUF(para->data_ext, MAX_DMA_EXT_NUM*4);
#if 0
    for (i = 0; i < para->num; i++)
    {
        DECODE_GETL(p_info[i].key_index);
        DECODE_GET(p_info[i].mac, 6);
        DECODE_GETW(p_info[i].vsi_id);
        DECODE_GETW(p_info[i].damac_index);
        DECODE_GETC(p_info[i].is_mac_hash);
        DECODE_GETC(p_info[i].valid);
        DECODE_GETC(p_info[i].is_aging);

        /*DRV_CHIP_AGT_DBG_CODE(DRV_CHIP_AGT_STR "aging %d, keyindex %d, adindex %d, vsi %d, MAC %02X%02X.%02X%02X.%02X%02X\n",
            p_info[i].is_aging,
            p_info[i].key_index,
            p_info[i].damac_index,
            p_info[i].vsi_id,
            p_info[i].mac[0],
            p_info[i].mac[1],
            p_info[i].mac[2],
            p_info[i].mac[3],
            p_info[i].mac[4],
            p_info[i].mac[5]);*/
    }
#endif

    DRV_EADP_DBG_INFO(g_gg_eadp_drv_debug_en, "decode DMA %s,chip %d,num %d\n",
                 _chip_agent_dma_op_str(para->op), para->chip_id, para->num);

    if (*size != 0)
    {
        return DRV_CHIP_AGT_E_MSG_LEN;
    }

    return DRV_CHIP_AGT_E_NONE;
}


int32
_chip_agent_swap32(uint32* data, int32 len, uint8 hton)
{
    int32 cnt;

    for (cnt = 0; cnt < len; cnt++)
    {
        if (hton)
        {
            data[cnt] = sal_htonl(data[cnt]);
        }
        else
        {
            data[cnt] = sal_ntohl(data[cnt]);
        }
    }

    return DRV_CHIP_AGT_E_NONE;
}

int32
_goldengate_chip_agent_fib_acc_type_copy_data(uint8 fib_acc_type, chip_agent_msg_fib_acc_t* para, drv_fib_acc_in_t* in, drv_fib_acc_out_t* out)
{
    switch (fib_acc_type)
    {
    case DRV_FIB_ACC_WRITE_MAC_BY_IDX:
    case DRV_FIB_ACC_WRITE_MAC_BY_KEY:
    case DRV_FIB_ACC_DEL_MAC_BY_IDX:
    case DRV_FIB_ACC_DEL_MAC_BY_KEY:
    case DRV_FIB_ACC_LKP_MAC:
        sal_memcpy((void*)para->acc_buf_in, (void*)&(in->mac),sizeof(in->mac));
        sal_memcpy((void*)para->acc_buf_out, (void*)&(out->mac),sizeof(out->mac));
        break;

    case DRV_FIB_ACC_FLUSH_MAC_BY_PORT_VLAN:
    case DRV_FIB_ACC_FLUSH_MAC_ALL:
    case DRV_FIB_ACC_FLUSH_MAC_BY_MAC_PORT:
        sal_memcpy((void*)para->acc_buf_in, (void*)&(in->mac_flush),sizeof(in->mac_flush));
        sal_memcpy((void*)para->acc_buf_out, (void*)&(out->mac),sizeof(out->mac));
        break;

    case DRV_FIB_ACC_LKP_FIB0:
    case DRV_FIB_ACC_WRITE_FIB0_BY_KEY:
        sal_memcpy((void*)para->acc_buf_in, (void*)&(in->fib),sizeof(in->fib));
        sal_memcpy((void*)(para->acc_buf_in+1), (in->fib.data),32*4);
        sal_memcpy((void*)para->acc_buf_out, (void*)&(out->fib),sizeof(out->fib));
        break;

    case DRV_FIB_ACC_READ_FIB0_BY_IDX:
        sal_memcpy((void*)para->acc_buf_in, (void*)&(in->rw),sizeof(in->rw));
        sal_memcpy((void*)para->acc_buf_out, (void*)&(out->read),sizeof(out->read));
        break;

    case DRV_FIB_ACC_WRITE_FIB0_BY_IDX:
        sal_memcpy((void*)para->acc_buf_in, (void*)&(in->rw),sizeof(in->rw));
        if(in->rw.data)
        {
            sal_memcpy((void*)(para->acc_buf_in+2), (in->rw.data),32*4);
        }
        sal_memcpy((void*)para->acc_buf_out, (void*)&(out->read),sizeof(out->read));
        break;

    case DRV_FIB_ACC_UPDATE_MAC_LIMIT:
    case DRV_FIB_ACC_READ_MAC_LIMIT:
    case DRV_FIB_ACC_WRITE_MAC_LIMIT:
        sal_memcpy((void*)para->acc_buf_in, (void*)&(in->mac_limit),sizeof(in->mac_limit));
        sal_memcpy((void*)para->acc_buf_out, (void*)&(out->mac_limit),sizeof(out->mac_limit));
        break;

    case DRV_FIB_ACC_DUMP_MAC_BY_PORT_VLAN:
    case DRV_FIB_ACC_DUMP_MAC_ALL:
        sal_memcpy((void*)para->acc_buf_in, (void*)&(in->dump),sizeof(in->dump));
        sal_memcpy((void*)para->acc_buf_out, (void*)&(out->dump),sizeof(out->dump));
        break;

    default:
        return DRV_E_INVALID_PARAMETER;
    }

    return DRV_E_NONE;

}


int32
_goldengate_chip_agent_cpu_acc_type_copy_data(uint8 cpu_acc_type, chip_agent_msg_fib_acc_t* para, drv_cpu_acc_in_t* in, drv_cpu_acc_out_t* out)
{

    sal_memcpy((void*)para->acc_buf_in, (void*)(in),sizeof(drv_cpu_acc_in_t));

    switch (cpu_acc_type)
    {
    case    DRV_CPU_DEL_ACC_FIB_HOST1_BY_IDX:
    case    DRV_CPU_READ_ACC_FIB_HOST1_BY_IDX:
    case    DRV_CPU_DEL_ACC_USER_ID_BY_IDX:
    case    DRV_CPU_READ_ACC_USER_ID_BY_IDX:
    case    DRV_CPU_DEL_ACC_XC_OAM_BY_IDX:
    case    DRV_CPU_READ_ACC_XC_OAM_BY_IDX:
    case    DRV_CPU_DEL_ACC_FLOW_HASH_BY_IDX:
    case    DRV_CPU_READ_ACC_FLOW_HASH_BY_IDX:
        break;
    default:
        if(in->data)
        {
            sal_memcpy((void*)(para->acc_buf_in+2), (void*)(in->data),32*4);
        }
        break;
    }

    if(out)
    {
        sal_memcpy((void*)para->acc_buf_out, (void*)(out),sizeof(drv_cpu_acc_out_t));
    }

    return DRV_E_NONE;
}


int32
_goldengate_chip_agent_ipfix_acc_type_copy_data(chip_agent_msg_fib_acc_t* para, drv_ipfix_acc_in_t* in, drv_ipfix_acc_out_t* out)
{
    sal_memcpy((void*)para->acc_buf_in, (void*)(in),sizeof(drv_ipfix_acc_in_t));
    if (in->data)
    {
        sal_memcpy((void*)(para->acc_buf_in + 3), (void*)(in->data), 16*4);
    }
    sal_memcpy((void*)para->acc_buf_out, (void*)(out), sizeof(drv_ipfix_acc_out_t));

    return DRV_E_NONE;
}

int32
_goldengate_chip_agent_decode_gg_header(uint8* buffer, int32 len, ms_packet_header_t* p_header)
{
    if (len < GG_HEADER_LEN + GG_CPUMAC_HDR_LEN)
    {
        return DRV_E_WRONG_SIZE;
    }

    /* GG Bridge-Header */
    sal_memcpy(p_header, buffer + GG_CPUMAC_HDR_LEN, sizeof(ms_packet_header_t));
    DRV_IF_ERROR_RETURN(_chip_agent_swap32((uint32*)p_header, GG_HEADER_LEN / 4, FALSE));

    return DRV_E_NONE;
}



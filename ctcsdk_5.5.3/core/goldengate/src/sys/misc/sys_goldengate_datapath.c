/**
 @file sys_goldengate_datapath.c

 @date 2014-10-28

 @version v3.0


*/

/****************************************************************
 *
 * Header Files
 *
 ***************************************************************/
#include "ctc_error.h"
#include "ctc_port.h"
#include "sys_goldengate_chip.h"
#include "sys_goldengate_datapath.h"
#include "sys_goldengate_port.h"
#include "sys_goldengate_register.h"
#include "sys_goldengate_queue_enq.h"
#include "sys_goldengate_wb_common.h"
#include "goldengate/include/drv_enum.h"
#include "goldengate/include/drv_io.h"
#include "goldengate/include/drv_chip_ctrl.h"

int32
sys_goldengate_datapath_wb_sync(uint8 lchip);
int32
sys_goldengate_datapath_wb_restore(uint8 lchip);
extern int32
sys_goldengate_queue_set_port_drop_en(uint8 lchip, uint16 gport, bool enable);
extern int32
sys_goldengate_queue_get_port_depth(uint8 lchip, uint16 gport, uint32* p_depth);
extern int32
sys_goldengate_port_set_mac_mode(uint8 lchip, uint8 slice_id, uint16 mac_id, uint8 mode);
extern int32
sys_goldengate_port_set_full_thrd(uint8 lchip, uint8 slice_id, uint16 mac_id, uint8 mode);
extern int32
sys_goldengate_queue_serdes_drop_init(uint8 lchip, uint16 port_id);

struct sys_datapath_master_s
{
    ctc_vector_t* p_hss_vector; /* store hss and serdes info */
    sys_datapath_lport_attr_t port_attr[SYS_MAX_LOCAL_SLICE_NUM][256];
    uint8 oam_chan;
    uint8 dma_chan;
    uint8 iloop_chan;
    uint8 eloop_chan;
    uint16 core_plla;   /*used for clock core*/
    uint16 core_pllb;   /*used for mdio/macled etc*/
    uint8 overclock_count; /*userd for switch nettx thrld*/

    SYS_CALLBACK_DATAPATH_FUN_P    datapath_update_func;   /* update datapath mapping for bpe*/
};
typedef struct sys_datapath_master_s sys_datapath_master_t;

sys_datapath_master_t* p_gg_datapath_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};

#define CG0_CHAN 36
#define CG1_CHAN 44
#define BUFRETRV_MEM_MAX_DEPTH 320

#define SYS_DATAPATH_HSS_IS_HSS28G(idx) \
    ((((idx) >= 5) && ((idx) <= 6)) || (((idx) >= 12) && ((idx) <= 13)))

#define SYS_DATAPATH_MAP_HSSID_TO_HSSIDX(id, type)  \
    (SYS_DATAPATH_HSS_TYPE_15G == type) ? (((id) >= HSS15G_NUM_PER_SLICE)  ? (((id) - HSS15G_NUM_PER_SLICE) + 7) : (id)) \
    : (((id) >= HSS28G_NUM_PER_SLICE) ? (((id) - HSS28G_NUM_PER_SLICE) + 12) : ((id) + HSS15G_NUM_PER_SLICE))

#define SYS_DATAPATH_MAP_SERDES_TO_HSSID(serdes_id) \
    SYS_DATAPATH_SERDES_IS_HSS28G(serdes_id) \
    ?((((serdes_id)% SERDES_NUM_PER_SLICE) - 40)/HSS28G_LANE_NUM + ((serdes_id)/SERDES_NUM_PER_SLICE)*HSS28G_NUM_PER_SLICE) \
    :(((serdes_id)% SERDES_NUM_PER_SLICE)/HSS15G_LANE_NUM + ((serdes_id)/SERDES_NUM_PER_SLICE)*HSS15G_NUM_PER_SLICE)

#define SYS_DATAPATH_MAP_SERDES_TO_HSS_INDEX(serdes_id) \
    SYS_DATAPATH_SERDES_IS_HSS28G(serdes_id) \
    ?((((serdes_id)% SERDES_NUM_PER_SLICE) - 40)/HSS28G_LANE_NUM + (((serdes_id)/SERDES_NUM_PER_SLICE)?12:5)) \
    :(((serdes_id)% SERDES_NUM_PER_SLICE)/HSS15G_LANE_NUM + ((serdes_id)/SERDES_NUM_PER_SLICE)*7)

#define SYS_DATAPATH_MAP_SERDES_TO_LANE_ID(serdes_id) \
    SYS_DATAPATH_SERDES_IS_HSS28G(serdes_id) \
    ?(((serdes_id% SERDES_NUM_PER_SLICE) - 40)%HSS28G_LANE_NUM) \
    :((serdes_id% SERDES_NUM_PER_SLICE)%HSS15G_LANE_NUM)

#define SYS_DATAPATH_MAP_SERDES_TO_MAC(id, shift) (((id) >>shift)<<shift)

#define SYS_DATAPATH_GET_HSS15G_MULT(clk, mult) \
{   \
    if ((clk) == SYS_DATAPATH_HSSCLK_515DOT625)  \
    {  \
        (mult) = 0x58;  \
    }  \
    else if ((clk) == SYS_DATAPATH_HSSCLK_500)  \
    {  \
        (mult) = 0x2c;  \
    }  \
    else if ((clk) == SYS_DATAPATH_HSSCLK_625)  \
    {  \
        (mult) = 0x6f;  \
    }  \
    else if ((clk) == SYS_DATAPATH_HSSCLK_567DOT1875)  \
    {  \
        (mult) = 0x10D;  \
    }  \
    else if ((clk) == SYS_DATAPATH_HSSCLK_644DOT53125)  \
    {  \
        (mult) = 0x13D;  \
    }  \
}

#define SYS_DATAPATH_GET_HSS28G_MULT(clk, mult) \
{   \
    if ((clk) == SYS_DATAPATH_HSSCLK_515DOT625)  \
    {  \
        (mult) = 0x38;  \
    }  \
    else if ((clk) == SYS_DATAPATH_HSSCLK_500)  \
    {  \
        (mult) = 0x0e;  \
    }  \
    else if ((clk) == SYS_DATAPATH_HSSCLK_625)  \
    {  \
        (mult) = 0x4a;  \
    }  \
    else if ((clk) == SYS_DATAPATH_HSSCLK_644DOT53125)  \
    {  \
        (mult) = 0x157;  \
    }  \
}

#define SYS_DATAPATH_INIT_CHECK(lchip)          \
do {                                        \
    SYS_LCHIP_CHECK_ACTIVE(lchip);               \
    if (NULL == p_gg_datapath_master[lchip])   \
    {                                       \
        return CTC_E_NOT_INIT;              \
    }                                       \
} while(0)

#define SYS_DATAPATH_GET_CLKTREE_DIV(div)  \
(((div) == SYS_DATAPATH_CLKTREE_DIV_FULL)?0:(((div) == SYS_DATAPATH_CLKTREE_DIV_HALF) \
    ?1:(((div) == SYS_DATAPATH_CLKTREE_DIV_QUAD)?3:7)))

#define SYS_DATAPATH_HSS_REG_MASK ((1<<SYS_DATAPATH_HSS_LINK_EN_BIT) \
    |(0x1<<SYS_DATAPATH_HSS_WR_MODE_BIT)|(0x3<<SYS_DATAPATH_HSS_PLL_SEL_BIT) \
    |(0x3<<SYS_DATAPATH_HSS_BW_SEL_BIT) \
    |(0x3<<SYS_DATAPATH_HSS_RATE_SEL_BIT))

#define SYS_DATAPATH_NETTX_BUF(speed) ((speed >= SYS_DATAPATH_SPEED_40G)?64:16)

#define SYS_DATAPATH_NEED_EXTERN(mode) \
    (((mode==CTC_CHIP_SERDES_XAUI_MODE) || (mode==CTC_CHIP_SERDES_DXAUI_MODE) || \
    (mode==CTC_CHIP_SERDES_XLG_MODE) || (mode==CTC_CHIP_SERDES_CG_MODE))?1:0)

#define DRV_CHIP_READ_HSS(hss_type, lchip, hss_id, address, p_value) \
{    if(hss_type) \
    {   \
        CTC_ERROR_RETURN(drv_goldengate_chip_read_hss28g(lchip, hss_id, address, p_value)); \
    }   \
    else \
    {   \
        CTC_ERROR_RETURN(drv_goldengate_chip_read_hss15g(lchip, hss_id, address, p_value)); \
    }\
}

#define DRV_CHIP_WRITE_HSS(hss_type, lchip, hss_id, address, p_value) \
{    if(hss_type) \
    {   \
        drv_goldengate_chip_write_hss28g(lchip, hss_id, address, p_value); \
    }   \
    else \
    {   \
        drv_goldengate_chip_write_hss15g(lchip, hss_id, address, p_value); \
    }\
}

#define LANE_ID_MAP_INTERAL_LANE_ID(type, lane_id) (type == SYS_DATAPATH_HSS_TYPE_28G)?(lane_id):(g_gg_hss15g_lane_map[lane_id])

/*Hss15G internal lane: A, E, F, B, C, G, H, D*/
uint8 g_gg_hss15g_lane_map[HSS15G_LANE_NUM] = {0, 4, 5, 1, 2, 6, 7, 3};

/*for hss28g , E,F,G,H is not used */
uint8 g_gg_hss_tx_addr_map[HSS15G_LANE_NUM] =
{
    DRV_HSS_TX_LINKA_ADDR, DRV_HSS_TX_LINKB_ADDR, DRV_HSS_TX_LINKC_ADDR, DRV_HSS_TX_LINKD_ADDR,
    DRV_HSS_TX_LINKE_ADDR, DRV_HSS_TX_LINKF_ADDR, DRV_HSS_TX_LINKG_ADDR, DRV_HSS_TX_LINKH_ADDR
};

/*for hss28g , E,F,G,H is not used */
uint8 g_gg_hss_rx_addr_map[HSS15G_LANE_NUM] =
{
    DRV_HSS_RX_LINKA_ADDR, DRV_HSS_RX_LINKB_ADDR, DRV_HSS_RX_LINKC_ADDR, DRV_HSS_RX_LINKD_ADDR,
    DRV_HSS_RX_LINKE_ADDR, DRV_HSS_RX_LINKF_ADDR, DRV_HSS_RX_LINKG_ADDR, DRV_HSS_RX_LINKH_ADDR
};

struct sys_datapath_netrx_buf_s
{
    uint8 buf_depth;
    uint8 buf_thrdhi;
    uint8 buf_thrdlow;
    uint8 buf_ptrnum;
};
typedef struct sys_datapath_netrx_buf_s sys_datapath_netrx_buf_t;

enum sys_datapath_hss_cfg_bit_e
{
    SYS_DATAPATH_HSS_RATE_SEL_BIT = 0,
    SYS_DATAPATH_HSS_BW_SEL_BIT = 2,
    SYS_DATAPATH_HSS_DFE_BIT = 4,
    SYS_DATAPATH_HSS_PLL_SEL_BIT = 6,
    SYS_DATAPATH_HSS_WR_MODE_BIT = 13,
    SYS_DATAPATH_HSS_LINK_RST = 14,
    SYS_DATAPATH_HSS_LINK_EN_BIT = 15
};
typedef enum sys_datapath_hss_cfg_bit_e sys_datapath_hss_cfg_bit_t;

enum sys_datapath_bit_width_e
{
    SYS_DATAPATH_BIT_WIDTH_8BIT,
    SYS_DATAPATH_BIT_WIDTH_10BIT,
    SYS_DATAPATH_BIT_WIDTH_20BIT,
    SYS_DATAPATH_BIT_WIDTH_40BIT
};
typedef enum sys_datapath_bit_width_e sys_datapath_bit_width_t;

enum sys_datapath_serdes_div_e
{
    SYS_DATAPATH_SERDES_DIV_FULL,
    SYS_DATAPATH_SERDES_DIV_HALF,
    SYS_DATAPATH_SERDES_DIV_QUAD,
    SYS_DATAPATH_SERDES_DIV_EIGHTH
};
typedef enum sys_datapath_serdes_div_e sys_datapath_serdes_div_t;

enum sys_datapath_clktree_div_e
{
    SYS_DATAPATH_CLKTREE_DIV_USELESS,
    SYS_DATAPATH_CLKTREE_DIV_FULL,
    SYS_DATAPATH_CLKTREE_DIV_HALF,
    SYS_DATAPATH_CLKTREE_DIV_QUAD,
    SYS_DATAPATH_CLKTREE_DIV_EIGHTH
};
typedef enum sys_datapath_clktree_div_e sys_datapath_clktree_div_t;

#define QMGRRA_CHAN_CREDIT_STEP0 4
#define QMGRRA_CHAN_CREDIT_STEP1 8
#define QMGRRA_CHAN_CREDIT_STEP2 30

extern int32 sys_goldengate_port_get_mac_en(uint8 lchip, uint16 gport, uint32* p_enable);
/****************************************************************************
 *
* Defines and Macros
*
*****************************************************************************/
#define __DEBUG__

STATIC int32
_sys_goldengate_datapath_show_calendar(uint8 lchip, uint32 start, uint32 end, uint8 is_all)
{
    uint8 start_idx = 0;
    uint8 end_idx = 0;
    uint8 index = 0;
    uint8 sub_idx = 0;
    uint32 cmd = 0;
    uint8 slice_id = 0;
    NetTxCal0_m cal_entry;
    uint8 mac_id[8] = {0};
    uint8 block_id = 0;
    char* block_str[4] = {"Txqm0", "Txqm1", "Cxqm0", "Cxqm1"};

    SYS_DATAPATH_INIT_CHECK(lchip);

    start_idx = 0;
    end_idx = 64;

    for (slice_id = 0; slice_id < 2; slice_id++)
    {
        SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"Slice %d Calendar Information:\n", slice_id);

        for (index = start_idx; index < end_idx; index++)
        {
            block_id = index/16;
            if ((index%16) == 0)
            {
                SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"%s \n", block_str[block_id]);
                SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"-------------------------------------------------- \n");
            }

            if ((index%8) == 0)
            {
                for (sub_idx = 0; sub_idx < 8; sub_idx++)
                {
                    cmd = DRV_IOR((NetTxCal0_t+slice_id), DRV_ENTRY_FLAG);
                    DRV_IOCTL(lchip, index+sub_idx, cmd, &cal_entry);
                    mac_id[sub_idx] = GetNetTxCal0(V, calEntry_f, &cal_entry);
                }

                SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"%-12s %-4d %-4d %-4d %-4d %-4d %-4d %-4d %-4d \n", "Entry index:", index, index+1,
                    index+2, index+3, index+4, index+5, index+6, index+7);
                SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"%-12s %-4d %-4d %-4d %-4d %-4d %-4d %-4d %-4d \n", "Mac Id",mac_id[0], mac_id[1],
                    mac_id[2], mac_id[3], mac_id[4], mac_id[5], mac_id[6], mac_id[7]);
            }

           if (((index+1)%16) == 0)
            {
                SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"-------------------------------------------------- \n");
                SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"\n");
            }
        }
    }
    return 0;
}

STATIC int32
_sys_goldengate_datapath_show_clktree(uint8 lchip, uint32 start, uint32 end, uint8 is_all)
{
    uint32 cmd = 0;
    uint32 tb_id = 0;
    HsClkTreeCfg0_m hs_clk_tree;
    CsClkTreeCfg0_m cs_clk_tree;
    uint32 TxIntfClkSel = 0;
    uint32 TxCoreClkSel = 0;
    uint32 CoreA_sel = 0;
    uint32 CoreB_sel = 0;
    uint32 intf_sel = 0;
    uint32 fld_id = 0;
    sys_datapath_hss_attribute_t* p_hss = NULL;
    uint8 start_idx = 0;
    uint8 end_idx = 0;
    uint8 lane_idx = 0;
    uint8 index = 0;
    uint8 internal_idx = 0;
    char* pll_str[SYS_DATAPATH_HSSCLK_MAX] = {"Disable","515.625M","500M", "644.53125M","625M", "567.1875M"};
    char* div_str[5] = {"Disable","FULL","HALF", "QUAD","EIGHTH"};

    SYS_DATAPATH_INIT_CHECK(lchip);

    if (is_all)
    {
        start_idx = 0;
        end_idx = SYS_DATAPATH_HSS_NUM;
    }
    else
    {
        start_idx = start;
        end_idx = end+1;
    }

    for (index = start_idx; index < end_idx; index++)
    {
        p_hss = ctc_vector_get(p_gg_datapath_master[lchip]->p_hss_vector, index);
        if (NULL == p_hss)
        {
            continue;
        }
        SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"Hssid  %d  Type:%-8s \n", p_hss->hss_id, (p_hss->hss_type)?"HSS28G":"HSS15G");
        SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"HssClkA:%-8s A0InfDiv:%-8s A0CoreDiv:%-8s A1CoreDiv:%-8s\n", pll_str[p_hss->plla_mode], div_str[p_hss->intf_div_a],
                            div_str[p_hss->core_div_a[0]], div_str[p_hss->core_div_a[1]]);

        SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"HssClkB:%-8s B0InfDiv:%-8s B0CoreDiv:%-8s B1CoreDiv:%-8s\n", pll_str[p_hss->pllb_mode], div_str[p_hss->intf_div_b],
                            div_str[p_hss->core_div_b[0]], div_str[p_hss->core_div_b[1]]);
        SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"-----------------------------------------------------------------------------\n");
        SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"%-4s %-16s %-16s %-8s %-8s %-8s\n", "LANE", "TxIntfSel", "TxCoreSel", "CoreASel", "CoreBSel", "IntfSel");
        SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"-----------------------------------------------------------------------------\n");
        for (lane_idx = 0; lane_idx < ((p_hss->hss_type)?HSS28G_LANE_NUM:HSS15G_LANE_NUM); lane_idx++)
        {
            internal_idx = lane_idx;
            if (p_hss->serdes_info[lane_idx].mode == CTC_CHIP_SERDES_NONE_MODE)
            {
                 SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"%-4d %-8s\n",  lane_idx, "Disable");
            }
            else
            {
                if (p_hss->hss_type == SYS_DATAPATH_HSS_TYPE_15G)
                {
                    tb_id = HsClkTreeCfg0_t + p_hss->hss_id;
                    cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
                    DRV_IOCTL(lchip, 0, cmd, &hs_clk_tree);
                    fld_id = HsClkTreeCfg0_cfgClockHssTxAIntfS2IntSel_f + 5*internal_idx;
                    DRV_IOR_FIELD(tb_id, fld_id, &TxIntfClkSel, &hs_clk_tree);
                    fld_id = HsClkTreeCfg0_cfgClockHssTxACoreS1IntSel_f + 5*internal_idx;
                    DRV_IOR_FIELD(tb_id, fld_id, &TxCoreClkSel, &hs_clk_tree);
                    fld_id = HsClkTreeCfg0_cfgClockHssTxACoreSA0IntSel_f + 5*internal_idx;
                    DRV_IOR_FIELD(tb_id, fld_id, &CoreA_sel, &hs_clk_tree);
                    fld_id = HsClkTreeCfg0_cfgClockHssTxACoreSB0IntSel_f + 5*internal_idx;
                    DRV_IOR_FIELD(tb_id, fld_id, &CoreB_sel, &hs_clk_tree);
                    fld_id = HsClkTreeCfg0_cfgClockHssTxAIntfSA0IntSel_f + 5*internal_idx;
                    DRV_IOR_FIELD(tb_id, fld_id, &intf_sel, &hs_clk_tree);
                    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"%-4d %-16s %-16s %-8s %-8s %-8s\n",  lane_idx, TxIntfClkSel?"CoreClk":"IntfClk",
                        TxCoreClkSel?"CoreB":"CoreA", CoreA_sel?"A0":"A1", CoreB_sel?"B0":"B1", intf_sel?"A0":"B0");
                }
                else
                {
                    tb_id = CsClkTreeCfg0_t + p_hss->hss_id;
                    cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
                    DRV_IOCTL(lchip, 0, cmd, &cs_clk_tree);
                    fld_id = CsClkTreeCfg0_cfgClockHssTxAIntfS2IntSel_f + 5*lane_idx;
                    DRV_IOR_FIELD(tb_id, fld_id, &TxIntfClkSel, &cs_clk_tree);
                    fld_id = CsClkTreeCfg0_cfgClockHssTxACoreS1IntSel_f + 5*lane_idx;
                    DRV_IOR_FIELD(tb_id, fld_id, &TxCoreClkSel, &cs_clk_tree);
                    fld_id = CsClkTreeCfg0_cfgClockHssTxACoreSA0IntSel_f + 5*lane_idx;
                    DRV_IOR_FIELD(tb_id, fld_id, &CoreA_sel, &cs_clk_tree);
                    fld_id = CsClkTreeCfg0_cfgClockHssTxACoreSB0IntSel_f + 5*lane_idx;
                    DRV_IOR_FIELD(tb_id, fld_id, &CoreB_sel, &cs_clk_tree);
                    fld_id = CsClkTreeCfg0_cfgClockHssTxAIntfSA0IntSel_f + 5*lane_idx;
                    DRV_IOR_FIELD(tb_id, fld_id, &intf_sel, &cs_clk_tree);
                    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"%-4d %-16s %-16s %-8s %-8s %-8s\n",  lane_idx, TxIntfClkSel?"CoreClk":"IntfClk",
                        TxCoreClkSel?"CoreB":"CoreA", CoreA_sel?"A0":"A1", CoreB_sel?"B0":"B1", intf_sel?"A0":"B0");
                }
            }
        }
        SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "-----------------------------------------------------------------------------\n");
        SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"\n");

    }

    return 0;
}

STATIC int32
_sys_goldengate_datapath_show_hss(uint8 lchip, uint32 start, uint32 end, uint8 is_all)
{
    uint8 start_idx = 0;
    uint8 end_idx = 0;
    sys_datapath_hss_attribute_t* p_hss = NULL;
    uint32 multa = 0;
    uint32 multb = 0;
    uint8 index = 0;
    char* pll_str[SYS_DATAPATH_HSSCLK_MAX] = {"Disable","515.625M","500M", "644.53125M","625M", "567.1875M"};
    char* vco_str[2][SYS_DATAPATH_HSSCLK_MAX] =
    {
        {"Disable","10.3125G","10G", "12.890625G","12.5G", "11.34375G"},
        {"Disable","20.625G","20G", "25.78125G","25G", "Not Support"}
    };

    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"%-4s %-8s %-8s %-12s %-12s %-12s %-12s %-8s %-8s\n", "IDX", "HSSID", "TYPE", "PLLA", "PLLB", "VCOA", "VCOB", "MULTA", "MULTB");
    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"---------------------------------------------------------------------------------------\n");

    SYS_DATAPATH_INIT_CHECK(lchip);

    if (is_all)
    {
        start_idx = 0;
        end_idx = SYS_DATAPATH_HSS_NUM;
    }
    else
    {
        start_idx = start;
        end_idx = end+1;
    }

    for (index = start_idx; index < end_idx; index++)
    {
        p_hss = ctc_vector_get(p_gg_datapath_master[lchip]->p_hss_vector, index);
        if (NULL == p_hss)
        {
            continue;
        }

        multa = 0;
        multb = 0;

        if (p_hss->plla_mode)
        {
            if (!p_hss->hss_type)
            {
                SYS_DATAPATH_GET_HSS15G_MULT(p_hss->plla_mode, multa);
            }
            else
            {
                SYS_DATAPATH_GET_HSS28G_MULT(p_hss->plla_mode, multa);
            }
        }

        if (p_hss->pllb_mode)
        {
            if (!p_hss->hss_type)
            {
                SYS_DATAPATH_GET_HSS15G_MULT(p_hss->pllb_mode, multb);
            }
            else
            {
                SYS_DATAPATH_GET_HSS28G_MULT(p_hss->pllb_mode, multb);
            }
        }

        SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"%-4d %-8d %-8s %-12s %-12s %-12s %-12s 0x%-8x 0x%-8x\n", index, p_hss->hss_id, (p_hss->hss_type?"HSS28G":"HSS15G"),
        pll_str[p_hss->plla_mode], pll_str[p_hss->pllb_mode], vco_str[p_hss->hss_type][p_hss->plla_mode], vco_str[p_hss->hss_type][p_hss->pllb_mode],
        multa, multb);
    }
    return 0;
}

STATIC int32
_sys_goldengate_datapath_show_serdes(uint8 lchip, uint32 start, uint32 end, uint8 is_all)
{
    char* mode[CTC_CHIP_MAX_SERDES_MODE] = {"None", "Xfi", "Sgmii", "Not support", "Not support", "Xaui", "D-xaui", "Xlg", "Cg","2.5G"};
    char* bw[4] = {"8BITS", "10BITS", "20BITS", "40BITS"};
    char* div[4] = {"FULL", "HALF", "QUAD", "EIGHTH"};
    uint8 start_idx = 0;
    uint8 end_idx = 0;
    uint32 hss_idx = 0;
    uint8 lane_idx = 0;
    sys_datapath_hss_attribute_t* p_hss = NULL;
    sys_datapath_serdes_info_t* p_serdes = NULL;
    sys_datapath_lport_attr_t* p_lport_attr = NULL;
    uint8 index = 0;
    uint8 slice_id = 0;

    SYS_DATAPATH_INIT_CHECK(lchip);

    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"%-8s %-8s %-8s %-8s %-8s %-8s %-8s %-5s %-5s %-5s %-5s %-9s\n", "SERDESID", "SLICE", "HSSID", "MODE", "BW", "RS", "PLLSEL", "CHAN", "MAC", "LPORT", "DYN", "OVERCLOCK");
    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"-----------------------------------------------------------------------------\n");

    if (is_all)
    {
        start_idx = 0;
        end_idx = SYS_GG_DATAPATH_SERDES_NUM;
    }
    else
    {
        start_idx = start;
        end_idx = end+1;
    }

    for (index = start_idx; index < end_idx; index++)
    {
        hss_idx = SYS_DATAPATH_MAP_SERDES_TO_HSS_INDEX(index);
        lane_idx = SYS_DATAPATH_MAP_SERDES_TO_LANE_ID(index);

        p_hss = ctc_vector_get(p_gg_datapath_master[lchip]->p_hss_vector, hss_idx);
        if (NULL == p_hss)
        {
            continue;
        }

        slice_id = (index>=48)?1:0;
        p_serdes = &p_hss->serdes_info[lane_idx];
        p_lport_attr = (sys_datapath_lport_attr_t*)(&(p_gg_datapath_master[lchip]->port_attr[slice_id][p_serdes->lport]));
        SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"%-8d %-8d %-8d %-8s %-8s %-8s %-8s %-5d %-5d %-5d %-5d %-9d\n", index, slice_id, p_hss->hss_id, mode[p_serdes->mode],
        bw[p_serdes->bit_width], div[p_serdes->rate_div], ((p_serdes->pll_sel ==SYS_DATAPATH_PLL_SEL_PLLB) ?"PLLB":"PLLA"),p_lport_attr->chan_id + 64*p_lport_attr->slice_id,
        p_lport_attr->mac_id + 64*p_lport_attr->slice_id, p_serdes->lport + 256*p_lport_attr->slice_id, p_serdes->is_dyn, p_serdes->overclocking_speed);

    }
    return 0;
}


int32
sys_goldengate_datapath_show_info(uint8 lchip, uint8 type, uint32 start, uint32 end, uint8 is_all)
{
    SYS_DATAPATH_INIT_CHECK(lchip);

    if (type == SYS_DATAPATH_DEBUG_HSS)
    {
        _sys_goldengate_datapath_show_hss(lchip, start, end, is_all);
    }
    else if (type == SYS_DATAPATH_DEBUG_SERDES)
    {
        _sys_goldengate_datapath_show_serdes(lchip, start, end, is_all);
    }
    else if (type == SYS_DATAPATH_DEBUG_CLKTREE)
    {
        _sys_goldengate_datapath_show_clktree(lchip, start, end, is_all);
    }
    else if (type == SYS_DATAPATH_DEBUG_CALENDAR)
    {
        _sys_goldengate_datapath_show_calendar(lchip, start, end, is_all);
    }
    return CTC_E_NONE;
}

int32
sys_goldengate_datapath_show_status(uint8 lchip)
{
    uint8 total_cnt = 0;
    uint8 sgmii_cnt = 0;
    uint8 xfi_cnt = 0;
    uint8 xlg_cnt = 0;
    uint8 dot25_cnt = 0;
    uint8 xaui_cnt = 0;
    uint8 dxaui_cnt = 0;
    uint8 cg_cnt = 0;
    uint8 index = 0;
    uint8 hss_idx = 0;
    uint8 lane_idx = 0;
    sys_datapath_hss_attribute_t* p_hss = NULL;
    sys_datapath_serdes_info_t* p_serdes = NULL;

    SYS_DATAPATH_INIT_CHECK(lchip);

    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"%-20s: %2d\n", "Core FrequencyA", p_gg_datapath_master[lchip]->core_plla);
    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"%-20s: %2d\n", "Core FrequencyB", p_gg_datapath_master[lchip]->core_pllb);
    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"\n");

    for (index = 0; index < 96; index++)
    {
        hss_idx = SYS_DATAPATH_MAP_SERDES_TO_HSS_INDEX(index);
        lane_idx = SYS_DATAPATH_MAP_SERDES_TO_LANE_ID(index);

        p_hss = ctc_vector_get(p_gg_datapath_master[lchip]->p_hss_vector, hss_idx);
        if (NULL == p_hss)
        {
            continue;
        }

        p_serdes = &p_hss->serdes_info[lane_idx];
        if (p_serdes->mode != CTC_CHIP_SERDES_NONE_MODE)
        {
            total_cnt++;
        }

        if (p_serdes->mode == CTC_CHIP_SERDES_XFI_MODE)
        {
            xfi_cnt++;
        }
        else if (p_serdes->mode == CTC_CHIP_SERDES_SGMII_MODE)
        {
            sgmii_cnt++;
        }
        else if (p_serdes->mode == CTC_CHIP_SERDES_XAUI_MODE)
        {
            xaui_cnt++;
        }
        else if (p_serdes->mode == CTC_CHIP_SERDES_DXAUI_MODE)
        {
            dxaui_cnt++;
        }
        else if (p_serdes->mode == CTC_CHIP_SERDES_XLG_MODE)
        {
            xlg_cnt++;
        }
        else if (p_serdes->mode == CTC_CHIP_SERDES_CG_MODE)
        {
            cg_cnt++;
        }
        else if (p_serdes->mode == CTC_CHIP_SERDES_2DOT5G_MODE)
        {
            dot25_cnt++;
        }
    }

    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"%-20s: %2d\n", "Total Serdes Count", total_cnt);
    if (sgmii_cnt)
    {
        SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"%-20s: %2d\n", "--Sgmii Mode Count", sgmii_cnt);
    }
    if (dot25_cnt)
    {
        SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"%-20s: %2d\n", "--2.5G Mode Count", dot25_cnt);
    }
    if (xfi_cnt)
    {
        SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"%-20s: %2d\n", "--XFI Mode Count", xfi_cnt);
    }
    if (xaui_cnt)
    {
        SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"%-20s: %2d\n", "--Xaui Mode Count", xaui_cnt);
    }
    if (dxaui_cnt)
    {
        SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"%-20s: %2d\n", "--D-Xaui Mode Count", dxaui_cnt);
    }
    if (xlg_cnt)
    {
        SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"%-20s: %2d\n", "--XLG Mode Count", xlg_cnt);
    }
    if (cg_cnt)
    {
        SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"%-20s: %2d\n", "--CG Mode Count", cg_cnt);
    }

    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"\n");

    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"%-20s\n", "Internal Port Status");
    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"%-8s %-8s %-8s\n", "Type", "PortId", "ChanId");

    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"-----------------------------\n");
    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"%-8s %-8d %-8d\n", "OAM", SYS_RSV_PORT_OAM_CPU_ID, p_gg_datapath_master[lchip]->oam_chan);
    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"%-8s %-8s %-8d\n", "DMA", "-", p_gg_datapath_master[lchip]->dma_chan);
    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"%-8s %-8d %-8d\n", "ILOOP", SYS_RSV_PORT_ILOOP_ID, p_gg_datapath_master[lchip]->iloop_chan);
    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"%-8s %-8d %-8d\n", "ELOOP", SYS_RSV_PORT_ELOOP_ID, p_gg_datapath_master[lchip]->eloop_chan);

    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"\n");
    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"%-20s\n", "ChipPort-mac-chan mapping(S0 means Slice0)");
    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"------------------------------------------------------\n");

    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-9s:   |%-7s| |%-8s| |%-7s| |%-8s|\n",
        "ChipPort", "S0 0~39", "S0 40~47", "S1 0~39", "S1 40~47");

    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-9s:   |%-7s| |%-8s| |%-7s| |%-8s|\n",
        "Channel", "S0 0~39", "S0 40~47", "S1 0~39", "S1 40~47");

    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-9s:   |%-7s| |%-8s| |%-7s| |%-8s|\n",
        "MacId", "S0 0~39", "S0 48~55", "S1 0~39", "S1 48~55");

    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-9s:   |%-7s| |%-8s| |%-7s| |%-8s|\n",
        "SharedPcs", "0~9", "10~11", "12~21", "22~23");

    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"------------------------------------------------------\n");

    return CTC_E_NONE;
}

#define __CLKTREE__

STATIC int32
_sys_goldengate_datapath_get_rate(uint8 lchip, uint8 mode, uint8 type, uint8* p_hssclk,
                            uint8* p_width, uint8* p_div, uint8* p_core_div, uint8* p_intf_div, uint16 overclocking_speed)
{
    switch (mode)
    {
        case CTC_CHIP_SERDES_NONE_MODE:
            *p_hssclk = SYS_DATAPATH_HSSCLK_DISABLE;
            break;

        case CTC_CHIP_SERDES_XFI_MODE:
            if (CTC_CHIP_SERDES_OCS_MODE_11_06G == overclocking_speed)
            {
                *p_hssclk = SYS_DATAPATH_HSSCLK_567DOT1875;
            }
            else if (CTC_CHIP_SERDES_OCS_MODE_12_58G == overclocking_speed)
            {
                *p_hssclk = SYS_DATAPATH_HSSCLK_644DOT53125;
            }
            else if (CTC_CHIP_SERDES_OCS_MODE_12_12G== overclocking_speed)
            {
                *p_hssclk = SYS_DATAPATH_HSSCLK_625;
            }
            else if (CTC_CHIP_SERDES_OCS_MODE_NONE == overclocking_speed)
            {
                *p_hssclk = SYS_DATAPATH_HSSCLK_515DOT625;
            }
            else
            {
                return CTC_E_INVALID_PARAM;
            }
            *p_width = SYS_DATAPATH_BIT_WIDTH_20BIT;
            *p_div = (type == SYS_DATAPATH_HSS_TYPE_15G)?SYS_DATAPATH_SERDES_DIV_FULL: SYS_DATAPATH_SERDES_DIV_HALF;
            *p_core_div = SYS_DATAPATH_CLKTREE_DIV_FULL;
            *p_intf_div = SYS_DATAPATH_CLKTREE_DIV_USELESS;
            break;

        case CTC_CHIP_SERDES_XLG_MODE:
            if (CTC_CHIP_SERDES_OCS_MODE_11_06G == overclocking_speed)
            {
                *p_hssclk = SYS_DATAPATH_HSSCLK_567DOT1875;
            }
            else if (CTC_CHIP_SERDES_OCS_MODE_12_58G == overclocking_speed)
            {
                *p_hssclk = SYS_DATAPATH_HSSCLK_644DOT53125;
            }
            else if (CTC_CHIP_SERDES_OCS_MODE_12_12G== overclocking_speed)
            {
                *p_hssclk = SYS_DATAPATH_HSSCLK_625;
            }
            else if (CTC_CHIP_SERDES_OCS_MODE_NONE == overclocking_speed)
            {
                *p_hssclk = SYS_DATAPATH_HSSCLK_515DOT625;
            }
            else
            {
                return CTC_E_INVALID_PARAM;
            }
            *p_width = SYS_DATAPATH_BIT_WIDTH_20BIT;
            *p_div = (type == SYS_DATAPATH_HSS_TYPE_15G)?SYS_DATAPATH_SERDES_DIV_FULL: SYS_DATAPATH_SERDES_DIV_HALF;
            *p_core_div = SYS_DATAPATH_CLKTREE_DIV_FULL;
            *p_intf_div = SYS_DATAPATH_CLKTREE_DIV_USELESS;
            break;

        case CTC_CHIP_SERDES_SGMII_MODE:
            if (CTC_CHIP_SERDES_OCS_MODE_NONE != overclocking_speed)
            {
                return CTC_E_INVALID_PARAM;
            }
            *p_hssclk = SYS_DATAPATH_HSSCLK_500;
            *p_width = SYS_DATAPATH_BIT_WIDTH_20BIT;
            *p_div = SYS_DATAPATH_SERDES_DIV_EIGHTH;
            *p_core_div = SYS_DATAPATH_CLKTREE_DIV_QUAD;
            *p_intf_div = SYS_DATAPATH_CLKTREE_DIV_EIGHTH;
            break;

        case CTC_CHIP_SERDES_XAUI_MODE:
            if (CTC_CHIP_SERDES_OCS_MODE_NONE != overclocking_speed)
            {
                return CTC_E_INVALID_PARAM;
            }
            *p_hssclk = SYS_DATAPATH_HSSCLK_625;
            *p_width = SYS_DATAPATH_BIT_WIDTH_20BIT;
            *p_div = SYS_DATAPATH_SERDES_DIV_QUAD;
            *p_core_div = SYS_DATAPATH_CLKTREE_DIV_QUAD;
            *p_intf_div = SYS_DATAPATH_CLKTREE_DIV_USELESS;
            break;
        case CTC_CHIP_SERDES_DXAUI_MODE:
            if (CTC_CHIP_SERDES_OCS_MODE_NONE != overclocking_speed)
            {
                return CTC_E_INVALID_PARAM;
            }
            *p_hssclk = SYS_DATAPATH_HSSCLK_625;
            *p_width = SYS_DATAPATH_BIT_WIDTH_20BIT;
            *p_div = SYS_DATAPATH_SERDES_DIV_HALF;
            *p_core_div = SYS_DATAPATH_CLKTREE_DIV_HALF;
            *p_intf_div = SYS_DATAPATH_CLKTREE_DIV_USELESS;
            break;

        case CTC_CHIP_SERDES_CG_MODE:
            if (CTC_CHIP_SERDES_OCS_MODE_NONE != overclocking_speed)
            {
                return CTC_E_INVALID_PARAM;
            }
            *p_hssclk = SYS_DATAPATH_HSSCLK_644DOT53125;
            *p_width = SYS_DATAPATH_BIT_WIDTH_40BIT;
            *p_div = SYS_DATAPATH_SERDES_DIV_FULL;
            *p_core_div = SYS_DATAPATH_CLKTREE_DIV_FULL;
            *p_intf_div = SYS_DATAPATH_CLKTREE_DIV_USELESS;
            break;

        case CTC_CHIP_SERDES_2DOT5G_MODE:
            if (CTC_CHIP_SERDES_OCS_MODE_NONE != overclocking_speed)
            {
                return CTC_E_INVALID_PARAM;
            }
            *p_hssclk = SYS_DATAPATH_HSSCLK_625;
            *p_width = SYS_DATAPATH_BIT_WIDTH_20BIT;
            *p_div = SYS_DATAPATH_SERDES_DIV_QUAD;
            *p_core_div = SYS_DATAPATH_CLKTREE_DIV_HALF;
            *p_intf_div = SYS_DATAPATH_CLKTREE_DIV_QUAD;
            break;

        default:
            /*not support*/
            return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

/*
 get mac id/lport/chan id from serdes id
*/
STATIC int32
_sys_goldengate_datapath_get_serdes_mapping(uint8 lchip, uint8 serdes_id, uint8 mode, uint16* p_lport,
                        uint8* p_mac, uint8* p_chan, uint8* p_speed)
{
    uint8 serdes_occ = 0;
    uint16 lport = 0;
    uint8 mac_id = 0;
    uint8 chan_id = 0;
    uint8 speed = 0;
    uint8 slice_id = 0;
    uint8 slice_serdes = 0;

    slice_id = (serdes_id >= 48)?1:0;
    slice_serdes = serdes_id - 48*slice_id;

    switch(mode)
    {
        case CTC_CHIP_SERDES_XFI_MODE:
            speed = CTC_PORT_SPEED_10G;
            serdes_occ = 0;
            break;

        case CTC_CHIP_SERDES_SGMII_MODE:
            speed = CTC_PORT_SPEED_1G;
            serdes_occ = 0;
            break;

        case CTC_CHIP_SERDES_XAUI_MODE:
            speed = CTC_PORT_SPEED_10G;
            serdes_occ = 2;
            break;

        case CTC_CHIP_SERDES_DXAUI_MODE:
            speed = CTC_PORT_SPEED_20G;
            serdes_occ = 2;
            break;

        case CTC_CHIP_SERDES_XLG_MODE:
            speed = CTC_PORT_SPEED_40G;
            serdes_occ = 2;
            break;

        case CTC_CHIP_SERDES_CG_MODE:
            speed = CTC_PORT_SPEED_100G;
            serdes_occ = 2;
            break;

        case CTC_CHIP_SERDES_2DOT5G_MODE:
            speed = CTC_PORT_SPEED_2G5;
            serdes_occ = 0;
            break;

        default:
            break;
    }

    if (slice_serdes <= 35)
    {
        mac_id = SYS_DATAPATH_MAP_SERDES_TO_MAC(slice_serdes,serdes_occ);
        lport = mac_id;
        chan_id = mac_id;
    }
    else if (slice_serdes <= 39)
    {
        mac_id = SYS_DATAPATH_MAP_SERDES_TO_MAC((slice_serdes + 12), serdes_occ);
        lport = mac_id - 8;
        chan_id = mac_id - 8;
    }
    else if (slice_serdes <= 43)
    {
        mac_id = SYS_DATAPATH_MAP_SERDES_TO_MAC((slice_serdes - 4), serdes_occ);
        lport = mac_id;
        chan_id = mac_id;
    }
    else if (slice_serdes <= 47)
    {
        mac_id = SYS_DATAPATH_MAP_SERDES_TO_MAC((slice_serdes + 8), serdes_occ);
        lport = mac_id-8;
        chan_id = mac_id-8;
    }

    *p_lport = lport;
    *p_mac = mac_id;
    *p_chan = chan_id;
    *p_speed = speed;

    return CTC_E_NONE;
}

/* type:hss15g or hss28g, dir:0-tx,1-rx */
STATIC int32
_sys_goldengate_datapath_encode_serdes_cfg(uint8 lchip, uint8 type, uint8 dir, uint16* p_value, sys_datapath_serdes_info_t* p_serdes)
{
    uint32 temp = 0;
    uint8 pll_sel = 0;

    if (p_serdes->pll_sel == SYS_DATAPATH_PLL_SEL_NONE)
    {
        /*Disable serdes*/
        return CTC_E_NONE;
    }

    if (p_serdes->pll_sel == SYS_DATAPATH_PLL_SEL_PLLA)
    {
        pll_sel = 0;
    }
    else if (p_serdes->pll_sel == SYS_DATAPATH_PLL_SEL_PLLB)
    {
        pll_sel = 1;
    }

    if (SYS_DATAPATH_HSS_TYPE_15G == type)
    {
       if (p_serdes->bit_width != SYS_DATAPATH_BIT_WIDTH_20BIT)
        {
            SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"Hss15g only suport BW 20bits!\n");
            return CTC_E_INVALID_PARAM;
        }

        if (dir == 0)
        {
            /*tx*/
            *p_value |= ((1 << SYS_DATAPATH_HSS_LINK_EN_BIT) | (1 << SYS_DATAPATH_HSS_WR_MODE_BIT));
            *p_value |= (0x3 << SYS_DATAPATH_HSS_BW_SEL_BIT);
            *p_value |= (p_serdes->rate_div << SYS_DATAPATH_HSS_RATE_SEL_BIT);
            *p_value |= (pll_sel << SYS_DATAPATH_HSS_PLL_SEL_BIT);
        }
        else
        {
            /*rx*/
            *p_value |= (1 << SYS_DATAPATH_HSS_LINK_EN_BIT) | (1 << SYS_DATAPATH_HSS_WR_MODE_BIT);
            *p_value |= (0x3 << SYS_DATAPATH_HSS_BW_SEL_BIT);
            *p_value |= (p_serdes->rate_div << SYS_DATAPATH_HSS_RATE_SEL_BIT);
            *p_value |= (pll_sel << SYS_DATAPATH_HSS_PLL_SEL_BIT);

            /*for rate_div not equal 1/4 or 1/8, select DFE12*/
            if (p_serdes->rate_div < 2)
            {
                *p_value |= 0x30;
            }
        }
    }
    else
    {
        if ((p_serdes->bit_width == SYS_DATAPATH_BIT_WIDTH_20BIT) && (p_serdes->rate_div == SYS_DATAPATH_SERDES_DIV_FULL))
        {
            SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"HSS28G cannot support full divider and 20bits BW!!\n");
            return CTC_E_INVALID_PARAM;
        }

        if (p_serdes->bit_width == SYS_DATAPATH_BIT_WIDTH_20BIT)
        {
            temp = 0x1;
        }
        else if (p_serdes->bit_width == SYS_DATAPATH_BIT_WIDTH_40BIT)
        {
            temp = 0x3;
        }
        else
        {
            SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"HSS28G only support 20bit and 40bits BW, now bw is %d!!\n", p_serdes->bit_width);
            return CTC_E_INVALID_PARAM;
        }

        if (dir == 0)
        {
            /*tx*/
            *p_value |= ((1 << SYS_DATAPATH_HSS_LINK_EN_BIT) | (1 << SYS_DATAPATH_HSS_WR_MODE_BIT));
            *p_value |= (temp << SYS_DATAPATH_HSS_BW_SEL_BIT);
            *p_value |= (p_serdes->rate_div << SYS_DATAPATH_HSS_RATE_SEL_BIT);
            *p_value |= (pll_sel << SYS_DATAPATH_HSS_PLL_SEL_BIT);
        }
        else
        {
            /*rx*/
            *p_value |= (1 << SYS_DATAPATH_HSS_LINK_EN_BIT) | (1 << SYS_DATAPATH_HSS_WR_MODE_BIT);
            *p_value |= (temp << SYS_DATAPATH_HSS_BW_SEL_BIT);
            *p_value |= (p_serdes->rate_div << SYS_DATAPATH_HSS_RATE_SEL_BIT);
            *p_value |= (pll_sel << SYS_DATAPATH_HSS_PLL_SEL_BIT);

            /*for rate_div not equal 1/4 or 1/8, select DFE15*/
            if (p_serdes->rate_div < 2)
            {
                *p_value |= 0x30;
            }
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_datapath_hss15g_cfg_lane_clktree(uint8 lchip, uint8 idx, sys_datapath_hss_attribute_t* p_hss)
{
    uint32 cmd = 0;
    uint32 tb_id = 0;
    sys_datapath_serdes_info_t* p_serdes = NULL;
    HsClkTreeCfg0_m clk_tree;
    HsMacroCfg0_m hs_macro;
    uint32 tmp_val = 0;
    uint32 fld_id = 0;
    uint8 get_valid_div = 0;
    uint32 out_mode = 0;
    uint8 lane_id = 0;

    lane_id = idx;

    p_serdes = &p_hss->serdes_info[idx];

    if (p_serdes->mode == CTC_CHIP_SERDES_NONE_MODE)
    {
        return CTC_E_NONE;
    }

    sal_memset(&clk_tree, 0, sizeof(HsClkTreeCfg0_m));
    sal_memset(&hs_macro, 0, sizeof(HsMacroCfg0_m));

    tb_id = HsClkTreeCfg0_t + p_hss->hss_id;
    cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &clk_tree);

    if ((p_serdes->mode == CTC_CHIP_SERDES_SGMII_MODE) || (p_serdes->mode == CTC_CHIP_SERDES_2DOT5G_MODE))
    {
        out_mode = 0;

        /*Must using intf divider*/
        if (p_serdes->pll_sel == SYS_DATAPATH_PLL_SEL_PLLB)
        {
            if (p_hss->intf_div_b == SYS_DATAPATH_CLKTREE_DIV_USELESS)
            {
                SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"Sgmii mode do not have interface divider!!, please check! \n");
                return CTC_E_INVALID_PARAM;
            }

            /*core b source select, using b0 core*/
            tmp_val = 1;
            fld_id = HsClkTreeCfg0_cfgClockHssTxACoreSB0IntSel_f + 5*lane_id;
            DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &clk_tree);
        }
        else if (p_serdes->pll_sel == SYS_DATAPATH_PLL_SEL_PLLA)
        {
            if (p_hss->intf_div_a == SYS_DATAPATH_CLKTREE_DIV_USELESS)
            {
                SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"Sgmii mode do not have interface divider!!, please check! \n");
                return CTC_E_INVALID_PARAM;
            }

            /*core a source select, using a0 core*/
            tmp_val = 1;
            fld_id = HsClkTreeCfg0_cfgClockHssTxACoreSA0IntSel_f + 5*lane_id;
            DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &clk_tree);
        }

        /*interface source select, plla using A0Intf, pllb using B0Intf*/
        fld_id = HsClkTreeCfg0_cfgClockHssTxAIntfSA0IntSel_f + 5*lane_id;
        tmp_val = (p_serdes->pll_sel == SYS_DATAPATH_PLL_SEL_PLLB)?0:1;
        DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &clk_tree);

        /*core source select, plla acore, pllb using bcore*/
        tmp_val = (p_serdes->pll_sel == SYS_DATAPATH_PLL_SEL_PLLB)?1:0;
        fld_id = HsClkTreeCfg0_cfgClockHssTxACoreS1IntSel_f + 5*lane_id;
        DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &clk_tree);

        /*tx clock select, using interface clock */
        tmp_val = 0;
        fld_id = HsClkTreeCfg0_cfgClockHssTxAIntfS2IntSel_f + 5*lane_id;
        DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &clk_tree);
    }
    else if ((p_serdes->mode == CTC_CHIP_SERDES_XFI_MODE) || (p_serdes->mode == CTC_CHIP_SERDES_XLG_MODE))
    {
        out_mode = 0x2;

        if (p_serdes->pll_sel == SYS_DATAPATH_PLL_SEL_PLLB)
        {
            /*core b source select, using b0 core*/
            tmp_val = 1;
            fld_id = HsClkTreeCfg0_cfgClockHssTxACoreSB0IntSel_f + 5*lane_id;
            DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &clk_tree);
        }
        else  if (p_serdes->pll_sel == SYS_DATAPATH_PLL_SEL_PLLA)
        {
            /*core a source select, using a0 core*/
            tmp_val = 1;
            fld_id = HsClkTreeCfg0_cfgClockHssTxACoreSA0IntSel_f + 5*lane_id;
            DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &clk_tree);
        }

        /*core source select, using acore or bcore*/
        tmp_val = (p_serdes->pll_sel == SYS_DATAPATH_PLL_SEL_PLLB)?1:0;
        fld_id = HsClkTreeCfg0_cfgClockHssTxACoreS1IntSel_f + 5*lane_id;
        DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &clk_tree);

        /*tx clock select, using core clock */
        tmp_val = 1;
        fld_id = HsClkTreeCfg0_cfgClockHssTxAIntfS2IntSel_f + 5*lane_id;
        DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &clk_tree);
    }
    else
    {
        out_mode = 0x01;

        /*XAUI,DXAUI*/
        if (p_serdes->pll_sel == SYS_DATAPATH_PLL_SEL_PLLB)
        {
            if (p_hss->core_div_b[0] != SYS_DATAPATH_CLKTREE_DIV_USELESS)
            {
                if ((p_hss->core_div_b[0] == SYS_DATAPATH_CLKTREE_DIV_QUAD) && (p_serdes->mode == CTC_CHIP_SERDES_XAUI_MODE))
                {
                    get_valid_div = 1;
                }
                else if ((p_hss->core_div_b[0] == SYS_DATAPATH_CLKTREE_DIV_HALF) && (p_serdes->mode == CTC_CHIP_SERDES_DXAUI_MODE))
                {
                    get_valid_div = 1;
                }
            }

            /*core b source select, if b0 match using b0core*/
            if (get_valid_div)
            {
                tmp_val = 1;
                fld_id = HsClkTreeCfg0_cfgClockHssTxACoreSB0IntSel_f + 5*lane_id;
                DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &clk_tree);
            }

            if ((p_hss->core_div_b[1] != SYS_DATAPATH_CLKTREE_DIV_USELESS) && (!get_valid_div))
            {
                if ((p_hss->core_div_b[1] == SYS_DATAPATH_CLKTREE_DIV_QUAD) && (p_serdes->mode == CTC_CHIP_SERDES_XAUI_MODE))
                {
                    get_valid_div = 1;
                }
                else if ((p_hss->core_div_b[1] == SYS_DATAPATH_CLKTREE_DIV_HALF) && (p_serdes->mode == CTC_CHIP_SERDES_DXAUI_MODE))
                {
                    get_valid_div = 1;
                }

                /*core b source select, if b1 match using b1core*/
                if (get_valid_div)
                {
                    tmp_val = 0;
                    fld_id = HsClkTreeCfg0_cfgClockHssTxACoreSB0IntSel_f + 5*lane_id;
                    DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &clk_tree);
                }
            }

            if (!get_valid_div)
            {
                SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"Hss15G Can not get valid core divider!! hssid:%d, lane:%d\n", p_hss->hss_id, idx);
                return CTC_E_DATAPATH_CLKTREE_CFG_ERROR;
            }

            /*core source select, using b0 or b1*/
            tmp_val = 1;
            fld_id = HsClkTreeCfg0_cfgClockHssTxACoreS1IntSel_f + 5*lane_id;
            DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &clk_tree);

            /*tx clock select, using core clock */
            tmp_val = 1;
            fld_id = HsClkTreeCfg0_cfgClockHssTxAIntfS2IntSel_f + 5*lane_id;
            DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &clk_tree);
        }
        else if (p_serdes->pll_sel == SYS_DATAPATH_PLL_SEL_PLLA)
        {
            if (p_hss->core_div_a[0] != SYS_DATAPATH_CLKTREE_DIV_USELESS)
            {
                if ((p_hss->core_div_a[0] == SYS_DATAPATH_CLKTREE_DIV_QUAD) && (p_serdes->mode == CTC_CHIP_SERDES_XAUI_MODE))
                {
                    get_valid_div = 1;
                }
                else if ((p_hss->core_div_a[0] == SYS_DATAPATH_CLKTREE_DIV_HALF) && (p_serdes->mode == CTC_CHIP_SERDES_DXAUI_MODE))
                {
                    get_valid_div = 1;
                }
            }

            /*core a source select, if a0 match using a0core*/
            if (get_valid_div)
            {
                tmp_val = 1;
                fld_id = HsClkTreeCfg0_cfgClockHssTxACoreSA0IntSel_f + 5*lane_id;
                DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &clk_tree);
            }

            if ((p_hss->core_div_a[1] != SYS_DATAPATH_CLKTREE_DIV_USELESS) && (!get_valid_div))
            {
                if ((p_hss->core_div_a[1] == SYS_DATAPATH_CLKTREE_DIV_QUAD) && (p_serdes->mode == CTC_CHIP_SERDES_XAUI_MODE))
                {
                    get_valid_div = 1;
                }
                else if ((p_hss->core_div_a[1] == SYS_DATAPATH_CLKTREE_DIV_HALF) && (p_serdes->mode == CTC_CHIP_SERDES_DXAUI_MODE))
                {
                    get_valid_div = 1;
                }

                /*core a source select, if a1 match using a1core*/
                if (get_valid_div)
                {
                    tmp_val = 0;
                    fld_id = HsClkTreeCfg0_cfgClockHssTxACoreSA0IntSel_f + 5*lane_id;
                    DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &clk_tree);
                }
            }

            if (!get_valid_div)
            {
                SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"Hss15G Can not get valid core divider!! hssid:%d, lane:%d\n", p_hss->hss_id, idx);
                return CTC_E_DATAPATH_CLKTREE_CFG_ERROR;
            }

            /*core source select, using a0 or a1*/
            tmp_val = 0;
            fld_id = HsClkTreeCfg0_cfgClockHssTxACoreS1IntSel_f + 5*lane_id;
            DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &clk_tree);

            /*tx clock select, using core clock */
            tmp_val = 1;
            fld_id = HsClkTreeCfg0_cfgClockHssTxAIntfS2IntSel_f + 5*lane_id;
            DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &clk_tree);
        }
    }

    cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &clk_tree);

    tb_id = HsMacroCfg0_t + p_hss->hss_id;
    cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &hs_macro);
    fld_id = HsMacroCfg0_cfgHssTxADataOutSel_f + 2*lane_id;
    DRV_IOW_FIELD(tb_id, fld_id, &out_mode, &hs_macro);
    cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &hs_macro);

    if ((p_serdes->mode == CTC_CHIP_SERDES_SGMII_MODE) || (p_serdes->mode == CTC_CHIP_SERDES_2DOT5G_MODE))
    {
        tb_id = HsMacroCfg0_t + p_hss->hss_id;
        cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &hs_macro);
        fld_id = HsMacroCfg0_cfgResetHssTxA_f + lane_id;
        tmp_val = 1;
        DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &hs_macro);
        cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &hs_macro);

        sal_task_sleep(1);

        cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &hs_macro);
        fld_id = HsMacroCfg0_cfgResetHssTxA_f + lane_id;
        tmp_val = 0;
        DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &hs_macro);
        cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &hs_macro);
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_datapath_hss28g_cfg_lane_clktree(uint8 lchip, uint8 idx, sys_datapath_hss_attribute_t* p_hss)
{
    uint32 cmd = 0;
    uint32 tb_id = 0;
    sys_datapath_serdes_info_t* p_serdes = NULL;
    CsClkTreeCfg0_m clk_tree;
    CsMacroCfg0_m cs_macro;
    uint32 tmp_val = 0;
    uint32 fld_id = 0;
    uint8 get_valid_div = 0;
    uint32 out_mode = 0;

    p_serdes = &p_hss->serdes_info[idx];

    if (p_serdes->mode == CTC_CHIP_SERDES_NONE_MODE)
    {
        return CTC_E_NONE;
    }

    sal_memset(&clk_tree, 0, sizeof(CsClkTreeCfg0_m));
    sal_memset(&cs_macro, 0, sizeof(CsMacroCfg0_m));

    tb_id = CsClkTreeCfg0_t + p_hss->hss_id;
    cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &clk_tree);

    if ((p_serdes->mode == CTC_CHIP_SERDES_SGMII_MODE) || (p_serdes->mode == CTC_CHIP_SERDES_2DOT5G_MODE))
    {
        out_mode = 0;

        /*Must using intf divider*/
        if (p_serdes->pll_sel == SYS_DATAPATH_PLL_SEL_PLLB)
        {
            if (p_hss->intf_div_b == SYS_DATAPATH_CLKTREE_DIV_USELESS)
            {
                SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"Sgmii mode do not have interface divider!!, please check! \n");
                return CTC_E_INVALID_PARAM;
            }

            /*select core b source, using b0core*/
            tmp_val = 1;
            fld_id = CsClkTreeCfg0_cfgClockHssTxACoreSB0IntSel_f + 5*idx;
            DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &clk_tree);
        }
        else if (p_serdes->pll_sel == SYS_DATAPATH_PLL_SEL_PLLA)
        {
            if (p_hss->intf_div_a == SYS_DATAPATH_CLKTREE_DIV_USELESS)
            {
                SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"Sgmii mode do not have interface divider!!, please check! \n");
                return CTC_E_INVALID_PARAM;
            }

            /*select core a source, using a0core*/
            tmp_val = 1;
            fld_id = CsClkTreeCfg0_cfgClockHssTxACoreSA0IntSel_f + 5*idx;
            DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &clk_tree);
        }

        /*select interface clock source, plla using A0Intf, pllb using B0Intf*/
        fld_id = CsClkTreeCfg0_cfgClockHssTxAIntfSA0IntSel_f + 5*idx;
        tmp_val = (p_serdes->pll_sel == SYS_DATAPATH_PLL_SEL_PLLB)?0:1;
        DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &clk_tree);

        /*select core clock source, plla using ACore, pllb using BCore*/
        tmp_val = (p_serdes->pll_sel == SYS_DATAPATH_PLL_SEL_PLLB)?1:0;
        fld_id = CsClkTreeCfg0_cfgClockHssTxACoreS1IntSel_f + 5*idx;
        DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &clk_tree);

        /*select tx clock source, using interface clock*/
        tmp_val = 0;
        fld_id = CsClkTreeCfg0_cfgClockHssTxAIntfS2IntSel_f + 5*idx;
        DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &clk_tree);
    }
    else if ((p_serdes->mode == CTC_CHIP_SERDES_XFI_MODE) || (p_serdes->mode == CTC_CHIP_SERDES_XLG_MODE))
    {
        out_mode = 0x2;

        if (p_serdes->pll_sel == SYS_DATAPATH_PLL_SEL_PLLB)
        {
            /*select core b source, using b0core*/
            tmp_val = 1;
            fld_id = CsClkTreeCfg0_cfgClockHssTxACoreSB0IntSel_f + 5*idx;
            DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &clk_tree);
        }
        else if (p_serdes->pll_sel == SYS_DATAPATH_PLL_SEL_PLLA)
        {
            /*select core a source, using a0core*/
            tmp_val = 1;
            fld_id = CsClkTreeCfg0_cfgClockHssTxACoreSA0IntSel_f + 5*idx;
            DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &clk_tree);
        }

        /*select core clock source, plla using ACore, pllb using BCore*/
        tmp_val = (p_serdes->pll_sel == SYS_DATAPATH_PLL_SEL_PLLB)?1:0;
        fld_id = CsClkTreeCfg0_cfgClockHssTxACoreS1IntSel_f + 5*idx;
        DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &clk_tree);

        /*select tx clock source, using core clock*/
        tmp_val = 1;
        fld_id = CsClkTreeCfg0_cfgClockHssTxAIntfS2IntSel_f + 5*idx;
        DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &clk_tree);
    }
    else if ((p_serdes->mode == CTC_CHIP_SERDES_XAUI_MODE) || (p_serdes->mode == CTC_CHIP_SERDES_DXAUI_MODE))
    {
        out_mode = 0x01;

        /*XAUI,DXAUI*/
        if (p_serdes->pll_sel == SYS_DATAPATH_PLL_SEL_PLLB)
        {
            if (p_hss->core_div_b[0] != SYS_DATAPATH_CLKTREE_DIV_USELESS)
            {
                if ((p_hss->core_div_b[0] == SYS_DATAPATH_CLKTREE_DIV_QUAD) && (p_serdes->mode == CTC_CHIP_SERDES_XAUI_MODE))
                {
                    get_valid_div = 1;
                }
                else if ((p_hss->core_div_b[0] == SYS_DATAPATH_CLKTREE_DIV_HALF) && (p_serdes->mode == CTC_CHIP_SERDES_DXAUI_MODE))
                {
                    get_valid_div = 1;
                }
            }

            /*select core b source, if b0 match using b0core*/
            if (get_valid_div)
            {
                tmp_val = 1;
                fld_id = CsClkTreeCfg0_cfgClockHssTxACoreSB0IntSel_f + 5*idx;
                DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &clk_tree);
            }

            if ((p_hss->core_div_b[1] != SYS_DATAPATH_CLKTREE_DIV_USELESS) && (!get_valid_div))
            {
                if ((p_hss->core_div_b[1] == SYS_DATAPATH_CLKTREE_DIV_QUAD) && (p_serdes->mode == CTC_CHIP_SERDES_XAUI_MODE))
                {
                    get_valid_div = 1;
                }
                else if ((p_hss->core_div_b[1] == SYS_DATAPATH_CLKTREE_DIV_HALF) && (p_serdes->mode == CTC_CHIP_SERDES_DXAUI_MODE))
                {
                    get_valid_div = 1;
                }

                /*select core b source, if b1 match using b1core*/
                if (get_valid_div)
                {
                    tmp_val = 0;
                    fld_id = CsClkTreeCfg0_cfgClockHssTxACoreSB0IntSel_f + 5*idx;
                    DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &clk_tree);
                }
            }

            if (!get_valid_div)
            {
                SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"Hss28G Can not get valid core divider!! hssid:%d, lane:%d\n", p_hss->hss_id, idx);
                return CTC_E_DATAPATH_CLKTREE_CFG_ERROR;
            }

            /*select core clock source, plla using ACore, pllb using BCore*/
            tmp_val = 1;
            fld_id = CsClkTreeCfg0_cfgClockHssTxACoreS1IntSel_f + 5*idx;
            DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &clk_tree);

            /*select tx clock source, using core clock*/
            tmp_val = 1;
            fld_id = CsClkTreeCfg0_cfgClockHssTxAIntfS2IntSel_f + 5*idx;
            DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &clk_tree);
        }
        else if (p_serdes->pll_sel == SYS_DATAPATH_PLL_SEL_PLLA)
        {
            if (p_hss->core_div_a[0] != SYS_DATAPATH_CLKTREE_DIV_USELESS)
            {
                if ((p_hss->core_div_a[0] == SYS_DATAPATH_CLKTREE_DIV_QUAD) && (p_serdes->mode == CTC_CHIP_SERDES_XAUI_MODE))
                {
                    get_valid_div = 1;
                }
                else if ((p_hss->core_div_a[0] == SYS_DATAPATH_CLKTREE_DIV_HALF) && (p_serdes->mode == CTC_CHIP_SERDES_DXAUI_MODE))
                {
                    get_valid_div = 1;
                }
            }

            /*select core a source, if a0 match using a0core*/
            if (get_valid_div)
            {
                tmp_val = 1;
                fld_id = CsClkTreeCfg0_cfgClockHssTxACoreSA0IntSel_f + 5*idx;
                DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &clk_tree);
            }

            if ((p_hss->core_div_a[1] != SYS_DATAPATH_CLKTREE_DIV_USELESS) && (!get_valid_div))
            {
                if ((p_hss->core_div_a[1] == SYS_DATAPATH_CLKTREE_DIV_QUAD) && (p_serdes->mode == CTC_CHIP_SERDES_XAUI_MODE))
                {
                    get_valid_div = 1;
                }
                else if ((p_hss->core_div_a[1] == SYS_DATAPATH_CLKTREE_DIV_HALF) && (p_serdes->mode == CTC_CHIP_SERDES_DXAUI_MODE))
                {
                    get_valid_div = 1;
                }

                /*select core a source, if a1 match using a1core*/
                if (get_valid_div)
                {
                    tmp_val = 0;
                    fld_id = CsClkTreeCfg0_cfgClockHssTxACoreSA0IntSel_f + 5*idx;
                    DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &clk_tree);
                }
            }

            if (!get_valid_div)
            {
                SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"Hss28G Can not get valid core divider!! hssid:%d, lane:%d\n", p_hss->hss_id, idx);
                return CTC_E_DATAPATH_CLKTREE_CFG_ERROR;
            }

            /*select core clock source, plla using ACore, pllb using BCore*/
            tmp_val = 0;
            fld_id = CsClkTreeCfg0_cfgClockHssTxACoreS1IntSel_f + 5*idx;
            DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &clk_tree);

            /*select tx clock source, using core clock*/
            tmp_val = 1;
            fld_id = CsClkTreeCfg0_cfgClockHssTxAIntfS2IntSel_f + 5*idx;
            DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &clk_tree);
        }
    }
    else
    {
        /*Cg mode*/
         out_mode = 0;

        /*select core b source, using b0core*/
        tmp_val = 1;
        fld_id = CsClkTreeCfg0_cfgClockHssTxACoreSB0IntSel_f + 5*idx;
        DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &clk_tree);

        /*select core clock source, plla using ACore, pllb using BCore*/
        tmp_val = 1;
        fld_id = CsClkTreeCfg0_cfgClockHssTxACoreS1IntSel_f + 5*idx;
        DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &clk_tree);

        /*select tx clock source, using core clock*/
        tmp_val = 1;
        fld_id = CsClkTreeCfg0_cfgClockHssTxAIntfS2IntSel_f + 5*idx;
        DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &clk_tree);
    }
    cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &clk_tree);

    tb_id = CsMacroCfg0_t + p_hss->hss_id;
    cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &cs_macro);
    fld_id = CsMacroCfg0_cfgHssTxADataOutSel_f + 3*idx;
    DRV_IOW_FIELD(tb_id, fld_id, &out_mode, &cs_macro);
    cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &cs_macro);

    if ((p_serdes->mode == CTC_CHIP_SERDES_SGMII_MODE) || (p_serdes->mode == CTC_CHIP_SERDES_2DOT5G_MODE))
    {
        tb_id = CsMacroCfg0_t + p_hss->hss_id;
        cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &cs_macro);
        fld_id = CsMacroCfg0_cfgResetHssTxA_f + idx;
        tmp_val = 1;
        DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &cs_macro);
        cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &cs_macro);

        sal_task_sleep(1);

        cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &cs_macro);
        fld_id = CsMacroCfg0_cfgResetHssTxA_f + idx;
        tmp_val = 0;
        DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &cs_macro);
        cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &cs_macro);
    }

    tb_id = CsMacroCfg0_t + p_hss->hss_id;
    fld_id = CsMacroCfg0_cfgHssTxADataCgmacSel_f + 3*idx;
    tmp_val = (p_serdes->mode == CTC_CHIP_SERDES_CG_MODE)?1:0;
    cmd = DRV_IOW(tb_id, fld_id);
    DRV_IOCTL(lchip, 0, cmd, &tmp_val);

    return CTC_E_NONE;
}

/*
serdes tx direction clock cfg, datapath init and dynamic switch change pll cfg will use the interface.
Notice: Before Using the interface must using sys_goldengate_datapath_build_serdes_info to set serdes info
pll_type: refer to sys_datapath_pll_sel_t
*/
int32
sys_goldengate_datapath_hss15g_cfg_clktree(uint8 lchip, uint8 hss_idx, uint8 pll_type)
{
    sys_datapath_hss_attribute_t* p_hss = NULL;
    HsClkTreeCfg0_m clk_tree;
    uint8 hss_id = 0;
    uint32 cmd = 0;
    uint32 tb_id = 0;
    uint32 value = 0;

#ifdef  EMULATION_PLATFORM
    return 0;
#endif

    p_hss = ctc_vector_get(p_gg_datapath_master[lchip]->p_hss_vector, hss_idx);
    if (NULL == p_hss)
    {
        return CTC_E_NONE;
    }

    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_INFO,"Hss %d ClkTree Op BitMap:0x%x\n", hss_idx, p_hss->clktree_bitmap);

    if (p_hss->hss_type != SYS_DATAPATH_HSS_TYPE_15G)
    {
        SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"Hss parameter error, not match, hss_idx:%d \n", hss_idx);
        return CTC_E_INVALID_PARAM;
    }

    sal_memset(&clk_tree, 0, sizeof(HsClkTreeCfg0_m));

    hss_id = p_hss->hss_id;

    tb_id = HsClkTreeCfg0_t + hss_id;
    cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &clk_tree);

    if ((pll_type == SYS_DATAPATH_PLL_SEL_PLLA) || (pll_type == SYS_DATAPATH_PLL_SEL_BOTH))
    {
        SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_INFO,"Hss PllA Parameter, mode:%d, intf_div:%d, Core0_div:%d,  Core1_div:%d \n",
            p_hss->plla_mode, p_hss->intf_div_a, p_hss->core_div_a[0], p_hss->core_div_a[1] );

        /*1. cfg plla intercafe divider and core divider*/
        if (p_hss->plla_mode != SYS_DATAPATH_HSSCLK_DISABLE)
        {
            if (CTC_IS_BIT_SET(p_hss->clktree_bitmap, SYS_DATAPATH_CLKTREE_INTF_A))
            {
                value = SYS_DATAPATH_GET_CLKTREE_DIV(p_hss->intf_div_a);
                DRV_IOW_FIELD(tb_id, HsClkTreeCfg0_cfgClkDividerHssTxClk20A0Intf_f, &value, &clk_tree);
            }

            if (CTC_IS_BIT_SET(p_hss->clktree_bitmap, SYS_DATAPATH_CLKTREE_CORE_A0))
            {
                value = SYS_DATAPATH_GET_CLKTREE_DIV(p_hss->core_div_a[0]);
                DRV_IOW_FIELD(tb_id, HsClkTreeCfg0_cfgClkDividerHssTxClk20A0Core_f, &value, &clk_tree);
            }

            if (CTC_IS_BIT_SET(p_hss->clktree_bitmap, SYS_DATAPATH_CLKTREE_CORE_A1))
            {
                value = SYS_DATAPATH_GET_CLKTREE_DIV(p_hss->core_div_a[1]);
                DRV_IOW_FIELD(tb_id, HsClkTreeCfg0_cfgClkDividerHssTxClk20A1Core_f, &value, &clk_tree);
            }
        }
    }

    if ((pll_type == SYS_DATAPATH_PLL_SEL_PLLB) || (pll_type == SYS_DATAPATH_PLL_SEL_BOTH))
    {
        SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_INFO,"Hss PllB Parameter, mode:%d, intf_div:%d, Core0_div:%d,  Core1_div:%d \n",
            p_hss->pllb_mode, p_hss->intf_div_b, p_hss->core_div_b[0], p_hss->core_div_b[1] );

        /*2. cfg pllb intercafe divider and core divider*/
        if (p_hss->pllb_mode != SYS_DATAPATH_HSSCLK_DISABLE)
        {
            if (CTC_IS_BIT_SET(p_hss->clktree_bitmap, SYS_DATAPATH_CLKTREE_INTF_B))
            {
                value = SYS_DATAPATH_GET_CLKTREE_DIV(p_hss->intf_div_b);
                DRV_IOW_FIELD(tb_id, HsClkTreeCfg0_cfgClkDividerHssTxClk20B0Intf_f, &value, &clk_tree);
            }

            if (CTC_IS_BIT_SET(p_hss->clktree_bitmap, SYS_DATAPATH_CLKTREE_CORE_B0))
            {
                value = SYS_DATAPATH_GET_CLKTREE_DIV(p_hss->core_div_b[0]);
                DRV_IOW_FIELD(tb_id, HsClkTreeCfg0_cfgClkDividerHssTxClk20B0Core_f, &value, &clk_tree);
            }

            if (CTC_IS_BIT_SET(p_hss->clktree_bitmap, SYS_DATAPATH_CLKTREE_CORE_B1))
            {
                value = SYS_DATAPATH_GET_CLKTREE_DIV(p_hss->core_div_b[1]);
                DRV_IOW_FIELD(tb_id, HsClkTreeCfg0_cfgClkDividerHssTxClk20B1Core_f, &value, &clk_tree);
            }
        }
    }

    cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &clk_tree);

    /*3. do clktree reset */
    cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &clk_tree);
    value = 1;
    if (CTC_IS_BIT_SET(p_hss->clktree_bitmap, SYS_DATAPATH_CLKTREE_INTF_A))
    {
        DRV_IOW_FIELD(tb_id, HsClkTreeCfg0_cfgClkResetHssTxClk20A0Intf_f, &value, &clk_tree);
    }
    if (CTC_IS_BIT_SET(p_hss->clktree_bitmap, SYS_DATAPATH_CLKTREE_INTF_B))
    {
        DRV_IOW_FIELD(tb_id, HsClkTreeCfg0_cfgClkResetHssTxClk20B0Intf_f, &value, &clk_tree);
    }
    if (CTC_IS_BIT_SET(p_hss->clktree_bitmap, SYS_DATAPATH_CLKTREE_CORE_A0))
    {
        DRV_IOW_FIELD(tb_id, HsClkTreeCfg0_cfgClkResetHssTxClk20A0Core_f, &value, &clk_tree);
    }
    if (CTC_IS_BIT_SET(p_hss->clktree_bitmap, SYS_DATAPATH_CLKTREE_CORE_A1))
    {
        DRV_IOW_FIELD(tb_id, HsClkTreeCfg0_cfgClkResetHssTxClk20A1Core_f, &value, &clk_tree);
    }
    if (CTC_IS_BIT_SET(p_hss->clktree_bitmap, SYS_DATAPATH_CLKTREE_CORE_B0))
    {
        DRV_IOW_FIELD(tb_id, HsClkTreeCfg0_cfgClkResetHssTxClk20B0Core_f, &value, &clk_tree);
    }
    if (CTC_IS_BIT_SET(p_hss->clktree_bitmap, SYS_DATAPATH_CLKTREE_CORE_B1))
    {
        DRV_IOW_FIELD(tb_id, HsClkTreeCfg0_cfgClkResetHssTxClk20B1Core_f, &value, &clk_tree);
    }
    cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &clk_tree);

    sal_task_sleep(5);

    cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &clk_tree);
    value = 0;
    if (CTC_IS_BIT_SET(p_hss->clktree_bitmap, SYS_DATAPATH_CLKTREE_INTF_A))
    {
        DRV_IOW_FIELD(tb_id, HsClkTreeCfg0_cfgClkResetHssTxClk20A0Intf_f, &value, &clk_tree);
        CTC_BIT_UNSET(p_hss->clktree_bitmap, SYS_DATAPATH_CLKTREE_INTF_A);
    }
    if (CTC_IS_BIT_SET(p_hss->clktree_bitmap, SYS_DATAPATH_CLKTREE_INTF_B))
    {
        DRV_IOW_FIELD(tb_id, HsClkTreeCfg0_cfgClkResetHssTxClk20B0Intf_f, &value, &clk_tree);
        CTC_BIT_UNSET(p_hss->clktree_bitmap, SYS_DATAPATH_CLKTREE_INTF_B);
    }
    if (CTC_IS_BIT_SET(p_hss->clktree_bitmap, SYS_DATAPATH_CLKTREE_CORE_A0))
    {
        DRV_IOW_FIELD(tb_id, HsClkTreeCfg0_cfgClkResetHssTxClk20A0Core_f, &value, &clk_tree);
        CTC_BIT_UNSET(p_hss->clktree_bitmap, SYS_DATAPATH_CLKTREE_CORE_A0);
    }
    if (CTC_IS_BIT_SET(p_hss->clktree_bitmap, SYS_DATAPATH_CLKTREE_CORE_A1))
    {
        DRV_IOW_FIELD(tb_id, HsClkTreeCfg0_cfgClkResetHssTxClk20A1Core_f, &value, &clk_tree);
        CTC_BIT_UNSET(p_hss->clktree_bitmap, SYS_DATAPATH_CLKTREE_CORE_A1);
    }
    if (CTC_IS_BIT_SET(p_hss->clktree_bitmap, SYS_DATAPATH_CLKTREE_CORE_B0))
    {
        DRV_IOW_FIELD(tb_id, HsClkTreeCfg0_cfgClkResetHssTxClk20B0Core_f, &value, &clk_tree);
        CTC_BIT_UNSET(p_hss->clktree_bitmap, SYS_DATAPATH_CLKTREE_CORE_B0);
    }
    if (CTC_IS_BIT_SET(p_hss->clktree_bitmap, SYS_DATAPATH_CLKTREE_CORE_B1))
    {
        DRV_IOW_FIELD(tb_id, HsClkTreeCfg0_cfgClkResetHssTxClk20B1Core_f, &value, &clk_tree);
        CTC_BIT_UNSET(p_hss->clktree_bitmap, SYS_DATAPATH_CLKTREE_CORE_B1);
    }
    cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &clk_tree);
    return CTC_E_NONE;
}

/*
serdes tx direction clock cfg, datapath init and dynamic switch change pll cfg will use the interface.
Notice: Before Using the interface must using sys_goldengate_datapath_build_serdes_info to set serdes info
pll_type: refer to sys_datapath_pll_sel_t
*/
int32
sys_goldengate_datapath_hss28g_cfg_clktree(uint8 lchip, uint8 hss_idx, uint8 pll_type)
{
    sys_datapath_hss_attribute_t* p_hss = NULL;
    CsClkTreeCfg0_m clk_tree;
    uint8 hss_id = 0;
    uint32 cmd = 0;
    uint32 tb_id = 0;
    uint32 value = 0;

#ifdef  EMULATION_PLATFORM
    return 0;
#endif

    p_hss = ctc_vector_get(p_gg_datapath_master[lchip]->p_hss_vector, hss_idx);
    if (NULL == p_hss)
    {
        return CTC_E_NONE;
    }

    if (p_hss->hss_type != SYS_DATAPATH_HSS_TYPE_28G)
    {
        SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"Hss parameter error, not match, hss_idx:%d \n", hss_idx);
        return CTC_E_INVALID_PARAM;
    }

    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_INFO,"Hss %d ClkTree Op BitMap:0x%x\n", hss_idx, p_hss->clktree_bitmap);

    sal_memset(&clk_tree, 0, sizeof(CsClkTreeCfg0_m));

    hss_id = p_hss->hss_id;

    tb_id = CsClkTreeCfg0_t + hss_id;
    cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &clk_tree);

    if ((pll_type == SYS_DATAPATH_PLL_SEL_PLLA) || (pll_type == SYS_DATAPATH_PLL_SEL_BOTH))
    {
        SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_INFO,"Hss PllA Parameter, mode:%d, intf_div:%d, Core0_div:%d,  Core1_div:%d \n",
            p_hss->plla_mode, p_hss->intf_div_a, p_hss->core_div_a[0], p_hss->core_div_a[1] );

        /*1. cfg plla intercafe divider and core divider*/
        if (p_hss->plla_mode != SYS_DATAPATH_HSSCLK_DISABLE)
        {
            if (CTC_IS_BIT_SET(p_hss->clktree_bitmap, SYS_DATAPATH_CLKTREE_INTF_A))
            {
                value = SYS_DATAPATH_GET_CLKTREE_DIV(p_hss->intf_div_a);
                DRV_IOW_FIELD(tb_id, CsClkTreeCfg0_cfgClkDividerHssTxClk40A0Intf_f, &value, &clk_tree);
            }

            if (CTC_IS_BIT_SET(p_hss->clktree_bitmap, SYS_DATAPATH_CLKTREE_CORE_A0))
            {
                value = SYS_DATAPATH_GET_CLKTREE_DIV(p_hss->core_div_a[0]);
                DRV_IOW_FIELD(tb_id, CsClkTreeCfg0_cfgClkDividerHssTxClk40A0Core_f, &value, &clk_tree);
            }

            if (CTC_IS_BIT_SET(p_hss->clktree_bitmap, SYS_DATAPATH_CLKTREE_CORE_A1))
            {
                value = SYS_DATAPATH_GET_CLKTREE_DIV(p_hss->core_div_a[1]);
                DRV_IOW_FIELD(tb_id, CsClkTreeCfg0_cfgClkDividerHssTxClk40A1Core_f, &value, &clk_tree);
            }
        }
    }

    if ((pll_type == SYS_DATAPATH_PLL_SEL_PLLB) || (pll_type == SYS_DATAPATH_PLL_SEL_BOTH))
    {
        SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_INFO,"Hss PllB Parameter, mode:%d, intf_div:%d, Core0_div:%d,  Core1_div:%d \n",
            p_hss->pllb_mode, p_hss->intf_div_b, p_hss->core_div_b[0], p_hss->core_div_b[1] );
        /*2. cfg pllb intercafe divider and core divider*/
        if (p_hss->pllb_mode != SYS_DATAPATH_HSSCLK_DISABLE)
        {
            if (CTC_IS_BIT_SET(p_hss->clktree_bitmap, SYS_DATAPATH_CLKTREE_INTF_B))
            {
                value = SYS_DATAPATH_GET_CLKTREE_DIV(p_hss->intf_div_b);
                DRV_IOW_FIELD(tb_id, CsClkTreeCfg0_cfgClkDividerHssTxClk40B0Intf_f, &value, &clk_tree);
            }

            if (CTC_IS_BIT_SET(p_hss->clktree_bitmap, SYS_DATAPATH_CLKTREE_CORE_B0))
            {
                value = SYS_DATAPATH_GET_CLKTREE_DIV(p_hss->core_div_b[0]);
                DRV_IOW_FIELD(tb_id, CsClkTreeCfg0_cfgClkDividerHssTxClk40B0Core_f, &value, &clk_tree);
            }

            if (CTC_IS_BIT_SET(p_hss->clktree_bitmap, SYS_DATAPATH_CLKTREE_CORE_B1))
            {
                value = SYS_DATAPATH_GET_CLKTREE_DIV(p_hss->core_div_b[1]);
                DRV_IOW_FIELD(tb_id, CsClkTreeCfg0_cfgClkDividerHssTxClk40B1Core_f, &value, &clk_tree);
            }
        }
    }
    cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &clk_tree);

    /*3. do clktree reset */
    cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &clk_tree);
    value = 1;
    if (CTC_IS_BIT_SET(p_hss->clktree_bitmap, SYS_DATAPATH_CLKTREE_INTF_A))
    {
        DRV_IOW_FIELD(tb_id, CsClkTreeCfg0_cfgClkResetHssTxClk40A0Intf_f, &value, &clk_tree);
    }
    if (CTC_IS_BIT_SET(p_hss->clktree_bitmap, SYS_DATAPATH_CLKTREE_INTF_B))
    {
        DRV_IOW_FIELD(tb_id, CsClkTreeCfg0_cfgClkResetHssTxClk40B0Intf_f, &value, &clk_tree);
    }
    if (CTC_IS_BIT_SET(p_hss->clktree_bitmap, SYS_DATAPATH_CLKTREE_CORE_A0))
    {
        DRV_IOW_FIELD(tb_id, CsClkTreeCfg0_cfgClkResetHssTxClk40A0Core_f, &value, &clk_tree);
    }
    if (CTC_IS_BIT_SET(p_hss->clktree_bitmap, SYS_DATAPATH_CLKTREE_CORE_A1))
    {
        DRV_IOW_FIELD(tb_id, CsClkTreeCfg0_cfgClkResetHssTxClk40A1Core_f, &value, &clk_tree);
    }
    if (CTC_IS_BIT_SET(p_hss->clktree_bitmap, SYS_DATAPATH_CLKTREE_CORE_B0))
    {
        DRV_IOW_FIELD(tb_id, CsClkTreeCfg0_cfgClkResetHssTxClk40B0Core_f, &value, &clk_tree);
    }
    if (CTC_IS_BIT_SET(p_hss->clktree_bitmap, SYS_DATAPATH_CLKTREE_CORE_B1))
    {
        DRV_IOW_FIELD(tb_id, CsClkTreeCfg0_cfgClkResetHssTxClk40B1Core_f, &value, &clk_tree);
    }
    cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &clk_tree);

    sal_task_sleep(5);

    cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &clk_tree);
    value = 0;
    if (CTC_IS_BIT_SET(p_hss->clktree_bitmap, SYS_DATAPATH_CLKTREE_INTF_A))
    {
        DRV_IOW_FIELD(tb_id, CsClkTreeCfg0_cfgClkResetHssTxClk40A0Intf_f, &value, &clk_tree);
        CTC_BIT_UNSET(p_hss->clktree_bitmap, SYS_DATAPATH_CLKTREE_INTF_A);
    }
    if (CTC_IS_BIT_SET(p_hss->clktree_bitmap, SYS_DATAPATH_CLKTREE_INTF_B))
    {
        DRV_IOW_FIELD(tb_id, CsClkTreeCfg0_cfgClkResetHssTxClk40B0Intf_f, &value, &clk_tree);
        CTC_BIT_UNSET(p_hss->clktree_bitmap, SYS_DATAPATH_CLKTREE_INTF_B);
    }
    if (CTC_IS_BIT_SET(p_hss->clktree_bitmap, SYS_DATAPATH_CLKTREE_CORE_A0))
    {
        DRV_IOW_FIELD(tb_id, CsClkTreeCfg0_cfgClkResetHssTxClk40A0Core_f, &value, &clk_tree);
        CTC_BIT_UNSET(p_hss->clktree_bitmap, SYS_DATAPATH_CLKTREE_CORE_A0);
    }
    if (CTC_IS_BIT_SET(p_hss->clktree_bitmap, SYS_DATAPATH_CLKTREE_CORE_A1))
    {
        DRV_IOW_FIELD(tb_id, CsClkTreeCfg0_cfgClkResetHssTxClk40A1Core_f, &value, &clk_tree);
        CTC_BIT_UNSET(p_hss->clktree_bitmap, SYS_DATAPATH_CLKTREE_CORE_A1);
    }
    if (CTC_IS_BIT_SET(p_hss->clktree_bitmap, SYS_DATAPATH_CLKTREE_CORE_B0))
    {
        DRV_IOW_FIELD(tb_id, CsClkTreeCfg0_cfgClkResetHssTxClk40B0Core_f, &value, &clk_tree);
        CTC_BIT_UNSET(p_hss->clktree_bitmap, SYS_DATAPATH_CLKTREE_CORE_B0);
    }
    if (CTC_IS_BIT_SET(p_hss->clktree_bitmap, SYS_DATAPATH_CLKTREE_CORE_B1))
    {
        DRV_IOW_FIELD(tb_id, CsClkTreeCfg0_cfgClkResetHssTxClk40B1Core_f, &value, &clk_tree);
        CTC_BIT_UNSET(p_hss->clktree_bitmap, SYS_DATAPATH_CLKTREE_CORE_B1);
    }
    cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &clk_tree);

    return CTC_E_NONE;
}

/*
   this interface is user to init hss15g, notice dynamic can not using this interface to config hss,
   because here will do hss core reset, will affect other serdes.
*/
STATIC int32
_sys_goldengate_datapath_hss15g_init(uint8 lchip, uint8 hss_idx)
{
    uint32 tb_id = 0;
    uint32 cmd = 0;
    uint32 fld_id = 0;
    uint32 value = 0;
    HsCfg0_m hs_cfg;
    HsMacroMiscMon0_m hs_mon;
    uint32 mult = 0;
    uint32 timeout = 0x4000;
    uint8 plla_ready = 0;
    uint8 pllb_ready = 0;
    uint8 plla_used = 0;
    uint8 pllb_used = 0;
    sys_datapath_hss_attribute_t* p_hss = NULL;
    uint8 hss_id = 0;

#ifdef  EMULATION_PLATFORM
    return 0;
#endif

    sal_memset(&hs_cfg, 0, sizeof(HsCfg0_m));
    sal_memset(&hs_mon, 0, sizeof(HsMacroMiscMon0_m));

    p_hss = ctc_vector_get(p_gg_datapath_master[lchip]->p_hss_vector, hss_idx);
    if (NULL == p_hss)
    {
         /*SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"Hss %d not used\n", hss_idx);*/
        return CTC_E_NONE;
    }

    if (p_hss->hss_type != SYS_DATAPATH_HSS_TYPE_15G)
    {
        SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"Hss parameter error, not match, hss_idx:%d \n", hss_idx);
        return CTC_E_INVALID_PARAM;
    }

    hss_id = p_hss->hss_id;

    /*1. release sup reset for hss15g, Notice hss_id is 0~9 for hss15g, not hss_idx */
    if (hss_id <= 4)
    {
        tb_id = (hss_id == 4)?RlmCsCtlReset0_t: RlmHsCtlReset0_t;
        fld_id = (hss_id == 4)?RlmCsCtlReset0_resetCoreHss15GUnitWrapper4_f:
            (RlmHsCtlReset0_resetCoreHss15GUnitWrapper0_f+hss_id%4);
    }
    else if (hss_id <= 9)
    {
        tb_id = (hss_id == 9)? RlmCsCtlReset1_t: RlmHsCtlReset1_t;
        fld_id = (hss_id == 9)?RlmCsCtlReset1_resetCoreHss15GUnitWrapper4_f:
            (RlmHsCtlReset1_resetCoreHss15GUnitWrapper0_f+(hss_id-HSS15G_NUM_PER_SLICE)%4);
    }
    value = 0;
    cmd = DRV_IOW(tb_id, fld_id);
    DRV_IOCTL(lchip, 0, cmd, &value);

    /*2. set hss15g core reset */
    tb_id = HsCfg0_t + hss_id;
    value = 1;
    cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &hs_cfg);
    DRV_IOW_FIELD(tb_id, HsCfg0_cfgHssReset_f, &value, &hs_cfg);
    cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &hs_cfg);

    plla_used = (p_hss->plla_mode != SYS_DATAPATH_HSSCLK_DISABLE)?1:0;
    pllb_used = (p_hss->pllb_mode != SYS_DATAPATH_HSSCLK_DISABLE)?1:0;

    /*3. set hss15g pin input */
    if (plla_used)
    {
        SYS_DATAPATH_GET_HSS15G_MULT(p_hss->plla_mode, mult);
        value = 0;
        DRV_IOW_FIELD(tb_id, HsCfg0_cfgHssPdwnPllA_f, &value, &hs_cfg);
        DRV_IOW_FIELD(tb_id, HsCfg0_cfgHssDivSelA_f, &mult, &hs_cfg);

        if ((p_hss->plla_mode == SYS_DATAPATH_HSSCLK_515DOT625) || (p_hss->plla_mode == SYS_DATAPATH_HSSCLK_500))
        {
            value = 0;
            DRV_IOW_FIELD(tb_id, HsCfg0_cfgHssVcoSelA_f, &value, &hs_cfg);
        }
        else
        {
            value = 1;
            DRV_IOW_FIELD(tb_id, HsCfg0_cfgHssVcoSelA_f, &value, &hs_cfg);
        }
    }
    else
    {
        value = 0;
        DRV_IOW_FIELD(tb_id, HsCfg0_cfgHssRefClkValidA_f, &value, &hs_cfg);
    }

    if (pllb_used)
    {
        SYS_DATAPATH_GET_HSS15G_MULT(p_hss->pllb_mode, mult);
        value = 0;
        DRV_IOW_FIELD(tb_id, HsCfg0_cfgHssPdwnPllB_f, &value, &hs_cfg);
        DRV_IOW_FIELD(tb_id, HsCfg0_cfgHssDivSelB_f, &mult, &hs_cfg);
        if (p_hss->pllb_mode == SYS_DATAPATH_HSSCLK_644DOT53125)
        {
            value = 1;
            DRV_IOW_FIELD(tb_id, HsCfg0_cfgHssVcoSelB_f, &value, &hs_cfg);
        }
        else
        {
            value = 0;
            DRV_IOW_FIELD(tb_id, HsCfg0_cfgHssVcoSelB_f, &value, &hs_cfg);
        }
    }
    else
    {
        value = 0;
        DRV_IOW_FIELD(tb_id, HsCfg0_cfgHssRefClkValidB_f, &value, &hs_cfg);
    }

    cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &hs_cfg);

    sal_task_sleep(1);

    /*4. release hss15g core reset */
    tb_id = HsCfg0_t + hss_id;
    value = 0;
    DRV_IOW_FIELD(tb_id, HsCfg0_cfgHssReset_f, &value, &hs_cfg);
    cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &hs_cfg);

    sal_task_sleep(1);

    /*5. wait hssprtready assert*/
    tb_id = HsMacroMiscMon0_t + hss_id;
    cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &hs_mon);

    if (0 == SDK_WORK_PLATFORM)
    {
        while(--timeout)
        {
            DRV_IOCTL(lchip, 0, cmd, &hs_mon);
            if (p_hss->plla_mode != SYS_DATAPATH_HSSCLK_DISABLE)
            {
                if (GetHsMacroMiscMon0(V,monHssPrtReadyA_f, &hs_mon))
                {
                    plla_ready = 1;
                }
            }

            if (p_hss->pllb_mode != SYS_DATAPATH_HSSCLK_DISABLE)
            {
                if (GetHsMacroMiscMon0(V,monHssPrtReadyB_f, &hs_mon))
                {
                    pllb_ready = 1;
                }
            }

            if (((plla_used && plla_ready) || (!plla_used)) &&
                ((pllb_used && pllb_ready) || (!pllb_used)))
            {
                break;
            }
        }

        /*check ready*/
        if ((plla_used && !plla_ready) || (pllb_used && !pllb_ready))
        {
            SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"HssIdx %d \n", hss_idx);
            return CTC_E_DATAPATH_HSS_NOT_READY;
        }
    }
    return CTC_E_NONE;
}

/*
   this interface is user to init hss28g, notice dynamic can not using this interface to config hss,
   because here will do hss core reset, will affect other serdes.
*/
STATIC int32
_sys_goldengate_datapath_hss28g_init(uint8 lchip, uint8 hss_idx)
{
    sys_datapath_hss_attribute_t* p_hss = NULL;
    uint8 hss_id = 0;
    uint32 tb_id = 0;
    uint32 cmd = 0;
    uint32 fld_id = 0;
    uint32 value = 0;
    CsCfg0_m cs_cfg;
    CsMacroMiscMon0_m cs_mon;
    uint32 mult = 0;
    uint32 timeout = 0x4000;
    uint8 plla_ready = 0;
    uint8 pllb_ready = 0;
    uint8 plla_used = 0;
    uint8 pllb_used = 0;

#ifdef  EMULATION_PLATFORM
    return 0;
#endif

    p_hss = ctc_vector_get(p_gg_datapath_master[lchip]->p_hss_vector, hss_idx);
    if (NULL == p_hss)
    {
         /*SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"Hss %d not used\n", hss_idx);*/
        return CTC_E_NONE;
    }

    if (p_hss->hss_type != SYS_DATAPATH_HSS_TYPE_28G)
    {
        SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"Hss parameter error, not match, hss_idx:%d \n", hss_idx);
        return CTC_E_INVALID_PARAM;
    }

    sal_memset(&cs_cfg, 0, sizeof(CsCfg0_m));
    sal_memset(&cs_mon, 0, sizeof(CsMacroMiscMon0_m));

    hss_id = p_hss->hss_id;

    /*1. reset sup config to make hss28g marco can access*/
    tb_id = (hss_id < 2)? RlmCsCtlReset0_t: RlmCsCtlReset1_t;
    fld_id = RlmCsCtlReset0_resetCoreHss28GUnitWrapper0_f + hss_id%HSS28G_NUM_PER_SLICE;
    value = 0;
    cmd = DRV_IOW(tb_id, fld_id);
    DRV_IOCTL(lchip, 0, cmd, &value);

    /*2. set hss28g core reset */
    tb_id = CsCfg0_t + hss_id;
    cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &cs_cfg);
    value = 1;
    DRV_IOW_FIELD(tb_id, CsCfg0_cfgHssReset_f, &value, &cs_cfg);
    cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &cs_cfg);

    /*3. set hss28g pin input */
    plla_used = (p_hss->plla_mode != SYS_DATAPATH_HSSCLK_DISABLE)?1:0;
    pllb_used = (p_hss->pllb_mode != SYS_DATAPATH_HSSCLK_DISABLE)?1:0;

    if (plla_used)
    {
        SYS_DATAPATH_GET_HSS28G_MULT(p_hss->plla_mode, mult);
        value = 0;
        DRV_IOW_FIELD(tb_id, CsCfg0_cfgHssPdwnPllA_f, &value, &cs_cfg);
        DRV_IOW_FIELD(tb_id, CsCfg0_cfgHssDivSelA_f, &mult, &cs_cfg);

        /*plla just using VCO:20G,20.625G*/
        value = 1;
        DRV_IOW_FIELD(tb_id, CsCfg0_cfgHssVcoSelA_f, &value, &cs_cfg);

        if (p_hss->plla_mode == SYS_DATAPATH_HSSCLK_500)
        {
            value = 1;
            DRV_IOW_FIELD(tb_id, CsCfg0_cfgHssPllDiv2A_f, &value, &cs_cfg);
        }
        else
        {
            value = 0;
            DRV_IOW_FIELD(tb_id, CsCfg0_cfgHssPllDiv2A_f, &value, &cs_cfg);
        }
    }
    else
    {
        value = 0;
        DRV_IOW_FIELD(tb_id, CsCfg0_cfgHssRefClkValidA_f, &value, &cs_cfg);
    }

    if (pllb_used)
    {
        SYS_DATAPATH_GET_HSS28G_MULT(p_hss->pllb_mode, mult);
        value = 0;
        DRV_IOW_FIELD(tb_id, CsCfg0_cfgHssPdwnPllB_f, &value, &cs_cfg);
        DRV_IOW_FIELD(tb_id, CsCfg0_cfgHssDivSelB_f, &mult, &cs_cfg);

        if (p_hss->pllb_mode == SYS_DATAPATH_HSSCLK_625)
        {
            value = 1;
            DRV_IOW_FIELD(tb_id, CsCfg0_cfgHssVcoSelB_f, &value, &cs_cfg);
            DRV_IOW_FIELD(tb_id, CsCfg0_cfgHssPllDiv2B_f, &value, &cs_cfg);
        }
        else if (p_hss->pllb_mode == SYS_DATAPATH_HSSCLK_500)
        {
            value = 0;
            DRV_IOW_FIELD(tb_id, CsCfg0_cfgHssVcoSelB_f, &value, &cs_cfg);
            value = 1;
            DRV_IOW_FIELD(tb_id, CsCfg0_cfgHssPllDiv2B_f, &value, &cs_cfg);
        }
        else if (p_hss->pllb_mode == SYS_DATAPATH_HSSCLK_515DOT625)
        {
            value = 0;
            DRV_IOW_FIELD(tb_id, CsCfg0_cfgHssVcoSelB_f, &value, &cs_cfg);
            DRV_IOW_FIELD(tb_id, CsCfg0_cfgHssPllDiv2B_f, &value, &cs_cfg);
        }
        else
        {
            value = 1;
            DRV_IOW_FIELD(tb_id, CsCfg0_cfgHssVcoSelB_f, &value, &cs_cfg);
            value = 0;
            DRV_IOW_FIELD(tb_id, CsCfg0_cfgHssPllDiv2B_f, &value, &cs_cfg);
        }
    }
    else
    {
        value = 0;
        DRV_IOW_FIELD(tb_id, CsCfg0_cfgHssRefClkValidB_f, &value, &cs_cfg);
    }

    cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &cs_cfg);

    sal_task_sleep(1);

    /*4. release hss28g core reset */
    tb_id = CsCfg0_t + hss_id;
    value = 0;
    DRV_IOW_FIELD(tb_id, CsCfg0_cfgHssReset_f, &value, &cs_cfg);
    cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &cs_cfg);

    sal_task_sleep(1);

    /*5. wait hssprtready ready assert */
    tb_id = CsMacroMiscMon0_t + hss_id;
    cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &cs_mon);

    if (0 == SDK_WORK_PLATFORM)
    {
        while(--timeout)
        {
            DRV_IOCTL(lchip, 0, cmd, &cs_mon);
            if (p_hss->plla_mode != SYS_DATAPATH_HSSCLK_DISABLE)
            {
                if (GetCsMacroMiscMon0(V,monHssPrtReadyA_f, &cs_mon))
                {
                    plla_ready = 1;
                }
            }

            if (p_hss->pllb_mode != SYS_DATAPATH_HSSCLK_DISABLE)
            {
                if (GetCsMacroMiscMon0(V,monHssPrtReadyB_f, &cs_mon))
                {
                    pllb_ready = 1;
                }
            }

            if (((plla_used && plla_ready) || (!plla_used)) &&
                ((pllb_used && pllb_ready) || (!pllb_used)))
            {
                break;
            }
        }

        /*check ready*/
        if ((plla_used && !plla_ready) || (pllb_used && !pllb_ready))
        {
            SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"HssIdx %d \n", hss_idx);
            return CTC_E_DATAPATH_HSS_NOT_READY;
        }
    }

    return CTC_E_NONE;
}

/*
   Used to change hss15g plla/pllb cfg, using for dynamic switch
   Notice: Before using the function, must confirm pll is not used by any serdes
   pll_type: refer to sys_datapath_pll_sel_t
   pll_clk:   refer to sys_datapath_hssclk_t
*/
int32
sys_goldengate_datapath_hss15g_pll_cfg(uint8 lchip, uint8 hss_idx, uint8 pll_type, uint8 pll_clk)
{
    sys_datapath_hss_attribute_t* p_hss = NULL;
    uint32 mult = 0;
    uint32 tb_id = 0;
    HsCfg0_m hs_cfg;
    uint32 cmd = 0;
    uint32 value = 0;
    HsMacroMiscMon0_m hs_mon;
    uint32 timeout = 0x4000;
    uint8 pll_ready = 0;

    p_hss = ctc_vector_get(p_gg_datapath_master[lchip]->p_hss_vector, hss_idx);
    if (NULL == p_hss)
    {
        SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"Hss %d not used\n", hss_idx);
        return CTC_E_NONE;
    }

    if (p_hss->hss_type != SYS_DATAPATH_HSS_TYPE_15G)
    {
        return CTC_E_INVALID_PARAM;
    }

    tb_id = HsCfg0_t + p_hss->hss_id;
    if (pll_type == SYS_DATAPATH_PLL_SEL_PLLA)
    {
        if (pll_clk == SYS_DATAPATH_HSSCLK_DISABLE)
        {
             /*cannot disable ref clock, because pll still have lane, if set disable will cause serdes cannot op*/
            cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &hs_cfg);
            value = 1;
            DRV_IOW_FIELD(tb_id, HsCfg0_cfgHssRefClkValidA_f, &value, &hs_cfg);
            cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &hs_cfg);
        }
        else
        {
            /*1. enable ref clock */
            cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &hs_cfg);
            value = 1;
            DRV_IOW_FIELD(tb_id, HsCfg0_cfgHssRefClkValidA_f, &value, &hs_cfg);
            cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &hs_cfg);

            sal_task_sleep(1);

            /*2. change pll config */
            cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &hs_cfg);
            SYS_DATAPATH_GET_HSS15G_MULT(pll_clk, mult);
            DRV_IOW_FIELD(tb_id, HsCfg0_cfgHssDivSelA_f, &mult, &hs_cfg);

            if ((pll_clk == SYS_DATAPATH_HSSCLK_515DOT625) || (pll_clk == SYS_DATAPATH_HSSCLK_500))
            {
                value = 0;
                DRV_IOW_FIELD(tb_id, HsCfg0_cfgHssVcoSelA_f, &value, &hs_cfg);
            }
            else
            {
                value = 1;
                DRV_IOW_FIELD(tb_id, HsCfg0_cfgHssVcoSelA_f, &value, &hs_cfg);
            }
            cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &hs_cfg);

            /*3. set pll recalculate*/
            cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &hs_cfg);
            value = 1;
            DRV_IOW_FIELD(tb_id, HsCfg0_cfgHssRecCalA_f, &value, &hs_cfg);
            cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &hs_cfg);

            sal_task_sleep(1);

            cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &hs_cfg);
            value = 0;
            DRV_IOW_FIELD(tb_id, HsCfg0_cfgHssRecCalA_f, &value, &hs_cfg);
            cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &hs_cfg);
        }

    }
    else if (pll_type == SYS_DATAPATH_PLL_SEL_PLLB)
    {
        if (pll_clk == SYS_DATAPATH_HSSCLK_DISABLE)
        {
             /*cannot disable ref clock, because pll still have lane, if set disable will cause serdes cannot op*/
            cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &hs_cfg);
            value = 1;
            DRV_IOW_FIELD(tb_id, HsCfg0_cfgHssRefClkValidB_f, &value, &hs_cfg);
            cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &hs_cfg);
        }
        else
        {
            /*1. enable ref clock */
            cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &hs_cfg);
            value = 1;
            DRV_IOW_FIELD(tb_id, HsCfg0_cfgHssRefClkValidB_f, &value, &hs_cfg);
            cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &hs_cfg);

            sal_task_sleep(1);

            /*2. change pll config */
            cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &hs_cfg);
            SYS_DATAPATH_GET_HSS15G_MULT(pll_clk, mult);
            value = 0;
            DRV_IOW_FIELD(tb_id, HsCfg0_cfgHssDivSelB_f, &mult, &hs_cfg);
            if (pll_clk == SYS_DATAPATH_HSSCLK_644DOT53125)
            {
                value = 1;
                DRV_IOW_FIELD(tb_id, HsCfg0_cfgHssVcoSelB_f, &value, &hs_cfg);
            }
            else
            {
                value = 0;
                DRV_IOW_FIELD(tb_id, HsCfg0_cfgHssVcoSelB_f, &value, &hs_cfg);
            }
            cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &hs_cfg);

            /*3. set pll recalculate*/
            cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &hs_cfg);
            value = 1;
            DRV_IOW_FIELD(tb_id, HsCfg0_cfgHssRecCalB_f, &value, &hs_cfg);
            cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &hs_cfg);

            sal_task_sleep(1);

            cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &hs_cfg);
            value = 0;
            DRV_IOW_FIELD(tb_id, HsCfg0_cfgHssRecCalB_f, &value, &hs_cfg);
            cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &hs_cfg);
        }
    }

    sal_task_sleep(10);

    /*5. wait hssptrready*/
    if (pll_clk != SYS_DATAPATH_HSSCLK_DISABLE)
    {
        tb_id = HsMacroMiscMon0_t + p_hss->hss_id;
        cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &hs_mon);

        if (0 == SDK_WORK_PLATFORM)
        {
            while(--timeout)
            {
                if (pll_type == SYS_DATAPATH_PLL_SEL_PLLA)
                {
                    if (GetHsMacroMiscMon0(V,monHssPrtReadyA_f, &hs_mon))
                    {
                        pll_ready = 1;
                    }
                }
                else if (pll_type == SYS_DATAPATH_PLL_SEL_PLLB)
                {
                    if (GetHsMacroMiscMon0(V,monHssPrtReadyB_f, &hs_mon))
                    {
                        pll_ready = 1;
                    }
                }

                if (pll_ready)
                {
                    break;
                }
            }

            /*check ready*/
            if (!pll_ready)
            {
                return CTC_E_DATAPATH_HSS_NOT_READY;
            }
        }
    }

    return CTC_E_NONE;
}

/*
   Used to change hss28g plla/pllb cfg, using for dynamic switch
   Notice: Before using the function, must confirm pll is not used by any serdes
   pll_type: refer to sys_datapath_pll_sel_t
   pll_clk:   refer to sys_datapath_hssclk_t
*/
int32
sys_goldengate_datapath_hss28g_pll_cfg(uint8 lchip, uint8 hss_idx, uint8 pll_type, uint8 pll_clk)
{
    sys_datapath_hss_attribute_t* p_hss = NULL;
    uint32 mult = 0;
    uint32 tb_id = 0;
    CsCfg0_m cs_cfg;
    uint32 cmd = 0;
    uint32 value = 0;
    CsMacroMiscMon0_m cs_mon;
    uint32 timeout = 0x4000;
    uint8 pll_ready = 0;

    p_hss = ctc_vector_get(p_gg_datapath_master[lchip]->p_hss_vector, hss_idx);
    if (NULL == p_hss)
    {
        SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"Hss %d not used\n", hss_idx);
        return CTC_E_NONE;
    }

    if (p_hss->hss_type != SYS_DATAPATH_HSS_TYPE_28G)
    {
        return CTC_E_INVALID_PARAM;
    }

    tb_id = CsCfg0_t + p_hss->hss_id;
    if (pll_type == SYS_DATAPATH_PLL_SEL_PLLA)
    {
        if (pll_clk == SYS_DATAPATH_HSSCLK_DISABLE)
        {
            /*cannot disable ref clock, because pll still have lane, if set disable will cause serdes cannot op*/
            cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &cs_cfg);
            value = 1;
            DRV_IOW_FIELD(tb_id, CsCfg0_cfgHssRefClkValidA_f, &value, &cs_cfg);
            cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &cs_cfg);
        }
        else
        {
            /*1. Enable ref clock */
            cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &cs_cfg);
            value = 1;
            DRV_IOW_FIELD(tb_id, CsCfg0_cfgHssRefClkValidA_f, &value, &cs_cfg);
            cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &cs_cfg);

            sal_task_sleep(1);

            /*2. change pll config */
            cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &cs_cfg);

            SYS_DATAPATH_GET_HSS28G_MULT(pll_clk, mult);
            DRV_IOW_FIELD(tb_id, CsCfg0_cfgHssDivSelA_f, &mult, &cs_cfg);

            /*plla just using VCO:20G,20.625G*/
            value = 1;
            DRV_IOW_FIELD(tb_id, CsCfg0_cfgHssVcoSelA_f, &value, &cs_cfg);

            if (pll_clk == SYS_DATAPATH_HSSCLK_500)
            {
                value = 1;
                DRV_IOW_FIELD(tb_id, CsCfg0_cfgHssPllDiv2A_f, &value, &cs_cfg);
            }
            else
            {
                value = 0;
                DRV_IOW_FIELD(tb_id, CsCfg0_cfgHssPllDiv2A_f, &value, &cs_cfg);
            }
            cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &cs_cfg);

            /*3. set pll recalculate*/
            cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &cs_cfg);
            value = 1;
            DRV_IOW_FIELD(tb_id, CsCfg0_cfgHssRecCalA_f, &value, &cs_cfg);
            cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &cs_cfg);

            sal_task_sleep(1);

            cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &cs_cfg);
            value = 0;
            DRV_IOW_FIELD(tb_id, CsCfg0_cfgHssRecCalA_f, &value, &cs_cfg);
            cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &cs_cfg);
        }

    }
    else if (pll_type == SYS_DATAPATH_PLL_SEL_PLLB)
    {
        if (pll_clk == SYS_DATAPATH_HSSCLK_DISABLE)
        {
             /*cannot disable ref clock, because pll still have lane, if set disable will cause serdes cannot op*/
            cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &cs_cfg);
            value = 1;
            DRV_IOW_FIELD(tb_id, CsCfg0_cfgHssRefClkValidB_f, &value, &cs_cfg);
            cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &cs_cfg);
        }
        else
        {
            /*1. enable ref clock */
            cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &cs_cfg);
            value = 1;
            DRV_IOW_FIELD(tb_id, CsCfg0_cfgHssRefClkValidB_f, &value, &cs_cfg);
            cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &cs_cfg);

            sal_task_sleep(1);

            /*2. change pll config */
            cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &cs_cfg);
            SYS_DATAPATH_GET_HSS28G_MULT(pll_clk, mult);
            value = 0;
            DRV_IOW_FIELD(tb_id, CsCfg0_cfgHssDivSelB_f, &mult, &cs_cfg);

            if (pll_clk == SYS_DATAPATH_HSSCLK_625)
            {
                value = 1;
                DRV_IOW_FIELD(tb_id, CsCfg0_cfgHssVcoSelB_f, &value, &cs_cfg);
                DRV_IOW_FIELD(tb_id, CsCfg0_cfgHssPllDiv2B_f, &value, &cs_cfg);
            }
            else if (pll_clk == SYS_DATAPATH_HSSCLK_500)
            {
                value = 0;
                DRV_IOW_FIELD(tb_id, CsCfg0_cfgHssVcoSelB_f, &value, &cs_cfg);
                value = 1;
                DRV_IOW_FIELD(tb_id, CsCfg0_cfgHssPllDiv2B_f, &value, &cs_cfg);
            }
            else if (pll_clk == SYS_DATAPATH_HSSCLK_515DOT625)
            {
                value = 0;
                DRV_IOW_FIELD(tb_id, CsCfg0_cfgHssVcoSelB_f, &value, &cs_cfg);
                DRV_IOW_FIELD(tb_id, CsCfg0_cfgHssPllDiv2B_f, &value, &cs_cfg);
            }
            else
            {
                value = 1;
                DRV_IOW_FIELD(tb_id, CsCfg0_cfgHssVcoSelB_f, &value, &cs_cfg);
                value = 0;
                DRV_IOW_FIELD(tb_id, CsCfg0_cfgHssPllDiv2B_f, &value, &cs_cfg);
            }
            cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &cs_cfg);

            /*3. set pll recalculate*/
            cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &cs_cfg);
            value = 1;
            DRV_IOW_FIELD(tb_id, CsCfg0_cfgHssRecCalB_f, &value, &cs_cfg);
            cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &cs_cfg);

            sal_task_sleep(1);

            cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &cs_cfg);
            value = 0;
            DRV_IOW_FIELD(tb_id, CsCfg0_cfgHssRecCalB_f, &value, &cs_cfg);
            cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &cs_cfg);
        }
    }

    sal_task_sleep(10);

    /*4. wait hssptrready*/
    if (pll_clk != SYS_DATAPATH_HSSCLK_DISABLE)
    {
        tb_id = CsMacroMiscMon0_t + p_hss->hss_id;
        cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &cs_mon);

        if (0 == SDK_WORK_PLATFORM)
        {
            while(--timeout)
            {
                if (pll_type == SYS_DATAPATH_PLL_SEL_PLLA)
                {
                    if (GetCsMacroMiscMon0(V,monHssPrtReadyA_f, &cs_mon))
                    {
                        pll_ready = 1;
                    }
                }
                else if (pll_type == SYS_DATAPATH_PLL_SEL_PLLB)
                {
                    if (GetCsMacroMiscMon0(V,monHssPrtReadyB_f, &cs_mon))
                    {
                        pll_ready = 1;
                    }
                }

                if (pll_ready)
                {
                    break;
                }
            }

            /*check ready*/
            if (!pll_ready)
            {
                return CTC_E_DATAPATH_HSS_NOT_READY;
            }
        }
    }

    return CTC_E_NONE;
}

/*
   Used to build serdes info softeware, prepare for set serdes mode
*/
int32
sys_goldengate_datapath_build_serdes_info(uint8 lchip, uint8 serdes_id, uint8 mode, uint8 is_dyn, uint16 lport_serdes, uint16 overclocking_speed)
{
    sys_datapath_hss_attribute_t* p_hss_vec = NULL;
    sys_datapath_lport_attr_t* p_port_attr = NULL;
    uint8 hss_idx = 0;
    uint8 hss_id = 0;
    uint8 lane_id = 0;
    uint8 hssclk = 0;
    uint8 rate_div = 0;
    uint8 bit_width = 0;
    uint8 core_div = 0;
    uint8 intf_div = 0;
    uint16 lport = 0;
    uint8 chan_id = 0;
    uint8 mac_id = 0;
    uint8 need_add = 0;
    uint8 speed_mode = 0;
    uint8 slice_id = 0;

    /*hss_idx used to index vector, hss_id is actual id*/
    hss_idx = SYS_DATAPATH_MAP_SERDES_TO_HSS_INDEX(serdes_id);
    hss_id = SYS_DATAPATH_MAP_SERDES_TO_HSSID(serdes_id);
    lane_id = SYS_DATAPATH_MAP_SERDES_TO_LANE_ID(serdes_id);

    if (SYS_DATAPATH_SERDES_IS_HSS28G(serdes_id))
    {
        CTC_ERROR_RETURN(_sys_goldengate_datapath_get_rate(lchip, mode,SYS_DATAPATH_HSS_TYPE_28G,
            &hssclk, &bit_width, &rate_div, &core_div, &intf_div, overclocking_speed));
    }
    else
    {
        CTC_ERROR_RETURN(_sys_goldengate_datapath_get_rate(lchip, mode,SYS_DATAPATH_HSS_TYPE_15G,
            &hssclk, &bit_width, &rate_div, &core_div, &intf_div, overclocking_speed));
    }

    need_add = 0;
    p_hss_vec = ctc_vector_get(p_gg_datapath_master[lchip]->p_hss_vector, hss_idx);
    if (p_hss_vec == NULL)
    {
        /*first usefull serdes in hss*/
        p_hss_vec = (sys_datapath_hss_attribute_t*)mem_malloc(MEM_SYSTEM_MODULE, sizeof(sys_datapath_hss_attribute_t));
        if (NULL == p_hss_vec)
        {
            return CTC_E_NO_MEMORY;
        }

        sal_memset(p_hss_vec, 0, sizeof(sys_datapath_hss_attribute_t));

        p_hss_vec->hss_id = hss_id;
        p_hss_vec->hss_type = SYS_DATAPATH_SERDES_IS_HSS28G(serdes_id)?1:0;

        /*using plla first, if serdes is cg must using pllb*/
        if (SYS_DATAPATH_SERDES_IS_HSS28G(serdes_id))
        {
            if ((SYS_DATAPATH_HSSCLK_625 == hssclk) || (SYS_DATAPATH_HSSCLK_644DOT53125 == hssclk))
            {
                p_hss_vec->pllb_mode = hssclk;
                p_hss_vec->intf_div_b = intf_div;
                p_hss_vec->core_div_b[0] = core_div;
                p_hss_vec->pllb_ref_cnt++;
                CTC_BIT_SET(p_hss_vec->clktree_bitmap, SYS_DATAPATH_CLKTREE_INTF_B);
                CTC_BIT_SET(p_hss_vec->clktree_bitmap, SYS_DATAPATH_CLKTREE_CORE_B0);
            }
            else
            {
                p_hss_vec->plla_mode = hssclk;
                p_hss_vec->intf_div_a = intf_div;
                p_hss_vec->core_div_a[0] = core_div;
                p_hss_vec->plla_ref_cnt++;
                CTC_BIT_SET(p_hss_vec->clktree_bitmap, SYS_DATAPATH_CLKTREE_INTF_A);
                CTC_BIT_SET(p_hss_vec->clktree_bitmap, SYS_DATAPATH_CLKTREE_CORE_A0);
            }
        }
        else
        {
             if (SYS_DATAPATH_HSSCLK_644DOT53125 == hssclk)
            {
                p_hss_vec->pllb_mode = hssclk;
                p_hss_vec->intf_div_b = intf_div;
                p_hss_vec->core_div_b[0] = core_div;
                p_hss_vec->pllb_ref_cnt++;
                CTC_BIT_SET(p_hss_vec->clktree_bitmap, SYS_DATAPATH_CLKTREE_INTF_B);
                CTC_BIT_SET(p_hss_vec->clktree_bitmap, SYS_DATAPATH_CLKTREE_CORE_B0);
            }
            else
            {
                p_hss_vec->plla_mode = hssclk;
                p_hss_vec->intf_div_a = intf_div;
                p_hss_vec->core_div_a[0] = core_div;
                p_hss_vec->plla_ref_cnt++;
                CTC_BIT_SET(p_hss_vec->clktree_bitmap, SYS_DATAPATH_CLKTREE_INTF_A);
                CTC_BIT_SET(p_hss_vec->clktree_bitmap, SYS_DATAPATH_CLKTREE_CORE_A0);
            }
        }

        p_hss_vec->serdes_info[lane_id].mode = mode;
        p_hss_vec->serdes_info[lane_id].overclocking_speed = overclocking_speed;
        p_hss_vec->serdes_info[lane_id].pll_sel = (p_hss_vec->pllb_mode != SYS_DATAPATH_HSSCLK_DISABLE)
                    ? SYS_DATAPATH_PLL_SEL_PLLB: SYS_DATAPATH_PLL_SEL_PLLA;
        p_hss_vec->serdes_info[lane_id].rate_div = rate_div;
        p_hss_vec->serdes_info[lane_id].bit_width = bit_width;
        p_hss_vec->serdes_info[lane_id].is_dyn = is_dyn;
        need_add = 1;
    }
    else
    {
        if (SYS_DATAPATH_HSSCLK_DISABLE == hssclk)
        {
            /*Only used for dynamic switch, swith Used Mode to Disable
               In thie case, clockTree and hssCfg do not change*/
            if (p_hss_vec->serdes_info[lane_id].pll_sel == SYS_DATAPATH_PLL_SEL_PLLB)
            {
                p_hss_vec->pllb_ref_cnt--;
            }
            else if (p_hss_vec->serdes_info[lane_id].pll_sel == SYS_DATAPATH_PLL_SEL_PLLA)
            {
                p_hss_vec->plla_ref_cnt--;
            }

            p_hss_vec->serdes_info[lane_id].pll_sel = SYS_DATAPATH_PLL_SEL_NONE;
            p_hss_vec->serdes_info[lane_id].mode = CTC_CHIP_SERDES_NONE_MODE;
        }
        else
        {
            if (p_hss_vec->plla_mode == hssclk)
            {
                /*one pll can only provide one intf div*/
                if ((p_hss_vec->intf_div_a != intf_div) && (p_hss_vec->intf_div_a)&&(intf_div))
                {
                    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"One PLL need provide two intf divider, please check!! \n");
                    return CTC_E_FEATURE_NOT_SUPPORT;
                }

                if (p_hss_vec->core_div_a[0] != core_div)
                {
                    if ((p_hss_vec->core_div_a[1] != core_div) && (p_hss_vec->core_div_a[1]))
                    {
                        SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"One PLL can not provide more core divider, please check!! \n");
                        return CTC_E_FEATURE_NOT_SUPPORT;
                    }
                    p_hss_vec->core_div_a[1] = core_div;
                    CTC_BIT_SET(p_hss_vec->clktree_bitmap, SYS_DATAPATH_CLKTREE_CORE_A1);
                }

                /*exist pll can provide serdes frequency*/
                p_hss_vec->serdes_info[lane_id].mode = mode;
                p_hss_vec->serdes_info[lane_id].overclocking_speed = overclocking_speed;
                p_hss_vec->serdes_info[lane_id].rate_div = rate_div;
                p_hss_vec->serdes_info[lane_id].bit_width = bit_width;
                p_hss_vec->serdes_info[lane_id].is_dyn = is_dyn;

                if (p_hss_vec->serdes_info[lane_id].pll_sel == SYS_DATAPATH_PLL_SEL_NONE)
                {
                    p_hss_vec->plla_ref_cnt++;
                }
                else if (p_hss_vec->serdes_info[lane_id].pll_sel == SYS_DATAPATH_PLL_SEL_PLLA)
                {
                    /*NO need change*/
                }
                else if (p_hss_vec->serdes_info[lane_id].pll_sel == SYS_DATAPATH_PLL_SEL_PLLB)
                {
                    p_hss_vec->plla_ref_cnt++;
                    p_hss_vec->pllb_ref_cnt--;
                }

                p_hss_vec->serdes_info[lane_id].pll_sel = SYS_DATAPATH_PLL_SEL_PLLA;

            }
            else if (p_hss_vec->pllb_mode == hssclk)
            {
                /*one pll can only provide one intf div*/
                if ((p_hss_vec->intf_div_b != intf_div) && (p_hss_vec->intf_div_b)&&(intf_div))
                {
                    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"One PLL need provide two intf divider, please check!! \n");
                    return CTC_E_FEATURE_NOT_SUPPORT;
                }

                if (p_hss_vec->core_div_b[0] != core_div)
                {
                    if ((p_hss_vec->core_div_b[1] != core_div) && (p_hss_vec->core_div_b[1]))
                    {
                        SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"One PLL can not provide more core divider, please check!! \n");
                        return CTC_E_FEATURE_NOT_SUPPORT;
                    }
                    p_hss_vec->core_div_b[1] = core_div;
                    CTC_BIT_SET(p_hss_vec->clktree_bitmap, SYS_DATAPATH_CLKTREE_CORE_B1);
                }

                /*exist pll can provide serdes frequency*/
                p_hss_vec->serdes_info[lane_id].mode = mode;
                p_hss_vec->serdes_info[lane_id].overclocking_speed = overclocking_speed;
                p_hss_vec->serdes_info[lane_id].rate_div = rate_div;
                p_hss_vec->serdes_info[lane_id].bit_width = bit_width;
                p_hss_vec->serdes_info[lane_id].is_dyn = is_dyn;
                if (p_hss_vec->serdes_info[lane_id].pll_sel == SYS_DATAPATH_PLL_SEL_NONE)
                {
                    p_hss_vec->pllb_ref_cnt++;
                }
                else if (p_hss_vec->serdes_info[lane_id].pll_sel == SYS_DATAPATH_PLL_SEL_PLLB)
                {
                    /*NO need change*/
                }
                else if (p_hss_vec->serdes_info[lane_id].pll_sel == SYS_DATAPATH_PLL_SEL_PLLA)
                {
                    p_hss_vec->plla_ref_cnt--;
                    p_hss_vec->pllb_ref_cnt++;
                }
                p_hss_vec->serdes_info[lane_id].pll_sel = SYS_DATAPATH_PLL_SEL_PLLB;

            }
            else if ((p_hss_vec->plla_mode == SYS_DATAPATH_HSSCLK_DISABLE)&&(((mode != CTC_CHIP_SERDES_XAUI_MODE) && (mode != CTC_CHIP_SERDES_DXAUI_MODE) &&
                 (mode != CTC_CHIP_SERDES_CG_MODE) && (mode != CTC_CHIP_SERDES_2DOT5G_MODE) && SYS_DATAPATH_SERDES_IS_HSS28G(serdes_id))
                 || (!SYS_DATAPATH_SERDES_IS_HSS28G(serdes_id) && (overclocking_speed != CTC_CHIP_SERDES_OCS_MODE_12_58G)
                 && (overclocking_speed != CTC_CHIP_SERDES_OCS_MODE_12_12G))))
            {
                SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "p_hss_vec->plla_mode == SYS_DATAPATH_HSSCLK_DISABLE\n");
                /*need using plla*/
                p_hss_vec->plla_mode = hssclk;
                p_hss_vec->intf_div_a = intf_div;
                p_hss_vec->core_div_a[0] = core_div;
                p_hss_vec->serdes_info[lane_id].mode = mode;
                p_hss_vec->serdes_info[lane_id].overclocking_speed = overclocking_speed;
                p_hss_vec->serdes_info[lane_id].rate_div = rate_div;
                p_hss_vec->serdes_info[lane_id].bit_width = bit_width;
                p_hss_vec->serdes_info[lane_id].is_dyn = is_dyn;

                if (p_hss_vec->serdes_info[lane_id].pll_sel == SYS_DATAPATH_PLL_SEL_NONE)
                {
                    p_hss_vec->plla_ref_cnt++;
                }
                else if (p_hss_vec->serdes_info[lane_id].pll_sel == SYS_DATAPATH_PLL_SEL_PLLB)
                {
                    p_hss_vec->plla_ref_cnt++;
                    p_hss_vec->pllb_ref_cnt--;
                }

                p_hss_vec->serdes_info[lane_id].pll_sel = SYS_DATAPATH_PLL_SEL_PLLA;
                CTC_BIT_SET(p_hss_vec->clktree_bitmap, SYS_DATAPATH_CLKTREE_INTF_A);
                CTC_BIT_SET(p_hss_vec->clktree_bitmap, SYS_DATAPATH_CLKTREE_CORE_A0);
            }
            else if (p_hss_vec->pllb_mode == SYS_DATAPATH_HSSCLK_DISABLE)
            {
                /*need using pllb*/
                p_hss_vec->pllb_mode = hssclk;
                p_hss_vec->intf_div_b = intf_div;
                p_hss_vec->core_div_b[0] = core_div;
                p_hss_vec->serdes_info[lane_id].mode = mode;
                p_hss_vec->serdes_info[lane_id].overclocking_speed = overclocking_speed;
                p_hss_vec->serdes_info[lane_id].rate_div = rate_div;
                p_hss_vec->serdes_info[lane_id].bit_width = bit_width;
                p_hss_vec->serdes_info[lane_id].is_dyn = is_dyn;
                if (p_hss_vec->serdes_info[lane_id].pll_sel == SYS_DATAPATH_PLL_SEL_NONE)
                {
                    p_hss_vec->pllb_ref_cnt++;
                }
                else if (p_hss_vec->serdes_info[lane_id].pll_sel == SYS_DATAPATH_PLL_SEL_PLLA)
                {
                    p_hss_vec->plla_ref_cnt--;
                    p_hss_vec->pllb_ref_cnt++;
                }
                p_hss_vec->serdes_info[lane_id].pll_sel = SYS_DATAPATH_PLL_SEL_PLLB;
                CTC_BIT_SET(p_hss_vec->clktree_bitmap, SYS_DATAPATH_CLKTREE_INTF_B);
                CTC_BIT_SET(p_hss_vec->clktree_bitmap, SYS_DATAPATH_CLKTREE_CORE_B0);
            }
            else
            {
                return CTC_E_FEATURE_NOT_SUPPORT;
            }
        }
    }

    if (p_hss_vec->plla_ref_cnt == 0)
    {
        SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_INFO,"Need Disable Plla!! \n");
        /*Disable plla hssclk*/
        p_hss_vec->plla_mode = SYS_DATAPATH_HSSCLK_DISABLE;
        p_hss_vec->core_div_a[0] = SYS_DATAPATH_CLKTREE_DIV_USELESS;
        p_hss_vec->core_div_a[1] = SYS_DATAPATH_CLKTREE_DIV_USELESS;
        p_hss_vec->intf_div_a = SYS_DATAPATH_CLKTREE_DIV_USELESS;
    }

    if (p_hss_vec->pllb_ref_cnt == 0)
    {
        SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_INFO,"Need Disable Pllb!! \n");
        /*Disable pllb hssclk*/
        p_hss_vec->pllb_mode = SYS_DATAPATH_HSSCLK_DISABLE;
        p_hss_vec->core_div_b[0] = SYS_DATAPATH_CLKTREE_DIV_USELESS;
        p_hss_vec->core_div_b[1] = SYS_DATAPATH_CLKTREE_DIV_USELESS;
        p_hss_vec->intf_div_b = SYS_DATAPATH_CLKTREE_DIV_USELESS;
    }

    slice_id = (serdes_id >= 48)?1:0;
    _sys_goldengate_datapath_get_serdes_mapping(lchip, serdes_id, mode, &lport,  &mac_id, &chan_id, &speed_mode);

    if (p_gg_datapath_master[lchip]->datapath_update_func)
    {
        lport = lport_serdes;
    }

    p_hss_vec->serdes_info[lane_id].lport = lport;
    p_port_attr = (sys_datapath_lport_attr_t*)(&p_gg_datapath_master[lchip]->port_attr[slice_id][lport]);
    p_port_attr->mac_id = mac_id;
    p_port_attr->port_type = (mode)? SYS_DATAPATH_NETWORK_PORT : SYS_DATAPATH_RSV_PORT;
    p_port_attr->chan_id = (mode)? chan_id : 0xff;
    p_port_attr->speed_mode = speed_mode;
    p_port_attr->slice_id = slice_id;
    p_port_attr->pcs_mode = mode;
    p_port_attr->serdes_id = serdes_id;

    if (SYS_DATAPATH_NEED_EXTERN(mode) && is_dyn && ((mac_id%4)==0))
    {
        p_port_attr->need_ext = 1;
    }
    else
    {
        p_port_attr->need_ext = 0;
    }

    if (need_add)
    {
        ctc_vector_add(p_gg_datapath_master[lchip]->p_hss_vector, hss_idx, (void*)p_hss_vec);
    }

    return CTC_E_NONE;
}

/*
   set serdes link enable or disable,dir:0-rx, 1-tx, 2-both
*/
int32
sys_goldengate_datapath_set_link_enable(uint8 lchip, uint8 serdes_id, uint8 enable, uint8 dir)
{
    uint16 value = 0;
    uint8 internal_lane = 0;
    uint16 address = 0;
    uint8 hss_id = 0;
    uint8 lane_id = 0;

    hss_id = SYS_DATAPATH_MAP_SERDES_TO_HSSID(serdes_id);
    lane_id = SYS_DATAPATH_MAP_SERDES_TO_LANE_ID(serdes_id);

    if (SYS_DATAPATH_SERDES_IS_HSS28G(serdes_id))
    {
        internal_lane = lane_id;
        if (enable)
        {
            if ((SYS_DATAPATH_SERDES_DIR_TX == dir) || (SYS_DATAPATH_SERDES_DIR_BOTH == dir))
            {
                /*enable tx link enable register*/
                address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0);
                CTC_ERROR_RETURN(drv_goldengate_chip_read_hss28g(lchip, hss_id, address, &value));
                value |= (1 << SYS_DATAPATH_HSS_LINK_EN_BIT);
                CTC_ERROR_RETURN(drv_goldengate_chip_write_hss28g(lchip, hss_id, address, value));
            }

            if ((SYS_DATAPATH_SERDES_DIR_RX == dir) || (SYS_DATAPATH_SERDES_DIR_BOTH == dir))
            {
                /*enable rx link enable register*/
                address = DRV_HSS_ADDR_COMBINE(g_gg_hss_rx_addr_map[internal_lane], 0);
                CTC_ERROR_RETURN(drv_goldengate_chip_read_hss28g(lchip, hss_id, address, &value));
                value |= (1 << SYS_DATAPATH_HSS_LINK_EN_BIT);
                CTC_ERROR_RETURN(drv_goldengate_chip_write_hss28g(lchip, hss_id, address, value));
            }
        }
        else
        {
            if ((SYS_DATAPATH_SERDES_DIR_TX == dir) || (SYS_DATAPATH_SERDES_DIR_BOTH == dir))
            {
                /*disable tx link enable register*/
                address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0);
                CTC_ERROR_RETURN(drv_goldengate_chip_read_hss28g(lchip, hss_id, address, &value));
                value &= ~(1 << SYS_DATAPATH_HSS_LINK_EN_BIT);
                CTC_ERROR_RETURN(drv_goldengate_chip_write_hss28g(lchip, hss_id, address, value));
            }

            if ((SYS_DATAPATH_SERDES_DIR_RX == dir) || (SYS_DATAPATH_SERDES_DIR_BOTH == dir))
            {
                /*disable rx link enable register*/
                address = DRV_HSS_ADDR_COMBINE(g_gg_hss_rx_addr_map[internal_lane], 0);
                CTC_ERROR_RETURN(drv_goldengate_chip_read_hss28g(lchip, hss_id, address, &value));
                value &= ~(1 << SYS_DATAPATH_HSS_LINK_EN_BIT);
                CTC_ERROR_RETURN(drv_goldengate_chip_write_hss28g(lchip, hss_id, address, value));
            }
        }
    }
    else
    {
        internal_lane = g_gg_hss15g_lane_map[lane_id];

        if (enable)
        {
            if ((SYS_DATAPATH_SERDES_DIR_TX == dir) || (SYS_DATAPATH_SERDES_DIR_BOTH == dir))
            {
                /*enable tx link enable register*/
                address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0);
                CTC_ERROR_RETURN(drv_goldengate_chip_read_hss15g(lchip, hss_id, address, &value));
                value |= (1 << SYS_DATAPATH_HSS_LINK_EN_BIT);
                CTC_ERROR_RETURN(drv_goldengate_chip_write_hss15g(lchip, hss_id, address, value));
            }

            if ((SYS_DATAPATH_SERDES_DIR_RX == dir) || (SYS_DATAPATH_SERDES_DIR_BOTH == dir))
            {
                /*enable rx link enable register*/
                address = DRV_HSS_ADDR_COMBINE(g_gg_hss_rx_addr_map[internal_lane], 0);
                CTC_ERROR_RETURN(drv_goldengate_chip_read_hss15g(lchip, hss_id, address, &value));
                value |= (1 << SYS_DATAPATH_HSS_LINK_EN_BIT);
                CTC_ERROR_RETURN(drv_goldengate_chip_write_hss15g(lchip, hss_id, address, value));
            }
        }
        else
        {
            if ((SYS_DATAPATH_SERDES_DIR_TX == dir) || (SYS_DATAPATH_SERDES_DIR_BOTH == dir))
            {
                /*disable tx link enable register*/
                address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0);
                CTC_ERROR_RETURN(drv_goldengate_chip_read_hss15g(lchip, hss_id, address, &value));
                value &= ~(1 << SYS_DATAPATH_HSS_LINK_EN_BIT);
                CTC_ERROR_RETURN(drv_goldengate_chip_write_hss15g(lchip, hss_id, address, value));
            }

            if ((SYS_DATAPATH_SERDES_DIR_RX == dir) || (SYS_DATAPATH_SERDES_DIR_BOTH == dir))
            {
                /*disable rx link enable register*/
                address = DRV_HSS_ADDR_COMBINE(g_gg_hss_rx_addr_map[internal_lane], 0);
                CTC_ERROR_RETURN(drv_goldengate_chip_read_hss15g(lchip, hss_id, address, &value));
                value &= ~(1 << SYS_DATAPATH_HSS_LINK_EN_BIT);
                CTC_ERROR_RETURN(drv_goldengate_chip_write_hss15g(lchip, hss_id, address, value));
            }
        }
    }

    return CTC_E_NONE;

}

/*
   set serdes link dfe reset or release
*/
int32
sys_goldengate_datapath_set_dfe_reset(uint8 lchip, uint8 serdes_id, uint8 enable)
{
    uint16 value = 0;
    uint8 internal_lane = 0;
    uint16 address = 0;
    uint8 hss_id = 0;
    uint8 lane_id = 0;

    hss_id = SYS_DATAPATH_MAP_SERDES_TO_HSSID(serdes_id);
    lane_id = SYS_DATAPATH_MAP_SERDES_TO_LANE_ID(serdes_id);

    if (SYS_DATAPATH_SERDES_IS_HSS28G(serdes_id))
    {
        internal_lane = lane_id;
        if (enable)
        {
            /*reset rx dfe reset*/
            address = DRV_HSS_ADDR_COMBINE(g_gg_hss_rx_addr_map[internal_lane], 0x8);
            CTC_ERROR_RETURN(drv_goldengate_chip_read_hss28g(lchip, hss_id, address, &value));
            value |= (1 << 0);
            CTC_ERROR_RETURN(drv_goldengate_chip_write_hss28g(lchip, hss_id, address, value));
        }
        else
        {
            /*release rx dfe reset*/
            address = DRV_HSS_ADDR_COMBINE(g_gg_hss_rx_addr_map[internal_lane], 0x8);
            CTC_ERROR_RETURN(drv_goldengate_chip_read_hss28g(lchip, hss_id, address, &value));
            value &= ~(1 << 0);
            CTC_ERROR_RETURN(drv_goldengate_chip_write_hss28g(lchip, hss_id, address, value));
        }
    }
    else
    {
        internal_lane = g_gg_hss15g_lane_map[lane_id];

        if (enable)
        {
            /*reset rx dfe reset*/
            address = DRV_HSS_ADDR_COMBINE(g_gg_hss_rx_addr_map[internal_lane], 0x8);
            CTC_ERROR_RETURN(drv_goldengate_chip_read_hss15g(lchip, hss_id, address, &value));
            value |= (1 << 0);
            CTC_ERROR_RETURN(drv_goldengate_chip_write_hss15g(lchip, hss_id, address, value));
        }
        else
        {
            /*release rx dfe reset*/
            address = DRV_HSS_ADDR_COMBINE(g_gg_hss_rx_addr_map[internal_lane], 0x8);
            CTC_ERROR_RETURN(drv_goldengate_chip_read_hss15g(lchip, hss_id, address, &value));
            value &= ~(1 << 0);
            CTC_ERROR_RETURN(drv_goldengate_chip_write_hss15g(lchip, hss_id, address, value));
        }
    }

    return CTC_E_NONE;

}

/*
   set serdes link dfe enable or disable
*/
int32
sys_goldengate_datapath_set_dfe_en(uint8 lchip, uint8 serdes_id, uint8 enable)
{
    uint16 value = 0;
    uint8 internal_lane = 0;
    uint16 address = 0;
    uint8 hss_id = 0;
    uint8 lane_id = 0;

    hss_id = SYS_DATAPATH_MAP_SERDES_TO_HSSID(serdes_id);
    lane_id = SYS_DATAPATH_MAP_SERDES_TO_LANE_ID(serdes_id);

    if (SYS_DATAPATH_SERDES_IS_HSS28G(serdes_id))
    {
        internal_lane = lane_id;
        if (enable)
        {
            address = DRV_HSS_ADDR_COMBINE(g_gg_hss_rx_addr_map[internal_lane], 0x0);
            CTC_ERROR_RETURN(drv_goldengate_chip_read_hss28g(lchip, hss_id, address, &value));
            value |= (0x30);
            CTC_ERROR_RETURN(drv_goldengate_chip_write_hss28g(lchip, hss_id, address, value));
        }
        else
        {
            address = DRV_HSS_ADDR_COMBINE(g_gg_hss_rx_addr_map[internal_lane], 0x0);
            CTC_ERROR_RETURN(drv_goldengate_chip_read_hss28g(lchip, hss_id, address, &value));
            value &= ~(0x30);
            CTC_ERROR_RETURN(drv_goldengate_chip_write_hss28g(lchip, hss_id, address, value));
        }
    }
    else
    {
        internal_lane = g_gg_hss15g_lane_map[lane_id];

        if (enable)
        {
            address = DRV_HSS_ADDR_COMBINE(g_gg_hss_rx_addr_map[internal_lane], 0x0);
            CTC_ERROR_RETURN(drv_goldengate_chip_read_hss15g(lchip, hss_id, address, &value));
            value |= (0x30);
            CTC_ERROR_RETURN(drv_goldengate_chip_write_hss15g(lchip, hss_id, address, value));
        }
        else
        {
            address = DRV_HSS_ADDR_COMBINE(g_gg_hss_rx_addr_map[internal_lane], 0x0);
            CTC_ERROR_RETURN(drv_goldengate_chip_read_hss15g(lchip, hss_id, address, &value));
            value &= ~(0x30);
            CTC_ERROR_RETURN(drv_goldengate_chip_write_hss15g(lchip, hss_id, address, value));
        }
    }

    return CTC_E_NONE;
}

/*
   set serdes link reset or release,dir:0-rx, 1-tx, 2-both
*/
int32
sys_goldengate_datapath_set_link_reset(uint8 lchip, uint8 serdes_id, uint8 enable, uint8 dir)
{
    uint16 value = 0;
    uint8 internal_lane = 0;
    uint16 address = 0;
    uint8 hss_id = 0;
    uint8 lane_id = 0;

    hss_id = SYS_DATAPATH_MAP_SERDES_TO_HSSID(serdes_id);
    lane_id = SYS_DATAPATH_MAP_SERDES_TO_LANE_ID(serdes_id);

    if (SYS_DATAPATH_SERDES_IS_HSS28G(serdes_id))
    {
        internal_lane = lane_id;
        if (enable)
        {
            if ((SYS_DATAPATH_SERDES_DIR_TX == dir) || (SYS_DATAPATH_SERDES_DIR_BOTH == dir))
            {
                /*reset tx link reset*/
                address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0);
                CTC_ERROR_RETURN(drv_goldengate_chip_read_hss28g(lchip, hss_id, address, &value));
                value |= (1 << SYS_DATAPATH_HSS_LINK_RST);
                CTC_ERROR_RETURN(drv_goldengate_chip_write_hss28g(lchip, hss_id, address, value));
            }

            if ((SYS_DATAPATH_SERDES_DIR_RX == dir) || (SYS_DATAPATH_SERDES_DIR_BOTH == dir))
            {
                /*reset rx link reset*/
                address = DRV_HSS_ADDR_COMBINE(g_gg_hss_rx_addr_map[internal_lane], 0);
                CTC_ERROR_RETURN(drv_goldengate_chip_read_hss28g(lchip, hss_id, address, &value));
                value |= (1 << SYS_DATAPATH_HSS_LINK_RST);
                CTC_ERROR_RETURN(drv_goldengate_chip_write_hss28g(lchip, hss_id, address, value));
            }
        }
        else
        {
            if ((SYS_DATAPATH_SERDES_DIR_TX == dir) || (SYS_DATAPATH_SERDES_DIR_BOTH == dir))
            {
                /*release tx link reset*/
                address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0);
                CTC_ERROR_RETURN(drv_goldengate_chip_read_hss28g(lchip, hss_id, address, &value));
                value &= ~(1 << SYS_DATAPATH_HSS_LINK_RST);
                CTC_ERROR_RETURN(drv_goldengate_chip_write_hss28g(lchip, hss_id, address, value));
            }

            if ((SYS_DATAPATH_SERDES_DIR_RX == dir) || (SYS_DATAPATH_SERDES_DIR_BOTH == dir))
            {
                /*release rx link reset*/
                address = DRV_HSS_ADDR_COMBINE(g_gg_hss_rx_addr_map[internal_lane], 0);
                CTC_ERROR_RETURN(drv_goldengate_chip_read_hss28g(lchip, hss_id, address, &value));
                value &= ~(1 << SYS_DATAPATH_HSS_LINK_RST);
                CTC_ERROR_RETURN(drv_goldengate_chip_write_hss28g(lchip, hss_id, address, value));
            }

        }
    }
    else
    {
        internal_lane = g_gg_hss15g_lane_map[lane_id];

        if (enable)
        {
            if ((SYS_DATAPATH_SERDES_DIR_TX == dir) || (SYS_DATAPATH_SERDES_DIR_BOTH == dir))
            {
                /*reset tx link reset*/
                address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0);
                CTC_ERROR_RETURN(drv_goldengate_chip_read_hss15g(lchip, hss_id, address, &value));
                value |= (1 << SYS_DATAPATH_HSS_LINK_RST);
                CTC_ERROR_RETURN(drv_goldengate_chip_write_hss15g(lchip, hss_id, address, value));
            }

            if ((SYS_DATAPATH_SERDES_DIR_RX == dir) || (SYS_DATAPATH_SERDES_DIR_BOTH == dir))
            {
                /*reset rx link reset*/
                address = DRV_HSS_ADDR_COMBINE(g_gg_hss_rx_addr_map[internal_lane], 0);
                CTC_ERROR_RETURN(drv_goldengate_chip_read_hss15g(lchip, hss_id, address, &value));
                value |= (1 << SYS_DATAPATH_HSS_LINK_RST);
                CTC_ERROR_RETURN(drv_goldengate_chip_write_hss15g(lchip, hss_id, address, value));
            }
        }
        else
        {
            if ((SYS_DATAPATH_SERDES_DIR_TX == dir) || (SYS_DATAPATH_SERDES_DIR_BOTH == dir))
            {
                /*release tx link reset*/
                address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0);
                CTC_ERROR_RETURN(drv_goldengate_chip_read_hss15g(lchip, hss_id, address, &value));
                value &= ~(1 << SYS_DATAPATH_HSS_LINK_RST);
                CTC_ERROR_RETURN(drv_goldengate_chip_write_hss15g(lchip, hss_id, address, value));
            }

            if ((SYS_DATAPATH_SERDES_DIR_RX == dir) || (SYS_DATAPATH_SERDES_DIR_BOTH == dir))
            {
                /*release rx link reset*/
                address = DRV_HSS_ADDR_COMBINE(g_gg_hss_rx_addr_map[internal_lane], 0);
                CTC_ERROR_RETURN(drv_goldengate_chip_read_hss15g(lchip, hss_id, address, &value));
                value &= ~(1 << SYS_DATAPATH_HSS_LINK_RST);
                CTC_ERROR_RETURN(drv_goldengate_chip_write_hss15g(lchip, hss_id, address, value));
            }
        }
    }

    return CTC_E_NONE;

}

/*
    Set serdes mode interface, used for init and dynamic switch
    Notice:Before using the interface must using sys_goldengate_datapath_build_serdes_info to build serdes info
*/
int32
sys_goldengate_datapath_set_hss_internal(uint8 lchip, uint8 serdes_id, uint8 mode)
{
    uint8 hss_idx = 0;
    uint8 hss_id = 0;
    uint8 lane_id = 0;
    sys_datapath_hss_attribute_t* p_hss = NULL;
    sys_datapath_serdes_info_t* p_serdes = NULL;
    uint16 value = 0;
    uint8 internal_lane = 0;
    uint16 address = 0;
    uint16 mask_value = 0;
    uint32 tb_id = 0;
    uint32 cmd = 0;
    HsCfg0_m hs_cfg;
    uint32 data = 0;

#ifdef  EMULATION_PLATFORM
    return 0;
#endif

    sal_memset(&hs_cfg, 0, sizeof(HsCfg0_m));

    hss_idx = SYS_DATAPATH_MAP_SERDES_TO_HSS_INDEX(serdes_id);
    lane_id = SYS_DATAPATH_MAP_SERDES_TO_LANE_ID(serdes_id);

    if (SYS_DATAPATH_SERDES_IS_HSS28G(serdes_id))
    {
        internal_lane = lane_id;
    }
    else
    {
        internal_lane = g_gg_hss15g_lane_map[lane_id];
    }

    p_hss = ctc_vector_get(p_gg_datapath_master[lchip]->p_hss_vector, hss_idx);
    if (NULL == p_hss)
    {
        return CTC_E_NONE;
    }

    hss_id = p_hss->hss_id;
    p_serdes = &p_hss->serdes_info[lane_id];

    if (p_serdes->mode != mode)
    {
        /*serdes info is not ready*/
        return CTC_E_UNEXPECT;
    }

    if (p_serdes->mode == CTC_CHIP_SERDES_NONE_MODE)
    {
        sys_goldengate_datapath_set_link_reset(lchip, serdes_id, TRUE, SYS_DATAPATH_SERDES_DIR_BOTH);
        sys_goldengate_datapath_set_link_enable(lchip, serdes_id, FALSE, SYS_DATAPATH_SERDES_DIR_BOTH);
        return CTC_E_NONE;
    }

    /*Set link enable*/
    sys_goldengate_datapath_set_link_enable(lchip, serdes_id, TRUE, SYS_DATAPATH_SERDES_DIR_BOTH);
    sys_goldengate_datapath_set_link_reset(lchip, serdes_id, TRUE, SYS_DATAPATH_SERDES_DIR_BOTH);
    sal_task_sleep(1);
    sys_goldengate_datapath_set_link_reset(lchip, serdes_id, FALSE, SYS_DATAPATH_SERDES_DIR_BOTH);
    sal_task_sleep(1);

    /*Cfg serdes internal register*/
    if (p_hss->hss_type == SYS_DATAPATH_HSS_TYPE_15G)
    {
        /*cfg tx register*/
        address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0);
        CTC_ERROR_RETURN(drv_goldengate_chip_read_hss15g(lchip, hss_id, address, &value));
        value &= ~(uint16)SYS_DATAPATH_HSS_REG_MASK;
        mask_value = 0;
        CTC_ERROR_RETURN(_sys_goldengate_datapath_encode_serdes_cfg(lchip, p_hss->hss_type, 0, &mask_value, p_serdes));
        value |= mask_value;
        CTC_ERROR_RETURN(drv_goldengate_chip_write_hss15g(lchip, hss_id, address, value));

        /*enable tx*/
        tb_id = HsCfg0_t + p_hss->hss_id;
        data = 1;
        cmd = DRV_IOW(tb_id, HsCfg0_cfgHssTxAOe_f+lane_id*2);
        DRV_IOCTL(lchip, 0, cmd, &data);

        /*cfg rx register*/
        mask_value = 0;
        address = DRV_HSS_ADDR_COMBINE(g_gg_hss_rx_addr_map[internal_lane], 0);
        CTC_ERROR_RETURN(drv_goldengate_chip_read_hss15g(lchip, hss_id, address, &value));
        value &= ~(uint16)SYS_DATAPATH_HSS_REG_MASK;
        CTC_ERROR_RETURN(_sys_goldengate_datapath_encode_serdes_cfg(lchip, p_hss->hss_type, 1, &mask_value, p_serdes));
        value |= mask_value;
        CTC_ERROR_RETURN(drv_goldengate_chip_write_hss15g(lchip, hss_id, address, value));

        if (CTC_CHIP_SERDES_DXAUI_MODE == p_serdes->mode)
        {
            value = 0xeffb;
        }
        else
        {
            value = 0xffff;
        }
        address = DRV_HSS_ADDR_COMBINE(g_gg_hss_rx_addr_map[internal_lane], 0x1f);
        CTC_ERROR_RETURN(drv_goldengate_chip_write_hss15g(lchip, hss_id, address, value));
    }
    else
    {
        /*xaui, dxaui,cg must using pllb*/
        if ((p_serdes->pll_sel == SYS_DATAPATH_PLL_SEL_PLLA) && ((p_serdes->mode == CTC_CHIP_SERDES_XAUI_MODE) || (p_serdes->mode == CTC_CHIP_SERDES_DXAUI_MODE) ||
                 (p_serdes->mode == CTC_CHIP_SERDES_CG_MODE) || (p_serdes->mode == CTC_CHIP_SERDES_2DOT5G_MODE)))
        {
            SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"Hss28G Must using pllb for the mode!! \n");
            return CTC_E_DATAPATH_CLKTREE_CFG_ERROR;
        }

        /*cfg tx register*/
        mask_value = 0;
        address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0);
        CTC_ERROR_RETURN(drv_goldengate_chip_read_hss28g(lchip, hss_id, address, &value));
        value &= ~(uint16)SYS_DATAPATH_HSS_REG_MASK;
        CTC_ERROR_RETURN(_sys_goldengate_datapath_encode_serdes_cfg(lchip, p_hss->hss_type, 0, &mask_value, p_serdes));
        value |= mask_value;
        CTC_ERROR_RETURN(drv_goldengate_chip_write_hss28g(lchip, hss_id, address, value));

        /*enable tx*/
        tb_id = CsCfg0_t + p_hss->hss_id;
        data = 1;
        cmd = DRV_IOW(tb_id, CsCfg0_cfgHssTxAOe_f+lane_id*2);
        DRV_IOCTL(lchip, 0, cmd, &data);

        /*cfg rx register*/
        mask_value = 0;
        address = DRV_HSS_ADDR_COMBINE(g_gg_hss_rx_addr_map[internal_lane], 0);
        CTC_ERROR_RETURN(drv_goldengate_chip_read_hss28g(lchip, hss_id, address, &value));
        value &= ~(uint16)SYS_DATAPATH_HSS_REG_MASK;
        CTC_ERROR_RETURN(_sys_goldengate_datapath_encode_serdes_cfg(lchip, p_hss->hss_type, 1, &mask_value, p_serdes));
        value |= mask_value;
        CTC_ERROR_RETURN(drv_goldengate_chip_write_hss28g(lchip, hss_id, address, value));

        /*cfg rx register */
        if (p_serdes->mode == CTC_CHIP_SERDES_SGMII_MODE)
        {
            address = DRV_HSS_ADDR_COMBINE(g_gg_hss_rx_addr_map[internal_lane], 0x6);
            value = 0x14;
            CTC_ERROR_RETURN(drv_goldengate_chip_write_hss28g(lchip, hss_id, address, value));

            address = DRV_HSS_ADDR_COMBINE(g_gg_hss_rx_addr_map[internal_lane], 0x7);
            value = 0x14;
            CTC_ERROR_RETURN(drv_goldengate_chip_write_hss28g(lchip, hss_id, address, value));
        }

        /*config DQCC*/
        if (CTC_CHIP_SERDES_CG_MODE == p_serdes->mode)
        {
            address = DRV_HSS_ADDR_COMBINE(g_gg_hss_rx_addr_map[internal_lane], 0x20);
            CTC_ERROR_RETURN(drv_goldengate_chip_read_hss28g(lchip, hss_id, address, &value));
            value |= 1;
            CTC_ERROR_RETURN(drv_goldengate_chip_write_hss28g(lchip, hss_id, address, value));
        }
        else
        {
            address = DRV_HSS_ADDR_COMBINE(g_gg_hss_rx_addr_map[internal_lane], 0x20);
            CTC_ERROR_RETURN(drv_goldengate_chip_read_hss28g(lchip, hss_id, address, &value));
            value &= ~1;
            CTC_ERROR_RETURN(drv_goldengate_chip_write_hss28g(lchip, hss_id, address, value));
        }

        if (CTC_CHIP_SERDES_DXAUI_MODE == p_serdes->mode)
        {
            value = 0xeffb;
        }
        else
        {
            value = 0xffff;
        }
        address = DRV_HSS_ADDR_COMBINE(g_gg_hss_rx_addr_map[internal_lane], 0x1f);
        CTC_ERROR_RETURN(drv_goldengate_chip_write_hss28g(lchip, hss_id, address, value));
    }

    return CTC_E_NONE;
}

#define __DATAPATH__

STATIC int32
_sys_goldengate_datapath_cfg_global_weight(uint8 lchip)
{
    uint8 index = 0;
    uint8 hss_idx = 0;
    uint8 lane_idx = 0;
    sys_datapath_hss_attribute_t* p_hss = NULL;
    sys_datapath_serdes_info_t* p_serdes = NULL;
    uint8 xg_cnt[2] = {0};
    uint8 xlg_cnt[2] = {0};
    uint32 cmd = 0;
    uint8 slice_id = 0;
    BufRetrvPktWeightConfig0_m pkt_wt;
    QMgrIntfWrrWtCtl0_m qmgr_wt;
    netLowSubIntfWrrWtCtl0_m sub_wt;
    uint32 xg_wt = 0;
    uint32 xlg_wt = 0;

    for (index = 0; index < 96; index++)
    {
        hss_idx = SYS_DATAPATH_MAP_SERDES_TO_HSS_INDEX(index);
        lane_idx = SYS_DATAPATH_MAP_SERDES_TO_LANE_ID(index);

        p_hss = ctc_vector_get(p_gg_datapath_master[lchip]->p_hss_vector, hss_idx);
        if (NULL == p_hss)
        {
            continue;
        }

        p_serdes = &p_hss->serdes_info[lane_idx];
        if (p_serdes->mode == CTC_CHIP_SERDES_NONE_MODE)
        {
            continue;
        }

        if (index >= 48)
        {
            slice_id = 1;
        }

        if ((p_serdes->mode == CTC_CHIP_SERDES_XFI_MODE) || (p_serdes->mode == CTC_CHIP_SERDES_SGMII_MODE) ||
            (p_serdes->mode == CTC_CHIP_SERDES_XAUI_MODE) || (p_serdes->mode == CTC_CHIP_SERDES_DXAUI_MODE) ||
            (p_serdes->mode == CTC_CHIP_SERDES_2DOT5G_MODE))
        {
            xg_cnt[slice_id]++;
        }

        if (p_serdes->mode == CTC_CHIP_SERDES_XLG_MODE)
        {
            xlg_cnt[slice_id]++;
        }
    }

    for (index = 0; index < SYS_MAX_LOCAL_SLICE_NUM; index++)
    {
        xg_wt = 10*xg_cnt[index];
        xlg_wt = 40*xlg_cnt[index];
        cmd = DRV_IOR((BufRetrvPktWeightConfig0_t+index), DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &pkt_wt);
        SetBufRetrvPktWeightConfig0(V, networkPktXLGWeightConfig_f, &pkt_wt, xlg_wt);
        SetBufRetrvPktWeightConfig0(V, networkPktXGWeightConfig_f, &pkt_wt, xg_wt);
        SetBufRetrvPktWeightConfig0(V, networkPktWeightConfig_f , &pkt_wt, (xg_wt+xlg_wt));
        cmd = DRV_IOW((BufRetrvPktWeightConfig0_t+index), DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &pkt_wt);

        cmd = DRV_IOR((QMgrIntfWrrWtCtl0_t+index), DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &qmgr_wt);
        SetQMgrIntfWrrWtCtl0(V, netLowIntfWtCfg_f, &qmgr_wt, (xg_wt+xlg_wt)/2);
        cmd = DRV_IOW((QMgrIntfWrrWtCtl0_t+index), DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &qmgr_wt);

        cmd = DRV_IOR((netLowSubIntfWrrWtCtl0_t+index), DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &sub_wt);
        SetnetLowSubIntfWrrWtCtl0(V, xgSubIntfWtCfg_f, &sub_wt, xg_wt/2);
        SetnetLowSubIntfWrrWtCtl0(V, xlgSubIntfWtCfg_f, &sub_wt, xlg_wt/2);
        cmd = DRV_IOW((netLowSubIntfWrrWtCtl0_t+index), DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &sub_wt);
    }
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_datapath_get_netrx_buf_cfg(uint8 lchip, uint8 mode, sys_datapath_netrx_buf_t* p_buf)
{
    switch(mode)
    {
        case CTC_CHIP_SERDES_NONE_MODE:
        case CTC_CHIP_SERDES_SGMII_MODE:
        case CTC_CHIP_SERDES_2DOT5G_MODE:
            p_buf->buf_depth = 2;
            p_buf->buf_thrdhi = 6;
            p_buf->buf_thrdlow = 3;
            p_buf->buf_ptrnum = 8;
            break;
        case CTC_CHIP_SERDES_XFI_MODE:
            p_buf->buf_depth = 2;
            p_buf->buf_thrdhi = 6;
            p_buf->buf_thrdlow = 3;
            p_buf->buf_ptrnum = 8;
            break;
        case CTC_CHIP_SERDES_XAUI_MODE:
        case CTC_CHIP_SERDES_DXAUI_MODE:
        case CTC_CHIP_SERDES_XLG_MODE:
            p_buf->buf_depth = 3;
            p_buf->buf_thrdhi = 28;
            p_buf->buf_thrdlow = 6;
            p_buf->buf_ptrnum = 32;
            break;
        case CTC_CHIP_SERDES_CG_MODE:
            p_buf->buf_depth = 3;
            p_buf->buf_thrdhi = 57;
            p_buf->buf_thrdlow = 16;
            p_buf->buf_ptrnum = 80;
            break;

        default:
            return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_datapath_get_bufretrv_ws(uint8 lchip, uint8 speed_mode, uint8* p_weight)
{
    switch (speed_mode)
    {
        case CTC_PORT_SPEED_10M:
        case CTC_PORT_SPEED_100M:
        case CTC_PORT_SPEED_1G:
        case CTC_PORT_SPEED_2G5:
        case CTC_PORT_SPEED_10G:
            *p_weight = 2;
            break;
        case CTC_PORT_SPEED_20G:
        case CTC_PORT_SPEED_40G:
            *p_weight = 8;
            break;
        case CTC_PORT_SPEED_100G:
            *p_weight = 20;
            break;
        default:
            return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

/*
    type:0-network port, 1-internal port
    for network port, valid chan range:0~51, network port must using this range, even for unused CG
    for internal port, valid chan range:52~63
*/
STATIC int32
_sys_goldengate_datapath_get_useless_chan(uint8 lchip, uint8 type, uint8* p_chan)
{
    uint8 index = 0;
    uint16 lport = 0;
    sys_datapath_lport_attr_t* port_attr = NULL;
    uint8 is_used = 0;
    uint8 useless_chan = 0;
    uint8 find_flag = 0;
    uint8 max_chan = 0;
    uint8 min_chan = 0;

    if (!type)
    {
        max_chan = 51;
        min_chan = 0;
    }
    else
    {
        max_chan = 63;
        min_chan = 52;
    }

    for (index = max_chan; (index+1) > min_chan; index--)
    {
        is_used = 0;
        for (lport = 0; lport < SYS_CHIP_PER_SLICE_PORT_NUM; lport++)
        {
            port_attr = sys_goldengate_datapath_get_port_capability(lchip, lport, 0);
            if (!port_attr)
            {
                continue;
            }

            if (port_attr->port_type == SYS_DATAPATH_RSV_PORT)
            {
                continue;
            }

            if (port_attr->need_ext)
            {
                if ((index == port_attr->chan_id) || (index == port_attr->chan_id + 1) ||
                  (index == port_attr->chan_id + 2) || (index == port_attr->chan_id + 3))
                {
                    /*chan is used*/
                    is_used = 1;
                    break;
                }
            }
            else
            {
                if (index == port_attr->chan_id)
                {
                    /*chan is used*/
                    is_used = 1;
                    break;
                }
            }
        }

        if (!is_used)
        {
            useless_chan = index;
            find_flag = 1;
            break;
        }
    }

    if (find_flag)
    {
        *p_chan = useless_chan;
        return CTC_E_NONE;
    }
    else
    {
        return CTC_E_UNEXPECT;
    }

}

/*
  Using for datapath init and dynamic, for network port
*/
STATIC int32
_sys_goldengate_datapath_set_netrx_buf(uint8 lchip, uint8 mode, uint8 mac_id, uint8 slice_id)
{
    uint32 cmd = 0;
    NetRxBufCfg0_m rx_buf;
    sys_datapath_netrx_buf_t rx_buf_cfg;

    sal_memset(&rx_buf, 0, sizeof(NetRxBufCfg0_m));
    sal_memset(&rx_buf_cfg, 0, sizeof(sys_datapath_netrx_buf_t));

    CTC_ERROR_RETURN(_sys_goldengate_datapath_get_netrx_buf_cfg(lchip, mode, &rx_buf_cfg));
    SetNetRxBufCfg0(V, cfgBufPtrNum_f, &rx_buf, rx_buf_cfg.buf_ptrnum);
    SetNetRxBufCfg0(V, cfgBufThrdHi_f, &rx_buf, rx_buf_cfg.buf_thrdhi);
    SetNetRxBufCfg0(V, cfgBufThrdLo_f, &rx_buf, rx_buf_cfg.buf_thrdlow);
    SetNetRxBufCfg0(V, cfgRsvBufDepth_f , &rx_buf, rx_buf_cfg.buf_depth);
    cmd = DRV_IOW((NetRxBufCfg0_t+slice_id), DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, mac_id, cmd, &rx_buf);

    return CTC_E_NONE;
}

/*
  Using for datapath init and dynamic, for network port cfg Nettx dynamic and STATIC info
  if p_end == NULL,means dynamic
*/
STATIC int32
_sys_goldengate_datapath_set_nettx_buf(uint8 lchip, uint8 speed_mode, uint8 mac_id, uint8 slice_id,
                uint16 s_pointer, uint16* p_end)
{
    uint32 cmd = 0;
    NetTxPortStaticInfo0_m static_info;
    NetTxPortDynamicInfo0_m dyn_info;
    uint8 port_mode = 0;
    uint16 buf_size = 0;
    uint16 e_poniter = 0;

    sal_memset(&static_info, 0, sizeof(NetTxPortStaticInfo0_m));
    sal_memset(&dyn_info, 0, sizeof(NetTxPortDynamicInfo0_m));

    switch(speed_mode)
    {
        case CTC_PORT_SPEED_10M:
        case CTC_PORT_SPEED_100M:
        case CTC_PORT_SPEED_1G:
            port_mode = 0x03;
            break;
        case CTC_PORT_SPEED_2G5:
            port_mode = 0x04;
            break;
        case CTC_PORT_SPEED_10G:
            port_mode = 0x00;
            break;
        case CTC_PORT_SPEED_20G:
        case CTC_PORT_SPEED_40G:
            port_mode = 0x01;
            break;
        case CTC_PORT_SPEED_100G:
            port_mode = 0x02;
            break;

        default:
            return CTC_E_INVALID_PARAM;
    }

    if ((mac_id == 36) || (mac_id == 52))
    {
        buf_size = 48;
    }
    else
    {
        if (mac_id%4 == 0)
        {
            buf_size = 20;
        }
        else
        {
            buf_size = 10;
        }
    }

    cmd = DRV_IOR((NetTxPortStaticInfo0_t+slice_id), DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, mac_id, cmd, &static_info);
    SetNetTxPortStaticInfo0(V,portMode_f, &static_info, port_mode);
    cmd = DRV_IOW((NetTxPortStaticInfo0_t+slice_id), DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, mac_id, cmd, &static_info);

    if (p_end)
    {
        /*dynamic switch do not need do this*/
        cmd = DRV_IOR((NetTxPortStaticInfo0_t+slice_id), DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, mac_id, cmd, &static_info);
        e_poniter = s_pointer+buf_size-1;
        SetNetTxPortStaticInfo0(V,startPtr_f, &static_info, s_pointer);
        SetNetTxPortStaticInfo0(V,endPtr_f, &static_info, e_poniter);

        cmd = DRV_IOW((NetTxPortStaticInfo0_t+slice_id), DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, mac_id, cmd, &static_info);

        SetNetTxPortDynamicInfo0(V, readPtr_f, &dyn_info, s_pointer);
        SetNetTxPortDynamicInfo0(V, writePtr_f, &dyn_info, s_pointer);
        cmd = DRV_IOW((NetTxPortDynamicInfo0_t+slice_id), DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, mac_id, cmd, &dyn_info);

        *p_end = e_poniter+1;
    }
    return CTC_E_NONE;
}

/*
   Using for datapath init and dynamic, Notice this interface only used to set network port channel
*/
STATIC int32
_sys_goldengate_datapath_set_qmgr_resource(uint8 lchip, uint8 speed, uint8 chan_id, uint8 slice_id, uint8 is_dyn)
{
    RaChanCredit0_m ra_credit;
    QMgrNetworkWtCfgMem0_m wt_cfg;
    uint32 cmd = 0;
    uint8 chan_slice = 0;

    chan_slice = chan_id;

    switch(speed)
    {
        case CTC_PORT_SPEED_10M:
        case CTC_PORT_SPEED_100M:
        case CTC_PORT_SPEED_1G:
        case CTC_PORT_SPEED_2G5:
        case CTC_PORT_SPEED_10G:
            SetQMgrNetworkWtCfgMem0(V, weightCfg_f, &wt_cfg, 2);

            break;
        case CTC_PORT_SPEED_20G:
        case CTC_PORT_SPEED_40G:
            SetQMgrNetworkWtCfgMem0(V, weightCfg_f, &wt_cfg, 8);
            break;
        case CTC_PORT_SPEED_100G:
            SetQMgrNetworkWtCfgMem0(V, weightCfg_f, &wt_cfg, 20);

            break;

        default:
            return CTC_E_INVALID_PARAM;
    }

    cmd = DRV_IOW((QMgrNetworkWtCfgMem0_t+slice_id), DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, chan_slice, cmd, &wt_cfg);

    if (!is_dyn)
    {
        /*dynamic do not need set credit */
        if ((chan_id == 36) ||(chan_id == 44))
        {
            SetRaChanCredit0(V, credit_f, &ra_credit, QMGRRA_CHAN_CREDIT_STEP1);
        }
        else
        {
            SetRaChanCredit0(V, credit_f, &ra_credit, QMGRRA_CHAN_CREDIT_STEP0);
        }

        cmd = DRV_IOW((RaChanCredit0_t+slice_id), DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, chan_slice, cmd, &ra_credit);
    }

    return CTC_E_NONE;
}

/*
   Using for datapath init and dynamic, Notice this interface only used to set network port channel
*/
STATIC int32
_sys_goldengate_datapath_set_chan_en(uint8 lchip, uint8 slice_id, uint8 speed, uint8 chan_id)
{
    QMgrDeqChanIdCfg0_m qmgr_chan;
    BufRetrvChanIdCfg_m bufretrv_chan;
    uint32 cmd = 0;
    uint32 xg_chan[2] = {0};
    uint32 xlg_chan[2] = {0};
    uint8 useless_chan = 0;

    cmd = DRV_IOR((BufRetrvChanIdCfg_t), DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, slice_id, cmd, &bufretrv_chan);

    cmd = DRV_IOR((QMgrDeqChanIdCfg0_t+slice_id), DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &qmgr_chan);

    GetQMgrDeqChanIdCfg0(A,cfgNetXGChanEn_f, &qmgr_chan, xg_chan);
    GetQMgrDeqChanIdCfg0(A,cfgNetXLGChanEn_f, &qmgr_chan, xlg_chan);

    switch(speed)
    {
        case CTC_PORT_SPEED_10M:
        case CTC_PORT_SPEED_100M:
        case CTC_PORT_SPEED_1G:
        case CTC_PORT_SPEED_2G5:
        case CTC_PORT_SPEED_10G:
            /*set XG chan enable and disable XLG chan*/
            if (chan_id < 32)
            {
                xg_chan[0] |= (1 << chan_id);
                xlg_chan[0] &= ~(1 << chan_id);
            }
            else
            {
                xg_chan[1] |= (1 << (chan_id-32));
                xlg_chan[1] &= ~(1 << (chan_id-32));
            }

            break;
        case CTC_PORT_SPEED_20G:
        case CTC_PORT_SPEED_40G:

            /*set XLG chan enable and disable XG chan*/
            if (chan_id < 32)
            {
                xg_chan[0] &= ~(1 << chan_id);
                xlg_chan[0] |= (1 << chan_id);
            }
            else
            {
                xg_chan[1] &= ~(1 << (chan_id-32));
                xlg_chan[1] |= (1 << (chan_id-32));
            }
            break;
        case CTC_PORT_SPEED_100G:

            if (chan_id == CG0_CHAN)
            {
                SetQMgrDeqChanIdCfg0(V,cfgCg0ChanId_f, &qmgr_chan, CG0_CHAN);
                SetBufRetrvChanIdCfg(V,cfgCg0ChanId_f, &bufretrv_chan, CG0_CHAN);
            }
            else
            {
                SetQMgrDeqChanIdCfg0(V,cfgCg1ChanId_f, &qmgr_chan, CG1_CHAN);
                SetBufRetrvChanIdCfg(V,cfgCg1ChanId_f, &bufretrv_chan, CG1_CHAN);
            }

            break;

        default:
            return CTC_E_INVALID_PARAM;
    }

    if ((chan_id == CG0_CHAN) && (speed != CTC_PORT_SPEED_100G))
    {
        /* CG0 Chan using as normal chan*/
        if (GetQMgrDeqChanIdCfg0(V,cfgCg0ChanId_f, &qmgr_chan) == CG0_CHAN)
        {
            CTC_ERROR_RETURN(_sys_goldengate_datapath_get_useless_chan(lchip, 0, &useless_chan));
            SetQMgrDeqChanIdCfg0(V,cfgCg0ChanId_f, &qmgr_chan, useless_chan);
            SetBufRetrvChanIdCfg(V,cfgCg0ChanId_f, &bufretrv_chan, useless_chan);
        }
    }

    if ((chan_id == CG1_CHAN) && (speed != CTC_PORT_SPEED_100G))
    {
        /* CG0 Chan using as normal chan*/
        if (GetQMgrDeqChanIdCfg0(V,cfgCg1ChanId_f, &qmgr_chan) == CG1_CHAN)
        {
            CTC_ERROR_RETURN(_sys_goldengate_datapath_get_useless_chan(lchip, 0, &useless_chan));
            SetQMgrDeqChanIdCfg0(V,cfgCg1ChanId_f, &qmgr_chan, useless_chan);
            SetBufRetrvChanIdCfg(V,cfgCg1ChanId_f, &bufretrv_chan, useless_chan);
        }
    }

    SetQMgrDeqChanIdCfg0(A,cfgNetXGChanEn_f, &qmgr_chan, xg_chan);
    SetQMgrDeqChanIdCfg0(A,cfgNetXLGChanEn_f, &qmgr_chan, xlg_chan);

    /*BufretrvChanIdCfg should be the same with QMgrDeqChanIdCfg*/
    SetBufRetrvChanIdCfg(A,cfgNetXGChanEn_f, &bufretrv_chan, xg_chan);
    SetBufRetrvChanIdCfg(A,cfgNetXLGChanEn_f, &bufretrv_chan, xlg_chan);

    cmd = DRV_IOW((QMgrDeqChanIdCfg0_t+slice_id), DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &qmgr_chan);

    cmd = DRV_IOW((BufRetrvChanIdCfg_t), DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, slice_id, cmd, &bufretrv_chan);

    return CTC_E_NONE;
}

/*
   Using for datapath init and dynamic, Config BufRetrvPktConfigMem and BufRetrvPktStatusMem
   Usefull for network channel and internal channel
*/
STATIC int32
_sys_goldengate_datapath_set_bufretrv_pkt_mem(uint8 lchip, uint8 slice_id,
        uint16 s_pointer, uint8 chan_id, uint8 speed_mode, uint16* p_end)
{
    uint8 weight = 0;
    uint8 step = 0;
    BufRetrvPktConfigMem0_m bufretrv_cfg;
    BufRetrvPktStatusMem0_m bufretrv_status;
    uint32 cmd = 0;

    if (s_pointer >= BUFRETRV_MEM_MAX_DEPTH)
    {
        SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"chan;%d, s-pointer:%d \n", chan_id, s_pointer);
        return CTC_E_EXCEED_MAX_SIZE;
    }

    if (p_gg_datapath_master[lchip]->oam_chan == chan_id)
    {
        weight = 2;
        step = QMGRRA_CHAN_CREDIT_STEP0;
    }
    else if (p_gg_datapath_master[lchip]->iloop_chan == chan_id)
    {
        weight = 20;
        step = QMGRRA_CHAN_CREDIT_STEP2;
    }
    else if (p_gg_datapath_master[lchip]->eloop_chan == chan_id)
    {
        weight = 20;
        step = QMGRRA_CHAN_CREDIT_STEP2;
    }
    else if ((p_gg_datapath_master[lchip]->dma_chan <= chan_id) && (p_gg_datapath_master[lchip]->dma_chan+3 >= chan_id))
    {
        weight = 8;
        step = QMGRRA_CHAN_CREDIT_STEP1;
    }
    else
    {
        CTC_ERROR_RETURN(_sys_goldengate_datapath_get_bufretrv_ws(lchip, speed_mode, &weight));
        step = ((chan_id == 36) || (chan_id == 44))?QMGRRA_CHAN_CREDIT_STEP1:QMGRRA_CHAN_CREDIT_STEP0;
    }

    sal_memset(&bufretrv_cfg, 0 ,sizeof(BufRetrvPktConfigMem0_m));
    sal_memset(&bufretrv_status, 0 ,sizeof(BufRetrvPktStatusMem0_m));

    /* notice: when NULL == p_end, cfg is used for dynamic switch */
    if(NULL == p_end)
    {
        cmd = DRV_IOR((BufRetrvPktConfigMem0_t+slice_id), DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &bufretrv_cfg));
        SetBufRetrvPktConfigMem0(V,weight_f, &bufretrv_cfg, weight);
        cmd = DRV_IOW((BufRetrvPktConfigMem0_t+slice_id), DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &bufretrv_cfg));
    }
    else /* cfg below is used for init */
    {
        SetBufRetrvPktConfigMem0(V,weight_f, &bufretrv_cfg, weight);
        SetBufRetrvPktConfigMem0(V,startPtr_f, &bufretrv_cfg, s_pointer);
        SetBufRetrvPktConfigMem0(V,endPtr_f , &bufretrv_cfg, (s_pointer+step-1));
        cmd = DRV_IOW((BufRetrvPktConfigMem0_t+slice_id), DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, chan_id, cmd, &bufretrv_cfg);

        SetBufRetrvPktStatusMem0(V,aFull_f, &bufretrv_status, 0);
        SetBufRetrvPktStatusMem0(V,weight_f, &bufretrv_status, weight);
        SetBufRetrvPktStatusMem0(V,headPtr_f, &bufretrv_status, s_pointer);
        SetBufRetrvPktStatusMem0(V,tailPtr_f, &bufretrv_status, s_pointer);
        cmd = DRV_IOW((BufRetrvPktStatusMem0_t+slice_id), DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, chan_id, cmd, &bufretrv_status);

        *p_end = (s_pointer+step-1);

        if (*p_end >= BUFRETRV_MEM_MAX_DEPTH)
        {
            SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"chan;%d, e-pointer:%d \n", chan_id, *p_end);
            return CTC_E_EXCEED_MAX_SIZE;
        }
    }

    return CTC_E_NONE;
}

/*
   Using for datapath init and dynamic, Config BufRetrvBufConfigMem and BufRetrvBufStatusMem
   Usefull for network channel and internal channel
*/
STATIC int32
_sys_goldengate_datapath_set_bufretrv_buf_mem(uint8 lchip, uint8 slice_id,
        uint16 s_pointer, uint8 chan_id, uint8 speed_mode, uint16* p_end)
{
    uint8 weight = 0;
    uint8 step = 0;
    BufRetrvBufConfigMem0_m bufretrv_cfg;
    BufRetrvBufStatusMem0_m bufretrv_status;
    uint32 cmd = 0;

    if (s_pointer >= BUFRETRV_MEM_MAX_DEPTH)
    {
        SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"chan;%d, s-pointer:%d \n", chan_id, s_pointer);
        return CTC_E_EXCEED_MAX_SIZE;
    }

    if (p_gg_datapath_master[lchip]->oam_chan == chan_id)
    {
        weight = 2;
        step = QMGRRA_CHAN_CREDIT_STEP0;
    }
    else if (p_gg_datapath_master[lchip]->iloop_chan == chan_id)
    {
        weight = 20;
        step = QMGRRA_CHAN_CREDIT_STEP2;
    }
    else if (p_gg_datapath_master[lchip]->eloop_chan == chan_id)
    {
        weight = 20;
        step = QMGRRA_CHAN_CREDIT_STEP2;
    }
    else if ((p_gg_datapath_master[lchip]->dma_chan <= chan_id) && (p_gg_datapath_master[lchip]->dma_chan+3 >= chan_id))
    {
        weight = 8;
        step = QMGRRA_CHAN_CREDIT_STEP1;
    }
    else
    {
        CTC_ERROR_RETURN(_sys_goldengate_datapath_get_bufretrv_ws(lchip, speed_mode, &weight));
        step = ((chan_id == 36) || (chan_id == 44))?QMGRRA_CHAN_CREDIT_STEP1:QMGRRA_CHAN_CREDIT_STEP0;
    }

    sal_memset(&bufretrv_cfg, 0 ,sizeof(BufRetrvBufConfigMem0_m));
    sal_memset(&bufretrv_status, 0 ,sizeof(BufRetrvBufStatusMem0_m));

    /* notice: when NULL == p_end, cfg is used for dynamic switch */
    if(NULL == p_end)
    {
        cmd = DRV_IOR((BufRetrvBufConfigMem0_t+slice_id), DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &bufretrv_cfg));
        SetBufRetrvPktConfigMem0(V,weight_f, &bufretrv_cfg, weight);
        cmd = DRV_IOW((BufRetrvBufConfigMem0_t+slice_id), DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &bufretrv_cfg));
    }
    else /* cfg below is used for init */
    {
        SetBufRetrvBufConfigMem0(V,weight_f, &bufretrv_cfg, weight);
        SetBufRetrvBufConfigMem0(V,startPtr_f, &bufretrv_cfg, s_pointer);
        SetBufRetrvBufConfigMem0(V,endPtr_f , &bufretrv_cfg, (s_pointer+step-1));
        cmd = DRV_IOW((BufRetrvBufConfigMem0_t+slice_id), DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, chan_id, cmd, &bufretrv_cfg);

        SetBufRetrvBufStatusMem0(V,aFull_f, &bufretrv_status, 0);
        SetBufRetrvBufStatusMem0(V,headPtr_f, &bufretrv_status, s_pointer);
        SetBufRetrvBufStatusMem0(V,tailPtr_f, &bufretrv_status, s_pointer);
        cmd = DRV_IOW((BufRetrvBufStatusMem0_t+slice_id), DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, chan_id, cmd, &bufretrv_status);

        *p_end = (s_pointer+step-1);
        if (*p_end >= BUFRETRV_MEM_MAX_DEPTH)
        {
            SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"chan;%d, end pointer:%d \n", chan_id, *p_end);
            return CTC_E_EXCEED_MAX_SIZE;
        }
    }

    return CTC_E_NONE;
}

/*
   Using for datapath init and dynamic, Config BufRetrvBufCreditConfigMem and BufRetrvBufCreditConfig using default
   Usefull for network channel and internal channel
*/
STATIC int32
_sys_goldengate_datapath_set_bufretrv_buf_credit(uint8 lchip, uint8 slice_id, uint8 chan_id, uint8 speed_mode)
{
    uint32 cmd = 0;
    uint8 credit_idx = 0;
    BufRetrvBufCreditConfigMem0_m credit_cfg;

    if (p_gg_datapath_master[lchip]->oam_chan == chan_id)
    {
        credit_idx = 0;
    }
    else if (p_gg_datapath_master[lchip]->iloop_chan == chan_id)
    {
        credit_idx = 3;
    }
    else if (p_gg_datapath_master[lchip]->eloop_chan == chan_id)
    {
        credit_idx = 3;

    }
    else if ((p_gg_datapath_master[lchip]->dma_chan <= chan_id) && ((uint16)(p_gg_datapath_master[lchip]->dma_chan+3) >= chan_id))
    {
        credit_idx = 1;

    }
    else
    {
        credit_idx = ((chan_id==36)||(chan_id==44))?1:0;
    }

    sal_memset(&credit_cfg, 0, sizeof(BufRetrvBufCreditConfigMem0_m));

    SetBufRetrvBufCreditConfigMem0(V, config_f, &credit_cfg, credit_idx);
    cmd = DRV_IOW((BufRetrvBufCreditConfigMem0_t+slice_id), DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, chan_id, cmd, &credit_cfg);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_datapath_set_bufretrv_credit(uint8 lchip, uint8 slice_id, uint8 chan_id, uint8 speed_mode)
{
    uint32 cmd = 0;
    uint8 credit_idx = 0;
    BufRetrvBufCreditConfigMem0_m credit_cfg;

    if (p_gg_datapath_master[lchip]->oam_chan == chan_id)
    {
        return CTC_E_NONE;
    }
    else if (p_gg_datapath_master[lchip]->iloop_chan == chan_id)
    {
        return CTC_E_NONE;
    }
    else if (p_gg_datapath_master[lchip]->eloop_chan == chan_id)
    {
        credit_idx = 3;

    }
    else if ((p_gg_datapath_master[lchip]->dma_chan <= chan_id) && ((uint16)(p_gg_datapath_master[lchip]->dma_chan+3) >= chan_id))
    {
        return CTC_E_NONE;
    }
    else
    {
        if ((chan_id==36)||(chan_id==44))
        {
            /*credit depth 49 */
            credit_idx = 2;
        }
        else
        {
            if (chan_id%4 == 0)
            {
                /*credit depth 21 */
                credit_idx = 1;
            }
            else
            {
                /*credit depth 11 */
                credit_idx = 0;
            }
        }
    }

    sal_memset(&credit_cfg, 0, sizeof(BufRetrvBufCreditConfigMem0_m));

    SetBufRetrvCreditConfigMem0(V, config_f, &credit_cfg, credit_idx);
    cmd = DRV_IOW((BufRetrvCreditConfigMem0_t+slice_id), DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, chan_id, cmd, &credit_cfg);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_datapath_set_calendar_default(uint8 lchip, uint8 slice_id)
{
    uint8 cal_block = 0;
    uint8 entry_id = 0;
    uint8 port_id = 0;
    NetTxCal0_m cal_entry;
    uint32 cmd = 0;
    sys_datapath_lport_attr_t* p_port = NULL;
    uint16 lport = 0;
    uint8 is_pad = 0;
    uint16 act_lport = 0;
    uint8 need_ext = 0;

    for (cal_block = 0; cal_block < 4; cal_block++)
    {
        for (entry_id = 0; entry_id < 16; entry_id++)
        {
            is_pad = 0;

            if (cal_block < 2)
            {
                /*Txqm0 and Txqm1*/
                port_id = (entry_id%4)*4 + entry_id/4 + cal_block*16;
            }
            else if (cal_block == 2)
            {
                /*Cxqm0*/
                port_id = (entry_id >=8)?36:(((entry_id%2)?(36+(entry_id-1)/2):(32+entry_id/2)));
                if (entry_id >=8)
                {
                    is_pad = 1;
                }
            }
            else
            {
                /*Cxqm1*/
                port_id = (entry_id >=8)?52:(((entry_id%2)?(52+(entry_id-1)/2):(48+entry_id/2)));
                if (entry_id >=8)
                {
                    is_pad = 1;
                }
            }

            SetNetTxCal0(V, calEntry_f, &cal_entry, port_id);
            cmd = DRV_IOW((NetTxCal0_t+slice_id), DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, (cal_block*16+entry_id), cmd, &cal_entry);

            /*pad entry need not update sw*/
            if (is_pad)
            {
                continue;
            }

            /*store calendar entry index to port attribute */
            lport = sys_goldengate_datapath_get_lport_with_mac(lchip, slice_id, (port_id-(port_id%4)));
            if (lport == SYS_COMMON_USELESS_MAC)
            {
                need_ext = 0;
            }
            else
            {
                need_ext = 1;
            }

            if (need_ext)
            {
                p_port = &p_gg_datapath_master[lchip]->port_attr[slice_id][lport];
                if (p_port->need_ext)
                {
                    act_lport = lport + (port_id%4);
                }
                else
                {
                    act_lport = sys_goldengate_datapath_get_lport_with_mac(lchip, slice_id, port_id);
                }
            }
            else
            {
                act_lport = sys_goldengate_datapath_get_lport_with_mac(lchip, slice_id, port_id);
            }

            if (act_lport == SYS_COMMON_USELESS_MAC)
            {
                continue;
            }

            p_port = &p_gg_datapath_master[lchip]->port_attr[slice_id][act_lport];
            p_port->cal_index = cal_block*16+entry_id;
        }
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_datapath_set_calendar_cfg(uint8 lchip, uint8 slice_id, uint8 speed_mode, uint8 mac_id)
{
    uint32 cmd = 0;
    sys_datapath_lport_attr_t* p_port = NULL;
    uint16 lport = 0;
    uint32 index = 0;
    NetTxCal0_m cal_entry;
    uint8 entry_id = 0;

    lport = sys_goldengate_datapath_get_lport_with_mac(lchip, slice_id, mac_id);
    if (lport == SYS_COMMON_USELESS_MAC)
    {
        return CTC_E_MAC_NOT_USED;
    }

    p_port = sys_goldengate_datapath_get_port_capability(lchip, lport, slice_id);
    if (!p_port)
    {
        return CTC_E_MAC_NOT_USED;
    }

    index = p_port->cal_index;
    if (speed_mode <= CTC_PORT_SPEED_10G)
    {
        SetNetTxCal0(V, calEntry_f, &cal_entry, mac_id);
        cmd = DRV_IOW((NetTxCal0_t+slice_id), DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, index, cmd, &cal_entry);
    }
    else if (speed_mode == CTC_PORT_SPEED_40G || speed_mode == CTC_PORT_SPEED_20G)
    {
        for (entry_id = 0; entry_id < 4; entry_id++)
        {
            SetNetTxCal0(V, calEntry_f, &cal_entry, mac_id);
            cmd = DRV_IOW((NetTxCal0_t+slice_id), DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, (index+entry_id*4), cmd, &cal_entry);
        }
    }
    else if (speed_mode == CTC_PORT_SPEED_100G)
    {
        for (entry_id = 0; entry_id < 4; entry_id++)
        {
            SetNetTxCal0(V, calEntry_f, &cal_entry, mac_id);
            cmd = DRV_IOW((NetTxCal0_t+slice_id), DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, (index+entry_id*2), cmd, &cal_entry);
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_datapath_nettx_init(uint8 lchip)
{
    uint32 cmd = 0;
    uint8 slice_id = 0;
    NetTxMiscCtl0_m misc_ctl;
    NetTxPortToChan0_m port_to_chan;
    NetTxEpeChanToPort0_m chan_to_port;
    NetTxCalCtl0_m cal_ctl;
    uint16 lport = 0;
    sys_datapath_lport_attr_t* port_attr = NULL;
    uint16 sp = 0;
    uint16 ep = 0;
    uint8 init_num = 1;
    uint8 index = 0;
    uint8 speed_mode = 0;
    uint8 fst_in_block[4] = {0};

    for (slice_id = 0; slice_id < SYS_MAX_LOCAL_SLICE_NUM; slice_id++)
    {
        cmd = DRV_IOR((NetTxMiscCtl0_t+slice_id), DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &misc_ctl);
        SetNetTxMiscCtl0(V,pauseEn_f, &misc_ctl, 0);
        cmd = DRV_IOW((NetTxMiscCtl0_t+slice_id), DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &misc_ctl);
    }

    /*cfg port to chan mapping and chan to port mapping*/
    for (slice_id = 0; slice_id < SYS_MAX_LOCAL_SLICE_NUM; slice_id++)
    {
        for (lport = 0; lport < SYS_CHIP_PER_SLICE_PORT_NUM; lport++)
        {
            port_attr = sys_goldengate_datapath_get_port_capability(lchip, lport, slice_id);

            if (!port_attr)
            {
                continue;
            }

            if (port_attr->port_type != SYS_DATAPATH_NETWORK_PORT)
            {
                /*only network port goto nettx*/
                continue;
            }

            init_num = (port_attr->need_ext)?4:1;

            for (index = 0; index < init_num; index++)
            {
                SetNetTxPortToChan0(V, chanId_f, &port_to_chan, port_attr->chan_id+index);
                cmd = DRV_IOW((NetTxPortToChan0_t+slice_id), DRV_ENTRY_FLAG);
                DRV_IOCTL(lchip, port_attr->mac_id+index, cmd, &port_to_chan);

                SetNetTxEpeChanToPort0(V, portId_f, &chan_to_port, port_attr->mac_id+index);
                cmd = DRV_IOW((NetTxEpeChanToPort0_t+slice_id), DRV_ENTRY_FLAG);
                DRV_IOCTL(lchip, port_attr->chan_id+index, cmd, &chan_to_port);
            }
        }
    }

    init_num = 1;

    /*cfg NetTxStaticInfo and NetTxDynamicInfo*/
    for (slice_id = 0; slice_id < SYS_MAX_LOCAL_SLICE_NUM; slice_id++)
    {
        fst_in_block[1] = 0;
        fst_in_block[2] = 0;
        fst_in_block[3] = 0;
        sp = 0;

        for (lport = 0; lport < SYS_CHIP_PER_SLICE_PORT_NUM; lport++)
        {
            port_attr = sys_goldengate_datapath_get_port_capability(lchip, lport, slice_id);

            if (!port_attr)
            {
                continue;
            }

            if (port_attr->port_type != SYS_DATAPATH_NETWORK_PORT)
            {
                /*only network port goto nettx*/
                continue;
            }

            if ((port_attr->mac_id >= 16) && (port_attr->mac_id < 32))
            {
                if (fst_in_block[1] == 0)
                {
                    /*find first usefull mac in block 1*/
                    sp = 0;
                    fst_in_block[1] = 1;
                }
            }

            if ((port_attr->mac_id >= 32) && (port_attr->mac_id < 48))
            {
                if (fst_in_block[2] == 0)
                {
                    /*find first usefull mac in block 2*/
                    sp = 0;
                    fst_in_block[2] = 1;
                }
            }

            if (port_attr->mac_id >= 48)
            {
                if (fst_in_block[3] == 0)
                {
                    /*find first usefull mac in block 3*/
                    sp = 0;
                    fst_in_block[3] = 1;
                }
            }

            init_num = (port_attr->need_ext)?4:1;

            for (index = 0; index < init_num; index++)
            {
                speed_mode = index?CTC_PORT_SPEED_10G:port_attr->speed_mode;
                CTC_ERROR_RETURN(_sys_goldengate_datapath_set_nettx_buf(lchip, speed_mode, (port_attr->mac_id+index),
                    slice_id, sp, &ep));
                sp = ep;
            }
        }
    }

    /*cfg calendar */
    for (slice_id = 0; slice_id < SYS_MAX_LOCAL_SLICE_NUM; slice_id++)
    {
        /*set NetTxCal default cfg, all is 1g/xg mode */
        CTC_ERROR_RETURN(_sys_goldengate_datapath_set_calendar_default(lchip, slice_id));

        for (lport = 0; lport < SYS_CHIP_PER_SLICE_PORT_NUM; lport++)
        {
            port_attr = sys_goldengate_datapath_get_port_capability(lchip, lport, slice_id);
            if (!port_attr)
            {
                continue;
            }

            if (port_attr->port_type != SYS_DATAPATH_NETWORK_PORT)
            {
                /*only network port goto nettx*/
                continue;
            }

            /*cfg NetTxCal for calendar*/
            if (port_attr->speed_mode <= CTC_PORT_SPEED_10G)
            {
                /*Using default cfg*/
                continue;
            }

            CTC_ERROR_RETURN(sys_goldengate_datapath_set_calendar_cfg(lchip, slice_id,
                port_attr->speed_mode, port_attr->mac_id));
        }

        /*cfg NetTxCalCtl for calendar*/
        sal_memset(&cal_ctl, 0, sizeof(NetTxCalCtl0_m));
        SetNetTxCalCtl0(V, calEntry0Sel_f, &cal_ctl, 0);
        SetNetTxCalCtl0(V, calEntry1Sel_f, &cal_ctl, 0);
        SetNetTxCalCtl0(V, calEntry2Sel_f, &cal_ctl, 0);
        SetNetTxCalCtl0(V, calEntry3Sel_f, &cal_ctl, 0);

        SetNetTxCalCtl0(V, walkerEnd0_f, &cal_ctl, 15);
        SetNetTxCalCtl0(V, walkerEnd1_f, &cal_ctl, 15);
        SetNetTxCalCtl0(V, walkerEnd2_f, &cal_ctl, 15);
        SetNetTxCalCtl0(V, walkerEnd3_f, &cal_ctl, 15);
        cmd = DRV_IOW((NetTxCalCtl0_t+slice_id), DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &cal_ctl);
    }

    sal_task_sleep(1);

    /*set nettx ready*/
    for (slice_id = 0; slice_id < SYS_MAX_LOCAL_SLICE_NUM; slice_id++)
    {
        cmd = DRV_IOR((NetTxMiscCtl0_t+slice_id), DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &misc_ctl);
        SetNetTxMiscCtl0(V, netTxReady_f, &misc_ctl, 1);
        cmd = DRV_IOW((NetTxMiscCtl0_t+slice_id), DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &misc_ctl);
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_datapath_misc_init(uint8 lchip)
{
    uint32 cmd = 0;
    ReleaseReqLinkListFifoThrd_m link_thrd;

    cmd = DRV_IOR((ReleaseReqLinkListFifoThrd_t), DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &link_thrd);
    SetReleaseReqLinkListFifoThrd(V,releaseReqLinkListFifoThrd_f, &link_thrd, 0xb);
    cmd = DRV_IOW((ReleaseReqLinkListFifoThrd_t), DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &link_thrd);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_datapath_epe_init(uint8 lchip)
{
    uint32 cmd = 0;
    uint8 slice_id = 0;
    uint16 lport = 0;
    sys_datapath_lport_attr_t* port_attr = NULL;
    uint8 init_num = 1;
    uint8 index = 0;
    uint32 value = 0;

    for (slice_id = 0; slice_id < 2; slice_id++)
    {
        for (lport = 0; lport < SYS_CHIP_PER_SLICE_PORT_NUM; lport++)
        {
            port_attr = sys_goldengate_datapath_get_port_capability(lchip, lport, slice_id);
            if (!port_attr)
            {
                continue;
            }

            if (port_attr->port_type != SYS_DATAPATH_NETWORK_PORT)
            {
                continue;
            }

            init_num = (port_attr->need_ext)?4:1;

            for (index = 0; index < init_num; index++)
            {
                value = lport + index;
                cmd = DRV_IOW(EpeHeaderAdjustPhyPortMap_t, EpeHeaderAdjustPhyPortMap_localPhyPort_f);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, (port_attr->chan_id+64*slice_id+index), cmd, &value));
            }

        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_datapath_chan_distribute(uint8 lchip)
{
    uint16 lport = 0;
    uint8 slice_id = 0;
    uint32 cmd = 0;
    sys_datapath_lport_attr_t* port_attr = NULL;
    QMgrDeqChanIdCfg0_m chan_cfg;
    BufRetrvChanIdCfg_m bufretrv_chan;
    uint8 useless_chan = 0;
    uint32 xg_chan[2] = {0};
    uint32 xlg_chan[2] = {0};
    uint8 cg_chan0 = 0;
    uint8 cg_chan1 = 0;
    uint8 oam_chan = 0;
    uint8 dma_chan = 0;
    uint8 iloop_chan = 0;
    uint8 eloop_chan = 0;
    uint8 index = 0;
    uint8 init_num = 1;
    uint8 speed_mode = 0;

    sal_memset(&chan_cfg, 0, sizeof(QMgrDeqChanIdCfg0_m));
    sal_memset(&bufretrv_chan, 0, sizeof(BufRetrvChanIdCfg_m));

    /*For useless CG chan, must set useless chan id */
    CTC_ERROR_RETURN(_sys_goldengate_datapath_get_useless_chan(lchip, 0, &useless_chan));

    /*clear cfg */
    for (slice_id =0; slice_id < SYS_MAX_LOCAL_SLICE_NUM; slice_id++)
    {
        SetQMgrDeqChanIdCfg0(V,cfgCg0ChanId_f, &chan_cfg, useless_chan);
        SetBufRetrvChanIdCfg(V,cfgCg0ChanId_f, &bufretrv_chan, useless_chan);
        SetQMgrDeqChanIdCfg0(V,cfgCg1ChanId_f, &chan_cfg, useless_chan);
        SetBufRetrvChanIdCfg(V,cfgCg1ChanId_f, &bufretrv_chan, useless_chan);

        cmd = DRV_IOW((QMgrDeqChanIdCfg0_t+slice_id), DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &chan_cfg);
        cmd = DRV_IOW((BufRetrvChanIdCfg_t), DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, slice_id, cmd, &bufretrv_chan);
    }

    for (slice_id = 0; slice_id < SYS_MAX_LOCAL_SLICE_NUM; slice_id++)
    {
        for (lport = 0; lport < SYS_CHIP_PER_SLICE_PORT_NUM; lport++)
        {
            port_attr = sys_goldengate_datapath_get_port_capability(lchip, lport, slice_id);
            if (!port_attr)
            {
                continue;
            }

            if (port_attr->port_type == SYS_DATAPATH_RSV_PORT)
            {
                /*rsv port have no channel info*/
                continue;
            }

            /*internal channel distribute*/
            if (port_attr->port_type == SYS_DATAPATH_OAM_PORT)
            {
                cmd = DRV_IOR((QMgrDeqChanIdCfg0_t+slice_id), DRV_ENTRY_FLAG);
                DRV_IOCTL(lchip, 0, cmd, &chan_cfg);
                cmd = DRV_IOR(BufRetrvChanIdCfg_t, DRV_ENTRY_FLAG);
                DRV_IOCTL(lchip, slice_id, cmd, &bufretrv_chan);

                SetQMgrDeqChanIdCfg0(V,cfgOamChanId_f, &chan_cfg, port_attr->chan_id);
                SetBufRetrvChanIdCfg(V,cfgOamChanId_f, &bufretrv_chan, port_attr->chan_id);

                cmd = DRV_IOW((QMgrDeqChanIdCfg0_t+slice_id), DRV_ENTRY_FLAG);
                DRV_IOCTL(lchip, 0, cmd, &chan_cfg);
                cmd = DRV_IOW(BufRetrvChanIdCfg_t, DRV_ENTRY_FLAG);
                DRV_IOCTL(lchip, slice_id, cmd, &bufretrv_chan);
            }
            else if (port_attr->port_type == SYS_DATAPATH_ILOOP_PORT)
            {
                cmd = DRV_IOR((QMgrDeqChanIdCfg0_t+slice_id), DRV_ENTRY_FLAG);
                DRV_IOCTL(lchip, 0, cmd, &chan_cfg);
                cmd = DRV_IOR((BufRetrvChanIdCfg_t), DRV_ENTRY_FLAG);
                DRV_IOCTL(lchip, slice_id, cmd, &bufretrv_chan);
                SetQMgrDeqChanIdCfg0(V,cfgILoopChanId_f, &chan_cfg, port_attr->chan_id);
                SetBufRetrvChanIdCfg(V,cfgILoopChanId_f, &bufretrv_chan, port_attr->chan_id);
                cmd = DRV_IOW((QMgrDeqChanIdCfg0_t+slice_id), DRV_ENTRY_FLAG);
                DRV_IOCTL(lchip, 0, cmd, &chan_cfg);
                cmd = DRV_IOW((BufRetrvChanIdCfg_t), DRV_ENTRY_FLAG);
                DRV_IOCTL(lchip, slice_id, cmd, &bufretrv_chan);
            }
            else if (port_attr->port_type == SYS_DATAPATH_ELOOP_PORT)
            {
                cmd = DRV_IOR((QMgrDeqChanIdCfg0_t+slice_id), DRV_ENTRY_FLAG);
                DRV_IOCTL(lchip, 0, cmd, &chan_cfg);
                cmd = DRV_IOR((BufRetrvChanIdCfg_t), DRV_ENTRY_FLAG);
                DRV_IOCTL(lchip, slice_id, cmd, &bufretrv_chan);
                SetQMgrDeqChanIdCfg0(V,cfgELoopChanId_f, &chan_cfg, port_attr->chan_id);
                SetBufRetrvChanIdCfg(V,cfgELoopChanId_f, &bufretrv_chan, port_attr->chan_id);
                cmd = DRV_IOW((QMgrDeqChanIdCfg0_t+slice_id), DRV_ENTRY_FLAG);
                DRV_IOCTL(lchip, 0, cmd, &chan_cfg);
                cmd = DRV_IOW((BufRetrvChanIdCfg_t), DRV_ENTRY_FLAG);
                DRV_IOCTL(lchip, slice_id, cmd, &bufretrv_chan);
            }

            /*network channel distribute*/
            if (port_attr->port_type == SYS_DATAPATH_NETWORK_PORT)
            {
                init_num = (port_attr->need_ext)?4:1;

                for (index = 0; index < init_num; index++)
                {
                    speed_mode = index?CTC_PORT_SPEED_10G:port_attr->speed_mode;
                    CTC_ERROR_RETURN(_sys_goldengate_datapath_set_chan_en(lchip, slice_id, speed_mode, (port_attr->chan_id+index)));
                }
            }
        }
    }

    /* dma channel distribute */
    for (slice_id = 0;  slice_id < SYS_MAX_LOCAL_SLICE_NUM; slice_id++)
    {
        cmd = DRV_IOR((QMgrDeqChanIdCfg0_t+slice_id), DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &chan_cfg);
        cmd = DRV_IOR((BufRetrvChanIdCfg_t), DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, slice_id, cmd, &bufretrv_chan);
        SetQMgrDeqChanIdCfg0(V,cfgDma0ChanId_f, &chan_cfg, p_gg_datapath_master[lchip]->dma_chan);
        SetQMgrDeqChanIdCfg0(V,cfgDma1ChanId_f, &chan_cfg, p_gg_datapath_master[lchip]->dma_chan+1);
        SetQMgrDeqChanIdCfg0(V,cfgDma2ChanId_f, &chan_cfg, p_gg_datapath_master[lchip]->dma_chan+2);
        SetQMgrDeqChanIdCfg0(V,cfgDma3ChanId_f, &chan_cfg, p_gg_datapath_master[lchip]->dma_chan+3);

        SetBufRetrvChanIdCfg(V,cfgDma0ChanId_f, &bufretrv_chan, p_gg_datapath_master[lchip]->dma_chan);
        SetBufRetrvChanIdCfg(V,cfgDma1ChanId_f, &bufretrv_chan, p_gg_datapath_master[lchip]->dma_chan+1);
        SetBufRetrvChanIdCfg(V,cfgDma2ChanId_f, &bufretrv_chan, p_gg_datapath_master[lchip]->dma_chan+2);
        SetBufRetrvChanIdCfg(V,cfgDma3ChanId_f, &bufretrv_chan, p_gg_datapath_master[lchip]->dma_chan+3);
        cmd = DRV_IOW((QMgrDeqChanIdCfg0_t+slice_id), DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &chan_cfg);
        cmd = DRV_IOW((BufRetrvChanIdCfg_t), DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, slice_id, cmd, &bufretrv_chan);
    }

    /*check chan, bufretrive and qmgr channel distribute is the same, so just one */
    for (slice_id = 0;  slice_id < SYS_MAX_LOCAL_SLICE_NUM; slice_id++)
    {
        cmd = DRV_IOR((BufRetrvChanIdCfg_t), DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, slice_id, cmd, &bufretrv_chan);
        GetBufRetrvChanIdCfg(A,cfgNetXGChanEn_f, &bufretrv_chan, xg_chan);
        GetBufRetrvChanIdCfg(A,cfgNetXLGChanEn_f, &bufretrv_chan, xlg_chan);
        oam_chan = GetBufRetrvChanIdCfg(V,cfgOamChanId_f, &bufretrv_chan);
        dma_chan = GetBufRetrvChanIdCfg(V,cfgDma0ChanId_f, &bufretrv_chan);
        iloop_chan = GetBufRetrvChanIdCfg(V,cfgILoopChanId_f, &bufretrv_chan);
        eloop_chan = GetBufRetrvChanIdCfg(V,cfgELoopChanId_f, &bufretrv_chan);
        cg_chan0 = GetBufRetrvChanIdCfg(V,cfgCg0ChanId_f, &bufretrv_chan);
        cg_chan1 = GetBufRetrvChanIdCfg(V,cfgCg1ChanId_f, &bufretrv_chan);

        /*Xg channel and Xlg channel should not overwrite*/
        if ((xg_chan[0] & xlg_chan[0]) || (xg_chan[1] & xlg_chan[1]))
        {
            SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"Chan distribute error, XG and XLG channel overwrite!! XG channel:0x%x-0x%x,  XLG channel:0x%x-0x%x \n",
                xg_chan[0], xg_chan[1], xlg_chan[0], xlg_chan[1]);
            return CTC_E_UNEXPECT;
        }

        if ((xg_chan[1] & (1 << (oam_chan-32))) || (xlg_chan[1] & (1 << (oam_chan-32))))
        {
            SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"Chan distribute error, XG/XLG channel and OAM channel overwrite!\n");
            return CTC_E_UNEXPECT;
        }

        if ((xg_chan[1] & (1 << (iloop_chan-32))) || (xlg_chan[1] & (1 << (iloop_chan-32))))
        {
            SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"Chan distribute error, XG/XLG channel and ILOOP channel overwrite!\n");
            return CTC_E_UNEXPECT;
        }

        if ((xg_chan[1] & (1 << (eloop_chan-32))) || (xlg_chan[1] & (1 << (eloop_chan-32))))
        {
            SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"Chan distribute error, XG/XLG channel and ELOOP channel overwrite!\n");
            return CTC_E_UNEXPECT;
        }

        if ((xg_chan[1] & (0xf << (dma_chan-32))) || (xlg_chan[1] & (0xf << (dma_chan-32))))
        {
            SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"Chan distribute error, XG/XLG channel and DMA channel overwrite!\n");
            return CTC_E_UNEXPECT;
        }

        if (cg_chan0 < 32)
        {
            if ((xg_chan[0] & (1 << cg_chan0)) || (xlg_chan[0] & (1 << cg_chan0)))
            {
                SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"Chan distribute error, XG/XLG channel and CG0 channel overwrite!\n");
                return CTC_E_UNEXPECT;
            }
        }
        else
        {
            if ((xg_chan[1] & (1 << (cg_chan0-32))) || (xlg_chan[1] & (1 << (cg_chan0-32))))
            {
                SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"Chan distribute error, XG/XLG channel and CG0 channel overwrite!\n");
                return CTC_E_UNEXPECT;
            }
        }

        if (cg_chan1 < 32)
        {
            if ((xg_chan[0] & (1 << cg_chan1)) || (xlg_chan[0] & (1 << cg_chan1)))
            {
                SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"Chan distribute error, XG/XLG channel and CG0 channel overwrite!\n");
                return CTC_E_UNEXPECT;
            }
        }
        else
        {
            if ((xg_chan[1] & (1 << (cg_chan1-32))) || (xlg_chan[1] & (1 << (cg_chan1-32))))
            {
                SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"Chan distribute error, XG/XLG channel and CG0 channel overwrite!\n");
                return CTC_E_UNEXPECT;
            }
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_datapath_bufretrv_init(uint8 lchip)
{
    uint32 cmd = 0;
    uint8 slice_id = 0;
    BufRetrvCreditCtl0_m buf_credit;
    uint16 lport = 0;
    sys_datapath_lport_attr_t* port_attr = NULL;
    uint16 s_pointer = 0;
    uint16 e_pointer = 0;
    BufRetrvCreditConfig0_m credit_cfg;
    BufRetrvBufCreditConfig0_m buf_cfg;
    uint8 init_num = 1;
    uint8 index = 0;
    uint8 speed_mode = 0;

    /*BufRetrvCreditCtl*/
    for (slice_id = 0; slice_id < SYS_MAX_LOCAL_SLICE_NUM; slice_id++)
    {
        cmd = DRV_IOR((BufRetrvCreditCtl0_t+slice_id), DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &buf_credit);
        SetBufRetrvCreditCtl0(V,rcdCreditConfig_f, &buf_credit, 190);
        SetBufRetrvCreditCtl0(V,ipeBufRetrvCredit_f, &buf_credit, 4);
        SetBufRetrvCreditCtl0(V,bufRetrvDma0CreditThrd_f, &buf_credit, 4);
        SetBufRetrvCreditCtl0(V,bufRetrvDma1CreditThrd_f, &buf_credit, 4);
        SetBufRetrvCreditCtl0(V,bufRetrvDma2CreditThrd_f, &buf_credit, 4);
        SetBufRetrvCreditCtl0(V,bufRetrvDma3CreditThrd_f, &buf_credit, 4);
        cmd = DRV_IOW((BufRetrvCreditCtl0_t+slice_id), DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &buf_credit);
    }

    for (slice_id = 0; slice_id < SYS_MAX_LOCAL_SLICE_NUM; slice_id++)
    {
        /*BufRetrvCreditConfig*/
        cmd = DRV_IOR((BufRetrvCreditConfig0_t+slice_id), DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &credit_cfg);
        SetBufRetrvCreditConfig0(V,creditConfig0_f, &credit_cfg, 11); /*1g*/
        SetBufRetrvCreditConfig0(V,creditConfig1_f, &credit_cfg, 21); /*40g*/
        SetBufRetrvCreditConfig0(V,creditConfig2_f, &credit_cfg, 49); /*100g*/
        SetBufRetrvCreditConfig0(V,creditConfig3_f, &credit_cfg, 64);
        cmd = DRV_IOW((BufRetrvCreditConfig0_t+slice_id), DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &credit_cfg);

        /*BufRetrvBufCreditConfig*/
        cmd = DRV_IOR((BufRetrvBufCreditConfig0_t+slice_id), DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &buf_cfg);
        SetBufRetrvBufCreditConfig0(V,bufThreshold0_f, &buf_cfg, 3);
        SetBufRetrvBufCreditConfig0(V,bufThreshold1_f, &buf_cfg, 7);
        SetBufRetrvBufCreditConfig0(V,bufThreshold2_f, &buf_cfg, 39);
        SetBufRetrvBufCreditConfig0(V,bufThreshold3_f, &buf_cfg, 29);
        cmd = DRV_IOW((BufRetrvBufCreditConfig0_t+slice_id), DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &buf_cfg);
    }

    for (slice_id = 0; slice_id < SYS_MAX_LOCAL_SLICE_NUM; slice_id++)
    {
        s_pointer = 0;
        for (lport = 0; lport < SYS_CHIP_PER_SLICE_PORT_NUM; lport++)
        {
            port_attr = sys_goldengate_datapath_get_port_capability(lchip, lport, slice_id);
            if (!port_attr)
            {
                continue;
            }

            if (port_attr->port_type == SYS_DATAPATH_RSV_PORT)
            {
                /*rsv port have no channel info*/
                continue;
            }

            init_num = (port_attr->need_ext)?4:1;

            for (index = 0; index < init_num; index++)
            {
                speed_mode = index?CTC_PORT_SPEED_10G:port_attr->speed_mode;

                /*init BufRetrvPktConfigMem and BufRetrvPktStatusMem*/
                CTC_ERROR_RETURN(_sys_goldengate_datapath_set_bufretrv_pkt_mem(lchip, slice_id, s_pointer,
                    (port_attr->chan_id+index), speed_mode, &e_pointer));

                /*init BufRetrvBufConfigMem and BufRetrvBufStatusMem*/
                CTC_ERROR_RETURN(_sys_goldengate_datapath_set_bufretrv_buf_mem(lchip, slice_id, s_pointer,
                    (port_attr->chan_id+index), speed_mode, &e_pointer));

                s_pointer = e_pointer+1;

                /*init BufRetrvBufCreditConfig */
                CTC_ERROR_RETURN(_sys_goldengate_datapath_set_bufretrv_buf_credit(lchip, slice_id,
                (port_attr->chan_id+index), speed_mode));

                /*init BufRetrvCreditConfig */
                CTC_ERROR_RETURN(_sys_goldengate_datapath_set_bufretrv_credit(lchip, slice_id,
                (port_attr->chan_id+index), speed_mode));
            }
        }

        /*Dma channel now do not have lport, need distribute resource seperately*/
        for (index = 0; index < 4; index++)
        {
            CTC_ERROR_RETURN(_sys_goldengate_datapath_set_bufretrv_pkt_mem(lchip, slice_id, s_pointer,
                    (p_gg_datapath_master[lchip]->dma_chan+index), 0, &e_pointer));
            CTC_ERROR_RETURN(_sys_goldengate_datapath_set_bufretrv_buf_mem(lchip, slice_id, s_pointer,
                    (p_gg_datapath_master[lchip]->dma_chan+index), 0, &e_pointer));
            CTC_ERROR_RETURN(_sys_goldengate_datapath_set_bufretrv_buf_credit(lchip, slice_id,
                    (p_gg_datapath_master[lchip]->dma_chan+index), 0));
            CTC_ERROR_RETURN(_sys_goldengate_datapath_set_bufretrv_credit(lchip, slice_id,
                    (p_gg_datapath_master[lchip]->dma_chan+index), 0));
             s_pointer = e_pointer+1;
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_datapath_share_init(uint8 lchip)
{
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_datapath_qmgr_init(uint8 lchip)
{
    uint32 cmd = 0;
    uint8 slice_id = 0;
    uint16 lport = 0;
    sys_datapath_lport_attr_t* port_attr = NULL;
    RaChanCredit0_m ra_credit;
    QMgrNetworkWtCfgMem0_m wt_cfg;
    uint8 index = 0;
    uint8 init_num = 1;
    uint8 speed_mode = 0;
    QMgrDeqSliceInterruptNormal0_m qmgr_deq_Inter;
    QMgrDeqSliceParityStatus0_m    qmgr_deq_status;
    QMgrDeqSliceRamChkRec0_m       qmgr_deq_chk_rec;

    /*init QMgrRaChanCredit */
    for (slice_id = 0; slice_id < SYS_MAX_LOCAL_SLICE_NUM; slice_id++)
    {
        for (lport = 0; lport < SYS_CHIP_PER_SLICE_PORT_NUM; lport++)
        {
            port_attr = sys_goldengate_datapath_get_port_capability(lchip, lport, slice_id);
            if (!port_attr)
            {
                continue;
            }

            if (port_attr->port_type == SYS_DATAPATH_NETWORK_PORT)
            {
                init_num = (port_attr->need_ext)?4:1;

                for (index = 0; index < init_num; index++)
                {
                    speed_mode = index?CTC_PORT_SPEED_10G:port_attr->speed_mode;
                    CTC_ERROR_RETURN(_sys_goldengate_datapath_set_qmgr_resource(lchip, speed_mode, (port_attr->chan_id+index), port_attr->slice_id,0));
                }
            }
            else if (port_attr->port_type == SYS_DATAPATH_OAM_PORT)
            {
                SetRaChanCredit0(V, credit_f, &ra_credit, QMGRRA_CHAN_CREDIT_STEP0);
                cmd = DRV_IOW((RaChanCredit0_t+slice_id), DRV_ENTRY_FLAG);
                DRV_IOCTL(lchip, port_attr->chan_id, cmd, &ra_credit);

                SetQMgrNetworkWtCfgMem0(V, weightCfg_f, &wt_cfg, 2);
                cmd = DRV_IOW((QMgrNetworkWtCfgMem0_t+slice_id), DRV_ENTRY_FLAG);
                DRV_IOCTL(lchip, port_attr->chan_id, cmd, &wt_cfg);
            }
            else if (port_attr->port_type == SYS_DATAPATH_DMA_PORT)
            {
                SetRaChanCredit0(V, credit_f, &ra_credit, QMGRRA_CHAN_CREDIT_STEP1);
                SetQMgrNetworkWtCfgMem0(V, weightCfg_f, &wt_cfg, 8);

                cmd = DRV_IOW((RaChanCredit0_t+slice_id), DRV_ENTRY_FLAG);
                DRV_IOCTL(lchip, (port_attr->chan_id), cmd, &ra_credit);

                cmd = DRV_IOW((QMgrNetworkWtCfgMem0_t+slice_id), DRV_ENTRY_FLAG);
                DRV_IOCTL(lchip, (port_attr->chan_id), cmd, &wt_cfg);
            }
            else if (port_attr->port_type == SYS_DATAPATH_ILOOP_PORT)
            {
                SetRaChanCredit0(V, credit_f, &ra_credit, QMGRRA_CHAN_CREDIT_STEP2);
                cmd = DRV_IOW((RaChanCredit0_t+slice_id), DRV_ENTRY_FLAG);
                DRV_IOCTL(lchip, port_attr->chan_id, cmd, &ra_credit);

                SetQMgrNetworkWtCfgMem0(V, weightCfg_f, &wt_cfg, 20);
                cmd = DRV_IOW((QMgrNetworkWtCfgMem0_t+slice_id), DRV_ENTRY_FLAG);
                DRV_IOCTL(lchip, port_attr->chan_id, cmd, &wt_cfg);
            }
            else if (port_attr->port_type == SYS_DATAPATH_ELOOP_PORT)
            {
                SetRaChanCredit0(V, credit_f, &ra_credit, QMGRRA_CHAN_CREDIT_STEP2);
                cmd = DRV_IOW((RaChanCredit0_t+slice_id), DRV_ENTRY_FLAG);
                DRV_IOCTL(lchip, port_attr->chan_id, cmd, &ra_credit);

                SetQMgrNetworkWtCfgMem0(V, weightCfg_f, &wt_cfg, 20);
                cmd = DRV_IOW((QMgrNetworkWtCfgMem0_t+slice_id), DRV_ENTRY_FLAG);
                DRV_IOCTL(lchip, port_attr->chan_id, cmd, &wt_cfg);
            }
        }

        /*special for dma channel*/
        for (index = 0; index < 4; index++)
        {
            SetRaChanCredit0(V, credit_f, &ra_credit, QMGRRA_CHAN_CREDIT_STEP1);
            SetQMgrNetworkWtCfgMem0(V, weightCfg_f, &wt_cfg, 8);

            cmd = DRV_IOW((RaChanCredit0_t+slice_id), DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, (p_gg_datapath_master[lchip]->dma_chan+index), cmd, &ra_credit);

            cmd = DRV_IOW((QMgrNetworkWtCfgMem0_t+slice_id), DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, (p_gg_datapath_master[lchip]->dma_chan+index), cmd, &wt_cfg);
        }
    }

    for (slice_id = 0; slice_id < SYS_MAX_LOCAL_SLICE_NUM; slice_id++)
    {
        /* set QMgrDeqSliceInterruptNormal */
        sal_memset(&qmgr_deq_Inter, 0, sizeof(qmgr_deq_Inter));
        SetQMgrDeqSliceInterruptNormal0(V, dsGrpSchStateParityError_f, &qmgr_deq_Inter, 1);
        SetQMgrDeqSliceInterruptNormal0(V, dsGrpShpProfileEccError_f, &qmgr_deq_Inter, 1);
        SetQMgrDeqSliceInterruptNormal0(V, dsQueShpProfIdEccError_f, &qmgr_deq_Inter, 1);
        SetQMgrDeqSliceInterruptNormal0(V, dsQueShpProfileEccError_f, &qmgr_deq_Inter, 1);
        SetQMgrDeqSliceInterruptNormal0(V, dsSchServiceProfile0ParityError_f, &qmgr_deq_Inter, 1);
        cmd = DRV_IOW((QMgrDeqSliceInterruptNormal0_t+slice_id), DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 1, cmd, &qmgr_deq_Inter);

        /* set QMgrDeqSliceParityStatus */
        cmd = DRV_IOR((QMgrDeqSliceParityStatus0_t+slice_id), DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &qmgr_deq_status);

        SetQMgrDeqSliceParityStatus0(V, dsGrpShpProfileSbeCnt_f, &qmgr_deq_status, 0);
        SetQMgrDeqSliceParityStatus0(V, dsQueShpProfIdSbeCnt_f, &qmgr_deq_status, 0);
        SetQMgrDeqSliceParityStatus0(V, dsQueShpProfileSbeCnt_f, &qmgr_deq_status, 0);
        cmd = DRV_IOW((QMgrDeqSliceParityStatus0_t+slice_id), DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &qmgr_deq_status);

        /* set QMgrDeqSliceRamChkRec */
        cmd = DRV_IOR((QMgrDeqSliceRamChkRec0_t+slice_id), DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &qmgr_deq_chk_rec);

        SetQMgrDeqSliceRamChkRec0(V, dsGrpSchStateParityFail_f, &qmgr_deq_chk_rec, 0);
        SetQMgrDeqSliceRamChkRec0(V, dsGrpShpProfileParityFail_f, &qmgr_deq_chk_rec, 0);
        SetQMgrDeqSliceRamChkRec0(V, dsQueShpProfIdParityFail_f, &qmgr_deq_chk_rec, 0);
        SetQMgrDeqSliceRamChkRec0(V, dsQueShpProfileParityFail_f, &qmgr_deq_chk_rec, 0);
        SetQMgrDeqSliceRamChkRec0(V, dsSchServiceProfile0ParityFail_f, &qmgr_deq_chk_rec, 0);
        cmd = DRV_IOW((QMgrDeqSliceRamChkRec0_t+slice_id), DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &qmgr_deq_chk_rec);
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_datapath_bufstore_init(uint8 lchip)
{
    uint32 cmd = 0;
    BufStoreMiscCtl_m misc_ctl;
    BufStoreReserved_m reserved;
    uint32 value = 0;
    ctc_chip_device_info_t dev_info;
    sal_memset(&dev_info, 0, sizeof(ctc_chip_device_info_t));
    sys_goldengate_chip_get_device_info(lchip, &dev_info);

    cmd = DRV_IOR(BufStoreMiscCtl_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &misc_ctl);
     /*-SetBufStoreMiscCtl(V, bsMetFifoReleaseFifoLFullThrd_f, &misc_ctl, 0x1c);*/
    SetBufStoreMiscCtl(V, resrcMgrDisable_f, &misc_ctl,0x1);
    if(dev_info.version_id >1)
    {
        SetBufStoreMiscCtl(V, maxPktSize_f, &misc_ctl, 16127);
    }
    else
    {
        SetBufStoreMiscCtl(V, maxPktSize_f, &misc_ctl, 15983);
    }

    SetBufStoreMiscCtl(V, minPktSize_f, &misc_ctl,0xa);
    cmd = DRV_IOW(BufStoreMiscCtl_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &misc_ctl);

    /*ignor destIdDiscardExcpVector affect McastRcd for r7292*/
    cmd = DRV_IOR(BufStoreReserved_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &reserved);
    value = GetBufStoreReserved(V, reserved_f, &reserved);
    value |= 0x04;
    SetBufStoreReserved(V, reserved_f, &reserved,value);
    cmd = DRV_IOW(BufStoreReserved_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &reserved);

    /*when the data is wrong, send 0 as the bufferPtr to PbCtl for r7314*/
    cmd = DRV_IOR(BufStoreReserved_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &reserved);
    value = GetBufStoreReserved(V, reserved_f, &reserved);
    value |= 0x08;
    SetBufStoreReserved(V, reserved_f, &reserved,value);
    cmd = DRV_IOW(BufStoreReserved_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &reserved);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_datapath_ipe_init(uint8 lchip)
{
    uint32 cmd = 0;
    uint8 slice_id = 0;
    IpeIntfMapFifoCtl0_m fifo_ctl;
    IpeLkupMgrCreditCtl0_m credit_ctl;

    for (slice_id = 0; slice_id < SYS_MAX_LOCAL_SLICE_NUM; slice_id++)
    {
        cmd = DRV_IOR(IpeIntfMapFifoCtl0_t+slice_id, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &fifo_ctl);
        SetIpeIntfMapFifoCtl0(V, userIdTrackFifoAFullThrd_f, &fifo_ctl, 23);
        cmd = DRV_IOW(IpeIntfMapFifoCtl0_t+slice_id, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &fifo_ctl);

        cmd = DRV_IOR(IpeLkupMgrCreditCtl0_t+slice_id, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &credit_ctl);
        SetIpeLkupMgrCreditCtl0(V,aclProcAclLkupCreditThrd_f, &credit_ctl, 23);
        cmd = DRV_IOW(IpeLkupMgrCreditCtl0_t+slice_id, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &credit_ctl);
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_datapath_netrx_init(uint8 lchip)
{
    uint32 cmd = 0;
    uint16 lport = 0;
    uint8 serdes_id = 0;
    NetRxLenChkCtl0_m len_chk;
    NetRxChannelMap0_m chan_map;
    IpeHeaderAdjustPhyPortMap_m port_map;
    NetRxCgCfg0_m cg_cfg;
    uint8 slice_id = 0;
    uint8 hss_idx = 0;
    uint8 lane_id = 0;
    sys_datapath_hss_attribute_t* p_hss = NULL;
    sys_datapath_serdes_info_t* p_serdes = NULL;
    sys_datapath_lport_attr_t* p_port = NULL;
    uint8 init_num = 1;
    uint8 index = 0;

    sal_memset(&chan_map, 0, sizeof(NetRxChannelMap0_m));
    sal_memset(&port_map, 0, sizeof(IpeHeaderAdjustPhyPortMap_m));

    /*2.NetRxBpduCfg default is macda, skip*/

    /*3.NetRxLenChkCtl init*/
    cmd = DRV_IOR(NetRxLenChkCtl0_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &len_chk);
    SetNetRxLenChkCtl0(V, cfgMinLen0_f, &len_chk, 0x0a);
    SetNetRxLenChkCtl0(V, cfgMinLen1_f, &len_chk, 0x0a);
    SetNetRxLenChkCtl0(V, cfgMinLen2_f, &len_chk, 0x0a);
    SetNetRxLenChkCtl0(V, cfgMinLen3_f, &len_chk, 0x0a);
    SetNetRxLenChkCtl0(V, cfgMinLen4_f, &len_chk, 0x0a);
    SetNetRxLenChkCtl0(V, cfgMinLen5_f, &len_chk, 0x0a);
    SetNetRxLenChkCtl0(V, cfgMinLen6_f, &len_chk, 0x0a);
    SetNetRxLenChkCtl0(V, cfgMinLen7_f, &len_chk, 0x0a);
    cmd = DRV_IOW(NetRxLenChkCtl0_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &len_chk);

    cmd = DRV_IOR(NetRxLenChkCtl1_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &len_chk);
    SetNetRxLenChkCtl1(V, cfgMinLen0_f, &len_chk, 0x0a);
    SetNetRxLenChkCtl1(V, cfgMinLen1_f, &len_chk, 0x0a);
    SetNetRxLenChkCtl1(V, cfgMinLen2_f, &len_chk, 0x0a);
    SetNetRxLenChkCtl1(V, cfgMinLen3_f, &len_chk, 0x0a);
    SetNetRxLenChkCtl1(V, cfgMinLen4_f, &len_chk, 0x0a);
    SetNetRxLenChkCtl1(V, cfgMinLen5_f, &len_chk, 0x0a);
    SetNetRxLenChkCtl1(V, cfgMinLen6_f, &len_chk, 0x0a);
    SetNetRxLenChkCtl1(V, cfgMinLen7_f, &len_chk, 0x0a);
    cmd = DRV_IOW(NetRxLenChkCtl1_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &len_chk);

    /*4. init channel-mac-lport mapping*/
    for (serdes_id = 0; serdes_id < SERDES_NUM_PER_SLICE*2; serdes_id++)
    {
        slice_id = (serdes_id >= SERDES_NUM_PER_SLICE)?1:0;
        hss_idx = SYS_DATAPATH_MAP_SERDES_TO_HSS_INDEX(serdes_id);
        lane_id = SYS_DATAPATH_MAP_SERDES_TO_LANE_ID(serdes_id);

        p_hss = ctc_vector_get(p_gg_datapath_master[lchip]->p_hss_vector, hss_idx);
        if (NULL == p_hss)
        {
            continue;
        }

        p_serdes = (sys_datapath_serdes_info_t*)&p_hss->serdes_info[lane_id];
        lport = p_serdes->lport;

        p_port = (sys_datapath_lport_attr_t*)(&p_gg_datapath_master[lchip]->port_attr[slice_id][lport]);
        if (NULL == p_port)
        {
            continue;
        }

        if (p_port->port_type != SYS_DATAPATH_NETWORK_PORT)
        {
            continue;
        }

        init_num = (p_port->need_ext)?4:1;

        for (index = 0; index < init_num; index++)
        {
            /* mac to channel */
            SetNetRxChannelMap0(V, cfgChanMapping_f, &chan_map, (p_port->chan_id+index));
            cmd = DRV_IOW((NetRxChannelMap0_t+slice_id), DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, (p_port->mac_id+index), cmd, &chan_map);

            /* channel to lport */
            cmd = DRV_IOR((IpeHeaderAdjustPhyPortMap_t), DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, (p_port->chan_id+64*slice_id+index), cmd, &port_map);
            SetIpeHeaderAdjustPhyPortMap(V, localPhyPort_f, &port_map, (lport+index));
            cmd = DRV_IOW((IpeHeaderAdjustPhyPortMap_t), DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, (p_port->chan_id+64*slice_id+index), cmd, &port_map);
        }

        /* 5. init NetRxBufCtl */
        CTC_ERROR_RETURN(_sys_goldengate_datapath_set_netrx_buf(lchip, p_serdes->mode, p_port->mac_id, slice_id));

        /*6. init NetRxCgCfg */
        if (p_port->speed_mode == CTC_PORT_SPEED_100G)
        {
            cmd = DRV_IOR((NetRxCgCfg0_t+p_port->slice_id), DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &cg_cfg);
            if (p_port->mac_id == 36)
            {
                SetNetRxCgCfg0(V,cgMode0_f, &cg_cfg, 1);
            }
            else
            {
                SetNetRxCgCfg0(V,cgMode1_f, &cg_cfg, 1);
            }
            cmd = DRV_IOW((NetRxCgCfg0_t+p_port->slice_id), DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &cg_cfg);
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_datapath_mac_init(uint8 lchip)
{
    uint8 index = 0;
    uint8 slice_id = 0;
    uint16 lport = 0;
    uint16 mac_id = 0;
    uint32 fld_id = 0;
    uint32 cmd = 0;
    uint32 value = 0;
    RefDivPcsLinkPulse_m link_pulse;
    sys_datapath_lport_attr_t* p_port_attr = NULL;

    if (0 == SDK_WORK_PLATFORM)
    {
        drv_goldengate_chip_write(lchip, RLM_NET_CTL_RESET0_ADDR, 0);
        drv_goldengate_chip_write(lchip, RLM_NET_CTL_RESET0_ADDR+4, 0);
        drv_goldengate_chip_write(lchip, RLM_NET_CTL_RESET0_ADDR+8, 0);
        drv_goldengate_chip_write(lchip, RLM_NET_CTL_RESET0_ADDR+0x0c, 0);
        drv_goldengate_chip_write(lchip, RLM_NET_CTL_RESET1_ADDR, 0);
        drv_goldengate_chip_write(lchip, RLM_NET_CTL_RESET1_ADDR+4, 0);
        drv_goldengate_chip_write(lchip, RLM_NET_CTL_RESET1_ADDR+8, 0);
        drv_goldengate_chip_write(lchip, RLM_NET_CTL_RESET1_ADDR+0x0c, 0);
    #if 0
        for (index = 0; index < 8; index++)
        {
            drv_goldengate_chip_write(lchip, SHARED_PCS_SOFT_RST0_ADDR+index*0x100, 0);
        }

        for (index = 0; index < 4; index++)
        {
            drv_goldengate_chip_write(lchip, SHARED_PCS_SOFT_RST8_ADDR+index*0x100, 0);
        }

        for (index = 0; index < 8; index++)
        {
            drv_goldengate_chip_write(lchip, SHARED_PCS_SOFT_RST12_ADDR+index*0x100, 0);
        }

        for (index = 0; index < 4; index++)
        {
            drv_goldengate_chip_write(lchip, SHARED_PCS_SOFT_RST20_ADDR+index*0x100, 0);
        }
#endif

        for (index = 0; index < 24; index++)
        {
            fld_id = XqmacInit0_init_f;
            value = 1;
            cmd = DRV_IOW((XqmacInit0_t+index), fld_id);
            DRV_IOCTL(lchip, 0, cmd, &value);
        }

        for (index = 0; index < 4; index++)
        {
            fld_id = CgmacInit0_init_f;
            value = 1;
            cmd = DRV_IOW((CgmacInit0_t+index), fld_id);
            DRV_IOCTL(lchip, 0, cmd, &value);
        }

        sal_task_sleep(100);

        for (index = 0; index < 24; index++)
        {
            fld_id = 0;
            cmd = DRV_IOR((XqmacInitDone0_t+index), fld_id);
            DRV_IOCTL(lchip, 0, cmd, &value);

            if (!value)
            {
                return CTC_E_NOT_INIT;
            }
        }

        for (index = 0; index < 4; index++)
        {
            fld_id = 0;
            cmd = DRV_IOW((CgmacInitDone0_t+index), fld_id);
            DRV_IOCTL(lchip, 0, cmd, &value);

            if (!value)
            {
                return CTC_E_NOT_INIT;
            }
        }
    }

    /*cfg linktimer*/
    cmd = DRV_IOR(RefDivPcsLinkPulse_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &link_pulse);
    SetRefDivPcsLinkPulse(V, cfgResetDivPcsLinkPulse_f, &link_pulse, 0);
     cmd = DRV_IOW(RefDivPcsLinkPulse_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &link_pulse);

    sal_task_sleep(1);

    cmd = DRV_IOR(RefDivPcsLinkPulse_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &link_pulse);
    SetRefDivPcsLinkPulse(V, cfgRefDivPcsLinkPulse_f, &link_pulse, 0x2625A);
    cmd = DRV_IOW(RefDivPcsLinkPulse_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &link_pulse);

    /*release pulse reset*/
    value = 0;
    cmd = DRV_IOW(RefDivNetEeePulse_t, RefDivNetEeePulse_cfgResetDivNetEeePulse_f);
    DRV_IOCTL(lchip, 0, cmd, &value);

    cmd = DRV_IOW(RefDivNetPausePulse_t, RefDivNetPausePulse_cfgResetDivNetPausePulse_f);
    DRV_IOCTL(lchip, 0, cmd, &value);

    cmd = DRV_IOW(RefDivPcsEeePulse_t, RefDivPcsEeePulse_cfgResetDivPcsEeePulse_f);
    DRV_IOCTL(lchip, 0, cmd, &value);

    cmd = DRV_IOW(RefDivPolicingUpdatePulse_t, RefDivPolicingUpdatePulse_cfgResetDivPolicingUpdatePulse_f);
    DRV_IOCTL(lchip, 0, cmd, &value);

    cmd = DRV_IOW(RefDivQMgrDeqShpPulse_t, RefDivQMgrDeqShpPulse_cfgResetDivQMgrDeqShpPulse_f);
    DRV_IOCTL(lchip, 0, cmd, &value);

    cmd = DRV_IOW(RefDivQMgrEnqDrePulse_t, RefDivQMgrEnqDrePulse_cfgResetDivQMgrEnqDrePulse_f);
    DRV_IOCTL(lchip, 0, cmd, &value);

    cmd = DRV_IOW(RefDivShareDlb_t, RefDivShareDlb_cfgResetDivShareDlbPulse_f);
    DRV_IOCTL(lchip, 0, cmd, &value);

    cmd = DRV_IOW(RefDivShareEfd_t, RefDivShareEfd_cfgResetDivShareEfdPulse_f);
    DRV_IOCTL(lchip, 0, cmd, &value);

    /*cfg mac mode */
    for (slice_id = 0; slice_id < SYS_MAX_LOCAL_SLICE_NUM; slice_id++)
    {
        for (lport = 0; lport < SYS_CHIP_PER_SLICE_PORT_NUM; lport++)
        {
            p_port_attr = sys_goldengate_datapath_get_port_capability(lchip, lport, slice_id);
            if (!p_port_attr)
            {
                continue;
            }

            if (p_port_attr->port_type != SYS_DATAPATH_NETWORK_PORT)
            {
                /*only network port goto set mac mode*/
                continue;
            }

            mac_id = p_port_attr->mac_id;
            if(mac_id > 39)
            {
                mac_id = mac_id - 8;
            }

            if (p_port_attr->pcs_mode != CTC_CHIP_SERDES_NONE_MODE)
            {
                sys_goldengate_port_set_mac_mode(lchip, slice_id, mac_id, p_port_attr->pcs_mode);
                sys_goldengate_port_set_full_thrd(lchip, slice_id, mac_id, p_port_attr->pcs_mode);
            }
        }
    }

    return 0;
}

int32
sys_goldengate_datapath_datapth_init(uint8 lchip)
{
     /*2. ipe init */
    CTC_ERROR_RETURN(_sys_goldengate_datapath_ipe_init(lchip));

     /*1. netrx init */
    CTC_ERROR_RETURN(_sys_goldengate_datapath_netrx_init(lchip));

     /*4. Qmgr init */
    CTC_ERROR_RETURN(_sys_goldengate_datapath_bufstore_init(lchip));

     /*3. share init */
    CTC_ERROR_RETURN(_sys_goldengate_datapath_share_init(lchip));

     /*4. Qmgr init */
    CTC_ERROR_RETURN(_sys_goldengate_datapath_qmgr_init(lchip));

     /*5. bufretr init */
    CTC_ERROR_RETURN(_sys_goldengate_datapath_bufretrv_init(lchip));

     /*6. chan init */
    CTC_ERROR_RETURN(_sys_goldengate_datapath_chan_distribute(lchip));

     /*7. epe init */
    CTC_ERROR_RETURN(_sys_goldengate_datapath_epe_init(lchip));

     /*8. nettx init */
    CTC_ERROR_RETURN(_sys_goldengate_datapath_nettx_init(lchip));

     /*7. misc init */
    CTC_ERROR_RETURN(_sys_goldengate_datapath_misc_init(lchip));

   CTC_ERROR_RETURN(_sys_goldengate_datapath_cfg_global_weight(lchip));

    /*0. mac init*/
    CTC_ERROR_RETURN(_sys_goldengate_datapath_mac_init(lchip));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_datapath_sup_init(uint8 lchip)
{
    uint32 cmd = 0;
    uint32 value = 0;
    SupDskCfg_m sup_dsk;
    SupMiscCfg_m sup_misc;
    PcieRegCrossCtl_m pcie_req;

    if (1 == SDK_WORK_PLATFORM)
    {
        return 0;
    }

    /*release DSK reset*/
    cmd = DRV_IOR(SupDskCfg_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &sup_dsk);
    SetSupDskCfg(V,cfgClkResetClkOobFc_f, &sup_dsk, 0);
    SetSupDskCfg(V,cfgClkResetClkMiscA_f, &sup_dsk, 0);
    SetSupDskCfg(V,cfgClkResetClkMiscB_f, &sup_dsk, 0);
    cmd = DRV_IOW(SupDskCfg_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &sup_dsk);

    /*release Tod/led/mdio reset*/
    cmd = DRV_IOR(SupMiscCfg_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &sup_misc);
    SetSupMiscCfg(V,cfgResetClkLedDiv_f, &sup_misc, 0);
    SetSupMiscCfg(V,cfgResetClkMdioDiv_f, &sup_misc, 0);
    SetSupMiscCfg(V,cfgResetClkMdioXgDiv_f, &sup_misc, 0);
    SetSupMiscCfg(V,cfgResetClkTodDiv_f, &sup_misc, 0);
    cmd = DRV_IOW(SupMiscCfg_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &sup_misc);

    /*release sup reset core */
    cmd = DRV_IOR(SupMiscCfg_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &sup_misc);
    SetSupMiscCfg(V,softResetCore_f, &sup_misc, 0);
    cmd = DRV_IOW(SupMiscCfg_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &sup_misc);

    /*init cfgTsUseIntRefClk */
    value = 1;
    cmd = DRV_IOW(SupMiscCfg_t, SupMiscCfg_cfgTsUseIntRefClk_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));

    /*release regReqCrossEn */
    cmd = DRV_IOR(PcieRegCrossCtl_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &pcie_req);
    SetPcieRegCrossCtl(V,regReqCrossEn_f, &pcie_req, 1);
    cmd = DRV_IOW(PcieRegCrossCtl_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &pcie_req);

    drv_goldengate_chip_write(lchip, 0x00020090, 0);

    /*do SupResetCtl reset*/
    drv_goldengate_chip_write(lchip, SUP_RESET_CTL_ADDR, 0xffffffff);
    drv_goldengate_chip_write(lchip, SUP_RESET_CTL_ADDR, 0);

    /*enable RlmHsCtlEnClk */
    drv_goldengate_chip_write(lchip, RLM_HS_CTL_EN_CLK0_ADDR, 0xffffffff);
    drv_goldengate_chip_write(lchip, RLM_HS_CTL_EN_CLK0_ADDR+4, 0xffffffff);
    drv_goldengate_chip_write(lchip, RLM_HS_CTL_EN_CLK1_ADDR, 0xffffffff);
    drv_goldengate_chip_write(lchip, RLM_HS_CTL_EN_CLK1_ADDR+4, 0xffffffff);

    /*enable RlmCsCtlEnClk */
    drv_goldengate_chip_write(lchip, RLM_CS_CTL_EN_CLK0_ADDR, 0xffffffff);
    drv_goldengate_chip_write(lchip, RLM_CS_CTL_EN_CLK0_ADDR+4, 0xffffffff);
    drv_goldengate_chip_write(lchip, RLM_CS_CTL_EN_CLK0_ADDR+8, 0xffffffff);
    drv_goldengate_chip_write(lchip, RLM_CS_CTL_EN_CLK0_ADDR+0xc, 0xffffffff);
    drv_goldengate_chip_write(lchip, RLM_CS_CTL_EN_CLK1_ADDR, 0xffffffff);
    drv_goldengate_chip_write(lchip, RLM_CS_CTL_EN_CLK1_ADDR+4, 0xffffffff);
    drv_goldengate_chip_write(lchip, RLM_CS_CTL_EN_CLK1_ADDR+8, 0xffffffff);
    drv_goldengate_chip_write(lchip, RLM_CS_CTL_EN_CLK1_ADDR+0xc, 0xffffffff);

    /*release RlmHsCtlReset*/
    drv_goldengate_chip_write(lchip, RLM_HS_CTL_RESET0_ADDR, 0);
    drv_goldengate_chip_write(lchip, RLM_HS_CTL_RESET0_ADDR+4, 0);
    drv_goldengate_chip_write(lchip, RLM_HS_CTL_RESET0_ADDR+8, 0);
    drv_goldengate_chip_write(lchip, RLM_HS_CTL_RESET0_ADDR+0x0c, 0);
    drv_goldengate_chip_write(lchip, RLM_HS_CTL_RESET1_ADDR, 0);
    drv_goldengate_chip_write(lchip, RLM_HS_CTL_RESET1_ADDR+4, 0);
    drv_goldengate_chip_write(lchip, RLM_HS_CTL_RESET1_ADDR+8, 0);
    drv_goldengate_chip_write(lchip, RLM_HS_CTL_RESET1_ADDR+0x0c, 0);

    /*release RlmCsCtlReset*/
    drv_goldengate_chip_write(lchip, RLM_CS_CTL_RESET0_ADDR, 0);
    drv_goldengate_chip_write(lchip, RLM_CS_CTL_RESET0_ADDR+4, 0);
    drv_goldengate_chip_write(lchip, RLM_CS_CTL_RESET0_ADDR+8, 0);
    drv_goldengate_chip_write(lchip, RLM_CS_CTL_RESET0_ADDR+0x0c, 0);
    drv_goldengate_chip_write(lchip, RLM_CS_CTL_RESET1_ADDR, 0);
    drv_goldengate_chip_write(lchip, RLM_CS_CTL_RESET1_ADDR+4, 0);
    drv_goldengate_chip_write(lchip, RLM_CS_CTL_RESET1_ADDR+8, 0);
    drv_goldengate_chip_write(lchip, RLM_CS_CTL_RESET1_ADDR+0x0c, 0);

    /*enable RlmNetCtlEnCLk*/
    drv_goldengate_chip_write(lchip, RLM_NET_CTL_EN_CLK0_ADDR, 0xffffffff);
    drv_goldengate_chip_write(lchip, RLM_NET_CTL_EN_CLK0_ADDR+4, 0xffffffff);
    drv_goldengate_chip_write(lchip, RLM_NET_CTL_EN_CLK0_ADDR+8, 0xffffffff);
    drv_goldengate_chip_write(lchip, RLM_NET_CTL_EN_CLK0_ADDR+0xc, 0xffffffff);
    drv_goldengate_chip_write(lchip, RLM_NET_CTL_EN_CLK1_ADDR, 0xffffffff);
    drv_goldengate_chip_write(lchip, RLM_NET_CTL_EN_CLK1_ADDR+4, 0xffffffff);
    drv_goldengate_chip_write(lchip, RLM_NET_CTL_EN_CLK1_ADDR+8, 0xffffffff);
    drv_goldengate_chip_write(lchip, RLM_NET_CTL_EN_CLK1_ADDR+0xc, 0xffffffff);

    /*release RlmNetCtlReset*/
    drv_goldengate_chip_write(lchip, RLM_NET_CTL_RESET0_ADDR+4, 0xfcf3ffff);
    drv_goldengate_chip_write(lchip, RLM_NET_CTL_RESET1_ADDR+4, 0xfcf3ffff);

    /*release RlmBsCtlReset */
    drv_goldengate_chip_write(lchip, RLM_BS_CTL_RESET_ADDR, 0);

    /*release RlmBrPbCtlReset */
    drv_goldengate_chip_write(lchip, RLM_BR_PB_CTL_RESET_ADDR, 0);

    /*release RlmDeqCtlReset */
    drv_goldengate_chip_write(lchip, RLM_DEQ_CTL_RESET_ADDR, 0);

    /*release RlmEnqCtlReset */
    drv_goldengate_chip_write(lchip, RLM_ENQ_CTL_RESET_ADDR, 0);

    /*release RlmIpeCtlReset */
    drv_goldengate_chip_write(lchip, RLM_IPE_CTL_RESET0_ADDR, 0);
    drv_goldengate_chip_write(lchip, RLM_IPE_CTL_RESET1_ADDR, 0);

    /*release RlmEpeCtlReset */
    drv_goldengate_chip_write(lchip, RLM_EPE_CTL_RESET0_ADDR, 0);
    drv_goldengate_chip_write(lchip, RLM_EPE_CTL_RESET1_ADDR, 0);

    /*release RlmAdCtlReset */
    drv_goldengate_chip_write(lchip, RLM_AD_CTL_RESET0_ADDR, 0);
    drv_goldengate_chip_write(lchip, RLM_AD_CTL_RESET1_ADDR, 0);

    /*release RlmHashCtlReset */
    drv_goldengate_chip_write(lchip, RLM_HASH_CTL_RESET0_ADDR, 0);
    drv_goldengate_chip_write(lchip, RLM_HASH_CTL_RESET1_ADDR, 0);

    /*release RlmShareDsCtlReset */
    drv_goldengate_chip_write(lchip, RLM_SHARE_DS_CTL_RESET_ADDR, 0);

    /*release RlmShareTcamCtlReset */
    drv_goldengate_chip_write(lchip, RLM_SHARE_TCAM_CTL_RESET_ADDR, 0);

    /*do special for bug 6935*/
    /*Enable dram refresh before dram init */
    cmd = DRV_IOW(DynamicDsShareEdramRefreshCtl_t, DynamicDsShareEdramRefreshCtl_refEn_f);
    value = 1;
    DRV_IOCTL(lchip, 0, cmd, &value);

    cmd = DRV_IOW(DsStatsEdramRefreshCtl_t, DsStatsEdramRefreshCtl_refEn_f);
    value = 1;
    DRV_IOCTL(lchip, 0, cmd, &value);

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_datapath_share_module_init(uint8 lchip)
{
    uint32 cmd = 0;
    uint32 fld_id = 0;
    uint32 value = 0;

    /* init */
    fld_id = 0;
    value = 1;
    cmd = DRV_IOW(DsAgingInit_t, fld_id);
    DRV_IOCTL(lchip, 0, cmd, &value);

    cmd = DRV_IOW(FibAccInit_t, fld_id);
    DRV_IOCTL(lchip, 0, cmd, &value);

    cmd = DRV_IOW(FlowAccInit_t, fld_id);
    DRV_IOCTL(lchip, 0, cmd, &value);

    fld_id = GlobalStatsInit_edramInit_f;
    cmd = DRV_IOW(GlobalStatsInit_t, fld_id);
    DRV_IOCTL(lchip, 0, cmd, &value);

    fld_id = GlobalStatsInit_init_f;
    cmd = DRV_IOW(GlobalStatsInit_t, fld_id);
    DRV_IOCTL(lchip, 0, cmd, &value);

    fld_id = LpmTcamIpInit_tcamInit_f;
    cmd = DRV_IOW(LpmTcamIpInit_t, fld_id);
    DRV_IOCTL(lchip, 0, cmd, &value);

    fld_id = LpmTcamIpInit_init_f;
    cmd = DRV_IOW(LpmTcamIpInit_t, fld_id);
    DRV_IOCTL(lchip, 0, cmd, &value);

    fld_id = LpmTcamIpInit_adInit_f;
    cmd = DRV_IOW(LpmTcamIpInit_t, fld_id);
    DRV_IOCTL(lchip, 0, cmd, &value);

    fld_id = PbCtlInit_init_f;
    cmd = DRV_IOW(PbCtlInit_t, fld_id);
    DRV_IOCTL(lchip, 0, cmd, &value);

    fld_id = PolicingInit_init_f;
    cmd = DRV_IOW(PolicingInit_t, fld_id);
    DRV_IOCTL(lchip, 0, cmd, &value);

    fld_id = ShareDlbInit_init_f;
    cmd = DRV_IOW(ShareDlbInit_t, fld_id);
    DRV_IOCTL(lchip, 0, cmd, &value);

    fld_id = ShareEfdInit_init_f;
    cmd = DRV_IOW(ShareEfdInit_t, fld_id);
    DRV_IOCTL(lchip, 0, cmd, &value);

    fld_id = ShareStormCtlInit_init_f;
    cmd = DRV_IOW(ShareStormCtlInit_t, fld_id);
    DRV_IOCTL(lchip, 0, cmd, &value);

    fld_id = DynamicDsShareInit_init_f;
    cmd = DRV_IOW(DynamicDsShareInit_t, fld_id);
    DRV_IOCTL(lchip, 0, cmd, &value);

    fld_id = FlowTcamInit_init_f;
    cmd = DRV_IOW(FlowTcamInit_t, fld_id);
    DRV_IOCTL(lchip, 0, cmd, &value);

    fld_id = FlowTcamInit_tcamInit_f;
    cmd = DRV_IOW(FlowTcamInit_t, fld_id);
    DRV_IOCTL(lchip, 0, cmd, &value);

    fld_id = MetFifoInit_metFifoInit_f;
    cmd = DRV_IOW(MetFifoInit_t, fld_id);
    DRV_IOCTL(lchip, 0, cmd, &value);

    /* init for ipfix entry cnt */
    value = 5;
    fld_id = FlowAccCreditCtl_hashAdTrackCreditThrd_f;
    cmd = DRV_IOW(FlowAccCreditCtl_t, fld_id);
    DRV_IOCTL(lchip, 0, cmd, &value);

    fld_id = FlowAccCreditCtl_hashKeyTrackCreditThrd_f;
    cmd = DRV_IOW(FlowAccCreditCtl_t, fld_id);
    DRV_IOCTL(lchip, 0, cmd, &value);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_datapath_share_module_init_check(uint8 lchip)
{
    uint32 cmd = 0;
    uint32 fld_id = 0;
    uint32 value = 0;

    fld_id = 0;
    cmd = DRV_IOR(DsAgingInitDone_t, fld_id);
    DRV_IOCTL(lchip, 0, cmd, &value);
    if (!value)
    {
        return CTC_E_NOT_INIT;
    }

    fld_id = 0;
    cmd = DRV_IOR(FibAccInitDone_t, fld_id);
    DRV_IOCTL(lchip, 0, cmd, &value);
    if (!value)
    {
        return CTC_E_NOT_INIT;
    }

    fld_id = 0;
    cmd = DRV_IOR(FlowAccInitDone_t, fld_id);
    DRV_IOCTL(lchip, 0, cmd, &value);
    if (!value)
    {
        return CTC_E_NOT_INIT;
    }

    fld_id = 0;
    cmd = DRV_IOR(GlobalStatsInitDone_t, fld_id);
    DRV_IOCTL(lchip, 0, cmd, &value);
    if (!value)
    {
        return CTC_E_NOT_INIT;
    }

    fld_id = 1;
    cmd = DRV_IOR(GlobalStatsInitDone_t, fld_id);
    DRV_IOCTL(lchip, 0, cmd, &value);
    if (!value)
    {
        return CTC_E_NOT_INIT;
    }

    fld_id = 0;
    cmd = DRV_IOR(LpmTcamIpInitDone_t, fld_id);
    DRV_IOCTL(lchip, 0, cmd, &value);
    if (!value)
    {
        return CTC_E_NOT_INIT;
    }

    fld_id = 1;
    cmd = DRV_IOR(LpmTcamIpInitDone_t, fld_id);
    DRV_IOCTL(lchip, 0, cmd, &value);
    if (!value)
    {
        return CTC_E_NOT_INIT;
    }

    fld_id = 2;
    cmd = DRV_IOR(LpmTcamIpInitDone_t, fld_id);
    DRV_IOCTL(lchip, 0, cmd, &value);
    if (!value)
    {
        return CTC_E_NOT_INIT;
    }

    fld_id = 0;
    cmd = DRV_IOR(PbCtlInitDone_t, fld_id);
    DRV_IOCTL(lchip, 0, cmd, &value);
    if (!value)
    {
        return CTC_E_NOT_INIT;
    }

    fld_id = 0;
    cmd = DRV_IOR(PolicingInitDone_t, fld_id);
    DRV_IOCTL(lchip, 0, cmd, &value);
    if (!value)
    {
        return CTC_E_NOT_INIT;
    }

    fld_id = 0;
    cmd = DRV_IOR(ShareDlbInitDone_t, fld_id);
    DRV_IOCTL(lchip, 0, cmd, &value);
    if (!value)
    {
        return CTC_E_NOT_INIT;
    }

    fld_id = 0;
    cmd = DRV_IOR(ShareEfdInitDone_t, fld_id);
    DRV_IOCTL(lchip, 0, cmd, &value);
    if (!value)
    {
        return CTC_E_NOT_INIT;
    }

    fld_id = 0;
    cmd = DRV_IOR(ShareStormCtlInitDone_t, fld_id);
    DRV_IOCTL(lchip, 0, cmd, &value);
    if (!value)
    {
        return CTC_E_NOT_INIT;
    }

    fld_id = 0;
    cmd = DRV_IOR(DynamicDsShareInitDone_t, fld_id);
    DRV_IOCTL(lchip, 0, cmd, &value);
    if (!value)
    {
        return CTC_E_NOT_INIT;
    }

    fld_id = 0;
    cmd = DRV_IOR(FlowTcamInitDone_t, fld_id);
    DRV_IOCTL(lchip, 0, cmd, &value);
    if (!value)
    {
        return CTC_E_NOT_INIT;
    }

    fld_id = 1;
    cmd = DRV_IOR(FlowTcamInitDone_t, fld_id);
    DRV_IOCTL(lchip, 0, cmd, &value);
    if (!value)
    {
        return CTC_E_NOT_INIT;
    }

    fld_id = 0;
    cmd = DRV_IOR(MetFifoInitDone_t, fld_id);
    DRV_IOCTL(lchip, 0, cmd, &value);
    if (!value)
    {
        return CTC_E_NOT_INIT;
    }

    return 0;
}

STATIC int32
_sys_goldengate_datapath_module_init(uint8 lchip)
{
    uint32 cmd = 0;
    uint8 slice_id = 0;
    uint32 fld_id = 0;
    uint32 value = 0;

    for (slice_id = 0; slice_id < SYS_MAX_LOCAL_SLICE_NUM; slice_id++)
    {
        value = 1;

        /*do netrx init */
        cmd = DRV_IOW(NetRxInit0_t+slice_id, NetRxInit0_init_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));

        /*do ipe init*/
        cmd = DRV_IOW(IpeForwardInit0_t+slice_id, IpeForwardInit0_init_f);
        DRV_IOCTL(lchip, 0, cmd, &value);
        cmd = DRV_IOW(IpeHdrAdjInit0_t+slice_id, IpeHdrAdjInit0_init_f);
        DRV_IOCTL(lchip, 0, cmd, &value);
        cmd = DRV_IOW(IpeIntfMapInit0_t+slice_id, IpeIntfMapInit0_init_f);
        DRV_IOCTL(lchip, 0, cmd, &value);
        cmd = DRV_IOW(IpeLkupMgrInit0_t+slice_id, IpeLkupMgrInit0_init_f);
        DRV_IOCTL(lchip, 0, cmd, &value);
        cmd = DRV_IOW(IpePktProcInit0_t+slice_id, IpePktProcInit0_init_f);
        DRV_IOCTL(lchip, 0, cmd, &value);

        cmd = DRV_IOW((QMgrDeqSliceInit0_t+slice_id), QMgrDeqSliceInit0_init_f);
        DRV_IOCTL(lchip, 0, cmd, &value);
        cmd = DRV_IOW((QMgrDeqSliceInit0_t+slice_id), QMgrDeqSliceInit0_stallInit_f);
        DRV_IOCTL(lchip, 0, cmd, &value);
        cmd = DRV_IOW((BufRetrvInit0_t+slice_id), BufRetrvInit0_init_f);
        DRV_IOCTL(lchip, 0, cmd, &value);

        cmd = DRV_IOW((EgrOamHashInit0_t+slice_id), EgrOamHashInit0_init_f);
        DRV_IOCTL(lchip, 0, cmd, &value);
        cmd = DRV_IOW((EpeAclOamInit0_t+slice_id), EpeAclOamInit0_init_f);
        DRV_IOCTL(lchip, 0, cmd, &value);
        cmd = DRV_IOW((EpeHdrAdjInit0_t+slice_id), EpeHdrAdjInit0_init_f);
        DRV_IOCTL(lchip, 0, cmd, &value);
        cmd = DRV_IOW((EpeHdrEditInit0_t+slice_id), EpeHdrEditInit0_init_f);
        DRV_IOCTL(lchip, 0, cmd, &value);
        cmd = DRV_IOW((EpeHdrProcInit0_t+slice_id), EpeHdrProcInit0_init_f);
        DRV_IOCTL(lchip, 0, cmd, &value);
        cmd = DRV_IOW((EpeNextHopInit0_t+slice_id), EpeNextHopInit0_init_f);
        DRV_IOCTL(lchip, 0, cmd, &value);
        cmd = DRV_IOW((FibEngineInit0_t+slice_id), FibEngineInit0_init_f);
        DRV_IOCTL(lchip, 0, cmd, &value);
        cmd = DRV_IOW((LpmTcamNatInit0_t+slice_id), LpmTcamNatInit0_init_f);
        DRV_IOCTL(lchip, 0, cmd, &value);
        cmd = DRV_IOW((LpmTcamNatInit0_t+slice_id), LpmTcamNatInit0_tcamInit_f);
        DRV_IOCTL(lchip, 0, cmd, &value);

        cmd = DRV_IOW((OobFcInit0_t+slice_id), OobFcInit0_init_f);
        DRV_IOCTL(lchip, 0, cmd, &value);
        cmd = DRV_IOW((DynamicDsHashInit0_t+slice_id), DynamicDsHashInit0_init_f);
        DRV_IOCTL(lchip, 0, cmd, &value);
        cmd = DRV_IOW((DynamicDsHashInit0_t+slice_id), DynamicDsHashInit0_dsFlowHashCamInit_f);
        DRV_IOCTL(lchip, 0, cmd, &value);
        cmd = DRV_IOW((DynamicDsHashInit0_t+slice_id), DynamicDsHashInit0_dsFibHost1HashCamInit_f);
        DRV_IOCTL(lchip, 0, cmd, &value);
        cmd = DRV_IOW((DynamicDsHashInit0_t+slice_id), DynamicDsHashInit0_dsFibHost0HashCamInit_f);
        DRV_IOCTL(lchip, 0, cmd, &value);
        cmd = DRV_IOW((DynamicDsAdInit0_t+slice_id), DynamicDsAdInit0_init_f);
        DRV_IOCTL(lchip, 0, cmd, &value);
        cmd = DRV_IOW((UserIdHashInit0_t+slice_id), UserIdHashInit0_userIdHashCamInit_f);
        DRV_IOCTL(lchip, 0, cmd, &value);
        cmd = DRV_IOW((FlowHashInit0_t+slice_id), FlowHashInit0_init_f);
        DRV_IOCTL(lchip, 0, cmd, &value);
        cmd = DRV_IOW((PktProcDsInit0_t+slice_id), PktProcDsInit0_init_f);
        DRV_IOCTL(lchip, 0, cmd, &value);

        cmd = DRV_IOW((NetTxInit0_t+slice_id), NetTxInit0_init_f);
        DRV_IOCTL(lchip, 0, cmd, &value);
    }

    /*do bufstore init*/
    cmd = DRV_IOW(BufStoreInit_t, BufStoreInit_init_f);
    DRV_IOCTL(lchip, 0, cmd, &value);

    /*do QMgrEnq init*/
    cmd = DRV_IOW(QMgrEnqInit_t, QMgrEnqInit_init_f);
    DRV_IOCTL(lchip, 0, cmd, &value);

    cmd = DRV_IOW(QMgrMsgStoreInit_t, QMgrMsgStoreInit_init_f);
    DRV_IOCTL(lchip, 0, cmd, &value);

    /*do Share init*/
    _sys_goldengate_datapath_share_module_init(lchip);

    sal_task_sleep(1000);

    if (0 == SDK_WORK_PLATFORM)
    {
        /*2. check init done */
        for (slice_id = 0; slice_id < SYS_MAX_LOCAL_SLICE_NUM; slice_id++)
        {
            fld_id = 0;
            cmd = DRV_IOR(NetRxInitDone0_t+slice_id, fld_id);
            DRV_IOCTL(lchip, 0, cmd, &value);
            if (!value)
            {
                return CTC_E_NOT_INIT;
            }
            cmd = DRV_IOR(IpeForwardInitDone0_t+slice_id, fld_id);
            DRV_IOCTL(lchip, 0, cmd, &value);
            if (!value)
            {
                return CTC_E_NOT_INIT;
            }
            cmd = DRV_IOR(IpeHdrAdjInitDone0_t+slice_id, fld_id);
            DRV_IOCTL(lchip, 0, cmd, &value);
            if (!value)
            {
                return CTC_E_NOT_INIT;
            }
            cmd = DRV_IOR(IpeIntfMapInitDone0_t+slice_id, fld_id);
            DRV_IOCTL(lchip, 0, cmd, &value);
            if (!value)
            {
                return CTC_E_NOT_INIT;
            }
            cmd = DRV_IOR(IpeLkupMgrInitDone0_t+slice_id, fld_id);
            DRV_IOCTL(lchip, 0, cmd, &value);
            if (!value)
            {
                return CTC_E_NOT_INIT;
            }
            cmd = DRV_IOR(IpePktProcInitDone0_t+slice_id, fld_id);
            DRV_IOCTL(lchip, 0, cmd, &value);
            if (!value)
            {
                return CTC_E_NOT_INIT;
            }
            cmd = DRV_IOR((QMgrDeqSliceInitDone0_t+slice_id), QMgrDeqSliceInitDone0_initDone_f);
            DRV_IOCTL(lchip, 0, cmd, &value);
            if (!value)
            {
                return CTC_E_NOT_INIT;
            }
            cmd = DRV_IOR((QMgrDeqSliceInitDone0_t+slice_id), QMgrDeqSliceInitDone0_stallInitDone_f);
            DRV_IOCTL(lchip, 0, cmd, &value);
            if (!value)
            {
                return CTC_E_NOT_INIT;
            }
            cmd = DRV_IOR((BufRetrvInitDone0_t+slice_id), BufRetrvInitDone0_initDone_f);
            DRV_IOCTL(lchip, 0, cmd, &value);
            if (!value)
            {
                return CTC_E_NOT_INIT;
            }
            cmd = DRV_IOR((EgrOamHashInitDone0_t+slice_id), EgrOamHashInitDone0_initDone_f);
            DRV_IOCTL(lchip, 0, cmd, &value);
            if (!value)
            {
                return CTC_E_NOT_INIT;
            }
            cmd = DRV_IOR((EpeAclOamInitDone0_t+slice_id), EpeAclOamInitDone0_initDone_f);
            DRV_IOCTL(lchip, 0, cmd, &value);
            if (!value)
            {
                return CTC_E_NOT_INIT;
            }
            cmd = DRV_IOR((EpeHdrAdjInitDone0_t+slice_id), EpeHdrAdjInitDone0_initDone_f);
            DRV_IOCTL(lchip, 0, cmd, &value);
            if (!value)
            {
                return CTC_E_NOT_INIT;
            }
            cmd = DRV_IOR((EpeHdrEditInitDone0_t+slice_id), EpeHdrEditInitDone0_initDone_f);
            DRV_IOCTL(lchip, 0, cmd, &value);
            if (!value)
            {
                return CTC_E_NOT_INIT;
            }
            cmd = DRV_IOR((EpeHdrProcInitDone0_t+slice_id), EpeHdrProcInitDone0_initDone_f);
            DRV_IOCTL(lchip, 0, cmd, &value);
            if (!value)
            {
                return CTC_E_NOT_INIT;
            }
            cmd = DRV_IOR((EpeNextHopInitDone0_t+slice_id), EpeNextHopInitDone0_initDone_f);
            DRV_IOCTL(lchip, 0, cmd, &value);
            if (!value)
            {
                return CTC_E_NOT_INIT;
            }
            cmd = DRV_IOR((FibEngineInitDone0_t+slice_id), FibEngineInitDone0_initDone_f);
            DRV_IOCTL(lchip, 0, cmd, &value);
            if (!value)
            {
                return CTC_E_NOT_INIT;
            }
            cmd = DRV_IOR((LpmTcamNatInitDone0_t+slice_id), LpmTcamNatInitDone0_initDone_f);
            DRV_IOCTL(lchip, 0, cmd, &value);
            if (!value)
            {
                return CTC_E_NOT_INIT;
            }
            cmd = DRV_IOR((LpmTcamNatInitDone0_t+slice_id), LpmTcamNatInitDone0_tcamInitDone_f);
            DRV_IOCTL(lchip, 0, cmd, &value);
            if (!value)
            {
                return CTC_E_NOT_INIT;
            }
            cmd = DRV_IOR((OobFcInitDone0_t+slice_id), OobFcInitDone0_initDone_f);
            DRV_IOCTL(lchip, 0, cmd, &value);
            if (!value)
            {
                return CTC_E_NOT_INIT;
            }
            cmd = DRV_IOR((DynamicDsHashInitDone0_t+slice_id), DynamicDsHashInitDone0_dsFibHost0HashCamInitDone_f);
            DRV_IOCTL(lchip, 0, cmd, &value);
            if (!value)
            {
                return CTC_E_NOT_INIT;
            }
            cmd = DRV_IOR((DynamicDsHashInitDone0_t+slice_id), DynamicDsHashInitDone0_dsFibHost1HashCamInitDone_f);
            DRV_IOCTL(lchip, 0, cmd, &value);
            if (!value)
            {
                return CTC_E_NOT_INIT;
            }
            cmd = DRV_IOR((DynamicDsHashInitDone0_t+slice_id), DynamicDsHashInitDone0_dsFlowHashCamInitDone_f);
            DRV_IOCTL(lchip, 0, cmd, &value);
            if (!value)
            {
                return CTC_E_NOT_INIT;
            }
            cmd = DRV_IOR((DynamicDsHashInitDone0_t+slice_id), DynamicDsHashInitDone0_initDone_f);
            DRV_IOCTL(lchip, 0, cmd, &value);
            if (!value)
            {
                return CTC_E_NOT_INIT;
            }
            cmd = DRV_IOR((DynamicDsAdInitDone0_t+slice_id), DynamicDsAdInitDone0_initDone_f);
            DRV_IOCTL(lchip, 0, cmd, &value);
            if (!value)
            {
                return CTC_E_NOT_INIT;
            }
            cmd = DRV_IOR((UserIdHashInitDone0_t+slice_id), UserIdHashInitDone0_userIdHashCamInitDone_f);
            DRV_IOCTL(lchip, 0, cmd, &value);
            if (!value)
            {
                return CTC_E_NOT_INIT;
            }
            cmd = DRV_IOR((FlowHashInitDone0_t+slice_id), FlowHashInitDone0_initDone_f);
            DRV_IOCTL(lchip, 0, cmd, &value);
            if (!value)
            {
                return CTC_E_NOT_INIT;
            }

            cmd = DRV_IOR((NetTxInitDone0_t+slice_id), NetTxInitDone0_initDone_f);
            DRV_IOCTL(lchip, 0, cmd, &value);
            if (!value)
            {
                return CTC_E_NOT_INIT;
            }
        }

        cmd = DRV_IOR(BufStoreInitDone_t, BufStoreInitDone_initDone_f);
        DRV_IOCTL(lchip, 0, cmd, &value);
        if (!value)
        {
            return CTC_E_NOT_INIT;
        }

        cmd = DRV_IOR(QMgrEnqInitDone_t, QMgrEnqInitDone_initDone_f);
        DRV_IOCTL(lchip, 0, cmd, &value);
        if (!value)
        {
            return CTC_E_NOT_INIT;
        }

        cmd = DRV_IOR(QMgrMsgStoreInitDone_t, QMgrMsgStoreInitDone_initDone_f);
        DRV_IOCTL(lchip, 0, cmd, &value);
        if (!value)
        {
            return CTC_E_NOT_INIT;
        }

        CTC_ERROR_RETURN(_sys_goldengate_datapath_share_module_init_check(lchip));
    }
    return 0;
}

STATIC int32
_sys_goldengate_datapath_core_clock_init(uint8 lchip, uint16 core_a, uint16 core_b)
{
#ifdef  EMULATION_PLATFORM
    return 0;
#endif

    PllCoreCfg_m core_cfg;
    PllCoreMon_m core_mon;
    uint32 cmd = 0;

    sal_memset(&core_cfg, 0, sizeof(PllCoreCfg_m));
    sal_memset(&core_mon, 0, sizeof(PllCoreMon_m));

    /*reset core pll*/
    cmd = DRV_IOR(PllCoreCfg_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &core_cfg);
    SetPllCoreCfg(V, cfgPllCoreReset_f, &core_cfg, 1);
    cmd = DRV_IOW(PllCoreCfg_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &core_cfg);

    if ((core_a == 600) && (core_b == 500))
    {
        SetPllCoreCfg(V, cfgPllCoreIntFbk_f, &core_cfg, 1);
        SetPllCoreCfg(V, cfgPllCoreMult124_f, &core_cfg, 1);
        SetPllCoreCfg(V, cfgPllCoreMultInt_f, &core_cfg, 60);
        SetPllCoreCfg(V, cfgPllCoreRange124_f, &core_cfg, 1);
        SetPllCoreCfg(V, cfgPllCoreRangeA_f, &core_cfg, 29);
        SetPllCoreCfg(V, cfgPllCoreRangeB_f, &core_cfg, 28);
    }
    else if ((core_a == 550) && (core_b == 550))
    {
        SetPllCoreCfg(V, cfgPllCoreIntFbk_f, &core_cfg, 1);
        SetPllCoreCfg(V, cfgPllCoreMult124_f, &core_cfg, 1);
        SetPllCoreCfg(V, cfgPllCoreMultInt_f, &core_cfg, 44);
        SetPllCoreCfg(V, cfgPllCoreRange124_f, &core_cfg, 3);
        SetPllCoreCfg(V, cfgPllCoreRangeA_f, &core_cfg, 1);
    }
    else if ((core_a == 500) && (core_b == 500))
    {
        SetPllCoreCfg(V, cfgPllCoreIntFbk_f, &core_cfg, 1);
        SetPllCoreCfg(V, cfgPllCoreMult124_f, &core_cfg, 1);
        SetPllCoreCfg(V, cfgPllCoreMultInt_f, &core_cfg, 60);
        SetPllCoreCfg(V, cfgPllCoreRange124_f, &core_cfg, 1);
        SetPllCoreCfg(V, cfgPllCoreRangeA_f, &core_cfg, 28);
    }
    else if ((core_a == 450) && (core_b == 450))
    {
        SetPllCoreCfg(V, cfgPllCoreIntFbk_f, &core_cfg, 1);
        SetPllCoreCfg(V, cfgPllCoreMult124_f, &core_cfg, 1);
        SetPllCoreCfg(V, cfgPllCoreMultInt_f, &core_cfg, 54);
        SetPllCoreCfg(V, cfgPllCoreRange124_f, &core_cfg, 1);
        SetPllCoreCfg(V, cfgPllCoreRangeA_f, &core_cfg, 28);
    }
    else if ((core_a == 400) && (core_b == 400))
    {
        SetPllCoreCfg(V, cfgPllCoreIntFbk_f, &core_cfg, 1);
        SetPllCoreCfg(V, cfgPllCoreMult124_f, &core_cfg, 1);
        SetPllCoreCfg(V, cfgPllCoreMultInt_f, &core_cfg, 48);
        SetPllCoreCfg(V, cfgPllCoreRange124_f, &core_cfg, 1);
        SetPllCoreCfg(V, cfgPllCoreRangeA_f, &core_cfg, 28);
    }
    else if ((core_a == 575) && (core_b == 460))
    {
        SetPllCoreCfg(V, cfgPllCoreIntFbk_f, &core_cfg, 1);
        SetPllCoreCfg(V, cfgPllCorePreDiv_f, &core_cfg, 1);
        SetPllCoreCfg(V, cfgPllCoreMult124_f, &core_cfg, 1);
        SetPllCoreCfg(V, cfgPllCoreMultInt_f, &core_cfg, 46);
        SetPllCoreCfg(V, cfgPllCoreRange124_f, &core_cfg, 1);
        SetPllCoreCfg(V, cfgPllCoreRangeA_f, &core_cfg, 30);
        SetPllCoreCfg(V, cfgPllCoreRangeB_f, &core_cfg, 29);
    }
    else
    {
        return CTC_E_DATAPATH_INVALID_CORE_FRE;
    }

    cmd = DRV_IOW(PllCoreCfg_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &core_cfg);

    sal_task_sleep(1);

    /*reset core pll*/
    cmd = DRV_IOR(PllCoreCfg_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &core_cfg);
    SetPllCoreCfg(V, cfgPllCoreReset_f, &core_cfg, 0);
    cmd = DRV_IOW(PllCoreCfg_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &core_cfg);

    sal_task_sleep(1);

    /*check pll lock*/
    cmd = DRV_IOR(PllCoreMon_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &core_mon);

    if (0 == SDK_WORK_PLATFORM)
    {
        if (!GetPllCoreMon(V,monPllCoreLock_f, &core_mon))
        {
            return CTC_E_DATAPATH_PLL_NOT_LOCK;
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_datapath_set_serdes_prbs_rx(uint8 lchip, ctc_chip_serdes_prbs_t* p_prbs)
{
    uint8   hss_id    = 0;
    uint8   hss_idx   = 0;
    uint8   lane_id   = 0;
    uint8   serdes_id = 0;
    uint8   internal_lane = 0;
    uint8   hss_type = 0;
    uint32  address = 0;
    uint16  value = 0;
    sys_datapath_hss_attribute_t* p_hss = NULL;
    uint32 tb_id = 0;
    uint32 cmd = 0;
    uint32 table_value = 0;

    serdes_id = p_prbs->serdes_id;
    hss_id = SYS_DATAPATH_MAP_SERDES_TO_HSSID(serdes_id);
    hss_idx = SYS_DATAPATH_MAP_SERDES_TO_HSS_INDEX(serdes_id);
    lane_id = SYS_DATAPATH_MAP_SERDES_TO_LANE_ID(serdes_id);

    if(SYS_DATAPATH_SERDES_IS_HSS28G(serdes_id))
    {
        hss_type = SYS_DATAPATH_HSS_TYPE_28G;
    }
    else
    {
        hss_type = SYS_DATAPATH_HSS_TYPE_15G;
    }

    p_hss = ctc_vector_get(p_gg_datapath_master[lchip]->p_hss_vector, hss_idx);
    if (NULL == p_hss)
    {
        return CTC_E_NONE;
    }

    /* relese hscfg.cfgRx#PrbsRst */
    if (p_hss->hss_type == SYS_DATAPATH_HSS_TYPE_15G)
    {
        tb_id = HsCfg0_t + p_hss->hss_id;
        table_value = 0;
        cmd = DRV_IOW(tb_id, HsCfg0_cfgRxAPrbsRst_f+lane_id*8);
        DRV_IOCTL(lchip, 0, cmd, &table_value);
    }
    else
    {
        tb_id = CsCfg0_t + p_hss->hss_id;
        table_value = 0;
        cmd = DRV_IOW(tb_id, CsCfg0_cfgRxAPrbsRst_f+lane_id*10);
        DRV_IOCTL(lchip, 0, cmd, &table_value);
    }


    /* cfg Receiver Test Control Register, offset 0x01 */
    internal_lane = LANE_ID_MAP_INTERAL_LANE_ID(hss_type, lane_id);
    address = DRV_HSS_ADDR_COMBINE(g_gg_hss_rx_addr_map[internal_lane], 1);

    /* set PATSEL(bit2:0) */
    DRV_CHIP_READ_HSS(hss_type, lchip, hss_id, address, &value);
    value |= 0x8;
    value = (value&0xfff8) + (p_prbs->polynome_type);
    DRV_CHIP_WRITE_HSS(hss_type, lchip, hss_id, address, value);

    /* reset, PRST(bit4)*/
    DRV_CHIP_READ_HSS(hss_type, lchip, hss_id, address, &value);
    value |= 0x10;
    DRV_CHIP_WRITE_HSS(hss_type, lchip, hss_id, address, value);
    sal_task_sleep(1);
    DRV_CHIP_READ_HSS(hss_type, lchip, hss_id, address, &value);
    value &= 0xffef;
    DRV_CHIP_WRITE_HSS(hss_type, lchip, hss_id, address, value);

    /* read result */
    DRV_CHIP_READ_HSS(hss_type, lchip, hss_id, address, &value);
    p_prbs->value = (uint8)((value & 0x300)>>8);

    /*disable prbs rx check*/
    value &= 0xfff7;
    DRV_CHIP_WRITE_HSS(hss_type, lchip, hss_id, address, value);

    /* reset, PRST(bit4)*/
    DRV_CHIP_READ_HSS(hss_type, lchip, hss_id, address, &value);
    value |= 0x10;
    DRV_CHIP_WRITE_HSS(hss_type, lchip, hss_id, address, value);
    DRV_CHIP_READ_HSS(hss_type, lchip, hss_id, address, &value);
    value &= 0xffef;
    DRV_CHIP_WRITE_HSS(hss_type, lchip, hss_id, address, value);

    /* reset hscfg.cfgRx#PrbsRst */
    if (p_hss->hss_type == SYS_DATAPATH_HSS_TYPE_15G)
    {
        tb_id = HsCfg0_t + p_hss->hss_id;
        table_value = 1;
        cmd = DRV_IOW(tb_id, HsCfg0_cfgRxAPrbsRst_f+lane_id*8);
        DRV_IOCTL(lchip, 0, cmd, &table_value);
    }
    else
    {
        tb_id = CsCfg0_t + p_hss->hss_id;
        table_value = 1;
        cmd = DRV_IOW(tb_id, CsCfg0_cfgRxAPrbsRst_f+lane_id*10);
        DRV_IOCTL(lchip, 0, cmd, &table_value);
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_datapath_set_serdes_tx_en(uint8 lchip, uint16 serdes_id, bool enable)
{
    uint8  hss_id    = 0;
    uint8  lane_id   = 0;
    uint8  hss_type = 0;
    uint8  internal_lane = 0;
    uint16 address = 0;
    uint16 value = 0;
    ctc_chip_serdes_loopback_t lb_param;

    sal_memset(&lb_param, 0, sizeof(ctc_chip_serdes_loopback_t));

    lb_param.mode = 1;
    lb_param.serdes_id = serdes_id;
    sys_goldengate_chip_get_property(lchip, CTC_CHIP_PROP_SERDES_LOOPBACK, (void*)&lb_param);
    if ((lb_param.enable) && (!enable))
    {
        return CTC_E_NONE;
    }

    hss_id = SYS_DATAPATH_MAP_SERDES_TO_HSSID(serdes_id);
    lane_id = SYS_DATAPATH_MAP_SERDES_TO_LANE_ID(serdes_id);

    if(SYS_DATAPATH_SERDES_IS_HSS28G(serdes_id))
    {
        hss_type = SYS_DATAPATH_HSS_TYPE_28G;
    }
    else
    {
        hss_type = SYS_DATAPATH_HSS_TYPE_15G;
    }

    internal_lane = LANE_ID_MAP_INTERAL_LANE_ID(hss_type, lane_id);
    address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x03);

    DRV_CHIP_READ_HSS(hss_type, lchip, hss_id, address, &value);
    if(enable)
    {
        value &= 0xffdf;
    }
    else
    {
        value |= 0x0020;
    }
    DRV_CHIP_WRITE_HSS(hss_type, lchip, hss_id, address, value);

    return CTC_E_NONE;
}

int32
sys_goldengate_datapath_serdes_tx_en_with_mac(uint8 lchip, uint8 slice_id, uint8 mac_id)
{
    uint16 lport = 0;
    uint8 num = 0;
    uint8 index = 0;
    uint16 serdes_id = 0;

    lport = sys_goldengate_datapath_get_lport_with_mac(lchip, slice_id, mac_id);
    if (lport == SYS_DATAPATH_USELESS_MAC)
    {
        return CTC_E_MAC_NOT_USED;
    }

    CTC_ERROR_RETURN(sys_goldengate_datapath_get_serdes_with_lport(lchip, lport, slice_id, &serdes_id, &num));

    /* if mac enable, need enable serdes tx */
    for (index = 0; index < num; index++)
    {
        CTC_ERROR_RETURN(sys_goldengate_datapath_set_serdes_tx_en(lchip, serdes_id+index, TRUE));
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_datapath_set_serdes_prbs_tx(uint8 lchip, void* p_data)
{
    uint8  hss_id    = 0;
    uint8  lane_id   = 0;
    uint8  serdes_id = 0;
    uint8  hss_type = 0;
    uint8  internal_lane = 0;
    uint16 address = 0;
    uint16 value = 0;
    ctc_chip_serdes_prbs_t* p_prbs = (ctc_chip_serdes_prbs_t*)p_data;

    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s\n", __FUNCTION__);
    serdes_id = p_prbs->serdes_id;
    hss_id = SYS_DATAPATH_MAP_SERDES_TO_HSSID(serdes_id);
    lane_id = SYS_DATAPATH_MAP_SERDES_TO_LANE_ID(serdes_id);

    if(SYS_DATAPATH_SERDES_IS_HSS28G(serdes_id))
    {
        hss_type = SYS_DATAPATH_HSS_TYPE_28G;
    }
    else
    {
        hss_type = SYS_DATAPATH_HSS_TYPE_15G;
    }

    /* cfg Transmit Test Control Register, offset 0x01 */
    internal_lane = LANE_ID_MAP_INTERAL_LANE_ID(hss_type, lane_id);
    address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 1);

    /* set SPSEL(bit13:11)=000 */
    DRV_CHIP_READ_HSS(hss_type, lchip, hss_id, address, &value);
    value &= 0xc3ff;

    /* value: 1--enable, 0--disbale */
    if(p_prbs->value)
    {
        /* set TPGMD(bit3)=1 */
        value |= 0x8;
    }
    else
    {
        /* set TPGMD(bit3)=0 */
        value &= 0xfff7;
    }

    /* set TPSEL(bit2:0) */
    value = (value&0xfff8) + p_prbs->polynome_type;
    DRV_CHIP_WRITE_HSS(hss_type, lchip, hss_id, address, value);

    /* reset, PRST(bit4)*/
    DRV_CHIP_READ_HSS(hss_type, lchip, hss_id, address, &value);
    value |= 0x10;
    DRV_CHIP_WRITE_HSS(hss_type, lchip, hss_id, address, value);
    sal_task_sleep(1);
    DRV_CHIP_READ_HSS(hss_type, lchip, hss_id, address, &value);
    value &= 0xffef;
    DRV_CHIP_WRITE_HSS(hss_type, lchip, hss_id, address, value);

    return CTC_E_NONE;
}

int32
sys_goldengate_datapath_set_serdes_prbs(uint8 lchip, void* p_data)
{
    uint32 enable;
    uint8 hss_idx = 0;
    uint16 drv_port = 0;
    uint16 gport = 0;
    uint8 lane_id = 0;
    uint8 slice_id = 0;
    uint8 gchip_id = 0;
    ctc_chip_serdes_prbs_t* p_prbs = (ctc_chip_serdes_prbs_t*)p_data;
    sys_datapath_hss_attribute_t* p_hss_vec = NULL;
    sys_datapath_serdes_info_t* p_serdes = NULL;
    CTC_PTR_VALID_CHECK(p_prbs);

    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s\n", __FUNCTION__);
    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_PARAM,"serdes_id:%d, value:%d, type:%d, mode:%d\n", p_prbs->serdes_id, p_prbs->value, p_prbs->mode, p_prbs->polynome_type);

    if (p_prbs->serdes_id >= SYS_GG_DATAPATH_SERDES_NUM)
    {
        return CTC_E_INVALID_PARAM;
    }

    hss_idx = SYS_DATAPATH_MAP_SERDES_TO_HSS_INDEX(p_prbs->serdes_id);
    lane_id = SYS_DATAPATH_MAP_SERDES_TO_LANE_ID(p_prbs->serdes_id);
    slice_id = (p_prbs->serdes_id >= SERDES_NUM_PER_SLICE)?1:0;

    p_hss_vec = ctc_vector_get(p_gg_datapath_master[lchip]->p_hss_vector, hss_idx);
    if (p_hss_vec == NULL)
    {
        return CTC_E_NOT_INIT;
    }

    p_serdes = &p_hss_vec->serdes_info[lane_id];
    drv_port = p_serdes->lport+256*slice_id;
    CTC_ERROR_RETURN(sys_goldengate_get_gchip_id(lchip, &gchip_id));
    gport = SYS_MAP_DRV_LPORT_TO_CTC_GPORT(gchip_id, drv_port);
    /* check, if mac disable, can not do prbs */
    CTC_ERROR_RETURN(sys_goldengate_port_get_mac_en(lchip, gport, &enable));
    if (FALSE == enable)
    {
        return CTC_E_PORT_MAC_IS_DISABLE;
    }

    switch(p_prbs->mode)
    {
        case 0: /* 0--Rx */
            CTC_ERROR_RETURN(_sys_goldengate_datapath_set_serdes_prbs_rx(lchip, p_prbs));
            break;
        case 1: /* 1--Tx */
            CTC_ERROR_RETURN(sys_goldengate_datapath_set_serdes_prbs_tx(lchip, p_prbs));
            break;
        default:
            return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_datapath_set_serdes_loopback_inter(uint8 lchip, ctc_chip_serdes_loopback_t* p_loopback)
{
    uint8  hss_id    = 0;
    uint8  hss_idx   = 0;
    uint8  lane_id   = 0;
    uint8  serdes_id = 0;
    uint8  slice_id  = 0;
    uint8  internal_lane = 0;
    uint8  hss_type = 0;
    uint16 address = 0;
    uint16 value = 0;
    uint32 value_pcs =0;
    uint32 cmd              = 0;
    uint8  index            = 0;
    uint32 tbl_id           = 0;
    uint8  step             = 0;
    uint16 mac_id           = 0;
    sys_datapath_hss_attribute_t* p_hss_vec = NULL;
    sys_datapath_lport_attr_t* p_port_attr = NULL;
    ctc_chip_serdes_polarity_t polarity;
    uint8  flag = 0;

    sal_memset(&polarity, 0, sizeof(ctc_chip_serdes_polarity_t));

    /* 1.get serdes and pcs info */
    serdes_id = p_loopback->serdes_id;
    slice_id = (serdes_id >= SERDES_NUM_PER_SLICE)?1:0;
    hss_id = SYS_DATAPATH_MAP_SERDES_TO_HSSID(serdes_id);
    hss_idx = SYS_DATAPATH_MAP_SERDES_TO_HSS_INDEX(serdes_id); /* notice: hss_id is different from hss_idx */
    lane_id = SYS_DATAPATH_MAP_SERDES_TO_LANE_ID(serdes_id);
    if(SYS_DATAPATH_SERDES_IS_HSS28G(serdes_id))
    {
        hss_type = SYS_DATAPATH_HSS_TYPE_28G;
    }
    else
    {
        hss_type = SYS_DATAPATH_HSS_TYPE_15G;
    }
    p_hss_vec = ctc_vector_get(p_gg_datapath_master[lchip]->p_hss_vector, hss_idx);
    if (p_hss_vec == NULL)
    {
        return CTC_E_NOT_INIT;
    }
    p_port_attr = (sys_datapath_lport_attr_t*)(&p_gg_datapath_master[lchip]->port_attr[slice_id][p_hss_vec->serdes_info[lane_id].lport]);

    mac_id = (p_port_attr->mac_id < 40)?p_port_attr->mac_id:(p_port_attr->mac_id-8);
    value_pcs = 0;
    if ((CTC_CHIP_SERDES_XFI_MODE == p_port_attr->pcs_mode) || (CTC_CHIP_SERDES_SGMII_MODE == p_port_attr->pcs_mode)
        || (CTC_CHIP_SERDES_2DOT5G_MODE == p_port_attr->pcs_mode))
    {
        step = XqmacSgmac1Cfg0_t - XqmacSgmac0Cfg0_t;
        tbl_id = XqmacSgmac0Cfg0_t + (mac_id+slice_id*48) / 4 + ((mac_id+slice_id*48)%4)*step;
        cmd = DRV_IOW(tbl_id, XqmacSgmac0Cfg0_rxEnable0_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value_pcs));
    }
    else if ((CTC_CHIP_SERDES_XLG_MODE == p_port_attr->pcs_mode) || (CTC_CHIP_SERDES_XAUI_MODE == p_port_attr->pcs_mode)
        || ((CTC_CHIP_SERDES_DXAUI_MODE == p_port_attr->pcs_mode)))
    {
        tbl_id = XqmacXlgmacCfg0_t + (mac_id+slice_id*48) / 4;
        cmd = DRV_IOW(tbl_id, XqmacXlgmacCfg0_rxEnable_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value_pcs));
        p_port_attr->switch_num++;
    }
    else if (CTC_CHIP_SERDES_CG_MODE == p_port_attr->pcs_mode)
    {
        tbl_id = CgmacCfg0_t + slice_id*2 + ((mac_id == 36)?0:1);
        cmd = DRV_IOW(tbl_id, CgmacCfg0_rxEnable_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value_pcs));
    }

    /* 2.cfg Receiver Test Control Register, offset 0x01 */
    internal_lane = LANE_ID_MAP_INTERAL_LANE_ID(hss_type, lane_id);
    address = DRV_HSS_ADDR_COMBINE(g_gg_hss_rx_addr_map[internal_lane], 1);
    /* set WPLPEN(bit6) WRPMD(bit5), and do polarity swap */
    DRV_CHIP_READ_HSS(hss_type, lchip, hss_id, address, &value);
    if(p_loopback->enable)
    {
        value |= 0x60;
        /* if rx_polarity is reverse, should set it normal */
        if(1 == p_hss_vec->serdes_info[lane_id].rx_polarity)
        {
            polarity.dir = 0;
            polarity.polarity_mode = 0;
            flag |= 0x1; /* flag is use for record polarity_mode swap */
            polarity.serdes_id = serdes_id;
            CTC_ERROR_RETURN(sys_goldengate_datapath_set_serdes_polarity(lchip, &polarity));
        }
    }
    else
    {
        value &= 0xff9f;
        /* recovery rx_polarity status */
        if(1 == p_hss_vec->serdes_info[lane_id].rx_polarity)
        {
            polarity.dir = 0;
            polarity.polarity_mode = 1;
            polarity.serdes_id = serdes_id;
            CTC_ERROR_RETURN(sys_goldengate_datapath_set_serdes_polarity(lchip, &polarity));
        }
    }
    DRV_CHIP_WRITE_HSS(hss_type, lchip, hss_id, address, value);

    /* 3.cfg the force signal detec */
    if(p_port_attr->pcs_mode == CTC_CHIP_SERDES_CG_MODE)
    {
        tbl_id = CgPcsRxLaneCfg0_t + slice_id*2 + ((p_port_attr->mac_id==36)?0:1);
        value_pcs = (p_loopback->enable)?1:0;
        cmd = DRV_IOW(tbl_id, CgPcsRxLaneCfg0_forceSignalDetect_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value_pcs));
    }
    else
    {
        step = SharedPcsSerdesCfg10_t - SharedPcsSerdesCfg00_t;
        tbl_id = SharedPcsSerdesCfg00_t+ (mac_id+slice_id*48) / 4 + (serdes_id%4)*step;
        value_pcs = (p_loopback->enable)?1:0;
        cmd = DRV_IOW(tbl_id, SharedPcsSerdesCfg00_forceSignalDetect0_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value_pcs));
    }

    /* 4.change sw by flag, used for enable */
    if(CTC_IS_BIT_SET(flag, 0))
    {
        p_hss_vec->serdes_info[lane_id].rx_polarity = 1;
        CTC_BIT_UNSET(flag, 0);
    }

    value_pcs = 1;
    sal_task_sleep(10);
    if ((CTC_CHIP_SERDES_XFI_MODE == p_port_attr->pcs_mode) || (CTC_CHIP_SERDES_SGMII_MODE == p_port_attr->pcs_mode)
        || (CTC_CHIP_SERDES_2DOT5G_MODE == p_port_attr->pcs_mode))
    {

        step = XqmacSgmac1Cfg0_t - XqmacSgmac0Cfg0_t;
        tbl_id = XqmacSgmac0Cfg0_t + (mac_id+slice_id*48) / 4 + ((mac_id+slice_id*48)%4)*step;
        cmd = DRV_IOW(tbl_id, XqmacSgmac0Cfg0_rxEnable0_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value_pcs));
    }
    else if ((CTC_CHIP_SERDES_XLG_MODE == p_port_attr->pcs_mode) || (CTC_CHIP_SERDES_XAUI_MODE == p_port_attr->pcs_mode)
        || ((CTC_CHIP_SERDES_DXAUI_MODE == p_port_attr->pcs_mode)))
    {
        if (p_port_attr->switch_num == 4)
        {
            p_port_attr->switch_num = 0;
            mac_id = (p_port_attr->mac_id < 40)?p_port_attr->mac_id:(p_port_attr->mac_id-8);
            tbl_id = XqmacXlgmacCfg0_t + (mac_id+slice_id*48) / 4;
            cmd = DRV_IOW(tbl_id, XqmacXlgmacCfg0_rxEnable_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value_pcs));
        }
    }
    else if (CTC_CHIP_SERDES_CG_MODE == p_port_attr->pcs_mode)
    {
        tbl_id = CgmacCfg0_t + slice_id*2 + ((mac_id == 36)?0:1);
        cmd = DRV_IOW(tbl_id, CgmacCfg0_rxEnable_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value_pcs));
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_datapath_set_serdes_loopback_exter(uint8 lchip, ctc_chip_serdes_loopback_t* p_loopback)
{
    uint8  hss_id    = 0;
    uint8  lane_id   = 0;
    uint8  serdes_id = 0;
    uint8  hss_type = 0;
    uint8  internal_lane = 0;
    uint16 address = 0;
    uint16 value = 0;
    uint16 gport = 0;
    uint32 enable = 0;

    /* 1.get serdes and pcs info */
    serdes_id = p_loopback->serdes_id;
    hss_id = SYS_DATAPATH_MAP_SERDES_TO_HSSID(serdes_id); /* notice: hss_id is different from hss_idx */
    lane_id = SYS_DATAPATH_MAP_SERDES_TO_LANE_ID(serdes_id);
    if(SYS_DATAPATH_SERDES_IS_HSS28G(serdes_id))
    {
        hss_type = SYS_DATAPATH_HSS_TYPE_28G;
    }
    else
    {
        hss_type = SYS_DATAPATH_HSS_TYPE_15G;
    }

    /* 2.cfg Transmit Configuration Mode Register, offset 0x00 */
    internal_lane = LANE_ID_MAP_INTERAL_LANE_ID(hss_type, lane_id);
    address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0);
    /* set Hss15g RXLOOP(bit8), and Hss28g RXLOOP(bit5) */
    DRV_CHIP_READ_HSS(hss_type, lchip, hss_id, address, &value);
    if(hss_type)
    {
        if(p_loopback->enable)
        {
            value |= 0x20;
        }
        else
        {
            value &= 0xffdf;
        }
        drv_goldengate_chip_write_hss28g(lchip, hss_id, address, value);
    }
    else
    {
        if(p_loopback->enable)
        {
            value |= 0x100;
        }
        else
        {
            value &= 0xfeff;
        }
        drv_goldengate_chip_write_hss15g(lchip, hss_id, address, value);
    }

    /* 3. mac disable, enable send */
    CTC_ERROR_RETURN(sys_goldengate_datapath_get_gport_with_serdes(lchip, serdes_id, &gport));
    CTC_ERROR_RETURN(sys_goldengate_port_get_mac_en(lchip, gport, &enable));
    if (FALSE == enable)
    {
        CTC_ERROR_RETURN(sys_goldengate_datapath_set_serdes_tx_en(lchip, serdes_id, p_loopback->enable));
    }

    return CTC_E_NONE;
}

/* serder internal and external loopback */
int32
sys_goldengate_datapath_set_serdes_loopback(uint8 lchip, void* p_data)
{
    ctc_chip_serdes_loopback_t* p_loopback = (ctc_chip_serdes_loopback_t*)p_data;
    CTC_PTR_VALID_CHECK(p_loopback);

    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s\n", __FUNCTION__);
    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_PARAM,"serdes_id:%d, enable_value:%d, type:%d\n", p_loopback->serdes_id, p_loopback->enable, p_loopback->mode);

    if (p_loopback->serdes_id >= SYS_GG_DATAPATH_SERDES_NUM)
    {
        return CTC_E_INVALID_PARAM;
    }

    switch(p_loopback->mode)
    {
        case 0: /* 0--interal */
            CTC_ERROR_RETURN(_sys_goldengate_datapath_set_serdes_loopback_inter(lchip, p_loopback));
            break;
        case 1: /* 1--external */
            CTC_ERROR_RETURN(_sys_goldengate_datapath_set_serdes_loopback_exter(lchip, p_loopback));
            break;
        default:
            return CTC_E_INVALID_PARAM;
    }
    return CTC_E_NONE;
}


/* serder internal and external loopback */
int32
sys_goldengate_datapath_get_serdes_loopback(uint8 lchip, void* p_data)
{
    uint8  hss_id    = 0;
    uint8  hss_idx   = 0;
    uint8  lane_id   = 0;
    uint8  serdes_id = 0;
    uint8  internal_lane = 0;
    uint8  hss_type = 0;
    uint16 address = 0;
    uint16 value = 0;
    sys_datapath_hss_attribute_t* p_hss_vec = NULL;
    ctc_chip_serdes_loopback_t* p_loopback = (ctc_chip_serdes_loopback_t*)p_data;
    CTC_PTR_VALID_CHECK(p_loopback);

    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s\n", __FUNCTION__);
    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_PARAM,"serdes_id:%d, enable_value:%d, type:%d\n", p_loopback->serdes_id, p_loopback->enable, p_loopback->mode);

    if (p_loopback->serdes_id >= SYS_GG_DATAPATH_SERDES_NUM)
    {
        return CTC_E_INVALID_PARAM;
    }

    serdes_id = p_loopback->serdes_id;
    hss_id = SYS_DATAPATH_MAP_SERDES_TO_HSSID(serdes_id);
    hss_idx = SYS_DATAPATH_MAP_SERDES_TO_HSS_INDEX(serdes_id); /* notice: hss_id is different from hss_idx */
    lane_id = SYS_DATAPATH_MAP_SERDES_TO_LANE_ID(serdes_id);
    if(SYS_DATAPATH_SERDES_IS_HSS28G(serdes_id))
    {
        hss_type = SYS_DATAPATH_HSS_TYPE_28G;
    }
    else
    {
        hss_type = SYS_DATAPATH_HSS_TYPE_15G;
    }
    p_hss_vec = ctc_vector_get(p_gg_datapath_master[lchip]->p_hss_vector, hss_idx);
    if (p_hss_vec == NULL)
    {
        return CTC_E_NOT_INIT;
    }

    internal_lane = LANE_ID_MAP_INTERAL_LANE_ID(hss_type, lane_id);

    switch(p_loopback->mode)
    {
        case 0: /* 0--interal */
        address = DRV_HSS_ADDR_COMBINE(g_gg_hss_rx_addr_map[internal_lane], 1);
        DRV_CHIP_READ_HSS(hss_type, lchip, hss_id, address, &value);
        if ((value & 0x60) == 0x60)
        {
            p_loopback->enable = 1;
        }
        else
        {
            p_loopback->enable = 0;
        }
        break;

        case 1: /* 1--external */
        address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0);
        DRV_CHIP_READ_HSS(hss_type, lchip, hss_id, address, &value);
        if(hss_type)
        {
            if((value & 0x20) == 0x20)
            {
                p_loopback->enable = 1;
            }
            else
            {
                p_loopback->enable = 0;
            }
        }
        else
        {
            if ((value & 0x100) == 0x100)
            {
                p_loopback->enable = 1;
            }
            else
            {
                p_loopback->enable = 0;
            }
        }
        break;

        default:
            return CTC_E_INVALID_PARAM;
    }
    return CTC_E_NONE;
}

int32
sys_goldengate_datapath_get_serdes_mode(uint8 lchip, void* p_data)
{
    uint8  hss_idx   = 0;
    uint8  lane_id   = 0;
    uint8  serdes_id = 0;
    sys_datapath_hss_attribute_t* p_hss_vec = NULL;
    sys_datapath_serdes_info_t* p_serdes = NULL;
    ctc_chip_serdes_info_t* p_serdes_info = (ctc_chip_serdes_info_t*)p_data;
    CTC_PTR_VALID_CHECK(p_serdes_info);

    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s\n", __FUNCTION__);
    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_PARAM,"serdes_id:%d\n", p_serdes_info->serdes_id);

    if (p_serdes_info->serdes_id >= SYS_GG_DATAPATH_SERDES_NUM)
    {
        return CTC_E_INVALID_PARAM;
    }

    serdes_id = p_serdes_info->serdes_id;
    hss_idx = SYS_DATAPATH_MAP_SERDES_TO_HSS_INDEX(serdes_id);
    lane_id = SYS_DATAPATH_MAP_SERDES_TO_LANE_ID(serdes_id);

    p_hss_vec = ctc_vector_get(p_gg_datapath_master[lchip]->p_hss_vector, hss_idx);
    if (p_hss_vec == NULL)
    {
        return CTC_E_NOT_INIT;
    }

    p_serdes = &p_hss_vec->serdes_info[lane_id];


    p_serdes_info->serdes_mode = p_serdes->mode;
    p_serdes_info->overclocking_speed = p_serdes->overclocking_speed;

    return CTC_E_NONE;
}

int32
sys_goldengate_datapath_set_serdes_cfg(uint8 lchip, uint32 chip_prop, void* p_data)
{
    ctc_chip_serdes_cfg_t* p_serdes_cfg = (ctc_chip_serdes_cfg_t*)p_data;
    uint16 address = 0;
    uint16 value  = 0;
    uint8 serdes_id = 0;
    uint8 hss_id = 0;
    uint8 hss_idx = 0;
    uint8 lane_id = 0;
    sys_datapath_hss_attribute_t* p_hss = NULL;
    uint8 internal_lane = 0;
    uint8   hss_type = 0;

    CTC_PTR_VALID_CHECK(p_serdes_cfg);

    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s\n", __FUNCTION__);
    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_PARAM,"serdes_id:%d, value:%d\n", p_serdes_cfg->serdes_id, p_serdes_cfg->value);

    if (p_serdes_cfg->serdes_id >= SYS_GG_DATAPATH_SERDES_NUM)
    {
        return CTC_E_INVALID_PARAM;
    }

    serdes_id = p_serdes_cfg->serdes_id;
    hss_idx = SYS_DATAPATH_MAP_SERDES_TO_HSS_INDEX(serdes_id);
    lane_id = SYS_DATAPATH_MAP_SERDES_TO_LANE_ID(serdes_id);

    if(SYS_DATAPATH_SERDES_IS_HSS28G(serdes_id))
    {
        hss_type = SYS_DATAPATH_HSS_TYPE_28G;
    }
    else
    {
        hss_type = SYS_DATAPATH_HSS_TYPE_15G;
    }

    internal_lane = LANE_ID_MAP_INTERAL_LANE_ID(hss_type, lane_id);
    p_hss = ctc_vector_get(p_gg_datapath_master[lchip]->p_hss_vector, hss_idx);
    if (NULL == p_hss)
    {
        return CTC_E_NONE;
    }

    hss_id = p_hss->hss_id;

    switch(chip_prop)
    {
        case CTC_CHIP_PROP_SERDES_P_FLAG:
            address = DRV_HSS_ADDR_COMBINE(g_gg_hss_rx_addr_map[internal_lane], 0x1b);
            DRV_CHIP_READ_HSS(hss_type, lchip, hss_id, address, &value);
            if (!SYS_DATAPATH_SERDES_IS_HSS28G(serdes_id))
            {
                return CTC_E_NOT_SUPPORT;
            }
            if (p_serdes_cfg->value)
            {
                value |= 0x60;
            }
            else
            {
                value &= 0x9f;
            }
            break;
        case CTC_CHIP_PROP_SERDES_PEAK:
            address = DRV_HSS_ADDR_COMBINE(g_gg_hss_rx_addr_map[internal_lane], 0x0b);
            DRV_CHIP_READ_HSS(hss_type, lchip, hss_id, address, &value);
            if (SYS_DATAPATH_SERDES_IS_HSS28G(serdes_id))
            {
                if (p_serdes_cfg->value > 0x1F)
                {
                    return CTC_E_INVALID_PARAM;
                }
                /*[13:9]bit*/
                value &= 0xC1FF;
                value |= (p_serdes_cfg->value<<9);
            }
            else
            {
                if (p_serdes_cfg->value > 0x7)
                {
                    return CTC_E_INVALID_PARAM;
                }
                /*[10:8]bit*/
                value &= 0xF8FF;
                value |= (p_serdes_cfg->value<<8);
            }
            break;
        case CTC_CHIP_PROP_SERDES_DPC:
            address = DRV_HSS_ADDR_COMBINE(g_gg_hss_rx_addr_map[internal_lane], 0x1f);
            DRV_CHIP_READ_HSS(hss_type, lchip, hss_id, address, &value);
            if (p_serdes_cfg->value)
            {
                value |= (1<<15);
            }
            else
            {
                value &= ~(1<<15);
            }
            break;
        case CTC_CHIP_PROP_SERDES_SLEW_RATE:
            if (SYS_DATAPATH_SERDES_IS_HSS28G(serdes_id))
            {
                if (p_serdes_cfg->value >= 2)
                {
                    return CTC_E_INVALID_PARAM;
                }
            }
            else
            {
                if (p_serdes_cfg->value >= 4)
                {
                    return CTC_E_INVALID_PARAM;
                }
            }
            address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x3);
            DRV_CHIP_READ_HSS(hss_type, lchip, hss_id, address, &value);
            value &= ~(3<<2);
            value |= (p_serdes_cfg->value<<2);
            break;
        default:
            return CTC_E_INVALID_PARAM;
            break;

    }

    DRV_CHIP_WRITE_HSS(hss_type, lchip, hss_id, address, value);

    return CTC_E_NONE;
}

/* get serder slew_rate */
int32
sys_goldengate_datapath_get_serdes_cfg(uint8 lchip, uint32 chip_prop, void* p_data)
{
    ctc_chip_serdes_cfg_t* p_serdes_cfg = (ctc_chip_serdes_cfg_t*)p_data;
    uint16 address = 0;
    uint16 value  = 0;
    uint8 serdes_id = 0;
    uint8 hss_id = 0;
    uint8 hss_idx = 0;
    uint8 lane_id = 0;
    sys_datapath_hss_attribute_t* p_hss = NULL;
    uint8 internal_lane = 0;
    uint8   hss_type = 0;

    CTC_PTR_VALID_CHECK(p_serdes_cfg);

    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s\n", __FUNCTION__);

    if (p_serdes_cfg->serdes_id >= SYS_GG_DATAPATH_SERDES_NUM)
    {
        return CTC_E_INVALID_PARAM;
    }

    serdes_id = p_serdes_cfg->serdes_id;
    hss_idx = SYS_DATAPATH_MAP_SERDES_TO_HSS_INDEX(serdes_id);
    lane_id = SYS_DATAPATH_MAP_SERDES_TO_LANE_ID(serdes_id);

    if(SYS_DATAPATH_SERDES_IS_HSS28G(serdes_id))
    {
        hss_type = SYS_DATAPATH_HSS_TYPE_28G;
    }
    else
    {
        hss_type = SYS_DATAPATH_HSS_TYPE_15G;
    }

    internal_lane = LANE_ID_MAP_INTERAL_LANE_ID(hss_type, lane_id);

    p_hss = ctc_vector_get(p_gg_datapath_master[lchip]->p_hss_vector, hss_idx);
    if (NULL == p_hss)
    {
        return CTC_E_NONE;
    }

    hss_id = p_hss->hss_id;
    switch(chip_prop)
    {
        case CTC_CHIP_PROP_SERDES_P_FLAG:
            address = DRV_HSS_ADDR_COMBINE(g_gg_hss_rx_addr_map[internal_lane], 0x1b);
            DRV_CHIP_READ_HSS(hss_type, lchip, hss_id, address, &value);
            if (!SYS_DATAPATH_SERDES_IS_HSS28G(serdes_id))
            {
                return CTC_E_NOT_SUPPORT;
            }

            if ((value & 0x60) == 0x60)
            {
                p_serdes_cfg->value = 1;
            }
            else
            {
                p_serdes_cfg->value = 0;
            }
            break;
        case CTC_CHIP_PROP_SERDES_PEAK:
            address = DRV_HSS_ADDR_COMBINE(g_gg_hss_rx_addr_map[internal_lane], 0x0b);
            DRV_CHIP_READ_HSS(hss_type, lchip, hss_id, address, &value);
            if (SYS_DATAPATH_SERDES_IS_HSS28G(serdes_id))
            {
                p_serdes_cfg->value = ((value>>9)&0x1F);
            }
            else
            {
                p_serdes_cfg->value = ((value>>8)&0x7);
            }
            break;
        case CTC_CHIP_PROP_SERDES_DPC:
            address = DRV_HSS_ADDR_COMBINE(g_gg_hss_rx_addr_map[internal_lane], 0x1f);
            DRV_CHIP_READ_HSS(hss_type, lchip, hss_id, address, &value);
            if (CTC_IS_BIT_SET(value, 15))
            {
                p_serdes_cfg->value = 1;
            }
            else
            {
                p_serdes_cfg->value = 0;
            }
            break;
        case CTC_CHIP_PROP_SERDES_SLEW_RATE:
            address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x3);
            DRV_CHIP_READ_HSS(hss_type, lchip, hss_id, address, &value);
            p_serdes_cfg->value = (value>>2)&0x3;
            break;
        default:
            return CTC_E_INVALID_PARAM;
            break;

    }



    return CTC_E_NONE;
}

int32
sys_goldengate_datapath_set_serdes_eqmode(uint8 lchip, uint8 serdes_id, uint8 enable)
{
    uint8   hss_id    = 0;
    uint8   hss_idx   = 0;
    uint8   lane_id   = 0;
    uint8   internal_lane = 0;
    uint8   hss_type = 0;
    uint32  address = 0;
    uint16  value = 0;
    sys_datapath_hss_attribute_t* p_hss = NULL;

    /* debug */
    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s\n", __FUNCTION__);
    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_PARAM,"serdes_id:%d, enable:%d\n", serdes_id, enable);

    /* get serdes info by serdes id */
    hss_id = SYS_DATAPATH_MAP_SERDES_TO_HSSID(serdes_id);
    hss_idx = SYS_DATAPATH_MAP_SERDES_TO_HSS_INDEX(serdes_id);
    lane_id = SYS_DATAPATH_MAP_SERDES_TO_LANE_ID(serdes_id);

    if(SYS_DATAPATH_SERDES_IS_HSS28G(serdes_id))
    {
        hss_type = SYS_DATAPATH_HSS_TYPE_28G;
    }
    else
    {
        hss_type = SYS_DATAPATH_HSS_TYPE_15G;
    }
    internal_lane = LANE_ID_MAP_INTERAL_LANE_ID(hss_type, lane_id);

    p_hss = ctc_vector_get(p_gg_datapath_master[lchip]->p_hss_vector, hss_idx);
    if (!p_hss)
    {
        return CTC_E_DATAPATH_HSS_NOT_READY;
    }

    /* set EQMODE bit4*/
    address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x02);
    DRV_CHIP_READ_HSS(hss_type, lchip, hss_id, address, &value);
    if(enable)
    {
        value |= 0x0010; /* used for 802.3ap mode ffe */
    }
    else
    {
        value &= 0xffef; /* used for traditional mode ffe */
    }
    DRV_CHIP_WRITE_HSS(hss_type, lchip, hss_id, address, value);

    return CTC_E_NONE;
}

/*CgPcsReserved0 bit0:1-3ap 0-userdefine*/
STATIC int32
_sys_goldengate_datapath_set_cgpcs_rsv_en(uint8 lchip, uint8 serdes_id, uint8 bit, uint32 value)
{
    uint32 cgpcs_rsv = 0;
    uint8   hss_idx   = 0;
    uint8   lane_id   = 0;
    uint8   slice_id  = 0;
    uint32  cmd        = 0;
    uint32  tbl_id     = 0;
    sys_datapath_hss_attribute_t* p_hss_vec = NULL;
    sys_datapath_lport_attr_t* p_port_attr = NULL;

    /* get serdes info by serdes id */
    slice_id = (serdes_id >= SERDES_NUM_PER_SLICE)?1:0;
    hss_idx = SYS_DATAPATH_MAP_SERDES_TO_HSS_INDEX(serdes_id);
    lane_id = SYS_DATAPATH_MAP_SERDES_TO_LANE_ID(serdes_id);

    p_hss_vec = ctc_vector_get(p_gg_datapath_master[lchip]->p_hss_vector, hss_idx);
    if (p_hss_vec == NULL)
    {
        return CTC_E_NOT_INIT;
    }
    p_port_attr = (sys_datapath_lport_attr_t*)(&p_gg_datapath_master[lchip]->port_attr[slice_id][p_hss_vec->serdes_info[lane_id].lport]);

    /* do fec cfg */
    if(CTC_CHIP_SERDES_CG_MODE == p_port_attr->pcs_mode)
    {
        tbl_id = CgPcsReserved0_t + (p_port_attr->slice_id*2) + ((p_port_attr->mac_id==36)?0:1);
        cmd = DRV_IOR(tbl_id, CgPcsReserved0_reserved_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &cgpcs_rsv));

        if (value)
        {
            CTC_BIT_SET(cgpcs_rsv, bit);
        }
        else
        {
            CTC_BIT_UNSET(cgpcs_rsv, bit);
        }
        cmd = DRV_IOW(tbl_id, CgPcsReserved0_reserved_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &cgpcs_rsv));
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_datapath_get_cgpcs_rsv_en(uint8 lchip, uint8 serdes_id, uint8 bit, uint32* p_value)
{
    uint32 cgpcs_rsv = 0;
    uint8   hss_idx   = 0;
    uint8   lane_id   = 0;
    uint8   slice_id  = 0;
    uint32  cmd        = 0;
    uint32  tbl_id     = 0;
    sys_datapath_hss_attribute_t* p_hss_vec = NULL;
    sys_datapath_lport_attr_t* p_port_attr = NULL;

    /* get serdes info by serdes id */
    slice_id = (serdes_id >= SERDES_NUM_PER_SLICE)?1:0;
    hss_idx = SYS_DATAPATH_MAP_SERDES_TO_HSS_INDEX(serdes_id);
    lane_id = SYS_DATAPATH_MAP_SERDES_TO_LANE_ID(serdes_id);

    p_hss_vec = ctc_vector_get(p_gg_datapath_master[lchip]->p_hss_vector, hss_idx);
    if (p_hss_vec == NULL)
    {
        return CTC_E_NOT_INIT;
    }
    p_port_attr = (sys_datapath_lport_attr_t*)(&p_gg_datapath_master[lchip]->port_attr[slice_id][p_hss_vec->serdes_info[lane_id].lport]);

    *p_value = 0;

    /* do fec cfg */
    if(CTC_CHIP_SERDES_CG_MODE == p_port_attr->pcs_mode)
    {
        tbl_id = CgPcsReserved0_t + (p_port_attr->slice_id*2) + ((p_port_attr->mac_id==36)?0:1);
        cmd = DRV_IOR(tbl_id, CgPcsReserved0_reserved_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &cgpcs_rsv));
        if (CTC_IS_BIT_SET(cgpcs_rsv, bit))
        {
            *p_value = 1;
        }
        else
        {
            *p_value = 0;
        }
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_datapath_get_serdes_ffe_status(uint8 lchip, uint8 serdes_id, uint8 mode, uint8* p_value)
{
    uint8   hss_id    = 0;
    uint8   hss_idx   = 0;
    uint8   lane_id   = 0;
    uint8   internal_lane = 0;
    uint8   hss_type = 0;
    uint32  address = 0;
    uint16  value = 0;
    sys_datapath_hss_attribute_t* p_hss = NULL;

    /* debug */
    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s\n", __FUNCTION__);
    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_PARAM,"serdes_id:%d\n", serdes_id);

    /* get serdes info by serdes id */
    hss_id = SYS_DATAPATH_MAP_SERDES_TO_HSSID(serdes_id);
    hss_idx = SYS_DATAPATH_MAP_SERDES_TO_HSS_INDEX(serdes_id);
    lane_id = SYS_DATAPATH_MAP_SERDES_TO_LANE_ID(serdes_id);

    if(SYS_DATAPATH_SERDES_IS_HSS28G(serdes_id))
    {
        hss_type = SYS_DATAPATH_HSS_TYPE_28G;
    }
    else
    {
        hss_type = SYS_DATAPATH_HSS_TYPE_15G;
    }
    internal_lane = LANE_ID_MAP_INTERAL_LANE_ID(hss_type, lane_id);

    p_hss = ctc_vector_get(p_gg_datapath_master[lchip]->p_hss_vector, hss_idx);
    if (!p_hss)
    {
        return CTC_E_DATAPATH_HSS_NOT_READY;
    }

    /* set EQMODE bit4*/
    address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x02);
    DRV_CHIP_READ_HSS(hss_type, lchip, hss_id, address, &value);
    if (mode == CTC_CHIP_SERDES_FFE_MODE_3AP && CTC_IS_BIT_SET(value, 4))
    {
        *p_value = 1; /* used for 802.3ap mode ffe */
    }
    else if (mode != CTC_CHIP_SERDES_FFE_MODE_3AP && !CTC_IS_BIT_SET(value, 4))
    {
        *p_value = 1; /* used for traditional mode ffe */

    }
    else
    {
        *p_value = 0;
    }

    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_INFO,"serdes_id:%d, status:%u\n", serdes_id, *p_value);

    return CTC_E_NONE;
}

int32
_sys_goldengate_datapath_check_serdes_ffe(uint8 lchip, ctc_chip_serdes_ffe_t* p_ffe)
{
    uint16 c0 = 0;
    uint16 c1 = 0;
    uint16 c2 = 0;
    uint16 c3 = 0;

    c0 = p_ffe->coefficient[0];
    c1 = p_ffe->coefficient[1];
    c2 = p_ffe->coefficient[2];
    c3 = p_ffe->coefficient[3];

    if (CTC_CHIP_SERDES_FFE_MODE_TYPICAL == p_ffe->mode)
    {
        return CTC_E_NONE;
    }

    if(SYS_DATAPATH_SERDES_IS_HSS28G(p_ffe->serdes_id))
    {
        if ((c0+c1+c2+c3) > 64 )
        {
            SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_ERROR,"serdes_id:%d, the ffe param must c0+c1+c2+c3 <= 64\n", p_ffe->serdes_id);
            return CTC_E_INVALID_PARAM;
        }
        if (CTC_CHIP_SERDES_FFE_MODE_3AP == p_ffe->mode)
        {
            if ((c0+c2) <= (c1+c3))
            {
                SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_ERROR,"serdes_id:%d, the ffe param must c0+c2>c1+c3\n", p_ffe->serdes_id);
                return CTC_E_INVALID_PARAM;
            }
        }
    }
    else
    {
        if ((c0+c1+c2) > 60)
        {
            SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_ERROR,"serdes_id:%d, the ffe param must c0+c1+c2<=60\n", p_ffe->serdes_id);
            return CTC_E_INVALID_PARAM;
        }

        if(CTC_CHIP_SERDES_FFE_MODE_3AP == p_ffe->mode)
        {
            if ((c0+c2) >= c1)
            {
                SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_ERROR,"serdes_id:%d, the ffe param must c1>c0+c2\n", p_ffe->serdes_id);
                return CTC_E_INVALID_PARAM;
            }

            if (c0>15)
            {
                SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_ERROR,"serdes_id:%d, the ffe param must c0<=15\n", p_ffe->serdes_id);
                return CTC_E_INVALID_PARAM;
            }
            if (c1>60)
            {
                SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_ERROR,"serdes_id:%d, the ffe param must c1<=60\n", p_ffe->serdes_id);
                return CTC_E_INVALID_PARAM;
            }
            if (c2>30)
            {
                SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_ERROR,"serdes_id:%d, the ffe param must c2<=30\n", p_ffe->serdes_id);
                return CTC_E_INVALID_PARAM;
            }
        }
        else
        {
            if (c0>25)
            {
                SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_ERROR,"serdes_id:%d, the ffe param must c0<=25\n", p_ffe->serdes_id);
                return CTC_E_INVALID_PARAM;
            }
            if ((c1>60) || (c1 <13))
            {
                SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_ERROR,"serdes_id:%d, the ffe param must 13<=c1<=60\n", p_ffe->serdes_id);
                return CTC_E_INVALID_PARAM;
            }
            if (c2>48)
            {
                SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_ERROR,"serdes_id:%d, the ffe param must c2<=48\n", p_ffe->serdes_id);
                return CTC_E_INVALID_PARAM;
            }
        }
    }

    return CTC_E_NONE;
}

/* set serdes ffe for 802.3ap mode */
int32
sys_goldengate_datapath_set_serdes_ffe_3ap(uint8 lchip, ctc_chip_serdes_ffe_t* p_ffe)
{
    uint8   hss_id    = 0;
    uint8   hss_idx   = 0;
    uint8   lane_id   = 0;
    uint8   serdes_id = 0;
    uint8   internal_lane = 0;
    uint8   hss_type = 0;
    uint32  address = 0;
    uint16  value = 0;
    sys_datapath_hss_attribute_t* p_hss = NULL;

    /* debug param */
    CTC_PTR_VALID_CHECK(p_ffe);
    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s\n", __FUNCTION__);

    /* get serdes info by serdes id */
    serdes_id = p_ffe->serdes_id;
    hss_id = SYS_DATAPATH_MAP_SERDES_TO_HSSID(serdes_id);
    hss_idx = SYS_DATAPATH_MAP_SERDES_TO_HSS_INDEX(serdes_id);
    lane_id = SYS_DATAPATH_MAP_SERDES_TO_LANE_ID(serdes_id);

    if(SYS_DATAPATH_SERDES_IS_HSS28G(serdes_id))
    {
        hss_type = SYS_DATAPATH_HSS_TYPE_28G;
    }
    else
    {
        hss_type = SYS_DATAPATH_HSS_TYPE_15G;
    }
    internal_lane = LANE_ID_MAP_INTERAL_LANE_ID(hss_type, lane_id);

    p_hss = ctc_vector_get(p_gg_datapath_master[lchip]->p_hss_vector, hss_idx);
    if (!p_hss)
    {
        return CTC_E_DATAPATH_HSS_NOT_READY;
    }

    /* CFG AESRC, use pin(3ap train) or register FFE param */
    address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x02);
    DRV_CHIP_READ_HSS(hss_type, lchip, hss_id, address, &value);
    value &= 0xffdf;
    DRV_CHIP_WRITE_HSS(hss_type, lchip, hss_id, address, value);

    /* CFG c0 c1 c2 c3.... */
    if(SYS_DATAPATH_HSS_TYPE_15G == hss_type)
    {
        /* step 1, enable EQMODE */
        CTC_ERROR_RETURN(sys_goldengate_datapath_set_serdes_eqmode(lchip, serdes_id, 1));

        /* step 2, cfg Vm Maximum Value Register, max +60 */
        address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x1e);
        value = 0x3d;
        DRV_CHIP_WRITE_HSS(hss_type, lchip, hss_id, address, value);

        address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x1f);
        value = 0xf;
        DRV_CHIP_WRITE_HSS(hss_type, lchip, hss_id, address, value);

        /* step 3, cfg V2 Limit Extended Register, min 0 */
        address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x1e);
        value = 0x00;
        DRV_CHIP_WRITE_HSS(hss_type, lchip, hss_id, address, value);

        address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x1f);
        value = 0x11;
        DRV_CHIP_WRITE_HSS(hss_type, lchip, hss_id, address, value);

        /* step 4, cfg C0 Limit Extended Register: 0-0xa */
        address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x1e);
        value = 0x11;
        DRV_CHIP_WRITE_HSS(hss_type, lchip, hss_id, address, value);

        address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x1f);
        value = 0x05;
        DRV_CHIP_WRITE_HSS(hss_type, lchip, hss_id, address, value);

        /* step 5, cfg C1 Limit Extended Register 0xD-0x3C */
        address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x1e);
        value = 0x3d00;
        DRV_CHIP_WRITE_HSS(hss_type, lchip, hss_id, address, value);

        address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x1f);
        value = 0x09;
        DRV_CHIP_WRITE_HSS(hss_type, lchip, hss_id, address, value);

        /* step 6, cfg C2 Limit Extended Register 0-0x1F */
        address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x1e);
        value = 0x0021;
        DRV_CHIP_WRITE_HSS(hss_type, lchip, hss_id, address, value);

        address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x1f);
        value = 0x0d;
        DRV_CHIP_WRITE_HSS(hss_type, lchip, hss_id, address, value);

        /* step 7, cfg C0/C1/C2 Init Extended Register */
         /*C0 bit[4:0]*/
        address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x1e);
        value = p_ffe->coefficient[0];
        value = (~value);
        value &= 0x000f;
        value |= 0x0010;
        value += 1;
        DRV_CHIP_WRITE_HSS(hss_type, lchip, hss_id, address, value);

        address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x1f);
        value = 0x03;
        DRV_CHIP_WRITE_HSS(hss_type, lchip, hss_id, address, value);

         /*C1 bit[6:0]*/
        address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x1e);
        value = p_ffe->coefficient[1];
        DRV_CHIP_WRITE_HSS(hss_type, lchip, hss_id, address, value);

        address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x1f);
        value = 0x07;
        DRV_CHIP_WRITE_HSS(hss_type, lchip, hss_id, address, value);

         /*C2 bit[5:0]*/
        address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x1e);
        value = p_ffe->coefficient[2];
        value = (~value);
        value &= 0x001f;
        value |= 0x0020;
        value += 1;
        DRV_CHIP_WRITE_HSS(hss_type, lchip, hss_id, address, value);

        address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x1f);
        value = 0x0b;
        DRV_CHIP_WRITE_HSS(hss_type, lchip, hss_id, address, value);

        /* step 8, cfg Transmit 802.3ap Adaptive Equalization Command Register */
        address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x0e);
        DRV_CHIP_READ_HSS(hss_type, lchip, hss_id, address, &value);
        value |= 0x1000;
        DRV_CHIP_WRITE_HSS(hss_type, lchip, hss_id, address, value);

        /* setp 9, Read Status */

        /* step 10, write 0 to bits[13:0] 0f the command register to put all of the coefficients in the hold state */
        address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x0e);
        DRV_CHIP_READ_HSS(hss_type, lchip, hss_id, address, &value);
        value &= 0xc000;
        DRV_CHIP_WRITE_HSS(hss_type, lchip, hss_id, address, value);
    }
    else if(SYS_DATAPATH_HSS_TYPE_28G == hss_type)
    {
        /* step 1, cfg tx 0x02 bit[6:4]*/
        CTC_ERROR_RETURN(sys_goldengate_datapath_set_serdes_eqmode(lchip, serdes_id, 1));

        /* step 2, cfg Vm Maximum Value Register, max +64 */
        address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x1e);
        value = 0x41;
        DRV_CHIP_WRITE_HSS(hss_type, lchip, hss_id, address, value);

        address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x1f);
        value = 0xf;
        DRV_CHIP_WRITE_HSS(hss_type, lchip, hss_id, address, value);

        /* step 3, cfg V2 Limit Extended Register, min 0 */
        address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x1e);
        value = 0x00;
        DRV_CHIP_WRITE_HSS(hss_type, lchip, hss_id, address, value);

        address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x1f);
        value = 0x11;
        DRV_CHIP_WRITE_HSS(hss_type, lchip, hss_id, address, value);

        /* step 4, cfg C0 Limit Extended Register: 0-0xa */
        address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x1e);
        value = 0xc0;
        DRV_CHIP_WRITE_HSS(hss_type, lchip, hss_id, address, value);

        address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x1f);
        value = 0x05;
        DRV_CHIP_WRITE_HSS(hss_type, lchip, hss_id, address, value);

        /* C1 */
        address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x1e);
        value = 0xc0;
        DRV_CHIP_WRITE_HSS(hss_type, lchip, hss_id, address, value);

        address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x1f);
        value = 0x09;
        DRV_CHIP_WRITE_HSS(hss_type, lchip, hss_id, address, value);

        /* C2 */
        address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x1e);
        value = 0x4100;
        DRV_CHIP_WRITE_HSS(hss_type, lchip, hss_id, address, value);

        address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x1f);
        value = 0x0d;
        DRV_CHIP_WRITE_HSS(hss_type, lchip, hss_id, address, value);

        /* C3 */
        address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x1e);
        value = 0xc0;
        DRV_CHIP_WRITE_HSS(hss_type, lchip, hss_id, address, value);

        address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x1f);
        value = 0x15;
        DRV_CHIP_WRITE_HSS(hss_type, lchip, hss_id, address, value);

        /* step 5, cfg C0/C1/C2/C3 Init Extended Register */
         /*C0*/
        address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x1e);
        value = p_ffe->coefficient[0];
        value = (~value);
        value &= 0x007f;
        value |= 0x0080;
        value += 1;
        DRV_CHIP_WRITE_HSS(hss_type, lchip, hss_id, address, value);

        address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x1f);
        value = 0x03;
        DRV_CHIP_WRITE_HSS(hss_type, lchip, hss_id, address, value);

         /*C1*/
        address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x1e);
        value = p_ffe->coefficient[1];
        value = (~value);
        value &= 0x007f;
        value |= 0x0080;
        value += 1;
        DRV_CHIP_WRITE_HSS(hss_type, lchip, hss_id, address, value);

        address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x1f);
        value = 0x07;
        DRV_CHIP_WRITE_HSS(hss_type, lchip, hss_id, address, value);

         /*C2*/
        address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x1e);
        value = p_ffe->coefficient[2];
        DRV_CHIP_WRITE_HSS(hss_type, lchip, hss_id, address, value);

        address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x1f);
        value = 0x0b;
        DRV_CHIP_WRITE_HSS(hss_type, lchip, hss_id, address, value);

         /*C3*/
        address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x1e);
        value = p_ffe->coefficient[3];
        value = (~value);
        value &= 0x007f;
        value |= 0x0080;
        value += 1;
        DRV_CHIP_WRITE_HSS(hss_type, lchip, hss_id, address, value);

        address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x1f);
        value = 0x13;
        DRV_CHIP_WRITE_HSS(hss_type, lchip, hss_id, address, value);

        /* step 6, cfg Transmit 802.3ap Adaptive Equalization Command Register */
        address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x0e);
        DRV_CHIP_READ_HSS(hss_type, lchip, hss_id, address, &value);
        value |= 0x1000;
        DRV_CHIP_WRITE_HSS(hss_type, lchip, hss_id, address, value);

        /* setp 7, Read Status  */

        /* step 8, write 0 to bits[13:0] 0f the command register to put all of the coefficients in the hold state */
        address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x0e);
        DRV_CHIP_READ_HSS(hss_type, lchip, hss_id, address, &value);
        value &= 0xc000;
        DRV_CHIP_WRITE_HSS(hss_type, lchip, hss_id, address, value);

        /*set bit0=1 3ap mode*/
        _sys_goldengate_datapath_set_cgpcs_rsv_en(lchip, serdes_id, 0, 1);
    }

    return CTC_E_NONE;
}

/* set serdes ffe for traditional sum mode */
int32
sys_goldengate_datapath_set_serdes_ffe_traditional(uint8 lchip, ctc_chip_serdes_ffe_t* p_ffe)
{
    uint8   hss_id    = 0;
    uint8   hss_idx   = 0;
    uint8   lane_id   = 0;
    uint8   serdes_id = 0;
    uint8   internal_lane = 0;
    uint8   hss_type = 0;
    uint32  address = 0;
    uint16  value = 0;
    uint16  coefficient[4] = {0};
    sys_datapath_hss_attribute_t* p_hss = NULL;
    sys_datapath_serdes_info_t* p_serdes = NULL;

    /* debug param */
    CTC_PTR_VALID_CHECK(p_ffe);
    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s\n", __FUNCTION__);

    /* get serdes info by serdes id */
    serdes_id = p_ffe->serdes_id;
    hss_id = SYS_DATAPATH_MAP_SERDES_TO_HSSID(serdes_id);
    hss_idx = SYS_DATAPATH_MAP_SERDES_TO_HSS_INDEX(serdes_id);
    lane_id = SYS_DATAPATH_MAP_SERDES_TO_LANE_ID(serdes_id);

    if(SYS_DATAPATH_SERDES_IS_HSS28G(serdes_id))
    {
        hss_type = SYS_DATAPATH_HSS_TYPE_28G;
    }
    else
    {
        hss_type = SYS_DATAPATH_HSS_TYPE_15G;
    }
    internal_lane = LANE_ID_MAP_INTERAL_LANE_ID(hss_type, lane_id);

    p_hss = ctc_vector_get(p_gg_datapath_master[lchip]->p_hss_vector, hss_idx);
    if (!p_hss)
    {
        return CTC_E_DATAPATH_HSS_NOT_READY;
    }

    p_serdes = &p_hss->serdes_info[lane_id];

    coefficient[0] = p_ffe->coefficient[0];
    coefficient[1] = p_ffe->coefficient[1];
    coefficient[2] = p_ffe->coefficient[2];
    coefficient[3] = p_ffe->coefficient[3];

    /* disable EQMODE */
    CTC_ERROR_RETURN(sys_goldengate_datapath_set_serdes_eqmode(lchip, serdes_id, 0));

    /* pff->mode==0 is typical mode, pff->mode==1 is user define mode */
    /* if is typical mode, c0,c1,c2,c3 value is set by motherboard material and trace length */
    /* TODO, need asic give typical mode cfg value */
    if(0 == p_ffe->mode)
    {
        if (0 == p_ffe->board_material)
        {
            /* for FR4 board */
            if(SYS_DATAPATH_HSS_TYPE_15G == hss_type)
            {
                /* trace length is 0~4inch c0:, c1:, c2:, c3: */
                if (p_ffe->trace_len == 0)
                {
                    coefficient[0] = 0x02;
                    coefficient[1] = 0x19;
                    coefficient[2] = 0x12;
                }
                /* trace length is 4~7inch c0:, c1:, c2:, c3: */
                else if (p_ffe->trace_len == 1)
                {
                    coefficient[0] = 0x02;
                    coefficient[1] = 0x19;
                    coefficient[2] = 0x18;
                }
                /* trace length is 7~10inch c0:, c1:, c2:, c3: */
                else if (p_ffe->trace_len == 2)
                {
                    coefficient[0] = 0x02;
                    coefficient[1] = 0x19;
                    coefficient[2] = 0x16;
                }
            }
            else
            {
                if(p_serdes->mode != CTC_CHIP_SERDES_CG_MODE)
                {
                    if (p_ffe->trace_len == 0)
                    {
                        coefficient[0] = 0x00;
                        coefficient[1] = 0x02;
                        coefficient[2] = 0x21;
                        coefficient[3] = 0x08;
                    }
                    else if (p_ffe->trace_len == 1)
                    {
                        coefficient[0] = 0x00;
                        coefficient[1] = 0x02;
                        coefficient[2] = 0x21;
                        coefficient[3] = 0x08;
                    }
                    else if (p_ffe->trace_len == 2)
                    {
                        coefficient[0] = 0x00;
                        coefficient[1] = 0x02;
                        coefficient[2] = 0x21;
                        coefficient[3] = 0x08;
                    }
                }
                else
                {
                    if (p_ffe->trace_len == 0)
                    {
                        coefficient[0] = 0x00;
                        coefficient[1] = 0x07;
                        coefficient[2] = 0x2d;
                        coefficient[3] = 0x08;
                    }
                    else if (p_ffe->trace_len == 1)
                    {
                        coefficient[0] = 0x00;
                        coefficient[1] = 0x07;
                        coefficient[2] = 0x2d;
                        coefficient[3] = 0x08;
                    }
                    else if (p_ffe->trace_len == 2)
                    {
                        coefficient[0] = 0x00;
                        coefficient[1] = 0x07;
                        coefficient[2] = 0x2d;
                        coefficient[3] = 0x08;
                    }
                }
            }
        }
        else if(1 == p_ffe->board_material)
        {
            /* for M4 board */
            if(SYS_DATAPATH_HSS_TYPE_15G == hss_type)
            {
                /* trace length is 0~4inch c0:, c1:, c2:, c3: */
                if (p_ffe->trace_len == 0)
                {
                    coefficient[0] = 0x00;
                    coefficient[1] = 0x1a;
                    coefficient[2] = 0x0b;
                }
                /* trace length is 4~7inch c0:, c1:, c2:, c3: */
                else if (p_ffe->trace_len == 1)
                {
                    coefficient[0] = 0x00;
                    coefficient[1] = 0x1c;
                    coefficient[2] = 0x0b;
                }
                /* trace length is 7~10inch c0:, c1:, c2:, c3: */
                else if (p_ffe->trace_len == 2)
                {
                    coefficient[0] = 0x02;
                    coefficient[1] = 0x1c;
                    coefficient[2] = 0x0f;
                }
            }
            else
            {
                if(p_serdes->mode != CTC_CHIP_SERDES_CG_MODE)
                {
                    if (p_ffe->trace_len == 0)
                    {
                        coefficient[0] = 0x00;
                        coefficient[1] = 0x01;
                        coefficient[2] = 0x20;
                        coefficient[3] = 0x03;
                    }
                    else if (p_ffe->trace_len == 1)
                    {
                        coefficient[0] = 0x00;
                        coefficient[1] = 0x01;
                        coefficient[2] = 0x20;
                        coefficient[3] = 0x03;
                    }
                    else if (p_ffe->trace_len == 2)
                    {
                        coefficient[0] = 0x00;
                        coefficient[1] = 0x01;
                        coefficient[2] = 0x20;
                        coefficient[3] = 0x03;
                    }
                }
                else
                {
                    if (p_ffe->trace_len == 0)
                    {
                        coefficient[0] = 0x00;
                        coefficient[1] = 0x07;
                        coefficient[2] = 0x2d;
                        coefficient[3] = 0x08;
                    }
                    else if (p_ffe->trace_len == 1)
                    {
                        coefficient[0] = 0x00;
                        coefficient[1] = 0x07;
                        coefficient[2] = 0x2d;
                        coefficient[3] = 0x08;
                    }
                    else if (p_ffe->trace_len == 2)
                    {
                        coefficient[0] = 0x00;
                        coefficient[1] = 0x07;
                        coefficient[2] = 0x2d;
                        coefficient[3] = 0x08;
                    }
                }
            }
        }
        else if(2 == p_ffe->board_material)
        {
            /* for M6 board */
            if(SYS_DATAPATH_HSS_TYPE_15G == hss_type)
            {
                /* trace length is 0~4inch c0:, c1:, c2:, c3: */
                if (p_ffe->trace_len == 0)
                {
                    coefficient[0] = 0x00;
                    coefficient[1] = 0x1b;
                    coefficient[2] = 0x10;
                }
                /* trace length is 4~7inch c0:, c1:, c2:, c3: */
                else if (p_ffe->trace_len == 1)
                {
                    coefficient[0] = 0x00;
                    coefficient[1] = 0x1c;
                    coefficient[2] = 0x10;
                }
                /* trace length is 7~10inch c0:, c1:, c2:, c3: */
                else if (p_ffe->trace_len == 2)
                {
                    coefficient[0] = 0x00;
                    coefficient[1] = 0x1d;
                    coefficient[2] = 0x13;
                }
            }
            else
            {
                if(p_serdes->mode != CTC_CHIP_SERDES_CG_MODE)
                {
                    if (p_ffe->trace_len == 0)
                    {
                        coefficient[0] = 0x00;
                        coefficient[1] = 0x01;
                        coefficient[2] = 0x20;
                        coefficient[3] = 0x03;
                    }
                    else if (p_ffe->trace_len == 1)
                    {
                        coefficient[0] = 0x00;
                        coefficient[1] = 0x01;
                        coefficient[2] = 0x20;
                        coefficient[3] = 0x03;
                    }
                    else if (p_ffe->trace_len == 2)
                    {
                        coefficient[0] = 0x00;
                        coefficient[1] = 0x01;
                        coefficient[2] = 0x20;
                        coefficient[3] = 0x03;
                    }
                }
                else
                {
                    if (p_ffe->trace_len == 0)
                    {
                        coefficient[0] = 0x00;
                        coefficient[1] = 0x03;
                        coefficient[2] = 0x2b;
                        coefficient[3] = 0x0b;
                    }
                    else if (p_ffe->trace_len == 1)
                    {
                        coefficient[0] = 0x00;
                        coefficient[1] = 0x03;
                        coefficient[2] = 0x2b;
                        coefficient[3] = 0x0b;
                    }
                    else if (p_ffe->trace_len == 2)
                    {
                        coefficient[0] = 0x00;
                        coefficient[1] = 0x03;
                        coefficient[2] = 0x2b;
                        coefficient[3] = 0x0b;
                    }
                }
            }
        }
    }

    /* 0.cfg Transmit Tap 0 Coefficient Register, offset 0x08, set NXTT0(bit4:0) or NXTT0(bit6:0)*/
    address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x08);
    DRV_CHIP_READ_HSS(hss_type, lchip, hss_id, address, &value);
    if(SYS_DATAPATH_HSS_TYPE_15G == hss_type)
    {
        value &= 0xffe0;
        value += coefficient[0];
    }
    else
    {
        value &= 0xff80;
        value += coefficient[0];
    }
    DRV_CHIP_WRITE_HSS(hss_type, lchip, hss_id, address, value);

    /* 1.HSS28G cfg Transmit Tap 1 Coefficient Register, offset 0x09, set NXTT1(bit6:0) */
    /*   HSS15G cfg Transmit Amplitude Register, offset 0x0c, set TXPWR(bit5:0) */
    if(SYS_DATAPATH_HSS_TYPE_15G == hss_type)
    {
        address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x09);
        DRV_CHIP_READ_HSS(hss_type, lchip, hss_id, address, &value);
        value &= 0xffc0;
        value += coefficient[1];
    }
    else
    {
        address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x09);
        DRV_CHIP_READ_HSS(hss_type, lchip, hss_id, address, &value);
        value &= 0xff80;
        value += coefficient[1];
    }
    DRV_CHIP_WRITE_HSS(hss_type, lchip, hss_id, address, value);

    /* 2.cfg Transmit Tap 2 Coefficient Register, offset 0x0a, set NXTT2(bit5:0) or NXTT2(bit6:0)*/
    address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x0a);
    DRV_CHIP_READ_HSS(hss_type, lchip, hss_id, address, &value);
    if(SYS_DATAPATH_HSS_TYPE_15G == hss_type)
    {
        value &= 0xffc0;
        value += coefficient[2];
    }
    else
    {
        value &= 0xff80;
        value += coefficient[2];
    }
    DRV_CHIP_WRITE_HSS(hss_type, lchip, hss_id, address, value);

    /* 3.cfg Transmit Tap 3 Coefficient Register, offset 0x0b, set NXTT3(bit6:0), be used for HHS28G */
    if(SYS_DATAPATH_HSS_TYPE_28G == hss_type)
    {
        address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x0b);
        DRV_CHIP_READ_HSS(hss_type, lchip, hss_id, address, &value);
        value &= 0xff80;
        value += coefficient[3];
        DRV_CHIP_WRITE_HSS(hss_type, lchip, hss_id, address, value);

        _sys_goldengate_datapath_set_cgpcs_rsv_en(lchip, serdes_id, 0, 0);
    }

    /* 4.cfg Transmit Coefficient Control Register, offset 0x02, set ALOAD(bit0)*/
    address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x02);
    DRV_CHIP_READ_HSS(hss_type, lchip, hss_id, address, &value);
    value |= 0x01;
    DRV_CHIP_WRITE_HSS(hss_type, lchip, hss_id, address, value);

    return CTC_E_NONE;
}

/* set serdes ffe */
int32
sys_goldengate_datapath_set_serdes_ffe(uint8 lchip, void* p_data)
{
    ctc_chip_serdes_ffe_t* p_ffe = (ctc_chip_serdes_ffe_t*)p_data;

    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s\n", __FUNCTION__);
    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_PARAM,"serdes_id:%d, mode:%d\n", p_ffe->serdes_id, p_ffe->mode);
    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_PARAM,"serdes_id:%d, C0:%d, C1:%d, C2:%d, C3:%d\n", p_ffe->serdes_id, p_ffe->coefficient[0], p_ffe->coefficient[1], p_ffe->coefficient[2], p_ffe->coefficient[3]);

    CTC_PTR_VALID_CHECK(p_ffe);
    if (p_ffe->serdes_id >= SYS_GG_DATAPATH_SERDES_NUM)
    {
        return CTC_E_INVALID_PARAM;
    }

    CTC_ERROR_RETURN(_sys_goldengate_datapath_check_serdes_ffe(lchip, p_ffe));
    if(CTC_CHIP_SERDES_FFE_MODE_3AP == p_ffe->mode)
    {
        CTC_ERROR_RETURN(sys_goldengate_datapath_set_serdes_ffe_3ap(lchip, p_ffe));
    }
    else
    {
        CTC_ERROR_RETURN(sys_goldengate_datapath_set_serdes_ffe_traditional(lchip, p_ffe));
    }

    return CTC_E_NONE;
}

/* get serdes ffe with 3ap mode */
int32
sys_goldengate_datapath_get_serdes_ffe_3ap(uint8 lchip, uint8 serdes_id, uint16* coefficient)
{
    uint8   hss_id    = 0;
    uint8   lane_id   = 0;
    uint8   internal_lane = 0;
    uint8   hss_type = 0;
    uint32  address = 0;
    uint16  value = 0;

    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s\n", __FUNCTION__);
    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_PARAM,"serdes_id:%d\n", serdes_id);

    /* get serdes info by serdes id */
    hss_id = SYS_DATAPATH_MAP_SERDES_TO_HSSID(serdes_id);
    lane_id = SYS_DATAPATH_MAP_SERDES_TO_LANE_ID(serdes_id);

    if(SYS_DATAPATH_SERDES_IS_HSS28G(serdes_id))
    {
        hss_type = SYS_DATAPATH_HSS_TYPE_28G;
    }
    else
    {
        hss_type = SYS_DATAPATH_HSS_TYPE_15G;
    }
    internal_lane = LANE_ID_MAP_INTERAL_LANE_ID(hss_type, lane_id);

    /* get c0 c1 c2 c3 value */
    if(SYS_DATAPATH_HSS_TYPE_15G == hss_type)
    {
        /* c0 */
        address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x18);
        DRV_CHIP_READ_HSS(hss_type, lchip, hss_id, address, &value);
        value &= 0x1f;
        coefficient[0] = (value/2);

        /* c2 */
        address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x1a);
        DRV_CHIP_READ_HSS(hss_type, lchip, hss_id, address, &value);
        value &= 0x3f;
        coefficient[2] = (value/2);

        /* c1 */
        address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x19);
        DRV_CHIP_READ_HSS(hss_type, lchip, hss_id, address, &value);
        value &= 0x3f;
        coefficient[1] = ((value-coefficient[0])-coefficient[2]);
    }
    else if(SYS_DATAPATH_HSS_TYPE_28G == hss_type)
    {
        /* c0 */
        address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x10);
        DRV_CHIP_READ_HSS(hss_type, lchip, hss_id, address, &value);
        value = (~value);
        value += 1;
        value &= 0x7f;
        coefficient[0] = value;

        /* c1 */
        address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x11);
        DRV_CHIP_READ_HSS(hss_type, lchip, hss_id, address, &value);
        value = (~value);
        value += 1;
        value &= 0x7f;
        coefficient[1] = value;

        /* c2 */
        address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x12);
        DRV_CHIP_READ_HSS(hss_type, lchip, hss_id, address, &value);
        value &= 0x7f;
        coefficient[2] = value;

        /* c3 */
        address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x13);
        DRV_CHIP_READ_HSS(hss_type, lchip, hss_id, address, &value);
        value = (~value);
        value += 1;
        value &= 0x7f;
        coefficient[3] = value;
    }

    return CTC_E_NONE;
}

/* get serdes ffe with traditional mode */
int32
sys_goldengate_datapath_get_serdes_ffe_traditional(uint8 lchip, uint8 serdes_id, uint16* coefficient)
{
    uint8   hss_id    = 0;
    uint8   lane_id   = 0;
    uint8   internal_lane = 0;
    uint8   hss_type = 0;
    uint32  address = 0;
    uint16  value = 0;
    ctc_chip_serdes_ffe_t ffe;

    sal_memset(&ffe, 0, sizeof(ctc_chip_serdes_ffe_t));

    /* get serdes info by serdes id */
    hss_id = SYS_DATAPATH_MAP_SERDES_TO_HSSID(serdes_id);
    lane_id = SYS_DATAPATH_MAP_SERDES_TO_LANE_ID(serdes_id);

    if(SYS_DATAPATH_SERDES_IS_HSS28G(serdes_id))
    {
        hss_type = SYS_DATAPATH_HSS_TYPE_28G;
    }
    else
    {
        hss_type = SYS_DATAPATH_HSS_TYPE_15G;
    }
    internal_lane = LANE_ID_MAP_INTERAL_LANE_ID(hss_type, lane_id);

    /* 0.get Transmit Tap 0 Coefficient Register, offset 0x08, set NXTT0(bit4:0) or NXTT0(bit6:0)*/
    address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x08);
    DRV_CHIP_READ_HSS(hss_type, lchip, hss_id, address, &value);
    if(SYS_DATAPATH_HSS_TYPE_15G == hss_type)
    {
        value &= 0x1f;
        coefficient[0] = value;
    }
    else
    {
        value &= 0x7f;
        coefficient[0] = value;
    }

    /* 1.HSS28G get Transmit Tap 1 Coefficient Register, offset 0x09, set NXTT1(bit6:0) */
    /*   HSS15G get Transmit Amplitude Register, offset 0x0c, set TXPWR(bit5:0) */
    if(SYS_DATAPATH_HSS_TYPE_15G == hss_type)
    {
        address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x09);
        DRV_CHIP_READ_HSS(hss_type, lchip, hss_id, address, &value);
        value &= 0x3f;
        coefficient[1] = value;
    }
    else
    {
        address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x09);
        DRV_CHIP_READ_HSS(hss_type, lchip, hss_id, address, &value);
        value &= 0x7f;
        coefficient[1] = value;
    }

    /* 2.get Transmit Tap 2 Coefficient Register, offset 0x0a, set NXTT2(bit5:0) or NXTT2(bit6:0)*/
    address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x0a);
    DRV_CHIP_READ_HSS(hss_type, lchip, hss_id, address, &value);
    if(SYS_DATAPATH_HSS_TYPE_15G == hss_type)
    {
        value &= 0x3f;
        coefficient[2] = value;
    }
    else
    {
        value &= 0x7f;
        coefficient[2] = value;
    }

    /* 3.get Transmit Tap 3 Coefficient Register, offset 0x0b, set NXTT3(bit6:0), be used for HHS28G */
    if(SYS_DATAPATH_HSS_TYPE_28G == hss_type)
    {
        address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x0b);
        DRV_CHIP_READ_HSS(hss_type, lchip, hss_id, address, &value);
        value &= 0x7f;
        coefficient[3] = value;
    }

    return CTC_E_NONE;
}

/* get serdes ffe */
int32
sys_goldengate_datapath_get_serdes_ffe(uint8 lchip, uint8 serdes_id, uint16* coefficient, uint8 mode, uint8* status)
{
    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s\n", __FUNCTION__);
    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_PARAM,"serdes_id:%d, ffe_mode:%d\n", serdes_id, mode);

    if (serdes_id >= SYS_GG_DATAPATH_SERDES_NUM)
    {
        return CTC_E_INVALID_PARAM;
    }

    if(CTC_CHIP_SERDES_FFE_MODE_3AP == mode)
    {
        CTC_ERROR_RETURN(sys_goldengate_datapath_get_serdes_ffe_3ap(lchip, serdes_id, coefficient));
    }
    else
    {
        CTC_ERROR_RETURN(sys_goldengate_datapath_get_serdes_ffe_traditional(lchip, serdes_id, coefficient));
    }

    CTC_ERROR_RETURN(sys_goldengate_datapath_get_serdes_ffe_status(lchip, serdes_id, mode, status));

    return CTC_E_NONE;
}

/* get serdes signal detect */
uint16
sys_goldengate_datapath_get_serdes_sigdet(uint8 lchip, uint16 serdes_id)
{
    uint8   hss_id    = 0;
    uint8   hss_idx   = 0;
    uint8   lane_id   = 0;
    uint8   slice_id  = 0;
    uint8   internal_lane = 0;
    uint8   hss_type  = 0;
    uint32  address   = 0;
    uint16  value     = 0;
    uint32  value_pcs =0;
    uint32  cmd        = 0;
    uint8   index      = 0;
    uint32  tbl_id     = 0;
    uint16  mac_id     = 0;
    uint8   step       = 0;
    sys_datapath_hss_attribute_t* p_hss_vec = NULL;
    sys_datapath_lport_attr_t* p_port_attr = NULL;

    /* get serdes info by serdes id */
    slice_id = (serdes_id >= SERDES_NUM_PER_SLICE)?1:0;
    hss_id = SYS_DATAPATH_MAP_SERDES_TO_HSSID(serdes_id);
    hss_idx = SYS_DATAPATH_MAP_SERDES_TO_HSS_INDEX(serdes_id);
    lane_id = SYS_DATAPATH_MAP_SERDES_TO_LANE_ID(serdes_id);
    if(SYS_DATAPATH_SERDES_IS_HSS28G(serdes_id))
    {
        hss_type = SYS_DATAPATH_HSS_TYPE_28G;
    }
    else
    {
        hss_type = SYS_DATAPATH_HSS_TYPE_15G;
    }
    p_hss_vec = ctc_vector_get(p_gg_datapath_master[lchip]->p_hss_vector, hss_idx);
    if (p_hss_vec == NULL)
    {
        return CTC_E_NOT_INIT;
    }
    p_port_attr = (sys_datapath_lport_attr_t*)(&p_gg_datapath_master[lchip]->port_attr[slice_id][p_hss_vec->serdes_info[lane_id].lport]);

    /* 1. read Receiver Sigdet Control Register, offset 0x27, get NXTT0(bit5) */
    internal_lane = LANE_ID_MAP_INTERAL_LANE_ID(hss_type, lane_id);
    address = DRV_HSS_ADDR_COMBINE(g_gg_hss_rx_addr_map[internal_lane], 0x27);
    DRV_CHIP_READ_HSS(hss_type, lchip, hss_id, address, &value);
    value &= 0x20;

    /* 2.get the force signal detec */
    if(p_port_attr->pcs_mode == CTC_CHIP_SERDES_CG_MODE)
    {
        tbl_id = CgPcsRxLaneCfg0_t + slice_id*2 + ((p_port_attr->mac_id==36)?0:1);
        cmd = DRV_IOR(tbl_id, CgPcsRxLaneCfg0_forceSignalDetect_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value_pcs));
    }
    else
    {
        mac_id = (p_port_attr->mac_id < 40)?p_port_attr->mac_id:(p_port_attr->mac_id-8);
        step = SharedPcsSerdesCfg10_t - SharedPcsSerdesCfg00_t;
        tbl_id = SharedPcsSerdesCfg00_t+ (mac_id+slice_id*48) / 4 + ((mac_id+slice_id*48)%4)*step;
        cmd = DRV_IOR(tbl_id, SharedPcsSerdesCfg00_forceSignalDetect0_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value_pcs));
    }

    return (value_pcs?1:(value?1:0));
}

uint32
sys_goldengate_datapath_get_serdes_no_random_data_flag(uint8 lchip, uint8 serdes_id)
{
    uint16 value = 0;
    uint8 internal_lane = 0;
    uint16 address = 0;
    uint8 hss_id = 0;
    uint8 lane_id = 0;
    uint32 random_data_flag = 0;
    uint8 index = 0;

    hss_id = SYS_DATAPATH_MAP_SERDES_TO_HSSID(serdes_id);
    lane_id = SYS_DATAPATH_MAP_SERDES_TO_LANE_ID(serdes_id);

    if (SYS_DATAPATH_SERDES_IS_HSS28G(serdes_id))
    {
        internal_lane = lane_id;
        /*check loopback internal*/
        address = DRV_HSS_ADDR_COMBINE(g_gg_hss_rx_addr_map[internal_lane], 1);
        drv_goldengate_chip_read_hss28g(lchip, hss_id, address, &value);
        if ((value & 0x60) == 0x60)
        {
            random_data_flag = 0;
            return random_data_flag;
        }

        for (index =0; index<3; index++)
        {
            address = DRV_HSS_ADDR_COMBINE(g_gg_hss_rx_addr_map[internal_lane], 0x8);
            CTC_ERROR_RETURN(drv_goldengate_chip_read_hss28g(lchip, hss_id, address, &value));
            /*bit 2 reset*/
            random_data_flag = CTC_IS_BIT_SET(value, 2);
            if (!random_data_flag)
            {
                break;
            }
        }
    }
    else
    {
        internal_lane = g_gg_hss15g_lane_map[lane_id];
        /*check loopback internal*/
        address = DRV_HSS_ADDR_COMBINE(g_gg_hss_rx_addr_map[internal_lane], 1);
        drv_goldengate_chip_read_hss15g(lchip, hss_id, address, &value);
        if ((value & 0x60) == 0x60)
        {
            random_data_flag = 0;
            return random_data_flag;
        }

        address = DRV_HSS_ADDR_COMBINE(g_gg_hss_rx_addr_map[internal_lane], 0x8);
        CTC_ERROR_RETURN(drv_goldengate_chip_read_hss15g(lchip, hss_id, address, &value));
        /*bit 2 reset*/
        random_data_flag = CTC_IS_BIT_SET(value, 2);
    }

    return random_data_flag;

}

uint32
sys_goldengate_datapath_get_serdes_fec_work_round_flag(uint8 lchip, uint8 serdes_id)
{
    uint16 value = 0;
    uint8 internal_lane = 0;
    uint16 address = 0;
    uint8 hss_id = 0;
    uint8 lane_id = 0;
    uint32 fec_error_flag = 0;
    uint32 vga_error_flag = 0;

    hss_id = SYS_DATAPATH_MAP_SERDES_TO_HSSID(serdes_id);
    lane_id = SYS_DATAPATH_MAP_SERDES_TO_LANE_ID(serdes_id);

    if (SYS_DATAPATH_SERDES_IS_HSS28G(serdes_id))
    {
        internal_lane = lane_id;
        address = DRV_HSS_ADDR_COMBINE(g_gg_hss_rx_addr_map[internal_lane], 0x1e);
        CTC_ERROR_RETURN(drv_goldengate_chip_read_hss28g(lchip, hss_id, address, &value));
        /*bit 2 reset*/
        fec_error_flag = CTC_IS_BIT_SET(value, 4);
        address = DRV_HSS_ADDR_COMBINE(g_gg_hss_rx_addr_map[internal_lane], 0xc);
        CTC_ERROR_RETURN(drv_goldengate_chip_read_hss28g(lchip, hss_id, address, &value));
        /*rx 0xc bit 5 reset*/
        vga_error_flag = CTC_IS_BIT_SET(value, 5);
    }
    else
    {
        internal_lane = g_gg_hss15g_lane_map[lane_id];
        /*check loopback internal*/
        address = DRV_HSS_ADDR_COMBINE(g_gg_hss_rx_addr_map[internal_lane], 0x1e);
        CTC_ERROR_RETURN(drv_goldengate_chip_read_hss15g(lchip, hss_id, address, &value));
        /*bit 2 reset*/
        fec_error_flag = CTC_IS_BIT_SET(value, 4);
        address = DRV_HSS_ADDR_COMBINE(g_gg_hss_rx_addr_map[internal_lane], 0xc);
        CTC_ERROR_RETURN(drv_goldengate_chip_read_hss28g(lchip, hss_id, address, &value));
        /*rx 0xc bit 5 reset*/
        vga_error_flag = CTC_IS_BIT_SET(value, 5);
    }

    return ((fec_error_flag == 0) || (vga_error_flag == 1));
}

uint32
sys_goldengate_datapath_get_serdes_aec_lock(uint8 lchip, uint8 serdes_id)
{
    uint16 value = 0;
    uint8 hss_id = 0;
    uint8 lane_id = 0;
    uint32 aec_lck = 0;

    hss_id = SYS_DATAPATH_MAP_SERDES_TO_HSSID(serdes_id);
    lane_id = SYS_DATAPATH_MAP_SERDES_TO_LANE_ID(serdes_id);

    /*check loopback internal*/
    drv_goldengate_chip_read_hss28g_aec_aet(lchip, hss_id, lane_id, 0, 0x6, &value);

    /*bit 5 reset*/
    aec_lck = CTC_IS_BIT_SET(value, 5);

    return aec_lck;
}

int16
sys_goldengate_datapath_set_serdes_aec_aet_done(uint8 lchip, uint16 serdes_id)
{
    uint8   hss_id    = 0;
    uint8   hss_idx   = 0;
    uint8   lane_id   = 0;
    uint8   internal_lane = 0;
    uint8   hss_type = 0;
    uint32  address = 0;
    uint16  value = 0;
    uint32  tmp_val = 0;
    uint32  cmd = 0;
    uint32  tb_id = 0;
    uint32  fld_id = 0;
    uint32  train_ok = 0;
    sys_datapath_hss_attribute_t* p_hss_vec = NULL;
    sys_datapath_serdes_info_t* p_serdes    = NULL;
    CsClkTreeCfg0_m clk_tree;
    CsMacroCfg0_m cs_macro;

    ctc_chip_device_info_t dev_info;
    sal_memset(&dev_info, 0, sizeof(ctc_chip_device_info_t));
    sys_goldengate_chip_get_device_info(lchip, &dev_info);

    /* get serdes info by serdes id */
    hss_id = SYS_DATAPATH_MAP_SERDES_TO_HSSID(serdes_id);
    hss_idx = SYS_DATAPATH_MAP_SERDES_TO_HSS_INDEX(serdes_id);
    lane_id = SYS_DATAPATH_MAP_SERDES_TO_LANE_ID(serdes_id);

    if(SYS_DATAPATH_SERDES_IS_HSS28G(serdes_id))
    {
        hss_type = SYS_DATAPATH_HSS_TYPE_28G;
    }
    else
    {
        hss_type = SYS_DATAPATH_HSS_TYPE_15G;
    }
    internal_lane = LANE_ID_MAP_INTERAL_LANE_ID(hss_type, lane_id);

    /* get serdes info from sw table */
    p_hss_vec = ctc_vector_get(p_gg_datapath_master[lchip]->p_hss_vector, hss_idx);
    if (p_hss_vec == NULL)
    {
        return CTC_E_NOT_INIT;
    }
    p_serdes = &p_hss_vec->serdes_info[lane_id];

    /* get training resule */
    if(SYS_DATAPATH_HSS_TYPE_15G == hss_type)
    {
        tb_id = HsMacroMon0_t + p_hss_vec->hss_id;
        fld_id = HsMacroMon0_monATrainOk_f + lane_id*5;
        cmd = DRV_IOR(tb_id, fld_id);
        DRV_IOCTL(lchip, 0, cmd, &train_ok);
    }
    else
    {
        tb_id = CsMacroMon0_t + p_hss_vec->hss_id;
        fld_id = CsMacroMon0_monATrainOk_f + lane_id*5;
        cmd = DRV_IOR(tb_id, fld_id);
        DRV_IOCTL(lchip, 0, cmd, &train_ok);
    }


    /* check chip version */
    if (dev_info.version_id <= 1)
    {
        /* this cfg is used for HSS28G xfi mode, if training_ok, recover HSS28G xfi bit-width cfg */
        if(train_ok && (SYS_DATAPATH_HSS_TYPE_28G == hss_type) && ((CTC_CHIP_SERDES_XFI_MODE == p_serdes->mode)||(CTC_CHIP_SERDES_XLG_MODE == p_serdes->mode)))
        {
            /* 1. cfg clock */
            tb_id = CsClkTreeCfg0_t + p_hss_vec->hss_id;
            cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &clk_tree);

            if ((p_serdes->pll_sel == SYS_DATAPATH_PLL_SEL_PLLB) && (SYS_DATAPATH_CLKTREE_DIV_USELESS == p_hss_vec->core_div_b[1]))
            {
                fld_id = CsClkTreeCfg0_cfgClockHssTxACoreSB0IntSel_f + 5*lane_id;
                tmp_val = 1;
                DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &clk_tree);
            }

            if ((p_serdes->pll_sel == SYS_DATAPATH_PLL_SEL_PLLA) && (SYS_DATAPATH_CLKTREE_DIV_USELESS == p_hss_vec->core_div_a[1]))
            {
                fld_id = CsClkTreeCfg0_cfgClockHssTxACoreSA0IntSel_f + 5*lane_id;
                tmp_val = 1;
                DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &clk_tree);
            }

            /* select core clock source, plla using ACore, pllb using BCore */
            tmp_val = (p_serdes->pll_sel == SYS_DATAPATH_PLL_SEL_PLLB)?1:0;
            fld_id = CsClkTreeCfg0_cfgClockHssTxACoreS1IntSel_f + 5*lane_id;
            DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &clk_tree);

            cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &clk_tree);

            /* 2. cfg serdes bit-width */
            /* cfg tx, offset 0x00, bit[3:2]=1 */
            address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x00);
            DRV_CHIP_READ_HSS(hss_type, lchip, hss_id, address, &value);
            value &= 0xfff3;
            value |= 0x2004;
            DRV_CHIP_WRITE_HSS(hss_type, lchip, hss_id, address, value);

            /* cfg rx, offset 0x00, bit[3:2]=1 */
            address = DRV_HSS_ADDR_COMBINE(g_gg_hss_rx_addr_map[internal_lane], 0x00);
            DRV_CHIP_READ_HSS(hss_type, lchip, hss_id, address, &value);
            value &= 0xfff3;
            value |= 0x2004;
            DRV_CHIP_WRITE_HSS(hss_type, lchip, hss_id, address, value);

            /* 3.CFG CsMacroCfg for HSS28G */
            tb_id = CsMacroCfg0_t + p_hss_vec->hss_id;
            cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &cs_macro);

            tmp_val = 0;
            fld_id = CsMacroCfg0_cfgHssTxADataCgmacSel_f + lane_id*3;
            DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &cs_macro);

            tmp_val = 1;
            fld_id = CsMacroCfg0_cfgAecTxAByp_f + lane_id;
            DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &cs_macro);

            cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &cs_macro);

        }
    }

    return train_ok;
}


/* NOTE: this func is only used for 802.3ap training, GG chip version1 HSS28G 10.3125G 3ap training should modify bit width 20 to 40 */
STATIC int16
_sys_goldengate_datapath_set_hss28g_serdes_width(uint8 lchip, uint8 hss_id, sys_datapath_hss_attribute_t* p_hss_vec, uint8 lane_id, bool enable)
{
    uint32  address = 0;
    uint16  value = 0;
    uint32  tmp_val = 0;
    uint32  cmd = 0;
    uint32  tb_id = 0;
    uint32  fld_id = 0;
    uint8   internal_lane = 0;
    sys_datapath_serdes_info_t* p_serdes    = NULL;
    CsClkTreeCfg0_m clk_tree;

    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s\n", __FUNCTION__);
    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_PARAM,"hss_id:%d, lane_id:%d, enable_value:%d\n", hss_id, lane_id, enable);

    internal_lane = lane_id;
    p_serdes = &p_hss_vec->serdes_info[lane_id];

     /* 1.1 cfg serdes bit-width */
    address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x00); /* cfg tx, offset 0x00, bit[3:2]=3 */
    drv_goldengate_chip_read_hss28g(lchip, hss_id, address, &value);
    if(enable)
    {
        value &= 0xfff3;
        value |= 0x200c;
    }
    else
    {
        value &= 0xfff0;
        value |= 0x2005;
    }
    drv_goldengate_chip_write_hss28g(lchip, hss_id, address, value);

    address = DRV_HSS_ADDR_COMBINE(g_gg_hss_rx_addr_map[internal_lane], 0x00); /* cfg rx, offset 0x00, bit[3:2]=3 */
    drv_goldengate_chip_read_hss28g(lchip, hss_id, address, &value);
    if(enable)
    {
        value &= 0xfff3;
        value |= 0x200c;
    }
    else
    {
        value &= 0xfff0;
        value |= 0x2005;
    }
    drv_goldengate_chip_write_hss28g(lchip, hss_id, address, value);

    /* 1.2 cfg clock */
    tb_id = CsClkTreeCfg0_t + p_hss_vec->hss_id;
    cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &clk_tree);
    if(enable)
    {
        if (p_serdes->pll_sel == SYS_DATAPATH_PLL_SEL_PLLB)
        {
            if (SYS_DATAPATH_CLKTREE_DIV_USELESS != p_hss_vec->core_div_b[1])
            {
                return CTC_E_SERDES_PLL_NOT_SUPPORT;
            }
            else
            {
                tmp_val = SYS_DATAPATH_GET_CLKTREE_DIV(SYS_DATAPATH_CLKTREE_DIV_HALF);
                DRV_IOW_FIELD(tb_id, CsClkTreeCfg0_cfgClkDividerHssTxClk40B1Core_f, &tmp_val, &clk_tree);

                fld_id = CsClkTreeCfg0_cfgClockHssTxACoreSB0IntSel_f + 5*lane_id;
                tmp_val = 0;
                DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &clk_tree);
                cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
                DRV_IOCTL(lchip, 0, cmd, &clk_tree);

                tmp_val = 0; /* release */
                DRV_IOW_FIELD(tb_id, CsClkTreeCfg0_cfgClkResetHssTxClk40B1Core_f, &tmp_val, &clk_tree);
                cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
                DRV_IOCTL(lchip, 0, cmd, &clk_tree);
            }
        }
        else if (p_serdes->pll_sel == SYS_DATAPATH_PLL_SEL_PLLA)
        {
            if (SYS_DATAPATH_CLKTREE_DIV_USELESS != p_hss_vec->core_div_a[1])
            {
                return CTC_E_SERDES_PLL_NOT_SUPPORT;
            }
            else
            {
                tmp_val = SYS_DATAPATH_GET_CLKTREE_DIV(SYS_DATAPATH_CLKTREE_DIV_HALF);
                DRV_IOW_FIELD(tb_id, CsClkTreeCfg0_cfgClkDividerHssTxClk40A1Core_f, &tmp_val, &clk_tree);

                fld_id = CsClkTreeCfg0_cfgClockHssTxACoreSA0IntSel_f + 5*lane_id;
                tmp_val = 0;
                DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &clk_tree);
                cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
                DRV_IOCTL(lchip, 0, cmd, &clk_tree);

                tmp_val = 0; /* release */
                DRV_IOW_FIELD(tb_id, CsClkTreeCfg0_cfgClkResetHssTxClk40A1Core_f, &tmp_val, &clk_tree);
                cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
                DRV_IOCTL(lchip, 0, cmd, &clk_tree);
            }
        }
    }
    else
    {
        if ((p_serdes->pll_sel == SYS_DATAPATH_PLL_SEL_PLLB) && (SYS_DATAPATH_CLKTREE_DIV_USELESS == p_hss_vec->core_div_b[1]))
        {
            tmp_val = 1; /* reset */
            DRV_IOW_FIELD(tb_id, CsClkTreeCfg0_cfgClkResetHssTxClk40B1Core_f, &tmp_val, &clk_tree);
            cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &clk_tree);

            fld_id = CsClkTreeCfg0_cfgClockHssTxACoreSB0IntSel_f + 5*lane_id;
            tmp_val = 1;
            DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &clk_tree);
            cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &clk_tree);
        }

        if ((p_serdes->pll_sel == SYS_DATAPATH_PLL_SEL_PLLA) && (SYS_DATAPATH_CLKTREE_DIV_USELESS == p_hss_vec->core_div_a[1]))
        {
            tmp_val = 1; /* reset */
            DRV_IOW_FIELD(tb_id, CsClkTreeCfg0_cfgClkResetHssTxClk40A1Core_f, &tmp_val, &clk_tree);
            cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &clk_tree);

            fld_id = CsClkTreeCfg0_cfgClockHssTxACoreSA0IntSel_f + 5*lane_id;
            tmp_val = 1;
            DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &clk_tree);
            cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &clk_tree);
        }
    }

    return CTC_E_NONE;
}



int16
sys_goldengate_datapath_hss15g_serdes_aec_aet(uint8 lchip, uint16 cfg_flag, uint8 hss_id, uint8 lane_id, sys_datapath_hss_attribute_t* p_hss_vec, bool enable, uint16 serdes_id)
{
    uint32  address = 0;
    uint16  value = 0;
    uint32  tmp_val = 0;
    uint32  cmd = 0;
    uint32  tb_id = 0;
    uint32  fld_id = 0;
    uint8   internal_lane = 0;
    sys_datapath_serdes_info_t* p_serdes    = NULL;
    HsMacroCfg0_m hs_macro;
    HsCfg0_m hs_cfg;

    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s\n", __FUNCTION__);
    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_PARAM,"hss_id:%d, lane_id:%d, enable_value:%d\n", hss_id, lane_id, enable);

    internal_lane = g_gg_hss15g_lane_map[lane_id];
    p_serdes = &p_hss_vec->serdes_info[lane_id];

    if((CTC_CHIP_SERDES_XFI_MODE == p_serdes->mode) || (CTC_CHIP_SERDES_XLG_MODE == p_serdes->mode))
    {
        /* 802.3ap enable */
        if(SYS_PORT_3AP_TRAINING_EN == cfg_flag)
        {
            /* cfg serdes limit */
            address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x1e);
            value = 0x3c;
            drv_goldengate_chip_write_hss15g(lchip, hss_id, address, value);
            address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x1f);
            value = 0xf;
            drv_goldengate_chip_write_hss15g(lchip, hss_id, address, value);

            address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x1e);
            value = 0x00;
            drv_goldengate_chip_write_hss15g(lchip, hss_id, address, value);
            address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x1f);
            value = 0x11;
            drv_goldengate_chip_write_hss15g(lchip, hss_id, address, value);

            address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x1e);
            value = 0x11;
            drv_goldengate_chip_write_hss15g(lchip, hss_id, address, value);
            address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x1f);
            value = 0x5;
            drv_goldengate_chip_write_hss15g(lchip, hss_id, address, value);

            address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x1e);
            value = 0x3c0d;
            drv_goldengate_chip_write_hss15g(lchip, hss_id, address, value);
            address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x1f);
            value = 0x9;
            drv_goldengate_chip_write_hss15g(lchip, hss_id, address, value);

            address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x1e);
            value = 0x21;
            drv_goldengate_chip_write_hss15g(lchip, hss_id, address, value);
            address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x1f);
            value = 0xd;
            drv_goldengate_chip_write_hss15g(lchip, hss_id, address, value);

            /* 1 cfg serdes lanes tx EQMODE, AESRC and P/N swap */
            address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x02);
            if(enable)
            {
                value = (p_serdes->tx_polarity)?(0x0270):(0x0230);
            }
            else
            {
                drv_goldengate_chip_read_hss15g(lchip, hss_id, address, &value);
                value &= 0xffcf;
                value |= 0x01;
            }
            drv_goldengate_chip_write_hss15g(lchip, hss_id, address, value);

            /* 2 cfg serdes lanes rx SPI enable, cfg DFE Control Register, offset 0x08, value = 0x2082 */
            address = DRV_HSS_ADDR_COMBINE(g_gg_hss_rx_addr_map[internal_lane], 0x08);
            drv_goldengate_chip_read_hss15g(lchip, hss_id, address, &value);
            if(enable)
            {
                value |= 0x2000;
            }
            else
            {
                value &= 0xdfff;
            }
            drv_goldengate_chip_write_hss15g(lchip, hss_id, address, value);

            /* 3. CFG HsMacroCfg forHSS15G */
            tb_id = HsMacroCfg0_t + p_hss_vec->hss_id;
            cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &hs_macro);
            if(enable)
            {
                /*  release HssRx/HssTx/Aec/Aet reset */
                tmp_val = 0;
                fld_id = HsMacroCfg0_cfgResetHssRxA_f + lane_id;
                DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &hs_macro);

                fld_id = HsMacroCfg0_cfgResetHssTxA_f + lane_id;
                DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &hs_macro);

                fld_id = HsMacroCfg0_cfgResetAecA_f + lane_id;
                DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &hs_macro);

                fld_id = HsMacroCfg0_cfgResetAetA_f + lane_id;
                DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &hs_macro);
                /* disable Aec Tx bypass */
                fld_id = HsMacroCfg0_cfgAecTxAByp_f + lane_id;
                DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &hs_macro);
            }
            else
            {
                tmp_val = 1;
                fld_id = HsMacroCfg0_cfgResetHssRxA_f + lane_id;
                DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &hs_macro);

                fld_id = HsMacroCfg0_cfgResetHssTxA_f + lane_id;
                DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &hs_macro);

                fld_id = HsMacroCfg0_cfgResetAecA_f + lane_id;
                DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &hs_macro);

                fld_id = HsMacroCfg0_cfgResetAetA_f + lane_id;
                DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &hs_macro);

                fld_id = HsMacroCfg0_cfgAecTxAByp_f + lane_id;
                DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &hs_macro);
            }
            cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &hs_macro);
        }

        /* 802.3ap init */
        if(SYS_PORT_3AP_TRAINING_INIT == cfg_flag)
        {
            /* add for HsCfg */
            tb_id = HsCfg0_t + p_hss_vec->hss_id;
            cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &hs_cfg);

            tmp_val = (enable)?2:0;
            fld_id = HsCfg0_cfgLaneAResv_f + lane_id;
            DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &hs_cfg);

            cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &hs_cfg);


            /* 1. CFG AET AEC REG */
            address = 0x00;
            value = 0xb0;
            drv_goldengate_chip_write_hss15g_aec_aet(lchip, hss_id, lane_id, 1, address, value);

            address = 0x01;
            value = 0x3000;
            drv_goldengate_chip_write_hss15g_aec_aet(lchip, hss_id, lane_id, 1, address, value);

            address = 0x02;
            value = 0x08c4;
            drv_goldengate_chip_write_hss15g_aec_aet(lchip, hss_id, lane_id, 1, address, value);

            address = 0x03;
            value = 0x00;
            drv_goldengate_chip_write_hss15g_aec_aet(lchip, hss_id, lane_id, 1, address, value);

            /* do DFE reset relese */
            CTC_ERROR_RETURN(sys_goldengate_datapath_set_dfe_reset(lchip, serdes_id, 1));
            CTC_ERROR_RETURN(sys_goldengate_datapath_set_dfe_reset(lchip, serdes_id, 0));

            address = 0x04;         /* cfg REG_MODE */
            value = (enable)?0x0044:0x00;
            drv_goldengate_chip_write_hss15g_aec_aet(lchip, hss_id, lane_id, 0, address, value);

            address = 0x06;        /* cfg FSM_CTL */
            value = (enable)?0x8003:0x00;
            drv_goldengate_chip_write_hss15g_aec_aet(lchip, hss_id, lane_id, 0, address, value);
        }
    }
    else
    {
        return CTC_E_SERDES_MODE_NOT_SUPPORT;
    }

    return CTC_E_NONE;
}

int16
sys_goldengate_datapath_hss28g_serdes_aec_aet(uint8 lchip, uint16 cfg_flag, uint8 hss_id, uint8 lane_id, sys_datapath_hss_attribute_t* p_hss_vec, bool enable, uint16 serdes_id, uint32 is_normal)
{
    uint32  address = 0;
    uint16  value = 0;
    uint32  tmp_val = 0;
    uint32  cmd = 0;
    uint32  tb_id = 0;
    uint32  fld_id = 0;
    uint8   internal_lane = 0;
    sys_datapath_serdes_info_t* p_serdes    = NULL;
    CsMacroCfg0_m cs_macro;
    CsCfg0_m cs_cfg;
    uint32 is_3ap = 0;

    ctc_chip_device_info_t dev_info;
    sal_memset(&dev_info, 0, sizeof(ctc_chip_device_info_t));

    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s\n", __FUNCTION__);
    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_PARAM,"hss_id:%d, lane_id:%d, enable_value:%d\n", hss_id, lane_id, enable);

    sys_goldengate_chip_get_device_info(lchip, &dev_info);
    internal_lane = lane_id;
    p_serdes = &p_hss_vec->serdes_info[lane_id];

    if((CTC_CHIP_SERDES_XFI_MODE == p_serdes->mode) || (CTC_CHIP_SERDES_CG_MODE == p_serdes->mode) || (CTC_CHIP_SERDES_XLG_MODE == p_serdes->mode))
    {
        if(SYS_PORT_3AP_TRAINING_EN == cfg_flag)
        {
            if (is_normal)
            {
                /* cfg serdes limit */
                address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x1e);
                value = 0x40;
                drv_goldengate_chip_write_hss28g(lchip, hss_id, address, value);
                address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x1f);
                value = 0xf;
                drv_goldengate_chip_write_hss28g(lchip, hss_id, address, value);

                address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x1e);
                value = 0x0;
                drv_goldengate_chip_write_hss28g(lchip, hss_id, address, value);
                address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x1f);
                value = 0x11;
                drv_goldengate_chip_write_hss28g(lchip, hss_id, address, value);

                address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x1e);
                value = 0x16;
                drv_goldengate_chip_write_hss28g(lchip, hss_id, address, value);
                address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x1f);
                value = 0x5;
                drv_goldengate_chip_write_hss28g(lchip, hss_id, address, value);

                address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x1e);
                value = 0x4000;
                drv_goldengate_chip_write_hss28g(lchip, hss_id, address, value);
                address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x1f);
                value = 0xd;
                drv_goldengate_chip_write_hss28g(lchip, hss_id, address, value);
            }
            /* 1.HSS28G: cfg serdes lanes rx DFE reset */
             /*CTC_ERROR_RETURN(sys_goldengate_datapath_set_dfe_reset(lchip, serdes_id, 1));*/

            /* 2.cfg serdes lanes tx EQMODE, AESRC and P/N swap */
            address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x02);
            if(enable)
            {
                value = (p_serdes->tx_polarity)?(0x0070):(0x0030);
            }
            else
            {
                drv_goldengate_chip_read_hss28g(lchip, hss_id, address, &value);
                _sys_goldengate_datapath_get_cgpcs_rsv_en(lchip, serdes_id, 0, &is_3ap);
                if (is_3ap)
                {
                    value &= 0xffdf;
                }
                else
                {
                    value &= 0xffcf;
                    value |= 0x01;
                }
            }
            drv_goldengate_chip_write_hss28g(lchip, hss_id, address, value);

            /* 3.cfg serdes lanes rx SPI enable, cfg DFE Control Register, offset 0x08, value = 0x2082 */
            address = DRV_HSS_ADDR_COMBINE(g_gg_hss_rx_addr_map[internal_lane], 0x08);
            drv_goldengate_chip_read_hss28g(lchip, hss_id, address, &value);
            if(enable)
            {
                value |= 0x2000;
                value &= 0xfffe;
            }
            else
            {
                value &= 0xdffe;
            }
            drv_goldengate_chip_write_hss28g(lchip, hss_id, address, value);

        }

        if(SYS_PORT_3AP_TRAINING_INIT == cfg_flag)
        {
            /* check chip version */
            if (dev_info.version_id <= 1)
            {
                if(CTC_CHIP_SERDES_XFI_MODE == p_serdes->mode || CTC_CHIP_SERDES_XLG_MODE == p_serdes->mode )
                {
                    CTC_ERROR_RETURN(_sys_goldengate_datapath_set_hss28g_serdes_width(lchip, hss_id, p_hss_vec, lane_id, enable));
                }
            }

            /* 2.CFG HsMacroCfg forHSS15G or CsMacroCfg for HSS28G */
            tb_id = CsMacroCfg0_t + p_hss_vec->hss_id;
            cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &cs_macro);
            if(enable)
            {
                tmp_val = 0;
                fld_id = CsMacroCfg0_cfgResetHssRxA_f + lane_id;
                DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &cs_macro);

                fld_id = CsMacroCfg0_cfgResetHssTxA_f + lane_id;
                DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &cs_macro);

                if (dev_info.version_id <= 1)
                {
                    if((CTC_CHIP_SERDES_XFI_MODE == p_serdes->mode) || (CTC_CHIP_SERDES_XLG_MODE == p_serdes->mode))
                    {
                        tmp_val = 1;
                        fld_id = CsMacroCfg0_cfgHssTxADataCgmacSel_f + lane_id*3;
                        DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &cs_macro);
                    }
                }

                tmp_val = 0;
                fld_id = CsMacroCfg0_cfgResetAecA_f + lane_id;
                DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &cs_macro);

                fld_id = CsMacroCfg0_cfgResetAetA_f + lane_id;
                DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &cs_macro);

                /* disable Aec Tx bypass, select pma tx/rx 40bit */
                fld_id = CsMacroCfg0_cfgAecTxAByp_f + lane_id;
                DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &cs_macro);

                /* check chip version */
                if (dev_info.version_id <= 1)
                {
                    if(CTC_CHIP_SERDES_XFI_MODE == p_serdes->mode || CTC_CHIP_SERDES_XLG_MODE == p_serdes->mode )
                    {
                        tmp_val = 2;
                        fld_id = CsMacroCfg0_cfgPmaRxAWidth_f + lane_id;
                        DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &cs_macro);
                        fld_id = CsMacroCfg0_cfgPmaTxAWidth_f + lane_id;
                        DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &cs_macro);
                    }
                }

                if(CTC_CHIP_SERDES_CG_MODE == p_serdes->mode)
                {
                    tmp_val = 2;
                    fld_id = CsMacroCfg0_cfgPmaRxAWidth_f + lane_id;
                    DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &cs_macro);
                    fld_id = CsMacroCfg0_cfgPmaTxAWidth_f + lane_id;
                    DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &cs_macro);
                }
            }
            else
            {
                tmp_val = 1;
                fld_id = CsMacroCfg0_cfgResetHssRxA_f + lane_id;
                DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &cs_macro);

                fld_id = CsMacroCfg0_cfgResetHssTxA_f + lane_id;
                DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &cs_macro);

                fld_id = CsMacroCfg0_cfgResetAecA_f + lane_id;
                DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &cs_macro);

                if (dev_info.version_id <= 1)
                {
                    if((CTC_CHIP_SERDES_XFI_MODE == p_serdes->mode) || (CTC_CHIP_SERDES_XLG_MODE == p_serdes->mode))
                    {
                        tmp_val = 0;
                        fld_id = CsMacroCfg0_cfgHssTxADataCgmacSel_f + lane_id*3;
                        DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &cs_macro);
                    }
                }

                tmp_val = 1;
                fld_id = CsMacroCfg0_cfgResetAetA_f + lane_id;
                DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &cs_macro);

                fld_id = CsMacroCfg0_cfgAecTxAByp_f + lane_id;
                DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &cs_macro);

                /* check chip version */
                if (dev_info.version_id <= 1)
                {
                    if((CTC_CHIP_SERDES_XFI_MODE == p_serdes->mode) || (CTC_CHIP_SERDES_XLG_MODE == p_serdes->mode))
                    {
                        fld_id = CsMacroCfg0_cfgPmaRxAWidth_f + lane_id;
                        DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &cs_macro);
                        fld_id = CsMacroCfg0_cfgPmaTxAWidth_f + lane_id;
                        DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &cs_macro);
                    }
                }
                else
                {
                        tmp_val = 1;
                        fld_id = CsMacroCfg0_cfgPmaRxAWidth_f + lane_id;
                        DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &cs_macro);
                        fld_id = CsMacroCfg0_cfgPmaTxAWidth_f + lane_id;
                        DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &cs_macro);
                }
            }

            cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &cs_macro);

            /* add for CsCfg */
            tb_id = CsCfg0_t + p_hss_vec->hss_id;
            cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &cs_cfg);

            tmp_val = (enable)?2:0;
            fld_id = CsCfg0_cfgLaneAResv_f + lane_id;
            DRV_IOW_FIELD(tb_id, fld_id, &tmp_val, &cs_cfg);

            cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &cs_cfg);

            /* 3. CFG AET REG, only used for HSS28G 25.78125G */
            if(CTC_CHIP_SERDES_CG_MODE == p_serdes->mode)
            {
                address = 0x00;
                value = (enable)?0x00b0:0x00;
                drv_goldengate_chip_write_hss28g_aec_aet(lchip, hss_id, internal_lane, 1, address, value);

                address = 0x01;
                value = (enable)?0x3000:0x00;
                drv_goldengate_chip_write_hss28g_aec_aet(lchip, hss_id, internal_lane, 1, address, value);
            }



            /* 4. CFG AET AEC REG */
            address = 0x00;
            value = 0xb0;
            drv_goldengate_chip_write_hss28g_aec_aet(lchip, hss_id, internal_lane, 1, address, value);

            address = 0x01;
            value = 0x3000;
            drv_goldengate_chip_write_hss28g_aec_aet(lchip, hss_id, internal_lane, 1, address, value);

            address = 0x02;
            value = 0x08c4;
            drv_goldengate_chip_write_hss28g_aec_aet(lchip, hss_id, internal_lane, 1, address, value);

            address = 0x03;
            value = 0x00;
            drv_goldengate_chip_write_hss28g_aec_aet(lchip, hss_id, internal_lane, 1, address, value);

            if (is_normal)
            {

                /* do DFE reset relese */
                CTC_ERROR_RETURN(sys_goldengate_datapath_set_dfe_reset(lchip, serdes_id, 1));
                CTC_ERROR_RETURN(sys_goldengate_datapath_set_dfe_reset(lchip, serdes_id, 0));
                address = 0x04;         /* cfg REG_MODE */
                value = (enable)?0x0044:0x00;
                drv_goldengate_chip_write_hss28g_aec_aet(lchip, hss_id, internal_lane, 0, address, value);

                address = 0x06;        /* cfg FSM_CTL */
                value = (enable)?0x8003:0x00;
                drv_goldengate_chip_write_hss28g_aec_aet(lchip, hss_id, internal_lane, 0, address, value);
            }
            else
            {
                address = 0x04;         /* cfg REG_MODE */
                value = (enable)?0x0044:0x00;
                drv_goldengate_chip_write_hss28g_aec_aet(lchip, hss_id, internal_lane, 0, address, value);

                address = 0x06;        /* cfg FSM_CTL */
                value = (enable)?0x8000:0x00;
                drv_goldengate_chip_write_hss28g_aec_aet(lchip, hss_id, internal_lane, 0, address, value);

                tmp_val = enable?1:0;
                tb_id = CsMacroCfg0_t + p_hss_vec->hss_id;
                switch(lane_id)
                {
                    case 0:
                        cmd = DRV_IOW(tb_id, CsMacroCfg0_cfgAMode16GFc_f);
                        DRV_IOCTL(lchip, 0, cmd, &tmp_val);
                        cmd = DRV_IOW(tb_id, CsMacroCfg0_cfgAMrTrEnable_f);
                        DRV_IOCTL(lchip, 0, cmd, &tmp_val);
                        break;
                    case 1:
                        cmd = DRV_IOW(tb_id, CsMacroCfg0_cfgBMode16GFc_f);
                        DRV_IOCTL(lchip, 0, cmd, &tmp_val);
                        cmd = DRV_IOW(tb_id, CsMacroCfg0_cfgBMrTrEnable_f);
                        DRV_IOCTL(lchip, 0, cmd, &tmp_val);
                        break;
                    case 2:
                        cmd = DRV_IOW(tb_id, CsMacroCfg0_cfgCMode16GFc_f);
                        DRV_IOCTL(lchip, 0, cmd, &tmp_val);
                        cmd = DRV_IOW(tb_id, CsMacroCfg0_cfgCMrTrEnable_f);
                        DRV_IOCTL(lchip, 0, cmd, &tmp_val);
                        break;
                    case 3:
                        cmd = DRV_IOW(tb_id, CsMacroCfg0_cfgDMode16GFc_f);
                        DRV_IOCTL(lchip, 0, cmd, &tmp_val);
                        cmd = DRV_IOW(tb_id, CsMacroCfg0_cfgDMrTrEnable_f);
                        DRV_IOCTL(lchip, 0, cmd, &tmp_val);
                        break;
                }
            }

        }
    }
    else
    {
        return CTC_E_SERDES_MODE_NOT_SUPPORT;
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_datapath_get_gport_with_serdes(uint8 lchip, uint16 serdes_id, uint16* p_gport)
{
    uint8 hss_idx = 0;
    uint16 drv_port = 0;
    uint16 gport = 0;
    uint8 lane_id = 0;
    uint8 slice_id = 0;
    uint8 gchip_id = 0;
    sys_datapath_hss_attribute_t* p_hss_vec = NULL;
    sys_datapath_serdes_info_t* p_serdes = NULL;
    CTC_PTR_VALID_CHECK(p_gport);

    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s\n", __FUNCTION__);
    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_PARAM,"serdes_id:%d\n", serdes_id);

    if (serdes_id >= SYS_GG_DATAPATH_SERDES_NUM)
    {
        return CTC_E_EXCEED_MAX_SIZE;
    }

    hss_idx = SYS_DATAPATH_MAP_SERDES_TO_HSS_INDEX(serdes_id);
    lane_id = SYS_DATAPATH_MAP_SERDES_TO_LANE_ID(serdes_id);
    slice_id = (serdes_id >= SERDES_NUM_PER_SLICE)?1:0;

    p_hss_vec = ctc_vector_get(p_gg_datapath_master[lchip]->p_hss_vector, hss_idx);
    if (p_hss_vec == NULL)
    {
        return CTC_E_NOT_INIT;
    }

    p_serdes = &p_hss_vec->serdes_info[lane_id];
    drv_port = p_serdes->lport+256*slice_id;
    CTC_ERROR_RETURN(sys_goldengate_get_gchip_id(lchip, &gchip_id));
    gport = SYS_MAP_DRV_LPORT_TO_CTC_GPORT(gchip_id, drv_port);

    *p_gport = gport;

    return CTC_E_NONE;
}

/* Support 802.3ap, set aec rx value enable */
int16
sys_goldengate_datapath_set_serdes_aec_rx_en(uint8 lchip, uint16 serdes_id, uint32 enable)
{
    uint8   hss_id    = 0;
    uint8   lane_id   = 0;
    uint8   hss_type  = 0;
    uint16  tbl_id    = 0;
    uint32  cmd       = 0;
    uint32  value     = 0;
    uint16  fld_id    = 0;
    HsMacroCfg0_m    hs_macro;
    CsMacroCfg0_m    cs_macro;

    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s\n", __FUNCTION__);
    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_PARAM,"serdes_id:%d\n", serdes_id);

    /* get serdes info by serdes id */
    hss_id = SYS_DATAPATH_MAP_SERDES_TO_HSSID(serdes_id);
    lane_id = SYS_DATAPATH_MAP_SERDES_TO_LANE_ID(serdes_id);

    if(SYS_DATAPATH_SERDES_IS_HSS28G(serdes_id))
    {
        hss_type = SYS_DATAPATH_HSS_TYPE_28G;
    }
    else
    {
        hss_type = SYS_DATAPATH_HSS_TYPE_15G;
    }

    value = enable?1:0;

    if(SYS_DATAPATH_HSS_TYPE_15G == hss_type)
    {
        /* 1. cfg HsMacroCfg */
        tbl_id = HsMacroCfg0_t + hss_id;
        cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &hs_macro);

        fld_id = HsMacroCfg0_cfgAecRxAByp_f + lane_id;
        DRV_IOW_FIELD(tbl_id, fld_id, &value, &hs_macro);

        cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &hs_macro);
    }
    else if(SYS_DATAPATH_HSS_TYPE_28G == hss_type)
    {
        /* 2. cfg CsMacroCfg */
        tbl_id = CsMacroCfg0_t + hss_id;
        cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &cs_macro);

        fld_id = CsMacroCfg0_cfgAecRxAByp_f + lane_id;
        DRV_IOW_FIELD(tbl_id, fld_id, &value, &hs_macro);

        tbl_id = CsMacroCfg0_t + hss_id;
        cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &cs_macro);
    }

    return CTC_E_NONE;
}

/* Support 802.3ap, auto_neg for Backplane Ethernet */
int16
sys_goldengate_datapath_set_serdes_auto_neg_en(uint8 lchip, uint16 serdes_id, uint32 enable)
{
    uint8   hss_id    = 0;
    uint8   lane_id   = 0;
    uint8   hss_type  = 0;
    uint16  tbl_id    = 0;
    uint32  cmd       = 0;
    uint32  value     = 0;
    uint16  fld_id    = 0;
    HssAnethCfg0_m   auto_neg_cfg;
    HsMacroCfg0_m    hs_macro;
    CsMacroCfg0_m    cs_macro;

    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s\n", __FUNCTION__);
    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_PARAM,"serdes_id:%d, enable:0x%X\n", serdes_id, enable);

    /* get serdes info by serdes id */
    hss_id = SYS_DATAPATH_MAP_SERDES_TO_HSSID(serdes_id);
    lane_id = SYS_DATAPATH_MAP_SERDES_TO_LANE_ID(serdes_id);

    if(SYS_DATAPATH_SERDES_IS_HSS28G(serdes_id))
    {
        hss_type = SYS_DATAPATH_HSS_TYPE_28G;
    }
    else
    {
        hss_type = SYS_DATAPATH_HSS_TYPE_15G;
    }

    /* enable cl73 atuo-neg */
    if(enable)
    {
        if(SYS_DATAPATH_HSS_TYPE_15G == hss_type)
        {
            /* 1. cfg HsMacroCfg */
            tbl_id = HsMacroCfg0_t + hss_id;
            cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &hs_macro);

            value = 0;
            fld_id = HsMacroCfg0_cfgAnethTxAByp_f + lane_id;
            DRV_IOW_FIELD(tbl_id, fld_id, &value, &hs_macro);
            value = 1;
            fld_id = HsMacroCfg0_cfgLaneAPrtReady_f + lane_id;
            DRV_IOW_FIELD(tbl_id, fld_id, &value, &hs_macro);

            cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &hs_macro);
        }
        else if(SYS_DATAPATH_HSS_TYPE_28G == hss_type)
        {
            /* 2. cfg CsMacroCfg */
            tbl_id = CsMacroCfg0_t + hss_id;
            cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &cs_macro);

            value = 0;
            fld_id = CsMacroCfg0_cfgAnethTxAByp_f + lane_id;
            DRV_IOW_FIELD(tbl_id, fld_id, &value, &cs_macro);
            value = 1;
            fld_id = CsMacroCfg0_cfgLaneAPrtReady_f + lane_id;
            DRV_IOW_FIELD(tbl_id, fld_id, &value, &cs_macro);

            tbl_id = CsMacroCfg0_t + hss_id;
            cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &cs_macro);
        }

        /* do DFE reset relese */
        CTC_ERROR_RETURN(sys_goldengate_datapath_set_dfe_reset(lchip, serdes_id, 1));
        CTC_ERROR_RETURN(sys_goldengate_datapath_set_dfe_reset(lchip, serdes_id, 0));

        /* 3. cfg HssAnethCfg */
        tbl_id = HssAnethCfg0_t + serdes_id;
        cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &auto_neg_cfg);

        value = 1;
        fld_id = HssAnethCfg0_mrAutonegEnable_f;
        DRV_IOW_FIELD(tbl_id, fld_id, &value, &auto_neg_cfg);
        value = 0;
        fld_id = HssAnethCfg0_powerOn_f;
        DRV_IOW_FIELD(tbl_id, fld_id, &value, &auto_neg_cfg);
        value = 0;
        fld_id = HssAnethCfg0_mrMainReset_f;
        DRV_IOW_FIELD(tbl_id, fld_id, &value, &auto_neg_cfg);

        cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &auto_neg_cfg);
    }
    else
    {
        if(SYS_DATAPATH_HSS_TYPE_15G == hss_type)
        {
            /* 1. cfg HsMacroCfg */
            tbl_id = HsMacroCfg0_t + hss_id;
            cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &hs_macro);

            value = 1;
            fld_id = HsMacroCfg0_cfgAnethTxAByp_f + lane_id;
            DRV_IOW_FIELD(tbl_id, fld_id, &value, &hs_macro);
            value = 0;
            fld_id = HsMacroCfg0_cfgLaneAPrtReady_f + lane_id;
            DRV_IOW_FIELD(tbl_id, fld_id, &value, &hs_macro);

            cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &hs_macro);
        }
        else if(SYS_DATAPATH_HSS_TYPE_28G == hss_type)
        {
            /* 2. cfg CsMacroCfg */
            tbl_id = CsMacroCfg0_t + hss_id;
            cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &cs_macro);

            value = 1;
            fld_id = CsMacroCfg0_cfgAnethTxAByp_f + lane_id;
            DRV_IOW_FIELD(tbl_id, fld_id, &value, &cs_macro);
            value = 0;
            fld_id = CsMacroCfg0_cfgLaneAPrtReady_f + lane_id;
            DRV_IOW_FIELD(tbl_id, fld_id, &value, &cs_macro);

            tbl_id = CsMacroCfg0_t + hss_id;
            cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &cs_macro);
        }

        /* 3. cfg HssAnethCfg */
        tbl_id = HssAnethCfg0_t + serdes_id;
        cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &auto_neg_cfg);

        value = 0;
        fld_id = HssAnethCfg0_linkStatus_f;
        DRV_IOW_FIELD(tbl_id, fld_id, &value, &auto_neg_cfg);
        fld_id = HssAnethCfg0_mrAutonegEnable_f;
        DRV_IOW_FIELD(tbl_id, fld_id, &value, &auto_neg_cfg);
        value = 1;
        fld_id = HssAnethCfg0_powerOn_f;
        DRV_IOW_FIELD(tbl_id, fld_id, &value, &auto_neg_cfg);
        value = 1;
        fld_id = HssAnethCfg0_mrMainReset_f;
        DRV_IOW_FIELD(tbl_id, fld_id, &value, &auto_neg_cfg);

        cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &auto_neg_cfg);
    }

    return CTC_E_NONE;
}

/* Support 802.3ap, auto_neg for Backplane Ethernet */
int16
sys_goldengate_datapath_set_serdes_auto_neg_ability(uint8 lchip, uint16 serdes_id, uint32 ability)
{
    uint16  tbl_id    = 0;
    uint32  cmd       = 0;
    uint32  value     = 0;
    uint16  fld_id    = 0;
    HssAnethCfg0_m   auto_neg_cfg;

    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s\n", __FUNCTION__);
    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_PARAM,"serdes_id:%d, ability:0x%X\n", serdes_id, ability);

    /* cfg HssAnethCfg */
    tbl_id = HssAnethCfg0_t + serdes_id;
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &auto_neg_cfg);

    /* cfg ability */
    fld_id = HssAnethCfg0_mrAdvAbility_f;
    DRV_IOR_FIELD(tbl_id, fld_id, &value, &auto_neg_cfg);
    value &= 0x1fffff;
    value |= ability;

    DRV_IOW_FIELD(tbl_id, fld_id, &value, &auto_neg_cfg);
    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &auto_neg_cfg);

    return CTC_E_NONE;
}

/* Support 802.3ap, auto_neg for Backplane Ethernet */
int16
sys_goldengate_datapath_get_serdes_auto_neg_local_ability(uint8 lchip, uint16 serdes_id, uint32* p_ability)
{
    uint16  tbl_id    = 0;
    uint32  cmd       = 0;
    uint32  value     = 0;
    uint16  fld_id    = 0;
    HssAnethCfg0_m   auto_neg_cfg;

    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s\n", __FUNCTION__);
    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_PARAM,"serdes_id:%d\n", serdes_id);

    /* get ability */
    tbl_id = HssAnethCfg0_t + serdes_id;
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &auto_neg_cfg);

    fld_id = HssAnethCfg0_mrAdvAbility_f;
    DRV_IOR_FIELD(tbl_id, fld_id, &value, &auto_neg_cfg);
    value &= 0xffe00000;

    *p_ability = value;
    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_PARAM,"serdes %d ability:0x%X\n", serdes_id, *p_ability);

    return CTC_E_NONE;
}

/* Support 802.3ap, auto_neg for Backplane Ethernet */
int16
sys_goldengate_datapath_get_serdes_auto_neg_remote_ability(uint8 lchip, uint16 serdes_id, uint32* p_ability)
{
    uint16  tbl_id    = 0;
    uint32  cmd       = 0;
    uint32  value     = 0;
    uint16  fld_id    = 0;
    HssAnethMon0_m  auto_neg_mon;

    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s\n", __FUNCTION__);
    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_PARAM,"serdes_id:%d\n", serdes_id);

    /* get ability */
    tbl_id = HssAnethMon0_t + serdes_id;
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &auto_neg_mon);

    fld_id = HssAnethMon0_mrLpAdvAbility_f;
    DRV_IOR_FIELD(tbl_id, fld_id, &value, &auto_neg_mon);
    value &= 0xffe00000;

    *p_ability = value;
    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_PARAM,"serdes %d ability:0x%X\n", serdes_id, *p_ability);

    return CTC_E_NONE;
}

/* Enable auto_neg bypass */
int16
sys_goldengate_datapath_set_serdes_auto_neg_bypass_en(uint8 lchip, uint16 serdes_id, uint32 enable)
{
    uint8   hss_id    = 0;
    uint8   lane_id   = 0;
    uint8   hss_type  = 0;
    uint16  tbl_id    = 0;
    uint32  cmd       = 0;
    uint32  value     = 0;
    uint16  fld_id    = 0;
    HsMacroCfg0_m   hs_macro;
    CsMacroCfg0_m   cs_macro;

    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s\n", __FUNCTION__);
    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_PARAM,"serdes_id:%d\n", serdes_id);

    /* get serdes info by serdes id */
    hss_id = SYS_DATAPATH_MAP_SERDES_TO_HSSID(serdes_id);
    lane_id = SYS_DATAPATH_MAP_SERDES_TO_LANE_ID(serdes_id);

    if(SYS_DATAPATH_SERDES_IS_HSS28G(serdes_id))
    {
        hss_type = SYS_DATAPATH_HSS_TYPE_28G;
    }
    else
    {
        hss_type = SYS_DATAPATH_HSS_TYPE_15G;
    }

    value = enable?1:0;
    /* operation */
    if(SYS_DATAPATH_HSS_TYPE_15G == hss_type)
    {
        /* 1. cfg HsMacroCfg */
        tbl_id = HsMacroCfg0_t + hss_id;
        cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &hs_macro);

        fld_id = HsMacroCfg0_cfgAnethTxAByp_f + lane_id;
        DRV_IOW_FIELD(tbl_id, fld_id, &value, &hs_macro);

        cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &hs_macro);
    }
    else if(SYS_DATAPATH_HSS_TYPE_28G == hss_type)
    {
        /* 2. cfg CsMacroCfg */
        tbl_id = CsMacroCfg0_t + hss_id;
        cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &cs_macro);

        fld_id = CsMacroCfg0_cfgAnethTxAByp_f + lane_id;
        DRV_IOW_FIELD(tbl_id, fld_id, &value, &cs_macro);

        tbl_id = CsMacroCfg0_t + hss_id;
        cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &cs_macro);
    }

    return CTC_E_NONE;
}

/* Support 802.3ap, auto link training for Backplane Ethernet */
int16
sys_goldengate_datapath_set_serdes_aec_aet_en(uint8 lchip, uint16 serdes_id, uint16 cfg_flag, bool enable, uint32 is_normal)
{
    uint8   hss_id    = 0;
    uint8   hss_idx   = 0;
    uint8   lane_id   = 0;
    uint8   hss_type = 0;
    sys_datapath_hss_attribute_t* p_hss_vec = NULL;

    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s\n", __FUNCTION__);
    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_PARAM,"serdes_id:%d, enable_value:%d\n", serdes_id, enable);

    /* get serdes info by serdes id */
    hss_id = SYS_DATAPATH_MAP_SERDES_TO_HSSID(serdes_id);
    hss_idx = SYS_DATAPATH_MAP_SERDES_TO_HSS_INDEX(serdes_id);
    lane_id = SYS_DATAPATH_MAP_SERDES_TO_LANE_ID(serdes_id);

    if(SYS_DATAPATH_SERDES_IS_HSS28G(serdes_id))
    {
        hss_type = SYS_DATAPATH_HSS_TYPE_28G;
    }
    else
    {
        hss_type = SYS_DATAPATH_HSS_TYPE_15G;
    }

    /* get serdes info from sw table */
    p_hss_vec = ctc_vector_get(p_gg_datapath_master[lchip]->p_hss_vector, hss_idx);
    if (p_hss_vec == NULL)
    {
        return CTC_E_NOT_INIT;
    }

    /* operation */
    if(SYS_DATAPATH_HSS_TYPE_15G == hss_type)
    {
        CTC_ERROR_RETURN(sys_goldengate_datapath_hss15g_serdes_aec_aet(lchip, cfg_flag, hss_id, lane_id, p_hss_vec, enable, serdes_id));
    }
    else
    {
        CTC_ERROR_RETURN(sys_goldengate_datapath_hss28g_serdes_aec_aet(lchip, cfg_flag, hss_id, lane_id, p_hss_vec, enable, serdes_id, is_normal));
    }

    return CTC_E_NONE;
}


/*return lport is per chip port, not driver port, mac id is per slice*/
uint16
sys_goldengate_datapath_get_lport_with_mac(uint8 lchip, uint8 slice_id, uint8 mac_id)
{
    uint16 lport = 0;
    sys_datapath_lport_attr_t* port_attr = NULL;
    uint16 temp = 0;
    uint8 find_flag = 0;

    for (lport = 0; lport < SYS_CHIP_PER_SLICE_PORT_NUM; lport++)
    {
        port_attr = sys_goldengate_datapath_get_port_capability(lchip, lport, slice_id);
        if (!port_attr)
        {
            continue;
        }

        if (port_attr->port_type == SYS_DATAPATH_RSV_PORT)
        {
            continue;
        }

        if (mac_id == port_attr->mac_id)
        {
            find_flag = 1;
            temp = lport;
            break;
        }
    }

    if (find_flag)
    {
        return temp;
    }
    else
    {
        return SYS_COMMON_USELESS_MAC;
    }

}
/*
    get port attribute, lport is chip port, notice not driver port
*/
void*
sys_goldengate_datapath_get_port_capability(uint8 lchip, uint16 lport, uint8 slice_id)
{
    sys_datapath_lport_attr_t* p_init_port = NULL;

    if (lport >= SYS_CHIP_PER_SLICE_PORT_NUM)
    {
        return NULL;
    }

    if (NULL == p_gg_datapath_master[lchip])
    {
        return NULL;
    }

    p_init_port = (sys_datapath_lport_attr_t*)(&p_gg_datapath_master[lchip]->port_attr[slice_id][lport]);

    return p_init_port;
}

int32
sys_goldengate_datapath_get_serdes_info(uint8 lchip, uint8 serdes_id, sys_datapath_serdes_info_t** p_serdes)
{
    uint16 hss_idx = 0;
    uint8 lane_idx = 0;
    sys_datapath_hss_attribute_t* p_hss = NULL;
    sys_datapath_serdes_info_t* p_serdes_info = NULL;

    hss_idx = SYS_DATAPATH_MAP_SERDES_TO_HSS_INDEX(serdes_id);
    lane_idx = SYS_DATAPATH_MAP_SERDES_TO_LANE_ID(serdes_id);

    p_hss = ctc_vector_get(p_gg_datapath_master[lchip]->p_hss_vector, hss_idx);
    if (NULL == p_hss)
    {
        return CTC_E_MAC_NOT_USED;
    }
    p_serdes_info = &p_hss->serdes_info[lane_idx];

    if (p_serdes_info == NULL)
    {
        return CTC_E_MAC_NOT_USED;
    }

    *p_serdes = p_serdes_info;

    return CTC_E_NONE;
}


int32
sys_goldengate_datapath_get_serdes_with_lport(uint8 lchip, uint16 chip_port, uint8 slice, uint16* p_serdes_id, uint8* num)
{
    uint16 hss_idx = 0;
    uint8 lane_idx = 0;
    uint16 serdes_idx = 0;
    uint8 slice_id = 0;
    sys_datapath_hss_attribute_t* p_hss = NULL;
    sys_datapath_serdes_info_t* p_serdes = NULL;
    uint8 flag = 0;

    for (serdes_idx = 0; serdes_idx < SYS_GG_DATAPATH_SERDES_NUM; serdes_idx++)
    {
        hss_idx = SYS_DATAPATH_MAP_SERDES_TO_HSS_INDEX(serdes_idx);
        lane_idx = SYS_DATAPATH_MAP_SERDES_TO_LANE_ID(serdes_idx);

        p_hss = ctc_vector_get(p_gg_datapath_master[lchip]->p_hss_vector, hss_idx);
        if (NULL == p_hss)
        {
            continue;
        }

        slice_id = (serdes_idx>=48)?1:0;
        p_serdes = &p_hss->serdes_info[lane_idx];

        if ((p_serdes->lport == chip_port) && (slice_id == slice))
        {
            *p_serdes_id = serdes_idx;
            flag = 1;
        }

        if (flag)
        {
            if ((p_serdes->mode == CTC_CHIP_SERDES_XAUI_MODE) || (p_serdes->mode == CTC_CHIP_SERDES_DXAUI_MODE) ||
               (p_serdes->mode == CTC_CHIP_SERDES_XLG_MODE) || (p_serdes->mode == CTC_CHIP_SERDES_CG_MODE))
            {
                *num = 4;
            }
            else
            {
                *num = 1;
            }

            break;
        }

    }


    return (flag?CTC_E_NONE:CTC_E_UNEXPECT);
}

int32
sys_goldengate_datapath_set_serdes_polarity(uint8 lchip, void* p_data)
{
    uint32 address = 0;
    uint8 hss_id = 0;
    uint8 lane_id = 0;
    uint8 internal_lane = 0;
    uint16 value = 0;
    uint8 hss_idx = 0;
    sys_datapath_hss_attribute_t* p_hss = NULL;
    sys_datapath_serdes_info_t* p_serdes = NULL;
    ctc_chip_serdes_polarity_t* p_polarity = (ctc_chip_serdes_polarity_t*)p_data;

 /*return 0;*/
#ifdef  EMULATION_PLATFORM
    return 0;
#endif

    SYS_DATAPATH_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_polarity);

    if ((p_polarity->serdes_id >= SYS_GG_DATAPATH_SERDES_NUM) || (p_polarity->dir > 1) ||(p_polarity->polarity_mode > 1))
    {
        return CTC_E_INVALID_PARAM;
    }

    hss_idx = SYS_DATAPATH_MAP_SERDES_TO_HSS_INDEX(p_polarity->serdes_id);

    p_hss = ctc_vector_get(p_gg_datapath_master[lchip]->p_hss_vector, hss_idx);
    if (NULL == p_hss)
    {
        return CTC_E_NONE;
    }

    hss_id = p_hss->hss_id;
    lane_id = SYS_DATAPATH_MAP_SERDES_TO_LANE_ID(p_polarity->serdes_id);

    if (SYS_DATAPATH_SERDES_IS_HSS28G(p_polarity->serdes_id))
    {
        /*Hss28g*/
        internal_lane = lane_id;

        if (p_polarity->dir)
        {
            /*tx*/
            address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 2);
            CTC_ERROR_RETURN(drv_goldengate_chip_read_hss28g(lchip, hss_id, address, &value));

            /* used for EQMODE enable */
            if (p_polarity->polarity_mode)
            {
                value |= (uint16)(1<<6);
            }
            else
            {
                value &= ~((uint16)(1<<6));
            }
            CTC_ERROR_RETURN(drv_goldengate_chip_write_hss28g(lchip, hss_id, address, value));

            /* used for EQMODE disable */
            /*1. set Transmit Polarity Register */
            address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x0d);
            CTC_ERROR_RETURN(drv_goldengate_chip_read_hss28g(lchip, hss_id, address, &value));
            if (p_polarity->polarity_mode)
            {
                /*set bit0~bit3:0100*/
                value &= 0xfff0;
                value |= 0x4;
            }
            else
            {
                /*set bit0~bit3:1011*/
                value &= 0xfff0;
                value |= 0xb;
            }
            CTC_ERROR_RETURN(drv_goldengate_chip_write_hss28g(lchip, hss_id, address, value));

            /*2. set Transmit Coefficient Control Register for apply */
            address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x02);
            CTC_ERROR_RETURN(drv_goldengate_chip_read_hss28g(lchip, hss_id, address, &value));
            value |= 0x01;
            CTC_ERROR_RETURN(drv_goldengate_chip_write_hss28g(lchip, hss_id, address, value));
        }
        else
        {
            /*rx*/
            address = DRV_HSS_ADDR_COMBINE(g_gg_hss_rx_addr_map[internal_lane], 0x20);
            CTC_ERROR_RETURN(drv_goldengate_chip_read_hss28g(lchip, hss_id, address, &value));
            if (p_polarity->polarity_mode)
            {
                value |= (uint16)(1<<6);
            }
            else
            {
                value &= ~((uint16)(1<<6));
            }
            CTC_ERROR_RETURN(drv_goldengate_chip_write_hss28g(lchip, hss_id, address, value));
        }
    }
    else
    {
        /*Hss15g*/
        internal_lane = g_gg_hss15g_lane_map[lane_id];

        if (p_polarity->dir)
        {
            /*tx*/
            address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 2);
            CTC_ERROR_RETURN(drv_goldengate_chip_read_hss15g(lchip, hss_id, address, &value));

            /* used for EQMODE enable */
            if (p_polarity->polarity_mode)
            {
                value |= (uint16)(1<<6);
            }
            else
            {
                value &= ~((uint16)(1<<6));
            }
            CTC_ERROR_RETURN(drv_goldengate_chip_write_hss15g(lchip, hss_id, address, value));

            /* used for EQMODE enable */
            /*1. set Transmit Polarity Register */
            address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x0d);
            CTC_ERROR_RETURN(drv_goldengate_chip_read_hss15g(lchip, hss_id, address, &value));
           if (p_polarity->polarity_mode)
            {
                /*set bit0~bit2:101*/
                value &= 0xfff8;
                value |= 0x5;
            }
            else
            {
                /*set bit0~bit2:010*/
                value &= 0xfff8;
                value |= 0x2;
            }
            CTC_ERROR_RETURN(drv_goldengate_chip_write_hss15g(lchip, hss_id, address, value));

            /*2. set Transmit Coefficient Control Register for apply */
            address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x02);
            CTC_ERROR_RETURN(drv_goldengate_chip_read_hss15g(lchip, hss_id, address, &value));
            value |= 0x01;
            CTC_ERROR_RETURN(drv_goldengate_chip_write_hss15g(lchip, hss_id, address, value));
        }
        else
        {
            /*rx*/
            address = DRV_HSS_ADDR_COMBINE(g_gg_hss_rx_addr_map[internal_lane], 0x20);
            CTC_ERROR_RETURN(drv_goldengate_chip_read_hss15g(lchip, hss_id, address, &value));

            if (p_polarity->polarity_mode)
            {
                value |= (uint16)(1<<6);
            }
            else
            {
                value &= ~((uint16)(1<<6));
            }

            CTC_ERROR_RETURN(drv_goldengate_chip_write_hss15g(lchip, hss_id, address, value));
        }
    }

    /*update sw*/
    p_serdes = &p_hss->serdes_info[lane_id];
    if (p_polarity->dir)
    {
        p_serdes->tx_polarity =  p_polarity->polarity_mode;
    }
    else
    {
        p_serdes->rx_polarity =  p_polarity->polarity_mode;
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_datapath_get_serdes_polarity(uint8 lchip, void* p_data)
{
    uint8 lane_id = 0;
    uint8 hss_idx = 0;
    sys_datapath_hss_attribute_t* p_hss = NULL;
    sys_datapath_serdes_info_t* p_serdes = NULL;
    ctc_chip_serdes_polarity_t* p_polarity = (ctc_chip_serdes_polarity_t*)p_data;

    SYS_DATAPATH_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_polarity);

    if ((p_polarity->serdes_id >= SYS_GG_DATAPATH_SERDES_NUM) || (p_polarity->dir > 1))
    {
        return CTC_E_INVALID_PARAM;
    }

    hss_idx = SYS_DATAPATH_MAP_SERDES_TO_HSS_INDEX(p_polarity->serdes_id);

    p_hss = ctc_vector_get(p_gg_datapath_master[lchip]->p_hss_vector, hss_idx);
    if (NULL == p_hss)
    {
        return CTC_E_INVALID_PARAM;
    }

    lane_id = SYS_DATAPATH_MAP_SERDES_TO_LANE_ID(p_polarity->serdes_id);

    p_serdes = &p_hss->serdes_info[lane_id];

    if (p_polarity->dir)
    {
        p_polarity->polarity_mode = p_serdes->tx_polarity;
    }
    else
    {
        p_polarity->polarity_mode = p_serdes->rx_polarity;
    }

    return CTC_E_NONE;

}

/*get clock core, core_type:0-plla, 1-pllb*/
uint16
sys_goldengate_datapath_get_core_clock(uint8 lchip, uint8 core_type)
{
    uint16 freq = 0;

    if (NULL == p_gg_datapath_master[lchip])
    {
        return 0;
    }

    if (core_type == 0)
    {
        freq = p_gg_datapath_master[lchip]->core_plla;
    }
    else if (core_type == 1)
    {
        freq = p_gg_datapath_master[lchip]->core_pllb;
    }

    return freq;
}

int32
sys_goldengate_datapath_set_impedance_calibration(uint8 lchip)
{
    uint8 index = 0;
    uint8 slave_index = 0;
    uint8 hssidx[4] = {0,7,5,12};
    uint8 hssnum[4] = {5,5,2,2};
    sys_datapath_hss_attribute_t* p_hss = NULL;
    uint32 value = 0;
    uint16 serdes_value = 0;
    uint32 tb_id = 0;
    uint32 cmd = 0;
    HsCfg0_m hs_cfg;
    HsMacroMiscMon0_m hs_mon;
    CsCfg0_m cs_cfg;
    CsMacroMiscMon0_m cs_mon;
    uint32 timeout = 0x4000;
    uint8 plla_ready = 0;
    uint8 pllb_ready = 0;
    uint8 train_done = 0;
    uint16 address = 0;
    uint8 hss_id = 0;
    uint8 hss_master_init = 0;
    uint8 hss_master_need_init = 0;

    /*For Hss15g Master HssIdx is 0 and 7, For Hss28g Master HssIdx is 5 and 12*/
    for (index = 0; index < 4; index++)
    {
        timeout = 0x4000;
        plla_ready = 0;
        pllb_ready = 0;
        hss_master_init = 0;
        hss_master_need_init = 0;
        train_done = 0;

        p_hss = ctc_vector_get(p_gg_datapath_master[lchip]->p_hss_vector, hssidx[index]);
        if (p_hss)
        {
            /*hss have do init, skip*/
            hss_master_init = 1;
        }

        /*adjust slaver hss */
        for (slave_index = 1; slave_index < hssnum[index]; slave_index++)
        {
            p_hss = ctc_vector_get(p_gg_datapath_master[lchip]->p_hss_vector, hssidx[index]+slave_index);
            if (p_hss)
            {
                break;
            }
        }
        if ((slave_index != hssnum[index]) && (0 == hss_master_init))
        {
            hss_master_need_init = 1;
        }

        /*get hssid by hssidx*/
        hss_id = (0==hssidx[index])?0:
                                ((7==hssidx[index])?5:((5==hssidx[index])?0:2));

        /*release HSSRESET */
        if ((1 == hss_master_need_init) && ((0 == hssidx[index]) ||(7 == hssidx[index])))
        {
            tb_id = HsCfg0_t + hss_id;
            value = 0;
            cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &hs_cfg);
            DRV_IOW_FIELD(tb_id, HsCfg0_cfgHssPdwnPllA_f, &value, &hs_cfg);
            DRV_IOW_FIELD(tb_id, HsCfg0_cfgHssRefClkValidB_f, &value, &hs_cfg);
            cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &hs_cfg);

            value = 1;
            cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &hs_cfg);
            DRV_IOW_FIELD(tb_id, HsCfg0_cfgHssRefClkValidA_f, &value, &hs_cfg);
            cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &hs_cfg);

            value = 0;
            cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &hs_cfg);
            DRV_IOW_FIELD(tb_id, HsCfg0_cfgHssReset_f, &value, &hs_cfg);
            cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &hs_cfg);

            tb_id = HsMacroMiscMon0_t + hss_id;
            cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &hs_mon);

            if (0 == SDK_WORK_PLATFORM)
            {
                /*check HssMonitor*/
                while(--timeout)
                {
                    DRV_IOCTL(lchip, 0, cmd, &hs_mon);
                    if (GetHsMacroMiscMon0(V,monHssPrtReadyA_f, &hs_mon))
                    {
                        plla_ready = 1;
                    }

                    if (GetHsMacroMiscMon0(V,monHssPrtReadyB_f, &hs_mon))
                    {
                        pllb_ready = 1;
                    }

                    if (plla_ready || pllb_ready)
                    {
                        break;
                    }
                }

                if ((0 == plla_ready) && (0 == pllb_ready))
                {
                    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"Impedance Calibration fail! hssidx %d \n", hssidx[index]);
                    return CTC_E_DATAPATH_HSS_NOT_READY;
                }
            }
        }
        else if ((1 == hss_master_need_init) && ((5 == hssidx[index]) ||(12 == hssidx[index])))
        {
            tb_id = CsCfg0_t + hss_id;
            cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &cs_cfg);
            value = 0;
            DRV_IOW_FIELD(tb_id, CsCfg0_cfgHssReset_f, &value, &cs_cfg);
            DRV_IOW_FIELD(tb_id, CsCfg0_cfgHssPdwnPllA_f, &value, &cs_cfg);
            DRV_IOW_FIELD(tb_id, CsCfg0_cfgHssRefClkValidB_f, &value, &cs_cfg);
            cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &cs_cfg);

            tb_id = CsCfg0_t + hss_id;
            cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &cs_cfg);
            value = 1;
            DRV_IOW_FIELD(tb_id, CsCfg0_cfgHssRefClkValidA_f, &value, &cs_cfg);
            cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &cs_cfg);

            if (0 == SDK_WORK_PLATFORM)
            {
                tb_id = CsMacroMiscMon0_t + hss_id;
                cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
                DRV_IOCTL(lchip, 0, cmd, &cs_mon);

                while(--timeout)
                {
                    DRV_IOCTL(lchip, 0, cmd, &cs_mon);
                    if (GetCsMacroMiscMon0(V,monHssPrtReadyA_f, &cs_mon))
                    {
                        plla_ready = 1;
                    }

                    if (GetCsMacroMiscMon0(V,monHssPrtReadyB_f, &cs_mon))
                    {
                        pllb_ready = 1;
                    }


                    if (plla_ready || pllb_ready)
                    {
                        break;
                    }
                }

                /*check ready*/
                if ((0 == plla_ready) && (0 == pllb_ready))
                {
                    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"Impedance Calibration fail! hssidx %d \n", hssidx[index]);
                    return CTC_E_DATAPATH_HSS_NOT_READY;
                }

                /*check Impedance Calibration done*/
                timeout = 0x4000;
                address = DRV_HSS_ADDR_COMBINE(0x10, 0x21);
                while(--timeout)
                {
                    drv_goldengate_chip_read_hss28g(lchip, hss_id, address, &serdes_value);
                    if (serdes_value & 0x01)
                    {
                        train_done = 1;
                    }

                    if (train_done)
                    {
                        break;
                    }
                }

                if (!train_done)
                {
                    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"Impedance Calibration train fail! hssidx %d \n", hssidx[index]);
                    return CTC_E_DATAPATH_HSS_NOT_READY;
                }
            }

            /*for 28G slave core*/
            p_hss = ctc_vector_get(p_gg_datapath_master[lchip]->p_hss_vector, (hssidx[index]+1));
            if (p_hss)
            {
                /*bc to all tx lane*/
                address = DRV_HSS_ADDR_COMBINE(0x11, 0x02);
                drv_goldengate_chip_read_hss28g(lchip, hss_id+1, address, &serdes_value);
                serdes_value |= 0x01;
                drv_goldengate_chip_write_hss28g(lchip, hss_id+1, address, serdes_value);
            }
        }
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_datapath_cb_register(uint8 lchip, SYS_CALLBACK_DATAPATH_FUN_P bpe_cb)
{
    LCHIP_CHECK(lchip);
    if (NULL == p_gg_datapath_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }

    p_gg_datapath_master[lchip]->datapath_update_func = bpe_cb;
    return CTC_E_NONE;
}

int32
sys_goldengate_datapath_set_misc_cfg(uint8 lchip)
{
    uint8  slice_id  = 0;
    uint16 serdes_id_tmp = 0;
    uint16 tbl_id = 0;
    uint16 fld_id = 0;
    uint16 serdes_id  =0;
    uint32 cmd = 0;
    uint32 value = 0;

    SYS_DATAPATH_INIT_CHECK(lchip);
    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s\n", __FUNCTION__);

    for (serdes_id = 0; serdes_id < SYS_GG_DATAPATH_SERDES_NUM; serdes_id++)
    {
        /* reset/relese, clear random value */
        slice_id = (serdes_id<48)?0:1;
        serdes_id_tmp = (serdes_id<48)?serdes_id:(serdes_id-48);
        if(serdes_id_tmp<32)
        {
            tbl_id = RlmHsCtlReset0_t + slice_id;
            fld_id = RlmHsCtlReset0_resetCoreHss15GAnethAWrapper0_f + (serdes_id_tmp/8) + ((serdes_id_tmp%8)*4);
            SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_PARAM,"tbl_id:%d, fld_id:%d\n", tbl_id, fld_id);
            value = 1;
            cmd = DRV_IOW(tbl_id, fld_id);
            DRV_IOCTL(lchip, 0, cmd, &value);

            value = 0;
            cmd = DRV_IOW(tbl_id, fld_id);
            DRV_IOCTL(lchip, 0, cmd, &value);
        }
        else if(serdes_id_tmp>31 && serdes_id_tmp<48)
        {
            tbl_id = RlmCsCtlReset0_t + slice_id;
            if(serdes_id_tmp<40)
            {
                fld_id = RlmCsCtlReset0_resetCoreHss15GAnethAWrapper4_f + (serdes_id_tmp-32);
            }
            else
            {
                fld_id = RlmCsCtlReset0_resetCoreHss28GAnethAWrapper0_f + ((serdes_id_tmp-40)/4) + ((serdes_id_tmp-40)%4)*2;
            }
            SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_PARAM,"tbl_id:%d, fld_id:%d\n", tbl_id, fld_id);
            value = 1;
            cmd = DRV_IOW(tbl_id, fld_id);
            DRV_IOCTL(lchip, 0, cmd, &value);

            value = 0;
            cmd = DRV_IOW(tbl_id, fld_id);
            DRV_IOCTL(lchip, 0, cmd, &value);
        }
    }

    return CTC_E_NONE;
}
uint32
sys_goldengate_datapath_get_prbs_status(uint8 lchip,uint8 serdes_id,uint8* enable,uint8* hss_id,uint8* hss_type,uint8* lane_id)
{
    uint8  internal_lane = 0;
    uint16 address = 0;
    uint16 value = 0;

    *hss_id = SYS_DATAPATH_MAP_SERDES_TO_HSSID(serdes_id);
    *lane_id = SYS_DATAPATH_MAP_SERDES_TO_LANE_ID(serdes_id);
    if(SYS_DATAPATH_SERDES_IS_HSS28G(serdes_id))
    {
        *hss_type = SYS_DATAPATH_HSS_TYPE_28G;
    }
    else
    {
        *hss_type = SYS_DATAPATH_HSS_TYPE_15G;
    }

    /* cfg Transmit Test Control Register, offset 0x01 */
    internal_lane = LANE_ID_MAP_INTERAL_LANE_ID(*hss_type, *lane_id);
    address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 1);

    /* reset, PRST(bit4)*/
    DRV_CHIP_READ_HSS(*hss_type, lchip, *hss_id, address, &value);
    *enable = (value & 0x8)? 1 : 0;

    return CTC_E_NONE;
}

#define __DYNAMIC_SWITCH__
int32
sys_goldengate_datapath_set_serdes_mode(uint8 lchip, uint8 serdes_id, uint8 mode, uint16 overclocking_speed)
{
    uint8 hss_idx = 0;
    uint8 slice_id = 0;
    sys_datapath_hss_attribute_t* p_hss_vec = NULL;
    sys_datapath_serdes_info_t* p_serdes = NULL;
    sys_datapath_lport_attr_t* p_port_attr = NULL;
    RlmNetCtlReset0_m rlm_reset;
    uint8 lane_id = 0;
    uint16 drv_port = 0;
    uint16 gport = 0;
    uint16 gport_new = 0;
    uint16 lport = 0;
    uint32 enable = 0;
    uint32 tb_id = 0;
    uint32 cmd = 0;
    uint32 value = 0;
    uint16 ori_lport = 0;
    uint8 ori_plla = 0;
    uint8 ori_pllb = 0;
    uint8 ori_core_div_a[SYS_DATAPATH_CORE_NUM] = {0};
    uint8 ori_core_div_b[SYS_DATAPATH_CORE_NUM] = {0};
    uint8 ori_intf_div_a = 0;
    uint8 ori_intf_div_b = 0;
    uint32 s_point = 0;
    uint16 mac_id = 0;
    uint16 coefficient[4] = {0};
    NetRxCgCfg0_m cg_cfg;
    int32 ret = 0;
    ctc_chip_serdes_polarity_t serdes_polarity;
    ctc_chip_serdes_ffe_t ffe;
    uint32 depth = 0;
    uint8 i = 0;
    uint16 address = 0;
    uint16 serdes_val = 0;
    uint8 internal_lane = 0;
    uint8 ffe_mode = 0;
    uint8 chan_id = 0;
    uint8 switch_4_to_1 = 0;
    uint8 ffe_status;
    uint8 gchip = 0;
    uint16 gport_temp = 0;
    uint8  drop_index = 0;
    uint8  drop_num = 0;
    uint8 need_reset_app = 0;
    uint8 overclock_status = 0;

    sal_memset(&ffe, 0, sizeof(ctc_chip_serdes_ffe_t));
    SYS_DATAPATH_INIT_CHECK(lchip);

    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s\n", __FUNCTION__);
    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_PARAM,"serdes_id:%d,  Mode:%d \n", serdes_id, mode);

    hss_idx = SYS_DATAPATH_MAP_SERDES_TO_HSS_INDEX(serdes_id);
    lane_id = SYS_DATAPATH_MAP_SERDES_TO_LANE_ID(serdes_id);
    slice_id = (serdes_id >= SERDES_NUM_PER_SLICE)?1:0;

    p_hss_vec = ctc_vector_get(p_gg_datapath_master[lchip]->p_hss_vector, hss_idx);
    if (p_hss_vec == NULL)
    {
        return CTC_E_NOT_INIT;
    }

    p_serdes = &p_hss_vec->serdes_info[lane_id];

    if ((mode == CTC_CHIP_SERDES_XSGMII_MODE) || (mode == CTC_CHIP_SERDES_QSGMII_MODE))
    {
        return CTC_E_FEATURE_NOT_SUPPORT;
    }

    if (!p_serdes->is_dyn)
    {
        return CTC_E_FEATURE_NOT_SUPPORT;
    }

    if ((p_hss_vec->hss_type == 0) && (mode == CTC_CHIP_SERDES_CG_MODE))
    {
        return CTC_E_FEATURE_NOT_SUPPORT;
    }

    if (overclocking_speed == SYS_CHIP_SERDES_OVERCLOCKING_SPEED_11G_MODE)
    {
        overclocking_speed = CTC_CHIP_SERDES_OCS_MODE_11_06G;
    }
    else if (overclocking_speed == SYS_CHIP_SERDES_OVERCLOCKING_SPEED_12DOT5G_MODE)
    {
        overclocking_speed = CTC_CHIP_SERDES_OCS_MODE_12_58G;
    }

    if ((p_hss_vec->hss_type == 1) && (overclocking_speed != CTC_CHIP_SERDES_OCS_MODE_NONE))
    {
        return CTC_E_FEATURE_NOT_SUPPORT;
    }
    p_port_attr = (sys_datapath_lport_attr_t*)(&p_gg_datapath_master[lchip]->port_attr[slice_id][p_serdes->lport]);
    if (p_port_attr == NULL)
    {
        return CTC_E_NOT_INIT;
    }

    /* mac id range 1-39, 48-55. if mac id bigger than 39, should be converted */
    mac_id = p_port_attr->mac_id;
    if(mac_id > 39)
    {
        mac_id = mac_id - 8;
    }

    if ((p_serdes->mode == mode) && (p_serdes->overclocking_speed == overclocking_speed))
    {
        /*switch mode not change, do not do switch*/
        return CTC_E_NONE;
    }

    if ((p_serdes->overclocking_speed == CTC_CHIP_SERDES_OCS_MODE_12_58G) && (overclocking_speed == CTC_CHIP_SERDES_OCS_MODE_NONE))
    {
        /*need dec*/
        overclock_status = 1;
    }
    else if ((p_serdes->overclocking_speed == CTC_CHIP_SERDES_OCS_MODE_NONE) && (overclocking_speed == CTC_CHIP_SERDES_OCS_MODE_12_58G))
    {
        /*need incr*/
        overclock_status = 2;
    }
    else
    {
        /*unchange*/
        overclock_status = 0;
    }
    ori_plla = p_hss_vec->plla_mode;
    ori_core_div_a[0] = p_hss_vec->core_div_a[0];
    ori_core_div_a[1] = p_hss_vec->core_div_a[1];
    ori_intf_div_a = p_hss_vec->intf_div_a;

    ori_pllb = p_hss_vec->pllb_mode;
    ori_core_div_b[0] = p_hss_vec->core_div_b[0];
    ori_core_div_b[1] = p_hss_vec->core_div_b[1];
    ori_intf_div_b = p_hss_vec->intf_div_b;
    ori_lport = p_serdes->lport;

    /*1. check mac is disable */
    drv_port = p_serdes->lport+256*slice_id;
    CTC_ERROR_RETURN(sys_goldengate_get_gchip_id(lchip, &gchip));
    gport = SYS_MAP_DRV_LPORT_TO_CTC_GPORT(gchip, drv_port);

    SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_INFO,"original mode:%d, original speed:%d, drv_port:0x%x,  ctc_gport:0x%x \n",
            p_serdes->mode,p_port_attr->speed_mode, drv_port, gport);

    /* if enable 100G port cl73, no check mac enable */
    lport = CTC_MAP_GPORT_TO_LPORT(gport);
    if (((CTC_CHIP_SERDES_CG_MODE != p_port_attr->pcs_mode) && (CTC_CHIP_SERDES_XLG_MODE != p_port_attr->pcs_mode))
        || (1 != p_gg_port_master[lchip]->igs_port_prop[lport].cl73_status))
    {
        sys_goldengate_port_get_mac_en(lchip, gport, &enable);
        if (TRUE == enable)
        {
            return CTC_E_DATAPATH_MAC_ENABLE;
        }
    }

    if (((p_serdes->mode == CTC_CHIP_SERDES_SGMII_MODE) || (p_serdes->mode == CTC_CHIP_SERDES_XFI_MODE) || (p_serdes->mode == CTC_CHIP_SERDES_2DOT5G_MODE))
        && ((mode == CTC_CHIP_SERDES_XLG_MODE) || (mode == CTC_CHIP_SERDES_CG_MODE) || (mode == CTC_CHIP_SERDES_XAUI_MODE)
         || (mode == CTC_CHIP_SERDES_DXAUI_MODE)))
    {
        /*from four serdes switch to one serdes */
        gport_temp = gport - gport%4;
        if (gport_temp == gport)
        {
            drop_num = 4;
            need_reset_app = 1;
        }
        else
        {
            gport_temp = gport;
            drop_num = 1;
            need_reset_app = 0;
        }
    }
    else if (((p_serdes->mode == CTC_CHIP_SERDES_SGMII_MODE) || (p_serdes->mode == CTC_CHIP_SERDES_XFI_MODE) || (p_serdes->mode == CTC_CHIP_SERDES_2DOT5G_MODE)
        || (p_serdes->mode == CTC_CHIP_SERDES_NONE_MODE)) && ((mode == CTC_CHIP_SERDES_SGMII_MODE) || (mode == CTC_CHIP_SERDES_XFI_MODE)
        || (mode == CTC_CHIP_SERDES_2DOT5G_MODE) || (mode == CTC_CHIP_SERDES_NONE_MODE)))
    {
        gport_temp = gport;
        drop_num = 1;
        need_reset_app = 0;
    }
    else
    {
        gport_temp = gport;
        drop_num = 1;
        need_reset_app = 1;
    }

    chan_id = p_port_attr->chan_id;
    for (drop_index=0; drop_index<drop_num; drop_index++)
    {
        /*2. If port is dest, enqdrop */
        sys_goldengate_queue_set_port_drop_en(lchip, gport_temp, TRUE);

        /*3. check queue flush clear*/
        sys_goldengate_queue_get_port_depth(lchip, gport_temp, &depth);
        tb_id = NetTxCreditStatus0_t + slice_id;
        cmd = DRV_IOR(tb_id, NetTxCreditStatus0_port0CreditUsed_f+p_port_attr->mac_id);
        DRV_IOCTL(lchip, 0, cmd, &value);
        while (depth || value)
        {
            sal_task_sleep(1);
            if((i++) >50)
            {
                SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Cannot flush queue depth(%d) to Zero credit %d port 0x%x\n", depth, value, gport_temp);
                sys_goldengate_queue_set_port_drop_en(lchip, gport_temp, FALSE);
                return CTC_E_DATAPATH_SWITCH_FAIL;
            }
            sys_goldengate_queue_get_port_depth(lchip, gport_temp, &depth);
            tb_id = NetTxCreditStatus0_t + slice_id;
            cmd = DRV_IOR(tb_id, NetTxCreditStatus0_port0CreditUsed_f+p_port_attr->mac_id);
            DRV_IOCTL(lchip, 0, cmd, &value);
        }
        /* credit not engouth, packet overrun*/
        sal_task_sleep(2);
        gport_temp++;
    }

    sys_goldengate_port_set_mac_mode(lchip, slice_id, mac_id, mode);
    /*4. reset XqmacApp for 10g/40g switch*/
    if ((p_port_attr->reset_app == 0) && need_reset_app)
    {
        SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Need do XqmacApp reset, mac_id:%d \n", p_port_attr->mac_id);
        tb_id = RlmNetCtlReset0_t + slice_id;
        cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &rlm_reset);
        value = 1;
        DRV_IOW_FIELD(tb_id, (RlmNetCtlReset0_resetCoreXqmac0App_f+(mac_id/4)*2), &value, &rlm_reset);
        cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &rlm_reset);

        tb_id = RlmNetCtlReset0_t + slice_id;
        cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &rlm_reset);
        value = 0;
        DRV_IOW_FIELD(tb_id, (RlmNetCtlReset0_resetCoreXqmac0App_f+(mac_id/4)*2), &value, &rlm_reset);
        cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &rlm_reset);
        p_port_attr->reset_app = 1;
    }

    /*5. serdes switch */
    /* get ffe value */
    internal_lane = LANE_ID_MAP_INTERAL_LANE_ID(p_hss_vec->hss_type, lane_id);
    address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x02);
    DRV_CHIP_READ_HSS(p_hss_vec->hss_type, lchip, p_hss_vec->hss_id, address, &serdes_val);
    serdes_val &= 0x10;
    ffe_mode = (serdes_val)?CTC_CHIP_SERDES_FFE_MODE_3AP:0;  /* 3ap mode, traditional mode */
    CTC_ERROR_GOTO(sys_goldengate_datapath_get_serdes_ffe(lchip, serdes_id, coefficient, ffe_mode, &ffe_status), ret, error2);
    /*5.1 rebuild serdes info */
    CTC_ERROR_GOTO(sys_goldengate_datapath_build_serdes_info(lchip, serdes_id, mode, 1, p_serdes->lport, overclocking_speed), ret, error2);

    if ((p_serdes->lport != ori_lport) && (ori_lport%4))
    {
        /*reserve original lport*/
        p_gg_datapath_master[lchip]->port_attr[slice_id][ori_lport].port_type = SYS_DATAPATH_RSV_PORT;
        switch_4_to_1 = 1;/* 4 serdes bangding to 1 mac*/
    }
    else
    {
        switch_4_to_1 = 0;
    }

    /*5.2 reconfig Hss */
    if (ori_plla != p_hss_vec->plla_mode)
    {
        SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Need change plla cfg, ori:%d, new:%d \n", ori_plla, p_hss_vec->plla_mode);

        if (p_hss_vec->hss_type)
        {
            CTC_ERROR_GOTO(sys_goldengate_datapath_hss28g_pll_cfg(lchip, hss_idx, SYS_DATAPATH_PLL_SEL_PLLA,
                    p_hss_vec->plla_mode), ret, error2);
        }
        else
        {
            CTC_ERROR_GOTO(sys_goldengate_datapath_hss15g_pll_cfg(lchip, hss_idx, SYS_DATAPATH_PLL_SEL_PLLA,
                    p_hss_vec->plla_mode), ret, error2);
        }
    }

    if (ori_pllb != p_hss_vec->pllb_mode)
    {
        SYS_DATAPATH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Need change pllb cfg, ori:%d, new:%d \n", ori_pllb, p_hss_vec->pllb_mode);
        if (p_hss_vec->hss_type)
        {
            CTC_ERROR_GOTO(sys_goldengate_datapath_hss28g_pll_cfg(lchip, hss_idx, SYS_DATAPATH_PLL_SEL_PLLB,
                    p_hss_vec->pllb_mode), ret, error2);
        }
        else
        {
            CTC_ERROR_GOTO(sys_goldengate_datapath_hss15g_pll_cfg(lchip, hss_idx, SYS_DATAPATH_PLL_SEL_PLLB,
                    p_hss_vec->pllb_mode), ret, error2);
        }
    }

    /*5.3 reconfig hss internal register for rx */
    CTC_ERROR_GOTO(sys_goldengate_datapath_set_hss_internal(lchip, serdes_id, mode), ret, error2);

    /*5.4 reconfig hss clktree */
    if ((ori_plla != p_hss_vec->plla_mode)
        ||(ori_core_div_a[0] != p_hss_vec->core_div_a[0])
        ||(ori_core_div_a[1] != p_hss_vec->core_div_a[1])
        ||(ori_intf_div_a != p_hss_vec->intf_div_a))
    {
        if (p_hss_vec->hss_type)
        {
            CTC_ERROR_GOTO(sys_goldengate_datapath_hss28g_cfg_clktree(lchip, hss_idx, SYS_DATAPATH_PLL_SEL_PLLA), ret, error2);
        }
        else
        {
            CTC_ERROR_GOTO(sys_goldengate_datapath_hss15g_cfg_clktree(lchip, hss_idx, SYS_DATAPATH_PLL_SEL_PLLA), ret, error2);
        }
    }

    if ((ori_pllb != p_hss_vec->pllb_mode)
        ||(ori_core_div_b[0] != p_hss_vec->core_div_b[0])
        ||(ori_core_div_b[1] != p_hss_vec->core_div_b[1])
        ||(ori_intf_div_b != p_hss_vec->intf_div_b))
    {
        if (p_hss_vec->hss_type)
        {
            CTC_ERROR_GOTO(sys_goldengate_datapath_hss28g_cfg_clktree(lchip, hss_idx, SYS_DATAPATH_PLL_SEL_PLLB), ret, error2);
        }
        else
        {
            CTC_ERROR_GOTO(sys_goldengate_datapath_hss15g_cfg_clktree(lchip, hss_idx, SYS_DATAPATH_PLL_SEL_PLLB), ret, error2);
        }
    }

    /*5.5 reconfig per lane clktree */
    if (p_hss_vec->hss_type)
    {
        CTC_ERROR_GOTO(_sys_goldengate_datapath_hss28g_cfg_lane_clktree(lchip, lane_id, p_hss_vec), ret, error2);
    }
    else
    {
        CTC_ERROR_GOTO(_sys_goldengate_datapath_hss15g_cfg_lane_clktree(lchip, lane_id, p_hss_vec), ret, error2);
    }

    /*5. 6 reset serdes tx link, should re-cfg ffe, set poly */
    CTC_ERROR_GOTO(sys_goldengate_datapath_set_link_reset(lchip, (serdes_id), TRUE, 1), ret, error2);
    sal_task_sleep(1);
    CTC_ERROR_GOTO(sys_goldengate_datapath_set_link_reset(lchip, (serdes_id), FALSE, 1), ret, error2);
    sal_task_sleep(1);

    serdes_polarity.serdes_id = serdes_id;

    serdes_polarity.dir = 0;
    serdes_polarity.polarity_mode= (p_serdes->rx_polarity)?1:0;
    CTC_ERROR_GOTO(sys_goldengate_datapath_set_serdes_polarity(lchip, &serdes_polarity), ret, error2);

    serdes_polarity.dir = 1;
    serdes_polarity.polarity_mode = (p_serdes->tx_polarity)?1:0;
    CTC_ERROR_GOTO(sys_goldengate_datapath_set_serdes_polarity(lchip, &serdes_polarity), ret, error2);

    /* cfg ffe, need select ffe_mode, 0--traditional mode, 1--3ap mode */
    if(0 == ffe_mode)
    {
        if (p_hss_vec->hss_type == 0) /*hss15g using sum mode*/
        {
            internal_lane = g_gg_hss15g_lane_map[lane_id];
            address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 2);
            CTC_ERROR_GOTO(drv_goldengate_chip_read_hss15g(lchip, p_hss_vec->hss_id, address, &serdes_val), ret, error2);
            serdes_val &= 0xff7f;
            CTC_ERROR_GOTO(drv_goldengate_chip_write_hss15g(lchip, p_hss_vec->hss_id, address, serdes_val), ret, error2);
            address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x02);
            CTC_ERROR_GOTO(drv_goldengate_chip_read_hss15g(lchip, p_hss_vec->hss_id, address, &serdes_val), ret, error2);
            serdes_val |= 0x01;
            CTC_ERROR_GOTO(drv_goldengate_chip_write_hss15g(lchip, p_hss_vec->hss_id, address, serdes_val), ret, error2);
        }
        ffe.mode = CTC_CHIP_SERDES_FFE_MODE_DEFINE;
    }
    else
    {
        ffe.mode = CTC_CHIP_SERDES_FFE_MODE_3AP;
    }
    ffe.serdes_id = serdes_id;
    ffe.coefficient[0] = coefficient[0];
    ffe.coefficient[1] = coefficient[1];
    ffe.coefficient[2] = coefficient[2];
    ffe.coefficient[3] = coefficient[3];
    CTC_ERROR_GOTO(sys_goldengate_datapath_set_serdes_ffe(lchip, &ffe), ret, error2);

    /*6. check switch success */
    if (mode != CTC_CHIP_SERDES_NONE_MODE)
    {
        /*7. Datapath switch */
        p_port_attr = (sys_datapath_lport_attr_t*)(&p_gg_datapath_master[lchip]->port_attr[slice_id][p_serdes->lport]);
        if (p_port_attr == NULL)
        {
            ret = CTC_E_NOT_INIT;
            goto error2;
        }

        p_port_attr->switch_num ++;
        gport_new = SYS_MAP_DRV_LPORT_TO_CTC_GPORT(gchip, p_serdes->lport+256*slice_id);
        /*7.1 reconfig Netrx_buf*/
        CTC_ERROR_GOTO(_sys_goldengate_datapath_set_netrx_buf(lchip, mode, p_port_attr->mac_id, slice_id), ret, error2);

        /*7.2 reconfig Buf weight */
        cmd = DRV_IOR((BufRetrvPktConfigMem0_t+slice_id), BufRetrvPktConfigMem0_startPtr_f);
        DRV_IOCTL(lchip, p_port_attr->chan_id, cmd, &s_point);

        CTC_ERROR_GOTO(_sys_goldengate_datapath_set_qmgr_resource(lchip, p_port_attr->speed_mode,
            p_port_attr->chan_id, slice_id, 1), ret, error2);

        CTC_ERROR_GOTO(_sys_goldengate_datapath_set_bufretrv_pkt_mem(lchip, slice_id, s_point,
            p_port_attr->chan_id, p_port_attr->speed_mode, NULL), ret, error2);

        CTC_ERROR_GOTO(_sys_goldengate_datapath_set_bufretrv_buf_mem(lchip, slice_id, s_point,
            p_port_attr->chan_id, p_port_attr->speed_mode, NULL), ret, error2);

        /*7.3 reconfig chan distribute*/
        CTC_ERROR_GOTO(_sys_goldengate_datapath_set_chan_en(lchip, slice_id, p_port_attr->speed_mode,
            p_port_attr->chan_id), ret, error2);

        /*7.4 reconfig NetTxPortStaticInfo portMode*/
        CTC_ERROR_GOTO(_sys_goldengate_datapath_set_nettx_buf(lchip, p_port_attr->speed_mode,
            p_port_attr->mac_id, slice_id, 0, NULL), ret, error2);

        /*7.5 reconfig calendar*/
        CTC_ERROR_GOTO(sys_goldengate_datapath_set_calendar_cfg(lchip, slice_id, p_port_attr->speed_mode,
            p_port_attr->mac_id), ret, error2);

        /*8. reconfig global weight due to realtime chan state*/
        CTC_ERROR_GOTO(_sys_goldengate_datapath_cfg_global_weight(lchip), ret, error2);

        /*9. special process for cg channel*/
        if (p_port_attr->chan_id == CG0_CHAN)
        {
            cmd = DRV_IOR((NetRxCgCfg0_t+p_port_attr->slice_id), DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &cg_cfg);
            if (p_port_attr->speed_mode == CTC_PORT_SPEED_100G)
            {
                SetNetRxCgCfg0(V,cgMode0_f, &cg_cfg, 1);
            }
            else
            {
                SetNetRxCgCfg0(V,cgMode0_f, &cg_cfg, 0);
            }
            cmd = DRV_IOW((NetRxCgCfg0_t+p_port_attr->slice_id), DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &cg_cfg);
        }

        if (p_port_attr->chan_id == CG1_CHAN)
        {
            cmd = DRV_IOR((NetRxCgCfg0_t+p_port_attr->slice_id), DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &cg_cfg);
            if (p_port_attr->speed_mode == CTC_PORT_SPEED_100G)
            {
                SetNetRxCgCfg0(V,cgMode1_f, &cg_cfg, 1);
            }
            else
            {
                SetNetRxCgCfg0(V,cgMode1_f, &cg_cfg, 0);
            }
            cmd = DRV_IOW((NetRxCgCfg0_t+p_port_attr->slice_id), DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &cg_cfg);
        }

        /*10. disable enqdrop */
        if ((mode == CTC_CHIP_SERDES_XLG_MODE) || (mode == CTC_CHIP_SERDES_CG_MODE)
            || (mode == CTC_CHIP_SERDES_XAUI_MODE) || (mode == CTC_CHIP_SERDES_DXAUI_MODE))
        {
            if (p_port_attr->switch_num == 4)
            {
                p_port_attr->switch_num = 0;
                sys_goldengate_queue_set_port_drop_en(lchip, gport_new, FALSE);
                sys_goldengate_queue_set_port_drop_en(lchip, gport, FALSE);
            }
            else if (gport != gport_new)
            {
                sys_goldengate_queue_set_port_drop_en(lchip, gport, FALSE);
            }
        }
        else
        {
            sys_goldengate_queue_set_port_drop_en(lchip, gport, FALSE);
            if (gport != gport_new)
            {
                sys_goldengate_queue_set_port_drop_en(lchip, gport_new, FALSE);
            }
        }

        /*11. add port to channel*/
        if (switch_4_to_1)
        {
            chan_id = SYS_DROP_CHANNEL_ID;
            lport = ori_lport + 256*slice_id;
        }
        else
        {
            chan_id = p_port_attr->chan_id + 64*slice_id;
            lport = p_serdes->lport + 256*slice_id;
        }
        sys_goldengate_add_port_to_channel(lchip, lport, chan_id);
        sys_goldengate_queue_serdes_drop_init(lchip, lport);
    }
    else
    {
        /*9. special process for cg channel*/
        if (chan_id == CG0_CHAN)
        {
            cmd = DRV_IOR((NetRxCgCfg0_t+p_port_attr->slice_id), DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &cg_cfg);
            if (p_port_attr->speed_mode == CTC_PORT_SPEED_100G)
            {
                SetNetRxCgCfg0(V,cgMode0_f, &cg_cfg, 1);
            }
            else
            {
                SetNetRxCgCfg0(V,cgMode0_f, &cg_cfg, 0);
            }
            cmd = DRV_IOW((NetRxCgCfg0_t+p_port_attr->slice_id), DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &cg_cfg);
        }

        if (chan_id == CG1_CHAN)
        {
            cmd = DRV_IOR((NetRxCgCfg0_t+p_port_attr->slice_id), DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &cg_cfg);
            if (p_port_attr->speed_mode == CTC_PORT_SPEED_100G)
            {
                SetNetRxCgCfg0(V,cgMode1_f, &cg_cfg, 1);
            }
            else
            {
                SetNetRxCgCfg0(V,cgMode1_f, &cg_cfg, 0);
            }
            cmd = DRV_IOW((NetRxCgCfg0_t+p_port_attr->slice_id), DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &cg_cfg);
        }
    }

    if (overclock_status == 1)
    {
        p_gg_datapath_master[lchip]->overclock_count--;
        if (p_gg_datapath_master[lchip]->overclock_count == 0)
        {
            if (sys_goldengate_get_cut_through_en(lchip))
            {
                sys_goldengate_net_tx_threshold_cfg(lchip, 5, 11);
            }
            else
            {
                sys_goldengate_net_tx_threshold_cfg(lchip, 3, 4);
            }
        }
    }
    else if (overclock_status == 2)
    {
        p_gg_datapath_master[lchip]->overclock_count++;
        if (p_gg_datapath_master[lchip]->overclock_count == 1)
        {
            sys_goldengate_net_tx_threshold_cfg(lchip, 3, 9);
        }
    }
    return CTC_E_NONE;

error2:
    sys_goldengate_queue_set_port_drop_en(lchip, gport, FALSE);
    p_port_attr->switch_num = 0;
    p_port_attr->reset_app = 0;

    return ret;
}

#define __INIT__
STATIC int32
_sys_goldengate_datapath_init_db(uint8 lchip, ctc_datapath_global_cfg_t* p_datapath_cfg)
{
    sys_datapath_lport_attr_t* p_port_attr = NULL;
    uint8 serdes_id = 0;
    uint8 slice_id = 0;

    p_gg_datapath_master[lchip]->core_plla = p_datapath_cfg->core_frequency_a;
    p_gg_datapath_master[lchip]->core_pllb = p_datapath_cfg->core_frequency_b;

    for (serdes_id = 0; serdes_id < SYS_GG_DATAPATH_SERDES_NUM; serdes_id++)
    {
        if (p_datapath_cfg->serdes[serdes_id].mode == CTC_CHIP_SERDES_NONE_MODE)
        {
            /*serdes is not used*/
            continue;
        }

        CTC_ERROR_RETURN(sys_goldengate_datapath_build_serdes_info(lchip, serdes_id, p_datapath_cfg->serdes[serdes_id].mode,
            p_datapath_cfg->serdes[serdes_id].is_dynamic, 0xFFFF, 0));
    }

    /*internal channel alloc */
    for (slice_id = 0; slice_id < SYS_MAX_LOCAL_SLICE_NUM; slice_id++)
    {
        /*
        for (index = 0; index < 4; index++)
        {
            p_port_attr = (sys_datapath_lport_attr_t*)(&p_gg_datapath_master[lchip]->port_attr[slice_id][SYS_RSV_PORT_CPU_DMA_ID+index]);
            p_port_attr->chan_id = SYS_DMA_CHANNEL_ID+index;
            p_port_attr->mac_id = 0;
            p_port_attr->port_type = SYS_DATAPATH_DMA_PORT;
            p_port_attr->slice_id = slice_id;
            p_port_attr->need_ext = 0;
        }
        */

        p_port_attr = (sys_datapath_lport_attr_t*)(&p_gg_datapath_master[lchip]->port_attr[slice_id][SYS_RSV_PORT_OAM_CPU_ID]);
        p_port_attr->chan_id = SYS_OAM_CHANNEL_ID;
        p_port_attr->mac_id = 0;
        p_port_attr->port_type = SYS_DATAPATH_OAM_PORT;
        p_port_attr->slice_id = slice_id;
        p_port_attr->need_ext = 0;

        p_port_attr = (sys_datapath_lport_attr_t*)(&p_gg_datapath_master[lchip]->port_attr[slice_id][SYS_RSV_PORT_ILOOP_ID]);
        p_port_attr->chan_id = SYS_ILOOP_CHANNEL_ID;
        p_port_attr->mac_id = 0;
        p_port_attr->port_type = SYS_DATAPATH_ILOOP_PORT;
        p_port_attr->slice_id = slice_id;
        p_port_attr->need_ext = 0;

        p_port_attr = (sys_datapath_lport_attr_t*)(&p_gg_datapath_master[lchip]->port_attr[slice_id][SYS_RSV_PORT_ELOOP_ID]);
        p_port_attr->chan_id = SYS_ELOOP_CHANNEL_ID;
        p_port_attr->mac_id = 0;
        p_port_attr->port_type = SYS_DATAPATH_ELOOP_PORT;
        p_port_attr->slice_id = slice_id;
        p_port_attr->need_ext = 0;
    }

    p_gg_datapath_master[lchip]->oam_chan = SYS_OAM_CHANNEL_ID;
    p_gg_datapath_master[lchip]->dma_chan = SYS_DMA_CHANNEL_ID;
    p_gg_datapath_master[lchip]->iloop_chan = SYS_ILOOP_CHANNEL_ID;
    p_gg_datapath_master[lchip]->eloop_chan = SYS_ELOOP_CHANNEL_ID;

    return CTC_E_NONE;
}

int32
sys_goldengate_datapath_init(uint8 lchip, void* p_global_cfg)
{
    uint8 serdes_id = 0;
    uint8 hss_idx = 0;
    uint16 lport = 0;
    sys_datapath_lport_attr_t* p_port = NULL;
    uint8 slice_id = 0;
    ctc_chip_serdes_polarity_t serdes_polarity;
    uint32 cmd = 0;
    uint8 mac_id = 0;
    NetTxPortDynamicInfo0_m dyn_info;
    sys_datapath_hss_attribute_t* p_hss = NULL;
    ctc_datapath_global_cfg_t* p_datapath_cfg = NULL;
    uint8 lane_idx = 0;
    uint16 value = 0;
    uint8 internal_lane = 0;
    uint16 address = 0;
    uint8 hss_id = 0;
    uint8 lane_id = 0;

    CTC_PTR_VALID_CHECK(p_global_cfg);
    p_datapath_cfg = (ctc_datapath_global_cfg_t*)p_global_cfg;

    if (lchip >= CTC_MAX_LOCAL_CHIP_NUM)
    {
        return CTC_E_INVALID_PARAM;
    }

    if (p_gg_datapath_master[lchip])
    {
        return CTC_E_NONE;
    }
    /*create datapath master*/
    p_gg_datapath_master[lchip] = (sys_datapath_master_t*)mem_malloc(MEM_SYSTEM_MODULE, sizeof(sys_datapath_master_t));
    if (NULL == p_gg_datapath_master[lchip])
    {
        return CTC_E_NO_MEMORY;
    }
    sal_memset(p_gg_datapath_master[lchip], 0, sizeof(sys_datapath_master_t));

    p_gg_datapath_master[lchip]->p_hss_vector = ctc_vector_init(2, SYS_DATAPATH_HSS_NUM /2);
    if (NULL == p_gg_datapath_master[lchip]->p_hss_vector)
    {
        mem_free(p_gg_datapath_master[lchip]);
        return CTC_E_NO_MEMORY;
    }

    /*init port attribute to rsv port default*/
    sal_memset(&dyn_info, 0, sizeof(NetTxPortDynamicInfo0_m));
    for (slice_id = 0; slice_id < SYS_MAX_LOCAL_SLICE_NUM; slice_id++)
    {
        for (lport = 0; lport < 256; lport++)
        {
            p_port = (sys_datapath_lport_attr_t*)(&p_gg_datapath_master[lchip]->port_attr[slice_id][lport]);
            p_port->port_type =  SYS_DATAPATH_RSV_PORT;
        }
    }

    /*0. prepare data base*/
    CTC_ERROR_RETURN(_sys_goldengate_datapath_init_db(lchip, p_datapath_cfg));

    /*1. do module release */
    CTC_ERROR_RETURN(_sys_goldengate_datapath_core_clock_init(lchip, p_datapath_cfg->core_frequency_a, p_datapath_cfg->core_frequency_b));

    CTC_ERROR_RETURN(_sys_goldengate_datapath_sup_init(lchip));

    CTC_ERROR_RETURN(_sys_goldengate_datapath_module_init(lchip));

    /*init NetTxPortDynamicInfo0 default*/
    sal_memset(&dyn_info, 0, sizeof(NetTxPortDynamicInfo0_m));
    for (slice_id = 0; slice_id < SYS_MAX_LOCAL_SLICE_NUM; slice_id++)
    {
        cmd = DRV_IOW((NetTxPortDynamicInfo0_t+slice_id), DRV_ENTRY_FLAG);
        for (mac_id = 0; mac_id < 64; mac_id++)
        {
            DRV_IOCTL(lchip, mac_id, cmd, &dyn_info);
        }
    }

    /*2. do hss init */
    for (hss_idx = 0; hss_idx < SYS_DATAPATH_HSS_NUM; hss_idx++)
    {
        if (SYS_DATAPATH_HSS_IS_HSS28G(hss_idx))
        {
            CTC_ERROR_RETURN(_sys_goldengate_datapath_hss28g_init(lchip, hss_idx));
        }
        else
        {
            CTC_ERROR_RETURN(_sys_goldengate_datapath_hss15g_init(lchip, hss_idx));
        }
    }

    sal_task_sleep(5);

    /*3. do serdes init */
    for (serdes_id = 0; serdes_id < SYS_GG_DATAPATH_SERDES_NUM; serdes_id++)
    {
        CTC_ERROR_RETURN(sys_goldengate_datapath_set_hss_internal(lchip, serdes_id, p_datapath_cfg->serdes[serdes_id].mode));
        hss_id = SYS_DATAPATH_MAP_SERDES_TO_HSSID(serdes_id);
        lane_id = SYS_DATAPATH_MAP_SERDES_TO_LANE_ID(serdes_id);
        /*peak init*/
        if (SYS_DATAPATH_SERDES_IS_HSS28G(serdes_id))
        {
            internal_lane = LANE_ID_MAP_INTERAL_LANE_ID(SYS_DATAPATH_HSS_TYPE_28G, lane_id);

            /*serdes 40 write link-rx 0xb value&0xbffff          //bit[14] set to 0 */
            address = DRV_HSS_ADDR_COMBINE(g_gg_hss_rx_addr_map[internal_lane], 0xb);
            CTC_ERROR_RETURN(drv_goldengate_chip_read_hss28g(lchip, hss_id, address, &value));
            value &= 0xBFFF;
            CTC_ERROR_RETURN(drv_goldengate_chip_write_hss28g(lchip, hss_id, address, value));

            address = DRV_HSS_ADDR_COMBINE(g_gg_hss_rx_addr_map[internal_lane], 0x1b);
            CTC_ERROR_RETURN(drv_goldengate_chip_read_hss28g(lchip, hss_id, address, &value));
            value |= 0x67;

            CTC_ERROR_RETURN(drv_goldengate_chip_write_hss28g(lchip, hss_id, address, value));

            /*init v2 min to 0*/
            address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x1e);
            value = 0;
            CTC_ERROR_RETURN(drv_goldengate_chip_write_hss28g(lchip, hss_id, address, value));
            address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x1f);
            value = 0x11;
            CTC_ERROR_RETURN(drv_goldengate_chip_write_hss28g(lchip, hss_id, address, value));
        }
        else
        {
            internal_lane = LANE_ID_MAP_INTERAL_LANE_ID(SYS_DATAPATH_HSS_TYPE_15G, lane_id);

            /*init v2 min to 0*/
            address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x1e);
            value = 0;
            CTC_ERROR_RETURN(drv_goldengate_chip_write_hss15g(lchip, hss_id, address, value));
            address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x1f);
            value = 0x11;
            CTC_ERROR_RETURN(drv_goldengate_chip_write_hss15g(lchip, hss_id, address, value));
        }
    }

    /*4. do clktree init */
    for (hss_idx = 0; hss_idx < SYS_DATAPATH_HSS_NUM; hss_idx++)
    {
        p_hss = ctc_vector_get(p_gg_datapath_master[lchip]->p_hss_vector, hss_idx);
        if (NULL == p_hss)
        {
            continue;
        }

        if (SYS_DATAPATH_HSS_IS_HSS28G(hss_idx))
        {
            CTC_ERROR_RETURN(sys_goldengate_datapath_hss28g_cfg_clktree(lchip, hss_idx,SYS_DATAPATH_PLL_SEL_BOTH));

            for (lane_idx = 0; lane_idx < HSS28G_LANE_NUM; lane_idx++)
            {
                CTC_ERROR_RETURN(_sys_goldengate_datapath_hss28g_cfg_lane_clktree(lchip, lane_idx, p_hss));
            }
        }
        else
        {
            CTC_ERROR_RETURN(sys_goldengate_datapath_hss15g_cfg_clktree(lchip, hss_idx,SYS_DATAPATH_PLL_SEL_BOTH));

            for (lane_idx = 0; lane_idx < HSS15G_LANE_NUM; lane_idx++)
            {
                CTC_ERROR_RETURN(_sys_goldengate_datapath_hss15g_cfg_lane_clktree(lchip, lane_idx, p_hss));
            }
        }
    }

    /*5. set serdes poly*/
    for (serdes_id = 0; serdes_id < SYS_GG_DATAPATH_SERDES_NUM; serdes_id++)
    {
        CTC_ERROR_RETURN(sys_goldengate_datapath_set_link_reset(lchip, (serdes_id), TRUE, 1));
        sal_task_sleep(1);
        CTC_ERROR_RETURN(sys_goldengate_datapath_set_link_reset(lchip, (serdes_id), FALSE, 1));
        sal_task_sleep(1);

        if (!SYS_DATAPATH_SERDES_IS_HSS28G(serdes_id))
        {
            hss_id = SYS_DATAPATH_MAP_SERDES_TO_HSSID(serdes_id);
            lane_id = SYS_DATAPATH_MAP_SERDES_TO_LANE_ID(serdes_id);
            internal_lane = g_gg_hss15g_lane_map[lane_id];
            /*hss15g using sum mode*/
            address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 2);
            CTC_ERROR_RETURN(drv_goldengate_chip_read_hss15g(lchip, hss_id, address, &value));
            value &= 0xff7f;
            CTC_ERROR_RETURN(drv_goldengate_chip_write_hss15g(lchip, hss_id, address, value));
            address = DRV_HSS_ADDR_COMBINE(g_gg_hss_tx_addr_map[internal_lane], 0x02);
            CTC_ERROR_RETURN(drv_goldengate_chip_read_hss15g(lchip, hss_id, address, &value));
            value |= 0x01;
            CTC_ERROR_RETURN(drv_goldengate_chip_write_hss15g(lchip, hss_id, address, value));
        }

        serdes_polarity.serdes_id = serdes_id;

        serdes_polarity.dir = 0;
        serdes_polarity.polarity_mode= (p_datapath_cfg->serdes[serdes_id].rx_polarity)?1:0;
        CTC_ERROR_RETURN(sys_goldengate_datapath_set_serdes_polarity(lchip, &serdes_polarity));

        serdes_polarity.dir = 1;
        serdes_polarity.polarity_mode = (p_datapath_cfg->serdes[serdes_id].tx_polarity)?1:0;
        CTC_ERROR_RETURN(sys_goldengate_datapath_set_serdes_polarity(lchip, &serdes_polarity));

        CTC_ERROR_RETURN(sys_goldengate_datapath_set_serdes_tx_en(lchip, serdes_id, FALSE));
    }

    /*6. do datapath init */
    CTC_ERROR_RETURN(sys_goldengate_datapath_datapth_init(lchip));

    /*7. do Impedance Calibration */
    CTC_ERROR_RETURN(sys_goldengate_datapath_set_impedance_calibration(lchip));

    /*8. misc */
    CTC_ERROR_RETURN(sys_goldengate_datapath_set_misc_cfg(lchip));

    if (CTC_WB_ENABLE && CTC_WB_STATUS(lchip) == CTC_WB_STATUS_RELOADING)
    {
        CTC_ERROR_RETURN(sys_goldengate_datapath_wb_restore(lchip));
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_datapath_free_node_data(void* node_data, void* user_data)
{
    mem_free(node_data);

    return CTC_E_NONE;
}

int32
sys_goldengate_datapath_deinit(uint8 lchip)
{
    LCHIP_CHECK(lchip);
    if (NULL == p_gg_datapath_master[lchip])
    {
        return CTC_E_NONE;
    }

    /*free hss vector*/
    ctc_vector_traverse(p_gg_datapath_master[lchip]->p_hss_vector, (vector_traversal_fn)_sys_goldengate_datapath_free_node_data, NULL);
    ctc_vector_release(p_gg_datapath_master[lchip]->p_hss_vector);

    mem_free(p_gg_datapath_master[lchip]);

    return CTC_E_NONE;
}

int32
_sys_goldengate_datapath_hss_wb_mapping(sys_wb_datapath_hss_attribute_t *p_wb_hss, sys_datapath_hss_attribute_t *p_hss, uint8 sync)
{
    uint16 loop = 0;

    if (sync)
    {
        p_wb_hss->hss_type         = p_hss->hss_type;
        p_wb_hss->hss_id           = p_hss->hss_id;
        p_wb_hss->plla_mode 	   = p_hss->plla_mode;
        p_wb_hss->pllb_mode 	   = p_hss->pllb_mode;
        p_wb_hss->intf_div_a       = p_hss->intf_div_a;
        p_wb_hss->intf_div_b       = p_hss->intf_div_b;
        p_wb_hss->plla_ref_cnt     = p_hss->plla_ref_cnt;
        p_wb_hss->pllb_ref_cnt     = p_hss->pllb_ref_cnt;
        p_wb_hss->clktree_bitmap   = p_hss->clktree_bitmap;

        for(loop = 0; loop < SYS_DATAPATH_CORE_NUM; loop++)
        {
            p_wb_hss->core_div_a[loop] = p_hss->core_div_a[loop];
            p_wb_hss->core_div_b[loop] = p_hss->core_div_b[loop];
        }

        for(loop = 0; loop < HSS15G_LANE_NUM; loop++)
        {
            sal_memcpy(&p_wb_hss->serdes_info[loop], &p_hss->serdes_info[loop], sizeof(sys_datapath_serdes_info_t));
        }
    }
    else
    {
        p_hss->hss_type         = p_wb_hss->hss_type;
        p_hss->hss_id           = p_wb_hss->hss_id;
        p_hss->plla_mode 	    = p_wb_hss->plla_mode;
        p_hss->pllb_mode 	    = p_wb_hss->pllb_mode;
        p_hss->intf_div_a       = p_wb_hss->intf_div_a;
        p_hss->intf_div_b       = p_wb_hss->intf_div_b;
        p_hss->plla_ref_cnt     = p_wb_hss->plla_ref_cnt;
        p_hss->pllb_ref_cnt     = p_wb_hss->pllb_ref_cnt;
        p_hss->clktree_bitmap   = p_wb_hss->clktree_bitmap;

        for(loop = 0; loop < SYS_DATAPATH_CORE_NUM; loop++)
        {
             p_hss->core_div_a[loop] = p_wb_hss->core_div_a[loop];
             p_hss->core_div_b[loop] = p_wb_hss->core_div_b[loop];
        }

        for(loop = 0; loop < HSS15G_LANE_NUM; loop++)
        {
            sal_memcpy(&p_hss->serdes_info[loop], &p_wb_hss->serdes_info[loop], sizeof(sys_datapath_serdes_info_t));
        }
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_datapath_hss_wb_sync_func(void* array_data, uint32 index, void* user_data)
{
    uint16 max_entry_cnt = 0;
    sys_datapath_hss_attribute_t *p_hss = (sys_datapath_hss_attribute_t *)array_data;
    sys_wb_datapath_hss_attribute_t  *p_wb_hss;
    ctc_wb_data_t *wb_data = (ctc_wb_data_t *)user_data;

    max_entry_cnt = CTC_WB_DATA_BUFFER_LENGTH / (wb_data->key_len + wb_data->data_len);

    p_wb_hss = (sys_wb_datapath_hss_attribute_t *)wb_data->buffer + wb_data->valid_cnt;
    sal_memset(p_wb_hss, 0, sizeof(sys_wb_datapath_hss_attribute_t));

    p_wb_hss->hss_idx = index;

    CTC_ERROR_RETURN(_sys_goldengate_datapath_hss_wb_mapping(p_wb_hss, p_hss, 1));
    if (++wb_data->valid_cnt == max_entry_cnt)
    {
        CTC_ERROR_RETURN(ctc_wb_add_entry(wb_data));
        wb_data->valid_cnt = 0;
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_datapath_wb_sync(uint8 lchip)
{
    int32 ret = CTC_E_NONE;
    ctc_wb_data_t wb_data;
    sys_wb_datapath_master_t  wb_datapath_master;
    uint32  max_entry_cnt = 0;
    uint16 loop = 0;

    /*syncup datapath matser*/
    wb_data.buffer = mem_malloc(MEM_SYSTEM_MODULE, CTC_WB_DATA_BUFFER_LENGTH);
    if (NULL == wb_data.buffer)
    {
        ret = CTC_E_NO_MEMORY;
        goto done;
    }
    sal_memset(wb_data.buffer, 0, CTC_WB_DATA_BUFFER_LENGTH);

    CTC_WB_INIT_DATA_T((&wb_data),sys_wb_datapath_master_t, CTC_FEATURE_CHIP, SYS_WB_APPID_DATAPATH_SUBID_MASTER);

    max_entry_cnt = CTC_WB_DATA_BUFFER_LENGTH/(wb_data.key_len + wb_data.data_len);
    sal_memset(wb_data.buffer, 0, CTC_WB_DATA_BUFFER_LENGTH);
    sal_memset(&wb_datapath_master, 0, sizeof(sys_wb_datapath_master_t));
    do
    {
        wb_datapath_master.slice_id = loop / 256;
        wb_datapath_master.lport = loop % 256;
        sal_memcpy((uint8*)&wb_datapath_master.port_attr, (uint8*)&p_gg_datapath_master[lchip]->port_attr[loop / 256][loop % 256], sizeof(sys_datapath_lport_attr_t));

        sal_memcpy((uint8*)wb_data.buffer + wb_data.valid_cnt * sizeof(sys_wb_datapath_master_t),  (uint8*)&wb_datapath_master, sizeof(sys_wb_datapath_master_t));
        if (++wb_data.valid_cnt == max_entry_cnt)
        {
            CTC_ERROR_GOTO(ctc_wb_add_entry(&wb_data), ret, done);
            wb_data.valid_cnt = 0;
        }
    }while(++loop < SYS_GG_MAX_PORT_NUM_PER_CHIP);

    if(wb_data.valid_cnt)
    {
        CTC_ERROR_GOTO(ctc_wb_add_entry(&wb_data), ret, done);
    }

    /*syncup datapath hss*/
    CTC_WB_INIT_DATA_T((&wb_data),sys_wb_datapath_hss_attribute_t, CTC_FEATURE_CHIP, SYS_WB_APPID_DATAPATH_SUBID_HSS_ATTRIBUTE);

    CTC_ERROR_GOTO(ctc_vector_traverse2(p_gg_datapath_master[lchip]->p_hss_vector, 0, _sys_goldengate_datapath_hss_wb_sync_func, (void *)&wb_data), ret, done);
    if (wb_data.valid_cnt > 0)
    {
        CTC_ERROR_GOTO(ctc_wb_add_entry(&wb_data), ret, done);
        wb_data.valid_cnt = 0;
    }

done:
    if (wb_data.buffer)
    {
        mem_free(wb_data.buffer);
    }

    return ret;
}

int32
sys_goldengate_datapath_wb_restore(uint8 lchip)
{
    uint16 entry_cnt = 0;
    uint16 lport = 0;
    uint8 slice_id = 0;
    int32 ret = CTC_E_NONE;
    ctc_wb_query_t wb_query;
    sys_wb_datapath_master_t  *p_wb_datapath_master;
    sys_wb_datapath_hss_attribute_t *p_wb_hss;
    sys_datapath_hss_attribute_t *p_hss;

    sal_memset(&wb_query, 0, sizeof(ctc_wb_query_t));
    wb_query.buffer = mem_malloc(MEM_SYSTEM_MODULE,  CTC_WB_DATA_BUFFER_LENGTH);
    if (NULL == wb_query.buffer)
    {
        ret = CTC_E_NO_MEMORY;
        goto done;
    }
    sal_memset(wb_query.buffer, 0, CTC_WB_DATA_BUFFER_LENGTH);

    CTC_WB_INIT_QUERY_T((&wb_query), sys_wb_datapath_master_t, CTC_FEATURE_CHIP, SYS_WB_APPID_DATAPATH_SUBID_MASTER);

    /*restore datapath master*/
    CTC_WB_QUERY_ENTRY_BEGIN((&wb_query));
        p_wb_datapath_master = (sys_wb_datapath_master_t*)wb_query.buffer + entry_cnt++;
        lport = p_wb_datapath_master->lport;
        slice_id = p_wb_datapath_master->slice_id;
        sal_memcpy((uint8*)&p_gg_datapath_master[lchip]->port_attr[slice_id][lport], (uint8*)&p_wb_datapath_master->port_attr, sizeof(sys_datapath_lport_attr_t));
    CTC_WB_QUERY_ENTRY_END((&wb_query));


    /*restore datapath hss*/
    CTC_WB_INIT_QUERY_T((&wb_query), sys_wb_datapath_hss_attribute_t, CTC_FEATURE_CHIP, SYS_WB_APPID_DATAPATH_SUBID_HSS_ATTRIBUTE);

    CTC_WB_QUERY_ENTRY_BEGIN((&wb_query));
        p_wb_hss = (sys_wb_datapath_hss_attribute_t *)wb_query.buffer + entry_cnt++;

        p_hss = mem_malloc(MEM_SYSTEM_MODULE,  sizeof(sys_datapath_hss_attribute_t));
        if (NULL == p_hss)
        {
            ret = CTC_E_NO_MEMORY;
            goto done;
        }
        sal_memset(p_hss, 0, sizeof(sys_datapath_hss_attribute_t));

        ret = _sys_goldengate_datapath_hss_wb_mapping(p_wb_hss, p_hss, 0);
        if (ret)
        {
            continue;
        }

        /*add to soft table*/
        ctc_vector_add(p_gg_datapath_master[lchip]->p_hss_vector, p_wb_hss->hss_idx, p_hss);
    CTC_WB_QUERY_ENTRY_END((&wb_query));

done:
    if (wb_query.key)
    {
        mem_free(wb_query.key);
    }

    if (wb_query.buffer)
    {
        mem_free(wb_query.buffer);
    }

    return ret;
}


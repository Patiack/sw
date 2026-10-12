#include "sal.h"
#include "ctc_cli.h"
#include "greatbelt/include/drv_lib.h"
#include "greatbelt/include/drv_chip_ctrl.h"
#include "ctc_dkit.h"
#include "ctc_dkit_common.h"
#include "ctc_greatbelt_dkit_misc.h"
#include "ctc_greatbelt_dkit.h"

#define MAX_PER_LINE                 512
#define DKIT_PORT_MIN                0
#define DKIT_PORT_MAX                59
#define DKIT_RX_PORT_MIN             0
#define DKIT_RX_PORT_MAX             60
#define DKIT_LEN_64B                 0x40
#define DKIT_LEN_128B                0x80
#define DKIT_LEN_192B                0xc0
#define DKIT_LEN_256B                0x100
#define DKIT_NETRX_PKT_LEN           256
#define DKIT_NETTX_BUF_DATA_LEN      32
#define DKIT_PKT_BUF_NUM             4
#define DKIT_PKT_BUF_MEM_MAX         4096 /**< 4M*/

enum ctc_dkit_misc_integrity_op_e
{
    CTC_DKIT_MISC_INTEGRITY_OP_NONE,
    CTC_DKIT_MISC_INTEGRITY_OP_E,
    CTC_DKIT_MISC_INTEGRITY_OP_BE,
    CTC_DKIT_MISC_INTEGRITY_OP_LE,
    CTC_DKIT_MISC_INTEGRITY_OP_B,
    CTC_DKIT_MISC_INTEGRITY_OP_L,
    CTC_DKIT_MISC_INTEGRITY_OP_NUM
};
typedef enum ctc_dkit_misc_integrity_op_e ctc_dkit_misc_integrity_op_t;

#define DKIT_SERDES_ID_MAX 32

#define DKITS_SERDES_ID_CHECK(ID) \
    do { \
        if(ID >= DKIT_SERDES_ID_MAX) \
        {\
            CTC_DKIT_PRINT("serdes id %d exceed max id %d!!!\n", ID, DKIT_SERDES_ID_MAX-1);\
            return DKIT_E_INVALID_PARAM; \
        }\
    } while (0)

#define PATTERN_NUM 9
static uint32 bit_map[1024];
uint8 lane_serdes_mapping[DKIT_SERDES_ID_MAX] =
{
    0, 1, 2, 3, 4, 5, 6, 7,             /* hss0, lane_idx seq */
   0, 1, 2, 3, 4, 5, 6, 7,             /* hss1, lane_idx seq */
   4, 0, 5, 1, 6, 2, 7, 3,             /* hss2, lane_idx seq */
   4, 0, 5, 1, 6, 2, 7, 3              /* hss3, lane_idx seq */
};


STATIC int32
_ctc_greatbelt_dkit_internal_convert_serdes_addr(ctc_dkit_serdes_wr_t* p_para, uint32* addr, uint8* hss_id)
{
    uint8 lane_id = 0;
    uint32 rx_lane_addr[8] = {0x2, 0x3, 0x6, 0x7, 0x0a, 0x0b, 0x0e, 0x0f};
    uint32 tx_lane_addr[8] = {0x00, 0x01, 0x04, 0x05, 0x08, 0x09, 0x0c, 0x0d};

    DKITS_PTR_VALID_CHECK(p_para);
    DKITS_PTR_VALID_CHECK(addr);
    DKITS_PTR_VALID_CHECK(hss_id);

    *hss_id = p_para->serdes_id / 8;
    lane_id = lane_serdes_mapping[p_para->serdes_id];

    if (CTC_DKIT_SERDES_TX == p_para->type)
    {
        *addr = tx_lane_addr[lane_id];
    }
    else if (CTC_DKIT_SERDES_RX == p_para->type)
    {
        *addr = rx_lane_addr[lane_id];
    }
    else if (CTC_DKIT_SERDES_PLLA == p_para->type)
    {
        *addr = 0x10;
    }
    else if (CTC_DKIT_SERDES_COMMON == p_para->type)
    {
        *addr = 0x10;
    }

    *addr = (*addr << 6) + p_para->addr_offset;

    CTC_DKIT_PRINT_DEBUG("hssid:%d, lane:%d\n", *hss_id, lane_id);

    return CLI_SUCCESS;
}


STATIC int8
_ctc_greatbelt_dkit_misc_uint2int(uint8 value, uint8 signed_bit)
{
    uint8 tmp = 0;

    if (DKITS_IS_BIT_SET(value, signed_bit))
    {
        tmp = (~value) + 1;
        return -tmp;
    }
    else
    {
        return value;
    }
}

STATIC int8
_ctc_greatbelt_dkit_misc_int2uint(int8 value, uint8 signed_bit)
{
    uint8 tmp = 0;

    if (value < 0)
    {
        tmp = ~((uint8)value) + 1;
        DKITS_BIT_SET(tmp, signed_bit);
        return tmp;
    }
    else
    {
        return value;
    }
}


int32
ctc_greatbelt_dkit_misc_read_serdes(void* para)
{
    uint32 addr = 0;
    uint8 hss_id = 0;
    ctc_dkit_serdes_wr_t* p_para = (ctc_dkit_serdes_wr_t*)para;
    DKITS_PTR_VALID_CHECK(p_para);
    DKITS_SERDES_ID_CHECK(p_para->serdes_id);

    _ctc_greatbelt_dkit_internal_convert_serdes_addr(p_para, &addr, &hss_id);

    drv_greatbelt_chip_read_hss12g(p_para->lchip, hss_id, addr, &p_para->data);
    CTC_DKIT_PRINT_DEBUG("read  hss id:%d, addr: 0x%04x, value: 0x%04x\n",  hss_id, addr, p_para->data);
    return CLI_SUCCESS;
}


int32
ctc_greatbelt_dkit_misc_write_serdes(void* para)
{
    uint32 addr = 0;
    uint8 hss_id = 0;
    ctc_dkit_serdes_wr_t* p_para = (ctc_dkit_serdes_wr_t*)para;
    DKITS_PTR_VALID_CHECK(p_para);
    DKITS_SERDES_ID_CHECK(p_para->serdes_id);

    _ctc_greatbelt_dkit_internal_convert_serdes_addr(p_para, &addr, &hss_id);
    drv_greatbelt_chip_write_hss12g(p_para->lchip, hss_id, addr, p_para->data);
    CTC_DKIT_PRINT_DEBUG("write hss id:%d, addr: 0x%04x, value: 0x%04x\n", hss_id, addr, p_para->data);
    return CLI_SUCCESS;
}

STATIC int32
_ctc_greatbelt_dkit_misc_serdes_loopback(ctc_dkit_serdes_ctl_para_t* p_serdes_para)
{
    uint8 enable = 0;
    ctc_dkit_serdes_wr_t ctc_dkit_serdes_wr;
    sal_memset(&ctc_dkit_serdes_wr, 0, sizeof(ctc_dkit_serdes_wr_t));
    enable = (CTC_DKIT_SERDIS_CTL_ENABLE == p_serdes_para->para[0]);

    if (CTC_DKIT_SERDIS_LOOPBACK_INTERNAL == p_serdes_para->para[1])
    {
        sal_memset(&ctc_dkit_serdes_wr, 0, sizeof(ctc_dkit_serdes_wr_t));
        ctc_dkit_serdes_wr.serdes_id = p_serdes_para->serdes_id;
        ctc_dkit_serdes_wr.lchip = p_serdes_para->lchip;
        ctc_dkit_serdes_wr.type = CTC_DKIT_SERDES_RX;
        /*1*/
        ctc_dkit_serdes_wr.addr_offset = 1;
        ctc_greatbelt_dkit_misc_read_serdes(&ctc_dkit_serdes_wr);
        if (enable)
        {
            ctc_dkit_serdes_wr.data |= 0x60;
        }
        else
        {
            ctc_dkit_serdes_wr.data &= 0xff9f;
        }
        ctc_greatbelt_dkit_misc_write_serdes(&ctc_dkit_serdes_wr);
        /*2*/
        ctc_dkit_serdes_wr.addr_offset = 0x1f;
        ctc_greatbelt_dkit_misc_read_serdes(&ctc_dkit_serdes_wr);
        if (enable)
        {
            ctc_dkit_serdes_wr.data  &= 0xfdff;
        }
        else
        {
            ctc_dkit_serdes_wr.data  |= 0x200;
        }
        ctc_greatbelt_dkit_misc_write_serdes(&ctc_dkit_serdes_wr);
        /*3*/
        ctc_dkit_serdes_wr.addr_offset = 0x27;
        ctc_greatbelt_dkit_misc_read_serdes(&ctc_dkit_serdes_wr);
        if (enable)
        {
            ctc_dkit_serdes_wr.data  &= 0xfbff;
            ctc_dkit_serdes_wr.data  |= 0x20;
        }
        else
        {
            ctc_dkit_serdes_wr.data  &= 0xff9f;
        }
        ctc_greatbelt_dkit_misc_write_serdes(&ctc_dkit_serdes_wr);
    }
    else
    {
        CTC_DKIT_PRINT("Not support external loopback!!!\n");
    }

    return CLI_SUCCESS;
}


STATIC int32
_ctc_greatbelt_dkit_misc_do_serdes_prbs(ctc_dkit_serdes_ctl_para_t* p_serdes_para, bool* pass)
{
#define TIMEOUT 20
    uint32 count = 0;
    uint8 time_out = 0;
    uint8 is_keep = 0;
    uint32 delay = 0;
    ctc_dkit_serdes_wr_t ctc_dkit_serdes_wr;

    sal_memset(&ctc_dkit_serdes_wr, 0, sizeof(ctc_dkit_serdes_wr_t));
    ctc_dkit_serdes_wr.serdes_id = p_serdes_para->serdes_id;
    ctc_dkit_serdes_wr.lchip= p_serdes_para->lchip;

    if (CTC_DKIT_SERDIS_CTL_ENABLE == p_serdes_para->para[0])
    {
        ctc_dkit_serdes_wr.type = CTC_DKIT_SERDES_TX;
        ctc_dkit_serdes_wr.addr_offset = 1; /*Transmit Test Control Register*/

        /*1. config*/
        ctc_greatbelt_dkit_misc_read_serdes(&ctc_dkit_serdes_wr);
        DKITS_BIT_SET(ctc_dkit_serdes_wr.data, 3);/*PCHKEN = 1*/
        SET_BIT_RANGE(ctc_dkit_serdes_wr.data, p_serdes_para->para[1], 0, 3)/*TPSEL*/
        if (p_serdes_para->para[1] == 8)
        {
            SET_BIT_RANGE(ctc_dkit_serdes_wr.data, 2, 11, 3);
        }
        else
        {
            SET_BIT_RANGE(ctc_dkit_serdes_wr.data, 0, 11, 3);
        }
        ctc_greatbelt_dkit_misc_write_serdes(&ctc_dkit_serdes_wr);
    }
    else if (CTC_DKIT_SERDIS_CTL_DISABLE == p_serdes_para->para[0])
    {
        ctc_dkit_serdes_wr.type = CTC_DKIT_SERDES_TX;
        ctc_dkit_serdes_wr.addr_offset = 1; /*Transmit Test Control Register*/

        /*1. config*/
        ctc_greatbelt_dkit_misc_read_serdes(&ctc_dkit_serdes_wr);
        DKITS_BIT_UNSET(ctc_dkit_serdes_wr.data, 3);/*PCHKEN = 0*/
        ctc_greatbelt_dkit_misc_write_serdes(&ctc_dkit_serdes_wr);
    }
    else if (CTC_DKIT_SERDIS_CTL_CEHCK == p_serdes_para->para[0])
    {
        is_keep = p_serdes_para->para[2];
        delay = p_serdes_para->para[3];
        if (delay)
        {
            sal_task_sleep(delay); /*delay before check*/
        }

        ctc_dkit_serdes_wr.type = CTC_DKIT_SERDES_RX;
        ctc_dkit_serdes_wr.addr_offset = 1; /*Receiver Test Control Register*/

        /*1. config pattern and enable*/
        ctc_greatbelt_dkit_misc_read_serdes(&ctc_dkit_serdes_wr);
        DKITS_BIT_SET(ctc_dkit_serdes_wr.data, 3);/*PCHKEN = 1*/
        SET_BIT_RANGE(ctc_dkit_serdes_wr.data, p_serdes_para->para[1], 0, 3)/*TPSEL*/
        SET_BIT_RANGE(ctc_dkit_serdes_wr.data, 0, 11, 3);
        ctc_greatbelt_dkit_misc_write_serdes(&ctc_dkit_serdes_wr);

        /*2. reset*/
        ctc_greatbelt_dkit_misc_read_serdes(&ctc_dkit_serdes_wr);
        DKITS_BIT_SET(ctc_dkit_serdes_wr.data, 4);/*PRST = 1*/
        ctc_greatbelt_dkit_misc_write_serdes(&ctc_dkit_serdes_wr);

        ctc_greatbelt_dkit_misc_read_serdes(&ctc_dkit_serdes_wr);
        DKITS_BIT_UNSET(ctc_dkit_serdes_wr.data, 4);/*PRST = 0*/
        ctc_greatbelt_dkit_misc_write_serdes(&ctc_dkit_serdes_wr);

        /*3. read result*/
        while (1)
        {
            ctc_greatbelt_dkit_misc_read_serdes(&ctc_dkit_serdes_wr);
            if ((DKITS_IS_BIT_SET(ctc_dkit_serdes_wr.data, 8))||(count>=TIMEOUT)) /*SYNCST*/
            {
                break;
            }
            count++;
        }
        if (count >= TIMEOUT)
        {
            *pass = FALSE;
            time_out = 1;
        }

        if (!DKITS_IS_BIT_SET(ctc_dkit_serdes_wr.data, 9)) /*ERRST*/
        {
            *pass = TRUE;
        }
        else
        {
            *pass = FALSE;
        }

        if (!is_keep)
        {
            /*4. disable*/
            ctc_greatbelt_dkit_misc_read_serdes(&ctc_dkit_serdes_wr);
            DKITS_BIT_UNSET(ctc_dkit_serdes_wr.data, 3);/*PCHKEN = 0*/
            ctc_greatbelt_dkit_misc_write_serdes(&ctc_dkit_serdes_wr);

            /*5. reset*/
            ctc_greatbelt_dkit_misc_read_serdes(&ctc_dkit_serdes_wr);
            DKITS_BIT_SET(ctc_dkit_serdes_wr.data, 4);/*PRST = 1*/
            ctc_greatbelt_dkit_misc_write_serdes(&ctc_dkit_serdes_wr);

            ctc_greatbelt_dkit_misc_read_serdes(&ctc_dkit_serdes_wr);
            DKITS_BIT_UNSET(ctc_dkit_serdes_wr.data, 4);/*PRST = 0*/
            ctc_greatbelt_dkit_misc_write_serdes(&ctc_dkit_serdes_wr);
        }

    }
    return time_out?CLI_ERROR:CLI_SUCCESS;
}


STATIC int32
_ctc_greatbelt_dkit_misc_serdes_prbs(ctc_dkit_serdes_ctl_para_t* p_serdes_para)
{
    int ret = CLI_SUCCESS;
    char* pattern[PATTERN_NUM] = {"PRBS7+","PRBS7-","PRBS15+","PRBS15-","PRBS23+","PRBS23-","PRBS31+","PRBS31-", "PRBS9"};
    char* enable = NULL;
    bool pass = FALSE;

    if ((p_serdes_para->para[1] > (PATTERN_NUM-1))
        || (CTC_DKIT_SERDIS_CTL_CEHCK == p_serdes_para->para[0] && (8 == p_serdes_para->para[1])))
    {
        CTC_DKIT_PRINT("Not support the pattern or action!!!\n");
        return CLI_ERROR;
    }

    ret = _ctc_greatbelt_dkit_misc_do_serdes_prbs(p_serdes_para, &pass);

    if ((CTC_DKIT_SERDIS_CTL_ENABLE == p_serdes_para->para[0])
           ||(CTC_DKIT_SERDIS_CTL_DISABLE == p_serdes_para->para[0]))
    {
        enable = (CTC_DKIT_SERDIS_CTL_ENABLE == p_serdes_para->para[0])? "Yes":"No";
        CTC_DKIT_PRINT("\n%-12s%-6s%-10s%-9s%-9s\n", "Serdes_ID", "Dir", "Pattern", "Enable", "Result");
        CTC_DKIT_PRINT("--------------------------------------------\n");
        CTC_DKIT_PRINT("%-12d%-6s%-10s%-9s%-9s\n", p_serdes_para->serdes_id, "TX", pattern[p_serdes_para->para[1]], enable, "  -");
    }
    else if (CTC_DKIT_SERDIS_CTL_CEHCK == p_serdes_para->para[0])
    {
        if (CLI_ERROR == ret)
        {
            enable = "Not Sync";
        }
        else
        {
            enable = pass? "Pass" : "Fail";
        }
        CTC_DKIT_PRINT("\n%-12s%-6s%-10s%-9s%-9s\n", "Serdes_ID", "Dir", "Pattern", "Enable", "Result");
        CTC_DKIT_PRINT("--------------------------------------------\n");
        CTC_DKIT_PRINT("%-12d%-6s%-10s%-9s%-9s\n", p_serdes_para->serdes_id, "RX", pattern[p_serdes_para->para[1]], "  -", enable);
    }

    return CLI_SUCCESS;
}

int32
_ctc_greatbelt_dkit_misc_serdes_eye(ctc_dkit_serdes_ctl_para_t* p_serdes_para)
{
#define RECEIVER_INTERNAL_STATUS_REG        0x1E
#define DFE_FUNCTION_CONTROL_REG1           0x1F
#define DFE_FUNCTION_CONTROL_REG2           0x20

#define READY_TO_READ_E3                    13U
#define READY_TO_READ_E6_0                  14U
#define READY_TO_READ_E6_1                  15U
#define INPUT_DATA_TOO_FAST                 12U
#define DPC_COMVERGENCE_COMPLETE            9U
#define DAC_COMVERGENCE_COMPLETE            8U
#define DDC_COMVERGENCE_COMPLETE            7U
#define EYE_AMPLITUDE_ERROR                 6U
#define EYE_WIDTH_ERROR                     5U
#define DFE_TRAINING_COMPLETE               4U
#define VGA_LOCKED_FIRST                    3U
#define ROTATOR_OFFSET_CALIBRATION_COMPLETE 2U
#define DQCC_INITIAL_CALIBRATION_COMPLETE   1U
#define DC_OFFSET_CALIBRATION_COMPLETE      0U

    uint32 times = 0;
    uint32 count = 0;
    uint32 i = 0;
    uint16 data = 0;
    uint16 def_ctl = 0;
    uint8  err = 0;

    int8  an = 0;
    int8  ap = 0;
    int8  amin = 0;
    int32 eye_open = 0;
    int32 an_total = 0;
    int32 ap_total = 0;
    int32 amin_total = 0;
    int32 eye_open_total = 0;

    uint32 width = 0;
    uint32 az = 0;
    uint32 oes = 0;
    uint32 ols = 0;
    uint32 width_total = 0;
    uint32 az_total = 0;
    uint32 oes_total = 0;
    uint32 ols_total = 0;
    uint32 deduced_width = 0;
    uint32 deduced_width_total = 0;

    ctc_dkit_serdes_wr_t ctc_dkit_serdes_para;
    uint8 lane_idx = 0;

    DKITS_PTR_VALID_CHECK(p_serdes_para);
    if (CTC_DKIT_SERDIS_EYE_WIDTH_SLOW == p_serdes_para->para[0])
    {
        CTC_DKIT_PRINT("Function not support in this chip!!!\n");
        return CLI_SUCCESS;
    }

    times = p_serdes_para->para[1];
    if (0 == times)
    {
        times = 10;
    }

    sal_memset(&ctc_dkit_serdes_para, 0, sizeof(ctc_dkit_serdes_wr_t));
    ctc_dkit_serdes_para.serdes_id = p_serdes_para->serdes_id;
    ctc_dkit_serdes_para.lchip = p_serdes_para->lchip;
    ctc_dkit_serdes_para.type = CTC_DKIT_SERDES_RX;

    lane_idx = lane_serdes_mapping[p_serdes_para->serdes_id];
    if (lane_idx < 4)
    {
        ctc_dkit_serdes_para.addr_offset = DFE_FUNCTION_CONTROL_REG1;
    }
    else
    {
        ctc_dkit_serdes_para.addr_offset = DFE_FUNCTION_CONTROL_REG2;
    }
    ctc_greatbelt_dkit_misc_read_serdes(&ctc_dkit_serdes_para);
    def_ctl = ctc_dkit_serdes_para.data;
    if (CTC_DKIT_EYE_METRIC_PRECISION_TYPE_E3 == p_serdes_para->precision)
    {
        /*FBER3*/
        DKITS_BIT_UNSET(ctc_dkit_serdes_para.data, 8);
    }
    else
    {
        /*FBER6*/
        DKITS_BIT_SET(ctc_dkit_serdes_para.data, 8);
    }
    ctc_greatbelt_dkit_misc_write_serdes(&ctc_dkit_serdes_para);


    do
    {
        err = 0;
        ctc_dkit_serdes_para.addr_offset = RECEIVER_INTERNAL_STATUS_REG;
        ctc_greatbelt_dkit_misc_read_serdes(&ctc_dkit_serdes_para);
        if (!DKITS_IS_BIT_SET(data, DC_OFFSET_CALIBRATION_COMPLETE))
        {
             /*err = 1;*/
        }
        if (!DKITS_IS_BIT_SET(data, DQCC_INITIAL_CALIBRATION_COMPLETE))
        {
             /*err = 1;*/
        }
        if (!DKITS_IS_BIT_SET(data, ROTATOR_OFFSET_CALIBRATION_COMPLETE))
        {
             /*err = 1;*/
        }
        if (!DKITS_IS_BIT_SET(data, VGA_LOCKED_FIRST))
        {
             /*err = 1;*/
        }
        if (!DKITS_IS_BIT_SET(data, DFE_TRAINING_COMPLETE))
        {
             /*err = 1;*/
        }
#if 0
         /*no need check*/
        if (DBG_IS_BIT_SET(data, EYE_WIDTH_ERROR))
        {
            CTC_DKIT_PRINT(" Eye width error.\n");
             /*err = 1;*/
        }
        if (DBG_IS_BIT_SET(data, EYE_AMPLITUDE_ERROR))
        {
            CTC_DKIT_PRINT(" Eye amplitude error.\n");
             /*err = 1;*/
        }
#endif
        if (!DKITS_IS_BIT_SET(data, DDC_COMVERGENCE_COMPLETE))
        {
             /*err = 1;*/
        }
        if (!DKITS_IS_BIT_SET(data, DAC_COMVERGENCE_COMPLETE))
        {
             /*err = 1;*/
        }
#if 0
         /* DPC Convergence Complete. Note this bit only valid for HSS15GB/HSS28G of Cu32 (GoldenGate)*/
        if (!DBG_IS_BIT_SET(data, DPC_COMVERGENCE_COMPLETE))
        {
            CTC_DKIT_PRINT(" DPC comvergence is not Complete.\n");
             /*err = 1;*/
        }
#endif
        if (DKITS_IS_BIT_SET(data, INPUT_DATA_TOO_FAST))
        {
             /*err = 1;*/
        }

        if (((CTC_DKIT_EYE_METRIC_PRECISION_TYPE_E3 == p_serdes_para->precision) && (!DKITS_IS_BIT_SET(data, READY_TO_READ_E3)))
            || ((CTC_DKIT_EYE_METRIC_PRECISION_TYPE_E6 == p_serdes_para->precision)
        && (!DKITS_IS_BIT_SET(data, READY_TO_READ_E6_0) || !DKITS_IS_BIT_SET(data, READY_TO_READ_E6_1))))
        {
            err = 1;
        }

        if (err)
        {
            count++;
            if (count >= 50)
            {
                CTC_DKIT_PRINT(" Digital eye measure is not ready!!! Receiver Internal Status Reg value = 0x%x\n", data);
                break;
            }
            sal_udelay(100);
        }
    }
    while (err);

    if ((CTC_DKIT_SERDIS_EYE_HEIGHT == p_serdes_para->para[0])
        || (CTC_DKIT_SERDIS_EYE_ALL == p_serdes_para->para[0]))
    {
        CTC_DKIT_PRINT("\n Eye height measure(%s)\n",
                       (CTC_DKIT_EYE_METRIC_PRECISION_TYPE_E3 == p_serdes_para->precision) ? "1E-3" : "1E-6");
        CTC_DKIT_PRINT(" ---------------------------------------------------------\n");
        CTC_DKIT_PRINT(" %-10s%-10s%-10s%-10s%s\n", "No.", "An", "Ap", "Amin", "EyeOpen");
        CTC_DKIT_PRINT(" ---------------------------------------------------------\n");

        for (i = 0; i < times; i++)
        {
            ctc_dkit_serdes_para.addr_offset = 0x08;
            ctc_greatbelt_dkit_misc_read_serdes(&ctc_dkit_serdes_para);
            DKITS_BIT_SET(ctc_dkit_serdes_para.data, 5);
            ctc_greatbelt_dkit_misc_write_serdes(&ctc_dkit_serdes_para);

            ctc_dkit_serdes_para.addr_offset = 0x12;
            ctc_greatbelt_dkit_misc_read_serdes(&ctc_dkit_serdes_para);
            an = _ctc_greatbelt_dkit_misc_uint2int((ctc_dkit_serdes_para.data >> 8) & 0xFF, 7);
            ap = _ctc_greatbelt_dkit_misc_uint2int(ctc_dkit_serdes_para.data & 0xFF, 7);

            ctc_dkit_serdes_para.addr_offset = 0x13;
            ctc_greatbelt_dkit_misc_read_serdes(&ctc_dkit_serdes_para);
            amin = _ctc_greatbelt_dkit_misc_uint2int(ctc_dkit_serdes_para.data & 0xFF, 7);

             /*1.0 means fully open eye with no high frequency loss*/
            if (ap != 0)
            {
                eye_open = (amin * 1000) / ap;
            }
            eye_open_total += eye_open;
            an_total += an;
            ap_total += ap;
            amin_total += amin;

            ctc_dkit_serdes_para.addr_offset = 0x08;
            ctc_greatbelt_dkit_misc_read_serdes(&ctc_dkit_serdes_para);
            DKITS_BIT_UNSET(ctc_dkit_serdes_para.data, 5);
            ctc_greatbelt_dkit_misc_write_serdes(&ctc_dkit_serdes_para);

            CTC_DKIT_PRINT(" %-10d%-10d%-10d%-10d0.%d\n",
                           i + 1, an, ap, amin,
                           (eye_open < 0) ? - eye_open : eye_open);
        }

        CTC_DKIT_PRINT(" ---------------------------------------------------------\n");
        CTC_DKIT_PRINT(" %-10s%-10d%-10d%-10d0.%d\n\n", "Avg",
                       an_total / times, ap_total / times, amin_total / times,
                       (eye_open_total / times) < 0 ? - (eye_open_total / times) : (eye_open_total / times));

        CTC_DKIT_PRINT("\n");
    }

    if ((CTC_DKIT_SERDIS_EYE_WIDTH == p_serdes_para->para[0])
        || (CTC_DKIT_SERDIS_EYE_ALL == p_serdes_para->para[0]))
    {
        CTC_DKIT_PRINT("\n Eye width measure(%s)\n",
                       (CTC_DKIT_EYE_METRIC_PRECISION_TYPE_E3 == p_serdes_para->precision) ? "1E-3" : "1E-6");
        CTC_DKIT_PRINT(" --------------------------------------------------------------\n");
        CTC_DKIT_PRINT(" %-10s%-10s%-10s%-10s%-10s%s\n", "No.", "Width", "Az", "Oes", "Ols", "DeducedWidth");
        CTC_DKIT_PRINT(" --------------------------------------------------------------\n");

        for (i = 0; i < times; i++)
        {
            ctc_dkit_serdes_para.addr_offset = 0x1F;
            ctc_greatbelt_dkit_misc_read_serdes(&ctc_dkit_serdes_para);
            DKITS_BIT_UNSET(ctc_dkit_serdes_para.data, 13);
            ctc_greatbelt_dkit_misc_write_serdes(&ctc_dkit_serdes_para);

            ctc_dkit_serdes_para.addr_offset = 0x2A;
            ctc_greatbelt_dkit_misc_read_serdes(&ctc_dkit_serdes_para);
            width = (ctc_dkit_serdes_para.data >> 10) & 0x1F;

            ctc_dkit_serdes_para.addr_offset = 0x13;
            ctc_greatbelt_dkit_misc_read_serdes(&ctc_dkit_serdes_para);
            az = (ctc_dkit_serdes_para.data >> 8) & 0x1F;

            ctc_dkit_serdes_para.addr_offset = 0x1D;
            ctc_greatbelt_dkit_misc_read_serdes(&ctc_dkit_serdes_para);
            oes = (ctc_dkit_serdes_para.data >> 6) & 0x1F;
            ols = ctc_dkit_serdes_para.data >> 11;
            deduced_width = oes + ols;

            width_total += width;
            az_total += az;
            oes_total += oes;
            ols_total += ols;
            deduced_width_total += deduced_width;

            ctc_dkit_serdes_para.addr_offset = 0x1F;
            ctc_greatbelt_dkit_misc_read_serdes(&ctc_dkit_serdes_para);
            DKITS_BIT_SET(ctc_dkit_serdes_para.data, 13);
            ctc_greatbelt_dkit_misc_write_serdes(&ctc_dkit_serdes_para);

            CTC_DKIT_PRINT(" %-10d%-10d%-10d%-10d%-10d%-10d\n", i + 1, width, az, oes, ols, deduced_width);
        }

        CTC_DKIT_PRINT(" --------------------------------------------------------------\n");
        CTC_DKIT_PRINT(" %-10s%-10d%-10d%-10d%-10d%-10d\n\n", "Avg",
                       width_total / times, az_total / times,
                       oes_total / times, ols_total / times, deduced_width_total / times);
    }
    if (lane_idx < 4)
    {
        ctc_dkit_serdes_para.addr_offset = DFE_FUNCTION_CONTROL_REG1;
    }
    else
    {
        ctc_dkit_serdes_para.addr_offset = DFE_FUNCTION_CONTROL_REG2;
    }
    ctc_dkit_serdes_para.data = def_ctl;
    ctc_greatbelt_dkit_misc_write_serdes(&ctc_dkit_serdes_para);

    return DRV_E_NONE;
}


int32
_ctc_greatbelt_dkit_misc_serdes_scope(ctc_dkit_serdes_ctl_para_t* p_serdes_para)
{
    sal_file_t p_f = NULL;
    char str0[50] = {0}, str1[50] = {0}, str2[50] = {0};
    int8  an[100] = {0};
    int8* an_direct = NULL;
    int8* an_plot = NULL;
    int32 ret = CLI_SUCCESS;
    int16  j = 0;
    uint16 i = 0, k = 0, n = 0, m = 0, count = 0, plot_idx = 0;
    uint16 reg_0x8_def = 0, reg_0x1f_def = 0, reg_0x6_def = 0, reg_0x2b_def = 0, reg_0x3_def = 0;
    ctc_dkit_serdes_wr_t ctc_dkit_serdes_wr;

    an_direct = (int8*)sal_malloc(5000 * sizeof(int8));
    an_plot = (int8*)sal_malloc(5000 * sizeof(int8));

    if (!an_direct || !an_plot)
    {
        goto clean;
    }

    sal_memset(an_direct, 0, 5000 * sizeof(int8));
    sal_memset(an_plot, 0, 5000 * sizeof(int8));

    sal_memset(&ctc_dkit_serdes_wr, 0, sizeof(ctc_dkit_serdes_wr_t));
    ctc_dkit_serdes_wr.serdes_id = p_serdes_para->serdes_id;
    ctc_dkit_serdes_wr.lchip = p_serdes_para->lchip;
    ctc_dkit_serdes_wr.type = CTC_DKIT_SERDES_RX;

    p_f = sal_fopen("./serdes_scope.txt", "wt");
    if (NULL == p_f)
    {
        CTC_DKIT_PRINT(" Write serdes scope file: %s failed!\n\n", "./serdes_scope.txt");
        ret = DRV_E_FATAL_EXCEP;
        goto clean;
    }

    sal_fprintf(p_f, "%-4s\t%s\n", "x", "y");

    /* 1. Read the register cfg value for the future recovery */
    /* rd_reg(0x08, 0x1F, 0x06, 0x2B, 0x03) and save temporaly; */
    ctc_dkit_serdes_wr.addr_offset = 0x8;
    ctc_greatbelt_dkit_misc_read_serdes(&ctc_dkit_serdes_wr);
    reg_0x8_def = ctc_dkit_serdes_wr.data;

    ctc_dkit_serdes_wr.addr_offset = 0x1F;
    ctc_greatbelt_dkit_misc_read_serdes(&ctc_dkit_serdes_wr);
    reg_0x1f_def = ctc_dkit_serdes_wr.data;

    ctc_dkit_serdes_wr.addr_offset = 0x6;
    ctc_greatbelt_dkit_misc_read_serdes(&ctc_dkit_serdes_wr);
    reg_0x6_def = ctc_dkit_serdes_wr.data;

    ctc_dkit_serdes_wr.addr_offset = 0x2B;
    ctc_greatbelt_dkit_misc_read_serdes(&ctc_dkit_serdes_wr);
    reg_0x2b_def = ctc_dkit_serdes_wr.data;

    ctc_dkit_serdes_wr.addr_offset = 0x3;
    ctc_greatbelt_dkit_misc_read_serdes(&ctc_dkit_serdes_wr);
    reg_0x3_def = ctc_dkit_serdes_wr.data;

    /* 2. Kick off an read */
    /* wr_reg(0x08[8:7,5])=0; Note: For HSS12G of Greatbelt, should be wr_reg(0x08[8:5])=0 */
    ctc_dkit_serdes_wr.addr_offset = 0x8;
    ctc_greatbelt_dkit_misc_read_serdes(&ctc_dkit_serdes_wr);
    DKITS_BIT_UNSET(ctc_dkit_serdes_wr.data, 8);
    DKITS_BIT_UNSET(ctc_dkit_serdes_wr.data, 7);
    DKITS_BIT_UNSET(ctc_dkit_serdes_wr.data, 6);
    DKITS_BIT_SET(ctc_dkit_serdes_wr.data, 5);
    ctc_greatbelt_dkit_misc_write_serdes(&ctc_dkit_serdes_wr);

    /* 3. Ensure all measure points are collected w/o rejection caused by the not-random detector */
    /* wr_reg(0x1F[2,12])=0, then wr_reg(0x08[3])=0. */
    ctc_dkit_serdes_wr.addr_offset = 0x1F;
    ctc_greatbelt_dkit_misc_read_serdes(&ctc_dkit_serdes_wr);
    DKITS_BIT_UNSET(ctc_dkit_serdes_wr.data, 2);
    DKITS_BIT_UNSET(ctc_dkit_serdes_wr.data, 12);
    ctc_greatbelt_dkit_misc_write_serdes(&ctc_dkit_serdes_wr);

    ctc_dkit_serdes_wr.addr_offset = 0x8;
    ctc_greatbelt_dkit_misc_read_serdes(&ctc_dkit_serdes_wr);
    DKITS_BIT_UNSET(ctc_dkit_serdes_wr.data, 3);
    ctc_greatbelt_dkit_misc_write_serdes(&ctc_dkit_serdes_wr);

    /* 4. note [14:10] is a signed value (-16 to +15) */
    /* wr_reg(RX_0x06[14:10])= 8; */
    ctc_dkit_serdes_wr.addr_offset = 0x6;
    ctc_greatbelt_dkit_misc_read_serdes(&ctc_dkit_serdes_wr);
    DKITS_BIT_UNSET(ctc_dkit_serdes_wr.data, 10);
    DKITS_BIT_UNSET(ctc_dkit_serdes_wr.data, 11);
    DKITS_BIT_UNSET(ctc_dkit_serdes_wr.data, 12);
    DKITS_BIT_SET(ctc_dkit_serdes_wr.data, 13);
    DKITS_BIT_UNSET(ctc_dkit_serdes_wr.data, 14);
    ctc_greatbelt_dkit_misc_write_serdes(&ctc_dkit_serdes_wr);

    /* 5. wr_reg(0x1F[7])=0; */
    ctc_dkit_serdes_wr.addr_offset = 0x1F;
    ctc_greatbelt_dkit_misc_read_serdes(&ctc_dkit_serdes_wr);
    DKITS_BIT_UNSET(ctc_dkit_serdes_wr.data, 7);
    ctc_greatbelt_dkit_misc_write_serdes(&ctc_dkit_serdes_wr);

    /* 6. use PRBS7 */
    /* wr_reg(0x2B[4:3,0]) = {2'b00, 1'b1}; wr_reg(0x2E[14:0])=126; */
    ctc_dkit_serdes_wr.addr_offset = 0x2B;
    ctc_greatbelt_dkit_misc_read_serdes(&ctc_dkit_serdes_wr);
    DKITS_BIT_UNSET(ctc_dkit_serdes_wr.data, 3);
    DKITS_BIT_UNSET(ctc_dkit_serdes_wr.data, 4);
    DKITS_BIT_SET(ctc_dkit_serdes_wr.data, 0);
    ctc_greatbelt_dkit_misc_write_serdes(&ctc_dkit_serdes_wr);

    /* 1111110 */
    ctc_dkit_serdes_wr.addr_offset = 0x2E;
    ctc_greatbelt_dkit_misc_read_serdes(&ctc_dkit_serdes_wr);
    ctc_dkit_serdes_wr.data = 126;/*1111110, bit 15 is Rsvd*/
    ctc_greatbelt_dkit_misc_write_serdes(&ctc_dkit_serdes_wr);

    /* Set amplitude rotator selector */
    /* wr_reg(0x03[5])=1; */
    ctc_dkit_serdes_wr.addr_offset = 0x03;
    ctc_greatbelt_dkit_misc_read_serdes(&ctc_dkit_serdes_wr);
    DKITS_BIT_SET(ctc_dkit_serdes_wr.data, 5);
    ctc_greatbelt_dkit_misc_write_serdes(&ctc_dkit_serdes_wr);

    /* scopemode ='001' */
    /* wr_reg(0x08[11:9]) = 3'b001; */
    ctc_dkit_serdes_wr.addr_offset = 0x08;
    ctc_greatbelt_dkit_misc_read_serdes(&ctc_dkit_serdes_wr);
    DKITS_BIT_SET(ctc_dkit_serdes_wr.data, 9);
    DKITS_BIT_UNSET(ctc_dkit_serdes_wr.data, 10);
    DKITS_BIT_UNSET(ctc_dkit_serdes_wr.data, 11);
    DKITS_BIT_UNSET(ctc_dkit_serdes_wr.data, 13);
    ctc_greatbelt_dkit_misc_write_serdes(&ctc_dkit_serdes_wr);

    sal_memset(an_plot, 0, 5000 * sizeof(int8));

    for (i = 0; i < 16; i++)
    {
        for (j = 28; j >= 0; j = j - 4)
        {
            /* Default amplitude offset target 180 degree */
            /* wr_reg(RX_0x03[4:0])=j; */
            ctc_dkit_serdes_wr.addr_offset = 0x3;
            ctc_greatbelt_dkit_misc_read_serdes(&ctc_dkit_serdes_wr);
            ctc_dkit_serdes_wr.data &= 0xFFE0;
            ctc_dkit_serdes_wr.data |= j;
            ctc_greatbelt_dkit_misc_write_serdes(&ctc_dkit_serdes_wr);

            /* Apply */
            /* wr_reg(0x03[6])=1'b1; */
            DKITS_BIT_SET(ctc_dkit_serdes_wr.data, 6);
            ctc_greatbelt_dkit_misc_write_serdes(&ctc_dkit_serdes_wr);

            /* Clear apply */
            DKITS_BIT_UNSET(ctc_dkit_serdes_wr.data, 6);
            ctc_greatbelt_dkit_misc_write_serdes(&ctc_dkit_serdes_wr);

            /* loop until An stable */
            for (k = 0; k < 99; k++)
            {
                count = 0;
                do
                {
                    if (count++ > 50)
                    {
                        break;
                    }

                    /* Read several times (might be 10 times or more) until the An value is changing by only a small amount. */
                    ctc_dkit_serdes_wr.addr_offset = 0x8;
                    ctc_greatbelt_dkit_misc_read_serdes(&ctc_dkit_serdes_wr);
                    /* wr_reg(0x08[14])=1'b1; */
                    DKITS_BIT_SET(ctc_dkit_serdes_wr.data, 14);
                    ctc_greatbelt_dkit_misc_write_serdes(&ctc_dkit_serdes_wr);
                    /* wr_reg(0x08[14])=1'b0; */
                    DKITS_BIT_UNSET(ctc_dkit_serdes_wr.data, 14);
                    ctc_greatbelt_dkit_misc_write_serdes(&ctc_dkit_serdes_wr);

                    ctc_greatbelt_dkit_misc_read_serdes(&ctc_dkit_serdes_wr);
                } while (!DKITS_IS_BIT_SET(ctc_dkit_serdes_wr.data, 15));
                /* poll(0x08[15]) until it turn to be 1'b1; */

                ctc_dkit_serdes_wr.addr_offset = 0x9;
                ctc_greatbelt_dkit_misc_read_serdes(&ctc_dkit_serdes_wr);
                /* An = rd_reg(0x09) >>8; */
                an[k] = _ctc_greatbelt_dkit_misc_int2uint((ctc_dkit_serdes_wr.data >> 8) & 0xFF, 7);

                 /*CTC_DKIT_PRINT("i:%u, j:%u, k:%u, an:%d\n", i, j, k, an[k]);*/

                if (0 == ((k + 1) % 10))
                {
                    for (n = 0; n < 9; n++)
                    {
                        for (m = n + 1; m < 10; m++)
                        {
                            if (((an[(((k + 1) / 10) - 1) * 10 + n] - an[(((k + 1) / 10) - 1) * 10 + m] > 2))
                               || ((an[(((k + 1) / 10) - 1) * 10 + n] - an[(((k + 1) / 10) - 1) * 10 + m] < -2)))
                            {
                                goto DROP_RST;
                            }
                        }
                    }
                    k = (sal_rand() % 10) + (((k + 1) / 10) - 1) * 10;
                    if ((0 == plot_idx) || (an[k] != an_plot[plot_idx - 1]))
                    {
                        break;
                    }
                }
DROP_RST:
                continue;
            }
            /* endloop */

            an_plot[plot_idx] = an[k];
            ctc_dkit_serdes_wr.addr_offset = 0x12;
            ctc_greatbelt_dkit_misc_read_serdes(&ctc_dkit_serdes_wr);
            an_direct[plot_idx] = _ctc_greatbelt_dkit_misc_uint2int(0xFF & (ctc_dkit_serdes_wr.data >> 8), 7);

            sal_sprintf(str0, "%d", k);
            sal_sprintf(str1, "%d", an_plot[plot_idx]);
            sal_sprintf(str2, "%d", an_direct[plot_idx]);

            sal_fprintf(p_f, "%-4u\t%-7s\t%s\n", plot_idx + 1, str1, str2);
            plot_idx++;
        }

        count = 0;
        do
        {   if (count++ > 50)
            {
                break;
            }
            ctc_dkit_serdes_wr.addr_offset = 0x2B;
            ctc_greatbelt_dkit_misc_read_serdes(&ctc_dkit_serdes_wr);
        } while (!DKITS_IS_BIT_SET(ctc_dkit_serdes_wr.data, 2));
        /* poll(RX_0x2B[2]) until it turn to be 1'b1; */

        /* wr_reg(0x2B[1])=1'b1; */
        ctc_dkit_serdes_wr.addr_offset = 0x2B;
        ctc_greatbelt_dkit_misc_read_serdes(&ctc_dkit_serdes_wr);
        DKITS_BIT_SET(ctc_dkit_serdes_wr.data, 1);
        ctc_greatbelt_dkit_misc_write_serdes(&ctc_dkit_serdes_wr);
    }
    sal_fclose(p_f);
    CTC_DKIT_PRINT(" Output serdes scope file:%s.\n", "./serdes_scope.txt");

    /* Recover the old values for the following registers */
    ctc_dkit_serdes_wr.addr_offset = 0x3;
    ctc_dkit_serdes_wr.data = reg_0x3_def;
    ctc_greatbelt_dkit_misc_write_serdes(&ctc_dkit_serdes_wr);

    ctc_dkit_serdes_wr.addr_offset = 0x2B;
    ctc_dkit_serdes_wr.data = reg_0x2b_def;
    ctc_greatbelt_dkit_misc_write_serdes(&ctc_dkit_serdes_wr);

    ctc_dkit_serdes_wr.addr_offset = 0x6;
    ctc_dkit_serdes_wr.data = reg_0x6_def;
    ctc_greatbelt_dkit_misc_write_serdes(&ctc_dkit_serdes_wr);

    ctc_dkit_serdes_wr.addr_offset = 0x1F;
    ctc_dkit_serdes_wr.data = reg_0x1f_def;
    ctc_greatbelt_dkit_misc_write_serdes(&ctc_dkit_serdes_wr);

    ctc_dkit_serdes_wr.addr_offset = 0x8;
    ctc_dkit_serdes_wr.data = reg_0x8_def;
    ctc_greatbelt_dkit_misc_write_serdes(&ctc_dkit_serdes_wr);
    DKITS_BIT_SET(ctc_dkit_serdes_wr.data, 0);
    ctc_greatbelt_dkit_misc_write_serdes(&ctc_dkit_serdes_wr);
    DKITS_BIT_UNSET(ctc_dkit_serdes_wr.data, 0);
    ctc_greatbelt_dkit_misc_write_serdes(&ctc_dkit_serdes_wr);

clean:
    if (an_plot)
    {
        sal_free(an_plot);
    }
    if (an_direct)
    {
        sal_free(an_direct);
    }

    return ret;
}


int32
ctc_greatbelt_dkit_misc_serdes_ctl(void* p_para)
{
    ctc_dkit_serdes_ctl_para_t* p_serdes_para = (ctc_dkit_serdes_ctl_para_t*)p_para;

    DKITS_PTR_VALID_CHECK(p_serdes_para);
    CTC_DKIT_LCHIP_CHECK(p_serdes_para->lchip);
    switch (p_serdes_para->type)
    {
        case CTC_DKIT_SERDIS_CTL_LOOPBACK:
            _ctc_greatbelt_dkit_misc_serdes_loopback(p_serdes_para);
            break;
        case CTC_DKIT_SERDIS_CTL_PRBS:
            _ctc_greatbelt_dkit_misc_serdes_prbs(p_serdes_para);
            break;
        case CTC_DKIT_SERDIS_CTL_EYE:
            _ctc_greatbelt_dkit_misc_serdes_eye(p_serdes_para);
            break;
        case CTC_DKIT_SERDIS_CTL_EYE_SCOPE:
            _ctc_greatbelt_dkit_misc_serdes_scope(p_serdes_para);
            break;
        default:
            CTC_DKIT_PRINT("Function not support in this chip!!!\n");
            break;
    }

    return CLI_SUCCESS;
}


STATIC int32
_ctc_greatbelt_dkit_misc_integrity_calc_poly(uint8 chip_id, uint32 index, uint8 b_exted, uint8 mode, char* poly_str, char* result_str)
{
#define MAX_OP_NUM 7
    uint8 op_time = 0;

    uint32 value[MAX_OP_NUM + 1] = {0};
    char op_code[MAX_OP_NUM] = {0};
    char tbl_str[64] = {0};
    char fld_str[64] = {0};

    char* op_str = NULL;
    char* head = NULL;

    tbls_id_t tbl_id = 0;
    fld_id_t fld_id = 0;

    uint32 cmd = 0;
    uint32 result = 0;
    uint32 cnt = 0;

    uint32 poly_idx = 0;

    DRV_PTR_VALID_CHECK(poly_str);
    head = sal_strstr(poly_str, "[");
    if (NULL == head)
    {
        sal_sscanf(poly_str, "%d", &result);
        return result;
    }
    else
    {
        while ((op_str = sal_strstr(head, "{op ")))
        {
            op_code[op_time] = op_str[4];
            if (b_exted)
            {
                sal_sscanf(head, "\x5b%[^.].%d.%[^\x5d]", tbl_str, &poly_idx, fld_str);
            }
            else
            {
                sal_sscanf(head, "\x5b%[^.].%[^\x5d]", tbl_str, fld_str);
                poly_idx = index;
            }

            if (mode)
            {
                if (DRV_E_NONE != drv_greatbelt_get_tbl_id_by_string(&tbl_id, tbl_str))
                {
                    CTC_DKIT_PRINT("can't find tbl %s \n", tbl_str);
                    return DRV_E_INVALID_TBL;
                }

                if (DRV_E_NONE != drv_greatbelt_get_field_id_by_string(tbl_id, &fld_id, fld_str))
                {
                    CTC_DKIT_PRINT("can find field %s\n", fld_str);
                    return DRV_E_INVALID_FLD;
                }

                cmd = DRV_IOR(tbl_id, fld_id);
                if (DRV_E_NONE != DRV_IOCTL(chip_id, poly_idx, cmd, &value[op_time]))
                {
                    CTC_DKIT_PRINT("read tbl_str %s fld_str %s error\n", tbl_str, fld_str);
                }
            }

            if (result_str)
            {
                sal_sprintf(result_str + sal_strlen(result_str), "[%s.%d.%s] {op %c} ", tbl_str, poly_idx, fld_str, op_code[op_time]);
            }

            head = sal_strstr(op_str, "[");
            sal_memset(tbl_str, 0, sizeof(tbl_str));
            sal_memset(fld_str, 0, sizeof(fld_str));
            op_str = NULL;
            op_time++;
            if (op_time > MAX_OP_NUM)
            {

            }
        }

        if (b_exted)
        {
            sal_sscanf(head, "\x5b%[^.].%d.%[^\x5d]", tbl_str, &poly_idx, fld_str);

        }
        else
        {
            sal_sscanf(head, "\x5b%[^.].%[^\x5d]", tbl_str, fld_str);
            poly_idx = index;
        }

        if (mode)
        {
            if (DRV_E_NONE != drv_greatbelt_get_tbl_id_by_string(&tbl_id, tbl_str))
            {
                CTC_DKIT_PRINT("can't find tbl %s \n", tbl_str);
                return DRV_E_INVALID_TBL;
            }

            if (DRV_E_NONE != drv_greatbelt_get_field_id_by_string(tbl_id, &fld_id, fld_str))
            {
                CTC_DKIT_PRINT("can find field %s\n", fld_str);
                return DRV_E_INVALID_FLD;
            }

            cmd = DRV_IOR(tbl_id, fld_id);
            if (DRV_E_NONE != DRV_IOCTL(chip_id, poly_idx, cmd, &value[op_time]))
            {
                CTC_DKIT_PRINT("read tbl_str %s fld_str %s error\n", tbl_str, fld_str);
            }
        }

        if (result_str)
        {
            sal_sprintf(result_str + sal_strlen(result_str), "[%s.%d.%s] ", tbl_str, poly_idx, fld_str);
        }

        cnt    = 0;
        result = value[cnt];

        while (cnt < op_time)
        {
            switch (op_code[cnt])
            {
            case '+':
                result += value[cnt + 1];
                break;

            case '-':
                result -= value[cnt + 1];
                break;

            case '&':
                result &= value[cnt + 1];
                break;

            case '|':
                result |= value[cnt + 1];
                break;

            default:
                break;
            }

            cnt++;
        }
    }

    return result;
}

STATIC int32
_ctc_greatbelt_dkit_misc_integrity_get_id_by_poly(char* poly_str, tbls_id_t* tbl_id, fld_id_t* fld_id)
{
    char tbl_str[64] = {0};
    char fld_str[64] = {0};
    char* head = NULL;

    head = sal_strstr(poly_str, "[");

    if (NULL == head)
    {
        return DRV_E_INVALID_PARAMETER;
    }
    else
    {
        sal_sscanf(head, "\x5b%[^.].%[^\x5d]", tbl_str, fld_str);
        if (DRV_E_NONE != drv_greatbelt_get_tbl_id_by_string(tbl_id, tbl_str))
        {
            CTC_DKIT_PRINT("can find table %s\n", tbl_str);
            return DRV_E_INVALID_TBL;
        }

        if (DRV_E_NONE != drv_greatbelt_get_field_id_by_string(*tbl_id, fld_id, fld_str))
        {
            CTC_DKIT_PRINT("can find field %s\n", fld_str);
            return DRV_E_INVALID_FLD;
        }
    }

    return DRV_E_NONE;
}

STATIC int
_ctc_greatbelt_dkit_misc_integrity_read_entry(uint8 chip_id, tbls_id_t id, uint32 index, uint32 offset, uint32* value)
{
    uint8 chipid_base  = 0;
    uint32 hw_addr_base = 0;
    uint32 entry_size   = 0;
    uint32 max_entry    = 0;
    uint32 hw_addr_offset      = 0;
    int32  ret = 0;

    if (MaxTblId_t <= id)
    {
        CTC_DKIT_PRINT("\nERROR! INVALID tbl/RegID! tbl/RegID: %d, file:%s line:%d function:%s\n",
                    id, __FILE__, __LINE__, __FUNCTION__);
        return CLI_ERROR;
    }

    entry_size      = TABLE_ENTRY_SIZE(id);
    max_entry       = TABLE_MAX_INDEX(id);
    hw_addr_base    = TABLE_DATA_BASE(id);

    if (index >= max_entry)
    {
        return CLI_ERROR;
    }

    /*TODO: dynamic table*/
    if ((4 == entry_size) || (8 == entry_size))
    { /*1w or 2w*/
        hw_addr_offset  = hw_addr_base + index * entry_size + offset;
    }
    else if (entry_size <= 4 * 4)
    { /*4w*/
        hw_addr_offset  = hw_addr_base + index * 4 * 4 + offset;
    }
    else if (entry_size <= 4 * 8)
    { /*8w*/
        hw_addr_offset  = hw_addr_base + index * 4 * 8 + offset;
    }
    /*
    else if (entry_size <= 4*12)
    {//12w
        hw_addr_offset  =   hw_addr_base + index * 4*12 + offset;
    }
    */
    else if (entry_size <= 4 * 16)
    { /*16w*/
        hw_addr_offset  = hw_addr_base + index * 4 * 16 + offset;
    }
    /*
    else if (entry_size <= 4*20)
    {//20w
        hw_addr_offset  =   hw_addr_base + index * 4*20 + offset;
    }
    else if (entry_size <= 4*24)
    {//24w
        hw_addr_offset  =   hw_addr_base + index * 4*24 + offset;
    }
    else if (entry_size <= 4*28)
    {//28w/
        hw_addr_offset  =   hw_addr_base + index * 4*28 + offset;
    }
    */
    else if (entry_size <= 4 * 32)
    { /*32w*/
        hw_addr_offset  = hw_addr_base + index * 4 * 32 + offset;
    }
    /*
    else if (entry_size <= 4*36)
    {//36w
        hw_addr_offset  =   hw_addr_base + index * 4*36 + offset;
    }
    else if (entry_size <= 4*40)
    {//40w/
        hw_addr_offset  =   hw_addr_base + index * 4*40 + offset;
    }
    */
    else if (entry_size <= 4 * 64)
    { /*64w*/
        hw_addr_offset  = hw_addr_base + index * 4 * 64 + offset;
    }

    ret = drv_greatbelt_get_chipid_base(&chipid_base);

    if (ret < DRV_E_NONE)
    {
        CTC_DKIT_PRINT("Get chipId base ERROR! Value = 0x%08x\n", chipid_base);
        return ret;
    }

    ret = drv_greatbelt_chip_read(chip_id - chipid_base, hw_addr_offset, value);

    if (ret < DRV_E_NONE)
    {
        CTC_DKIT_PRINT("0x%08x address read ERROR!\n", hw_addr_offset);
        return ret;
    }

    return CLI_SUCCESS;

}

STATIC int32
_ctc_greatbelt_dkit_misc_integrity_calc_inter_mask(uint8 chip_id, char* poly_str, uint32* cal_val)
{
    uint32 value[2] = {0};
    char op_code = 0;
    char tbl_str[64] = {0};

    char* op_str = NULL;
    char* head = NULL;
    tbls_id_t tbl_id = 0;
    uint32 result = 0;
    uint32 offset = 0;
    uint32 index  = 0;
    int32 ret = 0;

    DRV_PTR_VALID_CHECK(poly_str);
    head = sal_strstr(poly_str, "[");

    sal_sscanf(head, "[%[^.].%d.%d]", tbl_str, &index, &offset);

    if (DRV_E_NONE != drv_greatbelt_get_tbl_id_by_string(&tbl_id, tbl_str))
    {
        CTC_DKIT_PRINT("can't find tbl %s \n", tbl_str);
        return DRV_E_INVALID_TBL;
    }

    ret = _ctc_greatbelt_dkit_misc_integrity_read_entry(chip_id, tbl_id, index, offset, &value[0]);
    if (DRV_E_NONE != ret)
    {
        CTC_DKIT_PRINT("read tbl_str %s index %d offset %d error\n", tbl_str, index, offset);
        return ret;
    }

     /*DRV_IF_ERROR_RETURN(read_entry(chip_id, tbl_id, index, offset, &value[0]));*/

    op_str = sal_strstr(head, "{op ");
    if (NULL != op_str)
    {
        op_code = op_str[4];
        head = sal_strstr(op_str, "[");
        sal_sscanf(head, "[0x%x]", &value[1]);
    }
    else
    {

    }

    result = value[0];

    switch (op_code)
    {
    case '+':
        result += value[1];
        break;

    case '-':
        result -= value[1];
        break;

    case '&':
        result &= value[1];
        break;

    case '|':
        result |= value[1];
        break;

    default:
        break;
    }

    *cal_val = result;
    return DRV_E_NONE;
}

STATIC int32
_ctc_greatbelt_dkit_misc_integrity_result_do(uint8 chip_id, char* line, char* result, uint8* check_pass)
{
#define CHECK_NONE      0
#define CHECK_STATUS    1
#define CHECK_LINK_LIST 2
#define CHECK_INTR      3

    uint8 op_code = CTC_DKIT_MISC_INTEGRITY_OP_NONE;

    char* poly_str = NULL;
    char* poly_str_right = NULL;

    int32 exp_val = 0;
    int32 rel_val = 0;

    uint32 counter  = 0;
    uint32 index    = 0;
    uint32 next_index = 0;

    uint32 tbl_id = 0;
    uint32 fld_id = 0;
    uint32 cmd = 0;

    static uint8 integrity_stat = CHECK_NONE;
    static uint32 exp_head = 0;
    static uint32 exp_tail = 0;
    static uint32 exp_cnt = 0;

    poly_str = (char*)sal_malloc(MAX_PER_LINE * sizeof(char));
    poly_str_right = (char*)sal_malloc(MAX_PER_LINE * sizeof(char));

    if (!poly_str || !poly_str_right)
    {
        goto EXIT;
    }

    sal_memset(poly_str, 0, MAX_PER_LINE * sizeof(char));
    sal_memset(poly_str_right, 0, MAX_PER_LINE * sizeof(char));

    if (('\r' == line[0]) && ('\n' == line[1]))
    {
        sal_strncpy(result, line, MAX_PER_LINE);
    }
    else if ('#' == line[0])
    {
        if (sal_strstr(line, "Status Check"))
        {
            integrity_stat = CHECK_STATUS;
        }
        else if (sal_strstr(line, "Link List Check Begin"))
        {
            integrity_stat = CHECK_LINK_LIST;
            sal_memset(bit_map, 0, sizeof(bit_map));
        }
        else if (sal_strstr(line, "Link List Check End"))
        {
            integrity_stat = CHECK_NONE;
        }
        else if (sal_strstr(line, "Interrupt Check"))
        {
            integrity_stat = CHECK_INTR;
        }

        sal_strncpy(result, line, MAX_PER_LINE);
    }
    else
    {
        /*get op code*/
        if (sal_strstr(line, ">="))
        {
            op_code = CTC_DKIT_MISC_INTEGRITY_OP_BE;
        }
        else if (sal_strstr(line, "<="))
        {
            op_code = CTC_DKIT_MISC_INTEGRITY_OP_LE;
        }
        else if (sal_strstr(line, ">"))
        {
            op_code = CTC_DKIT_MISC_INTEGRITY_OP_B;
        }
        else if (sal_strstr(line, "<"))
        {
            op_code = CTC_DKIT_MISC_INTEGRITY_OP_L;
        }
        else if (sal_strstr(line, "="))
        {
            op_code = CTC_DKIT_MISC_INTEGRITY_OP_E;
        }
        else
        {
            op_code = CTC_DKIT_MISC_INTEGRITY_OP_NONE;
        }

        if (CHECK_LINK_LIST != integrity_stat)
        {
            goto EXIT;
        }

        /*to calc poly*/
        if (CTC_DKIT_MISC_INTEGRITY_OP_NONE == op_code)
        {
            sal_strncpy(result, line, MAX_PER_LINE);
        }
        else
        {
            /*status check*/
            if (CHECK_STATUS == integrity_stat)
            {
                switch (op_code)
                {
                case CTC_DKIT_MISC_INTEGRITY_OP_E:
                    sal_sscanf(line, "%[^=]=%s", poly_str, poly_str_right);
                    rel_val = _ctc_greatbelt_dkit_misc_integrity_calc_poly(chip_id, 0, TRUE, 1, poly_str, NULL);
                    exp_val = _ctc_greatbelt_dkit_misc_integrity_calc_poly(chip_id, 0, TRUE, 1, poly_str_right, NULL);
                    if (rel_val != exp_val)
                    {
                        sal_sprintf(result, "%s = %d@%d\n", poly_str, rel_val, exp_val);
                        CTC_DKIT_PRINT("%s", result);
                        *check_pass = FALSE;
                    }

                    break;

                case CTC_DKIT_MISC_INTEGRITY_OP_BE:
                    sal_sscanf(line, "%[^>=]>=%s", poly_str, poly_str_right);
                    rel_val = _ctc_greatbelt_dkit_misc_integrity_calc_poly(chip_id, 0, TRUE, 1, poly_str, NULL);
                    exp_val = _ctc_greatbelt_dkit_misc_integrity_calc_poly(chip_id, 0, TRUE, 1, poly_str_right, NULL);
                    if (rel_val < exp_val)
                    {
                        sal_sprintf(result, "%s >= %d@%d\n", poly_str, rel_val, exp_val);
                        CTC_DKIT_PRINT("%s", result);
                        *check_pass = FALSE;
                    }

                    break;

                case CTC_DKIT_MISC_INTEGRITY_OP_LE:
                    sal_sscanf(line, "%[^<=]<=%s", poly_str, poly_str_right);
                    rel_val = _ctc_greatbelt_dkit_misc_integrity_calc_poly(chip_id, 0, TRUE, 1, poly_str, NULL);
                    exp_val = _ctc_greatbelt_dkit_misc_integrity_calc_poly(chip_id, 0, TRUE, 1, poly_str_right, NULL);
                    if (rel_val > exp_val)
                    {
                        sal_sprintf(result, "%s <= %d@%d\n", poly_str, rel_val, exp_val);
                        CTC_DKIT_PRINT("%s", result);
                        *check_pass = FALSE;
                    }

                    break;

                case CTC_DKIT_MISC_INTEGRITY_OP_B:
                    sal_sscanf(line, "%[^>]>%s", poly_str, poly_str_right);
                    rel_val = _ctc_greatbelt_dkit_misc_integrity_calc_poly(chip_id, 0, TRUE, 1, poly_str, NULL);
                    exp_val = _ctc_greatbelt_dkit_misc_integrity_calc_poly(chip_id, 0, TRUE, 1, poly_str_right, NULL);
                    if (rel_val <= exp_val)
                    {
                        sal_sprintf(result, "%s > %d@%d\n", poly_str, rel_val, exp_val);
                        CTC_DKIT_PRINT("%s", result);
                        *check_pass = FALSE;
                    }

                    break;

                case CTC_DKIT_MISC_INTEGRITY_OP_L:
                    sal_sscanf(line, "%[^<]<%s", poly_str, poly_str_right);
                    rel_val = _ctc_greatbelt_dkit_misc_integrity_calc_poly(chip_id, 0, TRUE, 1, poly_str, NULL);
                    exp_val = _ctc_greatbelt_dkit_misc_integrity_calc_poly(chip_id, 0, TRUE, 1, poly_str_right, NULL);
                    if (rel_val >= exp_val)
                    {
                        sal_sprintf(result, "%s < %d@%d\n", poly_str, rel_val, exp_val);
                        CTC_DKIT_PRINT("%s", result);
                        *check_pass = FALSE;
                    }

                    break;

                default:
                    break;
                }
            }
            /*link list check*/
            else if (CHECK_LINK_LIST == integrity_stat)
            {
                if (sal_strstr(line, "{var"))
                {
                    sal_sscanf(line, "%*[^=]=%s", poly_str);
                }
                else
                {

                }

                if (sal_strstr(line, "{var linkHeadPtr}"))
                {
                    exp_head = _ctc_greatbelt_dkit_misc_integrity_calc_poly(chip_id, 0, FALSE, 1, poly_str, NULL);
                }
                else if (sal_strstr(line, "{var linkTailPtr}"))
                {
                    exp_tail = _ctc_greatbelt_dkit_misc_integrity_calc_poly(chip_id, 0, FALSE, 1, poly_str, NULL);
                }
                else if (sal_strstr(line, "{var linkCnt}"))
                {
                    exp_cnt = _ctc_greatbelt_dkit_misc_integrity_calc_poly(chip_id, 0, FALSE, 1, poly_str, NULL);
                }
                else if (sal_strstr(line, "{var nextPtr}"))
                {
                    counter   = 0;
                    index     = exp_head;
                    /*get tbl & field id*/
                    _ctc_greatbelt_dkit_misc_integrity_get_id_by_poly(poly_str, &tbl_id, &fld_id);

                    for (;;)
                    {
                        if (IS_BIT_SET(bit_map[index / 32], (index % 32)))
                        {
                            sal_sprintf(result, "@index (0x%x) looped\n", index);
                            break;
                        }
                        else
                        {
                            SET_BIT(bit_map[index / 32], (index % 32));
                        }

                        counter++;
                        if ((index == exp_tail) || (counter == exp_cnt))
                        {
                             /*CTC_DKIT_PRINT("index %d, exp_tail %d\n", index, exp_tail);*/
                             /*CTC_DKIT_PRINT("counter %d, exp_cnt %d\n", counter, exp_cnt);*/
                            break;
                        }

                        cmd = DRV_IOR(tbl_id, fld_id);
                        DRV_IOCTL(chip_id, index, cmd, &next_index);
                        if (next_index >= TABLE_MAX_INDEX(tbl_id))
                        {
                            sal_sprintf(result, "@index 0x%x Next Idx is 0x%x\n", index, next_index);
                            break;
                        }

                        index = next_index;

                    }

                    if (next_index != exp_tail)
                    {
                        sal_sprintf(result, "@actual tail = 0x%x, expetc tail = 0x%x\n", next_index, exp_tail);
                        CTC_DKIT_PRINT("%s", result);
                        *check_pass = FALSE;
                    }
                    else if (counter != exp_cnt)
                    {
                        sal_sprintf(result, "@actual counter = 0x%x, expetc counter = 0x%x\n", counter, exp_cnt);
                        CTC_DKIT_PRINT("%s", result);
                        *check_pass = FALSE;
                    }
                }
                else
                {
                    switch (op_code)
                    {
                    case CTC_DKIT_MISC_INTEGRITY_OP_E:
                        sal_sscanf(line, "%[^=]=%s", poly_str, poly_str_right);
                        rel_val = _ctc_greatbelt_dkit_misc_integrity_calc_poly(chip_id, 0, TRUE, 1, poly_str, NULL);
                        exp_val = _ctc_greatbelt_dkit_misc_integrity_calc_poly(chip_id, 0, TRUE, 1, poly_str_right, NULL);
                        if (rel_val != exp_val)
                        {
                            sal_sprintf(result, "%s = %d@%d\n", poly_str, rel_val, exp_val);
                            CTC_DKIT_PRINT("%s", result);
                            *check_pass = FALSE;
                        }

                        break;

                    case CTC_DKIT_MISC_INTEGRITY_OP_BE:
                        sal_sscanf(line, "%[^>=]>=%s", poly_str, poly_str_right);
                        rel_val = _ctc_greatbelt_dkit_misc_integrity_calc_poly(chip_id, 0, TRUE, 1, poly_str, NULL);
                        exp_val = _ctc_greatbelt_dkit_misc_integrity_calc_poly(chip_id, 0, TRUE, 1, poly_str_right, NULL);
                        if (rel_val < exp_val)
                        {
                            sal_sprintf(result, "%s >= %d@%d\n", poly_str, rel_val, exp_val);
                            CTC_DKIT_PRINT("%s", result);
                            *check_pass = FALSE;
                        }

                        break;

                    case CTC_DKIT_MISC_INTEGRITY_OP_LE:
                        sal_sscanf(line, "%[^<=]<=%s", poly_str, poly_str_right);
                        rel_val = _ctc_greatbelt_dkit_misc_integrity_calc_poly(chip_id, 0, TRUE, 1, poly_str, NULL);
                        exp_val = _ctc_greatbelt_dkit_misc_integrity_calc_poly(chip_id, 0, TRUE, 1, poly_str_right, NULL);
                        if (rel_val > exp_val)
                        {
                            sal_sprintf(result, "%s <= %d@%d\n", poly_str, rel_val, exp_val);
                            CTC_DKIT_PRINT("%s", result);
                            *check_pass = FALSE;
                        }

                        break;

                    case CTC_DKIT_MISC_INTEGRITY_OP_B:
                        sal_sscanf(line, "%[^>]>%s", poly_str, poly_str_right);
                        rel_val = _ctc_greatbelt_dkit_misc_integrity_calc_poly(chip_id, 0, TRUE, 1, poly_str, NULL);
                        exp_val = _ctc_greatbelt_dkit_misc_integrity_calc_poly(chip_id, 0, TRUE, 1, poly_str_right, NULL);
                        if (rel_val <= exp_val)
                        {
                            sal_sprintf(result, "%s > %d@%d\n", poly_str, rel_val, exp_val);
                            CTC_DKIT_PRINT("%s", result);
                            *check_pass = FALSE;
                        }

                        break;

                    case CTC_DKIT_MISC_INTEGRITY_OP_L:
                        sal_sscanf(line, "%[^<]<%s", poly_str, poly_str_right);
                        rel_val = _ctc_greatbelt_dkit_misc_integrity_calc_poly(chip_id, 0, TRUE, 1, poly_str, NULL);
                        exp_val = _ctc_greatbelt_dkit_misc_integrity_calc_poly(chip_id, 0, TRUE, 1, poly_str_right, NULL);
                        if (rel_val >= exp_val)
                        {
                            sal_sprintf(result, "%s < %d@%d\n", poly_str, rel_val, exp_val);
                            CTC_DKIT_PRINT("%s", result);
                            *check_pass = FALSE;
                        }

                        break;

                    default:
                        break;
                    }
                }
            }
            /*interrupt check*/
            else if (CHECK_INTR == integrity_stat)
            {
                uint32 val = 0;

                sal_sscanf(line, "%[^=]=%s", poly_str, poly_str_right);
                rel_val = _ctc_greatbelt_dkit_misc_integrity_calc_inter_mask(chip_id, poly_str, &val);
                exp_val = _ctc_greatbelt_dkit_misc_integrity_calc_poly(chip_id, 0, FALSE, 1, poly_str_right, NULL);
                if (val != exp_val)
                {
                    sal_sprintf(result, "%s = %u@%d\n", poly_str, val, exp_val);
                    CTC_DKIT_PRINT("%s", result);
                    *check_pass = FALSE;
                }
            }
            /*none check*/
            else
            {
                exp_head = 0;
                exp_tail = 0;
                exp_cnt  = 0;
            }
        }
    }

EXIT:
    if(NULL != poly_str)
    {
        sal_free(poly_str);
    }
    if(NULL != poly_str_right)
    {
        sal_free(poly_str_right);
    }

    return DRV_E_NONE;
}

STATIC int32
_ctc_greatbelt_dkit_misc_tx_chan_enable_tx(uint8 chip_id, uint8 port_id, uint8 b_enable)
{
    net_tx_channel_tx_en_t net_tx_channel_tx_en;
    uint32 cmd = 0;

    sal_memset(&net_tx_channel_tx_en, 0, sizeof(net_tx_channel_tx_en_t));

    cmd = DRV_IOR(NetTxChannelTxEn_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, &net_tx_channel_tx_en));
    if (b_enable)
    {
        if (port_id < 32)
        {
            SET_BIT(net_tx_channel_tx_en.port_tx_en_net31_to0, port_id);
        }
        else
        {
            SET_BIT(net_tx_channel_tx_en.port_tx_en_net59_to32, (port_id - 32));
        }
    }
    else
    {
        if (port_id < 32)
        {
            CLEAR_BIT(net_tx_channel_tx_en.port_tx_en_net31_to0, port_id);
        }
        else
        {
            CLEAR_BIT(net_tx_channel_tx_en.port_tx_en_net59_to32, (port_id - 32));
        }
    }

    cmd = DRV_IOW(NetTxChannelTxEn_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, &net_tx_channel_tx_en));

    return DRV_E_NONE;
}

STATIC void
_ctc_greatbelt_dkit_misc_print_pkt_buf(uint8* data, uint32 byte_len)
{
    uint32 addr = 0;
    uint32 i = 0;

    for (i = 0; i < byte_len; i++)
    {
        if (0 == i % 16)
        {
            CTC_DKIT_PRINT("\n %08x0: ", addr++);
        }

        CTC_DKIT_PRINT("%02x ", data[i]);
    }

    CTC_DKIT_PRINT("\n\n");
}

int32
ctc_greatbelt_dkit_misc_integrity_check(uint8 lchip, void* p_para1, void* p_para2, void* p_para3)
{
    char* gold_file = NULL;
    char* rlt_file = NULL;
    sal_file_t gold_fp = NULL;
    sal_file_t rslt_fp = NULL;
    char*   line = NULL;
    char*   result = NULL;
    uint8 check_pass = TRUE;
    int32 ret = DRV_E_NONE;

    DKITS_PTR_VALID_CHECK(p_para1);
    DKITS_PTR_VALID_CHECK(p_para2);

    line = (char*)sal_malloc(MAX_PER_LINE * sizeof(char));
    result = (char*)sal_malloc(MAX_PER_LINE * sizeof(char));

    if (!line || !result)
    {
        goto clean;
    }

    sal_memset(line, 0, MAX_PER_LINE * sizeof(char));
    sal_memset(result, 0, MAX_PER_LINE * sizeof(char));

    gold_file = (char*)p_para1;
    rlt_file = (char*)p_para2;

    gold_fp = sal_fopen(gold_file, "r");
    if (NULL == gold_fp)
    {
        CTC_DKIT_PRINT("open golden file %s failed!\n", gold_file);
        ret = DRV_E_FILE_OPEN_FAILED;
        goto clean;
    }

    rslt_fp = sal_fopen(rlt_file, "w+");
    if (NULL == rslt_fp)
    {
        sal_fclose(gold_fp);
        gold_fp = NULL;
        CTC_DKIT_PRINT("open destination file %s failed!\n", rlt_file);
        ret = DRV_E_FILE_OPEN_FAILED;
        goto clean;
    }

    while (NULL != sal_fgets(line, MAX_PER_LINE, gold_fp))
    {
        /* sdk: note temp fix chip_id = 0 for cmodel case bring up.*/
        _ctc_greatbelt_dkit_misc_integrity_result_do(lchip, line, result, &check_pass);
        sal_fprintf(rslt_fp, "%s", result);

        sal_memset(line, 0, MAX_PER_LINE);
        sal_memset(result, 0, MAX_PER_LINE);
    }

    if (check_pass)
    {
        CTC_DKIT_PRINT("integrity check passed!\n");
        ret = DRV_E_NONE;
    }
    else
    {
        ret = DRV_E_RESERVD_VALUE_ERROR;
    }

clean:

    /*close file*/
    if (NULL != gold_fp)
    {
        sal_fclose(gold_fp);
        gold_fp = NULL;
    }

    if (NULL != rslt_fp)
    {
        sal_fclose(rslt_fp);
        rslt_fp = NULL;
    }
    if(NULL != line)
    {
        sal_free(line);
    }
    if(NULL != result)
    {
        sal_free(result);
    }

    return ret;
}

STATIC int32
_ctc_greatbelt_dkit_misc_dump_packet_rx(uint8 lchip, uint16 lport)
{
    uint8* value = NULL;
    uint32 cmd = 0;
    uint32 pkt_len = 0;
    net_rx_chan_info_ram_t net_rx_chan_info_ram;
    net_rx_pkt_buf_ram_t net_rx_pkt_buf_ram;

    if (lport > DKIT_RX_PORT_MAX)
    {
        CTC_DKIT_PRINT("Error: port_id should be %d~%d.\n", DKIT_RX_PORT_MIN, DKIT_RX_PORT_MAX);
        return DRV_E_INVALID_PARAMETER;
    }
    CTC_DKIT_PRINT(" Dump net rx packet buffer on chipid:%u port:%u.\n",
                   lchip, lport);

    sal_memset(&net_rx_chan_info_ram, 0, sizeof(net_rx_chan_info_ram_t));
    sal_memset(&net_rx_pkt_buf_ram, 0, sizeof(net_rx_pkt_buf_ram_t));

    /*1. Read channel information from NetRxChanInfoRam*/
    cmd = DRV_IOR(NetRxChanInfoRam_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &net_rx_chan_info_ram));

    if (0 == net_rx_chan_info_ram.pkt_len)
    {
        CTC_DKIT_PRINT(" Error: net rx packet buffer capture is empty.\n");
        return DRV_E_ILLEGAL_LENGTH;
    }

    if (net_rx_chan_info_ram.pkt_len > DKIT_LEN_256B)
    {
        pkt_len = net_rx_chan_info_ram.pkt_len % DKIT_LEN_256B;
        CTC_DKIT_PRINT(" Net rx packet buffer length: %u(Byte); capture packet length: %u(Byte)\n",
                       net_rx_chan_info_ram.pkt_len, pkt_len);
    }
    else
    {
        pkt_len = net_rx_chan_info_ram.pkt_len;
        CTC_DKIT_PRINT(" Net rx packet buffer capture packet length: %u(Byte)\n", net_rx_chan_info_ram.pkt_len);
    }

    CTC_DKIT_PRINT(" HeadPtr: 0x%x; CurrentPtr: 0x%x\n",
                   net_rx_chan_info_ram.head_ptr, net_rx_chan_info_ram.curr_ptr);
    value = sal_malloc(DKIT_NETRX_PKT_LEN);
    if (NULL == value)
    {
        return DRV_E_NO_MEMORY;
    }

    sal_memset(value, 0, DKIT_NETRX_PKT_LEN);

    /*2. Read the packet from NetRxPktBufRam using headPtr and currPtr*/
    cmd = DRV_IOR(NetRxPktBufRam_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN_AND_FREE(DRV_IOCTL(lchip, net_rx_chan_info_ram.head_ptr * 2, cmd, &net_rx_pkt_buf_ram), value);
    sal_memcpy(value, &net_rx_pkt_buf_ram, NET_RX_PKT_BUF_RAM_ENTRY_SIZE);

    if (pkt_len > DKIT_LEN_64B)
    {
        cmd = DRV_IOR(NetRxPktBufRam_t, DRV_ENTRY_FLAG);
        DRV_IF_ERROR_RETURN_AND_FREE(DRV_IOCTL(lchip, net_rx_chan_info_ram.head_ptr * 2 + 1, cmd, &net_rx_pkt_buf_ram), value);
        sal_memcpy(value + NET_RX_PKT_BUF_RAM_ENTRY_SIZE, &net_rx_pkt_buf_ram, NET_RX_PKT_BUF_RAM_ENTRY_SIZE);
    }

    if (pkt_len > DKIT_LEN_128B)
    {
        cmd = DRV_IOR(NetRxPktBufRam_t, DRV_ENTRY_FLAG);
        DRV_IF_ERROR_RETURN_AND_FREE(DRV_IOCTL(lchip, net_rx_chan_info_ram.curr_ptr * 2, cmd, &net_rx_pkt_buf_ram), value);
        sal_memcpy(value + NET_RX_PKT_BUF_RAM_ENTRY_SIZE * 2, &net_rx_pkt_buf_ram, NET_RX_PKT_BUF_RAM_ENTRY_SIZE);
    }

    if (pkt_len > DKIT_LEN_192B)
    {
        cmd = DRV_IOR(NetRxPktBufRam_t, DRV_ENTRY_FLAG);
        DRV_IF_ERROR_RETURN_AND_FREE(DRV_IOCTL(lchip, net_rx_chan_info_ram.curr_ptr * 2 + 1, cmd, &net_rx_pkt_buf_ram), value);
        sal_memcpy(value + NET_RX_PKT_BUF_RAM_ENTRY_SIZE * 3, &net_rx_pkt_buf_ram, NET_RX_PKT_BUF_RAM_ENTRY_SIZE);
    }

    CTC_DKIT_PRINT("\n Net rx packet buffer data:\n");
    CTC_DKIT_PRINT(" ----------------------------------------------------------\n");
    _ctc_greatbelt_dkit_misc_print_pkt_buf(value, pkt_len);

    sal_free(value);

    return CLI_SUCCESS;
}

STATIC int32
_ctc_greatbelt_dkit_misc_dump_packet_bsr(uint8 lchip, uint16 lport, uint32 pkt_len)
{
    uint32 cmd = 0;
    uint32 tail_ptr = 0;
    uint32 head_buf_ptr = 0;
    uint32 head_ptr_offset = 0;
    uint32 read_ptr = 0;
    int32 i = 0;
    int32 read_times = 0;
    int32 loop_times = 0;
    int32 value_offset = 0;
    uint8* value = NULL;
    buf_ptr_mem_t buf_ptr_mem;
    pb_ctl_pkt_buf_t pb_ctl_pkt_buf;
    pb_ctl_hdr_buf_t pb_ctl_hdr_buf;
    buf_retrv_pkt_msg_mem_t buf_retrv_pkt_msg_mem;
    buf_retrv_pkt_status_mem_t buf_retrv_pkt_status_mem;
    buf_retrv_pkt_config_mem_t buf_retrv_pkt_config_mem;

    if (lport > DKIT_PORT_MAX)
    {
        CTC_DKIT_PRINT("Error: port_id should be %d~%d.\n", DKIT_PORT_MIN, DKIT_PORT_MAX);
        return DRV_E_INVALID_PARAMETER;
    }
    if (pkt_len > DKIT_PKT_BUF_MEM_MAX)
    {
        CTC_DKIT_PRINT("Error: pkt len should be 0~%dB.\n", DKIT_PKT_BUF_MEM_MAX);
        return DRV_E_INVALID_PARAMETER;
    }
    CTC_DKIT_PRINT(" Dump bsr packet buffer on chipid:%u port:%u packet length:%u.\n",
                   lchip, lport, pkt_len);

    sal_memset(&buf_ptr_mem, 0, sizeof(buf_ptr_mem_t));
    sal_memset(&pb_ctl_pkt_buf, 0, sizeof(pb_ctl_pkt_buf_t));
    sal_memset(&pb_ctl_hdr_buf, 0, sizeof(pb_ctl_hdr_buf_t));
    sal_memset(&buf_retrv_pkt_msg_mem, 0, sizeof(buf_retrv_pkt_msg_mem_t));
    sal_memset(&buf_retrv_pkt_status_mem, 0, sizeof(buf_retrv_buf_config_mem_t));
    sal_memset(&buf_retrv_pkt_config_mem, 0, sizeof(buf_retrv_pkt_config_mem_t));

    /*Step 1. Read BufRetrvPktStatusMem using destination channel as index */
    cmd = DRV_IOR(BufRetrvPktStatusMem_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &buf_retrv_pkt_status_mem));
    tail_ptr = buf_retrv_pkt_status_mem.tail_ptr;

    /*recal tail_ptr*/
    cmd = DRV_IOR(BufRetrvPktConfigMem_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &buf_retrv_pkt_config_mem));
    if (tail_ptr == buf_retrv_pkt_config_mem.start_ptr)
    {
        tail_ptr = buf_retrv_pkt_config_mem.end_ptr;
    }
    else
    {
        tail_ptr--;
    }

    /*Step 2. Read BufRetrvPktMsgMem using the tailPtr get above */
    cmd = DRV_IOR(BufRetrvPktMsgMem_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, tail_ptr, cmd, &buf_retrv_pkt_msg_mem));

    /*if doesn't set pkt_len, cal pkt_len*/
    if (0 == pkt_len)
    {
        pkt_len = (buf_retrv_pkt_msg_mem.data1 & 0x03FFF000) >> 12;
    }

    /*Step 3. Calculate the  Buffer Counter and Last Buffer Word Counter of this packet*/
    read_times = pkt_len / PB_CTL_PKT_BUF_ENTRY_SIZE + ((pkt_len % PB_CTL_PKT_BUF_ENTRY_SIZE == 0) ? 0 : 1);
    value = sal_malloc(read_times * PB_CTL_PKT_BUF_ENTRY_SIZE);
    if (NULL == value)
    {
        return DRV_E_NO_MEMORY;
    }

    /*Get two key information: headPtrOffset[29:28], headBufferPtr[27:14]*/
    head_buf_ptr = (buf_retrv_pkt_msg_mem.data0 & 0x0fffc000) >> 14;
    head_ptr_offset = (buf_retrv_pkt_msg_mem.data0 & 0x30000000) >> 28;
    if (read_times > DKIT_PKT_BUF_NUM)
    {
        loop_times = DKIT_PKT_BUF_NUM;
        read_times -= DKIT_PKT_BUF_NUM;
    }
    else
    {
        loop_times = read_times;
        read_times = 0;
    }

    CTC_DKIT_PRINT_DEBUG("read_times:%d,loop_times:%d, head_buf_ptr:0x%x, head_ptr_offset:0x%x\n", read_times + DKIT_PKT_BUF_NUM, loop_times, head_buf_ptr, head_ptr_offset);

    /*read PbCtlPktBuf */
    for (i = 0; i < loop_times; i++)
    {
        read_ptr = (head_buf_ptr << 2) | ((head_ptr_offset + i) % DKIT_PKT_BUF_NUM);
        cmd = DRV_IOR(PbCtlPktBuf_t, DRV_ENTRY_FLAG);
        DRV_IF_ERROR_RETURN_AND_FREE(DRV_IOCTL(lchip, read_ptr, cmd, &pb_ctl_pkt_buf), value);
#if 0
        DRV_DBG_INFO("times[%d] read_ptr:0x%x\n", i, read_ptr);
        CTC_DKIT_PRINT("data0~data7 :%02x %02x %02x %02x %02x %02x %02x %02x\n",
                   pb_ctl_pkt_buf.pkt_d0, pb_ctl_pkt_buf.pkt_d1, pb_ctl_pkt_buf.pkt_d2, pb_ctl_pkt_buf.pkt_d3,
                   pb_ctl_pkt_buf.pkt_d4, pb_ctl_pkt_buf.pkt_d5, pb_ctl_pkt_buf.pkt_d6, pb_ctl_pkt_buf.pkt_d7);
        CTC_DKIT_PRINT("data8~data15:%02x %02x %02x %02x %02x %02x %02x %02x\n",
                   pb_ctl_pkt_buf.pkt_d8, pb_ctl_pkt_buf.pkt_d9, pb_ctl_pkt_buf.pkt_d10, pb_ctl_pkt_buf.pkt_d11,
                   pb_ctl_pkt_buf.pkt_d12, pb_ctl_pkt_buf.pkt_d13, pb_ctl_pkt_buf.pkt_d14, pb_ctl_pkt_buf.pkt_d15);
#endif
        sal_memcpy(value + (value_offset++) * PB_CTL_PKT_BUF_ENTRY_SIZE, (uint8*)&pb_ctl_pkt_buf, PB_CTL_PKT_BUF_ENTRY_SIZE);
    }

    /*read PbCtlHdrBuf in PbCtl using the headBufferPtr above to get Bridge Header*/
    cmd = DRV_IOR(PbCtlHdrBuf_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN_AND_FREE(DRV_IOCTL(lchip, head_buf_ptr, cmd, &pb_ctl_hdr_buf), value);

    while (read_times > 0)
    {
        if (read_times > DKIT_PKT_BUF_NUM)
        {
            loop_times = DKIT_PKT_BUF_NUM;
            read_times -= DKIT_PKT_BUF_NUM;
        }
        else
        {
            loop_times = read_times;
            read_times = 0;
        }

        /*read BufPtrMem in BufStore to get next pointer*/
        cmd = DRV_IOR(BufPtrMem_t, DRV_ENTRY_FLAG);
        DRV_IF_ERROR_RETURN_AND_FREE(DRV_IOCTL(lchip, head_buf_ptr, cmd, &buf_ptr_mem), value);
        head_buf_ptr = buf_ptr_mem.buf_ptr;
        head_ptr_offset = buf_ptr_mem.buf_ptr_offset;

        CTC_DKIT_PRINT_DEBUG("read_times:%d,loop_times:%d, head_buf_ptr:0x%x, head_ptr_offset:0x%x\n", read_times + DKIT_PKT_BUF_NUM, loop_times, head_buf_ptr, head_ptr_offset);

        for (i = 0; i < loop_times; i++)
        {
            read_ptr = (head_buf_ptr << 2) | ((head_ptr_offset + i) % DKIT_PKT_BUF_NUM);
            cmd = DRV_IOR(PbCtlPktBuf_t, DRV_ENTRY_FLAG);
            DRV_IF_ERROR_RETURN_AND_FREE(DRV_IOCTL(lchip, read_ptr, cmd, &pb_ctl_pkt_buf), value);
#if 0
            DRV_DBG_INFO("times[%d] read_ptr:0x%x\n", i, read_ptr);
            CTC_DKIT_PRINT("data0~data7 :%02x %02x %02x %02x %02x %02x %02x %02x\n",
                       pb_ctl_pkt_buf.pkt_d0, pb_ctl_pkt_buf.pkt_d1, pb_ctl_pkt_buf.pkt_d2, pb_ctl_pkt_buf.pkt_d3,
                       pb_ctl_pkt_buf.pkt_d4, pb_ctl_pkt_buf.pkt_d5, pb_ctl_pkt_buf.pkt_d6, pb_ctl_pkt_buf.pkt_d7);
            CTC_DKIT_PRINT("data8~data15:%02x %02x %02x %02x %02x %02x %02x %02x\n",
                       pb_ctl_pkt_buf.pkt_d8, pb_ctl_pkt_buf.pkt_d9, pb_ctl_pkt_buf.pkt_d10, pb_ctl_pkt_buf.pkt_d11,
                       pb_ctl_pkt_buf.pkt_d12, pb_ctl_pkt_buf.pkt_d13, pb_ctl_pkt_buf.pkt_d14, pb_ctl_pkt_buf.pkt_d15);
#endif
            sal_memcpy(value + (value_offset++) * PB_CTL_PKT_BUF_ENTRY_SIZE, (uint8*)&pb_ctl_pkt_buf, PB_CTL_PKT_BUF_ENTRY_SIZE);
        }
    }

    CTC_DKIT_PRINT("\n PacketBuffer:%u(Byte)\n", pkt_len);
    CTC_DKIT_PRINT(" ----------------------------------------------------------");
    _ctc_greatbelt_dkit_misc_print_pkt_buf(value, pkt_len);

    CTC_DKIT_PRINT(" PacketHeader:\n");
    CTC_DKIT_PRINT(" ----------------------------------------------------------");
    _ctc_greatbelt_dkit_misc_print_pkt_buf((uint8*)&pb_ctl_hdr_buf, PB_CTL_HDR_BUF_ENTRY_SIZE);

    sal_free(value);

    return CLI_SUCCESS;
}

STATIC int32
_ctc_greatbelt_dkit_misc_dump_packet_capture(uint8 lchip, uint16 lport)
{
    /*1. set NetTxChannelTx portID bit 0*/
    DRV_IF_ERROR_RETURN(_ctc_greatbelt_dkit_misc_tx_chan_enable_tx(lchip, lport, FALSE));

    CTC_DKIT_PRINT(" Start capture net tx packet on chipid:%u port:%u.\n",
                   lchip, lport);

    return CLI_SUCCESS;
}

STATIC int32
_ctc_greatbelt_dkit_misc_dump_packet_tx(uint8 lchip, uint16 lport)
{
    net_tx_ch_dynamic_info_t net_tx_ch_dynamic_info;
    net_tx_ch_static_info_t net_tx_ch_static_info;
    net_tx_hdr_buf_t net_tx_hdr_buf;
    net_tx_pkt_buf_t net_tx_pkt_buf;
    uint32 read_ptr = 0;
    uint32 write_ptr = 0;
    uint32 start_ptr = 0;
    uint32 end_ptr = 0;
    uint32 curr_ptr = 0;
    uint32 pkt_depth = 0;
    uint32 valid_byte = 0;
    uint32 cmd = 0;
    uint8* value = NULL;
    int    index = 0;

    if (lport > DKIT_PORT_MAX)
    {
        CTC_DKIT_PRINT("Error: port_id should be %d~%d.\n", DKIT_PORT_MIN, DKIT_PORT_MAX);
        return DRV_E_INVALID_PARAMETER;
    }
    CTC_DKIT_PRINT(" Stop capture and dump tx packet buffer on chipid:%u port:%u.\n",
                   lchip, lport);

    sal_memset(&net_tx_ch_dynamic_info, 0, sizeof(net_tx_ch_dynamic_info_t));
    sal_memset(&net_tx_ch_static_info, 0, sizeof(net_tx_ch_static_info_t));
    sal_memset(&net_tx_hdr_buf, 0, sizeof(net_tx_hdr_buf_t));
    sal_memset(&net_tx_pkt_buf, 0, sizeof(net_tx_pkt_buf_t));

    /*1. Read NetTxChDynamicInfo table*/
    cmd = DRV_IOR(NetTxChDynamicInfo_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &net_tx_ch_dynamic_info));
    write_ptr = net_tx_ch_dynamic_info.write_ptr;
    read_ptr = net_tx_ch_dynamic_info.read_ptr;

    /*2. Read NetTxChStaticInfo table*/
    cmd = DRV_IOR(NetTxChStaticInfo_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &net_tx_ch_static_info));
    start_ptr = net_tx_ch_static_info.start_ptr;
    end_ptr = net_tx_ch_static_info.end_ptr;

    if (0 == net_tx_ch_dynamic_info.chan_word_cnt)
    {
        CTC_DKIT_PRINT(" Error: packet buffer is empty.\n");
        return DRV_E_ILLEGAL_LENGTH;
    }
    else
    {
        CTC_DKIT_PRINT(" Net tx chan word cnt: %u \n", net_tx_ch_dynamic_info.chan_word_cnt);
    }

    /*3. Calc pkt_depth*/
    if (write_ptr > read_ptr)
    {
        pkt_depth = write_ptr - read_ptr;
    }
    else if (write_ptr == read_ptr)
    {
        pkt_depth = end_ptr - start_ptr + 1;
    }
    else
    {
        pkt_depth = end_ptr - read_ptr + 1 + write_ptr - start_ptr;
    }

    CTC_DKIT_PRINT(" ReadPtr: 0x%x, WritePtr: 0x%x; StartPtr: 0x%x, EndPtr: 0x%x\n",
                   read_ptr, write_ptr, start_ptr, end_ptr);

    value = sal_malloc((pkt_depth * 2 + 1) * DKIT_NETTX_BUF_DATA_LEN);
    if (NULL == value)
    {
        return DRV_E_NO_MEMORY;
    }

    /*4. Read NetTxPktBuf to capture the packet*/
    for (index = 0; index < pkt_depth * 2; index++)
    {
        if (read_ptr * 2 + index > end_ptr * 2 + 1)
        {
            curr_ptr = read_ptr * 2 + index - end_ptr * 2 + start_ptr * 2 - 2;
        }
        else
        {
            curr_ptr = read_ptr * 2 + index;
        }

         /*CTC_DKIT_PRINT("curr_ptr: 0x%x\n", curr_ptr);*/
        cmd = DRV_IOR(NetTxPktBuf_t, DRV_ENTRY_FLAG);
        DRV_IF_ERROR_RETURN_AND_FREE(DRV_IOCTL(lchip, curr_ptr, cmd, &net_tx_pkt_buf), value);
        sal_memcpy(value + DKIT_NETTX_BUF_DATA_LEN * index, (uint8*)&net_tx_pkt_buf + 4, DKIT_NETTX_BUF_DATA_LEN);

        if (index >= pkt_depth * 2 - 2)
        {
            if (net_tx_pkt_buf.eop)
            {
                valid_byte = index * DKIT_NETTX_BUF_DATA_LEN + (net_tx_pkt_buf.full ? DKIT_NETTX_BUF_DATA_LEN : (net_tx_pkt_buf.pkt_d7));
            }

             /*CTC_DKIT_PRINT("curr_ptr: 0x%x, net_tx_pkt_buf.eop: %x, valid_byte: %dB\n", curr_ptr, net_tx_pkt_buf.eop, valid_byte);*/
        }
    }

    CTC_DKIT_PRINT(" PacketDepth: %u, ValidByte: %u(Byte)\n", pkt_depth, valid_byte);

    /*5. printf packet*/
    CTC_DKIT_PRINT("\n Net tx packet buffer data:\n");
    CTC_DKIT_PRINT(" ----------------------------------------------------------");
    _ctc_greatbelt_dkit_misc_print_pkt_buf(value, valid_byte);

    /*6. Read NetTxHdrBuf to capture the header of the packet*/
    cmd = DRV_IOR(NetTxHdrBuf_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN_AND_FREE(DRV_IOCTL(lchip, read_ptr * 2, cmd, &net_tx_hdr_buf), value);
    sal_memcpy(value, (uint8*)&net_tx_hdr_buf + 12, DKIT_NETTX_BUF_DATA_LEN);

    /*7. printf packet header*/
    CTC_DKIT_PRINT(" Net tx packet buffer header:\n");
    CTC_DKIT_PRINT(" ----------------------------------------------------------");
    _ctc_greatbelt_dkit_misc_print_pkt_buf(value, DKIT_NETTX_BUF_DATA_LEN);

    sal_free(value);

    /*8. clear configuration*/
    DRV_IF_ERROR_RETURN(_ctc_greatbelt_dkit_misc_tx_chan_enable_tx(lchip, lport, TRUE));

    return CLI_SUCCESS;
}

int32
ctc_greatbelt_dkit_misc_packet_dump(uint8 lchip, void* p_para1, void* p_para2, void* p_para3)
{
    uint16 lport = 0;
    uint16 gport = 0;
    uint32 pkt_len = 0;
    ctc_dkits_dump_packet_type_t dump_packet_type = 0;

    DKITS_PTR_VALID_CHECK(p_para1);
    DKITS_PTR_VALID_CHECK(p_para2);

    dump_packet_type = *((ctc_dkits_dump_packet_type_t*)p_para1);
    gport = *((uint16*)p_para2);
    lport = CTC_DKIT_MAP_GPORT_TO_LPORT(gport);

    if (lport > DKIT_PORT_MAX)
    {
        CTC_DKIT_PRINT("Error: port_id should be %d~%d.\n", DKIT_PORT_MIN, DKIT_PORT_MAX);
        return DRV_E_INVALID_PARAMETER;
    }

    switch (dump_packet_type)
    {
    case CTC_DKITS_DUMP_PACKET_TYPE_CAPTURE:
        _ctc_greatbelt_dkit_misc_dump_packet_capture(lchip, lport);
        break;
    case CTC_DKITS_DUMP_PACKET_TYPE_NETTX:
        _ctc_greatbelt_dkit_misc_dump_packet_tx(lchip, lport);
        break;
    case CTC_DKITS_DUMP_PACKET_TYPE_NETRX:
        _ctc_greatbelt_dkit_misc_dump_packet_rx(lchip, lport);
        break;
    case CTC_DKITS_DUMP_PACKET_TYPE_BSR:
        pkt_len = (NULL == p_para3) ? 0 : *((uint32*)p_para3);
        _ctc_greatbelt_dkit_misc_dump_packet_bsr(lchip, lport, pkt_len);
        break;
    default:
        CTC_DKIT_PRINT(" Error dump packet type.\n");
        break;
    }

    return CLI_SUCCESS;
}

int32
ctc_greatbelt_dkit_misc_chip_info(uint8 lchip, sal_file_t p_wf)
{
    uint32 cmd = 0;
    device_id_t device_id;
    char        chip_name[32] = {0};
    ctc_dkit_device_id_type_t    dev_id;
    uint32      version_id = 0;


    sal_memset(&device_id, 0, sizeof(device_id_t));
    cmd = DRV_IOR(DeviceId_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &device_id);

    switch(device_id.device_id)
    {
        case 0xf5: /*5160*/
            dev_id  = CTC_DKIT_DEVICE_ID_GB_5160;
            sal_strcpy(chip_name, "CTC5160");
            break;

        case 0x25: /*5120*/
            dev_id  = CTC_DKIT_DEVICE_ID_RM_5120;
            sal_strcpy(chip_name, "CTC5120");
            break;

        case 0x35: /*5162*/
            dev_id  = CTC_DKIT_DEVICE_ID_GB_5162;
            sal_strcpy(chip_name, "CTC5162");
            break;
        case 0xb5: /*5163*/
            dev_id  = CTC_DKIT_DEVICE_ID_GB_5163;
            sal_strcpy(chip_name, "CTC5163");
            break;

        case 0x15: /*3162*/
            dev_id  = CTC_DKIT_DEVICE_ID_RT_3162;
            sal_strcpy(chip_name, "CTC3162");
            break;

        case 0x95: /*3163*/
            dev_id  = CTC_DKIT_DEVICE_ID_RT_3163;
            sal_strcpy(chip_name, "CTC3163");
            break;

        default:
            dev_id  = CTC_DKIT_DEVICE_ID_INVALID;
            sal_strcpy(chip_name, "Not Support Chip");
            break;
    }
    version_id = device_id.device_rev;

    CTC_DKITS_PRINT_FILE(p_wf, "Chip name: %s, Device Id: %d, Version Id: %d \n",
                               chip_name, dev_id, version_id);

    CTC_DKITS_PRINT_FILE(p_wf, "Dkits %s Released at %s Chip Series: %s\nCopyright (C) %s Centec Networks Inc.  All rights reserved.\n"
            , CTC_DKITS_VERSION_STR, CTC_DKITS_RELEASE_DATE, CTC_DKITS_CURRENT_GB_CHIP, CTC_DKITS_COPYRIGHT_TIME);
    CTC_DKITS_PRINT_FILE(p_wf, "-------------------------------------------------------------\n");

    return CLI_SUCCESS;
}

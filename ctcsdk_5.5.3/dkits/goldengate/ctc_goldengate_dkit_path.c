#include "goldengate/include/drv_lib.h"
#include "ctc_cli.h"
#include "ctc_dkit.h"
#include "ctc_goldengate_dkit.h"
#include "ctc_goldengate_dkit_captured_info.h"
#include "ctc_goldengate_dkit_path.h"
#include "ctc_goldengate_dkit_bus_info.h"

extern ctc_dkit_master_t* g_gg_dkit_master[CTC_DKITS_MAX_LOCAL_CHIP_NUM];

 int32
ctc_goldengate_dkit_path_process(void* p_para)
{
    ctc_dkit_path_para_t* p_path_para = (ctc_dkit_path_para_t*)p_para;
    sal_time_t tv;
    uint8 lchip = 0;

    DKITS_PTR_VALID_CHECK(p_para);
    lchip = p_path_para->lchip;
    CTC_DKIT_LCHIP_CHECK(lchip);
    DKITS_PTR_VALID_CHECK(g_gg_dkit_master[lchip]);

    /*get systime*/
    sal_time(&tv);
    CTC_DKIT_PRINT("\n");
    CTC_DKIT_PRINT("TIME: %s", sal_ctime(&tv));
    CTC_DKIT_PRINT("============================================================\n");
    if ((p_path_para->captured) && (p_path_para->on_line))
    {
        ctc_goldengate_dkit_captured_path_process((void*)p_path_para);
    }
    else
    {
        ctc_goldengate_dkit_bus_path_process(lchip, &(g_gg_dkit_master[lchip]->path_stats), p_path_para->detail);
    }
    CTC_DKIT_PRINT("============================================================\n");

    return CLI_SUCCESS;
}



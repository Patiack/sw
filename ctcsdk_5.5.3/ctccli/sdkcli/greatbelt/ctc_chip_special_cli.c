
#include "sal.h"

#include "ctc_macro.h"
#include "ctc_chip.h"
#include "ctc_cli.h"
#include "ctc_scl_cli.h"
#include "ctc_greatbelt_port_cli.h"
#include "ctc_greatbelt_acl_cli.h"
#include "ctc_greatbelt_nexthop_cli.h"
#include "ctc_greatbelt_l2_cli.h"
#include "ctc_greatbelt_vlan_cli.h"
#include "ctc_greatbelt_scl_cli.h"
#include "ctc_greatbelt_ipmc_cli.h"
#include "ctc_greatbelt_mirror_cli.h"
#include "ctc_greatbelt_ipuc_cli.h"
#include "ctc_greatbelt_mpls_cli.h"
#include "ctc_greatbelt_qos_cli.h"
#include "ctc_greatbelt_pdu_cli.h"
#include "ctc_greatbelt_interrupt_cli.h"
#include "ctc_greatbelt_packet_cli.h"
#include "ctc_greatbelt_oam_cli.h"
#include "ctc_greatbelt_stacking_cli.h"
#include "ctc_greatbelt_linkagg_cli.h"
#include "ctc_greatbelt_dma_cli.h"
#include "ctc_greatbelt_aps_cli.h"
#include "ctc_greatbelt_chip_cli.h"
#include "ctc_greatbelt_ftm_cli.h"

extern ctc_chip_special_callback_fun_t g_chip_special_cli[MAX_CTC_CHIP_TYPE];

int32
ctc_greatbelt_chip_special_cli_init(void)
{
    ctc_greatbelt_nexthop_cli_init();
    ctc_greatbelt_l2_cli_init();
    ctc_greatbelt_vlan_cli_init();
    ctc_greatbelt_scl_cli_init();
    ctc_greatbelt_port_cli_init();
    ctc_greatbelt_mirror_cli_init();
    ctc_greatbelt_acl_cli_init();
    ctc_greatbelt_ipmc_cli_init();
    ctc_greatbelt_ipuc_cli_init();
    ctc_greatbelt_mpls_cli_init();
    ctc_greatbelt_pdu_cli_init();
    ctc_greatbelt_oam_cli_init();
    ctc_greatbelt_qos_cli_init();
    ctc_greatbelt_interrupt_cli_init();
    ctc_greatbelt_packet_cli_init();
    ctc_greatbelt_stacking_cli_init();
    ctc_greatbelt_linkagg_cli_init();
    ctc_greatbelt_dma_cli_init();
    ctc_greatbelt_aps_cli_init();
    ctc_greatbelt_chip_cli_init();
    ctc_greatbelt_ftm_cli_init();

    return 0;
}

int32
ctc_greatbelt_chip_special_cli_deinit(void)
{
    ctc_greatbelt_ftm_cli_deinit();
    ctc_greatbelt_chip_cli_deinit();
    ctc_greatbelt_aps_cli_deinit();
    ctc_greatbelt_dma_cli_deinit();
    ctc_greatbelt_linkagg_cli_deinit();
    ctc_greatbelt_stacking_cli_deinit();
    ctc_greatbelt_packet_cli_deinit();
    ctc_greatbelt_interrupt_cli_deinit();
    ctc_greatbelt_qos_cli_deinit();
    ctc_greatbelt_oam_cli_deinit();
    ctc_greatbelt_pdu_cli_deinit();
    ctc_greatbelt_mpls_cli_deinit();
    ctc_greatbelt_ipuc_cli_deinit();
    ctc_greatbelt_ipmc_cli_deinit();
    ctc_greatbelt_acl_cli_deinit();
    ctc_greatbelt_mirror_cli_deinit();
    ctc_greatbelt_port_cli_deinit();
    ctc_greatbelt_scl_cli_deinit();
    ctc_greatbelt_vlan_cli_deinit();
    ctc_greatbelt_l2_cli_deinit();
    ctc_greatbelt_nexthop_cli_deinit();

    return 0;
}

int32
ctc_greatbelt_chip_special_cli_callback_init(void)
{
    g_chip_special_cli[CTC_CHIP_GREATBELT].chip_special_cli_init = ctc_greatbelt_chip_special_cli_init;
    g_chip_special_cli[CTC_CHIP_GREATBELT].chip_special_cli_deinit = ctc_greatbelt_chip_special_cli_deinit;

    return 0;
}


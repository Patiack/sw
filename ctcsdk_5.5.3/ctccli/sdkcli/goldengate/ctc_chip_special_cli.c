
#include "sal.h"

#include "ctc_macro.h"
#include "ctc_chip.h"
#include "ctc_cli.h"
#include "ctc_cli_common.h"
#include "ctc_scl_cli.h"
#include "ctc_goldengate_port_cli.h"
#include "ctc_goldengate_acl_cli.h"
#include "ctc_goldengate_nexthop_cli.h"
#include "ctc_goldengate_l2_cli.h"
#include "ctc_goldengate_vlan_cli.h"
#include "ctc_goldengate_scl_cli.h"
#include "ctc_goldengate_security_cli.h"
#include "ctc_goldengate_ipmc_cli.h"
#include "ctc_goldengate_mirror_cli.h"
#include "ctc_goldengate_ipuc_cli.h"
#include "ctc_goldengate_mpls_cli.h"
#include "ctc_goldengate_qos_cli.h"
#include "ctc_goldengate_pdu_cli.h"
#include "ctc_goldengate_interrupt_cli.h"
#include "ctc_goldengate_packet_cli.h"
#include "ctc_goldengate_oam_cli.h"
#include "ctc_goldengate_stacking_cli.h"
#include "ctc_goldengate_linkagg_cli.h"
#include "ctc_goldengate_dma_cli.h"
#include "ctc_goldengate_aps_cli.h"
#include "ctc_goldengate_chip_cli.h"
#include "ctc_goldengate_overlay_tunnel_cli.h"
#include "ctc_goldengate_bpe_cli.h"
#include "ctc_goldengate_ipfix_cli.h"
#include "ctc_goldengate_monitor_cli.h"
#include "ctc_goldengate_stats_cli.h"
#include "ctc_goldengate_ftm_cli.h"
#include "ctc_goldengate_l3if_cli.h"
#include "ctc_goldengate_trill_cli.h"
#include "ctc_goldengate_fcoe_cli.h"
#ifdef CHIP_AGENT
#include "goldengate/ctc_chip_agent_cli.h"
#endif

extern ctc_chip_special_callback_fun_t g_chip_special_cli[MAX_CTC_CHIP_TYPE];

int32
ctc_goldengate_chip_special_cli_init(void)
{
    ctc_goldengate_nexthop_cli_init();
    ctc_goldengate_l2_cli_init();
    ctc_goldengate_vlan_cli_init();
    ctc_goldengate_scl_cli_init();
    ctc_goldengate_security_cli_init();
    ctc_goldengate_port_cli_init();
    ctc_goldengate_mirror_cli_init();
    ctc_goldengate_acl_cli_init();
    ctc_goldengate_bpe_cli_init();
    ctc_goldengate_ipmc_cli_init();
    ctc_goldengate_ipuc_cli_init();
    ctc_goldengate_mpls_cli_init();
    ctc_goldengate_pdu_cli_init();
    ctc_goldengate_oam_cli_init();
    ctc_goldengate_qos_cli_init();
    ctc_goldengate_interrupt_cli_init();
    ctc_goldengate_packet_cli_init();
    ctc_goldengate_stacking_cli_init();
    ctc_goldengate_linkagg_cli_init();
    ctc_goldengate_dma_cli_init();
    ctc_goldengate_aps_cli_init();
    ctc_goldengate_chip_cli_init();
    ctc_goldengate_overlay_tunnel_cli_init();
    ctc_goldengate_ipfix_cli_init();
    ctc_goldengate_monitor_cli_init();
    ctc_goldengate_stats_cli_init();
    ctc_goldengate_ftm_cli_init();
    ctc_goldengate_l3if_cli_init();
    ctc_goldengate_trill_cli_init();
    ctc_goldengate_fcoe_cli_init();
#ifdef CHIP_AGENT
    ctc_goldengate_chip_agent_cli_init();
#endif

    return 0;
}

int32
ctc_goldengate_chip_special_cli_deinit(void)
{
#ifdef CHIP_AGENT
    ctc_goldengate_chip_agent_cli_deinit();
#endif
    ctc_goldengate_fcoe_cli_deinit();
    ctc_goldengate_trill_cli_deinit();
    ctc_goldengate_l3if_cli_deinit();
    ctc_goldengate_ftm_cli_deinit();
    ctc_goldengate_stats_cli_deinit();
    ctc_goldengate_monitor_cli_deinit();
    ctc_goldengate_ipfix_cli_deinit();
    ctc_goldengate_overlay_tunnel_cli_deinit();
    ctc_goldengate_chip_cli_deinit();
    ctc_goldengate_aps_cli_deinit();
    ctc_goldengate_linkagg_cli_deinit();
    ctc_goldengate_stacking_cli_deinit();
    ctc_goldengate_packet_cli_deinit();
    ctc_goldengate_interrupt_cli_deinit();
    ctc_goldengate_qos_cli_deinit();
    ctc_goldengate_oam_cli_deinit();
    ctc_goldengate_pdu_cli_deinit();
    ctc_goldengate_mpls_cli_deinit();
    ctc_goldengate_ipuc_cli_deinit();
    ctc_goldengate_ipmc_cli_deinit();
    ctc_goldengate_bpe_cli_deinit();
    ctc_goldengate_acl_cli_deinit();
    ctc_goldengate_mirror_cli_deinit();
    ctc_goldengate_port_cli_deinit();
    ctc_goldengate_security_cli_deinit();
    ctc_goldengate_scl_cli_deinit();
    ctc_goldengate_vlan_cli_deinit();
    ctc_goldengate_l2_cli_deinit();
    ctc_goldengate_nexthop_cli_deinit();

    return 0;
}

int32
ctc_goldengate_chip_special_cli_callback_init(void)
{
    g_chip_special_cli[CTC_CHIP_GOLDENGATE].chip_special_cli_init = ctc_goldengate_chip_special_cli_init;
    g_chip_special_cli[CTC_CHIP_GOLDENGATE].chip_special_cli_deinit = ctc_goldengate_chip_special_cli_deinit;

    return 0;
}


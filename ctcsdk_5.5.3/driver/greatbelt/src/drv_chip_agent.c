#include "greatbelt/include/drv_lib.h"
#include "greatbelt/include/drv_chip_agent.h"

extern drv_chip_agent_mode_t g_gb_chip_agent_mode;
drv_chip_agent_t g_drv_gb_chip_agent_master;
bool g_gb_eadp_drv_debug_en = FALSE;

drv_chip_agent_mode_t g_gb_chip_agent_mode = DRV_CHIP_AGT_MODE_NONE;

int32
drv_greatbelt_chip_agent_mode(void)
{
    return g_gb_chip_agent_mode;
}

uint32
chip_agent_get_mode(void)
{
    return g_gb_chip_agent_mode;
}


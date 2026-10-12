#ifndef _CTC_DKIT_GOLDENGATE_INTERFACE_H_
#define _CTC_DKIT_GOLDENGATE_INTERFACE_H_


#ifdef __cplusplus
extern "C" {
#endif


#include "sal.h"

#define CTC_DKIT_INTERFACE_MAC_NUM 128

struct ctc_dkit_interface_status_s
{
    uint16 gport[CTC_DKIT_INTERFACE_MAC_NUM];
    uint8 mac_en[CTC_DKIT_INTERFACE_MAC_NUM];
    uint8 mac_mode[CTC_DKIT_INTERFACE_MAC_NUM];
    uint8 auto_neg[CTC_DKIT_INTERFACE_MAC_NUM];
    uint8 link_up[CTC_DKIT_INTERFACE_MAC_NUM];
    uint8 valid[CTC_DKIT_INTERFACE_MAC_NUM];
};
typedef struct ctc_dkit_interface_status_s ctc_dkit_interface_status_t;

extern int32
ctc_goldengate_dkit_interface_get_mac_status(uint8 lchip, ctc_dkit_interface_status_t* status);

extern int32
ctc_goldengate_dkit_interface_show_mac_status(void* p_para);

#ifdef __cplusplus
}
#endif

#endif


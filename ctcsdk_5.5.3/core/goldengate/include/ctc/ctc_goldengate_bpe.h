/**
 @file ctc_goldengate_bpe.h

 @author  Copyright (C) 2011 Centec Networks Inc.  All rights reserved.

 @date 2013-05-28

 @version v2.0

\p
 The BPE module is a function module, which stands for Bridge Port Extension.
 The module support 802.1Qbg, Bridge Port Extender, and Centec Mux/DeMux respectively.
\b
\p
 802.1Qbg is one of the Edge Virtual Bridging (EVB) standard to resolve the vSwitch
 limitation. A Virtual Ethernet Port Aggregator (VEPA) is a capability within a physical
 end station that collaborates with an adjacent, external bridge to provide bridging support
 between multiple virtual end stations and external networks. The VEPA collaborates by forwarding
 all station-originated frames to the adjacent bridge for frame processing and frame relay
 (including 'hairpin' forwarding) and by steering and replicating frames received
 from the VEPA uplink to the appropriate destinations.
\d Goldengate support VEPA by Port module's ctc_port_set_reflective_bridge_en()
\d The multi-channel VEPA take more advantages of network policies and security features, meanwhile overcome
 basic VEPA's limitation in bridging functions, such as Spanning tree, which can be nonstandard
 across deployments.
 Goldengate support multi-channel VEPA by ctc_bpe_set_port_extender() with mode CTC_BPE_8021QBG_M_VEPA.
\b
\p
 802.1BR is one of the VM deploy technology in datacenter networks which proposed by Cisco.
 The Bridge Port Extender is compose of at least one control bridge (CB) and one or more port extender (PE).
 The CB in Bridge Port Extender is a central control bridge, and make packet forward decision which
 received from PE. The PE relay the packet received from its Extended Port to CB through uplink Cascade Port port.
\b
\p
 There are three port type in Extended Bridge:
\pp
\d Upstream Port:
 The Port Extender Upstream Port provides connectivity to the Controlling Bridge Cascade Port or
 to the Cascade Port of another Port Extender. The Upstream Port allows frames that carry E-TAGs.
 Upstream Port exist in PE. Goldengate support Upstream Port by ctc_bpe_set_port_extender()
 with mode CTC_BPE_8021BR_UPSTREAM.
\pp
\d Cascade Port:
 The Cascade Port is used exclusively to provide connectivity to a cascaded Port Extender. The Cascade Port
 allows frames that carry E-TAGs.
 Cascade Port exists in CB and PE. Goldengate support Cascade Port by ctc_bpe_set_port_extender()
 with mode CTC_BPE_8021BR_CB_CASCADE exclusively for CB, mode CTC_BPE_8021BR_PE_CASCADE specifically for PE.
\pp
\d Extended Port:
 The Extended Port operates as a Port of the Controlling Bridge. Extended Ports do not allow frames
 that carry E-TAGs.
 Extended Port exist in CB and PE. Goldengate support Extended Port by ctc_bpe_set_port_extender()
 with mode CTC_BPE_8021BR_EXTENDED no matter of CB or PE.
\b
\p
 Centec Mux/DeMux is a centec private port extend technology in order to extend panel port. Goldengate
 support maximum 512 panel port by downlink its extender layer2 switch. The packet between goldengate and
 layer2 switch carry a mux header which identify the extender port id in layer2 switch. Goldengate map the
 physical port by mux header. The extender port in layer2 switch should not recognize any vlan tag and forward to
 goldengate through its uplink port.
 Goldengate support Mux/DeMux by ctc_bpe_set_port_extender() with mode CTC_BPE_MUX_DEMUX_MODE.
\b

 \S ctc_bpe.h:ctc_bpe_externder_mode_t
 \S ctc_bpe.h:ctc_bpe_extender_t
*/

#ifndef _CTC_GOLDENGATE_BPE_H
#define _CTC_GOLDENGATE_BPE_H
#ifdef __cplusplus
extern "C" {
#endif
/***************************************************************
 *
 * Header Files
 *
 ***************************************************************/

#include "ctc_bpe.h"

/**
 @brief Initialize the bpe module

 @param[in] lchip    local chip id

 @param[in] bpe_global_cfg bpe global configure

 @remark  Initialize bpe with global config

 @return CTC_E_XXX

*/
extern int32
ctc_goldengate_bpe_init(uint8 lchip, void* bpe_global_cfg);

/**
 @brief De-Initialize bpe module

 @param[in] lchip    local chip id

 @remark  User can de-initialize the bpe configuration

 @return CTC_E_XXX
*/
extern int32
ctc_goldengate_bpe_deinit(uint8 lchip);

/**
 @brief Set port extender function

 @param[in] lchip    local chip id

 @param[in] gport global port to enable/disable BPE function

 @param[in] p_extender port extend information for BPE function

 @remark set port extender of global port
 @return CTC_E_XXX

*/
extern int32
ctc_goldengate_bpe_set_port_extender(uint8 lchip, uint32 gport, ctc_bpe_extender_t* p_extender);

/**
 @brief Get port extender enable status

 @param[in] lchip    local chip id

 @param[in] gport global port to enable/disable BPE function

 @param[out] enable enable or disable

 @remark get port extender of global port
 @return CTC_E_XXX

*/
extern int32
ctc_goldengate_bpe_get_port_extender(uint8 lchip, uint32 gport, bool* enable);

#ifdef __cplusplus
}
#endif

#endif

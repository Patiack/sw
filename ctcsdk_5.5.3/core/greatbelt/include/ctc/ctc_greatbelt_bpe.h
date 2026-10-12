/**
 @file ctc_greatbelt_bpe.h

 @author  Copyright (C) 2011 Centec Networks Inc.  All rights reserved.

 @date 2013-05-28

 @version v2.0

\p
 The BPE module is a function module, which stands for Bridge Port Extension.
 The module support VEPA, Bridge Port Extender, and Centec Mux/DeMux respectively.
\b
\p
 Centec Mux/DeMux is a centec private port extend technology in order to extend panel port. Greatbelt
 support maximum 512 panel port by its downlink extender layer2 switch. The packet between greatbelt and
 layer2 switch carry a mux header which identify the extender port id in layer2 switch. Greatbelt map the
 physical port by mux header. The extender port in layer2 switch should not recognize any vlan tag and forward to
 greatbelt through its uplink port.
 Greatbelt support Mux/DeMux by ctc_bpe_set_port_extender() with mode CTC_BPE_MUX_DEMUX_MODE.
\b
\p
 InterLaken Interface is a high-speed channelized interface, which used as interface with NP. When GreatBelt
 used in interlaken mode, it just works as MAC Aggregation. Mux/Demux Interface is another Port Extension function.
 Greatbelt support interlaken by ctc_bpe_set_intlk_en();

 \S ctc_bpe.h:ctc_bpe_externder_mode_e
 \S ctc_bpe.h:ctc_bpe_extender_t
*/

 #ifndef _CTC_GREATBELT_BPE_H
#define _CTC_GREATBELT_BPE_H
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
 @brief The function is to init the bpe module

 @param[in] lchip    local chip id

 @param[in] bpe_global_cfg bpe global configure

 @remark  Initialize bpe with global config

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_bpe_init(uint8 lchip, void* bpe_global_cfg);

/**
 @brief De-Initialize bpe module

 @param[in] lchip    local chip id

 @remark  User can de-initialize the bpe configuration

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_bpe_deinit(uint8 lchip);

/**
 @brief The interface is to enable interlaken function

 @param[in] lchip    local chip id

 @param[in] enable enable Interlaken or not

 @remark enable bpe interlaken
 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_bpe_set_intlk_en(uint8 lchip, bool enable);

/**
 @brief The interface is to get enable interlaken function

 @param[in] lchip    local chip id

 @param[out] enable enable Interlaken or not

 @remark get interlaken is enable or not
 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_bpe_get_intlk_en(uint8 lchip, bool* enable);

/**
 @brief The interface is to set port mux/demux function

 @param[in] lchip    local chip id

 @param[in] gport global port to enable/disable mux demux function

 @param[in] p_extender port extend information for mux/demux function

 @remark set port extender of global port
 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_bpe_set_port_extender(uint8 lchip, uint32 gport, ctc_bpe_extender_t* p_extender);

/**
 @brief The interface is to get mux/demux enable

 @param[in] lchip    local chip id

 @param[in] gport global port to enable/disable mux demux function

 @param[out] enable enable or disable

 @remark get port extender of global port
 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_bpe_get_port_extender(uint8 lchip, uint32 gport, bool* enable);

#ifdef __cplusplus
}
#endif

#endif

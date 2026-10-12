/**
 @file ctc_greatbelt_ptp.h

 @author  Copyright (C) 2011 Centec Networks Inc.  All rights reserved.

 @date 2012-09-17

 @version v2.0

 The IEEE 1588v2 Precision Time Protocol (PTP) defines a packet-based time synchronization method that provides frequency,
 phase and time-of-day information with sub-microsecond accuracy.
 \p
 The chip supports another two interfaces to synchronize clock phase:
 \p
 (1)The Sync Interface is used to synchronize the time information between other chip and local clock. Sync Interface
 consists of 3 bi-directional signals:
 \p
 \d  SyncPulse: 0.001Hz - 250KHz
 \d  SyncClock: 1Hz - 25MHz
 \d  SyncCode: 97bits serial data w/ CRC protection
\p
 (2)The ToD Interface is used to synchronize the time information between other device and local clock. ToD information
 has the baud rate of 9600, without even-odd check. 1 start bit with low level. 1 stop bit with high level. Idle frame
 is high level with 8 data bits. The ToD frame starts in 1ms and finishes in 500ms. The ToD describes the time information
 of 1PPS raise edge. ToD should be trigged each second. For pulse per second, the raise edge should be referenced. The
 raise should be less than 50ns and the pulse width should be in the range of 20ms to 200ms.
\p
 The module provides APIs to support PTP:
\p
\d   (1)Use ctc_ptp_set_device_type() to set the device type: none, OC, BC, E2E_TC, P2P_TC, refer to ctc_ptp_device_type_t.
\d   (2)Set the latency, asymmetry, path delay by ctc_ptp_set_adjust_delay(), then the PTP message passing the chip will be
      adjusted those delay into CF.
\d   (3)After get the offset and drift by PTP protocol, use ctc_ptp_adjust_clock_offset() and ctc_ptp_set_clock_drift() to
      adjust local clock phase and frequency.
\d   (4)The module provides APIs to support Sync Interface, which can be used for time synchronization from or to other chip:
        ctc_ptp_set_sync_intf();
        ctc_ptp_set_sync_intf_toggle_time();
\d   (5)The module provides APIs to support Tod Interface, which can be used for time synchronization from or to other device:
        ctc_ptp_set_tod_intf();
        ctc_ptp_set_tod_intf_tx_code();
*/

#ifndef _CTC_GREATBELT_PTP_H
#define _CTC_GREATBELT_PTP_H
#ifdef __cplusplus
extern "C" {
#endif

/****************************************************************
 *
 * Header Files
 *
 ***************************************************************/
#include "ctc_const.h"
#include "ctc_ptp.h"
/****************************************************************
*
* Defines and Macros
*
****************************************************************/
/****************************************************************************
 *
* Function
*
*****************************************************************************/
/**
 @addtogroup PTP
 @{
*/

/**
 @brief Initialize PTP module and set ptp default config

 @param[in] lchip    local chip id

 @param[in]  p_ptp_cfg  The configuration of PTP

 @remark When p_ptp_cfg is NULL,ptp will use default global config to initialize sdk.
\p
         By default:
\p
\d         (1)Enable process ucast PTP message, p_ptp_cfg->ptp_ucast_en can change it, and use ctc_ptp_set_global_property()
            is another way.
\d         (2)Sync, signaling, management message will not copy to cpu when set the device as TC, p_ptp_cfg->sync_copy_to_cpu_en,
            p_ptp_cfg->signaling_copy_to_cpu_en and p_ptp_cfg->management_copy_to_cpu_en can change them, and use
            ctc_ptp_set_global_property() is another way.
\d         (3)P2P TC will not process Delay_Request message, p_ptp_cfg->delay_request_process_en can change it, and use
            ctc_ptp_set_global_property() is another way.
\d         (4)Select port_based_ptp_en enable, it means enable PTP by port, p_ptp_cfg->port_based_ptp_en can change it, and
            use ctc_ptp_set_global_property() is another way.
\d         (5)Enable syncClock, syncPulse, syncCode signal enable for Sync Interface.
\d         (6)Enable pulse and code signal for ToD Interface.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_ptp_init(uint8 lchip, ctc_ptp_global_config_t* p_ptp_cfg);

/**
 @brief De-Initialize ptp module

 @param[in] lchip    local chip id

 @remark  User can de-initialize the ptp configuration

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_ptp_deinit(uint8 lchip);

/**
 @brief Set ptp property

 @param[in] lchip    local chip id

 @param[in]  property  Enumeration of PTP property

 @param[in]  value  Property value

 @remark  Set ptp property, refer to ctc_ptp_global_prop_t

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_ptp_set_global_property(uint8 lchip, ctc_ptp_global_prop_t property, uint32 value);

/**
 @brief Get ptp property

 @param[in] lchip    local chip id

 @param[in]  property  Enumeration of PTP property

 @param[out]  value  Property value

 @remark  Get ptp property

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_ptp_get_global_property(uint8 lchip, ctc_ptp_global_prop_t property, uint32* value);

/**
 @brief Get local clock timestamp

 @param[in] lchip_id    local chip id

 @param[out]  p_timestamp  Timestamp read from free running clock

 @remark Get local clock timestamp from free running clock(FRC)

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_ptp_get_clock_timestamp(uint8 lchip_id, ctc_ptp_time_t* p_timestamp);

/**
 @brief Adjust offset for local clock

 @param[in] lchip_id    local chip id

 @param[in] p_offset  Offset different from master clock

 @remark (1)The clock offset will be adjusted at the next clock cycle;
 \p
         (2)The clock must be set to a time after 1990 when adjust the clock at the first time(p_timestamp.seconds >= 315964819),
            so that it can be set ToW of TOD Interface;
\p
         (3)p_timestamp->nano_nanoseconds is invalid,and p_timestamp->is_negative indicates the sign of clock offset(both
            seconds and nanoseconds)
\p
 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_ptp_adjust_clock_offset(uint8 lchip_id, ctc_ptp_time_t* p_offset);

/**
 @brief Set drift for local clock

 @param[in] lchip_id    local chip id

 @param[in]  p_drift  The drift parameter defined as amount of time to drift per 1 second

 @remark  The range of p_drift->nanoseconds is <0 - (10^9/quanta-1)>,for example,if quanta = 8,than the range is <0-124999999>,
          the p_drift->seconds and p_drift->nano_nanoseconds is invalid

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_ptp_set_clock_drift(uint8 lchip_id, ctc_ptp_time_t* p_drift);

/**
 @brief Get drift value

 @param[in] lchip_id    local chip id

 @param[out]  p_drift  The drift parameter defined as amount of time to drift per 1 second

 @remark  Get drift value

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_ptp_get_clock_drift(uint8 lchip_id, ctc_ptp_time_t* p_drift);

/**
 @brief Set PTP device type

 @param[in] lchip    local chip id

 @param[in]  device_type  The type of PTP device

 @remark  Set PTP device type, refer to ctc_ptp_device_type_t

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_ptp_set_device_type(uint8 lchip, ctc_ptp_device_type_t device_type);

/**
 @brief Get PTP device type

 @param[in] lchip    local chip id

 @param[out]  p_device_type  The type of PTP device

 @remark  Get PTP device type

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_ptp_get_device_type(uint8 lchip, ctc_ptp_device_type_t* p_device_type);

/**
 @brief Set adjust delay for PTP message delay correct

 @param[in] lchip    local chip id

 @param[in]  gport  Global port ID

 @param[in]  type  Enum of delay type

 @param[in]  value  The value of delay,unit:nanosecond

 @remark  The latency, asymmetry and path dalay will be corrected when a PTP message pass the device, refer to
          ctc_ptp_adjust_delay_type_t

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_ptp_set_adjust_delay(uint8 lchip, uint32 gport, ctc_ptp_adjust_delay_type_t type, int64 value);

/**
 @brief Set adjust delay for PTP message delay correct

 @param[in] lchip    local chip id

 @param[in]  gport  Global port ID

 @param[in]  type  Enum of delay type

 @param[out]  value  The value of delay,unit:nanosecond

 @remark  The latency, asymmetry and path dalay will be corrected when a PTP message pass the device

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_ptp_get_adjust_delay(uint8 lchip, uint32 gport, ctc_ptp_adjust_delay_type_t type, int64* value);

/**
 @brief  Set Sync Interface config

 @param[in] lchip_id    local chip id

 @param[in]  p_sync_cfg  Configuration of Sync Interface

 @remark  p_sync_cfg->lock and p_sync_cfg->accuracy is used for output mode,and the range of parameter must be set as follow:
\p
\d            p_sync_cfg->accuracy <32-49 or 0xFE>
\d            p_sync_cfg->sync_clock_hz <0-25000000>
\d            p_sync_cfg->sync_pulse_hz <0-250000>
\d            p_sync_cfg->sync_pulse_duty<1-99>
\p
          in addition, the signal frequency must satisfy:
\p
\d            (1)sync_clock_hz must be integer multiples of sync_pulse_hz
\d            (2)Tp > (SYS_PTP_MAX_SYNC_CODE_BIT+config.epoch)*Tc,Tp is the period of sync_pulse_hz, and Tc is the period
            of sync_clock_hz

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_ptp_set_sync_intf(uint8 lchip_id, ctc_ptp_sync_intf_cfg_t* p_sync_cfg);

/**
 @brief  Get Sync Interface config

 @param[in] lchip_id    local chip id

 @param[in]  p_sync_cfg  Configuration of Sync Interface

 @remark  p_sync_cfg->lock and p_sync_cfg->accuracy is used for output mode

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_ptp_get_sync_intf(uint8 lchip_id, ctc_ptp_sync_intf_cfg_t* p_sync_cfg);

/**
 @brief  Set toggle time to trigger Sync Interface output syncColck signal

 @param[in] lchip_id    local chip id

 @param[in]  p_toggle_time  Toggle time to trigger Sync Interface output syncColck signal

 @remark  In order to output syncClock signal, the toggle time should be later than local clock time, and the toggle time
          should be set after adjust clock offset every time

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_ptp_set_sync_intf_toggle_time(uint8 lchip_id, ctc_ptp_time_t* p_toggle_time);

/**
 @brief  Get toggle time to trigger Sync Interface output syncColck signal

 @param[in] lchip_id    local chip id

 @param[out]  p_toggle_time  Toggle time to trigger Sync Interface output syncColck signal

 @remark  Get toggle time to trigger Sync Interface output syncColck signal

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_ptp_get_sync_intf_toggle_time(uint8 lchip_id, ctc_ptp_time_t* p_toggle_time);

/**
 @brief  Get Sync Interface input time code

 @param[in] lchip_id    local chip id

 @param[out]  p_time_code  The time information receive by Sync Interface

 @remark  Get input time code from Sync Interface

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_ptp_get_sync_intf_code(uint8 lchip_id, ctc_ptp_sync_intf_code_t* p_time_code);

/**
 @brief  Clear Sync Interface input time code

 @param[in] lchip    local chip id

 @remark  The time code must be cleared in order to receive next time code

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_ptp_clear_sync_intf_code(uint8 lchip);

/**
 @brief  Set ToD Interface config

 @param[in] lchip_id    local chip id

 @param[in]  p_tod_cfg  The configuration of ToD Interface

 @remark  Set ToD Interface config, refer ctc_ptp_tod_intf_cfg_t

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_ptp_set_tod_intf(uint8 lchip_id, ctc_ptp_tod_intf_cfg_t* p_tod_cfg);

/**
 @brief  Get ToD Interface config

 @param[in] lchip_id    local chip id

 @param[out]  p_tod_cfg  The configuration of ToD Interface

 @remark  Get ToD Interface config

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_ptp_get_tod_intf(uint8 lchip_id, ctc_ptp_tod_intf_cfg_t* p_tod_cfg);

/**
 @brief  Set ToD Interface output code config

 @param[in] lchip_id    local chip id

 @param[in]  p_tx_code  The configuration of ToD Interface output code

 @remark  Set ToD Interface output code config, refer to ctc_ptp_tod_intf_code_t

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_ptp_set_tod_intf_tx_code(uint8 lchip_id, ctc_ptp_tod_intf_code_t* p_tx_code);

/**
 @brief  Get ToD Interface output code config

 @param[in] lchip_id    local chip id

 @param[out]  p_tx_code  The configuration of ToD Interface output code

 @remark  Get ToD Interface output code config

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_ptp_get_tod_intf_tx_code(uint8 lchip_id, ctc_ptp_tod_intf_code_t* p_tx_code);

/**
 @brief  Get ToD Interface input time code

 @param[in] lchip_id    local chip id

 @param[out]  p_rx_code  The time code received by ToD Interface

 @remark  Get ToD Interface input time code

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_ptp_get_tod_intf_rx_code(uint8 lchip_id, ctc_ptp_tod_intf_code_t* p_rx_code);

/**
 @brief  Clear ToD Interface input time code

 @param[in] lchip    local chip id

 @remark  The time code must be cleared in order to receive next time code

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_ptp_clear_tod_intf_code(uint8 lchip);

/**
 @brief  Get timestamp captured by local clock

 @param[in] lchip_id    local chip id

 @param[in,out]  p_captured_ts  Timestamp source type and sequence id, Captured timestamp

 @remark  p_captured_ts->type indicates the captured type(TX or RX)
 \p
 \d           TX: Ts captured when PTP message output at mac
 \d           RX: Ts captured when SyncPulse or 1PPS arrives
 \p
          refer to ctc_ptp_capured_ts_t

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_ptp_get_captured_ts(uint8 lchip_id, ctc_ptp_capured_ts_t* p_captured_ts);

/**@} end of @addgroup   */

#ifdef __cplusplus
}
#endif

#endif


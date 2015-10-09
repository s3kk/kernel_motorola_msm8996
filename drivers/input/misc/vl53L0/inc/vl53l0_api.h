/*******************************************************************************
Copyright � 2015, STMicroelectronics International N.V.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of STMicroelectronics nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, AND
NON-INFRINGEMENT OF INTELLECTUAL PROPERTY RIGHTS ARE DISCLAIMED.
IN NO EVENT SHALL STMICROELECTRONICS INTERNATIONAL N.V. BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*******************************************************************************/
/*
 * @file vl53l0_api.h
 * $Date: 2014-12-04 16:15:06 +0100 (Thu, 04 Dec 2014) $
 * $Revision: 1906 $
 */



#ifndef _VL53L0_API_H_
#define _VL53L0_API_H_

#include "vl53l0_strings.h"
#include "vl53l0_def.h"
#include "vl53l0_platform.h"


#ifdef __cplusplus
extern "C" {
#endif


#ifdef _MSC_VER
#   ifdef VL53L0_API_EXPORTS
#       define VL53L0_API  __declspec(dllexport)
#   else
#       define VL53L0_API
#   endif
#else
#   define VL53L0_API
#endif


/** @defgroup VL53L0_general_group VL53L0 General Functions
 *  @brief    General functions and definitions
 *  @{
 */

/**
 * @brief Return the VL53L0 PAL Implementation Version
 *
 * @note This function doesn't access to the device
 *
 * @param   pVersion              Pointer to current PAL Implementation Version
 * @return  VL53L0_ERROR_NONE        Success
 * @return  "Other error code"    See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_GetVersion(VL53L0_Version_t *pVersion);

/**
 * @brief Return the PAL Specification Version used for the current
 * implementation.
 *
 * @note This function doesn't access to the device
 *
 * @param   pPalSpecVersion       Pointer to current PAL Specification Version
 * @return  VL53L0_ERROR_NONE        Success
 * @return  "Other error code"    See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_GetPalSpecVersion(
			VL53L0_Version_t *pPalSpecVersion);


/**
 * @brief Reads the Device information for given Device
 *
 * @note This function Access to the device
 *
 * @param   Dev                 Device Handle
 * @param   pVL53L0_DeviceInfo     Pointer to current device info for a given
 * Device
 * @return  VL53L0_ERROR_NONE      Success
 * @return  "Other error code"  See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_GetDeviceInfo(VL53L0_DEV Dev,
			VL53L0_DeviceInfo_t *pVL53L0_DeviceInfo);


/**
 * @brief Read current status of the error register for the selected device
 *
 * @note This function Access to the device
 *
 * @param   Dev                   Device Handle
 * @param   pDeviceErrorStatus    Pointer to current error code of the device
 * @return  VL53L0_ERROR_NONE        Success
 * @return  "Other error code"    See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_GetDeviceErrorStatus(VL53L0_DEV Dev,
			VL53L0_DeviceError * pDeviceErrorStatus);

/**
 * @brief Human readable error string for a given Error Code
 *
 * @note This function doesn't access to the device
 *
 * @param   ErrorCode             The error code as stored on
 * ::VL53L0_DeviceError
 * @param   pDeviceErrorString    The error string corresponding to the
 * ErrorCode
 * @return  VL53L0_ERROR_NONE        Success
 * @return  "Other error code"    See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_GetDeviceErrorString(
			VL53L0_DeviceError ErrorCode, char *pDeviceErrorString);


/**
 * @brief Human readable error string for current PAL error status
 *
 * @note This function doesn't access to the device
 *
 * @param   PalErrorCode          The error code as stored on @a VL53L0_Error
 * @param   pPalErrorString       The error string corresponding to the
 * PalErrorCode
 * @return  VL53L0_ERROR_NONE        Success
 * @return  "Other error code"    See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_GetPalErrorString(VL53L0_Error PalErrorCode,
			char *pPalErrorString);


/**
 * @brief Reads the internal state of the PAL for a given Device
 *
 * @note This function doesn't access to the device
 *
 * @param   Dev                   Device Handle
 * @param   pPalState             Pointer to current state of the PAL for a
 * given Device
 * @return  VL53L0_ERROR_NONE     Success
 * @return  "Other error code"    See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_GetPalState(VL53L0_DEV Dev,
			VL53L0_State * pPalState);


/**
 * @brief Set the power mode for a given Device
 * The power mode can be Standby or Idle. Different level of both Standby and
 * Idle can exists.
 * This function should not be used when device is in Ranging state.
 *
 * @note This function Access to the device
 *
 * @param   Dev                   Device Handle
 * @param   PowerMode             The value of the power mode to set.
 * see ::VL53L0_PowerModes Valid values are:
 * VL53L0_POWERMODE_STANDBY_LEVEL1, VL53L0_POWERMODE_IDLE_LEVEL1
 * @return  VL53L0_ERROR_NONE                  Success
 * @return  VL53L0_ERROR_MODE_NOT_SUPPORTED    This error occurs when PowerMode
 * is not in the supported list
 * @return  "Other error code"    See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_SetPowerMode(VL53L0_DEV Dev,
			VL53L0_PowerModes PowerMode);

/**
 * @brief Get the power mode for a given Device
 *
 * @note This function Access to the device
 *
 * @param   Dev                   Device Handle
 * @param   pPowerMode            Pointer to the current value of the power
 * mode. see ::VL53L0_PowerModes. Valid values are:
 * VL53L0_POWERMODE_STANDBY_LEVEL1, VL53L0_POWERMODE_IDLE_LEVEL1
 * @return  VL53L0_ERROR_NONE     Success
 * @return  "Other error code"    See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_GetPowerMode(VL53L0_DEV Dev,
			VL53L0_PowerModes * pPowerMode);


/**
 * Set or over-hide part to part calibration offset
 * \sa VL53L0_DataInit()   VL53L0_GetOffsetCalibrationDataMicroMeter()
 *
 * @note This function Access to the device
 *
 * @param   Dev                                Device Handle
 * @param   OffsetCalibrationDataMicroMeter    Offset (in micrometer)
 * @return  VL53L0_ERROR_NONE                  Success
 * @return  "Other error code"                 See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_SetOffsetCalibrationDataMicroMeter(
			VL53L0_DEV Dev,
			int32_t OffsetCalibrationDataMicroMeter);

/**
 * @brief Get part to part calibration offset
 *
 * @par Function Description
 * Should only be used after a successful call to @a VL53L0_DataInit to backup
 * device NVM value
 *
 * @note This function Access to the device
 *
 * @param   Dev                                Device Handle
 * @param   pOffsetCalibrationDataMicroMeter   Return part to part calibration
 * offset from device (in micro meter)
 * @return  VL53L0_ERROR_NONE                  Success
 * @return  "Other error code"                 See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_GetOffsetCalibrationDataMicroMeter(
			VL53L0_DEV Dev,
			int32_t *pOffsetCalibrationDataMicroMeter);

/**
 * Set Group parameter Hold state
 *
 * @par Function Description
 * Set or remove device internal group parameter hold
 *
 * @note This function is not Implemented
 *
 * @param   Dev      Device Handle
 * @param   GroupParamHold   Group parameter Hold state to be set (on/off)
 * @return  VL53L0_ERROR_NOT_IMPLEMENTED        Not implemented
 */
VL53L0_API VL53L0_Error VL53L0_SetGroupParamHold(VL53L0_DEV Dev,
			uint8_t GroupParamHold);

/**
 * @brief Get the maximal distance for actual setup
 * @par Function Description
 * Device must be initialized through @a VL53L0_SetParameters() prior calling
 * this function.
 *
 * Any range value more than the value returned is to be considered as "no
 * target detected"
 * or "no target in detectable range"\n
 * @warning The maximal distance depends on the setup
 *
 * @note This function is not Implemented
 *
 * @param   Dev      Device Handle
 * @param   pUpperLimitMilliMeter   The maximal range limit for actual setup
 * (in millimeter)
 * @return  VL53L0_ERROR_NOT_IMPLEMENTED        Not implemented
 */
VL53L0_API VL53L0_Error VL53L0_GetUpperLimitMilliMeter(VL53L0_DEV Dev,
			uint16_t *pUpperLimitMilliMeter);

/** @} VL53L0_general_group */


/** @defgroup VL53L0_init_group VL53L0 Init Functions
 *  @brief    VL53L0 Init Functions
 *  @{
 */

/**
 * @brief Set new device address
 *
 * After completion the device will answer to the new address programmed. This
 * function should be called when several devices are used in parallel
 * before start programming the sensor.
 * When a single device us used, there is no need to call this function.
 *
 * @note This function Access to the device
 *
 * @param   Dev                   Device Handle
 * @param   DeviceAddress         The new Device address
 * @return  VL53L0_ERROR_NONE     Success
 * @return  "Other error code"    See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_SetDeviceAddress(VL53L0_DEV Dev,
			uint8_t DeviceAddress);

/**
 *
 * @brief One time device initialization
 *
 * To be called once and only once after device is brought out of reset (Chip
 * enable) and booted see @a VL53L0_WaitDeviceBooted()
 *
 * @par Function Description
 * When not used after a fresh device "power up" or reset, it may return @a
 * #VL53L0_ERROR_CALIBRATION_WARNING
 * meaning wrong calibration data may have been fetched from device that can
 * result in ranging offset error\n
 * If application cannot execute device reset or need to run VL53L0_DataInit
 * multiple time
 * then it  must ensure proper offset calibration saving and restore on its own
 * by using @a VL53L0_GetOffsetCalibrationData() on first power up and then @a
 * VL53L0_SetOffsetCalibrationData() in all subsequent init
 * This function will change the VL53L0_State from VL53L0_STATE_POWERDOWN to
 * VL53L0_STATE_WAIT_STATICINIT.
 *
 * @note This function Access to the device
 *
 * @param   Dev                   Device Handle
 * @return  VL53L0_ERROR_NONE     Success
 * @return  "Other error code"    See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_DataInit(VL53L0_DEV Dev);

/**
 * @brief Do basic device init (and eventually patch loading)
 * This function will change the VL53L0_State from VL53L0_STATE_WAIT_STATICINIT
 * to VL53L0_STATE_IDLE.
 * In this stage all defalut setting will be applied.
 *
 * @note This function Access to the device
 *
 * @param   Dev                   Device Handle
 * @return  VL53L0_ERROR_NONE     Success
 * @return  "Other error code"    See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_StaticInit(VL53L0_DEV Dev);

/**
 * @brief Wait for device booted after chip enable (hardware standby)
 * This function can be run only when VL53L0_State is VL53L0_STATE_POWERDOWN.
 *
 * @note This function is not Implemented
 *
 * @param   Dev      Device Handle
 * @return  VL53L0_ERROR_NOT_IMPLEMENTED        Not implemented
 *
 */
VL53L0_API VL53L0_Error VL53L0_WaitDeviceBooted(VL53L0_DEV Dev);

/**
 * @brief Do an hard reset or soft reset (depending on implementation) of the
 * device \n
 * After call of this function, device must be in same state as right after a
 * power-up sequence.
 * This function will change the VL53L0_State to VL53L0_STATE_POWERDOWN.
 *
 * @note This function Access to the device
 *
 * @param   Dev                   Device Handle
 * @return  VL53L0_ERROR_NONE     Success
 * @return  "Other error code"    See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_ResetDevice(VL53L0_DEV Dev);

/** @} VL53L0_init_group */


/** @defgroup VL53L0_parameters_group VL53L0 Parameters Functions
 *  @brief    Functions used to prepare and setup the device
 *  @{
 */

/**
 * @brief  Prepare device for operation
 * @par Function Description
 * Update device with provided parameters
 * @li Then start ranging operation.
 *
 * @note This function Access to the device
 *
 * @param   Dev                   Device Handle
 * @param   pDeviceParameters     Pointer to store current device parameters.
 * @return  VL53L0_ERROR_NONE     Success
 * @return  "Other error code"    See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_SetDeviceParameters(VL53L0_DEV Dev,
			const VL53L0_DeviceParameters_t *pDeviceParameters);

/**
 * @brief  Retrieve current device parameters
 * @par Function Description
 * Get actual parameters of the device
 * @li Then start ranging operation.
 *
 * @note This function Access to the device
 *
 * @param   Dev                   Device Handle
 * @param   pDeviceParameters     Pointer to store current device parameters.
 * @return  VL53L0_ERROR_NONE     Success
 * @return  "Other error code"    See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_GetDeviceParameters(VL53L0_DEV Dev,
			VL53L0_DeviceParameters_t *pDeviceParameters);

/**
 * @brief  Set a new device mode
 * @par Function Description
 * Set device to a new mode (ranging, histogram ...)
 *
 * @note This function doesn't Access to the device
 *
 * @param   Dev                   Device Handle
 * @param   DeviceMode            New device mode to apply
 *                                Valid values are:
 *                                VL53L0_DEVICEMODE_SINGLE_RANGING
 *                                VL53L0_DEVICEMODE_CONTINUOUS_RANGING
 *                                VL53L0_DEVICEMODE_CONTINUOUS_TIMED_RANGING
 *                                VL53L0_DEVICEMODE_SINGLE_HISTOGRAM
 * (functionality not available)
 *
 * @return  VL53L0_ERROR_NONE                   Success
 * @return  VL53L0_ERROR_MODE_NOT_SUPPORTED     This error occurs when
 * DeviceMode is not in the supported list
 */
VL53L0_API VL53L0_Error VL53L0_SetDeviceMode(VL53L0_DEV Dev,
			VL53L0_DeviceModes DeviceMode);

/**
 * @brief  Get current new device mode
 * @par Function Description
 * Get actual mode of the device(ranging, histogram ...)
 *
 * @note This function doesn't Access to the device
 *
 * @param   Dev                   Device Handle
 * @param   pDeviceMode           Pointer to current apply mode value
 *                                Valid values are:
 *                                VL53L0_DEVICEMODE_SINGLE_RANGING
 *                                VL53L0_DEVICEMODE_CONTINUOUS_RANGING
 *                                VL53L0_DEVICEMODE_CONTINUOUS_TIMED_RANGING
 *                                VL53L0_DEVICEMODE_SINGLE_HISTOGRAM
 * (functionality not available)
 *
 * @return  VL53L0_ERROR_NONE                   Success
 * @return  VL53L0_ERROR_MODE_NOT_SUPPORTED     This error occurs when
 * DeviceMode is not in the supported list
 */
VL53L0_API VL53L0_Error VL53L0_GetDeviceMode(VL53L0_DEV Dev,
			VL53L0_DeviceModes * pDeviceMode);

/**
 * @brief  Set a new Histogram mode
 * @par Function Description
 * Set device to a new Histogram mode
 *
 * @note This function doesn't Access to the device
 *
 * @param   Dev                   Device Handle
 * @param   HistogramMode         New device mode to apply
 *                                Valid values are:
 *                                VL53L0_HISTOGRAMMODE_DISABLED
 * @return  VL53L0_ERROR_NONE                   Success
 * @return  VL53L0_ERROR_MODE_NOT_SUPPORTED     This error occurs when
 * HistogramMode is not in the supported list
 * @return  "Other error code"    See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_SetHistogramMode(VL53L0_DEV Dev,
			VL53L0_HistogramModes HistogramMode);

/**
 * @brief  Get current new device mode
 * @par Function Description
 * Get current Histogram mode of a Device
 *
 * @note This function doesn't Access to the device
 *
 * @param   Dev                   Device Handle
 * @param   pHistogramMode        Pointer to current Histogram Mode value
 *                                Valid values are:
 *                                VL53L0_HISTOGRAMMODE_DISABLED
 * @return  VL53L0_ERROR_NONE     Success
 * @return  "Other error code"    See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_GetHistogramMode(VL53L0_DEV Dev,
			VL53L0_HistogramModes * pHistogramMode);

/**
 * @brief Set Ranging Timing Budget in microseconds
 *
 * @par Function Description
 * Defines the maximum time allowed by the user to the device to run a full
 * ranging sequence
 * for the current mode (ranging, histogram, ASL ...)
 *
 * @note This function Access to the device
 *
 * @param   Dev                                Device Handle
 * @param MeasurementTimingBudgetMicroSeconds  Max measurement time in
 * microseconds.
 *                                             Valid values are:
 *                                             >= 17000 microseconds when
 * wraparound is enabled
 *                                             >= 12000 microseconds when
 * wraparound is disabled
 * @return  VL53L0_ERROR_NONE                  Success
 * @return  VL53L0_ERROR_INVALID_PARAMS        This error is returned if
 * MeasurementTimingBudgetMicroSeconds is out of range
 * @return  "Other error code"                 See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_SetMeasurementTimingBudgetMicroSeconds(
			VL53L0_DEV Dev,
			uint32_t MeasurementTimingBudgetMicroSeconds);

/**
 * @brief Get Ranging Timing Budget in microseconds
 *
 * @par Function Description
 * Returns the programmed the maximum time allowed by the user to the device to
 * run a full ranging sequence
 * for the current mode (ranging, histogram, ASL ...)
 *
 * @note This function Access to the device
 *
 * @param   Dev                                    Device Handle
 * @param   pMeasurementTimingBudgetMicroSeconds   Max measurement time in
 * microseconds.
 *                                                 Valid values are:
 *                                                 >= 17000 microseconds when
 * wraparound is enabled
 *                                                 >= 12000 microseconds when
 * wraparound is disabled
 * @return  VL53L0_ERROR_NONE                      Success
 * @return  "Other error code"                     See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_GetMeasurementTimingBudgetMicroSeconds(
			VL53L0_DEV Dev,
			uint32_t *pMeasurementTimingBudgetMicroSeconds);

/**
 * Program continuous mode Inter-Measurement period in milliseconds
 *
 * @par Function Description
 * When trying to set too short time return  INVALID_PARAMS minimal value
 *
 * @note This function Access to the device
 *
 * @param   Dev                                  Device Handle
 * @param   InterMeasurementPeriodMilliSeconds   Requires Inter-Measurement
 * Period in milliseconds.
 * @return  VL53L0_ERROR_NONE                    Success
 * @return  "Other error code"                   See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_SetInterMeasurementPeriodMilliSeconds(
			VL53L0_DEV Dev,
			uint32_t InterMeasurementPeriodMilliSeconds);

/**
 * Get continuous mode Inter-Measurement period in milliseconds
 *
 * @par Function Description
 * When trying to set too short time return  INVALID_PARAMS minimal value
 *
 * @note This function Access to the device
 *
 * @param   Dev                                  Device Handle
 * @param   pInterMeasurementPeriodMilliSeconds  Pointer to programmed
 * Inter-Measurement Period in milliseconds.
 * @return  VL53L0_ERROR_NONE                    Success
 * @return  "Other error code"                   See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_GetInterMeasurementPeriodMilliSeconds(
			VL53L0_DEV Dev,
			uint32_t *pInterMeasurementPeriodMilliSeconds);

/**
 * @brief Enable/Disable Cross talk compensation feature
 *
 * @note This function Access to the device
 *
 * @param   Dev                       Device Handle
 * @param   XTalkCompensationEnable   Cross talk compensation to be set
 * 0=disabled else = enabled
 * @return  VL53L0_ERROR_NONE         Success
 * @return  "Other error code"        See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_SetXTalkCompensationEnable(
			VL53L0_DEV Dev, uint8_t XTalkCompensationEnable);

/**
 * @brief Get Cross talk compensation rate
 *
 * @note This function Access to the device
 *
 * @param   Dev                        Device Handle
 * @param   pXTalkCompensationEnable   Pointer to the Cross talk compensation
 * state 0=disabled or 1 = enabled
 * @return  VL53L0_ERROR_NONE          Success
 * @return  "Other error code"         See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_GetXTalkCompensationEnable(
			VL53L0_DEV Dev, uint8_t *pXTalkCompensationEnable);

/**
 * @brief Set Cross talk compensation rate
 *
 * @par Function Description
 * Set Cross talk compensation rate.
 *
 * @note This function Access to the device
 *
 * @param   Dev                            Device Handle
 * @param   XTalkCompensationRateMegaCps   Compensation rate in Mega counts per
 * second (16.16 fix point) see datasheet for details
 * @return  VL53L0_ERROR_NONE              Success
 * @return  "Other error code"             See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_SetXTalkCompensationRateMegaCps(
			VL53L0_DEV Dev,
			FixPoint1616_t XTalkCompensationRateMegaCps);

/**
 * @brief Get Cross talk compensation rate
 *
 * @par Function Description
 * Get Cross talk compensation rate.
 *
 * @note This function Access to the device
 *
 * @param   Dev                            Device Handle
 * @param   pXTalkCompensationRateMegaCps  Pointer to Compensation rate in Mega
 * counts per second (16.16 fix point) see datasheet for details
 * @return  VL53L0_ERROR_NONE              Success
 * @return  "Other error code"             See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_GetXTalkCompensationRateMegaCps(
			VL53L0_DEV Dev,
			FixPoint1616_t *pXTalkCompensationRateMegaCps);


/**
 * @brief  Get the number of the check limit managed by a given Device
 *
 * @par Function Description
 * This function give the number of the check limit managed by the Device
 *
 * @note This function doesn't Access to the device
 *
 * @param   Dev                           Device Handle
 * @param   pNumberOfLimitCheck           Pointer to the number of check limit.
 * @return  VL53L0_ERROR_NONE             Success
 * @return  "Other error code"            See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_GetNumberOfLimitCheck(
			uint16_t *pNumberOfLimitCheck);

/**
 * @brief  Return a description string for a given limit check number
 *
 * @par Function Description
 * This function returns a description string for a given limit check number.
 * The limit check is identified with the LimitCheckId.
 *
 * @note This function doesn't Access to the device
 *
 * @param   Dev                           Device Handle
 * @param   LimitCheckId                  Limit Check ID  (0<= LimitCheckId <
 * VL53L0_GetNumberOfLimitCheck() ).
 * @param   pLimitCheckString             Pointer to the description string of
 * the given check limit.
 * @return  VL53L0_ERROR_NONE             Success
 * @return  VL53L0_ERROR_INVALID_PARAMS   This error is returned when
 * LimitCheckId value is out of range.
 * @return  "Other error code"            See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_GetLimitCheckInfo(VL53L0_DEV Dev,
			uint16_t LimitCheckId, char *pLimitCheckString);


/**
 * @brief  Enable/Disable a specific limit check
 *
 * @par Function Description
 * This function Enable/Disable a specific limit check.
 * The limit check is identified with the LimitCheckId.
 *
 * @note This function doesn't Access to the device
 *
 * @param   Dev                           Device Handle
 * @param   LimitCheckId                  Limit Check ID  (0<= LimitCheckId <
 * VL53L0_GetNumberOfLimitCheck() ).
 * @param   LimitCheckEnable              if 1 the check limit corresponding to
 * LimitCheckId is Enabled
 *                                        if 0 the check limit corresponding to
 * LimitCheckId is disabled
 * @return  VL53L0_ERROR_NONE             Success
 * @return  VL53L0_ERROR_INVALID_PARAMS   This error is returned when
 * LimitCheckId value is out of range.
 * @return  "Other error code"            See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_SetLimitCheckEnable(VL53L0_DEV Dev,
			uint16_t LimitCheckId, uint8_t LimitCheckEnable);


/**
 * @brief  Get specific limit check enable state
 *
 * @par Function Description
 * This function get the enable state of a specific limit check.
 * The limit check is identified with the LimitCheckId.
 *
 * @note This function Access to the device
 *
 * @param   Dev                           Device Handle
 * @param   LimitCheckId                  Limit Check ID  (0<= LimitCheckId <
 * VL53L0_GetNumberOfLimitCheck() ).
 * @param   pLimitCheckEnable             Pointer to the check limit enable
 * value.
 *                                        if 1 the check limit corresponding to
 * LimitCheckId is Enabled
 *                                        if 0 the check limit corresponding to
 * LimitCheckId is disabled
 * @return  VL53L0_ERROR_NONE             Success
 * @return  VL53L0_ERROR_INVALID_PARAMS   This error is returned when
 * LimitCheckId value is out of range.
 * @return  "Other error code"            See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_GetLimitCheckEnable(VL53L0_DEV Dev,
			uint16_t LimitCheckId, uint8_t *pLimitCheckEnable);

/**
 * @brief  Set a specific limit check value
 *
 * @par Function Description
 * This function set a specific limit check value.
 * The limit check is identified with the LimitCheckId.
 *
 * @note This function Access to the device
 *
 * @param   Dev                           Device Handle
 * @param   LimitCheckId                  Limit Check ID  (0<= LimitCheckId <
 * VL53L0_GetNumberOfLimitCheck() ).
 * @param   LimitCheckValue               Limit check Value for a given
 * LimitCheckId
 * @return  VL53L0_ERROR_NONE             Success
 * @return  VL53L0_ERROR_INVALID_PARAMS   This error is returned when either
 * LimitCheckId or LimitCheckValue value is out of range.
 * @return  "Other error code"            See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_SetLimitCheckValue(VL53L0_DEV Dev,
			uint16_t LimitCheckId, FixPoint1616_t LimitCheckValue);

/**
 * @brief  Get a specific limit check value
 *
 * @par Function Description
 * This function get a specific limit check value from device then it updates
 * internal values and check enables.
 * The limit check is identified with the LimitCheckId.
 *
 * @note This function Access to the device
 *
 * @param   Dev                           Device Handle
 * @param   LimitCheckId                  Limit Check ID  (0<= LimitCheckId <
 * VL53L0_GetNumberOfLimitCheck() ).
 * @param   pLimitCheckValue              Pointer to Limit check Value for a
 * given LimitCheckId.
 * @return  VL53L0_ERROR_NONE             Success
 * @return  VL53L0_ERROR_INVALID_PARAMS   This error is returned when
 * LimitCheckId value is out of range.
 * @return  "Other error code"            See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_GetLimitCheckValue(VL53L0_DEV Dev,
			uint16_t LimitCheckId,
			FixPoint1616_t *pLimitCheckValue);


/**
 * @brief  Enable/disable Snr check for a given Device
 *
 * @par Function Description
 * This function set the Snr check Enable for a given position and for a given
 * device
 *
 * @note This function Access to the device
 *
 * @param   Dev                   Device Handle
 * @param   Position              Indicate the position of the check in the
 * sequence, it could be for example EARLY or FINAL.
 * @param   SnrLimitCheckEnable   if 1 = SnrLimit Check is Enabled ; 0 =
 * SnrLimit Check is disabled
 * @return  VL53L0_ERROR_NONE               Success
 * @return  VL53L0_ERROR_INVALID_PARAMS     This error is returned when
 * Position value is out of range.
 * @return  "Other error code"    See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_SetSnrLimitCheckEnable(VL53L0_DEV Dev,
			VL53L0_CheckPosition Position,
			uint8_t SnrLimitCheckEnable);


/**
 * @brief  Get Snr Limit check enable value for a given position and for a
 * given Device
 *
 * @par Function Description
 * This function get the Snr Limit check Enable for a given position and for a
 * given device
 *
 * @note This function Access to the device
 *
 * @param   Dev                   Device Handle
 * @param   Position              Indicate the position of the check in the
 * sequence, it could be for example EARLY or FINAL.
 * @param   pSnrLimitCheckEnable  Pointer to programmed SnrLimit Check Enable
 * value.
 * @return  VL53L0_ERROR_NONE               Success
 * @return  VL53L0_ERROR_INVALID_PARAMS     This error is returned when
 * Position value is out of range.
 * @return  "Other error code"    See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_GetSnrLimitCheckEnable(VL53L0_DEV Dev,
			VL53L0_CheckPosition Position,
			uint8_t *pSnrLimitCheckEnable);

/**
 * @brief Set SNR limit value for a given position and for a given Device
 *
 * @par Function Description
 * Set SNR limit value for a given position and for a given device.
 *
 * @note This function Access to the device
 *
 * @param   Dev                  Device Handle.
 * @param   Position             Indicate the position of the check in the
 * sequence, it could be for example EARLY or FINAL.
 * @param   SnrLimitValue        SNR limit value to validate the measurement
 * @return  VL53L0_ERROR_NONE               Success
 * @return  VL53L0_ERROR_INVALID_PARAMS     This error is returned when
 * Position value is out of range.
 * @return  "Other error code"   See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_SetSnrLimitValue(VL53L0_DEV Dev,
			VL53L0_CheckPosition Position,
			FixPoint1616_t SnrLimitValue);


/**
 * @brief Get SNR limit value
 *
 * @par Function Description
 * Get SNR limit value for a given position and for a given device.
 *
 * @note This function Access to the device
 *
 * @param   Dev                  Device Handle.
 * @param   Position             Indicate the position of the check in the
 * sequence, it could be for example EARLY or FINAL.
 * @param   pSnrLimitValue       Pointer to current SNR limit value to validate
 * the measurement
 * @return  VL53L0_ERROR_NONE               Success
 * @return  VL53L0_ERROR_INVALID_PARAMS     This error is returned when
 * Position value is out of range.
 * @return  "Other error code"   See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_GetSnrLimitValue(VL53L0_DEV Dev,
			VL53L0_CheckPosition Position,
			FixPoint1616_t *pSnrLimitValue);

/**
 * @brief  Enable/disable Signal check for a given Device
 *
 * @par Function Description
 * This function set the Signal check Enable for a given position and for a
 * given device
 *
 * @note This function doesn't Access to the device
 *
 * @param   Dev                     Device Handle
 * @param   Position                Indicate the position of the check in the
 * sequence, it could be for example EARLY or FINAL.
 * @param   SignalLimitCheckEnable   if 1 = SignalLimit Check is Enabled ; 0 =
 * SignalLimit Check is disabled
 * @return  VL53L0_ERROR_NONE               Success
 * @return  VL53L0_ERROR_INVALID_PARAMS     This error is returned when
 * Position value is out of range.
 * @return  "Other error code"      See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_SetSignalLimitCheckEnable(VL53L0_DEV Dev,
			VL53L0_CheckPosition Position,
			uint8_t SignalLimitCheckEnable);


/**
 * @brief  Get Signal Limit check enable value for a given position and for a
 * given Device
 *
 * @par Function Description
 * This function get the Signal Limit check Enable for a given position and for
 * a given device
 *
 * @note This function doesn't Access to the device
 *
 * @param   Dev                     Device Handle
 * @param   Position                Indicate the position of the check in the
 * sequence, it could be for example EARLY or FINAL.
 * @param   pSignalLimitCheckEnable  Pointer to programmed SignalLimit Check
 * Enable value.
 * @return  VL53L0_ERROR_NONE               Success
 * @return  VL53L0_ERROR_INVALID_PARAMS     This error is returned when
 * Position value is out of range.
 * @return  "Other error code"      See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_GetSignalLimitCheckEnable(VL53L0_DEV Dev,
			VL53L0_CheckPosition Position,
			uint8_t *pSignalLimitCheckEnable);

/**
 * @brief Set Signal limit value for a given position and for a given Device
 *
 * @par Function Description
 * Set Signal limit value for a given position and for a given device.
 *
 * @note This function doesn't Access to the device
 *
 * @param   Dev                  Device Handle.
 * @param   Position             Indicate the position of the check in the
 * sequence, it could be for example EARLY or FINAL.
 * @param   SignalLimitValue     Signal limit value to validate the measurement
 * @return  VL53L0_ERROR_NONE               Success
 * @return  VL53L0_ERROR_INVALID_PARAMS     This error is returned when
 * Position value is out of range.
 * @return  "Other error code"   See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_SetSignalLimitValue(VL53L0_DEV Dev,
			VL53L0_CheckPosition Position, FixPoint1616_t
SignalLimitValue);


/**
 * @brief Get Signal limit value
 *
 * @par Function Description
 * Get Signal limit value for a given position and for a given device.
 *
 * @note This function doesn't Access to the device
 *
 * @param   Dev                  Device Handle.
 * @param   Position             Indicate the position of the check in the
 * sequence, it could be for example EARLY or FINAL.
 * @param   pSignalLimitValue    Pointer to current Signal limit value to
 * validate the measurement
 * @return  VL53L0_ERROR_NONE               Success
 * @return  VL53L0_ERROR_INVALID_PARAMS     This error is returned when
 * Position value is out of range.
 * @return  "Other error code"   See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_GetSignalLimitValue(VL53L0_DEV Dev,
			VL53L0_CheckPosition Position, FixPoint1616_t
*pSignalLimitValue);


/**
 * @brief  Enable/disable Sigma check for a given Device
 *
 * @par Function Description
 * This function set the Sigma check Enable for a given position and for a
 * given device
 *
 * @note This function doesn't Access to the device
 *
 * @param   Dev                     Device Handle
 * @param   Position                Indicate the position of the check in the
 * sequence, it could be for example EARLY or FINAL.
 * @param   SigmaLimitCheckEnable   if 1 = SigmaLimit Check is Enabled ; 0 =
 * SigmaLimit Check is disabled
 * @return  VL53L0_ERROR_NONE               Success
 * @return  VL53L0_ERROR_INVALID_PARAMS     This error is returned when
 * Position value is out of range.
 * @return  "Other error code"      See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_SetSigmaLimitCheckEnable(VL53L0_DEV Dev,
			VL53L0_CheckPosition Position, uint8_t
SigmaLimitCheckEnable);


/**
 * @brief  Get Sigma Limit check enable value for a given position and for a
 * given Device
 *
 * @par Function Description
 * This function get the Sigma Limit check Enable for a given position and for
 * a given device
 *
 * @note This function doesn't Access to the device
 *
 * @param   Dev                     Device Handle
 * @param   Position                Indicate the position of the check in the
 * sequence, it could be for example EARLY or FINAL.
 * @param   pSigmaLimitCheckEnable  Pointer to programmed SigmaLimit Check
 * Enable value.
 * @return  VL53L0_ERROR_NONE               Success
 * @return  VL53L0_ERROR_INVALID_PARAMS     This error is returned when
 * Position value is out of range.
 * @return  "Other error code"      See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_GetSigmaLimitCheckEnable(VL53L0_DEV Dev,
			VL53L0_CheckPosition Position, uint8_t
*pSigmaLimitCheckEnable);

/**
 * @brief Set Sigma limit value for a given position and for a given Device
 *
 * @par Function Description
 * Set Sigma limit value for a given position and for a given device.
 *
 * @note This function doesn't Access to the device
 *
 * @param   Dev                  Device Handle.
 * @param   Position             Indicate the position of the check in the
 * sequence, it could be for example EARLY or FINAL.
 * @param   SigmaLimitValue      Sigma limit value to validate the measurement
 * @return  VL53L0_ERROR_NONE               Success
 * @return  VL53L0_ERROR_INVALID_PARAMS     This error is returned when
 * Position value is out of range.
 * @return  "Other error code"   See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_SetSigmaLimitValue(VL53L0_DEV Dev,
			VL53L0_CheckPosition Position, FixPoint1616_t
SigmaLimitValue);


/**
 * @brief Get Sigma limit value
 *
 * @par Function Description
 * Get Sigma limit value for a given position and for a given device.
 *
 * @note This function doesn't Access to the device
 *
 * @param   Dev                  Device Handle.
 * @param   Position             Indicate the position of the check in the
 * sequence, it could be for example EARLY or FINAL.
 * @param   pSigmaLimitValue     Pointer to current Sigma limit value to
 * validate the measurement
 * @return  VL53L0_ERROR_NONE               Success
 * @return  VL53L0_ERROR_INVALID_PARAMS     This error is returned when
 * Position value is out of range.
 * @return  "Other error code"   See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_GetSigmaLimitValue(VL53L0_DEV Dev,
			VL53L0_CheckPosition Position, FixPoint1616_t
*pSigmaLimitValue);

/**
 * @brief  Set Enable/disable Rate check Limit for a given position and for a
 * given device
 *
 * @par Function Description
 * This function set the Rate Limit check Enable for a given position and for a
 * given device
 *
 * @note This function Access to the device
 *
 * @param   Dev                   Device Handle
 * @param   Position              Indicate the position in the sequence it
 * could be for example EARLY or FINAL.
 * @param   RateLimitCheckEnable  if 1 = Rate Limit Check is Enabled ; 0 = Rate
 * Limit Check is disabled
 * @return  VL53L0_ERROR_NONE               Success
 * @return  VL53L0_ERROR_INVALID_PARAMS     This error is returned when
 * Position value is out of range.
 * @return  "Other error code"    See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_SetRateLimitCheckEnable(VL53L0_DEV Dev,
			VL53L0_CheckPosition Position, uint8_t
RateLimitCheckEnable);


/**
 * @brief  Get Rate Limit check Enable for a given position and for a given
 * device
 *
 * @par Function Description
 * This function get the Rate Limit check Enable for a given position and for a
 * given device
 *
 * @note This function Access to the device
 *
 * @param   Dev                     Device Handle
 * @param   Position                Indicate the position in the sequence it
 * could be for example EARLY or FINAL.
 * @param   pRateLimitCheckEnable   Pointer to programmed Rate Limit Check
 * Enable value.
 * @return  VL53L0_ERROR_NONE               Success
 * @return  VL53L0_ERROR_INVALID_PARAMS     This error is returned when
 * Position value is out of range.
 * @return  "Other error code"      See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_GetRateLimitCheckEnable(VL53L0_DEV Dev,
			VL53L0_CheckPosition Position, uint8_t
*pRateLimitCheckEnable);

/**
 * @brief Set Rate limit value for a given position and for a given device
 *
 * @par Function Description
 * Set Rate limit value for a given position and for a given device.
 *
 * @note This function Access to the device
 *
 * @param   Dev                  Device Handle.
 * @param   Position             Indicate the position in the sequence it could
 * be for example EARLY or FINAL.
 * @param   RateLimitValue       Rate limit value
 * @return  VL53L0_ERROR_NONE               Success
 * @return  VL53L0_ERROR_INVALID_PARAMS     This error is returned when
 * Position value is out of range.
 * @return  "Other error code"   See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_SetRateLimitValue(VL53L0_DEV Dev,
			VL53L0_CheckPosition Position,
			FixPoint1616_t RateLimitValue);

/**
 * @brief Get Rate limit value for a given position and for a given device
 *
 * @par Function Description
 * Get Rate limit value for a given position and for a given device.
 *
 * @note This function Access to the device
 *
 * @param   Dev                  Device Handle.
 * @param   Position             Indicate the position in the sequence it could
 * be for example EARLY or FINAL.
 * @param   pRateLimitValue      Pointer to current Rate limit value
 * @return  VL53L0_ERROR_NONE               Success
 * @return  VL53L0_ERROR_INVALID_PARAMS     This error is returned when
 * Position value is out of range.
 * @return  "Other error code"   See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_GetRateLimitValue(VL53L0_DEV Dev,
			VL53L0_CheckPosition Position,
			FixPoint1616_t *pRateLimitValue);

/**
 * @brief  Enable (or disable) Wrap around Check
 *
 * @note This function Access to the device
 *
 * @param   Dev                    Device Handle
 * @param   WrapAroundCheckEnable  Wrap around Check to be set 0=disabled,
 * other = enabled
 * @return  VL53L0_ERROR_NONE      Success
 * @return  "Other error code"     See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_SetWrapAroundCheckEnable(VL53L0_DEV Dev,
			uint8_t WrapAroundCheckEnable);

/**
 * @brief  Get setup of Wrap around Check
 *
 * @par Function Description
 * This function get the wrapAround check enable parameters
 *
 * @note This function Access to the device
 *
 * @param   Dev                     Device Handle
 * @param   pWrapAroundCheckEnable  Pointer to the Wrap around Check state
 * 0=disabled or 1 = enabled
 * @return  VL53L0_ERROR_NONE       Success
 * @return  "Other error code"      See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_GetWrapAroundCheckEnable(VL53L0_DEV Dev,
			uint8_t *pWrapAroundCheckEnable);

/** @} VL53L0_parameters_group */


/** @defgroup VL53L0_measurement_group VL53L0 Measurement Functions
 *  @brief    Functions used for the measurements
 *  @{
 */

/**
 * @brief Single shot measurement.
 *
 * @par Function Description
 * Perform simple measurement sequence (Start measure, Wait measure to end, and
 * returns when measurement is done).
 * Once function returns, user can get valid data by calling
 * VL53L0_GetRangingMeasurement or VL53L0_GetHistogramMeasurement depending on
 * defined measurement mode
 * User should Clear the interrupt in case this are enabled by using the
 * function VL53L0_ClearInterruptMask().
 *
 * @warning This function is a blocking function
 *
 * @note This function Access to the device
 *
 * @param   Dev                  Device Handle
 * @return  VL53L0_ERROR_NONE    Success
 * @return  "Other error code"   See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_PerformSingleMeasurement(VL53L0_DEV Dev);

/**
 * @brief Perform Reference Calibration
 *
 * @details Perform a reference calibration of the Device.
 * This function should be run from time to time before doing a ranging
 * measurement.
 * This function will launch a special ranging measurement, so if interrupt are
 * enable an interrupt will be done.
 * This function will clear the interrupt generated automatically.
 *
 * @warning This function is a blocking function
 *
 * @note This function Access to the device
 *
 * @param   Dev                  Device Handle
 * @return  VL53L0_ERROR_NONE    Success
 * @return  "Other error code"   See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_PerformRefCalibration(VL53L0_DEV Dev);

/**
 * @brief Perform XTalk Calibration
 *
 * @details Perform a XTalk calibration of the Device.
 * This function will launch a ranging measurement, if interrupts are enabled
 * an interrupt will be done.
 * This function will clear the interrupt generated automatically.
 * This function will program a new value for the XTalk compensation and it
 * will enable the cross talk before exit.
 *
 * @warning This function is a blocking function
 *
 * @note This function Access to the device
 *
 * @note This function change the device mode to
 * VL53L0_DEVICEMODE_SINGLE_RANGING
 *
 * @param   Dev                  Device Handle
 * @param   XTalkCalDistance               XTalkCalDistance value used for the
 * XTalk computation.
 * @param   pXTalkCompensationRateMegaCps  Pointer to new XTalkCompensation
 * value.
 * @return  VL53L0_ERROR_NONE    Success
 * @return  "Other error code"   See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_PerformXTalkCalibration(VL53L0_DEV Dev,
			FixPoint1616_t XTalkCalDistance,
			FixPoint1616_t *pXTalkCompensationRateMegaCps);

/**
 * @brief Start device measurement
 *
 * @details Started measurement will depend on device parameters set through @a
 * VL53L0_SetParameters()
 * This is a non-blocking function
 * This function will change the VL53L0_State from VL53L0_STATE_IDLE to
 * VL53L0_STATE_RUNNING.
 *
 * @note This function Access to the device
 *

 * @param   Dev                  Device Handle
 * @return  VL53L0_ERROR_NONE                  Success
 * @return  VL53L0_ERROR_MODE_NOT_SUPPORTED    This error occurs when
 * DeviceMode programmed with @a VL53L0_SetDeviceMode is not in the supported
 * list:
 *                                             Supported mode are:
 * VL53L0_DEVICEMODE_SINGLE_RANGING, VL53L0_DEVICEMODE_CONTINUOUS_RANGING,
 * VL53L0_DEVICEMODE_CONTINUOUS_TIMED_RANGING
 * @return  VL53L0_ERROR_TIME_OUT    Time out on start measurement
 * @return  "Other error code"   See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_StartMeasurement(VL53L0_DEV Dev);

/**
 * @brief Stop device measurement
 *
 * @details Will set the device in standby mode at end of current measurement \n
 *          Not necessary in single mode as device shall return automatically
 * in standby mode at end of measurement.
 *          This function will change the VL53L0_State from
 * VL53L0_STATE_RUNNING to VL53L0_STATE_IDLE.
 *
 * @note This function Access to the device
 *
 * @param   Dev                  Device Handle
 * @return  VL53L0_ERROR_NONE    Success
 * @return  "Other error code"   See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_StopMeasurement(VL53L0_DEV Dev);

/**
 * @brief Return Measurement Data Ready
 *
 * @par Function Description
 * This function indicate that a measurement data is ready.
 * This function check if interrupt mode is used then check is done accordingly.
 * If perform function clear the interrupt, this function will not work, like
 * in case of @a VL53L0_PerformSingleRangingMeasurement().
 * The previous function is blocking function, VL53L0_GetMeasurementDataReady
 * is used for non-blocking capture.
 *
 * @note This function Access to the device
 *
 * @param   Dev                    Device Handle
 * @param   pMeasurementDataReady  Pointer to Measurement Data Ready. 0=data
 * not ready, 1 = data ready
 * @return  VL53L0_ERROR_NONE      Success
 * @return  "Other error code"     See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_GetMeasurementDataReady(VL53L0_DEV Dev,
			uint8_t *pMeasurementDataReady);

/**
 * @brief Wait for device ready for a new measurement command. Blocking
 * function.
 *
 * @note This function is not Implemented
 *
 * @param   Dev      Device Handle
 * @param   MaxLoop    Max Number of polling loop (timeout).
 * @return  VL53L0_ERROR_NOT_IMPLEMENTED   Not implemented
 */
VL53L0_API VL53L0_Error VL53L0_WaitDeviceReadyForNewMeasurement(VL53L0_DEV Dev,
			uint32_t MaxLoop);


/**
 * @brief Retrieve the measurements from device for a given setup
 *
 * @par Function Description
 * Get data from last successful Ranging measurement
 * @warning USER should take care about  @a VL53L0_GetNumberOfROIZones() before
 * get data.
 * PAL will fill a NumberOfROIZones times the corresponding data structure used
 * in the measurement function.
 *
 * @note This function Access to the device
 *
 * @param   Dev                      Device Handle
 * @param   pRangingMeasurementData  Pointer to the data structure to fill up.
 * @return  VL53L0_ERROR_NONE        Success
 * @return  "Other error code"       See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_GetRangingMeasurementData(VL53L0_DEV Dev,
		VL53L0_RangingMeasurementData_t *pRangingMeasurementData);

/**
 * @brief Retrieve the measurements from device for a given setup
 *
 * @par Function Description
 * Get data from last successful Histogram measurement
 * @warning USER should take care about  @a VL53L0_GetNumberOfROIZones() before
 * get data.
 * PAL will fill a NumberOfROIZones times the corresponding data structure used
 * in the measurement function.
 * @note This function is not Implemented
 * @param	Dev							Device Handle
 * @param	pHistogramMeasurementData	Pointer to the data structure to fill
 * up.
 * @return  VL53L0_ERROR_NOT_IMPLEMENTED   Not implemented
 */
VL53L0_API VL53L0_Error VL53L0_GetHistogramMeasurementData(VL53L0_DEV Dev,
		VL53L0_HistogramMeasurementData_t *pHistogramMeasurementData);

/**
 * @brief Performs a single ranging measurement and retrieve the ranging
 * measurement data
 *
 * @par Function Description
 * This function will change the device mode to
 * VL53L0_DEVICEMODE_SINGLE_RANGING with @a VL53L0_SetDeviceMode(),
 * It performs measurement with @a VL53L0_PerformSingleMeasurement()
 * It get data from last successful Ranging measurement with @a
 * VL53L0_GetRangingMeasurementData.
 * Finally it clear the interrupt with @a VL53L0_ClearInterruptMask().
 * @note This function Access to the device
 *
 * @note This function change the device mode to
 * VL53L0_DEVICEMODE_SINGLE_RANGING
 *
 * @param	Dev							Device Handle
 * @param   pRangingMeasurementData		Pointer to the data structure to fill up.
 * @return  VL53L0_ERROR_NONE			Success
 * @return  "Other error code"			See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_PerformSingleRangingMeasurement(VL53L0_DEV Dev,
		VL53L0_RangingMeasurementData_t *pRangingMeasurementData);

/**
 * @brief Performs a single histogram measurement and retrieve the histogram
 * measurement data
 *        Is equivalent to VL53L0_PerformSingleMeasurement +
 * VL53L0_GetHistogramMeasurementData
 *
 * @par Function Description
 * Get data from last successful Ranging measurement.
 * This function will clear the interrupt in case of these are enabled.
 *
 * @note This function is not Implemented
 *
 * @param	Dev							Device Handle
 * @param   pHistogramMeasurementData	Pointer to the data structure to fill
 * up.
 * @return  VL53L0_ERROR_NOT_IMPLEMENTED   Not implemented
 */
VL53L0_API VL53L0_Error VL53L0_PerformSingleHistogramMeasurement(
		VL53L0_DEV Dev,
		VL53L0_HistogramMeasurementData_t *pHistogramMeasurementData);



/**
 * @brief Set the number of ROI Zones to be used for a specific Device
 *
 * @par Function Description
 * Set the number of ROI Zones to be used for a specific Device.
 * The programmed value should be less than the max number of ROI Zones given
 * with  @a VL53L0_GetMaxNumberOfROIZones().
 * This version of API manage only one zone.
 *
 * @param	Dev							Device Handle
 * @param   NumberOfROIZones            Number of ROI Zones to be used for a
 * specific Device.
 * @return  VL53L0_ERROR_NONE             Success
 * @return  VL53L0_ERROR_INVALID_PARAMS   This error is returned if
 * NumberOfROIZones != 1
 */
VL53L0_API VL53L0_Error VL53L0_SetNumberOfROIZones(VL53L0_DEV Dev,
			uint8_t NumberOfROIZones);

/**
 * @brief Get the number of ROI Zones managed by the Device
 *
 * @par Function Description
 * Get number of ROI Zones managed by the Device
 * USER should take care about  @a VL53L0_GetNumberOfROIZones() before get data
 * after a perform measurement.
 * PAL will fill a NumberOfROIZones times the corresponding data structure used
 * in the measurement function.
 *
 * @note This function doesn't Access to the device
 *
 * @param	Dev							Device Handle
 * @param   pNumberOfROIZones           Pointer to the Number of ROI Zones
 * value.
 * @return  VL53L0_ERROR_NONE           Success
 */
VL53L0_API VL53L0_Error VL53L0_GetNumberOfROIZones(VL53L0_DEV Dev,
			uint8_t *pNumberOfROIZones);

/**
 * @brief Get the Maximum number of ROI Zones managed by the Device
 *
 * @par Function Description
 * Get Maximum number of ROI Zones managed by the Device.
 *
 * @note This function doesn't Access to the device
 *
 * @param	Dev							Device Handle
 * @param   pMaxNumberOfROIZones        Pointer to the Maximum Number of ROI
 * Zones value.
 * @return  VL53L0_ERROR_NONE           Success
 */
VL53L0_API VL53L0_Error VL53L0_GetMaxNumberOfROIZones(VL53L0_DEV Dev,
			uint8_t *pMaxNumberOfROIZones);


/** @} VL53L0_measurement_group */


/** @defgroup VL53L0_interrupt_group VL53L0 Interrupt Functions
 *  @brief    Functions used for interrupt managements
 *  @{
 */

/**
 * @brief Set the configuration of GPIO pin for a given device
 *
 * @note This function Access to the device
 *
 * @param   Dev                   Device Handle
 * @param   Pin                   ID of the GPIO Pin
 * @param   Functionality         Select Pin functionality. Refer to
 * ::VL53L0_GpioFunctionality
 * @param   DeviceMode            Device Mode associated to the Gpio.
 * @param   Polarity              Set interrupt polarity. Active high or active
 * low see ::VL53L0_InterruptPolarity
 * @return  VL53L0_ERROR_NONE                                Success
 * @return  VL53L0_ERROR_GPIO_NOT_EXISTING                   Only Pin=0 is
 * accepted.
 * @return  VL53L0_ERROR_GPIO_FUNCTIONALITY_NOT_SUPPORTED    This error occurs
 * when Functionality programmed is not in the supported list:
 *                                                           Supported value
 * are:
 *
 * VL53L0_GPIOFUNCTIONALITY_OFF, VL53L0_GPIOFUNCTIONALITY_THRESHOLD_CROSSED_LOW,
 *
 * VL53L0_GPIOFUNCTIONALITY_THRESHOLD_CROSSED_HIGH,
 * VL53L0_GPIOFUNCTIONALITY_THRESHOLD_CROSSED_OUT,
 *
 * VL53L0_GPIOFUNCTIONALITY_NEW_MEASURE_READY
 * @return  "Other error code"    See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_SetGpioConfig(VL53L0_DEV Dev, uint8_t Pin,
				VL53L0_DeviceModes DeviceMode,
				VL53L0_GpioFunctionality Functionality,
				VL53L0_InterruptPolarity Polarity);


/**
 * @brief Get current configuration for GPIO pin for a given device
 *
 * @note This function Access to the device
 *
 * @param   Dev                   Device Handle
 * @param   Pin                   ID of the GPIO Pin
 * @param   pDeviceMode           Pointer to Device Mode associated to the Gpio.
 * @param   pFunctionality        Pointer to Pin functionality.
 * Refer to ::VL53L0_GpioFunctionality
 * @param   pPolarity             Pointer to interrupt polarity. Active high or
 * active low see ::VL53L0_InterruptPolarity
 * @return	VL53L0_ERROR_NONE		Success
 * @return  VL53L0_ERROR_GPIO_NOT_EXISTING  Only Pin=0 is accepted.
 * @return  VL53L0_ERROR_GPIO_FUNCTIONALITY_NOT_SUPPORTED    This error occurs
 * when Functionality programmed is not in the supported list:
 * Supported value are:
 * VL53L0_GPIOFUNCTIONALITY_OFF, VL53L0_GPIOFUNCTIONALITY_THRESHOLD_CROSSED_LOW,
 * VL53L0_GPIOFUNCTIONALITY_THRESHOLD_CROSSED_HIGH,
 * VL53L0_GPIOFUNCTIONALITY_THRESHOLD_CROSSED_OUT,
 * VL53L0_GPIOFUNCTIONALITY_NEW_MEASURE_READY
 * @return  "Other error code"    See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_GetGpioConfig(VL53L0_DEV Dev, uint8_t Pin,
			VL53L0_DeviceModes * pDeviceMode,
			VL53L0_GpioFunctionality * pFunctionality,
			VL53L0_InterruptPolarity * pPolarity);

/**
 * @brief Set low and high Interrupt thresholds for a given mode (ranging, ALS,
 * ...) for a given device
 *
 * @par Function Description
 * Set low and high Interrupt thresholds for a given mode (ranging, ALS, ...)
 * for a given device
 *
 * @note This function Access to the device
 *
 * @note DeviceMode is ignored for the current device
 *
 * @param   Dev                  Device Handle
 * @param   DeviceMode           Device Mode for which change thresholds
 * @param   ThresholdLow         Low threshold (mm, lux ..., depending on the
 * mode)
 * @param   ThresholdHigh        High threshold (mm, lux ..., depending on the
 * mode)
 * @return  VL53L0_ERROR_NONE    Success
 * @return  "Other error code"   See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_SetInterruptThresholds(VL53L0_DEV Dev,
			VL53L0_DeviceModes DeviceMode,
			FixPoint1616_t ThresholdLow,
			FixPoint1616_t ThresholdHigh);

/**
 * @brief  Get high and low Interrupt thresholds for a given mode (ranging,
 * ALS, ...) for a given device
 *
 * @par Function Description
 * Get high and low Interrupt thresholds for a given mode (ranging, ALS, ...)
 * for a given device
 *
 * @note This function Access to the device
 *
 * @note DeviceMode is ignored for the current device
 *
 * @param   Dev                 Device Handle
 * @param   DeviceMode          Device Mode from which read thresholds
 * @param   pThresholdLow       Low threshold (mm, lux ..., depending on the
 * mode)
 * @param   pThresholdHigh      High threshold (mm, lux ..., depending on the
 * mode)
 * @return  VL53L0_ERROR_NONE   Success
 * @return  "Other error code"  See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_GetInterruptThresholds(VL53L0_DEV Dev,
			VL53L0_DeviceModes DeviceMode,
			FixPoint1616_t *pThresholdLow,
			FixPoint1616_t *pThresholdHigh);

/**
 * @brief Clear given system interrupt condition
 *
 * @par Function Description
 * Clear given interrupt(s).
 *
 * @note This function Access to the device
 *
 * @param   Dev                  Device Handle
 * @param   InterruptMask        Mask of interrupts to clear
 * @return  VL53L0_ERROR_NONE    Success
 * @return  "Other error code"   See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_ClearInterruptMask(VL53L0_DEV Dev,
			uint32_t InterruptMask);

/**
 * @brief Return device interrupt status
 *
 * @par Function Description
 * Returns currently raised interrupts by the device.
 * User shall be able to activate/deactivate interrupts through
 * @a VL53L0_SetGpioConfig()
 *
 * @note This function Access to the device
 *
 * @param   Dev                    Device Handle
 * @param   pInterruptMaskStatus   Pointer to status variable to update
 * @return  VL53L0_ERROR_NONE      Success
 * @return  "Other error code"     See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_GetInterruptMaskStatus(VL53L0_DEV Dev,
			uint32_t *pInterruptMaskStatus);


/**
 * @brief Configure ranging interrupt reported to system
 *
 * @note This function is not Implemented
 *
 * @param	Dev					Device Handle
 * @param	InterruptMask		Mask of interrupt to Enable/disable
 * (0:interrupt disabled or 1: interrupt enabled)
 * @return  VL53L0_ERROR_NOT_IMPLEMENTED   Not implemented
 */
VL53L0_API VL53L0_Error VL53L0_EnableInterruptMask(VL53L0_DEV Dev,
			uint32_t InterruptMask);


/** @} VL53L0_interrupt_group */


/** @defgroup VL53L0_SPADfunctions_group VL53L0 SPAD Functions
 *  @brief    Functions used for SPAD managements
 *  @{
 */



/**
 * @brief  Set the SPAD Ambient Damper Threshold value
 *
 * @par Function Description
 * This function set the SPAD Ambient Damper Threshold value
 *
 * @note This function Access to the device
 *
 * @param   Dev                           Device Handle
 * @param   SpadAmbientDamperThreshold    SPAD Ambient Damper Threshold value
 * @return  VL53L0_ERROR_NONE             Success
 * @return  "Other error code"            See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_SetSpadAmbientDamperThreshold(VL53L0_DEV Dev,
					uint16_t SpadAmbientDamperThreshold);

/**
 * @brief  Get the current SPAD Ambient Damper Threshold value
 *
 * @par Function Description
 * This function get the SPAD Ambient Damper Threshold value
 *
 * @note This function Access to the device
 *
 * @param   Dev                           Device Handle
 * @param   pSpadAmbientDamperThreshold   Pointer to programmed SPAD Ambient
 * Damper Threshold value
 * @return  VL53L0_ERROR_NONE             Success
 * @return  "Other error code"            See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_GetSpadAmbientDamperThreshold(VL53L0_DEV Dev,
					uint16_t *pSpadAmbientDamperThreshold);


/**
 * @brief  Set the SPAD Ambient Damper Factor value
 *
 * @par Function Description
 * This function set the SPAD Ambient Damper Factor value
 *
 * @note This function Access to the device
 *
 * @param   Dev                           Device Handle
 * @param   SpadAmbientDamperFactor       SPAD Ambient Damper Factor value
 * @return  VL53L0_ERROR_NONE             Success
 * @return  "Other error code"            See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_SetSpadAmbientDamperFactor(VL53L0_DEV Dev,
					uint16_t SpadAmbientDamperFactor);

/**
 * @brief  Get the current SPAD Ambient Damper Factor value
 *
 * @par Function Description
 * This function get the SPAD Ambient Damper Factor value
 *
 * @note This function Access to the device
 *
 * @param   Dev                           Device Handle
 * @param   pSpadAmbientDamperFactor      Pointer to programmed SPAD Ambient
 * Damper Factor value
 * @return  VL53L0_ERROR_NONE             Success
 * @return  "Other error code"            See ::VL53L0_Error
 */
VL53L0_API VL53L0_Error VL53L0_GetSpadAmbientDamperFactor(VL53L0_DEV Dev,
					uint16_t *pSpadAmbientDamperFactor);


/** @} VL53L0_SPADfunctions_group */



#ifdef __cplusplus
}
#endif

#endif /* _VL53L0_API_H_ */

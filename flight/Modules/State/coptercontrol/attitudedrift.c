/**
 ******************************************************************************
 * @addtogroup OpenPilotModules OpenPilot Modules
 * @{
 * @addtogroup Attitude Copter Control Attitude Estimation
 * @brief A collection of mathematics that help reduce attitude drift. Pick and choose for best performance
 * @{
 *
 * @file       attitudedrift.c
 * @author     The OpenPilot Team, http://www.openpilot.org Copyright (C) 2010.
 * @brief      Module to handle all comms to the AHRS on a periodic basis.
 *
 * @see        The GNU Public License (GPL) Version 3
 *
 ******************************************************************************/
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

/**
 * UAVObjects are automatically generated by the UAVObjectGenerator from
 * the object definition XML file.
 *
 * Modules have no API, all communication to other modules is done through UAVObjects.
 * However modules may use the API exposed by shared libraries.
 * See the OpenPilot wiki for more details.
 * http://www.openpilot.org/OpenPilot_Application_Architecture
 *
 */

#include "pios.h"
#include "hwsettings.h"
#include <pios_board_info.h>
#include "attitudedrift.h"
#include "CoordinateConversions.h"

#include "gpsvelocity.h"
#if defined (PIOS_INCLUDE_MAGNETOMETER)	//THIS PIOS DEFINE DOES NOT CURRENTLY EXIST, BUT WE SHOULD ADD IT IN ORDER TO SUPPORT ALL MAGS, NOT JUST THE HMC5883
#include "magnetometer.h"
#endif
#if defined (PIOS_GPS_PROVIDES_AIRSPEED)	//THIS PIOS DEFINE DOES NOT CURRENTLY EXIST, BUT WE SHOULD ADD IT IN ORDER TO SUPPORT ALL MAGS, NOT JUST THE HMC5883
#include "airspeedactual.h"
#endif

//Include files for attitude estimators
#include "ccc.h"
#include "premerlani_dcm.h"
#include "premerlani_gps.h"

//Global variables
extern AttitudeSettingsData attitudeSettings;

bool firstpass_flag = true;

struct GlobalDcmDriftVariables {
	float GPSV_old[3];

	float accels_e_integrator[3];
	float omegaCorrI[3];

	bool gpsPresent_flag;
	volatile uint8_t gpsVelocityDataConsumption_flag;
	bool magNewData_flag;

	float accelsKp;
	float rollPitchKp;
	float rollPitchKi;
	float yawKp;
	float yawKi;
	float gyroCalibTau;

	//! Accumulator for the time step between GPS updates
	float delT_between_GPS;
};

#define GPS_UNCONSUMED       0x00
#define GPS_CONSUMED_BY_RPY  0x01
#define GPS_CONSUMED_BY_Y    0x02
#define GPS_CONSUMED         0xFF

struct GlobalDcmDriftVariables *drft;

// Private constants

// Private types

// Private variables
#define GRAV -9.805f

//#define DRIFT_TYPE CCC
enum DRIFT_CORRECTION_ALGOS {
	CCC,
	PREMERLANI
};

// Private functions
static void calibrate_gyros_high_speed(float gyro[3], float omegaCorrP[3],
				float normOmegaScalar, float delT,
				float *ggain);

static void GPSVelocityUpdatedCb(UAVObjEvent * objEv);
#if defined (PIOS_INCLUDE_MAGNETOMETER)
static void MagnetometerUpdatedCb(UAVObjEvent * objEv);
#endif

/**
 * Correct attitude drift. Choose from any of the following algorithms
 */
void updateAttitudeDrift(AccelsData * accelsData, GyrosData * gyrosData, const float delT, GlobalAttitudeVariables *glblAtt, InertialSensorSettingsData *inertialSensorSettings)
{
	float *gyros = &gyrosData->x;
	float *accels = &accelsData->x;
	float omegaCorrP[3];

	if (attitudeSettings.FilterChoice == ATTITUDESETTINGS_FILTERCHOICE_CCC) {
		CottonComplementaryCorrection(accels, gyros, delT, glblAtt, omegaCorrP);
	} else if (attitudeSettings.FilterChoice == ATTITUDESETTINGS_FILTERCHOICE_PREMERLANI || 
		attitudeSettings.FilterChoice == ATTITUDESETTINGS_FILTERCHOICE_PREMERLANI_GPS) {
		if (firstpass_flag) {
			uint8_t optionalModules[HWSETTINGS_OPTIONALMODULES_NUMELEM];
			HwSettingsOptionalModulesGet(optionalModules);

			//Allocate memory for DCM drift globals
			drft = (struct GlobalDcmDriftVariables *)
			    pvPortMalloc(sizeof (struct GlobalDcmDriftVariables));

			memset(drft->GPSV_old, 0, sizeof(drft->GPSV_old));
			memset(drft->omegaCorrI, 0, sizeof(drft->omegaCorrI));
			memset(drft->accels_e_integrator, 0, sizeof(drft->accels_e_integrator));

			// TODO: Expose these settings through UAVO
			drft->accelsKp = 1;
			drft->rollPitchKp = 20;
			drft->rollPitchKi = 1;
			drft->yawKp = 0;
			drft->yawKi = 0;
			drft->gyroCalibTau = 100;

			// Set flags
			if (optionalModules[HWSETTINGS_OPTIONALMODULES_GPS] == HWSETTINGS_OPTIONALMODULES_ENABLED && PIOS_COM_GPS) {
				GPSVelocityConnectCallback(GPSVelocityUpdatedCb);
				drft->gpsPresent_flag = true;
				drft->gpsVelocityDataConsumption_flag = GPS_CONSUMED;
			} else {
				drft->gpsPresent_flag = false;
			}

#if defined (PIOS_INCLUDE_MAGNETOMETER)
			MagnetometerConnectCallback(MagnetometerUpdatedCb);
#endif
			drft->magNewData_flag = false;
			drft->delT_between_GPS = 0;
			firstpass_flag = false;
		}

		// Apply arbitrary scaling to get into effective units
		drft->rollPitchKp = glblAtt->accelKp * 1000.0f;
		drft->rollPitchKi = glblAtt->accelKi * 10000.0f;

		// Convert quaternions into rotation matrix
		float Rbe[3][3];
		Quaternion2R(glblAtt->q, Rbe);

#if defined (PIOS_INCLUDE_GPS)
		if (attitudeSettings.FilterChoice == ATTITUDESETTINGS_FILTERCHOICE_PREMERLANI_GPS) {
			Premerlani_GPS(accels, gyros, Rbe, delT, true, glblAtt, omegaCorrP);
		} else if (attitudeSettings.FilterChoice == ATTITUDESETTINGS_FILTERCHOICE_PREMERLANI)
#endif
		{
			Premerlani_DCM(accels, gyros, Rbe, delT, false, glblAtt, omegaCorrP); //<-- GAWD, I HATE HOW FUNCTION ARGUMENTS JUST PILE UP. IT LOOKS UNPROFESSIONAL TO MIX INPUTS AND OUTPUTS
		}
	}
	
	//Calibrate the gyroscopes.	
	//TODO: but only calibrate when system is armed.
	if (0) { //<-- CURRENTLY DISABLE UNTIL TESTING CAN BE DONE.
		float normOmegaScalar = VectorMagnitude(gyros);
		calibrate_gyros_high_speed(gyros, omegaCorrP, normOmegaScalar, delT, &(attitudeSettings.GyroScale[0]));
	}
}

//Values taken from GentleNav
#define MINIMUM_SPIN_RATE_GYRO_CALIB 50.0	// degrees/second
/*
 * At high speeds, the gyro gains can be honed in on. 
 *  Taken from "Fast Rotations", William Premerlani
 */
static void calibrate_gyros_high_speed(float gyro[3], float omegaCorrP[3], float normOmegaScalar, float delT, float *ggain)
{
	if (normOmegaScalar > MINIMUM_SPIN_RATE_GYRO_CALIB) {
		float normOmegaVector[3] = { gyro[0] / normOmegaScalar, gyro[1] / normOmegaScalar, gyro[2] / normOmegaScalar };
		
		//Calculate delta gain and update gains
		ggain[0] += normOmegaVector[0] * omegaCorrP[0] / normOmegaScalar *
		(attitudeSettings.GyroGain / drft->gyroCalibTau) * delT;
		ggain[1] += normOmegaVector[1] * omegaCorrP[1] / normOmegaScalar *
		(attitudeSettings.GyroGain / drft->gyroCalibTau) * delT;
		ggain[2] += normOmegaVector[2] * omegaCorrP[2] / normOmegaScalar *
		(attitudeSettings.GyroGain / drft->gyroCalibTau) * delT;
		
		//Saturate gyro gains
		float lowThresh = 1.0f / 1.05f * attitudeSettings.GyroGain;
		float highThresh = 1.05f * attitudeSettings.GyroGain;
		for (int i = 0; i < 3; i++) {
			ggain[i] = (ggain[i] < lowThresh)  ? lowThresh  : ggain[i];
			ggain[i] = (ggain[i] > highThresh) ? highThresh : ggain[i];
		}
	}
}



static void GPSVelocityUpdatedCb(UAVObjEvent * objEv)
{
	drft->gpsVelocityDataConsumption_flag = GPS_UNCONSUMED;
}

#if defined (PIOS_INCLUDE_MAGNETOMETER)
static void MagnetometerUpdatedCb(UAVObjEvent * objEv)
{
	drft->magNewData_flag = true;
}
#endif


/**
 * @}
 * @}
 */

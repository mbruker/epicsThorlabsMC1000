/*
 * EPICS asyn driver for Thorlabs model MC1000 optical chopper
 * December 2018, M. W. Bruker
 */

#ifndef _THORLABS_MC1000_DRIVER_H
#define _THORLABS_MC1000_DRIVER_H

#include "asynPortDriver.h"
#include <string>

class asynUser;
class epicsMutex;

#define P_FirmwareRevision_String	"FIRMWARE_REVISION"
#define P_EnableMotor_String	"ENABLE_MOTOR"
#define P_EnableEcho_String	"ENABLE_ECHO"
#define P_MotorRunning_String	"MOTOR_RUNNING"
#define P_Blade_String		"BLADE"
#define P_InternalFreq_String	"INTERNAL_FREQ"
#define P_HarmonicMult_String	"HARMONIC_MULT"
#define P_Subharmonic_String	"SUBHARMONIC"
#define P_ExternalRef_String	"EXTERNAL_REF"
#define P_RefOutput_String	"REF_OUTPUT"
#define P_Status_String		"STATUS"
#define P_CurrentFreq_String	"CURRENT_FREQ"

class ThorlabsMC1000Driver : public asynPortDriver {
public:
	ThorlabsMC1000Driver(const char *portName, const char *serialPortName);
	virtual asynStatus writeInt32(asynUser *pasynUser, epicsInt32 value);
	void pollThread();
protected:
	struct ioPvt {
		asynOctet *pasynOctet;
		void *octetPvt;
	};

	asynStatus sendMessage(const char *s);
	unsigned short int recvLine();
	void parseMessage(std::string message);

	int initialised;

	void setReplyBuffer(const std::string &reply);
	std::string replyBuffer;
	epicsMutex *replyBufferMutex;

	int P_FirmwareRevision;
	int P_EnableMotor;
	int P_EnableEcho;
	int P_Blade;
	int P_InternalFreq;
	int P_HarmonicMult;
	int P_Subharmonic;
	int P_ExternalRef;
	int P_RefOutput;
	int P_Status;
	int P_CurrentFreq;

	asynUser *asynUserSerial;
};

#endif

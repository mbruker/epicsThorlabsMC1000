/*
    EPICS asyn driver for Thorlabs model MC1000 optical chopper
    December 2018, M. W. Bruker

    Whoever designed this protocol should be fired immediately.
    Seriously, there are no words to describe it.
    The manual states that "for experienced programmers, the chopper
    serial interface may be incorporated into a user program [...]".
    Unfortunately, it seems that such a person was not available at
    the company to test it.
*/

#include "thorlabs_mc1000_driver.h"
#include <asynDriver.h>
#include <iocsh.h>
#include <epicsExport.h>
#include <epicsThread.h>
#include <asynOctetSyncIO.h>
#include <stdint.h>
#include <string.h>
#include <cantProceed.h>

#define STATUS_STOPPED 0
#define STATUS_RUNNING 1
#define STATUS_NO_REF 2
#define STATUS_LOCK_ERROR 3
#define STATUS_PLL_ERROR 4

void ThorlabsMC1000PollThreadC(void *drvPvt)
{
    ((ThorlabsMC1000Driver *) drvPvt)->pollThread();
}

ThorlabsMC1000Driver::ThorlabsMC1000Driver(const char *portName, const char *serialPortName)
   : asynPortDriver(portName,
                    1, /* maxAddr */
                    asynInt32Mask | asynOctetMask | asynDrvUserMask,
                    asynInt32Mask | asynOctetMask,
                    ASYN_CANBLOCK, /* asynFlags */
                    1, /* autoConnect */
                    0, /* default priority */
                    0), /* default stack size */
    initialised(0)
{
    asynStatus status;
    asynInterface *pasynInterface;
    struct ioPvt *pioPvt;
    
    pioPvt = (struct ioPvt *) callocMustSucceed(1, sizeof(struct ioPvt), "ThorlabsMC1000");
    asynUserSerial = pasynManager->createAsynUser(0, 0);
    asynUserSerial->userPvt = pioPvt;
    status = pasynManager->connectDevice(asynUserSerial, serialPortName, 0);
    if (status != asynSuccess) {
        printf("Cannot connect to port %s: %s\n", serialPortName, asynUserSerial->errorMessage);
        return;
    }
    pasynInterface = pasynManager->findInterface(asynUserSerial, asynOctetType, 1);
    if (!pasynInterface) {
        printf("%s interface not supported\n", asynOctetType);
        return;
    }
    pioPvt->pasynOctet = (asynOctet *) pasynInterface->pinterface;
    pioPvt->octetPvt = pasynInterface->drvPvt;

    replyBufferMutex = new epicsMutex;
    replyBuffer[0] = 0;

    createParam(P_FirmwareRevision_String, asynParamOctet, &P_FirmwareRevision);
    createParam(P_EnableMotor_String, asynParamInt32, &P_EnableMotor);
    createParam(P_EnableEcho_String, asynParamInt32, &P_EnableEcho);
    createParam(P_Blade_String, asynParamInt32, &P_Blade);
    createParam(P_InternalFreq_String, asynParamInt32, &P_InternalFreq);
    createParam(P_HarmonicMult_String, asynParamInt32, &P_HarmonicMult);
    createParam(P_Subharmonic_String, asynParamInt32, &P_Subharmonic);
    createParam(P_ExternalRef_String, asynParamInt32, &P_ExternalRef);
    createParam(P_RefOutput_String, asynParamInt32, &P_RefOutput);
    createParam(P_Status_String, asynParamInt32, &P_Status);
    createParam(P_CurrentFreq_String, asynParamInt32, &P_CurrentFreq);

    pasynManager->lockPort(asynUserSerial);
    sendMessage("\r\n");    // display the main menu
    pasynManager->unlockPort(asynUserSerial);
    
    status = (asynStatus) (epicsThreadCreate("ThorlabsMC1000PollThread",
                                             epicsThreadPriorityMedium,
                                             epicsThreadGetStackSize(epicsThreadStackMedium),
                                             (EPICSTHREADFUNC) ThorlabsMC1000PollThreadC,
                                             this) == NULL);
    if (status) {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "epicsThreadCreate failed\n");
    }

    int elapsed = 0;
    while (elapsed < 20) {
        lock();
        if (initialised) {
            unlock();
            break;
        }
        unlock();
        epicsThreadSleep(0.1);
    }
    if (!initialised) {
        printf("ThorlabsMC1000: Failed to read initial data from the device. Driver not initialised\n");
        return;
    }
    char buffer[100];
    getStringParam(P_FirmwareRevision, 99, buffer);
    printf("ThorlabsMC1000: Driver initialised. Firmware revision: %s\n", buffer);

    int echo;
    lock();
    getIntegerParam(P_EnableEcho, &echo);
    unlock();
    if (!echo)
        sendMessage("e");
    elapsed = 0;
    while (elapsed < 20) {
        lock();
        getIntegerParam(P_EnableEcho, &echo);
        unlock();
        if (echo)
            break;
    }
    if (!echo)
        printf("ThorlabsMC1000: Failed to turn on echo. Speed readout not functional\n");
}

void ThorlabsMC1000Driver::pollThread()
{
    for (;;) {
        recvLine();
        epicsThreadSleep(0.2);
    }
}

asynStatus ThorlabsMC1000Driver::sendMessage(const char *s)
{
    asynStatus status = asynSuccess;
    size_t numBytes = 0;
    struct ioPvt *pioPvt = (struct ioPvt *) asynUserSerial->userPvt;

    pasynManager->lockPort(asynUserSerial);
    asynUserSerial->timeout = 1.0;
    while (*s) {
        status = pioPvt->pasynOctet->write(pioPvt->octetPvt, asynUserSerial, s++, 1, &numBytes);
        // Even though the device operates at a fixed baud rate of 19.2 kbaud,
        // sending any reply at this rate causes this excellent piece of equipment to hiccup.
        // You have to pause after every byte to mimic human typing.
        epicsThreadSleep(0.01);
    }
    pasynManager->unlockPort(asynUserSerial);

    return status;
}

void ThorlabsMC1000Driver::parseMessage(std::string message)
{
    std::size_t a, b;
    while ((a = message.find_first_of("\r\n")) != std::string::npos)
        message.erase(a, 1);

    if (message.find("Enter new value") != std::string::npos) {
        replyBufferMutex->lock();
        sendMessage(replyBuffer.c_str());
        replyBuffer.clear();
        replyBufferMutex->unlock();
        return;
    }

    if ((message.find("<NO REF>") != std::string::npos) || (message.find("<\"REF \">") != std::string::npos)) {
        lock();
        setIntegerParam(P_Status, STATUS_NO_REF);
        setIntegerParam(P_CurrentFreq, 0);
        setIntegerParam(P_EnableMotor, 0);
        unlock();
        return;
    }
    if ((message.find("<\"LOC \">") != std::string::npos) || (message.find("<motor exceeded>") != std::string::npos)) {
        lock();
        setIntegerParam(P_Status, STATUS_LOCK_ERROR);
        setIntegerParam(P_CurrentFreq, 0);
        setIntegerParam(P_EnableMotor, 0);
        unlock();
        return;
    }
    if (message.find("<\"PLL \">") != std::string::npos) {
        lock();
        setIntegerParam(P_Status, STATUS_PLL_ERROR);
        setIntegerParam(P_CurrentFreq, 0);
        setIntegerParam(P_EnableMotor, 0);
        unlock();
        return;
    }
    if (message.find("<\"OFF \">") != std::string::npos) {
        lock();
        setIntegerParam(P_Status, STATUS_STOPPED);
        setIntegerParam(P_CurrentFreq, 0);
        setIntegerParam(P_EnableMotor, 0);
        unlock();
        return;
    }

    a = message.find("<\"");
    b = message.find("\">", a);
    if ((a != std::string::npos) && (b != std::string::npos)) {
        std::string speedStr = message.substr(a + 2, b - a - 2);
        lock();
        try {
            int speed = stoi(speedStr);
            setIntegerParam(P_CurrentFreq, speed);
        } catch (std::exception &e) {
        }
        unlock();
        return;
    }

    a = message.find("Revision");
    if (a != std::string::npos) {
        lock();
        setStringParam(P_FirmwareRevision, message.substr(a).c_str());
        unlock();
        return;
    }

    a = message.find("(R)un motor");
    if (a != std::string::npos) {
        if (message.find("On") != std::string::npos) {
            lock();
            setIntegerParam(P_EnableMotor, 1);
            setIntegerParam(P_Status, STATUS_RUNNING);
            unlock();
        }
        return;
    }

    a = message.find("(E)cho panel");
    if (a != std::string::npos) {
        lock();
        if (message.find("On") != std::string::npos) {
            setIntegerParam(P_EnableEcho, 1);
        } else {
            setIntegerParam(P_EnableEcho, 0);
        }
        unlock();
        return;
    }

    a = message.find("(B)lade");
    if (a != std::string::npos) {
        a = message.find("(", a + 7);
        b = message.find(")", a + 1);
        std::string between = message.substr(a + 1, b - a - 1);
        lock();
        if (between == "B 10")
            setIntegerParam(P_Blade, 0);
        else if (between == "B 15")
            setIntegerParam(P_Blade, 1);
        else if (between == "B 30")
            setIntegerParam(P_Blade, 2);
        else if (between == "B 60")
            setIntegerParam(P_Blade, 3);
        else if (between == "B 2F")
            setIntegerParam(P_Blade, 4);
        else if (between == "B  2")
            setIntegerParam(P_Blade, 5);
        else if (between == "B2-2")
            setIntegerParam(P_Blade, 6);
        unlock();
        return;
    }

    a = message.find("(I)nternal Frequency");
    if (a != std::string::npos) {
        a = message.find("(", a + 7);
        b = message.find(")", a + 1);
        std::string between = message.substr(a + 1, b - a - 1);
        lock();
        try {
            setIntegerParam(P_InternalFreq, std::stoi(between));
        } catch (std::exception &e) {
        }
        unlock();
        return;
    }

    a = message.find("(N) Harmonic Multiplier");
    if (a != std::string::npos) {
        a = message.find("(", a + 7);
        b = message.find(")", a + 1);
        std::string between = message.substr(a + 1, b - a - 1);
        lock();
        try {
            setIntegerParam(P_HarmonicMult, std::stoi(between));
        } catch (std::exception &e) {
        }
        unlock();
        return;
    }

    a = message.find("(M) Sub-Harmonic Divider");
    if (a != std::string::npos) {
        a = message.find("(", a + 7);
        b = message.find(")", a + 1);
        std::string between = message.substr(a + 1, b - a - 1);
        lock();
        try {
            setIntegerParam(P_Subharmonic, std::stoi(between));
        } catch (std::exception &e) {
        }
        unlock();
        return;
    }

    a = message.find("(X) External Reference");
    if (a != std::string::npos) {
        lock();
        if (message.find("On") != std::string::npos)
            setIntegerParam(P_ExternalRef, 1);
        else
            setIntegerParam(P_ExternalRef, 0);
        unlock();
        return;
    }

    a = message.find("(O) Reference Output");
    if (a != std::string::npos) {
        a = message.find("(", a + 7);
        b = message.find(")", a + 1);
        std::string between = message.substr(a + 1, b - a - 1);
        lock();
        if (between == "outer")
            setIntegerParam(P_RefOutput, 0);
        else if (between == "synth")
            setIntegerParam(P_RefOutput, 1);
        else if (between == "sum")
            setIntegerParam(P_RefOutput, 2);
        else if (between == "diff")
            setIntegerParam(P_RefOutput, 3);
        unlock();
        return;
    }
    
    if (message.find("Enter choice") != std::string::npos) {
        lock();
        initialised = 1;
        unlock();
        return;
    }

}

// You cannot just send the parameter of a command immediately after the command.
// Rather, you have to wait until the device is done asking for it.
void ThorlabsMC1000Driver::setReplyBuffer(const std::string &reply)
{
    for (;;) {
        replyBufferMutex->lock();
        if (!replyBuffer.size())
            break;
        replyBufferMutex->unlock();
        epicsThreadSleep(0.01);
    }
    replyBuffer = reply;
    replyBufferMutex->unlock();
}

unsigned short int ThorlabsMC1000Driver::recvLine()
{
    size_t numBytes = 0;
    struct ioPvt *pioPvt = (struct ioPvt *) asynUserSerial->userPvt;
    static const int bufferSize = 256;
    char buffer[bufferSize];
    memset(buffer, 0, bufferSize);
    static std::string message;

    pasynManager->lockPort(asynUserSerial);
    asynUserSerial->timeout = 0;
    pioPvt->pasynOctet->read(pioPvt->octetPvt, asynUserSerial, buffer, bufferSize, &numBytes, 0);
    pasynManager->unlockPort(asynUserSerial);
    if (numBytes == 0)
        return 0;
    
    message.append(buffer, numBytes);
    std::size_t delim = message.find_first_of("\n>");
    while (delim != std::string::npos) {
        parseMessage(message.substr(0, delim + 1));
        message.erase(0, delim + 1);
        delim = message.find_first_of("\n>");
    }

    lock();
    callParamCallbacks();
    unlock();
    
    return 0;
}

asynStatus ThorlabsMC1000Driver::writeInt32(asynUser *pasynUser, epicsInt32 value)
{
    int function = pasynUser->reason;
    
    if (function == P_EnableMotor) {
        int oldValue;
        getIntegerParam(P_EnableMotor, &oldValue);
        if (value != oldValue)
            return sendMessage("r");
        return asynSuccess;
    } else if (function == P_EnableEcho) {
        int oldValue;
        getIntegerParam(P_EnableEcho, &oldValue);
        if (value != oldValue)
            return sendMessage("e");
        return asynSuccess;
    } else if (function == P_Blade) {
        sendMessage("b");
        setReplyBuffer(std::to_string(value) + "\n");
        return asynSuccess;
    } else if (function == P_InternalFreq) {
        sendMessage("i");
        setReplyBuffer(std::to_string(value) + "\n");
        return asynSuccess;
    } else if (function == P_HarmonicMult) {
        sendMessage("n");
        setReplyBuffer(std::to_string(value) + "\n");
        return asynSuccess;
    } else if (function == P_Subharmonic) {
        sendMessage("m");
        setReplyBuffer(std::to_string(value) + "\n");
        return asynSuccess;
    } else if (function == P_ExternalRef) {
        int oldValue;
        getIntegerParam(P_ExternalRef, &oldValue);
        if (value != oldValue)
            return sendMessage("x");
        return asynSuccess;
    } else if (function == P_RefOutput) {
        sendMessage("o");
        setReplyBuffer(std::to_string(value) + "\n");
        return asynSuccess;
    } else
   	    return asynPortDriver::writeInt32(pasynUser, value);
}


extern "C" {

int ThorlabsMC1000Configure(const char *portName, const char *serialPortName)
{
    new ThorlabsMC1000Driver(portName, serialPortName); // scary but apparently the usual way
    return asynSuccess;
}

static const iocshArg initArg0 = { "portName", iocshArgString };
static const iocshArg initArg1 = { "serialPortName", iocshArgString };
static const iocshArg * const initArgs[] = {&initArg0, &initArg1};
static const iocshFuncDef initFuncDef = { "ThorlabsMC1000Configure", 2, initArgs };
static void initCallFunc(const iocshArgBuf *args)
{
    ThorlabsMC1000Configure(args[0].sval, args[1].sval);
}

void ThorlabsMC1000DriverRegister()
{
    iocshRegister(&initFuncDef, initCallFunc);
}

epicsExportRegistrar(ThorlabsMC1000DriverRegister);

}


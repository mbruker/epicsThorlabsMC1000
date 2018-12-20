#!../../bin/linux-x86_64/test

< envPaths

cd "${TOP}"

## Register all support components
dbLoadDatabase "dbd/test.dbd"
test_registerRecordDeviceDriver pdbbase

cd "${TOP}/iocBoot/${IOC}"
epicsEnvSet("STREAM_PROTOCOL_PATH", ".")

drvAsynSerialPortConfigure("TTY_MC1000", "/dev/ttyUSB0")
asynSetOption("TTY_MC1000", 0, "baud", 19200)
asynSetOption("TTY_MC1000", 0, "bits", 8)
asynSetOption("TTY_MC1000", 0, "parity", "none")
asynSetOption("TTY_MC1000", 0, "stop", 1)
asynSetOption("TTY_MC1000", 0, "clocal", "Y")
asynSetOption("TTY_MC1000", 0, "crtscts", "N")
asynSetTraceMask("TTY_MC1000", -1, 9)
asynSetTraceIOMask("TTY_MC1000", -1, 2)

ThorlabsMC1000Configure("MC1000", "TTY_MC1000")

dbLoadRecords("${THORLABS_MC1000}/db/thorlabs_mc1000.db", "P=test:,R=mc1000:,PORT=MC1000,ADDR=0,TIMEOUT=1")


iocInit

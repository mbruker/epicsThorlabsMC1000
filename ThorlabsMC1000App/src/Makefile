TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

#==================================================
# build a support library

LIBRARY_IOC += ThorlabsMC1000

# xxxRecord.h will be created from xxxRecord.dbd
#DBDINC += xxxRecord
# install ThorlabsMC1000.dbd into <top>/dbd
DBD += ThorlabsMC1000.dbd

# specify all source files to be compiled and added to the library
ThorlabsMC1000_SRCS += thorlabs_mc1000_driver.cpp

ThorlabsMC1000_LIBS += $(EPICS_BASE_IOC_LIBS)

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE


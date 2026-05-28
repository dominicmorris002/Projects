#pragma once

#ifndef functions_hpp
#define functions_hpp

#include <cstdint>
#include <string>

// system functions
void resetAlarms();
void pidSetTunings();
void oilPumpPrime();
void testPumpIntlk();


// *************************************************************
// Global Variables
// *************************************************************
// **System**
// type             // variable                    
extern bool         sts_sysRun;
extern bool         inp_mtrRun;
extern bool         inp_lowOil;
extern bool         inp_aux1;
extern bool         inp_aux2;
extern bool         inp_aux3;
extern bool         out_mtrEnable;
extern bool         out_ledGrn;
extern bool         out_ledRed;
extern bool         ocmd_sysRun;
extern bool         ocmd_prime;   // add alongside ocmd_sysRun
extern bool         pcmd_sysRun;

// **Alarms**
extern bool         alm_active;
extern bool         alm_interlock;
extern bool         alm_sysFault;
extern bool         alm_lowDripRateWar;
extern bool         alm_lowDripRateShDn;
extern bool         alm_lowOil;
extern bool         alm_aux1;
extern bool         alm_aux2;
extern bool         alm_aux3;
extern bool         cfg_aux1Enable;
extern bool         cfg_aux2Enable;
extern bool         cfg_aux3Enable;
extern std::string  cfg_aux1Label;
extern std::string  cfg_aux2Label;
extern std::string  cfg_aux3Label;

// **Drip Rate**
// type             // variable
extern double       cfg_dripRate;
extern double       val_dripRate;
extern double       val_pumpSpd;
extern double       val_pumpSpdPct;
extern double       cfg_dripRateCtrlKp;
extern double       cfg_dripRateCtrlKi;
extern double       cfg_dripRateCtrlKd;
extern int          cfg_lowDripShDnDelay;
extern bool         cfg_lowDripShDnEnable;

// **Scheduling**
// type             // variable
extern uint8_t      cfg_schedHour;
extern uint8_t      cfg_schedMinute;
extern bool         cfg_schedDays[7];



#endif
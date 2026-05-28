/**********************************************************************************************
 * Arduino PID Library - Version 1.2.1
 * by Brett Beauregard <br3ttb@gmail.com> brettbeauregard.com
 *
 * This Library is licensed under the MIT License
 **********************************************************************************************/

#include <functional>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "PID.hpp"

LOG_MODULE_REGISTER(lib_PID);

/*Constructor (...)*********************************************************
 *    The parameters specified here are those for for which we can't set up
 *    reliable defaults, so we need to have the user set them.
 ***************************************************************************/
PID::PID(double* Input, double* Output, double* Setpoint,
        double Kp, double Ki, double Kd, int POn, int ControllerDirection)
{
   myOutput = Output;
   myInput = Input;
   mySetpoint = Setpoint;
   inAuto = false;

   PID::SetOutputLimits(0, 255);				//default output limit corresponds to
                                          //the arduino pwm limits

   SampleTime = 100;							   //default Controller Sample Time is 0.1 seconds
   TimingMode = OVERSAMPLE;               //default oversample mode

   PID::SetControllerDirection(ControllerDirection);
   PID::SetTunings(Kp, Ki, Kd, POn);

}

/*Constructor (...)*********************************************************
 *    To allow backwards compatability for v1.1, or for people that just want
 *    to use Proportional on Error without explicitly saying so
 ***************************************************************************/

PID::PID(double* Input, double* Output, double* Setpoint,
        double Kp, double Ki, double Kd, int ControllerDirection)
    :PID::PID(Input, Output, Setpoint, Kp, Ki, Kd, P_ON_E, ControllerDirection)
{

}

/* Compute() **********************************************************************
 *     This, as they say, is where the magic happens.  this function should be called
 *   every time "void loop()" executes.  the function will decide for itself whether a new
 *   pid Output needs to be computed.  returns true when the output is computed,
 *   false when nothing has been done.
 **********************************************************************************/
bool PID::Compute()
{
   if(!inAuto) return false;
   if (TimingMode == OVERSAMPLE){
      unsigned long now = k_uptime_get();
      unsigned long timeChange = (now - lastTime);
      if(timeChange < SampleTime){
         return false;
      }
      else {
         lastTime = now;
      }
   }

   /*Compute all the working error variables*/
   double input = *myInput;
   double error = *mySetpoint - input;
   double dInput = (input - lastInput);
   
   LOG_DBG("PID Inputs: SP=%.2f, PV=%.2f, Error=%.2f, dInput=%.2f", *mySetpoint, input, error, dInput);
   LOG_DBG("PID Gains: kp=%.2f, ki=%.2f, kd=%.2f (internal)", kp, ki, kd);
   
   double prevOutputSum = outputSum;
   outputSum+= (ki * error);
   
   LOG_DBG("Integral: prev=%.2f, ki*error=%.2f, new=%.2f", prevOutputSum, (ki * error), outputSum);

   /*Add Proportional on Measurement, if P_ON_M is specified*/
   if(!pOnE) {
      outputSum-= kp * dInput;
      LOG_DBG("P_ON_M applied: outputSum -= %.2f, new outputSum=%.2f", kp * dInput, outputSum);
   } else {
      LOG_DBG("P_ON_E mode: P_ON_M skipped");
   }

   double outputSumBeforeLimit = outputSum;
   if(outputSum > outMax) outputSum= outMax;
   else if(outputSum < outMin) outputSum= outMin;
   
   if(outputSumBeforeLimit != outputSum) {
      LOG_DBG("Integral clamped: %.2f -> %.2f (limits: %.2f to %.2f)", 
              outputSumBeforeLimit, outputSum, outMin, outMax);
   }

   /*Add Proportional on Error, if P_ON_E is specified*/
   double output;
   if(pOnE) {
      output = kp * error;
      LOG_DBG("P_ON_E: kp * error = %.2f * %.2f = %.2f", kp, error, output);
   } else {
      output = 0;
      LOG_DBG("P_ON_M mode: P term = 0");
   }

   /*Compute Rest of PID Output*/
   double dTerm = -kd * dInput;
   double finalOutputBeforeLimit = output + outputSum + dTerm;
   output += outputSum + dTerm;
   
   LOG_DBG("Final calc: P(%.2f) + I(%.2f) + D(%.2f) = %.2f", 
           (pOnE ? kp * error : 0.0), outputSum, dTerm, finalOutputBeforeLimit);

   if(output > outMax) output = outMax;
   else if(output < outMin) output = outMin;
   
   if(finalOutputBeforeLimit != output) {
      LOG_DBG("Output clamped: %.2f -> %.2f (limits: %.2f to %.2f)", 
              finalOutputBeforeLimit, output, outMin, outMax);
   }
   
   *myOutput = output;
   
   LOG_DBG("PID Final: Output=%.2f", output);

   /*Remember some variables for next time*/
   lastInput = input;
   return true;
}



/* SetTunings(...)*************************************************************
 * This function allows the controller's dynamic performance to be adjusted.
 * it's called automatically from the constructor, but tunings can also
 * be adjusted on the fly during normal operation
 ******************************************************************************/
void PID::SetTunings(double Kp, double Ki, double Kd, int POn)
{
   if (Kp<0 || Ki<0 || Kd<0) return;

   pOn = POn;
   pOnE = POn == P_ON_E;

   dispKp = Kp; dispKi = Ki; dispKd = Kd;

   double SampleTimeInSec = ((double)SampleTime)/1000;
   kp = Kp;
   ki = Ki * SampleTimeInSec;
   kd = Kd / SampleTimeInSec;

   if(controllerDirection ==REVERSE)
   {
      kp = (0 - kp);
      ki = (0 - ki);
      kd = (0 - kd);
   }
}

/* SetTunings(...)*************************************************************
 * Set Tunings using the last-rembered POn setting
 ******************************************************************************/
void PID::SetTunings(double Kp, double Ki, double Kd){
    SetTunings(Kp, Ki, Kd, pOn); 
}

/* SetSampleTime(...) *********************************************************
 * sets the period, in Milliseconds, at which the calculation is performed
 ******************************************************************************/
void PID::SetSampleTime(int NewTimingMode, int NewSampleTime)
{
   if (NewSampleTime > 0 && NewTimingMode > 0)
   {
      double ratio  = (double)NewSampleTime
                      / (double)SampleTime;
      ki *= ratio;
      kd /= ratio;
      SampleTime = (unsigned long)NewSampleTime;
      TimingMode = NewTimingMode;
   }
}

/* SetOutputLimits(...)****************************************************
 *     This function will be used far more often than SetInputLimits.  while
 *  the input to the controller will generally be in the 0-1023 range (which is
 *  the default already,)  the output will be a little different.  maybe they'll
 *  be doing a time window and will need 0-8000 or something.  or maybe they'll
 *  want to clamp it from 0-125.  who knows.  at any rate, that can all be done
 *  here.
 **************************************************************************/
void PID::SetOutputLimits(double Min, double Max)
{
   if(Min >= Max) return;
   outMin = Min;
   outMax = Max;

   if(inAuto)
   {
	   if(*myOutput > outMax) *myOutput = outMax;
	   else if(*myOutput < outMin) *myOutput = outMin;

	   if(outputSum > outMax) outputSum= outMax;
	   else if(outputSum < outMin) outputSum= outMin;
   }
}

/* SetMode(...)****************************************************************
 * Allows the controller Mode to be set to manual (0) or Automatic (non-zero)
 * when the transition from manual to auto occurs, the controller is
 * automatically initialized
 ******************************************************************************/
void PID::SetMode(int Mode)
{
   bool newAuto = (Mode == AUTOMATIC);
   if(newAuto && !inAuto)
   {  /*we just went from manual to auto*/
      PID::Initialize();
   }
   inAuto = newAuto;
}

/* Initialize()****************************************************************
 *	does all the things that need to happen to ensure a bumpless transfer
 *  from manual to automatic mode.
 ******************************************************************************/
void PID::Initialize()
{
   outputSum = *myOutput;
   lastInput = *myInput;
   if(outputSum > outMax) outputSum = outMax;
   else if(outputSum < outMin) outputSum = outMin;
}

/* SetControllerDirection(...)*************************************************
 * The PID will either be connected to a DIRECT acting process (+Output leads
 * to +Input) or a REVERSE acting process(+Output leads to -Input.)  we need to
 * know which one, because otherwise we may increase the output when we should
 * be decreasing.  This is called from the constructor.
 ******************************************************************************/
void PID::SetControllerDirection(int Direction)
{
   if(inAuto && Direction !=controllerDirection)
   {
	   kp = (0 - kp);
      ki = (0 - ki);
      kd = (0 - kd);
   }
   controllerDirection = Direction;
}

/* Status Funcions*************************************************************
 * Just because you set the Kp=-1 doesn't mean it actually happened.  these
 * functions query the internal state of the PID.  they're here for display
 * purposes.  this are the functions the PID Front-end uses for example
 ******************************************************************************/
double PID::GetKp(){ return  dispKp; }
double PID::GetKi(){ return  dispKi;}
double PID::GetKd(){ return  dispKd;}
int PID::GetMode(){ return  inAuto ? AUTOMATIC : MANUAL;}
int PID::GetDirection(){ return controllerDirection;}


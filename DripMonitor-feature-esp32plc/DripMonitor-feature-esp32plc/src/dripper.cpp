// *************************************************************
// Includes & Logging
// *************************************************************
#include "dripper.hpp"

#include <stdint.h>
#include <deque>

#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/sys/clock.h>

#include <time.h>

#include "storage.hpp"
#include "modbus_rtu.h"                 // ← ADDED

#include "../include/PID.hpp"
#include "../include/utility.hpp"

// External variables
extern bool provisioning_mode;

LOG_MODULE_REGISTER(app_dripper, CONFIG_APP_DRIPPER_LOG_LEVEL);

// *************************************************************
// Configuration
// *************************************************************
// type         // variable                         // value    // comment
#define         cfg_dripperThreadStackSize          4096        // stack size in bytes
#define         cfg_dripperThreadStartDelay         500         // start delay in ms

// **Drip Rate**
// type         // variable                         // value    // comment
#define         cfg_dripRateMin                     5           // minimum setpoint
#define         cfg_dripRateMax                     50          // maximum setpoint
#define         cfg_dripSMASize                     3           // number of prev drips for simple average
#define         cfg_dripRateFiltTC                  20000       // time constant for filtering, milliseconds
#define         cfg_dripRateCutoffInterval          15          // cutoff time where drip rate clamps to 0, seconds
#define         cfg_dripRateLowAlarm                3           // alarm if drip rate is this amount below setpoint
#define         cfg_dripRateLowWarnDelay            300000      // delay time before alarming, milliseconds

// **Oil Pump**
// type       // variable                           // value    // comment
#define         cfg_pumpMinRPM                      0.5         // Max RPM allowable in PID control
#define         cfg_pumpMaxRPM                      10          // Max RPM allowable in PID control
#define         cfg_pumpPrimeRPM                    30          // RPM for priming
#define         cfg_pumpStepsPerRev                 1600        // steps per rev as configured on stepper driver
#define         cfg_pumpStepWidth                   50          // duration (usec) to keep step pulse on
#define         cfg_pumpPIDSampleTime               500         // period (msec) for PID updates
#define         cfg_preOilMaxTime                   30*60*1000  // period (msec) allowed for pre-oil run time


// *************************************************************
// I/O
// *************************************************************
// **inputs**
static const struct gpio_dt_spec inp_drip_dt = GPIO_DT_SPEC_GET_OR(DT_ALIAS(inp_drip), gpios, {0});
static const struct gpio_dt_spec inp_mtrRun_dt = GPIO_DT_SPEC_GET_OR(DT_ALIAS(inp_mtr_run), gpios, {0});
static const struct gpio_dt_spec inp_lowOil_dt = GPIO_DT_SPEC_GET_OR(DT_ALIAS(inp_low_oil), gpios, {0});
//static const struct gpio_dt_spec inp_aux1_dt = GPIO_DT_SPEC_GET_OR(DT_ALIAS(inp_aux1), gpios, {0});
//static const struct gpio_dt_spec inp_aux2_dt = GPIO_DT_SPEC_GET_OR(DT_ALIAS(inp_aux2), gpios, {0});
//static const struct gpio_dt_spec inp_aux3_dt = GPIO_DT_SPEC_GET_OR(DT_ALIAS(inp_aux3), gpios, {0});

// **outputs**
//
// All four outputs land on PCA9685 PWM channels (the PLC 21 has no native
// digital output GPIOs available at the screw terminals).  The three
// on/off-style outputs (motor enable, green LED, red LED) are driven via
// the PWM API as duty=full / duty=0 — see pwmDigitalSet().  The variable-
// speed pump output uses pwm_set_dt() with a varying period.
static const struct pwm_dt_spec out_mtrEnable_dt = PWM_DT_SPEC_GET(DT_ALIAS(out_mtr_enable));
static const struct pwm_dt_spec out_ledGrn_dt    = PWM_DT_SPEC_GET(DT_ALIAS(out_led_grn));
static const struct pwm_dt_spec out_ledRed_dt    = PWM_DT_SPEC_GET(DT_ALIAS(out_led_red));

// **ISR callbacks**
static struct gpio_callback cbData_getDripRate;


// *************************************************************
// Global Variables
// *************************************************************
// **System**
// type             // variable                     // value    // comment
bool                sts_sysRun;                                 // system run status
bool                inp_mtrRun;                                 // motor run status from input
bool                inp_lowOil;                                 // low oil status from input
bool                inp_aux1;                                   // aux1 status from input
bool                inp_aux2;                                   // aux2 status from input
bool                inp_aux3;                                   // aux3 status from input
bool                out_mtrEnable;                              // motor enable output
bool                out_ledGrn;                                 // green enclosure led
bool                out_ledRed;                                 // red enclosure led
bool                ocmd_sysRun;                                // operator command to start system manually
bool                pcmd_sysRun;                                // program command (scheduled) to start system manually
bool                ocmd_prime = false;
// **Alarms**
bool                alm_active;                                 // alarm summary bit
bool                alm_interlock;                              // interlocking alarms summary bit
bool                alm_sysFault;                               // controller fault alarm, non-resetable
bool                alm_lowDripRateWar;                         // low drip rate warning alarm
bool                alm_lowDripRateShDn;                        // low drip rate shutdown alarm
bool                alm_lowOil;                                 // low oil level alarm
bool                alm_aux1;                                   // user configurable aux 1 alarm
bool                alm_aux2;                                   // user configurable aux 2 alarm
bool                alm_aux3;                                   // user configurable aux 3 alarm
bool                cfg_aux1Enable;                             // user configurable aux 1 enable
bool                cfg_aux2Enable;                             // user configurable aux 2 enable
bool                cfg_aux3Enable;                             // user configurable aux 3 enable
std::string         cfg_aux1Label;                              // user configurable aux 1 label
std::string         cfg_aux2Label;                              // user configurable aux 1 label
std::string         cfg_aux3Label;                              // user configurable aux 1 label

// **Drip Rate**
// type             // variable                     // value    // comment
double              cfg_dripRate                    = 20;       // target drip rate, runtime configurable and stored through power cycle
double              val_dripRate;                               // oil drip rate in drips per minute
double              val_pumpSpd                     = 0;        // pump speed calculated by PID in RPM
double              val_pumpSpdPct                  = 0;        // pump speed in percent
double              cfg_dripRateCtrlKp              = 0.25;      // drip rate proportional, default value if none stored in flash memory
double              cfg_dripRateCtrlKi              = 0.01;     // drip rate integral, default value if none stored in flash memory
double              cfg_dripRateCtrlKd              = 0;        // drip rate derivative, default value if nong_pumpStepWidthe stored in flash memory
int                 cfg_lowDripShDnDelay    = 5;                // delay in minutes after warning alarm to shutdown pump
bool                cfg_lowDripShDnEnable   = 1;                // enable/disable low drip shutdown protection

// **Scheduling**
// type             // variable                     // value    // comment
uint8_t             cfg_schedHour                   = 0;        // hour of the day (24hr format) to start
uint8_t             cfg_schedMinute                 = 0;        // minute of the day to start
bool                cfg_schedDays[7]                = {0};      // days to run, no days disables scheduled start

// *************************************************************
// Local Variables
// *************************************************************
// **Watchdog**
// type             // variable                     // value    // comment
static int          wdt_channel_id;
static const struct device *const wdt               = DEVICE_DT_GET(DT_ALIAS(watchdog0));
static bool         sts_wdtEnabled;                             // status to ensure watchdog gets installed to permit system to run
static bool         sts_pca9685OutputsOk;                       // false when PCA9685/I2C is absent — skip PWM to avoid long stalls
static bool         sts_expanderInputsOk;                       // false when MCP23008/I2C is absent — skip expander reads
// **Oil Pump**
// type             // variable                     // value    // comment
static uint32_t     val_pumpStepWidth               = 0;        // duration of the pulse to stepper driver
static bool         sts_oilPumpRunning              = 0;        // oil pump running status bit
static bool         sts_oilPumpPriming              = 0;        // oil pump priming status bit
static bool         sts_testPumpIntlk               = 0;        // pump interlock testing status bit

// **Drip Sensor**
// type             // variable                     // value    // comment
static int64_t      val_lastDripTime                = 0;        // time of last drip

// Pump speed control output (Q0.5 on PLC 21).  Currently driven the
// "old way": variable-period PWM emulating step pulses, fed through the
// PCA9685.  This will be re-tasked when the dedicated stepper-controller-
// with-analog-input change lands.
static const struct pwm_dt_spec out_pumpSpd_dt = PWM_DT_SPEC_GET(DT_ALIAS(out_pump_spd));

// Helper for treating a PCA9685 PWM channel as an on/off output.
// Sets duty cycle to 100 % (channel always-on) or 0 % (always-off).
static inline int pwmDigitalSet(const struct pwm_dt_spec *spec, bool on) {
    if (!sts_pca9685OutputsOk || spec == nullptr || !device_is_ready(spec->dev)) {
        return -ENODEV;
    }
    return pwm_set_pulse_dt(spec, on ? spec->period : 0);
}

static PID dripRateCtrl(&val_dripRate, &val_pumpSpd, &cfg_dripRate,
                 cfg_dripRateCtrlKp, cfg_dripRateCtrlKi, cfg_dripRateCtrlKd, P_ON_E, DIRECT);


// *************************************************************
// Static Functions
// *************************************************************
// watchdog
static int wdtInit();
static void wdtCallback(const struct device *wdt_dev, int channel_id);

// general
static inline void gpioConfig(const gpio_dt_spec *spec, const gpio_flags_t type, const char* ioName);
static void inputsInit();
static void outputsInit();

// alarm functions
static void alarmSummary();
static void lowDripRateAlarm(bool reset);
static void lowOilAlarm();
static void auxAlarms();

// drip rate functions
static void getDripRateCb(const struct device *dev, struct gpio_callback *cb, uint32_t pins);
static void dripRateCutoff(struct k_work *work);

// oil pump functions
static void pidInit();
static void oilPumpPrimeStop(struct k_work *work);
static void oilPumpPWM();

static void testPumpIntlkStop(struct k_work *work);;

// main
static void dripperMain();


// *************************************************************
// Thread
// *************************************************************
#ifdef CONFIG_ENABLE_DRIPPER
K_THREAD_DEFINE(dripperThread, cfg_dripperThreadStackSize, dripperMain, NULL, NULL, NULL, -10, K_ESSENTIAL | K_FP_REGS, cfg_dripperThreadStartDelay);
#endif


// *************************************************************
// Macro Functions
// *************************************************************
#define LOG_STATE_CHANGE(var) \
    do { \
        static bool var ## _prev; \
        if (var != var ## _prev){ \
            LOG_DBG("%s state change: %s", #var,  var ? "On" : "Off"); \
            var ## _prev = var; \
        } \
    } while (0)

/***************************************************************
 * @brief Provide debounce timers and inversion for input signal
 * @param inp: gpio input
 * @param onDbncTime: Timer in ms raw signal must be on before
 *      setting input true
 * @param offDbncTime: Timer in ms raw signal must be off before
 *      setting input false
 * @param invert: invert signal
 * 
 * @return processed input value
 **************************************************************/
static inline bool gpioInputRaw(const gpio_dt_spec *spec, bool invert)
{
    if (spec == nullptr || spec->port == nullptr || !device_is_ready(spec->port)) {
        return false;
    }
    int val = gpio_pin_get_dt(spec);
    if (val < 0) {
        return false;
    }
    return (val != 0) ^ invert;
}

#define INPUT_DEBOUNCE(inp, onDbncTime, offDbncTime, invert) \
    do { \
        static utyTimer inp##_onDbnc(onDbncTime); \
        static utyTimer inp##_offDbnc(offDbncTime); \
        static bool inp##_prev = false; \
        bool inp##_raw = gpioInputRaw(&inp##_dt, invert); \
        inp##_onDbnc.enable(inp##_raw); \
        inp##_offDbnc.enable(!inp##_raw); \
        if (inp##_onDbnc.done()) {inp = true;} \
        else if (inp##_offDbnc.done()){inp = false;} \
        else {inp = inp##_prev;} \
        inp##_prev = inp; \
    } while (0)

// *************************************************************
// Timers & Work
// *************************************************************
K_WORK_DELAYABLE_DEFINE(dripRateCutoffWrkD, dripRateCutoff);
K_WORK_DELAYABLE_DEFINE(oilPumpPrimeStopWrkD, oilPumpPrimeStop);
K_WORK_DELAYABLE_DEFINE(testPumpIntlkStopWrkD, testPumpIntlkStop);


/***************************************************************
 * @brief Initalize watchdog to monitor execution of this thread
 *        and reset the system if it stops executing
 * 
 * @return int 0 if successful, negative value on error
 **************************************************************/
static int wdtInit(){
    int ret;
    
    if (!device_is_ready(wdt)) {
		LOG_ERR("%s: device not ready", wdt->name);
		return -ENODEV;
	}

	struct wdt_timeout_cfg wdt_config = {
		/* Allow headroom for slow I2C when expander/PCA9685 is missing. */
		.window = {0U, 5000U},
        .callback = wdtCallback,
		.flags = WDT_FLAG_RESET_SOC,
	};

    wdt_channel_id = wdt_install_timeout(wdt, &wdt_config);
	if (wdt_channel_id < 0) {
		LOG_ERR("WDT install error: %d", wdt_channel_id);
		return wdt_channel_id;
	}

	ret = wdt_setup(wdt, 0);
	if (ret < 0) {
		LOG_ERR("WDT setup error: %d", ret);
		return ret;
	}

    return 0;
}


/***************************************************************
 * @brief Callback routine if watchdog timer expires
 * 
 * @param wdt_dev wdt device
 * @param channel_id wdt channel
 **************************************************************/
static void wdtCallback(const struct device *wdt_dev, int channel_id){
    /*
     * Watchdog warning ISR.
     *
     * On the original hardware this dropped the motor-enable line via a
     * direct GPIO write so the pump would stop a few ms before the chip
     * actually reset.  On the PLC 21 the motor enable is on a PCA9685
     * channel which means *I2C* — and I2C is not safe to touch from an
     * ISR (it would block on a semaphore and assert).  So this callback
     * is intentionally empty.
     *
     * The fail-safe is provided by hardware instead:
     *   1. The hardware watchdog forces a chip reset.
     *   2. On boot, the PCA9685 driver's init zeros all 16 channels,
     *      so motor enable + every other Q output are guaranteed OFF
     *      within a few hundred ms of any lock-up.
     */
    ARG_UNUSED(wdt_dev);
    ARG_UNUSED(channel_id);
}


/***************************************************************
 * @brief Helper function to config points and log errors
 *
 * Skips gracefully if the spec is unmapped (port == NULL) — happens
 * for any *_OR(... , {0}) spec whose alias / nodelabel does not
 * exist in the active board overlay (e.g. on the ESP32 PLC 21 we
 * currently have no gpio-mtr-enable / gpio-pump-step / led2 / led3
 * nodes).  Without this check, gpio_pin_configure_dt() would deref
 * a NULL `struct device *` and trip a CPU 0 EXCCAUSE 28 fault.
 *
 * @param spec gpio devicetree spec
 * @param type gpio flag
 * @param ioName gpio name for error logging
 **************************************************************/
static inline void gpioConfig(const gpio_dt_spec *spec, const gpio_flags_t type, const char* ioName){
    int ret;

    if (spec->port == nullptr) {
        LOG_WRN("%s GPIO not mapped in DTS, skipping configure", ioName);
        return;
    }
    if (!device_is_ready(spec->port)) {
        alm_sysFault = true;
        LOG_ERR("%s GPIO controller not ready", ioName);
        return;
    }

    ret = gpio_pin_configure_dt(spec, type);
    if (ret < 0) {
        alm_sysFault = true;
        LOG_ERR("%s GPIO Pin Configure Error: %d", ioName, ret);
    }
}


#if DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(mcp23008))
static bool mcp23008BusOk(void)
{
    static const struct i2c_dt_spec chip = I2C_DT_SPEC_GET(DT_NODELABEL(mcp23008));
    uint8_t iodir = 0;

    if (!i2c_is_ready_dt(&chip)) {
        return false;
    }
    /* IODIR register — proves the expander answers on the bus. */
    return i2c_reg_read_byte_dt(&chip, 0x03, &iodir) == 0;
}
#else
static bool mcp23008BusOk(void) { return false; }
#endif

/***************************************************************
 * @brief Configure inputs
 * 
 **************************************************************/
static void inputsInit(){
    int ret;

    // Drip sensor ISR.  Drip detection is non-optional: a missing alias or
    // an interrupt-incapable controller (e.g. MCP23008, whose INT line isn't
    // wired on PLC 21) sysFaults so the system runs in safe-state instead.
    gpioConfig(&inp_drip_dt, GPIO_INPUT, "Drip Sensor");
    if (inp_drip_dt.port == nullptr) {
        alm_sysFault = true;
        LOG_ERR("Drip Sensor GPIO not mapped in DTS, drip detection unavailable");
    } else {
        ret = gpio_pin_interrupt_configure_dt(&inp_drip_dt, GPIO_INT_EDGE_TO_ACTIVE);
        if (ret) {
            alm_sysFault = true;
            LOG_ERR("Drip Sensor GPIO Pin Interrupt Configure Error: %d", ret);
        } else {
            gpio_init_callback(&cbData_getDripRate, getDripRateCb, BIT(inp_drip_dt.pin));
            ret = gpio_add_callback(inp_drip_dt.port, &cbData_getDripRate);
            if (ret) {
                alm_sysFault = true;
                LOG_ERR("Drip Sensor GPIO Add Callback Error: %d", ret);
            }
        }
    }

    // Other inputs on MCP23008 — driver may report "ready" even when I2C NACKs
    sts_expanderInputsOk = mcp23008BusOk();
    if (sts_expanderInputsOk) {
        ret = gpio_pin_configure_dt(&inp_mtrRun_dt, GPIO_INPUT);
        if (ret < 0) {
            sts_expanderInputsOk = false;
            LOG_WRN("Motor Running GPIO configure failed (%d)", ret);
        }
        ret = gpio_pin_configure_dt(&inp_lowOil_dt, GPIO_INPUT);
        if (ret < 0) {
            sts_expanderInputsOk = false;
            LOG_WRN("Low Oil GPIO configure failed (%d)", ret);
        }
        if (!sts_expanderInputsOk) {
            LOG_WRN("MCP23008 inputs disabled");
        }
    } else {
        LOG_WRN("MCP23008 not on I2C bus — motor-run/low-oil inputs disabled");
    }
    //gpioConfig(&inp_aux1_dt, GPIO_INPUT, "Aux 1");
    //gpioConfig(&inp_aux2_dt, GPIO_INPUT, "Aux 2");
    //gpioConfig(&inp_aux3_dt, GPIO_INPUT, "Aux 3");
}


/***************************************************************
 * @brief Configure outputs
 *
 * The three on/off-style outputs are PCA9685 PWM channels, so there is
 * no per-pin "configure" step — the PCA9685 driver brings the chip up
 * during its own init and zeros every output.  We just sanity-check
 * that the controllers are ready and force the initial OFF state.
 **************************************************************/
#if DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(pca9685))
static bool pca9685BusOk(void)
{
    static const struct i2c_dt_spec chip = I2C_DT_SPEC_GET(DT_NODELABEL(pca9685));
    uint8_t mode1 = 0;

    if (!i2c_is_ready_dt(&chip)) {
        return false;
    }
    return i2c_reg_read_byte_dt(&chip, 0x00, &mode1) == 0;
}
#else
static bool pca9685BusOk(void) { return false; }
#endif

static void outputsInit(){
    sts_pca9685OutputsOk = pca9685BusOk();
    if (!sts_pca9685OutputsOk) {
        LOG_WRN("PCA9685 not on I2C bus — motor/LED outputs disabled");
        return;
    }

    const struct pwm_dt_spec *outs[] = {
        &out_mtrEnable_dt, &out_ledGrn_dt, &out_ledRed_dt,
    };

    for (size_t i = 0; i < ARRAY_SIZE(outs); i++) {
        int ret = pwm_set_pulse_dt(outs[i], 0);
        if (ret < 0) {
            sts_pca9685OutputsOk = false;
            LOG_WRN("PCA9685 output init failed (%d) — outputs disabled", ret);
            return;
        }
    }
}


/***************************************************************
 * @brief Sets alarm interlocks and summary global variables
 * 
 **************************************************************/
static void alarmSummary(){
    //interlocking alarms
    alm_interlock = alm_sysFault || alm_lowDripRateShDn;

    // all alarms (includes interlocking)
    alm_active = alm_interlock || alm_lowDripRateWar || alm_lowOil || alm_aux1 || alm_aux2 || alm_aux3;
}


/***************************************************************
 * @brief User called command to reset latched alarms
 * 
 **************************************************************/
void resetAlarms(){
    lowDripRateAlarm(true);
}


/***************************************************************
 * @brief Monitors drip rate and sets alarm for low drip rate
 * 
 * @param reset Resets the latched alarm if called with True
 **************************************************************/
static void lowDripRateAlarm(bool reset){
    static utyTimer dripRateLowWarnTmr(cfg_dripRateLowWarnDelay);
    static utyTimer dripRateLowShutdownTmr(cfg_lowDripShDnDelay);

    // check and update time if modified
    if (dripRateLowShutdownTmr.preset()*60*1000 != cfg_lowDripShDnDelay) {
        dripRateLowShutdownTmr.preset(cfg_lowDripShDnDelay*60*1000); 
    }

    // only shutdown alarm is latching and requires reset
    if (reset) {
        alm_lowDripRateShDn = false;
    }

    // evaluate alarms
    if (sts_sysRun && (val_dripRate < (cfg_dripRate - cfg_dripRateLowAlarm)) && !reset){
        dripRateLowWarnTmr.enable(true);
        if (dripRateLowWarnTmr.done()){
            alm_lowDripRateWar = true;
        }
    }
    else{
        dripRateLowWarnTmr.enable(false);
        alm_lowDripRateWar = false;
    }

    dripRateLowShutdownTmr.enable(alm_lowDripRateWar);
    if (dripRateLowShutdownTmr.done() && cfg_lowDripShDnEnable) {
        alm_lowDripRateShDn = true;
    }
}


/***************************************************************
 * @brief Monitors low oil input and sets alarm if low
 * 
 **************************************************************/
static void lowOilAlarm(){
    alm_lowOil = inp_lowOil;
}


/***************************************************************
 * @brief Monitors user configurable aux alarm inputs
 * 
 **************************************************************/
static void auxAlarms(){
    alm_aux1 = inp_aux1 && cfg_aux1Enable;
    alm_aux2 = inp_aux2 && cfg_aux2Enable;
    alm_aux3 = inp_aux3 && cfg_aux3Enable;
}


/***************************************************************
 * @brief Calculates the drip rate and resets alarm timer if in
 *        good range. Timeout function used to set rate to 0
 *        after no drips in period of time
 * 
 **************************************************************/
static void getDripRateCb(const struct device *dev, struct gpio_callback *cb, uint32_t pins){
    static std::deque<uint32_t> prevDripDTs;
    int64_t uptime = k_uptime_get();
    uint32_t dt = uptime - val_lastDripTime;
    uint32_t dtSum = 0;
    double wrk_dripRateRaw = 0;

    int ret;

    if (dt > 250) {

        // queue of the previous 3 times between drips to calculate the average rate for additional smoothing
        if (prevDripDTs.size() >= cfg_dripSMASize) {
            prevDripDTs.pop_front();
        }
        prevDripDTs.push_back(dt);
        
        // calculate the sum of previous drip times
        for (uint8_t i = 0; i < prevDripDTs.size(); i++){
            dtSum += prevDripDTs[i];
        }

        // calculate average drip rate for last 3 drips
        wrk_dripRateRaw = 60000.0 * prevDripDTs.size() / dtSum; // drips per minute
        
        // store last drip time to prevent double trigger on a drip
        val_lastDripTime = uptime;

        // calculate time filter multiplier and use to calculate filtered dripRate
        double alpha = (double)dtSum / (cfg_dripRateFiltTC * prevDripDTs.size() + dtSum);
        val_dripRate = ((1.0 - alpha) * val_dripRate) + (alpha * wrk_dripRateRaw);

        // start timer to clamp dripRate to 0 after period of time
        ret = k_work_reschedule(&dripRateCutoffWrkD, K_SECONDS(cfg_dripRateCutoffInterval));
        if (ret < 0){
            val_dripRate = 0;
            LOG_ERR("Failed to submit dripRateCutoffWrk: %d", ret);
        }

        // debug logging
        LOG_DBG("DT: %d", dt);
        LOG_DBG("Alpha: %f", alpha);
        LOG_DBG("Drip Rate, Raw: %f", wrk_dripRateRaw);
        LOG_DBG("Drip Rate, Filtered: %f", val_dripRate);
    }

}


/***************************************************************
 * @brief Delayable work function to set the drip rate to zero
 * 
 * @param work 
 **************************************************************/
static void dripRateCutoff(struct k_work *work){
    val_dripRate = 0;
}


/***************************************************************
 * @brief Retreives values from flash stroage and sets PID
 *        parameters
 * 
 **************************************************************/
static void pidInit(){
    storageInitVar(FS_DRIP_RATE_SP, &cfg_dripRate, sizeof(cfg_dripRate));
    storageInitVar(FS_DRIP_RATE_CTRL_KP, &cfg_dripRateCtrlKp, sizeof(cfg_dripRateCtrlKp));
    storageInitVar(FS_DRIP_RATE_CTRL_KI, &cfg_dripRateCtrlKi, sizeof(cfg_dripRateCtrlKi));
    storageInitVar(FS_DRIP_RATE_CTRL_KD, &cfg_dripRateCtrlKd, sizeof(cfg_dripRateCtrlKd));
        dripRateCtrl.SetOutputLimits(cfg_pumpMinRPM, cfg_pumpMaxRPM);
    dripRateCtrl.SetSampleTime(OVERSAMPLE, cfg_pumpPIDSampleTime);
    pidSetTunings();
}


/***************************************************************
 * @brief Helper function to set PID tuning parameters
 * 
 **************************************************************/
void pidSetTunings(){
    dripRateCtrl.SetTunings(cfg_dripRateCtrlKp, cfg_dripRateCtrlKi, cfg_dripRateCtrlKd);
}


/***************************************************************
 * @brief Primes oil pump by running it at 100% speed
 * 
 **************************************************************/
void oilPumpPrime(){
    int ret;
    sts_oilPumpPriming = true;
    // start timer to stop priming if routine is not re-called,
    // display does not have button release function
    ret = k_work_reschedule(&oilPumpPrimeStopWrkD, K_MSEC(1100));
    if (ret < 0){
        sts_oilPumpPriming = false;
        LOG_ERR("Failed to submit oilPumpPrimeStopWrk: %d", ret);
    }
}


/***************************************************************
 * @brief Delayable work function to stop oil pump priming since
 *        it is started by holding a button that does not signal
 *        when it is released
 * 
 * @param work 
 **************************************************************/
static void oilPumpPrimeStop(struct k_work *work){
    sts_oilPumpPriming = false;
}


/***************************************************************
 * @brief Primes oil pump by running it at 100% speed
 * 
 **************************************************************/
void testPumpIntlk(){
    int ret;
    sts_testPumpIntlk = true;
    // start timer to stop priming if routine is not re-called,
    // display does not have button release function
    ret = k_work_reschedule(&testPumpIntlkStopWrkD, K_MSEC(1100));
    if (ret < 0){
        sts_oilPumpPriming = false;
        LOG_ERR("Failed to submit testPumpIntlkStopWrk: %d", ret);
    }
}


/***************************************************************
 * @brief Delayable work function to stop oil pump priming since
 *        it is started by holding a button that does not signal
 *        when it is released
 * 
 * @param work 
 **************************************************************/
static void testPumpIntlkStop(struct k_work *work){
    sts_testPumpIntlk = false;
}


/***************************************************************
 * @brief Updates the PWM output for the oil pump controller
 * 
 * PWM is used here to vary the frequency of pulses, not for
 * voltage level control
 * 
 **************************************************************/
static void oilPumpPWM(){
    /*
     * TEMPORARILY DISABLED on the ESP32 PLC 21.
     *
     * The original step-pulse PWM scheme (variable period, fixed pulse
     * width) drives a stepper motor controller's STEP input directly.
     * On this hardware the only PWM source available is the PCA9685,
     * which has a single chip-wide period and tops out at ~1.5 kHz.
     * The dripper's nominal step rate (~1.6 kHz at 60 RPM with 1600
     * steps/rev) exceeds that limit, so the driver returns -ENOTSUP
     * and floods the log.
     *
     * This function will be rewritten when the dedicated stepper
     * controller (analog-input variant) is wired in: the PWM channel
     * will then be driven at a fixed frequency with variable duty
     * cycle, with the duty cycle representing the desired motor
     * speed via low-pass filter on Q0.5 (= A0_5).  Until then, no
     * pump output is generated.
     */
}


/***************************************************************
 * @brief Recalls stored variables from memory
 * 
 **************************************************************/
static void varInit(){
    // drip shutdown protection
    storageInitVar(FS_LOW_DRIP_SHDN_ENABLE, &cfg_lowDripShDnEnable, sizeof(cfg_lowDripShDnEnable));
    storageInitVar(FS_LOW_DRIP_SHDN_DELAY, &cfg_lowDripShDnDelay, sizeof(cfg_lowDripShDnDelay));

    // scheduled start
    storageInitVar(FS_SCHED_HOUR, &cfg_schedHour, sizeof(cfg_schedHour));
    storageInitVar(FS_SCHED_MINUTE, &cfg_schedMinute, sizeof(cfg_schedMinute));
    storageInitVar(FS_SCHED_DAYS, &cfg_schedDays, sizeof(cfg_schedDays));

    // aux sensors
    storageInitVar(FS_AUX1_ENABLE, &cfg_aux1Enable, sizeof(cfg_aux1Enable));
    storageInitVar(FS_AUX1_LABEL, &cfg_aux1Label, sizeof(cfg_aux1Label));
    storageInitVar(FS_AUX2_ENABLE, &cfg_aux2Enable, sizeof(cfg_aux2Enable));
    storageInitVar(FS_AUX2_LABEL, &cfg_aux2Label, sizeof(cfg_aux2Label));
    storageInitVar(FS_AUX3_ENABLE, &cfg_aux3Enable, sizeof(cfg_aux3Enable));
    storageInitVar(FS_AUX3_LABEL, &cfg_aux3Label, sizeof(cfg_aux3Label));
}


/***************************************************************
 * @brief Checks the current time to see if the system should
 *        start on schedule. 
 * 
 * @return true if system should start
 * @return false if not scheduled to start
 **************************************************************/
static bool schedCheck(){
    struct timespec current_time = {};
    struct tm *tm_info;

    if (sys_clock_gettime(SYS_CLOCK_REALTIME, &current_time) != 0) {
        return false;
    }

    tm_info = localtime(&current_time.tv_sec);
    if (tm_info == nullptr) {
        return false;
    }

    int currentDay = tm_info->tm_wday;
    int currentHour = tm_info->tm_hour;
    int currentMinute = tm_info->tm_min;
    
    // Compare with scheduled time
    return (cfg_schedDays[currentDay] &&
            currentHour == cfg_schedHour &&
            currentMinute == cfg_schedMinute);
}


/***************************************************************
 * @brief Main loop function for Dripper control, loops at 10ms
 * 
 **************************************************************/
static void dripperMain(){
    int ret;
    utyTimer preOilRunTmr(cfg_preOilMaxTime);
    bool dripBlink = 0;

    // Wait until provisioning is complete before starting dripper operations
    while (provisioning_mode) {
        k_sleep(K_MSEC(500));
    }

    // I/O and storage before the 1 s hardware watchdog — outputsInit() can
    // stall on a missing PCA9685 if we arm WDT first.
    inputsInit();
    outputsInit();
    pidInit();
    varInit();

    ret = wdtInit();
    if (ret == 0) {
        sts_wdtEnabled = true;
        wdt_feed(wdt, wdt_channel_id);
    }

    while(1){
        if (sts_wdtEnabled) {
            wdt_feed(wdt, wdt_channel_id);
        }

        // update inputs
        if (sts_expanderInputsOk) {
            INPUT_DEBOUNCE(inp_mtrRun, 50, 50, false);
            INPUT_DEBOUNCE(inp_lowOil, 50, 50, true);
        }
        //INPUT_DEBOUNCE(inp_aux1, 50, 50, true);
        //INPUT_DEBOUNCE(inp_aux2, 50, 50, true);
        //INPUT_DEBOUNCE(inp_aux3, 50, 50, true);
        
        LOG_STATE_CHANGE(inp_mtrRun);
        LOG_STATE_CHANGE(inp_lowOil);

        // if motor is running, disable manual oiling
        if (inp_mtrRun){
            ocmd_sysRun = false;
        }

        // check scheduled start time to run
        if (!sts_sysRun) {
            pcmd_sysRun = schedCheck();
        }

        // monitor max run time
        preOilRunTmr.enable(ocmd_sysRun || pcmd_sysRun);
        if (preOilRunTmr.done()) {
            ocmd_sysRun = false;
            pcmd_sysRun = false;
        }     

        // system run status change, watchdog must be enabled to run
        sts_sysRun = sts_wdtEnabled && (inp_mtrRun || ocmd_sysRun || pcmd_sysRun);
        LOG_STATE_CHANGE(sts_sysRun);

        // blink off LED light to indicate a drip
        if (k_uptime_get() - val_lastDripTime < 50){
            dripBlink = true;
        } else {
            dripBlink = false;
        }

        // set LEDs, green is on when no alarm, blinks off for a drip
        out_ledGrn = !dripBlink && !alm_active;
        out_ledRed = !dripBlink && alm_active;

        // check alarms
        lowDripRateAlarm(false);
        lowOilAlarm();
        auxAlarms();
        alarmSummary();

        // start/stop pump
        if (sts_oilPumpPriming) {
            dripRateCtrl.SetMode(MANUAL);
            val_pumpSpd = cfg_pumpPrimeRPM;
            val_pumpStepWidth = cfg_pumpStepWidth;
            oilPumpPWM();
        }
        else{
            if (sts_sysRun){
                dripRateCtrl.SetMode(AUTOMATIC);
                val_pumpStepWidth = cfg_pumpStepWidth;
                sts_oilPumpRunning = true;
                if (dripRateCtrl.Compute()){
                    oilPumpPWM();
                    
                    // PID tuning data logging (enable via debug flag)
                    #ifdef CONFIG_PID_TUNING_LOG
                    LOG_INF("{\"timestamp\":%.2f,\"sp\":%.2f,\"pv\":%.2f,\"cv\":%.2f,\"cv_pct\":%.2f,\"kp\":%.2f,\"ki\":%.2f,\"kd\":%.2f}",
                            k_uptime_get()/1000.0,             // timestamp
                            cfg_dripRate,                      // setpoint
                            val_dripRate,                      // process variable
                            val_pumpSpd,                       // control variable
                            val_pumpSpdPct,                    // cv percent
                            cfg_dripRateCtrlKp,                // Kp
                            cfg_dripRateCtrlKi,                // Ki
                            cfg_dripRateCtrlKd);               // Kd
                    #endif
                }
            }
            else{
                dripRateCtrl.SetMode(MANUAL);
                val_pumpSpd = cfg_pumpMinRPM;
                val_pumpStepWidth = 0;
                sts_oilPumpRunning = false;
                oilPumpPWM();
            }
        }

        // convert motor speed to percent for display
        val_pumpSpdPct = val_pumpSpd/cfg_pumpMaxRPM*100;

        // enable motor if no alarms
        out_mtrEnable = !alm_interlock && !sts_testPumpIntlk;

        // write outputs (PCA9685 PWM-as-on/off, see pwmDigitalSet)
        pwmDigitalSet(&out_mtrEnable_dt, out_mtrEnable);
        pwmDigitalSet(&out_ledGrn_dt,    out_ledGrn);
        pwmDigitalSet(&out_ledRed_dt,    out_ledRed);

        CHECK_STACK(cfg_dripperThreadStackSize);

        // Sync live PLC state into Modbus input registers for HMI  // ← ADDED
        modbus_rtu_sync();                                           // ← ADDED

        k_sleep(K_MSEC(10));
    }
}
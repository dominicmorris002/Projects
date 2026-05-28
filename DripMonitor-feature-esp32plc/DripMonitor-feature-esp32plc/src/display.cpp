// *************************************************************
// Includes & Logging
// *************************************************************
#include "display.hpp"

#include <stdint.h>
#include <functional>
#include <vector>
#include <map>
#include <variant>
#include <string>
#include <iomanip>
#include <cmath>

#include <zephyr/device.h>
#include <zephyr/logging/log.h>

#include "../include/LK204.hpp"
#include "../include/utility.hpp"
#include "dripper.hpp"
#include "storage.hpp"
#include "cloud.hpp"
#include "version.hpp"

// External variables
extern bool provisioning_mode;

LOG_MODULE_REGISTER(app_display, CONFIG_APP_DISPLAY_LOG_LEVEL);

// *************************************************************
// Enumerations & Structs
// *************************************************************
enum disp_id {
    DISP_MENU = 0,
    DISP_MAIN,
    DISP_ALARMS,
    DISP_PRIME,
    DISP_SETTINGS,
    DISP_NET_STATUS,
    DISP_ADVANCED,

    // Settings sub items
    DISP_DRIP_SHDN_CONF = 41,

    // Advanced sub items
    DISP_IO = 61,
    DISP_PID

};


// *************************************************************
// Configuration
// *************************************************************
// type         // variable                     // value        // comment
#define         cfg_displayThreadStackSize      8192            // stack size in bytes
#define         cfg_cursor                      0x7E            // cursor Symbol
#define         cfg_screenMinInterval           50              // milliseconds between screen updates
#define         cfg_screenMaxInterval           60000           // milliseconds between screen updates
#define         cfg_dispCols                    20              // LCD display columns
#define         cfg_dispRows                    4               // LCD display rows
#define         cfg_screenTO                    180000          // milliseconds before returning to main screen
#define         cfg_alarmUpdateInterval         1500            // milliseconds between alarm message changes


// *************************************************************
// Local Variables
// *************************************************************
// type             // variable                     // value    // comment
static i2c_dt_spec  lcdDev = I2C_DT_SPEC_GET(DT_NODELABEL(lcd));// devicetree device
static LK204        lcd(lcdDev);                                // lcd display screen object

// type             // variable                     // value    // comment
static uint16_t     dispNum                         = 0;        // current navigation depth, 0 = main menu
static uint8_t      dispLine                        = 0;        // current line index that shows on top of display
static uint8_t      dispPos                         = 0;        // current display position (arrow)
static std::string  str_sysStatus;                              // current system state to display
static std::string  str_alarm;                                  // current alarm string to display
static std::string  str_cloudErr;                               // current error string to display
static std::string  str_networkState;                           // current ethernet/network L4 state to display
static std::string  str_cloudConnStatus;                        // current cloud connection status
static std::string  str_dripShDnEnable;                            // current drip rate shutdown protection status
static double       buf_dripSP;                                 // drip rate setpoint for display to show and modify
static double       buf_dripCtrlKpSP;                           // PID Kp setpoint for display to show and modify
static double       buf_dripCtrlKiSP;                           // PID Ki setpoint for display to show and modify
static double       buf_dripCtrlKdSP;                           // PID Kd setpoint for display to show and modify
static bool         buf_dripShDnEnable;                            // Low drip rate shutdown status to modify
static int          buf_dripShDnDelay;                          // Low drip rate shutdown delay to show and modify, minutes
static std::string  str_version = Version::getVersion();        // program version number
static bool         sts_displayOk;                              // false if LCD I2C is absent

// Nav buttons
// type             // variable                     // value    // comment
static uint8_t      btnPress;                                   // lcd display buttons
static bool         sts_advMode                     = 0;        // enable advanced mode
static uint8_t      wrk_advMode                     = 0;        // allow enabling adv mode with 5 consecutive presses


// *************************************************************
// ValueWrapper Class Definition
// *************************************************************
class ValueWrapper {
private:
    std::variant<std::reference_wrapper<bool>, std::reference_wrapper<int>, std::reference_wrapper<double>, std::reference_wrapper<std::string>> value;
    int decimals;  // New member to store decimal places

public:
    ValueWrapper(bool& v, int dec = 0);
    ValueWrapper(int& v, int dec = 0);
    ValueWrapper(double& v, int dec = 1);      // Default 1 decimal for double
    ValueWrapper(std::string& v, int dec = 0);

    std::string str() const;
};

ValueWrapper::ValueWrapper(bool& v, int dec) : value(std::ref(v)), decimals(dec) {}

ValueWrapper::ValueWrapper(int& v, int dec) : value(std::ref(v)), decimals(dec) {}

ValueWrapper::ValueWrapper(double& v, int dec) : value(std::ref(v)), decimals(dec) {}

ValueWrapper::ValueWrapper(std::string& v, int dec) : value(std::ref(v)), decimals(dec) {}

std::string ValueWrapper::str() const {
    return std::visit([this](const auto& arg) -> std::string {
        std::ostringstream oss;
        if constexpr (std::is_same_v<decltype(arg.get()), double&>) {
            oss << std::fixed << std::setprecision(decimals) << arg.get();
        } else {
            oss << arg.get();            
        }
        return oss.str();
    }, value);
}

// *************************************************************
// Helper Factory Functions for ValueWrapper
// *************************************************************
auto VW0(double& v) { return ValueWrapper(v, 0); }  // 0 decimals
auto VW1(double& v) { return ValueWrapper(v, 1); }  // 1 decimal
auto VW2(double& v) { return ValueWrapper(v, 2); }  // 2 decimals
auto VW(auto& v) { return ValueWrapper(v); }          // default

// *************************************************************
// MenuItem Definition
// *************************************************************
struct MenuItem {
    std::vector<std::string>                text;
    std::vector<ValueWrapper>               appendVar;
    std::vector<ValueWrapper>               rightVar;
    std::vector<std::function<void()>>      SelectFn;
    std::vector<std::function<void()>>      LeftFn;
    std::vector<std::function<void()>>      RightFn;

    MenuItem();
    MenuItem(const std::vector<std::string>&                _text,
             const std::vector<ValueWrapper>&               _appendVar,
             const std::vector<ValueWrapper>&               _rightVar,
             const std::vector<std::function<void()>>&      _SelectFn,
             const std::vector<std::function<void()>>&      _LeftFn,
             const std::vector<std::function<void()>>&      _RightFn);
};

MenuItem::MenuItem() = default;

MenuItem::MenuItem(const std::vector<std::string>&                _text,
                   const std::vector<ValueWrapper>&               _appendVar,
                   const std::vector<ValueWrapper>&               _rightVar,
                   const std::vector<std::function<void()>>&      _SelectFn,
                   const std::vector<std::function<void()>>&      _LeftFn,
                   const std::vector<std::function<void()>>&      _RightFn)
    : text(_text), appendVar(_appendVar), rightVar(_rightVar),
    SelectFn(_SelectFn), LeftFn(_LeftFn), RightFn(_RightFn)  {}


// *************************************************************
// Static Functions
// *************************************************************
static void noop();
static void displayInit();
static void show(uint8_t num);
static void saveSettings();
static void togDripShutdown(bool confirm);
static void saveTunings();
static uint8_t getMenuSize();
static void updateScreen(bool forceUpdate);
static void updateLEDs();
static void displayMain();


// *************************************************************
// Thread
// *************************************************************
#ifdef CONFIG_ENABLE_DISPLAY
K_THREAD_DEFINE(displayThread, cfg_displayThreadStackSize, displayMain, NULL, NULL, NULL, 5, 0, 1500);
#endif

// *************************************************************
// Macro Functions
// *************************************************************
// macro function to modify variables with an upper/lower bound
#define INC(var, step, max)     (var = var + step > max ? max : var + step)
#define DEC(var, step, min)     (var = var - step < min ? min : var - step)


// Menu Setup
static std::string nullstr = ""; //empty string for constructing menu variables
static const std::map<int, MenuItem> menuMap = {
    // **Menu**
    {DISP_MENU,MenuItem(
        {"Main",                        "Alarms",                       "Prime Pump",                   "Settings",                     "Network",                      "Advanced"                      },
        {VW(nullstr),                   VW(nullstr),                    VW(nullstr),                    VW(nullstr),                    VW(nullstr),                    VW(nullstr)                     },
        {VW(nullstr),                   VW(nullstr),                    VW(nullstr),                    VW(nullstr),                    VW(nullstr),                    VW(nullstr)                     },
        {std::bind(show,DISP_MAIN),     std::bind(show,DISP_ALARMS),    std::bind(show,DISP_PRIME),     std::bind(show,DISP_SETTINGS),  std::bind(show,DISP_NET_STATUS),std::bind(show,DISP_ADVANCED)   },
        {&noop,                         &noop,                          &noop,                          &noop,                          &noop,                          &noop                           },
        {&noop,                         &noop,                          &noop,                          []() { wrk_advMode++; },        &noop,                          &noop                           }
    )},

    // **Main**
    {DISP_MAIN,MenuItem(
        {"Rate: ",                      "Set:  ",                       "",                             "Menu"                          },
        {VW1(val_dripRate),             VW0(cfg_dripRate),              VW(str_alarm),                  VW(nullstr)                     },
        {VW(str_sysStatus),             VW(nullstr),                    VW(nullstr),                    VW(nullstr)                     },
        {&noop,                         &noop,                          std::bind(show,DISP_ALARMS),    std::bind(show,DISP_MENU)       },
        {&noop,                         &noop,                          &noop,                          &noop,                          },
        {&noop,                         &noop,                          &noop,                          &noop,                          }
    )},

    // **Alarms**
    {DISP_ALARMS,MenuItem(
        {"Active Alarm: ",              " ",                            "Reset System",                 "Back"                          },
        {VW(nullstr),                   VW(str_alarm),                  VW(nullstr),                    VW(nullstr)                     },
        {VW(nullstr),                   VW(nullstr),                    VW(nullstr),                    VW(nullstr)                     },
        {&noop,                         &noop,                          &resetAlarms,                   std::bind(show,DISP_MENU)       },
        {&noop,                         &noop,                          &noop,                          &noop,                          },
        {&noop,                         &noop,                          &noop,                          &noop,                          }
    )},

    // **Prime Pump**
    {DISP_PRIME,MenuItem(
        {"Hold Sel to Prime",           "Back"                          },
        {VW(nullstr),                   VW(nullstr)                     },
        {VW(nullstr),                   VW(nullstr)                     },
        {&oilPumpPrime,                 std::bind(show,DISP_MENU)       },
        {&noop,                         &noop                           },
        {&noop,                         &noop                           }
    )},

    // **Settings**
    {DISP_SETTINGS,MenuItem(
        {"Drip Rate: ",                 "LoDripShutdown: ",             "LoDripShDnTime: ",              "Save",                         "Back"                          },
        {VW0(buf_dripSP),               VW(str_dripShDnEnable),         VW(buf_dripShDnDelay),           VW(nullstr),                    VW(nullstr)                     },
        {VW(nullstr),                   VW(nullstr),                    VW(nullstr),                     VW(nullstr),                    VW(nullstr)                     },
        {&noop,                         &noop,                          &noop,                           &saveSettings,                  std::bind(show,DISP_MENU)       },
        {[](){DEC(buf_dripSP,1,5);},    std::bind(togDripShutdown,0),   [](){DEC(buf_dripShDnDelay,5,5);},   &noop,                      &noop                           },
        {[](){INC(buf_dripSP,1,50);},   std::bind(togDripShutdown,0),   [](){INC(buf_dripShDnDelay,5,240);}, &noop,                      &noop                           }
    )},

    // **Pump Protection Confirmation**
    {DISP_DRIP_SHDN_CONF,MenuItem(
        {"Warning! Pump shut-",         " down protection",             " will be disabled",            "Continue",                     "Cancel"                        },
        {VW(nullstr),                   VW(nullstr),                    VW(nullstr),                    VW(nullstr),                    VW(nullstr)                     },
        {VW(nullstr),                   VW(nullstr),                    VW(nullstr),                    VW(nullstr),                    VW(nullstr)                     },
        {&noop,                         &noop,                          &noop,                          std::bind(togDripShutdown,1),   std::bind(show,DISP_SETTINGS)   },
        {&noop,                         &noop,                          &noop,                          &noop,                          &noop                           },
        {&noop,                         &noop,                          &noop,                          &noop,                          &noop                           }
    )},

    // **Cloud Status**
    {DISP_NET_STATUS,MenuItem(
        {"Cloud: ",                     " ",                            " ",                            "Back"                          },
        {VW(str_cloudConnStatus),       VW(str_networkState),           VW(str_cloudErr),               VW(nullstr)                     },
        {VW(nullstr),                   VW(nullstr),                    VW(nullstr),                    VW(nullstr)                     },
        {&noop,                         &noop,                          &noop,                          std::bind(show,DISP_MENU)       },
        {&noop,                         &noop,                          &noop,                          &noop                           },
        {&noop,                         &noop,                          &noop,                          &noop                           }
    )},

    // **Advanced**
    {DISP_ADVANCED,MenuItem(
        {"I/O",                         "PID",                          "Hold Sel test Intlk",          "",                             "Back"                          },
        {VW(nullstr),                   VW(nullstr),                    VW(nullstr),                    VW(str_version),                VW(nullstr)                     },
        {VW(nullstr),                   VW(nullstr),                    VW(nullstr),                    VW(nullstr),                    VW(nullstr)                     },
        {std::bind(show,DISP_IO),       std::bind(show,DISP_PID),       &testPumpIntlk,                 &noop,                          std::bind(show,DISP_MENU)       },
        {&noop,                         &noop,                          &noop,                          &noop,                          &noop,                          },
        {&noop,                         &noop,                          &noop,                          &noop,                          &noop,                          }
    )},

    // **Advanced - I/O**
    {DISP_IO,MenuItem(
        {"I-Low Oil Lvl",               "I-Mtr Running",                "O-Mtr Enable",                 "Back"                          },
        {VW(nullstr),                   VW(nullstr),                    VW(nullstr),                    VW(nullstr)                     },
        {VW(inp_lowOil),                VW(inp_mtrRun),                 VW(out_mtrEnable),              VW(nullstr)                     },
        {&noop,                         &noop,                          &noop,                          std::bind(show,DISP_ADVANCED)   },
        {&noop,                         &noop,                          &noop,                          &noop,                          },
        {&noop,                         &noop,                          &noop,                          &noop,                          }
    )},

    // **Advanced - PID**
    {DISP_PID,MenuItem(
        {"SP: ",                        "PV: ",                         "CV: ",                         "Kp: ",                                 "Ki: ",                                 "Kd: ",                                 "Save Tunings",                 "Back"                          },
        {VW0(cfg_dripRate),             VW1(val_dripRate),              VW1(val_pumpSpdPct),            VW2(buf_dripCtrlKpSP),                  VW2(buf_dripCtrlKiSP),                  VW2(buf_dripCtrlKdSP),                  VW(nullstr),                    VW(nullstr)                     },
        {VW(nullstr),                   VW(nullstr),                    VW(nullstr),                    VW(nullstr),                            VW(nullstr),                            VW(nullstr),                            VW(nullstr),                    VW(nullstr)                     },
        {&noop,                         &noop,                          &noop,                          &noop,                                  &noop,                                  &noop,                                  &saveTunings,                   std::bind(show,DISP_ADVANCED)   },
        {&noop,                         &noop,                          &noop,                          [](){DEC(buf_dripCtrlKpSP,0.01,0);},    [](){DEC(buf_dripCtrlKiSP,0.01,0);},    [](){DEC(buf_dripCtrlKdSP,0.01,0);},    &noop,                          &noop,                          },
        {&noop,                         &noop,                          &noop,                          [](){INC(buf_dripCtrlKpSP,0.01,10);},   [](){INC(buf_dripCtrlKiSP,0.01,10);},   [](){INC(buf_dripCtrlKdSP,0.01,10);},   &noop,                          &noop,                          }
    )}
                        
};


/***************************************************************
 * @brief No operation function, placeholder for display
 *        function calls
 * 
 **************************************************************/
static void noop(){}


/***************************************************************
 * @brief Initalize display
 * 
 **************************************************************/
static void displayInit(){
    sts_displayOk = false;
    if (!device_is_ready(lcdDev.bus)) {
        LOG_WRN("LCD I2C bus not ready — local display disabled");
        return;
    }

    sts_displayOk = true;
    // display setup
    lcd.init(20,4);
    lcd.setAutoBacklight(LK204::OMIT_LIGHT_BOTH);
    lcd.displayBacklightOnTime(5);
    lcd.setAutoRepeatMode(LK204::TYPEMATIC);
    lcd.setStartupScreen("                          FLUXION         KL TECHNOLOGIES");
    lcd.setLEDStartup(1, LK204::LED_YELLOW);
    lcd.setLEDStartup(2, LK204::LED_YELLOW);
    lcd.setLEDStartup(3, LK204::LED_YELLOW);
    lcd.setLED(1, LK204::LED_GREEN);
}


/***************************************************************
 * @brief Change screen number to display and perform any screen
 *        specific on-show actions
 * 
 * @param num Screen number to change to
 **************************************************************/
static void show(uint8_t num){
    dispNum = num;
    dispLine = 0;
    dispPos = 0;

    // on-show screen specific changes
    switch (dispNum){
        case DISP_SETTINGS:
            buf_dripSP = cfg_dripRate;
            buf_dripShDnEnable = cfg_lowDripShDnEnable;
            str_dripShDnEnable = buf_dripShDnEnable ? "On" : "Off";
            buf_dripShDnDelay = cfg_lowDripShDnDelay;
            break;
        case DISP_PID:
            buf_dripCtrlKpSP = cfg_dripRateCtrlKp;
            buf_dripCtrlKiSP = cfg_dripRateCtrlKi;
            buf_dripCtrlKdSP = cfg_dripRateCtrlKd;
        default:
            break;
    }
}


/***************************************************************
 * @brief Check settings variables for changes and save to
 *        controller variables and flash memory
 * 
 **************************************************************/
static void saveSettings(){
    if (cfg_dripRate != buf_dripSP) {
        cfg_dripRate = buf_dripSP;
        storageWrite(FS_DRIP_RATE_SP, &cfg_dripRate, sizeof(cfg_dripRate));
    }
    if (cfg_lowDripShDnEnable != buf_dripShDnEnable) {
        cfg_lowDripShDnEnable = buf_dripShDnEnable;
        storageWrite(FS_LOW_DRIP_SHDN_ENABLE, &cfg_lowDripShDnEnable, sizeof(cfg_lowDripShDnEnable));
    }
    if (cfg_lowDripShDnDelay != buf_dripShDnDelay) {
        cfg_lowDripShDnDelay = buf_dripShDnDelay;
        storageWrite(FS_LOW_DRIP_SHDN_DELAY, &cfg_lowDripShDnDelay, sizeof(cfg_lowDripShDnDelay));
    }
}


/***************************************************************
 * @brief Check PID tuning variables for changes and save to
 *        controller variables and flash memory
 * 
 **************************************************************/
static void saveTunings(){
    if (cfg_dripRateCtrlKp != buf_dripCtrlKpSP) {
        cfg_dripRateCtrlKp = buf_dripCtrlKpSP;
        storageWrite(FS_DRIP_RATE_CTRL_KP, &cfg_dripRateCtrlKp, sizeof(cfg_dripRateCtrlKp));
    }
    if (cfg_dripRateCtrlKi != buf_dripCtrlKdSP) {
        cfg_dripRateCtrlKi = buf_dripCtrlKiSP;
        storageWrite(FS_DRIP_RATE_CTRL_KI, &cfg_dripRateCtrlKi, sizeof(cfg_dripRateCtrlKi));
    }
    if (cfg_dripRateCtrlKd != buf_dripCtrlKdSP) {
        cfg_dripRateCtrlKd = buf_dripCtrlKdSP;
        storageWrite(FS_DRIP_RATE_CTRL_KD, &cfg_dripRateCtrlKd, sizeof(cfg_dripRateCtrlKd));
    }
    pidSetTunings();
}


static void togDripShutdown(bool confirm){
    if (buf_dripShDnEnable) {
        if (confirm) {
            buf_dripShDnEnable = false;
            str_dripShDnEnable = "Off";
            dispNum = DISP_SETTINGS;
        } else {
            dispNum = DISP_DRIP_SHDN_CONF;
        }
    } else {
        buf_dripShDnEnable = true;
        str_dripShDnEnable = "On";
    }
}


/***************************************************************
 * @brief Get the size of the menu being displayed, handles
 *        removing hidden screens
 * 
 * @return uint8_t size of screen menu being displayed
 **************************************************************/
static uint8_t getMenuSize(){
    uint8_t menuSize = menuMap.at(dispNum).text.size();

    //subtract 1 to hide Advanced screen
    if (!sts_advMode && dispNum == 0) {
        menuSize--;
        if (wrk_advMode >= 5){
            sts_advMode = true;
        }

        // clear adv mode countner if move off Settings
        if (dispLine+dispPos != 3){
            wrk_advMode = 0;                  
        }
    }

    return menuSize; 
}


/***************************************************************
 * @brief Cycles through active alarm messages for display based
 *        on set interval
 * 
 **************************************************************/
static void getAlarmMsg(){
    static int index = 0;
    static uint64_t lastUpdate;

    // clear index if no alarm active
    if (!alm_active) {
        index = 0;
        str_alarm = "";
        return;
    }

    // check last update time
    uint64_t uptime = k_uptime_get();
    if (uptime < (lastUpdate + cfg_alarmUpdateInterval)){
        return;
    } else {
        lastUpdate = uptime;
    }

    // loop at most two times to re-evaluate list after the end is reached, break out if new message is set
    for (int i=0; i<2; i++){
        // setup alarm status string
        if      (index < 1 && alm_sysFault)             {index = 1; str_alarm = "System Fault"; break;}
        // lowDripeRateShutdown and Warning share the same index so only one of the two messages is shown
        else if (index < 2 && alm_lowDripRateShDn)  {index = 2; str_alarm = "Low Drip Shutdown"; break;}
        else if (index < 2 && alm_lowDripRateWar)   {index = 2; str_alarm = "Low Drip Warning"; break;} 
        else if (index < 3 && alm_lowOil)               {index = 3; str_alarm = "Low Oil"; break;}
        else if (index < 4 && alm_aux1)                 {index = 4; str_alarm = cfg_aux1Label; break;}
        else if (index < 5 && alm_aux2)                 {index = 5; str_alarm = cfg_aux2Label; break;}
        else if (index < 6 && alm_aux3)                 {index = 6; str_alarm = cfg_aux3Label; break;}
        else if (index == 0)                            {str_alarm = ""; break;}
        else                                            {index = 0;}
    }
}


/***************************************************************
 * @brief Gets cloud/network status for display
 * 
 **************************************************************/
void getCloudStatus(){
    switch(val_cloudConnStatus){
        case 0:
            str_cloudConnStatus = "Disonnected";
            break;
        case 1:
            str_cloudConnStatus = "Connecting";
            break;
        case 2:
            str_cloudConnStatus = "Connected";
            break;
        default:
            str_cloudConnStatus = "Unknown " + std::to_string(val_cloudConnStatus);
            break;
    }


    str_networkState = val_networkConnected ? "Connected" : "Disconnected";
}


/***************************************************************
 * @brief Function takes information, checks if it has changed
 *        since last update and updates the screen if so. A
 *        minimum time to between updates is applied as well
 * 
 * @param forceUpdate force screen update
 **************************************************************/
static void updateScreen(bool forceUpdate){

    bool                updateDisp = forceUpdate;
    uint8_t             rightTextLen;
    static int64_t      screenLastUpdate;
    int64_t             uptime = k_uptime_get();
    static uint8_t      dispPosCache;
    uint8_t             i;
    uint8_t             menuSize = getMenuSize();
    uint8_t             dispRows = (menuSize < cfg_dispRows) ? menuSize : cfg_dispRows;
    
    // screen display variables
    std::vector<std::string> leftText, rightText;
    leftText.resize(cfg_dispRows);
    rightText.resize(cfg_dispRows);

    // cache display variables for next run
    static std::vector<std::string> leftTextCache, rightTextCache;
    if (leftTextCache.size() != cfg_dispRows) {
        leftTextCache.resize(cfg_dispRows);
    }
    if (rightTextCache.size() != cfg_dispRows) {
        rightTextCache.resize(cfg_dispRows);
    }

    // screen specific string variables
    switch(dispNum){
        case DISP_MAIN:
            if (alm_active) {str_sysStatus = "ALM";} 
            else if (sts_sysRun) {str_sysStatus = "ON";}
            else {str_sysStatus = "OFF";}
            getAlarmMsg();
            break;
        case DISP_ALARMS:
            getAlarmMsg();
            break;
        case DISP_NET_STATUS:
            getCloudStatus();
            break;
    }
    
    // construct current screen info
    for (i = 0; i < dispRows; i++){
        leftText[i] = menuMap.at(dispNum).text[dispLine + i] + menuMap.at(dispNum).appendVar[dispLine + i].str();
        rightText[i] = menuMap.at(dispNum).rightVar[dispLine + i].str();
    }

    if (dispPos != dispPosCache){
        updateDisp = true;
    }

    // check for screen changes
    for (i = 0; i < dispRows && !updateDisp; i++){
        if (leftText[i] != leftTextCache[i]){
            updateDisp = true;
        }
        else if (rightText[i] != rightTextCache[i]){
            updateDisp = true;
        }
    }

    // enforce time based limits
    if (((uptime - screenLastUpdate) < cfg_screenMinInterval && !forceUpdate)) {
        updateDisp = false;
    } else if ((uptime - screenLastUpdate) > cfg_screenMaxInterval) {
        updateDisp = true;
    }

    // update screen if needed
    if (updateDisp) {
        lcd.clear();
        
        for (int i = 0; i < dispRows; i++){
            // Left justified text
            lcd.setCursor(1, i);
            lcd.write(leftText[i]);

            // Right justified text
            rightTextLen = rightText[i].length();
            if (rightTextLen > 0){
                lcd.setCursor(cfg_dispCols-rightTextLen, i);
                lcd.write(rightText[i]);
            }
        }
        
        // display cursor arrow
        lcd.setCursor(0, dispPos);
        lcd.write(cfg_cursor);
        dispPosCache = dispPos;

        // cache values for next update
        dispPosCache = dispPos;
        leftTextCache = leftText;
        rightTextCache = rightText;
        screenLastUpdate = uptime;
    }
}


/***************************************************************
 * @brief Checks variables and changes LEDs as needed
 * 
 **************************************************************/
static void updateLEDs(){
    static bool sts_sysRunPrev = !sts_sysRun;
    static bool alm_activePrev = !alm_active;
    bool ledChange = false;

    if (sts_sysRun != sts_sysRunPrev){
        lcd.setLED(2, sts_sysRun ? LK204::LED_GREEN : LK204::LED_OFF);
        sts_sysRunPrev = sts_sysRun;
        ledChange = true;
    }

    if (alm_active != alm_activePrev){
        lcd.setLED(3, alm_active ? LK204::LED_RED : LK204::LED_OFF);
        alm_activePrev = alm_active;
        ledChange = true;
    }

    // force screen update to clear any display issues changing GPIOs may have caused
    if (ledChange) {
        k_sleep(K_MSEC(100));
        updateScreen(true);
    }
    
}


/***************************************************************
 * @brief Main looping function for display, loops at 100ms
 * 
 **************************************************************/
static void displayMain(){
    int64_t btnLastPress = 0;
    int64_t uptime = 0;
    uint8_t menuSize = 0;

    // Wait until provisioning is complete before starting display operations
    while (provisioning_mode) {
        k_sleep(K_MSEC(500));
    }

    displayInit();

    while(1){
        if (!sts_displayOk) {
            k_sleep(K_SECONDS(1));
            continue;
        }

        uptime = k_uptime_get();

        menuSize = getMenuSize();

        // process LCD screen buttons
        btnPress = lcd.readButtons();
        if (btnPress > 0) {
            LOG_DBG("Button press value: %d", btnPress);
        }
        if (btnPress != LK204::NONE) {
            btnLastPress = uptime;

            switch (btnPress){
                case LK204::BTN_UP:
                    if      (dispPos > 0)   {dispPos--;}
                    else if (dispLine > 0)  {dispLine--;}
                    break;
                case LK204::BTN_DOWN:
                    if      (dispPos < cfg_dispRows - 1 && dispPos < menuSize - 1)  {dispPos++;}
                    else if (dispLine < menuSize - cfg_dispRows)                    {dispLine++;}
                    break;
                case LK204::BTN_CTRSELECT:
                    menuMap.at(dispNum).SelectFn[dispLine+dispPos]();
                    break;
                case LK204::BTN_LEFT:
                    menuMap.at(dispNum).LeftFn[dispLine+dispPos]();
                    break;
                case LK204::BTN_RIGHT:
                    menuMap.at(dispNum).RightFn[dispLine+dispPos]();
                    break;
            }
        }
        // if no button presses return to main screen
        else if (uptime > btnLastPress + cfg_screenTO) {
            show(DISP_MAIN);
        }

        updateScreen(false);
        updateLEDs();
        CHECK_STACK(cfg_displayThreadStackSize);
        k_sleep(K_MSEC(100));
    }
}

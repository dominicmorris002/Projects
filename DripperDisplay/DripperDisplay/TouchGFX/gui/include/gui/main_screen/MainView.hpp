#ifndef MAINVIEW_HPP
#define MAINVIEW_HPP

#include <gui_generated/main_screen/MainViewBase.hpp>
#include <gui/main_screen/MainPresenter.hpp>
#include "modbus_master.h"                // FIX: include before it's used in declaration

class MainView : public MainViewBase
{
public:
    MainView();
    virtual ~MainView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();

    virtual void PrimeOilerPress();
    virtual void RunOilerPress();

    void handlePollResult(const MB_PollResult &result);  // FIX: inside the class

    void updateDripRate(int value);
    void updateSetpoint(int value);
    void updateStatusText(const char* text);
    void updateAlarmText(const char* text);
    void updateNetworkText(const char* text);

protected:
    bool oilerRunning;
    int  dripRate;
    int  setpoint;
};

#endif // MAINVIEW_HPP
#ifndef MAINVIEW_HPP
#define MAINVIEW_HPP

#include <gui_generated/main_screen/MainViewBase.hpp>
#include <gui/main_screen/MainPresenter.hpp>
#include "modbus_master.hpp"

class MainView : public MainViewBase
{
public:
    MainView();
    virtual ~MainView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();
    virtual void handleTickEvent();
    virtual void handleClickEvent(const touchgfx::ClickEvent &evt);

    virtual void PrimeOilerPress();
    virtual void RunOilerPress();
    virtual void SetpointPress();

    void handlePollResult(const MB_PollResult &result);

    void updateDripRateTenths(uint16_t tenths);
    void updateSetpoint(int dpm);
    void updateStatusText(const char *text);
    void updateAlarmText(uint16_t alarms);
    void updateNetworkText(bool connected);

protected:
    void applyPollResult(const MB_PollResult &result);
    void onSettingsButtonPressed(const touchgfx::AbstractButton &src);

    touchgfx::Callback<MainView, const touchgfx::AbstractButton &> settingsButtonCallback;

    bool     oilerRunning;
    uint16_t dripRateTenths;
    int      setpointDpm;
    uint16_t lastAlarms;
    bool     networkShown;
    bool     alarmBellVisible;
    uint32_t lastPrimeSendMs;

    static const uint16_t ALARM_LINE_SIZE   = 20;
    static const uint16_t NETWORK_LINE_SIZE = 20;
    touchgfx::Unicode::UnicodeChar alarmLineBuffer[ALARM_LINE_SIZE];
    touchgfx::Unicode::UnicodeChar networkLineBuffer[NETWORK_LINE_SIZE];

    char    lastStatusBuf[16];
    uint8_t commFailStreak;
};

#endif // MAINVIEW_HPP

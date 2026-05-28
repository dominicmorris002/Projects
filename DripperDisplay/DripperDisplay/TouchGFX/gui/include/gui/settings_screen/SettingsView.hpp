#ifndef SETTINGSVIEW_HPP
#define SETTINGSVIEW_HPP

#include <gui_generated/settings_screen/SettingsViewBase.hpp>
#include <gui/settings_screen/SettingsPresenter.hpp>

class SettingsView : public SettingsViewBase
{
public:
    SettingsView();
    virtual ~SettingsView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();
    virtual void handleTickEvent();

    virtual void SavePress();
    virtual void DripRateMinusPress();
    virtual void DripRatePlusPress();
    virtual void ShutdownDelayMinusPress();
    virtual void ShutdownDelayPlusPress();
    virtual void PumpShutdownPress();

    void loadFromPlc();

private:
    int dripRateSetpoint;
    int shutdownDelay;
    bool pumpShutdownEnabled;
    int savedTicksRemaining;

    void updateDripRateDisplay();
    void updateShutdownDelayDisplay();
    void updateToggleDisplay();
};

#endif
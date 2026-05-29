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
    void refreshFromPlc();
    void applyCachedSettings();

private:
    int dripRateSetpoint;
    int shutdownDelay;
    bool pumpShutdownEnabled;
    int savedTicksRemaining;
    int lastDisplayedDrip;
    int lastDisplayedDelay;

    void updateDripRateDisplay();
    void updateShutdownDelayDisplay();
    void updateToggleDisplay();
    void onBackButtonPressed(const touchgfx::AbstractButton &src);

    touchgfx::Callback<SettingsView, const touchgfx::AbstractButton &> backButtonCallback;
};

#endif
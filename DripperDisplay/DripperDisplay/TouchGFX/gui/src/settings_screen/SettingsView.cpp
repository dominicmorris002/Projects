#include <gui/settings_screen/SettingsView.hpp>
#include <gui/common/FrontendApplication.hpp>
#include <images/BitmapDatabase.hpp>
#include <touchgfx/Unicode.hpp>
#include "hmi_settings_cache.hpp"

#ifdef TARGET_STM32
#include "modbus_master.hpp"
#  include "stm32u5xx_hal.h"
#endif

static const int SAVED_DISPLAY_TICKS = 45;

SettingsView::SettingsView()
    : dripRateSetpoint(20),
      shutdownDelay(5),
      pumpShutdownEnabled(true),
      savedTicksRemaining(0),
      lastDisplayedDrip(-1),
      lastDisplayedDelay(-1)
{
}

void SettingsView::setupScreen()
{
    SettingsViewBase::setupScreen();

    toggleButton1.setBitmaps(
        touchgfx::Bitmap(BITMAP_OFFBUTTON_ID),
        touchgfx::Bitmap(BITMAP_ONBUTTON_ID)
    );

    refreshFromPlc();
    savedTicksRemaining = 0;
}

void SettingsView::tearDownScreen()
{
    SettingsViewBase::tearDownScreen();
}

void SettingsView::applyCachedSettings()
{
    int drip = 0;
    int delay = 0;
    bool pump = true;

    if (hmi_settings_load(&drip, &delay, &pump)) {
        dripRateSetpoint    = drip;
        shutdownDelay       = delay;
        pumpShutdownEnabled = pump;
    }
}

void SettingsView::refreshFromPlc()
{
#ifdef TARGET_STM32
    if (hmi_settings_consume_protection_off()) {
        pumpShutdownEnabled = false;
    }
#endif

    applyCachedSettings();
    loadFromPlc();
    lastDisplayedDrip = -1;
    lastDisplayedDelay = -1;
    updateDripRateDisplay();
    updateShutdownDelayDisplay();
    updateToggleDisplay();
}

void SettingsView::loadFromPlc()
{
#ifdef TARGET_STM32
    MB_PollResult r = {};
    bool gotPlc = false;

    for (int attempt = 0; attempt < 4; attempt++) {
        mb_pause_for_ms(350);
        HAL_Delay(30);
        r = mb_poll();
        if (r.ok) {
            gotPlc = true;
            break;
        }
    }

    if (!gotPlc) {
        applyCachedSettings();
        return;
    }

    if (r.drip_rate_sp >= 50 && r.drip_rate_sp <= 500) {
        dripRateSetpoint = r.drip_rate_sp / 10;
    }
    if (r.low_drip_shdn_delay <= 999 && r.low_drip_shdn_delay >= 1) {
        shutdownDelay = (int)r.low_drip_shdn_delay;
    }
    pumpShutdownEnabled = (r.low_drip_shdn_en != 0);

    hmi_settings_save(dripRateSetpoint, shutdownDelay, pumpShutdownEnabled);
#endif
}

void SettingsView::handleTickEvent()
{
    if (savedTicksRemaining > 0) {
        savedTicksRemaining--;

        if (savedTicksRemaining == 0) {
            application().gotoMainScreenNoTransition();
        }
    }
}

void SettingsView::DripRateMinusPress()
{
    if (dripRateSetpoint > 5) {
        dripRateSetpoint--;
    }
    lastDisplayedDrip = -1;
    updateDripRateDisplay();
}

void SettingsView::DripRatePlusPress()
{
    if (dripRateSetpoint < 50) {
        dripRateSetpoint++;
    }
    lastDisplayedDrip = -1;
    updateDripRateDisplay();
}

void SettingsView::updateDripRateDisplay()
{
    if (dripRateSetpoint == lastDisplayedDrip) {
        return;
    }
    lastDisplayedDrip = dripRateSetpoint;

    Unicode::snprintf(Number_3Buffer, NUMBER_3_SIZE, "%d", dripRateSetpoint);
    Number_3.invalidate();
}

void SettingsView::ShutdownDelayMinusPress()
{
    if (shutdownDelay > 1) {
        shutdownDelay--;
    }
    lastDisplayedDelay = -1;
    updateShutdownDelayDisplay();
}

void SettingsView::ShutdownDelayPlusPress()
{
    if (shutdownDelay < 999) {
        shutdownDelay++;
    }
    lastDisplayedDelay = -1;
    updateShutdownDelayDisplay();
}

void SettingsView::updateShutdownDelayDisplay()
{
    if (shutdownDelay == lastDisplayedDelay) {
        return;
    }
    lastDisplayedDelay = shutdownDelay;

    Unicode::snprintf(Number_2_1Buffer, NUMBER_2_1_SIZE, "%d", shutdownDelay);
    Number_2_1.invalidate();
}

void SettingsView::PumpShutdownPress()
{
    if (pumpShutdownEnabled) {
        updateToggleDisplay();
        static_cast<FrontendApplication &>(application())
            .gotoProtection_Disable_Confirmation_ScreenNoTransition();
        return;
    }

    pumpShutdownEnabled = true;
    updateToggleDisplay();
}

void SettingsView::updateToggleDisplay()
{
    toggleButton1.forceState(pumpShutdownEnabled);
    toggleButton1.invalidate();
}

void SettingsView::SavePress()
{
#ifdef TARGET_STM32
    mb_pause_for_ms(3000);

    bool ok = true;

    if (!mb_set_drip_rate((float)dripRateSetpoint)) {
        ok = false;
    }
    if (!mb_write_holding_u16(MB_HOLD_LOW_DRIP_SHDN_EN,
                              pumpShutdownEnabled ? 1u : 0u)) {
        ok = false;
    }
    if (shutdownDelay < 1) {
        shutdownDelay = 1;
    }
    if (!mb_write_holding_u16(MB_HOLD_LOW_DRIP_SHDN_DELAY,
                              (uint16_t)shutdownDelay)) {
        ok = false;
    }

    if (!ok) {
        return;
    }

    hmi_settings_save(dripRateSetpoint, shutdownDelay, pumpShutdownEnabled);
    HAL_Delay(50);
    loadFromPlc();
    lastDisplayedDrip = -1;
    lastDisplayedDelay = -1;
    updateDripRateDisplay();
    updateShutdownDelayDisplay();
    updateToggleDisplay();
#endif

    savedTicksRemaining = SAVED_DISPLAY_TICKS;
}

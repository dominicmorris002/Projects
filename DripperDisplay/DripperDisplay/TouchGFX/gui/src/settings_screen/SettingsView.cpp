#include <gui/settings_screen/SettingsView.hpp>
#include <gui/common/FrontendApplication.hpp> /* protection screen navigation */
#include <images/BitmapDatabase.hpp>
#include <touchgfx/Unicode.hpp>

#ifdef TARGET_STM32
#include "modbus_master.hpp"
#endif

static const int SAVED_DISPLAY_TICKS = 60;

SettingsView::SettingsView()
    : dripRateSetpoint(20),
      shutdownDelay(5),
      pumpShutdownEnabled(true),
      savedTicksRemaining(0)
{
}

void SettingsView::setupScreen()
{
    SettingsViewBase::setupScreen();

    toggleButton1.setBitmaps(
        touchgfx::Bitmap(BITMAP_OFFBUTTON_ID),
        touchgfx::Bitmap(BITMAP_ONBUTTON_ID)
    );

    loadFromPlc();
    savedTicksRemaining = 0;

    updateDripRateDisplay();
    updateShutdownDelayDisplay();
    updateToggleDisplay();
}

void SettingsView::tearDownScreen()
{
    SettingsViewBase::tearDownScreen();
}

void SettingsView::loadFromPlc()
{
#ifdef TARGET_STM32
    MB_PollResult r = mb_poll();
    if (!r.ok) {
        return;
    }

    if (r.drip_rate_sp >= 50 && r.drip_rate_sp <= 500) {
        dripRateSetpoint = r.drip_rate_sp / 10;
    }
    if (r.low_drip_shdn_delay <= 999) {
        shutdownDelay = r.low_drip_shdn_delay;
    }
    pumpShutdownEnabled = (r.low_drip_shdn_en != 0);
#endif
}

void SettingsView::handleTickEvent()
{
    if (savedTicksRemaining > 0) {
        savedTicksRemaining--;

        if (savedTicksRemaining == 0) {
            application().gotoMainScreenWipeTransitionEast();
        }
    }
}

void SettingsView::DripRateMinusPress()
{
    if (dripRateSetpoint > 5) {
        dripRateSetpoint--;
    }
    updateDripRateDisplay();
}

void SettingsView::DripRatePlusPress()
{
    if (dripRateSetpoint < 50) {
        dripRateSetpoint++;
    }
    updateDripRateDisplay();
}

void SettingsView::updateDripRateDisplay()
{
    Unicode::snprintf(Number_3Buffer, NUMBER_3_SIZE, "%d", dripRateSetpoint);
    Number_3.invalidate();
}

void SettingsView::ShutdownDelayMinusPress()
{
    if (shutdownDelay > 0) {
        shutdownDelay--;
    }
    updateShutdownDelayDisplay();
}

void SettingsView::ShutdownDelayPlusPress()
{
    if (shutdownDelay < 999) {
        shutdownDelay++;
    }
    updateShutdownDelayDisplay();
}

void SettingsView::updateShutdownDelayDisplay()
{
    Unicode::snprintf(Number_2_1Buffer, NUMBER_2_1_SIZE, "%d", shutdownDelay);
    Number_2_1.invalidate();
}

void SettingsView::PumpShutdownPress()
{
    if (pumpShutdownEnabled) {
        /* Turning protection OFF requires confirmation — keep toggle ON. */
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
    bool ok = true;

    if (!mb_set_drip_rate((float)dripRateSetpoint)) {
        ok = false;
    }
    if (!mb_write_holding_u16(MB_HOLD_LOW_DRIP_SHDN_EN,
                              pumpShutdownEnabled ? 1u : 0u)) {
        ok = false;
    }
    if (!mb_write_holding_u16(MB_HOLD_LOW_DRIP_SHDN_DELAY,
                              (uint16_t)shutdownDelay)) {
        ok = false;
    }

    if (!ok) {
        return;
    }
#endif

    savedTicksRemaining = SAVED_DISPLAY_TICKS;
}

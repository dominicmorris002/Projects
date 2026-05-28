#include <gui/settings_screen/SettingsView.hpp>
#include <images/BitmapDatabase.hpp>
#include <touchgfx/Unicode.hpp>
#include <cstdio>
#include <cstring>

#ifdef TARGET_STM32
#include "stm32u5xx_hal.h"
extern UART_HandleTypeDef huart1;
#define UART_SEND(buf, len) HAL_UART_Transmit(&huart1, (uint8_t*)(buf), (uint16_t)(len), 200)
#else
#define UART_SEND(buf, len) (void)0
#endif

static const int SAVED_DISPLAY_TICKS = 94;

SettingsView::SettingsView()
    : dripRateSetpoint(0),
      shutdownDelay(0),
      pumpShutdownEnabled(false),
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

    dripRateSetpoint = 0;
    shutdownDelay = 0;
    pumpShutdownEnabled = false;
    savedTicksRemaining = 0;

    updateDripRateDisplay();
    updateShutdownDelayDisplay();
    updateToggleDisplay();
}

void SettingsView::tearDownScreen()
{
    SettingsViewBase::tearDownScreen();
}

void SettingsView::handleTickEvent()
{
    if (savedTicksRemaining > 0)
    {
        savedTicksRemaining--;

        if (savedTicksRemaining == 0)
        {
            updateDripRateDisplay();
            application().gotoMainScreenWipeTransitionEast();
        }
    }
}

// ───────── DRIP RATE ─────────

void SettingsView::DripRateMinusPress()
{
    if (dripRateSetpoint > 0) dripRateSetpoint--;
    updateDripRateDisplay();
}

void SettingsView::DripRatePlusPress()
{
    if (dripRateSetpoint < 999) dripRateSetpoint++;
    updateDripRateDisplay();
}

void SettingsView::updateDripRateDisplay()
{
    Unicode::snprintf(Number_3Buffer, NUMBER_3_SIZE, "%d", dripRateSetpoint);
    Number_3.invalidate();
}

// ───────── SHUTDOWN DELAY ─────────

void SettingsView::ShutdownDelayMinusPress()
{
    if (shutdownDelay > 0) shutdownDelay--;
    updateShutdownDelayDisplay();
}

void SettingsView::ShutdownDelayPlusPress()
{
    if (shutdownDelay < 999) shutdownDelay++;
    updateShutdownDelayDisplay();
}

void SettingsView::updateShutdownDelayDisplay()
{
    Unicode::snprintf(Number_2_1Buffer, NUMBER_2_1_SIZE, "%d", shutdownDelay);
    Number_2_1.invalidate();
}

// ───────── PUMP TOGGLE ─────────

void SettingsView::PumpShutdownPress()
{
    pumpShutdownEnabled = !pumpShutdownEnabled;
    updateToggleDisplay();

    if (pumpShutdownEnabled)
    {
        UART_SEND("PUMP_SHUTDOWN:1\n", strlen("PUMP_SHUTDOWN:1\n"));
    }
    else
    {
        UART_SEND("PUMP_SHUTDOWN:0\n", strlen("PUMP_SHUTDOWN:0\n"));
    }
}

void SettingsView::updateToggleDisplay()
{
    toggleButton1.forceState(pumpShutdownEnabled);
    toggleButton1.invalidate();
}

// ───────── SAVE ─────────

void SettingsView::SavePress()
{
    char msg[64];

    snprintf(msg, sizeof(msg),
             "SETPOINT:%d,DELAY:%d,SHUTDOWN:%d\n",
             dripRateSetpoint,
             shutdownDelay,
             pumpShutdownEnabled ? 1 : 0);

    UART_SEND(msg, strlen(msg));

    Unicode::snprintf(Number_3Buffer, NUMBER_3_SIZE, "OK");
    Number_3.invalidate();

    savedTicksRemaining = SAVED_DISPLAY_TICKS;
}
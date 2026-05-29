#include <gui/main_screen/MainView.hpp>
#include <texts/TextKeysAndLanguages.hpp>
#include <images/BitmapDatabase.hpp>
#include <touchgfx/Unicode.hpp>
#include <cstring>

#ifdef TARGET_STM32
#  include "modbus_master.hpp"
#  include "stm32u5xx_hal.h"
#endif

#include <touchgfx/events/ClickEvent.hpp>

namespace {

static const touchgfx::TypedText kNumericTypedText =
    touchgfx::TypedText(T___SINGLEUSE_OL3R);

static int dpmFromTenths(uint16_t tenths)
{
    if (tenths >= 50 && tenths <= 500) {
        return (int)(tenths / 10);
    }
    if (tenths >= 5 && tenths <= 50) {
        return (int)tenths;
    }
    return -1;
}

static void formatDpmInteger(uint16_t tenths, touchgfx::Unicode::UnicodeChar *buf, uint16_t size)
{
    Unicode::snprintf(buf, size, "%u", (unsigned)((tenths + 5) / 10));
}

} // namespace

MainView::MainView()
    : oilerRunning(false),
      dripRateTenths(0xFFFF),
      setpointDpm(-1),
      lastAlarms(0xFFFF),
      networkShown(false),
      alarmBellVisible(false),
      lastPrimeSendMs(0),
      commFailStreak(0)
{
    alarmLineBuffer[0]   = 0;
    networkLineBuffer[0] = 0;
    lastStatusBuf[0]     = 0;
}

void MainView::setupScreen()
{
    MainViewBase::setupScreen();

    Number_1.setTypedText(kNumericTypedText);
    textArea6.setTypedText(kNumericTypedText);

    Unicode::snprintf(alarmLineBuffer, ALARM_LINE_SIZE, "%s",
                      touchgfx::TypedText(T___SINGLEUSE_0936).getText());
    textArea5.setWildcard(alarmLineBuffer);

    Unicode::snprintf(networkLineBuffer, NETWORK_LINE_SIZE, "%s",
                      touchgfx::TypedText(T___SINGLEUSE_RE9M).getText());
    textArea5_1.setWildcard(networkLineBuffer);

    oilerRunning = false;
    dripRateTenths = 0xFFFF;
    setpointDpm = -1;

    updateDripRateTenths(0);
    updateSetpoint(20);
    updateAlarmText(0);
    updateNetworkText(false);
    updateStatusText("STANDBY");
}

void MainView::tearDownScreen()
{
    MainViewBase::tearDownScreen();
}

void MainView::handleTickEvent()
{
#ifdef TARGET_STM32
    if (button1.getPressedState()) {
        const uint32_t now = HAL_GetTick();
        /* Slow prime while held — avoids colliding with background Modbus poll. */
        if ((now - lastPrimeSendMs) >= 1000u) {
            mb_pause_for_ms(1200);
            if (mb_send_prime()) {
                lastPrimeSendMs = now;
            }
        }
    }
#endif
}

void MainView::updateDripRateTenths(uint16_t tenths)
{
    if (tenths == dripRateTenths) {
        return;
    }

    dripRateTenths = tenths;
    /* Top number on main screen = live drip rate */
    formatDpmInteger(tenths, textArea6Buffer, TEXTAREA6_SIZE);
    textArea6.invalidate();
}

void MainView::updateSetpoint(int dpm)
{
    if (dpm < 0 || dpm == setpointDpm) {
        return;
    }

    setpointDpm = dpm;
    /* Bottom number on main screen = setpoint */
    Unicode::snprintf(Number_1Buffer, NUMBER_1_SIZE, "%d", setpointDpm);
    Number_1.invalidate();
}

void MainView::updateStatusText(const char *text)
{
    if (!text) {
        return;
    }

    if (strncmp(lastStatusBuf, text, sizeof(lastStatusBuf) - 1) == 0) {
        return;
    }

    strncpy(lastStatusBuf, text, sizeof(lastStatusBuf) - 1);
    lastStatusBuf[sizeof(lastStatusBuf) - 1] = 0;
    Unicode::strncpy(textArea2Buffer, text, TEXTAREA2_SIZE - 1);
    textArea2Buffer[TEXTAREA2_SIZE - 1] = 0;
    textArea2.invalidate();
}

void MainView::updateAlarmText(uint16_t alarms)
{
    if (alarms == lastAlarms) {
        return;
    }
    lastAlarms = alarms;

    /* Keep alarm line as designer text ("No Alarms") — custom text uses missing glyphs (????). */
    Unicode::snprintf(alarmLineBuffer, ALARM_LINE_SIZE, "%s",
                      touchgfx::TypedText(T___SINGLEUSE_0936).getText());

    const bool active = (alarms != 0);
    if (active) {
        if (!alarmBellVisible) {
            image2.setVisible(true);
            alarmBellVisible = true;
            image2.invalidate();
        }
    } else if (alarmBellVisible) {
        image2.setVisible(false);
        alarmBellVisible = false;
        image2.invalidate();
    }

    textArea5.invalidate();
}

void MainView::updateNetworkText(bool connected)
{
    if (connected == networkShown) {
        return;
    }
    networkShown = connected;

    if (connected) {
        Unicode::strncpy(networkLineBuffer, "Connected", NETWORK_LINE_SIZE - 1);
        image3.setBitmap(touchgfx::Bitmap(BITMAP_SIGNAL_STATUS_4_ID));
    } else {
        Unicode::snprintf(networkLineBuffer, NETWORK_LINE_SIZE, "%s",
                          touchgfx::TypedText(T___SINGLEUSE_RE9M).getText());
        image3.setBitmap(touchgfx::Bitmap(BITMAP_SIGNAL_STATUS_0_ID));
    }

    networkLineBuffer[NETWORK_LINE_SIZE - 1] = 0;
    textArea5_1.invalidate();
    image3.invalidate();
}

void MainView::PrimeOilerPress()
{
    /* Hold-to-prime: see handleTickEvent() */
}

void MainView::SetpointPress()
{
    application().gotoSettingsScreenWipeTransitionEast();
}

void MainView::handleClickEvent(const touchgfx::ClickEvent &evt)
{
    if (evt.getType() == touchgfx::ClickEvent::RELEASED) {
        touchgfx::Rect r = Number_1.getAbsoluteRect();
        if (r.intersect(evt.getX(), evt.getY())) {
            SetpointPress();
            return;
        }
        r = textArea6.getAbsoluteRect();
        if (r.intersect(evt.getX(), evt.getY())) {
            SetpointPress();
            return;
        }
    }

    MainViewBase::handleClickEvent(evt);
}

void MainView::RunOilerPress()
{
    const bool wantRun = !oilerRunning;

#ifdef TARGET_STM32
    mb_pause_for_ms(200);
    if (!mb_set_run(wantRun)) {
        updateStatusText("COMM ERR");
        return;
    }
#endif

    oilerRunning = wantRun;
    updateStatusText(oilerRunning ? "RUNNING" : "STANDBY");
}

void MainView::applyPollResult(const MB_PollResult &result)
{
    updateDripRateTenths(result.drip_rate);

    const int sp = dpmFromTenths(result.drip_rate_sp);
    if (sp >= 0) {
        updateSetpoint(sp);
    }

    const bool running = (result.status == MB_STATUS_RUNNING);
    if (running != oilerRunning) {
        oilerRunning = running;
    }

    updateAlarmText(result.alarms);

    if (result.alarms & MB_ALM_LOW_OIL) {
        updateStatusText("LOW OIL");
    } else if (result.alarms & MB_ALM_LOW_DRIP_SHDN) {
        updateStatusText("DRIP SHDN");
    } else if (result.alarms & MB_ALM_LOW_DRIP_WARN) {
        updateStatusText("DRIP WARN");
    } else if (result.status == MB_STATUS_FAULT) {
        updateStatusText("FAULT");
    } else if (oilerRunning) {
        updateStatusText("RUNNING");
    } else {
        updateStatusText("STANDBY");
    }
}

void MainView::handlePollResult(const MB_PollResult &result)
{
    if (!result.ok) {
        commFailStreak++;

        if (commFailStreak >= 5) {
            updateStatusText("COMM ERR");
        }
        return;
    }

    commFailStreak = 0;
    applyPollResult(result);
}

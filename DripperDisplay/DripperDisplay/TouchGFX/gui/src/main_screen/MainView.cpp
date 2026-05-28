#include <gui/main_screen/MainView.hpp>
#include "modbus_master.h"
   // ← ADD at the top with other includes

#ifdef TARGET_STM32
#  include "stm32u5xx_hal.h"
#endif

MainView::MainView()
    : oilerRunning(false), dripRate(0), setpoint(0)
{
}

void MainView::setupScreen()
{
    MainViewBase::setupScreen();
    oilerRunning = false;
    updateStatusText("STANDBY");
}

void MainView::tearDownScreen()
{
    MainViewBase::tearDownScreen();
}

// ── Number display ─────────────────────────────────────────

void MainView::updateDripRate(int value)
{
    dripRate = value;
    Unicode::snprintf(Number_1Buffer, NUMBER_1_SIZE, "%d", dripRate);
    Number_1.invalidate();
}

void MainView::updateSetpoint(int value)
{
    setpoint = value;
    // Reuse Number_1 since Number_2 doesn't exist yet
    Unicode::snprintf(Number_1Buffer, NUMBER_1_SIZE, "%d", setpoint);
    Number_1.invalidate();
}

// ── Status text ────────────────────────────────────────────

void MainView::updateStatusText(const char *text)
{
    if (!text) return;

    Unicode::snprintf(textArea2Buffer, TEXTAREA2_SIZE, "%s", "");
    Unicode::strncpy(textArea2Buffer, text, TEXTAREA2_SIZE - 1);
    textArea2Buffer[TEXTAREA2_SIZE - 1] = 0;
    textArea2.invalidate();
}

// ── Button handlers ────────────────────────────────────────

void MainView::PrimeOilerPress()
{
#ifdef TARGET_STM32
    mb_send_prime();
#endif
}

void MainView::RunOilerPress()
{
    oilerRunning = !oilerRunning;

#ifdef TARGET_STM32
    bool ok = mb_set_run(oilerRunning);

    if (!ok) {
        // Revert toggle if comms failed so UI stays in sync with hardware
        oilerRunning = !oilerRunning;
        updateStatusText("COMM ERR");
        textArea2.invalidate();
        return;
    }
#endif

    updateStatusText(oilerRunning ? "RUNNING" : "STANDBY");
    textArea2.invalidate();
}

// ── Poll callback — called by MainPresenter every 500 ms ──

void MainView::handlePollResult(const MB_PollResult &result)
{
    // Update drip rate display (divide out the ×10 scaling)
    updateDripRate(result.drip_rate / 10);

    // Sync run state from hardware (handles remote stop / fault stop)
    oilerRunning = (result.status == MB_STATUS_RUNNING);

    // Status text — alarm conditions take priority over run/stop
    if (result.alarms & MB_ALM_LOW_OIL) {
        updateStatusText("LOW OIL");
    } else if (result.alarms & MB_ALM_LOW_DRIP_SHDN) {
        updateStatusText("DRIP SHDN");
    } else if (result.alarms & MB_ALM_LOW_DRIP_WARN) {
        updateStatusText("DRIP WARN");
    } else if (result.status == MB_STATUS_FAULT) {
        updateStatusText("FAULT");
    } else if (result.status == MB_STATUS_RUNNING) {
        updateStatusText("RUNNING");
    } else {
        updateStatusText("STANDBY");
    }
}
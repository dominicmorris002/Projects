#include <gui/main_screen/MainPresenter.hpp>
#include <gui/main_screen/MainView.hpp>
#include "modbus_master.h"

MainPresenter::MainPresenter(MainView &v)
    : view(v), tickCounter(0)
{
}

void MainPresenter::activate()
{
}

void MainPresenter::deactivate()
{
}

// TouchGFX calls this every frame (typically 16ms at 60fps, or 33ms at 30fps).
// We count ticks and poll the ESP32 every ~500ms.
void MainPresenter::handleTickEvent()
{
    // Adjust POLL_EVERY so that POLL_EVERY × frame_period ≈ 500ms
    // At 60fps: 30 ticks × 16ms = 480ms. At 30fps: 15 ticks × 33ms = 495ms.
    static const uint32_t POLL_EVERY = 30u;

    if (++tickCounter >= POLL_EVERY) {
        tickCounter = 0;

#ifdef TARGET_STM32
        MB_PollResult result;
        if (mb_poll(&result)) {
            view.handlePollResult(result);
        }
        // If poll fails we just skip this cycle — display keeps last known state
#endif
    }
}
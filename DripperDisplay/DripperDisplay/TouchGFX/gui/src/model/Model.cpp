// =============================================================
// Model.cpp  —  DripperDisplay TouchGFX Model
// Fixed: tick() now polls ESP32 via Modbus every 500 ms and
//        forwards the result to the presenter → view.
// =============================================================
#include <gui/model/Model.hpp>
#include <gui/model/ModelListener.hpp>
#include "modbus_master.hpp"

#ifdef TARGET_STM32
#  include "stm32u5xx_hal.h"
#endif

// How often to poll the ESP32 (in TouchGFX ticks).
// TouchGFX runs at 60 Hz by default → 30 ticks = 500 ms.
// If your frame rate differs, adjust accordingly.
#define POLL_INTERVAL_TICKS  30

Model::Model() : modelListener(0), tickCounter(0)
{
}

void Model::tick()
{
    tickCounter++;
    if (tickCounter < POLL_INTERVAL_TICKS) return;
    tickCounter = 0;

#ifdef TARGET_STM32
    MB_PollResult result = mb_poll();
    if (modelListener) {
        modelListener->onPollResult(result);
    }
#endif
}
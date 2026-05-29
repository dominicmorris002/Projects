#include <gui/model/Model.hpp>
#include <gui/model/ModelListener.hpp>
#include "modbus_master.hpp"

#define POLL_INTERVAL_TICKS  30

Model::Model() : modelListener(0), tickCounter(0)
{
}

void Model::tick()
{
    tickCounter++;
    if (tickCounter < POLL_INTERVAL_TICKS) {
        return;
    }
    tickCounter = 0;

#ifdef TARGET_STM32
    if (mb_poll_paused()) {
        return;
    }

    MB_PollResult result = mb_poll();
    if (modelListener) {
        modelListener->onPollResult(result);
    }
#endif
}

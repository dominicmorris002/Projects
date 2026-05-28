/*
 * modbus_integration_example.cpp
 * ────────────────────────────────────────────────────────────────────────────
 * Minimal example showing HOW to plug ModbusRTU into your existing
 * DripMonitor threads.  This is NOT a drop-in replacement for dripper.cpp
 * or display.cpp — it shows the integration pattern only.
 *
 * Suggested placement:
 *   • Declare one ModbusRTU instance (global or in dripper.cpp).
 *   • Call modbus.init() once inside the dripper or main thread's init path,
 *     after Zephyr's device subsystem is ready (after k_sleep or main() body).
 *   • Periodically call modbus.writeStatus() after every dripper state update.
 *   • Call modbus.readCommands() on the same interval to receive screen input.
 *
 * Typical polling interval: 250–500 ms.
 * ────────────────────────────────────────────────────────────────────────────
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "modbus_rtu.hpp"
// Your existing headers:
// #include "dripper.hpp"
// #include "storage.hpp"

LOG_MODULE_REGISTER(app_modbus_example, LOG_LEVEL_INF);

// ── Singleton ────────────────────────────────────────────────────────────────
static ModbusRTU modbus;

// ── Modbus polling thread ─────────────────────────────────────────────────────
// Stack size: 2048 bytes is sufficient for the Modbus client API.
#define MODBUS_THREAD_STACK_SIZE 2048
#define MODBUS_THREAD_PRIORITY   7          /* lower number = higher priority */
#define MODBUS_POLL_INTERVAL_MS  500        /* how often to sync with screen  */

K_THREAD_STACK_DEFINE(modbus_stack, MODBUS_THREAD_STACK_SIZE);
static struct k_thread modbus_thread_data;

static void modbus_thread(void *, void *, void *)
{
    /* Wait for the rest of the system to stabilise before opening the bus. */
    k_sleep(K_MSEC(500));

    int rc = modbus.init();
    if (rc != 0) {
        LOG_ERR("Modbus init failed — RS-485 comms unavailable");
        return;
    }

    while (true) {

        /* ── 1. Push current dripper state to the screen ──────────────── */
        //
        // In your real code, read these from the global dripper state or
        // shared variables protected by a mutex / atomic.
        //
        // uint8_t  sys_status  = dripper_get_status();   // MB_STATUS_*
        // double   drip_rate   = dripper_get_rate();      // drips/min
        // double   drip_sp     = dripper_get_setpoint();  // drips/min
        // uint16_t alarms      = dripper_get_alarm_flags(); // MB_ALARM_*
        // bool     motor_on    = dripper_is_motor_on();
        // bool     low_oil     = dripper_is_low_oil();

        // Placeholder values — replace with real dripper accessors:
        uint8_t  sys_status = MB_STATUS_RUNNING;
        double   drip_rate  = 12.5;
        double   drip_sp    = 10.0;
        uint16_t alarms     = 0;
        bool     motor_on   = true;
        bool     low_oil    = false;

        rc = modbus.writeStatus(sys_status, drip_rate, drip_sp,
                                alarms, motor_on, low_oil);
        if (rc != 0) {
            LOG_WRN("Modbus writeStatus error %d — will retry", rc);
        }

        /* ── 2. Read commands from the screen ─────────────────────────── */
        uint16_t cmd   = MB_CMD_NONE;
        double   newSP = 0.0;

        rc = modbus.readCommands(cmd, newSP);
        if (rc == 0 && cmd != MB_CMD_NONE) {

            LOG_INF("Modbus command received: %u", cmd);

            switch (cmd) {
            case MB_CMD_START:
                // dripper_start();
                LOG_INF("CMD: START motor");
                break;

            case MB_CMD_STOP:
                // dripper_stop();
                LOG_INF("CMD: STOP motor");
                break;

            case MB_CMD_PRIME:
                // dripper_prime();
                LOG_INF("CMD: PRIME");
                break;

            case MB_CMD_RESET_ALARMS:
                // dripper_reset_alarms();
                LOG_INF("CMD: RESET ALARMS");
                break;

            default:
                LOG_WRN("Unknown Modbus command %u — ignoring", cmd);
                break;
            }

            /* If the screen also sent a new setpoint, apply it. */
            if (newSP > 0.0) {
                LOG_INF("New drip setpoint from screen: %.1f drips/min", newSP);
                // dripper_set_setpoint(newSP);
                // storage_save_setpoint(newSP);
            }

            /* Acknowledge: clear the command register so the screen knows
             * the PLC consumed it. */
            modbus.ackCommand();
        }

        k_sleep(K_MSEC(MODBUS_POLL_INTERVAL_MS));
    }
}

/*
 * Call this from main() or your board init code ONCE, after SYS_INIT is done.
 *
 * Example in main.cpp:
 *
 *   #include "modbus_integration_example.hpp"  // forward-declare start_modbus_thread()
 *
 *   int main(void) {
 *       ...
 *       start_modbus_thread();
 *       ...
 *   }
 */
void start_modbus_thread(void)
{
    k_thread_create(&modbus_thread_data,
                    modbus_stack,
                    K_THREAD_STACK_SIZEOF(modbus_stack),
                    modbus_thread,
                    nullptr, nullptr, nullptr,
                    MODBUS_THREAD_PRIORITY,
                    0,
                    K_NO_WAIT);
    k_thread_name_set(&modbus_thread_data, "modbus");
}

#pragma once
// =============================================================
// modbus_master.hpp
// Display (STM32) side — Modbus RTU Master
// Talks to ESP32 PLC 21 Flex over RS-485 at 19200 baud, Even parity
// =============================================================
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// -------------------------------------------------------------
// ── Register addresses (must match modbus_rtu.cpp on ESP32) ──
// -------------------------------------------------------------

// Slave node ID
#define MODBUS_NODE_ID          1

// Holding registers — display WRITES these (FC06)
#define MB_HOLD_RUN             0   // 0 = STOP, 1 = RUN
#define MB_HOLD_DRIP_RATE_SP    1   // drip rate setpoint × 10  (e.g. 200 = 20.0 dpm)
#define MB_HOLD_MODE            2   // 0 = normal, 1 = prime
#define MB_HOLD_LOW_DRIP_SHDN_EN    3
#define MB_HOLD_LOW_DRIP_SHDN_DELAY 4

// Input registers — display READS these (FC04)
#define MB_INP_STATUS           0   // see MB_STATUS_* below
#define MB_INP_DRIP_RATE        1   // actual drip rate × 10
#define MB_INP_DRIP_RATE_SP     2   // active setpoint × 10
#define MB_INP_ALARMS           3   // alarm bitmask — see MB_ALM_* below
#define MB_INP_LOW_DRIP_SHDN_EN     4
#define MB_INP_LOW_DRIP_SHDN_DELAY  5

// -------------------------------------------------------------
// ── Status values (MB_INP_STATUS) ────────────────────────────
// -------------------------------------------------------------
#define MB_STATUS_STANDBY       0
#define MB_STATUS_RUNNING       1
#define MB_STATUS_FAULT         2

// -------------------------------------------------------------
// ── Alarm bitmask (MB_INP_ALARMS) ────────────────────────────
// -------------------------------------------------------------
#define MB_ALM_SYS_FAULT        (1u << 0)
#define MB_ALM_LOW_DRIP_WARN    (1u << 1)
#define MB_ALM_LOW_DRIP_SHDN    (1u << 2)
#define MB_ALM_LOW_OIL          (1u << 3)

// -------------------------------------------------------------
// ── Poll result structure — filled by mb_poll() ──────────────
// -------------------------------------------------------------
typedef struct {
    uint16_t status;       // MB_STATUS_*
    uint16_t drip_rate;    // actual rate × 10
    uint16_t drip_rate_sp; // setpoint × 10
    uint16_t alarms;       // MB_ALM_* bitmask
    uint16_t low_drip_shdn_en;    // 0/1 protection enabled
    uint16_t low_drip_shdn_delay; // minutes
    bool     ok;           // false if comms failed
} MB_PollResult;

// -------------------------------------------------------------
// ── Public API ────────────────────────────────────────────────
// -------------------------------------------------------------

// Call once at startup (sets up UART for Modbus RTU)
void     mb_init(void);

// Read all input registers from the ESP32 in one shot.
// Returns result.ok == false if comms timed out or CRC failed.
MB_PollResult mb_poll(void);

// Pause background polls until HAL_GetTick() passes this window.
void     mb_pause_for_ms(uint32_t ms);
bool     mb_poll_paused(void);

// Write the run command — true = RUN, false = STOP
// Returns false if write failed
bool     mb_set_run(bool run);

// Send a prime pulse (holds MB_HOLD_MODE=1 for one cycle)
bool     mb_send_prime(void);

// Write a new drip rate setpoint (pass actual dpm, e.g. 20.5)
// Returns false if write failed
bool     mb_set_drip_rate(float dpm);

bool     mb_write_holding_u16(uint16_t addr, uint16_t val);

#ifdef __cplusplus
}
#endif

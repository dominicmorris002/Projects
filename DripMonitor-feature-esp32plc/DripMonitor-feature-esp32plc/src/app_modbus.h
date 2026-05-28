// =============================================================
// app_modbus.h  —  ESP32 PLC 21 side  (FIXED)
//
// ⚠ The old version of this file had wrong register definitions.
//   This version matches modbus_rtu.cpp exactly.
//   Replace your existing app_modbus.h with this file.
// =============================================================
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Slave Node ID
#define MODBUS_NODE_ID          1

// ── Holding registers (display WRITES, PLC READS) ─────────────
// FC06 Write Single Register
#define MB_HOLD_RUN             0   // 0 = STOP, 1 = RUN
#define MB_HOLD_DRIP_RATE_SP    1   // drip rate setpoint × 10 (e.g. 200 = 20.0 dpm)
#define MB_HOLD_MODE            2   // 0 = normal, 1 = prime
#define MB_HOLD_LOW_DRIP_SHDN_EN  3 // 0 = protection off, 1 = on
#define MB_HOLD_LOW_DRIP_SHDN_DELAY 4 // shutdown delay, minutes

// ── Input registers (PLC WRITES, display READS) ───────────────
// FC04 Read Input Registers
#define MB_INP_STATUS           0   // see MB_STATUS_* below
#define MB_INP_DRIP_RATE        1   // actual drip rate × 10
#define MB_INP_DRIP_RATE_SP     2   // active setpoint × 10
#define MB_INP_ALARMS           3   // alarm bitmask — see MB_ALM_* below
#define MB_INP_LOW_DRIP_SHDN_EN 4   // low-drip shutdown protection enabled
#define MB_INP_LOW_DRIP_SHDN_DELAY 5 // shutdown delay, minutes

// ── Status values (MB_INP_STATUS) ────────────────────────────
#define MB_STATUS_STANDBY       0
#define MB_STATUS_RUNNING       1
#define MB_STATUS_FAULT         2

// ── Alarm bitmask (MB_INP_ALARMS) ────────────────────────────
#define MB_ALM_SYS_FAULT        (1u << 0)
#define MB_ALM_LOW_DRIP_WARN    (1u << 1)
#define MB_ALM_LOW_DRIP_SHDN    (1u << 2)
#define MB_ALM_LOW_OIL          (1u << 3)

#ifdef __cplusplus
}
#endif
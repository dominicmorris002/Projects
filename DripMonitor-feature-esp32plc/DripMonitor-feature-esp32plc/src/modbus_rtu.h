// modbus_rtu.h — ESP32 PLC 21 FLEX (Zephyr 4.4)
#pragma once

#include <stdint.h>

// ── Slave identity ─────────────────────────────────────────
#define MODBUS_NODE_ID   1

// ── Holding register addresses  (HMI writes → PLC reads) ──
#define MB_HOLD_RUN          0   // 0=stop, 1=run
#define MB_HOLD_DRIP_RATE_SP 1   // setpoint × 10  (e.g. 25.5 dpm → 255)
#define MB_HOLD_MODE         2   // 0=normal, 1=prime
#define MB_HOLD_LOW_DRIP_SHDN_EN    3
#define MB_HOLD_LOW_DRIP_SHDN_DELAY 4

// ── Input register addresses  (PLC writes → HMI reads) ────
#define MB_INP_STATUS        0   // 0=standby, 1=running, 2=fault
#define MB_INP_DRIP_RATE     1   // actual drip rate × 10
#define MB_INP_DRIP_RATE_SP  2   // active setpoint × 10
#define MB_INP_ALARMS        3   // bitmask — see MB_ALM_* below
#define MB_INP_LOW_DRIP_SHDN_EN     4
#define MB_INP_LOW_DRIP_SHDN_DELAY  5

// ── Alarm bitmask bits ─────────────────────────────────────
#define MB_ALM_SYS_FAULT      (1u << 0)
#define MB_ALM_LOW_DRIP_WARN  (1u << 1)
#define MB_ALM_LOW_DRIP_SHDN  (1u << 2)
#define MB_ALM_LOW_OIL        (1u << 3)

// ── Status values ──────────────────────────────────────────
#define MB_STATUS_STANDBY  0u
#define MB_STATUS_RUNNING  1u
#define MB_STATUS_FAULT    2u

#ifdef __cplusplus
extern "C" {
#endif

int  modbus_rtu_init(void);
void modbus_rtu_sync(void);

#ifdef __cplusplus
}
#endif
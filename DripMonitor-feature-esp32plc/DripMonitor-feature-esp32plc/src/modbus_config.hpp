#pragma once

#include <cstdint>

// ── Modbus interface ─────────────────────────
#define MODBUS_BAUD 19200
#define MODBUS_RESPONSE_TIMEOUT_MS 200
#define MODBUS_SERVER_ADDR 1
#define MODBUS_IFACE_NODE   DT_ALIAS(modbus_rs485)

// ── Registers ────────────────────────────────
enum {
    MB_REG_SYS_STATUS = 0,
    MB_REG_DRIP_RATE,
    MB_REG_DRIP_SETPOINT,
    MB_REG_ALARM_FLAGS,
    MB_REG_COMMAND,
    MB_REG_NEW_DRIP_SP,
    MB_REG_MOTOR_STATE,
    MB_REG_LOW_OIL,
    MB_NUM_HOLD_REGS
};

// ── Commands ─────────────────────────────────
enum {
    MB_CMD_NONE = 0
};
#pragma once

/*
 * modbus_rtu.hpp
 * ──────────────
 * Thin C++ wrapper around Zephyr's modbus_client API for the
 * DripMonitor ESP32 PLC 21 application.
 *
 * Role: Modbus RTU **client** (master).
 *   - The ESP32 PLC is the master.
 *   - The TouchGFX screen (or any external device) is the server (slave).
 *   - The physical layer is the PLC 21's on-board RS-485 transceiver
 *     on UART2 (TX=GPIO17, RX=GPIO16, DE/RE=GPIO4).
 *
 * Exposed register map (server address 1 by default, see MODBUS_SERVER_ADDR):
 *
 *   Holding registers (FC03 read / FC06 write single / FC16 write multiple)
 *   ────────────────────────────────────────────────────────────────────────
 *   Reg 0  │ RO │ System status     │ 0=IDLE 1=RUNNING 2=ALARM 3=PRIMING
 *   Reg 1  │ RO │ Drip rate (×10)   │ e.g. 125 = 12.5 drips/min
 *   Reg 2  │ RO │ Drip setpoint(×10)│ e.g. 100 = 10.0 drips/min
 *   Reg 3  │ RO │ Alarm bitfield    │ bit0=LOW_OIL, bit1=LOW_DRIP_WARN,
 *          │    │                   │ bit2=LOW_DRIP_SHUTDOWN
 *   Reg 4  │ RW │ Command register  │ 1=START 2=STOP 3=PRIME 4=RESET_ALARMS
 *   Reg 5  │ RW │ New drip SP (×10) │ written by screen to change setpoint
 *   Reg 6  │ RO │ Motor state       │ 0=OFF 1=ON
 *   Reg 7  │ RO │ Low oil flag      │ 0=OK 1=LOW
 *
 *   Coils (FC01 read / FC05 write single)  — optional, not used yet
 *   ─────────────────────────────────────
 *   Coil 0 │ RW │ Motor enable override
 */

#include <cstdint>
#include <zephyr/modbus/modbus.h>

// ── Compile-time tunables ─────────────────────────────────────────────────

/* Modbus node label from the devicetree (must match .overlay). */
#define MODBUS_IFACE_NODE   DT_NODELABEL(modbus0)

/* Default address of the remote server (TouchGFX screen / slave device).
 * The Modbus standard reserves 0 for broadcast; valid slave IDs are 1–247. */
#define MODBUS_SERVER_ADDR  1

/* Baud rate — must match uart2 `current-speed` in the overlay AND
 * the setting on the remote device. */
#define MODBUS_BAUD         19200

/* Response timeout in milliseconds.  Outdoors with long cable runs
 * 200 ms is safe; shorten for snappier UI if the cable is short. */
#define MODBUS_RESPONSE_TIMEOUT_MS  200

// ── Register indices (holding registers) ────────────────────────────────
#define MB_REG_SYS_STATUS       0
#define MB_REG_DRIP_RATE        1
#define MB_REG_DRIP_SETPOINT    2
#define MB_REG_ALARM_FLAGS      3
#define MB_REG_COMMAND          4
#define MB_REG_NEW_DRIP_SP      5
#define MB_REG_MOTOR_STATE      6
#define MB_REG_LOW_OIL          7
#define MB_NUM_HOLD_REGS        8   /* total holding registers in the map */

// ── Alarm bitfield (MB_REG_ALARM_FLAGS) ─────────────────────────────────
#define MB_ALARM_LOW_OIL            (1u << 0)
#define MB_ALARM_LOW_DRIP_WARN      (1u << 1)
#define MB_ALARM_LOW_DRIP_SHUTDOWN  (1u << 2)

// ── Command values (MB_REG_COMMAND) ─────────────────────────────────────
#define MB_CMD_NONE             0
#define MB_CMD_START            1
#define MB_CMD_STOP             2
#define MB_CMD_PRIME            3
#define MB_CMD_RESET_ALARMS     4

// ── System-status values (MB_REG_SYS_STATUS) ────────────────────────────
#define MB_STATUS_IDLE          0
#define MB_STATUS_RUNNING       1
#define MB_STATUS_ALARM         2
#define MB_STATUS_PRIMING       3

// ─────────────────────────────────────────────────────────────────────────

class ModbusRTU {
public:
    ModbusRTU() = default;

    /**
     * Initialise the Modbus serial interface.
     * Must be called once before any read/write.
     * Returns 0 on success, negative errno on failure.
     */
    int init();

    /**
     * Write the full status block (regs 0–3, 6–7) to the remote screen
     * in one FC16 (Write Multiple Registers) transaction.
     * Returns 0 on success, negative errno on failure.
     */
    int writeStatus(uint8_t sysStatus, double dripRate, double dripSetpoint,
                    uint16_t alarmFlags, bool motorOn, bool lowOil);

    /**
     * Read the command register (reg 4) and optional new setpoint (reg 5)
     * from the screen.
     * outCmd is set to one of MB_CMD_*.
     * outNewSP is set to the new drip setpoint (÷10), only meaningful when
     * outCmd is MB_CMD_* and reg 5 is non-zero.
     * Returns 0 on success, negative errno on failure.
     */
    int readCommands(uint16_t &outCmd, double &outNewSP);

    /**
     * Acknowledge a command by writing MB_CMD_NONE (0) to reg 4 so the
     * screen knows the PLC has consumed the command.
     */
    int ackCommand();

    /** Returns true if init() succeeded and the interface is open. */
    bool isReady() const { return _iface >= 0; }

private:
    int _iface = -1;
};

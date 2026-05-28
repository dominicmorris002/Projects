// modbus_rtu.cpp — ESP32 PLC 21 FLEX  (Zephyr 4.4)

#include "modbus_rtu.h"
#include "dripper.hpp"

#include <zephyr/kernel.h>
#include <zephyr/modbus/modbus.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(modbus_rtu, LOG_LEVEL_INF);

// ── Register banks ─────────────────────────────────────────
static uint16_t holding[3];   // HMI → PLC  (FC06 writes land here)
static uint16_t inp_reg[4];   // PLC → HMI  (FC04 reads come from here)
static int      iface = -1;

// ── Modbus callbacks ───────────────────────────────────────
static int holding_reg_rd(uint16_t addr, uint16_t *val)
{
    if (addr >= ARRAY_SIZE(holding)) return -ENOTSUP;
    *val = holding[addr];
    return 0;
}

static int holding_reg_wr(uint16_t addr, uint16_t val)
{
    if (addr >= ARRAY_SIZE(holding)) return -ENOTSUP;
    holding[addr] = val;

    switch (addr) {
    case MB_HOLD_RUN:
        ocmd_sysRun = (val != 0);
        LOG_INF("HMI → Run: %s", ocmd_sysRun ? "RUN" : "STOP");
        break;

    case MB_HOLD_DRIP_RATE_SP: {
        double sp = val / 10.0;
        if (sp >= 5.0 && sp <= 50.0) {
            cfg_dripRate = sp;
            LOG_INF("HMI → drip SP: %.1f dpm", cfg_dripRate);
        } else {
            LOG_WRN("HMI drip SP out of range: %.1f — ignored", sp);
        }
        break;
    }

    case MB_HOLD_MODE:
        if (val == 1u) {
            ocmd_prime = true;   // dripper.hpp must declare: extern bool ocmd_prime;
            LOG_INF("HMI → Prime command");
        } else {
            ocmd_prime = false;
        }
        break;

    default:
        break;
    }
    return 0;
}

static int input_reg_rd(uint16_t addr, uint16_t *val)
{
    if (addr >= ARRAY_SIZE(inp_reg)) return -ENOTSUP;
    *val = inp_reg[addr];
    return 0;
}

static struct modbus_user_callbacks cbs;

// ── Init ───────────────────────────────────────────────────
int modbus_rtu_init(void)
{
    cbs.holding_reg_rd = holding_reg_rd;
    cbs.holding_reg_wr = holding_reg_wr;
    cbs.input_reg_rd   = input_reg_rd;

    struct modbus_iface_param p = {};
    p.mode                = MODBUS_MODE_RTU;
    p.rx_timeout          = 500000;       // µs
    p.serial.baud         = 19200;
    p.serial.parity       = UART_CFG_PARITY_EVEN;
    p.serial.stop_bits    = UART_CFG_STOP_BITS_1;
    p.server.user_cb      = &cbs;
    p.server.unit_id      = MODBUS_NODE_ID;

    iface = modbus_iface_get_by_name("modbus0");
    if (iface < 0) {
        LOG_ERR("modbus0 interface not found (%d) — check DTS alias", iface);
        return iface;
    }

    int rc = modbus_init_server(iface, p);
    if (rc) {
        LOG_ERR("modbus_init_server failed (%d)", rc);
        return rc;
    }

    // Safe power-on defaults
    holding[MB_HOLD_RUN]          = 0u;
    holding[MB_HOLD_DRIP_RATE_SP] = (uint16_t)(cfg_dripRate * 10.0);
    holding[MB_HOLD_MODE]         = 0u;

    LOG_INF("Modbus RTU server ready  node_id=%d  baud=19200  parity=E",
            MODBUS_NODE_ID);
    return 0;
}

// ── Sync — call from your dripper tick ────────────────────
// Mirrors the live dripper state into the input registers so
// the HMI can read them with FC04.
void modbus_rtu_sync(void)
{
    if (iface < 0) return;

    // Status word
    inp_reg[MB_INP_STATUS] = alm_sysFault   ? MB_STATUS_FAULT
                           : sts_sysRun     ? MB_STATUS_RUNNING
                                            : MB_STATUS_STANDBY;

    // Actual drip rate × 10, clamped to uint16 range
    double r = val_dripRate * 10.0;
    inp_reg[MB_INP_DRIP_RATE] = (r > 65535.0) ? 65535u : (uint16_t)r;

    // Active setpoint × 10
    double sp = cfg_dripRate * 10.0;
    inp_reg[MB_INP_DRIP_RATE_SP] = (sp > 65535.0) ? 65535u : (uint16_t)sp;

    // Alarm bitmask
    uint16_t alms = 0u;
    if (alm_sysFault)        alms |= MB_ALM_SYS_FAULT;
    if (alm_lowDripRateWar)  alms |= MB_ALM_LOW_DRIP_WARN;
    if (alm_lowDripRateShDn) alms |= MB_ALM_LOW_DRIP_SHDN;
    if (alm_lowOil)          alms |= MB_ALM_LOW_OIL;
    inp_reg[MB_INP_ALARMS] = alms;
}
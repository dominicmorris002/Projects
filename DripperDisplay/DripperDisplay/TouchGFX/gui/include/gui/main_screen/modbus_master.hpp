// modbus_master.hpp — STM32 HMI  Modbus RTU master
// Place alongside your other application headers.
#pragma once

#include <stdint.h>
#include <stdbool.h>

// ── Must match ESP32 modbus_rtu.h exactly ─────────────────
#define MB_SLAVE_ID          1

#define MB_HOLD_RUN          0
#define MB_HOLD_DRIP_RATE_SP 1
#define MB_HOLD_MODE         2

#define MB_INP_STATUS        0
#define MB_INP_DRIP_RATE     1
#define MB_INP_DRIP_RATE_SP  2
#define MB_INP_ALARMS        3

#define MB_STATUS_STANDBY    0u
#define MB_STATUS_RUNNING    1u
#define MB_STATUS_FAULT      2u

#define MB_ALM_SYS_FAULT     (1u << 0)
#define MB_ALM_LOW_DRIP_WARN (1u << 1)
#define MB_ALM_LOW_DRIP_SHDN (1u << 2)
#define MB_ALM_LOW_OIL       (1u << 3)

#define MB_MODE_NORMAL       0u
#define MB_MODE_PRIME        1u

// ── Structured poll result ─────────────────────────────────
typedef struct {
    uint16_t status;       // MB_STATUS_*
    uint16_t drip_rate;    // actual × 10
    uint16_t drip_rate_sp; // setpoint × 10
    uint16_t alarms;       // bitmask MB_ALM_*
} MB_PollResult;

// ── Public API ─────────────────────────────────────────────
#ifdef __cplusplus
extern "C" {
#endif

// Call once from your HAL init or MX_USART1_UART_Init section
void mb_master_init(void);

// Send run/stop command. Returns true on acknowledged response.
bool mb_set_run(bool run);

// Send prime command. Returns true on acknowledged response.
bool mb_send_prime(void);

// Send drip rate setpoint (actual dpm value e.g. 25.5).
// Internally multiplied by 10 and range-checked [5.0, 50.0].
bool mb_set_drip_rate(float dpm);

// Poll all 4 input registers in one FC04 request.
// Returns true and fills *out on success.
bool mb_poll(MB_PollResult *out);

#ifdef __cplusplus
}
#endif
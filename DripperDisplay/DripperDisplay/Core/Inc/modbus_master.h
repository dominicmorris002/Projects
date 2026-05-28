#ifndef MODBUS_MASTER_HPP
#define MODBUS_MASTER_HPP

#include <stdint.h>
#include <stdbool.h>

// ── Modbus config ──────────────────────────────────────────
#define MB_SLAVE_ID         1

#define MB_HOLD_RUN         0x0000
#define MB_HOLD_MODE        0x0001
#define MB_HOLD_DRIP_RATE_SP 0x0002

#define MB_MODE_NORMAL      0
#define MB_MODE_PRIME       1

#define MB_STATUS_STANDBY   0
#define MB_STATUS_RUNNING   1
#define MB_STATUS_FAULT     2

#define MB_ALM_SYS_FAULT     (1u << 0)
#define MB_ALM_LOW_DRIP_WARN (1u << 1)
#define MB_ALM_LOW_DRIP_SHDN (1u << 2)
#define MB_ALM_LOW_OIL       (1u << 3)

typedef struct {
    uint16_t status;
    uint16_t drip_rate;
    uint16_t drip_rate_sp;
    uint16_t alarms;
} MB_PollResult;

// ── THIS is the fix — makes C++ functions callable from C ──
#ifdef __cplusplus
extern "C" {
#endif

void mb_master_init(void);
bool mb_set_run(bool run);
bool mb_send_prime(void);
bool mb_set_drip_rate(float dpm);
bool mb_poll(MB_PollResult *out);

#ifdef __cplusplus
}
#endif

#endif // MODBUS_MASTER_HPP
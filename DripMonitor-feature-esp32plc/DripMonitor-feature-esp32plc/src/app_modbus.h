#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Slave Node ID */
#define MODBUS_NODE_ID  1

/* Holding registers (R/W: External screen commands the PLC) */
#define MB_HOLD_DRIP_INTERVAL_MS   0
#define MB_HOLD_DRIP_COUNT         1
#define MB_HOLD_MODE               2   /* 0 = Manual, 1 = Auto */

/* Input registers (RO: External screen monitors PLC state) */
#define MB_INP_STATUS              0   /* 0 = Idle, 1 = Running, 2 = Fault */
#define MB_INP_TOTAL_DRIPS         1   /* Lifetime counter */

/* Initialization and Interface API */
int  app_modbus_init(void);
void app_modbus_set_status(uint16_t status, uint16_t total_drips);

uint16_t app_modbus_get_drip_interval(void);
uint16_t app_modbus_get_drip_count(void);
uint16_t app_modbus_get_mode(void);

#ifdef __cplusplus
}
#endif
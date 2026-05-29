#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void hmi_settings_save(int drip_dpm, int shutdown_min, bool pump_protect_on);
bool hmi_settings_load(int *drip_dpm, int *shutdown_min, bool *pump_protect_on);

void hmi_settings_request_protection_off(void);
bool hmi_settings_consume_protection_off(void);

#ifdef __cplusplus
}
#endif

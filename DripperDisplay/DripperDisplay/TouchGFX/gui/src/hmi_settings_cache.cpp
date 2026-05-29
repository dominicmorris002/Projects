#include "hmi_settings_cache.hpp"

static struct {
    int  drip_dpm;
    int  shutdown_min;
    bool pump_protect_on;
    bool valid;
    bool protection_off_pending;
} s_cache;

void hmi_settings_save(int drip_dpm, int shutdown_min, bool pump_protect_on)
{
    s_cache.drip_dpm          = drip_dpm;
    s_cache.shutdown_min      = shutdown_min;
    s_cache.pump_protect_on   = pump_protect_on;
    s_cache.valid             = true;
}

bool hmi_settings_load(int *drip_dpm, int *shutdown_min, bool *pump_protect_on)
{
    if (!s_cache.valid || !drip_dpm || !shutdown_min || !pump_protect_on) {
        return false;
    }

    *drip_dpm        = s_cache.drip_dpm;
    *shutdown_min    = s_cache.shutdown_min;
    *pump_protect_on = s_cache.pump_protect_on;
    return true;
}

void hmi_settings_request_protection_off(void)
{
    s_cache.protection_off_pending = true;
    s_cache.pump_protect_on        = false;
    if (s_cache.valid) {
        /* Keep cache in sync so Settings reload shows OFF even if Modbus read fails. */
    }
}

bool hmi_settings_consume_protection_off(void)
{
    if (!s_cache.protection_off_pending) {
        return false;
    }
    s_cache.protection_off_pending = false;
    return true;
}

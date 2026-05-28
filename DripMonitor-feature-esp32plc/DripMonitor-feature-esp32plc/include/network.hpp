#pragma once

#ifndef network_hpp
#define network_hpp

/*
 * Lightweight network helper for the ESP32 PLC 21 + WIZnet W5500 + FX30
 * cellular gateway path.  The PLC sees the FX30 as a plain DHCP-served
 * Ethernet uplink, so this module just:
 *   - waits for L4 connectivity events from net_mgmt,
 *   - syncs the system clock from public NTP servers,
 *   - exposes a single status enum and an event callback for cloud.cpp.
 *
 * No modem, no PPP, no AT chat, no signal-quality query - all of that is
 * handled inside the FX30 and is not visible to the application.
 */

// **SNTP**
// type    // variable             // value             // comment
#define   cfg_ntpServer1           "time1.google.com"   // primary NTP server
#define   cfg_ntpServer2           "time2.google.com"   // secondary NTP server
#define   cfg_ntpServer3           "time3.google.com"   // tertiary NTP server
#define   cfg_sntpTimeout          5000                 // ms
#define   cfg_sntpRetryInterval    2000                 // ms

#include <zephyr/net/net_mgmt.h>

struct net_if;
struct net_mgmt_event_callback;

class Network {
public:
    enum link_status {
        LINK_DOWN = 0,
        LINK_UP,
        LINK_UNKNOWN = 0xff
    };

    enum network_event {
        NETWORK_EVENT_L4_CONNECTED,
        NETWORK_EVENT_L4_DISCONNECTED,
    };

    typedef void (*network_event_callback_t)(network_event event);

    Network();

    /* Register the net_mgmt L4 callback and bring the interface up.  Safe
     * to call once at startup; idempotent on subsequent calls. */
    int init();

    /* Block-and-query SNTP across the configured server list, then push
     * the result into CLOCK_REALTIME.  Returns 0 on success. */
    int sync_clock();

    /* Application-side hook for L4 connect / disconnect notifications. */
    void register_event_callback(network_event_callback_t callback);

    static bool sts_network_connected;
    static bool sts_time_synced;

private:
    static void net_l4_event_cb(struct net_mgmt_event_callback *cb,
                                uint64_t mgmt_event,
                                struct net_if *iface);

    struct net_if *iface;
    struct net_mgmt_event_callback net_event_mgmt;

    static network_event_callback_t event_callback;
};

#endif // network_hpp

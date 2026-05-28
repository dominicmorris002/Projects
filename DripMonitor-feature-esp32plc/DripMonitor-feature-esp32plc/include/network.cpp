#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/sntp.h>
#include <zephyr/net/socket.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/clock.h>

#include <time.h>

#include "network.hpp"

LOG_MODULE_REGISTER(pkg_network, LOG_LEVEL_INF);

bool Network::sts_network_connected = false;
bool Network::sts_time_synced = false;
Network::network_event_callback_t Network::event_callback = nullptr;

Network::Network()
    : iface(net_if_get_default())
{
}

void Network::register_event_callback(network_event_callback_t callback)
{
    event_callback = callback;
}

int Network::init()
{
    if (iface == nullptr) {
        LOG_ERR("No default network interface available");
        return -ENODEV;
    }

    net_mgmt_init_event_callback(&net_event_mgmt, net_l4_event_cb,
                                 NET_EVENT_L4_CONNECTED |
                                 NET_EVENT_L4_DISCONNECTED);
    net_mgmt_add_event_callback(&net_event_mgmt);

    /* The W5500 driver normally brings the iface up at boot; make sure
     * it's at least administratively up so static IP config can settle. */
    int ret = net_if_up(iface);
    if (ret < 0 && ret != -EALREADY) {
        LOG_ERR("net_if_up failed: %d", ret);
        return ret;
    }

    /* If the link is already up by the time we attached the callback we
     * won't get a NET_EVENT_L4_CONNECTED, so synthesize one. */
    if (net_if_flag_is_set(iface, NET_IF_RUNNING)) {
        LOG_INF("Network interface already running");
        sts_network_connected = true;
        if (event_callback != nullptr) {
            event_callback(NETWORK_EVENT_L4_CONNECTED);
        }
    }

    return 0;
}

void Network::net_l4_event_cb(struct net_mgmt_event_callback *cb,
                              uint64_t mgmt_event, struct net_if *iface)
{
    ARG_UNUSED(cb);
    ARG_UNUSED(iface);

    switch (mgmt_event) {
    case NET_EVENT_L4_CONNECTED:
        LOG_INF("L4 network connected");
        sts_network_connected = true;
        if (event_callback != nullptr) {
            event_callback(NETWORK_EVENT_L4_CONNECTED);
        }
        break;

    case NET_EVENT_L4_DISCONNECTED:
        LOG_WRN("L4 network disconnected");
        sts_network_connected = false;
        if (event_callback != nullptr) {
            event_callback(NETWORK_EVENT_L4_DISCONNECTED);
        }
        break;

    default:
        break;
    }
}

int Network::sync_clock()
{
    static const char *ntpServers[] = {
        cfg_ntpServer1, cfg_ntpServer2, cfg_ntpServer3,
    };

    struct zsock_addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    struct timespec tspec = {};
    int ret = -ETIMEDOUT;

    for (size_t i = 0; i < ARRAY_SIZE(ntpServers); i++) {
        struct zsock_addrinfo *addr = nullptr;

        ret = zsock_getaddrinfo(ntpServers[i], "123", &hints, &addr);
        if (ret < 0 || addr == nullptr) {
            LOG_WRN("DNS resolution failed for %s: %d", ntpServers[i], ret);
            continue;
        }

        struct sntp_ctx ctx;
        ret = sntp_init(&ctx, addr->ai_addr, addr->ai_addrlen);
        if (ret < 0) {
            LOG_WRN("sntp_init failed for %s: %d", ntpServers[i], ret);
            zsock_freeaddrinfo(addr);
            continue;
        }

        struct sntp_time now;
        ret = sntp_query(&ctx, cfg_sntpTimeout, &now);
        sntp_close(&ctx);
        zsock_freeaddrinfo(addr);

        if (ret == 0) {
            tspec.tv_sec = now.seconds;
            tspec.tv_nsec = ((uint64_t)now.fraction * 1000000000ULL) >> 32;
            break;
        }

        LOG_WRN("sntp_query %s failed: %d, retrying", ntpServers[i], ret);
        k_sleep(K_MSEC(cfg_sntpRetryInterval));
    }

    if (ret != 0) {
        LOG_ERR("All NTP servers failed");
        return ret;
    }

    ret = sys_clock_settime(SYS_CLOCK_REALTIME, &tspec);
    if (ret != 0) {
        LOG_ERR("sys_clock_settime failed: %d", ret);
        return ret;
    }

    sts_time_synced = true;
    LOG_INF("System clock synced to NTP: %u", (uint32_t)tspec.tv_sec);
    return 0;
}

# Cloud State Diagram

The cloud thread is driven by a Zephyr SMF state machine (`cloud.cpp`).
Network connectivity is provided by the on-board WIZnet W5500 Ethernet
controller, which talks to a Sierra Wireless FX30 cellular gateway.  The
PLC sees only a normal DHCP-served Ethernet uplink, so the state machine
has no modem/PPP/AT-chat states; it just reacts to L4 link events from
`net_mgmt` and to MQTT events from the broker.

```mermaid
stateDiagram-v2
    [*] --> SYSTEM_INIT

    SYSTEM_INIT --> NETWORK_DISCONNECTED: tago.init() && network.init() OK
    SYSTEM_INIT --> [*]: init failed (terminate)

    NETWORK_DISCONNECTED --> NETWORK_CONNECTED: NET_EVENT_L4_CONNECTED

    state NETWORK_CONNECTED {
        [*] --> MQTT_CONNECTING: enter (auto)
        MQTT_CONNECTING --> MQTT_CONNECTED: tago.connect() OK (MQTT CONNACK)
        MQTT_CONNECTING --> MQTT_CONNECTING: TIMER_RETRY (backoff)
        MQTT_CONNECTING --> ERROR_RECOVERY: 5 retries failed

        MQTT_CONNECTED --> MQTT_CONNECTING: MQTT_DISCONNECTED
        MQTT_CONNECTED --> MQTT_CONNECTED: TIMER_UPDATE_PARAMS\n(publish + keep-alive)
    }

    NETWORK_CONNECTED --> NETWORK_CONNECTED: TIMER_CLOCK_SYNC\n(network.sync_clock via SNTP)

    NETWORK_CONNECTED --> NETWORK_DISCONNECTED: NET_EVENT_L4_DISCONNECTED

    ERROR_RECOVERY --> NETWORK_CONNECTED: 60 s back-off,\nlink still up
    ERROR_RECOVERY --> NETWORK_DISCONNECTED: 60 s back-off,\nlink down
```

Notes:
- The old `MODEM_REGISTERED` / `MODEM_DEREGISTERED` events and the
  `modem.restart()` recovery action no longer exist — the FX30 owns the
  cellular link and the PLC has nothing locally to power-cycle.
- The watchdog (configured in the board defconfig) is responsible for
  recovering from CPU lock-ups; the SMF only handles application-level
  retries.

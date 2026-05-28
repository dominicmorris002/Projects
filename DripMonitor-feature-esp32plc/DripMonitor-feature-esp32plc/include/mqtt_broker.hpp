#pragma once

#ifndef mqtt_broker_hpp
#define mqtt_broker_hpp

#include <zephyr/net/mqtt.h>
#include <zephyr/net/socket.h>
#include <string>
#include <vector>
#include <unordered_set>

struct key_value_pair {
    std::string key;
    std::string value;
};

class mqttBroker {
public:
    enum connSts {
        NOT_CONNECTED,
        CONNECTING,
        CONNECTED
    };

    struct mqtt_publish_unacked {
        mqtt_publish_param param;
        uint8_t retryCount;
    };

    mqttBroker(const char* addr, uint16_t port, mqtt_evt_cb_t cbFunc, sec_tag_t* secTags, size_t m_sec_tags_size);
    void init(const std::string& id, const std::string& username, const std::string& password);
    int connect();
    int disconnect();
    int getConnectionStatus();
    bool getTopicSubscribeStatus();
    int subscribeTopic(const char *topic);
    int publishMsg(const char *topic, std::vector<key_value_pair> &params);
    void retryMsgs();
    int evtHandler(struct mqtt_client *client, const struct mqtt_evt *evt,
        uint8_t *buffer, size_t bufferSize);
    int brokerKeepAlive(int timeout);


private:
    int brokerInit();
    void setClientInfo();
    int pollSock(int timeout);
    int waitForConnack(uint32_t timeout_ms);
    uint16_t getMsgID(std::vector<mqtt_publish_unacked> &msgs);


    // type                             // variable                     // comment
    uint32_t                            retryInterval = 2000;           // interval before retrying network function, ms
    uint32_t                            connectionTimeout = 30000;      // timeout for waiting for CONNACK, ms
    bool                                brokerResolved = false;         // status bit for broker dns resolution
    bool                                topicSubscribed = false;        // status bit for a topic is subscribbed, does not track for more than 1
    struct mqtt_client                  client_ctx;                     // mqtt client
    struct sockaddr_storage             broker;                         // mqtt broker info 
    struct mqtt_utf8                    mqttClientId;                   // client ID structure
    struct mqtt_utf8                    mqttUsername;                   // client username structure
    struct mqtt_utf8                    mqttPassword;                   // client password structure
    struct zsock_pollfd                 fds[1];                         // file descriptors for polling
    uint8_t                             rx_buffer[512];                 // mqtt receive buffer
    uint8_t                             tx_buffer[512];                 // mqtt transmit buffer
    std::vector<mqtt_publish_unacked>   unackedMsgs;                    // unacked mqtt messages
    const char*                         brokerHostname;                 // web address of broker to connect to
    uint16_t                            brokerPort;                     // broker port to connect to
    std::string                         clientId;                       // client ID for connection
    std::string                         clientUsername;                 // client username for connection
    std::string                         clientPassword;                 // client password for connection
    mqtt_evt_cb_t                       mqttEvtHandler;                 // 
    enum connSts                        mqttConnStatus;                       // connection status, 0=n/c, 1=connecting, 2=connected

};




#endif // mqtt_broker_hpp
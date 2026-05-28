#include <zephyr/net/socket.h>
#include <zephyr/net/tls_credentials.h>
#include <zephyr/random/random.h>
#include <zephyr/logging/log.h>

#include <sstream>

#include "mqtt_broker.hpp"

LOG_MODULE_REGISTER(pkg_mqtt_broker, LOG_LEVEL_INF);

// *************************************************************
// Constructor
// *************************************************************
mqttBroker::mqttBroker(const char* addr, uint16_t port, mqtt_evt_cb_t cbFunc, sec_tag_t* m_sec_tags, size_t m_sec_tags_size)
    : brokerHostname(addr), brokerPort(port), mqttEvtHandler(cbFunc) {
        mqtt_client_init(&client_ctx);

        client_ctx.broker = &broker;
        client_ctx.evt_cb = mqttEvtHandler;
        client_ctx.protocol_version = MQTT_VERSION_3_1_1;
        client_ctx.transport.type = MQTT_TRANSPORT_NON_SECURE;

        client_ctx.rx_buf = rx_buffer;
        client_ctx.rx_buf_size = sizeof(rx_buffer);
        client_ctx.tx_buf = tx_buffer;
        client_ctx.tx_buf_size = sizeof(tx_buffer);

        // Initialize connection state
        mqttConnStatus = NOT_CONNECTED;
        fds[0].fd = -1;

        /* TLS config if re-enabled in the future
        struct mqtt_sec_config *tls_config = &client_ctx.transport.tls.config;

        tls_config->peer_verify = TLS_PEER_VERIFY_REQUIRED;
        tls_config->cipher_list = NULL;
        tls_config->sec_tag_list = m_sec_tags;
        tls_config->sec_tag_count = m_sec_tags_size;
        tls_config->hostname = brokerHostname;
        */

}

// *************************************************************
// Public Functions
// *************************************************************
void mqttBroker::init(const std::string& id, const std::string& username, const std::string& password) {
    clientId = id;
    clientUsername = username;
    clientPassword = password;

    mqttClientId = {
        .utf8 = (uint8_t *)clientId.c_str(),
        .size = clientId.length()
    };

    mqttUsername = {
        .utf8 = (uint8_t *)clientUsername.c_str(),
        .size = clientUsername.length()
    };

    mqttPassword = {
        .utf8 = (uint8_t *)clientPassword.c_str(),
        .size = clientPassword.length()
    };

    client_ctx.client_id = mqttClientId;
    client_ctx.password = &mqttPassword;
    client_ctx.user_name = &mqttUsername;    
}


/***************************************************************
 * @brief Connect to the MQTT broker. Performs DNS resolution
 *        first if not already resolved.
 * 
 * @return int - 0 on success, negative error code on failure
 **************************************************************/
int mqttBroker::connect(){

    int ret;
    int retries = 3;

    LOG_INF("Connecting MQTT client");

    // resolve address
    if (!brokerResolved){
        ret = brokerInit();
        if (ret){
            LOG_ERR("DNS not resolved, cannot connect: %d", ret);
            return ret;
        }
    }

    // connect
    mqttConnStatus = CONNECTING;
    while (retries--){
        ret = mqtt_connect(&client_ctx);
        if (ret == 0) {
            // prepare file descriptors for polling
            fds[0].fd = client_ctx.transport.tcp.sock;
            fds[0].events = ZSOCK_POLLIN;

            // wait for CONNACK with timeout
            ret = waitForConnack(connectionTimeout);
            if (ret == 0) {
                return 0;  // connection successful
            }
        }

        // if connection fails, abort (close socket)
        mqtt_abort(&client_ctx);
        fds[0].fd = -1;

        if (retries > 0){
            LOG_ERR("MQTT Connect Error: %d, retrying", ret);
            k_sleep(K_MSEC(retryInterval));
        } else {
            LOG_ERR("MQTT Connect Error: %d", ret);
        }
    }

    mqttConnStatus = NOT_CONNECTED;
    return ret;
}


int mqttBroker::disconnect(){
    int ret;
    LOG_INF("Disconnecting MQTT client");

    ret = mqtt_disconnect(&client_ctx, NULL);
    if (ret != 0) {
        LOG_ERR("MQTT Disconnect Error: %d, aborting", ret);
    }

    return ret;
}


int mqttBroker::getConnectionStatus(){
    return mqttConnStatus;
}


bool mqttBroker::getTopicSubscribeStatus(){
    return topicSubscribed;
}


int mqttBroker::subscribeTopic(const char *topic) {
    int ret;

    struct mqtt_topic sub_topic[] = {{
        .topic = {
            .utf8 = (uint8_t *)topic,
            .size = strlen(topic)
        },
        .qos = MQTT_QOS_1_AT_LEAST_ONCE
    }};

	const struct mqtt_subscription_list sub_list = {
		.list = sub_topic,
		.list_count = ARRAY_SIZE(sub_topic),
		.message_id = 1u,
	};

	LOG_INF("Subscribing to %hu topic(s)", sub_list.list_count);

	ret = mqtt_subscribe(&client_ctx, &sub_list);
	if (ret != 0) {
		LOG_ERR("Failed to subscribe to topics: %d", ret);
	}

    brokerKeepAlive(1000);

	return ret;
}


int mqttBroker::publishMsg(const char *topic, std::vector<key_value_pair> &params){
    
    if (mqttConnStatus != CONNECTED){
        return 0;
    }

    int ret;
	struct mqtt_publish_param param;

    //construct topic
    struct mqtt_topic pub_topic = {
        .topic = {
            .utf8 = (uint8_t *)topic,
            .size = strlen(topic)
        },
        .qos = MQTT_QOS_1_AT_LEAST_ONCE
    };
    
    //construct payload as {variable: , value: } pairs to store at Tago.io
    std::stringstream payload;
    bool firstLoop = 1;
    for (const key_value_pair &pair : params){
        if (!firstLoop){
            payload << ",";
        } else {
            firstLoop = 0;
        }
        payload << pair.key << ":" << pair.value;
    }

    // clear cloud params
    params.clear();

    //convert text to char array for compatibility
    size_t textLen = payload.str().length();
    uint8_t payloadText[1024];
    if (textLen > (sizeof(payloadText)-1)) {
        LOG_ERR("Message to publish exceeds buffer limit, length: %zu", textLen);
    }
    size_t i = 0;
    for (char c : payload.str()){
        payloadText[i++] = c;
    }

    //construct and publish message
	param.message.topic = pub_topic;
	param.message.payload.data = (uint8_t *)payloadText;
    param.message.payload.len = textLen;
	param.message_id = getMsgID(unackedMsgs);
	param.dup_flag = 0U;
	param.retain_flag = 0U;

    // store message for tracking
    unackedMsgs.push_back({param, 0});
	ret = mqtt_publish(&client_ctx, &param);
    if (ret){
        LOG_ERR("Messaged %d Failed: %d", param.message_id, ret);
    } else {
        LOG_INF("Messaged %d Published", param.message_id);
    }
    return ret;
}


void mqttBroker::retryMsgs(){
    if (mqttConnStatus != CONNECTED || unackedMsgs.empty()){
        return;
    }

    // remove messages that have exceeded retry counts
    for (int i = unackedMsgs.size() - 1; i >= 0; i--) {
        if (unackedMsgs[i].retryCount >= 3) {
            unackedMsgs.erase(unackedMsgs.begin() + i);
        }
    }

    // retry unacked messages, one at a time
    if (!unackedMsgs.empty()) {
        mqtt_publish(&client_ctx, &unackedMsgs[0].param);
        unackedMsgs[0].retryCount++;
    }
       
}


int mqttBroker::evtHandler(struct mqtt_client *client, const struct mqtt_evt *evt,
    uint8_t *buffer, size_t bufferSize){

	int ret = 0;
    int ackRet = 0;

	switch (evt->type) {
	case MQTT_EVT_CONNACK:
        if (evt->result != 0) {
            LOG_ERR("MQTT connect failed %d", evt->result);
            ret = evt->result;
            break;
        }

        mqttConnStatus = CONNECTED;
        LOG_INF("MQTT client connected");

		break;

	case MQTT_EVT_DISCONNECT:
    
		LOG_INF("MQTT client disconnected %d", evt->result);

		mqttConnStatus = NOT_CONNECTED;
        topicSubscribed = false;
        fds[0].fd = -1;

		break;

    case MQTT_EVT_PUBLISH:

        ret = mqtt_read_publish_payload(&client_ctx, buffer, bufferSize);
        if (ret < 0) {
            LOG_ERR("MQTT failed to read payload: %d", ret);
        } else if (ret > 0) {
            // acknowlege receipt if QOS1
            if (evt->param.publish.message.topic.qos == MQTT_QOS_1_AT_LEAST_ONCE){
                struct mqtt_puback_param ack_param = {
                    .message_id = evt->param.publish.message_id
                };
                ackRet = mqtt_publish_qos1_ack(&client_ctx, &ack_param);
                if (ackRet < 0) {
                    LOG_ERR("MQTT failed to ack QOS1 message: %d", ackRet);
                }
            }
        }

        LOG_INF("PUBLISH packet id: %u", evt->param.publish.message_id);

        break;

	case MQTT_EVT_PUBACK:
        {
            if (evt->result != 0) {
                LOG_ERR("MQTT PUBACK error %d", evt->result);
                break;
            }

            uint16_t msgID = evt->param.puback.message_id;
            LOG_INF("PUBACK message id: %u", msgID);
            
            // remove message from unacked list
            bool msgAcked = false;
            for (uint8_t i = 0; i < unackedMsgs.size(); i++) {
                if (unackedMsgs[i].param.message_id == msgID) {
                    unackedMsgs.erase(unackedMsgs.begin() + i);
                    msgAcked = true;
                    break;
                }
            }

            // if message not found
            if (!msgAcked){
                LOG_WRN("No message to ack matching ID: %u", msgID);
            }
        }
		break;

	case MQTT_EVT_PUBREC:
        {           
             if (evt->result != 0) {
                LOG_ERR("MQTT PUBREC error %d", evt->result);
                break;
            }

            LOG_INF("PUBREC packet id: %u", evt->param.pubrec.message_id);

            const struct mqtt_pubrel_param rel_param = {
                .message_id = evt->param.pubrec.message_id
            };

            ret = mqtt_publish_qos2_release(client, &rel_param);
            if (ret != 0) {
                LOG_ERR("Failed to send MQTT PUBREL: %d", ret);
            }
        }

		break;

	case MQTT_EVT_PUBCOMP:
		if (evt->result != 0) {
			LOG_ERR("MQTT PUBCOMP error %d", evt->result);
			break;
		}

		LOG_INF("PUBCOMP packet id: %u",
			evt->param.pubcomp.message_id);

		break;

    case MQTT_EVT_SUBACK:
        topicSubscribed = true;
        LOG_INF("SUBACK packet id: %u", evt->param.pubrec.message_id);
        break;

    case MQTT_EVT_UNSUBACK:
        topicSubscribed = false;
        LOG_INF("UNSUBACK packet id: %u", evt->param.pubrec.message_id);
        break;

	case MQTT_EVT_PINGRESP:
		LOG_INF("PINGRESP packet");
		break;

	default:
        LOG_WRN("Unhandled MQTT event type: %d", evt->type);
		break;
	}

    return ret;
}


int mqttBroker::brokerKeepAlive(int timeout){
    int64_t remaining = timeout;
	int64_t start_time = k_uptime_get();
	int ret;

	while (remaining > 0 && mqttConnStatus == CONNECTED) {

		if (pollSock(remaining)) {
			ret = mqtt_input(&client_ctx);
			if (ret != 0) {
				LOG_ERR("MQTT Input Read Error: %d", ret);
				return ret;
			}
		}

		ret = mqtt_live(&client_ctx);
		if (ret != 0 && ret != -EAGAIN) {
			LOG_ERR("MQTT Live Error: %d", ret);
			return ret;
		} else if (ret == 0) {
			ret = mqtt_input(&client_ctx);
			if (ret != 0) {
				LOG_ERR("MQTT Input Read Error: %d", ret);
				return ret;
			}
		}

		remaining = timeout + start_time - k_uptime_get();
        k_yield();
	}

	return 0;
}


// *************************************************************
// Private Functions
// *************************************************************
int mqttBroker::brokerInit(){
    struct zsock_addrinfo *addrInfo = NULL;
	int retries = 3;
	int ret;

    const struct zsock_addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_STREAM,
		.ai_protocol = 0,
	};

    struct sockaddr_in *broker4 = (struct sockaddr_in *)&broker;
    broker4->sin_family = AF_INET;
    broker4->sin_port = htons(brokerPort);
    char ip_str[NET_IPV4_ADDR_LEN];

    char port_str[6];
    sprintf(port_str, "%d", brokerPort);

	while (retries--) {
		ret = zsock_getaddrinfo(brokerHostname, port_str, &hints, &addrInfo);
		if (!ret) {
            net_ipaddr_copy(&broker4->sin_addr, &net_sin(addrInfo->ai_addr)->sin_addr);
            net_addr_ntop(AF_INET, &broker4->sin_addr, ip_str, sizeof(ip_str));

            LOG_INF("DNS resolved for %s:%d -> %s", brokerHostname, brokerPort, ip_str);
            brokerResolved = true;
            return 0;
		}
		if (retries > 0){
            LOG_ERR("DNS not resolved for %s:%d, error: %d, retrying",brokerHostname, brokerPort, ret);
            k_sleep(K_MSEC(retryInterval));
        } else {
            LOG_ERR("DNS not resolved for %s:%d, error: %d",brokerHostname, brokerPort, ret);
        }
        zsock_freeaddrinfo(addrInfo);
	}
    
	return ret;
}


int mqttBroker::pollSock(int timeout){
	int ret = 0;

    if (fds[0].fd >= 0) {
        ret = zsock_poll(fds, 1, timeout);
        if (ret < 0) {
                LOG_ERR("ZSOCK Poll Error: %d", ret);
            }
    } else {
        LOG_ERR("Socket not open");
        return -EAGAIN;
    }

	return ret;

}


int mqttBroker::waitForConnack(uint32_t timeout_ms){
    int64_t remaining = timeout_ms;
    int64_t start_time = k_uptime_get();
    int ret;

    while (remaining > 0 && mqttConnStatus == CONNECTING) {
        if (pollSock(remaining)) {
            ret = mqtt_input(&client_ctx);
            if (ret != 0) {
                LOG_ERR("MQTT Input Read Error while waiting for CONNACK: %d", ret);
                return ret;
            }
        }

        remaining = timeout_ms + start_time - k_uptime_get();
        k_yield();
    }

    if (mqttConnStatus == CONNECTED) {
        return 0;
    } else if (mqttConnStatus == CONNECTING) {
        LOG_ERR("CONNACK timeout after %u ms", timeout_ms);
        return -ETIMEDOUT;
    } else {
        LOG_ERR("Connection failed while waiting for CONNACK");
        return -ECONNREFUSED;
    }
}


uint16_t mqttBroker::getMsgID(std::vector<mqtt_publish_unacked> &msgs){
    uint16_t msgID;
    bool unique;

    // check the ID is not currently in use
    do {
        unique = true;
        msgID = (uint16_t)(sys_rand32_get() & 0xFFFF);

        for (const mqtt_publish_unacked &item : msgs) {
            if (msgID == item.param.message_id){
                unique = false;
                break;
            }
        }
    } while (!unique);

    return msgID;
}



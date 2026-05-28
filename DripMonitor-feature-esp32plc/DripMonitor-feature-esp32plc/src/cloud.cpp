// *************************************************************
// Includes & Logging
// *************************************************************
#include "cloud.hpp"
#include <cstddef>

// External variables
extern bool provisioning_mode;

#include <vector>
#include <string>
#include <bitset>
#include <cmath>

#include <zephyr/smf.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/tls_credentials.h>

#include "../include/utility.hpp"
#include "../include/mqtt_broker.hpp"
#include "../include/network.hpp"
#include "dripper.hpp"
#include "storage.hpp"

LOG_MODULE_REGISTER(app_cloud, CONFIG_APP_CLOUD_LOG_LEVEL);

// *************************************************************
// Enumerations & Structs
// *************************************************************
enum cloud_id {
    // numbers
    CLD_STS_SYSRUN = 1,
    CLD_INP_MTRRUN,
    CLD_OCMD_SYSRUN,
    CLD_ALM_LOW_DRIP_WARN,
    CLD_ALM_LOW_DRIP_SHDN,
    CLD_ALM_LOW_OIL,
    CLD_VAL_DRIP_RATE,
    CLD_CFG_DRIP_RATE,
    CLD_CFG_LOW_DRIP_SHDN_ENABLE,
    CLD_CFG_LOW_DRIP_SHDN_DELAY,
    CLD_CFG_AUX1_ENABLE,
    CLD_CFG_AUX2_ENABLE,
    CLD_CFG_AUX3_ENABLE,

    // strings
    CLD_CFG_SCHED_START = 100,
    CLD_CFG_SCHED_VALIDATION,
    CLD_CFG_AUX1_LABEL,
    CLD_CFG_AUX2_LABEL,
    CLD_CFG_AUX3_LABEL
};

enum cloud_state {
    ROOT,
    SYSTEM_INIT,
    NETWORK_DISCONNECTED,
    NETWORK_CONNECTED,
    MQTT_CONNECTING,
    MQTT_CONNECTED,
    ERROR_RECOVERY
};


// Events that drive state transitions
enum class CloudEvent {
    NO_EVENT,

    // Network events (from callbacks)
    NET_L4_CONNECTED,
    NET_L4_DISCONNECTED,

    // MQTT events
    MQTT_CONNECTED,
    MQTT_DISCONNECTED,

    // Timer events
    TIMER_RETRY,
    TIMER_UPDATE_PARAMS,
    TIMER_CLOCK_SYNC,

    // Control events
    FATAL_ERROR
};

struct CloudEventMsg {
    CloudEvent event;
    int64_t timestamp;
    int error_code;           // For error events
    void* data;               // Optional event data
};


// User-defined object for state machine context
struct CloudSmfContext {
    // SMF context - MUST be first member
    struct smf_ctx ctx;

    // Application state
    CloudEvent pending_event;

    // Network state tracking
    bool tago_initialized;
    int tago_init_error;
    bool network_initialized;
    int network_init_error;
    bool network_connected;

    // MQTT state
    bool mqtt_connected;
    bool mqtt_subscribed;
    bool time_synced;

    // Retry management
    struct {
        int attempt;
        int64_t backoff_ms;
    } mqtt_retry, time_sync_retry;

    // Timers
    int64_t last_clock_sync_time;
} cloud_smf_ctx = {0};


// *************************************************************
// Configuration
// *************************************************************
// **Thread**
// type         // variable                     // value            // comment
#define         cfg_cloudThreadStackSize        16384               // stack size for cloud thread
#define         cfg_cloudThreadPriority         10                  // low preemptable priority
#define         cfg_cloudThreadDelay            5000                // ms

// *MQTT Broker**
// type         // variable                     // value            // comment
#define         cfg_brokerHostname              "mqtt.tago.io"
#define         cfg_brokerPort                  1883
#define         cfg_mqttTopicUp                 "dm/1/up"           // MQTT Topic, dm = drip monitor, 1 = version, uploading to server
#define         cfg_mqttTopicDn                 "dm/1/dn"           // MQTT Topic, dm = drip monitor, 1 = version, downloading to device

// *Misc**
#define         cfg_cloudUpdateInterval         5000                // interval for updating cloud parameters, ms
#define         cfg_retryInterval               10000               // interval for connection retries, ms
#define         cfg_eventQueueSize              16                  // Number of messages
#define         cfg_timeSyncInterval            (24 * 60 * 60 * 1000) // 24 hours in ms

// *************************************************************
// Global Variables
// *************************************************************
// type                         // variable                         // comment
bool                            val_networkConnected    = false;    // L4 connectivity flag (mirrored for display)
uint8_t                         val_cloudConnStatus     = 0xff;     // cloud connection status, initialized to unused value

// *************************************************************
// Local Variables
// *************************************************************
// **MQTT/Cloud**
// type                         // variable                         // comment
static std::string              val_schedStartStr;                  // runtime code for scheduled start
std::vector<key_value_pair>     cloudParams;                        // updated cloud parameters to publish
sec_tag_t                       m_sec_tags[]            = {1};      // tls cert security tag

Network network;

// *************************************************************
// Static Functions
// *************************************************************
static void mqttEvtHandler(struct mqtt_client *client, const struct mqtt_evt *evt);
static void cloudUpdateParams(bool initialConn);
static void cloudMain();

// Work function declarations
static void cloud_update_work_cb(struct k_work *work);
static void network_retry_work_cb(struct k_work *work);
static void clock_sync_work_cb(struct k_work *work);

// Adapter callback to bridge network events to cloud SMF
static void network_event_adapter(Network::network_event event);

// State machine function declarations
static enum smf_state_result root_run(void *o);
static void system_init_entry(void *o);
static enum smf_state_result system_init_run(void *o);
static void network_disconnected_entry(void *o);
static enum smf_state_result network_disconnected_run(void *o);
static void network_connected_entry(void *o);
static enum smf_state_result network_connected_run(void *o);
static void network_connected_exit(void *o);
static void mqtt_connecting_entry(void *o);
static enum smf_state_result mqtt_connecting_run(void *o);
static void mqtt_connecting_exit(void *o);
static void mqtt_connected_entry(void *o);
static enum smf_state_result mqtt_connected_run(void *o);
static void mqtt_connected_exit(void *o);
static void error_recovery_entry(void *o);
static enum smf_state_result error_recovery_run(void *o);

mqttBroker tago(cfg_brokerHostname, cfg_brokerPort, mqttEvtHandler, m_sec_tags, ARRAY_SIZE(m_sec_tags));


// *************************************************************
// State Machine
// *************************************************************
static const struct smf_state cloud_states[] = {
    [ROOT] = SMF_CREATE_STATE(
        NULL,
        root_run,
        NULL,
        NULL,
        &cloud_states[SYSTEM_INIT]
    ),

    [SYSTEM_INIT] = SMF_CREATE_STATE(
        system_init_entry,
        system_init_run,
        NULL,
        &cloud_states[ROOT],
        NULL
    ),

    [NETWORK_DISCONNECTED] = SMF_CREATE_STATE(
        network_disconnected_entry,
        network_disconnected_run,
        NULL,
        &cloud_states[ROOT],
        NULL
    ),

    [NETWORK_CONNECTED] = SMF_CREATE_STATE(
        network_connected_entry,
        network_connected_run,
        network_connected_exit,
        &cloud_states[ROOT],
        &cloud_states[MQTT_CONNECTING]
    ),

    [MQTT_CONNECTING] = SMF_CREATE_STATE(
        mqtt_connecting_entry,
        mqtt_connecting_run,
        mqtt_connecting_exit,
        &cloud_states[NETWORK_CONNECTED],
        NULL
    ),

    [MQTT_CONNECTED] = SMF_CREATE_STATE(
        mqtt_connected_entry,
        mqtt_connected_run,
        mqtt_connected_exit,
        &cloud_states[NETWORK_CONNECTED],
        NULL
    ),

    [ERROR_RECOVERY] = SMF_CREATE_STATE(
        error_recovery_entry,
        error_recovery_run,
        NULL,
        &cloud_states[ROOT],
        NULL
    ),
};

K_MSGQ_DEFINE(cloud_event_queue, sizeof(CloudEventMsg), cfg_eventQueueSize, 4);


// *************************************************************
// Thread
// *************************************************************
#ifdef CONFIG_ENABLE_CLOUD
K_THREAD_DEFINE(cloudThread, cfg_cloudThreadStackSize, cloudMain, NULL, NULL, NULL, cfg_cloudThreadPriority, 0, cfg_cloudThreadDelay);
#endif


// *************************************************************
// Macro Functions
// *************************************************************
// macro function to check if variable has changed
// var_init detects first run of macro
// a deadband and minimum update interval is applied to prevent
// excessive data usage. If var == 0 after interval then deadband
// is ignored to prevent a 0 reading retaining a small amount when
// system is off. After variables are converted to strings, extra
// 0's are removed from float/double values to save data
#define CHECK_UPDATE_VAR(var, cloudID, params, deadband, minInterval, maxInterval, force) \
    do { \
        static decltype(var) var##_prev; \
        static bool var##_init; \
        static int64_t var##_lastUpdate;\
        int64_t uptime = k_uptime_get(); \
        bool dbExceeded = (var > (var##_prev + deadband)) || (var < (var##_prev - deadband)); \
        bool zeroClamp = var == 0 && var##_prev != 0; \
        bool minIntElapsed = uptime > (var##_lastUpdate + minInterval); \
        bool maxIntElapsed = uptime > (var##_lastUpdate + maxInterval); \
        bool updateReq = force || maxIntElapsed || !var##_init; \
        if (((dbExceeded || zeroClamp) && minIntElapsed) || updateReq){ \
            std::string varStr = std::to_string(var); \
            size_t decPos = varStr.find('.'); \
            if (decPos != std::string::npos) { \
                varStr.erase(varStr.find_last_not_of('0') + 1); \
                if (varStr[varStr.size() - 1] == '.') { \
                    varStr.erase(decPos); \
                } \
            } \
            params.push_back({std::to_string(cloudID), varStr}); \
            var##_prev = var; \
            var##_lastUpdate = uptime; \
            var##_init = 1; \
        } \
    } while (0)

// adds string variable to update without deadband check
#define UPDATE_STRING_VAR(var, cloudID, params, minInterval) \
    do { \
        static bool var##_init; \
        static int64_t var##_lastUpdate;\
        int64_t uptime = k_uptime_get(); \
        bool intElapsed = uptime > (var##_lastUpdate + minInterval); \
        if ((intElapsed) || !var##_init){ \
            params.push_back({std::to_string(cloudID), var}); \
            var##_lastUpdate = uptime; \
            var##_init = 1; \
        } \
    } while (0)


// *************************************************************
// Timers & Work
// *************************************************************
K_WORK_DELAYABLE_DEFINE(cloud_update_workd, cloud_update_work_cb);
K_WORK_DELAYABLE_DEFINE(network_retry_workd, network_retry_work_cb);
K_WORK_DELAYABLE_DEFINE(clock_sync_workd, clock_sync_work_cb);


/***************************************************************
 * @brief Helper function to convert cloud input and save to
 *        storage
 * 
 * @param inpVal input from cloud
 * @param outVal output variable to store value in
 * @param storageId storage ID, use FS_NO_STORE to disable
 **************************************************************/
static inline void cloudInput(const std::string inpVal, bool &outVal, fs_id storageId){
    bool bval = false;
    bool err;

    STOB(inpVal, bval, err);
    if (outVal != bval && !err){
        outVal = bval;
        if (storageId > 0){
            storageWrite(storageId, &outVal, sizeof(outVal));
        }
    }
}


/***************************************************************
 * @brief Helper function to convert cloud input and save to
 *        storage
 * 
 * @param inpVal input from cloud
 * @param outVal output variable to store value in
 * @param storageId storage ID, use FS_NO_STORE to disable
 **************************************************************/
static inline void cloudInput(const std::string inpVal, int &outVal, fs_id storageId){
    int ival;
    bool err;

    STOI(inpVal, ival, 10, err);
    if (outVal != ival && !err){
        outVal = ival;
        if (storageId > 0){
            storageWrite(storageId, &outVal, sizeof(outVal));
        }
    }
}


/***************************************************************
 * @brief Helper function to convert cloud input and save to
 *        storage
 * 
 * @param inpVal input from cloud
 * @param outVal output variable to store value in
 * @param storageId storage ID, use FS_NO_STORE to disable
 **************************************************************/
static inline void cloudInput(const std::string inpVal, double &outVal, fs_id storageId){
    double dval;
    bool err;

    STOD(inpVal, dval, err);
    if (outVal != dval && !err){
        outVal = dval;
        if (storageId > 0){
            storageWrite(storageId, &outVal, sizeof(outVal));
        }
    }
}

/***************************************************************
 * @brief Helper function to convert cloud input and save to
 *        storage
 * 
 * @param inpVal input from cloud
 * @param outVal output variable to store value in
 * @param storageId storage ID, use FS_NO_STORE to disable
 **************************************************************/
static inline void cloudInput(const std::string inpVal, std::string &outVal, fs_id storageId){
    if (outVal != inpVal){
        outVal = inpVal;
        if (storageId > 0){
            storageWrite(storageId, &outVal, sizeof(outVal));
        }
    }
}


/***************************************************************
 * @brief Post a cloud event to the event queue
 * 
 * @param event event to post
 * @param error_code code from errno.h
 **************************************************************/
static inline void post_cloud_event(CloudEvent event, int error_code = 0) {
    CloudEventMsg msg = {
        .event = event,
        .timestamp = k_uptime_get(),
        .error_code = error_code,
        .data = nullptr
    };
    
    int ret = k_msgq_put(&cloud_event_queue, &msg, K_NO_WAIT);
    if (ret == 0) {
        LOG_DBG("Event posted: %d (queue usage: %d/%d)", 
                (int)event,
                k_msgq_num_used_get(&cloud_event_queue),
                cfg_eventQueueSize);
    } else {
        LOG_ERR("Failed to post event %d: %d", (int)event, ret);
    }
}


/***************************************************************
 * @brief Timer callback for cloud update
 * 
 **************************************************************/
static void cloud_update_work_cb(struct k_work *work) {
    post_cloud_event(CloudEvent::TIMER_UPDATE_PARAMS);
}


/***************************************************************
 * @brief Timer callback for mqtt retry
 * 
 **************************************************************/
static void network_retry_work_cb(struct k_work *work) {
    post_cloud_event(CloudEvent::TIMER_RETRY);
}


/***************************************************************
 * @brief Timer callback for clock sync
 * 
 **************************************************************/
static void clock_sync_work_cb(struct k_work *work) {
    post_cloud_event(CloudEvent::TIMER_CLOCK_SYNC);
}


/***************************************************************
 * @brief Run function for the root state, warns on unhandled events
 * 
 * @param o - pointer to user defined object
 **************************************************************/
static enum smf_state_result root_run(void *o) {
    struct CloudSmfContext *ctx = (struct CloudSmfContext *)o;

    switch (ctx->pending_event) {
        case CloudEvent::NO_EVENT:
            break;

        case CloudEvent::NET_L4_DISCONNECTED:
            ctx->network_connected = false;
            val_networkConnected = false;
            break;

        case CloudEvent::MQTT_DISCONNECTED:
            ctx->mqtt_connected = false;
            break;

        // Timer events that may arrive after state transitions - ignore gracefully
        case CloudEvent::TIMER_UPDATE_PARAMS:
        case CloudEvent::TIMER_RETRY:
            LOG_DBG("Root state: Ignoring stale timer event %d", (int)ctx->pending_event);
            break;

        default:
            LOG_WRN("Root state: Unhandled event %d", (int)ctx->pending_event);
            break;
    }

    return SMF_EVENT_HANDLED;
}


/***************************************************************
 * @brief Entry function for the system init state
 * 
 * @param o - pointer to user defined object
 **************************************************************/
static void system_init_entry(void *o) {
    struct CloudSmfContext *ctx = (struct CloudSmfContext *)o;
    static char serialNumber[12] = {0};
    static char apiKey[64] = {0};
    int ret;

    LOG_INF("State Machine: Initializing system");

    // Initialize Tago.io
    if ((storageRead(FS_SERIAL_NUM, serialNumber, sizeof(serialNumber)) > 0) &&
        (storageRead(FS_API_KEY, apiKey, sizeof(apiKey)) > 0)) {
            tago.init(serialNumber, "token", apiKey);
            ctx->tago_initialized = true;
    } else {
        LOG_ERR("Error reading serial number or API key");
        ctx->tago_init_error = -ENOENT;
    }

    // Register adapter callback BEFORE network.init() so we don't miss events
    network.register_event_callback(network_event_adapter);

    // Initialize network (registers net_mgmt callbacks and brings the iface up)
    ret = network.init();
    if (ret == 0){
        ctx->network_initialized = true;
    } else {
        ctx->network_init_error = ret;
    }

    if (ctx->network_initialized && ctx->tago_initialized){
        smf_set_state(SMF_CTX(&cloud_smf_ctx), &cloud_states[NETWORK_DISCONNECTED]);
    } else if (ctx->network_init_error){
        LOG_ERR("Error initializing network: %d", ctx->network_init_error);
        smf_set_terminate(SMF_CTX(&cloud_smf_ctx), ctx->network_init_error);
    } else if (ctx->tago_init_error){
        LOG_ERR("Error initializing Tago.io: %d", ctx->tago_init_error);
        smf_set_terminate(SMF_CTX(&cloud_smf_ctx), ctx->tago_init_error);
    }

}

/***************************************************************
 * @brief Run function for the system init state
 * 
 * @param o - pointer to user defined object
 **************************************************************/
static enum smf_state_result system_init_run(void *o) {
    ARG_UNUSED(o);
    LOG_DBG("State Machine: System init run");
    return SMF_EVENT_PROPAGATE;
}


/***************************************************************
 * @brief Entry function for the network disconnected state
 * 
 * @param o - pointer to user defined object
 **************************************************************/
static void network_disconnected_entry(void *o) {
    ARG_UNUSED(o);
    val_networkConnected = false;
    LOG_INF("State Machine: Network disconnected");
}


/***************************************************************
 * @brief Run function for the network disconnected state
 * 
 * @param o - pointer to user defined object
 **************************************************************/
static enum smf_state_result network_disconnected_run(void *o) {
    struct CloudSmfContext *ctx = (struct CloudSmfContext *)o;

    LOG_DBG("State Machine: Network disconnected run");

    switch (ctx->pending_event) {
        case CloudEvent::NET_L4_CONNECTED:
            ctx->network_connected = true;
            val_networkConnected = true;
            smf_set_state(SMF_CTX(&cloud_smf_ctx), &cloud_states[NETWORK_CONNECTED]);
            break;

        default:
            return SMF_EVENT_PROPAGATE;
    }

    return SMF_EVENT_HANDLED;
}


/***************************************************************
 * @brief Entry function for the network connected state
 * 
 * @param o - pointer to user defined object
 **************************************************************/
static void network_connected_entry(void *o) {
    struct CloudSmfContext *ctx = (struct CloudSmfContext *)o;
    int64_t uptime = k_uptime_get();
    int64_t time_since_last_clock_sync = uptime - ctx->last_clock_sync_time;
    int64_t time_until_next_clock_sync;

    LOG_INF("State Machine: Network connected");
    val_networkConnected = true;

    // Prime the clock-sync retry state on every entry so the first failure
    // sequence (and any subsequent re-entry after an L4 drop) starts with
    // a fresh attempt counter and a non-zero backoff to double from. The
    // success path also resets these, but we can't rely on that having run
    // before the first failure.
    ctx->time_sync_retry.attempt = 0;
    ctx->time_sync_retry.backoff_ms = cfg_retryInterval;

    if (ctx->last_clock_sync_time == 0 || time_since_last_clock_sync >= cfg_timeSyncInterval){
        k_work_schedule(&clock_sync_workd, K_NO_WAIT);
    } else {
        time_until_next_clock_sync = cfg_timeSyncInterval - time_since_last_clock_sync;
        k_work_schedule(&clock_sync_workd, K_MSEC(time_until_next_clock_sync));
    }

}

/***************************************************************
 * @brief Run function for the network connected state
 * 
 * @param o - pointer to user defined object
 **************************************************************/
static enum smf_state_result network_connected_run(void *o) {
    struct CloudSmfContext *ctx = (struct CloudSmfContext *)o;

    LOG_DBG("State Machine: Network connected run");

    switch (ctx->pending_event) {
        case CloudEvent::NET_L4_DISCONNECTED:
            ctx->network_connected = false;
            val_networkConnected = false;
            smf_set_state(SMF_CTX(&cloud_smf_ctx), &cloud_states[NETWORK_DISCONNECTED]);
            break;

        case CloudEvent::TIMER_CLOCK_SYNC:
            if (network.sync_clock() == 0){
                ctx->time_synced = true;
                ctx->time_sync_retry.attempt = 0;
                ctx->time_sync_retry.backoff_ms = cfg_retryInterval;
                ctx->last_clock_sync_time = k_uptime_get();
                LOG_INF("System clock synced to network time");
                k_work_schedule(&clock_sync_workd, K_MSEC(cfg_timeSyncInterval));
            } else if (ctx->time_sync_retry.attempt < 5){
                ctx->time_sync_retry.attempt++;
                ctx->time_sync_retry.backoff_ms *= 2;
                LOG_WRN("Error syncing system clock to network time, retrying in %lld ms",
                        (long long)ctx->time_sync_retry.backoff_ms);
                k_work_reschedule(&clock_sync_workd, K_MSEC(ctx->time_sync_retry.backoff_ms));
            } else {
                LOG_ERR("Error syncing system clock to network time");
                smf_set_state(SMF_CTX(&cloud_smf_ctx), &cloud_states[ERROR_RECOVERY]);
            }

            break;

        default:
            return SMF_EVENT_PROPAGATE;
    }

    return SMF_EVENT_HANDLED;
}


/***************************************************************
 * @brief Exit function for the network connected state
 * 
 * @param o - pointer to user defined object
 **************************************************************/
static void network_connected_exit(void *o) {
    ARG_UNUSED(o);
    LOG_DBG("State Machine: Network connected exit");
    k_work_cancel_delayable(&clock_sync_workd);
}


/***************************************************************
 * @brief Entry function for the mqtt connecting state
 * 
 * @param o - pointer to user defined object
 **************************************************************/
static void mqtt_connecting_entry(void *o) {
    struct CloudSmfContext *ctx = (struct CloudSmfContext *)o;

    LOG_INF("State Machine: Connecting to MQTT");
    val_cloudConnStatus = 1; // Connecting

    // Reset retry state on every entry so a re-entry from MQTT_CONNECTED
    // (after MQTT_DISCONNECTED) starts with a fresh, non-zero backoff.
    ctx->mqtt_retry.attempt = 0;
    ctx->mqtt_retry.backoff_ms = cfg_retryInterval;

    // TIMER_RETRY drives both the initial connect attempt and any subsequent
    // backoff retries. The 2s delay gives the IP stack time to stabilize on
    // first entry; on re-entry from MQTT_CONNECTED it's a harmless small wait.
    k_work_schedule(&network_retry_workd, K_MSEC(2000));
}


/***************************************************************
 * @brief Run function for the mqtt connecting state
 * 
 * @param o - pointer to user defined object
 **************************************************************/
static enum smf_state_result mqtt_connecting_run(void *o) {
    struct CloudSmfContext *ctx = (struct CloudSmfContext *)o;
    int ret;

    LOG_DBG("State Machine: MQTT connecting run");

    switch (ctx->pending_event) {
        case CloudEvent::TIMER_RETRY:
            ret = tago.connect();
            if (ret == 0){
                ctx->mqtt_connected = true;
                ctx->mqtt_retry.attempt = 0;
                ctx->mqtt_retry.backoff_ms = cfg_retryInterval;
                smf_set_state(SMF_CTX(&cloud_smf_ctx), &cloud_states[MQTT_CONNECTED]);
            } else if (ctx->mqtt_retry.attempt < 5){
                ctx->mqtt_retry.attempt++;
                ctx->mqtt_retry.backoff_ms *= 2;
                k_work_schedule(&network_retry_workd, K_MSEC(ctx->mqtt_retry.backoff_ms));
            } else {
                smf_set_state(SMF_CTX(&cloud_smf_ctx), &cloud_states[ERROR_RECOVERY]);
            }
            break;

        default:
            return SMF_EVENT_PROPAGATE;
    }

    return SMF_EVENT_HANDLED;
}


/***************************************************************
 * @brief Exit function for the mqtt connecting state
 * 
 * @param o - pointer to user defined object
 **************************************************************/
static void mqtt_connecting_exit(void *o) {
    ARG_UNUSED(o);
    LOG_DBG("State Machine: MQTT connecting exit");
    k_work_cancel_delayable(&network_retry_workd);
    // Reset the public status to "Disconnected" on every exit from
    // MQTT_CONNECTING.  The only path that wants to keep going is the
    // success transition into MQTT_CONNECTED, whose entry immediately
    // overwrites this with "Connected".  All other paths (5-retry
    // failure → ERROR_RECOVERY, NET_L4_DISCONNECTED → NETWORK_DISCONNECTED)
    // need the display to stop reporting "Connecting" mid-state.
    val_cloudConnStatus = 0; // Disconnected
}


/***************************************************************
 * @brief Entry function for the mqtt connected state
 *        Subscribe to the update topic
 * 
 * @param o - pointer to user defined object
 **************************************************************/
static void mqtt_connected_entry(void *o) {
    struct CloudSmfContext *ctx = (struct CloudSmfContext *)o;
    int ret;

    LOG_INF("State Machine: Connected to MQTT");
    val_cloudConnStatus = 2; // Connected

    ret = tago.subscribeTopic(cfg_mqttTopicDn);
    if (ret == 0){
        ctx->mqtt_subscribed = true;
    } else {
        k_work_schedule(&network_retry_workd, K_MSEC(ctx->mqtt_retry.backoff_ms));
    }

    cloudUpdateParams(true);
    k_work_schedule(&cloud_update_workd, K_MSEC(cfg_cloudUpdateInterval));

}


/***************************************************************
 * @brief Run function for the mqtt connected state
 * 
 * @param o - pointer to user defined object
 **************************************************************/
static enum smf_state_result mqtt_connected_run(void *o) {
    struct CloudSmfContext *ctx = (struct CloudSmfContext *)o;


    switch (ctx->pending_event) {
        case CloudEvent::MQTT_DISCONNECTED:
            ctx->mqtt_connected = false;
            smf_set_state(SMF_CTX(&cloud_smf_ctx), &cloud_states[MQTT_CONNECTING]);
            break;

        case CloudEvent::TIMER_UPDATE_PARAMS:
            tago.retryMsgs();
            cloudUpdateParams(false);
            k_work_schedule(&cloud_update_workd, K_MSEC(cfg_cloudUpdateInterval));
            tago.brokerKeepAlive(cfg_cloudUpdateInterval);
            break;

        case CloudEvent::TIMER_RETRY:
            tago.subscribeTopic(cfg_mqttTopicDn);
            if (ctx->mqtt_subscribed){
                ctx->mqtt_retry.attempt = 0;
                ctx->mqtt_retry.backoff_ms = cfg_retryInterval;
            } else if (ctx->mqtt_retry.attempt < 5){
                ctx->mqtt_retry.attempt++;
                ctx->mqtt_retry.backoff_ms *= 2;
                k_work_reschedule(&network_retry_workd, K_MSEC(ctx->mqtt_retry.backoff_ms));
            }
            break;

        default:
            return SMF_EVENT_PROPAGATE;
    }

    return SMF_EVENT_HANDLED;
}

/***************************************************************
 * @brief Exit function for the mqtt connected state
 *        Subscribe to the update topic
 * 
 * @param o - pointer to user defined object
 **************************************************************/
static void mqtt_connected_exit(void *o) {
    struct CloudSmfContext *ctx = (struct CloudSmfContext *)o;

    LOG_DBG("State Machine: MQTT connected exit");

    k_work_cancel_delayable(&cloud_update_workd);
    k_work_cancel_delayable(&network_retry_workd);
    if (ctx->mqtt_connected){
        (void)tago.disconnect();
    }
    ctx->mqtt_connected = false;
    ctx->mqtt_subscribed = false;
    ctx->mqtt_retry.attempt = 0;
    ctx->mqtt_retry.backoff_ms = cfg_retryInterval;
    val_cloudConnStatus = 0; // Disconnected
}

/***************************************************************
 * @brief Entry function for the error recovery state
 *
 * @param o - pointer to user defined object
 *
 * For the cellular path the modem stack used to handle this by power-
 * cycling the radio, which guaranteed a fresh NET_L4_DISCONNECTED ->
 * NET_L4_CONNECTED cascade as a side effect.  Over Ethernet+FX30
 * there is nothing local for us to "restart" — the FX30 owns the WAN
 * and the W5500 link to it stays up regardless of whether MQTT or
 * NTP are healthy.  We back off, then on the timer either re-enter
 * NETWORK_CONNECTED (link still up — common case) or fall to
 * NETWORK_DISCONNECTED (link genuinely gone) based on the live L4
 * state tracked in the context.  See error_recovery_run().
 **************************************************************/
static void error_recovery_entry(void *o) {
    ARG_UNUSED(o);
    LOG_WRN("State Machine: Error Recovery - backing off before retry");
    k_work_schedule(&network_retry_workd, K_MSEC(60000));
}


/***************************************************************
 * @brief Run function for the error recovery state
 * 
 * @param o - pointer to user defined object
 **************************************************************/
static enum smf_state_result error_recovery_run(void *o) {
    struct CloudSmfContext *ctx = (struct CloudSmfContext *)o;

    if (ctx->pending_event == CloudEvent::TIMER_RETRY) {
        // Re-enter NETWORK_CONNECTED if the L4 link is currently up; this
        // re-runs network_connected_entry (which re-arms the clock-sync
        // work) and auto-descends into MQTT_CONNECTING via the SMF
        // initial-transition.  If the link is genuinely down, route to
        // NETWORK_DISCONNECTED and wait for the actual NET_L4_CONNECTED.
        // Without this distinction, an Ethernet build would deadlock here
        // any time the MQTT broker / NTP server failed without the link
        // bouncing.
        if (ctx->network_connected) {
            LOG_INF("Error Recovery: link still up, retrying via NETWORK_CONNECTED");
            smf_set_state(SMF_CTX(&cloud_smf_ctx), &cloud_states[NETWORK_CONNECTED]);
        } else {
            LOG_INF("Error Recovery: link down, falling back to NETWORK_DISCONNECTED");
            smf_set_state(SMF_CTX(&cloud_smf_ctx), &cloud_states[NETWORK_DISCONNECTED]);
        }
    }

    return SMF_EVENT_HANDLED;
}


/***************************************************************
 * @brief Handler function for MQTT events, called by Zephyr's
 *        MQTT library
 * 
 * @param client MQTT client
 * @param evt MQTT event
 **************************************************************/
void mqttEvtHandler(struct mqtt_client *client, const struct mqtt_evt *evt){
	int ret;
    uint8_t buffer[256];
    size_t bufferSize = sizeof(buffer)/sizeof(buffer[0]);

    ret = tago.evtHandler(client, evt, buffer, bufferSize);

    switch (evt->type) {
        case MQTT_EVT_CONNACK:
            post_cloud_event(CloudEvent::MQTT_CONNECTED);
            break;

        case MQTT_EVT_DISCONNECT:
            post_cloud_event(CloudEvent::MQTT_DISCONNECTED);
            break;

        default:
            break;
    }

    if (evt->type == MQTT_EVT_PUBLISH && ret > 0) { 
        // convert to string and decode, only accepts one key value pair
        std::string pubMsg = std::string(buffer, buffer + ret);
        struct key_value_pair pubKvp = {
            .key = pubMsg.substr(0, pubMsg.find(":")),
            .value = pubMsg.substr(pubMsg.find(":") + 1, std::string::npos)
        };

        if (pubKvp.key.length() == 0 || pubKvp.value.length() == 0){
            LOG_ERR("Error decoding MQTT message: %s", pubMsg.c_str());
            return;
        }

        int keyID;
        bool err;

        STOI(pubKvp.key, keyID, 10, err);
        if (err) {
            return;
        }

        switch(keyID) {
            case CLD_OCMD_SYSRUN:
                cloudInput(pubKvp.value, ocmd_sysRun, FS_NO_STORE);
                break;

            case CLD_CFG_DRIP_RATE:
                cloudInput(pubKvp.value, cfg_dripRate, FS_DRIP_RATE_SP);
                break;                

            case CLD_CFG_LOW_DRIP_SHDN_ENABLE:
                cloudInput(pubKvp.value, cfg_lowDripShDnEnable, FS_LOW_DRIP_SHDN_ENABLE);
                break;

            case CLD_CFG_LOW_DRIP_SHDN_DELAY:
                cloudInput(pubKvp.value, cfg_lowDripShDnDelay, FS_LOW_DRIP_SHDN_DELAY);
                break;

            case CLD_CFG_AUX1_ENABLE:
                cloudInput(pubKvp.value, cfg_aux1Enable, FS_AUX1_ENABLE);
                break;
            
            case CLD_CFG_AUX2_ENABLE:
                cloudInput(pubKvp.value, cfg_aux2Enable, FS_AUX2_ENABLE);
                break;

            case CLD_CFG_AUX3_ENABLE:
                cloudInput(pubKvp.value, cfg_aux3Enable, FS_AUX3_ENABLE);
                break;

            case CLD_CFG_SCHED_START:
                int hour, minute, dayValue;
                bool err1, err2, err3;
                val_schedStartStr = pubKvp.value;
                STOI(val_schedStartStr.substr(0, 2), hour, 10, err1);
                STOI(val_schedStartStr.substr(2, 2), minute, 10, err2);
                STOI(val_schedStartStr.substr(4, 2), dayValue, 16, err3);
                if (!err1 && !err2 && !err3) {
                    cfg_schedHour = hour;
                    cfg_schedMinute = minute;
                    std::bitset<7> days = dayValue;
                    for (int i = 0; i < 7; i++) {
                        cfg_schedDays[i] = days[6-i];
                    }
                    storageWrite(FS_SCHED_HOUR, &cfg_schedHour, sizeof(cfg_schedHour));
                    storageWrite(FS_SCHED_MINUTE, &cfg_schedMinute, sizeof(cfg_schedMinute));
                    storageWrite(FS_SCHED_DAYS, &cfg_schedDays, sizeof(cfg_schedDays));
                    UPDATE_STRING_VAR(val_schedStartStr, CLD_CFG_SCHED_VALIDATION, cloudParams, 0);
                } else {
                    LOG_ERR("Error setting scheduled start: hour %d, minute %d, day %d", err1, err2, err3);
                    val_schedStartStr = "E000000";
                    UPDATE_STRING_VAR(val_schedStartStr, CLD_CFG_SCHED_VALIDATION, cloudParams, 0);
                }
                
                break;

            case CLD_CFG_AUX1_LABEL:
                cloudInput(pubKvp.value, cfg_aux1Label, FS_AUX1_LABEL);
                break;

            case CLD_CFG_AUX2_LABEL:
                cloudInput(pubKvp.value, cfg_aux2Label, FS_AUX2_LABEL);
                break;

            case CLD_CFG_AUX3_LABEL:
                cloudInput(pubKvp.value, cfg_aux3Label, FS_AUX3_LABEL);
                break;

            default:
                LOG_WRN("Unexpected MQTT Variable: %d", keyID);
                break;
        }
 
    }
}


/***************************************************************
 * @brief Checks various parameters and loads into update queue
 *        if deadband and minimum time requirements are met
 *        Most variable use a 10s (10000) minimum invterval and
 *        4hr (14400000) maximum interval to send data. Data is
 *        sent on (re)connection.
 * 
 **************************************************************/
static void cloudUpdateParams(bool initialConn = false){

    if (tago.getConnectionStatus() == mqttBroker::CONNECTED) {

        // Runtime data
        CHECK_UPDATE_VAR(sts_sysRun,                CLD_STS_SYSRUN,                 cloudParams, 0, 10000, 14400000, initialConn);
        CHECK_UPDATE_VAR(inp_mtrRun,                CLD_INP_MTRRUN,                 cloudParams, 0, 10000, 14400000, initialConn);
        CHECK_UPDATE_VAR(ocmd_sysRun,               CLD_OCMD_SYSRUN,                cloudParams, 0, 10000, 14400000, initialConn);
        CHECK_UPDATE_VAR(alm_lowDripRateWar,        CLD_ALM_LOW_DRIP_WARN,          cloudParams, 0, 10000, 14400000, initialConn);
        CHECK_UPDATE_VAR(alm_lowDripRateShDn,       CLD_ALM_LOW_DRIP_SHDN,          cloudParams, 0, 10000, 14400000, initialConn);
        CHECK_UPDATE_VAR(alm_lowOil,                CLD_ALM_LOW_OIL,                cloudParams, 0, 10000, 14400000, initialConn);
        CHECK_UPDATE_VAR(val_dripRate,              CLD_VAL_DRIP_RATE,              cloudParams, 3, 60000, 14400000, initialConn);

        // Configuration Data - once every 24hr
        CHECK_UPDATE_VAR(cfg_dripRate,              CLD_CFG_DRIP_RATE,              cloudParams, 0, 10000, 86400000, 0);
        CHECK_UPDATE_VAR(cfg_lowDripShDnEnable,     CLD_CFG_LOW_DRIP_SHDN_ENABLE,   cloudParams, 0, 10000, 86400000, 0);
        CHECK_UPDATE_VAR(cfg_lowDripShDnDelay,      CLD_CFG_LOW_DRIP_SHDN_DELAY,    cloudParams, 0, 10000, 86400000, 0);
        CHECK_UPDATE_VAR(cfg_aux1Enable,            CLD_CFG_AUX1_ENABLE,            cloudParams, 0, 10000, 86400000, 0);
        CHECK_UPDATE_VAR(cfg_aux2Enable,            CLD_CFG_AUX2_ENABLE,            cloudParams, 0, 10000, 86400000, 0);
        CHECK_UPDATE_VAR(cfg_aux3Enable,            CLD_CFG_AUX3_ENABLE,            cloudParams, 0, 10000, 86400000, 0);

        if (!cloudParams.empty()){
            LOG_INF("Publishing Messages");
            tago.publishMsg(cfg_mqttTopicUp, cloudParams);
        }
    } else {
        LOG_WRN("Cloud not connected, could not update parameters");
    }

}


/***************************************************************
 * @brief Adapter callback for network events
 *        Translates Network module events to cloud SMF events
 *
 * @param event Network event from network helper
 **************************************************************/
static void network_event_adapter(Network::network_event event) {
    switch (event) {
        case Network::NETWORK_EVENT_L4_CONNECTED:
            LOG_INF("Network adapter: L4 connected");
            post_cloud_event(CloudEvent::NET_L4_CONNECTED);
            break;

        case Network::NETWORK_EVENT_L4_DISCONNECTED:
            LOG_INF("Network adapter: L4 disconnected");
            post_cloud_event(CloudEvent::NET_L4_DISCONNECTED);
            break;

        default:
            LOG_WRN("Network adapter: Unknown event %d", (int)event);
            break;
    }
}

/***************************************************************
 * @brief Main looping function for cloud, loops at 1000ms
 * 
 **************************************************************/
static void cloudMain(){
    // Wait until provisioning is complete before starting cloud operations
    while (provisioning_mode) {
        k_sleep(K_MSEC(500));
    }

    int ret;
    CloudEventMsg cloud_event_msg;

    smf_set_initial(SMF_CTX(&cloud_smf_ctx), &cloud_states[SYSTEM_INIT]);

    // Loop will run every cfg_cloudUpdateInterval in the MQTT_CONNECTED state
    while (true){
        ret = k_msgq_get(&cloud_event_queue, &cloud_event_msg, K_FOREVER);
        if (ret == 0) {
            cloud_smf_ctx.pending_event = cloud_event_msg.event;

            LOG_DBG("Executing with event: %d", (int)cloud_event_msg.event);

            ret = smf_run_state(SMF_CTX(&cloud_smf_ctx));
            if (ret < 0) {
                LOG_ERR("State machine stopping: %d", ret);
                return;
            }
        }      

        CHECK_STACK(cfg_cloudThreadStackSize);   
    };
}

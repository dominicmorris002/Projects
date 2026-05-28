#pragma once

#ifndef utility_hpp
#define utility_hpp

#include <stdint.h>
#include <zephyr/kernel.h>

class utyTimer{
private:
    bool        _enabled;
    bool        _done;
    int64_t     _preset;
    int64_t     _accum;
    int64_t     _start;

public:
    // constructor
    utyTimer(int64_t preset) : _enabled(false) , _done(false), _preset(preset), _accum(0), _start(false){}

    // get current preset
    int64_t preset(){
        return _preset;
    }

    // optional change of preset after initialization
    void preset(int64_t preset){
        _preset = preset;
    }

    // start/stop timer
    void enable(bool enable){
        // only acts on change of state, can set enable on each scan if needed
        // without affecting timer function
        if (enable && !_enabled){
            _enabled = enable;
            _start = k_uptime_get();
        }
        else if (!enable && _enabled){
            _enabled = enable;
            _start = 0;
            _accum = 0;
            _done = false;
        }
    }

    // update time and return if done
    bool done(){
        if (_enabled) {
            if (!_done) {
                _accum = k_uptime_get() - _start;
            }

            _done = (_accum >= _preset) ? true : false;
            return _done;
        } else {
            return false;
        }
    }

    // get accumulated time
    int64_t accum(){
        return _accum;
    }

    // reset timer
    void reset(){
        _accum = 0;
        _done = false;
        if (_enabled){
            _start = k_uptime_get();
        }
    }

};


// macro to report on stack usage
#define CHECK_STACK(total_stack) \
    do { \
        k_tid_t thread_id = k_current_get(); \
        size_t unused_stack_space = 0; \
        int stack_util = 0; \
        static int stack_util_last = 0; \
        int ret = k_thread_stack_space_get(thread_id, &unused_stack_space); \
        if (ret == 0) { \
            stack_util = 100-(100*unused_stack_space/total_stack); \
            if (stack_util > 80 && stack_util != stack_util_last){ \
                LOG_WRN("Stack utilization: %u%%, unused stack space: %u bytes", stack_util, unused_stack_space); \
            } \
            else if (stack_util != stack_util_last){ \
                LOG_DBG("Stack utilization: %u%%, unused stack space: %u bytes", stack_util, unused_stack_space); \
            } \
            stack_util_last = stack_util; \
        } else { \
            LOG_ERR("Failed to get stack space: %d", ret); \
        } \
    } while(0)


// macro to convert std::string to integer and handle errors
#define STOI(str, var, base, err) \
    do { \
        char* p; \
        var = std::strtol(str.c_str(), &p, base); \
        if (*p) { \
            LOG_ERR("Error converting string to int: %s", str.c_str()); \
            err = true; \
        } else { \
            err = false; \
        } \
    } while (0)


// macro to convert std::string to double and handle errors
#define STOD(str, var, err) \
    do { \
        char* p; \
        var = std::strtod(str.c_str(), &p); \
        if (*p) { \
            LOG_ERR("Error converting string to double: %s", str.c_str()); \
            err = true; \
        } else { \
            err = false; \
        } \
    } while (0)


// macro to convert std::string to boolean and handle errors
#define STOB(str, var, err) \
    do { \
        if (str == "1" || str == "True" || str == "true") { \
            var = true; \
            err = false; \
        } else if (str == "0" || str == "False" || str == "false") { \
            var = false; \
            err = false; \
        } else { \
            LOG_ERR("Error converting string to bool: %s", str.c_str()); \
            err = true; \
        } \
    } while (0)

#endif
#pragma once

#ifndef storage_hpp
#define storage_hpp

#include <cstddef>

enum fs_id {
    FS_NO_STORE = 0, // Don't store value
    FS_SERIAL_NUM = 1,
    FS_API_KEY,
    FS_DRIP_RATE_SP,
    FS_DRIP_RATE_CTRL_KP,
    FS_DRIP_RATE_CTRL_KI,
    FS_DRIP_RATE_CTRL_KD,
    FS_LOW_DRIP_SHDN_ENABLE,
    FS_LOW_DRIP_SHDN_DELAY,
    FS_SCHED_HOUR,
    FS_SCHED_MINUTE,
    FS_SCHED_DAYS,
    FS_AUX1_ENABLE,
    FS_AUX1_LABEL,
    FS_AUX2_ENABLE,
    FS_AUX2_LABEL,
    FS_AUX3_ENABLE,
    FS_AUX3_LABEL
};

int storageInit(bool clearStorage);
int storageInitVar(fs_id id, void *data, size_t size);
int storageRead(fs_id id, void *data, size_t size);
int storageWrite(fs_id id, const void *data, size_t size);

extern bool sts_fsMounted;

#endif

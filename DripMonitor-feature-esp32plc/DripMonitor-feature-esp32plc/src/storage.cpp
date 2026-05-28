// *************************************************************
// Includes
// *************************************************************
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/kvss/nvs.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>

#include "storage.hpp"

LOG_MODULE_REGISTER(app_storage);

// *************************************************************
// Global Variables
// *************************************************************
// type             // variable                     // value    // comment
bool                sts_fsMounted;                              // file system is ready


// *************************************************************
// Local Variables
// *************************************************************
// type             // variable                     // value    // comment
static struct nvs_fs fs;

int storageInit(bool clearStorage){
    int ret;
    struct flash_pages_info flashInfo;
    sts_fsMounted = false;

    fs.flash_device = FIXED_PARTITION_DEVICE(storage_partition);
    if (!device_is_ready(fs.flash_device)) {
        LOG_ERR("Flash device %s is not ready", fs.flash_device->name);
        return -ENODEV;
    }

    fs.offset = FIXED_PARTITION_OFFSET(storage_partition);
    ret = flash_get_page_info_by_offs(fs.flash_device, fs.offset, &flashInfo);
    if (ret) {
        LOG_ERR("Unable to get flash page info: %d", ret);
        return ret;
    }

    fs.sector_size = flashInfo.size;
    fs.sector_count = 2U;

    // mount the file system
    ret = nvs_mount(&fs);
    if (ret) {
        LOG_ERR("NVS mount failed: %d", ret);
        return ret;
    }
    else {
        sts_fsMounted = true;
    }

    if (clearStorage) {
        ret = nvs_clear(&fs);
        if (ret) {
            LOG_ERR("NVS clear failed: %d", ret);
            return ret;
        }
        else {
            LOG_INF("NVS cleared successfully");
        }
    }

    return 0;
}

int storageInitVar(fs_id id, void *data, size_t size){
    int ret;

    ret = storageRead(id, data, size);
    if (ret > 0) {
        LOG_INF("Flash storage ID %d recalled from storage", id);
    }
    else {
        LOG_INF("Initializing flash storage ID %d", id);
        storageWrite(id, data, size);
    }

    return ret;
}

int storageRead(fs_id id, void *data, size_t size){
    int ret;

    ret = nvs_read(&fs, id, data, size);
    if (ret == -ENOENT) {
        LOG_WRN("Flash storage ID %d not found", id);
    }
    else if (ret < 0) {
        LOG_ERR("Flash storage read error for ID %d: %d", id, ret);
    }

    return ret;
}

int storageWrite(fs_id id, const void *data, size_t size){
    int ret = 0;

    if (id > 0){
        ret = nvs_write(&fs, id, data, size);
        if (ret < 0) {
            LOG_ERR("Flash storage write error for ID %d: %d", id, ret);
        }
    }
    
    return ret;
}
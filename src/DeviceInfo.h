#ifndef SMARTVOICEBOX_DEVICE_INFO_H
#define SMARTVOICEBOX_DEVICE_INFO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *device_model;
    const char *firmware_version;
    char chip_model[32];
    uint16_t chip_revision;
    uint8_t cpu_cores;
    uint32_t cpu_mhz;
    uint32_t flash_bytes;
    uint32_t flash_firmware_bytes;
    uint32_t heap_total_bytes;
    uint32_t heap_free_bytes;
    uint32_t psram_total_bytes;
    uint32_t psram_free_bytes;
} device_info_snapshot_t;

void device_info_get_snapshot(device_info_snapshot_t *info);
void device_info_log_snapshot(const device_info_snapshot_t *info);

#ifdef __cplusplus
}
#endif

#endif

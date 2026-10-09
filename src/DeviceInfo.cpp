#include "DeviceInfo.h"
#include "FirmwareVersion.h"

#include <Arduino.h>
#include <stdio.h>

void device_info_get_snapshot(device_info_snapshot_t *info)
{
    if (!info) return;
    *info = {};
    // N16R8 is the module variant confirmed by the owner, not an API result.
    info->device_model = "ESP32-S3 N16R8";
    info->firmware_version = SMARTVOICEBOX_FIRMWARE_VERSION;
    snprintf(info->chip_model, sizeof(info->chip_model), "%s", ESP.getChipModel());
    info->chip_revision = ESP.getChipRevision();
    info->cpu_cores = ESP.getChipCores();
    info->cpu_mhz = getCpuFrequencyMhz();
    info->flash_bytes = ESP.getFlashChipSize();
    // getSketchSize verifies the running image: cache it instead of scanning
    // Flash on every page refresh. This excludes other Flash data/partitions.
    static const uint32_t firmware_bytes = ESP.getSketchSize();
    info->flash_firmware_bytes = firmware_bytes;
    // Arduino reports the internal heap separately from the external PSRAM.
    info->heap_total_bytes = ESP.getHeapSize();
    info->heap_free_bytes = ESP.getFreeHeap();
    info->psram_total_bytes = ESP.getPsramSize();
    info->psram_free_bytes = ESP.getFreePsram();
}

void device_info_log_snapshot(const device_info_snapshot_t *info)
{
    if (!info) return;
    Serial.printf(
        "[DEVICE_INFO] model=%s chip=%s rev=%u.%u fw=%s cores=%u cpu_mhz=%u "
        "flash_bytes=%u flash_firmware_bytes=%u heap_free=%u heap_total=%u "
        "psram_free=%u psram_total=%u\n",
        info->device_model, info->chip_model,
        static_cast<unsigned>(info->chip_revision / 100),
        static_cast<unsigned>(info->chip_revision % 100),
        info->firmware_version, static_cast<unsigned>(info->cpu_cores),
        static_cast<unsigned>(info->cpu_mhz), static_cast<unsigned>(info->flash_bytes),
        static_cast<unsigned>(info->flash_firmware_bytes),
        static_cast<unsigned>(info->heap_free_bytes),
        static_cast<unsigned>(info->heap_total_bytes),
        static_cast<unsigned>(info->psram_free_bytes),
        static_cast<unsigned>(info->psram_total_bytes));
}

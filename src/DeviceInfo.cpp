#include "DeviceInfo.h"
#include "FirmwareVersion.h"
#include "SystemBranding.h"

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_image_format.h"
#include "esp_ota_ops.h"
#include "esp_private/esp_clk.h"
#include <stdio.h>

void device_info_get_snapshot(device_info_snapshot_t *info)
{
    if (!info) return;
    *info = {};
    // N16R8 is the module variant confirmed by the owner, not an API result.
    info->device_model = "ESP32-S3 N16R8";
    info->firmware_version = SMARTVOICEBOX_FIRMWARE_VERSION;
    info->system_name = BREEZE_OS_NAME;
    esp_chip_info_t chip = {};
    esp_chip_info(&chip);
    snprintf(info->chip_model, sizeof(info->chip_model), "%s", "ESP32-S3");
    info->chip_revision = chip.revision;
    info->cpu_cores = chip.cores;
    info->cpu_mhz = esp_clk_cpu_freq() / 1000000;
    esp_flash_get_size(nullptr, &info->flash_bytes);
    // Verify once. This counts the running image, excluding other partitions.
    static const uint32_t firmware_bytes = []() -> uint32_t {
        const esp_partition_t *partition = esp_ota_get_running_partition();
        if (!partition) return 0;
        const esp_partition_pos_t position = {partition->address, partition->size};
        esp_image_metadata_t image = {};
        return esp_image_verify(ESP_IMAGE_VERIFY_SILENT, &position, &image) == ESP_OK
            ? image.image_len : 0;
    }();
    info->flash_firmware_bytes = firmware_bytes;
    info->heap_total_bytes = heap_caps_get_total_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    info->heap_free_bytes = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    info->psram_total_bytes = heap_caps_get_total_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    info->psram_free_bytes = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void device_info_log_snapshot(const device_info_snapshot_t *info)
{
    if (!info) return;
    printf(
        "[DEVICE_INFO] system=%s model=%s chip=%s rev=%u.%u fw=%s cores=%u cpu_mhz=%u "
        "flash_bytes=%u flash_firmware_bytes=%u heap_free=%u heap_total=%u "
        "psram_free=%u psram_total=%u\n",
        info->system_name, info->device_model, info->chip_model,
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

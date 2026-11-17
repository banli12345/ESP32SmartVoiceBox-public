set(APP_DIR "${CMAKE_CURRENT_LIST_DIR}/../src")
file(GLOB APP_SOURCES "${APP_DIR}/*.cpp" "${APP_DIR}/*.c")
file(GLOB GUI_SOURCES
    "${APP_DIR}/ui/generated/*.c"
    "${APP_DIR}/ui/generated/images/*.c"
    "${APP_DIR}/ui/generated/guider_fonts/*.c"
    "${APP_DIR}/ui/custom/*.c")
list(FILTER GUI_SOURCES EXCLUDE REGEX
    "/(pinyin_full_dict|lv_font_SourceHanSerifSC_Regular_16|lv_font_chinese_city_32)\\.c$")

idf_component_register(
    SRCS ${APP_SOURCES} ${GUI_SOURCES}
    INCLUDE_DIRS
        "${APP_DIR}" "../include"
        "${APP_DIR}/ui" "${APP_DIR}/ui/generated"
        "${APP_DIR}/ui/generated/images" "${APP_DIR}/ui/custom"
    REQUIRES
        lvgl minimp3 esp_lcd esp_driver_gpio esp_driver_ledc esp_driver_i2c
        esp_driver_i2s esp_driver_uart esp_driver_sdmmc
        fatfs sdmmc nvs_flash esp_wifi esp_event esp_netif bt
        esp_http_client esp_http_server esp-tls mbedtls json lwip esp_timer
        esp_app_format app_update esp_partition spi_flash esp_psram bootloader_support
)
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
set_target_properties(${COMPONENT_LIB} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES)

# LVGL's pinyin public header lives here in v8.3.1.
idf_component_get_property(LVGL_DIR lvgl COMPONENT_DIR)
target_include_directories(${COMPONENT_LIB} PRIVATE "${LVGL_DIR}/src/extra/others/ime")

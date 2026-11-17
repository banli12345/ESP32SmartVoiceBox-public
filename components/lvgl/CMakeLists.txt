file(GLOB_RECURSE LVGL_SOURCES "src/*.c")
idf_component_register(
    SRCS ${LVGL_SOURCES}
    INCLUDE_DIRS "." "src" "src/extra/others/ime"
    REQUIRES esp_timer heap
)
target_compile_definitions(${COMPONENT_LIB} PUBLIC LV_CONF_INCLUDE_SIMPLE=1)

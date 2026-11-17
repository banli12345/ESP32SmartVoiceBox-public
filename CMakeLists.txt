cmake_minimum_required(VERSION 3.16)

if(DEFINED IDF_TARGET AND NOT IDF_TARGET STREQUAL "esp32s3")
    message(FATAL_ERROR "This board requires IDF_TARGET=esp32s3")
endif()
set(IDF_TARGET "esp32s3" CACHE STRING "Target chip")
set(COMPONENTS main)
option(BREEZE_GENERIC_BUILD "Build without local WiFi provisioning defaults" ON)
if(BREEZE_GENERIC_BUILD)
    add_compile_definitions(BREEZE_GENERIC_BUILD=1)
endif()

# LVGL and the application must see exactly the same board configuration.
include_directories("${CMAKE_CURRENT_LIST_DIR}/include")
add_compile_definitions(LV_CONF_INCLUDE_SIMPLE=1)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32_smart_voice_box VERSION 1.28.0)

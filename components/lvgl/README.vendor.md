# LVGL 8.3.1

Upstream: https://github.com/lvgl/lvgl/tree/v8.3.1

This directory vendors `src/`, `lvgl.h`, and `LICENCE.txt` from the project's
existing LVGL 8.3.1 dependency. They are committed with the project so a clean
ESP-IDF checkout never needs a PlatformIO cache. The local `CMakeLists.txt`
registers these unchanged sources as an ESP-IDF component; board configuration
comes from `include/lv_conf.h`.

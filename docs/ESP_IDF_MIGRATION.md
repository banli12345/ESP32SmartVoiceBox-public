# 当前原生ESP-IDF工程

Breeze OS 1.28.0，目标ESP32-S3 N16R8，ESP-IDF 5.5.x（验证5.5.6），LVGL 8.3.1。根目录CMake构建；旧第三方参考驱动不参与当前应用链接。

| 模块 | 当前实现 |
|---|---|
| 入口/调度 | app_main、FreeRTOS、GUI与后台网络分开。 |
| 显示/触摸 | esp_lcd RGB双缓冲，14MHz像素时钟，变化区域同步；GT911状态优先、目标10ms轮询。 |
| 存储 | SDMMC 1-bit FATFS/VFS，参数NVS。 |
| 网络 | esp_wifi/esp_netif、8项WiFi记录、NAPT、SNTP、HTTP/TLS、cJSON。 |
| AI | HTTPS直连、JSON导入NVS、上一轮上下文、本地闹钟解析。 |
| 语音 | 完整PCM先缓存SD，再从文件预取到有界PSRAM队列；I2S输出，结束清理。 |
| 时钟 | 单闹钟、重复星期、时长、倒计时，复用后台服务与已有音频仲裁。 |
| 游戏 | 六款，新增11×11五子棋和100张随机三消；退出释放页面。 |
| OTA | 两个4MiB应用槽、校验、启动健康确认与回滚；GCC TLS预算检查为0。 |

引脚与时序见src/board_pins.h。LCD初始化SPI和SD共用GPIO48/47，先初始化LCD再挂SD，运行中不能复用该SPI再次初始化屏。仅新板首次部署需要整套引导、分区和初始OTA数据；日常只上传应用BIN，不擦NVS，也不固定写入地址。

自己的天气和AI配置见[API指南](API_CONFIGURATION.md)。天气Key编译，AI Key通过SD/串口导入NVS。SD字体、通用图标、词库和媒体独立部署，两枚应用图标已在固件中。[编译部署](GETTING_STARTED.md) · [使用图解](USER_GUIDE.md) · [硬件](HARDWARE.md)

当前城市字库、中文键盘、40枚天气图标、显式音乐下载、相册内存回退和双轴唤醒见[本版说明](RELEASE_V1.28.0.md)与[字库部署](FONTS_AND_MOTION.md)。

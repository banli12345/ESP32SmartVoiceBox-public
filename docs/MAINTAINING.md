# 公开源码维护

本公开main只保留当前Breeze OS 1.28.0的一个无父提交快照，后续发布替换为新快照，不追加旧版本历史或标签；firmware/只保留当前通用固件ZIP、校验与来源清单，不累积旧版固件。私密主线保留完整开发历史与API配置。

更新前先在仓库外保存自己的配置，再重新克隆。快照更替后不能使用普通快进pull获取，避免混入个人配置。填写自己的Key见[API配置](API_CONFIGURATION.md)，构建和SD资源见[部署](GETTING_STARTED.md)。真实配置、个人媒体、NVS/Flash备份不提交。

发布时从已审查源码导出，排除私密配置、旧版本和本机产物，检查真实密钥、历史凭据、个人路径、部署地址、文档链接，再做无配置干净构建。同步前保存本机备份，使用明确旧main SHA的force-with-lease防止覆盖新提交；私密主线使用普通追加推送。

固件更新保留WiFi、热点、亮度、音量、音色和闹钟设置，不例行擦除NVS；日常OTA写非活动槽，不能固定写app1地址。只有首次部署需要完整bootloader、分区表和初始OTA数据。

主机测试依赖各自编译器和部分SD字库；公开源码没有本机工具缓存。API配置生成器可运行 `python -m unittest discover -s tests -p test_configure_services.py`。ELF检查脚本 `tests/firmware_startup_layout_test.py` 用于确认当前每任务GCC TLS预算为0。设备确认范围见[当前说明](WHATS_NEW.md)。

## 缓存参数与测速

新克隆使用sdkconfig.defaults；已有sdkconfig需要同步CONFIG_LWIP_TCP_WND_DEFAULT=32768、CONFIG_LWIP_TCP_RECVMBOX_SIZE=24，否则旧值不会自动覆盖。接收、合并写入和临时省电在src/AiSpeech.cpp，界面状态在src/Web_ai.cpp。省电恢复遇到临时错误最多重试3次；持续失败会记录，不假装成功。

屏上均速包含请求等待、写卡和flush；串口cache_pace的bps仅按正文接收与合并写入计算。比较新旧版本要固定网络、文本和音色，并使用同一种口径。用户约285KiB/s是语音缓存优化后的界面读数，旧约267KiB/s来自1.25.3正文日志，不直接算百分比。验证界限见[最新说明](RELEASE_V1.28.0.md)。

## 当前内存与图片检查

保留2.5MiB LVGL池，区分预留量与实际使用。开启指令/只读数据PSRAM加速时，大型const也占PSRAM；城市数据修改后运行weather_city_packed_host_test.py全量比对。相册GUI须在解绑和缓存失效后释放图像，加载中取消不得发布旧数据；检查photo_loader_host_test.py、photo_view_ui_host_test.py和screen_idle_host_test.py。构建结束后对最终ELF运行keyboard_firmware_layout_test.py、firmware_startup_layout_test.py、firmware_psram_budget_test.py，并把BIN内ELF散列与实际文件对应。预算不等于运行时空闲。

## 网页配置与通用镜像

默认 `BREEZE_GENERIC_BUILD=ON`；API Key 由 NVS/SD 提供，初始 WiFi 缓冲应为空。最终 ELF 可运行 `firmware_generic_config_test.py`。配置保存不立即更改运行快照，显式重启统一启用；服务配置无需 SD，媒体和语音仍需。SD 导入成功后删除文件，网页改动不回写电脑主文件。

HTTP/配置检查见 `service_config_web_host_test.py`，实际网页脚本检查见 `service_config_page_test.py`；后者需 Node，部分主机测试使用维护者本机工具与字体，不能把缺少依赖当作已验证。文档中的图片与用户确认范围分开记录。


本轮完整 Python 回归发现电脑素材工具取消视频转换后临时文件仍被占用（Windows WinError 32），旧1.26.3快照亦可重现。53项中其余52项通过；发布记录应保留此例外，不写成全部通过。对应 `tests/test_sd_media_conversion.py` 的 `test_cancel_live_ffmpeg_worker`，电脑转换/取消逻辑本次未变更。


## 当前通用包的维护

用户已授权公开提供当前通用固件及首次烧录教程。每次源码快照更新时保留/替换 `firmware/` 的当前包、README、SHA256SUMS与manifest；仅对已审查的具体ZIP路径豁免忽略规则，不放开所有ZIP/BIN。公开现用完整版本，不保留旧包，不需要创建历史固件Release。禁止导出包含个人NVS的设备Flash转储，发布前校验空NVS区域、通用WiFi初始值、API Key、镜像来源及内层文件散列。Factory和OTA用途不得混写。

当前包是已验证的1.28.0通用构建，运行代码未因补教程而变化；主机/实机确认边界沿用对应发布说明。后续更换固件时同步更新首页下载链接、首次教程、清单和哈希，并保留原硬件/实机演示。

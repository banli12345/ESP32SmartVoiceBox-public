# 当前通用固件：Breeze OS 1.28.0

**[直接下载通用固件 ZIP](https://github.com/banli12345/ESP32SmartVoiceBox-public/raw/refs/heads/main/firmware/BreezeOS-1.28.0-Generic.zip)** · [首次烧录图文步骤](../docs/FIRST_FLASH.md) · [硬件与屏幕兼容性](../docs/HARDWARE.md)

仅适用于本项目接线的 ESP32-S3 **N16R8（16 MB Flash、8 MB OPI PSRAM）** 与配套 ST7701 480×480 RGB / GT911 屏幕。同尺寸或同为 ST7701 不保证初始化参数、时序、FPC 定义相同。换屏前先确认[屏幕参考和驱动说明](../docs/HARDWARE.md#屏幕购买参考与兼容性)。

| ZIP 内文件 | 用法 |
| --- | --- |
| `BreezeOS-1.28.0-Factory.bin` | 新板首次烧录，从 `0x0` 写入；含引导、分区、初始 OTA 数据及应用，会覆盖配置区域 |
| `BreezeOS-1.28.0-OTA.bin` | 已安装本项目 IDF 系统的设备，通过热点网页上传，在“系统更新”安装 |
| `config/*.example.json` | 占位符示例，也可直接通过热点网页填写自己的配置 |
| `README.txt`、`licenses/` | 包内说明及组件许可证 |

ZIP 不含真实 API Key、个人 WiFi 默认值或设备 NVS/整片 Flash 转储。服务配置由每位用户在网页填写自己的值；NVS 保存后掉电保留。SD 字库、输入法、图标/壁纸与媒体需另行准备，ZIP 不包含完整 SD 资源。

下载校验见 [SHA256SUMS.txt](SHA256SUMS.txt)，镜像单项大小、散列和构建来源见 [manifest.json](manifest.json)。仓库只保留当前版本通用包，后续替换旧包，不累积历史固件。

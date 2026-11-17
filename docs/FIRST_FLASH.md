# 首次烧录 Breeze OS 1.28.0

[下载当前通用固件包](https://github.com/banli12345/ESP32SmartVoiceBox-public/raw/refs/heads/main/firmware/BreezeOS-1.28.0-Generic.zip) · [返回首页](../README.md) · [硬件连接表](HARDWARE.md) · [配置自己的 API](API_CONFIGURATION.md)

新板首次烧录使用 **Factory.bin，从 `0x0` 写入**。已运行本项目 IDF 系统的设备日常更新使用 OTA.bin。Factory 是合并镜像文件名，不是要求在烧录工具里选择 Factory 工作模式；下面使用 Develop 模式。

## 1. 先确认硬件与屏幕

- 主控：ESP32-S3 **N16R8**，16 MB Flash、8 MB OPI PSRAM。图纸曾标 N8R8，实际容量以模组为准，8 MB Flash 板不能直接烧当前16 MB布局。
- 屏幕：本项目 **ST7701、480×480、RGB 接口、GT911 触摸**，且引脚按[连接表](HARDWARE.md)连接。背光、供电及功放跳线也需匹配。
- 数据线：USB 线必须能传数据。本项目 CH340K 数据 USB 口使用 UART 下载，电脑应识别出实际 COM 端口；其他板使用原生 USB 时按其下载方式处理。

**[项目提供的屏幕购买参考（淘宝）](https://e.tb.cn/h.jP6J6hg39QyTTQC?tk=JCbl64U5hAT)**。这是用户提供的购买参考链接，未从这里核实当前商品详情/选项。下单前向商家确认驱动、触摸芯片、FPC 针脚、供电、初始化代码和 RGB 时序。

**不同商家的屏幕驱动和初始化参数可能不同，即使尺寸、分辨率或驱动名称相同，也可能黑屏、花屏、偏色或触摸无响应。当前固件不会自动识别任意屏幕；需按商家资料适配驱动后重新构建。** 相关入口见[屏幕兼容性](HARDWARE.md#屏幕购买参考与兼容性)。

## 2. 下载与准备

1. 下载并解压 [BreezeOS-1.28.0-Generic.zip](https://github.com/banli12345/ESP32SmartVoiceBox-public/raw/refs/heads/main/firmware/BreezeOS-1.28.0-Generic.zip)。在 GitHub 文件页面点击下载原始文件也可以，不能把 ZIP 直接交给烧录工具。
2. 解压后确认有 `BreezeOS-1.28.0-Factory.bin` 和 `BreezeOS-1.28.0-OTA.bin`。首次只选择 Factory。
3. 准备 FAT32 SD 卡。字体、输入法、图标、壁纸和媒体另按[素材部署](GETTING_STARTED.md#素材准备)生成/复制；`fonts`、`ime`、`icons`、`wallpapers` 等目录直接放 SD 根目录，不多套一层目录。固件 ZIP 不含完整 SD 素材；OTA 也不会安装它们。
4. 从[乐鑫官网下载 Flash Download Tools](https://www.espressif.com/en/support/download/other-tools)，解压后运行。关闭串口助手、IDF monitor 等占用设备串口的程序。

可选：在 PowerShell 用 `Get-FileHash .\BreezeOS-1.28.0-Generic.zip -Algorithm SHA256` 对照[校验文件](../firmware/SHA256SUMS.txt)。

## 3. 填写烧录工具

| 项目 | 填写值 |
| --- | --- |
| ChipType | **ESP32-S3** |
| WorkMode | **Develop** |
| LoadMode | 本项目 CH340K 串口使用 **UART** |
| 第一行文件 | 解压出的 `BreezeOS-1.28.0-Factory.bin`，勾选该行 |
| 地址 | **`0x0`** |
| COM | 设备管理器中实际出现的端口，不能照抄教程的 COM3 |
| BAUD | **460800**；连接不稳定可降为115200 |
| DoNotChgBin | **勾选**，保留镜像构建参数 |
| 镜像参数 | **DIO、80 MHz、16 MB Flash** |

只勾选上述一行，Factory 已包含配套 bootloader、分区表、初始 OTA 数据和应用，不用再添加这些文件。工具3.9.10及之后默认不可编辑 SPI SPEED/MODE，沿用镜像即可；旧版可编辑时核对 DIO/80 MHz。操作和参数含义见[乐鑫官方工具说明](https://docs.espressif.com/projects/esp-test-tools/en/latest/esp32s3/production_stage/tools/flash_download_tool.html)。

**Factory 会覆盖配置区域，已有 WiFi、API Key 等配置可能丢失。它用于首次部署，不能上传到系统 OTA 页面。** 新板无需例行点击 ERASE 整片擦除。

## 4. 进入下载模式并烧录

1. 接通数据 USB，确认设备有供电，选择正确 COM。
2. 按住 **BOOT**，按一下 **EN/RST**，松开 EN/RST，继续保持 BOOT。
3. 在工具点 **START**，连接成功、开始写入后松开 BOOT。
4. 等待 **FINISH**，期间保持供电。完成后按一下 EN/RST，正常启动进入 Breeze OS。

若一直停在连接状态，检查数据线/串口是否正确、串口助手是否已关闭，再按上述顺序进入下载模式；有自动下载电路的板卡也可能无需手动按键。

## 5. 首次联网与自己的密钥

1. 装好配套 SD 资源，在设备“设置 → WiFi”连接可上网的2.4 GHz网络。通用固件没有预设个人 WiFi。
2. 从顶部下滑开启热点，长按热点查看名称、密码和网址。
3. 手机连接该热点，浏览器打开设备显示的网址（默认 `http://172.31.42.1/`），在页面底部进入“服务配置”，或直接访问 `/settings`。
4. 填自己的 AI 基础地址、模型与 Key；天气卡填实况、预报、生活指数地址及 Key。两张卡分别保存，再点 **“重启并生效”**。首次配置必须填真实 Key；后续已有 Key 留空可保留，不回显旧 Key。
5. 重启后设备重新连接上游 WiFi，检查天气/生活指数及 AI。手机需要重新连接热点再刷新网页。

配置保存在设备 NVS，断电保留；保存配置无需 SD，但语音缓存、字库、音乐和媒体仍需要 SD。服务协议要与现有 DeepSeek / 心知客户端兼容，详见[API 配置教程](API_CONFIGURATION.md)。

## 6. 以后用 OTA 更新

上传 ZIP 内的 **OTA.bin** 到设备热点网页，再在设备“设置 → 系统更新 → 检查更新 → 安装并重启”。保持供电和 SD 卡可用，系统会写非活动应用分区；日常 OTA 保留 NVS 和 SD。

当前网页只检查你上传到 SD 的候选包，没有独立互联网查新服务器。检查完成后在设备信息确认系统版本。具体流程见[网页上传与 OTA](WEB_UPLOAD_OTA.md)。

## 也可使用已有 ESP-IDF 环境

本机配套 ESP-IDF 5.5.6 / esptool 4.x 可在 ZIP 解压目录执行（将 COM3 改为实际串口）：

```powershell
python -m esptool --chip esp32s3 --port COM3 --baud 460800 write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB 0x0 BreezeOS-1.28.0-Factory.bin
```

这是合并镜像单文件烧录；源码构建及 `idf.py flash` 的完整部署另见[编译指南](GETTING_STARTED.md)。此教程没有实际替你操作设备，换屏、新板首次配置等验证范围见[本版说明](RELEASE_V1.28.0.md)。

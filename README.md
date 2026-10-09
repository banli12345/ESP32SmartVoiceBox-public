# ESP32SmartVoiceBox

基于 **ESP32-S3 + LVGL 的智能桌面终端**，使用 480×480 RGB 触摸屏，将时钟、天气、音乐、照片、视频、中文 AI 对话和小游戏集中在一个设备上。运行素材保存在 SD 卡，WiFi、音量、时间和天气缓存通过 NVS 保存。

**当前版本：v1.6，2026-10-09 发布。** 新增设备软复位和屏幕重启，采用淡草绿渐变主题与蓝天白云天气页面，完善中文名称显示，修复视频音轨无声，并将壁纸页温度图标缩小为 40×40。继承 v1.5 的设备信息和 HTTP/HTTPS 文字浏览器，以及串口素材上传、LRC 原样上传与 `server/` 备份。`main` 与 `v1.6` 标签对应本次发布；旧版本标签保持原位置。说明见 [v1.6 发布说明](docs/RELEASE_V1.6.md)，状态见[开发进度](#开发进度)。

v1.6 新增设置中的“重启设备”：点击后选择“取消”或“重启”，确认后重新启动 ESP32 并返回主界面，保留已保存的配置和 SD 文件。开机约 1 秒后，使用已保存的 WiFi 名称与密码发起连接；未保存网络时需手动配网。

v1.6 还新增设置中的“重启屏幕”：点击后短暂关闭背光，在 VSYNC 帧边界停止 RGB 输出，重新初始化已有 RGB 控制器并启动 DMA，再恢复原亮度并重绘页面，WiFi 连接和 ESP32 运行状态保留。沿用现有屏幕和帧缓冲，不重复使用与 SD 卡共用的屏幕初始化 SPI 引脚。同步等待最多 500 ms，失败时可再次点击；开发阶段串口记录三次重启成功、每次约 60 ms，实机恢复验证正常。

v1.6 的应用列表、SD 状态、设置、WiFi、设备信息、素材列表、浏览器等页面采用淡草绿到米白色的渐变背景，搭配浅色卡片和深绿色文字，配色集中在 `src/UITheme.h`。天气页面独立使用蓝天渐变、静态白云和太阳装饰，以及三日预报卡片；实际天气图标仍随天气数据变化。音乐、2048 和壁纸时钟页保留改主题前的样式；退出天气时恢复应用列表的草绿渐变。热点、视频和照片列表使用 20px 中文字库；壁纸页的温度图标改为深色轮廓、白色管体和红色液柱，尺寸由 64×64 缩小为 40×40，温度数字在下方居中。

v1.6 的视频音轨改为直接通过 VFS 读取 SD 文件，修复播放时 WAV 文件头读取异常导致的无声。短读时保留已取得的 PCM 数据并最多重试三次，持续失败才停止音频并记录文件与读取位置；静音计数增加了防回绕处理。真实 WAV 的六次逐字节读取循环，以及短读、临时读取失败、退出取消、音频切换和计数回绕测试均已通过。开发阶段实机测试记录到三次音轨启动（包含重新打开和中文名视频）及一次完整循环，无音轨错误或缓冲耗尽；实机播放验证持续正常。验证使用的自动播放代码已移除。

## 文档导航

- [功能介绍](#功能介绍)
- [硬件与接线](#硬件与接线)
- [快速开始](#快速开始)
- [SD 卡与素材格式](#sd-卡与素材格式)
- [工程结构](#工程结构)
- [版本与开发进度](#版本记录)
- [常见问题](#常见问题)
- [完整部署与素材转换指南](docs/GETTING_STARTED.md)
- [串口素材上位机使用与验证](docs/SERIAL_MEDIA_UPLOADER.md)
- [v1.6 发布说明](docs/RELEASE_V1.6.md)
- [AI 服务器源码与部署配置](server/README.md)
- [架构、模块与服务器协议说明](docs/PROJECT_GUIDE.html)（HTML 文档，下载后用浏览器打开）

## 功能介绍

| 功能 | 说明 |
| --- | --- |
| 桌面主界面 | 时间、日期、天气、网络状态、陀螺仪温度与帧率显示，支持更换壁纸 |
| 设置 | WiFi 配网、屏幕亮度、音量、城市与壁纸选择，保存设备配置；设备信息显示 ESP32-S3N16R8、固件版本、CPU 和内存，容量显示已用／总量（Flash 已用仅统计当前固件） |
| 文字浏览器 | 网址输入、HTTP/HTTPS 文字网页、正文分页与滚动、链接跳转、后退、刷新及取消；使用说明见 [文字浏览器](docs/text-browser.md) |
| 天气与日历 | 当前天气、三日预报、月历和月份切换；断网显示已保存的时间、天气缓存 |
| 音乐与闹铃 | SD 卡 WAV 播放、同名 LRC 歌词、上一首/下一首、播放模式与闹铃 |
| 中文 AI 助手 | 拼音键盘输入、候选字、HTTP/WebSocket 对话和服务器返回的 TTS 播报 |
| 照片与视频 | SD 卡照片浏览、RVF 视频和同名 WAV 音轨播放；按音频进度同步画面 |
| 串口素材上传 | Windows 上位机转换原始图片、音乐与视频，通过串口分块上传至 SD 卡 |
| 2048 | 触摸滑动合并数字、分数显示 |
| 陀螺仪与平衡游戏 | MPU6050 姿态、温度显示，以及通过设备倾斜控制的小球游戏 |

AI 问答和 TTS 使用配套服务器，源码已备份到 [`server/`](server/)：Node.js 提供 HTTP/WebSocket 和网页，DeepSeek 生成文字，edge-tts 与 FFmpeg 生成语音。服务器部署步骤见[服务器说明](server/README.md)。目前设备 AI 页面使用文字输入。

## 硬件与接线

| 部件 | 当前工程使用的配置 |
| --- | --- |
| 主控 | ESP32-S3，CPU 240 MHz，16 MB Flash，8 MB OPI PSRAM |
| 显示屏 | ST7701 RGB 接口，480×480，RGB565 |
| 触摸 | GT911，I²C 地址 `0x5D` |
| 传感器 | MPU6050，与触摸共用 I²C 总线 |
| 音频输出 | MAX98357A I²S 功放及扬声器 |
| 存储 | SD_MMC，1-bit 模式；部署建议使用 FAT32 文件系统 |

主要引脚如下，完整 RGB 数据引脚见[部署指南](docs/GETTING_STARTED.md#硬件接线)。接入其他板卡时，应同时核对引脚、屏幕初始化序列和 RGB 时序。

| 信号 | GPIO |
| --- | --- |
| LCD DE / VSYNC / HSYNC / PCLK | 18 / 17 / 16 / 21 |
| LCD 初始化 CS / SCK / MOSI | 39 / 48 / 47 |
| 屏幕背光 | 38 |
| I²C SDA / SCL | 19 / 45 |
| SD CLK / CMD / D0 | 48 / 47 / 41 |
| SD DAT2 控制线 | 42 |
| I²S BCLK / LRCLK / DOUT | 1 / 2 / 40 |

LCD 初始化 SPI 和 SD 共用 GPIO48、GPIO47。工程先初始化屏幕，再释放对应引脚并挂载 SD；这些引脚不能随意改作其他外设用途。

## 快速开始

准备 Git、Python 和 PlatformIO。以下命令在 Windows PowerShell 中执行：

```powershell
git clone --branch v1.6 https://github.com/OWNER/ESP32SmartVoiceBox-public.git
cd ESP32SmartVoiceBox-public
py -3 -m pip install platformio
```

**首次配置请先阅读[编译环境](docs/GETTING_STARTED.md#编译环境)：工程使用 Arduino-ESP32 3.x 接口，`platformio.ini` 中的 `platform = espressif32` 尚未锁定平台来源。** 已验证的环境为 pioarduino 55.03.312、Arduino-ESP32 3.3.12、ESP-IDF 5.5.5 和 LVGL 8.3.1。仅安装默认平台可能得到不兼容的 Arduino 2.x。

完成平台配置后，编译、查看端口、上传固件并打开日志：

```powershell
py -3 -m platformio run
py -3 -m platformio device list
# COM3 是示例，请替换为设备实际串口；上传前关闭串口监视器
py -3 -m platformio run --target upload --upload-port COM3
py -3 -m platformio device monitor --port COM3 --baud 115200
```

需要启用天气服务、AI HTTP 和 WebSocket 时，先复制 [`src/private_config.h.example`](src/private_config.h.example) 为本地忽略文件 `src/private_config.h` 并填入自己的配置。设备联网后同步时间和天气。

应用固件输出到 `.pio/build/esp32-s3-devkitc-1/firmware.bin`。**烧录固件与更新 SD 卡素材是两步操作**：将生成的 `sd_card_files/` 内部目录复制到 SD 卡根目录，再重启设备检查加载日志。

v1.6 支持[串口素材上位机](docs/SERIAL_MEDIA_UPLOADER.md)：安装 `scripts/requirements.txt` 中的依赖后，双击 `SDMediaUploader.cmd`。公开仓库只提供已清理的源码，不附带预编译固件、EXE 或个人部署文件；上位机可按文档从源码构建。

`sd_card_files/`、原始素材、源字体和 `.tools/` 被 Git 忽略，首次克隆不会带上这些文件，上述 Release 附件也不包含它们。完整界面所需的字体、图标和媒体必须另行准备，具体步骤见[素材准备](docs/GETTING_STARTED.md#素材准备)。

## SD 卡与素材格式

```text
SD 卡根目录/
├─ fonts/
│  ├─ chinese_16.bin
│  ├─ chinese_20.bin
│  └─ chinese_32.bin
├─ ime/
│  └─ pinyin_full.txt
├─ icons/
│  └─ music.bin、settings.bin、weather.bin 等应用图标
├─ wallpapers/
│  ├─ wallpaper1.bin
│  ├─ wallpaper2.bin
│  └─ wallpaper3.bin
├─ imges/
│  └─ 照片名.bin
├─ videos/
│  ├─ 视频名.rvf
│  └─ 视频名.wav
└─ music/
   ├─ alarm.wav
   ├─ 歌曲名.wav
   └─ 歌曲名.lrc
```

照片目录的拼写为 **`imges`**，与当前代码一致。复制时保持路径，不要在 SD 根目录再套一层 `sd_card_files`。

| 素材 | 设备读取的格式 | 准备方式 |
| --- | --- | --- |
| 字体 | LVGL v8 BIN，16 / 20 / 32 px | 使用配套字库；更新脚本还需要源 TTF、Node.js 和原有 BIN |
| 应用图标 | 64×64 LVGL TRUE_COLOR_ALPHA BIN | `scripts/convert_app_icons.py` |
| 壁纸 | 480×480 LVGL BIN | 图片转换；选择 `wallpaper1.bin` 至 `wallpaper3.bin` |
| 照片 | 480×450 LVGL BIN | 图片转换，保留比例并补边 |
| 音乐、闹铃 | 44100 Hz、双声道、PCM16 WAV，标准 44 字节文件头 | MP3 先转换，再复制到 `/music` |
| 歌词 | UTF-8 LRC，与歌曲主文件名相同 | 原样上传，不转换；如 `song.wav` 配 `song.lrc` |
| 视频 | RVF1，RGB565 小端；推荐 320×240 | MP4 用 `scripts/convert_video.py` 转换 |
| 视频音频 | 同名 44100 Hz、双声道 PCM16 WAV | 转换视频时提取；无音轨视频只生成 RVF |

先安装资源脚本依赖：

```powershell
py -3 -m pip install -r scripts/requirements.txt
py -3 scripts/generate_pinyin_dict.py
py -3 scripts/convert_video.py "D:\素材\demo.mp4" --width 320 --height 240 --fps 20
```

视频输出为 `sd_card_files/videos/demo.rvf`，源文件有音轨时同时生成 `demo.wav`。转换脚本默认帧率为 10 FPS，上面的命令显式设为 20 FPS；上位机默认使用 20 FPS。壁纸、照片、音乐和字体的详细转换示例见[部署指南](docs/GETTING_STARTED.md#素材准备)。

## 工程结构

| 路径 | 用途 |
| --- | --- |
| [`platformio.ini`](platformio.ini) | 板卡、Flash/PSRAM、库依赖、编译宏和源码过滤 |
| [`src/main.cpp`](src/main.cpp) | 启动顺序、RGB 双帧缓冲、LVGL 刷新、触摸、MPU6050 与 FPS 统计 |
| [`src/SDcar.cpp`](src/SDcar.cpp) | SD_MMC、LVGL 文件系统、字体、壁纸、照片、视频、音乐和拼音资源 |
| [`src/Sound.cpp`](src/Sound.cpp) | 音乐、闹铃、音量、I²S 输出以及 AI 语音与音乐切换 |
| [`src/Web_ai.cpp`](src/Web_ai.cpp) | AI HTTP/WebSocket、回复文本和 TTS WAV 处理 |
| [`src/setwifi.cpp`](src/setwifi.cpp) | WiFi、NTP、天气和 NVS 缓存 |
| [`src/video_draw.cpp`](src/video_draw.cpp) | 保留抗锯齿的视频缩放与绘制优化 |
| [`src/video_audio.inc`](src/video_audio.inc) / [`src/wav_pcm.c`](src/wav_pcm.c) | 视频音频输出、WAV 校验和音画同步 |
| [`src/SerialUpload.cpp`](src/SerialUpload.cpp) / [`src/serial_upload_protocol.c`](src/serial_upload_protocol.c) | ESP32 串口上传接收、分块校验和文件替换 |
| [`tools/sd_uploader/`](tools/sd_uploader/) | Windows 串口素材上位机 |
| [`server/`](server/) | AI 后台、网页、TTS 脚本、依赖锁文件及部署配置模板 |
| [`src/ui/custom/`](src/ui/custom/) | 页面、应用、游戏和业务回调 |
| [`src/ui/generated/`](src/ui/generated/) | GUI Guider 生成的基础界面与内置资源 |
| [`include/`](include/) | LVGL 配置、触摸配置及本地依赖库 |
| [`scripts/`](scripts/) / [`font_assets/`](font_assets/) | 素材转换脚本与字体字符来源 |

## 版本记录

| 版本 | 说明 |
| --- | --- |
| v1.6 | 软复位、屏幕重启、渐变主题、中文名称、视频音频修复和小尺寸温度图标；本仓库只保留这一份脱敏源码快照。 |

默认分支为 `main`，克隆命令指定 `v1.6` 标签以获取本次脱敏源码快照。其他历史版本仍保存在私有仓库中。

### 开发进度

v1.6 已合并设置中的设备软复位与屏幕恢复、开机连接已保存 WiFi、淡草绿渐变与蓝天白云天气页面、20px 中文名称显示，以及视频 WAV 直接读取和 AI 音频输出交接修复。实机验证屏幕重启可恢复显示，视频音频持续播放正常。发布验证与适用范围见 [v1.6 发布说明](docs/RELEASE_V1.6.md)。

串口素材上传、视频配套音频和文字浏览器的使用方式分别见对应文档；这些文档只描述通用接口和本地操作步骤，不包含个人设备日志、服务器地址或部署凭据。

## 常见问题

**界面很流畅，FPS 为什么与视频帧率不同？**

屏幕 FPS 按 LCD VSYNC 次数计算。当前 10 MHz PCLK 和面板时序对应约 35 Hz；视频实际呈现帧率单独记录在串口诊断中，实测稳定约 10 FPS。把源视频转换为 20 FPS，不代表设备一定每秒呈现 20 个新视频帧。

**为什么烧录成功，却没有壁纸、中文字或媒体？**

固件不会自动写入 SD 卡。确认资源已经生成，目录位于 SD 根目录，串口中有挂载和资源加载日志。字体缺失时会回退，显示内容可能不完整。

**PNG、JPG、MP3、MP4 能直接放入 SD 卡吗？**

当前固件的这些播放路径读取转换后的 BIN、PCM WAV 和 RVF。请先转换；LRC 使用同名 UTF-8 文件，直接原样上传即可。

**中文双引号、冒号等符号不正常怎么办？**

确认已部署更新后的 `/fonts/chinese_20.bin`，并使用包含该修复的 v1.3 或更新固件。字库和固件中的大字库配置需要配套，单独重新烧录不会替换 SD 上的旧字库。

**视频没有声音怎么办？**

确认运行 v1.4 或更新固件，且 `/videos/视频名.rvf` 配有同名 `.wav`，音轨为 44100 Hz、双声道 PCM16。缺失或格式无效时会回退到无声视频，请检查串口日志。稳定 v1.3 标签只播放无声 RVF。

**首次编译出现 I²S 或 LEDC 接口报错怎么办？**

优先核对 Arduino-ESP32 版本是否为 3.x，以及平台来源是否匹配[编译环境说明](docs/GETTING_STARTED.md#编译环境)。


## Public source and local configuration

This repository is a sanitized public source snapshot. It contains no WiFi password, weather key, AI key, private server address, server environment file, device log, or preconfigured firmware binary. Copy `src/private_config.h.example` to the ignored `src/private_config.h` and fill local values before enabling online AI or weather. Copy `server/.env.example` to a local `.env`; never commit either file.

The public source does not include a precompiled firmware release. Build locally after supplying the configuration required by your deployment.


# 编译部署与素材转换指南

本指南对应 `main` 的 **Breeze OS 1.28.0 原生 ESP-IDF 工程**，验证环境为 ESP-IDF 5.5.6。
当前公开仓库保存最新1.28.0原生IDF源码快照，并提供当前通用固件包；配置自己的API见[API教程](API_CONFIGURATION.md)，操作见[图解使用](USER_GUIDE.md)。

源码包含固件、固定版本的LVGL/minimp3和工具源码；运行用SD字库、图标、壁纸、
媒体及打包EXE需要另外准备。当前AI直接访问云端API，设备热点可提供音乐、照片、
视频和OTA上传入口。当前不需要自建中转服务器。

游戏和背单词两枚图标已内置固件；背单词需另行将 `words/cet4.bwv`、`fonts/cet4_20.bin` 放入 SD 卡根目录对应文件夹。词库文本源与转换脚本在仓库中，完整命令见[背单词部署](WORD_MATCH.md)。OTA 只替换应用分区，不会安装 SD 资源。

## 不编译，直接首次烧录

下载[当前通用固件包](../firmware/README.md)，按[首次烧录教程](FIRST_FLASH.md)把 Factory.bin 从0x0写入。仅适用于匹配的 N16R8 与项目屏幕/接线。不同商家屏幕的驱动和时序可能不同，请先看[硬件与购买参考](HARDWARE.md#屏幕购买参考与兼容性)。已有系统日常更新用OTA.bin，保留NVS；完整SD素材需另备。

## 硬件接线

当前板卡使用 ESP32-S3、16 MB Flash、8 MB OPI PSRAM、ST7701 480×480 RGB 屏、GT911 触摸、MPU6050、MAX98357A 功放和 SD 卡。

| 接口 | 信号 | GPIO |
| --- | --- | --- |
| LCD 控制 | DE / VSYNC / HSYNC / PCLK | 18 / 17 / 16 / 21 |
| LCD 红色 | R0 / R1 / R2 / R3 / R4 | 11 / 12 / 13 / 14 / 0 |
| LCD 绿色 | G0 / G1 / G2 / G3 / G4 / G5 | 8 / 20 / 3 / 46 / 9 / 10 |
| LCD 蓝色 | B0 / B1 / B2 / B3 / B4 | 4 / 5 / 6 / 7 / 15 |
| LCD 初始化 SPI | CS / SCK / MOSI | 39 / 48 / 47 |
| LCD 背光 | PWM | 38 |
| GT911 与 MPU6050 | SDA / SCL | 19 / 45 |
| GT911 | I²C 地址 | `0x5D` |
| SD_MMC 1-bit | CLK / CMD / D0 | 48 / 47 / 41 |
| SD 控制 | DAT2 控制线，初始化拉高 | 42 |
| MAX98357A | BCLK / LRCLK / DOUT | 1 / 2 / 40 |

引脚和屏幕时序在 [`src/board_pins.h`](../src/board_pins.h)，GT911驱动在 [`src/board_touch.c`](../src/board_touch.c)，SD 与音频配置分别在 [`src/SDcar.cpp`](../src/SDcar.cpp)、[`src/Sound.cpp`](../src/Sound.cpp)。共用 GPIO48/47 的 LCD 初始化 SPI 与 SD 按启动顺序切换。

Flash 使用 **DIO、80 MHz**；PSRAM 使用 OPI。当前板卡曾在 QIO Flash 配置下无法正常启动，移植时应先保持 [`sdkconfig.defaults`](../sdkconfig.defaults) 中的有效配置。1.22.0 起 RGB PCLK 为 14 MHz，按时序计算约 49.3 Hz；双帧缓冲放在 PSRAM，DMA bounce buffer 为 20 行，传输超时恢复使用原 10 MHz。

## 编译环境

当前工程使用 ESP-IDF 5.5.x（本机 5.5.6）、工具链配套 Python 3.11、LVGL 8.3.1 和 `espressif/esp_websocket_client` 1.6.1。固件不使用 Arduino 组件；LVGL 源码位于 `components/lvgl/`，其他依赖由 `main/idf_component.yml` 和 `dependencies.lock` 固定。

### 首次安装

按[乐鑫 Windows 安装指南](https://docs.espressif.com/projects/esp-idf/en/release-v5.5/esp32s3/get-started/windows-setup.html)安装 ESP-IDF 5.5 与 ESP32-S3 工具链，打开对应 ESP-IDF 终端，再进入本次迁移后的工程根目录。公开仓库只保留当前IDF源码快照。

若要在普通 PowerShell 使用本机现有安装，可执行以下示例；其他电脑需要替换为实际安装路径：

```powershell
$env:IDF_TOOLS_PATH = 'C:\Espressif'
$env:IDF_PYTHON_ENV_PATH = 'C:\Espressif\python_env\idf5.5_py3.11_env'
$env:PATH = "$env:IDF_PYTHON_ENV_PATH\Scripts;$env:PATH"
. 'C:\Espressif\frameworks\esp-idf-v5.5.6\export.ps1'
idf.py --version
```

首次配置需要网络下载 WebSocket 组件，后续可使用本地依赖缓存。素材转换脚本使用系统 Python，可另外安装 `scripts/requirements.txt`，不需要更改 IDF 虚拟环境。

### 获取、编译与首次烧录

```powershell
git clone https://github.com/banli12345/ESP32SmartVoiceBox-public.git
cd ESP32SmartVoiceBox-public
idf.py set-target esp32s3
idf.py build
# 新板或从Arduino首次迁移时烧录完整配套镜像；COM3替换为实际串口
idf.py -p COM3 flash
idf.py -p COM3 monitor
```

公开快照更替后请重新克隆，先在仓库外备份自己的配置，再恢复本机配置并构建。
保留本地改动时先提交或另行备份，避免与远端更新冲突。`set-target` 用于首次配置；
不必每次增量编译都重新执行。串口监视器退出组合键为 `Ctrl+]`，烧录或串口素材
上传前关闭监视器。日志为115200波特率，素材上传握手后自动切换到921600。

产物为 `build/esp32_smart_voice_box.bin` 应用镜像、`build/bootloader/bootloader.bin`
引导程序和 `build/partition_table/partition-table.bin` 分区表。首次烧录使用
`idf.py flash` 及其 `build/flasher_args.json` 中的完整镜像组合；不需要例行擦除NVS。
完整烧录还会写初始OTA选择数据，**已使用IDF双分区的设备日常更新优先采用下面的OTA**，
不要把完整烧录当作只更新当前应用槽，也不要固定假定当前运行app0或app1。

### 日常OTA更新

1. 连接设备热点，浏览器访问热点设置显示的网址（通常 `http://172.31.42.1/`，以实际地址为准）。
2. 在“固件与系统更新”上传本次构建的 `build/esp32_smart_voice_box.bin`。只上传应用镜像，
   不上传bootloader、分区表或整片Flash合并文件。
3. 上传完成后，在设备选择“设置 → 系统更新 → 检查更新 → 安装并重启”。
4. 保持供电并保留SD卡。固件写入未运行的OTA分区，通过校验后才切换启动。
   新系统的界面与音频任务正常运行约20秒后确认有效，确认前启动失败可由引导程序回滚。

当前“检查更新”检查SD卡上已上传的候选文件；没有独立互联网固件服务器和自动在线拉取。
详情见[网页上传与OTA](WEB_UPLOAD_OTA.md)。

| 分区 | 偏移 | 大小 |
| --- | --- | --- |
| NVS | `0x9000` | `0x5000` |
| OTA选择数据 | `0xe000` | `0x2000` |
| app0 / ota_0 | `0x10000` | `0x400000`（4 MiB） |
| app1 / ota_1 | `0x410000` | `0x400000`（4 MiB） |
| SPIFFS保留区 | `0x810000` | `0x7F0000` |

16 MiB Flash末尾为 `0x1000000`，没有factory分区。旧SPIFFS地址曾为 `0x410000`，
首次从旧分区布局迁移时如有SPIFFS数据需另行迁移；本工程运行素材主要位于SD卡。
WiFi、天气缓存、音量、壁纸等继续使用NVS。VS Code使用Espressif IDF扩展，
工作区没有绑定开发者电脑路径。

### 网络、天气和AI配置

1. 在设备“设置 → WiFi”连接 2.4 GHz 网络并保存。默认通用构建无预置个人 WiFi；NVS 中已有网络继续使用。
2. 开启设备热点，手机连接后打开设置显示的网址，在网页底部进入“服务配置”（默认 `http://172.31.42.1/settings`）。
3. AI 卡填自己的 HTTPS 基础地址、Key、模型；天气卡填兼容心知协议的实况、预报、生活指数端点与 Key。分别保存两张卡，最后点击“重启并生效”。
4. 已有 Key 不回显，留空保留保存值。配置在 NVS 中，重启/日常 OTA 保留，修改不重新编译；SD `/config/ai.json`、`/config/weather.json` 导入也兼容。完整格式与限制见[API 配置](API_CONFIGURATION.md)。

根 CMake 的 `BREEZE_GENERIC_BUILD` 默认 ON，忽略本地私有头的 WiFi 默认值。AI 和天气 Key 均由运行时提供。普通问答直接通过 HTTPS 请求 DeepSeek；语音由云端接口生成、ESP32 完整缓存后经 I2S 播放，不在芯片安装大模型。在线音乐指令由本地解析；不需要旧的自建中转服务器。网页服务配置不要求 SD，但语音、字体、音乐和媒体仍需要 SD。

联网天气、生活指数、云端问答和在线音乐需要互联网；农历、常见节日、SD媒体与游戏
可离线运行。热点是AP+STA/NAPT共享上游WiFi；BLE配对不提供A2DP手机音箱功能。

## 素材准备

`sd_card_files/` 是本机生成资源目录，被 `.gitignore` 排除；源码克隆后需要自行创建和生成。源图片、视频、音乐、`simhei.ttf` 和 `.tools/` 也不随仓库提供。

所有命令从项目根目录执行。下面用 `D:\素材\` 作输入路径示例，请替换为自己的文件路径。输出只需复制相应内部目录到 SD 根目录，目录树见[项目首页](../README.md#sd-卡与素材格式)。

### 壁纸和照片

转换后的图片是 LVGL v8 TRUE_COLOR_ALPHA BIN：4 字节头，每个像素包含 RGB565 小端颜色和一个 alpha 字节。它不是普通 BMP，也不是只含 RGB565 像素的裸 BIN。

单张壁纸可利用现有图片转换函数生成，中心裁切至 480×480：

```powershell
@'
from pathlib import Path
from PIL import Image, ImageOps
from scripts.convert_app_icons import encode_lvgl_bin

target = Path("sd_card_files/wallpapers/wallpaper1.bin")
target.parent.mkdir(parents=True, exist_ok=True)
with Image.open(r"D:\素材\wallpaper.png") as source:
    image = ImageOps.exif_transpose(source)
    encode_lvgl_bin(image, target, 480, 480)
print(target)
'@ | py -3 -
```

照片保留比例，补边至 480×450，输出目录必须为 `imges`：

```powershell
@'
from pathlib import Path
from PIL import Image, ImageOps
from scripts.convert_app_icons import encode_lvgl_bin

target = Path("sd_card_files/imges/photo1.bin")
target.parent.mkdir(parents=True, exist_ok=True)
with Image.open(r"D:\素材\photo.jpg") as source:
    image = ImageOps.exif_transpose(source)
    encode_lvgl_bin(image, target, 480, 450, preserve_aspect=True)
print(target)
'@ | py -3 -
```

### 应用图标

单独转换一个应用图标：

```powershell
py -3 scripts/convert_app_icons.py --icon "D:\素材\music.png" --name music --size 64
```

输出 `sd_card_files/icons/music.bin`。其他图标使用代码约定的名称，例如 `settings`、`weather`、`clock`、`photos`、`browser`、`video`、`calendar`。

脚本的批量模式使用 `--source-dir`，输入文件名固定在脚本的 `ICONS`、`WALLPAPERS`、`PHOTO_IMAGES` 表中。使用任意文件名的素材时，请先用上面的单文件示例，或调整批量配置。

### 音乐与闹铃

SD 音乐路径直接读取 WAV，输出必须为 **44100 Hz、双声道、16-bit 小端 PCM，标准 44 字节 WAV 头**。普通 FFmpeg WAV 可能带额外元数据块；下面先解码成 PCM，再用 Python `wave` 写标准头，适配当前读取方式：

```powershell
@'
from pathlib import Path
import subprocess
import wave
import imageio_ffmpeg

source = Path(r"D:\素材\song.mp3")
target = Path("sd_card_files/music/song.wav")
target.parent.mkdir(parents=True, exist_ok=True)
pcm = subprocess.run([
    imageio_ffmpeg.get_ffmpeg_exe(), "-hide_banner", "-loglevel", "error",
    "-i", str(source), "-vn", "-ac", "2", "-ar", "44100",
    "-c:a", "pcm_s16le", "-f", "s16le", "pipe:1",
], stdout=subprocess.PIPE, check=True).stdout
with wave.open(str(target), "wb") as output:
    output.setnchannels(2)
    output.setsampwidth(2)
    output.setframerate(44100)
    output.writeframes(pcm)
print(target)
'@ | py -3 -
```

配套歌词保存为 UTF-8 `sd_card_files/music/song.lrc`，使用相同主文件名。LRC 可直接通过上位机原样上传，无需转换；同名 MP3 配套歌词也不会被转码或改写。闹铃文件为 `sd_card_files/music/alarm.wav`，采用相同音频格式。

调整歌词时间的示例：

```powershell
py -3 scripts/shift_lrc.py "sd_card_files/music/song.lrc" 800
```

该脚本会原地调整歌词，请先备份；上述正数从歌词时间戳减去 800 ms，让歌词提前出现。负数则让歌词延后。

### 视频

```powershell
py -3 scripts/convert_video.py "D:\素材\demo.mp4" --width 320 --height 240 --fps 20
```

输出 `sd_card_files/videos/demo.rvf`，格式为 32 字节 RVF1 头加 RGB565 小端原始帧。默认保留输入比例并补黑边。设备显示时使用现有抗锯齿缩放。源视频有音轨时同时生成 `demo.wav`，固定 44100 Hz、双声道、PCM16，并对齐 RVF 时长。两份文件应一起放入 SD 卡 `/videos`。

转换脚本默认 10 FPS，上位机默认 20 FPS；`--fps 20` 指定 20 FPS 源帧序列，实际播放性能取决于 SD 读取和绘制。320×240、20 FPS 的像素数据约 3.07 MB/s，1 分钟约 184 MB，准备 SD 空间和复制时间时需考虑文件大小。

v1.4 支持只转换无声画面，或为现有 RVF 补同名音轨：

```powershell
py -3 scripts/convert_video.py "D:\素材\demo.mp4" --width 320 --height 240 --fps 20 --no-audio
py -3 scripts/convert_video.py "D:\素材\demo.mp4" --audio-only --output sd_card_files/videos/demo.rvf
```

`--no-audio` 生成无声 RVF，并移除输出目录中旧的同名 WAV；`--audio-only` 校验已有 RVF 后生成音轨，保留原画面文件。无音轨的源视频仅生成 RVF。播放时音频进度决定视频时间，过期画面会跳过以保持同步；音轨缺失或无效时回退到无声播放。稳定 v1.3 标签不包含这些视频音频功能。

### 拼音词典

```powershell
py -3 scripts/generate_pinyin_dict.py
```

输出 `sd_card_files/ime/pinyin_full.txt`。设备启动时加载词典，缺失时使用小型备用词典。脚本将常用简体字排在候选前面，并补充 GB2312 汉字与单字母候选。

### 字体

字体更新脚本需要额外准备：

1. 含有所需字形的源 TTF，默认根目录 `simhei.ttf`。
2. 已有配套的 `sd_card_files/fonts/chinese_20.bin` 或 `chinese_32.bin`；这些更新脚本会读取并保留原字库字符。
3. `.tools/node-v24.21.0-win-x64/node.exe` 及其 `node_modules/npm/bin/npx-cli.js`，由脚本调用 `lv_font_conv`。当前路径固定在脚本中，使用其他 Node 安装位置时需要调整对应常量。

以上文件均未随 Git 发布，安装 Python 依赖不会补齐它们。16px 字库需另行准备与固件匹配的 LVGL v8 BIN。

1.12.0 起，已有的 16px / 32px BIN 也可以用 `--size` 与 `--keep-subset`
保留原字符集补字，并通过 `scripts/upload_fonts.py` 更新到设备。
完整命令见[字库与切换说明](FONTS_AND_MOTION.md)。

准备好原字库和工具后，可以把需要补入的字符保存为 UTF-8 `D:\素材\extra_symbols.txt`，例如 `“”：；？！（）｛｝`，再运行：

```powershell
py -3 scripts/update_chinese20_font.py --text-file "D:\素材\extra_symbols.txt"
py -3 scripts/update_chinese32_city_font.py
```

更新结果分别为 `/fonts/chinese_20.bin`、`/fonts/chinese_32.bin`。中文大字库需匹配固件 `LV_FONT_FMT_TXT_LARGE` 配置；该修复自 v1.3 起包含在固件中，v1.4 同样包含。替换前备份旧文件，替换后重启确认加载和字形显示。

## SD 卡部署与首次验证

1. 准备 FAT32 SD 卡，将 `fonts/`、`ime/`、`icons/`、`wallpapers/`、`imges/`、`videos/`、`music/` 按实际资源复制到根目录。
2. 插入设备，烧录固件或重启，并打开 115200 波特率串口日志。
3. 检查 `SD_MMC` 挂载结果、字体与词典加载情况；失败时先核对接线、供电和资源路径。
4. 检查主界面与壁纸、中文输入候选、音乐及歌词、照片与视频。音频问题同时核对 PCM 格式和 I²S 接线。
5. 更换资源后重启，重新加载启动缓存及媒体列表。

屏幕上的 FPS 统计 LCD VSYNC 刷新率，以设备当前读数为准；14 MHz PCLK 下的约49.3 Hz是时序理论值，不能直接当作界面或视频帧率。串口中的视频呈现统计用于观察视频实际更新速度。两项统计含义不同，不能用屏幕 FPS 判断 RVF 是否达到转换时设定的帧率。

2026-10-08 已完成实机 WAV 串口上传、SD 读回校验和视频音频播放检查。其余素材类型、异常恢复及音频抢占仍需补充验证。状态见[验证范围](WHATS_NEW.md)和[设备验证与已知限制](SERIAL_MEDIA_UPLOADER.md#设备验证与已知限制)。首次使用串口上位机时，先烧录当前含上传协议的IDF固件。

当前城市字库、中文键盘、40枚天气图标、显式音乐下载、相册内存回退和双轴唤醒见[本版说明](RELEASE_V1.28.0.md)与[字库部署](FONTS_AND_MOTION.md)。

# 编译部署与素材转换指南

本指南对应 **v1.6**（2026-10-09 发布），包含设备与屏幕重启、渐变主题、中文名称显示、视频音轨修复和小尺寸温度图标，继承设备信息、HTTP/HTTPS 文字浏览器及串口素材上传。首次使用需烧录 v1.4 或更新固件；上位机的使用、打包和检查步骤见[串口素材上位机文档](SERIAL_MEDIA_UPLOADER.md)。旧版本标签保持原位置，稳定 v1.3 保留原行为。Git 源码不含 EXE 和运行素材；[公开源码](https://github.com/banli12345/ESP32SmartVoiceBox-public) 提供新固件包，上位机未修改，复用 [公开源码](https://github.com/banli12345/ESP32SmartVoiceBox-public) 的 `SDMediaUploader-v1.4-windows.zip`。发布说明见 [RELEASE_V1.6.md](RELEASE_V1.6.md)，浏览器用法见[文字浏览器](text-browser.md)。

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

显示配置在 [`src/main.cpp`](../src/main.cpp)，触摸配置在 [`include/touch.h`](../include/touch.h)，SD 与音频配置分别在 [`src/SDcar.cpp`](../src/SDcar.cpp)、[`src/Sound.cpp`](../src/Sound.cpp)。共用 GPIO48/47 的 LCD 初始化 SPI 与 SD 按启动顺序切换。

Flash 使用 **DIO、80 MHz**；PSRAM 使用 OPI。当前板卡曾在 QIO Flash 配置下无法正常启动，移植时应先保持 [`platformio.ini`](../platformio.ini) 中的有效配置。RGB PCLK 为 10 MHz，双帧缓冲放在 PSRAM，DMA bounce buffer 为 20 行。

## 编译环境

本机已验证的环境如下；这些版本用于复现当前工程，库和平台升级后应重新检查编译与设备行为。

| 项目 | 已验证版本 |
| --- | --- |
| PlatformIO 平台 | pioarduino `55.03.312`（包管理器显示为 `55.3.312`） |
| Arduino-ESP32 | `3.3.12` |
| ESP-IDF | `5.5.5` |
| LVGL | `8.3.1` |
| Python | 本机使用 `3.13`；脚本使用 Python 3.9+ 语法，具体依赖兼容性以安装结果为准 |

Arduino-ESP32 与 ESP-IDF 的版本对应关系可参考 [pioarduino 55.03.312 发布页](https://github.com/pioarduino/platform-espressif32/releases/tag/55.03.312)。

### 首次安装

```powershell
git clone --branch v1.6 https://github.com/banli12345/ESP32SmartVoiceBox-public.git
cd ESP32SmartVoiceBox
py -3 -m pip install platformio
py -3 -m pip install -r scripts/requirements.txt
```

仓库目前将平台写为 `platform = espressif32`，没有固定 pioarduino 下载地址。新电脑为了使用匹配的 Arduino 3.x 平台，在 `platformio.ini` 的 `[env:esp32-s3-devkitc-1]` 下，将该行改为：

```ini
platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.312/platform-espressif32.zip
```

随后执行编译。现有本机环境已经安装对应平台，可继续使用原配置。仓库中已有本地依赖位于 `include/`，其他依赖由 PlatformIO 按 `lib_deps` 获取。首次编译需要网络。

### 编译、烧录与日志

```powershell
py -3 -m platformio run
py -3 -m platformio device list
py -3 -m platformio run --target upload --upload-port COM3
py -3 -m platformio device monitor --port COM3 --baud 115200
```

`COM3` 为示例，使用设备列表里的实际 USB 串口。烧录前关闭串口监视器和其他占用该端口的程序。若电脑没有检测到设备，请先检查 USB 连接和串口驱动。

在已安装 VS Code PlatformIO 的 Windows 电脑上，也可以直接调用其虚拟环境：

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run
```

常规上传交由 PlatformIO 处理 bootloader、分区表和应用的偏移。应用镜像在 `.pio/build/esp32-s3-devkitc-1/firmware.bin`；部分平台还生成 `firmware.factory.bin` 合并镜像，以实际输出为准。仓库中的 `partitions.csv` 尚未在 `platformio.ini` 中通过 `board_build.partitions` 指定，不能据此认定正在使用该分区布局。

### 网络配置

- 在设备设置页面配置 WiFi；配置保存在 NVS。
- 天气密钥配置在本地忽略文件 `src/private_config.h`，请先复制 `src/private_config.h.example` 并填入自己的有效配置；不要直接修改或提交源文件中的密钥。
- AI HTTP 地址和 WebSocket 主机在 `src/Web_ai.cpp`。配套后端已备份到 [`server/`](../server/)，依赖、环境变量、PM2 与 systemd 部署说明见[服务器文档](../server/README.md)。
- 普通问答使用 `POST /api/chat`，请求包含 `message`；回复读取 `text`、可选 `audio_url`。WebSocket 路径为 `/esp32`，推送类型为 `ai_reply`。
- AI TTS 音频要求 16000 Hz、单声道、PCM16 WAV。它与 SD 音乐的 44100 Hz、双声道要求不同。

完整请求及返回格式见 [HTML 协议说明](PROJECT_GUIDE.html#ai)。服务器的 `PUBLIC_BASE_URL` 需要让设备能够访问生成的 WAV，HTTP 与 WebSocket 地址也应指向同一后台。断网时设备可继续使用本地媒体、游戏和已经保存的时间、天气数据，新的在线问答与天气获取需要恢复网络。

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

屏幕上的 FPS 是 LCD VSYNC 刷新率，当前约 35 Hz。串口中的视频呈现统计用于观察视频实际更新速度。两项统计含义不同，不能用屏幕 FPS 判断 RVF 是否达到转换时设定的帧率。

2026-10-08 已完成实机 WAV 串口上传、SD 读回校验和视频音频播放检查。其余素材类型、异常恢复及音频抢占仍需补充验证。状态见[开发进度](../README.md#开发进度)和[设备验证与已知限制](SERIAL_MEDIA_UPLOADER.md#设备验证与已知限制)。首次使用串口上位机时，必须先烧录带接收协议的 v1.4 或更新固件，旧 v1.3 不能接收上传。


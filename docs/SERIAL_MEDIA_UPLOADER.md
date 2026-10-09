# Windows 串口素材上位机（兼容 v1.4／v1.5／v1.6）

当前固件版本为 v1.6（2026-10-09 发布），串口上传协议与 v1.4 保持兼容。WAV 实机上传和 SD 读回校验记录来自 v1.4，v1.6 新增视频音轨读取与音频输出交接修复，文末列出尚待补充的验证项目。Git 源码不包含打包后的 EXE，可按下文重建；运行素材也不在 Git 中。[公开源码](https://github.com/banli12345/ESP32SmartVoiceBox-public) 公开仓库不附带预编译固件；上位机没有修改，仍从 [公开源码](https://github.com/banli12345/ESP32SmartVoiceBox-public) 获取 `SDMediaUploader-v1.4-windows.zip`。发布说明见 [RELEASE_V1.6.md](RELEASE_V1.6.md)。所有命令从项目根目录运行。

Windows 串口素材上位机：双击项目根目录的 `SDMediaUploader.cmd`，或解压 Release 附件后运行 `SDMediaUploader/SDMediaUploader.exe`，也可使用本地打包的 `dist/SDMediaUploader/SDMediaUploader.exe`。选择素材类别、添加原始文件、选择 USB 串口，点击“转换并上传”。首次使用须先烧录 v1.4 或更新固件；v1.3 及更早固件没有串口接收协议。设备会重启进入上传模式，整批资源校验并提交后再次重启加载新资源。串口监视器需要关闭。

| 类别 | 原始文件 | 自动转换 | SD 卡路径 |
| --- | --- | --- | --- |
| 壁纸 | PNG / JPG | 480×480，中心裁切，LVGL BIN | `/wallpapers/wallpaper1.bin` 至 `wallpaper3.bin`，选择位置 |
| 音乐 | MP3 | 44100 Hz、双声道、PCM16、44 字节头 WAV | `/music/原文件名.wav` |
| 歌词 | LRC | 原文件直接上传，不转码、不修改时间戳或换行 | `/music/原文件名.lrc`，可单独上传或自动配对同名 MP3 |
| 照片 | PNG / JPG | 480×450，保留比例并补黑边，LVGL BIN | `/imges/原文件名.bin` |
| 视频 | MP4 | 320×240，默认 20 FPS，同时生成配套 PCM WAV | `/videos/原文件名.rvf` 和同名 `.wav` |

无音轨 MP4 仅生成 RVF；替换这类视频时会移除 SD 上旧的同名音轨。视频缩放的现有抗锯齿继续保留。一次最多 32 个输出文件，支持中文名称，SD 完整路径最多 63 个 UTF-8 字节。“仅转换并导出”可在未连接设备时生成完整素材目录。上位机使用 115200 握手，切换到 921600 上传；大视频需要等待串口传输完成。

歌词无需转换。在“音乐和歌词”中添加 LRC 即可；队列只有 LRC 时按钮显示“直接上传 LRC”。MP3 旁边的同名 LRC 也按原样一起上传。设备当前读取 UTF-8 歌词，请使用原本就是 UTF-8 的 LRC；上位机保留所选文件的原始字节。

传输采用 16 KiB 分块、CRC32、应答重试和 SD 读回校验。资源先写临时文件，整批校验成功后才替换正式文件；提交前取消会保留原素材。提交阶段会禁用取消。提交日志和备份用于重启时恢复未完成的替换，恢复失败会保留备份并提示错误。

源码运行与 Windows 打包：

```powershell
py -3 -m pip install -r scripts/requirements.txt
py -3 -m tools.sd_uploader.gui
# 打包需要额外安装 PyInstaller，生成目录包含 Python 和 FFmpeg
py -3 -m pip install pyinstaller
py -3 scripts/build_sd_uploader.py
```

分发时复制整个 `dist/SDMediaUploader/` 目录，保留旁边的 `_internal`。Python 回归检查：`py -3 -m unittest discover -s tests -p "test_*.py"`。纯 C 协议检查可编译 `tests/serial_upload_protocol_test.c` 和 `src/serial_upload_protocol.c`（头文件目录 `src`，源码使用 UTF-8）。

接收端的文件替换与故障恢复检查会直接编译实际 `SerialUpload.cpp`，UART 和 SD 使用 `tests/serial_fakes` 中的主机模拟。Windows 在 Visual Studio 开发者终端执行：

```bat
cl /nologo /std:c++20 /utf-8 /EHsc /D_CRT_SECURE_NO_WARNINGS /Itests\serial_fakes /Isrc tests\serial_upload_receiver_test.cpp src\serial_upload_protocol.c /Fe:%TEMP%\serial_receiver_test.exe
%TEMP%\serial_receiver_test.exe
```

这些主机检查不能代替设备上的真实串口、SD 写入和播放测试。

## 设备验证与已知限制

2026-10-08 在 COM3 连接的 ESP32-S3 上完成了以下检查：

- 上位机上传版固件烧录成功，SD_MMC 以 40 MHz、1-bit 挂载，根目录检查通过，壁纸缓存和音乐初始化正常。
- 使用上位机的实际 `SerialUploader` 传输实现，在 115200 波特率完成握手，进入 921600 波特率上传模式，将 5,847,704 字节的音轨写入 `/videos/ikun20.wav`；设备端 SD 读回 CRC32 校验通过，整批提交并重启成功。本次从连接到提交约 81 秒，包含握手、上传和读回校验。
- 设备识别 `/videos/ikun20.rvf`：320×240、20 FPS、663 帧；音轨为 44100 Hz、双声道 PCM16，共 1,461,915 个采样帧，两者时长均为 33.15 秒。
- 用户实机确认声音、音画同步、循环，以及退出视频后恢复音乐均正常；采样日志中的音频 `underrun` 为 0。

这次通过源码调用传输模块，未操作打包后的图形界面。LCD VSYNC 约 35 FPS，视频日志中稳定呈现约 10 FPS，尚未达到源文件的 20 FPS；现有抗锯齿保持不变。只补充了 `ikun20.wav`，`ikun.rvf` 仍因没有同名音轨而回退到无声播放。

用户仍观察到轻微整屏明暗闪动。v1.3 对照同样存在，尚无原因结论，不能据此确认是硬件故障。当前发布使用原来的屏幕与 Wi-Fi 配置，没有纳入此前未奏效的屏幕缓冲、重同步或 Wi-Fi 低功率实验修改；屏幕仍为 10 MHz PCLK、20 行 bounce 缓冲及原有背光配置。

以下实机验证尚未完成：

- 用图形上位机验证壁纸、照片、MP3/LRC、完整 RVF/WAV 配对上传及对应素材的加载。
- 在实机验证取消上传和异常中断时的原素材保留、提交日志恢复。
- 检查视频与闹铃、AI 音频之间的切换，以及切换后的输出恢复。

本次发布不移动 `v1.2`、`v1.3` 标签。已通过的 WAV 传输和播放验证不能代替上面的后续项目。


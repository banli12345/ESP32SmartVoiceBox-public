# Windows 串口素材上位机

适用于当前 Breeze OS 1.28.0 原生IDF固件。源码提供Python上位机与打包脚本，不提供旧Release、预编译EXE或固件。

双击根目录SDMediaUploader.cmd，或执行下方Python命令启动。选择类别、原始文件、USB串口后点击“转换并上传”。设备会重启进入上传模式，整批校验提交后再次重启加载资源；先关闭串口监视器。也可在未连接时仅转换导出。

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

## 验证范围

上传协议主机检查覆盖CRC、应答、文件替换与中断恢复；不代表当前图形上位机已完成所有硬件故障注入。当前用户已确认热点网页上传图片、带声音视频及音乐可用；串口传输速度与显示帧率取决于设备和素材，未在此版本重新测量。

[当前部署指南](GETTING_STARTED.md) · [网页上传与OTA](WEB_UPLOAD_OTA.md)

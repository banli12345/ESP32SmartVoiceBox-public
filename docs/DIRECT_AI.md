# ESP32-S3 直连 DeepSeek

当前原生 ESP-IDF 固件直接通过 HTTPS 请求 DeepSeek 云端模型，并调用同一服务
的语音接口。无需运行旧版 Node.js、edge-tts 或 FFmpeg 服务器。大模型在云端
推理，ESP32 负责联网、显示回答和 I2S 播音，必须连接可上网的 WiFi。

## 推荐：网页配置

开启设备热点，手机在原网页底部进入“服务配置”（`/settings`），填自己的 AI 基础地址、Key 与模型，保存后点“重启并生效”。已有 Key 不回显，留空保留；首次必须填写。无需重新编译。天气和生活指数在同页另一张卡独立保存，完整说明见[API 配置教程](API_CONFIGURATION.md)。

## 兼容：导入配置

先烧录包含 `AiConfig.cpp` 的新版固件。在电脑本地创建私有 JSON 文件，
不要提交真实Key，也不必把Key发到聊天里。先按[API配置教程](API_CONFIGURATION.md)
把占位符换成自己的凭据，再生成下述本机导入文件：

```json
{
  "version": 1,
  "api_base": "https://api.deepseek.com",
  "api_key": "填写自己的 DeepSeek API Key",
  "model": "deepseek-flash",
  "speak": true
}
```

关闭串口监视器，执行（端口按实际设备调整）：

```powershell
python scripts/provision_ai.py "C:\private\ai.json" --port COM79
```

此工具沿用串口素材上传协议，设备会重启。也可关机取出 SD 卡，把配置保存到
卡根目录的 `config/ai.json`。开机验证成功并写入 NVS 后，固件删除 SD 上的
导入文件；配置无效或 NVS 写入失败时保留原配置和导入文件。API Key 不编译
进固件，不在设备界面或日志中显示。NVS 当前没有加密，包含 NVS 的 Flash
备份也需按私有文件保存。更新配置可重复导入，`speak: false` 关闭自动播报。

## 使用与错误提示

在 AI 应用输入普通问题即可请求大模型，例如“用一句话介绍 ESP32-S3”。
有文字后自动生成语音，通过板载 I2S 功放播放；语音失败仍保留文字回答。
1.23.0 起，在“设置 → 语音设置”选择女声 stella 或男声 tide，再点击“保存音色”。
保存由后台服务写入 NVS，成功后下次语音请求生效，重启保留。正在播报的回答
继续使用请求开始时的音色；此设置与 API Key、模型和原有导入 JSON 分开保存，
不需要重新导入 API 配置。
1.24.0 起，“帮我设置明天8点的闹钟”等指令由设备本地执行，可离线返回文字；普通问答保留上一轮上下文，故事可输入“继续”。播报时可点击“停止播报”。具体规则见[时钟和 AI 提醒](WHATS_NEW.md)。
1.25.0 起，语音先完整缓存到 SD 卡，确认下载结束后关闭网络连接并从本地播放；缓存/播放期间可取消，结束或失败后自动删除临时文件，开机清理断电残留。没有可写 SD 卡时保留文字并提示错误。详见[完整语音缓存](WHATS_NEW.md)。
点歌、播放第几首、暂停和音量命令仍由设备直接处理，具体见
[在线音乐说明](ONLINE_MUSIC.md)。目前没有麦克风语音识别功能。

1.8.1 起，点击问题框后键盘固定在屏幕底部，中文候选栏在键盘上方，
问题框和“发送”按钮并排位于候选栏上方。点击键盘收起键或侧边返回可
收起键盘，保留未发送内容，并恢复回答区高度；再次侧边返回退出 AI 页面。
此布局仅用于 AI 助手，WiFi 和其他表单的键盘布局保持原有行为。

HTTP 401 表示 Key 无效，402 表示余额不足，429 表示限流，5xx 表示模型服务
异常。网络、证书、内存、格式和超时错误分别处理，不显示可能包含敏感信息
的原始错误响应。旧固件的 HTTP 500 来自中转服务，不能据此认定 DeepSeek
本身故障。

## 实现和验证

- `AiConfig.cpp`：一次性 SD/UART 导入，NVS 保存；不清空已有设备设置。
- `AiDirectClient.cpp`：原生 `esp_http_client`，IPv4、CA 证书校验、关闭自动
  重定向、24 KiB 响应上限；解析 `choices[0].message.content`，UTF-8 安全截断。
- `AiSpeech.cpp`：直连 `/v1/tools/tts`，24 kHz 单声道 PCM16；网络接收和音频
  消费分开，先写入完整 SD 临时文件，再通过有界 PSRAM 队列预取本地音频，
  复制为双声道交给现有音频仲裁，结束、取消或失败后删除临时文件；
  播放后恢复音乐。音色取自请求开始时的设置快照。
- `AiVoice.cpp` / `AiVoiceUI.c`：音色设置及后台保存，NVS 命名空间 ai_voice，
  不改变 ai_direct/config 的二进制布局。默认仍为 stella 女声。
- `Web_ai.cpp`：仅在 AI 后台任务执行网络请求；不启动旧服务器的广播 WebSocket。

协议/错误/短读/PCM/NVS 故障测试：在 VS C++ 开发者终端运行
`python tests/ai_direct_client_host_test.py`；音频切换测试运行
`python tests/audio_handoff_host_test.py`。主机测试不验证实物扬声器听感。
键盘布局测试运行 `python tests/ai_chat_ui_host_test.py`，使用真实 LVGL 和
SD 字体，覆盖输入、拼音选字、发送/确认键、收起/返回和重复打开。

2026-11-14 在 COM79 的 ESP32-S3 上完成实机检查：配置由 UART 传入后成功
从 NVS 加载，普通问答的文本和语音均返回 HTTP 200。开启 BLE、热点并播放
SD 音乐后再次问答成功，24 kHz 语音输出结束后音乐恢复；该次语音流为
234,240 字节，AIChatTask 最小剩余栈 7,424 字节。没有发现重启或堆损坏。
正式固件已移除自动问答测试代码，Key 不包含在固件 bin/elf 中。设备测试
确认网络、PCM 和 I2S 软件链路；用户已在正式固件上确认文字回答和扬声器
语音播报均正常。

接口参考：[DeepSeek 对话](https://api-docs.deepseek.com/api/create-chat-completion)、
[模型列表](https://api-docs.deepseek.com/api/list-models/)、
[语音合成](https://api-docs.deepseek.com/api/tts/)。

## 当前缓存均速

完整缓存、临时WiFi省电切换及屏上均速/耗时见[图解](USER_GUIDE.md#完整缓存与音色)和[当前实现](RELEASE_V1.28.0.md)。临时文件仍在SD，HTTP关闭并校验后才经I2S播放。接收优化不改变API Key配置或上游协议。缓存或播放期间可停止；设备不会把未完成音频当成完整缓存播放。

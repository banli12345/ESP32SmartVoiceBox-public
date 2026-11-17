# 设备文字浏览器 · 原生 IDF

浏览器地址栏默认填入本机热点网页地址 `http://172.31.42.1/`，点击“打开”再加载，也可以直接修改为其他网址。1.20.3 修复本机地址打开路径：本机 HTTP 根页面使用原有“随身音乐”LVGL 适配，直接连接工程中的搜歌、播放和 WAV/LRC 保存功能。该适配入口在热点和上游 WiFi 关闭时也能显示；在线搜歌仍需上游网络，播放/保存仍需 SD 卡。不会自动启用热点或发起网络请求。

当前不显示额外“本机音乐”快捷按钮；输入本机网址后打开音乐适配页。其他网址仅支持简易文字阅读和链接，不运行复杂脚本或抖音网页视频。

其他网址仍使用文字阅读，不执行任意网页 JavaScript、不显示网页视频。当前原生 IDF 以 `idf.py build` 构建，HTTP 与传输分块由 `esp_http_client` 处理，TLS 使用 SDK 外部内存配置，不依赖旧 PlatformIO 链接脚本。侧边返回先收起键盘，再返回历史网页，无历史时退出。

## 以下为历史 v1.5 记录

下面保留最初 Arduino / PlatformIO 浏览器的实现与测试记录，其中构建命令、传输读取器、链接适配和旧服务器网址不适用于当前 IDF 工程。历史发布范围见 [v1.5 发布说明](WHATS_NEW.md)。

## 使用

1. 在设置中连接 WiFi，返回应用页并打开“浏览器”。
2. 输入网址后点击“打开”，也可使用键盘的确认键。未写协议的网址默认使用 HTTPS；首次测试可使用 `https://example.com`。
3. 上下滑动阅读本页正文，底部“上一页／下一页”切换正文分页。
4. 点击“链接”显示可打开的链接，再点击链接进入下一网页。工具栏“后退”返回之前的网址，“刷新”重新读取当前网页。
5. “取消”停止当前请求；右上角返回应用页。退出时删除浏览器界面并释放页面缓存；若连接操作尚未返回，下载任务会先停止，再释放缓存。

## 当前范围

- 支持 UTF-8／ASCII 的 HTML、XHTML 和纯文字响应。提取标题、可见正文及最多 8 个链接。
- 网页按块读取；正文上限 12 KiB，单页最多 1,024 字节，切页保持完整 UTF-8 字符。超过正文／256 KiB 下载上限时显示已截断状态。
- 保留最近 5 个访问网址，退出即清空；网址最长 383 字节，超出限制会拒绝。
- 支持相对链接及最多 4 次 HTTP 跳转。支持 Content-Length、连接关闭结尾和 chunked 分块响应；HTTPS 使用 SDK 内置 CA 证书包验证服务器，不跳过证书验证。HTTPS 需要设备时间有效。
- 请求使用移动网页兼容的 User-Agent，以便网站返回适合读取的移动版 HTML。实测百度对旧标识仅返回 227 字节、没有正文的脚本跳转壳；移动标识可以收到带文字与链接的页面。仅提取服务器返回的 HTML，不执行脚本中的跳转，包括 HTTPS 到 HTTP 的脚本跳转。
- 支持IP地址和显式端口，例如使用自己的静态网页服务地址；仅显示静态文字与链接，不能执行依赖JavaScript的交互。
- 不运行 JavaScript、不还原 CSS 排版、不显示网页图片／视频；暂不支持登录表单、压缩正文及 GBK 等非 UTF-8 编码。依赖脚本的网页可能没有可读正文。
- 下载在独立任务中运行，LVGL 仅在界面线程更新。正文、解析状态和 512 字节传输缓冲约占 18 KiB PSRAM，界面状态从已有 LVGL 内存池分配；不改显示时序、背光、WiFi 配置或 SDK 内存配置。
- 正文使用 512 字节 PSRAM 缓冲流式读取，`browser_body.c` 解码 HTTP 传输分块，解码状态不足 200 字节、不分配堆内存。替代 `HTTPClient::writeToStream` 的 4 KiB 内部缓冲：设备实测旧读取器在较大百度响应上出现 `esp-aes: Failed to allocate memory`，随后 TLS 读取中断。分块大小行与尾部行限制 127 字节，尾部总长度限制 2 KiB；不完整的 Content-Length 或 chunked 响应会报告读取失败，正文／下载上限到达时保留已读取内容并标记截断。
- 浏览器使用预留的 10 KiB 内部 RAM 栈及静态任务控制块；首次访问创建任务，后续复用任务，空闲时阻塞等待通知。预留区避免 WiFi 连接后内部 RAM 碎片造成创建失败，也避免任务栈频繁访问 PSRAM；退出会释放页面缓存，固定任务预留区保留。
- 当前 SDK 的 mbedTLS 默认仅分配内部 RAM。`scripts/browser_tls_link.py` 包装 `esp_mbedtls_mem_calloc`：仅在浏览器任务中将 TLS 收发缓冲、证书与加密对象分配到 PSRAM，其他任务仍使用 SDK 原分配器。仅移动大缓冲仍会在 RSA 验证阶段耗尽内部 RAM，因此小对象也必须使用 PSRAM。释放使用 SDK 的通用堆释放函数。此适配依赖已验证的 ESP-IDF 5.5.5 分配器符号，升级 SDK 后需重新验证。
- 显示提交不再把整个 RGB 驱动调用包在关中断临界区内；临界区仅读取帧完成计数，提交后等待新的完成事件才复用帧缓冲。保持原 10 MHz PCLK、20 行 bounce、双帧缓冲及背光配置。实际视觉稳定性需实机确认。

## 验证

`tests/browser_document_test.c` 可用普通 C 编译器编译，与 `src/browser_document.c` 一起运行。覆盖中文、实体、标题、相对链接、脚本／注释跳过、分块边界、长输入、UTF-8 截断和 600 组异常输入检查。

```text
cc -std=c11 -Wall -Wextra -I src tests/browser_document_test.c src/browser_document.c -o browser_document_test
```

固件通过 `platformio run` 编译。设备上的联网阅读、触摸翻页、链接跳转、取消及退出后资源回收需结合串口日志和实机操作验证。

`tests/browser_body_test.c` 验证固定长度、连接关闭、分块大小、扩展、尾部、二进制载荷、1～31 字节输入边界、异常格式、提前断流和 600 组异常输入；`tests/browser_body_parser_test.c` 验证传输分块跨中文、实体和 HTML 标签时的正文解析，以及正文／下载限制。

```text
cc -std=c11 -Wall -Wextra -I src tests/browser_body_test.c src/browser_body.c -o browser_body_test
cc -std=c11 -Wall -Wextra -I src tests/browser_body_parser_test.c src/browser_body.c src/browser_document.c -o browser_body_parser_test
```

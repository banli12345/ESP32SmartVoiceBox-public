# API 配置（Breeze OS 1.28.0）

## 推荐：直接在热点网页填写

1. 在设备下拉栏开启热点，长按热点查看名称、密码和网址。手机连接后，打开设备显示的网址（默认http://172.31.42.1/）。
2. 点击网页底部“服务配置”，或直接打开该地址的 `/settings`。
3. AI卡填写API基础地址、模型和自己的Key；天气卡填写实况、预报、生活指数地址及Key。生活指数可沿用天气Key，也可取消勾选填写独立Key。
4. 两张卡分别点保存，再点“重启并生效”。保存只写 NVS，当前运行服务仍用旧值；不会自动重启。修改配置不用重新编译，也不需要 SD。
5. 重启后重新连接设备热点并刷新页面；通过设备显示的热点地址访问，STA 局域网地址不提供此入口。页面令牌失效时刷新重新操作。

网页只返回配置状态与非密钥字段，已有密钥不回显；留空表示保留已保存Key，首次配置没有旧 Key 时必须填写。保存一项不会清空另一项未保存的输入。数据写入原有NVS命名空间，重启后统一启用；保存/校验失败保持原配置。通过热点地址访问，写入需要当前页面令牌，非热点主机名和跨站来源会被拒绝。系统更新进行中不能保存配置或重启。AI 地址仅支持 HTTPS；天气地址支持 HTTP/HTTPS，均不能含查询参数、片段或账号密码，详细字段限制在下方。勾选生活指数沿用天气 Key 并保存，会以天气 Key 替代此前独立的指数 Key。原有 SD 导入仍保留，可在下方查阅。

网页修改只写设备 NVS，不回写电脑主文件；SD 中仍有效的旧导入 JSON 可能在下次启动覆盖新设置，应保持两者一致。

## 通用固件与个人配置

默认 `BREEZE_GENERIC_BUILD=ON`，忽略本机私有WiFi头中的默认凭据。AI/天气Key来自设备NVS或SD导入，不编入通用应用；新板未配置时显示未配置，用户通过网页填写自己的值即可。现有设备升级保留自己的NVS，不会因为换成通用应用而清除Key。公开分享使用通用应用/首次部署包，不包含已配置设备的NVS或整片Flash备份。

可运行 `python tests/firmware_generic_config_test.py build/esp32_smart_voice_box.elf` 核对最终ELF的WiFi初始缓冲为空；发布时还需核对私有Key未进入应用镜像。首次部署的 Factory 合并镜像会覆盖配置区域，不能作为网页 OTA 包上传；日常只上传应用 BIN，保留原 NVS 与 SD，不能混用。


## 兼容方式：SD一次性导入

天气、预报、生活指数和AI配置均可由SD一次性导入NVS。本版已支持此流程，之后修改地址或Key只需重新放入JSON并重启，无需重新编译。更换服务仍要求请求参数和返回JSON兼容，不能仅改地址就接入任意厂商。

## SD文件与看不到ai.json的原因

| SD根目录中的路径 | 配置 | 成功后 |
| --- | --- | --- |
| `config/ai.json` | DeepSeek地址、Key、模型和朗读 | 写入NVS `ai_direct` 后删除导入文件 |
| `config/weather.json` | 实况/预报/生活指数地址和Key | 写入NVS `weather_api` 后删除导入文件 |
| `config/ai.example.json`、`weather.example.json` | 不含真实Key的样例 | 不参与导入，可以长期保留 |

芯片重启后从NVS读取，删除导入文件不会丢失配置。电脑上看不到ai.json通常表示导入已成功。想修改时在电脑重新生成或新建文件，复制到SD的config文件夹再重启；不需要导出芯片原有Key。两份配置分别导入，修改一份不要求重新导入另一份。

格式、长度、地址或占位符校验失败，或者NVS保存/提交失败时，不启用新配置、不删除文件，并保留原有效配置。成功写入但文件清理失败时，配置已生效，文件可能仍在，下一次开机会再次尝试导入。普通WiFi/热点、亮度、音色等NVS设置不受影响，不擦除NVS。

## 直接准备天气与生活指数文件

复制仓库 `config/weather.example.json` 为SD `/config/weather.json`，填自己的Key：

```json
{
  "version": 1,
  "api_key": "YOUR_SENIVERSE_API_KEY",
  "now_url": "http://api.seniverse.com/v3/weather/now.json",
  "daily_url": "http://api.seniverse.com/v3/weather/daily.json",
  "life_url": "https://api.seniverse.com/v3/life/suggestion.json"
}
```

占位符必须替换。`api_key` 用于实况和预报；生活指数默认用同一个Key。需要单独的生活指数Key时，增加 `life_api_key` 字段。三个URL字段省略时使用上面的现行地址，明确写空字符串则无效。文件需要整数版本1和有效api_key；最长2048字节，UTF-8，接受BOM。

地址为完整的HTTP/HTTPS端点，不含`?`查询、`#`片段、账号密码或反斜杠，最长191字节。客户端追加key、location、language等参数。Key为1～95个可打印ASCII字符，不含空白；示例占位符不会被导入。输入无效时保留原NVS和SD文件。

当前继续使用weather/now.json，daily.json取三日，life/suggestion.json读取穿衣、紫外线、感冒和运动。国内位置表先把中文市县解析成V3 ID；账号是否有地点及接口权限仍由服务端决定，不探测或切换整点实况接口。更换生活指数配置后优先重新请求，避免当天旧缓存掩盖新地址/Key的效果。

## AI文件

复制 `config/ai.example.json` 为SD `/config/ai.json`，填写自己的值：

```json
{
  "version": 1,
  "api_base": "https://api.deepseek.com",
  "api_key": "YOUR_DEEPSEEK_API_KEY",
  "model": "deepseek-flash",
  "speak": true
}
```

地址最长191字节，仅HTTPS，不含账号信息、查询、片段和反斜杠；Key为8～255个可打印ASCII字符，模型名1～63字符，speak为布尔值。沿用现有AI导入流程，地址/Key/模型改完后只需重新导入。男女声仍在设备语音设置保存，不靠修改Key切换。

## 从一份电脑配置生成两份文件

公开用户先复制 `config/services.example.json` 为本机 `config/services.private.json` 并填自己的Key；电脑主文件只留在自己的电脑，不提交仓库。这份合并JSON是电脑上的编辑主文件，不直接当作SD导入文件。

```powershell
python scripts/configure_services.py config/services.private.json
```

现在生成 `config/weather.json` 和 `config/ai.json`，不再写入私有头文件。可直接指定SD路径：

```powershell
python scripts/configure_services.py config/services.private.json --weather-output F:/config/weather.json --ai-output F:/config/ai.json
```

请换成自己的盘符，复制前备份需要保留的旧配置。工具只用Python标准库，不访问设备或网络，不打印Key；先验证全部输入再生成文件。将两份文件放到SD后安全弹出，插回设备并重启。保留电脑主文件作为以后修改的来源，导入文件删除后从主文件重新生成即可。

AI也可沿用 `scripts/provision_ai.py config/ai.json --port COMx` 上传到SD。天气JSON同样是SD文件，本版不要求通过串口。没有新文件、已有有效NVS时，设备继续使用原配置。运行语音与媒体仍需可用SD卡，配置存NVS不代表音频缓存也迁入NVS。

## 代码入口

| 修改内容 | 位置 |
| --- | --- |
| 天气/生活地址和Key导入、验证与NVS | `src/WeatherConfig.cpp`、`src/WeatherConfig.h` |
| 网页表单、HTTP 校验与保存/重启 | `src/service_config_page.h`、`src/ServiceConfigWeb.cpp` |
| AI导入、验证与NVS | `src/AiConfig.cpp`、`src/AiConfig.h` |
| 天气实况/预报参数与JSON解析 | `src/setwifi.cpp` |
| 四项生活指数参数与解析 | `src/LifeData.cpp` |
| 文本/语音协议 | `src/AiDirectClient.cpp`、`src/AiSpeech.cpp` |
| 网易云搜索、音源、歌词 | `src/OnlineMusic.cpp`，仍为源码中的公开端点，不使用本次天气配置 |
| 电脑合并配置生成器 | `scripts/configure_services.py` |

`src/private_config.h` 只保留可选WiFi启动默认值；旧DEVICE_WEATHER_API_KEY宏不再参与当前固件。农历仍由本地计算，无需Key。真实JSON、NVS/Flash备份和生成的导入包只留在本机/私密范围，公开示例只有占位符。
## 保留与分享边界

网页修改设备 NVS，不回写电脑上的主文件；再次插入旧 SD JSON 会按导入规则覆盖对应配置。公开源码及示例只含占位符。分享自己编译的应用前保持通用构建，并检查镜像不含自己的凭据；不要分享已配置设备的 NVS/整片 Flash 转储或导入 JSON。

“不回显”不代表 NVS 已加密。当前配置页经设备热点 HTTP 访问，天气默认实况/预报端点也使用 HTTP；在自己信任的热点中配置。HTTPS 天气端点需与账号服务兼容，客户端会校验服务器证书。设备配置存储、请求协议与源码是否公开是不同的边界。

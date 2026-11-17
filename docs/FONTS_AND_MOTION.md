# 当前字库、键盘与唤醒

桌面、进入和退出应用使用约180ms层叠侧滑，应用内部直接显示；青禾开机约3秒。界面图是生产LVGL主机渲染，不是设备帧率证据。

## SD 字库与输入法

当前系统16/20/32px字库对应261/7406/1254个字形。32px覆盖官方3239条国内位置记录所需汉字，20px补“埇氹沚浉瀍猇硚鄠”；原有~·《》、全角冒号及旧字形保留。背单词cet4_20.bin独立部署。OTA不会替换SD资源。

仓库提供字符源、Python脚本和天气PNG，TTF、生成BIN/ZIP及本机Node工具不随Git提供。字库更新脚本需要已有配套BIN以保留旧字符，不是空目录一键生成完整运行资源；准备自己的TTF，并调整脚本中的Node/NPX路径或相应参数。

```powershell
python scripts/update_chinese20_font.py "埇氹沚浉瀍猇硚鄠" --size 20 --keep-subset --output sd_card_files/fonts/chinese_20.bin
python scripts/update_chinese32_city_font.py --font simhei.ttf --output sd_card_files/fonts/chinese_32.bin
python scripts/generate_pinyin_dict.py
```

第一条给已有配套20px字库补齐8个城市稀有字。生成后脚本检查原字形保留。完整词典依赖pypinyin；见scripts/requirements.txt。备份SD原文件，再复制fonts/chinese_20.bin、fonts/chinese_32.bin、ime/pinyin_full.txt到根目录并重启。16px无需因城市更新重做。

<p align="center"><img src="images/keyboard-space.png" width="360" alt="四排键盘与Space"></p>

天气的40枚图标随固件编译，无需上传SD；重建入口为scripts/convert_weather_icons.py。城市索引由tools/gen_weather_city_data.py从assets/weather_cities/domestic.json生成，采用压缩块，不在启动时全量解压。

## 息屏与唤醒

设置1、2、3、5、10、20、30、60分钟或永不；以触摸空闲计时。息屏后双击，或ROLL/PITCH双轴转动唤醒。判断角速度合成0.525rad/s持续60ms，忽略YAW/Z，刚息屏600ms内抑制。用户已确认双轴与双击正常。

[部署](GETTING_STARTED.md) · [中文输入](CHINESE_INPUT.md) · [背单词资源](WORD_MATCH.md)

# 掷圣杯·吕祖求签

为 **FoloToy AI Passport**（ESP32-C3）打造的纯离线求签问卜应用。把设备变成随身求签小物件——长按 OK 摇杯问神，达成三圣杯后自动摇签筒求得吕祖百签之一，还有连续香火日、六种杯妆皮肤、称号系统和连圣杯小游戏。

![状态](https://img.shields.io/badge/编译-通过-brightgreen)
![许可](https://img.shields.io/badge/license-MIT-blue)
![平台](https://img.shields.io/badge/平台-ESP32--C3-green)
![FoloToy](https://img.shields.io/badge/FoloToy-AI%20Passport-orange)

## 功能

- **传统三掷仪式**——长按 OK 摇杯，每日掷三次；连续三圣杯（两片皆凹面朝上）即达成仪式，自动摇签筒
- **吕祖百签**——每签含典故四字、七言四句诗和简明解曰，等级从"上上"到"下签"共六档
- **六种杯妆皮肤**——木杯（默认）、玉杯、金杯、琉璃杯、月老杯、财神杯，随连续香火日递增解锁
- **称号系统**——8 项成就（一次三掷全圣杯、累计笑杯三十、连续香火日、收集签种、金签十种等）
- **连圣杯小游戏**——每掷一回，看能连续掷出多少圣杯
- **每日香火**——开机即得一香火日，连续签到保持记录；断签归零
- **纯离线**——无需 Wi-Fi，无需手机，无需服务器，全部在设备本地运行
- **音效与摇动动画**——摇杯时的咔嗒声、掷出结果音、金签铃声、胜败提示

## 硬件参数

| 项目 | 规格 |
|------|------|
| 主控 | ESP32-C3 |
| 内置 Flash | 8 MB |
| 屏幕 | 240 × 320 ST7789P3 |
| 音频 | ES8311 codec, I2S 全双工 |
| 输入 | 三键（ADC 分压） |

## 编译与刷机

```bash
# 克隆
git clone https://github.com/skyeric999/LvZu-Fortune-Stick.git
cd LvZu-Fortune-Stick

# 配置 ESP-IDF (v5.5+) 并编译
idf.py set-target esp32c3
idf.py build

# 从 0x0 刷写合并固件
idf.py -p COMx flash
```

或直接刷写预编译合并 bin：

```bash
python -m esptool --chip esp32c3 -p COMx -b 460800 \
  --before default_reset --after hard_reset \
  write_flash --flash_mode dio --flash_size 8MB --flash_freq 80m \
  0x0 build/FoloToy-AI-Passport-full.bin
```

## 按键说明

| 动作 | 短按 | 长按 |
|------|------|------|
| OK | 确认/进入/求签 | 主页摇杯 / 返回上一级 |
| UP | 上移/上滚 | — |
| DOWN | 下移/下滚 | — |

## 项目结构

```
main/
├── main.c               # 入口，初始化顺序，按键分发给 UI
├── ui_shengbei.c/.h     # LVGL 全部页面（主页、掷果、签文、签文库、杯妆、称号、香火、游戏、彩蛋）
├── shengbei_model.c/.h  # 纯逻辑：掷杯、求签、皮肤、称号、香火
├── shengbei_store.c/.h  # NVS 持久化（model → flash）
├── sfx_player.c/.h      # 音效 worker：队列 PCM 播放 + 代次取消
├── sfx_tones.c/.h       # 波形合成：正弦 + 噪声 + 指数衰减包络
├── qian_data.c/.h       # 吕祖百签（典故、签诗、解曰、等级）
├── assets/
│   ├── fonts/           # 自定义子集 LVGL 字体（16px、26px）
│   └── images/          # 圣杯实拍图（ARGB8888，白底转透明）
└── CMakeLists.txt

tools/
├── gen_font_subset.py   # 扫描源码字符串 → 生成最小 LVGL 字体子集
├── gen_cup_image.py     # 圣杯图 → ARGB8888 LVGL 图片 + 透明底
└── font_inventory_*.txt # 字体覆盖审计报告

tests/
└── test_shengbei_model.c # Unity 单元测试
```

## 掷杯规则

核心仪式遵循传统民俗规制：

1. **每日三掷**——每次掷杯有四种结果：
   - **圣杯**（两片皆凹面朝上）：神明允准
   - **笑杯**（一上一下）：神明莞尔笑而不答
   - **阴杯**（两片皆凹面朝下）：神明示否
   - **平杯**（立在边缘）：罕见，按笑杯处理

2. **仪式达成**——三掷中至少两圣杯即达"圣杯允签"，可摇签筒

3. **否极泰来**——三阴杯的特殊结果仍可求得一签

4. **每日重置**——三掷机会午夜刷新，不累积

## 社区发布

此玩法已提交 FoloToy AI Passport 社区审核：
- **项目**：[community-3d57b7a2](https://ai-passport.folotoy.cn/)（待审核）
- **固件 SHA256**：`6b3f2511029c208f1b1ae9c7ddba4723ee4d0cbb20e33a17b55b7d76c69fcdae`

## 许可

MIT 许可，欢迎二次创作。FoloToy AI Passport 框架 © FoloToy。

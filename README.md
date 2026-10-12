# Lu Zu Fortune Sticks · Crescent Cup Divination (掷圣杯·吕祖求签)

A fully offline divination app for the **FoloToy AI Passport** (ESP32-C3). Turn your device into a pocket-sized fortune-telling companion — shake the crescent moon cups (圣杯), draw one of 100 Lü Zu fortune sticks, and track your daily incense streak.

![Status](https://img.shields.io/badge/build-passing-brightgreen)
![License](https://img.shields.io/badge/license-MIT-blue)
![Platform](https://img.shields.io/badge/platform-ESP32--C3-green)
![FoloToy](https://img.shields.io/badge/FoloToy-AI%20Passport-orange)

## Features

- **Traditional three-toss ritual** — shake the cups, throw three times daily; three consecutive holy cups (圣杯) unlock your fortune stick
- **100 Lü Zu fortune sticks** — each with a title, four-line poem, and interpretation, graded from 上上 (top auspicious) to 下签 (unfavorable)
- **Six cup skins** — wood (default), jade, gold, glazed, matchmaker, and wealth god; unlock progressively by maintaining your incense streak
- **Achievement titles** — 8 titles for notable feats (three holy cups in one toss, 30 laughing cups, daily incense streaks, collection milestones, golden sticks)
- **Chain of Holy Cups mini-game** — one-toss-per-round, how many holy cups can you land in a row?
- **Daily incense system** — open the app daily to extend your streak; keep it alive to unlock premium skins
- **Fully offline** — no Wi-Fi, no phone, no server. Everything runs locally on the device
- **Sound effects with vibration animation** — shaking clacks, throw results, golden stick jingle, win/lose chimes

## Hardware

| Item | Specification |
|------|---------------|
| MCU | ESP32-C3 |
| Flash | 8 MB |
| Display | 240 × 320 ST7789P3 |
| Audio | ES8311 codec, I2S full duplex |
| Input | 3 buttons (ADC voltage divider) |

## Build & Flash

```bash
# Clone
git clone https://github.com/skyeric999/LvZu-Fortune-Stick.git
cd LvZu-Fortune-Stick

# Set up ESP-IDF (v5.5+) and build
idf.py set-target esp32c3
idf.py build

# Flash merged firmware from 0x0
idf.py -p COMx flash
```

Or flash the pre-built merged binary directly:

```bash
python -m esptool --chip esp32c3 -p COMx -b 460800 \
  --before default_reset --after hard_reset \
  write_flash --flash_mode dio --flash_size 8MB --flash_freq 80m \
  0x0 build/FoloToy-AI-Passport-full.bin
```

## Controls

| Action | Short press | Long press |
|--------|-------------|------------|
| OK | Confirm / enter / draw stick | Shake cups (home) / return from page |
| UP | Navigate up / scroll up | — |
| DOWN | Navigate down / scroll down | — |

## Project Structure

```
main/
├── main.c               # App entry, init order, input dispatcher
├── ui_shengbei.c/.h     # All LVGL pages (home, result, stick, library, skins, titles, incense, game, eggs)
├── shengbei_model.c/.h  # Pure game logic: ritual throws, stick draw, skins, titles, incense
├── shengbei_store.c/.h  # NVS persistence (model → flash)
├── sfx_player.c/.h      # Audio worker: queue-based PCM playback with generation-based cancellation
├── sfx_tones.c/.h       # Waveform synthesis: sine + noise + exponential decay
├── qian_data.c/.h       # 100 Lü Zu fortune sticks (title, poem, interpretation, grade)
├── assets/
│   ├── fonts/           # Custom-subset LVGL fonts (16px, 26px)
│   └── images/          # Cup photo (ARGB8888, transparent background)
└── CMakeLists.txt

tools/
├── gen_font_subset.py   # Scan source strings → generate minimal LVGL font subset
├── gen_cup_image.py     # Convert cup photo → ARGB8888 LVGL image with transparency
└── font_inventory_*.txt # Font coverage audit reports

tests/
└── test_shengbei_model.c # Unity tests for the model logic
```

## Ritual Rules

The core 掷杯 ceremony follows traditional Chinese folk religion:

1. **Three tosses daily** — each toss yields one of four outcomes:
   - **圣杯** (holy cup) — both cups concave-up: the deity approves
   - **笑杯** (laughing cup) — one up, one down: the deity smiles but does not answer
   - **阴杯** (dark cup) — both concave-down: the deity denies
   - **平杯** (flat cup) — landing exactly on edge: rare, treated as 笑杯

2. **Qualified throw** — at least two holy cups in the three-toss sequence

3. **否极泰来** (from the worst to the best) — three dark cups can still yield a fortune stick as a special outcome

4. **Daily reset** — three tosses refresh at midnight; unused tosses do not accumulate

## Community

This play is published on the FoloToy AI Passport community:
- **Project**: [community-3d57b7a2](https://ai-passport.folotoy.cn/) (pending review)
- **Firmware SHA256**: `6b3f2511029c208f1b1ae9c7ddba4723ee4d0cbb20e33a17b55b7d76c69fcdae`

## License

MIT — feel free to fork, remix, and adapt. The FoloToy AI Passport framework is © FoloToy.

// main/sfx_tones.h —— 音效波形合成（纯函数，无 FreeRTOS/LVGL 依赖，可宿主测试）。
// 全部音效本地合成，无音频素材文件。
#pragma once

#include <stddef.h>
#include <stdint.h>

#define SFX_SAMPLE_RATE 16000

typedef enum {
    SFX_CLACK = 0,      // 木杯碰撞（短促哒声）
    SFX_CHIME_SHENG,    // 圣杯：上行双音
    SFX_PING_XIAO,      // 笑杯：单音轻响
    SFX_THUD_YIN,       // 阴杯：低沉闷响
    SFX_RATTLE,         // 签筒摇动（竹签哗啦）
    SFX_WIN,            // 小游戏胜利
    SFX_LOSE,           // 小游戏结束
    SFX_GOLD,           // 金签闪耀
    SFX_COUNT,
} sfx_id_t;

// 渲染一段音效到 buf（16bit 单声道）。返回写入的样本数；
// 失败或 buf 不足时返回 0。rand_state 用于碰撞音的随机抖动。
size_t sfx_render(sfx_id_t id, int16_t *buf, size_t max_samples, uint32_t *rand_state);

// 单段音效的最大样本数（据此分配播放缓冲）。
size_t sfx_max_samples(void);

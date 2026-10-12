// main/sfx_tones.c —— 音效波形合成：正弦/噪声 + 指数衰减包络，16bit 单声道 16kHz。
#include "sfx_tones.h"

#include <math.h>

#define SFX_PI 3.14159265358979f

typedef struct {
    float freq;        // 基频 Hz（0=纯噪声）
    uint32_t ms;       // 时长
    uint8_t noise_mix; // 噪声占比 0..255
    float decay;       // 每毫秒衰减系数（相对）
    float gain;        // 峰值增益 0..1
} tone_def_t;

// 音色表：所有音效 = 一串依次衔接的 tone。
static const struct {
    uint8_t tone_count;
    tone_def_t tones[4];
} SFX_TABLE[SFX_COUNT] = {
    [SFX_CLACK] = { 2, { { 210.0f, 24, 150, 0.86f, 0.85f },
                         { 340.0f, 42, 90, 0.80f, 0.55f } } },
    [SFX_CHIME_SHENG] = { 2, { { 659.0f, 110, 0, 0.988f, 0.5f },
                               { 880.0f, 160, 0, 0.990f, 0.5f } } },
    [SFX_PING_XIAO] = { 1, { { 1046.0f, 130, 0, 0.988f, 0.45f } } },
    [SFX_THUD_YIN] = { 1, { { 116.0f, 190, 60, 0.982f, 0.75f } } },
    [SFX_RATTLE] = { 4, { { 520.0f, 55, 200, 0.86f, 0.45f },
                          { 470.0f, 55, 200, 0.86f, 0.45f },
                          { 540.0f, 55, 200, 0.86f, 0.45f },
                          { 500.0f, 90, 200, 0.88f, 0.45f } } },
    [SFX_WIN] = { 3, { { 523.0f, 100, 0, 0.990f, 0.5f },
                       { 659.0f, 100, 0, 0.990f, 0.5f },
                       { 784.0f, 190, 0, 0.992f, 0.5f } } },
    [SFX_LOSE] = { 2, { { 330.0f, 150, 0, 0.990f, 0.45f },
                        { 247.0f, 220, 0, 0.992f, 0.45f } } },
    [SFX_GOLD] = { 4, { { 784.0f, 70, 0, 0.992f, 0.42f },
                        { 988.0f, 70, 0, 0.992f, 0.42f },
                        { 1175.0f, 70, 0, 0.992f, 0.42f },
                        { 1568.0f, 150, 0, 0.993f, 0.42f } } },
};

static uint32_t lcg(uint32_t *state) {
    *state = *state * 1664525u + 1013904223u;
    return *state >> 8;
}

size_t sfx_render(sfx_id_t id, int16_t *buf, size_t max_samples, uint32_t *rand_state) {
    if (id >= SFX_COUNT || !buf || !rand_state) return 0;

    size_t total = 0;
    for (int t = 0; t < SFX_TABLE[id].tone_count; t++) {
        const tone_def_t *def = &SFX_TABLE[id].tones[t];
        // 抖动：碰撞类音每次频率 ±8%，听感更自然。
        float freq = def->freq;
        if (def->noise_mix > 0 && freq > 0) {
            float jitter = 1.0f + (float)(lcg(rand_state) % 17 - 8) / 100.0f;
            freq *= jitter;
        }
        size_t n = (size_t)(SFX_SAMPLE_RATE * def->ms / 1000);
        if (total + n > max_samples) return 0;

        float noise_gain = (float)def->noise_mix / 255.0f;
        float amp = 1.0f;
        for (size_t i = 0; i < n; i++) {
            if (i % 8 == 0) amp *= def->decay;  // 每 0.5ms 衰减一次
            float phase = 2.0f * SFX_PI * freq * (float)i / (float)SFX_SAMPLE_RATE;
            float tone = sinf(phase);
            float noise = (float)(lcg(rand_state) % 4096) / 2048.0f - 1.0f;
            float sample = (1.0f - noise_gain) * tone + noise_gain * noise;
            buf[total + i] = (int16_t)(sample * amp * def->gain * 32000.0f * 0.5f);
        }
        total += n;
    }
    return total;
}

size_t sfx_max_samples(void) {
    size_t worst = 0;
    for (int id = 0; id < SFX_COUNT; id++) {
        size_t n = 0;
        for (int t = 0; t < SFX_TABLE[id].tone_count; t++) {
            n += (size_t)(SFX_SAMPLE_RATE * SFX_TABLE[id].tones[t].ms / 1000);
        }
        if (n > worst) worst = n;
    }
    return worst;
}

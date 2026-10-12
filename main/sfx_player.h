// main/sfx_player.h —— 音效播放 worker（改编自 demo/tetris 的 rtttl_player 模式）。
// worker 独占音频 PCM 写入；外部只投递请求。摇杯循环音以"代次"打断。
#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "sfx_tones.h"

// 启动 worker（内部完成 bsp_audio_init/set_format；幂等）。
esp_err_t sfx_player_start(void);

// 停止 worker（应用退出用；一般不需要）。
void sfx_player_stop(void);

// 播放一段音效（替换正在播放的音效）。
void sfx_play(sfx_id_t id);

// 摇动循环音：开始后循环播放碰撞声，直到 sfx_shake_stop()。
void sfx_shake_start(void);
void sfx_shake_stop(void);

bool sfx_is_playing(void);

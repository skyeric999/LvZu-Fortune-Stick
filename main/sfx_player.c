// main/sfx_player.c —— 音效 worker：请求队列 + 代次取消，PCM 分块写入 bsp_audio。
#include "sfx_player.h"

#include <stdlib.h>

#include "bsp_audio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define SFX_VOLUME 75
#define SFX_CHUNK 256

static const char *TAG = "sfx";
static QueueHandle_t s_queue;
static TaskHandle_t s_task;
static volatile uint32_t s_generation;
static volatile bool s_playing;
static int16_t *s_render_buf;  // 单段音效渲染缓冲（worker 生命周期）

typedef enum {
    REQ_ONE = 0,   // 播放一段
    REQ_SHAKE,     // 进入摇动循环
} req_kind_t;

typedef struct {
    req_kind_t kind;
    sfx_id_t id;
    uint32_t generation;
} sfx_req_t;

static uint32_t rand_state = 0x1A2B3C4D;

static void write_chunks(const int16_t *buf, size_t samples, uint32_t generation) {
    size_t written = 0;
    while (written < samples) {
        if (generation != s_generation) return;
        size_t n = samples - written;
        if (n > SFX_CHUNK) n = SFX_CHUNK;
        if (bsp_audio_write(buf + written, n * sizeof(int16_t)) != ESP_OK) return;
        written += n;
    }
}

static void play_effect(sfx_id_t id, uint32_t generation) {
    size_t n = sfx_render(id, s_render_buf, SFX_CHUNK * 40, &rand_state);
    if (n > 0) write_chunks(s_render_buf, n, generation);
}

static void player_task(void *arg) {
    (void)arg;
    if (bsp_audio_init() != ESP_OK || bsp_audio_set_format(SFX_SAMPLE_RATE, 16, 1) != ESP_OK) {
        ESP_LOGE(TAG, "音频初始化失败，音效不可用");
        vTaskDelete(NULL);  // 静音运行：UI 不依赖音效存在
        return;
    }
    bsp_audio_set_volume(SFX_VOLUME);

    sfx_req_t req;
    uint32_t gen = s_generation;
    bool shaking = false;
    for (;;) {
        // 摇动循环：无请求时也持续出声，直到代次变化。
        if (shaking) {
            play_effect(SFX_CLACK, gen);
            if (xQueueReceive(s_queue, &req, pdMS_TO_TICKS(90)) == pdTRUE) {
                if (req.generation == s_generation) {
                    if (req.kind == REQ_ONE) play_effect(req.id, req.generation);
                    else if (req.kind == REQ_SHAKE) { /* 重复开启，忽略 */ }
                    else { shaking = false; }
                }
            }
            if (gen != s_generation) shaking = false;
            continue;
        }
        if (xQueueReceive(s_queue, &req, portMAX_DELAY) != pdTRUE) continue;
        if (req.generation != s_generation) continue;
        s_playing = true;
        if (req.kind == REQ_ONE) {
            play_effect(req.id, req.generation);
        } else if (req.kind == REQ_SHAKE) {
            gen = s_generation;
            shaking = true;
            s_playing = false;
            continue;
        }
        s_playing = false;
    }
}

esp_err_t sfx_player_start(void) {
    if (s_task) return ESP_OK;
    s_render_buf = malloc(SFX_CHUNK * 40 * sizeof(int16_t));
    if (!s_render_buf) return ESP_ERR_NO_MEM;
    s_queue = xQueueCreate(4, sizeof(sfx_req_t));
    if (!s_queue) {
        free(s_render_buf);
        s_render_buf = NULL;
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(player_task, "sfx", 3584, NULL, 4, &s_task) != pdPASS) {
        vQueueDelete(s_queue);
        s_queue = NULL;
        free(s_render_buf);
        s_render_buf = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void sfx_player_stop(void) {
    s_generation++;
    if (s_queue) xQueueReset(s_queue);
}

static void post(sfx_req_t *req) {
    if (!s_queue) return;
    req->generation = ++s_generation;
    xQueueReset(s_queue);
    (void)xQueueSend(s_queue, req, 0);
}

void sfx_play(sfx_id_t id) {
    sfx_req_t req = { .kind = REQ_ONE, .id = id };
    post(&req);
}

void sfx_shake_start(void) {
    sfx_req_t req = { .kind = REQ_SHAKE, .id = SFX_CLACK };
    post(&req);
}

void sfx_shake_stop(void) {
    s_generation++;  // 使摇动循环与排队音效全部失效
    if (s_queue) xQueueReset(s_queue);
}

bool sfx_is_playing(void) {
    return s_playing;
}

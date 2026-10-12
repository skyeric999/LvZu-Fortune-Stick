// main/main.c —— 掷圣杯·吕祖求签 应用入口。
//
// 初始化顺序：显示 → 按键输入队列 → 音效 worker → 电量计 → NVS 档案 →
// 开机签到（一个香火日）→ UI。单项外设失败不阻塞：音效静音、电量角标隐藏，
// 档案存取失败时本会话照常可玩、只是不落盘。
//
// 按键语义：回调只入队（见 bsp_button.h）；分发任务逐条取出交给 ui_sb_input，
// 由它持有 bsp_lvgl_lock() 操作 LVGL。
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_pins.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "key_guard.h"
#include "shengbei_model.h"
#include "shengbei_store.h"
#include "sfx_player.h"
#include "ui_shengbei.h"

static const char *TAG = "shengbei";

#define INPUT_QUEUE_DEPTH 8
#define INPUT_TASK_STACK  5120

typedef struct {
    bsp_btn_t btn;
    bsp_btn_ev_t event;
} input_event_t;

static QueueHandle_t s_input_queue;
static volatile bool s_input_ready;
static sb_model_t s_model;

// 按键唤醒（深睡后重启）时那颗键还按着，先吞掉它的动作直到松手（见 on_key）。
#define KEY_GUARD_MAX_US (10LL * 1000 * 1000)
static key_guard_t s_key_guard;

// 按键回调运行在共享 esp_timer 任务：只入队，立即返回。
static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user) {
    (void)user;
    // 先吞守卫再查就绪：release 可能晚于初始化电压采样、早于分发就绪到来，
    // 若在这里被丢弃，守卫会一直武装、吞掉下一次真实按键。
    const bool was_armed = s_key_guard.armed;
    if (key_guard_consume(&s_key_guard, ev == BSP_BTN_RELEASE, esp_timer_get_time())) {
        if (was_armed && !s_key_guard.armed) {
            ESP_LOGI(TAG, "唤醒键已松手,按键恢复响应");
        }
        return;
    }
    if (!s_input_ready || !s_input_queue) return;
    const input_event_t input = { .btn = btn, .event = ev };
    (void)xQueueSend(s_input_queue, &input, 0);
}

static void input_task(void *arg) {
    (void)arg;
    input_event_t input;
    for (;;) {
        if (xQueueReceive(s_input_queue, &input, portMAX_DELAY) == pdTRUE) {
            ui_sb_input(input.btn, input.event);
        }
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "掷圣杯·吕祖求签 启动");
    const uint32_t wake_causes = esp_sleep_get_wakeup_causes();
    // 按键唤醒时,那颗键在整个启动期间还按着。先挡住它的动作(见 on_key)。
    if (wake_causes & (1u << ESP_SLEEP_WAKEUP_GPIO)) {
        key_guard_arm(&s_key_guard, esp_timer_get_time(), KEY_GUARD_MAX_US);
        ESP_LOGI(TAG, "按键唤醒:先屏蔽按键动作,直到那颗键松手");
    }

    // 屏幕是应用的载体：失败无法继续，直接退出。
    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "显示/LVGL 初始化失败,应用无法继续。"
                      "检查 SPI 接线(MOSI=%d SCLK=%d CS=%d DC=%d BL=%d)",
                 BSP_LCD_MOSI, BSP_LCD_SCLK, BSP_LCD_CS, BSP_LCD_DC, BSP_LCD_BL);
        return;
    }
    bsp_display_backlight(100);

    // 按键输入队列与分发任务。
    bool input_ok = false;
    s_input_queue = xQueueCreate(INPUT_QUEUE_DEPTH, sizeof(input_event_t));
    if (s_input_queue) {
        if (xTaskCreate(input_task, "sb_input", INPUT_TASK_STACK, NULL, 5,
                        NULL) == pdPASS) {
            input_ok = bsp_button_init(on_key, NULL) == ESP_OK;
            if (!input_ok) {
                ESP_LOGE(TAG, "按键初始化失败");
            } else if (s_key_guard.armed) {
                // 若唤醒键在按键驱动启动前就已松手,组件不会产生 release 事件,
                // 守卫会一直拦到兜底。按真实电压同步一次:已松手立即撤防。
                const bool released = bsp_button_read_mv() >= BSP_BTN_MV_RELEASED_MIN;
                key_guard_sync_released(&s_key_guard, released);
                if (!s_key_guard.armed) {
                    ESP_LOGI(TAG, "唤醒键在按键驱动启动前已松手,提前撤防");
                }
            }
        } else {
            ESP_LOGE(TAG, "按键输入任务创建失败");
        }
    } else {
        ESP_LOGE(TAG, "按键事件队列创建失败");
    }

    // 音效 worker（内部完成音频初始化；失败则应用静音运行）。
    esp_err_t sfx_err = sfx_player_start();
    if (sfx_err != ESP_OK) {
        ESP_LOGE(TAG, "音效启动失败(%s),本会话静音", esp_err_to_name(sfx_err));
    }

    // 电量计（不应答时 UI 隐藏角标）。
    if (bsp_battery_init() != ESP_OK) {
        ESP_LOGW(TAG, "电量计不可用,角标隐藏");
    }

    // 档案：载入持久化存档,无档用默认值。
    sb_model_defaults(&s_model);
    if (!shengbei_store_init(&s_model)) {
        ESP_LOGW(TAG, "档案存取不可用,本会话改动不会持久化");
    }

    // 开机即新香火日：签到、结转解锁与称号,立即请求落盘。
    sb_events_t events;
    sb_events_clear(&events);
    sb_model_open_day(&s_model, &events);
    shengbei_store_request_save(&s_model);

    // xorshift 种子不能为 0,兜底一个非零值。
    uint32_t seed = (uint32_t)esp_random();
    if (seed == 0) seed = 0x1234ABCD;

    if (bsp_lvgl_lock(1000)) {
        ui_sb_init(&s_model, &events, seed);
        bsp_lvgl_unlock();
        s_input_ready = true;
    } else {
        ESP_LOGE(TAG, "LVGL 锁获取失败,UI 未启动");
    }

    ESP_LOGI(TAG, "就绪: input=%d sfx=%s", input_ok ? 1 : 0,
             sfx_err == ESP_OK ? "on" : "muted");
}

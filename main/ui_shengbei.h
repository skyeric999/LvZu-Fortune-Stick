// main/ui_shengbei.h —— 掷圣杯·吕祖求签 全新界面（庙宇主题，独立于基线 demo 外壳）。
// 所有 LVGL 调用发生在输入任务并持有 bsp_lvgl_lock()（由 main.c 保证）。
#pragma once

#include <stdint.h>

#include "bsp_button.h"
#include "shengbei_model.h"

// 初始化界面（根屏+启动页）。open_events 为本次开机的签到/解锁事件（可为 NULL）。
void ui_sb_init(sb_model_t *model, const sb_events_t *open_events, uint32_t rand_seed);

// 按键事件入口（输入任务中调用，内部自取 LVGL 锁）。
void ui_sb_input(bsp_btn_t btn, bsp_btn_ev_t ev);

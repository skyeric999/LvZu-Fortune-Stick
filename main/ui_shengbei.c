// main/ui_shengbei.c —— 掷圣杯·吕祖求签 界面与页面流（全新庙宇主题设计）。
//
// 页面：启动 → 今日(摇杯/摇筒) → 掷果浮层 / 签文页；
//       收藏(签文库/杯妆/称号/香火)、游戏(连圣杯/翻牌配对)、彩蛋浮层。
// 按键：OK 短按=确认，长按=返回（摇杯按住期间忽略）；UP/DOWN=选择/滚动。
// 所有 LVGL 调用都在输入任务内且持有 bsp_lvgl_lock()（由 main.c 与本文件保证）。
#include "ui_shengbei.h"

#include <stdio.h>
#include <string.h>

#include "bsp_battery.h"
#include "bsp_display.h"
#include "esp_log.h"
#include "lvgl.h"
#include "qian_data.h"
#include "shengbei_model.h"
#include "shengbei_store.h"
#include "sfx_player.h"

LV_FONT_DECLARE(app_font_16);
LV_FONT_DECLARE(app_font_26);
LV_IMG_DECLARE(cup_photo);

// 整体画面左移量（用户要求所有界面画面整体向左移动）
#define SB_PAGE_X_OFFSET (-10)

// 字体审计：进入页面后遍历所有 label，逐字符验证字形查找，缺失即打日志。
// 用于真机定位"方框"字符（LVGL 对查找失败的字形静默跳过，无法目视区分）。
static const char *SB_FONT_TAG = "sb_font";

static lv_obj_tree_walk_res_t font_audit_walk(lv_obj_t *obj, void *user_data) {
    (void)user_data;
    if (!lv_obj_check_type(obj, &lv_label_class)) return LV_OBJ_TREE_WALK_NEXT;
    const lv_font_t *font = lv_obj_get_style_text_font(obj, LV_PART_MAIN);
    if (!font) return LV_OBJ_TREE_WALK_NEXT;
    const char *text = lv_label_get_text(obj);
    if (!text) return LV_OBJ_TREE_WALK_NEXT;
    const char *p = text;
    while (*p) {
        uint32_t cp = 0;
        uint8_t len = 1;
        uint8_t b = (uint8_t)*p;
        if (b < 0x80) cp = b;
        else if ((b & 0xE0) == 0xC0) { cp = b & 0x1F; len = 2; }
        else if ((b & 0xF0) == 0xE0) { cp = b & 0x0F; len = 3; }
        else if ((b & 0xF8) == 0xF0) { cp = b & 0x07; len = 4; }
        else { p++; continue; }
        bool bad = false;
        for (uint8_t k = 1; k < len; k++) {
            if ((p[k] & 0xC0) != 0x80) { bad = true; break; }
            cp = (cp << 6) | (p[k] & 0x3F);
        }
        if (bad) { p++; continue; }
        lv_font_glyph_dsc_t dsc;
        if (!lv_font_get_glyph_dsc(font, &dsc, cp, 0)) {
            ESP_LOGW(SB_FONT_TAG, "缺字形 U+%04X label=\"%s\"", (unsigned)cp, text);
        }
        p += len;
    }
    return LV_OBJ_TREE_WALK_NEXT;
}

// ---------------------------------------------------------------------------
// 主题
// ---------------------------------------------------------------------------
#define C_BG        0x2E0F0C  // 深庙红
#define C_PANEL     0x4A1A12  // 供案木
#define C_PANEL_HI  0x5C2318
#define C_INK       0xF3E6C8  // 米金文字
#define C_DIM       0xA98D6F  // 暗金
#define C_GOLD      0xE7C169  // 鎏金
#define C_ACCENT    0xC0392B  // 朱砂
#define C_GOOD      0x8FD3A8  // 吉
#define C_MID       0xD9B36C  // 平
#define C_BAD       0x8FA6C2  // 阴
#define C_TRACK     0x2A100C  // 杯槽

// 皮肤双面配色 {阳面, 阴面}
static const uint32_t SKIN_COLORS[SB_SKIN_COUNT][2] = {
    { 0xC89B62, 0x8A6238 },  // 木杯
    { 0x9ADBB8, 0x53997A },  // 玉杯
    { 0xF2CE7B, 0xC99B3F },  // 金杯
    { 0xAEDCF2, 0x6FA8CC },  // 琉璃杯
    { 0xEFB0C4, 0xC97B96 },  // 月老杯
    { 0xF0BE55, 0xB8862B },  // 财神杯
};

static const char *SKIN_NAMES[SB_SKIN_COUNT] = {
    "木杯", "玉杯", "金杯", "琉璃杯", "月老杯", "财神杯",
};

static const char *SKIN_REQ[SB_SKIN_COUNT] = {
    "随行相伴", "连续三日", "连续七日", "连续三十日", "连续六十日", "连续一百八十日",
};

static const char *TITLE_NAMES[SB_TITLE_COUNT] = {
    "圣杯王", "笑杯达人", "阴杯逆袭", "恒心三十",
    "座上常客", "半百藏签", "全签录", "点签成金",
};

static const char *TITLE_REQ[SB_TITLE_COUNT] = {
    "一次三掷全圣杯", "累计笑杯三十", "否极泰来得签", "连续香火三十日",
    "累计十五香火日", "收集五十种签", "百签尽收", "金签十种",
};

static const char *EGG_TITLE[SB_EGG_COUNT] = {
    "圣圣圣", "笑笑笑", "否极泰来", "百掷",
    "千掷", "七日之约", "初得金签", "缘定三生",
};

static const char *EGG_TEXT[SB_EGG_COUNT] = {
    "三掷皆圣杯，神明全然允准。\n今日宜大展宏图。",
    "三掷皆笑杯，神明莞尔。\n所问之事，笑而不答。",
    "三阴之后，天道好还。\n天赐一签，且看且惜。",
    "累计掷杯百次，心诚则灵。",
    "累计掷杯千次，此心如一。",
    "连续七个香火日，与神有约。",
    "旧签重逢，点签成金。",
    "连续三百六十香火日，\n缘定三生。",
};

// ---------------------------------------------------------------------------
// 页面状态
// ---------------------------------------------------------------------------
typedef enum {
    PG_SPLASH = 0,
    PG_HOME,
    PG_RESULT,    // 掷果浮层
    PG_QIAN,      // 签文
    PG_COLLECT,   // 收藏根
    PG_QIANLIB,   // 签文库
    PG_QIANVIEW,  // 签条目
    PG_SKINS,     // 杯妆
    PG_TITLES,    // 称号
    PG_INCENSE,   // 香火·补签
    PG_GAMES,     // 游戏根
    PG_LIANSHENG, // 连圣杯
    PG_EGG,       // 彩蛋浮层
} sb_page_t;

static sb_page_t s_page = PG_SPLASH;
static sb_model_t *s_model;
static uint32_t s_rand = 0x1234ABCD;

static lv_obj_t *s_scr;
static lv_obj_t *s_page_obj;
static lv_timer_t *s_page_timer;      // 页面私有定时器（退出先删）
static lv_timer_t *s_batt_timer;
static lv_obj_t *s_batt_label;

// 摇动状态（按住 OK 期间）
static bool s_shaking;
static bool s_shake_saw_long;       // 摇动期间 LONG 已触发（长按松手驱动不会再发 CLICK）
static bool s_shake_click_swallow;  // 松手后吞掉那次 CLICK（仅短按摇动时武装）
static lv_obj_t *s_shake_obj;       // 摇动的容器（杯/签筒）
static lv_anim_t s_shake_anim;       // X 轴摇动动画
static lv_anim_t s_shake_anim_y;     // Y 轴摇动动画

// 浮层流转
static uint8_t s_pending_eggs;  // 待展示彩蛋位图
static sb_page_t s_after_eggs;  // 彩蛋播完后去向

// 结果浮层
static sb_throw_t s_last_throw;
static bool s_result_final;     // 本次掷完仪式结束（失败/否极泰来）

// 列表选中
static int s_collect_sel;       // 收藏根: 0..3
static int s_games_sel;         // 游戏根: 0（翻牌配对已移除）
static int s_lib_sel;           // 签文库: 0..99
static int s_skin_sel;          // 杯妆: 0..5
static int s_incense_sel;       // 香火页: 0..1

// 连圣杯
static uint16_t s_lian_run;

// 页面控件引用（刷新用）
static lv_obj_t *s_home_state_label;
static lv_obj_t *s_home_hint_label;
static lv_obj_t *s_home_cups;      // 首页杯/签筒容器
static lv_obj_t *s_home_streak_label;
static lv_obj_t *s_home_incense_label;
static lv_obj_t *s_result_word;
static lv_obj_t *s_result_info;
static lv_obj_t *s_result_cups;
static lv_obj_t *s_lian_run_label;
static lv_obj_t *s_lian_best_label;
static lv_obj_t *s_lian_cups;
static lv_obj_t *s_lian_hint;
static lv_obj_t *s_incense_value_label;

// ---------------------------------------------------------------------------
// 通用小件
// ---------------------------------------------------------------------------
static lv_obj_t *make_label(lv_obj_t *parent, const char *text, const lv_font_t *font,
                            uint32_t color, lv_align_t align, int x, int y) {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    if (text) lv_label_set_text(l, text);
    if (align != LV_ALIGN_DEFAULT) lv_obj_align(l, align, x, y);
    return l;
}

static lv_obj_t *make_panel(lv_obj_t *parent, int x, int y, int w, int h, uint32_t bg) {
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_set_pos(p, x, y);
    lv_obj_set_size(p, w, h);
    lv_obj_set_style_bg_color(p, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(p, 10, 0);
    lv_obj_set_style_border_width(p, 0, 0);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    return p;
}

// 一对杯按结果摆放（实拍圣杯图片，白底已转透明），返回容器（可整体摇动）。
// 表现：按杯妆皮肤着色为基调；圣杯=整体翻面示意一片朝下；阴杯=阴面色加深。
static lv_obj_t *build_cups(lv_obj_t *parent, int cx, int cy, int skin, sb_throw_t r,
                            bool resting) {
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_set_size(box, 144, 144);
    lv_obj_set_x(box, cx - 72);
    lv_obj_set_y(box, cy - 72);
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *img = lv_image_create(box);
    lv_image_set_src(img, &cup_photo);
    lv_obj_center(img);
    // 按杯妆皮肤着色：阳面色为基调，阴杯结果用阴面色加深
    uint32_t base_color = SKIN_COLORS[skin][0];  // 阳面色
    uint8_t base_opa = LV_OPA_40;
    if (!resting && r == SB_THROW_YIN) {
        base_color = SKIN_COLORS[skin][1];  // 阴面色
        base_opa = LV_OPA_70;
    }
    lv_obj_set_style_img_recolor(img, lv_color_hex(base_color), 0);
    lv_obj_set_style_img_recolor_opa(img, base_opa, 0);
    if (!resting && r == SB_THROW_SHENG) {
        lv_image_set_rotation(img, 1800);  // 180°：示意一片翻面
    }
    return box;
}

// 签筒（求签态视觉）
static lv_obj_t *build_tube(lv_obj_t *parent, int cx, int cy) {
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_set_size(box, 90, 80);
    lv_obj_set_x(box, cx - 45);
    lv_obj_set_y(box, cy - 40);
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *tube = make_panel(box, 15, 18, 60, 58, 0x6B4226);
    lv_obj_set_style_radius(tube, 8, 0);
    for (int i = 0; i < 4; i++) {
        lv_obj_t *stick = lv_obj_create(box);
        lv_obj_set_size(stick, 6, 34);
        lv_obj_set_pos(stick, 22 + i * 11, -4 - (i % 2) * 4);
        lv_obj_set_style_bg_color(stick, lv_color_hex(0xD9B36C), 0);
        lv_obj_set_style_bg_opa(stick, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(stick, 3, 0);
        lv_obj_set_style_border_width(stick, 0, 0);
    }
    return box;
}

// ---------------------------------------------------------------------------
// 摇动动画与音效
// ---------------------------------------------------------------------------
static void shake_exec_cb(void *var, int32_t v) {
    lv_obj_set_x((lv_obj_t *)var, v);
}

static void shake_exec_cb_y(void *var, int32_t v) {
    lv_obj_set_y((lv_obj_t *)var, v);
}

static void shake_start(lv_obj_t *target) {
    if (!target) return;
    s_shake_obj = target;
    int32_t base_x = lv_obj_get_x(target);
    int32_t base_y = lv_obj_get_y(target);
    // X 轴：幅度 ±12，70ms 往返
    lv_anim_init(&s_shake_anim);
    lv_anim_set_var(&s_shake_anim, target);
    lv_anim_set_exec_cb(&s_shake_anim, shake_exec_cb);
    lv_anim_set_values(&s_shake_anim, base_x - 12, base_x + 12);
    lv_anim_set_time(&s_shake_anim, 70);
    lv_anim_set_playback_time(&s_shake_anim, 70);
    lv_anim_set_repeat_count(&s_shake_anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&s_shake_anim);
    // Y 轴：幅度 ±5，50ms 往返（与 X 不同频，抖动更自然）
    lv_anim_init(&s_shake_anim_y);
    lv_anim_set_var(&s_shake_anim_y, target);
    lv_anim_set_exec_cb(&s_shake_anim_y, shake_exec_cb_y);
    lv_anim_set_values(&s_shake_anim_y, base_y - 5, base_y + 5);
    lv_anim_set_time(&s_shake_anim_y, 50);
    lv_anim_set_playback_time(&s_shake_anim_y, 50);
    lv_anim_set_repeat_count(&s_shake_anim_y, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&s_shake_anim_y);
    sfx_shake_start();
    ESP_LOGI("sb_ui", "shake_start: target=%p base=(%d,%d)", target, base_x, base_y);
}

static void shake_stop(void) {
    if (!s_shaking) return;
    lv_anim_del(s_shake_obj, shake_exec_cb);
    lv_anim_del(s_shake_obj, shake_exec_cb_y);
    sfx_shake_stop();
    s_shake_obj = NULL;
    s_shaking = false;
    // 仅短按摇动武装吞标志：短按松手驱动会补发 CLICK（需吞掉防止误触新页）；
    // 长按摇动松手驱动只发 RELEASE，不武装——否则会吞掉用户下一次真实点击。
    s_shake_click_swallow = !s_shake_saw_long;
}

// ---------------------------------------------------------------------------
// 页面生命周期
// ---------------------------------------------------------------------------
static void goto_page(sb_page_t p);
static void leave_current(void) {
    if (s_page_timer) {
        lv_timer_del(s_page_timer);
        s_page_timer = NULL;
    }
    if (s_shaking) {
        // 页面切换前先停摇动，避免残留动画/音效访问已删除对象
        s_shaking = false;  // 不走 shake_stop 的 swallow 逻辑
        lv_anim_del(s_shake_obj, shake_exec_cb);
        lv_anim_del(s_shake_obj, shake_exec_cb_y);
        sfx_shake_stop();
        s_shake_obj = NULL;
    }
    if (s_page_obj) {
        lv_obj_delete(s_page_obj);
        s_page_obj = NULL;
    }
    s_home_state_label = s_home_hint_label = s_home_cups = NULL;
    s_home_streak_label = s_home_incense_label = NULL;
    s_result_word = s_result_info = s_result_cups = NULL;
    s_lian_run_label = s_lian_best_label = s_lian_cups = s_lian_hint = NULL;
    s_incense_value_label = NULL;
}

static sb_page_t back_target(sb_page_t p) {
    switch (p) {
        case PG_QIAN: return PG_HOME;
        case PG_COLLECT:
        case PG_GAMES: return PG_HOME;
        case PG_QIANLIB:
        case PG_SKINS:
        case PG_TITLES:
        case PG_INCENSE: return PG_COLLECT;
        case PG_QIANVIEW: return PG_QIANLIB;
        case PG_LIANSHENG: return PG_GAMES;
        default: return PG_SPLASH;
    }
}

// ---------------------------------------------------------------------------
// 彩蛋
// ---------------------------------------------------------------------------
static void collect_events_eggs(uint8_t eggs_new) {
    s_pending_eggs |= (uint8_t)(eggs_new & ~s_model->eggs_seen);
    s_model->eggs_seen |= (uint8_t)(eggs_new & ~s_model->eggs_seen);
}

static void show_next_egg_or(sb_page_t after) {
    if (s_pending_eggs) {
        s_after_eggs = after;
        goto_page(PG_EGG);
    } else if (s_page != after) {
        goto_page(after);
    }
}

// ---------------------------------------------------------------------------
// 模型操作入口（掷杯/求签/游戏）
// ---------------------------------------------------------------------------
static void do_ritual_throw(void) {
    sb_events_t e;
    sb_events_clear(&e);
    s_last_throw = sb_roll_throw(&s_rand);
    sb_model_ritual_throw(s_model, &e, s_last_throw);
    collect_events_eggs(e.eggs_new);
    shengbei_store_request_save(s_model);
    s_result_final = (s_model->today_throws >= 3);
    switch (s_last_throw) {
        case SB_THROW_SHENG: sfx_play(SFX_CHIME_SHENG); break;
        case SB_THROW_XIAO: sfx_play(SFX_PING_XIAO); break;
        default: sfx_play(SFX_THUD_YIN); break;
    }
    goto_page(PG_RESULT);
}

static void do_draw(void) {
    sb_events_t e;
    sb_events_clear(&e);
    if (!sb_model_draw_qian(s_model, &e, &s_rand)) return;
    collect_events_eggs(e.eggs_new);
    shengbei_store_request_save(s_model);
    if (e.qian_gold) sfx_play(SFX_GOLD);
    else sfx_play(SFX_WIN);
    show_next_egg_or(PG_QIAN);
}

static void do_game_throw(void) {
    sb_events_t e;
    sb_events_clear(&e);
    sb_throw_t r = sb_roll_throw(&s_rand);
    bool cont = sb_model_game_throw(s_model, &e, r);
    collect_events_eggs(e.eggs_new);
    if (cont) {
        s_lian_run++;
        switch (r) {
            case SB_THROW_SHENG: sfx_play(SFX_CHIME_SHENG); break;
            case SB_THROW_XIAO: sfx_play(SFX_PING_XIAO); break;
            default: sfx_play(SFX_THUD_YIN); break;
        }
        // 圣杯后重摆待掷
        if (s_lian_cups) {
            lv_obj_delete(s_lian_cups);
            s_lian_cups = build_cups(s_page_obj, 120, 180, s_model->skin_selected, r, false);
            s_shake_obj = NULL;
        }
        if (s_lian_run_label) {
            char buf[24];
            snprintf(buf, sizeof(buf), "连击 %u", (unsigned)s_lian_run);
            lv_label_set_text(s_lian_run_label, buf);
        }
    } else {
        sfx_play(SFX_LOSE);
        bool record = sb_model_game_finish(s_model, &e, s_lian_run);
        shengbei_store_request_save(s_model);
        if (record) sfx_play(SFX_GOLD);
        if (s_lian_best_label) {
            char buf[96];
            snprintf(buf, sizeof(buf), "本局 %u 连 · 最佳 %u%s",
                     (unsigned)s_lian_run, (unsigned)s_model->best_lian_sheng,
                     record ? " · 新纪录！香火+10" : "");
            lv_label_set_text(s_lian_best_label, buf);
        }
        if (s_lian_hint) lv_label_set_text(s_lian_hint, "OK 继续挑战 · 长按返回");
        s_lian_run = 0;
    }
}

// ---------------------------------------------------------------------------
// 今日页
// ---------------------------------------------------------------------------
static const char *home_state_text(void) {
    switch ((sb_today_state_t)s_model->today_state) {
        case SB_TODAY_IDLE:
            return s_model->today_throws ? "继续摇杯" : "今日三掷未启";
        case SB_TODAY_QUALIFIED: return "圣杯允签";
        case SB_TODAY_DRAWN: return "仪式圆满";
        case SB_TODAY_FAILED: return "今日无缘";
        case SB_TODAY_PITAILAI: return "否极泰来";
        default: return "";
    }
}

static void home_hint_refresh(void) {
    if (!s_home_hint_label) return;
    const char *hint;
    switch ((sb_today_state_t)s_model->today_state) {
        case SB_TODAY_IDLE:
            hint = s_model->today_throws
                       ? "长按OK摇杯\nUP/DOWN 切换"
                       : "长按OK摇杯问神\nUP/DOWN 切换";
            break;
        case SB_TODAY_QUALIFIED: hint = "长按OK摇签筒求签"; break;
        case SB_TODAY_DRAWN: hint = "OK 回看今日签\nUP/DOWN 切换"; break;
        case SB_TODAY_FAILED: hint = "明日再来\nUP/DOWN 切换"; break;
        case SB_TODAY_PITAILAI: hint = "长按OK 摇筒取天赐签"; break;
        default: hint = ""; break;
    }
    lv_label_set_text(s_home_hint_label, hint);
}

static lv_obj_t *home_visual_refresh(void) {
    if (s_home_cups) {
        lv_obj_delete(s_home_cups);
        s_home_cups = NULL;
    }
    if (s_model->today_state == SB_TODAY_QUALIFIED || s_model->today_state == SB_TODAY_PITAILAI) {
        s_home_cups = build_tube(s_page_obj, 120, 190);
    } else {
        s_home_cups = build_cups(s_page_obj, 120, 190, s_model->skin_selected,
                                 SB_THROW_SHENG, true);
    }
    return s_home_cups;
}

static void build_home(void) {
    s_page_obj = lv_obj_create(s_scr);
    lv_obj_set_size(s_page_obj, 240, 320);
    lv_obj_set_style_bg_color(s_page_obj, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(s_page_obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_page_obj, 0, 0);
    lv_obj_clear_flag(s_page_obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(s_page_obj, 0, 0);

    s_home_streak_label = make_label(s_page_obj, NULL, &app_font_16, C_DIM,
                                     LV_ALIGN_TOP_LEFT, 8, 4);
    s_home_incense_label = make_label(s_page_obj, NULL, &app_font_16, C_GOLD,
                                      LV_ALIGN_TOP_LEFT, 8, 26);

    lv_obj_t *card = make_panel(s_page_obj, 20, 62, 200, 60, C_PANEL);
    s_home_state_label = make_label(card, NULL, &app_font_26, C_GOLD, LV_ALIGN_CENTER, 0, 0);

    s_home_cups = home_visual_refresh();

    s_home_hint_label = make_label(s_page_obj, NULL, &app_font_16, C_DIM,
                                   LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_text_align(s_home_hint_label, LV_TEXT_ALIGN_CENTER, 0);

    // 签到横幅（开机签到事件）
    char buf[48];
    snprintf(buf, sizeof(buf), "连 %u 香火日", (unsigned)s_model->streak);
    lv_label_set_text(s_home_streak_label, buf);
    snprintf(buf, sizeof(buf), "香火 %u", (unsigned)s_model->incense);
    lv_label_set_text(s_home_incense_label, buf);
    lv_label_set_text(s_home_state_label, home_state_text());
    home_hint_refresh();
}

// ---------------------------------------------------------------------------
// 掷果浮层
// ---------------------------------------------------------------------------
static void build_result(void) {
    s_page_obj = lv_obj_create(s_scr);
    lv_obj_set_size(s_page_obj, 240, 320);
    lv_obj_set_style_bg_color(s_page_obj, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(s_page_obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_page_obj, 0, 0);
    lv_obj_clear_flag(s_page_obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(s_page_obj, 0, 0);

    s_result_cups = build_cups(s_page_obj, 120, 100, s_model->skin_selected, s_last_throw, false);

    s_result_word = make_label(s_page_obj, NULL, &app_font_26, C_GOLD, LV_ALIGN_CENTER, 0, 42);
    s_result_info = make_label(s_page_obj, NULL, &app_font_16, C_DIM, LV_ALIGN_CENTER, 0, 82);

    const char *word;
    uint32_t color = C_GOLD;
    switch (s_last_throw) {
        case SB_THROW_SHENG: word = "圣杯"; color = C_GOLD; break;
        case SB_THROW_XIAO: word = "笑杯"; color = C_MID; break;
        default: word = "阴杯"; color = C_BAD; break;
    }
    lv_label_set_text(s_result_word, word);
    lv_obj_set_style_text_color(s_result_word, lv_color_hex(color), 0);

    char buf[64];
    if (s_model->today_state == SB_TODAY_QUALIFIED) {
        snprintf(buf, sizeof(buf), "神明允准 · 长按OK摇筒求签");
    } else if (s_model->today_state == SB_TODAY_PITAILAI) {
        snprintf(buf, sizeof(buf), "第 %u 掷 · 三阴既满，天赐一签", (unsigned)s_model->today_throws);
    } else if (s_model->today_state == SB_TODAY_FAILED) {
        snprintf(buf, sizeof(buf), "第 %u 掷 · 今日无缘，明日再会",
                 (unsigned)s_model->today_throws);
    } else {
        snprintf(buf, sizeof(buf), "第 %u 掷 · 尚余 %u 掷", (unsigned)s_model->today_throws,
                 (unsigned)(3 - s_model->today_throws));
    }
    lv_label_set_text(s_result_info, buf);
}

// ---------------------------------------------------------------------------
// 签文页 / 签条目
// ---------------------------------------------------------------------------
static void build_qian_content(lv_obj_t *parent, int id, bool gold, bool collected_view) {
    const lvzu_qian_t *q = &QIAN[id - 1];
    static const uint32_t LEVEL_COLORS[6] = { C_GOLD, C_GOOD, C_GOOD, C_MID, C_MID, C_BAD };

    char head[64];
    if (gold) {
        snprintf(head, sizeof(head), "第 %d 签 · %s · %s ★金签", id, q->title,
                 QIAN_LEVEL_NAMES[q->level]);
    } else {
        snprintf(head, sizeof(head), "第 %d 签 · %s · %s", id, q->title,
                 QIAN_LEVEL_NAMES[q->level]);
    }
    lv_obj_t *head_label = make_label(parent, head, &app_font_16, LEVEL_COLORS[q->level],
                                      LV_ALIGN_TOP_MID, 0, 4);
    // 限制宽度居中换行：长签题两行时不再压到下方诗句
    lv_obj_set_width(head_label, 220);
    lv_label_set_long_mode(head_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(head_label, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *poem = make_label(parent, q->poem, &app_font_16, C_INK, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_width(poem, 220);
    lv_label_set_long_mode(poem, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(poem, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(poem, LV_ALIGN_TOP_MID, 0, 48);
    (void)head_label;

    lv_obj_t *jie = make_label(parent, NULL, &app_font_16, C_DIM, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_width(jie, 208);
    lv_label_set_long_mode(jie, LV_LABEL_LONG_WRAP);
    char jiebuf[128];
    snprintf(jiebuf, sizeof(jiebuf), "解曰：%s", q->jie);
    lv_label_set_text(jie, jiebuf);
    // 下移两行：给 4 行诗句留出空间（48 + 4×19 = 122），避免解曰与诗句重叠
    lv_obj_align(jie, LV_ALIGN_TOP_MID, 0, 150);

    if (collected_view && !gold) {
        uint8_t n = s_model->qian_count[id - 1];
        if (n > 0) {
            char got[32];
            snprintf(got, sizeof(got), "已藏 · 阅签 %u 次", (unsigned)n);
            make_label(parent, got, &app_font_16, C_DIM, LV_ALIGN_BOTTOM_MID, 0, -4);
        }
    }
}

static void build_qian(void) {
    s_page_obj = lv_obj_create(s_scr);
    lv_obj_set_size(s_page_obj, 240, 320);
    lv_obj_set_style_bg_color(s_page_obj, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(s_page_obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_page_obj, 0, 0);
    lv_obj_clear_flag(s_page_obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(s_page_obj, 0, 0);

    lv_obj_t *content = lv_obj_create(s_page_obj);
    lv_obj_set_size(content, 224, 250);
    lv_obj_align(content, LV_ALIGN_TOP_MID, 0, 6);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    build_qian_content(content, (int)s_model->today_qian, s_model->today_gold, false);

    make_label(s_page_obj, "OK 收下 · 长按返回", &app_font_16, C_DIM, LV_ALIGN_BOTTOM_MID, 0, -8);
}

static void build_qianview(void) {
    s_page_obj = lv_obj_create(s_scr);
    lv_obj_set_size(s_page_obj, 240, 320);
    lv_obj_set_style_bg_color(s_page_obj, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(s_page_obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_page_obj, 0, 0);
    lv_obj_clear_flag(s_page_obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(s_page_obj, 0, 0);

    int id = s_lib_sel + 1;
    bool gold = s_model->qian_count[s_lib_sel] >= 2;
    lv_obj_t *content = lv_obj_create(s_page_obj);
    lv_obj_set_size(content, 224, 250);
    lv_obj_align(content, LV_ALIGN_TOP_MID, 0, 6);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    if (s_model->qian_count[s_lib_sel] == 0) {
        make_label(content, "？", &app_font_26, C_TRACK, LV_ALIGN_CENTER, 0, -20);
        make_label(content, "尚未求得此签", &app_font_16, C_DIM, LV_ALIGN_CENTER, 0, 20);
        return;
    }
    build_qian_content(content, id, gold, true);
    make_label(s_page_obj, "长按返回", &app_font_16, C_DIM, LV_ALIGN_BOTTOM_MID, 0, -8);
}

// ---------------------------------------------------------------------------
// 签文库（虚拟列表：只创建可视窗口内的行，避免 100 行全量创建耗尽 LVGL 堆）
// ---------------------------------------------------------------------------
#define LIB_WIN_ROWS 7
static lv_obj_t *s_lib_rows[LIB_WIN_ROWS];
static int s_lib_win_start;  // 窗口首行对应的签号（0 基）

static void lib_row_refresh(int win_i) {
    int idx = s_lib_win_start + win_i;
    lv_obj_t *row = s_lib_rows[win_i];
    bool got = s_model->qian_count[idx] > 0;
    bool gold = s_model->qian_count[idx] >= 2;
    lv_obj_t *label = lv_obj_get_child(row, 0);
    char buf[48];
    if (got) {
        snprintf(buf, sizeof(buf), "第%d签 %s %s%s", idx + 1, QIAN[idx].title,
                 QIAN_LEVEL_NAMES[QIAN[idx].level], gold ? " ★" : "");
    } else {
        snprintf(buf, sizeof(buf), "第%d签 ？？？", idx + 1);
    }
    lv_label_set_text(label, buf);
    lv_obj_set_style_text_color(label,
                                lv_color_hex(!got ? C_TRACK : gold ? C_GOLD : C_INK), 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(idx == s_lib_sel ? C_PANEL_HI : C_PANEL), 0);
}

static void lib_sel_refresh(void) {
    // 循环包装：UP 从第1签跳到第100签，DOWN 从第100签跳到第1签
    s_lib_sel = (s_lib_sel % SB_QIAN_MAX + SB_QIAN_MAX) % SB_QIAN_MAX;
    // 选中行超出窗口边缘时滚动窗口跟随
    if (s_lib_sel < s_lib_win_start) s_lib_win_start = s_lib_sel;
    if (s_lib_sel >= s_lib_win_start + LIB_WIN_ROWS) {
        s_lib_win_start = s_lib_sel - LIB_WIN_ROWS + 1;
    }
    if (s_lib_win_start > SB_QIAN_MAX - LIB_WIN_ROWS) {
        s_lib_win_start = SB_QIAN_MAX - LIB_WIN_ROWS;
    }
    if (s_lib_win_start < 0) s_lib_win_start = 0;
    for (int i = 0; i < LIB_WIN_ROWS; i++) lib_row_refresh(i);
}

static void build_qianlib(void) {
    s_page_obj = lv_obj_create(s_scr);
    lv_obj_set_size(s_page_obj, 240, 320);
    lv_obj_set_style_bg_color(s_page_obj, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(s_page_obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_page_obj, 0, 0);
    lv_obj_clear_flag(s_page_obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(s_page_obj, 0, 0);

    char head[40];
    snprintf(head, sizeof(head), "签文库 %u/%d", (unsigned)sb_model_collected(s_model),
             SB_QIAN_MAX);
    make_label(s_page_obj, head, &app_font_16, C_GOLD, LV_ALIGN_TOP_LEFT, 8, 4);
    make_label(s_page_obj, "OK 查看 · 长按返回", &app_font_16, C_DIM, LV_ALIGN_BOTTOM_MID, 0, -8);

    for (int i = 0; i < LIB_WIN_ROWS; i++) {
        lv_obj_t *row = make_panel(s_page_obj, 8, 34 + i * 30, 224, 28, C_PANEL);
        make_label(row, NULL, &app_font_16, C_INK, LV_ALIGN_LEFT_MID, 6, 0);
        s_lib_rows[i] = row;
    }
    s_lib_win_start = 0;
    lib_sel_refresh();
}

// ---------------------------------------------------------------------------
// 杯妆 / 称号 / 香火
// ---------------------------------------------------------------------------
static lv_obj_t *s_skin_rows[SB_SKIN_COUNT];
static lv_obj_t *s_title_rows[SB_TITLE_COUNT];

static void build_skins(void) {
    s_page_obj = lv_obj_create(s_scr);
    lv_obj_set_size(s_page_obj, 240, 320);
    lv_obj_set_style_bg_color(s_page_obj, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(s_page_obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_page_obj, 0, 0);
    lv_obj_clear_flag(s_page_obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(s_page_obj, 0, 0);

    make_label(s_page_obj, "杯妆", &app_font_26, C_GOLD, LV_ALIGN_TOP_LEFT, 8, 4);
    make_label(s_page_obj, "OK 选用 · 长按返回", &app_font_16, C_DIM, LV_ALIGN_TOP_RIGHT, -8, 12);

    for (int i = 0; i < SB_SKIN_COUNT; i++) {
        bool unlocked = sb_model_skin_unlocked(s_model, (sb_skin_t)i);
        bool selected = s_model->skin_selected == i;
        lv_obj_t *row = make_panel(s_page_obj, 10, 48 + i * 42, 220, 38,
                                   i == s_skin_sel ? C_PANEL_HI : C_PANEL);
        s_skin_rows[i] = row;

        // 色样：两片小色块
        lv_obj_t *chip_a = lv_obj_create(row);
        lv_obj_set_size(chip_a, 20, 20);
        lv_obj_align(chip_a, LV_ALIGN_LEFT_MID, 8, 0);
        lv_obj_set_style_bg_color(chip_a, lv_color_hex(SKIN_COLORS[i][0]), 0);
        lv_obj_set_style_bg_opa(chip_a, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(chip_a, 10, 0);
        lv_obj_set_style_border_width(chip_a, 0, 0);

        lv_obj_t *chip_b = lv_obj_create(row);
        lv_obj_set_size(chip_b, 20, 20);
        lv_obj_align(chip_b, LV_ALIGN_LEFT_MID, 30, 0);
        lv_obj_set_style_bg_color(chip_b, lv_color_hex(SKIN_COLORS[i][1]), 0);
        lv_obj_set_style_bg_opa(chip_b, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(chip_b, 10, 0);
        lv_obj_set_style_border_width(chip_b, 0, 0);

        char buf[64];
        if (unlocked) {
            snprintf(buf, sizeof(buf), "%s%s", SKIN_NAMES[i], selected ? " ▶" : "");
        } else {
            snprintf(buf, sizeof(buf), "%s · 未解锁（%s）", SKIN_NAMES[i], SKIN_REQ[i]);
        }
        lv_obj_t *name = make_label(row, buf, &app_font_16,
                                    unlocked ? C_INK : C_TRACK, LV_ALIGN_LEFT_MID, 58, 0);
        (void)name;
    }
}

static void skin_sel_refresh(void) {
    for (int i = 0; i < SB_SKIN_COUNT; i++) {
        lv_obj_set_style_bg_color(s_skin_rows[i],
                                  lv_color_hex(i == s_skin_sel ? C_PANEL_HI : C_PANEL), 0);
    }
}

static void build_titles(void) {
    s_page_obj = lv_obj_create(s_scr);
    lv_obj_set_size(s_page_obj, 240, 320);
    lv_obj_set_style_bg_color(s_page_obj, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(s_page_obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_page_obj, 0, 0);
    lv_obj_clear_flag(s_page_obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(s_page_obj, 0, 0);

    make_label(s_page_obj, "称号", &app_font_26, C_GOLD, LV_ALIGN_TOP_LEFT, 8, 4);
    make_label(s_page_obj, "长按返回", &app_font_16, C_DIM, LV_ALIGN_TOP_RIGHT, -8, 12);

    for (int i = 0; i < SB_TITLE_COUNT; i++) {
        bool has = sb_model_title_has(s_model, (sb_title_t)i);
        lv_obj_t *row = make_panel(s_page_obj, 10, 50 + i * 32, 220, 28, C_PANEL);
        s_title_rows[i] = row;
        char buf[72];
        snprintf(buf, sizeof(buf), "%s · %s", TITLE_NAMES[i], TITLE_REQ[i]);
        lv_obj_t *label = make_label(row, buf, &app_font_16, has ? C_GOLD : C_TRACK,
                                     LV_ALIGN_LEFT_MID, 8, 0);
        (void)label;
    }
}

static void build_incense(void) {
    s_page_obj = lv_obj_create(s_scr);
    lv_obj_set_size(s_page_obj, 240, 320);
    lv_obj_set_style_bg_color(s_page_obj, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(s_page_obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_page_obj, 0, 0);
    lv_obj_clear_flag(s_page_obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(s_page_obj, 0, 0);

    make_label(s_page_obj, "香火与补签", &app_font_26, C_GOLD, LV_ALIGN_TOP_LEFT, 8, 4);
    s_incense_value_label = make_label(s_page_obj, NULL, &app_font_16, C_INK,
                                       LV_ALIGN_TOP_LEFT, 8, 40);
    char buf[96];
    snprintf(buf, sizeof(buf), "香火 %u · 连续 %u 香火日",
             (unsigned)s_model->incense, (unsigned)s_model->streak);
    lv_label_set_text(s_incense_value_label, buf);
    if (s_model->pending_patch > 0) {
        snprintf(buf, sizeof(buf), "曾连续 %u 日，愿续此香缘（补签 %d 香火）",
                 (unsigned)s_model->pending_patch, SB_PATCH_COST);
        make_label(s_page_obj, buf, &app_font_16, C_MID, LV_ALIGN_TOP_LEFT, 8, 62);
    } else {
        make_label(s_page_obj, "告假将中断连续，可用香火补签",
                   &app_font_16, C_DIM, LV_ALIGN_TOP_LEFT, 8, 62);
    }

    static const char *ACTIONS[2] = { "今日告假", "补签续缘" };
    for (int i = 0; i < 2; i++) {
        bool avail = (i == 0) ? (!s_model->leave_used && s_model->today_state == SB_TODAY_IDLE &&
                                 s_model->streak > 0)
                              : (s_model->pending_patch > 0 &&
                                 s_model->incense >= SB_PATCH_COST);
        lv_obj_t *row = make_panel(s_page_obj, 10, 110 + i * 44, 220, 38,
                                   i == s_incense_sel ? C_PANEL_HI : C_PANEL);
        char buf2[48];
        snprintf(buf2, sizeof(buf2), "%s%s", ACTIONS[i], avail ? "" : "（不可用）");
        make_label(row, buf2, &app_font_16, avail ? C_INK : C_TRACK, LV_ALIGN_LEFT_MID, 8, 0);
    }
    make_label(s_page_obj, "OK 执行 · 长按返回", &app_font_16, C_DIM, LV_ALIGN_BOTTOM_MID, 0, -8);
}

static void incense_refresh(void) {
    // 重建本页最简单
    goto_page(PG_INCENSE);
}

// ---------------------------------------------------------------------------
// 收藏 / 游戏根
// ---------------------------------------------------------------------------
static void build_menu_page(const char *title, const char *const *items, int count, int sel) {
    s_page_obj = lv_obj_create(s_scr);
    lv_obj_set_size(s_page_obj, 240, 320);
    lv_obj_set_style_bg_color(s_page_obj, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(s_page_obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_page_obj, 0, 0);
    lv_obj_clear_flag(s_page_obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(s_page_obj, 0, 0);

    make_label(s_page_obj, title, &app_font_26, C_GOLD, LV_ALIGN_TOP_LEFT, 8, 4);
    make_label(s_page_obj, "OK 进入 · 长按返回", &app_font_16, C_DIM, LV_ALIGN_BOTTOM_MID, 0, -8);

    for (int i = 0; i < count; i++) {
        lv_obj_t *row = make_panel(s_page_obj, 10, 60 + i * 46, 220, 40,
                                   i == sel ? C_PANEL_HI : C_PANEL);
        make_label(row, items[i], &app_font_16, C_INK, LV_ALIGN_LEFT_MID, 10, 0);
    }
}

// ---------------------------------------------------------------------------
// 连圣杯
// ---------------------------------------------------------------------------
static void build_liansheng(void) {
    s_page_obj = lv_obj_create(s_scr);
    lv_obj_set_size(s_page_obj, 240, 320);
    lv_obj_set_style_bg_color(s_page_obj, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(s_page_obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_page_obj, 0, 0);
    lv_obj_clear_flag(s_page_obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(s_page_obj, 0, 0);

    make_label(s_page_obj, "连圣杯", &app_font_26, C_GOLD, LV_ALIGN_TOP_LEFT, 8, 4);

    s_lian_run_label = make_label(s_page_obj, "连击 0", &app_font_26, C_INK,
                                  LV_ALIGN_TOP_MID, 0, 44);
    s_lian_best_label = make_label(s_page_obj, NULL, &app_font_16, C_GOLD,
                                   LV_ALIGN_TOP_MID, 0, 84);
    char buf[40];
    snprintf(buf, sizeof(buf), "本地最佳 %u 连", (unsigned)s_model->best_lian_sheng);
    lv_label_set_text(s_lian_best_label, buf);

    s_lian_cups = build_cups(s_page_obj, 120, 180, s_model->skin_selected, SB_THROW_SHENG, true);
    s_lian_hint = make_label(s_page_obj, "OK 掷杯 · 长按返回退出", &app_font_16, C_DIM,
                             LV_ALIGN_BOTTOM_MID, 0, -10);
}

// ---------------------------------------------------------------------------
// 彩蛋浮层
// ---------------------------------------------------------------------------
static void build_egg(void) {
    s_page_obj = lv_obj_create(s_scr);
    lv_obj_set_size(s_page_obj, 240, 320);
    lv_obj_set_style_bg_color(s_page_obj, lv_color_hex(0x1A0605), 0);
    lv_obj_set_style_bg_opa(s_page_obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_page_obj, 0, 0);
    lv_obj_clear_flag(s_page_obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(s_page_obj, 0, 0);

    // 取最低位彩蛋
    int egg = 0;
    for (int i = 0; i < SB_EGG_COUNT; i++) {
        if (s_pending_eggs & (1u << i)) { egg = i; break; }
    }
    s_pending_eggs &= (uint8_t)~(1u << egg);

    lv_obj_t *glow = make_panel(s_page_obj, 30, 90, 180, 120, C_PANEL_HI);
    lv_obj_set_style_radius(glow, 16, 0);
    make_label(s_page_obj, "彩蛋", &app_font_16, C_MID, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_t *title = make_label(s_page_obj, EGG_TITLE[egg], &app_font_26, C_GOLD,
                                 LV_ALIGN_CENTER, 0, -30);
    lv_obj_t *text = make_label(s_page_obj, EGG_TEXT[egg], &app_font_16, C_INK,
                                LV_ALIGN_CENTER, 0, 10);
    lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(text, 170);
    lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
    (void)glow;
    make_label(s_page_obj, "OK 收下", &app_font_16, C_DIM, LV_ALIGN_BOTTOM_MID, 0, -20);
    sfx_play(SFX_GOLD);
}

// ---------------------------------------------------------------------------
// 启动页
// ---------------------------------------------------------------------------
static void splash_blink_cb(lv_timer_t *t) {
    lv_obj_t *hint = (lv_obj_t *)lv_timer_get_user_data(t);
    lv_obj_t *label = lv_obj_get_child(hint, 0);
    if (lv_obj_has_flag(label, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_clear_flag(label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
    }
}

static void build_splash(void) {
    s_page_obj = lv_obj_create(s_scr);
    lv_obj_set_size(s_page_obj, 240, 320);
    lv_obj_set_style_bg_color(s_page_obj, lv_color_hex(0x4A0E08), 0);
    lv_obj_set_style_bg_opa(s_page_obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_page_obj, 0, 0);
    lv_obj_clear_flag(s_page_obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(s_page_obj, 0, 0);

    make_panel(s_page_obj, 40, 70, 160, 4, C_GOLD);
    make_label(s_page_obj, "圣杯问道", &app_font_26, C_GOLD, LV_ALIGN_CENTER, 0, -50);
    make_label(s_page_obj, "掷杯问神 · 吕祖赐签", &app_font_16, C_INK, LV_ALIGN_CENTER, 0, 0);
    make_panel(s_page_obj, 40, 220, 160, 4, C_GOLD);

    lv_obj_t *hint_holder = make_panel(s_page_obj, 0, 0, 240, 30, 0x4A0E08);
    lv_obj_align(hint_holder, LV_ALIGN_BOTTOM_MID, 0, -40);
    lv_obj_set_style_bg_opa(hint_holder, LV_OPA_TRANSP, 0);
    make_label(hint_holder, "按 OK 启香", &app_font_16, C_DIM, LV_ALIGN_CENTER, 0, 0);

    s_page_timer = lv_timer_create(splash_blink_cb, 600, hint_holder);
}

// ---------------------------------------------------------------------------
// 页面切换
// ---------------------------------------------------------------------------
static void goto_page(sb_page_t p) {
    leave_current();
    s_page = p;
    switch (p) {
        case PG_SPLASH: build_splash(); break;
        case PG_HOME: build_home(); break;
        case PG_RESULT: build_result(); break;
        case PG_QIAN: build_qian(); break;
        case PG_COLLECT: {
            static const char *ITEMS[4] = { "签文库", "杯妆", "称号", "香火·补签" };
            build_menu_page("收藏", ITEMS, 4, s_collect_sel);
            break;
        }
        case PG_GAMES: {
            static const char *ITEMS[1] = { "连圣杯" };
            build_menu_page("小游戏", ITEMS, 1, s_games_sel);
            break;
        }
        case PG_QIANLIB: build_qianlib(); break;
        case PG_QIANVIEW: build_qianview(); break;
        case PG_SKINS: build_skins(); break;
        case PG_TITLES: build_titles(); break;
        case PG_INCENSE: build_incense(); break;
        case PG_LIANSHENG: build_liansheng(); break;
        case PG_EGG: build_egg(); break;
    }
    if (s_page_obj) {
        lv_obj_set_x(s_page_obj, SB_PAGE_X_OFFSET);  // 整体画面左移
        lv_obj_tree_walk(s_page_obj, font_audit_walk, NULL);
    }
}

// ---------------------------------------------------------------------------
// 输入处理
// ---------------------------------------------------------------------------
static bool home_shake_allowed(void) {
    return s_model->today_state == SB_TODAY_IDLE ||
           s_model->today_state == SB_TODAY_QUALIFIED ||
           s_model->today_state == SB_TODAY_PITAILAI;
}

void ui_sb_input(bsp_btn_t btn, bsp_btn_ev_t ev) {
    if (!bsp_lvgl_lock(500)) return;

    if (ev == BSP_BTN_RELEASE) {
        if (s_shaking) {
            shake_stop();
            if (btn == BSP_BTN_OK && s_page == PG_HOME) {
                // 主页摇杯松手：按当日状态掷杯或求签。
                if (s_model->today_state == SB_TODAY_QUALIFIED ||
                    s_model->today_state == SB_TODAY_PITAILAI) {
                    do_draw();
                } else if (home_shake_allowed()) {
                    do_ritual_throw();
                }
            }
        }
        bsp_lvgl_unlock();
        return;
    }

    if (btn == BSP_BTN_OK && ev == BSP_BTN_LONG) {
        if (s_shaking) {  // 摇动中的 LONG 是按住的自然结果，忽略并记录
            s_shake_saw_long = true;
            bsp_lvgl_unlock();
            return;
        }
        if (s_page != PG_SPLASH && s_page != PG_HOME) {
            goto_page(back_target(s_page));
        }
        bsp_lvgl_unlock();
        return;
    }

    if (btn == BSP_BTN_OK && ev == BSP_BTN_CLICK) {
        if (s_shake_click_swallow) {
            s_shake_click_swallow = false;
            bsp_lvgl_unlock();
            return;
        }
        switch (s_page) {
            case PG_SPLASH:
                goto_page(PG_HOME);
                show_next_egg_or(PG_HOME);
                break;
            case PG_HOME:
                // 仪式圆满后 OK 回看今日签（对应主页提示"OK 回看今日签"）
                if ((sb_today_state_t)s_model->today_state == SB_TODAY_DRAWN &&
                    s_model->today_qian > 0) {
                    goto_page(PG_QIAN);
                }
                break;
            case PG_RESULT:
                if (s_model->today_state == SB_TODAY_QUALIFIED && !s_result_final) {
                    goto_page(PG_HOME);
                } else {
                    show_next_egg_or(PG_HOME);
                }
                break;
            case PG_QIAN:
            case PG_QIANVIEW:
                if (s_pending_eggs) show_next_egg_or(PG_HOME);
                else goto_page(back_target(s_page));
                break;
            case PG_EGG:
                if (s_pending_eggs) goto_page(PG_EGG);
                else if (s_page != s_after_eggs) goto_page(s_after_eggs);
                break;
            case PG_COLLECT:
                if (s_collect_sel == 0) goto_page(PG_QIANLIB);
                else if (s_collect_sel == 1) goto_page(PG_SKINS);
                else if (s_collect_sel == 2) goto_page(PG_TITLES);
                else goto_page(PG_INCENSE);
                break;
            case PG_QIANLIB:
                if (s_model->qian_count[s_lib_sel] > 0) goto_page(PG_QIANVIEW);
                break;
            case PG_SKINS:
                if (sb_model_skin_unlocked(s_model, (sb_skin_t)s_skin_sel)) {
                    s_model->skin_selected = (uint8_t)s_skin_sel;
                    shengbei_store_request_save(s_model);
                    sfx_play(SFX_PING_XIAO);
                    goto_page(PG_SKINS);  // 刷新 ▸
                }
                break;
            case PG_INCENSE: {
                sb_events_t e;
                sb_events_clear(&e);
                bool done = false;
                if (s_incense_sel == 0) {
                    done = sb_model_leave(s_model, &e);
                } else {
                    done = sb_model_patch(s_model, &e);
                    if (done) collect_events_eggs(e.eggs_new);
                }
                if (done) {
                    shengbei_store_request_save(s_model);
                    sfx_play(SFX_WIN);
                } else {
                    sfx_play(SFX_THUD_YIN);
                }
                incense_refresh();
                break;
            }
            case PG_GAMES:
                goto_page(PG_LIANSHENG);
                break;
            case PG_LIANSHENG:
                do_game_throw();
                break;
            default:
                break;
        }
        bsp_lvgl_unlock();
        return;
    }

    if (btn == BSP_BTN_OK && ev == BSP_BTN_PRESS) {
        // 摇杯手势只在主页仪式用；游戏页短按掷、长按返回，不占用长按。
        if (s_page == PG_HOME && home_shake_allowed()) {
            s_shake_saw_long = false;
            s_shaking = true;
            shake_start(s_home_cups);
        }
        bsp_lvgl_unlock();
        return;
    }

    if (ev == BSP_BTN_CLICK && (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN)) {
        if (s_shaking) {  // 摇动中不做导航，避免页面切换打断摇杯
            bsp_lvgl_unlock();
            return;
        }
        int dir = (btn == BSP_BTN_UP) ? -1 : 1;
        switch (s_page) {
            case PG_SPLASH: break;
            case PG_HOME:
                // UP → 收藏，DOWN → 游戏（循环：收藏↔今日↔游戏）
                if (dir < 0) {
                    s_collect_sel = 0;
                    goto_page(PG_COLLECT);
                } else {
                    s_games_sel = 0;
                    goto_page(PG_GAMES);
                }
                break;
            case PG_COLLECT:
                s_collect_sel = (s_collect_sel + dir + 4) % 4;
                goto_page(PG_COLLECT);
                break;
            case PG_QIANLIB:
                s_lib_sel += dir;
                lib_sel_refresh();
                break;
            case PG_SKINS:
                s_skin_sel = (s_skin_sel + dir + SB_SKIN_COUNT) % SB_SKIN_COUNT;
                skin_sel_refresh();
                break;
            case PG_INCENSE:
                s_incense_sel = (s_incense_sel + dir + 2) % 2;
                goto_page(PG_INCENSE);
                break;
            default:
                break;
        }
    }

    bsp_lvgl_unlock();
}

// ---------------------------------------------------------------------------
// 电量
// ---------------------------------------------------------------------------
static void batt_timer_cb(lv_timer_t *t) {
    (void)t;
    if (!s_batt_label) return;
    int soc = bsp_battery_soc();
    if (soc < 0) {
        lv_obj_add_flag(s_batt_label, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    char buf[12];
    snprintf(buf, sizeof(buf), "%d%%", soc);
    lv_label_set_text(s_batt_label, buf);
    lv_obj_clear_flag(s_batt_label, LV_OBJ_FLAG_HIDDEN);
}

// ---------------------------------------------------------------------------
// 初始化
// ---------------------------------------------------------------------------
void ui_sb_init(sb_model_t *model, const sb_events_t *open_events, uint32_t rand_seed) {
    s_model = model;
    s_rand = rand_seed | 1u;
    s_pending_eggs = 0;
    s_lib_sel = 0;
    s_collect_sel = 0;
    s_games_sel = 0;
    s_skin_sel = model->skin_selected;
    s_incense_sel = 0;
    s_lian_run = 0;

    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_screen_load(s_scr);

    // 电量角标（所有页面之上）
    s_batt_label = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_batt_label, &app_font_16, 0);
    lv_obj_set_style_text_color(s_batt_label, lv_color_hex(C_DIM), 0);
    lv_obj_align(s_batt_label, LV_ALIGN_TOP_RIGHT, -6, 4);
    lv_label_set_text(s_batt_label, "");
    s_batt_timer = lv_timer_create(batt_timer_cb, 30000, NULL);
    batt_timer_cb(NULL);

    if (open_events) collect_events_eggs(open_events->eggs_new);

    goto_page(PG_SPLASH);
}

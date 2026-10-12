// main/shengbei_model.h —— 掷圣杯·吕祖求签 纯逻辑模型。
// 与 ESP-IDF/LVGL 完全解耦，可被宿主测试直接编译。
// 本机为离线设备（无 RTC、无网络授时），"每日"采用会话制：
// 每次开机视为一个"香火日"，提供一次完整三掷仪式；告假制造断签，香火可补签。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "qian_data.h"

#define SB_QIAN_MAX QIAN_COUNT
#define SB_SKIN_COUNT 6
#define SB_TITLE_COUNT 8
#define SB_EGG_COUNT 8

#define SB_INCENSE_DAILY 2       // 每香火日打开所得香火
#define SB_PATCH_COST 5          // 补签一次（恢复断签）消耗香火
#define SB_GAME_RECORD_BONUS 10  // 打破连圣杯记录奖励香火
#define SB_PAIRS_WIN_BONUS 5     // 翻牌配对胜利奖励香火

// 连续香火日解锁杯皮肤的门槛（下标对齐 sb_skin_t）。
extern const uint16_t SB_SKIN_UNLOCK_STREAK[SB_SKIN_COUNT];

// 掷杯结果。概率模型：两枚均匀杯，一阴一阳=圣杯(0.5)，两阳=笑杯(0.25)，两阴=阴杯(0.25)。
typedef enum {
    SB_THROW_SHENG = 0,
    SB_THROW_XIAO,
    SB_THROW_YIN,
    SB_THROW_COUNT,
} sb_throw_t;

// 杯皮肤（解锁顺序=累计连续香火日）。
typedef enum {
    SB_SKIN_WOOD = 0,    // 木杯（默认拥有）
    SB_SKIN_JADE,        // 玉杯   连3
    SB_SKIN_GOLD,        // 金杯   连7
    SB_SKIN_GLASS,       // 琉璃杯 连30
    SB_SKIN_YUELAO,      // 月老杯 连60
    SB_SKIN_CAISHEN,     // 财神杯 连180
} sb_skin_t;

// 称号（仅本机显示）。
typedef enum {
    SB_TITLE_SHENG_WANG = 0,  // 圣杯王：一次仪式三掷全圣杯
    SB_TITLE_XIAO_DA_REN,     // 笑杯达人：累计笑杯 ≥ 30
    SB_TITLE_NI_XI,           // 阴杯逆袭：否极泰来后求得签
    SB_TITLE_HENG_XIN,        // 恒心三十：连续香火日 ≥ 30
    SB_TITLE_CHANG_KE,        // 座上常客：累计香火日 ≥ 15
    SB_TITLE_BAN_BAI,         // 半百藏签：收集 ≥ 50 种签
    SB_TITLE_QUAN_QIAN,       // 全签录：收集满 100 种签
    SB_TITLE_JIN_QIAN,        // 点签成金：金签 ≥ 10 种
} sb_title_t;

// 隐藏彩蛋（触发一次后不再重复弹出）。
typedef enum {
    SB_EGG_SAN_SHENG = 0,  // 圣圣圣：三掷皆圣杯
    SB_EGG_SAN_XIAO,       // 笑笑笑：三掷皆笑杯
    SB_EGG_PI_TAI_LAI,     // 否极泰来：三掷皆阴杯
    SB_EGG_BAI_ZHI,        // 百掷：累计掷杯 ≥ 100
    SB_EGG_QIAN_ZHI,       // 千掷：累计掷杯 ≥ 1000
    SB_EGG_QI_RI,          // 七日之约：连续香火日 ≥ 7 首次达成
    SB_EGG_CHU_JIN,        // 初得金签：第一次集齐重复签
    SB_EGG_YUAN_DING,      // 缘定三生：连续香火日 ≥ 360
} sb_egg_t;

// 今日仪式状态。
typedef enum {
    SB_TODAY_IDLE = 0,     // 未掷（本香火日还有三掷机会）
    SB_TODAY_QUALIFIED,    // 已得圣杯，可摇筒求签
    SB_TODAY_DRAWN,        // 今日签已得，仪式圆满
    SB_TODAY_FAILED,       // 三掷未得圣杯（有笑杯）——今日无缘
    SB_TODAY_PITAILAI,     // 三掷皆阴杯——否极泰来，天赐一签
} sb_today_state_t;

typedef struct {
    // 资源与签到
    uint16_t incense;        // 香火
    uint16_t streak;         // 连续香火日
    uint16_t pending_patch;  // 告假后待补的连续天数（补签可恢复）
    bool     leave_used;     // 本香火日已告假
    // 累计
    uint32_t total_throws;   // 掷杯累计
    uint16_t throw_count[SB_THROW_COUNT];  // 按结果分类
    uint16_t days_opened;    // 香火日累计
    // 今日
    uint8_t  today_state;    // sb_today_state_t
    uint8_t  today_throws;   // 已掷次数 0..3
    uint8_t  today_bits;     // 每掷 2bit，第 n 掷占 [2n, 2n+1)
    uint16_t today_qian;     // 今日签号（0=无）
    bool     today_gold;     // 今日签为金签（重复签升级）
    // 收集
    uint8_t  qian_count[SB_QIAN_MAX];  // 各签抽中次数：0 未得，1 有，≥2 金签
    // 游戏
    uint16_t best_lian_sheng;  // 连圣杯最佳纪录
    // 皮肤与成就
    uint8_t  skin_unlocked;  // 位图
    uint8_t  skin_selected;  // sb_skin_t
    uint8_t  titles;         // 位图
    uint8_t  eggs_seen;      // 位图
} sb_model_t;

// 一次性事件集，open/throw/draw 等操作后由 UI 读取并清空。
typedef struct {
    bool checked_in;      // 本次开机完成签到（香火+2）
    bool new_streak_high; // 连续香火日刷新个人最高（UI 用）
    uint8_t skins_unlocked;  // 本次新解锁的皮肤位图
    uint8_t titles_gained;   // 本次新获得的称号位图
    uint8_t eggs_new;        // 本次新触发的彩蛋位图
    bool qian_gold;          // 本次求签得金签（升级）
    bool record_broken;      // 连圣杯纪录被打破
} sb_events_t;

// —— 基础 ——
void sb_model_defaults(sb_model_t *m);
void sb_events_clear(sb_events_t *e);

// 轻量可复现伪随机（宿主测试与设备共用）。返回 0..0x7FFFFFFF。
uint32_t sb_rand(uint32_t *state);

// —— 香火日生命周期 ——
// 每次开机调用一次：完成签到、累计天数、结转解锁。
void sb_model_open_day(sb_model_t *m, sb_events_t *e);
// 告假（仅今日未掷时可用）：连续中断，待补天数记下。
bool sb_model_leave(sb_model_t *m, sb_events_t *e);
// 补签：消耗香火恢复告假前的连续天数。
bool sb_model_patch(sb_model_t *m, sb_events_t *e);

// —— 仪式掷杯 ——
// 由随机态生成一掷结果（公平杯概率）。
sb_throw_t sb_roll_throw(uint32_t *rand_state);
// 仪式掷杯：今日三次机会内记录结果并推进状态（三掷机会独立走完，
// 任一掷得圣杯即具求签资格；全阴=否极泰来，无圣杯且非全阴=无缘）。非法时返回 false。
bool sb_model_ritual_throw(sb_model_t *m, sb_events_t *e, sb_throw_t r);
// 求签：从百签库抽取一支（重复签升级金签）。仅 QUALIFIED/PITAILAI 可用。
bool sb_model_draw_qian(sb_model_t *m, sb_events_t *e, uint32_t *rand_state);

// —— 连圣杯小游戏 ——
// 小游戏掷杯：累计掷杯统计，返回 true=圣杯（连击继续）。
// "当前连击"为会话内瞬态（UI 维护）；中断时用 sb_model_game_finish 结算纪录。
bool sb_model_game_throw(sb_model_t *m, sb_events_t *e, sb_throw_t r);
// 结算一次完整连击：打破纪录则更新并奖励香火，返回 true。
bool sb_model_game_finish(sb_model_t *m, sb_events_t *e, uint16_t run);

// —— 翻牌配对小游戏（纯逻辑） ——
#define SB_PAIRS_GRID 16     // 4×4
#define SB_PAIRS_KINDS 8     // 8 对
typedef struct {
    uint8_t board[SB_PAIRS_GRID];   // 花色 0..7，两两成对
    uint8_t first_pick;             // 已翻第一张，0xFF=无
    uint8_t matched_mask_hi, matched_mask_lo;  // 已配对位图（16 位拆两字节）
    uint8_t flipped_mask_hi, flipped_mask_lo;  // 当前翻开位图
    uint8_t moves;
    bool    finished;
} sb_pairs_t;

void sb_pairs_init(sb_pairs_t *g, uint32_t *rand_state);
// 翻开 cursor 张：返回 0=无变化(已开/已配对) 1=第一张 2=第二张(若配对写入 matched)
int sb_pairs_pick(sb_pairs_t *g, uint8_t cursor);
// 第二张选定后判定：返回 1 配对成功，0 失败（并自动盖回翻开的牌）。
bool sb_pairs_resolve(sb_pairs_t *g);
bool sb_pairs_is_matched(const sb_pairs_t *g, uint8_t idx);
bool sb_pairs_is_flipped(const sb_pairs_t *g, uint8_t idx);

// —— 查询辅助 ——
bool sb_model_title_has(const sb_model_t *m, sb_title_t t);
bool sb_model_egg_seen(const sb_model_t *m, sb_egg_t g);
uint16_t sb_model_collected(const sb_model_t *m);   // 已收集签种数
uint16_t sb_model_gold_count(const sb_model_t *m);  // 金签种数
bool sb_model_skin_unlocked(const sb_model_t *m, sb_skin_t s);

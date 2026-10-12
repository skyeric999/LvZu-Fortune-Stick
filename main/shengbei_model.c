// main/shengbei_model.c —— 掷圣杯·吕祖求签 纯逻辑实现（见头文件说明）。
#include "shengbei_model.h"

#include <string.h>

const uint16_t SB_SKIN_UNLOCK_STREAK[SB_SKIN_COUNT] = {
    0,    // 木杯：默认
    3,    // 玉杯
    7,    // 金杯
    30,   // 琉璃杯
    60,   // 月老杯
    180,  // 财神杯
};

#define SB_XIAO_DA_REN_XIAO 30   // 笑杯达人：累计笑杯
#define SB_HENG_XIN_STREAK 30    // 恒心三十
#define SB_CHANG_KE_DAYS 15      // 座上常客
#define SB_BAN_BAI_COLLECTED 50  // 半百藏签
#define SB_JIN_QIAN_GOLD 10      // 点签成金
#define SB_EGG_BAI_ZHI_N 100     // 百掷
#define SB_EGG_QIAN_ZHI_N 1000   // 千掷
#define SB_EGG_QI_RI_N 7         // 七日之约
#define SB_EGG_YUAN_DING_N 360   // 缘定三生

// —— 基础 ——

void sb_model_defaults(sb_model_t *m) {
    memset(m, 0, sizeof(*m));
    m->skin_unlocked = 1u << SB_SKIN_WOOD;
    m->skin_selected = SB_SKIN_WOOD;
    m->today_qian = 0;
}

void sb_events_clear(sb_events_t *e) {
    memset(e, 0, sizeof(*e));
}

uint32_t sb_rand(uint32_t *state) {
    // xorshift32，宿主与设备行为一致。
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

// —— 内部：解锁结转 ——

static void refresh_skins(sb_model_t *m, sb_events_t *e) {
    for (int s = 0; s < SB_SKIN_COUNT; s++) {
        if (m->streak >= SB_SKIN_UNLOCK_STREAK[s]) {
            uint8_t bit = (uint8_t)(1u << s);
            if (!(m->skin_unlocked & bit)) {
                m->skin_unlocked |= bit;
                e->skins_unlocked |= bit;
            }
        }
    }
}

static void refresh_titles(sb_model_t *m, sb_events_t *e) {
    const struct { sb_title_t t; bool cond; } rules[] = {
        { SB_TITLE_SHENG_WANG,
          m->today_throws == 3 && m->today_state == SB_TODAY_QUALIFIED &&
              m->today_bits == 0x00 },  // 三掷全圣杯：低 6 位全 0（SB_THROW_SHENG=0）
        { SB_TITLE_XIAO_DA_REN, m->throw_count[SB_THROW_XIAO] >= SB_XIAO_DA_REN_XIAO },
        { SB_TITLE_NI_XI, m->today_state == SB_TODAY_DRAWN && m->today_bits == 0x2A },
        { SB_TITLE_HENG_XIN, m->streak >= SB_HENG_XIN_STREAK },
        { SB_TITLE_CHANG_KE, m->days_opened >= SB_CHANG_KE_DAYS },
        { SB_TITLE_BAN_BAI, sb_model_collected(m) >= SB_BAN_BAI_COLLECTED },
        { SB_TITLE_QUAN_QIAN, sb_model_collected(m) == SB_QIAN_MAX },
        { SB_TITLE_JIN_QIAN, sb_model_gold_count(m) >= SB_JIN_QIAN_GOLD },
    };
    for (size_t i = 0; i < sizeof(rules) / sizeof(rules[0]); i++) {
        if (rules[i].cond) {
            uint8_t bit = (uint8_t)(1u << rules[i].t);
            if (!(m->titles & bit)) {
                m->titles |= bit;
                e->titles_gained |= bit;
            }
        }
    }
}

// —— 香火日 ——

void sb_model_open_day(sb_model_t *m, sb_events_t *e) {
    m->days_opened++;
    m->streak++;
    m->incense += SB_INCENSE_DAILY;
    m->today_state = SB_TODAY_IDLE;
    m->today_throws = 0;
    m->today_bits = 0;
    m->today_qian = 0;
    m->today_gold = false;
    m->leave_used = false;

    uint16_t prev_best = m->best_lian_sheng;
    (void)prev_best;
    e->checked_in = true;
    if (m->streak >= SB_EGG_QI_RI_N) e->eggs_new |= 1u << SB_EGG_QI_RI;
    if (m->streak >= SB_EGG_YUAN_DING_N) e->eggs_new |= 1u << SB_EGG_YUAN_DING;
    refresh_skins(m, e);
    refresh_titles(m, e);
}

bool sb_model_leave(sb_model_t *m, sb_events_t *e) {
    (void)e;
    if (m->leave_used || m->today_state != SB_TODAY_IDLE || m->streak == 0) return false;
    m->pending_patch = m->streak;
    m->streak = 0;
    m->leave_used = true;
    return true;
}

bool sb_model_patch(sb_model_t *m, sb_events_t *e) {
    if (m->pending_patch == 0 || m->incense < SB_PATCH_COST) return false;
    m->incense -= SB_PATCH_COST;
    m->streak = m->pending_patch;
    m->pending_patch = 0;
    refresh_skins(m, e);
    refresh_titles(m, e);
    return true;
}

// —— 仪式 ——

sb_throw_t sb_roll_throw(uint32_t *rand_state) {
    // 两枚公平杯：bit0/bit1 各表一枚（1=阳）。一阴一阳=圣杯，两阳=笑杯，两阴=阴杯。
    uint32_t r = sb_rand(rand_state);
    uint32_t c1 = r & 1u, c2 = (r >> 1) & 1u;
    if (c1 != c2) return SB_THROW_SHENG;
    return c1 ? SB_THROW_XIAO : SB_THROW_YIN;
}

static void count_throw(sb_model_t *m, sb_events_t *e, sb_throw_t r) {
    m->total_throws++;
    m->throw_count[r]++;
    if (m->total_throws == SB_EGG_BAI_ZHI_N) e->eggs_new |= 1u << SB_EGG_BAI_ZHI;
    if (m->total_throws == SB_EGG_QIAN_ZHI_N) e->eggs_new |= 1u << SB_EGG_QIAN_ZHI;
}

bool sb_model_ritual_throw(sb_model_t *m, sb_events_t *e, sb_throw_t r) {
    if (m->today_state != SB_TODAY_IDLE) return false;
    if (m->today_throws >= 3) return false;

    m->today_bits |= (uint8_t)(r << (2 * m->today_throws));
    m->today_throws++;
    count_throw(m, e, r);

    // 三掷机会独立：任一掷得圣杯即具求签资格，但三掷机会照常走完，
    // 三掷全圣杯（圣杯王）才有意义。
    if (m->today_throws == 3) {
        uint8_t bits = (uint8_t)(m->today_bits & 0x3F);
        bool any_sheng = false, all_yin = true;
        for (int i = 0; i < 3; i++) {
            sb_throw_t t = (sb_throw_t)((bits >> (2 * i)) & 0x3u);
            if (t == SB_THROW_SHENG) any_sheng = true;
            if (t != SB_THROW_YIN) all_yin = false;
        }
        if (all_yin) {
            m->today_state = SB_TODAY_PITAILAI;
            e->eggs_new |= 1u << SB_EGG_PI_TAI_LAI;
        } else if (any_sheng) {
            m->today_state = SB_TODAY_QUALIFIED;
        } else {
            m->today_state = SB_TODAY_FAILED;
        }
        if (bits == 0x00) e->eggs_new |= 1u << SB_EGG_SAN_SHENG;  // 三掷皆圣
        if (bits == 0x15) e->eggs_new |= 1u << SB_EGG_SAN_XIAO;   // 三掷皆笑
    }
    refresh_titles(m, e);
    return true;
}

bool sb_model_draw_qian(sb_model_t *m, sb_events_t *e, uint32_t *rand_state) {
    if (m->today_state != SB_TODAY_QUALIFIED && m->today_state != SB_TODAY_PITAILAI) {
        return false;
    }
    uint16_t id = (uint16_t)(sb_rand(rand_state) % SB_QIAN_MAX + 1);
    m->today_qian = id;
    m->qian_count[id - 1]++;
    bool gold = m->qian_count[id - 1] >= 2;
    m->today_gold = gold;
    m->today_state = SB_TODAY_DRAWN;

    if (gold) e->qian_gold = true;
    if (gold && m->qian_count[id - 1] == 2) e->eggs_new |= 1u << SB_EGG_CHU_JIN;
    refresh_titles(m, e);  // 阴杯逆袭等称号由此统一结转
    return true;
}

// —— 连圣杯 ——

// 小游戏掷杯：仅累计掷杯统计。返回 true=圣杯（连击继续），false=连击中断。
// "当前连击"为会话内瞬态，由 UI 维护；中断时用 sb_model_game_finish 结算。
bool sb_model_game_throw(sb_model_t *m, sb_events_t *e, sb_throw_t r) {
    count_throw(m, e, r);
    refresh_titles(m, e);  // 笑杯达人等累计称号在游戏中也能达成
    return r == SB_THROW_SHENG;
}
bool sb_model_game_finish(sb_model_t *m, sb_events_t *e, uint16_t run) {
    if (run > m->best_lian_sheng) {
        m->best_lian_sheng = run;
        m->incense += SB_GAME_RECORD_BONUS;
        e->record_broken = true;
        return true;
    }
    return false;
}

// —— 翻牌配对 ——

void sb_pairs_init(sb_pairs_t *g, uint32_t *rand_state) {
    for (uint8_t i = 0; i < SB_PAIRS_KINDS; i++) {
        g->board[2 * i] = i;
        g->board[2 * i + 1] = i;
    }
    // Fisher–Yates 洗牌
    for (uint8_t i = SB_PAIRS_GRID - 1; i > 0; i--) {
        uint8_t j = (uint8_t)(sb_rand(rand_state) % (i + 1));
        uint8_t t = g->board[i];
        g->board[i] = g->board[j];
        g->board[j] = t;
    }
    g->first_pick = 0xFF;
    g->matched_mask_hi = 0;
    g->matched_mask_lo = 0;
    g->flipped_mask_hi = 0;
    g->flipped_mask_lo = 0;
    g->moves = 0;
    g->finished = false;
}

static bool pairs_bit_get(uint8_t hi, uint8_t lo, uint8_t idx) {
    return idx < 8 ? ((lo >> idx) & 1u) : ((hi >> (idx - 8)) & 1u);
}

static void pairs_bit_set(uint8_t *hi, uint8_t *lo, uint8_t idx) {
    if (idx < 8) *lo |= (uint8_t)(1u << idx);
    else *hi |= (uint8_t)(1u << (idx - 8));
}

bool sb_pairs_is_matched(const sb_pairs_t *g, uint8_t idx) {
    return pairs_bit_get(g->matched_mask_hi, g->matched_mask_lo, idx);
}

bool sb_pairs_is_flipped(const sb_pairs_t *g, uint8_t idx) {
    return pairs_bit_get(g->flipped_mask_hi, g->flipped_mask_lo, idx);
}

static void pairs_flip_clear(sb_pairs_t *g) {
    g->flipped_mask_hi = g->matched_mask_hi;
    g->flipped_mask_lo = g->matched_mask_lo;
}

int sb_pairs_pick(sb_pairs_t *g, uint8_t cursor) {
    if (cursor >= SB_PAIRS_GRID || g->finished) return 0;
    if (sb_pairs_is_matched(g, cursor) || sb_pairs_is_flipped(g, cursor)) return 0;

    if (g->first_pick == 0xFF) {
        g->first_pick = cursor;
        pairs_bit_set(&g->flipped_mask_hi, &g->flipped_mask_lo, cursor);
        return 1;
    }
    pairs_bit_set(&g->flipped_mask_hi, &g->flipped_mask_lo, cursor);
    g->moves++;
    return 2;
}

bool sb_pairs_resolve(sb_pairs_t *g) {
    if (g->first_pick == 0xFF || g->finished) return false;
    uint8_t a = g->first_pick, b = 0xFF;
    for (uint8_t i = 0; i < SB_PAIRS_GRID; i++) {
        // 只在"当前翻开且未配对"的牌里找第二张——否则上一对的已配对牌
        // 仍留在 flipped 掩码里，会被误认成第二张翻开牌。
        if (sb_pairs_is_flipped(g, i) && !sb_pairs_is_matched(g, i) && i != a) {
            b = i;
            break;
        }
    }
    if (b == 0xFF) return false;

    bool ok = g->board[a] == g->board[b];
    if (ok) {
        pairs_bit_set(&g->matched_mask_hi, &g->matched_mask_lo, a);
        pairs_bit_set(&g->matched_mask_hi, &g->matched_mask_lo, b);
        uint8_t matched = 0;
        for (uint8_t i = 0; i < SB_PAIRS_GRID; i++) {
            if (sb_pairs_is_matched(g, i)) matched++;
        }
        if (matched == SB_PAIRS_GRID) g->finished = true;
    }
    // 无论成败，翻盖掩码归位到已配对集合：翻开态是瞬态，由 matched 驱动常显。
    pairs_flip_clear(g);
    g->first_pick = 0xFF;
    return ok;
}

// —— 查询 ——

bool sb_model_title_has(const sb_model_t *m, sb_title_t t) {
    return (m->titles & (1u << t)) != 0;
}

bool sb_model_egg_seen(const sb_model_t *m, sb_egg_t g) {
    return (m->eggs_seen & (1u << g)) != 0;
}

uint16_t sb_model_collected(const sb_model_t *m) {
    uint16_t n = 0;
    for (int i = 0; i < SB_QIAN_MAX; i++) {
        if (m->qian_count[i] > 0) n++;
    }
    return n;
}

uint16_t sb_model_gold_count(const sb_model_t *m) {
    uint16_t n = 0;
    for (int i = 0; i < SB_QIAN_MAX; i++) {
        if (m->qian_count[i] >= 2) n++;
    }
    return n;
}

bool sb_model_skin_unlocked(const sb_model_t *m, sb_skin_t s) {
    return (m->skin_unlocked & (1u << s)) != 0;
}

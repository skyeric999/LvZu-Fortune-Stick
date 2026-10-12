// tests/test_shengbei_model.c —— 掷圣杯·吕祖求签 纯逻辑宿主测试。
// 覆盖：签到/香火日、三掷定签状态机、求签与金签、补签、皮肤/称号解锁、
//       彩蛋触发、连圣杯结算、翻牌配对、签文数据结构校验。
// 编译（validate.sh --static 自动执行）：
//   cc -std=c11 -Wall -Wextra -Werror -Imain tests/test_shengbei_model.c
//      main/shengbei_model.c main/qian_data.c -o test_shengbei_model
#include "qian_data.h"
#include "shengbei_model.h"

#include <stdio.h>
#include <string.h>

static int g_fail;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
            g_fail++;                                                     \
        }                                                                 \
    } while (0)

// 统计 UTF-8 字符数（按前导字节）。
static int utf8_count(const char *s) {
    int n = 0;
    for (; *s; s++) {
        if (((unsigned char)*s & 0xC0) != 0x80) n++;
    }
    return n;
}

static void test_defaults(void) {
    sb_model_t m;
    sb_model_defaults(&m);
    CHECK(m.incense == 0);
    CHECK(m.streak == 0);
    CHECK(m.today_state == SB_TODAY_IDLE);
    CHECK(m.skin_unlocked == (uint8_t)(1u << SB_SKIN_WOOD));
    CHECK(m.skin_selected == SB_SKIN_WOOD);
    CHECK(sb_model_skin_unlocked(&m, SB_SKIN_WOOD));
    CHECK(!sb_model_skin_unlocked(&m, SB_SKIN_JADE));
    CHECK(sb_model_collected(&m) == 0);
}

static void test_rand_and_roll(void) {
    uint32_t s = 0xBEEF;
    for (int i = 0; i < 100; i++) {
        uint32_t v = sb_rand(&s);
        CHECK(v != 0);  // xorshift32 非零态自持
    }
    // 概率分布：圣杯 0.5、笑杯 0.25、阴杯 0.25。5000 掷给出宽松区间。
    uint32_t r = 0xCAFE;
    int cnt[SB_THROW_COUNT] = {0};
    for (int i = 0; i < 5000; i++) cnt[sb_roll_throw(&r)]++;
    CHECK(cnt[SB_THROW_SHENG] > 2200 && cnt[SB_THROW_SHENG] < 2800);
    CHECK(cnt[SB_THROW_XIAO] > 1000 && cnt[SB_THROW_XIAO] < 1500);
    CHECK(cnt[SB_THROW_YIN] > 1000 && cnt[SB_THROW_YIN] < 1500);
}

static void test_open_day(void) {
    sb_model_t m;
    sb_events_t e;
    sb_model_defaults(&m);
    sb_events_clear(&e);
    sb_model_open_day(&m, &e);
    CHECK(m.days_opened == 1);
    CHECK(m.streak == 1);
    CHECK(m.incense == SB_INCENSE_DAILY);
    CHECK(e.checked_in);
    CHECK(m.today_state == SB_TODAY_IDLE);
}

// 驱动一次指定序列的三掷仪式，返回末次事件。
static bool ritual(sb_model_t *m, sb_throw_t a, sb_throw_t b, sb_throw_t c,
                   sb_events_t *e_out) {
    sb_events_t e;
    sb_events_clear(&e);
    m->today_state = SB_TODAY_IDLE;
    m->today_throws = 0;
    m->today_bits = 0;
    bool ok = sb_model_ritual_throw(m, &e, a) &&
              sb_model_ritual_throw(m, &e, b) &&
              sb_model_ritual_throw(m, &e, c);
    *e_out = e;
    return ok;
}

static void test_ritual_states(void) {
    sb_model_t m;
    sb_model_defaults(&m);
    sb_events_t e;

    // 圣圣圣 → 允签 + 圣杯王 + 圣圣圣彩蛋
    ritual(&m, SB_THROW_SHENG, SB_THROW_SHENG, SB_THROW_SHENG, &e);
    CHECK(m.today_state == SB_TODAY_QUALIFIED);
    CHECK(m.today_bits == 0x00);
    CHECK(e.eggs_new & (1u << SB_EGG_SAN_SHENG));
    CHECK(sb_model_title_has(&m, SB_TITLE_SHENG_WANG));

    // 圣阴笑 → 允签（任一圣杯）
    ritual(&m, SB_THROW_SHENG, SB_THROW_YIN, SB_THROW_XIAO, &e);
    CHECK(m.today_state == SB_TODAY_QUALIFIED);
    CHECK(!(e.eggs_new & (1u << SB_EGG_SAN_SHENG)));

    // 阴阴阴 → 否极泰来 + 彩蛋
    ritual(&m, SB_THROW_YIN, SB_THROW_YIN, SB_THROW_YIN, &e);
    CHECK(m.today_state == SB_TODAY_PITAILAI);
    CHECK(m.today_bits == 0x2A);
    CHECK(e.eggs_new & (1u << SB_EGG_PI_TAI_LAI));

    // 笑笑笑 → 无缘 + 彩蛋
    ritual(&m, SB_THROW_XIAO, SB_THROW_XIAO, SB_THROW_XIAO, &e);
    CHECK(m.today_state == SB_TODAY_FAILED);
    CHECK(m.today_bits == 0x15);
    CHECK(e.eggs_new & (1u << SB_EGG_SAN_XIAO));

    // 阴笑阴 → 无缘、无三同彩蛋
    ritual(&m, SB_THROW_YIN, SB_THROW_XIAO, SB_THROW_YIN, &e);
    CHECK(m.today_state == SB_TODAY_FAILED);
    CHECK(!(e.eggs_new & (1u << SB_EGG_SAN_XIAO)));
    CHECK(!(e.eggs_new & (1u << SB_EGG_PI_TAI_LAI)));

    // 掷满三掷后拒绝第四掷
    CHECK(!sb_model_ritual_throw(&m, &e, SB_THROW_SHENG));

    // 两掷后仍可继续（三掷机会独立）
    sb_events_clear(&e);
    m.today_state = SB_TODAY_IDLE;
    m.today_throws = 0;
    m.today_bits = 0;
    CHECK(sb_model_ritual_throw(&m, &e, SB_THROW_YIN));
    CHECK(m.today_state == SB_TODAY_IDLE);
    CHECK(sb_model_ritual_throw(&m, &e, SB_THROW_YIN));
    CHECK(m.today_state == SB_TODAY_IDLE);
    CHECK(sb_model_ritual_throw(&m, &e, SB_THROW_SHENG));
    CHECK(m.today_state == SB_TODAY_QUALIFIED);
}

static void test_draw_and_gold(void) {
    sb_model_t m;
    sb_model_defaults(&m);
    sb_events_t e;
    uint32_t r = 0x51CE;

    // 未允签不可求签
    sb_events_clear(&e);
    CHECK(!sb_model_draw_qian(&m, &e, &r));

    // 允签后可求签：多次抽取覆盖收集与金签升级
    sb_model_open_day(&m, &e);
    ritual(&m, SB_THROW_SHENG, SB_THROW_SHENG, SB_THROW_SHENG, &e);
    int gold_seen = 0, drew = 0;
    for (int i = 0; i < 2000; i++) {
        // 每次抽签后重新允许（模拟多日仪式）
        if (m.today_state != SB_TODAY_QUALIFIED && m.today_state != SB_TODAY_PITAILAI) {
            m.today_state = SB_TODAY_QUALIFIED;
        }
        sb_events_clear(&e);
        CHECK(sb_model_draw_qian(&m, &e, &r));
        drew++;
        CHECK(m.today_qian >= 1 && m.today_qian <= SB_QIAN_MAX);
        CHECK(m.today_state == SB_TODAY_DRAWN);
        if (e.qian_gold) gold_seen++;
        if (e.eggs_new & (1u << SB_EGG_CHU_JIN)) gold_seen += 100;  // 首次金签彩蛋出现过
        m.today_state = SB_TODAY_QUALIFIED;
    }
    CHECK(gold_seen > 100);           // 首金彩蛋触发过且后续金签事件正常
    CHECK(sb_model_collected(&m) > 50);  // 2000 抽必覆盖过半签库
    CHECK(drew == 2000);
}

static void test_leave_and_patch(void) {
    sb_model_t m;
    sb_model_defaults(&m);
    sb_events_t e;
    sb_events_clear(&e);

    // 连续 5 天
    for (int i = 0; i < 5; i++) sb_model_open_day(&m, &e);
    CHECK(m.streak == 5);
    CHECK(m.incense == 10);

    // 告假：连续中断，待补 5
    CHECK(sb_model_leave(&m, &e));
    CHECK(m.streak == 0);
    CHECK(m.pending_patch == 5);
    CHECK(m.leave_used);
    CHECK(!sb_model_leave(&m, &e));  // 一日只许告假一次

    // 告假次日开机：连续重新计 1，待补仍在
    sb_events_clear(&e);
    sb_model_open_day(&m, &e);
    CHECK(m.streak == 1);
    CHECK(m.pending_patch == 5);

    // 香火不足不可补签（此时 8 < 5? 实为 8 ≥ 5 可补——直接验证扣减与恢复）
    uint16_t incense_before = m.incense;
    CHECK(sb_model_patch(&m, &e));
    CHECK(m.incense == incense_before - SB_PATCH_COST);
    CHECK(m.streak == 5);
    CHECK(m.pending_patch == 0);
    CHECK(!sb_model_patch(&m, &e));  // 无待补

    // 香火不足时补签被拒
    m.pending_patch = 5;
    m.incense = SB_PATCH_COST - 1;
    CHECK(!sb_model_patch(&m, &e));
}

static void test_skins_and_titles(void) {
    sb_model_t m;
    sb_model_defaults(&m);
    sb_events_t e;
    sb_events_clear(&e);

    // 180 个香火日：全部皮肤按门槛解锁
    for (int i = 0; i < 180; i++) {
        sb_events_clear(&e);
        sb_model_open_day(&m, &e);
    }
    CHECK(m.streak == 180);
    CHECK(m.days_opened == 180);
    for (int s = 0; s < SB_SKIN_COUNT; s++) {
        CHECK(sb_model_skin_unlocked(&m, (sb_skin_t)s));
    }
    CHECK(sb_model_title_has(&m, SB_TITLE_HENG_XIN));  // 连续 30
    CHECK(sb_model_title_has(&m, SB_TITLE_CHANG_KE));  // 累计 15 日

    // 笑杯达人：30 次笑杯（小游戏掷杯同样计入累计）
    sb_events_clear(&e);
    for (int i = 0; i < 30; i++) {
        sb_model_game_throw(&m, &e, SB_THROW_XIAO);
    }
    CHECK(sb_model_title_has(&m, SB_TITLE_XIAO_DA_REN));

    // 点签成金：10 种金签（直接置位后经一次掷杯结转）
    for (int i = 0; i < 10; i++) m.qian_count[i] = 2;
    sb_events_clear(&e);
    m.today_state = SB_TODAY_IDLE;
    m.today_throws = 0;
    m.today_bits = 0;
    sb_model_ritual_throw(&m, &e, SB_THROW_SHENG);
    CHECK(sb_model_title_has(&m, SB_TITLE_JIN_QIAN));
    CHECK(sb_model_gold_count(&m) == 10);
}

static void test_throw_milestone_eggs(void) {
    sb_model_t m;
    sb_model_defaults(&m);
    sb_events_t e;
    sb_events_clear(&e);
    bool bai = false, qian = false;
    for (int i = 0; i < 1000; i++) {
        sb_model_game_throw(&m, &e, SB_THROW_SHENG);
        if (e.eggs_new & (1u << SB_EGG_BAI_ZHI)) bai = true;
        if (e.eggs_new & (1u << SB_EGG_QIAN_ZHI)) qian = true;
        sb_events_clear(&e);
    }
    CHECK(m.total_throws == 1000);
    CHECK(bai);   // 恰在第 100 掷
    CHECK(qian);  // 恰在第 1000 掷
}

static void test_game_finish(void) {
    sb_model_t m;
    sb_model_defaults(&m);
    sb_events_t e;
    sb_events_clear(&e);
    CHECK(sb_model_game_finish(&m, &e, 5));
    CHECK(m.best_lian_sheng == 5);
    CHECK(m.incense == SB_GAME_RECORD_BONUS);
    CHECK(e.record_broken);
    sb_events_clear(&e);
    CHECK(!sb_model_game_finish(&m, &e, 3));  // 未破纪录
    CHECK(m.incense == SB_GAME_RECORD_BONUS);
}

static void test_pairs(void) {
    sb_pairs_t g;
    uint32_t r = 0xABCD;
    sb_pairs_init(&g, &r);

    // 每种花色恰好两张
    int seen[SB_PAIRS_KINDS] = {0};
    for (int i = 0; i < SB_PAIRS_GRID; i++) {
        CHECK(g.board[i] < SB_PAIRS_KINDS);
        seen[g.board[i]]++;
    }
    for (int k = 0; k < SB_PAIRS_KINDS; k++) CHECK(seen[k] == 2);

    // 找一对相同花色 → 配对成功
    int a = -1, b = -1;
    for (int i = 0; i < SB_PAIRS_GRID && b < 0; i++) {
        for (int j = i + 1; j < SB_PAIRS_GRID; j++) {
            if (g.board[i] == g.board[j]) { a = i; b = j; break; }
        }
    }
    CHECK(sb_pairs_pick(&g, (uint8_t)a) == 1);
    CHECK(sb_pairs_is_flipped(&g, (uint8_t)a));
    CHECK(sb_pairs_pick(&g, (uint8_t)b) == 2);
    CHECK(sb_pairs_resolve(&g));
    CHECK(sb_pairs_is_matched(&g, (uint8_t)a));
    CHECK(sb_pairs_is_matched(&g, (uint8_t)b));
    CHECK(sb_pairs_pick(&g, (uint8_t)a) == 0);  // 已配对不可再翻
    CHECK(!g.finished);

    // 找一对不同花色 → 失配盖回
    int c = -1, d = -1;
    for (int i = 0; i < SB_PAIRS_GRID && d < 0; i++) {
        if (sb_pairs_is_matched(&g, (uint8_t)i)) continue;
        for (int j = i + 1; j < SB_PAIRS_GRID; j++) {
            if (sb_pairs_is_matched(&g, (uint8_t)j)) continue;
            if (g.board[i] != g.board[j]) { c = i; d = j; break; }
        }
    }
    CHECK(c >= 0);
    CHECK(sb_pairs_pick(&g, (uint8_t)c) == 1);
    CHECK(sb_pairs_pick(&g, (uint8_t)d) == 2);
    CHECK(!sb_pairs_resolve(&g));
    CHECK(!sb_pairs_is_flipped(&g, (uint8_t)c));  // 失配自动盖回

    // 已翻开/已配对的牌再翻无效果
    sb_pairs_pick(&g, (uint8_t)a);
    CHECK(sb_pairs_pick(&g, (uint8_t)a) == 0);

    // 翻完全部 16 张（逐对成功）→ finished
    for (int round = 0; round < SB_PAIRS_KINDS - 1; round++) {
        int x = -1, y = -1;
        for (int i = 0; i < SB_PAIRS_GRID; i++) {
            if (sb_pairs_is_matched(&g, (uint8_t)i) || sb_pairs_is_flipped(&g, (uint8_t)i))
                continue;
            if (x < 0) { x = i; continue; }
            if (g.board[i] == g.board[x]) { y = i; break; }
        }
        CHECK(x >= 0 && y >= 0);
        CHECK(sb_pairs_pick(&g, (uint8_t)x) == 1);
        CHECK(sb_pairs_pick(&g, (uint8_t)y) == 2);
        CHECK(sb_pairs_resolve(&g));
    }
    CHECK(g.finished);
    CHECK(sb_pairs_pick(&g, 0) == 0);  // 结束后不再接受翻牌
    CHECK(!sb_pairs_resolve(&g));
}

static void test_qian_data_integrity(void) {
    CHECK(QIAN_COUNT == 100);
    for (int i = 0; i < QIAN_COUNT; i++) {
        const lvzu_qian_t *q = &QIAN[i];
        CHECK(q->id == i + 1);
        CHECK(q->level >= 0 && q->level < 6);
        int t = utf8_count(q->title);
        CHECK(t >= 3 && t <= 6);

        // 签诗：两行，每行 16 字（七言，七言。）
        char buf[256];
        snprintf(buf, sizeof(buf), "%s", q->poem);
        char *nl = strchr(buf, '\n');
        CHECK(nl != NULL);
        if (nl) {
            *nl = 0;
            char *l1 = buf, *l2 = nl + 1;
            CHECK(utf8_count(l1) == 16);
            CHECK(utf8_count(l2) == 16);
            CHECK(strchr(l2, '\n') == NULL);
            // 每行句读：第 8 字是逗号（U+FF0C = EF BC 8C），末字是句号（U+3002）
            CHECK(l1[7 * 3] == '\xef' && l1[7 * 3 + 1] == '\xbc' && l1[7 * 3 + 2] == '\x8c');
            CHECK(l2[7 * 3] == '\xef' && l2[7 * 3 + 1] == '\xbc' && l2[7 * 3 + 2] == '\x8c');
            CHECK(strstr(l1, "\xe3\x80\x82") != NULL);
            CHECK(strstr(l2, "\xe3\x80\x82") != NULL);
        }
        int j = utf8_count(q->jie);
        CHECK(j >= 8 && j <= 40);
    }
    CHECK(QIAN_LEVEL_NAMES[0] != NULL && QIAN_LEVEL_NAMES[5] != NULL);
}

int main(void) {
    test_defaults();
    test_rand_and_roll();
    test_open_day();
    test_ritual_states();
    test_draw_and_gold();
    test_leave_and_patch();
    test_skins_and_titles();
    test_throw_milestone_eggs();
    test_game_finish();
    test_pairs();
    test_qian_data_integrity();
    if (g_fail) {
        printf("FAILED: %d check(s)\n", g_fail);
        return 1;
    }
    printf("test_shengbei_model: PASS\n");
    return 0;
}

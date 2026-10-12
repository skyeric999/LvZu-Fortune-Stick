// main/qian_data.h —— 吕祖灵签百签离线数据接口。
// 签文以传统吕祖灵签传本为基础整理改写，用于离线求签演示，非权威校勘版本。
#pragma once

#include <stdint.h>

#define QIAN_COUNT 100

// 吉凶等级（用于显示与配色）。
typedef enum {
    QIAN_LEVEL_SHANG_SHANG = 0,  // 上上
    QIAN_LEVEL_SHANG_JI,         // 上吉
    QIAN_LEVEL_ZHONG_JI,         // 中吉
    QIAN_LEVEL_ZHONG_PING,       // 中平
    QIAN_LEVEL_PING,             // 平
    QIAN_LEVEL_XIA,              // 下签
} qian_level_t;

typedef struct {
    uint8_t id;            // 签号 1..100
    uint8_t level;         // qian_level_t
    const char *title;     // 典故四字
    const char *poem;      // 签诗四句（含标点，\n 分行）
    const char *jie;       // 解曰（简断语）
} lvzu_qian_t;

// 等级中文名，按 qian_level_t 下标取用。
extern const char *const QIAN_LEVEL_NAMES[6];

// 百签数据表，QIAN[i].id == i + 1。
extern const lvzu_qian_t QIAN[QIAN_COUNT];

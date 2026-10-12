// main/shengbei_store.h —— 香火档案的 NVS 持久化（后台落盘，类似番茄钟存档模式）。
#pragma once

#include <stdbool.h>

#include "shengbei_model.h"

// 初始化：打开 NVS、载入档案（无档则用默认值）、启动落盘 worker。
// 失败时返回 false（UI 仍可运行，但本会话改动不会持久化）。
bool shengbei_store_init(sb_model_t *model);

// 请求把当前模型写入 NVS（非阻塞；worker 只保留最新一份）。
void shengbei_store_request_save(const sb_model_t *model);

// 本会话存取是否出现过错误。
bool shengbei_store_has_error(void);

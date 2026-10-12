// main/shengbei_store.c —— 香火档案持久化：magic+version+crc 的整体 blob，后台 worker 落盘。
#include "shengbei_store.h"

#include <stddef.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

#define STORE_MAGIC 0x53424355U  // "SBCU"
#define STORE_VERSION 1
#define STORE_KEY "archive"

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    sb_model_t model;
    uint32_t crc32;
} store_blob_t;

static const char *TAG = "sb_store";
static QueueHandle_t s_queue;
static nvs_handle_t s_nvs;
static bool s_ready;
static volatile bool s_error;

static uint32_t crc32_bytes(const void *data, size_t len) {
    const uint8_t *bytes = data;
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0; i < len; i++) {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc >> 1) ^ (0xEDB88320U & (uint32_t)-(int32_t)(crc & 1));
        }
    }
    return ~crc;
}

static store_blob_t blob_from_model(const sb_model_t *model) {
    store_blob_t blob = {
        .magic = STORE_MAGIC,
        .version = STORE_VERSION,
        .size = sizeof(sb_model_t),
        .model = *model,
    };
    blob.crc32 = crc32_bytes(&blob.model, sizeof(blob.model));
    return blob;
}

static bool model_from_blob(const store_blob_t *blob, sb_model_t *model) {
    if (blob->magic != STORE_MAGIC || blob->version != STORE_VERSION ||
        blob->size != sizeof(blob->model) ||
        blob->crc32 != crc32_bytes(&blob->model, sizeof(blob->model))) {
        return false;
    }
    *model = blob->model;
    return true;
}

static void save_task(void *arg) {
    (void)arg;
    store_blob_t blob;
    for (;;) {
        if (xQueueReceive(s_queue, &blob, portMAX_DELAY) == pdTRUE) {
            esp_err_t err = nvs_set_blob(s_nvs, STORE_KEY, &blob, sizeof(blob));
            if (err == ESP_OK) err = nvs_commit(s_nvs);
            if (err != ESP_OK) {
                s_error = true;
                ESP_LOGE(TAG, "NVS 保存失败: %s", esp_err_to_name(err));
            }
        }
    }
}

bool shengbei_store_init(sb_model_t *model) {
    if (s_ready) return true;

    sb_model_defaults(model);
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS 分区需要恢复: %s", esp_err_to_name(err));
        err = nvs_flash_erase();
        if (err == ESP_OK) err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS 初始化失败: %s", esp_err_to_name(err));
        s_error = true;
        return false;
    }

    err = nvs_open("shengbei", NVS_READWRITE, &s_nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS 打开失败: %s", esp_err_to_name(err));
        s_error = true;
        return false;
    }

    store_blob_t blob;
    size_t size = sizeof(blob);
    err = nvs_get_blob(s_nvs, STORE_KEY, &blob, &size);
    if (err == ESP_OK && size == sizeof(blob)) {
        if (!model_from_blob(&blob, model)) {
            ESP_LOGW(TAG, "存档校验失败，使用默认档案");
        } else {
            ESP_LOGI(TAG, "已恢复香火档案: 连%u日 香火%u 掷杯%lu",
                     model->streak, model->incense, (unsigned long)model->total_throws);
        }
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "档案读取失败: %s", esp_err_to_name(err));
    }

    s_queue = xQueueCreate(1, sizeof(store_blob_t));
    if (!s_queue || xTaskCreate(save_task, "sb_save", 3072, NULL, 3, NULL) != pdPASS) {
        ESP_LOGE(TAG, "存档 worker 创建失败");
        s_error = true;
        return false;
    }
    s_ready = true;
    return true;
}

void shengbei_store_request_save(const sb_model_t *model) {
    if (!s_ready || !model) return;
    store_blob_t blob = blob_from_model(model);
    if (xQueueOverwrite(s_queue, &blob) != pdTRUE) s_error = true;
}

bool shengbei_store_has_error(void) {
    return s_error;
}

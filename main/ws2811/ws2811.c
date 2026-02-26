#include "ws2811.h"
#include <string.h>
#include <math.h>
#include "esp_log.h"
#include "led_strip.h"

static const char *TAG = "WS2811";

// WS2811 内部结构体
typedef struct {
    led_strip_handle_t strip;
    uint32_t led_count;
    uint8_t brightness;
    uint8_t max_brightness;
} ws2811_t;

// ============================================================================
// 初始化和释放
// ============================================================================

esp_err_t ws2811_init(const ws2811_config_t *config, ws2811_handle_t *handle) {
    if (config == NULL || handle == NULL) {
        ESP_LOGE(TAG, "Invalid arguments");
        return ESP_ERR_INVALID_ARG;
    }

    if (config->led_count == 0) {
        ESP_LOGE(TAG, "LED count must be greater than 0");
        return ESP_ERR_INVALID_ARG;
    }

    // 分配内存
    ws2811_t *ws2811 = (ws2811_t *)calloc(1, sizeof(ws2811_t));
    if (ws2811 == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory");
        return ESP_ERR_NO_MEM;
    }

    ws2811->led_count = config->led_count;
    ws2811->brightness = config->max_brightness;
    ws2811->max_brightness = config->max_brightness;

    // 配置 LED 灯带
    led_strip_config_t strip_config = {
        .strip_gpio_num = config->gpio_num,
        .max_leds = config->led_count,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,  // WS2812 使用 GRB 格式
        .led_model = LED_MODEL_WS2812,
    };

    // 配置 RMT
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000,  // 10MHz
    };

    // 创建 LED 灯带
    ESP_LOGI(TAG, "Creating RMT device: GPIO=%d, LEDs=%lu", config->gpio_num, config->led_count);
    esp_err_t ret = led_strip_new_rmt_device(&strip_config, &rmt_config, &ws2811->strip);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create LED strip: %s", esp_err_to_name(ret));
        free(ws2811);
        return ret;
    }
    ESP_LOGI(TAG, "RMT device created successfully");

    // 不要在初始化时清除 LED，让用户自己控制
    // led_strip_clear(ws2811->strip);

    *handle = (ws2811_handle_t)ws2811;
    ESP_LOGI(TAG, "WS2811 initialized: GPIO=%d, LEDs=%lu, Brightness=%d", 
             config->gpio_num, config->led_count, config->max_brightness);

    return ESP_OK;
}

esp_err_t ws2811_deinit(ws2811_handle_t handle) {
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ws2811_t *ws2811 = (ws2811_t *)handle;

    // 清除所有 LED
    led_strip_clear(ws2811->strip);

    // 删除 LED 灯带
    led_strip_del(ws2811->strip);

    // 释放内存
    free(ws2811);

    ESP_LOGI(TAG, "WS2811 deinitialized");
    return ESP_OK;
}

// ============================================================================
// LED 控制
// ============================================================================

esp_err_t ws2811_set_pixel(ws2811_handle_t handle, uint32_t index, ws2811_color_t color) {
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ws2811_t *ws2811 = (ws2811_t *)handle;

    if (index >= ws2811->led_count) {
        ESP_LOGE(TAG, "LED index %lu out of range (max: %lu)", index, ws2811->led_count - 1);
        return ESP_ERR_INVALID_ARG;
    }

    // 应用亮度调整
    uint8_t r = (color.r * ws2811->brightness) / 255;
    uint8_t g = (color.g * ws2811->brightness) / 255;
    uint8_t b = (color.b * ws2811->brightness) / 255;

    return led_strip_set_pixel(ws2811->strip, index, r, g, b);
}

esp_err_t ws2811_set_pixel_rgb(ws2811_handle_t handle, uint32_t index, uint8_t r, uint8_t g, uint8_t b) {
    ws2811_color_t color = {r, g, b};
    return ws2811_set_pixel(handle, index, color);
}

esp_err_t ws2811_set_all(ws2811_handle_t handle, ws2811_color_t color) {
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ws2811_t *ws2811 = (ws2811_t *)handle;

    for (uint32_t i = 0; i < ws2811->led_count; i++) {
        esp_err_t ret = ws2811_set_pixel(handle, i, color);
        if (ret != ESP_OK) {
            return ret;
        }
    }

    return ESP_OK;
}

esp_err_t ws2811_set_all_rgb(ws2811_handle_t handle, uint8_t r, uint8_t g, uint8_t b) {
    ws2811_color_t color = {r, g, b};
    return ws2811_set_all(handle, color);
}

esp_err_t ws2811_clear(ws2811_handle_t handle) {
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ws2811_t *ws2811 = (ws2811_t *)handle;
    return led_strip_clear(ws2811->strip);
}

esp_err_t ws2811_refresh(ws2811_handle_t handle) {
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ws2811_t *ws2811 = (ws2811_t *)handle;
    return led_strip_refresh(ws2811->strip);
}

// ============================================================================
// 亮度控制
// ============================================================================

esp_err_t ws2811_set_brightness(ws2811_handle_t handle, uint8_t brightness) {
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ws2811_t *ws2811 = (ws2811_t *)handle;

    // 限制亮度不超过最大值
    if (brightness > ws2811->max_brightness) {
        brightness = ws2811->max_brightness;
    }

    ws2811->brightness = brightness;
    ESP_LOGD(TAG, "Brightness set to %d", brightness);

    return ESP_OK;
}

uint8_t ws2811_get_brightness(ws2811_handle_t handle) {
    if (handle == NULL) {
        return 0;
    }

    ws2811_t *ws2811 = (ws2811_t *)handle;
    return ws2811->brightness;
}

// ============================================================================
// 颜色转换和辅助函数
// ============================================================================

ws2811_color_t ws2811_hsv_to_rgb(uint16_t h, uint8_t s, uint8_t v) {
    ws2811_color_t color = {0, 0, 0};

    // 限制输入范围
    h = h % 360;
    if (s > 100) s = 100;
    if (v > 100) v = 100;

    // 转换为 0-1 范围
    float hf = h / 60.0f;
    float sf = s / 100.0f;
    float vf = v / 100.0f;

    int i = (int)hf;
    float f = hf - i;
    float p = vf * (1.0f - sf);
    float q = vf * (1.0f - sf * f);
    float t = vf * (1.0f - sf * (1.0f - f));

    float r, g, b;

    switch (i) {
        case 0:
            r = vf; g = t; b = p;
            break;
        case 1:
            r = q; g = vf; b = p;
            break;
        case 2:
            r = p; g = vf; b = t;
            break;
        case 3:
            r = p; g = q; b = vf;
            break;
        case 4:
            r = t; g = p; b = vf;
            break;
        case 5:
        default:
            r = vf; g = p; b = q;
            break;
    }

    color.r = (uint8_t)(r * 255);
    color.g = (uint8_t)(g * 255);
    color.b = (uint8_t)(b * 255);

    return color;
}

ws2811_color_t ws2811_dim_color(ws2811_color_t color, uint8_t brightness) {
    ws2811_color_t dimmed;
    dimmed.r = (color.r * brightness) / 255;
    dimmed.g = (color.g * brightness) / 255;
    dimmed.b = (color.b * brightness) / 255;
    return dimmed;
}

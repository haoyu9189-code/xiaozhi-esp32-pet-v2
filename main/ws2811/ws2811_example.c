/**
 * @file ws2811_example.c
 * @brief WS2811/WS2812 LED 使用示例
 * 
 * 这个文件包含了各种 LED 效果的示例代码
 * 可以根据需要复制到你的项目中使用
 */

#include "ws2811.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "WS2811_EXAMPLE";

// ============================================================================
// 示例 1：基本使用
// ============================================================================

void example_basic_usage(void) {
    // 配置 LED
    ws2811_config_t config = {
        .gpio_num = 48,          // GPIO 引脚（根据你的硬件修改）
        .led_count = 1,          // LED 数量
        .max_brightness = 255,   // 最大亮度
    };

    // 初始化
    ws2811_handle_t led;
    esp_err_t ret = ws2811_init(&config, &led);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WS2811");
        return;
    }

    // 设置为红色
    ws2811_set_all(led, WS2811_COLOR_RED);
    ws2811_refresh(led);
    vTaskDelay(pdMS_TO_TICKS(1000));

    // 设置为绿色
    ws2811_set_all(led, WS2811_COLOR_GREEN);
    ws2811_refresh(led);
    vTaskDelay(pdMS_TO_TICKS(1000));

    // 设置为蓝色
    ws2811_set_all(led, WS2811_COLOR_BLUE);
    ws2811_refresh(led);
    vTaskDelay(pdMS_TO_TICKS(1000));

    // 关闭
    ws2811_clear(led);
    ws2811_refresh(led);

    // 释放资源
    ws2811_deinit(led);
}

// ============================================================================
// 示例 2：呼吸灯效果
// ============================================================================

void example_breathing_effect(void) {
    ws2811_config_t config = {
        .gpio_num = 48,
        .led_count = 1,
        .max_brightness = 255,
    };

    ws2811_handle_t led;
    ws2811_init(&config, &led);

    // 呼吸灯循环
    for (int cycle = 0; cycle < 5; cycle++) {
        // 渐亮
        for (int brightness = 0; brightness <= 255; brightness += 5) {
            ws2811_set_brightness(led, brightness);
            ws2811_set_all(led, WS2811_COLOR_BLUE);
            ws2811_refresh(led);
            vTaskDelay(pdMS_TO_TICKS(20));
        }

        // 渐暗
        for (int brightness = 255; brightness >= 0; brightness -= 5) {
            ws2811_set_brightness(led, brightness);
            ws2811_set_all(led, WS2811_COLOR_BLUE);
            ws2811_refresh(led);
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }

    ws2811_deinit(led);
}

// ============================================================================
// 示例 3：彩虹效果
// ============================================================================

void example_rainbow_effect(void) {
    ws2811_config_t config = {
        .gpio_num = 48,
        .led_count = 10,  // 10 个 LED
        .max_brightness = 255,
    };

    ws2811_handle_t led;
    ws2811_init(&config, &led);

    // 彩虹循环
    for (int offset = 0; offset < 360; offset += 5) {
        for (uint32_t i = 0; i < 10; i++) {
            // 每个 LED 显示不同的颜色
            uint16_t hue = (offset + i * 36) % 360;  // 36度间隔
            ws2811_color_t color = ws2811_hsv_to_rgb(hue, 100, 100);
            ws2811_set_pixel(led, i, color);
        }
        ws2811_refresh(led);
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    ws2811_deinit(led);
}

// ============================================================================
// 示例 4：闪烁效果
// ============================================================================

void example_blink_effect(void) {
    ws2811_config_t config = {
        .gpio_num = 48,
        .led_count = 1,
        .max_brightness = 255,
    };

    ws2811_handle_t led;
    ws2811_init(&config, &led);

    // 闪烁 10 次
    for (int i = 0; i < 10; i++) {
        // 开
        ws2811_set_all(led, WS2811_COLOR_RED);
        ws2811_refresh(led);
        vTaskDelay(pdMS_TO_TICKS(200));

        // 关
        ws2811_clear(led);
        ws2811_refresh(led);
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    ws2811_deinit(led);
}

// ============================================================================
// 示例 5：追逐效果（多个 LED）
// ============================================================================

void example_chase_effect(void) {
    ws2811_config_t config = {
        .gpio_num = 48,
        .led_count = 10,
        .max_brightness = 255,
    };

    ws2811_handle_t led;
    ws2811_init(&config, &led);

    // 追逐循环
    for (int cycle = 0; cycle < 5; cycle++) {
        for (uint32_t i = 0; i < 10; i++) {
            ws2811_clear(led);
            ws2811_set_pixel(led, i, WS2811_COLOR_GREEN);
            ws2811_refresh(led);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }

    ws2811_deinit(led);
}

// ============================================================================
// 示例 6：渐变效果
// ============================================================================

void example_fade_effect(void) {
    ws2811_config_t config = {
        .gpio_num = 48,
        .led_count = 1,
        .max_brightness = 255,
    };

    ws2811_handle_t led;
    ws2811_init(&config, &led);

    // 从红色渐变到蓝色
    for (int i = 0; i <= 255; i += 5) {
        uint8_t r = 255 - i;
        uint8_t b = i;
        ws2811_set_all_rgb(led, r, 0, b);
        ws2811_refresh(led);
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    ws2811_deinit(led);
}

// ============================================================================
// 示例 7：状态指示灯
// ============================================================================

void example_status_indicator(void) {
    ws2811_config_t config = {
        .gpio_num = 48,
        .led_count = 1,
        .max_brightness = 100,  // 降低亮度，更省电
    };

    ws2811_handle_t led;
    ws2811_init(&config, &led);

    // 模拟不同状态
    ESP_LOGI(TAG, "Status: Idle (Green)");
    ws2811_set_all(led, WS2811_COLOR_GREEN);
    ws2811_refresh(led);
    vTaskDelay(pdMS_TO_TICKS(2000));

    ESP_LOGI(TAG, "Status: Listening (Blue)");
    ws2811_set_all(led, WS2811_COLOR_BLUE);
    ws2811_refresh(led);
    vTaskDelay(pdMS_TO_TICKS(2000));

    ESP_LOGI(TAG, "Status: Speaking (Cyan)");
    ws2811_set_all(led, WS2811_COLOR_CYAN);
    ws2811_refresh(led);
    vTaskDelay(pdMS_TO_TICKS(2000));

    ESP_LOGI(TAG, "Status: Error (Red)");
    ws2811_set_all(led, WS2811_COLOR_RED);
    ws2811_refresh(led);
    vTaskDelay(pdMS_TO_TICKS(2000));

    ws2811_deinit(led);
}

// ============================================================================
// 主函数示例
// ============================================================================

void ws2811_example_task(void *pvParameters) {
    ESP_LOGI(TAG, "Starting WS2811 examples...");

    // 运行示例（取消注释你想测试的示例）
    // example_basic_usage();
    // example_breathing_effect();
    // example_rainbow_effect();
    // example_blink_effect();
    // example_chase_effect();
    // example_fade_effect();
    example_status_indicator();

    ESP_LOGI(TAG, "WS2811 examples completed");
    vTaskDelete(NULL);
}

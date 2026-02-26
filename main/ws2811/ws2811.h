#ifndef _WS2811_H_
#define _WS2811_H_

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief WS2811/WS2812 LED 控制库
 * 
 * 支持的 LED 型号：
 * - WS2811
 * - WS2812
 * - WS2812B
 * - SK6812
 * - APA106
 */

// LED 颜色结构体
typedef struct {
    uint8_t r;  // 红色 (0-255)
    uint8_t g;  // 绿色 (0-255)
    uint8_t b;  // 蓝色 (0-255)
} ws2811_color_t;

// LED 灯带句柄
typedef void* ws2811_handle_t;

// LED 效果类型
typedef enum {
    WS2811_EFFECT_STATIC,       // 静态颜色
    WS2811_EFFECT_BREATHING,    // 呼吸灯
    WS2811_EFFECT_RAINBOW,      // 彩虹流动
    WS2811_EFFECT_CHASE,        // 追逐效果
    WS2811_EFFECT_BLINK,        // 闪烁
    WS2811_EFFECT_FADE,         // 渐变
} ws2811_effect_t;

// LED 配置结构体
typedef struct {
    int gpio_num;               // GPIO 引脚号
    uint32_t led_count;         // LED 数量
    uint32_t max_brightness;    // 最大亮度 (0-255)
} ws2811_config_t;

/**
 * @brief 初始化 WS2811 LED 灯带
 * 
 * @param config LED 配置
 * @param handle 返回的 LED 句柄
 * @return esp_err_t 
 *         - ESP_OK: 成功
 *         - ESP_ERR_INVALID_ARG: 参数无效
 *         - ESP_ERR_NO_MEM: 内存不足
 */
esp_err_t ws2811_init(const ws2811_config_t *config, ws2811_handle_t *handle);

/**
 * @brief 释放 WS2811 LED 灯带资源
 * 
 * @param handle LED 句柄
 * @return esp_err_t 
 */
esp_err_t ws2811_deinit(ws2811_handle_t handle);

/**
 * @brief 设置单个 LED 的颜色
 * 
 * @param handle LED 句柄
 * @param index LED 索引 (0 开始)
 * @param color 颜色
 * @return esp_err_t 
 */
esp_err_t ws2811_set_pixel(ws2811_handle_t handle, uint32_t index, ws2811_color_t color);

/**
 * @brief 设置单个 LED 的颜色（RGB 分量）
 * 
 * @param handle LED 句柄
 * @param index LED 索引
 * @param r 红色 (0-255)
 * @param g 绿色 (0-255)
 * @param b 蓝色 (0-255)
 * @return esp_err_t 
 */
esp_err_t ws2811_set_pixel_rgb(ws2811_handle_t handle, uint32_t index, uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief 设置所有 LED 为相同颜色
 * 
 * @param handle LED 句柄
 * @param color 颜色
 * @return esp_err_t 
 */
esp_err_t ws2811_set_all(ws2811_handle_t handle, ws2811_color_t color);

/**
 * @brief 设置所有 LED 为相同颜色（RGB 分量）
 * 
 * @param handle LED 句柄
 * @param r 红色 (0-255)
 * @param g 绿色 (0-255)
 * @param b 蓝色 (0-255)
 * @return esp_err_t 
 */
esp_err_t ws2811_set_all_rgb(ws2811_handle_t handle, uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief 清除所有 LED（关闭）
 * 
 * @param handle LED 句柄
 * @return esp_err_t 
 */
esp_err_t ws2811_clear(ws2811_handle_t handle);

/**
 * @brief 刷新 LED 显示（将缓冲区数据发送到 LED）
 * 
 * @param handle LED 句柄
 * @return esp_err_t 
 */
esp_err_t ws2811_refresh(ws2811_handle_t handle);

/**
 * @brief 设置全局亮度
 * 
 * @param handle LED 句柄
 * @param brightness 亮度 (0-255)
 * @return esp_err_t 
 */
esp_err_t ws2811_set_brightness(ws2811_handle_t handle, uint8_t brightness);

/**
 * @brief 获取当前亮度
 * 
 * @param handle LED 句柄
 * @return uint8_t 当前亮度 (0-255)
 */
uint8_t ws2811_get_brightness(ws2811_handle_t handle);

// ============================================================================
// 预定义颜色
// ============================================================================

#define WS2811_COLOR_RED        ((ws2811_color_t){255, 0, 0})
#define WS2811_COLOR_GREEN      ((ws2811_color_t){0, 255, 0})
#define WS2811_COLOR_BLUE       ((ws2811_color_t){0, 0, 255})
#define WS2811_COLOR_WHITE      ((ws2811_color_t){255, 255, 255})
#define WS2811_COLOR_YELLOW     ((ws2811_color_t){255, 255, 0})
#define WS2811_COLOR_CYAN       ((ws2811_color_t){0, 255, 255})
#define WS2811_COLOR_MAGENTA    ((ws2811_color_t){255, 0, 255})
#define WS2811_COLOR_ORANGE     ((ws2811_color_t){255, 165, 0})
#define WS2811_COLOR_PURPLE     ((ws2811_color_t){128, 0, 128})
#define WS2811_COLOR_PINK       ((ws2811_color_t){255, 192, 203})
#define WS2811_COLOR_OFF        ((ws2811_color_t){0, 0, 0})

// ============================================================================
// 辅助函数
// ============================================================================

/**
 * @brief 从 HSV 转换为 RGB 颜色
 * 
 * @param h 色调 (0-360)
 * @param s 饱和度 (0-100)
 * @param v 明度 (0-100)
 * @return ws2811_color_t RGB 颜色
 */
ws2811_color_t ws2811_hsv_to_rgb(uint16_t h, uint8_t s, uint8_t v);

/**
 * @brief 创建颜色
 * 
 * @param r 红色 (0-255)
 * @param g 绿色 (0-255)
 * @param b 蓝色 (0-255)
 * @return ws2811_color_t 
 */
static inline ws2811_color_t ws2811_color(uint8_t r, uint8_t g, uint8_t b) {
    return (ws2811_color_t){r, g, b};
}

/**
 * @brief 调整颜色亮度
 * 
 * @param color 原始颜色
 * @param brightness 亮度 (0-255)
 * @return ws2811_color_t 调整后的颜色
 */
ws2811_color_t ws2811_dim_color(ws2811_color_t color, uint8_t brightness);

#ifdef __cplusplus
}
#endif

#endif // _WS2811_H_

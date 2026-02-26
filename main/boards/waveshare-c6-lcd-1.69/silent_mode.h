#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 天气类型枚举（供 MCP 调用使用）
 */
typedef enum {
    WEATHER_TYPE_CLOUDY = 0,  // 多云
    WEATHER_TYPE_DAY,         // 白天/晴天
    WEATHER_TYPE_NIGHT,       // 夜晚
    WEATHER_TYPE_RAINY,       // 雨天
    WEATHER_TYPE_SNOWY        // 雪天
} WeatherType;

/**
 * @brief 进入静默模式
 * @note NFC 卡片移除时调用此函数
 * @return true 如果成功进入或正在进入静默模式
 */
bool silent_mode_enter(void);

/**
 * @brief 退出静默模式
 * @note NFC 卡片放上时调用此函数
 * @return true 如果成功退出或正在退出静默模式
 */
bool silent_mode_exit(void);

/**
 * @brief 获取静默模式状态
 * @return true 如果在静默模式中（包括进入/退出过渡状态）
 */
bool silent_mode_get_state(void);

/**
 * @brief 设置天气状态（MCP 调用）
 * @param weather 天气类型
 * @note 调用后会存储天气状态，静默模式下会立即更新显示
 */
void weather_set_state(WeatherType weather);

/**
 * @brief 根据天气字符串设置天气状态
 * @param weather_str 天气字符串 ("cloudy", "sunny", "clear", "night", "rainy", "rain", "snowy", "snow")
 * @note 会自动匹配天气类型，未匹配则根据时间自动判断
 */
void weather_set_from_string(const char* weather_str);

#ifdef __cplusplus
}
#endif

#ifndef BACKGROUND_MANAGER_H
#define BACKGROUND_MANAGER_H

#include <stdint.h>
#include <stdbool.h>

// Background indices (must match order in gifs/index.json backgrounds array)
// Order: time(4) + weather(1) + festival(7) + style(4) = 16
enum BackgroundIndex {
    // Time (0-3)
    BG_TIME_DAY = 0,        // 白天
    BG_TIME_SUNSET = 1,     // 夕阳
    BG_TIME_SUNRISE = 2,    // 日出
    BG_TIME_NIGHT = 3,      // 黑夜

    // Weather (4)
    BG_WEATHER_RAINY = 4,   // 雨天

    // Festival (5-11)
    BG_FESTIVAL_CHRISTMAS = 5,    // 圣诞
    BG_FESTIVAL_BIRTHDAY = 6,     // 生日
    BG_FESTIVAL_SPRING = 7,       // 春节
    BG_FESTIVAL_NEWYEAR = 8,      // 元旦
    BG_FESTIVAL_MIDAUTUMN = 9,    // 中秋
    BG_FESTIVAL_HALLOWEEN = 10,   // 万圣节
    BG_FESTIVAL_VALENTINES = 11,  // 情人节

    // Style (12-15) - unlocked via achievements
    BG_STYLE_CYBERPUNK = 12,      // 赛博朋克
    BG_STYLE_STEAMPUNK = 13,      // 蒸汽朋克
    BG_STYLE_FANTASY = 14,        // 奇幻
    BG_STYLE_SPACE = 15,          // 太空舱

    BG_COUNT = 4  // Must match gifs/index.json backgrounds array length
};

// Weather conditions (simplified: only clear and rainy)
// MCP.weather outputs rainy/snowy -> WEATHER_RAINY
// Otherwise -> WEATHER_CLEAR (default)
enum WeatherCondition {
    WEATHER_CLEAR = 0,      // 晴天 (default)
    WEATHER_RAINY = 1       // 雨天 (includes rain/snow from MCP)
};

// Background manager class
class BackgroundManager {
public:
    static BackgroundManager& GetInstance();

    // Get the appropriate background based on current conditions
    // Returns background index (0-19)
    uint16_t GetCurrentBackground();

    // Update conditions (call periodically or on event)
    void UpdateTime(uint8_t hour, uint8_t minute, uint8_t month, uint8_t day, uint16_t year = 0);
    void UpdateWeather(WeatherCondition condition);
    void SetBirthday(uint8_t month, uint8_t day);

    // Force a specific background (for testing)
    void ForceBackground(uint16_t bg_idx);
    void ClearForce();

    // Get last selected background
    uint16_t GetLastBackground() const { return last_background_; }

private:
    BackgroundManager();

    // Check functions (in priority order)
    bool CheckSpecialRandom(uint16_t& bg_idx);   // 20% style backgrounds on time period change
    bool CheckFestival(uint16_t& bg_idx);        // Festival dates
    bool CheckWeather(uint16_t& bg_idx);         // Weather conditions
    uint16_t GetTimeBackground();                 // Time of day (always returns valid)

    // Get time period (0=sunrise, 1=day, 2=sunset, 3=night)
    uint8_t GetTimePeriod() const;

    // Current state
    uint8_t current_hour_;
    uint8_t current_minute_;
    uint8_t current_month_;
    uint8_t current_day_;
    uint16_t current_year_;
    WeatherCondition current_weather_;

    // User settings
    uint8_t birthday_month_;
    uint8_t birthday_day_;

    // Force mode
    bool force_enabled_;
    uint16_t forced_background_;

    // Last selected
    uint16_t last_background_;

    // Time period tracking for 20% style random trigger
    uint8_t last_time_period_;           // Last time period (0-3)
    uint16_t current_decided_bg_;        // Current decided background (may be style from 20% random)
    bool bg_decided_this_period_;        // Whether background was decided for current period
};

#endif // BACKGROUND_MANAGER_H

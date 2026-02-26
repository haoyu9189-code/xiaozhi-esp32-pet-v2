#include "background_manager.h"
#include "pet_achievements.h"
#include <esp_log.h>
#include <esp_random.h>
#include <time.h>
#include <vector>

static const char* TAG = "BackgroundManager";

// Lunar festival dates (2025-2035)
// Each festival displays for 3 days: day before, day of, and day after
struct LunarFestivalDate {
    uint16_t year;
    uint8_t month;
    uint8_t day;
};

// Spring Festival (Chinese New Year - 春节初一)
static const LunarFestivalDate kSpringFestival[] = {
    {2025, 1, 29},   // 2025年1月29日
    {2026, 2, 17},   // 2026年2月17日
    {2027, 2, 6},    // 2027年2月6日
    {2028, 1, 26},   // 2028年1月26日
    {2029, 2, 13},   // 2029年2月13日
    {2030, 2, 3},    // 2030年2月3日
    {2031, 1, 23},   // 2031年1月23日
    {2032, 2, 11},   // 2032年2月11日
    {2033, 1, 31},   // 2033年1月31日
    {2034, 2, 19},   // 2034年2月19日
    {2035, 2, 8},    // 2035年2月8日
};

// Mid-Autumn Festival (中秋节)
static const LunarFestivalDate kMidAutumn[] = {
    {2025, 10, 6},   // 2025年10月6日
    {2026, 9, 25},   // 2026年9月25日
    {2027, 9, 15},   // 2027年9月15日
    {2028, 10, 3},   // 2028年10月3日
    {2029, 9, 22},   // 2029年9月22日
    {2030, 9, 12},   // 2030年9月12日
    {2031, 10, 1},   // 2031年10月1日
    {2032, 9, 19},   // 2032年9月19日
    {2033, 9, 8},    // 2033年9月8日
    {2034, 9, 27},   // 2034年9月27日
    {2035, 9, 16},   // 2035年9月16日
};

// Helper: Check if current date is within ±1 day of festival date
static bool IsNearFestivalDate(uint16_t year, uint8_t month, uint8_t day,
                               const LunarFestivalDate* dates, size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (dates[i].year == year) {
            uint8_t festival_month = dates[i].month;
            uint8_t festival_day = dates[i].day;

            // Check if within ±1 day
            if (month == festival_month) {
                int day_diff = (int)day - (int)festival_day;
                if (day_diff >= -1 && day_diff <= 1) {
                    return true;
                }
            }
            // Handle cross-month cases (e.g., Jan 31 -> Feb 1)
            // Previous month: festival is on 1st, check if current is last day of prev month
            if (month == festival_month - 1 && festival_day == 1 && day >= 30) {
                return true;  // Dec 31 matches Jan 1 festival
            }
            // Next month: festival is on last days (28-31), check if current is 1st of next month
            if (month == festival_month + 1 && day == 1 && festival_day >= 28) {
                return true;  // Feb 1 matches Jan 31 festival
            }
            // Handle year boundary (Dec -> Jan)
            if (month == 1 && festival_month == 12 && festival_day >= 30 && day == 1) {
                return true;  // Jan 1 matches Dec 31 festival
            }
            if (month == 12 && festival_month == 1 && festival_day == 1 && day >= 30) {
                return true;  // Dec 31 matches Jan 1 festival
            }
            break;  // Found the year, no need to check further
        }
    }
    return false;
}

BackgroundManager& BackgroundManager::GetInstance() {
    static BackgroundManager instance;
    return instance;
}

BackgroundManager::BackgroundManager()
    : current_hour_(12)
    , current_minute_(0)
    , current_month_(1)
    , current_day_(1)
    , current_year_(2026)
    , current_weather_(WEATHER_CLEAR)
    , birthday_month_(0)
    , birthday_day_(0)
    , force_enabled_(false)
    , forced_background_(BG_TIME_DAY)
    , last_background_(BG_TIME_DAY)
    , last_time_period_(1)  // day period (12:00 default)
    , current_decided_bg_(BG_TIME_DAY)
    , bg_decided_this_period_(false)
{
    // Initialize from system time if available
    time_t now;
    struct tm timeinfo;
    if (time(&now) != -1 && localtime_r(&now, &timeinfo)) {
        current_hour_ = timeinfo.tm_hour;
        current_minute_ = timeinfo.tm_min;
        current_month_ = timeinfo.tm_mon + 1;
        current_day_ = timeinfo.tm_mday;
        current_year_ = timeinfo.tm_year + 1900;
        last_time_period_ = GetTimePeriod();
    }
}

void BackgroundManager::UpdateTime(uint8_t hour, uint8_t minute, uint8_t month, uint8_t day, uint16_t year) {
    current_hour_ = hour;
    current_minute_ = minute;
    current_month_ = month;
    current_day_ = day;
    if (year > 0) {
        current_year_ = year;
    }
}

void BackgroundManager::UpdateWeather(WeatherCondition condition) {
    if (current_weather_ != condition) {
        ESP_LOGI(TAG, "Weather changed: %d -> %d", current_weather_, condition);
        current_weather_ = condition;
    }
}

void BackgroundManager::SetBirthday(uint8_t month, uint8_t day) {
    birthday_month_ = month;
    birthday_day_ = day;
    ESP_LOGI(TAG, "Birthday set: %d/%d", month, day);
}

void BackgroundManager::ForceBackground(uint16_t bg_idx) {
    if (bg_idx < BG_COUNT) {
        force_enabled_ = true;
        forced_background_ = bg_idx;
        ESP_LOGI(TAG, "Force background: %d", bg_idx);
    }
}

void BackgroundManager::ClearForce() {
    force_enabled_ = false;
    bg_decided_this_period_ = false;
    last_time_period_ = 0xFF;  // Force time period re-evaluation
    ESP_LOGI(TAG, "Force cleared, will re-decide background");
}

uint16_t BackgroundManager::GetCurrentBackground() {
    // Priority 1: Force mode (MCP override)
    if (force_enabled_) {
        last_background_ = forced_background_;
        return forced_background_;
    }

    uint16_t bg_idx;

    // Priority 2: Festival background (全天显示，highest priority after force)
    if (CheckFestival(bg_idx)) {
        ESP_LOGI(TAG, "Festival background: %d (month=%d, day=%d)",
                 bg_idx, current_month_, current_day_);
        last_background_ = bg_idx;
        return bg_idx;
    }

    // Priority 3: Weather background (独立背景，replaces all)
    if (CheckWeather(bg_idx)) {
        ESP_LOGI(TAG, "Weather background: %d (weather=%d)",
                 bg_idx, current_weather_);
        last_background_ = bg_idx;
        return bg_idx;
    }

    // Priority 4: Time-based background (time or style)
    // Check if time period changed (5:00, 8:00, 17:00, 19:00)
    uint8_t current_period = GetTimePeriod();
    if (current_period != last_time_period_) {
        // Time period changed, decide new background
        ESP_LOGI(TAG, "Time period changed: %d -> %d (hour=%d)",
                 last_time_period_, current_period, current_hour_);
        last_time_period_ = current_period;
        bg_decided_this_period_ = false;

        // 20% chance for unlocked style backgrounds
        if (CheckSpecialRandom(bg_idx)) {
            ESP_LOGI(TAG, "Style background selected (20%% random): %d", bg_idx);
            current_decided_bg_ = bg_idx;
            bg_decided_this_period_ = true;
            last_background_ = bg_idx;
            return bg_idx;
        }

        // 80% chance: use time background
        uint16_t time_bg = GetTimeBackground();
        ESP_LOGI(TAG, "Time background selected: %d (hour=%d)", time_bg, current_hour_);
        current_decided_bg_ = time_bg;
        bg_decided_this_period_ = true;
        last_background_ = time_bg;
        return time_bg;
    }

    // Time period not changed, return current decided background
    last_background_ = current_decided_bg_;
    return current_decided_bg_;
}

bool BackgroundManager::CheckSpecialRandom(uint16_t& bg_idx) {
    // Build list of unlocked style backgrounds
    auto& achievements = PetAchievements::GetInstance();
    std::vector<uint16_t> unlocked_styles;

    if (achievements.IsCyberpunkUnlocked()) {
        unlocked_styles.push_back(BG_STYLE_CYBERPUNK);
    }
    if (achievements.IsFantasyUnlocked()) {
        unlocked_styles.push_back(BG_STYLE_FANTASY);
    }
    if (achievements.IsSpaceUnlocked()) {
        unlocked_styles.push_back(BG_STYLE_SPACE);
    }
    if (achievements.IsSteampunkUnlocked()) {
        unlocked_styles.push_back(BG_STYLE_STEAMPUNK);
    }

    // No unlocked styles, skip special random
    if (unlocked_styles.empty()) {
        return false;
    }

    // 20% chance (20 in 100) for special style backgrounds
    uint32_t rand = esp_random() % 100;
    if (rand < 20) {  // 0-19 out of 0-99 = 20%
        // Randomly select one of the unlocked styles
        bg_idx = unlocked_styles[esp_random() % unlocked_styles.size()];
        ESP_LOGI(TAG, "Special style background selected: %d (from %d unlocked)", bg_idx, unlocked_styles.size());
        return true;
    }
    return false;
}

bool BackgroundManager::CheckFestival(uint16_t& bg_idx) {
    auto& achievements = PetAchievements::GetInstance();

    // Birthday (user-configured) - requires unlock
    if (birthday_month_ > 0 && birthday_day_ > 0) {
        if (current_month_ == birthday_month_ && current_day_ == birthday_day_) {
            if (achievements.IsBirthdayUnlocked()) {
                bg_idx = BG_FESTIVAL_BIRTHDAY;
                return true;
            }
        }
    }

    // Lunar festivals (use lookup table, display for 3 days: ±1 day)
    // Spring Festival (春节)
    if (IsNearFestivalDate(current_year_, current_month_, current_day_,
                          kSpringFestival, sizeof(kSpringFestival) / sizeof(kSpringFestival[0]))) {
        if (achievements.IsSpringFestivalUnlocked()) {
            bg_idx = BG_FESTIVAL_SPRING;
            ESP_LOGI(TAG, "Spring Festival detected (year=%d, month=%d, day=%d)",
                     current_year_, current_month_, current_day_);
            return true;
        }
    }

    // Mid-Autumn Festival (中秋)
    if (IsNearFestivalDate(current_year_, current_month_, current_day_,
                          kMidAutumn, sizeof(kMidAutumn) / sizeof(kMidAutumn[0]))) {
        if (achievements.IsMidAutumnUnlocked()) {
            bg_idx = BG_FESTIVAL_MIDAUTUMN;
            ESP_LOGI(TAG, "Mid-Autumn Festival detected (year=%d, month=%d, day=%d)",
                     current_year_, current_month_, current_day_);
            return true;
        }
    }

    // Fixed date festivals (solar calendar)
    // 元旦 (January 1st, single day)
    if (current_month_ == 1 && current_day_ == 1) {
        if (achievements.IsNewYearUnlocked()) {
            bg_idx = BG_FESTIVAL_NEWYEAR;
            return true;
        }
    }

    // 情人节 (February 14th, single day)
    if (current_month_ == 2 && current_day_ == 14) {
        if (achievements.IsValentinesUnlocked()) {
            bg_idx = BG_FESTIVAL_VALENTINES;
            return true;
        }
    }

    // 万圣节 (October 31st, single day)
    if (current_month_ == 10 && current_day_ == 31) {
        if (achievements.IsHalloweenUnlocked()) {
            bg_idx = BG_FESTIVAL_HALLOWEEN;
            return true;
        }
    }

    // 圣诞节 (December 24-25, 2 days)
    if (current_month_ == 12 && (current_day_ == 24 || current_day_ == 25)) {
        if (achievements.IsChristmasUnlocked()) {
            bg_idx = BG_FESTIVAL_CHRISTMAS;
            return true;
        }
    }

    return false;
}

bool BackgroundManager::CheckWeather(uint16_t& bg_idx) {
    // Only WEATHER_RAINY triggers weather background
    // WEATHER_CLEAR (default) falls through to time background
    if (current_weather_ == WEATHER_RAINY) {
        bg_idx = BG_WEATHER_RAINY;
        return true;
    }
    return false;
}

uint8_t BackgroundManager::GetTimePeriod() const {
    // Time periods (0-3):
    // 0: 05:00 - 07:59 -> sunrise
    // 1: 08:00 - 16:59 -> day
    // 2: 17:00 - 18:59 -> sunset
    // 3: 19:00 - 04:59 -> night

    if (current_hour_ >= 5 && current_hour_ < 8) {
        return 0;  // sunrise
    } else if (current_hour_ >= 8 && current_hour_ < 17) {
        return 1;  // day
    } else if (current_hour_ >= 17 && current_hour_ < 19) {
        return 2;  // sunset
    } else {
        return 3;  // night
    }
}

uint16_t BackgroundManager::GetTimeBackground() {
    // Time periods:
    // 05:00 - 07:59 -> sunrise (日出)
    // 08:00 - 16:59 -> day (白天)
    // 17:00 - 18:59 -> sunset (夕阳)
    // 19:00 - 04:59 -> night (黑夜)

    if (current_hour_ >= 5 && current_hour_ < 8) {
        return BG_TIME_SUNRISE;
    } else if (current_hour_ >= 8 && current_hour_ < 17) {
        return BG_TIME_DAY;
    } else if (current_hour_ >= 17 && current_hour_ < 19) {
        return BG_TIME_SUNSET;
    } else {
        return BG_TIME_NIGHT;
    }
}

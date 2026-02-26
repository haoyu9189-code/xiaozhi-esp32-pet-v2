#include "ambient_dialogue.h"
#include "dialogue_texts.h"
#include "pet_event_log.h"
#include "application.h"
#include "board.h"
#include "display.h"
#include "pet_coin.h"
#include <esp_log.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <time.h>
#include <string.h>

#define TAG "AmbientDialogue"

// 触发概率（百分比）
#define EVENT_TRIGGER_CHANCE 50     // 事件触发概率50%
#define TIME_GREETING_CHANCE 30     // 时间问候每分钟触发概率（平均每小时2次）
#define MOOD_MUMBLE_CHANCE 20       // 心情自言自语每分钟触发概率（平均每小时1次）

// 冷却时间（分钟）
#define EVENT_COOLDOWN_MINUTES 5    // 同类事件冷却时间
#define TIME_GREETING_COOLDOWN 60   // 时间问候冷却1小时
#define MOOD_COOLDOWN 30            // 心情自言自语冷却30分钟
#define FESTIVAL_COOLDOWN 180       // 节日祝福冷却3小时

// 文本长度阈值（字符数）
#define SHORT_TEXT_THRESHOLD 15     // 短文本阈值
#define SHORT_TEXT_DURATION 3000    // 短文本显示3秒
#define LONG_TEXT_DURATION 5000     // 长文本显示5秒

AmbientDialogue& AmbientDialogue::GetInstance() {
    static AmbientDialogue instance;
    return instance;
}

void AmbientDialogue::Initialize() {
    ESP_LOGI(TAG, "Initializing ambient dialogue system");

    // 初始化冷却时间数组
    for (int i = 0; i < 20; i++) {
        last_trigger_time_[i] = 0;
    }

    tick_counter_ = 0;
    last_greeting_hour_ = 0xFF;
    last_festival_day_ = 0;
}

void AmbientDialogue::Tick() {
    tick_counter_++;

    // 语音交互中不显示环境对白（避免干扰）
    if (PetStateMachine::GetInstance().IsInVoiceInteraction()) {
        return;
    }

    // 每分钟随机检查时间问候
    if ((esp_random() % 100) < TIME_GREETING_CHANCE) {
        CheckTimeGreeting();
    }

    // 每分钟随机检查心情自言自语
    if ((esp_random() % 100) < MOOD_MUMBLE_CHANCE) {
        CheckMoodMumble();
    }

    // 每10分钟检查一次节日
    if ((tick_counter_ % 10) == 0) {
        CheckFestivalGreeting();
    }
}

void AmbientDialogue::TriggerEvent(DialogueEvent event, bool force) {
    bool in_voice = PetStateMachine::GetInstance().IsInVoiceInteraction();

    // 检查是否应该触发（语音中也要做概率检查以决定是否记录事件）
    if (!force && !ShouldTrigger(event)) {
        return;
    }

    // 检查冷却
    if (IsInCooldown(event)) {
        ESP_LOGD(TAG, "Event %d in cooldown, skipping", (int)event);
        return;
    }

    const char* text = nullptr;
    int count = 0;

    // 根据事件类型选择文本
    switch (event) {
        case DialogueEvent::kCoinAppear:
            count = DialogueTexts::kCoinAppearCount;
            text = DialogueTexts::kCoinAppear[esp_random() % count];
            break;

        case DialogueEvent::kCoinPickup:
            count = DialogueTexts::kCoinPickupCount;
            text = DialogueTexts::kCoinPickup[esp_random() % count];
            break;

        case DialogueEvent::kPoopAppear:
            count = DialogueTexts::kPoopAppearCount;
            text = DialogueTexts::kPoopAppear[esp_random() % count];
            break;

        case DialogueEvent::kPoopStep:
            count = DialogueTexts::kPoopStepCount;
            text = DialogueTexts::kPoopStep[esp_random() % count];
            break;

        case DialogueEvent::kStartEating:
            count = DialogueTexts::kStartEatingCount;
            text = DialogueTexts::kStartEating[esp_random() % count];
            break;

        case DialogueEvent::kFullEating:
            count = DialogueTexts::kFullEatingCount;
            text = DialogueTexts::kFullEating[esp_random() % count];
            break;

        case DialogueEvent::kStartBathing:
            count = DialogueTexts::kStartBathingCount;
            text = DialogueTexts::kStartBathing[esp_random() % count];
            break;

        case DialogueEvent::kFullBathing:
            count = DialogueTexts::kFullBathingCount;
            text = DialogueTexts::kFullBathing[esp_random() % count];
            break;

        default:
            ESP_LOGW(TAG, "Unknown event type: %d", (int)event);
            return;
    }

    if (text) {
        // 记录到事件日志（无论是否显示）
        PetEventType log_type = PetEventType::kAmbientDialogue;
        switch (event) {
            case DialogueEvent::kCoinAppear:  log_type = PetEventType::kCoinSpawned; break;
            case DialogueEvent::kCoinPickup:  log_type = PetEventType::kCoinPickup; break;
            case DialogueEvent::kPoopAppear:  log_type = PetEventType::kPoopSpawned; break;
            case DialogueEvent::kPoopStep:    log_type = PetEventType::kPoopStep; break;
            case DialogueEvent::kStartEating: log_type = PetEventType::kStartEating; break;
            case DialogueEvent::kFullEating:  log_type = PetEventType::kFullEating; break;
            case DialogueEvent::kStartBathing: log_type = PetEventType::kStartBathing; break;
            case DialogueEvent::kFullBathing: log_type = PetEventType::kFullBathing; break;
            default: break;
        }
        PetEventLog::GetInstance().Log(log_type, text);

        // 语音交互中不显示环境对白，但事件已记录
        if (in_voice) {
            ESP_LOGD(TAG, "In voice interaction, suppressing display for event %d", (int)event);
            return;
        }

        ShowDialogue(text);
        UpdateCooldown(event);
    }
}

void AmbientDialogue::ShowDialogue(const char* text) {
    if (!text || strlen(text) == 0) {
        return;
    }

    ESP_LOGI(TAG, "Showing dialogue: %s", text);

    // 计算显示时长
    int duration_ms = GetDisplayDuration(text);

    // 显示对白（使用字幕系统）
    auto display = Board::GetInstance().GetDisplay();
    display->SetChatMessage("system", text);

    // 设置定时器清除字幕
    static esp_timer_handle_t clear_timer = nullptr;
    if (clear_timer == nullptr) {
        esp_timer_create_args_t timer_args = {
            .callback = [](void* arg) {
                Board::GetInstance().GetDisplay()->SetChatMessage("system", "");
            },
            .arg = nullptr,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "dialogue_clear",
        };
        esp_timer_create(&timer_args, &clear_timer);
    }

    // 停止旧定时器，启动新定时器
    esp_timer_stop(clear_timer);
    esp_timer_start_once(clear_timer, duration_ms * 1000ULL);
}

int AmbientDialogue::GetDisplayDuration(const char* text) {
    if (!text) {
        return SHORT_TEXT_DURATION;
    }

    // 统计UTF-8字符数（中文占3字节）
    int char_count = 0;
    const char* p = text;
    while (*p) {
        if ((*p & 0x80) == 0) {
            // ASCII字符
            char_count++;
            p++;
        } else if ((*p & 0xE0) == 0xC0) {
            // 2字节UTF-8
            char_count++;
            p += 2;
        } else if ((*p & 0xF0) == 0xE0) {
            // 3字节UTF-8（中文）
            char_count++;
            p += 3;
        } else if ((*p & 0xF8) == 0xF0) {
            // 4字节UTF-8
            char_count++;
            p += 4;
        } else {
            p++;
        }
    }

    // 根据字符数决定显示时长
    return (char_count <= SHORT_TEXT_THRESHOLD) ? SHORT_TEXT_DURATION : LONG_TEXT_DURATION;
}

bool AmbientDialogue::ShouldTrigger(DialogueEvent event) {
    // 随机概率检查
    return (esp_random() % 100) < EVENT_TRIGGER_CHANCE;
}

bool AmbientDialogue::IsInCooldown(DialogueEvent event) {
    int event_index = (int)event;
    if (event_index < 0 || event_index >= 20) {
        return false;
    }

    uint32_t current_time = esp_timer_get_time() / 1000 / 1000 / 60;  // 分钟
    uint32_t elapsed = current_time - last_trigger_time_[event_index];

    // 不同事件类型使用不同冷却时间
    uint32_t cooldown = EVENT_COOLDOWN_MINUTES;
    if (event == DialogueEvent::kTimeGreeting) {
        cooldown = TIME_GREETING_COOLDOWN;
    } else if (event == DialogueEvent::kMoodMumble) {
        cooldown = MOOD_COOLDOWN;
    } else if (event == DialogueEvent::kFestivalGreeting) {
        cooldown = FESTIVAL_COOLDOWN;
    }

    return elapsed < cooldown;
}

void AmbientDialogue::UpdateCooldown(DialogueEvent event) {
    int event_index = (int)event;
    if (event_index >= 0 && event_index < 20) {
        last_trigger_time_[event_index] = esp_timer_get_time() / 1000 / 1000 / 60;  // 分钟
    }
}

void AmbientDialogue::CheckTimeGreeting() {
    // 检查冷却
    if (IsInCooldown(DialogueEvent::kTimeGreeting)) {
        return;
    }

    // 获取当前时间
    time_t now;
    struct tm timeinfo;
    if (time(&now) == -1 || localtime_r(&now, &timeinfo) == nullptr) {
        return;
    }

    uint8_t hour = timeinfo.tm_hour;

    // 如果还是同一个小时，不重复问候
    if (hour == last_greeting_hour_) {
        return;
    }

    // 获取时间段
    int period = GetTimePeriod();

    const char* text = nullptr;
    int count = 0;

    switch (period) {
        case 1:  // 早上
            count = DialogueTexts::kMorningGreetingCount;
            text = DialogueTexts::kMorningGreeting[esp_random() % count];
            break;
        case 2:  // 下午
            count = DialogueTexts::kAfternoonGreetingCount;
            text = DialogueTexts::kAfternoonGreeting[esp_random() % count];
            break;
        case 3:  // 傍晚
            count = DialogueTexts::kEveningGreetingCount;
            text = DialogueTexts::kEveningGreeting[esp_random() % count];
            break;
        case 0:  // 夜晚
            count = DialogueTexts::kNightGreetingCount;
            text = DialogueTexts::kNightGreeting[esp_random() % count];
            break;
    }

    if (text) {
        PetEventLog::GetInstance().Log(PetEventType::kAmbientDialogue, text);
        ShowDialogue(text);
        UpdateCooldown(DialogueEvent::kTimeGreeting);
        last_greeting_hour_ = hour;
    }
}

void AmbientDialogue::CheckMoodMumble() {
    // 检查冷却
    if (IsInCooldown(DialogueEvent::kMoodMumble)) {
        return;
    }

    // 获取宠物状态
    auto& pet = PetStateMachine::GetInstance();
    const auto& stats = pet.GetStats();

    const char* text = nullptr;
    int count = 0;

    // 优先级：负面情绪 > 正面情绪
    if (stats.hunger < 30) {
        // 饥饿
        count = DialogueTexts::kHungryCount;
        text = DialogueTexts::kHungry[esp_random() % count];
    } else if (stats.cleanliness < 30) {
        // 很脏
        count = DialogueTexts::kDirtyCount;
        text = DialogueTexts::kDirty[esp_random() % count];
    } else if (stats.happiness < 30) {
        // 心情不好
        count = DialogueTexts::kUnhappyCount;
        text = DialogueTexts::kUnhappy[esp_random() % count];
    } else if (stats.hunger >= 60 && stats.cleanliness >= 60 && stats.happiness >= 60) {
        // 状态良好
        count = DialogueTexts::kFeelGoodCount;
        text = DialogueTexts::kFeelGood[esp_random() % count];
    } else if (stats.happiness >= 80) {
        // 心情很好
        count = DialogueTexts::kHappyCount;
        text = DialogueTexts::kHappy[esp_random() % count];
    }

    if (text) {
        PetEventLog::GetInstance().Log(PetEventType::kAmbientDialogue, text);
        ShowDialogue(text);
        UpdateCooldown(DialogueEvent::kMoodMumble);
    }
}

void AmbientDialogue::CheckFestivalGreeting() {
    // 检查冷却
    if (IsInCooldown(DialogueEvent::kFestivalGreeting)) {
        return;
    }

    // 获取当前日期
    time_t now;
    struct tm timeinfo;
    if (time(&now) == -1 || localtime_r(&now, &timeinfo) == nullptr) {
        return;
    }

    uint16_t day_of_year = timeinfo.tm_yday + 1;

    // 如果还是同一天，不重复祝福
    if (day_of_year == last_festival_day_) {
        return;
    }

    // 检查节日
    int festival_id = CheckFestival();
    if (festival_id < 0) {
        return;  // 不是节日
    }

    const char* text = nullptr;
    int count = 0;

    // 根据月份和日期判断节日
    int month = timeinfo.tm_mon + 1;
    int day = timeinfo.tm_mday;

    if (month == 1 && day == 1) {
        // 元旦
        count = DialogueTexts::kNewYearCount;
        text = DialogueTexts::kNewYear[esp_random() % count];
    } else if (month == 2 && day == 14) {
        // 情人节
        count = DialogueTexts::kValentinesDayCount;
        text = DialogueTexts::kValentinesDay[esp_random() % count];
    } else if (month >= 1 && month <= 2 && day >= 21) {
        // 春节（粗略判断：1月21日-2月20日）
        count = DialogueTexts::kSpringFestivalCount;
        text = DialogueTexts::kSpringFestival[esp_random() % count];
    } else if (month == 4 && day >= 4 && day <= 6) {
        // 清明节
        count = DialogueTexts::kQingmingFestivalCount;
        text = DialogueTexts::kQingmingFestival[esp_random() % count];
    } else if (month == 5 && day == 1) {
        // 劳动节
        count = DialogueTexts::kLaborDayCount;
        text = DialogueTexts::kLaborDay[esp_random() % count];
    } else if (month == 6 && day == 1) {
        // 儿童节
        count = DialogueTexts::kChildrensDayCount;
        text = DialogueTexts::kChildrensDay[esp_random() % count];
    } else if (month >= 6 && month <= 7) {
        // 端午节（粗略判断）
        count = DialogueTexts::kDragonBoatFestivalCount;
        text = DialogueTexts::kDragonBoatFestival[esp_random() % count];
    } else if (month >= 9 && month <= 10 && day >= 1 && day <= 15) {
        // 中秋节（粗略判断）
        count = DialogueTexts::kMidAutumnFestivalCount;
        text = DialogueTexts::kMidAutumnFestival[esp_random() % count];
    } else if (month == 10 && day == 1) {
        // 国庆节
        count = DialogueTexts::kNationalDayCount;
        text = DialogueTexts::kNationalDay[esp_random() % count];
    } else if (month == 10 && day == 31) {
        // 万圣节
        count = DialogueTexts::kHalloweenCount;
        text = DialogueTexts::kHalloween[esp_random() % count];
    } else if (month == 12 && day == 25) {
        // 圣诞节
        count = DialogueTexts::kChristmasCount;
        text = DialogueTexts::kChristmas[esp_random() % count];
    }

    if (text) {
        PetEventLog::GetInstance().Log(PetEventType::kAmbientDialogue, text);
        ShowDialogue(text);
        UpdateCooldown(DialogueEvent::kFestivalGreeting);
        last_festival_day_ = day_of_year;
    }
}

int AmbientDialogue::GetTimePeriod() {
    time_t now;
    struct tm timeinfo;
    if (time(&now) == -1 || localtime_r(&now, &timeinfo) == nullptr) {
        return 0;
    }

    int hour = timeinfo.tm_hour;

    if (hour >= 6 && hour < 12) {
        return 1;  // 早上
    } else if (hour >= 12 && hour < 18) {
        return 2;  // 下午
    } else if (hour >= 18 && hour < 21) {
        return 3;  // 傍晚
    } else {
        return 0;  // 夜晚
    }
}

int AmbientDialogue::CheckFestival() {
    time_t now;
    struct tm timeinfo;
    if (time(&now) == -1 || localtime_r(&now, &timeinfo) == nullptr) {
        return -1;
    }

    int month = timeinfo.tm_mon + 1;
    int day = timeinfo.tm_mday;

    // 简单检查是否是节日（返回节日ID）
    if (month == 1 && day == 1) return 0;         // 元旦
    if (month == 2 && day == 14) return 1;        // 情人节
    if (month >= 1 && month <= 2 && day >= 21) return 2;  // 春节
    if (month == 4 && day >= 4 && day <= 6) return 3;     // 清明节
    if (month == 5 && day == 1) return 4;         // 劳动节
    if (month == 6 && day == 1) return 5;         // 儿童节
    if (month >= 6 && month <= 7) return 6;       // 端午节
    if (month >= 9 && month <= 10 && day >= 1 && day <= 15) return 7;  // 中秋节
    if (month == 10 && day == 1) return 8;        // 国庆节
    if (month == 10 && day == 31) return 9;       // 万圣节
    if (month == 12 && day == 25) return 10;      // 圣诞节

    return -1;  // 不是节日
}

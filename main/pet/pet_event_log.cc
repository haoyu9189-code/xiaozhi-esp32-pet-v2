#include "pet_event_log.h"
#include <esp_timer.h>
#include <esp_log.h>
#include <cstring>
#include <cstdio>

#define TAG "PetEventLog"

PetEventLog& PetEventLog::GetInstance() {
    static PetEventLog instance;
    return instance;
}

void PetEventLog::Log(PetEventType type, const char* description) {
    auto& event = events_[head_];
    event.type = type;
    event.timestamp_ms = esp_timer_get_time() / 1000;
    strncpy(event.description, description ? description : "", sizeof(event.description) - 1);
    event.description[sizeof(event.description) - 1] = '\0';

    head_ = (head_ + 1) % kMaxEvents;
    if (count_ < kMaxEvents) {
        count_++;
    }

    ESP_LOGD(TAG, "Event logged: type=%d, desc=%s", (int)type, description);
}

std::string PetEventLog::GetRecentEventsText(int max_events) const {
    if (count_ == 0) {
        return "";
    }

    std::string result = "\n【最近发生的事】\n";
    int num = (max_events < count_) ? max_events : count_;

    for (int i = 0; i < num; i++) {
        // 从最新的事件开始往回读
        int idx = (head_ - 1 - i + kMaxEvents) % kMaxEvents;
        const auto& event = events_[idx];

        int mins = MinutesAgo(event.timestamp_ms);
        char line[128];
        if (mins < 1) {
            snprintf(line, sizeof(line), "- 刚才：%s\n", event.description);
        } else if (mins < 60) {
            snprintf(line, sizeof(line), "- %d分钟前：%s\n", mins, event.description);
        } else {
            snprintf(line, sizeof(line), "- %d小时前：%s\n", mins / 60, event.description);
        }
        result += line;
    }

    return result;
}

std::string PetEventLog::GetRecentEventsJson(int max_events) const {
    if (count_ == 0) {
        return "[]";
    }

    std::string result = "[";
    int num = (max_events < count_) ? max_events : count_;

    for (int i = 0; i < num; i++) {
        int idx = (head_ - 1 - i + kMaxEvents) % kMaxEvents;
        const auto& event = events_[idx];

        int mins = MinutesAgo(event.timestamp_ms);

        char entry[192];
        snprintf(entry, sizeof(entry),
                 "%s{\"type\":\"%s\",\"minutes_ago\":%d,\"description\":\"%s\"}",
                 (i > 0) ? "," : "",
                 EventTypeName(event.type),
                 mins,
                 event.description);
        result += entry;
    }

    result += "]";
    return result;
}

const char* PetEventLog::EventTypeName(PetEventType type) {
    switch (type) {
        case PetEventType::kAmbientDialogue: return "ambient_dialogue";
        case PetEventType::kCoinSpawned:     return "coin_spawned";
        case PetEventType::kCoinPickup:      return "coin_pickup";
        case PetEventType::kPoopSpawned:     return "poop_spawned";
        case PetEventType::kPoopStep:        return "poop_step";
        case PetEventType::kStartEating:     return "start_eating";
        case PetEventType::kFullEating:      return "full_eating";
        case PetEventType::kStartBathing:    return "start_bathing";
        case PetEventType::kFullBathing:     return "full_bathing";
        case PetEventType::kAutoFeed:        return "auto_feed";
        case PetEventType::kAutoBathe:       return "auto_bathe";
        default: return "unknown";
    }
}

int PetEventLog::MinutesAgo(int64_t timestamp_ms) {
    int64_t now = esp_timer_get_time() / 1000;
    if (now < timestamp_ms) return 0;
    return static_cast<int>((now - timestamp_ms) / 60000);
}

#include "schedule_manager.h"
#include <esp_log.h>
#include <cstring>
#include <ctime>
#include <algorithm>

#define TAG "ScheduleMgr"

static const char* NVS_NAMESPACE = "schedule";
static const char* KEY_SCHEDULES = "items";
static const char* KEY_NEXT_ID = "next_id";

ScheduleManager& ScheduleManager::GetInstance() {
    static ScheduleManager instance;
    return instance;
}

ScheduleManager::~ScheduleManager() {
    if (dirty_) {
        SaveToNvs();
    }
    if (nvs_handle_) {
        nvs_close(nvs_handle_);
    }
}

bool ScheduleManager::Init() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (initialized_) {
        return true;
    }

    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return false;
    }

    LoadFromNvs();
    initialized_ = true;

    ESP_LOGI(TAG, "Initialized with %d schedules", (int)schedules_.size());
    return true;
}

void ScheduleManager::LoadFromNvs() {
    // Load next ID
    nvs_get_u32(nvs_handle_, KEY_NEXT_ID, &next_id_);
    if (next_id_ == 0) next_id_ = 1;

    // Load schedules
    size_t size = 0;
    esp_err_t err = nvs_get_blob(nvs_handle_, KEY_SCHEDULES, nullptr, &size);

    if (err == ESP_ERR_NVS_NOT_FOUND || size == 0) {
        ESP_LOGI(TAG, "No schedules found in NVS");
        return;
    }

    int count = size / sizeof(ScheduleItem);
    std::vector<ScheduleItem> items(count);
    err = nvs_get_blob(nvs_handle_, KEY_SCHEDULES, items.data(), &size);

    if (err == ESP_OK) {
        schedules_.clear();
        for (const auto& item : items) {
            if (memcmp(item.magic, "XZSC", 4) == 0 && item.enabled) {
                schedules_.push_back(item);
            }
        }
        ESP_LOGI(TAG, "Loaded %d schedules from NVS", (int)schedules_.size());
    }
}

void ScheduleManager::SaveToNvs() {
    if (!nvs_handle_) return;

    nvs_set_u32(nvs_handle_, KEY_NEXT_ID, next_id_);

    if (!schedules_.empty()) {
        nvs_set_blob(nvs_handle_, KEY_SCHEDULES, schedules_.data(),
                     schedules_.size() * sizeof(ScheduleItem));
    } else {
        nvs_erase_key(nvs_handle_, KEY_SCHEDULES);
    }

    nvs_commit(nvs_handle_);
    dirty_ = false;
    ESP_LOGI(TAG, "Saved %d schedules to NVS", (int)schedules_.size());
}

void ScheduleManager::Save() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (dirty_) {
        SaveToNvs();
    }
}

uint32_t ScheduleManager::ParseTimeString(const char* time_str) {
    if (!time_str || strlen(time_str) == 0) {
        return 0;
    }

    time_t now = time(nullptr);
    struct tm tm_now;
    localtime_r(&now, &tm_now);

    struct tm tm_target = tm_now;
    tm_target.tm_sec = 0;

    int hour = 0, minute = 0;
    int year = 0, month = 0, day = 0;

    // Try "YYYY-MM-DD HH:MM"
    if (sscanf(time_str, "%d-%d-%d %d:%d", &year, &month, &day, &hour, &minute) == 5) {
        tm_target.tm_year = year - 1900;
        tm_target.tm_mon = month - 1;
        tm_target.tm_mday = day;
        tm_target.tm_hour = hour;
        tm_target.tm_min = minute;
    }
    // Try "MM-DD HH:MM"
    else if (sscanf(time_str, "%d-%d %d:%d", &month, &day, &hour, &minute) == 4) {
        tm_target.tm_mon = month - 1;
        tm_target.tm_mday = day;
        tm_target.tm_hour = hour;
        tm_target.tm_min = minute;
        // If the date has passed this year, set to next year
        if (mktime(&tm_target) < now) {
            tm_target.tm_year++;
        }
    }
    // Try "HH:MM" (today or tomorrow)
    else if (sscanf(time_str, "%d:%d", &hour, &minute) == 2) {
        tm_target.tm_hour = hour;
        tm_target.tm_min = minute;
        // If time has passed today, set to tomorrow
        if (mktime(&tm_target) < now) {
            tm_target.tm_mday++;
        }
    }
    else {
        ESP_LOGW(TAG, "Invalid time format: %s", time_str);
        return 0;
    }

    return mktime(&tm_target);
}

uint32_t ScheduleManager::AddSchedule(const char* time_str, const char* content,
                                       const char* repeat) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_) {
        ESP_LOGW(TAG, "Not initialized");
        return 0;
    }

    if (schedules_.size() >= MAX_SCHEDULES) {
        ESP_LOGW(TAG, "Schedule limit reached");
        return 0;
    }

    uint32_t trigger_time = ParseTimeString(time_str);
    if (trigger_time == 0) {
        return 0;
    }

    ScheduleItem item;
    memcpy(item.magic, "XZSC", 4);
    item.id = next_id_++;
    item.trigger_time = trigger_time;
    strncpy(item.content, content, sizeof(item.content) - 1);
    item.content[sizeof(item.content) - 1] = '\0';
    strncpy(item.repeat_type, repeat ? repeat : "none", sizeof(item.repeat_type) - 1);
    item.repeat_type[sizeof(item.repeat_type) - 1] = '\0';
    item.triggered = 0;
    item.enabled = 1;
    memset(item.reserved, 0, sizeof(item.reserved));

    schedules_.push_back(item);
    dirty_ = true;

    // Format time for logging
    struct tm* tm_info = localtime((time_t*)&trigger_time);
    char time_buf[32];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M", tm_info);

    ESP_LOGI(TAG, "Added schedule #%lu: '%s' at %s (%s)",
             (unsigned long)item.id, content, time_buf, repeat);

    return item.id;
}

bool ScheduleManager::RemoveSchedule(uint32_t id) {
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto it = schedules_.begin(); it != schedules_.end(); ++it) {
        if (it->id == id) {
            ESP_LOGI(TAG, "Removed schedule #%lu: '%s'", (unsigned long)id, it->content);
            schedules_.erase(it);
            dirty_ = true;
            return true;
        }
    }

    return false;
}

int ScheduleManager::GetSchedules(ScheduleItem* items, int max_count) {
    std::lock_guard<std::mutex> lock(mutex_);

    int count = std::min((int)schedules_.size(), max_count);
    for (int i = 0; i < count; i++) {
        items[i] = schedules_[i];
    }
    return count;
}

int ScheduleManager::GetUpcoming(ScheduleItem* items, int max_count, int hours) {
    std::lock_guard<std::mutex> lock(mutex_);

    time_t now = time(nullptr);
    time_t deadline = now + hours * 3600;

    int count = 0;
    for (const auto& item : schedules_) {
        if (item.enabled && !item.triggered &&
            item.trigger_time >= now && item.trigger_time <= deadline) {
            if (count < max_count) {
                items[count++] = item;
            }
        }
    }

    return count;
}

uint32_t ScheduleManager::CalculateNextTrigger(const ScheduleItem& item) {
    time_t current = item.trigger_time;
    struct tm tm_time;
    localtime_r(&current, &tm_time);

    if (strcmp(item.repeat_type, "daily") == 0) {
        tm_time.tm_mday += 1;
    } else if (strcmp(item.repeat_type, "weekly") == 0) {
        tm_time.tm_mday += 7;
    } else if (strcmp(item.repeat_type, "monthly") == 0) {
        tm_time.tm_mon += 1;
    } else {
        return 0;  // No repeat
    }

    return mktime(&tm_time);
}

void ScheduleManager::CheckAndTrigger() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_ || !reminder_callback_) {
        return;
    }

    time_t now = time(nullptr);

    for (auto& item : schedules_) {
        if (!item.enabled || item.triggered) {
            continue;
        }

        // Check if it's time to trigger (within 60 seconds window)
        if (item.trigger_time <= now && (now - item.trigger_time) < 60) {
            ESP_LOGI(TAG, "Triggering reminder #%lu: '%s'",
                     (unsigned long)item.id, item.content);

            // Call the callback
            reminder_callback_(item);

            // Handle repeat
            if (strcmp(item.repeat_type, "none") == 0) {
                item.triggered = 1;
            } else {
                // Calculate next trigger time
                uint32_t next_time = CalculateNextTrigger(item);
                if (next_time > 0) {
                    item.trigger_time = next_time;
                    ESP_LOGI(TAG, "Next trigger for #%lu at %lu",
                             (unsigned long)item.id, (unsigned long)next_time);
                } else {
                    item.triggered = 1;
                }
            }

            dirty_ = true;
        }
    }

    // Clean up triggered non-repeating schedules
    schedules_.erase(
        std::remove_if(schedules_.begin(), schedules_.end(),
            [](const ScheduleItem& item) {
                return item.triggered && strcmp(item.repeat_type, "none") == 0;
            }),
        schedules_.end()
    );
}

void ScheduleManager::SetReminderCallback(ReminderCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    reminder_callback_ = callback;
}

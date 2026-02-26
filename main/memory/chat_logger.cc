#include "chat_logger.h"
#include <esp_log.h>
#include <cstring>
#include <ctime>
#include <algorithm>

#define TAG "ChatLogger"

namespace {
    const char* NVS_NAMESPACE = "chat_log";
    const char* KEY_META = "meta";
    const char* KEY_MESSAGES = "messages";
}

ChatLogger& ChatLogger::GetInstance() {
    static ChatLogger instance;
    return instance;
}

ChatLogger::~ChatLogger() {
    Flush();
    if (nvs_handle_ != 0) {
        nvs_close(nvs_handle_);
    }
}

bool ChatLogger::Initialize() {
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
    ESP_LOGI(TAG, "Chat logger initialized with %d messages", (int)buffer_.size());
    return true;
}

void ChatLogger::LoadFromNvs() {
    // Load meta
    size_t meta_size = sizeof(ChatLogMeta);
    esp_err_t err = nvs_get_blob(nvs_handle_, KEY_META, &meta_, &meta_size);
    if (err != ESP_OK || memcmp(meta_.magic, MEMORY_MAGIC_CHAT, 4) != 0) {
        memset(&meta_, 0, sizeof(meta_));
        memcpy(meta_.magic, MEMORY_MAGIC_CHAT, 4);
    }

    // Load messages
    ChatMessage messages[MAX_CHAT_MESSAGES];
    size_t msg_size = sizeof(messages);
    err = nvs_get_blob(nvs_handle_, KEY_MESSAGES, messages, &msg_size);
    if (err == ESP_OK) {
        int count = msg_size / sizeof(ChatMessage);
        buffer_.clear();
        for (int i = 0; i < count; i++) {
            if (strlen(messages[i].content) > 0) {
                buffer_.push_back(messages[i]);
            }
        }
    }
}

void ChatLogger::SaveToNvs() {
    if (!dirty_ || buffer_.empty()) return;

    esp_err_t err = nvs_set_blob(nvs_handle_, KEY_MESSAGES,
                                  buffer_.data(),
                                  buffer_.size() * sizeof(ChatMessage));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save messages: %s", esp_err_to_name(err));
        return;
    }

    SaveMeta();
    nvs_commit(nvs_handle_);
    dirty_ = false;
}

void ChatLogger::SaveMeta() {
    meta_.last_save_time = time(nullptr);
    nvs_set_blob(nvs_handle_, KEY_META, &meta_, sizeof(ChatLogMeta));
}

void ChatLogger::Flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    SaveToNvs();
}

bool ChatLogger::Log(const char* role, const char* content) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_) {
        ESP_LOGW(TAG, "Chat logger not initialized");
        return false;
    }

    ChatMessage msg;
    memset(&msg, 0, sizeof(msg));
    msg.timestamp = time(nullptr);
    msg.role = (strcmp(role, "user") == 0) ? 0 : 1;

    // Truncate content if too long
    size_t content_len = strlen(content);
    if (content_len >= sizeof(msg.content)) {
        // Smart truncate at word boundary
        strncpy(msg.content, content, sizeof(msg.content) - 4);
        strcpy(msg.content + sizeof(msg.content) - 4, "...");
    } else {
        strcpy(msg.content, content);
    }

    // Ring buffer - remove oldest if full
    if (buffer_.size() >= MAX_CHAT_MESSAGES) {
        buffer_.erase(buffer_.begin());
    }

    buffer_.push_back(msg);
    meta_.total_count++;
    meta_.newest_index++;
    dirty_ = true;

    // Batch save every 10 messages
    if (meta_.total_count % 10 == 0) {
        SaveToNvs();
    }

    return true;
}

int ChatLogger::GetRecent(std::vector<ChatMessage>& messages, int count) {
    std::lock_guard<std::mutex> lock(mutex_);

    messages.clear();
    int start = std::max(0, (int)buffer_.size() - count);
    for (size_t i = start; i < buffer_.size(); i++) {
        messages.push_back(buffer_[i]);
    }
    return messages.size();
}

int ChatLogger::GetToday(std::vector<ChatMessage>& messages) {
    std::lock_guard<std::mutex> lock(mutex_);

    messages.clear();

    time_t now = time(nullptr);
    struct tm* tm_now = localtime(&now);

    // Get start of today
    struct tm today_start = *tm_now;
    today_start.tm_hour = 0;
    today_start.tm_min = 0;
    today_start.tm_sec = 0;
    time_t today_timestamp = mktime(&today_start);

    for (const auto& msg : buffer_) {
        if (msg.timestamp >= today_timestamp) {
            messages.push_back(msg);
        }
    }
    return messages.size();
}

int ChatLogger::Search(const std::string& keyword, std::vector<ChatMessage>& messages,
                       int max_count) {
    std::lock_guard<std::mutex> lock(mutex_);

    messages.clear();
    for (const auto& msg : buffer_) {
        if (messages.size() >= (size_t)max_count) break;
        if (strstr(msg.content, keyword.c_str()) != nullptr) {
            messages.push_back(msg);
        }
    }
    return messages.size();
}

std::string ChatLogger::GetFormatted(int max_messages) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::string result;
    int start = std::max(0, (int)buffer_.size() - max_messages);

    for (size_t i = start; i < buffer_.size(); i++) {
        const auto& msg = buffer_[i];

        // Format timestamp
        time_t timestamp = msg.timestamp;
        struct tm* tm_info = localtime(&timestamp);
        char time_str[20];
        strftime(time_str, sizeof(time_str), "%H:%M", tm_info);

        // Add message
        result += "[";
        result += time_str;
        result += "] ";
        result += (msg.role == 0) ? "User" : "Assistant";
        result += ": ";
        result += msg.content;
        result += "\n";
    }

    return result;
}

int ChatLogger::Trim(int keep_count) {
    std::lock_guard<std::mutex> lock(mutex_);

    if ((int)buffer_.size() <= keep_count) {
        return 0;
    }

    int removed = buffer_.size() - keep_count;
    buffer_.erase(buffer_.begin(), buffer_.begin() + removed);
    dirty_ = true;
    SaveToNvs();

    ESP_LOGI(TAG, "Trimmed %d old messages", removed);
    return removed;
}

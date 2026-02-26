#include "pending_memory.h"
#include "memory_storage.h"
#include <esp_log.h>
#include <cstring>
#include <ctime>
#include <algorithm>

#define TAG "PendingMem"

static const char* NVS_NAMESPACE = "pending_mem";
static const char* KEY_PENDING = "pending";

PendingMemory& PendingMemory::GetInstance() {
    static PendingMemory instance;
    return instance;
}

PendingMemory::~PendingMemory() {
    // Lock to ensure thread safety during destruction
    std::lock_guard<std::mutex> lock(mutex_);
    if (dirty_) {
        SaveToNvs();
    }
    if (nvs_handle_) {
        nvs_close(nvs_handle_);
    }
}

bool PendingMemory::Init() {
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
    CleanExpired();
    initialized_ = true;

    ESP_LOGI(TAG, "Initialized with %d pending items", (int)pending_.size());
    return true;
}

void PendingMemory::LoadFromNvs() {
    size_t size = 0;
    esp_err_t err = nvs_get_blob(nvs_handle_, KEY_PENDING, nullptr, &size);

    if (err == ESP_ERR_NVS_NOT_FOUND || size == 0) {
        ESP_LOGI(TAG, "No pending data found in NVS");
        return;
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get pending size: %s", esp_err_to_name(err));
        return;
    }

    // Read item count first
    uint8_t count = 0;
    size_t count_size = sizeof(count);
    err = nvs_get_blob(nvs_handle_, "count", &count, &count_size);
    if (err != ESP_OK || count == 0) {
        return;
    }

    // Read items
    std::vector<PendingItem> items(count);
    size = count * sizeof(PendingItem);
    err = nvs_get_blob(nvs_handle_, KEY_PENDING, items.data(), &size);

    if (err == ESP_OK) {
        pending_.clear();
        for (const auto& item : items) {
            if (memcmp(item.magic, "XZPD", 4) == 0) {
                pending_.push_back(item);
            }
        }
        ESP_LOGI(TAG, "Loaded %d pending items from NVS", (int)pending_.size());
    }
}

void PendingMemory::SaveToNvs() {
    if (!nvs_handle_) {
        return;
    }

    uint8_t count = pending_.size();
    nvs_set_blob(nvs_handle_, "count", &count, sizeof(count));

    if (count > 0) {
        nvs_set_blob(nvs_handle_, KEY_PENDING, pending_.data(),
                     pending_.size() * sizeof(PendingItem));
    } else {
        nvs_erase_key(nvs_handle_, KEY_PENDING);
    }

    nvs_commit(nvs_handle_);
    dirty_ = false;
    ESP_LOGI(TAG, "Saved %d pending items to NVS", count);
}

void PendingMemory::Save() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (dirty_) {
        SaveToNvs();
    }
}

std::string PendingMemory::MakeKey(const ExtractedMemory& mem) {
    std::string key;

    switch (mem.type) {
        case ExtractedType::IDENTITY:
            key = "identity:";
            key += mem.category;  // name, age, gender, location
            break;

        case ExtractedType::PREFERENCE:
            if (strcmp(mem.category, "like") == 0) {
                key = "like:";
            } else {
                key = "dislike:";
            }
            key += mem.content;
            break;

        case ExtractedType::FAMILY:
            key = "family:";
            key += mem.category;  // relation type
            key += ":";
            key += mem.content;   // name
            break;

        case ExtractedType::FACT:
            key = "fact:";
            key += mem.content;
            break;

        case ExtractedType::EVENT:
            key = "event:";
            key += mem.content;
            break;

        default:
            key = "other:";
            key += mem.content;
            break;
    }

    // Truncate key if too long
    if (key.length() > 31) {
        key = key.substr(0, 31);
    }

    return key;
}

bool PendingMemory::IsSameValue(const PendingItem& item, const ExtractedMemory& mem) {
    // For identity, check if value matches
    if (mem.type == ExtractedType::IDENTITY) {
        return strcmp(item.value, mem.content) == 0;
    }

    // For preferences and facts, key already contains the content
    return true;
}

int PendingMemory::FindByKey(const std::string& key) {
    for (size_t i = 0; i < pending_.size(); i++) {
        if (strcmp(pending_[i].key, key.c_str()) == 0) {
            return i;
        }
    }
    return -1;
}

bool PendingMemory::AddOrConfirm(const ExtractedMemory& memory) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_) {
        ESP_LOGW(TAG, "Not initialized");
        return false;
    }

    // High confidence memories skip confirmation
    if (memory.confidence >= HIGH_CONFIDENCE_THRESHOLD) {
        ESP_LOGI(TAG, "High confidence memory, skip confirmation: %s", memory.content);
        return true;
    }

    std::string key = MakeKey(memory);
    int idx = FindByKey(key);

    if (idx >= 0) {
        // Found existing item
        PendingItem& item = pending_[idx];

        if (IsSameValue(item, memory)) {
            // Same value, increment count
            item.count++;
            ESP_LOGI(TAG, "Key '%s' count: %d", key.c_str(), item.count);

            if (item.count >= CONFIRM_THRESHOLD) {
                // Confirmed! Remove from pending
                pending_.erase(pending_.begin() + idx);
                dirty_ = true;
                ESP_LOGI(TAG, "Confirmed memory: %s", key.c_str());
                return true;
            }
        } else {
            // Different value, reset with new value
            strncpy(item.value, memory.content, sizeof(item.value) - 1);
            item.value[sizeof(item.value) - 1] = '\0';
            item.count = 1;
            item.first_seen = time(nullptr);
            ESP_LOGI(TAG, "Key '%s' value changed, reset count", key.c_str());
        }

        dirty_ = true;
        return false;
    }

    // New item
    if (pending_.size() >= MAX_PENDING_ITEMS) {
        // Remove oldest item
        uint32_t oldest_time = UINT32_MAX;
        int oldest_idx = 0;
        for (size_t i = 0; i < pending_.size(); i++) {
            if (pending_[i].first_seen < oldest_time) {
                oldest_time = pending_[i].first_seen;
                oldest_idx = i;
            }
        }
        ESP_LOGI(TAG, "Removing oldest pending item: %s", pending_[oldest_idx].key);
        pending_.erase(pending_.begin() + oldest_idx);
    }

    PendingItem item;
    memcpy(item.magic, "XZPD", 4);
    item.type = memory.type;
    strncpy(item.key, key.c_str(), sizeof(item.key) - 1);
    item.key[sizeof(item.key) - 1] = '\0';
    strncpy(item.value, memory.content, sizeof(item.value) - 1);
    item.value[sizeof(item.value) - 1] = '\0';
    item.first_seen = time(nullptr);
    item.count = 1;
    memset(item.reserved, 0, sizeof(item.reserved));

    pending_.push_back(item);
    dirty_ = true;

    ESP_LOGI(TAG, "Added pending item: %s = %s", key.c_str(), memory.content);
    return false;
}

void PendingMemory::CleanExpired() {
    uint32_t now = time(nullptr);
    size_t before = pending_.size();

    pending_.erase(
        std::remove_if(pending_.begin(), pending_.end(),
            [now](const PendingItem& item) {
                return (now - item.first_seen) > PENDING_EXPIRY_SECONDS;
            }),
        pending_.end()
    );

    if (pending_.size() < before) {
        dirty_ = true;
        ESP_LOGI(TAG, "Cleaned %d expired items", (int)(before - pending_.size()));
    }
}

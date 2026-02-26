#include "memory_archive.h"
#include <esp_log.h>
#include <esp_vfs.h>
#include <esp_spiffs.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>
#include <cJSON.h>

#define TAG "MemArchive"

MemoryArchive& MemoryArchive::GetInstance() {
    static MemoryArchive instance;
    return instance;
}

MemoryArchive::~MemoryArchive() {
    if (spiffs_mounted_) {
        esp_vfs_spiffs_unregister("memory");
        ESP_LOGI(TAG, "Memory SPIFFS unmounted");
    }
}

bool MemoryArchive::Init() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (initialized_) {
        return true;
    }

    // Mount SPIFFS (Memory partition - dedicated for archiving)
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "memory",
        .max_files = 5,
        .format_if_mount_failed = true  // Format on first use
    };

    ESP_LOGI(TAG, "Attempting to mount memory partition...");
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount SPIFFS (may need formatting)");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition 'memory'");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return false;
    }

    ESP_LOGI(TAG, "Memory partition mounted successfully");
    spiffs_mounted_ = true;

    // Get partition info
    size_t total = 0, used = 0;
    ret = esp_spiffs_info("memory", &total, &used);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Memory SPIFFS: total=%u KB, used=%u KB, available=%u KB",
                 (unsigned int)(total / 1024),
                 (unsigned int)(used / 1024),
                 (unsigned int)((total - used) / 1024));
    } else {
        ESP_LOGE(TAG, "Failed to get SPIFFS info: %s", esp_err_to_name(ret));
    }

    // Note: SPIFFS is a flat filesystem, no need to create directories
    // Files with "/" in the name will work directly
    ESP_LOGI(TAG, "SPIFFS is ready (flat filesystem, no directories needed)");

    initialized_ = true;
    ESP_LOGI(TAG, "Memory archive initialized successfully");
    return true;
}

bool MemoryArchive::CreateDirectoryIfNotExists() {
    // SPIFFS is a flat filesystem - directories are not needed
    // Files with "/" in the name (e.g., "/spiffs/memory/facts_archive.jsonl")
    // will work directly without creating intermediate directories
    return true;
}

bool MemoryArchive::AppendToFile(const char* filename, const char* json_line) {
    if (!initialized_) {
        ESP_LOGW(TAG, "Archive not initialized");
        return false;
    }

    FILE* f = fopen(filename, "a");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open file for append: %s (errno: %d)", filename, errno);
        return false;
    }

    int written = fprintf(f, "%s\n", json_line);
    if (written < 0) {
        ESP_LOGE(TAG, "Failed to write to file: %s (errno: %d)", filename, errno);
        fclose(f);
        return false;
    }

    if (fclose(f) != 0) {
        ESP_LOGE(TAG, "Failed to close file: %s (errno: %d)", filename, errno);
        return false;
    }

    return true;
}

std::string MemoryArchive::SerializeFact(const Fact& fact) {
    cJSON* root = cJSON_CreateObject();

    // Add timestamp
    char timestamp[20];
    time_t now = time(nullptr);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", localtime(&now));
    cJSON_AddStringToObject(root, "timestamp", timestamp);

    cJSON_AddStringToObject(root, "type", "fact");
    cJSON_AddStringToObject(root, "content", fact.content);

    char* json_str = cJSON_PrintUnformatted(root);
    std::string result(json_str);

    cJSON_free(json_str);
    cJSON_Delete(root);

    return result;
}

std::string MemoryArchive::SerializeMoment(const SpecialMoment& moment) {
    cJSON* root = cJSON_CreateObject();

    // Add timestamp
    char timestamp[20];
    time_t now = time(nullptr);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", localtime(&now));
    cJSON_AddStringToObject(root, "timestamp", timestamp);

    cJSON_AddStringToObject(root, "type", "moment");
    cJSON_AddStringToObject(root, "topic", moment.topic);
    cJSON_AddStringToObject(root, "content", moment.content);
    cJSON_AddNumberToObject(root, "emotion_type", moment.emotion.type);
    cJSON_AddNumberToObject(root, "emotion_intensity", moment.emotion.intensity);
    cJSON_AddNumberToObject(root, "importance", moment.importance);

    char* json_str = cJSON_PrintUnformatted(root);
    std::string result(json_str);

    cJSON_free(json_str);
    cJSON_Delete(root);

    return result;
}

std::string MemoryArchive::SerializeEvent(const Event& event) {
    cJSON* root = cJSON_CreateObject();

    // Add timestamp
    char timestamp[20];
    time_t now = time(nullptr);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", localtime(&now));
    cJSON_AddStringToObject(root, "timestamp", timestamp);

    cJSON_AddStringToObject(root, "type", "event");
    cJSON_AddStringToObject(root, "date", event.date);
    cJSON_AddStringToObject(root, "event_type", event.event_type);
    cJSON_AddStringToObject(root, "content", event.content);
    cJSON_AddNumberToObject(root, "significance", event.significance);

    char* json_str = cJSON_PrintUnformatted(root);
    std::string result(json_str);

    cJSON_free(json_str);
    cJSON_Delete(root);

    return result;
}

bool MemoryArchive::ArchiveFacts(const std::vector<Fact>& facts) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (facts.empty()) {
        return true;
    }

    int archived_count = 0;
    for (const auto& fact : facts) {
        std::string json_line = SerializeFact(fact);
        if (AppendToFile(FACTS_ARCHIVE, json_line.c_str())) {
            archived_count++;
        } else {
            ESP_LOGW(TAG, "Failed to archive fact: %s", fact.content);
        }
    }

    ESP_LOGI(TAG, "Archived %d facts to %s", archived_count, FACTS_ARCHIVE);
    return archived_count > 0;
}

bool MemoryArchive::ArchiveMoments(const std::vector<SpecialMoment>& moments) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (moments.empty()) {
        return true;
    }

    int archived_count = 0;
    for (const auto& moment : moments) {
        std::string json_line = SerializeMoment(moment);
        if (AppendToFile(MOMENTS_ARCHIVE, json_line.c_str())) {
            archived_count++;
        } else {
            ESP_LOGW(TAG, "Failed to archive moment: %s - %s", moment.topic, moment.content);
        }
    }

    ESP_LOGI(TAG, "Archived %d moments to %s", archived_count, MOMENTS_ARCHIVE);
    return archived_count > 0;
}

bool MemoryArchive::ArchiveEvents(const std::vector<Event>& events) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (events.empty()) {
        return true;
    }

    int archived_count = 0;
    for (const auto& event : events) {
        std::string json_line = SerializeEvent(event);
        if (AppendToFile(EVENTS_ARCHIVE, json_line.c_str())) {
            archived_count++;
        } else {
            ESP_LOGW(TAG, "Failed to archive event: %s - %s", event.date, event.content);
        }
    }

    ESP_LOGI(TAG, "Archived %d events to %s", archived_count, EVENTS_ARCHIVE);
    return archived_count > 0;
}

size_t MemoryArchive::GetArchiveCount(const char* type) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_) {
        return 0;
    }

    const char* filename = GetArchiveFilename(type);
    if (!filename) {
        return 0;
    }

    FILE* f = fopen(filename, "r");
    if (!f) {
        return 0;
    }

    size_t count = 0;
    char line[1024];  // Increased buffer size
    while (fgets(line, sizeof(line), f) != nullptr) {
        // Only count complete lines (ending with newline)
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            count++;
        } else if (len >= sizeof(line) - 1) {
            // Line truncated, skip to end
            int ch;
            while ((ch = fgetc(f)) != '\n' && ch != EOF);
            count++;  // Still count it as a line
        }
    }

    fclose(f);
    return count;
}

// ========== Stage 2: Recall/Search Implementation ==========

const char* MemoryArchive::GetArchiveFilename(const char* type) {
    if (strcmp(type, "fact") == 0) {
        return FACTS_ARCHIVE;
    } else if (strcmp(type, "moment") == 0) {
        return MOMENTS_ARCHIVE;
    } else if (strcmp(type, "event") == 0) {
        return EVENTS_ARCHIVE;
    }
    return nullptr;
}

bool MemoryArchive::ParseArchivedItem(const char* json_line, ArchivedItem& item) {
    // Initialize item to avoid garbage data
    memset(&item, 0, sizeof(item));

    cJSON* root = cJSON_Parse(json_line);
    if (!root) {
        ESP_LOGW(TAG, "Failed to parse JSON line: %.50s...", json_line);
        return false;
    }

    // Extract timestamp
    cJSON* timestamp = cJSON_GetObjectItem(root, "timestamp");
    if (timestamp && cJSON_IsString(timestamp)) {
        strncpy(item.timestamp, timestamp->valuestring, sizeof(item.timestamp) - 1);
    }

    // Extract type
    cJSON* type = cJSON_GetObjectItem(root, "type");
    if (type && cJSON_IsString(type)) {
        strncpy(item.type, type->valuestring, sizeof(item.type) - 1);
    }

    // Store the entire JSON as content (for flexible parsing later)
    strncpy(item.content, json_line, sizeof(item.content) - 1);

    cJSON_Delete(root);
    return true;
}

bool MemoryArchive::MatchesTimeRange(const char* timestamp, const char* start_date, const char* end_date) {
    // timestamp format: YYYY-MM-DDTHH:MM:SS
    // date format: YYYY-MM-DD
    // Simple string comparison works due to ISO 8601 format

    if (start_date && strlen(start_date) > 0) {
        if (strncmp(timestamp, start_date, 10) < 0) {
            return false;
        }
    }

    if (end_date && strlen(end_date) > 0) {
        if (strncmp(timestamp, end_date, 10) > 0) {
            return false;
        }
    }

    return true;
}

bool MemoryArchive::ContainsKeyword(const char* content, const char* keyword) {
    if (!keyword || strlen(keyword) == 0) {
        return true;  // Empty keyword matches everything
    }

    // Case-insensitive search
    std::string content_lower(content);
    std::string keyword_lower(keyword);

    // Convert to lowercase
    for (char& c : content_lower) {
        if (c >= 'A' && c <= 'Z') {
            c = c + ('a' - 'A');
        }
    }
    for (char& c : keyword_lower) {
        if (c >= 'A' && c <= 'Z') {
            c = c + ('a' - 'A');
        }
    }

    return content_lower.find(keyword_lower) != std::string::npos;
}

std::vector<ArchivedItem> MemoryArchive::RecallByTimeRange(
    const char* type, const char* start_date, const char* end_date, int limit) {

    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ArchivedItem> results;

    if (!initialized_) {
        ESP_LOGW(TAG, "Archive not initialized");
        return results;
    }

    const char* filename = GetArchiveFilename(type);
    if (!filename) {
        ESP_LOGW(TAG, "Unknown type: %s", type);
        return results;
    }

    FILE* f = fopen(filename, "r");
    if (!f) {
        ESP_LOGI(TAG, "No archive file found for type: %s", type);
        return results;
    }

    ESP_LOGI(TAG, "Recalling %s by time range: %s to %s (limit: %d)",
             type, start_date ? start_date : "any", end_date ? end_date : "any", limit);

    char line[1024];  // Increased buffer size for longer JSON lines
    int matched = 0;
    int total_lines = 0;
    int truncated_lines = 0;

    while (fgets(line, sizeof(line), f) != nullptr && matched < limit) {
        total_lines++;

        // Check for truncated line (no newline at end and buffer is full)
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
            len--;
        } else if (len >= sizeof(line) - 1) {
            // Line was truncated, skip to end of this line
            ESP_LOGW(TAG, "Line %d truncated (>%d bytes), skipping", total_lines, (int)sizeof(line));
            truncated_lines++;
            int ch;
            while ((ch = fgetc(f)) != '\n' && ch != EOF);
            continue;
        }

        ArchivedItem item;
        if (ParseArchivedItem(line, item)) {
            if (MatchesTimeRange(item.timestamp, start_date, end_date)) {
                results.push_back(item);
                matched++;
            }
        }
    }

    fclose(f);

    ESP_LOGI(TAG, "Recalled %d/%d items (scanned %d lines, %d truncated)",
             matched, limit, total_lines, truncated_lines);

    return results;
}

std::vector<ArchivedItem> MemoryArchive::RecallByKeyword(
    const char* type, const char* keyword, int limit) {

    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ArchivedItem> results;

    if (!initialized_) {
        ESP_LOGW(TAG, "Archive not initialized");
        return results;
    }

    const char* filename = GetArchiveFilename(type);
    if (!filename) {
        ESP_LOGW(TAG, "Unknown type: %s", type);
        return results;
    }

    FILE* f = fopen(filename, "r");
    if (!f) {
        ESP_LOGI(TAG, "No archive file found for type: %s", type);
        return results;
    }

    ESP_LOGI(TAG, "Recalling %s by keyword: '%s' (limit: %d)", type, keyword, limit);

    char line[1024];  // Increased buffer size for longer JSON lines
    int matched = 0;
    int total_lines = 0;
    int truncated_lines = 0;

    while (fgets(line, sizeof(line), f) != nullptr && matched < limit) {
        total_lines++;

        // Check for truncated line
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
            len--;
        } else if (len >= sizeof(line) - 1) {
            ESP_LOGW(TAG, "Line %d truncated (>%d bytes), skipping", total_lines, (int)sizeof(line));
            truncated_lines++;
            int ch;
            while ((ch = fgetc(f)) != '\n' && ch != EOF);
            continue;
        }

        ArchivedItem item;
        if (ParseArchivedItem(line, item)) {
            if (ContainsKeyword(item.content, keyword)) {
                results.push_back(item);
                matched++;
            }
        }
    }

    fclose(f);

    ESP_LOGI(TAG, "Recalled %d/%d items (scanned %d lines, %d truncated)",
             matched, limit, total_lines, truncated_lines);

    return results;
}

std::vector<ArchivedItem> MemoryArchive::RecallRecent(const char* type, int limit) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ArchivedItem> results;

    if (!initialized_) {
        ESP_LOGW(TAG, "Archive not initialized");
        return results;
    }

    const char* filename = GetArchiveFilename(type);
    if (!filename) {
        ESP_LOGW(TAG, "Unknown type: %s", type);
        return results;
    }

    FILE* f = fopen(filename, "r");
    if (!f) {
        ESP_LOGI(TAG, "No archive file found for type: %s", type);
        return results;
    }

    ESP_LOGI(TAG, "Recalling %d most recent %s items", limit, type);

    // Read all lines first (JSONL is append-only, so last lines are most recent)
    std::vector<std::string> all_lines;
    char line[1024];  // Increased buffer size
    int truncated_lines = 0;

    while (fgets(line, sizeof(line), f) != nullptr) {
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
            len--;
        } else if (len >= sizeof(line) - 1) {
            // Line truncated, skip rest and don't add to results
            ESP_LOGW(TAG, "Line truncated (>%d bytes), skipping", (int)sizeof(line));
            truncated_lines++;
            int ch;
            while ((ch = fgetc(f)) != '\n' && ch != EOF);
            continue;
        }
        all_lines.push_back(std::string(line));
    }

    fclose(f);

    if (truncated_lines > 0) {
        ESP_LOGW(TAG, "Skipped %d truncated lines", truncated_lines);
    }

    // Take the last N lines (most recent)
    int start_idx = (int)all_lines.size() - limit;
    if (start_idx < 0) {
        start_idx = 0;
    }

    for (int i = start_idx; i < (int)all_lines.size(); i++) {
        ArchivedItem item;
        if (ParseArchivedItem(all_lines[i].c_str(), item)) {
            results.push_back(item);
        }
    }

    ESP_LOGI(TAG, "Recalled %d recent items (total archived: %d)",
             (int)results.size(), (int)all_lines.size());

    return results;
}

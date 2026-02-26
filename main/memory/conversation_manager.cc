#include "conversation_manager.h"
#include "memory_extractor.h"
#include "memory_storage.h"
#include "pending_memory.h"
#include "personality_evolver.h"
#include "chat_logger.h"
#include <esp_log.h>
#include <cstring>
#include <ctime>

#define TAG "ConvMgr"

ConversationManager& ConversationManager::GetInstance() {
    static ConversationManager instance;
    return instance;
}

bool ConversationManager::Init() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (initialized_) {
        return true;
    }

    // Initialize pending memory
    PendingMemory::GetInstance().Init();

    short_term_.reserve(SHORT_TERM_CAPACITY);
    summaries_.reserve(10);

    initialized_ = true;
    ESP_LOGI(TAG, "Conversation manager initialized");
    return true;
}

void ConversationManager::AddMessage(const char* role, const char* content) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_) {
        Init();
    }

    // Create message
    ChatMessage msg;
    msg.timestamp = time(nullptr);
    msg.role = (strcmp(role, "user") == 0) ? 0 : 1;
    strncpy(msg.content, content, sizeof(msg.content) - 1);
    msg.content[sizeof(msg.content) - 1] = '\0';

    // Add to short-term memory
    if (short_term_.size() >= SHORT_TERM_CAPACITY) {
        // Remove oldest message
        short_term_.erase(short_term_.begin());
    }
    short_term_.push_back(msg);

    // Track rounds (1 round = user message + assistant response)
    if (msg.role == 0) {
        // User message
        last_user_msg_idx_ = short_term_.size() - 1;
    } else if (last_user_msg_idx_ >= 0) {
        // Assistant response after user message = 1 round complete
        current_round_++;
        last_user_msg_idx_ = -1;
    }

    message_count_++;

    // Also log to ChatLogger for persistence
    ChatLogger::GetInstance().Log(role, content);

    ESP_LOGD(TAG, "Added message: role=%s, round=%d, count=%d",
             role, current_round_, message_count_);
}

std::string ConversationManager::GetRecentConversation(int rounds) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (short_term_.empty()) {
        return "";
    }

    std::string result;
    result.reserve(2048);

    // Calculate how many messages to show (rounds * 2)
    int msg_count = std::min((int)short_term_.size(), rounds * 2);
    int start_idx = short_term_.size() - msg_count;

    for (size_t i = start_idx; i < short_term_.size(); i++) {
        const ChatMessage& msg = short_term_[i];

        // Format time
        struct tm* tm_info = localtime((time_t*)&msg.timestamp);
        char time_buf[16];
        strftime(time_buf, sizeof(time_buf), "%H:%M", tm_info);

        // Format message
        char line[256];
        snprintf(line, sizeof(line), "[%s] %s: %s\n",
                 time_buf,
                 msg.role == 0 ? "User" : "Assistant",
                 msg.content);
        result += line;
    }

    return result;
}

void ConversationManager::CheckAndProcess() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (current_round_ >= PROCESS_INTERVAL_ROUNDS) {
        ESP_LOGI(TAG, "Reached %d rounds, processing to long-term", current_round_);
        ProcessToLongTerm();
    }
}

void ConversationManager::ProcessNow() {
    std::lock_guard<std::mutex> lock(mutex_);
    ProcessToLongTerm();
}

void ConversationManager::ProcessToLongTerm() {
    if (short_term_.empty()) {
        return;
    }

    ESP_LOGI(TAG, "Processing %d messages to long-term memory", (int)short_term_.size());

    // 1. Extract memories from all user messages
    int extracted = ExtractAndApply(short_term_);
    ESP_LOGI(TAG, "Extracted and processed %d memories", extracted);

    // 2. Generate summary
    std::string summary = GenerateSummary(short_term_);
    if (!summary.empty()) {
        summaries_.push_back(summary);
        // Keep only last 10 summaries
        if (summaries_.size() > 10) {
            summaries_.erase(summaries_.begin());
        }
        ESP_LOGI(TAG, "Generated summary: %s", summary.c_str());
    }

    // 3. Update affection stats
    PersonalityEvolver::GetInstance().OnConversationEnd();

    // 4. Save pending memories
    PendingMemory::GetInstance().Save();

    // 5. Flush memory storage
    MemoryStorage::GetInstance().Flush();

    // 6. Reset counters (keep recent messages)
    current_round_ = 0;
    message_count_ = 0;

    // Keep only the most recent messages (half capacity)
    if (short_term_.size() > SHORT_TERM_CAPACITY / 2) {
        short_term_.erase(short_term_.begin(),
                          short_term_.begin() + (short_term_.size() - SHORT_TERM_CAPACITY / 2));
    }
}

int ConversationManager::ExtractAndApply(const std::vector<ChatMessage>& messages) {
    int total_applied = 0;
    auto& pending = PendingMemory::GetInstance();
    auto& storage = MemoryStorage::GetInstance();

    for (const auto& msg : messages) {
        // Only extract from user messages
        if (msg.role != 0) {
            continue;
        }

        // Extract memories
        std::vector<ExtractedMemory> memories = MemoryExtractor::Extract(msg.content);

        for (const auto& mem : memories) {
            // Skip low confidence
            if (mem.confidence < 3) {
                continue;
            }

            // Try to confirm through pending system
            bool confirmed = pending.AddOrConfirm(mem);

            if (confirmed) {
                // Memory confirmed! Apply to storage
                switch (mem.type) {
                    case ExtractedType::IDENTITY:
                        if (strcmp(mem.category, "name") == 0) {
                            storage.UpdateProfile(mem.content, nullptr, 0, nullptr, nullptr);
                        } else if (strcmp(mem.category, "age") == 0) {
                            int age = atoi(mem.content);
                            if (age > 0 && age < 150) {
                                storage.UpdateProfile(nullptr, nullptr, age, nullptr, nullptr);
                            }
                        } else if (strcmp(mem.category, "gender") == 0) {
                            storage.UpdateProfile(nullptr, nullptr, 0, mem.content, nullptr);
                        } else if (strcmp(mem.category, "location") == 0) {
                            storage.UpdateProfile(nullptr, nullptr, 0, nullptr, mem.content);
                        }
                        break;

                    case ExtractedType::PREFERENCE:
                        storage.AddPreference(mem.content, strcmp(mem.category, "like") == 0);
                        break;

                    case ExtractedType::FAMILY:
                        storage.AddFamilyMember(mem.category, mem.content, nullptr, 3, nullptr);
                        break;

                    case ExtractedType::FACT:
                        storage.AddFact(mem.content);
                        break;

                    default:
                        break;
                }

                total_applied++;
                ESP_LOGI(TAG, "Applied confirmed memory: type=%d, content=%s",
                         (int)mem.type, mem.content);
            }
        }
    }

    return total_applied;
}

std::string ConversationManager::GenerateSummary(const std::vector<ChatMessage>& messages) {
    if (messages.empty()) {
        return "";
    }

    // Simple summary: count topics discussed
    std::string summary;
    summary.reserve(256);

    // Get time range
    uint32_t start_time = messages.front().timestamp;
    uint32_t end_time = messages.back().timestamp;

    struct tm* start_tm = localtime((time_t*)&start_time);
    char time_buf[32];
    strftime(time_buf, sizeof(time_buf), "%m-%d %H:%M", start_tm);

    // Count user/assistant messages
    int user_count = 0;
    int assistant_count = 0;
    for (const auto& msg : messages) {
        if (msg.role == 0) user_count++;
        else assistant_count++;
    }

    // Generate simple summary
    char summary_buf[256];
    snprintf(summary_buf, sizeof(summary_buf),
             "[%s] %d rounds chat, %d user msgs, %d responses",
             time_buf, (user_count + assistant_count) / 2, user_count, assistant_count);

    return std::string(summary_buf);
}

void ConversationManager::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    short_term_.clear();
    current_round_ = 0;
    message_count_ = 0;
    last_user_msg_idx_ = -1;
    ESP_LOGI(TAG, "Short-term memory cleared");
}

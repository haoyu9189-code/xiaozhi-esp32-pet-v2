#ifndef CHAT_LOGGER_H
#define CHAT_LOGGER_H

#include "memory_types.h"
#include <string>
#include <vector>
#include <mutex>
#include <nvs_flash.h>

class ChatLogger {
public:
    static ChatLogger& GetInstance();

    // Initialization
    bool Initialize();

    // Log message
    bool Log(const char* role, const char* content);

    // Get messages
    int GetRecent(std::vector<ChatMessage>& messages, int count = 20);
    int GetToday(std::vector<ChatMessage>& messages);
    int Search(const std::string& keyword, std::vector<ChatMessage>& messages,
               int max_count = 20);

    // Formatted output
    std::string GetFormatted(int max_messages = 10);

    // Maintenance
    int Trim(int keep_count = MAX_CHAT_MESSAGES);
    void Flush();

    // Statistics
    int GetTotalCount() const { return meta_.total_count; }

private:
    ChatLogger() = default;
    ~ChatLogger();

    nvs_handle_t nvs_handle_ = 0;
    std::vector<ChatMessage> buffer_;
    ChatLogMeta meta_;
    bool dirty_ = false;
    bool initialized_ = false;
    std::mutex mutex_;

    void SaveToNvs();
    void LoadFromNvs();
    void SaveMeta();
};

#endif // CHAT_LOGGER_H

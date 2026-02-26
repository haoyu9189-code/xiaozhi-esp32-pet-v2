#ifndef CONVERSATION_MANAGER_H
#define CONVERSATION_MANAGER_H

#include "memory_types.h"
#include <string>
#include <vector>
#include <mutex>

// Short-term memory capacity (10 rounds = 20 messages)
#define SHORT_TERM_CAPACITY 20

// Process to long-term every N rounds
#define PROCESS_INTERVAL_ROUNDS 10

class ConversationManager {
public:
    static ConversationManager& GetInstance();

    // Initialize
    bool Init();

    // Add a message to short-term memory
    // role: "user" or "assistant"
    void AddMessage(const char* role, const char* content);

    // Get recent conversation as formatted string
    // rounds: number of conversation rounds (1 round = user + assistant)
    std::string GetRecentConversation(int rounds = 10);

    // Get current round count in this session
    int GetCurrentRound() const { return current_round_; }

    // Get total message count
    int GetMessageCount() const { return message_count_; }

    // Check if processing is needed and do it
    void CheckAndProcess();

    // Force process current messages to long-term
    void ProcessNow();

    // Clear short-term memory
    void Clear();

    // Get short-term messages
    const std::vector<ChatMessage>& GetShortTermMessages() const { return short_term_; }

    // Get historical summaries
    const std::vector<std::string>& GetSummaries() const { return summaries_; }

private:
    ConversationManager() = default;

    std::vector<ChatMessage> short_term_;   // Short-term memory
    std::vector<std::string> summaries_;    // Historical summaries
    std::mutex mutex_;

    int current_round_ = 0;                 // Rounds in current session
    int message_count_ = 0;                 // Total messages since last process
    int last_user_msg_idx_ = -1;            // Index of last user message
    bool initialized_ = false;

    // Process messages to long-term memory
    void ProcessToLongTerm();

    // Extract and apply memories from messages
    int ExtractAndApply(const std::vector<ChatMessage>& messages);

    // Generate summary of messages
    std::string GenerateSummary(const std::vector<ChatMessage>& messages);
};

#endif // CONVERSATION_MANAGER_H

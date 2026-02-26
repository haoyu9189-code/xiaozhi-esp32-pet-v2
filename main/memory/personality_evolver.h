#ifndef PERSONALITY_EVOLVER_H
#define PERSONALITY_EVOLVER_H

#include "memory_types.h"
#include <string>
#include <vector>
#include <mutex>

class PersonalityEvolver {
public:
    static PersonalityEvolver& GetInstance();

    // Initialization
    bool Init();

    // Affection management
    void AddAffection(AffectionEvent event);
    void AddAffection(int8_t amount, const char* reason = nullptr);
    uint8_t GetAffection() const;
    int8_t GetMood() const;

    // Conversation lifecycle
    void OnConversationStart();
    void OnConversationEnd();
    void AddMessageCount(uint32_t count = 1);

    // Special events
    SpecialEventInfo CheckSpecialEvents();
    void RecordEmotionalMoment(uint8_t emotion_type, uint8_t intensity);
    void RecordSharedSecret();
    void RecordComfort();

    // Achievements
    void CheckAchievements();
    bool HasAchievement(Achievement ach) const;
    std::vector<Achievement> GetNewAchievements();

    // Relationship
    RelationshipStage GetRelationshipStage() const;
    bool GetStageChange(RelationshipStage* old_stage, RelationshipStage* new_stage);

    // Personality prompt generation
    std::string GeneratePersonalityPrompt();

    // Statistics
    const AffectionStats& GetStats() const { return stats_; }
    uint32_t GetTotalConversations() const { return stats_.total_conversations; }
    uint32_t GetTotalMessages() const { return stats_.total_messages; }
    uint16_t GetStreakDays() const { return stats_.streak_days; }

    // Data management
    void Flush();

private:
    PersonalityEvolver() = default;
    ~PersonalityEvolver();

    AffectionStats stats_;
    std::mutex mutex_;
    bool initialized_ = false;
    bool dirty_ = false;

    // Session tracking
    uint32_t session_start_time_ = 0;
    uint32_t session_messages_ = 0;

    // State tracking
    RelationshipStage previous_stage_ = RelationshipStage::STRANGER;
    uint16_t new_achievements_ = 0;

    void LoadFromNvs();
    void SaveToNvs();
    void UpdateStreak();
    RelationshipStage CalculateStageFromCoins() const;
};

#endif // PERSONALITY_EVOLVER_H

#ifndef PET_ACHIEVEMENTS_H
#define PET_ACHIEVEMENTS_H

#include <cstdint>
#include <functional>

// Activity counters for tracking achievements
struct ActivityCounters {
    uint32_t bathe_count;
    uint32_t feed_count;
    uint32_t play_count;
    uint32_t conversation_count;
    uint32_t days_alive;

    ActivityCounters() : bathe_count(0), feed_count(0), play_count(0),
                         conversation_count(0), days_alive(0) {}
};

// Bitmask for unlocked backgrounds (style + festival)
struct UnlockedBackgrounds {
    uint32_t flags;

    UnlockedBackgrounds() : flags(0) {}

    // Bit positions for style backgrounds (0-3)
    static constexpr uint32_t BIT_CYBERPUNK = (1 << 0);   // BG_STYLE_CYBERPUNK = 12
    static constexpr uint32_t BIT_FANTASY = (1 << 1);     // BG_STYLE_FANTASY = 14
    static constexpr uint32_t BIT_SPACE = (1 << 2);       // BG_STYLE_SPACE = 15
    static constexpr uint32_t BIT_STEAMPUNK = (1 << 3);   // BG_STYLE_STEAMPUNK = 13

    // Bit positions for festival backgrounds (4-10)
    static constexpr uint32_t BIT_CHRISTMAS = (1 << 4);   // BG_FESTIVAL_CHRISTMAS = 5
    static constexpr uint32_t BIT_BIRTHDAY = (1 << 5);    // BG_FESTIVAL_BIRTHDAY = 6
    static constexpr uint32_t BIT_SPRING = (1 << 6);      // BG_FESTIVAL_SPRING = 7
    static constexpr uint32_t BIT_NEWYEAR = (1 << 7);     // BG_FESTIVAL_NEWYEAR = 8
    static constexpr uint32_t BIT_MIDAUTUMN = (1 << 8);   // BG_FESTIVAL_MIDAUTUMN = 9
    static constexpr uint32_t BIT_HALLOWEEN = (1 << 9);   // BG_FESTIVAL_HALLOWEEN = 10
    static constexpr uint32_t BIT_VALENTINES = (1 << 10); // BG_FESTIVAL_VALENTINES = 11
};

// Achievement types
enum class AchievementType {
    kBather5,       // Bathe 5 times -> Cyberpunk
    kBather20,      // Bathe 20 times -> Fantasy
    kTalker10,      // 10 conversations -> Space
    kCaretaker7Days // 7 days alive -> Steampunk
};

class PetAchievements {
public:
    static PetAchievements& GetInstance();

    // Initialize (load from NVS)
    void Initialize();

    // Activity tracking
    void OnBathe();
    void OnFeed();
    void OnPlay();
    void OnConversation();
    void OnDayPassed();

    // Check and unlock achievements
    void CheckAchievements();

    // Query unlock status - Style backgrounds
    bool IsBackgroundUnlocked(uint16_t bg_idx) const;
    bool IsCyberpunkUnlocked() const;
    bool IsFantasyUnlocked() const;
    bool IsSpaceUnlocked() const;
    bool IsSteampunkUnlocked() const;

    // Query unlock status - Festival backgrounds
    bool IsChristmasUnlocked() const;
    bool IsBirthdayUnlocked() const;
    bool IsSpringFestivalUnlocked() const;
    bool IsNewYearUnlocked() const;
    bool IsMidAutumnUnlocked() const;
    bool IsHalloweenUnlocked() const;
    bool IsValentinesUnlocked() const;

    // Unlock festival backgrounds (called by MCP)
    void UnlockChristmas();
    void UnlockBirthday();
    void UnlockSpringFestival();
    void UnlockNewYear();
    void UnlockMidAutumn();
    void UnlockHalloween();
    void UnlockValentines();

    // Unlock style backgrounds (for coin purchase)
    void UnlockCyberpunk();
    void UnlockFantasy();
    void UnlockSpace();
    void UnlockSteampunk();

    // Get counters for MCP status
    const ActivityCounters& GetCounters() const { return counters_; }
    const UnlockedBackgrounds& GetUnlocked() const { return unlocked_; }

    // Get list of unlocked background indices
    std::vector<uint16_t> GetUnlockedBackgroundIndices() const;

    // Achievement unlock callback
    using AchievementCallback = std::function<void(AchievementType, const char* bg_name)>;
    void SetAchievementCallback(AchievementCallback cb) { achievement_callback_ = cb; }

private:
    PetAchievements() = default;

    ActivityCounters counters_;
    UnlockedBackgrounds unlocked_;
    AchievementCallback achievement_callback_;

    // Helper methods
    bool IsFlagSet(uint32_t bit) const;
    void UnlockFestival(uint32_t bit, const char* name);

    void UnlockBackground(uint32_t bit, AchievementType type, const char* bg_name);
    void Save();
    void Load();
};

#endif // PET_ACHIEVEMENTS_H

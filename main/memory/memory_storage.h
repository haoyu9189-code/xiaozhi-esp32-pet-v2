#ifndef MEMORY_STORAGE_H
#define MEMORY_STORAGE_H

#include "memory_types.h"
#include <string>
#include <vector>
#include <mutex>
#include <nvs_flash.h>

// Conflict detection result
struct ConflictInfo {
    bool has_conflict;
    Event conflicting_event;
    std::vector<std::string> suggested_times;

    ConflictInfo() : has_conflict(false), conflicting_event() {}
};

class MemoryStorage {
public:
    static MemoryStorage& GetInstance();

    // Initialization
    int Init();

    // User profile
    int GetProfile(UserProfile* profile);
    AUDNAction UpdateProfile(const char* name, const char* birthday,
                             uint8_t age, const char* gender,
                             const char* location);

    // Active user
    std::string GetActiveUser();
    bool SetActiveUser(const char* name);

    // Family members
    int GetFamily(FamilyMember* members, int max_count);
    int GetFamilyCount();
    AUDNAction AddFamilyMember(const char* relation, const char* name,
                               const char* member_type, uint8_t closeness,
                               const char* shared_memory);
    AUDNAction UpdateFamilyMember(const char* name, uint8_t closeness,
                                  const char* shared_memory);
    AUDNAction RemoveFamilyMember(const char* name);

    // Preferences
    int GetPreferences(Preferences* prefs);
    AUDNAction AddPreference(const char* item, bool is_like);
    AUDNAction RemovePreference(const char* item, bool is_like);

    // Events
    int GetEvents(Event* events, int max_count);
    int GetUpcomingEvents(Event* events, int max_count, int days);
    AUDNAction AddEvent(const char* date, const char* event_type,
                        const char* content, uint8_t emotion_type,
                        uint8_t emotion_intensity, uint8_t significance);
    AUDNAction MarkEventReminded(const char* date, const char* event_type);

    // Facts (rolling queue)
    int GetFacts(Fact* facts, int max_count);
    int GetRecentFacts(Fact* facts, int max_count, int days);
    AUDNAction AddFact(const char* content);

    // Traits
    int GetTraits(Trait* traits, int max_count);
    int GetTraitsByCategory(const char* category, Trait* traits, int max_count);
    AUDNAction AddTrait(const char* category, const char* content);
    AUDNAction RemoveTrait(const char* content);

    // Habits
    int GetHabits(Habit* habits, int max_count);
    AUDNAction AddHabit(const char* content, const char* frequency);
    AUDNAction RemoveHabit(const char* content);

    // Special moments
    int GetMoments(SpecialMoment* moments, int max_count);
    int GetRecentMoments(SpecialMoment* moments, int max_count, int days);
    AUDNAction AddMoment(const char* topic, const char* content,
                         uint8_t emotion_type, uint8_t emotion_intensity,
                         uint8_t importance);

    // Goals
    int GetGoals(PersonalGoal* goals, int max_count);
    int GetActiveGoals(PersonalGoal* goals, int max_count);
    AUDNAction AddGoal(const char* content, uint8_t category,
                       uint8_t priority);
    AUDNAction UpdateGoal(const char* content, uint8_t progress, uint8_t status);

    // Schedule management (日程管理)
    bool AddEvent(const Event& event);
    std::vector<Event> GetEventsCopy();  // Returns a copy to ensure thread safety
    bool DeleteSchedule(const std::string& content);
    void AutoCleanCompletedSchedules();
    std::vector<Event> GetUpcomingSchedules(int minutes_ahead);
    bool MarkScheduleReminded(const std::string& content);
    bool CompleteSchedule(const std::string& content);  // Mark schedule as completed, generate next if repeating
    void GenerateNextRepeatSchedule(const Event& completed_event);
    ConflictInfo CheckScheduleConflict(const char* date, const char* time, int duration_minutes = 60);

    // Queries
    int GetSummary(char* buffer, int buffer_size);
    int Search(const char* keyword, char* buffer, int buffer_size);

    // Data management
    bool EraseAll();
    void Flush();

private:
    MemoryStorage() = default;
    ~MemoryStorage();

    nvs_handle_t nvs_handle_ = 0;
    std::mutex mutex_;
    bool initialized_ = false;

    // Cache
    UserProfile profile_cache_;
    std::vector<FamilyMember> family_cache_;
    Preferences prefs_cache_;
    std::vector<Event> events_cache_;
    std::vector<Fact> facts_cache_;
    std::vector<Trait> traits_cache_;
    std::vector<Habit> habits_cache_;
    std::vector<SpecialMoment> moments_cache_;
    std::vector<PersonalGoal> goals_cache_;

    // State flags
    bool profile_loaded_ = false;
    bool profile_dirty_ = false;
    bool family_loaded_ = false;
    bool family_dirty_ = false;
    bool prefs_loaded_ = false;
    bool prefs_dirty_ = false;
    bool events_loaded_ = false;
    bool events_dirty_ = false;
    bool facts_loaded_ = false;
    bool facts_dirty_ = false;
    bool traits_loaded_ = false;
    bool traits_dirty_ = false;
    bool habits_loaded_ = false;
    bool habits_dirty_ = false;
    bool moments_loaded_ = false;
    bool moments_dirty_ = false;
    bool goals_loaded_ = false;
    bool goals_dirty_ = false;

    // Internal methods
    void LoadProfile();
    void SaveProfile();
    void LoadFamily();
    void SaveFamily();
    void LoadPrefs();
    void SavePrefs();
    void LoadEvents();
    void SaveEvents();
    void LoadFacts();
    void SaveFacts();
    void LoadTraits();
    void SaveTraits();
    void LoadHabits();
    void SaveHabits();
    void LoadMoments();
    void SaveMoments();
    void LoadGoals();
    void SaveGoals();
};

#endif // MEMORY_STORAGE_H

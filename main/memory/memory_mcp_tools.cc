#include "memory_mcp_tools.h"
#include "memory_storage.h"
#include "pending_memory.h"
#include "memory_archive.h"
#include "mcp_server.h"
#include <esp_log.h>
#include <esp_timer.h>
#include <cstring>

#define TAG "MemMcpTools"

// Convert AUDNAction to string
static const char* ActionToString(AUDNAction action) {
    switch (action) {
        case AUDNAction::ADDED: return "added";
        case AUDNAction::UPDATED: return "updated";
        case AUDNAction::DELETED: return "deleted";
        case AUDNAction::NOOP: return "no_change";
        default: return "unknown";
    }
}

// Build JSON response for "read" action with optional type filter
static cJSON* BuildReadResponse(const std::string& type_filter = "") {
    int64_t start_time = esp_timer_get_time();
    auto& storage = MemoryStorage::GetInstance();

    cJSON* root = cJSON_CreateObject();
    bool include_all = type_filter.empty();

    // Profile
    if (include_all || type_filter == "profile") {
        UserProfile profile;
        storage.GetProfile(&profile);
        cJSON* profile_json = cJSON_CreateObject();
        if (strlen(profile.name) > 0) {
            cJSON_AddStringToObject(profile_json, "name", profile.name);
        }
        if (strlen(profile.birthday) > 0) {
            cJSON_AddStringToObject(profile_json, "birthday", profile.birthday);
        }
        if (profile.age > 0) {
            cJSON_AddNumberToObject(profile_json, "age", profile.age);
        }
        if (!include_all || strlen(profile.gender) > 0) {
            cJSON_AddStringToObject(profile_json, "gender", profile.gender);
        }
        if (!include_all || strlen(profile.location) > 0) {
            cJSON_AddStringToObject(profile_json, "location", profile.location);
        }
        cJSON_AddItemToObject(root, "profile", profile_json);
    }

    // Preferences
    if (include_all || type_filter == "preferences" || type_filter == "like" || type_filter == "dislike") {
        Preferences prefs;
        storage.GetPreferences(&prefs);
        cJSON* prefs_json = cJSON_CreateObject();
        cJSON* likes = cJSON_CreateArray();
        cJSON* dislikes = cJSON_CreateArray();
        for (int i = 0; i < prefs.likes_count; i++) {
            cJSON_AddItemToArray(likes, cJSON_CreateString(prefs.likes[i]));
        }
        for (int i = 0; i < prefs.dislikes_count; i++) {
            cJSON_AddItemToArray(dislikes, cJSON_CreateString(prefs.dislikes[i]));
        }
        cJSON_AddItemToObject(prefs_json, "likes", likes);
        cJSON_AddItemToObject(prefs_json, "dislikes", dislikes);
        cJSON_AddItemToObject(root, "preferences", prefs_json);
    }

    // Family
    if (include_all || type_filter == "family") {
        FamilyMember members[MAX_FAMILY_MEMBERS];
        int family_count = storage.GetFamily(members, MAX_FAMILY_MEMBERS);
        if (family_count > 0) {
            cJSON* family = cJSON_CreateArray();
            for (int i = 0; i < family_count; i++) {
                cJSON* member = cJSON_CreateObject();
                cJSON_AddStringToObject(member, "relation", members[i].relation);
                cJSON_AddStringToObject(member, "name", members[i].name);
                if (type_filter == "family") {
                    if (members[i].closeness > 0) {
                        cJSON_AddNumberToObject(member, "closeness", members[i].closeness);
                    }
                    if (strlen(members[i].shared_memory) > 0) {
                        cJSON_AddStringToObject(member, "memory", members[i].shared_memory);
                    }
                }
                cJSON_AddItemToArray(family, member);
            }
            cJSON_AddItemToObject(root, "family", family);
        }
    }

    // Facts
    if (include_all || type_filter == "fact") {
        Fact facts[MAX_FACTS];
        int days = (type_filter == "fact") ? 30 : 7;
        int fact_count = storage.GetRecentFacts(facts, MAX_FACTS, days);
        if (fact_count > 0) {
            cJSON* facts_json = cJSON_CreateArray();
            for (int i = 0; i < fact_count; i++) {
                cJSON_AddItemToArray(facts_json, cJSON_CreateString(facts[i].content));
            }
            cJSON_AddItemToObject(root, "facts", facts_json);
        }
    }

    // Traits
    if (include_all || type_filter == "trait") {
        Trait traits[MAX_TRAITS];
        int trait_count = storage.GetTraits(traits, MAX_TRAITS);
        if (trait_count > 0) {
            cJSON* traits_json = cJSON_CreateArray();
            for (int i = 0; i < trait_count; i++) {
                cJSON* trait = cJSON_CreateObject();
                cJSON_AddStringToObject(trait, "category", traits[i].category);
                cJSON_AddStringToObject(trait, "content", traits[i].content);
                cJSON_AddItemToArray(traits_json, trait);
            }
            cJSON_AddItemToObject(root, "traits", traits_json);
        }
    }

    // Habits
    if (include_all || type_filter == "habit") {
        Habit habits[MAX_HABITS];
        int habit_count = storage.GetHabits(habits, MAX_HABITS);
        if (habit_count > 0) {
            cJSON* habits_json = cJSON_CreateArray();
            for (int i = 0; i < habit_count; i++) {
                cJSON* habit = cJSON_CreateObject();
                cJSON_AddStringToObject(habit, "content", habits[i].content);
                if (type_filter == "habit") {
                    cJSON_AddStringToObject(habit, "frequency", habits[i].frequency);
                }
                cJSON_AddItemToArray(habits_json, habit);
            }
            cJSON_AddItemToObject(root, "habits", habits_json);
        }
    }

    // Events
    if (type_filter == "event") {
        Event events[MAX_EVENTS];
        int event_count = storage.GetUpcomingEvents(events, MAX_EVENTS, 30);
        if (event_count > 0) {
            cJSON* events_json = cJSON_CreateArray();
            for (int i = 0; i < event_count; i++) {
                cJSON* event = cJSON_CreateObject();
                cJSON_AddStringToObject(event, "date", events[i].date);
                cJSON_AddStringToObject(event, "type", events[i].event_type);
                cJSON_AddStringToObject(event, "content", events[i].content);
                cJSON_AddItemToArray(events_json, event);
            }
            cJSON_AddItemToObject(root, "events", events_json);
        }
    }

    // Goals
    if (type_filter == "goal") {
        PersonalGoal goals[MAX_GOALS];
        int goal_count = storage.GetActiveGoals(goals, MAX_GOALS);
        if (goal_count > 0) {
            cJSON* goals_json = cJSON_CreateArray();
            for (int i = 0; i < goal_count; i++) {
                cJSON* goal = cJSON_CreateObject();
                cJSON_AddStringToObject(goal, "content", goals[i].content);
                cJSON_AddNumberToObject(goal, "progress", goals[i].progress);
                cJSON_AddNumberToObject(goal, "priority", goals[i].priority);
                cJSON_AddItemToArray(goals_json, goal);
            }
            cJSON_AddItemToObject(root, "goals", goals_json);
        }
    }

    // Moments
    if (include_all || type_filter == "moment") {
        SpecialMoment moments[MAX_MOMENTS];
        int days = (type_filter == "moment") ? 30 : 7;
        int moment_count = storage.GetRecentMoments(moments, MAX_MOMENTS, days);
        if (moment_count > 0) {
            cJSON* moments_json = cJSON_CreateArray();
            int limit = (type_filter == "moment") ? moment_count : ((moment_count > 3) ? 3 : moment_count);
            for (int i = 0; i < limit; i++) {
                cJSON* moment = cJSON_CreateObject();
                cJSON_AddStringToObject(moment, "topic", moments[i].topic);
                cJSON_AddStringToObject(moment, "content", moments[i].content);
                cJSON_AddNumberToObject(moment, "importance", moments[i].importance);
                cJSON_AddItemToArray(moments_json, moment);
            }
            cJSON_AddItemToObject(root, "moments", moments_json);
        }
    }

    // Schedules (always sort by priority)
    if (include_all || type_filter == "schedule") {
        auto all_events = storage.GetEventsCopy();  // Thread-safe copy
        std::vector<Event> pending_schedules;
        pending_schedules.reserve(16);
        for (const auto& event : all_events) {
            if (IsSchedule(event) && !IsCompleted(event)) {
                pending_schedules.push_back(event);
            }
        }

        if (!pending_schedules.empty()) {
            // Sort by priority (significance) descending, then date + time ascending
            std::sort(pending_schedules.begin(), pending_schedules.end(),
                [](const Event& a, const Event& b) {
                    if (a.significance != b.significance) {
                        return a.significance > b.significance;
                    }
                    int date_cmp = strcmp(a.date, b.date);
                    if (date_cmp != 0) return date_cmp < 0;
                    return strcmp(a.time, b.time) < 0;
                });

            cJSON* schedules = cJSON_CreateArray();
            // Limit only for overview (include_all), show all for specific query
            size_t schedule_limit = (type_filter == "schedule") ? pending_schedules.size() :
                                   ((pending_schedules.size() > 8) ? 8 : pending_schedules.size());
            for (size_t i = 0; i < schedule_limit; i++) {
                const auto& schedule = pending_schedules[i];
                cJSON* item = cJSON_CreateObject();
                std::string datetime = std::string(schedule.date) + " " + std::string(schedule.time);
                cJSON_AddStringToObject(item, "datetime", datetime.c_str());
                cJSON_AddStringToObject(item, "content", schedule.content);
                cJSON_AddNumberToObject(item, "priority", schedule.significance);
                const char* priority_label = (schedule.significance >= 4) ? "high" :
                                            (schedule.significance >= 3) ? "medium" : "low";
                cJSON_AddStringToObject(item, "priority_label", priority_label);

                // Add repeat info for specific query
                if (type_filter == "schedule" && IsRepeating(schedule)) {
                    uint8_t repeat_type = GetRepeatType(schedule);
                    const char* repeat_str = (repeat_type == REPEAT_DAILY) ? "daily" :
                                            (repeat_type == REPEAT_WEEKLY) ? "weekly" :
                                            (repeat_type == REPEAT_MONTHLY) ? "monthly" : "none";
                    cJSON_AddStringToObject(item, "repeat", repeat_str);
                }

                cJSON_AddItemToArray(schedules, item);
            }
            cJSON_AddItemToObject(root, "schedules", schedules);
        }
    }

    int64_t end_time = esp_timer_get_time();
    int elapsed_ms = (int)((end_time - start_time) / 1000);
    ESP_LOGI(TAG, "BuildReadResponse(type='%s') took %d ms",
             type_filter.empty() ? "all" : type_filter.c_str(),
             elapsed_ms);

    return root;
}

// Build ExtractedMemory from type and content
static bool BuildExtractedMemory(const std::string& type, const std::string& content,
                                  ExtractedMemory& mem, uint8_t confidence = 4) {
    memset(&mem, 0, sizeof(mem));
    mem.confidence = confidence;

    if (type == "name" || type == "age" || type == "birthday" ||
        type == "gender" || type == "location") {
        mem.type = ExtractedType::IDENTITY;
        strncpy(mem.category, type.c_str(), sizeof(mem.category) - 1);
        strncpy(mem.content, content.c_str(), sizeof(mem.content) - 1);
        return true;
    } else if (type == "like") {
        mem.type = ExtractedType::PREFERENCE;
        strncpy(mem.category, "like", sizeof(mem.category) - 1);
        strncpy(mem.content, content.c_str(), sizeof(mem.content) - 1);
        return true;
    } else if (type == "dislike") {
        mem.type = ExtractedType::PREFERENCE;
        strncpy(mem.category, "dislike", sizeof(mem.category) - 1);
        strncpy(mem.content, content.c_str(), sizeof(mem.content) - 1);
        return true;
    } else if (type == "family") {
        mem.type = ExtractedType::FAMILY;
        size_t colon = content.find(':');
        if (colon != std::string::npos) {
            strncpy(mem.category, content.substr(0, colon).c_str(), sizeof(mem.category) - 1);
            strncpy(mem.content, content.substr(colon + 1).c_str(), sizeof(mem.content) - 1);
        } else {
            strncpy(mem.category, "family", sizeof(mem.category) - 1);
            strncpy(mem.content, content.c_str(), sizeof(mem.content) - 1);
        }
        return true;
    } else if (type == "fact") {
        mem.type = ExtractedType::FACT;
        strncpy(mem.category, "fact", sizeof(mem.category) - 1);
        strncpy(mem.content, content.c_str(), sizeof(mem.content) - 1);
        return true;
    }
    return false;
}

// Apply memory to storage directly
static AUDNAction ApplyMemoryToStorage(const std::string& type, const std::string& content) {
    auto& storage = MemoryStorage::GetInstance();

    if (type == "name") {
        return storage.UpdateProfile(content.c_str(), nullptr, 0, nullptr, nullptr);
    } else if (type == "age") {
        int age = atoi(content.c_str());
        if (age > 0) {
            return storage.UpdateProfile(nullptr, nullptr, age, nullptr, nullptr);
        }
        return AUDNAction::NOOP;
    } else if (type == "birthday") {
        return storage.UpdateProfile(nullptr, content.c_str(), 0, nullptr, nullptr);
    } else if (type == "gender") {
        return storage.UpdateProfile(nullptr, nullptr, 0, content.c_str(), nullptr);
    } else if (type == "location") {
        return storage.UpdateProfile(nullptr, nullptr, 0, nullptr, content.c_str());
    } else if (type == "like") {
        return storage.AddPreference(content.c_str(), true);
    } else if (type == "dislike") {
        return storage.AddPreference(content.c_str(), false);
    } else if (type == "family") {
        // Format: "relation:name" or "relation:name:memory"
        size_t colon1 = content.find(':');
        if (colon1 != std::string::npos) {
            std::string relation = content.substr(0, colon1);
            std::string rest = content.substr(colon1 + 1);
            size_t colon2 = rest.find(':');
            if (colon2 != std::string::npos) {
                std::string name = rest.substr(0, colon2);
                std::string memory = rest.substr(colon2 + 1);
                return storage.AddFamilyMember(relation.c_str(), name.c_str(), nullptr, 3, memory.c_str());
            } else {
                return storage.AddFamilyMember(relation.c_str(), rest.c_str(), nullptr, 3, nullptr);
            }
        } else {
            return storage.AddFamilyMember("family", content.c_str(), nullptr, 3, nullptr);
        }
    } else if (type == "fact") {
        return storage.AddFact(content.c_str());
    } else if (type == "trait") {
        size_t colon = content.find(':');
        if (colon != std::string::npos) {
            return storage.AddTrait(content.substr(0, colon).c_str(), content.substr(colon + 1).c_str());
        } else {
            return storage.AddTrait("other", content.c_str());
        }
    } else if (type == "habit") {
        size_t colon = content.find(':');
        if (colon != std::string::npos) {
            return storage.AddHabit(content.substr(0, colon).c_str(), content.substr(colon + 1).c_str());
        } else {
            return storage.AddHabit(content.c_str(), "occasionally");
        }
    } else if (type == "event") {
        size_t colon1 = content.find(':');
        if (colon1 != std::string::npos) {
            std::string date = content.substr(0, colon1);
            std::string rest = content.substr(colon1 + 1);
            size_t colon2 = rest.find(':');
            if (colon2 != std::string::npos) {
                return storage.AddEvent(date.c_str(), rest.substr(0, colon2).c_str(),
                                        rest.substr(colon2 + 1).c_str(), 0, 0, 3);
            } else {
                return storage.AddEvent(date.c_str(), "reminder", rest.c_str(), 0, 0, 3);
            }
        }
    } else if (type == "goal") {
        size_t colon = content.find(':');
        if (colon != std::string::npos) {
            int priority = atoi(content.substr(colon + 1).c_str());
            if (priority < 1 || priority > 5) priority = 3;
            return storage.AddGoal(content.substr(0, colon).c_str(), (uint8_t)GoalCategory::OTHER, priority);
        } else {
            return storage.AddGoal(content.c_str(), (uint8_t)GoalCategory::OTHER, 3);
        }
    } else if (type == "moment") {
        size_t colon1 = content.find(':');
        if (colon1 != std::string::npos) {
            std::string topic = content.substr(0, colon1);
            std::string rest = content.substr(colon1 + 1);
            size_t colon2 = rest.find(':');
            if (colon2 != std::string::npos) {
                int importance = atoi(rest.substr(colon2 + 1).c_str());
                if (importance < 1 || importance > 5) importance = 3;
                return storage.AddMoment(topic.c_str(), rest.substr(0, colon2).c_str(), 0, 0, importance);
            } else {
                return storage.AddMoment(topic.c_str(), rest.c_str(), 0, 0, 3);
            }
        }
    }

    // Default to fact
    return storage.AddFact(content.c_str());
}

// Handle "write" action - with PendingMemory confirmation support
// Handle "complete_schedule" action
static std::string HandleCompleteSchedule(const std::string& content) {
    if (content.empty()) {
        return "error: content is required";
    }

    auto& storage = MemoryStorage::GetInstance();

    // Use thread-safe CompleteSchedule method
    if (storage.CompleteSchedule(content)) {
        return "completed: schedule '" + content + "'";
    }
    return "error: schedule not found or already completed";
}

static std::string HandleWrite(const std::string& type, const std::string& content, bool force) {
    if (content.empty()) {
        return "error: content is required";
    }

    // Handle schedule type (日程)
    if (type == "schedule") {
        // Schedule 需要额外的 datetime 参数，这里先返回错误，实际处理在工具注册处
        return "error: schedule requires datetime parameter, use memory(action='write', type='schedule', content='...', datetime='YYYY-MM-DD HH:MM')";
    }

    // Types that support pending confirmation
    bool supports_pending = (type == "name" || type == "age" || type == "birthday" ||
                             type == "gender" || type == "location" || type == "like" ||
                             type == "dislike" || type == "family" || type == "fact");

    // If force=true or type doesn't support pending, save directly
    if (force || !supports_pending) {
        AUDNAction result = ApplyMemoryToStorage(type, content);
        return std::string(ActionToString(result)) + ": " + content;
    }

    // Use PendingMemory confirmation system
    ExtractedMemory mem;
    if (!BuildExtractedMemory(type, content, mem, 4)) {
        // Fallback to direct save if can't build ExtractedMemory
        AUDNAction result = ApplyMemoryToStorage(type, content);
        return std::string(ActionToString(result)) + ": " + content;
    }

    auto& pending = PendingMemory::GetInstance();
    bool confirmed = pending.AddOrConfirm(mem);

    if (confirmed) {
        // Memory confirmed! Apply to storage
        AUDNAction result = ApplyMemoryToStorage(type, content);
        pending.Save();
        return std::string("confirmed_") + ActionToString(result) + ": " + content;
    } else {
        // Memory pending confirmation
        pending.Save();
        return std::string("pending: ") + content + " (needs more mentions to confirm)";
    }
}

// Handle "delete" action
static std::string HandleDelete(const std::string& type, const std::string& content) {
    if (content.empty()) {
        return "error: content is required";
    }

    auto& storage = MemoryStorage::GetInstance();
    AUDNAction result = AUDNAction::NOOP;

    if (type == "like") {
        result = storage.RemovePreference(content.c_str(), true);
    } else if (type == "dislike") {
        result = storage.RemovePreference(content.c_str(), false);
    } else if (type == "family") {
        result = storage.RemoveFamilyMember(content.c_str());
    } else if (type == "trait") {
        result = storage.RemoveTrait(content.c_str());
    } else if (type == "habit") {
        result = storage.RemoveHabit(content.c_str());
    } else if (type == "schedule") {
        if (storage.DeleteSchedule(content)) {
            return "deleted: schedule '" + content + "'";
        } else {
            return "error: schedule not found";
        }
    } else if (type == "all") {
        if (storage.EraseAll()) {
            return "deleted: all memory data";
        } else {
            return "error: failed to erase all data";
        }
    } else {
        return "error: unsupported delete type. Use: like, dislike, family, trait, habit, all";
    }

    return std::string(ActionToString(result)) + ": " + content;
}

// Handle "search" action
static std::string HandleSearch(const std::string& keyword) {
    if (keyword.empty()) {
        return "error: keyword is required";
    }

    auto& storage = MemoryStorage::GetInstance();
    char buffer[512];
    int len = storage.Search(keyword.c_str(), buffer, sizeof(buffer));

    if (len > 0) {
        return std::string(buffer, len);
    } else {
        return "no results found for: " + keyword;
    }
}

// Stage 2: Recall archived memories
static cJSON* HandleRecall(const std::string& type, const std::string& start_date,
                           const std::string& end_date, const std::string& keyword, int limit) {
    auto& archive = MemoryArchive::GetInstance();

    if (!archive.IsInitialized()) {
        cJSON* error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Archive not initialized");
        return error;
    }

    if (type.empty()) {
        cJSON* error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "type parameter is required (fact/moment/event)");
        return error;
    }

    std::vector<ArchivedItem> results;

    // Determine recall method based on parameters
    if (!keyword.empty()) {
        // Keyword search
        ESP_LOGI("MemMcpTools", "Recalling by keyword: type=%s, keyword='%s', limit=%d",
                 type.c_str(), keyword.c_str(), limit);
        results = archive.RecallByKeyword(type.c_str(), keyword.c_str(), limit);
    }
    else if (!start_date.empty() || !end_date.empty()) {
        // Time range search
        ESP_LOGI("MemMcpTools", "Recalling by time range: type=%s, start='%s', end='%s', limit=%d",
                 type.c_str(), start_date.c_str(), end_date.c_str(), limit);
        results = archive.RecallByTimeRange(type.c_str(),
                                           start_date.empty() ? nullptr : start_date.c_str(),
                                           end_date.empty() ? nullptr : end_date.c_str(),
                                           limit);
    }
    else {
        // Recent items (default)
        ESP_LOGI("MemMcpTools", "Recalling recent: type=%s, limit=%d", type.c_str(), limit);
        results = archive.RecallRecent(type.c_str(), limit);
    }

    // Build JSON response
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", type.c_str());
    cJSON_AddNumberToObject(root, "count", (int)results.size());

    cJSON* items = cJSON_CreateArray();
    for (const auto& item : results) {
        // Parse the JSON content
        cJSON* content_json = cJSON_Parse(item.content);
        if (content_json) {
            cJSON_AddItemToArray(items, content_json);
        }
    }
    cJSON_AddItemToObject(root, "items", items);

    ESP_LOGI("MemMcpTools", "Recalled %d %s items from archive", (int)results.size(), type.c_str());

    return root;
}

void RegisterMemoryMcpTools(McpServer& mcp_server) {
    ESP_LOGI(TAG, "Registering memory MCP tool");

    // Initialize PendingMemory
    PendingMemory::GetInstance().Init();

    // Single unified memory tool
    mcp_server.AddTool(
        "memory",
        "Memory management tool. Use this to remember, recall, or delete information about the user.\n"
        "Actions:\n"
        "- read: Get memory info. Use 'type' parameter to filter specific data type.\n"
        "  Types: profile, preferences, family, fact, trait, habit, event, goal, moment, schedule\n"
        "  Without type: returns overview (profile + recent facts/moments + top 8 schedules)\n"
        "  With type: returns ALL data of that specific type\n"
        "- write: Remember new info\n"
        "  Types: name, age, birthday, gender, location, like, dislike, family, fact, trait, habit, event, goal, moment, schedule\n"
        "  Options:\n"
        "    force: Set to true when user explicitly asks to remember (触发词: 记住、别忘了、请记住). Default false.\n"
        "           Without force, info needs 2 mentions to be permanently saved.\n"
        "    datetime: Required for schedule type (format: YYYY-MM-DD HH:MM)\n"
        "    repeat: Optional for schedule type. Values: daily, weekly, monthly. Default: none.\n"
        "  Formats:\n"
        "    family: 'relation:name' or 'relation:name:memory'\n"
        "    trait: 'category:content' (categories: personality/appearance/ability/other)\n"
        "    habit: 'content' or 'content:frequency' (daily/weekly/occasionally)\n"
        "    event: 'MM-DD:type:content' (e.g. '03-15:birthday:妈妈生日')\n"
        "    goal: 'content' or 'content:priority(1-5)'\n"
        "    moment: 'topic:content' or 'topic:content:importance(1-5)'\n"
        "    schedule: content='title', datetime='YYYY-MM-DD HH:MM', repeat='daily/weekly/monthly' (optional)\n"
        "- delete: Remove saved info. Types: like, dislike, family, trait, habit, schedule, all\n"
        "- complete_schedule: Mark a schedule as completed. Requires content (title of the schedule)\n"
        "- search: Search memories by keyword\n"
        "- recall: Retrieve archived memories from long-term storage\n"
        "  Required: type (fact/moment/event)\n"
        "  Optional: keyword (search text), start_date (YYYY-MM-DD), end_date (YYYY-MM-DD), limit (default 10)\n"
        "  Methods: (1) By keyword: recall(action='recall', type='fact', keyword='北京')\n"
        "           (2) By time range: recall(action='recall', type='moment', start_date='2025-01-01', end_date='2025-12-31')\n"
        "           (3) Recent items: recall(action='recall', type='fact', limit=20)\n"
        "Examples:\n"
        "- memory(action='read') -> overview: profile + recent data + top 8 schedules\n"
        "- memory(action='read', type='schedule') -> ALL pending schedules (full list)\n"
        "- memory(action='read', type='preferences') -> all likes/dislikes\n"
        "- memory(action='read', type='family') -> all family members with full details\n"
        "- memory(action='write', type='name', content='小明') -> pending, needs confirmation\n"
        "- memory(action='write', type='name', content='小明', force=true) -> saved immediately\n"
        "- memory(action='write', type='family', content='妈妈:张丽:一起去过北京', force=true)\n"
        "- memory(action='write', type='schedule', content='团队会议', datetime='2026-02-05 14:00')\n"
        "- memory(action='write', type='schedule', content='每日晨练', datetime='2026-02-03 08:00', repeat='daily')\n"
        "- memory(action='complete_schedule', content='团队会议')\n"
        "- memory(action='delete', type='schedule', content='团队会议')\n"
        "- memory(action='delete', type='like', content='音乐')\n"
        "- memory(action='search', content='妈妈')\n"
        "- memory(action='recall', type='fact', keyword='北京') -> search archived facts containing '北京'\n"
        "- memory(action='recall', type='moment', start_date='2025-01-01', end_date='2025-12-31') -> retrieve moments from 2025\n"
        "- memory(action='recall', type='fact', limit=20) -> get 20 most recent archived facts",
        PropertyList({
            Property("action", kPropertyTypeString),
            Property("type", kPropertyTypeString, std::string("")),
            Property("content", kPropertyTypeString, std::string("")),
            Property("force", kPropertyTypeBoolean, false),
            Property("datetime", kPropertyTypeString, std::string("")),
            Property("repeat", kPropertyTypeString, std::string("")),
            Property("keyword", kPropertyTypeString, std::string("")),
            Property("start_date", kPropertyTypeString, std::string("")),
            Property("end_date", kPropertyTypeString, std::string("")),
            Property("limit", kPropertyTypeInteger, 10)
        }),
        [](const PropertyList& props) -> ReturnValue {
            std::string action = props["action"].value<std::string>();
            ESP_LOGI("MemMcpTools", "Memory tool called: action=%s", action.c_str());

            if (action == "read") {
                std::string type_filter = props["type"].value<std::string>();
                return BuildReadResponse(type_filter);
            }
            else if (action == "write") {
                std::string type = props["type"].value<std::string>();
                std::string content = props["content"].value<std::string>();
                bool force = props["force"].value<bool>();
                std::string datetime_str = props["datetime"].value<std::string>();
                std::string repeat_str = props["repeat"].value<std::string>();

                // Special handling for schedule type
                if (type == "schedule") {
                    ESP_LOGI("MemMcpTools", "Schedule write request: content='%s', datetime='%s', repeat='%s'",
                             content.c_str(), datetime_str.c_str(), repeat_str.c_str());

                    if (datetime_str.empty()) {
                        ESP_LOGW("MemMcpTools", "Schedule write failed: datetime is empty");
                        return std::string("error: datetime is required for schedule (format: YYYY-MM-DD HH:MM)");
                    }

                    // Split datetime into date and time
                    size_t space_pos = datetime_str.find(' ');
                    if (space_pos == std::string::npos || datetime_str.length() < 16) {
                        return std::string("error: datetime format should be YYYY-MM-DD HH:MM");
                    }

                    std::string date = datetime_str.substr(0, space_pos);
                    std::string time = datetime_str.substr(space_pos + 1);

                    // Parse repeat type
                    uint8_t repeat_type = REPEAT_NONE;
                    if (!repeat_str.empty()) {
                        if (repeat_str == "daily") {
                            repeat_type = REPEAT_DAILY;
                        } else if (repeat_str == "weekly") {
                            repeat_type = REPEAT_WEEKLY;
                        } else if (repeat_str == "monthly") {
                            repeat_type = REPEAT_MONTHLY;
                        } else {
                            return std::string("error: invalid repeat type. Use: daily, weekly, or monthly");
                        }
                    }

                    // Create schedule event
                    Event event;
                    strncpy(event.date, date.c_str(), sizeof(event.date) - 1);
                    event.date[sizeof(event.date) - 1] = '\0';
                    strncpy(event.time, time.c_str(), sizeof(event.time) - 1);
                    event.time[sizeof(event.time) - 1] = '\0';
                    strncpy(event.event_type, "schedule", sizeof(event.event_type) - 1);
                    event.event_type[sizeof(event.event_type) - 1] = '\0';
                    strncpy(event.content, content.c_str(), sizeof(event.content) - 1);
                    event.content[sizeof(event.content) - 1] = '\0';
                    SetSchedule(event, true);
                    SetRepeatType(event, repeat_type);
                    event.reminded = 0;
                    event.significance = 3;
                    event.repeat_interval = 1;  // Currently only support interval of 1

                    auto& storage = MemoryStorage::GetInstance();

                    // Check for conflicts
                    ESP_LOGI("MemMcpTools", "Checking schedule conflict for %s %s...", date.c_str(), time.c_str());
                    int64_t start_time = esp_timer_get_time();
                    ConflictInfo conflict = storage.CheckScheduleConflict(date.c_str(), time.c_str());
                    int64_t elapsed_us = esp_timer_get_time() - start_time;
                    ESP_LOGI("MemMcpTools", "Conflict check took %d us", (int)elapsed_us);
                    if (conflict.has_conflict) {
                        std::string msg = "conflict: '" + content + "' at " + datetime_str +
                                         " conflicts with '" + std::string(conflict.conflicting_event.content) + "'";
                        if (!conflict.suggested_times.empty()) {
                            msg += ". Suggested times: ";
                            for (size_t i = 0; i < conflict.suggested_times.size(); i++) {
                                if (i > 0) msg += ", ";
                                msg += conflict.suggested_times[i];
                            }
                        }
                        return msg;
                    }

                    ESP_LOGI("MemMcpTools", "Adding schedule to storage...");
                    ESP_LOGI("MemMcpTools", "Calling storage.AddEvent() - before");
                    bool add_result = storage.AddEvent(event);
                    ESP_LOGI("MemMcpTools", "Calling storage.AddEvent() - after, result=%d", add_result);
                    if (add_result) {
                        std::string msg = "added: schedule '" + content + "' at " + datetime_str;
                        if (repeat_type != REPEAT_NONE) {
                            msg += " (repeating: " + repeat_str + ")";
                        }
                        ESP_LOGI("MemMcpTools", "Schedule added successfully: %s", msg.c_str());
                        return msg;
                    } else {
                        ESP_LOGE("MemMcpTools", "Failed to add schedule (storage full)");
                        return "error: failed to add schedule (storage full)";
                    }
                }

                return HandleWrite(type, content, force);
            }
            else if (action == "delete") {
                std::string type = props["type"].value<std::string>();
                std::string content = props["content"].value<std::string>();
                return HandleDelete(type, content);
            }
            else if (action == "search") {
                std::string content = props["content"].value<std::string>();
                return HandleSearch(content);
            }
            else if (action == "complete_schedule") {
                std::string content = props["content"].value<std::string>();
                return HandleCompleteSchedule(content);
            }
            else if (action == "recall") {
                std::string type = props["type"].value<std::string>();
                std::string keyword = props["keyword"].value<std::string>();
                std::string start_date = props["start_date"].value<std::string>();
                std::string end_date = props["end_date"].value<std::string>();
                int limit = props["limit"].value<int>();
                return HandleRecall(type, start_date, end_date, keyword, limit);
            }
            else {
                return std::string("Unknown action. Use: read, write, delete, search, complete_schedule, recall");
            }
        }
    );

    ESP_LOGI(TAG, "Memory tool registered");
}

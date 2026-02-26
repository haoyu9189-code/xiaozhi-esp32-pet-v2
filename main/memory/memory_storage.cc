#include "memory_storage.h"
#include "memory_archive.h"
#include <esp_log.h>
#include <cstring>
#include <ctime>
#include <algorithm>

#define TAG "Memory"

namespace {
    const char* NVS_NAMESPACE = "memory";
    const char* KEY_PROFILE = "profile";
    const char* KEY_FAMILY = "family";
    const char* KEY_PREFS = "prefs";
    const char* KEY_EVENTS = "events";
    const char* KEY_FACTS = "facts";
    const char* KEY_TRAITS = "traits";
    const char* KEY_HABITS = "habits";
    const char* KEY_MOMENTS = "moments";
    const char* KEY_GOALS = "goals";
    // const char* KEY_ACTIVE_USER = "active_usr";  // Unused
}

MemoryStorage& MemoryStorage::GetInstance() {
    static MemoryStorage instance;
    return instance;
}

MemoryStorage::~MemoryStorage() {
    Flush();
    if (nvs_handle_ != 0) {
        nvs_close(nvs_handle_);
    }
}

int MemoryStorage::Init() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (initialized_) {
        return 0;
    }

    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return -1;
    }

    initialized_ = true;
    ESP_LOGI(TAG, "Memory storage initialized");
    return 0;
}

void MemoryStorage::Flush() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (profile_dirty_) SaveProfile();
    if (family_dirty_) SaveFamily();
    if (prefs_dirty_) SavePrefs();
    if (events_dirty_) SaveEvents();
    if (facts_dirty_) SaveFacts();
    if (traits_dirty_) SaveTraits();
    if (habits_dirty_) SaveHabits();
    if (moments_dirty_) SaveMoments();
    if (goals_dirty_) SaveGoals();

    nvs_commit(nvs_handle_);
}

// ============== Profile ==============

void MemoryStorage::LoadProfile() {
    if (profile_loaded_) return;

    size_t size = sizeof(UserProfile);
    esp_err_t err = nvs_get_blob(nvs_handle_, KEY_PROFILE, &profile_cache_, &size);
    if (err == ESP_OK && memcmp(profile_cache_.magic, MEMORY_MAGIC_PROFILE, 4) == 0) {
        profile_loaded_ = true;
    } else {
        memset(&profile_cache_, 0, sizeof(UserProfile));
        memcpy(profile_cache_.magic, MEMORY_MAGIC_PROFILE, 4);
        profile_loaded_ = true;
    }
}

void MemoryStorage::SaveProfile() {
    if (!profile_dirty_) return;

    esp_err_t err = nvs_set_blob(nvs_handle_, KEY_PROFILE, &profile_cache_, sizeof(UserProfile));
    if (err == ESP_OK) {
        profile_dirty_ = false;
        ESP_LOGI(TAG, "Profile saved");
    } else {
        ESP_LOGE(TAG, "Failed to save profile: %s", esp_err_to_name(err));
    }
}

int MemoryStorage::GetProfile(UserProfile* profile) {
    std::lock_guard<std::mutex> lock(mutex_);
    LoadProfile();
    if (profile) {
        memcpy(profile, &profile_cache_, sizeof(UserProfile));
    }
    return 0;
}

AUDNAction MemoryStorage::UpdateProfile(const char* name, const char* birthday,
                                         uint8_t age, const char* gender,
                                         const char* location) {
    std::lock_guard<std::mutex> lock(mutex_);
    LoadProfile();

    bool changed = false;

    if (name && strlen(name) > 0) {
        if (strncmp(profile_cache_.name, name, sizeof(profile_cache_.name)) != 0) {
            strncpy(profile_cache_.name, name, sizeof(profile_cache_.name) - 1);
            changed = true;
        }
    }
    if (birthday && strlen(birthday) > 0) {
        if (strncmp(profile_cache_.birthday, birthday, sizeof(profile_cache_.birthday)) != 0) {
            strncpy(profile_cache_.birthday, birthday, sizeof(profile_cache_.birthday) - 1);
            changed = true;
        }
    }
    if (age > 0 && profile_cache_.age != age) {
        profile_cache_.age = age;
        changed = true;
    }
    if (gender && strlen(gender) > 0) {
        if (strncmp(profile_cache_.gender, gender, sizeof(profile_cache_.gender)) != 0) {
            strncpy(profile_cache_.gender, gender, sizeof(profile_cache_.gender) - 1);
            changed = true;
        }
    }
    if (location && strlen(location) > 0) {
        if (strncmp(profile_cache_.location, location, sizeof(profile_cache_.location)) != 0) {
            strncpy(profile_cache_.location, location, sizeof(profile_cache_.location) - 1);
            changed = true;
        }
    }

    if (changed) {
        profile_dirty_ = true;
        SaveProfile();
        nvs_commit(nvs_handle_);
        return AUDNAction::UPDATED;
    }
    return AUDNAction::NOOP;
}

std::string MemoryStorage::GetActiveUser() {
    std::lock_guard<std::mutex> lock(mutex_);
    LoadProfile();
    return std::string(profile_cache_.name);
}

bool MemoryStorage::SetActiveUser(const char* name) {
    return UpdateProfile(name, nullptr, 0, nullptr, nullptr) != AUDNAction::NOOP;
}

// ============== Family ==============

void MemoryStorage::LoadFamily() {
    if (family_loaded_) return;

    FamilyMember members[MAX_FAMILY_MEMBERS];
    size_t size = sizeof(members);
    esp_err_t err = nvs_get_blob(nvs_handle_, KEY_FAMILY, members, &size);
    if (err == ESP_OK) {
        int count = size / sizeof(FamilyMember);
        family_cache_.clear();
        for (int i = 0; i < count; i++) {
            if (strlen(members[i].name) > 0) {
                family_cache_.push_back(members[i]);
            }
        }
    }
    family_loaded_ = true;
}

void MemoryStorage::SaveFamily() {
    if (!family_dirty_) return;

    if (family_cache_.empty()) {
        nvs_erase_key(nvs_handle_, KEY_FAMILY);
    } else {
        esp_err_t err = nvs_set_blob(nvs_handle_, KEY_FAMILY,
                                      family_cache_.data(),
                                      family_cache_.size() * sizeof(FamilyMember));
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to save family: %s", esp_err_to_name(err));
        }
    }
    family_dirty_ = false;
}

int MemoryStorage::GetFamily(FamilyMember* members, int max_count) {
    std::lock_guard<std::mutex> lock(mutex_);
    LoadFamily();

    int count = std::min((int)family_cache_.size(), max_count);
    if (members) {
        for (int i = 0; i < count; i++) {
            memcpy(&members[i], &family_cache_[i], sizeof(FamilyMember));
        }
    }
    return count;
}

int MemoryStorage::GetFamilyCount() {
    std::lock_guard<std::mutex> lock(mutex_);
    LoadFamily();
    return family_cache_.size();
}

AUDNAction MemoryStorage::AddFamilyMember(const char* relation, const char* name,
                                           const char* member_type, uint8_t closeness,
                                           const char* shared_memory) {
    std::lock_guard<std::mutex> lock(mutex_);
    LoadFamily();

    // Check if exists
    for (auto& member : family_cache_) {
        if (strcmp(member.name, name) == 0) {
            // Update existing
            if (relation) strncpy(member.relation, relation, sizeof(member.relation) - 1);
            if (member_type) strncpy(member.member_type, member_type, sizeof(member.member_type) - 1);
            if (closeness > 0) member.closeness = closeness;
            if (shared_memory) strncpy(member.shared_memory, shared_memory, sizeof(member.shared_memory) - 1);
            family_dirty_ = true;
            SaveFamily();
            nvs_commit(nvs_handle_);
            return AUDNAction::UPDATED;
        }
    }

    // Add new
    if (family_cache_.size() >= MAX_FAMILY_MEMBERS) {
        ESP_LOGW(TAG, "Family members limit reached");
        return AUDNAction::NOOP;
    }

    FamilyMember member;
    memset(&member, 0, sizeof(member));
    strncpy(member.relation, relation ? relation : "", sizeof(member.relation) - 1);
    strncpy(member.name, name, sizeof(member.name) - 1);
    strncpy(member.member_type, member_type ? member_type : "", sizeof(member.member_type) - 1);
    member.closeness = closeness > 0 ? closeness : 3;
    if (shared_memory) strncpy(member.shared_memory, shared_memory, sizeof(member.shared_memory) - 1);

    family_cache_.push_back(member);
    family_dirty_ = true;
    SaveFamily();
    nvs_commit(nvs_handle_);

    ESP_LOGI(TAG, "Added family member: %s (%s)", name, relation);
    return AUDNAction::ADDED;
}

AUDNAction MemoryStorage::UpdateFamilyMember(const char* name, uint8_t closeness,
                                              const char* shared_memory) {
    std::lock_guard<std::mutex> lock(mutex_);
    LoadFamily();

    for (auto& member : family_cache_) {
        if (strcmp(member.name, name) == 0) {
            if (closeness > 0) member.closeness = closeness;
            if (shared_memory) strncpy(member.shared_memory, shared_memory, sizeof(member.shared_memory) - 1);
            family_dirty_ = true;
            SaveFamily();
            nvs_commit(nvs_handle_);
            return AUDNAction::UPDATED;
        }
    }
    return AUDNAction::NOOP;
}

AUDNAction MemoryStorage::RemoveFamilyMember(const char* name) {
    std::lock_guard<std::mutex> lock(mutex_);
    LoadFamily();

    for (auto it = family_cache_.begin(); it != family_cache_.end(); ++it) {
        if (strcmp(it->name, name) == 0) {
            family_cache_.erase(it);
            family_dirty_ = true;
            SaveFamily();
            nvs_commit(nvs_handle_);
            ESP_LOGI(TAG, "Removed family member: %s", name);
            return AUDNAction::DELETED;
        }
    }
    return AUDNAction::NOOP;
}

// ============== Preferences ==============

void MemoryStorage::LoadPrefs() {
    if (prefs_loaded_) return;

    size_t size = sizeof(Preferences);
    esp_err_t err = nvs_get_blob(nvs_handle_, KEY_PREFS, &prefs_cache_, &size);
    if (err != ESP_OK || memcmp(prefs_cache_.magic, MEMORY_MAGIC_PREFERENCES, 4) != 0) {
        memset(&prefs_cache_, 0, sizeof(Preferences));
        memcpy(prefs_cache_.magic, MEMORY_MAGIC_PREFERENCES, 4);
    }
    prefs_loaded_ = true;
}

void MemoryStorage::SavePrefs() {
    if (!prefs_dirty_) return;

    esp_err_t err = nvs_set_blob(nvs_handle_, KEY_PREFS, &prefs_cache_, sizeof(Preferences));
    if (err == ESP_OK) {
        prefs_dirty_ = false;
    } else {
        ESP_LOGE(TAG, "Failed to save preferences: %s", esp_err_to_name(err));
    }
}

int MemoryStorage::GetPreferences(Preferences* prefs) {
    std::lock_guard<std::mutex> lock(mutex_);
    LoadPrefs();
    if (prefs) {
        memcpy(prefs, &prefs_cache_, sizeof(Preferences));
    }
    return 0;
}

AUDNAction MemoryStorage::AddPreference(const char* item, bool is_like) {
    std::lock_guard<std::mutex> lock(mutex_);
    LoadPrefs();

    if (is_like) {
        // Check if exists
        for (int i = 0; i < prefs_cache_.likes_count; i++) {
            if (strcmp(prefs_cache_.likes[i], item) == 0) {
                return AUDNAction::NOOP;
            }
        }
        if (prefs_cache_.likes_count >= MAX_LIKES) {
            return AUDNAction::NOOP;
        }
        strncpy(prefs_cache_.likes[prefs_cache_.likes_count], item, 31);
        prefs_cache_.likes_count++;
    } else {
        for (int i = 0; i < prefs_cache_.dislikes_count; i++) {
            if (strcmp(prefs_cache_.dislikes[i], item) == 0) {
                return AUDNAction::NOOP;
            }
        }
        if (prefs_cache_.dislikes_count >= MAX_DISLIKES) {
            return AUDNAction::NOOP;
        }
        strncpy(prefs_cache_.dislikes[prefs_cache_.dislikes_count], item, 31);
        prefs_cache_.dislikes_count++;
    }

    prefs_dirty_ = true;
    SavePrefs();
    nvs_commit(nvs_handle_);
    ESP_LOGI(TAG, "Added preference: %s (%s)", item, is_like ? "like" : "dislike");
    return AUDNAction::ADDED;
}

AUDNAction MemoryStorage::RemovePreference(const char* item, bool is_like) {
    std::lock_guard<std::mutex> lock(mutex_);
    LoadPrefs();

    if (is_like) {
        for (int i = 0; i < prefs_cache_.likes_count; i++) {
            if (strcmp(prefs_cache_.likes[i], item) == 0) {
                // Shift remaining items
                for (int j = i; j < prefs_cache_.likes_count - 1; j++) {
                    strcpy(prefs_cache_.likes[j], prefs_cache_.likes[j + 1]);
                }
                prefs_cache_.likes_count--;
                prefs_dirty_ = true;
                SavePrefs();
                nvs_commit(nvs_handle_);
                return AUDNAction::DELETED;
            }
        }
    } else {
        for (int i = 0; i < prefs_cache_.dislikes_count; i++) {
            if (strcmp(prefs_cache_.dislikes[i], item) == 0) {
                for (int j = i; j < prefs_cache_.dislikes_count - 1; j++) {
                    strcpy(prefs_cache_.dislikes[j], prefs_cache_.dislikes[j + 1]);
                }
                prefs_cache_.dislikes_count--;
                prefs_dirty_ = true;
                SavePrefs();
                nvs_commit(nvs_handle_);
                return AUDNAction::DELETED;
            }
        }
    }
    return AUDNAction::NOOP;
}

// ============== Events ==============

void MemoryStorage::LoadEvents() {
    if (events_loaded_) return;

    Event events[MAX_EVENTS];
    size_t size = sizeof(events);
    esp_err_t err = nvs_get_blob(nvs_handle_, KEY_EVENTS, events, &size);
    if (err == ESP_OK) {
        int count = size / sizeof(Event);
        events_cache_.clear();
        for (int i = 0; i < count; i++) {
            if (strlen(events[i].content) > 0) {
                events_cache_.push_back(events[i]);
            }
        }
    }
    events_loaded_ = true;
}

int MemoryStorage::GetEvents(Event* events, int max_count) {
    std::lock_guard<std::mutex> lock(mutex_);
    LoadEvents();

    int count = std::min((int)events_cache_.size(), max_count);
    if (events) {
        for (int i = 0; i < count; i++) {
            memcpy(&events[i], &events_cache_[i], sizeof(Event));
        }
    }
    return count;
}

int MemoryStorage::GetUpcomingEvents(Event* events, int max_count, int days) {
    std::lock_guard<std::mutex> lock(mutex_);
    LoadEvents();

    // Get current date
    time_t now = time(nullptr);
    struct tm* tm_now = localtime(&now);
    int current_month = tm_now->tm_mon + 1;
    int current_day = tm_now->tm_mday;

    int count = 0;
    for (const auto& event : events_cache_) {
        if (count >= max_count) break;

        // Parse event date (MM-DD format)
        int event_month = 0, event_day = 0;
        if (strlen(event.date) >= 5) {
            if (event.date[2] == '-') {
                sscanf(event.date, "%d-%d", &event_month, &event_day);
            } else if (strlen(event.date) >= 10 && event.date[4] == '-') {
                int year;
                sscanf(event.date, "%d-%d-%d", &year, &event_month, &event_day);
            }
        }

        // Check if within range
        int diff = (event_month - current_month) * 30 + (event_day - current_day);
        if (diff >= 0 && diff <= days) {
            if (events) {
                memcpy(&events[count], &event, sizeof(Event));
            }
            count++;
        }
    }
    return count;
}

AUDNAction MemoryStorage::AddEvent(const char* date, const char* event_type,
                                    const char* content, uint8_t emotion_type,
                                    uint8_t emotion_intensity, uint8_t significance) {
    std::lock_guard<std::mutex> lock(mutex_);
    LoadEvents();

    // Check if exists
    for (const auto& event : events_cache_) {
        if (strcmp(event.date, date) == 0 && strcmp(event.event_type, event_type) == 0) {
            return AUDNAction::NOOP;
        }
    }

    if (events_cache_.size() >= MAX_EVENTS) {
        // Remove oldest (first)
        events_cache_.erase(events_cache_.begin());
    }

    Event event;  // Constructor initializes to zero
    strncpy(event.date, date, sizeof(event.date) - 1);
    strncpy(event.event_type, event_type, sizeof(event.event_type) - 1);
    strncpy(event.content, content, sizeof(event.content) - 1);
    event.reminded = 0;
    event.emotion.type = emotion_type;
    event.emotion.intensity = emotion_intensity;
    event.significance = significance;

    events_cache_.push_back(event);
    events_dirty_ = true;
    SaveEvents();
    nvs_commit(nvs_handle_);

    ESP_LOGI(TAG, "Added event: %s - %s", date, event_type);
    return AUDNAction::ADDED;
}

AUDNAction MemoryStorage::MarkEventReminded(const char* date, const char* event_type) {
    std::lock_guard<std::mutex> lock(mutex_);
    LoadEvents();

    for (auto& event : events_cache_) {
        if (strcmp(event.date, date) == 0 && strcmp(event.event_type, event_type) == 0) {
            event.reminded = 1;
            events_dirty_ = true;
            SaveEvents();
            nvs_commit(nvs_handle_);
            return AUDNAction::UPDATED;
        }
    }
    return AUDNAction::NOOP;
}

// ============== Facts ==============

void MemoryStorage::LoadFacts() {
    if (facts_loaded_) return;

    Fact facts[MAX_FACTS];
    size_t size = sizeof(facts);
    esp_err_t err = nvs_get_blob(nvs_handle_, KEY_FACTS, facts, &size);
    if (err == ESP_OK) {
        int count = size / sizeof(Fact);
        facts_cache_.clear();
        for (int i = 0; i < count; i++) {
            if (strlen(facts[i].content) > 0) {
                facts_cache_.push_back(facts[i]);
            }
        }
    }
    facts_loaded_ = true;
}

void MemoryStorage::SaveFacts() {
    if (!facts_dirty_) return;

    esp_err_t err;
    if (facts_cache_.empty()) {
        err = nvs_erase_key(nvs_handle_, KEY_FACTS);
    } else {
        err = nvs_set_blob(nvs_handle_, KEY_FACTS,
                           facts_cache_.data(),
                           facts_cache_.size() * sizeof(Fact));
    }
    if (err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND) {
        facts_dirty_ = false;
    } else {
        ESP_LOGE(TAG, "Failed to save facts: %s", esp_err_to_name(err));
    }
}

int MemoryStorage::GetFacts(Fact* facts, int max_count) {
    std::lock_guard<std::mutex> lock(mutex_);
    LoadFacts();

    int count = std::min((int)facts_cache_.size(), max_count);
    if (facts) {
        for (int i = 0; i < count; i++) {
            memcpy(&facts[i], &facts_cache_[i], sizeof(Fact));
        }
    }
    return count;
}

int MemoryStorage::GetRecentFacts(Fact* facts, int max_count, int days) {
    std::lock_guard<std::mutex> lock(mutex_);
    LoadFacts();

    time_t now = time(nullptr);
    time_t cutoff = now - (days * 24 * 60 * 60);

    int count = 0;
    for (const auto& fact : facts_cache_) {
        if (count >= max_count) break;
        if (fact.timestamp >= cutoff) {
            if (facts) {
                memcpy(&facts[count], &fact, sizeof(Fact));
            }
            count++;
        }
    }
    return count;
}

AUDNAction MemoryStorage::AddFact(const char* content) {
    std::lock_guard<std::mutex> lock(mutex_);
    LoadFacts();

    // Check for duplicates
    for (const auto& fact : facts_cache_) {
        if (strcmp(fact.content, content) == 0) {
            return AUDNAction::NOOP;
        }
    }

    // Rolling queue with archiving
    if (facts_cache_.size() >= MAX_FACTS) {
        // Archive the oldest fact before removing
        auto& archive = MemoryArchive::GetInstance();
        if (archive.IsInitialized()) {
            std::vector<Fact> to_archive;
            to_archive.push_back(facts_cache_.front());
            archive.ArchiveFacts(to_archive);
        }
        facts_cache_.erase(facts_cache_.begin());
    }

    Fact fact;
    memset(&fact, 0, sizeof(fact));
    fact.timestamp = time(nullptr);
    strncpy(fact.content, content, sizeof(fact.content) - 1);

    facts_cache_.push_back(fact);
    facts_dirty_ = true;
    SaveFacts();
    nvs_commit(nvs_handle_);

    ESP_LOGI(TAG, "Added fact: %s", content);
    return AUDNAction::ADDED;
}

// ============== Traits ==============

void MemoryStorage::LoadTraits() {
    if (traits_loaded_) return;

    Trait traits[MAX_TRAITS];
    size_t size = sizeof(traits);
    esp_err_t err = nvs_get_blob(nvs_handle_, KEY_TRAITS, traits, &size);
    if (err == ESP_OK) {
        int count = size / sizeof(Trait);
        traits_cache_.clear();
        for (int i = 0; i < count; i++) {
            if (strlen(traits[i].content) > 0) {
                traits_cache_.push_back(traits[i]);
            }
        }
    }
    traits_loaded_ = true;
}

void MemoryStorage::SaveTraits() {
    if (!traits_dirty_) return;

    esp_err_t err;
    if (traits_cache_.empty()) {
        err = nvs_erase_key(nvs_handle_, KEY_TRAITS);
    } else {
        err = nvs_set_blob(nvs_handle_, KEY_TRAITS,
                           traits_cache_.data(),
                           traits_cache_.size() * sizeof(Trait));
    }
    if (err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND) {
        traits_dirty_ = false;
    } else {
        ESP_LOGE(TAG, "Failed to save traits: %s", esp_err_to_name(err));
    }
}

int MemoryStorage::GetTraits(Trait* traits, int max_count) {
    std::lock_guard<std::mutex> lock(mutex_);
    LoadTraits();

    int count = std::min((int)traits_cache_.size(), max_count);
    if (traits) {
        for (int i = 0; i < count; i++) {
            memcpy(&traits[i], &traits_cache_[i], sizeof(Trait));
        }
    }
    return count;
}

int MemoryStorage::GetTraitsByCategory(const char* category, Trait* traits, int max_count) {
    std::lock_guard<std::mutex> lock(mutex_);
    LoadTraits();

    int count = 0;
    for (const auto& trait : traits_cache_) {
        if (count >= max_count) break;
        if (strcmp(trait.category, category) == 0) {
            if (traits) {
                memcpy(&traits[count], &trait, sizeof(Trait));
            }
            count++;
        }
    }
    return count;
}

AUDNAction MemoryStorage::AddTrait(const char* category, const char* content) {
    std::lock_guard<std::mutex> lock(mutex_);
    LoadTraits();

    for (const auto& trait : traits_cache_) {
        if (strcmp(trait.content, content) == 0) {
            return AUDNAction::NOOP;
        }
    }

    if (traits_cache_.size() >= MAX_TRAITS) {
        return AUDNAction::NOOP;
    }

    Trait trait;
    memset(&trait, 0, sizeof(trait));
    strncpy(trait.category, category, sizeof(trait.category) - 1);
    strncpy(trait.content, content, sizeof(trait.content) - 1);

    traits_cache_.push_back(trait);
    traits_dirty_ = true;
    SaveTraits();
    nvs_commit(nvs_handle_);

    ESP_LOGI(TAG, "Added trait: %s - %s", category, content);
    return AUDNAction::ADDED;
}

AUDNAction MemoryStorage::RemoveTrait(const char* content) {
    std::lock_guard<std::mutex> lock(mutex_);
    LoadTraits();

    for (auto it = traits_cache_.begin(); it != traits_cache_.end(); ++it) {
        if (strcmp(it->content, content) == 0) {
            traits_cache_.erase(it);
            traits_dirty_ = true;
            SaveTraits();
            nvs_commit(nvs_handle_);
            return AUDNAction::DELETED;
        }
    }
    return AUDNAction::NOOP;
}

// ============== Habits ==============

void MemoryStorage::LoadHabits() {
    if (habits_loaded_) return;

    Habit habits[MAX_HABITS];
    size_t size = sizeof(habits);
    esp_err_t err = nvs_get_blob(nvs_handle_, KEY_HABITS, habits, &size);
    if (err == ESP_OK) {
        int count = size / sizeof(Habit);
        habits_cache_.clear();
        for (int i = 0; i < count; i++) {
            if (strlen(habits[i].content) > 0) {
                habits_cache_.push_back(habits[i]);
            }
        }
    }
    habits_loaded_ = true;
}

void MemoryStorage::SaveHabits() {
    if (!habits_dirty_) return;

    esp_err_t err;
    if (habits_cache_.empty()) {
        err = nvs_erase_key(nvs_handle_, KEY_HABITS);
    } else {
        err = nvs_set_blob(nvs_handle_, KEY_HABITS,
                           habits_cache_.data(),
                           habits_cache_.size() * sizeof(Habit));
    }
    if (err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND) {
        habits_dirty_ = false;
    } else {
        ESP_LOGE(TAG, "Failed to save habits: %s", esp_err_to_name(err));
    }
}

int MemoryStorage::GetHabits(Habit* habits, int max_count) {
    std::lock_guard<std::mutex> lock(mutex_);
    LoadHabits();

    int count = std::min((int)habits_cache_.size(), max_count);
    if (habits) {
        for (int i = 0; i < count; i++) {
            memcpy(&habits[i], &habits_cache_[i], sizeof(Habit));
        }
    }
    return count;
}

AUDNAction MemoryStorage::AddHabit(const char* content, const char* frequency) {
    std::lock_guard<std::mutex> lock(mutex_);
    LoadHabits();

    for (const auto& habit : habits_cache_) {
        if (strcmp(habit.content, content) == 0) {
            return AUDNAction::NOOP;
        }
    }

    if (habits_cache_.size() >= MAX_HABITS) {
        return AUDNAction::NOOP;
    }

    Habit habit;
    memset(&habit, 0, sizeof(habit));
    strncpy(habit.content, content, sizeof(habit.content) - 1);
    strncpy(habit.frequency, frequency ? frequency : "occasionally", sizeof(habit.frequency) - 1);

    habits_cache_.push_back(habit);
    habits_dirty_ = true;
    SaveHabits();
    nvs_commit(nvs_handle_);

    ESP_LOGI(TAG, "Added habit: %s", content);
    return AUDNAction::ADDED;
}

AUDNAction MemoryStorage::RemoveHabit(const char* content) {
    std::lock_guard<std::mutex> lock(mutex_);
    LoadHabits();

    for (auto it = habits_cache_.begin(); it != habits_cache_.end(); ++it) {
        if (strcmp(it->content, content) == 0) {
            habits_cache_.erase(it);
            habits_dirty_ = true;
            SaveHabits();
            nvs_commit(nvs_handle_);
            return AUDNAction::DELETED;
        }
    }
    return AUDNAction::NOOP;
}

// ============== Moments ==============

void MemoryStorage::LoadMoments() {
    if (moments_loaded_) return;

    SpecialMoment moments[MAX_MOMENTS];
    size_t size = sizeof(moments);
    esp_err_t err = nvs_get_blob(nvs_handle_, KEY_MOMENTS, moments, &size);
    if (err == ESP_OK) {
        int count = size / sizeof(SpecialMoment);
        moments_cache_.clear();
        for (int i = 0; i < count; i++) {
            if (strlen(moments[i].content) > 0) {
                moments_cache_.push_back(moments[i]);
            }
        }
    }
    moments_loaded_ = true;
}

void MemoryStorage::SaveMoments() {
    if (!moments_dirty_) return;

    esp_err_t err;
    if (moments_cache_.empty()) {
        err = nvs_erase_key(nvs_handle_, KEY_MOMENTS);
    } else {
        err = nvs_set_blob(nvs_handle_, KEY_MOMENTS,
                           moments_cache_.data(),
                           moments_cache_.size() * sizeof(SpecialMoment));
    }
    if (err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND) {
        moments_dirty_ = false;
    } else {
        ESP_LOGE(TAG, "Failed to save moments: %s", esp_err_to_name(err));
    }
}

int MemoryStorage::GetMoments(SpecialMoment* moments, int max_count) {
    std::lock_guard<std::mutex> lock(mutex_);
    LoadMoments();

    int count = std::min((int)moments_cache_.size(), max_count);
    if (moments) {
        for (int i = 0; i < count; i++) {
            memcpy(&moments[i], &moments_cache_[i], sizeof(SpecialMoment));
        }
    }
    return count;
}

int MemoryStorage::GetRecentMoments(SpecialMoment* moments, int max_count, int days) {
    std::lock_guard<std::mutex> lock(mutex_);
    LoadMoments();

    time_t now = time(nullptr);
    time_t cutoff = now - (days * 24 * 60 * 60);

    int count = 0;
    for (const auto& moment : moments_cache_) {
        if (count >= max_count) break;
        if (moment.timestamp >= cutoff) {
            if (moments) {
                memcpy(&moments[count], &moment, sizeof(SpecialMoment));
            }
            count++;
        }
    }
    return count;
}

AUDNAction MemoryStorage::AddMoment(const char* topic, const char* content,
                                     uint8_t emotion_type, uint8_t emotion_intensity,
                                     uint8_t importance) {
    std::lock_guard<std::mutex> lock(mutex_);
    LoadMoments();

    if (moments_cache_.size() >= MAX_MOMENTS) {
        // Archive the oldest moment before removing
        auto& archive = MemoryArchive::GetInstance();
        if (archive.IsInitialized()) {
            std::vector<SpecialMoment> to_archive;
            to_archive.push_back(moments_cache_.front());
            archive.ArchiveMoments(to_archive);
        }
        // Remove oldest
        moments_cache_.erase(moments_cache_.begin());
    }

    SpecialMoment moment;
    memset(&moment, 0, sizeof(moment));
    moment.timestamp = time(nullptr);
    strncpy(moment.topic, topic, sizeof(moment.topic) - 1);
    strncpy(moment.content, content, sizeof(moment.content) - 1);
    moment.emotion.type = emotion_type;
    moment.emotion.intensity = emotion_intensity;
    moment.importance = importance;

    moments_cache_.push_back(moment);
    moments_dirty_ = true;
    SaveMoments();
    nvs_commit(nvs_handle_);

    ESP_LOGI(TAG, "Added moment: %s", topic);
    return AUDNAction::ADDED;
}

// ============== Goals ==============

void MemoryStorage::LoadGoals() {
    if (goals_loaded_) return;

    PersonalGoal goals[MAX_GOALS];
    size_t size = sizeof(goals);
    esp_err_t err = nvs_get_blob(nvs_handle_, KEY_GOALS, goals, &size);
    if (err == ESP_OK) {
        int count = size / sizeof(PersonalGoal);
        goals_cache_.clear();
        for (int i = 0; i < count; i++) {
            if (strlen(goals[i].content) > 0) {
                goals_cache_.push_back(goals[i]);
            }
        }
    }
    goals_loaded_ = true;
}

void MemoryStorage::SaveGoals() {
    if (!goals_dirty_) return;

    esp_err_t err;
    if (goals_cache_.empty()) {
        err = nvs_erase_key(nvs_handle_, KEY_GOALS);
    } else {
        err = nvs_set_blob(nvs_handle_, KEY_GOALS,
                           goals_cache_.data(),
                           goals_cache_.size() * sizeof(PersonalGoal));
    }
    if (err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND) {
        goals_dirty_ = false;
    } else {
        ESP_LOGE(TAG, "Failed to save goals: %s", esp_err_to_name(err));
    }
}

int MemoryStorage::GetGoals(PersonalGoal* goals, int max_count) {
    std::lock_guard<std::mutex> lock(mutex_);
    LoadGoals();

    int count = std::min((int)goals_cache_.size(), max_count);
    if (goals) {
        for (int i = 0; i < count; i++) {
            memcpy(&goals[i], &goals_cache_[i], sizeof(PersonalGoal));
        }
    }
    return count;
}

int MemoryStorage::GetActiveGoals(PersonalGoal* goals, int max_count) {
    std::lock_guard<std::mutex> lock(mutex_);
    LoadGoals();

    int count = 0;
    for (const auto& goal : goals_cache_) {
        if (count >= max_count) break;
        if (goal.status == (uint8_t)GoalStatus::ACTIVE) {
            if (goals) {
                memcpy(&goals[count], &goal, sizeof(PersonalGoal));
            }
            count++;
        }
    }
    return count;
}

AUDNAction MemoryStorage::AddGoal(const char* content, uint8_t category, uint8_t priority) {
    std::lock_guard<std::mutex> lock(mutex_);
    LoadGoals();

    for (const auto& goal : goals_cache_) {
        if (strcmp(goal.content, content) == 0) {
            return AUDNAction::NOOP;
        }
    }

    if (goals_cache_.size() >= MAX_GOALS) {
        return AUDNAction::NOOP;
    }

    PersonalGoal goal;
    memset(&goal, 0, sizeof(goal));
    strncpy(goal.content, content, sizeof(goal.content) - 1);
    goal.created = time(nullptr);
    goal.updated = goal.created;
    goal.category = category;
    goal.status = (uint8_t)GoalStatus::ACTIVE;
    goal.progress = 0;
    goal.priority = priority > 0 ? priority : 3;

    goals_cache_.push_back(goal);
    goals_dirty_ = true;
    SaveGoals();
    nvs_commit(nvs_handle_);

    ESP_LOGI(TAG, "Added goal: %s", content);
    return AUDNAction::ADDED;
}

AUDNAction MemoryStorage::UpdateGoal(const char* content, uint8_t progress, uint8_t status) {
    std::lock_guard<std::mutex> lock(mutex_);
    LoadGoals();

    for (auto& goal : goals_cache_) {
        if (strcmp(goal.content, content) == 0) {
            goal.progress = progress;
            goal.status = status;
            goal.updated = time(nullptr);
            goals_dirty_ = true;
            SaveGoals();
            nvs_commit(nvs_handle_);
            return AUDNAction::UPDATED;
        }
    }
    return AUDNAction::NOOP;
}

// ============== Queries ==============

int MemoryStorage::GetSummary(char* buffer, int buffer_size) {
    std::lock_guard<std::mutex> lock(mutex_);

    LoadProfile();
    LoadFamily();
    LoadPrefs();
    LoadEvents();
    LoadFacts();

    int written = 0;

    // Profile
    if (strlen(profile_cache_.name) > 0) {
        written += snprintf(buffer + written, buffer_size - written,
                           "User: %s", profile_cache_.name);
        if (profile_cache_.age > 0) {
            written += snprintf(buffer + written, buffer_size - written,
                               ", %d years old", profile_cache_.age);
        }
        if (strlen(profile_cache_.location) > 0) {
            written += snprintf(buffer + written, buffer_size - written,
                               ", from %s", profile_cache_.location);
        }
        written += snprintf(buffer + written, buffer_size - written, "\n");
    }

    // Family
    if (!family_cache_.empty()) {
        written += snprintf(buffer + written, buffer_size - written, "Family: ");
        for (size_t i = 0; i < family_cache_.size(); i++) {
            if (i > 0) written += snprintf(buffer + written, buffer_size - written, ", ");
            written += snprintf(buffer + written, buffer_size - written,
                               "%s(%s)", family_cache_[i].name, family_cache_[i].relation);
        }
        written += snprintf(buffer + written, buffer_size - written, "\n");
    }

    // Preferences
    if (prefs_cache_.likes_count > 0) {
        written += snprintf(buffer + written, buffer_size - written, "Likes: ");
        for (int i = 0; i < prefs_cache_.likes_count; i++) {
            if (i > 0) written += snprintf(buffer + written, buffer_size - written, ", ");
            written += snprintf(buffer + written, buffer_size - written, "%s", prefs_cache_.likes[i]);
        }
        written += snprintf(buffer + written, buffer_size - written, "\n");
    }

    return written;
}

int MemoryStorage::Search(const char* keyword, char* buffer, int buffer_size) {
    std::lock_guard<std::mutex> lock(mutex_);

    LoadProfile();
    LoadFamily();
    LoadPrefs();
    LoadEvents();
    LoadFacts();
    LoadTraits();
    LoadHabits();

    int written = 0;

    // Search in facts
    for (const auto& fact : facts_cache_) {
        if (strstr(fact.content, keyword) != nullptr) {
            written += snprintf(buffer + written, buffer_size - written,
                               "Fact: %s\n", fact.content);
            if (written >= buffer_size - 1) break;
        }
    }

    // Search in family
    for (const auto& member : family_cache_) {
        if (strstr(member.name, keyword) != nullptr ||
            strstr(member.relation, keyword) != nullptr) {
            written += snprintf(buffer + written, buffer_size - written,
                               "Family: %s (%s)\n", member.name, member.relation);
            if (written >= buffer_size - 1) break;
        }
    }

    // Search in events
    for (const auto& event : events_cache_) {
        if (strstr(event.content, keyword) != nullptr) {
            written += snprintf(buffer + written, buffer_size - written,
                               "Event: %s - %s\n", event.date, event.content);
            if (written >= buffer_size - 1) break;
        }
    }

    return written;
}

bool MemoryStorage::EraseAll() {
    std::lock_guard<std::mutex> lock(mutex_);

    esp_err_t err = nvs_erase_all(nvs_handle_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to erase all: %s", esp_err_to_name(err));
        return false;
    }
    nvs_commit(nvs_handle_);

    // Clear cache
    memset(&profile_cache_, 0, sizeof(profile_cache_));
    family_cache_.clear();
    memset(&prefs_cache_, 0, sizeof(prefs_cache_));
    events_cache_.clear();
    facts_cache_.clear();
    traits_cache_.clear();
    habits_cache_.clear();
    moments_cache_.clear();
    goals_cache_.clear();

    // Reset flags
    profile_loaded_ = false;
    profile_dirty_ = false;
    family_loaded_ = false;
    family_dirty_ = false;
    prefs_loaded_ = false;
    prefs_dirty_ = false;
    events_loaded_ = false;
    events_dirty_ = false;
    facts_loaded_ = false;
    facts_dirty_ = false;
    traits_loaded_ = false;
    traits_dirty_ = false;
    habits_loaded_ = false;
    habits_dirty_ = false;
    moments_loaded_ = false;
    moments_dirty_ = false;
    goals_loaded_ = false;
    goals_dirty_ = false;

    ESP_LOGI(TAG, "All memory data erased");
    return true;
}

// === Schedule Management (日程管理) ===

bool MemoryStorage::AddEvent(const Event& event) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (events_cache_.size() >= MAX_EVENTS) {
        ESP_LOGW(TAG, "Events storage full (%d/%d)", (int)events_cache_.size(), MAX_EVENTS);
        return false;
    }

    events_cache_.push_back(event);
    events_dirty_ = true;

    // 立即保存
    SaveEvents();

    ESP_LOGI(TAG, "Added %s: %s at %s %s",
             IsSchedule(event) ? "schedule" : "event",
             event.content, event.date, event.time);

    return true;
}

std::vector<Event> MemoryStorage::GetEventsCopy() {
    std::lock_guard<std::mutex> lock(mutex_);
    LoadEvents();
    return events_cache_;  // Returns a copy, lock protects during copy
}

bool MemoryStorage::DeleteSchedule(const std::string& content) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = std::remove_if(events_cache_.begin(), events_cache_.end(),
        [&content](const Event& e) {
            return IsSchedule(e) && strcmp(e.content, content.c_str()) == 0;
        });

    if (it != events_cache_.end()) {
        events_cache_.erase(it, events_cache_.end());
        events_dirty_ = true;
        SaveEvents();
        ESP_LOGI(TAG, "Deleted schedule: %s", content.c_str());
        return true;
    }

    return false;
}

void MemoryStorage::SaveEvents() {
    if (!events_dirty_) return;

    // NOTE: Caller must hold mutex_! This method uses the class nvs_handle_.

    esp_err_t err;

    // 保存 events 数组
    if (!events_cache_.empty()) {
        err = nvs_set_blob(nvs_handle_, KEY_EVENTS, events_cache_.data(),
                           events_cache_.size() * sizeof(Event));
    } else {
        // 清空数据
        err = nvs_erase_key(nvs_handle_, KEY_EVENTS);
    }

    if (err == ESP_OK) {
        events_dirty_ = false;
        ESP_LOGD(TAG, "Saved %d events to NVS", (int)events_cache_.size());
    } else {
        ESP_LOGE(TAG, "Failed to save events: %s", esp_err_to_name(err));
    }
}

void MemoryStorage::AutoCleanCompletedSchedules() {
    std::lock_guard<std::mutex> lock(mutex_);

    time_t now = time(nullptr);
    uint32_t cutoff = now - (30 * 24 * 60 * 60);  // 30 天前

    auto it = std::remove_if(events_cache_.begin(), events_cache_.end(),
        [cutoff](const Event& e) {
            if (!IsSchedule(e) || !IsCompleted(e)) return false;

            // 解析 date，检查是否超过 30 天
            struct tm tm = {};
            int year = 0, month = 0, day = 0;
            if (sscanf(e.date, "%d-%d-%d", &year, &month, &day) == 3) {
                tm.tm_year = year - 1900;
                tm.tm_mon = month - 1;
                tm.tm_mday = day;
                tm.tm_isdst = -1;  // Let mktime determine DST
                time_t event_time = mktime(&tm);
                if (event_time == (time_t)-1) {
                    return false;  // Invalid date, don't remove
                }
                return event_time < cutoff;
            }
            return false;
        });

    if (it != events_cache_.end()) {
        size_t removed = std::distance(it, events_cache_.end());
        events_cache_.erase(it, events_cache_.end());
        events_dirty_ = true;
        SaveEvents();
        ESP_LOGI(TAG, "Auto-cleaned %d completed schedules", (int)removed);
    }
}

std::vector<Event> MemoryStorage::GetUpcomingSchedules(int minutes_ahead) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<Event> upcoming;
    time_t now = time(nullptr);
    time_t threshold = now + (minutes_ahead * 60);

    for (const auto& event : events_cache_) {
        if (!IsSchedule(event) || IsCompleted(event)) continue;

        // 解析日程时间
        struct tm tm = {};
        int year = 0, month = 0, day = 0, hour = 0, minute = 0;
        if (sscanf(event.date, "%d-%d-%d", &year, &month, &day) == 3 &&
            sscanf(event.time, "%d:%d", &hour, &minute) == 2) {
            tm.tm_year = year - 1900;
            tm.tm_mon = month - 1;
            tm.tm_mday = day;
            tm.tm_hour = hour;
            tm.tm_min = minute;
            tm.tm_sec = 0;
            tm.tm_isdst = -1;  // Let mktime determine DST

            time_t schedule_time = mktime(&tm);
            if (schedule_time == (time_t)-1) {
                ESP_LOGW(TAG, "Invalid schedule datetime: %s %s", event.date, event.time);
                continue;
            }
            // 检查是否在提醒窗口内，且未被提醒过
            if (schedule_time > now && schedule_time <= threshold &&
                event.reminded == 0) {
                upcoming.push_back(event);
            }
        }
    }

    return upcoming;
}

bool MemoryStorage::MarkScheduleReminded(const std::string& content) {
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto& event : events_cache_) {
        if (IsSchedule(event) && !IsCompleted(event) &&
            strcmp(event.content, content.c_str()) == 0) {
            event.reminded = 1;
            events_dirty_ = true;
            SaveEvents();
            ESP_LOGI(TAG, "Marked schedule as reminded: %s", content.c_str());
            return true;
        }
    }

    return false;
}

bool MemoryStorage::CompleteSchedule(const std::string& content) {
    std::lock_guard<std::mutex> lock(mutex_);
    LoadEvents();

    for (auto& event : events_cache_) {
        if (IsSchedule(event) && !IsCompleted(event) &&
            strcmp(event.content, content.c_str()) == 0) {
            // If repeating, generate next occurrence first
            if (IsRepeating(event)) {
                GenerateNextRepeatSchedule(event);
            }

            SetCompleted(event, true);
            events_dirty_ = true;
            SaveEvents();
            nvs_commit(nvs_handle_);
            ESP_LOGI(TAG, "Completed schedule: %s", content.c_str());
            return true;
        }
    }

    return false;
}

void MemoryStorage::GenerateNextRepeatSchedule(const Event& completed_event) {
    if (!IsRepeating(completed_event)) {
        return;
    }

    // 解析当前日期
    struct tm tm = {};
    int year = 0, month = 0, day = 0, hour = 0, minute = 0;
    if (sscanf(completed_event.date, "%d-%d-%d", &year, &month, &day) != 3) {
        ESP_LOGW(TAG, "Failed to parse date for repeat schedule: %s", completed_event.date);
        return;
    }
    if (sscanf(completed_event.time, "%d:%d", &hour, &minute) != 2) {
        ESP_LOGW(TAG, "Failed to parse time for repeat schedule: %s", completed_event.time);
        return;
    }

    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = minute;
    tm.tm_sec = 0;
    tm.tm_isdst = -1;  // Let mktime determine DST

    // 根据重复类型计算下一次时间
    uint8_t repeat_type = GetRepeatType(completed_event);
    if (repeat_type == REPEAT_DAILY) {
        tm.tm_mday += 1;
    } else if (repeat_type == REPEAT_WEEKLY) {
        tm.tm_mday += 7;
    } else if (repeat_type == REPEAT_MONTHLY) {
        tm.tm_mon += 1;
    } else {
        ESP_LOGW(TAG, "Unknown repeat type: %d", repeat_type);
        return;
    }

    time_t normalized = mktime(&tm);  // 规范化日期
    if (normalized == (time_t)-1) {
        ESP_LOGE(TAG, "Failed to normalize date for repeat schedule");
        return;
    }

    // 创建新的重复日程
    Event next_event = completed_event;
    year = tm.tm_year + 1900;
    month = tm.tm_mon + 1;
    day = tm.tm_mday;

    // Validate date components are in reasonable ranges
    if (year < 1970 || year > 2100 || month < 1 || month > 12 || day < 1 || day > 31) {
        ESP_LOGE(TAG, "Invalid date after mktime: %d-%d-%d", year, month, day);
        return;
    }

    snprintf(next_event.date, sizeof(next_event.date),
             "%04d-%02d-%02d", year, month, day);
    SetCompleted(next_event, false);
    next_event.reminded = 0;

    // 添加新日程 (不需要 lock，因为调用方已经持有锁)
    if (events_cache_.size() < MAX_EVENTS) {
        events_cache_.push_back(next_event);
        events_dirty_ = true;
        SaveEvents();
        ESP_LOGI(TAG, "Generated next repeat schedule: '%s' at %s %s",
                 next_event.content, next_event.date, next_event.time);
    } else {
        ESP_LOGW(TAG, "Cannot generate next repeat schedule: events storage full");
    }
}

ConflictInfo MemoryStorage::CheckScheduleConflict(const char* date, const char* time, int duration_minutes) {
    std::lock_guard<std::mutex> lock(mutex_);
    ConflictInfo info;

    // Parse new schedule time
    struct tm new_tm = {};
    int year = 0, month = 0, day = 0, hour = 0, minute = 0;
    if (sscanf(date, "%d-%d-%d", &year, &month, &day) != 3 ||
        sscanf(time, "%d:%d", &hour, &minute) != 2) {
        ESP_LOGW(TAG, "Invalid date/time format for conflict check");
        return info;
    }

    new_tm.tm_year = year - 1900;
    new_tm.tm_mon = month - 1;
    new_tm.tm_mday = day;
    new_tm.tm_hour = hour;
    new_tm.tm_min = minute;
    new_tm.tm_sec = 0;
    new_tm.tm_isdst = -1;  // Let mktime determine DST
    time_t new_start = mktime(&new_tm);
    if (new_start == (time_t)-1) {
        ESP_LOGW(TAG, "Failed to parse schedule time for conflict check");
        return info;
    }
    time_t new_end = new_start + (duration_minutes * 60);

    // Ensure events are loaded
    if (!events_loaded_) {
        LoadEvents();
    }

    // Check all uncompleted schedules
    for (const auto& event : events_cache_) {
        if (!IsSchedule(event) || IsCompleted(event)) continue;
        if (strcmp(event.date, date) != 0) continue;  // Different date, skip

        // Parse existing schedule time
        int ex_hour = 0, ex_minute = 0;
        if (sscanf(event.time, "%d:%d", &ex_hour, &ex_minute) != 2) continue;

        struct tm ex_tm = new_tm;  // Reuse date
        ex_tm.tm_hour = ex_hour;
        ex_tm.tm_min = ex_minute;
        ex_tm.tm_sec = 0;
        time_t ex_start = mktime(&ex_tm);
        if (ex_start == (time_t)-1) continue;  // Skip invalid times
        time_t ex_end = ex_start + (60 * 60);  // Assume each schedule is 1 hour

        // Check time overlap
        if ((new_start >= ex_start && new_start < ex_end) ||
            (new_end > ex_start && new_end <= ex_end) ||
            (new_start <= ex_start && new_end >= ex_end)) {
            info.has_conflict = true;
            info.conflicting_event = event;

            // Generate suggested times (1 hour before and 2 hours after)
            char suggestion[8];  // "HH:MM\0" = 6 bytes, but use 8 for safety
            int before_hour = (ex_hour - 1 + 24) % 24;
            snprintf(suggestion, sizeof(suggestion), "%02d:%02d", before_hour, ex_minute);
            info.suggested_times.push_back(suggestion);

            int after_hour = (ex_hour + 2) % 24;
            snprintf(suggestion, sizeof(suggestion), "%02d:%02d", after_hour, ex_minute);
            info.suggested_times.push_back(suggestion);

            ESP_LOGI(TAG, "Conflict detected: new schedule at %s %s conflicts with '%s' at %s",
                     date, time, event.content, event.time);
            break;
        }
    }

    return info;
}

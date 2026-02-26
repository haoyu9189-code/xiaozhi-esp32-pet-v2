#include "pet_achievements.h"
#include "background_manager.h"
#include <nvs_flash.h>
#include <nvs.h>
#include <esp_log.h>
#include <vector>

#define TAG "PetAchievements"

#define NVS_NAMESPACE "pet_achieve"
#define NVS_KEY_COUNTERS "counters"
#define NVS_KEY_UNLOCKED "unlocked"

PetAchievements& PetAchievements::GetInstance() {
    static PetAchievements instance;
    return instance;
}

void PetAchievements::Initialize() {
    ESP_LOGI(TAG, "Initializing pet achievements system");
    Load();
    ESP_LOGI(TAG, "Achievements loaded: bathe=%lu, feed=%lu, play=%lu, conv=%lu, days=%lu",
             counters_.bathe_count, counters_.feed_count, counters_.play_count,
             counters_.conversation_count, counters_.days_alive);
    ESP_LOGI(TAG, "Unlocked backgrounds: flags=0x%08lX", unlocked_.flags);
}

void PetAchievements::OnBathe() {
    counters_.bathe_count++;
    ESP_LOGI(TAG, "Bathe count: %lu", counters_.bathe_count);
    CheckAchievements();
    Save();
}

void PetAchievements::OnFeed() {
    counters_.feed_count++;
    Save();
}

void PetAchievements::OnPlay() {
    counters_.play_count++;
    Save();
}

void PetAchievements::OnConversation() {
    counters_.conversation_count++;
    ESP_LOGI(TAG, "Conversation count: %lu", counters_.conversation_count);
    CheckAchievements();
    Save();
}

void PetAchievements::OnDayPassed() {
    counters_.days_alive++;
    ESP_LOGI(TAG, "Days alive: %lu", counters_.days_alive);
    CheckAchievements();
    Save();
}

void PetAchievements::CheckAchievements() {
    // Bather5: bathe >= 5 -> Cyberpunk
    if (counters_.bathe_count >= 5 && !IsCyberpunkUnlocked()) {
        UnlockBackground(UnlockedBackgrounds::BIT_CYBERPUNK, AchievementType::kBather5, "Cyberpunk");
    }

    // Bather20: bathe >= 20 -> Fantasy
    if (counters_.bathe_count >= 20 && !IsFantasyUnlocked()) {
        UnlockBackground(UnlockedBackgrounds::BIT_FANTASY, AchievementType::kBather20, "Fantasy");
    }

    // Talker10: conversation >= 10 -> Space
    if (counters_.conversation_count >= 10 && !IsSpaceUnlocked()) {
        UnlockBackground(UnlockedBackgrounds::BIT_SPACE, AchievementType::kTalker10, "Space");
    }

    // Caretaker7Days: days >= 7 -> Steampunk
    if (counters_.days_alive >= 7 && !IsSteampunkUnlocked()) {
        UnlockBackground(UnlockedBackgrounds::BIT_STEAMPUNK, AchievementType::kCaretaker7Days, "Steampunk");
    }
}

void PetAchievements::UnlockBackground(uint32_t bit, AchievementType type, const char* bg_name) {
    unlocked_.flags |= bit;
    ESP_LOGI(TAG, "Achievement unlocked: %s background!", bg_name);

    if (achievement_callback_) {
        achievement_callback_(type, bg_name);
    }
}

bool PetAchievements::IsBackgroundUnlocked(uint16_t bg_idx) const {
    switch (bg_idx) {
        // Style backgrounds
        case BG_STYLE_CYBERPUNK:
            return IsCyberpunkUnlocked();
        case BG_STYLE_FANTASY:
            return IsFantasyUnlocked();
        case BG_STYLE_SPACE:
            return IsSpaceUnlocked();
        case BG_STYLE_STEAMPUNK:
            return IsSteampunkUnlocked();
        // Festival backgrounds
        case BG_FESTIVAL_CHRISTMAS:
            return IsChristmasUnlocked();
        case BG_FESTIVAL_BIRTHDAY:
            return IsBirthdayUnlocked();
        case BG_FESTIVAL_SPRING:
            return IsSpringFestivalUnlocked();
        case BG_FESTIVAL_NEWYEAR:
            return IsNewYearUnlocked();
        case BG_FESTIVAL_MIDAUTUMN:
            return IsMidAutumnUnlocked();
        case BG_FESTIVAL_HALLOWEEN:
            return IsHalloweenUnlocked();
        case BG_FESTIVAL_VALENTINES:
            return IsValentinesUnlocked();
        default:
            return true;  // Time/Weather backgrounds are always available
    }
}

// Style background unlock check methods
bool PetAchievements::IsCyberpunkUnlocked() const { return IsFlagSet(UnlockedBackgrounds::BIT_CYBERPUNK); }
bool PetAchievements::IsFantasyUnlocked() const { return IsFlagSet(UnlockedBackgrounds::BIT_FANTASY); }
bool PetAchievements::IsSpaceUnlocked() const { return IsFlagSet(UnlockedBackgrounds::BIT_SPACE); }
bool PetAchievements::IsSteampunkUnlocked() const { return IsFlagSet(UnlockedBackgrounds::BIT_STEAMPUNK); }

// Helper: check if a flag bit is set
bool PetAchievements::IsFlagSet(uint32_t bit) const {
    return (unlocked_.flags & bit) != 0;
}

// Helper: unlock a festival background if not already unlocked
void PetAchievements::UnlockFestival(uint32_t bit, const char* name) {
    if (!IsFlagSet(bit)) {
        unlocked_.flags |= bit;
        ESP_LOGI(TAG, "Festival unlocked: %s background!", name);
        Save();
    }
}

// Festival unlock check methods
bool PetAchievements::IsChristmasUnlocked() const { return IsFlagSet(UnlockedBackgrounds::BIT_CHRISTMAS); }
bool PetAchievements::IsBirthdayUnlocked() const { return IsFlagSet(UnlockedBackgrounds::BIT_BIRTHDAY); }
bool PetAchievements::IsSpringFestivalUnlocked() const { return IsFlagSet(UnlockedBackgrounds::BIT_SPRING); }
bool PetAchievements::IsNewYearUnlocked() const { return IsFlagSet(UnlockedBackgrounds::BIT_NEWYEAR); }
bool PetAchievements::IsMidAutumnUnlocked() const { return IsFlagSet(UnlockedBackgrounds::BIT_MIDAUTUMN); }
bool PetAchievements::IsHalloweenUnlocked() const { return IsFlagSet(UnlockedBackgrounds::BIT_HALLOWEEN); }
bool PetAchievements::IsValentinesUnlocked() const { return IsFlagSet(UnlockedBackgrounds::BIT_VALENTINES); }

// Festival unlock methods (called by MCP)
void PetAchievements::UnlockChristmas() { UnlockFestival(UnlockedBackgrounds::BIT_CHRISTMAS, "Christmas"); }
void PetAchievements::UnlockBirthday() { UnlockFestival(UnlockedBackgrounds::BIT_BIRTHDAY, "Birthday"); }
void PetAchievements::UnlockSpringFestival() { UnlockFestival(UnlockedBackgrounds::BIT_SPRING, "Spring Festival"); }
void PetAchievements::UnlockNewYear() { UnlockFestival(UnlockedBackgrounds::BIT_NEWYEAR, "New Year"); }
void PetAchievements::UnlockMidAutumn() { UnlockFestival(UnlockedBackgrounds::BIT_MIDAUTUMN, "Mid-Autumn"); }
void PetAchievements::UnlockHalloween() { UnlockFestival(UnlockedBackgrounds::BIT_HALLOWEEN, "Halloween"); }
void PetAchievements::UnlockValentines() { UnlockFestival(UnlockedBackgrounds::BIT_VALENTINES, "Valentine's"); }

// Style background unlock methods (for coin purchase)
void PetAchievements::UnlockCyberpunk() { UnlockFestival(UnlockedBackgrounds::BIT_CYBERPUNK, "Cyberpunk"); }
void PetAchievements::UnlockFantasy() { UnlockFestival(UnlockedBackgrounds::BIT_FANTASY, "Fantasy"); }
void PetAchievements::UnlockSpace() { UnlockFestival(UnlockedBackgrounds::BIT_SPACE, "Space"); }
void PetAchievements::UnlockSteampunk() { UnlockFestival(UnlockedBackgrounds::BIT_STEAMPUNK, "Steampunk"); }

// Get list of unlocked background indices
std::vector<uint16_t> PetAchievements::GetUnlockedBackgroundIndices() const {
    std::vector<uint16_t> indices;

    // Check all style backgrounds
    if (IsCyberpunkUnlocked()) indices.push_back(BG_STYLE_CYBERPUNK);
    if (IsFantasyUnlocked()) indices.push_back(BG_STYLE_FANTASY);
    if (IsSpaceUnlocked()) indices.push_back(BG_STYLE_SPACE);
    if (IsSteampunkUnlocked()) indices.push_back(BG_STYLE_STEAMPUNK);

    // Check all festival backgrounds
    if (IsChristmasUnlocked()) indices.push_back(BG_FESTIVAL_CHRISTMAS);
    if (IsBirthdayUnlocked()) indices.push_back(BG_FESTIVAL_BIRTHDAY);
    if (IsSpringFestivalUnlocked()) indices.push_back(BG_FESTIVAL_SPRING);
    if (IsNewYearUnlocked()) indices.push_back(BG_FESTIVAL_NEWYEAR);
    if (IsMidAutumnUnlocked()) indices.push_back(BG_FESTIVAL_MIDAUTUMN);
    if (IsHalloweenUnlocked()) indices.push_back(BG_FESTIVAL_HALLOWEEN);
    if (IsValentinesUnlocked()) indices.push_back(BG_FESTIVAL_VALENTINES);

    return indices;
}

void PetAchievements::Save() {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return;
    }

    err = nvs_set_blob(handle, NVS_KEY_COUNTERS, &counters_, sizeof(ActivityCounters));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save counters: %s", esp_err_to_name(err));
    }

    err = nvs_set_blob(handle, NVS_KEY_UNLOCKED, &unlocked_, sizeof(UnlockedBackgrounds));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save unlocked: %s", esp_err_to_name(err));
    }

    nvs_commit(handle);
    nvs_close(handle);
    ESP_LOGD(TAG, "Achievements saved");
}

void PetAchievements::Load() {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "No saved achievements found, using defaults");
        counters_ = ActivityCounters();
        unlocked_ = UnlockedBackgrounds();
        return;
    }

    size_t size = sizeof(ActivityCounters);
    err = nvs_get_blob(handle, NVS_KEY_COUNTERS, &counters_, &size);
    if (err != ESP_OK || size != sizeof(ActivityCounters)) {
        ESP_LOGW(TAG, "Failed to load counters or size mismatch, using defaults");
        counters_ = ActivityCounters();
    }

    size = sizeof(UnlockedBackgrounds);
    err = nvs_get_blob(handle, NVS_KEY_UNLOCKED, &unlocked_, &size);
    if (err != ESP_OK || size != sizeof(UnlockedBackgrounds)) {
        ESP_LOGW(TAG, "Failed to load unlocked or size mismatch, using defaults");
        unlocked_ = UnlockedBackgrounds();
    }

    nvs_close(handle);
}

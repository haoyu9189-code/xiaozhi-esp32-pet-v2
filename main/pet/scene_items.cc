#include "scene_items.h"
#include "pet_coin.h"
#include "pet_state.h"
#include "pet_achievements.h"
#include "background_manager.h"
#include "ambient_dialogue.h"
#include "application.h"
#include "board.h"
#include "display/display.h"
#include "assets/lang_config.h"
#include <nvs_flash.h>
#include <nvs.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_random.h>
#include <time.h>
#include <math.h>
#include <vector>

#define TAG "SceneItems"

// Find empty slot in item array, returns -1 if none found
template<typename T, int N>
static int FindEmptySlot(T (&items)[N]) {
    for (int i = 0; i < N; i++) {
        if (!items[i].active) return i;
    }
    return -1;
}
#define NVS_NAMESPACE "scene_items"
#define NVS_KEY_STATE "state"

// Poop spawn interval range (in milliseconds)
// 平均每15分钟一次：10-20分钟随机
#define POOP_SPAWN_MIN_INTERVAL_MS (10 * 60 * 1000)   // 10 minutes minimum
#define POOP_SPAWN_MAX_INTERVAL_MS (20 * 60 * 1000)   // 20 minutes maximum

// Festival background unlock data for random unlock feature
static const struct {
    uint16_t bg_index;
    const char* name;
    bool (PetAchievements::*is_unlocked)() const;
    void (PetAchievements::*unlock)();
} kFestivalBackgrounds[] = {
    {BG_FESTIVAL_CHRISTMAS,  "圣诞",   &PetAchievements::IsChristmasUnlocked,       &PetAchievements::UnlockChristmas},
    {BG_FESTIVAL_BIRTHDAY,   "生日",   &PetAchievements::IsBirthdayUnlocked,        &PetAchievements::UnlockBirthday},
    {BG_FESTIVAL_SPRING,     "春节",   &PetAchievements::IsSpringFestivalUnlocked,  &PetAchievements::UnlockSpringFestival},
    {BG_FESTIVAL_NEWYEAR,    "元旦",   &PetAchievements::IsNewYearUnlocked,         &PetAchievements::UnlockNewYear},
    {BG_FESTIVAL_MIDAUTUMN,  "中秋",   &PetAchievements::IsMidAutumnUnlocked,       &PetAchievements::UnlockMidAutumn},
    {BG_FESTIVAL_HALLOWEEN,  "万圣节", &PetAchievements::IsHalloweenUnlocked,       &PetAchievements::UnlockHalloween},
    {BG_FESTIVAL_VALENTINES, "情人节", &PetAchievements::IsValentinesUnlocked,      &PetAchievements::UnlockValentines},
};

SceneItemState::SceneItemState()
    : coin_count(0)
    , poop_count(0)
    , last_coin_spawn_hour(25)  // Invalid hour to force first spawn
    , last_coin_spawn_day(0)
    , last_poop_spawn_day(0)
    , daily_poop_spawns(0)
    , next_poop_spawn_time(0)
{
    for (int i = 0; i < MAX_SCENE_COINS; i++) {
        coins[i] = SceneItem();
    }
    for (int i = 0; i < MAX_SCENE_POOPS; i++) {
        poops[i] = SceneItem();
    }
}

SceneItemManager& SceneItemManager::GetInstance() {
    static SceneItemManager instance;
    return instance;
}

SceneItemManager::SceneItemManager()
    : initialized_(false)
    , state_dirty_(false)
    , last_save_time_(0)
{
}

void SceneItemManager::Initialize() {
    if (initialized_) {
        return;
    }

    Load();
    CheckDailyReset();

    // Schedule first poop spawn if needed
    // Fix: Also reschedule if saved time is from previous boot cycle (stale)
    // esp_timer_get_time() resets to 0 on reboot, so saved next_poop_spawn_time
    // from previous boot will be invalid (too far in the future)
    int64_t now = esp_timer_get_time() / 1000;
    if (state_.next_poop_spawn_time == 0 ||
        state_.next_poop_spawn_time > now + POOP_SPAWN_MAX_INTERVAL_MS * 2) {
        ESP_LOGI(TAG, "Rescheduling poop spawn (saved time invalid or stale: %lld, now: %lld)",
                 (long long)state_.next_poop_spawn_time, (long long)now);
        ScheduleNextPoopSpawn();
    }

    initialized_ = true;
    ESP_LOGI(TAG, "Initialized: coins=%d, poops=%d, daily_poop_spawns=%d",
             state_.coin_count, state_.poop_count, state_.daily_poop_spawns);
}

void SceneItemManager::Tick() {
    if (!initialized_) {
        return;
    }

    // Get current hunger from pet
    auto& pet = PetStateMachine::GetInstance();
    const auto& stats = pet.GetStats();

    // Check poop spawn
    CheckPoopSpawn(stats.hunger);

    // Note: 金币生成已移至PetStateMachine::Tick()，根据饥饿和清洁值动态调整频率
    // CheckCoinSpawn() is now deprecated - coin spawning handled in PetStateMachine::Tick()

    // Check if delayed save is needed
    SaveIfNeeded();
}

void SceneItemManager::CheckDailyReset() {
    time_t now;
    struct tm timeinfo;
    if (time(&now) == -1 || localtime_r(&now, &timeinfo) == nullptr) {
        return;
    }

    uint16_t current_day = timeinfo.tm_yday + 1;  // 1-366

    if (state_.last_poop_spawn_day != current_day) {
        ESP_LOGI(TAG, "Daily reset: poop spawns reset (day %d -> %d)",
                 state_.last_poop_spawn_day, current_day);
        state_.last_poop_spawn_day = current_day;
        state_.daily_poop_spawns = 0;
        ScheduleNextPoopSpawn();
        Save();
    }
}

void SceneItemManager::CheckPoopSpawn(int hunger) {
    // Debug: 降低日志频率，每10分钟打印一次
    static uint32_t poop_gen_log = 0;
    if ((poop_gen_log++ % 600) == 0) {  // 每600秒（10分钟）打印一次
        int64_t now = esp_timer_get_time() / 1000;
        int64_t time_until_spawn = state_.next_poop_spawn_time - now;
        int seconds = time_until_spawn / 1000;
        int minutes = seconds / 60;
        ESP_LOGD(TAG, "💩 Poop gen check: hunger=%d (need>0), poops=%d/%d, daily=%d/%d, next_in=%dm%ds",
                 hunger, state_.poop_count, MAX_SCENE_POOPS,
                 state_.daily_poop_spawns, POOP_MAX_DAILY_SPAWNS, minutes, seconds % 60);
    }

    // Only spawn if hunger > 0 (饥饿度为0时不产生便便)
    if (hunger <= POOP_HUNGER_THRESHOLD) {
        return;
    }

    // Check if we've reached the max daily spawns
    if (state_.daily_poop_spawns >= POOP_MAX_DAILY_SPAWNS) {
        return;
    }

    // Check if there's room for more poops
    if (state_.poop_count >= MAX_SCENE_POOPS) {
        return;
    }

    // Check if it's time to spawn
    int64_t now = esp_timer_get_time() / 1000;  // Convert to ms
    if (state_.next_poop_spawn_time > 0 && now >= state_.next_poop_spawn_time) {
        SpawnPoop();
        ScheduleNextPoopSpawn();
    }
}

void SceneItemManager::SpawnPoop() {
    int slot = FindEmptySlot(state_.poops);
    if (slot < 0) return;

    int16_t x, y;
    GetRandomPosition(&x, &y);

    state_.poops[slot] = {x, y, SCENE_ITEM_POOP, 0, true};
    state_.poop_count++;
    state_.daily_poop_spawns++;

    ESP_LOGI(TAG, "Poop spawned at (%d, %d), total=%d, daily=%d",
             x, y, state_.poop_count, state_.daily_poop_spawns);

    // 触发"便便生成"对白
    AmbientDialogue::GetInstance().TriggerEvent(DialogueEvent::kPoopAppear);

    Save();
}

void SceneItemManager::ScheduleNextPoopSpawn() {
    // Only schedule if we haven't reached daily limit
    if (state_.daily_poop_spawns >= POOP_MAX_DAILY_SPAWNS) {
        state_.next_poop_spawn_time = 0;
        return;
    }

    // Random interval between min and max
    uint32_t interval = POOP_SPAWN_MIN_INTERVAL_MS +
        (esp_random() % (POOP_SPAWN_MAX_INTERVAL_MS - POOP_SPAWN_MIN_INTERVAL_MS));

    int64_t now = esp_timer_get_time() / 1000;
    state_.next_poop_spawn_time = now + interval;

    ESP_LOGI(TAG, "Next poop spawn scheduled in %lu ms", (unsigned long)interval);
}

void SceneItemManager::ClearAllPoops() {
    int coin_spawned = 0;

    for (int i = 0; i < MAX_SCENE_POOPS; i++) {
        if (state_.poops[i].active) {
            // 50% chance to spawn coin at poop location
            if ((esp_random() % 100) < 50) {
                SpawnCoinAt(state_.poops[i].x, state_.poops[i].y);
                coin_spawned++;
                ESP_LOGI(TAG, "Lucky! Coin spawned at poop location (%d, %d)",
                         state_.poops[i].x, state_.poops[i].y);
            }

            state_.poops[i].active = false;
            state_.poops[i].step_count = 0;
        }
    }
    state_.poop_count = 0;

    ESP_LOGI(TAG, "All poops cleared (bathing), %d coins spawned", coin_spawned);
    Save();
}

void SceneItemManager::CheckCoinSpawn() {
    // Only spawn if no poops exist
    if (state_.poop_count > 0) {
        return;
    }

    // Check if we've reached max coins
    if (state_.coin_count >= MAX_SCENE_COINS) {
        return;
    }

    // Get current time
    time_t now;
    struct tm timeinfo;
    if (time(&now) == -1 || localtime_r(&now, &timeinfo) == nullptr) {
        return;
    }

    uint16_t current_day = timeinfo.tm_yday + 1;
    uint32_t current_hour = timeinfo.tm_hour;

    // Check if hour changed
    if (current_day != state_.last_coin_spawn_day ||
        current_hour != state_.last_coin_spawn_hour) {
        SpawnCoin();
        state_.last_coin_spawn_day = current_day;
        state_.last_coin_spawn_hour = current_hour;
        MarkDirty();  // Delay save to reduce NVS blocking
    }
}

// Internal: spawn coin at position, returns true if successful
bool SceneItemManager::SpawnCoinInternal(int16_t x, int16_t y, bool log_position) {
    if (state_.coin_count >= MAX_SCENE_COINS) {
        ESP_LOGW(TAG, "Cannot spawn coin: already at max (%d)", MAX_SCENE_COINS);
        return false;
    }

    int slot = FindEmptySlot(state_.coins);
    if (slot < 0) {
        ESP_LOGW(TAG, "Cannot spawn coin: no empty slots");
        return false;
    }

    state_.coins[slot].x = x;
    state_.coins[slot].y = y;
    state_.coins[slot].type = SCENE_ITEM_COIN;
    state_.coins[slot].step_count = 0;
    state_.coins[slot].active = true;
    state_.coin_count++;

    if (log_position) {
        ESP_LOGI(TAG, "💰 Coin spawned at specific position (%d, %d), slot=%d, total=%d, active=%d",
                 x, y, slot, state_.coin_count, state_.coins[slot].active);
    } else {
        ESP_LOGI(TAG, "💰 Coin spawned at random position (%d, %d), slot=%d, total=%d, active=%d",
                 x, y, slot, state_.coin_count, state_.coins[slot].active);
        // 触发"金币出现"对白（仅随机生成时触发，不包括洗澡后生成的金币）
        AmbientDialogue::GetInstance().TriggerEvent(DialogueEvent::kCoinAppear);
    }

    MarkDirty();  // Delay save to reduce NVS blocking
    return true;
}

void SceneItemManager::SpawnCoin() {
    int16_t x, y;
    GetRandomPosition(&x, &y);
    SpawnCoinInternal(x, y, false);
}

void SceneItemManager::SpawnCoinAt(int16_t x, int16_t y) {
    SpawnCoinInternal(x, y, true);
}

void SceneItemManager::DebugSpawnItems() {
    // Force spawn both a coin and a poop at known positions for testing
    // Clear existing items first
    for (int i = 0; i < MAX_SCENE_COINS; i++) {
        state_.coins[i].active = false;
    }
    for (int i = 0; i < MAX_SCENE_POOPS; i++) {
        state_.poops[i].active = false;
    }
    state_.coin_count = 0;
    state_.poop_count = 0;

    // Spawn a coin at left side
    state_.coins[0].x = -40;  // Left of center
    state_.coins[0].y = 20;   // Below center
    state_.coins[0].type = SCENE_ITEM_COIN;
    state_.coins[0].step_count = 0;
    state_.coins[0].active = true;
    state_.coin_count = 1;

    // Spawn a poop at right side
    state_.poops[0].x = 40;   // Right of center
    state_.poops[0].y = 20;   // Below center
    state_.poops[0].type = SCENE_ITEM_POOP;
    state_.poops[0].step_count = 0;
    state_.poops[0].active = true;
    state_.poop_count = 1;

    ESP_LOGI(TAG, "[DEBUG] Test items spawned: coin at (-40,20), poop at (40,20)");
}

void SceneItemManager::CheckCollision(int16_t pet_x, int16_t pet_y) {
    // Check coin collisions
    for (int i = 0; i < MAX_SCENE_COINS; i++) {
        if (!state_.coins[i].active) continue;

        int16_t dist = GetDistance(pet_x, pet_y, state_.coins[i].x, state_.coins[i].y);
        if (dist < COIN_PICKUP_DISTANCE) {
            OnCoinPickup(i);
        }
    }

    // Check poop collisions
    for (int i = 0; i < MAX_SCENE_POOPS; i++) {
        if (!state_.poops[i].active) continue;

        int16_t dist = GetDistance(pet_x, pet_y, state_.poops[i].x, state_.poops[i].y);
        if (dist < POOP_STEP_DISTANCE) {
            OnPoopStep(i);
        }
    }
}

void SceneItemManager::OnCoinPickup(int index) {
    if (index < 0 || index >= MAX_SCENE_COINS || !state_.coins[index].active) {
        return;
    }

    // Deactivate coin
    state_.coins[index].active = false;
    state_.coin_count--;

    // Random reward 1-3 coins
    uint8_t reward = COIN_REWARD_MIN + (esp_random() % (COIN_REWARD_MAX - COIN_REWARD_MIN + 1));

    ESP_LOGI(TAG, "Coin picked up at (%d, %d), reward=%d",
             state_.coins[index].x, state_.coins[index].y, reward);

    // Add coins to system
    CoinSystem::GetInstance().AddCoins(reward);

    // 触发"捡到金币"对白
    AmbientDialogue::GetInstance().TriggerEvent(DialogueEvent::kCoinPickup);

    // Play pickup sound only in idle state (avoid sample rate switching during conversation)
    auto& app = Application::GetInstance();
    if (app.GetDeviceState() == kDeviceStateIdle) {
        app.PlaySound(Lang::Sounds::OGG_SUCCESS);
    }

    // 1% chance to unlock random background
    if ((esp_random() % 100) < COIN_UNLOCK_CHANCE) {
        TryUnlockRandomBackground();
    }

    MarkDirty();  // Delay save to reduce NVS blocking
}

void SceneItemManager::TryUnlockRandomBackground() {
    auto& achievements = PetAchievements::GetInstance();
    auto display = Board::GetInstance().GetDisplay();

    // Collect indices of locked festival backgrounds
    std::vector<size_t> locked_indices;
    for (size_t i = 0; i < sizeof(kFestivalBackgrounds) / sizeof(kFestivalBackgrounds[0]); i++) {
        if (!(achievements.*kFestivalBackgrounds[i].is_unlocked)()) {
            locked_indices.push_back(i);
        }
    }

    if (locked_indices.empty()) {
        ESP_LOGI(TAG, "1%% luck triggered but all festival backgrounds already unlocked");
        if (display) {
            display->ShowNotification("幸运! 所有背景已解锁!", 3000);
        }
        return;
    }

    // Unlock a random locked festival background
    size_t idx = locked_indices[esp_random() % locked_indices.size()];
    const auto& bg = kFestivalBackgrounds[idx];
    (achievements.*bg.unlock)();

    ESP_LOGI(TAG, "1%% luck! Unlocked festival background: %s (idx=%d)", bg.name, bg.bg_index);

    char notification[64];
    snprintf(notification, sizeof(notification), "幸运解锁: %s背景!", bg.name);
    if (display) {
        display->ShowNotification(notification, 5000);
    }
}

void SceneItemManager::OnPoopStep(int index) {
    if (index < 0 || index >= MAX_SCENE_POOPS || !state_.poops[index].active) {
        return;
    }

    // Check cooldown (10秒内不能重复踩同一个便便)
    int64_t now = esp_timer_get_time() / 1000;  // Convert to ms
    if (state_.poops[index].last_step_time > 0) {
        int64_t time_since_last_step = now - state_.poops[index].last_step_time;
        if (time_since_last_step < POOP_STEP_COOLDOWN_MS) {
            ESP_LOGD(TAG, "Poop step cooldown active (%lld ms remaining)",
                     (long long)(POOP_STEP_COOLDOWN_MS - time_since_last_step));
            return;
        }
    }

    // Update last step time
    state_.poops[index].last_step_time = now;

    // Increment step count
    state_.poops[index].step_count++;

    ESP_LOGI(TAG, "Poop stepped on at (%d, %d), step_count=%d",
             state_.poops[index].x, state_.poops[index].y, state_.poops[index].step_count);

    // Reduce cleanliness and happiness (优化后：-2清洁度，-2心情)
    auto& pet = PetStateMachine::GetInstance();
    pet.ReduceCleanliness(2);

    // 触发"踩到便便"对白
    AmbientDialogue::GetInstance().TriggerEvent(DialogueEvent::kPoopStep);

    // Check if poop should be deactivated (max steps reached)
    if (state_.poops[index].step_count >= POOP_MAX_STEP_COUNT) {
        int16_t poop_x = state_.poops[index].x;
        int16_t poop_y = state_.poops[index].y;
        state_.poops[index].active = false;
        state_.poop_count--;
        ESP_LOGI(TAG, "Poop deactivated after %d steps", POOP_MAX_STEP_COUNT);

        // 50% chance to spawn coin at poop location (compensation for stepping)
        if ((esp_random() % 100) < 50) {
            SpawnCoinAt(poop_x, poop_y);
            ESP_LOGI(TAG, "Lucky! Coin spawned at stepped poop location (%d, %d)", poop_x, poop_y);
        }
    }

    MarkDirty();  // Delay save to reduce NVS blocking
}

void SceneItemManager::GetRandomPosition(int16_t* x, int16_t* y) {
    // Random position within spawn area
    *x = (int16_t)(esp_random() % (ITEM_SPAWN_MAX_X * 2 + 1)) - ITEM_SPAWN_MAX_X;
    *y = (int16_t)(esp_random() % (ITEM_SPAWN_MAX_Y * 2 + 1)) - ITEM_SPAWN_MAX_Y;
}

int16_t SceneItemManager::GetDistance(int16_t x1, int16_t y1, int16_t x2, int16_t y2) {
    int32_t dx = x2 - x1;
    int32_t dy = y2 - y1;
    return (int16_t)sqrt(dx * dx + dy * dy);
}

const SceneItem* SceneItemManager::GetCoins(uint8_t* out_count) const {
    if (out_count) {
        *out_count = state_.coin_count;
    }
    return state_.coins;
}

const SceneItem* SceneItemManager::GetPoops(uint8_t* out_count) const {
    if (out_count) {
        *out_count = state_.poop_count;
    }
    return state_.poops;
}

void SceneItemManager::Save() {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for write: %s", esp_err_to_name(err));
        return;
    }

    err = nvs_set_blob(handle, NVS_KEY_STATE, &state_, sizeof(SceneItemState));
    if (err == ESP_OK) {
        nvs_commit(handle);
        ESP_LOGD(TAG, "Saved scene items state to NVS");
    } else {
        ESP_LOGE(TAG, "Failed to save scene items state: %s", esp_err_to_name(err));
    }
    nvs_close(handle);
}

void SceneItemManager::Load() {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "No saved scene items state, using defaults");
        state_ = SceneItemState();
        return;
    }

    size_t size = sizeof(SceneItemState);
    err = nvs_get_blob(handle, NVS_KEY_STATE, &state_, &size);
    if (err != ESP_OK || size != sizeof(SceneItemState)) {
        ESP_LOGW(TAG, "Invalid scene items state in NVS, using defaults");
        state_ = SceneItemState();
    }
    nvs_close(handle);

    // Recount active items
    state_.coin_count = 0;
    for (int i = 0; i < MAX_SCENE_COINS; i++) {
        if (state_.coins[i].active) state_.coin_count++;
    }
    state_.poop_count = 0;
    for (int i = 0; i < MAX_SCENE_POOPS; i++) {
        if (state_.poops[i].active) state_.poop_count++;
    }
}

void SceneItemManager::MarkDirty() {
    state_dirty_ = true;
}

void SceneItemManager::SaveIfNeeded() {
    if (!state_dirty_) return;

    uint32_t now = esp_timer_get_time() / 1000;  // Convert to ms
    if (now - last_save_time_ >= SAVE_INTERVAL_MS) {
        Save();
        state_dirty_ = false;
        last_save_time_ = now;
        ESP_LOGI(TAG, "Deferred save completed");
    }
}

void SceneItemManager::ForceSave() {
    if (state_dirty_) {
        Save();
        state_dirty_ = false;
        ESP_LOGI(TAG, "Force save completed");
    }
}

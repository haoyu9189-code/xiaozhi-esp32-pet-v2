#include "pet_coin.h"
#include "pet_event_log.h"
#include <nvs_flash.h>
#include <nvs.h>
#include <esp_log.h>
#include <time.h>

#define TAG "CoinSystem"
#define NVS_NAMESPACE "pet_coin"
#define NVS_KEY_STATE "state"

// Chat milestone rewards: {message_count, coin_reward}
static const struct { uint32_t count; uint8_t reward; } kChatMilestones[] = {
    {1, 2},   // 1st message: +2 coins
    {5, 2},   // 5th message: +2 coins
    {6, 2},   // 6th message: +2 coins
};


CoinSystem& CoinSystem::GetInstance() {
    static CoinSystem instance;
    return instance;
}

void CoinSystem::Initialize() {
    Load();
    CheckDailyReset();
    ESP_LOGI(TAG, "Initialized: coins=%d, daily_chat=%lu",
             state_.coins, (unsigned long)state_.daily_chat_count);
}

bool CoinSystem::SpendCoins(uint8_t amount) {
    ESP_LOGI(TAG, "SpendCoins called: need=%d, current=%d", amount, state_.coins);
    if (state_.coins < amount) {
        ESP_LOGW(TAG, "Insufficient coins: have %d, need %d", state_.coins, amount);
        return false;
    }
    state_.coins -= amount;
    state_.total_coins_spent += amount;  // Track cumulative spending for intimacy system
    Save();
    ESP_LOGI(TAG, "✅ Spent %d coins, remaining: %d", amount, state_.coins);
    if (coin_callback_) {
        coin_callback_(state_.coins, "spent");
        ESP_LOGI(TAG, "Coin callback triggered for status update");
    }
    return true;
}

void CoinSystem::AddCoins(uint8_t amount) {
    uint16_t new_total = state_.coins + amount;
    uint8_t old_coins = state_.coins;
    state_.coins = (new_total > MAX_COINS) ? MAX_COINS : static_cast<uint8_t>(new_total);

    ESP_LOGI(TAG, "Added %d coins: %d -> %d", amount, old_coins, state_.coins);

    Save();
    if (coin_callback_) {
        coin_callback_(state_.coins, "earned");
    }

}

void CoinSystem::OnChatMessage() {
    CheckDailyReset();
    state_.daily_chat_count++;

    // Check fixed milestones first
    uint8_t reward = 0;
    for (const auto& m : kChatMilestones) {
        if (state_.daily_chat_count == m.count) {
            reward = m.reward;
            ESP_LOGI(TAG, "Chat milestone (message %lu): +%d coins",
                     (unsigned long)m.count, m.reward);
            break;
        }
    }

    // Check repeating milestone (every 10 messages after 6th)
    if (reward == 0 && state_.daily_chat_count > CHAT_MILESTONE_3 &&
        (state_.daily_chat_count - CHAT_MILESTONE_3) % 10 == 0) {
        reward = 1;
        ESP_LOGI(TAG, "Chat milestone (every 10 after 6th): +1 coin at message %lu",
                 (unsigned long)state_.daily_chat_count);
    }

    if (reward > 0) {
        AddCoins(reward);
    }
    Save();
}

void CoinSystem::CheckDailyReset() {
    time_t now;
    struct tm timeinfo;
    if (time(&now) == -1 || localtime_r(&now, &timeinfo) == nullptr) {
        ESP_LOGW(TAG, "Failed to get current time for daily reset check");
        return;
    }

    uint16_t current_year = timeinfo.tm_year + 1900;
    uint16_t current_day = timeinfo.tm_yday + 1;  // tm_yday is 0-365

    if (state_.last_reset_year != current_year ||
        state_.last_reset_day != current_day) {
        ESP_LOGI(TAG, "Daily reset: old=%d/%d, new=%d/%d",
                 state_.last_reset_year, state_.last_reset_day,
                 current_year, current_day);
        state_.daily_chat_count = 0;
        state_.last_reset_year = current_year;
        state_.last_reset_day = current_day;
        Save();
    }
}

void CoinSystem::CheckAutoConsumption() {
    // 吃饭/洗澡/背景奖励均通过MCP工具由AI触发，不自动消费
}

void CoinSystem::CheckRewardTimer() {
    // 背景奖励通过MCP工具由AI触发，不自动触发
}

void CoinSystem::Save() {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for write: %s", esp_err_to_name(err));
        return;
    }

    err = nvs_set_blob(handle, NVS_KEY_STATE, &state_, sizeof(CoinState));
    if (err == ESP_OK) {
        nvs_commit(handle);
        ESP_LOGD(TAG, "Saved coin state to NVS");
    } else {
        ESP_LOGE(TAG, "Failed to save coin state: %s", esp_err_to_name(err));
    }
    nvs_close(handle);
}

void CoinSystem::Load() {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "No saved coin state, using defaults");
        state_ = CoinState();
        return;
    }

    size_t size = sizeof(CoinState);
    err = nvs_get_blob(handle, NVS_KEY_STATE, &state_, &size);
    if (err != ESP_OK || size != sizeof(CoinState)) {
        ESP_LOGW(TAG, "Invalid coin state in NVS, using defaults");
        state_ = CoinState();
    }
    nvs_close(handle);
}

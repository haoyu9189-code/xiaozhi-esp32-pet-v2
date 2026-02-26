#include "pet_state.h"
#include "pet_achievements.h"
#include "pet_coin.h"
#include "scene_items.h"
#include "ambient_dialogue.h"
#include <nvs_flash.h>
#include <nvs.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_random.h>
#include <cstring>

#define TAG "PetState"

#define NVS_NAMESPACE "pet_state"
#define NVS_KEY_STATS "stats"

PetStateMachine& PetStateMachine::GetInstance() {
    static PetStateMachine instance;
    return instance;
}

void PetStateMachine::Initialize() {
    ESP_LOGI(TAG, "Initializing pet state machine");
    Load();

    current_action_ = PetAction::kIdle;
    ESP_LOGI(TAG, "Pet initialized: hunger=%d, happiness=%d, cleanliness=%d",
             stats_.hunger, stats_.happiness, stats_.cleanliness);
}

void PetStateMachine::Tick() {
    // 检查定时动作是否结束
    UpdateActionTimer();

    // === 持续恢复机制（吃饭/洗澡期间每分钟恢复，独立于动画显示） ===
    // 检查持续性恢复是否超时（5分钟）
    if (continuous_recovery_action_ != PetAction::kIdle) {
        uint32_t now = esp_timer_get_time() / 1000;
        if (now - continuous_recovery_start_ >= continuous_recovery_duration_) {
            ESP_LOGI(TAG, "Continuous recovery timeout, stopping %s",
                     ActionToString(continuous_recovery_action_));
            PetAction old_recovery = continuous_recovery_action_;
            continuous_recovery_action_ = PetAction::kIdle;
            // 如果当前动画是该持续动作，切换到合适的状态
            if (current_action_ == old_recovery) {
                RestoreIdleAction();
            }
        }
    }

    bool feeding_active = (continuous_recovery_action_ == PetAction::kEating);
    bool bathing_active = (continuous_recovery_action_ == PetAction::kBathing);

    if (feeding_active) {
        int old_hunger = stats_.hunger;
        stats_.hunger = Clamp(stats_.hunger + 20);
        // 吃饭增加心情：饥饿值增量的一半
        int hunger_gain = stats_.hunger - old_hunger;
        stats_.happiness = Clamp(stats_.happiness + hunger_gain / 2);
        ESP_LOGI(TAG, "Eating... hunger: %d -> %d, happiness: +%d",
                 old_hunger, stats_.hunger, hunger_gain / 2);

        // 满了自动退出
        if (stats_.hunger >= STAT_FULL) {
            ESP_LOGI(TAG, "Hunger full! Stop eating.");
            // 触发"吃饱了"对白
            AmbientDialogue::GetInstance().TriggerEvent(DialogueEvent::kFullEating);
            continuous_recovery_action_ = PetAction::kIdle;
            // 如果当前动画是eating，切换到合适的状态
            if (current_action_ == PetAction::kEating) {
                RestoreIdleAction();
            }
        }
    }

    if (bathing_active) {
        int old_cleanliness = stats_.cleanliness;
        stats_.cleanliness = Clamp(stats_.cleanliness + 20);
        // 洗澡增加心情：清洁值增量的一半
        int cleanliness_gain = stats_.cleanliness - old_cleanliness;
        stats_.happiness = Clamp(stats_.happiness + cleanliness_gain / 2);
        ESP_LOGI(TAG, "Bathing... cleanliness: %d -> %d, happiness: +%d",
                 old_cleanliness, stats_.cleanliness, cleanliness_gain / 2);

        // 满了自动退出
        if (stats_.cleanliness >= STAT_FULL) {
            ESP_LOGI(TAG, "Cleanliness full! Stop bathing.");
            // 触发"洗完澡"对白
            AmbientDialogue::GetInstance().TriggerEvent(DialogueEvent::kFullBathing);
            continuous_recovery_action_ = PetAction::kIdle;
            // 如果当前动画是bathing，切换到合适的状态
            if (current_action_ == PetAction::kBathing) {
                RestoreIdleAction();
            }
        }
    }

    // === 属性衰减（使用计数器控制频率） ===
    decay_tick_counter_++;

    // 检查大便数量（大便负面效果可叠加）
    auto& scene = SceneItemManager::GetInstance();
    uint8_t poop_count = scene.GetPoopCount();

    // 饥饿度：基础每6分钟-1，每个大便加速
    // 0便便:6分钟 1便便:4分钟 2便便:3分钟 3便便:2分钟
    int hunger_interval = 6;
    if (poop_count >= 3) {
        hunger_interval = 2;  // 3个大便：每2分钟-1
    } else if (poop_count == 2) {
        hunger_interval = 3;  // 2个大便：每3分钟-1
    } else if (poop_count == 1) {
        hunger_interval = 4;  // 1个大便：每4分钟-1
    }
    if (decay_tick_counter_ % hunger_interval == 0) {
        stats_.hunger = Clamp(stats_.hunger - decay_config_.hunger_per_min);
    }

    // 心情：基础每3分钟-1（便便不再加速心情衰减，只在踩到时才减心情）
    if (decay_tick_counter_ % 3 == 0) {
        stats_.happiness = Clamp(stats_.happiness - 1);
    }

    // 清洁度：基础每30分钟-1（优化后），每个大便加速
    // 0便便:30分钟 1便便:20分钟 2便便:12分钟 3便便:6分钟
    int cleanliness_interval = 30;
    if (poop_count >= 3) {
        cleanliness_interval = 6;  // 3个大便：每6分钟-1
    } else if (poop_count == 2) {
        cleanliness_interval = 12;  // 2个大便：每12分钟-1
    } else if (poop_count == 1) {
        cleanliness_interval = 20;  // 1个大便：每20分钟-1
    }
    if (decay_tick_counter_ % cleanliness_interval == 0) {
        stats_.cleanliness = Clamp(stats_.cleanliness - decay_config_.cleanliness_per_min);
    }

    // 重置计数器防止溢出（LCM(2,3,4,6,12,20,30)=60）
    if (decay_tick_counter_ >= 60) {
        decay_tick_counter_ = 0;
    }

    // Debug: 每60分钟报告一次大便惩罚情况
    static uint32_t poop_penalty_log = 0;
    if (poop_count > 0 && (poop_penalty_log++ % 60) == 0) {
        ESP_LOGI(TAG, "💩 Poop penalty active! count=%d, hunger_interval=%dmin, clean_interval=%dmin",
                 poop_count, hunger_interval, cleanliness_interval);
    }

    // 年龄增加
    stats_.age_minutes++;

    // === 金币生成机制 ===
    // 条件：饥饿度>50 && 清洁度>50 && 心情>50（三个属性都要维持）
    // 频率：根据三个属性的平均值动态调整
    //  平均>90: 每2分钟  平均>80: 每5分钟  平均>70: 每10分钟  平均>50: 每15分钟
    // 惩罚：有大便时速度减半（间隔翻倍）

    // Debug: 每分钟报告一次金币生成条件
    ESP_LOGI(TAG, "💰 Coin check: H=%d%s C=%d%s HP=%d timer=%lu poops=%d coins=%d/%d",
             stats_.hunger, stats_.hunger > 50 ? "✓" : "✗",
             stats_.cleanliness, stats_.cleanliness > 50 ? "✓" : "✗",
             stats_.happiness,
             happy_coin_timer_, poop_count, scene.GetCoinCount(), MAX_SCENE_COINS);

    if (stats_.hunger > STAT_GOOD_THRESHOLD && stats_.cleanliness > STAT_GOOD_THRESHOLD) {
        // 条件满足，重置保底计时器
        stats_.coin_blocked_minutes = 0;

        // 计算饥饿和清洁的平均值（心情不影响生成条件，只影响显示）
        int avg_attr = (stats_.hunger + stats_.cleanliness) / 2;

        // 根据平均值确定生成间隔（频率翻倍）
        uint32_t spawn_interval;
        if (avg_attr >= 90) {
            spawn_interval = 2;   // 每2分钟（平均≥90）
        } else if (avg_attr >= 80) {
            spawn_interval = 5;   // 每5分钟（平均80-89）
        } else if (avg_attr >= 70) {
            spawn_interval = 10;  // 每10分钟（平均70-79）
        } else {
            spawn_interval = 15;  // 每15分钟（平均51-69）
        }

        // 大便惩罚：每个大便增加50%间隔（可叠加）
        // 0便便:×1  1便便:×1.5  2便便:×2  3便便:×2.5
        uint32_t base_interval = spawn_interval;  // 保存基础间隔用于日志
        if (poop_count > 0) {
            spawn_interval = spawn_interval * (10 + poop_count * 5) / 10;
        }

        // Debug: 每10分钟报告一次金币生成状态（移到这里以显示完整信息）
        static uint32_t coin_gen_log = 0;
        if ((coin_gen_log++ % 10) == 0) {
            ESP_LOGI(TAG, "💰 Coin gen: H=%d C=%d avg=%d → base=%lum poops=%d → final=%lum, timer=%lu/%lu, coins=%d/%d",
                     stats_.hunger, stats_.cleanliness, avg_attr,
                     (unsigned long)base_interval, poop_count, (unsigned long)spawn_interval,
                     happy_coin_timer_, (unsigned long)spawn_interval,
                     scene.GetCoinCount(), MAX_SCENE_COINS);
        }

        happy_coin_timer_++;
        if (happy_coin_timer_ >= spawn_interval) {
            happy_coin_timer_ = 0;

            // 检查是否可以生成金币
            if (scene.GetCoinCount() < MAX_SCENE_COINS) {
                scene.SpawnCoin();
                ESP_LOGI(TAG, "💰 Spawned coin! (H:%d C:%d P:%d avg=%d, interval=%lum, poops=%d)",
                         stats_.hunger, stats_.cleanliness, stats_.happiness, avg_attr,
                         (unsigned long)spawn_interval, poop_count);
            } else {
                ESP_LOGD(TAG, "Cannot spawn coin: already at max (%d)", MAX_SCENE_COINS);
            }
        }
    } else {
        // 条件不满足：任一属性 ≤ 50
        stats_.coin_blocked_minutes++;  // 递增被阻止的时间（持久化到NVS）

        // Debug: 每10分钟报告一次，即使条件不满足
        static uint32_t coin_blocked_log = 0;
        if ((coin_blocked_log++ % 10) == 0) {
            ESP_LOGW(TAG, "💰 Coin gen BLOCKED: H=%d%s C=%d%s HP=%d (need H&C >50), blocked_time=%lu/180min",
                     stats_.hunger, stats_.hunger <= 50 ? "⚠" : "",
                     stats_.cleanliness, stats_.cleanliness <= 50 ? "⚠" : "",
                     stats_.happiness,
                     (unsigned long)stats_.coin_blocked_minutes);
        }

        // 保底机制：3小时（180分钟）不满足条件，强制生成3个金币（上限10个）
        constexpr int FAILSAFE_COIN_LIMIT = 10;  // 保底金币上限
        if (stats_.coin_blocked_minutes >= 180) {
            ESP_LOGI(TAG, "💰 FAILSAFE triggered! 3 hours without coin spawn, forcing 3 coins");
            int spawned = 0;
            for (int i = 0; i < 3 && scene.GetCoinCount() < FAILSAFE_COIN_LIMIT; i++) {
                scene.SpawnCoin();
                spawned++;
            }
            ESP_LOGI(TAG, "💰 Failsafe spawned %d coins (total now: %d, limit: %d)",
                     spawned, scene.GetCoinCount(), FAILSAFE_COIN_LIMIT);
            stats_.coin_blocked_minutes = 0;  // 重置保底计时器
        }

        happy_coin_timer_ = 0;  // 重置正常生成计时器
    }

    // 2. 饥饿和清洁度都满时，心情恢复满
    if (stats_.hunger >= STAT_FULL && stats_.cleanliness >= STAT_FULL) {
        if (stats_.happiness < STAT_FULL) {
            stats_.happiness = STAT_FULL;
            ESP_LOGI(TAG, "Both full (hunger & cleanliness) -> happiness restored to 100!");
        }
    }

    // 保存状态
    Save();

    ESP_LOGD(TAG, "Tick: hunger=%d, happiness=%d, cleanliness=%d",
             stats_.hunger, stats_.happiness, stats_.cleanliness);
}

// 提取的公共方法：开始持续恢复（吃饭/洗澡）
void PetStateMachine::StartContinuousRecovery(PetAction action, DialogueEvent start_event) {
    continuous_recovery_action_ = action;
    continuous_recovery_start_ = esp_timer_get_time() / 1000;
    continuous_recovery_duration_ = RECOVERY_DURATION_MS;

    SetAction(action, RECOVERY_DURATION_MS);
    if (in_voice_interaction_) {
        voice_animation_locked_ = true;
    } else {
        AmbientDialogue::GetInstance().TriggerEvent(start_event);
    }
    ESP_LOGI(TAG, "Started %s (in_voice=%d, locked=%d)",
             ActionToString(action), in_voice_interaction_, voice_animation_locked_);
}

// 提取的公共方法：恢复到空闲/聆听状态
void PetStateMachine::RestoreIdleAction() {
    if (in_voice_interaction_) {
        SetAction(PetAction::kListening);
    } else {
        SetAction(PetAction::kIdle);
    }
}

void PetStateMachine::Feed() {
    ESP_LOGI(TAG, "Feed requested");
    StartContinuousRecovery(PetAction::kEating, DialogueEvent::kStartEating);
    PetAchievements::GetInstance().OnFeed();
    Save();
}

int PetStateMachine::Bathe() {
    ESP_LOGI(TAG, "Bathe requested");
    StartContinuousRecovery(PetAction::kBathing, DialogueEvent::kStartBathing);
    PetAchievements::GetInstance().OnBathe();

    // 洗澡时清除所有便便
    SceneItemManager::GetInstance().ClearAllPoops();

    Save();
    return 20;
}


void PetStateMachine::ReduceCleanliness(int amount) {
    // 踩便便：清洁度-2, 心情-2（优化后的惩罚值）
    int happiness_penalty = 2;  // 心情固定惩罚-2
    ESP_LOGI(TAG, "Stepped on poop: cleanliness -%d, happiness -%d", amount, happiness_penalty);
    stats_.cleanliness = Clamp(stats_.cleanliness - amount);
    stats_.happiness = Clamp(stats_.happiness - happiness_penalty);
    Save();
}

void PetStateMachine::OnConversationEnd() {
    // 动态计算对话奖励：基础5 + 消息数奖励(最多+5) + 查看状态(+5) + 照顾行为(+5)
    int reward = 5;  // 基础分

    // 消息数奖励：每2条消息+1，最多+5
    int msg_bonus = session_msg_count_ / 2;
    if (msg_bonus > 5) msg_bonus = 5;
    reward += msg_bonus;

    // 查看状态奖励
    if (session_checked_status_) {
        reward += 5;
    }

    // 照顾行为奖励（喂食/洗澡）
    if (session_did_care_) {
        reward += 5;
    }

    stats_.happiness = Clamp(stats_.happiness + reward);
    ESP_LOGI(TAG, "Conversation end: happiness +%d (msgs=%d, status=%d, care=%d, current: %d)",
             reward, session_msg_count_, session_checked_status_, session_did_care_, stats_.happiness);

    // 重置session追踪
    session_msg_count_ = 0;
    session_checked_status_ = false;
    session_did_care_ = false;

    PetAchievements::GetInstance().OnConversation();
    Save();
}

bool PetStateMachine::Move(MoveDirection direction, int16_t distance) {
    if (!move_callback_) {
        ESP_LOGW(TAG, "Move callback not set");
        return false;
    }

    const char* dir_name = "unknown";
    switch (direction) {
        case MoveDirection::kUp:    dir_name = "up"; break;
        case MoveDirection::kDown:  dir_name = "down"; break;
        case MoveDirection::kLeft:  dir_name = "left"; break;
        case MoveDirection::kRight: dir_name = "right"; break;
    }

    ESP_LOGI(TAG, "Move request: direction=%s, distance=%d", dir_name, distance);
    return move_callback_(direction, distance);
}

void PetStateMachine::OnDeviceStateChanged(DeviceState old_state, DeviceState new_state) {
    // 吃饭/洗澡期间：无条件保护动画，任何状态切换都不覆盖
    bool in_recovery = (continuous_recovery_action_ != PetAction::kIdle);
    if (in_recovery) {
        uint32_t elapsed = esp_timer_get_time() / 1000 - continuous_recovery_start_;
        if (elapsed >= continuous_recovery_duration_) {
            // 恢复已结束，清除保护
            in_recovery = false;
            continuous_recovery_action_ = PetAction::kIdle;
        }
    }

    switch (new_state) {
        case kDeviceStateListening:
            if (!in_voice_interaction_) {
                previous_pet_action_ = current_action_;
                in_voice_interaction_ = true;
            }
            // 吃饭/洗澡中 → 不切换动画
            if (!in_recovery && !voice_animation_locked_) {
                SetAction(PetAction::kListening);
            }
            break;

        case kDeviceStateSpeaking:
            if (!in_recovery && !voice_animation_locked_) {
                SetAction(PetAction::kSpeaking);
            }
            break;

        case kDeviceStateConnecting:
            if (!in_recovery && !voice_animation_locked_) {
                SetAction(PetAction::kThinking);
            }
            break;

        case kDeviceStateIdle:
            if (in_voice_interaction_) {
                in_voice_interaction_ = false;
                voice_animation_locked_ = false;
            }

            if (in_recovery) {
                // 吃饭/洗澡中 → 确保动画继续播放
                uint32_t elapsed = esp_timer_get_time() / 1000 - continuous_recovery_start_;
                uint32_t remaining = continuous_recovery_duration_ - elapsed;
                ESP_LOGI(TAG, "Idle but %s in progress, continuing animation (remaining: %lu ms)",
                         ActionToString(continuous_recovery_action_), remaining);
                SetAction(continuous_recovery_action_, remaining);
            } else if (current_action_ != PetAction::kListening &&
                       current_action_ != PetAction::kSpeaking &&
                       current_action_ != PetAction::kThinking) {
                // 非语音动作（如walk），保持不变
                ESP_LOGI(TAG, "Idle, keeping current action: %s", ActionToString(current_action_));
            } else {
                SetAction(PetAction::kIdle);
            }
            break;

        default:
            break;
    }
}

void PetStateMachine::SetAction(PetAction action, uint32_t duration_ms) {
    current_action_ = action;
    action_duration_ = duration_ms;

    if (duration_ms > 0) {
        action_start_time_ = esp_timer_get_time() / 1000;  // 转换为毫秒
    } else {
        action_start_time_ = 0;
    }

    const char* animation = ActionToAnimation(action);
    ESP_LOGI(TAG, "SetAction: %s -> animation: %s, duration: %lu ms",
             ActionToString(action), animation, duration_ms);

    // 触发回调
    if (action_callback_) {
        action_callback_(action, animation);
    }
}

void PetStateMachine::UpdateActionTimer() {
    if (action_duration_ == 0) {
        return;
    }

    uint32_t now = esp_timer_get_time() / 1000;
    if (now - action_start_time_ >= action_duration_) {
        // 定时动作结束
        ESP_LOGI(TAG, "Timed action ended, returning to %s",
                 in_voice_interaction_ ? "listening" : "idle");
        action_duration_ = 0;
        // 如果在语音交互中，恢复到listening；否则回到idle
        if (in_voice_interaction_) {
            SetAction(PetAction::kListening);
        } else {
            SetAction(PetAction::kIdle);
        }
    }
}

const char* PetStateMachine::GetMoodDescription() const {
    if (stats_.hunger < 30) return "很饿，想吃东西";
    if (stats_.cleanliness < 30) return "脏脏的，想洗澡";
    if (stats_.happiness < 30) return "心情不好";
    if (stats_.happiness >= STAT_FULL) return "心情超好！";
    if (stats_.IsBothFull()) return "吃饱喝足，非常满足";
    return "状态正常";
}

const char* PetStateMachine::GetCurrentAnimation() const {
    return ActionToAnimation(current_action_);
}

const char* PetStateMachine::ActionToString(PetAction action) {
    switch (action) {
        case PetAction::kIdle:      return "idle";
        case PetAction::kEating:    return "eating";
        case PetAction::kBathing:   return "bathing";
        case PetAction::kSleeping:  return "sleeping";
        case PetAction::kPlaying:   return "playing";
        case PetAction::kSick:      return "sick";
        case PetAction::kListening: return "listening";
        case PetAction::kSpeaking:  return "speaking";
        case PetAction::kThinking:  return "thinking";
        default: return "unknown";
    }
}

const char* PetStateMachine::ActionToAnimation(PetAction action) {
    switch (action) {
        // 宠物专属动作
        case PetAction::kIdle:      return "idle";       // 待机
        case PetAction::kEating:    return "eat";        // 吃饭
        case PetAction::kBathing:   return "bath";       // 洗澡
        case PetAction::kSleeping:  return "sleep";      // 睡觉
        case PetAction::kPlaying:   return "walk";       // 玩耍用行走动画
        case PetAction::kSick:      return "sleep";      // 生病用睡觉动画

        // 与小智语音联动的动作
        case PetAction::kListening: return "listen";     // 聆听
        case PetAction::kSpeaking:  return "talk";       // 讲话
        case PetAction::kThinking:  return "idle";       // 思考用待机动画

        default: return "idle";
    }
}

void PetStateMachine::Save() {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return;
    }

    err = nvs_set_blob(handle, NVS_KEY_STATS, &stats_, sizeof(PetStats));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save pet stats: %s", esp_err_to_name(err));
    } else {
        nvs_commit(handle);
        ESP_LOGD(TAG, "Pet stats saved");
    }

    nvs_close(handle);
}

void PetStateMachine::Load() {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "No saved pet state found, using defaults");
        stats_ = PetStats();
        return;
    }

    size_t size = sizeof(PetStats);
    err = nvs_get_blob(handle, NVS_KEY_STATS, &stats_, &size);
    if (err != ESP_OK || size != sizeof(PetStats)) {
        ESP_LOGW(TAG, "Failed to load pet stats or size mismatch, using defaults");
        stats_ = PetStats();
    } else {
        ESP_LOGI(TAG, "Pet stats loaded from NVS");
    }

    nvs_close(handle);
}

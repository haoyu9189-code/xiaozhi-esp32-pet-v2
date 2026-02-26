#ifndef PET_STATE_H
#define PET_STATE_H

#include <cstdint>
#include <functional>
#include "device_state.h"

// 前向声明（避免循环依赖）
enum class DialogueEvent : uint8_t;

// 宠物系统常量
static constexpr uint32_t RECOVERY_DURATION_MS = 5 * 60 * 1000;  // 持续恢复时长（5分钟）
static constexpr int8_t STAT_GOOD_THRESHOLD = 50;                // 属性"良好"阈值
static constexpr int8_t STAT_FULL = 100;                         // 属性满值

// 宠物动作状态（用于动画绑定）
enum class PetAction : uint8_t {
    kIdle,          // 待机 → idle
    kEating,        // 吃东西 → eat
    kBathing,       // 洗澡 → bath
    kSleeping,      // 睡觉 → sleep
    kPlaying,       // 玩耍 → walk
    kSick,          // 生病 → sleep
    // 以下与小智语音交互联动
    kListening,     // 聆听用户 → listen
    kSpeaking,      // 说话中 → talk
    kThinking,      // 思考中 → idle
};

// 移动方向（用于MCP语音控制）
enum class MoveDirection : uint8_t {
    kUp,
    kDown,
    kLeft,
    kRight,
};

// 宠物属性
struct PetStats {
    int8_t hunger;      // 饱食度 0-100
    int8_t happiness;   // 心情值 0-100
    int8_t cleanliness; // 清洁度 0-100
    uint32_t age_minutes; // 年龄（分钟）
    uint32_t coin_blocked_minutes; // 金币生成被阻止的累计时间（分钟），用于保底机制

    PetStats() : hunger(80), happiness(80), cleanliness(80), age_minutes(0), coin_blocked_minutes(0) {}

    // Helper methods for checking thresholds
    bool NeedsBathing() const { return cleanliness < 30; }
    bool IsVeryHappy() const { return happiness >= 100; }
    bool IsBothFull() const { return hunger >= 100 && cleanliness >= 100; }
};

// 属性衰减配置
struct DecayConfig {
    int8_t hunger_per_min = 1;     // 饱食度衰减量（每3分钟执行一次）
    int8_t happiness_per_min = 1;  // 快乐度衰减量（每分钟执行）
    int8_t cleanliness_per_min = 1;// 清洁度衰减量（每10分钟执行一次）
};

class PetStateMachine {
public:
    static PetStateMachine& GetInstance();

    // 初始化（从NVS加载或创建新宠物）
    void Initialize();

    // 每分钟调用一次
    void Tick();

    // 用户交互
    void Feed();        // 喂食 → 持续吃5分钟，每分钟hunger+20，满了自动退出
    int Bathe();        // 洗澡 → 持续洗5分钟，每分钟cleanliness+20，满了自动退出，clears poop
    void ReduceCleanliness(int amount);  // 减少清洁度和心情值（踩便便时调用）

    // 对话结束时触发（随机触发吃或玩 → 提升快乐度）
    void OnConversationEnd();

    // 检查是否需要洗澡提醒
    bool NeedsBathingReminder() const { return stats_.NeedsBathing(); }

    // 与小智语音系统联动
    void OnDeviceStateChanged(DeviceState old_state, DeviceState new_state);

    // 获取当前应该播放的动画名
    const char* GetCurrentAnimation() const;

    // 获取状态
    const PetStats& GetStats() const { return stats_; }
    PetAction GetAction() const { return current_action_; }

    // 获取心情描述（供MCP使用）
    const char* GetMoodDescription() const;

    // 是否在语音交互中（供环境对白系统检查）
    bool IsInVoiceInteraction() const { return in_voice_interaction_; }

    // 是否正在持续恢复（吃饭/洗澡），用于判断对话结束后是否应进入聆听
    bool IsInContinuousRecovery() const { return continuous_recovery_action_ != PetAction::kIdle; }

    // 对话质量追踪（供MCP tool和application调用）
    void OnSessionMessage() { session_msg_count_++; }
    void OnSessionStatusChecked() { session_checked_status_ = true; }
    void OnSessionCareAction() { session_did_care_ = true; }

    // 动作名转字符串
    static const char* ActionToString(PetAction action);

    // 动画映射
    static const char* ActionToAnimation(PetAction action);

    // 状态变化回调
    using ActionCallback = std::function<void(PetAction, const char*)>;
    void SetActionCallback(ActionCallback cb) { action_callback_ = cb; }

    // 移动控制回调 (direction, distance) -> success
    // 板级代码注册此回调来处理实际的移动动画
    using MoveCallback = std::function<bool(MoveDirection direction, int16_t distance)>;
    void SetMoveCallback(MoveCallback cb) { move_callback_ = cb; }

    // 触发移动（通过MCP调用）
    // 返回: true=移动成功, false=移动失败（无回调或条件不满足）
    bool Move(MoveDirection direction, int16_t distance = 30);

    // 位置跟踪（由board代码更新）
    void SetPosition(int16_t x, int16_t y) { position_x_ = x; position_y_ = y; }
    int16_t GetPositionX() const { return position_x_; }
    int16_t GetPositionY() const { return position_y_; }

private:
    PetStateMachine() = default;

    PetStats stats_;
    PetAction current_action_ = PetAction::kIdle;
    PetAction previous_pet_action_ = PetAction::kIdle;  // 交互前的宠物动作
    DecayConfig decay_config_;
    ActionCallback action_callback_;
    MoveCallback move_callback_;

    // 位置跟踪（相对于屏幕中心的偏移）
    int16_t position_x_ = 0;
    int16_t position_y_ = 0;

    uint32_t action_start_time_ = 0;  // 动作开始时间(ms)
    uint32_t action_duration_ = 0;    // 动作持续时间(ms)
    bool in_voice_interaction_ = false; // 是否在语音交互中
    bool voice_animation_locked_ = false; // 对话中触发吃饭/洗澡时锁定动画，防止语音状态覆盖

    // 对话质量追踪（用于动态奖励计算）
    uint16_t session_msg_count_ = 0;        // 本次对话消息数
    bool session_checked_status_ = false;   // 本次对话是否查看了状态
    bool session_did_care_ = false;         // 本次对话是否进行了照顾（喂食/洗澡）

    // 持续性恢复跟踪（独立于动画显示）
    PetAction continuous_recovery_action_ = PetAction::kIdle;  // 正在进行的持续恢复动作(kEating/kBathing)
    uint32_t continuous_recovery_start_ = 0;   // 恢复开始时间(ms)
    uint32_t continuous_recovery_duration_ = 300000;  // 恢复持续时间(5分钟)

    // 金币生成计时器
    uint32_t happy_coin_timer_ = 0;   // 金币生成计时器（分钟），触发频率根据属性动态调整
    // 注意: coin_blocked_timer 已移至 PetStats 中持久化 (stats_.coin_blocked_minutes)

    // 属性衰减计时器（每3分钟才衰减一次）
    uint8_t decay_tick_counter_ = 0;  // 计数器：0,1,2循环

    void SetAction(PetAction action, uint32_t duration_ms = 0);
    void UpdateActionTimer();  // 检查定时动作是否结束
    void Save();  // 保存到NVS
    void Load();  // 从NVS加载

    // 提取的公共方法
    void StartContinuousRecovery(PetAction action, DialogueEvent start_event);  // 开始持续恢复（吃饭/洗澡）
    void RestoreIdleAction();  // 恢复到空闲/聆听状态

    // 限制值在0-100范围
    static int8_t Clamp(int value) {
        if (value < 0) return 0;
        if (value > 100) return 100;
        return static_cast<int8_t>(value);
    }
};

#endif // PET_STATE_H

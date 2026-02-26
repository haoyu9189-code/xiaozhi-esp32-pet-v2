#ifndef AMBIENT_DIALOGUE_H
#define AMBIENT_DIALOGUE_H

#include <stdint.h>
#include "pet_state.h"

// 环境对白事件类型
enum class DialogueEvent : uint8_t {
    // 事件反应
    kCoinAppear,        // 金币出现
    kCoinPickup,        // 捡到金币
    kPoopAppear,        // 便便生成
    kPoopStep,          // 踩到便便
    kStartEating,       // 开始吃饭
    kFullEating,        // 吃饱了
    kStartBathing,      // 开始洗澡
    kFullBathing,       // 洗完澡

    // 时间问候（自动触发）
    kTimeGreeting,      // 时间问候（早中晚夜）

    // 心情自言自语（自动触发）
    kMoodMumble,        // 根据属性值自言自语

    // 节日祝福（自动触发）
    kFestivalGreeting,  // 节日祝福
};

// 环境对白管理器
class AmbientDialogue {
public:
    static AmbientDialogue& GetInstance();

    // 初始化
    void Initialize();

    // 每分钟调用一次（用于定时触发时间问候、心情自言自语等）
    void Tick();

    // 触发事件对白（外部调用）
    // event: 事件类型
    // force: 是否强制触发（跳过随机概率检查）
    void TriggerEvent(DialogueEvent event, bool force = false);

private:
    AmbientDialogue() = default;
    ~AmbientDialogue() = default;
    AmbientDialogue(const AmbientDialogue&) = delete;
    AmbientDialogue& operator=(const AmbientDialogue&) = delete;

    // 显示对白
    void ShowDialogue(const char* text);

    // 计算显示时长（ms）
    int GetDisplayDuration(const char* text);

    // 随机触发检查（返回是否触发）
    bool ShouldTrigger(DialogueEvent event);

    // 冷却检查（返回是否在冷却中）
    bool IsInCooldown(DialogueEvent event);

    // 更新冷却时间
    void UpdateCooldown(DialogueEvent event);

    // 时间检测
    void CheckTimeGreeting();

    // 心情检测
    void CheckMoodMumble();

    // 节日检测
    void CheckFestivalGreeting();

    // 获取时间段（0=夜晚, 1=早上, 2=下午, 3=傍晚）
    int GetTimePeriod();

    // 检查是否是特定节日（返回节日ID，-1表示不是节日）
    int CheckFestival();

    // 冷却时间记录（分钟）
    uint32_t last_trigger_time_[20];  // 每种事件的最后触发时间

    // 上次时间问候的小时
    uint8_t last_greeting_hour_ = 0xFF;

    // 上次节日祝福的日期
    uint16_t last_festival_day_ = 0;

    // Tick计数器（用于随机触发）
    uint32_t tick_counter_ = 0;
};

#endif  // AMBIENT_DIALOGUE_H

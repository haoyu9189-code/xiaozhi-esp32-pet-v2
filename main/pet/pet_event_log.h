#ifndef PET_EVENT_LOG_H
#define PET_EVENT_LOG_H

#include <cstdint>
#include <string>

// 事件类型
enum class PetEventType : uint8_t {
    kAmbientDialogue,   // 环境自言自语
    kCoinSpawned,       // 金币出现
    kCoinPickup,        // 捡到金币
    kPoopSpawned,       // 便便生成
    kPoopStep,          // 踩到便便
    kStartEating,       // 开始吃饭
    kFullEating,        // 吃饱了
    kStartBathing,      // 开始洗澡
    kFullBathing,       // 洗完澡
    kAutoFeed,          // 自动喂食
    kAutoBathe,         // 自动洗澡
};

// 单条事件记录 (~56 bytes)
struct PetEvent {
    PetEventType type;
    int64_t timestamp_ms;       // esp_timer时间戳（毫秒），使用int64避免49天溢出
    char description[48];       // 事件描述文本

    PetEvent() : type(PetEventType::kAmbientDialogue), timestamp_ms(0) {
        description[0] = '\0';
    }
};

// 事件日志环形缓冲区（16条，~832 bytes RAM）
class PetEventLog {
public:
    static PetEventLog& GetInstance();

    // 记录事件
    void Log(PetEventType type, const char* description);

    // 获取最近事件的文本描述（用于注入系统提示词）
    // max_events: 最多返回多少条
    std::string GetRecentEventsText(int max_events = 5) const;

    // 获取最近事件的JSON格式（用于pet(status)返回）
    std::string GetRecentEventsJson(int max_events = 5) const;

    // 获取事件数量
    int GetCount() const { return count_; }

private:
    PetEventLog() = default;

    static constexpr int kMaxEvents = 16;
    PetEvent events_[kMaxEvents];
    int head_ = 0;      // 下一个写入位置
    int count_ = 0;     // 当前事件数量

    // 获取事件类型的中文名称
    static const char* EventTypeName(PetEventType type);

    // 计算距今的分钟数
    static int MinutesAgo(int64_t timestamp_ms);
};

#endif // PET_EVENT_LOG_H

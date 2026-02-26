#ifndef PET_COIN_H
#define PET_COIN_H

#include <cstdint>
#include <functional>

// 金币系统状态 - 持久化到NVS
struct CoinState {
    uint8_t coins;              // 当前金币 (0-99)
    uint32_t daily_chat_count;  // 今日聊天计数
    uint16_t last_reset_day;    // 上次重置的日期 (day of year, 1-366)
    uint16_t last_reset_year;   // 上次重置的年份
    uint32_t total_coins_spent; // 累计消费的金币总数（用于亲密度系统）

    CoinState() : coins(0), daily_chat_count(0), last_reset_day(0), last_reset_year(0), total_coins_spent(0) {}
};

// 聊天奖励里程碑
constexpr uint32_t CHAT_MILESTONE_1 = 1;   // 第1句 +2金币
constexpr uint32_t CHAT_MILESTONE_2 = 5;   // 第5句 +2金币
constexpr uint32_t CHAT_MILESTONE_3 = 6;   // 第6句 +2金币
// 之后每10句 +1金币

// 金币消费
constexpr uint8_t COST_FEED = 1;   // 吃饭消耗1金币
constexpr uint8_t COST_BATHE = 1;  // 洗澡消耗1金币
constexpr uint8_t COST_BACKGROUND = 10;  // 购买背景消耗10金币

// 金币限制
constexpr uint8_t MAX_COINS = 99;

class CoinSystem {
public:
    static CoinSystem& GetInstance();

    // 初始化（从NVS加载）
    void Initialize();

    // 金币管理
    uint8_t GetCoins() const { return state_.coins; }
    bool SpendCoins(uint8_t amount);  // 返回false表示金币不足
    void AddCoins(uint8_t amount);

    // 聊天奖励系统
    void OnChatMessage();     // 每次聊天结束时调用
    void CheckDailyReset();   // 检查是否需要重置每日计数

    // 自动消费检查（每分钟调用）
    void CheckAutoConsumption();

    // 背景奖励（通过MCP工具由AI触发，保留接口兼容）
    bool IsRewardPlaying() const { return false; }
    void CheckRewardTimer();

    // 获取状态（用于显示和MCP）
    const CoinState& GetState() const { return state_; }
    uint32_t GetDailyChatCount() const { return state_.daily_chat_count; }
    uint32_t GetTotalCoinsSpent() const { return state_.total_coins_spent; }

    // 金币变化回调
    using CoinCallback = std::function<void(uint8_t coins, const char* reason)>;
    void SetCoinCallback(CoinCallback cb) { coin_callback_ = cb; }

private:
    CoinSystem() = default;

    CoinState state_;
    CoinCallback coin_callback_;

    void Save();
    void Load();
};

#endif // PET_COIN_H

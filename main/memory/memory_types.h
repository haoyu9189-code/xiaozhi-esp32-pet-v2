#ifndef MEMORY_TYPES_H
#define MEMORY_TYPES_H

#include <cstdint>
#include <cstring>

// Magic bytes for data validation
#define MEMORY_MAGIC_PROFILE        "XZPF"
#define MEMORY_MAGIC_FAMILY         "XZFM"
#define MEMORY_MAGIC_PREFERENCES    "XZPR"
#define MEMORY_MAGIC_EVENTS         "XZEV"
#define MEMORY_MAGIC_FACTS          "XZFC"
#define MEMORY_MAGIC_TRAITS         "XZTR"
#define MEMORY_MAGIC_HABITS         "XZHB"
#define MEMORY_MAGIC_MOMENTS        "XZMT"
#define MEMORY_MAGIC_GOALS          "XZGL"
#define MEMORY_MAGIC_CHAT           "XZCH"
#define MEMORY_MAGIC_AFFECTION      "XZAF"

// Storage limits (optimized for 48KB NVS)
#define MAX_FAMILY_MEMBERS  8
#define MAX_LIKES           8
#define MAX_DISLIKES        8
#define MAX_EVENTS          16
#define MAX_FACTS           20
#define MAX_TRAITS          10
#define MAX_HABITS          10
#define MAX_MOMENTS         10
#define MAX_GOALS           5
#define MAX_CHAT_MESSAGES   30

// Operation result
enum class AUDNAction {
    ADDED,
    UPDATED,
    DELETED,
    NOOP
};

// Emotion type
enum class EmotionType : uint8_t {
    NEUTRAL = 0,
    HAPPY = 1,
    SAD = 2,
    EXCITED = 3,
    WORRIED = 4,
    TOUCHED = 5
};

// Significance level
enum class SignificanceLevel : uint8_t {
    LOW = 1,
    MEDIUM = 2,
    HIGH = 3,
    VERY_HIGH = 4,
    CRITICAL = 5
};

// Goal category
enum class GoalCategory : uint8_t {
    LEARNING = 0,
    HEALTH = 1,
    CAREER = 2,
    HOBBY = 3,
    OTHER = 4
};

// Goal status
enum class GoalStatus : uint8_t {
    ACTIVE = 0,
    COMPLETED = 1,
    PAUSED = 2,
    ABANDONED = 3
};

// Relationship stage (based on total coins spent)
enum class RelationshipStage : uint8_t {
    STRANGER = 0,       // 0-20 coins spent
    ACQUAINTANCE = 1,   // 21-50 coins spent
    FRIEND = 2,         // 51-100 coins spent
    CLOSE_FRIEND = 3,   // 101-200 coins spent
    SOULMATE = 4        // 201+ coins spent
};

// Emotional context
struct EmotionalContext {
    uint8_t type;       // EmotionType
    uint8_t intensity;  // 1-5
};

// User profile (~79 bytes)
struct UserProfile {
    char magic[4];      // XZPF
    char name[32];
    char birthday[6];   // MM-DD
    uint8_t age;
    char gender[8];
    char location[32];
};

// Family member (~130 bytes)
struct FamilyMember {
    char relation[16];
    char name[32];
    char member_type[16];
    uint8_t closeness;      // 1-5
    char shared_memory[64];
    uint8_t reserved;
};

// Preferences (~530 bytes)
struct Preferences {
    char magic[4];      // XZPR
    char likes[MAX_LIKES][32];
    char dislikes[MAX_DISLIKES][32];
    uint8_t likes_count;
    uint8_t dislikes_count;
};

// Event (~106 bytes, 扩展后支持重复日程)
struct Event {
    char date[11];           // YYYY-MM-DD or MM-DD
    char time[6];            // HH:MM (用于日程的时间部分，普通事件为空)
    char event_type[16];
    char content[64];
    uint8_t reminded;
    EmotionalContext emotion;
    uint8_t significance;
    uint8_t flags;           // 位字段：bit0=is_schedule, bit1=completed, bit2-4=repeat_type
    uint8_t repeat_interval; // 重复间隔（如每N天/周/月，目前仅支持1）

    Event() {
        memset(this, 0, sizeof(Event));
    }
};

// Event 辅助宏和函数
#define EVENT_FLAG_IS_SCHEDULE  0x01
#define EVENT_FLAG_COMPLETED    0x02

inline bool IsSchedule(const Event& e) { return (e.flags & EVENT_FLAG_IS_SCHEDULE) != 0; }
inline bool IsCompleted(const Event& e) { return (e.flags & EVENT_FLAG_COMPLETED) != 0; }
inline void SetSchedule(Event& e, bool val) {
    if (val) e.flags |= EVENT_FLAG_IS_SCHEDULE;
    else e.flags &= ~EVENT_FLAG_IS_SCHEDULE;
}
inline void SetCompleted(Event& e, bool val) {
    if (val) e.flags |= EVENT_FLAG_COMPLETED;
    else e.flags &= ~EVENT_FLAG_COMPLETED;
}

// 重复类型标志 (bits 2-4)
#define REPEAT_TYPE_MASK     0x1C  // bits 2-4 (0001 1100)
#define REPEAT_NONE          0x00
#define REPEAT_DAILY         0x04  // bit2 (0000 0100)
#define REPEAT_WEEKLY        0x08  // bit3 (0000 1000)
#define REPEAT_MONTHLY       0x0C  // bit2+3 (0000 1100)

// 重复类型辅助函数
inline uint8_t GetRepeatType(const Event& e) {
    return e.flags & REPEAT_TYPE_MASK;
}
inline void SetRepeatType(Event& e, uint8_t type) {
    e.flags = (e.flags & ~REPEAT_TYPE_MASK) | (type & REPEAT_TYPE_MASK);
}
inline bool IsRepeating(const Event& e) {
    return GetRepeatType(e) != REPEAT_NONE;
}

// Fact (~132 bytes)
struct Fact {
    uint32_t timestamp;
    char content[128];
};

// Trait (~64 bytes)
struct Trait {
    char category[16];  // personality/appearance/ability/other
    char content[48];
};

// Habit (~64 bytes)
struct Habit {
    char content[48];
    char frequency[16]; // daily/weekly/occasionally
};

// Special moment (~168 bytes)
struct SpecialMoment {
    uint32_t timestamp;
    char topic[32];
    char content[128];
    EmotionalContext emotion;
    uint8_t importance; // 1-5
};

// Personal goal (~80 bytes, optimized)
struct PersonalGoal {
    char content[64];
    uint32_t created;
    uint32_t updated;
    uint8_t category;   // GoalCategory
    uint8_t status;     // GoalStatus
    uint8_t progress;   // 0-100
    uint8_t priority;   // 1-5
};

// Chat message (~100 bytes)
struct ChatMessage {
    uint32_t timestamp;
    uint8_t role;       // 0=user, 1=assistant
    char content[92];
    uint8_t reserved[3];
};

// Chat log metadata
struct ChatLogMeta {
    char magic[4];           // XZCH
    uint32_t total_count;
    uint32_t oldest_index;
    uint32_t newest_index;
    uint32_t last_save_time;
};

// Affection event type (unique values for each event)
enum class AffectionEvent : int8_t {
    // Positive (ascending order by impact)
    DAILY_FIRST = 1,
    CHAT_COMPLETE = 2,
    STREAK_BONUS = 3,
    LONG_CHAT = 4,
    REMEMBERED_INFO = 5,
    SHARE_FEELING = 6,
    COMFORTED = 7,
    ANNIVERSARY = 8,
    BIRTHDAY_WISH = 10,

    // Negative
    FORGOT_INFO = -2,
    LONG_ABSENCE = -3
};

// Achievement flags
enum Achievement : uint16_t {
    ACH_FIRST_CHAT = 1 << 0,
    ACH_WEEK_STREAK = 1 << 1,
    ACH_MONTH_STREAK = 1 << 2,
    ACH_100_CHATS = 1 << 3,
    ACH_SHARE_SECRET = 1 << 4,
    ACH_FIRST_COMFORT = 1 << 5,
    ACH_ANNIVERSARY_1 = 1 << 6,
    ACH_MAX_AFFECTION = 1 << 7,
    ACH_KNOW_FAMILY = 1 << 8,
    ACH_KNOW_HOBBY = 1 << 9
};

// Affection statistics (~48 bytes)
struct AffectionStats {
    char magic[4];          // XZAF
    uint8_t affection;      // 0-100
    uint8_t max_affection;
    int8_t mood;            // -10~+10
    uint8_t reserved1;

    uint32_t first_meet_date;
    uint32_t last_chat_date;
    uint16_t streak_days;
    uint16_t total_days;

    uint32_t total_conversations;
    uint32_t total_messages;
    uint32_t total_chat_seconds;

    uint8_t happy_moments;
    uint8_t sad_moments;
    uint8_t comforted_times;
    uint8_t shared_secrets;

    uint16_t achievements;
    uint8_t stage;          // RelationshipStage
    uint8_t reserved2;
};

// Special event for personality evolver
struct SpecialEventInfo {
    bool has_event;
    char event_type[16];
    char message[64];
};

// Extracted memory type
enum class ExtractedType {
    NONE = 0,
    IDENTITY,       // name, age, gender, location
    PREFERENCE,     // like/dislike
    FAMILY,         // family member
    EVENT,          // event/plan
    FACT            // general fact
};

// Extracted memory structure
struct ExtractedMemory {
    ExtractedType type;
    char category[16];
    char content[64];
    uint8_t confidence;  // 1-5
};

#endif // MEMORY_TYPES_H

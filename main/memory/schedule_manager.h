#ifndef SCHEDULE_MANAGER_H
#define SCHEDULE_MANAGER_H

#include <string>
#include <vector>
#include <mutex>
#include <functional>
#include <nvs_flash.h>

// Maximum schedules
#define MAX_SCHEDULES 20

// Schedule item structure (~128 bytes)
struct ScheduleItem {
    char magic[4];              // "XZSC"
    uint32_t id;                // Unique ID
    uint32_t trigger_time;      // Unix timestamp to trigger
    char content[64];           // Reminder content
    char repeat_type[16];       // none, daily, weekly, monthly
    uint8_t triggered;          // Already triggered flag
    uint8_t enabled;            // Enabled flag
    uint8_t reserved[2];        // Padding
};

// Callback type for reminder trigger
using ReminderCallback = std::function<void(const ScheduleItem& item)>;

class ScheduleManager {
public:
    static ScheduleManager& GetInstance();

    // Initialize and load from NVS
    bool Init();

    // Add a new schedule
    // time_str format: "HH:MM" (today) or "YYYY-MM-DD HH:MM" or "MM-DD HH:MM"
    // repeat: "none", "daily", "weekly", "monthly"
    // Returns schedule ID, or 0 on failure
    uint32_t AddSchedule(const char* time_str, const char* content,
                         const char* repeat = "none");

    // Remove a schedule by ID
    bool RemoveSchedule(uint32_t id);

    // List all schedules
    int GetSchedules(ScheduleItem* items, int max_count);

    // Get upcoming schedules (within N hours)
    int GetUpcoming(ScheduleItem* items, int max_count, int hours = 24);

    // Check and trigger due reminders
    // Call this periodically (e.g., every minute)
    void CheckAndTrigger();

    // Set reminder callback
    void SetReminderCallback(ReminderCallback callback);

    // Force save to NVS
    void Save();

    // Get schedule count
    int GetCount() const { return schedules_.size(); }

private:
    ScheduleManager() = default;
    ~ScheduleManager();

    std::vector<ScheduleItem> schedules_;
    nvs_handle_t nvs_handle_ = 0;
    std::mutex mutex_;
    bool dirty_ = false;
    bool initialized_ = false;
    uint32_t next_id_ = 1;
    ReminderCallback reminder_callback_;

    // Parse time string to unix timestamp
    uint32_t ParseTimeString(const char* time_str);

    // Calculate next trigger time for repeating schedules
    uint32_t CalculateNextTrigger(const ScheduleItem& item);

    // NVS operations
    void LoadFromNvs();
    void SaveToNvs();
};

#endif // SCHEDULE_MANAGER_H

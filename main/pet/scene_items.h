#ifndef _SCENE_ITEMS_H_
#define _SCENE_ITEMS_H_

#include <stdint.h>
#include <stdbool.h>

// Item type definitions (match item_loader.h)
#define SCENE_ITEM_COIN  0
#define SCENE_ITEM_POOP  1

// Scene limits
#define MAX_SCENE_COINS 10  // 保底机制允许最多10个金币
#define MAX_SCENE_POOPS 3

// Spawn area (same as pet random walk area)
#define ITEM_SPAWN_MAX_X 60   // ±60 pixels from center
#define ITEM_SPAWN_MAX_Y 15   // ±15 pixels from center

// Collision distances (检测范围)
#define COIN_PICKUP_DISTANCE 30   // Pet picks up coin within 30px (金币捡取范围)
#define POOP_STEP_DISTANCE   35   // Pet steps on poop within 35px (便便踩踏范围，优化后更容易躲开)

// Poop mechanics
#define POOP_MAX_STEP_COUNT 3     // Poop becomes inactive after 3 steps
#define POOP_HUNGER_THRESHOLD 0  // Poop spawns when hunger > 0 (只要不是空腹就会产生)
#define POOP_MAX_DAILY_SPAWNS 12  // Max 12 poops per day (平均每2小时一次，全天可产生)
#define POOP_STEP_COOLDOWN_MS 10000  // 10 seconds cooldown between steps on same poop

// Coin mechanics
#define COIN_REWARD_MIN 1         // Minimum coins per pickup
#define COIN_REWARD_MAX 3         // Maximum coins per pickup
#define COIN_UNLOCK_CHANCE 1      // 1% chance to unlock background

// Scene item structure
struct SceneItem {
    int16_t x;              // Position X (relative to center)
    int16_t y;              // Position Y (relative to center)
    uint8_t type;           // SCENE_ITEM_COIN or SCENE_ITEM_POOP
    uint8_t step_count;     // For poop: times stepped on (0-3)
    bool active;            // Is this item slot active?
    uint32_t last_step_time; // For poop: last step timestamp (ms), used for cooldown

    SceneItem() : x(0), y(0), type(0), step_count(0), active(false), last_step_time(0) {}
    SceneItem(int16_t x_, int16_t y_, uint8_t type_, uint8_t step_count_, bool active_)
        : x(x_), y(y_), type(type_), step_count(step_count_), active(active_), last_step_time(0) {}
};

// Persistent state for scene items
struct SceneItemState {
    SceneItem coins[MAX_SCENE_COINS];
    SceneItem poops[MAX_SCENE_POOPS];
    uint8_t coin_count;             // Number of active coins
    uint8_t poop_count;             // Number of active poops
    uint32_t last_coin_spawn_hour;  // Hour of last coin spawn (0-23)
    uint16_t last_coin_spawn_day;   // Day of year of last coin spawn
    uint16_t last_poop_spawn_day;   // Day of year of last poop check
    uint8_t daily_poop_spawns;      // Poops spawned today
    int64_t next_poop_spawn_time;   // Next poop spawn time (ms since boot)

    SceneItemState();
};

// Scene item manager - handles spawning, collision, rendering data
class SceneItemManager {
public:
    static SceneItemManager& GetInstance();

    // Initialize from NVS
    void Initialize();

    // Called every second to check spawning
    void Tick();

    // Poop system
    void CheckPoopSpawn(int hunger);
    void ClearAllPoops();  // Called when bathing

    // Coin system
    void CheckCoinSpawn();
    void SpawnCoin();  // Manually spawn a coin at random position

    // Collision detection (called when pet movement ends)
    // pet_x, pet_y: pet offset from center
    void CheckCollision(int16_t pet_x, int16_t pet_y);

    // Get items for rendering
    const SceneItem* GetCoins(uint8_t* out_count) const;
    const SceneItem* GetPoops(uint8_t* out_count) const;

    // Get counts
    uint8_t GetCoinCount() const { return state_.coin_count; }
    uint8_t GetPoopCount() const { return state_.poop_count; }

    // Check if there are any poops (used to determine if coins can spawn)
    bool HasPoops() const { return state_.poop_count > 0; }

    // Debug: force spawn test items
    void DebugSpawnItems();

    // Force immediate save (called before shutdown)
    void ForceSave();

private:
    SceneItemManager();
    ~SceneItemManager() = default;
    SceneItemManager(const SceneItemManager&) = delete;
    SceneItemManager& operator=(const SceneItemManager&) = delete;

    // Spawning
    void SpawnPoop();
    void SpawnCoinAt(int16_t x, int16_t y);
    bool SpawnCoinInternal(int16_t x, int16_t y, bool log_position);
    void ScheduleNextPoopSpawn();

    // Event handlers
    void OnCoinPickup(int index);
    void OnPoopStep(int index);

    // Generate random position within spawn area
    void GetRandomPosition(int16_t* x, int16_t* y);

    // Calculate distance between two points
    int16_t GetDistance(int16_t x1, int16_t y1, int16_t x2, int16_t y2);

    // NVS persistence
    void Save();
    void Load();
    void CheckDailyReset();

    // Lucky unlock
    void TryUnlockRandomBackground();

    // Delayed save support
    void MarkDirty();           // Mark state as needing save
    void SaveIfNeeded();        // Check and perform delayed save

    SceneItemState state_;
    bool initialized_;
    bool state_dirty_;          // Whether state needs to be saved
    uint32_t last_save_time_;   // Last save timestamp (ms)
    static constexpr uint32_t SAVE_INTERVAL_MS = 30000;  // 30 second save interval
};

#endif // _SCENE_ITEMS_H_

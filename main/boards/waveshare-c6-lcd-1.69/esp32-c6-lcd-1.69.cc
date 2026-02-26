#include "wifi_board.h"
#include "codecs/es8311_audio_codec.h"
#include "display/lcd_display.h"
#include "system_reset.h"
#include "application.h"
#include "assets/lang_config.h"
#include "button.h"
#include "config.h"
#include <esp_log.h>
#include "i2c_device.h"
#include <driver/i2c_master.h>
#include <ssid_manager.h>
#include "pet/pet_status_display.h"
#include "pet/pet_state.h"
#include <esp_partition.h>
#include <esp_flash.h>
#include <esp_random.h>
#include <esp_task_wdt.h>

// ============================================================================
// WiFi配置 - 从Flash数据分区读取 (地址: 0x7F0000)
// 格式: Magic(4) + Version(2) + SSID_len(1) + SSID(32) + PWD_len(1) + PWD(64)
// ============================================================================
#define WIFI_CONFIG_FLASH_ADDR  0x7F0000
#define WIFI_CONFIG_MAGIC       "WIFI"

struct WifiConfigBin {
    char magic[4];          // "WIFI"
    uint16_t version;       // 1
    uint8_t ssid_len;       // SSID长度
    char ssid[32];          // SSID (null-padded)
    uint8_t pwd_len;        // 密码长度
    char password[64];      // 密码 (null-padded)
} __attribute__((packed));
// ============================================================================
#include <driver/ledc.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_timer.h>
#include <esp_lvgl_port.h>
#include <esp_lcd_touch_cst816s.h>
#include "iot_button.h"
#include "power_manager.h"
#include "power_save_timer.h"
// Silent mode removed
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstring>
#include <math.h>
#include <esp_sleep.h>
#include <time.h>

// Animation loader (headerless per-frame palette format, 160x160)
#include "animation_loader.h"
// Background loader (FS format with header, 280x240)
#include "background_loader.h"
#include "background_manager.h"
// Item loader (40x40 items: coin, poop)
#include "item_loader.h"
// Scene items manager (spawning, collision detection)
#include "pet/scene_items.h"
// Ambient dialogue system (cute pet dialogues)
#include "pet/ambient_dialogue.h"

#define TAG "waveshare_lcd_1_69"

// Animation configuration
#define ANIM_FRAME_INTERVAL_MS  167     // ~6fps (12 frames in 2 seconds)
#define ANIM_SLEEP_TIMEOUT_DAY_MS   (10 * 60 * 1000)  // 10 minutes for daytime (8:00-19:00)
#define ANIM_SLEEP_TIMEOUT_NIGHT_MS (5 * 60 * 1000)   // 5 minutes for nighttime
#define ANIM_SLEEP_DURATION_DAY_MS  (10 * 60 * 1000)  // Sleep 10 minutes during daytime
#define ANIM_SLEEP_DURATION_NIGHT_MS (30 * 60 * 1000) // Sleep 30 minutes during nighttime
#define ANIM_TOUCH_DURATION_MS  3000    // Touch animation duration (3 seconds)

// Random walk configuration (pet wanders around screen)
#define RANDOM_WALK_MIN_INTERVAL_MS  (5 * 1000)   // Minimum 5 seconds between walks
#define RANDOM_WALK_MAX_INTERVAL_MS  (10 * 1000)  // Maximum 10 seconds between walks
#define RANDOM_WALK_DURATION_MS      2000         // Walk animation duration (2 seconds)
#define RANDOM_WALK_MAX_OFFSET_X     60           // Max X offset (touches screen edges)
#define RANDOM_WALK_MAX_OFFSET_Y     15           // Max Y offset (stays within UI bars)

// Random action configuration (pet does random animations)
#define RANDOM_ACTION_MIN_INTERVAL_MS  (5 * 1000)   // Minimum 5 seconds between actions
#define RANDOM_ACTION_MAX_INTERVAL_MS  (10 * 1000)  // Maximum 10 seconds between actions
#define RANDOM_ACTION_DURATION_MS      3000         // Action animation duration (3 seconds)

// Pet behavior state machine - unified management of random walk/action
// Replaces separate random_walk_active and random_action_active flags
// to prevent race conditions where both could trigger in the same frame
enum class PetBehaviorState : uint8_t {
    IDLE = 0,       // Idle, can start new behavior
    WALKING,        // Random walk in progress
    ACTION,         // Random action in progress (pet_head/talk/listen)
    COOLDOWN,       // Cooldown period (prevents immediate re-trigger)
    INTERRUPTED,    // Interrupted by external event (listening/speaking)
};

// Random action animations
static const char* RANDOM_ACTIONS[] = {"pet_head", "talk", "listen"};
static const int RANDOM_ACTION_COUNT = 3;

// Composite buffer dimensions (full screen size)
#define COMPOSITE_WIDTH   280
#define COMPOSITE_HEIGHT  240

// Animation scaling configuration (100% = no scaling)
// Source animation: 160x160, no scaling to save memory/CPU
#define ANIM_SCALE_PERCENT  100  // Scale percentage (100 = no scaling)
#define ANIM_SCALED_WIDTH   ((ANIM_FRAME_WIDTH * ANIM_SCALE_PERCENT) / 100)   // 160
#define ANIM_SCALED_HEIGHT  ((ANIM_FRAME_HEIGHT * ANIM_SCALE_PERCENT) / 100)  // 160

// Animation position within composite buffer (centered after scaling)
#define ANIM_OFFSET_IN_COMPOSITE_X  ((COMPOSITE_WIDTH - ANIM_SCALED_WIDTH) / 2)    // 60
#define ANIM_OFFSET_IN_COMPOSITE_Y  ((COMPOSITE_HEIGHT - ANIM_SCALED_HEIGHT) / 2)  // 40

// Composite buffer position on screen (full screen, start from top)
#define COMPOSITE_SCREEN_Y  0

// UI areas to skip during direct LCD output (to preserve LVGL-drawn UI)
#define TOP_UI_HEIGHT     25   // Height of top bar (status icons, matches bg_offset_y)
#define BOTTOM_UI_HEIGHT  25   // Height of bottom bar (subtitles)

// Background color range for transparency (from all frames in index.json + 20% margin)
// All frames bg_color_rgb range: R(1-7), G(173-206), B(104-138)
// Expand 20%: R(0-9), G(166-213), B(97-145)
// Convert to RGB565:
#define BG_R_MIN  0    // 0 >> 3 = 0
#define BG_R_MAX  3    // 9 >> 3 = 1
#define BG_G_MIN  36   // 166 >> 2 = 41
#define BG_G_MAX  46   // 213 >> 2 = 53
#define BG_B_MIN  10   // 97 >> 3 = 12
#define BG_B_MAX  14   // 145 >> 3 = 18

// Helper: Check if RGB565 pixel is within background color range
static inline bool is_background_color(uint16_t pixel) {
    uint8_t r = (pixel >> 11) & 0x1F;  // 5-bit red
    uint8_t g = (pixel >> 5) & 0x3F;   // 6-bit green
    uint8_t b = pixel & 0x1F;          // 5-bit blue
    // Note: BG_R_MIN is 0, so r >= BG_R_MIN is always true for uint8_t
    return (r <= BG_R_MAX &&
            g >= BG_G_MIN && g <= BG_G_MAX &&
            b >= BG_B_MIN && b <= BG_B_MAX);
}

// Item position center in composite buffer (near bottom of animation area)
// Animation bottom Y = 40 + 160 = 200, items at Y=180 appear at pet's feet level
#define ITEM_CENTER_X  (COMPOSITE_WIDTH / 2)   // 140
#define ITEM_CENTER_Y  180                     // Near ground level

// Cached item bounds - pre-computed once per frame to avoid 53K function calls
// Each item stores screen-space bounding box for fast inline rejection
struct CachedItemBounds {
    int16_t x1, y1, x2, y2;  // Screen coordinates (x1,y1) to (x2,y2) exclusive
    uint8_t item_type;       // ITEM_TYPE_COIN or ITEM_TYPE_POOP
    bool active;
};

// Maximum cached items = MAX_SCENE_COINS + MAX_SCENE_POOPS = 8
#define MAX_CACHED_ITEMS 8
static CachedItemBounds cached_items[MAX_CACHED_ITEMS];
static uint8_t cached_item_count = 0;
static int16_t cached_items_min_y = COMPOSITE_HEIGHT;  // Quick rejection for rows above all items
static int16_t cached_items_max_y = 0;                  // Quick rejection for rows below all items

// Prepare item bounds cache - call once per frame before rendering
// Returns true if there are any active items to render
static bool prepare_item_bounds_cache() {
    auto& scene_mgr = SceneItemManager::GetInstance();
    uint8_t coin_count = scene_mgr.GetCoinCount();
    uint8_t poop_count = scene_mgr.GetPoopCount();

    cached_item_count = 0;
    cached_items_min_y = COMPOSITE_HEIGHT;
    cached_items_max_y = 0;

    if (coin_count == 0 && poop_count == 0) {
        return false;
    }

    auto& item_loader = ItemLoader::GetInstance();
    if (!item_loader.IsInitialized()) {
        static bool warned = false;
        if (!warned) {
            ESP_LOGW(TAG, "ItemLoader not initialized, coins=%d poops=%d", coin_count, poop_count);
            warned = true;
        }
        return false;
    }

    const SceneItem* coins = scene_mgr.GetCoins(nullptr);
    const SceneItem* poops = scene_mgr.GetPoops(nullptr);

    // Cache poop bounds
    for (uint8_t i = 0; i < MAX_SCENE_POOPS && cached_item_count < MAX_CACHED_ITEMS; i++) {
        if (!poops[i].active) continue;
        auto& c = cached_items[cached_item_count];
        c.x1 = ITEM_CENTER_X + poops[i].x - ITEM_WIDTH / 2;
        c.y1 = ITEM_CENTER_Y + poops[i].y - ITEM_HEIGHT / 2;
        c.x2 = c.x1 + ITEM_WIDTH;
        c.y2 = c.y1 + ITEM_HEIGHT;
        c.item_type = ITEM_TYPE_POOP;
        c.active = true;
        if (c.y1 < cached_items_min_y) cached_items_min_y = c.y1;
        if (c.y2 > cached_items_max_y) cached_items_max_y = c.y2;
        cached_item_count++;
    }

    // Cache coin bounds
    for (uint8_t i = 0; i < MAX_SCENE_COINS && cached_item_count < MAX_CACHED_ITEMS; i++) {
        if (!coins[i].active) continue;
        auto& c = cached_items[cached_item_count];
        c.x1 = ITEM_CENTER_X + coins[i].x - ITEM_WIDTH / 2;
        c.y1 = ITEM_CENTER_Y + coins[i].y - ITEM_HEIGHT / 2;
        c.x2 = c.x1 + ITEM_WIDTH;
        c.y2 = c.y1 + ITEM_HEIGHT;
        c.item_type = ITEM_TYPE_COIN;
        c.active = true;
        if (c.y1 < cached_items_min_y) cached_items_min_y = c.y1;
        if (c.y2 > cached_items_max_y) cached_items_max_y = c.y2;

        // Debug: log coin caching details
        static uint32_t coin_cache_log = 0;
        if ((coin_cache_log++ % 120) == 0) {  // Every 120 frames (~2 sec)
            ESP_LOGI(TAG, "💰 Cached coin[%d]: offset(%d,%d) screen(%d,%d to %d,%d)",
                     i, coins[i].x, coins[i].y, c.x1, c.y1, c.x2, c.y2);
        }
        cached_item_count++;
    }

    // Debug: log item caching status (once per 60 frames to avoid spam)
    static uint32_t item_log_counter = 0;
    if (cached_item_count > 0 && (item_log_counter++ % 60) == 0) {
        ESP_LOGI(TAG, "Items cached: %d (y range: %d-%d)", cached_item_count, cached_items_min_y, cached_items_max_y);
    }

    return cached_item_count > 0;
}

// Fast item pixel lookup using cached bounds - call from rendering loop
// Only checks cached items, assumes prepare_item_bounds_cache() was called
static inline bool sample_item_pixel_fast(int16_t screen_x, int16_t screen_y, uint16_t* out_pixel) {
    // Quick row rejection (most rows have no items)
    if (screen_y < cached_items_min_y || screen_y >= cached_items_max_y) {
        return false;
    }

    auto& item_loader = ItemLoader::GetInstance();

    for (uint8_t i = 0; i < cached_item_count; i++) {
        const auto& c = cached_items[i];
        // Bounds check
        if (screen_x < c.x1 || screen_x >= c.x2 ||
            screen_y < c.y1 || screen_y >= c.y2) {
            continue;
        }
        // Inside this item - check transparency
        uint16_t local_x = screen_x - c.x1;
        uint16_t local_y = screen_y - c.y1;
        if (item_loader.IsTransparent(c.item_type, local_x, local_y)) {
            continue;  // Transparent pixel, check next item
        }
        *out_pixel = item_loader.GetPixel(c.item_type, local_x, local_y);
        return true;
    }
    return false;
}

// Animation frame range: 6 animations × 13 frames = 78 (indices 0-77)
// ANIM_FRAME_COUNT is defined in animation_loader.h

// Map device state to emotion name (currently unused)
#if 0
static const char* get_emotion_for_state(DeviceState state) {
    switch (state) {
        case kDeviceStateSpeaking:  return "happy";
        case kDeviceStateListening: return "idle";
        case kDeviceStateIdle:      return "neutral";
        default:                    return "neutral";
    }
}
#endif

// Animation manager state
static struct {
    lv_obj_t* bg_image;         // Animation layer
    lv_obj_t* static_bg_image;  // Static background layer (behind animation)
    lv_img_dsc_t static_bg_dsc; // Static background descriptor
    esp_timer_handle_t timer;
    lv_img_dsc_t frame_dsc;

    // Current animation
    const AnimationDef* current_anim;   // Current playing animation
    const AnimationDef* base_anim;      // Base animation (determined by device state)
    uint16_t current_frame;             // Current frame within animation
    int8_t anim_direction;              // 1=forward, -1=backward

    // Touch interaction
    const AnimationDef* touch_anim;     // Touch-triggered animation
    int64_t touch_start_time;
    bool touch_active;
    bool swipe_active;
    bool swipe_ending;

    // Sleep tracking
    int64_t last_activity_time;
    bool is_sleeping;
    int64_t sleep_start_time;       // When sleep started (for auto-wake)
    bool sleep_is_daytime;          // Was it daytime when sleep started

    // Animation position offset
    int16_t anim_offset_x;          // Current X offset from center (negative = left)
    int16_t anim_offset_y;          // Current Y offset from center
    bool anim_mirror_x;             // Mirror animation horizontally

    // Unified pet behavior state machine (replaces random_walk_* and random_action_*)
    struct {
        PetBehaviorState state;           // Current state
        int64_t behavior_start_time;      // When current behavior started
        int64_t next_behavior_time;       // When to start next behavior
        int64_t cooldown_end_time;        // When cooldown ends

        // Walk parameters (only used in WALKING state)
        int16_t walk_start_x, walk_start_y;
        int16_t walk_target_x, walk_target_y;

        // Action parameters (only used in ACTION state)
        const char* current_action;       // "pet_head"/"talk"/"listen"
    } pet_behavior;

    // PWR walk protection (prevent random walk conflict with PWR walk-off/back)
    int64_t pwr_walk_cooldown_until; // Don't start random walk until this time

    // State tracking
    DeviceState last_device_state;

    // UI state
    bool ui_transparent;
    lv_obj_t* container;
    lv_obj_t* content;        // Chat content area (has background color)
    lv_obj_t* top_bar;
    lv_obj_t* bottom_bar;
    lv_obj_t* chat_message_label;
    lv_obj_t* status_label;
    lv_obj_t* notification_label;
    lv_obj_t* network_label;
    lv_obj_t* mute_label;
    lv_obj_t* battery_label;
    lv_obj_t* pet_status_container;
} anim_mgr = {
    .bg_image = nullptr,
    .static_bg_image = nullptr,
    .static_bg_dsc = {},
    .timer = nullptr,
    .frame_dsc = {},
    .current_anim = nullptr,
    .base_anim = nullptr,
    .current_frame = 0,
    .anim_direction = 1,
    .touch_anim = nullptr,
    .touch_start_time = 0,
    .touch_active = false,
    .swipe_active = false,
    .swipe_ending = false,
    .last_activity_time = 0,
    .is_sleeping = false,
    .sleep_start_time = 0,
    .sleep_is_daytime = false,
    .anim_offset_x = 0,
    .anim_offset_y = 0,
    .anim_mirror_x = false,
    .pet_behavior = {
        .state = PetBehaviorState::IDLE,
        .behavior_start_time = 0,
        .next_behavior_time = 0,
        .cooldown_end_time = 0,
        .walk_start_x = 0,
        .walk_start_y = 0,
        .walk_target_x = 0,
        .walk_target_y = 0,
        .current_action = nullptr,
    },
    .pwr_walk_cooldown_until = 0,
    .last_device_state = kDeviceStateUnknown,
    .ui_transparent = false,
    .container = nullptr,
    .content = nullptr,
    .top_bar = nullptr,
    .bottom_bar = nullptr,
    .chat_message_label = nullptr,
    .status_label = nullptr,
    .notification_label = nullptr,
    .network_label = nullptr,
    .mute_label = nullptr,
    .battery_label = nullptr,
    .pet_status_container = nullptr,
};

// Touch detection state (for pet walk control)
static struct {
    esp_lcd_touch_handle_t handle;
    bool initialized;

    // Touch tracking
    int16_t start_x;
    int16_t start_y;
    int16_t last_x;
    int16_t last_y;
    bool tracking;
} touch_state = {
    .handle = nullptr,
    .initialized = false,
    .start_x = 0,
    .start_y = 0,
    .last_x = 0,
    .last_y = 0,
    .tracking = false,
};



// Forward declarations (animation)
static void animation_switch_to(const char* emotion_name);
// static void animation_update_frame(void);  // Currently unused
static void process_touch_swipe(void);     // Touch/swipe detection for pet_head animation
static void apply_animation_ui_style(void);
static void init_static_background(void);

// Forward declaration for low-memory mode flag
static bool use_direct_lcd_mode = false;

// Bar background images - DISABLED to save 14KB RAM
// Both bars now use semi-transparent backgrounds with sampled colors
static lv_obj_t* top_bar_bg_img = nullptr;     // Not used (saves 14KB)
static lv_obj_t* bottom_bar_bg_img = nullptr;  // Not used (saves 14KB)

// Bar background color (used for both top and bottom bars)
// Default to BG_TIME_DAY color (0xC220 = orange-brown) instead of blue (0x1a1a)
static uint16_t bottom_bar_bg_color_rgb565 = 0xC220;

// Pre-sampled bar colors from backgrounds.bin (generated by scripts/sample_bg_colors.py)
// Run: python scripts/sample_bg_colors.py --input gifs/backgrounds.bin --output main/boards/waveshare-c6-lcd-1.69/bg_bar_colors.h
#include "bg_bar_colors.h"

// Helper: Convert RGB565 to lv_color (forward declaration for apply_animation_ui_style)
// Color is sampled directly from background buffer, so it matches the displayed background
static inline lv_color_t rgb565_to_lv_color(uint16_t rgb565) {
    uint8_t r5 = (rgb565 >> 11) & 0x1F;
    uint8_t g6 = (rgb565 >> 5) & 0x3F;
    uint8_t b5 = rgb565 & 0x1F;
    uint8_t r8 = (r5 << 3) | (r5 >> 2);
    uint8_t g8 = (g6 << 2) | (g6 >> 4);
    uint8_t b8 = (b5 << 3) | (b5 >> 2);
    return lv_color_make(r8, g8, b8);
}

// Apply transparent UI and white text after animation is loaded
static void apply_animation_ui_style(void) {
    if (anim_mgr.ui_transparent) {
        return;  // Already applied
    }

    // Set screen background to black for better contrast with white text
    // Animation (160x128) doesn't cover full screen (280x240), so areas outside
    // animation need a dark background for white text to be visible
    lv_obj_t* screen = lv_screen_active();
    if (screen != nullptr) {
        lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
    }

    // Show the background image (only if not using direct LCD output mode)
    // In direct LCD mode, animation is rendered directly to LCD, not via LVGL
    if (anim_mgr.bg_image != nullptr && !use_direct_lcd_mode) {
        lv_obj_clear_flag(anim_mgr.bg_image, LV_OBJ_FLAG_HIDDEN);
    }

    // Make all backgrounds transparent
    if (anim_mgr.container != nullptr) {
        lv_obj_set_style_bg_opa(anim_mgr.container, LV_OPA_TRANSP, 0);
    }
    if (anim_mgr.content != nullptr) {
        lv_obj_set_style_bg_opa(anim_mgr.content, LV_OPA_TRANSP, 0);
    }
    // NOTE: Bar colors and transparency are set by check_and_update_background()
    // Here we only set visibility
    if (anim_mgr.top_bar != nullptr) {
        lv_obj_remove_flag(anim_mgr.top_bar, LV_OBJ_FLAG_HIDDEN);
    }
    if (anim_mgr.bottom_bar != nullptr) {
        lv_obj_remove_flag(anim_mgr.bottom_bar, LV_OBJ_FLAG_HIDDEN);
    }

    // Force all text to white for better visibility on animated background
    lv_color_t white = lv_color_hex(0xFFFFFF);

    // Note: Pet status and chat message mutual exclusion is managed by SetChatMessage()
    // This function only updates the pet status values, not visibility

    if (anim_mgr.chat_message_label != nullptr) {
        lv_obj_set_style_text_color(anim_mgr.chat_message_label, white, 0);
    }

    if (anim_mgr.status_label != nullptr) {
        lv_obj_set_style_text_color(anim_mgr.status_label, white, 0);
        lv_obj_remove_flag(anim_mgr.status_label, LV_OBJ_FLAG_HIDDEN);
    }
    if (anim_mgr.notification_label != nullptr) {
        lv_obj_set_style_text_color(anim_mgr.notification_label, white, 0);
        lv_obj_remove_flag(anim_mgr.notification_label, LV_OBJ_FLAG_HIDDEN);
    }
    if (anim_mgr.network_label != nullptr) {
        lv_obj_set_style_text_color(anim_mgr.network_label, white, 0);
    }
    if (anim_mgr.mute_label != nullptr) {
        lv_obj_set_style_text_color(anim_mgr.mute_label, white, 0);
    }
    if (anim_mgr.battery_label != nullptr) {
        lv_obj_set_style_text_color(anim_mgr.battery_label, white, 0);
    }

    anim_mgr.ui_transparent = true;
    ESP_LOGI(TAG, "Animation UI style applied (transparent + white text)");
}

// Static background buffer for software compositing (actual size depends on background file)
// If memory is tight, can be nullptr and read from flash row-by-row
static uint16_t* static_bg_buffer = nullptr;

// Composite output buffer (280x240 RGB565 = 134KB) - the final rendered frame
// NOTE: May be nullptr on low-memory devices, will use row-by-row direct LCD output
static uint16_t* composite_buffer = nullptr;

// Row buffer for reading background from flash (280 pixels = 560 bytes)
static uint16_t* bg_row_buffer = nullptr;

// Second row buffer for composited output (280 pixels = 560 bytes)
// Used for low-memory row-by-row direct LCD output
static uint16_t* composite_row_buffer = nullptr;

// LCD panel handle for direct output
static esp_lcd_panel_handle_t direct_lcd_panel = nullptr;

// Top and bottom bar background buffers - DISABLED to save 14KB RAM
// Both bars now use semi-transparent backgrounds with sampled colors instead
static uint16_t* top_bar_bg_buffer = nullptr;     // Not allocated (saves 14KB)
static uint16_t* bottom_bar_bg_buffer = nullptr;  // Not allocated (saves 14KB)

// Actual background dimensions (may be smaller than composite area)
static uint16_t actual_bg_width = 0;
static uint16_t actual_bg_height = 0;
static uint16_t bg_offset_y = 0;  // Vertical offset to center background in composite area

// Helper: Swap bytes in RGB565 (for direct LCD output mode)
// LCD expects big-endian but ESP32 stores little-endian
static inline uint16_t swap_bytes_rgb565(uint16_t pixel) {
    return (pixel >> 8) | (pixel << 8);
}

// Helper: Invert RGB565 color (for direct LCD output mode)
// When bypassing LVGL, we need to manually handle DISPLAY_INVERT_COLOR
static inline uint16_t invert_rgb565(uint16_t pixel) {
    // RGB565: RRRRRGGGGGGBBBBB - invert each color component
    uint16_t r = 31 - ((pixel >> 11) & 0x1F);  // 5-bit red
    uint16_t g = 63 - ((pixel >> 5) & 0x3F);   // 6-bit green
    uint16_t b = 31 - (pixel & 0x1F);          // 5-bit blue
    return (r << 11) | (g << 5) | b;
}

// Helper: Convert ARGB8888 to RGB565
static inline uint16_t argb8888_to_rgb565(uint32_t argb) {
    uint8_t r = (argb >> 16) & 0xFF;
    uint8_t g = (argb >> 8) & 0xFF;
    uint8_t b = argb & 0xFF;
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

// Helper: Alpha blend foreground (ARGB8888) with background (RGB565)
static inline uint16_t blend_pixel_argb(uint32_t fg_argb, uint16_t bg_rgb565, uint8_t alpha) {
    // Extract foreground RGB
    uint8_t fg_r = (fg_argb >> 16) & 0xFF;
    uint8_t fg_g = (fg_argb >> 8) & 0xFF;
    uint8_t fg_b = fg_argb & 0xFF;

    // Extract background RGB565 -> RGB888
    uint8_t bg_r = ((bg_rgb565 >> 11) & 0x1F) << 3;
    uint8_t bg_g = ((bg_rgb565 >> 5) & 0x3F) << 2;
    uint8_t bg_b = (bg_rgb565 & 0x1F) << 3;

    // Blend: out = fg * alpha + bg * (255 - alpha)
    uint8_t out_r = (fg_r * alpha + bg_r * (255 - alpha)) / 255;
    uint8_t out_g = (fg_g * alpha + bg_g * (255 - alpha)) / 255;
    uint8_t out_b = (fg_b * alpha + bg_b * (255 - alpha)) / 255;

    // Convert back to RGB565
    return ((out_r >> 3) << 11) | ((out_g >> 2) << 5) | (out_b >> 3);
}

// Helper: Alpha blend foreground (RGB565) with background (RGB565) using separate alpha
static inline uint16_t blend_pixel_rgb565(uint16_t fg_rgb565, uint16_t bg_rgb565, uint8_t alpha) {
    // Extract foreground RGB565 -> RGB888
    uint8_t fg_r = ((fg_rgb565 >> 11) & 0x1F) << 3;
    uint8_t fg_g = ((fg_rgb565 >> 5) & 0x3F) << 2;
    uint8_t fg_b = (fg_rgb565 & 0x1F) << 3;

    // Extract background RGB565 -> RGB888
    uint8_t bg_r = ((bg_rgb565 >> 11) & 0x1F) << 3;
    uint8_t bg_g = ((bg_rgb565 >> 5) & 0x3F) << 2;
    uint8_t bg_b = (bg_rgb565 & 0x1F) << 3;

    // Blend
    uint8_t out_r = (fg_r * alpha + bg_r * (255 - alpha)) / 255;
    uint8_t out_g = (fg_g * alpha + bg_g * (255 - alpha)) / 255;
    uint8_t out_b = (fg_b * alpha + bg_b * (255 - alpha)) / 255;

    return ((out_r >> 3) << 11) | ((out_g >> 2) << 5) | (out_b >> 3);
}

// Current background index (0-16, maps to BackgroundLoader)
static uint16_t current_bg_idx = 0;

// Helper: Sample bar color from background buffer (bottom-center area)
// Returns RGB565 color averaged from bottom-center pixels
static uint16_t sample_bar_color_from_buffer(const uint16_t* bg_buffer, uint16_t width, uint16_t height) {
    if (bg_buffer == nullptr || width == 0 || height == 0) {
        return 0x0000;  // Black fallback
    }

    // Sample from Y=200 (visible background area), center X
    uint16_t sample_y = 200;
    uint16_t center_x = width / 2;

    uint32_t r_sum = 0, g_sum = 0, b_sum = 0;
    int sample_count = 0;

    // Sample 5 pixels horizontally at bottom center
    for (int dx = -2; dx <= 2; dx++) {
        int x = center_x + dx * 5;
        if (x >= 0 && x < width) {
            uint16_t pixel = bg_buffer[sample_y * width + x];
            // Extract RGB565 components
            r_sum += (pixel >> 11) & 0x1F;
            g_sum += (pixel >> 5) & 0x3F;
            b_sum += pixel & 0x1F;
            sample_count++;
        }
    }

    if (sample_count == 0) {
        return 0x0000;
    }

    // Average and reconstruct RGB565
    uint16_t r_avg = r_sum / sample_count;
    uint16_t g_avg = g_sum / sample_count;
    uint16_t b_avg = b_sum / sample_count;

    return (r_avg << 11) | (g_avg << 5) | b_avg;
}

// Initialize static background for 280x240 full display
// Backgrounds are stored in a separate file (backgrounds.bin) with FS format header
// Format: 280x240 fullscreen, 8-bit indexed with shared RGB565 palette
static void init_static_background(void) {
    static bool bg_init_attempted = false;
    if (bg_init_attempted) {
        return;
    }
    bg_init_attempted = true;

    // First ensure AnimationLoader is initialized (for freeing transparent buffers)
    auto& anim_loader = AnimationLoader::GetInstance();
    if (!anim_loader.IsInitialized()) {
        if (!anim_loader.Initialize()) {
            ESP_LOGW(TAG, "AnimationLoader init failed");
            return;
        }
    }

    // Free transparent buffers (ARGB8888/RGB565A8) to make room for composite buffer
    // We only need RGB565 mode for chroma key compositing
    anim_loader.FreeTransparentBuffers();

    // Find assets partition for backgrounds
    const esp_partition_t* partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "assets");
    if (!partition) {
        ESP_LOGW(TAG, "Assets partition not found - backgrounds disabled");
        return;
    }

    // Calculate background offset in partition
    // Animation data: 78 frames × 26368 bytes = 2,056,704 bytes
    // Background data starts after animation data
    size_t anim_data_size = (size_t)ANIM_FRAME_COUNT * ANIM_FRAME_SIZE_RAW;
    size_t bg_offset = anim_data_size;

    ESP_LOGI(TAG, "Initializing BackgroundLoader at offset %u (after %d animation frames)",
             (unsigned)bg_offset, ANIM_FRAME_COUNT);

    // Initialize BackgroundLoader
    auto& bg_loader = BackgroundLoader::GetInstance();
    if (!bg_loader.Initialize(partition, bg_offset)) {
        ESP_LOGW(TAG, "BackgroundLoader init failed - backgrounds disabled");
        // Continue without backgrounds - animation will still work
    } else {
        ESP_LOGI(TAG, "BackgroundLoader initialized: %dx%d, %d backgrounds",
                 bg_loader.GetWidth(), bg_loader.GetHeight(), bg_loader.GetBackgroundCount());
    }

    // Initialize ItemLoader (items come after backgrounds in partition)
    // Layout: animation data | background data (16 × 67968) | item data (2 × 2368)
    size_t item_offset = bg_offset + ((size_t)BG_COUNT * BG_FRAME_SIZE_RAW);
    auto& item_loader = ItemLoader::GetInstance();
    if (!item_loader.Initialize(partition, item_offset, ITEM_TYPE_COUNT)) {
        ESP_LOGW(TAG, "ItemLoader init failed - scene items disabled");
    } else {
        ESP_LOGI(TAG, "ItemLoader initialized: %dx%d, %d items",
                 item_loader.GetWidth(), item_loader.GetHeight(), item_loader.GetItemCount());
    }

    // Initialize SceneItemManager (uses NVS for persistence)
    SceneItemManager::GetInstance().Initialize();
    ESP_LOGI(TAG, "SceneItemManager initialized");

    // Initialize AmbientDialogue (cute pet dialogues)
    AmbientDialogue::GetInstance().Initialize();
    ESP_LOGI(TAG, "AmbientDialogue initialized");

    // Scene items will spawn naturally through Tick() based on game logic

    // Background dimensions (from BackgroundLoader if available, otherwise 280x240)
    if (bg_loader.IsInitialized()) {
        actual_bg_width = bg_loader.GetWidth();
        actual_bg_height = bg_loader.GetHeight();
    } else {
        actual_bg_width = COMPOSITE_WIDTH;   // 280
        actual_bg_height = COMPOSITE_HEIGHT; // 240
    }

    // Background is fullscreen (280x240), no offset needed
    bg_offset_y = 0;
    ESP_LOGI(TAG, "Background system: %dx%d fullscreen", actual_bg_width, actual_bg_height);

    // Allocate composite buffer (280x240 RGB565 = 134,400 bytes = 131KB)
    size_t composite_size = COMPOSITE_WIDTH * COMPOSITE_HEIGHT * sizeof(uint16_t);
    composite_buffer = (uint16_t*)heap_caps_malloc(composite_size, MALLOC_CAP_DMA);
    if (!composite_buffer) {
        composite_buffer = (uint16_t*)heap_caps_malloc(composite_size, MALLOC_CAP_INTERNAL);
    }
    if (!composite_buffer) {
        composite_buffer = (uint16_t*)malloc(composite_size);
    }

    if (!composite_buffer) {
        // Full composite buffer failed - try low-memory mode with row-by-row output
        ESP_LOGW(TAG, "Failed to allocate composite buffer (%u bytes) - trying low-memory mode", (unsigned)composite_size);

        // Allocate composite row buffer (only 280 pixels = 560 bytes)
        size_t row_size = COMPOSITE_WIDTH * sizeof(uint16_t);
        composite_row_buffer = (uint16_t*)heap_caps_malloc(row_size, MALLOC_CAP_DMA);
        if (!composite_row_buffer) {
            composite_row_buffer = (uint16_t*)malloc(row_size);
        }

        if (composite_row_buffer) {
            use_direct_lcd_mode = true;
            ESP_LOGI(TAG, "Low-memory mode enabled: row-by-row direct LCD output (%u bytes)", (unsigned)row_size);
        } else {
            ESP_LOGE(TAG, "Failed to allocate even row buffer - compositing disabled");
            return;
        }
    } else {
        ESP_LOGI(TAG, "Composite buffer allocated: %u bytes (%dx%d RGB565)",
                 (unsigned)composite_size, COMPOSITE_WIDTH, COMPOSITE_HEIGHT);
    }

    // Allocate row buffer for reading background from flash (background width = 280 pixels)
    size_t row_size = actual_bg_width * sizeof(uint16_t);
    bg_row_buffer = (uint16_t*)heap_caps_malloc(row_size, MALLOC_CAP_DMA);
    if (!bg_row_buffer) {
        bg_row_buffer = (uint16_t*)malloc(row_size);
    }

    if (!bg_row_buffer) {
        ESP_LOGE(TAG, "Failed to allocate row buffer (%u bytes)", (unsigned)row_size);
        if (composite_buffer) {
            free(composite_buffer);
            composite_buffer = nullptr;
        }
        if (composite_row_buffer) {
            free(composite_row_buffer);
            composite_row_buffer = nullptr;
            use_direct_lcd_mode = false;
        }
        return;
    }
    ESP_LOGI(TAG, "Row buffer allocated: %u bytes", (unsigned)row_size);

    // Note: Item rendering uses pre-decoded memory buffers in ItemLoader
    // No additional row buffer allocation needed here

    // Try to allocate full background buffer for faster compositing
    // Size is 280x240 RGB565 = 134,400 bytes
    size_t bg_buffer_size = actual_bg_width * actual_bg_height * sizeof(uint16_t);
    static_bg_buffer = (uint16_t*)heap_caps_malloc(bg_buffer_size, MALLOC_CAP_SPIRAM);
    if (!static_bg_buffer) {
        static_bg_buffer = (uint16_t*)heap_caps_malloc(bg_buffer_size, MALLOC_CAP_INTERNAL);
    }
    if (!static_bg_buffer) {
        static_bg_buffer = (uint16_t*)malloc(bg_buffer_size);
    }

    if (static_bg_buffer) {
        // Full background buffer available
        ESP_LOGI(TAG, "Full background buffer allocated (%u bytes)", (unsigned)bg_buffer_size);
        if (!bg_loader.IsInitialized()) {
            // No BackgroundLoader - fill with black as fallback
            memset(static_bg_buffer, 0, bg_buffer_size);
            ESP_LOGI(TAG, "Background buffer filled with black (no backgrounds available)");
        }
        // NOTE: Actual background decode is done by check_and_update_background()
    } else {
        ESP_LOGI(TAG, "Full background buffer not available - will use row-by-row mode (slower)");
    }

    // MEMORY OPTIMIZATION for ESP32C6: Don't allocate bar background buffers
    // Both bars use semi-transparent background with sampled colors (saves 14KB RAM)
    top_bar_bg_buffer = nullptr;
    bottom_bar_bg_buffer = nullptr;
    top_bar_bg_img = nullptr;
    bottom_bar_bg_img = nullptr;

    // NOTE: Bar color is set by check_and_update_background() to avoid duplication

    // Summary log
    if (use_direct_lcd_mode) {
        ESP_LOGI(TAG, "Background system initialized (LOW-MEMORY MODE):");
        ESP_LOGI(TAG, "  Mode: Direct LCD row-by-row output");
        ESP_LOGI(TAG, "  Buffers: composite_row=%p, bg_row=%p", composite_row_buffer, bg_row_buffer);
    } else {
        ESP_LOGI(TAG, "Background system initialized (NORMAL MODE):");
        ESP_LOGI(TAG, "  Buffers: composite=%p, bg_row=%p, static_bg=%p",
                 composite_buffer, bg_row_buffer, static_bg_buffer);
    }
    ESP_LOGI(TAG, "  Background: %dx%d, anim_offset=(%d,%d)",
             actual_bg_width, actual_bg_height,
             ANIM_OFFSET_IN_COMPOSITE_X, ANIM_OFFSET_IN_COMPOSITE_Y);
}

// Check and update background based on time/weather/festival
// Call this periodically (e.g., every minute or on state change)
// Made non-static so MCP tools can trigger background refresh
void check_and_update_background(bool force_update = false) {
    static uint16_t last_bg_idx = 0xFFFF;  // Invalid initial value
    static uint32_t last_check_time = 0;
    static bool bar_color_applied = false;  // Track if bar color has been applied at least once

    // Check at most once per minute (60000ms), unless force_update is true
    uint32_t now = esp_timer_get_time() / 1000;
    if (!force_update && now - last_check_time < 60000 && last_bg_idx != 0xFFFF && bar_color_applied) {
        return;
    }
    last_check_time = now;

    // When force_update is true, reset last_bg_idx to ensure the background is reloaded
    if (force_update) {
        last_bg_idx = 0xFFFF;
    }

    // Update time from system clock
    time_t now_time;
    struct tm timeinfo;
    if (time(&now_time) != -1 && localtime_r(&now_time, &timeinfo)) {
        auto& bg_mgr = BackgroundManager::GetInstance();
        bg_mgr.UpdateTime(timeinfo.tm_hour, timeinfo.tm_min,
                          timeinfo.tm_mon + 1, timeinfo.tm_mday,
                          timeinfo.tm_year + 1900);
    }

    // Get recommended background from manager (returns 0-16 index)
    auto& bg_mgr = BackgroundManager::GetInstance();
    uint16_t new_bg_idx = bg_mgr.GetCurrentBackground();

    // Clamp to valid range (use 16 as max if BackgroundLoader not initialized)
    auto& bg_loader = BackgroundLoader::GetInstance();
    uint16_t max_bg = bg_loader.IsInitialized() ? bg_loader.GetBackgroundCount() : 16;
    if (new_bg_idx >= max_bg) {
        new_bg_idx = 0;  // Fallback to base background
    }

    // Check if background changed OR bar color never applied
    if (new_bg_idx != last_bg_idx || !bar_color_applied) {
        ESP_LOGI(TAG, "Background switching: %d -> %d (bar_color_applied=%d)",
                 last_bg_idx, new_bg_idx, bar_color_applied);
        last_bg_idx = new_bg_idx;
        current_bg_idx = new_bg_idx;

        // Load background to buffer (if BackgroundLoader initialized and buffer available)
        if (bg_loader.IsInitialized() && static_bg_buffer != nullptr &&
            actual_bg_width > 0 && actual_bg_height > 0) {
            bg_loader.DecodeFull(new_bg_idx, static_bg_buffer);
            ESP_LOGI(TAG, "Background %d loaded to buffer", new_bg_idx);

            // Sample bar color directly from loaded background buffer
            bottom_bar_bg_color_rgb565 = sample_bar_color_from_buffer(
                static_bg_buffer, actual_bg_width, actual_bg_height);
            ESP_LOGI(TAG, "Bar color sampled from buffer: 0x%04X", bottom_bar_bg_color_rgb565);
        } else {
            // No buffer or BackgroundLoader not initialized: use pre-sampled bar color from compile-time table
            bottom_bar_bg_color_rgb565 = bg_bar_colors[new_bg_idx < 16 ? new_bg_idx : 0];
            ESP_LOGI(TAG, "Bar color from pre-sampled table[%d]: 0x%04X", new_bg_idx, bottom_bar_bg_color_rgb565);
        }

        // Apply color and transparency to bars (single source of truth for bar styling)
        if (lvgl_port_lock(50)) {
            lv_color_t bar_color = rgb565_to_lv_color(bottom_bar_bg_color_rgb565);
            if (anim_mgr.top_bar != nullptr) {
                lv_obj_set_style_bg_color(anim_mgr.top_bar, bar_color, 0);
                lv_obj_set_style_bg_opa(anim_mgr.top_bar, LV_OPA_70, 0);
            }
            if (anim_mgr.bottom_bar != nullptr) {
                lv_obj_set_style_bg_color(anim_mgr.bottom_bar, bar_color, 0);
                lv_obj_set_style_bg_opa(anim_mgr.bottom_bar, LV_OPA_70, 0);
            }
            bar_color_applied = true;
            ESP_LOGI(TAG, "Bar color applied: 0x%04X", bottom_bar_bg_color_rgb565);
            lvgl_port_unlock();
        }
    }
}

// Switch to a different animation by emotion name
static void animation_switch_to(const char* emotion_name) {
    if (!emotion_name) {
        return;
    }

    ESP_LOGI(TAG, "animation_switch_to: %s", emotion_name);

    auto& loader = AnimationLoader::GetInstance();

    // Must be initialized to switch animations
    if (!loader.IsInitialized()) {
        ESP_LOGW(TAG, "animation_switch_to: AnimationLoader not initialized yet");
        return;
    }

    const AnimationDef* anim = loader.GetAnimationByName(emotion_name);

    if (!anim || anim == anim_mgr.current_anim) {
        return;  // Not found or already playing
    }

    // Get first frame - try ARGB8888, then RGB565A8, then RGB565 with chroma key
    const uint8_t* frame_data;
    if (loader.IsARGBAvailable()) {
        frame_data = loader.GetFrameByIndexARGB(anim->start_frame);
    } else if (loader.IsRGB565A8Available()) {
        frame_data = loader.GetFrameByIndexRGB565A8(anim->start_frame);
    } else {
        frame_data = loader.GetFrameByIndex(anim->start_frame);
    }
    if (frame_data == nullptr) {
        ESP_LOGW(TAG, "Failed to load frame for animation: %s", emotion_name);
        return;
    }

    // Update frame descriptor for new 160x128 frames
    anim_mgr.frame_dsc.header.w = ANIM_FRAME_WIDTH;
    anim_mgr.frame_dsc.header.h = ANIM_FRAME_HEIGHT;
    if (loader.IsARGBAvailable()) {
        anim_mgr.frame_dsc.header.cf = LV_COLOR_FORMAT_ARGB8888;
        anim_mgr.frame_dsc.header.stride = ANIM_FRAME_WIDTH * 4;  // 4 bytes per pixel
        anim_mgr.frame_dsc.data_size = ANIM_FRAME_SIZE_ARGB8888;
    } else if (loader.IsRGB565A8Available()) {
        anim_mgr.frame_dsc.header.cf = LV_COLOR_FORMAT_RGB565A8;
        anim_mgr.frame_dsc.header.stride = ANIM_FRAME_WIDTH * 2;  // RGB565 stride
        anim_mgr.frame_dsc.data_size = ANIM_FRAME_SIZE_RGB565A8;
    } else {
        anim_mgr.frame_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
        anim_mgr.frame_dsc.header.stride = ANIM_FRAME_WIDTH * 2;  // 2 bytes per pixel
        anim_mgr.frame_dsc.data_size = ANIM_FRAME_SIZE_RGB565;
    }
    anim_mgr.frame_dsc.data = frame_data;

    anim_mgr.current_anim = anim;
    anim_mgr.current_frame = 0;
    anim_mgr.anim_direction = 1;

    // Track base animation for state animations (not interjections)
    // Base animation is used to restore after interjection plays
    if (strcmp(emotion_name, "idle") == 0 ||
        strcmp(emotion_name, "neutral") == 0 ||
        strcmp(emotion_name, "standby") == 0 ||
        strcmp(emotion_name, "listening") == 0 ||
        strcmp(emotion_name, "speaking") == 0 ||
        strcmp(emotion_name, "talking") == 0) {
        anim_mgr.base_anim = anim;
        ESP_LOGI(TAG, "Set base animation: %s", anim->name);
    }

    ESP_LOGI(TAG, "Switched to animation: %s (frames %d-%d, %dfps)",
             anim->name, anim->start_frame,
             anim->start_frame + anim->frame_count - 1, anim->fps);
}

// Update current frame (currently unused - using animation_timer_callback instead)
#if 0
static void animation_update_frame(void) {
    if (anim_mgr.bg_image == nullptr) {
        return;
    }

    // Lazy initialization of AnimationLoader
    static bool loader_init_attempted = false;
    static bool loader_initialized = false;
    if (!loader_init_attempted) {
        auto& app = Application::GetInstance();
        DeviceState state = app.GetDeviceState();

        // Wait until device is ready
        if (state != kDeviceStateIdle && state != kDeviceStateListening && state != kDeviceStateSpeaking) {
            return;
        }

        loader_init_attempted = true;
        if (!AnimationLoader::GetInstance().Initialize()) {
            ESP_LOGE(TAG, "Failed to initialize AnimationLoader - check if frames.bin is flashed to 0x800000");
        } else {
            loader_initialized = true;
            ESP_LOGI(TAG, "AnimationLoader initialized (FS format, 160x128, 4bit)");

            // Now initialize background (deferred to here so WiFi can init first)
            // This needs AnimationLoader to be ready to calculate background offset
            init_static_background();

            // Set default animation
            animation_switch_to("idle");

            // Apply UI style and show first frame immediately
            if (lvgl_port_lock(0)) {
                apply_animation_ui_style();

                // Force update LVGL image with first frame
                if (anim_mgr.frame_dsc.data != nullptr) {
                    lv_image_set_src(anim_mgr.bg_image, &anim_mgr.frame_dsc);
                    lv_obj_invalidate(anim_mgr.bg_image);
                    ESP_LOGI(TAG, "First frame displayed (data=%p, size=%lux%lu)",
                             anim_mgr.frame_dsc.data,
                             (unsigned long)anim_mgr.frame_dsc.header.w,
                             (unsigned long)anim_mgr.frame_dsc.header.h);
                } else {
                    ESP_LOGW(TAG, "No frame data available for display");
                }

                lvgl_port_unlock();
            }
        }
    }

    if (!loader_initialized || !anim_mgr.current_anim) {
        return;
    }

    // Calculate global frame index
    uint16_t global_frame = anim_mgr.current_anim->start_frame + anim_mgr.current_frame;

    // Get decoded frame - try ARGB8888, then RGB565A8, then RGB565
    auto& loader = AnimationLoader::GetInstance();
    const uint8_t* frame_data;
    if (loader.IsARGBAvailable()) {
        frame_data = loader.GetFrameByIndexARGB(global_frame);
    } else if (loader.IsRGB565A8Available()) {
        frame_data = loader.GetFrameByIndexRGB565A8(global_frame);
    } else {
        frame_data = loader.GetFrameByIndex(global_frame);
    }
    if (frame_data == nullptr) {
        ESP_LOGW(TAG, "Failed to get frame %d", global_frame);
        return;
    }
    anim_mgr.frame_dsc.data = frame_data;

    // Update LVGL image
    if (lvgl_port_lock(0)) {
        lv_image_set_src(anim_mgr.bg_image, &anim_mgr.frame_dsc);
        lv_obj_invalidate(anim_mgr.bg_image);
        lvgl_port_unlock();
    }
}
#endif

// Forward declarations for pet behavior state machine
static void pet_behavior_update(void);
static void pet_behavior_interrupt(void);

// Animation timer callback - uses anim_mgr.current_anim for state-based animation
static void animation_timer_callback(void* arg) {
    static bool loader_init_attempted = false;
    static bool loader_initialized = false;

    // Lazy initialization of AnimationLoader (only attempt once)
    if (!loader_init_attempted) {
        auto& app = Application::GetInstance();
        DeviceState state = app.GetDeviceState();

        // Wait until device is ready (idle/listening/speaking)
        if (state != kDeviceStateIdle && state != kDeviceStateListening && state != kDeviceStateSpeaking) {
            return;
        }

        loader_init_attempted = true;  // Mark as attempted, won't retry if failed

        if (!AnimationLoader::GetInstance().Initialize()) {
            ESP_LOGE(TAG, "Failed to initialize AnimationLoader - animation disabled");
            return;
        }

        loader_initialized = true;
        ESP_LOGI(TAG, "AnimationLoader initialized - starting state-based animation");

        // Initialize background (deferred to here so WiFi can init first)
        init_static_background();

        // Initial background selection based on current time/conditions
        check_and_update_background();

        // Apply UI style
        if (lvgl_port_lock(0)) {
            apply_animation_ui_style();
            lvgl_port_unlock();
        }

        // Set initial animation based on current device state
        if (state == kDeviceStateIdle) {
            animation_switch_to("idle");
            ESP_LOGI(TAG, "Initial state: idle");
        } else if (state == kDeviceStateListening) {
            animation_switch_to("listen");
            ESP_LOGI(TAG, "Initial state: listening");
        } else if (state == kDeviceStateSpeaking) {
            animation_switch_to("talk");
            ESP_LOGI(TAG, "Initial state: speaking");
        }
    }

    if (!loader_initialized) {
        return;
    }

    // Periodically check if background should change (time/weather/festival)
    check_and_update_background();

    // Scene items tick - check for coin/poop spawning (once per second)
    static int64_t last_scene_tick = 0;
    int64_t now_ms = esp_timer_get_time() / 1000;
    if (now_ms - last_scene_tick >= 1000) {
        SceneItemManager::GetInstance().Tick();
        last_scene_tick = now_ms;
    }

    // Process touch swipe for pet_head animation
    process_touch_swipe();

    // Pet behavior state machine - handles random walk and actions
    pet_behavior_update();

    // Check for inactivity timeout - switch to sleep animation
    // Daytime (8:00-19:00): 10 minutes, Nighttime: 5 minutes
    // Only in idle state (not during listening/speaking)
    auto& app = Application::GetInstance();
    DeviceState current_state = app.GetDeviceState();

    // Check if should auto-wake from sleep
    if (anim_mgr.is_sleeping && anim_mgr.sleep_start_time > 0) {
        int64_t now = esp_timer_get_time() / 1000;
        int64_t sleep_duration = now - anim_mgr.sleep_start_time;
        int64_t max_sleep = anim_mgr.sleep_is_daytime
            ? ANIM_SLEEP_DURATION_DAY_MS    // 10 minutes
            : ANIM_SLEEP_DURATION_NIGHT_MS; // 30 minutes

        if (sleep_duration >= max_sleep) {
            // Wake up!
            animation_switch_to("idle");
            anim_mgr.is_sleeping = false;
            anim_mgr.sleep_start_time = 0;
            anim_mgr.last_activity_time = now;  // Reset activity timer
            ESP_LOGI(TAG, "Auto-waking from sleep (slept %.1f min, %s)",
                     sleep_duration / 60000.0f,
                     anim_mgr.sleep_is_daytime ? "daytime" : "nighttime");
        }
    }
    // Check for inactivity timeout - switch to sleep animation
    else if (!anim_mgr.is_sleeping &&
             current_state == kDeviceStateIdle &&
             anim_mgr.current_anim != nullptr) {

        int64_t now = esp_timer_get_time() / 1000;
        int64_t idle_duration = now - anim_mgr.last_activity_time;

        // Check if current animation is NOT already sleep
        bool is_sleep_anim = (strcmp(anim_mgr.current_anim->name, "sleep") == 0);

        // Determine sleep timeout based on time of day
        time_t now_time;
        struct tm timeinfo;
        time(&now_time);
        localtime_r(&now_time, &timeinfo);
        int hour = timeinfo.tm_hour;
        bool is_daytime = (hour >= 8 && hour < 19);
        // Daytime: 10 min timeout, Nighttime: 5 min timeout
        int64_t sleep_timeout = is_daytime
            ? ANIM_SLEEP_TIMEOUT_DAY_MS
            : ANIM_SLEEP_TIMEOUT_NIGHT_MS;

        if (!is_sleep_anim && idle_duration >= sleep_timeout) {
            // Enter sleep
            // 停止所有正在进行的宠物行为（移动、动作）
            pet_behavior_interrupt();

            animation_switch_to("sleep");
            anim_mgr.is_sleeping = true;
            anim_mgr.sleep_start_time = now;
            anim_mgr.sleep_is_daytime = is_daytime;
            ESP_LOGI(TAG, "Entering sleep (%.1f min idle, %s) - will sleep for %d min",
                     idle_duration / 60000.0f,
                     is_daytime ? "daytime" : "nighttime",
                     is_daytime ? 10 : 30);
        }
    }

    auto& loader = AnimationLoader::GetInstance();

    // Use current animation from anim_mgr (set by animation_switch_to)
    // If no animation is set, default to idle
    if (anim_mgr.current_anim == nullptr) {
        animation_switch_to("idle");
        if (anim_mgr.current_anim == nullptr) {
            return;  // Still no animation, give up
        }
    }

    // Calculate global frame index from current animation
    uint16_t global_frame_idx = anim_mgr.current_anim->start_frame + anim_mgr.current_frame;

    // Always use RGB565 mode for compositing (chroma key transparency)
    // Background color comes from animation frame's palette (index 0)
    const uint8_t* frame_data = loader.GetFrameByIndex(global_frame_idx);
    if (frame_data == nullptr) {
        ESP_LOGW(TAG, "Failed to get frame %d", global_frame_idx);
        return;
    }

    // Background transparency is determined by color range (green tones)
    // No need to read palette - just check if pixel color is within background range

    // Check for invalid flash data (all 0xFF means not programmed)
    // Only check first 8 bytes to save time
    bool invalid_data = true;
    for (int i = 0; i < 8; i++) {
        if (frame_data[i] != 0xFF) {
            invalid_data = false;
            break;
        }
    }
    if (invalid_data) {
        static bool warned = false;
        if (!warned) {
            ESP_LOGE(TAG, "Animation data appears invalid (all 0xFF) - please flash assets to 0x800000");
            ESP_LOGE(TAG, "Run: esptool.py --chip esp32c6 write_flash 0x800000 gifs/assets_combined.bin");
            warned = true;
        }
        return;  // Skip rendering invalid data
    }

    // Software compositing: 280x240 background (or smaller centered) + 160x128 animation
    // Animation is placed at (60, 31) within the composite buffer
    // Uses chroma key (0xF81F) for transparency (hard edges)
    bool use_composite = false;
    bool use_direct_output = false;

    // Check which compositing mode to use
    if (composite_buffer != nullptr && bg_row_buffer != nullptr) {
        // Full buffer mode - composite to buffer, then display via LVGL
        use_composite = true;
    } else if (use_direct_lcd_mode && composite_row_buffer != nullptr &&
               bg_row_buffer != nullptr && direct_lcd_panel != nullptr) {
        // Low-memory mode - composite row by row, direct to LCD
        use_direct_output = true;
    }

    if (use_composite || use_direct_output) {
        // For direct LCD output mode, acquire LVGL port lock to prevent SPI bus conflicts
        // This ensures animation timer and LVGL task don't access LCD simultaneously
        bool lock_acquired = false;
        if (use_direct_output) {
            lock_acquired = lvgl_port_lock(0);  // Non-blocking
            if (!lock_acquired) {
                // LVGL is busy, skip this frame to avoid SPI bus conflict
                goto advance_frame;
            }
        }

        auto& bg_loader = BackgroundLoader::GetInstance();
        const uint16_t* anim_frame = (const uint16_t*)frame_data;  // RGB565 format

        // Background: 280x240 fullscreen (from BackgroundLoader)
        // Animation: 160x160 source scaled to ANIM_SCALED_WIDTH x ANIM_SCALED_HEIGHT, with dynamic offset
        // Chroma key (green tones) in animation means transparent - show background through

        // Calculate dynamic animation offset (base + walk-off offset)
        int16_t dyn_offset_x = ANIM_OFFSET_IN_COMPOSITE_X + anim_mgr.anim_offset_x;
        int16_t dyn_offset_y = ANIM_OFFSET_IN_COMPOSITE_Y + anim_mgr.anim_offset_y;

        // Pre-compute item bounds once per frame (avoids 53K+ function calls)
        bool has_items = prepare_item_bounds_cache();

        // Composite row by row (only render rows that will be output to LCD)
        // Skip top UI (0-24) and bottom UI (215-239), only render visible area (25-214)
        for (uint16_t y = TOP_UI_HEIGHT; y < COMPOSITE_HEIGHT - BOTTOM_UI_HEIGHT; y++) {
            // Get background row - from RAM buffer or decode from flash
            uint16_t* bg_row = nullptr;
            if (static_bg_buffer != nullptr) {
                // Fast mode: background already in RAM (280x240)
                bg_row = &static_bg_buffer[y * actual_bg_width];
            } else if (bg_row_buffer != nullptr && bg_loader.IsInitialized()) {
                // Slow mode: decode row from flash
                bg_loader.DecodeRow(current_bg_idx, y, bg_row_buffer);
                bg_row = bg_row_buffer;
            }

            // Check if this row overlaps with scaled animation area (using dynamic offset)
            bool in_anim_y = ((int16_t)y >= dyn_offset_y &&
                              (int16_t)y < dyn_offset_y + ANIM_SCALED_HEIGHT);
            // Map scaled Y to source Y using nearest-neighbor sampling
            int16_t scaled_y = (int16_t)y - dyn_offset_y;  // Position within scaled output
            uint16_t src_y = (scaled_y >= 0 && scaled_y < ANIM_SCALED_HEIGHT)
                           ? (scaled_y * ANIM_FRAME_HEIGHT) / ANIM_SCALED_HEIGHT : 0;

            // Determine output buffer
            uint16_t* out_row = use_direct_output ? composite_row_buffer
                                                  : &composite_buffer[y * COMPOSITE_WIDTH];

            // Note: Item rendering uses pre-decoded memory buffers, no cache invalidation needed

            for (uint16_t x = 0; x < COMPOSITE_WIDTH; x++) {
                uint16_t out_pixel;
                uint16_t item_pixel;

                // Check if this pixel overlaps with scaled animation area (using dynamic offset)
                bool in_anim_x = ((int16_t)x >= dyn_offset_x &&
                                  (int16_t)x < dyn_offset_x + ANIM_SCALED_WIDTH);

                if (in_anim_y && in_anim_x) {
                    // Inside scaled animation area - sample from source with scaling
                    int16_t scaled_x = (int16_t)x - dyn_offset_x;  // Position within scaled output
                    uint16_t src_x = (scaled_x >= 0 && scaled_x < ANIM_SCALED_WIDTH)
                                   ? (scaled_x * ANIM_FRAME_WIDTH) / ANIM_SCALED_WIDTH : 0;
                    // Apply horizontal mirror if walking back (face right instead of left)
                    if (anim_mgr.anim_mirror_x) {
                        src_x = ANIM_FRAME_WIDTH - 1 - src_x;
                    }
                    uint16_t anim_pixel = anim_frame[src_y * ANIM_FRAME_WIDTH + src_x];

                    if (is_background_color(anim_pixel)) {
                        // Transparent animation pixel - check items first, then background
                        if (has_items && sample_item_pixel_fast(x, y, &item_pixel)) {
                            out_pixel = item_pixel;
                        } else {
                            out_pixel = (bg_row != nullptr) ? bg_row[x] : 0x0000;
                        }
                    } else {
                        // Opaque animation pixel
                        out_pixel = anim_pixel;
                    }
                } else {
                    // Outside animation area - check items first, then background
                    if (has_items && sample_item_pixel_fast(x, y, &item_pixel)) {
                        out_pixel = item_pixel;
                    } else {
                        out_pixel = (bg_row != nullptr) ? bg_row[x] : 0x0000;
                    }
                }

                // In direct LCD mode, need to swap bytes (LCD expects big-endian)
                // LVGL handles this automatically, but we bypass LVGL in direct output
                if (use_direct_output) {
                    out_row[x] = swap_bytes_rgb565(out_pixel);
                } else {
                    out_row[x] = out_pixel;
                }
            }

            // In direct output mode, send each row immediately to LCD
            // Skip top UI area (0-24) and bottom UI area (210-239) to preserve LVGL-drawn elements
            // Only output rows 25-209 (185 rows) to LCD
            if (use_direct_output) {
                int screen_y = COMPOSITE_SCREEN_Y + y;
                if (screen_y >= TOP_UI_HEIGHT && screen_y < (COMPOSITE_HEIGHT - BOTTOM_UI_HEIGHT)) {
                    esp_lcd_panel_draw_bitmap(direct_lcd_panel,
                                              DISPLAY_OFFSET_X, screen_y,
                                              DISPLAY_OFFSET_X + COMPOSITE_WIDTH, screen_y + 1,
                                              out_row);
                }
            }
        }

        // Release LVGL lock if we acquired it for direct output
        if (use_direct_output && lock_acquired) {
            lvgl_port_unlock();
        }
    }

    // Skip LVGL image update if using direct LCD output (already sent row by row)
    if (!use_direct_output) {
        // Update frame descriptor for composite output (exclude bottom UI area)
        size_t composite_pixels = COMPOSITE_WIDTH * (COMPOSITE_HEIGHT - BOTTOM_UI_HEIGHT);
        anim_mgr.frame_dsc.header.w = COMPOSITE_WIDTH;
        anim_mgr.frame_dsc.header.h = COMPOSITE_HEIGHT - BOTTOM_UI_HEIGHT;  // 210 rows (240 - 30)
        if (use_composite) {
            // Composited output is always RGB565
            anim_mgr.frame_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
            anim_mgr.frame_dsc.header.stride = COMPOSITE_WIDTH * 2;
            anim_mgr.frame_dsc.data_size = composite_pixels * sizeof(uint16_t);
            anim_mgr.frame_dsc.data = (const uint8_t*)composite_buffer;
        } else {
            // No compositing available - fallback to animation only (160x128)
            anim_mgr.frame_dsc.header.w = ANIM_FRAME_WIDTH;
            anim_mgr.frame_dsc.header.h = ANIM_FRAME_HEIGHT;
            anim_mgr.frame_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
            anim_mgr.frame_dsc.header.stride = ANIM_FRAME_WIDTH * 2;
            anim_mgr.frame_dsc.data_size = ANIM_FRAME_SIZE_RGB565;
            anim_mgr.frame_dsc.data = frame_data;
        }

        // Update LVGL image
        if (lvgl_port_lock(0)) {
            lv_image_set_src(anim_mgr.bg_image, &anim_mgr.frame_dsc);
            lv_obj_invalidate(anim_mgr.bg_image);
            lvgl_port_unlock();
        }
    }

advance_frame:
    // Advance to next frame within current animation (loop within animation)
    if (anim_mgr.current_anim != nullptr) {
        anim_mgr.current_frame++;
        if (anim_mgr.current_frame >= anim_mgr.current_anim->frame_count) {
            anim_mgr.current_frame = 0;  // Loop within this animation
        }
    }
}


// =============================================================================
// Pet Behavior State Machine - Unified management of random walk/action
// =============================================================================

// Check if pet behavior can start (external conditions)
static bool pet_behavior_can_start(void) {
    if (anim_mgr.touch_active || anim_mgr.swipe_active) return false;
    if (anim_mgr.is_sleeping) return false;

    int64_t now = esp_timer_get_time() / 1000;
    if (now < anim_mgr.pwr_walk_cooldown_until) return false;

    // Don't start random behavior while pet is eating, bathing, or sleeping
    auto& pet = PetStateMachine::GetInstance();
    PetAction current_action = pet.GetAction();
    if (current_action == PetAction::kEating ||
        current_action == PetAction::kBathing ||
        current_action == PetAction::kSleeping) {
        return false;
    }

    auto& app = Application::GetInstance();
    return app.GetDeviceState() == kDeviceStateIdle;
}

// Start random walk
static void pet_behavior_start_walk(void) {
    ESP_LOGI(TAG, "==> WALK TRIGGERED: Random walk starting");
    int64_t now = esp_timer_get_time() / 1000;

    // Save starting position
    anim_mgr.pet_behavior.walk_start_x = anim_mgr.anim_offset_x;
    anim_mgr.pet_behavior.walk_start_y = anim_mgr.anim_offset_y;

    // Generate random target position
    int32_t rand_x = (int32_t)(esp_random() % (2 * RANDOM_WALK_MAX_OFFSET_X + 1)) - RANDOM_WALK_MAX_OFFSET_X;
    int32_t rand_y = (int32_t)(esp_random() % (2 * RANDOM_WALK_MAX_OFFSET_Y + 1)) - RANDOM_WALK_MAX_OFFSET_Y;
    anim_mgr.pet_behavior.walk_target_x = (int16_t)rand_x;
    anim_mgr.pet_behavior.walk_target_y = (int16_t)rand_y;

    // Set state atomically
    anim_mgr.pet_behavior.behavior_start_time = now;
    anim_mgr.pet_behavior.state = PetBehaviorState::WALKING;

    // Set mirror based on direction
    anim_mgr.anim_mirror_x = (anim_mgr.pet_behavior.walk_target_x > anim_mgr.pet_behavior.walk_start_x);

    animation_switch_to("walk");
    anim_mgr.current_frame = 0;

    ESP_LOGI(TAG, "Pet behavior: IDLE -> WALKING (%d,%d) -> (%d,%d)",
             anim_mgr.pet_behavior.walk_start_x, anim_mgr.pet_behavior.walk_start_y,
             anim_mgr.pet_behavior.walk_target_x, anim_mgr.pet_behavior.walk_target_y);
}

// Start random action
static void pet_behavior_start_action(void) {
    int64_t now = esp_timer_get_time() / 1000;

    // Pick random action
    int action_idx = esp_random() % RANDOM_ACTION_COUNT;
    anim_mgr.pet_behavior.current_action = RANDOM_ACTIONS[action_idx];

    // Set state atomically
    anim_mgr.pet_behavior.behavior_start_time = now;
    anim_mgr.pet_behavior.state = PetBehaviorState::ACTION;

    animation_switch_to(anim_mgr.pet_behavior.current_action);
    anim_mgr.current_frame = 0;

    ESP_LOGI(TAG, "Pet behavior: IDLE -> ACTION (%s)", anim_mgr.pet_behavior.current_action);
}

// Complete current behavior - enter cooldown
static void pet_behavior_complete(void) {
    int64_t now = esp_timer_get_time() / 1000;
    PetBehaviorState old_state = anim_mgr.pet_behavior.state;

    if (old_state == PetBehaviorState::WALKING) {
        // Set final position
        anim_mgr.anim_offset_x = anim_mgr.pet_behavior.walk_target_x;
        anim_mgr.anim_offset_y = anim_mgr.pet_behavior.walk_target_y;
        anim_mgr.anim_mirror_x = false;

        // Update position in PetStateMachine for MCP access
        PetStateMachine::GetInstance().SetPosition(
            anim_mgr.anim_offset_x, anim_mgr.anim_offset_y);

        // Check collision with scene items (skip if in silent mode - off screen)
        if (!anim_mgr.is_sleeping) {
            SceneItemManager::GetInstance().CheckCollision(
                anim_mgr.anim_offset_x, anim_mgr.anim_offset_y);
        }
    }

    // 静默模式特殊处理：行走完成后保持睡眠状态，不切换动画
    if (anim_mgr.is_sleeping) {
        anim_mgr.pet_behavior.state = PetBehaviorState::IDLE;  // 重置行为状态
        animation_switch_to("sleep");  // 保持睡眠动画
        ESP_LOGI(TAG, "Silent mode walk complete, staying off-screen at (%d, %d)",
                 anim_mgr.anim_offset_x, anim_mgr.anim_offset_y);
        return;
    }

    // Enter cooldown to prevent immediate re-trigger
    anim_mgr.pet_behavior.state = PetBehaviorState::COOLDOWN;
    anim_mgr.pet_behavior.cooldown_end_time = now + ANIM_FRAME_INTERVAL_MS + 50;

    // Schedule next behavior
    uint32_t interval = RANDOM_WALK_MIN_INTERVAL_MS +
        (esp_random() % (RANDOM_WALK_MAX_INTERVAL_MS - RANDOM_WALK_MIN_INTERVAL_MS));
    anim_mgr.pet_behavior.next_behavior_time = now + interval;

    animation_switch_to("idle");

    ESP_LOGI(TAG, "Pet behavior: %s -> COOLDOWN, next in %lu ms",
             old_state == PetBehaviorState::WALKING ? "WALKING" : "ACTION", interval);
}

// Interrupt pet behavior (external event)
static void pet_behavior_interrupt(void) {
    if (anim_mgr.pet_behavior.state == PetBehaviorState::WALKING ||
        anim_mgr.pet_behavior.state == PetBehaviorState::ACTION) {
        anim_mgr.anim_mirror_x = false;
        anim_mgr.pet_behavior.state = PetBehaviorState::INTERRUPTED;
        ESP_LOGI(TAG, "Pet behavior: INTERRUPTED");
    }
}

// Resume pet behavior (external event ended)
static void pet_behavior_resume(void) {
    if (anim_mgr.pet_behavior.state == PetBehaviorState::INTERRUPTED) {
        int64_t now = esp_timer_get_time() / 1000;
        uint32_t interval = RANDOM_WALK_MIN_INTERVAL_MS +
            (esp_random() % (RANDOM_WALK_MAX_INTERVAL_MS - RANDOM_WALK_MIN_INTERVAL_MS));
        anim_mgr.pet_behavior.next_behavior_time = now + interval;
        anim_mgr.pet_behavior.state = PetBehaviorState::IDLE;
        ESP_LOGI(TAG, "Pet behavior: INTERRUPTED -> IDLE, next in %lu ms", interval);
    }
}

// =============================================================================
// MCP Move Control - Voice-controlled directional movement
// =============================================================================

// Check if MCP move is allowed (less restrictive than pet_behavior_can_start)
static bool mcp_move_can_start(void) {
    // Block during touch interactions
    if (anim_mgr.touch_active || anim_mgr.swipe_active) return false;

    // Block during sleep
    if (anim_mgr.is_sleeping) return false;

    // Block during PWR walk cooldown
    int64_t now = esp_timer_get_time() / 1000;
    if (now < anim_mgr.pwr_walk_cooldown_until) return false;

    // Block during eating, bathing, or sleeping actions
    auto& pet = PetStateMachine::GetInstance();
    PetAction current_action = pet.GetAction();
    if (current_action == PetAction::kEating ||
        current_action == PetAction::kBathing ||
        current_action == PetAction::kSleeping) {
        return false;
    }

    // ✅ ALLOW during voice interaction (Listening/Speaking/Thinking)
    // This is the key difference from pet_behavior_can_start()
    return true;
}

// Handle MCP move request (called from pet_state.cc via callback)
static bool handle_mcp_move(MoveDirection direction, int16_t distance) {
    // Check if movement is allowed (use MCP-specific check)
    if (!mcp_move_can_start()) {
        ESP_LOGW(TAG, "MCP move: cannot start (busy or conditions not met)");
        return false;
    }

    // If already walking, reject
    if (anim_mgr.pet_behavior.state == PetBehaviorState::WALKING) {
        ESP_LOGW(TAG, "MCP move: already walking");
        return false;
    }

    int64_t now = esp_timer_get_time() / 1000;

    // Save starting position
    anim_mgr.pet_behavior.walk_start_x = anim_mgr.anim_offset_x;
    anim_mgr.pet_behavior.walk_start_y = anim_mgr.anim_offset_y;

    // Calculate target based on direction
    int16_t target_x = anim_mgr.anim_offset_x;
    int16_t target_y = anim_mgr.anim_offset_y;

    switch (direction) {
        case MoveDirection::kUp:
            target_y -= distance;
            break;
        case MoveDirection::kDown:
            target_y += distance;
            break;
        case MoveDirection::kLeft:
            target_x -= distance;
            break;
        case MoveDirection::kRight:
            target_x += distance;
            break;
    }

    // Clamp to valid range
    int16_t original_target_x = target_x;
    int16_t original_target_y = target_y;

    if (target_x < -RANDOM_WALK_MAX_OFFSET_X) target_x = -RANDOM_WALK_MAX_OFFSET_X;
    if (target_x > RANDOM_WALK_MAX_OFFSET_X) target_x = RANDOM_WALK_MAX_OFFSET_X;
    if (target_y < -RANDOM_WALK_MAX_OFFSET_Y) target_y = -RANDOM_WALK_MAX_OFFSET_Y;
    if (target_y > RANDOM_WALK_MAX_OFFSET_Y) target_y = RANDOM_WALK_MAX_OFFSET_Y;

    // Check if already at boundary (no movement after clamping)
    if (target_x == anim_mgr.anim_offset_x && target_y == anim_mgr.anim_offset_y) {
        ESP_LOGW(TAG, "MCP move: already at boundary (current=%d,%d, target=%d,%d, clamped from %d,%d)",
                 anim_mgr.anim_offset_x, anim_mgr.anim_offset_y,
                 target_x, target_y, original_target_x, original_target_y);
        return false;
    }

    ESP_LOGI(TAG, "MCP move: current=(%d,%d), target=(%d,%d)",
             anim_mgr.anim_offset_x, anim_mgr.anim_offset_y, target_x, target_y);

    anim_mgr.pet_behavior.walk_target_x = target_x;
    anim_mgr.pet_behavior.walk_target_y = target_y;

    // Set state atomically
    anim_mgr.pet_behavior.behavior_start_time = now;
    anim_mgr.pet_behavior.state = PetBehaviorState::WALKING;

    // Set mirror based on horizontal direction (face movement direction)
    if (direction == MoveDirection::kLeft) {
        anim_mgr.anim_mirror_x = false;  // Default facing right, so left = no mirror
    } else if (direction == MoveDirection::kRight) {
        anim_mgr.anim_mirror_x = true;   // Right = mirror
    }
    // Up/down: keep current mirror state

    animation_switch_to("walk");
    anim_mgr.current_frame = 0;

    const char* dir_names[] = {"up", "down", "left", "right"};
    ESP_LOGI(TAG, "==> WALK TRIGGERED: MCP move %s, (%d,%d) -> (%d,%d)",
             dir_names[(int)direction],
             anim_mgr.pet_behavior.walk_start_x, anim_mgr.pet_behavior.walk_start_y,
             target_x, target_y);

    return true;
}

// Update walk position (called during WALKING state)
static void pet_behavior_update_walk(int64_t now) {
    int64_t elapsed = now - anim_mgr.pet_behavior.behavior_start_time;

    if (elapsed >= RANDOM_WALK_DURATION_MS) {
        pet_behavior_complete();
    } else {
        // Interpolate position with ease-in-out
        float progress = (float)elapsed / RANDOM_WALK_DURATION_MS;
        float smooth_progress = progress < 0.5f
            ? 2.0f * progress * progress
            : 1.0f - (-2.0f * progress + 2.0f) * (-2.0f * progress + 2.0f) / 2.0f;

        anim_mgr.anim_offset_x = anim_mgr.pet_behavior.walk_start_x +
            (int16_t)((anim_mgr.pet_behavior.walk_target_x - anim_mgr.pet_behavior.walk_start_x) * smooth_progress);
        anim_mgr.anim_offset_y = anim_mgr.pet_behavior.walk_start_y +
            (int16_t)((anim_mgr.pet_behavior.walk_target_y - anim_mgr.pet_behavior.walk_start_y) * smooth_progress);
    }
}

// Main state machine update - replaces check_random_walk/update_random_walk/check_random_action/update_random_action
static void pet_behavior_update(void) {
    int64_t now = esp_timer_get_time() / 1000;

    // 睡眠时只允许行走状态更新（静默模式行走需要继续）
    // 其他行为（IDLE检查新行为、ACTION）在睡眠时不执行
    if (anim_mgr.is_sleeping && anim_mgr.pet_behavior.state != PetBehaviorState::WALKING) {
        return;
    }

    switch (anim_mgr.pet_behavior.state) {
        case PetBehaviorState::IDLE:
            // Check if should start new behavior
            if (pet_behavior_can_start()) {
                // Initialize first behavior time if not set
                if (anim_mgr.pet_behavior.next_behavior_time == 0) {
                    uint32_t initial_delay = RANDOM_WALK_MIN_INTERVAL_MS +
                        (esp_random() % (RANDOM_WALK_MAX_INTERVAL_MS - RANDOM_WALK_MIN_INTERVAL_MS));
                    anim_mgr.pet_behavior.next_behavior_time = now + initial_delay;
                    ESP_LOGI(TAG, "Pet behavior initialized, first in %lu ms", initial_delay);
                    break;
                }

                // Check if time to start
                if (now >= anim_mgr.pet_behavior.next_behavior_time) {
                    // 50/50 chance: walk or action
                    if (esp_random() % 2 == 0) {
                        pet_behavior_start_walk();
                    } else {
                        pet_behavior_start_action();
                    }
                }
            }
            break;

        case PetBehaviorState::WALKING:
            pet_behavior_update_walk(now);
            break;

        case PetBehaviorState::ACTION:
            if (now - anim_mgr.pet_behavior.behavior_start_time >= RANDOM_ACTION_DURATION_MS) {
                pet_behavior_complete();
            }
            break;

        case PetBehaviorState::COOLDOWN:
            if (now >= anim_mgr.pet_behavior.cooldown_end_time) {
                anim_mgr.pet_behavior.state = PetBehaviorState::IDLE;
                ESP_LOGD(TAG, "Pet behavior: COOLDOWN -> IDLE");
            }
            break;

        case PetBehaviorState::INTERRUPTED:
            // Wait for pet_behavior_resume() to be called
            break;
    }
}


// Process touch/swipe for animation triggers
// - Swipe: triggers pet_head animation (petting interaction)
// - Tap: directly toggles chat state (no animation)
// Convert touch screen coordinates (0-280, 0-240) to pet offset coordinates
// Screen center is at (140, 120), pet offset range is [-60, 60] for X, [-15, 15] for Y
static void touch_to_pet_offset(int16_t touch_x, int16_t touch_y, int16_t* offset_x, int16_t* offset_y) {
    // Screen dimensions: 280x240
    const int16_t SCREEN_CENTER_X = 140;
    const int16_t SCREEN_CENTER_Y = 120;

    // Calculate offset from center
    int16_t dx = touch_x - SCREEN_CENTER_X;
    int16_t dy = touch_y - SCREEN_CENTER_Y;

    // Scale to pet offset range with clamping
    // X: map 140 pixels to 60 offset units (ratio ~0.43)
    *offset_x = (int16_t)((dx * RANDOM_WALK_MAX_OFFSET_X) / SCREEN_CENTER_X);
    if (*offset_x < -RANDOM_WALK_MAX_OFFSET_X) *offset_x = -RANDOM_WALK_MAX_OFFSET_X;
    if (*offset_x > RANDOM_WALK_MAX_OFFSET_X) *offset_x = RANDOM_WALK_MAX_OFFSET_X;

    // Y: map 120 pixels to 15 offset units (ratio ~0.125)
    *offset_y = (int16_t)((dy * RANDOM_WALK_MAX_OFFSET_Y) / SCREEN_CENTER_Y);
    if (*offset_y < -RANDOM_WALK_MAX_OFFSET_Y) *offset_y = -RANDOM_WALK_MAX_OFFSET_Y;
    if (*offset_y > RANDOM_WALK_MAX_OFFSET_Y) *offset_y = RANDOM_WALK_MAX_OFFSET_Y;
}

// Start pet walk to specific coordinates (triggered by touch)
static bool pet_walk_to_position(int16_t target_x, int16_t target_y) {
    int64_t now = esp_timer_get_time() / 1000;

    // Check if pet can move (similar to mcp_move_can_start but for touch)
    if (anim_mgr.is_sleeping) {
        ESP_LOGD(TAG, "Pet walk blocked: sleeping");
        return false;
    }

    // If already walking, allow overriding the target
    if (anim_mgr.pet_behavior.state == PetBehaviorState::WALKING) {
        ESP_LOGI(TAG, "Touch walk: updating target while walking");
    }

    // Save starting position
    anim_mgr.pet_behavior.walk_start_x = anim_mgr.anim_offset_x;
    anim_mgr.pet_behavior.walk_start_y = anim_mgr.anim_offset_y;

    // Set touch-specified target
    anim_mgr.pet_behavior.walk_target_x = target_x;
    anim_mgr.pet_behavior.walk_target_y = target_y;

    // Set state atomically
    anim_mgr.pet_behavior.behavior_start_time = now;
    anim_mgr.pet_behavior.state = PetBehaviorState::WALKING;

    // Set mirror based on direction (face movement direction)
    anim_mgr.anim_mirror_x = (target_x > anim_mgr.pet_behavior.walk_start_x);

    animation_switch_to("walk");
    anim_mgr.current_frame = 0;

    ESP_LOGI(TAG, "==> WALK TRIGGERED: Touch walk (%d,%d) -> (%d,%d)",
             anim_mgr.pet_behavior.walk_start_x, anim_mgr.pet_behavior.walk_start_y,
             target_x, target_y);

    return true;
}

static void process_touch_swipe(void) {
    if (!touch_state.initialized || touch_state.handle == nullptr) {
        static bool warned = false;
        if (!warned) {
            ESP_LOGW(TAG, "Touch not initialized (init=%d, handle=%p)",
                     touch_state.initialized, touch_state.handle);
            warned = true;
        }
        return;
    }

    esp_lcd_touch_point_data_t touch_data[1];
    uint8_t touch_cnt = 0;

    // Read touch data (ignore errors, just skip this cycle)
    if (esp_lcd_touch_read_data(touch_state.handle) != ESP_OK) {
        return;
    }

    esp_err_t err = esp_lcd_touch_get_data(
        touch_state.handle, touch_data, &touch_cnt, 1);

    if (err == ESP_OK && touch_cnt > 0) {
        int16_t x = touch_data[0].x;
        int16_t y = touch_data[0].y;

        if (!touch_state.tracking) {
            // Start tracking new touch
            touch_state.start_x = x;
            touch_state.start_y = y;
            touch_state.tracking = true;
        }

        // Update last known position
        touch_state.last_x = x;
        touch_state.last_y = y;
    } else {
        // Touch released - trigger pet walk to touch position
        if (touch_state.tracking) {
            // Convert touch coordinates to pet offset
            int16_t target_offset_x, target_offset_y;
            touch_to_pet_offset(touch_state.last_x, touch_state.last_y,
                              &target_offset_x, &target_offset_y);

            // Start pet walk to target position
            pet_walk_to_position(target_offset_x, target_offset_y);

            ESP_LOGI(TAG, "Touch released at (%d, %d) -> pet offset (%d, %d)",
                     touch_state.last_x, touch_state.last_y,
                     target_offset_x, target_offset_y);

            touch_state.tracking = false;
        }
    }
}

class CustomLcdDisplay : public SpiLcdDisplay {
public:
    CustomLcdDisplay(esp_lcd_panel_io_handle_t io_handle,
                    esp_lcd_panel_handle_t panel_handle,
                    int width,
                    int height,
                    int offset_x,
                    int offset_y,
                    bool mirror_x,
                    bool mirror_y,
                    bool swap_xy)
        : SpiLcdDisplay(io_handle, panel_handle,
                    width, height, offset_x, offset_y, mirror_x, mirror_y, swap_xy) {
        // Store LCD panel handle for direct output mode (low memory fallback)
        direct_lcd_panel = panel_handle;

        DisplayLockGuard lock(this);
        lv_obj_set_style_pad_left(status_bar_, LV_HOR_RES * 0.1, 0);
        lv_obj_set_style_pad_right(status_bar_, LV_HOR_RES * 0.1, 0);

        // Move status text labels up (closer to top edge)
        // WiFi and battery icons are at the edge, but status text has room to move up
        if (status_label_ != nullptr) {
            lv_obj_set_style_translate_y(status_label_, -8, 0);  // Move up 8px
        }
        if (notification_label_ != nullptr) {
            lv_obj_set_style_translate_y(notification_label_, -8, 0);  // Move up 8px
        }

        // NOTE: AnimationLoader initialization is deferred to first use
        // This allows audio codec to initialize first, reducing peak memory usage

        // Initialize LVGL frame descriptor for composite output (RGB565)
        // Exclude bottom UI area (30px) to preserve LVGL-drawn bottom bar
        anim_mgr.frame_dsc = {
            .header = {
                .magic = LV_IMAGE_HEADER_MAGIC,
                .cf = LV_COLOR_FORMAT_RGB565,
                .flags = 0,
                .w = COMPOSITE_WIDTH,     // 280
                .h = COMPOSITE_HEIGHT - BOTTOM_UI_HEIGHT,    // 210 (240 - 30)
                .stride = (uint32_t)(COMPOSITE_WIDTH * 2),  // 2 bytes per pixel for RGB565
                .reserved_2 = 0,
            },
            .data_size = (uint32_t)(COMPOSITE_WIDTH * (COMPOSITE_HEIGHT - BOTTOM_UI_HEIGHT) * 2),
            .data = nullptr,  // Will be set after first composite
        };

        // Create background images at the BOTTOM of the z-order
        // Use LV_OBJ_FLAG_FLOATING to prevent them from affecting layout
        lv_obj_t* screen = lv_screen_active();

        // Log current child count for debugging
        uint32_t initial_child_cnt = lv_obj_get_child_count(screen);
        ESP_LOGI(TAG, "Screen has %lu children before creating bg images", (unsigned long)initial_child_cnt);

        // Create static background image (will be bottom-most after reordering)
        anim_mgr.static_bg_image = lv_image_create(screen);
        lv_obj_add_flag(anim_mgr.static_bg_image, LV_OBJ_FLAG_FLOATING);  // Don't affect layout
        lv_obj_align(anim_mgr.static_bg_image, LV_ALIGN_CENTER, 0, 0);
        lv_image_set_scale(anim_mgr.static_bg_image, 256);
        lv_obj_add_flag(anim_mgr.static_bg_image, LV_OBJ_FLAG_HIDDEN);  // Hidden until background loaded

        // Create animation/composite image
        // This displays the 280x240 composite (background + animation)
        anim_mgr.bg_image = lv_image_create(screen);
        lv_obj_add_flag(anim_mgr.bg_image, LV_OBJ_FLAG_FLOATING);  // Don't affect layout

        // CRITICAL: Move both images to the bottom of the z-order
        // Order of move operations matters: last moved to index 0 will be at very bottom
        // We want: [static_bg(0), bg_image(1), ...UI elements...]
        lv_obj_move_to_index(anim_mgr.bg_image, 0);         // bg_image goes to bottom first
        lv_obj_move_to_index(anim_mgr.static_bg_image, 0);  // static_bg pushes bg_image to index 1

        ESP_LOGI(TAG, "Background images created and moved to bottom of z-order");

        // Set initial frame (hidden until animation data is loaded)
        lv_image_set_src(anim_mgr.bg_image, &anim_mgr.frame_dsc);

        // Position at top of screen (full 280x240)
        lv_obj_set_pos(anim_mgr.bg_image, 0, COMPOSITE_SCREEN_Y);

        // Use original pixel size (no scaling)
        // LVGL scale: 256 = 1.0x
        lv_image_set_scale(anim_mgr.bg_image, 256);

        lv_obj_add_flag(anim_mgr.bg_image, LV_OBJ_FLAG_HIDDEN);  // Hide until animation loaded

        // Store UI element pointers for deferred transparency (applied after animation loads)
        // This ensures original UI is visible during network configuration
        anim_mgr.container = container_;
        anim_mgr.content = content_;
        anim_mgr.top_bar = top_bar_;
        anim_mgr.bottom_bar = bottom_bar_;
        anim_mgr.chat_message_label = chat_message_label_;
        anim_mgr.status_label = status_label_;
        anim_mgr.notification_label = notification_label_;
        anim_mgr.network_label = network_label_;
        anim_mgr.mute_label = mute_label_;
        anim_mgr.battery_label = battery_label_;
        anim_mgr.pet_status_container = pet_status_container_;

        // Configure chat message label for single-line scrolling display
        if (chat_message_label_ != nullptr) {
            // Set to single line with horizontal scroll (marquee style)
            lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
            lv_obj_set_style_text_align(chat_message_label_, LV_TEXT_ALIGN_LEFT, 0);
            // Limit height to single line
            auto text_font = lv_obj_get_style_text_font(chat_message_label_, 0);
            if (text_font != nullptr) {
                lv_obj_set_height(chat_message_label_, lv_font_get_line_height(text_font));
            }
        }

        // Adjust top bar for this display
        // Keep bar in place to cover black area, but adjust content alignment
        if (top_bar_ != nullptr) {
            // Set fixed height to cover top black area (bg_offset_y = 25)
            lv_obj_set_height(top_bar_, 25);  // Match bg_offset_y to fully cover black area
            lv_obj_set_style_pad_top(top_bar_, 2, 0);   // Small top padding
            lv_obj_set_style_pad_bottom(top_bar_, 0, 0);  // Content at bottom of bar
            // Move WiFi and battery icons toward center
            lv_obj_set_style_pad_left(top_bar_, 18, 0);
            lv_obj_set_style_pad_right(top_bar_, 18, 0);

            // Set a semi-transparent background initially (uses same color as bottom bar)
            lv_obj_set_style_bg_opa(top_bar_, LV_OPA_70, 0);
            lv_obj_set_style_bg_color(top_bar_, rgb565_to_lv_color(bottom_bar_bg_color_rgb565), 0);
        }

        // Set white text color for status elements
        if (status_label_ != nullptr) {
            lv_obj_set_style_text_color(status_label_, lv_color_hex(0xFFFFFF), 0);
            ESP_LOGI(TAG, "Status label initialized with white text");
        }
        if (notification_label_ != nullptr) {
            lv_obj_set_style_text_color(notification_label_, lv_color_hex(0xFFFFFF), 0);
        }

        // Adjust top bar icon positions
        if (top_bar_ != nullptr) {
            // Move WiFi icon up
            if (network_label_ != nullptr) {
                lv_obj_set_style_translate_y(network_label_, -5, 0);  // Move up 5px
            }
            // Move battery icon up
            if (battery_label_ != nullptr) {
                lv_obj_set_style_translate_y(battery_label_, -5, 0);  // Move up 5px
            }
            // Move mute icon up (if exists)
            if (mute_label_ != nullptr) {
                lv_obj_set_style_translate_y(mute_label_, -5, 0);  // Move up 5px
            }
        }
        // Keep bottom bar at default position with fixed height to cover black area
        // Bottom bar aligned to screen bottom
        if (bottom_bar_ != nullptr) {
            lv_obj_set_height(bottom_bar_, BOTTOM_UI_HEIGHT);  // Fixed height 25px
            lv_obj_align(bottom_bar_, LV_ALIGN_BOTTOM_MID, 0, 0);  // Align to screen bottom

            // Set a semi-transparent background initially (color will be updated when background loads)
            lv_obj_set_style_bg_opa(bottom_bar_, LV_OPA_70, 0);
            lv_obj_set_style_bg_color(bottom_bar_, rgb565_to_lv_color(bottom_bar_bg_color_rgb565), 0);

            // Change bottom bar to flex layout to accommodate pet status icons
            lv_obj_set_flex_flow(bottom_bar_, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(bottom_bar_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        }

        // Create pet status display container in bottom bar
        if (bottom_bar_ != nullptr) {
            pet_status_container_ = PetStatusDisplay::Create(bottom_bar_);
            if (pet_status_container_ != nullptr) {
                ESP_LOGI(TAG, "Pet status display created in bottom bar");
                // Ensure it's visible
                lv_obj_remove_flag(pet_status_container_, LV_OBJ_FLAG_HIDDEN);
            }
        }

        // Set initial text for bottom bar to make it visible
        if (chat_message_label_ != nullptr) {
            lv_label_set_text(chat_message_label_, "小智 AI");
            lv_obj_set_style_text_color(chat_message_label_, lv_color_hex(0xFFFFFF), 0);
            // Make the chat label grow to take remaining space
            lv_obj_set_flex_grow(chat_message_label_, 1);
            lv_obj_set_style_text_align(chat_message_label_, LV_TEXT_ALIGN_CENTER, 0);
            ESP_LOGI(TAG, "Bottom bar initialized with pet status and chat message");
        }

        // NOTE: Top/bottom bar background images are created in init_static_background()
        // after buffers are allocated (deferred initialization)

        // Hide emoji display (replaced by animation background)
        if (emoji_label_ != nullptr) {
            lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
        }
        if (emoji_image_ != nullptr) {
            lv_obj_add_flag(emoji_image_, LV_OBJ_FLAG_HIDDEN);
        }
        if (emoji_box_ != nullptr) {
            lv_obj_add_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
        }

        // Initialize activity time
        anim_mgr.last_activity_time = esp_timer_get_time() / 1000;

        // NOTE: init_static_background() is deferred to animation_update_frame()
        // This allows WiFi to initialize first, as background buffers need ~131KB RAM

        ESP_LOGI(TAG, "Animation system initialized (deferred background loading)");
    }

    void StartAnimation() {
        if (anim_mgr.timer != nullptr) {
            ESP_LOGI(TAG, "Animation timer already started");
            return;
        }

        ESP_LOGI(TAG, "Starting animation timer...");
        esp_timer_create_args_t timer_args = {
            .callback = animation_timer_callback,
            .arg = nullptr,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "anim_timer",
            .skip_unhandled_events = true,
        };
        ESP_ERROR_CHECK(esp_timer_create(&timer_args, &anim_mgr.timer));
        ESP_ERROR_CHECK(esp_timer_start_periodic(anim_mgr.timer, ANIM_FRAME_INTERVAL_MS * 1000));

        // Register MCP move callback for voice-controlled movement
        PetStateMachine::GetInstance().SetMoveCallback(handle_mcp_move);
        ESP_LOGI(TAG, "MCP move callback registered");

        ESP_LOGI(TAG, "Animation timer started (%d ms interval)", ANIM_FRAME_INTERVAL_MS);
    }

    // Override SetPetStatus to update the pet status display
    virtual void SetPetStatus(const PetStats& stats, uint8_t coins = 0) override {
        // Call base class implementation for logging
        SpiLcdDisplay::SetPetStatus(stats, coins);

        // Update the pet status display if it exists
        if (pet_status_container_ != nullptr) {
            DisplayLockGuard lock(this);
            PetStatusDisplay::Update(pet_status_container_, stats, coins);
            ESP_LOGD(TAG, "Pet status updated: H=%d Coins=%d C=%d HP=%d",
                     stats.hunger, coins, stats.cleanliness, stats.happiness);
        }
    }

    // Override SetTheme to maintain transparent UI and white text (only if animation loaded)
    virtual void SetTheme(Theme* theme) override {
        SpiLcdDisplay::SetTheme(theme);

        // Only apply animation style if animation has been loaded
        // This preserves the original theme during network configuration
        if (anim_mgr.ui_transparent) {
            DisplayLockGuard lock(this);

            // Keep screen background black for white text visibility
            lv_obj_t* screen = lv_screen_active();
            if (screen != nullptr) {
                lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
            }

            // Make all backgrounds transparent (animation shows through)
            if (container_ != nullptr) {
                lv_obj_set_style_bg_opa(container_, LV_OPA_TRANSP, 0);
            }
            if (content_ != nullptr) {
                lv_obj_set_style_bg_opa(content_, LV_OPA_TRANSP, 0);
            }
            // Top and bottom bars - semi-transparent with sampled background colors
            if (top_bar_ != nullptr) {
                lv_obj_set_style_bg_color(top_bar_, rgb565_to_lv_color(bottom_bar_bg_color_rgb565), 0);
                lv_obj_set_style_bg_opa(top_bar_, LV_OPA_70, 0);
            }
            if (bottom_bar_ != nullptr) {
                lv_obj_set_style_bg_color(bottom_bar_, rgb565_to_lv_color(bottom_bar_bg_color_rgb565), 0);
                lv_obj_set_style_bg_opa(bottom_bar_, LV_OPA_70, 0);
            }

            // Force all text to white for better visibility on animated background
            lv_color_t white = lv_color_hex(0xFFFFFF);
            if (chat_message_label_ != nullptr) {
                lv_obj_set_style_text_color(chat_message_label_, white, 0);
            }
            if (status_label_ != nullptr) {
                lv_obj_set_style_text_color(status_label_, white, 0);
            }
            if (notification_label_ != nullptr) {
                lv_obj_set_style_text_color(notification_label_, white, 0);
            }
            if (network_label_ != nullptr) {
                lv_obj_set_style_text_color(network_label_, white, 0);
            }
            if (mute_label_ != nullptr) {
                lv_obj_set_style_text_color(mute_label_, white, 0);
            }
            if (battery_label_ != nullptr) {
                lv_obj_set_style_text_color(battery_label_, white, 0);
            }
        }
    }

    // Override SetEmotion to use AnimationLoader instead of emoji_collection
    // Only switch animation for known emotions to avoid overriding state animations
    virtual void SetEmotion(const char* emotion) override {
        if (emotion == nullptr) {
            return;
        }

        // Print emotion value for lighting control debugging
        ESP_LOGI(TAG, ">>> SetEmotion: [%s] <<<", emotion);

        // Only process emotions that have valid mappings
        // Ignore unknown emotions (like "happy", "sad", etc.) to prevent
        // overriding the current state animation (speaking/listening/idle)
        static const char* known_emotions[] = {
            "idle", "neutral", "standby",
            "listening", "speaking", "talking",
            "listen", "talk",  // PetState action animations
            "eat", "bath", "sleep", "walk",  // Pet behavior animations
            nullptr
        };

        bool is_known = false;
        for (int i = 0; known_emotions[i] != nullptr; i++) {
            if (strcmp(emotion, known_emotions[i]) == 0) {
                is_known = true;
                break;
            }
        }

        if (!is_known) {
            ESP_LOGD(TAG, "SetEmotion: unknown emotion '%s' (animation ignored, can use for lighting)", emotion);
            return;
        }

        // Check if this is an idle-type emotion
        bool is_idle_emotion = (strcmp(emotion, "idle") == 0 ||
                                strcmp(emotion, "neutral") == 0 ||
                                strcmp(emotion, "standby") == 0);

        // Don't switch to idle if pet behavior is active
        if (is_idle_emotion) {
            if (anim_mgr.pet_behavior.state == PetBehaviorState::WALKING ||
                anim_mgr.pet_behavior.state == PetBehaviorState::ACTION) {
                ESP_LOGD(TAG, "SetEmotion(%s) ignored - pet behavior active", emotion);
                return;
            }
            // Resume if was interrupted
            if (anim_mgr.pet_behavior.state == PetBehaviorState::INTERRUPTED) {
                pet_behavior_resume();
            }
        } else {
            // For non-idle emotions (listen/talk), interrupt pet behavior
            pet_behavior_interrupt();
        }

        animation_switch_to(emotion);
    }

    // Override SetStatus to trigger animation changes and interjection timer
    virtual void SetStatus(const char* status) override {
        // Call parent implementation for UI updates
        SpiLcdDisplay::SetStatus(status);

        if (status == nullptr) {
            return;
        }

        ESP_LOGI(TAG, "SetStatus: %s", status);

        // Check if AnimationLoader is ready before switching animations
        auto& loader = AnimationLoader::GetInstance();
        bool can_switch_anim = loader.IsInitialized();

        // Handle animation switching based on status
        if (std::strcmp(status, Lang::Strings::LISTENING) == 0) {
            // Wake from sleep for user interaction
            if (anim_mgr.is_sleeping) {
                anim_mgr.is_sleeping = false;
                anim_mgr.sleep_start_time = 0;
                ESP_LOGI(TAG, "Woken from sleep by user interaction (LISTENING)");
            }
            // Reset activity timer on actual user interaction
            anim_mgr.last_activity_time = esp_timer_get_time() / 1000;
            ESP_LOGI(TAG, "User interaction detected, activity timer reset");
            // 聆听状态：播放聆听动画，中止宠物行为
            pet_behavior_interrupt();
            if (can_switch_anim) {
                animation_switch_to("listen");
            }
        } else if (std::strcmp(status, Lang::Strings::STANDBY) == 0) {
            // 睡眠状态优先：不要打断睡眠，让自动唤醒机制生效
            if (anim_mgr.is_sleeping) {
                ESP_LOGD(TAG, "Standby status ignored - sleeping (auto-wake in progress)");
                return;  // 睡眠时不处理STANDBY状态切换
            }

            // 吃饭/洗澡状态：不要打断持续性动作
            auto& pet = PetStateMachine::GetInstance();
            PetAction current_action = pet.GetAction();
            if (current_action == PetAction::kEating ||
                current_action == PetAction::kBathing) {
                ESP_LOGD(TAG, "Standby status ignored - eating/bathing active");
                return;  // 吃饭/洗澡时不切换到idle
            }

            // 待机状态：如果宠物行为正在进行，不切换动画
            if (anim_mgr.pet_behavior.state == PetBehaviorState::WALKING ||
                anim_mgr.pet_behavior.state == PetBehaviorState::ACTION) {
                ESP_LOGD(TAG, "Standby status ignored - pet behavior active");
                // Don't switch animation, let pet behavior continue
            } else {
                // Resume if was interrupted, then switch to idle
                if (anim_mgr.pet_behavior.state == PetBehaviorState::INTERRUPTED) {
                    pet_behavior_resume();
                }
                if (can_switch_anim) {
                    animation_switch_to("idle");
                }
            }
        } else if (std::strcmp(status, Lang::Strings::SPEAKING) == 0) {
            // Wake from sleep for user interaction
            if (anim_mgr.is_sleeping) {
                anim_mgr.is_sleeping = false;
                anim_mgr.sleep_start_time = 0;
                ESP_LOGI(TAG, "Woken from sleep by user interaction (SPEAKING)");
            }
            // Reset activity timer for speaking (conversation)
            anim_mgr.last_activity_time = esp_timer_get_time() / 1000;
            // 讲话状态：播放讲话动画，中止宠物行为
            pet_behavior_interrupt();
            if (can_switch_anim) {
                animation_switch_to("talk");
            }
        }
    }
};

class CustomButton: public Button {
public:
    using Button::Button;  // 继承父类构造函数

    void OnPressDownDel(void) {
        if (button_handle_ == nullptr) {
            return;
        }
        on_press_down_ = NULL;
        iot_button_unregister_cb(button_handle_, BUTTON_PRESS_DOWN, nullptr);
    }
    void OnPressUpDel(void) {
        if (button_handle_ == nullptr) {
            return;
        }
        on_press_up_ = NULL;
        iot_button_unregister_cb(button_handle_, BUTTON_PRESS_UP, nullptr);
    }
};

class CustomBoard : public WifiBoard {
private:
    CustomButton boot_button_;
    CustomButton pwr_button_;
    i2c_master_bus_handle_t i2c_bus_;
    CustomLcdDisplay* display_ = nullptr;
    PowerManager* power_manager_ = nullptr;
    PowerSaveTimer* power_save_timer_ = nullptr;

    // PWR按键长按状态管理
    esp_timer_handle_t pwr_hold_timer_ = nullptr;
    int64_t pwr_press_start_time_ = 0;
    bool pwr_long_press_active_ = false;
    int shutdown_countdown_ = 3;

    void InitializePowerManager() {
        power_manager_ = new PowerManager(BATTERY_CHARGING_PIN, BATTERY_ADC_PIN, BATTERY_EN_PIN);
        power_manager_->PowerON();
    }

    void InitializePowerSaveTimer() {
        power_save_timer_ = new PowerSaveTimer(-1, 60, 300);
        power_save_timer_->OnEnterSleepMode([this]() {
            GetDisplay()->SetPowerSaveMode(true);
        });
        power_save_timer_->OnExitSleepMode([this]() {
            GetDisplay()->SetPowerSaveMode(false);
        });
        power_save_timer_->OnShutdownRequest([this]() {
            power_manager_->PowerOff();
        });
        power_save_timer_->SetEnabled(true);
    }

    void InitializeI2c() {
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = (i2c_port_t)I2C_NUM_0,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));
    }

    void InitializeSpi() {
        ESP_LOGI(TAG, "Initialize QSPI bus");
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_MOSI_PIN;
        buscfg.miso_io_num = DISPLAY_MISO_PIN;
        buscfg.sclk_io_num = DISPLAY_CLK_PIN;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeLcdDisplay() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;

        ESP_LOGI(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
        io_config.spi_mode = DISPLAY_SPI_MODE;
        io_config.pclk_hz = 40 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI2_HOST, &io_config, &panel_io));

        ESP_LOGI(TAG, "Install LCD driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RST_PIN;
        panel_config.rgb_ele_order = DISPLAY_RGB_ORDER;
        panel_config.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));
        esp_lcd_panel_reset(panel);
        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);

        display_ = new CustomLcdDisplay(
            panel_io,
            panel,
            DISPLAY_WIDTH,
            DISPLAY_HEIGHT,
            DISPLAY_OFFSET_X,
            DISPLAY_OFFSET_Y,
            DISPLAY_MIRROR_X,
            DISPLAY_MIRROR_Y,
            DISPLAY_SWAP_XY
        );
    }

    void InitializeTouch() {
#ifdef DISPLAY_TOUCH_INT_PIN
        ESP_LOGI(TAG, "Initialize touch controller CST816");

        esp_lcd_touch_config_t tp_cfg = {
            .x_max = DISPLAY_WIDTH - 1,
            .y_max = DISPLAY_HEIGHT - 1,
            .rst_gpio_num = DISPLAY_TOUCH_RST_PIN,
            .int_gpio_num = DISPLAY_TOUCH_INT_PIN,
            .levels = {
                .reset = 0,
                .interrupt = 0,
            },
            .flags = {
                .swap_xy = DISPLAY_SWAP_XY ? 1 : 0,
                .mirror_x = DISPLAY_MIRROR_X ? 1 : 0,
                .mirror_y = DISPLAY_MIRROR_Y ? 1 : 0,
            },
        };

        esp_lcd_panel_io_handle_t tp_io_handle = nullptr;
        esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_CST816S_CONFIG();
        tp_io_config.scl_speed_hz = 400 * 1000;

        esp_err_t ret = esp_lcd_new_panel_io_i2c(i2c_bus_, &tp_io_config, &tp_io_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create touch panel IO: %s", esp_err_to_name(ret));
            return;
        }

        ret = esp_lcd_touch_new_i2c_cst816s(tp_io_handle, &tp_cfg, &touch_state.handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize touch controller: %s", esp_err_to_name(ret));
            return;
        }

        touch_state.initialized = true;
        ESP_LOGI(TAG, "Touch panel initialized successfully (handle=%p)", touch_state.handle);
#else
        ESP_LOGW(TAG, "Touch screen not configured (DISPLAY_TOUCH_INT_PIN not defined)");
#endif
    }

    // PWR按键长按定时器回调
    static void PwrHoldTimerCallback(void* arg) {
        CustomBoard* board = static_cast<CustomBoard*>(arg);
        board->HandlePwrHoldTimer();
    }

    // 关机任务函数（在独立任务中执行关机操作）
    static void ShutdownTask(void* arg) {
        CustomBoard* board = static_cast<CustomBoard*>(arg);
        ESP_LOGI(TAG, "关机任务开始执行");

        board->GetDisplay()->ShowNotification("正在关机...");
        vTaskDelay(pdMS_TO_TICKS(500));  // 等待显示

        ESP_LOGI(TAG, "调用 PowerOff()");
        if (board->power_manager_ != nullptr) {
            board->power_manager_->PowerOff();
        }

        vTaskDelay(pdMS_TO_TICKS(200));  // 等待电源关闭

        // 禁用所有唤醒源
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

        ESP_LOGI(TAG, "进入深度睡眠...");
        esp_deep_sleep_start();

        // 如果深度睡眠失败（不应该到达这里），尝试重启
        ESP_LOGE(TAG, "深度睡眠失败，执行重启");
        esp_restart();
    }

    void HandlePwrHoldTimer() {
        int64_t hold_time_ms = (esp_timer_get_time() - pwr_press_start_time_) / 1000;

        ESP_LOGI(TAG, "PWR hold time: %ld ms", (long)hold_time_ms);

        // 计算动画完全移出屏幕左侧需要的偏移量
        // offset_x = -(ANIM_OFFSET_IN_COMPOSITE_X + ANIM_SCALED_WIDTH) = -(36 + 208) = -244
        const int16_t WALK_OFF_TARGET_X = -(ANIM_OFFSET_IN_COMPOSITE_X + ANIM_SCALED_WIDTH);

        // ===== PWR长按3秒逻辑 =====
        if (hold_time_ms >= 3000 && !pwr_long_press_active_) {
            pwr_long_press_active_ = true;

            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                return;  // 启动中不处理
            }

            if (anim_mgr.is_sleeping) {
                // 静默模式下：行走回到中心，退出静默模式
                int64_t now = esp_timer_get_time() / 1000;

                // 设置行走参数：从当前位置走回中心
                anim_mgr.pet_behavior.walk_start_x = anim_mgr.anim_offset_x;
                anim_mgr.pet_behavior.walk_start_y = anim_mgr.anim_offset_y;
                anim_mgr.pet_behavior.walk_target_x = 0;
                anim_mgr.pet_behavior.walk_target_y = 0;
                anim_mgr.pet_behavior.behavior_start_time = now;
                anim_mgr.pet_behavior.state = PetBehaviorState::WALKING;

                // 面朝右（向中心走）
                anim_mgr.anim_mirror_x = true;
                animation_switch_to("walk");

                // 退出静默模式
                anim_mgr.is_sleeping = false;
                app.GetAudioService().EnableWakeWordDetection(true);

                ESP_LOGI(TAG, "==> WALK TRIGGERED: PWR长按3秒（静默模式）- 行走回到中心并退出静默模式，从(%d,%d)到(0,0)",
                         anim_mgr.pet_behavior.walk_start_x, anim_mgr.pet_behavior.walk_start_y);
                GetDisplay()->ShowNotification("欢迎回来");
            } else {
                // 正常模式：进入静默模式并行走退出屏幕
                int64_t now = esp_timer_get_time() / 1000;

                // 如果正在对话或监听，先停止
                auto state = app.GetDeviceState();
                if (state == kDeviceStateListening || state == kDeviceStateSpeaking ||
                    state == kDeviceStateConnecting) {
                    app.AbortSpeaking(kAbortReasonNone);
                    app.SetDeviceState(kDeviceStateIdle);
                }

                // 设置行走参数：从当前位置走到屏幕外
                anim_mgr.pet_behavior.walk_start_x = anim_mgr.anim_offset_x;
                anim_mgr.pet_behavior.walk_start_y = anim_mgr.anim_offset_y;
                anim_mgr.pet_behavior.walk_target_x = WALK_OFF_TARGET_X;
                anim_mgr.pet_behavior.walk_target_y = 0;
                anim_mgr.pet_behavior.behavior_start_time = now;
                anim_mgr.pet_behavior.state = PetBehaviorState::WALKING;

                // 面朝左（向左走出屏幕）
                anim_mgr.anim_mirror_x = false;
                animation_switch_to("walk");

                // 进入静默模式
                anim_mgr.is_sleeping = true;
                app.GetAudioService().EnableWakeWordDetection(false);

                ESP_LOGI(TAG, "==> WALK TRIGGERED: PWR长按3秒 - 进入静默模式并行走退出屏幕，从(%d,%d)到(%d,0)",
                         anim_mgr.pet_behavior.walk_start_x, anim_mgr.pet_behavior.walk_start_y,
                         WALK_OFF_TARGET_X);
                GetDisplay()->ShowNotification("静默模式");
            }

            // 停止定时器
            StopPwrHoldTimer();
        }
    }

    void StartPwrHoldTimer() {
        if (pwr_hold_timer_ == nullptr) {
            esp_timer_create_args_t timer_args = {
                .callback = PwrHoldTimerCallback,
                .arg = this,
                .dispatch_method = ESP_TIMER_TASK,
                .name = "pwr_hold_timer",
                .skip_unhandled_events = true
            };
            esp_timer_create(&timer_args, &pwr_hold_timer_);
        }

        pwr_press_start_time_ = esp_timer_get_time();
        pwr_long_press_active_ = false;
        shutdown_countdown_ = 3;

        // Abort any active pet behavior and set cooldown (1 second buffer)
        int64_t now = esp_timer_get_time() / 1000;
        anim_mgr.pwr_walk_cooldown_until = now + 1000;  // 1 second cooldown starts now
        if (anim_mgr.pet_behavior.state == PetBehaviorState::WALKING) {
            // Reset to start position for walk
            anim_mgr.anim_offset_x = anim_mgr.pet_behavior.walk_start_x;
            anim_mgr.anim_offset_y = anim_mgr.pet_behavior.walk_start_y;
            anim_mgr.anim_mirror_x = false;
            anim_mgr.pet_behavior.state = PetBehaviorState::IDLE;
            animation_switch_to("idle");
            ESP_LOGI(TAG, "PWR按下，中止宠物行为 (walk)");
        } else if (anim_mgr.pet_behavior.state == PetBehaviorState::ACTION) {
            anim_mgr.pet_behavior.state = PetBehaviorState::IDLE;
            animation_switch_to("idle");
            ESP_LOGI(TAG, "PWR按下，中止宠物行为 (action)");
        }

        // 每100ms检查一次
        esp_timer_start_periodic(pwr_hold_timer_, 100 * 1000);
        ESP_LOGI(TAG, "PWR按键按下，开始计时");
    }

    void StopPwrHoldTimer() {
        if (pwr_hold_timer_ != nullptr) {
            esp_timer_stop(pwr_hold_timer_);
        }

        pwr_long_press_active_ = false;
        ESP_LOGD(TAG, "PWR按键松开，停止计时");
    }

    void InitializeButtons() {
        // BOOT按键：手动切换背景
        boot_button_.OnClick([this]() {
            static uint16_t manual_bg_idx = 0;
            manual_bg_idx = (manual_bg_idx + 1) % BG_COUNT;

            auto& bg_mgr = BackgroundManager::GetInstance();
            bg_mgr.ForceBackground(manual_bg_idx);

            // 强制刷新背景 (传入true绕过60秒限制)
            check_and_update_background(true);

            ESP_LOGI(TAG, "BOOT按键：切换背景到 %d", manual_bg_idx);
        });

        boot_button_.OnLongPress([this]() {
            // 长按清除强制背景，恢复自动切换
            auto& bg_mgr = BackgroundManager::GetInstance();
            bg_mgr.ClearForce();
            check_and_update_background(true);  // 强制更新
            ESP_LOGI(TAG, "BOOT长按：恢复自动背景切换");
        });

        boot_button_.OnPressUp([this]() {
            // 无操作
        });

        // PWR按键：单击=切换聊天，长按3秒=静默模式切换
        pwr_button_.OnPressDown([this]() {
            StartPwrHoldTimer();
        });

        pwr_button_.OnPressUp([this]() {
            StopPwrHoldTimer();
        });

        pwr_button_.OnClick([this]() {
            int64_t press_duration = (esp_timer_get_time() - pwr_press_start_time_) / 1000;
            ESP_LOGI(TAG, "PWR单击触发 (按压时长: %ld ms)", (long)press_duration);

            power_save_timer_->WakeUp();
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                ESP_LOGI(TAG, "设备启动中，忽略PWR单击");
                return;
            }
            ESP_LOGI(TAG, "PWR单击，切换聆听状态");
            app.ToggleChatState();
        });

        // PWR按键三击：重置WiFi
        pwr_button_.OnMultipleClick([this]() {
            ESP_LOGI(TAG, "PWR三击：重置WiFi");
            power_save_timer_->WakeUp();
            EnterWifiConfigMode();
        }, 3);

        // PWR按键双击：息屏
        pwr_button_.OnDoubleClick([this]() {
            auto backlight = Board::GetInstance().GetBacklight();
            backlight->SetBrightness(0);
            ESP_LOGI(TAG, "PWR双击，息屏");
        });
    }

public:
    CustomBoard() :
        boot_button_(BOOT_BUTTON_GPIO, false, 1000), pwr_button_(PWR_BUTTON_GPIO) {
        InitializePowerManager();
        InitializePowerSaveTimer();
        InitializeI2c();
        InitializeSpi();
        InitializeLcdDisplay();

        // Temporarily enable touch logs for initialization only
        esp_log_level_set("i2c.master", ESP_LOG_WARN);
        esp_log_level_set("lcd_panel.io.i2c", ESP_LOG_WARN);
        esp_log_level_set("CST816S", ESP_LOG_INFO);

        InitializeTouch();

        // Completely suppress I2C touch errors after initialization
        // CST816S goes to sleep when not touched, causing normal NACK errors every 167ms
        // These are expected behavior, not real errors - safe to ignore completely
        esp_log_level_set("i2c.master", ESP_LOG_NONE);
        esp_log_level_set("lcd_panel.io.i2c", ESP_LOG_NONE);
        esp_log_level_set("CST816S", ESP_LOG_NONE);

        InitializeButtons();
        GetBacklight()->RestoreBrightness();
        display_->StartAnimation();
    }

    virtual AudioCodec* GetAudioCodec() override {
        static Es8311AudioCodec audio_codec(
            i2c_bus_,
            I2C_NUM_0,
            AUDIO_INPUT_SAMPLE_RATE,
            AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK,
            AUDIO_I2S_GPIO_BCLK,
            AUDIO_I2S_GPIO_WS,
            AUDIO_I2S_GPIO_DOUT,
            AUDIO_I2S_GPIO_DIN,
            AUDIO_CODEC_PA_PIN,
            AUDIO_CODEC_ES8311_ADDR
        );
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }

    virtual bool GetBatteryLevel(int &level, bool& charging, bool& discharging) override {
        static bool last_discharging = false;
        charging = power_manager_->IsCharging();
        discharging = power_manager_->IsDischarging();
        if (discharging != last_discharging) {
            power_save_timer_->SetEnabled(discharging);
            last_discharging = discharging;
        }
        level = power_manager_->GetBatteryLevel();
        return true;
    }

    virtual void SetPowerSaveLevel(PowerSaveLevel level) override {
        if (level != PowerSaveLevel::LOW_POWER) {
            power_save_timer_->WakeUp();
        }
        WifiBoard::SetPowerSaveLevel(level);
    }

    virtual void StartNetwork() override {
        // 从Flash读取WiFi配置 (地址: 0x7F0000)
        // Flash配置优先级最高，会覆盖NVS中的旧配置
        auto& ssid_manager = SsidManager::GetInstance();

        WifiConfigBin wifi_config;
        esp_err_t err = esp_flash_read(NULL, &wifi_config, WIFI_CONFIG_FLASH_ADDR, sizeof(wifi_config));

        if (err == ESP_OK && memcmp(wifi_config.magic, WIFI_CONFIG_MAGIC, 4) == 0) {
            // 验证数据有效性
            if (wifi_config.ssid_len > 0 && wifi_config.ssid_len <= 32) {
                // 确保字符串以null结尾
                wifi_config.ssid[wifi_config.ssid_len] = '\0';
                wifi_config.password[wifi_config.pwd_len] = '\0';

                // 检查是否和NVS中的第一个WiFi相同
                auto& ssid_list = ssid_manager.GetSsidList();
                bool need_update = true;
                if (!ssid_list.empty()) {
                    const auto& first_ssid = ssid_list[0];
                    if (first_ssid.ssid == wifi_config.ssid &&
                        first_ssid.password == wifi_config.password) {
                        need_update = false;
                        ESP_LOGI(TAG, "Flash WiFi same as NVS, no update needed");
                    }
                }

                if (need_update) {
                    // 将Flash中的WiFi添加到列表（保留用户配网的其他WiFi）
                    ESP_LOGI(TAG, "Adding WiFi from flash: %s", wifi_config.ssid);
                    ssid_manager.AddSsid(wifi_config.ssid, wifi_config.password);
                }
            } else {
                ESP_LOGI(TAG, "WiFi config in flash has invalid SSID length");
            }
        } else {
            ESP_LOGI(TAG, "No valid WiFi config in flash, using NVS");
        }

        // 调用父类方法继续正常的网络初始化流程
        WifiBoard::StartNetwork();
    }
};

DECLARE_BOARD(CustomBoard);

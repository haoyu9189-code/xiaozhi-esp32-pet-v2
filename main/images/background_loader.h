#ifndef _BACKGROUND_LOADER_H_
#define _BACKGROUND_LOADER_H_

#include <stdint.h>
#include <stdbool.h>
#include <esp_partition.h>

// Background dimensions (280x240 fullscreen)
#define BG_WIDTH  280
#define BG_HEIGHT 240

// Per-frame format: RGB888 palette + 8-bit indexed pixels
#define BG_PALETTE_COLORS 256
#define BG_PALETTE_SIZE   (BG_PALETTE_COLORS * 3)  // 768 bytes (RGB888)
#define BG_PIXELS_SIZE    (BG_WIDTH * BG_HEIGHT)   // 67200 bytes
#define BG_FRAME_SIZE_RAW (BG_PALETTE_SIZE + BG_PIXELS_SIZE)  // 67968 bytes per frame

// Display dimensions
#define BG_DISPLAY_WIDTH  280
#define BG_DISPLAY_HEIGHT 240

// Background definition
typedef struct {
    const char* name;       // Background name (e.g., "day", "night")
    const char* label;      // Display label (e.g., "白天", "黑夜")
    uint16_t start_frame;   // Frame index in backgrounds.bin
    uint16_t count;         // Number of frames (usually 1 for static backgrounds)
} BackgroundDef;

// Background category
typedef enum {
    BG_CATEGORY_BASE = 0,   // Base background
    BG_CATEGORY_TIME,       // Time variants (day/night/sunrise/sunset)
    BG_CATEGORY_SEASON,     // Season variants (spring/summer/autumn/winter)
    BG_CATEGORY_WEATHER,    // Weather variants (sunny/rain/snow/thunder/fog)
    BG_CATEGORY_FESTIVAL,   // Festival variants (spring festival/christmas/birthday)
    BG_CATEGORY_STYLE,      // Style variants (cyberpunk/steampunk/fantasy)
    BG_CATEGORY_COUNT
} BackgroundCategory;

// Background loader class - headerless per-frame RGB888 palette format
// Same format as AnimationLoader but with 280x240 dimensions
class BackgroundLoader {
public:
    static BackgroundLoader& GetInstance();

    // Initialize loader with partition
    // background_offset: offset within the partition where backgrounds.bin starts
    // bg_count: number of background frames (default 16)
    bool Initialize(const esp_partition_t* partition, size_t background_offset, uint16_t bg_count = 16);

    // Check if initialized
    bool IsInitialized() const { return initialized_; }

    // Get background dimensions (280x240)
    uint16_t GetWidth() const { return width_; }
    uint16_t GetHeight() const { return height_; }

    // Get total background count
    uint16_t GetBackgroundCount() const { return total_bg_count_; }

    // Get current palette (valid after ReadAndDecodeFrame)
    const uint16_t* GetPalette() const { return palette_; }

    // Decode one row of background to RGB565 buffer
    // row: row index (0 to height-1)
    // out_buf: output buffer, must be at least width * 2 bytes (RGB565)
    void DecodeRow(uint16_t bg_idx, uint16_t row, uint16_t* out_buf) const;

    // Set current background index
    void SetCurrentBackground(uint16_t bg_idx);
    uint16_t GetCurrentBackground() const { return current_bg_idx_; }

    // Decode full background to RGB565 buffer
    // out_buf: must be at least width*height*2 bytes
    // Returns true on success
    bool DecodeFull(uint16_t bg_idx, uint16_t* out_buf) const;

    // Get frame data offset for direct flash access
    size_t GetFrameOffset(uint16_t bg_idx) const;

    // Get frame size
    size_t GetFrameSize() const { return frame_size_; }

private:
    BackgroundLoader();
    ~BackgroundLoader();
    BackgroundLoader(const BackgroundLoader&) = delete;
    BackgroundLoader& operator=(const BackgroundLoader&) = delete;

    // Read frame from flash and decode palette
    bool ReadAndDecodeFrame(uint16_t frame_idx) const;

    bool initialized_;
    const esp_partition_t* partition_;
    size_t base_offset_;  // Offset within partition

    // Background info
    uint16_t width_;            // Image width (280)
    uint16_t height_;           // Image height (240)
    uint16_t total_bg_count_;   // Total number of backgrounds
    size_t frame_size_;         // Size of one frame (67968)

    // Per-frame palette (converted from RGB888 to RGB565)
    mutable uint16_t palette_[BG_PALETTE_COLORS];

    // Pixel buffer for reading from flash
    mutable uint8_t* pixel_buffer_;
    mutable uint16_t cached_frame_idx_;

    // Current background
    uint16_t current_bg_idx_;
};

#endif // _BACKGROUND_LOADER_H_

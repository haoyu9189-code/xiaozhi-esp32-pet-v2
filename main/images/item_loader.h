#ifndef _ITEM_LOADER_H_
#define _ITEM_LOADER_H_

#include <stdint.h>
#include <stdbool.h>
#include <esp_partition.h>

// Item dimensions (40x40 pixels)
#define ITEM_WIDTH  40
#define ITEM_HEIGHT 40

// Item types
#define ITEM_TYPE_COIN  0
#define ITEM_TYPE_POOP  1
#define ITEM_TYPE_COUNT 2

// Per-frame format: RGB888 palette + 8-bit indexed pixels
// Note: items use 255 colors (not 256) per gifs/items/index.json
#define ITEM_PALETTE_COLORS 255
#define ITEM_PALETTE_SIZE   (ITEM_PALETTE_COLORS * 3)  // 765 bytes (RGB888)
#define ITEM_PIXELS_SIZE    (ITEM_WIDTH * ITEM_HEIGHT) // 1600 bytes
#define ITEM_FRAME_SIZE     (ITEM_PALETTE_SIZE + ITEM_PIXELS_SIZE)  // 2365 bytes per frame

// Item loader class - loads 40x40 item sprites from flash
// Same format as BackgroundLoader but smaller dimensions
class ItemLoader {
public:
    static ItemLoader& GetInstance();

    // Initialize loader with partition
    // item_offset: offset within the partition where items data starts
    // item_count: number of item frames (default 2: coin, poop)
    bool Initialize(const esp_partition_t* partition, size_t item_offset, uint16_t item_count = ITEM_TYPE_COUNT);

    // Check if initialized
    bool IsInitialized() const { return initialized_; }

    // Get item dimensions (40x40)
    uint16_t GetWidth() const { return width_; }
    uint16_t GetHeight() const { return height_; }

    // Get total item count
    uint16_t GetItemCount() const { return total_item_count_; }

    // Get background color index for an item (for transparency)
    uint8_t GetBgColorIndex(uint16_t item_type) const;

    // Get current palette (valid after ReadAndDecodeFrame)
    const uint16_t* GetPalette() const { return palette_; }

    // Get pre-decoded item buffer (decoded at init time for fast access)
    // Returns nullptr if not available
    const uint16_t* GetDecodedItem(uint16_t item_type) const;

    // Get pixel from pre-decoded item (fast, no flash access)
    // Returns pixel value, or 0 if out of bounds
    uint16_t GetPixel(uint16_t item_type, uint16_t x, uint16_t y) const;

    // Check if pixel is transparent (background color)
    bool IsTransparent(uint16_t item_type, uint16_t x, uint16_t y) const;

    // Decode full item to RGB565 buffer
    // item_type: ITEM_TYPE_COIN or ITEM_TYPE_POOP
    // out_buf: must be at least ITEM_WIDTH * ITEM_HEIGHT * 2 bytes
    // Returns true on success
    bool DecodeFull(uint16_t item_type, uint16_t* out_buf) const;

    // Decode one row of item to RGB565 buffer
    // row: row index (0 to ITEM_HEIGHT-1)
    // out_buf: output buffer, must be at least ITEM_WIDTH * 2 bytes
    void DecodeRow(uint16_t item_type, uint16_t row, uint16_t* out_buf) const;

    // Get frame data offset for direct flash access
    size_t GetFrameOffset(uint16_t item_type) const;

    // Get frame size
    size_t GetFrameSize() const { return frame_size_; }

private:
    ItemLoader();
    ~ItemLoader();
    ItemLoader(const ItemLoader&) = delete;
    ItemLoader& operator=(const ItemLoader&) = delete;

    // Read frame from flash and decode palette
    bool ReadAndDecodeFrame(uint16_t frame_idx) const;

    bool initialized_;
    const esp_partition_t* partition_;
    size_t base_offset_;  // Offset within partition

    // Item info
    uint16_t width_;            // Image width (40)
    uint16_t height_;           // Image height (40)
    uint16_t total_item_count_; // Total number of items
    size_t frame_size_;         // Size of one frame

    // Per-frame palette (converted from RGB888 to RGB565)
    mutable uint16_t palette_[ITEM_PALETTE_COLORS];

    // Pixel buffer for reading from flash
    mutable uint8_t* pixel_buffer_;
    mutable uint16_t cached_frame_idx_;

    // Background color indices for each item (for transparency)
    uint8_t bg_color_index_[ITEM_TYPE_COUNT];

    // Pre-decoded item buffers (for fast pixel access without flash reads)
    // Each buffer is ITEM_WIDTH * ITEM_HEIGHT * 2 bytes = 3200 bytes
    uint16_t* decoded_items_[ITEM_TYPE_COUNT];
    uint16_t bg_color_rgb565_[ITEM_TYPE_COUNT];  // Pre-converted bg colors
};

#endif // _ITEM_LOADER_H_

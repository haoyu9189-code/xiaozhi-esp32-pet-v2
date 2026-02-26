#include "item_loader.h"
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <string.h>

static const char* TAG = "ItemLoader";

// Background color indices from gifs/items/index.json
// Frame 0 (coin): bg_color_index = 68
// Frame 1 (poop): bg_color_index = 0
static const uint8_t DEFAULT_BG_COLOR_INDEX[ITEM_TYPE_COUNT] = {68, 0};

ItemLoader& ItemLoader::GetInstance() {
    static ItemLoader instance;
    return instance;
}

ItemLoader::ItemLoader()
    : initialized_(false)
    , partition_(nullptr)
    , base_offset_(0)
    , width_(ITEM_WIDTH)
    , height_(ITEM_HEIGHT)
    , total_item_count_(0)
    , frame_size_(ITEM_FRAME_SIZE)
    , pixel_buffer_(nullptr)
    , cached_frame_idx_(0xFFFF)
{
    memset(palette_, 0, sizeof(palette_));
    memcpy(bg_color_index_, DEFAULT_BG_COLOR_INDEX, sizeof(bg_color_index_));
    memset(decoded_items_, 0, sizeof(decoded_items_));
    memset(bg_color_rgb565_, 0, sizeof(bg_color_rgb565_));
}

ItemLoader::~ItemLoader() {
    if (pixel_buffer_) {
        free(pixel_buffer_);
        pixel_buffer_ = nullptr;
    }
    for (int i = 0; i < ITEM_TYPE_COUNT; i++) {
        if (decoded_items_[i]) {
            free(decoded_items_[i]);
            decoded_items_[i] = nullptr;
        }
    }
}

bool ItemLoader::Initialize(const esp_partition_t* partition, size_t item_offset, uint16_t item_count) {
    if (initialized_) {
        ESP_LOGW(TAG, "Already initialized");
        return true;
    }

    if (!partition) {
        ESP_LOGE(TAG, "Invalid partition");
        return false;
    }

    partition_ = partition;
    base_offset_ = item_offset;
    total_item_count_ = item_count;

    // Use default dimensions (40x40)
    width_ = ITEM_WIDTH;
    height_ = ITEM_HEIGHT;

    // Frame size from index.json: 2365 bytes (255 colors * 3 + 40*40)
    // We'll use the standard calculation but can adjust if needed
    frame_size_ = ITEM_FRAME_SIZE;

    ESP_LOGI(TAG, "Item format: headerless per-frame RGB888 palette");
    ESP_LOGI(TAG, "Dimensions: %dx%d, items: %d, frame_size: %u bytes",
             width_, height_, total_item_count_, (unsigned)frame_size_);
    ESP_LOGI(TAG, "Base offset in partition: 0x%X", (unsigned)base_offset_);

    // Allocate row buffer for reading one row
    pixel_buffer_ = (uint8_t*)heap_caps_malloc(width_, MALLOC_CAP_DMA);
    if (!pixel_buffer_) {
        pixel_buffer_ = (uint8_t*)malloc(width_);
    }

    if (!pixel_buffer_) {
        ESP_LOGE(TAG, "Failed to allocate row buffer (%u bytes)", (unsigned)width_);
        return false;
    }

    ESP_LOGI(TAG, "Row buffer allocated: %u bytes", (unsigned)width_);
    ESP_LOGI(TAG, "Partition size: %u bytes, item data end: 0x%X",
             (unsigned)partition_->size,
             (unsigned)(base_offset_ + ITEM_TYPE_COUNT * frame_size_));

    // Pre-decode all items to memory for fast rendering (no flash access during composite)
    bool items_decoded = true;
    for (int i = 0; i < ITEM_TYPE_COUNT && i < item_count; i++) {
        // Allocate buffer for decoded item (40x40 RGB565 = 3200 bytes)
        decoded_items_[i] = (uint16_t*)heap_caps_malloc(ITEM_WIDTH * ITEM_HEIGHT * sizeof(uint16_t), MALLOC_CAP_DMA);
        if (!decoded_items_[i]) {
            decoded_items_[i] = (uint16_t*)malloc(ITEM_WIDTH * ITEM_HEIGHT * sizeof(uint16_t));
        }

        if (!decoded_items_[i]) {
            ESP_LOGW(TAG, "Failed to allocate buffer for item %d", i);
            items_decoded = false;
            continue;
        }

        // Decode the item from flash to memory
        ESP_LOGI(TAG, "Decoding item %d from offset 0x%X...", i,
                 (unsigned)(base_offset_ + i * frame_size_));
        if (DecodeFull(i, decoded_items_[i])) {
            // Store the background color in RGB565 for fast transparency check
            bg_color_rgb565_[i] = palette_[bg_color_index_[i]];
            ESP_LOGI(TAG, "Item %d decoded OK (bg_idx=%d, bg_color=0x%04X)",
                     i, bg_color_index_[i], bg_color_rgb565_[i]);
        } else {
            ESP_LOGE(TAG, "Failed to decode item %d from flash!", i);
            free(decoded_items_[i]);
            decoded_items_[i] = nullptr;
            items_decoded = false;
        }
    }

    if (!items_decoded) {
        ESP_LOGW(TAG, "Some items failed to decode - items may not display correctly");
    }

    initialized_ = true;
    ESP_LOGI(TAG, "Item loader initialized successfully");
    ESP_LOGI(TAG, "BG color indices: coin=%d, poop=%d", bg_color_index_[0], bg_color_index_[1]);
    return true;
}

bool ItemLoader::ReadAndDecodeFrame(uint16_t frame_idx) const {
    if (!partition_) {
        return false;
    }

    if (frame_idx >= total_item_count_) {
        ESP_LOGE(TAG, "Invalid frame index: %d (max: %d)", frame_idx, total_item_count_ - 1);
        return false;
    }

    // Check cache
    if (frame_idx == cached_frame_idx_) {
        return true;
    }

    // Calculate frame offset
    size_t frame_offset = base_offset_ + ((size_t)frame_idx * frame_size_);

    // Verify offset is within partition
    if (frame_offset + ITEM_PALETTE_SIZE > partition_->size) {
        ESP_LOGE(TAG, "Frame offset 0x%X + palette exceeds partition size 0x%X",
                 (unsigned)frame_offset, (unsigned)partition_->size);
        return false;
    }

    // Read RGB888 palette (765 bytes for 255 colors)
    uint8_t palette_rgb888[ITEM_PALETTE_SIZE];
    esp_err_t err = esp_partition_read(partition_, frame_offset, palette_rgb888, ITEM_PALETTE_SIZE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read palette at offset 0x%X: %s",
                 (unsigned)frame_offset, esp_err_to_name(err));
        return false;
    }

    // Debug: show first few palette bytes to verify data
    ESP_LOGI(TAG, "Palette[0] RGB888: %02X %02X %02X",
             palette_rgb888[0], palette_rgb888[1], palette_rgb888[2]);

    // Convert RGB888 to RGB565
    for (int i = 0; i < ITEM_PALETTE_COLORS; i++) {
        uint8_t r = palette_rgb888[i * 3];
        uint8_t g = palette_rgb888[i * 3 + 1];
        uint8_t b = palette_rgb888[i * 3 + 2];
        palette_[i] = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    }

    cached_frame_idx_ = frame_idx;
    return true;
}

uint8_t ItemLoader::GetBgColorIndex(uint16_t item_type) const {
    if (item_type >= ITEM_TYPE_COUNT) {
        return 0;
    }
    return bg_color_index_[item_type];
}

void ItemLoader::DecodeRow(uint16_t item_type, uint16_t row, uint16_t* out_buf) const {
    if (!initialized_ || !pixel_buffer_ || !out_buf) {
        return;
    }

    if (item_type >= total_item_count_ || row >= height_) {
        return;
    }

    // Ensure palette is loaded for this frame
    if (!ReadAndDecodeFrame(item_type)) {
        memset(out_buf, 0, width_ * sizeof(uint16_t));
        return;
    }

    // Calculate offset for this row's pixel data
    size_t frame_offset = base_offset_ + ((size_t)item_type * frame_size_);
    size_t row_offset = frame_offset + ITEM_PALETTE_SIZE + ((size_t)row * width_);

    // Read row pixels from flash
    esp_err_t err = esp_partition_read(partition_, row_offset, pixel_buffer_, width_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read row %d: %s", row, esp_err_to_name(err));
        memset(out_buf, 0, width_ * sizeof(uint16_t));
        return;
    }

    // Decode row using cached palette
    for (uint16_t x = 0; x < width_; x++) {
        uint8_t idx = pixel_buffer_[x];
        out_buf[x] = palette_[idx];
    }
}

bool ItemLoader::DecodeFull(uint16_t item_type, uint16_t* out_buf) const {
    // Note: Don't check initialized_ here - this function is called during Initialize()
    if (!partition_ || !pixel_buffer_ || !out_buf) {
        return false;
    }

    if (item_type >= total_item_count_) {
        ESP_LOGE(TAG, "Invalid item type: %d", item_type);
        return false;
    }

    // Ensure palette is loaded
    if (!ReadAndDecodeFrame(item_type)) {
        ESP_LOGE(TAG, "Failed to read item frame %d", item_type);
        return false;
    }

    // Decode row by row
    size_t frame_offset = base_offset_ + ((size_t)item_type * frame_size_);

    for (uint16_t y = 0; y < height_; y++) {
        size_t row_offset = frame_offset + ITEM_PALETTE_SIZE + ((size_t)y * width_);

        // Read row pixels from flash
        esp_err_t err = esp_partition_read(partition_, row_offset, pixel_buffer_, width_);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read row %d: %s", y, esp_err_to_name(err));
            return false;
        }

        // Decode row using cached palette
        for (uint16_t x = 0; x < width_; x++) {
            uint8_t idx = pixel_buffer_[x];
            out_buf[y * width_ + x] = palette_[idx];
        }
    }

    // Debug: show sample pixels from center of item
    uint16_t center_pixel = out_buf[20 * width_ + 20];  // Center pixel
    uint16_t corner_pixel = out_buf[0];  // Top-left corner
    ESP_LOGI(TAG, "Item %d decoded: center=0x%04X corner=0x%04X (bg=0x%04X)",
             item_type, center_pixel, corner_pixel, palette_[bg_color_index_[item_type]]);

    return true;
}

size_t ItemLoader::GetFrameOffset(uint16_t item_type) const {
    if (!initialized_ || item_type >= total_item_count_) {
        return 0;
    }
    return base_offset_ + ((size_t)item_type * frame_size_);
}

const uint16_t* ItemLoader::GetDecodedItem(uint16_t item_type) const {
    if (!initialized_ || item_type >= ITEM_TYPE_COUNT) {
        return nullptr;
    }
    return decoded_items_[item_type];
}

uint16_t ItemLoader::GetPixel(uint16_t item_type, uint16_t x, uint16_t y) const {
    if (!initialized_ || item_type >= ITEM_TYPE_COUNT || !decoded_items_[item_type]) {
        return 0;
    }
    if (x >= width_ || y >= height_) {
        return 0;
    }
    return decoded_items_[item_type][y * width_ + x];
}

bool ItemLoader::IsTransparent(uint16_t item_type, uint16_t x, uint16_t y) const {
    if (!initialized_ || item_type >= ITEM_TYPE_COUNT || !decoded_items_[item_type]) {
        return true;  // Not available = transparent
    }
    if (x >= width_ || y >= height_) {
        return true;  // Out of bounds = transparent
    }
    uint16_t pixel = decoded_items_[item_type][y * width_ + x];
    return pixel == bg_color_rgb565_[item_type];
}

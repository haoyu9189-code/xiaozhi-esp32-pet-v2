#include "background_loader.h"
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <string.h>

static const char* TAG = "BackgroundLoader";

BackgroundLoader& BackgroundLoader::GetInstance() {
    static BackgroundLoader instance;
    return instance;
}

BackgroundLoader::BackgroundLoader()
    : initialized_(false)
    , partition_(nullptr)
    , base_offset_(0)
    , width_(BG_WIDTH)
    , height_(BG_HEIGHT)
    , total_bg_count_(0)
    , frame_size_(BG_FRAME_SIZE_RAW)
    , pixel_buffer_(nullptr)
    , cached_frame_idx_(0xFFFF)
    , current_bg_idx_(0)
{
    memset(palette_, 0, sizeof(palette_));
}

BackgroundLoader::~BackgroundLoader() {
    if (pixel_buffer_) {
        free(pixel_buffer_);
        pixel_buffer_ = nullptr;
    }
}

bool BackgroundLoader::Initialize(const esp_partition_t* partition, size_t background_offset, uint16_t bg_count) {
    if (initialized_) {
        ESP_LOGW(TAG, "Already initialized");
        return true;
    }

    if (!partition) {
        ESP_LOGE(TAG, "Invalid partition");
        return false;
    }

    partition_ = partition;
    base_offset_ = background_offset;
    total_bg_count_ = bg_count;

    // Use default dimensions (280x240)
    width_ = BG_WIDTH;
    height_ = BG_HEIGHT;
    frame_size_ = BG_FRAME_SIZE_RAW;

    ESP_LOGI(TAG, "Background format: headerless per-frame RGB888 palette");
    ESP_LOGI(TAG, "Dimensions: %dx%d, frames: %d, frame_size: %u bytes",
             width_, height_, total_bg_count_, (unsigned)frame_size_);
    ESP_LOGI(TAG, "Base offset in partition: %u", (unsigned)base_offset_);

    // Allocate row buffer for reading one row (much smaller than full frame)
    // We read pixels row-by-row to save memory
    pixel_buffer_ = (uint8_t*)heap_caps_malloc(width_, MALLOC_CAP_DMA);
    if (!pixel_buffer_) {
        pixel_buffer_ = (uint8_t*)malloc(width_);
    }

    if (!pixel_buffer_) {
        ESP_LOGE(TAG, "Failed to allocate row buffer (%u bytes)", (unsigned)width_);
        return false;
    }

    ESP_LOGI(TAG, "Row buffer allocated: %u bytes (low-memory mode)", (unsigned)width_);

    // Verify we can read the first frame's palette
    if (!ReadAndDecodeFrame(0)) {
        ESP_LOGW(TAG, "Failed to read first frame - backgrounds may not be present");
        // Continue anyway - backgrounds are optional
    }

    initialized_ = true;
    current_bg_idx_ = 0;

    ESP_LOGI(TAG, "Background loader initialized successfully");
    return true;
}

bool BackgroundLoader::ReadAndDecodeFrame(uint16_t frame_idx) const {
    if (!partition_) {
        return false;
    }

    if (frame_idx >= total_bg_count_) {
        ESP_LOGE(TAG, "Invalid frame index: %d (max: %d)", frame_idx, total_bg_count_ - 1);
        return false;
    }

    // Check cache - only palette is cached, pixels read on demand
    if (frame_idx == cached_frame_idx_) {
        return true;
    }

    // Calculate frame offset
    size_t frame_offset = base_offset_ + ((size_t)frame_idx * frame_size_);

    // Read RGB888 palette (768 bytes)
    uint8_t palette_rgb888[BG_PALETTE_SIZE];
    esp_err_t err = esp_partition_read(partition_, frame_offset, palette_rgb888, BG_PALETTE_SIZE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read palette for frame %d: %s", frame_idx, esp_err_to_name(err));
        return false;
    }

    // Convert RGB888 to RGB565
    for (int i = 0; i < BG_PALETTE_COLORS; i++) {
        uint8_t r = palette_rgb888[i * 3];
        uint8_t g = palette_rgb888[i * 3 + 1];
        uint8_t b = palette_rgb888[i * 3 + 2];
        palette_[i] = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    }

    cached_frame_idx_ = frame_idx;
    return true;
}

void BackgroundLoader::DecodeRow(uint16_t bg_idx, uint16_t row, uint16_t* out_buf) const {
    if (!initialized_ || !pixel_buffer_ || !out_buf) {
        return;
    }

    if (bg_idx >= total_bg_count_ || row >= height_) {
        return;
    }

    // Ensure palette is loaded for this frame
    if (!ReadAndDecodeFrame(bg_idx)) {
        memset(out_buf, 0, width_ * sizeof(uint16_t));
        return;
    }

    // Calculate offset for this row's pixel data
    size_t frame_offset = base_offset_ + ((size_t)bg_idx * frame_size_);
    size_t row_offset = frame_offset + BG_PALETTE_SIZE + ((size_t)row * width_);

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

void BackgroundLoader::SetCurrentBackground(uint16_t bg_idx) {
    if (bg_idx >= total_bg_count_) {
        ESP_LOGW(TAG, "Invalid background index: %d (max: %d)", bg_idx, total_bg_count_ - 1);
        return;
    }

    if (bg_idx != current_bg_idx_) {
        current_bg_idx_ = bg_idx;
        ESP_LOGI(TAG, "Background changed to %d", bg_idx);
    }
}

bool BackgroundLoader::DecodeFull(uint16_t bg_idx, uint16_t* out_buf) const {
    if (!initialized_ || !pixel_buffer_ || !out_buf) {
        return false;
    }

    if (bg_idx >= total_bg_count_) {
        ESP_LOGE(TAG, "Invalid background index: %d", bg_idx);
        return false;
    }

    // Ensure palette is loaded
    if (!ReadAndDecodeFrame(bg_idx)) {
        ESP_LOGE(TAG, "Failed to read background frame %d", bg_idx);
        return false;
    }

    ESP_LOGI(TAG, "Decoding full background: bg=%d, size=%dx%d (row-by-row)",
             bg_idx, width_, height_);

    // Decode row by row to save memory
    size_t frame_offset = base_offset_ + ((size_t)bg_idx * frame_size_);

    for (uint16_t y = 0; y < height_; y++) {
        size_t row_offset = frame_offset + BG_PALETTE_SIZE + ((size_t)y * width_);

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

    ESP_LOGI(TAG, "Full background decoded successfully");
    return true;
}

size_t BackgroundLoader::GetFrameOffset(uint16_t bg_idx) const {
    if (!initialized_ || bg_idx >= total_bg_count_) {
        return 0;
    }
    return base_offset_ + ((size_t)bg_idx * frame_size_);
}

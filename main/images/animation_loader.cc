#include "animation_loader.h"
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <string.h>

#define TAG "AnimLoader"

// Animation table - from gifs/index.json
// 8 animations, each with 13 frames, total 104 animation frames
static const AnimationDef ANIMATION_TABLE[ANIM_TYPE_COUNT] = {
    // name,         start, count, fps, loop
    {"idle",            0,    13,  15, true },  // 待机 (idle/breathing)
    {"talk",           13,    13,  15, true },  // 讲话 (speaking)
    {"pet_head",       26,    13,  15, true },  // 摸头 (head pat)
    {"walk",           39,    13,  15, true },  // 行走 (walking)
    {"listen",         52,    13,  15, true },  // 聆听 (listening)
    {"eat",            65,    13,  15, true },  // 吃饭 (eating)
    {"sleep",          78,    13,  15, true },  // 睡觉 (sleeping)
    {"bath",           91,    13,  15, true },  // 洗澡 (bathing)
};

// Emotion name aliases for easier mapping
// Animation mapping (8 animations):
//   idle      = 待机 (idle/breathing)
//   talk      = 讲话 (speaking)
//   pet_head  = 摸头 (head pat interaction)
//   walk      = 行走 (walking)
//   listen    = 聆听 (listening)
//   eat       = 吃饭 (eating)
//   sleep     = 睡觉 (sleeping)
//   bath      = 洗澡 (bathing)
static const struct {
    const char* alias;
    const char* actual;
} EMOTION_ALIASES[] = {
    // 待机 aliases
    {"neutral",   "idle"},     // 待机
    {"standby",   "idle"},     // 待机
    // 讲话 aliases
    {"speaking",  "talk"},     // 讲话
    {"talking",   "talk"},     // 讲话
    // 聆听 aliases
    {"listening", "listen"},   // 聆听
    // 摸头 aliases
    {"petting",   "pet_head"}, // 摸头
    {"pat",       "pet_head"}, // 摸头
    // 行走 aliases
    {"walking",   "walk"},     // 行走
    // 吃饭 aliases
    {"eating",    "eat"},      // 吃饭
    {"feed",      "eat"},      // 吃饭
    // 睡觉 aliases
    {"sleeping",  "sleep"},    // 睡觉
    // 洗澡 aliases
    {"bathing",   "bath"},     // 洗澡
    {"shower",    "bath"},     // 洗澡
    {nullptr, nullptr}
};

AnimationLoader& AnimationLoader::GetInstance() {
    static AnimationLoader instance;
    return instance;
}

AnimationLoader::AnimationLoader()
    : initialized_(false)
    , partition_(nullptr)
    , pixel_buffer_(nullptr)
    , cached_frame_idx_(0xFFFF)
    , decode_buffer_(nullptr)
    , decode_buffer_argb_(nullptr)
    , decode_buffer_rgb565a8_(nullptr) {
    memset(palette_, 0, sizeof(palette_));
}

AnimationLoader::~AnimationLoader() {
    if (pixel_buffer_) {
        heap_caps_free(pixel_buffer_);
        pixel_buffer_ = nullptr;
    }

    if (decode_buffer_) {
        heap_caps_free(decode_buffer_);
        decode_buffer_ = nullptr;
    }

    if (decode_buffer_argb_) {
        heap_caps_free(decode_buffer_argb_);
        decode_buffer_argb_ = nullptr;
    }

    if (decode_buffer_rgb565a8_) {
        heap_caps_free(decode_buffer_rgb565a8_);
        decode_buffer_rgb565a8_ = nullptr;
    }
}

bool AnimationLoader::Initialize() {
    if (initialized_) {
        return true;
    }

    ESP_LOGI(TAG, "Initializing animation loader (new headerless format)...");

    // Find assets partition
    partition_ = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                          ESP_PARTITION_SUBTYPE_DATA_SPIFFS,
                                          "assets");
    if (partition_ == nullptr) {
        ESP_LOGE(TAG, "Assets partition not found");
        return false;
    }

    ESP_LOGI(TAG, "Assets partition found: %s, size: %lu KB",
             partition_->label, partition_->size / 1024);

    // Log format info
    ESP_LOGI(TAG, "Frame format:");
    ESP_LOGI(TAG, "  Size: %dx%d", ANIM_FRAME_WIDTH, ANIM_FRAME_HEIGHT);
    ESP_LOGI(TAG, "  Palette: %d colors (RGB888, %d bytes)", ANIM_PALETTE_COLORS, ANIM_PALETTE_SIZE);
    ESP_LOGI(TAG, "  Pixels: %d bytes (8-bit indexed)", ANIM_PIXELS_SIZE);
    ESP_LOGI(TAG, "  Frame size: %d bytes", ANIM_FRAME_SIZE_RAW);
    ESP_LOGI(TAG, "  Total frames: %d", ANIM_TOTAL_FRAMES);
    ESP_LOGI(TAG, "  Total data: %u bytes (%.1f MB)",
             (unsigned)(ANIM_TOTAL_FRAMES * ANIM_FRAME_SIZE_RAW),
             (ANIM_TOTAL_FRAMES * ANIM_FRAME_SIZE_RAW) / (1024.0f * 1024.0f));

    // Allocate pixel buffer for reading from flash
    pixel_buffer_ = (uint8_t*)heap_caps_malloc(ANIM_PIXELS_SIZE, MALLOC_CAP_DMA);
    if (!pixel_buffer_) {
        ESP_LOGE(TAG, "Failed to allocate pixel buffer (%d bytes)", ANIM_PIXELS_SIZE);
        return false;
    }
    ESP_LOGI(TAG, "Pixel buffer allocated: %d bytes", ANIM_PIXELS_SIZE);

    // Allocate decode buffer for legacy API (one full frame RGB565)
    size_t decode_buf_size = ANIM_FRAME_SIZE_RGB565;
    decode_buffer_ = (uint16_t*)heap_caps_malloc(decode_buf_size, MALLOC_CAP_DMA);
    if (!decode_buffer_) {
        ESP_LOGE(TAG, "Failed to allocate decode buffer (%u bytes)", (unsigned)decode_buf_size);
        heap_caps_free(pixel_buffer_);
        pixel_buffer_ = nullptr;
        return false;
    }
    ESP_LOGI(TAG, "Decode buffer allocated: %u bytes", (unsigned)decode_buf_size);

    // Try to allocate ARGB8888 decode buffer
    size_t argb_buf_size = ANIM_FRAME_SIZE_ARGB8888;
    decode_buffer_argb_ = (uint32_t*)heap_caps_malloc(argb_buf_size, MALLOC_CAP_DMA);
    if (!decode_buffer_argb_) {
        decode_buffer_argb_ = (uint32_t*)heap_caps_malloc(argb_buf_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (!decode_buffer_argb_) {
        decode_buffer_argb_ = (uint32_t*)malloc(argb_buf_size);
    }

    if (decode_buffer_argb_) {
        ESP_LOGI(TAG, "ARGB decode buffer allocated: %u bytes", (unsigned)argb_buf_size);
    } else {
        // Try RGB565A8 as fallback
        size_t rgb565a8_buf_size = ANIM_FRAME_SIZE_RGB565A8;
        decode_buffer_rgb565a8_ = (uint8_t*)heap_caps_malloc(rgb565a8_buf_size, MALLOC_CAP_DMA);
        if (!decode_buffer_rgb565a8_) {
            decode_buffer_rgb565a8_ = (uint8_t*)malloc(rgb565a8_buf_size);
        }
        if (decode_buffer_rgb565a8_) {
            ESP_LOGI(TAG, "RGB565A8 decode buffer allocated: %u bytes", (unsigned)rgb565a8_buf_size);
        } else {
            ESP_LOGW(TAG, "No transparent buffer available, using chroma key mode");
        }
    }

    initialized_ = true;

    const char* mode_name = "RGB565 (chroma key)";
    if (decode_buffer_argb_) {
        mode_name = "ARGB8888 transparent";
    } else if (decode_buffer_rgb565a8_) {
        mode_name = "RGB565A8 transparent";
    }
    ESP_LOGI(TAG, "Animation loader initialized (%s mode)", mode_name);
    ESP_LOGI(TAG, "  Display offset: (%d, %d)", ANIM_OFFSET_X, ANIM_OFFSET_Y);

    return true;
}

void AnimationLoader::FreeTransparentBuffers() {
    size_t freed = 0;

    if (decode_buffer_argb_) {
        freed += ANIM_FRAME_SIZE_ARGB8888;
        heap_caps_free(decode_buffer_argb_);
        decode_buffer_argb_ = nullptr;
    }

    if (decode_buffer_rgb565a8_) {
        freed += ANIM_FRAME_SIZE_RGB565A8;
        heap_caps_free(decode_buffer_rgb565a8_);
        decode_buffer_rgb565a8_ = nullptr;
    }

    if (freed > 0) {
        ESP_LOGI(TAG, "Freed transparent buffers: %u bytes", (unsigned)freed);
    }
}

const AnimationDef* AnimationLoader::GetAnimationDef(AnimLoaderType type) const {
    if (type >= ANIM_TYPE_COUNT) {
        return &ANIMATION_TABLE[ANIM_IDLE];
    }
    return &ANIMATION_TABLE[type];
}

const AnimationDef* AnimationLoader::GetAnimationByName(const char* name) const {
    if (!name) {
        return &ANIMATION_TABLE[ANIM_IDLE];
    }

    // First, check aliases
    for (int i = 0; EMOTION_ALIASES[i].alias != nullptr; i++) {
        if (strcmp(EMOTION_ALIASES[i].alias, name) == 0) {
            name = EMOTION_ALIASES[i].actual;
            break;
        }
    }

    // Then find in animation table
    for (int i = 0; i < ANIM_TYPE_COUNT; i++) {
        if (strcmp(ANIMATION_TABLE[i].name, name) == 0) {
            return &ANIMATION_TABLE[i];
        }
    }

    ESP_LOGW(TAG, "Animation '%s' not found, using idle", name);
    return &ANIMATION_TABLE[ANIM_IDLE];
}

// Helper: convert RGB888 to RGB565
static inline uint16_t rgb888_to_rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

bool AnimationLoader::ReadAndDecodeFrame(uint16_t frame_idx) const {
    if (!initialized_ || !pixel_buffer_ || frame_idx >= ANIM_TOTAL_FRAMES) {
        return false;
    }

    // Check if frame is already cached
    if (frame_idx == cached_frame_idx_) {
        return true;
    }

    // Calculate frame offset in partition
    size_t frame_offset = (size_t)frame_idx * ANIM_FRAME_SIZE_RAW;

    // Read RGB888 palette (768 bytes)
    uint8_t pal_rgb888[ANIM_PALETTE_SIZE];
    esp_err_t err = esp_partition_read(partition_, frame_offset, pal_rgb888, ANIM_PALETTE_SIZE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read palette for frame %d: %s", frame_idx, esp_err_to_name(err));
        return false;
    }

    // Convert RGB888 palette to RGB565
    for (int i = 0; i < ANIM_PALETTE_COLORS; i++) {
        palette_[i] = rgb888_to_rgb565(
            pal_rgb888[i * 3],
            pal_rgb888[i * 3 + 1],
            pal_rgb888[i * 3 + 2]
        );
    }

    // Read pixel indices
    err = esp_partition_read(partition_, frame_offset + ANIM_PALETTE_SIZE, pixel_buffer_, ANIM_PIXELS_SIZE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read pixels for frame %d: %s", frame_idx, esp_err_to_name(err));
        return false;
    }

    cached_frame_idx_ = frame_idx;
    return true;
}

void AnimationLoader::DecodeFrame(uint16_t frame_idx, uint16_t* out_buf) const {
    if (!out_buf || !ReadAndDecodeFrame(frame_idx)) {
        return;
    }

    // Decode indexed pixels to RGB565 (preserve original colors)
    // Background transparency is handled by color range check in compositing
    for (size_t i = 0; i < ANIM_PIXELS_SIZE; i++) {
        uint8_t idx = pixel_buffer_[i];
        out_buf[i] = palette_[idx];
    }
}

void AnimationLoader::DecodeFrameARGB(uint16_t frame_idx, uint32_t* out_buf) const {
    if (!out_buf || !ReadAndDecodeFrame(frame_idx)) {
        return;
    }

    // Decode indexed pixels to ARGB8888
    for (size_t i = 0; i < ANIM_PIXELS_SIZE; i++) {
        uint8_t idx = pixel_buffer_[i];
        if (idx == 0) {
            // Transparent background
            out_buf[i] = 0x00000000;
        } else {
            // Convert RGB565 to ARGB8888
            uint16_t rgb565 = palette_[idx];
            uint8_t r = ((rgb565 >> 11) & 0x1F) << 3;
            uint8_t g = ((rgb565 >> 5) & 0x3F) << 2;
            uint8_t b = (rgb565 & 0x1F) << 3;
            out_buf[i] = (0xFF << 24) | (r << 16) | (g << 8) | b;
        }
    }
}

void AnimationLoader::DecodeFrameRGB565A8(uint16_t frame_idx, uint8_t* out_buf) const {
    if (!out_buf || !ReadAndDecodeFrame(frame_idx)) {
        return;
    }

    // RGB565A8 format: first RGB565 data, then A8 alpha data
    uint16_t* rgb_buf = (uint16_t*)out_buf;
    uint8_t* alpha_buf = out_buf + ANIM_PIXELS_SIZE * sizeof(uint16_t);

    for (size_t i = 0; i < ANIM_PIXELS_SIZE; i++) {
        uint8_t idx = pixel_buffer_[i];
        rgb_buf[i] = palette_[idx];
        alpha_buf[i] = (idx == 0) ? 0x00 : 0xFF;
    }
}

void AnimationLoader::DecodeBackgroundFrame(uint16_t frame_idx, uint16_t* out_buf) const {
    if (!out_buf || !ReadAndDecodeFrame(frame_idx)) {
        return;
    }

    // Decode indexed pixels to RGB565 WITHOUT chroma key
    // Background frames are rendered fully (no transparency)
    for (size_t i = 0; i < ANIM_PIXELS_SIZE; i++) {
        uint8_t idx = pixel_buffer_[i];
        out_buf[i] = palette_[idx];
    }
}

void AnimationLoader::DecodeBackgroundRow(uint16_t frame_idx, uint16_t row_idx, uint16_t* out_buf) const {
    if (!out_buf || row_idx >= ANIM_FRAME_HEIGHT || !ReadAndDecodeFrame(frame_idx)) {
        return;
    }

    // Decode one row of indexed pixels to RGB565 WITHOUT chroma key
    size_t row_offset = row_idx * ANIM_FRAME_WIDTH;
    for (size_t x = 0; x < ANIM_FRAME_WIDTH; x++) {
        uint8_t idx = pixel_buffer_[row_offset + x];
        out_buf[x] = palette_[idx];
    }
}

// Legacy API compatibility
const uint8_t* AnimationLoader::GetFrame(AnimLoaderType type, uint8_t frame_idx) {
    if (!initialized_ || !decode_buffer_) {
        return nullptr;
    }

    const AnimationDef* anim = GetAnimationDef(type);
    if (!anim || frame_idx >= anim->frame_count) {
        return nullptr;
    }

    uint16_t global_frame = anim->start_frame + frame_idx;
    DecodeFrame(global_frame, decode_buffer_);

    return (const uint8_t*)decode_buffer_;
}

const uint8_t* AnimationLoader::GetFrameByIndex(int frame_idx) {
    if (!initialized_ || !decode_buffer_ || frame_idx < 0 || frame_idx >= ANIM_TOTAL_FRAMES) {
        return nullptr;
    }

    DecodeFrame((uint16_t)frame_idx, decode_buffer_);
    return (const uint8_t*)decode_buffer_;
}

const uint8_t* AnimationLoader::GetFrameByIndexARGB(int frame_idx) {
    if (!initialized_ || !decode_buffer_argb_ || frame_idx < 0 || frame_idx >= ANIM_TOTAL_FRAMES) {
        return nullptr;
    }

    DecodeFrameARGB((uint16_t)frame_idx, decode_buffer_argb_);
    return (const uint8_t*)decode_buffer_argb_;
}

const uint8_t* AnimationLoader::GetFrameByIndexRGB565A8(int frame_idx) {
    if (!initialized_ || !decode_buffer_rgb565a8_ || frame_idx < 0 || frame_idx >= ANIM_TOTAL_FRAMES) {
        return nullptr;
    }

    DecodeFrameRGB565A8((uint16_t)frame_idx, decode_buffer_rgb565a8_);
    return decode_buffer_rgb565a8_;
}

const uint8_t* AnimationLoader::GetBackgroundFrameByIndex(int frame_idx) {
    if (!initialized_ || !decode_buffer_ || frame_idx < 0 || frame_idx >= ANIM_TOTAL_FRAMES) {
        return nullptr;
    }

    DecodeBackgroundFrame((uint16_t)frame_idx, decode_buffer_);
    return (const uint8_t*)decode_buffer_;
}

/*
 * Simple Frame Decoder for P256 indexed color frames
 * Format: frames.bin - no header, each frame has its own palette
 * Frame structure: [palette 256×3 RGB888] + [pixels W×H bytes]
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>

namespace simple_frame {

// Frame format constants (P256, 200×200)
static constexpr uint16_t FRAME_WIDTH = 200;
static constexpr uint16_t FRAME_HEIGHT = 200;
static constexpr uint16_t PALETTE_COLORS = 256;
static constexpr size_t PALETTE_SIZE = PALETTE_COLORS * 3;  // RGB888
static constexpr size_t PIXEL_COUNT = FRAME_WIDTH * FRAME_HEIGHT;
static constexpr size_t FRAME_SIZE = PALETTE_SIZE + PIXEL_COUNT;  // 26368 bytes

// Animation definition
struct Animation {
    const char* name;
    uint16_t start_frame;
    uint16_t frame_count;
    uint8_t fps;
    bool loop;
};

class SimpleFrameDecoder {
public:
    SimpleFrameDecoder() = default;
    ~SimpleFrameDecoder() = default;

    // Load frames.bin data (memory-mapped, no header)
    bool load(const void* data, size_t size) {
        if (!data || size < FRAME_SIZE) {
            return false;
        }

        data_ = static_cast<const uint8_t*>(data);
        data_size_ = size;

        // Calculate frame count from file size
        frame_count_ = size / FRAME_SIZE;

        return frame_count_ > 0;
    }

    // Get frame data pointer (start of palette)
    const uint8_t* getFrameData(uint16_t frame_idx) const {
        if (frame_idx >= frame_count_ || !data_) {
            return nullptr;
        }
        return data_ + (size_t)frame_idx * FRAME_SIZE;
    }

    // Get palette data for a frame (RGB888, 256×3 bytes)
    const uint8_t* getPaletteData(uint16_t frame_idx) const {
        return getFrameData(frame_idx);
    }

    // Get pixel data for a frame (after palette)
    const uint8_t* getPixelData(uint16_t frame_idx) const {
        const uint8_t* frame = getFrameData(frame_idx);
        if (!frame) return nullptr;
        return frame + PALETTE_SIZE;
    }

    // Convert RGB888 to RGB565
    static uint16_t rgb888to565(uint8_t r, uint8_t g, uint8_t b) {
        return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    }

    // Build RGB565 palette from frame's RGB888 palette
    void buildPalette565(uint16_t frame_idx, uint16_t* palette565) const {
        const uint8_t* pal = getPaletteData(frame_idx);
        if (!pal || !palette565) return;

        for (int i = 0; i < PALETTE_COLORS; i++) {
            palette565[i] = rgb888to565(pal[i*3], pal[i*3+1], pal[i*3+2]);
        }
    }

    // Decode single row to RGB565 buffer
    void decodeRow(uint16_t frame_idx, uint16_t row, uint16_t* out_buf, const uint16_t* palette565) const {
        const uint8_t* pixels = getPixelData(frame_idx);
        if (!pixels || !out_buf || !palette565 || row >= FRAME_HEIGHT) return;

        const uint8_t* row_data = pixels + (size_t)row * FRAME_WIDTH;
        for (uint16_t x = 0; x < FRAME_WIDTH; x++) {
            out_buf[x] = palette565[row_data[x]];
        }
    }

    // Decode full frame to RGB565 buffer
    void decodeFrame(uint16_t frame_idx, uint16_t* out_buf) const {
        const uint8_t* pixels = getPixelData(frame_idx);
        if (!pixels || !out_buf) return;

        // Build palette for this frame
        uint16_t palette565[PALETTE_COLORS];
        buildPalette565(frame_idx, palette565);

        // Decode all pixels
        for (size_t i = 0; i < PIXEL_COUNT; i++) {
            out_buf[i] = palette565[pixels[i]];
        }
    }

    // Getters
    uint16_t width() const { return FRAME_WIDTH; }
    uint16_t height() const { return FRAME_HEIGHT; }
    uint16_t frameCount() const { return frame_count_; }

private:
    const uint8_t* data_ = nullptr;
    size_t data_size_ = 0;
    uint16_t frame_count_ = 0;
};

// 动画索引常量 (11个动画)
enum AnimIndex {
    ANIM_BLINK = 0,         // 穿插用 - 眨眼
    ANIM_POSITIVE,          // 穿插用 - 开心
    ANIM_LISTEN,            // 主动画 - 聆听
    ANIM_LOOK_DOWN_RIGHT,   // 穿插用 - 看右下
    ANIM_IDLE,              // 主动画 - 空闲
    ANIM_TALK,              // 主动画 - 说话
    ANIM_LOOK_LEFT,         // 穿插用 - 看左
    ANIM_YAWN,              // 穿插用 - 打哈欠
    ANIM_TOUCH,             // 触摸专用
    ANIM_DISAPPEAR,         // 特殊 - 消失
    ANIM_PET_HEAD,          // 触摸专用 - 摸头
    ANIM_COUNT
};

// Animation table - maps emotion names to frame ranges
// 匹配 index.json (11个动画，每个9帧)
static const Animation ANIMATION_TABLE[] = {
    // name,              start, count, fps, loop
    {"blink",                0,     9,   6, false},  // 穿插用 - 眨眼
    {"positive",             9,     9,   6, false},  // 穿插用 - 开心
    {"listen",              18,     9,   6, true },  // 主动画 - 聆听
    {"look_down_right",     27,     9,   6, false},  // 穿插用 - 看右下
    {"idle",                36,     9,   6, true },  // 主动画 - 空闲
    {"talk",                45,     9,   6, true },  // 主动画 - 说话
    {"look_left",           54,     9,   6, false},  // 穿插用 - 看左
    {"yawn",                63,     9,   6, false},  // 穿插用 - 打哈欠
    {"touch",               72,     9,   6, false},  // 触摸专用
    {"disappear",           81,     9,   6, false},  // 特殊 - 消失
    {"pet_head",            90,     9,   6, false},  // 触摸专用 - 摸头
};

static constexpr size_t ANIMATION_COUNT = sizeof(ANIMATION_TABLE) / sizeof(ANIMATION_TABLE[0]);

// 可穿插动画列表（排除主动画和特殊动画）
static const int INSERTABLE_ANIMS[] = {
    ANIM_BLINK,
    ANIM_POSITIVE,
    ANIM_LOOK_DOWN_RIGHT,
    ANIM_LOOK_LEFT,
    ANIM_YAWN,
};
static constexpr size_t INSERTABLE_COUNT = sizeof(INSERTABLE_ANIMS) / sizeof(INSERTABLE_ANIMS[0]);

// 随机穿插概率 (0-100)
static constexpr int INSERT_CHANCE = 30;

// Find animation by name
inline const Animation* findAnimation(const char* name) {
    if (!name) return nullptr;

    for (size_t i = 0; i < ANIMATION_COUNT; i++) {
        if (strcmp(ANIMATION_TABLE[i].name, name) == 0) {
            return &ANIMATION_TABLE[i];
        }
    }

    // Default to idle if not found
    return &ANIMATION_TABLE[ANIM_IDLE];
}

} // namespace simple_frame

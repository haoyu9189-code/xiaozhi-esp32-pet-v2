#ifndef _ANIMATION_LOADER_H_
#define _ANIMATION_LOADER_H_

#include <stdint.h>
#include <stdbool.h>
#include <esp_partition.h>

// Animation output dimensions (160x160, centered on 280x240 display)
#define ANIM_FRAME_WIDTH  160
#define ANIM_FRAME_HEIGHT 160
#define ANIM_FRAME_SIZE_RGB565 (ANIM_FRAME_WIDTH * ANIM_FRAME_HEIGHT * 2)  // RGB565 = 2 bytes per pixel
#define ANIM_FRAME_SIZE_RGB565A8 (ANIM_FRAME_WIDTH * ANIM_FRAME_HEIGHT * 3)  // RGB565A8 = 3 bytes per pixel
#define ANIM_FRAME_SIZE_ARGB8888 (ANIM_FRAME_WIDTH * ANIM_FRAME_HEIGHT * 4)  // ARGB8888 = 4 bytes per pixel

// New format: per-frame RGB888 palette + 8-bit indexed pixels
#define ANIM_PALETTE_COLORS 256
#define ANIM_PALETTE_SIZE   (ANIM_PALETTE_COLORS * 3)  // 768 bytes (RGB888)
#define ANIM_PIXELS_SIZE    (ANIM_FRAME_WIDTH * ANIM_FRAME_HEIGHT)  // 25600 bytes
#define ANIM_FRAME_SIZE_RAW (ANIM_PALETTE_SIZE + ANIM_PIXELS_SIZE)  // 26368 bytes per frame

// Display dimensions
#define ANIM_DISPLAY_WIDTH  280
#define ANIM_DISPLAY_HEIGHT 240

// Display offset (centered)
#define ANIM_OFFSET_X ((ANIM_DISPLAY_WIDTH - ANIM_FRAME_WIDTH) / 2)   // 60
#define ANIM_OFFSET_Y ((ANIM_DISPLAY_HEIGHT - ANIM_FRAME_HEIGHT) / 2) // 40

// Animation types for emotion mapping (8 animations)
typedef enum {
    ANIM_IDLE = 0,       // idle (待机)
    ANIM_TALK,           // talk (讲话)
    ANIM_PET_HEAD,       // pet_head (摸头)
    ANIM_WALK,           // walk (行走)
    ANIM_LISTEN,         // listen (聆听)
    ANIM_EAT,            // eat (吃饭)
    ANIM_SLEEP,          // sleep (睡觉)
    ANIM_BATH,           // bath (洗澡)
    ANIM_TYPE_COUNT      // 8
} AnimLoaderType;

// Animation definition
typedef struct {
    const char* name;       // Emotion name
    uint16_t start_frame;   // First frame index
    uint16_t frame_count;   // Number of frames
    uint8_t fps;            // Playback speed
    bool loop;              // Loop animation
} AnimationDef;

// Animation frame count (8 animations × 13 frames = 104)
// Backgrounds are stored separately and handled by BackgroundLoader (280x240 fullscreen)
#define ANIM_FRAME_COUNT        104

// Total frames in frames.bin (animation only, backgrounds in separate file)
#define ANIM_TOTAL_FRAMES       104

// Animation loader class - headerless per-frame palette format
class AnimationLoader {
public:
    static AnimationLoader& GetInstance();

    // Initialize loader with partition
    bool Initialize();

    // Get animation definition by type
    const AnimationDef* GetAnimationDef(AnimLoaderType type) const;

    // Get animation definition by name
    const AnimationDef* GetAnimationByName(const char* name) const;

    // Read frame from flash and decode to RGB565 (handles per-frame palette)
    bool ReadAndDecodeFrame(uint16_t frame_idx) const;

    // Decode full frame to RGB565 buffer (needs ANIM_FRAME_SIZE_RGB565 bytes)
    void DecodeFrame(uint16_t frame_idx, uint16_t* out_buf) const;

    // Decode full frame to ARGB8888 buffer with transparent background
    void DecodeFrameARGB(uint16_t frame_idx, uint32_t* out_buf) const;

    // Decode full frame to RGB565A8 buffer (RGB565 + alpha)
    void DecodeFrameRGB565A8(uint16_t frame_idx, uint8_t* out_buf) const;

    // Decode background frame to RGB565 buffer (no transparency, full decode)
    // Used for background frames (78-94) that should not have chroma key
    void DecodeBackgroundFrame(uint16_t frame_idx, uint16_t* out_buf) const;

    // Decode a single row of background frame (for low-memory row-by-row mode)
    // row_idx: 0 to ANIM_FRAME_HEIGHT-1
    // out_buf: buffer for ANIM_FRAME_WIDTH pixels (RGB565)
    void DecodeBackgroundRow(uint16_t frame_idx, uint16_t row_idx, uint16_t* out_buf) const;

    // Get current frame's palette (valid after ReadAndDecodeFrame)
    const uint16_t* GetPalette() const { return palette_; }

    // Get background color index (first palette entry is typically background)
    uint8_t GetBgColorIdx() const { return 0; }

    // Get frame dimensions
    uint16_t GetWidth() const { return ANIM_FRAME_WIDTH; }
    uint16_t GetHeight() const { return ANIM_FRAME_HEIGHT; }

    // Get total frame count
    uint16_t GetFrameCount() const { return ANIM_TOTAL_FRAMES; }

    // Get total data size in partition
    size_t GetTotalDataSize() const { return (size_t)ANIM_TOTAL_FRAMES * ANIM_FRAME_SIZE_RAW; }

    // Check if initialized
    bool IsInitialized() const { return initialized_; }

    // Check if ARGB8888 output is available
    bool IsARGBAvailable() const { return decode_buffer_argb_ != nullptr; }

    // Check if RGB565A8 output is available
    bool IsRGB565A8Available() const { return decode_buffer_rgb565a8_ != nullptr; }

    // Check if any transparent mode is available
    bool IsTransparentModeAvailable() const { return decode_buffer_argb_ != nullptr || decode_buffer_rgb565a8_ != nullptr; }

    // Free transparent buffers to save memory for compositing
    void FreeTransparentBuffers();

    // Legacy compatibility - get frame by animation type and index
    const uint8_t* GetFrame(AnimLoaderType type, uint8_t frame_idx);
    const uint8_t* GetFrameByIndex(int frame_idx);

    // Get frame as ARGB8888 with transparent background
    const uint8_t* GetFrameByIndexARGB(int frame_idx);

    // Get frame as RGB565A8 with transparent background
    const uint8_t* GetFrameByIndexRGB565A8(int frame_idx);

    // Get background frame by index (no transparency, full decode)
    // Returns pointer to internal decode buffer (valid until next call)
    const uint8_t* GetBackgroundFrameByIndex(int frame_idx);

private:
    AnimationLoader();
    ~AnimationLoader();
    AnimationLoader(const AnimationLoader&) = delete;
    AnimationLoader& operator=(const AnimationLoader&) = delete;

    bool initialized_;
    const esp_partition_t* partition_;

    // Per-frame palette (converted from RGB888 to RGB565)
    mutable uint16_t palette_[ANIM_PALETTE_COLORS];

    // Frame buffer for reading pixel indices from flash
    mutable uint8_t* pixel_buffer_;
    mutable uint16_t cached_frame_idx_;

    // Decode buffer for legacy API (one frame RGB565)
    uint16_t* decode_buffer_;

    // Decode buffer for ARGB8888 with transparent background
    uint32_t* decode_buffer_argb_;

    // Decode buffer for RGB565A8 with transparent background
    uint8_t* decode_buffer_rgb565a8_;
};

#endif // _ANIMATION_LOADER_H_

/*
 * Simple Frame Display Implementation
 */

#include "simple_frame_display.h"

#include <esp_log.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <cstring>

static const char* TAG = "SimpleFrameDisplay";

namespace simple_frame {

SimpleFrameDisplay::SimpleFrameDisplay(esp_lcd_panel_handle_t panel,
                                       esp_lcd_panel_io_handle_t panel_io,
                                       int screen_width, int screen_height)
    : panel_(panel)
    , panel_io_(panel_io)
    , screen_width_(screen_width)
    , screen_height_(screen_height)
{
    width_ = screen_width;
    height_ = screen_height;

    // Calculate offset to center 200x200 on screen
    offset_x_ = (screen_width_ - FRAME_WIDTH) / 2;   // (280-200)/2 = 40
    offset_y_ = (screen_height_ - FRAME_HEIGHT) / 2; // (240-200)/2 = 20

    // Allocate row buffer in DMA-capable memory
    row_buffer_ = static_cast<uint16_t*>(
        heap_caps_malloc(ROW_BUFFER_WIDTH * sizeof(uint16_t), MALLOC_CAP_DMA));

    if (!row_buffer_) {
        ESP_LOGE(TAG, "Failed to allocate row buffer");
    }

    // Create mutex
    mutex_ = xSemaphoreCreateMutex();
    render_complete_ = xSemaphoreCreateBinary();
    xSemaphoreGive(render_complete_);

    ESP_LOGI(TAG, "Created SimpleFrameDisplay %dx%d, offset (%d,%d)",
             screen_width_, screen_height_, offset_x_, offset_y_);
}

SimpleFrameDisplay::~SimpleFrameDisplay()
{
    // Stop animation task
    anim_running_ = false;
    if (anim_task_) {
        vTaskDelay(pdMS_TO_TICKS(100));
        vTaskDelete(anim_task_);
        anim_task_ = nullptr;
    }

    if (row_buffer_) {
        heap_caps_free(row_buffer_);
        row_buffer_ = nullptr;
    }

    if (mutex_) {
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
    }

    if (render_complete_) {
        vSemaphoreDelete(render_complete_);
        render_complete_ = nullptr;
    }
}

bool SimpleFrameDisplay::LoadFramesData(const void* data, size_t size)
{
    if (!decoder_.load(data, size)) {
        ESP_LOGE(TAG, "Failed to load frames.bin data");
        return false;
    }

    data_loaded_ = true;
    ESP_LOGI(TAG, "Loaded frames.bin: %dx%d, %d frames (P256 format)",
             decoder_.width(), decoder_.height(), decoder_.frameCount());

    // Clear screen with black
    ClearScreen(0x0000);

    // Start animation task
    if (!anim_task_) {
        anim_running_ = true;
        xTaskCreatePinnedToCore(AnimationTask, "anim_task", 4096, this,
                                5, &anim_task_, 1);
    }

    // Set default animation
    SetEmotion("idle");

    return true;
}

void SimpleFrameDisplay::SetEmotion(const char* emotion)
{
    if (!emotion || !data_loaded_) {
        return;
    }

    ESP_LOGI(TAG, "SetEmotion: %s", emotion);

    const Animation* anim = findAnimation(emotion);
    if (!anim) {
        ESP_LOGW(TAG, "Animation not found: %s, using idle", emotion);
        anim = &ANIMATION_TABLE[ANIM_IDLE];
    }

    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
        base_anim_ = anim;        // 保存基础动画
        current_anim_ = anim;
        current_frame_ = anim->start_frame;
        playing_insert_ = false;  // 重置穿插状态
        anim_paused_ = false;
        xSemaphoreGive(mutex_);
    }
}

void SimpleFrameDisplay::SetStatus(const char* status)
{
    if (!status) return;

    ESP_LOGI(TAG, "SetStatus: %s", status);
    status_text_ = status;

    // Map status to emotion (使用新动画名)
    if (strstr(status, "listening") || strstr(status, "Listening")) {
        SetEmotion("listen");
    } else if (strstr(status, "speaking") || strstr(status, "Speaking")) {
        SetEmotion("talk");
    } else if (strstr(status, "thinking") || strstr(status, "Thinking")) {
        SetEmotion("idle");
    } else if (strstr(status, "idle") || strstr(status, "Idle")) {
        SetEmotion("idle");
    }
}

void SimpleFrameDisplay::SetChatMessage(const char* role, const char* content)
{
    if (content) {
        chat_message_ = content;
        ESP_LOGD(TAG, "Chat [%s]: %s", role ? role : "?", content);
    }
}

void SimpleFrameDisplay::SetTheme(Theme* theme)
{
    current_theme_ = theme;
    ESP_LOGI(TAG, "SetTheme: %s", theme ? theme->name().c_str() : "null");
}

void SimpleFrameDisplay::ShowNotification(const char* notification, int duration_ms)
{
    if (notification) {
        ESP_LOGI(TAG, "Notification: %s", notification);
        status_text_ = notification;
    }
}

void SimpleFrameDisplay::UpdateStatusBar(bool update_all)
{
    // Could update time display here if needed
}

void SimpleFrameDisplay::SetPowerSaveMode(bool on)
{
    ESP_LOGI(TAG, "PowerSaveMode: %s", on ? "ON" : "OFF");
    anim_paused_ = on;
}

void SimpleFrameDisplay::SetTextFont(std::shared_ptr<LvglFont> font)
{
    text_font_ = font;
}

bool SimpleFrameDisplay::Lock(int timeout_ms)
{
    if (!mutex_) return false;
    return xSemaphoreTake(mutex_, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void SimpleFrameDisplay::Unlock()
{
    if (mutex_) {
        xSemaphoreGive(mutex_);
    }
}

void SimpleFrameDisplay::AnimationTask(void* param)
{
    auto* display = static_cast<SimpleFrameDisplay*>(param);
    display->RunAnimationLoop();
}

void SimpleFrameDisplay::RunAnimationLoop()
{
    TickType_t last_wake_time = xTaskGetTickCount();

    while (anim_running_) {
        if (anim_paused_ || !current_anim_ || !data_loaded_) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        // Get current animation info
        const Animation* anim = nullptr;
        uint16_t frame = 0;

        if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(10)) == pdTRUE) {
            anim = current_anim_;
            frame = current_frame_;

            // Advance frame
            current_frame_++;
            uint16_t end_frame = anim->start_frame + anim->frame_count;

            if (current_frame_ >= end_frame) {
                // 动画播完一轮
                if (playing_insert_) {
                    // 穿插动画播完，回到主动画
                    playing_insert_ = false;
                    if (base_anim_) {
                        current_anim_ = base_anim_;
                        current_frame_ = base_anim_->start_frame;
                        ESP_LOGD(TAG, "Insert done, back to: %s", base_anim_->name);
                    }
                } else {
                    // 主动画播完一轮，30%概率穿插
                    if ((esp_random() % 100) < INSERT_CHANCE) {
                        // 随机选一个穿插动画
                        int idx = INSERTABLE_ANIMS[esp_random() % INSERTABLE_COUNT];
                        current_anim_ = &ANIMATION_TABLE[idx];
                        current_frame_ = current_anim_->start_frame;
                        playing_insert_ = true;
                        ESP_LOGD(TAG, "Insert animation: %s", current_anim_->name);
                    } else {
                        // 继续循环主动画
                        current_frame_ = anim->start_frame;
                    }
                }
            }
            xSemaphoreGive(mutex_);
        }

        if (anim && frame < decoder_.frameCount()) {
            // Render frame
            RenderFrame(frame);

            // Delay based on FPS
            uint32_t delay_ms = 1000 / (anim->fps > 0 ? anim->fps : 12);
            vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(delay_ms));
        } else {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }

    vTaskDelete(nullptr);
}

void SimpleFrameDisplay::RenderFrame(uint16_t frame_idx)
{
    if (!panel_ || !row_buffer_ || !data_loaded_) {
        return;
    }

    // Get pixel data (after palette)
    const uint8_t* pixels = decoder_.getPixelData(frame_idx);
    if (!pixels) {
        return;
    }

    // Build RGB565 palette for this frame
    decoder_.buildPalette565(frame_idx, palette565_);

    // Render row by row (P256 format: 1 byte per pixel)
    for (uint16_t y = 0; y < FRAME_HEIGHT; y++) {
        // Decode one row using the palette
        const uint8_t* row_data = pixels + (size_t)y * FRAME_WIDTH;

        for (uint16_t x = 0; x < FRAME_WIDTH; x++) {
            row_buffer_[x] = palette565_[row_data[x]];
        }

        // Draw row to LCD at offset position
        int screen_y = offset_y_ + y;
        esp_lcd_panel_draw_bitmap(panel_,
                                  offset_x_,               // x_start
                                  screen_y,                // y_start
                                  offset_x_ + FRAME_WIDTH, // x_end
                                  screen_y + 1,            // y_end
                                  row_buffer_);
    }
}

void SimpleFrameDisplay::RenderFrameScaled(uint16_t frame_idx)
{
    // For future: implement 2x scaling if needed
    // 160x160 -> 320x320 would overflow 280x240 screen
    // So we just center the frame for now
    RenderFrame(frame_idx);
}

void SimpleFrameDisplay::ClearScreen(uint16_t color)
{
    if (!panel_ || !row_buffer_) {
        return;
    }

    // Fill row buffer with color
    for (size_t i = 0; i < ROW_BUFFER_WIDTH; i++) {
        row_buffer_[i] = color;
    }

    // Clear animation area (160x160 centered)
    for (int y = offset_y_; y < offset_y_ + FRAME_HEIGHT; y++) {
        esp_lcd_panel_draw_bitmap(panel_,
                                  offset_x_, y,
                                  offset_x_ + FRAME_WIDTH, y + 1,
                                  row_buffer_);
    }
}

} // namespace simple_frame

/*
 * Simple Frame Display - Display class using frames.bin format
 * For 200x200 P256 animations on 280x240 screen
 */

#pragma once

#include "display.h"
#include "simple_frame_decoder.h"
#include "lvgl_font.h"

#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <memory>
#include <string>
#include <atomic>

namespace simple_frame {

class SimpleFrameDisplay : public Display {
public:
    SimpleFrameDisplay(esp_lcd_panel_handle_t panel, esp_lcd_panel_io_handle_t panel_io,
                       int screen_width, int screen_height);
    virtual ~SimpleFrameDisplay();

    // Display interface
    virtual void SetEmotion(const char* emotion) override;
    virtual void SetStatus(const char* status) override;
    virtual void SetChatMessage(const char* role, const char* content) override;
    virtual void SetTheme(Theme* theme) override;
    virtual void ShowNotification(const char* notification, int duration_ms = 3000) override;
    virtual void UpdateStatusBar(bool update_all = false) override;
    virtual void SetPowerSaveMode(bool on) override;

    // Load frames.bin data (call after partition is mapped)
    bool LoadFramesData(const void* data, size_t size);

    // Set text font for labels
    void SetTextFont(std::shared_ptr<LvglFont> font);

protected:
    virtual bool Lock(int timeout_ms = 0) override;
    virtual void Unlock() override;

private:
    // Animation task
    static void AnimationTask(void* param);
    void RunAnimationLoop();

    // Render current frame to LCD
    void RenderFrame(uint16_t frame_idx);
    void RenderFrameScaled(uint16_t frame_idx);

    // Clear screen
    void ClearScreen(uint16_t color = 0x0000);

    // LCD panel
    esp_lcd_panel_handle_t panel_ = nullptr;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;

    // Frame decoder
    SimpleFrameDecoder decoder_;
    bool data_loaded_ = false;

    // Animation state
    const Animation* current_anim_ = nullptr;
    uint16_t current_frame_ = 0;
    std::atomic<bool> anim_running_{false};
    std::atomic<bool> anim_paused_{false};

    // Render buffer (one row at a time for memory efficiency)
    uint16_t* row_buffer_ = nullptr;
    static constexpr size_t ROW_BUFFER_WIDTH = FRAME_WIDTH;  // 200

    // RGB565 palette buffer (built from per-frame RGB888 palette)
    uint16_t palette565_[PALETTE_COLORS];

    // Screen dimensions
    int screen_width_ = 280;
    int screen_height_ = 240;

    // Animation offset (center 160x160 on screen)
    int offset_x_ = 0;
    int offset_y_ = 0;

    // Threading
    TaskHandle_t anim_task_ = nullptr;
    SemaphoreHandle_t mutex_ = nullptr;
    SemaphoreHandle_t render_complete_ = nullptr;

    // Status text
    std::string status_text_;
    std::string chat_message_;

    // Font
    std::shared_ptr<LvglFont> text_font_;

    // 随机穿插动画相关
    const Animation* base_anim_ = nullptr;    // 基础动画(idle/talk/listen)
    bool playing_insert_ = false;              // 是否正在播放穿插动画
};

} // namespace simple_frame

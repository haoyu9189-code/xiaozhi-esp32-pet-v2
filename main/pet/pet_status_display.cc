#include "pet_status_display.h"
#include <esp_log.h>
#include <cstdio>
#include <cstring>

#define TAG "PetStatusDisplay"

// Structure to store references to display components
typedef struct {
    lv_obj_t* canvas;      // Canvas for icons
    lv_obj_t* labels[4];   // Labels for values (hunger, cleanliness, happiness, coins)
    uint8_t* canvas_buf;   // Canvas buffer
} pet_status_data_t;

void PetStatusDisplay::DrawIcon(lv_obj_t* canvas, const uint8_t* icon_data,
                                  int x, int y, lv_color_t color) {
    // Draw 12x12 1-bit icon
    // Each row is 2 bytes (16 bits, but only 12 are used)
    for (int row = 0; row < 12; row++) {
        uint16_t row_data = (icon_data[row * 2] << 8) | icon_data[row * 2 + 1];
        for (int col = 0; col < 12; col++) {
            // Check if bit is set (MSB first)
            if (row_data & (0x8000 >> col)) {
                lv_canvas_set_px(canvas, x + col, y + row, color, LV_OPA_COVER);
            }
        }
    }
}

lv_obj_t* PetStatusDisplay::Create(lv_obj_t* parent) {
    // Create a container for the pet status
    lv_obj_t* container = lv_obj_create(parent);
    lv_obj_set_size(container, LV_SIZE_CONTENT, HEIGHT);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(container, 12, 0);  // Space between items (value to next icon)

    // Allocate data structure
    pet_status_data_t* data = (pet_status_data_t*)lv_malloc(sizeof(pet_status_data_t));
    if (!data) {
        ESP_LOGE(TAG, "Failed to allocate pet status data");
        return container;
    }
    memset(data, 0, sizeof(pet_status_data_t));

    // Calculate canvas size: 4 icons * 12 pixels wide, 12 pixels tall
    int canvas_width = ICON_SIZE * 4 + 8 * 3;  // 4 icons with 8px spacing between icon groups
    int canvas_height = HEIGHT;

    // Allocate canvas buffer (RGB565 = 2 bytes per pixel)
    size_t buf_size = canvas_width * canvas_height * 2;
    data->canvas_buf = (uint8_t*)lv_malloc(buf_size);
    if (!data->canvas_buf) {
        ESP_LOGE(TAG, "Failed to allocate canvas buffer");
        lv_free(data);
        return container;
    }
    memset(data->canvas_buf, 0, buf_size);

    // Create canvas for icons
    data->canvas = lv_canvas_create(container);
    lv_canvas_set_buffer(data->canvas, data->canvas_buf, canvas_width, canvas_height, LV_COLOR_FORMAT_RGB565);
    lv_canvas_fill_bg(data->canvas, lv_color_hex(0x000000), LV_OPA_TRANSP);
    lv_obj_set_style_pad_right(data->canvas, 4, 0);

    // Create labels for each stat value - they will be after each icon
    // Using a simpler approach: one container with icons drawn on canvas, followed by value labels
    // Format: [canvas with all icons] [label1] [label2] [label3] [label4] [label5]
    // But this doesn't align icons with values well...

    // Better approach: Create individual icon+label pairs
    lv_obj_del(data->canvas);
    lv_free(data->canvas_buf);

    // Icon data and colors for the 4 stats (hunger, cleanliness, happiness, coins)
    struct IconInfo {
        const uint8_t* data;
        lv_color_t color;
    };
    const IconInfo icons[] = {
        { nullptr,           lv_color_hex(0xFFA500) },  // hunger - special multi-color burger
        { ICON_BATH_12x12,   lv_color_hex(0x00CED1) },  // cleanliness - cyan
        { ICON_HEART_12x12,  lv_color_hex(0xFF6B6B) },  // happiness - coral red (心情)
        { ICON_COIN_12x12,   lv_color_hex(0xFFD700) }   // coins - gold (古铜钱)
    };

    for (int i = 0; i < 4; i++) {
        // Create a small container for each icon+value pair
        lv_obj_t* item = lv_obj_create(container);
        lv_obj_set_size(item, LV_SIZE_CONTENT, HEIGHT);
        lv_obj_set_style_bg_opa(item, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(item, 0, 0);
        lv_obj_set_style_pad_all(item, 0, 0);
        lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(item, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(item, 12, 0);  // Space between icon and value

        // Create small canvas for this icon
        size_t icon_buf_size = ICON_SIZE * ICON_SIZE * 4;  // ARGB8888 for transparency
        uint8_t* icon_buf = (uint8_t*)lv_malloc(icon_buf_size);
        if (icon_buf) {
            memset(icon_buf, 0, icon_buf_size);  // All zeros = fully transparent

            lv_obj_t* icon_canvas = lv_canvas_create(item);
            lv_canvas_set_buffer(icon_canvas, icon_buf, ICON_SIZE, ICON_SIZE, LV_COLOR_FORMAT_ARGB8888);

            if (i == 0) {
                // Special multi-color burger icon
                DrawIcon(icon_canvas, ICON_BURGER_TOP_12x12,    0, 0, lv_color_hex(0xFFA500));  // top bun - orange
                DrawIcon(icon_canvas, ICON_BURGER_LETTUCE_12x12, 0, 0, lv_color_hex(0x32CD32)); // lettuce - green
                DrawIcon(icon_canvas, ICON_BURGER_PATTY_12x12,  0, 0, lv_color_hex(0x8B4513));  // patty - brown
                DrawIcon(icon_canvas, ICON_BURGER_BOTTOM_12x12, 0, 0, lv_color_hex(0xFFA500));  // bottom bun - orange
            } else {
                // Draw single-color icon
                DrawIcon(icon_canvas, icons[i].data, 0, 0, icons[i].color);
            }

            // Store canvas buffer pointer as user data for cleanup
            lv_obj_set_user_data(icon_canvas, icon_buf);

            // Add event to free buffer when canvas is deleted
            lv_obj_add_event_cb(icon_canvas, [](lv_event_t* e) {
                uint8_t* buf = (uint8_t*)lv_event_get_user_data(e);
                if (buf) lv_free(buf);
            }, LV_EVENT_DELETE, icon_buf);
        }

        // Create value label (white color for all)
        data->labels[i] = lv_label_create(item);
        lv_label_set_text(data->labels[i], "0");
        lv_obj_set_style_text_color(data->labels[i], lv_color_white(), 0);
    }

    // Store data pointer for updates
    lv_obj_set_user_data(container, data);

    // Add event to free data when container is deleted
    lv_obj_add_event_cb(container, [](lv_event_t* e) {
        pet_status_data_t* d = (pet_status_data_t*)lv_event_get_user_data(e);
        if (d) lv_free(d);
    }, LV_EVENT_DELETE, data);

    return container;
}

void PetStatusDisplay::Update(lv_obj_t* container, const PetStats& stats, uint8_t coins) {
    pet_status_data_t* data = (pet_status_data_t*)lv_obj_get_user_data(container);
    if (!data) return;

    char buf[8];

    // Convert 0-100 to 0-10 scale (divide by 10, round)
    auto toScale10 = [](int8_t val) -> int {
        return (val + 5) / 10;  // Round to nearest
    };

    // Update hunger value (0-10)
    snprintf(buf, sizeof(buf), "%d", toScale10(stats.hunger));
    lv_label_set_text(data->labels[0], buf);

    // Update cleanliness value (0-10)
    snprintf(buf, sizeof(buf), "%d", toScale10(stats.cleanliness));
    lv_label_set_text(data->labels[1], buf);

    // Update happiness value (0-10) - 心情
    snprintf(buf, sizeof(buf), "%d", toScale10(stats.happiness));
    lv_label_set_text(data->labels[2], buf);

    // Update coins value (0-99, direct display)
    snprintf(buf, sizeof(buf), "%d", coins);
    lv_label_set_text(data->labels[3], buf);
}

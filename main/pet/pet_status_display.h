#ifndef PET_STATUS_DISPLAY_H
#define PET_STATUS_DISPLAY_H

#include <lvgl.h>
#include "pet_state.h"
#include "pet_icons.h"

// Pet status display widget
// Displays 5 icons with their corresponding values

class PetStatusDisplay {
public:
    // Create pet status display at given parent
    // Returns the container object
    static lv_obj_t* Create(lv_obj_t* parent);

    // Update the display with current pet stats and coin count
    static void Update(lv_obj_t* container, const PetStats& stats, uint8_t coins);

    // Draw a 12x12 1-bit icon on canvas at position
    static void DrawIcon(lv_obj_t* canvas, const uint8_t* icon_data,
                         int x, int y, lv_color_t color);

private:
    // Icon width/height constants
    static constexpr int ICON_SIZE = 12;
    static constexpr int ICON_SPACING = 4;
    static constexpr int VALUE_WIDTH = 24;
    static constexpr int ITEM_WIDTH = ICON_SIZE + ICON_SPACING + VALUE_WIDTH;
    static constexpr int TOTAL_WIDTH = ITEM_WIDTH * 5;
    static constexpr int HEIGHT = 25;  // Match BOTTOM_UI_HEIGHT
};

#endif // PET_STATUS_DISPLAY_H

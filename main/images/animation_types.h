// Animation types for multi-animation system
// 4 animations with different triggers
// Frame data is stored in assets partition and loaded via AnimationLoader

#ifndef _ANIMATION_TYPES_H_
#define _ANIMATION_TYPES_H_

#include <stdint.h>

// Animation types enum (4 types)
typedef enum {
    ANIM_TYPE_DYNAMIC1 = 0,  // Speaking - AI talking
    ANIM_TYPE_DYNAMIC2,       // Listening - user talking
    ANIM_TYPE_STATIC1,        // Breathing - idle/not speaking
    ANIM_TYPE_DYNAMIC3,       // Interaction - center touch
    ANIM_TYPE_COUNT
} AnimationType;

// Animation data structure (using frame indices instead of pointers)
// Frame data is loaded from assets partition using AnimationLoader
typedef struct {
    const uint8_t* frame_indices;   // Array of unique frame indices
    const uint8_t* bounce_indices;  // Bounce animation indices (forward + reverse)
    uint16_t frame_count;           // Number of frames
    uint16_t bounce_count;          // Number of bounce frames
    uint16_t width;                 // Image width
    uint16_t height;                // Image height
    bool is_dynamic;                // true = animate, false = static (first frame only)
} AnimationData;

#endif // _ANIMATION_TYPES_H_

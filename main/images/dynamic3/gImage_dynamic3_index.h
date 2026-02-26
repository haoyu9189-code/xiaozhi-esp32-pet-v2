// Auto-generated animation index file (tile-based system)
// Animation: dynamic3
// Total frames: 13
// Frame size: 140 x 120 (display layer handles 2x upscale)
// Format: Tile-based with 16-color palette

#ifndef _GIMAGE_DYNAMIC3_INDEX_H_
#define _GIMAGE_DYNAMIC3_INDEX_H_

#include <stdint.h>

// 图片尺寸 (display layer handles 2x upscale to 280x240)
#define GIMAGE_DYNAMIC3_WIDTH  140
#define GIMAGE_DYNAMIC3_HEIGHT 120

// 帧索引数组 - 正向播放
static const uint8_t gImage_dynamic3_frame_indices[] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 
};
#define GIMAGE_DYNAMIC3_FRAME_COUNT 13

// 帧索引数组 - 往返播放
static const uint8_t gImage_dynamic3_bounce_indices[] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 11, 10, 9, 
    8, 7, 6, 5, 4, 3, 2, 1, 0, 
};
#define GIMAGE_DYNAMIC3_BOUNCE_COUNT 25

#endif // _GIMAGE_DYNAMIC3_INDEX_H_

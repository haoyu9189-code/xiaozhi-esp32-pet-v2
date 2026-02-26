// Auto-generated animation index file
// Total frames: 9
// Image size: 280 x 240
// Format: RGB565 Little Endian (for LVGL)

#ifndef _GIMAGE_CHRISTMAS_INDEX_H_
#define _GIMAGE_CHRISTMAS_INDEX_H_

#include <stdint.h>

#include "gImage_christmas_0001.h"
#include "gImage_christmas_0002.h"
#include "gImage_christmas_0003.h"
#include "gImage_christmas_0004.h"
#include "gImage_christmas_0005.h"
#include "gImage_christmas_0006.h"
#include "gImage_christmas_0007.h"
#include "gImage_christmas_0008.h"
#include "gImage_christmas_0009.h"

// 图片尺寸
#define GIMAGE_CHRISTMAS_WIDTH  280
#define GIMAGE_CHRISTMAS_HEIGHT 240

// 图片数组 - 正向播放
static const uint8_t* gImage_christmas_frames[] = {
    gImage_christmas_0001,
    gImage_christmas_0002,
    gImage_christmas_0003,
    gImage_christmas_0004,
    gImage_christmas_0005,
    gImage_christmas_0006,
    gImage_christmas_0007,
    gImage_christmas_0008,
    gImage_christmas_0009,
};
#define GIMAGE_CHRISTMAS_FRAME_COUNT 9

// 图片数组 - 往返播放 (只排除尾帧避免重复)
static const uint8_t* gImage_christmas_bounce[] = {
    gImage_christmas_0001,
    gImage_christmas_0002,
    gImage_christmas_0003,
    gImage_christmas_0004,
    gImage_christmas_0005,
    gImage_christmas_0006,
    gImage_christmas_0007,
    gImage_christmas_0008,
    gImage_christmas_0009,
    gImage_christmas_0008,
    gImage_christmas_0007,
    gImage_christmas_0006,
    gImage_christmas_0005,
    gImage_christmas_0004,
    gImage_christmas_0003,
    gImage_christmas_0002,
    gImage_christmas_0001,
};
#define GIMAGE_CHRISTMAS_BOUNCE_COUNT 17

#endif // _GIMAGE_CHRISTMAS_INDEX_H_

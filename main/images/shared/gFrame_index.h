// Auto-generated shared frames info (tile-based system)
// Total frames: 52
// Source size: 140 x 120
// Frame size: 140 x 120 (display layer handles 2x upscale)

#ifndef _GFRAME_INDEX_H_
#define _GFRAME_INDEX_H_

#include <stdint.h>

// Frame dimensions (display layer handles 2x upscale to 280x240)
#define GFRAME_WIDTH  140
#define GFRAME_HEIGHT 120
#define GFRAME_SIZE   (140 * 120 * 2)  // RGB565
#define GFRAME_UNIQUE_COUNT 52

// Source dimensions (before upscale)
#define GFRAME_SRC_WIDTH  140
#define GFRAME_SRC_HEIGHT 120

// Tile info
#define GFRAME_TILE_SIZE 10
#define GFRAME_TILE_COUNT 6162

// Frame data is stored in assets partition
// Use AnimationLoader to access frame data

#endif // _GFRAME_INDEX_H_

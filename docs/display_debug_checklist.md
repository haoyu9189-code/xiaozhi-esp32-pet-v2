# 280x240 屏幕显示问题调试检查清单

## 目标效果回顾

```
280×240 屏幕
┌─────────────────────────────────┐ Y=0
│         黑色边框 25px            │
├─────────────────────────────────┤ Y=25
│                                 │
│   ┌─────────────────────────┐   │
│   │   背景图 base.png        │   │
│   │      280×190            │   │
│   │  ┌─────────────────┐    │   │
│   │  │   动画角色       │    │   │
│   │  │   160×128       │    │   │
│   │  │   (透明部分      │    │   │
│   │  │    露出背景)     │    │   │
│   │  └─────────────────┘    │   │
│   │                         │   │
│   └─────────────────────────┘   │
│                                 │
├─────────────────────────────────┤ Y=215
│         黑色边框 25px            │
└─────────────────────────────────┘ Y=240
```

**合成逻辑：**
```c
if (在动画区域内) {
    if (动画像素 == 紫色0xF81F) → 显示背景像素
    else → 显示动画像素
} else {
    → 显示背景像素
}
```

## 当前问题

1. **动画位置错误** - 动画移到了左上角（应该居中）
2. **透明色显示异常** - 动画背景颜色显示为紫色（应该透明显示底层背景）
3. **全黑背景** - 除了字幕UI外全黑（背景图未正确显示）

---

## 一、位置计算检查

### 1.1 当前代码中的offset计算

**文件：** `main/display/simple_frame_display.cc:26-28`

```cpp
// 当前计算（错误）
offset_x_ = (screen_width_ - 160) / 2;  // (280-160)/2 = 60
offset_y_ = (screen_height_ - 128) / 2; // (240-128)/2 = 56
```

### 1.2 正确的offset计算（需要修改）

根据目标效果：
- 背景区域：Y=25 到 Y=215，高度190px
- 动画应该居中在背景区域内，而不是整个屏幕

```cpp
// 正确计算
const int TOP_BORDER = 25;
const int BOTTOM_BORDER = 25;
const int BG_HEIGHT = 190;  // 240 - 25 - 25
const int ANIM_WIDTH = 160;
const int ANIM_HEIGHT = 128;

offset_x_ = (screen_width_ - ANIM_WIDTH) / 2;           // (280-160)/2 = 60
offset_y_ = TOP_BORDER + (BG_HEIGHT - ANIM_HEIGHT) / 2; // 25 + (190-128)/2 = 25 + 31 = 56
```

**检查项：**
- [ ] 验证 `screen_width_` 是否正确传入为 280
- [ ] 验证 `screen_height_` 是否正确传入为 240
- [ ] 添加日志打印 offset 值：`ESP_LOGI(TAG, "offset_x=%d, offset_y=%d", offset_x_, offset_y_);`
- [ ] 检查 `RenderFrame()` 中 `esp_lcd_panel_draw_bitmap()` 的坐标参数

### 1.3 渲染坐标检查

**文件：** `main/display/simple_frame_display.cc:272-279`

```cpp
// 当前渲染代码
int screen_y = offset_y_ + y;
esp_lcd_panel_draw_bitmap(panel_,
                          offset_x_,           // x_start
                          screen_y,            // y_start
                          offset_x_ + frame_width, // x_end
                          screen_y + 1,        // y_end
                          row_buffer_);
```

**检查项：**
- [ ] 确认 `esp_lcd_panel_draw_bitmap` 的坐标是否正确（x_end 是 exclusive 还是 inclusive？）
- [ ] 添加首帧渲染时的日志：`ESP_LOGI(TAG, "Rendering at (%d,%d)-(%d,%d)", x_start, y_start, x_end, y_end);`

---

## 二、透明色处理检查

### 2.1 当前透明色机制

**文件：** `main/display/simple_frame_decoder.h:21`

```cpp
struct FramesBinHeader {
    // ...
    uint8_t  bg_color_idx;  // background color palette index (背景色调色板索引)
    uint16_t palette[16];   // RGB565 palette
};
```

**问题：** 当前代码只存储了背景色索引，但没有实现透明色替换逻辑！

### 2.2 缺失的透明色处理代码

**文件：** `main/display/simple_frame_display.cc:266-270`

当前代码直接输出调色板颜色，没有透明色检查：

```cpp
// 当前代码（错误）
for (uint16_t x = 0; x < frame_width; x += 2) {
    uint8_t byte = row_data[x / 2];
    row_buffer_[x]     = palette[(byte >> 4) & 0x0F];
    row_buffer_[x + 1] = palette[byte & 0x0F];
}
```

### 2.3 应该添加的透明色处理逻辑

```cpp
// 需要添加的逻辑
const uint8_t bg_idx = decoder_.bgColorIdx();
const uint16_t transparent_color = 0xF81F;  // 紫色透明色

for (uint16_t x = 0; x < frame_width; x += 2) {
    uint8_t byte = row_data[x / 2];

    uint8_t idx0 = (byte >> 4) & 0x0F;
    uint8_t idx1 = byte & 0x0F;

    // 如果是背景色索引，替换为背景图的像素
    if (idx0 == bg_idx) {
        row_buffer_[x] = background_pixel[x];  // 需要背景图数据
    } else {
        row_buffer_[x] = palette[idx0];
    }

    if (idx1 == bg_idx) {
        row_buffer_[x + 1] = background_pixel[x + 1];
    } else {
        row_buffer_[x + 1] = palette[idx1];
    }
}
```

**检查项：**
- [ ] 确认 `bg_color_idx` 在 frames.bin 头部中的值（应该是0-15之间的调色板索引）
- [ ] 打印调色板内容：`for(int i=0;i<16;i++) ESP_LOGI(TAG, "palette[%d]=0x%04X", i, palette[i]);`
- [ ] 确认 `palette[bg_color_idx]` 是否 == 0xF81F（紫色）
- [ ] 检查是否有背景图数据的加载和访问逻辑

---

## 三、背景图加载检查

### 3.1 背景图加载路径

**文件：** `main/assets.cc:215-221`

```cpp
if (cJSON_IsString(background_image)) {
    if (!GetAssetData(background_image->valuestring, ptr, size)) {
        ESP_LOGE(TAG, "The background image file %s is not found", background_image->valuestring);
    }
    auto background_image = std::make_shared<LvglCBinImage>(ptr);
    light_theme->set_background_image(background_image);
}
```

**检查项：**
- [ ] 确认 skin.json 中是否正确配置了 `background_image: "base.png"`
- [ ] 确认 base.png 文件是否存在于 assets 分区中
- [ ] 添加日志确认背景图是否加载成功

### 3.2 背景图显示检查

**文件：** `main/display/lcd_display.cc:1075-1080`

```cpp
// Set background image
if (lvgl_theme->background_image() != nullptr) {
    lv_obj_set_style_bg_image_src(container_, lvgl_theme->background_image()->image_dsc(), 0);
} else {
    lv_obj_set_style_bg_color(container_, lvgl_theme->background_color(), 0);
}
```

**问题：** 这是 LVGL 的背景图设置，但 `SimpleFrameDisplay` 不使用 LVGL！

### 3.3 SimpleFrameDisplay 中缺失背景图支持

**当前 SimpleFrameDisplay 问题：**
1. 没有背景图数据成员
2. 没有背景图加载函数
3. `RenderFrame()` 没有背景图合成逻辑

**需要添加的成员变量：**

```cpp
// simple_frame_display.h
class SimpleFrameDisplay : public Display {
private:
    // 背景图相关
    const uint16_t* background_data_ = nullptr;  // RGB565 背景图数据
    int bg_width_ = 280;
    int bg_height_ = 190;
    int bg_offset_y_ = 25;  // 背景图在屏幕上的Y偏移
};
```

**检查项：**
- [ ] SimpleFrameDisplay 类中是否有背景图相关成员
- [ ] 是否有 `SetBackgroundImage()` 或类似函数
- [ ] 背景图数据是如何传递给 SimpleFrameDisplay 的

---

## 四、内存分配检查

### 4.1 行缓冲区分配

**文件：** `main/display/simple_frame_display.cc:30-32`

```cpp
row_buffer_ = static_cast<uint16_t*>(
    heap_caps_malloc(ROW_BUFFER_WIDTH * sizeof(uint16_t), MALLOC_CAP_DMA));
```

**计算：**
- `ROW_BUFFER_WIDTH` = 160
- 缓冲区大小 = 160 * 2 = 320 bytes

**检查项：**
- [ ] 确认 `row_buffer_` 分配成功（打印指针地址）
- [ ] 确认缓冲区大小足够（如果需要存储背景行，需要 280*2=560 bytes）
- [ ] 检查 DMA 对齐要求

### 4.2 frames.bin 数据映射

**文件：** `main/display/simple_frame_decoder.h:69-73`

```cpp
// Validate size
size_t expected_size = HEADER_SIZE + (size_t)frame_count_ * FRAME_SIZE;
if (size < expected_size) {
    return false;
}
```

**计算：**
- `HEADER_SIZE` = 44 bytes
- `FRAME_SIZE` = 160 * 128 / 2 = 10240 bytes (4-bit indexed)
- 对于1824帧：44 + 1824 * 10240 = 18,677,804 bytes ≈ 17.8 MB

**检查项：**
- [ ] 打印 frames.bin 的实际大小和期望大小
- [ ] 确认 mmap 分区大小足够
- [ ] 检查 `decoder_.load()` 返回值

### 4.3 背景图内存需求

如果需要在 RAM 中存储背景图：
- 背景图尺寸：280 × 190 × 2 = 106,400 bytes ≈ 104 KB
- 或者只存储一行：280 × 2 = 560 bytes

**检查项：**
- [ ] 检查可用 heap 大小：`ESP_LOGI(TAG, "Free heap: %d", esp_get_free_heap_size());`
- [ ] 检查 DMA 可用内存：`ESP_LOGI(TAG, "Free DMA: %d", heap_caps_get_free_size(MALLOC_CAP_DMA));`

---

## 五、显示驱动检查

### 5.1 EmoteDisplay vs SimpleFrameDisplay

当前代码库中有两个显示实现：

| 特性 | EmoteDisplay | SimpleFrameDisplay |
|------|--------------|-------------------|
| 文件 | emote_display.cc | simple_frame_display.cc |
| 引擎 | gfx_emote (esp_emote_gfx) | 直接 LCD 操作 |
| 背景 | 纯黑色 `GFX_COLOR_HEX(0x000000)` | 使用 decoder.getBgColor() |
| 动画 | bin格式动画 | frames.bin 格式 |
| UI | 支持 label, image, anim | 无 UI 元素 |

**关键问题：你使用的是哪个 Display 实现？**

**检查项：**
- [ ] 确认当前板子配置使用的是哪个 Display 类
- [ ] 查看 board 配置文件中的 display 初始化代码

### 5.2 检查板子配置

搜索板子配置文件中的 display 初始化：

```bash
grep -r "EmoteDisplay\|SimpleFrameDisplay" main/boards/
```

**检查项：**
- [ ] 找到当前板子的配置文件
- [ ] 确认使用的 Display 类型
- [ ] 确认传递给 Display 构造函数的参数（width, height）

### 5.3 gfx_emote 背景设置

**文件：** `main/display/emote_display.cc:234`

```cpp
gfx_emote_set_bg_color(engine_handle, GFX_COLOR_HEX(0x000000));  // 黑色背景
```

**问题：** 如果使用 EmoteDisplay，背景被硬编码为黑色！

**检查项：**
- [ ] 是否需要修改为支持背景图
- [ ] gfx_emote 库是否支持背景图
- [ ] 查看 `gfx_emote_set_bg_image()` 或类似 API

---

## 六、合成逻辑实现方案

### 6.1 方案A：逐行合成（推荐）

内存占用最小，每次只处理一行：

```cpp
void SimpleFrameDisplay::RenderFrameWithBackground(uint16_t frame_idx)
{
    const uint8_t* frame_data = decoder_.getFrameData(frame_idx);
    const uint16_t* palette = decoder_.palette();
    const uint8_t bg_idx = decoder_.bgColorIdx();

    const int TOP_BORDER = 25;
    const int ANIM_START_Y = offset_y_;  // 动画在屏幕上的起始Y
    const int ANIM_END_Y = offset_y_ + 128;

    // 处理每一行
    for (int screen_y = TOP_BORDER; screen_y < 215; screen_y++) {
        // 加载这一行的背景数据
        LoadBackgroundRow(screen_y - TOP_BORDER, bg_row_buffer_);  // 280 pixels

        // 检查是否在动画区域内
        if (screen_y >= ANIM_START_Y && screen_y < ANIM_END_Y) {
            int anim_y = screen_y - ANIM_START_Y;
            const uint8_t* anim_row = frame_data + anim_y * 80;  // 160/2 = 80 bytes per row

            // 合成动画到背景
            for (int x = 0; x < 160; x += 2) {
                uint8_t byte = anim_row[x / 2];
                uint8_t idx0 = (byte >> 4) & 0x0F;
                uint8_t idx1 = byte & 0x0F;

                int bg_x = offset_x_ + x;

                if (idx0 != bg_idx) {
                    bg_row_buffer_[bg_x] = palette[idx0];
                }
                if (idx1 != bg_idx) {
                    bg_row_buffer_[bg_x + 1] = palette[idx1];
                }
            }
        }

        // 输出合成后的行
        esp_lcd_panel_draw_bitmap(panel_, 0, screen_y, 280, screen_y + 1, bg_row_buffer_);
    }
}
```

**内存需求：**
- `bg_row_buffer_`: 280 * 2 = 560 bytes (DMA)

### 6.2 方案B：使用背景图分区映射

如果背景图存储在 flash 中并通过 mmap 访问：

```cpp
// 初始化时
const uint16_t* bg_data_;  // mmap 映射的背景图指针
int bg_width_ = 280;
int bg_height_ = 190;

// 渲染时直接访问
inline uint16_t GetBackgroundPixel(int x, int y) {
    return bg_data_[y * bg_width_ + x];
}
```

**优点：** 无需额外 RAM
**缺点：** flash 读取速度较慢

### 6.3 方案C：全帧缓冲

需要大量内存，不推荐：

```cpp
// 全屏缓冲
uint16_t* frame_buffer_;  // 280 * 240 * 2 = 134,400 bytes = 131 KB
```

---

## 七、调试日志添加建议

### 7.1 初始化阶段日志

```cpp
// simple_frame_display.cc 构造函数中添加
ESP_LOGI(TAG, "=== SimpleFrameDisplay Init ===");
ESP_LOGI(TAG, "Screen: %dx%d", screen_width_, screen_height_);
ESP_LOGI(TAG, "Animation offset: (%d, %d)", offset_x_, offset_y_);
ESP_LOGI(TAG, "Row buffer: %p, size=%d", row_buffer_, ROW_BUFFER_WIDTH * 2);
ESP_LOGI(TAG, "Free heap: %lu", esp_get_free_heap_size());
ESP_LOGI(TAG, "Free DMA: %lu", heap_caps_get_free_size(MALLOC_CAP_DMA));
```

### 7.2 数据加载日志

```cpp
// LoadFramesData() 中添加
ESP_LOGI(TAG, "=== Frames Data Loaded ===");
ESP_LOGI(TAG, "Frame size: %dx%d", decoder_.width(), decoder_.height());
ESP_LOGI(TAG, "Frame count: %d", decoder_.frameCount());
ESP_LOGI(TAG, "BG color index: %d", decoder_.bgColorIdx());
ESP_LOGI(TAG, "BG color value: 0x%04X", decoder_.getBgColor());

// 打印调色板
for (int i = 0; i < 16; i++) {
    ESP_LOGI(TAG, "Palette[%2d] = 0x%04X", i, decoder_.palette()[i]);
}
```

### 7.3 渲染日志（仅首帧）

```cpp
// RenderFrame() 开始处添加
static bool first_frame = true;
if (first_frame) {
    ESP_LOGI(TAG, "=== First Frame Render ===");
    ESP_LOGI(TAG, "Frame idx: %d", frame_idx);
    ESP_LOGI(TAG, "Drawing at X: %d-%d", offset_x_, offset_x_ + 160);
    ESP_LOGI(TAG, "Drawing at Y: %d-%d", offset_y_, offset_y_ + 128);
    first_frame = false;
}
```

---

## 八、修复步骤总结

### 第一步：添加调试日志
1. 添加上述所有调试日志
2. 编译运行，收集日志信息
3. 确认当前使用的 Display 类型

### 第二步：修复位置问题
1. 检查 offset 计算是否正确
2. 考虑是否需要调整为背景区域居中

### 第三步：实现背景图支持
1. 添加背景图加载接口
2. 添加背景图数据成员
3. 实现背景行读取函数

### 第四步：实现透明色合成
1. 确认透明色索引值
2. 修改 `RenderFrame()` 添加合成逻辑
3. 使用方案A（逐行合成）实现

### 第五步：内存优化
1. 将行缓冲区扩大到 280 pixels
2. 确保 DMA 对齐
3. 测试内存使用情况

---

## 九、快速验证测试

### 测试1：纯色背景测试

临时将背景替换为纯色，验证动画位置是否正确：

```cpp
// 临时测试代码
void SimpleFrameDisplay::RenderFrame(uint16_t frame_idx)
{
    // 先填充整个背景区域为灰色
    uint16_t gray = 0x7BEF;  // RGB565 灰色
    for (int y = 25; y < 215; y++) {
        for (int i = 0; i < 280; i++) {
            wide_row_buffer_[i] = gray;
        }
        esp_lcd_panel_draw_bitmap(panel_, 0, y, 280, y + 1, wide_row_buffer_);
    }

    // 然后绘制动画（不透明）
    // ... 原有绘制代码 ...
}
```

### 测试2：打印首帧数据

```cpp
// 打印第一帧前几个字节的数据
const uint8_t* frame_data = decoder_.getFrameData(0);
ESP_LOG_BUFFER_HEX_LEVEL(TAG, frame_data, 32, ESP_LOG_INFO);
```

### 测试3：验证透明色索引

```cpp
// 统计透明色像素数量
int transparent_count = 0;
const uint8_t* frame_data = decoder_.getFrameData(0);
uint8_t bg_idx = decoder_.bgColorIdx();

for (int i = 0; i < 160 * 128 / 2; i++) {
    uint8_t byte = frame_data[i];
    if (((byte >> 4) & 0x0F) == bg_idx) transparent_count++;
    if ((byte & 0x0F) == bg_idx) transparent_count++;
}

ESP_LOGI(TAG, "Transparent pixels in frame 0: %d / %d (%.1f%%)",
         transparent_count, 160 * 128,
         100.0f * transparent_count / (160 * 128));
```

---

## 十、关键文件清单

| 文件 | 作用 | 需要修改 |
|------|------|---------|
| `main/display/simple_frame_display.h` | 显示类头文件 | 添加背景图成员 |
| `main/display/simple_frame_display.cc` | 显示实现 | 添加合成逻辑 |
| `main/display/simple_frame_decoder.h` | 帧解码器 | 可能需要扩展 |
| `main/display/emote_display.cc` | Emote显示 | 如果使用此类需修改 |
| `main/assets.cc` | 资源加载 | 确认背景图加载 |
| Board配置文件 | 板子配置 | 确认Display类型 |

---

## 十一、紫色(0xF81F)问题分析

### 为什么动画背景变成紫色？

**可能原因1：调色板中紫色被当作透明色使用**
- frames.bin 的调色板中，`bg_color_idx` 对应的颜色是 0xF81F
- 当没有背景图时，透明像素直接显示调色板颜色 = 紫色

**可能原因2：LVGL透明色约定**
- LVGL 使用 0xF81F (LV_COLOR_CHROMA_KEY) 作为色键透明色
- 如果某处代码错误地使用了这个值

**验证方法：**
```cpp
ESP_LOGI(TAG, "BG color idx: %d", decoder_.bgColorIdx());
ESP_LOGI(TAG, "Palette[bg_idx]: 0x%04X", decoder_.getBgColor());
// 如果输出 0xF81F，说明紫色就是透明色标记
```

**解决方案：**
在合成时，检测到透明色索引时，应该用背景图像素替换，而不是用调色板颜色。

---

*文档版本: 1.0*
*创建日期: 2025-01-16*
